#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

namespace sysintel {

// A discrete "something changed" fact, distinct from SystemState's
// continuous readings. See tech design's Normalized Event Schema.
struct SystemEvent {
    std::string type;  // e.g. "process.started", "power.ac_connected"
    int64_t timestamp_ms = 0;
    std::unordered_map<std::string, std::string> data;
};

}  // namespace sysintel
