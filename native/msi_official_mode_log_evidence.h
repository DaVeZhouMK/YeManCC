#pragma once
// Pure memory collector. Ops owns all platform access; no directory enumeration.
// roots(): up to three trusted known-folder roots, with the labels below.
// dates(): today and optionally yesterday, in either order (local log dates).
// read(path, cap): bounded tail bytes, with the original file size when known.
// hash(bytes): a 64-hex digest of exactly those returned bytes; no file reopening.
// now(): monotonic milliseconds. The 500ms budget is a BETWEEN-READ checkpoint,
// never a timeout/cancellation guarantee for an individual Ops invocation.
#include "json.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ymcc::msi::officialLogEvidence {
using json = nlohmann::json;
using Json = json;
struct Root { std::string label; std::wstring path; };
struct ReadResult {
    std::string bytes;
    bool available = false;
    bool truncated = false;
    std::uint64_t originalBytes = 0;
    std::string reason;
};
inline constexpr std::uint32_t fileByteLimit = 64u * 1024u;
inline constexpr std::uint32_t readLimit = 24;
inline constexpr std::uint64_t totalByteLimit = 1536u * 1024u;
inline constexpr std::uint64_t checkpointMs = 500;
inline constexpr std::size_t eventLimit = 32;

namespace detail {
inline const char* known(std::string_view s, std::initializer_list<const char*> values,
                         const char* fallback = "other") {
    for (const char* v : values) if (s == v) return v;
    return fallback;
}
inline int rootIndex(std::string_view label) {
    if (label == "program-data") return 0;
    if (label == "program-files-x86") return 1;
    if (label == "local-app-data") return 2;
    return -1;
}
inline const char* rootLabel(std::size_t i) {
    constexpr std::array<const char*, 3> labels{{"program-data", "program-files-x86", "local-app-data"}};
    return labels[i];
}
// Only ordinary absolute local drive paths. Reject device/UNC namespaces, ADS,
// dot segments and Win32 trailing-dot/space aliases, before appending literals.
inline bool rootPath(std::wstring& p) {
    if (p.size() < 3 || p.size() > 32700) return false;
    for (auto& c : p) if (c == L'/') c = L'\\';
    const auto c = p[0];
    if (!((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z')) ||
        p[1] != L':' || p[2] != L'\\') return false;
    if (p.size() > 3 && p.back() == L'\\') p.pop_back();
    std::size_t begin = 3;
    for (std::size_t i = 3; i <= p.size(); ++i) {
        if (i != p.size() && p[i] != L'\\') {
            const auto ch = p[i];
            if (ch < 32 || ch == 127 || ch == L':' || ch == L'<' || ch == L'>' ||
                ch == L'"' || ch == L'|' || ch == L'?' || ch == L'*') return false;
            continue;
        }
        if (i == 3 && p.size() == 3) return true;
        if (i == begin || p[i - 1] == L'.' || p[i - 1] == L' ') return false;
        const auto part = p.substr(begin, i - begin);
        if (part == L"." || part == L"..") return false;
        begin = i + 1;
    }
    return true;
}
inline int digits(std::string_view s) {
    int n = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') return -1;
        n = n * 10 + (c - '0');
    }
    return n;
}
inline int daysInMonth(int y, int m) {
    constexpr std::array<int, 12> days{{31,28,31,30,31,30,31,31,30,31,30,31}};
    if (m < 1 || m > 12) return 0;
    return days[static_cast<std::size_t>(m - 1)] +
        (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0) ? 1 : 0);
}
inline bool validDate(std::string_view d) {
    if (d.size() != 10 || d[4] != '-' || d[7] != '-') return false;
    const auto y = digits(d.substr(0, 4)), m = digits(d.substr(5, 2)), day = digits(d.substr(8, 2));
    return y >= 1 && m >= 1 && m <= 12 && day >= 1 && day <= daysInMonth(y, m);
}
inline bool previousDate(std::string_view older, std::string_view newer) {
    int y = digits(newer.substr(0, 4)), m = digits(newer.substr(5, 2)), d = digits(newer.substr(8, 2));
    if (--d == 0) { if (--m == 0) { m = 12; --y; } d = daysInMonth(y, m); }
    return digits(older.substr(0, 4)) == y && digits(older.substr(5, 2)) == m && digits(older.substr(8, 2)) == d;
}
inline json numericDate(std::string_view d) {
    return {{"year", digits(d.substr(0, 4))}, {"month", digits(d.substr(5, 2))}, {"day", digits(d.substr(8, 2))}};
}
inline json timestamp(std::string_view s) {
    if (s.size() != 23 || !validDate(s.substr(0, 10)) || s[10] != ' ' ||
        s[13] != ':' || s[16] != ':' || s[19] != '.') return nullptr;
    const auto h = digits(s.substr(11, 2)), m = digits(s.substr(14, 2)),
               sec = digits(s.substr(17, 2)), ms = digits(s.substr(20, 3));
    if (h < 0 || h > 23 || m < 0 || m > 59 || sec < 0 || sec > 59 || ms < 0) return nullptr;
    auto j = numericDate(s.substr(0, 10));
    j["hour"] = h; j["minute"] = m; j["second"] = sec; j["millisecond"] = ms;
    return j;
}
inline const char* caller(std::string_view s) {
    return known(s, {"WinEventProc", "PBT_APMRESUMESUSPEND", "Window_StateChanged", "WindowState.Minimized",
        "Window_IsKeyboardFocusWithinChanged", "GetFocusRepeatedly", "LaunchMcxOSD",
        "RetrySetControlModeInThread:WinEventProc", "RetrySetControlModeInThread:PBT_APMRESUMESUSPEND",
        "RetrySetControlModeInThread:Window_StateChanged", "RetrySetControlModeInThread:WindowState.Minimized",
        "RetrySetControlModeInThread:Window_IsKeyboardFocusWithinChanged",
        "RetrySetControlModeInThread:GetFocusRepeatedly", "RetrySetControlModeInThread:LaunchMcxOSD"});
}
inline bool starts(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}
inline json event(std::string_view line, std::uint32_t lineNumber) {
    json time = nullptr;
    // The module is discarded. An invalid timestamp can never become a number.
    if (line.size() > 24 && line[23] == ' ' && line[24] == '[') {
        time = timestamp(line.substr(0, 23)); line.remove_prefix(24);
    }
    if (!line.empty() && line.front() == '[') {
        const auto end = line.find("] ");
        if (end == std::string_view::npos) return nullptr;
        line.remove_prefix(end + 2);
    }
    json out = {{"lineInTail", lineNumber}, {"time", time}, {"sourceIdentity", "unknown"}, {"writerSuccessProven", false}};
    constexpr std::array<std::pair<std::string_view, const char*>, 3> patterns{{
        {"Switch Gamepad Mode (", "switch-gamepad-mode"},
        {"Gamepad Mode Updated (", "gamepad-mode-updated"},
        {"Device state changed: ", "device-state-changed"}
    }};
    for (std::size_t i = 0; i < patterns.size(); ++i) {
        if (!starts(line, patterns[i].first)) continue;
        auto values = line.substr(patterns[i].first.size());
        if (i != 2) {
            if (values.empty() || values.back() != ')') return nullptr;
            values.remove_suffix(1);
        }
        const auto comma = values.find(", ");
        if (comma == std::string_view::npos) return nullptr;
        out["kind"] = patterns[i].second;
        out["mode"] = known(values.substr(0, comma), {"Offline", "XInput", "DInput", "MSI", "Desktop", "BIOS", "TESTING"});
        out["mkeys"] = known(values.substr(comma + 2), {"Macro", "Combination", "Disabled"});
        return out;
    }
    constexpr std::string_view send = " Send ControlMode command ", result = " and result= ";
    const auto sep = line.find(send);
    if (sep == std::string_view::npos) return nullptr;
    const auto values = line.substr(sep + send.size());
    const auto resultSep = values.find(result);
    if (resultSep == std::string_view::npos) return nullptr;
    out["kind"] = "control-mode-command";
    out["caller"] = caller(line.substr(0, sep));
    out["command"] = known(values.substr(0, resultSep),
        {"GoToXInputMode", "GoToDesktopMode", "GotoMSIMode", "GoToMacroKeys", "GoToCombinationKeys"});
    out["result"] = values.substr(resultSep + result.size()) == "done" ? "done" : "other";
    return out;
}
inline void parse(json& file, const ReadResult& r, bool wholeFile) {
    const std::string_view bytes(r.bytes);
    // Without an offset in ReadResult, a tail's first fragment is unknowable.
    // Discard it conservatively, even if it looks like a complete event.
    const bool unknownHead = !wholeFile;
    file["headBoundary"] = unknownHead ? "unknown" : "file-start";
    file["headFragmentDiscarded"] = unknownHead && !bytes.empty();
    file["tailBoundary"] = bytes.empty() ? (wholeFile ? "empty-file" : "unknown") :
        (bytes.back() == '\n' ? "newline" : "partial-or-unterminated");
    file["tailFragmentDiscarded"] = !bytes.empty() && bytes.back() != '\n';
    std::uint32_t line = 1, seen = 0;
    std::size_t begin = 0;
    auto events = json::array();
    while (begin < bytes.size()) {
        const auto end = bytes.find('\n', begin);
        if (end == std::string_view::npos) break; // Never parse a terminal fragment.
        auto text = bytes.substr(begin, end - begin);
        if (!text.empty() && text.back() == '\r') text.remove_suffix(1);
        if (line == 1 && wholeFile && starts(text, "\xEF\xBB\xBF")) text.remove_prefix(3);
        if (!(line == 1 && unknownHead)) {
            auto value = event(text, line);
            if (!value.is_null()) {
                ++seen;
                if (events.size() == eventLimit) events.erase(events.begin());
                events.push_back(std::move(value));
            }
        }
        begin = end + 1; ++line;
    }
    file["completeLinesInTail"] = line - 1;
    file["eventsSeen"] = seen;
    file["eventsOmitted"] = seen - static_cast<std::uint32_t>(events.size());
    file["events"] = std::move(events);
}
inline bool digest(std::string& s) {
    if (s.size() != 64) return false;
    for (auto& c : s) {
        if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}
inline std::wstring path(const Root& r, std::string_view day, std::size_t role) {
    constexpr std::array<const wchar_t*, 4> prefixes{{
        L"API\\ControlMode\\MSI_Center_M_Server_ControlMode_", L"API\\GamePad\\MSI_Center_M_Server_GamePad_",
        L"UI\\MSI Center M_", L"API\\MSI_Center_M_Server_UserScenario_"}};
    std::wstring out = r.path;
    if (out.back() != L'\\') out += L'\\';
    out += L"MSI\\MSI_Center_M\\Log\\"; out += prefixes[role];
    for (const char c : day) out += static_cast<wchar_t>(c);
    out += role == 3 ? L"_AI.log" : L".log";
    return out;
}
inline const char* roleName(std::size_t i) {
    constexpr std::array<const char*, 4> names{{"control-mode", "gamepad", "ui", "user-scenario-ai"}};
    return names[i];
}
} // namespace detail

// No process-global state; disabled means exactly zero Ops calls.
template<class Ops> json collect(bool enabled, Ops& ops) {
    json out = {{"schema", 1}, {"enabled", enabled}, {"status", enabled ? "unknown" : "disabled"},
        {"scope", "official-fixed-log-tail-only"}, {"staticReferenceVersion", "3.0.2605.2101"},
        {"installedVersionMatch", "unknown"}, {"gamepadDetailLogging", "unknown"},
        {"sourceIdentity", "unknown"}, {"coverage", "unknown"}, {"planCompleted", false},
        {"proofs", {{"cause", false}, {"firmware", false}, {"injector", false}, {"sourceIdentity", false},
                    {"writerSuccess", false}, {"absence", false}}},
        {"limits", {{"roots", 3}, {"dates", 2}, {"fileBytes", fileByteLimit}, {"reads", readLimit},
                    {"totalBytes", totalByteLimit}, {"checkpointMs", checkpointMs}, {"eventsPerFile", eventLimit}}},
        {"budgetMeaning", "between-read-checkpoints-no-inflight-timeout"},
        {"timeBasis", "log-local-timezone-unknown"}, {"lineNumberBasis", "returned-tail"},
        {"rootPathPolicy", "absolute-local-drive-no-traversal"},
        {"beginTick", nullptr}, {"endTick", nullptr}, {"readAttempts", 0}, {"returnedBytes", 0},
        {"plannedReads", 0}, {"issues", json::array()}, {"files", json::array()}};
    if (!enabled) return out;
    bool degraded = false;
    auto issue = [&](const char* stage, const char* reason, std::uint64_t slot = 0) {
        degraded = true;
        out["issues"].push_back({{"stage", stage}, {"reason", reason}, {"slot", slot}, {"state", "unknown"}});
    };
    std::uint64_t begin = 0, last = 0;
    bool clockGood = true;
    try { begin = last = ops.now(); out["beginTick"] = begin; }
    catch (...) { issue("clock", "now-exception"); return out; }
    auto checkpoint = [&]() {
        std::uint64_t tick = 0;
        try { tick = ops.now(); out["endTick"] = tick; }
        catch (...) { issue("clock", "now-exception"); clockGood = false; return false; }
        if (tick < last) { issue("clock", "clock-regressed"); clockGood = false; return false; }
        last = tick;
        if (tick - begin >= checkpointMs) { issue("budget", "checkpoint-exhausted"); return false; }
        return true;
    };
    std::vector<Root> roots;
    std::vector<std::string> dates;
    try { roots = ops.roots(); } catch (...) { issue("roots", "roots-exception"); }
    try { dates = ops.dates(); } catch (...) { issue("dates", "dates-exception"); }
    std::array<Root, 3> valid{};
    std::array<bool, 3> present{}, duplicate{};
    bool dateOK = !dates.empty() && dates.size() <= 2;
    if (roots.empty()) issue("roots", "empty-roots");
    if (roots.size() > 3) issue("roots", "roots-limit");
    else for (std::size_t i = 0; i < roots.size(); ++i) {
        auto r = roots[i]; const int index = detail::rootIndex(r.label);
        if (index < 0) { issue("roots", "invalid-label", i); continue; }
        const auto slot = static_cast<std::size_t>(index);
        if (present[slot] || duplicate[slot]) {
            duplicate[slot] = true; present[slot] = false; issue("roots", "duplicate-root", i); continue;
        }
        if (!detail::rootPath(r.path)) { issue("roots", "invalid-root", i); continue; }
        valid[slot] = std::move(r); present[slot] = true;
    }
    if (dates.empty()) issue("dates", "empty-dates");
    if (dates.size() > 2) issue("dates", "dates-limit");
    else for (std::size_t i = 0; i < dates.size(); ++i) if (!detail::validDate(dates[i])) {
        dateOK = false; issue("dates", "invalid-date", i);
    }
    if (dateOK) {
        std::sort(dates.begin(), dates.end(), [](const auto& a, const auto& b) { return a > b; });
        if (dates.size() == 2 && !detail::previousDate(dates[1], dates[0])) {
            dateOK = false; issue("dates", "nonconsecutive-dates");
        }
    }
    std::uint32_t planned = 0, attempts = 0, available = 0;
    std::uint64_t returned = 0;
    for (const bool p : present) if (p && dateOK) planned += static_cast<std::uint32_t>(dates.size()) * 4;
    out["plannedReads"] = planned;
    bool stop = false;
    if (dateOK) for (const auto& day : dates) for (std::size_t root = 0; root < valid.size(); ++root) {
        if (!present[root]) continue;
        for (std::size_t role = 0; role < 4 && !stop; ++role) {
            if (attempts >= readLimit) { issue("budget", "read-limit"); stop = true; break; }
            if (returned >= totalByteLimit) { issue("budget", "byte-limit"); stop = true; break; }
            if (!checkpoint()) { stop = true; break; }
            const auto cap = static_cast<std::uint32_t>(std::min<std::uint64_t>(fileByteLimit, totalByteLimit - returned));
            json file = {{"root", detail::rootLabel(root)}, {"role", detail::roleName(role)},
                {"date", detail::numericDate(day)}, {"state", "unknown"}, {"reason", "unknown"},
                {"readAttempted", true}, {"available", false}, {"returnedBytes", 0}, {"originalBytes", nullptr},
                {"truncated", false}, {"shortRead", false}, {"wholeFileRead", false},
                {"hash", nullptr}, {"hashState", "unknown"}, {"hashAlgorithm", "unknown"},
                {"headBoundary", "unknown"}, {"tailBoundary", "unknown"},
                {"headFragmentDiscarded", false}, {"tailFragmentDiscarded", false},
                {"completeLinesInTail", 0}, {"eventsSeen", 0}, {"eventsOmitted", 0}, {"events", json::array()}};
            ++attempts;
            try {
                const auto r = ops.read(detail::path(valid[root], day, role), cap);
                file["available"] = r.available;
                if (r.available || r.originalBytes != 0) file["originalBytes"] = r.originalBytes;
                file["returnedBytes"] = r.bytes.size(); file["truncated"] = r.truncated;
                returned += static_cast<std::uint64_t>(r.bytes.size());
                const bool over = r.bytes.size() > cap;
                const bool inconsistent = r.available && r.originalBytes < r.bytes.size();
                const bool shortRead = r.available && r.bytes.size() < std::min<std::uint64_t>(r.originalBytes, cap);
                file["shortRead"] = shortRead;
                if (over) {
                    // Do not hash/parse unbounded bytes supplied by a broken Ops.
                    file["reason"] = "read-over-cap"; issue("read", "read-over-cap", attempts - 1); stop = true;
                } else {
                    try {
                        auto h = ops.hash(r.bytes);
                        if (detail::digest(h)) { file["hash"] = h; file["hashState"] = "returned-bytes"; }
                        else { file["hashState"] = "invalid-digest"; issue("hash", "invalid-digest", attempts - 1); }
                    } catch (...) { file["hashState"] = "hash-exception"; issue("hash", "hash-exception", attempts - 1); }
                    if (!r.available) {
                        file["reason"] = detail::known(r.reason, {"not-found", "access-denied", "sharing-violation", "io-error", "missing-or-not-regular-file", "read-denied", "size-unavailable", "read-failed"}, "unknown");
                        degraded = true;
                    } else if (inconsistent) {
                        file["reason"] = "inconsistent-size"; issue("read", "inconsistent-size", attempts - 1);
                    } else {
                        ++available;
                        const bool whole = !r.truncated && !shortRead && r.originalBytes == r.bytes.size();
                        file["wholeFileRead"] = whole;
                        file["state"] = whole ? "available" : "partial";
                        file["reason"] = shortRead ? "short-read" : (whole ? "none" : "tail-only");
                        detail::parse(file, r, whole);
                        if (!whole || file["tailFragmentDiscarded"] == true) degraded = true;
                    }
                }
            } catch (...) { file["reason"] = "read-exception"; issue("read", "read-exception", attempts - 1); }
            out["files"].push_back(std::move(file));
        }
    }
    out["readAttempts"] = attempts; out["returnedBytes"] = returned;
    if (clockGood && !stop) (void)checkpoint();
    out["planCompleted"] = planned != 0 && attempts == planned && !stop;
    if (available != 0) out["status"] = degraded ? "partial" : "collected";
    return out;
}
} // namespace ymcc::msi::officialLogEvidence
