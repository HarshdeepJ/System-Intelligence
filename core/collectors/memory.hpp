#pragma once
#include <cstdint>

namespace sysintel {

struct MemorySnapshot {
    uint64_t total_bytes = 0;
    uint64_t available_bytes = 0;
    int memory_load_percent = 0;  // 0-100, reported directly by Windows
};

MemorySnapshot get_memory_snapshot();

}  // namespace sysintel
