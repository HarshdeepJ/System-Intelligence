#pragma once
#include <unordered_map>
#include <vector>

#include "../collectors/process.hpp"
#include "../model/system_event.hpp"

namespace sysintel {

// Synthesizes discrete events by diffing consecutive polled snapshots --
// no ETW needed for v1. Reuses the process collector's full PID list (see
// get_all_process_identities) rather than adding a new Windows API surface;
// true ETW-based process tracking (lower latency, catches processes that
// start and exit between poll ticks) is a later upgrade, not needed yet.
class SystemEventDetector {
public:
    // Compares the given readings to the previous call and returns whatever
    // changed. The very first call establishes the baseline and returns no
    // events (there's nothing to diff against yet).
    std::vector<SystemEvent> detect(const std::vector<ProcessIdentity>& processes,
                                     bool battery_present, bool on_ac_power);

private:
    bool has_baseline_ = false;
    std::unordered_map<uint32_t, std::string> known_pids_;
    bool last_on_ac_power_ = false;
};

}  // namespace sysintel
