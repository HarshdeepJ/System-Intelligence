#include "process.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <utility>

namespace sysintel {

namespace {

std::string wide_to_utf8(const wchar_t* wide) {
    if (!wide) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), len, nullptr, nullptr);
    return result;
}

std::vector<std::pair<std::string, double>> read_pdh_array(PDH_HCOUNTER counter) {
    std::vector<std::pair<std::string, double>> out;

    DWORD buffer_size = 0;
    DWORD item_count = 0;
    PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &buffer_size, &item_count, nullptr);
    if (item_count == 0 || buffer_size == 0) {
        return out;
    }

    std::vector<char> buffer(buffer_size);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &buffer_size, &item_count, items) !=
        ERROR_SUCCESS) {
        return out;
    }

    for (DWORD i = 0; i < item_count; ++i) {
        out.emplace_back(wide_to_utf8(items[i].szName), items[i].FmtValue.doubleValue);
    }
    return out;
}

}  // namespace

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

std::vector<ProcessCpuInfo> get_top_processes_by_cpu(int limit) {
    std::vector<ProcessCpuInfo> processes;

    PDH_HQUERY query = nullptr;
    if (PdhOpenQuery(nullptr, 0, &query) != ERROR_SUCCESS) {
        return processes;
    }

    PDH_HCOUNTER cpu_counter = nullptr;
    PDH_HCOUNTER pid_counter = nullptr;
    bool ok = true;
    ok &= PdhAddEnglishCounterW(query, L"\\Process(*)\\% Processor Time", 0, &cpu_counter) ==
          ERROR_SUCCESS;
    ok &= PdhAddEnglishCounterW(query, L"\\Process(*)\\ID Process", 0, &pid_counter) ==
          ERROR_SUCCESS;
    if (!ok) {
        PdhCloseQuery(query);
        return processes;
    }

    PdhCollectQueryData(query);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    PdhCollectQueryData(query);

    auto cpu_vals = read_pdh_array(cpu_counter);
    auto pid_vals = read_pdh_array(pid_counter);

    std::unordered_map<std::string, uint32_t> pid_by_instance;
    for (const auto& [instance, pid_value] : pid_vals) {
        pid_by_instance[instance] = static_cast<uint32_t>(pid_value);
    }

    for (const auto& [instance, cpu_percent] : cpu_vals) {
        if (instance == "_Total" || instance == "Idle") {
            continue;
        }
        auto it = pid_by_instance.find(instance);
        if (it == pid_by_instance.end() || it->second == 0) {
            continue;  // PID 0 is the System Idle Process's own instance in some layouts
        }

        ProcessCpuInfo info;
        info.pid = it->second;
        // PDH instance names strip the ".exe" and append "#N" for
        // duplicates (e.g. "chrome#3") rather than giving us the real
        // filename -- good enough for display, not byte-identical to
        // get_all_process_identities()'s names.
        info.name = instance;
        info.cpu_percent = cpu_percent;
        processes.push_back(std::move(info));
    }

    PdhCloseQuery(query);

    std::sort(processes.begin(), processes.end(),
              [](const ProcessCpuInfo& a, const ProcessCpuInfo& b) {
                  return a.cpu_percent > b.cpu_percent;
              });

    if (static_cast<int>(processes.size()) > limit) {
        processes.resize(limit);
    }

    return processes;
}

std::vector<ProcessIdentity> get_all_process_identities() {
    std::vector<ProcessIdentity> processes;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return processes;
    }

    PROCESSENTRY32 entry{};
    entry.dwSize = sizeof(entry);

    if (Process32First(snapshot, &entry)) {
        do {
            processes.push_back({entry.th32ProcessID, entry.szExeFile});
        } while (Process32Next(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return processes;
}

}  // namespace sysintel
