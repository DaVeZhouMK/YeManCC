#pragma once
#include "remote_input_environment_evidence.h"

// Ordinary log export only. These observations do not identify the process
// handling a key, read Button Actions, or suppress an official default action.
namespace ymcc::rogOfficialKeyEvidence {
namespace readOnly = ymcc::remoteInputEnvironmentEvidence;
inline constexpr ULONGLONG checkpointMs = 500;
inline constexpr DWORD maxProcessEntries = 8192;
inline constexpr std::size_t maxHelperProcesses = 8;
// Existing oem_vendor_stack.h candidates; query rights only, no takeover.
inline constexpr std::array<const wchar_t*,3> serviceNames{{
    L"ArmouryCrateSEService",L"AsusAppService",L"ArmouryCrateControlInterface"
}};
// Observed in ROG field logs. Name selection is not publisher verification.
inline constexpr const wchar_t* helperName = L"ArmouryCrateSE.UserSessionHelper.exe";
inline bool expired(ULONGLONG begin) noexcept { return GetTickCount64()-begin>=checkpointMs; }
inline DWORD unsignedField(const json& object,const char* name) {
    const auto field=object.find(name);
    if(field==object.end()||!field->is_number_unsigned())return 0;
    const auto value=field->get<std::uint64_t>();
    return value<=MAXDWORD?static_cast<DWORD>(value):0;
}
inline DWORD serviceProcessId(const json& row) {
    const auto status=row.find("status");
    if(status==row.end()||!status->is_object()||!status->value("known",false))return 0;
    const auto type=unsignedField(*status,"serviceType");
    const auto state=unsignedField(*status,"currentState");
    // SCM does not guarantee a PID in start/stop-pending or stopped states.
    if(!(type&SERVICE_WIN32)||
        (state!=SERVICE_RUNNING&&state!=SERVICE_PAUSED&&
         state!=SERVICE_PAUSE_PENDING&&state!=SERVICE_CONTINUE_PENDING))return 0;
    return unsignedField(*status,"processId");
}
inline json packageIdentity(HANDLE process) {
    using QueryFn=LONG(WINAPI*)(HANDLE,UINT32*,PWSTR);
    const auto kernel=GetModuleHandleW(L"kernel32.dll");
    const auto query=kernel?reinterpret_cast<QueryFn>(GetProcAddress(kernel,"GetPackageFullName")):nullptr;
    if(!query)return readOnly::skipped("package-api-unavailable");
    UINT32 chars=0;LONG code=query(process,&chars,nullptr);
    json out={{"queried",true},{"known",false},{"returnCode",code},
        {"fullName",nullptr},{"source","GetPackageFullName(process-handle)"},
        {"officialAboutVersionProven",false}};
    if(code!=ERROR_INSUFFICIENT_BUFFER)return out;
    if(!chars||chars>4096){out["status"]="package-name-character-limit";return out;}
    std::vector<wchar_t> buffer(chars,wchar_t(0xffff));
    const auto capacity=chars;code=query(process,&chars,buffer.data());out["returnCode"]=code;
    if(code!=ERROR_SUCCESS)return out;
    if(chars<2||chars>capacity||buffer[chars-1]!=0){out["status"]="invalid-package-name";return out;}
    const auto end=std::find(buffer.begin(),buffer.begin()+chars,wchar_t(0));
    if(end!=buffer.begin()+chars-1){out["status"]="invalid-package-name-length";return out;}
    auto encoded=readOnly::utf8Path(std::wstring(buffer.data(),chars-1));
    out["known"]=encoded.value("known",false);out["encoding"]=encoded;
    if(out["known"].get<bool>())out["fullName"]=encoded.at("value");
    return out;
}
struct NativeOps {
    readOnly::VersionApi version;
    ULONGLONG now() const noexcept { return GetTickCount64(); }
    json process(DWORD pid,ULONGLONG begin) {
        json out={{"pid",pid},{"birthFileTime",readOnly::skipped("process-not-open")},
            {"imagePath",readOnly::skipped("process-not-open")},
            {"fileVersion",readOnly::skipped("image-path-unavailable")},
            {"packageIdentity",readOnly::skipped("process-not-open")},
            {"snapshotToHandleIdentityProven",false},{"keyHandlerProven",false}};
        if(expired(begin)){out["processOpen"]=readOnly::skipped("export-checkpoint-budget");return out;}
        SetLastError(ERROR_SUCCESS);
        readOnly::Handle handle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));
        out["processOpen"]=readOnly::queryResult(static_cast<bool>(handle),handle?ERROR_SUCCESS:GetLastError());
        if(!handle)return out;
        out["birthFileTime"]=readOnly::birth(handle.value);
        std::array<wchar_t,readOnly::maxImagePathChars> buffer{};DWORD chars=static_cast<DWORD>(buffer.size());
        SetLastError(ERROR_SUCCESS);
        const bool ok=QueryFullProcessImageNameW(handle.value,0,buffer.data(),&chars)!=FALSE;
        const auto error=ok?ERROR_SUCCESS:GetLastError();
        out["imagePath"]=readOnly::queryResult(ok,error);
        if(ok&&chars&&chars<buffer.size()) {
            const std::wstring path(buffer.data(),chars);out["imagePath"]=readOnly::utf8Path(path);
            if(!expired(begin)) {
                out["fileVersion"]=readOnly::fileVersion(version,path);
                out["fileVersion"]["officialAboutVersionProven"]=false;
            }else out["fileVersion"]=readOnly::skipped("export-checkpoint-budget");
        }else if(ok){out["imagePath"]["known"]=false;out["imagePath"]["status"]="invalid-image-path-length";}
        out["packageIdentity"]=expired(begin)?readOnly::skipped("export-checkpoint-budget"):packageIdentity(handle.value);
        return out;
    }
    json services(ULONGLONG begin) {
        json out={{"rows",json::array()},{"complete",false},{"serviceToProcessIdentityProven",false}};
        SetLastError(ERROR_SUCCESS);
        readOnly::ServiceHandle manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));
        out["managerOpen"]=readOnly::queryResult(manager.value!=nullptr,manager.value?ERROR_SUCCESS:GetLastError());
        for(const auto* name:serviceNames) {
            json row={{"name",readOnly::asciiName(name)},{"serviceOpenAccess","SERVICE_QUERY_STATUS"}};
            if(expired(begin))row["statusOpen"]=readOnly::skipped("export-checkpoint-budget");
            else if(!manager.value)row["statusOpen"]=readOnly::skipped("scm-open-unavailable");
            else readOnly::serviceStatus(manager.value,name,row);
            const DWORD pid=serviceProcessId(row);
            row["observedProcess"]=pid?process(pid,begin):json(readOnly::skipped("no-valid-user-service-pid"));
            out["rows"].push_back(std::move(row));
        }
        out["complete"]=manager.value!=nullptr;
        for(const auto& row:out["rows"])if(!row.contains("status")||!row["status"].value("known",false))out["complete"]=false;
        return out;
    }
    json helpers(ULONGLONG begin) {
        json out={{"rows",json::array()},{"enumerationComplete",false},
            {"selection","exact UserSessionHelper basename;publisher-unverified;not-all-ASUS-processes"},
            {"installationAbsenceProven",false}};
        SetLastError(ERROR_SUCCESS);
        readOnly::Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
        out["snapshot"]=readOnly::queryResult(static_cast<bool>(snapshot),snapshot?ERROR_SUCCESS:GetLastError());
        if(!snapshot)return out;
        PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);SetLastError(ERROR_SUCCESS);
        BOOL available=Process32FirstW(snapshot.value,&entry);DWORD error=available?ERROR_SUCCESS:GetLastError();DWORD visited=0;
        while(available) {
            if(visited>=maxProcessEntries){out["stopReason"]="process-entry-limit";break;}
            if(expired(begin)){out["stopReason"]="export-checkpoint-budget";break;}
            ++visited;
            if(readOnly::sameExecutable(entry.szExeFile,helperName)) {
                if(out["rows"].size()>=maxHelperProcesses){out["stopReason"]="matching-process-limit";break;}
                auto row=process(entry.th32ProcessID,begin);row["snapshotName"]=readOnly::asciiName(helperName);
                out["rows"].push_back(std::move(row));
            }
            entry.dwSize=sizeof(entry);SetLastError(ERROR_SUCCESS);
            available=Process32NextW(snapshot.value,&entry);error=available?ERROR_SUCCESS:GetLastError();
        }
        if(!available){out["enumerationComplete"]=error==ERROR_NO_MORE_FILES;out["enumerationWin32Error"]=error;
            out["stopReason"]=error==ERROR_NO_MORE_FILES?"snapshot-end":"enumeration-failed";}
        out["entriesVisited"]=visited;return out;
    }
};
template<class Ops> json collect(bool detailed,bool knownRog,Ops& ops) {
    readOnly::LastErrorScope restore;
    json out={{"schema","ymcc-rog-official-key-environment/1"},{"readOnly",true},
        {"detailEnabled",detailed},{"knownRogFamily",knownRog},
        {"gateReason",!detailed?"virtual-detail-disabled":!knownRog?"not-rog-family":"enabled"},
        {"services",readOnly::skipped("gate-disabled")},{"helpers",readOnly::skipped("gate-disabled")},
        {"buttonActionsReadback",nullptr},{"defaultActionSuppressionProven",false},
        {"keyHandlerProven",false},{"firmwareMappingReadback",nullptr},
        {"budget",{{"checkpointMs",checkpointMs},{"maxServiceNames",serviceNames.size()},
            {"maxProcessEntries",maxProcessEntries},{"maxHelperProcesses",maxHelperProcesses},
            {"hardIoTimeout",false}}}};
    if(!detailed||!knownRog)return out;
    const auto begin=ops.now();out["services"]=ops.services(begin);
    if(ops.now()-begin<checkpointMs)out["helpers"]=ops.helpers(begin);
    else out["helpers"]=readOnly::skipped("export-checkpoint-budget");
    out["elapsedMs"]=ops.now()-begin;
    return out;
}
} // namespace ymcc::rogOfficialKeyEvidence
