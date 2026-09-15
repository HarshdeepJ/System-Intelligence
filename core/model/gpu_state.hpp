#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "availability.hpp"

namespace sysintel {

struct GpuProcessUsage {
    uint32_t pid = 0;
    // Name isn't resolved here -- NVML only gives a PID; joining against
    // get_all_process_identities() for a name is left to the presentation
    // layer so this collector doesn't need to depend on the process one.
    Reading<uint64_t> used_vram_bytes;
};

struct GpuState {
    std::string vendor;  // "NVIDIA" | "AMD" | "Intel" | "Unknown"
    std::string model;
    Reading<int> utilization_percent;
    Reading<uint64_t> used_vram_bytes;
    Reading<uint64_t> total_vram_bytes;
    Reading<int> temperature_celsius;
    Reading<double> power_watts;
    Reading<std::string> performance_state;  // e.g. NVIDIA's "P0".."P12"
    std::vector<GpuProcessUsage> processes;
};

// Detects each GPU's vendor and dispatches to the matching provider.
// NVIDIA gets real telemetry via NVML when the driver is present; AMD and
// Intel currently return Availability::kUnsupported for every metric --
// there's no provider for either yet.
std::vector<GpuState> collect_gpu_state();

}  // namespace sysintel
