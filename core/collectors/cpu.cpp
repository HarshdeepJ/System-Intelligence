#include "cpu.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pdh.h>

#include <chrono>
#include <thread>

namespace sysintel {

CpuSnapshot get_cpu_snapshot() {
    CpuSnapshot snap;

    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER counter = nullptr;

    if (PdhOpenQuery(nullptr, 0, &query) != ERROR_SUCCESS) {
        return snap;
    }

    if (PdhAddEnglishCounterW(query, L"\\Processor(_Total)\\% Processor Time", 0, &counter) !=
        ERROR_SUCCESS) {
        PdhCloseQuery(query);
        return snap;
    }

    PdhCollectQueryData(query);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    PdhCollectQueryData(query);

    PDH_FMT_COUNTERVALUE value{};
    if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS) {
        snap.total_utilization_percent = value.doubleValue;
    }

    PdhCloseQuery(query);
    return snap;
}

}  // namespace sysintel
