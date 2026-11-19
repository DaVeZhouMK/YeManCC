#pragma once
#include <cstdint>
#include "../steam_startup_window_facts.h"
#include <functional>
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
    return ymcc::steaminfo::startupWindowCandidate(visible,sameSession,ownedImage,sdlClass,appWindow,resizable,width,height);
}
enum class SteamPresence { Unknown, Absent, Running };
inline SteamPresence steamPresence(bool available,bool completed,int presence,std::uint32_t pid,std::uint64_t birth) noexcept {
    if (!available || !completed) return SteamPresence::Unknown;
    if (presence == 0 && !pid && !birth) return SteamPresence::Absent;
    if (presence == 1 && pid && birth) return SteamPresence::Running;
    return SteamPresence::Unknown;
}
struct Snapshot {
    bool active = false, ymcc = false, steamWindow = false, cancelled = false;
    SteamPresence presence = SteamPresence::Unknown;
};
enum class Action { EnsureYmcc, StartSteam, ShowHome, FocusWindow };
using Current = std::function<bool()>;
enum class Result { Success, Unsupported, OutsideFse, MissingFiles, Busy, TaskFailed, InternalError, YmccTimeout, BatFailed, UriFailed, Timeout, Cancelled, ActionUnknown };
// All effects go through these ports. Tests use the same production controller
// with memory ports; they never launch the product, Steam or Windows tasks.
struct Ports {
    virtual ~Ports() = default;
    virtual bool available() = 0;
    virtual bool selectedHome() = 0;
    virtual bool lock() = 0;
    virtual bool files() = 0;
    virtual Snapshot snapshot() = 0;
    // Same activation's final owner check; no Steam/account/UI authority.
    virtual bool current(Action) = 0;
    // Synchronous fixed outlet. Do not retain Current beyond this call.
    virtual bool dispatch(Action, const Current&) = 0;
    virtual void begin(std::uint64_t) {}
    virtual bool uncertain() const { return false; }
    virtual bool commandBusy() const { return false; }
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
    p.begin(begin);
    const auto admission = [&](Action action) {
        if (!p.current(action)) return Result::Cancelled;
        const auto now = p.now();
        const auto budget = action == Action::EnsureYmcc ? YmccDeadlineMs : DeadlineMs;
        if (now < begin || now - begin >= budget)
            return action == Action::EnsureYmcc ? Result::YmccTimeout : Result::Timeout;
        return Result::Success;
    };
    const auto dispatch = [&](Action action, Result failure) {
        auto gate = admission(action);
        if (gate != Result::Success) return gate;
        const bool submitted = p.dispatch(action, [&] { return admission(action) == Result::Success; });
        if (submitted) return Result::Success;
        if (p.commandBusy()) return Result::Busy; // Terminal for this activation; no focus retry/fallback.
        gate = admission(action);
        if (gate != Result::Success) return gate;
        return p.uncertain() ? Result::ActionUnknown : failure;
    };
    if (!s.ymcc) {
        p.event("ymcc-task-request");
        const auto submitted = dispatch(Action::EnsureYmcc, Result::TaskFailed);
        if (submitted != Result::Success) return submitted;
        while (!s.ymcc) {
            p.wait(); s = p.snapshot();
            if (s.cancelled || !s.active) return Result::Cancelled;
            if (p.now() - begin >= YmccDeadlineMs) return Result::YmccTimeout;
        }
        p.event("ymcc-process-observed"); // Not an input-readiness assertion.
    }
    // Wait for a completed public observation. No window is not process absence.
    while (s.presence == SteamPresence::Unknown) {
        const auto gate = admission(Action::StartSteam);
        if (gate != Result::Success) return gate;
        p.wait(); s = p.snapshot();
        if (s.cancelled || !s.active) return Result::Cancelled;
        if (p.now() < begin || p.now() - begin >= DeadlineMs) return Result::Timeout;
    }
    if (s.presence == SteamPresence::Running) {
        p.event("existing-steam-home-request");
        const auto submitted = dispatch(Action::ShowHome, Result::UriFailed);
        if (submitted != Result::Success) return submitted;
    } else {
        p.event("bat-request");
        const auto submitted = dispatch(Action::StartSteam, Result::BatFailed);
        if (submitted != Result::Success) return submitted;
    }
    while (true) {
        s = p.snapshot();
        if (s.cancelled || !s.active) return Result::Cancelled;
        if (p.now() - begin >= DeadlineMs) return Result::Timeout;
        // HWND handoff is not Valve UI-mode, account or input evidence.
        if (s.ymcc && s.steamWindow) {
            const auto focused = dispatch(Action::FocusWindow, Result::InternalError);
            if (focused == Result::Success) { p.event("steam-window-handoff"); return Result::Success; }
            if (focused != Result::InternalError) return focused;
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
    case Result::ActionUnknown: return L"Steam 请求结果尚未确认，已停止本次自动操作，请查看 steam-launch.log。";
    default: return L"";
    }
}
}
