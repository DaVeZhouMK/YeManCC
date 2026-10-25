#pragma once
// Ephemeral admission for linked loaders, NOT a Steam startup command.
// Saved enabled/theme flags never authorize attaching/reinjecting on YMCC restart.
#include "steam_session_observer.h"
#include "steam_ui_epoch.h"
#include <mutex>
namespace ymcc::steamlink {
class Admission {
    mutable std::mutex mutex_;
    ULONGLONG applicationCreated_ = 0;
    steamsession::Session manual_, desktop_, observed_, entered_;
    enum class Mode { Unknown, Desktop, BigPicture };
    Mode lastMode_ = Mode::Unknown;
    std::string lastEpoch_;
    static bool identity(const steamsession::Session& a, const steamsession::Session& b) {
        return a.pid && a.created && a.pid == b.pid && a.created == b.created && a.root == b.root;
    }
public:
    explicit Admission(ULONGLONG created = 0) : applicationCreated_(created) {}
    bool allows(const steamsession::Session& steam) const {
        std::lock_guard lock(mutex_);
        if (!steam.pid || !steam.created || steam.root.empty() || identity(steam, desktop_)) return false;
        // A Steam process genuinely started after YMCC may be followed by the
        // existing missing-only CSS/sidebar recovery. YMCC itself never starts it.
        return identity(steam, manual_) || identity(steam, entered_) || (applicationCreated_ && steam.created > applicationCreated_);
    }
    // The first known mode is a baseline, never an entry. Unknown results and
    // a new CEF document/epoch do not manufacture a desktop -> Big Picture edge.
    // Only a later verified mode change in the same Steam process admits use.
    bool observeMode(const steamsession::Session& steam, const std::string& mode, const std::string& epoch = {}) {
        if (!steam.pid || !steam.created || steam.root.empty() || (mode != "desktop" && mode != "bigpicture")) return false;
        std::lock_guard lock(mutex_);
        if (!identity(steam, observed_)) { observed_ = steam; entered_ = {}; lastMode_ = Mode::Unknown; lastEpoch_.clear(); }
        const auto next = mode == "bigpicture" ? Mode::BigPicture : Mode::Desktop;
        const bool entered = next == Mode::BigPicture && lastMode_ != Mode::Unknown &&
            (lastMode_ == Mode::Desktop || steamui::laterModeEntry(lastEpoch_, epoch));
        if (entered) { entered_ = steam; desktop_ = {}; }
        if (next == Mode::Desktop) { entered_ = {}; if (lastMode_ == Mode::BigPicture) manual_ = {}; }
        lastMode_ = next; if (steamui::updateModeEpoch(lastEpoch_, epoch)) lastEpoch_ = epoch;
        return entered;
    }
    bool manual(const steamsession::Session& steam) {
        if (!steam.pid || !steam.created || steam.root.empty()) return false;
        std::lock_guard lock(mutex_); manual_ = steam; desktop_ = {}; return true;
    }
    void desktop(const steamsession::Session& steam) { std::lock_guard lock(mutex_); manual_ = {}; desktop_ = steam; }
    void observe(const steamsession::Session& steam) {
        std::lock_guard lock(mutex_);
        if (!identity(steam, manual_)) manual_ = {};
        if (!identity(steam, desktop_)) desktop_ = {};
        if (!identity(steam, entered_)) entered_ = {};
        if (!identity(steam, observed_)) { observed_ = {}; lastMode_ = Mode::Unknown; lastEpoch_.clear(); }
    }
};
inline ULONGLONG currentProcessCreated() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
    return (static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}
}
