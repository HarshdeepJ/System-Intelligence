#include "event_detector.hpp"

#include "../storage/metric_sample.hpp"

namespace sysintel {

std::vector<SystemEvent> SystemEventDetector::detect(
    const std::vector<ProcessIdentity>& processes, bool battery_present, bool on_ac_power) {
    std::vector<SystemEvent> events;

    std::unordered_map<uint32_t, std::string> current_pids;
    current_pids.reserve(processes.size());
    for (const auto& p : processes) {
        current_pids[p.pid] = p.name;
    }

    if (!has_baseline_) {
        // Nothing to diff against yet -- just establish the baseline.
        known_pids_ = std::move(current_pids);
        last_on_ac_power_ = on_ac_power;
        has_baseline_ = true;
        return events;
    }

    int64_t now_ms = current_timestamp_ms();

    for (const auto& [pid, name] : current_pids) {
        if (known_pids_.find(pid) == known_pids_.end()) {
            SystemEvent event;
            event.type = "process.started";
            event.timestamp_ms = now_ms;
            event.data["pid"] = std::to_string(pid);
            event.data["name"] = name;
            events.push_back(std::move(event));
        }
    }

    for (const auto& [pid, name] : known_pids_) {
        if (current_pids.find(pid) == current_pids.end()) {
            SystemEvent event;
            event.type = "process.stopped";
            event.timestamp_ms = now_ms;
            event.data["pid"] = std::to_string(pid);
            event.data["name"] = name;
            events.push_back(std::move(event));
        }
    }

    if (battery_present && on_ac_power != last_on_ac_power_) {
        SystemEvent event;
        event.type = on_ac_power ? "power.ac_connected" : "power.ac_disconnected";
        event.timestamp_ms = now_ms;
        events.push_back(std::move(event));
    }

    known_pids_ = std::move(current_pids);
    last_on_ac_power_ = on_ac_power;
    return events;
}

}  // namespace sysintel
