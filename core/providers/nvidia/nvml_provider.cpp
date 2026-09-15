#include "nvml_provider.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "nvml_min.hpp"

namespace sysintel {

namespace {

using namespace nvml;

PFN_nvmlInit_v2 g_nvmlInit_v2 = nullptr;
PFN_nvmlShutdown g_nvmlShutdown = nullptr;
PFN_nvmlDeviceGetCount_v2 g_nvmlDeviceGetCount_v2 = nullptr;
PFN_nvmlDeviceGetHandleByIndex_v2 g_nvmlDeviceGetHandleByIndex_v2 = nullptr;
PFN_nvmlDeviceGetName g_nvmlDeviceGetName = nullptr;
PFN_nvmlDeviceGetUtilizationRates g_nvmlDeviceGetUtilizationRates = nullptr;
PFN_nvmlDeviceGetMemoryInfo g_nvmlDeviceGetMemoryInfo = nullptr;
PFN_nvmlDeviceGetTemperature g_nvmlDeviceGetTemperature = nullptr;
PFN_nvmlDeviceGetPowerUsage g_nvmlDeviceGetPowerUsage = nullptr;
PFN_nvmlDeviceGetPerformanceState g_nvmlDeviceGetPerformanceState = nullptr;
PFN_nvmlDeviceGetComputeRunningProcesses_v3 g_nvmlDeviceGetComputeRunningProcesses_v3 = nullptr;

template <typename Fn>
bool load(HMODULE module, const char* name, Fn& out) {
    out = reinterpret_cast<Fn>(GetProcAddress(module, name));
    return out != nullptr;
}

}  // namespace

NvmlProvider::NvmlProvider() {
    HMODULE module = LoadLibraryA("nvml.dll");
    if (!module) {
        // The overwhelmingly common case on non-NVIDIA machines: no error,
        // just no provider available.
        return;
    }

    bool ok = true;
    ok &= load(module, "nvmlInit_v2", g_nvmlInit_v2);
    ok &= load(module, "nvmlShutdown", g_nvmlShutdown);
    ok &= load(module, "nvmlDeviceGetCount_v2", g_nvmlDeviceGetCount_v2);
    ok &= load(module, "nvmlDeviceGetHandleByIndex_v2", g_nvmlDeviceGetHandleByIndex_v2);
    ok &= load(module, "nvmlDeviceGetName", g_nvmlDeviceGetName);
    ok &= load(module, "nvmlDeviceGetUtilizationRates", g_nvmlDeviceGetUtilizationRates);
    ok &= load(module, "nvmlDeviceGetMemoryInfo", g_nvmlDeviceGetMemoryInfo);
    ok &= load(module, "nvmlDeviceGetTemperature", g_nvmlDeviceGetTemperature);
    ok &= load(module, "nvmlDeviceGetPowerUsage", g_nvmlDeviceGetPowerUsage);
    ok &= load(module, "nvmlDeviceGetPerformanceState", g_nvmlDeviceGetPerformanceState);
    // Process enumeration is best-effort -- not fatal if missing on an
    // older driver.
    load(module, "nvmlDeviceGetComputeRunningProcesses_v3",
         g_nvmlDeviceGetComputeRunningProcesses_v3);

    if (!ok) {
        FreeLibrary(module);
        return;
    }

    if (g_nvmlInit_v2() != kNvmlSuccess) {
        FreeLibrary(module);
        return;
    }

    module_ = module;
    available_ = true;
}

NvmlProvider::~NvmlProvider() {
    if (available_ && g_nvmlShutdown) {
        g_nvmlShutdown();
    }
    if (module_) {
        FreeLibrary(reinterpret_cast<HMODULE>(module_));
    }
}

std::vector<GpuState> NvmlProvider::collect() {
    std::vector<GpuState> result;
    if (!available_) {
        return result;
    }

    unsigned int count = 0;
    if (g_nvmlDeviceGetCount_v2(&count) != kNvmlSuccess) {
        return result;
    }

    for (unsigned int i = 0; i < count; ++i) {
        nvmlDevice_t device = nullptr;
        if (g_nvmlDeviceGetHandleByIndex_v2(i, &device) != kNvmlSuccess) {
            continue;
        }

        GpuState state;
        state.vendor = "NVIDIA";

        char name_buf[96] = {0};
        if (g_nvmlDeviceGetName(device, name_buf, sizeof(name_buf)) == kNvmlSuccess) {
            state.model = name_buf;
        } else {
            state.model = "NVIDIA GPU";
        }

        nvmlUtilization_t util{};
        if (g_nvmlDeviceGetUtilizationRates(device, &util) == kNvmlSuccess) {
            state.utilization_percent = Reading<int>::ok(static_cast<int>(util.gpu));
        } else {
            state.utilization_percent = Reading<int>::unavailable();
        }

        nvmlMemory_t mem{};
        if (g_nvmlDeviceGetMemoryInfo(device, &mem) == kNvmlSuccess) {
            state.used_vram_bytes = Reading<uint64_t>::ok(mem.used);
            state.total_vram_bytes = Reading<uint64_t>::ok(mem.total);
        } else {
            state.used_vram_bytes = Reading<uint64_t>::unavailable();
            state.total_vram_bytes = Reading<uint64_t>::unavailable();
        }

        unsigned int temp = 0;
        if (g_nvmlDeviceGetTemperature(device, kNvmlTemperatureGpu, &temp) == kNvmlSuccess) {
            state.temperature_celsius = Reading<int>::ok(static_cast<int>(temp));
        } else {
            state.temperature_celsius = Reading<int>::unavailable();
        }

        unsigned int power_mw = 0;
        if (g_nvmlDeviceGetPowerUsage(device, &power_mw) == kNvmlSuccess) {
            state.power_watts = Reading<double>::ok(power_mw / 1000.0);
        } else {
            state.power_watts = Reading<double>::unavailable();
        }

        int pstate = 0;
        if (g_nvmlDeviceGetPerformanceState(device, &pstate) == kNvmlSuccess) {
            state.performance_state = Reading<std::string>::ok("P" + std::to_string(pstate));
        } else {
            state.performance_state = Reading<std::string>::unavailable();
        }

        if (g_nvmlDeviceGetComputeRunningProcesses_v3) {
            // Guess a generously-sized buffer rather than doing NVML's
            // documented two-call "ask for the size first" dance -- simpler,
            // and if a machine somehow has more than 64 GPU processes we
            // just skip attribution for the overflow rather than retrying.
            unsigned int proc_count = 64;
            std::vector<nvmlProcessInfo_t> infos(proc_count);
            nvmlReturn_t rc =
                g_nvmlDeviceGetComputeRunningProcesses_v3(device, &proc_count, infos.data());
            if (rc == kNvmlSuccess) {
                infos.resize(proc_count);
                for (const auto& info : infos) {
                    GpuProcessUsage usage;
                    usage.pid = info.pid;
                    usage.used_vram_bytes = Reading<uint64_t>::ok(info.usedGpuMemory);
                    state.processes.push_back(std::move(usage));
                }
            }
        }

        result.push_back(std::move(state));
    }

    return result;
}

}  // namespace sysintel
