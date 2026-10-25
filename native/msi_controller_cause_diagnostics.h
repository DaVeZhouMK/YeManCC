#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <winsvc.h>
#include <hidsdi.h>
#include <array>
#include <mutex>
#include <string>
#include <vector>
#include <json.hpp>
#include "msi_official_policy_metadata.h"

namespace ymcc::msi::cause {
using nlohmann::json;
inline std::wstring fold(std::wstring s){for(auto& c:s)if(c>=L'A'&&c<=L'Z')c=wchar_t(c-L'A'+L'a');return s;}
inline std::string utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);if(n<=0)return {};std::string r(size_t(n),'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),r.data(),n,nullptr,nullptr);return r;}
inline bool ctwActiveProcess(const std::wstring& s){const auto n=fold(s);return n==L"msi_center_m_server.exe"||n==L"msi center m.exe"||n==L"mcmosdinfo.exe"||n==L"msi center osd info.exe"||n==L"msi_center_m_launcher.exe"||n==L"mongmode.exe";}
inline bool relevantProcess(const std::wstring& s){
    const auto name=fold(s);
    // The pinned API_AiEngine host was missing from the old inventory filter.
    // Candidate presence is NOT loaded-module / live-policy / mode-writer proof.
    // Do not extend CTW's independent active-process rule or any control gate.
    return ctwActiveProcess(s)||name==L"msi_center_m_server_controlmode.exe"||
        name==L"gamebar_widget.exe"||name==L"msi_center_m_server_userscenario.exe";
}
struct Handle {HANDLE h=INVALID_HANDLE_VALUE;~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
struct ServiceHandle {SC_HANDLE h=nullptr;~ServiceHandle(){if(h)CloseServiceHandle(h);}};
inline json officialStack(){
    const auto begin=GetTickCount64();DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
    json rows=json::array();bool processComplete=false,activeProcess=false;DWORD processError=0;
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};
    if(snapshot.h==INVALID_HANDLE_VALUE)processError=GetLastError();else{
        PROCESSENTRY32W p{};p.dwSize=sizeof(p);BOOL more=Process32FirstW(snapshot.h,&p);unsigned scanned=0;
        while(more&&scanned++<4096){
            if(GetTickCount64()-begin>100){processError=WAIT_TIMEOUT;break;}
            if(relevantProcess(p.szExeFile)){
                const bool active=ctwActiveProcess(p.szExeFile);activeProcess|=active;
                json row={{"pid",p.th32ProcessID},{"exe",utf8(p.szExeFile)},{"foreground",p.th32ProcessID==foregroundPid},{"ctwActiveRule",active},{"modeWriterProven",false}};
                Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,p.th32ProcessID)};
                if(process.h){FILETIME created{},exit{},kernel{},user{};const bool gotTime=GetProcessTimes(process.h,&created,&exit,&kernel,&user)!=FALSE;row["creationTimeKnown"]=gotTime;if(gotTime)row["creationTime100ns"]=(uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;
                    wchar_t image[1024]{};DWORD n=1024;const bool gotPath=QueryFullProcessImageNameW(process.h,0,image,&n)!=FALSE;row["imagePathKnown"]=gotPath;if(gotPath)row["imagePath"]=utf8(std::wstring(image,n));else row["imagePathError"]=GetLastError();
                }else{row["identityQueryError"]=GetLastError();row["creationTimeKnown"]=false;}
                rows.push_back(std::move(row));if(rows.size()>=24){processError=ERROR_MORE_DATA;break;}
            }
            more=Process32NextW(snapshot.h,&p);
        }
        if(!more){processError=GetLastError();processComplete=processError==ERROR_NO_MORE_FILES;}
        if(scanned>=4096&&more)processError=ERROR_MORE_DATA;
    }
    json service={{"displayName","MSI Foundation Service"},{"known",false}};bool serviceRunning=false;
    if(GetTickCount64()-begin<=100){
        ServiceHandle scm{OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT)};
        if(!scm.h)service["error"]=GetLastError();else{
            wchar_t name[256]{};DWORD n=256;const bool resolved=GetServiceKeyNameW(scm.h,L"MSI Foundation Service",name,&n)!=FALSE;
            const wchar_t* key=resolved?name:L"MSI Foundation Service";service["serviceKey"]=utf8(key);service["displayNameResolved"]=resolved;
            ServiceHandle sh{OpenServiceW(scm.h,key,SERVICE_QUERY_STATUS)};
            if(!sh.h){const DWORD err=GetLastError();service["error"]=err;service["known"]=err==ERROR_SERVICE_DOES_NOT_EXIST;if(err==ERROR_SERVICE_DOES_NOT_EXIST)service["present"]=false;}
            else{SERVICE_STATUS_PROCESS status{};DWORD bytes=0;bool ok=QueryServiceStatusEx(sh.h,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&status),sizeof(status),&bytes)!=FALSE;service["known"]=ok;service["present"]=true;if(ok){service["state"]=status.dwCurrentState;service["pid"]=status.dwProcessId;serviceRunning=status.dwCurrentState==SERVICE_RUNNING;service["running"]=serviceRunning;}else service["error"]=GetLastError();}
        }
    }else service["reason"]="soft-budget-spent-before-service-query";
    json policySnapshot={{"schema",1},{"complete",false},{"reason","process-inventory-soft-budget-spent"},
        {"fields",json::array()},{"registryMutated",false},{"firmwareModeVerified",false},{"modeWriterProven",false}};
    const auto policyElapsed=GetTickCount64()-begin;
    if(policyElapsed<100){
        const auto remaining=100-policyElapsed;
        try{policySnapshot=ymcc::msi::policyMetadata::windowsSnapshot(remaining<40?remaining:40);}
        catch(...){policySnapshot["reason"]="read-only-policy-snapshot-exception";}
    }
    // CTW also checks two enabled scheduled tasks. We deliberately do NOT invoke
    // Task Scheduler/PowerShell here, so absence of processes is never 'inactive'.
    return {{"beginTick",begin},{"endTick",GetTickCount64()},{"elapsedMs",GetTickCount64()-begin},{"foregroundPid",foregroundPid},{"processes",rows},{"processEnumerationComplete",processComplete},{"processEnumerationError",processError},{"foundationService",service},{"officialPolicySnapshot",policySnapshot},
        {"ctwActiveObserved",activeProcess||serviceRunning},{"ctwInactiveVerified",false},{"ctwAssessment",activeProcess||serviceRunning?"active-rule-observed":"unknown-tasks-not-observed"},
        {"scheduledTasks",{{"checked",false},{"names",json::array({"MSI_Center_M_Server","MSI_Center_M_Updater"})}}},{"modeWriterProven",false},{"processInventoryFilterVersion",2},{"userScenarioCandidateCovered",true},{"officialPolicySettingsRead",policySnapshot.value("complete",false)},{"officialPolicyReadAttempts",policySnapshot.value("readAttempts",0U)},{"officialPolicyReadMeaning","bounded configuration metadata only; completeness is not installed-version or valid-mode proof"},{"loadedAiEngineIdentityVerified",false},{"scope","process/service presence and foreground identity only; never control authority"},{"budgetMeaning","100ms soft budget between APIs; no in-flight API hard cancellation"}};
}
struct ProbeGate {
    bool diagnosticBuild=false, detailed=false, knownBoard=false, exitFence=false,powerReady=false;
    bool routeArmed=false,sourceValid=false,returnOwed=false,samePhysicalLocation=false;
    uint64_t epoch=0,anchorEpoch=0,power=0,anchorPower=0;
};
inline bool probeAllowed(const ProbeGate& f){return f.diagnosticBuild&&f.detailed&&f.knownBoard&&!f.exitFence&&f.powerReady&&f.routeArmed&&!f.sourceValid&&f.returnOwed&&f.samePhysicalLocation&&f.epoch!=0&&f.epoch==f.anchorEpoch&&f.power!=0&&f.power==f.anchorPower;}
inline json response(const BYTE* p,size_t n){
    json raw=json::array();for(size_t i=0;i<n&&i<16;++i)raw.push_back(p[i]);
    const bool ctw=n>=6&&p[4]==0x27,valid=ctw&&(p[5]==1||p[5]==2||p[5]==4);
    // 2026-10-11 20:53:54.960 real G3E receipt: response report ID is
    // 0x10, NOT the query/output report ID 0x0F. Keep request echoes rejected.
    const bool strict=valid&&p[0]==0x10&&p[3]==0x3c;
    json r={{"responseParserVersion",2},{"responseReportId",n?p[0]:0},{"expectedResponseReportId",16},{"length",n},{"first16",raw},{"ctwResponseMatched",ctw},{"modeValueRecognized",valid},{"strictHeaderMatched",strict},{"modeKnown",strict},{"deviceModeVerified",false},{"modeWriterProven",false},{"responseCorrelation","protocol/time/interface only; no nonce, stale/unsolicited response not ruled out"}};
    if(ctw)r["reportedMode"]=p[5];if(strict)r["observedMode"]=p[5];return r;
}

// One retained allocation owns handle/event/OVERLAPPED/buffer until actual OS
// completion. A timeout never frees memory still referenced by Read/WriteFile.
// It is intentionally process-lifetime storage, not a static-destructor hazard.
struct Io {HANDLE handle=INVALID_HANDLE_VALUE,event=nullptr;OVERLAPPED ov{};std::array<BYTE,512> buffer{};bool pending=false;~Io(){if(event)CloseHandle(event);if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}};
inline Io*& retained(){static Io* value=nullptr;return value;}
inline std::mutex& ioMutex(){static std::mutex value;return value;}

#ifdef YMCC_FAULT_INJECT
static bool (*g_completionForTest)(Io*,DWORD&,DWORD&)=nullptr;
#endif
inline bool completed(Io* p,DWORD& bytes,DWORD& error){
#ifdef YMCC_FAULT_INJECT
    if(g_completionForTest)return g_completionForTest(p,bytes,error);
#endif
if(GetOverlappedResult(p->handle,&p->ov,&bytes,FALSE)){p->pending=false;error=0;return true;}error=GetLastError();if(error!=ERROR_IO_INCOMPLETE){p->pending=false;return true;}return false;}
struct QueryOwner {Io* io;~QueryOwner(){if(io->pending)retained()=io;else delete io;}};
inline bool reap(){auto*& p=retained();if(!p)return true;DWORD bytes=0,error=0;if(!completed(p,bytes,error))return false;delete p;p=nullptr;return true;}
inline bool transfer(Io& io,bool write,DWORD length,DWORD waitMs,DWORD& bytes,DWORD& error,bool& cancelled){
    ResetEvent(io.event);io.ov={};io.ov.hEvent=io.event;io.pending=true;bytes=0;
    const BOOL ok=write?WriteFile(io.handle,io.buffer.data(),length,&bytes,&io.ov):ReadFile(io.handle,io.buffer.data(),length,&bytes,&io.ov);
    if(ok){io.pending=false;error=0;return true;}
    error=GetLastError();if(error!=ERROR_IO_PENDING){io.pending=false;return false;}
    const DWORD waited=WaitForSingleObject(io.event,waitMs);
    if(waited==WAIT_OBJECT_0){completed(&io,bytes,error);return !io.pending&&error==0;}
    error=waited==WAIT_TIMEOUT?WAIT_TIMEOUT:GetLastError();const DWORD waitError=error;
    cancelled=CancelIoEx(io.handle,&io.ov)!=FALSE;WaitForSingleObject(io.event,10);DWORD reapError=0;completed(&io,bytes,reapError);error=waitError;return false;
}
inline json queryMode(const std::wstring& path){
    json r={{"queryCommand",json::array({15,0,0,60,38,0,0})},{"modeSwitchIssued",false},{"modeKnown",false},{"deviceModeVerified",false},{"modeWriterProven",false}};
    std::unique_lock<std::mutex> lock(ioMutex(),std::try_to_lock);if(!lock.owns_lock()){r["reason"]="diagnostic-query-busy";return r;}
    if(!reap()){r["reason"]="previous-query-cancellation-not-reaped";r["ioRetained"]=true;return r;}
    auto* io=new Io;QueryOwner owner{io};io->handle=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OVERLAPPED,nullptr);
    if(io->handle==INVALID_HANDLE_VALUE){r["reason"]="command-open-failed";r["error"]=GetLastError();return r;}
    HIDD_ATTRIBUTES attr{};attr.Size=sizeof(attr);PHIDP_PREPARSED_DATA prep=nullptr;HIDP_CAPS caps{};
    const bool attributes=HidD_GetAttributes(io->handle,&attr)!=FALSE;
    const bool capabilities=HidD_GetPreparsedData(io->handle,&prep)!=FALSE&&HidP_GetCaps(prep,&caps)==HIDP_STATUS_SUCCESS;if(prep)HidD_FreePreparsedData(prep);
    const bool endpoint=attributes&&capabilities&&attr.VendorID==0x0db0&&((attr.ProductID==0x1901&&caps.UsagePage==0xffa0&&caps.Usage==1)||(attr.ProductID==0x1902&&caps.UsagePage==0xfff0&&caps.Usage==0x40));
    r["endpointRevalidated"]=endpoint;r["productId"]=attr.ProductID;r["usagePage"]=caps.UsagePage;r["usage"]=caps.Usage;r["inputReportByteLength"]=caps.InputReportByteLength;
    if(!endpoint||caps.OutputReportByteLength!=64||caps.InputReportByteLength<6||caps.InputReportByteLength>io->buffer.size()){r["reason"]="command-capability-unverified";return r;}
    io->event=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!io->event){r["reason"]="query-event-failed";r["error"]=GetLastError();return r;}
    const auto begin=GetTickCount64();io->buffer[0]=15;io->buffer[3]=60;io->buffer[4]=38;
    DWORD bytes=0,error=0;bool cancelled=false;
    r["queryWriteAttempted"]=true;const bool sent=transfer(*io,true,64,50,bytes,error,cancelled)&&bytes==64;r["queryWriteOk"]=sent;r["queryWritten"]=bytes;r["queryWriteError"]=error;
    json replies=json::array();if(sent){for(unsigned attempt=0;attempt<4;++attempt){const auto spent=GetTickCount64()-begin;if(spent>=250)break;io->buffer.fill(0);const bool got=transfer(*io,false,caps.InputReportByteLength,DWORD(250-spent),bytes,error,cancelled);if(!got){r["readError"]=error;break;}auto parsed=response(io->buffer.data(),bytes);replies.push_back(parsed);if(parsed.value("modeKnown",false)){r["modeKnown"]=true;r["observedMode"]=parsed["observedMode"];break;}}}
    r["replies"]=replies;r["elapsedMs"]=GetTickCount64()-begin;r["cancelAttempted"]=cancelled;r["ioRetained"]=io->pending;r["reason"]=r.value("modeKnown",false)?"protocol-matched-mode-observation":"no-strict-mode-response";
    return r;
}
} // namespace ymcc::msi::cause
