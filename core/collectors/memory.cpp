#include "memory.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace sysintel {

MemorySnapshot get_memory_snapshot() {
    MemorySnapshot snap;

    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);

    if (GlobalMemoryStatusEx(&status)) {
        snap.total_bytes = status.ullTotalPhys;
        snap.available_bytes = status.ullAvailPhys;
        snap.memory_load_percent = static_cast<int>(status.dwMemoryLoad);
    }

    return snap;
}

}  // namespace sysintel
