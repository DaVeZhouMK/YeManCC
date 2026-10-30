#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <winsvc.h>
#include <winver.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// Explicit export call only. Requires the including translation unit's json
// alias and existing Kernel32/Advapi32 linkage. No background state or logger.
// Never infer a remote session, an input producer, or controller ownership from
// a process name, a driver package, or SERVICE_RUNNING.
namespace ymcc::remoteInputEnvironmentEvidence {
inline constexpr DWORD maxProcessEntries = 8192;
inline constexpr std::size_t maxMatchingProcesses = 32;
inline constexpr ULONGLONG phaseCheckpointMs = 500;
inline constexpr DWORD maxImagePathChars = 32768;
inline constexpr DWORD maxVersionBytes = 1024 * 1024;
inline constexpr DWORD maxServiceConfigBytes = 8192;
inline constexpr std::array<const wchar_t*, 5> executableNames{{
    L"GameViewer.exe", L"GameViewerServer.exe", L"GameViewerService.exe",
    L"GameViewerHealthd.exe", L"GameViewerLauncher.exe"
}};
// Export-only GameInput context. Keep these separate from the GameViewer
// installation list: service images must never derive UU log discovery roots.
inline constexpr std::array<const wchar_t*, 2> gameInputExecutableNames{{
    L"GameInputSvc.exe", L"GameInputRedistService.exe"
}};
// Exact SCM candidate probes, not an EXE-to-service-name mapping. A missing
// candidate name does not prove no GameInput service/runtime is installed.
inline constexpr std::array<const wchar_t*, 5> serviceNames{{
    L"uuycinput", L"uuycinputmf", L"ViGEmBus",
    L"GameInputSvc", L"GameInputRedistService"
}};

struct LastErrorScope {
    DWORD saved = GetLastError();
    ~LastErrorScope() noexcept { SetLastError(saved); }
};
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE handle) noexcept : value(handle) {}
    ~Handle() noexcept { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const noexcept { return value && value != INVALID_HANDLE_VALUE; }
};
struct ServiceHandle {
    SC_HANDLE value = nullptr;
    explicit ServiceHandle(SC_HANDLE handle) noexcept : value(handle) {}
    ~ServiceHandle() noexcept { if (value) CloseServiceHandle(value); }
    ServiceHandle(const ServiceHandle&) = delete;
    ServiceHandle& operator=(const ServiceHandle&) = delete;
};
inline json skipped(const char* reason) {
    return {{"queried",false},{"known",false},{"win32Error",nullptr},{"status",reason}};
}
inline json queryResult(bool success, DWORD error) {
    return {{"queried",true},{"known",success},{"win32Error",error},
        {"status",success ? "known" : (error ? "query-failed" : "query-failed-without-error-code")}};
}
inline std::uint64_t fileTimeValue(const FILETIME& value) noexcept {
    return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
inline std::string fileTimeString(const FILETIME& value) { return std::to_string(fileTimeValue(value)); }
inline std::string asciiName(const wchar_t* value) {
    std::string out;
    for (std::size_t i=0; i<MAX_PATH && value[i]; ++i) out.push_back(static_cast<char>(value[i]));
    return out; // Used only for the fixed ASCII allowlists above.
}
inline wchar_t folded(wchar_t value) noexcept {
    return value>=L'A' && value<=L'Z' ? static_cast<wchar_t>(value+(L'a'-L'A')) : value;
}
inline bool sameExecutable(const wchar_t* value, const wchar_t* expected) noexcept {
    for (std::size_t i=0; i<MAX_PATH; ++i) {
        if (folded(value[i])!=folded(expected[i])) return false;
        if (!expected[i]) return true;
    }
    return false;
}
inline bool gameInputExecutable(const wchar_t* value) noexcept {
    for (const auto* expected : gameInputExecutableNames) if (sameExecutable(value,expected)) return true;
    return false;
}
inline const wchar_t* selectedName(const wchar_t* value) noexcept {
    for (const auto* expected : executableNames) if (sameExecutable(value,expected)) return expected;
    for (const auto* expected : gameInputExecutableNames) if (sameExecutable(value,expected)) return expected;
    return nullptr;
}
inline bool checkpointExpired(ULONGLONG begin) noexcept { return GetTickCount64()-begin>=phaseCheckpointMs; }
inline json birth(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    SetLastError(ERROR_SUCCESS);
    const bool success=GetProcessTimes(process,&created,&exited,&kernel,&user)!=FALSE;
    const DWORD error=success ? ERROR_SUCCESS : GetLastError();
    auto out=queryResult(success,error);
    out["fileTime"]=success ? json(fileTimeString(created)) : json(nullptr);
    out["encoding"]="unsigned-64-bit-decimal-string;100ns-since-1601-UTC";
    return out;
}
inline json session(DWORD pid) {
    DWORD value=0; SetLastError(ERROR_SUCCESS);
    const bool success=ProcessIdToSessionId(pid,&value)!=FALSE;
    const DWORD error=success ? ERROR_SUCCESS : GetLastError();
    auto out=queryResult(success,error); out["id"]=success ? json(value) : json(nullptr);
    out["scope"]="PID query; not atomic with snapshot or process handle";
    return out;
}

// Loads only the OS version resource reader, never the selected executable.
// One lazy load attempt per collection; all functions resolved dynamically.
struct VersionApi {
    using SizeFn = DWORD (WINAPI*)(LPCWSTR,LPDWORD);
    using ReadFn = BOOL (WINAPI*)(LPCWSTR,DWORD,DWORD,LPVOID);
    using QueryFn = BOOL (WINAPI*)(LPCVOID,LPCWSTR,LPVOID*,PUINT);
    HMODULE module=nullptr;
    SizeFn size=nullptr; ReadFn read=nullptr; QueryFn query=nullptr;
    bool attempted=false; DWORD error=ERROR_SUCCESS;
    VersionApi() noexcept = default;
    VersionApi(const VersionApi&)=delete;
    VersionApi& operator=(const VersionApi&)=delete;
    ~VersionApi() noexcept { if (module) FreeLibrary(module); }
    bool ensure() noexcept {
        if (attempted) return size && read && query;
        attempted=true; SetLastError(ERROR_SUCCESS);
        module=LoadLibraryExW(L"version.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) { error=GetLastError(); return false; }
        SetLastError(ERROR_SUCCESS);
        size=reinterpret_cast<SizeFn>(GetProcAddress(module,"GetFileVersionInfoSizeW"));
        if (!size) { error=GetLastError(); return false; }
        read=reinterpret_cast<ReadFn>(GetProcAddress(module,"GetFileVersionInfoW"));
        if (!read) { error=GetLastError(); return false; }
        query=reinterpret_cast<QueryFn>(GetProcAddress(module,"VerQueryValueW"));
        if (!query) { error=GetLastError(); return false; }
        return true;
    }
    json evidence() const {
        if (!attempted) return skipped("not-needed");
        auto out=queryResult(size && read && query,error);
        out["loadPolicy"]="LoadLibraryExW(LOAD_LIBRARY_SEARCH_SYSTEM32)";
        return out;
    }
};
inline json fileVersion(VersionApi& api, const std::wstring& path) {
    if (!api.ensure()) {
        auto out=skipped("system32-version-api-unavailable"); out["loaderWin32Error"]=api.error; return out;
    }
    DWORD unused=0; SetLastError(ERROR_SUCCESS);
    const DWORD bytes=api.size(path.c_str(),&unused);
    if (!bytes) return queryResult(false,GetLastError());
    if (bytes>maxVersionBytes) {
        auto out=skipped("version-resource-byte-limit"); out["reportedBytes"]=bytes; return out;
    }
    std::vector<DWORD> buffer((bytes+sizeof(DWORD)-1)/sizeof(DWORD));
    SetLastError(ERROR_SUCCESS);
    if (!api.read(path.c_str(),0,bytes,buffer.data())) return queryResult(false,GetLastError());
    LPVOID root=nullptr; UINT rootBytes=0;
    // VerQueryValue does not specify a GetLastError contract. Do not invent one.
    if (!api.query(buffer.data(),L"\\",&root,&rootBytes)) {
        auto out=skipped("fixed-file-info-unavailable"); out["queried"]=true;
        out["win32ErrorApplicable"]=false; return out;
    }
    const auto base=reinterpret_cast<std::uintptr_t>(buffer.data());
    const auto address=reinterpret_cast<std::uintptr_t>(root);
    if (!root || rootBytes<sizeof(VS_FIXEDFILEINFO) || address<base ||
        address-base>bytes || sizeof(VS_FIXEDFILEINFO)>bytes-(address-base)) {
        auto out=skipped("invalid-fixed-file-info-range"); out["queried"]=true; return out;
    }
    VS_FIXEDFILEINFO fixed{}; std::memcpy(&fixed,root,sizeof(fixed));
    if (fixed.dwSignature!=0xfeef04bdu) {
        auto out=skipped("invalid-fixed-file-info-signature"); out["queried"]=true; return out;
    }
    const std::array<unsigned,4> parts{{HIWORD(fixed.dwFileVersionMS),LOWORD(fixed.dwFileVersionMS),
        HIWORD(fixed.dwFileVersionLS),LOWORD(fixed.dwFileVersionLS)}};
    auto out=queryResult(true,ERROR_SUCCESS);
    out["parts"]=parts;
    out["value"]=std::to_string(parts[0])+"."+std::to_string(parts[1])+"."+
        std::to_string(parts[2])+"."+std::to_string(parts[3]);
    out["source"]="file-at-queried-image-path:VS_FIXEDFILEINFO.dwFileVersion";
    out["loadedImageVersionProven"]=false;
    return out;
}
inline constexpr std::size_t maxDerivedInstallations=4;
inline constexpr const wchar_t* defaultInstallRoot=L"C:\\Program Files\\Netease\\GameViewer";
struct ObservedInstallation {std::wstring root;DWORD sourcePid;};
inline json utf8Path(const std::wstring& path);
inline bool samePath(const std::wstring& a,const std::wstring& b) noexcept {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)if(folded(a[i])!=folded(b[i]))return false;
    return true;
}
inline bool normalizedLocalPath(const std::wstring& input,std::wstring& path) {
    path=input;
    for(auto& ch:path)if(ch==L'/')ch=L'\\';
    if(path.size()<4||!((path[0]>=L'A'&&path[0]<=L'Z')||(path[0]>=L'a'&&path[0]<=L'z'))||path[1]!=L':'||path[2]!=L'\\')return false;
    for(std::size_t begin=3;begin<path.size();) {
        const auto next=path.find(L'\\',begin);
        const auto component=path.substr(begin,next==std::wstring::npos?path.size()-begin:next-begin);
        if(component.empty()||component==L"."||component==L".."||component.back()==L'.'||component.back()==L' ')return false;
        for(wchar_t ch:component)if(ch<0x20||ch==L':'||ch==L'*'||ch==L'?'||ch==L'"'||ch==L'<'||ch==L'>'||ch==L'|')return false;
        if(next==std::wstring::npos)break;
        begin=next+1;
    }
    return path.back()!=L'\\';
}
inline json installationObservation(const std::wstring& image,const wchar_t* expected,DWORD pid,std::vector<ObservedInstallation>& installs) {
    json out={{"known",false},{"layoutMatched",false},{"publisherVerified",false},{"physicalIdentityProven",false},
        {"defaultExecutableLocationMatchesByPath",nullptr},{"nonDefaultExecutableLocationObserved",nullptr},
        {"nonDefaultInstallationObserved",nullptr},{"root",nullptr},{"derivedRootEligible",false},{"derivedRootRetained",false}};
    std::wstring path;
    if(!normalizedLocalPath(image,path)){out["reason"]="unsupported-or-non-normalized-local-executable-path";return out;}
    const bool atDefault=samePath(path,std::wstring(defaultInstallRoot)+L"\\bin\\"+expected);
    out["known"]=true;out["defaultExecutableLocationMatchesByPath"]=atDefault;
    out["nonDefaultExecutableLocationObserved"]=!atDefault;
    const auto leaf=path.find_last_of(L'\\');
    if(leaf==std::wstring::npos||!sameExecutable(path.c_str()+leaf+1,expected)){out["reason"]="snapshot-image-name-mismatch";return out;}
    const auto bin=path.find_last_of(L'\\',leaf-1);
    if(bin==std::wstring::npos||!samePath(path.substr(bin+1,leaf-bin-1),L"bin")){out["reason"]="not-GameViewer-bin-layout";return out;}
    const std::wstring root=path.substr(0,bin);
    const auto parent=root.find_last_of(L'\\');
    if(parent==std::wstring::npos||!samePath(root.substr(parent+1),L"GameViewer")){out["reason"]="not-GameViewer-installation-root";return out;}
    const auto encoded=utf8Path(root);
    if(!encoded.value("known",false)){out["known"]=false;out["rootEncoding"]=encoded;return out;}
    out["layoutMatched"]=true;out["root"]=encoded.at("value");out["derivedRootEligible"]=true;
    out["nonDefaultInstallationObserved"]=!samePath(root,defaultInstallRoot);
    out["basis"]="queried-process-image-path;exact-GameViewer/bin/allowlisted-name-layout;publisher-unverified";
    for(const auto& existing:installs)if(samePath(existing.root,root)){out["derivedRootRetained"]=true;return out;}
    if(installs.size()>=maxDerivedInstallations){out["reason"]="derived-installation-limit";return out;}
    installs.push_back({root,pid});out["derivedRootRetained"]=true;return out;
}
inline json processRow(const PROCESSENTRY32W& entry, const wchar_t* name, VersionApi& api, ULONGLONG begin, std::vector<ObservedInstallation>& installs) {
    json out={{"pid",entry.th32ProcessID},{"parentPid",entry.th32ParentProcessID},
        {"parentPidScope","Toolhelp snapshot; parent lifetime/identity unverified"},
        {"snapshotName",asciiName(name)},{"snapshotToHandleIdentityProven",false},
        {"category",gameInputExecutable(name) ? "gameinput-service-candidate" : "remote-input-client"},
        {"imagePath",skipped("process-open-unavailable")},{"birthFileTime",skipped("process-open-unavailable")},
        {"fileVersion",skipped("image-path-unavailable")},{"installation",skipped("image-path-unavailable")}};
    SetLastError(ERROR_SUCCESS);
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID));
    const DWORD openError=process ? ERROR_SUCCESS : GetLastError();
    out["processOpen"]=queryResult(static_cast<bool>(process),openError);
    // Session query is independent, including when process access is denied.
    out["session"]=session(entry.th32ProcessID);
    if (!process) return out;
    out["birthFileTime"]=birth(process.value);
    std::array<wchar_t,maxImagePathChars> image{}; DWORD chars=static_cast<DWORD>(image.size());
    SetLastError(ERROR_SUCCESS);
    const bool pathOk=QueryFullProcessImageNameW(process.value,0,image.data(),&chars)!=FALSE;
    const DWORD pathError=pathOk ? ERROR_SUCCESS : GetLastError();
    out["imagePath"]=queryResult(pathOk,pathError);
    out["imagePath"]["value"]=nullptr;
    if (!pathOk) return out;
    if (!chars || chars>=image.size()) {
        out["imagePath"]["known"]=false; out["imagePath"]["status"]="invalid-path-length"; return out;
    }
    const std::wstring path(image.data(),chars);
    SetLastError(ERROR_SUCCESS);
    const int utf8Bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path.data(),static_cast<int>(path.size()),nullptr,0,nullptr,nullptr);
    if (!utf8Bytes) {
        out["imagePath"]["encoding"]=queryResult(false,GetLastError());
        out["imagePath"]["known"]=false; return out;
    }
    std::string utf8(static_cast<std::size_t>(utf8Bytes),'\0'); SetLastError(ERROR_SUCCESS);
    if (!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path.data(),static_cast<int>(path.size()),utf8.data(),utf8Bytes,nullptr,nullptr)) {
        out["imagePath"]["encoding"]=queryResult(false,GetLastError());
        out["imagePath"]["known"]=false; return out;
    }
    out["imagePath"]["value"]=utf8;
    const auto separator=path.find_last_of(L"\\/");
    const wchar_t* baseName=path.c_str()+(separator==std::wstring::npos ? 0 : separator+1);
    const bool stillMatches=sameExecutable(baseName,name);
    out["imagePath"]["baseNameStillMatchesSnapshot"]=stillMatches;
    out["installation"]=gameInputExecutable(name) ? skipped("not-GameViewer-installation")
        : installationObservation(path,name,entry.th32ProcessID,installs);
    if (!stillMatches) out["fileVersion"]=skipped("image-name-changed-after-snapshot");
    else if (checkpointExpired(begin)) out["fileVersion"]=skipped("process-phase-checkpoint-budget");
    else out["fileVersion"]=fileVersion(api,path);
    return out;
}
inline void collectProcesses(json& out,std::vector<ObservedInstallation>& installs) {
    const ULONGLONG begin=GetTickCount64();
    out={{"snapshotAttempts",1},{"snapshot",skipped("not-yet-attempted")},
        {"enumerationComplete",false},{"enumerationWin32Error",nullptr},{"entriesVisited",0},
        {"matchingEntriesObserved",0},{"rows",json::array()},{"stopReason",nullptr},
        {"absenceProven",false},{"signatureChecked",false},
        {"defaultInstallationPathObserved",false},{"nonDefaultInstallationObserved",false},
        {"nonDefaultExecutablePathObserved",false},{"installPathQueriesComplete",false},{"installationPresenceIsAbsenceProof",false},
        {"selection","case-insensitive exact executable basename only; not verified publisher identity"}};
    VersionApi api;
    SetLastError(ERROR_SUCCESS);
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
    const DWORD snapshotError=snapshot ? ERROR_SUCCESS : GetLastError();
    out["snapshot"]=queryResult(static_cast<bool>(snapshot),snapshotError);
    if (!snapshot) { out["stopReason"]="snapshot-failed"; return; }
    PROCESSENTRY32W entry{}; entry.dwSize=sizeof(entry); SetLastError(ERROR_SUCCESS);
    BOOL available=Process32FirstW(snapshot.value,&entry);
    DWORD enumerationError=available ? ERROR_SUCCESS : GetLastError();
    DWORD visited=0, matched=0;
    while (available) {
        if (visited>=maxProcessEntries) { out["stopReason"]="process-entry-limit"; break; }
        if (checkpointExpired(begin)) { out["stopReason"]="process-phase-checkpoint-budget"; break; }
        ++visited;
        if (const auto* name=selectedName(entry.szExeFile)) {
            ++matched;
            if (out["rows"].size()>=maxMatchingProcesses) { out["stopReason"]="matching-process-limit"; break; }
            out["rows"].push_back(processRow(entry,name,api,begin,installs));
        }
        entry.dwSize=sizeof(entry); SetLastError(ERROR_SUCCESS);
        available=Process32NextW(snapshot.value,&entry);
        enumerationError=available ? ERROR_SUCCESS : GetLastError();
    }
    if (!available) {
        out["enumerationComplete"]=enumerationError==ERROR_NO_MORE_FILES;
        out["enumerationWin32Error"]=enumerationError;
        out["stopReason"]=enumerationError==ERROR_NO_MORE_FILES ? "snapshot-end" : "enumeration-failed";
    }
    out["entriesVisited"]=visited; out["matchingEntriesObserved"]=matched;
    out["defaultInstallationPathObserved"]=false; out["nonDefaultInstallationObserved"]=false;
    out["nonDefaultExecutablePathObserved"]=false;out["installPathQueriesComplete"]=out["enumerationComplete"];
    for(const auto& row:out["rows"]) {
        if(row.value("category",std::string{})=="gameinput-service-candidate")continue;
        const auto& install=row["installation"];
        if(!install.value("known",false)){out["installPathQueriesComplete"]=false;continue;}
        if(install.value("defaultExecutableLocationMatchesByPath",false))out["defaultInstallationPathObserved"]=true;
        if(install.value("nonDefaultExecutableLocationObserved",false))out["nonDefaultExecutablePathObserved"]=true;
        if(install.value("layoutMatched",false)&&install.value("nonDefaultInstallationObserved",false))out["nonDefaultInstallationObserved"]=true;
        if(install.value("derivedRootEligible",false)&&!install.value("derivedRootRetained",false))out["installPathQueriesComplete"]=false;
    }
    out["installationPresenceIsAbsenceProof"]=false;
    out["versionApi"]=api.evidence(); out["elapsedMs"]=GetTickCount64()-begin;
}
inline const char* serviceStateName(DWORD state) noexcept {
    switch (state) {
    case SERVICE_STOPPED:return "stopped";
    case SERVICE_START_PENDING:return "start-pending";
    case SERVICE_STOP_PENDING:return "stop-pending";
    case SERVICE_RUNNING:return "running";
    case SERVICE_CONTINUE_PENDING:return "continue-pending";
    case SERVICE_PAUSE_PENDING:return "pause-pending";
    case SERVICE_PAUSED:return "paused";
    default:return "unrecognized";
    }
}
inline void serviceStatus(SC_HANDLE manager, const wchar_t* name, json& row) {
    SetLastError(ERROR_SUCCESS);
    ServiceHandle service(OpenServiceW(manager,name,SERVICE_QUERY_STATUS));
    const DWORD openError=service.value ? ERROR_SUCCESS : GetLastError();
    row["statusOpen"]=queryResult(service.value!=nullptr,openError);
    if (!service.value) return;
    SERVICE_STATUS_PROCESS status{}; DWORD needed=0; SetLastError(ERROR_SUCCESS);
    const bool success=QueryServiceStatusEx(service.value,SC_STATUS_PROCESS_INFO,
        reinterpret_cast<LPBYTE>(&status),sizeof(status),&needed)!=FALSE;
    const DWORD error=success ? ERROR_SUCCESS : GetLastError();
    row["status"]=queryResult(success,error);
    if (!success) return;
    row["status"]["serviceType"]=status.dwServiceType;
    row["status"]["currentState"]=status.dwCurrentState;
    row["status"]["stateName"]=serviceStateName(status.dwCurrentState);
    row["status"]["controlsAccepted"]=status.dwControlsAccepted;
    row["status"]["serviceWin32ExitCode"]=status.dwWin32ExitCode;
    row["status"]["serviceSpecificExitCode"]=status.dwServiceSpecificExitCode;
    row["status"]["processId"]=status.dwProcessId;
    row["status"]["processIdScope"]="raw SCM field; driver PID or transition-state PID may be undefined; not an input producer";
    row["status"]["checkPoint"]=status.dwCheckPoint;
    row["status"]["waitHintMs"]=status.dwWaitHint;
    row["status"]["serviceFlags"]=status.dwServiceFlags;
}
inline void serviceConfig(SC_HANDLE manager, const wchar_t* name, json& row) {
    SetLastError(ERROR_SUCCESS);
    ServiceHandle service(OpenServiceW(manager,name,SERVICE_QUERY_CONFIG));
    const DWORD openError=service.value ? ERROR_SUCCESS : GetLastError();
    row["configOpen"]=queryResult(service.value!=nullptr,openError);
    if (!service.value) return;
    DWORD needed=0; SetLastError(ERROR_SUCCESS);
    const bool probe=QueryServiceConfigW(service.value,nullptr,0,&needed)!=FALSE;
    const DWORD probeError=probe ? ERROR_SUCCESS : GetLastError();
    row["configProbe"]={{"success",probe},{"win32Error",probeError},{"requiredBytes",needed}};
    if (probe || probeError!=ERROR_INSUFFICIENT_BUFFER) {
        row["config"]=queryResult(false,probeError);
        if (probe) row["config"]["status"]="unexpected-empty-buffer-probe-success";
        return;
    }
    if (needed<sizeof(QUERY_SERVICE_CONFIGW) || needed>maxServiceConfigBytes) {
        row["config"]=skipped("service-config-byte-limit-or-invalid-size"); return;
    }
    // Pointer-aligned storage. No returned account/command-line/string is read.
    std::vector<DWORD_PTR> storage((needed+sizeof(DWORD_PTR)-1)/sizeof(DWORD_PTR));
    auto* config=reinterpret_cast<QUERY_SERVICE_CONFIGW*>(storage.data());
    const DWORD capacity=needed; SetLastError(ERROR_SUCCESS);
    const bool success=QueryServiceConfigW(service.value,config,capacity,&needed)!=FALSE;
    const DWORD error=success ? ERROR_SUCCESS : GetLastError();
    row["config"]=queryResult(success,error);
    if (!success) return; // One read only: changing config is partial, never retried.
    row["config"]["serviceType"]=config->dwServiceType;
    row["config"]["startType"]=config->dwStartType;
    row["config"]["errorControl"]=config->dwErrorControl;
    row["config"]["tagId"]=config->dwTagId;
    row["config"]["scope"]="numeric public SCM configuration only; no binary command line, account, dependencies or private configuration";
}
inline void collectServices(json& out) {
    const ULONGLONG begin=GetTickCount64();
    out={{"managerOpen",skipped("not-yet-attempted")},{"rows",json::array()},
        {"candidateNameProbes",true},{"missingExactNameProvesGameInputAbsent",false},
        {"servicePidToImageBindingProven",false},
        {"scope","five exact SCM candidate names; not inferred from EXE names; status and config queried independently"}};
    SetLastError(ERROR_SUCCESS);
    ServiceHandle manager(OpenSCManagerW(nullptr,SERVICES_ACTIVE_DATABASEW,SC_MANAGER_CONNECT));
    const DWORD managerError=manager.value ? ERROR_SUCCESS : GetLastError();
    out["managerOpen"]=queryResult(manager.value!=nullptr,managerError);
    for (const auto* name:serviceNames) {
        json row={{"name",asciiName(name)},{"statusOpen",skipped("manager-unavailable")},
            {"configOpen",skipped("manager-unavailable")},{"status",skipped("service-status-open-unavailable")},
            {"config",skipped("service-config-open-unavailable")}};
        if (manager.value) {
            if (checkpointExpired(begin)) {
                row["statusOpen"]=skipped("service-phase-checkpoint-budget");
                row["configOpen"]=skipped("service-phase-checkpoint-budget");
            } else {
                serviceStatus(manager.value,name,row);
                if (!checkpointExpired(begin)) serviceConfig(manager.value,name,row);
                else row["configOpen"]=skipped("service-phase-checkpoint-budget");
            }
        }
        out["rows"].push_back(std::move(row));
    }
    out["elapsedMs"]=GetTickCount64()-begin;
}

// Fixed roots observed in the UU 4.42 local slog inventory (2026-10-24).
// No registry/config/profile lookup and no installation-path guessing.
inline constexpr std::array<const wchar_t*,7> verifiedLogRoots{{
    L"C:\\Program Files\\Netease\\GameViewer\\log\\uuyc-cli\\Log",
    L"C:\\Program Files\\Netease\\GameViewer\\log\\upgrade\\Log",
    L"C:\\Program Files\\Netease\\GameViewer\\log\\service\\Log",
    L"C:\\Program Files\\Netease\\GameViewer\\log\\server\\log",
    L"C:\\Program Files\\Netease\\GameViewer\\log\\Healthd\\log",
    L"C:\\Program Files\\Netease\\GameViewer\\log\\launcher\\log",
    L"C:\\Program Files\\Netease\\GameViewer\\log\\client\\Log"
}};
inline constexpr DWORD maxLogEntriesPerRoot=512, maxLogEntries=2048;
inline constexpr std::size_t maxLogCandidates=4;
inline constexpr std::uint64_t maxCandidateLogBytes=12ull*1024*1024;
struct FindHandle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit FindHandle(HANDLE handle) noexcept:value(handle){}
    ~FindHandle() noexcept {if(value!=INVALID_HANDLE_VALUE)FindClose(value);}
    FindHandle(const FindHandle&)=delete;
    FindHandle& operator=(const FindHandle&)=delete;
};
inline json utf8Path(const std::wstring& path) {
    SetLastError(ERROR_SUCCESS);
    const int bytes=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path.data(),static_cast<int>(path.size()),nullptr,0,nullptr,nullptr);
    if(!bytes)return queryResult(false,GetLastError());
    std::string value(static_cast<std::size_t>(bytes),'\0'); SetLastError(ERROR_SUCCESS);
    if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,path.data(),static_cast<int>(path.size()),value.data(),bytes,nullptr,nullptr))
        return queryResult(false,GetLastError());
    auto out=queryResult(true,ERROR_SUCCESS);out["value"]=value;return out;
}
inline json inspectLogRoot(const std::wstring& root,ULONGLONG begin) {
    // Check only exact fixed path components, never enumerate their contents.
    // Refuse junctions/symlinks at every component, including the drive root.
    for(std::size_t end=3;;) {
        if(checkpointExpired(begin))return skipped("log-phase-checkpoint-budget");
        const std::wstring component=root.substr(0,end);SetLastError(ERROR_SUCCESS);
        const DWORD attrs=GetFileAttributesW(component.c_str());
        if(attrs==INVALID_FILE_ATTRIBUTES) {
            auto out=queryResult(false,GetLastError());out["failedComponent"]=utf8Path(component);return out;
        }
        if(!(attrs&FILE_ATTRIBUTE_DIRECTORY)||(attrs&FILE_ATTRIBUTE_REPARSE_POINT)) {
            auto out=skipped("non-directory-or-reparse-root-component");out["queried"]=true;
            out["attributes"]=attrs;out["failedComponent"]=utf8Path(component);return out;
        }
        if(end==root.size())break;
        const auto next=root.find(L'\\',end+1);
        end=next==std::wstring::npos?root.size():next;
    }
    return queryResult(true,ERROR_SUCCESS);
}
struct SlogCandidate {std::wstring path,root;const char* role;bool derived,verifiedDefault;std::uint64_t bytes;FILETIME modified;};
inline bool newerLog(const SlogCandidate& a,const SlogCandidate& b) {
    const auto at=fileTimeValue(a.modified),bt=fileTimeValue(b.modified);
    return at!=bt?at>bt:a.path<b.path;
}
struct LogRoot {std::wstring path;const char* role;bool derived,verifiedDefault;DWORD sourcePid;};
inline int primaryRole(const char* role) noexcept {
    if(std::strcmp(role,"server")==0)return 0;
    if(std::strcmp(role,"client")==0)return 1;
    if(std::strcmp(role,"service")==0)return 2;
    return -1;
}
inline void collectLogCandidates(json& candidates,json& discovery,const std::vector<ObservedInstallation>& installs) {
    const ULONGLONG begin=GetTickCount64();
    candidates=json::array();
    discovery={{"basis","verified-default-UU4.42-log-roots-plus-strict-query-image-derived-installations"},
        {"complete",true},{"roots",json::array()},{"entriesVisited",0},{"selectedBytes",0},
        {"maxCandidates",maxLogCandidates},{"maxSelectedBytes",maxCandidateLogBytes},
        {"maxEntriesPerRoot",maxLogEntriesPerRoot},{"maxEntries",maxLogEntries},{"maxDerivedInstallations",maxDerivedInstallations},
        {"contentsRead",false},{"filesCopied",false},{"slogDecoded",false},{"recursive",false},
        {"snapshotAtomic",false},{"filesystemIdentityProven",false},{"absenceOfUuLogsProven",false},
        {"selection","reserve newest eligible server/client/service log before recent fill; hard byte cap wins if role reservations cannot all fit"},
        {"scope","Exact fixed log roots or only three log suffixes derived from a normalized local queried image ending in GameViewer/bin/exact-allowlisted-name. Derived roots do not verify publisher. Missing default C directories do not prove UU or its logs absent. No profile/config/account directory enumeration. Reject observed root/file reparse attributes; caller revalidates containment, final disk identity and actual copy byte budget. Non-atomic; native SLOG content remains unread and undecoded."}};
    std::vector<LogRoot> roots;roots.reserve(verifiedLogRoots.size()+3*maxDerivedInstallations);
    const auto addRoot=[&](const std::wstring& path,const char* role,bool derived,bool verified,DWORD pid) {
        for(auto& root:roots)if(samePath(root.path,path)) {
            root.derived=root.derived||derived;root.verifiedDefault=root.verifiedDefault||verified;
            if(derived)root.sourcePid=pid;return;
        }
        roots.push_back({path,role,derived,verified,pid});
    };
    for(const auto& install:installs) {
        addRoot(install.root+L"\\log\\server\\log","server",true,false,install.sourcePid);
        addRoot(install.root+L"\\log\\client\\Log","client",true,false,install.sourcePid);
        addRoot(install.root+L"\\log\\service\\Log","service",true,false,install.sourcePid);
    }
    const std::array<const char*,7> fixedRoles{{"uuyc-cli","upgrade","service","server","healthd","launcher","client"}};
    // Visit the three required roles before auxiliary fixed directories.
    for(const std::size_t index:{3u,6u,2u,0u,1u,4u,5u})addRoot(verifiedLogRoots[index],fixedRoles[index],false,true,0);
    std::vector<SlogCandidate> newest;newest.reserve(maxLogCandidates+1);
    std::array<SlogCandidate,3> roleNewest{};std::array<bool,3> roleFound{{false,false,false}};
    std::array<unsigned,3> roleFilesSeen{{0,0,0}};
    DWORD total=0;
    for(const auto& spec:roots) {
        const std::wstring& root=spec.path;const auto rootText=utf8Path(root);
        json row={{"root",rootText.value("value",json(nullptr))},{"rootEncoding",rootText},{"role",spec.role},
            {"derived",spec.derived},{"verifiedDefaultRoot",spec.verifiedDefault},{"publisherVerified",false},
            {"sourceProcessPid",spec.derived?json(spec.sourcePid):json(nullptr)},
            {"entriesVisited",0},{"enumerationComplete",false},{"rejectedNonRegularOrReparse",0},
            {"rejectedNonSlogName",0},{"rejectedOversize",0},{"enumerationWin32Error",nullptr}};
        row["rootCheck"]=inspectLogRoot(root,begin);
        if(!rootText.value("known",false)||!row["rootCheck"].value("known",false)||total>=maxLogEntries||checkpointExpired(begin)) {
            row["stopReason"]=total>=maxLogEntries?"total-log-entry-limit":
                (checkpointExpired(begin)?"log-phase-checkpoint-budget":"root-unavailable-or-rejected");
            discovery["complete"]=false;discovery["roots"].push_back(std::move(row));continue;
        }
        WIN32_FIND_DATAW data{};SetLastError(ERROR_SUCCESS);
        FindHandle search(FindFirstFileExW((root+L"\\*.slog").c_str(),FindExInfoBasic,&data,FindExSearchNameMatch,nullptr,0));
        if(search.value==INVALID_HANDLE_VALUE) {
            const DWORD error=GetLastError();row["enumerationWin32Error"]=error;
            row["enumerationComplete"]=error==ERROR_FILE_NOT_FOUND;
            row["stopReason"]=error==ERROR_FILE_NOT_FOUND?"no-matching-log-files":"enumeration-open-failed";
            if(error!=ERROR_FILE_NOT_FOUND)discovery["complete"]=false;
            discovery["roots"].push_back(std::move(row));continue;
        }
        DWORD visited=0,rejected=0,wrongName=0,oversize=0,error=ERROR_SUCCESS;bool available=true;
        while(available) {
            if(visited>=maxLogEntriesPerRoot||total>=maxLogEntries||checkpointExpired(begin)) {
                row["stopReason"]=checkpointExpired(begin)?"log-phase-checkpoint-budget":"log-entry-limit";
                discovery["complete"]=false;break;
            }
            ++visited;++total;
            if(data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))++rejected;
            else {
                const std::wstring name(data.cFileName);
                const bool leaf=name.size()>5&&name.find_first_of(L"\\/:")==std::wstring::npos&&
                    sameExecutable(name.c_str()+name.size()-5,L".slog");
                const std::uint64_t bytes=(static_cast<std::uint64_t>(data.nFileSizeHigh)<<32)|data.nFileSizeLow;
                const int role=primaryRole(spec.role);
                if(!leaf)++wrongName;
                else {
                    if(role>=0)++roleFilesSeen[static_cast<std::size_t>(role)];
                    if(bytes>maxCandidateLogBytes)++oversize;
                    else {
                        SlogCandidate candidate{root+L"\\"+name,root,spec.role,spec.derived,spec.verifiedDefault,bytes,data.ftLastWriteTime};
                        if(role>=0) {
                            const auto slot=static_cast<std::size_t>(role);
                            if(!roleFound[slot]||newerLog(candidate,roleNewest[slot])){roleNewest[slot]=candidate;roleFound[slot]=true;}
                        }
                        newest.push_back(std::move(candidate));std::sort(newest.begin(),newest.end(),newerLog);
                        if(newest.size()>maxLogCandidates)newest.pop_back();
                    }
                }
            }
            SetLastError(ERROR_SUCCESS);available=FindNextFileW(search.value,&data)!=FALSE;
            error=available?ERROR_SUCCESS:GetLastError();
        }
        row["entriesVisited"]=visited;row["rejectedNonRegularOrReparse"]=rejected;
        row["rejectedNonSlogName"]=wrongName;row["rejectedOversize"]=oversize;
        if(!available) {
            row["enumerationWin32Error"]=error;row["enumerationComplete"]=error==ERROR_NO_MORE_FILES;
            row["stopReason"]=error==ERROR_NO_MORE_FILES?"directory-end":"enumeration-failed";
            if(error!=ERROR_NO_MORE_FILES)discovery["complete"]=false;
        }
        discovery["roots"].push_back(std::move(row));
    }
    std::vector<SlogCandidate> priority;
    std::uint64_t reservationBytes=0;
    for(std::size_t i=0;i<roleFound.size();++i)if(roleFound[i]){priority.push_back(roleNewest[i]);reservationBytes+=roleNewest[i].bytes;}
    // If the three latest full files cannot coexist under 12 MiB, retain as
    // many distinct roles as possible; never silently exceed the byte limit.
    std::sort(priority.begin(),priority.end(),[](const SlogCandidate& a,const SlogCandidate& b){
        return a.bytes!=b.bytes?a.bytes<b.bytes:newerLog(a,b);
    });
    std::vector<std::wstring> consideredPaths;std::array<bool,3> roleSelected{{false,false,false}};
    std::uint64_t selectedBytes=0;unsigned budgetOmitted=0,encodingOmitted=0;
    const auto select=[&](const SlogCandidate& candidate,bool reserved) {
        if(candidates.size()>=maxLogCandidates)return;
        for(const auto& path:consideredPaths)if(samePath(path,candidate.path))return;
        consideredPaths.push_back(candidate.path);
        if(candidate.bytes>maxCandidateLogBytes-selectedBytes){++budgetOmitted;return;}
        const auto path=utf8Path(candidate.path),root=utf8Path(candidate.root);
        if(!path.value("known",false)||!root.value("known",false)){++encodingOmitted;discovery["complete"]=false;return;}
        candidates.push_back({{"path",path.at("value")},{"root",root.at("value")},{"role",candidate.role},
            {"derived",candidate.derived},{"verifiedDefaultRoot",candidate.verifiedDefault},{"publisherVerified",false},
            {"sizeBytes",candidate.bytes},{"mtimeFileTime",fileTimeString(candidate.modified)},
            {"mtimeEncoding","unsigned-64-bit-decimal-string;100ns-since-1601-UTC"},
            {"selectionPriority",reserved?"latest-required-role":"recent-fill"},
            {"basis",candidate.derived?"query-image-derived-log-root;publisher-unverified":"verified-default-log-root"}});
        selectedBytes+=candidate.bytes;
        const int role=primaryRole(candidate.role);if(role>=0)roleSelected[static_cast<std::size_t>(role)]=true;
    };
    for(const auto& candidate:priority)select(candidate,true);
    for(const auto& candidate:newest)select(candidate,false);
    discovery["roleCoverage"]=json::array();
    const std::array<const char*,3> requiredRoles{{"server","client","service"}};
    bool allObservedRolesSelected=true;
    for(std::size_t i=0;i<requiredRoles.size();++i) {
        const bool present=roleFilesSeen[i]!=0;
        if(present&&!roleSelected[i])allObservedRolesSelected=false;
        discovery["roleCoverage"].push_back({{"role",requiredRoles[i]},{"logFilesObserved",roleFilesSeen[i]},
            {"eligibleLatestObserved",roleFound[i]},{"selected",roleSelected[i]},
            {"omissionReason",roleSelected[i]?json(nullptr):json(!present?"no-role-log-observed;not-absence-proof":
                (!roleFound[i]?"all-observed-role-files-exceed-byte-cap":(reservationBytes>maxCandidateLogBytes?"role-reservations-exceed-total-byte-cap":"path-encoding-or-selection-unavailable")))}});
    }
    discovery["requiredRolesCoveredAmongObserved"]=allObservedRolesSelected;
    if(!allObservedRolesSelected)discovery["complete"]=false;
    discovery["roleReservationBytes"]=reservationBytes;
    discovery["roleReservationsFitByteBudget"]=reservationBytes<=maxCandidateLogBytes;
    discovery["entriesVisited"]=total;discovery["newestWindowCount"]=newest.size();
    discovery["selectedBytes"]=selectedBytes;discovery["omittedByTotalByteBudget"]=budgetOmitted;
    discovery["omittedByPathEncoding"]=encodingOmitted;discovery["elapsedMs"]=GetTickCount64()-begin;
}
} // namespace ymcc::remoteInputEnvironmentEvidence

// Call ONLY from the existing explicit export path. No implicit initialization,
// cache, timer, hook, thread, log content read, file output, device handle or system mutation.
static json collectRemoteInputEnvironmentEvidence() {
    using namespace ymcc::remoteInputEnvironmentEvidence;
    LastErrorScope restore;
    const ULONGLONG begin=GetTickCount64(); FILETIME wallBegin{}; GetSystemTimeAsFileTime(&wallBegin);
    json out={{"schema",1},{"readOnly",true},{"invocationScope","explicit-export-only;caller-owned"},
        {"snapshotAtomic",false},{"status","partial"},{"exception",false},
        {"collectionBeginTick",begin},{"collectionBeginFileTime",fileTimeString(wallBegin)},
        {"processes",{{"status","not-collected"}}},{"services",{{"status","not-collected"}}},
        {"logCandidates",json::array()},{"logDiscovery",{{"complete",false},{"status","not-collected"}}},
        {"limits",{{"toolhelpSnapshotAttempts",1},{"processEntries",maxProcessEntries},
            {"matchingProcesses",maxMatchingProcesses},{"phaseCheckpointMs",phaseCheckpointMs},
            {"hardIoTimeout",false},{"imagePathChars",maxImagePathChars},{"versionResourceBytes",maxVersionBytes},
            {"services",serviceNames.size()},{"serviceConfigBytes",maxServiceConfigBytes},
            {"serviceConfigReadRetries",0},{"logRoots",verifiedLogRoots.size()+3*maxDerivedInstallations},
            {"derivedInstallations",maxDerivedInstallations},
            {"logCandidates",maxLogCandidates},{"candidateLogBytes",maxCandidateLogBytes},
            {"logEntriesPerRoot",maxLogEntriesPerRoot},{"logEntries",maxLogEntries}}},
        {"interpretation",{{"processPresenceProvesRemoteSession",false},{"runningDriverProvesRemoteSession",false},
            {"inputSenderKnown",false},{"activeControllerOwnershipKnown",false},{"processNamesProvePublisher",false},
            {"pidReuseExcluded",false},{"fileVersionProvesLoadedImage",false},
            {"gameInputRuntimeSelectionKnown",false},{"servicePidToImageBindingProven",false}}},
        {"privacy",{{"commandLinesCollected",false},{"accountsOrTokensCollected",false},
            {"connectionIdsCollected",false},{"privateConfigurationCollected",false}}},
        {"scope","One Toolhelp process snapshot and sequential read-only queries. Names/presence, file versions and driver state do not prove a remote connection, input sender, keyboard delivery or active gamepad ownership. Process exit/PID reuse, file replacement and SCM changes can occur between observations. Checkpoints do not bound a single OS call."}};
    try {
        out["collector"]={{"pid",GetCurrentProcessId()},{"threadId",GetCurrentThreadId()},
            {"birthFileTime",birth(GetCurrentProcess())},{"session",session(GetCurrentProcessId())}};
        out["processAllowlist"]=json::array();
        for (const auto* name:executableNames) out["processAllowlist"].push_back(asciiName(name));
        for (const auto* name:gameInputExecutableNames) out["processAllowlist"].push_back(asciiName(name));
        out["collectorExtensions"]=json::array({"gameinput-service-context-export-only-v1"});
        std::vector<ObservedInstallation> installs;installs.reserve(maxDerivedInstallations);
        collectProcesses(out["processes"],installs);
        collectServices(out["services"]);
        collectLogCandidates(out["logCandidates"],out["logDiscovery"],installs);
        bool complete=out["processes"].value("enumerationComplete",false) && out["logDiscovery"].value("complete",false)
            && out["collector"]["birthFileTime"].value("known",false) && out["collector"]["session"].value("known",false);
        for (const auto& row:out["processes"].value("rows",json::array())) {
            for (const auto* field:{"processOpen","imagePath","birthFileTime","session","fileVersion"})
                complete=complete && row.at(field).value("known",false);
        }
        for (const auto& row:out["services"]["rows"])
            complete=complete && row.at("status").value("known",false) && row.at("config").value("known",false);
        out["status"]=complete ? "complete" : "partial";
    } catch (...) {
        out["exception"]=true; // No exception text: may contain private paths/data.
        out["status"]="partial";
    }
    FILETIME wallEnd{}; GetSystemTimeAsFileTime(&wallEnd);
    out["collectionEndFileTime"]=fileTimeString(wallEnd);
    out["collectionEndTick"]=GetTickCount64(); out["elapsedMs"]=GetTickCount64()-begin;
    return out;
}