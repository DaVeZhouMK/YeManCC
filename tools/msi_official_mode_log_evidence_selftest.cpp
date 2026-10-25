#include "../native/msi_official_mode_log_evidence.h"
#include <functional>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace ymcc::msi::officialLogEvidence;
namespace {
std::uint64_t checks = 0;
void check(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
std::string logLine(const std::string& body, const std::string& time = "2026-10-15 12:34:56.789") {
    return time + " [private-module-never-export] " + body + "\r\n";
}
ReadResult full(std::string bytes) {
    ReadResult r; r.originalBytes = bytes.size(); r.bytes = std::move(bytes); r.available = true; return r;
}
struct MemoryOps {
    std::vector<Root> rootValues{{"program-data", L"C:\\KnownData"}};
    std::vector<std::string> dateValues{"2026-10-15"};
    std::vector<std::wstring> paths;
    std::vector<std::uint32_t> caps;
    std::vector<std::string> hashInputs;
    std::function<ReadResult(const std::wstring&, std::uint32_t)> supply;
    std::uint64_t tick = 100, readAdvance = 0;
    unsigned rootsCalls = 0, datesCalls = 0, nowCalls = 0;
    unsigned failNowAt = 0, failReadAt = 0, failHashAt = 0;
    bool failRoots = false, failDates = false, regress = false;
    std::string hashValue = std::string(64, 'a');
    std::vector<Root> roots() {
        ++rootsCalls;
        if (failRoots) throw std::runtime_error("PRIVATE roots exception path");
        return rootValues;
    }
    std::vector<std::string> dates() {
        ++datesCalls;
        if (failDates) throw std::runtime_error("PRIVATE dates exception path");
        return dateValues;
    }
    std::uint64_t now() {
        ++nowCalls;
        if (failNowAt == nowCalls) throw std::runtime_error("PRIVATE clock exception");
        if (regress && nowCalls > 1) return tick - 1;
        return tick;
    }
    ReadResult read(const std::wstring& path, std::uint32_t cap) {
        paths.push_back(path); caps.push_back(cap); tick += readAdvance;
        if (failReadAt == paths.size()) throw std::runtime_error("PRIVATE read error C:\\Users\\Alice");
        return supply ? supply(path, cap) : full(logLine("Switch Gamepad Mode (XInput, Macro)"));
    }
    std::string hash(const std::string& bytes) {
        hashInputs.push_back(bytes);
        if (failHashAt == hashInputs.size()) throw std::runtime_error("PRIVATE hash error");
        return hashValue;
    }
};
MemoryOps allRoots() {
    MemoryOps o;
    o.rootValues = {{"local-app-data", L"E:/Users/PrivateName/Local/"},
                    {"program-files-x86", L"D:\\Program Files (x86)"},
                    {"program-data", L"C:\\ProgramData"}};
    o.dateValues = {"2026-10-14", "2026-10-15"};
    return o;
}
const json& first(const json& j) { return j.at("files").at(0); }
const json& events(const json& j) { return first(j).at("events"); }
bool hasIssue(const json& j, const char* reason) {
    for (const auto& issue : j.at("issues")) if (issue.at("reason") == reason) return true;
    return false;
}
void keys(const json& j, std::initializer_list<const char*> expected) {
    std::set<std::string> want, got;
    for (const auto* key : expected) want.insert(key);
    for (auto it = j.begin(); it != j.end(); ++it) got.insert(it.key());
    check(j.is_object() && got == want, "fixed schema keys");
}
void numericTime(const json& t, bool withClock) {
    if (t.is_null()) return;
    if (withClock) keys(t, {"year", "month", "day", "hour", "minute", "second", "millisecond"});
    else keys(t, {"year", "month", "day"});
    for (const auto& v : t) check(v.is_number_integer() && v.get<int>() >= 0, "numeric time only");
}
void schemaPrivacy(const json& j) {
    keys(j, {"schema", "enabled", "status", "scope", "staticReferenceVersion", "installedVersionMatch",
        "gamepadDetailLogging", "sourceIdentity", "coverage", "planCompleted", "proofs", "limits",
        "budgetMeaning", "timeBasis", "lineNumberBasis", "rootPathPolicy", "beginTick", "endTick",
        "readAttempts", "returnedBytes", "plannedReads", "issues", "files"});
    check(j["files"].size() <= readLimit && j["issues"].size() <= 40, "bounded output collections");
    keys(j["proofs"], {"cause", "firmware", "injector", "sourceIdentity", "writerSuccess", "absence"});
    for (const auto& value : j["proofs"]) check(value == false, "no cause firmware injector writer or absence proof");
    check(j["installedVersionMatch"] == "unknown" && j["coverage"] == "unknown" &&
          j["gamepadDetailLogging"] == "unknown" && j["sourceIdentity"] == "unknown", "provenance remains unknown");
    const std::set<std::string> reasons = {"unknown", "none", "short-read", "tail-only", "not-found", "access-denied",
        "sharing-violation", "io-error", "missing-or-not-regular-file", "read-denied", "size-unavailable", "read-failed", "read-exception", "read-over-cap", "inconsistent-size"};
    for (const auto& f : j["files"]) {
        keys(f, {"root", "role", "date", "state", "reason", "readAttempted", "available", "returnedBytes",
            "originalBytes", "truncated", "shortRead", "wholeFileRead", "hash", "hashState", "hashAlgorithm",
            "headBoundary", "tailBoundary", "headFragmentDiscarded", "tailFragmentDiscarded", "completeLinesInTail",
            "eventsSeen", "eventsOmitted", "events"});
        check(f["root"] == "program-data" || f["root"] == "program-files-x86" || f["root"] == "local-app-data", "root whitelist");
        check(reasons.count(f["reason"].get<std::string>()) != 0, "reason enum only");
        numericTime(f["date"], false);
        if (!f["hash"].is_null()) {
            const auto h = f["hash"].get<std::string>();
            check(h.size() == 64 && h.find_first_not_of("abcdef0123456789") == std::string::npos, "digest encoding only");
        }
        check(f["events"].size() <= eventLimit, "bounded event output");
        for (const auto& e : f["events"]) {
            numericTime(e["time"], true);
            check(e["lineInTail"].is_number_unsigned(), "tail line index numeric");
            check(e["sourceIdentity"] == "unknown" && e["writerSuccessProven"] == false, "updated not writer proof");
            if (e["kind"] == "control-mode-command") {
                keys(e, {"kind", "lineInTail", "time", "sourceIdentity", "writerSuccessProven", "caller", "command", "result"});
                check(e["result"] == "done" || e["result"] == "other", "result enum only");
                check(std::string(detail::caller(e["caller"].get<std::string>())) == e["caller"].get<std::string>(), "caller enum only");
                check(std::string(detail::known(e["command"].get<std::string>(),
                    {"GoToXInputMode", "GoToDesktopMode", "GotoMSIMode", "GoToMacroKeys", "GoToCombinationKeys"})) == e["command"].get<std::string>(), "command enum only");
            } else {
                keys(e, {"kind", "lineInTail", "time", "sourceIdentity", "writerSuccessProven", "mode", "mkeys"});
                check(e["kind"] == "switch-gamepad-mode" || e["kind"] == "gamepad-mode-updated" ||
                    e["kind"] == "device-state-changed", "event kind enum only");
                check(std::string(detail::known(e["mode"].get<std::string>(),
                    {"Offline", "XInput", "DInput", "MSI", "Desktop", "BIOS", "TESTING"})) == e["mode"].get<std::string>(), "mode enum only");
                check(std::string(detail::known(e["mkeys"].get<std::string>(), {"Macro", "Combination", "Disabled"})) == e["mkeys"].get<std::string>(), "mkeys enum only");
            }
        }
    }
    const auto text = j.dump();
    for (const char* privateText : {"PRIVATE", "PrivateName", "Alice", "private-module-never-export", "C:\\\\", "MSI_Center_M_Server_", ".log", "GAME_LIST", "WINDOW_TITLE"})
        check(text.find(privateText) == std::string::npos, "no sensitive text in schema");
}
void zeroGate() {
    MemoryOps o; o.failRoots = o.failDates = true; o.failNowAt = 1;
    const auto j = collect(false, o);
    check(o.rootsCalls + o.datesCalls + o.nowCalls + o.paths.size() + o.hashInputs.size() == 0, "disabled zero Ops calls");
    check(j["status"] == "disabled" && j["files"].empty(), "disabled explicit schema"); schemaPrivacy(j);
}
void fixedPlan() {
    auto o = allRoots(); const auto j = collect(true, o);
    check(o.paths.size() == 24 && j["readAttempts"] == 24 && j["planCompleted"] == true, "24 fixed reads");
    const std::vector<std::wstring> roots{L"C:\\ProgramData", L"D:\\Program Files (x86)", L"E:\\Users\\PrivateName\\Local"};
    const std::vector<std::wstring> roles{L"API\\ControlMode\\MSI_Center_M_Server_ControlMode_",
        L"API\\GamePad\\MSI_Center_M_Server_GamePad_", L"UI\\MSI Center M_", L"API\\MSI_Center_M_Server_UserScenario_"};
    std::size_t n = 0;
    for (const auto* day : {L"2026-10-15", L"2026-10-14"}) for (const auto& root : roots) for (std::size_t role = 0; role < 4; ++role) {
        const auto expected = root + L"\\MSI\\MSI_Center_M\\Log\\" + roles[role] + day + (role == 3 ? L"_AI.log" : L".log");
        check(o.paths[n] == expected, "literal path date-root-role order");
        check(o.caps[n] == fileByteLimit, "64KiB requested cap"); ++n;
    }
    check(j["staticReferenceVersion"] == "3.0.2605.2101" && j["installedVersionMatch"] == "unknown", "static template not installed G3E match");
    schemaPrivacy(j);
}
void grammar() {
    MemoryOps o; std::string text;
    for (const char* mode : {"Offline", "XInput", "DInput", "MSI", "Desktop", "BIOS", "TESTING"})
        text += logLine(std::string("Switch Gamepad Mode (") + mode + ", Macro)");
    for (const char* mkey : {"Macro", "Combination", "Disabled"})
        text += logLine(std::string("Gamepad Mode Updated (Desktop, ") + mkey + ")");
    text += logLine("Device state changed: DInput, Disabled");
    const std::vector<std::string> callers{"WinEventProc", "PBT_APMRESUMESUSPEND", "Window_StateChanged", "WindowState.Minimized",
        "Window_IsKeyboardFocusWithinChanged", "GetFocusRepeatedly", "LaunchMcxOSD"};
    for (const auto& caller : callers) for (const char* prefix : {"", "RetrySetControlModeInThread:"})
        text += logLine(prefix + caller + " Send ControlMode command GoToXInputMode and result= done");
    for (const char* cmd : {"GoToXInputMode", "GoToDesktopMode", "GotoMSIMode", "GoToMacroKeys", "GoToCombinationKeys"})
        text += logLine(std::string("WinEventProc Send ControlMode command ") + cmd + " and result= done");
    o.supply = [text](const auto&, auto) { return full(text); };
    auto j = collect(true, o); const auto& e = events(j);
    check(e.size() == 30 && first(j)["eventsSeen"] == 30, "all whitelist forms parsed");
    for (const auto& value : e) check(value["time"]["millisecond"] == 789 && value["time"]["year"] == 2026, "validated numeric timestamp");
    check(e.at(10)["kind"] == "device-state-changed" && e.at(24)["caller"] == "RetrySetControlModeInThread:LaunchMcxOSD", "device and retry grammar");
    schemaPrivacy(j);
}
// Exact MainUI SetControlMode/RetrySetControlModeInThread body literals, with
// SharedData.LogWriter.GenerateContentToWrite's timestamp/module envelope.
void cachedMainUICommandForms() {
    MemoryOps o;
    const std::string text =
        "2026-10-15 12:34:56.789 [MainUI:MSI Center M] WinEventProc Send ControlMode command GotoMSIMode and result= done\r\n"
        "2026-10-15 12:34:56.790 [MSI Center M] RetrySetControlModeInThread:LaunchMcxOSD Send ControlMode command GoToMacroKeys and result= PRIVATE failure\r\n"
        "2026-10-15 12:34:56.791 [MSI Center M] RetrySetControlModeInThread:EVENT_OBJECT_FOCUS Send ControlMode command GoToXInputMode and result= done\r\n"
        "2026-10-15 12:34:56.792 [Log Error]  Send ControlMode command GoToCombinationKeys and result= \r\n";
    o.supply = [text](const auto&, auto) { return full(text); };
    const auto j = collect(true, o); const auto& e = events(j);
    check(e.size() == 4 && e[0]["caller"] == "WinEventProc" && e[0]["result"] == "done", "cached MainUI command formatting");
    check(e[1]["caller"] == "RetrySetControlModeInThread:LaunchMcxOSD" && e[1]["result"] == "other", "cached retry and API response formatting");
    check(e[2]["caller"] == "other" && e[3]["caller"] == "other" && e[3]["result"] == "other", "out-of-whitelist and default empty caller remain other");
    schemaPrivacy(j);
}
void malicious() {
    MemoryOps o;
    const std::string payload = "PRIVATE Alice C:\\Users\\Alice WINDOW_TITLE GAME_LIST ";
    const std::string text = logLine("Switch Gamepad Mode (" + payload + ", " + payload + ")") +
        logLine("Gamepad Mode Updated (XInput, Disabled)") +
        logLine(payload + " Send ControlMode command " + payload + " and result= " + payload) +
        logLine("RetrySetControlModeInThread:RetrySetControlModeInThread:WinEventProc Send ControlMode command GotoMSIMode and result= done") +
        logLine("Window TITLE only") + logLine("prefix Switch Gamepad Mode (XInput, Macro)") +
        logLine("Switch Gamepad Mode (Desktop, Macro) PRIVATE suffix");
    o.supply = [text](const auto&, auto) { return full(text); };
    const auto j = collect(true, o);
    check(events(j).size() == 4, "only anchored grammar events retained");
    check(events(j)[0]["mode"] == "other" && events(j)[0]["mkeys"] == "other", "unknown mode and mkeys sanitized");
    check(events(j)[2]["caller"] == "other" && events(j)[2]["command"] == "other" && events(j)[2]["result"] == "other", "unknown command fields sanitized");
    check(events(j)[3]["caller"] == "other", "retry prefix cannot recurse"); schemaPrivacy(j);
    MemoryOps missing; missing.supply = [payload](const auto&, auto) { ReadResult r; r.reason = payload; return r; };
    const auto m = collect(true, missing); check(first(m)["reason"] == "unknown" && m["status"] == "unknown", "unavailable does not infer absence"); schemaPrivacy(m);
    for (const auto& digest : {payload, std::string(64, 'z'), std::string(65, 'a'), std::string(200000, 'b')}) {
        MemoryOps bad; bad.hashValue = digest; const auto b = collect(true, bad);
        check(first(b)["hash"].is_null() && hasIssue(b, "invalid-digest"), "hash output cannot export payload"); schemaPrivacy(b);
    }
    MemoryOps upper; upper.hashValue = std::string(64, 'A');
    check(first(collect(true, upper))["hash"] == std::string(64, 'a'), "digest normalized hex");
}
void timeValidation() {
    for (const char* invalid : {"2026-02-29 12:34:56.789", "1900-02-29 12:34:56.789", "2026-13-15 12:34:56.789",
        "2026-10-00 12:34:56.789", "0000-10-15 12:34:56.789", "2026-10-15 24:34:56.789", "2026-10-15 12:60:56.789",
        "2026-10-15 12:34:60.789", "2026-10-15 12:34:56.78x", "2026/10/15 12:34:56.789", "2026-10-15T12:34:56.789"}) {
        MemoryOps o; const auto text = logLine("Gamepad Mode Updated (XInput, Macro)", invalid);
        o.supply = [text](const auto&, auto) { return full(text); };
        const auto j = collect(true, o);
        check(events(j).size() == 1 && events(j)[0]["time"].is_null(), "invalid fixed timestamp becomes null");
    }
    for (const char* valid : {"2000-02-29 00:00:00.000", "2024-02-29 23:59:59.999", "9999-12-31 23:59:59.999"}) {
        MemoryOps o; const auto text = logLine("Switch Gamepad Mode (BIOS, Disabled)", valid);
        o.supply = [text](const auto&, auto) { return full(text); };
        check(!events(collect(true, o))[0]["time"].is_null(), "leap and numeric timestamp accepted");
    }
    MemoryOps plain; plain.supply = [](const auto&, auto) { return full("Switch Gamepad Mode (MSI, Macro)\n"); };
    check(events(collect(true, plain))[0]["time"].is_null(), "no fabricated timestamp");
}
void rootAndDateValidation() {
    const std::vector<std::wstring> invalid{L"", L"relative", L"C:relative", L"\\rooted", L"\\\\server\\share", L"\\\\?\\C:\\data",
        L"\\\\.\\C:\\data", L"C:\\data\\..\\Users", L"C:\\data\\.\\x", L"C:\\data.\\x", L"C:\\data \\x",
        L"C:\\data:stream", L"C:\\data\\\\x", L"C:\\data*", std::wstring(L"C:\\data\0hidden", 14), L"C:\\data\nx"};
    for (const auto& path : invalid) {
        MemoryOps o; o.rootValues[0].path = path; const auto j = collect(true, o);
        check(o.paths.empty() && hasIssue(j, "invalid-root") && j["status"] == "unknown", "invalid root has explicit unknown and no reads"); schemaPrivacy(j);
    }
    for (const char* label : {"", "PRIVATE-label", "PROGRAM-DATA", "program-data/../PRIVATE"}) {
        MemoryOps o; o.rootValues[0].label = label; const auto j = collect(true, o);
        check(o.paths.empty() && hasIssue(j, "invalid-label"), "root label whitelist"); schemaPrivacy(j);
    }
    MemoryOps extra = allRoots(); extra.rootValues.push_back({"program-data", L"C:\\extra"});
    const auto e = collect(true, extra); check(extra.paths.empty() && hasIssue(e, "roots-limit"), "more than three roots fail closed");
    MemoryOps duplicate; duplicate.rootValues.push_back(duplicate.rootValues[0]);
    check(hasIssue(collect(true, duplicate), "duplicate-root") && duplicate.paths.empty(), "duplicate roots not reread");
    MemoryOps empty; empty.rootValues.clear();
    check(hasIssue(collect(true, empty), "empty-roots") && empty.paths.empty(), "empty roots unknown");
    for (const auto& dates : std::vector<std::vector<std::string>>{{}, {"2026-10-15", "2026-10-14", "2026-10-13"},
        {"2026-10-15", "2026-10-15"}, {"2026-10-15", "2026-10-13"}, {"2026-02-29"}, {"1900-02-29"},
        {"0000-01-01"}, {"2026-00-10"}, {"2026-10-32"}, {"2026-1-01"}, {"2026-10-15/../../PRIVATE"},
        {std::string("2026-10-15\0PRIVATE", 18)}}) {
        MemoryOps o; o.dateValues = dates; const auto j = collect(true, o);
        check(o.paths.empty() && !j["issues"].empty() && j["status"] == "unknown", "invalid dates never used in paths"); schemaPrivacy(j);
    }
    for (const auto& dates : std::vector<std::vector<std::string>>{{"2026-01-01", "2025-12-31"}, {"2024-03-01", "2024-02-29"}, {"2000-03-01", "2000-02-29"}}) {
        MemoryOps o; o.dateValues = dates;
        check(collect(true, o)["readAttempts"] == 8, "consecutive dates across month year and leap");
    }
    MemoryOps local; local.rootValues[0].path = L"C:\\";
    check(collect(true, local)["readAttempts"] == 4 && local.paths[0].find(L"C:\\MSI\\") == 0, "drive-root joining");
    MemoryOps mixed; mixed.rootValues.push_back({"local-app-data", L"PRIVATE relative"});
    const auto m = collect(true, mixed); check(mixed.paths.size() == 4 && hasIssue(m, "invalid-root"), "valid roots survive invalid sibling without fallback");
}
void boundariesAndHash() {
    const auto a = logLine("Switch Gamepad Mode (XInput, Macro)");
    const auto b = logLine("Gamepad Mode Updated (Desktop, Disabled)");
    const auto c = logLine("Device state changed: MSI, Combination");
    MemoryOps o;
    const auto text = a + b + c.substr(0, c.size() - 2);
    o.supply = [text](const auto&, auto) { auto r = full(text); r.truncated = true; r.originalBytes += 50000; return r; };
    const auto j = collect(true, o); const auto& f = first(j);
    check(events(j).size() == 1 && events(j)[0]["kind"] == "gamepad-mode-updated", "tail head and terminal fragments discarded");
    check(events(j)[0]["lineInTail"] == 2 && f["wholeFileRead"] == false && f["headBoundary"] == "unknown", "tail-local line and whole-file distinction");
    check(f["headFragmentDiscarded"] == true && f["tailFragmentDiscarded"] == true && f["tailBoundary"] == "partial-or-unterminated", "explicit two partial boundaries");
    check(o.hashInputs.size() == 4 && o.hashInputs[0] == text, "hash exactly returned bytes including discarded fragments"); schemaPrivacy(j);
    MemoryOps terminal; terminal.supply = [a](const auto&, auto) { return full(a.substr(0, a.size() - 2)); };
    const auto t = collect(true, terminal); check(events(t).empty() && first(t)["tailFragmentDiscarded"] == true, "even full-file unterminated final line is not an event");
    MemoryOps shortOps; shortOps.supply = [a,b](const auto&, auto) { auto r = full(a + b); r.originalBytes += 1; return r; };
    const auto s = collect(true, shortOps);
    check(first(s)["shortRead"] == true && first(s)["reason"] == "short-read" && events(s).size() == 1, "short read explicit and unknown head removed");
    MemoryOps flagOnly; flagOnly.supply = [a,b](const auto&, auto) { auto r = full(a+b); r.truncated = true; return r; };
    const auto flag = collect(true, flagOnly); check(first(flag)["wholeFileRead"] == false && first(flag)["shortRead"] == false && events(flag).size() == 1, "truncation flag independently honored");
    MemoryOps bom; bom.supply = [a](const auto&, auto) { return full("\xEF\xBB\xBF" + a); };
    check(events(collect(true, bom)).size() == 1, "UTF8 BOM and CRLF");
    MemoryOps empty; empty.supply = [](const auto&, auto) { return full(""); };
    const auto e = collect(true, empty); check(first(e)["tailBoundary"] == "empty-file" && !first(e)["hash"].is_null() && events(e).empty(), "empty returned bytes hashed without absence inference");
    MemoryOps over; over.supply = [](const auto&, auto cap) { return full(std::string(static_cast<std::size_t>(cap) + 1, 'x')); };
    const auto v = collect(true, over); check(over.paths.size() == 1 && over.hashInputs.empty() && hasIssue(v, "read-over-cap") && first(v)["events"].empty(), "broken Ops over-cap stops without unbounded hash or parse");
    MemoryOps inconsistent; inconsistent.supply = [a](const auto&, auto) { auto r = full(a); r.originalBytes = 0; return r; };
    const auto i = collect(true, inconsistent); check(hasIssue(i, "inconsistent-size") && events(i).empty() && inconsistent.hashInputs[0] == a, "inconsistent original size explicit unknown and hash actual bytes");
}
void limitsAndBudget() {
    auto o = allRoots(); o.supply = [](const auto&, auto cap) { return full(std::string(cap, '\n')); };
    const auto j = collect(true, o);
    check(o.paths.size() == 24 && j["returnedBytes"] == totalByteLimit && o.hashInputs.size() == 24, "exact 24-read 1.5MiB boundary");
    check(j["planCompleted"] == true, "full bounded plan complete"); schemaPrivacy(j);
    auto slow = allRoots(); slow.readAdvance = 500;
    const auto s = collect(true, slow);
    check(slow.paths.size() == 1 && hasIssue(s, "checkpoint-exhausted") && s["planCompleted"] == false, "500ms is checkpoint between reads");
    auto edge = allRoots(); edge.readAdvance = 499;
    check(collect(true, edge)["readAttempts"] == 2, "499ms allows next IO and overshoot checked afterwards");
    MemoryOps clock; clock.regress = true;
    check(hasIssue(collect(true, clock), "clock-regressed") && clock.paths.empty(), "clock regression fails closed");
    MemoryOps post; post.readAdvance = 130;
    const auto p = collect(true, post);
    check(p["readAttempts"] == 4 && p["planCompleted"] == true && hasIssue(p, "checkpoint-exhausted"), "slow final IO recorded without pretending hard timeout");
}
void newest32() {
    MemoryOps o; std::string text;
    for (unsigned i = 0; i < 70; ++i) {
        if (i % 2 == 0) text += "unrelated PRIVATE WINDOW_TITLE GAME_LIST\n";
        text += logLine("Gamepad Mode Updated (XInput, Macro)");
    }
    o.supply = [text](const auto&, auto) { return full(text); };
    const auto j = collect(true, o); const auto& f = first(j);
    check(events(j).size() == 32 && f["eventsSeen"] == 70 && f["eventsOmitted"] == 38, "newest32 and exact omit count");
    check(events(j)[0]["lineInTail"] == 59 && events(j)[31]["lineInTail"] == 105, "last32 in append order retain tail-local indices"); schemaPrivacy(j);
    auto many = allRoots(); many.supply = [text](const auto&, auto) { return full(text); };
    const auto m = collect(true, many);
    std::size_t retained = 0; for (const auto& file : m["files"]) retained += file["events"].size();
    check(retained == 24 * 32, "24 files each independently bounded32");
}
void failuresAndExceptions() {
    for (const char* reason : {"not-found", "access-denied", "sharing-violation", "io-error", "missing-or-not-regular-file", "read-denied", "size-unavailable", "read-failed"}) {
        MemoryOps o; o.supply = [reason](const auto&, auto) { ReadResult r; r.reason = reason; return r; };
        const auto j = collect(true, o);
        check(first(j)["reason"] == reason && j["status"] == "unknown" && first(j)["available"] == false && first(j)["originalBytes"].is_null(), "missing and denied have unknown original size"); schemaPrivacy(j);
    }
    MemoryOps unavailable; const auto a = logLine("Switch Gamepad Mode (XInput, Macro)");
    unavailable.supply = [a](const auto&, auto) { auto r = full(a); r.available = false; r.reason = "io-error"; return r; };
    const auto u = collect(true, unavailable); check(events(u).empty() && unavailable.hashInputs[0] == a, "unavailable returned bytes hashed but not interpreted");
    for (const char* stage : {"roots", "dates", "initial-now", "checkpoint-now", "read", "hash"}) {
        MemoryOps o; const std::string name = stage;
        if (name == "roots") o.failRoots = true;
        if (name == "dates") o.failDates = true;
        if (name == "initial-now") o.failNowAt = 1;
        if (name == "checkpoint-now") o.failNowAt = 2;
        if (name == "read") o.failReadAt = 1;
        if (name == "hash") o.failHashAt = 1;
        const auto j = collect(true, o);
        const char* issue = name == "roots" ? "roots-exception" : name == "dates" ? "dates-exception" :
            name == "read" ? "read-exception" : name == "hash" ? "hash-exception" : "now-exception";
        check(hasIssue(j, issue), "Ops exception explicitly recorded");
        if (name == "initial-now") check(o.rootsCalls == 0 && o.datesCalls == 0, "initial clock failure stops Ops");
        if (name == "read") check(o.paths.size() == 4 && first(j)["reason"] == "read-exception", "read exception counts attempt and later files retained");
        if (name == "hash") check(first(j)["hashState"] == "hash-exception" && !events(j).empty(), "hash exception separate from parse evidence");
        schemaPrivacy(j);
    }
    MemoryOps end; end.failNowAt = 6;
    const auto e = collect(true, end); check(hasIssue(e, "now-exception") && e["readAttempts"] == 4, "final checkpoint exception recorded");
}
} // namespace
int main() {
    const std::vector<std::pair<const char*, void(*)()>> tests{{"zero-gate", zeroGate}, {"fixed-plan", fixedPlan},
        {"grammar-whitelists", grammar}, {"cached-MainUI-command-format", cachedMainUICommandForms}, {"schema-privacy-malicious-payloads", malicious}, {"numeric-time-validation", timeValidation},
        {"root-date-limits", rootAndDateValidation}, {"file-boundaries-and-returned-byte-hash", boundariesAndHash},
        {"read-byte-time-budgets", limitsAndBudget}, {"newest32-omit", newest32}, {"failures-and-Ops-exceptions", failuresAndExceptions}};
    json result = {{"pass", true}, {"checks", 0}, {"cases", json::array()}, {"memoryOpsOnly", true},
        {"actualWindowsInput", false}, {"OEMCalls", 0}, {"SteamCalls", 0}, {"productExecuted", false},
        {"uiFilenameCandidateOnly", true}};
    for (const auto& test : tests) {
        const auto before = checks;
        try { test.second(); result["cases"].push_back({{"name", test.first}, {"pass", true}, {"checks", checks - before}}); }
        catch (const std::exception& error) {
            result["pass"] = false;
            result["cases"].push_back({{"name", test.first}, {"pass", false}, {"checks", checks - before}, {"failure", error.what()}});
        }
        catch (...) { result["pass"] = false; result["cases"].push_back({{"name", test.first}, {"pass", false}, {"failure", "unexpected exception"}}); }
    }
    result["checks"] = checks;
    std::cout << result.dump(2) << '\n';
    return result["pass"] == true ? 0 : 1;
}
