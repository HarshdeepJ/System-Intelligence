#pragma once

// A minimal, hand-declared subset of NVIDIA's NVML C API: just enough for
// GPU utilization, memory, temperature, power, performance state, and
// running processes.
//
// NVML deliberately guarantees a stable ABI across driver versions so tools
// can load nvml.dll dynamically at runtime instead of linking against a
// static import lib from the CUDA Toolkit. That matters here: this project
// should compile and run cleanly on a machine with no NVIDIA hardware, no
// driver, and no CUDA Toolkit installed at all (which describes the
// machine this was written on) -- it just reports GPU telemetry as
// unavailable/unsupported in that case, rather than failing to build.

namespace sysintel::nvml {

using nvmlReturn_t = int;
constexpr nvmlReturn_t kNvmlSuccess = 0;

using nvmlDevice_t = void*;

struct nvmlUtilization_t {
    unsigned int gpu;
    unsigned int memory;
};

struct nvmlMemory_t {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

constexpr int kNvmlTemperatureGpu = 0;

struct nvmlProcessInfo_t {
    unsigned int pid;
    unsigned long long usedGpuMemory;
    // Newer driver versions append gpuInstanceId/computeInstanceId after
    // this; we never read past usedGpuMemory, so their presence or absence
    // in the real struct doesn't affect us.
    unsigned int gpuInstanceId;
    unsigned int computeInstanceId;
};

using PFN_nvmlInit_v2 = nvmlReturn_t (*)();
using PFN_nvmlShutdown = nvmlReturn_t (*)();
using PFN_nvmlDeviceGetCount_v2 = nvmlReturn_t (*)(unsigned int*);
using PFN_nvmlDeviceGetHandleByIndex_v2 = nvmlReturn_t (*)(unsigned int, nvmlDevice_t*);
using PFN_nvmlDeviceGetName = nvmlReturn_t (*)(nvmlDevice_t, char*, unsigned int);
using PFN_nvmlDeviceGetUtilizationRates = nvmlReturn_t (*)(nvmlDevice_t, nvmlUtilization_t*);
using PFN_nvmlDeviceGetMemoryInfo = nvmlReturn_t (*)(nvmlDevice_t, nvmlMemory_t*);
using PFN_nvmlDeviceGetTemperature = nvmlReturn_t (*)(nvmlDevice_t, int, unsigned int*);
using PFN_nvmlDeviceGetPowerUsage = nvmlReturn_t (*)(nvmlDevice_t, unsigned int*);
using PFN_nvmlDeviceGetPerformanceState = nvmlReturn_t (*)(nvmlDevice_t, int*);
using PFN_nvmlDeviceGetComputeRunningProcesses_v3 =
    nvmlReturn_t (*)(nvmlDevice_t, unsigned int*, nvmlProcessInfo_t*);

}  // namespace sysintel::nvml
