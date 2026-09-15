#include "../model/gpu_state.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

#include "../providers/nvidia/nvml_provider.hpp"
#include "../providers/windows/wmi_client.hpp"

namespace sysintel {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string infer_gpu_vendor(const std::string& name) {
    std::string lower = to_lower(name);
    if (lower.find("nvidia") != std::string::npos) return "NVIDIA";
    if (lower.find("amd") != std::string::npos || lower.find("radeon") != std::string::npos)
        return "AMD";
    if (lower.find("intel") != std::string::npos) return "Intel";
    return "Unknown";
}

GpuState make_state(const std::string& vendor, const std::string& model, Availability avail) {
    GpuState state;
    state.vendor = vendor;
    state.model = model;
    state.utilization_percent.availability = avail;
    state.used_vram_bytes.availability = avail;
    state.total_vram_bytes.availability = avail;
    state.temperature_celsius.availability = avail;
    state.power_watts.availability = avail;
    state.performance_state.availability = avail;
    return state;
}

}  // namespace

std::vector<GpuState> collect_gpu_state() {
    std::vector<GpuState> result;

    // Same source SystemInventory uses to enumerate adapters -- vendor
    // detection doesn't need its own separate WMI query.
    std::vector<std::pair<std::string, std::string>> adapters;  // name, vendor
    WmiClient wmi;
    if (wmi.ok()) {
        for (const auto& row : wmi.query("SELECT Name FROM Win32_VideoController")) {
            auto it = row.find("Name");
            std::string name = it != row.end() ? it->second : "Unknown GPU";
            adapters.emplace_back(name, infer_gpu_vendor(name));
        }
    }

    NvmlProvider nvml;
    std::vector<GpuState> nvml_states =
        nvml.available() ? nvml.collect() : std::vector<GpuState>{};
    size_t nvml_index = 0;

    for (const auto& [name, vendor] : adapters) {
        if (vendor == "NVIDIA") {
            if (nvml_index < nvml_states.size()) {
                result.push_back(std::move(nvml_states[nvml_index++]));
            } else {
                // NVIDIA hardware is present per WMI, but NVML couldn't be
                // loaded/initialized -- genuinely unavailable right now, not
                // "we never built a provider for this."
                result.push_back(make_state(vendor, name, Availability::kUnavailable));
            }
        } else {
            // AMD, Intel, or unrecognized -- no provider exists yet.
            result.push_back(make_state(vendor, name, Availability::kUnsupported));
        }
    }

    return result;
}

}  // namespace sysintel
