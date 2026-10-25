#pragma once
#include <tlhelp32.h>
#include <memory>
// Only the existing explicit MSI shield worker requests this diagnostic.
// Parent PID equality is observational: it grants no stop or input-dependency authority.
// Toolhelp rows have no creation time. PID reuse between a first row and OpenProcess
// is unknowable if the replacement has the same parent PID; only the opened handle
// and a later snapshot are compared. Neither SCM readbacks nor these fields prove no ABA.
static constexpr DWORD kMsiFoundationProcessAccess = PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE;
static constexpr unsigned kMsiFoundationChildLimit = 16, kMsiFoundationEntryLimit = 4096;
static constexpr ULONGLONG kMsiFoundationSoftBudgetMs = 100;
struct MsiFoundationReadHandle {
    HANDLE value;
    explicit MsiFoundationReadHandle(HANDLE h) : value(h) {}
    ~MsiFoundationReadHandle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    MsiFoundationReadHandle(const MsiFoundationReadHandle&) = delete;
    MsiFoundationReadHandle& operator=(const MsiFoundationReadHandle&) = delete;
};
static bool msiFoundationHandleAlive(HANDLE h, json& row) {
    const DWORD wait = WaitForSingleObject(h, 0);
    row["handleAliveKnown"] = wait == WAIT_TIMEOUT || wait == WAIT_OBJECT_0;
    row["originalHandleAlive"] = wait == WAIT_TIMEOUT;
    if (wait == WAIT_FAILED) row["waitWin32Error"] = GetLastError();
    else if (wait != WAIT_TIMEOUT && wait != WAIT_OBJECT_0) row["waitWin32Error"] = ERROR_INVALID_DATA;
    return wait == WAIT_TIMEOUT;
}
static bool msiFoundationReadIdentity(HANDLE h, json& row) {
    FILETIME created{}, exited{}, kernel{}, user{};
    row["creationTimeRead"] = false; row["imagePathRead"] = false;
    if (GetProcessTimes(h, &created, &exited, &kernel, &user)) {
        const uint64_t time = (static_cast<uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        row["creationTime100ns"] = time; row["creationTimeRead"] = time != 0;
        if (!time) row["creationTimeWin32Error"] = ERROR_INVALID_DATA;
    } else row["creationTimeWin32Error"] = GetLastError();
    std::vector<wchar_t> path(32768); DWORD size = static_cast<DWORD>(path.size());
    const BOOL pathRead = QueryFullProcessImageNameW(h, 0, path.data(), &size);
    if (pathRead && size > 0 && size < path.size()) {
        row["imagePath"] = W2U(std::wstring(path.data(), size)); row["imagePathRead"] = true;
    } else row["imagePathWin32Error"] = pathRead ? ERROR_INVALID_DATA : GetLastError();
    const bool alive = msiFoundationHandleAlive(h, row);
    row["identityRead"] = row.value("creationTimeRead", false) && row.value("imagePathRead", false) && alive;
    return row.value("identityRead", false);
}
static bool msiFoundationServiceRecheck(SC_HANDLE service, const SERVICE_STATUS_PROCESS& anchor, json& row) {
    SERVICE_STATUS_PROCESS current{}; DWORD bytes = 0;
    row["stateRead"] = false; row["matchesAnchor"] = false;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&current), sizeof(current), &bytes)) {
        row["win32Error"] = GetLastError(); return false;
    }
    row["stateRead"] = true; row["servicePid"] = current.dwProcessId;
    row["serviceState"] = current.dwCurrentState; row["serviceType"] = current.dwServiceType;
    const bool same = current.dwProcessId == anchor.dwProcessId && current.dwCurrentState == anchor.dwCurrentState &&
        current.dwServiceType == anchor.dwServiceType;
    row["matchesAnchor"] = same;
    return same;
}
template<class Visitor>
static bool msiFoundationWalkSnapshot(json& result, const char* phase, ULONGLONG begin, Visitor visit) {
    json scan = {{"complete", false}, {"entriesScanned", 0}, {"truncated", false}};
    const auto finish = [&](bool complete) { scan["complete"] = complete; result[phase] = scan; return complete; };
    if (GetTickCount64() - begin >= kMsiFoundationSoftBudgetMs) {
        scan["reason"] = "soft-budget-exhausted"; scan["truncated"] = true; return finish(false);
    }
    MsiFoundationReadHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.value == INVALID_HANDLE_VALUE || !snapshot.value) {
        scan["win32Error"] = GetLastError(); scan["reason"] = "snapshot-query-failed"; return finish(false);
    }
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    for (unsigned n = 0; n < kMsiFoundationEntryLimit; ++n) {
        if (GetTickCount64() - begin >= kMsiFoundationSoftBudgetMs) {
            scan["reason"] = "soft-budget-exhausted"; scan["truncated"] = true; return finish(false);
        }
        const BOOL ok = n == 0 ? Process32FirstW(snapshot.value, &entry) : Process32NextW(snapshot.value, &entry);
        if (!ok) {
            const DWORD error = GetLastError(); scan["win32Error"] = error;
            scan["reason"] = error == ERROR_NO_MORE_FILES ? "enumeration-ended" : "enumeration-query-failed";
            return finish(error == ERROR_NO_MORE_FILES);
        }
        scan["entriesScanned"] = n + 1;
        if (!visit(entry)) { scan["reason"] = "child-limit"; scan["truncated"] = true; return finish(false); }
    }
    scan["reason"] = "entry-limit"; scan["truncated"] = true; return finish(false);
}
static json msiFoundationObserveProcesses(SC_HANDLE service, const SERVICE_STATUS_PROCESS& anchor, bool stateRead,
                                         DWORD unavailableError = ERROR_SUCCESS) {
    const ULONGLONG begin = GetTickCount64();
    json result = {{"schemaVersion", 1}, {"readOnly", true}, {"servicePid", anchor.dwProcessId},
        {"serviceState", anchor.dwCurrentState}, {"serviceStateRead", stateRead}, {"complete", false},
        {"incomplete", true}, {"truncated", false}, {"childrenAbsenceKnown", false}, {"children", json::array()},
        {"parentPidOnlyMatch", true}, {"inputDependencyProven", false}, {"stopAuthorization", false},
        {"atomicSnapshot", false}, {"firstSnapshotProcessIdentityKnown", false}, {"noAbaProven", false},
        {"initialServiceStatusToOpenProcessIdentityKnown", false},
        {"proofScope", "opened-handle-lifetime-and-second-snapshot-parent-pid-consistency-only"},
        {"completeScope", "bounded-enumeration-and-current-held-process-observation-not-first-snapshot-generation"},
        {"requestedProcessAccess", kMsiFoundationProcessAccess},
        {"childLimit", kMsiFoundationChildLimit}, {"entryLimitPerSnapshot", kMsiFoundationEntryLimit},
        {"snapshotLimit", 2}, {"softBudgetMs", kMsiFoundationSoftBudgetMs}, {"hardApiCancellation", false},
        {"observedBeginMonoMs", begin}, {"nonCausalStatement", "static parent PID equality does not prove input dependency or stop safety"}};
    const auto finish = [&](const char* reason) {
        result["reason"] = reason; result["observedEndMonoMs"] = GetTickCount64(); return result;
    };
    if (!service || !stateRead) {
        result["serviceStatusWin32Error"] = unavailableError; return finish("service-status-unavailable");
    }
    // SCM process IDs are not usable in stopped/start-pending states.
    if (!anchor.dwProcessId || (anchor.dwCurrentState != SERVICE_RUNNING && anchor.dwCurrentState != SERVICE_PAUSED))
        return finish("service-pid-unavailable-or-state-not-stable");
    json parent = {{"pid", anchor.dwProcessId}, {"identityRead", false}, {"originalHandleAlive", false}};
    MsiFoundationReadHandle parentHandle(OpenProcess(kMsiFoundationProcessAccess, FALSE, anchor.dwProcessId));
    if (!parentHandle.value) {
        parent["openWin32Error"] = GetLastError(); result["parentProcess"] = parent; return finish("parent-open-failed");
    }
    bool parentOk = msiFoundationReadIdentity(parentHandle.value, parent);
    result["parentProcess"] = parent;
    if (!parentOk) return finish("parent-identity-unavailable");
    if (!msiFoundationServiceRecheck(service, anchor, result["serviceAfterParentOpen"]))
        return finish("service-anchor-not-stable-after-open");
    if (!msiFoundationHandleAlive(parentHandle.value, result["parentProcess"])) return finish("original-parent-handle-not-alive");
    std::vector<std::unique_ptr<MsiFoundationReadHandle>> handles;
    bool parentSeen = false;
    const bool firstComplete = msiFoundationWalkSnapshot(result, "candidateEnumeration", begin, [&](const PROCESSENTRY32W& entry) {
        if (entry.th32ProcessID == anchor.dwProcessId) {
            parentSeen = true; result["parentProcess"]["snapshotParentPid"] = entry.th32ParentProcessID;
        }
        if (entry.th32ParentProcessID != anchor.dwProcessId || entry.th32ProcessID == anchor.dwProcessId) return true;
        if (result["children"].size() >= kMsiFoundationChildLimit) return false;
        result["children"].push_back({{"pid", entry.th32ProcessID}, {"parentPid", entry.th32ParentProcessID},
            {"parentPidEqualsServicePid", true}, {"parentPidOnlyMatch", true}, {"identityRead", false},
            {"snapshotParentPidRevalidated", false}, {"firstSnapshotProcessIdentityKnown", false},
            {"heldHandleAndSecondSnapshotConsistent", false}});
        return true;
    });
    result["serviceProcessSeenInSnapshot"] = parentSeen;
    // Open candidates only after the first enumeration. Hold every original handle
    // across a second snapshot to revalidate the CURRENT opened instances. This cannot
    // identify the first snapshot generation when a PID was reused before OpenProcess.
    bool allChildrenRead = true;
    for (auto& child : result["children"]) {
        if (GetTickCount64() - begin >= kMsiFoundationSoftBudgetMs) {
            child["reason"] = "soft-budget-exhausted"; handles.emplace_back(); allChildrenRead = false; continue;
        }
        HANDLE h = OpenProcess(kMsiFoundationProcessAccess, FALSE, child["pid"].get<DWORD>());
        handles.emplace_back(h ? std::make_unique<MsiFoundationReadHandle>(h) : nullptr);
        if (!h) { child["openWin32Error"] = GetLastError(); allChildrenRead = false; continue; }
        const bool read = msiFoundationReadIdentity(h, child);
        const bool lifetime = read && child.value("creationTime100ns", uint64_t{0}) >= parent.value("creationTime100ns", uint64_t{0});
        child["parentLifetimeCompatible"] = lifetime;
        allChildrenRead = allChildrenRead && read && lifetime;
    }
    bool secondComplete = result["children"].empty();
    if (!result["children"].empty()) {
        secondComplete = msiFoundationWalkSnapshot(result, "identityEnumeration", begin, [&](const PROCESSENTRY32W& entry) {
            for (auto& child : result["children"]) {
                if (child["pid"] == entry.th32ProcessID && child["parentPid"] == entry.th32ParentProcessID)
                    child["snapshotParentPidRevalidated"] = true;
            }
            return true;
        });
    }
    const bool serviceStable = msiFoundationServiceRecheck(service, anchor, result["serviceAfterEnumeration"]);
    const bool parentAlive = msiFoundationHandleAlive(parentHandle.value, result["parentProcess"]);
    result["heldServiceHandleAndStatusReadbacksConsistent"] = serviceStable && parentAlive;
    bool allAssociated = allChildrenRead;
    for (size_t n = 0; n < handles.size(); ++n) {
        auto& child = result["children"][n];
        const bool alive = handles[n] && msiFoundationHandleAlive(handles[n]->value, child);
        child["heldHandleAndSecondSnapshotConsistent"] = serviceStable && parentAlive && secondComplete && alive &&
            child.value("identityRead", false) && child.value("parentLifetimeCompatible", false) &&
            child.value("snapshotParentPidRevalidated", false);
        allAssociated = allAssociated && child.value("heldHandleAndSecondSnapshotConsistent", false);
    }
    const bool withinBudget = GetTickCount64() - begin < kMsiFoundationSoftBudgetMs;
    result["softBudgetExhausted"] = !withinBudget;
    result["truncated"] = result["candidateEnumeration"].value("truncated", false) ||
        result.value("identityEnumeration", json::object()).value("truncated", false) || !withinBudget;
    const bool complete = firstComplete && secondComplete && parentSeen && serviceStable && parentAlive && allAssociated && withinBudget;
    result["complete"] = complete; result["incomplete"] = !complete;
    result["childrenAbsenceKnown"] = complete && result["children"].empty();
    return finish(complete ? "bounded-observation-complete" : "bounded-observation-incomplete");
}
