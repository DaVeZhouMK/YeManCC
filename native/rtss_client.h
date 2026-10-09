// Included by main.cpp after the existing process/path/file helpers.
#include "rtss/protocol.h"
static std::mutex g_rtssBridgeMx;

static json nativeRtssReloadProfiles(const std::wstring& dllPath) {
    std::lock_guard<std::mutex> lock(g_rtssBridgeMx);
    const auto path = fspath::path(dllPath);
    if (!path.is_absolute() || ascii_lower(W2U(path.filename().wstring())) != "rtsshooks64.dll" ||
        !file_exists(dllPath) || !file_exists((path.parent_path() / L"RTSS.exe").wstring()))
        return json{{"ok", false}, {"error", "RTSSHooks64.dll 路径或配套 RTSS.exe 无效"}};
    const auto helper = exe_dir() + L"\\YMCCRtssProfileHelper.exe";
    if (!file_exists(helper))
        return json{{"ok", false}, {"error", "RTSS 安全配置助手缺失，请使用完整的新版本程序"}};
    if (!sgRunExeSync(helper, L"\"" + dllPath + L"\"", 4000))
        return json{{"ok", false}, {"error", "RTSS 配置助手失败或超时（已隔离，不会重启 YMCC/RTSS）"}};
    return json{{"ok", true}};
}
struct RtssBridgeSearch { std::wstring dir; HWND window = nullptr; };
static BOOL CALLBACK nativeRtssFindBridge(HWND window, LPARAM value) {
    auto& search = *reinterpret_cast<RtssBridgeSearch*>(value);
    wchar_t cls[96]{};
    if (!GetClassNameW(window, cls, 96) || std::wcscmp(cls, ymcc::rtss::kWindowClass)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (sameFinalPath(processImagePath(pid), search.dir + L"\\RTSS.exe")) {
        search.window = window;
        return FALSE;
    }
    return TRUE;
}
static bool nativeRtssBridgeMessage(HWND window, ymcc::rtss::Packet& packet, DWORD_PTR& reply) {
    COPYDATASTRUCT data{};
    data.dwData = ymcc::rtss::kCopyDataTag;
    data.cbData = sizeof(packet);
    data.lpData = &packet;
    SetLastError(ERROR_SUCCESS);
    if (SendMessageTimeoutW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
            SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &reply)) return true;
    // SendMessageTimeout may fail without setting last-error. Never report a
    // stale file error as the reason a live bridge failed to respond.
    if (!GetLastError()) SetLastError(ERROR_GEN_FAILURE);
    return false;
}
static bool rtssCopyAssetAtomic(const std::wstring& from, const std::wstring& to) {
    if (!file_exists(from)) { SetLastError(ERROR_FILE_NOT_FOUND); return false; }
    if (file_exists(to)) {
        const auto a = sha256File(from), b = sha256File(to);
        if (!a.empty() && a == b) return true;
    }
    const auto temp = to + L".ymcc-" + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    if (!CopyFileW(from.c_str(), temp.c_str(), FALSE)) return false;
    if (MoveFileExW(temp.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    const DWORD error = GetLastError();
    DeleteFileW(temp.c_str());
    SetLastError(error);
    return false;
}
// Installation/update is maintenance, not a prerequisite for a compatible
// RTSS-owned bridge that is already running. Do not hash/replace a loaded DLL
// or rewrite its enable flags on every layout click (locked/read-only installs
// can still have a fully usable bridge). Always verify the host path and wire ABI.
static json nativeRtssPrepareOverlay(const std::wstring& dir, const std::wstring& powerControlDir) {
    std::lock_guard<std::mutex> lock(g_rtssBridgeMx);
    const auto root = fspath::path(dir);
    const auto assets = fspath::path(powerControlDir) / L"RTSS-Overlays";
    if (!root.is_absolute() || !fspath::path(powerControlDir).is_absolute() ||
        !file_exists((root / L"RTSS.exe").wstring()) ||
        !file_exists((root / L"Plugins/Client/OverlayEditor.dll").wstring()))
        return json{{"ok", false}, {"error", "RTSS OverlayEditor 安装路径无效"}};
    RtssBridgeSearch search{dir};
    EnumWindows(nativeRtssFindBridge, reinterpret_cast<LPARAM>(&search));
    const bool live = search.window != nullptr;
    if (live) {
        ymcc::rtss::Packet ping{};
        DWORD_PTR reply = 0;
        if (!nativeRtssBridgeMessage(search.window, ping, reply)) {
            const DWORD error = GetLastError();
            return json{{"ok", false}, {"error", "RTSS 模板桥暂未响应，请稍后重试；未替换插件或重启任何进程"},
                {"errorCode", "RTSS_BRIDGE_UNRESPONSIVE"}, {"win32Error", error}};
        }
        if (reply != static_cast<DWORD_PTR>(ymcc::rtss::Reply::Complete))
            return json{{"ok", false}, {"error", "已加载的 RTSS 模板桥协议不兼容，请在退出游戏后正常退出 RTSS，再由 YMCC 启动 RTSS 完成更新"},
                {"errorCode", "RTSS_BRIDGE_INCOMPATIBLE"}, {"bridgeCode", reply}};
    }
    // Keep the source beside YMCC.exe permanently. A missing distribution
    // source remains an installation error, but must not disable a live bridge.
    const auto source = exe_dir() + L"\\YMCCOverlayBridge.dll";
    if (!live && !file_exists(source))
        return json{{"ok", false}, {"error", "YMCC 程序目录缺少 YMCCOverlayBridge.dll，请使用完整的发布包"},
            {"errorCode", "RTSS_BRIDGE_SOURCE_MISSING"}, {"path", W2U(source)}};
    std::error_code ec;
    fspath::create_directories(root / L"Plugins/Client/Overlays", ec);
    if (ec) return json{{"ok", false}, {"error", "无法创建 RTSS 模板目录"}, {"win32Error", ec.value()}};
    for (const auto* name : {L"YeManOBS-W-1.ovl", L"YeManOBS-L-1.ovl", L"YeManOBS-JJ-1.ovl", L"Empty.ovl"}) {
        const auto target = (root / L"Plugins/Client/Overlays" / name).wstring();
        // Preserve existing user-edited layouts; only supply missing shipped templates.
        if (!file_exists(target) && !rtssCopyAssetAtomic((assets / name).wstring(), target)) {
            const DWORD error = GetLastError();
            return json{{"ok", false}, {"error", "补齐 RTSS 模板失败：" + W2U(name)},
                {"win32Error", error}, {"path", W2U(target)}};
        }
    }
    if (live)
        return json{{"ok", true}, {"bridgeState", "loaded_compatible"}, {"bridgeMaintenanceDeferred", true}};
    fspath::create_directories(root / L"Profiles", ec);
    if (ec) return json{{"ok", false}, {"error", "无法创建 RTSS 配置目录"}, {"win32Error", ec.value()}};
    const auto plugin = (root / L"Plugins/Client/YMCCOverlayBridge.dll").wstring();
    if (!rtssCopyAssetAtomic(source, plugin)) {
        const DWORD error = GetLastError();
        return json{{"ok", false}, {"error", "安装或更新 RTSS 模板桥失败，请检查目录权限、文件占用及安全软件；不会强制卸载插件"},
            {"errorCode", "RTSS_BRIDGE_INSTALL_FAILED"}, {"win32Error", error}, {"path", W2U(plugin)}};
    }
    // Enable at the next normal RTSS start. Never restart a running hooked game.
    const auto config = (root / L"Profiles/Config").wstring();
    if (!WritePrivateProfileStringW(L"Plugins", L"OverlayEditor.dll", L"1", config.c_str()) ||
        !WritePrivateProfileStringW(L"Plugins", L"YMCCOverlayBridge.dll", L"1", config.c_str())) {
        const DWORD error = GetLastError();
        return json{{"ok", false}, {"error", "启用 RTSS 模板桥配置失败"},
            {"win32Error", error}, {"path", W2U(config)}};
    }
    return json{{"ok", true}, {"bridgeState", "installed"}};
}
// Deployment/enabling on disk is not proof that a running RTSS has called
// the plugin's Start(). Diagnose those separately; never force a reload.
static json nativeRtssMissingBridge(const std::wstring& dir) {
    const auto root = fspath::path(dir);
    const auto plugin = (root / L"Plugins/Client/YMCCOverlayBridge.dll").wstring();
    const auto config = (root / L"Profiles/Config").wstring();
    const bool installed = file_exists(plugin);
    const bool enabled = GetPrivateProfileIntW(L"Plugins", L"YMCCOverlayBridge.dll", 0, config.c_str()) != 0;
    std::string state, detail;
    if (!installed) {
        state = "not_installed";
        detail = "RTSS 模板桥文件缺失，请重新部署完整的 YMCCOverlayBridge.dll；不会自动重启 YMCC 或 RTSS";
    } else if (!enabled) {
        state = "disabled";
        detail = "RTSS 模板桥已安装但未启用，请在 RTSS 设置的插件页启用 YMCCOverlayBridge.dll；YMCC 无需重启";
    } else {
        state = "enabled_not_loaded";
        detail = "RTSS 模板桥已安装并启用，但当前 RTSS 进程尚未加载或启动它。请先退出游戏及其他被 RTSS 挂钩的程序，再正常重启 RTSS 一次；YMCC 无需重启，模板切换也不会自动重启任何进程。若重新启动 RTSS 后仍出现此提示，请检查插件加载错误";
    }
    return json{{"ok", false}, {"error", detail}, {"errorCode", "RTSS_BRIDGE_UNAVAILABLE"},
        {"bridgeState", state}, {"bridgeInstalled", installed}, {"bridgeEnabled", enabled}};
}
static json nativeRtssLoadOverlay(const std::wstring& dir, const std::string& layout) {
    using namespace ymcc::rtss;
    std::lock_guard<std::mutex> lock(g_rtssBridgeMx);
    if (layout.size() >= sizeof(Packet{}.layout))
        return json{{"ok", false}, {"error", "无效的 RTSS 模板名称"}};
    Packet packet{};
    std::memcpy(packet.layout, layout.c_str(), layout.size() + 1);
    if (!allowedLayout(packet.layout)) return json{{"ok", false}, {"error", "不支持的 RTSS 模板"}};
    RtssBridgeSearch search{dir};
    EnumWindows(nativeRtssFindBridge, reinterpret_cast<LPARAM>(&search));
    if (!search.window) return nativeRtssMissingBridge(dir);
    static std::atomic<std::uint32_t> next{static_cast<std::uint32_t>(GetTickCount()) | 1u};
    packet.requestId = ++next;
    if (!packet.requestId) packet.requestId = ++next;
    packet.command = Command::Load;
    DWORD_PTR reply = 0;
    if (!nativeRtssBridgeMessage(search.window, packet, reply))
        return json{{"ok", false}, {"error", "RTSS 模板桥未响应，未确认切换结果"}, {"win32Error", GetLastError()}};
    const ULONGLONG deadline = GetTickCount64() + 4000;
    while (reply == static_cast<DWORD_PTR>(Reply::Accepted) && GetTickCount64() < deadline) {
        Sleep(20);
        packet.command = Command::Status;
        if (!nativeRtssBridgeMessage(search.window, packet, reply)) {
            // Sensor polling/full hypertext rebuild may occupy RTSS's UI thread
            // longer than a single 200ms message bound. Retry STATUS, never LOAD.
            reply = static_cast<DWORD_PTR>(Reply::Accepted);
            if (!IsWindow(search.window)) break;
        }
    }
    if (reply != static_cast<DWORD_PTR>(Reply::Complete)) {
        std::string detail;
        switch (static_cast<Reply>(reply)) {
        case Reply::Accepted: detail = "RTSS 模板加载超时，结果未知；未自动重启任何进程"; break;
        case Reply::Invalid: detail = "RTSS 模板桥协议不兼容，需在 RTSS 下次正常启动前更新插件"; break;
        case Reply::Busy: detail = "RTSS 正在加载另一份模板，请稍后再试"; break;
        case Reply::EditorUnavailable: detail = "RTSS OverlayEditor 插件未就绪，请在 RTSS 插件页启用它"; break;
        case Reply::TemplateMissing: detail = "RTSS 模板缺失或不完整，保留原模板"; break;
        case Reply::PersistFailed: detail = "模板设置保存失败，请检查 RTSS 目录写入权限"; break;
        case Reply::ProfileUnavailable: detail = "RTSS 配置 SDK 未就绪，保留原模板"; break;
        case Reply::SdkException: detail = "RTSS SDK 调用异常，YMCC 未加载该第三方 DLL"; break;
        default: detail = "RTSS 未完成模板完整加载（错误码 " + std::to_string(reply) + "）"; break;
        }
        return json{{"ok", false}, {"error", detail}, {"bridgeCode", reply}};
    }
    return json{{"ok", true}, {"layout", layout}};
}
