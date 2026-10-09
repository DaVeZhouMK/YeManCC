// Third-party RTSSHooks is confined to this disposable helper process.
// YMCC must never LoadLibrary/FreeLibrary RTSSHooks (WebView2 hook lifetime).
#include <windows.h>
#include <cwchar>
#include <string>
using LoadProfileFn = void (*)(LPCSTR);
using UpdateProfilesFn = void (*)();
static DWORD invokeSdk(LoadProfileFn load, UpdateProfilesFn update) {
    __try { load(""); update(); return ERROR_SUCCESS; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
}
int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc != 2) return ERROR_INVALID_PARAMETER;
    const std::wstring path = argv[1];
    const auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos || _wcsicmp(path.c_str() + slash + 1, L"RTSSHooks64.dll"))
        return ERROR_INVALID_PARAMETER;
    if (GetFileAttributesW((path.substr(0, slash + 1) + L"RTSS.exe").c_str()) == INVALID_FILE_ATTRIBUTES)
        return ERROR_FILE_NOT_FOUND;
    HMODULE dll = LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!dll) return static_cast<int>(GetLastError());
    auto load = reinterpret_cast<LoadProfileFn>(GetProcAddress(dll, "LoadProfile"));
    auto update = reinterpret_cast<UpdateProfilesFn>(GetProcAddress(dll, "UpdateProfiles"));
    if (!load || !update) return ERROR_PROC_NOT_FOUND;
    const DWORD result = invokeSdk(load, update);
    // Do not repeatedly unload/reload hook DLLs in a long-lived process.
    // Process teardown reclaims the entire helper after a single SDK call.
    return static_cast<int>(result);
}

