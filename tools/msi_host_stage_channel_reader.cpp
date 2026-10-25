#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include "msi_host_stage_evidence.h"

#if defined(YMCC_MSI_STAGE_CODEC_ONLY) || defined(YMCC_FAULT_INJECT) || defined(YMCC_MSI_FOREGROUND_ISOLATION_TEST)
#error This standalone reader must compile the actual Windows mapping adapter.
#endif

namespace stage = ymcc::msi::hostStageEvidence;
using Json = stage::Json;

// The actual header above is compiled unchanged, without Windows API overrides.
// A separate OS audit asks for the same read access and never writes a byte.
static Json readOnlyAudit(const stage::Expected& expected) {
    const auto name = std::wstring(L"Local\\YeManCC-MSI-HostStage-v1-") +
        std::to_wstring(expected.parentPid) + L"-" + std::to_wstring(expected.hostPid) + L"-" +
        std::wstring(expected.hostInstance.begin(), expected.hostInstance.end());
    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
    if (!mapping) return {{"opened", false}, {"win32Error", GetLastError()}};
    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, stage::capacity);
    const DWORD readViewError = view ? ERROR_SUCCESS : GetLastError();
    MEMORY_BASIC_INFORMATION info{};
    const bool queried = view && VirtualQuery(view, &info, sizeof(info)) == sizeof(info);
    SetLastError(ERROR_SUCCESS);
    void* writable = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, stage::capacity);
    const DWORD writableError = writable ? ERROR_SUCCESS : GetLastError();
    const bool writeViewDenied = writable == nullptr && writableError == ERROR_ACCESS_DENIED;
    if (writable) UnmapViewOfFile(writable);
    if (view) UnmapViewOfFile(view);
    CloseHandle(mapping);
    return {{"opened", true}, {"requestedAccess", FILE_MAP_READ}, {"handleInheritable", false},
        {"readViewOpened", view != nullptr}, {"readViewError", readViewError},
        {"viewProtectionQueried", queried}, {"viewProtection", info.Protect},
        {"viewIsReadOnly", queried && info.Protect == PAGE_READONLY},
        {"writeViewDenied", writeViewDenied}, {"writeViewError", writableError},
        {"handlesClosedBeforeReturn", true},
        {"scope", "Independent OS read-access audit; actual header read() is compiled without API overrides."}};
}

// Redirected stdin is an anonymous pipe. Polling is bounded by the whole-process
// 24-second deadline; EOF or an explicit quit ends it immediately.
static bool boundedLine(std::string& line, ULONGLONG started) {
    line.clear();
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    while (GetTickCount64() - started < 24000) {
        DWORD available = 0;
        if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) return false;
            throw std::runtime_error("Fixture stdin must be a redirected pipe.");
        }
        if (!available) { Sleep(2); continue; }
        char c = 0; DWORD got = 0;
        if (!ReadFile(input, &c, 1, &got, nullptr) || !got) return false;
        if (c == '\n') return true;
        if (c != '\r') line.push_back(c);
        if (line.size() > 8192) throw std::runtime_error("Oversized fixture command.");
    }
    throw std::runtime_error("Fixture lifetime exceeded 24 seconds.");
}

static stage::Expected parseExpected(const Json& value) {
    stage::Expected result;
    result.parentPid = value.at("parentPid").get<std::uint64_t>();
    result.hostPid = value.at("hostPid").get<std::uint64_t>();
    result.hostInstance = value.at("hostInstance").get<std::string>();
    result.requestId = value.at("requestId").get<std::string>();
    result.runId = value.at("runId").get<std::string>();
    result.command = value.at("command").get<std::string>();
    result.sequence = value.at("sequence").get<std::uint64_t>();
    result.epoch = value.at("epoch").get<std::uint64_t>();
    if (!stage::guidN(result.hostInstance) || !result.parentPid || !result.hostPid ||
        result.requestId.rfind("stage-fixture-", 0) != 0 || result.runId.rfind("stage-run-", 0) != 0 ||
        result.command.rfind("fixture_", 0) != 0)
        throw std::runtime_error("Fixture-only expected identity required.");
    return result;
}

static void emit(const Json& value) { std::cout << value.dump() << std::endl; }

int main() {
    const ULONGLONG started = GetTickCount64();
    try {
        emit({{"ok", true}, {"op", "ready"}, {"processId", GetCurrentProcessId()},
            {"actualWindowsHeaderReadCompiled", true}, {"lifetimeLimitMs", 24000}});
        std::string line;
        unsigned reads = 0;
        while (boundedLine(line, started)) {
            const auto command = Json::parse(line);
            const auto op = command.at("op").get<std::string>();
            if (op == "quit") {
                emit({{"ok", true}, {"op", op}, {"actualHeaderReadCalls", reads},
                    {"lifetimeMs", GetTickCount64() - started}});
                return 0;
            }
            if (op != "read") throw std::runtime_error("Unsupported fixture operation.");
            const auto expected = parseExpected(command.at("expected"));
            // REAL OpenFileMappingW and MapViewOfFile via the actual native header.
            auto row = stage::read(expected);
            ++reads;
            Json response = {{"ok", true}, {"op", op}, {"case", command.at("case")},
                {"row", row}, {"readerPid", GetCurrentProcessId()}, {"actualHeaderReadCalls", reads}};
            if (command.value("auditReadOnly", false)) response["readOnlyAudit"] = readOnlyAudit(expected);
            // read() and the optional audit have closed all their mapping handles.
            emit(response);
        }
        return 0;
    } catch (const std::exception& ex) {
        emit({{"ok", false}, {"error", ex.what()}, {"lifetimeMs", GetTickCount64() - started}});
        return 3;
    }
}
