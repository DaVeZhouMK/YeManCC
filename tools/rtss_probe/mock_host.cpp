#include <windows.h>
#include <cwchar>
#include "../../native/rtss/protocol.h"
static WNDPROC originalBridgeProc = nullptr;
static BOOL CALLBACK findOwnBridge(HWND window, LPARAM value) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;
    wchar_t name[96]{};
    if (GetClassNameW(window, name, 96) && !std::wcscmp(name, ymcc::rtss::kWindowClass)) {
        *reinterpret_cast<HWND*>(value) = window;
        return FALSE;
    }
    return TRUE;
}
static LRESULT CALLBACK probeBridgeProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_COPYDATA && l) {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(l);
        if (data->dwData == ymcc::rtss::kCopyDataTag && data->cbData == sizeof(ymcc::rtss::Packet) && data->lpData) {
            ymcc::rtss::Packet packet{};
            std::memcpy(&packet, data->lpData, sizeof(packet));
            if (packet.command == ymcc::rtss::Command::Ping) {
                if (GetFileAttributesW(L"mock-incompatible-bridge") != INVALID_FILE_ATTRIBUTES)
                    return static_cast<LRESULT>(ymcc::rtss::Reply::Invalid);
                if (GetFileAttributesW(L"mock-slow-bridge") != INVALID_FILE_ATTRIBUTES) Sleep(900);
            }
        }
    }
    return CallWindowProcW(originalBridgeProc, window, message, w, l);
}
int wmain() {
    HMODULE hooks=LoadLibraryW(L"RTSSHooks.dll");
    HMODULE editor=LoadLibraryW(L"Plugins\\Client\\OverlayEditor.dll");
    HMODULE bridge=LoadLibraryW(L"Plugins\\Client\\YMCCOverlayBridge.dll");
    if(!hooks||!editor||!bridge)return 20;
    auto startEditor=reinterpret_cast<BOOL(*)()>(GetProcAddress(editor,"Start"));
    auto startBridge=reinterpret_cast<BOOL(*)()>(GetProcAddress(bridge,"Start"));
    if(!startEditor||!startBridge||!startEditor()||!startBridge())return 21;
    HWND bridgeWindow = nullptr;
    EnumWindows(findOwnBridge, reinterpret_cast<LPARAM>(&bridgeWindow));
    if (!bridgeWindow) return 22;
    originalBridgeProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(bridgeWindow, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(probeBridgeProc)));
    if (!originalBridgeProc) return 23;
    wchar_t eventName[80]{};swprintf_s(eventName,L"Local\\YMCC.RtssProbe.%lu",GetCurrentProcessId());
    HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,eventName);
    WritePrivateProfileStringW(L"Host",L"Ready",L"1",L".\\host-state.ini");
    bool done=false;
    while(!done){
        const DWORD wait=MsgWaitForMultipleObjects(1,&stop,FALSE,10000,QS_ALLINPUT);
        if(wait==WAIT_OBJECT_0)break;
        MSG msg{};
        while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){
            if(msg.message==WM_QUIT){done=true;break;}
            TranslateMessage(&msg);DispatchMessageW(&msg);
        }
    }
    reinterpret_cast<void(*)()>(GetProcAddress(bridge,"Stop"))();
    reinterpret_cast<void(*)()>(GetProcAddress(editor,"Stop"))();
    CloseHandle(stop);
    return 0;
}
