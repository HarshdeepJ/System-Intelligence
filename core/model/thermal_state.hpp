#pragma once
#include <string>
#include <vector>

#include "availability.hpp"

namespace sysintel {

struct ThermalZoneState {
    std::string name;
    Reading<double> temperature_celsius;
};

struct FanState {
    std::string name;
    Reading<int> rpm;
};

struct ThermalAndFanState {
    std::vector<ThermalZoneState> thermal_zones;
    std::vector<FanState> fans;
};

// The domain the PRD itself warns is the most OEM-fragmented -- expect
// `unsupported` more often than not. Worth trying properly (not skipping
// entirely) specifically because Reading<T> exists to make that outcome
// distinguishable from an error.
ThermalAndFanState get_thermal_and_fan_state();

}  // namespace sysintel
