#pragma once
#include <optional>

namespace sysintel {

struct BatterySnapshot {
    bool battery_present = false;
    bool on_ac_power = false;
    bool charging = false;
    int charge_percent = -1;               // 0-100, -1 if unknown
    std::optional<double> rate_watts;      // negative = discharging, positive = charging
    std::optional<long> estimated_seconds_remaining;
};

BatterySnapshot get_battery_snapshot();

}  // namespace sysintel
