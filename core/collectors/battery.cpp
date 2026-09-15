#include "battery.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <powrprof.h>

namespace sysintel {

BatterySnapshot get_battery_snapshot() {
    BatterySnapshot snap;

    SYSTEM_POWER_STATUS sps{};
    if (GetSystemPowerStatus(&sps)) {
        snap.on_ac_power = (sps.ACLineStatus == 1);
        snap.battery_present = (sps.BatteryFlag != 128) && (sps.BatteryFlag != 255);
        if (sps.BatteryLifePercent != 255) {
            snap.charge_percent = sps.BatteryLifePercent;
        }
        snap.charging = (sps.BatteryFlag & 8) != 0;
        if (sps.BatteryLifeTime != static_cast<DWORD>(-1)) {
            snap.estimated_seconds_remaining = static_cast<long>(sps.BatteryLifeTime);
        }
    }

    // SYSTEM_BATTERY_STATE gives us Rate in milliwatts directly, which is a much
    // better signal for "discharge power" than anything derived indirectly.
    SYSTEM_BATTERY_STATE sbs{};
    LONG status = CallNtPowerInformation(
        SystemBatteryState, nullptr, 0, &sbs, sizeof(sbs));

    if (status == 0 /* STATUS_SUCCESS */ && sbs.BatteryPresent) {
        snap.battery_present = true;
        // Rate is declared DWORD in this SDK, but Windows actually stores a
        // signed mW value in it (negative = discharging) as a two's-complement
        // bit pattern, so it must be reinterpreted as signed before use.
        snap.rate_watts = static_cast<double>(static_cast<LONG>(sbs.Rate)) / 1000.0;
        snap.charging = sbs.Charging != 0;
    }

    return snap;
}

}  // namespace sysintel
