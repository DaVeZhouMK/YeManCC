#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
namespace fs=std::filesystem;
#include "steam_client_action_channel_fixtures/native_resource_observation.inc"
namespace HostWorkerFifo {
static fs::path g_worker,g_labRoot,g_dataRoot;
static unsigned workerCalls=0,workerLaunches=0,unexpectedPorts=0;
static std::vector<json> childStarts;
}
static std::string observedUtf8(const std::wstring& value){
    if(value.empty())return {};const auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);
    if(!n)throw std::runtime_error("Observed image UTF-8 conversion");std::string out(n,'\0');
    if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),int(value.size()),out.data(),n,nullptr,nullptr)!=n)throw std::runtime_error("Observed image UTF-8 conversion");return out;
}
static BOOL WINAPI hostChainCreatePipe(PHANDLE read,PHANDLE write,LPSECURITY_ATTRIBUTES attrs,DWORD size){
    const auto ok=::CreatePipe(read,write,attrs,size);const auto error=GetLastError();if(ok){nativeTrackHandle(*read,9);nativeTrackHandle(*write,9);}SetLastError(error);return ok;
}
static HANDLE WINAPI hostChainCreateJob(LPSECURITY_ATTRIBUTES attrs,LPCWSTR name){const auto h=::CreateJobObjectW(attrs,name);const auto error=GetLastError();nativeTrackHandle(h,12);SetLastError(error);return h;}
static BOOL WINAPI hostChainCreateProcess(LPCWSTR application,LPWSTR command,LPSECURITY_ATTRIBUTES processSecurity,LPSECURITY_ATTRIBUTES threadSecurity,BOOL inherit,DWORD flags,LPVOID env,LPCWSTR directory,LPSTARTUPINFOW startup,LPPROCESS_INFORMATION process){
    using namespace HostWorkerFifo;++workerCalls;
    if(!application||fs::path(application).lexically_normal()!=g_worker.lexically_normal()||!inherit||!(flags&CREATE_SUSPENDED)||!(flags&EXTENDED_STARTUPINFO_PRESENT)){
        ++unexpectedPorts;SetLastError(ERROR_ACCESS_DENIED);return FALSE;
    }
    const auto ok=::CreateProcessW(application,command,processSecurity,threadSecurity,inherit,flags,env,directory,startup,process);const auto error=GetLastError();
    if(ok){
        ++workerLaunches;nativeTrackHandle(process->hProcess,10);nativeTrackHandle(process->hThread,11);
        FILETIME birth{},exit{},kernel{},user{};wchar_t image[32768]{};DWORD length=32768;BOOL inJob=FALSE;
        const bool actual=GetProcessTimes(process->hProcess,&birth,&exit,&kernel,&user)&&QueryFullProcessImageNameW(process->hProcess,0,image,&length)&&IsProcessInJob(process->hProcess,nullptr,&inJob);
        childStarts.push_back({{"pid",process->dwProcessId},{"birth",std::to_string((ULONGLONG(birth.dwHighDateTime)<<32)|birth.dwLowDateTime)},
            {"application",observedUtf8(application)},{"actualImage",actual?json(observedUtf8(std::wstring(image,length))):json(nullptr)},
            {"actualIdentityObserved",actual},{"alreadyInParentJobBeforeResume",inJob!=FALSE},{"createdSuspended",bool(flags&CREATE_SUSPENDED)},
            {"extendedHandleList",bool(flags&EXTENDED_STARTUPINFO_PRESENT)}});
        if(!actual||!inJob||GetProcessId(process->hProcess)!=process->dwProcessId)++unexpectedPorts;
    }
    SetLastError(error);return ok;
}
#define OpenProcess nativeObserveOpenProcess
#define OpenProcessToken nativeObserveOpenProcessToken
#define CreateFileW nativeObserveCreateFile
#define CreateNamedPipeW nativeObserveCreatePipe
#define OpenFileMappingW nativeObserveOpenMapping
#define CreateFileMappingW nativeObserveCreateMapping
#define MapViewOfFile nativeObserveMap
#define UnmapViewOfFile nativeObserveUnmap
#define CreateWindowExW nativeObserveCreateWindow
#define DestroyWindow nativeObserveDestroyWindow
#define CloseHandle nativeObserveClose
#define CreatePipe hostChainCreatePipe
#define CreateJobObjectW hostChainCreateJob
#define CreateProcessW hostChainCreateProcess
#include "steam_information_client.h"
#define CreateEventW nativeObserveCreateEvent
#define OpenEventW nativeObserveOpenEvent
#include "steam_client_action_channel.h"
#undef OpenEventW
#undef CreateEventW
#include "process_efficiency.h"
#include "custom-steam-library/custom_steam_library_paths.h"
#include "custom-steam-library/custom_steam_library_config.h"
#include "custom-steam-library/custom_steam_library_plan_receipt.h"
#include "custom_steam_library_facts_ipc_processes.inc"

namespace ymcc::steamaction {
static unsigned typedChainCalls=0;
static Outcome typedChainOutcome=Outcome::Unknown;
static json typedChainRequest;
static std::wstring typedChainReturnedEvent;
static unsigned typedChainReturnSignals=0;
static Outcome typedChainShutdown(DWORD pid,std::uint64_t birth,ULONGLONG deadline,
    std::uint64_t generation,std::uint64_t sequence,std::uint64_t serial,const std::function<bool()>& current){
    ++typedChainCalls;
    typedChainRequest={{"pid",pid},{"birth",std::to_string(birth)},{"powerGeneration",std::to_string(generation)},
        {"powerSequence",std::to_string(sequence)},{"powerSerial",std::to_string(serial)},{"deadline",deadline}};
    typedChainOutcome=shutdown(pid,birth,deadline,generation,sequence,serial,current);
    if(!typedChainReturnedEvent.empty()){
        const auto event=nativeObserveOpenEvent(EVENT_MODIFY_STATE,FALSE,typedChainReturnedEvent.c_str());
        if(!event)throw std::runtime_error("Typed return observation event unavailable");
        const bool signalled=SetEvent(event)!=FALSE;
        const bool closed=nativeObserveClose(event)!=FALSE;
        if(!signalled||!closed)throw std::runtime_error("Typed return observation event not returned");
        ++typedChainReturnSignals;
    }
    return typedChainOutcome;
}
}
namespace HostWorkerFifo {
static json typedSteamClient;
static std::mutex g_steamOperationMutex,g_activeProcessMutex,g_workerProcessJobMutex;
static std::atomic<bool> g_taskRunning{false},g_cancelRequested{false},g_shuttingDown{false};
static HANDLE g_activeProcess=nullptr,g_workerProcessJob=nullptr;
static HWND g_window=nullptr;
static bool g_updateHealthOnly=false;
static int g_artworkJobs=0,g_identitySearchJobs=0;
static constexpr UINT WM_WORKSPACE_WRITES_DRAINED=WM_APP+0x46;
static json selectedTarget=json::object();
static json steamTargetSnapshot(){return selectedTarget;}
static std::string currentCase;
static bool observerMode(){return currentCase.rfind("chain-observer-",0)==0;}
static bool typedMode(){return currentCase.rfind("chain-typed-",0)==0||observerMode();}
static unsigned launches=0,drained=0,snapshots=0,previews=0,completionResults=0;
static std::vector<json> replies,progress,executions;
static std::mutex resultMutex,threadMutex;
static std::condition_variable gate,finishedChanged;
static bool released=false,finished=false;
static std::thread fifoWorker;
static void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void setSteamTarget(const json& args){selectedTarget=args;}
static json workspaceSnapshot(bool refresh,const json&){need(!refresh,"Unexpected refresh snapshot");++snapshots;return {{"steamPlan",{{"mode","stale-fixture-plan"}}},{"steamPlanSource","stale"},{"steamPlanError","stale"}};}
static void attachArtworkPreviews(json& completed){++previews;need(completed.is_object(),"Completion is not a document");completed["fixturePreviewObserved"]=true;}
static json commitToAllSteamAccounts(const json&){++unexpectedPorts;throw std::runtime_error("Unselected all-account port");}
static json refreshArtworkForAllSteamAccounts(const json&){++unexpectedPorts;throw std::runtime_error("Unselected artwork port");}
static void postWebMessageNoThrow(json value){std::lock_guard lock(resultMutex);replies.push_back(std::move(value));}
static void postPipelineEvent(const std::string&,json value){std::lock_guard lock(resultMutex);progress.push_back(std::move(value));}
struct AsyncResult {json message;bool refreshWatchers=false;};
static bool enqueueWorkspaceResult(std::unique_ptr<AsyncResult> result)noexcept{try{postWebMessageNoThrow(std::move(result->message));return true;}catch(...){++unexpectedPorts;return false;}}
static BOOL WINAPI observedPost(HWND window,UINT message,WPARAM,LPARAM){if(window!=g_window||message!=WM_WORKSPACE_WRITES_DRAINED){++unexpectedPorts;return FALSE;}++drained;return TRUE;}
[[noreturn]] static void unselected(){++unexpectedPorts;throw std::logic_error("Unselected UI or network path");}
static void acceptWorkspaceRefreshProtocol(const json&){unselected();}
static std::optional<json> admitHostDataRootCommand(const std::string&){return {};}
static void appendUiError(const json&){unselected();}
static bool cancelBackgroundJobs(int,const std::string& = {}){unselected();}
static std::string backgroundExecutableKey(const std::string&){unselected();}
static void startArtworkTask(uint64_t,std::string,json){unselected();}
static void startNetworkWakeTask(uint64_t){unselected();}
static bool requestTaskCancellation(){unselected();}
static json startIdentitySearchRequest(const json&){unselected();}
static json executeWorkspaceCommand(const std::string&,const json&);
static void launchHostBackgroundTask(std::function<void()> task){
    ++launches;need(!fifoWorker.joinable(),"Second simultaneous Host FIFO thread");
    fifoWorker=std::thread([task=std::move(task)]{
        {std::unique_lock lock(threadMutex);if(!gate.wait_for(lock,std::chrono::seconds(5),[]{return released;})){finished=true;finishedChanged.notify_all();return;}}
        try{task();}catch(...){++unexpectedPorts;}
        {std::lock_guard lock(threadMutex);finished=true;}finishedChanged.notify_all();
    });
}
#include "production-library-worker-chain.h"
#define PostMessageW observedPost
#define shutdown typedChainShutdown
#include "production-library-fifo.h"
#undef shutdown
#undef PostMessageW
#include "production-library-no-restart.h"
static json executeWorkspaceCommand(const std::string& command,const json& args){
    executions.push_back({{"command",command},{"arguments",args}});
    need(command=="commit","Unselected Host command");
    const bool fault=args.value("fixtureExit",false);
    need(SetEnvironmentVariableW(L"YMCC_CSL_CHAIN_CLI_EXIT",fault?L"1":nullptr)!=FALSE,"Host fixture fault environment");
    try{auto result=productionLibraryCommand(command,args);SetEnvironmentVariableW(L"YMCC_CSL_CHAIN_CLI_EXIT",nullptr);++completionResults;return result;}
    catch(...){SetEnvironmentVariableW(L"YMCC_CSL_CHAIN_CLI_EXIT",nullptr);throw;}
}
static void releaseAndJoin(){
    {std::lock_guard lock(threadMutex);released=true;}gate.notify_all();
    if(!fifoWorker.joinable())return;
    {std::unique_lock lock(threadMutex);if(!finishedChanged.wait_for(lock,std::chrono::seconds(10),[]{return finished;})){
        g_cancelRequested=true;need(finishedChanged.wait_for(lock,std::chrono::seconds(5),[]{return finished;}),"Host FIFO did not join after cancellation");}}
    fifoWorker.join();
}
static json request(bool fault=false){auto out=json{{"selectionMode","explicit-game-directories"},{"accountId","19627"},
    {"steamRoot",toUtf8((g_dataRoot.parent_path()/L"Steam").wstring())},{"selectedGameDirectories",json::array({toUtf8((g_dataRoot.parent_path()/L"Game").wstring())})},
    {"forceCloseSteam",false},{"fixtureExit",fault}};if(typedMode()){need(typedSteamClient.is_object(),"Typed seed has no Steam client tuple");out["forceCloseSteam"]=true;out["steamClient"]=typedSteamClient;}return out;}
static json resources(){return {{"returned",nativeObservedResourcesReturned()},{"errors",nativeResourceErrors},
    {"handlesAcquired",nativeHandleAcquires},{"handlesReleased",nativeHandleReleases},{"handlesOutstanding",nativeObservedHandles.size()},
    {"viewsAcquired",nativeViewAcquires},{"viewsReleased",nativeViewReleases},{"viewsOutstanding",nativeObservedViews.size()},
    {"windowsAcquired",nativeWindowAcquires},{"windowsReleased",nativeWindowReleases},{"windowsOutstanding",nativeObservedWindows.size()}};}
static void expectReply(uint64_t id,bool ok){
    const auto found=std::find_if(replies.begin(),replies.end(),[&](const auto& value){return value.at("id")==id;});
    need(found!=replies.end()&&found->value("ok",false)==ok,"Host FIFO waiter result mismatch");
    if(ok){const auto& response=found->at("result");need(response.at("steamPlan").value("mode",std::string{})=="steam-library-add-commit"&&
        response.value("steamPlanSource",std::string{})=="verified-steam-add-completion"&&response.at("steamPlan").value("fixturePreviewObserved",false),"Host returned stale or unverified completion");}
}
static int entry(int argc,wchar_t** argv){
    if(argc!=4||std::wstring(argv[1])!=L"--host-worker-chain-client")return 2;
    const auto root=fs::path(ymcc::steaminfo::utf16(LibraryFactsIpc::ExclusiveJsonRead(fs::path(argv[3]).parent_path()/L"seed.json").at("root").get<std::string>()));
    g_labRoot=root/L"CustomSteamLibrary";g_worker=g_labRoot/L"SteamArtworkLab.exe";g_dataRoot=root/L"transaction"/L"data";
    const auto output=fs::absolute(argv[3]);need(output.parent_path()==root,"Host result escaped owned package");
    currentCase=ymcc::steaminfo::utf8(argv[2]);
    if(typedMode()){
        const auto seed=LibraryFactsIpc::ExclusiveJsonRead(root/L"seed.json");typedSteamClient=seed.at("steamClient");
        if(seed.contains("typedActionReturnedEvent")){
            wchar_t providerText[32]{};
            const auto length=GetEnvironmentVariableW(L"YMCC_CSL_CHAIN_PROVIDER_PID",providerText,32);
            need(length&&length<32&&std::wstring(providerText).find_first_not_of(L"0123456789")==std::wstring::npos,"Typed observation provider PID");
            const auto expected=L"Local\\YMCC.LibraryFacts.ActionReturned."+std::wstring(providerText);
            ymcc::steamaction::typedChainReturnedEvent=ymcc::steaminfo::utf16(seed.at("typedActionReturnedEvent").get<std::string>());
            need(ymcc::steamaction::typedChainReturnedEvent==expected,"Typed observation event escaped actual provider namespace");
        }
    }
    const std::vector<std::string> names={"chain-single-commit","chain-current-dedupe","chain-close-retains-commit","chain-final-unknown-preserves","chain-final-present-preserves","chain-nonzero-worker-continues","chain-typed-shutdown-commit","chain-typed-revoked-preserves","chain-typed-rejected-preserves","chain-typed-unknown-preserves","chain-typed-ack-cancel-unknown-preserves","chain-typed-ack-submitted-unknown-preserves","chain-observer-shutdown-commit","chain-observer-power-revoked-preserves"};
    if(std::find(names.begin(),names.end(),currentCase)==names.end())return 2;
    need(fs::is_regular_file(g_worker),"Actual Worker fixture missing");
    nativeObserveResourceBaseline();initializeWorkerProcessJob();need(g_workerProcessJob!=nullptr,"Original Host nested Job creation failed");
    std::string error;JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};bool accountingObserved=false;
    try{
        requireHostDataRootReady();
        if(observerMode()){
            beginCommand(101,"commit",request());
            need(g_acceptedWrites==1&&g_commandQueue.size()==1,"Observer FIFO accepted wrong write count");
            releaseAndJoin();const bool success=currentCase=="chain-observer-shutdown-commit";
            expectReply(101,success);
            need(ymcc::steamaction::typedChainCalls==(success?1u:0u),"Observer stale authority reached action or valid action lost");
            need(workerLaunches==(success?1u:0u)&&workerCalls==workerLaunches&&completionResults==(success?1u:0u),"Observer refusal reached Worker or success lost completion");
            if(success)need(ymcc::steamaction::typedChainOutcome==ymcc::steamaction::Outcome::Submitted,"Observer valid typed action was not submitted");
        }else if(typedMode()){
            beginCommand(101,"commit",request());
            need(g_acceptedWrites==1&&g_commandQueue.size()==1,"Typed FIFO did not accept exactly one writer");
            releaseAndJoin();
            const bool success=currentCase=="chain-typed-shutdown-commit";
            expectReply(101,success);
            need(ymcc::steamaction::typedChainCalls==1,"Typed shutdown was skipped or replayed");
            need((ymcc::steamaction::typedChainOutcome==ymcc::steamaction::Outcome::Submitted)==success,"Typed action outcome mismatch");
            if(currentCase=="chain-typed-unknown-preserves"||currentCase=="chain-typed-ack-cancel-unknown-preserves"||currentCase=="chain-typed-ack-submitted-unknown-preserves")need(ymcc::steamaction::typedChainOutcome==ymcc::steamaction::Outcome::Unknown,"Unknown was replaced by another outcome");
            need(workerLaunches==(success?1u:0u)&&workerCalls==workerLaunches&&completionResults==(success?1u:0u),"Typed rejection reached Worker or success lost completion");
        }else if(currentCase=="chain-nonzero-worker-continues"){
            beginCommand(101,"commit",request(true));beginCommand(102,"commit",request());
            need(g_acceptedWrites==2&&g_commandQueue.size()==2,"Distinct FIFO requests were merged");
            releaseAndJoin();expectReply(101,false);expectReply(102,true);need(workerLaunches==2&&completionResults==1,"Nonzero Worker blocked or replayed successor");
        }else{
            const auto args=request();beginCommand(101,"commit",args);
            if(currentCase=="chain-current-dedupe")beginCommand(102,"commit",args);
            if(currentCase=="chain-close-retains-commit"){g_closeAfterWrites=true;beginCommand(102,"commit",args);}
            need(g_acceptedWrites==1&&g_commandQueue.size()==1,"Host did not retain exactly one disk writer");
            releaseAndJoin();const bool ok=currentCase!="chain-final-unknown-preserves"&&currentCase!="chain-final-present-preserves";
            expectReply(101,ok);
            if(currentCase=="chain-current-dedupe")expectReply(102,true);
            if(currentCase=="chain-close-retains-commit"){expectReply(102,false);need(drained==1,"Close-retained commit did not publish drained");}
            need(workerLaunches==1&&completionResults==(ok?1u:0u),"Wrong number of actual Worker/completion calls");
        }
        need(!fifoWorker.joinable()&&!g_taskRunning&&g_acceptedWrites==0&&g_commandQueue.empty()&&g_commitWaiters.empty()&&g_activeProcess==nullptr,"Host FIFO or process owner was not returned");
        need(unexpectedPorts==0&&launches==1,"Unexpected port or duplicate FIFO worker");
        const auto accountingUntil=GetTickCount64()+2000;
        for(;;){
            need(QueryInformationJobObject(g_workerProcessJob,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr)!=FALSE,"Original Host Job accounting unavailable");
            accountingObserved=true;if(accounting.ActiveProcesses==0)break;
            need(GetTickCount64()<accountingUntil,"Original Host nested Job still owns a Worker");Sleep(2);
        }
    }catch(const std::exception& e){error=e.what();releaseAndJoin();}
    closeWorkerProcessJob();
    const auto observed=resources();bool passed=error.empty()&&observed.value("returned",false)&&g_workerProcessJob==nullptr;
    LibraryFactsIpc::ExclusiveJsonWrite(output,{{"passed",passed},{"name",currentCase},{"error",error},{"replies",replies},{"progress",progress},{"executions",executions},
        {"workerCalls",workerCalls},{"workerLaunches",workerLaunches},{"childStarts",childStarts},{"completionResults",completionResults},
        {"snapshots",snapshots},{"previews",previews},{"fifoLaunches",launches},{"drained",drained},{"acceptedWritesFinal",g_acceptedWrites.load()},
        {"queueEmpty",g_commandQueue.empty()},{"waitersEmpty",g_commitWaiters.empty()},{"workerJoined",!fifoWorker.joinable()},{"activeProcessCleared",g_activeProcess==nullptr},
        {"originalHostJobAccountingObserved",accountingObserved},{"originalHostJobActiveFinal",accounting.ActiveProcesses},{"originalHostJobProcesses",accounting.TotalProcesses},{"requestResources",observed},
        {"externalProcessAttempts",unexpectedPorts},{"networkCalls",0},{"actualHostFifo",true},{"actualHostRunProcess",true},{"actualCompletionReader",true},{"uiAndSnapshotControlled",true},
        {"typedShutdownCalls",ymcc::steamaction::typedChainCalls},{"typedShutdownOutcome",LONG(ymcc::steamaction::typedChainOutcome)},{"typedShutdownRequest",ymcc::steamaction::typedChainRequest},{"actualTypedClientExecuted",typedMode()},{"typedReturnObservationSignals",ymcc::steamaction::typedChainReturnSignals}});
    return passed?0:1;
}
}
int wmain(int argc,wchar_t** argv){try{return HostWorkerFifo::entry(argc,argv);}catch(const std::exception& e){std::cerr<<"HOST_CHAIN_FIXTURE_FAILED: "<<e.what()<<'\n';return 1;}}
