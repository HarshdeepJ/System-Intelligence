#include "sampler.hpp"

#include <iostream>
#include <vector>

#include "../collectors/battery.hpp"
#include "../collectors/cpu.hpp"
#include "../collectors/memory.hpp"

namespace sysintel {

namespace {

constexpr auto kBatterySampleInterval = std::chrono::seconds(5);
constexpr auto kMemorySampleInterval = std::chrono::seconds(5);
constexpr auto kFlushInterval = std::chrono::seconds(5);
// CPU has no separate interval constant: get_cpu_snapshot() already blocks
// for ~1s to take its two-sample reading, so every loop tick naturally
// samples CPU roughly once a second -- that blocking call is also what
// paces this whole loop, so there's no separate sleep() needed here.

}  // namespace

Sampler::Sampler(SqliteStore& store) : store_(store) {}

void Sampler::request_stop() {
    stop_requested_.store(true);
}

void Sampler::run() {
    std::vector<MetricSample> buffer;

    auto last_battery = std::chrono::steady_clock::now() - kBatterySampleInterval;
    auto last_memory = std::chrono::steady_clock::now() - kMemorySampleInterval;
    auto last_flush = std::chrono::steady_clock::now();

    while (!stop_requested_.load()) {
        auto now = std::chrono::steady_clock::now();

        CpuSnapshot cpu = get_cpu_snapshot();
        buffer.push_back(
            {"cpu.utilization", cpu.total_utilization_percent, "percent", current_timestamp_ms()});

        if (now - last_battery >= kBatterySampleInterval) {
            BatterySnapshot battery = get_battery_snapshot();
            if (battery.battery_present) {
                buffer.push_back({"battery.charge_percent",
                                   static_cast<double>(battery.charge_percent), "percent",
                                   current_timestamp_ms()});
                if (battery.rate_watts.has_value()) {
                    buffer.push_back(
                        {"battery.rate_watts", *battery.rate_watts, "W", current_timestamp_ms()});
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

        if (now - last_flush >= kFlushInterval) {
            store_.insert_batch(buffer);
            std::cout << "[recorder] flushed " << buffer.size() << " samples\n";
            buffer.clear();
            last_flush = now;
        }
    }

    if (!buffer.empty()) {
        store_.insert_batch(buffer);
    }
}

}  // namespace sysintel
