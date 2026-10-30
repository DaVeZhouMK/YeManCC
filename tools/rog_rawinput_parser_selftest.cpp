// Offline fixture only. Production behavior comes from extracted_production.hpp.
#define NOMINMAX
#include <windows.h>
#include <hidsdi.h>
#include <Xinput.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <mutex>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
using json = nlohmann::json;

#ifndef FIXTURE_TEST_REGISTRATION
#define FIXTURE_TEST_REGISTRATION 0
#endif
#if FIXTURE_TEST_REGISTRATION
// Only the identity resolver and external effects are substituted. The enum,
// family predicate/name, sink helper and registration function are verbatim.
#define FIXTURE_REGISTRATION_TYPES
#include "extracted_production.hpp"
#undef FIXTURE_REGISTRATION_TYPES
struct FixtureMachineIdentity {
    bool attempted = false, resolved = false;
    YmccFamily family = YmccFamily::Unknown;
};
static FixtureMachineIdentity g_machineIdentity;
static HWND g_hwnd = reinterpret_cast<HWND>(0x100);
static HWND g_rawInputSinkHwnd = nullptr;
static bool g_rawInputRegistered = false;
static std::atomic<bool> g_keyboardModifierRawSubscribed{false};
static constexpr UINT_PTR GP_STATE_FALLBACK_TIMER_ID = 0x5678;
static const HWND fixtureSink = reinterpret_cast<HWND>(0x200);
static const HWND fixtureStaleSink = reinterpret_cast<HWND>(0x199);
static YmccFamily fixtureResolvedFamily = YmccFamily::Unknown;
static json fixtureCase, fixtureEvents, fixtureRegistrations, fixtureLogs, fixtureFailures;
static unsigned fixtureResolveCalls, fixtureResolutions, fixtureClassCalls, fixtureCreateCalls;
static unsigned fixtureTimerCalls, fixtureLegionCalls;
static DWORD fixtureError;
static bool fixtureReadbackActive = false;
static unsigned fixtureReadbackQueryCalls, fixtureWindowThreadCalls;

static unsigned long long hwndValue(HWND hwnd) {
    return static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(hwnd));
}
static void checkRegistration(bool passed, const char* label) {
    if (!passed && std::find(fixtureFailures.begin(), fixtureFailures.end(), json(label)) == fixtureFailures.end())
        fixtureFailures.push_back(label);
}
static void recordApi(const char* api) {
    fixtureEvents.push_back({{"api", api}, {"attempted", g_machineIdentity.attempted},
                             {"resolved", g_machineIdentity.resolved}});
}
// The real resolver also calls msiNativeForegroundIdentityReady at its tail.
// That possible MSI observer message and cross-family timing are outside this mock.
static void ymccResolveMachineIdentityOnce() {
    checkRegistration(!fixtureReadbackActive, "readback-forbids-identity-resolution");
    ++fixtureResolveCalls;
    recordApi("resolve-identity");
    if (g_machineIdentity.attempted) return;
    ++fixtureResolutions;
    g_machineIdentity.attempted = true;
    g_machineIdentity.family = fixtureResolvedFamily;
    g_machineIdentity.resolved = fixtureResolvedFamily != YmccFamily::Unknown;
}
static LRESULT CALLBACK rawInputSinkWndProc(HWND, UINT, WPARAM, LPARAM) { return 0; }
static BOOL fixtureIsWindow(HWND hwnd) {
    recordApi("IsWindow");
    fixtureError = ERROR_INVALID_DATA;
    return fixtureReadbackActive
        ? hwnd && hwnd == g_rawInputSinkHwnd && fixtureCase.value("sinkLive", false)
        : hwnd == fixtureSink;
}
static HMODULE fixtureGetModuleHandleW(LPCWSTR name) {
    recordApi("GetModuleHandleW");
    checkRegistration(name == nullptr, "module-handle-argument");
    fixtureError = ERROR_INVALID_DATA;
    return reinterpret_cast<HMODULE>(0x300);
}
static ATOM fixtureRegisterClassExW(const WNDCLASSEXW* wc) {
    checkRegistration(!fixtureReadbackActive, "readback-forbids-register-class");
    ++fixtureClassCalls;
    recordApi("RegisterClassExW");
    checkRegistration(wc && wc->cbSize == sizeof(WNDCLASSEXW) &&
                      wc->lpfnWndProc == rawInputSinkWndProc && wc->lpszClassName &&
                      std::wstring(wc->lpszClassName) == L"YMCC_RawInputSink", "sink-window-class");
    fixtureError = fixtureCase.value("classError", DWORD(ERROR_SUCCESS));
    return fixtureError ? 0 : 1;
}
static HWND fixtureCreateWindowExW(DWORD exStyle, LPCWSTR className, LPCWSTR title, DWORD style,
                                  int x, int y, int width, int height, HWND parent, HMENU menu,
                                  HINSTANCE instance, LPVOID parameter) {
    checkRegistration(!fixtureReadbackActive, "readback-forbids-create-window");
    ++fixtureCreateCalls;
    recordApi("CreateWindowExW");
    checkRegistration(exStyle == 0 && className && std::wstring(className) == L"YMCC_RawInputSink" &&
                      title && !*title && style == WS_OVERLAPPED && x == 0 && y == 0 && width == 1 && height == 1 &&
                      !parent && !menu && instance == reinterpret_cast<HINSTANCE>(0x300) && !parameter,
                      "ordinary-hidden-sink-window");
    const DWORD error = fixtureCase.value("createError", DWORD(ERROR_SUCCESS));
    fixtureError = error ? error : ERROR_INVALID_DATA; // stale success error must not leak into diagnostics
    return error ? nullptr : fixtureSink;
}
static DWORD fixtureGetLastError() { return fixtureError; }
static BOOL fixtureRegisterRawInputDevices(PCRAWINPUTDEVICE devices, UINT count, UINT size) {
    checkRegistration(!fixtureReadbackActive, "readback-forbids-register-input");
    recordApi("RegisterRawInputDevices");
    checkRegistration(devices && size == sizeof(RAWINPUTDEVICE), "registration-size");
    const bool gamepad = fixtureRegistrations.size() % 2 == 0;
    checkRegistration(count == (gamepad ? 3u : 2u), "registration-count-and-order");
    json copied = json::array();
    for (UINT i = 0; devices && i < count; ++i)
        copied.push_back({{"page", devices[i].usUsagePage}, {"usage", devices[i].usUsage},
                          {"flags", devices[i].dwFlags}, {"target", hwndValue(devices[i].hwndTarget)}});
    fixtureRegistrations.push_back({{"devices", copied}, {"size", size}});
    const DWORD error = fixtureCase.value(gamepad ? "rawError" : "keyboardMouseError", DWORD(ERROR_SUCCESS));
    fixtureError = error ? error : ERROR_INVALID_DATA;
    return error ? FALSE : TRUE;
}
static UINT_PTR fixtureSetTimer(HWND hwnd, UINT_PTR id, UINT interval, TIMERPROC callback) {
    checkRegistration(!fixtureReadbackActive, "readback-forbids-set-timer");
    ++fixtureTimerCalls;
    recordApi("SetTimer");
    checkRegistration(hwnd == g_hwnd && id == GP_STATE_FALLBACK_TIMER_ID && interval == 50 && !callback,
                      "fallback-timer-main-window");
    fixtureError = ERROR_GEN_FAILURE;
    return id;
}
static bool legionMotionRegisterRawInput(HWND hwnd) {
    checkRegistration(!fixtureReadbackActive, "readback-forbids-legion-registration");
    ++fixtureLegionCalls;
    recordApi("legionMotionRegisterRawInput");
    checkRegistration(hwnd == g_hwnd, "legion-registration-main-window");
    fixtureError = ERROR_NOT_SUPPORTED;
    return true;
}
static void appendNativeLifecycleLog(const char* event, const json& fields) {
    checkRegistration(!fixtureReadbackActive, "readback-forbids-append-log");
    recordApi("appendNativeLifecycleLog");
    checkRegistration(std::string(event) == "gamepad-input-registration", "registration-diagnostic-event");
    fixtureLogs.push_back(fields);
}
// Check the fixed buffer contract without querying the OS.
static UINT fixtureGetRegisteredRawInputDevices(PRAWINPUTDEVICE devices, PUINT count, UINT size) {
    ++fixtureReadbackQueryCalls;
    recordApi("GetRegisteredRawInputDevices");
    checkRegistration(fixtureReadbackActive, "readback-query-only-in-readback-phase");
    const UINT supplied = count ? *count : 0;
    checkRegistration(devices && count && supplied == 32 && size == sizeof(RAWINPUTDEVICE),
                      "readback-one-fixed-32-device-buffer");
    fixtureEvents.back()["providedCapacity"] = supplied;
    const auto& table = fixtureCase.at("devices");
    const DWORD error = fixtureCase.at("readError");
    if (count) *count = fixtureCase.at("requiredCount").get<UINT>();
    fixtureError = error ? error : ERROR_GEN_FAILURE;
    if (error) return ~0u;
    const size_t copied = (std::min)(static_cast<size_t>(supplied), table.size());
    for (size_t i = 0; devices && i < copied; ++i) {
        const auto& entry = table.at(i);
        devices[i] = {entry.at("page").get<USHORT>(), entry.at("usage").get<USHORT>(),
                      entry.at("flags").get<DWORD>(),
                      reinterpret_cast<HWND>(entry.at("target").get<std::uintptr_t>())};
    }
    return fixtureCase.at("returnedCount").get<UINT>();
}
static DWORD fixtureGetWindowThreadProcessId(HWND hwnd, LPDWORD pid) {
    ++fixtureWindowThreadCalls;
    recordApi("GetWindowThreadProcessId");
    checkRegistration(fixtureReadbackActive && hwnd == g_rawInputSinkHwnd &&
                      fixtureCase.value("sinkLive", false) && pid, "readback-live-sink-thread-query");
    if (pid) *pid = fixtureCase.at("sinkPid").get<DWORD>();
    fixtureError = ERROR_INVALID_DATA; // Later successful calls must not leak this error.
    return fixtureCase.at("sinkThreadId").get<DWORD>();
}
#define GetRegisteredRawInputDevices fixtureGetRegisteredRawInputDevices
#define GetWindowThreadProcessId fixtureGetWindowThreadProcessId
#define IsWindow fixtureIsWindow
#define GetModuleHandleW fixtureGetModuleHandleW
#define RegisterClassExW fixtureRegisterClassExW
#define CreateWindowExW fixtureCreateWindowExW
#define GetLastError fixtureGetLastError
#define RegisterRawInputDevices fixtureRegisterRawInputDevices
#define SetTimer fixtureSetTimer
#include "extracted_production.hpp"

static YmccFamily familyFromInput(const std::string& value) {
    if (value == "Unknown") return YmccFamily::Unknown;
    if (value == "AsusRogAllyX") return YmccFamily::AsusRogAllyX;
    if (value == "AsusRogAlly") return YmccFamily::AsusRogAlly;
    if (value == "AsusRogAllyClassic") return YmccFamily::AsusRogAllyClassic;
    if (value == "AsusRogAllyXClassic") return YmccFamily::AsusRogAllyXClassic;
    if (value == "LenovoLegionGo") return YmccFamily::LenovoLegionGo;
    if (value == "MsiClaw") return YmccFamily::MsiClaw;
    if (value == "ValveSteamDeck") return YmccFamily::ValveSteamDeck;
    throw std::runtime_error("unsupported fixture family " + value);
}
static json identityState() {
    return {{"attempted", g_machineIdentity.attempted}, {"resolved", g_machineIdentity.resolved},
            {"family", ymccFamilyName(g_machineIdentity.family)}};
}
static void checkDiagnostic(const json& log, const char* key, const json& expected) {
    const std::string label = std::string("diagnostic.") + key;
    checkRegistration(log.contains(key) && log.at(key) == expected, label.c_str());
}
static json runRegistration(const json& row) {
    fixtureReadbackActive = false;
    fixtureCase = row;
    fixtureEvents = fixtureRegistrations = fixtureLogs = fixtureFailures = json::array();
    fixtureResolveCalls = fixtureResolutions = fixtureClassCalls = fixtureCreateCalls = 0;
    fixtureTimerCalls = fixtureLegionCalls = 0;
    fixtureError = ERROR_GEN_FAILURE;
    fixtureResolvedFamily = familyFromInput(row.at("family"));
    const bool cold = row.value("cold", false), expectSink = row.at("sink");
    const bool requested = row.at("family").get<std::string>().find("AsusRog") == 0;
    g_machineIdentity = { !cold, !cold && fixtureResolvedFamily != YmccFamily::Unknown,
                          cold ? YmccFamily::Unknown : fixtureResolvedFamily };
    g_rawInputSinkHwnd = row.value("stale", false) ? fixtureStaleSink : nullptr;
    g_rawInputRegistered = false;
    const bool rawOk = row.value("rawError", 0) == 0;
    const bool kmOk = row.value("keyboardMouseError", 0) == 0;
    g_keyboardModifierRawSubscribed.store(!kmOk); // prove both success and failure store the result
    const auto before = identityState();
    const unsigned repeats = row.value("repeats", 1u);
    const HWND expectedTarget = expectSink ? fixtureSink : g_hwnd;
    DWORD sinkError = row.value("classError", DWORD(ERROR_SUCCESS));
    if (sinkError == ERROR_CLASS_ALREADY_EXISTS) sinkError = ERROR_SUCCESS;
    if (!sinkError) sinkError = row.value("createError", DWORD(ERROR_SUCCESS));
    for (unsigned step = 0; step < repeats; ++step) {
        const unsigned resolveBefore = fixtureResolveCalls;
        checkRegistration(gamepadRegisterRawInput() == rawOk, "registration-return");
        checkRegistration(fixtureResolveCalls == resolveBefore + 1, "identity-resolver-call");
        checkRegistration(g_machineIdentity.attempted && g_machineIdentity.family == fixtureResolvedFamily &&
                          g_machineIdentity.resolved == (fixtureResolvedFamily != YmccFamily::Unknown),
                          "identity-resolved-before-registration");
        checkRegistration(g_rawInputRegistered == rawOk, "gamepad-registration-state");
        checkRegistration(g_keyboardModifierRawSubscribed.load() == kmOk, "keyboard-mouse-subscription-state");
        checkRegistration(fixtureRegistrations.size() == (step + 1) * 2, "two-registration-calls");
        if (fixtureRegistrations.size() >= (step + 1) * 2) {
            const auto& gamepads = fixtureRegistrations.at(step * 2).at("devices");
            const auto& km = fixtureRegistrations.at(step * 2 + 1).at("devices");
            checkRegistration(gamepads.size() == 3 && km.size() == 2, "usage-counts");
            const unsigned gamepadUsages[] = {0x05, 0x04, 0x08}, kmUsages[] = {0x06, 0x02};
            for (size_t i = 0; i < gamepads.size(); ++i) {
                const auto& device = gamepads.at(i);
                checkRegistration(i < 3 && device.at("page") == 1 && device.at("usage") == gamepadUsages[i],
                                  "three-gamepad-usages");
                checkRegistration(device.at("flags") == (RIDEV_INPUTSINK | RIDEV_DEVNOTIFY), "gamepad-flags");
                checkRegistration(device.at("target") == hwndValue(expectedTarget), "gamepad-target");
            }
            for (size_t i = 0; i < km.size(); ++i) {
                const auto& device = km.at(i);
                checkRegistration(i < 2 && device.at("page") == 1 && device.at("usage") == kmUsages[i],
                                  "keyboard-mouse-usages");
                checkRegistration(device.at("flags") == (RIDEV_INPUTSINK | RIDEV_DEVNOTIFY), "keyboard-mouse-flags");
                checkRegistration(device.at("target") == hwndValue(g_hwnd), "keyboard-mouse-target");
            }
        }
        checkRegistration(fixtureLogs.size() == step + 1, "one-registration-diagnostic");
        if (fixtureLogs.size() > step) {
            const auto& log = fixtureLogs.at(step);
            checkDiagnostic(log, "rawInputRegistered", rawOk);
            checkDiagnostic(log, "gamepadRawInputRegistered", rawOk);
            checkDiagnostic(log, "keyboardMouseRawInputRegistered", kmOk);
            checkDiagnostic(log, "machineIdentityAttempted", true);
            checkDiagnostic(log, "machineIdentityResolved", fixtureResolvedFamily != YmccFamily::Unknown);
            checkDiagnostic(log, "machineFamily", ymccFamilyName(fixtureResolvedFamily));
            checkDiagnostic(log, "gamepadSinkRequested", requested);
            checkDiagnostic(log, "gamepadSinkCreateError", sinkError);
            checkDiagnostic(log, "gamepadSinkCreated", expectSink);
            checkDiagnostic(log, "gamepadSinkFallbackToMainWindow", !expectSink);
            checkDiagnostic(log, "gamepadSinkHwnd", hwndValue(expectSink ? fixtureSink : nullptr));
            checkDiagnostic(log, "gamepadTargetHwnd", hwndValue(expectedTarget));
            checkDiagnostic(log, "rawInputError", row.value("rawError", 0));
            checkDiagnostic(log, "keyboardMouseRawInputError", row.value("keyboardMouseError", 0));
            checkDiagnostic(log, "fallbackTimerArmed", true);
            checkDiagnostic(log, "fallbackTimerId", GP_STATE_FALLBACK_TIMER_ID);
        }
    }
    checkRegistration(fixtureClassCalls == (requested ? 1u : 0u), "sink-class-attempts-and-reuse");
    const bool classFailed = row.value("classError", 0) && row.value("classError", 0) != ERROR_CLASS_ALREADY_EXISTS;
    checkRegistration(fixtureCreateCalls == (requested && !classFailed ? 1u : 0u), "sink-create-attempts-and-reuse");
    checkRegistration(fixtureResolutions == (cold ? 1u : 0u), "identity-resolution-idempotent");
    checkRegistration(fixtureTimerCalls == repeats && fixtureLegionCalls == repeats, "existing-registration-side-effects");
    const auto registrationIdentity = identityState();
    const auto resolveCallsAtRegistration = fixtureResolveCalls;
    if (row.value("probeErrorReset", false)) {
        DWORD error = ERROR_GEN_FAILURE;
#if FIXTURE_SINK_ERROR_PARAMETER
        checkRegistration(ensureRawInputSinkWindow(&error) == fixtureSink && error == ERROR_SUCCESS,
                          "reused-sink-error-reset");
        // Also call the new helper through the preserved default argument.
        checkRegistration(ensureRawInputSinkWindow() == fixtureSink, "sink-default-argument");
        g_rawInputSinkHwnd = nullptr;
        error = ERROR_GEN_FAILURE;
        checkRegistration(ensureRawInputSinkWindow(&error) == fixtureSink && error == ERROR_SUCCESS,
                          "new-sink-error-reset");
#else
        (void)error;
        checkRegistration(ensureRawInputSinkWindow() == fixtureSink, "sink-default-argument");
        checkRegistration(false, "sink-error-out-parameter");
#endif
    }
    // Logical delayed completion only: no Sleep, real clock or device resolution.
    if (row.value("lateResolve", false)) ymccResolveMachineIdentityOnce();
    return {{"id", row.at("id")}, {"phase", "registration"}, {"failures", fixtureFailures},
            {"beforeIdentity", before}, {"registrationIdentity", registrationIdentity}, {"lateIdentity", identityState()},
            {"lateResolveSimulatedAfterMs", row.value("lateResolve", false) ? json(33) : json(nullptr)},
            {"mainHwnd", hwndValue(g_hwnd)}, {"expectedTargetHwnd", hwndValue(expectedTarget)},
            {"resolverCallsDuringRegistration", resolveCallsAtRegistration}, {"events", fixtureEvents},
            {"registrations", fixtureRegistrations}, {"diagnostics", fixtureLogs},
            {"classCalls", fixtureClassCalls}, {"createCalls", fixtureCreateCalls}};
}
static json readbackState() {
    return {{"identity", identityState()}, {"mainHwnd", hwndValue(g_hwnd)},
            {"sinkHwnd", hwndValue(g_rawInputSinkHwnd)}, {"rawInputRegistered", g_rawInputRegistered},
            {"keyboardMouseSubscribed", g_keyboardModifierRawSubscribed.load()}};
}
static json runRegistrationReadback(const json& row) {
    fixtureReadbackActive = true;
    fixtureCase = row;
    fixtureEvents = fixtureRegistrations = fixtureLogs = fixtureFailures = json::array();
    fixtureResolveCalls = fixtureResolutions = fixtureClassCalls = fixtureCreateCalls = 0;
    fixtureTimerCalls = fixtureLegionCalls = fixtureReadbackQueryCalls = fixtureWindowThreadCalls = 0;
    fixtureError = ERROR_GEN_FAILURE;
    g_machineIdentity = {true, true, YmccFamily::AsusRogAllyX};
    g_hwnd = reinterpret_cast<HWND>(0x100);
    g_rawInputSinkHwnd = reinterpret_cast<HWND>(row.at("sinkHwnd").get<std::uintptr_t>());
    g_rawInputRegistered = true;
    g_keyboardModifierRawSubscribed.store(true);
    const auto before = readbackState();
    json observed;
#if FIXTURE_REGISTRATION_READBACK
    observed = rogRawInputRegistrationEvidence(); // Exact extracted production body.
#else
    checkRegistration(false, "readback-helper-not-extracted");
#endif
    const auto after = readbackState();
    for (auto it = row.at("expected").begin(); it != row.at("expected").end(); ++it) {
        const std::string label = "readback." + it.key();
        checkRegistration(observed.contains(it.key()) && observed.at(it.key()) == it.value(), label.c_str());
    }
    checkRegistration(fixtureReadbackQueryCalls == 1, "readback-single-query-no-resize-retry");
    checkRegistration(fixtureWindowThreadCalls == (row.at("sinkLive").get<bool>() ? 1u : 0u),
                      "readback-thread-query-only-for-live-sink");
    const size_t windowQueries = static_cast<size_t>(std::count_if(fixtureEvents.begin(), fixtureEvents.end(),
        [](const json& event) { return event.at("api") == "IsWindow"; }));
    checkRegistration(windowQueries == (g_rawInputSinkHwnd ? 1u : 0u), "readback-sink-liveness-query");
    const auto writes = fixtureClassCalls + fixtureCreateCalls + fixtureResolveCalls + fixtureTimerCalls +
                        fixtureLegionCalls + fixtureRegistrations.size() + fixtureLogs.size();
    checkRegistration(writes == 0, "readback-no-registration-creation-or-other-writes");
    checkRegistration(before == after, "readback-input-state-unchanged");
    fixtureReadbackActive = false;
    return {{"id", row.at("id")}, {"phase", "registration-readback"}, {"failures", fixtureFailures},
            {"observed", observed}, {"expected", row.at("expected")}, {"events", fixtureEvents},
            {"beforeState", before}, {"afterState", after}, {"queryCalls", fixtureReadbackQueryCalls},
            {"windowThreadCalls", fixtureWindowThreadCalls}, {"forbiddenWriteCalls", writes},
            {"registrations", fixtureRegistrations}, {"classCalls", fixtureClassCalls},
            {"createCalls", fixtureCreateCalls}};
}
int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("expected input.jsonl output.jsonl");
        std::ifstream input(argv[1]);
        std::ofstream output(argv[2]);
        if (!input || !output) throw std::runtime_error("cannot open registration input/output");
        unsigned cases = 0, failed = 0, readbackQueries = 0;
        size_t readbackWrites = 0;
        std::map<std::string, unsigned> phaseCases, failuresByAssertion;
        std::string line;
        while (std::getline(input, line)) {
            const auto row = json::parse(line);
            const bool readback = row.value("phase", std::string()) == "registration-readback";
            const auto result = readback ? runRegistrationReadback(row) : runRegistration(row);
            ++cases;
            ++phaseCases[result.at("phase").get<std::string>()];
            if (readback) {
                readbackQueries += result.at("queryCalls").get<unsigned>();
                readbackWrites += result.at("forbiddenWriteCalls").get<size_t>();
            }
            if (!result.at("failures").empty()) ++failed;
            for (const auto& failure : result.at("failures")) ++failuresByAssertion[failure.get<std::string>()];
            output << result.dump() << '\n';
        }
        if (!cases) throw std::runtime_error("empty registration input");
        std::cout << json{{"cases", cases}, {"failed", failed}, {"failuresByAssertion", failuresByAssertion},
                          {"casesByPhase", phaseCases}, {"readbackQueryCalls", readbackQueries},
                          {"readbackForbiddenWriteCalls", readbackWrites},
                          {"realRawInputCalls", 0}, {"realWindowCalls", 0}, {"realDeviceReads", 0}}.dump() << '\n';
        return failed ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
#else

struct ReplayDescriptor {
    std::vector<HIDP_VALUE_CAPS> caps;
    std::map<USAGE, ULONG> values;
    std::map<USAGE, LONG> scaled;
    std::map<USAGE, USHORT> links;
    std::vector<USAGE> buttons;
    std::vector<char> report;
    unsigned rawCalls = 0, scaledCalls = 0, capsCalls = 0, shimErrors = 0, invalidReportCalls = 0;
};
static ReplayDescriptor* current = nullptr;
static PHIDP_PREPARSED_DATA capturePrep = nullptr;
static std::vector<ReplayDescriptor>* batchDescriptors = nullptr;
static bool validPrep(PHIDP_PREPARSED_DATA prep) {
    return current && (prep == reinterpret_cast<PHIDP_PREPARSED_DATA>(current) || prep == capturePrep);
}
static bool validReport(PHIDP_PREPARSED_DATA prep, PCHAR report, ULONG size) {
    if (batchDescriptors) {
        bool matched = false;
        for (auto& d : *batchDescriptors) {
            if (size == d.report.size() && std::equal(d.report.begin(), d.report.end(), report)) {
                current = &d;
                matched = true;
                break;
            }
        }
        if (!matched) { ++current->invalidReportCalls; return false; }
    }
    if (!validPrep(prep) || size != current->report.size() ||
        !std::equal(current->report.begin(), current->report.end(), report)) {
        if (current) ++current->shimErrors;
        return false;
    }
    return true;
}
static NTSTATUS fixtureGetValueCaps(HIDP_REPORT_TYPE type, PHIDP_VALUE_CAPS caps,
                                    PUSHORT count, PHIDP_PREPARSED_DATA prep) {
    if (!validPrep(prep) || type != HidP_Input)
        throw std::runtime_error("wrong descriptor/type at caps shim");
    ++current->capsCalls;
    if (*count < current->caps.size()) {
        *count = static_cast<USHORT>(current->caps.size());
        return HIDP_STATUS_BUFFER_TOO_SMALL;
    }
    std::copy(current->caps.begin(), current->caps.end(), caps);
    *count = static_cast<USHORT>(current->caps.size());
    return HIDP_STATUS_SUCCESS;
}
static NTSTATUS fixtureGetUsageValue(HIDP_REPORT_TYPE type, USAGE page, USHORT link,
    USAGE usage, PULONG value, PHIDP_PREPARSED_DATA prep, PCHAR report, ULONG size) {
    if (!validReport(prep, report, size)) return HIDP_STATUS_INVALID_REPORT_LENGTH;
    ++current->rawCalls;
    if (type != HidP_Input || page != 1 || !current->values.count(usage) ||
        current->links.at(usage) != link) return HIDP_STATUS_USAGE_NOT_FOUND;
    *value = current->values.at(usage);
    return HIDP_STATUS_SUCCESS;
}
static NTSTATUS fixtureGetScaledUsageValue(HIDP_REPORT_TYPE type, USAGE page, USHORT link,
    USAGE usage, PLONG value, PHIDP_PREPARSED_DATA prep, PCHAR report, ULONG size) {
    if (!validReport(prep, report, size)) return HIDP_STATUS_INVALID_REPORT_LENGTH;
    ++current->scaledCalls;
    if (type != HidP_Input || page != 1 || !current->scaled.count(usage) ||
        current->links.at(usage) != link) return HIDP_STATUS_USAGE_NOT_FOUND;
    *value = current->scaled.at(usage);
    return HIDP_STATUS_SUCCESS;
}
static NTSTATUS fixtureGetUsages(HIDP_REPORT_TYPE type, USAGE page, USHORT link,
    PUSAGE usages, PULONG count, PHIDP_PREPARSED_DATA prep, PCHAR report, ULONG size) {
    if (!validReport(prep, report, size)) return HIDP_STATUS_INVALID_REPORT_LENGTH;
    if (type != HidP_Input || page != 9 || link != 0) return HIDP_STATUS_USAGE_NOT_FOUND;
    if (*count < current->buttons.size()) return HIDP_STATUS_BUFFER_TOO_SMALL;
    std::copy(current->buttons.begin(), current->buttons.end(), usages);
    *count = static_cast<ULONG>(current->buttons.size());
    return HIDP_STATUS_SUCCESS;
}
// The optional batch test calls the actual production capture function. These
// dependencies model only its operating-system boundary and pad sink.
struct FixtureRogSession {
    bool identityMatched = true, hasStableInput = true, inputPathIsMi05 = true;
    bool buttonMapLocked = true, rawDeviceMatchesInputPath = false;
    int buttonMap = 0;
    USHORT productId = 0x1B4C;
    HANDLE rawDevice = nullptr;
    unsigned long long rawDeviceTopology = 0;
    std::wstring inputPath = L"fixture-MI05";
    ULONGLONG lastHidTick = 0, lastHidPower = 0, lastHidTopology = 0, lastHidSequence = 0;
};
struct FixtureRawEvidence {
    unsigned long long received = 0, accepted = 0, rejected = 0, parseFailures = 0;
    unsigned lastReportBytes = 0, lastReportCount = 0;
    DWORD lastError = 0;
    const char* lastCapture = "no-report";
};
static FixtureRogSession g_rog;
static FixtureRawEvidence g_rogRawEvidence;
static std::mutex g_gamepadReadMx;
static bool fixtureBoundaryEntered = true;
static thread_local bool tls_rogRemapActive = false;
static unsigned fixtureNameCalls = 0, fixtureNameFailures = 0;
struct RogRemapBoundaryScope {
    bool entered;
    explicit RogRemapBoundaryScope(int) : entered(fixtureBoundaryEntered) {}
};
static std::atomic<unsigned long long> g_xinputTopologyEpoch{3};
static unsigned long long currentPowerGeneration() { return 7; }
static bool rogIsAllyPid(USHORT pid) { return pid == 0x1B4C; }
static std::vector<XINPUT_GAMEPAD> capturedPads;
static void rogAcceptHidPad(const XINPUT_GAMEPAD& pad) { capturedPads.push_back(pad); }
static UINT fixtureRawInputDeviceInfo(HANDLE, UINT command, void* data, UINT* size) {
    if (command == RIDI_DEVICEINFO) {
        auto info = static_cast<RID_DEVICE_INFO*>(data);
        info->dwType = RIM_TYPEHID;
        info->hid.dwVendorId = 0x0B05;
        info->hid.dwProductId = 0x1B4C;
        return sizeof(*info);
    }
    if (command == RIDI_DEVICENAME) {
        ++fixtureNameCalls;
        if (fixtureNameFailures) { --fixtureNameFailures; return static_cast<UINT>(-1); }
        const std::wstring name = L"fixture-MI05";
        if (*size <= name.size()) return static_cast<UINT>(-1);
        std::copy(name.begin(), name.end(), static_cast<wchar_t*>(data));
        static_cast<wchar_t*>(data)[name.size()] = 0;
        return static_cast<UINT>(name.size());
    }
    if (command == RIDI_PREPARSEDDATA) {
        if (data) { std::memset(data, 0, *size); capturePrep = reinterpret_cast<PHIDP_PREPARSED_DATA>(data); }
        *size = 16;
        return data ? 16 : 0;
    }
    return static_cast<UINT>(-1);
}
static ULONGLONG fixtureTicks() { return 100000; }
static LONG fixtureMessageTime() { return 99990; }
static DWORD fixtureLastError() { return ERROR_SUCCESS; }
#define GetRawInputDeviceInfoW fixtureRawInputDeviceInfo
#define GetTickCount64 fixtureTicks
#define GetMessageTime fixtureMessageTime
#define GetLastError fixtureLastError
#define HidP_GetValueCaps fixtureGetValueCaps
#define HidP_GetUsageValue fixtureGetUsageValue
#define HidP_GetScaledUsageValue fixtureGetScaledUsageValue
#define HidP_GetUsages fixtureGetUsages
#include "extracted_production.hpp"

static json padJson(const XINPUT_GAMEPAD& p) {
    return {{"wButtons", p.wButtons}, {"bLeftTrigger", p.bLeftTrigger},
        {"bRightTrigger", p.bRightTrigger}, {"sThumbLX", p.sThumbLX},
        {"sThumbLY", p.sThumbLY}, {"sThumbRX", p.sThumbRX}, {"sThumbRY", p.sThumbRY}};
}
static void loadDescriptor(const json& row, ReplayDescriptor& d) {
    const auto hex = row.at("rawHex").get<std::string>();
    if (hex.empty() || hex.size() % 2) throw std::runtime_error("invalid rawHex");
    for (size_t i = 0; i < hex.size(); i += 2)
        d.report.push_back(static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    for (const auto& c : row.at("caps")) {
        HIDP_VALUE_CAPS cap{};
        cap.UsagePage = static_cast<USAGE>(c.value("page", 1));
        cap.LinkCollection = static_cast<USHORT>(c.value("link", 0));
        cap.BitSize = static_cast<USHORT>(c.value("bits", 16));
        cap.LogicalMin = c.at("min").get<LONG>();
        cap.LogicalMax = c.at("max").get<LONG>();
        cap.IsRange = c.contains("last");
        if (cap.IsRange) {
            cap.Range.UsageMin = c.at("usage").get<USAGE>();
            cap.Range.UsageMax = c.at("last").get<USAGE>();
        } else cap.NotRange.Usage = c.at("usage").get<USAGE>();
        d.caps.push_back(cap);
    }
    for (const auto& v : row.at("values")) {
        const auto u = v.at("usage").get<USAGE>();
        d.values[u] = v.at("raw").get<ULONG>();
        d.scaled[u] = v.value("scaled", 17); // Deliberately different physical units.
        d.links[u] = static_cast<USHORT>(v.value("link", 0));
    }
    d.buttons = row.at("buttons").get<std::vector<USAGE>>();
}
#if FIXTURE_TEST_CAPTURE
static json runBatch(const json& row) {
    std::vector<ReplayDescriptor> descriptors(row.at("batch").size());
    size_t index = 0;
    for (const auto& sample : row.at("batch")) loadDescriptor(sample, descriptors[index++]);
    const size_t stride = descriptors.front().report.size();
    const size_t offset = offsetof(RAWINPUT, data.hid.bRawData);
    const size_t total = offset + stride * descriptors.size();
    std::vector<ULONGLONG> storage((total + sizeof(ULONGLONG) - 1) / sizeof(ULONGLONG));
    auto raw = reinterpret_cast<RAWINPUT*>(storage.data());
    raw->header.dwType = RIM_TYPEHID;
    raw->header.hDevice = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0x1234));
    raw->header.dwSize = static_cast<DWORD>(total - row.value("truncate", 0));
    raw->data.hid.dwSizeHid = row.value("stride", static_cast<unsigned>(stride));
    raw->data.hid.dwCount = row.value("count", static_cast<unsigned>(descriptors.size()));
    for (size_t i = 0; i < descriptors.size(); ++i) {
        if (descriptors[i].report.size() != stride) throw std::runtime_error("batch strides differ");
        std::copy(descriptors[i].report.begin(), descriptors[i].report.end(),
                  raw->data.hid.bRawData + i * stride);
    }
    g_rog = FixtureRogSession{};
    g_rogRawEvidence = FixtureRawEvidence{};
    capturedPads.clear();
    fixtureNameCalls = 0;
    fixtureNameFailures = row.value("nameFailOnce", false) ? 1 : 0;
    fixtureBoundaryEntered = row.value("boundaryEntered", true);
    current = &descriptors.front();
    capturePrep = nullptr;
    batchDescriptors = &descriptors;
    rogCaptureRawInput(raw); // Actual extracted loop, not a fixture reimplementation.
    json failures = json::array(), actual = json::array();
    json firstAttempt = nullptr;
    if (row.value("retry", false)) {
        firstAttempt = {{"captured", capturedPads.size()}, {"nameCalls", fixtureNameCalls},
                        {"cacheDeviceNull", g_rog.rawDevice == nullptr},
                        {"cacheTopology", g_rog.rawDeviceTopology}};
        if (!capturedPads.empty()) failures.push_back("failed-name-query-published");
        if (g_rog.rawDevice != nullptr || g_rog.rawDeviceTopology != 0)
            failures.push_back("failed-name-query-committed-cache");
        rogCaptureRawInput(raw); // Same handle/topology, next delivered report must retry.
    }
    if (row.contains("expectedNameCalls") && fixtureNameCalls != row.at("expectedNameCalls").get<unsigned>())
        failures.push_back("name-query-call-count");
    for (const auto& pad : capturedPads) actual.push_back(padJson(pad));
    const size_t expectedCount = row.at("expectedCount");
    if (capturedPads.size() != expectedCount) failures.push_back("accepted-frame-count");
    if (expectedCount == descriptors.size() && capturedPads.size() == expectedCount) {
        for (size_t i = 0; i < expectedCount; ++i) {
            const auto& sample = row.at("batch").at(i);
            const json pad = actual.at(i);
            for (const auto& item : sample.at("expected").items()) {
                const int got = pad.at(item.key()), want = item.value();
                const int mask = sample.value("masks", json::object()).value(item.key(), -1);
                if (mask >= 0 ? (got & mask) != want : std::abs(got - want) > 1)
                    failures.push_back("frame-" + std::to_string(i) + "." + item.key());
            }
        }
        if (g_rog.lastHidSequence != expectedCount) failures.push_back("published-sequence");
        if (g_rog.lastHidTick != 99990 || g_rog.lastHidPower != 7 || g_rog.lastHidTopology != 3)
            failures.push_back("published-context");
    }
    if (!expectedCount && std::string(g_rogRawEvidence.lastCapture) != row.value("expectedReason", "invalid-report-bounds"))
        failures.push_back("capture-rejection-reason");
    unsigned errors = 0, invalidReports = 0;
    for (const auto& d : descriptors) { errors += d.shimErrors; invalidReports += d.invalidReportCalls; }
    if (invalidReports) failures.push_back("parser-received-concatenated-or-wrong-report");
    if (errors) failures.push_back("shim-report-mismatch");
    batchDescriptors = nullptr;
    capturePrep = nullptr;
    current = nullptr;
    return {{"id", row.at("id")}, {"phase", "batch"}, {"actual", actual}, {"failures", failures},
            {"captureReason", g_rogRawEvidence.lastCapture}, {"sequence", g_rog.lastHidSequence},
            {"shimErrors", errors}, {"invalidReportCalls", invalidReports},
            {"nameCalls", fixtureNameCalls}, {"firstAttempt", firstAttempt}};
}
#endif
int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("expected input.jsonl output.jsonl");
        std::ifstream input(argv[1]);
        std::ofstream output(argv[2]);
        if (!input || !output) throw std::runtime_error("cannot open fixture input/output");
        unsigned cases = 0, failed = 0, neutralFailures = 0, shimErrors = 0;
        unsigned historical = 0, rawCalls = 0, scaledCalls = 0;
        std::map<std::string, unsigned> failedByPhase, casesByPhase;
        XINPUT_GAMEPAD previousPad{XINPUT_GAMEPAD_A, 255, 255, 32767, -32768, 32767, -32768};
        std::string line;
        while (std::getline(input, line)) {
            const json row = json::parse(line);
#if FIXTURE_TEST_CAPTURE
            if (row.contains("batch")) {
                const auto result = runBatch(row);
                ++cases;
                ++casesByPhase["batch"];
                if (!result.at("failures").empty()) { ++failed; ++failedByPhase["batch"]; }
                output << result.dump() << '\n';
                continue;
            }
#endif
            ReplayDescriptor descriptor;
            loadDescriptor(row, descriptor);
            current = &descriptor;
            XINPUT_GAMEPAD pad = previousPad;
            auto prep = reinterpret_cast<PHIDP_PREPARSED_DATA>(current);
            bool parsed = false;
            if (row.value("mi05", true)) {
#if FIXTURE_HAS_MI05_PARAMETER
                parsed = rogParseReport(prep, descriptor.report.data(),
                    static_cast<ULONG>(descriptor.report.size()), row.value("map", 0), &pad, true);
#else
                parsed = rogParseReport(prep, descriptor.report.data(),
                    static_cast<ULONG>(descriptor.report.size()), row.value("map", 0), &pad);
#endif
            } else {
                // Intentionally omit the optional argument: exercise the generic default.
                parsed = rogParseReport(prep, descriptor.report.data(),
                    static_cast<ULONG>(descriptor.report.size()), row.value("map", 0), &pad);
            }
            previousPad = pad;
            const auto actual = padJson(pad);
            json failures = json::array();
            if (parsed != row.value("parsed", true)) failures.push_back("parsed");
            for (const auto& item : row.at("expected").items()) {
                const auto key = item.key();
                const int got = actual.at(key).get<int>(), want = item.value().get<int>();
                const int mask = row.value("masks", json::object()).value(key, -1);
                const int tolerance = key.find("sThumb") == 0 ? 1 : 0;
                if (mask >= 0 ? (got & mask) != want : std::abs(got - want) > tolerance) {
                    const auto neutral = row.value("neutralAxes", std::vector<std::string>{});
                    const bool isNeutral = std::find(neutral.begin(), neutral.end(), key) != neutral.end();
                    failures.push_back(isNeutral ? "neutral." + key : key);
                    if (isNeutral) ++neutralFailures;
                }
            }
            const bool activity = rogPadHasActivity(pad);
            if (row.contains("activity") && activity != row.at("activity").get<bool>())
                failures.push_back("activity");
            if (row.value("rawOnly", false) && descriptor.scaledCalls) failures.push_back("scaled-physical-used");
            if (descriptor.shimErrors) failures.push_back("shim-report-mismatch");
            const std::string phase = row.at("phase");
            ++cases;
            ++casesByPhase[phase];
            if (phase == "historical") ++historical;
            if (!failures.empty()) { ++failed; ++failedByPhase[phase]; }
            shimErrors += descriptor.shimErrors;
            rawCalls += descriptor.rawCalls;
            scaledCalls += descriptor.scaledCalls;
            output << json{{"id", row.at("id")}, {"phase", phase}, {"parsed", parsed},
                {"actual", actual}, {"activity", activity}, {"failures", failures},
                {"rawCalls", descriptor.rawCalls}, {"scaledCalls", descriptor.scaledCalls},
                {"capsCalls", descriptor.capsCalls}}.dump() << '\n';
            current = nullptr;
        }
        if (!cases) throw std::runtime_error("empty fixture input");
        std::cout << json{{"cases", cases}, {"historical", historical}, {"failed", failed},
            {"neutralAxisFailures", neutralFailures}, {"failedByPhase", failedByPhase}, {"casesByPhase", casesByPhase},
            {"shimErrors", shimErrors}, {"rawCalls", rawCalls}, {"scaledCalls", scaledCalls}}.dump() << '\n';
        return failed ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}

#endif // FIXTURE_TEST_REGISTRATION
