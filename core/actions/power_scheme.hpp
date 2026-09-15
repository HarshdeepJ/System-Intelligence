#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <optional>
#include <string>
#include <vector>

namespace sysintel {

struct PowerScheme {
    GUID guid{};
    std::string guid_string;
    std::string friendly_name;
};

std::string guid_to_string(const GUID& guid);
bool guid_from_string(const std::string& s, GUID& out);

// Every power scheme currently registered on this machine. Deliberately not
// a hardcoded list of "well-known" scheme GUIDs -- those aren't guaranteed
// stable across Windows versions/OEMs, so this asks Windows directly.
std::vector<PowerScheme> enumerate_power_schemes();

// The scheme Windows is currently using, or nullopt if it couldn't be read.
std::optional<PowerScheme> get_active_power_scheme();

// Switches to the given scheme and verifies the switch actually took by
// reading the active scheme back afterward.
bool set_active_power_scheme(const GUID& guid);

// Finds the first scheme whose friendly name contains `keyword`
// (case-insensitive) -- how a plain-English request like "power saver"
// gets mapped to whatever GUID this specific machine actually uses for it.
std::optional<PowerScheme> find_power_scheme_by_keyword(const std::vector<PowerScheme>& schemes,
                                                         const std::string& keyword);

// Windows 11's "Power Mode" slider (Settings > System > Power) -- a much
// more reliable action target than classic multi-scheme switching, since it
// works even when a machine has only one classic scheme registered (true on
// this project's own dev machine: `powercfg /list` shows only "Balanced").
//
// These overlay GUIDs are NOT the classic GUID_MAX_POWER_SAVINGS-style
// scheme-template constants (an earlier version of this code assumed they
// were reused here -- verified wrong empirically). They were instead
// discovered by asking Windows directly: enumerating with
// ACCESS_OVERLAY_SCHEME and reading each one's friendly name back
// (see debug_enumerate_overlay_schemes()), then cross-checking that the
// "Better Battery-life Overlay" GUID it reported matched what
// PowerGetUserConfiguredDCPowerMode() actually returned live on this
// machine. Only kBestPowerEfficiency has that live cross-check; treat
// kBestPerformance as inferred-from-name and lower-confidence.
enum class PowerModeLevel { kBestPowerEfficiency, kBestPerformance };

GUID power_mode_guid(PowerModeLevel level);
const char* power_mode_name(PowerModeLevel level);
std::optional<PowerModeLevel> power_mode_from_name(const std::string& name);

// The raw GUID string Windows currently has configured for battery (DC)
// power mode -- always available and exact, even when it doesn't match
// either named PowerModeLevel above (e.g. "Balanced"/no-overlay, or an
// OEM-specific overlay this code doesn't have a name for). This is what
// rollback should capture and restore: the *exact* prior state, not an
// approximation of it.
std::string get_dc_power_mode_raw_guid();

bool set_dc_power_mode(PowerModeLevel level);
bool set_dc_power_mode_raw_guid(const std::string& guid_string);

std::vector<PowerScheme> debug_enumerate_overlay_schemes();

}  // namespace sysintel
