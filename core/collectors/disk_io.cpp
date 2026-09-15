#include "../model/disk_io_state.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <pdh.h>
#include <pdhmsg.h>

#include <chrono>
#include <thread>
#include <utility>
#include <vector>

namespace sysintel {

namespace {

using NamedValues = std::vector<std::pair<std::string, double>>;

std::string wide_to_utf8(const wchar_t* wide) {
    if (!wide) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), len, nullptr, nullptr);
    return result;
}

NamedValues read_array(PDH_HCOUNTER counter) {
    NamedValues out;

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

Reading<double> find_value(const NamedValues& values, const std::string& name) {
    for (const auto& [n, v] : values) {
        if (n == name) return Reading<double>::ok(v);
    }
    return Reading<double>::unavailable();
}

}  // namespace

std::vector<DiskIoState> get_disk_io_state() {
    std::vector<DiskIoState> result;

    PDH_HQUERY query = nullptr;
    if (PdhOpenQuery(nullptr, 0, &query) != ERROR_SUCCESS) {
        return result;
    }

    PDH_HCOUNTER read_bytes = nullptr;
    PDH_HCOUNTER write_bytes = nullptr;
    PDH_HCOUNTER reads = nullptr;
    PDH_HCOUNTER writes = nullptr;
    PDH_HCOUNTER queue_length = nullptr;

    bool ok = true;
    ok &= PdhAddEnglishCounterW(query, L"\\PhysicalDisk(*)\\Disk Read Bytes/sec", 0, &read_bytes) ==
          ERROR_SUCCESS;
    ok &=
        PdhAddEnglishCounterW(query, L"\\PhysicalDisk(*)\\Disk Write Bytes/sec", 0, &write_bytes) ==
        ERROR_SUCCESS;
    ok &= PdhAddEnglishCounterW(query, L"\\PhysicalDisk(*)\\Disk Reads/sec", 0, &reads) ==
          ERROR_SUCCESS;
    ok &= PdhAddEnglishCounterW(query, L"\\PhysicalDisk(*)\\Disk Writes/sec", 0, &writes) ==
          ERROR_SUCCESS;
    ok &= PdhAddEnglishCounterW(query, L"\\PhysicalDisk(*)\\Current Disk Queue Length", 0,
                                 &queue_length) == ERROR_SUCCESS;

    if (!ok) {
        PdhCloseQuery(query);
        return result;
    }

    PdhCollectQueryData(query);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    PdhCollectQueryData(query);

    auto read_bytes_vals = read_array(read_bytes);
    auto write_bytes_vals = read_array(write_bytes);
    auto reads_vals = read_array(reads);
    auto writes_vals = read_array(writes);
    auto queue_vals = read_array(queue_length);

    for (const auto& [name, value] : read_bytes_vals) {
        if (name == "_Total") {
            continue;  // the aggregate row -- we want per-disk instances
        }
        DiskIoState state;
        state.instance = name;
        state.read_bytes_per_sec = Reading<double>::ok(value);
        state.write_bytes_per_sec = find_value(write_bytes_vals, name);
        state.reads_per_sec = find_value(reads_vals, name);
        state.writes_per_sec = find_value(writes_vals, name);
        state.queue_length = find_value(queue_vals, name);
        result.push_back(std::move(state));
    }

    PdhCloseQuery(query);
    return result;
}

}  // namespace sysintel
