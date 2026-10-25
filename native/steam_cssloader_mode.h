#pragma once
#include <windows.h>
#include "steam_ui_epoch.h"
#include <algorithm>
#include <string>
#include <utility>

namespace ymcc::steamcss {
// CSS can reconcile an existing Big Picture session without any permission
// to launch Steam, switch modes, reload its JS context or attach Decky.
template<class SteamSession, class Snapshot>
inline bool appearanceModeReady(const SteamSession& session, const Snapshot& state) {
    try {
        const auto pid = state.find("pid");
        return session.pid && session.created && !session.root.empty() && state.is_object() &&
            pid != state.end() && pid->is_number_integer() &&
            (pid->is_number_unsigned() ? pid->template get<uint64_t>() == session.pid : pid->template get<int64_t>() == session.pid) &&
            state.at("known") == true && state.at("running") == true && state.at("mode") == "bigpicture" &&
            state.at("birth") == std::to_string(session.created);
    } catch (...) { return false; }
}
// At most two exit-triggered recovery waves per verified Steam process.
// A normal/unknown exit is never a restart grant. Only a different Steam birth
// or an explicit manual action resets this budget; mode/Ready/link hints do not.
class ExitRecovery {
    DWORD delay_; unsigned used_ = 0; ULONGLONG due_ = 0; bool pending_ = false;
public:
    explicit ExitRecovery(DWORD delay = 30000) : delay_(delay) {}
    static bool abnormal(bool known, DWORD code) { return known && code != 0 && code != STILL_ACTIVE && code != 0xc000013aUL; }
    bool admit(ULONGLONG now) { if (pending_ || used_ >= 2) return false; ++used_; due_ = now + delay_; pending_ = true; return true; }
    void reset() { used_ = 0; cancel(); }
    void cancel() { pending_ = false; due_ = 0; }
    bool pending() const { return pending_; }
    unsigned used() const { return used_; }
    DWORD timeout(ULONGLONG now) const { return !pending_ ? INFINITE : now >= due_ ? 0 : static_cast<DWORD>(due_ - now); }
    bool take(ULONGLONG now) { if (timeout(now) != 0) return false; cancel(); return true; }
};
enum class UiMode { Unknown, Desktop, BigPicture };
struct ModeSample {
    UiMode mode; std::string epoch;
    ModeSample(UiMode value = UiMode::Unknown, std::string entry = {}) : mode(value), epoch(std::move(entry)) {}
};
// Window events request a finite read window. No event means no mode query.
// One wave coalesces events, observes at most three times and then parks.
class ModeWindow {
    ULONGLONG due_ = 0, nextAllowed_ = 0, requestedUntil_ = 0;
    DWORD debounce_, gap_, failureGap_;
    unsigned attempts_ = 0;
    bool armed_ = false, requested_ = false;
    UiMode last_ = UiMode::Unknown; std::string epoch_;
public:
    explicit ModeWindow(DWORD debounce = 600, DWORD gap = 5000, DWORD failureGap = 30000)
        : debounce_(debounce), gap_(gap), failureGap_(failureGap) {}
    void reset() { due_ = nextAllowed_ = requestedUntil_ = 0; attempts_ = 0; armed_ = requested_ = false; last_ = UiMode::Unknown; epoch_.clear(); }
    // A manual read-only verification must suspend pending reads while keeping
    // the last mode/document, so duplicate completions cannot create an entry.
    void park() { due_ = requestedUntil_ = 0; attempts_ = 0; armed_ = requested_ = false; }
    void request(ULONGLONG now) {
        if (armed_) return; // More name/show events cannot postpone or multiply a pending read.
        attempts_ = 0; armed_ = true; due_ = std::max(now + debounce_, nextAllowed_);
    }
    void recoveryEntry(ULONGLONG now) { requested_ = true; requestedUntil_ = now + 30000; request(now); }
    void manualEntry(ULONGLONG now) { recoveryEntry(now); }
    void resumeEntry(ULONGLONG now) { recoveryEntry(now); }
    DWORD timeout(ULONGLONG now) const { return !armed_ ? INFINITE : now >= due_ ? 0 : static_cast<DWORD>(due_ - now); }
    bool take(ULONGLONG now) {
        if (timeout(now) != 0) return false;
        armed_ = false; ++attempts_; nextAllowed_ = now + gap_; return true;
    }
    // The shared reader already did the debounced, rate-limited CDP read.
    // Accept its verified result now; do not impose a second five-second gap.
    bool confirmed(ULONGLONG now, const ModeSample& sample) {
        armed_ = false; attempts_ = 0; nextAllowed_ = std::max(nextAllowed_, now + gap_);
        return finish(now, sample);
    }
    bool finish(ULONGLONG now, const ModeSample& sample) {
        const auto mode = sample.mode;
        if (mode == UiMode::Unknown) {
            if (attempts_ < 3) { armed_ = true; due_ = std::max(now + gap_, nextAllowed_); }
            else nextAllowed_ = std::max(nextAllowed_, now + failureGap_);
            return false; // Unknown cannot manufacture a desktop->bigpicture edge.
        }
        const auto previousDocument = ymcc::steamui::parseModeEpoch(epoch_);
        const auto currentDocument = ymcc::steamui::parseModeEpoch(sample.epoch);
        // A replaced CEF document loses its styles while Steam PID/mode stay
        // unchanged. Reapply CSS once; this never grants Decky or Steam startup.
        const bool documentChanged = previousDocument.valid && currentDocument.valid && previousDocument.document != currentDocument.document;
        const bool entered = mode == UiMode::BigPicture && (last_ != mode || documentChanged || ymcc::steamui::laterModeEntry(epoch_, sample.epoch)) && !(requested_ && now < requestedUntil_);
        last_ = mode; if (ymcc::steamui::updateModeEpoch(epoch_, sample.epoch)) epoch_ = sample.epoch; requested_ = false; return entered;
    }
};
}
