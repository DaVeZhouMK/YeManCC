// R114 Windows-only private process test. No product, Host, device, Steam or driver calls.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "one_shot_helper_diagnostic.h"
#include "native_refresh_writer_runtime.h"
using json = nlohmann::json;
using Capture = ymcc::input::OneShotHelperDiagnostic;

// Inert dependencies used only by the exact extracted runtime lease/receipt methods.
namespace ymcc::nativeForeground {
enum class Profile { Msi, Rog, Gpd };
static const char* name(Profile) { return "msi"; }
// Transport-only dependency: deliberately does not execute production policy.
// The full native TU oracle separately verifies mutation admission/zero writes.
static bool gameNodeRefreshMutationAllowed(Profile, const std::string&, const std::string&) { return true; }
}
struct NativeForegroundRefreshTarget {};
struct HidHidePhysicalIdentity { std::string deviceInstanceId; };
struct HidHideVisibilitySnapshot {};
static json receiptSink;
static std::string receiptDomain;
static json logHistory = json::array();
static bool throwLogSink = false;
static std::string W2U(const std::wstring& text) {
    if (text.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count, nullptr, nullptr);
    return result;
}
static void appendNativeLifecycleLog(const char* domain, const json& value) {
    if (throwLogSink) throw std::runtime_error("fixture log sink failure");
    logHistory.push_back({{"domain", domain}, {"value", value}});
    receiptDomain = domain;
    receiptSink = value;
}
// Lazy/inert Cycle dependencies: the only possible executable is this fixture itself.
namespace fspath = std::filesystem;
static std::wstring fixtureCycleImage;
static bool fixtureLaunchFailure = false;
static unsigned fixtureCycleCreateCalls = 0;
static unsigned fixtureCycleHashCalls = 0;
static std::wstring inputHostExecutablePath() { return fixtureCycleImage; }
static std::wstring U2W(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}
static std::string sha256File(const std::wstring&) { ++fixtureCycleHashCalls; return "fixture-inert-file-hash"; }
static BOOL fixtureCycleCreateProcessW(LPCWSTR image, LPWSTR command, LPSECURITY_ATTRIBUTES processAttributes,
    LPSECURITY_ATTRIBUTES threadAttributes, BOOL inherit, DWORD flags, LPVOID environment, LPCWSTR cwd,
    LPSTARTUPINFOW startup, LPPROCESS_INFORMATION process) {
    ++fixtureCycleCreateCalls;
    if (fixtureLaunchFailure) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return ::CreateProcessW(image, command, processAttributes, threadAttributes, inherit, flags, environment, cwd, startup, process);
}
namespace ymcc::processEfficiency {
static BOOL createProcess(LPCWSTR image, LPWSTR command, LPSECURITY_ATTRIBUTES processAttributes,
    LPSECURITY_ATTRIBUTES threadAttributes, BOOL inherit, DWORD flags, LPVOID environment, LPCWSTR cwd,
    LPSTARTUPINFOW startup, LPPROCESS_INFORMATION process) {
    return fixtureCycleCreateProcessW(image, command, processAttributes, threadAttributes, inherit,
        flags, environment, cwd, startup, process);
}
}
// The tracked GPD launch is outside this transport fixture. Compile its real
// types, but fail before any writer record/Job can be created if entered.
static unsigned unadmittedTrackedWriterCalls = 0;
static std::wstring nativeRefreshWriterRecordPath() {
    ++unadmittedTrackedWriterCalls; throw std::logic_error("unadmitted tracked writer path");
}
static std::string inputHostTargetReturnMachineBinding() {
    ++unadmittedTrackedWriterCalls; throw std::logic_error("unadmitted tracked writer machine");
}
static json nativeRefreshWriterEvidence(const ymcc::nativeRefreshWriter::LaunchScope&, const char*) {
    ++unadmittedTrackedWriterCalls; throw std::logic_error("unadmitted tracked writer evidence");
}
// Generated from the bound production runtime without editing its method bodies.
#include "runtime-consumer.actual.h"
#include "producer-replay.actual.h"

static const std::string prefix = "YMCC_MSI_GAME_AUTHORITY_DIAGNOSTIC ";
static json checks = json::array();
static json cases = json::array();
static json lifecycle = json::array();
static json producerPlayback = json::array();
static unsigned assertions = 0;
static unsigned failures = 0;
static unsigned children = 0;
static unsigned terminalCalls = 0;
static unsigned readsWhileAlive = 0;
static std::wstring executable;
static std::wstring privateDirectory;

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(std::string(message) + " (Win32=" + std::to_string(GetLastError()) + ")");
}
static void check(bool passed, const std::string& label, const json& evidence = nullptr) {
    ++assertions;
    if (!passed) ++failures;
    checks.push_back({{"test", label}, {"passed", passed}, {"evidence", evidence}});
}
struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct Process {
    PROCESS_INFORMATION info{};
    ~Process() {
        if (info.hThread) CloseHandle(info.hThread);
        if (info.hProcess) {
            if (WaitForSingleObject(info.hProcess, 0) == WAIT_TIMEOUT) {
                TerminateProcess(info.hProcess, 91); // This fixture's exact child handle only.
                WaitForSingleObject(info.hProcess, 5000);
            }
            CloseHandle(info.hProcess);
        }
    }
};
static DWORD handleCount() {
    DWORD count = 0;
    require(GetProcessHandleCount(GetCurrentProcess(), &count) != FALSE, "GetProcessHandleCount");
    return count;
}
static json files() {
    json names = json::array();
    WIN32_FIND_DATAW entry{};
    const HANDLE search = FindFirstFileW((privateDirectory + L"\\*").c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE) {
        require(GetLastError() == ERROR_FILE_NOT_FOUND, "FindFirstFileW own directory");
        return names;
    }
    do {
        const std::wstring name(entry.cFileName);
        if (name != L"." && name != L"..") names.push_back(W2U(name));
    } while (FindNextFileW(search, &entry));
    FindClose(search);
    return names;
}
static json validRecord(const char* stage = "first") {
    return {{"Schema", 1}, {"Kind", "ymcc.gameAuthority.failure"}, {"Stage", stage}, {"Result", 8},
        {"FixtureOnly", true}, {"Description", "synthetic stdout; no hardware call"}};
}
static json refreshRecord(const char* stage = "enable") {
    return {{"Schema", 1}, {"Kind", "ymcc.gameRefresh.failure"}, {"Stage", stage}, {"Result", 6},
        {"Xusb", true}, {"PrepareLocateResult", 0}, {"CurrentLocateResult", 0}, {"SameNode", true},
        {"DisableCallEntered", true}, {"DisableResult", 0}, {"EnableCallEntered", true}, {"EnableResult", 4294967295ULL},
        {"ExceptionType", nullptr}, {"ExceptionHResult", nullptr},
        {"FixtureOnly", true}, {"Description", "synthetic refresh diagnostics; no device action"}};
}
static std::string wire(const json& record) { return prefix + record.dump() + "\n"; }
static bool writeAll(HANDLE output, const std::string& bytes) {
    size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        const DWORD wanted = static_cast<DWORD>((bytes.size() - offset) > 65536 ? 65536 : bytes.size() - offset);
        if (!WriteFile(output, bytes.data() + offset, wanted, &written, nullptr) || !written) return false;
        offset += written;
    }
    return true;
}
static std::wstring eventName(const wchar_t* suffix) {
    return L"Local\\YMCC-R114-Diagnostic-" + std::to_wstring(GetCurrentProcessId()) + L"-" + suffix;
}
static bool spawn(Process& process, Capture* capture, const std::wstring& arguments, bool broadInheritance = false,
                  const std::wstring& image = {}) {
    const auto& path = image.empty() ? executable : image;
    std::wstring command = L"\"" + path + L"\" --child " + arguments;
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(0);
    STARTUPINFOW basic{};
    basic.cb = sizeof(basic);
    basic.dwFlags = STARTF_USESHOWWINDOW;
    basic.wShowWindow = SW_HIDE;
    LPSTARTUPINFOW startup = &basic;
    DWORD flags = CREATE_NO_WINDOW;
    if (capture) {
        startup = capture->startup();
        if (broadInheritance) {
            // Positive control: deliberately omit the handle list for OUR sentinel only.
            basic = *startup;
            basic.cb = sizeof(basic);
            startup = &basic;
        } else flags |= EXTENDED_STARTUPINFO_PRESENT;
    }
    const BOOL launched = CreateProcessW(path.c_str(), buffer.data(), nullptr, nullptr, capture ? TRUE : FALSE,
        flags, nullptr, privateDirectory.c_str(), startup, &process.info);
    if (!launched) return false;
    ++children;
    if (capture) capture->started();
    CloseHandle(process.info.hThread);
    process.info.hThread = nullptr;
    return true;
}
static DWORD completed(Process& process) {
    require(WaitForSingleObject(process.info.hProcess, 10000) == WAIT_OBJECT_0, "private child bounded completion");
    DWORD exitCode = STILL_ACTIVE;
    require(GetExitCodeProcess(process.info.hProcess, &exitCode) != FALSE, "GetExitCodeProcess private child");
    return exitCode;
}
static json terminal(Capture& capture, Process& process, DWORD code) {
    ++terminalCalls;
    if (WaitForSingleObject(process.info.hProcess, 0) != WAIT_OBJECT_0) {
        ++readsWhileAlive;
        throw std::runtime_error("terminal prohibited before private child termination");
    }
    return capture.terminal(code);
}
static bool facts(const json& value) {
    return value.is_object() && (value.contains("reportedFacts") || value.value("state", "") == "record-accepted");
}
struct Scenario { std::string name; std::string bytes; DWORD code; std::string expected; bool stderrOnly = false; };
static std::vector<Scenario> scenarios() {
    std::vector<Scenario> result;
    auto add = [&](const std::string& name, const std::string& bytes, DWORD code, const std::string& expected) {
        result.push_back({name, bytes, code, expected});
    };
    add("valid-first-exit8", wire(validRecord()), 8, "accepted");
    add("valid-after-prepare-exit8", wire(validRecord("after-prepare")), 8, "accepted");
    add("empty-success-no-record", "", 0, "missing");
    add("empty-failure-no-record", "", 8, "missing");
    add("wrong-prefix", "OTHER " + validRecord().dump(), 8, "rejected");
    add("leading-noise", "noise\n" + wire(validRecord()), 8, "rejected");
    add("prefix-only", prefix, 8, "rejected");
    add("valid-record-wrong-exit0", wire(validRecord()), 0, "rejected");
    add("valid-record-wrong-exit7", wire(validRecord()), 7, "rejected");
    add("valid-record-still-active-code", wire(validRecord()), STILL_ACTIVE, "rejected");
    for (const auto& field : std::vector<std::string>{"Schema", "Kind", "Stage", "Result"}) {
        auto record = validRecord(); record.erase(field);
        add("missing-" + field, wire(record), 8, "rejected");
        const std::vector<std::pair<std::string, json>> types = {
            {"null", nullptr}, {"string", "1"}, {"bool", true}, {"array", json::array()}, {"object", json::object()}};
        for (const auto& item : types) {
            record = validRecord(); record[field] = item.second;
            add("wrong-type-" + field + "-" + item.first, wire(record), 8, "rejected");
        }
    }
    for (const auto& item : std::vector<std::pair<std::string, json>>{{"zero", 0}, {"two", 2},
             {"fractional", 1.75}, {"float", 1.0}, {"negative", -1}, {"wrap32", 4294967297ULL}}) {
        auto record = validRecord(); record["Schema"] = item.second;
        add("wrong-Schema-" + item.first, wire(record), 8, "rejected");
    }
    for (const auto& item : std::vector<std::pair<std::string, json>>{{"seven", 7}, {"nine", 9},
             {"fractional", 8.75}, {"float", 8.0}, {"negative", -8}, {"wrap32", 4294967304ULL}}) {
        auto record = validRecord(); record["Result"] = item.second;
        add("wrong-Result-" + item.first, wire(record), 8, "rejected");
    }
    for (const auto& stage : {"other", "", "First", "after_prepare"}) {
        auto record = validRecord(); record["Stage"] = stage;
        add(std::string("wrong-stage-") + stage, wire(record), 8, "rejected");
    }
    auto wrongKind = validRecord(); wrongKind["Kind"] = "ymcc.gameAuthority.success";
    add("wrong-kind", wire(wrongKind), 8, "rejected");
    add("truncated-json", prefix + "{\"Schema\":1,\"Kind\":", 8, "rejected");
    add("invalid-json", prefix + "{\"Schema\":1,}", 8, "rejected");
    add("json-array", prefix + "[]", 8, "rejected");
    add("json-null", prefix + "null", 8, "rejected");
    add("duplicate-record", wire(validRecord()) + wire(validRecord("after-prepare")), 8, "rejected");
    add("duplicate-json-without-prefix", wire(validRecord()) + validRecord().dump(), 8, "rejected");
    add("trailing-garbage", wire(validRecord()) + "garbage", 8, "rejected");
    add("embedded-nul", wire(validRecord()) + std::string(1, '\0') + "x", 8, "rejected");
    add("duplicate-conflicting-schema-key", prefix + "{\"Schema\":2,\"Schema\":1,\"Kind\":\"ymcc.gameAuthority.failure\",\"Stage\":\"first\",\"Result\":8}", 8, "rejected");
    add("duplicate-conflicting-result-key", prefix + "{\"Schema\":1,\"Kind\":\"ymcc.gameAuthority.failure\",\"Stage\":\"first\",\"Result\":7,\"Result\":8}", 8, "rejected");
    // Result=6 is a separate, bounded diagnostic kind. Every scenario still
    // launches a private child; it changes no refresh operation or authority.
    for (const auto& stage : {"admission", "prepare", "prepare-locate", "revalidate", "current-locate", "current-authority", "disable", "enable"})
        add(std::string("refresh-valid-stage-") + stage, wire(refreshRecord(stage)), 6, "accepted");
    auto minimal = json{{"Schema", 1}, {"Kind", "ymcc.gameRefresh.failure"}, {"Stage", "admission"}, {"Result", 6}, {"FixtureOnly", true}};
    add("refresh-minimal-optional-fields-absent", wire(minimal), 6, "accepted");
    for (const auto code : {DWORD(0), DWORD(7), DWORD(8), DWORD(STILL_ACTIVE)})
        add("refresh-wrong-exit-" + std::to_string(code), wire(refreshRecord()), code, "rejected");
    for (const auto& field : std::vector<std::string>{"Schema", "Kind", "Stage", "Result"}) {
        auto record = refreshRecord(); record.erase(field);
        add("refresh-missing-" + field, wire(record), 6, "rejected");
        for (const auto& item : std::vector<std::pair<std::string, json>>{{"null", nullptr}, {"string", "6"}, {"bool", true}, {"array", json::array()}, {"object", json::object()}}) {
            record = refreshRecord(); record[field] = item.second;
            add("refresh-wrong-type-" + field + "-" + item.first, wire(record), 6, "rejected");
        }
    }
    for (const auto& item : std::vector<std::pair<std::string, json>>{{"float", 1.0}, {"fractional", 1.5}, {"negative", -1}, {"wrap32", 4294967297ULL}}) {
        auto record = refreshRecord(); record["Schema"] = item.second;
        add("refresh-wrong-Schema-" + item.first, wire(record), 6, "rejected");
    }
    for (const auto& item : std::vector<std::pair<std::string, json>>{{"eight", 8}, {"float", 6.0}, {"fractional", 6.5}, {"negative", -6}, {"wrap32", 4294967302ULL}}) {
        auto record = refreshRecord(); record["Result"] = item.second;
        add("refresh-wrong-Result-" + item.first, wire(record), 6, "rejected");
    }
    for (const auto& stage : {"first", "after-prepare", "", "Enable", "enabled", "success"}) {
        auto record = refreshRecord(); record["Stage"] = stage;
        add(std::string("refresh-wrong-stage-") + stage, wire(record), 6, "rejected");
    }
    for (const auto& field : {"Xusb", "DisableCallEntered", "EnableCallEntered"}) {
        for (const auto& item : std::vector<std::pair<std::string, json>>{{"null", nullptr}, {"int", 1}, {"string", "true"}, {"array", json::array()}}) {
            auto record = refreshRecord(); record[field] = item.second;
            add(std::string("refresh-bool-") + field + "-" + item.first, wire(record), 6, "rejected");
        }
    }
    for (const auto& item : std::vector<std::pair<std::string, json>>{{"null", nullptr}, {"false", false}}) {
        auto record = refreshRecord(); record["SameNode"] = item.second;
        add("refresh-valid-SameNode-" + item.first, wire(record), 6, "accepted");
    }
    for (const auto& item : std::vector<std::pair<std::string, json>>{{"int", 0}, {"string", "false"}, {"object", json::object()}}) {
        auto record = refreshRecord(); record["SameNode"] = item.second;
        add("refresh-wrong-SameNode-" + item.first, wire(record), 6, "rejected");
    }
    for (const auto& field : {"PrepareLocateResult", "CurrentLocateResult", "DisableResult", "EnableResult"}) {
        for (const auto& item : std::vector<std::pair<std::string, json>>{{"null", nullptr}, {"zero", 0}, {"max", 4294967295ULL}}) {
            auto record = refreshRecord(); record[field] = item.second;
            add(std::string("refresh-valid-uint-") + field + "-" + item.first, wire(record), 6, "accepted");
        }
        for (const auto& item : std::vector<std::pair<std::string, json>>{{"negative", -1}, {"wrap", 4294967296ULL}, {"huge", 18446744073709551615ULL}, {"float", 0.0}, {"fractional", 0.5}, {"bool", false}, {"string", "0"}, {"array", json::array()}}) {
            auto record = refreshRecord(); record[field] = item.second;
            add(std::string("refresh-wrong-uint-") + field + "-" + item.first, wire(record), 6, "rejected");
        }
    }
    for (const auto& item : std::vector<std::pair<std::string, json>>{{"null", nullptr}, {"min", -2147483647LL - 1}, {"max", 2147483647}, {"zero", 0}}) {
        auto record = refreshRecord(); record["ExceptionHResult"] = item.second;
        add("refresh-valid-hresult-" + item.first, wire(record), 6, "accepted");
    }
    for (const auto& item : std::vector<std::pair<std::string, json>>{{"underflow", -2147483649LL}, {"overflow", 2147483648ULL}, {"float", 0.0}, {"bool", true}, {"string", "-1"}}) {
        auto record = refreshRecord(); record["ExceptionHResult"] = item.second;
        add("refresh-wrong-hresult-" + item.first, wire(record), 6, "rejected");
    }
    for (const auto& item : std::vector<std::pair<std::string, json>>{{"null", nullptr}, {"text", "System.InvalidOperationException"}}) {
        auto record = refreshRecord(); record["ExceptionType"] = item.second;
        add("refresh-valid-exception-type-" + item.first, wire(record), 6, "accepted");
    }
    for (const auto& item : std::vector<std::pair<std::string, json>>{{"int", 6}, {"bool", true}, {"array", json::array()}, {"nul", std::string("X\0Y", 3)}}) {
        auto record = refreshRecord(); record["ExceptionType"] = item.second;
        add("refresh-wrong-exception-type-" + item.first, wire(record), 6, "rejected");
    }
    auto noProof = refreshRecord(); noProof["HardwareSuccessProven"] = false;
    add("refresh-explicit-no-hardware-proof", wire(noProof), 6, "accepted");
    for (const auto& item : std::vector<std::pair<std::string, json>>{{"true", true}, {"int", 0}, {"null", nullptr}}) {
        auto record = refreshRecord(); record["HardwareSuccessProven"] = item.second;
        add("refresh-forbidden-hardware-proof-" + item.first, wire(record), 6, "rejected");
    }
    add("refresh-duplicate-Schema", prefix + "{\"Schema\":2,\"Schema\":1,\"Kind\":\"ymcc.gameRefresh.failure\",\"Stage\":\"enable\",\"Result\":6}", 6, "rejected");
    add("refresh-duplicate-optional", prefix + "{\"Schema\":1,\"Kind\":\"ymcc.gameRefresh.failure\",\"Stage\":\"enable\",\"Result\":6,\"DisableResult\":1,\"DisableResult\":0}", 6, "rejected");
    add("refresh-concatenated-record", wire(refreshRecord()) + wire(validRecord()), 6, "rejected");
    add("refresh-embedded-NUL", wire(refreshRecord()) + std::string(1, '\0'), 6, "rejected");
    add("refresh-malformed-json", prefix + "{\"Kind\":\"ymcc.gameRefresh.failure\",}", 6, "rejected");
    std::string refreshExact = wire(refreshRecord()); refreshExact.append(2048 - refreshExact.size(), ' ');
    add("refresh-exact-2048-valid", refreshExact, 6, "accepted");
    add("refresh-oversize-2049", refreshExact + " ", 6, "oversize");
    // Compensation facts extend only Result=6 observation. No field grants
    // recovery authority and a zero compensating Enable never changes exit 6.
    auto compensation = refreshRecord();
    compensation["CompensationChecked"] = true;
    compensation["CompensationIdentityMatched"] = true;
    compensation["CompensationLocateResult"] = 0;
    compensation["CompensationSameNode"] = true;
    compensation["CompensationAttempted"] = true;
    compensation["CompensationEnableResult"] = 0;
    compensation["CompensationExceptionType"] = nullptr;
    compensation["CompensationExceptionHResult"] = nullptr;
    add("compensation-zero-keeps-original-failure6", wire(compensation), 6, "accepted");
    add("compensation-zero-does-not-accept-exit0", wire(compensation), 0, "rejected");
    for (const auto* field : {"CompensationChecked", "CompensationIdentityMatched", "CompensationLocateResult",
            "CompensationSameNode", "CompensationAttempted", "CompensationEnableResult",
            "CompensationExceptionType", "CompensationExceptionHResult"}) {
        auto record = compensation; record.erase(field);
        add(std::string("compensation-absent-") + field, wire(record), 6, "accepted");
    }
    for (const auto* field : {"CompensationChecked", "CompensationAttempted"}) {
        for (const auto& item : std::vector<std::pair<std::string, json>>{{"true", true}, {"false", false}}) {
            auto record = compensation; record[field] = item.second;
            add(std::string("compensation-valid-bool-") + field + "-" + item.first, wire(record), 6, "accepted");
        }
        for (const auto& item : std::vector<std::pair<std::string, json>>{{"null", nullptr}, {"int", 0}, {"float", 0.0}, {"string", "true"}, {"array", json::array()}, {"object", json::object()}}) {
            auto record = compensation; record[field] = item.second;
            add(std::string("compensation-wrong-bool-") + field + "-" + item.first, wire(record), 6, "rejected");
        }
    }
    for (const auto* field : {"CompensationIdentityMatched", "CompensationSameNode"}) {
        for (const auto& item : std::vector<std::pair<std::string, json>>{{"null", nullptr}, {"true", true}, {"false", false}}) {
            auto record = compensation; record[field] = item.second;
            add(std::string("compensation-valid-nullable-bool-") + field + "-" + item.first, wire(record), 6, "accepted");
        }
        for (const auto& item : std::vector<std::pair<std::string, json>>{{"int", 1}, {"float", 1.0}, {"string", "false"}, {"array", json::array()}, {"object", json::object()}}) {
            auto record = compensation; record[field] = item.second;
            add(std::string("compensation-wrong-nullable-bool-") + field + "-" + item.first, wire(record), 6, "rejected");
        }
    }
    for (const auto* field : {"CompensationLocateResult", "CompensationEnableResult"}) {
        for (const auto& item : std::vector<std::pair<std::string, json>>{{"null", nullptr}, {"zero", 0}, {"max", 4294967295ULL}}) {
            auto record = compensation; record[field] = item.second;
            add(std::string("compensation-valid-uint-") + field + "-" + item.first, wire(record), 6, "accepted");
        }
        for (const auto& item : std::vector<std::pair<std::string, json>>{{"negative", -1}, {"wrap", 4294967296ULL}, {"huge", 18446744073709551615ULL}, {"float", 0.0}, {"fractional", 0.5}, {"bool", false}, {"string", "0"}, {"array", json::array()}, {"object", json::object()}}) {
            auto record = compensation; record[field] = item.second;
            add(std::string("compensation-wrong-uint-") + field + "-" + item.first, wire(record), 6, "rejected");
        }
    }
    for (const auto& item : std::vector<std::pair<std::string, json>>{{"null", nullptr}, {"min", -2147483647LL - 1}, {"max", 2147483647}, {"zero", 0}}) {
        auto record = compensation; record["CompensationExceptionHResult"] = item.second;
        add("compensation-valid-hresult-" + item.first, wire(record), 6, "accepted");
    }
    for (const auto& item : std::vector<std::pair<std::string, json>>{{"underflow", -2147483649LL}, {"overflow", 2147483648ULL}, {"huge", 18446744073709551615ULL}, {"float", 0.0}, {"fractional", -0.5}, {"bool", true}, {"string", "-1"}, {"array", json::array()}, {"object", json::object()}}) {
        auto record = compensation; record["CompensationExceptionHResult"] = item.second;
        add("compensation-wrong-hresult-" + item.first, wire(record), 6, "rejected");
    }
    for (const auto& item : std::vector<std::pair<std::string, json>>{{"null", nullptr}, {"empty", ""}, {"text", "System.ApplicationException"}}) {
        auto record = compensation; record["CompensationExceptionType"] = item.second;
        add("compensation-valid-exception-type-" + item.first, wire(record), 6, "accepted");
    }
    for (const auto& item : std::vector<std::pair<std::string, json>>{{"int", 6}, {"float", 6.0}, {"bool", true}, {"array", json::array()}, {"object", json::object()}, {"nul", std::string("X\0Y", 3)}}) {
        auto record = compensation; record["CompensationExceptionType"] = item.second;
        add("compensation-wrong-exception-type-" + item.first, wire(record), 6, "rejected");
    }
    add("compensation-duplicate-field", prefix + "{\"Schema\":1,\"Kind\":\"ymcc.gameRefresh.failure\",\"Stage\":\"enable\",\"Result\":6,\"CompensationAttempted\":false,\"CompensationAttempted\":true}", 6, "rejected");
    std::string compensationExact = wire(compensation); compensationExact.append(2048 - compensationExact.size(), ' ');
    add("compensation-exact-2048-valid", compensationExact, 6, "accepted");
    add("compensation-oversize-2049", compensationExact + " ", 6, "oversize");
    std::string exact = wire(validRecord()); exact.append(2048 - exact.size(), ' ');
    add("exact-2048-valid", exact, 8, "accepted");
    add("oversize-2049", exact + " ", 8, "oversize");
    add("oversize-8192", std::string(8192, 'x'), 8, "oversize");
    add("oversize-2MiB-no-pipe-block", std::string(2 * 1024 * 1024, 'x'), 8, "oversize");
    result.push_back({"stderr-record-not-stdout", wire(validRecord()), 8, "missing", true});
    return result;
}
static int child(int argc, wchar_t** argv) {
    if (argc < 3) return 90;
    const std::wstring mode(argv[2]);
    if (mode == L"fallback-success") return 0;
    if (mode == L"producer") {
        if (argc != 4) return 90;
        const size_t index = static_cast<size_t>(_wtoi(argv[3]));
        if (index >= producerReplays.size()) return 90;
        return writeAll(GetStdHandle(STD_OUTPUT_HANDLE), producerReplays[index].bytes) ? static_cast<int>(producerReplays[index].exitCode) : 92;
    }
    if (mode == L"case") {
        if (argc != 4) return 90;
        const auto all = scenarios();
        const size_t index = static_cast<size_t>(_wtoi(argv[3]));
        if (index >= all.size()) return 90;
        const auto& test = all[index];
        if (!writeAll(GetStdHandle(test.stderrOnly ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE), test.bytes)) return 92;
        return static_cast<int>(test.code);
    }
    if (mode == L"inherit") {
        if (argc != 4) return 90;
        const HANDLE sentinel = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(_wcstoui64(argv[3], nullptr, 10)));
        auto record = validRecord();
        record["SentinelSignaled"] = SetEvent(sentinel) != FALSE;
        record["StdoutType"] = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
        record["StdinType"] = GetFileType(GetStdHandle(STD_INPUT_HANDLE));
        record["StderrEqualsStdin"] = GetStdHandle(STD_ERROR_HANDLE) == GetStdHandle(STD_INPUT_HANDLE);
        record["StdoutDifferent"] = GetStdHandle(STD_OUTPUT_HANDLE) != GetStdHandle(STD_INPUT_HANDLE);
        return writeAll(GetStdHandle(STD_OUTPUT_HANDLE), wire(record)) ? 8 : 92;
    }
    if (mode == L"hold") {
        if (argc != 5) return 90;
        Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[3]));
        Handle release(OpenEventW(SYNCHRONIZE, FALSE, argv[4]));
        if (!ready.value || !release.value) return 93;
        if (!writeAll(GetStdHandle(STD_OUTPUT_HANDLE), wire(validRecord("after-prepare")))) return 92;
        FlushFileBuffers(GetStdHandle(STD_OUTPUT_HANDLE));
        if (!SetEvent(ready.value)) return 94;
        // Bounded failsafe even if this fixture parent is interrupted.
        return WaitForSingleObject(release.value, 15000) == WAIT_OBJECT_0 ? 8 : 95;
    }
    return 90;
}
static void ordinaryCases() {
    const auto all = scenarios();
    for (size_t i = 0; i < all.size(); ++i) {
        const auto& test = all[i];
        json observed;
        DWORD code = STILL_ACTIVE;
        {
            Capture capture;
            require(capture.prepare(privateDirectory), "prepare actual capture");
            check(files().size() == 1, test.name + ": one owned spool while prepared");
            Process process;
            require(spawn(process, &capture, L"case " + std::to_wstring(i)), "launch private scenario");
            code = completed(process);
            observed = terminal(capture, process, code);
        }
        const bool accepted = facts(observed);
        bool pass = !accepted;
        if (test.expected == "accepted") pass = accepted && observed.value("state", "") == "record-accepted" &&
            observed.value("hardwareSuccessProven", true) == false && observed["reportedFacts"]["FixtureOnly"] == true;
        if (test.expected == "missing") pass = !accepted && observed.is_object() && observed.value("state", "") == "record-missing";
        if (test.expected == "oversize") pass = !accepted && observed.is_object() && observed.value("state", "") == "record-oversize";
        check(pass, test.name + ": terminal policy", observed);
        check(code == test.code, test.name + ": original exit unchanged", code);
        check(files().empty(), test.name + ": delete-on-close cleanup", files());
        cases.push_back({{"name", test.name}, {"expected", test.expected}, {"exitCode", code},
            {"stdoutBytesExpected", test.stderrOnly ? 0 : test.bytes.size()}, {"inputText", test.bytes.size() <= 2048 ? json(test.bytes) : json(nullptr)}, {"passed", pass}, {"actual", observed}});
    }
}
static void fallbackAndFailure() {
    // Observe first failed CreateProcess with NO diagnostic capture at all.
    // Some Windows components initialize per-process state on this path.
    const DWORD controlBefore = handleCount();
    json failureControl = json::array();
    for (unsigned i = 0; i < 4; ++i) {
        {
            Process process;
            check(!spawn(process, nullptr, L"fallback-success", false, executable + L".no-capture-missing"),
                "raw missing-image control has no capture or child");
        }
        failureControl.push_back(handleCount());
    }
    check(failureControl[1] == failureControl[2] && failureControl[2] == failureControl[3],
        "repeated no-capture CreateProcess failure handle count stabilizes", failureControl);
    lifecycle.push_back({{"mode", "missing-image-no-capture-control"}, {"beforeHandles", controlBefore},
        {"afterEachFailureHandles", failureControl}, {"captureCreated", false}, {"spoolCreated", false}});
    const DWORD baseline = handleCount();
    {
        auto capture = std::make_unique<Capture>();
        const bool prepared = capture->prepare(privateDirectory + L"\\intentionally-absent");
        check(!prepared, "prepare failure in own missing directory");
        if (!prepared) capture.reset();
        Process process;
        require(spawn(process, capture.get(), L"fallback-success"), "original launch after prepare failure");
        check(completed(process) == 0, "prepare failure preserves basic launch exit0");
    }
    check(handleCount() == baseline && files().empty(), "prepare failure fallback releases handles and creates no file");
    {
        Capture capture;
        require(capture.prepare(privateDirectory), "prepare before launch failure");
        Process process;
        check(!spawn(process, &capture, L"fallback-success", false, executable + L".does-not-exist"), "launch failure is captured without child");
    }
    check(handleCount() == baseline && files().empty(), "launch failure releases unstarted capture", {{"baseline", baseline}, {"after", handleCount()}, {"files", files()}});
    for (unsigned i = 0; i < 16; ++i) {
        {
            Capture capture;
            require(capture.prepare(privateDirectory), "repeat launch-failure prepare");
            Process process;
            check(!spawn(process, &capture, L"fallback-success", false, executable + L".repeat-missing"),
                "repeat launch-failure remains a failure");
        }
        check(handleCount() == baseline && files().empty(), "repeat launch-failure releases capture handles and spool");
    }
    {
        Capture capture;
        const auto result = capture.terminal(0);
        check(!facts(result) && result.value("state", "") == "capture-read-unavailable", "unprepared capture cannot invent facts", result);
    }
    for (unsigned i = 0; i < 8; ++i) {
        Capture capture;
        require(capture.prepare(privateDirectory), "prepare abandoned own spool");
    }
    check(handleCount() == baseline && files().empty(), "abandoned prepared captures release handles and files", {{"baseline", baseline}, {"after", handleCount()}, {"files", files()}});
}
static void inheritance(bool positiveControl) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    Handle sentinel(CreateEventW(&sa, TRUE, FALSE, nullptr));
    require(sentinel.value != nullptr, "create own inheritable sentinel");
    DWORD flags = 0;
    require(GetHandleInformation(sentinel.value, &flags) != FALSE, "sentinel handle information");
    check((flags & HANDLE_FLAG_INHERIT) != 0, "sentinel is actually inheritable");
    json result;
    {
        Capture capture;
        require(capture.prepare(privateDirectory), "prepare inheritance capture");
        auto* startup = capture.startup();
        DWORD outputFlags = 0, nullFlags = 0;
        require(GetHandleInformation(startup->hStdOutput, &outputFlags) != FALSE, "stdout handle information");
        require(GetHandleInformation(startup->hStdInput, &nullFlags) != FALSE, "NUL handle information");
        check(startup->cb == sizeof(STARTUPINFOEXW) && startup->hStdInput == startup->hStdError &&
            startup->hStdInput != startup->hStdOutput && (outputFlags & HANDLE_FLAG_INHERIT) &&
            (nullFlags & HANDLE_FLAG_INHERIT), "real startup exposes two distinct inheritable standard handles");
        Process process;
        const auto argument = L"inherit " + std::to_wstring(reinterpret_cast<uintptr_t>(sentinel.value));
        require(spawn(process, &capture, argument, positiveControl), "launch inheritance private child");
        const auto code = completed(process);
        result = terminal(capture, process, code);
    }
    const bool signaled = WaitForSingleObject(sentinel.value, 0) == WAIT_OBJECT_0;
    check(facts(result) && result["reportedFacts"].value("SentinelSignaled", !positiveControl) == positiveControl &&
        signaled == positiveControl, positiveControl ? "inheritance detector positive control sees own leaked event" :
        "explicit handle list excludes extra inheritable event", result);
    check(result["reportedFacts"]["StdoutType"] == FILE_TYPE_DISK && result["reportedFacts"]["StdinType"] == FILE_TYPE_CHAR &&
        result["reportedFacts"]["StderrEqualsStdin"] == true && result["reportedFacts"]["StdoutDifferent"] == true,
        "child receives disk stdout and shared NUL input/error", result);
    check(files().empty(), "inheritance spool deleted");
}
static void resetLease() {
    g_msiNativeForegroundLease = MsiNativeForegroundLease{};
    g_msiNativeForegroundCyclePending = false;
    receiptSink = nullptr;
}
static void runtimeImmediate(bool success) {
    resetLease();
    Process process;
    auto& lease = g_msiNativeForegroundLease;
    lease.helperDiagnosticCapture = std::make_unique<Capture>();
    require(lease.helperDiagnosticCapture->prepare(privateDirectory), "runtime immediate prepare");
    require(spawn(process, lease.helperDiagnosticCapture.get(), success ? L"case 2" : L"case 0"), "runtime immediate spawn");
    const auto code = completed(process);
    lease.helperGameTarget = "FIXTURE-GAME-ORIGINAL";
    lease.helperExeSha256 = "fixture-exe-sha256";
    lease.helperDllSha256 = "fixture-dll-sha256";
    const auto logStart = logHistory.size();
    ++terminalCalls;
    msiNativeForegroundSettleHelperDiagnosticLocked(code); // Exact production method.
    lease.helperExitCode = code;
    lease.cycleAccepted = code == 0;
    msiNativeForegroundReceipt("fixture-terminal", "synthetic-existing-call-facts"); // Exact ordinary receipt method.
    check(logHistory.size() == logStart + (success ? 1 : 2), "failure Settle emits ordinary lifecycle before ordinary receipt");
    json retained = nullptr;
    if (!success) {
        retained = logHistory.at(logStart);
        check(retained["domain"] == "input-host.native-helper-diagnostic" &&
            retained["value"]["helperExitCode"] == 8 && retained["value"]["helperGameTarget"] == "FIXTURE-GAME-ORIGINAL" &&
            retained["value"]["deviceResultChanged"] == false && retained["value"]["helperDiagnostic"] == lease.helperDiagnostic,
            "failure lifecycle captures original helper identity and diagnostic", retained);
    }
    check(!lease.helperDiagnosticCapture, "actual Settle consumes capture once");
    const auto preserved = lease.helperDiagnostic;
    msiNativeForegroundSettleHelperDiagnosticLocked(code);
    check(lease.helperDiagnostic == preserved, "actual Settle without capture preserves prior diagnostic");
    check(receiptSink["helperDiagnostic"] == lease.helperDiagnostic && receiptSink["helperExitCode"] == code &&
        receiptSink["cycleAccepted"] == success && receiptDomain == "msi-native-foreground-isolation",
        "ordinary actual receipt consumes settled helperDiagnostic without changing exit outcome", receiptSink);
    check(success ? !facts(receiptSink["helperDiagnostic"]) : facts(receiptSink["helperDiagnostic"]),
        success ? "exit0 receipt has no record" : "exit8 accepted diagnostic remains failed cycle");
    check(receiptSink["verificationScope"] == "config-and-cycle-command-only" &&
        receiptSink["externalInputBlocked"] == "unverified" && receiptSink["virtualTargetCreated"] == false &&
        receiptSink["modeWrite"] == false, "ordinary receipt evidence boundary preserved");
    if (!success) {
        lease.helperGameTarget = "FIXTURE-RESTORE-REPLACEMENT";
        lease.helperDiagnostic = nullptr;
        lease.helperExeSha256 = "fixture-restore-sha256";
        msiNativeForegroundReceipt("fixture-restore", "later-helper-overwrite");
        check(logHistory.at(logStart) == retained &&
            receiptSink["helperGameTarget"] == "FIXTURE-RESTORE-REPLACEMENT" && receiptSink["helperDiagnostic"].is_null(),
            "earlier failure lifecycle survives later restore replacement", retained);
    }
    resetLease();
    check(files().empty(), "actual immediate settle closes spool");
}
static void runtimeTimeout(bool terminate) {
    resetLease();
    const auto readyName = eventName(terminate ? L"terminate-ready" : L"natural-ready");
    const auto releaseName = eventName(terminate ? L"terminate-release" : L"natural-release");
    Handle ready(CreateEventW(nullptr, TRUE, FALSE, readyName.c_str()));
    Handle release(CreateEventW(nullptr, TRUE, FALSE, releaseName.c_str()));
    require(ready.value && release.value, "own timeout readiness events");
    Process process;
    auto& lease = g_msiNativeForegroundLease;
    lease.helperDiagnosticCapture = std::make_unique<Capture>();
    require(lease.helperDiagnosticCapture->prepare(privateDirectory), "timeout capture prepare");
    require(spawn(process, lease.helperDiagnosticCapture.get(), L"hold \"" + readyName + L"\" \"" + releaseName + L"\""), "private hold child");
    require(WaitForSingleObject(ready.value, 10000) == WAIT_OBJECT_0, "private child stdout flushed readiness");
    require(WaitForSingleObject(process.info.hProcess, 1) == WAIT_TIMEOUT, "forced initial timeout");
    const auto pid = process.info.dwProcessId;
    lease.cycleProcess = process.info.hProcess;
    process.info.hProcess = nullptr; // Actual poll owns this exact process handle.
    g_msiNativeForegroundCyclePending = true;
    const HANDLE originalProcess = lease.cycleProcess;
    const Capture* originalCapture = lease.helperDiagnosticCapture.get();
    const auto beforeCalls = terminalCalls;
    // R115: a second request cannot overwrite a real still-live operation.
    lease.helperGameTarget="FIXTURE-LIVE-ORIGINAL";lease.helperXusbTarget=false;
    lease.helperExePath="FIXTURE-LIVE-EXE";lease.helperExeSha256="LIVE-EXE-HASH";
    lease.helperDllPath="FIXTURE-LIVE-DLL";lease.helperDllSha256="LIVE-DLL-HASH";
    const auto createBefore=fixtureCycleCreateCalls,hashBefore=fixtureCycleHashCalls;
    HidHidePhysicalIdentity replacement;replacement.deviceInstanceId="FIXTURE-NEW-TARGET";
    const int reentered=msiNativeForegroundCycleLocked(true,&replacement,true);
    check(reentered==1&&lease.helperGameTarget=="FIXTURE-LIVE-ORIGINAL"&&!lease.helperXusbTarget&&
        lease.helperExePath=="FIXTURE-LIVE-EXE"&&lease.helperExeSha256=="LIVE-EXE-HASH"&&
        lease.helperDllPath=="FIXTURE-LIVE-DLL"&&lease.helperDllSha256=="LIVE-DLL-HASH"&&
        lease.cycleProcess==originalProcess&&lease.helperDiagnosticCapture.get()==originalCapture&&
        fixtureCycleCreateCalls==createBefore&&fixtureCycleHashCalls==hashBefore,
        "R115 second request preserves same real live helper identity and performs no new launch/hash");
    for (int i = 0; i < 3; ++i) {
        check(msiNativeForegroundPollCycleLocked() == -1 && lease.cycleProcess == originalProcess &&
            lease.helperDiagnosticCapture.get() == originalCapture && GetProcessId(lease.cycleProcess) == pid &&
            g_msiNativeForegroundCyclePending && lease.helperDiagnostic.is_null(), "actual timeout poll retains same PID/handle/capture, no terminal facts");
    }
    msiNativeForegroundReceipt("fixture-pending", "private-timeout");
    check(receiptSink["helperPending"] == true && receiptSink["helperDiagnostic"].is_null() &&
        files().size() == 1 && terminalCalls == beforeCalls, "pending ordinary receipt does not consume live capture", receiptSink);
    if (terminate) require(TerminateProcess(lease.cycleProcess, 8) != FALSE, "terminate only own held child");
    else require(SetEvent(release.value) != FALSE, "release only own held child");
    require(WaitForSingleObject(lease.cycleProcess, 10000) == WAIT_OBJECT_0, "held child actually terminal before read");
    ++terminalCalls;
    const int settled = msiNativeForegroundPollCycleLocked(); // Exact production wait/exit/Settle path.
    msiNativeForegroundReceipt("fixture-terminal", "private-timeout-settled");
    check(settled == 2 && !lease.cycleProcess && !lease.helperDiagnosticCapture && !g_msiNativeForegroundCyclePending &&
        lease.helperExitCode == 8 && !lease.cycleAccepted && facts(lease.helperDiagnostic),
        terminate ? "actual poll settles terminated same child and then reads" : "actual poll settles naturally exited same child and then reads", receiptSink);
    check(receiptSink["helperDiagnostic"] == lease.helperDiagnostic && receiptSink["helperPending"] == false &&
        receiptSink["cycleAccepted"] == false && receiptSink["helperFailure"] == "helper-terminal-nonzero-exit",
        "actual poll receipt preserves exit8 failure and self-reported scope", receiptSink);
    check(files().empty(), "actual poll reset deletes spool");
    lifecycle.push_back({{"mode", terminate ? "timeout-terminate" : "timeout-natural"}, {"samePid", pid},
        {"pendingPolls", 3}, {"receipt", receiptSink}});
    resetLease();
}
static void optionalCaptureAndSinkFailure() {
    const DWORD baseline = handleCount();
    {
        auto rootOnly = fixtureActualOptionalCapture(false);
        check(!rootOnly && files().empty() && handleCount() == baseline, "actual root-only branch creates no stdout spool or handles");
        Process process;
        require(spawn(process, rootOnly.get(), L"fallback-success"), "root-only basic private launch");
        check(completed(process) == 0, "root-only basic launch unchanged");
    }
    {
        auto game = fixtureActualOptionalCapture(true);
        check(game && files().size() == 1, "actual gameSource branch creates optional spool in own TMP directory");
    }
    check(files().empty() && handleCount() == baseline, "optional game capture cleanup");
    resetLease();
    {
        Process process;
        auto& lease = g_msiNativeForegroundLease;
        lease.helperDiagnosticCapture = std::make_unique<Capture>();
        require(lease.helperDiagnosticCapture->prepare(privateDirectory), "sink failure prepare");
        require(spawn(process, lease.helperDiagnosticCapture.get(), L"case 0"), "sink failure private launch");
        const auto code = completed(process);
        throwLogSink = true;
        ++terminalCalls;
        msiNativeForegroundSettleHelperDiagnosticLocked(code);
        throwLogSink = false;
        check(!lease.helperDiagnosticCapture && facts(lease.helperDiagnostic) && code == 8 && files().empty(),
            "actual noexcept Settle tolerates lifecycle sink exception and releases capture");
    }
    resetLease();
    check(files().empty() && handleCount() == baseline, "log sink failure leaves no handles or spool");
}
static void actualProducerReplay() {
    for (size_t i = 0; i < producerReplays.size(); ++i) {
        resetLease();
        const auto& replay = producerReplays[i];
        auto& lease = g_msiNativeForegroundLease;
        lease.helperDiagnosticCapture = std::make_unique<Capture>();
        require(lease.helperDiagnosticCapture->prepare(privateDirectory), "producer replay actual capture prepare");
        Process process;
        require(spawn(process, lease.helperDiagnosticCapture.get(), L"producer " + std::to_wstring(i)), "C++ private child replays actual C# Facts");
        const auto code = completed(process);
        ++terminalCalls;
        msiNativeForegroundSettleHelperDiagnosticLocked(code);
        lease.helperExitCode = code;
        lease.cycleAccepted = code == 0;
        msiNativeForegroundReceipt("fixture-producer-replay", replay.id.c_str());
        const auto expected = json::parse(replay.bytes.begin() + prefix.size(), replay.bytes.end());
        const auto& diagnostic = lease.helperDiagnostic;
        if (!replay.refresh) {
            check(code == 8 && facts(diagnostic) && diagnostic["reportedFacts"] == expected &&
                diagnostic["stdoutBytes"] == replay.bytes.size() && diagnostic["hardwareSuccessProven"] == false,
                replay.id + ": real C# field replay accepted byte-bounded and lossless", diagnostic);
            check(expected["LocateResult"] == 4294967295ULL && expected["CompatibleSize"].is_null() &&
                expected["CompatibleData"].is_null() && expected["Xusb"].is_boolean() && expected["ParentChain"].is_array(),
                replay.id + ": actual PascalCase uint32/bool/null/array fields retained");
        } else {
            check(code == 6 && facts(diagnostic) && diagnostic["reportedFacts"] == expected &&
                diagnostic["stdoutBytes"] == replay.bytes.size() && diagnostic["hardwareSuccessProven"] == false,
                replay.id + ": actual C# Result6 diagnostic replay accepted unchanged", diagnostic);
            check(expected["Kind"] == "ymcc.gameRefresh.failure" && expected["Result"] == 6 &&
                expected["Xusb"].is_boolean() && expected["DisableCallEntered"].is_boolean() &&
                expected["EnableCallEntered"].is_boolean() && expected["ExceptionHResult"].is_number_integer(),
                replay.id + ": real refresh nullable return codes and exception fields retained");
            if (expected.contains("CompensationChecked")) {
                check(diagnostic["reportedFacts"]["CompensationChecked"] == expected["CompensationChecked"] &&
                    diagnostic["reportedFacts"]["CompensationAttempted"] == expected["CompensationAttempted"] &&
                    diagnostic["reportedFacts"]["CompensationEnableResult"] == expected["CompensationEnableResult"] &&
                    diagnostic["reportedFacts"]["CompensationExceptionType"] == expected["CompensationExceptionType"] &&
                    diagnostic["reportedFacts"]["CompensationExceptionHResult"] == expected["CompensationExceptionHResult"],
                    replay.id + ": original and compensation facts remain separate and lossless");
                // A current-locate/authority failure precedes Disable. Keep
                // its real stage and prove that no later write/compensation ran.
                const bool beforeDisable = expected["DisableCallEntered"] == false;
                const auto stage = expected["Stage"].get<std::string>();
                const bool stageConsistent = beforeDisable
                    ? ((stage == "admission" || stage == "prepare" || stage == "prepare-locate" || stage == "revalidate" ||
                        stage == "current-locate" || stage == "current-authority") && expected["EnableCallEntered"] == false &&
                        expected["CompensationChecked"] == false && expected["CompensationAttempted"] == false)
                    : expected["Stage"] == (expected["EnableCallEntered"] == true ? "enable" : "disable");
                check(code == 6 && !lease.cycleAccepted && diagnostic["hardwareSuccessProven"] == false && stageConsistent,
                    replay.id + ": compensation never promotes failed operation or original stage to success");
            }
        }
        check(receiptSink["helperDiagnostic"] == diagnostic && receiptSink["cycleAccepted"] == false &&
            !lease.helperDiagnosticCapture && files().empty(), replay.id + ": actual receipt retains producer facts without success authority");
        producerPlayback.push_back({{"id", replay.id}, {"wireBytes", replay.bytes.size()}, {"actual", diagnostic},
            {"recordEqual", diagnostic["reportedFacts"] == expected}, {"exitCode", code}, {"refresh", replay.refresh},
            {"scope", "actual no-device C# deserialized Facts, reserialized and replayed by C++ fixture child"}});
        resetLease();
    }
}
static void actualCycleAttribution() {
    const DWORD baseline = handleCount();
    for (const std::string mode : {"early-policy", "empty-location", "missing-helper", "invalid-root", "invalid-location", "invalid-target", "launch-failure"}) {
        resetLease();
        fixtureCycleImage = executable;
        fixtureLaunchFailure = false;
        fixtureCycleCreateCalls = fixtureCycleHashCalls = 0;
        auto& lease = g_msiNativeForegroundLease;
        // Seed facts through a real exit8 child and the actual production Settle.
        {
            Process previous;
            lease.helperDiagnosticCapture = std::make_unique<Capture>();
            require(lease.helperDiagnosticCapture->prepare(privateDirectory), "attribution previous capture prepare");
            require(spawn(previous, lease.helperDiagnosticCapture.get(), L"case 0"), "attribution previous private child");
            const auto code = completed(previous);
            ++terminalCalls;
            msiNativeForegroundSettleHelperDiagnosticLocked(code);
            check(facts(lease.helperDiagnostic), mode + ": previous exit8 facts really accepted");
        }
        const auto historicRecord = logHistory.at(logHistory.size() - 1);
        lease.helperExitCode=8;lease.helperError=1234;lease.helperFailure="OLD-HELPER-FAILURE";
        lease.root = "FIXTURE-ROOT";
        lease.locations = {L"FIXTURE-LOCATION"};
        HidHidePhysicalIdentity target;
        target.deviceInstanceId = "FIXTURE-NEW-GAME";
        const HidHidePhysicalIdentity* selected = &target;
        if (mode == "early-policy") { selected = nullptr; lease.profile = ymcc::nativeForeground::Profile::Rog; }
        if (mode == "empty-location") lease.locations.clear();
        if (mode == "missing-helper") fixtureCycleImage = privateDirectory + L"\\never-created-helper.exe";
        if (mode == "invalid-root") lease.root = "FIXTURE-INVALID-\"ROOT";
        if (mode == "invalid-location") lease.locations = {L"FIXTURE-INVALID-\"LOCATION"};
        if (mode == "invalid-target") target.deviceInstanceId = "FIXTURE-INVALID-\"TARGET";
        if (mode == "launch-failure") fixtureLaunchFailure = true;
        const auto processCountBefore = children;
        const int result = msiNativeForegroundCycleLocked(false, selected, false); // Entire exact production Cycle.
        msiNativeForegroundReceipt("fixture-next-attempt", mode.c_str());
        check(result == 2 && lease.helperDiagnostic.is_null() && receiptSink["helperDiagnostic"].is_null(),
            mode + ": actual next Cycle failure cannot borrow previous diagnosis", receiptSink);
        check(lease.helperExitCode==STILL_ACTIVE&&lease.helperFailure!="OLD-HELPER-FAILURE"&&
            (mode=="missing-helper"||mode=="launch-failure"||lease.helperError==0),
            mode+": R115 new-attempt observations never carry prior exit/error/failure",receiptSink);
        check(!lease.helperDiagnosticCapture && !lease.cycleProcess && files().empty(), mode + ": failure retains no new capture/process/spool");
        check(children == processCountBefore && fixtureCycleCreateCalls == (mode == "launch-failure" ? 1U : 0U),
            mode + ": lazy boundaries did not launch a child");
        check(historicRecord["domain"] == "input-host.native-helper-diagnostic" && facts(historicRecord["value"]["helperDiagnostic"]),
            mode + ": prior ordinary lifecycle remains available");
        if (mode == "early-policy" || mode == "empty-location" || mode == "missing-helper")
            check(fixtureCycleHashCalls == 0, mode + ": early rejection never hashes anything");
    }
    // R115: unresolved separately owned helper state is a wait, not a new attempt.
    resetLease();
    {
        auto& lease = g_msiNativeForegroundLease;
        lease.helperDiagnostic = {{"state", "record-accepted"}, {"reportedFacts", validRecord()}};
        lease.helperDiagnosticCapture = std::make_unique<Capture>();
        require(lease.helperDiagnosticCapture->prepare(privateDirectory), "pending ownership capture prepare");
        Handle sentinel(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        require(sentinel.value != nullptr, "own pending ownership sentinel");
        lease.cycleProcess = sentinel.value; // Early-return-only sentinel; never passed to process APIs.
        const Capture* sameCapture = lease.helperDiagnosticCapture.get();
        const auto pendingFacts=lease.helperDiagnostic;
        check(msiNativeForegroundCycleLocked(false) == 1 && lease.helperDiagnostic==pendingFacts &&
            lease.helperDiagnosticCapture.get() == sameCapture && lease.cycleProcess == sentinel.value&&g_msiNativeForegroundCyclePending,
            "R115 unresolved process ownership is rearmed for poll without resetting its facts");
        lease.cycleProcess = nullptr;
    }
    resetLease();
    fixtureLaunchFailure = false;
    check(files().empty() && handleCount() == baseline, "actual Cycle attribution cases release handles and spools",
        {{"baseline", baseline}, {"after", handleCount()}, {"files", files()}});
}
static void resourceRounds(unsigned rounds) {
    const DWORD baseline = handleCount();
    json counts = json::array();
    const auto all = scenarios();
    const std::vector<size_t> selection{0, 1, 2, all.size() - 2};
    for (unsigned i = 0; i < rounds; ++i) {
        {
            Capture capture;
            require(capture.prepare(privateDirectory), "round prepare");
            Process process;
            require(spawn(process, &capture, L"case " + std::to_wstring(selection[i % selection.size()])), "round private child");
            const auto code = completed(process);
            (void)terminal(capture, process, code);
        }
        const DWORD after = handleCount();
        counts.push_back(after);
        check(after == baseline && files().empty(), "resource round " + std::to_string(i) + ": no handles or temporary files leaked",
            {{"baselineHandles", baseline}, {"afterHandles", after}, {"remainingFiles", files()}});
    }
    lifecycle.push_back({{"mode", "repeated-resources"}, {"rounds", rounds}, {"baselineHandles", baseline},
        {"finalHandles", handleCount()}, {"eachRoundHandles", counts}, {"remainingFiles", files()}});
}
int wmain(int argc, wchar_t** argv) {
    if (argc > 1 && std::wstring(argv[1]) == L"--child") return child(argc, argv);
    if (argc > 1 && std::wstring(argv[1]) == L"--msi-native-cycle") { writeAll(GetStdHandle(STD_OUTPUT_HANDLE), wire(validRecord())); return 8; }
    if (argc != 5 || std::wstring(argv[1]) != L"--parent") return 90;
    const std::wstring output(argv[2]);
    const std::wstring variant(argv[3]);
    const unsigned rounds = static_cast<unsigned>(_wtoi(argv[4]));
    std::wstring path(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(size);
    executable = path;
    privateDirectory = output + L"\\private-" + variant + L"-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    json fatal = nullptr;
    bool created = false;
    bool removed = false;
    try {
        require(size && CreateDirectoryW(privateDirectory.c_str(), nullptr), "create unique private directory (must not preexist)");
        created = true;
        require(SetEnvironmentVariableW(L"TMP", privateDirectory.c_str()) && SetEnvironmentVariableW(L"TEMP", privateDirectory.c_str()), "process-local own temporary directory");
        ordinaryCases();
        fallbackAndFailure();
        inheritance(false);
        inheritance(true);
        runtimeImmediate(false);
        runtimeImmediate(true);
        runtimeTimeout(true);
        runtimeTimeout(false);
        optionalCaptureAndSinkFailure();
        actualProducerReplay();
        actualCycleAttribution();
        resourceRounds(rounds);
        check(readsWhileAlive == 0, "all terminal reads follow proven child termination");
        check(unadmittedTrackedWriterCalls == 0, "transport fixture never enters tracked GPD writer side effects");
        check(files().empty(), "own final directory empty");
        // No file deletion or recursive deletion; remove only our newly-created empty directory.
        removed = RemoveDirectoryW(privateDirectory.c_str()) != FALSE;
        check(removed, "remove only own empty private directory");
    } catch (const std::exception& error) {
        fatal = error.what();
        ++failures;
        resetLease();
        if (created && files().empty()) removed = RemoveDirectoryW(privateDirectory.c_str()) != FALSE;
    }
    const json report = {{"schema", 1}, {"variant", W2U(variant)}, {"success", failures == 0},
        {"assertions", assertions}, {"failures", failures}, {"fatal", fatal}, {"privateChildProcesses", children},
        {"terminalCalls", terminalCalls}, {"readsWhileAlive", readsWhileAlive}, {"cases", cases}, {"checks", checks},
        {"lifecycle", lifecycle}, {"producerReplay", producerPlayback}, {"privateDirectory", W2U(privateDirectory)}, {"privateDirectoryRemoved", removed},
        {"boundary", {{"realProductionHeader", true}, {"realWindowsPrivateChildren", true},
            {"runtimeMethods", "exact extracted lease/Settle/poll/ordinary receipt"},
            {"runtimeLogSink", "fixture memory only"},
            {"mutationPolicy", "inert-true-transport-leaf; production policy not executed"},
            {"productionMutationPolicyTested", false}, {"productOrHostStarted", false}, {"deviceAccess", false},
            {"steamAccess", false}, {"driverAccess", false}, {"recursiveDelete", false}}}};
    const std::string text = report.dump(2) + "\n";
    if (!writeAll(GetStdHandle(STD_OUTPUT_HANDLE), text)) return 96;
    return failures == 0 ? 0 : 1;
}
