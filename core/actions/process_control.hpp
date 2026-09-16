#pragma once
#include <cstdint>

namespace sysintel {

// Suspends/resumes every thread in a process in one call -- the same
// mechanism Process Explorer's "Suspend" menu item uses. There is no
// documented Win32 API for whole-process suspend (only per-thread
// SuspendThread), so this goes through ntdll's NtSuspendProcess/
// NtResumeProcess: undocumented, but stable since Windows NT and relied on
// by exactly this kind of tool for decades.
//
// This is deliberately not terminate_process(): the PRD's own Level 3
// "reversible action" examples list "temporarily suspend process," not
// "terminate" -- a suspended process can be resumed exactly as it was, a
// terminated one cannot be brought back. That's what makes this a safe
// second action for the Action Broker rather than a one-way door.
bool suspend_process(uint32_t pid);
bool resume_process(uint32_t pid);

}  // namespace sysintel
