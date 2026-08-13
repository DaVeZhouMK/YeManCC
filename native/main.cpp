// 强强 — Ultra-thin Win32 + WebView2 shell
// Single-file C++ shell with full native API surface.
//
// Build:
//   cl /EHsc /O2 /std:c++20 /utf-8 main.cpp
//      /I<webview2_include> /I<json_include>
//      /Fe:app.exe /link /SUBSYSTEM:WINDOWS <WebView2LoaderStatic.lib>
//
// Usage:
//   app.exe                              -> Production (virtual host -> dist/)
//   app.exe --dev http://localhost:3000  -> Dev mode

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <dbt.h>
#include <cfgmgr32.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <initguid.h>
#include <devpkey.h>
#include <xinput.h>
#include <wrl.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <limits>
#include <cstring>
#include <cstdlib>
#include <dwmapi.h>
#include <gdiplus.h>
#include <windowsx.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <cctype>
#include <cwctype>
#include <mutex>
#include <atomic>
#include <cstdio>
#include <thread>
#include <deque>
#include <condition_variable>
#include <unordered_set>
#include <set>
#include <map>
#include <cstdarg>
#include <cmath>
#include <chrono>
#include <ctime>
#include <new>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winhttp.lib")
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")
#include <sddl.h>
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "cfgmgr32.lib")

#include "resource.h"

#include <psapi.h>      // GetProcessMemoryInfo（进程工作集）
#include <tlhelp32.h>   // CreateToolhelp32Snapshot / Process32*W（进程枚举）
#include <powrprof.h>   // SetSuspendState / GetPwrCapabilities（升级 S4/关机）
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "PowrProf.lib")
#pragma comment(lib, "xinput9_1_0.lib")  // 后台手柄呼出：LB+RB 2 秒呼出程序（系统自带，无需重分发）

#include "json.hpp"
#include "version.h"
using json = nlohmann::json;
using namespace Microsoft::WRL;
namespace fspath = std::filesystem;

// ================================================================
//  String helpers
// ================================================================

static std::string W2U(const wchar_t* w, int len = -1) {
    if (!w || !*w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, len, s.data(), n, nullptr, nullptr);
    if (len == -1 && !s.empty() && s.back() == '\0') s.pop_back();
    return s;
}
static std::string W2U(const std::wstring& w) { return W2U(w.c_str(), (int)w.size()); }

static std::wstring U2W(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

// 控制台子进程（powercfg/schtasks/cmd 等）输出为系统 OEM 代码页（中文机=GBK），
// 原始字节直接进 JSON 会变成乱码且无法被前端正则匹配。这里统一转成 UTF-8，
// 保证 webview 端拿到可读中文。纯 ASCII 内容在此转换下保持不变，安全。
static std::string oemToUtf8(const std::string& bytes) {
    if (bytes.empty()) return bytes;
    UINT cp = GetOEMCP();
    if (cp == CP_UTF8) return bytes;
    int wlen = MultiByteToWideChar(cp, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
    if (wlen <= 0) return bytes;
    std::wstring w(wlen, L'\0');
    MultiByteToWideChar(cp, 0, bytes.data(), (int)bytes.size(), w.data(), wlen);
    return W2U(w);
}

static std::string ascii_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return s;
}

static bool isSteamHostName(const wchar_t* host) {
    const auto value = ascii_lower(W2U(host));
    return value == "steampowered.com" || value.ends_with(".steampowered.com") ||
           value == "steamstatic.com" || value.ends_with(".steamstatic.com") ||
           value == "steamusercontent.com" || value.ends_with(".steamusercontent.com") ||
           value == "akamaihd.net" || value.ends_with(".akamaihd.net") ||
           value == "eccdnx.com" || value.ends_with(".eccdnx.com");
}

static constexpr DWORD DEFAULT_HTTP_TIMEOUT_MS = 30000;

static void setHttpTimeouts(HINTERNET session, DWORD maxTimeoutMs = DEFAULT_HTTP_TIMEOUT_MS) {
    maxTimeoutMs = (std::max)(static_cast<DWORD>(1), maxTimeoutMs);
    DWORD resolveTimeout = (std::min)(static_cast<DWORD>(10000), maxTimeoutMs);
    DWORD connectTimeout = (std::min)(static_cast<DWORD>(15000), maxTimeoutMs);
    DWORD sendTimeout = (std::min)(static_cast<DWORD>(30000), maxTimeoutMs);
    DWORD receiveTimeout = (std::min)(static_cast<DWORD>(30000), maxTimeoutMs);
    WinHttpSetTimeouts(session, resolveTimeout, connectTimeout, sendTimeout, receiveTimeout);
}

static std::string trim_ascii(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && is_space((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && is_space((unsigned char)s.back())) s.pop_back();
    return s;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static std::string url_decode_path(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); i++) {
        if (value[i] == '%' && i + 2 < value.size()) {
            int hi = hex_value(value[i + 1]);
            int lo = hex_value(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i] == '\\' ? '/' : value[i]);
    }
    return out;
}

static bool is_allowed_shell_target(const std::string& target) {
    auto value = trim_ascii(target);
    if (value.empty()) return false;
    if (std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20; })) return false;

    auto lower = ascii_lower(value);
    auto colon = lower.find(':');
    if (colon != std::string::npos) {
        if (colon == 1 && std::isalpha((unsigned char)lower[0]) &&
            value.size() > 2 && (value[2] == '\\' || value[2] == '/')) {
            return true;
        }

        auto scheme = lower.substr(0, colon + 1);
        return scheme == "http:" || scheme == "https:" || scheme == "mailto:" || scheme == "file:" || scheme == "steam:";
    }

    return value.rfind("\\\\", 0) == 0;
}

static bool is_http_token(const std::string& value) {
    if (value.empty() || value.size() > 32) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_';
    });
}

static bool has_header_injection_chars(const std::string& value) {
    return value.find('\r') != std::string::npos || value.find('\n') != std::string::npos;
}

static std::wstring quote_windows_arg(const std::wstring& arg) {
    if (arg.empty()) return L"\"\"";
    bool needsQuotes = arg.find_first_of(L" \t\n\v\"") != std::wstring::npos;
    if (!needsQuotes) return arg;

    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            backslashes++;
        } else if (ch == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(ch);
            backslashes = 0;
        } else {
            out.append(backslashes, L'\\');
            backslashes = 0;
            out.push_back(ch);
        }
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

static std::wstring safe_path_component(std::wstring value, const std::wstring& fallback) {
    for (auto& ch : value) {
        if (ch < 32 || wcschr(L"<>:\"/\\|?*", ch)) ch = L'-';
    }
    while (!value.empty() && (value.back() == L'.' || value.back() == L' ')) value.pop_back();
    while (!value.empty() && value.front() == L' ') value.erase(value.begin());
    return value.empty() ? fallback : value;
}

static bool is_dangerous_remove_target(const std::string& rawPath) {
    auto path = trim_ascii(rawPath);
    if (path.empty() || path == "/" || path == "\\") return true;
    if (path.size() == 2 && path[1] == ':' && std::isalpha((unsigned char)path[0])) return true;

    std::error_code ec;
    auto normalized = fspath::weakly_canonical(fspath::path(U2W(path)), ec);
    if (ec) {
        ec.clear();
        normalized = fspath::absolute(fspath::path(U2W(path)), ec);
        if (ec) return true;
    }
    normalized = normalized.lexically_normal();

    const auto root = normalized.root_path();
    return !root.empty() && normalized == root;
}

static std::wstring exe_dir() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    PathRemoveFileSpecW(p);
    return p;
}

static bool file_exists(const std::wstring& path) {
    std::error_code ec;
    return fspath::is_regular_file(fspath::path(path), ec);
}

static std::vector<std::wstring> production_asset_dirs() {
    std::vector<std::wstring> dirs;
    auto push_unique = [&](const std::wstring& dir) {
        if (!dir.empty() && std::find(dirs.begin(), dirs.end(), dir) == dirs.end())
            dirs.push_back(dir);
    };

    auto dir = exe_dir();
    push_unique(dir);
    push_unique((fspath::path(dir) / "dist").wstring());

    auto parent = fspath::path(dir).parent_path();
    if (!parent.empty()) {
        push_unique((parent / "dist").wstring());
        push_unique(parent.wstring());
    }
    return dirs;
}

static std::wstring resolve_frontend_dir() {
    for (const auto& dir : production_asset_dirs()) {
        if (file_exists(dir + L"\\index.html"))
            return dir;
    }
    return {};
}

static std::wstring resolve_power_control_dir() {
    const std::wstring fixed = L"C:\\SOFT\\YeMan\\PowerControl";
    wchar_t overridePath[32768]{};
    const DWORD overrideLength = GetEnvironmentVariableW(
        L"YEMAN_POWER_CONTROL_DIR", overridePath,
        static_cast<DWORD>(_countof(overridePath)));
    if (overrideLength > 0 && overrideLength < _countof(overridePath)) {
        std::error_code ec;
        const fspath::path candidate(overridePath);
        if (fspath::is_directory(candidate, ec) && !ec)
            return candidate.wstring();
    }
    // Production always uses the shared sibling directory. The old fallback
    // to <YeManCC>\PowerControl made a malformed legacy update look healthy
    // while silently splitting runtime files across two locations.
    return fixed;
}

static const std::wstring POWER_CONTROL_DIR = resolve_power_control_dir();

// ================================================================
//  Global state
// ================================================================

static HWND                              g_hwnd;
static ComPtr<ICoreWebView2Environment>  g_env;
static ComPtr<ICoreWebView2Controller>   g_ctrl;
static ComPtr<ICoreWebView2>             g_view;
static std::wstring                      g_devUrl;
// `g_webviewReady` means the Vue document has painted and acknowledged a
// compositor frame. Navigation completion alone is not sufficient: on a cold
// WebView2 start an internal about:blank navigation can complete first.
static bool                              g_webviewReady = false;
static bool                              g_webviewNavigationReady = false;
static unsigned long long                g_webviewNavigationGeneration = 0;
static UINT64                            g_webviewNavigationId = 0;
static UINT64                            g_webviewCompletedNavigationId = 0;
static unsigned long long                g_webviewRenderReadyGeneration = 0;
static UINT64                            g_webviewRenderReadyNavigationId = 0;
static bool                              g_webviewNeedsShowNudge = false;
static std::wstring                      g_updateHandshakePath;
static std::string                       g_updateHandshakeToken;
static std::mutex                        g_updateProgressMtx;
static json                               g_updateProgress = json::object();

static void updateProgressPost(const json& data);
static json updateProgressRead();

enum class WebViewGpuMode : uint8_t { Default, Legacy, Software };
enum class WebViewDeferredRecovery : uint8_t { None, Reload, RecreateController };
static WebViewGpuMode                     g_webviewGpuMode = WebViewGpuMode::Default;
static std::wstring                       g_webviewDataDir;
static bool                               g_webviewDataDirInitialized = false;
static std::mutex                         g_webviewFailureLogMx;
static std::atomic<unsigned long long>    g_webviewGeneration{1};
static bool                               g_webviewRecoveryInProgress = false;
static std::string                        g_webviewRecoveryAction;
static WebViewDeferredRecovery            g_webviewDeferredRecovery = WebViewDeferredRecovery::None;
static std::deque<ULONGLONG>              g_webviewRendererFailures;
static std::deque<ULONGLONG>              g_webviewGpuFailures;
static std::deque<ULONGLONG>              g_webviewUtilityFailures;
static std::deque<ULONGLONG>              g_webviewBrowserFailures;

struct WebViewFailureInfo {
    COREWEBVIEW2_PROCESS_FAILED_KIND kind = COREWEBVIEW2_PROCESS_FAILED_KIND_UNKNOWN_PROCESS_EXITED;
    COREWEBVIEW2_PROCESS_FAILED_REASON reason = COREWEBVIEW2_PROCESS_FAILED_REASON_UNEXPECTED;
    int exitCode = 0;
    std::wstring description;
    std::wstring modulePath;
    ULONGLONG tick = 0;
};

// Config
static json g_cfg;
static bool g_frameless    = false;
static bool g_rounded      = true;   // Win11 rounded corners + DWM shadow for frameless windows
static bool g_saveWindowState = true;
static bool g_fullHeight   = false;  // 全屏高度 + 右侧吸附模式
static bool g_startMinimized = false; // 启动参数 --minimized：开机最小化启动（留在任务栏）

// ── 按键呼出（后台手柄呼出）──
static bool g_summonEnabled = true;   // 「按键呼出」开关：后台按住 LB+RB 呼出程序（设置可关，默认开）
static bool g_bDoubleMinimize = true; // 双击 B 最小化到托盘（设置可关）
struct FocusTargetSnapshot {
    HWND hwnd = nullptr;
    DWORD pid = 0;
    ULONGLONG processCreated = 0;
    std::wstring path;
    std::wstring className;
    std::wstring monitorDevice;
    RECT monitorRect{};
    bool fullscreen = false;
    bool valid = false;
};
struct FocusSessionState {
    FocusTargetSnapshot target;
    bool active = false;
    bool returning = false;
    bool ownedTopmost = false;
    ULONGLONG returnStarted = 0;
    ULONGLONG returnDeadline = 0;
};
static FocusSessionState g_focusSession;
static FocusTargetSnapshot g_pendingSummonTarget; // LB/RB 首个肩键按下时抓取，防覆盖层在0.5s内抢前台
static FocusTargetSnapshot g_rememberedGameTarget; // 一键启动LS前由前端登记的真实游戏
static ULONGLONG g_rememberedGameDeadline = 0;
// 手动暂停中的 PID 绝不能成为回焦目标。否则暂停后隐藏/再次呼出时，
// 焦点状态机可能对已冻结窗口反复执行跨线程窗口操作，拖住宿主消息泵。
static std::atomic<DWORD> g_manualPausedPid{0};
// Process control is dispatched to a worker. Block all focus operations for
// the complete transaction, including the interval before its marker/result
// is written back to the UI thread.
static std::atomic<bool> g_gameControlInFlight{false};
static std::mutex g_rememberedGameTargetMx;
static int g_summonFocusRetries = 0;
static bool g_summonAltTried = false;
static bool g_bClosePending = false;       // 双击B后等待第二次B释放，避免按键泄漏到游戏
static ULONGLONG g_bClosePendingSince = 0;
static ULONGLONG g_bCloseReadyAt = 0;
static bool g_tdpShortcut = true;    // Start + 上/下 快捷调节 TDP 最大值（前端处理，持久化在 native）
static bool g_fpsShortcut = true;    // Start + 左/右 快捷调节 RTSS 锁帧（前端处理，持久化在 native）
static bool g_killGame = false;       // 选择(Back) + B 长按 0.5s → 结束当前游戏（执行 KiLL-EXE.bat）
static bool g_openKeyboard = false;   // 选择(Back) + X 长按 0.5s → 打开 Windows 触摸键盘
static bool g_returnDesktop = false;  // 选择(Back) + A 组合按下瞬间 → 返回桌面
static bool g_mouseToggle = false;    // 选择(Back) + Y 长按 0.5s → 模拟鼠标开/关
static std::string g_mouseBackend = "joyxoff"; // 模拟鼠标方案：joyxoff / gamebar
static std::atomic<bool> g_summonQuit{false};  // 退出信号（跨线程原子；手柄已改 Raw Input，掌机自动关闭线程仍复用）

// ── 掌机前端自动关闭（后台轮询 5 秒，温和关闭 = 发 WM_CLOSE）──
static std::atomic<bool> g_autoCloseEnabled{false};   // 总开关（原子读写，跨线程）
static std::vector<std::string> g_autoCloseProcs;     // 目标进程名列表（可带/不带 .exe，支持 * 前缀）
static std::mutex g_autoCloseMx;                       // 保护 procs 列表（前端 set 与后台线程读）
static HANDLE g_autoCloseThread = nullptr;
static std::atomic<bool> g_exitRequested{false};
static std::atomic<WPARAM> g_exitCode{0};
static HANDLE g_exitCleanupThread = nullptr;
static int  g_baseW        = 580;    // 设计基准宽（缩放比例分子）
static int  g_baseH        = 780;    // 设计基准高（缩放比例分母）
static bool g_resizing     = false;   // WM_ENTERSIZEMOVE / WM_EXITSIZEMOVE guard
static bool g_allowWebviewPermissions = false;

// 全屏高度 + 右侧吸附布局：
// 窗口高度 = 工作区高度（上下贴合全屏），宽度按设计基准比例缩放，并吸附屏幕右侧。
static void applyFullHeightLayout(HMONITOR preferredMonitor = nullptr) {
    if (!g_hwnd) return;
    HMONITOR mon = preferredMonitor ? preferredMonitor : MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(mon, &mi)) return;
    int waX = mi.rcWork.left;
    int waY = mi.rcWork.top;
    int waW = mi.rcWork.right - mi.rcWork.left;
    int waH = mi.rcWork.bottom - mi.rcWork.top;
    if (g_baseH <= 0) return;
    double R = (double)g_baseW / (double)g_baseH; // 设计基准宽高比
    int targetH = waH;                            // 横屏默认上下贴合全屏
    int targetW = (int)round((double)targetH * R);
    bool widthLimited = false;
    if (targetW > waW) {                          // 极窄屏保护：以宽度为准
        widthLimited = true;
        targetW = waW;
        targetH = (int)round((double)targetW / R);
    }
    int x = waX + waW - targetW;                  // 吸附右侧
    int y = widthLimited ? waY + (waH - targetH) / 2 : waY; // 逻辑竖屏时垂直居中
    SetWindowPos(g_hwnd, nullptr, x, y, targetW, targetH, SWP_NOZORDER | SWP_NOACTIVATE);
}
static int  g_effectType   = 0; // 0=none, 2=mica, 3=acrylic, 4=micaAlt
static bool g_deferFirstShow = false;       // showWhenReady: keep window hidden until WebView2 paints
static int  g_firstShowCmd   = SW_SHOWNORMAL; // the show command to use once content is ready
static std::unordered_map<std::string, bool> g_permissions; // cmd -> allowed

// Tray
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_SHOW 4001
#define ID_TRAY_MIN  4002
#define ID_TRAY_EXIT 4003
#define ID_TRAY_SEP  4004
static NOTIFYICONDATAW g_nid = {};
static bool            g_taskbarResident = false; // 任务栏常驻开关（前端 tray.create/remove 同步，默认不常驻）
static HINSTANCE       g_hinst = nullptr;
static HWND            g_menuHwnd = nullptr;
struct TrayMenuItem { std::wstring label; int cmd; bool danger = false; };
static std::vector<TrayMenuItem> g_menuItems;
static std::vector<RECT> g_menuItemRects;
static int g_menuHover = -1;
static int g_menuW = 0, g_menuH = 0;
static bool g_menuClassReg = false;
// 配色（对齐前端 tokens.css：--bg-panel #121722 / --text #e9eef5 / --text-dim #8b96a8 / --bg-input #1a2230 / --danger #e5484d）
static const COLORREF MENU_BG     = RGB(0x12,0x17,0x22);
static const COLORREF MENU_TEXT   = RGB(0xe9,0xee,0xf5);
static const COLORREF MENU_DIM    = RGB(0x8b,0x96,0xa8);
static const COLORREF MENU_HOVER  = RGB(0x1a,0x22,0x30);
static const COLORREF MENU_SEP     = RGB(0x2a,0x33,0x42);
static const COLORREF MENU_DANGER = RGB(0xe5,0x48,0x4d);
static const int MENU_RADIUS = 14; // +15% 气泡放大
static bool            g_trayActive = false;
static UINT            g_taskbarCreatedMsg = 0;
static UINT            g_showExistingInstanceMsg = 0;
static HICON           g_appIconLarge = nullptr;
static HICON           g_appIconSmall = nullptr;
static HICON           g_memTrayIcon  = nullptr; // 内存变色托盘图标（动态生成，用完 DestroyIcon）
static int             g_memTrayLevel = -1;      // 上次内存档位：-1=未初始化 0=黑(<80) 1=黄(80-90) 2=红(90+)

static HICON loadAppIcon(HINSTANCE hInstance, int cx, int cy) {
    HICON icon = (HICON)LoadImageW(
        hInstance,
        MAKEINTRESOURCEW(IDI_APP),
        IMAGE_ICON,
        cx,
        cy,
        LR_DEFAULTCOLOR);
    if (!icon) {
        icon = (HICON)LoadImageW(
            nullptr,
            IDI_APPLICATION,
            IMAGE_ICON,
            cx,
            cy,
            LR_SHARED);
    }
    return icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}

static void initAppIcons(HINSTANCE hInstance) {
    if (!g_appIconLarge) {
        g_appIconLarge = loadAppIcon(
            hInstance,
            GetSystemMetrics(SM_CXICON),
            GetSystemMetrics(SM_CYICON));
    }
    if (!g_appIconSmall) {
        g_appIconSmall = loadAppIcon(
            hInstance,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON));
    }
    if (!g_appIconSmall) g_appIconSmall = g_appIconLarge;
    if (!g_appIconLarge) g_appIconLarge = g_appIconSmall;
}

// File watchers
struct FileWatcher {
    HANDLE hDir;
    HANDLE hThread;
    std::wstring path;
    int id;
    std::atomic<bool> active;
};
static std::unordered_map<int, FileWatcher*> g_watchers;
static int g_nextWatchId = 1;

#define WM_FILE_CHANGED (WM_USER + 2)
#define WM_GAMEPAD_TDP_DELTA (WM_USER + 4)
#define WM_GAMEPAD_BRIGHTNESS (WM_USER + 5)
#define WM_APP_EXIT            (WM_USER + 6) // 任意 IPC 工作线程请求 UI 线程执行完整退出
#define WM_APP_EXIT_READY      (WM_USER + 7) // 后台退出清理完成，UI 线程只做 WebView2/窗口销毁
#define WM_POWER_RESUME_READY  (WM_USER + 8) // 睡眠守护恢复完成，通知前端开始串行恢复事务
#define WM_POWER_RESUME_COMMIT (WM_USER + 9) // 前端完成恢复事务，UI 线程统一开放硬件闸门
#define WM_WEBVIEW_PROCESS_FAILED (WM_USER + 10) // COM 回调只采集信息，UI 线程统一记录和处置
#define WM_WEBVIEW_RECOVERY_RESTART (WM_USER + 11) // browser 数据目录隔离完成，UI 线程重建 environment
#define WM_UPDATE_PROGRESS (WM_USER + 12) // 更新线程 -> UI 线程转发进度

// ================================================================
//  AC/DC 电源插拔订阅（推送/零轮询）+ 尾防抖 + 频繁切换熔断
// ================================================================
// GUID_ACDC_POWER_SOURCE = {5D3E9A59-E9D5-4B00-A6BD-FF34FF516548}
// 本地定义（static const GUID 直接分配存储，无需 INITGUID / 额外链接库）。
static const GUID YM_GUID_ACDC_POWER_SOURCE =
    { 0x5d3e9a59, 0xe9d5, 0x4b00, { 0xa6, 0xbd, 0xff, 0x34, 0xff, 0x51, 0x65, 0x48 } };

// GUID_MONITOR_POWER_ON = {02731015-4510-4526-99e6-e5a17ebd1aea}
// 该通知只表示显示器开/关，不等价于系统已进入 S3/S0 低功耗状态。
// 关屏仅更新状态；破坏性的进程冻结只由 PBT_APMSUSPEND 触发。
static const GUID YM_GUID_MONITOR_POWER_ON =
    { 0x02731015, 0x4510, 0x4526, { 0x99, 0xe6, 0xe5, 0xa1, 0x7e, 0xbd, 0x1a, 0xea } };

// GUID_ACTIVE_POWERSCHEME = {245D8541-3943-4422-B025-13A784F679B7}
// 活动电源方案变化通知：只在 Windows 广播方案 GUID 变化时通知前端，不做常驻轮询。
static const GUID YM_GUID_ACTIVE_POWERSCHEME =
    { 0x245d8541, 0x3943, 0x4422, { 0xb0, 0x25, 0x13, 0xa7, 0x84, 0xf6, 0x79, 0xb7 } };

static HPOWERNOTIFY       g_acdcNotify   = nullptr; // RegisterPowerSettingNotification 句柄
static HPOWERNOTIFY       g_monitorNotify= nullptr; // 显示器开关通知句柄
static HPOWERNOTIFY       g_schemeNotify = nullptr; // 活动电源方案通知句柄
static int               g_lastAcState  = -1;      // -1=未初始化 0=离电(DC) 1=插电(AC)
static std::vector<DWORD> g_acSwitchTicks;          // 5 秒滑动窗口内的真实切换时间戳

#define TIMER_ID_ACDC     0xA100  // 尾防抖 SetTimer id（避开已用的 99）
#define ACDC_DEBOUNCE_MS  5000    // 每次变化后延迟 5s 刷新；期间再变化则顺延重新计时
#define ACDC_BURST_MS     5000    // 熔断滑动窗口 = 5s
#define ACDC_BURST_LIMIT  10      // 5s 内 >10 次切换 → 直接退出，防止系统卡死
#define MEM_TRAY_TIMER_ID 0xA201  // 内存变色托盘图标刷新（30 s）
#define SG_RESLEEP_TIMER_ID 0xA202 // 入睡失败重睡检查（250 ms）
static constexpr UINT_PTR WEBVIEW_RECOVERY_TIMER_ID = 0xA205;
static constexpr UINT_PTR WEBVIEW_RECOVERY_ACTION_TIMER_ID = 0xA206;
static constexpr UINT_PTR POWER_RESUME_NUDGE_TIMER_ID = 0xA207;
static constexpr UINT_PTR POWER_RESUME_WATCHDOG_TIMER_ID = 0xA208;
static constexpr UINT_PTR WEBVIEW_RENDER_READY_TIMER_ID = 0xA209;
static constexpr UINT_PTR WEBVIEW_POST_SHOW_NUDGE_TIMER_ID = 0xA20A;
static constexpr UINT WEBVIEW_RENDER_READY_TIMEOUT_MS = 1000;
static constexpr UINT POWER_RESUME_WATCHDOG_DELAY_MS = 8000;
static constexpr UINT POWER_RESUME_WATCHDOG_RETRY_MS = 5000;
static constexpr UINT POWER_RESUME_PROBE_RETRY_MS = 450;

// ================================================================
// 睡眠守护：入睡冻结 → 唤醒恢复；可选异常唤醒后单次重睡。
// 重睡只在启用后生效，观察窗口最多30秒，连续10秒无输入才触发，
// 每次重睡尝试后使用单调时钟抑制300秒；系统时间仅用于审计记录。
// 不升级 S4，不创建外部保底任务。
static const std::wstring SG_DIR = POWER_CONTROL_DIR + L"\\Sleep";
static const std::wstring SG_MANUAL_DIR = SG_DIR + L"\\manual-suspended";
static const std::wstring SG_KILL_LIST = SG_DIR + L"\\睡眠击杀名单.txt";
static const std::wstring SG_SLEEP_TRIGGER_MARKER = SG_DIR + L"\\sleep-trigger-last.txt";
static const std::wstring SG_RESLEEP_MARKER = SG_DIR + L"\\resleep-last.txt";
static const std::wstring TOPMON_STOP_MARKER = POWER_CONTROL_DIR + L"\\topmon.stop";
static const uint64_t     SG_MIN_WS   = 500ULL * 1024 * 1024; // 仅冻结工作集≥500MB的进程(避开系统/小进程)

static bool g_guardEnabled = false;  // 总开关（持久化 Enable.txt）
static std::atomic<bool> g_sgInSuspend{false};  // 本周期是否已冻结游戏、处于"睡眠值守"状态
static bool g_sgCleanupDone   = false;  // 正常退出清理只执行一次（app.exit/WM_DESTROY/会话结束共用）
static std::mutex g_sgOpMtx;         // 串行化冻结/恢复/退出清理，避免电源事件与 IPC 同时操作同一进程
static DWORD g_sgSessionId = 0;      // 当前会话 ID（仅冻结同会话进程，避开系统/其他用户会话）
static bool g_sgSessionValid = false; // 会话获取失败时 fail-closed，禁止跨会话冻结/击杀

static void stopNativeMonitorForExit();
static void cleanupExitArtifacts();
static void sgCleanupBeforeExit();
static void sgStopWorkThread();
static void poolStop();
static bool stopWatcher(FileWatcher* w, DWORD timeoutMs);
static void beginAsyncExit(HWND hwnd, WPARAM code);

static void stopTopMonitorForExit() {
    stopNativeMonitorForExit();
    cleanupExitArtifacts();
}

// ── 睡眠守护可调参数（持久化于 Sleep\sleepguard.json，前端控制面修改）──
// 入睡（PBT_APMSUSPEND）→ 冻结最大工作集进程；
// 唤醒（PBT_APMRESUME*）→ 直接恢复冻结进程。显示器通知仅记录状态。
static std::string g_sgMode   = "off"; // 总开关模式：off / custom
static bool g_sgPauseResume   = true;  // 睡眠时暂停游戏 + 唤醒时自动恢复（两者绑定，只一个开关）
static bool g_sgKillListEnabled = false; // 入睡前终止 Sleep\\睡眠击杀名单.txt 中的指定 exe
static bool g_sgResleepEnabled = false; // 异常唤醒后满足静默条件时重新进入睡眠
static bool g_monitorOn       = true;  // 当前显示器状态
static ULONGLONG g_sgSleepTriggerTick = 0; // 最近一次睡眠触发的单调时间
static ULONGLONG g_sgWakeTick = 0; // 最近一次唤醒的单调时间
static ULONGLONG g_sgLastInputTick = 0; // 最近一次手柄/键盘输入的单调时间
static ULONGLONG g_sgResleepCooldownTick = 0; // 最近一次重睡后的5分钟抑制截止时间
static double g_sgResleepCooldownEpoch = 0.0; // 跨进程保留的重睡系统时间
static ULONGLONG g_lastResumeNotifyTick = 0; // 同一唤醒周期的前端事件去重时间
static bool g_sgResleepPending = false; // 已进入重睡观察窗口
static double g_sgSleepTriggerEpoch = 0.0; // 审计用系统时间（Unix秒）

// 电源生命周期状态。WM_POWERBROADCAST 只切换状态/排队，不阻塞系统电源回调；
// 所有真正的恢复完成通知都在统一事务提交后由 UI 线程发出。
enum class PowerLifecycle : uint8_t { Ready, Suspending, Suspended, Resuming };
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<unsigned long long> g_powerGeneration{1};
static std::atomic<bool> g_hardwareWriteGate{true};
static std::atomic<bool> g_inputReady{true};
static std::atomic<bool> g_inputReleaseRequired{false};
static ULONGLONG g_resumeReadyTick = 0;
static std::atomic<unsigned long long> g_resumeReadyGeneration{0};
static unsigned long long g_resumeWatchdogGeneration = 0;
static unsigned int g_resumeWatchdogAttempts = 0;
static unsigned long long g_resumeProbeGeneration = 0;
static unsigned int g_resumeProbeAttempts = 0;
static bool g_resumeProbeInFlight = false;
static bool g_resumeProbeForcedReset = false;
static void traceLog(const char* fmt, ...);
static void traceInit();

static const char* powerLifecycleName(PowerLifecycle state) {
    switch (state) {
    case PowerLifecycle::Ready: return "ready";
    case PowerLifecycle::Suspending: return "suspending";
    case PowerLifecycle::Suspended: return "suspended";
    case PowerLifecycle::Resuming: return "resuming";
    }
    return "ready";
}

static bool hardwareWriteAllowed() {
    return g_hardwareWriteGate.load(std::memory_order_acquire) &&
           g_powerLifecycle.load(std::memory_order_acquire) == PowerLifecycle::Ready;
}

static unsigned long long currentPowerGeneration() {
    return g_powerGeneration.load(std::memory_order_acquire);
}

static bool powerLifecycleMatches(PowerLifecycle phase, unsigned long long generation) {
    return currentPowerGeneration() == generation &&
        g_powerLifecycle.load(std::memory_order_acquire) == phase;
}

static void closeHardwareWriteGate(const char* reason) {
    g_hardwareWriteGate.store(false, std::memory_order_release);
    traceLog("power gate closed reason=%s generation=%llu", reason ? reason : "unknown",
             currentPowerGeneration());
}

static void openHardwareWriteGate() {
    g_hardwareWriteGate.store(true, std::memory_order_release);
    traceLog("power gate opened generation=%llu", currentPowerGeneration());
}

// 电源通知只负责排队，冻结/恢复在独立生命周期线程执行，避免阻塞窗口消息线程。
enum class SgWork : uint8_t { Suspend, WakeAutomatic, WakeSuspend };
struct SgWorkItem {
    SgWork kind;
    unsigned long long generation;
};
static std::mutex g_sgWorkMx;
static std::condition_variable g_sgWorkCv;
static std::deque<SgWorkItem> g_sgWorkQ;
static std::thread g_sgWorkThread;
static bool g_sgWorkStop = false;
#define SG_OVERHEAT_TIMER_ID 0xA203
static bool g_sgOverheatSleepEnabled = false;
static int g_sgOverheatTempC = 95;
static ULONGLONG g_sgOverheatAboveTick = 0;
static ULONGLONG g_sgOverheatLastInputTick = 0;
static bool g_sgOverheatAttempted = false;

// ntdll 运行时解析（无需额外链接库）
typedef LONG (NTAPI* NtSuspendProcess_t)(HANDLE);
typedef LONG (NTAPI* NtResumeProcess_t)(HANDLE);
static NtSuspendProcess_t fnNtSuspend = nullptr;
static NtResumeProcess_t  fnNtResume  = nullptr;
static bool g_sgNtInit = false;
static void sgInitNt() {
    if (g_sgNtInit) return;
    g_sgNtInit = true;
    HMODULE h = GetModuleHandleW(L"ntdll.dll");
    if (h) {
        fnNtSuspend = (NtSuspendProcess_t)GetProcAddress(h, "NtSuspendProcess");
        fnNtResume  = (NtResumeProcess_t)GetProcAddress(h, "NtResumeProcess");
    }
}

// ---- 小工具 ----
static double sgNowEpoch() { // Unix 秒(含亚秒)，用于 300s 窗口精确比对
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER ul; ul.LowPart = ft.dwLowDateTime; ul.HighPart = ft.dwHighDateTime;
    return (double)(ul.QuadPart - 116444736000000000ULL) / 1e7;
}
static std::wstring sgBaseName(const std::wstring& path) { // 取文件名并去 .exe/小写
    std::wstring f = path;
    auto p = f.find_last_of(L"\\/");
    if (p != std::wstring::npos) f = f.substr(p + 1);
    if (f.size() > 4 && (f.compare(f.size() - 4, 4, L".exe") == 0 ||
                          f.compare(f.size() - 4, 4, L".EXE") == 0))
        f = f.substr(0, f.size() - 4);
    std::transform(f.begin(), f.end(), f.begin(), ::towlower);
    return f;
}
static void sgWriteFile(const std::wstring& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    if (f) f.write(content.data(), (std::streamsize)content.size());
}
// 原子写：同目录唯一临时文件 + 分片路径锁 + 全量 WriteFile/FlushFileBuffers 校验。
// 每个成功返回只提交本次调用自己的内容；任何短写/刷盘/替换失败均保留原文件。
static bool sgWriteFileAtomic(const std::wstring& path, const std::string& content) {
    static std::mutex pathLocks[32];
    static std::atomic<unsigned long long> tempSeq{1};

    std::wstring lockKey = path;
    std::transform(lockKey.begin(), lockKey.end(), lockKey.begin(), ::towlower);
    std::lock_guard<std::mutex> pathLock(pathLocks[std::hash<std::wstring>{}(lockKey) % 32]);

    const auto seq = tempSeq.fetch_add(1);
    std::wstring tmp = path + L".ymcc." + std::to_wstring(GetCurrentProcessId()) +
        L"." + std::to_wstring(seq) + L".tmp";
    HANDLE file = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    bool writeOk = true;
    size_t offset = 0;
    while (offset < content.size()) {
        const size_t remaining = content.size() - offset;
        const DWORD chunk = static_cast<DWORD>((std::min)(remaining, static_cast<size_t>(1u << 20)));
        DWORD written = 0;
        if (!WriteFile(file, content.data() + offset, chunk, &written, nullptr) || written != chunk) {
            writeOk = false;
            break;
        }
        offset += written;
    }
    if (writeOk && !FlushFileBuffers(file)) writeOk = false;
    if (!CloseHandle(file)) writeOk = false;
    if (!writeOk) {
        DeleteFileW(tmp.c_str());
        return false;
    }

    // 1) 首选 MoveFileEx（原子替换）。目标可能被 RTSS 钩子短暂占用 → 重试。
    for (int attempt = 0; attempt < 6; attempt++) {
        if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;
        if (attempt < 5) Sleep(50);
    }
    // 2) 退路 ReplaceFileW。仍失败时原文件保持完整，只删除本次调用自己的临时文件。
    if (ReplaceFileW(path.c_str(), tmp.c_str(), nullptr, REPLACEFILE_WRITE_THROUGH, nullptr, 0))
        return true;
    DeleteFileW(tmp.c_str());
    return false;
}

static void signalUpdateHandshake() {
    if (g_updateHandshakePath.empty() || g_updateHandshakeToken.empty()) return;
    json marker = {
        {"phase", "started"},
        {"pid", GetCurrentProcessId()},
        {"token", g_updateHandshakeToken}
    };
    if (!sgWriteFileAtomic(g_updateHandshakePath, marker.dump())) {
        traceLog("UPDATE handshake marker write failed");
    }
}

static std::string sgReadFile(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// 用户持久化配置唯一入口。运行快照、日志、名单和媒体仍使用各自的
// 独立文件；只有低频用户设置进入 yeman-settings.json。
static const std::wstring YM_SETTINGS_FILE = POWER_CONTROL_DIR + L"\\yeman-settings.json";
static const std::wstring YM_SETTINGS_BACKUP_FILE = YM_SETTINGS_FILE + L".bak";
static std::mutex g_settingsFileMtx;

static std::wstring normalizeWindowsPathForCompare(std::wstring path) {
    for (auto& ch : path) {
        if (ch == L'/') ch = L'\\';
    }
    wchar_t full[32768]{};
    const DWORD length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(_countof(full)), full, nullptr);
    if (length > 0 && length < _countof(full)) path.assign(full, length);
    while (path.size() > 3 && !path.empty() && path.back() == L'\\') path.pop_back();
    for (auto& ch : path) ch = static_cast<wchar_t>(towlower(ch));
    return path;
}

static bool sameWindowsPath(const std::wstring& left, const std::wstring& right) {
    return normalizeWindowsPathForCompare(left) == normalizeWindowsPathForCompare(right);
}

static json ymSettingsReadUnlocked() {
    const auto raw = sgReadFile(YM_SETTINGS_FILE);
    if (!raw.empty()) {
        try {
            auto j = json::parse(raw);
            if (j.is_object()) return j;
        } catch (...) {}
    }
    const auto backup = sgReadFile(YM_SETTINGS_BACKUP_FILE);
    if (!backup.empty()) {
        try {
            auto j = json::parse(backup);
            if (j.is_object()) return j;
        } catch (...) {}
    }
    return json::object();
}
class SettingsFileGuard {
public:
    SettingsFileGuard() : local_(g_settingsFileMtx) {
        mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\YeManCC-UnifiedSettings");
        if (mutex_) WaitForSingleObject(mutex_, INFINITE);
    }
    ~SettingsFileGuard() {
        if (mutex_) {
            ReleaseMutex(mutex_);
            CloseHandle(mutex_);
        }
    }
    SettingsFileGuard(const SettingsFileGuard&) = delete;
    SettingsFileGuard& operator=(const SettingsFileGuard&) = delete;
private:
    std::unique_lock<std::mutex> local_;
    HANDLE mutex_ = nullptr;
};
static bool ymSettingsWriteDocumentUnlocked(const std::string& content) {
    json parsed;
    try {
        parsed = json::parse(content);
    } catch (...) {
        return false;
    }
    if (!parsed.is_object()) return false;
    const auto raw = sgReadFile(YM_SETTINGS_FILE);
    json old;
    bool oldValid = false;
    if (!raw.empty()) {
        try {
            old = json::parse(raw);
            oldValid = old.is_object();
        } catch (...) {}
    }
    if (!raw.empty() && !oldValid) {
        const auto corrupt = YM_SETTINGS_FILE + L".corrupt-native-" + std::to_wstring(GetTickCount64());
        sgWriteFileAtomic(corrupt, raw);
    }
    if (oldValid) sgWriteFileAtomic(YM_SETTINGS_BACKUP_FILE, raw);
    return sgWriteFileAtomic(YM_SETTINGS_FILE, parsed.dump(2));
}
static bool ymSettingsWriteDocument(const std::string& content) {
    SettingsFileGuard guard;
    return ymSettingsWriteDocumentUnlocked(content);
}
static json ymSettingsRead() {
    SettingsFileGuard guard;
    return ymSettingsReadUnlocked();
}
static json ymSettingsSection(const char* section);
static bool ymSettingsWriteSection(const char* section, const json& value) {
    SettingsFileGuard guard;
    const auto raw = sgReadFile(YM_SETTINGS_FILE);
    auto all = ymSettingsReadUnlocked();
    if (!all.is_object()) all = json::object();
    all["schemaVersion"] = (std::max)(1, all.value("schemaVersion", 1));
    if (!all.contains("baselineId")) all["baselineId"] = "2026-08-09-user-default";
    all[section] = value;
    // Keep the last valid main file available for recovery before replacing it.
    if (!raw.empty()) {
        try {
            auto old = json::parse(raw);
            if (old.is_object()) sgWriteFileAtomic(YM_SETTINGS_BACKUP_FILE, raw);
            else sgWriteFileAtomic(YM_SETTINGS_FILE + L".corrupt-native-" + std::to_wstring(GetTickCount64()), raw);
        } catch (...) {
            sgWriteFileAtomic(YM_SETTINGS_FILE + L".corrupt-native-" + std::to_wstring(GetTickCount64()), raw);
        }
    }
    return sgWriteFileAtomic(YM_SETTINGS_FILE, all.dump(2));
}
static bool ymSettingsExists() {
    return fspath::exists(YM_SETTINGS_FILE);
}
static json ymSettingsSection(const char* section) {
    const auto all = ymSettingsRead();
    auto it = all.find(section);
    return it != all.end() && it->is_object() ? *it : json::object();
}
static bool ymSettingsPatchSection(const char* section, const json& patch) {
    SettingsFileGuard guard;
    const auto all = ymSettingsReadUnlocked();
    auto it = all.find(section);
    auto current = it != all.end() && it->is_object() ? *it : json::object();
    if (!current.is_object()) current = json::object();
    if (patch.is_object()) {
        for (auto it = patch.begin(); it != patch.end(); ++it) current[it.key()] = it.value();
    }
    auto next = all.is_object() ? all : json::object();
    next["schemaVersion"] = (std::max)(1, next.value("schemaVersion", 1));
    if (!next.contains("baselineId")) next["baselineId"] = "2026-08-09-user-default";
    next[section] = current;
    const auto raw = sgReadFile(YM_SETTINGS_FILE);
    if (!raw.empty()) {
        try {
            auto old = json::parse(raw);
            if (old.is_object()) sgWriteFileAtomic(YM_SETTINGS_BACKUP_FILE, raw);
            else sgWriteFileAtomic(YM_SETTINGS_FILE + L".corrupt-native-" + std::to_wstring(GetTickCount64()), raw);
        } catch (...) {
            sgWriteFileAtomic(YM_SETTINGS_FILE + L".corrupt-native-" + std::to_wstring(GetTickCount64()), raw);
        }
    }
    return sgWriteFileAtomic(YM_SETTINGS_FILE, next.dump(2));
}

// ================================================================
// Native monitor daemon
// ================================================================
// The old implementation started TopMonitor.ps1 and FPS-Monitor.ps1 as two
// independent PowerShell processes.  Keep their small JSON/file contract for
// the existing frontend, but read HWiNFO once and publish both views from one
// native worker thread.
static const std::wstring MONITOR_DIR = POWER_CONTROL_DIR;
static const std::wstring MONITOR_TOP_JSON = MONITOR_DIR + L"\\topmon.json";
static const std::wstring MONITOR_FPS_JSON = MONITOR_DIR + L"\\fps-status.json";
static const std::wstring MONITOR_FPS_HB = MONITOR_DIR + L"\\fps-monitor.hb";
static const std::wstring MONITOR_HWINFO_OK = MONITOR_DIR + L"\\hwinfo-ok";
static const std::wstring MONITOR_TOP_STOP = MONITOR_DIR + L"\\topmon.stop";
static const std::wstring MONITOR_FPS_STOP = MONITOR_DIR + L"\\fps-monitor.stop";
static const std::wstring TDP_DAEMON_PID = MONITOR_DIR + L"\\tdpctl-daemon.pid";
static const std::wstring TDP_DAEMON_HB = MONITOR_DIR + L"\\tdpctl-daemon.hb";
static const std::wstring TDP_RESPONSE = MONITOR_DIR + L"\\tdpctl-resp.json";
static const std::wstring TDP_COMMAND = MONITOR_DIR + L"\\tdpctl-cmd.txt";
static const std::wstring FLOAT_ACTIVE_MARKER = MONITOR_DIR + L"\\float-active";
static const std::wstring LEGACY_TOPMON_PID = MONITOR_DIR + L"\\topmon.pid";
static const std::wstring LEGACY_FPS_PID = MONITOR_DIR + L"\\fps-monitor.pid";

static std::vector<std::wstring> sgExcludes();

// Process names are normalized to lowercase base names.  Keep the matcher
// small and local so exclude.txt can use safe patterns such as qemu-system-*.
static bool sgNamePatternMatches(const std::wstring& value, const std::wstring& pattern) {
    size_t valueIndex = 0, patternIndex = 0, starIndex = std::wstring::npos, starValue = 0;
    while (valueIndex < value.size()) {
        if (patternIndex < pattern.size() &&
            (pattern[patternIndex] == L'?' || pattern[patternIndex] == value[valueIndex])) {
            ++valueIndex;
            ++patternIndex;
        } else if (patternIndex < pattern.size() && pattern[patternIndex] == L'*') {
            starIndex = patternIndex++;
            starValue = valueIndex;
        } else if (starIndex != std::wstring::npos) {
            patternIndex = starIndex + 1;
            valueIndex = ++starValue;
        } else {
            return false;
        }
    }
    while (patternIndex < pattern.size() && pattern[patternIndex] == L'*') ++patternIndex;
    return patternIndex == pattern.size();
}

static bool sgNameExcludedBy(const std::wstring& name, const std::vector<std::wstring>& patterns) {
    return std::any_of(patterns.begin(), patterns.end(), [&](const std::wstring& pattern) {
        return sgNamePatternMatches(name, pattern);
    });
}

struct NativeMonitorHw {
    double fps = 0.0;
    double fps1 = 0.0;
    double gpuLoad = 0.0;
    double packagePower = 0.0;
    double freqMhz = 0.0;
    double tempC = 0.0;
    double chargeW = 0.0;
    double remainMin = -1.0;
    double gpuPowerW = 0.0;
    double gpuClockMhz = 0.0;
    bool sharedOk = false;
    bool fpsSensor = false;
};

static std::string monitorField(const BYTE* p, size_t n) {
    size_t len = 0;
    while (len < n && p[len] != 0) ++len;
    return std::string(reinterpret_cast<const char*>(p), len);
}

static bool monitorLaunchHWiNFORecovery() {
    const std::wstring bat = MONITOR_DIR + L"\\YeManHWiNFO.bat";
    if (!file_exists(bat)) return false;

    wchar_t comspec[MAX_PATH]{};
    DWORD length = GetEnvironmentVariableW(L"ComSpec", comspec, _countof(comspec));
    const std::wstring cmdExe = length > 0 && length < _countof(comspec)
        ? std::wstring(comspec, length)
        : L"C:\\Windows\\System32\\cmd.exe";

    // 需要双层引号：第一层给 CreateProcess，第二层给 cmd /c 的 bat 路径。
    const std::wstring command = L"\"" + cmdExe + L"\" /d /c \"\"" + bat + L"\" restart\"";
    std::vector<wchar_t> commandLine(command.begin(), command.end());
    commandLine.push_back(L'\0');
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

static bool monitorReadHWiNFO(NativeMonitorHw& out) {
    static const wchar_t* names[] = {
        L"Global\\HWiNFO_SENS_SM2", L"HWiNFO_SENS_SM2",
        L"Global\\HWiNFO_SENS_SM", L"HWiNFO_SENS_SM"
    };
    HANDLE mapping = nullptr;
    for (const auto* name : names) {
        mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name);
        if (mapping) break;
    }
    if (!mapping) return false;

    BYTE* view = static_cast<BYTE*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    if (!view) {
        CloseHandle(mapping);
        return false;
    }
    MEMORY_BASIC_INFORMATION mbi{};
    const SIZE_T mappedSize = VirtualQuery(view, &mbi, sizeof(mbi)) ? mbi.RegionSize : 0;
    auto readU32 = [&](size_t off, uint32_t& value) -> bool {
        if (off + sizeof(value) > mappedSize) return false;
        memcpy(&value, view + off, sizeof(value));
        return true;
    };
    auto readDouble = [&](size_t off, double& value) -> bool {
        if (off + sizeof(value) > mappedSize) return false;
        memcpy(&value, view + off, sizeof(value));
        return std::isfinite(value);
    };

    bool ok = false;
    do {
        uint32_t signature = 0, readingOffset = 0, readingSize = 0, readingCount = 0;
        if (!readU32(0, signature) || signature != 0x53695748u ||
            !readU32(32, readingOffset) || !readU32(36, readingSize) ||
            !readU32(40, readingCount) || readingSize < 316 || readingCount == 0 || readingCount > 100000)
            break;
        const uint64_t end = static_cast<uint64_t>(readingOffset) +
            static_cast<uint64_t>(readingSize) * static_cast<uint64_t>(readingCount);
        if (readingOffset >= mappedSize || end > mappedSize) break;

        double packageTemp = 0.0;
        double tctlTemp = 0.0;
        const auto finitePositive = [](double v) { return std::isfinite(v) && v > 0.0; };
        const auto finiteNonNegative = [](double v) { return std::isfinite(v) && v >= 0.0; };
        for (uint32_t i = 0; i < readingCount; ++i) {
            const size_t base = static_cast<size_t>(readingOffset) +
                static_cast<size_t>(i) * static_cast<size_t>(readingSize);
            const std::string label = monitorField(view + base + 12, 128);
            const std::string unit = monitorField(view + base + 268, 16);
            const std::string low = ascii_lower(label);
            const std::string unitLow = ascii_lower(unit);
            double value = 0.0, average = 0.0;
            if (!readDouble(base + 284, value)) value = 0.0;
            if (!readDouble(base + 308, average)) average = 0.0;

            if (low.find("framerate") != std::string::npos) {
                if (low.find("presented") != std::string::npos && low.find("(avg)") != std::string::npos) {
                    out.fpsSensor = true;
                    out.fps = value > 0.0 ? value : average;
                } else if ((low.find("presented") != std::string::npos && low.find("(1%)") != std::string::npos) ||
                           low.find("1% low") != std::string::npos) {
                    out.fpsSensor = true;
                    out.fps1 = value > 0.0 ? value : average;
                }
                continue;
            }

            const bool cpuPackagePower = low.find("power") != std::string::npos &&
                low.find("cpu") != std::string::npos &&
                (low.find("package") != std::string::npos || low.find("pkg") != std::string::npos) &&
                low.find("core") == std::string::npos && low.find("ccd") == std::string::npos &&
                low.find("soc") == std::string::npos && low.find("gpu") == std::string::npos;
            if (cpuPackagePower && (unitLow == "w" || unitLow.find("watt") != std::string::npos)) {
                if (finitePositive(value)) out.packagePower = value;
                continue;
            }

            const bool cpuClock = (low.find("core") != std::string::npos && low.find("clock") != std::string::npos) ||
                (low.find("cpu") != std::string::npos && low.find("clock") != std::string::npos);
            const bool badClock = low.find("effective") != std::string::npos || low.find("bus") != std::string::npos ||
                low.find("memory") != std::string::npos || low.find("gpu") != std::string::npos ||
                low.find("video") != std::string::npos || low.find("crossbar") != std::string::npos ||
                low.find("vcn") != std::string::npos || low.find("soc") != std::string::npos ||
                low.find("ref") != std::string::npos || low.find("ratio") != std::string::npos ||
                low.find("boost") != std::string::npos || low.find("uncore") != std::string::npos ||
                low.find("mesh") != std::string::npos || low.find("dram") != std::string::npos ||
                low.find("display") != std::string::npos || low.find("encoder") != std::string::npos;
            if (cpuClock && !badClock && unitLow.find("mhz") != std::string::npos && finitePositive(value)) {
                out.freqMhz = (std::max)(out.freqMhz, value);
                continue;
            }

            const bool tctl = low.find("cpu") != std::string::npos &&
                (low.find("tctl") != std::string::npos || low.find("tdie") != std::string::npos);
            const bool cpuPackageTemp = low == "cpu package";
            if ((tctl || cpuPackageTemp) && low.find("power") == std::string::npos &&
                low.find("voltage") == std::string::npos && low.find("rpm") == std::string::npos &&
                low.find("current") == std::string::npos && finitePositive(value)) {
                if (tctl) tctlTemp = (std::max)(tctlTemp, value);
                else packageTemp = (std::max)(packageTemp, value);
                continue;
            }

            if ((low.find("estimated") != std::string::npos && low.find("remaining") != std::string::npos && low.find("time") != std::string::npos) ||
                (low.find("remaining") != std::string::npos && low.find("time") != std::string::npos)) {
                if (finitePositive(value)) {
                    if (unitLow == "h" || unitLow.find("hour") != std::string::npos) out.remainMin = value * 60.0;
                    else if (unitLow == "min" || unitLow.find("minute") != std::string::npos) out.remainMin = value;
                    else if (unitLow == "s" || unitLow.find("second") != std::string::npos) out.remainMin = value / 60.0;
                }
                continue;
            }

            const bool charge = (low.find("charge rate") != std::string::npos || low.find("battery power") != std::string::npos ||
                (low.find("battery") != std::string::npos && low.find("charge") != std::string::npos)) &&
                low.find("level") == std::string::npos && low.find("capacity") == std::string::npos &&
                low.find("remaining") == std::string::npos && low.find("time") == std::string::npos;
            if (charge) {
                if (unitLow == "mw") out.chargeW = value / 1000.0;
                else if (unitLow == "w" || unitLow.find("watt") != std::string::npos) out.chargeW = value;
                continue;
            }

            const bool gpuPower = low.find("gpu power") != std::string::npos &&
                low.find("limit") == std::string::npos && low.find("rated") == std::string::npos &&
                low.find("maximum") == std::string::npos && low.find("capability") == std::string::npos &&
                low.find("tgp") == std::string::npos && low.find("tbp") == std::string::npos;
            if (gpuPower && (unitLow == "w" || unitLow.find("watt") != std::string::npos) && finiteNonNegative(value) && value < 1000.0) {
                out.gpuPowerW = (std::max)(out.gpuPowerW, value);
                continue;
            }

            const bool gpuClock = (low.find("gpu") != std::string::npos && low.find("clock") != std::string::npos) ||
                (low.find("clock") != std::string::npos && low.find("gpu") != std::string::npos);
            const bool badGpuClock = low.find("soc") != std::string::npos || low.find("memory") != std::string::npos ||
                low.find("video") != std::string::npos || low.find("display") != std::string::npos ||
                low.find("encoder") != std::string::npos || low.find("decoder") != std::string::npos ||
                low.find("bus") != std::string::npos || low.find("core ") != std::string::npos;
            if (gpuClock && !badGpuClock && unitLow.find("mhz") != std::string::npos && finitePositive(value)) {
                out.gpuClockMhz = (std::max)(out.gpuClockMhz, value);
                continue;
            }

            if (low.find("gpu") != std::string::npos && low.find("video") == std::string::npos &&
                low.find("compute") == std::string::npos && low.find("memory controller") == std::string::npos &&
                low.find("bus load") == std::string::npos && low.find("busy") == std::string::npos &&
                low.find("memory usage") == std::string::npos && low.find("fan") == std::string::npos &&
                unitLow == "%" && (low.find("d3d usage") != std::string::npos ||
                low.find("core load") != std::string::npos || low.find("utilization") != std::string::npos) &&
                finiteNonNegative(value)) {
                out.gpuLoad = (std::max)(out.gpuLoad, value);
            }
        }
        out.tempC = tctlTemp > 0.0 ? tctlTemp : packageTemp;
        // 共享内存健康的定义是：签名、header、reading 区间和至少一个元素均可读。
        // 不能要求功耗/频率/温度必须非零，否则不同机型的传感器命名差异会被误报成故障。
        out.sharedOk = true;
        ok = true;
    } while (false);

    UnmapViewOfFile(view);
    CloseHandle(mapping);
    return ok;
}

static double nativeCpuUsagePct() {
    static ULONGLONG prevIdle = 0, prevKernel = 0, prevUser = 0;
    static bool havePrev = false;
    FILETIME idleFt{}, kernelFt{}, userFt{};
    if (!GetSystemTimes(&idleFt, &kernelFt, &userFt)) return 0.0;
    auto ticks = [](const FILETIME& ft) {
        ULARGE_INTEGER v{}; v.LowPart = ft.dwLowDateTime; v.HighPart = ft.dwHighDateTime;
        return static_cast<ULONGLONG>(v.QuadPart);
    };
    const ULONGLONG idle = ticks(idleFt), kernel = ticks(kernelFt), user = ticks(userFt);
    if (!havePrev) { havePrev = true; prevIdle = idle; prevKernel = kernel; prevUser = user; return 0.0; }
    const ULONGLONG idleDelta = idle - prevIdle;
    const ULONGLONG totalDelta = (kernel - prevKernel) + (user - prevUser);
    prevIdle = idle; prevKernel = kernel; prevUser = user;
    if (!totalDelta || totalDelta < idleDelta) return 0.0;
    return (std::min)(100.0, (std::max)(0.0, 100.0 * static_cast<double>(totalDelta - idleDelta) / static_cast<double>(totalDelta)));
}

static bool nativeBatteryStatus(bool& ac, bool& hasBattery, double& remainMin, int& batteryPercent) {
    SYSTEM_POWER_STATUS sps{};
    if (!GetSystemPowerStatus(&sps)) return false;
    ac = sps.ACLineStatus == 1;
    hasBattery = sps.BatteryFlag != 128 && sps.BatteryLifePercent != 255;
    batteryPercent = hasBattery && sps.BatteryLifePercent != 255
        ? static_cast<int>(sps.BatteryLifePercent) : -1;
    remainMin = (sps.BatteryLifeTime != 0xFFFFFFFFu && sps.BatteryLifeTime > 0)
        ? static_cast<double>(sps.BatteryLifeTime) / 60.0 : -1.0;
    return true;
}

static bool nativeMonitorExcluded(const std::wstring& name) {
    static const wchar_t* blacklist[] = {
        L"system", L"idle", L"csrss", L"winlogon", L"lsass", L"services", L"smss",
        L"dwm", L"explorer", L"msedge", L"chrome", L"firefox", L"brave", L"opera",
        L"msedgewebview2", L"searchhost", L"fontdrvhost", L"sihost",
        L"taskhostw", L"audiodg", L"nvcontainer", L"nvdisplay", L"rundll32", L"conhost",
        L"systemsettings", L"shellhost", L"startmenuexperiencehost", L"runtimebroker",
        L"applicationframehost", L"peopleexperiencehost", L"lockapp", L"svchost", L"powershell",
        L"pwsh", L"yemancc", L"yemantdpctl", L"rtss", L"rtsshooksloader*", L"hwinfo64", L"gameviewer",
        L"losslessscaling", L"magpie", L"gameoverlayui", L"gamebar", L"gamebarftserver",
        L"gamebarpresencewriter", L"xboxgamebarwidgets", L"openspeedy",
        L"uuremote", L"uuremotefe", L"uur", L"neteaseuu", L"sunloginclient",
        L"teamviewer", L"anydesk", L"todesk", L"steam", L"steamwebhelper", L"qq",
        L"chatgpt", L"discord", L"slack", L"teams", L"rtkauduservice64",
        L"nvdisplaycontainer",
        L"vmware-vmx", L"virtualboxvm", L"qemu-system-*",
        L"vmmem", L"vmmemwsl", L"wslhost", L"vmcompute"
    };
    for (const auto* item : blacklist) {
        if (sgNamePatternMatches(name, item)) return true;
    }
    return false;
}

struct NativeMonitorGame {
    DWORD pid = 0;
    std::wstring name;
    ULONGLONG lastEnum = 0;
    ULONGLONG lastValidate = 0;
};

static bool nativeProcessAlive(DWORD pid) {
    if (!pid) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}

static NativeMonitorGame nativeFindGame(ULONGLONG now) {
    NativeMonitorGame best;
    const auto userExcludes = sgExcludes();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return best;
    PROCESSENTRY32W pe{sizeof(pe)};
    if (Process32FirstW(snap, &pe)) {
        do {
            const std::wstring name = sgBaseName(pe.szExeFile);
            if (nativeMonitorExcluded(name) || sgNameExcludedBy(name, userExcludes)) continue;
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
            if (!h) continue;
            PROCESS_MEMORY_COUNTERS_EX mc{};
            mc.cb = sizeof(mc);
            SIZE_T ws = 0;
            if (GetProcessMemoryInfo(h, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&mc), sizeof(mc))) ws = mc.WorkingSetSize;
            CloseHandle(h);
            if (ws < static_cast<SIZE_T>(SG_MIN_WS)) continue;
            if (ws > best.lastEnum) {
                best.pid = pe.th32ProcessID;
                best.name = name;
                best.lastEnum = static_cast<ULONGLONG>(ws);
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    best.lastEnum = now;
    best.lastValidate = now;
    return best;
}

struct NativeDetectedGame {
    DWORD pid = 0;
    std::wstring name;
    std::wstring title;
    std::wstring path;
    SIZE_T workingSet = 0;
};

struct NativeGameWindowTitleContext {
    DWORD pid = 0;
    std::wstring title;
};

static BOOL CALLBACK nativeGameWindowTitleEnum(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<NativeGameWindowTitleContext*>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!ctx || pid != ctx->pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER))
        return TRUE;

    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) return TRUE;
    std::wstring title(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(hwnd, title.data(), static_cast<int>(title.size()));
    if (copied <= 0) return TRUE;
    title.resize(static_cast<size_t>(copied));
    ctx->title = std::move(title);
    return FALSE;
}

static std::wstring nativeProcessWindowTitle(DWORD pid) {
    NativeGameWindowTitleContext ctx{pid, {}};
    EnumWindows(nativeGameWindowTitleEnum, reinterpret_cast<LPARAM>(&ctx));
    return ctx.title;
}

static NativeDetectedGame nativeDetectGame() {
    NativeDetectedGame best;
    DWORD currentSession = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSession)) return best;

    const auto userExcludes = sgExcludes();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return best;

    PROCESSENTRY32W pe{sizeof(pe)};
    if (Process32FirstW(snap, &pe)) {
        do {
            const DWORD pid = pe.th32ProcessID;
            const std::wstring name = sgBaseName(pe.szExeFile);
            if (!pid || pid == 4 || pid == GetCurrentProcessId() ||
                nativeMonitorExcluded(name) || sgNameExcludedBy(name, userExcludes)) {
                continue;
            }

            DWORD processSession = 0;
            if (!ProcessIdToSessionId(pid, &processSession) || processSession != currentSession)
                continue;

            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
            if (!process) continue;

            PROCESS_MEMORY_COUNTERS_EX memory{};
            memory.cb = sizeof(memory);
            SIZE_T workingSet = 0;
            std::wstring path;
            if (GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
                workingSet = memory.WorkingSetSize;
            std::vector<wchar_t> image(32768);
            DWORD imageLength = static_cast<DWORD>(image.size());
            if (QueryFullProcessImageNameW(process, 0, image.data(), &imageLength))
                path.assign(image.data(), imageLength);
            CloseHandle(process);

            if (workingSet < static_cast<SIZE_T>(SG_MIN_WS) || workingSet <= best.workingSet)
                continue;
            best.pid = pid;
            best.name = name;
            best.path = std::move(path);
            best.workingSet = workingSet;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    if (best.pid) best.title = nativeProcessWindowTitle(best.pid);
    return best;
}

static std::mutex g_nativeMonitorMx;
static std::condition_variable g_nativeMonitorCv;
static std::thread g_nativeMonitorThread;
static bool g_nativeMonitorTop = false;
static bool g_nativeMonitorFps = false;
static bool g_nativeMonitorStop = false;
static bool g_nativeHWiNFORecoveryArmed = true;

static void nativeMonitorDeleteOutputs() {
    DeleteFileW(MONITOR_TOP_JSON.c_str());
    DeleteFileW(MONITOR_FPS_JSON.c_str());
    DeleteFileW(MONITOR_FPS_HB.c_str());
    DeleteFileW(MONITOR_HWINFO_OK.c_str());
    DeleteFileW(MONITOR_TOP_STOP.c_str());
    DeleteFileW(MONITOR_FPS_STOP.c_str());
    DeleteFileW((MONITOR_TOP_JSON + L".tmp").c_str());
    DeleteFileW((MONITOR_FPS_JSON + L".tmp").c_str());
    DeleteFileW((MONITOR_FPS_HB + L".tmp").c_str());
    DeleteFileW((MONITOR_HWINFO_OK + L".tmp").c_str());
    DeleteFileW((MONITOR_DIR + L"\\fps-monitor.log.tmp").c_str());
}

static void nativeMonitorLoop() {
    NativeMonitorGame game;
    nativeMonitorDeleteOutputs();
    DeleteFileW(MONITOR_TOP_STOP.c_str());
    DeleteFileW(MONITOR_FPS_STOP.c_str());
    for (;;) {
        bool top = false, fps = false;
        {
            std::lock_guard<std::mutex> lock(g_nativeMonitorMx);
            if (g_nativeMonitorStop) break;
            top = g_nativeMonitorTop;
            fps = g_nativeMonitorFps;
        }
        if (!top && !fps) {
            // Both consumers stopped: remove the last published snapshot before
            // waiting, so the frontend cannot observe stale monitor data.
            nativeMonitorDeleteOutputs();
            std::unique_lock<std::mutex> lock(g_nativeMonitorMx);
            g_nativeMonitorCv.wait_for(lock, std::chrono::milliseconds(500), [] {
                return g_nativeMonitorStop || g_nativeMonitorTop || g_nativeMonitorFps;
            });
            continue;
        }

        NativeMonitorHw hw;
        const bool hwReadable = monitorReadHWiNFO(hw);
        const bool hwHealthy = hwReadable && hw.sharedOk;
        if (hwHealthy) {
            // 一次故障恢复成功后重新布防，允许未来新的独立故障再复位一次。
            g_nativeHWiNFORecoveryArmed = true;
        } else if (g_nativeHWiNFORecoveryArmed) {
            // 启动期或运行中首次数据读取失败（包括共享内存 12 小时失效、EXE 消失）
            // 一律直接复位一次；持续失败期间不循环复位，不额外枚举进程。
            g_nativeHWiNFORecoveryArmed = false;
            monitorLaunchHWiNFORecovery();
        }
        const double nowEpochMs = sgNowEpoch() * 1000.0;
        bool ac = true, hasBattery = false;
        int batteryPercent = -1;
        double winRemain = -1.0;
        nativeBatteryStatus(ac, hasBattery, winRemain, batteryPercent);
        const double remain = hw.remainMin >= 0.0 ? hw.remainMin : winRemain;
        const double cpuUsage = nativeCpuUsagePct();

        if (hwHealthy) {
            sgWriteFileAtomic(MONITOR_HWINFO_OK, std::to_string(static_cast<long long>(nowEpochMs)));
        } else {
            DeleteFileW(MONITOR_HWINFO_OK.c_str());
        }

        if (top) {
            json topJson = {
                {"ts", static_cast<long long>(nowEpochMs)},
                {"tdpW", std::round(hw.packagePower * 10.0) / 10.0},
                {"freqMhz", static_cast<int>(std::round(hw.freqMhz))},
                {"tempC", static_cast<int>(std::round(hw.tempC))},
                {"ac", ac ? 1 : 0},
                {"hasBattery", hasBattery},
                {"batteryPercent", batteryPercent},
                {"chargeW", std::round(hw.chargeW * 10.0) / 10.0},
                {"remainMin", remain >= 0.0 ? static_cast<int>(std::round(remain)) : -1},
                {"cpuUsage", static_cast<int>(std::round(cpuUsage))},
                {"gpuPowerW", std::round(hw.gpuPowerW * 10.0) / 10.0},
                {"gpuClockMhz", static_cast<int>(std::round(hw.gpuClockMhz))},
                {"hwDown", !hwHealthy}
            };
            sgWriteFileAtomic(MONITOR_TOP_JSON, topJson.dump());
        } else {
            DeleteFileW(MONITOR_TOP_JSON.c_str());
        }

        if (fps) {
            sgWriteFileAtomic(MONITOR_FPS_HB, "{\"ts\":" + std::to_string(static_cast<long long>(nowEpochMs)) + "}");
            const ULONGLONG now = GetTickCount64();
            if (hw.fpsSensor && hw.fps > 0.0) {
                if (!game.pid || now - game.lastValidate >= 3000ULL) {
                    if (game.pid && nativeProcessAlive(game.pid)) {
                        game.lastValidate = now;
                    } else {
                        game = nativeFindGame(now);
                    }
                }
                if (game.pid) {
                    json fpsJson = {
                        {"ts", static_cast<long long>(nowEpochMs)},
                        {"fps", std::round(hw.fps * 10.0) / 10.0},
                        {"fps1", std::round(hw.fps1 * 10.0) / 10.0},
                        {"gpu", static_cast<int>(std::round(hw.gpuLoad))},
                        {"packagePower", std::round(hw.packagePower * 10.0) / 10.0},
                        {"game", W2U(game.name)},
                        {"pid", static_cast<int>(game.pid)}
                    };
                    sgWriteFileAtomic(MONITOR_FPS_JSON, fpsJson.dump());
                } else {
                    DeleteFileW(MONITOR_FPS_JSON.c_str());
                }
            } else {
                DeleteFileW(MONITOR_FPS_JSON.c_str());
            }
        } else {
            DeleteFileW(MONITOR_FPS_JSON.c_str());
            DeleteFileW(MONITOR_FPS_HB.c_str());
            game = {};
        }

        // 顶部监控保持 2 秒；仅 FPS 浮动且没有游戏时降低到 5 秒，
        // 游戏出现后恢复 1 秒，减少空闲状态下的 HWiNFO/进程检测压力。
        const auto cadence = top
            ? std::chrono::milliseconds(2000)
            : std::chrono::milliseconds(game.pid ? 1000 : 5000);
        std::unique_lock<std::mutex> lock(g_nativeMonitorMx);
        g_nativeMonitorCv.wait_for(lock, cadence, [top, fps] {
            return g_nativeMonitorStop || g_nativeMonitorTop != top || g_nativeMonitorFps != fps;
        });
        if (g_nativeMonitorStop) break;
    }
    nativeMonitorDeleteOutputs();
}

static void nativeMonitorSetMode(bool top, bool fps, bool enabled) {
    std::lock_guard<std::mutex> lock(g_nativeMonitorMx);
    if (enabled) {
        if (top) g_nativeMonitorTop = true;
        if (fps) g_nativeMonitorFps = true;
        if (!g_nativeMonitorThread.joinable()) {
            g_nativeMonitorStop = false;
            g_nativeMonitorThread = std::thread(nativeMonitorLoop);
        }
    } else {
        if (top) g_nativeMonitorTop = false;
        if (fps) g_nativeMonitorFps = false;
    }
    g_nativeMonitorCv.notify_all();
}

static void stopNativeMonitorForExit() {
    {
        std::lock_guard<std::mutex> lock(g_nativeMonitorMx);
        g_nativeMonitorTop = false;
        g_nativeMonitorFps = false;
        g_nativeMonitorStop = true;
        g_nativeMonitorCv.notify_all();
    }
    if (g_nativeMonitorThread.joinable()) g_nativeMonitorThread.join();
    nativeMonitorDeleteOutputs();
}

static constexpr wchar_t kTdpDaemonPipeName[] = L"\\\\.\\pipe\\YeManTdpCtl.v1";
static const std::wstring kTdpDaemonExe = POWER_CONTROL_DIR + L"\\pawnio\\YeManTdpCtl.exe";
static HANDLE g_tdpDaemonJob = nullptr;
static bool g_tdpDaemonStopSent = false;
static std::atomic<unsigned long long> g_tdpRequestSeq{1};

static std::wstring finalPathForFile(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::vector<wchar_t> buf(32768);
    DWORD n = GetFinalPathNameByHandleW(h, buf.data(), (DWORD)buf.size(), FILE_NAME_NORMALIZED);
    CloseHandle(h);
    if (!n || n >= buf.size()) return {};
    std::wstring out(buf.data(), n);
    if (out.rfind(L"\\\\?\\", 0) == 0) out.erase(0, 4);
    return out;
}

static bool sameFinalPath(const std::wstring& a, const std::wstring& b) {
    auto fa = finalPathForFile(a), fb = finalPathForFile(b);
    return !fa.empty() && !fb.empty() && _wcsicmp(fa.c_str(), fb.c_str()) == 0;
}

static std::wstring processImagePath(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    std::vector<wchar_t> buf(32768);
    DWORD n = (DWORD)buf.size();
    bool ok = QueryFullProcessImageNameW(h, 0, buf.data(), &n) != FALSE;
    CloseHandle(h);
    return ok ? std::wstring(buf.data(), n) : std::wstring{};
}

static bool tdpVerifyPipeServer(HANDLE pipe, DWORD* serverPidOut = nullptr) {
    DWORD pid = 0;
    if (!GetNamedPipeServerProcessId(pipe, &pid) || !pid) return false;
    DWORD sidServer = 0, sidSelf = 0;
    if (!ProcessIdToSessionId(pid, &sidServer) ||
        !ProcessIdToSessionId(GetCurrentProcessId(), &sidSelf) || sidServer != sidSelf)
        return false;
    auto image = processImagePath(pid);
    bool ok = !image.empty() && sameFinalPath(image, kTdpDaemonExe);
    if (ok && serverPidOut) *serverPidOut = pid;
    return ok;
}

static json tdpDaemonPipeRequest(const json& request, DWORD timeoutMs = 3000) {
    const std::string op = request.value("op", std::string{});
    if (op == "set" && !hardwareWriteAllowed())
        throw std::runtime_error("hardware writes are blocked during power transition");
    if (!WaitNamedPipeW(kTdpDaemonPipeName, timeoutMs))
        throw std::runtime_error("TDP daemon pipe unavailable");
    HANDLE pipe = CreateFileW(kTdpDaemonPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe == INVALID_HANDLE_VALUE)
        throw std::runtime_error("TDP daemon pipe connect failed");
    try {
        if (!tdpVerifyPipeServer(pipe))
            throw std::runtime_error("TDP daemon identity verification failed");
        DWORD mode = PIPE_READMODE_MESSAGE;
        if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr))
            throw std::runtime_error("TDP daemon pipe mode failed");
        std::string payload = request.dump();
        if (payload.empty() || payload.size() > 4096)
            throw std::runtime_error("TDP daemon request too large");
        auto ioWithTimeout = [&](bool write, void* data, DWORD size, DWORD& transferred) -> bool {
            OVERLAPPED ov{};
            ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!ov.hEvent) return false;
            BOOL started = write
                ? WriteFile(pipe, data, size, nullptr, &ov)
                : ReadFile(pipe, data, size, nullptr, &ov);
            DWORD le = started ? ERROR_SUCCESS : GetLastError();
            bool ok = false;
            if (started || le == ERROR_IO_PENDING) {
                DWORD wr = WaitForSingleObject(ov.hEvent, timeoutMs);
                if (wr == WAIT_OBJECT_0)
                    ok = GetOverlappedResult(pipe, &ov, &transferred, FALSE) != FALSE;
                else
                    CancelIoEx(pipe, &ov);
            }
            CloseHandle(ov.hEvent);
            return ok;
        };
        DWORD wrote = 0;
        if (op == "set" && !hardwareWriteAllowed())
            throw std::runtime_error("hardware writes were blocked before daemon dispatch");
        if (!ioWithTimeout(true, payload.data(), (DWORD)payload.size(), wrote) || wrote != payload.size())
            throw std::runtime_error("TDP daemon request write failed or timed out");
        char buf[4096] = {};
        DWORD got = 0;
        if (!ioWithTimeout(false, buf, sizeof(buf), got) || got == 0 || got > sizeof(buf))
            throw std::runtime_error("TDP daemon response read failed or timed out");
        json resp = json::parse(std::string(buf, buf + got));
        if (resp.value("version", 0) != 1 ||
            resp.value("requestId", std::string{}) != request.value("requestId", std::string{}))
            throw std::runtime_error("TDP daemon response correlation failed");
        CloseHandle(pipe);
        return resp;
    } catch (...) {
        CloseHandle(pipe);
        throw;
    }
}

static std::string nextTdpRequestId() {
    return std::to_string(GetCurrentProcessId()) + "-" +
           std::to_string(g_tdpRequestSeq.fetch_add(1, std::memory_order_relaxed));
}

// ================================================================
// OpenSpeedy native client
// ================================================================
// The native persistent OpenSpeedy client was disabled after it caused
// process freezes on some games. The verified renderer-side transaction
// remains the only game-speed path.
#if 0
// Keep one pipe per architecture and one authoritative target lifecycle in
// the native host. The renderer must never start PowerShell or send INJECT.
static constexpr wchar_t kSpeedBridgePipe32[] = L"\\\\.\\pipe\\OpenSpeedyBridge32";
static constexpr wchar_t kSpeedBridgePipe64[] = L"\\\\.\\pipe\\OpenSpeedyBridge64";

struct SpeedHackPipe {
    HANDLE handle = nullptr;
    // Only set for a bridge process launched by this YeManCC instance.  An
    // already-running OpenSpeedy bridge is never terminated by us.
    HANDLE ownedProcess = nullptr;
};
struct SpeedHackProcessIdentity {
    bool x86 = false;
    ULONGLONG creationTime = 0;
};
struct SpeedHackActive {
    bool injected = false;
    bool enabled = false;
    DWORD pid = 0;
    bool x86 = false;
    double factor = 1.0;
    ULONGLONG creationTime = 0;
};

static SpeedHackPipe g_speedBridge32;
static SpeedHackPipe g_speedBridge64;
static SpeedHackActive g_speedHackActive;
static std::mutex g_speedHackMx;
static std::thread g_speedHackPrewarmThread;
static std::atomic<bool> g_speedHackStopping{false};

static SpeedHackPipe& speedHackPipe(bool x86) { return x86 ? g_speedBridge32 : g_speedBridge64; }
static const wchar_t* speedHackPipeName(bool x86) { return x86 ? kSpeedBridgePipe32 : kSpeedBridgePipe64; }
static std::wstring speedHackBridgePath(bool x86) {
    return POWER_CONTROL_DIR + L"\\OpenSpeedy\\" + (x86 ? L"bridge32.exe" : L"bridge64.exe");
}

static void speedHackClosePipe(SpeedHackPipe& pipe) {
    if (pipe.handle) { CloseHandle(pipe.handle); pipe.handle = nullptr; }
    if (pipe.ownedProcess) {
        if (WaitForSingleObject(pipe.ownedProcess, 0) == WAIT_TIMEOUT) {
            TerminateProcess(pipe.ownedProcess, 0);
            WaitForSingleObject(pipe.ownedProcess, 1000);
        }
        CloseHandle(pipe.ownedProcess);
        pipe.ownedProcess = nullptr;
    }
}

static bool speedHackResponseOk(const std::string& response) {
    return response == "OK" || response.rfind("OK ", 0) == 0;
}

static HANDLE speedHackOpenPipe(const wchar_t* name, DWORD timeoutMs) {
    if (!WaitNamedPipeW(name, timeoutMs)) return nullptr;
    HANDLE handle = CreateFileW(name, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return nullptr;
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(handle, &mode, nullptr, nullptr)) {
        CloseHandle(handle); return nullptr;
    }
    return handle;
}

static bool speedHackLaunchBridge(bool x86, HANDLE* ownedProcessOut = nullptr) {
    if (ownedProcessOut) *ownedProcessOut = nullptr;
    const auto bridge = speedHackBridgePath(x86);
    if (!file_exists(bridge)) return false;
    std::wstring command = quote_windows_arg(bridge);
    std::vector<wchar_t> commandLine(command.begin(), command.end());
    commandLine.push_back(L'\0');
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    const auto workingDir = fspath::path(bridge).parent_path().wstring();
    const BOOL created = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr,
        FALSE, CREATE_NO_WINDOW, nullptr, workingDir.c_str(), &startup, &process);
    if (!created) return false;
    CloseHandle(process.hThread);
    if (ownedProcessOut) *ownedProcessOut = process.hProcess;
    else CloseHandle(process.hProcess);
    return true;
}

static bool speedHackPipeBelongsToUs(HANDLE pipe, bool x86) {
    DWORD serverPid = 0;
    if (!GetNamedPipeServerProcessId(pipe, &serverPid) || !serverPid) return true;
    const auto image = processImagePath(serverPid);
    return image.empty() || sameFinalPath(image, speedHackBridgePath(x86));
}

static bool speedHackEnsurePipe(bool x86) {
    auto& cached = speedHackPipe(x86);
    if (cached.handle) return true;
    const auto name = speedHackPipeName(x86);
    HANDLE handle = speedHackOpenPipe(name, 80);
    HANDLE ownedProcess = nullptr;
    if (!handle) {
        if (!speedHackLaunchBridge(x86, &ownedProcess)) return false;
        const ULONGLONG deadline = GetTickCount64() + 3000;
        while (GetTickCount64() < deadline) {
            handle = speedHackOpenPipe(name, 120);
            if (handle) break;
            Sleep(30);
        }
    }
    if (!handle) {
        if (ownedProcess) {
            TerminateProcess(ownedProcess, 0);
            WaitForSingleObject(ownedProcess, 1000);
            CloseHandle(ownedProcess);
        }
        return false;
    }
    if (!speedHackPipeBelongsToUs(handle, x86)) {
        CloseHandle(handle);
        if (ownedProcess) {
            TerminateProcess(ownedProcess, 0);
            WaitForSingleObject(ownedProcess, 1000);
            CloseHandle(ownedProcess);
        }
        return false;
    }
    cached.handle = handle;
    cached.ownedProcess = ownedProcess;
    return true;
}

static bool speedHackSend(SpeedHackPipe& pipe, const std::string& command, std::string& response) {
    if (!pipe.handle) return false;
    const std::string wire = command + "\n";
    DWORD written = 0;
    if (!WriteFile(pipe.handle, wire.data(), static_cast<DWORD>(wire.size()), &written, nullptr) ||
        written != wire.size()) return false;
    constexpr DWORD timeoutMs = 10000;
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    std::string received;
    char buffer[4096]{};
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe.handle, nullptr, 0, nullptr, &available, nullptr)) {
            response = "OpenSpeedy pipe peek failed"; return false;
        }
        if (!available) {
            if (GetTickCount64() >= deadline) {
                response = "OpenSpeedy bridge response timeout"; return false;
            }
            Sleep(10); continue;
        }
        DWORD read = 0;
        const BOOL ok = ReadFile(pipe.handle, buffer, sizeof(buffer), &read, nullptr);
        if (!ok && GetLastError() != ERROR_MORE_DATA) return false;
        if (read) received.append(buffer, buffer + read);
        if (ok) break;
    }
    response = trim_ascii(received);
    return !response.empty();
}

static bool speedHackCommand(bool x86, const std::string& command, std::string& response) {
    auto& pipe = speedHackPipe(x86);
    if (!speedHackEnsurePipe(x86)) { response = "OpenSpeedy bridge unavailable"; return false; }
    if (speedHackSend(pipe, command, response)) return true;
    // Never retry a command after it was written: retrying INJECT can create a
    // second hook while the first remote operation is still completing.
    speedHackClosePipe(pipe);
    if (response.empty()) response = "OpenSpeedy pipe communication failed";
    return false;
}

static void speedHackWaitForDisableSettle() { Sleep(300); }

static void speedHackPrewarmProc() {
    if (g_speedHackStopping.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(g_speedHackMx);
    for (const bool x86 : {false, true}) {
        if (g_speedHackStopping.load(std::memory_order_acquire)) break;
        std::string response;
        const bool ok = speedHackCommand(x86, "GETSPEED", response) && speedHackResponseOk(response);
        traceLog("OpenSpeedy prewarm arch=%s ok=%d response=%s", x86 ? "x86" : "x64", ok ? 1 : 0, response.c_str());
    }
}

static void speedHackStartPrewarm() {
    g_speedHackStopping.store(false, std::memory_order_release);
    if (!g_speedHackPrewarmThread.joinable()) g_speedHackPrewarmThread = std::thread(speedHackPrewarmProc);
}

static bool speedHackReadProcessIdentity(DWORD pid, SpeedHackProcessIdentity& identity) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!process) return false;
    DWORD exitCode = 0;
    if (!GetExitCodeProcess(process, &exitCode) || exitCode != STILL_ACTIVE) {
        CloseHandle(process);
        return false;
    }
    FILETIME creation{}, exitTime{}, kernelTime{}, userTime{};
    if (!GetProcessTimes(process, &creation, &exitTime, &kernelTime, &userTime)) {
        CloseHandle(process);
        return false;
    }
    BOOL wow64 = FALSE;
    const BOOL ok = IsWow64Process(process, &wow64);
    CloseHandle(process);
    if (!ok) return false;
    ULARGE_INTEGER stamp{};
    stamp.LowPart = creation.dwLowDateTime;
    stamp.HighPart = creation.dwHighDateTime;
    identity.x86 = wow64 != FALSE;
    identity.creationTime = stamp.QuadPart;
    return identity.creationTime != 0;
}

static bool speedHackActiveMatches(const SpeedHackProcessIdentity& identity, DWORD pid) {
    return g_speedHackActive.injected && g_speedHackActive.pid == pid &&
        g_speedHackActive.x86 == identity.x86 &&
        g_speedHackActive.creationTime == identity.creationTime;
}

static void speedHackAddMessage(json& messages, const std::string& command, const std::string& response) {
    if (!response.empty()) messages.push_back(command + ": " + response);
}

static json speedHackClearLocked(DWORD requestedPid) {
    if (!g_speedHackActive.injected)
        return json{{"ok", true}, {"msgs", json::array({"no active speed target"})}};
    if (requestedPid && requestedPid != g_speedHackActive.pid)
        return json{{"ok", true}, {"skipped", true}, {"safeFallback", true},
                    {"msgs", json::array({"stale speed target ignored"})}};
    const DWORD pid = g_speedHackActive.pid;
    const bool x86 = g_speedHackActive.x86;
    SpeedHackProcessIdentity identity{};
    if (!speedHackReadProcessIdentity(pid, identity) ||
        !speedHackActiveMatches(identity, pid)) {
        traceLog("OpenSpeedy clear stale target pid=%lu; local state discarded", static_cast<unsigned long>(pid));
        g_speedHackActive = {};
        return json{{"ok", true}, {"safeFallback", true},
                    {"msgs", json::array({"target exited or PID was reused; local state cleared"})}};
    }
    json messages = json::array();
    std::string response;
    bool ok = true;
    if (g_speedHackActive.enabled) {
        const bool disableOk = speedHackCommand(x86, "DISABLE " + std::to_string(pid), response) && speedHackResponseOk(response);
        speedHackAddMessage(messages, "DISABLE", response);
        ok = ok && disableOk;
    }
    // Do not retain an "injected but disabled" local state.  OpenSpeedy starts
    // a fresh enable path with INJECT -> ENABLE on the next activation, which
    // is safer than trusting a stale renderer-side module snapshot.
    if (ok) g_speedHackActive = {};
    return json{{"ok", ok}, {"safeFallback", true}, {"reason", ok ? "" : "operation_failed"}, {"msgs", messages}};
}

static json speedHackResetLocked(DWORD requestedPid) {
    if (!g_speedHackActive.injected)
        return json{{"ok", true}, {"msgs", json::array({"no active speed target"})}};
    if (requestedPid && requestedPid != g_speedHackActive.pid)
        return json{{"ok", true}, {"skipped", true}, {"safeFallback", true},
                    {"msgs", json::array({"stale speed target ignored"})}};
    SpeedHackProcessIdentity identity{};
    if (!speedHackReadProcessIdentity(g_speedHackActive.pid, identity) ||
        !speedHackActiveMatches(identity, g_speedHackActive.pid)) {
        traceLog("OpenSpeedy reset stale target pid=%lu; local state discarded", static_cast<unsigned long>(g_speedHackActive.pid));
        g_speedHackActive = {};
        return json{{"ok", true}, {"safeFallback", true},
                    {"msgs", json::array({"target exited or PID was reused; local state cleared"})}};
    }
    std::string response;
    const bool ok = speedHackCommand(g_speedHackActive.x86, "SETSPEED 1", response) && speedHackResponseOk(response);
    if (ok) g_speedHackActive.factor = 1.0;
    json messages = json::array();
    speedHackAddMessage(messages, "SETSPEED 1", response);
    return json{{"ok", ok}, {"safeFallback", !ok}, {"reason", ok ? "" : "operation_failed"}, {"msgs", messages}};
}

static json speedHackApplyLocked(DWORD pid, double factor) {
    SpeedHackProcessIdentity identity{};
    if (!speedHackReadProcessIdentity(pid, identity))
        throw std::runtime_error("target process is not running or cannot be queried");
    const bool x86 = identity.x86;
    json messages = json::array();
    std::string response;
    if (speedHackActiveMatches(identity, pid)) {
        if (std::abs(g_speedHackActive.factor - factor) < 0.000001 && (factor == 1.0 || g_speedHackActive.enabled))
            return json{{"ok", true}, {"msgs", json::array({"speed already applied"})}};
        if (factor == 1.0) return speedHackResetLocked(pid);
        if (!g_speedHackActive.enabled) {
            if (!speedHackCommand(x86, "ENABLE " + std::to_string(pid), response) || !speedHackResponseOk(response))
                return json{{"ok", false}, {"safeFallback", true}, {"reason", "operation_failed"},
                            {"msgs", json::array({"ENABLE failed: " + response})}};
            g_speedHackActive.enabled = true;
        }
        if (!speedHackCommand(x86, "SETSPEED " + std::to_string(factor), response) || !speedHackResponseOk(response)) {
            // A failed/timeout pipe operation may still be executing inside
            // the bridge. Do not immediately send rollback commands on the
            // same connection or a newly spawned replacement bridge.
            g_speedHackActive = {};
            traceLog("OpenSpeedy SETSPEED failed pid=%lu response=%s; local state reset",
                     static_cast<unsigned long>(pid), response.c_str());
            return json{{"ok", false}, {"safeFallback", true}, {"reason", "operation_failed"},
                        {"msgs", json::array({"SETSPEED failed: " + response})}};
        }
        g_speedHackActive.factor = factor;
        speedHackAddMessage(messages, "SETSPEED", response);
        return json{{"ok", true}, {"msgs", messages}};
    }
    if (g_speedHackActive.injected) {
        const auto cleared = speedHackClearLocked(0);
        if (!cleared.value("ok", false)) return cleared;
    }
    auto fail = [&](const std::string& command) -> json {
        // The bridge processes commands serially.  If a command times out or
        // its pipe breaks, sending rollback commands can race a still-running
        // remote operation.  Drop local state and require a fresh INJECT path.
        traceLog("OpenSpeedy %s failed pid=%lu response=%s; local state reset",
                 command.c_str(), static_cast<unsigned long>(pid), response.c_str());
        g_speedHackActive = {};
        return json{{"ok", false}, {"safeFallback", true}, {"reason", "operation_failed"},
                    {"msgs", json::array({command + ": " + response})}};
    };
    // Match OpenSpeedy's activation path.  INJECT is idempotent for an already
    // loaded module (Windows reuses the module), while it avoids relying on a
    // permission-sensitive Toolhelp module snapshot after a long wait.
    if (!speedHackCommand(x86, "INJECT " + std::to_string(pid), response) || !speedHackResponseOk(response)) return fail("INJECT");
    if (!speedHackCommand(x86, "ENABLE " + std::to_string(pid), response) || !speedHackResponseOk(response)) return fail("ENABLE");
    if (!speedHackCommand(x86, "SETSPEED " + std::to_string(factor), response) || !speedHackResponseOk(response)) return fail("SETSPEED");
    speedHackAddMessage(messages, "SETSPEED", response);
    g_speedHackActive = {true, true, pid, x86, factor, identity.creationTime};
    return json{{"ok", true}, {"msgs", messages}};
}

static json speedHackApply(const json& args) {
    const int pid = args.value("pid", 0);
    const double factor = args.value("factor", 0.0);
    if (pid <= 0) throw std::runtime_error("invalid speed target PID");
    if (!std::isfinite(factor) || factor <= 0.0 || factor > 16.0) throw std::runtime_error("invalid speed factor");
    std::lock_guard<std::mutex> lock(g_speedHackMx);
    return speedHackApplyLocked(static_cast<DWORD>(pid), factor);
}

static json speedHackReset(const json& args) {
    const int pid = args.value("pid", 0);
    std::lock_guard<std::mutex> lock(g_speedHackMx);
    return speedHackResetLocked(pid > 0 ? static_cast<DWORD>(pid) : 0);
}

static json speedHackClear(const json& args) {
    const int pid = args.value("pid", 0);
    std::lock_guard<std::mutex> lock(g_speedHackMx);
    return speedHackClearLocked(pid > 0 ? static_cast<DWORD>(pid) : 0);
}

static void speedHackCloseAll() {
    g_speedHackStopping.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(g_speedHackMx);
        if (g_speedHackActive.injected) {
            std::string response;
            speedHackCommand(g_speedHackActive.x86, "SETSPEED 1", response);
            if (g_speedHackActive.enabled)
                speedHackCommand(g_speedHackActive.x86, "DISABLE " + std::to_string(g_speedHackActive.pid), response);
        }
        speedHackClosePipe(g_speedBridge32);
        speedHackClosePipe(g_speedBridge64);
        g_speedHackActive = {};
    }
    if (g_speedHackPrewarmThread.joinable()) g_speedHackPrewarmThread.join();
}

#endif

static HANDLE ensureTdpDaemonJob() {
    if (g_tdpDaemonJob) return g_tdpDaemonJob;
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) return nullptr;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                 &limits, sizeof(limits))) {
        CloseHandle(job);
        return nullptr;
    }
    g_tdpDaemonJob = job;
    return g_tdpDaemonJob;
}

static void stopTdpDaemonForExit() {
    // Capture the verified server PID before sending quit.  The RPC response
    // is only an acknowledgement; the daemon may still be unwinding its
    // hardware handles after it has replied.
    DWORD daemonPid = 0;
    HANDLE daemonWait = nullptr;
    if (WaitNamedPipeW(kTdpDaemonPipeName, 200)) {
        HANDLE probe = CreateFileW(kTdpDaemonPipeName, GENERIC_READ | GENERIC_WRITE,
                                   0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (probe != INVALID_HANDLE_VALUE) {
            if (tdpVerifyPipeServer(probe, &daemonPid) && daemonPid)
                daemonWait = OpenProcess(SYNCHRONIZE, FALSE, daemonPid);
            CloseHandle(probe);
        }
    }
    if (!g_tdpDaemonStopSent) {
        g_tdpDaemonStopSent = true;
        try {
            json req = {{"version", 1}, {"requestId", nextTdpRequestId()},
                        {"op", "quit"}, {"args", json::object()}};
            tdpDaemonPipeRequest(req, 2000);
        } catch (...) {
        }
    }
    if (daemonWait) {
        // Normal path: wait for the daemon to release PawnIO and exit.
        DWORD result = WaitForSingleObject(daemonWait, 3000);
        if (result == WAIT_TIMEOUT && g_tdpDaemonJob) {
            // Final bounded fallback.  This job contains only the trusted
            // YeManTdpCtl daemon, never unrelated WebView2 or user processes.
            CloseHandle(g_tdpDaemonJob);
            g_tdpDaemonJob = nullptr;
            WaitForSingleObject(daemonWait, 2000);
        }
        CloseHandle(daemonWait);
    } else if (g_tdpDaemonJob) {
        // No verified server handle means there is nothing safe to wait for;
        // close the lifetime job so a stale daemon cannot survive the app.
        CloseHandle(g_tdpDaemonJob);
        g_tdpDaemonJob = nullptr;
    }
    cleanupExitArtifacts();
}

static DWORD WINAPI exitCleanupThreadProc(LPVOID param) {
    HWND hwnd = reinterpret_cast<HWND>(param);
    g_summonQuit = true;
    sgStopWorkThread();
    poolStop();
    for (auto& [id, w] : g_watchers) {
        if (stopWatcher(w, 1000)) delete w;
    }
    g_watchers.clear();
    stopTdpDaemonForExit();
    stopTopMonitorForExit();
    sgCleanupBeforeExit();
    if (g_autoCloseThread) {
        WaitForSingleObject(g_autoCloseThread, 3000);
        CloseHandle(g_autoCloseThread);
        g_autoCloseThread = nullptr;
    }
    PostMessageW(hwnd, WM_APP_EXIT_READY, 0, 0);
    return 0;
}

static void beginAsyncExit(HWND hwnd, WPARAM code) {
    if (!hwnd || !IsWindow(hwnd)) return;
    bool expected = false;
    if (!g_exitRequested.compare_exchange_strong(expected, true)) return;
    g_exitCode.store(code, std::memory_order_relaxed);
    // Make the user-visible exit immediate.  Do not call AnimateWindow here:
    // the fade itself is synchronous and would still block the UI thread.
    ShowWindow(hwnd, SW_HIDE);
    if (g_trayActive) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_trayActive = false;
    }
    ComPtr<ITaskbarList3> tb;
    if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&tb))) &&
        SUCCEEDED(tb->HrInit())) {
        tb->DeleteTab(hwnd);
    }
    if (!g_exitCleanupThread) {
        g_exitCleanupThread = CreateThread(nullptr, 0, exitCleanupThreadProc,
                                           reinterpret_cast<LPVOID>(hwnd), 0, nullptr);
        if (!g_exitCleanupThread) {
            // The fallback remains synchronous only if the OS refuses to
            // create the cleanup worker; correctness is more important here.
            stopTdpDaemonForExit();
            stopTopMonitorForExit();
            sgCleanupBeforeExit();
            PostMessageW(hwnd, WM_APP_EXIT_READY, g_exitCode.load(std::memory_order_relaxed), 0);
        }
    }
}

static void cleanupExitArtifacts() {
    const std::wstring exact[] = {
        FLOAT_ACTIVE_MARKER, TDP_DAEMON_PID, TDP_DAEMON_HB, TDP_RESPONSE,
        TDP_COMMAND, TDP_DAEMON_PID + L".tmp", TDP_DAEMON_HB + L".tmp",
        TDP_RESPONSE + L".tmp", TDP_COMMAND + L".tmp", LEGACY_TOPMON_PID,
        LEGACY_FPS_PID,
        MONITOR_TOP_JSON, MONITOR_FPS_JSON, MONITOR_FPS_HB, MONITOR_HWINFO_OK,
        MONITOR_TOP_STOP, MONITOR_FPS_STOP,
        MONITOR_TOP_JSON + L".tmp", MONITOR_FPS_JSON + L".tmp",
        MONITOR_FPS_HB + L".tmp", MONITOR_HWINFO_OK + L".tmp",
    };
    for (const auto& path : exact) DeleteFileW(path.c_str());

    // Atomic writers may leave uniquely suffixed command/response temp files
    // after a forced exit.  Restrict cleanup to the known TDP file prefixes.
    std::error_code ec;
    for (const auto& entry : fspath::directory_iterator(MONITOR_DIR, ec)) {
        if (ec || !entry.is_regular_file(ec)) continue;
        const auto name = ascii_lower(W2U(entry.path().filename().wstring()));
        if ((name.rfind("tdpctl-daemon.", 0) == 0 ||
             name.rfind("tdpctl-cmd.txt.", 0) == 0 ||
             name.rfind("tdpctl-resp.json.", 0) == 0 ||
             name.rfind("topmon.json.ymcc.", 0) == 0 ||
             name.rfind("fps-status.json.ymcc.", 0) == 0 ||
             name.rfind("fps-monitor.hb.ymcc.", 0) == 0 ||
             name.rfind("hwinfo-ok.ymcc.", 0) == 0) &&
            name.find(".tmp") != std::string::npos) {
            DeleteFileW(entry.path().c_str());
        }
    }
}


static std::wstring sgSelfBase() {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return sgBaseName(std::wstring(buf, n));
}
static std::string sgRegSZ(HKEY root, const wchar_t* sub, const wchar_t* val) {
    HKEY hk; if (RegOpenKeyExW(root, sub, 0, KEY_READ, &hk) != ERROR_SUCCESS) return {};
    wchar_t buf[512]; DWORD sz = sizeof(buf); DWORD type = 0;
    LONG r = RegQueryValueExW(hk, val, nullptr, &type, (BYTE*)buf, &sz);
    RegCloseKey(hk);
    if (r != ERROR_SUCCESS || type != REG_SZ) return {};
    return W2U(std::wstring(buf, sz / sizeof(wchar_t)));
}
static std::string sgDetectVendor() { // 对齐 yeman.detectVendor
    if (fspath::exists(L"C:\\SOFT\\YeMan\\PowerControl\\AMD.txt")) return "amd";
    if (fspath::exists(L"C:\\SOFT\\YeMan\\PowerControl\\intel.txt")) return "intel";
    std::string s = sgRegSZ(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"VendorIdentifier");
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    if (s.find("AUTHENTICAMD") != std::string::npos) return "amd";
    if (s.find("GENUINEINTEL") != std::string::npos) return "intel";
    return "unknown";
}

// ── 睡眠守护配置持久化（Sleep\sleepguard.json）──
static void sgMarkSleepTrigger() {
    g_sgSleepTriggerTick = GetTickCount64();
    g_sgSleepTriggerEpoch = sgNowEpoch();
    g_sgLastInputTick = g_sgSleepTriggerTick;
    g_sgWakeTick = 0;
    g_sgResleepPending = g_sgResleepEnabled;
    sgWriteFileAtomic(
        SG_SLEEP_TRIGGER_MARKER,
        "triggerEpoch=" + std::to_string(g_sgSleepTriggerEpoch) + "\n"
    );
    // 观察窗口从唤醒信号开始，不能在入睡前启动定时器；睡眠期间消息线程不会可靠运行。
}

static void sgMarkInputActivity() {
    const ULONGLONG now = GetTickCount64();
    if (g_sgResleepEnabled && g_sgSleepTriggerTick != 0)
        g_sgLastInputTick = now;
    if (g_sgOverheatSleepEnabled && g_guardEnabled)
        g_sgOverheatLastInputTick = now;
}

static void sgStopResleepObservation();

static void sgStartResleepObservation() {
    if (!g_sgResleepEnabled || g_sgSleepTriggerTick == 0 || !g_hwnd) return;
    const ULONGLONG now = GetTickCount64();
    if (now < g_sgResleepCooldownTick) {
        sgStopResleepObservation();
        return;
    }
    g_sgWakeTick = now;
    g_sgLastInputTick = now;
    g_sgResleepPending = true;
    SetTimer(g_hwnd, SG_RESLEEP_TIMER_ID, 250, nullptr);
}

static void sgStopResleepObservation() {
    if (g_hwnd) KillTimer(g_hwnd, SG_RESLEEP_TIMER_ID);
    g_sgResleepPending = false;
    g_sgSleepTriggerTick = 0;
    g_sgWakeTick = 0;
}

static void sgTryResleep() {
    if (!g_sgResleepPending) return;
    if (!g_sgResleepEnabled) {
        sgStopResleepObservation();
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (now < g_sgResleepCooldownTick ||
        now - g_sgWakeTick > 30000ULL) {
        sgStopResleepObservation();
        return;
    }
    if (now - g_sgLastInputTick < 10000ULL) return;

    const std::string log =
        "sleepTriggerEpoch=" + std::to_string(g_sgSleepTriggerEpoch) +
        "\nresleepEpoch=" + std::to_string(sgNowEpoch()) +
        "\naction=resleep\n";
    sgWriteFileAtomic(SG_RESLEEP_MARKER, log);
    g_sgResleepCooldownTick = now + 300000ULL;
    sgStopResleepObservation();
    // 仅执行一次重睡尝试；失败不循环，避免与电源/USB驱动争抢。
    SetSuspendState(FALSE, FALSE, FALSE);
}

static bool sgReadTopmonOverheat(double& tempC, bool& hasBattery) {
    const std::string c = sgReadFile(L"C:\\SOFT\\YeMan\\PowerControl\\topmon.json");
    if (c.empty()) return false;
    try {
        const json j = json::parse(c);
        const long long ts = j.value("ts", 0LL);
        const long long now = static_cast<long long>(sgNowEpoch() * 1000.0);
        if (ts <= 0 || now < ts || now - ts > 6000) return false;
        tempC = j.value("tempC", 0.0);
        hasBattery = j.value("hasBattery", false);
        return std::isfinite(tempC);
    } catch (...) {
        return false;
    }
}

static void sgResetOverheatState() {
    g_sgOverheatAboveTick = 0;
    g_sgOverheatLastInputTick = 0;
    g_sgOverheatAttempted = false;
}

static void sgTryOverheatSleep() {
    if (!g_guardEnabled || !g_sgOverheatSleepEnabled) {
        sgResetOverheatState();
        return;
    }
    double tempC = 0.0;
    bool hasBattery = false;
    if (!sgReadTopmonOverheat(tempC, hasBattery) || !hasBattery || tempC <= g_sgOverheatTempC) {
        sgResetOverheatState();
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_sgOverheatAboveTick == 0) {
        g_sgOverheatAboveTick = now;
        g_sgOverheatLastInputTick = now;
        g_sgOverheatAttempted = false;
        return;
    }
    if (g_sgOverheatAttempted || now - g_sgOverheatLastInputTick < 10000ULL) return;
    g_sgOverheatAttempted = true;
    if (!SetSuspendState(TRUE, FALSE, FALSE))
        SetSuspendState(FALSE, FALSE, FALSE);
}

static void sgUpdateOverheatTimer() {
    if (!g_hwnd) return;
    if (g_guardEnabled && g_sgOverheatSleepEnabled)
        SetTimer(g_hwnd, SG_OVERHEAT_TIMER_ID, 250, nullptr);
    else {
        KillTimer(g_hwnd, SG_OVERHEAT_TIMER_ID);
        sgResetOverheatState();
    }
}

static void sgLoadResleepCooldown() {
    g_sgResleepCooldownTick = 0;
    g_sgResleepCooldownEpoch = 0.0;
    const std::string c = sgReadFile(SG_RESLEEP_MARKER);
    const std::string key = "resleepEpoch=";
    const size_t p = c.find(key);
    if (p == std::string::npos) return;
    try {
        const double epoch = std::stod(c.substr(p + key.size()));
        const double age = sgNowEpoch() - epoch;
        if (age >= 0.0 && age < 300.0) {
            g_sgResleepCooldownEpoch = epoch;
            g_sgResleepCooldownTick = GetTickCount64() +
                static_cast<ULONGLONG>((300.0 - age) * 1000.0);
        }
    } catch (...) {
        // 损坏的冷却记录不阻断主程序；本次启动按无冷却处理。
    }
}

static void sgLoadConfig() {
    json settings = ymSettingsSection("sleep");
    std::string c = sgReadFile(SG_DIR + L"\\sleepguard.json");
    if (!settings.empty()) c = settings.dump();
    if (!c.empty()) {
        try {
            json j = json::parse(c);
            g_sgPauseResume = j.value("pauseResume", true);
            g_sgKillListEnabled = j.value("killListEnabled", false);
            g_sgResleepEnabled = j.value("resleepEnabled", false);
            g_sgOverheatSleepEnabled = j.value("overheatSleepEnabled", false);
            g_sgOverheatTempC = j.value("overheatTempC", 95);
            if (g_sgOverheatTempC < 85) g_sgOverheatTempC = 85;
            if (g_sgOverheatTempC > 100) g_sgOverheatTempC = 100;
            g_sgMode      = j.value("mode", std::string("off"));
        } catch (...) {}
    }
    if (g_sgMode != "off" && g_sgMode != "custom") g_sgMode = "off";
}
static void sgSaveConfig() {
    json j = {
        {"mode", g_sgMode},
        {"pauseResume", g_sgPauseResume},
        {"killListEnabled", g_sgKillListEnabled},
        {"resleepEnabled", g_sgResleepEnabled},
        {"overheatSleepEnabled", g_sgOverheatSleepEnabled},
        {"overheatTempC", g_sgOverheatTempC}
    };
    ymSettingsPatchSection("sleep", j);
}

// ── 按键呼出（后台手柄呼出）：持久化于 C:\SOFT\YeMan\PowerControl\summon.json ──
static std::wstring summonPath() {
    return L"C:\\SOFT\\YeMan\\PowerControl\\summon.json";
}
static void summonSave();
static void summonLoad() {
    json settings = ymSettingsSection("gamepad");
    std::string c = settings.empty() ? sgReadFile(summonPath()) : settings.dump();
    bool needsSave = false;
    if (!c.empty()) {
        try {
            json j = json::parse(c);
            auto loadBool = [&](const char* key, bool& dst, bool fallback) {
                if (!j.contains(key) || !j[key].is_boolean()) {
                    dst = fallback;
                    needsSave = true;
                    return;
                }
                dst = j[key].get<bool>();
            };
            loadBool("enabled", g_summonEnabled, true);
            loadBool("bDoubleMinimize", g_bDoubleMinimize, true);
            loadBool("tdpShortcut", g_tdpShortcut, true);
            loadBool("fpsShortcut", g_fpsShortcut, true);
            loadBool("killGame", g_killGame, false);
            loadBool("openKeyboard", g_openKeyboard, false);
            loadBool("returnDesktop", g_returnDesktop, false);
            loadBool("mouseToggle", g_mouseToggle, false);
            const std::string backend = j.value("mouseBackend", std::string("joyxoff"));
            if (backend == "joyxoff" || backend == "gamebar") g_mouseBackend = backend;
            else {
                g_mouseBackend = "joyxoff";
                needsSave = true;
            }
        } catch (...) {
            needsSave = true;
        }
    } else {
        needsSave = true;
    }
    // 首次启动、字段缺失或 JSON 损坏时只补齐当前文件，不覆盖已有合法用户配置。
    // Before WebView2 performs the one-time legacy migration, do not create a
    // partial unified file that could hide the other legacy sections.
    if (needsSave && (ymSettingsExists() || !c.empty() && !settings.empty())) summonSave();
}
static void summonSave() {
    json j = {
        {"enabled", g_summonEnabled},
        {"bDoubleMinimize", g_bDoubleMinimize},
        {"tdpShortcut", g_tdpShortcut},
        {"fpsShortcut", g_fpsShortcut},
        {"killGame", g_killGame},
        {"openKeyboard", g_openKeyboard},
        {"returnDesktop", g_returnDesktop},
        {"mouseToggle", g_mouseToggle},
        {"mouseBackend", g_mouseBackend}
    };
    std::error_code ec;
    fspath::create_directories(fspath::path(summonPath()).parent_path(), ec);
    ymSettingsPatchSection("gamepad", j);
}

// ── 掌机前端自动关闭：配置持久化于 PowerControl\autoclose.json ──
static std::wstring autoClosePath() {
    return L"C:\\SOFT\\YeMan\\PowerControl\\autoclose.json";
}
static void autoCloseLoad() {
    const auto settings = ymSettingsSection("autoclose");
    std::string c = settings.empty() ? sgReadFile(autoClosePath()) : settings.dump();
    if (c.empty()) {
        // 首启默认预填推测进程名（用户可在设置页自行编辑增删）。
        // KO助手 / NewKO 进程名未知，留给用户补充。
        std::lock_guard<std::mutex> lk(g_autoCloseMx);
        g_autoCloseProcs = { "OneXConsole", "AYASpace", "GPD WinControls" };
        return;
    }
    try {
        json j = json::parse(c);
        g_autoCloseEnabled = j.value("enabled", false);
        std::lock_guard<std::mutex> lk(g_autoCloseMx);
        g_autoCloseProcs.clear();
        if (j.contains("procs") && j["procs"].is_array())
            for (auto& p : j["procs"])
                if (p.is_string()) g_autoCloseProcs.push_back(p.get<std::string>());
    } catch (...) {}
}
static void autoCloseSave() {
    json arr = json::array();
    {
        std::lock_guard<std::mutex> lk(g_autoCloseMx);
        for (auto& p : g_autoCloseProcs) arr.push_back(p);
    }
    json j = { {"enabled", g_autoCloseEnabled.load()}, {"procs", arr} };
    std::error_code ec;
    fspath::create_directories(fspath::path(autoClosePath()).parent_path(), ec);
    ymSettingsPatchSection("autoclose", j);
}
// 进程名匹配：exe 已去 .exe 后缀，大小写不敏感；names 项可带 .exe、可用 * 前缀通配。
static bool acNameMatch(const std::wstring& exe, const std::vector<std::string>& names) {
    for (auto& n8 : names) {
        std::wstring n = U2W(n8);
        if (n.size() > 4 && _wcsicmp(n.c_str() + n.size() - 4, L".exe") == 0)
            n.resize(n.size() - 4);
        if (n.empty()) continue;
        if (n.back() == L'*') {
            std::wstring pre = n.substr(0, n.size() - 1);
            if (exe.size() >= pre.size() &&
                _wcsnicmp(exe.c_str(), pre.c_str(), pre.size()) == 0) return true;
        } else if (_wcsicmp(exe.c_str(), n.c_str()) == 0) {
            return true;
        }
    }
    return false;
}
// EnumWindows 回调：对属于目标 PID 集合的顶层窗口 PostMessage(WM_CLOSE) —— 等同点右上角 X（温和关闭）。
struct AcCloseCtx { const std::unordered_set<DWORD>* pids; DWORD selfPid; };
static BOOL CALLBACK acCloseEnum(HWND h, LPARAM lp) {
    auto* ctx = reinterpret_cast<AcCloseCtx*>(lp);
    DWORD wpid = 0; GetWindowThreadProcessId(h, &wpid);
    if (wpid && wpid != ctx->selfPid && ctx->pids->count(wpid))
        PostMessageW(h, WM_CLOSE, 0, 0);
    return TRUE;
}
// 后台轮询线程：每 5 秒，若开关开启则枚举进程匹配列表 → 对匹配进程的顶层窗口发 WM_CLOSE。
// 复用 g_summonQuit 作退出信号（与手柄呼出线程一致）。永不强杀、永不碰自身进程。
static DWORD WINAPI autoCloseThread(LPVOID) {
    const DWORD selfPid = GetCurrentProcessId();
    while (!g_summonQuit) {
        // 5 秒轮询，分片 sleep 便于退出时快速响应
        for (int i = 0; i < 50 && !g_summonQuit; i++) Sleep(100);
        if (g_summonQuit) break;
        if (!g_autoCloseEnabled.load()) continue;
        std::vector<std::string> names;
        {
            std::lock_guard<std::mutex> lk(g_autoCloseMx);
            names = g_autoCloseProcs;
        }
        if (names.empty()) continue;
        // 枚举进程，收集匹配 PID
        std::unordered_set<DWORD> pids;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe{ sizeof(pe) };
            if (Process32FirstW(snap, &pe)) {
                do {
                    std::wstring exe = pe.szExeFile;
                    if (exe.size() > 4 && _wcsicmp(exe.c_str() + exe.size() - 4, L".exe") == 0)
                        exe.resize(exe.size() - 4);
                    if (pe.th32ProcessID != selfPid && acNameMatch(exe, names))
                        pids.insert(pe.th32ProcessID);
                } while (Process32NextW(snap, &pe));
            }
            CloseHandle(snap);
        }
        if (!pids.empty()) {
            AcCloseCtx ctx{ &pids, selfPid };
            EnumWindows(acCloseEnum, reinterpret_cast<LPARAM>(&ctx));
        }
    }
    return 0;
}
// 将已隐藏/后台的窗口带到前台（绕过前台锁，可靠抢焦）。
// 所有抢焦都采用有限、非阻塞的状态机：成功立即停止，绝不在 UI 线程 Sleep。
#define SUMMON_FOCUS_TIMER_ID 0x5A31
#define RETURN_GAME_FOCUS_TIMER_ID 0x5A32
#define GP_HOLD_TIMER_ID 0x5A33  // 手柄快捷键"按住0.5s/连发"复检（仅按住期间运行）
#define FOCUS_DISPLAY_TIMER_ID 0x5A34

static void showWindowAnimated(HWND hwnd, int showCmd, bool activate);
static void hideWindowAnimated(HWND hwnd);

static ULONGLONG focusFileTimeTicks(const FILETIME& ft) {
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

static bool focusQueryProcessIdentity(DWORD pid, std::wstring* path, ULONGLONG* created) {
    if (!pid) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    if (path) {
        std::vector<wchar_t> image(32768);
        DWORD length = static_cast<DWORD>(image.size());
        if (QueryFullProcessImageNameW(process, 0, image.data(), &length))
            path->assign(image.data(), length);
        else
            path->clear();
    }
    if (created) {
        FILETIME create{}, exit{}, kernel{}, user{};
        *created = GetProcessTimes(process, &create, &exit, &kernel, &user)
            ? focusFileTimeTicks(create) : 0;
    }
    CloseHandle(process);
    return true;
}

static std::wstring focusWindowClass(HWND hwnd) {
    wchar_t cls[256]{};
    if (hwnd) GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
    return cls;
}

static bool focusMonitorInfo(HMONITOR monitor, std::wstring* device, RECT* rect) {
    if (!monitor) return false;
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return false;
    if (device) *device = info.szDevice;
    if (rect) *rect = info.rcMonitor;
    return true;
}

struct FocusMonitorFindContext {
    std::wstring device;
    HMONITOR result = nullptr;
};

static BOOL CALLBACK focusFindMonitorEnum(HMONITOR monitor, HDC, LPRECT, LPARAM param) {
    auto* ctx = reinterpret_cast<FocusMonitorFindContext*>(param);
    std::wstring device;
    if (ctx && focusMonitorInfo(monitor, &device, nullptr) &&
        _wcsicmp(device.c_str(), ctx->device.c_str()) == 0) {
        ctx->result = monitor;
        return FALSE;
    }
    return TRUE;
}

static HMONITOR focusResolveMonitor(const FocusTargetSnapshot& target) {
    if (!target.monitorDevice.empty()) {
        FocusMonitorFindContext ctx{target.monitorDevice, nullptr};
        EnumDisplayMonitors(nullptr, nullptr, focusFindMonitorEnum, reinterpret_cast<LPARAM>(&ctx));
        if (ctx.result) return ctx.result;
    }
    if (target.hwnd && IsWindow(target.hwnd))
        return MonitorFromWindow(target.hwnd, MONITOR_DEFAULTTONEAREST);
    POINT pt{target.monitorRect.left, target.monitorRect.top};
    return MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
}

static bool focusWindowCoversMonitor(HWND hwnd, RECT monitorRect) {
    RECT windowRect{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                                     &windowRect, sizeof(windowRect))) &&
        !GetWindowRect(hwnd, &windowRect)) {
        return false;
    }
    RECT intersection{};
    if (!IntersectRect(&intersection, &windowRect, &monitorRect)) return false;
    const long long monitorArea = static_cast<long long>(monitorRect.right - monitorRect.left) *
                                  static_cast<long long>(monitorRect.bottom - monitorRect.top);
    const long long intersectionArea = static_cast<long long>(intersection.right - intersection.left) *
                                       static_cast<long long>(intersection.bottom - intersection.top);
    const int tolerance = (std::max)(3, MulDiv(3, static_cast<int>(GetDpiForWindow(hwnd)), 96));
    const bool edgeMatch = abs(windowRect.left - monitorRect.left) <= tolerance &&
                           abs(windowRect.top - monitorRect.top) <= tolerance &&
                           abs(windowRect.right - monitorRect.right) <= tolerance &&
                           abs(windowRect.bottom - monitorRect.bottom) <= tolerance;
    return edgeMatch || (monitorArea > 0 && intersectionArea * 100 >= monitorArea * 98);
}

static bool focusWindowLooksFullscreen(HWND hwnd, RECT monitorRect) {
    if (!focusWindowCoversMonitor(hwnd, monitorRect)) return false;
    QUERY_USER_NOTIFICATION_STATE state = QUNS_NOT_PRESENT;
    const bool d3dFullscreen = SUCCEEDED(SHQueryUserNotificationState(&state)) &&
                               state == QUNS_RUNNING_D3D_FULL_SCREEN;
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    return d3dFullscreen || (style & WS_POPUP) != 0 || (style & WS_CAPTION) == 0;
}

static bool focusKnownOverlayName(const std::wstring& name) {
    static const wchar_t* overlays[] = {
        L"losslessscaling", L"magpie", L"gameoverlayui", L"gamebar",
        L"gamebarftserver", L"gamebarpresencewriter", L"xboxgamebarwidgets",
        L"rtss", L"rtsshooksloader32", L"rtsshooksloader64"
    };
    for (const auto* overlay : overlays) {
        if (name == overlay) return true;
    }
    return false;
}

static std::wstring focusWindowProcessName(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    std::wstring path;
    focusQueryProcessIdentity(pid, &path, nullptr);
    return sgBaseName(path);
}

static bool focusIsKnownOverlayWindow(HWND hwnd) {
    return focusKnownOverlayName(focusWindowProcessName(hwnd));
}

static FocusTargetSnapshot focusCaptureTarget(HWND hwnd) {
    FocusTargetSnapshot target;
    if (!hwnd || !IsWindow(hwnd) || hwnd == g_hwnd) return target;
    HWND root = GetAncestor(hwnd, GA_ROOTOWNER);
    if (root && IsWindow(root) && root != g_hwnd) hwnd = root;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || pid == GetCurrentProcessId()) return target;
    target.hwnd = hwnd;
    target.pid = pid;
    focusQueryProcessIdentity(pid, &target.path, &target.processCreated);
    target.className = focusWindowClass(hwnd);
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    focusMonitorInfo(monitor, &target.monitorDevice, &target.monitorRect);
    target.fullscreen = focusWindowLooksFullscreen(hwnd, target.monitorRect);
    target.valid = true;
    return target;
}

static bool focusTargetIdentityMatches(const FocusTargetSnapshot& target, DWORD pid) {
    if (!target.valid || !pid || pid != target.pid) return false;
    std::wstring currentPath;
    ULONGLONG currentCreated = 0;
    if (!focusQueryProcessIdentity(pid, &currentPath, &currentCreated))
        return true; // 受保护进程：原 HWND+PID 仍比放弃恢复更可靠
    if (target.processCreated && currentCreated && target.processCreated != currentCreated) return false;
    if (!target.path.empty() && !currentPath.empty() &&
        _wcsicmp(target.path.c_str(), currentPath.c_str()) != 0) return false;
    return true;
}

static bool isUsableFocusWindow(HWND hwnd, DWORD expectedPid, bool allowIconic = false) {
    if (!hwnd || hwnd == g_hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) return false;
    if (!allowIconic && IsIconic(hwnd)) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || (expectedPid && pid != expectedPid)) return false;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return false;
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return false;
    RECT rect{};
    return GetWindowRect(hwnd, &rect) && rect.right > rect.left && rect.bottom > rect.top;
}

static bool focusIsRestorableExactWindow(HWND hwnd, DWORD expectedPid) {
    if (!hwnd || hwnd == g_hwnd || !IsWindow(hwnd)) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || pid != expectedPid) return false;
    if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return false;
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return false;
    RECT rect{};
    return GetWindowRect(hwnd, &rect) && rect.right > rect.left && rect.bottom > rect.top;
}

static bool focusPidIsManuallyPaused(DWORD pid) {
    if (!pid) return false;
    if (g_manualPausedPid.load(std::memory_order_acquire) == pid) return true;
    // 启动残留或异常退出时，内存状态可能还没有恢复，但标记仍是权威依据。
    // 这里只做一次轻量存在性判断；不会枚举进程、读取路径或向目标窗口发消息。
    const std::wstring marker = SG_MANUAL_DIR + L"\\" + std::to_wstring(pid) + L".txt";
    return fspath::exists(marker);
}

static bool focusGameControlBlocked() {
    return g_gameControlInFlight.load(std::memory_order_acquire) ||
           g_manualPausedPid.load(std::memory_order_acquire) != 0;
}

struct FocusWindowCandidate {
    DWORD pid = 0;
    const FocusTargetSnapshot* preferred = nullptr;
    HWND hwnd = nullptr;
    long long score = (std::numeric_limits<long long>::min)();
};

static BOOL CALLBACK findFocusWindowEnum(HWND hwnd, LPARAM param) {
    auto* candidate = reinterpret_cast<FocusWindowCandidate*>(param);
    if (!candidate || !isUsableFocusWindow(hwnd, candidate->pid, true)) return TRUE;
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    long long score = static_cast<long long>(rect.right - rect.left) *
                      static_cast<long long>(rect.bottom - rect.top);
    if (!GetWindow(hwnd, GW_OWNER)) score += (1LL << 48);
    if (candidate->preferred) {
        if (!candidate->preferred->className.empty() &&
            _wcsicmp(candidate->preferred->className.c_str(), focusWindowClass(hwnd).c_str()) == 0)
            score += (1LL << 50);
        std::wstring monitorDevice;
        focusMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitorDevice, nullptr);
        if (!candidate->preferred->monitorDevice.empty() &&
            _wcsicmp(candidate->preferred->monitorDevice.c_str(), monitorDevice.c_str()) == 0)
            score += (1LL << 49);
    }
    if (score > candidate->score) {
        candidate->score = score;
        candidate->hwnd = hwnd;
    }
    return TRUE;
}

static HWND focusFindWindowForPid(DWORD pid, const FocusTargetSnapshot* preferred = nullptr) {
    if (!pid) return nullptr;
    FocusWindowCandidate candidate{pid, preferred, nullptr, (std::numeric_limits<long long>::min)()};
    EnumWindows(findFocusWindowEnum, reinterpret_cast<LPARAM>(&candidate));
    return candidate.hwnd;
}

static DWORD focusFindProcessByPath(const std::wstring& wantedPath) {
    if (wantedPath.empty()) return 0;
    DWORD currentSession = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSession)) return 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    DWORD result = 0;
    PROCESSENTRY32W entry{sizeof(entry)};
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (!entry.th32ProcessID || entry.th32ProcessID == GetCurrentProcessId()) continue;
            DWORD session = 0;
            if (!ProcessIdToSessionId(entry.th32ProcessID, &session) || session != currentSession) continue;
            std::wstring path;
            if (focusQueryProcessIdentity(entry.th32ProcessID, &path, nullptr) &&
                !path.empty() && _wcsicmp(path.c_str(), wantedPath.c_str()) == 0 &&
                focusFindWindowForPid(entry.th32ProcessID)) {
                result = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

static bool focusResolveTarget(FocusTargetSnapshot& target) {
    if (!target.valid || !target.pid) return false;
    if (focusGameControlBlocked() || focusPidIsManuallyPaused(target.pid)) return false;
    if (focusIsRestorableExactWindow(target.hwnd, target.pid) &&
        focusTargetIdentityMatches(target, target.pid)) return true;
    if (focusTargetIdentityMatches(target, target.pid)) {
        if (HWND replacement = focusFindWindowForPid(target.pid, &target)) {
            target.hwnd = replacement;
            target.className = focusWindowClass(replacement);
            return true;
        }
    }
    if (!target.path.empty()) {
        const DWORD replacementPid = focusFindProcessByPath(target.path);
        if (replacementPid) {
            target.pid = replacementPid;
            focusQueryProcessIdentity(replacementPid, nullptr, &target.processCreated);
            target.hwnd = focusFindWindowForPid(replacementPid, &target);
            if (target.hwnd) {
                target.className = focusWindowClass(target.hwnd);
                return true;
            }
        }
    }
    return false;
}

static FocusTargetSnapshot focusRememberedGameTarget() {
    std::lock_guard<std::mutex> lock(g_rememberedGameTargetMx);
    if (!g_rememberedGameTarget.valid || GetTickCount64() > g_rememberedGameDeadline) {
        g_rememberedGameTarget = {};
        g_rememberedGameDeadline = 0;
        return {};
    }
    FocusTargetSnapshot target = g_rememberedGameTarget;
    if (!focusResolveTarget(target)) return {};
    g_rememberedGameTarget = target;
    return target;
}

static FocusTargetSnapshot focusFindUnderlyingTarget(HWND overlay) {
    FocusTargetSnapshot best;
    HMONITOR overlayMonitor = MonitorFromWindow(overlay, MONITOR_DEFAULTTONEAREST);
    long long bestArea = 0;
    int inspected = 0;
    for (HWND hwnd = GetWindow(overlay, GW_HWNDNEXT); hwnd && inspected < 128;
         hwnd = GetWindow(hwnd, GW_HWNDNEXT), ++inspected) {
        if (!isUsableFocusWindow(hwnd, 0, false) || focusIsKnownOverlayWindow(hwnd)) continue;
        if (MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) != overlayMonitor) continue;
        const std::wstring name = focusWindowProcessName(hwnd);
        if (nativeMonitorExcluded(name)) continue;
        RECT rect{};
        GetWindowRect(hwnd, &rect);
        const long long area = static_cast<long long>(rect.right - rect.left) *
                               static_cast<long long>(rect.bottom - rect.top);
        if (area > bestArea) {
            best = focusCaptureTarget(hwnd);
            bestArea = area;
        }
    }
    return best;
}

static FocusTargetSnapshot focusCaptureForegroundTarget() {
    HWND foreground = GetForegroundWindow();
    FocusTargetSnapshot exact = focusCaptureTarget(foreground);
    if (!exact.valid) return {};
    if (focusIsKnownOverlayWindow(exact.hwnd)) {
        FocusTargetSnapshot remembered = focusRememberedGameTarget();
        if (remembered.valid) {
            remembered.fullscreen = remembered.fullscreen || exact.fullscreen;
            return remembered;
        }
        FocusTargetSnapshot underlying = focusFindUnderlyingTarget(exact.hwnd);
        if (underlying.valid) {
            underlying.fullscreen = underlying.fullscreen || exact.fullscreen;
            return underlying;
        }
    }
    return exact;
}

static bool focusMainWindowIsForeground() {
    HWND foreground = GetForegroundWindow();
    if (!foreground) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(foreground, &pid);
    return pid == GetCurrentProcessId();
}

static bool refocusWebView(bool allowAltFallback = false) {
    if (!g_hwnd || !IsWindow(g_hwnd)) return false;
    if (IsIconic(g_hwnd) || !IsWindowVisible(g_hwnd)) ShowWindow(g_hwnd, SW_RESTORE);
    auto attempt = []() -> bool {
        HWND foreground = GetForegroundWindow();
        DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
        const DWORD currentThread = GetCurrentThreadId();
        DWORD foregroundPid = 0;
        if (foreground) GetWindowThreadProcessId(foreground, &foregroundPid);
        // Never attach the native UI thread to a process being controlled or
        // to a manually suspended PID. Its input queue may be non-responsive.
        const bool attached = !focusGameControlBlocked() &&
                              !focusPidIsManuallyPaused(foregroundPid) &&
                              foregroundThread && foregroundThread != currentThread &&
                              AttachThreadInput(currentThread, foregroundThread, TRUE) != FALSE;
        BringWindowToTop(g_hwnd);
        SetForegroundWindow(g_hwnd);
        SetActiveWindow(g_hwnd);
        SetFocus(g_hwnd);
        if (attached) AttachThreadInput(currentThread, foregroundThread, FALSE);
        return focusMainWindowIsForeground();
    };
    bool focused = attempt();
    if (!focused && allowAltFallback) {
        INPUT inputs[2]{};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_MENU;
        inputs[1] = inputs[0];
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, inputs, sizeof(INPUT));
        focused = attempt();
    }
    if (focused && g_ctrl)
        g_ctrl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    return focused;
}

static void focusReleaseOwnedTopmost() {
    if (g_focusSession.ownedTopmost && g_hwnd && IsWindow(g_hwnd))
        SetWindowPos(g_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    g_focusSession.ownedTopmost = false;
}

static void focusClearSession() {
    if (g_hwnd) {
        KillTimer(g_hwnd, SUMMON_FOCUS_TIMER_ID);
        KillTimer(g_hwnd, RETURN_GAME_FOCUS_TIMER_ID);
    }
    focusReleaseOwnedTopmost();
    g_focusSession = {};
    g_pendingSummonTarget = {};
    g_summonFocusRetries = 0;
    g_summonAltTried = false;
    g_bClosePending = false;
    g_bClosePendingSince = 0;
    g_bCloseReadyAt = 0;
}

static void focusPrepareSummonSession() {
    const bool summonedFromAnotherProcess = !focusMainWindowIsForeground();
    if (g_pendingSummonTarget.valid || summonedFromAnotherProcess) {
        focusReleaseOwnedTopmost();
        g_focusSession = {};
        g_focusSession.target = g_pendingSummonTarget;
    }
    g_pendingSummonTarget = {};
    g_focusSession.active = true;
    g_focusSession.returning = false;
    g_focusSession.returnStarted = 0;
    g_focusSession.returnDeadline = 0;
    const LONG_PTR exStyle = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOPMOST) == 0) g_focusSession.ownedTopmost = true;
}

static HMONITOR focusCurrentTargetMonitor() {
    if (g_focusSession.target.valid) return focusResolveMonitor(g_focusSession.target);
    return g_hwnd ? MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST) : nullptr;
}

static void focusReflowMainWindow() {
    if (g_fullHeight && g_hwnd) applyFullHeightLayout(focusCurrentTargetMonitor());
}

static void focusScheduleDisplayReflow() {
    if (!g_hwnd) return;
    KillTimer(g_hwnd, FOCUS_DISPLAY_TIMER_ID);
    SetTimer(g_hwnd, FOCUS_DISPLAY_TIMER_ID, 250, nullptr);
}

static HWND resolvePreviousFocusWindow(bool allowGameFallback = false) {
    if (g_focusSession.target.valid && focusResolveTarget(g_focusSession.target))
        return g_focusSession.target.hwnd;

    FocusTargetSnapshot remembered = focusRememberedGameTarget();
    if (remembered.valid) {
        g_focusSession.target = remembered;
        return remembered.hwnd;
    }

    // 不再根据“当前最大工作集进程”猜测回焦目标。暂停、插帧、覆盖层和
    // 多显示器切换都会让这个猜测不可靠；没有已保存的窗口就保持隐藏。
    if (allowGameFallback && !focusPidIsManuallyPaused(g_manualPausedPid.load(std::memory_order_acquire))) {
        const auto game = nativeDetectGame();
        if (game.pid && !focusPidIsManuallyPaused(game.pid)) {
            FocusTargetSnapshot fallback;
            fallback.pid = game.pid;
            fallback.path = game.path;
            focusQueryProcessIdentity(game.pid, nullptr, &fallback.processCreated);
            fallback.hwnd = focusFindWindowForPid(game.pid);
            fallback.valid = fallback.hwnd != nullptr;
            if (fallback.valid) {
                fallback.className = focusWindowClass(fallback.hwnd);
                HMONITOR monitor = MonitorFromWindow(fallback.hwnd, MONITOR_DEFAULTTONEAREST);
                focusMonitorInfo(monitor, &fallback.monitorDevice, &fallback.monitorRect);
                fallback.fullscreen = focusWindowLooksFullscreen(fallback.hwnd, fallback.monitorRect);
                g_focusSession.target = fallback;
                return fallback.hwnd;
            }
        }
    }
    return nullptr;
}

static bool focusForegroundMatchesTarget() {
    if (!g_focusSession.target.valid) return false;
    HWND foreground = GetForegroundWindow();
    DWORD pid = 0;
    if (foreground) GetWindowThreadProcessId(foreground, &pid);
    return pid != 0 && pid == g_focusSession.target.pid;
}

static bool refocusPreviousWindow() {
    if (focusGameControlBlocked()) return false;
    // 回焦只使用呼出时保存的窗口快照，绝不在返回桌面/关闭窗口时重新扫描并猜游戏。
    const bool allowFallback = false;
    HWND target = resolvePreviousFocusWindow(allowFallback);
    if (!target) return false;
    if (!IsWindowVisible(target)) ShowWindowAsync(target, SW_SHOW);
    if (IsIconic(target)) ShowWindowAsync(target, SW_RESTORE);
    const DWORD targetThread = GetWindowThreadProcessId(target, nullptr);
    const DWORD currentThread = GetCurrentThreadId();
    HWND foreground = GetForegroundWindow();
    const DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    DWORD targetPid = 0;
    GetWindowThreadProcessId(target, &targetPid);
    if (focusGameControlBlocked() || focusPidIsManuallyPaused(targetPid)) return false;
    // Never attach the native UI thread to the target game input queue. A
    // suspended game can leave that queue blocked and freeze the shell.
    DWORD foregroundPid = 0;
    if (foreground) GetWindowThreadProcessId(foreground, &foregroundPid);
    const bool attachedForeground = foregroundThread && foregroundThread != currentThread &&
                                    foregroundThread != targetThread &&
                                    !focusPidIsManuallyPaused(foregroundPid) &&
                                    AttachThreadInput(currentThread, foregroundThread, TRUE) != FALSE;
    BringWindowToTop(target);
    SetForegroundWindow(target);
    if (attachedForeground) AttachThreadInput(currentThread, foregroundThread, FALSE);
    return focusForegroundMatchesTarget();
}

static bool focusForegroundIsTransientOrShell() {
    HWND foreground = GetForegroundWindow();
    if (!foreground) return true;
    DWORD pid = 0;
    GetWindowThreadProcessId(foreground, &pid);
    if (pid == GetCurrentProcessId() ||
        (g_focusSession.target.valid && pid == g_focusSession.target.pid)) return true;
    const std::wstring cls = focusWindowClass(foreground);
    if (_wcsicmp(cls.c_str(), L"Progman") == 0 || _wcsicmp(cls.c_str(), L"WorkerW") == 0 ||
        _wcsicmp(cls.c_str(), L"Shell_TrayWnd") == 0) return true;
    return focusIsKnownOverlayWindow(foreground);
}

static void focusBeginReturnToPreviousWindow(ULONGLONG now) {
    KillTimer(g_hwnd, SUMMON_FOCUS_TIMER_ID);
    g_summonFocusRetries = 0;
    g_summonAltTried = false;
    g_focusSession.returning = true;
    g_focusSession.returnStarted = now;
    g_focusSession.returnDeadline = now + (g_focusSession.target.fullscreen ? 3200 : 2200);
    focusReleaseOwnedTopmost();
    hideWindowAnimated(g_hwnd);
    if (focusGameControlBlocked() || focusPidIsManuallyPaused(g_focusSession.target.pid)) {
        focusClearSession();
        return;
    }
    if (refocusPreviousWindow()) {
        focusClearSession();
        return;
    }
    SetTimer(g_hwnd, RETURN_GAME_FOCUS_TIMER_ID, 50, nullptr);
}

// Wake recovery must not depend on the renderer delivering the one-shot
// resume event. WebView2 can retain the last compositor frame while its input
// connection is stale; this nudge is UI-thread-only, bounded, and does not
// navigate or run any external command.
static void nudgeWebViewAfterResume(bool resetVisibility) {
    if (!g_hwnd || !IsWindow(g_hwnd) || !g_ctrl || g_exitRequested.load(std::memory_order_acquire)) return;
    const BOOL visible = IsWindowVisible(g_hwnd) && !IsIconic(g_hwnd);
    // A normal window restore only needs a bounds/focus refresh. Toggling the
    // controller invisible->visible here races DWM/WebView2 composition and
    // produces a one-frame black/transparent flash. Keep the hard reset for
    // sleep/renderer recovery, where the compositor may actually be stale.
    if (resetVisibility) g_ctrl->put_IsVisible(FALSE);
    g_ctrl->put_IsVisible(visible || g_deferFirstShow);
    RECT bounds{};
    if (GetClientRect(g_hwnd, &bounds)) g_ctrl->put_Bounds(bounds);
    if (visible) {
        SetActiveWindow(g_hwnd);
        g_ctrl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    }
}

static void stopPowerResumeWatchdog() {
    if (!g_hwnd) return;
    KillTimer(g_hwnd, POWER_RESUME_NUDGE_TIMER_ID);
    KillTimer(g_hwnd, POWER_RESUME_WATCHDOG_TIMER_ID);
    g_resumeWatchdogGeneration = 0;
    g_resumeWatchdogAttempts = 0;
    g_resumeProbeGeneration = 0;
    g_resumeProbeAttempts = 0;
    g_resumeProbeInFlight = false;
    g_resumeProbeForcedReset = false;
}

static void appendWebViewDiagnostic(json entry);
static void ipc_emit(const std::string& ev, const json& data);
static void probePowerResumeStability(unsigned long long generation);

static void schedulePowerResumeProbe(unsigned long long generation, UINT delayMs) {
    if (!g_hwnd || generation == 0 ||
        g_powerLifecycle.load(std::memory_order_acquire) != PowerLifecycle::Resuming ||
        generation != currentPowerGeneration() || g_exitRequested.load(std::memory_order_acquire)) return;
    g_resumeProbeGeneration = generation;
    KillTimer(g_hwnd, POWER_RESUME_NUDGE_TIMER_ID);
    SetTimer(g_hwnd, POWER_RESUME_NUDGE_TIMER_ID, delayMs, nullptr);
}

// A wake probe only asks the renderer for a tiny DOM snapshot. A successful
// response proves that the page has resumed; it avoids a visibility reset on
// every wake. The reset is deliberately delayed until two bounded probe
// failures, with a third probe as the final observation before the normal
// watchdog can recover the renderer.
static void probePowerResumeStability(unsigned long long generation) {
    if (!g_view || generation != currentPowerGeneration() ||
        g_powerLifecycle.load(std::memory_order_acquire) != PowerLifecycle::Resuming ||
        g_resumeProbeInFlight || g_resumeProbeAttempts >= 3) return;

    g_resumeProbeInFlight = true;
    ++g_resumeProbeAttempts;
    static constexpr wchar_t script[] =
        LR"(JSON.stringify({readyState:document.readyState,visibility:document.visibilityState,app:!!document.getElementById('app'),body:!!document.body}))";
    const HRESULT hr = g_view->ExecuteScript(
        script,
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [generation](HRESULT callbackHr, LPCWSTR result) -> HRESULT {
                if (generation != currentPowerGeneration()) return S_OK;
                // The renderer callback can arrive after the native wake
                // transaction has already committed (or been superseded by
                // a new power event). Do not leave the old probe marked as
                // in-flight, otherwise the next probe for this generation
                // could be suppressed indefinitely.
                if (g_powerLifecycle.load(std::memory_order_acquire) != PowerLifecycle::Resuming) {
                    g_resumeProbeInFlight = false;
                    return S_OK;
                }
                g_resumeProbeInFlight = false;
                bool healthy = false;
                std::string readyState;
                std::string visibility;
                try {
                    if (SUCCEEDED(callbackHr) && result) {
                        auto outer = json::parse(W2U(result));
                        auto state = outer.is_string() ? json::parse(outer.get<std::string>()) : outer;
                        readyState = state.value("readyState", std::string{});
                        visibility = state.value("visibility", std::string{});
                        healthy = (readyState == "interactive" || readyState == "complete") &&
                                  state.value("app", false) && state.value("body", false);
                    }
                } catch (...) {}
                appendWebViewDiagnostic({
                    {"event", healthy ? "power-resume-ui-stable" : "power-resume-ui-probe-failed"},
                    {"generation", generation},
                    {"attempt", g_resumeProbeAttempts},
                    {"hresult", static_cast<int64_t>(callbackHr)},
                    {"readyState", readyState},
                    {"visibility", visibility}
                });
                if (healthy) {
                    ipc_emit("power.resume-ui-stable", {{"generation", generation}});
                    return S_OK;
                }

                if (!g_resumeProbeForcedReset && g_resumeProbeAttempts >= 2) {
                    g_resumeProbeForcedReset = true;
                    // Only the failed-wake path reaches this hard reset. Normal
                    // restore and summon paths use bounds/focus refresh only.
                    nudgeWebViewAfterResume(true);
                }
                if (g_resumeProbeAttempts < 3)
                    schedulePowerResumeProbe(generation, POWER_RESUME_PROBE_RETRY_MS);
                else
                    ipc_emit("power.resume-ui-degraded", {{"generation", generation}});
                return S_OK;
            }).Get());
    if (FAILED(hr)) {
        g_resumeProbeInFlight = false;
        if (g_resumeProbeAttempts < 3)
            schedulePowerResumeProbe(generation, POWER_RESUME_PROBE_RETRY_MS);
    }
}

static void armPowerResumeWatchdog(unsigned long long generation, UINT delayMs) {
    if (!g_hwnd || generation == 0 || g_exitRequested.load(std::memory_order_acquire)) return;
    // Windows may deliver both RESUMEAUTOMATIC and RESUMESUSPEND for one
    // wake, followed by the sleep-guard worker's resume-ready message. Keep
    // the first watchdog deadline instead of resetting it for every duplicate
    // notification; the watchdog must remain independent of the WebView2
    // renderer and of the sleep-guard worker.
    if (g_resumeWatchdogGeneration == generation) return;
    g_resumeWatchdogGeneration = generation;
    g_resumeWatchdogAttempts = 0;
    KillTimer(g_hwnd, POWER_RESUME_NUDGE_TIMER_ID);
    KillTimer(g_hwnd, POWER_RESUME_WATCHDOG_TIMER_ID);
    SetTimer(g_hwnd, POWER_RESUME_NUDGE_TIMER_ID, 300, nullptr);
    SetTimer(g_hwnd, POWER_RESUME_WATCHDOG_TIMER_ID, delayMs, nullptr);
}

static void bringToFront(HWND hwnd) {
    if (!hwnd) return;
    focusPrepareSummonSession();
    focusReflowMainWindow();
    // 最小化(WS_VISIBLE 仍在但已收进任务栏，IsWindowVisible 仍返回 TRUE)也必须恢复，
    // 否则呼出/抢焦时 ShowWindow 被跳过 → 窗口仍是最小化、看不到（"程序觉得有页面但实际没有"）。
    // Always use the normal restore path.  The HWND can remain marked visible
    // after an animated tray hide while WebView2 and the frontend media are
    // already paused, so a controller-only restore is not sufficient.
    showWindowAnimated(hwnd, IsIconic(hwnd) ? SW_RESTORE : SW_SHOW, false);
    // 置顶仅属于本次呼出会话；隐藏/返回游戏时恢复原状态。
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    // 先使用标准前台切换；失败后定时器只进行一次 Alt 兼容兜底。
    g_summonFocusRetries = 0;
    g_summonAltTried = false;
    if (refocusWebView(false)) {
        KillTimer(g_hwnd, SUMMON_FOCUS_TIMER_ID);
    } else {
        SetTimer(g_hwnd, SUMMON_FOCUS_TIMER_ID, 80, nullptr);
    }
}
// 后台手柄轮询线程：检测任意手柄 LB+RB 同时按住满 0.5 秒 → 呼出程序（仅当窗口未在前台时）
static void hideWindowAnimated(HWND hwnd); // 前向声明（本线程要用）
// ── 手柄全局快捷调节前向声明（调用 ipc_emit） ──
static inline int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static void nativeAdjustTdp(int delta);
static void nativeAdjustRtss(int delta);
static void nativeAdjustBrightness(int dir); // Start+左右转发亮度调节
// 浮动运行标志文件：autofloat 启用时写、关闭时删，且每 5 秒刷新时间戳；
// native 按 30 秒新鲜度判定，避免程序强杀/残留文件导致误判浮动仍在运行而吞掉按键。
static bool floatActive() {
    try {
        auto p = fspath::path(L"C:\\SOFT\\YeMan\\PowerControl\\float-active");
        if (!fspath::exists(p)) return false;
        auto last = fspath::last_write_time(p);
        auto now = fspath::file_time_type::clock::now();
        return (now - last) < std::chrono::seconds(30);
    } catch (...) { return false; }
}
static void runKillBat();          // 选择 + B → 执行 KiLL-EXE.bat 结束当前游戏
static void openTouchKeyboard();   // 选择 + X → 呼出 Windows 触摸键盘
static void returnToDesktop();     // 选择 + A → 内部 Win+D 立即返回桌面
static void toggleMouseMode();     // 选择 + Y → 启停当前选中的模拟鼠标后端
static void ipc_emit(const std::string& ev, const json& data);
// 后台手柄轮询线程：
//  - 按住 LB+RB 满 0.5 秒 → 呼出程序（仅当窗口隐藏在托盘时；全局免点击，绕开 Chromium Gamepad API 需先点击的限制）
//  - 0.5 秒内双击 B → 最小化到托盘（同样全局免点击，与前端引擎的双击 B 解耦）
// ── 事件驱动手柄：Raw Input 订阅（零后台占用，Tier A）──
// 替代原 100Hz 轮询线程：向 OS 注册 Raw Input（RIDEV_INPUTSINK 后台也能收到、
// RIDEV_DEVNOTIFY 收插拔），OS 仅在手柄状态变化时投递 WM_INPUT，空闲=0 CPU。
// 收到 WM_INPUT 后用 XInputGetState 读权威按键态（映射稳定，无需解析 HID 报表），
// gamepadEval() 做边沿+计时器驱动的全局快捷键。所有"按住0.5s"用一次性计时器，
// 仅按住期间运行（50ms 复检），松开即停 → 空闲零占用。
// 原 ipc_emit("gamepad.state") 已删除：前端（engine.ts / GamepadVisualizer.vue）均用
// navigator.getGamepads() 直接读，无人消费该事件，属纯浪费（详见优化方案 MD）。
static bool g_padConnected   = false;
static bool g_rawInputRegistered = false;
static WORD  g_curW          = 0;     // 首个已连手柄的按钮位（权威态）
static WORD  g_prevW         = 0;     // 上一帧按钮位（边沿检测）
static XINPUT_GAMEPAD g_curPad{};     // 当前手柄完整状态，用于区分真实变化与周期性 HID 报文
static XINPUT_GAMEPAD g_prevPad{};    // 上一次手柄完整状态
static bool g_gpHoldTimerOn = false;  // 持有计时器是否在跑
static BYTE  g_riBuf[1024];           // WM_INPUT 报文缓冲（HID 报文通常 <256B）
// 快捷键状态（原线程局部变量提升为文件作用域，供 WM_INPUT / WM_TIMER 复用）
static bool gpArmed        = true;
static ULONGLONG gpHoldStart = 0;
static bool gpPrevB        = false;
static ULONGLONG gpLastBPress = 0;
static bool gpKbArmed = true, gpKxArmed = true, gpKyArmed = true;
static ULONGLONG gpKbHoldStart = 0, gpKxHoldStart = 0, gpKyHoldStart = 0;
static ULONGLONG s_dpHeldStart = 0, s_dpLastEmit = 0;
static ULONGLONG s_brightnessHeldStart = 0, s_brightnessLastEmit = 0;

static void gamepadReadState() {
    bool live = false;
    XINPUT_GAMEPAD pad{};
    for (DWORD i = 0; i < 4; i++) {
        XINPUT_STATE s; ZeroMemory(&s, sizeof(s));
        if (XInputGetState(i, &s) == ERROR_SUCCESS) {
            live = true;
            pad = s.Gamepad;
            break;
        }
    }
    g_padConnected = live;
    g_curPad = pad;
    g_curW = pad.wButtons;
}

static void gamepadResetNativeState(bool requireRelease) {
    g_curW = 0;
    g_prevW = 0;
    ZeroMemory(&g_curPad, sizeof(g_curPad));
    ZeroMemory(&g_prevPad, sizeof(g_prevPad));
    gpArmed = true;
    gpPrevB = false;
    gpHoldStart = 0;
    gpLastBPress = 0;
    g_bClosePending = false;
    g_bClosePendingSince = 0;
    g_bCloseReadyAt = 0;
    g_pendingSummonTarget = {};
    gpKbArmed = gpKxArmed = gpKyArmed = true;
    gpKbHoldStart = gpKxHoldStart = gpKyHoldStart = 0;
    s_dpHeldStart = s_dpLastEmit = 0;
    s_brightnessHeldStart = s_brightnessLastEmit = 0;
    if (g_gpHoldTimerOn && g_hwnd) {
        KillTimer(g_hwnd, GP_HOLD_TIMER_ID);
        g_gpHoldTimerOn = false;
    }
    g_inputReleaseRequired.store(requireRelease, std::memory_order_release);
}

// 任何与"按住0.5s / 连发"相关的键仍按下 → 需保持持有计时器
static bool gpAnyShortcutHeld() {
    return g_bClosePending || (g_curW & (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER |
        XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK |
        XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN |
        XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT)) != 0;
}

// 边沿+计时器驱动的全部全局快捷键（WM_INPUT 唤醒 与 持有计时器复检 都调用，幂等）
static void gamepadEval() {
    const ULONGLONG HOLD_MS = 500;
    const ULONGLONG B_DOUBLE_MS = 500;
    gamepadReadState();
    if (!g_inputReady.load(std::memory_order_acquire)) {
        // 恢复事务完成前只刷新快照，不执行任何快捷动作。
        g_prevW = g_curW;
        g_prevPad = g_curPad;
        return;
    }
    if (g_inputReleaseRequired.load(std::memory_order_acquire)) {
        g_prevW = g_curW;
        g_prevPad = g_curPad;
        if (!gpAnyShortcutHeld())
            g_inputReleaseRequired.store(false, std::memory_order_release);
        return;
    }
    if (g_padConnected && memcmp(&g_curPad, &g_prevPad, sizeof(XINPUT_GAMEPAD)) != 0)
        sgMarkInputActivity();
    WORD w = g_curW;
    bool both       = (w & XINPUT_GAMEPAD_LEFT_SHOULDER) && (w & XINPUT_GAMEPAD_RIGHT_SHOULDER);
    bool bPressed   = (w & XINPUT_GAMEPAD_B) != 0;
    bool aPressed   = (w & XINPUT_GAMEPAD_A) != 0;
    bool startHeld  = (w & XINPUT_GAMEPAD_START) != 0;
    bool selectHeld = (w & XINPUT_GAMEPAD_BACK) != 0;
    bool xPressed   = (w & XINPUT_GAMEPAD_X) != 0;
    bool yPressed   = (w & XINPUT_GAMEPAD_Y) != 0;
    bool dpUp   = (w & XINPUT_GAMEPAD_DPAD_UP) != 0;
    bool dpDown = (w & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
    bool dpLeft = (w & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
    bool dpRight= (w & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
    ULONGLONG now = GetTickCount64();

    const bool shoulderDown = (w & (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER)) != 0;
    const bool shoulderWasDown = (g_prevW & (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER)) != 0;
    // 在首个肩键刚按下时就记录原前台，防止 Steam/Game Bar/LS 在0.5秒等待期内抢走前台。
    if (g_summonEnabled && shoulderDown && !shoulderWasDown && !focusMainWindowIsForeground())
        g_pendingSummonTarget = focusCaptureForegroundTarget();

    // ── 双击 B → 等待第二次B释放后隐藏并回焦，避免B键泄漏到游戏 ──
    if (bPressed && !gpPrevB && !g_bClosePending) {
        const bool canClose = g_summonEnabled && g_bDoubleMinimize &&
                              g_hwnd && IsWindow(g_hwnd) && IsWindowVisible(g_hwnd);
        if (canClose && gpLastBPress != 0 && (now - gpLastBPress) <= B_DOUBLE_MS) {
            gpLastBPress = 0;
            g_bClosePending = true;
            g_bClosePendingSince = now;
            g_bCloseReadyAt = 0;
        } else if (canClose) {
            gpLastBPress = now;
        } else {
            gpLastBPress = 0;
        }
    }
    gpPrevB = bPressed;

    if (g_bClosePending) {
        // This is a global shortcut.  Do not cancel it merely because another
        // window became foreground while the second B is being released.
        // The old foreground gate made the shortcut depend on page/window
        // focus and caused an occasional missed minimize.
        if (!bPressed) {
            if (g_bCloseReadyAt == 0) g_bCloseReadyAt = now + 60;
            if (now >= g_bCloseReadyAt) {
                g_bClosePending = false;
                g_bClosePendingSince = 0;
                g_bCloseReadyAt = 0;
                focusBeginReturnToPreviousWindow(now);
            }
        } else if (now - g_bClosePendingSince >= 1500) {
            // 坏键/长按保护：不能无限阻塞隐藏动作。
            g_bClosePending = false;
            g_bClosePendingSince = 0;
            g_bCloseReadyAt = 0;
            focusBeginReturnToPreviousWindow(now);
        }
    }

    // ── 按住 LB+RB 0.5s → 强制呼出/抢焦（任意窗口态，全局免点击）──
    if (both) {
        if (gpHoldStart == 0) gpHoldStart = now;
        if (gpArmed && g_summonEnabled && g_hwnd && (now - gpHoldStart) >= HOLD_MS) {
            gpArmed = false;
            if (!g_pendingSummonTarget.valid && !focusMainWindowIsForeground())
                g_pendingSummonTarget = focusCaptureForegroundTarget();
            bringToFront(g_hwnd);
            ipc_emit("gamepad.summon", json::object());
        }
    } else {
        gpHoldStart = 0;
        if (!shoulderDown && gpArmed) g_pendingSummonTarget = {};
        gpArmed = true;
    }

    // ── Start + 方向键 全局快捷调节（TDP / RTSS 锁帧），按住自动连发 ──
    // 速度参数需与 src/gamepad/engine.ts 的 SLIDER_REPEAT_* / SLIDER_ACCEL_* 保持一致。
    if (startHeld) {
        if (g_tdpShortcut && (dpUp || dpDown)) {
            int delta = dpUp ? 1 : -1;
            ULONGLONG nowMs = GetTickCount64();
            if (s_dpHeldStart == 0) { s_dpHeldStart = nowMs; s_dpLastEmit = nowMs; nativeAdjustTdp(delta); }
            else {
                ULONGLONG heldMs = nowMs - s_dpHeldStart;
                ULONGLONG interval = 150;
                if (heldMs > 500) { ULONGLONG dec = (heldMs - 500) / 400 * 15; interval = dec >= 110 ? 40 : 150 - dec; }
                if (nowMs - s_dpLastEmit >= interval) { s_dpLastEmit = nowMs; nativeAdjustTdp(delta); }
            }
        } else { s_dpHeldStart = 0; }
        if (g_fpsShortcut && (dpRight || dpLeft)) {
            int dir = dpRight ? 1 : -1;
            ULONGLONG nowMs = GetTickCount64();
            if (s_brightnessHeldStart == 0) { s_brightnessHeldStart = nowMs; s_brightnessLastEmit = nowMs; nativeAdjustBrightness(dir); }
            else {
                ULONGLONG heldMs = nowMs - s_brightnessHeldStart;
                ULONGLONG interval = 150;
                if (heldMs > 500) { ULONGLONG dec = (heldMs - 500) / 400 * 15; interval = dec >= 110 ? 40 : 150 - dec; }
                if (nowMs - s_brightnessLastEmit >= interval) { s_brightnessLastEmit = nowMs; nativeAdjustBrightness(dir); }
            }
        } else { s_brightnessHeldStart = 0; }
    } else {
        s_dpHeldStart = 0;
        s_brightnessHeldStart = 0;
    }

    // ── 选择(Back) + B 长按 0.5s → 结束当前游戏 ──
    if (selectHeld && bPressed) {
        if (gpKbHoldStart == 0) gpKbHoldStart = now;
        if (gpKbArmed && g_killGame && (now - gpKbHoldStart) >= HOLD_MS) {
            gpKbArmed = false;
            runKillBat();
        }
    } else { gpKbHoldStart = 0; gpKbArmed = true; }

    // ── 选择(Back) + X 长按 0.5s → 打开 Windows 触摸键盘 ──
    if (selectHeld && xPressed) {
        if (gpKxHoldStart == 0) gpKxHoldStart = now;
        if (gpKxArmed && g_openKeyboard && (now - gpKxHoldStart) >= HOLD_MS) {
            gpKxArmed = false;
            openTouchKeyboard();
        }
    } else { gpKxHoldStart = 0; gpKxArmed = true; }

    // ── 选择(Back) + A 组合按下瞬间 → 返回桌面（只在组合刚建立时触发一次） ──
    const bool comboNow = selectHeld && aPressed;
    const bool comboPrev = (g_prevW & XINPUT_GAMEPAD_BACK) && (g_prevW & XINPUT_GAMEPAD_A);
    if (g_returnDesktop && comboNow && !comboPrev) {
        returnToDesktop();
    }

    // ── 选择(Back) + Y 长按 0.5s → 模拟鼠标开/关 ──
    if (selectHeld && yPressed) {
        if (gpKyHoldStart == 0) gpKyHoldStart = now;
        if (gpKyArmed && g_mouseToggle && (now - gpKyHoldStart) >= HOLD_MS) {
            gpKyArmed = false;
            toggleMouseMode();
        }
    } else { gpKyHoldStart = 0; gpKyArmed = true; }

    // 持有计时器：仅在有相关键按下时运行（空闲自动停 -> 0 CPU）
    if (gpAnyShortcutHeld()) {
        if (!g_gpHoldTimerOn) { SetTimer(g_hwnd, GP_HOLD_TIMER_ID, 50, nullptr); g_gpHoldTimerOn = true; }
    } else if (g_gpHoldTimerOn) {
        KillTimer(g_hwnd, GP_HOLD_TIMER_ID);
        g_gpHoldTimerOn = false;
    }
    g_prevW = w;
    g_prevPad = g_curPad;
}

// 注册 Raw Input 订阅：后台(窗口隐藏)也能收到手柄 WM_INPUT，并收插拔通知
static bool gamepadRegisterRawInput() {
    RAWINPUTDEVICE rid[4];
    rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x05; // Generic Desktop / Game Pad
    rid[0].dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
    rid[0].hwndTarget = g_hwnd;
    rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x04; // Generic Desktop / Joystick
    rid[1].dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
    rid[1].hwndTarget = g_hwnd;
    rid[2].usUsagePage = 0x01; rid[2].usUsage = 0x06; // Generic Desktop / Keyboard
    rid[2].dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
    rid[2].hwndTarget = g_hwnd;
    rid[3].usUsagePage = 0x01; rid[3].usUsage = 0x02; // Generic Desktop / Mouse
    rid[3].dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
    rid[3].hwndTarget = g_hwnd;
    g_rawInputRegistered = RegisterRawInputDevices(rid, 4, sizeof(RAWINPUTDEVICE)) != FALSE;
    return g_rawInputRegistered;
}

static bool gamepadRecoverAfterResume() {
    const bool rawOk = gamepadRegisterRawInput();
    gamepadResetNativeState(true);
    gamepadReadState();
    return rawOk;
}
// 休眠能力必须独立读取系统状态；电源键/睡眠键/合盖动作配置不是当前电源事件类型。
// S3/S4 都统一走同一套安全恢复事务，因此这里仅用于诊断，不用于跳过恢复链路。
static bool sgHibernateAvailable() {
    SYSTEM_POWER_CAPABILITIES caps{};
    if (!GetPwrCapabilities(&caps)) return false;
    return caps.SystemS4 != 0;
}

// 系统休眠开关必须独立于当前电源方案读取。SystemS4 表示硬件/固件能力，
// HibernateEnabled 表示 Windows 功能开关。绝不读取快速启动设置，也不使用休眠文件
// 是否存在来猜测开关状态，避免 reduced/快速启动文件与完整 S4 语义互相污染。
static json sgHibernateState() {
    SYSTEM_POWER_CAPABILITIES caps{};
    const bool capsKnown = GetPwrCapabilities(&caps) != FALSE;

    DWORD enabledValue = 0;
    DWORD enabledSize = sizeof(enabledValue);
    DWORD enabledType = 0;
    bool registryKnown = false;
    HKEY powerKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Control\\Power",
            0, KEY_READ | KEY_WOW64_64KEY, &powerKey) == ERROR_SUCCESS) {
        registryKnown = RegQueryValueExW(powerKey, L"HibernateEnabled", nullptr,
                            &enabledType, reinterpret_cast<BYTE*>(&enabledValue), &enabledSize) == ERROR_SUCCESS &&
                        enabledType == REG_DWORD && enabledSize == sizeof(enabledValue);
        RegCloseKey(powerKey);
    }

    const bool enabledKnown = registryKnown;
    const bool enabled = registryKnown && enabledValue != 0;
    const bool supportedKnown = capsKnown;
    const bool supported = capsKnown && caps.SystemS4 != 0;

    return json{
        {"supported", supported},
        {"supportedKnown", supportedKnown},
        {"enabled", enabled},
        {"enabledKnown", enabledKnown},
        {"source", registryKnown ? "registry" : "unknown"}
    };
}

// 同步执行外部 exe（带超时，避免阻塞电源事件回调），不捕获输出；仅退出码 0 视为成功。
static bool sgRunExeSync(const std::wstring& exe, const std::wstring& args, DWORD timeoutMs = 4000) {
    if (!fspath::exists(exe)) return false;
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        CloseHandle(job);
        return false;
    }
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si{sizeof(si)}; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
        CloseHandle(job);
        return false;
    }
    if (!AssignProcessToJobObject(job, pi.hProcess)) {
        TerminateProcess(pi.hProcess, ERROR_ACCESS_DENIED);
        WaitForSingleObject(pi.hProcess, 1000);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseHandle(job);
        return false;
    }
    ResumeThread(pi.hThread);
    DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (wait == WAIT_TIMEOUT) {
        // 不能让“已判定失败”的 TDP 子进程稍后继续改硬件，超时必须终止并回收。
        TerminateJobObject(job, ERROR_TIMEOUT);
        WaitForSingleObject(pi.hProcess, 1000);
    } else {
        // 根进程正常退出后也回收仍留在 Job 内的后代，避免 bat/rundll32 残留。
        TerminateJobObject(job, ERROR_SUCCESS);
    }
    DWORD exitCode = STILL_ACTIVE;
    bool ok = wait == WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode == 0;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseHandle(job);
    return ok;
}

// 同步运行命令并捕获 stdout（CREATE_NO_WINDOW），stderr 合并到 stdout 避免管道死锁。
// 供 smt.get / smt.set 调用（读/写 BCD 的 bcdedit）。
struct RunOut { std::string out; DWORD exitCode = 0; bool ran = false; };
static RunOut runCapture(const std::wstring& cmdLine, DWORD timeoutMs = 10000) {
    RunOut res;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE hOutR = nullptr, hOutW = nullptr;
    if (!CreatePipe(&hOutR, &hOutW, &sa, 0)) return res;
    SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) { CloseHandle(hOutR); CloseHandle(hOutW); return res; }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        CloseHandle(job); CloseHandle(hOutR); CloseHandle(hOutW); return res;
    }

    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hOutW;
    si.hStdError = hOutW; // 合并 stderr→stdout，单行读取无死锁
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back(0);
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
        CloseHandle(job); CloseHandle(hOutR); CloseHandle(hOutW);
        return res;
    }
    if (!AssignProcessToJobObject(job, pi.hProcess)) {
        TerminateProcess(pi.hProcess, ERROR_ACCESS_DENIED);
        WaitForSingleObject(pi.hProcess, 1000);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        CloseHandle(job); CloseHandle(hOutR); CloseHandle(hOutW);
        return res;
    }
    ResumeThread(pi.hThread);
    CloseHandle(hOutW);

    constexpr size_t MAX_CAPTURE_BYTES = 8u << 20;
    bool timedOut = false;
    bool overflow = false;
    bool processExited = false;
    const ULONGLONG deadline = GetTickCount64() + (std::max)(DWORD{100}, timeoutMs);
    auto drainAvailable = [&]() {
        for (;;) {
            DWORD available = 0;
            if (!PeekNamedPipe(hOutR, nullptr, 0, nullptr, &available, nullptr) || available == 0) break;
            char chunk[4096];
            DWORD read = 0;
            const DWORD wanted = (std::min)(available, static_cast<DWORD>(sizeof(chunk)));
            if (!ReadFile(hOutR, chunk, wanted, &read, nullptr) || read == 0) break;
            const size_t room = res.out.size() < MAX_CAPTURE_BYTES ? MAX_CAPTURE_BYTES - res.out.size() : 0;
            if (room) res.out.append(chunk, (std::min)(room, static_cast<size_t>(read)));
            if (static_cast<size_t>(read) > room) { overflow = true; break; }
        }
    };

    for (;;) {
        drainAvailable();
        if (overflow) break;
        const DWORD wait = WaitForSingleObject(pi.hProcess, 20);
        if (wait == WAIT_OBJECT_0) { processExited = true; break; }
        if (wait == WAIT_FAILED || GetTickCount64() >= deadline) { timedOut = true; break; }
    }

    // 即使根进程已退出，也终止仍持有管道/硬件句柄的后代，保证函数必然收敛。
    TerminateJobObject(job, timedOut ? ERROR_TIMEOUT : (overflow ? ERROR_BUFFER_OVERFLOW : ERROR_SUCCESS));
    WaitForSingleObject(pi.hProcess, 1000);
    drainAvailable();
    if (processExited && !timedOut && !overflow) GetExitCodeProcess(pi.hProcess, &res.exitCode);
    else res.exitCode = timedOut ? ERROR_TIMEOUT : ERROR_BUFFER_OVERFLOW;
    CloseHandle(hOutR);
    CloseHandle(job);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    res.ran = true;
    return res;
}
// 进程内实时检测 SMT：GetLogicalProcessorInformation 统计物理核与逻辑处理器数，
// 逻辑 > 物理 ⇒ 超线程开启。零 shell、零中文解码、免提权、毫秒级。
struct SmtLive { int phys = 0; int logic = 0; };
static SmtLive detectSmtLive() {
    SmtLive lv;
    DWORD len = 0;
    // 第一次探测：传 NULL 缓冲，预期返回 FALSE、SetLastError=ERROR_INSUFFICIENT_BUFFER，
    // 同时通过 len 写出所需字节数。注意返回值本身是 FALSE，不能用"&& len>0"判断成功。
    if (!GetLogicalProcessorInformation(nullptr, &len) &&
        GetLastError() == ERROR_INSUFFICIENT_BUFFER && len > 0) {
        std::vector<BYTE> buf(len);
        auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(buf.data());
        if (GetLogicalProcessorInformation(info, &len)) {
            DWORD n = len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
            for (DWORD i = 0; i < n; i++) {
                if (info[i].Relationship == RelationProcessorCore) {
                    lv.phys++;
                    ULONG_PTR m = info[i].ProcessorMask;
                    while (m) { lv.logic += (m & 1); m >>= 1; }
                }
            }
        }
    }
    return lv;
}

// Windows hybrid-CPU detection.  EfficiencyClass is the only OS-reported
// signal that distinguishes processor classes; core count, SMT and CCD/L3
// topology cannot do that reliably on AMD or multi-socket systems.
struct CoreArchitecture {
    bool detected = false;
    bool heterogeneous = false;
    std::vector<int> classes;
    std::string source;
    int logical = 0;
    int physical = 0;
};

static void sortUniqueClasses(std::vector<int>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

static CoreArchitecture detectCoreArchitecture() {
    CoreArchitecture result;
    std::vector<int> cpuSetClasses;
    ULONG len = 0;
    if (!GetSystemCpuSetInformation(nullptr, 0, &len, nullptr, 0) &&
        GetLastError() == ERROR_INSUFFICIENT_BUFFER && len > 0) {
        std::vector<BYTE> buf(len);
        ULONG returned = 0;
        if (GetSystemCpuSetInformation(
                reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buf.data()),
                len, &returned, nullptr, 0)) {
            ULONG offset = 0;
            while (offset + sizeof(SYSTEM_CPU_SET_INFORMATION) <= returned) {
                auto* info = reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buf.data() + offset);
                if (info->Size < sizeof(SYSTEM_CPU_SET_INFORMATION) ||
                    offset + info->Size > returned) break;
                if (info->Type == CpuSetInformation) {
                    cpuSetClasses.push_back(static_cast<int>(info->CpuSet.EfficiencyClass));
                    result.logical++;
                }
                offset += info->Size;
            }
        }
    }
    sortUniqueClasses(cpuSetClasses);

    // EfficiencyClass=0 for every CPU is a valid uniform result, but it is
    // also what older Windows builds may expose.  Use the documented
    // processor relationship API as a compatibility fallback in that case.
    std::vector<int> relationshipClasses;
    DWORD pLen = 0;
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &pLen) &&
        GetLastError() == ERROR_INSUFFICIENT_BUFFER && pLen > 0) {
        std::vector<BYTE> pBuf(pLen);
        if (GetLogicalProcessorInformationEx(
                RelationProcessorCore,
                reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(pBuf.data()),
                &pLen)) {
            BYTE* ptr = pBuf.data();
            BYTE* end = pBuf.data() + pLen;
            while (ptr < end) {
                auto* rec = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(ptr);
                if (rec->Size == 0 || ptr + rec->Size > end) break;
                if (rec->Relationship == RelationProcessorCore) {
                    relationshipClasses.push_back(static_cast<int>(rec->Processor.EfficiencyClass));
                    result.physical++;
                    for (WORD g = 0; g < rec->Processor.GroupCount; g++) {
                        ULONG_PTR mask = static_cast<ULONG_PTR>(rec->Processor.GroupMask[g].Mask);
                        while (mask) { result.logical += (mask & 1) ? 1 : 0; mask >>= 1; }
                    }
                }
                ptr += rec->Size;
            }
        }
    }
    sortUniqueClasses(relationshipClasses);

    const bool cpuSetUseful = !cpuSetClasses.empty() &&
        !(cpuSetClasses.size() == 1 && cpuSetClasses.front() == 0);
    if (cpuSetUseful || relationshipClasses.empty()) {
        result.classes = cpuSetClasses;
        result.source = cpuSetClasses.empty() ? "none" : "cpu-set";
    } else {
        result.classes = relationshipClasses;
        result.source = relationshipClasses.empty() ? "none" : "processor-relationship";
        result.logical = 0;
    }
    result.detected = !result.classes.empty();
    result.heterogeneous = result.classes.size() >= 2;
    if (result.logical == 0 && !cpuSetClasses.empty()) result.logical = static_cast<int>(cpuSetClasses.size());
    return result;
}

// ================================================================
//  CCD 核心控制：L3 域拓扑检测 + 全局进程亲和性（UXTU 同款思路）
// ================================================================

static int popcountPtr(ULONG_PTR m) {
    int c = 0;
    while (m) { c++; m &= (m - 1); }
    return c;
}

static std::string toHexU(ULONG_PTR v) {
    if (v == 0) return "0";
    static const char* hex = "0123456789ABCDEF";
    std::string s;
    while (v) { s.push_back(hex[v & 0xF]); v >>= 4; }
    std::reverse(s.begin(), s.end());
    return s;
}

struct CcdTopology {
    int logical = 0;
    int l3Domains = 0;
    int physicalCores = 0;
    std::vector<std::string> ccdMasks;
};

static CcdTopology detectCcdTopology() {
    CcdTopology t;
    DWORD len = 0;
    if ((GetLogicalProcessorInformationEx(RelationCache, nullptr, &len) ||
         GetLastError() != ERROR_INSUFFICIENT_BUFFER) || len == 0) {
        // 回退：只用 SMT 检测的逻辑核数
        SmtLive lv = detectSmtLive();
        t.logical = lv.logic;
        return t;
    }
    std::vector<BYTE> buf(len);
    auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data());
    if (!GetLogicalProcessorInformationEx(RelationCache, info, &len)) {
        SmtLive lv = detectSmtLive();
        t.logical = lv.logic;
        return t;
    }
    std::vector<ULONG_PTR> masks;
    int l3Records = 0;
    BYTE* ptr = buf.data();
    BYTE* end = buf.data() + len;
    while (ptr < end) {
        auto* rec = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(ptr);
        if (rec->Relationship == RelationCache) {
            // 注意：SDK 10.0.26100 的 CACHE_RELATIONSHIP 用 GroupMasks[]（无 Processor 成员），
            // 必须用 rec->Cache.GroupMasks，不能用 rec->Processor.GroupMask（那是缓存记录里不存在的联合体成员）。
            auto& c = rec->Cache;
            if (c.Level == 3) {              // L3 即一个 CCD 的缓存域
                l3Records++;
                for (WORD g = 0; g < c.GroupCount; g++) {
                    ULONG_PTR m = (ULONG_PTR)c.GroupMasks[g].Mask;
                    if (m) masks.push_back(m);
                }
            }
        }
        ptr += rec->Size;
    }
    // 去重（同一 L3 域可能跨多个 group 重复出现）
    std::sort(masks.begin(), masks.end());
    masks.erase(std::unique(masks.begin(), masks.end()), masks.end());
    // 兜底：掩码为空但确有 >=2 个 L3 域（少数 AMD 把 GroupMask 报空），按逻辑核数均分
    if (masks.empty() && l3Records >= 2) {
        DWORD total = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        if (total > 0 && total <= 64) {
            ULONG_PTR full = ((ULONG_PTR)1 << total) - 1;
            int per = (int)(total / l3Records);
            for (int i = 0; i < l3Records; i++) {
                ULONG_PTR lo = (ULONG_PTR)i * per;
                ULONG_PTR hi = (i + 1 == l3Records) ? (ULONG_PTR)total : (ULONG_PTR)(i + 1) * per;
                ULONG_PTR m = full & (((ULONG_PTR)1 << hi) - 1) & ~(((ULONG_PTR)1 << lo) - 1);
                if (m) masks.push_back(m);
            }
        }
    }
    t.l3Domains = (int)masks.size();
    // 物理核数：读取 RelationProcessor，避免把 SMT 线程误显示为核心。
    DWORD pLen = 0;
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &pLen) &&
        GetLastError() == ERROR_INSUFFICIENT_BUFFER && pLen > 0) {
        std::vector<BYTE> pBuf(pLen);
        if (GetLogicalProcessorInformationEx(RelationProcessorCore,
                reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(pBuf.data()), &pLen)) {
            BYTE* pp = pBuf.data();
            BYTE* pe = pBuf.data() + pLen;
            while (pp < pe) {
                auto* rec = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(pp);
                if (rec->Relationship == RelationProcessorCore) t.physicalCores++;
                pp += rec->Size;
            }
        }
    }
    for (auto m : masks) {
        t.ccdMasks.push_back("0x" + toHexU(m));
        t.logical += popcountPtr(m);
    }
    if (t.logical == 0) {
        SmtLive lv = detectSmtLive();
        t.logical = lv.logic;
    }
    return t;
}

static std::atomic<int>       g_ccdMode{0};
static std::vector<ULONG_PTR> g_ccdMasks;
static std::mutex             g_ccdMutex;
static std::unordered_set<DWORD> g_ccdSeen;
static std::atomic<bool>      g_ccdRunning{false};
static std::atomic<bool>      g_ccdStop{false};
static DWORD                  g_ccdSelfPid = 0;
static std::wstring           g_ccdSelfExe;

static void writeLog(const std::string& level, const std::string& msg);  // 定义在文件后部, 前置声明供 CCD 日志用

static bool ccdSkipProcess(DWORD pid, const std::wstring& exe) {
    if (pid == 0 || pid == g_ccdSelfPid) return true;
    static const wchar_t* skip[] = {
        L"system", L"registry", L"smss.exe", L"csrss.exe", L"wininit.exe",
        L"services.exe", L"lsass.exe", L"winlogon.exe", L"audiodg.exe",
        L"dwm.exe", L"svchost.exe", nullptr
    };
    std::wstring lower = exe;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    for (int i = 0; skip[i]; i++)
        if (lower == skip[i]) return true;
    if (!g_ccdSelfExe.empty() && lower == g_ccdSelfExe) return true;
    return false;
}

// 返回: 0=成功, -1=打开失败(进程已退出/受保护), >0=SetProcessAffinityMask 的错误码
static int ccdApplyToPid(DWORD pid, ULONG_PTR mask) {
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return -1;
    BOOL ok = SetProcessAffinityMask(h, mask);
    DWORD err = ok ? 0 : GetLastError();
    CloseHandle(h);
    return ok ? 0 : (int)err;
}

static void ccdApplyMode(int mode) {
    std::lock_guard<std::mutex> lk(g_ccdMutex);
    if (g_ccdMasks.empty()) return;
    ULONG_PTR mask = 0;
    if (mode == 0) {
        for (auto m : g_ccdMasks) mask |= m;
    } else if (mode >= 1 && mode <= (int)g_ccdMasks.size()) {
        mask = g_ccdMasks[mode - 1];
    } else {
        return;
    }
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{ sizeof(pe) };
    std::vector<DWORD> pids;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (!ccdSkipProcess(pe.th32ProcessID, pe.szExeFile))
                pids.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    int failOpen = 0, failSet = 0;
    for (DWORD pid : pids) {
        int e = ccdApplyToPid(pid, mask);
        if (e < 0) failOpen++;
        else if (e > 0) failSet++;
    }
    g_ccdSeen.clear();
    for (DWORD pid : pids) g_ccdSeen.insert(pid);
    if (failOpen || failSet) {
        char buf[160];
        snprintf(buf, sizeof(buf), "[ccd] mode=%d 应用完成: 打开失败=%d 设置失败=%d", mode, failOpen, failSet);
        writeLog("warn", buf);
    }
}

static void ccdApplyNew(int mode) {
    std::lock_guard<std::mutex> lk(g_ccdMutex);
    if (g_ccdMasks.empty()) return;
    ULONG_PTR mask = 0;
    if (mode == 0) {
        for (auto m : g_ccdMasks) mask |= m;
    } else if (mode >= 1 && mode <= (int)g_ccdMasks.size()) {
        mask = g_ccdMasks[mode - 1];
    } else {
        return;
    }
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{ sizeof(pe) };
    if (Process32FirstW(snap, &pe)) {
        do {
            DWORD pid = pe.th32ProcessID;
            if (ccdSkipProcess(pid, pe.szExeFile)) continue;
            if (g_ccdSeen.count(pid)) continue;
            g_ccdSeen.insert(pid);
            ccdApplyToPid(pid, mask);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

static DWORD WINAPI ccdWorker(LPVOID) {
    int lastMode = -1;
    int fullCount = 0;
    while (!g_ccdStop.load()) {
        int mode = g_ccdMode.load();
        if (mode != lastMode) {
            ccdApplyMode(mode);
            lastMode = mode;
            fullCount = 0;
        } else if (mode != 0) {
            // 每 ~30s 全量刷新一次: 兜住 PID 复用导致的漏应用, 同时限制 g_ccdSeen 无界增长
            if (++fullCount >= 25) {
                ccdApplyMode(mode);
                fullCount = 0;
            } else {
                ccdApplyNew(mode);
            }
        }
        Sleep(1200);
    }
    return 0;
}

static void ccdStartWorker() {
    g_ccdStop.store(false);  // 窗口可能销毁重建(托盘/常驻切换), 允许 worker 重启
    if (g_ccdRunning.exchange(true)) return;
    g_ccdSelfPid = GetCurrentProcessId();
    wchar_t path[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH)) {
        g_ccdSelfExe = std::filesystem::path(path).filename().wstring();
        std::transform(g_ccdSelfExe.begin(), g_ccdSelfExe.end(), g_ccdSelfExe.begin(), ::towlower);
    }
    HANDLE h = CreateThread(nullptr, 0, ccdWorker, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
}

// SMT 开关的真正实现：通过注册表 FeatureSettingsOverride 的 0x40 位（Speculative Store Bypass
// 缓解位）控制超线程。置位 ⇒ 该缓解启用 ⇒ 系统只给每物理核 1 线程 → 真正关闭超线程（N 核 / N 线程）。
// 与 bcdedit numproc 不同，本机制不会按固件枚举顺序砍核，能稳定得到「每核 1 线程」。
// 状态检测：运行态用 detectSmtLive()（logic>phys）；下次启动态读 0x40 位。
// 注意：需管理员写 HKLM；读 HKLM 通常免管理员。修改后需重启生效。
struct SmtReg { bool ok = false; DWORD override = 0; DWORD mask = 0; };
static SmtReg readSmtReg() {
    SmtReg r;
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
            0, KEY_READ | KEY_WOW64_64KEY, &hk) != ERROR_SUCCESS) {
        return r;
    }
    r.ok = true;
    DWORD dw = 0, sz = sizeof(dw);
    if (RegGetValueW(hk, nullptr, L"FeatureSettingsOverride", RRF_RT_REG_DWORD, nullptr, &dw, &sz) == ERROR_SUCCESS)
        r.override = dw;
    sz = sizeof(dw);
    if (RegGetValueW(hk, nullptr, L"FeatureSettingsOverrideMask", RRF_RT_REG_DWORD, nullptr, &dw, &sz) == ERROR_SUCCESS)
        r.mask = dw;
    RegCloseKey(hk);
    return r;
}
static bool writeSmtReg(DWORD override, DWORD mask) {
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
            0, KEY_WRITE | KEY_WOW64_64KEY, &hk) != ERROR_SUCCESS)
        return false;
    bool ok = true;
    if (override == 0 && mask == 0) {
        // 完全恢复系统默认：删除两个值（比留 0 更干净，避免被误判为"已限制"）
        RegDeleteValueW(hk, L"FeatureSettingsOverride");
        RegDeleteValueW(hk, L"FeatureSettingsOverrideMask");
    } else {
        if (RegSetValueExW(hk, L"FeatureSettingsOverride", 0, REG_DWORD,
                (const BYTE*)&override, sizeof(override)) != ERROR_SUCCESS) ok = false;
        if (RegSetValueExW(hk, L"FeatureSettingsOverrideMask", 0, REG_DWORD,
                (const BYTE*)&mask, sizeof(mask)) != ERROR_SUCCESS) ok = false;
    }
    RegCloseKey(hk);
    return ok;
}

// ── 手柄全局快捷调节（后台 XInput 线程处理，不依赖窗口焦点，游戏内全屏也生效） ──
// 手柄 TDP 调节：只转发按键方向给前端（gamepad.tdp-delta），由前端程序统一调节/记录/刷新 UI，
// 前端写入 control-config.json，native 不再直接读取或写入 tdp.txt。
static void nativeAdjustTdp(int delta) {
    if (!g_tdpShortcut) return;
    PostMessageW(g_hwnd, WM_GAMEPAD_TDP_DELTA, (WPARAM)delta, 0);
}
// 手柄锁帧调节：统一转发方向，由程序更新 FPS 帧率上限并负责 RTSS 下发。
static void nativeAdjustBrightness(int dir) {
    if (!g_fpsShortcut) return;
    PostMessageW(g_hwnd, WM_GAMEPAD_BRIGHTNESS, (WPARAM)dir, 0);
}
static const GUID YM_GUID_YEMAN_SCHEME =
    { 0x1cb8b882, 0xa900, 0x4b9f, { 0x9b, 0xac, 0x99, 0xd1, 0x51, 0xe6, 0x44, 0x41 } };
static const GUID YM_GUID_VIDEO_SUBGROUP =
    { 0x7516b95f, 0xf776, 0x4464, { 0x8c, 0x53, 0x06, 0x16, 0x7f, 0x40, 0xcc, 0x99 } };
static const GUID YM_GUID_VIDEO_BRIGHTNESS =
    { 0xaded5e82, 0xb909, 0x4619, { 0x99, 0x49, 0xf5, 0xd7, 0x1d, 0xac, 0x0b, 0xcb } };

static void nativeApplyBrightness(int dir) {
    if (!g_fpsShortcut || !g_hwnd) return;
    GUID* active = nullptr;
    const bool yeman = PowerGetActiveScheme(nullptr, &active) == ERROR_SUCCESS && active &&
        IsEqualGUID(*active, YM_GUID_YEMAN_SCHEME);
    if (active) LocalFree(active);
    SYSTEM_POWER_STATUS sps{};
    const bool dc = GetSystemPowerStatus(&sps) && sps.ACLineStatus == 0;
    if (!yeman) {
        ipc_emit("gamepad.brightness", json{{"ok", false}, {"reason", "not-yeman"}, {"mode", dc ? "dc" : "ac"}});
        return;
    }
    ULONG current = 0;
    DWORD rc = dc
        ? PowerReadDCValueIndex(nullptr, &YM_GUID_YEMAN_SCHEME, &YM_GUID_VIDEO_SUBGROUP,
                                &YM_GUID_VIDEO_BRIGHTNESS, &current)
        : PowerReadACValueIndex(nullptr, &YM_GUID_YEMAN_SCHEME, &YM_GUID_VIDEO_SUBGROUP,
                                &YM_GUID_VIDEO_BRIGHTNESS, &current);
    if (rc != ERROR_SUCCESS) {
        ipc_emit("gamepad.brightness", json{{"ok", false}, {"reason", "unsupported"}, {"mode", dc ? "dc" : "ac"}});
        return;
    }
    const LONG next = std::max<LONG>(0, std::min<LONG>(100, (LONG)current + dir * 5));
    rc = dc
        ? PowerWriteDCValueIndex(nullptr, &YM_GUID_YEMAN_SCHEME, &YM_GUID_VIDEO_SUBGROUP,
                                 &YM_GUID_VIDEO_BRIGHTNESS, (ULONG)next)
        : PowerWriteACValueIndex(nullptr, &YM_GUID_YEMAN_SCHEME, &YM_GUID_VIDEO_SUBGROUP,
                                 &YM_GUID_VIDEO_BRIGHTNESS, (ULONG)next);
    if (rc == ERROR_SUCCESS) rc = PowerSetActiveScheme(nullptr, &YM_GUID_YEMAN_SCHEME);
    ipc_emit("gamepad.brightness", json{{"ok", rc == ERROR_SUCCESS}, {"value", (int)next},
        {"mode", dc ? "dc" : "ac"}, {"reason", rc == ERROR_SUCCESS ? "" : "write-failed"}});
}
// RTSS 锁帧：读/改写 Profiles\Global 的 Limit=，rundll32 重载配置（对齐前端 setRtssLimit）。
static void nativeAdjustRtss(int delta) {
    if (!g_fpsShortcut) return;
    std::wstring g = L"C:\\Program Files (x86)\\RivaTuner Statistics Server\\Profiles\\Global";
    if (!fspath::exists(g)) return;
    std::string txt = sgReadFile(g);
    int cur = 0; bool found = false;
    std::string out;
    size_t pos = 0;
    while (pos <= txt.size()) {
        size_t nl = txt.find('\n', pos);
        std::string line = txt.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("Limit=", 0) == 0) {
            try { cur = std::stoi(line.substr(6)); } catch (...) { cur = 0; }
            found = true;
            int next = clampInt(cur + delta, 20, 200);
            cur = next;
            out += "Limit=" + std::to_string(next) + "\r\n";
        } else {
            out += line;
            if (!line.empty()) out += "\r\n";
        }
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    if (!found) {
        int next = clampInt(60 + delta, 20, 200);
        cur = next;
        out += "Limit=" + std::to_string(next) + "\r\n";
    }
    sgWriteFileAtomic(g, out); // 原子写，避免 RTSS 在游戏内读 Global 时读到半截
    std::wstring dll = L"\"C:\\Program Files (x86)\\RivaTuner Statistics Server\\RTSSHooks64.dll\"";
    std::wstring ru  = L"C:\\Windows\\System32\\rundll32.exe";
    // 外部改完文件后：LoadProfile 重新载入磁盘 → UpdateProfiles 套用到运行中的游戏。
    // ⚠ 不要 SaveProfile：那会让 RTSS 把"内存里的旧状态"写回磁盘，覆盖刚改的内容甚至写坏（损坏根因）。
    sgRunExeSync(ru, dll + L" LoadProfile");
    sgRunExeSync(ru, dll + L" UpdateProfiles");
    ipc_emit("gamepad.refresh", {});
}

// 选择 + B：结束当前游戏（执行 KiLL-EXE.bat）。bat 路径固定，缺失则 no-op。
static void runKillBat() {
    std::wstring bat = L"C:\\SOFT\\YeMan\\PowerControl\\KiLL-EXE.bat";
    if (!fspath::exists(bat)) return;
    sgRunExeSync(L"cmd.exe", L"/c \"" + bat + L"\"");
}

// 选择 + X：呼出 Windows 新触摸键盘（TabTip.exe）。后台进程直接启动，键盘自身为 topmost UI。
static void openTouchKeyboard() {
    std::wstring exe = L"C:\\Program Files\\Common Files\\microsoft shared\\ink\\TabTip.exe";
    if (!fspath::exists(exe)) return;
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

// 选择 + A：内部模拟 Win+D 返回桌面；不启动脚本、不等待、不切回游戏窗口。
static void returnToDesktop() {
    if (g_hwnd && IsWindow(g_hwnd))
        // Use the common hide path so WebView2 visibility, UI lifecycle,
        // focus timers and owned TOPMOST state are released together.
        hideWindowAnimated(g_hwnd);

    INPUT inputs[4] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_LWIN;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'D';
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'D';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_LWIN;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, inputs, sizeof(INPUT));
}

// ── 模拟鼠标统一后端 ─────────────────────────────────────────────
// JoyXoff 继续使用现有脚本；微软方案直接调用 GameInputRedistService 的本地 COM 服务。
// 微软鼠标会在系统上层接管手柄输入，此时野蛮收到“无按键”属于正常状态，不记录为程序错误。
struct __declspec(uuid("BF2915CE-D079-4CDB-A65C-4AD4C56C6D76")) IYeManMouseModeManager : IUnknown {};
struct __declspec(uuid("4020A3D1-EF53-451B-A207-4F1549B5A6FF")) IYeManGameInputServiceClient : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetEnabled(unsigned char enabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsEnabled(unsigned char* enabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetAutoEnabled(unsigned char enabled) = 0;
};
static const CLSID CLSID_YEMAN_GAMEINPUT_SERVICE =
    {0xB773D0F8, 0x01F8, 0x41E7, {0x9F, 0x23, 0x68, 0x71, 0xDA, 0x0A, 0xAC, 0xFF}};
static const std::wstring JOYXOFF_EXE = L"C:\\SOFT\\Joyxoff\\Joyxoff.exe";
static const std::wstring JOYXOFF_VBS = L"C:\\SOFT\\YeMan\\PowerControl\\模拟鼠标.vbs";
static const std::wstring JOYXOFF_BAT = L"C:\\SOFT\\YeMan\\PowerControl\\JoyXoff.bat";

static bool processRunningExact(const wchar_t* exeName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{sizeof(pe)};
    bool running = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) { running = true; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return running;
}

static bool launchJoyxoffToggle() {
    std::wstring cmd;
    if (fspath::exists(JOYXOFF_VBS)) cmd = L"wscript.exe //nologo \"" + JOYXOFF_VBS + L"\"";
    else if (fspath::exists(JOYXOFF_BAT)) cmd = L"cmd.exe /c \"" + JOYXOFF_BAT + L"\"";
    else return false;

    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    const BOOL created = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!created) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

static HRESULT gamebarMouseClient(ComPtr<IYeManGameInputServiceClient>& client) {
    ComPtr<IYeManMouseModeManager> manager;
    HRESULT hr = CoCreateInstance(CLSID_YEMAN_GAMEINPUT_SERVICE, nullptr, CLSCTX_LOCAL_SERVER,
                                  __uuidof(IYeManMouseModeManager),
                                  reinterpret_cast<void**>(manager.GetAddressOf()));
    if (FAILED(hr)) return hr;
    return manager.As(&client);
}

static HRESULT gamebarMouseGet(bool& enabled) {
    enabled = false;
    ComPtr<IYeManGameInputServiceClient> client;
    HRESULT hr = gamebarMouseClient(client);
    if (FAILED(hr)) return hr;
    unsigned char value = 0;
    hr = client->IsEnabled(&value);
    if (SUCCEEDED(hr)) enabled = value != 0;
    return hr;
}

static HRESULT gamebarMouseSet(bool enabled, bool& actual) {
    actual = false;
    ComPtr<IYeManGameInputServiceClient> client;
    HRESULT hr = gamebarMouseClient(client);
    if (FAILED(hr)) return hr;
    hr = client->SetEnabled(enabled ? 1 : 0);
    if (FAILED(hr)) return hr;
    unsigned char value = 0;
    hr = client->IsEnabled(&value);
    if (SUCCEEDED(hr)) actual = value != 0;
    if (SUCCEEDED(hr) && actual != enabled) return E_FAIL;
    return hr;
}

static std::string gamebarMouseError(HRESULT hr) {
    if (hr == REGDB_E_CLASSNOTREG) return "未找到微软鼠标组件，请安装或更新 Xbox Game Bar";
    if (hr == E_NOINTERFACE) return "当前系统的 GameInput 版本不支持微软鼠标";
    char buf[96]{};
    snprintf(buf, sizeof(buf), "微软鼠标接口调用失败（0x%08X）", static_cast<unsigned>(hr));
    return buf;
}

static json mouseModeGetState() {
    const bool joyOn = processRunningExact(L"Joyxoff.exe");
    bool gamebarOn = false;
    const HRESULT gamebarHr = gamebarMouseGet(gamebarOn);
    return {
        {"ok", true},
        {"backend", g_mouseBackend},
        {"on", joyOn || gamebarOn},
        {"joyxoffOn", joyOn},
        {"gamebarOn", gamebarOn},
        {"joyxoffAvailable", fspath::exists(JOYXOFF_EXE)},
        {"gamebarAvailable", SUCCEEDED(gamebarHr)}
    };
}

static json mouseModeSetBackend(const std::string& backend) {
    if (backend != "joyxoff" && backend != "gamebar")
        return {{"ok", false}, {"backend", g_mouseBackend}, {"error", "未知模拟鼠标方案"}};
    if (backend == "joyxoff" && !fspath::exists(JOYXOFF_EXE))
        return {{"ok", false}, {"backend", g_mouseBackend}, {"error", "未找到 C:\\SOFT\\Joyxoff\\Joyxoff.exe"}};
    if (backend == "gamebar") {
        bool enabled = false;
        const HRESULT hr = gamebarMouseGet(enabled);
        if (FAILED(hr)) return {{"ok", false}, {"backend", g_mouseBackend}, {"error", gamebarMouseError(hr)}};
    }
    g_mouseBackend = backend;
    summonSave();
    json state = mouseModeGetState();
    state["backend"] = g_mouseBackend;
    return state;
}

static json mouseModeToggleState() {
    if (g_mouseBackend == "gamebar") {
        bool current = false;
        HRESULT hr = gamebarMouseGet(current);
        if (FAILED(hr)) return {{"ok", false}, {"backend", g_mouseBackend}, {"on", false}, {"error", gamebarMouseError(hr)}};
        if (!current && processRunningExact(L"Joyxoff.exe")) launchJoyxoffToggle();
        bool actual = current;
        hr = gamebarMouseSet(!current, actual);
        if (FAILED(hr)) return {{"ok", false}, {"backend", g_mouseBackend}, {"on", current}, {"error", gamebarMouseError(hr)}};
        return {{"ok", true}, {"backend", g_mouseBackend}, {"on", actual}};
    }

    if (!fspath::exists(JOYXOFF_EXE))
        return {{"ok", false}, {"backend", g_mouseBackend}, {"on", false}, {"error", "未找到 C:\\SOFT\\Joyxoff\\Joyxoff.exe"}};
    const bool current = processRunningExact(L"Joyxoff.exe");
    if (!current) {
        bool gamebarOn = false;
        if (SUCCEEDED(gamebarMouseGet(gamebarOn)) && gamebarOn) {
            bool ignored = false;
            gamebarMouseSet(false, ignored);
        }
    }
    if (!launchJoyxoffToggle())
        return {{"ok", false}, {"backend", g_mouseBackend}, {"on", current}, {"error", "JoyXoff 启动脚本不可用"}};
    return {{"ok", true}, {"backend", g_mouseBackend}, {"on", !current}};
}

// 选择 + Y：按设置页保存的方案切换。成功/失败都回传结构化状态；
// 微软鼠标开启后手柄组合键被系统接管，属于预期行为，不额外报错或重试。
static void toggleMouseMode() {
    ipc_emit("gamepad.mouse-toggle", mouseModeToggleState());
}

// 系统内置黑名单（基名小写，不含 .exe）。WebView2 进程也排除，保护壳自身。
static const wchar_t* SG_BLACKLIST[] = {
    L"csrss", L"winlogon", L"lsass", L"services", L"smss", L"system", L"idle",
    L"dwm", L"explorer", L"msedgewebview2", L"searchhost", L"fontdrvhost",
    L"sihost", L"taskhostw", L"dwm", L"audiodg", L"nvcontainer", L"nvdisplay",
    L"rundll32", L"conhost", L"systemsettings", L"shellhost", L"startmenuexperiencehost",
    L"vmware-vmx", L"virtualboxvm", L"qemu-system-*",
    L"vmmem", L"vmmemwsl", L"wslhost", L"vmcompute"
};
static std::vector<std::wstring> sgExcludes() {
    std::vector<std::wstring> ex;
    // 内置黑名单
    for (const wchar_t* b : SG_BLACKLIST) ex.push_back(std::wstring(b));
    // 自身
    ex.push_back(sgSelfBase());
    // exclude.txt（UTF-8，# 注释，空行忽略）
    std::wstring ep = SG_DIR + L"\\exclude.txt";
    std::string txt = sgReadFile(ep);
    std::istringstream iss(txt); std::string line;
    while (std::getline(iss, line)) {
        // 去掉行内注释
        auto h = line.find('#'); if (h != std::string::npos) line = line.substr(0, h);
        // 去首尾空白
        size_t a = line.find_first_not_of(" \t\r\n"); if (a == std::string::npos) continue;
        size_t b = line.find_last_not_of(" \t\r\n"); line = line.substr(a, b - a + 1);
        if (line.empty()) continue;
        std::wstring w = sgBaseName(U2W(line));
        if (!w.empty()) ex.push_back(w);
    }
    return ex;
}

static void sgKillListedProcesses() {
    const std::string txt = sgReadFile(SG_KILL_LIST);
    if (txt.empty()) return;
    std::vector<std::string> names;
    std::istringstream iss(txt);
    std::string line;
    while (std::getline(iss, line)) {
        auto h = line.find('#');
        if (h != std::string::npos) line = line.substr(0, h);
        size_t a = line.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        size_t b = line.find_last_not_of(" \t\r\n");
        line = line.substr(a, b - a + 1);
        if (!line.empty()) names.push_back(line);
    }
    if (names.empty()) return;
    const DWORD selfPid = GetCurrentProcessId();
    const std::wstring selfBase = sgSelfBase();
    if (!g_sgSessionValid) return;
    auto excludes = sgExcludes();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{ sizeof(pe) };
    if (Process32FirstW(snap, &pe)) {
        do {
            std::wstring base = sgBaseName(pe.szExeFile);
            if (pe.th32ProcessID == 0 || pe.th32ProcessID == 4 ||
                pe.th32ProcessID == selfPid || base == selfBase) {
                continue;
            }
            bool excluded = false;
            excluded = sgNameExcludedBy(base, excludes);
            if (excluded) continue;
            DWORD psid = 0;
            if (!ProcessIdToSessionId(pe.th32ProcessID, &psid) || psid != g_sgSessionId) {
                continue;
            }
            if (acNameMatch(base, names)) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (h) { TerminateProcess(h, 0); CloseHandle(h); }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

static bool sgEnsureMarkerDir(const std::wstring& dir) {
    std::error_code ec;
    fspath::create_directories(fspath::path(dir), ec);
    return !ec && fspath::exists(fspath::path(dir));
}

// 前端识别结果可能已缓存；暂停执行前重新选取候选 PID，避免用户看到的
// 游戏已经变化时仍按旧 PID 操作。这里不读取路径、名称或创建时间作验证。
static bool sgValidatePauseTarget(DWORD rootPid, std::string* error = nullptr) {
    const auto selected = nativeDetectGame();
    if (!selected.pid) {
        if (error) *error = "no eligible game process";
        return false;
    }
    if (selected.pid != rootPid) {
        if (error) *error = "game target changed; selected PID is no longer the largest eligible process";
        return false;
    }

    return true;
}

static bool sgWriteProcessMarker(const std::wstring& dir, DWORD pid, const char* state) {
    if (!sgEnsureMarkerDir(dir)) return false;
    const std::wstring markerPath = dir + L"\\" + std::to_wstring(pid) + L".txt";
    std::string body = "pid=" + std::to_string(pid) +
        "|epoch=" + std::to_string(sgNowEpoch()) + "|state=" + state;
    return sgWriteFileAtomic(markerPath, body);
}

// 入睡前冻结结果（写进日志，便于跨机确认游戏到底有没有被冻）
struct SgSuspendResult { std::string name; DWORD pid = 0; uint64_t ws = 0; bool frozen = false; };
// 唤醒后恢复结果
struct SgResumeResult  { int count = 0; std::string names; };

// 阶段1 入睡前：写标记(先于冻结，保证崩溃可恢复) → 冻结最大工作集进程
static json sgSuspendGameByPidUnlocked(DWORD rootPid, const std::wstring& markerDir);
static SgResumeResult sgResumeMarkedDirectoryUnlocked(const std::wstring& dir);
static int sgResumeEligibleProcessesFallback();

static SgSuspendResult sgSuspendTarget(unsigned long long generation) {
    std::lock_guard<std::mutex> opLock(g_sgOpMtx);
    SgSuspendResult r;
    try {
        sgInitNt();
        if (!powerLifecycleMatches(PowerLifecycle::Suspending, generation)) return r;
        if (g_sgKillListEnabled) sgKillListedProcesses();
        if (!powerLifecycleMatches(PowerLifecycle::Suspending, generation)) return r;
        // 睡眠和手动暂停共用同一根 PID 规则：只选唯一最大候选进程，
        // 实际操作由 sgSuspendGameByPidUnlocked 再次校验后只冻结该 PID。
        const auto game = nativeDetectGame();
        r.name = W2U(game.name); r.pid = game.pid; r.ws = game.workingSet;
        // 写"目标"展示文件（仅展示，不参与恢复）
        sgWriteFile(SG_DIR + L"\\target.txt",
            "name=" + r.name + "|pid=" + std::to_string(r.pid) +
            "|epoch=" + std::to_string(sgNowEpoch()));
        if (g_sgPauseResume && r.pid != 0 && r.ws >= SG_MIN_WS &&
            powerLifecycleMatches(PowerLifecycle::Suspending, generation)) {
            const json result = sgSuspendGameByPidUnlocked(r.pid, SG_DIR + L"\\suspended");
            r.frozen = result.value("paused", false);
            return r;
        }
    } catch (...) {}
    return r;
}

// 睡眠守护测试入口也只接受根 PID；未传入时复用 native 游戏识别结果。
static json sgSuspendCurrent(DWORD rootPid = 0) {
    std::lock_guard<std::mutex> opLock(g_sgOpMtx);
    try {
        sgInitNt();
        if (!rootPid) rootPid = nativeDetectGame().pid;
        return sgSuspendGameByPidUnlocked(rootPid, SG_DIR + L"\\suspended");
    } catch (...) {}
    return json{{"paused", false}, {"rootPid", static_cast<int>(rootPid)}};
}

static json sgSuspendGameByPidUnlocked(DWORD rootPid, const std::wstring& markerDir) {
    json out = {
        {"paused", false}, {"rootPid", static_cast<int>(rootPid)},
        {"pids", json::array()}, {"processes", json::array()},
        {"failedPids", json::array()},
        {"okCount", 0}, {"failCount", 0}
    };
    try {
        sgInitNt();
        DWORD currentSession = 0;
        DWORD rootSession = 0;
        if (!rootPid || rootPid == 4 || rootPid == GetCurrentProcessId() ||
            !fnNtSuspend ||
            !ProcessIdToSessionId(GetCurrentProcessId(), &currentSession) ||
            !ProcessIdToSessionId(rootPid, &rootSession) || rootSession != currentSession) {
            return out;
        }

        // 强制单 PID：不能递归子进程树。WebView2、启动器及其它子进程即使
        // 工作集很大，也必须继续由排除清单保护，绝不随游戏根进程一起冻结。
        const std::vector<DWORD> targets{rootPid};
        bool rootPaused = false;
        for (const DWORD pid : targets) {
            const std::wstring marker = markerDir.empty()
                ? std::wstring{}
                : markerDir + L"\\" + std::to_wstring(pid) + L".txt";
            const std::wstring otherMarkerDir = markerDir == SG_MANUAL_DIR
                ? SG_DIR + L"\\suspended"
                : SG_MANUAL_DIR;
            const std::wstring otherMarker = otherMarkerDir + L"\\" + std::to_wstring(pid) + L".txt";
            if (fspath::exists(otherMarker)) {
                out["failedPids"].push_back(static_cast<int>(pid));
                out["error"] = "process already suspended by other guard mode";
                if (pid == rootPid) break;
                continue;
            }
            if (!marker.empty() && fspath::exists(marker)) {
                out["pids"].push_back(static_cast<int>(pid));
                out["processes"].push_back({{"pid", static_cast<int>(pid)}});
                if (pid == rootPid) rootPaused = true;
                continue;
            }
            if (!markerDir.empty() && !sgWriteProcessMarker(markerDir, pid, "pending")) {
                out["failedPids"].push_back(static_cast<int>(pid));
                if (pid == rootPid) break;
                continue;
            }

            HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
            const LONG status = h && fnNtSuspend ? fnNtSuspend(h) : static_cast<LONG>(-1);
            if (h) CloseHandle(h);
            if (status >= 0) {
                if (!markerDir.empty()) sgWriteProcessMarker(markerDir, pid, "suspended");
                out["pids"].push_back(static_cast<int>(pid));
                out["processes"].push_back({{"pid", static_cast<int>(pid)}});
                out["okCount"] = out["okCount"].get<int>() + 1;
                if (pid == rootPid) rootPaused = true;
            } else {
                if (!markerDir.empty()) fspath::remove(marker);
                out["failedPids"].push_back(static_cast<int>(pid));
                if (pid == rootPid) break;
            }
        }
        out["failCount"] = static_cast<int>(out["failedPids"].size());
        out["paused"] = rootPaused;
    } catch (...) {}
    return out;
}

static json sgSuspendGameByPidUnlocked(DWORD rootPid, const std::wstring& markerDir);

static json sgResumeGameByPids(const json& values, const std::wstring& markerDir) {
    json out = {{"resumed", 0}, {"failedPids", json::array()}, {"stalePids", json::array()}};
    try {
        sgInitNt();
        if (!fnNtResume || !values.is_array()) return out;
        if (values.empty()) {
            const SgResumeResult fallback = sgResumeMarkedDirectoryUnlocked(markerDir);
            out["resumed"] = fallback.count;
            if (fallback.count == 0)
                out["resumed"] = sgResumeEligibleProcessesFallback();
            return out;
        }
        for (const auto& value : values) {
            const DWORD pid = value.is_number_unsigned()
                ? value.get<DWORD>()
                : value.is_number_integer()
                    ? static_cast<DWORD>(value.get<int>())
                    : value.is_object() ? value.value("pid", 0u) : 0u;
            if (!pid || pid == 4 || pid == GetCurrentProcessId()) continue;
            HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
            const bool resumed = h && fnNtResume(h) >= 0;
            if (h) CloseHandle(h);
            if (resumed) {
                if (!markerDir.empty())
                    fspath::remove(markerDir + L"\\" + std::to_wstring(pid) + L".txt");
                out["resumed"] = out["resumed"].get<int>() + 1;
            } else if (!h) {
                if (!markerDir.empty())
                    fspath::remove(markerDir + L"\\" + std::to_wstring(pid) + L".txt");
            } else {
                out["failedPids"].push_back(static_cast<int>(pid));
            }
        }
        // 记录 PID 全部不存在或没有任何一个恢复成功时，按现有游戏筛选规则
        // 进行最大恢复兜底；兜底仍然只向 OpenProcess 传 PID，不读取地址。
        if (out["resumed"].get<int>() == 0)
            out["resumed"] = sgResumeEligibleProcessesFallback();
    } catch (...) {}
    return out;
}

static json sgSuspendGameByPid(DWORD rootPid) {
    std::lock_guard<std::mutex> opLock(g_sgOpMtx);
    // 前端 game.detect 结果可能已缓存 5 秒；执行前重新选取最大候选。
    std::string validationError;
    if (!sgValidatePauseTarget(rootPid, &validationError)) {
        return json{
            {"paused", false},
            {"rootPid", static_cast<int>(rootPid)},
            {"pids", json::array()},
            {"processes", json::array()},
            {"failedPids", json::array()},
            {"okCount", 0},
            {"failCount", 1},
            {"error", validationError}
        };
    }
    return sgSuspendGameByPidUnlocked(rootPid, SG_MANUAL_DIR);
}

static SgResumeResult sgResumeMarkedDirectoryUnlocked(const std::wstring& dir) {
    SgResumeResult rr;
    try {
        sgInitNt();
        if (!fspath::exists(dir)) return rr;
        for (auto& e : fspath::directory_iterator(dir)) {
            if (!e.is_regular_file()) continue;
            std::wstring fn = e.path().filename().wstring();
            // 解析 <pid>.txt 中的数字 PID
            std::wstring digits;
            for (wchar_t c : fn) { if (c >= L'0' && c <= L'9') digits.push_back(c); else break; }
            if (digits.empty()) { fspath::remove(e.path()); continue; }
            DWORD pid = (DWORD)_wtol(digits.c_str());
            if (pid == 0) { fspath::remove(e.path()); continue; }
            HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
            if (!h) { fspath::remove(e.path()); continue; } // 进程已不在 → 清标记
            bool resumed = false;
            if (fnNtResume) resumed = fnNtResume(h) >= 0;
            CloseHandle(h);
            if (resumed) {
                fspath::remove(e.path()); // 仅确认恢复成功才删标记
                rr.count++;
            } else {
                rr.count = rr.count;
            }
        }
    } catch (...) {}
    return rr;
}

static int sgResumeEligibleProcessesFallback() {
    int resumed = 0;
    try {
        sgInitNt();
        if (!fnNtResume) return 0;
        DWORD currentSession = 0;
        if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSession)) return 0;
        const auto userExcludes = sgExcludes();
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;
        PROCESSENTRY32W pe{sizeof(pe)};
        if (Process32FirstW(snap, &pe)) {
            do {
                const DWORD pid = pe.th32ProcessID;
                const std::wstring name = sgBaseName(pe.szExeFile);
                if (!pid || pid == 4 || pid == GetCurrentProcessId() ||
                    nativeMonitorExcluded(name) || sgNameExcludedBy(name, userExcludes)) {
                    continue;
                }
                DWORD processSession = 0;
                if (!ProcessIdToSessionId(pid, &processSession) || processSession != currentSession)
                    continue;
                HANDLE query = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
                if (!query) continue;
                PROCESS_MEMORY_COUNTERS_EX memory{};
                memory.cb = sizeof(memory);
                const bool memoryOk = GetProcessMemoryInfo(
                    query, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) != FALSE;
                CloseHandle(query);
                if (!memoryOk || memory.WorkingSetSize < static_cast<SIZE_T>(SG_MIN_WS)) continue;
                HANDLE target = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
                if (target && fnNtResume(target) >= 0) ++resumed;
                if (target) CloseHandle(target);
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    } catch (...) {}
    return resumed;
}

static SgResumeResult sgResumeAll(bool allowEligibleFallback = false) {
    std::lock_guard<std::mutex> opLock(g_sgOpMtx);
    SgResumeResult result = sgResumeMarkedDirectoryUnlocked(SG_DIR + L"\\suspended");
    if (allowEligibleFallback && result.count == 0)
        result.count = sgResumeEligibleProcessesFallback();
    return result;
}

static SgResumeResult sgResumeManualAll(bool allowEligibleFallback = false) {
    std::lock_guard<std::mutex> opLock(g_sgOpMtx);
    SgResumeResult result = sgResumeMarkedDirectoryUnlocked(SG_MANUAL_DIR);
    if (allowEligibleFallback && result.count == 0)
        result.count = sgResumeEligibleProcessesFallback();
    return result;
}

// 启动和正常退出需要同时处理睡眠、手动两套 PID 标记。只有两套记录都没有
// 恢复成功时才执行一次高内存游戏兜底，避免分别扫描两次并重复 Resume 同一 PID。
static SgResumeResult sgResumeTrackedAll(bool allowEligibleFallback = false) {
    std::lock_guard<std::mutex> opLock(g_sgOpMtx);
    SgResumeResult result = sgResumeMarkedDirectoryUnlocked(SG_DIR + L"\\suspended");
    const SgResumeResult manual = sgResumeMarkedDirectoryUnlocked(SG_MANUAL_DIR);
    result.count += manual.count;
    if (allowEligibleFallback && result.count == 0)
        result.count = sgResumeEligibleProcessesFallback();
    return result;
}

static std::thread g_startupResumeThread;

static void joinStartupResumeThread() {
    if (g_startupResumeThread.joinable()) g_startupResumeThread.join();
}

static void startStartupResumeThread() {
    joinStartupResumeThread();
    g_startupResumeThread = std::thread([] {
        // PID 记录缺失、失效或恢复数为 0 时，也要尽最大可能解除遗留挂起。
        sgResumeTrackedAll(true);
    });
}

static bool sgHasMarkerFiles(const std::wstring& dir) {
    std::error_code ec;
    if (!fspath::exists(dir, ec) || ec) return false;
    for (auto& e : fspath::directory_iterator(dir, ec)) {
        if (e.is_regular_file()) return true;
    }
    return false;
}

static void sgCleanupBeforeExit() {
    if (g_sgCleanupDone) return;
    g_sgCleanupDone = true;
    // 正常退出必须解除所有由本程序挂起的进程。
    SgResumeResult rr = sgResumeTrackedAll(true);
    (void)rr;
    // 若仍有标记，说明恢复失败；允许 WM_DESTROY/消息循环退出再重试一次。
    const bool pending = sgHasMarkerFiles(SG_DIR + L"\\suspended") ||
        sgHasMarkerFiles(SG_MANUAL_DIR);
    if (pending) g_sgCleanupDone = false;
    g_sgInSuspend = false;
}

// ── 唤醒处置：恢复本周期资源；仅自动/代理唤醒进入重睡观察 ──
static void sgRealWake(const char* src) {
    const bool hadSuspend = g_sgInSuspend;
    if (hadSuspend) {
        // 恢复全部标记：即使用户在睡眠周期中关闭了开关，也不能把已冻结游戏留住。
        SgResumeResult rr = sgResumeAll(true);
        (void)rr;
        g_sgInSuspend = false;
    }

    // S3 的 RESUMEAUTOMATIC 和 S0 的显示器亮起都只能作为“候选”信号；
    // RESUMESUSPEND 代表用户主动唤醒，必须取消此前已经启动的观察窗口，
    // 即使 RESUMEAUTOMATIC 已经先把 g_sgInSuspend 清零。
    if (strcmp(src, "resume_suspend") == 0) {
        sgStopResleepObservation();
        return;
    }
    if (!hadSuspend) return;
    if (g_sgResleepEnabled &&
        (strcmp(src, "resume_auto") == 0 || strcmp(src, "monitor_on") == 0)) {
        sgStartResleepObservation();
    } else {
        sgStopResleepObservation();
    }
}

static void sgQueueWork(SgWork work, unsigned long long generation) {
    std::lock_guard<std::mutex> lock(g_sgWorkMx);
    if (g_sgWorkStop) return;
    for (const auto queued : g_sgWorkQ) {
        if (queued.kind == work && queued.generation == generation) return;
    }
    // 电源事件偶尔会成组到达；保持小而有界的队列，且不让旧事件无限堆积。
    if (g_sgWorkQ.size() >= 8) g_sgWorkQ.pop_front();
    g_sgWorkQ.push_back({work, generation});
    g_sgWorkCv.notify_one();
}

static void sgWorkLoop() {
    for (;;) {
        SgWorkItem item{};
        {
            std::unique_lock<std::mutex> lock(g_sgWorkMx);
            g_sgWorkCv.wait(lock, [] { return g_sgWorkStop || !g_sgWorkQ.empty(); });
            if (g_sgWorkStop && g_sgWorkQ.empty()) return;
            item = g_sgWorkQ.front();
            g_sgWorkQ.pop_front();
        }
        try {
            if (item.kind == SgWork::Suspend) {
                // PBT_APMSUSPEND 是系统统一的睡眠入口；这里保持原有睡眠守护行为。
                // 不再调用不存在的 sgIsHibernateAction()，避免 native 包无法编译。
                if (!powerLifecycleMatches(PowerLifecycle::Suspending, item.generation))
                    continue;
                 if (g_guardEnabled && !g_sgInSuspend.load()) {
                    g_sgInSuspend.store(true);
                    const SgSuspendResult result = sgSuspendTarget(item.generation);
                    if (!result.frozen) g_sgInSuspend.store(false);
                }
                // A fast wake can arrive while the suspend worker is still
                // enumerating processes. Never overwrite Resuming with the
                // stale Suspended completion from that older work item.
                if (currentPowerGeneration() == item.generation) {
                    PowerLifecycle expected = PowerLifecycle::Suspending;
                    g_powerLifecycle.compare_exchange_strong(
                        expected, PowerLifecycle::Suspended,
                        std::memory_order_acq_rel, std::memory_order_acquire);
                }
                continue;
            } else if (item.kind == SgWork::WakeAutomatic) {
                if (!powerLifecycleMatches(PowerLifecycle::Resuming, item.generation))
                    continue;
                if (g_sgInSuspend.load()) sgRealWake("resume_auto");
                else sgStopResleepObservation();
                if (powerLifecycleMatches(PowerLifecycle::Resuming, item.generation) &&
                    g_resumeReadyGeneration.exchange(item.generation, std::memory_order_acq_rel) != item.generation && g_hwnd)
                    PostMessageW(g_hwnd, WM_POWER_RESUME_READY, 0, 0);
            } else if (item.kind == SgWork::WakeSuspend) {
                if (currentPowerGeneration() != item.generation) continue;
                // RESUMESUSPEND may follow RESUMEAUTOMATIC after the frontend
                // has already committed. It must still cancel an automatic
                // re-sleep observation, but it must not reopen a completed
                // power transaction.
                if (g_sgInSuspend.load()) sgRealWake("resume_suspend");
                else sgStopResleepObservation();
                if (powerLifecycleMatches(PowerLifecycle::Resuming, item.generation) &&
                    g_resumeReadyGeneration.exchange(item.generation, std::memory_order_acq_rel) != item.generation && g_hwnd)
                    PostMessageW(g_hwnd, WM_POWER_RESUME_READY, 0, 0);
            }
        } catch (...) {
            if (item.kind == SgWork::Suspend) g_sgInSuspend.store(false);
        }
    }
}

static void sgStartWorkThread() {
    std::lock_guard<std::mutex> lock(g_sgWorkMx);
    if (g_sgWorkThread.joinable()) return;
    g_sgWorkStop = false;
    g_sgWorkThread = std::thread(sgWorkLoop);
}

static void sgStopWorkThread() {
    {
        std::lock_guard<std::mutex> lock(g_sgWorkMx);
        g_sgWorkStop = true;
        g_sgWorkQ.clear();
    }
    g_sgWorkCv.notify_all();
    if (g_sgWorkThread.joinable()) g_sgWorkThread.join();
}

// Child windows
struct ChildWindow {
    int id;
    HWND hwnd;
    ComPtr<ICoreWebView2Controller> ctrl;
    ComPtr<ICoreWebView2> view;
};
static std::unordered_map<int, ChildWindow*> g_children;
static int g_nextChildId = 1;

static void releaseChildWebView(ChildWindow* cw) {
    if (!cw) return;
    if (cw->ctrl) cw->ctrl->Close();
    cw->view.Reset();
    cw->ctrl.Reset();
}

// Release every WebView2 controller before the native windows and COM
// apartment are torn down.  This makes the WebView2 child process tree
// observe an orderly shutdown instead of relying on process termination.
static void closeWebViewControllers() {
    while (!g_children.empty()) {
        auto it = g_children.begin();
        ChildWindow* cw = it->second;
        g_children.erase(it);
        if (cw) {
            releaseChildWebView(cw);
            if (cw->hwnd && IsWindow(cw->hwnd)) DestroyWindow(cw->hwnd);
            delete cw;
        }
    }
    if (g_ctrl) g_ctrl->Close();
    g_view.Reset();
    g_ctrl.Reset();
}

static void closeWebViewsForExit() {
    closeWebViewControllers();
    g_env.Reset();
}

// Splash window
static HWND g_splash = nullptr;
static double g_splashAngle = 0.0;
static double g_splashPhase = 0.0;
static LARGE_INTEGER g_splashLastQpc{};
static LARGE_INTEGER g_splashQpcFrequency{};
static constexpr UINT_PTR SPLASH_ANIMATION_TIMER_ID = 1;
static constexpr UINT SPLASH_TIMER_INTERVAL_MS = 8; // 约 120 FPS，减少转动跳帧
static constexpr double SPLASH_ROTATION_DEGREES_PER_SECOND = 420.0;
static ULONG_PTR g_splashGdiplusToken = 0;
static HDC g_splashDc = nullptr;
static HBITMAP g_splashBitmap = nullptr;
static void* g_splashBits = nullptr;
static int g_splashSurfaceW = 0;
static int g_splashSurfaceH = 0;

// Logging
static std::wstring g_logFile;
static std::mutex   g_logMtx;

// Background brush for frameless border area
static HBRUSH   g_bgBrush = nullptr;
static COLORREF g_bgClr   = 0;

// Apply DWM visual attributes for frameless window (border, caption, backdrop).
// Called at creation and by window.setBackgroundColor. Not called on focus
// change — Wails doesn't do it either, and with WebView2 filling the full
// client area the 1px DWM border is barely perceptible anyway.
static void applyFramelessDwmAttrs();
static bool configureAppHost(ICoreWebView2* view);
static void setupWebView(ICoreWebView2Controller* ctrl);
static HRESULT finishCreateController(ICoreWebView2Environment* env, unsigned long long generation = 0);
static void completeWebViewRecovery();
static void failWebViewRecovery(const char* reason);
static void beginWebViewRecovery(const char* kind, const char* action, size_t attempt);
static void scheduleWebViewRecoveryAction(WebViewDeferredRecovery action);
static void resetWebViewRenderState();
static void finalizeWebViewRenderReady(unsigned long long generation, UINT64 navigationId, const char* source);
static void probeWebViewRenderState(unsigned long long generation, UINT64 navigationId);
static void signalUpdateHandshake();
static void armPowerResumeWatchdog(unsigned long long generation, UINT delayMs = POWER_RESUME_WATCHDOG_DELAY_MS);
static void stopPowerResumeWatchdog();
static void nudgeWebViewAfterResume(bool resetVisibility = true);
static void enableWindowTransitions(HWND hwnd);
static void showWindowAnimated(HWND hwnd, int showCmd, bool activate = true);
static void hideWindowAnimated(HWND hwnd);
// Force a taskbar button for this window (WS_EX_APPWINDOW alone is sometimes
// ignored by the shell for WebView2 / frameless hosts — without it the window
// never appears in the taskbar and "minimize" has nowhere to go).
static void ensureTaskbarButton(HWND hwnd);
static bool systemUsesDarkMode();
static COLORREF currentWindowBackgroundColor();
static json systemThemeInfo();
static void applyNativeTheme();
static void closeWebViewsForExit();

// Embedded assets (single-exe mode) — zero-copy: entries point directly into PE section
#ifdef SINGLE_EXE
struct PakEntry { std::string path; const char* data; uint32_t size; };
static std::vector<PakEntry> g_pakEntries;
#endif


static std::wstring g_appDataDir;
static std::wstring app_data_dir() {
    if (!g_appDataDir.empty()) return g_appDataDir;
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) {
        auto title = g_cfg.value("/window/title"_json_pointer, std::string{"QQ"});
        g_appDataDir = std::wstring(p) + L"\\" + safe_path_component(U2W(title), L"QQ");
        CoTaskMemFree(p);
    } else {
        g_appDataDir = exe_dir() + L"\\data";
    }
    fspath::create_directories(g_appDataDir);
    return g_appDataDir;
}

static std::wstring readEnvironmentString(const wchar_t* name) {
    DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) return {};
    std::vector<wchar_t> buffer(static_cast<size_t>(required));
    DWORD written = GetEnvironmentVariableW(name, buffer.data(), required);
    if (written == 0 || written >= required) return {};
    return std::wstring(buffer.data(), written);
}

static WebViewGpuMode configuredWebViewGpuMode() {
    auto value = readEnvironmentString(L"YEMAN_WEBVIEW_GPU");
    if (_wcsicmp(value.c_str(), L"legacy") == 0) return WebViewGpuMode::Legacy;
    if (_wcsicmp(value.c_str(), L"software") == 0) return WebViewGpuMode::Software;
    return WebViewGpuMode::Default;
}

static const char* webViewGpuModeName(WebViewGpuMode mode) {
    switch (mode) {
    case WebViewGpuMode::Legacy: return "legacy";
    case WebViewGpuMode::Software: return "software";
    case WebViewGpuMode::Default: return "default";
    }
    return "default";
}

static std::wstring webview_data_dir() {
    if (g_webviewDataDirInitialized) return g_webviewDataDir;
    g_webviewDataDirInitialized = true;
    auto configured = readEnvironmentString(L"YEMAN_WEBVIEW_DATA_DIR");
    if (!configured.empty()) {
        if (configured.size() >= 2 && configured.front() == L'"' && configured.back() == L'"')
            configured = configured.substr(1, configured.size() - 2);
        std::error_code ec;
        fspath::path path(configured);
        if (path.is_relative()) path = fspath::path(exe_dir()) / path;
        fspath::create_directories(path, ec);
        if (!ec) {
            g_webviewDataDir = path.wstring();
            return g_webviewDataDir;
        }
    }
    g_webviewDataDir = app_data_dir();
    return g_webviewDataDir;
}

static fspath::path webview_profile_dir() {
    return fspath::path(webview_data_dir()) / L"EBWebView";
}

static fspath::path webview_failure_marker_path() {
    return webview_profile_dir() / L"yeman-browser-failure.json";
}

static std::string webViewLocalTimestamp(bool compact) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char value[40]{};
    if (compact) {
        snprintf(value, sizeof(value), "%04u%02u%02u-%02u%02u%02u",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    } else {
        snprintf(value, sizeof(value), "%04u-%02u-%02uT%02u:%02u:%02u.%03u",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    }
    return value;
}

static const char* webViewProcessKindName(COREWEBVIEW2_PROCESS_FAILED_KIND kind) {
    switch (kind) {
    case COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED: return "browser";
    case COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED: return "renderer";
    case COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE: return "renderer-unresponsive";
    case COREWEBVIEW2_PROCESS_FAILED_KIND_FRAME_RENDER_PROCESS_EXITED: return "frame-renderer";
    case COREWEBVIEW2_PROCESS_FAILED_KIND_UTILITY_PROCESS_EXITED: return "utility";
    case COREWEBVIEW2_PROCESS_FAILED_KIND_SANDBOX_HELPER_PROCESS_EXITED: return "sandbox-helper";
    case COREWEBVIEW2_PROCESS_FAILED_KIND_GPU_PROCESS_EXITED: return "gpu";
    case COREWEBVIEW2_PROCESS_FAILED_KIND_PPAPI_PLUGIN_PROCESS_EXITED: return "ppapi-plugin";
    case COREWEBVIEW2_PROCESS_FAILED_KIND_PPAPI_BROKER_PROCESS_EXITED: return "ppapi-broker";
    case COREWEBVIEW2_PROCESS_FAILED_KIND_UNKNOWN_PROCESS_EXITED: return "unknown";
    }
    return "unknown";
}

static const char* webViewFailureReasonName(COREWEBVIEW2_PROCESS_FAILED_REASON reason) {
    switch (reason) {
    case COREWEBVIEW2_PROCESS_FAILED_REASON_UNEXPECTED: return "unexpected";
    case COREWEBVIEW2_PROCESS_FAILED_REASON_UNRESPONSIVE: return "unresponsive";
    case COREWEBVIEW2_PROCESS_FAILED_REASON_TERMINATED: return "terminated";
    case COREWEBVIEW2_PROCESS_FAILED_REASON_CRASHED: return "crashed";
    case COREWEBVIEW2_PROCESS_FAILED_REASON_LAUNCH_FAILED: return "launch-failed";
    case COREWEBVIEW2_PROCESS_FAILED_REASON_OUT_OF_MEMORY: return "out-of-memory";
    case COREWEBVIEW2_PROCESS_FAILED_REASON_PROFILE_DELETED: return "profile-deleted";
    }
    return "unknown";
}

static void appendWebViewDiagnostic(json entry) {
    try {
        entry["time"] = webViewLocalTimestamp(false);
        entry["gpuMode"] = webViewGpuModeName(g_webviewGpuMode);
        std::lock_guard<std::mutex> lock(g_webviewFailureLogMx);
        std::ofstream out(fspath::path(webview_data_dir()) / L"webview-failures.log",
            std::ios::binary | std::ios::app);
        if (out) {
            out << entry.dump() << '\n';
            out.flush();
        }
    } catch (...) {}
}

static void appendWebViewFailureDiagnostic(const WebViewFailureInfo& info) {
    appendWebViewDiagnostic({
        {"event", "process-failed"},
        {"kind", webViewProcessKindName(info.kind)},
        {"kindCode", static_cast<int>(info.kind)},
        {"reason", webViewFailureReasonName(info.reason)},
        {"reasonCode", static_cast<int>(info.reason)},
        {"exitCode", info.exitCode},
        {"description", W2U(info.description)},
        {"module", W2U(info.modulePath)},
        {"tick", info.tick}
    });
}

static bool writeWebViewFailureMarker(const WebViewFailureInfo& info) {
    try {
        std::error_code ec;
        fspath::create_directories(webview_profile_dir(), ec);
        if (ec) return false;
        json marker = {
            {"time", webViewLocalTimestamp(false)},
            {"kind", webViewProcessKindName(info.kind)},
            {"reason", webViewFailureReasonName(info.reason)},
            {"exitCode", info.exitCode},
            {"description", W2U(info.description)},
            {"module", W2U(info.modulePath)},
            {"gpuMode", webViewGpuModeName(g_webviewGpuMode)}
        };
        return sgWriteFileAtomic(webview_failure_marker_path().wstring(), marker.dump(2));
    } catch (...) {
        return false;
    }
}

static bool isolateWebViewProfile(const char* trigger) {
    const auto profile = webview_profile_dir();
    std::error_code ec;
    if (!fspath::exists(profile, ec) || ec) return true;

    const auto parent = profile.parent_path();
    const auto stamp = U2W(webViewLocalTimestamp(true));
    fspath::path target;
    for (int attempt = 1; attempt <= 100; ++attempt) {
        target = parent / (L"EBWebView.failed-" + stamp + L"-browser-" + std::to_wstring(attempt));
        ec.clear();
        if (!fspath::exists(target, ec) && !ec) break;
    }

    ec.clear();
    fspath::rename(profile, target, ec);
    appendWebViewDiagnostic({
        {"event", "profile-isolation"},
        {"ok", !ec},
        {"trigger", trigger ? trigger : "unknown"},
        {"from", W2U(profile.wstring())},
        {"to", W2U(target.wstring())},
        {"error", ec ? ec.message() : std::string{}}
    });
    return !ec;
}

static void isolateMarkedWebViewProfile() {
    const auto marker = webview_failure_marker_path();
    std::error_code ec;
    if (!fspath::exists(marker, ec) || ec) return;
    isolateWebViewProfile("startup-browser-marker");
}

static std::wstring webViewBrowserArguments(WebViewGpuMode mode) {
    std::wstring args =
        L"--disable-features=msSmartScreenProtection,RendererCodeIntegrity,msWebOOUI,msPdfOOUI"
        L" --disable-background-networking --no-proxy-server"
        L" --disable-extensions --disable-component-extensions-with-background-pages"
         L" --no-default-browser-check --disable-client-side-phishing-detection";
    if (mode == WebViewGpuMode::Legacy)
        args += L" --disable-gpu-sandbox --in-process-gpu";
    else if (mode == WebViewGpuMode::Software)
        args += L" --disable-gpu";
    return args;
}

// ================================================================
//  Embedded resource loader (single-exe mode)
// ================================================================

#ifdef SINGLE_EXE
// Load a small resource as std::string (used for config only)
static std::string loadResourceString(int id) {
    HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCE(id), RT_RCDATA);
    if (!hRes) return {};
    HGLOBAL hData = LoadResource(nullptr, hRes);
    if (!hData) return {};
    DWORD sz = SizeofResource(nullptr, hRes);
    auto* ptr = (const char*)LockResource(hData);
    return std::string(ptr, sz);
}

// Zero-copy PAK loader: entries point directly into PE memory-mapped section
static void loadPak() {
    HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCE(IDR_HTML), RT_RCDATA);
    if (!hRes) return;
    HGLOBAL hData = LoadResource(nullptr, hRes);
    if (!hData) return;
    DWORD totalSize = SizeofResource(nullptr, hRes);
    const char* base = (const char*)LockResource(hData);
    if (!base || totalSize < 4 || base[0] != 'Q' || base[1] != 'Q') return;
    const char* end = base + totalSize;
    const char* p = base + 2;
    uint16_t count; memcpy(&count, p, 2); p += 2;
    for (uint16_t i = 0; i < count && p < end; i++) {
        if (p + 2 > end) break;
        uint16_t pathLen; memcpy(&pathLen, p, 2); p += 2;
        if (p + pathLen > end) break;
        std::string path(p, pathLen); p += pathLen;
        if (p + 4 > end) break;
        uint32_t dataLen; memcpy(&dataLen, p, 4); p += 4;
        if (p + dataLen > end) break;
        g_pakEntries.push_back({path, p, dataLen});
        p += dataLen;
    }
}

static const PakEntry* findPakEntry(const std::string& path) {
    for (auto& e : g_pakEntries)
        if (e.path == path) return &e;
    return nullptr;
}

static std::wstring guessMimeType(const std::string& path) {
    auto ext = path.substr(path.rfind('.') + 1);
    if (ext == "html" || ext == "htm") return L"text/html";
    if (ext == "js" || ext == "mjs")   return L"application/javascript";
    if (ext == "css")                  return L"text/css";
    if (ext == "json")                 return L"application/json";
    if (ext == "svg")                  return L"image/svg+xml";
    if (ext == "png")                  return L"image/png";
    if (ext == "jpg" || ext == "jpeg") return L"image/jpeg";
    if (ext == "gif")                  return L"image/gif";
    if (ext == "ico")                  return L"image/x-icon";
    if (ext == "woff2")                return L"font/woff2";
    if (ext == "woff")                 return L"font/woff";
    if (ext == "ttf")                  return L"font/ttf";
    return L"application/octet-stream";
}
#endif

// ================================================================
//  Config loader
// ================================================================

static COLORREF parseHexColor(const std::string& hex, COLORREF def = RGB(26,26,46)) {
    if (hex.size() < 7 || hex[0] != '#') return def;
    try {
        int r = std::stoi(hex.substr(1,2), nullptr, 16);
        int g = std::stoi(hex.substr(3,2), nullptr, 16);
        int b = std::stoi(hex.substr(5,2), nullptr, 16);
        return RGB(r, g, b);
    } catch (...) { return def; }
}

static int parseEffectType(const std::string& name) {
    if (name == "mica")     return 2;
    if (name == "acrylic")  return 3;
    if (name == "micaAlt" || name == "tabbed") return 4;
    return 0;
}

static void applyWindowEffect(HWND hwnd, int effectType) {
    // DWM 属性（模糊/边框/标题）统一由 applyFramelessDwmAttrs() 通过
    // SetWindowCompositionAttribute(ACCENT_ENABLE_BLURBEHIND) 管理，不再使用
    // DWMWA_SYSTEMBACKDROP_TYPE（在本机/此窗口样式下不透）。
    (void)hwnd; // DWM 操作已集中到 applyFramelessDwmAttrs
    if (g_ctrl) {
        ComPtr<ICoreWebView2Controller2> ctrl2;
        if (SUCCEEDED(g_ctrl.As(&ctrl2))) {
            BYTE alpha = g_frameless ? 0 : 255;
            auto clr = currentWindowBackgroundColor();
            ctrl2->put_DefaultBackgroundColor({alpha, GetRValue(clr), GetGValue(clr), GetBValue(clr)});
        }
    }
    // 同步边框/标题色与 Accent Blur 状态
    if (g_frameless) applyFramelessDwmAttrs();
}

// Set DWM visual attributes once. Uses g_bgClr captured at window creation.
// Safe to call again if g_bgClr changes (e.g. window.setBackgroundColor).
struct ACCENTPOLICY { DWORD AccentState; DWORD AccentFlags; DWORD GradientColor; DWORD AnimationId; };
struct WINCOMPATTRDATA { DWORD attribute; PVOID pData; ULONG dataSize; };
using pSetWindowCompositionAttribute = BOOL (WINAPI*)(HWND, WINCOMPATTRDATA*);

// 用 SetWindowCompositionAttribute(ACCENT_ENABLE_BLURBEHIND) 在 DWM 合成器层
// 给整窗加模糊。比 DWMWA_SYSTEMBACKDROP_TYPE 兼容性更好，在本机/此窗口样式下
// SystemBackdrop 始终不透，而这条路线经多年实战验证可稳定工作。
static void applyAccentBlur(HWND hwnd, bool enable) {
    static HMODULE hUser = GetModuleHandleW(L"user32.dll");
    static pSetWindowCompositionAttribute fn = hUser
        ? (pSetWindowCompositionAttribute)GetProcAddress(hUser, "SetWindowCompositionAttribute")
        : nullptr;
    if (!fn) return;
    const DWORD WCA_ACCENT_POLICY = 19;
    const DWORD ACCENT_DISABLED = 0;
    const DWORD ACCENT_ENABLE_BLURBEHIND = 3;
    ACCENTPOLICY policy = {};
    if (enable) {
        policy.AccentState = ACCENT_ENABLE_BLURBEHIND;
        policy.AccentFlags = 2; // 让模糊覆盖四边
        policy.GradientColor = 0x20000000; // ARGB: alpha 32 的纯黑，极淡 tint，主要靠 CSS 面板着色
    } else {
        policy.AccentState = ACCENT_DISABLED;
    }
    WINCOMPATTRDATA data = { WCA_ACCENT_POLICY, &policy, sizeof(policy) };
    fn(hwnd, &data);
}

static void applyFramelessDwmAttrs() {
    if (!g_frameless || !g_hwnd) return;

    int lum = (GetRValue(g_bgClr)*299 + GetGValue(g_bgClr)*587 + GetBValue(g_bgClr)*114) / 1000;
    BOOL darkMode = (lum < 128) ? TRUE : FALSE;

    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20
    DwmSetWindowAttribute(g_hwnd, 20, &darkMode, sizeof(darkMode));
    // DWMWA_WINDOW_CORNER_PREFERENCE = 33 — Win11 rounded corners (default on).
    // Once WM_NCCALCSIZE strips the frame, Win11 stops auto-rounding the borderless
    // window (square corners + no shadow); requesting ROUND explicitly restores both.
    // DWMWCP_ROUND = 2, DWMWCP_DONOTROUND = 1. Toggle via window.rounded (default true).
    DWORD cornerPref = g_rounded ? 2u : 1u;
    DwmSetWindowAttribute(g_hwnd, 33, &cornerPref, sizeof(cornerPref));
    // SystemBackdrop 与本机窗口样式冲突，统一关闭，改走 SetWindowCompositionAttribute BlurBehind。
    int noneEffect = 0;
    DwmSetWindowAttribute(g_hwnd, 38, &noneEffect, sizeof(noneEffect));
    if (g_effectType >= 2) {
        // 磨砂玻璃：边框/标题色必须 COLOR_NONE，否则 DWM 用实色填充扩展区 → 不透明。
        COLORREF none = 0xFFFFFFFE; // DWMWA_COLOR_NONE
        DwmSetWindowAttribute(g_hwnd, 34, &none, sizeof(none));
        DwmSetWindowAttribute(g_hwnd, 35, &none, sizeof(none));
        applyAccentBlur(g_hwnd, true);
    } else {
        // DWMWA_BORDER_COLOR = 34 — concrete color matching bg is more reliable
        // than DWMWA_COLOR_NONE which can flash on defocus on some installs.
        DwmSetWindowAttribute(g_hwnd, 34, &g_bgClr, sizeof(g_bgClr));
        // DWMWA_CAPTION_COLOR = 35
        DwmSetWindowAttribute(g_hwnd, 35, &g_bgClr, sizeof(g_bgClr));
        // 非磨砂：底部留 1px frame，避免白边。
        MARGINS m = {0, 0, 0, 1};
        DwmExtendFrameIntoClientArea(g_hwnd, &m);
        applyAccentBlur(g_hwnd, false);
    }
}

static json loadConfig() {
    for (const auto& dir : production_asset_dirs()) {
        auto path = dir + L"\\app.config.json";
        std::ifstream f(path);
        if (f) {
            try {
                json j;
                f >> j;
                return j;
            } catch (...) {
                // 外部配置损坏不能阻断窗口创建；继续尝试下一候选，最终回退内置/默认配置。
            }
        }
    }
#ifdef SINGLE_EXE
    auto cfg = loadResourceString(IDR_CONFIG);
    if (!cfg.empty()) {
        try { return json::parse(cfg); } catch (...) {}
    }
#endif
    return json::object();
}

// ================================================================
//  IPC 异步执行基础设施（C 方案）
//  历史卡顿根因：ipc_dispatch 曾在 WebView2 UI 线程同步等待 powercfg/schtasks；
//  当前外部命令均有超时/进程树回收，但仍必须在线程池执行，避免消息泵冻结。
//  方案：白名单命令 offload 到 worker 线程池执行，完成后 PostMessage
//  回 UI 线程，再由 UI 线程调用 PostWebMessageAsJson（WebView2 仅允许
//  UI 线程调用）。前端 ipc.ts 用 pending Map<id> 匹配响应，乱序安全。
//  可调旋钮（环境变量，便于多方案实测）：
//    YEMAN_ASYNC  0=全同步(旧行为) 1=仅 shell.run 异步(默认) 2=扩展白名单
//    YEMAN_POOL   worker 线程数 1..16（默认 4）
//    YEMAN_TRACE=debug  显式开启诊断日志(%TEMP%\yemancc_trace.log)+冻结监视线程
// ================================================================

#define WM_IPC_RESULT (WM_USER + 3)

static int  g_asyncMode = 2;   // 0/1/2 —— 2: 扩展白名单(读+写+进程启动)全异步，浮动调度不再阻塞 UI 线程
static int  g_poolSize  = 3;   // 改为 3：默认异步池线程数。实测首屏已降至 1 个 shell.run，正常交互峰值并发 2-4，3 线程绰绰有余，省 ~5 条常驻线程

// ── 诊断 trace（仅 YEMAN_TRACE=debug 时激活；生产默认关闭）──
static std::atomic<bool> g_traceOn{false};
static std::mutex   g_traceMx;
static FILE*        g_traceFile = nullptr;
static ULONGLONG    g_traceT0 = 0;

static void traceLog(const char* fmt, ...) {
    if (!g_traceOn.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lk(g_traceMx);
    if (!g_traceFile) return;
    fprintf(g_traceFile, "[%8llu] ", (unsigned long long)(GetTickCount64() - g_traceT0));
    va_list ap; va_start(ap, fmt);
    vfprintf(g_traceFile, fmt, ap);
    va_end(ap);
    fputc('\n', g_traceFile);
    fflush(g_traceFile);
}

static void traceInit() {
    std::lock_guard<std::mutex> lk(g_traceMx);
    if (g_traceFile) return;
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring p = std::wstring(tmp) + L"yemancc_trace.log";
    g_traceFile = _wfopen(p.c_str(), L"w");
    g_traceT0 = GetTickCount64();
    g_traceOn = true;
}

// ── worker 线程池（懒创建；detach 常驻，进程退出时随之销毁）──
static std::mutex g_poolMx;
static std::condition_variable g_poolCv;
static std::deque<std::function<void()>> g_poolQ;
static std::vector<std::thread> g_poolThreads;
static bool g_poolStarted = false;
static bool g_poolStopping = false;
// Exit must be able to interrupt a shell.run child instead of waiting for its
// full command timeout.  The worker remains joined, but active subprocesses
// are terminated through their private Job object as soon as shutdown starts.
static std::atomic<bool> g_poolCancel{false};
static constexpr size_t IPC_POOL_QUEUE_LIMIT = 32;

static void poolStartLocked() {
    if (g_poolStarted) return;
    g_poolStarted = true;
    g_poolStopping = false;
    g_poolCancel.store(false, std::memory_order_release);
    g_poolThreads.reserve((size_t)g_poolSize);
    for (int i = 0; i < g_poolSize; i++) {
        g_poolThreads.emplace_back([] {
            for (;;) {
                std::function<void()> job;
                {
                    std::unique_lock<std::mutex> lk(g_poolMx);
                    g_poolCv.wait(lk, [] { return g_poolStopping || !g_poolQ.empty(); });
                    if (g_poolStopping && g_poolQ.empty()) return;
                    job = std::move(g_poolQ.front());
                    g_poolQ.pop_front();
                }
                try { job(); } catch (...) {}
            }
        });
    }
}

static bool poolSubmit(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lk(g_poolMx);
        if (g_poolStopping || g_poolQ.size() >= IPC_POOL_QUEUE_LIMIT) return false;
        poolStartLocked();
        g_poolQ.push_back(std::move(job));
    }
    g_poolCv.notify_one();
    return true;
}

static void poolStop() {
    g_poolCancel.store(true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lk(g_poolMx);
        if (!g_poolStarted) return;
        g_poolStopping = true;
        // 丢弃尚未开始的任务，避免退出时继续向已销毁的 WebView 投递结果。
        g_poolQ.clear();
    }
    g_poolCv.notify_all();
    for (auto& thread : g_poolThreads) {
        if (thread.joinable()) thread.join();
    }
    g_poolThreads.clear();
    std::lock_guard<std::mutex> lk(g_poolMx);
    g_poolStarted = false;
}

// 完全可控睡眠已移除。睡眠页只提供电源计划级别的睡眠优化。
#if 0 // 旧实现保留隔离，禁止参与编译或运行
struct CsOverrideRec {
    std::string type;
    std::string name;
    std::set<std::string> requests;
};

static std::string csReqKey(const std::string& type, const std::string& name) {
    return ascii_lower(type) + "\n" + ascii_lower(name);
}

static std::string csTrimLine(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return trim_ascii(std::move(s));
}

static std::vector<std::string> csLines(const std::string& text) {
    std::vector<std::string> out;
    size_t p = 0;
    while (p <= text.size()) {
        size_t q = text.find('\n', p);
        out.push_back(csTrimLine(text.substr(p, q == std::string::npos ? std::string::npos : q - p)));
        if (q == std::string::npos) break;
        p = q + 1;
    }
    return out;
}

static bool csRequestType(const std::string& value) {
    const auto v = ascii_lower(value);
    return v == "display" || v == "system" || v == "awaymode";
}

static std::string csJoinTokens(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& x : v) {
        if (!out.empty()) out += ' ';
        out += x;
    }
    return out;
}

static std::string csCanonicalCaller(const std::string& type, std::string name) {
    name = csTrimLine(std::move(name));
    if (!name.empty() && name.front() == '"' && name.back() == '"')
        name = name.substr(1, name.size() - 2);
    if (ascii_lower(type) == "process") {
        const auto p = name.find_last_of("\\/");
        if (p != std::string::npos) name = name.substr(p + 1);
    }
    return name;
}

static std::map<std::string, CsOverrideRec> csParseOverrides(const std::string& raw) {
    std::map<std::string, CsOverrideRec> out;
    std::string section;
    for (auto line : csLines(raw)) {
        if (line.empty()) continue;
        if (line.size() > 2 && line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            if (section != "PROCESS" && section != "SERVICE" && section != "DRIVER") section.clear();
            continue;
        }
        if (section.empty()) continue;
        std::istringstream iss(line);
        std::vector<std::string> tokens;
        std::string token;
        while (iss >> token) tokens.push_back(token);
        if (tokens.empty()) continue;
        std::set<std::string> requests;
        while (!tokens.empty() && csRequestType(tokens.back())) {
            requests.insert(ascii_lower(tokens.back()));
            tokens.pop_back();
        }
        if (tokens.empty() || requests.empty()) continue;
        const std::string name = csCanonicalCaller(section, csJoinTokens(tokens));
        if (name.empty()) continue;
        auto& rec = out[csReqKey(section, name)];
        rec.type = section;
        rec.name = name;
        rec.requests.insert(requests.begin(), requests.end());
    }
    return out;
}

static std::map<std::string, CsOverrideRec> csParseRequests(const std::string& raw) {
    std::map<std::string, CsOverrideRec> out;
    std::string requestType;
    for (auto line : csLines(raw)) {
        if (line.empty()) { requestType.clear(); continue; }
        const auto colon = line.find(':');
        if (colon != std::string::npos && colon == line.size() - 1) {
            const auto maybe = ascii_lower(line.substr(0, colon));
            if (csRequestType(maybe)) requestType = maybe;
            else if (maybe == "perfboost" || maybe == "active lock screen" || maybe == "execution" || maybe == "执行") requestType.clear();
            continue;
        }
        if (requestType.empty() || line.front() != '[') continue;
        const auto rb = line.find(']');
        if (rb == std::string::npos) continue;
        const std::string type = line.substr(1, rb - 1);
        if (type != "PROCESS" && type != "SERVICE" && type != "DRIVER") continue;
        const std::string name = csCanonicalCaller(type, line.substr(rb + 1));
        if (name.empty()) continue;
        auto& rec = out[csReqKey(type, name)];
        rec.type = type;
        rec.name = name;
        rec.requests.insert(requestType);
    }
    return out;
}

static std::wstring csPowerCfgPath() {
    wchar_t sys[MAX_PATH] = {};
    const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    if (!n || n >= MAX_PATH) return L"powercfg.exe";
    return std::wstring(sys, n) + L"\\powercfg.exe";
}

static RunOut csPowerCfg(const std::vector<std::wstring>& args) {
    std::wstring cmd = quote_windows_arg(csPowerCfgPath());
    for (const auto& arg : args) {
        cmd += L' ';
        cmd += arg;
    }
    RunOut r = runCapture(cmd);
    r.out = oemToUtf8(r.out);
    return r;
}

static std::vector<std::string> csPowerCfgList(const char* query) {
    const RunOut r = csPowerCfg({L"/devicequery", U2W(query)});
    if (!r.ran || r.exitCode != 0) return {};
    std::vector<std::string> out;
    for (auto line : csLines(r.out)) {
        if (line.empty()) continue;
        const auto low = ascii_lower(line);
        if (low == "none." || low == "none" || line == "无。" || line == "无") continue;
        out.push_back(line);
    }
    return out;
}

static bool csSetDeviceWake(const std::string& name, bool enable) {
    const RunOut r = csPowerCfg({U2W(enable ? "/deviceenableawake" : "/devicedisablewake"), quote_windows_arg(U2W(name))});
    return r.ran && r.exitCode == 0;
}

static std::wstring csPnpProperty(DEVINST dn, const DEVPROPKEY& key) {
    DEVPROPTYPE type = 0;
    ULONG size = 0;
    CONFIGRET cr = CM_Get_DevNode_PropertyW(dn, &key, &type, nullptr, &size, 0);
    if (cr != CR_BUFFER_SMALL || size < sizeof(wchar_t)) return {};
    std::vector<BYTE> buf(size + sizeof(wchar_t), 0);
    cr = CM_Get_DevNode_PropertyW(dn, &key, &type, buf.data(), &size, 0);
    if (cr != CR_SUCCESS || type != DEVPROP_TYPE_STRING) return {};
    return std::wstring(reinterpret_cast<wchar_t*>(buf.data()));
}

struct CsPnpNode {
    DEVINST dn = 0;
    std::wstring instanceId;
    std::wstring friendlyName;
    std::wstring deviceDesc;
    std::wstring service;
    std::wstring className;
    ULONG status = 0;
};

static std::vector<CsPnpNode> csEnumeratePnp() {
    ULONG chars = 0;
    if (CM_Get_Device_ID_List_SizeW(&chars, nullptr, CM_GETIDLIST_FILTER_PRESENT) != CR_SUCCESS || chars == 0)
        return {};
    std::vector<wchar_t> ids(chars + 1, L'\0');
    if (CM_Get_Device_ID_ListW(nullptr, ids.data(), chars, CM_GETIDLIST_FILTER_PRESENT) != CR_SUCCESS)
        return {};
    std::vector<CsPnpNode> out;
    for (const wchar_t* p = ids.data(); *p; p += wcslen(p) + 1) {
        DEVINST dn = 0;
        if (CM_Locate_DevNodeW(&dn, const_cast<wchar_t*>(p), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) continue;
        CsPnpNode node;
        node.dn = dn;
        node.instanceId = p;
        node.friendlyName = csPnpProperty(dn, DEVPKEY_Device_FriendlyName);
        node.deviceDesc = csPnpProperty(dn, DEVPKEY_Device_DeviceDesc);
        node.service = csPnpProperty(dn, DEVPKEY_Device_Service);
        node.className = csPnpProperty(dn, DEVPKEY_Device_Class);
        ULONG problem = 0;
        CM_Get_DevNode_Status(&node.status, &problem, dn, 0);
        out.push_back(std::move(node));
    }
    return out;
}

static std::wstring csPnpName(const CsPnpNode& node) {
    return !node.friendlyName.empty() ? node.friendlyName : node.deviceDesc;
}

static std::wstring csLowerW(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return value;
}

static bool csContainsW(const std::wstring& value, const std::wstring& needle) {
    return csLowerW(value).find(csLowerW(needle)) != std::wstring::npos;
}

static bool csIsStarted(const CsPnpNode& node) {
    return (node.status & DN_STARTED) != 0;
}

static bool csNodeIsMouse(const CsPnpNode& node) {
    const auto cls = csLowerW(node.className);
    const auto name = csLowerW(csPnpName(node));
    return cls == L"mouse" || name.find(L"mouse") != std::wstring::npos || name.find(L"鼠标") != std::wstring::npos;
}

static bool csIsAncestor(DEVINST child, DEVINST ancestor) {
    DEVINST cur = child;
    for (int depth = 0; depth < 64; depth++) {
        if (cur == ancestor) return true;
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, cur, 0) != CR_SUCCESS) break;
        cur = parent;
    }
    return false;
}

static std::set<std::string> csProtectedWakeNames(const std::vector<CsPnpNode>& nodes) {
    std::set<std::string> out;
    for (const auto& node : nodes) {
        if (!csNodeIsMouse(node)) continue;
        DEVINST cur = node.dn;
        for (int depth = 0; depth < 64; depth++) {
            for (const auto& candidate : nodes) {
                if (candidate.dn == cur) {
                    const auto display = csPnpName(candidate);
                    if (!display.empty()) out.insert(W2U(display));
                    break;
                }
            }
            DEVINST parent = 0;
            if (CM_Get_Parent(&parent, cur, 0) != CR_SUCCESS) break;
            cur = parent;
        }
    }
    return out;
}

static std::vector<CsPnpNode> csUsb4Hosts(const std::vector<CsPnpNode>& nodes) {
    std::vector<CsPnpNode> out;
    for (const auto& node : nodes) {
        const auto service = csLowerW(node.service);
        const auto name = csLowerW(csPnpName(node));
        if (service == L"usb4hostrouter" || name.find(L"usb4 host router") != std::wstring::npos ||
            name.find(L"usb4 主机路由器") != std::wstring::npos) {
            out.push_back(node);
        }
    }
    return out;
}

static std::string csGuidString(const GUID& guid) {
    wchar_t buf[64] = {};
    if (!StringFromGUID2(guid, buf, ARRAYSIZE(buf))) return {};
    return ascii_lower(W2U(buf));
}

static bool csParseGuid(const std::string& text, GUID& guid) {
    const std::wstring w = U2W(text);
    return !w.empty() && SUCCEEDED(CLSIDFromString(w.c_str(), &guid));
}

static bool csReadWakeTimer(const GUID& scheme, bool ac, DWORD& value) {
    const auto rc = ac
        ? PowerReadACValueIndex(nullptr, &scheme, &CS_GUID_SLEEP_SUBGROUP, &CS_GUID_RTC_WAKE, &value)
        : PowerReadDCValueIndex(nullptr, &scheme, &CS_GUID_SLEEP_SUBGROUP, &CS_GUID_RTC_WAKE, &value);
    return rc == ERROR_SUCCESS;
}

static bool csWriteWakeTimer(const GUID& scheme, bool ac, DWORD value) {
    const auto rc = ac
        ? PowerWriteACValueIndex(nullptr, &scheme, &CS_GUID_SLEEP_SUBGROUP, &CS_GUID_RTC_WAKE, value)
        : PowerWriteDCValueIndex(nullptr, &scheme, &CS_GUID_SLEEP_SUBGROUP, &CS_GUID_RTC_WAKE, value);
    return rc == ERROR_SUCCESS;
}

static json csDefaultState() {
    return json{
        {"version", 1},
        {"mainEnabled", false},
        {"usb4HardBlockEnabled", false},
        {"wakeInitialized", false},
        {"wakeBaseline", json::array()},
        {"wakeProtected", json::array()},
        {"wakeDisabled", json::array()},
        {"timerSchemes", json::array()},
        {"managedOverrides", json::array()},
        {"managedUsb4", json::array()},
        {"usb4Detected", json::array()},
        {"usb4MouseConflict", false},
        {"lastError", ""},
        {"lastAction", ""},
        {"lastUpdated", 0LL}
    };
}

static void csMergeDefaults(json& state) {
    const json defaults = csDefaultState();
    for (auto it = defaults.begin(); it != defaults.end(); ++it)
        if (!state.contains(it.key())) state[it.key()] = it.value();
}

static bool csSaveLocked() {
    std::error_code ec;
    fspath::create_directories(SG_DIR, ec);
    g_csState["lastUpdated"] = (long long)sgNowEpoch();
    return sgWriteFileAtomic(CS_POLICY_FILE, g_csState.dump(2));
}

static void csLoadState() {
    std::lock_guard<std::mutex> lock(g_csMtx);
    g_csState = csDefaultState();
    const auto raw = sgReadFile(CS_POLICY_FILE);
    if (!raw.empty()) {
        try {
            json loaded = json::parse(raw);
            if (loaded.is_object()) {
                g_csState = std::move(loaded);
                csMergeDefaults(g_csState);
            }
        } catch (...) {
            g_csState["lastError"] = "controlled-sleep.json 损坏，已安全回退为关闭";
        }
    }
    g_csMainEnabled.store(g_csState.value("mainEnabled", false), std::memory_order_release);
    g_csUsb4Enabled.store(g_csState.value("usb4HardBlockEnabled", false), std::memory_order_release);
}

static json& csArray(json& state, const char* key) {
    if (!state[key].is_array()) state[key] = json::array();
    return state[key];
}

static bool csArrayContains(const json& array, const std::string& value) {
    if (!array.is_array()) return false;
    for (const auto& item : array) if (item.is_string() && ascii_lower(item.get<std::string>()) == ascii_lower(value)) return true;
    return false;
}

static std::string csJsonName(const json& item) {
    return item.is_object() ? item.value("name", std::string{}) : std::string{};
}

static json csStatusLocked() {
    json result = g_csState;
    result["enabled"] = g_csState.value("mainEnabled", false);
    result["usb4HardBlockEnabled"] = g_csState.value("usb4HardBlockEnabled", false);
    result["wakeDisabledCount"] = g_csState["wakeDisabled"].is_array() ? g_csState["wakeDisabled"].size() : 0;
    result["managedOverrideCount"] = g_csState["managedOverrides"].is_array() ? g_csState["managedOverrides"].size() : 0;
    result["usb4DisabledCount"] = g_csState["managedUsb4"].is_array() ? g_csState["managedUsb4"].size() : 0;
    result["usb4DetectedCount"] = g_csState["usb4Detected"].is_array() ? g_csState["usb4Detected"].size() : 0;
    result["timerSchemeCount"] = g_csState["timerSchemes"].is_array() ? g_csState["timerSchemes"].size() : 0;
    return result;
}

static std::set<std::string> csSetFromJson(const json& value) {
    std::set<std::string> out;
    if (!value.is_array()) return out;
    for (const auto& item : value) if (item.is_string()) out.insert(ascii_lower(item.get<std::string>()));
    return out;
}

static json csJsonSet(const std::set<std::string>& value) {
    json out = json::array();
    for (const auto& item : value) out.push_back(item);
    return out;
}

static bool csApplyOverride(const CsOverrideRec& rec, const std::set<std::string>& requests) {
    std::vector<std::wstring> args = {U2W("/requestsoverride"), U2W(rec.type), quote_windows_arg(U2W(rec.name))};
    for (const auto& request : requests) args.push_back(U2W(request));
    const auto r = csPowerCfg(args);
    return r.ran && r.exitCode == 0;
}

static void csRecordErrorLocked(const std::string& error) {
    g_csState["lastError"] = error;
}

static bool csApplyTimersLocked() {
    GUID* activeScheme = nullptr;
    if (PowerGetActiveScheme(nullptr, &activeScheme) != ERROR_SUCCESS || !activeScheme) {
        csRecordErrorLocked("无法读取当前电源方案");
        return false;
    }
    const GUID scheme = *activeScheme;
    LocalFree(activeScheme);
    const auto guid = csGuidString(scheme);
    auto& schemes = csArray(g_csState, "timerSchemes");
    json* entry = nullptr;
    for (auto& item : schemes) if (item.is_object() && item.value("guid", std::string{}) == guid) { entry = &item; break; }
    if (!entry) {
        DWORD ac = 0, dc = 0;
        if (!csReadWakeTimer(scheme, true, ac) || !csReadWakeTimer(scheme, false, dc)) {
            csRecordErrorLocked("无法读取当前电源方案的唤醒定时器");
            return false;
        }
        schemes.push_back({{"guid", guid}, {"ac", ac}, {"dc", dc}, {"expectedAc", 0}, {"expectedDc", 0}});
        entry = &schemes.back();
        csSaveLocked();
    }
    DWORD ac = 0, dc = 0;
    if (!csReadWakeTimer(scheme, true, ac) || !csReadWakeTimer(scheme, false, dc)) {
        csRecordErrorLocked("无法回读唤醒定时器");
        return false;
    }
    bool changed = false;
    if (ac != 0) { if (!csWriteWakeTimer(scheme, true, 0)) { csRecordErrorLocked("关闭 AC 唤醒定时器失败"); return false; } changed = true; }
    if (dc != 0) { if (!csWriteWakeTimer(scheme, false, 0)) { csRecordErrorLocked("关闭 DC 唤醒定时器失败"); return false; } changed = true; }
    if (changed) PowerSetActiveScheme(nullptr, &scheme);
    DWORD acAfter = 1, dcAfter = 1;
    if (!csReadWakeTimer(scheme, true, acAfter) || !csReadWakeTimer(scheme, false, dcAfter) || acAfter != 0 || dcAfter != 0) {
        csRecordErrorLocked("唤醒定时器关闭后回读仍未为 0");
        return false;
    }
    return true;
}

static bool csRestoreTimersLocked() {
    bool ok = true;
    GUID originalScheme{};
    bool haveOriginalScheme = false;
    GUID* activeScheme = nullptr;
    if (PowerGetActiveScheme(nullptr, &activeScheme) == ERROR_SUCCESS && activeScheme) {
        originalScheme = *activeScheme;
        haveOriginalScheme = true;
        LocalFree(activeScheme);
    }
    for (const auto& entry : csArray(g_csState, "timerSchemes")) {
        if (!entry.is_object()) continue;
        GUID scheme{};
        if (!csParseGuid(entry.value("guid", std::string{}), scheme)) { ok = false; continue; }
        const DWORD ac = entry.value("ac", 0u), dc = entry.value("dc", 0u);
        if (!csWriteWakeTimer(scheme, true, ac) || !csWriteWakeTimer(scheme, false, dc)) ok = false;
    }
    // PowerWrite* updates every recorded plan directly. Reactivate only the plan
    // that was active before restoration, so disabling the feature never changes
    // the user's selected power plan.
    if (haveOriginalScheme && PowerSetActiveScheme(nullptr, &originalScheme) != ERROR_SUCCESS) ok = false;
    if (!ok) csRecordErrorLocked("恢复唤醒定时器失败");
    else g_csState["timerSchemes"] = json::array();
    return ok;
}

static bool csApplyDevicesLocked() {
    const auto nodes = csEnumeratePnp();
    const auto protectedNames = csProtectedWakeNames(nodes);
    json protectedJson = json::array();
    for (const auto& name : protectedNames) protectedJson.push_back(name);
    g_csState["wakeProtected"] = protectedJson;
    const auto armed = csPowerCfgList("wake_armed");
    if (armed.empty()) {
        // 空列表是合法状态，但如果 powercfg 失败不能误认为没有设备。
        const auto probe = csPowerCfg({L"/devicequery", L"wake_armed"});
        if (!probe.ran || probe.exitCode != 0) { csRecordErrorLocked("读取允许唤醒设备失败"); return false; }
    }
    g_csState["lastWakeArmed"] = armed;
    if (!g_csState.value("wakeInitialized", false)) {
        g_csState["wakeInitialized"] = true;
        g_csState["wakeBaseline"] = armed;
    }
    auto& disabled = csArray(g_csState, "wakeDisabled");
    bool ok = true;
    for (const auto& name : armed) {
        if (protectedNames.count(name) || ascii_lower(name).find("mouse") != std::string::npos || name.find("鼠标") != std::string::npos) continue;
        json* item = nullptr;
        for (auto& existing : disabled) if (ascii_lower(csJsonName(existing)) == ascii_lower(name)) { item = &existing; break; }
        if (!item) {
            disabled.push_back({{"name", name}, {"applied", false}});
            item = &disabled.back();
            csSaveLocked();
        }
        // The device may have been re-armed by Device Manager or another tool
        // after our first pass. Seeing it in wake_armed is itself enough reason
        // to issue the disable command again; no extra probe is needed.
        if (!csSetDeviceWake(name, false)) {
            ok = false;
            csRecordErrorLocked("禁止设备唤醒失败：" + name);
        } else {
            (*item)["applied"] = true;
            csSaveLocked();
        }
    }
    return ok;
}

static bool csRestoreDevicesLocked() {
    bool ok = true;
    auto& disabled = csArray(g_csState, "wakeDisabled");
    json remaining = json::array();
    for (const auto& item : disabled) {
        const auto name = csJsonName(item);
        if (name.empty()) continue;
        if (item.value("applied", false) && !csSetDeviceWake(name, true)) {
            ok = false;
            remaining.push_back(item);
            csRecordErrorLocked("恢复设备唤醒失败：" + name);
        }
    }
    if (ok) {
        g_csState["wakeDisabled"] = json::array();
        g_csState["wakeBaseline"] = json::array();
        g_csState["wakeProtected"] = json::array();
        g_csState["wakeInitialized"] = false;
    } else {
        g_csState["wakeDisabled"] = remaining;
    }
    return ok;
}

static void csAddOverrideEntry(json& item, const CsOverrideRec& rec, const std::set<std::string>& before, const std::set<std::string>& expected) {
    item = json{{"type", rec.type}, {"name", rec.name}, {"before", csJsonSet(before)}, {"expected", csJsonSet(expected)}, {"applied", false}};
}

static bool csApplyOverridesLocked() {
    const auto requestsRun = csPowerCfg({L"/requests"});
    const auto overridesRun = csPowerCfg({L"/requestsoverride"});
    if (!requestsRun.ran || requestsRun.exitCode != 0 || !overridesRun.ran || overridesRun.exitCode != 0) {
        csRecordErrorLocked("读取睡眠请求或请求覆盖失败");
        return false;
    }
    const auto requests = csParseRequests(requestsRun.out);
    const auto overrides = csParseOverrides(overridesRun.out);
    auto& managed = csArray(g_csState, "managedOverrides");
    bool ok = true;
    for (const auto& [key, active] : requests) {
        std::set<std::string> before;
        auto old = overrides.find(key);
        if (old != overrides.end()) before = old->second.requests;
        json* item = nullptr;
        for (auto& existing : managed) {
            if (existing.value("type", std::string{}) == active.type &&
                ascii_lower(existing.value("name", std::string{})) == ascii_lower(active.name)) { item = &existing; break; }
        }
        const auto expected = [&] { auto v = before; v.insert(active.requests.begin(), active.requests.end()); return v; }();
        if (!item) {
            managed.push_back(json::object());
            item = &managed.back();
            csAddOverrideEntry(*item, active, before, expected);
            csSaveLocked();
        } else {
            (*item)["expected"] = csJsonSet(expected);
        }
        const auto current = overrides.find(key);
        const bool already = current != overrides.end() && current->second.requests == expected;
        if (!already || !item->value("applied", false)) {
            if (!csApplyOverride(active, expected)) {
                ok = false;
                (*item)["applied"] = false;
                csRecordErrorLocked("设置程序睡眠请求覆盖失败：" + active.name);
            } else {
                (*item)["applied"] = true;
                csSaveLocked();
            }
        }
    }
    return ok;
}

static bool csRestoreOverridesLocked() {
    bool ok = true;
    const auto currentRun = csPowerCfg({L"/requestsoverride"});
    if (!currentRun.ran || currentRun.exitCode != 0) {
        csRecordErrorLocked("读取睡眠请求覆盖失败");
        return false;
    }
    auto current = csParseOverrides(currentRun.out);
    auto& managed = csArray(g_csState, "managedOverrides");
    json remaining = json::array();
    for (const auto& item : managed) {
        const std::string type = item.value("type", std::string{});
        const std::string name = item.value("name", std::string{});
        if (type.empty() || name.empty()) continue;
        const auto key = csReqKey(type, name);
        auto it = current.find(key);
        const auto expected = csSetFromJson(item["expected"]);
        if (it != current.end() && it->second.requests != expected) {
            // 外部程序已在本开关期间改过此项：不覆盖外部修改。
            continue;
        }
        CsOverrideRec rec{type, name, {}};
        const auto before = csSetFromJson(item["before"]);
        if (!csApplyOverride(rec, before)) {
            ok = false;
            remaining.push_back(item);
            csRecordErrorLocked("恢复程序睡眠请求覆盖失败：" + name);
        }
    }
    g_csState["managedOverrides"] = remaining;
    if (!ok) return false;
    return true;
}

static bool csUsb4MouseConflict(const std::vector<CsPnpNode>& nodes, const std::vector<CsPnpNode>& hosts) {
    for (const auto& host : hosts)
        for (const auto& node : nodes)
            if (csNodeIsMouse(node) && csIsAncestor(node.dn, host.dn)) return true;
    return false;
}

static bool csApplyUsb4Locked() {
    const auto nodes = csEnumeratePnp();
    const auto hosts = csUsb4Hosts(nodes);
    json detected = json::array();
    for (const auto& host : hosts) detected.push_back({{"instanceId", W2U(host.instanceId)}, {"name", W2U(csPnpName(host))}, {"started", csIsStarted(host)}});
    g_csState["usb4Detected"] = detected;
    g_csState["usb4MouseConflict"] = csUsb4MouseConflict(nodes, hosts);
    if (g_csState.value("usb4MouseConflict", false)) {
        csRecordErrorLocked("鼠标位于 USB4 设备树内，无法同时保留鼠标唤醒和硬禁用 USB4");
        return false;
    }
    auto& managed = csArray(g_csState, "managedUsb4");
    bool ok = true;
    for (const auto& host : hosts) {
        const std::string id = W2U(host.instanceId);
        json* item = nullptr;
        for (auto& existing : managed) if (existing.value("instanceId", std::string{}) == id) { item = &existing; break; }
        if (!item) {
            managed.push_back({{"instanceId", id}, {"name", W2U(csPnpName(host))}, {"wasStarted", csIsStarted(host)}, {"disabled", false}});
            item = &managed.back();
            csSaveLocked();
        }
        if (item->value("wasStarted", false) && !item->value("disabled", false)) {
            const auto rc = CM_Disable_DevNode(host.dn, CM_DISABLE_UI_NOT_OK);
            if (rc != CR_SUCCESS) {
                ok = false;
                csRecordErrorLocked("USB4 Host Router 禁用失败：" + W2U(csPnpName(host)) + "，错误码=" + std::to_string(rc));
            } else {
                (*item)["disabled"] = true;
                csSaveLocked();
            }
        }
    }
    return ok;
}

static bool csRestoreUsb4Locked() {
    bool ok = true;
    json remaining = json::array();
    for (const auto& item : csArray(g_csState, "managedUsb4")) {
        if (!item.value("disabled", false)) continue;
        DEVINST dn = 0;
        const auto id = U2W(item.value("instanceId", std::string{}));
        if (id.empty() || CM_Locate_DevNodeW(&dn, const_cast<wchar_t*>(id.c_str()), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) {
            // 设备已断开；非永久禁用不会影响下次枚举。
            continue;
        }
        if (CM_Enable_DevNode(dn, 0) != CR_SUCCESS) {
            ok = false;
            remaining.push_back(item);
            csRecordErrorLocked("恢复 USB4 Host Router 失败：" + item.value("name", std::string{}));
        }
    }
    g_csState["managedUsb4"] = remaining;
    return ok;
}

static bool csRestoreAllLocked() {
    bool ok = true;
    if (!csRestoreUsb4Locked()) ok = false;
    if (!csRestoreOverridesLocked()) ok = false;
    if (!csRestoreTimersLocked()) ok = false;
    if (!csRestoreDevicesLocked()) ok = false;
    if (ok) {
        g_csState["usb4HardBlockEnabled"] = false;
        g_csUsb4Enabled.store(false, std::memory_order_release);
    }
    csSaveLocked();
    return ok;
}

static bool csApplyAllLocked(unsigned flags) {
    bool ok = true;
    if (flags & CS_RECONCILE_TIMERS) if (!csApplyTimersLocked()) ok = false;
    if (flags & CS_RECONCILE_DEVICES) if (!csApplyDevicesLocked()) ok = false;
    if (flags & CS_RECONCILE_REQUESTS) if (!csApplyOverridesLocked()) ok = false;
    if (flags & CS_RECONCILE_USB4) {
        if (g_csState.value("usb4HardBlockEnabled", false)) { if (!csApplyUsb4Locked()) ok = false; }
        else if (!csRestoreUsb4Locked()) ok = false;
    }
    csSaveLocked();
    return ok;
}

static void csSyncTimerOnUiThread() {
    if (!g_hwnd) return;
    KillTimer(g_hwnd, CS_RECONCILE_TIMER_5M_ID);
    KillTimer(g_hwnd, CS_RECONCILE_TIMER_10M_ID);
    if (g_csMainEnabled.load(std::memory_order_acquire)) {
        SetTimer(g_hwnd, CS_RECONCILE_TIMER_5M_ID, CS_RECONCILE_DELAY_5M_MS, nullptr);
        SetTimer(g_hwnd, CS_RECONCILE_TIMER_10M_ID, CS_RECONCILE_DELAY_10M_MS, nullptr);
    }
}

static void csScheduleReconcile(unsigned flags) {
    g_csReconcilePending.fetch_or(flags, std::memory_order_acq_rel);
    bool expected = false;
    if (!g_csReconcileQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;
    if (!poolSubmit([] {
        for (;;) {
            const unsigned pending = g_csReconcilePending.exchange(0, std::memory_order_acq_rel);
            if (!pending) break;
            std::lock_guard<std::mutex> lock(g_csMtx);
            if (g_csState.value("mainEnabled", false)) csApplyAllLocked(pending);
            else if (g_csState.value("wakeInitialized", false) || g_csState["managedOverrides"].size() || g_csState["timerSchemes"].size() || g_csState["managedUsb4"].size()) csRestoreAllLocked();
        }
        g_csReconcileQueued.store(false, std::memory_order_release);
        if (g_csReconcilePending.load(std::memory_order_acquire)) csScheduleReconcile(CS_RECONCILE_ALL);
    })) {
        g_csReconcileQueued.store(false, std::memory_order_release);
    }
}

static json csSetMain(bool on) {
    std::lock_guard<std::mutex> lock(g_csMtx);
    if (on) {
        g_csState["mainEnabled"] = true;
        g_csMainEnabled.store(true, std::memory_order_release);
        csSaveLocked();
        const bool ok = csApplyAllLocked(CS_RECONCILE_ALL);
        if (g_hwnd) PostMessageW(g_hwnd, WM_CS_TIMER_SYNC, 0, 0);
        return json{{"ok", ok}, {"status", csStatusLocked()}};
    }
    g_csState["mainEnabled"] = false;
    g_csMainEnabled.store(false, std::memory_order_release);
    g_csState["usb4HardBlockEnabled"] = false;
    g_csUsb4Enabled.store(false, std::memory_order_release);
    csSaveLocked();
    const bool ok = csRestoreAllLocked();
    if (g_hwnd) PostMessageW(g_hwnd, WM_CS_TIMER_SYNC, 0, 0);
    return json{{"ok", ok}, {"status", csStatusLocked()}};
}

static json csSetUsb4Hard(bool on) {
    std::lock_guard<std::mutex> lock(g_csMtx);
    if (on && !g_csState.value("mainEnabled", false))
        return json{{"ok", false}, {"error", "请先开启完全可控睡眠"}, {"status", csStatusLocked()}};
    if (on) {
        const auto nodes = csEnumeratePnp();
        const auto hosts = csUsb4Hosts(nodes);
        if (csUsb4MouseConflict(nodes, hosts)) {
            g_csState["usb4MouseConflict"] = true;
            csRecordErrorLocked("鼠标位于 USB4 设备树内，请将鼠标移到普通 USB 接口后再开启硬拦截");
            csSaveLocked();
            return json{{"ok", false}, {"error", g_csState["lastError"]}, {"status", csStatusLocked()}};
        }
        g_csState["usb4HardBlockEnabled"] = true;
        g_csUsb4Enabled.store(true, std::memory_order_release);
        csSaveLocked();
        const bool ok = csApplyUsb4Locked();
        csSaveLocked();
        return json{{"ok", ok}, {"status", csStatusLocked()}};
    }
    g_csState["usb4HardBlockEnabled"] = false;
    g_csUsb4Enabled.store(false, std::memory_order_release);
    csSaveLocked();
    const bool ok = csRestoreUsb4Locked();
    csSaveLocked();
    return json{{"ok", ok}, {"status", csStatusLocked()}};
}

static json csRestoreUsb4Emergency() {
    std::lock_guard<std::mutex> lock(g_csMtx);
    g_csState["usb4HardBlockEnabled"] = false;
    g_csUsb4Enabled.store(false, std::memory_order_release);
    const bool ok = csRestoreUsb4Locked();
    csSaveLocked();
    return json{{"ok", ok}, {"status", csStatusLocked()}};
}

#endif

// ── 异步白名单：只放「无 UI、无全局可变状态、纯本地计算/IO」的命令 ──
static bool ipc_cmd_async(const std::string& cmd) {
    if (g_asyncMode <= 0) return false;
    // 模式 1：仅 shell.run —— powercfg/schtasks 等全部子进程的唯一入口，
    // 占 UI 线程阻塞成本的 95% 以上；handler 为纯函数（只用局部变量）。
    static const std::unordered_set<std::string> lvl1 = {
        "shell.run",
    };
    // 模式 2：扩展白名单（读 + 写 + 进程启动，均为独立文件/系统叶子操作，无跨命令共享可变状态）。
    // 把 fs.* 全部(含写)、shell.hidden、shell.execute 也 offload，使浮动调度相关的所有 IPC
    // 都不在 UI 线程同步阻塞 —— 消除「前端每秒同步读守护写的 fps-status.json」造成的消息泵冻结
    // （鼠标旁 IDC_APPSTARTING 转圈的根因）。shell.run 已在 lvl1 异步。
    static const std::unordered_set<std::string> lvl2 = {
        "fs.readTextFile", "fs.readTextRange", "fs.readBinaryFile",
        "fs.writeTextFile", "fs.writeTextFileAtomic", "fs.writeBinaryFile", "fs.exists", "fs.readDir", "fs.stat",
        "fs.remove", "fs.mkdir", "fs.rename", "fs.copyFile",
        "background.get", "background.install", "background.clear",
        "dynamicBackground.get", "dynamicBackground.installUrl", "dynamicBackground.clear",
        "registry.read", "registry.exists", "http.request", "smt.get", "smt.set",
        "shell.hidden", "shell.execute", "tdpDaemon.start", "tdpDaemon.request",
        "app.checkUpdate", "app.downloadUpdate", "app.installUpdate",
        // Process enumeration, working-set queries, image-path lookup and
        // window-title enumeration can all stall under system pressure.
        // Keep the WebView2/UI message pump out of this path.
        "game.detect",
        // NtSuspendProcess/NtResumeProcess and marker IO must never run on
        // the WebView2 owner thread. A frozen game can leave window/renderer
        // callbacks pending even though the native call itself is PID-only.
        "game.suspend", "game.resume",
    };
    if (lvl1.count(cmd)) return true;
    if (g_asyncMode >= 2 && lvl2.count(cmd)) return true;
    return false;
}

// ── 消息泵冻结监视（仅 trace 模式）：从后台线程 SendMessageTimeout 测
//    WM_NULL 被泵处理的延迟；≥100ms 记为一次冻结。──
static DWORD WINAPI freezeMonitorThread(LPVOID) {
    for (;;) {
        HWND h = g_hwnd;
        if (h) {
            ULONGLONG t0 = GetTickCount64();
            DWORD_PTR r = 0;
            SendMessageTimeoutW(h, WM_NULL, 0, 0, SMTO_NORMAL, 10000, &r);
            ULONGLONG dt = GetTickCount64() - t0;
            if (dt >= 100) traceLog("FREEZE %llums", (unsigned long long)dt);
        }
        Sleep(50);
    }
    return 0;
}

// ================================================================
//  IPC bridge
// ================================================================

using IpcFn = std::function<json(const json&)>;
static std::unordered_map<std::string, IpcFn> g_cmds;

struct IpcResultEnvelope {
    unsigned long long generation = 0;
    json response;
};

static void ipc_on(const std::string& cmd, IpcFn fn) {
    g_cmds[cmd] = std::move(fn);
}

static void ipc_emit(const std::string& ev, const json& data = {}) {
    if (!g_view) return;
    json m = {{"event", ev}, {"data", data}};
    g_view->PostWebMessageAsJson(U2W(m.dump()).c_str());
}

static std::wstring updateProgressPath() {
    return app_data_dir() + L"\\update\\update-progress.json";
}

static void updateProgressPost(const json& data) {
    {
        std::lock_guard<std::mutex> lock(g_updateProgressMtx);
        g_updateProgress = data;
        sgWriteFileAtomic(updateProgressPath(), data.dump());
    }
    if (!g_hwnd || !IsWindow(g_hwnd)) return;
    auto* heap = new json(data);
    if (!PostMessageW(g_hwnd, WM_UPDATE_PROGRESS, 0, reinterpret_cast<LPARAM>(heap)))
        delete heap;
}

static json updateProgressRead() {
    {
        std::lock_guard<std::mutex> lock(g_updateProgressMtx);
        if (!g_updateProgress.empty()) return g_updateProgress;
    }
    const auto raw = sgReadFile(updateProgressPath());
    if (raw.empty()) return json::object();
    try {
        auto parsed = json::parse(raw);
        return parsed.is_object() ? parsed : json::object();
    } catch (...) {
        return json::object();
    }
}

static void ipc_dispatch(LPCWSTR raw) {
    try {
        auto req = json::parse(W2U(raw));
        const auto generation = g_webviewGeneration.load(std::memory_order_acquire);
        json resp;
        resp["id"] = req.value("id", -1);
        auto cmd  = req.value("cmd", std::string{});
        auto args = req.value("args", json::object());
        const bool traceRenderHandshake =
            cmd == "window.renderContext" || cmd == "window.renderReady";
        if (traceRenderHandshake)
            traceLog("IPC render-handshake dispatch cmd=%s", cmd.c_str());

        // Permission check
        if (!g_permissions.empty()) {
            auto it = g_permissions.find(cmd);
            if (it == g_permissions.end()) {
                // Check wildcard: "fs.*" matches "fs.readTextFile"
                auto dot = cmd.find('.');
                if (dot != std::string::npos) {
                    auto ns = cmd.substr(0, dot) + ".*";
                    it = g_permissions.find(ns);
                }
            }
            if (it != g_permissions.end() && !it->second) {
                resp["error"] = "permission denied: " + cmd;
                g_view->PostWebMessageAsJson(U2W(resp.dump()).c_str());
                return;
            }
        }

        auto it = g_cmds.find(cmd);
        if (it == g_cmds.end()) {
            resp["error"] = "unknown: " + cmd;
            g_view->PostWebMessageAsJson(U2W(resp.dump()).c_str());
            return;
        }

        // ── 异步路径：白名单命令 offload 到 worker 线程池，避免 UI 线程
        //    同步等子进程/慢 IO。g_cmds 启动后只读，复制 IpcFn 跨线程安全。──
        // Focus state belongs to the native UI thread. Clear it before the
        // process operation is queued so no timer can try to refocus a game
        // while that PID is being suspended or resumed.
        const bool gameControlCommand = cmd == "game.suspend" || cmd == "game.resume";
        if (gameControlCommand) {
            focusClearSession();
            g_gameControlInFlight.store(true, std::memory_order_release);
        }

        if (ipc_cmd_async(cmd)) {
            IpcFn fn = it->second;
            ULONGLONG tq = GetTickCount64();
            const bool queued = poolSubmit([resp, args, fn = std::move(fn), cmd, tq, generation, gameControlCommand]() mutable {
                ULONGLONG ts = GetTickCount64();
                try { resp["result"] = fn(args); }
                catch (const std::exception& e) { resp["error"] = e.what(); }
                catch (...) { resp["error"] = "native error: " + cmd; }
                if (gameControlCommand)
                    g_gameControlInFlight.store(false, std::memory_order_release);
                traceLog("IPC %-28s pool wait=%llums exec=%llums", cmd.c_str(),
                         (unsigned long long)(ts - tq),
                         (unsigned long long)(GetTickCount64() - ts));
                // WebView2 仅允许 UI 线程调用 → 结果经 WM_IPC_RESULT 回传
                //（对齐 watcher 的 WM_FILE_CHANGED 堆分配 + WndProc delete 模式）
                auto* heap = new IpcResultEnvelope{generation, std::move(resp)};
                if (!PostMessageW(g_hwnd, WM_IPC_RESULT, 0, (LPARAM)heap))
                    delete heap; // 窗口已销毁（进程退出中）
            });
            if (!queued) {
                if (gameControlCommand)
                    g_gameControlInFlight.store(false, std::memory_order_release);
                resp["error"] = "IPC worker queue is full or stopping";
                g_view->PostWebMessageAsJson(U2W(resp.dump()).c_str());
            }
            return;
        }

        // ── 同步路径（原行为）──
        ULONGLONG ts = GetTickCount64();
        try { resp["result"] = it->second(args); }
        catch (const std::exception& e) { resp["error"] = e.what(); }
        catch (...) { resp["error"] = "native error: " + cmd; }
        if (gameControlCommand)
            g_gameControlInFlight.store(false, std::memory_order_release);
        if (traceRenderHandshake)
            traceLog("IPC render-handshake response cmd=%s hasError=%d",
                     cmd.c_str(), resp.contains("error") ? 1 : 0);
        if (g_traceOn.load(std::memory_order_relaxed)) {
            ULONGLONG dt = GetTickCount64() - ts;
            if (dt >= 1) traceLog("IPC %-28s ui   exec=%llums", cmd.c_str(), (unsigned long long)dt);
        }
        g_view->PostWebMessageAsJson(U2W(resp.dump()).c_str());
    } catch (...) {}
}

static bool windowAnimationsEnabled() {
    ANIMATIONINFO ai{sizeof(ai)};
    if (SystemParametersInfoW(SPI_GETANIMATION, sizeof(ai), &ai, 0))
        return ai.iMinAnimate != 0;
    return true;
}

static void enableWindowTransitions(HWND hwnd) {
    BOOL disabled = FALSE;
    DwmSetWindowAttribute(hwnd, 3, &disabled, sizeof(disabled)); // DWMWA_TRANSITIONS_FORCEDISABLED
}

static void showWindowAnimated(HWND hwnd, int showCmd, bool activate) {
    if (!hwnd) return;

    // 非常驻时确保任务栏按钮不出现：WS_EX_APPWINDOW 窗口在显示后可能被 shell 重新加入任务栏。
    if (hwnd == g_hwnd && !g_taskbarResident) {
        ComPtr<ITaskbarList3> tb2;
        if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&tb2))) && SUCCEEDED(tb2->HrInit()))
            tb2->DeleteTab(hwnd);
    }

    // Hiding is explicit: just hide, no animation/activation.
    if (showCmd == SW_HIDE) {
        hideWindowAnimated(hwnd);
        return;
    }

    if (hwnd == g_hwnd &&
        (g_deferFirstShow ||
         (g_webviewRecoveryInProgress &&
          (!g_ctrl || !g_webviewReady || !IsWindowVisible(hwnd))))) {
        // A tray/gamepad restore can race a controller recreation. Keep the
        // transparent native shell hidden until NavigationCompleted confirms
        // a new compositor frame; otherwise only the DWM shadow is visible.
        g_deferFirstShow = true;
        g_firstShowCmd = showCmd;
        if (g_ctrl) g_ctrl->put_IsVisible(TRUE);
        return;
    }

    // Minimizing must never grab foreground focus: calling SetForegroundWindow()
    // right after ShowWindow(SW_MINIMIZE) makes Windows restore the window to the
    // foreground a moment later (esp. at startup, when no window owns focus yet).
    bool isMinimize = (showCmd == SW_MINIMIZE);

    if (showCmd == SW_MINIMIZE || showCmd == SW_MAXIMIZE || showCmd == SW_RESTORE || IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, showCmd);
    } else if (windowAnimationsEnabled()) {
        DWORD flags = AW_BLEND | (activate ? AW_ACTIVATE : 0);
        if (!AnimateWindow(hwnd, 120, flags))
            ShowWindow(hwnd, showCmd);
    } else {
        ShowWindow(hwnd, showCmd);
    }

    if (hwnd == g_hwnd && g_ctrl)
        // A minimized/hidden native window must not leave the WebView2
        // controller visible.  Chromium treats an active media element in a
        // visible controller as a Video Wake Lock even when the host window is
        // in the tray, which keeps handhelds awake and wastes power.
        g_ctrl->put_IsVisible((IsWindowVisible(hwnd) && !IsIconic(hwnd)) || g_deferFirstShow);

    if (hwnd == g_hwnd && !isMinimize && g_webviewReady &&
        IsWindowVisible(hwnd) && !IsIconic(hwnd)) {
        // Every hidden/minimized -> visible path gets one compositor refresh.
        // This covers tray/taskbar restore, gamepad summon, Steam Big Picture
        // display transitions, and a second-launch activation request.
        g_webviewNeedsShowNudge = true;
        KillTimer(g_hwnd, WEBVIEW_POST_SHOW_NUDGE_TIMER_ID);
        SetTimer(g_hwnd, WEBVIEW_POST_SHOW_NUDGE_TIMER_ID, 60, nullptr);
    }

    if (hwnd == g_hwnd && !isMinimize)
        ipc_emit("window.shown");

    if (activate && !isMinimize)
        SetForegroundWindow(hwnd);
}

static void hideWindowAnimated(HWND hwnd) {
    if (!hwnd) return;

    const bool isMainWindow = hwnd == g_hwnd;
    if (isMainWindow) {
        if (!g_focusSession.returning && g_focusSession.active)
            focusClearSession();
        g_webviewNeedsShowNudge = true;
        if (g_ctrl) g_ctrl->put_IsVisible(FALSE);
    }
    // The main WebView host must change visibility atomically. AnimateWindow
    // composites a transparent native shell while WebView2 is changing its
    // controller visibility, which is the source of the intermittent flash
    // on minimize/restore. Child tool windows may keep their animation.
    if (!isMainWindow && IsWindowVisible(hwnd) && windowAnimationsEnabled()) {
        if (AnimateWindow(hwnd, 100, AW_HIDE | AW_BLEND)) {
            if (isMainWindow) ipc_emit("window.hidden");
            return;
        }

    }
    ShowWindow(hwnd, SW_HIDE);
    if (isMainWindow) ipc_emit("window.hidden");
}


static bool followsSystemTheme() {
    return g_cfg.value("/window/followSystemTheme"_json_pointer, false);
}

static bool systemUsesDarkMode() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) != ERROR_SUCCESS) return true;
    DWORD val = 0, sz = sizeof(val);
    if (RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr, (BYTE*)&val, &sz) != ERROR_SUCCESS)
        val = 0;
    RegCloseKey(hKey);
    return val == 0; // 0 = dark mode
}

static std::string colorToHex(COLORREF clr) {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", GetRValue(clr), GetGValue(clr), GetBValue(clr));
    return buf;
}

static COLORREF systemAccentColor() {
    DWORD color = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque))) {
        return RGB((color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff);
    }

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD sz = sizeof(color);
        if (RegQueryValueExW(hKey, L"ColorizationColor", nullptr, nullptr, (BYTE*)&color, &sz) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return RGB((color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff);
        }
        RegCloseKey(hKey);
    }
    return RGB(0, 120, 212);
}

static COLORREF currentWindowBackgroundColor() {
    auto winCfg = g_cfg.value("window", json::object());
    if (followsSystemTheme()) {
        auto key = systemUsesDarkMode() ? "darkBackgroundColor" : "lightBackgroundColor";
        auto fallback = systemUsesDarkMode()
            ? winCfg.value("backgroundColor", std::string{"#1a1a2e"})
            : std::string{"#f6f6f9"};
        return parseHexColor(winCfg.value(key, fallback));
    }
    return parseHexColor(winCfg.value("backgroundColor", std::string{"#1a1a2e"}));
}

static json systemThemeInfo() {
    bool dark = systemUsesDarkMode();
    COLORREF bg = currentWindowBackgroundColor();
    COLORREF fg = dark ? RGB(238, 238, 238) : RGB(32, 32, 36);
    return {
        {"dark", dark},
        {"accentColor", colorToHex(systemAccentColor())},
        {"backgroundColor", colorToHex(bg)},
        {"foregroundColor", colorToHex(fg)},
    };
}

static void applyNativeTheme() {
    if (!g_hwnd || !followsSystemTheme()) return;

    g_bgClr = currentWindowBackgroundColor();
    BOOL darkMode = systemUsesDarkMode() ? TRUE : FALSE;
    DwmSetWindowAttribute(g_hwnd, 20, &darkMode, sizeof(darkMode));

    if (g_frameless) {
        if (g_effectType >= 2) {
            // 磨砂玻璃：边框/标题色必须 COLOR_NONE，否则 DWM 用实色填满扩展区 → 不透明。
            COLORREF none = 0xFFFFFFFE; // DWMWA_COLOR_NONE
            DwmSetWindowAttribute(g_hwnd, 34, &none, sizeof(none));
            DwmSetWindowAttribute(g_hwnd, 35, &none, sizeof(none));
        } else {
            DwmSetWindowAttribute(g_hwnd, 34, &g_bgClr, sizeof(g_bgClr));
            DwmSetWindowAttribute(g_hwnd, 35, &g_bgClr, sizeof(g_bgClr));
        }
    } else {
        COLORREF border = darkMode ? RGB(64, 70, 82) : RGB(218, 221, 227);
        const COLORREF captionDefault = 0xFFFFFFFF; // DWMWA_COLOR_DEFAULT
        DwmSetWindowAttribute(g_hwnd, 34, &border, sizeof(border));
        DwmSetWindowAttribute(g_hwnd, 35, &captionDefault, sizeof(captionDefault));
    }

    if (g_bgBrush) DeleteObject(g_bgBrush);
    g_bgBrush = CreateSolidBrush(g_bgClr);
    InvalidateRect(g_hwnd, nullptr, TRUE);

    if (g_ctrl) {
        ComPtr<ICoreWebView2Controller2> ctrl2;
        if (SUCCEEDED(g_ctrl.As(&ctrl2))) {
            BYTE alpha = g_frameless ? 0 : 255;
            ctrl2->put_DefaultBackgroundColor({
                alpha, GetRValue(g_bgClr), GetGValue(g_bgClr), GetBValue(g_bgClr)
            });
        }
    }
}

// ================================================================
//  Commands: Window
// ================================================================

struct DisplayModeInfo {
    std::string id;
    int width = 0;
    int height = 0;
    int refresh = 0;
    int orientation = 0;
};

// Resolution choices must follow the direction of the currently active desktop
// mode. Do not use dmDisplayOrientation alone: a portrait panel can be rotated
// into landscape while the driver still reports a rotated mode flag.
static bool sameDisplayDirection(int width, int height, int currentWidth, int currentHeight) {
    if (width <= 0 || height <= 0 || currentWidth <= 0 || currentHeight <= 0) return true;
    const bool currentLandscape = currentWidth >= currentHeight;
    return (width >= height) == currentLandscape;
}

static bool currentDisplayDevice(std::wstring& device, DEVMODEW& current) {
    if (!g_hwnd) return false;
    HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi{sizeof(mi)};
    if (!GetMonitorInfoW(mon, &mi)) return false;
    device = mi.szDevice;
    current = DEVMODEW{};
    current.dmSize = sizeof(current);
    return EnumDisplaySettingsExW(device.c_str(), ENUM_CURRENT_SETTINGS, &current, 0) != FALSE;
}

static std::string displayModeId(int width, int height, int refresh, int orientation) {
    return std::to_string(width) + "x" + std::to_string(height) + "@" +
           std::to_string(refresh) + "/" + std::to_string(orientation);
}

static std::vector<DisplayModeInfo> enumerateCurrentDisplayModes(std::wstring* deviceOut = nullptr,
                                                                   DEVMODEW* currentOut = nullptr) {
    std::wstring device;
    DEVMODEW current{};
    std::vector<DisplayModeInfo> modes;
    if (!currentDisplayDevice(device, current)) return modes;
    if (deviceOut) *deviceOut = device;
    if (currentOut) *currentOut = current;
    std::unordered_set<std::string> seen;
    for (DWORD i = 0;; i++) {
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsExW(device.c_str(), i, &dm, EDS_ROTATEDMODE)) break;
        if (dm.dmPelsWidth == 0 || dm.dmPelsHeight == 0) continue;
        if (!sameDisplayDirection((int)dm.dmPelsWidth, (int)dm.dmPelsHeight,
                                  (int)current.dmPelsWidth, (int)current.dmPelsHeight)) continue;
        const int hz = dm.dmDisplayFrequency > 1 ? (int)dm.dmDisplayFrequency :
                       (current.dmDisplayFrequency > 1 ? (int)current.dmDisplayFrequency : 60);
        const int orientation = (int)dm.dmDisplayOrientation;
        const std::string id = displayModeId((int)dm.dmPelsWidth, (int)dm.dmPelsHeight, hz, orientation);
        if (!seen.insert(id).second) continue;
        modes.push_back({id, (int)dm.dmPelsWidth, (int)dm.dmPelsHeight, hz, orientation});
    }
    std::sort(modes.begin(), modes.end(), [](const DisplayModeInfo& a, const DisplayModeInfo& b) {
        if (a.width != b.width) return a.width < b.width;
        if (a.height != b.height) return a.height < b.height;
        if (a.refresh != b.refresh) return a.refresh < b.refresh;
        return a.orientation < b.orientation;
    });
    return modes;
}

static void reg_display() {
    ipc_on("display.getModes", [](const json&) -> json {
        std::wstring device;
        DEVMODEW current{};
        auto modes = enumerateCurrentDisplayModes(&device, &current);
        const std::string currentId = displayModeId((int)current.dmPelsWidth, (int)current.dmPelsHeight,
            current.dmDisplayFrequency > 1 ? (int)current.dmDisplayFrequency : 60,
            (int)current.dmDisplayOrientation);
        json list = json::array();
        for (const auto& m : modes) {
            list.push_back({{"id", m.id}, {"width", m.width}, {"height", m.height},
                            {"refresh", m.refresh}, {"orientation", m.orientation}});
        }
        return {{"current", currentId}, {"modes", list}};
    });
    ipc_on("display.setMode", [](const json& a) -> json {
        const int width = a.value("width", 0);
        const int height = a.value("height", 0);
        const int refresh = a.value("refresh", 0);
        const int orientation = a.value("orientation", 0);
        if (width <= 0 || height <= 0 || refresh <= 0) throw std::runtime_error("Invalid display mode");
        std::wstring device;
        DEVMODEW current{};
        const auto modes = enumerateCurrentDisplayModes(&device, &current);
        const auto it = std::find_if(modes.begin(), modes.end(), [&](const DisplayModeInfo& m) {
            return m.width == width && m.height == height && m.refresh == refresh && m.orientation == orientation;
        });
        if (it == modes.end()) throw std::runtime_error("Display mode is not supported by the current monitor");
        DEVMODEW dm = current;
        dm.dmPelsWidth = (DWORD)it->width;
        dm.dmPelsHeight = (DWORD)it->height;
        dm.dmDisplayFrequency = (DWORD)it->refresh;
        dm.dmDisplayOrientation = (DWORD)it->orientation;
        dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_DISPLAYORIENTATION;
        LONG test = ChangeDisplaySettingsExW(device.c_str(), &dm, nullptr, CDS_TEST, nullptr);
        if (test != DISP_CHANGE_SUCCESSFUL) throw std::runtime_error("Display mode test failed");
        const LONG result = ChangeDisplaySettingsExW(device.c_str(), &dm, nullptr,
            CDS_UPDATEREGISTRY | CDS_RESET, nullptr);
        if (result != DISP_CHANGE_SUCCESSFUL) throw std::runtime_error("Display mode change failed");
        return {{"id", it->id}, {"width", it->width}, {"height", it->height},
                {"refresh", it->refresh}, {"orientation", it->orientation}};
    });
}

static void reg_window() {
    ipc_on("window.setTitle", [](const json& a) -> json {
        SetWindowTextW(g_hwnd, U2W(a.value("title", std::string{})).c_str());
        return true;
    });
    ipc_on("window.minimize", [](const json&) -> json {
        PostMessageW(g_hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        return true;
    });
    ipc_on("window.maximize", [](const json&) -> json {
        WINDOWPLACEMENT wp{sizeof(wp)};
        GetWindowPlacement(g_hwnd, &wp);
        if (!IsWindowVisible(g_hwnd)) {
            showWindowAnimated(g_hwnd, SW_MAXIMIZE);
            return true;
        }
        PostMessageW(g_hwnd, WM_SYSCOMMAND, wp.showCmd == SW_MAXIMIZE ? SC_RESTORE : SC_MAXIMIZE, 0);
        return true;
    });
    ipc_on("window.restore", [](const json&) -> json {
        if (!IsWindowVisible(g_hwnd)) {
            showWindowAnimated(g_hwnd, SW_RESTORE);
            return true;
        }
        PostMessageW(g_hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
        return true;
    });
    ipc_on("window.close", [](const json&) -> json {
        PostMessageW(g_hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
        return true;
    });
    ipc_on("window.getState", [](const json&) -> json {
        return {
            {"visible", g_hwnd && IsWindowVisible(g_hwnd) != FALSE},
            {"minimized", g_hwnd && IsIconic(g_hwnd) != FALSE}
        };
    });
    // The frontend obtains the document identity before signalling its first
    // painted frame.  Keep both values as strings so the 64-bit WebView2
    // navigation id is never rounded by JavaScript's Number representation.
    ipc_on("window.renderContext", [](const json&) -> json {
        return {
            {"generation", std::to_string(g_webviewGeneration.load(std::memory_order_acquire))},
            {"navigationId", std::to_string(g_webviewNavigationId)}
        };
    });
    ipc_on("window.renderReady", [](const json& a) -> json {
        unsigned long long generation = 0;
        UINT64 navigationId = 0;
        try {
            generation = std::stoull(a.value("generation", std::string{}));
            navigationId = static_cast<UINT64>(std::stoull(a.value("navigationId", std::string{})));
        } catch (...) {
            return json{{"ok", false}, {"reason", "invalid-render-context"}};
        }

        const auto current = g_webviewGeneration.load(std::memory_order_acquire);
        if (generation != current || navigationId == 0 || navigationId != g_webviewNavigationId) {
            appendWebViewDiagnostic({
                {"event", "render-ready-stale"},
                {"generation", generation},
                {"currentGeneration", current},
                {"navigationId", std::to_string(navigationId)},
                {"currentNavigationId", std::to_string(g_webviewNavigationId)}
            });
            return json{{"ok", false}, {"reason", "stale-render-context"}};
        }

        g_webviewRenderReadyGeneration = generation;
        g_webviewRenderReadyNavigationId = navigationId;
        appendWebViewDiagnostic({
            {"event", "render-ready-signal"},
            {"generation", generation},
            {"navigationId", std::to_string(navigationId)},
            {"navigationComplete", g_webviewNavigationReady}
        });
        if (g_webviewNavigationReady && g_webviewCompletedNavigationId == navigationId)
            finalizeWebViewRenderReady(generation, navigationId, "frontend");
        return json{{"ok", true}, {"pendingNavigation", !g_webviewNavigationReady}};
    });
    ipc_on("window.show", [](const json&) -> json {
        showWindowAnimated(g_hwnd, IsIconic(g_hwnd) ? SW_RESTORE : SW_SHOW);
        refocusWebView();
        return true;
    });
    ipc_on("window.hide", [](const json&) -> json {
        hideWindowAnimated(g_hwnd);
        return true;
    });
    ipc_on("window.size", [](const json&) -> json {
        RECT r; GetClientRect(g_hwnd, &r);
        return {{"w", r.right}, {"h", r.bottom}};
    });
    ipc_on("window.setSize", [](const json& a) -> json {
        int w = a.value("w", 0), h = a.value("h", 0);
        if (w <= 0 || h <= 0) return false;
        RECT cr, wr;
        GetClientRect(g_hwnd, &cr);
        GetWindowRect(g_hwnd, &wr);
        int fw = (wr.right - wr.left) - cr.right + w;
        int fh = (wr.bottom - wr.top) - cr.bottom + h;
        SetWindowPos(g_hwnd, nullptr, 0, 0, fw, fh, SWP_NOMOVE | SWP_NOZORDER);
        return true;
    });
    ipc_on("window.position", [](const json&) -> json {
        RECT r; GetWindowRect(g_hwnd, &r);
        return {{"x", r.left}, {"y", r.top}};
    });
    ipc_on("window.setPosition", [](const json& a) -> json {
        SetWindowPos(g_hwnd, nullptr, a.value("x", 0), a.value("y", 0), 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER);
        return true;
    });
    ipc_on("window.center", [](const json&) -> json {
        RECT wr; GetWindowRect(g_hwnd, &wr);
        int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
        HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(mon, &mi);
        int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - ww) / 2;
        int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - wh) / 2;
        SetWindowPos(g_hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        return true;
    });
    ipc_on("window.setAlwaysOnTop", [](const json& a) -> json {
        HWND z = a.value("top", true) ? HWND_TOPMOST : HWND_NOTOPMOST;
        SetWindowPos(g_hwnd, z, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        return true;
    });
    ipc_on("window.isMaximized", [](const json&) -> json {
        WINDOWPLACEMENT wp{sizeof(wp)};
        GetWindowPlacement(g_hwnd, &wp);
        return wp.showCmd == SW_MAXIMIZE;
    });
    ipc_on("window.setEffect", [](const json& a) -> json {
        auto name = a.value("effect", std::string{"none"});
        g_effectType = parseEffectType(name);
        applyWindowEffect(g_hwnd, g_effectType);
        return true;
    });
    ipc_on("window.setBackgroundColor", [](const json& a) -> json {
        auto hex = a.value("color", std::string{});
        if (hex.empty()) return false;
        COLORREF clr = parseHexColor(hex);
        // Update DWM border + caption colors (via shared helper)
        if (g_frameless) {
            g_bgClr = clr;
            applyFramelessDwmAttrs();
            if (g_bgBrush) DeleteObject(g_bgBrush);
            g_bgBrush = CreateSolidBrush(clr);
            InvalidateRect(g_hwnd, nullptr, TRUE);
        }
        // Update WebView2 default background
        if (g_ctrl) {
            ComPtr<ICoreWebView2Controller2> ctrl2;
            if (SUCCEEDED(g_ctrl.As(&ctrl2))) {
            BYTE alpha = g_frameless ? 0 : 255;
            ctrl2->put_DefaultBackgroundColor({alpha, GetRValue(clr), GetGValue(clr), GetBValue(clr)});
            }
        }
        return true;
    });
}

// ================================================================
//  Commands: Dialogs
// ================================================================

static json show_file_dialog(bool save, const json& a) {
    ComPtr<IFileDialog> dlg;
    HRESULT hr;
    if (save)
        hr = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dlg));
    else
        hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dlg));
    if (FAILED(hr)) throw std::runtime_error("Failed to create file dialog");

    FILEOPENDIALOGOPTIONS opts;
    dlg->GetOptions(&opts);
    bool multi  = !save && a.value("multiple", false);
    bool folder = a.value("folder", false);
    if (multi)  opts |= FOS_ALLOWMULTISELECT;
    if (folder) opts |= FOS_PICKFOLDERS;
    dlg->SetOptions(opts);

    // Filters
    std::vector<COMDLG_FILTERSPEC> specs;
    std::vector<std::wstring> names, pats;
    if (a.contains("filters") && a["filters"].is_array()) {
        for (auto& f : a["filters"]) {
            names.push_back(U2W(f.value("name", "")));
            std::string p;
            for (auto& ext : f["extensions"]) {
                if (!p.empty()) p += ";";
                auto e = ext.get<std::string>();
                p += (e == "*") ? "*.*" : ("*." + e);
            }
            pats.push_back(U2W(p));
        }
        for (size_t i = 0; i < names.size(); i++)
            specs.push_back({names[i].c_str(), pats[i].c_str()});
        dlg->SetFileTypes((UINT)specs.size(), specs.data());
    }

    if (a.contains("defaultName"))
        dlg->SetFileName(U2W(a["defaultName"].get<std::string>()).c_str());

    if (FAILED(dlg->Show(g_hwnd))) return nullptr; // cancelled

    if (multi) {
        ComPtr<IFileOpenDialog> od;
        dlg.As(&od);
        ComPtr<IShellItemArray> items;
        od->GetResults(&items);
        DWORD count; items->GetCount(&count);
        json arr = json::array();
        for (DWORD i = 0; i < count; i++) {
            ComPtr<IShellItem> item;
            items->GetItemAt(i, &item);
            LPWSTR path; item->GetDisplayName(SIGDN_FILESYSPATH, &path);
            arr.push_back(W2U(path));
            CoTaskMemFree(path);
        }
        return arr;
    } else {
        ComPtr<IShellItem> item;
        dlg->GetResult(&item);
        LPWSTR path; item->GetDisplayName(SIGDN_FILESYSPATH, &path);
        auto result = W2U(path);
        CoTaskMemFree(path);
        return result;
    }
}

static void reg_dialog() {
    ipc_on("dialog.openFile", [](const json& a) -> json {
        return show_file_dialog(false, a);
    });
    ipc_on("dialog.saveFile", [](const json& a) -> json {
        return show_file_dialog(true, a);
    });
    ipc_on("dialog.openFolder", [](const json& a) -> json {
        json arg = a.is_null() ? json::object() : a;
        arg["folder"] = true;
        return show_file_dialog(false, arg);
    });
    ipc_on("dialog.message", [](const json& a) -> json {
        auto title   = a.value("title", std::string{"Message"});
        auto message = a.value("message", std::string{});
        auto type    = a.value("type", std::string{"info"});
        UINT flags = MB_OK;
        if (type == "warning")     flags |= MB_ICONWARNING;
        else if (type == "error")  flags |= MB_ICONERROR;
        else                       flags |= MB_ICONINFORMATION;
        MessageBoxW(g_hwnd, U2W(message).c_str(), U2W(title).c_str(), flags);
        return true;
    });
    ipc_on("dialog.confirm", [](const json& a) -> json {
        auto title   = a.value("title", std::string{"Confirm"});
        auto message = a.value("message", std::string{});
        return MessageBoxW(g_hwnd, U2W(message).c_str(), U2W(title).c_str(),
                           MB_YESNO | MB_ICONQUESTION) == IDYES;
    });
}

// ================================================================
//  Commands: File system
// ================================================================

static void reg_fs() {
    ipc_on("fs.readTextFile", [](const json& a) -> json {
        auto path = a.value("path", std::string{});
        std::ifstream f(U2W(path), std::ios::binary);
        if (!f) throw std::runtime_error("Cannot open: " + path);
        return std::string((std::istreambuf_iterator<char>(f)), {});
    });
    // Range read for large/growing files: returns a window [offset, offset+maxBytes) plus the
    // file's total size, so a frontend can poll a growing log without re-reading the whole file.
    ipc_on("fs.readTextRange", [](const json& a) -> json {
        auto path = a.value("path", std::string{});
        uint64_t offset = a.value("offset", uint64_t{0});
        uint64_t maxBytes = a.value("maxBytes", uint64_t{0});

        std::ifstream f(U2W(path), std::ios::binary | std::ios::ate);
        if (!f) throw std::runtime_error("Cannot open: " + path);

        auto endPos = f.tellg();
        if (endPos < 0) throw std::runtime_error("Cannot stat: " + path);
        uint64_t size = static_cast<uint64_t>(endPos);
        if (offset > size) offset = size;

        uint64_t toRead = size - offset;
        if (maxBytes > 0 && toRead > maxBytes) toRead = maxBytes;
        if (toRead > static_cast<uint64_t>((std::numeric_limits<std::streamsize>::max)()))
            throw std::runtime_error("Requested range is too large");

        std::string content;
        content.resize(static_cast<size_t>(toRead));
        f.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (toRead > 0) {
            f.read(content.data(), static_cast<std::streamsize>(toRead));
            content.resize(static_cast<size_t>(f.gcount()));
        }
        return {{"content", content}, {"offset", offset}, {"size", size}};
    });
    ipc_on("fs.writeTextFile", [](const json& a) -> json {
        auto path    = a.value("path", std::string{});
        auto content = a.value("content", std::string{});
        std::ofstream f(U2W(path), std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("Cannot write: " + path);
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
        f.flush();
        if (!f.good()) throw std::runtime_error("Write failed: " + path);
        f.close();
        if (f.fail()) throw std::runtime_error("Close failed: " + path);
        return true;
    });
    // 原子写：先写临时文件再 MoveFileEx/ReplaceFileW 替换，避免截断式写被 RTSS 并发读取时读到半截（损坏根因）。
    // ⚠ 原子替换失败时绝不回退到截断式写（sgWriteFile）——那会把正在被 RTSS 读取的配置文件写半截，
    // 导致 RTSS 解析崩溃 / 配置永久损坏。此处保留原文件、返回 false，由调用方决定重试。
    ipc_on("fs.writeTextFileAtomic", [](const json& a) -> json {
        auto path    = a.value("path", std::string{});
        auto content = a.value("content", std::string{});
        std::wstring wp = U2W(path);
        return sgWriteFileAtomic(wp, content);
    });
    ipc_on("settings.write", [](const json& a) -> json {
        if (!sameWindowsPath(U2W(a.value("path", std::string{})), YM_SETTINGS_FILE))
            throw std::runtime_error("Invalid unified settings path");
        return ymSettingsWriteDocument(a.value("content", std::string{}));
    });
    ipc_on("fs.exists", [](const json& a) -> json {
        return fspath::exists(U2W(a.value("path", std::string{})));
    });
    ipc_on("fs.readDir", [](const json& a) -> json {
        auto path = a.value("path", std::string{});
        json entries = json::array();
        for (auto& e : fspath::directory_iterator(U2W(path))) {
            entries.push_back({
                {"name",   W2U(e.path().filename().wstring())},
                {"isDir",  e.is_directory()},
                {"isFile", e.is_regular_file()},
            });
        }
        return entries;
    });
    ipc_on("fs.mkdir", [](const json& a) -> json {
        fspath::create_directories(U2W(a.value("path", std::string{})));
        return true;
    });
    ipc_on("fs.remove", [](const json& a) -> json {
        auto path = a.value("path", std::string{});
        if (is_dangerous_remove_target(path))
            throw std::runtime_error("Refusing to remove root/drive path");
        fspath::remove_all(U2W(path));
        return true;
    });
    ipc_on("fs.rename", [](const json& a) -> json {
        fspath::rename(U2W(a.value("from", std::string{})),
                       U2W(a.value("to", std::string{})));
        return true;
    });
    ipc_on("fs.stat", [](const json& a) -> json {
        auto path = a.value("path", std::string{});
        WIN32_FILE_ATTRIBUTE_DATA d;
        if (!GetFileAttributesExW(U2W(path).c_str(), GetFileExInfoStandard, &d))
            throw std::runtime_error("Not found: " + path);
        ULARGE_INTEGER sz; sz.HighPart = d.nFileSizeHigh; sz.LowPart = d.nFileSizeLow;
        ULARGE_INTEGER ft; ft.HighPart = d.ftLastWriteTime.dwHighDateTime;
        ft.LowPart = d.ftLastWriteTime.dwLowDateTime;
        int64_t ts = (ft.QuadPart - 116444736000000000LL) / 10000000LL;
        bool isDir = (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        return {{"size", sz.QuadPart}, {"modified", ts}, {"isDir", isDir}, {"isFile", !isDir}};
    });
}

static std::mutex g_backgroundMtx;

// Update downloads are deliberately conservative for unreliable networks:
// keep retrying the complete package at a fixed cadence, but never let one
// update operation wait indefinitely.
static constexpr ULONGLONG UPDATE_RETRY_WINDOW_MS = 5ULL * 60ULL * 1000ULL;
static constexpr DWORD UPDATE_RETRY_INTERVAL_MS = 5000;

struct DownloadAttemptResult {
    bool ok = false;
    bool retryable = true;
    std::string error;
    DWORD win32Error = ERROR_SUCCESS;
    DWORD httpStatus = 0;
    uint64_t receivedBytes = 0;
    uint64_t expectedBytes = 0;
};

static DownloadAttemptResult downloadFileAttempt(
    const std::string& url, const std::wstring& dest,
    const std::function<void(uint64_t, uint64_t)>& progress,
    ULONGLONG deadline);
static bool downloadFile(const std::string& url, const std::wstring& dest,
                         const std::function<void(uint64_t, uint64_t)>& progress = {});
static bool webmDurationSeconds(const std::wstring& path, double& seconds);

static std::wstring background_assets_dir() {
    auto dir = std::wstring{L"C:\\SOFT\\YeMan\\PowerControl\\ui-background"};
    std::error_code ec;
    fspath::create_directories(dir, ec);
    return dir;
}

static std::wstring background_config_path() {
    return background_assets_dir() + L"\\background.json";
}

static json background_state() {
    json cfg = ymSettingsSection("background").value("asset", json::object());
    if (!cfg.is_object() || cfg.empty()) {
        std::ifstream f(background_config_path(), std::ios::binary);
        if (!f) return {{"enabled", false}, {"kind", "image"}, {"url", ""}};
        try { f >> cfg; } catch (...) { return {{"enabled", false}, {"kind", "image"}, {"url", ""}}; }
    }
    auto file = cfg.value("file", std::string{});
    if (file.empty() || file.find('/') != std::string::npos || file.find('\\') != std::string::npos)
        return {{"enabled", false}, {"kind", "image"}, {"url", ""}};
    auto path = background_assets_dir() + L"\\" + U2W(file);
    std::error_code ec;
    if (!fspath::is_regular_file(path, ec)) return {{"enabled", false}, {"kind", "image"}, {"url", ""}};
    auto stamp = cfg.value("stamp", uint64_t{0});
    auto kind = cfg.value("kind", file == "background.mp4" ? std::string{"video"} : std::string{"image"});
    if (kind != "video") kind = "image";
    return {
        {"enabled", true},
        {"kind", kind},
        {"file", file},
        {"url", "https://user-assets.localhost/" + file + "?v=" + std::to_string(stamp)}
    };
}

static void reg_background() {
    ipc_on("background.get", [](const json&) -> json {
        std::lock_guard<std::mutex> lock(g_backgroundMtx);
        return background_state();
    });
    ipc_on("background.install", [](const json& a) -> json {
        std::lock_guard<std::mutex> lock(g_backgroundMtx);
        auto source = U2W(a.value("source", std::string{}));
        std::error_code ec;
        if (source.empty() || !fspath::is_regular_file(source, ec))
            throw std::runtime_error("Background image not found");
        auto size = fspath::file_size(source, ec);
        if (ec || size == 0) throw std::runtime_error("Background file size is invalid");
        auto ext = fspath::path(source).extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        const bool isVideo = ext == L".mp4";
        const uint64_t maxSize = isVideo ? 2ULL * 1024ULL * 1024ULL * 1024ULL : 20ULL * 1024ULL * 1024ULL;
        if (size > maxSize) throw std::runtime_error(isVideo ? "MP4 background must be 1 byte to 2 GB" : "Background image must be 1 byte to 20 MB");
        if (ext != L".jpg" && ext != L".jpeg" && ext != L".png" && !isVideo)
            throw std::runtime_error("Only JPG, JPEG and MP4 are supported");
        std::string file;
        if (isVideo) {
            std::ifstream sig(source, std::ios::binary);
            unsigned char header[16] = {};
            sig.read(reinterpret_cast<char*>(header), sizeof(header));
            const bool mp4 = sig.gcount() >= 12 && header[4] == 'f' && header[5] == 't' && header[6] == 'y' && header[7] == 'p';
            if (!mp4) throw std::runtime_error("Invalid MP4 container");
            file = "background.mp4";
        } else {
            std::ifstream sig(source, std::ios::binary);
            unsigned char header[8] = {};
            sig.read(reinterpret_cast<char*>(header), sizeof(header));
            const bool jpeg = header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF;
            const bool png = header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47 &&
                             header[4] == 0x0D && header[5] == 0x0A && header[6] == 0x1A && header[7] == 0x0A;
            if ((ext == L".png" && !png) || (ext != L".png" && !jpeg))
                throw std::runtime_error("Invalid background image data");
            file = ext == L".png" ? "background.png" : "background.jpg";
        }
        auto dir = background_assets_dir();
        auto target = dir + L"\\" + U2W(file);
        auto temp = target + L".tmp";
        fspath::copy_file(source, temp, fspath::copy_options::overwrite_existing, ec);
        if (ec) throw std::runtime_error("Failed to copy background file");
        if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temp.c_str());
            throw std::runtime_error("Failed to activate background file");
        }
        for (const auto& old : {L"background.jpg", L"background.png", L"background.mp4"}) {
            if (W2U(old) != file) DeleteFileW((dir + L"\\" + old).c_str());
        }
        uint64_t stamp = GetTickCount64();
        {
            if (!ymSettingsPatchSection("background", json{{"asset", json{{"file", file}, {"kind", isVideo ? "video" : "image"}, {"stamp", stamp}}}}))
                throw std::runtime_error("Failed to write background config");
        }
        return background_state();
    });
    ipc_on("background.clear", [](const json&) -> json {
        std::lock_guard<std::mutex> lock(g_backgroundMtx);
        auto dir = background_assets_dir();
        DeleteFileW((dir + L"\\background.jpg").c_str());
        DeleteFileW((dir + L"\\background.png").c_str());
        DeleteFileW((dir + L"\\background.mp4").c_str());
        DeleteFileW(background_config_path().c_str());
        ymSettingsPatchSection("background", json{{"asset", json::object()}});
        return json{{"enabled", false}, {"kind", "image"}, {"url", ""}};
    });

    ipc_on("dynamicBackground.get", [](const json&) -> json {
        std::lock_guard<std::mutex> lock(g_backgroundMtx);
        {
            const auto unified = ymSettingsSection("background").value("dynamic", json::object());
            if (unified.is_object() && !unified.empty()) {
                const auto source = unified.value("url", std::string{});
                if (!source.empty()) {
                    auto kind = unified.value("kind", std::string{"video"});
                    if (kind != "image") kind = "video";
                    return json{{"enabled", true}, {"kind", kind}, {"url", source},
                        {"fallbackUrls", unified.value("fallbackUrls", json::array())},
                        {"appId", unified.value("appId", 0)}, {"gameName", unified.value("gameName", std::string{})},
                        {"source", unified.value("source", std::string{"video-online"})}};
                }
            }
            std::ifstream online(background_assets_dir() + L"\\dynamic-online.json", std::ios::binary);
            if (online) {
                try {
                    json cfg; online >> cfg;
                    const auto source = cfg.value("url", std::string{});
                    if (!source.empty()) {
                        auto kind = cfg.value("kind", std::string{"video"});
                        if (kind != "image") kind = "video";
                        return json{
                            {"enabled", true}, {"kind", kind}, {"url", source},
                            {"fallbackUrls", cfg.value("fallbackUrls", json::array())},
                            {"appId", cfg.value("appId", 0)}, {"gameName", cfg.value("gameName", std::string{})},
                            {"source", cfg.value("source", std::string{"video-online"})}
                        };
                    }
                } catch (...) { /* fall through to the local cache */ }
            }
        }
        std::ifstream f(background_assets_dir() + L"\\dynamic.json", std::ios::binary);
        if (!f) return json{{"enabled", false}, {"kind", "image"}, {"url", ""}};
        json cfg;
        try { f >> cfg; } catch (...) { return json{{"enabled", false}, {"kind", "image"}, {"url", ""}}; }
        auto file = cfg.value("file", std::string{});
        if (file.empty() || file.find('/') != std::string::npos || file.find('\\') != std::string::npos)
            return json{{"enabled", false}, {"kind", "image"}, {"url", ""}};
        auto path = background_assets_dir() + L"\\" + U2W(file);
        std::error_code ec;
        if (!fspath::is_regular_file(path, ec)) return json{{"enabled", false}, {"kind", "image"}, {"url", ""}};
        auto kind = cfg.value("kind", std::string{"image"});
        if (kind != "video") kind = "image";
        return json{
            {"enabled", true}, {"kind", kind},
            {"url", "https://user-assets.localhost/" + file + "?v=" + std::to_string(cfg.value("stamp", uint64_t{0}))},
            {"appId", cfg.value("appId", 0)}, {"gameName", cfg.value("gameName", std::string{})},
            {"source", cfg.value("source", std::string{})}
        };
    });
    ipc_on("dynamicBackground.installOnline", [](const json& a) -> json {
        std::lock_guard<std::mutex> lock(g_backgroundMtx);
        const auto source = a.value("source", std::string{});
        const auto fallbackUrls = a.value("fallbackUrls", json::array());
        auto kind = a.value("kind", std::string{"video"});
        if (kind != "image") kind = "video";
        if (source.empty() || source.size() > 4096 || source.rfind("https://", 0) != 0)
            throw std::runtime_error("Online Steam video URL is invalid");
        if (!fallbackUrls.is_array() || fallbackUrls.size() > 16)
            throw std::runtime_error("Online Steam video fallback list is invalid");
        json urls = json::array();
        for (const auto& item : fallbackUrls) {
            if (item.is_string() && item.get<std::string>().rfind("https://", 0) == 0 && item.get<std::string>().size() <= 4096)
                urls.push_back(item.get<std::string>());
        }
        auto dir = background_assets_dir();
        for (const auto& old : {L"dynamic.jpg", L"dynamic.png", L"dynamic.mp4", L"dynamic.webm", L"dynamic.json"})
            DeleteFileW((dir + L"\\" + old).c_str());
        const auto appId = a.value("appId", 0);
        const auto gameName = a.value("gameName", std::string{});
        const auto sourceType = a.value("sourceType", std::string{"video-online"});
        const json dynamic = {{"url", source}, {"fallbackUrls", urls}, {"kind", kind}, {"appId", appId}, {"gameName", gameName}, {"source", sourceType}};
        if (!ymSettingsPatchSection("background", json{{"dynamic", dynamic}}))
            throw std::runtime_error("Failed to save online Steam video state");
        return json{
            {"enabled", true}, {"kind", kind}, {"url", source}, {"fallbackUrls", urls},
            {"appId", appId}, {"gameName", gameName}, {"source", sourceType}
        };
    });
    ipc_on("dynamicBackground.installUrl", [](const json& a) -> json {
        std::lock_guard<std::mutex> lock(g_backgroundMtx);
        auto source = a.value("source", std::string{});
        auto kind = a.value("kind", std::string{"image"});
        if (kind != "video") kind = "image";
        auto appId = a.value("appId", 0);
        auto gameName = a.value("gameName", std::string{});
        if (source.empty() || source.size() > 4096) throw std::runtime_error("Dynamic background URL is invalid");
        auto dir = background_assets_dir();
        std::wstring file = kind == "video" ? L"dynamic.webm" : L"dynamic.jpg";
        auto lower = ascii_lower(source);
        if (kind == "video" && lower.find(".mp4") != std::string::npos) file = L"dynamic.mp4";
        if (kind == "image" && lower.find(".png") != std::string::npos) file = L"dynamic.png";
        auto target = dir + L"\\" + file;
        // Each refresh gets its own temporary file so overlapping UI/game-status refreshes
        // cannot delete or truncate another download in progress.
        auto temp = target + L".tmp." + std::to_wstring(GetTickCount64());
        if (!downloadFile(source, temp)) throw std::runtime_error("Failed to download Steam background media");
        std::error_code ec;
        auto size = fspath::file_size(temp, ec);
        if (ec || size == 0 || size > (kind == "video" ? 300ULL * 1024ULL * 1024ULL : 30ULL * 1024ULL * 1024ULL)) {
            fspath::remove(temp, ec);
            throw std::runtime_error("Steam background media size is invalid");
        }
        if (kind == "video" && lower.find(".webm") != std::string::npos) {
            double duration = 0.0;
            if (!webmDurationSeconds(temp, duration)) {
                fspath::remove(temp, ec);
                throw std::runtime_error("Steam video duration cannot be verified");
            }
            if (duration < 30.0) {
                fspath::remove(temp, ec);
                throw std::runtime_error("Steam video is shorter than 30 seconds (" + std::to_string(duration) + "s)");
            }
        }
        if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temp.c_str());
            throw std::runtime_error("Failed to activate Steam background media");
        }
        for (const auto& old : {L"dynamic.jpg", L"dynamic.png", L"dynamic.mp4", L"dynamic.webm"}) {
            if (old != file) DeleteFileW((dir + L"\\" + old).c_str());
        }
        auto stamp = GetTickCount64();
        const json dynamic = {{"file", W2U(file)}, {"kind", kind}, {"appId", appId}, {"gameName", gameName}, {"source", a.value("sourceType", std::string{})}, {"stamp", stamp}, {"url", "https://user-assets.localhost/" + W2U(file) + "?v=" + std::to_string(stamp)}};
        if (!ymSettingsPatchSection("background", json{{"dynamic", dynamic}}))
            throw std::runtime_error("Failed to write dynamic background config");
        return json{
            {"enabled", true}, {"kind", kind}, {"url", "https://user-assets.localhost/" + W2U(file) + "?v=" + std::to_string(stamp)},
            {"appId", appId}, {"gameName", gameName}, {"source", a.value("sourceType", std::string{})}
        };
    });
    ipc_on("dynamicBackground.clear", [](const json&) -> json {
        std::lock_guard<std::mutex> lock(g_backgroundMtx);
        auto dir = background_assets_dir();
        for (const auto& old : {L"dynamic.jpg", L"dynamic.png", L"dynamic.mp4", L"dynamic.webm", L"dynamic.json"})
            DeleteFileW((dir + L"\\" + old).c_str());
        DeleteFileW((dir + L"\\dynamic-online.json").c_str());
        ymSettingsPatchSection("background", json{{"dynamic", json::object()}});
        return json{{"enabled", false}, {"kind", "image"}, {"url", ""}};
    });
}

// ================================================================
//  Commands: Music player (folder → dedicated virtual host)
// ================================================================

static std::mutex g_musicMtx;
static const wchar_t* MUSIC_HOST = L"music-assets.invalid";

static std::wstring music_config_path() {
    return std::wstring{L"C:\\SOFT\\YeMan\\PowerControl\\music_player.json"};
}

static json music_config_read() {
    const auto unified = ymSettingsSection("music");
    if (!unified.empty()) return unified;
    std::ifstream f(music_config_path(), std::ios::binary);
    if (!f) return json::object();
    try { json cfg; f >> cfg; return cfg.is_object() ? cfg : json::object(); }
    catch (...) { return json::object(); }
}

static void music_config_write(const json& cfg) {
    std::error_code ec;
    fspath::create_directories(fspath::path(music_config_path()).parent_path(), ec);
    if (!ymSettingsPatchSection("music", cfg))
        throw std::runtime_error("Failed to write music config");
}

static std::string music_mode_from_config() {
    const json cfg = music_config_read();
    const std::string m = cfg.value("mode", std::string("sequential"));
    return m == "random" ? "random" : "sequential";
}

// 读取已持久化的音乐目录（仅当真实存在且为目录时返回）
static std::wstring music_folder_from_config() {
    const json cfg = music_config_read();
    auto folder = cfg.value("folder", std::string{});
    if (folder.empty()) return {};
    std::error_code ec;
    if (!fspath::is_directory(U2W(folder), ec)) return {};
    return U2W(folder);
}

// 读取已持久化的音量（缺省 0.8；与 music_player.json 的 folder 同文件存储）
static double music_volume_from_config() {
    const json cfg = music_config_read();
    double v = cfg.value("volume", 0.8);
    if (!(v >= 0.0 && v <= 1.0)) v = 0.8;
    return v;
}

// 启动期在导航前恢复映射（当前页即生效，无需刷新）
static bool configureMusicHost(ICoreWebView2* view) {
    if (!view) return true;
    auto folder = music_folder_from_config();
    if (folder.empty()) return true; // 未配置不是错误
    ComPtr<ICoreWebView2_3> v3;
    if (FAILED(view->QueryInterface(IID_PPV_ARGS(&v3)))) return true;
    v3->SetVirtualHostNameToFolderMapping(
        MUSIC_HOST, folder.c_str(),
        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
    return true;
}

static void reg_music() {
    ipc_on("music.get", [](const json&) -> json {
        std::lock_guard<std::mutex> lock(g_musicMtx);
        auto folder = music_folder_from_config();
        const auto mode = music_mode_from_config();
        if (folder.empty())
            return {{"enabled", false}, {"folder", ""}, {"baseUrl", ""}, {"reloadRecommended", false},
                    {"volume", music_volume_from_config()}, {"mode", mode}};
        return {
            {"enabled", true},
            {"folder", W2U(folder)},
            {"baseUrl", "https://music-assets.invalid/"},
            {"reloadRecommended", false},
            {"volume", music_volume_from_config()},
            {"mode", mode}
        };
    });
    ipc_on("music.setFolder", [](const json& a) -> json {
        std::lock_guard<std::mutex> lock(g_musicMtx);
        auto folder = U2W(a.value("folder", std::string{}));
        std::error_code ec;
        if (folder.empty() || !fspath::is_directory(folder, ec))
            throw std::runtime_error("Invalid music folder");
        if (folder.size() > static_cast<size_t>(MAX_PATH))
            throw std::runtime_error("Music folder path too long");
        json cfg = music_config_read();
        cfg["folder"] = W2U(folder);
        music_config_write(cfg);
        ComPtr<ICoreWebView2_3> v3;
        if (SUCCEEDED(g_view->QueryInterface(IID_PPV_ARGS(&v3)))) {
            v3->SetVirtualHostNameToFolderMapping(
                MUSIC_HOST, folder.c_str(),
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
        }
        return {
            {"enabled", true},
            {"folder", W2U(folder)},
            {"baseUrl", "https://music-assets.invalid/"},
            {"reloadRecommended", true},
            {"volume", music_volume_from_config()},
            {"mode", music_mode_from_config()}
        };
    });
    ipc_on("music.clearFolder", [](const json&) -> json {
        std::lock_guard<std::mutex> lock(g_musicMtx);
        json cfg = music_config_read();
        cfg["folder"] = "";
        music_config_write(cfg);
        ComPtr<ICoreWebView2_3> v3;
        if (SUCCEEDED(g_view->QueryInterface(IID_PPV_ARGS(&v3)))) {
            v3->ClearVirtualHostNameToFolderMapping(MUSIC_HOST);
        }
        return {{"enabled", false}, {"folder", ""}, {"baseUrl", ""}, {"reloadRecommended", false},
                {"volume", music_volume_from_config()}, {"mode", music_mode_from_config()}};
    });
    // 音量独立持久化：即使未配置文件夹也可记忆；写盘时保留 folder
    ipc_on("music.setVolume", [](const json& a) -> json {
        std::lock_guard<std::mutex> lock(g_musicMtx);
        double v = a.value("volume", 0.8);
        if (!(v >= 0.0 && v <= 1.0)) v = 0.8;
        json cfg = music_config_read();
        cfg["volume"] = v;
        music_config_write(cfg);
        return {{"volume", v}};
    });
    ipc_on("music.setMode", [](const json& a) -> json {
        std::lock_guard<std::mutex> lock(g_musicMtx);
        const std::string m = a.value("mode", std::string("sequential"));
        if (m != "sequential" && m != "random")
            throw std::runtime_error("Invalid music mode");
        json cfg = music_config_read();
        cfg["mode"] = m;
        music_config_write(cfg);
        return {{"mode", m}};
    });
}

// ================================================================
//  Commands: Clipboard
// ================================================================

static void reg_clipboard() {
    ipc_on("clipboard.readText", [](const json&) -> json {
        if (!OpenClipboard(g_hwnd)) return nullptr;
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (!h) { CloseClipboard(); return nullptr; }
        auto text = W2U(static_cast<LPCWSTR>(GlobalLock(h)));
        GlobalUnlock(h);
        CloseClipboard();
        return text;
    });
    ipc_on("clipboard.writeText", [](const json& a) -> json {
        auto text = U2W(a.value("text", std::string{}));
        if (!OpenClipboard(g_hwnd)) return false;
        EmptyClipboard();
        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (h) {
            memcpy(GlobalLock(h), text.c_str(), bytes);
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        }
        CloseClipboard();
        return true;
    });
}

// ================================================================
//  Commands: Shell & App
// ================================================================

static void reg_shell_app() {
    // Native monitor daemon: one worker reads HWiNFO/CPU/battery/process state
    // and keeps the legacy topmon/fps JSON contract for the frontend.
    ipc_on("game.detect", [](const json&) -> json {
        const auto game = nativeDetectGame();
        if (!game.pid) return nullptr;
        // 共享检测结果同时作为外部缩放/插帧覆盖层的真实游戏记忆。
        // Lossless Scaling 启动后可能成为最上层窗口，回焦必须仍指向其下方游戏。
        FocusTargetSnapshot remembered;
        remembered.pid = game.pid;
        remembered.path = game.path;
        focusQueryProcessIdentity(game.pid, nullptr, &remembered.processCreated);
        remembered.hwnd = focusFindWindowForPid(game.pid);
        remembered.valid = true;
        if (remembered.hwnd) {
            remembered.className = focusWindowClass(remembered.hwnd);
            HMONITOR monitor = MonitorFromWindow(remembered.hwnd, MONITOR_DEFAULTTONEAREST);
            focusMonitorInfo(monitor, &remembered.monitorDevice, &remembered.monitorRect);
            remembered.fullscreen = focusWindowLooksFullscreen(remembered.hwnd, remembered.monitorRect);
        }
        {
            std::lock_guard<std::mutex> lock(g_rememberedGameTargetMx);
            g_rememberedGameTarget = remembered;
            g_rememberedGameDeadline = GetTickCount64() + 6ULL * 60ULL * 60ULL * 1000ULL;
        }
        return json{
            {"pid", static_cast<int>(game.pid)},
            {"name", W2U(game.name)},
            {"title", W2U(game.title)},
            {"path", W2U(game.path)},
            {"ts", static_cast<long long>(sgNowEpoch() * 1000.0)},
        };
    });
    ipc_on("game.suspend", [](const json& a) -> json {
        const int pid = a.value("pid", 0);
        if (pid <= 0) return json{{"paused", false}, {"error", "invalid pid"}};
        const auto result = sgSuspendGameByPid(static_cast<DWORD>(pid));
        if (result.value("paused", false))
            g_manualPausedPid.store(static_cast<DWORD>(pid), std::memory_order_release);
        return result;
    });
    ipc_on("game.resume", [](const json& a) -> json {
        std::lock_guard<std::mutex> opLock(g_sgOpMtx);
        const auto result = sgResumeGameByPids(
            a.contains("pids") ? a["pids"] : json::array(), SG_MANUAL_DIR);
        g_manualPausedPid.store(0, std::memory_order_release);
        return result;
    });
    ipc_on("monitor.start", [](const json& a) -> json {
        const bool top = a.value("top", false);
        const bool fps = a.value("fps", false);
        if (!top && !fps) throw std::runtime_error("monitor.start requires top or fps");
        nativeMonitorSetMode(top, fps, true);
        return json{{"ok", true}, {"top", top}, {"fps", fps}};
    });
    ipc_on("monitor.stop", [](const json& a) -> json {
        nativeMonitorSetMode(a.value("top", false), a.value("fps", false), false);
        return json{{"ok", true}};
    });

    // TDP daemon 专用控制面：路径和参数由 native 固定，前端不能传入任意管理员程序。
    ipc_on("tdpDaemon.start", [](const json&) -> json {
        const auto phase = g_powerLifecycle.load(std::memory_order_acquire);
        if (phase == PowerLifecycle::Suspending || phase == PowerLifecycle::Suspended)
            throw std::runtime_error("TDP daemon start blocked during suspend");
        if (!sameFinalPath(kTdpDaemonExe, kTdpDaemonExe))
            throw std::runtime_error("Trusted YeManTdpCtl.exe not found");
        HANDLE job = ensureTdpDaemonJob();
        if (!job)
            throw std::runtime_error("Failed to create TDP daemon lifetime job");
        if (WaitNamedPipeW(kTdpDaemonPipeName, 200)) {
            HANDLE pipe = CreateFileW(kTdpDaemonPipeName, GENERIC_READ | GENERIC_WRITE, 0,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (pipe != INVALID_HANDLE_VALUE) {
                DWORD pid = 0;
                bool valid = tdpVerifyPipeServer(pipe, &pid);
                CloseHandle(pipe);
                if (valid) {
                    HANDLE daemon = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE,
                                                FALSE, pid);
                    if (daemon) {
                        bool attached = AssignProcessToJobObject(job, daemon) != FALSE;
                        CloseHandle(daemon);
                        if (attached) return json{{"ok", true}};
                    }
                }
            }
        }
        std::wstring cmdLine = quote_windows_arg(kTdpDaemonExe) + L" daemon";
        std::vector<wchar_t> cmd(cmdLine.begin(), cmdLine.end());
        cmd.push_back(0);
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(kTdpDaemonExe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
            DWORD le = GetLastError();
            throw std::runtime_error("Failed to start trusted TDP daemon (Win32 " + std::to_string(le) + ")");
        }
        if (!AssignProcessToJobObject(job, pi.hProcess)) {
            DWORD le = GetLastError();
            TerminateProcess(pi.hProcess, ERROR_ACCESS_DENIED);
            WaitForSingleObject(pi.hProcess, 1000);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            throw std::runtime_error("Failed to bind TDP daemon lifetime (Win32 " + std::to_string(le) + ")");
        }
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return json{{"ok", true}};
    });
    ipc_on("tdpDaemon.request", [](const json& a) -> json {
        auto op = a.value("op", std::string{});
        if (op != "ping" && op != "set" && op != "resume" && op != "quit")
            throw std::runtime_error("TDP daemon operation not allowed");
        json args = a.contains("args") && a["args"].is_object() ? a["args"] : json::object();
        if (op == "set") {
            if (!args.contains("watts") || !args["watts"].is_number())
                throw std::runtime_error("TDP daemon watts required");
            double w = args["watts"].get<double>();
            if (!std::isfinite(w) || w < 2.0 || w > 200.0)
                throw std::runtime_error("TDP daemon watts out of range");
        }
        json req = {{"version", 1}, {"requestId", nextTdpRequestId()},
                    {"op", op}, {"args", args}};
        DWORD timeoutMs = a.value("timeoutMs", 3000u);
        if (timeoutMs < 100) timeoutMs = 100;
        if (timeoutMs > 10000) timeoutMs = 10000;
        return tdpDaemonPipeRequest(req, timeoutMs);
    });

    ipc_on("shell.open", [](const json& a) -> json {
        auto url = a.value("url", std::string{});
        auto target = trim_ascii(url);
        if (target.empty()) return false;
        if (!is_allowed_shell_target(target)) throw std::runtime_error("blocked unsafe shell.open target");
        auto ret = (INT_PTR)ShellExecuteW(nullptr, L"open", U2W(target).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (ret <= 32) throw std::runtime_error("failed to open shell target");
        return true;
    });
    ipc_on("shell.execute", [](const json& a) -> json {
        auto program = a.value("program", std::string{});
        if (program.empty()) throw std::runtime_error("program is required");
        std::wstring args;
        if (a.contains("args") && a["args"].is_array()) {
            for (auto& arg : a["args"]) {
                if (!arg.is_string()) throw std::runtime_error("shell.execute args must be strings");
                if (!args.empty()) args += L' ';
                args += quote_windows_arg(U2W(arg.get<std::string>()));
            }
        }
        auto ret = (INT_PTR)ShellExecuteW(nullptr, nullptr, U2W(program).c_str(),
                                          args.empty() ? nullptr : args.c_str(),
                                          nullptr, SW_SHOWNORMAL);
        if (ret <= 32) throw std::runtime_error("failed to execute program");
        return true;
    });
    // ── shell.hidden：与 shell.execute 行为一致（异步、不阻塞、进程脱离存活），
    //    但用 CREATE_NO_WINDOW 隐藏窗口。专供「自动CPU浮动优化」守护等
    //    需要后台常驻但不弹窗的场景。⚠ 不要改动 shell.execute / shell.run。──
    ipc_on("shell.hidden", [](const json& a) -> json {
        auto program = a.value("program", std::string{});
        if (program.empty()) throw std::runtime_error("program is required");
        std::wstring cmdLine = quote_windows_arg(U2W(program));
        if (a.contains("args") && a["args"].is_array()) {
            for (auto& arg : a["args"]) {
                if (!arg.is_string()) throw std::runtime_error("shell.hidden args must be strings");
                cmdLine += L" ";
                cmdLine += quote_windows_arg(U2W(arg.get<std::string>()));
            }
        }
        std::vector<wchar_t> cmd(cmdLine.begin(), cmdLine.end());
        cmd.push_back(0);
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        // CREATE_NO_WINDOW 隐藏窗口；不创建可继承管道（守护 stdout 不需要，避免缓冲死锁）；
        // 不传 envBlock（YeManCC 以 requireAdministrator 运行，子进程不会再弹 UAC/MessageBox）。
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            DWORD le = GetLastError();
            throw std::runtime_error(("Failed to start hidden process (Win32 " + std::to_string(le) + ")").c_str());
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return json{{"ok", true}};
    });
    ipc_on("app.exit", [](const json& a) -> json {
        // IPC 可能运行在工作线程；窗口销毁和退出清理必须切回 UI 线程。
        if (g_hwnd && IsWindow(g_hwnd)) PostMessageW(g_hwnd, WM_APP_EXIT, (WPARAM)a.value("code", 0), 0);
        else PostQuitMessage(a.value("code", 0));
        return true;
    });
    ipc_on("app.dataDir", [](const json&) -> json {
        return W2U(app_data_dir());
    });
    ipc_on("app.exeDir", [](const json&) -> json {
        return W2U(exe_dir());
    });
    ipc_on("app.powerControlDir", [](const json&) -> json {
        return W2U(POWER_CONTROL_DIR);
    });
    // Extract a file embedded in the single-exe pak to disk (for sidecar binaries
    // that must run from a real path). Returns false in portable builds (nothing
    // is embedded) or when the named asset isn't found.
    ipc_on("app.extractAsset", [](const json& a) -> json {
        auto name = a.value("name", std::string{});
        auto dest = a.value("dest", std::string{});
        if (name.empty() || dest.empty()) throw std::runtime_error("name and dest are required");
#ifdef SINGLE_EXE
        const PakEntry* e = findPakEntry(name);
        if (!e) return false;
        auto wdest = U2W(dest);
        std::error_code ec;
        fspath::create_directories(fspath::path(wdest).parent_path(), ec);
        // Skip the write if the destination already matches the embedded asset (avoids
        // redundant disk churn re-extracting unchanged sidecars on every launch).
        if (fspath::exists(wdest, ec) && fspath::file_size(wdest, ec) == e->size) {
            std::ifstream in(wdest, std::ios::binary);
            std::string existing((std::istreambuf_iterator<char>(in)), {});
            if (existing.size() == e->size && memcmp(existing.data(), e->data, e->size) == 0)
                return true;
        }
        std::ofstream out(wdest, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot open destination for writing");
        out.write(e->data, e->size);
        out.close();
        return out.good();
#else
        (void)name; (void)dest;
        return false;
#endif
    });
    // ── 睡眠守护（Sleep Guard）控制面 ──
    ipc_on("sleepGuard.set", [](const json& a) -> json {
        bool on = a.value("on", false);
        g_guardEnabled = on;
        sgWriteFile(SG_DIR + L"\\Enable.txt", on ? "1" : "0");
        sgUpdateOverheatTimer();
        return true;
    });
    ipc_on("sleepGuard.get", [](const json& a) -> json {
        int suspended = 0;
        std::wstring dir = SG_DIR + L"\\suspended";
        std::error_code ec;
        if (fspath::exists(dir, ec))
            for (auto& e : fspath::directory_iterator(dir, ec))
                if (e.is_regular_file()) suspended++;
        return {
            {"enabled", g_guardEnabled},
            {"mode", g_sgMode},
            {"suspended", suspended},
            {"pauseResume", g_sgPauseResume},
            {"killListEnabled", g_sgKillListEnabled},
            {"resleepEnabled", g_sgResleepEnabled},
            {"overheatSleepEnabled", g_sgOverheatSleepEnabled},
            {"overheatTempC", g_sgOverheatTempC}
        };
    });
    ipc_on("sleepGuard.setConfig", [](const json& a) -> json {
        // 仅在提供字段时覆盖，缺省保留当前值（前端始终发全量）
        if (a.contains("mode")) {
            std::string m = a.value("mode", g_sgMode);
            if (m == "off" || m == "custom") g_sgMode = m;
        }
        if (a.contains("pauseResume")) g_sgPauseResume = a.value("pauseResume", g_sgPauseResume);
        if (a.contains("killListEnabled")) g_sgKillListEnabled = a.value("killListEnabled", g_sgKillListEnabled);
        if (a.contains("resleepEnabled")) {
            g_sgResleepEnabled = a.value("resleepEnabled", g_sgResleepEnabled);
            if (!g_sgResleepEnabled) sgStopResleepObservation();
        }
        if (a.contains("overheatSleepEnabled"))
            g_sgOverheatSleepEnabled = a.value("overheatSleepEnabled", g_sgOverheatSleepEnabled);
        if (a.contains("overheatTempC")) {
            g_sgOverheatTempC = a.value("overheatTempC", g_sgOverheatTempC);
            if (g_sgOverheatTempC < 85) g_sgOverheatTempC = 85;
            if (g_sgOverheatTempC > 100) g_sgOverheatTempC = 100;
        }
        sgSaveConfig();
        sgUpdateOverheatTimer();
        return true;
    });
    ipc_on("sleepGuard.recoverAll", [](const json& a) -> json {
        SgResumeResult rr = sgResumeAll(true); // 记录缺失/恢复为0时按PID规则兜底恢复
        if (rr.count > 0) {
            g_sgInSuspend = false;
        }
        return {{"resumed", rr.count}};
    });
    ipc_on("sleepGuard.suspendCurrent", [](const json& a) -> json {
        return sgSuspendCurrent();
    });

    ipc_on("window.startDrag", [](const json&) -> json {
        if (g_fullHeight) return false;   // 全屏吸附模式禁止拖动
        ReleaseCapture();
        SendMessageW(g_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return true;
    });
    // Start a native resize drag. Frontend detects mouse near window edge and
    // calls this on mousedown — Windows then drives the resize (correct cursors,
    // snap, aero-shake). WebView2 fills the entire client area for zero visual
    // chrome around the content.
    ipc_on("window.startResize", [](const json& a) -> json {
        if (!g_hwnd) return false;
        auto edge = a.value("edge", std::string{});
        WPARAM hit = 0;
        if      (edge == "left")         hit = HTLEFT;
        else if (edge == "right")        hit = HTRIGHT;
        else if (edge == "top")          hit = HTTOP;
        else if (edge == "bottom")       hit = HTBOTTOM;
        else if (edge == "top-left")     hit = HTTOPLEFT;
        else if (edge == "top-right")    hit = HTTOPRIGHT;
        else if (edge == "bottom-left")  hit = HTBOTTOMLEFT;
        else if (edge == "bottom-right") hit = HTBOTTOMRIGHT;
        if (!hit) return false;
        ReleaseCapture();
        PostMessageW(g_hwnd, WM_NCLBUTTONDOWN, hit, 0);
        return true;
    });
    ipc_on("window.getConfig", [](const json&) -> json {
        return g_cfg;
    });
    ipc_on("window.isFrameless", [](const json&) -> json {
        return g_frameless;
    });
}

// ================================================================
//  Commands: Environment variables
// ================================================================

static void reg_env() {
    ipc_on("env.get", [](const json& a) -> json {
        auto name = a.value("name", std::string{});
        wchar_t buf[32768];
        DWORD len = GetEnvironmentVariableW(U2W(name).c_str(), buf, 32768);
        if (len == 0) return nullptr;
        return W2U(buf);
    });
    ipc_on("env.getAll", [](const json&) -> json {
        json result = json::object();
        auto env = GetEnvironmentStringsW();
        if (!env) return result;
        for (auto p = env; *p; p += wcslen(p) + 1) {
            auto s = W2U(p);
            auto eq = s.find('=');
            if (eq != std::string::npos && eq > 0)
                result[s.substr(0, eq)] = s.substr(eq + 1);
        }
        FreeEnvironmentStringsW(env);
        return result;
    });
}

// ================================================================
//  Commands: Global hotkeys
// ================================================================

static void reg_hotkey() {
    ipc_on("hotkey.register", [](const json& a) -> json {
        int id  = a.value("id", 0);
        int mod = a.value("modifiers", 0); // MOD_ALT=1, MOD_CONTROL=2, MOD_SHIFT=4, MOD_WIN=8
        int key = a.value("key", 0);       // Virtual key code
        bool ok = RegisterHotKey(g_hwnd, id, mod | MOD_NOREPEAT, key);
        return ok;
    });
    ipc_on("hotkey.unregister", [](const json& a) -> json {
        return (bool)UnregisterHotKey(g_hwnd, a.value("id", 0));
    });
    ipc_on("hotkey.unregisterAll", [](const json&) -> json {
        // Unregister IDs 1..100
        for (int i = 1; i <= 100; i++) UnregisterHotKey(g_hwnd, i);
        return true;
    });
}

// ================================================================
//  Commands: Notifications
// ================================================================

static void reg_notification() {
    ipc_on("notification.show", [](const json& a) -> json {
        auto title = U2W(a.value("title", std::string{"通知"}));
        auto body  = U2W(a.value("body", std::string{}));
        // Use tray balloon if tray is active
        if (g_trayActive) {
            g_nid.uFlags |= NIF_INFO;
            wcsncpy_s(g_nid.szInfoTitle, title.c_str(), _TRUNCATE);
            wcsncpy_s(g_nid.szInfo, body.c_str(), _TRUNCATE);
            g_nid.dwInfoFlags = NIIF_INFO;
            Shell_NotifyIconW(NIM_MODIFY, &g_nid);
            return true;
        }
        // Create temporary tray icon for notification
        NOTIFYICONDATAW nid{sizeof(nid)};
        nid.hWnd  = g_hwnd;
        nid.uID   = 99;
        nid.uFlags = NIF_ICON | NIF_INFO;
        nid.hIcon  = g_appIconSmall ? g_appIconSmall : LoadIconW(nullptr, IDI_APPLICATION);
        wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
        wcsncpy_s(nid.szInfo, body.c_str(), _TRUNCATE);
        nid.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconW(NIM_ADD, &nid);
        // Remove after a delay (fire-and-forget via timer)
        SetTimer(g_hwnd, 99, 5000, [](HWND h, UINT, UINT_PTR id, DWORD) {
            NOTIFYICONDATAW nid{sizeof(nid)};
            nid.hWnd = h; nid.uID = 99;
            Shell_NotifyIconW(NIM_DELETE, &nid);
            KillTimer(h, id);
        });
        return true;
    });
}

// ================================================================
//  Commands: Context menu
// ================================================================

static void reg_menu() {
    ipc_on("menu.popup", [](const json& a) -> json {
        if (!a.contains("items") || !a["items"].is_array()) return nullptr;
        HMENU hMenu = CreatePopupMenu();
        int idx = 1;
        for (auto& item : a["items"]) {
            if (item.is_string() && item.get<std::string>() == "-") {
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            } else if (item.is_object()) {
                auto label = U2W(item.value("label", std::string{""}));
                UINT flags = MF_STRING;
                if (item.value("disabled", false)) flags |= MF_GRAYED;
                if (item.value("checked", false))  flags |= MF_CHECKED;
                AppendMenuW(hMenu, flags, idx, label.c_str());
            }
            idx++;
        }
        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(g_hwnd);
        int cmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_NONOTIFY,
                                    pt.x, pt.y, g_hwnd, nullptr);
        DestroyMenu(hMenu);
        if (cmd == 0) return nullptr; // cancelled
        return cmd - 1; // 0-based index
    });
}

// ================================================================
//  Commands: HTTP client
// ================================================================

static bool crackHttpUrl(const std::string& url, URL_COMPONENTS& uc,
                         wchar_t (&host)[256], wchar_t (&path)[2048],
                         wchar_t (&extra)[2048], std::wstring& objectPath) {
    auto wUrl = U2W(url);
    ZeroMemory(&uc, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;  uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;   uc.dwUrlPathLength = 2048;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(wUrl.c_str(), 0, 0, &uc))
        return false;

    objectPath.assign(path, uc.dwUrlPathLength);
    objectPath.append(extra, uc.dwExtraInfoLength);
    auto fragment = objectPath.find(L'#');
    if (fragment != std::wstring::npos)
        objectPath.resize(fragment);
    if (objectPath.empty())
        objectPath = L"/";
    return true;
}

static void reg_http() {
    ipc_on("http.request", [](const json& a) -> json {
        auto url    = a.value("url", std::string{});
        auto method = a.value("method", std::string{"GET"});
        auto body   = a.value("body", std::string{});
        auto hdrs   = a.value("headers", json::object());

        if (url.empty()) throw std::runtime_error("url is required");

        // Parse URL
        URL_COMPONENTS uc;
        wchar_t host[256]{}, path[2048]{}, extra[2048]{};
        std::wstring objectPath;
        if (!crackHttpUrl(url, uc, host, path, extra, objectPath))
            throw std::runtime_error("Invalid URL");

        if (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS)
            throw std::runtime_error("Only http and https URLs are supported");
        if (!is_http_token(method))
            throw std::runtime_error("Invalid HTTP method");

        bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
        bool steamHostHeader = false;
        if (hdrs.is_object()) {
            const auto it = hdrs.find("Host");
            if (it != hdrs.end() && it->is_string()) {
                const auto hostHeader = U2W(it->get<std::string>());
                steamHostHeader = isSteamHostName(hostHeader.c_str());
            }
        }
        const bool steamHost = isSteamHostName(host) || steamHostHeader;
        HINTERNET hSession = WinHttpOpen(L"QQ/1.0",
                                          steamHost ? WINHTTP_ACCESS_TYPE_NO_PROXY
                                                    : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) throw std::runtime_error("WinHttpOpen failed");
        setHttpTimeouts(hSession);
        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
        WinHttpSetOption(hSession, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

        HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); throw std::runtime_error("WinHttpConnect failed"); }

        auto wMethod = U2W(method);
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, wMethod.c_str(), objectPath.c_str(),
                                                 nullptr, WINHTTP_NO_REFERER,
                                                 WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                 https ? WINHTTP_FLAG_SECURE : 0);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            throw std::runtime_error("WinHttpOpenRequest failed");
        }
        if (steamHost && https) {
            DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                                  SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                                  SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
            WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags, sizeof(securityFlags));
        }

        // Add custom headers
        std::wstring allHeaders;
        for (auto& [k, v] : hdrs.items()) {
            if (!v.is_string() || !is_http_token(k) || has_header_injection_chars(v.get<std::string>()))
                throw std::runtime_error("Invalid HTTP header");
            allHeaders += U2W(k) + L": " + U2W(v.get<std::string>()) + L"\r\n";
        }
        if (!allHeaders.empty())
            WinHttpAddRequestHeaders(hRequest, allHeaders.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

        // Send
        LPVOID bodyPtr = body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data();
        DWORD bodyLen  = body.empty() ? 0 : (DWORD)body.size();
        if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, bodyPtr, bodyLen, bodyLen, 0) ||
            !WinHttpReceiveResponse(hRequest, nullptr)) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            throw std::runtime_error("HTTP request failed");
        }

        // Status code
        DWORD statusCode = 0, sz = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &sz, WINHTTP_NO_HEADER_INDEX);

        // Response headers
        DWORD hdrSize = 0;
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                            WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &hdrSize, WINHTTP_NO_HEADER_INDEX);
        std::wstring respHdrs(hdrSize / sizeof(wchar_t), 0);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                            WINHTTP_HEADER_NAME_BY_INDEX, respHdrs.data(), &hdrSize, WINHTTP_NO_HEADER_INDEX);

        // Response body
        std::string respBody;
        constexpr size_t MAX_HTTP_RESPONSE_BYTES = 8u << 20;
        const ULONGLONG responseDeadline = GetTickCount64() + 120000;
        std::string responseError;
        DWORD available, read;
        for (;;) {
            if (g_poolCancel.load(std::memory_order_acquire)) {
                responseError = "HTTP request cancelled during shutdown";
                break;
            }
            if (GetTickCount64() >= responseDeadline) {
                responseError = "HTTP response timed out";
                break;
            }
            if (!WinHttpQueryDataAvailable(hRequest, &available)) {
                responseError = "HTTP response read failed";
                break;
            }
            if (available == 0) break;
            if (respBody.size() + static_cast<size_t>(available) > MAX_HTTP_RESPONSE_BYTES) {
                responseError = "HTTP response exceeded 8 MiB limit";
                break;
            }
            std::string chunk(available, 0);
            if (!WinHttpReadData(hRequest, chunk.data(), available, &read)) {
                responseError = "HTTP response read failed";
                break;
            }
            chunk.resize(read);
            respBody += chunk;
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        if (!responseError.empty()) throw std::runtime_error(responseError);

        return json{{"status", statusCode}, {"headers", W2U(respHdrs)}, {"body", respBody}};
    });
}

// ================================================================
//  Commands: Special directories & OS info
// ================================================================

static std::wstring getKnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) {
        std::wstring s(p);
        CoTaskMemFree(p);
        return s;
    }
    return {};
}

static void reg_os() {
    ipc_on("os.platform", [](const json&) -> json { return "windows"; });
    ipc_on("os.arch", [](const json&) -> json {
        SYSTEM_INFO si; GetNativeSystemInfo(&si);
        switch (si.wProcessorArchitecture) {
            case PROCESSOR_ARCHITECTURE_AMD64: return "x64";
            case PROCESSOR_ARCHITECTURE_ARM64: return "arm64";
            case PROCESSOR_ARCHITECTURE_INTEL: return "x86";
            default: return "unknown";
        }
    });
    ipc_on("os.version", [](const json&) -> json {
        OSVERSIONINFOEXW vi{sizeof(vi)};
        // Use RtlGetVersion to bypass deprecation
        using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOEXW*);
        auto fn = (RtlGetVersionFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
        if (fn) fn(&vi);
        return std::to_string(vi.dwMajorVersion) + "." +
               std::to_string(vi.dwMinorVersion) + "." +
               std::to_string(vi.dwBuildNumber);
    });
    ipc_on("os.hostname", [](const json&) -> json {
        wchar_t buf[256]; DWORD sz = 256;
        GetComputerNameW(buf, &sz);
        return W2U(buf);
    });
    ipc_on("os.username", [](const json&) -> json {
        wchar_t buf[256]; DWORD sz = 256;
        GetUserNameW(buf, &sz);
        return W2U(buf);
    });
    ipc_on("os.locale", [](const json&) -> json {
        wchar_t buf[85]; GetUserDefaultLocaleName(buf, 85);
        return W2U(buf);
    });
    ipc_on("os.theme", [](const json&) -> json {
        return systemThemeInfo();
    });
    ipc_on("os.accentColor", [](const json&) -> json {
        return colorToHex(systemAccentColor());
    });

    // Special folders
    ipc_on("path.home", [](const json&) -> json {
        return W2U(getKnownFolder(FOLDERID_Profile));
    });
    ipc_on("path.documents", [](const json&) -> json {
        return W2U(getKnownFolder(FOLDERID_Documents));
    });
    ipc_on("path.desktop", [](const json&) -> json {
        return W2U(getKnownFolder(FOLDERID_Desktop));
    });
    ipc_on("path.downloads", [](const json&) -> json {
        return W2U(getKnownFolder(FOLDERID_Downloads));
    });
    ipc_on("path.appData", [](const json&) -> json {
        return W2U(getKnownFolder(FOLDERID_RoamingAppData));
    });
    ipc_on("path.localAppData", [](const json&) -> json {
        return W2U(getKnownFolder(FOLDERID_LocalAppData));
    });
    ipc_on("path.temp", [](const json&) -> json {
        wchar_t buf[MAX_PATH]; GetTempPathW(MAX_PATH, buf);
        return W2U(buf);
    });
}

// ================================================================
//  Commands: File watcher
// ================================================================

static DWORD WINAPI watchThread(LPVOID param) {
    auto* w = (FileWatcher*)param;
    BYTE buf[4096];
    while (w->active) {
        DWORD bytes = 0;
        if (ReadDirectoryChangesW(w->hDir, buf, sizeof(buf), TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE |
            FILE_NOTIFY_CHANGE_CREATION,
            &bytes, nullptr, nullptr))
        {
            auto* info = (FILE_NOTIFY_INFORMATION*)buf;
            while (info && w->active) {
                std::wstring name(info->FileName, info->FileNameLength / sizeof(wchar_t));
                const char* action = "unknown";
                switch (info->Action) {
                    case FILE_ACTION_ADDED:            action = "created"; break;
                    case FILE_ACTION_REMOVED:          action = "deleted"; break;
                    case FILE_ACTION_MODIFIED:         action = "modified"; break;
                    case FILE_ACTION_RENAMED_OLD_NAME: action = "renamed"; break;
                    case FILE_ACTION_RENAMED_NEW_NAME: action = "renamed"; break;
                }
                // Post to main thread
                PostMessageW(g_hwnd, WM_FILE_CHANGED, w->id, (LPARAM)new json{
                    {"id", w->id}, {"action", action}, {"path", W2U(name)}
                });
                if (info->NextEntryOffset == 0) break;
                info = (FILE_NOTIFY_INFORMATION*)((BYTE*)info + info->NextEntryOffset);
            }
        } else {
            break;
        }
    }
    return 0;
}

static bool stopWatcher(FileWatcher* w, DWORD timeoutMs = 3000) {
    if (!w) return true;
    w->active = false;

    if (w->hThread)
        CancelSynchronousIo(w->hThread);
    if (w->hDir && w->hDir != INVALID_HANDLE_VALUE)
        CancelIoEx(w->hDir, nullptr);

    DWORD wait = w->hThread ? WaitForSingleObject(w->hThread, timeoutMs) : WAIT_OBJECT_0;
    if (wait == WAIT_TIMEOUT && w->hDir && w->hDir != INVALID_HANDLE_VALUE) {
        CloseHandle(w->hDir);
        w->hDir = INVALID_HANDLE_VALUE;
        wait = w->hThread ? WaitForSingleObject(w->hThread, timeoutMs) : WAIT_OBJECT_0;
    }
    if (wait == WAIT_TIMEOUT)
        return false;

    if (w->hThread) {
        CloseHandle(w->hThread);
        w->hThread = nullptr;
    }
    if (w->hDir && w->hDir != INVALID_HANDLE_VALUE) {
        CloseHandle(w->hDir);
        w->hDir = INVALID_HANDLE_VALUE;
    }
    return true;
}

static void reg_watcher() {
    ipc_on("watcher.start", [](const json& a) -> json {
        auto path = a.value("path", std::string{});
        if (path.empty()) throw std::runtime_error("path is required");
        auto wpath = U2W(path);
        HANDLE hDir = CreateFileW(wpath.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (hDir == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Cannot watch: " + path);

        int id = g_nextWatchId++;
        auto* w = new FileWatcher{hDir, nullptr, wpath, id, true};
        w->hThread = CreateThread(nullptr, 0, watchThread, w, 0, nullptr);
        g_watchers[id] = w;
        return id;
    });
    ipc_on("watcher.stop", [](const json& a) -> json {
        int id = a.value("id", 0);
        auto it = g_watchers.find(id);
        if (it == g_watchers.end()) return false;
        auto* w = it->second;
        if (!stopWatcher(w))
            return false;
        delete w;
        g_watchers.erase(it);
        return true;
    });
}

// ================================================================
//  Commands: Window state persistence
// ================================================================

static std::wstring g_stateFile;

static void saveWindowState() {
    if (!g_saveWindowState || g_stateFile.empty()) return;
    WINDOWPLACEMENT wp{sizeof(wp)};
    GetWindowPlacement(g_hwnd, &wp);
    json state = {
        {"x",         wp.rcNormalPosition.left},
        {"y",         wp.rcNormalPosition.top},
        {"w",         wp.rcNormalPosition.right - wp.rcNormalPosition.left},
        {"h",         wp.rcNormalPosition.bottom - wp.rcNormalPosition.top},
        {"maximized", wp.showCmd == SW_MAXIMIZE},
    };
    std::ofstream f(g_stateFile, std::ios::binary);
    if (f) f << state.dump(2);
}

static json loadWindowState() {
    if (!g_saveWindowState || g_stateFile.empty()) return {};
    auto readState = [](const std::wstring& path) -> json {
        std::ifstream f(path);
        if (!f) return {};
        try { json j; f >> j; return j; }
        catch (...) { return {}; }
    };
    auto state = readState(g_stateFile);
    if (!state.empty()) return state;
    // One-time compatibility read for older builds that stored this preference in AppData.
    return readState(app_data_dir() + L"\\window-state.json");
}

static void reg_state() {
    ipc_on("window.saveState", [](const json&) -> json {
        saveWindowState();
        return true;
    });
    ipc_on("window.loadState", [](const json&) -> json {
        return loadWindowState();
    });
}

// ================================================================
//  DevTools toggle
// ================================================================

static void reg_devtools() {
    ipc_on("devtools.open", [](const json&) -> json {
        if (g_view) g_view->OpenDevToolsWindow();
        return true;
    });
    ipc_on("devtools.close", [](const json&) -> json {
        // WebView2 doesn't have a direct close devtools API
        // but opening when already open just focuses
        return true;
    });
}

// ================================================================
//  Commands: System tray
// ================================================================

// ── 内存变色托盘图标生成：圆角方块底色 + 白色 Y 字 ──
// 底色按内存负载切换：黑(<80%) / 黄(80-90%) / 红(90-100%)
// 饱和度刻意压低，保证白色 Y 字始终清晰可辨。
static HICON makeMemoryIcon(COLORREF bgColor) {
    const int cx = 24, cy = 24; // 比 SM_CXSMICON(16) 大，缩放后更清晰
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem    = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = cx;
    bmi.bmiHeader.biHeight      = -cy;           // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hBmp);

    // ① 圆角矩形底色填充（用 GDI 路径模拟圆角）
    RECT rc{0, 0, cx, cy};
    HRGN rgn = CreateRoundRectRgn(0, 0, cx + 1, cy + 1, 5, 5);
    HBRUSH hBr = CreateSolidBrush(bgColor);
    FillRgn(hdcMem, rgn, hBr);
    DeleteObject(hBr);
    DeleteObject(rgn);

    // ② 白色 Y 字居中绘制
    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, RGB(255, 255, 255));
    HFONT hFont = CreateFontW(
        -18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI"
    );
    HFONT hOldFnt = (HFONT)SelectObject(hdcMem, hFont);
    DrawTextW(hdcMem, L"Y", 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdcMem, hOldFnt);
    DeleteObject(hFont);

    SelectObject(hdcMem, hOld);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    // ③ 位图 → 图标（mask 全零 = 全不透明）
    ICONINFO ii{};
    ii.fIcon   = TRUE;
    ii.hbmMask = CreateBitmap(cx, cy, 1, 1, nullptr); // 全 0 → 不透明
    ii.hbmColor= hBmp;
    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(ii.hbmMask);
    DeleteObject(hBmp);          // 释放 DIB section
    return hIcon;
}

// 按物理内存负载百分比返回档位：0=黑(<80) 1=黄(80-90) 2=红(90+)
static int memTrayLevel() {
    MEMORYSTATUSEX ms{}; ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return 0;
    if (ms.dwMemoryLoad >= 90) return 2;
    if (ms.dwMemoryLoad >= 80) return 1;
    return 0;
}

// 刷新托盘图标颜色（仅当档位变化时才重建图标，避免无谓 GDI 开销）
static void refreshMemTrayIcon() {
    if (!g_trayActive || !g_hwnd) return;
    int lvl = memTrayLevel();
    if (lvl == g_memTrayLevel && g_memTrayIcon) return; // 档位未变，跳过

    // 销毁旧动态图标
    if (g_memTrayIcon) { DestroyIcon(g_memTrayIcon); g_memTrayIcon = nullptr; }

    static const COLORREF colors[] = {
        RGB(20, 20, 26),     // 0: 黑（与当前 Y 徽标底色一致）
        RGB(195, 165, 50),   // 1: 低饱和琥珀/金黄
        RGB(178, 62, 58),    // 2: 低饱和砖红
    };

    COLORREF bg = colors[lvl];
    if (lvl == 0) {
        // 黑色档位：直接复用原始 app icon，不生成新图标
        g_nid.hIcon = g_appIconSmall ? g_appIconSmall : LoadIconW(nullptr, IDI_APPLICATION);
    } else {
        g_memTrayIcon = makeMemoryIcon(bg);
        g_nid.hIcon   = g_memTrayIcon;
    }
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    g_memTrayLevel = lvl;
}

// Add (or re-add) the notification-area (system tray) icon. Called both from the
// IPC command and once at startup so the app is always resident in the taskbar.
static bool trayCreate(const std::wstring& tip) {
    if (!g_hwnd) return false;
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = g_appIconSmall ? g_appIconSmall : LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(g_nid.szTip, tip.c_str(), _TRUNCATE);
    // 启用 Vista+ 通知图标版本：回调消息 lParam 直接携带原始鼠标消息
    // (WM_LBUTTONUP / WM_LBUTTONDBLCLK / WM_RBUTTONUP)，否则默认旧版只发 NIN_SELECT，
    // 导致双击托盘图标无法唤出窗口（case WM_LBUTTONDBLCLK 永不触发）。
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    BOOL ok = Shell_NotifyIconW(NIM_ADD, &g_nid);
    // NIM_ADD can transiently fail if Explorer is still starting; retry once.
    if (!ok) ok = Shell_NotifyIconW(NIM_ADD, &g_nid);
    if (ok) Shell_NotifyIconW(NIM_SETVERSION, &g_nid); // 必须在 ADD 之后调用
    g_trayActive = !!ok;
    if (g_trayActive) refreshMemTrayIcon(); // 创建后立即按当前内存档位更新颜色
    return g_trayActive;
}

// ── 任务栏常驻模式：同步「任务栏按钮」与「托盘图标」二选一 ──
// on=true  → 常规窗口(移除 WS_EX_TOOLWINDOW)+ AddTab：显示任务栏按钮；托盘移除（不靠托盘）。
// on=false → 工具窗口(WS_EX_TOOLWINDOW)+ DeleteTab：无任务栏按钮；托盘在位（默认）。
// 注意：工具窗口本身不会自动产生任务栏按钮，故 AddTab/DeleteTab 不会造成重复按钮。
static void applyResidentMode() {
    if (!g_hwnd) return;
    bool on = g_taskbarResident;
    // 不再切换 WS_EX_TOOLWINDOW：该样式会让 DWM 忽略 SystemBackdrop 属性，毛玻璃整片失效。
    // 隐藏任务栏按钮完全靠 ITaskbarList::DeleteTab 实现；窗口恒为 WS_EX_APPWINDOW（Backdrop 友好）。
    // 同步 shell 任务栏按钮（AddTab 显示 / DeleteTab 隐藏）
    ComPtr<ITaskbarList3> tb;
    if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&tb))) && SUCCEEDED(tb->HrInit())) {
        if (on) tb->AddTab(g_hwnd); else tb->DeleteTab(g_hwnd);
    }
    // 托盘：仅「非常驻」时存在（常驻=任务栏按钮，不靠托盘）
    if (on) {
        if (g_trayActive) { Shell_NotifyIconW(NIM_DELETE, &g_nid); g_trayActive = false; }
    } else {
        if (!g_trayActive) trayCreate(L"野蛮控制中心");
    }
    // 重新套用 DWM 属性（含 SystemBackdrop），确保在样式确定后 backdrop 真正生效
    applyFramelessDwmAttrs();
}

static void reg_tray() {
    // 任务栏常驻：resident=true → 显示任务栏按钮(并移除托盘)；false → 仅托盘(默认)
    ipc_on("tray.setResident", [](const json& a) -> json {
        g_taskbarResident = a.value("resident", false);
        applyResidentMode();
        return g_taskbarResident;
    });
    ipc_on("tray.setTooltip", [](const json& a) -> json {
        if (!g_trayActive) return false;
        auto tip = U2W(a.value("tooltip", std::string{"App"}));
        wcsncpy_s(g_nid.szTip, tip.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        return true;
    });
    // ⚠️ Xbox / 全屏游戏检测线程已废弃（2026-08-02）：原 1.5s 后台轮询（xboxDetectThread）已删除，
    //    「Xbox全屏游戏模式」开关仅保留任务计划/联动（见 PowerView.vue），不再起后台线程。
    //    xbox.setActive 保留为 no-op 兼容旧前端，避免残留调用报错。
    ipc_on("xbox.setActive", [](const json&) -> json { return true; });
}

// ================================================================
//  Commands: Registry
// ================================================================

static HKEY parseRootKey(const std::string& root) {
    if (root == "HKCU" || root == "HKEY_CURRENT_USER")   return HKEY_CURRENT_USER;
    if (root == "HKLM" || root == "HKEY_LOCAL_MACHINE")   return HKEY_LOCAL_MACHINE;
    if (root == "HKCR" || root == "HKEY_CLASSES_ROOT")    return HKEY_CLASSES_ROOT;
    if (root == "HKU"  || root == "HKEY_USERS")           return HKEY_USERS;
    return HKEY_CURRENT_USER;
}

static void reg_registry() {
    ipc_on("registry.read", [](const json& a) -> json {
        auto root = a.value("root", std::string{"HKCU"});
        auto path = a.value("path", std::string{});
        auto name = a.value("name", std::string{});
        HKEY hKey;
        if (RegOpenKeyExW(parseRootKey(root), U2W(path).c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
            return nullptr;
        DWORD type, size = 0;
        auto wName = U2W(name);
        RegQueryValueExW(hKey, wName.c_str(), nullptr, &type, nullptr, &size);
        if (size == 0) { RegCloseKey(hKey); return nullptr; }
        std::vector<BYTE> buf(size);
        RegQueryValueExW(hKey, wName.c_str(), nullptr, &type, buf.data(), &size);
        RegCloseKey(hKey);
        switch (type) {
            case REG_SZ:
            case REG_EXPAND_SZ:
                return W2U(reinterpret_cast<wchar_t*>(buf.data()));
            case REG_DWORD:
                return (int)*reinterpret_cast<DWORD*>(buf.data());
            case REG_QWORD:
                return (int64_t)*reinterpret_cast<uint64_t*>(buf.data());
            default:
                return nullptr;
        }
    });
    ipc_on("registry.write", [](const json& a) -> json {
        auto root  = a.value("root", std::string{"HKCU"});
        auto path  = a.value("path", std::string{});
        auto name  = a.value("name", std::string{});
        auto value = a["value"];
        HKEY hKey;
        if (RegCreateKeyExW(parseRootKey(root), U2W(path).c_str(), 0, nullptr,
            0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
            throw std::runtime_error("Cannot open registry key");
        auto wName = U2W(name);
        LONG result;
        if (value.is_string()) {
            auto wVal = U2W(value.get<std::string>());
            result = RegSetValueExW(hKey, wName.c_str(), 0, REG_SZ,
                (const BYTE*)wVal.c_str(), (DWORD)((wVal.size()+1)*sizeof(wchar_t)));
        } else if (value.is_number_integer()) {
            DWORD dw = (DWORD)value.get<int>();
            result = RegSetValueExW(hKey, wName.c_str(), 0, REG_DWORD, (const BYTE*)&dw, sizeof(dw));
        } else {
            RegCloseKey(hKey);
            throw std::runtime_error("Unsupported value type");
        }
        RegCloseKey(hKey);
        return result == ERROR_SUCCESS;
    });
    // Batch writes for the fixed YeMan power plan.
    //
    // The processor subgroup contains settings hidden by Windows/OEMs. Match
    // PowerSettingsExplorer by clearing POWER_ATTRIBUTE_HIDE once per native
    // process, then use PowerWriteAC/DCValueIndex. Some firmware rejects one
    // hidden class or value through the Power API; fall back to the exact
    // scheme/setting registry key and keep processing the rest of the batch.
    ipc_on("registry.writePowerBatch", [](const json& a) -> json {
        const auto scheme = a.value("scheme", std::string{});
        const auto subGroup = a.value("subGroup", std::string{});
        const auto valueName = a.value("valueName", std::string{});
        const auto entries = a.value("entries", json::array());
        if (scheme.empty() || subGroup.empty() ||
            (valueName != "ACSettingIndex" && valueName != "DCSettingIndex") ||
            !entries.is_array() || entries.empty()) {
            throw std::runtime_error("invalid power registry batch");
        }
        const auto parseGuid = [](const std::string& text, GUID& guid) -> bool {
            auto wide = U2W(text);
            if (wide.empty()) return false;
            if (wide.front() != L'{') wide.insert(wide.begin(), L'{');
            if (wide.back() != L'}') wide.push_back(L'}');
            return !wide.empty() && SUCCEEDED(CLSIDFromString(wide.c_str(), &guid));
        };
        GUID schemeGuid{};
        GUID subGroupGuid{};
        const bool policyGuidsValid = parseGuid(scheme, schemeGuid) &&
            parseGuid(subGroup, subGroupGuid);

        // Equivalent to `powercfg /attributes SUB_PROCESSOR <setting>
        // -ATTRIB_HIDE`. This is deliberately one-shot: CPU floating can
        // submit batches several times per second.
        if (policyGuidsValid && _stricmp(subGroup.c_str(),
                "54533251-82be-4824-96c1-47b60b740d00") == 0) {
            static std::once_flag processorUnlockOnce;
            std::call_once(processorUnlockOnce, [&]() {
                const auto processorPath =
                    L"SYSTEM\\CurrentControlSet\\Control\\Power\\PowerSettings\\" + U2W(subGroup);
                HKEY hProcessor = nullptr;
                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, processorPath.c_str(), 0,
                                  KEY_READ | KEY_WRITE, &hProcessor) != ERROR_SUCCESS) {
                    return;
                }
                for (DWORD index = 0; ; ++index) {
                    wchar_t name[80]{};
                    DWORD nameLen = static_cast<DWORD>(std::size(name));
                    const LONG enumResult = RegEnumKeyExW(hProcessor, index, name, &nameLen,
                                                          nullptr, nullptr, nullptr, nullptr);
                    if (enumResult == ERROR_NO_MORE_ITEMS) break;
                    if (enumResult != ERROR_SUCCESS) continue;
                    GUID settingGuid{};
                    std::wstring guidText(name, nameLen);
                    if (guidText.front() != L'{') guidText = L"{" + guidText + L"}";
                    if (FAILED(CLSIDFromString(guidText.c_str(), &settingGuid))) continue;
                    HKEY hSetting = nullptr;
                    if (RegOpenKeyExW(hProcessor, name, 0, KEY_READ | KEY_WRITE, &hSetting) != ERROR_SUCCESS)
                        continue;
                    DWORD attributes = 0;
                    DWORD attributesSize = sizeof(attributes);
                    DWORD attributesType = 0;
                    const LONG readResult = RegQueryValueExW(hSetting, L"Attributes", nullptr,
                                                              &attributesType,
                                                              reinterpret_cast<BYTE*>(&attributes),
                                                              &attributesSize);
                    if (readResult == ERROR_SUCCESS && attributesType == REG_DWORD &&
                        (attributes & POWER_ATTRIBUTE_HIDE) != 0) {
                        const DWORD visibleAttributes = attributes & ~POWER_ATTRIBUTE_HIDE;
                        if (PowerWriteSettingAttributes(&subGroupGuid, &settingGuid,
                                                        visibleAttributes) != ERROR_SUCCESS) {
                            RegSetValueExW(hSetting, L"Attributes", 0, REG_DWORD,
                                           reinterpret_cast<const BYTE*>(&visibleAttributes),
                                           sizeof(visibleAttributes));
                        }
                    }
                    RegCloseKey(hSetting);
                }
                RegCloseKey(hProcessor);
            });
        }

        json failed = json::array();
        int written = 0;
        for (const auto& entry : entries) {
            const auto setting = entry.value("setting", std::string{});
            const auto& rawValue = entry["value"];
            if (setting.empty() || !rawValue.is_number_integer()) {
                failed.push_back({{"setting", setting}, {"code", ERROR_INVALID_DATA}});
                continue;
            }
            const auto value = rawValue.get<int64_t>();
            if (value < 0 || value > static_cast<int64_t>(UINT32_MAX)) {
                failed.push_back({{"setting", setting}, {"code", ERROR_INVALID_DATA}});
                continue;
            }
            const DWORD dw = static_cast<DWORD>(value);
            LONG result = ERROR_INVALID_PARAMETER;
            if (policyGuidsValid) {
                GUID settingGuid{};
                if (parseGuid(setting, settingGuid)) {
                    const DWORD attributes = PowerReadSettingAttributes(&subGroupGuid, &settingGuid);
                    if ((attributes & POWER_ATTRIBUTE_HIDE) != 0) {
                        PowerWriteSettingAttributes(&subGroupGuid, &settingGuid,
                                                    attributes & ~POWER_ATTRIBUTE_HIDE);
                    }
                    result = valueName == "ACSettingIndex"
                        ? static_cast<LONG>(PowerWriteACValueIndex(
                            nullptr, &schemeGuid, &subGroupGuid, &settingGuid, dw))
                        : static_cast<LONG>(PowerWriteDCValueIndex(
                            nullptr, &schemeGuid, &subGroupGuid, &settingGuid, dw));
                } else {
                    result = ERROR_INVALID_DATA;
                }
            }
            if (result != ERROR_SUCCESS) {
                // Correct layout is ...\\<subgroup>\\<setting>\\ACSettingIndex,
                // not a value named <setting> directly under the subgroup.
                const auto settingPath =
                    L"SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes\\" +
                    U2W(scheme) + L"\\" + U2W(subGroup) + L"\\" + U2W(setting);
                HKEY hSetting = nullptr;
                if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, settingPath.c_str(), 0, nullptr,
                                    0, KEY_SET_VALUE, nullptr, &hSetting, nullptr) == ERROR_SUCCESS) {
                    result = RegSetValueExW(hSetting, U2W(valueName).c_str(), 0, REG_DWORD,
                                            reinterpret_cast<const BYTE*>(&dw), sizeof(dw));
                    RegCloseKey(hSetting);
                }
            }
            if (result == ERROR_SUCCESS) {
                ++written;
            } else {
                failed.push_back({{"setting", setting}, {"code", result}});
            }
        }
        return json{{"ok", failed.empty()}, {"written", written}, {"failed", failed}};
    });
    ipc_on("registry.delete", [](const json& a) -> json {
        auto root = a.value("root", std::string{"HKCU"});
        auto path = a.value("path", std::string{});
        auto name = a.value("name", std::string{});
        if (name.empty()) {
            // Delete entire key
            return RegDeleteTreeW(parseRootKey(root), U2W(path).c_str()) == ERROR_SUCCESS;
        }
        HKEY hKey;
        if (RegOpenKeyExW(parseRootKey(root), U2W(path).c_str(), 0, KEY_WRITE, &hKey) != ERROR_SUCCESS)
            return false;
        auto result = RegDeleteValueW(hKey, U2W(name).c_str());
        RegCloseKey(hKey);
        return result == ERROR_SUCCESS;
    });
    ipc_on("registry.exists", [](const json& a) -> json {
        auto root = a.value("root", std::string{"HKCU"});
        auto path = a.value("path", std::string{});
        HKEY hKey;
        if (RegOpenKeyExW(parseRootKey(root), U2W(path).c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
            return false;
        RegCloseKey(hKey);
        return true;
    });
}

// ================================================================
//  Commands: Deep link / URL protocol
// ================================================================

static void reg_protocol() {
    ipc_on("protocol.register", [](const json& a) -> json {
        auto scheme = a.value("scheme", std::string{});
        auto desc   = a.value("description", scheme + " Protocol");
        if (scheme.empty()) throw std::runtime_error("scheme is required");

        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);

        auto wScheme = U2W(scheme);
        auto regPath = L"Software\\Classes\\" + wScheme;

        HKEY hKey;
        // Create scheme key
        if (RegCreateKeyExW(HKEY_CURRENT_USER, regPath.c_str(), 0, nullptr,
            0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
            throw std::runtime_error("Cannot register protocol");

        auto wDesc = U2W(desc);
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)wDesc.c_str(), (DWORD)((wDesc.size()+1)*sizeof(wchar_t)));
        auto urlProto = L"URL Protocol";
        RegSetValueExW(hKey, L"URL Protocol", 0, REG_SZ, (BYTE*)L"", sizeof(wchar_t));
        RegCloseKey(hKey);

        // Create shell\open\command key
        auto cmdPath = regPath + L"\\shell\\open\\command";
        if (RegCreateKeyExW(HKEY_CURRENT_USER, cmdPath.c_str(), 0, nullptr,
            0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
            throw std::runtime_error("Cannot register protocol command");

        auto cmd = L"\"" + std::wstring(exePath) + L"\" \"%1\"";
        RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)cmd.c_str(), (DWORD)((cmd.size()+1)*sizeof(wchar_t)));
        RegCloseKey(hKey);
        return true;
    });
    ipc_on("protocol.unregister", [](const json& a) -> json {
        auto scheme = a.value("scheme", std::string{});
        if (scheme.empty()) throw std::runtime_error("scheme is required");
        auto regPath = L"Software\\Classes\\" + U2W(scheme);
        return RegDeleteTreeW(HKEY_CURRENT_USER, regPath.c_str()) == ERROR_SUCCESS;
    });
}

// ================================================================
//  Commands: Logging
// ================================================================

static void writeLog(const std::string& level, const std::string& msg) {
    if (g_logFile.empty()) return;
    std::lock_guard<std::mutex> lock(g_logMtx);
    std::ofstream f(g_logFile, std::ios::app);
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    char ts[32];
    snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    f << "[" << ts << "] [" << level << "] " << msg << "\n";
}

static void reg_log() {
    ipc_on("log.setFile", [](const json& a) -> json {
        auto path = a.value("path", std::string{});
        if (path.empty()) {
            // Default to data/app.log
            g_logFile = app_data_dir() + L"\\app.log";
        } else {
            g_logFile = U2W(path);
        }
        // Ensure parent directory exists
        auto parent = fspath::path(g_logFile).parent_path();
        fspath::create_directories(parent);
        return W2U(g_logFile);
    });
    ipc_on("log.write", [](const json& a) -> json {
        auto level = a.value("level", std::string{"info"});
        auto msg   = a.value("message", std::string{});
        writeLog(level, msg);
        return true;
    });
    ipc_on("log.clear", [](const json&) -> json {
        if (g_logFile.empty()) return false;
        std::ofstream f(g_logFile, std::ios::trunc);
        return f.good();
    });
    ipc_on("log.getPath", [](const json&) -> json {
        return g_logFile.empty() ? nullptr : json(W2U(g_logFile));
    });
}

// ================================================================
//  Commands: Auto-update
// ================================================================

static json httpGet(const std::string& url) {
    URL_COMPONENTS uc;
    wchar_t host[256]{}, path[2048]{}, extra[2048]{};
    std::wstring objectPath;
    if (!crackHttpUrl(url, uc, host, path, extra, objectPath))
        throw std::runtime_error("Invalid URL");
    bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    HINTERNET hSession = WinHttpOpen(L"QQ/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) throw std::runtime_error("WinHttpOpen failed");
    setHttpTimeouts(hSession);
    // raw.githubusercontent.com 会 302 跳转到 CDN，必须自动跟随重定向
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hSession, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
    HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); throw std::runtime_error("WinHttpConnect failed"); }
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", objectPath.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        throw std::runtime_error("WinHttpOpenRequest failed");
    }
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        throw std::runtime_error("Request failed");
    }
    std::string body;
    constexpr size_t MAX_MANIFEST_BYTES = 4u << 20;
    const ULONGLONG deadline = GetTickCount64() + 60000;
    DWORD avail, rd;
    while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
        if (g_poolCancel.load(std::memory_order_acquire) || GetTickCount64() >= deadline ||
            body.size() + static_cast<size_t>(avail) > MAX_MANIFEST_BYTES) {
            WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
            throw std::runtime_error("Update manifest request cancelled, timed out, or exceeded size limit");
        }
        std::string chunk(avail, 0);
        WinHttpReadData(hRequest, chunk.data(), avail, &rd);
        chunk.resize(rd); body += chunk;
    }
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
    if (statusCode < 200 || statusCode >= 300) {
        std::string preview = body.size() > 200 ? body.substr(0, 200) : body;
        throw std::runtime_error("HTTP " + std::to_string(statusCode) + ": " + preview);
    }
    return json::parse(body);
}

static std::string downloadWinHttpError(const char* operation, DWORD error) {
    return std::string(operation) + " failed (WinHTTP/Win32 " + std::to_string(error) + ")";
}

static DownloadAttemptResult downloadFileAttempt(
    const std::string& url, const std::wstring& dest,
    const std::function<void(uint64_t, uint64_t)>& progress,
    ULONGLONG deadline) {
    DownloadAttemptResult result;
    URL_COMPONENTS uc;
    wchar_t host[256]{}, path[2048]{}, extra[2048]{};
    std::wstring objectPath;
    HINTERNET hS = nullptr;
    HINTERNET hC = nullptr;
    HINTERNET hR = nullptr;
    std::ofstream out;

    auto closeHandles = [&]() {
        if (hR) { WinHttpCloseHandle(hR); hR = nullptr; }
        if (hC) { WinHttpCloseHandle(hC); hC = nullptr; }
        if (hS) { WinHttpCloseHandle(hS); hS = nullptr; }
    };
    auto removePartial = [&]() {
        std::error_code ec;
        fspath::remove(dest, ec);
    };
    auto fail = [&](std::string error, DWORD win32Error = ERROR_SUCCESS,
                    bool retryable = true) -> DownloadAttemptResult {
        if (out.is_open()) out.close();
        closeHandles();
        removePartial();
        result.ok = false;
        result.retryable = retryable;
        result.error = std::move(error);
        result.win32Error = win32Error;
        return result;
    };
    auto remainingMs = [&]() -> DWORD {
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) return 0;
        return static_cast<DWORD>((std::min)(deadline - now, static_cast<ULONGLONG>(0xFFFFFFFFu)));
    };
    auto attemptTimeoutMs = [&]() -> DWORD {
        return (std::min)(remainingMs(), DEFAULT_HTTP_TIMEOUT_MS);
    };

    std::error_code cleanupEc;
    fspath::remove(dest, cleanupEc);
    if (cleanupEc)
        return fail("Cannot remove the previous download package", ERROR_ACCESS_DENIED, false);
    if (g_poolCancel.load(std::memory_order_acquire))
        return fail("Download cancelled", ERROR_CANCELLED, false);
    if (!crackHttpUrl(url, uc, host, path, extra, objectPath))
        return fail("Invalid download URL", ERROR_INVALID_PARAMETER, false);
    if (uc.nScheme != INTERNET_SCHEME_HTTPS)
        return fail("Only HTTPS download URLs are supported", ERROR_INVALID_PARAMETER, false);
    if (remainingMs() == 0)
        return fail("Download retry window expired", ERROR_WINHTTP_TIMEOUT, true);

    const bool steamHost = isSteamHostName(host);
    hS = WinHttpOpen(L"QQ/1.0",
        steamHost ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) {
        const DWORD error = GetLastError();
        return fail(downloadWinHttpError("WinHttpOpen", error), error);
    }
    setHttpTimeouts(hS, attemptTimeoutMs());
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    if (!WinHttpSetOption(hS, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy))) {
        const DWORD error = GetLastError();
        return fail(downloadWinHttpError("WinHttpSetOption(redirect)", error), error);
    }
    hC = WinHttpConnect(hS, host, uc.nPort, 0);
    if (!hC) {
        const DWORD error = GetLastError();
        return fail(downloadWinHttpError("WinHttpConnect", error), error);
    }
    hR = WinHttpOpenRequest(hC, L"GET", objectPath.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hR) {
        const DWORD error = GetLastError();
        return fail(downloadWinHttpError("WinHttpOpenRequest", error), error);
    }
    if (steamHost) {
        DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                              SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                              SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
        if (!WinHttpSetOption(hR, WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags, sizeof(securityFlags))) {
            const DWORD error = GetLastError();
            return fail(downloadWinHttpError("WinHttpSetOption(security)", error), error);
        }
    }
    const wchar_t* requestHeaders =
        L"Accept: video/webm,video/mp4,application/octet-stream,*/*\r\n"
        L"Accept-Encoding: identity\r\n"
        L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/131.0 Safari/537.36\r\n"
        L"Referer: https://store.steampowered.com/\r\n";
    if (!WinHttpAddRequestHeaders(hR, requestHeaders, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD)) {
        const DWORD error = GetLastError();
        return fail(downloadWinHttpError("WinHttpAddRequestHeaders", error), error);
    }
    if (remainingMs() == 0)
        return fail("Download retry window expired", ERROR_WINHTTP_TIMEOUT, true);
    setHttpTimeouts(hR, attemptTimeoutMs());
    if (!WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        const DWORD error = GetLastError();
        return fail(downloadWinHttpError("WinHttpSendRequest", error), error);
    }
    if (!WinHttpReceiveResponse(hR, nullptr)) {
        const DWORD error = GetLastError();
        return fail(downloadWinHttpError("WinHttpReceiveResponse", error), error);
    }
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(hR, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
        const DWORD error = GetLastError();
        return fail(downloadWinHttpError("WinHttpQueryHeaders(status)", error), error);
    }
    result.httpStatus = statusCode;
    if (statusCode < 200 || statusCode >= 300) {
        const bool retryable = statusCode == 408 || statusCode == 425 || statusCode == 429 || statusCode >= 500;
        return fail("HTTP " + std::to_string(statusCode), ERROR_SUCCESS, retryable);
    }
    DWORD expectedLength = 0;
    DWORD expectedLengthSize = sizeof(expectedLength);
    const bool hasExpectedLength = WinHttpQueryHeaders(
        hR, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &expectedLength, &expectedLengthSize, WINHTTP_NO_HEADER_INDEX) != FALSE;
    result.expectedBytes = hasExpectedLength ? expectedLength : 0;
    out.open(dest, std::ios::binary | std::ios::trunc);
    if (!out)
        return fail("Cannot open download destination for writing", ERROR_ACCESS_DENIED, false);

    uint64_t receivedLength = 0;
    DWORD avail = 0, rd = 0;
    while (true) {
        if (g_poolCancel.load(std::memory_order_acquire))
            return fail("Download cancelled", ERROR_CANCELLED, false);
        if (remainingMs() == 0)
            return fail("Download attempt timed out", ERROR_WINHTTP_TIMEOUT, true);
        setHttpTimeouts(hR, attemptTimeoutMs());
        if (!WinHttpQueryDataAvailable(hR, &avail)) {
            const DWORD error = GetLastError();
            result.receivedBytes = receivedLength;
            return fail(downloadWinHttpError("WinHttpQueryDataAvailable", error), error);
        }
        if (avail == 0) break;
        const DWORD readSize = (std::min)(avail, static_cast<DWORD>(1u << 20));
        std::string chunk(readSize, 0);
        setHttpTimeouts(hR, attemptTimeoutMs());
        if (!WinHttpReadData(hR, chunk.data(), readSize, &rd)) {
            const DWORD error = GetLastError();
            result.receivedBytes = receivedLength;
            return fail(downloadWinHttpError("WinHttpReadData", error), error);
        }
        if (rd == 0) {
            result.receivedBytes = receivedLength;
            return fail("WinHttpReadData returned no data", ERROR_WINHTTP_CONNECTION_ERROR, true);
        }
        out.write(chunk.data(), rd);
        receivedLength += rd;
        result.receivedBytes = receivedLength;
        if (!out)
            return fail("Failed to write the downloaded package", ERROR_WRITE_FAULT, false);
        if (progress) progress(receivedLength, hasExpectedLength ? expectedLength : 0);
    }
    out.close();
    closeHandles();
    result.receivedBytes = receivedLength;
    if (hasExpectedLength && receivedLength != expectedLength) {
        result.error = "Downloaded size mismatch (received " + std::to_string(receivedLength) +
            ", expected " + std::to_string(expectedLength) + ")";
        result.retryable = true;
        removePartial();
        return result;
    }
    result.ok = true;
    result.retryable = false;
    result.error.clear();
    return result;
}

static bool downloadFile(const std::string& url, const std::wstring& dest,
                         const std::function<void(uint64_t, uint64_t)>& progress) {
    const ULONGLONG deadline = GetTickCount64() + 15ULL * 60ULL * 1000ULL;
    return downloadFileAttempt(url, dest, progress, deadline).ok;
}

static bool webmReadVint(const std::vector<unsigned char>& data, size_t pos, size_t& width, uint64_t& value) {
    if (pos >= data.size()) return false;
    const unsigned char first = data[pos];
    unsigned char mask = 0x80;
    width = 1;
    while (width <= 8 && !(first & mask)) { mask >>= 1; width++; }
    if (width > 8 || pos + width > data.size()) return false;
    value = first & (mask - 1);
    for (size_t i = 1; i < width; i++) value = (value << 8) | data[pos + i];
    return true;
}

static bool webmDurationSeconds(const std::wstring& path, double& seconds) {
    seconds = 0.0;
    std::error_code ec;
    const auto fileSize = fspath::file_size(path, ec);
    if (ec || fileSize == 0 || fileSize > 300ULL * 1024ULL * 1024ULL) return false;
    const size_t readSize = (size_t)std::min<uintmax_t>(fileSize, 8ULL * 1024ULL * 1024ULL);
    std::vector<unsigned char> data(readSize);
    std::ifstream in(path, std::ios::binary);
    if (!in || !in.read(reinterpret_cast<char*>(data.data()), (std::streamsize)data.size())) return false;
    uint64_t timecodeScale = 1000000;
    double durationValue = 0.0;
    bool foundDuration = false;
    for (size_t i = 0; i + 2 < data.size(); i++) {
        if (data[i] == 0x2A && data[i + 1] == 0xD7 && data[i + 2] == 0xB1) {
            size_t width = 0; uint64_t value = 0;
            if (webmReadVint(data, i + 3, width, value) && value > 0 && value <= 8 && i + 3 + width + value <= data.size()) {
                uint64_t scale = 0;
                for (size_t j = 0; j < value; j++) scale = (scale << 8) | data[i + 3 + width + j];
                if (scale > 0) timecodeScale = scale;
            }
        }
        if (data[i] == 0x44 && data[i + 1] == 0x89) {
            size_t width = 0; uint64_t value = 0;
            if (!webmReadVint(data, i + 2, width, value) || (value != 4 && value != 8) || i + 2 + width + value > data.size()) continue;
            const unsigned char* raw = data.data() + i + 2 + width;
            if (value == 8) {
                uint64_t bits = 0; for (size_t j = 0; j < 8; j++) bits = (bits << 8) | raw[j];
                std::memcpy(&durationValue, &bits, sizeof(durationValue));
            } else {
                uint32_t bits = 0; for (size_t j = 0; j < 4; j++) bits = (bits << 8) | raw[j];
                float f = 0.0f; std::memcpy(&f, &bits, sizeof(f)); durationValue = f;
            }
            if (std::isfinite(durationValue) && durationValue > 0.0) {
                foundDuration = true;
                // Stop at the first Duration in the Info element. Random 0x4489
                // byte sequences inside media clusters are not EBML elements.
                break;
            }
        }
    }
    if (!foundDuration) return false;
    seconds = durationValue * (double)timecodeScale / 1000000000.0;
    return std::isfinite(seconds) && seconds > 0.0;
}

// 计算文件 SHA-256（CryptoAPI，advapi32 已链接），返回小写 hex 串
static std::string sha256File(const std::wstring& path) {
    HCRYPTPROV h = 0;
    if (!CryptAcquireContextW(&h, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return {};
    HCRYPTHASH hh = 0;
    if (!CryptCreateHash(h, CALG_SHA_256, 0, 0, &hh)) { CryptReleaseContext(h, 0); return {}; }
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { CryptDestroyHash(hh); CryptReleaseContext(h, 0); return {}; }
    BYTE buf[65536]; DWORD rd = 0;
    bool ok = true;
    while (true) {
        if (!ReadFile(f, buf, sizeof(buf), &rd, nullptr)) { ok = false; break; }
        if (rd == 0) break;
        if (!CryptHashData(hh, buf, rd, 0)) { ok = false; break; }
    }
    CloseHandle(f);
    BYTE dig[32]; DWORD diglen = sizeof(dig);
    std::string out;
    if (ok && CryptGetHashParam(hh, HP_HASHVAL, dig, &diglen, 0)) {
        wchar_t hex[3];
        for (int i = 0; i < 32; i++) { wsprintfW(hex, L"%02x", dig[i]); out += (char)hex[0]; out += (char)hex[1]; }
    }
    CryptDestroyHash(hh); CryptReleaseContext(h, 0);
    return out;
}

struct StrictUpdateVersion {
    uint32_t major = 0;
    uint32_t minor = 0;
    uint32_t patch = 0;
};

static bool parseStrictUpdateVersion(const std::string& value, StrictUpdateVersion& out) {
    if (value.empty()) return false;
    uint32_t parts[3]{};
    size_t start = 0;
    for (size_t index = 0; index < 3; index++) {
        const size_t end = index < 2 ? value.find('.', start) : value.size();
        if (end == std::string::npos || end == start) return false;
        if (index == 2 && value.find('.', start) != std::string::npos) return false;
        if (index < 2 && end == value.size() - 1) return false;
        if (end - start > 1 && value[start] == '0') return false;
        uint64_t part = 0;
        for (size_t cursor = start; cursor < end; cursor++) {
            const unsigned char ch = static_cast<unsigned char>(value[cursor]);
            if (!std::isdigit(ch)) return false;
            const uint64_t digit = static_cast<uint64_t>(ch - '0');
            const uint64_t limit = static_cast<uint64_t>(std::numeric_limits<int32_t>::max());
            if (part > (limit - digit) / 10) return false;
            part = part * 10 + digit;
        }
        parts[index] = static_cast<uint32_t>(part);
        start = end + 1;
    }
    out = {parts[0], parts[1], parts[2]};
    return true;
}

static int compareStrictUpdateVersions(const StrictUpdateVersion& left, const StrictUpdateVersion& right) {
    if (left.major != right.major) return left.major < right.major ? -1 : 1;
    if (left.minor != right.minor) return left.minor < right.minor ? -1 : 1;
    if (left.patch != right.patch) return left.patch < right.patch ? -1 : 1;
    return 0;
}

static StrictUpdateVersion requireStrictUpdateVersion(const std::string& value, const char* label) {
    StrictUpdateVersion parsed{};
    if (!parseStrictUpdateVersion(value, parsed))
        throw std::runtime_error(std::string(label) + " must be a strict x.y.z version");
    return parsed;
}

static StrictUpdateVersion requireNewerUpdateVersion(const std::string& value) {
    const auto requested = requireStrictUpdateVersion(value, "Requested update version");
    const auto current = requireStrictUpdateVersion(APP_VER_STR, "Current application version");
    if (compareStrictUpdateVersions(requested, current) <= 0)
        throw std::runtime_error("Update version must be newer than the installed version");
    return requested;
}

static bool isStrictUpdateSha256(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isxdigit(ch) != 0;
    });
}

static std::string readPackagedUpdateVersion(const std::wstring& staging) {
    auto raw = sgReadFile(staging + L"\\YeManCC\\version.json");
    if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB && static_cast<unsigned char>(raw[2]) == 0xBF) {
        raw.erase(0, 3);
    }
    if (raw.empty()) throw std::runtime_error("Update package missing YeManCC/version.json");
    try {
        const auto manifest = json::parse(raw);
        if (!manifest.is_object()) throw std::runtime_error("Update package version.json is not an object");
        const auto version = manifest.value("version", std::string{});
        requireStrictUpdateVersion(version, "Update package version");
        return version;
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("Update package version.json is invalid: ") + e.what());
    }
}

static bool updatePackageLayoutIsSafe(const std::wstring& staging) {
    std::error_code ec;
    const fspath::path root(staging);
    for (fspath::directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) {
        const auto name = it->path().filename().wstring();
        if (_wcsicmp(name.c_str(), L"YeManCC") != 0 && _wcsicmp(name.c_str(), L"PowerControl") != 0)
            return false;
    }
    if (ec) return false;
    for (fspath::recursive_directory_iterator it(root, fspath::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec)) {
        if (it->is_symlink(ec)) return false;
        if (ec) return false;
        if (it->is_directory(ec)) { if (ec) return false; continue; }
        if (it->is_regular_file(ec)) { if (ec) return false; continue; }
        return false;
    }
    return !ec;
}

// 用系统 tar.exe 解压 zip 到目标目录（Windows 10+ 自带，无需第三方库）
static bool unzipTar(const std::wstring& zip, const std::wstring& dest) {
    std::error_code ec; fspath::create_directories(dest, ec);
    std::wstring cmd = L"\"C:\\Windows\\System32\\tar.exe\" -xf \"" + zip + L"\" -C \"" + dest + L"\"";
    const RunOut result = runCapture(cmd, 120000);
    return result.ran && result.exitCode == 0;
}

// ── 更新加速器（steamcommunity_302 等）：辅助 GitHub 更新下载 ──
// 路径固定指向 C:\\SOFT\\steamcommunity\\steamcommunity_302.cli.exe。native 的 WinHTTP 已改为
// 自动跟随系统代理，该工具运行后更新请求即走加速通道。此功能为手动触发，不会随程序启动。
static const std::wstring g_updateAccelPath = L"C:\\SOFT\\steamcommunity\\steamcommunity_302.cli.exe";
static bool isUpdateAccelRunning() {
    std::wstring target = sgBaseName(g_updateAccelPath);
    if (target.empty()) return false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{ sizeof(pe) };
    bool running = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            std::wstring exe = pe.szExeFile;
            if (exe.size() > 4 && _wcsicmp(exe.c_str() + exe.size() - 4, L".exe") == 0)
                exe.resize(exe.size() - 4);
            if (_wcsicmp(exe.c_str(), target.c_str()) == 0) { running = true; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return running;
}
static bool startUpdateAccel() {
    if (g_updateAccelPath.empty() || !fspath::exists(g_updateAccelPath)) return false;
    if (isUpdateAccelRunning()) return true;
    HINSTANCE r = ShellExecuteW(nullptr, L"open", g_updateAccelPath.c_str(), nullptr, nullptr, SW_HIDE);
    return (reinterpret_cast<intptr_t>(r) > 32);
}
static bool stopUpdateAccel() {
    std::wstring target = sgBaseName(g_updateAccelPath);
    if (target.empty()) return false;
    bool killed = false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{ sizeof(pe) };
        if (Process32FirstW(snap, &pe)) {
            do {
                std::wstring exe = pe.szExeFile;
                if (exe.size() > 4 && _wcsicmp(exe.c_str() + exe.size() - 4, L".exe") == 0)
                    exe.resize(exe.size() - 4);
                if (_wcsicmp(exe.c_str(), target.c_str()) == 0) {
                    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (h) { TerminateProcess(h, 0); CloseHandle(h); killed = true; }
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
    return killed;
}

// Move the support page produced by the first updater version from the shared
// root into the program directory. New releases keep it beside YeManCC.exe.
static void migrate_legacy_support_page() {
    wchar_t exePath[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return;
    std::wstring exeStr(exePath);
    auto exeDir = exeStr.substr(0, exeStr.find_last_of(L"\\"));
    auto root = exeDir.substr(0, exeDir.find_last_of(L"\\"));
    auto legacy = root + L"\\YeMan-Support.html";
    auto current = exeDir + L"\\YeMan-Support.html";
    std::error_code ec;
    if (fspath::exists(legacy) && !fspath::exists(current)) {
        fspath::copy_file(legacy, current, fspath::copy_options::skip_existing, ec);
    }
}

static bool is_legacy_power_control_user_file(const fspath::path& relative) {
    auto value = relative.generic_wstring();
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    if (value.ends_with(L".json") || value.ends_with(L".json.bak") || value.ends_with(L".bak")) return true;
    if (value.rfind(L"ui-background/", 0) == 0) return true;
    if (value.rfind(L"steamcharts/", 0) == 0) return true;
    return false;
}

// v0.0.3 and earlier local updater builds could leave the packaged dependency
// directory beside YeManCC.exe. Move only release files to the canonical
// sibling directory, preserving user state and media if an old folder contains
// any of those files. A successful package-only migration removes the stale
// directory so future launches cannot use it again.
static void migrate_legacy_power_control_dir() {
    const fspath::path fixed(L"C:\\SOFT\\YeMan\\PowerControl");
    const fspath::path legacy = fspath::path(exe_dir()) / "PowerControl";
    std::error_code ec;
    if (!fspath::is_directory(legacy, ec)) return;
    ec.clear();
    if (fspath::equivalent(legacy, fixed, ec)) return;

    ec.clear();
    fspath::create_directories(fixed, ec);
    if (ec) {
        traceLog("legacy PowerControl migration could not create target");
        return;
    }

    bool failed = false;
    for (fspath::recursive_directory_iterator it(legacy, ec), end; it != end && !ec; it.increment(ec)) {
        const auto relative = fspath::relative(it->path(), legacy, ec);
        if (ec) break;
        if (it->is_directory(ec)) continue;
        if (ec) break;
        const bool isUserFile = is_legacy_power_control_user_file(relative);
        const auto destination = fixed / relative;
        fspath::create_directories(destination.parent_path(), ec);
        if (ec) { failed = true; break; }
        const auto copyMode = isUserFile
            ? fspath::copy_options::skip_existing
            : fspath::copy_options::overwrite_existing;
        fspath::copy_file(it->path(), destination, copyMode, ec);
        if (ec) { failed = true; break; }
    }
    if (ec) failed = true;
    if (failed) {
        traceLog("legacy PowerControl migration failed; keeping old directory");
        return;
    }
    ec.clear();
    fspath::remove_all(legacy, ec);
    if (ec) traceLog("legacy PowerControl migrated but stale directory cleanup failed");
}

static void reg_updater() {
    ipc_on("app.version", [](const json&) -> json {
        return APP_VER_STR;
    });
    ipc_on("app.checkUpdate", [](const json& a) -> json {
        auto url = a.value("url", std::string{});
        if (url.empty()) throw std::runtime_error("url is required");
        return httpGet(url);
    });
    ipc_on("app.updateState", [](const json&) -> json {
        return updateProgressRead();
    });
    // 下载完整更新包（exe + index.html + assets + ...）到 %LOCALAPPDATA%\YeManCC\update\package.zip，强制 SHA-256 校验
    ipc_on("app.downloadUpdate", [](const json& a) -> json {
        auto url = a.value("url", std::string{});
        if (url.empty()) throw std::runtime_error("url is required");
        auto sha = a.value("sha256", std::string{});
        auto operationId = a.value("operationId", std::string{});
        auto version = a.value("version", std::string{});
        if (operationId.empty()) operationId = "update-" + std::to_string(GetTickCount64());
        auto failBeforeDownload = [&](const std::string& error) -> void {
            updateProgressPost({
                {"operationId", operationId}, {"phase", "failed"}, {"version", version},
                {"error", error}, {"message", std::string("无法开始更新：") + error},
                {"updatedAt", static_cast<int64_t>(time(nullptr) * 1000)}
            });
            throw std::runtime_error(error);
        };
        if (!isStrictUpdateSha256(sha)) failBeforeDownload("A valid SHA-256 is required");
        try { requireNewerUpdateVersion(version); }
        catch (const std::exception& e) { failBeforeDownload(e.what()); }
        std::error_code ec; fspath::create_directories(app_data_dir() + L"\\update", ec);
        if (ec) throw std::runtime_error("Failed to create update directory");
        auto dest = app_data_dir() + L"\\update\\package.zip";
        fspath::remove(dest, ec);
        updateProgressPost({
            {"operationId", operationId}, {"phase", "downloading"},
            {"version", version},
            {"downloadedBytes", 0}, {"totalBytes", 0}, {"percent", 0},
            {"speedBps", 0}, {"etaSeconds", 0}, {"message", "正在下载更新包"},
            {"updatedAt", static_cast<int64_t>(time(nullptr) * 1000)}
        });
        try {
            const ULONGLONG retryDeadline = GetTickCount64() + UPDATE_RETRY_WINDOW_MS;
            uint64_t attempt = 0;
            DownloadAttemptResult lastFailure;
            bool downloaded = false;
            while (!downloaded) {
                if (g_poolCancel.load(std::memory_order_acquire))
                    throw std::runtime_error("Download cancelled during shutdown");
                const ULONGLONG attemptStarted = GetTickCount64();
                if (attemptStarted >= retryDeadline)
                    break;
                attempt++;
                ULONGLONG lastTick = attemptStarted;
                uint64_t lastBytes = 0;
                auto result = downloadFileAttempt(url, dest, [&](uint64_t received, uint64_t total) {
                    const ULONGLONG now = GetTickCount64();
                    if (now - lastTick < 250 && received != total) return;
                    const double seconds = (std::max)(0.001, (now - lastTick) / 1000.0);
                    const uint64_t delta = received >= lastBytes ? received - lastBytes : 0;
                    const double speed = delta / seconds;
                    const double percent = total > 0 ? (std::min)(100.0, received * 100.0 / total) : 0.0;
                    const int64_t eta = total > received && speed > 1.0
                        ? static_cast<int64_t>((total - received) / speed) : 0;
                    updateProgressPost({
                        {"operationId", operationId}, {"phase", "downloading"},
                        {"version", version}, {"attempt", attempt},
                        {"downloadedBytes", received}, {"totalBytes", total},
                        {"percent", percent}, {"speedBps", speed}, {"etaSeconds", eta},
                        {"message", "正在下载更新包（第 " + std::to_string(attempt) + " 次尝试）"},
                        {"updatedAt", static_cast<int64_t>(time(nullptr) * 1000)}
                    });
                    lastTick = now;
                    lastBytes = received;
                }, retryDeadline);
                if (result.ok) {
                    updateProgressPost({
                        {"operationId", operationId}, {"phase", "validating"},
                        {"version", version}, {"attempt", attempt},
                        {"downloadedBytes", result.receivedBytes},
                        {"totalBytes", result.expectedBytes},
                        {"percent", 100}, {"speedBps", 0}, {"etaSeconds", 0},
                        {"message", "正在校验更新包（第 " + std::to_string(attempt) + " 次尝试）"},
                        {"updatedAt", static_cast<int64_t>(time(nullptr) * 1000)}
                    });
                    const auto got = sha256File(dest);
                    if (isStrictUpdateSha256(got) && _stricmp(got.c_str(), sha.c_str()) == 0) {
                        downloaded = true;
                        break;
                    }
                    result.ok = false;
                    result.retryable = true;
                    result.error = "Checksum mismatch (expected " + sha + ", got " + got + ")";
                    std::error_code checksumEc;
                    fspath::remove(dest, checksumEc);
                }
                lastFailure = std::move(result);
                if (!lastFailure.retryable) {
                    throw std::runtime_error("下载失败，已尝试 " + std::to_string(attempt) +
                        " 次：" + lastFailure.error);
                }

                const ULONGLONG now = GetTickCount64();
                if (now >= retryDeadline || retryDeadline - now <= UPDATE_RETRY_INTERVAL_MS)
                    break;
                const uint64_t nextAttempt = attempt + 1;
                const ULONGLONG remainingAfterWaitMs = retryDeadline - now - UPDATE_RETRY_INTERVAL_MS;
                const uint64_t remainingMinutes = (std::max)(1ULL,
                    (remainingAfterWaitMs + 59999ULL) / 60000ULL);
                const std::string retryMessage = "下载失败，正在重新尝试第 " +
                    std::to_string(nextAttempt) + " 次（剩余约 " +
                    std::to_string(remainingMinutes) + " 分钟）";
                updateProgressPost({
                    {"operationId", operationId}, {"phase", "downloading"},
                    {"version", version}, {"attempt", attempt}, {"nextAttempt", nextAttempt},
                    {"retryInSeconds", UPDATE_RETRY_INTERVAL_MS / 1000},
                    {"remainingRetrySeconds", static_cast<uint64_t>((retryDeadline - now) / 1000ULL)},
                    {"downloadedBytes", lastFailure.receivedBytes},
                    {"totalBytes", lastFailure.expectedBytes},
                    {"percent", 0}, {"speedBps", 0}, {"etaSeconds", 0},
                    {"lastError", lastFailure.error}, {"message", retryMessage},
                    {"updatedAt", static_cast<int64_t>(time(nullptr) * 1000)}
                });

                const ULONGLONG waitDeadline = GetTickCount64() + UPDATE_RETRY_INTERVAL_MS;
                while (GetTickCount64() < waitDeadline) {
                    if (g_poolCancel.load(std::memory_order_acquire))
                        throw std::runtime_error("Download cancelled during shutdown");
                    const ULONGLONG waitRemaining = waitDeadline - GetTickCount64();
                    Sleep(static_cast<DWORD>((std::min)(waitRemaining, 100ULL)));
                }
            }
            if (!downloaded) {
                const std::string detail = lastFailure.error.empty()
                    ? std::string("Download retry window expired") : lastFailure.error;
                throw std::runtime_error("下载失败，已尝试 " + std::to_string(attempt) +
                    " 次，5 分钟内仍未成功：" + detail);
            }
            updateProgressPost({
                {"operationId", operationId}, {"phase", "downloaded"},
                {"version", version},
                {"percent", 100}, {"message", "更新包校验通过"},
                {"updatedAt", static_cast<int64_t>(time(nullptr) * 1000)}
            });
        } catch (const std::exception& e) {
            std::error_code cleanupEc;
            fspath::remove(dest, cleanupEc);
            updateProgressPost({
                {"operationId", operationId}, {"phase", "failed"},
                {"version", version},
                {"error", e.what()}, {"message", std::string("下载失败：") + e.what()},
                {"updatedAt", static_cast<int64_t>(time(nullptr) * 1000)}
            });
            throw;
        }
        return W2U(dest);
    });
    // Install the complete release payload into the sibling YeManCC and
    // PowerControl targets while preserving player-owned files.
    // Merge-copy only: files added by players and absent from the package remain.
    ipc_on("app.installUpdate", [](const json& a) -> json {
        const auto operationId = a.value("operationId", std::string{});
        const auto version = a.value("version", std::string{});
        const auto sha = a.value("sha256", std::string{});
        auto failBeforeInstall = [&](const std::string& error, const std::string& message) -> void {
            updateProgressPost({
                {"operationId", operationId}, {"phase", "failed"}, {"version", version},
                {"error", error}, {"message", message},
                {"updatedAt", static_cast<int64_t>(time(nullptr) * 1000)}
            });
            throw std::runtime_error(error);
        };
        if (!isStrictUpdateSha256(sha)) failBeforeInstall("A valid SHA-256 is required", "更新包 SHA-256 无效");
        try { requireNewerUpdateVersion(version); }
        catch (const std::exception& e) { failBeforeInstall(e.what(), "目标版本不是可安装的新版本"); }
        updateProgressPost({
            {"operationId", operationId}, {"phase", "installing"},
            {"version", version},
            {"percent", 100}, {"message", "正在安装并重启程序"},
            {"updatedAt", static_cast<int64_t>(time(nullptr) * 1000)}
        });
        auto zip = app_data_dir() + L"\\update\\package.zip";
        if (!fspath::exists(zip)) {
            updateProgressPost({{"operationId", operationId}, {"phase", "failed"},
                {"error", "No update package downloaded"}, {"message", "更新包不存在"}});
            throw std::runtime_error("No update package downloaded");
        }
        const auto installHash = sha256File(zip);
        if (!isStrictUpdateSha256(installHash) || _stricmp(installHash.c_str(), sha.c_str()) != 0) {
            std::error_code cleanupEc;
            fspath::remove(zip, cleanupEc);
            updateProgressPost({{"operationId", operationId}, {"phase", "failed"},
                {"error", "Update package checksum mismatch before installation"}, {"message", "安装前校验更新包失败"}});
            throw std::runtime_error("Update package checksum mismatch before installation");
        }
        auto staging = app_data_dir() + L"\\update\\staging";
        std::error_code ec; fspath::remove_all(staging, ec);
        if (!unzipTar(zip, staging)) {
            fspath::remove_all(staging, ec);
            fspath::remove(zip, ec);
            updateProgressPost({{"operationId", operationId}, {"phase", "failed"},
                {"error", "Failed to extract update package"}, {"message", "更新包解压失败"}});
            throw std::runtime_error("Failed to extract update package");
        }
        if (!updatePackageLayoutIsSafe(staging)) {
            fspath::remove_all(staging, ec);
            fspath::remove(zip, ec);
            updateProgressPost({{"operationId", operationId}, {"phase", "failed"}, {"version", version},
                {"error", "Update package contains an unsafe or unexpected layout"}, {"message", "更新包目录结构不安全"}});
            throw std::runtime_error("Update package contains an unsafe or unexpected layout");
        }
        std::string packagedVersion;
        try {
            packagedVersion = readPackagedUpdateVersion(staging);
        } catch (const std::exception& e) {
            fspath::remove_all(staging, ec);
            fspath::remove(zip, ec);
            updateProgressPost({{"operationId", operationId}, {"phase", "failed"}, {"version", version},
                {"error", e.what()}, {"message", "更新包内版本清单无效"}});
            throw;
        }
        if (packagedVersion != version) {
            fspath::remove_all(staging, ec);
            fspath::remove(zip, ec);
            updateProgressPost({{"operationId", operationId}, {"phase", "failed"},
                {"error", "Update package version does not match requested version"}, {"message", "更新包版本与请求版本不一致"}});
            throw std::runtime_error("Update package version does not match requested version");
        }
        wchar_t exePath[MAX_PATH]; GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring exeStr(exePath);
        std::wstring exedir = exeStr.substr(0, exeStr.find_last_of(L"\\"));
        const auto packagedYeManCC = staging + L"\\YeManCC";
        if (!fspath::is_directory(packagedYeManCC)) {
            updateProgressPost({{"operationId", operationId}, {"phase", "failed"},
                {"error", "Update package missing YeManCC directory"}, {"message", "更新包缺少 YeManCC 目录"}});
            throw std::runtime_error("Update package missing YeManCC directory");
        }
        const auto packagedExe = packagedYeManCC + L"\\YeManCC.exe";
        if (!fspath::exists(packagedExe)) {
            updateProgressPost({{"operationId", operationId}, {"phase", "failed"},
                {"error", "Update package missing YeManCC.exe"}, {"message", "更新包缺少 YeManCC.exe"}});
            throw std::runtime_error("Update package missing YeManCC.exe");
        }
        const auto packagedPowerControl = staging + L"\\PowerControl";
        if (!fspath::exists(packagedPowerControl)) {
            updateProgressPost({{"operationId", operationId}, {"phase", "failed"},
                {"error", "Update package missing PowerControl"}, {"message", "更新包缺少 PowerControl 目录"}});
            throw std::runtime_error("Update package missing PowerControl");
        }
        const auto packagedSupport = packagedYeManCC + L"\\YeMan-Support.html";
        if (!fspath::exists(packagedSupport)) {
            updateProgressPost({{"operationId", operationId}, {"phase", "failed"},
                {"error", "Update package missing YeMan-Support.html"}, {"message", "更新包缺少支持页面"}});
            throw std::runtime_error("Update package missing YeMan-Support.html");
        }
        // 依赖包固定目标目录（与前端 yeman.ts 的 PC_DIR 默认一致）
        std::wstring pcDir = L"C:\\SOFT\\YeMan\\PowerControl";
        std::wstring supportPath = exedir + L"\\YeMan-Support.html";
        auto script = app_data_dir() + L"\\update.ps1";
        auto psLiteral = [](const std::wstring& value) {
            std::string s = W2U(value);
            std::string out = "'";
            for (char c : s) {
                if (c == '\'') out += "''";
                else out += c;
            }
            out += "'";
            return out;
        };
        {
            std::ofstream f(script, std::ios::binary);
            f.put((char)0xEF); f.put((char)0xBB); f.put((char)0xBF);
            f << "$ErrorActionPreference = 'Stop'\n";
            f << "$parentPid = " << GetCurrentProcessId() << "\n";
            f << "$zip = " << psLiteral(zip) << "\n";
            f << "$staging = " << psLiteral(staging) << "\n";
            f << "$exePath = " << psLiteral(exePath) << "\n";
            f << "$exeDir = " << psLiteral(exedir) << "\n";
            f << "$programSource = " << psLiteral(packagedYeManCC) << "\n";
            f << "$packageExe = " << psLiteral(packagedExe) << "\n";
            f << "$pcDir = " << psLiteral(pcDir) << "\n";
            f << "$supportPath = " << psLiteral(supportPath) << "\n";
            f << "$backup = $exePath + '.old'\n";
            f << "$newExe = $exePath + '.new'\n";
            f << "$state = Join-Path (Split-Path -Parent $staging) 'update-state.json'\n";
            f << "$rollbackRoot = Join-Path (Split-Path -Parent $staging) ('rollback-' + [guid]::NewGuid().ToString('N'))\n";
            f << "$rollbackFiles = Join-Path $rollbackRoot 'files'\n";
            f << "$rollbackAddedFiles = New-Object 'System.Collections.Generic.List[string]'\n";
            f << "$ordinaryRollbackPrepared = $false\n";
            f << "$scriptPath = " << psLiteral(script) << "\n";
            f << "$progressPath = " << psLiteral(updateProgressPath()) << "\n";
            f << "$operationId = " << psLiteral(U2W(operationId)) << "\n";
            f << "$version = " << psLiteral(U2W(version)) << "\n";
            f << "function Set-UpdateProgress([string]$phase, [string]$message, [string]$error = '') {\n";
            f << "  $obj = [ordered]@{ operationId = $operationId; version = $version; phase = $phase; percent = 100; message = $message; error = $error; updatedAt = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() }\n";
            f << "  $obj | ConvertTo-Json -Compress | Set-Content -LiteralPath $progressPath -Encoding utf8\n";
            f << "}\n";
            // YeManTdpCtl.exe and its PyInstaller _internal directory are a
            // single runtime.  Give transient file locks enough time to
            // clear, then verify every TDP runtime file before committing the
            // main executable update.
            f << "$copyRetries = 60\n";
            f << "$copyWaitSeconds = 2\n";
            f << "$powerControlSource = Join-Path $staging 'PowerControl'\n";
            f << "$tdpSource = Join-Path $powerControlSource 'pawnio'\n";
            f << "$tdpTarget = Join-Path $pcDir 'pawnio'\n";
            f << "$tdpTransactionId = [guid]::NewGuid().ToString('N')\n";
            f << "$tdpStage = Join-Path $pcDir ('pawnio.update-' + $tdpTransactionId)\n";
            f << "$tdpBackup = Join-Path $pcDir ('pawnio.rollback-' + $tdpTransactionId)\n";
            f << "$tdpOriginalMoved = $false\n";
            f << "$tdpCommitted = $false\n";
            f << "$updateCommitted = $false\n";
            f << "$rollbackSucceeded = $false\n";
            f << "$newProcess = $null\n";
            f << "$handshakeToken = [guid]::NewGuid().ToString('N')\n";
            f << "$handshakePath = Join-Path (Split-Path -Parent $staging) ('update-handshake-' + $handshakeToken + '.json')\n";
            f << "$handshakeTimeoutSeconds = 90\n";
            f << "$parentExitTimeoutSeconds = 60\n";
            f << "function Copy-TreeChecked([string]$source, [string]$destination, [string[]]$extraArgs) {\n";
            f << "  & robocopy $source $destination /E /COPY:DAT /DCOPY:DAT /R:$copyRetries /W:$copyWaitSeconds /XJ @extraArgs /NFL /NDL /NJH /NJS /NP\n";
            f << "  if ($LASTEXITCODE -ge 8) { throw ('copy failed: ' + $source + ' -> ' + $destination + ' (robocopy=' + $LASTEXITCODE + ')') }\n";
            f << "}\n";
            f << "function Rename-DirectoryChecked([string]$source, [string]$destination, [string]$label) {\n";
            f << "  if ((Split-Path -Parent $source) -ne (Split-Path -Parent $destination)) { throw ($label + ' must stay in one parent directory') }\n";
            f << "  $destinationName = Split-Path -Leaf $destination\n";
            f << "  $lastError = ''\n";
            f << "  for ($attempt = 1; $attempt -le $copyRetries; $attempt++) {\n";
            f << "    try {\n";
            f << "      if (!(Test-Path -LiteralPath $source -PathType Container)) { throw ($label + ' source directory missing') }\n";
            f << "      if (Test-Path -LiteralPath $destination) { throw ($label + ' destination already exists') }\n";
            f << "      Rename-Item -LiteralPath $source -NewName $destinationName -ErrorAction Stop\n";
            f << "      if ((Test-Path -LiteralPath $source) -or !(Test-Path -LiteralPath $destination -PathType Container)) { throw ($label + ' rename verification failed') }\n";
            f << "      return\n";
            f << "    } catch {\n";
            f << "      $lastError = $_.Exception.Message\n";
            f << "      if ($attempt -lt $copyRetries) { Start-Sleep -Seconds $copyWaitSeconds }\n";
            f << "    }\n";
            f << "  }\n";
            f << "  throw ($label + ' failed after ' + $copyRetries + ' attempts: ' + $lastError)\n";
            f << "}\n";
            f << "function Assert-FileMatch([string]$source, [string]$destination, [string]$label) {\n";
            f << "  if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw ($label + ' source missing') }\n";
            f << "  if (!(Test-Path -LiteralPath $destination -PathType Leaf)) { throw ($label + ' destination missing') }\n";
            f << "  $s = Get-Item -LiteralPath $source; $d = Get-Item -LiteralPath $destination\n";
            f << "  if ($s.Length -ne $d.Length) { throw ($label + ' size mismatch') }\n";
            f << "  $sh = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash\n";
            f << "  $dh = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash\n";
            f << "  if ($sh -ne $dh) { throw ($label + ' SHA256 mismatch') }\n";
            f << "}\n";
            f << "function Assert-TreeMatch([string]$source, [string]$destination, [string]$label) {\n";
            f << "  if (!(Test-Path -LiteralPath $source -PathType Container)) { throw ($label + ' source directory missing') }\n";
            f << "  if (!(Test-Path -LiteralPath $destination -PathType Container)) { throw ($label + ' destination directory missing') }\n";
            f << "  $sourceFiles = @(Get-ChildItem -LiteralPath $source -Recurse -File | ForEach-Object { $_.FullName.Substring($source.Length).TrimStart('\\') })\n";
            f << "  $destinationFiles = @(Get-ChildItem -LiteralPath $destination -Recurse -File | ForEach-Object { $_.FullName.Substring($destination.Length).TrimStart('\\') })\n";
            f << "  if (@($sourceFiles | Where-Object { $_ -notin $destinationFiles }).Count -ne 0) { throw ($label + ' destination missing files') }\n";
            f << "  if (@($destinationFiles | Where-Object { $_ -notin $sourceFiles }).Count -ne 0) { throw ($label + ' destination has stale files') }\n";
            f << "  foreach ($relative in $sourceFiles) {\n";
            f << "    $item = Get-Item -LiteralPath (Join-Path $source $relative)\n";
            f << "    $relative = $item.FullName.Substring($source.Length).TrimStart('\\')\n";
            f << "    Assert-FileMatch $item.FullName (Join-Path $destination $relative) ($label + '\\' + $relative)\n";
            f << "  }\n";
            f << "}\n";
            f << "function Register-FileForRollback([string]$sourceFile, [string]$sourceRoot, [string]$targetRoot, [string]$bucket) {\n";
            f << "  $relative = $sourceFile.Substring($sourceRoot.Length).TrimStart('\\')\n";
            f << "  if ([string]::IsNullOrWhiteSpace($relative)) { throw 'rollback relative path is empty' }\n";
            f << "  $target = Join-Path $targetRoot $relative\n";
            f << "  if (Test-Path -LiteralPath $target -PathType Container) { throw ('update target is a directory but package entry is a file: ' + $target) }\n";
            f << "  if (Test-Path -LiteralPath $target -PathType Leaf) {\n";
            f << "    $backupFile = Join-Path (Join-Path $rollbackFiles $bucket) $relative\n";
            f << "    $backupParent = Split-Path -Parent $backupFile\n";
            f << "    if (!(Test-Path -LiteralPath $backupParent -PathType Container)) { New-Item -ItemType Directory -Path $backupParent -Force | Out-Null }\n";
            f << "    Copy-Item -LiteralPath $target -Destination $backupFile -Force -ErrorAction Stop\n";
            f << "    Assert-FileMatch $target $backupFile ('rollback backup ' + $bucket + '\\' + $relative)\n";
            f << "  } else {\n";
            f << "    $rollbackAddedFiles.Add($target) | Out-Null\n";
            f << "  }\n";
            f << "}\n";
            f << "function Register-TreeForRollback([string]$sourceRoot, [string]$targetRoot, [string]$bucket, [string[]]$excludedTopLevel) {\n";
            f << "  foreach ($item in Get-ChildItem -LiteralPath $sourceRoot -Recurse -File) {\n";
            f << "    $relative = $item.FullName.Substring($sourceRoot.Length).TrimStart('\\')\n";
            f << "    $topLevel = ($relative -split '\\\\', 2)[0]\n";
            f << "    if ($topLevel -in $excludedTopLevel) { continue }\n";
            f << "    Register-FileForRollback $item.FullName $sourceRoot $targetRoot $bucket\n";
            f << "  }\n";
            f << "}\n";
            f << "function Restore-OrdinaryFiles {\n";
            f << "  if (!$ordinaryRollbackPrepared) { return }\n";
            f << "  for ($index = $rollbackAddedFiles.Count - 1; $index -ge 0; $index--) {\n";
            f << "    $added = $rollbackAddedFiles[$index]\n";
            f << "    if (Test-Path -LiteralPath $added -PathType Leaf) { Remove-Item -LiteralPath $added -Force -ErrorAction Stop }\n";
            f << "    elseif (Test-Path -LiteralPath $added) { throw ('rollback expected an added file but found another item: ' + $added) }\n";
            f << "  }\n";
            f << "  $programBackup = Join-Path $rollbackFiles 'YeManCC'\n";
            f << "  if (Test-Path -LiteralPath $programBackup -PathType Container) { Copy-TreeChecked $programBackup $exeDir @() }\n";
            f << "  $powerControlBackup = Join-Path $rollbackFiles 'PowerControl'\n";
            f << "  if (Test-Path -LiteralPath $powerControlBackup -PathType Container) { Copy-TreeChecked $powerControlBackup $pcDir @() }\n";
            f << "  $supportBackup = Join-Path $rollbackFiles 'Support'\n";
            f << "  if (Test-Path -LiteralPath $supportBackup -PathType Leaf) { Copy-Item -LiteralPath $supportBackup -Destination $supportPath -Force -ErrorAction Stop }\n";
            f << "  foreach ($backupFile in Get-ChildItem -LiteralPath $rollbackFiles -Recurse -File) {\n";
            f << "    $relative = $backupFile.FullName.Substring($rollbackFiles.Length).TrimStart('\\')\n";
            f << "    if ($relative -eq 'Support') { $target = $supportPath }\n";
            f << "    elseif ($relative.StartsWith('YeManCC\\')) { $target = Join-Path $exeDir $relative.Substring('YeManCC\\'.Length) }\n";
            f << "    elseif ($relative.StartsWith('PowerControl\\')) { $target = Join-Path $pcDir $relative.Substring('PowerControl\\'.Length) }\n";
            f << "    else { throw ('unknown rollback bucket: ' + $relative) }\n";
            f << "    Assert-FileMatch $backupFile.FullName $target ('rollback restored ' + $relative)\n";
            f << "  }\n";
            f << "}\n";
            f << "try {\n";
            f << "  $parentExitDeadline = (Get-Date).AddSeconds($parentExitTimeoutSeconds)\n";
            f << "  while (Get-Process -Id $parentPid -ErrorAction SilentlyContinue) {\n";
            f << "    if ((Get-Date) -ge $parentExitDeadline) { throw ('parent YeManCC process did not exit within ' + $parentExitTimeoutSeconds + ' seconds') }\n";
            f << "    Start-Sleep -Milliseconds 100\n";
            f << "  }\n";
            f << "  if (!(Test-Path -LiteralPath $packageExe)) { throw 'staged executable missing' }\n";
            f << "  if (!(Test-Path -LiteralPath (Join-Path $tdpSource 'YeManTdpCtl.exe'))) { throw 'staged YeManTdpCtl.exe missing' }\n";
            f << "  if (!(Test-Path -LiteralPath (Join-Path $tdpSource '_internal') -PathType Container)) { throw 'staged YeManTdpCtl _internal missing' }\n";
            f << "  Set-UpdateProgress 'installing' '正在复制更新文件'\n";
            f << "  New-Item -ItemType Directory -Path $rollbackFiles -Force | Out-Null\n";
            f << "  Register-TreeForRollback $programSource $exeDir 'YeManCC' @('YeManCC.exe','YeMan-Support.html')\n";
            f << "  Register-FileForRollback $packageExe $programSource $exeDir 'YeManCC'\n";
            f << "  Register-TreeForRollback $powerControlSource $pcDir 'PowerControl' @('pawnio')\n";
            f << "  if (Test-Path -LiteralPath $supportPath -PathType Leaf) {\n";
            f << "    Copy-Item -LiteralPath $supportPath -Destination (Join-Path $rollbackFiles 'Support') -Force -ErrorAction Stop\n";
            f << "    Assert-FileMatch $supportPath (Join-Path $rollbackFiles 'Support') 'rollback backup support page'\n";
            f << "  } else {\n";
            f << "    $rollbackAddedFiles.Add($supportPath) | Out-Null\n";
            f << "  }\n";
            f << "  $ordinaryRollbackPrepared = $true\n";
            f << "  Set-Content -LiteralPath $state -Value '{\"phase\":\"copying\"}' -Encoding utf8\n";
            // Copy non-PawnIO PowerControl assets first. PawnIO is staged and
            // committed as one directory transaction below.
            f << "  if (!(Test-Path -LiteralPath $pcDir -PathType Container)) { New-Item -ItemType Directory -Path $pcDir -Force | Out-Null }\n";
            f << "  Copy-TreeChecked $powerControlSource $pcDir @('/XD', $tdpSource)\n";
            f << "  $sourcePython = @(Get-ChildItem -LiteralPath (Join-Path $tdpSource '_internal') -Filter 'python*.dll' -File | ForEach-Object Name)\n";
            f << "  if ($sourcePython.Count -eq 0) { throw 'TDP runtime has no Python DLL' }\n";
            f << "  Copy-TreeChecked $tdpSource $tdpStage @('/PURGE')\n";
            f << "  Assert-TreeMatch $tdpSource $tdpStage 'YeManTdpCtl.runtime.stage'\n";
            f << "  if (Test-Path -LiteralPath $tdpTarget -PathType Container) { Rename-DirectoryChecked $tdpTarget $tdpBackup 'PawnIO backup'; $tdpOriginalMoved = $true }\n";
            f << "  Rename-DirectoryChecked $tdpStage $tdpTarget 'PawnIO commit'\n";
            f << "  $tdpCommitted = $true\n";
            f << "  Assert-TreeMatch $tdpSource $tdpTarget 'YeManTdpCtl.runtime.committed'\n";
            f << "  Set-UpdateProgress 'installing' 'TDP 运行时校验通过'\n";
            f << "  Set-Content -LiteralPath $state -Value '{\"phase\":\"tdp-verified\"}' -Encoding utf8\n";
            f << "  if (Test-Path -LiteralPath $exePath -PathType Leaf) { Copy-Item -LiteralPath $exePath -Destination $backup -Force }\n";
            f << "  Copy-Item -LiteralPath $packageExe -Destination $newExe -Force\n";
            f << "  Move-Item -LiteralPath $newExe -Destination $exePath -Force\n";
            f << "  Copy-TreeChecked $programSource $exeDir @('/XF','YeManCC.exe','YeMan-Support.html')\n";
            f << "  Copy-Item -LiteralPath (Join-Path $programSource 'YeMan-Support.html') -Destination $supportPath -Force\n";
            f << "  Set-UpdateProgress 'installing' '正在启动新版本'\n";
            f << "  Set-Content -LiteralPath $state -Value '{\"phase\":\"launching\"}' -Encoding utf8\n";
            f << "  $newProcess = Start-Process -FilePath $exePath -ArgumentList @('--update-handshake', ('\"' + $handshakePath + '\"'), '--update-handshake-token', $handshakeToken) -PassThru\n";
            f << "  $handshakeDeadline = (Get-Date).AddSeconds($handshakeTimeoutSeconds)\n";
            f << "  $handshakeOk = $false\n";
            f << "  while ((Get-Date) -lt $handshakeDeadline) {\n";
            f << "    if ($newProcess.HasExited) { break }\n";
            f << "    if (Test-Path -LiteralPath $handshakePath -PathType Leaf) {\n";
            f << "      try {\n";
            f << "        $marker = Get-Content -LiteralPath $handshakePath -Raw -ErrorAction Stop | ConvertFrom-Json\n";
            f << "        $markerPid = [int]$marker.pid\n";
            f << "        $markerProcess = Get-Process -Id $markerPid -ErrorAction SilentlyContinue\n";
            f << "        if ($marker.phase -eq 'started' -and [string]$marker.token -eq $handshakeToken -and $markerPid -eq $newProcess.Id -and $markerProcess) { $handshakeOk = $true; break }\n";
            f << "      } catch { }\n";
            f << "    }\n";
            f << "    Start-Sleep -Milliseconds 250\n";
            f << "  }\n";
            f << "  if (!$handshakeOk) { throw 'new YeManCC process did not complete startup handshake' }\n";
            f << "  $updateCommitted = $true\n";
            f << "  Set-Content -LiteralPath $state -Value ('{\"phase\":\"started\",\"pid\":' + $newProcess.Id + '}') -Encoding utf8\n";
            f << "  Set-UpdateProgress 'completed' '更新已完成'\n";
            f << "} catch {\n";
            f << "  $installError = $_.Exception.Message\n";
            f << "  $rollbackError = $null\n";
            f << "  $rollbackAttempted = $ordinaryRollbackPrepared -or $tdpCommitted -or $tdpOriginalMoved -or (Test-Path -LiteralPath $backup)\n";
            f << "  if (!$updateCommitted -and $newProcess) { try { if (!$newProcess.HasExited) { Stop-Process -Id $newProcess.Id -Force -ErrorAction SilentlyContinue; $newProcess.WaitForExit(5000) } } catch { } }\n";
            f << "  if (!$updateCommitted -and ($tdpCommitted -or $tdpOriginalMoved)) {\n";
            f << "    try {\n";
            f << "      if (Test-Path -LiteralPath $tdpTarget) { Remove-Item -LiteralPath $tdpTarget -Recurse -Force -ErrorAction Stop }\n";
            f << "      if (Test-Path -LiteralPath $tdpBackup -PathType Container) { Rename-DirectoryChecked $tdpBackup $tdpTarget 'PawnIO rollback' }\n";
            f << "      elseif ($tdpOriginalMoved) { throw 'PawnIO rollback directory missing' }\n";
            f << "    } catch { $rollbackError = $_.Exception.Message }\n";
            f << "  }\n";
            f << "  if (!$updateCommitted) {\n";
            f << "    try { Restore-OrdinaryFiles } catch { if (!$rollbackError) { $rollbackError = $_.Exception.Message } else { $rollbackError += '; ordinary rollback: ' + $_.Exception.Message } }\n";
            f << "    try { if (Test-Path -LiteralPath $backup) { Copy-Item -LiteralPath $backup -Destination $exePath -Force -ErrorAction Stop } } catch { if (!$rollbackError) { $rollbackError = $_.Exception.Message } else { $rollbackError += '; EXE rollback: ' + $_.Exception.Message } }\n";
            f << "    if (!$rollbackError) { $rollbackSucceeded = $true }\n";
            f << "  }\n";
            f << "  $errorMessage = $installError\n";
            f << "  if ($rollbackError) { $errorMessage += '; rollback: ' + $rollbackError }\n";
            f << "  Set-UpdateProgress 'failed' ('更新失败：' + $errorMessage) $errorMessage\n";
            f << "  $failurePhase = if ($rollbackAttempted) { 'rolled-back' } else { 'failed' }\n";
            f << "  Set-Content -LiteralPath $state -Value ('{\"phase\":\"' + $failurePhase + '\",\"error\":' + (ConvertTo-Json $errorMessage -Compress) + '}') -Encoding utf8\n";
            f << "} finally {\n";
            f << "  Remove-Item -LiteralPath $newExe -Force -ErrorAction SilentlyContinue\n";
            f << "  Remove-Item -LiteralPath $tdpStage -Recurse -Force -ErrorAction SilentlyContinue\n";
            f << "  if ($updateCommitted) {\n";
            f << "    foreach ($cleanupPath in @($backup, $tdpBackup, $rollbackRoot)) {\n";
            f << "      try { if (Test-Path -LiteralPath $cleanupPath) { Remove-Item -LiteralPath $cleanupPath -Recurse -Force -ErrorAction Stop } } catch { }\n";
            f << "    }\n";
            f << "  } elseif ($rollbackSucceeded) {\n";
            f << "    Remove-Item -LiteralPath $backup -Force -ErrorAction SilentlyContinue\n";
            f << "    Remove-Item -LiteralPath $tdpBackup -Recurse -Force -ErrorAction SilentlyContinue\n";
            f << "    Remove-Item -LiteralPath $rollbackRoot -Recurse -Force -ErrorAction SilentlyContinue\n";
            f << "  }\n";
            f << "  Remove-Item -LiteralPath $handshakePath -Force -ErrorAction SilentlyContinue\n";
            f << "  Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue\n";
            f << "  Remove-Item -LiteralPath $scriptPath -Force -ErrorAction SilentlyContinue\n";
            f << "}\n";
        }
        std::wstring psArgs = L"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + script + L"\"";
        if ((INT_PTR)ShellExecuteW(nullptr, L"open", L"powershell.exe", psArgs.c_str(), nullptr, SW_HIDE) <= 32)
            throw std::runtime_error("Failed to launch update helper");
        // 更新退出也必须走窗口统一清理链，不能直接 PostQuitMessage 绕过 WM_DESTROY。
        if (g_hwnd && IsWindow(g_hwnd)) PostMessageW(g_hwnd, WM_APP_EXIT, 0, 0);
        else PostQuitMessage(0);
        return true;
    });

    // ── 更新加速器（steamcommunity_302 等）：手动切换、查看运行状态 ──
    ipc_on("updateAccel.get", [](const json&) -> json {
        bool exists = fspath::exists(g_updateAccelPath);
        bool running = exists && isUpdateAccelRunning();
        return {
            {"exists", exists},
            {"running", running}
        };
    });
    ipc_on("updateAccel.set", [](const json&) -> json {
        bool exists = fspath::exists(g_updateAccelPath);
        bool running = exists && isUpdateAccelRunning();
        bool ok = true;
        if (running) {
            ok = stopUpdateAccel();
        } else if (exists) {
            ok = startUpdateAccel();
        } else {
            ok = false;
        }
        running = exists && isUpdateAccelRunning();
        return {
            {"exists", exists},
            {"running", running},
            {"ok", ok}
        };
    });
}

// ================================================================
//  Commands: Multi-window
// ================================================================

static LRESULT CALLBACK ChildWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_SIZE) {
        for (auto& [id, cw] : g_children) {
            if (cw->hwnd == h && cw->ctrl) {
                RECT b; GetClientRect(h, &b);
                cw->ctrl->put_Bounds(b);
                break;
            }
        }
        return 0;
    }
    if (m == WM_CLOSE) {
        for (auto& [id, cw] : g_children) {
            if (cw->hwnd == h) {
                ipc_emit("window.childClosed", {{"id", id}});
                releaseChildWebView(cw);
                break;
            }
        }
        hideWindowAnimated(h);
        DestroyWindow(h);
        return 0;
    }
    if (m == WM_DESTROY) {
        for (auto it = g_children.begin(); it != g_children.end(); ++it) {
            if (it->second->hwnd == h) {
                releaseChildWebView(it->second);
                delete it->second;
                g_children.erase(it);
                break;
            }
        }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void reg_multiwindow() {
    static bool classRegistered = false;

    ipc_on("window.createChild", [](const json& a) -> json {
        auto title = U2W(a.value("title", std::string{""}));
        int w = a.value("width", 600), h = a.value("height", 400);
        auto url = a.value("url", std::string{});

        if (!classRegistered) {
            WNDCLASSEXW wc{sizeof(wc)};
            wc.lpfnWndProc = ChildWndProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"QQ_Child";
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = g_bgBrush ? g_bgBrush : (HBRUSH)(COLOR_WINDOW + 1);
            RegisterClassExW(&wc);
            classRegistered = true;
        }

        int id = g_nextChildId++;
        HWND child = CreateWindowExW(0, L"QQ_Child", title.c_str(),
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, w, h,
            g_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        enableWindowTransitions(child);
        showWindowAnimated(child, SW_SHOW);

        auto* cw = new ChildWindow{id, child, nullptr, nullptr};
        g_children[id] = cw;

        // Create WebView2 in child window (windowed mode for simplicity)
        g_env->CreateCoreWebView2Controller(child,
            Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [id, url](HRESULT hr, ICoreWebView2Controller* ctrl) -> HRESULT {
                auto it = g_children.find(id);
                if (FAILED(hr) || it == g_children.end()) return hr;
                auto* cw = it->second;
                cw->ctrl = ctrl;
                ctrl->get_CoreWebView2(&cw->view);
                RECT b; GetClientRect(cw->hwnd, &b);
                ctrl->put_Bounds(b);
                configureAppHost(cw->view.Get());

                // Navigate
                if (!url.empty()) {
                    cw->view->Navigate(U2W(url).c_str());
                } else {
                    cw->view->Navigate(L"https://app.localhost/index.html");
                }
                return S_OK;
            }).Get());

        return id;
    });

    ipc_on("window.closeChild", [](const json& a) -> json {
        int id = a.value("id", 0);
        auto it = g_children.find(id);
        if (it == g_children.end()) return false;
        PostMessageW(it->second->hwnd, WM_CLOSE, 0, 0);
        return true;
    });

    ipc_on("window.listChildren", [](const json&) -> json {
        json arr = json::array();
        for (auto& [id, cw] : g_children)
            arr.push_back(id);
        return arr;
    });
}

// ================================================================
//  Commands: System theme + Taskbar + Shell.run + Opacity
// ================================================================

static void reg_extras() {
    // System theme detection
    ipc_on("os.isDarkMode", [](const json&) -> json {
        return systemUsesDarkMode();
    });

    // 按键呼出：后台按住手柄 LB+RB 呼出程序（设置开关，持久化 summon.json）
    ipc_on("summon.get", [](const json&) -> json {
        return {
            {"enabled", g_summonEnabled},
            {"bDoubleMinimize", g_bDoubleMinimize},
            {"tdpShortcut", g_tdpShortcut},
            {"fpsShortcut", g_fpsShortcut},
            {"killGame", g_killGame},
            {"openKeyboard", g_openKeyboard},
            {"returnDesktop", g_returnDesktop},
            {"mouseToggle", g_mouseToggle},
            {"mouseBackend", g_mouseBackend}
        };
    });
    ipc_on("summon.set", [](const json& a) -> json {
        if (a.contains("enabled")) g_summonEnabled = a.value("enabled", g_summonEnabled);
        if (a.contains("bDoubleMinimize")) g_bDoubleMinimize = a.value("bDoubleMinimize", g_bDoubleMinimize);
        if (a.contains("tdpShortcut")) g_tdpShortcut = a.value("tdpShortcut", g_tdpShortcut);
        if (a.contains("fpsShortcut")) g_fpsShortcut = a.value("fpsShortcut", g_fpsShortcut);
        if (a.contains("killGame")) g_killGame = a.value("killGame", g_killGame);
        if (a.contains("openKeyboard")) g_openKeyboard = a.value("openKeyboard", g_openKeyboard);
        if (a.contains("returnDesktop")) g_returnDesktop = a.value("returnDesktop", g_returnDesktop);
        if (a.contains("mouseToggle")) g_mouseToggle = a.value("mouseToggle", g_mouseToggle);
        if (a.contains("mouseBackend")) {
            const std::string backend = a.value("mouseBackend", g_mouseBackend);
            if (backend == "joyxoff" || backend == "gamebar") g_mouseBackend = backend;
        }
        summonSave();
        return {
            {"enabled", g_summonEnabled},
            {"bDoubleMinimize", g_bDoubleMinimize},
            {"tdpShortcut", g_tdpShortcut},
            {"fpsShortcut", g_fpsShortcut},
            {"killGame", g_killGame},
            {"openKeyboard", g_openKeyboard},
            {"returnDesktop", g_returnDesktop},
            {"mouseToggle", g_mouseToggle},
            {"mouseBackend", g_mouseBackend}
        };
    });

    // 模拟鼠标方案与统一开关。只在页面进入/用户操作时读取，不新增后台轮询。
    ipc_on("mouseMode.get", [](const json&) -> json {
        return mouseModeGetState();
    });
    ipc_on("mouseMode.setBackend", [](const json& a) -> json {
        return mouseModeSetBackend(a.value("backend", std::string{}));
    });
    ipc_on("mouseMode.toggle", [](const json&) -> json {
        return mouseModeToggleState();
    });

    // ── 掌机前端自动关闭：后台线程每 5 秒轮询，命中目标进程名即温和发 WM_CLOSE ──
    ipc_on("autoclose.get", [](const json&) -> json {
        std::lock_guard<std::mutex> lk(g_autoCloseMx);
        json arr = json::array();
        for (auto& p : g_autoCloseProcs) arr.push_back(p);
        return { {"enabled", g_autoCloseEnabled.load()}, {"procs", arr} };
    });
    ipc_on("autoclose.set", [](const json& a) -> json {
        if (a.contains("enabled")) g_autoCloseEnabled = a.value("enabled", false);
        if (a.contains("procs") && a["procs"].is_array()) {
            std::lock_guard<std::mutex> lk(g_autoCloseMx);
            g_autoCloseProcs.clear();
            for (auto& p : a["procs"]) if (p.is_string()) g_autoCloseProcs.push_back(p.get<std::string>());
        }
        autoCloseSave();
        std::lock_guard<std::mutex> lk(g_autoCloseMx);
        json arr = json::array();
        for (auto& p : g_autoCloseProcs) arr.push_back(p);
        return { {"enabled", g_autoCloseEnabled.load()}, {"procs", arr} };
    });

    // ── 超线程 / SMT 开关（注册表 FeatureSettingsOverride 0x40 位，需重启生效）──
    // 实时检测用进程内 GetLogicalProcessorInformation（detectSmtLive）；
    // 写开关用 readSmtReg()/writeSmtReg() 直接操作 HKLM 注册表（仅翻转 0x40 位，
    // 保留其他 Spectre/SSB 缓解位；掩码同步置位 0x40 使该位真正生效）。两者均无需 lambda 捕获。
    ipc_on("smt.get", [](const json&) -> json {
        json r;
        SmtLive lv = detectSmtLive();
        int phys = lv.phys, logic = lv.logic;
        r["physicalCores"] = phys;
        r["logicalProcs"] = logic;
        r["liveOn"] = (logic > phys);
        // 下次启动态：真正的禁用 HT 主位是 override 的 0x2000(bit13)，且需 mask 低位 0x03 总控
        // 受理该位才生效。两者同时满足 ⇒ 配置为关闭超线程 ⇒ configOn=false；读取失败则维持 null。
        SmtReg reg = readSmtReg();
        if (reg.ok) {
            bool cfgOff = ((reg.override & 0x2000u) != 0) && ((reg.mask & 0x03u) != 0);
            r["configOn"] = (cfgOff ? false : true);
        }
        else r["configOn"] = json();
        return r;
    });
    ipc_on("smt.set", [](const json& a) -> json {
        bool on = a.value("on", false); // true=开启超线程, false=关闭超线程
        json r; r["ok"] = false; r["error"] = ""; r["info"] = "";
        SmtReg reg = readSmtReg();
        if (!reg.ok) {
            r["error"] = "无法访问注册表（请确认以管理员身份运行本程序）";
            return r;
        }
        DWORD override = reg.override;
        DWORD mask = reg.mask;
        // 兼容清理：删除旧的 bcdedit numproc 残留，避免与本机制叠加造成核心数混乱（best-effort）
        runCapture(L"bcdedit /deletevalue {current} numproc");
        if (on) {
            // 开启超线程 = 删除两个注册表值，完全恢复 Windows 默认（默认即每核 2 线程）。
            // 【关键教训】真正强制关闭 HT 的是 override 的 0x2000 位(bit13)，mask 低位 0x03 是
            // 总控(受理 override 的所有禁用位)。之前只清 0x40 会清掉无关位、把 0x2000 留下，
            // 导致重启后仍 16/16（实测踩坑）。删除恢复默认最确定、最干净，也与参考 bat 一致。
            if (!writeSmtReg(0, 0)) {
                r["error"] = "写入注册表失败（请确认以管理员身份运行本程序）";
                return r;
            }
            r["ok"] = true;
            r["info"] = "已恢复默认超线程（每个物理核心 2 线程），重启后生效";
            return r;
        } else {
            // 关闭超线程：置位 0x2000(真正的禁用 HT 主位)+0x40，mask 低位 0x03 作总控使其受理。
            // 对齐微软官方禁用超线程标准值(Override=0x2048, Mask=0x03)，增量置位以保留其他缓解位。
            override |= 0x2048u;   // 0x2000(disable HT) + 0x40 + 0x8
            mask    |= 0x0003u;    // 总控：受理 override 的禁用位
            if (!writeSmtReg(override, mask)) {
                r["error"] = "写入注册表失败（请确认以管理员身份运行本程序）";
                return r;
            }
            r["ok"] = true;
            r["info"] = "已预约关闭超线程，重启后生效（每个物理核心将只使用 1 个线程）";
            return r;
        }
    });

    // Window opacity。无边框透明模式即使 alpha=255 也必须保留 WS_EX_LAYERED，
    // 否则会破坏 WebView2 DefaultBackgroundColor alpha=0 的逐像素透明基线。
    ipc_on("window.setOpacity", [](const json& a) -> json {
        double opacity = std::clamp(a.value("opacity", 1.0), 0.0, 1.0);
        BYTE alpha = static_cast<BYTE>(opacity * 255.0);
        auto style = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
        if (g_frameless || alpha < 255) {
            SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, style | WS_EX_LAYERED);
            SetLayeredWindowAttributes(g_hwnd, 0, alpha, LWA_ALPHA);
        } else {
            SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, style & ~WS_EX_LAYERED);
        }
        return true;
    });

    // Taskbar progress
    ipc_on("window.setProgress", [](const json& a) -> json {
        double value = a.value("value", -1.0); // -1 = hide, 0..1 = progress
        ComPtr<ITaskbarList3> taskbar;
        if (FAILED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(&taskbar)))) return false;
        taskbar->HrInit();
        if (value < 0) {
            taskbar->SetProgressState(g_hwnd, TBPF_NOPROGRESS);
        } else {
            taskbar->SetProgressState(g_hwnd, TBPF_NORMAL);
            taskbar->SetProgressValue(g_hwnd, (ULONGLONG)(value * 1000), 1000);
        }
        return true;
    });

    // Shell.run with stdout/stderr capture
    // ── sys.info：系统静态信息一次直读（全部 Win32/注册表 API，毫秒级）。
    //    取代前端为拿 CPU 名/物理核数/内存/AC-DC 而各自冷启动一个
    //    powershell -NoProfile（每次 600~1900ms）的旧路径。──
    ipc_on("sys.info", [](const json&) -> json {
        json r;
        // CPU 名称：注册表 ProcessorNameString（与 WMI Win32_Processor.Name 同源）
        {
            HKEY hk;
            wchar_t buf[256]{}; DWORD sz = sizeof(buf), type = 0;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                    L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                    0, KEY_READ | KEY_WOW64_64KEY, &hk) == ERROR_SUCCESS) {
                if (RegQueryValueExW(hk, L"ProcessorNameString", nullptr, &type,
                        (LPBYTE)buf, &sz) == ERROR_SUCCESS && type == REG_SZ) {
                    std::wstring s = buf;
                    // 去首尾空白（该值常带前导空格）
                    size_t b = s.find_first_not_of(L" \t");
                    size_t e = s.find_last_not_of(L" \t");
                    r["cpuName"] = (b == std::wstring::npos) ? "" : W2U(s.substr(b, e - b + 1));
                }
                RegCloseKey(hk);
            }
            if (!r.contains("cpuName")) r["cpuName"] = "";
        }
        // 物理核/逻辑处理器：复用 SMT 检测（GetLogicalProcessorInformation）
        {
            SmtLive lv = detectSmtLive();
            r["physicalCores"] = lv.phys;
            r["logicalProcs"]  = lv.logic;
        }
        // 物理内存总量（字节）：GlobalMemoryStatusEx
        {
            MEMORYSTATUSEX ms{}; ms.dwLength = sizeof(ms);
            r["totalMemoryBytes"] = GlobalMemoryStatusEx(&ms)
                ? (double)ms.ullTotalPhys : 0.0;
        }
        // AC/DC：GetSystemPowerStatus（0=电池 DC，1=交流 AC，255=未知→按 AC）
        {
            SYSTEM_POWER_STATUS sps{};
            if (GetSystemPowerStatus(&sps)) {
                r["acLine"] = (int)sps.ACLineStatus;      // 0/1/255 原样给前端
                r["powerMode"] = (sps.ACLineStatus == 0) ? "dc" : "ac";
                r["hasBattery"] = sps.BatteryFlag != 128;
            } else {
                r["acLine"] = 255;
                r["powerMode"] = "ac";
                r["hasBattery"] = false;
            }
        }
        // 公共启动目录 + 用户目录（取代 [Environment]::GetFolderPath / $env:USERPROFILE）
        {
            PWSTR p = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_CommonStartup, 0, nullptr, &p))) {
                r["commonStartup"] = W2U(p); CoTaskMemFree(p);
            } else r["commonStartup"] = "";
            wchar_t up[MAX_PATH]{}; DWORD n = GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH);
            r["userProfile"] = (n > 0 && n < MAX_PATH) ? W2U(up) : "";
        }
        return r;
    });

    // ── power.activeScheme：PowerGetActiveScheme API 直读当前方案 GUID（取代
    //    powercfg /getactivescheme 子进程 + 中文文案解析）。──
    ipc_on("power.activeScheme", [](const json&) -> json {
        GUID* g = nullptr;
        if (PowerGetActiveScheme(nullptr, &g) != ERROR_SUCCESS || !g)
            return "";
        wchar_t buf[64];
        swprintf_s(buf, L"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            g->Data1, g->Data2, g->Data3,
            g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3],
            g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
        LocalFree(g);
        return W2U(buf);
    });

    ipc_on("power.lifecycle", [](const json&) -> json {
        const auto phase = g_powerLifecycle.load(std::memory_order_acquire);
        return json{
            {"phase", powerLifecycleName(phase)},
            {"generation", currentPowerGeneration()},
            {"hardwareWritesAllowed", hardwareWriteAllowed()},
            {"inputReady", g_inputReady.load(std::memory_order_acquire)},
            {"hibernateAvailable", sgHibernateAvailable()}
        };
    });

    ipc_on("power.hibernateState", [](const json&) -> json {
        return sgHibernateState();
    });

    ipc_on("power.resumeComplete", [](const json& a) -> json {
        const auto generation = a.value("generation", 0ULL);
        const bool daemonRequired = a.value("daemonRequired", false);
        const bool daemonReady = a.value("daemonReady", false);
        const auto current = currentPowerGeneration();
        const auto phase = g_powerLifecycle.load(std::memory_order_acquire);
        traceLog("power resumeComplete attempt generation=%llu current=%llu phase=%s daemonRequired=%d daemonReady=%d",
                 generation, current, powerLifecycleName(phase), daemonRequired ? 1 : 0, daemonReady ? 1 : 0);
        if (generation != current) {
            traceLog("power resumeComplete rejected reason=stale_generation generation=%llu current=%llu",
                     generation, current);
            return json{{"ok", false}, {"reason", "stale_generation"}, {"generation", current},
                        {"daemonRequired", daemonRequired}, {"daemonReady", daemonReady}};
        }
        if (phase == PowerLifecycle::Ready) {
            traceLog("power resumeComplete already_ready generation=%llu", current);
            return json{{"ok", true}, {"generation", current},
                        {"inputReady", g_inputReady.load(std::memory_order_acquire)},
                        {"daemonRequired", daemonRequired}, {"daemonReady", daemonReady}};
        }
        if (phase != PowerLifecycle::Resuming ||
            g_resumeReadyGeneration.load(std::memory_order_acquire) != current) {
            traceLog("power resumeComplete rejected reason=native_recovery_not_ready generation=%llu readyGeneration=%llu",
                     current, g_resumeReadyGeneration.load(std::memory_order_acquire));
            return json{{"ok", false}, {"reason", "native_recovery_not_ready"}, {"generation", current},
                        {"daemonRequired", daemonRequired}, {"daemonReady", daemonReady}};
        }
        if (!gamepadRecoverAfterResume()) {
            traceLog("power resumeComplete rejected reason=raw_input_registration_failed generation=%llu", current);
            return json{{"ok", false}, {"reason", "raw_input_registration_failed"}, {"generation", current},
                        {"daemonRequired", daemonRequired}, {"daemonReady", daemonReady}};
        }
        g_inputReady.store(true, std::memory_order_release);
        g_powerLifecycle.store(PowerLifecycle::Ready, std::memory_order_release);
        openHardwareWriteGate();
        traceLog("power resumeComplete committed generation=%llu daemonRequired=%d daemonReady=%d",
                 current, daemonRequired ? 1 : 0, daemonReady ? 1 : 0);
        PostMessageW(g_hwnd, WM_POWER_RESUME_COMMIT, 0, 0);
        return json{{"ok", true}, {"generation", current}, {"inputReady", true},
                    {"daemonRequired", daemonRequired}, {"daemonReady", daemonReady}};
    });

    // ── proc.running：Toolhelp32 进程枚举（取代 Get-Process 子进程）。
    //    args: { names: ["RTSS","steam"] }（不带 .exe，不区分大小写；
    //    支持 * 结尾前缀匹配，如 "RTSSHooksLoader*"）。返回 {"RTSS":true,...} ──
    ipc_on("proc.running", [](const json& a) -> json {
        std::vector<std::wstring> names;
        if (a.contains("names") && a["names"].is_array())
            for (auto& n : a["names"])
                if (n.is_string()) names.push_back(U2W(n.get<std::string>()));
        json out = json::object();
        for (auto& n : names) out[W2U(n)] = false;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return out;
        PROCESSENTRY32W pe{ sizeof(pe) };
        if (Process32FirstW(snap, &pe)) {
            do {
                std::wstring exe = pe.szExeFile;
                // 去 .exe 后缀
                if (exe.size() > 4 && _wcsicmp(exe.c_str() + exe.size() - 4, L".exe") == 0)
                    exe.resize(exe.size() - 4);
                for (auto& n : names) {
                    if (n.empty()) continue;
                    bool hit;
                    if (n.back() == L'*') {
                        std::wstring pre = n.substr(0, n.size() - 1);
                        hit = exe.size() >= pre.size() &&
                              _wcsnicmp(exe.c_str(), pre.c_str(), pre.size()) == 0;
                    } else {
                        hit = _wcsicmp(exe.c_str(), n.c_str()) == 0;
                    }
                    if (hit) out[W2U(n)] = true;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        return out;
    });

    // ── cpu.topology：检测 L3 缓存域（CCD）拓扑，决定 CPU 核心控制卡片是否显示。
    //    返回 { logical, l3Domains, ccdMasks:["0x...", ...] } ──
    ipc_on("cpu.topology", [](const json&) -> json {
        CcdTopology t = detectCcdTopology();
        json r;
        r["logical"]    = t.logical;
        r["physicalCores"] = t.physicalCores;
        r["l3Domains"]  = t.l3Domains;
        r["ccdMasks"]   = t.ccdMasks;
        return r;
    });

    // ── cpu.architecture：只读检测 Windows 是否报告混合效率等级。──
    //    该探针不读取或写入电源方案；前端只有 heterogeneous=true 时显示大小核心编辑。
    ipc_on("cpu.architecture", [](const json&) -> json {
        const CoreArchitecture a = detectCoreArchitecture();
        json classes = json::array();
        for (int value : a.classes) classes.push_back(value);
        return {
            {"detected", a.detected},
            {"heterogeneous", a.heterogeneous},
            {"efficiencyClasses", classes},
            {"source", a.source},
            {"logical", a.logical},
            {"physical", a.physical},
        };
    });

    // ── cpu.setCcdMode：全局进程亲和性，0=全核，1..N=仅第 N-1 个 CCD。
    //    首次调用启动后台轮询线程，新启动的进程会被自动限制到目标 CCD。──
    ipc_on("cpu.setCcdMode", [](const json& a) -> json {
        if (!hardwareWriteAllowed())
            throw std::runtime_error("hardware writes are blocked during power transition");
        int mode = a.value("mode", 0);
        if (mode < 0) throw std::runtime_error("mode must be >= 0");
        CcdTopology t = detectCcdTopology();
        if (t.ccdMasks.size() < 2)
            throw std::runtime_error("this CPU has fewer than 2 L3 domains");
        if (mode > (int)t.ccdMasks.size())
            throw std::runtime_error("CCD mode exceeds detected L3 domains");
        {
            std::lock_guard<std::mutex> lk(g_ccdMutex);
            g_ccdMasks.clear();
            for (auto& s : t.ccdMasks) {
                ULONG_PTR m = 0;
                try {
                    m = std::stoull(s, nullptr, 16);
                } catch (...) {}
                if (m) g_ccdMasks.push_back(m);
            }
        }
        ccdStartWorker();
        g_ccdMode.store(mode);
        json r;
        r["mode"] = mode;
        r["applied"] = true;
        return r;
    });

    ipc_on("shell.run", [](const json& a) -> json {
        auto program = a.value("program", std::string{});
        if (program.empty()) throw std::runtime_error("program is required");
        const auto programName = ascii_lower(W2U(fspath::path(U2W(program)).filename().wstring()));
        bool tdpHardwareWrite = false;
        std::string firstArg;
        if (a.contains("args") && a["args"].is_array() && !a["args"].empty() && a["args"][0].is_string()) {
            firstArg = ascii_lower(a["args"][0].get<std::string>());
            tdpHardwareWrite = firstArg == "set" || firstArg == "set-amd" || firstArg == "set-intel" ||
                               firstArg == "pbo" || firstArg == "uv" || firstArg == "restore";
        }
        const bool trustedTdpCtl = sameFinalPath(U2W(program), kTdpDaemonExe);

        // During suspend/resume, keep read-only powercfg queries available but
        // reject every command family that changes a scheme, hibernation, or
        // wake policy.  Frontend CPU scheduling uses these direct argv forms.
        auto powercfgVerb = firstArg;
        while (!powercfgVerb.empty() && (powercfgVerb.front() == '/' || powercfgVerb.front() == '-'))
            powercfgVerb.erase(powercfgVerb.begin());
        static const std::unordered_set<std::string> powercfgWriteVerbs = {
            "s", "x", "setactive", "overlaysetactive", "setacvalueindex", "setdcvalueindex",
            "change", "changename", "duplicatescheme", "delete", "import", "restoredefaultschemes",
            "setsecuritydescriptor", "hibernate", "h", "requestsoverride",
            "deviceenablewake", "devicedisablewake"
        };
        const bool powercfgHardwareWrite =
            (programName == "powercfg" || programName == "powercfg.exe") &&
            powercfgWriteVerbs.count(powercfgVerb) != 0;

        // CPU profile VBS files ultimately execute multiple powercfg writes.
        // Gate only the application's PowerControl\TDP scripts; unrelated VBS
        // helpers (Steam, JoyXoff, etc.) remain unaffected.
        bool tdpProfileScriptWrite = false;
        if ((programName == "cscript" || programName == "cscript.exe" ||
             programName == "wscript" || programName == "wscript.exe") &&
            a.contains("args") && a["args"].is_array()) {
            for (const auto& arg : a["args"]) {
                if (!arg.is_string()) continue;
                auto script = ascii_lower(arg.get<std::string>());
                std::replace(script.begin(), script.end(), '/', '\\');
                if (script.ends_with(".vbs") && script.find("\\powercontrol\\tdp\\") != std::string::npos) {
                    tdpProfileScriptWrite = true;
                    break;
                }
            }
        }

        const bool gatedHardwareWrite =
            (trustedTdpCtl && tdpHardwareWrite) || powercfgHardwareWrite || tdpProfileScriptWrite;
        if (gatedHardwareWrite && !hardwareWriteAllowed()) {
            traceLog("power gate blocked shell.run program=%s generation=%llu",
                     programName.c_str(), currentPowerGeneration());
            throw std::runtime_error("hardware writes are blocked during power transition");
        }
        int timeoutRaw = a.value("timeoutMs", 30000);
        DWORD timeoutMs = static_cast<DWORD>((std::max)(100, (std::min)(600000, timeoutRaw)));
        constexpr size_t MAX_CAPTURE_BYTES = 8u << 20;

        std::wstring cmdLine = quote_windows_arg(U2W(program));
        if (a.contains("args") && a["args"].is_array()) {
            for (auto& arg : a["args"]) {
                if (!arg.is_string()) throw std::runtime_error("shell.run args must be strings");
                cmdLine += L" ";
                cmdLine += quote_windows_arg(U2W(arg.get<std::string>()));
            }
        }

        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE hOutR = nullptr, hOutW = nullptr, hErrR = nullptr, hErrW = nullptr;
        if (!CreatePipe(&hOutR, &hOutW, &sa, 0) || !CreatePipe(&hErrR, &hErrW, &sa, 0)) {
            if (hOutR) CloseHandle(hOutR); if (hOutW) CloseHandle(hOutW);
            if (hErrR) CloseHandle(hErrR); if (hErrW) CloseHandle(hErrW);
            throw std::runtime_error("Failed to create process pipes");
        }
        SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(hErrR, HANDLE_FLAG_INHERIT, 0);

        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (!job) {
            CloseHandle(hOutR); CloseHandle(hOutW);
            CloseHandle(hErrR); CloseHandle(hErrW);
            throw std::runtime_error("Failed to create process job");
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
            CloseHandle(job);
            CloseHandle(hOutR); CloseHandle(hOutW);
            CloseHandle(hErrR); CloseHandle(hErrW);
            throw std::runtime_error("Failed to configure process job");
        }

        STARTUPINFOW si{sizeof(si)};
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hOutW;
        si.hStdError = hErrW;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION pi{};

        std::vector<wchar_t> cmd(cmdLine.begin(), cmdLine.end());
        cmd.push_back(0);
        const DWORD createFlags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                            createFlags, nullptr, nullptr, &si, &pi)) {
            CloseHandle(hOutR); CloseHandle(hOutW);
            CloseHandle(hErrR); CloseHandle(hErrW);
            CloseHandle(job);
            throw std::runtime_error("Failed to start process");
        }
        CloseHandle(hOutW);
        CloseHandle(hErrW);

        if (!AssignProcessToJobObject(job, pi.hProcess)) {
            TerminateProcess(pi.hProcess, ERROR_ACCESS_DENIED);
            WaitForSingleObject(pi.hProcess, 1000);
            CloseHandle(hOutR); CloseHandle(hErrR);
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(job);
            throw std::runtime_error("Failed to assign process job");
        }
        if (gatedHardwareWrite && !hardwareWriteAllowed()) {
            TerminateProcess(pi.hProcess, ERROR_OPERATION_ABORTED);
            WaitForSingleObject(pi.hProcess, 1000);
            CloseHandle(hOutR); CloseHandle(hErrR);
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(job);
            throw std::runtime_error("hardware writes were blocked before process dispatch");
        }
        ResumeThread(pi.hThread);

        struct PipeCapture {
            HANDLE h = nullptr;
            std::string data;
            std::atomic<bool> overflow{false};
            size_t maxBytes = 0;
        };
        auto readPipe = [](PipeCapture* capture) {
            char buf[4096];
            DWORD rd = 0;
            while (ReadFile(capture->h, buf, sizeof(buf), &rd, nullptr) && rd > 0) {
                const size_t available = capture->data.size() < capture->maxBytes
                    ? capture->maxBytes - capture->data.size() : 0;
                if (available > 0) capture->data.append(buf, (std::min)(available, static_cast<size_t>(rd)));
                if (static_cast<size_t>(rd) > available) capture->overflow.store(true);
            }
            CloseHandle(capture->h);
            capture->h = nullptr;
        };

        PipeCapture outCapture{hOutR, {}, false, MAX_CAPTURE_BYTES};
        PipeCapture errCapture{hErrR, {}, false, MAX_CAPTURE_BYTES};
        std::thread outReader;
        std::thread errReader;
        try {
            outReader = std::thread(readPipe, &outCapture);
            errReader = std::thread(readPipe, &errCapture);
        } catch (...) {
            CloseHandle(job);
            if (outReader.joinable()) outReader.join(); else if (outCapture.h) CloseHandle(outCapture.h);
            if (errReader.joinable()) errReader.join(); else if (errCapture.h) CloseHandle(errCapture.h);
            WaitForSingleObject(pi.hProcess, 1000);
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
            throw std::runtime_error("Failed to start pipe readers");
        }

        bool timedOut = false;
        bool cancelled = false;
        bool outputOverflow = false;
        const ULONGLONG deadline = GetTickCount64() + timeoutMs;
        for (;;) {
            if (g_poolCancel.load(std::memory_order_acquire)) {
                cancelled = true;
                break;
            }
            DWORD waitMs = 50;
            ULONGLONG now = GetTickCount64();
            if (now >= deadline) {
                timedOut = true;
                break;
            }
            waitMs = static_cast<DWORD>((std::min)(static_cast<ULONGLONG>(waitMs), deadline - now));
            DWORD wr = WaitForSingleObject(pi.hProcess, waitMs);
            if (wr == WAIT_OBJECT_0) break;
            if (wr == WAIT_FAILED) {
                timedOut = true;
                break;
            }
            if (outCapture.overflow.load() || errCapture.overflow.load()) {
                outputOverflow = true;
                break;
            }
        }

        if (timedOut || cancelled || outputOverflow) {
            TerminateJobObject(job, timedOut ? ERROR_TIMEOUT :
                (cancelled ? ERROR_CANCELLED : ERROR_BUFFER_OVERFLOW));
        } else {
            // shell.run 只允许同步命令；根进程退出后关闭 Job，回收仍继承管道的后代，保证 reader 得到 EOF。
            TerminateJobObject(job, ERROR_SUCCESS);
        }
        WaitForSingleObject(pi.hProcess, 1000);
        CloseHandle(job);

        if (outReader.joinable()) outReader.join();
        if (errReader.joinable()) errReader.join();

        DWORD exitCode = 0;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (timedOut) throw std::runtime_error("Process timed out");
        if (cancelled) throw std::runtime_error("Process cancelled during shutdown");
        if (outputOverflow) throw std::runtime_error("Process output exceeded 8 MiB limit");
        return json{{"exitCode", static_cast<int>(exitCode)},
                    {"stdout", oemToUtf8(outCapture.data)},
                    {"stderr", oemToUtf8(errCapture.data)}};
    });
}

// ================================================================
//  Splash screen
// ================================================================

static void releaseSplashSurface() {
    if (g_splashDc) {
        if (g_splashBitmap) SelectObject(g_splashDc, g_splashBitmap);
        DeleteDC(g_splashDc);
    }
    if (g_splashBitmap) DeleteObject(g_splashBitmap);
    g_splashDc = nullptr;
    g_splashBitmap = nullptr;
    g_splashBits = nullptr;
    g_splashSurfaceW = 0;
    g_splashSurfaceH = 0;
}

static bool ensureSplashSurface(int width, int height) {
    if (g_splashDc && g_splashBitmap &&
        g_splashSurfaceW == width && g_splashSurfaceH == height) return true;
    releaseSplashSurface();

    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    g_splashDc = CreateCompatibleDC(screen);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    g_splashBitmap = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &g_splashBits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!g_splashDc || !g_splashBitmap || !g_splashBits) {
        releaseSplashSurface();
        return false;
    }
    SelectObject(g_splashDc, g_splashBitmap);
    g_splashSurfaceW = width;
    g_splashSurfaceH = height;
    return true;
}

static void renderSplash() {
    if (!g_splash || !g_splashGdiplusToken) return;
    RECT client{};
    if (!GetClientRect(g_splash, &client)) return;
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0 || !ensureSplashSurface(width, height)) return;

    using namespace Gdiplus;
    const int stride = width * 4;
    Bitmap bitmap(width, height, stride, PixelFormat32bppPARGB,
                  static_cast<BYTE*>(g_splashBits));
    Graphics graphics(&bitmap);
    graphics.SetCompositingMode(CompositingModeSourceCopy);
    graphics.Clear(Color(0, 0, 0, 0));
    graphics.SetCompositingMode(CompositingModeSourceOver);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    graphics.SetCompositingQuality(CompositingQualityHighQuality);

    const float scale = static_cast<float>(GetDpiForWindow(g_splash)) / 96.0f;
    const float radius = 24.0f * scale;
    const float stroke = 4.5f * scale;
    const float cx = static_cast<float>(width) * 0.5f;
    const float cy = static_cast<float>(height) * 0.5f;
    RectF ring(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
    Pen pen(Color(246, 248, 250, 250), stroke);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);
    const float angle = static_cast<float>(g_splashAngle) - 90.0f;
    const float arcWave = static_cast<float>(0.5 + 0.5 * std::sin(g_splashPhase * 0.82 + 0.7));
    const float sweep = 178.0f + 108.0f * arcWave;
    const BYTE alpha = static_cast<BYTE>(218.0f + 34.0f *
        static_cast<float>(0.5 + 0.5 * std::sin(g_splashPhase * 0.82 + 2.1)));
    Pen animatedPen(Color(alpha, 246, 248, 250), stroke);
    animatedPen.SetStartCap(LineCapRound);
    animatedPen.SetEndCap(LineCapRound);
    animatedPen.SetLineJoin(LineJoinRound);
    graphics.DrawArc(&animatedPen, ring, angle, sweep);

    POINT position{};
    RECT windowRect{};
    if (!GetWindowRect(g_splash, &windowRect)) return;
    position.x = windowRect.left;
    position.y = windowRect.top;
    SIZE size{width, height};
    POINT source{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    HDC screen = GetDC(nullptr);
    if (screen) {
        UpdateLayeredWindow(g_splash, screen, &position, &size, g_splashDc,
                            &source, 0, &blend, ULW_ALPHA);
        ReleaseDC(nullptr, screen);
    }
}

static LRESULT CALLBACK SplashProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        g_splashAngle = 0.0;
        g_splashPhase = 0.0;
        QueryPerformanceFrequency(&g_splashQpcFrequency);
        QueryPerformanceCounter(&g_splashLastQpc);
        SetTimer(h, SPLASH_ANIMATION_TIMER_ID, SPLASH_TIMER_INTERVAL_MS, nullptr);
        return 0;
    case WM_TIMER:
        if (w == SPLASH_ANIMATION_TIMER_ID) {
            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            double elapsedSeconds = SPLASH_TIMER_INTERVAL_MS / 1000.0;
            if (g_splashLastQpc.QuadPart > 0 && g_splashQpcFrequency.QuadPart > 0 &&
                now.QuadPart >= g_splashLastQpc.QuadPart) {
                elapsedSeconds = static_cast<double>(now.QuadPart - g_splashLastQpc.QuadPart) /
                    static_cast<double>(g_splashQpcFrequency.QuadPart);
            }
            elapsedSeconds = (std::max)(0.001, (std::min)(elapsedSeconds, 0.1));
            g_splashLastQpc = now;
            g_splashPhase += elapsedSeconds * 2.0 * 3.14159265358979323846 * 0.62;
            const double speedFactor = 0.58 + 0.82 *
                (0.5 + 0.5 * std::sin(g_splashPhase));
            g_splashAngle = std::fmod(
                g_splashAngle + SPLASH_ROTATION_DEGREES_PER_SECOND * speedFactor * elapsedSeconds,
                360.0);
            renderSplash();
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        ValidateRect(h, nullptr);
        return 0;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_DESTROY:
        KillTimer(h, SPLASH_ANIMATION_TIMER_ID);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

    // ── 自绘托盘右键菜单（深色圆角，程序风格；替代系统 TrackPopupMenu）──
    static void closeTrayMenu();
    static void execMenuCmd(int cmd);

    static LRESULT CALLBACK TrayMenuProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        switch (m) {
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(h, &ps);
            RECT rc; GetClientRect(h, &rc);
            int r = MENU_RADIUS;
            // 背景圆角
            HBRUSH bBg = CreateSolidBrush(MENU_BG);
            HPEN pen = CreatePen(PS_NULL, 0, 0);
            SelectObject(hdc, pen); SelectObject(hdc, bBg);
            RoundRect(hdc, 0, 0, rc.right, rc.bottom, 2*r, 2*r);
            DeleteObject(pen); DeleteObject(bBg);

            HFONT hFont = CreateFontW(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI"); // +30% 字号
            auto old = SelectObject(hdc, hFont);
            SetBkMode(hdc, TRANSPARENT);
            int i = 0;
            for (auto& item : g_menuItems) {
                if (item.cmd == ID_TRAY_SEP) {
                    RECT sr = g_menuItemRects[i];
                    HPEN sp = CreatePen(PS_SOLID, 1, MENU_SEP);
                    SelectObject(hdc, sp);
                    MoveToEx(hdc, sr.left + 16, (sr.top + sr.bottom) / 2, nullptr);
                    LineTo(hdc, sr.right - 16, (sr.top + sr.bottom) / 2);
                    DeleteObject(sp);
                } else {
                    RECT ir = g_menuItemRects[i];
                    if (i == g_menuHover) {
                        HBRUSH bH = CreateSolidBrush(MENU_HOVER);
                        HPEN ph = CreatePen(PS_NULL, 0, 0);
                        SelectObject(hdc, ph); SelectObject(hdc, bH);
                        RoundRect(hdc, ir.left + 5, ir.top + 2, ir.right - 5, ir.bottom - 2, 7, 7);
                        DeleteObject(ph); DeleteObject(bH);
                    }
                    SetTextColor(hdc, item.danger ? MENU_DANGER : MENU_TEXT);
                    RECT tr = ir; tr.left += 16;
                    DrawTextW(hdc, item.label.c_str(), -1, &tr,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                }
                i++;
            }
            SelectObject(hdc, old);
            DeleteObject(hFont);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            int hit = -1;
            for (size_t i = 0; i < g_menuItemRects.size(); i++) {
                if (PtInRect(&g_menuItemRects[i], p) && g_menuItems[i].cmd != ID_TRAY_SEP) {
                    hit = (int)i; break;
                }
            }
            if (hit != g_menuHover) { g_menuHover = hit; InvalidateRect(h, nullptr, FALSE); }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            POINT p{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            RECT cr; GetClientRect(h, &cr);
            if (!PtInRect(&cr, p)) { closeTrayMenu(); return 0; } // 点窗口外：关闭
            for (size_t i = 0; i < g_menuItemRects.size(); i++) {
                if (PtInRect(&g_menuItemRects[i], p) && g_menuItems[i].cmd != ID_TRAY_SEP) {
                    execMenuCmd(g_menuItems[i].cmd);
                    return 0;
                }
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (w == VK_ESCAPE) { closeTrayMenu(); return 0; }
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(w) == WA_INACTIVE) closeTrayMenu();
            return 0;
        case WM_KILLFOCUS:
            closeTrayMenu();
            return 0;
        case WM_DESTROY:
            g_menuHwnd = nullptr;
            return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }

    static void closeTrayMenu() {
        if (g_menuHwnd) {
            ReleaseCapture();
            DestroyWindow(g_menuHwnd);
            g_menuHwnd = nullptr;
        }
    }

    static void execMenuCmd(int cmd) {
        closeTrayMenu();
        if (cmd == ID_TRAY_SHOW) {
            if (!IsWindowVisible(g_hwnd)) {
                showWindowAnimated(g_hwnd, SW_SHOW);
            } else hideWindowAnimated(g_hwnd);
        } else if (cmd == ID_TRAY_MIN) {
            hideWindowAnimated(g_hwnd);
        } else if (cmd == ID_TRAY_EXIT) {
            PostMessageW(g_hwnd, WM_APP_EXIT, 0, 0);
        }
    }

    static void showTrayMenu() {
        if (g_menuHwnd) closeTrayMenu();
        bool hidden = !IsWindowVisible(g_hwnd);
        g_menuItems.clear();
        g_menuItems.push_back({ hidden ? L"显示窗口" : L"隐藏窗口", ID_TRAY_SHOW });
        g_menuItems.push_back({ L"隐藏到托盘", ID_TRAY_MIN });
        g_menuItems.push_back({ L"", ID_TRAY_SEP });
        g_menuItems.push_back({ L"退出", ID_TRAY_EXIT, true });

        int scale = GetDpiForSystem() / 96;
        int itemH = 39 * scale; // 34 * 1.15 ≈ 39（气泡 +15%）
        int padY  = 7 * scale;  // 6 * 1.15 ≈ 7
        int w = 196 * scale;    // 170 * 1.15 ≈ 196
        int seps = 0;
        for (auto& it : g_menuItems) if (it.cmd == ID_TRAY_SEP) seps++;
        int itemCount = (int)g_menuItems.size() - seps;
        int h = padY * 2 + itemH * itemCount + 9 * scale * seps; // 分隔 8 * 1.15 ≈ 9

        g_menuItemRects.assign(g_menuItems.size(), RECT{});
        int y = padY;
        for (size_t i = 0; i < g_menuItems.size(); i++) {
            if (g_menuItems[i].cmd == ID_TRAY_SEP) {
                g_menuItemRects[i] = { 0, y, w, y + 9 * scale };
                y += 9 * scale;
            } else {
                g_menuItemRects[i] = { 0, y, w, y + itemH };
                y += itemH;
            }
        }
        g_menuW = w; g_menuH = h;

        // 定位到托盘图标矩形（根治 GetMessagePos 陈旧坐标导致菜单位置飘的问题）
        RECT ri{};
        NOTIFYICONIDENTIFIER ni{sizeof(ni)};
        ni.hWnd = g_hwnd;
        ni.uID  = g_nid.uID;
        BOOL gotRect = (Shell_NotifyIconGetRect(&ni, &ri) == S_OK);
        if (!gotRect) { POINT p; GetCursorPos(&p); ri = { p.x, p.y, p.x + 1, p.y + 1 }; }
        HMONITOR mon = MonitorFromRect(&ri, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(mon, &mi);
        int x = ri.right - w;            // 右对齐图标右缘
        int yy = ri.top - h;             // 默认向上展开
        if (yy < mi.rcWork.top) yy = ri.bottom; // 顶部任务栏则向下展开
        if (x < mi.rcWork.left) x = mi.rcWork.left;
        if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;

        if (!g_menuClassReg) {
            WNDCLASSEXW mc{sizeof(mc)};
            mc.lpfnWndProc   = TrayMenuProc;
            mc.hInstance     = g_hinst;
            mc.lpszClassName = L"QQ_TrayMenu";
            mc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
            mc.hbrBackground = CreateSolidBrush(MENU_BG);
            RegisterClassExW(&mc);
            g_menuClassReg = true;
        }
        if (!g_menuHwnd) {
            g_menuHwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                L"QQ_TrayMenu", L"", WS_POPUP, x, yy, w, h, nullptr, nullptr, g_hinst, nullptr);
            DWORD round = 2; // DWMWCP_ROUND
            DwmSetWindowAttribute(g_menuHwnd, 33, &round, sizeof(round));
            MARGINS m = {0,0,0,1};
            DwmExtendFrameIntoClientArea(g_menuHwnd, &m);
        } else {
            SetWindowPos(g_menuHwnd, nullptr, x, yy, w, h, SWP_NOZORDER);
        }
        g_menuHover = -1;
        ShowWindow(g_menuHwnd, SW_SHOW);
        SetWindowPos(g_menuHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        UpdateWindow(g_menuHwnd);
        SetCapture(g_menuHwnd);
    }

static void showSplash(HINSTANCE hi, int w, int h) {
    if (!g_cfg.value("/window/splash"_json_pointer, false)) return;
    WNDCLASSEXW sc{sizeof(sc)};
    sc.lpfnWndProc  = SplashProc;
    sc.hInstance     = hi;
    sc.lpszClassName = L"QQ_Splash";
    sc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&sc);

    HMONITOR mon = MonitorFromPoint({0,0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    const UINT dpi = GetDpiForSystem();
    int sw = MulDiv(120, static_cast<int>(dpi), 96);
    int sh = MulDiv(120, static_cast<int>(dpi), 96);
    int sx = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - sw) / 2;
    int sy = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - sh) / 2;

    g_splash = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        L"QQ_Splash", L"",
        WS_POPUP, sx, sy, sw, sh, nullptr, nullptr, hi, nullptr);
    if (!g_splash) return;
    ShowWindow(g_splash, SW_SHOWNOACTIVATE);
    renderSplash();
}

static void closeSplash() {
    if (g_splash) {
        KillTimer(g_splash, SPLASH_ANIMATION_TIMER_ID);
        DestroyWindow(g_splash);
        g_splash = nullptr;
    }
    releaseSplashSurface();
}

static void shutdownSplashGraphics() {
    closeSplash();
    if (g_splashGdiplusToken) {
        Gdiplus::GdiplusShutdown(g_splashGdiplusToken);
        g_splashGdiplusToken = 0;
    }
}

// ================================================================
//  WebView2 initialization
// ================================================================

static bool configureUserAssetsHost(ICoreWebView2* view) {
    if (!view) return false;
    ComPtr<ICoreWebView2_3> userAssetsView;
    if (FAILED(view->QueryInterface(IID_PPV_ARGS(&userAssetsView)))) return false;
    auto userAssets = background_assets_dir();
    return SUCCEEDED(userAssetsView->SetVirtualHostNameToFolderMapping(
        L"user-assets.localhost", userAssets.c_str(),
        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW));
}

static bool configureAppHost(ICoreWebView2* view) {
    if (!view) return false;
    // 自定义背景是可选功能；映射失败不得阻断 app.localhost 主界面启动。
    configureUserAssetsHost(view);
    // 音乐目录映射（启动期恢复已配置目录，无需刷新）
    configureMusicHost(view);

#ifdef SINGLE_EXE
    if (!g_pakEntries.empty()) {
        if (!findPakEntry("index.html"))
            return false;

        view->AddWebResourceRequestedFilter(
            L"http://app.localhost/*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        view->AddWebResourceRequestedFilter(
            L"https://app.localhost/*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        view->AddWebResourceRequestedFilter(
            L"http://app.local/*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        view->AddWebResourceRequestedFilter(
            L"https://app.local/*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        view->add_WebResourceRequested(
            Callback<ICoreWebView2WebResourceRequestedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                ComPtr<ICoreWebView2WebResourceRequest> request;
                args->get_Request(&request);
                LPWSTR uri;
                request->get_Uri(&uri);
                std::wstring wUri(uri);
                CoTaskMemFree(uri);

                std::string path;
                auto scheme = wUri.find(L"://");
                auto firstSlash = scheme == std::wstring::npos
                    ? std::wstring::npos
                    : wUri.find(L'/', scheme + 3);
                if (firstSlash != std::wstring::npos)
                    path = W2U(wUri.substr(firstSlash + 1));
                auto qpos = path.find('?');
                if (qpos != std::string::npos) path = path.substr(0, qpos);
                auto hpos = path.find('#');
                if (hpos != std::string::npos) path = path.substr(0, hpos);
                path = url_decode_path(path);
                if (path.empty()) path = "index.html";

                auto* entry = findPakEntry(path);
                if (!entry) {
                    static const char missing[] = "Embedded asset not found";
                    ComPtr<IStream> stream;
                    stream.Attach(SHCreateMemStream(
                        reinterpret_cast<const BYTE*>(missing), sizeof(missing) - 1));
                    if (!stream) return S_OK;

                    ComPtr<ICoreWebView2WebResourceResponse> response;
                    g_env->CreateWebResourceResponse(
                        stream.Get(), 404, L"Not Found",
                        L"Content-Type: text/plain; charset=utf-8",
                        &response);
                    args->put_Response(response.Get());
                    return S_OK;
                }

                auto mime = guessMimeType(path);
                ComPtr<IStream> stream;
                stream.Attach(SHCreateMemStream(
                    reinterpret_cast<const BYTE*>(entry->data), entry->size));
                if (!stream) return S_OK;

                ComPtr<ICoreWebView2WebResourceResponse> response;
                g_env->CreateWebResourceResponse(
                    stream.Get(), 200, L"OK",
                    (L"Content-Type: " + mime).c_str(),
                    &response);
                args->put_Response(response.Get());
                return S_OK;
            }).Get(), nullptr);
        return true;
    }
#endif

    auto dir = resolve_frontend_dir();
    if (dir.empty())
        return false;
    ComPtr<ICoreWebView2_3> v3;
    if (FAILED(view->QueryInterface(IID_PPV_ARGS(&v3))))
        return false;
    if (FAILED(v3->SetVirtualHostNameToFolderMapping(
            L"app.localhost", dir.c_str(),
            COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW))) return false;
    if (FAILED(v3->SetVirtualHostNameToFolderMapping(
            L"app.local", dir.c_str(),
            COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW))) return false;
    return true;
}

static bool isExpectedMainDocumentSource(const std::wstring& source) {
    if (source.empty() || _wcsicmp(source.c_str(), L"about:blank") == 0) return false;
    if (g_devUrl.empty())
        return source.rfind(L"https://app.localhost/", 0) == 0 ||
               source.rfind(L"https://app.local/", 0) == 0;

    std::wstring expected = g_devUrl;
    const auto hash = expected.find(L'#');
    if (hash != std::wstring::npos) expected.erase(hash);
    while (expected.size() > 1 && expected.back() == L'/') expected.pop_back();
    if (source.size() < expected.size()) return false;
    return _wcsnicmp(source.c_str(), expected.c_str(), expected.size()) == 0;
}

static void resetWebViewRenderState() {
    if (g_hwnd) {
        KillTimer(g_hwnd, WEBVIEW_RENDER_READY_TIMER_ID);
        KillTimer(g_hwnd, WEBVIEW_POST_SHOW_NUDGE_TIMER_ID);
    }
    g_webviewReady = false;
    g_webviewNavigationReady = false;
    g_webviewNavigationGeneration = 0;
    g_webviewNavigationId = 0;
    g_webviewCompletedNavigationId = 0;
    g_webviewRenderReadyGeneration = 0;
    g_webviewRenderReadyNavigationId = 0;
    g_webviewNeedsShowNudge = true;
}

static void finalizeWebViewRenderReady(
    unsigned long long generation, UINT64 navigationId, const char* source) {
    traceLog("WEBVIEW render-ready id=%llu source=%s",
             static_cast<unsigned long long>(navigationId), source ? source : "unknown");
    if (!g_hwnd || generation != g_webviewGeneration.load(std::memory_order_acquire) ||
        !g_webviewNavigationReady || g_webviewNavigationGeneration != generation ||
        navigationId == 0 || g_webviewCompletedNavigationId != navigationId ||
        g_webviewNavigationId != navigationId) return;

    KillTimer(g_hwnd, WEBVIEW_RENDER_READY_TIMER_ID);
    g_webviewReady = true;
    signalUpdateHandshake();
    g_webviewRenderReadyGeneration = generation;
    g_webviewRenderReadyNavigationId = navigationId;
    appendWebViewDiagnostic({
        {"event", "render-ready-complete"},
        {"source", source ? source : "unknown"},
        {"generation", generation},
        {"navigationId", std::to_string(navigationId)}
    });

    completeWebViewRecovery();

    if (g_deferFirstShow) {
        const int showCmd = g_firstShowCmd;
        g_deferFirstShow = false;
        showWindowAnimated(g_hwnd, showCmd, true);
        UpdateWindow(g_hwnd);
    }
    // 主界面已经完成首帧并显示后立刻关闭启动页；不设置最短展示时长。
    closeSplash();

    // A cold WebView2 compositor can retain a transparent frame until its
    // controller is toggled after the native host is actually visible. This
    // one-shot nudge automates the same recovery users observe after repeatedly
    // activating the taskbar/tray icon, without adding a polling loop.
    if (IsWindowVisible(g_hwnd) && !IsIconic(g_hwnd)) {
        g_webviewNeedsShowNudge = true;
        KillTimer(g_hwnd, WEBVIEW_POST_SHOW_NUDGE_TIMER_ID);
        SetTimer(g_hwnd, WEBVIEW_POST_SHOW_NUDGE_TIMER_ID, 60, nullptr);
    } else if (g_ctrl) {
        g_ctrl->put_IsVisible(FALSE);
    }
}

static void probeWebViewRenderState(unsigned long long generation, UINT64 navigationId) {
    if (!g_view || generation != g_webviewGeneration.load(std::memory_order_acquire) ||
        navigationId == 0 || navigationId != g_webviewNavigationId) return;

    static constexpr wchar_t script[] =
        LR"(JSON.stringify({readyState:document.readyState,appChildren:(document.getElementById('app')||{}).childElementCount||0,bodyChildren:(document.body||{}).childElementCount||0}))";
    const HRESULT executeHr = g_view->ExecuteScript(script,
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
        [generation, navigationId](HRESULT hr, LPCWSTR result) -> HRESULT {
            if (generation != g_webviewGeneration.load(std::memory_order_acquire) ||
                navigationId != g_webviewNavigationId || g_webviewReady) return S_OK;

            bool usableDom = false;
            std::string readyState;
            int appChildren = 0;
            int bodyChildren = 0;
            try {
                if (SUCCEEDED(hr) && result) {
                    auto outer = json::parse(W2U(result));
                    auto state = outer.is_string() ? json::parse(outer.get<std::string>()) : outer;
                    readyState = state.value("readyState", std::string{});
                    appChildren = state.value("appChildren", 0);
                    bodyChildren = state.value("bodyChildren", 0);
                    usableDom = (readyState == "interactive" || readyState == "complete") &&
                                appChildren > 0 && bodyChildren > 0;
                }
            } catch (...) {}

            appendWebViewDiagnostic({
                {"event", "render-ready-timeout-probe"},
                {"generation", generation},
                {"navigationId", std::to_string(navigationId)},
                {"hresult", static_cast<int64_t>(hr)},
                {"readyState", readyState},
                {"appChildren", appChildren},
                {"bodyChildren", bodyChildren},
                {"usable", usableDom}
            });

            if (usableDom) {
                finalizeWebViewRenderReady(generation, navigationId, "dom-probe-fallback");
                return S_OK;
            }

            if (!g_webviewRecoveryInProgress) {
                beginWebViewRecovery("render-timeout", "reload", 1);
                scheduleWebViewRecoveryAction(WebViewDeferredRecovery::Reload);
            } else if (g_webviewRecoveryAction == "reload") {
                beginWebViewRecovery("render-timeout", "controller-recreate", 2);
                scheduleWebViewRecoveryAction(WebViewDeferredRecovery::RecreateController);
            } else {
                failWebViewRecovery("frontend DOM unavailable after controller recovery");
            }
            return S_OK;
        }).Get());

    if (FAILED(executeHr)) {
        appendWebViewDiagnostic({
            {"event", "render-ready-probe-start-failed"},
            {"generation", generation},
            {"navigationId", std::to_string(navigationId)},
            {"hresult", static_cast<int64_t>(executeHr)}
        });
        if (!g_webviewRecoveryInProgress) {
            beginWebViewRecovery("render-timeout", "reload", 1);
            scheduleWebViewRecoveryAction(WebViewDeferredRecovery::Reload);
        }
    }
}

static void setupWebView(ICoreWebView2Controller* ctrl) {
    traceLog("WEBVIEW controller-ready");
    resetWebViewRenderState();
    g_ctrl = ctrl;
    g_ctrl->get_CoreWebView2(&g_view);

    RECT b; GetClientRect(g_hwnd, &b);
    g_ctrl->put_Bounds(b);
    // The controller is created asynchronously, after the native window may
    // already have been hidden by --minimized or tray-resident startup.  Sync
    // its initial visibility immediately instead of relying on a later
    // window.hidden event that can never arrive for this startup path.
    g_ctrl->put_IsVisible((IsWindowVisible(g_hwnd) && !IsIconic(g_hwnd)) || g_deferFirstShow);

    // Background color from config。无边框透明基线固定 alpha=0；可见底板由前端分块 rgba 绘制。
    ComPtr<ICoreWebView2Controller2> ctrl2;
    if (SUCCEEDED(g_ctrl.As(&ctrl2))) {
        auto clr = currentWindowBackgroundColor();
        BYTE alpha = g_frameless ? 0 : 255;
        ctrl2->put_DefaultBackgroundColor({alpha, GetRValue(clr), GetGValue(clr), GetBValue(clr)});
    }

    // Wails: use raw pixels mode for better resize performance
    ComPtr<ICoreWebView2Controller3> ctrl3;
    if (SUCCEEDED(g_ctrl.As(&ctrl3))) {
        ctrl3->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS);
        ctrl3->put_ShouldDetectMonitorScaleChanges(FALSE);
        UINT dpi = GetDpiForWindow(g_hwnd);
        ctrl3->put_RasterizationScale(dpi / 96.0);
    }

    // Settings
    ComPtr<ICoreWebView2Settings> s;
    g_view->get_Settings(&s);
    s->put_IsScriptEnabled(TRUE);
    s->put_AreDefaultScriptDialogsEnabled(TRUE);
    s->put_IsWebMessageEnabled(TRUE);
    bool dev = !g_devUrl.empty();
    s->put_AreDevToolsEnabled(TRUE);                 // 始终允许 DevTools（F12 / 右键）便于排查
    s->put_AreDefaultContextMenusEnabled(TRUE);
    s->put_IsStatusBarEnabled(FALSE);

    // Wails-aligned: disable browser shortcuts (Ctrl+P, Ctrl+S, etc.)
    ComPtr<ICoreWebView2Settings3> s3;
    if (SUCCEEDED(s.As(&s3)))
        s3->put_AreBrowserAcceleratorKeysEnabled(FALSE);

    // Disable swipe navigation (prevents accidental back/forward on touchpad)
    ComPtr<ICoreWebView2Settings6> s6;
    if (SUCCEEDED(s.As(&s6)))
        s6->put_IsSwipeNavigationEnabled(FALSE);

    // Disable pinch zoom (desktop apps usually don't need it)
    ComPtr<ICoreWebView2Settings5> s5;
    if (SUCCEEDED(s.As(&s5)))
        s5->put_IsPinchZoomEnabled(FALSE);

    // Enable CSS app-region:drag for declarative title bar drag.
    // 全屏吸附模式：禁用非客户区支持，彻底关闭 CSS 拖动（从根源锁定窗口）。
    ComPtr<ICoreWebView2Settings9> s9;
    if (SUCCEEDED(s.As(&s9)))
        s9->put_IsNonClientRegionSupportEnabled(g_fullHeight ? FALSE : TRUE);

    // WebView permissions are denied by default. Apps that embed trusted pages
    // can opt into auto-allow with window.allowWebviewPermissions.
    g_view->add_PermissionRequested(
        Callback<ICoreWebView2PermissionRequestedEventHandler>(
        [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
            args->put_State(g_allowWebviewPermissions
                ? COREWEBVIEW2_PERMISSION_STATE_ALLOW
                : COREWEBVIEW2_PERMISSION_STATE_DENY);
            return S_OK;
        }).Get(), nullptr);

    // Process failure collection only. Recovery and filesystem work are dispatched
    // to the window thread so no exception or blocking operation crosses the COM callback.
    g_view->add_ProcessFailed(
        Callback<ICoreWebView2ProcessFailedEventHandler>(
        [](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
            if (!args) return S_OK;
            auto* info = new (std::nothrow) WebViewFailureInfo();
            if (!info) return S_OK;

            LPWSTR description = nullptr;
            LPWSTR modulePath = nullptr;
            try {
                args->get_ProcessFailedKind(&info->kind);
                info->tick = GetTickCount64();

                ComPtr<ICoreWebView2ProcessFailedEventArgs2> args2;
                if (SUCCEEDED(args->QueryInterface(IID_PPV_ARGS(&args2)))) {
                    args2->get_Reason(&info->reason);
                    args2->get_ExitCode(&info->exitCode);
                    if (SUCCEEDED(args2->get_ProcessDescription(&description)) && description)
                        info->description = description;
                }

                ComPtr<ICoreWebView2ProcessFailedEventArgs3> args3;
                if (SUCCEEDED(args->QueryInterface(IID_PPV_ARGS(&args3))) &&
                    SUCCEEDED(args3->get_FailureSourceModulePath(&modulePath)) && modulePath)
                    info->modulePath = modulePath;
            } catch (...) {
                if (description) CoTaskMemFree(description);
                if (modulePath) CoTaskMemFree(modulePath);
                delete info;
                return S_OK;
            }
            if (description) CoTaskMemFree(description);
            if (modulePath) CoTaskMemFree(modulePath);

            if (!g_hwnd || !PostMessageW(g_hwnd, WM_WEBVIEW_PROCESS_FAILED, 0,
                    reinterpret_cast<LPARAM>(info)))
                delete info;
            return S_OK;
        }).Get(), nullptr);

    // IPC handler
    g_view->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
        [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* a) -> HRESULT {
            LPWSTR m; a->get_WebMessageAsJson(&m);
            ipc_dispatch(m);
            CoTaskMemFree(m);
            return S_OK;
        }).Get(), nullptr);

    // 首次导航监听必须先注册；本地虚拟 host 很快，先 Navigate 会丢失完成事件并让窗口永久隐藏。
    EventRegistrationToken navigationStartingToken{};
    HRESULT navStartingHr = g_view->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
        [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
            if (!args) return S_OK;
            LPWSTR uriRaw = nullptr;
            UINT64 navigationId = 0;
            args->get_Uri(&uriRaw);
            args->get_NavigationId(&navigationId);
            const std::wstring uri = uriRaw ? uriRaw : L"";
            if (uriRaw) CoTaskMemFree(uriRaw);
            if (!isExpectedMainDocumentSource(uri)) return S_OK;

            // Hash-only navigation in an already healthy page is not a new
            // renderer bootstrap and must not re-arm the first-frame gate.
            if (g_webviewReady && !g_webviewRecoveryInProgress && !g_deferFirstShow)
                return S_OK;

            KillTimer(g_hwnd, WEBVIEW_RENDER_READY_TIMER_ID);
            g_webviewReady = false;
            g_webviewNavigationReady = false;
            g_webviewNavigationGeneration = g_webviewGeneration.load(std::memory_order_acquire);
            g_webviewNavigationId = navigationId;
            g_webviewCompletedNavigationId = 0;
            g_webviewRenderReadyGeneration = 0;
            g_webviewRenderReadyNavigationId = 0;
            appendWebViewDiagnostic({
                {"event", "navigation-start"},
                {"generation", g_webviewNavigationGeneration},
                {"navigationId", std::to_string(navigationId)},
                {"uri", W2U(uri)}
            });
            return S_OK;
        }).Get(), &navigationStartingToken);
    if (FAILED(navStartingHr)) {
        MessageBoxW(g_hwnd, L"Unable to register WebView2 navigation start handler.",
                    L"UI loading failed", MB_ICONERROR);
        PostQuitMessage(1);
        return;
    }

    EventRegistrationToken navigationToken{};
    HRESULT navHandlerHr = g_view->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
        [](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
            UINT64 navigationId = 0;
            if (args) args->get_NavigationId(&navigationId);
            LPWSTR sourceRaw = nullptr;
            if (sender) sender->get_Source(&sourceRaw);
            const std::wstring source = sourceRaw ? sourceRaw : L"";
            if (sourceRaw) CoTaskMemFree(sourceRaw);

            const auto generation = g_webviewGeneration.load(std::memory_order_acquire);
            const bool expectedNavigation =
                navigationId != 0 && navigationId == g_webviewNavigationId &&
                generation == g_webviewNavigationGeneration &&
                isExpectedMainDocumentSource(source);
            if (!expectedNavigation) {
                if (g_deferFirstShow || g_webviewRecoveryInProgress) {
                    appendWebViewDiagnostic({
                        {"event", "navigation-complete-ignored"},
                        {"generation", generation},
                        {"navigationId", std::to_string(navigationId)},
                        {"expectedNavigationId", std::to_string(g_webviewNavigationId)},
                        {"source", W2U(source)}
                    });
                }
                return S_OK;
            }

            BOOL success = FALSE;
            if (!args || FAILED(args->get_IsSuccess(&success)) || !success) {
                COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                if (args) args->get_WebErrorStatus(&status);
                if (g_webviewRecoveryInProgress) {
                    appendWebViewDiagnostic({
                        {"event", "recovery-navigation-failed"},
                        {"status", static_cast<int>(status)}
                    });
                    failWebViewRecovery("navigation failed after recovery");
                    return S_OK;
                }
                std::wstring msg = L"前端资源加载失败，YeManCC 无法显示界面。\n\nWebView2 错误码：" +
                    std::to_wstring(static_cast<int>(status));
                MessageBoxW(g_hwnd, msg.c_str(), L"界面加载失败", MB_ICONERROR);
                PostQuitMessage(1);
                return S_OK;
            }
            g_webviewNavigationReady = true;
            g_webviewCompletedNavigationId = navigationId;
            traceLog("WEBVIEW navigation-complete id=%llu", static_cast<unsigned long long>(navigationId));
            appendWebViewDiagnostic({
                {"event", "navigation-complete"},
                {"generation", generation},
                {"navigationId", std::to_string(navigationId)},
                {"source", W2U(source)}
            });

            // The controller may paint while the native host is deferred, but
            // the window is revealed only after the Vue page acknowledges two
            // animation frames. This avoids accepting about:blank or an empty
            // compositor frame as a usable UI.
            if (g_ctrl) {
                g_ctrl->put_IsVisible(FALSE);
                g_ctrl->put_IsVisible((IsWindowVisible(g_hwnd) && !IsIconic(g_hwnd)) || g_deferFirstShow);
            }
            if (g_webviewRenderReadyGeneration == generation &&
                g_webviewRenderReadyNavigationId == navigationId) {
                finalizeWebViewRenderReady(generation, navigationId, "frontend-before-navigation-complete");
            } else {
                KillTimer(g_hwnd, WEBVIEW_RENDER_READY_TIMER_ID);
                SetTimer(g_hwnd, WEBVIEW_RENDER_READY_TIMER_ID, WEBVIEW_RENDER_READY_TIMEOUT_MS, nullptr);
            }
            return S_OK;
        }).Get(), &navigationToken);
    if (FAILED(navHandlerHr)) {
        MessageBoxW(g_hwnd, L"无法注册 WebView2 页面加载监听。", L"界面加载失败", MB_ICONERROR);
        PostQuitMessage(1);
        return;
    }

    HRESULT navigateHr = E_FAIL;
    if (dev) {
        // 开发模式仍需注册 user-assets.localhost，否则自定义背景 URL 无法解析；失败只禁用可选背景，不阻断主界面。
        configureUserAssetsHost(g_view.Get());
        configureMusicHost(g_view.Get());
        navigateHr = g_view->Navigate(g_devUrl.c_str());
    } else {
        if (!configureAppHost(g_view.Get())) {
            MessageBoxW(
                g_hwnd,
                L"未找到或无法映射前端资源（index.html）。\n普通构建请保留 dist 目录；如需单文件分发，请使用 bun run build:single 或 bun run package:single。",
                L"错误",
                MB_ICONERROR);
            PostQuitMessage(1);
            return;
        }
        navigateHr = g_view->Navigate(L"https://app.localhost/index.html");
    }
    if (FAILED(navigateHr)) {
        MessageBoxW(g_hwnd, L"WebView2 无法发起页面导航。", L"界面加载失败", MB_ICONERROR);
        PostQuitMessage(1);
    }
}

// If WebView2 Runtime is missing, download the evergreen bootstrapper and run it.
static void installWebView2Runtime() {
    wchar_t tempPath[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tempPath) == 0) return;
    std::wstring setupPath = std::wstring(tempPath) + L"MicrosoftEdgeWebview2Setup.exe";
    const wchar_t* url = L"https://go.microsoft.com/fwlink/p/?LinkId=2124703";
    HRESULT hr = URLDownloadToFileW(nullptr, url, setupPath.c_str(), 0, nullptr);
    if (SUCCEEDED(hr)) {
        ShellExecuteW(g_hwnd, L"open", setupPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        MessageBoxW(g_hwnd,
            L"WebView2 Runtime 安装程序已启动。\n安装完成后请重新打开 YeManCC。",
            L"正在安装", MB_ICONINFORMATION);
    } else {
        MessageBoxW(g_hwnd,
            L"下载安装程序失败，请手动访问以下链接安装：\nhttps://go.microsoft.com/fwlink/p/?LinkId=2124703",
            L"错误", MB_ICONERROR);
    }
}

static void init_webview(unsigned long long generation = 0) {
    traceLog("WEBVIEW init-start generation=%llu", generation);
    if (generation == 0)
        generation = g_webviewGeneration.load(std::memory_order_acquire);
    g_webviewGpuMode = configuredWebViewGpuMode();
    auto dataDir = webview_data_dir();

    // Only an explicit marker written after BROWSER_PROCESS_EXITED triggers isolation.
    // Crashpad dumps alone are diagnostic evidence and must never delete user data.
    isolateMarkedWebViewProfile();

    // GPU A/B profiles: default uses WebView2 isolation, legacy reproduces the old
    // in-process/sandbox-disabled behavior, and software is the initialization fallback.
    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    auto browserArgs = webViewBrowserArguments(g_webviewGpuMode);
    options->put_AdditionalBrowserArguments(browserArgs.c_str());
    HRESULT createHr = CreateCoreWebView2EnvironmentWithOptions(nullptr, dataDir.c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [generation](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
            if (generation != g_webviewGeneration.load(std::memory_order_acquire)) return S_OK;
            // GPU-fallback retry: if first init fails (common on some GPU drivers),
            // retry once with --disable-gpu for pure software rendering.
            if (FAILED(hr)) {
                auto dataDir2 = webview_data_dir();
                g_webviewGpuMode = WebViewGpuMode::Software;
                appendWebViewDiagnostic({
                    {"event", "environment-init-fallback"},
                    {"initialHresult", static_cast<int64_t>(hr)}
                });
                auto opts2 = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
                auto fallbackArgs = webViewBrowserArguments(g_webviewGpuMode);
                opts2->put_AdditionalBrowserArguments(fallbackArgs.c_str());
                return CreateCoreWebView2EnvironmentWithOptions(
                    nullptr, dataDir2.c_str(), opts2.Get(),
                    Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                    [generation](HRESULT hr2, ICoreWebView2Environment* env2) -> HRESULT {
                        if (generation != g_webviewGeneration.load(std::memory_order_acquire)) return S_OK;
                        if (FAILED(hr2)) {
                            auto res = MessageBoxW(g_hwnd,
                                L"WebView2 Runtime 未安装或已损坏，YeManCC 无法启动。\n\n是否现在下载并安装 WebView2 Runtime？",
                                L"缺少 WebView2 Runtime", MB_ICONQUESTION | MB_YESNO);
                            if (res == IDYES)
                                installWebView2Runtime();
                            PostQuitMessage(1);
                            return hr2;
                        }
                        g_env = env2;
                        HRESULT controllerHr = finishCreateController(g_env.Get(), generation);
                        if (FAILED(controllerHr)) {
                            MessageBoxW(g_hwnd, L"WebView2 控制器创建失败，YeManCC 无法显示界面。",
                                L"WebView2 初始化失败", MB_ICONERROR);
                            PostQuitMessage(1);
                            return controllerHr;
                        }
                        return S_OK;
                    }).Get());
            }
            g_env = env;
            HRESULT controllerHr = finishCreateController(g_env.Get(), generation);
            if (FAILED(controllerHr)) {
                MessageBoxW(g_hwnd, L"WebView2 控制器创建失败，YeManCC 无法显示界面。",
                    L"WebView2 初始化失败", MB_ICONERROR);
                PostQuitMessage(1);
                return controllerHr;
            }
            return S_OK;
        }).Get());
    if (FAILED(createHr)) {
        MessageBoxW(g_hwnd,
            L"WebView2 初始化请求失败，YeManCC 无法创建界面。\n\n请修复或重新安装 WebView2 Runtime。",
            L"WebView2 初始化失败", MB_ICONERROR);
        PostQuitMessage(1);
    }
}

// Shared controller creation logic (used by both normal and GPU-fallback init paths).
// Sets background color to avoid white flash, then creates the controller + calls setupWebView.
static HRESULT finishCreateController(ICoreWebView2Environment* env, unsigned long long generation) {
    if (!env) return E_POINTER;
    if (generation == 0)
        generation = g_webviewGeneration.load(std::memory_order_acquire);
    ComPtr<ICoreWebView2Environment10> env10;
    auto handler = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        [generation](HRESULT hr, ICoreWebView2Controller* ctrl) -> HRESULT {
            if (generation != g_webviewGeneration.load(std::memory_order_acquire)) {
                if (ctrl) ctrl->Close();
                appendWebViewDiagnostic({
                    {"event", "stale-controller-callback"},
                    {"generation", generation},
                    {"currentGeneration", g_webviewGeneration.load(std::memory_order_acquire)}
                });
                return S_OK;
            }
            if (FAILED(hr)) {
                if (g_webviewRecoveryInProgress) failWebViewRecovery("controller callback failed");
                else PostQuitMessage(1);
                return hr;
            }
            setupWebView(ctrl);
            return S_OK;
        });
    if (SUCCEEDED(env->QueryInterface(IID_PPV_ARGS(&env10)))) {
        ComPtr<ICoreWebView2ControllerOptions> opts;
        if (SUCCEEDED(env10->CreateCoreWebView2ControllerOptions(&opts))) {
            ComPtr<ICoreWebView2ControllerOptions3> opts3;
            if (SUCCEEDED(opts.As(&opts3))) {
                auto clr = currentWindowBackgroundColor();
                BYTE alpha = g_frameless ? 0 : 255;
                opts3->put_DefaultBackgroundColor({alpha, GetRValue(clr), GetGValue(clr), GetBValue(clr)});
            }
            return env10->CreateCoreWebView2ControllerWithOptions(g_hwnd, opts.Get(), handler.Get());
        }
    }
    return env->CreateCoreWebView2Controller(g_hwnd, handler.Get());
}

static std::deque<ULONGLONG>& webViewFailureHistory(COREWEBVIEW2_PROCESS_FAILED_KIND kind) {
    if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED)
        return g_webviewBrowserFailures;
    if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_GPU_PROCESS_EXITED)
        return g_webviewGpuFailures;
    if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED ||
        kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE ||
        kind == COREWEBVIEW2_PROCESS_FAILED_KIND_FRAME_RENDER_PROCESS_EXITED)
        return g_webviewRendererFailures;
    return g_webviewUtilityFailures;
}

static size_t recordWebViewFailureAttempt(COREWEBVIEW2_PROCESS_FAILED_KIND kind) {
    auto& history = webViewFailureHistory(kind);
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG windowMs =
        kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED ? 5ULL * 60 * 1000 : 60ULL * 1000;
    while (!history.empty() && now - history.front() > windowMs) history.pop_front();
    history.push_back(now);
    return history.size();
}

static void beginWebViewRecovery(const char* kind, const char* action, size_t attempt) {
    const auto generation = g_webviewGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    resetWebViewRenderState();
    g_webviewRecoveryInProgress = true;
    g_webviewRecoveryAction = action ? action : "unknown";
    KillTimer(g_hwnd, WEBVIEW_RECOVERY_ACTION_TIMER_ID);
    g_webviewDeferredRecovery = WebViewDeferredRecovery::None;
    SetTimer(g_hwnd, WEBVIEW_RECOVERY_TIMER_ID, 20000, nullptr);
    ipc_emit("webview.recovering", {
        {"kind", kind ? kind : "unknown"},
        {"action", action ? action : "unknown"},
        {"attempt", attempt},
        {"generation", generation},
        {"reason", "WebView2 process recovery"}
    });
    appendWebViewDiagnostic({
        {"event", "recovery-start"},
        {"kind", kind ? kind : "unknown"},
        {"action", action ? action : "unknown"},
        {"attempt", attempt},
        {"generation", generation}
    });
}

static void failWebViewRecovery(const char* reason) {
    appendWebViewDiagnostic({
        {"event", "recovery-failed"},
        {"action", g_webviewRecoveryAction},
        {"reason", reason ? reason : "unknown"}
    });
    KillTimer(g_hwnd, WEBVIEW_RECOVERY_TIMER_ID);
    KillTimer(g_hwnd, WEBVIEW_RECOVERY_ACTION_TIMER_ID);
    KillTimer(g_hwnd, WEBVIEW_RENDER_READY_TIMER_ID);
    KillTimer(g_hwnd, WEBVIEW_POST_SHOW_NUDGE_TIMER_ID);
    g_webviewDeferredRecovery = WebViewDeferredRecovery::None;
    g_webviewRecoveryInProgress = false;
    beginAsyncExit(g_hwnd, 1);
}

static bool recreateWebViewController() {
    const bool wasVisible = g_hwnd && IsWindowVisible(g_hwnd) && !IsIconic(g_hwnd);
    if (wasVisible) {
        g_firstShowCmd = IsZoomed(g_hwnd) ? SW_MAXIMIZE : SW_SHOW;
        g_deferFirstShow = true;
        hideWindowAnimated(g_hwnd);
    }
    g_webviewReady = false;
    closeWebViewControllers();
    if (!g_env) return false;
    return SUCCEEDED(finishCreateController(g_env.Get()));
}

static void scheduleWebViewRecoveryAction(WebViewDeferredRecovery action) {
    g_webviewDeferredRecovery = action;
    // PostWebMessageAsJson is asynchronous.  Give the live renderer a bounded
    // window to run rejectAllPending() before reload/controller destruction.
    // This matters for GPU/utility failures where the page itself is still alive.
    SetTimer(g_hwnd, WEBVIEW_RECOVERY_ACTION_TIMER_ID, 100, nullptr);
}

static void executeWebViewRecoveryAction() {
    KillTimer(g_hwnd, WEBVIEW_RECOVERY_ACTION_TIMER_ID);
    const auto action = g_webviewDeferredRecovery;
    g_webviewDeferredRecovery = WebViewDeferredRecovery::None;
    if (!g_webviewRecoveryInProgress) return;

    if (action == WebViewDeferredRecovery::Reload) {
        HRESULT hr = g_view ? g_view->Reload() : E_FAIL;
        if (FAILED(hr)) {
            appendWebViewDiagnostic({{"event", "reload-failed"}, {"hresult", static_cast<int64_t>(hr)}});
            if (!recreateWebViewController()) failWebViewRecovery("reload and controller recreation failed");
            else g_webviewRecoveryAction = "controller-recreate";
        }
        return;
    }

    if (action == WebViewDeferredRecovery::RecreateController &&
        !recreateWebViewController())
        failWebViewRecovery("controller recreation failed");
}

static DWORD WINAPI browserProfileIsolationThread(LPVOID) {
    bool ok = false;
    for (int attempt = 0; attempt < 12; ++attempt) {
        if (isolateWebViewProfile("browser-runtime")) {
            ok = true;
            break;
        }
        Sleep(150);
    }
    if (g_hwnd && !PostMessageW(g_hwnd, WM_WEBVIEW_RECOVERY_RESTART, ok ? 1 : 0, 0))
        return 0;
    return 0;
}

static void recoverWebViewProcess(const WebViewFailureInfo& info) {
    if (g_exitRequested.load(std::memory_order_acquire)) return;
    const auto kind = webViewProcessKindName(info.kind);
    const size_t attempt = recordWebViewFailureAttempt(info.kind);

    if (g_webviewRecoveryInProgress) {
        appendWebViewDiagnostic({
            {"event", "recovery-overlap"},
            {"kind", kind},
            {"attempt", attempt}
        });
        if (info.kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED)
            failWebViewRecovery("browser failed during recovery");
        return;
    }

    if (info.kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED) {
        if (attempt > 2) {
            appendWebViewDiagnostic({{"event", "recovery-exhausted"}, {"kind", kind}, {"attempt", attempt}});
            beginAsyncExit(g_hwnd, 1);
            return;
        }
        beginWebViewRecovery(kind, "browser-environment", attempt);
        const bool markerWritten = writeWebViewFailureMarker(info);
        appendWebViewDiagnostic({{"event", "browser-runtime-marker"}, {"ok", markerWritten}});
        if (g_hwnd && IsWindowVisible(g_hwnd) && !IsIconic(g_hwnd)) {
            g_firstShowCmd = IsZoomed(g_hwnd) ? SW_MAXIMIZE : SW_SHOW;
            g_deferFirstShow = true;
            hideWindowAnimated(g_hwnd);
        }
        g_webviewReady = false;
        closeWebViewControllers();
        g_env.Reset();
        HANDLE worker = CreateThread(nullptr, 0, browserProfileIsolationThread, nullptr, 0, nullptr);
        if (worker) CloseHandle(worker);
        else failWebViewRecovery("cannot create profile isolation worker");
        return;
    }

    if (attempt > 2) {
        appendWebViewDiagnostic({{"event", "recovery-exhausted"}, {"kind", kind}, {"attempt", attempt}});
        beginAsyncExit(g_hwnd, 1);
        return;
    }

    if (attempt == 1) {
        beginWebViewRecovery(kind, "reload", attempt);
        scheduleWebViewRecoveryAction(WebViewDeferredRecovery::Reload);
        return;
    }

    beginWebViewRecovery(kind, "controller-recreate", attempt);
    scheduleWebViewRecoveryAction(WebViewDeferredRecovery::RecreateController);
}

static void completeWebViewRecovery() {
    if (!g_webviewRecoveryInProgress) return;
    const bool powerResumeRecovery =
        g_resumeWatchdogGeneration != 0 &&
        g_resumeWatchdogGeneration == currentPowerGeneration() &&
        g_powerLifecycle.load(std::memory_order_acquire) == PowerLifecycle::Resuming;
    KillTimer(g_hwnd, WEBVIEW_RECOVERY_TIMER_ID);
    KillTimer(g_hwnd, WEBVIEW_RECOVERY_ACTION_TIMER_ID);
    g_webviewDeferredRecovery = WebViewDeferredRecovery::None;
    appendWebViewDiagnostic({
        {"event", "recovery-complete"},
        {"action", g_webviewRecoveryAction},
        {"generation", g_webviewGeneration.load(std::memory_order_acquire)}
    });
    g_webviewRecoveryInProgress = false;
    g_webviewRecoveryAction.clear();
    if (powerResumeRecovery)
        SetTimer(g_hwnd, POWER_RESUME_WATCHDOG_TIMER_ID, POWER_RESUME_WATCHDOG_RETRY_MS, nullptr);
}

static void handlePowerResumeNotification(SgWork work, const char* reason) {
    const ULONGLONG now = GetTickCount64();
    auto generation = currentPowerGeneration();
    const auto phase = g_powerLifecycle.load(std::memory_order_acquire);

    // Windows commonly sends RESUMEAUTOMATIC followed by RESUMESUSPEND for
    // one physical wake. If the first event has already committed, the second
    // must not close the gate again: resume-ready is generation-deduplicated,
    // so reopening the same transaction would otherwise leave it stuck.
    const bool recentlyCommittedDuplicate =
        phase == PowerLifecycle::Ready &&
        g_resumeReadyGeneration.load(std::memory_order_acquire) == generation &&
        g_lastResumeNotifyTick != 0 && now - g_lastResumeNotifyTick <= 5000;
    if (recentlyCommittedDuplicate) {
        traceLog("power resume duplicate ignored reason=%s generation=%llu",
                 reason ? reason : "unknown", generation);
        // A user-confirmed wake must still cancel the automatic re-sleep
        // observation even when native/frontend recovery already committed.
        if (work == SgWork::WakeSuspend) sgQueueWork(work, generation);
        return;
    }

    if (phase != PowerLifecycle::Resuming) {
        if (phase == PowerLifecycle::Ready) {
            // Some modern systems can report a resume without a preceding
            // PBT_APMSUSPEND. Give that physical wake its own generation.
            generation = g_powerGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
            g_resumeReadyGeneration.store(0, std::memory_order_release);
        }
        g_powerLifecycle.store(PowerLifecycle::Resuming, std::memory_order_release);
        closeHardwareWriteGate(reason);
        g_inputReady.store(false, std::memory_order_release);
        gamepadResetNativeState(true);
        g_lastResumeNotifyTick = now;
        armPowerResumeWatchdog(generation);
        ipc_emit("power.resuming", {{"generation", generation}});
    } else {
        armPowerResumeWatchdog(generation);
    }
    sgQueueWork(work, generation);
}

// ================================================================
//  Window procedure
// ================================================================

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_taskbarCreatedMsg && m == g_taskbarCreatedMsg) {
        g_trayActive = false;
        g_memTrayLevel = -1;
        if (g_memTrayIcon) { DestroyIcon(g_memTrayIcon); g_memTrayIcon = nullptr; }
        applyResidentMode();
        return 0;
    }
    if (g_showExistingInstanceMsg && m == g_showExistingInstanceMsg) {
        // A second process must not call ShowWindow on our HWND directly: it
        // cannot resynchronize this process's WebView2 controller and can expose
        // only the transparent native shell. Restore entirely on the owner UI
        // thread, then perform the normal one-shot compositor nudge.
        showWindowAnimated(h, IsIconic(h) ? SW_RESTORE : SW_SHOW, true);
        refocusWebView();
        return 0;
    }
    switch (m) {
    // ── Fill background (visible briefly before WebView2 content loads) ──
    case WM_NCPAINT:
        if (g_frameless) return 0;
        break;
    case WM_NCACTIVATE:
        if (g_frameless) return TRUE; // prevents DefWindowProc from repainting NC area
        break;
    case WM_ERASEBKGND:
        if (g_frameless) {
            // 根窗口保持透明，避免原生 GDI 底色覆盖前端分块底板与桌面。
            return 1;
        }
        break;
    case WM_PAINT:
        if (g_frameless) {
            // 无边框窗口不绘制整窗背景；WebView2 和前端局部底板负责内容。
            PAINTSTRUCT ps;
            BeginPaint(h, &ps);
            EndPaint(h, &ps);
            return 0;
        }
        break;

    case WM_SHOWWINDOW:
        if (w) {
            if (g_webviewReady) {
                g_webviewNeedsShowNudge = true;
                KillTimer(g_hwnd, WEBVIEW_POST_SHOW_NUDGE_TIMER_ID);
                SetTimer(g_hwnd, WEBVIEW_POST_SHOW_NUDGE_TIMER_ID, 80, nullptr);
            }
        } else {
            g_webviewNeedsShowNudge = true;
            if (g_ctrl) g_ctrl->put_IsVisible(FALSE);
        }
        break;

    case WM_ACTIVATE:
        if (LOWORD(w) != WA_INACTIVE && g_webviewNeedsShowNudge && g_webviewReady &&
            IsWindowVisible(h) && !IsIconic(h)) {
            KillTimer(g_hwnd, WEBVIEW_POST_SHOW_NUDGE_TIMER_ID);
            SetTimer(g_hwnd, WEBVIEW_POST_SHOW_NUDGE_TIMER_ID, 30, nullptr);
        }
        break;

    case WM_ENTERSIZEMOVE:
        g_resizing = true;
        // Suspend WebView2 redraws during resize drag to prevent composition storm
        if (g_hwnd) SendMessageW(g_hwnd, WM_SETREDRAW, FALSE, 0);
        break;

    case WM_EXITSIZEMOVE:
        g_resizing = false;
        if (g_hwnd) SendMessageW(g_hwnd, WM_SETREDRAW, TRUE, 0);
        // Apply final bounds now that the drag is done
        if (g_ctrl) {
            RECT b; GetClientRect(g_hwnd, &b);
            g_ctrl->put_Bounds(b);
        }
        InvalidateRect(g_hwnd, nullptr, FALSE);
        saveWindowState();
        break;

    case WM_SIZE:
        // During resize drag (WM_ENTERSIZEMOVE→WM_EXITSIZEMOVE), Windows floods
        // WM_SIZE. Calling put_Bounds on every message causes WebView2 composition
        // to re-layout synchronously, which blocks the message pump → freeze.
        // Instead, we only apply the final bounds on WM_EXITSIZEMOVE.
        if (g_ctrl && !g_resizing) {
            RECT b; GetClientRect(h, &b);
            g_ctrl->put_Bounds(b);
        }
        if (w == SIZE_MAXIMIZED)      ipc_emit("window.maximized");
        else if (w == SIZE_MINIMIZED) ipc_emit("window.minimized");
        else if (w == SIZE_RESTORED)  ipc_emit("window.restored");
        ipc_emit("window.resized", {{"w", (int)LOWORD(l)}, {"h", (int)HIWORD(l)}});
        return 0;

    case WM_DPICHANGED:
        if (g_ctrl) {
            ComPtr<ICoreWebView2Controller3> ctrl3;
            if (SUCCEEDED(g_ctrl.As(&ctrl3)))
                ctrl3->put_RasterizationScale(HIWORD(w) / 96.0);
        }
        if (g_fullHeight) {
            applyFullHeightLayout(focusCurrentTargetMonitor()); // 重新按目标显示器工作区计算
        } else {
            auto* r = reinterpret_cast<RECT*>(l);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER);
        }
        return 0;

    case WM_DISPLAYCHANGE:
        // Exclusive fullscreen / Steam Big Picture can rebuild the display
        // compositor without a normal tray restore. Reflow the window and
        // refresh the controller once after the display settles.
        focusScheduleDisplayReflow();
        if (g_ctrl) {
            RECT bounds{};
            if (GetClientRect(h, &bounds)) g_ctrl->put_Bounds(bounds);
        }
        if (g_webviewReady && IsWindowVisible(h) && !IsIconic(h)) {
            g_webviewNeedsShowNudge = true;
            KillTimer(g_hwnd, WEBVIEW_POST_SHOW_NUDGE_TIMER_ID);
            SetTimer(g_hwnd, WEBVIEW_POST_SHOW_NUDGE_TIMER_ID, 150, nullptr);
        }
        return 0;

    case WM_MOVE:
    case WM_MOVING:
        // Wails: notify WebView2 of position changes (fixes popup positioning)
        if (g_ctrl) {
            ComPtr<ICoreWebView2Controller3> ctrl3;
            if (SUCCEEDED(g_ctrl.As(&ctrl3)))
                ctrl3->NotifyParentWindowPositionChanged();
        }
        if (m == WM_MOVE)
            ipc_emit("window.moved", {{"x", (int)(short)LOWORD(l)}, {"y", (int)(short)HIWORD(l)}});
        return 0;

    case WM_SETTINGCHANGE:
        if (w == SPI_SETWORKAREA) focusScheduleDisplayReflow();
        if (l) {
            auto setting = W2U((LPCWSTR)l);
            if (setting == "ImmersiveColorSet") {
                applyNativeTheme();
                ipc_emit("os.themeChanged", systemThemeInfo());
            }
        }
        return 0;
    case WM_THEMECHANGED:
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        applyNativeTheme();
        ipc_emit("os.themeChanged", systemThemeInfo());
        return 0;
    case WM_SETFOCUS:
        if (g_ctrl) g_ctrl->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        ipc_emit("window.focus");
        return 0;
    case WM_KILLFOCUS:
        ipc_emit("window.blur");
        return 0;
    case WM_TIMER:
        if (w == POWER_RESUME_NUDGE_TIMER_ID) {
            KillTimer(g_hwnd, POWER_RESUME_NUDGE_TIMER_ID);
            if (g_powerLifecycle.load(std::memory_order_acquire) == PowerLifecycle::Resuming) {
                const auto generation = currentPowerGeneration();
                if (g_resumeProbeGeneration == generation)
                    probePowerResumeStability(generation);
                else
                    nudgeWebViewAfterResume(false);
            }
            return 0;
        }
        if (w == POWER_RESUME_WATCHDOG_TIMER_ID) {
            KillTimer(g_hwnd, POWER_RESUME_WATCHDOG_TIMER_ID);
            if (g_powerLifecycle.load(std::memory_order_acquire) != PowerLifecycle::Resuming ||
                g_resumeWatchdogGeneration != currentPowerGeneration()) {
                stopPowerResumeWatchdog();
                return 0;
            }
            // A healthy resume normally commits well before this point. If the
            // renderer retained a stale frame and stopped dispatching input,
            // force the same bounded recovery path used for renderer failures.
            if (!g_webviewRecoveryInProgress && g_resumeWatchdogAttempts == 0) {
                g_resumeWatchdogAttempts = 1;
                beginWebViewRecovery("power-resume", "reload", 1);
                scheduleWebViewRecoveryAction(WebViewDeferredRecovery::Reload);
                return 0;
            }
            if (!g_webviewRecoveryInProgress && g_resumeWatchdogAttempts == 1) {
                g_resumeWatchdogAttempts = 2;
                beginWebViewRecovery("power-resume", "controller-recreate", 2);
                scheduleWebViewRecoveryAction(WebViewDeferredRecovery::RecreateController);
                return 0;
            }
            appendWebViewDiagnostic({
                {"event", "power-resume-watchdog-degraded"},
                {"generation", currentPowerGeneration()},
                {"attempts", g_resumeWatchdogAttempts}
            });
            // At this point native sleep-guard recovery is complete, but two
            // renderer recovery attempts did not produce a frontend commit.
            // Fail open for hardware writes instead of leaving the entire CPU/
            // TDP control plane permanently disabled until the app restarts.
            if (g_resumeReadyGeneration.load(std::memory_order_acquire) == currentPowerGeneration()) {
                const bool inputReady = gamepadRecoverAfterResume();
                g_inputReady.store(inputReady, std::memory_order_release);
                g_powerLifecycle.store(PowerLifecycle::Ready, std::memory_order_release);
                openHardwareWriteGate();
                traceLog("power watchdog degraded commit generation=%llu inputReady=%d",
                         currentPowerGeneration(), inputReady ? 1 : 0);
                stopPowerResumeWatchdog();
                ipc_emit("power.resume-degraded", {
                    {"generation", currentPowerGeneration()},
                    {"reason", "frontend_commit_timeout"},
                    {"inputReady", inputReady}
                });
                ipc_emit("power.resumed", {
                    {"generation", currentPowerGeneration()},
                    {"inputReady", inputReady},
                    {"degraded", true}
                });
                return 0;
            }
            stopPowerResumeWatchdog();
            return 0;
        }
        if (w == WEBVIEW_RENDER_READY_TIMER_ID) {
            KillTimer(g_hwnd, WEBVIEW_RENDER_READY_TIMER_ID);
            if (!g_webviewReady && g_webviewNavigationReady)
                probeWebViewRenderState(g_webviewNavigationGeneration, g_webviewCompletedNavigationId);
            return 0;
        }
        if (w == WEBVIEW_POST_SHOW_NUDGE_TIMER_ID) {
            KillTimer(g_hwnd, WEBVIEW_POST_SHOW_NUDGE_TIMER_ID);
            if (g_webviewReady && IsWindowVisible(g_hwnd) && !IsIconic(g_hwnd)) {
                // Normal show/restore: refresh bounds and focus only. A hard
                // controller visibility reset belongs to power/renderer
                // recovery and can flash the transparent host window.
                nudgeWebViewAfterResume(false);
                g_webviewNeedsShowNudge = false;
                appendWebViewDiagnostic({
                    {"event", "post-show-nudge"},
                    {"generation", g_webviewGeneration.load(std::memory_order_acquire)},
                    {"navigationId", std::to_string(g_webviewCompletedNavigationId)}
                });
            }
            return 0;
        }
        if (w == WEBVIEW_RECOVERY_ACTION_TIMER_ID) {
            executeWebViewRecoveryAction();
            return 0;
        }
        if (w == WEBVIEW_RECOVERY_TIMER_ID) {
            if (g_webviewRecoveryInProgress) failWebViewRecovery("recovery timeout");
            return 0;
        }
        if (w == SUMMON_FOCUS_TIMER_ID) {
            if (!g_focusSession.active || g_focusSession.returning ||
                !IsWindowVisible(g_hwnd)) {
                KillTimer(g_hwnd, SUMMON_FOCUS_TIMER_ID);
                g_summonFocusRetries = 0;
                g_summonAltTried = false;
            } else {
                ++g_summonFocusRetries;
                const bool useAlt = !g_summonAltTried && g_summonFocusRetries >= 2;
                if (useAlt) g_summonAltTried = true;
                if (refocusWebView(useAlt) || g_summonFocusRetries >= 6) {
                    KillTimer(g_hwnd, SUMMON_FOCUS_TIMER_ID);
                    g_summonFocusRetries = 0;
                    g_summonAltTried = false;
                }
            }
        }
        if (w == RETURN_GAME_FOCUS_TIMER_ID) {
            const ULONGLONG now = GetTickCount64();
            const bool userMovedElsewhere = g_focusSession.returnStarted &&
                now - g_focusSession.returnStarted >= 900 && !focusForegroundIsTransientOrShell();
            if (userMovedElsewhere)
                focusClearSession();
            else if (refocusPreviousWindow() || now >= g_focusSession.returnDeadline)
                focusClearSession();
        }
        if (w == FOCUS_DISPLAY_TIMER_ID) {
            KillTimer(g_hwnd, FOCUS_DISPLAY_TIMER_ID);
            focusReflowMainWindow();
        }
        if (w == MEM_TRAY_TIMER_ID) {
            refreshMemTrayIcon();
        }
        if (w == SG_RESLEEP_TIMER_ID) {
            sgTryResleep();
        }
        if (w == SG_OVERHEAT_TIMER_ID) {
            sgTryOverheatSleep();
        }
        if (w == GP_HOLD_TIMER_ID) {
            gamepadEval();
        }
        return 0;
    case WM_SYSCOMMAND:
        // 拦截最小化：工具窗口(WS_EX_TOOLWINDOW)最小化后 DWM 会残留一个缩略图黑块
        // （用户实测「最小化才会出现黑块」）。改为直接隐藏到托盘，不真正最小化。
        if ((w & 0xFFF0) == SC_MINIMIZE && !g_taskbarResident) {
            hideWindowAnimated(h);
            return 0;
        }
        // 任务栏常驻时保留 Windows 原生最小化状态，确保任务栏按钮和
        // HWND 状态一致；非常驻模式才隐藏到托盘。
        break;
    case WM_APP_EXIT:
        beginAsyncExit(h, w);
        return 0;
    case WM_WEBVIEW_PROCESS_FAILED: {
        auto* info = reinterpret_cast<WebViewFailureInfo*>(l);
        if (!info) return 0;
        appendWebViewFailureDiagnostic(*info);
        recoverWebViewProcess(*info);
        delete info;
        return 0;
    }
    case WM_WEBVIEW_RECOVERY_RESTART:
        if (!g_webviewRecoveryInProgress) return 0;
        if (w == 0) {
            failWebViewRecovery("profile isolation failed");
            return 0;
        }
        appendWebViewDiagnostic({{"event", "browser-profile-isolated"}});
        init_webview();
        return 0;
    case WM_APP_EXIT_READY:
        if (!g_exitRequested.load()) return 0;
        if (g_exitCleanupThread) {
            WaitForSingleObject(g_exitCleanupThread, 0);
            CloseHandle(g_exitCleanupThread);
            g_exitCleanupThread = nullptr;
        }
        if (g_acdcNotify) { UnregisterPowerSettingNotification(g_acdcNotify); g_acdcNotify = nullptr; }
        if (g_monitorNotify) { UnregisterPowerSettingNotification(g_monitorNotify); g_monitorNotify = nullptr; }
        if (g_schemeNotify) { UnregisterPowerSettingNotification(g_schemeNotify); g_schemeNotify = nullptr; }
        KillTimer(h, TIMER_ID_ACDC);
        KillTimer(h, SUMMON_FOCUS_TIMER_ID);
        KillTimer(h, RETURN_GAME_FOCUS_TIMER_ID);
        KillTimer(h, FOCUS_DISPLAY_TIMER_ID);
        KillTimer(h, MEM_TRAY_TIMER_ID);
        KillTimer(h, SG_RESLEEP_TIMER_ID);
        KillTimer(h, SG_OVERHEAT_TIMER_ID);
        KillTimer(h, GP_HOLD_TIMER_ID);
        stopPowerResumeWatchdog();
        KillTimer(h, WEBVIEW_RECOVERY_TIMER_ID);
        KillTimer(h, WEBVIEW_RECOVERY_ACTION_TIMER_ID);
        if (g_memTrayIcon) { DestroyIcon(g_memTrayIcon); g_memTrayIcon = nullptr; }
        if (g_tdpDaemonJob) { CloseHandle(g_tdpDaemonJob); g_tdpDaemonJob = nullptr; }
        closeWebViewsForExit();
        DestroyWindow(h);
        return 0;
    case WM_POWER_RESUME_READY:
        if (g_powerLifecycle.load(std::memory_order_acquire) == PowerLifecycle::Resuming) {
            const auto generation = currentPowerGeneration();
            g_resumeReadyTick = GetTickCount64();
            armPowerResumeWatchdog(generation);
            // Sleep wake starts with a light refresh. The bounded probe below
            // escalates only when the renderer does not answer.
            nudgeWebViewAfterResume(false);
            g_resumeProbeGeneration = generation;
            g_resumeProbeAttempts = 0;
            g_resumeProbeInFlight = false;
            g_resumeProbeForcedReset = false;
            schedulePowerResumeProbe(generation, 180);
            ipc_emit("gamepad.restart", {{"generation", generation}});
            ipc_emit("power.resume-ready", {
                {"generation", generation},
                {"hibernateAvailable", sgHibernateAvailable()}
            });
        }
        return 0;
    case WM_POWER_RESUME_COMMIT:
        if (g_powerLifecycle.load(std::memory_order_acquire) == PowerLifecycle::Ready) {
            stopPowerResumeWatchdog();
            ipc_emit("power.resumed", {
                {"generation", currentPowerGeneration()},
                {"inputReady", g_inputReady.load(std::memory_order_acquire)}
            });
        }
        return 0;
    case WM_QUERYENDSESSION:
        // 系统注销/关机前先恢复被冻结游戏；返回 TRUE 允许会话结束继续。
        sgCleanupBeforeExit();
        return TRUE;
    case WM_ENDSESSION:
        if (w) sgCleanupBeforeExit();
        else g_sgCleanupDone = false; // 会话结束被取消，允许后续真正退出再次执行恢复
        return 0;
    case WM_CLOSE:
        saveWindowState();
        ipc_emit("window.closing");
        // 托盘常驻 或 任务栏常驻：关闭=隐藏（托盘/任务栏按钮仍可作入口），不退出进程
        if (g_trayActive || g_taskbarResident) { hideWindowAnimated(h); return 0; }
        beginAsyncExit(h, 0);
        return 0;
    case WM_DESTROY:
        if (g_exitRequested.load()) {
            g_summonQuit = true;
            PostQuitMessage((int)g_exitCode.load(std::memory_order_relaxed));
            return 0;
        }
        // CCD worker 退出（收尾清理）
        g_ccdStop.store(true);
        poolStop();
        // 取消电源通知订阅 + 清防抖定时器
        if (g_acdcNotify) { UnregisterPowerSettingNotification(g_acdcNotify); g_acdcNotify = nullptr; }
        if (g_monitorNotify) { UnregisterPowerSettingNotification(g_monitorNotify); g_monitorNotify = nullptr; }
        if (g_schemeNotify) { UnregisterPowerSettingNotification(g_schemeNotify); g_schemeNotify = nullptr; }
        sgStopWorkThread();
        sgStopResleepObservation();
        KillTimer(h, TIMER_ID_ACDC);
        KillTimer(h, SUMMON_FOCUS_TIMER_ID);
        KillTimer(h, RETURN_GAME_FOCUS_TIMER_ID);
        KillTimer(h, FOCUS_DISPLAY_TIMER_ID);
        KillTimer(h, MEM_TRAY_TIMER_ID);
        KillTimer(h, SG_RESLEEP_TIMER_ID);
        KillTimer(h, SG_OVERHEAT_TIMER_ID);
        KillTimer(h, GP_HOLD_TIMER_ID);
        stopPowerResumeWatchdog();
        if (g_memTrayIcon) { DestroyIcon(g_memTrayIcon); g_memTrayIcon = nullptr; }
        // 退出前恢复被冻结的进程；统一清理函数保证 app.exit/会话结束/窗口销毁只执行一次。
        stopTdpDaemonForExit();
        stopTopMonitorForExit();
        sgCleanupBeforeExit();
        closeWebViewsForExit();
        // Cleanup watchers
        for (auto& [id, w] : g_watchers) {
            if (stopWatcher(w, 1000))
                delete w;
        }
        g_watchers.clear();
        if (g_trayActive) Shell_NotifyIconW(NIM_DELETE, &g_nid);
        // 按键呼出：已改为 Raw Input 事件驱动（gamepadRegisterRawInput + WndProc WM_INPUT），
        // 无轮询线程；g_summonQuit 仍供掌机自动关闭后台线程 autoCloseThread 使用
        g_summonQuit = true;
        // 掌机前端自动关闭：通知后台轮询线程退出并回收
        if (g_autoCloseThread) {
            WaitForSingleObject(g_autoCloseThread, 3000);
            CloseHandle(g_autoCloseThread);
            g_autoCloseThread = nullptr;
        }
        if (g_tdpDaemonJob) {
            CloseHandle(g_tdpDaemonJob);
            g_tdpDaemonJob = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    case WM_HOTKEY:
        ipc_emit("hotkey.triggered", {{"id", (int)w}});
        return 0;
    case WM_FILE_CHANGED: {
        auto* data = reinterpret_cast<json*>(l);
        ipc_emit("watcher.changed", *data);
        delete data;
        return 0;
    }
    case WM_IPC_RESULT: {
        // worker 线程池完成的 IPC 响应，在 UI 线程回传给 WebView2
        auto* result = reinterpret_cast<IpcResultEnvelope*>(l);
        if (result && g_view &&
            result->generation == g_webviewGeneration.load(std::memory_order_acquire))
            g_view->PostWebMessageAsJson(U2W(result->response.dump()).c_str());
        delete result;
        return 0;
    }
    case WM_UPDATE_PROGRESS: {
        auto* progress = reinterpret_cast<json*>(l);
        if (progress) {
            ipc_emit("update.progress", *progress);
            delete progress;
        }
        return 0;
    }
    case WM_GAMEPAD_TDP_DELTA:
        // 手柄后台线程 → UI 线程；PostWebMessageAsJson 仅允许 UI 线程调用
        if (!g_inputReady.load(std::memory_order_acquire) || !hardwareWriteAllowed()) return 0;
        ipc_emit("gamepad.tdp-delta", json{{"delta", (int)w}});
        return 0;
    case WM_GAMEPAD_BRIGHTNESS:
        if (!g_inputReady.load(std::memory_order_acquire) || !hardwareWriteAllowed()) return 0;
        nativeApplyBrightness((int)w);
        return 0;
    case WM_INPUT: {
        // 手柄订阅唤醒：OS 仅在手柄状态变化时投递；RIDEV_INPUTSINK 使隐藏/托盘态也能收到。
        // 用 XInputGetState 读权威按键态并处理全局快捷键（空闲无 WM_INPUT = 0 CPU）。
        UINT dwSize = 0;
        GetRawInputData((HRAWINPUT)l, RID_INPUT, nullptr, &dwSize, sizeof(RAWINPUTHEADER));
        if (dwSize > 0 && dwSize <= sizeof(g_riBuf)) {
            if (GetRawInputData((HRAWINPUT)l, RID_INPUT, g_riBuf, &dwSize, sizeof(RAWINPUTHEADER)) == dwSize) {
                RAWINPUT* raw = (RAWINPUT*)g_riBuf;
                if (raw->header.dwType == RIM_TYPEHID) {
                    sgMarkInputActivity();
                    gamepadEval();
                } else if (raw->header.dwType == RIM_TYPEKEYBOARD || raw->header.dwType == RIM_TYPEMOUSE) {
                    sgMarkInputActivity();
                }
            }
        }
        return 0;
    }
    case WM_INPUT_DEVICE_CHANGE:
        // 手柄插拔：刷新连接态（前端用浏览器 Gamepad API 感知连接，无需 native 转发）
        gamepadEval();
        return 0;
    case WM_DEVICECHANGE:
        return TRUE;
    case WM_KEYDOWN:
        // F12 toggles DevTools (production 也允许，便于跨机排查黑屏)
        if (w == VK_F12) {
            if (g_view) g_view->OpenDevToolsWindow();
            return 0;
        }
        break;
    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)w;
        UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        json files = json::array();
        wchar_t path[MAX_PATH];
        for (UINT i = 0; i < count; i++) {
            DragQueryFileW(hDrop, i, path, MAX_PATH);
            files.push_back(W2U(path));
        }
        POINT pt;
        DragQueryPoint(hDrop, &pt);
        DragFinish(hDrop);
        ipc_emit("window.fileDrop", {{"files", files}, {"x", pt.x}, {"y", pt.y}});
        return 0;
    }

    case WM_NCLBUTTONDOWN:
        // 全屏吸附模式：彻底禁止通过非客户区（标题栏/边框）发起拖动或缩放。
        // CSS app-region:drag 在 setupWebView 已禁用，此处兜底拦截 IPC/window.startDrag 等任意来源。
        if (g_fullHeight) return 0;
        break;

    case WM_NCHITTEST:
        if (g_frameless && !IsZoomed(h)) {
            if (g_fullHeight) {
                // 全屏吸附模式：禁止拖动与缩放，将命中区域始终判定为客户区
                return HTCLIENT;
            }
            POINT pt{ GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            RECT rc;
            GetWindowRect(h, &rc);

            UINT dpi = GetDpiForWindow(h);
            int frameX = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            int frameY = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            int minFrame = MulDiv(6, dpi, 96);
            if (frameX < minFrame) frameX = minFrame;
            if (frameY < minFrame) frameY = minFrame;

            bool left = pt.x >= rc.left && pt.x < rc.left + frameX;
            bool right = pt.x < rc.right && pt.x >= rc.right - frameX;
            bool top = pt.y >= rc.top && pt.y < rc.top + frameY;
            bool bottom = pt.y < rc.bottom && pt.y >= rc.bottom - frameY;

            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
        }
        break;

    case WM_POWERBROADCAST:
        // 订阅式（非轮询）电源插拔通知。注册 GUID_ACDC_POWER_SOURCE 后，
        // 仅当系统电源来源变化时 OS 才推送本消息 → 零后台损耗。
        if (w == PBT_POWERSETTINGCHANGE && l) {
            auto* pbs = reinterpret_cast<POWERBROADCAST_SETTING*>(l);
            if (IsEqualGUID(pbs->PowerSetting, YM_GUID_ACDC_POWER_SOURCE) &&
                pbs->DataLength >= sizeof(DWORD)) {
                DWORD cond = *reinterpret_cast<DWORD*>(pbs->Data); // SYSTEM_POWER_CONDITION
                int ac = (cond == 0 /*PoAc*/) ? 1 : 0;            // 0=插电 1/2=离电
                if (g_lastAcState < 0) {
                    // 注册后系统会立即回调一次当前状态：仅记录，不算切换、不刷新
                    g_lastAcState = ac;
                } else if (ac != g_lastAcState) {
                    g_lastAcState = ac;
                    // 视频背景专用轻量事件：即时暂停/恢复；不触发 CPU/TDP 等重型链路。
                    ipc_emit("power.sourceChanged", {{"ac", ac == 1}});
                    // ── 熔断：5s 滑动窗口内真实切换 >10 次 → 请求完整退出，避免系统卡死且不绕过 Sleep Guard 恢复。──
                    DWORD now = GetTickCount();
                    g_acSwitchTicks.push_back(now);
                    g_acSwitchTicks.erase(
                        std::remove_if(g_acSwitchTicks.begin(), g_acSwitchTicks.end(),
                            [now](DWORD t){ return (now - t) > ACDC_BURST_MS; }),
                        g_acSwitchTicks.end());
                    if ((int)g_acSwitchTicks.size() > ACDC_BURST_LIMIT) {
                        KillTimer(h, TIMER_ID_ACDC);
                        PostMessageW(h, WM_APP_EXIT, 0, 0);
                        return 0;
                    }
                    // ── 尾防抖：连续切换只在最后一次之后 5s 触发一次刷新 ──
                    // SetTimer 同 id 会重置倒计时 → 天然实现「顺延到最后一次重新计时」。
                    SetTimer(h, TIMER_ID_ACDC, ACDC_DEBOUNCE_MS,
                        [](HWND hh, UINT, UINT_PTR id, DWORD) {
                            KillTimer(hh, id);
                            ipc_emit("power.acChanged", {{"ac", g_lastAcState == 1}});
                        });
                }
            }
        else if (IsEqualGUID(pbs->PowerSetting, YM_GUID_ACTIVE_POWERSCHEME)) {
                // Windows 活动电源方案变化广播：仅转发事件，方案是否恢复由前端 CPU guard 决定。
                ipc_emit("power.schemeChanged", {});
            }
            else if (IsEqualGUID(pbs->PowerSetting, YM_GUID_MONITOR_POWER_ON) &&
                     pbs->DataLength >= sizeof(DWORD)) {
                DWORD on = *reinterpret_cast<DWORD*>(pbs->Data); // 0=显示器关 1=开
                // 显示器状态不等价于系统睡眠。普通超时关屏不得冻结进程或降低 TDP。
                g_monitorOn = on != 0;
            }
        }
        // ── 睡眠守护：PBT_APM* 同样走 WM_POWERBROADCAST（窗口程序自动送达，无需额外注册）──
        else if (w == PBT_APMSUSPEND) {
            {
                focusClearSession();
                KillTimer(g_hwnd, FOCUS_DISPLAY_TIMER_ID);
                const auto generation = g_powerGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
                g_powerLifecycle.store(PowerLifecycle::Suspending, std::memory_order_release);
                g_resumeReadyGeneration.store(0, std::memory_order_release);
                g_lastResumeNotifyTick = 0;
                closeHardwareWriteGate("suspend");
                stopPowerResumeWatchdog();
                g_inputReady.store(false, std::memory_order_release);
                gamepadResetNativeState(true);
                ipc_emit("power.suspending", {
                    {"generation", generation},
                    {"hibernateAvailable", sgHibernateAvailable()}
                });
                sgMarkSleepTrigger();
                sgQueueWork(SgWork::Suspend, generation);
            }
        }
        else if (w == PBT_APMRESUMEAUTOMATIC) {
            // 自动唤醒候选：恢复本周期资源，并进入最多30秒的输入静默观察。
            handlePowerResumeNotification(SgWork::WakeAutomatic, "resume_auto");
        }
        else if (w == PBT_APMRESUMESUSPEND) {
            // S3 确定性用户唤醒：直接恢复并取消重睡观察。
            handlePowerResumeNotification(SgWork::WakeSuspend, "resume_suspend");
        }
        return TRUE;

    case WM_TRAYICON:
        switch (LOWORD(l)) {
        case WM_LBUTTONUP:
            // 单击托盘图标：窗口隐藏则显示，已显示则隐藏到托盘（toggle）
            if (!IsWindowVisible(g_hwnd)) {
                showWindowAnimated(g_hwnd, SW_SHOW);
            } else
                hideWindowAnimated(g_hwnd);
            ipc_emit("tray.click");
            break;
        case WM_LBUTTONDBLCLK:
            // 双击托盘图标：总是彻底隐藏主窗口到托盘，并刷新任务栏状态。
            // 之前这里是直接 showWindowAnimated(SW_SHOW)，导致双击后任务栏出现黑框窗口；
            // 后改为 toggle 仍有用户反馈黑块残留。双击托盘语义明确为"完全退到后台"，
            // 故不再呼出，只隐藏；呼出由单击托盘(LBUTTONUP)负责。
            hideWindowAnimated(g_hwnd);
            applyResidentMode(); // 强制重新同步任务栏按钮/托盘状态，消除残留黑框/按钮
            ipc_emit("tray.doubleClick");
            break;
        case WM_RBUTTONUP:
            ipc_emit("tray.rightClick");
            showTrayMenu();
            break;
        }
        return 0;

    case WM_COMMAND: {
        int id = (int)LOWORD(w);
        if (id == ID_TRAY_SHOW) {
            if (!IsWindowVisible(g_hwnd)) {
                showWindowAnimated(g_hwnd, SW_SHOW);
            } else
                hideWindowAnimated(g_hwnd);
        } else if (id == ID_TRAY_MIN) {
            hideWindowAnimated(g_hwnd);
        } else if (id == ID_TRAY_EXIT) {
            // 彻底退出：先移除托盘图标，再销毁窗口（WM_DESTROY 会做收尾并 PostQuitMessage）
            PostMessageW(g_hwnd, WM_APP_EXIT, 0, 0);
        }
        return 0;
    }

    // ── Frameless window handling ──
    case WM_NCCALCSIZE:
        if (g_frameless && w) {
            auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(l);
            // When maximized, respect taskbar area
            WINDOWPLACEMENT wpl{sizeof(wpl)};
            GetWindowPlacement(h, &wpl);
            if (wpl.showCmd == SW_MAXIMIZE) {
                HMONITOR mon = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi{sizeof(mi)};
                GetMonitorInfoW(mon, &mi);
                p->rgrc[0] = mi.rcWork;
            }
            return 0;
        }
        break;

    case WM_GETMINMAXINFO: {
        auto mw = g_cfg.value("/window/minWidth"_json_pointer, 200);
        auto mh = g_cfg.value("/window/minHeight"_json_pointer, 150);
        auto* mmi = reinterpret_cast<MINMAXINFO*>(l);
        if (!g_frameless) {
            RECT minRect{0, 0, mw, mh};
            DWORD style = static_cast<DWORD>(GetWindowLongW(h, GWL_STYLE));
            DWORD exStyle = static_cast<DWORD>(GetWindowLongW(h, GWL_EXSTYLE));
            AdjustWindowRectExForDpi(&minRect, style, FALSE, exStyle, GetDpiForWindow(h));
            mw = minRect.right - minRect.left;
            mh = minRect.bottom - minRect.top;
        }
        mmi->ptMinTrackSize = { mw, mh };
        if (g_frameless) {
            HMONITOR mon = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi{sizeof(mi)};
            if (GetMonitorInfoW(mon, &mi)) {
                mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
                mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
                mmi->ptMaxTrackSize = mmi->ptMaxSize;
            }
        }
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

// ================================================================
//  Entry point
// ================================================================

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int ns) {
    g_hinst = hi;
    wchar_t traceEnv[64] = {};
    if (GetEnvironmentVariableW(L"YEMAN_TRACE", traceEnv, _countof(traceEnv)) &&
        _wcsicmp(traceEnv, L"debug") == 0) {
        traceInit();
        traceLog("BOOT enter");
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    EnableMouseInPointer(TRUE);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput splashGdiplusInput;
    if (Gdiplus::GdiplusStartup(&g_splashGdiplusToken, &splashGdiplusInput, nullptr) != Gdiplus::Ok)
        g_splashGdiplusToken = 0;
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
    g_showExistingInstanceMsg = RegisterWindowMessageW(L"YeManCC.ShowExistingInstance.v1");

    // Parse --dev <url>
    int argc;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"--dev") == 0 && i + 1 < argc) { g_devUrl = argv[i + 1]; }
        else if (wcscmp(argv[i], L"--minimized") == 0) { g_startMinimized = true; }
        else if (wcscmp(argv[i], L"--update-handshake") == 0 && i + 1 < argc) {
            g_updateHandshakePath = argv[++i];
        }
        else if (wcscmp(argv[i], L"--update-handshake-token") == 0 && i + 1 < argc) {
            g_updateHandshakeToken = W2U(argv[++i]);
        }
    }
    LocalFree(argv);

    // Load config
    g_cfg = loadConfig();
    traceLog("BOOT config-loaded");
#ifdef SINGLE_EXE
    loadPak();
#endif
    auto winCfg    = g_cfg.value("window", json::object());
    auto title     = U2W(winCfg.value("title", std::string{"\xe5\xbc\xba\xe5\xbc\xba"}));

    // Single instance lock
    bool singleInstance = winCfg.value("singleInstance", true);
    HANDLE hMutex = nullptr;
    if (singleInstance) {
        auto mutexName = U2W("QQ_" + winCfg.value("title", std::string{"app"}));
        hMutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND existing = FindWindowW(L"QQ", title.c_str());
            if (existing) {
                DWORD existingPid = 0;
                GetWindowThreadProcessId(existing, &existingPid);
                if (existingPid) AllowSetForegroundWindow(existingPid);
                if (!g_showExistingInstanceMsg ||
                    !PostMessageW(existing, g_showExistingInstanceMsg, 0, 0)) {
                    // Last-resort compatibility path for an older running build.
                    ShowWindow(existing, IsIconic(existing) ? SW_RESTORE : SW_SHOW);
                    SetForegroundWindow(existing);
                }
            }
            if (hMutex) CloseHandle(hMutex);
            shutdownSplashGraphics();
            CoUninitialize();
            return 0;
        }
    }
    // Recoverable runtime markers can outlive a forced process termination.
    // Clear them after acquiring the single-instance lock so the frontend
    // cannot treat stale monitor/float files as a live session.
    cleanupExitArtifacts();
    // Repair the first updater layout before startup workers read PowerControl.
    migrate_legacy_power_control_dir();
    int  width     = winCfg.value("width", 1024);
    int  height    = winCfg.value("height", 768);
    g_frameless    = winCfg.value("frameless", false);
    g_rounded      = winCfg.value("rounded", true);
    g_saveWindowState = winCfg.value("saveWindowState", true);
    g_fullHeight   = winCfg.value("fullHeight", false);
    g_baseW        = width;
    g_baseH        = height;
    g_allowWebviewPermissions = winCfg.value("allowWebviewPermissions", false);
    bool initiallyVisible = winCfg.value("visible", true);
    // Default ON (like "rounded"): hold the window hidden until WebView2 paints its
    // first frame, eliminating the blank/native-framed startup flash. Opt out with false.
    bool showWhenReady = winCfg.value("showWhenReady", true);
    bool shouldCenter = winCfg.value("center", false);
    g_effectType   = parseEffectType(winCfg.value("effect", std::string{"none"}));

    // Security: load permissions
    if (g_cfg.contains("permissions") && g_cfg["permissions"].is_object()) {
        for (auto& [k, v] : g_cfg["permissions"].items()) {
            g_permissions[k] = v.get<bool>();
        }
    }

    COLORREF bgClr = currentWindowBackgroundColor();

    // Window class
    initAppIcons(hi);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hi;
    wc.lpszClassName  = L"QQ";
    wc.hCursor        = LoadCursorW(nullptr, IDC_ARROW);
    // 无边框透明基线：窗口类不提供实色背景刷，局部可见底色全部交给前端
    // 导航/卡片/控件分块绘制；否则 GDI 会先把整个窗口铺成深色，遮住桌面。
    wc.hIcon          = g_appIconLarge ? g_appIconLarge : LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm        = g_appIconSmall ? g_appIconSmall : wc.hIcon;
    RegisterClassExW(&wc);

    // Frameless mode must not be created as WS_OVERLAPPEDWINDOW.  Relying on
    // WM_NCCALCSIZE to hide that style is DPI/Windows-version sensitive: the
    // native caption buttons can reappear and steal the top edge from the
    // WebView, which then makes the top monitor look vertically misaligned.
    //
    // Keep a thick frame only for the ordinary frameless/resizable mode; the
    // fullHeight layout is intentionally a fixed, program-positioned window.
    // Its DPI/display changes are handled by applyFullHeightLayout(), so it
    // must not expose a manual resize or drag border.
    // 默认不常驻任务栏：窗口以 WS_EX_APPWINDOW 创建，再由 applyResidentMode() 调
    // ITaskbarList::DeleteTab 隐藏任务栏按钮（等价于“不存在于任务栏”）。
    // 注意：绝不能用 WS_EX_TOOLWINDOW 隐藏任务栏按钮——该样式会让 DWM 忽略
    // SystemBackdrop 属性，磨砂玻璃整片失效（灰块根因）。任务栏常驻开启时由
    // applyResidentMode() AddTab 显示按钮；默认仅靠托盘入口。
    DWORD style = g_frameless
        ? (g_fullHeight
            ? (WS_POPUP | WS_CLIPCHILDREN)
            : (WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN))
        : (WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN);

    DWORD exStyle = WS_EX_APPWINDOW;
    int windowWidth = width;
    int windowHeight = height;
    if (!g_frameless) {
        RECT desiredClient{0, 0, width, height};
        AdjustWindowRectExForDpi(&desiredClient, style, FALSE, exStyle, GetDpiForSystem());
        windowWidth = desiredClient.right - desiredClient.left;
        windowHeight = desiredClient.bottom - desiredClient.top;
    }
    g_hwnd = CreateWindowExW(exStyle, L"QQ", title.c_str(),
        style,
        CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight,
        nullptr, nullptr, hi, nullptr);
    traceLog("BOOT window-created ok=%d", g_hwnd != nullptr ? 1 : 0);
    if (g_hwnd) {
        if (g_appIconLarge) SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)g_appIconLarge);
        if (g_appIconSmall) SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_appIconSmall);

        // 透明基线：普通 WebView2 客户区 + CSS 分块底板。
        // WebView2 DefaultBackgroundColor alpha=0（透明根）+ WS_EX_LAYERED
        // 是 WebView2 官方逐像素透明组合：空白像素透桌面、底板按 rgba
        // 半透明、文字/图标按自身 alpha 合成。
        // 整窗 alpha=255：整窗不淡化（不透明最小值）。透明观感完全由
        // 前端分块底板的 rgba 逐像素提供（空白区 alpha=0 透桌面、
        // 底板近实体 97-98%）。鼠标命中由前端 zoom 缩放修正。
        // 合成路径：GPU 合成器（已去除 --disable-gpu-compositing），
        // 每秒重绘（HWiNFO 面板）不再阻塞 UI 线程。
        if (g_frameless) {
            LONG_PTR exStyle = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
            SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
            SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);
        }
    }
    enableWindowTransitions(g_hwnd);

    // 全屏高度 + 右侧吸附模式：忽略历史窗口状态，按工作区重新计算布局
    if (g_fullHeight) {
        applyFullHeightLayout();
    } else {
    // Window state persistence — restore previous position/size
    g_stateFile = L"C:\\SOFT\\YeMan\\PowerControl\\window-state.json";
    bool restoredState = false;
    auto prevState = loadWindowState();
    if (!prevState.empty()) {
        int sx = prevState.value("x", 0);
        int sy = prevState.value("y", 0);
        int sw = prevState.value("w", width);
        int sh = prevState.value("h", height);
        SetWindowPos(g_hwnd, nullptr, sx, sy, sw, sh, SWP_NOZORDER);
        if (prevState.value("maximized", false))
            ns = SW_MAXIMIZE;
        restoredState = true;
    }
    if (!restoredState && shouldCenter) {
        RECT wr;
        GetWindowRect(g_hwnd, &wr);
        int ww = wr.right - wr.left;
        int wh = wr.bottom - wr.top;
        HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{sizeof(mi)};
        if (GetMonitorInfoW(mon, &mi)) {
            int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - ww) / 2;
            int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - wh) / 2;
            SetWindowPos(g_hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
    }
    }

    // 启动参数 --minimized：开机最小化启动。
    // 窗口永远只以托盘图标常驻，不显示主窗口，UI 在后台加载。
    if (g_startMinimized) {
        ns = SW_HIDE;
        initiallyVisible = false;
    }

    // DWM attributes for frameless
    if (g_frameless) {
        g_bgClr = bgClr;
        applyFramelessDwmAttrs();
        g_bgBrush = CreateSolidBrush(bgClr);
    }
    applyNativeTheme();

    // 只在用户正常打开窗口且启用首帧等待时显示。--minimized 开机启动保持完全安静。
    if (initiallyVisible && showWhenReady) showSplash(hi, width, height);

    // Enable file drag-drop
    DragAcceptFiles(g_hwnd, TRUE);

    // 手柄呼出：注册 Raw Input 订阅（后台/托盘态也能收到 WM_INPUT，零轮询占用）
    gamepadRegisterRawInput();

    // 订阅 AC/DC 电源来源变化（推送式，零轮询）。系统会在注册后立即回调一次当前状态。
    g_acdcNotify = RegisterPowerSettingNotification(
        g_hwnd, &YM_GUID_ACDC_POWER_SOURCE, DEVICE_NOTIFY_WINDOW_HANDLE);
    // 显示器开关通知：S0 现代待机没有 S3，不会发 PBT_APMSUSPEND，改用显示器关闭/亮起
    // 作为「进入睡眠 / 唤醒」信号（进 DRIPS 前必关显示器）。S3 桌面机两条路径都覆盖。
    g_monitorNotify = RegisterPowerSettingNotification(
        g_hwnd, &YM_GUID_MONITOR_POWER_ON, DEVICE_NOTIFY_WINDOW_HANDLE);
    g_schemeNotify = RegisterPowerSettingNotification(
        g_hwnd, &YM_GUID_ACTIVE_POWERSCHEME, DEVICE_NOTIFY_WINDOW_HANDLE);

    // ── 睡眠守护：加载持久化开关 + 孤儿恢复（上次崩溃残留的冻结进程）──
    sgInitNt();
    sgStartWorkThread();
    sgLoadConfig();
    sgLoadResleepCooldown();
    {
        DWORD sessionId = 0;
        g_sgSessionValid = ProcessIdToSessionId(GetCurrentProcessId(), &sessionId) != FALSE;
        g_sgSessionId = g_sgSessionValid ? sessionId : 0;
        if (!g_sgSessionValid) {
            OutputDebugStringW(L"YeMan Sleep Guard: unable to resolve process session; fail-closed.\n");
        }
        std::string en = sgReadFile(SG_DIR + L"\\Enable.txt");
        g_guardEnabled = (en == "1");
        sgUpdateOverheatTimer();
        // 启动只恢复本程序自己留下的两类身份标记：睡眠冻结和手动暂停。
        // 两个目录分离，任何一类都不会按名称/内存扫描其他进程。
        // 恢复标记只影响本程序自己留下的 PID 文件。放到独立线程，让
        // WebView2 冷启动与磁盘/进程恢复并行；退出前统一 join。
        traceLog("BOOT startup-resume-dispatch");
        startStartupResumeThread();
    }

    // ── 按键呼出：加载开关（后台手柄 LB+RB 0.5 秒呼出程序）──
    summonLoad();

    // ── 掌机前端自动关闭：加载开关 + 进程名列表（持久化于 config/autoclose.json）──
    autoCloseLoad();

    // ── 更新加速器：不会随程序自动启动，仅在前端手动触发 ──

    // Register all commands
    reg_display();
    reg_window();
    reg_dialog();
    reg_fs();
    reg_background();
    reg_music();
    reg_clipboard();
    reg_shell_app();
    reg_tray();
    // 应用「任务栏常驻」偏好（默认不常驻）：默认仅托盘、无任务栏按钮。
    // 前端 App.vue 启动期会按 C:\SOFT\YeMan\PowerControl\tray_resident.json 再同步一次，
    // 但此处先保证即使前端未就绪窗口也可经托盘唤回。
    applyResidentMode();
    // 内存变色托盘图标：30 s 定时刷新，仅托盘模式生效（resident 模式无托盘则跳过）
    SetTimer(g_hwnd, MEM_TRAY_TIMER_ID, 30000, nullptr);
    reg_env();
    reg_hotkey();
    reg_notification();
    reg_menu();
    reg_http();
    reg_os();
    reg_watcher();
    reg_state();
    reg_devtools();
    reg_registry();
    reg_protocol();
    reg_log();
    reg_updater();
    migrate_legacy_support_page();
    reg_multiwindow();
    reg_extras();

    // ── 手柄呼出：已改为 Raw Input 事件驱动（gamepadRegisterRawInput + WndProc WM_INPUT），无需轮询线程 ──

    // ── 掌机前端自动关闭：启动后台轮询线程（5 秒一次，即使隐藏到托盘也持续工作）──
    g_autoCloseThread = CreateThread(nullptr, 0, autoCloseThread, nullptr, 0, nullptr);

    // Show immediately by default. With "showWhenReady" (default on), hold the window
    // hidden until WebView2 paints its first frame (revealed in NavigationCompleted),
    // so the user never sees a blank/native-framed flash during WebView2 init.
    g_firstShowCmd = ns;
    if (initiallyVisible) {
        if (showWhenReady) {
            g_deferFirstShow = true;
        } else {
            showWindowAnimated(g_hwnd, ns, true);
            UpdateWindow(g_hwnd);
        }
    }

    // ── IPC 异步方案旋钮（环境变量；无变量时用编译默认值）──
    {
        wchar_t eb[64] = {};
        if (GetEnvironmentVariableW(L"YEMAN_ASYNC", eb, 32)) {
            int v = _wtoi(eb); if (v < 0) v = 0; if (v > 2) v = 2;
            // 模式1(仅 shell.run 异步)对浮动调度不安全：每轮 fs 读/写与 shell.hidden 仍会
            // 在 UI 线程同步执行，导致鼠标旁 IDC_APPSTARTING 转圈。因此历史遗留的 1 一律按
            // 2(全异步)处理，避免环境变量悄悄降级回不安全模式。仅 0(强制全同步，调试用)
            // 与 2(强制全异步) 生效。
            if (v == 1) v = 2;
            g_asyncMode = v;
        }
        if (GetEnvironmentVariableW(L"YEMAN_POOL", eb, 32)) {
            int v = _wtoi(eb); if (v < 1) v = 1; if (v > 16) v = 16;
            g_poolSize = v;
        }
        if (GetEnvironmentVariableW(L"YEMAN_TRACE", eb, 64) &&
            _wcsicmp(eb, L"debug") == 0) {
            traceInit();
            traceLog("BOOT async=%d pool=%d", g_asyncMode, g_poolSize);
            CreateThread(nullptr, 0, freezeMonitorThread, nullptr, 0, nullptr);
        }
    }

    traceLog("BOOT init-webview-call");
    init_webview();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 兜底：即使通过 WM_QUIT 直接离开消息循环，也确保冻结进程被解除。
    shutdownSplashGraphics();
    joinStartupResumeThread();
    sgStopWorkThread();
    poolStop();
    g_summonQuit = true;
    if (g_autoCloseThread) {
        WaitForSingleObject(g_autoCloseThread, 3000);
        CloseHandle(g_autoCloseThread);
        g_autoCloseThread = nullptr;
    }
            stopTdpDaemonForExit();
            stopTopMonitorForExit();
            sgCleanupBeforeExit();
    closeWebViewsForExit();
    if (hMutex) CloseHandle(hMutex);
    CoUninitialize();
    return (int)msg.wParam;
}
