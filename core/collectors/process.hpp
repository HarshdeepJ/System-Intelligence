#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sysintel {

struct ProcessInfo {
    uint32_t pid = 0;
    std::string name;
    uint64_t working_set_bytes = 0;
};

// Returns up to `limit` processes, sorted by working-set memory (descending).
// Per-process CPU% is deliberately left for a later step (it needs the same
// two-sample rate trick as the total CPU counter, per process).
std::vector<ProcessInfo> get_top_processes_by_memory(int limit = 10);

}  // namespace sysintel
