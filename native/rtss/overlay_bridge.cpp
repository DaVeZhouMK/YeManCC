// RTSS is a 32-bit plugin host even on x64 Windows. This DLL is loaded ONLY
// by RTSS via its documented client-plugin Start/Stop ABI. Never load it in
// YMCC, inject it remotely, or call OverlayEditor's export in another process.
// SDK: HotkeyHandler/OverlayEditorInterface.cpp -> PostOverlayMessage("Load")
//      Overlay/ProcessMessage -> Load -> UM_OVERLAY_LOADED -> Update + InitTimer.
#include "protocol.h"
#include <cwchar>

using namespace ymcc::rtss;
static HMODULE g_module = nullptr;
static HWND g_window = nullptr;
static Packet g_pending{};
static Reply g_reply = Reply::UnknownRequest;
static constexpr UINT kExecute = WM_APP + 41;
static constexpr UINT kComplete = WM_APP + 42;
using PostOverlayMessageFn = void (*)(LPCSTR, LPCSTR, LPCSTR);

static bool pluginFile(const wchar_t* leaf, wchar_t (&path)[MAX_PATH]) {
    const DWORD length = GetModuleFileNameW(g_module, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return false;
    wchar_t* slash = std::wcsrchr(path, L'\\');
    if (!slash) return false;
    slash[1] = 0;
    return wcscat_s(path, leaf) == 0;
}
static bool templateExists(const char* name) {
    wchar_t path[MAX_PATH]{};
    wchar_t wide[64]{};
    if (!MultiByteToWideChar(CP_ACP, 0, name, -1, wide, 64) ||
        !pluginFile(L"Overlays\\", path) || wcscat_s(path, wide)) return false;
    const DWORD attrs = GetFileAttributesW(path);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) return false;
    // COverlay::Load silently treats missing/invalid files as zero-layer overlays.
    // Reject those BEFORE disturbing a currently visible layout.
    const int layers = GetPrivateProfileIntW(L"General", L"Layers", -1, path);
    if (!std::strcmp(name, "Empty.ovl")) return layers == 0;
    if (layers <= 0 || layers > 4096) return false;
    for (int i = 0; i < layers; ++i) {
        wchar_t section[32]{}, contents[1024]{};
        swprintf_s(section, L"Layer%d", i);
        if (!GetPrivateProfileSectionW(section, contents, 1024, path)) return false;
    }
    return true;
}
// EnableBgnd is explicitly documented as the OSD shadow in RTSS's
// RTSSProfileInterface.h. Custom YMCC templates draw their own backgrounds:
// a Global shadow otherwise adds a second black outline to all their layers.
// Save the original setting once and restore it when YMCC monitoring is off.
static Reply applyShadowPolicy(const Packet* packet) {
    __try {
        HMODULE hooks = GetModuleHandleW(L"RTSSHooks.dll");
        if (!hooks) return Reply::ProfileUnavailable;
        using LoadFn = void (*)(LPCSTR);
        using PropertyFn = BOOL (*)(LPCSTR, LPBYTE, DWORD);
        using UpdateFn = void (*)();
        auto load = reinterpret_cast<LoadFn>(GetProcAddress(hooks, "LoadProfile"));
        auto save = reinterpret_cast<LoadFn>(GetProcAddress(hooks, "SaveProfile"));
        auto get = reinterpret_cast<PropertyFn>(GetProcAddress(hooks, "GetProfileProperty"));
        auto set = reinterpret_cast<PropertyFn>(GetProcAddress(hooks, "SetProfileProperty"));
        auto update = reinterpret_cast<UpdateFn>(GetProcAddress(hooks, "UpdateProfiles"));
        if (!load || !save || !get || !set || !update) return Reply::ProfileUnavailable;
        wchar_t cfg[MAX_PATH]{};
        if (!pluginFile(L"YMCCOverlayBridge.cfg", cfg)) return Reply::PersistFailed;
        load(""); // fresh Global, not a cached profile or a game's detection settings
        DWORD current = 0;
        if (!get("EnableBgnd", reinterpret_cast<LPBYTE>(&current), sizeof(current)))
            return Reply::ProfileUnavailable;
        const int original = GetPrivateProfileIntW(L"Shadow", L"Original", -1, cfg);
        const bool off = !std::strcmp(packet->layout, "Empty.ovl");
        DWORD desired = 0;
        if (off) {
            if (original < 0) return Reply::Accepted;
            // If the user changed shadow meanwhile, don't overwrite that choice.
            desired = current == 0 ? static_cast<DWORD>(original) : current;
        } else if (original < 0) {
            if (!WritePrivateProfileStringW(L"Shadow", L"Original", current ? L"1" : L"0", cfg))
                return Reply::PersistFailed;
        }
        if (current != desired) {
            if (!set("EnableBgnd", reinterpret_cast<LPBYTE>(&desired), sizeof(desired)))
                return Reply::ProfileUnavailable;
            save(""); // SDK load/get/set/save transaction; don't change FPS/font/zoom
            update();
        }
        if (off && !WritePrivateProfileStringW(L"Shadow", L"Original", nullptr, cfg))
            return Reply::PersistFailed;
        return Reply::Accepted;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return Reply::SdkException; }
}
static Reply postLoad(const Packet* packet) {
    __try {
        HMODULE editor = GetModuleHandleW(L"OverlayEditor.dll");
        HWND client = nullptr;
        DWORD pid = 0;
        while ((client = FindWindowExW(nullptr, client, nullptr, L"RTSSOverlayEditorClientWnd"))) {
            if (GetWindowThreadProcessId(client, &pid) == GetCurrentThreadId() &&
                pid == GetCurrentProcessId()) break;
        }
        if (!editor || !client) return Reply::EditorUnavailable;
        auto post = reinterpret_cast<PostOverlayMessageFn>(GetProcAddress(editor, "PostOverlayMessage"));
        if (!post) return Reply::EditorUnavailable;
        const Reply profile = applyShadowPolicy(packet);
        if (profile != Reply::Accepted) return profile;
        post("Load", "", packet->layout);
        // The official export POSTS UM_OVERLAY_MESSAGE. Do not report success
        // here. This FIFO barrier runs after OverlayEditor's full synchronous
        // Load/Update/InitTimer (also when the editor dialog is the handler).
        if (!PostMessageW(g_window, kComplete, packet->requestId, 0)) return Reply::QueueFailed;
        return Reply::Accepted;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return Reply::SdkException;
    }
}
static LRESULT CALLBACK bridgeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_COPYDATA) {
        auto data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (!data || data->dwData != kCopyDataTag || data->cbData != sizeof(Packet) || !data->lpData)
            return static_cast<LRESULT>(Reply::Invalid);
        Packet packet{};
        std::memcpy(&packet, data->lpData, sizeof(packet));
        if (packet.version != kVersion) return static_cast<LRESULT>(Reply::Invalid);
        if (packet.command == Command::Ping) return static_cast<LRESULT>(Reply::Complete);
        if (packet.command == Command::Status)
            return static_cast<LRESULT>(packet.requestId == g_pending.requestId ? g_reply : Reply::UnknownRequest);
        if (packet.command != Command::Load || !packet.requestId || !allowedLayout(packet.layout))
            return static_cast<LRESULT>(Reply::Invalid);
        if (g_reply == Reply::Accepted) return static_cast<LRESULT>(Reply::Busy);
        if (!templateExists(packet.layout)) return static_cast<LRESULT>(Reply::TemplateMissing);
        g_pending = packet;
        g_reply = Reply::Accepted;
        if (!PostMessageW(hwnd, kExecute, packet.requestId, 0)) g_reply = Reply::QueueFailed;
        return static_cast<LRESULT>(g_reply);
    }
    if (msg == kExecute && wParam == g_pending.requestId && g_reply == Reply::Accepted) {
        g_reply = postLoad(&g_pending);
        return 0;
    }
    if (msg == kComplete && wParam == g_pending.requestId && g_reply == Reply::Accepted) {
        wchar_t cfg[MAX_PATH]{};
        wchar_t layout[64]{};
        if (!pluginFile(L"OverlayEditor.cfg", cfg) ||
            !MultiByteToWideChar(CP_ACP, 0, g_pending.layout, -1, layout, 64) ||
            !WritePrivateProfileStringW(L"Settings", L"Layout", layout, cfg)) {
            g_reply = Reply::PersistFailed;
        } else {
            g_reply = Reply::Complete;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
extern "C" __declspec(dllexport) BOOL Start() {
    if (IsWindow(g_window)) return TRUE;
    WNDCLASSW wc{};
    wc.lpfnWndProc = bridgeWndProc;
    wc.hInstance = g_module;
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return FALSE;
    // Top-level, invisible window so x64 YMCC can discover it without injection.
    g_window = CreateWindowExW(0, kWindowClass, L"YMCC RTSS Overlay Bridge", WS_POPUP,
                              0, 0, 0, 0, nullptr, nullptr, g_module, nullptr);
    return g_window != nullptr;
}
extern "C" __declspec(dllexport) void Stop() {
    if (IsWindow(g_window)) DestroyWindow(g_window);
    g_window = nullptr;
    g_reply = Reply::UnknownRequest;
    g_pending = Packet{};
    UnregisterClassW(kWindowClass, g_module);
}
extern "C" __declspec(dllexport) BOOL Setup(HWND) { return FALSE; }
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    // No timers, windows, polling, driver access, or blocking under loader lock.
    return TRUE;
}
