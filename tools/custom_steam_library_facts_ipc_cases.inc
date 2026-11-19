// Explicit variant of the original datachain consumer. Real client/serve IPC
// and the original file transaction run on an owned invisible desktop.
#include <shellapi.h>
#include <chrono>
#include <thread>
static unsigned factsIpcRegistryDenials=0;
// This selftest must never contend for the installed Steam source slot.
#pragma push_macro("CreateMutexW")
#undef CreateMutexW
static HANDLE WINAPI libraryFactsOwnedSourceMutex(LPSECURITY_ATTRIBUTES attributes,BOOL initialOwner,LPCWSTR requested){
    if(!requested||!std::wstring_view(requested).starts_with(L"Local\\YeManCC.SteamSource.v1.")){
        SetLastError(ERROR_INVALID_NAME);return nullptr;
    }
    const auto name=L"Local\\YMCCFixture.LibraryFacts.Source."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());
    return ::CreateMutexW(attributes,initialOwner,name.c_str());
}
#define CreateMutexW libraryFactsOwnedSourceMutex
#include "../native/steam_source_owner.h"
#undef CreateMutexW
#pragma pop_macro("CreateMutexW")
#ifdef YMCC_LIBRARY_OBSERVER_CHAIN
#ifndef YMCC_LIBRARY_TYPED_SHUTDOWN
#error OBSERVER_CHAIN requires TYPED_SHUTDOWN
#endif
#define YMCC_LIBRARY_OBSERVER_DECLARATIONS 1
#include "custom_steam_library_observer_chain.inc"
#undef YMCC_LIBRARY_OBSERVER_DECLARATIONS
#define RegGetValueW ChainObserver::presenceRegistry
#else
static LSTATUS WINAPI factsIpcNoRegistry(HKEY,LPCWSTR,LPCWSTR,DWORD,LPDWORD,PVOID,LPDWORD){++factsIpcRegistryDenials;return ERROR_FILE_NOT_FOUND;}
#define RegGetValueW factsIpcNoRegistry
#endif
#define OpenProcessToken nativeObserveOpenProcessToken
#define OpenFileMappingW nativeObserveOpenMapping
#define CreateFileMappingW nativeObserveCreateMapping
#define MapViewOfFile nativeObserveMap
#define UnmapViewOfFile nativeObserveUnmap
#define CreateWindowExW nativeObserveCreateWindow
#define DestroyWindow nativeObserveDestroyWindow
#include "../native/steam_information_producer.h"
#undef OpenProcessToken
#undef OpenFileMappingW
#undef CreateFileMappingW
#undef MapViewOfFile
#undef UnmapViewOfFile
#undef CreateWindowExW
#undef DestroyWindow

#undef RegGetValueW
#include "custom_steam_library_facts_ipc_processes.inc"
#ifdef YMCC_LIBRARY_TYPED_SHUTDOWN
#include "custom_steam_library_typed_shutdown.inc"
#endif
#ifdef YMCC_LIBRARY_OBSERVER_CHAIN
#include "custom_steam_library_observer_chain.inc"
#endif

namespace LibraryFactsIpc {
namespace si=ymcc::steaminfo;
static const std::vector<std::string> names={
    "ipc-absent-commits-real-files","ipc-present-first-preserves-original",
    "ipc-unknown-final-preserves-original","ipc-present-final-preserves-original",
    "ipc-duplicate-provider-preserves-original","ipc-wrong-client-image-preserves-original"};
static std::wstring modulePath(){std::wstring p(32768,L'\0');const auto n=GetModuleFileNameW(nullptr,p.data(),DWORD(p.size()));check(n&&n<p.size(),"IPC fixture module path");p.resize(n);return p;}
static void closeHandle(si::Handle& h){if(h.value){check(CloseHandle(h.value)!=FALSE,"IPC owned handle close");h.value=nullptr;}}
static DWORD handles(){DWORD count=0;check(GetProcessHandleCount(GetCurrentProcess(),&count)!=FALSE,"IPC actual handle count");return count;}
static bool known(const std::string& name){return std::find(names.begin(),names.end(),name)!=names.end();}
#ifdef YMCC_LIBRARY_HOST_CHAIN
static bool chainKnown(const std::string& name);
#endif
static bool within(const fs::path& child,const fs::path& parent){return pathWithin(fs::absolute(child),fs::absolute(parent));}
static json resourceReceipt(){return {{"returned",nativeObservedResourcesReturned()},{"errors",nativeResourceErrors},{"handlesAcquired",nativeHandleAcquires},{"handlesReleased",nativeHandleReleases},{"viewsAcquired",nativeViewAcquires},{"viewsReleased",nativeViewReleases},{"windowsAcquired",nativeWindowAcquires},{"windowsReleased",nativeWindowReleases},{"handlesOutstanding",nativeObservedHandles.size()},{"viewsOutstanding",nativeObservedViews.size()},{"windowsOutstanding",nativeObservedWindows.size()}};}
struct Provider {
    std::string name;fs::path root;std::wstring desktop;
    DWORD baseline=0;unsigned requests=0,presenceRequests=0,rejections=0,callbackErrors=0;
    si::Handle staged,stop;
    OwnedChild target;
    bool targetStarted=false,targetJoined=false;
#ifndef YMCC_LIBRARY_OBSERVER_CHAIN
    ymcc::steamsession::SourceOwner source;
#endif
    std::vector<json> observations;
    ~Provider() noexcept {if(stop.value)SetEvent(stop.value);}
    void startTarget(){
        check(!targetStarted,"IPC target launched twice");
        target.start(root/L"SteamOwned"/L"steam.exe",{L"--facts-ipc-idle",L"Local\\YMCC.LibraryFacts.Stop."+std::to_wstring(GetCurrentProcessId())},desktop);
        targetStarted=true;
    }
    void finishTarget(){if(!targetStarted||targetJoined)return;check(SetEvent(stop.value)!=FALSE,"IPC target normal stop");target.join(3000);check(target.exitCode==0,"IPC target exit");target.close();targetJoined=true;}
    si::Facts read(std::uint32_t mask,const std::shared_ptr<const ymcc::processInventory::Snapshot>& all){
#ifdef YMCC_LIBRARY_OBSERVER_CHAIN
        return si::fixtureLibraryCollect(mask,g_steamSettingsObserver,
            [](const ymcc::steamsession::Session& session){return g_steamModeObserver.snapshot(session);},all);
#else
        ++requests;si::Facts out;out.available=true;out.requested=mask;
        if(mask&si::Presence){
            ++presenceRequests;ymcc::processInventory::Snapshot owned;
            check(all&&all->status==ymcc::processInventory::Status::Complete,"IPC real ancestry census unavailable");
            owned.status=all->status;owned.observedAt=all->observedAt;
            for(const auto& row:all->rows)if(targetStarted&&row.pid==target.pid)owned.rows.push_back(row);
            si::collectPresence(out,owned,{});
            if((name=="ipc-unknown-final-preserves-original"
#ifdef YMCC_LIBRARY_HOST_CHAIN
                ||name=="chain-final-unknown-preserves"
#endif
                )&&WaitForSingleObject(staged.value,0)==WAIT_OBJECT_0)out.available=false;
        }
        
#ifdef YMCC_LIBRARY_TYPED_SHUTDOWN
        ChainTyped::facts(out);
#endif
        out.observedAt=GetTickCount64();observations.push_back({{"mask",mask},{"available",out.available},{"presence",out.presence},{"pid",out.processPid},{"birth",std::to_string(out.processBirth)},{"ownedCensusOnly",true}});
        return out;
#endif
    }
};
static Provider* provider=nullptr;
static LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM from,LPARAM data){
#ifdef YMCC_LIBRARY_TYPED_SHUTDOWN
    if(provider&&ChainTyped::enabled){try{
        // Ack modes post only the cancel drain, after observing sender return; submitted work leaves no posted drain.
        if(message==ChainTyped::drainMessage){ChainTyped::drain();return 0;}
        if(message==WM_COPYDATA&&data&&reinterpret_cast<const COPYDATASTRUCT*>(data)->dwData==ymcc::steamaction::messageTag)
            return ChainTyped::serve(window,reinterpret_cast<HWND>(from),reinterpret_cast<const COPYDATASTRUCT*>(data));
    }catch(...){++provider->callbackErrors;return FALSE;}}
#endif
    if(message==WM_COPYDATA&&provider){
      try{
        const auto* copy=reinterpret_cast<const COPYDATASTRUCT*>(data);
        if(copy&&copy->dwData==si::messageTag&&copy->cbData==sizeof(si::Request)&&copy->lpData){
            const auto* request=static_cast<const si::Request*>(copy->lpData);
            if((request->mask&si::Presence)&&(provider->name=="ipc-present-final-preserves-original"
#ifdef YMCC_LIBRARY_HOST_CHAIN
                ||provider->name=="chain-final-present-preserves"
#endif
                )&&
                WaitForSingleObject(provider->staged.value,0)==WAIT_OBJECT_0&&!provider->targetStarted)provider->startTarget();
        }
#ifdef YMCC_LIBRARY_OBSERVER_CHAIN
        const auto before=ChainObserver::collectCalls;
        const auto result=fixtureLibraryInformation(window,from,copy);
        ChainObserver::informationReturned(copy,result,before);
#else
        const auto result=si::serve(window,reinterpret_cast<HWND>(from),copy,
            [](std::uint32_t mask,const std::shared_ptr<const ymcc::processInventory::Snapshot>& table,const auto& reading){
                check(reading.currentFor(provider->source),"IPC facts reader did not receive its source lease");
                return provider->read(mask,table);
            },[]{return provider->source.read();});
#endif
        if(!result)++provider->rejections;return result;
      }catch(...){++provider->callbackErrors;return FALSE;}
    }
    return DefWindowProcW(window,message,from,data);
}
static HWND makeWindow(){
    WNDCLASSW cls{};cls.lpfnWndProc=windowProc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YMCC.LibraryFacts.Provider";
    const auto registered=RegisterClassW(&cls);check(registered||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"IPC provider window registration");
    const auto window=CreateWindowExW(0,cls.lpszClassName,L"owned facts provider",WS_POPUP,0,0,1,1,nullptr,nullptr,cls.hInstance,nullptr);
    check(window&&SetPropW(window,si::property,reinterpret_cast<HANDLE>(ULONG_PTR(si::protocol))),"IPC provider property");return window;
}
static void removeWindow(HWND window){if(window){check(RemovePropW(window,si::property)!=nullptr,"IPC remove provider property");check(DestroyWindow(window)!=FALSE,"IPC destroy provider window");}}
static int writer(const std::string& name,const fs::path& output,const fs::path& transaction,DWORD providerPid){
    check(known(name)&&providerPid&&providerPid!=GetCurrentProcessId(),"IPC writer exact mode/provider");
    const auto root=fs::path(modulePath()).parent_path().parent_path();
    check(within(output,root)&&within(transaction,root)&&!fs::exists(transaction),"IPC writer output/transaction roots");
    fs::create_directories(transaction);g_workerDataRoot=transaction/L"data";
    SetEnvironmentVariableW(L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT",g_workerDataRoot.c_str());
    SetEnvironmentVariableW(L"YEMAN_GAME_WHITELIST_PATH",(transaction/L"whitelist.txt").c_str());
    SetEnvironmentVariableW(L"YEMAN_PLAYER_BLACKLIST_PATH",(transaction/L"blacklist.txt").c_str());
    Fixture f(transaction,true);textFile(f.local,validLocal);
    const auto before=readBinaryFile(f.target.path),localBefore=readBinaryFile(f.local);
    identityCallerActive=true;factsIpcRealClient=true;factsIpcOwnedProviderPid=providerPid;
    si::Handle stage(OpenEventW(EVENT_MODIFY_STATE|SYNCHRONIZE,FALSE,(L"Local\\YMCC.LibraryFacts.Stage."+std::to_wstring(providerPid)).c_str()));
    check(stage.value!=nullptr,"IPC writer phase event");
    selftestMoveFault=[&](const std::wstring&,const std::wstring& to)->DWORD{if(to.ends_with(L"transaction.pending.json"))check(SetEvent(stage.value)!=FALSE,"IPC transaction final-stage signal");return ERROR_SUCCESS;};
    nativeObserveResourceBaseline();const auto baseline=handles();bool refused=false;std::string error;json result;
    try{result=f.commit();}catch(const std::exception& e){refused=true;error=e.what();}
    selftestMoveFault={};const auto after=handles();
    const auto targetAfter=readBinaryFile(f.target.path),localAfter=readBinaryFile(f.local);
    const bool targetPreserved=targetAfter==before,localPreserved=localAfter==localBefore;
    const bool success=name=="ipc-absent-commits-real-files";
    bool passed=nativeObservedResourcesReturned()&&!hasStages(transaction)&&identityCallerProcesses==0&&identityCallerNetwork==0&&factsIpcAllowedProcessReads>0;
    if(success){
        passed=passed&&!refused&&result.value("committed",false)&&readShortcuts(f.target.path,f.target.accountId).size()==1&&
            !targetPreserved&&!localPreserved&&result.value("recentSignalStatus",std::string{})=="updated"&&
            result.value("localConfigBeforeSha256",std::string{})==sha256(localBefore)&&result.value("localConfigAfterSha256",std::string{})==sha256(localAfter)&&
            readBinaryFile(fs::path(toWide(result.at("localConfigBackup").get<std::string>())))==localBefore&&fs::is_regular_file(fs::path(toWide(result.at("transactionReceipt").get<std::string>())));
    }else{
        passed=passed&&refused&&targetPreserved&&localPreserved;
        if(name!="ipc-present-first-preserves-original"&&name!="ipc-present-final-preserves-original")passed=passed&&error.find("[steam-process-unknown]")!=std::string::npos;
    }
    const bool staged=WaitForSingleObject(stage.value,0)==WAIT_OBJECT_0;
    closeHandle(stage);
    ExclusiveJsonWrite(output,{{"passed",passed},{"name",name},{"pid",GetCurrentProcessId()},{"refused",refused},{"error",error},{"staged",staged},
        {"targetPreserved",targetPreserved},{"localPreserved",localPreserved},{"stagesCleaned",!hasStages(transaction)},
        {"handleCountBefore",baseline},{"handleCountAfter",after},{"coldKernelHandleDeltaUnattributed",static_cast<long long>(after)-baseline},{"requestResources",resourceReceipt()},{"allKernelResourcesReturnedProven",false},{"allowedOwnedProcessReads",factsIpcAllowedProcessReads},
        {"externalProcessAttempts",identityCallerProcesses},{"networkCalls",identityCallerNetwork},{"actualPublicFactsClient",true},{"result",result}});
    return passed?0:1;
}
static int serve(const std::string& name,const fs::path& output){
    check(known(name),"IPC provider exact case");const auto root=fs::path(modulePath()).parent_path();check(within(output,root),"IPC provider output root");
    auto desktop=std::make_unique<OwnedDesktop>();Provider state;state.name=name;state.root=root;state.desktop=desktop->full();provider=&state;
#ifndef YMCC_LIBRARY_OBSERVER_CHAIN
    check(state.source.claim(),"IPC facts provider isolated source claim");
    state.source.resumeReads();
#endif
    const auto window=makeWindow();HWND duplicate=nullptr;if(name=="ipc-duplicate-provider-preserves-original")duplicate=makeWindow();
    state.baseline=handles();
    state.staged.value=CreateEventW(nullptr,TRUE,FALSE,(L"Local\\YMCC.LibraryFacts.Stage."+std::to_wstring(GetCurrentProcessId())).c_str());
    check(state.staged.value&&GetLastError()!=ERROR_ALREADY_EXISTS,"IPC fresh stage event");
    state.stop.value=CreateEventW(nullptr,TRUE,FALSE,(L"Local\\YMCC.LibraryFacts.Stop."+std::to_wstring(GetCurrentProcessId())).c_str());
    check(state.stop.value&&GetLastError()!=ERROR_ALREADY_EXISTS,"IPC fresh stop event");
    if(name=="ipc-present-first-preserves-original")state.startTarget();
    nativeObserveResourceBaseline();OwnedChild child;const auto leaf=name=="ipc-wrong-client-image-preserves-original"?L"NotWorker.exe":L"SteamArtworkLab.exe";
    child.start(root/L"CustomSteamLibrary"/leaf,{L"--facts-ipc-writer",toWide(name),(root/L"writer.json").wstring(),(root/L"transaction").wstring(),std::to_wstring(GetCurrentProcessId())},desktop->full());
    check(MessagePumpUntil(child,10000),"IPC writer bounded completion");child.join(1000);const auto exit=child.exitCode;child.close();state.finishTarget();
    const auto observed=ExclusiveJsonRead(root/L"writer.json");
    closeHandle(state.staged);closeHandle(state.stop);const auto after=handles();
    removeWindow(duplicate);removeWindow(window);provider=nullptr;desktop.reset();
    bool passed=exit==0&&observed.value("passed",false)&&nativeObservedResourcesReturned()&&state.callbackErrors==0&&
        processStarts==processJoins&&processCloseFailures==0&&exceptionalJoins==0&&cleanupFailures==0&&desktopStarts==1&&desktopCloses==1;
    const bool noRequest=name=="ipc-duplicate-provider-preserves-original"||name=="ipc-wrong-client-image-preserves-original";
    if(noRequest)passed=passed&&state.requests==0;
    else if(name=="ipc-present-first-preserves-original")passed=passed&&state.presenceRequests==1&&state.observations[0]["presence"]==1&&state.targetJoined;
    else passed=passed&&state.presenceRequests>=2;
    if(name=="ipc-unknown-final-preserves-original")passed=passed&&state.rejections==1;
    if(name=="ipc-present-final-preserves-original")passed=passed&&state.targetJoined&&state.observations.back()["presence"]==1;
    ExclusiveJsonWrite(output,{{"passed",passed},{"name",name},{"providerPid",GetCurrentProcessId()},{"writerPid",child.pid},{"childExitCode",exit},
        {"requests",state.requests},{"presenceRequests",state.presenceRequests},{"serveRejections",state.rejections},{"registryReadsDenied",factsIpcRegistryDenials},{"callbackErrors",state.callbackErrors},
        {"targetStarted",state.targetStarted},{"targetJoined",state.targetJoined},{"writerJoined",true},{"handleCountBefore",state.baseline},{"handleCountAfter",after},{"coldKernelHandleDeltaUnattributed",static_cast<long long>(after)-state.baseline},{"requestResources",resourceReceipt()},{"allKernelResourcesReturnedProven",false},
        {"observations",state.observations},{"writer",observed},{"actualPublicServe",true},{"actualProcessInventoryAncestry",true},{"ownedPresenceCensusOnly",true},
        {"processStarts",processStarts},{"processJoins",processJoins},{"processCloseFailures",processCloseFailures},{"exceptionalJoins",exceptionalJoins},{"cleanupFailures",cleanupFailures},
        {"desktopStarts",desktopStarts},{"desktopCloses",desktopCloses},{"privateDesktop",true},{"desktopSwitchedVisible",false}});
    return passed?0:1;
}
#ifdef YMCC_LIBRARY_HOST_CHAIN
#include "custom_steam_library_host_worker_provider.inc"
#endif
static int entry(int argc,wchar_t** argv){
#ifdef YMCC_LIBRARY_HOST_CHAIN
    if(argc==5&&std::wstring(argv[2])==L"--host-worker-chain")return chainEntry(argc,argv);
    if(argc==4&&std::wstring(argv[1])==L"--facts-ipc-provider"&&chainKnown(toUtf8(argv[2])))return serveHost(toUtf8(argv[2]),argv[3]);
#endif
    if(argc==3&&std::wstring(argv[1])==L"--facts-ipc-idle"){
        si::Handle stop(OpenEventW(SYNCHRONIZE,FALSE,argv[2]));check(stop.value&&WaitForSingleObject(stop.value,18000)==WAIT_OBJECT_0,"IPC inert target normal stop");return 0;
    }
    if(argc==6&&std::wstring(argv[1])==L"--facts-ipc-writer")return writer(toUtf8(argv[2]),argv[3],argv[4],DWORD(std::stoul(argv[5])));
    if(argc==4&&std::wstring(argv[1])==L"--facts-ipc-provider")return serve(toUtf8(argv[2]),argv[3]);
    if(argc!=4||std::wstring(argv[2])!=L"--facts-ipc"||!known(toUtf8(argv[3])))return 2;
    const auto root=fs::absolute(argv[1]);
    const auto build=fs::absolute(fs::path(__FILE__).parent_path()/L".."/L".."/L".."/L"Build").lexically_normal();
    check(within(root,build)&&!fs::exists(root),"IPC controller new owned Build root");
    fs::create_directories(root/L"CustomSteamLibrary");fs::create_directories(root/L"SteamOwned");
    for(const auto& file:{root/L"YeManCC.exe",root/L"CustomSteamLibrary"/L"SteamArtworkLab.exe",root/L"CustomSteamLibrary"/L"NotWorker.exe",root/L"SteamOwned"/L"steam.exe"})check(fs::copy_file(modulePath(),file,fs::copy_options::none),"IPC inert PE copy");
    OwnedChild child;const auto name=toUtf8(argv[3]);child.start(root/L"YeManCC.exe",{L"--facts-ipc-provider",argv[3],(root/L"provider.json").wstring()});
    child.join(10000);const auto exit=child.exitCode;child.close();const auto receipt=ExclusiveJsonRead(root/L"provider.json");const bool passed=exit==0&&receipt.value("passed",false);
    const json raw={{"allPassed",passed},{"caseCount",1},{"failedCount",passed?0:1},{"cases",json::array({{{"name",name},{"passed",passed},{"error",passed?"":"Actual IPC/file transaction or resource assertion failed"}}})},
        {"fixtures",toUtf8(root.wstring())},{"realSteamFilesModified",false},{"identityNetworkCalls",receipt["writer"]["networkCalls"]},{"identityProcessCalls",receipt["writer"]["externalProcessAttempts"]},
        {"actualPublicFactsIpc",true},{"actualWorkerFileTransaction",true},{"wholeMachineAbsenceProven",false},{"provider",receipt}};
    ExclusiveJsonWrite(root/L"summary.json",raw);std::cout<<raw.dump(2)<<"\n";return passed?0:1;
}
} // namespace LibraryFactsIpc
