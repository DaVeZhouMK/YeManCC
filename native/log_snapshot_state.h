#pragma once
#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include "json.hpp"

namespace ymcc::logging {
using SnapshotJson = nlohmann::json;
enum class SnapshotKind { full, oemWake, hud, screen, sleepInspection, throttle, queue, revision };

// Exact structured comparison, no hashes or serialized copies on unchanged
// observations. Ignore only documented counters at their specified object level.
inline bool snapshotCounter(std::string_view key, SnapshotKind kind) noexcept {
    if (kind == SnapshotKind::oemWake) return key == "requestedEpoch" || key == "durationMs";
    if (kind == SnapshotKind::hud) return key == "seq";
    if (kind == SnapshotKind::revision) return key == "revision";
    if (kind == SnapshotKind::throttle) return key == "waitMs";
    if (kind == SnapshotKind::queue) return key == "enqueueCount" || key == "sinceRequestMs" || key == "queueDepth";
    if (kind != SnapshotKind::screen) return false;
    for (const auto name : {"events", "injectedInputs", "repaints", "cursorFallbackShows",
        "cursorFallbackHides", "cursorFallbackHandoffs", "cursorFallbackSyncs", "cursorFallbackUploads",
        "standaloneSpecialRequests", "standaloneSpecialCompleted"}) if (key == name) return true;
    return false;
}
inline bool snapshotValueEqual(const SnapshotJson& a, const SnapshotJson& b) {
    // Keep type changes visible (JSON's ordinary numeric == accepts 1 == 1.0).
    if (a.type() != b.type()) return false;
    if (a.is_primitive()) {
        if (a.is_number_float() && a == b)
            return std::signbit(a.get<double>()) == std::signbit(b.get<double>());
        return a == b;
    }
    if (a.size() != b.size()) return false;
    auto ai = a.begin(), bi = b.begin();
    for (; ai != a.end(); ++ai, ++bi) {
        if (a.is_object() && ai.key() != bi.key()) return false;
        if (!snapshotValueEqual(ai.value(), bi.value())) return false;
    }
    return true;
}
// Telemetry paths are exact, not recursive name wildcards. Unknown fields and
// failures remain semantic. Complete trajectories are retained in detail rows.
inline bool observationTelemetryKey(std::string_view path, std::string_view key, SnapshotKind kind) noexcept {
    if (kind == SnapshotKind::sleepInspection)
        return path == "nativePrimary.log" && (key == "copiedBytes" || key == "originalBytes");
    if (kind != SnapshotKind::screen) return false;
    if (path.empty()) {
        if (snapshotCounter(key, kind)) return true;
        for (const auto name : {"cursorFallbackQueries", "cursorDeckReportPulseTick", "cursorDeckDeferredMoves",
            "cursorDeckMovePostReportDeferrals", "contactGeometryHits", "contactGeometryInvalidations",
            "contactGeometryReads", "contactVisualMoves", "contactVisualUploads", "outlineUploads",
            "autoHideWakeups", "autoHideTransitions", "cursorDeckForcedShows", "cursorDeckForcedHiddenSamples"})
            if (key == name) return true;
    }
    if (path == "cursorTransitionTrace") return key == "entries" || key == "total";
    if (path == "cursorTransitionTrace.placements")
        return key == "unchanged" || key == "lastRaiseTick" || key == "deckRaises" || key == "setWindowPos";
    return path == "cursorVisibilityContext" && key == "gestures";
}
inline bool observationEqual(const SnapshotJson& a, const SnapshotJson& b, SnapshotKind kind, std::string_view path = {}) {
    if (!a.is_object() || !b.is_object()) return snapshotValueEqual(a, b);
    auto ai = a.begin(), bi = b.begin();
    const auto skip = [&](const auto& it, const SnapshotJson& peer) {
        if (!observationTelemetryKey(path, it.key(), kind)) return false;
        const auto other = peer.find(it.key());
        if (other == peer.end() || it.value().type() != other->type()) return false;
        if (path == "cursorTransitionTrace" && it.key() == "entries") return it.value().is_array();
        // A missing/wrong-type/unavailable counter is evidence, not noise.
        return it.value().is_number_unsigned() ||
            (it.value().is_number_integer() && it.value().template get<long long>() >= 0);
    };
    for (;;) {
        while (ai != a.end() && skip(ai, b)) ++ai;
        while (bi != b.end() && skip(bi, a)) ++bi;
        if (ai == a.end() || bi == b.end()) return ai == a.end() && bi == b.end();
        if (ai.key() != bi.key()) return false;
        // Recurse only through known containers: no per-record path allocation.
        std::string_view child;
        if (kind == SnapshotKind::screen) {
            if (path.empty() && ai.key() == "cursorTransitionTrace") child = "cursorTransitionTrace";
            else if (path == "cursorTransitionTrace" && ai.key() == "placements") child = "cursorTransitionTrace.placements";
            else if (path.empty() && ai.key() == "cursorVisibilityContext") child = "cursorVisibilityContext";
        } else if (kind == SnapshotKind::sleepInspection) {
            if (path.empty() && ai.key() == "nativePrimary") child = "nativePrimary";
            else if (path == "nativePrimary" && ai.key() == "log") child = "nativePrimary.log";
        }
        if (child.empty() ? !snapshotValueEqual(ai.value(), bi.value()) : !observationEqual(ai.value(), bi.value(), kind, child)) return false;
        ++ai; ++bi;
    }
}
inline bool snapshotEqual(const SnapshotJson& a, const SnapshotJson& b, SnapshotKind kind, bool root = true) {
    if (kind == SnapshotKind::sleepInspection) return observationEqual(a, b, kind);
    if (!root && kind == SnapshotKind::screen) return observationEqual(a, b, kind);
    if (!a.is_object() || !b.is_object()) return snapshotValueEqual(a, b);
    auto ai = a.begin(), bi = b.begin();
    const auto skip = root ? (kind == SnapshotKind::oemWake || kind == SnapshotKind::throttle || kind == SnapshotKind::queue || kind == SnapshotKind::revision ? kind : SnapshotKind::full) : kind;
    for (;;) {
        while (ai != a.end() && snapshotCounter(ai.key(), skip)) ++ai;
        while (bi != b.end() && snapshotCounter(bi.key(), skip)) ++bi;
        if (ai == a.end() || bi == b.end()) return ai == a.end() && bi == b.end();
        if (ai.key() != bi.key()) return false;
        const bool nested = root && ((kind == SnapshotKind::hud && ai.key() == "hud") ||
                                     (kind == SnapshotKind::screen && ai.key() == "state"));
        if (nested ? !snapshotEqual(ai.value(), bi.value(), kind, false) : !snapshotValueEqual(ai.value(), bi.value())) return false;
        ++ai; ++bi;
    }
}
struct JsonSnapshotLogState {
    SnapshotJson value;
    unsigned long long generation = 0;
    bool known = false;
    bool observe(const SnapshotJson& next, unsigned long long nextGeneration, SnapshotKind kind = SnapshotKind::full) {
        if (known && generation == nextGeneration && snapshotEqual(value, next, kind)) return false;
        value = next; generation = nextGeneration; known = true;
        return true;
    }
};

inline std::string_view logJsonText(const SnapshotJson& object, const char* key, std::string_view fallback = {}) {
    const auto it = object.find(key);
    // Match json.value string semantics: a wrong type must reach the existing
    // diagnostic exception counter, never masquerade as a missing field.
    return it != object.end() ? std::string_view(it->get_ref<const std::string&>()) : fallback;
}
// References live only for the current observation. Owned strings are updated
// only after an emitted row. The hot unchanged-frame path allocates nothing.
struct FrameEvidenceView {
    std::array<std::string_view, 4> text;
    std::array<unsigned long long, 5> numbers;
    std::array<bool, 3> flags;
};
struct FrameEvidenceLogState {
    std::array<std::string, 4> text;
    std::array<unsigned long long, 5> numbers{};
    std::array<bool, 3> flags{};
    bool known = false;
    bool changed(const FrameEvidenceView& next) const noexcept {
        if (!known || numbers != next.numbers || flags != next.flags) return true;
        for (size_t i = 0; i < text.size(); ++i) if (text[i] != next.text[i]) return true;
        return false;
    }
    void remember(const FrameEvidenceView& next) {
        for (size_t i = 0; i < text.size(); ++i) text[i].assign(next.text[i]);
        numbers = next.numbers; flags = next.flags; known = true;
    }
};
}
