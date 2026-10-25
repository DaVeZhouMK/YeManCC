#pragma once

#include <windows.h>

namespace ymcc::exitLifecycle {

// Exit phase only. A real cleanup-thread handle wakes the owner without posted
// messages or timers. With no handle, the timeout lets the owner retry creation.
// The caller owns the handle and keeps it valid throughout this wait.
inline DWORD waitForCleanupOrMessages(HANDLE cleanupThread) noexcept {
    return ::MsgWaitForMultipleObjectsEx(
        cleanupThread ? 1 : 0,
        cleanupThread ? &cleanupThread : nullptr,
        cleanupThread ? INFINITE : 200,
        QS_ALLINPUT,
        MWMO_INPUTAVAILABLE);
}

} // namespace ymcc::exitLifecycle
