#include "../model/thermal_state.hpp"

#include <string>

#include "../providers/windows/wmi_client.hpp"

namespace sysintel {

namespace {

double kelvin_tenths_to_celsius(double tenths_kelvin) {
    return (tenths_kelvin / 10.0) - 273.15;
}

}  // namespace

ThermalAndFanState get_thermal_and_fan_state() {
    ThermalAndFanState result;

    // Thermal zones live in ROOT\WMI, not ROOT\CIMV2 where most
    // inventory/GPU classes live -- ACPI exposes them under a different
    // WMI namespace entirely.
    WmiClient wmi_zones("ROOT\\WMI");
    if (wmi_zones.ok()) {
        auto rows = wmi_zones.query(
            "SELECT InstanceName, CurrentTemperature FROM MSAcpi_ThermalZoneTemperature");
        for (const auto& row : rows) {
            ThermalZoneState zone;
            auto name_it = row.find("InstanceName");
            zone.name = (name_it != row.end() && !name_it->second.empty()) ? name_it->second
                                                                            : "thermal zone";

            auto temp_it = row.find("CurrentTemperature");
            if (temp_it != row.end() && !temp_it->second.empty()) {
                try {
                    double tenths_kelvin = std::stod(temp_it->second);
                    zone.temperature_celsius =
                        Reading<double>::ok(kelvin_tenths_to_celsius(tenths_kelvin));
                } catch (...) {
                    zone.temperature_celsius = Reading<double>::unavailable();
                }
            } else {
                zone.temperature_celsius = Reading<double>::unavailable();
            }
            result.thermal_zones.push_back(std::move(zone));
        }
    }

    if (result.thermal_zones.empty()) {
        // Either the ROOT\WMI namespace/class wasn't queryable at all, or
        // it returned zero instances -- either way, genuinely unsupported
        // on this machine, not just "field empty".
        ThermalZoneState zone;
        zone.name = "cpu";
        zone.temperature_celsius = Reading<double>::unsupported();
        result.thermal_zones.push_back(std::move(zone));
    }

    // Win32_Fan (ROOT\CIMV2) is notoriously unreliable across OEMs -- most
    // laptops report nothing usable here, per the PRD's own warning.
    WmiClient wmi_cimv2;
    if (wmi_cimv2.ok()) {
        auto rows = wmi_cimv2.query("SELECT Name, DesiredSpeed FROM Win32_Fan");
        for (const auto& row : rows) {
            FanState fan;
            auto name_it = row.find("Name");
            fan.name = (name_it != row.end() && !name_it->second.empty()) ? name_it->second : "fan";

            auto speed_it = row.find("DesiredSpeed");
            if (speed_it != row.end() && !speed_it->second.empty()) {
                try {
                    fan.rpm = Reading<int>::ok(std::stoi(speed_it->second));
                } catch (...) {
                    fan.rpm = Reading<int>::unavailable();
                }
            } else {
                fan.rpm = Reading<int>::unavailable();
            }
            result.fans.push_back(std::move(fan));
        }
    }

    if (result.fans.empty()) {
        FanState fan;
        fan.name = "fan";
        fan.rpm = Reading<int>::unsupported();
        result.fans.push_back(std::move(fan));
    }

    return result;
}

}  // namespace sysintel
