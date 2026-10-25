#pragma once
// MSI shield diagnostics only. Discovery never grants stop/disable authority.
// Included after json/W2U; all SCM handles request query/enumeration rights only.
#include "msi_foundation_process_observation.h"
static bool msiShieldServiceCandidate(const std::wstring& name, const std::wstring& display) {
    const auto prefix = [](const std::wstring& s, const wchar_t* p) {
        const size_t length = wcslen(p);
        return s.size() >= length && _wcsnicmp(s.c_str(), p, length) == 0;
    };
    return prefix(name, L"MSI ") || prefix(name, L"MSI_") || prefix(name, L"MSI-") ||
        prefix(display, L"MSI ") || prefix(display, L"Micro-Star ");
}
static json msiShieldObserveService(SC_HANDLE scm, const std::wstring& name, bool observeFoundationProcesses = false, const char* processPhase = "unspecified") {
    const bool processEvidence = observeFoundationProcesses && _wcsicmp(name.c_str(), L"MSI Foundation Service") == 0;
    json result = {{"service", W2U(name)}, {"readOnly", true}, {"mutationPolicy", "preserve-unverified-dependency"},
        {"controllerOnlyRoleVerified", false}, {"existsKnown", false}, {"configRead", false}, {"stateRead", false}};
    constexpr DWORD access = SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS;
    result["requestedAccess"] = access;
    SC_HANDLE service = OpenServiceW(scm, name.c_str(), access);
    if (!service) {
        const DWORD error = GetLastError();
        result["win32Error"] = error;
        result["existsKnown"] = error == ERROR_SERVICE_DOES_NOT_EXIST;
        if (error == ERROR_SERVICE_DOES_NOT_EXIST) result["exists"] = false;
        if (processEvidence) {
            result["foundationProcessObservation"] = msiFoundationObserveProcesses(nullptr, {}, false, error);
            result["foundationProcessObservation"]["observationPhase"] = processPhase;
        }
        return result;
    }
    result["existsKnown"] = true; result["exists"] = true;
    SERVICE_STATUS_PROCESS status{}; DWORD bytes = 0;
    if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytes)) {
        result["stateRead"] = true; result["serviceState"] = status.dwCurrentState;
        result["processId"] = status.dwProcessId; result["serviceType"] = status.dwServiceType;
    } else result["stateWin32Error"] = GetLastError();
    bytes = 0;
    const BOOL initial = QueryServiceConfigW(service, nullptr, 0, &bytes);
    const DWORD initialError = initial ? ERROR_SUCCESS : GetLastError();
    if (!initial && initialError == ERROR_INSUFFICIENT_BUFFER && bytes >= sizeof(QUERY_SERVICE_CONFIGW) && bytes <= 1024 * 1024) {
        std::vector<BYTE> buffer(bytes);
        auto* config = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
        if (QueryServiceConfigW(service, config, static_cast<DWORD>(buffer.size()), &bytes)) {
            // The SCM owns the string layout in this successful query buffer.
            result["configRead"] = true; result["startType"] = config->dwStartType;
            result["binaryPath"] = config->lpBinaryPathName ? W2U(config->lpBinaryPathName) : "";
            result["displayName"] = config->lpDisplayName ? W2U(config->lpDisplayName) : "";
            json dependencies = json::array();
            if (config->lpDependencies) {
                const wchar_t* cursor = config->lpDependencies;
                const auto end = reinterpret_cast<uintptr_t>(buffer.data()) + buffer.size();
                for (unsigned n = 0; n < 64 && reinterpret_cast<uintptr_t>(cursor) + sizeof(wchar_t) <= end && *cursor; ++n) {
                    const wchar_t* next = cursor;
                    while (reinterpret_cast<uintptr_t>(next) + sizeof(wchar_t) <= end && *next) ++next;
                    if (reinterpret_cast<uintptr_t>(next) + sizeof(wchar_t) > end) break;
                    dependencies.push_back(W2U(std::wstring(cursor, next)));
                    cursor = next + 1;
                }
            }
            result["dependencies"] = std::move(dependencies);
        } else result["configWin32Error"] = GetLastError();
    } else result["configWin32Error"] = initialError == ERROR_INSUFFICIENT_BUFFER ? ERROR_INVALID_DATA : initialError;
    result["readbackVerified"] = result.value("configRead", false) && result.value("stateRead", false);
    if (processEvidence) {
        result["foundationProcessObservation"] = msiFoundationObserveProcesses(
            service, status, result.value("stateRead", false), result.value("stateWin32Error", DWORD{ERROR_SUCCESS}));
        result["foundationProcessObservation"]["observationPhase"] = processPhase;
    }
    CloseServiceHandle(service);
    return result;
}
static json msiShieldReadProtectedService() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        const DWORD error = GetLastError();
        json process = msiFoundationObserveProcesses(nullptr, {}, false, error);
        process["observationPhase"] = "after-existing-process-isolation";
        return {{"service", "MSI Foundation Service"}, {"readOnly", true}, {"existsKnown", false},
            {"readbackVerified", false}, {"win32Error", error}, {"controllerOnlyRoleVerified", false},
            {"foundationProcessObservation", std::move(process)}};
    }
    json result = msiShieldObserveService(scm, L"MSI Foundation Service", true, "after-existing-process-isolation");
    result["observedAtMonoMs"] = GetTickCount64();
    CloseServiceHandle(scm);
    return result;
}
static json msiShieldReadServiceInventory() {
    constexpr DWORD kPageBytes = 256 * 1024;
    constexpr unsigned kMaxPages = 8, kMaxCandidates = 32;
    json result = {{"readOnly", true}, {"discoveryAuthorizesMutation", false}, {"controllerOnlyServiceIdentified", false},
        {"complete", false}, {"truncated", false}, {"observedAtMonoMs", GetTickCount64()},
        {"processObservationPhase", "before-existing-process-isolation"}, {"services", json::array()}};
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm) { result["win32Error"] = GetLastError(); return result; }
    std::vector<BYTE> buffer(kPageBytes); DWORD resume = 0;
    std::vector<std::wstring> seen;
    for (unsigned page = 0; page < kMaxPages; ++page) {
        DWORD needed = 0, count = 0;
        const BOOL ok = EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
            buffer.data(), static_cast<DWORD>(buffer.size()), &needed, &count, &resume, nullptr);
        const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        result["pagesScanned"] = page + 1;
        if (!ok && error != ERROR_MORE_DATA) { result["win32Error"] = error; break; }
        if (count > buffer.size() / sizeof(ENUM_SERVICE_STATUS_PROCESSW)) { result["win32Error"] = ERROR_INVALID_DATA; break; }
        const auto* entries = reinterpret_cast<const ENUM_SERVICE_STATUS_PROCESSW*>(buffer.data());
        for (DWORD i = 0; i < count; ++i) {
            const auto& entry = entries[i];
            if (!entry.lpServiceName) continue;
            const std::wstring name = entry.lpServiceName;
            const std::wstring display = entry.lpDisplayName ? entry.lpDisplayName : L"";
            if (!msiShieldServiceCandidate(name, display) || std::find(seen.begin(), seen.end(), name) != seen.end()) continue;
            if (seen.size() >= kMaxCandidates) { result["truncated"] = true; break; }
            seen.push_back(name);
            // Only the exact Foundation SCM key receives the before snapshot. The
            // existing seen list guarantees one observation per inventory invocation.
            const bool foundation = _wcsicmp(name.c_str(), L"MSI Foundation Service") == 0;
            json facts = msiShieldObserveService(scm, name, foundation, "before-existing-process-isolation");
            facts["enumeratedDisplayName"] = W2U(display);
            facts["candidateOnly"] = true;
            result["services"].push_back(std::move(facts));
        }
        if (result.value("truncated", false)) break;
        if (ok) { result["complete"] = true; result["win32Error"] = ERROR_SUCCESS; break; }
        if (page + 1 == kMaxPages || (!count && !resume)) { result["truncated"] = true; result["win32Error"] = ERROR_MORE_DATA; break; }
    }
    result["candidateCount"] = result["services"].size();
    CloseServiceHandle(scm);
    return result;
}
