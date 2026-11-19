#pragma once
#include <windows.h>

// Optional one-time metadata for Windows' external thread counters and stacks.
// Called only at worker entry. Never schedules work or changes worker lifetime.
namespace ymcc::threadRole {
inline void describe(const wchar_t* role) noexcept {
    if (!role || !*role) return;
    const DWORD savedError = GetLastError();
    using SetDescription = HRESULT (WINAPI*)(HANDLE, PCWSTR);
    static const auto setDescription = []() noexcept -> SetDescription {
        const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        return kernel ? reinterpret_cast<SetDescription>(GetProcAddress(kernel, "SetThreadDescription")) : nullptr;
    }();
    if (setDescription) (void)setDescription(GetCurrentThread(), role);
    SetLastError(savedError);
}
} // namespace ymcc::threadRole
