#include "process.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>

namespace sysintel {

std::vector<ProcessInfo> get_top_processes_by_memory(int limit) {
    std::vector<ProcessInfo> processes;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return processes;
    }

    PROCESSENTRY32 entry{};
    entry.dwSize = sizeof(entry);

    if (Process32First(snapshot, &entry)) {
        do {
            ProcessInfo info;
            info.pid = entry.th32ProcessID;
            info.name = entry.szExeFile;

            HANDLE process = OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, entry.th32ProcessID);
            if (process) {
                PROCESS_MEMORY_COUNTERS_EX pmc{};
                if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                                          sizeof(pmc))) {
                    info.working_set_bytes = pmc.WorkingSetSize;
                }
                CloseHandle(process);
            }

            if (info.working_set_bytes > 0) {
                processes.push_back(info);
            }
        } while (Process32Next(snapshot, &entry));
    }

    CloseHandle(snapshot);

    std::sort(processes.begin(), processes.end(), [](const ProcessInfo& a, const ProcessInfo& b) {
        return a.working_set_bytes > b.working_set_bytes;
    });

    if (static_cast<int>(processes.size()) > limit) {
        processes.resize(limit);
    }

    return processes;
}

}  // namespace sysintel
