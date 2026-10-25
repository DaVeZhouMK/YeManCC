// Complete actual callers/budget/wrapper are injected verbatim. All proxy and
// lifecycle dependencies below are process-local deterministic fixture seams.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
#include "json.hpp"
using json = nlohmann::json;
using DWORD = unsigned long;
using BYTE = unsigned char;
using BOOL = int;
using WCHAR = wchar_t;
using ULONGLONG = unsigned long long;
using USHORT = unsigned short;
using USAGE = unsigned short;
using InputHostMutex = std::mutex;
static constexpr BOOL FALSE = 0;
static constexpr DWORD success = 0, genericFailure = 5, disconnected = 1167;
static_assert(sizeof(DWORD) == 4);
/*ACTUAL_TYPES*/
static HidHidePhysicalIdentity g_hidHidePhysicalIdentity;
static std::mutex g_hidHidePhysicalIdentityMx;
static struct { HidHidePhysicalIdentity physical; } g_hidHideVisibilityJournal;
static ULONGLONG fakeNow = 10000, g_inputHostEpoch = 8;
static std::string g_inputHostRunId = "r118-inert-run", fixturePersona = "elite";
static const char* inputHostPersona() { return fixturePersona.c_str(); }
static ULONGLONG GetTickCount64() { return fakeNow; }
static std::atomic<unsigned long long> g_powerGeneration{3}, g_xinputTopologyEpoch{2};
static std::atomic<unsigned long long> g_inputHostEpochMirror{8}, g_inputHostVirtualIntentRevision{0};
static std::atomic<unsigned> g_inputHostVirtualIntentWriters{0};
static std::atomic<bool> g_inputReady{true}, g_inputHostPowerAdmissionRevoked{false}, g_inputCaptureStop{false};
static std::atomic<bool> g_externalPriorityVirtualSuspended{false};
enum class PowerLifecycle { Ready, Suspended };
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static struct { bool enabled = true, virtualTargetEnabled = true; } g_realStickTest;
static bool fixtureExit = false, fixtureReleaseWindow = false;
static std::atomic<bool> g_physicalReleaseWindowDutyActive{false};
static bool physicalReleaseWindowActive() { return fixtureReleaseWindow; }
static bool inputHostExitFenceRaised() { return fixtureExit; }
static DWORD g_xinputConnectedMask = 2, g_xinputVirtualEndpointMask = 2, g_xinputSlotUnknownMask = 0, g_xinputPrimarySlot = 0;
static InputHostMutex g_inputHostMx;
static const std::wstring virtualPath = L"fixture-HIDMAESTRO-XUSB", physicalPath = L"physical-A";
static std::wstring U2W(const std::string& value) { return {value.begin(), value.end()}; }
static std::string W2U(const std::wstring& value) { std::string out; for (const auto c : value) out.push_back(static_cast<char>(c)); return out; }
static int budgetOps();
static int attemptCount();
static ULONGLONG issueDeadline();
static bool issueWaitLogged();
static void mutate(const std::string& name);
static json trace = json::array();
static void record(const char* operation, json detail = json::object()) {
    detail["op"] = operation; detail["tick"] = fakeNow; detail["budget"] = budgetOps(); detail["attempts"] = attemptCount();
    trace.push_back(std::move(detail));
}
static void appendNativeLifecycleLog(const char* event, json detail) { record("log", {{"event", event}, {"detail", std::move(detail)}}); }
static void xinputSlotsRequestIdentityReprobe(DWORD mask, const char* why) { record("reprobe", {{"mask", mask}, {"why", why}}); }

enum class Caller { Guard, Handoff };
enum class Hook { Never, WrapperAvailability, FirstSet, Refresh, RetrySet, Readback, Cycle };
struct Configuration {
    bool initialAvailable = true, wrapperAvailable = true, hasRefresh = true, cycleOk = true;
    bool applyMapping = true, rollbackMatches = true, rollbackPresent = true;
    DWORD refreshResult = success, rollbackResult = success, getResult = success;
    int readbackOverride = -1, finalReadbackOverride = -1;
    std::vector<DWORD> results{success};
    Hook hook = Hook::Never;
    std::string mutation = "exit";
    int seedOperations = 0, seedAttempts = 0;
};
static Configuration cfg;
static Caller caller = Caller::Guard;
static bool preflightComplete = false, wrapperEntrySeen = false, hookUsed = false;
static int wrapperEntryBudget = -1, wrapperEntryAttempts = -1;
static int virtualIndex = 1, physicalIndex = 0;
static unsigned setCalls = 0, rollbackCalls = 0, refreshCalls = 0, getCalls = 0, pathCalls = 0, cycleCalls = 0, requeues = 0;
static unsigned wrapperGets = 0, unexpectedCalls = 0, reanchors = 0;
static void hook(Hook point) { if (!hookUsed && cfg.hook == point) { hookUsed = true; mutate(cfg.mutation); record("cancel", {{"reason", cfg.mutation}}); } }
static bool ymccOpenXInputAvailable() {
    const bool wrapperEntry = preflightComplete && !wrapperEntrySeen;
    if (wrapperEntry) {
        wrapperEntrySeen = true; wrapperEntryBudget = budgetOps(); wrapperEntryAttempts = attemptCount();
        record("wrapper-availability-enter");
        hook(Hook::WrapperAvailability);
    }
    const bool value = cfg.initialAvailable && (!wrapperEntry || cfg.wrapperAvailable);
    record("available", {{"value", value}, {"wrapperEntry", wrapperEntry}});
    return value;
}
static DWORD fakeGetDevicePath(DWORD slot, WCHAR* buffer, DWORD* count) {
    ++pathCalls;
    std::wstring path;
    if (slot == static_cast<DWORD>(virtualIndex)) path = virtualPath;
    else if (slot == static_cast<DWORD>(physicalIndex)) path = physicalPath;
    record("get-path", {{"slot", slot}, {"data", buffer != nullptr}, {"path", W2U(path)}});
    if (path.empty()) { *count = 0; return disconnected; }
    const DWORD required = static_cast<DWORD>(path.size() + 1);
    if (!buffer) { *count = required; return success; }
    if (*count < required) return genericFailure;
    std::copy(path.begin(), path.end(), buffer); buffer[path.size()] = L'\0'; *count = required;
    if (caller == Caller::Handoff && path == virtualPath) preflightComplete = true;
    return success;
}
static DWORD fakeGet(const WCHAR* raw, BYTE* slot) {
    ++getCalls;
    const std::wstring path(raw);
    int actual = path == virtualPath ? virtualIndex : physicalIndex;
    const bool afterSet = setCalls > 0;
    DWORD result = success;
    if (path == virtualPath && afterSet) {
        ++wrapperGets;
        if (wrapperGets == 1 && cfg.readbackOverride >= 0) actual = cfg.readbackOverride;
        if (wrapperGets > 1 && cfg.finalReadbackOverride >= 0) actual = cfg.finalReadbackOverride;
        result = cfg.getResult;
    }
    *slot = actual < 0 ? static_cast<BYTE>(0xFF) : static_cast<BYTE>(actual);
    record("get", {{"path", W2U(path)}, {"slot", *slot}, {"result", result}});
    if (caller == Caller::Guard && path == virtualPath && !afterSet) preflightComplete = true;
    if (path == virtualPath && afterSet) hook(Hook::Readback);
    return result;
}
static DWORD fakeSet(const WCHAR* raw, BYTE slot, BOOL powerDown) {
    const std::wstring path(raw);
    DWORD result = genericFailure;
    unsigned ordinal = 0;
    if (path == virtualPath) {
        ordinal = setCalls++;
        if (ordinal < cfg.results.size()) result = cfg.results[ordinal]; else ++unexpectedCalls;
    } else {
        ++rollbackCalls; result = cfg.rollbackResult;
        if (path != physicalPath) ++unexpectedCalls;
    }
    record("set", {{"path", W2U(path)}, {"slot", slot}, {"powerDown", powerDown}, {"result", result}, {"rollback", path != virtualPath}});
    if (result == success && cfg.applyMapping) {
        if (path == virtualPath) { virtualIndex = slot; if (physicalIndex == slot) physicalIndex = -1; }
        else {
            physicalIndex = cfg.rollbackMatches ? slot : -1;
            if (virtualIndex == slot) virtualIndex = 1;
            g_xinputConnectedMask = cfg.rollbackPresent ? 3u : 2u;
            g_xinputVirtualEndpointMask = 2;
        }
    }
    if (path == virtualPath) hook(ordinal == 0 ? Hook::FirstSet : Hook::RetrySet);
    return result;
}
static DWORD fakeRefresh() { ++refreshCalls; record("refresh", {{"result", cfg.refreshResult}}); hook(Hook::Refresh); return cfg.refreshResult; }
static auto g_ymccOpenXInputGetUserIndex = &fakeGet;
static auto g_ymccOpenXInputSetUserIndex = &fakeSet;
static auto g_ymccOpenXInputGetDevicePath = &fakeGetDevicePath;
static DWORD (*g_ymccOpenXInputRefreshMappings)() = &fakeRefresh;
static void hidHideCycleClearBusyIfStale(const char* reason) { record("clear-cycle-busy", {{"reason", reason}}); }
static bool hidHideCycleInstance(const std::wstring& root, const char* phase) {
    ++cycleCalls; record("cycle", {{"root", W2U(root)}, {"phase", phase}, {"result", cfg.cycleOk}});
    if (cfg.cycleOk) physicalIndex = 1;
    hook(Hook::Cycle); return cfg.cycleOk;
}
static void ymccOpenXInputReanchorPhysicalSource() { ++reanchors; record("reanchor"); }
/*ACTUAL_WRAPPER*/
/*ACTUAL_HELPERS*/
/*ACTUAL_BUDGET*/
/*ACTUAL_PLAN_TYPES*/
/*ACTUAL_PLAN_FUNCTIONS*/
/*ACTUAL_REFRESH*/
static bool ymccSlotHandoffPlanRequeue() { ++requeues; record("requeue"); return true; }
/*ACTUAL_GUARD*/
static void handoffCase4() {
    const ULONGLONG now = GetTickCount64();
/*ACTUAL_CASE4*/
}
static int budgetOps() { return g_ymccSlotIntent.operations; }
static int attemptCount() { return g_openXInputVirtualSlotAttempts; }
static ULONGLONG issueDeadline() {
#if R118_HAS_ISSUE_WAIT
    return g_ymccSlotHandoffPlan.issueAdmissionDeadline;
#else
    return 0;
#endif
}
static bool issueWaitLogged() {
#if R118_HAS_ISSUE_WAIT
    return g_ymccSlotHandoffPlan.issueWaitLogged;
#else
    return false;
#endif
}
static void mutate(const std::string& name) {
    if (name == "advance-clock") fakeNow += 10000;
    else if (name == "writer") g_inputHostVirtualIntentWriters = 1;
    else if (name == "intent-revision") ++g_inputHostVirtualIntentRevision;
    else if (name == "exit") fixtureExit = true;
    else if (name == "off") g_realStickTest.virtualTargetEnabled = false;
    else if (name == "not-ready") g_inputReady = false;
    else if (name == "revoked") g_inputHostPowerAdmissionRevoked = true;
    else if (name == "power-generation") ++g_powerGeneration;
    else if (name == "topology-generation") ++g_xinputTopologyEpoch;
    else if (name == "source-unknown") g_hidHidePhysicalIdentity.baseContainerDevicePath.clear();
    else if (name == "source-changed") g_hidHidePhysicalIdentity.baseContainerDevicePath = "physical-B";
    else if (name == "plan-changed") g_ymccSlotHandoffPlan.intentKey = "superseded-plan";
    else throw std::runtime_error("Unknown fixture mutation: " + name);
}
static void setupPlan() {
    g_ymccSlotHandoffPlan = YmccSlotHandoffPlan{};
    auto& plan = g_ymccSlotHandoffPlan;
    plan.active = true; plan.stage = 4; plan.nextDueTick = fakeNow;
    plan.epoch = g_inputHostEpoch; plan.runId = g_inputHostRunId; plan.persona = fixturePersona;
    ymccSlotIntentKeyLocked(plan.intentKey);
    plan.targetPath = virtualPath; plan.targetSlot = 1;
    plan.frozenSourcePath = physicalPath; plan.frozenSourceSlot = 0;
    g_ymccSlotHandoffPlanActive = true;
}
static void reset(Caller kind, const Configuration& configuration) {
    cfg = configuration; caller = kind; fakeNow = 10000;
    fixtureExit = false; fixtureReleaseWindow = false; g_physicalReleaseWindowDutyActive = false; g_realStickTest = {}; g_inputReady = true;
    g_powerLifecycle = PowerLifecycle::Ready; g_inputHostPowerAdmissionRevoked = false; g_inputCaptureStop = false;
    g_powerGeneration = 3; g_xinputTopologyEpoch = 2; g_inputHostEpoch = 8;
    g_inputHostRunId = "r118-inert-run"; fixturePersona = "elite";
    g_inputHostEpochMirror = 8; g_inputHostVirtualIntentRevision = 0; g_inputHostVirtualIntentWriters = 0;
    g_externalPriorityVirtualSuspended = false;
    g_hidHidePhysicalIdentity = HidHidePhysicalIdentity{};
    g_hidHidePhysicalIdentity.resolved = true;
    g_hidHidePhysicalIdentity.baseContainerDevicePath = "physical-A";
    g_hidHidePhysicalIdentity.baseContainerDeviceInstanceId = "USB\\VID_1234&PID_5678\\fixture-A";
    g_hidHideVisibilityJournal.physical = g_hidHidePhysicalIdentity;
    g_xinputConnectedMask = kind == Caller::Guard ? 2u : 3u;
    g_xinputVirtualEndpointMask = 2; g_xinputSlotUnknownMask = 0; g_xinputPrimarySlot = 0;
    g_ymccSlotIntent = YmccSlotIntentBudget{}; ymccSlotIntentSync();
    g_ymccSlotIntent.operations = cfg.seedOperations;
    g_openXInputVirtualSlotAttempts = cfg.seedAttempts;
    setupPlan(); if (kind == Caller::Guard) { g_ymccSlotHandoffPlan.active = false; g_ymccSlotHandoffPlanActive = false; }
    preflightComplete = wrapperEntrySeen = hookUsed = false;
    wrapperEntryBudget = wrapperEntryAttempts = -1;
    virtualIndex = 1; physicalIndex = 0;
    setCalls = rollbackCalls = refreshCalls = getCalls = pathCalls = cycleCalls = requeues = wrapperGets = unexpectedCalls = reanchors = 0;
    g_ymccOpenXInputRefreshMappings = cfg.hasRefresh ? &fakeRefresh : nullptr;
    trace = json::array();
}
static void invoke() { if (caller == Caller::Guard) ymccOpenXInputVirtualSlotGuard(); else handoffCase4(); }
static json logs(const char* event) {
    json out = json::array();
    for (const auto& row : trace) if (row["op"] == "log" && row["event"] == event) out.push_back(row["detail"]);
    return out;
}
static unsigned countOp(const char* op) { return static_cast<unsigned>(std::count_if(trace.begin(), trace.end(), [op](const json& row) { return row["op"] == op; })); }
static json snapshot() {
    return {{"budget", budgetOps()}, {"attempts", attemptCount()}, {"exhausted", g_ymccSlotIntent.exhausted},
        {"confirmed", g_ymccSlotIntent.confirmedSlot0}, {"confirmedPath", g_ymccSlotIntent.confirmedPath},
        {"planActive", g_ymccSlotHandoffPlan.active}, {"planStage", g_ymccSlotHandoffPlan.stage},
        {"nextDue", g_ymccSlotHandoffPlan.nextDueTick}, {"issueDeadline", issueDeadline()}, {"issueWaitLogged", issueWaitLogged()}, {"requeues", requeues},
        {"sets", setCalls}, {"refreshes", refreshCalls}, {"rollbacks", rollbackCalls}, {"cycles", cycleCalls},
        {"gets", getCalls}, {"pathCalls", pathCalls}, {"wrapperEntryBudget", wrapperEntryBudget},
        {"wrapperEntryAttempts", wrapperEntryAttempts}, {"hookUsed", hookUsed}, {"trace", trace}};
}
static json cases = json::array(), checks = json::array();
static unsigned assertions = 0, failedChecks = 0, executions = 0;
static void expect(const char* name, const json& actual, const json& expected) {
    ++assertions; const bool pass = actual == expected; if (!pass) ++failedChecks;
    checks.push_back({{"name", name}, {"actual", actual}, {"expected", expected}, {"pass", pass}});
}
static void finish(const std::string& name) {
    bool pass = true; for (const auto& row : checks) pass = pass && row["pass"].get<bool>();
    cases.push_back({{"name", name}, {"passed", pass}, {"checks", checks}, {"observation", snapshot()}});
    checks = json::array();
}
struct Expected {
    int operations = 1;
    unsigned sets = 1, refreshes = 0, acknowledgements = 1, reprobes = 1;
    bool confirmed = true, cancellation = false;
    bool issued = true;
};
static void runCase(Caller kind, const std::string& name, const Configuration& configuration, const Expected& expected) {
    reset(kind, configuration); invoke(); ++executions;
    expect("shared operation budget counts only issued attempt", budgetOps(), expected.operations);
    expect("guard attempt counter changes only on reservation", attemptCount(), configuration.seedAttempts + (kind == Caller::Guard && expected.issued ? 1 : 0));
    expect("exact virtual Set count", setCalls, expected.sets);
    expect("exact Refresh count", refreshCalls, expected.refreshes);
    expect("successful issued Set ACK retained", logs("gamepad-openxinput-assign-slot").size(), expected.acknowledgements);
    expect("exact identity reprobes", countOp("reprobe"), expected.reprobes);
    expect("caller confirmation requires current successful readback", g_ymccSlotIntent.confirmedSlot0, expected.confirmed);
    expect("no unplanned proxy calls", unexpectedCalls, 0);
    expect("no rollback for unaccepted/cancelled attempt", rollbackCalls, 0);
    if (wrapperEntrySeen) {
        expect("wrapper availability runs before any reservation", wrapperEntryBudget, configuration.seedOperations);
        expect("wrapper availability runs before guard attempt increments", wrapperEntryAttempts, configuration.seedAttempts);
    }
    for (const auto& row : trace) {
        if (row["op"] == "set" || row["op"] == "refresh") {
            expect("every issued Set/Refresh/retry has exactly one shared reservation", row["budget"], configuration.seedOperations + 1);
            if (kind == Caller::Guard) expect("same-attempt retry never increments attempts twice", row["attempts"], configuration.seedAttempts + 1);
        }
    }
    if (configuration.hook != Hook::Never && wrapperEntrySeen) expect("selected cancellation boundary actually executed", hookUsed, true);
    if (expected.acknowledgements) for (const auto& ack : logs("gamepad-openxinput-assign-slot")) {
        expect("ACK reports proxy-success result only", ack["result"], success);
        const bool accepted = configuration.getResult == success && configuration.readbackOverride != 1 && configuration.readbackOverride != 255;
        expect("ACK accepted reflects supplied readback, not subsequent admission", ack["accepted"], accepted);
    }
    if (!expected.issued) {
        expect("unissued attempt has no cycle", cycleCalls, 0);
        if (kind == Caller::Guard || expected.cancellation || !wrapperEntrySeen) expect("unissued cancelled/guard attempt never queues a handoff retry", requeues, 0);
        if (kind == Caller::Handoff) {
            const bool waiting = wrapperEntrySeen && !expected.cancellation;
            expect("unissued-current handoff retains plan; cancellation clears it", g_ymccSlotHandoffPlan.active, waiting);
            expect("unissued-current handoff uses original queue", requeues, waiting ? 1 : 0);
            if (waiting) {
                expect("unissued-current stays at stage4", g_ymccSlotHandoffPlan.stage, 4);
                expect("unissued-current starts original fixed 4s window", issueDeadline(), fakeNow + 4000);
                expect("unissued-current retains original 100ms spacing", g_ymccSlotHandoffPlan.nextDueTick, fakeNow + 100);
            }
        }
    } else if (expected.cancellation) {
        expect("cancelled issued attempt never requeues/rearms", requeues, 0);
        expect("cancelled issued attempt never starts cycle", cycleCalls, 0);
        if (kind == Caller::Handoff) expect("cancelled handoff clears plan", g_ymccSlotHandoffPlan.active, false);
    } else if (kind == Caller::Handoff && !expected.confirmed) {
        const bool limit = expected.operations >= kYmccSlotIntentMaxOperations;
        expect("existing failure retry/limit policy preserved", requeues, limit ? 0 : 1);
        expect("existing exhausted bit policy preserved", g_ymccSlotIntent.exhausted, limit);
        if (!limit) {
            expect("existing retry stage preserved", g_ymccSlotHandoffPlan.stage, 3);
            expect("original 1500ms retry deadline preserved", g_ymccSlotHandoffPlan.nextDueTick, fakeNow + 1500);
        }
    }
    if (expected.issued && !expected.cancellation && expected.confirmed && kind == Caller::Handoff) expect("positive handoff retains one original cycle", cycleCalls, 1);
    // Callback rechecks may consult cached availability but may not add raw route queries.
    if (wrapperEntrySeen) {
        expect("callback adds no raw device-path reads", pathCalls, 2);
        expect("callback adds no raw mapping reads", getCalls,
            (kind == Caller::Guard ? 1u : 0u) + expected.acknowledgements + (kind == Caller::Handoff && expected.confirmed ? 1u : 0u));
    }
    if (kind == Caller::Handoff && expected.issued) {
        expect("issued operation clears unissued deadline", issueDeadline(), 0);
        expect("issued operation clears unissued wait log state", issueWaitLogged(), false);
    }
    finish(std::string(kind == Caller::Guard ? "guard/" : "handoff/") + name);
}
static void matrix() {
    for (const auto kind : {Caller::Guard, Caller::Handoff}) {
        Configuration c; Expected e;
        runCase(kind, "healthy-first-success", c, e);
        c.initialAvailable = false; e = {0, 0, 0, 0, 0, false, false, false};
        runCase(kind, "initial-proxy-unavailable", c, e);
        c = {}; c.wrapperAvailable = false;
        runCase(kind, "proxy-unavailable-at-wrapper-entry", c, e);
        e.cancellation = true;
        for (const std::string mutation : {"exit", "off", "not-ready", "revoked", "power-generation", "topology-generation", "source-unknown", "source-changed"}) {
            c = {}; c.hook = Hook::WrapperAvailability; c.mutation = mutation;
            runCase(kind, "reject-before-first-set/" + mutation, c, e);
        }
        if (kind == Caller::Handoff) for (const std::string mutation : {"plan-changed", "writer", "intent-revision"}) {
            c = {}; c.hook = Hook::WrapperAvailability; c.mutation = mutation;
            runCase(kind, "reject-before-first-set/" + mutation, c, e);
        }
        for (const auto point : {Hook::FirstSet, Hook::Refresh}) for (const std::string mutation : {"exit", "off", "power-generation", "topology-generation", "source-unknown"}) {
            c = {}; c.results = {genericFailure, success}; c.hook = point; c.mutation = mutation;
            e = {1, 1, point == Hook::Refresh ? 1u : 0u, 0, point == Hook::Refresh ? 1u : 0u, false, true, true};
            runCase(kind, std::string(point == Hook::FirstSet ? "cancel-before-refresh/" : "cancel-before-retry/") + mutation, c, e);
        }
        for (const auto point : {Hook::FirstSet, Hook::RetrySet, Hook::Readback}) {
            c = {}; c.hook = point; c.mutation = "exit";
            const bool retry = point == Hook::RetrySet;
            if (retry) c.results = {genericFailure, success};
            e = {1, retry ? 2u : 1u, retry ? 1u : 0u, 1, retry ? 2u : 1u, false, true, true};
            runCase(kind, point == Hook::FirstSet ? "issued-success-revoked-before-ack" : point == Hook::RetrySet ? "issued-retry-success-revoked-before-ack" : "issued-success-revoked-during-readback", c, e);
        }
        c = {}; c.results = {disconnected}; e = {1, 1, 0, 0, 0, false};
        runCase(kind, "issued-disconnected-consumes-one", c, e);
        c = {}; c.results = {genericFailure}; c.hasRefresh = false; e = {1, 1, 0, 0, 1, false};
        runCase(kind, "issued-failure-without-refresh-consumes-one", c, e);
        c = {}; c.results = {genericFailure}; c.refreshResult = genericFailure; e = {1, 1, 1, 0, 1, false};
        runCase(kind, "failed-refresh-no-double-budget", c, e);
        c = {}; c.results = {genericFailure, genericFailure}; e = {1, 2, 1, 0, 2, false};
        runCase(kind, "failed-retry-no-double-budget", c, e);
        c.results = {genericFailure, success}; e = {1, 2, 1, 1, 2, true};
        runCase(kind, "successful-refresh-retry-no-double-budget", c, e);
        c = {}; c.readbackOverride = 1; e = {1, 1, 0, 1, 1, false};
        runCase(kind, "mismatching-wrapper-readback-no-false-confirmed", c, e);
        c = {}; c.getResult = disconnected;
        runCase(kind, "failed-wrapper-readback-no-false-confirmed", c, e);
        c = {}; c.seedOperations = 1; c.results = {disconnected}; e = {2, 1, 0, 0, 0, false};
        runCase(kind, "second-issued-operation-reaches-original-limit", c, e);
        c = {}; c.seedOperations = 2; e = {2, 0, 0, 0, 0, false, false, false};
        runCase(kind, "two-operation-limit-rejects-third", c, e);
    }
}
static void rollbackCase(const std::string& name, const Configuration& configuration, bool verified, bool virtualConfirmed = false) {
    reset(Caller::Handoff, configuration); invoke(); ++executions;
    expect("rollback keeps original single attempt reservation", budgetOps(), 2);
    expect("virtual assignment and rollback use real wrapper Set calls", json::array({setCalls, rollbackCalls}), json::array({1, 1}));
    expect("failed cycle still triggers original compensation", cycleCalls, 1);
    const auto events = logs("gamepad-slot-handoff"); expect("handoff result logged once", events.size(), 1);
    if (!events.empty()) {
        expect("rollback remains needed after failed cycle", events[0]["rollbackNeeded"], true);
        expect("rollback verified requires ACK/readback/presence", events[0]["rollbackVerified"], verified);
        expect("rollbackOk retains existing verification result", events[0]["rollbackOk"], verified);
    }
    expect("final virtual readback remains independent of physical rollback verification", g_ymccSlotIntent.confirmedSlot0, virtualConfirmed);
    expect("original exhaustion decision follows final virtual readback", g_ymccSlotIntent.exhausted, !virtualConfirmed);
    expect("at-limit compensation does not rearm", requeues, 0);
    for (const auto& row : trace) if (row["op"] == "set") {
        expect("compensation retains same shared budget", row["budget"], 2);
        if (row["rollback"] == true) { expect("compensation uses frozen source only", row["path"], W2U(physicalPath)); expect("compensation uses frozen slot only", row["slot"], 0); }
    }
    finish("handoff/rollback/" + name);
}
static void sharedLimitCase() {
    Configuration c; c.results = {disconnected, disconnected}; reset(Caller::Guard, c); invoke(); ++executions;
    expect("guard first failure spends first shared operation", budgetOps(), 1);
    caller = Caller::Handoff; g_xinputConnectedMask = 3; setupPlan(); preflightComplete = wrapperEntrySeen = false;
    invoke(); ++executions;
    expect("handoff uses second operation from same budget", budgetOps(), 2);
    expect("handoff second failure exhausts original limit", g_ymccSlotIntent.exhausted, true);
    const unsigned issued = setCalls;
    caller = Caller::Guard; g_xinputConnectedMask = 2; invoke(); ++executions;
    caller = Caller::Handoff; g_xinputConnectedMask = 3; setupPlan(); invoke(); ++executions;
    expect("neither caller can issue third operation", setCalls, issued);
    expect("neither caller rearms shared budget", budgetOps(), 2);
    g_hidHidePhysicalIdentity.baseContainerDevicePath.clear(); ymccSlotIntentSync();
    expect("unknown source preserves spent budget", budgetOps(), 2);
    g_hidHidePhysicalIdentity.baseContainerDevicePath = "physical-A"; ymccSlotIntentSync();
    expect("same source recovery does not refund/rearm", budgetOps(), 2);
    expect("same source recovery retains exhausted flag", g_ymccSlotIntent.exhausted, true);
    expect("guard attempt counter counts its one issued reservation", attemptCount(), 1);
    expect("original shared limit remains exactly two", kYmccSlotIntentMaxOperations, 2);
    finish("shared/guard-handoff-original-two-operation-limit");
}
static void nextAttempt() {
    preflightComplete = wrapperEntrySeen = hookUsed = false;
    wrapperEntryBudget = wrapperEntryAttempts = -1;
    invoke(); ++executions;
}
static bool canContinueUnissued() { return g_ymccSlotHandoffPlan.active && g_ymccSlotHandoffPlan.stage == 4; }
static void unissuedWindowCases() {
    Configuration c; c.wrapperAvailable = false;
    reset(Caller::Handoff, c); invoke(); ++executions;
    const ULONGLONG fixedDeadline = 14000;
    expect("initial unissued window uses existing 4 seconds", issueDeadline(), fixedDeadline);
    unsigned observations = 1;
    while (canContinueUnissued() && observations < 45) {
        expect("observation never renews deadline", issueDeadline(), fixedDeadline);
        expect("each unissued observation spends zero", budgetOps(), 0);
        expect("unissued wait never fabricates exhaustion", g_ymccSlotIntent.exhausted, false);
        const auto previous = fakeNow;
        fakeNow = g_ymccSlotHandoffPlan.nextDueTick;
        expect("wait uses fixed 100ms spacing capped by deadline", fakeNow, (std::min)(previous + 100, fixedDeadline));
        nextAttempt(); ++observations;
    }
    expect("fixed deadline terminates memory plan", g_ymccSlotHandoffPlan.active, false);
    expect("deadline has not moved beyond original 4 seconds", fakeNow, fixedDeadline);
    expect("all repeated unissued observations spend zero", budgetOps(), 0);
    expect("all repeated unissued observations issue zero writes", json::array({setCalls, refreshCalls, rollbackCalls, cycleCalls}), json::array({0, 0, 0, 0}));
    expect("unissued expiry never confirms or exhausts", json::array({g_ymccSlotIntent.confirmedSlot0, g_ymccSlotIntent.exhausted}), json::array({false, false}));
    unsigned waitLogs = 0, expiryLogs = 0;
    for (const auto& row : logs("gamepad-slot-handoff-queue")) {
        if (row["reason"] == "assignment-not-issued") ++waitLogs;
        if (row["reason"] == "assignment-unissued-before-deadline") ++expiryLogs;
    }
    expect("one wait receipt for fixed window", waitLogs, 1);
    expect("one explicit unissued expiry receipt", expiryLogs, 1);
    expect("unissued wait uses 40 existing queue slots", requeues, 40);
    expect("unissued expiry has no exhaustion receipt", logs("gamepad-slot-handoff-exhausted").size(), 0);
    finish("handoff/unissued/repeated-fixed-window-expiry");

    reset(Caller::Handoff, c); invoke(); ++executions;
    if (canContinueUnissued()) { fakeNow = g_ymccSlotHandoffPlan.nextDueTick; cfg.wrapperAvailable = true; nextAttempt(); }
    expect("transient unavailability automatically recovers same plan", setCalls, 1);
    expect("recovery spends first operation only", budgetOps(), 1);
    expect("recovery consumes no second guard attempt", attemptCount(), 0);
    expect("recovery preserves successful ACK", logs("gamepad-openxinput-assign-slot").size(), 1);
    expect("recovery confirms supplied target readback", g_ymccSlotIntent.confirmedSlot0, true);
    expect("issued success clears unissued wait state", json::array({issueDeadline(), issueWaitLogged()}), json::array({0, false}));
    expect("recovery adds no duplicate retry", requeues, 1);
    finish("handoff/unissued/transient-unavailable-recovers");

    reset(Caller::Handoff, c); invoke(); ++executions;
    if (canContinueUnissued()) { fakeNow = g_ymccSlotHandoffPlan.nextDueTick; cfg.wrapperAvailable = true; cfg.results = {disconnected}; nextAttempt(); }
    expect("recovered but failed issue spends one", budgetOps(), 1);
    expect("recovered failure performs one Set only", setCalls, 1);
    expect("issued failure clears unissued wait state", json::array({issueDeadline(), issueWaitLogged()}), json::array({0, false}));
    expect("issued failure retains original retry stage", g_ymccSlotHandoffPlan.stage, 3);
    expect("issued failure uses original 1500ms retry", g_ymccSlotHandoffPlan.nextDueTick, fakeNow + 1500);
    expect("wait plus original retry has exactly two requeues", requeues, 2);
    expect("first failed issue does not exhaust budget", g_ymccSlotIntent.exhausted, false);
    finish("handoff/unissued/recovery-failure-keeps-original-retry");

    reset(Caller::Handoff, c); invoke(); ++executions;
    if (canContinueUnissued()) { fakeNow = g_ymccSlotHandoffPlan.nextDueTick; cfg.hook = Hook::WrapperAvailability; cfg.mutation = "exit"; nextAttempt(); }
    expect("cancellation while waiting immediately clears plan", g_ymccSlotHandoffPlan.active, false);
    expect("cancellation while waiting spends nothing", budgetOps(), 0);
    expect("cancellation while waiting never requeues again", requeues, 1);
    expect("cancellation while waiting never writes", setCalls, 0);
    expect("cancelled waiting window is cleared", issueDeadline(), 0);
    finish("handoff/unissued/cancel-waiting-plan-immediately");

    c = {}; c.hook = Hook::WrapperAvailability; c.mutation = "advance-clock";
    reset(Caller::Handoff, c); invoke(); ++executions;
    expect("expiry at first callback cannot reserve", budgetOps(), 0);
    expect("expiry at first callback cannot issue", setCalls, 0);
    expect("expiry at first callback clears memory plan", g_ymccSlotHandoffPlan.active, false);
    expect("expiry does not fake health/exhaustion", json::array({g_ymccSlotIntent.confirmedSlot0, g_ymccSlotIntent.exhausted}), json::array({false, false}));
    expect("expiry does not requeue", requeues, 0);
    finish("handoff/unissued/deadline-check-before-first-real-set");

    c = {}; c.results = {genericFailure, success}; c.hook = Hook::FirstSet; c.mutation = "advance-clock";
    reset(Caller::Handoff, c); invoke(); ++executions;
    expect("issued attempt retains one reservation after wait deadline passes", budgetOps(), 1);
    expect("issued attempt allows original Refresh/retry beyond unissued deadline", json::array({setCalls, refreshCalls}), json::array({2, 1}));
    expect("issued retry retains original ACK", logs("gamepad-openxinput-assign-slot").size(), 1);
    expect("issued retry confirms actual supplied readback", g_ymccSlotIntent.confirmedSlot0, true);
    expect("issued attempt clears unissued deadline", issueDeadline(), 0);
    expect("issued operation does not start an extra retry plan", requeues, 0);
    finish("handoff/issued/unissued-window-is-not-new-io-timeout");

    c = {}; c.wrapperAvailable = false; c.seedAttempts = 2;
    reset(Caller::Guard, c); invoke(); ++executions;
    expect("unissued virtual tick spends no attempt", attemptCount(), 2);
    expect("unissued virtual tick spends no operation", budgetOps(), 0);
    fakeNow += 1000; cfg.wrapperAvailable = true; nextAttempt();
    expect("next virtual tick recovers with one reserved attempt", attemptCount(), 3);
    expect("next virtual tick recovers with one shared operation", budgetOps(), 1);
    expect("recovered virtual tick performs one Set", setCalls, 1);
    expect("recovered virtual tick preserves ACK/readback", g_ymccSlotIntent.confirmedSlot0, true);
    finish("guard/unissued/transient-unavailable-recovers-next-tick");
}

int main() {
    try {
        (void)&ymccSlotIntentMarkPending;
        (void)&physicalReleaseWindowActive;
        matrix();
        unissuedWindowCases();
        Configuration c; c.seedOperations = 1; c.cycleOk = false;
        rollbackCase("ack-and-presence-verified", c, true);
        c.rollbackMatches = false; rollbackCase("mismatching-readback-is-unverified", c, false);
        c.rollbackMatches = true; c.rollbackPresent = false; rollbackCase("missing-physical-presence-is-unverified", c, false);
        c.rollbackPresent = true; c.rollbackResult = disconnected; rollbackCase("failed-compensation-remains-unverified-but-final-virtual-readback-controls", c, false, true);
        sharedLimitCase();
        unsigned failedCases = 0; for (const auto& row : cases) if (!row["passed"].get<bool>()) ++failedCases;
        std::cout << json{{"passed", failedChecks == 0}, {"caseCount", cases.size()}, {"callerExecutions", executions},
            {"assertions", assertions}, {"failedChecks", failedChecks}, {"failedCases", failedCases}, {"cases", cases},
            {"realDeviceOperations", 0}, {"productionExecuted", false}, {"assignWrapperIsActual", true},
            {"scope", "Actual complete virtual guard + actual case4 body + actual budgets/gates/wrapper; inert raw proxy/cycle/queue. No full scheduling or hardware acceptance."}}.dump(2) << '\n';
        return failedChecks ? 1 : 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
