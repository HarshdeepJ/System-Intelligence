#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "availability.hpp"

namespace sysintel {

struct NetworkAdapterState {
    std::string name;
    std::string type;  // "ethernet" | "wifi"
    bool operational = false;
    Reading<uint64_t> link_speed_bps;
    Reading<double> bytes_sent_per_sec;
    Reading<double> bytes_received_per_sec;
    // Only meaningful for type == "wifi"; kUnsupported for everything else.
    Reading<int> wifi_signal_percent;
};

// Takes ~1 second: computes throughput from two samples spaced apart, the
// same trick the CPU collector uses for its rate counter.
std::vector<NetworkAdapterState> get_network_state();

}  // namespace sysintel
