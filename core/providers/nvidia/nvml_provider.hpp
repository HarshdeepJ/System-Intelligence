#pragma once
#include <vector>

#include "../../model/gpu_state.hpp"

namespace sysintel {

// Loads nvml.dll dynamically at runtime (see nvml_min.hpp for why). On any
// machine without an NVIDIA driver installed, available() is simply false
// and callers should treat every GPU metric as unavailable/unsupported --
// that is the expected, common case here, not an error path.
//
// CAVEAT: written without access to NVIDIA hardware (this project's dev
// machine has only an Intel iGPU). The dynamic-loading mechanism and struct
// layouts follow NVIDIA's publicly documented, stable NVML ABI, but this
// code has not been exercised against a real GPU or driver. Treat it as
// needing verification on NVIDIA hardware before relying on it.
class NvmlProvider {
public:
    NvmlProvider();
    ~NvmlProvider();

    NvmlProvider(const NvmlProvider&) = delete;
    NvmlProvider& operator=(const NvmlProvider&) = delete;

    bool available() const { return available_; }

    std::vector<GpuState> collect();

private:
    bool available_ = false;
    void* module_ = nullptr;  // HMODULE
};

}  // namespace sysintel
