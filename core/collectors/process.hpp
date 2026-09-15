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

struct ProcessIdentity {
    uint32_t pid = 0;
    std::string name;
};

struct ProcessCpuInfo {
    uint32_t pid = 0;
    std::string name;
    double cpu_percent = 0.0;  // PDH's raw (unnormalized) reading -- can
                                // exceed 100% for a process using multiple
                                // cores, matching pre-Win8 Task Manager
                                // semantics, not the newer normalized view.
};

// Returns up to `limit` processes, sorted by working-set memory (descending).
std::vector<ProcessInfo> get_top_processes_by_memory(int limit = 10);

// Returns up to `limit` processes, sorted by CPU usage (descending). Takes
// ~1 second: PDH's per-process % Processor Time counter needs two samples
// spaced apart, the same trick the total CPU collector uses.
std::vector<ProcessCpuInfo> get_top_processes_by_cpu(int limit = 10);

// Every running process's PID + name, with no per-process memory query --
// cheap enough to call every few seconds just to diff against the last
// snapshot (see events/process_event_detector.cpp).
std::vector<ProcessIdentity> get_all_process_identities();

}  // namespace sysintel
