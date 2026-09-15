#pragma once
#include <chrono>
#include <cstdint>
#include <string>

namespace sysintel {

// The unified telemetry shape: every metric from every collector ends up as
// one of these, regardless of which Windows API produced it.
struct MetricSample {
    std::string metric;
    double value;
    std::string unit;
    int64_t timestamp_ms;  // milliseconds since Unix epoch
};

inline int64_t current_timestamp_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}  // namespace sysintel
