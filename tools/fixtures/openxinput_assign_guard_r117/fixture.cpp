// Only declarations and complete functions at the two markers come from production.
// Every external dependency below is an inert, deterministic fixture seam.
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include "json.hpp"

using json = nlohmann::json;
using DWORD = unsigned long;
using BYTE = unsigned char;
using BOOL = int;
using WCHAR = wchar_t;
static_assert(sizeof(DWORD) == 4 && sizeof(BYTE) == 1);
static constexpr BOOL FALSE = 0;
static constexpr DWORD ok = 0, unavailable = 1, failure = 5, disconnected = 1167;

enum class Revoke { Never, Availability, FirstSet, Refresh, RetrySet, Get };
struct Scenario {
    std::wstring path = L"fixture-XUSB-path";
    BYTE slot = 0, actual = 0;
    bool available = true, readbackAvailable = true, hasRefresh = true;
    bool initialAccept = true, initialAdmission = true;
    DWORD getResult = ok, refreshResult = ok;
    std::vector<DWORD> setResults{ok};
    Revoke revoke = Revoke::Never;
};
struct Simulation {
    Scenario scenario;
    json trace = json::array();
    unsigned availableCalls = 0, setCalls = 0, getCalls = 0, refreshCalls = 0, guardCalls = 0;
    unsigned unexpectedCalls = 0;
    bool admitted = true, contextCorrect = true;
    void* expectedContext = nullptr;
};
static Simulation* sim = nullptr;
struct GuardContext { unsigned cookie = 0x117; };
static GuardContext fixtureGuardContext;

static std::string W2U(const std::wstring& value) {
    std::string out;
    for (const auto c : value) out.push_back(static_cast<char>(c));
    return out;
}
static bool ymccOpenXInputAvailable() {
    const bool value = sim->availableCalls++ == 0 ? sim->scenario.available : sim->scenario.readbackAvailable;
    sim->trace.push_back({{"op", "available"}, {"value", value}});
    if (sim->scenario.revoke == Revoke::Availability) sim->admitted = false;
    return value;
}
static DWORD fakeSet(const WCHAR* devicePath, BYTE targetSlot, BOOL powerDown) {
    const auto ordinal = sim->setCalls++;
    const auto& values = sim->scenario.setResults;
    const DWORD result = ordinal < values.size() ? values[ordinal] : failure;
    if (ordinal >= values.size()) ++sim->unexpectedCalls;
    sim->trace.push_back({{"op", "set"}, {"path", W2U(devicePath)}, {"slot", targetSlot},
        {"powerDownOnChange", powerDown}, {"result", result}});
    if ((ordinal == 0 && sim->scenario.revoke == Revoke::FirstSet) ||
        (ordinal == 1 && sim->scenario.revoke == Revoke::RetrySet)) sim->admitted = false;
    return result;
}
static DWORD fakeGet(const WCHAR* devicePath, BYTE* userIndex) {
    ++sim->getCalls;
    *userIndex = sim->scenario.actual;
    sim->trace.push_back({{"op", "get"}, {"path", W2U(devicePath)},
        {"result", sim->scenario.getResult}, {"writtenSlot", *userIndex}});
    if (sim->scenario.revoke == Revoke::Get) sim->admitted = false;
    return sim->scenario.getResult;
}
static DWORD fakeRefresh() {
    ++sim->refreshCalls;
    sim->trace.push_back({{"op", "refresh"}, {"result", sim->scenario.refreshResult}});
    if (sim->scenario.revoke == Revoke::Refresh) sim->admitted = false;
    return sim->scenario.refreshResult;
}
static auto g_ymccOpenXInputSetUserIndex = &fakeSet;
static auto g_ymccOpenXInputGetUserIndex = &fakeGet;
static DWORD (*g_ymccOpenXInputRefreshMappings)() = &fakeRefresh;
static void appendNativeLifecycleLog(const char* event, json detail) {
    sim->trace.push_back({{"op", "log"}, {"event", event}, {"detail", std::move(detail)}});
}
static void xinputSlotsRequestIdentityReprobe(DWORD slots, const char* why) {
    sim->trace.push_back({{"op", "reprobe"}, {"slots", slots}, {"why", why}});
}

namespace baseline {
/*ACTUAL_BASELINE*/
}
namespace candidate {
/*ACTUAL_CANDIDATE*/
}

static_assert(std::is_same_v<candidate::OpenXInputAssignGuard, bool(*)(void*) noexcept>);
static_assert(std::is_same_v<decltype(&candidate::ymccOpenXInputAssignSlot),
    bool(*)(const std::wstring&, BYTE, bool&, candidate::OpenXInputAssignGuard, void*)>);
static_assert(std::is_same_v<decltype(&baseline::ymccOpenXInputAssignSlot),
    bool(*)(const std::wstring&, BYTE, bool&)>);

static bool fakeGuard(void* opaque) noexcept {
    ++sim->guardCalls;
    const bool correct = opaque == sim->expectedContext && (!opaque || fixtureGuardContext.cookie == 0x117);
    sim->contextCorrect = sim->contextCorrect && correct;
    sim->trace.push_back({{"op", "guard"}, {"allowed", sim->admitted},
        {"contextMatches", correct}, {"contextWasNull", opaque == nullptr}});
    return sim->admitted;
}

enum class Mode { Baseline, Default, NullGuard, NullWithContext, TrueWithContext, TrueDefaultContext };
static const char* modeName(Mode mode) {
    switch (mode) {
    case Mode::Baseline: return "baseline";
    case Mode::Default: return "default-three-args";
    case Mode::NullGuard: return "explicit-null-four-args";
    case Mode::NullWithContext: return "null-with-nonnull-context";
    case Mode::TrueWithContext: return "guard-with-context";
    case Mode::TrueDefaultContext: return "guard-with-default-null-context";
    }
    return "invalid";
}
struct Observation {
    bool returned = false, accepted = false, contextCorrect = true, admittedAfter = true;
    unsigned guardCalls = 0, unexpectedCalls = 0;
    json trace, external;
    json view() const { return {{"returned", returned}, {"accepted", accepted}, {"trace", external}}; }
    json detail() const {
        return {{"returned", returned}, {"accepted", accepted}, {"trace", trace},
            {"guardCalls", guardCalls}, {"contextCorrect", contextCorrect},
            {"admittedAfter", admittedAfter}, {"unexpectedCalls", unexpectedCalls}};
    }
};
static Observation execute(const Scenario& scenario, Mode mode) {
    Simulation state;
    state.scenario = scenario;
    state.admitted = scenario.initialAdmission;
    state.expectedContext = mode == Mode::TrueDefaultContext ? nullptr : &fixtureGuardContext;
    sim = &state;
    g_ymccOpenXInputRefreshMappings = scenario.hasRefresh ? &fakeRefresh : nullptr;
    bool accepted = scenario.initialAccept, returned = false;
    switch (mode) {
    case Mode::Baseline: returned = baseline::ymccOpenXInputAssignSlot(scenario.path, scenario.slot, accepted); break;
    case Mode::Default: returned = candidate::ymccOpenXInputAssignSlot(scenario.path, scenario.slot, accepted); break;
    case Mode::NullGuard: returned = candidate::ymccOpenXInputAssignSlot(scenario.path, scenario.slot, accepted, nullptr); break;
    case Mode::NullWithContext: returned = candidate::ymccOpenXInputAssignSlot(scenario.path, scenario.slot, accepted, nullptr, &fixtureGuardContext); break;
    case Mode::TrueWithContext: returned = candidate::ymccOpenXInputAssignSlot(scenario.path, scenario.slot, accepted, &fakeGuard, &fixtureGuardContext); break;
    case Mode::TrueDefaultContext: returned = candidate::ymccOpenXInputAssignSlot(scenario.path, scenario.slot, accepted, &fakeGuard); break;
    }
    json external = json::array();
    for (const auto& event : state.trace) if (event["op"] != "guard") external.push_back(event);
    sim = nullptr;
    return {returned, accepted, state.contextCorrect, state.admitted, state.guardCalls,
        state.unexpectedCalls, state.trace, external};
}

// Expected events describe observable contracts; the assignment algorithm exists
// exclusively in the complete source extracts above.
static json availableEvent(bool value = true) { return {{"op", "available"}, {"value", value}}; }
static json setEvent(const Scenario& s, size_t index = 0) {
    return {{"op", "set"}, {"path", W2U(s.path)}, {"slot", s.slot},
        {"powerDownOnChange", 0}, {"result", s.setResults.at(index)}};
}
static json refreshEvent(const Scenario& s) { return {{"op", "refresh"}, {"result", s.refreshResult}}; }
static json reprobeEvent(DWORD mask, const char* why) { return {{"op", "reprobe"}, {"slots", mask}, {"why", why}}; }
static DWORD slotMask(const Scenario& s) { return 1u << s.slot; }
static BYTE observedSlot(const Scenario& s) { return s.readbackAvailable && s.getResult == ok ? s.actual : static_cast<BYTE>(0xFF); }
static json successTail(const Scenario& s, bool retry) {
    json events = json::array({availableEvent(s.readbackAvailable)});
    if (s.readbackAvailable) events.push_back({{"op", "get"}, {"path", W2U(s.path)},
        {"result", s.getResult}, {"writtenSlot", s.actual}});
    json detail = {{"path", W2U(s.path)}, {"targetSlot", s.slot}, {"result", 0},
        {"actualSlot", observedSlot(s)}, {"accepted", observedSlot(s) == s.slot}};
    if (retry) detail["refreshRetried"] = true;
    events.push_back({{"op", "log"}, {"event", "gamepad-openxinput-assign-slot"}, {"detail", detail}});
    events.push_back(reprobeEvent(slotMask(s), retry ? "assign-slot-success-after-refresh" : "assign-slot-success"));
    return events;
}
static json concatenate(json events, const json& tail) {
    for (const auto& event : tail) events.push_back(event);
    return events;
}
static json firstSuccess(const Scenario& s) {
    return concatenate(json::array({availableEvent(), setEvent(s)}), successTail(s, false));
}
static json retrySuccess(const Scenario& s) {
    return concatenate(json::array({availableEvent(), setEvent(s), refreshEvent(s),
        reprobeEvent(0xFu, "refresh-mappings"), setEvent(s, 1)}), successTail(s, true));
}
static json expectedGuardTrace(const json& external, bool nullContext, bool deniedAtEnd = false) {
    json full = json::array();
    const auto guardEvent = [nullContext](bool allowed) {
        return json{{"op", "guard"}, {"allowed", allowed}, {"contextMatches", true}, {"contextWasNull", nullContext}};
    };
    for (const auto& event : external) {
        if (event["op"] == "set" || event["op"] == "refresh") full.push_back(guardEvent(true));
        full.push_back(event);
    }
    if (deniedAtEnd) full.push_back(guardEvent(false));
    return full;
}
static unsigned count(const json& events, const char* operation) {
    return static_cast<unsigned>(std::count_if(events.begin(), events.end(), [operation](const json& event) { return event["op"] == operation; }));
}
static bool adjacentGuards(const Observation& observation) {
    for (size_t i = 0; i < observation.trace.size(); ++i) {
        const auto& event = observation.trace[i];
        if (event["op"] == "set" || event["op"] == "refresh") {
            if (i == 0 || observation.trace[i - 1]["op"] != "guard" ||
                observation.trace[i - 1]["allowed"] != true) return false;
        }
    }
    return true;
}
struct Case {
    std::string name;
    Scenario scenario;
    bool returned, accepted;
    json expected;
};
static json allCases = json::array();
static unsigned assertions = 0, failedChecks = 0, executions = 0;
static void check(json& row, const char* name, bool pass) {
    ++assertions;
    if (!pass) ++failedChecks;
    row["checks"].push_back({{"name", name}, {"pass", pass}});
}
static bool passedRow(const json& row) {
    for (const auto& c : row["checks"]) if (!c["pass"].get<bool>()) return false;
    return true;
}
static void finish(json row) { row["passed"] = passedRow(row); allCases.push_back(std::move(row)); }
static std::vector<Case> legacyCases() {
    std::vector<Case> cases;
    for (BYTE slot = 0; slot < 4; ++slot) {
        for (const bool retry : {false, true}) for (const std::string readback : {"match", "different", "unknown", "get-fails", "proxy-lost"}) {
            Scenario s;
            s.slot = s.actual = slot;
            s.initialAccept = (slot % 2) == 0;
            if (retry) s.setResults = {failure, ok};
            if (readback == "different") s.actual = static_cast<BYTE>((slot + 1) % 4);
            if (readback == "unknown") s.actual = 0xFF;
            if (readback == "get-fails") s.getResult = disconnected;
            if (readback == "proxy-lost") s.readbackAvailable = false;
            cases.push_back({"slot-" + std::to_string(slot) + (retry ? "/retry/" : "/first/") + readback,
                s, true, observedSlot(s) == s.slot, retry ? retrySuccess(s) : firstSuccess(s)});
        }
    }
    for (const DWORD first : {unavailable, failure}) {
        Scenario s; s.slot = s.actual = 2; s.setResults = {first}; s.hasRefresh = false;
        cases.push_back({"failure-" + std::to_string(first) + "/no-refresh", s, false, false,
            json::array({availableEvent(), setEvent(s), reprobeEvent(slotMask(s), "assign-failed-uncertain")})});
        s.hasRefresh = true;
        for (const DWORD refreshed : {unavailable, failure, disconnected}) {
            s.refreshResult = refreshed;
            cases.push_back({"failure-" + std::to_string(first) + "/refresh-fails-" + std::to_string(refreshed), s, false, false,
                json::array({availableEvent(), setEvent(s), refreshEvent(s), reprobeEvent(slotMask(s), "assign-failed-uncertain")})});
        }
        s.refreshResult = ok;
        for (const DWORD retried : {unavailable, failure, disconnected}) {
            s.setResults = {first, retried};
            cases.push_back({"failure-" + std::to_string(first) + "/retry-fails-" + std::to_string(retried), s, false, false,
                json::array({availableEvent(), setEvent(s), refreshEvent(s), reprobeEvent(0xFu, "refresh-mappings"),
                    setEvent(s, 1), reprobeEvent(slotMask(s), "assign-failed-uncertain")})});
        }
        s.setResults = {first, ok};
        cases.push_back({"failure-" + std::to_string(first) + "/retry-succeeds", s, true, true, retrySuccess(s)});
    }
    Scenario s; s.setResults = {disconnected};
    cases.push_back({"first-device-disconnected", s, false, false, json::array({availableEvent(), setEvent(s)})});
    s = {}; s.available = false;
    cases.push_back({"proxy-unavailable", s, false, false, json::array({availableEvent(false)})});
    s = {}; s.path.clear();
    cases.push_back({"empty-device-path", s, false, false, json::array({availableEvent()})});
    for (const BYTE invalid : {static_cast<BYTE>(4), static_cast<BYTE>(0xFF)}) {
        s = {}; s.slot = invalid;
        cases.push_back({"invalid-slot-" + std::to_string(invalid), s, false, false, json::array({availableEvent()})});
    }
    // An opaque context and revoked admission have no meaning when no guard is supplied.
    for (const bool retry : {false, true}) {
        s = {}; s.initialAdmission = false;
        if (retry) s.setResults = {failure, ok};
        cases.push_back({retry ? "null-revoked-context/retry" : "null-revoked-context/first",
            s, true, true, retry ? retrySuccess(s) : firstSuccess(s)});
    }
    return cases;
}
static void verifyLegacy(const Case& test) {
    json row = {{"name", test.name}, {"family", "exact-legacy-equivalence"}, {"checks", json::array()}, {"observations", json::object()}};
    const auto old = execute(test.scenario, Mode::Baseline); ++executions;
    const json expectedView = {{"returned", test.returned}, {"accepted", test.accepted}, {"trace", test.expected}};
    check(row, "baseline matches independently specified full calls/results/logs/reprobes", old.view() == expectedView);
    row["expected"] = expectedView;
    row["observations"][modeName(Mode::Baseline)] = old.detail();
    for (const auto mode : {Mode::Default, Mode::NullGuard, Mode::NullWithContext, Mode::TrueWithContext, Mode::TrueDefaultContext}) {
        const bool guarded = mode == Mode::TrueWithContext || mode == Mode::TrueDefaultContext;
        if (guarded && !test.scenario.initialAdmission) continue;
        const auto current = execute(test.scenario, mode); ++executions;
        const bool nullContext = mode == Mode::TrueDefaultContext;
        const auto expectedFull = guarded ? expectedGuardTrace(test.expected, nullContext) : test.expected;
        check(row, (std::string(modeName(mode)) + ": exact baseline return/accept/external trace").c_str(), current.view() == old.view());
        check(row, (std::string(modeName(mode)) + ": exact complete trace including callback boundary/context").c_str(), current.trace == expectedFull);
        check(row, (std::string(modeName(mode)) + ": no unplanned calls and context preserved").c_str(),
            current.unexpectedCalls == 0 && current.contextCorrect &&
            current.guardCalls == (guarded ? count(test.expected, "set") + count(test.expected, "refresh") : 0));
        if (guarded) check(row, "every mutating proxy call immediately follows its successful callback", adjacentGuards(current));
        row["observations"][modeName(mode)] = current.detail();
    }
    finish(std::move(row));
}
static void verifyBoundary(const Case& test, bool denied, bool compareLegacy = false) {
    json row = {{"name", test.name}, {"family", "guard-boundary"}, {"checks", json::array()}, {"observations", json::object()}};
    const auto expectedView = json{{"returned", test.returned}, {"accepted", test.accepted}, {"trace", test.expected}};
    row["expected"] = expectedView;
    for (const auto mode : {Mode::TrueWithContext, Mode::TrueDefaultContext}) {
        const auto current = execute(test.scenario, mode); ++executions;
        check(row, "callback cancellation preserves specified return/accept and exact external effects", current.view() == expectedView);
        check(row, "exact successful boundary checks plus final rejection only when necessary",
            current.trace == expectedGuardTrace(test.expected, mode == Mode::TrueDefaultContext, denied));
        check(row, "all issued writes immediately follow successful admission", adjacentGuards(current));
        check(row, "context, callback count and finite proxy script preserved", current.contextCorrect && current.unexpectedCalls == 0 &&
            current.guardCalls == count(test.expected, "set") + count(test.expected, "refresh") + static_cast<unsigned>(denied));
        if (denied) check(row, "callback rejection clears accept and returns false", !current.returned && !current.accepted);
        if (compareLegacy) {
            const auto old = execute(test.scenario, Mode::Baseline); ++executions;
            check(row, "already issued operation completes exactly the legacy observation path", current.view() == old.view());
            check(row, "successful set still yields one readback/log and one success reprobe",
                count(current.external, "log") == 1 && count(current.external, "get") == (test.scenario.readbackAvailable ? 1u : 0u) &&
                count(current.external, "reprobe") == (test.scenario.setResults.size() == 1 ? 1u : 2u));
        }
        row["observations"][modeName(mode)] = current.detail();
    }
    finish(std::move(row));
}
static void boundaryCases() {
    for (const bool initialAccept : {false, true}) {
        Scenario s; s.initialAccept = initialAccept; s.initialAdmission = false;
        verifyBoundary({"deny-before-first-set/accept-seed-" + std::to_string(initialAccept), s, false, false,
            json::array({availableEvent()})}, true);
        s.initialAdmission = true; s.revoke = Revoke::Availability;
        verifyBoundary({"revoke-during-availability/accept-seed-" + std::to_string(initialAccept), s, false, false,
            json::array({availableEvent()})}, true);
        for (const DWORD first : {unavailable, failure}) {
            s = {}; s.initialAccept = initialAccept; s.setResults = {first, ok}; s.revoke = Revoke::FirstSet;
            verifyBoundary({"revoke-after-failed-first-set/" + std::to_string(first) + "/accept-seed-" + std::to_string(initialAccept),
                s, false, false, json::array({availableEvent(), setEvent(s)})}, true);
            s.revoke = Revoke::Refresh;
            verifyBoundary({"revoke-after-successful-refresh/" + std::to_string(first) + "/accept-seed-" + std::to_string(initialAccept),
                s, false, false, json::array({availableEvent(), setEvent(s), refreshEvent(s), reprobeEvent(0xFu, "refresh-mappings")})}, true);
        }
    }
    for (const BYTE slot : {static_cast<BYTE>(0), static_cast<BYTE>(3)}) {
        for (const bool retry : {false, true}) for (const std::string readback : {"match", "different", "get-fails", "proxy-lost"}) {
            Scenario s; s.slot = s.actual = slot;
            s.revoke = retry ? Revoke::RetrySet : Revoke::FirstSet;
            if (retry) s.setResults = {failure, ok};
            if (readback == "different") s.actual = static_cast<BYTE>((slot + 1) % 4);
            if (readback == "get-fails") s.getResult = disconnected;
            if (readback == "proxy-lost") s.readbackAvailable = false;
            verifyBoundary({"issued-success-after-revocation/slot-" + std::to_string(slot) + (retry ? "/retry/" : "/first/") + readback,
                s, true, observedSlot(s) == slot, retry ? retrySuccess(s) : firstSuccess(s)}, false, true);
        }
    }
    Scenario s; s.setResults = {failure}; s.refreshResult = failure; s.revoke = Revoke::Refresh;
    verifyBoundary({"revoked-during-failed-refresh-still-invalidates-once", s, false, false,
        json::array({availableEvent(), setEvent(s), refreshEvent(s), reprobeEvent(slotMask(s), "assign-failed-uncertain")})}, false);
    s = {}; s.setResults = {disconnected}; s.revoke = Revoke::FirstSet;
    verifyBoundary({"revoked-during-disconnected-set-keeps-terminal-no-reprobe", s, false, false,
        json::array({availableEvent(), setEvent(s)})}, false);
    s = {}; s.hasRefresh = false; s.setResults = {failure}; s.revoke = Revoke::FirstSet;
    verifyBoundary({"revoked-without-refresh-keeps-uncertain-reprobe", s, false, false,
        json::array({availableEvent(), setEvent(s), reprobeEvent(slotMask(s), "assign-failed-uncertain")})}, false);
    s = {}; s.setResults = {failure, failure}; s.revoke = Revoke::RetrySet;
    verifyBoundary({"revoked-during-failed-retry-keeps-refresh-and-uncertain-reprobes", s, false, false,
        json::array({availableEvent(), setEvent(s), refreshEvent(s), reprobeEvent(0xFu, "refresh-mappings"),
            setEvent(s, 1), reprobeEvent(slotMask(s), "assign-failed-uncertain")})}, false);
    s = {}; s.revoke = Revoke::Get;
    verifyBoundary({"revoked-during-readback-still-logs-and-reprobes-success", s, true, true, firstSuccess(s)}, false, true);
    // Failed preconditions must not invoke even a rejecting callback.
    s = {}; s.initialAdmission = false; s.available = false;
    verifyBoundary({"proxy-unavailable-does-not-call-rejecting-guard", s, false, false, json::array({availableEvent(false)})}, false);
    s = {}; s.initialAdmission = false; s.path.clear();
    verifyBoundary({"empty-path-does-not-call-rejecting-guard", s, false, false, json::array({availableEvent()})}, false);
    s = {}; s.initialAdmission = false; s.slot = 4;
    verifyBoundary({"invalid-slot-does-not-call-rejecting-guard", s, false, false, json::array({availableEvent()})}, false);
}
int main() {
    try {
        for (const auto& test : legacyCases()) verifyLegacy(test);
        boundaryCases();
        unsigned failedCases = 0;
        for (const auto& row : allCases) if (!row["passed"].get<bool>()) ++failedCases;
        std::cout << json{{"passed", failedChecks == 0}, {"caseCount", allCases.size()},
            {"executions", executions}, {"assertions", assertions}, {"failedChecks", failedChecks},
            {"failedCases", failedCases}, {"cases", allCases}, {"realDeviceOperations", 0},
            {"productExecuted", false}, {"hostExecuted", false}, {"physicalSuccessClaimed", false}}.dump(2) << '\n';
        return failedChecks ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
