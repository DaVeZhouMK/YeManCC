// Mock only: emulate SDK message queue without injection, GPU or hardware access.
#include <windows.h>
#include <cwchar>
#include <cstring>
static HMODULE module;
static HWND window;
static char pending[64]{};
static wchar_t current[64]{};
static wchar_t folder[MAX_PATH]{};
static constexpr UINT kLoad = WM_APP + 84;
static LRESULT CALLBACK wnd(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == kLoad) {
        if (GetFileAttributesW(L"mock-slow-editor") != INVALID_FILE_ATTRIBUTES) Sleep(750);
        if (GetFileAttributesW(L"mock-stall-editor") != INVALID_FILE_ATTRIBUTES) Sleep(4800);
        wchar_t file[MAX_PATH]{};
        wcscpy_s(file, folder);
        wcscat_s(file, L"\\Overlays\\");
        MultiByteToWideChar(CP_ACP, 0, pending, -1, current, 64);
        wcscat_s(file, current);
        const int count = GetPrivateProfileIntW(L"General", L"Layers", -1, file);
        wchar_t state[MAX_PATH]{}, number[32]{};
        swprintf_s(state, L"%s\\mock-state.ini", folder);
        swprintf_s(number, L"%d", count);
        WritePrivateProfileStringW(L"Mock", L"Layout", current, state);
        WritePrivateProfileStringW(L"Mock", L"Layers", number, state);
        const int old = GetPrivateProfileIntW(L"Mock", L"Updates", 0, state);
        swprintf_s(number, L"%d", old + 1);
        WritePrivateProfileStringW(L"Mock", L"Updates", number, state);
        return 0;
    }
    return DefWindowProcW(h,m,w,l);
}
extern "C" __declspec(dllexport) BOOL Start() {
    GetModuleFileNameW(module, folder, MAX_PATH);
    *std::wcsrchr(folder, L'\\') = 0;
    WNDCLASSW wc{}; wc.lpfnWndProc=wnd; wc.hInstance=module; wc.lpszClassName=L"YMCC.MockEditor";
    RegisterClassW(&wc);
    window=CreateWindowW(wc.lpszClassName,L"RTSSOverlayEditorClientWnd",WS_POPUP,0,0,0,0,nullptr,nullptr,module,nullptr);
    return window != nullptr;
}
extern "C" __declspec(dllexport) void Stop() { DestroyWindow(window); }
extern "C" __declspec(dllexport) void PostOverlayMessage(LPCSTR message, LPCSTR, LPCSTR params) {
    if (std::strcmp(message,"Load") || !params) return;
    if (GetFileAttributesW(L"mock-fault-editor") != INVALID_FILE_ATTRIBUTES)
        RaiseException(0xe0123456,0,0,nullptr);
    strcpy_s(pending,params);
    PostMessageW(window,kLoad,0,0);
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD r,LPVOID) {if(r==DLL_PROCESS_ATTACH){module=h;DisableThreadLibraryCalls(h);}return TRUE;}
