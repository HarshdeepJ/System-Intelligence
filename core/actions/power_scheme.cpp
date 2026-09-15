#include "power_scheme.hpp"

#include <powrprof.h>

#include <cstdio>
#include <cstring>

#include "../util/strings.hpp"

namespace sysintel {

namespace {

std::string read_friendly_name(const GUID& guid) {
    DWORD size = 0;
    // First call with a null buffer just to learn the required size.
    PowerReadFriendlyName(nullptr, &guid, nullptr, nullptr, nullptr, &size);
    if (size == 0) {
        return "";
    }

    std::vector<UCHAR> buffer(size);
    if (PowerReadFriendlyName(nullptr, &guid, nullptr, nullptr, buffer.data(), &size) !=
        ERROR_SUCCESS) {
        return "";
    }

    // The buffer holds a null-terminated UTF-16 string (power scheme names
    // are stored as Unicode registry values regardless of build charset).
    auto* wide = reinterpret_cast<wchar_t*>(buffer.data());
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return "";
    }
    std::string result(static_cast<size_t>(len - 1), '\0');  // len includes the null terminator
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), len, nullptr, nullptr);
    return result;
}

}  // namespace

std::string guid_to_string(const GUID& guid) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX",
                  guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2],
                  guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
    return buf;
}

bool guid_from_string(const std::string& s, GUID& out) {
    unsigned long d1 = 0;
    unsigned int d2 = 0, d3 = 0;
    unsigned int d4[8] = {0};
    int matched = std::sscanf(s.c_str(), "%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", &d1,
                               &d2, &d3, &d4[0], &d4[1], &d4[2], &d4[3], &d4[4], &d4[5], &d4[6],
                               &d4[7]);
    if (matched != 11) {
        return false;
    }
    out.Data1 = d1;
    out.Data2 = static_cast<unsigned short>(d2);
    out.Data3 = static_cast<unsigned short>(d3);
    for (int i = 0; i < 8; ++i) {
        out.Data4[i] = static_cast<unsigned char>(d4[i]);
    }
    return true;
}

std::vector<PowerScheme> enumerate_power_schemes() {
    std::vector<PowerScheme> result;

    for (ULONG index = 0;; ++index) {
        GUID guid{};
        DWORD size = sizeof(GUID);
        DWORD rc = PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME, index,
                                   reinterpret_cast<UCHAR*>(&guid), &size);
        if (rc != ERROR_SUCCESS) {
            break;  // ERROR_NO_MORE_ITEMS, or something went wrong -- either way, done
        }

        PowerScheme scheme;
        scheme.guid = guid;
        scheme.guid_string = guid_to_string(guid);
        scheme.friendly_name = read_friendly_name(guid);
        result.push_back(std::move(scheme));
    }

    return result;
}

std::optional<PowerScheme> get_active_power_scheme() {
    GUID* active = nullptr;
    if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || !active) {
        return std::nullopt;
    }

    PowerScheme scheme;
    scheme.guid = *active;
    scheme.guid_string = guid_to_string(*active);
    scheme.friendly_name = read_friendly_name(*active);
    LocalFree(active);  // PowerGetActiveScheme allocates this for the caller
    return scheme;
}

bool set_active_power_scheme(const GUID& guid) {
    if (PowerSetActiveScheme(nullptr, &guid) != ERROR_SUCCESS) {
        return false;
    }

    auto active = get_active_power_scheme();
    if (!active) {
        return false;
    }
    return std::memcmp(&active->guid, &guid, sizeof(GUID)) == 0;
}

std::optional<PowerScheme> find_power_scheme_by_keyword(const std::vector<PowerScheme>& schemes,
                                                         const std::string& keyword) {
    std::string lower_keyword = to_lower(keyword);
    for (const auto& scheme : schemes) {
        if (to_lower(scheme.friendly_name).find(lower_keyword) != std::string::npos) {
            return scheme;
        }
    }
    return std::nullopt;
}

namespace {

// Discovered by enumerating with ACCESS_OVERLAY_SCHEME and reading each
// GUID's friendly name back from Windows itself (see
// debug_enumerate_overlay_schemes()), NOT the classic GUID_MAX_POWER_SAVINGS
// scheme-template constants -- an earlier version of this code assumed
// those were reused here, which live testing on this machine disproved.
// kBestPowerEfficiency's mapping is cross-checked: it's the exact GUID
// PowerGetUserConfiguredDCPowerMode() returned live on this laptop, and
// Windows reported that same GUID's friendly name as "Better Battery-life
// Overlay". kBestPerformance is inferred from its name ("Max Performance
// Overlay") but has not had the same live cross-check.
constexpr GUID kOverlayBestPowerEfficiency = {
    0x961CC777, 0x2547, 0x4F9D, {0x81, 0x74, 0x7D, 0x86, 0x18, 0x1B, 0x8A, 0x7A}};
constexpr GUID kOverlayBestPerformance = {
    0xDED574B5, 0x45A0, 0x4F42, {0x87, 0x37, 0x46, 0x34, 0x5C, 0x09, 0xC2, 0x38}};

}  // namespace

GUID power_mode_guid(PowerModeLevel level) {
    switch (level) {
        case PowerModeLevel::kBestPowerEfficiency:
            return kOverlayBestPowerEfficiency;
        case PowerModeLevel::kBestPerformance:
            return kOverlayBestPerformance;
    }
    return kOverlayBestPowerEfficiency;
}

const char* power_mode_name(PowerModeLevel level) {
    switch (level) {
        case PowerModeLevel::kBestPowerEfficiency:
            return "best_power_efficiency";
        case PowerModeLevel::kBestPerformance:
            return "best_performance";
    }
    return "unknown";
}

std::optional<PowerModeLevel> power_mode_from_name(const std::string& name) {
    std::string lower = to_lower(name);
    if (lower == "best_power_efficiency" || lower == "power_saver" || lower == "battery_saver") {
        return PowerModeLevel::kBestPowerEfficiency;
    }
    if (lower == "best_performance" || lower == "performance") {
        return PowerModeLevel::kBestPerformance;
    }
    return std::nullopt;
}

std::string get_dc_power_mode_raw_guid() {
    GUID guid{};
    PowerGetUserConfiguredDCPowerMode(&guid);  // leaves guid all-zero on failure -- a valid,
                                                // distinguishable "no override" state to log
    return guid_to_string(guid);
}

bool set_dc_power_mode(PowerModeLevel level) {
    GUID guid = power_mode_guid(level);
    if (PowerSetUserConfiguredDCPowerMode(&guid) != ERROR_SUCCESS) {
        return false;
    }
    return to_lower(get_dc_power_mode_raw_guid()) == to_lower(guid_to_string(guid));
}

bool set_dc_power_mode_raw_guid(const std::string& guid_string) {
    GUID guid{};
    if (!guid_from_string(guid_string, guid)) {
        return false;
    }
    if (PowerSetUserConfiguredDCPowerMode(&guid) != ERROR_SUCCESS) {
        return false;
    }
    return to_lower(get_dc_power_mode_raw_guid()) == to_lower(guid_string);
}

std::vector<PowerScheme> debug_enumerate_overlay_schemes() {
    std::vector<PowerScheme> result;
    for (ULONG index = 0;; ++index) {
        GUID guid{};
        DWORD size = sizeof(GUID);
        DWORD rc = PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_OVERLAY_SCHEME, index,
                                   reinterpret_cast<UCHAR*>(&guid), &size);
        if (rc != ERROR_SUCCESS) {
            break;
        }
        PowerScheme scheme;
        scheme.guid = guid;
        scheme.guid_string = guid_to_string(guid);
        scheme.friendly_name = read_friendly_name(guid);
        result.push_back(std::move(scheme));
    }
    return result;
}

}  // namespace sysintel
