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
#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shobjidl.h>
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
#include <limits>
#include <cstring>
#include <cstdlib>
#include <dwmapi.h>
#include <windowsx.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <cctype>
#include <mutex>
#include <atomic>
#include <cstdio>
#include <thread>
#include <deque>
#include <condition_variable>
#include <unordered_set>
#include <cstdarg>
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winhttp.lib")
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")

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
        return scheme == "http:" || scheme == "https:" || scheme == "mailto:" || scheme == "file:";
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

// ================================================================
//  Global state
// ================================================================

static HWND                              g_hwnd;
static ComPtr<ICoreWebView2Environment>  g_env;
static ComPtr<ICoreWebView2Controller>   g_ctrl;
static ComPtr<ICoreWebView2>             g_view;
static std::wstring                      g_devUrl;
static bool                              g_webviewReady = false;

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
static bool g_tdpShortcut = false;    // Start + 上/下 快捷调节 TDP（前端处理，持久化在 native）
static bool g_fpsShortcut = false;    // Start + 左/右 快捷调节 RTSS 锁帧（前端处理，持久化在 native）
static bool g_killGame = false;       // 选择(Back) + B 长按 0.5s → 结束当前游戏（执行 KiLL-EXE.bat）
static bool g_openKeyboard = false;   // 选择(Back) + X 长按 0.5s → 呼出 Windows 触摸键盘（TabTip.exe）
static bool g_summonQuit    = false;  // 退出时通知后台手柄轮询线程退出
static HANDLE g_summonThread = nullptr;

// ── 掌机前端自动关闭（后台轮询 5 秒，温和关闭 = 发 WM_CLOSE）──
static std::atomic<bool> g_autoCloseEnabled{false};   // 总开关（原子读写，跨线程）
static std::vector<std::string> g_autoCloseProcs;     // 目标进程名列表（可带/不带 .exe，支持 * 前缀）
static std::mutex g_autoCloseMx;                       // 保护 procs 列表（前端 set 与后台线程读）
static HANDLE g_autoCloseThread = nullptr;
static int  g_baseW        = 580;    // 设计基准宽（缩放比例分子）
static int  g_baseH        = 780;    // 设计基准高（缩放比例分母）
static bool g_resizing     = false;   // WM_ENTERSIZEMOVE / WM_EXITSIZEMOVE guard
static bool g_allowWebviewPermissions = false;

// 全屏高度 + 右侧吸附布局：
// 窗口高度 = 工作区高度（上下贴合全屏），宽度按设计基准比例缩放，并吸附屏幕右侧。
static void applyFullHeightLayout() {
    if (!g_hwnd) return;
    HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(mon, &mi)) return;
    int waX = mi.rcWork.left;
    int waY = mi.rcWork.top;
    int waW = mi.rcWork.right - mi.rcWork.left;
    int waH = mi.rcWork.bottom - mi.rcWork.top;
    if (g_baseH <= 0) return;
    double R = (double)g_baseW / (double)g_baseH; // 设计基准宽高比
    int targetH = waH;                            // 上下贴合全屏
    int targetW = (int)round((double)targetH * R);
    if (targetW > waW) {                          // 极窄屏保护：以宽度为准
        targetW = waW;
        targetH = (int)round((double)targetW / R);
    }
    int x = waX + waW - targetW;                  // 吸附右侧
    int y = waY;                                  // 顶部对齐（targetH=waH 即底部贴合）
    SetWindowPos(g_hwnd, nullptr, x, y, targetW, targetH, SWP_NOZORDER);
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
static NOTIFYICONDATAW g_nid = {};
static bool            g_trayActive = false;
static HICON           g_appIconLarge = nullptr;
static HICON           g_appIconSmall = nullptr;

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

// ================================================================
//  AC/DC 电源插拔订阅（推送/零轮询）+ 尾防抖 + 频繁切换熔断
// ================================================================
// GUID_ACDC_POWER_SOURCE = {5D3E9A59-E9D5-4B00-A6BD-FF34FF516548}
// 本地定义（static const GUID 直接分配存储，无需 INITGUID / 额外链接库）。
static const GUID YM_GUID_ACDC_POWER_SOURCE =
    { 0x5d3e9a59, 0xe9d5, 0x4b00, { 0xa6, 0xbd, 0xff, 0x34, 0xff, 0x51, 0x65, 0x48 } };

// GUID_MONITOR_POWER_ON = {02731015-4510-4526-99e6-e5a17ebd1aea}
// 显示器开关通知。现代待机(S0ix)系统没有 S3，OS 不会向应用广播 PBT_APMSUSPEND，
// 但进 DRIPS(低功耗待机)前必定先关显示器 → 用「显示器关闭」作为跨 S0/S3 的「进入睡眠」信号。
static const GUID YM_GUID_MONITOR_POWER_ON =
    { 0x02731015, 0x4510, 0x4526, { 0x99, 0xe6, 0xe5, 0xa1, 0x7e, 0xbd, 0x1a, 0xea } };

static HPOWERNOTIFY       g_acdcNotify   = nullptr; // RegisterPowerSettingNotification 句柄
static HPOWERNOTIFY       g_monitorNotify= nullptr; // 显示器开关通知句柄
static int               g_lastAcState  = -1;      // -1=未初始化 0=离电(DC) 1=插电(AC)
static std::vector<DWORD> g_acSwitchTicks;          // 5 秒滑动窗口内的真实切换时间戳

#define TIMER_ID_ACDC     0xA100  // 尾防抖 SetTimer id（避开已用的 99）
#define ACDC_DEBOUNCE_MS  5000    // 每次变化后延迟 5s 刷新；期间再变化则顺延重新计时
#define ACDC_BURST_MS     5000    // 熔断滑动窗口 = 5s
#define ACDC_BURST_LIMIT  10      // 5s 内 >10 次切换 → 直接退出，防止系统卡死

// ================================================================
//  睡眠守护（Sleep Guard）— 见 docs/sleep-guard-design.md v5
//  入睡前冻结"最大工作集进程"(游戏) + 压 TDP 到 12W；
//  唤醒时由 OS 电源消息判定是否"用户主动唤醒"——是则恢复，否则判定误唤醒、
//  记录日志并立即重睡；5 分钟内反复误唤醒≥3 次则升级 S4 休眠(或关机)。
//  纪律：慢操作(冻结/TDP/恢复)只在电源事件回调里做最简处理，极端情况靠孤儿恢复兜底。
// ================================================================
static const std::wstring SG_DIR = L"C:\\SOFT\\YeMan\\PowerControl\\Sleep";
static const uint64_t     SG_MIN_WS   = 500ULL * 1024 * 1024; // 仅冻结工作集≥500MB的进程(避开系统/小进程)

static bool g_guardEnabled = false;  // 总开关（持久化 Enable.txt）
static bool g_sgInSuspend  = false;  // 本周期是否已冻结游戏、处于"睡眠值守"状态
static DWORD g_sgSessionId = 0;      // 当前会话 ID（仅冻结同会话进程，避开系统/其他用户会话）

// ── 睡眠守护可调参数（持久化于 Sleep\sleepguard.json，前端控制面修改）──
// 机制极简：入睡（显示器关闭 / PBT_APMSUSPEND）→ 冻结最大工作集进程 + 压 TDP；
// 唤醒（显示器亮起 / PBT_APMRESUME*）→ 直接恢复游戏 + 还原 TDP。不做误唤醒判定、不重睡、不强制 S4。
static std::string g_sgMode   = "off"; // 总开关模式：off / custom
static bool g_sgPauseResume   = true;  // 睡眠时暂停游戏 + 唤醒时自动恢复（两者绑定，只一个开关）
static bool g_sgTdpLock       = true;  // 入睡调低 TDP
static int  g_sgSleepTdp      = 12;    // 入睡 TDP(W) 5~30
static bool g_sgTdpLowered    = false; // 本次睡眠是否真的压了 TDP（决定恢复时是否还原）
static bool g_monitorOn       = true;  // 当前显示器状态

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
static std::string sgReadFile(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
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
static void sgLoadConfig() {
    std::string c = sgReadFile(SG_DIR + L"\\sleepguard.json");
    if (!c.empty()) {
        try {
            json j = json::parse(c);
            g_sgPauseResume = j.value("pauseResume", true);
            auto tdp = j.value("sleepTdp", json::object());
            g_sgTdpLock  = tdp.value("mode", std::string("lock")) == "lock";
            g_sgSleepTdp = tdp.value("watts", 12);
            g_sgMode      = j.value("mode", std::string("off"));
        } catch (...) {}
    }
    if (g_sgMode != "off" && g_sgMode != "custom") g_sgMode = "off";
    if (g_sgSleepTdp < 5)    g_sgSleepTdp = 5;
    if (g_sgSleepTdp > 30)   g_sgSleepTdp = 30;
}
static void sgSaveConfig() {
    json j = {
        {"mode", g_sgMode},
        {"pauseResume", g_sgPauseResume},
        {"sleepTdp", {{"mode", g_sgTdpLock ? "lock" : "off"}, {"watts", g_sgSleepTdp}}}
    };
    sgWriteFile(SG_DIR + L"\\sleepguard.json", j.dump(2));
}

// ── 按键呼出（后台手柄呼出）：持久化于 <exe_dir>\config\summon.json ──
static std::wstring summonPath() {
    return exe_dir() + L"\\config\\summon.json";
}
static void summonLoad() {
    std::string c = sgReadFile(summonPath());
    if (!c.empty()) {
        try {
            json j = json::parse(c);
            g_summonEnabled   = j.value("enabled", true);
            g_bDoubleMinimize = j.value("bDoubleMinimize", true);
            g_tdpShortcut     = j.value("tdpShortcut", false);
            g_fpsShortcut     = j.value("fpsShortcut", false);
            g_killGame        = j.value("killGame", false);
            g_openKeyboard     = j.value("openKeyboard", false);
        } catch (...) {}
    }
}
static void summonSave() {
    json j = {
        {"enabled", g_summonEnabled},
        {"bDoubleMinimize", g_bDoubleMinimize},
        {"tdpShortcut", g_tdpShortcut},
        {"fpsShortcut", g_fpsShortcut},
        {"killGame", g_killGame},
        {"openKeyboard", g_openKeyboard}
    };
    std::error_code ec;
    fspath::create_directories(fspath::path(summonPath()).parent_path(), ec);
    sgWriteFile(summonPath(), j.dump(2));
}

// ── 掌机前端自动关闭：配置持久化于 <exe_dir>\config\autoclose.json ──
static std::wstring autoClosePath() {
    return exe_dir() + L"\\config\\autoclose.json";
}
static void autoCloseLoad() {
    std::string c = sgReadFile(autoClosePath());
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
    sgWriteFile(autoClosePath(), j.dump(2));
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
// 将已隐藏/后台的窗口带到前台（绕过前台锁限制，AttachThreadInput 附加到前台线程）
static void bringToFront(HWND hwnd) {
    if (!hwnd) return;
    if (!IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    // 置顶：呼出后位于所有窗口之上（含无边框全屏游戏），且不受“前台锁”限制——
    // 否则后台进程首次 SetForegroundWindow 会被 Windows 静默拒绝，表现为“必须点击一次才弹出来”
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    HWND fg = GetForegroundWindow();
    DWORD fgTid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD myTid = GetCurrentThreadId();
    bool attached = false;
    if (fgTid && fgTid != myTid) attached = AttachThreadInput(myTid, fgTid, TRUE) != 0;
    SetForegroundWindow(hwnd);
    if (attached) AttachThreadInput(myTid, fgTid, FALSE);
}
// 后台手柄轮询线程：检测任意手柄 LB+RB 同时按住满 0.5 秒 → 呼出程序（仅当窗口未在前台时）
static void hideWindowAnimated(HWND hwnd); // 前向声明（本线程要用）
// ── 手柄全局快捷调节：前向声明（定义见 sgRestoreTdp 之后，调用 ipc_emit） ──
static inline int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static void nativeAdjustTdp(int delta);
static void nativeAdjustRtss(int delta);
static void runKillBat();          // 选择 + B → 执行 KiLL-EXE.bat 结束当前游戏
static void openTouchKeyboard();   // 选择 + X → 呼出 Windows 触摸键盘（TabTip.exe）
static void ipc_emit(const std::string& ev, const json& data);
// 后台手柄轮询线程：
//  - 按住 LB+RB 满 0.5 秒 → 呼出程序（仅当窗口隐藏在托盘时；全局免点击，绕开 Chromium Gamepad API 需先点击的限制）
//  - 0.5 秒内双击 B → 最小化到托盘（同样全局免点击，与前端引擎的双击 B 解耦）
static DWORD WINAPI gamepadSummonThread(LPVOID) {
    const ULONGLONG HOLD_MS = 500;
    const ULONGLONG B_DOUBLE_MS = 500;
    bool armed = true;           // 需松开后重新蓄力，避免持续按住反复触发
    ULONGLONG holdStart = 0;
    bool prevB = false;          // B 键上一帧状态（用于边沿检测，避免长按连发）
    ULONGLONG lastBPress = 0;    // 上一次 B 键按下时刻（双击判定）
    // 选择(Back) + B / 选择 + X 长按 0.5s 触发状态（需松开重蓄力，避免反复触发）
    bool kbArmed = true, kxArmed = true;
    ULONGLONG kbHoldStart = 0, kxHoldStart = 0;
    // Start+方向键 快捷调节的边沿状态（跨帧保持，用于每按一次仅触发一次）
    static bool prevDpUp = false, prevDpDown = false, prevDpLeft = false, prevDpRight = false;
    while (!g_summonQuit) {
        bool both = false;
        bool bPressed = false;
        bool startHeld = false;
        bool selectHeld = false;  // 选择(Back) 键
        bool xPressed = false;    // X 键
        bool dpUp = false, dpDown = false, dpLeft = false, dpRight = false;
        for (DWORD i = 0; i < 4; i++) {
            XINPUT_STATE s; ZeroMemory(&s, sizeof(s));
            if (XInputGetState(i, &s) == ERROR_SUCCESS) {
                WORD w = s.Gamepad.wButtons;
                if ((w & XINPUT_GAMEPAD_LEFT_SHOULDER) && (w & XINPUT_GAMEPAD_RIGHT_SHOULDER)) {
                    both = true;
                }
                if (w & XINPUT_GAMEPAD_B) bPressed = true;
                if (w & XINPUT_GAMEPAD_START) startHeld = true;
                if (w & XINPUT_GAMEPAD_BACK) selectHeld = true;
                if (w & XINPUT_GAMEPAD_X) xPressed = true;
                if (w & XINPUT_GAMEPAD_DPAD_UP) dpUp = true;
                if (w & XINPUT_GAMEPAD_DPAD_DOWN) dpDown = true;
                if (w & XINPUT_GAMEPAD_DPAD_LEFT) dpLeft = true;
                if (w & XINPUT_GAMEPAD_DPAD_RIGHT) dpRight = true;
            }
        }
        ULONGLONG now = GetTickCount64();
        // ── 双击 B → 最小化到托盘（仅窗口可见且设置开启时生效；隐藏时为 no-op，不影响游戏内 B 连按）──
        if (bPressed && !prevB) {
            if (g_summonEnabled && g_bDoubleMinimize && IsWindowVisible(g_hwnd) && (now - lastBPress) <= B_DOUBLE_MS) {
                lastBPress = 0;
                hideWindowAnimated(g_hwnd);
            } else {
                lastBPress = now;
            }
        }
        prevB = bPressed;
        // ── 按住 LB+RB 0.5s → 呼出（仅隐藏时）──
        if (both) {
            if (holdStart == 0) holdStart = now;
            if (armed && g_summonEnabled && g_hwnd && (now - holdStart) >= HOLD_MS) {
                armed = false;
                // 仅当窗口隐藏在托盘时呼出。已可见（哪怕被遮挡/开始菜单在前）绝不再抢焦点，
                // 否则按住 LB+RB 会强行把焦点从开始菜单/其它窗口抢回，表现为“手柄控制了开始菜单”。
                if (!IsWindowVisible(g_hwnd))
                    bringToFront(g_hwnd);
            }
        } else {
            holdStart = 0;
            armed = true;
        }
        // ── Start + 方向键 全局快捷调节（TDP / RTSS 锁帧） ──
        // 不依赖窗口焦点，游戏内全屏也生效；每按一次即一次生效（边沿检测，零 debounce）。
        if (startHeld) {
            if (g_tdpShortcut) {
                if (dpUp && !prevDpUp)   nativeAdjustTdp(+1);
                if (dpDown && !prevDpDown) nativeAdjustTdp(-1);
            }
            if (g_fpsShortcut) {
                if (dpRight && !prevDpRight) nativeAdjustRtss(+5);
                if (dpLeft && !prevDpLeft)   nativeAdjustRtss(-5);
            }
        }
        prevDpUp = dpUp; prevDpDown = dpDown; prevDpLeft = dpLeft; prevDpRight = dpRight;
        // ── 选择(Back) + B 长按 0.5s → 结束当前游戏（全局，游戏内全屏也生效） ──
        if (selectHeld && bPressed) {
            if (kbHoldStart == 0) kbHoldStart = now;
            if (kbArmed && g_killGame && (now - kbHoldStart) >= HOLD_MS) {
                kbArmed = false;
                runKillBat();
            }
        } else {
            kbHoldStart = 0;
            kbArmed = true;
        }
        // ── 选择(Back) + X 长按 0.5s → 呼出 Windows 触摸键盘（全局，游戏内全屏也生效） ──
        if (selectHeld && xPressed) {
            if (kxHoldStart == 0) kxHoldStart = now;
            if (kxArmed && g_openKeyboard && (now - kxHoldStart) >= HOLD_MS) {
                kxArmed = false;
                openTouchKeyboard();
            }
        } else {
            kxHoldStart = 0;
            kxArmed = true;
        }
        Sleep(50);
    }
    return 0;
}
// 隐藏规则：若实际进入 S4 休眠（本程序自己的「强制入睡」或用户/系统把电源键/睡眠键/合盖配成休眠），
// 睡眠守护的冻结/TDP/值守一律不执行（"一切均不发生"）。这里检测配置层面是否为 S4 动作。
static bool sgIsHibernateAction() {
    HKEY hk; wchar_t act[64] = {}; DWORD sz = sizeof(act);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes", 0, KEY_READ, &hk) != ERROR_SUCCESS)
        return false;
    if (RegQueryValueExW(hk, L"ActiveSetting", nullptr, nullptr, (BYTE*)act, &sz) != ERROR_SUCCESS) {
        RegCloseKey(hk); return false;
    }
    RegCloseKey(hk);
    std::wstring base = std::wstring(L"SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes\\")
        + act + L"\\4f971e89-eebd-4455-a8de-9e59040e7347"; // 电源按钮和盖子
    const wchar_t* items[] = {
        L"7648efa3-dd9c-4e3e-b566-50f929386280", // 电源按钮
        L"96996bc0-ad50-47ec-923b-6f41874dd9eb", // 睡眠按钮
        L"5ca83367-6e45-459f-a27b-476b1d01c936"  // 合盖
    };
    const wchar_t* vals[] = { L"ACSettingIndex", L"DCSettingIndex" };
    for (auto it : items) {
        std::wstring p = base + L"\\" + it;
        for (auto v : vals) {
            HKEY hk2; DWORD dw = 0, vsz = sizeof(dw), typ = 0;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, p.c_str(), 0, KEY_READ, &hk2) == ERROR_SUCCESS) {
                if (RegQueryValueExW(hk2, v, nullptr, &typ, (BYTE*)&dw, &vsz) == ERROR_SUCCESS && typ == REG_DWORD) {
                    if (dw == 2) { RegCloseKey(hk2); return true; } // 2 = 休眠(S4)
                }
                RegCloseKey(hk2);
            }
        }
    }
    return false;
}

// 同步执行外部 exe（带超时，避免阻塞电源事件回调），不捕获输出
static void sgRunExeSync(const std::wstring& exe, const std::wstring& args, DWORD timeoutMs = 4000) {
    if (!fspath::exists(exe)) return;
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si{sizeof(si)}; si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return;
    WaitForSingleObject(pi.hProcess, timeoutMs);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
}

// 同步运行命令并捕获 stdout（CREATE_NO_WINDOW），stderr 合并到 stdout 避免管道死锁。
// 供 smt.get / smt.set 调用（读/写 BCD 的 bcdedit）。
struct RunOut { std::string out; DWORD exitCode = 0; bool ran = false; };
static RunOut runCapture(const std::wstring& cmdLine) {
    RunOut res;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE hOutR = nullptr, hOutW = nullptr;
    if (!CreatePipe(&hOutR, &hOutW, &sa, 0)) return res;
    SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hOutW;
    si.hStdError = hOutW; // 合并 stderr→stdout，单行读取无死锁
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back(0);
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hOutR); CloseHandle(hOutW);
        return res;
    }
    CloseHandle(hOutW);
    std::string out; char b[4096]; DWORD rd;
    while (ReadFile(hOutR, b, sizeof(b), &rd, nullptr) && rd > 0) out.append(b, rd);
    CloseHandle(hOutR);
    WaitForSingleObject(pi.hProcess, 10000);
    GetExitCodeProcess(pi.hProcess, &res.exitCode);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    res.out = std::move(out);
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

// TDP 压制/还原：复用 pawnio/YeManTdpCtl.exe（与前端 setTdp 同款机制，零新依赖）
static bool sgApplyTdp(int watts) {
    std::wstring exe = L"C:\\SOFT\\YeMan\\PowerControl\\pawnio\\YeManTdpCtl.exe";
    if (!fspath::exists(exe)) return false;
    std::wstring a = L"set " + std::to_wstring(watts);
    std::string v = sgDetectVendor();
    if (!v.empty()) a += L" --vendor " + U2W(v);
    sgRunExeSync(exe, a);
    return true; // best-effort：外部 exe 已调用（成功与否由 YeManTdpCtl 自身决定）
}
static void sgRestoreTdp() { // 还原到用户配置的 DC TDP（PowerControl/tdp-dc.txt）
    std::wstring p = L"C:\\SOFT\\YeMan\\PowerControl\\tdp-dc.txt";
    std::string c = sgReadFile(p);
    if (c.empty()) return;
    try { int w = std::stoi(c); if (w > 0) sgApplyTdp(w); } catch (...) {}
}

// ── 手柄全局快捷调节（后台 XInput 线程处理，不依赖窗口焦点，游戏内全屏也生效） ──
// TDP：读 tdp-ac.txt（桌面恒 AC，与前端 adjustTdp 同真相源），±delta 后存档并立即下发硬件。
static int nativeReadTdp() {
    int v = 0;
    std::string c = sgReadFile(L"C:\\SOFT\\YeMan\\PowerControl\\tdp-ac.txt");
    if (c.empty()) c = sgReadFile(L"C:\\SOFT\\YeMan\\PowerControl\\tdp-dc.txt");
    if (!c.empty()) { try { v = std::stoi(c); } catch (...) { v = 0; } }
    return v;
}
static void nativeAdjustTdp(int delta) {
    if (!g_tdpShortcut) return;
    int cur = nativeReadTdp();
    int next = clampInt((cur > 0 ? cur : 0) + delta, 2, 300);
    if (next == cur) return;
    sgWriteFile(L"C:\\SOFT\\YeMan\\PowerControl\\tdp-ac.txt", std::to_string(next));
    sgApplyTdp(next);
    ipc_emit("gamepad.refresh", {}); // 通知前端回读刷新（窗口可见时）
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
    sgWriteFile(g, out);
    std::wstring dll = L"\"C:\\Program Files (x86)\\RivaTuner Statistics Server\\RTSSHooks64.dll\"";
    std::wstring ru  = L"C:\\Windows\\System32\\rundll32.exe";
    sgRunExeSync(ru, dll + L" LoadProfile");
    sgRunExeSync(ru, dll + L" SaveProfile");
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

// 系统内置黑名单（基名小写，不含 .exe）。WebView2 进程也排除，保护壳自身。
static const wchar_t* SG_BLACKLIST[] = {
    L"csrss", L"winlogon", L"lsass", L"services", L"smss", L"system", L"idle",
    L"dwm", L"explorer", L"msedgewebview2", L"searchhost", L"fontdrvhost",
    L"sihost", L"taskhostw", L"dwm", L"audiodg", L"nvcontainer", L"nvdisplay",
    L"rundll32", L"conhost", L"systemsettings", L"shellhost", L"startmenuexperiencehost"
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

struct SgProc { DWORD pid; std::wstring name; uint64_t ws; };
// 入睡前冻结结果（写进日志，便于跨机确认游戏到底有没有被冻）
struct SgSuspendResult { std::string name; DWORD pid = 0; uint64_t ws = 0; bool frozen = false; bool tdp = false; };
// 唤醒后恢复结果
struct SgResumeResult  { int count = 0; std::string names; };
static std::vector<SgProc> sgEnumProcs() {
    std::vector<SgProc> out;
    auto ex = sgExcludes();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            DWORD pid = pe.th32ProcessID;
            if (pid == 0 || pid == 4) continue;
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
            if (!h) continue;
            uint64_t ws = 0;
            PROCESS_MEMORY_COUNTERS_EX mc{}; mc.cb = sizeof(mc);
            if (GetProcessMemoryInfo(h, (PROCESS_MEMORY_COUNTERS*)&mc, sizeof(mc))) ws = mc.WorkingSetSize;
            wchar_t img[MAX_PATH] = {}; DWORD sz = MAX_PATH;
            std::wstring name;
            if (QueryFullProcessImageNameW(h, 0, img, &sz)) name = img;
            CloseHandle(h);
            std::wstring base = sgBaseName(name);
            bool skip = false;
            for (auto& e : ex) if (e == base) { skip = true; break; }
            if (skip) continue;
            // 仅考虑与 YeManCC 同会话的进程（避开系统会话 / 其他用户会话里的进程）
            if (g_sgSessionId != 0) {
                DWORD psid = 0;
                if (!ProcessIdToSessionId(pid, &psid) || psid != g_sgSessionId) continue;
            }
            // 额外排除游戏之外不应被冻的常见宿主（参考用户 PowerShell 扫描规则：steam/explorer/Taskmgr）
            static const wchar_t* extraExcl[] = { L"steam.exe", L"explorer.exe", L"Taskmgr.exe" };
            bool ex2 = false;
            for (auto e : extraExcl) if (_wcsicmp(base.c_str(), e) == 0) { ex2 = true; break; }
            if (ex2) continue;
            out.push_back({ pid, base, ws });
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

// 阶段1 入睡前：写标记(先于冻结，保证崩溃可恢复) → 冻结最大工作集进程 → 压 TDP
static SgSuspendResult sgSuspendTarget() {
    SgSuspendResult r;
    try {
        sgInitNt();
        auto procs = sgEnumProcs();
        SgProc best{0, {}, 0};
        for (auto& p : procs) if (p.ws > best.ws) best = p;
        r.name = W2U(best.name); r.pid = best.pid; r.ws = best.ws;
        // 写"目标"展示文件（仅展示，不参与恢复）
        sgWriteFile(SG_DIR + L"\\target.txt",
            "name=" + r.name + "|pid=" + std::to_string(r.pid) +
            "|epoch=" + std::to_string(sgNowEpoch()));
        if (best.pid == 0 || best.ws < SG_MIN_WS) return r; // 无可冻的大进程
        // 睡眠时暂停游戏（与「唤醒自动恢复」绑定，只一个开关）
        if (g_sgPauseResume) {
            // 标记先于冻结写：崩溃残留可被孤儿恢复扫描
            std::wstring mpath = SG_DIR + L"\\suspended\\" + std::to_wstring(best.pid) + L".txt";
            sgWriteFile(mpath, "name=" + r.name + "|epoch=" + std::to_string(sgNowEpoch()) +
                "|tdplocked=" + std::to_string(g_sgTdpLock ? g_sgSleepTdp : 0));
            HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, best.pid);
            if (h) {
                if (fnNtSuspend) { fnNtSuspend(h); r.frozen = true; }
                CloseHandle(h);
            }
        }
        // 入睡调低 TDP（best-effort）
        if (g_sgTdpLock) { r.tdp = sgApplyTdp(g_sgSleepTdp); g_sgTdpLowered = true; }
    } catch (...) {}
    return r;
}

// 手动暂停：冻结当前最大工作集进程（=当前游戏），写标记但不锁 TDP（区别于睡眠冻结）
static json sgSuspendCurrent() {
    json out = {{"paused", false}};
    try {
        sgInitNt();
        auto procs = sgEnumProcs();
        SgProc best{0, {}, 0};
        for (auto& p : procs) if (p.ws > best.ws) best = p;
        if (best.pid == 0 || best.ws < SG_MIN_WS) return out; // 无足够大的进程
        std::wstring mpath = SG_DIR + L"\\suspended\\" + std::to_wstring(best.pid) + L".txt";
        sgWriteFile(mpath, "name=" + W2U(best.name) + "|epoch=" + std::to_string(sgNowEpoch()) +
            "|tdplocked=0");
        HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, best.pid);
        if (h) {
            if (fnNtSuspend) fnNtSuspend(h);
            CloseHandle(h);
        }
        out["paused"] = true;
        out["pid"] = (int)best.pid;
        out["name"] = W2U(best.name);
    } catch (...) {}
    return out;
}

// 恢复全部被冻结进程（校验 PID 存活 + 映像名），并还原 TDP
static SgResumeResult sgResumeAll(bool restoreTdp) {
    SgResumeResult rr;
    try {
        std::wstring dir = SG_DIR + L"\\suspended";
        if (!fspath::exists(dir)) { if (restoreTdp) sgRestoreTdp(); return rr; }
        for (auto& e : fspath::directory_iterator(dir)) {
            if (!e.is_regular_file()) continue;
            std::wstring fn = e.path().filename().wstring();
            // 解析 <pid>.txt 中的数字 PID
            std::wstring digits;
            for (wchar_t c : fn) { if (c >= L'0' && c <= L'9') digits.push_back(c); else break; }
            if (digits.empty()) { fspath::remove(e.path()); continue; }
            DWORD pid = (DWORD)_wtol(digits.c_str());
            if (pid == 0) { fspath::remove(e.path()); continue; }
            HANDLE h = OpenProcess(PROCESS_SUSPEND_RESUME | PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE, pid);
            if (!h) { fspath::remove(e.path()); continue; } // 进程已不在 → 清标记
            // PID 复用校验：映像名须与标记一致，防误恢复错进程
            bool match = true;
            std::wstring nm;
            std::string content = sgReadFile(e.path());
            auto pos = content.find("name=");
            if (pos != std::string::npos) {
                std::string stored = content.substr(pos + 5);
                auto bar = stored.find('|'); if (bar != std::string::npos) stored = stored.substr(0, bar);
                nm = sgBaseName(U2W(stored));
                wchar_t img[MAX_PATH] = {}; DWORD sz = MAX_PATH;
                if (QueryFullProcessImageNameW(h, 0, img, &sz)) {
                    if (sgBaseName(std::wstring(img, sz)) != nm) match = false;
                }
            }
            if (match && fnNtResume) fnNtResume(h);
            CloseHandle(h);
            if (match) {
                fspath::remove(e.path()); // 仅成功恢复才删标记
                rr.count++;
                if (!rr.names.empty()) rr.names += ",";
                rr.names += W2U(nm);
            }
        }
        if (restoreTdp) sgRestoreTdp();
    } catch (...) {}
    return rr;
}

// ── 唤醒处置（极简，不做误唤醒判定、不重睡、不强制 S4）──
// 入睡（显示器关闭 / PBT_APMSUSPEND）已冻结游戏 + 压 TDP；这里任何唤醒（显示器亮起 /
// PBT_APMRESUMEAUTOMATIC / PBT_APMRESUMESUSPEND）直接恢复游戏并还原 TDP。
// 隐藏规则：若实际进入 S4 休眠（用户/系统把电源按钮/睡眠键/合盖配成休眠），由 sgIsHibernateAction()
// 在入睡侧拦截，本函数不会触发，故「S4 不触发」天然成立。
static void sgRealWake(const char* src) {
    if (!g_sgInSuspend) return;            // 非本程序睡眠周期，忽略
    // 睡眠时暂停游戏 与 唤醒自动恢复 绑定：开启才恢复游戏（关闭则保持冻结，需手动恢复）
    SgResumeResult rr;
    if (g_sgPauseResume) rr = sgResumeAll(g_sgTdpLowered);
    g_sgTdpLowered = false;
    g_sgInSuspend = false;
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

// Splash window
static HWND g_splash = nullptr;

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
static HRESULT finishCreateController(ICoreWebView2Environment* env);
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
    // DWMWA_SYSTEMBACKDROP_TYPE = 38 (Windows 11 22H2+)
    DwmSetWindowAttribute(hwnd, 38, &effectType, sizeof(effectType));
    if (effectType >= 2) {
        MARGINS m = {-1, -1, -1, -1};
        DwmExtendFrameIntoClientArea(hwnd, &m);
    }
    if (g_ctrl) {
        ComPtr<ICoreWebView2Controller2> ctrl2;
        if (SUCCEEDED(g_ctrl.As(&ctrl2))) {
            BYTE alpha = (effectType >= 2) ? 0 : 255;
            auto clr = currentWindowBackgroundColor();
            ctrl2->put_DefaultBackgroundColor({alpha, GetRValue(clr), GetGValue(clr), GetBValue(clr)});
        }
    }
}

// Set DWM visual attributes once. Uses g_bgClr captured at window creation.
// Safe to call again if g_bgClr changes (e.g. window.setBackgroundColor).
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
    // DWMWA_BORDER_COLOR = 34 — concrete color matching bg is more reliable
    // than DWMWA_COLOR_NONE which can flash on defocus on some installs.
    DwmSetWindowAttribute(g_hwnd, 34, &g_bgClr, sizeof(g_bgClr));
    // DWMWA_CAPTION_COLOR = 35
    DwmSetWindowAttribute(g_hwnd, 35, &g_bgClr, sizeof(g_bgClr));

    // Frame margins / backdrop
    if (g_effectType >= 2) {
        MARGINS m = {-1, -1, -1, -1};
        DwmExtendFrameIntoClientArea(g_hwnd, &m);
        DwmSetWindowAttribute(g_hwnd, 38, &g_effectType, sizeof(g_effectType));
    } else {
        MARGINS m = {0, 0, 0, 1};
        DwmExtendFrameIntoClientArea(g_hwnd, &m);
    }
}

static json loadConfig() {
    for (const auto& dir : production_asset_dirs()) {
        auto path = dir + L"\\app.config.json";
        std::ifstream f(path);
        if (f) { json j; f >> j; return j; }
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
//  卡顿根因：ipc_dispatch 在 WebView2 UI 线程同步执行 handler，其中
//  shell.run 会 WaitForSingleObject(INFINITE) 等 powercfg/schtasks 子进程
//  跑完，期间消息泵完全冻结（拖动/绘制卡死）。
//  方案：白名单命令 offload 到 worker 线程池执行，完成后 PostMessage
//  回 UI 线程，再由 UI 线程调用 PostWebMessageAsJson（WebView2 仅允许
//  UI 线程调用）。前端 ipc.ts 用 pending Map<id> 匹配响应，乱序安全。
//  可调旋钮（环境变量，便于多方案实测）：
//    YEMAN_ASYNC  0=全同步(旧行为) 1=仅 shell.run 异步(默认) 2=扩展白名单
//    YEMAN_POOL   worker 线程数 1..16（默认 4）
//    YEMAN_TRACE  1=写诊断日志(%TEMP%\yemancc_trace.log)+冻结监视线程
// ================================================================

#define WM_IPC_RESULT (WM_USER + 3)

static int  g_asyncMode = 1;   // 0/1/2，见上（默认 1：仅 shell.run 异步，实测零冻结且风险面最小）
static int  g_poolSize  = 8;   // 实测 8 线程使 11 个首屏子进程全并行，2.4s 内完成（4 线程需 3.8s）

// ── 诊断 trace（仅 YEMAN_TRACE=1 时激活；生产零开销）──
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
static std::once_flag g_poolOnce;

static void poolSubmit(std::function<void()> job) {
    std::call_once(g_poolOnce, [] {
        for (int i = 0; i < g_poolSize; i++) {
            std::thread([] {
                for (;;) {
                    std::function<void()> j;
                    {
                        std::unique_lock<std::mutex> lk(g_poolMx);
                        g_poolCv.wait(lk, [] { return !g_poolQ.empty(); });
                        j = std::move(g_poolQ.front());
                        g_poolQ.pop_front();
                    }
                    // handler 抛出的异常已在任务内部捕获，此处兜底防线程死亡
                    try { j(); } catch (...) {}
                }
            }).detach();
        }
    });
    {
        std::lock_guard<std::mutex> lk(g_poolMx);
        g_poolQ.push_back(std::move(job));
    }
    g_poolCv.notify_one();
}

// ── 异步白名单：只放「无 UI、无全局可变状态、纯本地计算/IO」的命令 ──
static bool ipc_cmd_async(const std::string& cmd) {
    if (g_asyncMode <= 0) return false;
    // 模式 1：仅 shell.run —— powercfg/schtasks 等全部子进程的唯一入口，
    // 占 UI 线程阻塞成本的 95% 以上；handler 为纯函数（只用局部变量）。
    static const std::unordered_set<std::string> lvl1 = {
        "shell.run",
    };
    // 模式 2：扩展只读命令（纯读文件/注册表/HTTP/CPU 拓扑，均无共享可变状态）
    static const std::unordered_set<std::string> lvl2 = {
        "fs.readTextFile", "fs.readTextRange", "fs.exists", "fs.readDir", "fs.stat",
        "registry.read", "registry.exists", "http.request", "smt.get",
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

static void ipc_on(const std::string& cmd, IpcFn fn) {
    g_cmds[cmd] = std::move(fn);
}

static void ipc_emit(const std::string& ev, const json& data = {}) {
    if (!g_view) return;
    json m = {{"event", ev}, {"data", data}};
    g_view->PostWebMessageAsJson(U2W(m.dump()).c_str());
}

static void ipc_dispatch(LPCWSTR raw) {
    try {
        auto req = json::parse(W2U(raw));
        json resp;
        resp["id"] = req.value("id", -1);
        auto cmd  = req.value("cmd", std::string{});
        auto args = req.value("args", json::object());

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
        if (ipc_cmd_async(cmd)) {
            IpcFn fn = it->second;
            ULONGLONG tq = GetTickCount64();
            poolSubmit([resp, args, fn = std::move(fn), cmd, tq]() mutable {
                ULONGLONG ts = GetTickCount64();
                try { resp["result"] = fn(args); }
                catch (const std::exception& e) { resp["error"] = e.what(); }
                catch (...) { resp["error"] = "native error: " + cmd; }
                traceLog("IPC %-28s pool wait=%llums exec=%llums", cmd.c_str(),
                         (unsigned long long)(ts - tq),
                         (unsigned long long)(GetTickCount64() - ts));
                // WebView2 仅允许 UI 线程调用 → 结果经 WM_IPC_RESULT 回传
                //（对齐 watcher 的 WM_FILE_CHANGED 堆分配 + WndProc delete 模式）
                json* heap = new json(std::move(resp));
                if (!PostMessageW(g_hwnd, WM_IPC_RESULT, 0, (LPARAM)heap))
                    delete heap; // 窗口已销毁（进程退出中）
            });
            return;
        }

        // ── 同步路径（原行为）──
        ULONGLONG ts = GetTickCount64();
        try { resp["result"] = it->second(args); }
        catch (const std::exception& e) { resp["error"] = e.what(); }
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

    // Hiding is explicit: just hide, no animation/activation.
    if (showCmd == SW_HIDE) {
        hideWindowAnimated(hwnd);
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

    if (activate && !isMinimize)
        SetForegroundWindow(hwnd);
}

static void hideWindowAnimated(HWND hwnd) {
    if (!hwnd) return;

    if (IsWindowVisible(hwnd) && windowAnimationsEnabled()) {
        if (AnimateWindow(hwnd, 100, AW_HIDE | AW_BLEND))
            return;
    }
    ShowWindow(hwnd, SW_HIDE);
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
        DwmSetWindowAttribute(g_hwnd, 34, &g_bgClr, sizeof(g_bgClr));
        DwmSetWindowAttribute(g_hwnd, 35, &g_bgClr, sizeof(g_bgClr));
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
            BYTE alpha = (g_effectType >= 2) ? 0 : 255;
            ctrl2->put_DefaultBackgroundColor({
                alpha, GetRValue(g_bgClr), GetGValue(g_bgClr), GetBValue(g_bgClr)
            });
        }
    }
}

// ================================================================
//  Commands: Window
// ================================================================

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
    ipc_on("window.show", [](const json&) -> json {
        showWindowAnimated(g_hwnd, IsIconic(g_hwnd) ? SW_RESTORE : SW_SHOW);
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
            if (SUCCEEDED(g_ctrl.As(&ctrl2)))
                ctrl2->put_DefaultBackgroundColor({255, GetRValue(clr), GetGValue(clr), GetBValue(clr)});
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
        std::ofstream f(U2W(path), std::ios::binary);
        if (!f) throw std::runtime_error("Cannot write: " + path);
        f.write(content.data(), content.size());
        return true;
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
    ipc_on("app.exit", [](const json& a) -> json {
        PostQuitMessage(a.value("code", 0));
        return true;
    });
    ipc_on("app.dataDir", [](const json&) -> json {
        return W2U(app_data_dir());
    });
    ipc_on("app.exeDir", [](const json&) -> json {
        return W2U(exe_dir());
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
            {"sleepTdp", {{"mode", g_sgTdpLock ? "lock" : "off"}, {"watts", g_sgSleepTdp}}}
        };
    });
    ipc_on("sleepGuard.setConfig", [](const json& a) -> json {
        // 仅在提供字段时覆盖，缺省保留当前值（前端始终发全量）
        if (a.contains("mode")) {
            std::string m = a.value("mode", g_sgMode);
            if (m == "off" || m == "custom") g_sgMode = m;
        }
        if (a.contains("pauseResume")) g_sgPauseResume = a.value("pauseResume", g_sgPauseResume);
        if (a.contains("sleepTdp")) {
            auto t = a["sleepTdp"];
            g_sgTdpLock = t.value("mode", std::string("lock")) == "lock";
            int w = t.value("watts", g_sgSleepTdp); if (w < 5) w = 5; if (w > 30) w = 30; g_sgSleepTdp = w;
        }
        sgSaveConfig();
        return true;
    });
    ipc_on("sleepGuard.recoverAll", [](const json& a) -> json {
        int before = 0;
        std::wstring dir = SG_DIR + L"\\suspended";
        std::error_code ec;
        if (fspath::exists(dir, ec))
            for (auto& e : fspath::directory_iterator(dir, ec))
                if (e.is_regular_file()) before++;
        sgResumeAll(true); // 恢复全部 + 还原 TDP（仅恢复不冻结）
        return {{"resumed", before}};
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
        HINTERNET hSession = WinHttpOpen(L"QQ/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) throw std::runtime_error("WinHttpOpen failed");

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
        DWORD available, read;
        while (WinHttpQueryDataAvailable(hRequest, &available) && available > 0) {
            std::string chunk(available, 0);
            WinHttpReadData(hRequest, chunk.data(), available, &read);
            chunk.resize(read);
            respBody += chunk;
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

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
    std::ifstream f(g_stateFile);
    if (!f) return {};
    try { json j; f >> j; return j; }
    catch (...) { return {}; }
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
    BOOL ok = Shell_NotifyIconW(NIM_ADD, &g_nid);
    // NIM_ADD can transiently fail if Explorer is still starting; retry once.
    if (!ok) ok = Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_trayActive = !!ok;
    return g_trayActive;
}

static void reg_tray() {
    ipc_on("tray.create", [](const json& a) -> json {
        auto tip = U2W(a.value("tooltip", std::string{"App"}));
        return trayCreate(tip);
    });
    ipc_on("tray.setTooltip", [](const json& a) -> json {
        if (!g_trayActive) return false;
        auto tip = U2W(a.value("tooltip", std::string{"App"}));
        wcsncpy_s(g_nid.szTip, tip.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        return true;
    });
    ipc_on("tray.remove", [](const json&) -> json {
        if (!g_trayActive) return false;
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_trayActive = false;
        return true;
    });
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
    DWORD avail, rd;
    while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
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

static bool downloadFile(const std::string& url, const std::wstring& dest) {
    URL_COMPONENTS uc;
    wchar_t host[256]{}, path[2048]{}, extra[2048]{};
    std::wstring objectPath;
    if (!crackHttpUrl(url, uc, host, path, extra, objectPath)) return false;
    if (uc.nScheme != INTERNET_SCHEME_HTTPS) return false;
    std::error_code cleanupEc;
    std::filesystem::remove(dest, cleanupEc);
    bool https = true;
    HINTERNET hS = WinHttpOpen(L"QQ/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return false;
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hS, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
    HINTERNET hC = WinHttpConnect(hS, host, uc.nPort, 0);
    if (!hC) { WinHttpCloseHandle(hS); return false; }
    HINTERNET hR = WinHttpOpenRequest(hC, L"GET", objectPath.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
    if (!hR) { WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return false; }
    if (!WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(hR, nullptr)) {
        WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
        return false;
    }
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(hR, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX) ||
        statusCode < 200 || statusCode >= 300) {
        WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
        return false;
    }
    std::ofstream out(dest, std::ios::binary);
    if (!out) {
        WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
        return false;
    }
    bool ok = true;
    DWORD avail = 0, rd = 0;
    while (true) {
        if (!WinHttpQueryDataAvailable(hR, &avail)) { ok = false; break; }
        if (avail == 0) break;
        std::string chunk(avail, 0);
        if (!WinHttpReadData(hR, chunk.data(), avail, &rd)) { ok = false; break; }
        out.write(chunk.data(), rd);
        if (!out) { ok = false; break; }
    }
    out.close();
    WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
    if (!ok) {
        std::error_code ec;
        std::filesystem::remove(dest, ec);
    }
    return ok;
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
    BYTE buf[65536]; DWORD rd;
    while (ReadFile(f, buf, sizeof(buf), &rd, nullptr) && rd > 0) CryptHashData(hh, buf, rd, 0);
    CloseHandle(f);
    BYTE dig[32]; DWORD diglen = sizeof(dig);
    std::string out;
    if (CryptGetHashParam(hh, HP_HASHVAL, dig, &diglen, 0)) {
        wchar_t hex[3];
        for (int i = 0; i < 32; i++) { wsprintfW(hex, L"%02x", dig[i]); out += (char)hex[0]; out += (char)hex[1]; }
    }
    CryptDestroyHash(hh); CryptReleaseContext(h, 0);
    return out;
}

// 用系统 tar.exe 解压 zip 到目标目录（Windows 10+ 自带，无需第三方库）
static bool unzipTar(const std::wstring& zip, const std::wstring& dest) {
    std::error_code ec; fspath::create_directories(dest, ec);
    std::wstring cmd = L"\"C:\\Windows\\System32\\tar.exe\" -xf \"" + zip + L"\" -C \"" + dest + L"\"";
    STARTUPINFOW si{ sizeof(si) }; PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 120000);
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return code == 0;
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

static void reg_updater() {
    ipc_on("app.version", [](const json&) -> json {
        return APP_VER_STR;
    });
    ipc_on("app.checkUpdate", [](const json& a) -> json {
        auto url = a.value("url", std::string{});
        if (url.empty()) throw std::runtime_error("url is required");
        return httpGet(url);
    });
    // 下载完整更新包（exe + index.html + assets + ...）到 %LOCALAPPDATA%\YeManCC\update\package.zip，可选 sha256 校验
    ipc_on("app.downloadUpdate", [](const json& a) -> json {
        auto url = a.value("url", std::string{});
        if (url.empty()) throw std::runtime_error("url is required");
        auto sha = a.value("sha256", std::string{});
        std::error_code ec; fspath::create_directories(app_data_dir() + L"\\update", ec);
        auto dest = app_data_dir() + L"\\update\\package.zip";
        if (!downloadFile(url, dest)) throw std::runtime_error("Download failed");
        if (!sha.empty()) {
            auto got = sha256File(dest);
            if (_stricmp(got.c_str(), sha.c_str()) != 0)
                throw std::runtime_error("Checksum mismatch (expected " + sha + ", got " + got + ")");
        }
        return W2U(dest);
    });
    // 安装：解压已下载的 package.zip → 写 update.bat 用 robocopy 合并覆盖安装目录
    // （排除 config/ 与 app.config.json，保留用户数据）→ 重启 → 自删 bat → 退出主程序
    ipc_on("app.installUpdate", [](const json&) -> json {
        auto zip = app_data_dir() + L"\\update\\package.zip";
        if (!fspath::exists(zip)) throw std::runtime_error("No update package downloaded");
        auto staging = app_data_dir() + L"\\update\\staging";
        std::error_code ec; fspath::remove_all(staging, ec);
        if (!unzipTar(zip, staging)) throw std::runtime_error("Failed to extract update package");
        wchar_t exePath[MAX_PATH]; GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring exeStr(exePath);
        std::wstring exedir = exeStr.substr(0, exeStr.find_last_of(L"\\"));
        auto bat = app_data_dir() + L"\\update.bat";
        {
            std::ofstream f(bat);
            f << "@echo off\n";
            f << "timeout /t 1 /nobreak >nul\n";
            f << "copy \"" << W2U(exePath) << "\" \"" << W2U(exePath) << ".old\" >nul 2>&1\n";
            // /E 递归；/XF 排除 app.config.json（保留已安装的）；/XD 排除 config（保留用户数据）
            f << "robocopy \"" << W2U(staging) << "\" \"" << W2U(exedir)
              << "\" /E /XF app.config.json /XD config /NFL /NDL /NJH /NJS /NP\n";
            f << "start \"\" \"" << W2U(exePath) << "\"\n";
            f << "del \"%~f0\"\n";
        }
        ShellExecuteW(nullptr, L"open", bat.c_str(), nullptr, nullptr, SW_HIDE);
        PostQuitMessage(0);
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
            {"openKeyboard", g_openKeyboard}
        };
    });
    ipc_on("summon.set", [](const json& a) -> json {
        if (a.contains("enabled")) g_summonEnabled = a.value("enabled", g_summonEnabled);
        if (a.contains("bDoubleMinimize")) g_bDoubleMinimize = a.value("bDoubleMinimize", g_bDoubleMinimize);
        if (a.contains("tdpShortcut")) g_tdpShortcut = a.value("tdpShortcut", g_tdpShortcut);
        if (a.contains("fpsShortcut")) g_fpsShortcut = a.value("fpsShortcut", g_fpsShortcut);
        if (a.contains("killGame")) g_killGame = a.value("killGame", g_killGame);
        if (a.contains("openKeyboard")) g_openKeyboard = a.value("openKeyboard", g_openKeyboard);
        summonSave();
        return {
            {"enabled", g_summonEnabled},
            {"bDoubleMinimize", g_bDoubleMinimize},
            {"tdpShortcut", g_tdpShortcut},
            {"fpsShortcut", g_fpsShortcut},
            {"killGame", g_killGame},
            {"openKeyboard", g_openKeyboard}
        };
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

    // Window opacity
    ipc_on("window.setOpacity", [](const json& a) -> json {
        double opacity = a.value("opacity", 1.0);
        BYTE alpha = (BYTE)(opacity * 255);
        auto style = GetWindowLongW(g_hwnd, GWL_EXSTYLE);
        if (alpha < 255) {
            SetWindowLongW(g_hwnd, GWL_EXSTYLE, style | WS_EX_LAYERED);
            SetLayeredWindowAttributes(g_hwnd, 0, alpha, LWA_ALPHA);
        } else {
            SetWindowLongW(g_hwnd, GWL_EXSTYLE, style & ~WS_EX_LAYERED);
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
            } else {
                r["acLine"] = 255;
                r["powerMode"] = "ac";
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

    ipc_on("shell.run", [](const json& a) -> json {
        auto program = a.value("program", std::string{});
        if (program.empty()) throw std::runtime_error("program is required");
        std::wstring cmdLine = quote_windows_arg(U2W(program));
        if (a.contains("args") && a["args"].is_array()) {
            for (auto& arg : a["args"]) {
                if (!arg.is_string()) throw std::runtime_error("shell.run args must be strings");
                cmdLine += L" ";
                cmdLine += quote_windows_arg(U2W(arg.get<std::string>()));
            }
        }

        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE hOutR, hOutW, hErrR, hErrW;
        CreatePipe(&hOutR, &hOutW, &sa, 0);
        CreatePipe(&hErrR, &hErrW, &sa, 0);
        SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(hErrR, HANDLE_FLAG_INHERIT, 0);
        // 写端 hOutW/hErrW 保持可继承：子进程需要它来写 stdout，shell.run 才能读回
        // 命令输出（如 readPhysicalCores 读 NumberOfCores）。RTSS 经
        // `start /B` 启动时会持有该管道导致阻塞的问题，已在前端启动命令用 `> NUL 2>&1`
        // 把 RTSS 的 std 重定向到 NUL 来解决，无需在此断开写端继承。

        STARTUPINFOW si{sizeof(si)};
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hOutW;
        si.hStdError = hErrW;
        PROCESS_INFORMATION pi{};

        std::vector<wchar_t> cmd(cmdLine.begin(), cmdLine.end());
        cmd.push_back(0);
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(hOutR); CloseHandle(hOutW);
            CloseHandle(hErrR); CloseHandle(hErrW);
            throw std::runtime_error("Failed to start process");
        }
        CloseHandle(hOutW);
        CloseHandle(hErrW);

        auto readPipe = [](HANDLE h) -> std::string {
            std::string result;
            char buf[4096];
            DWORD rd;
            while (ReadFile(h, buf, sizeof(buf), &rd, nullptr) && rd > 0)
                result.append(buf, rd);
            CloseHandle(h);
            return result;
        };

        // Read stderr in a background thread to prevent pipe deadlock
        struct PipeCtx { HANDLE h; std::string data; };
        auto* errCtx = new PipeCtx{hErrR, {}};
        HANDLE hErrThread = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
            auto* c = (PipeCtx*)p;
            char buf[4096]; DWORD rd;
            while (ReadFile(c->h, buf, sizeof(buf), &rd, nullptr) && rd > 0)
                c->data.append(buf, rd);
            CloseHandle(c->h);
            return 0;
        }, errCtx, 0, nullptr);
        auto stdout_ = oemToUtf8(readPipe(hOutR));
        WaitForSingleObject(hErrThread, INFINITE);
        CloseHandle(hErrThread);
        auto stderr_ = oemToUtf8(std::move(errCtx->data));
        delete errCtx;
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        return json{{"exitCode", (int)exitCode}, {"stdout", stdout_}, {"stderr", stderr_}};
    });
}

// ================================================================
//  Splash screen
// ================================================================

static LRESULT CALLBACK SplashProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        // Background
        auto bg = currentWindowBackgroundColor();
        HBRUSH brush = CreateSolidBrush(bg);
        FillRect(hdc, &rc, brush);
        DeleteObject(brush);
        // Title text
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(200,200,200));
        auto title = U2W(g_cfg.value("/window/title"_json_pointer, std::string{"\xe5\xbc\xba\xe5\xbc\xba"}));
        HFONT hFont = CreateFontW(28, 0, 0, 0, FW_LIGHT, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        auto old = SelectObject(hdc, hFont);
        DrawTextW(hdc, title.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        // "Loading..." below
        RECT rc2 = rc; rc2.top += 40;
        HFONT hSmall = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        SelectObject(hdc, hSmall);
        SetTextColor(hdc, RGB(120,120,140));
        DrawTextW(hdc, L"Loading...", -1, &rc2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, old);
        DeleteObject(hFont);
        DeleteObject(hSmall);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
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
    int sw = 300, sh = 120;
    int sx = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - sw) / 2;
    int sy = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - sh) / 2;

    g_splash = CreateWindowExW(WS_EX_TOOLWINDOW, L"QQ_Splash", L"",
        WS_POPUP, sx, sy, sw, sh, nullptr, nullptr, hi, nullptr);
    // DWM shadow
    MARGINS m = {0,0,0,1};
    DwmExtendFrameIntoClientArea(g_splash, &m);
    ShowWindow(g_splash, SW_SHOW);
    UpdateWindow(g_splash);
}

static void closeSplash() {
    if (g_splash) {
        DestroyWindow(g_splash);
        g_splash = nullptr;
    }
}

// ================================================================
//  WebView2 initialization
// ================================================================

static bool configureAppHost(ICoreWebView2* view) {
    if (!view) return false;

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
    v3->SetVirtualHostNameToFolderMapping(
        L"app.localhost", dir.c_str(),
        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
    v3->SetVirtualHostNameToFolderMapping(
        L"app.local", dir.c_str(),
        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
    return true;
}

static void setupWebView(ICoreWebView2Controller* ctrl) {
    g_ctrl = ctrl;
    g_ctrl->get_CoreWebView2(&g_view);

    RECT b; GetClientRect(g_hwnd, &b);
    g_ctrl->put_Bounds(b);

    // Background color from config (alpha=255 for opaque)
    ComPtr<ICoreWebView2Controller2> ctrl2;
    if (SUCCEEDED(g_ctrl.As(&ctrl2))) {
        auto clr = currentWindowBackgroundColor();
        BYTE alpha = (g_effectType >= 2) ? 0 : 255;
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

    // Process crash recovery
    g_view->add_ProcessFailed(
        Callback<ICoreWebView2ProcessFailedEventHandler>(
        [](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
            COREWEBVIEW2_PROCESS_FAILED_KIND kind;
            args->get_ProcessFailedKind(&kind);
            if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED) {
                // Auto-clean corrupted EBWebView data so next launch recovers
                auto dataDir = app_data_dir();
                auto ebw = dataDir + L"\\EBWebView";
                if (fspath::exists(ebw)) {
                    fspath::remove_all(ebw);
                    fspath::create_directories(dataDir);
                }
                MessageBoxW(g_hwnd,
                    L"WebView2 进程已崩溃。\n\n已自动清除损坏的缓存数据，\n下次启动将自动恢复。",
                    L"错误", MB_ICONERROR);
                PostQuitMessage(1);
            }
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

    // Navigate
    if (dev) {
        g_view->Navigate(g_devUrl.c_str());
    } else {
        if (!configureAppHost(g_view.Get())) {
            MessageBoxW(
                g_hwnd,
                L"未找到前端资源（index.html）。\n普通构建请保留 dist 目录；如需单文件分发，请使用 bun run build:single 或 bun run package:single。",
                L"错误",
                MB_ICONERROR);
            PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
            return;
        }
        g_view->Navigate(L"https://app.localhost/index.html");
    }
    g_view->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
        [](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
            // Wails hack: Hide+Show WebView2 controller to force render
            // https://github.com/MicrosoftEdge/WebView2Feedback/issues/1077
            g_ctrl->put_IsVisible(FALSE);
            g_ctrl->put_IsVisible(TRUE);
            g_webviewReady = true;
            closeSplash();
            // First frame is painted now — reveal a window held hidden by showWhenReady,
            // so the user never sees the blank/native-framed window during WebView2 init.
            if (g_deferFirstShow) {
                g_deferFirstShow = false;
                showWindowAnimated(g_hwnd, g_firstShowCmd, true);
                UpdateWindow(g_hwnd);
            }
            return S_OK;
        }).Get(), nullptr);
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

static void init_webview() {
    auto dataDir = app_data_dir();

    // Auto-recovery: if previous session crashed (crashpad .dmp files exist in
    // EBWebView/Crashpad/reports/), clear the WebView2 user data directory.
    // Corrupted GPU/cache/Session Storage is the #1 cause of BROWSER_PROCESS_EXITED
    // on subsequent launches.  This is safe because EBWebView is fully ephemeral —
    // all persistent state lives in PowerControl txt/xml files, not in WebView2 data.
    {
        auto crashDir = dataDir + L"\\EBWebView\\Crashpad\\reports";
        bool hasCrashDumps = false;
        if (fspath::is_directory(crashDir)) {
            for (auto const& e : fspath::directory_iterator(crashDir)) {
                if (e.path().extension() == L".dmp") { hasCrashDumps = true; break; }
            }
        }
        if (hasCrashDumps) {
            auto ebw = dataDir + L"\\EBWebView";
            if (fspath::exists(ebw)) {
                fspath::remove_all(ebw);
                fspath::create_directories(dataDir); // keep root dir for window-state.json
            }
        }
    }

    // Stability-focused browser arguments for long-running embedded WebView2:
    //   --disable-software-rasterizer REMOVED: was forcing GPU-only rendering;
    //     without software fallback, any GPU hiccup (driver/DWM/VRAM) kills
    //     the entire browser process (BROWSER_PROCESS_EXITED).  Software
    //     rasterizer is a critical safety net for embedded/kiosk scenarios.
    //   --disable-gpu-compositing: reduces GPU composition pressure on
    //     high-refresh / multi-monitor / DWM-effect setups.
    //   --in-process-gpu: runs GPU in browser process so a renderer/GPU
    //     crash does NOT propagate to BROWSER_PROCESS_EXITED.
    //   --disable-gpu-sandbox kept: relaxes GPU process sandbox (helps some drivers).
    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    options->put_AdditionalBrowserArguments(
        L"--disable-features=msSmartScreenProtection,RendererCodeIntegrity,msWebOOUI,msPdfOOUI"
        L" --disable-background-networking --no-proxy-server"
        L" --disable-gpu-sandbox --disable-gpu-compositing --in-process-gpu"
        L" --disable-extensions --disable-component-extensions-with-background-pages"
        L" --no-default-browser-check --disable-client-side-phishing-detection"
        L" --disable-renderer-backgrounding");
    CreateCoreWebView2EnvironmentWithOptions(nullptr, dataDir.c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
            // GPU-fallback retry: if first init fails (common on some GPU drivers),
            // retry once with --disable-gpu for pure software rendering.
            if (FAILED(hr)) {
                auto dataDir2 = app_data_dir();
                auto opts2 = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
                opts2->put_AdditionalBrowserArguments(
                    L"--disable-features=msSmartScreenProtection,RendererCodeIntegrity,msWebOOUI,msPdfOOUI"
                    L" --disable-background-networking --no-proxy-server"
                    L" --disable-gpu --disable-gpu-compositing --in-process-gpu"
                    L" --disable-extensions --disable-component-extensions-with-background-pages"
                    L" --no-default-browser-check --disable-client-side-phishing-detection"
                    L" --disable-renderer-backgrounding");
                return CreateCoreWebView2EnvironmentWithOptions(
                    nullptr, dataDir2.c_str(), opts2.Get(),
                    Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                    [](HRESULT hr2, ICoreWebView2Environment* env2) -> HRESULT {
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
                        finishCreateController(g_env.Get());
                        return S_OK;
                    }).Get());
            }
            g_env = env;
            finishCreateController(g_env.Get());
            return S_OK;
        }).Get());
}

// Shared controller creation logic (used by both normal and GPU-fallback init paths).
// Sets background color to avoid white flash, then creates the controller + calls setupWebView.
static HRESULT finishCreateController(ICoreWebView2Environment* env) {
    ComPtr<ICoreWebView2Environment10> env10;
    auto handler = Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        [](HRESULT hr, ICoreWebView2Controller* ctrl) -> HRESULT {
            if (FAILED(hr)) { PostQuitMessage(1); return hr; }
            setupWebView(ctrl);
            return S_OK;
        });
    if (SUCCEEDED(env->QueryInterface(IID_PPV_ARGS(&env10)))) {
        ComPtr<ICoreWebView2ControllerOptions> opts;
        if (SUCCEEDED(env10->CreateCoreWebView2ControllerOptions(&opts))) {
            ComPtr<ICoreWebView2ControllerOptions3> opts3;
            if (SUCCEEDED(opts.As(&opts3))) {
                auto clr = currentWindowBackgroundColor();
                BYTE alpha = (g_effectType >= 2) ? 0 : 255;
                opts3->put_DefaultBackgroundColor({alpha, GetRValue(clr), GetGValue(clr), GetBValue(clr)});
            }
            return env10->CreateCoreWebView2ControllerWithOptions(g_hwnd, opts.Get(), handler.Get());
        }
    }
    return env->CreateCoreWebView2Controller(g_hwnd, handler.Get());
}

// ================================================================
//  Window procedure
// ================================================================

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    // ── Fill background (visible briefly before WebView2 content loads) ──
    case WM_NCPAINT:
        if (g_frameless) return 0;
        break;
    case WM_NCACTIVATE:
        if (g_frameless) return TRUE; // prevents DefWindowProc from repainting NC area
        break;
    case WM_ERASEBKGND:
        if (g_frameless && g_bgBrush) {
            HDC hdc = (HDC)w;
            RECT rc;
            GetClientRect(h, &rc);
            FillRect(hdc, &rc, g_bgBrush);
            return 1;
        }
        break;
    case WM_PAINT:
        if (g_frameless && g_bgBrush) {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(h, &ps);
            FillRect(hdc, &ps.rcPaint, g_bgBrush);
            EndPaint(h, &ps);
            return 0;
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
            if (g_fullHeight) {
                applyFullHeightLayout();   // 重新按新 DPI 工作区计算并吸附右侧
            } else {
                auto* r = reinterpret_cast<RECT*>(l);
                SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER);
            }
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
    case WM_SYSCOMMAND:
        // The window lives only in the tray, never in the main taskbar.
        // Intercept minimize and hide to the tray instead.
        if ((w & 0xFFF0) == SC_MINIMIZE) {
            hideWindowAnimated(h);
            return 0;
        }
        break;
    case WM_CLOSE:
        saveWindowState();
        ipc_emit("window.closing");
        if (g_trayActive) { hideWindowAnimated(h); return 0; }
        hideWindowAnimated(h);
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        // 取消电源通知订阅 + 清防抖定时器
        if (g_acdcNotify) { UnregisterPowerSettingNotification(g_acdcNotify); g_acdcNotify = nullptr; }
        KillTimer(h, TIMER_ID_ACDC);
        // 睡眠守护：退出前尽力恢复全部冻结进程 + 还原 TDP（防冻死游戏）
        sgResumeAll(true);
        // Cleanup watchers
        for (auto& [id, w] : g_watchers) {
            if (stopWatcher(w, 1000))
                delete w;
        }
        g_watchers.clear();
        if (g_trayActive) Shell_NotifyIconW(NIM_DELETE, &g_nid);
        // 按键呼出：通知后台手柄轮询线程退出并回收
        g_summonQuit = true;
        if (g_summonThread) {
            WaitForSingleObject(g_summonThread, 1000);
            CloseHandle(g_summonThread);
            g_summonThread = nullptr;
        }
        // 掌机前端自动关闭：通知后台轮询线程退出并回收
        if (g_autoCloseThread) {
            WaitForSingleObject(g_autoCloseThread, 1000);
            CloseHandle(g_autoCloseThread);
            g_autoCloseThread = nullptr;
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
        auto* resp = reinterpret_cast<json*>(l);
        if (g_view) g_view->PostWebMessageAsJson(U2W(resp->dump()).c_str());
        delete resp;
        return 0;
    }
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
                    // ── 熔断：5s 滑动窗口内真实切换 >10 次 → 硬退出，避免系统卡死 ──
                    DWORD now = GetTickCount();
                    g_acSwitchTicks.push_back(now);
                    g_acSwitchTicks.erase(
                        std::remove_if(g_acSwitchTicks.begin(), g_acSwitchTicks.end(),
                            [now](DWORD t){ return (now - t) > ACDC_BURST_MS; }),
                        g_acSwitchTicks.end());
                    if ((int)g_acSwitchTicks.size() > ACDC_BURST_LIMIT) {
                        KillTimer(h, TIMER_ID_ACDC);
                        ExitProcess(0); // 立即退出，不再响应任何后续切换
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
            else if (IsEqualGUID(pbs->PowerSetting, YM_GUID_MONITOR_POWER_ON) &&
                     pbs->DataLength >= sizeof(DWORD)) {
                DWORD on = *reinterpret_cast<DWORD*>(pbs->Data); // 0=显示器关 1=开
                if (on == 0) {
                    // 显示器关闭 → 进入睡眠（覆盖 S0 现代待机；S3 仍由 PBT_APMSUSPEND 处理）
                    g_monitorOn = false;
                    if (g_guardEnabled && !sgIsHibernateAction()) {
                        // 隐藏规则：实际进入 S4 休眠（用户/系统把电源按钮/睡眠键/合盖配成休眠）→ 一切均不发生
                        g_sgInSuspend = true;
                        sgSuspendTarget(); // 冻结游戏 + 压 TDP（诊断日志已移除）
                    }
                } else {
                    // 显示器亮起 → 唤醒（覆盖 S0 现代待机）：直接恢复游戏 + 还原 TDP
                    g_monitorOn = true;
                    if (g_sgInSuspend) sgRealWake("monitor_on");
                }
            }
        }
        // ── 睡眠守护：PBT_APM* 同样走 WM_POWERBROADCAST（窗口程序自动送达，无需额外注册）──
        else if (w == PBT_APMSUSPEND) {
            if (g_guardEnabled && !sgIsHibernateAction()) {
                g_sgInSuspend = true;
                sgSuspendTarget(); // 冻结游戏 + 压 TDP（诊断日志已移除）
            }
        }
        else if (w == PBT_APMRESUMEAUTOMATIC) {
            // 自动/误唤醒候选：直接恢复游戏 + 还原 TDP（不做误唤醒判定、不重睡）
            if (g_sgInSuspend) sgRealWake("resume_auto");
        }
        else if (w == PBT_APMRESUMESUSPEND) {
            // S3 确定性用户唤醒：直接恢复
            if (g_sgInSuspend) sgRealWake("resume_suspend");
        }
        return TRUE;

    case WM_TRAYICON:
        switch (LOWORD(l)) {
        case WM_LBUTTONUP:
            // 单击托盘图标：窗口隐藏则显示，已显示则隐藏到托盘（toggle）
            if (!IsWindowVisible(g_hwnd)) {
                SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                showWindowAnimated(g_hwnd, SW_SHOW);
            } else
                hideWindowAnimated(g_hwnd);
            ipc_emit("tray.click");
            break;
        case WM_LBUTTONDBLCLK:
            SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            showWindowAnimated(g_hwnd, SW_SHOW);
            ipc_emit("tray.doubleClick");
            break;
        case WM_RBUTTONUP: {
            ipc_emit("tray.rightClick");
            HMENU hMenu = CreatePopupMenu();
            bool hidden = !IsWindowVisible(g_hwnd);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW, hidden ? L"显示窗口" : L"隐藏窗口");
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_MIN, L"隐藏到托盘");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出");
            POINT pt;
            DWORD msgPos = GetMessagePos();
            pt.x = GET_X_LPARAM(msgPos);
            pt.y = GET_Y_LPARAM(msgPos);
            SetForegroundWindow(g_hwnd);
            TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
                           pt.x, pt.y, 0, g_hwnd, nullptr);
            // 经典要求：弹出菜单后发一个空消息，确保菜单能正确消失
            PostMessageW(g_hwnd, WM_NULL, 0, 0);
            DestroyMenu(hMenu);
            break;
        }
        }
        return 0;

    case WM_COMMAND: {
        int id = (int)LOWORD(w);
        if (id == ID_TRAY_SHOW) {
            if (!IsWindowVisible(g_hwnd)) {
                SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                showWindowAnimated(g_hwnd, SW_SHOW);
            } else
                hideWindowAnimated(g_hwnd);
        } else if (id == ID_TRAY_MIN) {
            hideWindowAnimated(g_hwnd);
        } else if (id == ID_TRAY_EXIT) {
            // 彻底退出：先移除托盘图标，再销毁窗口（WM_DESTROY 会做收尾并 PostQuitMessage）
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            g_trayActive = false;
            DestroyWindow(g_hwnd);
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
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Parse --dev <url>
    int argc;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"--dev") == 0 && i + 1 < argc) { g_devUrl = argv[i + 1]; }
        else if (wcscmp(argv[i], L"--minimized") == 0) { g_startMinimized = true; }
    }
    LocalFree(argv);

    // Load config
    g_cfg = loadConfig();
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
                showWindowAnimated(existing, IsIconic(existing) ? SW_RESTORE : SW_SHOW);
            }
            if (hMutex) CloseHandle(hMutex);
            CoUninitialize();
            return 0;
        }
    }
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
    wc.hbrBackground  = CreateSolidBrush(bgClr);
    wc.hIcon          = g_appIconLarge ? g_appIconLarge : LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm        = g_appIconSmall ? g_appIconSmall : wc.hIcon;
    RegisterClassExW(&wc);

    // Keep the standard overlapped window semantics even in frameless mode.
    // WM_NCCALCSIZE removes the visible chrome, while DWM keeps native animations.
    // WS_EX_TOOLWINDOW keeps the window out of the main taskbar: the app lives
    // only in the notification-area tray icon, which is the desired behavior.
    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;

    DWORD exStyle = WS_EX_TOOLWINDOW;
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
    if (g_hwnd) {
        if (g_appIconLarge) SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)g_appIconLarge);
        if (g_appIconSmall) SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_appIconSmall);
    }
    enableWindowTransitions(g_hwnd);

    // 全屏高度 + 右侧吸附模式：忽略历史窗口状态，按工作区重新计算布局
    if (g_fullHeight) {
        applyFullHeightLayout();
    } else {
    // Window state persistence — restore previous position/size
    g_stateFile = app_data_dir() + L"\\window-state.json";
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

    // Enable file drag-drop
    DragAcceptFiles(g_hwnd, TRUE);

    // 订阅 AC/DC 电源来源变化（推送式，零轮询）。系统会在注册后立即回调一次当前状态。
    g_acdcNotify = RegisterPowerSettingNotification(
        g_hwnd, &YM_GUID_ACDC_POWER_SOURCE, DEVICE_NOTIFY_WINDOW_HANDLE);
    // 显示器开关通知：S0 现代待机没有 S3，不会发 PBT_APMSUSPEND，改用显示器关闭/亮起
    // 作为「进入睡眠 / 唤醒」信号（进 DRIPS 前必关显示器）。S3 桌面机两条路径都覆盖。
    g_monitorNotify = RegisterPowerSettingNotification(
        g_hwnd, &YM_GUID_MONITOR_POWER_ON, DEVICE_NOTIFY_WINDOW_HANDLE);

    // ── 睡眠守护：加载持久化开关 + 孤儿恢复（上次崩溃残留的冻结进程）──
    sgInitNt();
    sgLoadConfig();
    {
        ProcessIdToSessionId(GetCurrentProcessId(), &g_sgSessionId);
        std::string en = sgReadFile(SG_DIR + L"\\Enable.txt");
        g_guardEnabled = (en == "1");
        if (fspath::exists(SG_DIR + L"\\suspended")) {
            // 启动即恢复任何上次未恢复的冻结进程并还原 TDP（标记先于冻结写，故安全）
            sgResumeAll(true);
        }
    }

    // ── 按键呼出：加载开关（后台手柄 LB+RB 0.5 秒呼出程序）──
    summonLoad();

    // ── 掌机前端自动关闭：加载开关 + 进程名列表（持久化于 config/autoclose.json）──
    autoCloseLoad();

    // ── 更新加速器：不会随程序自动启动，仅在前端手动触发 ──

    // Register all commands
    reg_window();
    reg_dialog();
    reg_fs();
    reg_clipboard();
    reg_shell_app();
    reg_tray();
    // 常驻任务栏：启动即创建通知区域（系统托盘）图标，窗口开/最小化都一直存在
    trayCreate(L"野蛮控制中心");
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
    reg_multiwindow();
    reg_extras();

    // ── 按键呼出：启动后台手柄轮询线程（即使窗口隐藏到托盘也持续检测 LB+RB 0.5 秒）──
    g_summonThread = CreateThread(nullptr, 0, gamepadSummonThread, nullptr, 0, nullptr);

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
        wchar_t eb[32];
        if (GetEnvironmentVariableW(L"YEMAN_ASYNC", eb, 32)) {
            int v = _wtoi(eb); if (v < 0) v = 0; if (v > 2) v = 2;
            g_asyncMode = v;
        }
        if (GetEnvironmentVariableW(L"YEMAN_POOL", eb, 32)) {
            int v = _wtoi(eb); if (v < 1) v = 1; if (v > 16) v = 16;
            g_poolSize = v;
        }
        if (GetEnvironmentVariableW(L"YEMAN_TRACE", eb, 32) && _wtoi(eb)) {
            traceInit();
            traceLog("BOOT async=%d pool=%d", g_asyncMode, g_poolSize);
            CreateThread(nullptr, 0, freezeMonitorThread, nullptr, 0, nullptr);
        }
    }

    init_webview();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hMutex) CloseHandle(hMutex);
    CoUninitialize();
    return (int)msg.wParam;
}
