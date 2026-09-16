#include "sampler.hpp"

#include <algorithm>
#include <iostream>
#include <vector>

#include "../collectors/battery.hpp"
#include "../collectors/cpu.hpp"
#include "../collectors/memory.hpp"
#include "../collectors/process.hpp"
#include "../model/disk_io_state.hpp"
#include "../model/network_state.hpp"
#include "../model/thermal_state.hpp"

namespace sysintel {

namespace {

constexpr auto kBatterySampleInterval = std::chrono::seconds(5);
constexpr auto kMemorySampleInterval = std::chrono::seconds(5);
constexpr auto kNetworkSampleInterval = std::chrono::seconds(5);
constexpr auto kDiskSampleInterval = std::chrono::seconds(5);
constexpr auto kTopCpuSampleInterval = std::chrono::seconds(5);
constexpr auto kThermalSampleInterval = std::chrono::seconds(5);
constexpr auto kFlushInterval = std::chrono::seconds(5);
// Network, disk, and top-cpu sampling each block for ~1s internally (the
// same two-sample rate trick as CPU), so a tick that samples any of them
// takes noticeably longer than a normal one -- fine at a 5s interval on a
// recording daemon nobody's timing with a stopwatch, not fine if done every
// tick. Thermal is a plain WMI query, no blocking.
// CPU has no separate interval constant: get_cpu_snapshot() already blocks
// for ~1s to take its two-sample reading, so every loop tick naturally
// samples CPU roughly once a second -- that blocking call is also what
// paces this whole loop, so there's no separate sleep() needed here.

}  // namespace

Sampler::Sampler(SqliteStore& store) : store_(store) {}

void Sampler::request_stop() {
    stop_requested_.store(true);
}

void Sampler::set_periodic_hook(std::chrono::seconds interval, std::function<void()> hook) {
    hook_interval_ = interval;
    hook_ = std::move(hook);
}

void Sampler::run() {
    std::vector<MetricSample> buffer;
    std::vector<SystemEvent> event_buffer;

    auto last_battery = std::chrono::steady_clock::now() - kBatterySampleInterval;
    auto last_memory = std::chrono::steady_clock::now() - kMemorySampleInterval;
    auto last_network = std::chrono::steady_clock::now() - kNetworkSampleInterval;
    auto last_disk = std::chrono::steady_clock::now() - kDiskSampleInterval;
    auto last_top_cpu = std::chrono::steady_clock::now() - kTopCpuSampleInterval;
    auto last_thermal = std::chrono::steady_clock::now() - kThermalSampleInterval;
    auto last_flush = std::chrono::steady_clock::now();
    auto last_hook = std::chrono::steady_clock::now();

    bool known_battery_present = false;
    bool known_on_ac_power = true;

    while (!stop_requested_.load()) {
        auto now = std::chrono::steady_clock::now();

        CpuSnapshot cpu = get_cpu_snapshot();
        buffer.push_back(
            {"cpu.utilization", cpu.total_utilization_percent, "percent", current_timestamp_ms()});

        // Cheap enough to call every tick (~1s): no per-process memory
        // query, just a PID/name walk, purely to diff against last tick.
        auto processes = get_all_process_identities();
        auto new_events =
            event_detector_.detect(processes, known_battery_present, known_on_ac_power);
        event_buffer.insert(event_buffer.end(), new_events.begin(), new_events.end());

        if (now - last_battery >= kBatterySampleInterval) {
            BatterySnapshot battery = get_battery_snapshot();
            known_battery_present = battery.battery_present;
            known_on_ac_power = battery.on_ac_power;
            if (battery.battery_present) {
                buffer.push_back({"battery.charge_percent",
                                   static_cast<double>(battery.charge_percent), "percent",
                                   current_timestamp_ms()});
                if (battery.rate_watts.has_value()) {
                    buffer.push_back(
                        {"battery.rate_watts", *battery.rate_watts, "W", current_timestamp_ms()});

                    // A dedicated "how hard is the battery discharging right now"
                    // series, only emitted while actually discharging. This does
                    // double duty as the on-battery gate: the anomaly detector
                    // never needs a separate "on_battery" flag -- if there are no
                    // recent rows in this series, we're simply not discharging.
                    if (!battery.on_ac_power && *battery.rate_watts < 0) {
                        buffer.push_back({"battery.discharge_watts", -(*battery.rate_watts), "W",
                                           current_timestamp_ms()});
                    }
                }
            }
            last_battery = now;
        }

        if (now - last_memory >= kMemorySampleInterval) {
            MemorySnapshot mem = get_memory_snapshot();
            buffer.push_back({"memory.load_percent",
                               static_cast<double>(mem.memory_load_percent), "percent",
                               current_timestamp_ms()});
            last_memory = now;
        }

        if (now - last_network >= kNetworkSampleInterval) {
            double total_sent = 0.0;
            double total_received = 0.0;
            for (const auto& adapter : get_network_state()) {
                if (!adapter.operational) {
                    continue;
                }
                if (adapter.bytes_sent_per_sec.value.has_value()) {
                    total_sent += *adapter.bytes_sent_per_sec.value;
                }
                if (adapter.bytes_received_per_sec.value.has_value()) {
                    total_received += *adapter.bytes_received_per_sec.value;
                }
            }
            int64_t ts = current_timestamp_ms();
            buffer.push_back({"network.bytes_sent_per_sec", total_sent, "B/s", ts});
            buffer.push_back({"network.bytes_received_per_sec", total_received, "B/s", ts});
            // One combined number to baseline/alert on -- "which direction"
            // doesn't matter as much as "is there unusual network activity
            // at all," and this avoids needing two separate incident
            // domains for one collector.
            buffer.push_back({"network.total_bytes_per_sec", total_sent + total_received, "B/s", ts});
            last_network = now;
        }

        if (now - last_disk >= kDiskSampleInterval) {
            double total_read = 0.0;
            double total_write = 0.0;
            for (const auto& disk : get_disk_io_state()) {
                if (disk.read_bytes_per_sec.value.has_value()) {
                    total_read += *disk.read_bytes_per_sec.value;
                }
                if (disk.write_bytes_per_sec.value.has_value()) {
                    total_write += *disk.write_bytes_per_sec.value;
                }
            }
            int64_t ts = current_timestamp_ms();
            buffer.push_back({"disk.read_bytes_per_sec", total_read, "B/s", ts});
            buffer.push_back({"disk.write_bytes_per_sec", total_write, "B/s", ts});
            buffer.push_back({"disk.total_bytes_per_sec", total_read + total_write, "B/s", ts});
            last_disk = now;
        }

        if (now - last_top_cpu >= kTopCpuSampleInterval) {
            // The single busiest process's CPU% right now -- a scalar
            // proxy for "is one process monopolizing the CPU," distinct
            // from cpu.utilization's system-wide total. Per-process CPU
            // itself doesn't reduce to one time series (it's a ranked list
            // of many short-lived instances), but its *maximum* does.
            auto top = get_top_processes_by_cpu(1);
            double top_cpu_percent = top.empty() ? 0.0 : top.front().cpu_percent;
            buffer.push_back(
                {"process.top_cpu_percent", top_cpu_percent, "percent", current_timestamp_ms()});
            last_top_cpu = now;
        }

        if (now - last_thermal >= kThermalSampleInterval) {
            // Only recorded when at least one zone/fan actually reports a
            // value -- on hardware where thermal is unsupported/unavailable
            // (the common case per Phase 8's findings), this simply
            // contributes nothing, the same way battery.discharge_watts
            // contributes nothing while on AC.
            ThermalAndFanState thermal = get_thermal_and_fan_state();
            int64_t ts = current_timestamp_ms();

            double max_temp = 0.0;
            bool have_temp = false;
            for (const auto& zone : thermal.thermal_zones) {
                if (zone.temperature_celsius.value.has_value()) {
                    max_temp = std::max(max_temp, *zone.temperature_celsius.value);
                    have_temp = true;
                }
            }
            if (have_temp) {
                buffer.push_back({"thermal.cpu_temp_celsius", max_temp, "C", ts});
            }

            int max_rpm = 0;
            bool have_rpm = false;
            for (const auto& fan : thermal.fans) {
                if (fan.rpm.value.has_value()) {
                    max_rpm = std::max(max_rpm, *fan.rpm.value);
                    have_rpm = true;
                }
            }
            if (have_rpm) {
                buffer.push_back({"thermal.fan_rpm", static_cast<double>(max_rpm), "rpm", ts});
            }
            last_thermal = now;
        }

        if (now - last_flush >= kFlushInterval) {
            store_.insert_batch(buffer);
            std::cout << "[recorder] flushed " << buffer.size() << " samples";
            if (!event_buffer.empty()) {
                store_.insert_events(event_buffer);
                std::cout << ", " << event_buffer.size() << " events";
                event_buffer.clear();
            }
            std::cout << "\n";
            buffer.clear();
            last_flush = now;
        }

        if (hook_ && hook_interval_.count() > 0 && now - last_hook >= hook_interval_) {
            // Flush first, so the hook (e.g. an anomaly check) sees the
            // freshest possible data rather than whatever's still buffered.
            if (!buffer.empty()) {
                store_.insert_batch(buffer);
                buffer.clear();
                last_flush = now;
            }
            if (!event_buffer.empty()) {
                store_.insert_events(event_buffer);
                event_buffer.clear();
            }
            hook_();
            last_hook = now;
        }
    }

    if (!buffer.empty()) {
        store_.insert_batch(buffer);
    }
    if (!event_buffer.empty()) {
        store_.insert_events(event_buffer);
    }
}

}  // namespace sysintel
