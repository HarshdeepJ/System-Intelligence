#include "process_control.hpp"

#include <windows.h>

namespace sysintel {

namespace {

using NtProcessControlFn = LONG(WINAPI*)(HANDLE);

// GetProcAddress, not a static import -- ntdll's suspend/resume entry
// points are deliberately absent from any header, so there's nothing to
// link against directly.
NtProcessControlFn load_ntdll_fn(const char* name) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        return nullptr;
    }
    return reinterpret_cast<NtProcessControlFn>(GetProcAddress(ntdll, name));
}

bool call_nt_process_fn(uint32_t pid, const char* fn_name) {
    HANDLE process = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
    if (!process) {
        return false;
    }

    NtProcessControlFn fn = load_ntdll_fn(fn_name);
    // NTSTATUS success codes are non-negative; every failure has the high
    // bit set, so a signed >= 0 check is NT_SUCCESS without needing ntdef.h.
    bool ok = fn != nullptr && fn(process) >= 0;

    CloseHandle(process);
    return ok;
}

}  // namespace

bool suspend_process(uint32_t pid) { return call_nt_process_fn(pid, "NtSuspendProcess"); }

bool resume_process(uint32_t pid) { return call_nt_process_fn(pid, "NtResumeProcess"); }

}  // namespace sysintel
