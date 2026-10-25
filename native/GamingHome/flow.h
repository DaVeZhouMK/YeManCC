#pragma once
#include <cstdint>
#include <string_view>
namespace ymcc::gaminghome {
inline constexpr wchar_t Bat[] = L"C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.bat";
inline constexpr wchar_t Ymcc[] = L"C:\\SOFT\\YeMan\\YeManCC\\YeManCC.exe";
inline constexpr wchar_t Task[] = L"YeManCC-GamingHome";
inline constexpr wchar_t TaskOwner[] = L"YMCC Gaming Home demand start v1";
inline constexpr wchar_t SteamUri[] = L"steam://open/gamepadui";
inline constexpr std::uint64_t DeadlineMs = 60000;
inline constexpr std::uint64_t YmccDeadlineMs = 15000;
inline bool scheme(std::wstring_view arg, std::wstring_view prefix) {
    if (arg.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        const auto c = arg[i] >= L'A' && arg[i] <= L'Z' ? arg[i] + (L'a' - L'A') : arg[i];
        if (c != prefix[i]) return false;
    }
    return true;
}
inline bool activation(std::wstring_view arg) {
    return arg == L"--activate" || scheme(arg, L"gamingapp:") || scheme(arg, L"yemancc-home:");
}
inline bool windowCandidate(bool visible, bool sameSession, bool ownedImage,
                            bool sdlClass, bool appWindow, bool resizable, int width, int height) {
    return visible && sameSession && ownedImage && sdlClass && appWindow && !resizable && width >= 480 && height >= 270;
}
struct Snapshot { bool active = false, ymcc = false, steamWindow = false, cancelled = false; };
enum class Result { Success, Unsupported, OutsideFse, MissingFiles, Busy, TaskFailed, InternalError, YmccTimeout, BatFailed, UriFailed, Timeout, Cancelled };
// All effects go through these ports. Tests use the same production controller
// with memory ports; they never launch the product, Steam or Windows tasks.
struct Ports {
    virtual ~Ports() = default;
    virtual bool available() = 0;
    virtual bool selectedHome() = 0;
    virtual bool lock() = 0;
    virtual bool files() = 0;
    virtual Snapshot snapshot() = 0;
    virtual bool startYmccTask() = 0;
    virtual bool startBat() = 0;
    virtual bool showSteam() = 0;
    virtual bool focusSteam() = 0;
    virtual std::uint64_t now() = 0;
    virtual void wait() = 0;
    virtual void event(const char*) = 0;
};
inline Result run(Ports& p) {
    if (!p.available()) return Result::Unsupported;
    if (!p.selectedHome()) return Result::OutsideFse;
    if (!p.lock()) return Result::Busy;
    if (!p.files()) return Result::MissingFiles;
    auto s = p.snapshot();
    if (!s.active) return Result::OutsideFse;
    if (s.cancelled) return Result::Cancelled;
    const auto begin = p.now();
    if (!s.ymcc) {
        p.event("ymcc-task-request");
        if (!p.startYmccTask()) return Result::TaskFailed;
        while (!s.ymcc) {
            p.wait(); s = p.snapshot();
            if (s.cancelled || !s.active) return Result::Cancelled;
            if (p.now() - begin >= YmccDeadlineMs) return Result::YmccTimeout;
        }
        p.event("ymcc-process-observed"); // Not an input-readiness assertion.
    }
    if (s.steamWindow) {
        p.event("existing-steam-home-request");
        if (!p.showSteam()) return Result::UriFailed;
    } else {
        p.event("bat-request");
        if (!p.startBat()) return Result::BatFailed;
    }
    while (true) {
        s = p.snapshot();
        if (s.cancelled || !s.active) return Result::Cancelled;
        if (p.now() - begin >= DeadlineMs) return Result::Timeout;
        // A verified visible Steam window is only an HWND handoff receipt.
        // Geometry/foreground are never reported as Valve UI-mode evidence.
        if (s.ymcc && s.steamWindow && p.focusSteam()) {
            p.event("steam-window-handoff"); return Result::Success;
        }
        p.wait();
    }
}
inline const wchar_t* message(Result r) {
    switch (r) {
    case Result::Unsupported: return L"当前 Windows 未提供可用的全屏主页接口。";
    case Result::OutsideFse: return L"当前不在 Windows 全屏环境中。请从系统全屏主页进入 YMCC。";
    case Result::MissingFiles: return L"未找到固定的 YMCC 或 YeManSteam.bat，请检查 C:\\SOFT\\YeMan 安装目录。";
    case Result::InternalError: return L"YMCC 全屏主页遇到内部错误，请查看普通日志中的 steam-launch.log。";
    case Result::TaskFailed: return L"无法运行 YMCC 按需启动任务。请重新执行本组件的启用入口。";
    case Result::YmccTimeout: return L"15 秒内未观察到 YMCC 进程。请检查 YMCC 按需任务与启动日志。";
    case Result::BatFailed: return L"无法调用原 YeManSteam.bat。";
    case Result::UriFailed: return L"无法提交 Steam 大屏主页请求。";
    case Result::Timeout: return L"60 秒内未完成 Steam 窗口交接。请检查 YMCC 导出中的 steam-launch.log。";
    default: return L"";
    }
}
}
