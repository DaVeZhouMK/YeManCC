// Actual AsyncRequest/WinHTTP callback measurements, never synthetic callbacks.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <atomic>
namespace measure {
struct Telemetry {
    LONG magic=0x594d4151;
    volatile LONG workerPid=0,stage=0,registrations=0,callbacks=0,closing=0,lateCallbacks=0,closeIssued=0;
    volatile LONG offStarted=0,weakCount=0,weakLive=0,returned=0,canceled=0,closed=0,unexpected=0,finishMs=0;
};
inline Telemetry* active=nullptr;
inline std::atomic<WINHTTP_STATUS_CALLBACK> delegate{nullptr};
inline SRWLOCK createLock=SRWLOCK_INIT;
inline std::atomic<unsigned> creates{0},forceCalls{0};inline std::atomic<DWORD> targetWorker{0};
inline void CALLBACK callback(HINTERNET handle,DWORD_PTR context,DWORD status,void* info,DWORD bytes){
    auto* value=active;if(value){InterlockedIncrement(&value->callbacks);if(value->offStarted)InterlockedIncrement(&value->lateCallbacks);}
    const auto original=delegate.load();if(original)original(handle,context,status,info,bytes);
    if(value&&status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)InterlockedIncrement(&value->closing);
}
inline WINHTTP_STATUS_CALLBACK WINAPI setCallback(HINTERNET handle,WINHTTP_STATUS_CALLBACK original,DWORD flags,DWORD_PTR reserved){
    if(original){delegate=original;if(active)InterlockedIncrement(&active->registrations);}
    return ::WinHttpSetStatusCallback(handle,original?callback:nullptr,flags,reserved);
}
inline BOOL WINAPI closeHttp(HINTERNET handle){if(active)InterlockedIncrement(&active->closeIssued);return ::WinHttpCloseHandle(handle);}
inline BOOL WINAPI create(LPCWSTR a,LPWSTR b,LPSECURITY_ATTRIBUTES c,LPSECURITY_ATTRIBUTES d,BOOL e,DWORD f,LPVOID g,LPCWSTR h,LPSTARTUPINFOW i,LPPROCESS_INFORMATION j){AcquireSRWLockExclusive(&createLock);const auto result=::CreateProcessW(a,b,c,d,e,f,g,h,i,j);const auto error=GetLastError();ReleaseSRWLockExclusive(&createLock);if(result)++creates;SetLastError(error);return result;}
inline BOOL WINAPI createToken(HANDLE a,DWORD b,LPCWSTR c,LPWSTR d,DWORD e,LPVOID f,LPCWSTR g,LPSTARTUPINFOW h,LPPROCESS_INFORMATION i){AcquireSRWLockExclusive(&createLock);const auto result=::CreateProcessWithTokenW(a,b,c,d,e,f,g,h,i);const auto error=GetLastError();ReleaseSRWLockExclusive(&createLock);if(result)++creates;SetLastError(error);return result;}
inline BOOL WINAPI terminateJob(HANDLE job,UINT exitCode){
    unsigned char bytes[1024]{};auto* members=reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(bytes);
    if(targetWorker&&QueryInformationJobObject(job,JobObjectBasicProcessIdList,members,sizeof(bytes),nullptr))for(DWORD i=0;i<members->NumberOfProcessIdsInList;++i)if(members->ProcessIdList[i]==targetWorker)++forceCalls;
    // No delay, denied call, additional grace or policy change is introduced.
    return ::TerminateJobObject(job,exitCode);
}
}
#define WinHttpSetStatusCallback measure::setCallback
#define WinHttpCloseHandle measure::closeHttp
#define CreateProcessW measure::create
#define CreateProcessWithTokenW measure::createToken
#define TerminateJobObject measure::terminateJob
#include "typed-legacy-async-request-helpers.inc"
#include "typed-legacy-async-request-local-peer.inc"
#undef WinHttpSetStatusCallback
#undef WinHttpCloseHandle
#undef CreateProcessW
#undef CreateProcessWithTokenW
#undef TerminateJobObject
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
#error The typed sidecar cannot compile historical Runtime callbacks.
#endif
#include <fstream>
namespace asyncfixture {
using namespace rtfixture;using namespace ymcc::deckymirror;using Json=nlohmann::json;
using ymcc::deckysidebar::Runtime;using ymcc::deckysidebar::Config;using ymcc::deckysidebar::BindingMode;using ymcc::deckysidebar::BindingPlan;
inline Json rows=Json::array(),direct=Json::array(),negatives=Json::array();
inline Json counters(const measure::Telemetry& t){return {{"workerPid",static_cast<LONG>(t.workerPid)},{"stage",static_cast<LONG>(t.stage)},{"registrations",static_cast<LONG>(t.registrations)},{"callbacks",static_cast<LONG>(t.callbacks)},{"handleClosingCallbacks",static_cast<LONG>(t.closing)},{"callbacksAfterOffStarted",static_cast<LONG>(t.lateCallbacks)},{"closeIssued",static_cast<LONG>(t.closeIssued)},{"weakCount",static_cast<LONG>(t.weakCount)},{"weakLive",static_cast<LONG>(t.weakLive)},{"bindingReturned",static_cast<LONG>(t.returned)==1},{"reason",static_cast<LONG>(t.canceled)==1?Json("mirror-bootstrap-canceled"):Json(nullptr)},{"callbackOwnersReleasedObserved",static_cast<LONG>(t.closed)==1},{"unexpectedError",static_cast<LONG>(t.unexpected)},{"finishMs",static_cast<LONG>(t.finishMs)}};}
struct Mapping {
    detail::Handle handle;measure::Telemetry* view=nullptr;std::wstring name;
    explicit Mapping(unsigned serial){name=L"Local\\YMCC-AsyncRequest-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(serial);handle.value=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(measure::Telemetry),name.c_str());need(handle.value&&GetLastError()!=ERROR_ALREADY_EXISTS,"fresh-owned-telemetry");view=static_cast<measure::Telemetry*>(MapViewOfFile(handle.value,FILE_MAP_ALL_ACCESS,0,0,sizeof(measure::Telemetry)));need(view!=nullptr,"owned-telemetry-map");new(view)measure::Telemetry;}
    ~Mapping(){if(view)UnmapViewOfFile(view);}
};
struct WeakLedger :std::vector<std::weak_ptr<AsyncState>> {
    bool retainFirst=false;std::shared_ptr<AsyncState> retained;
    void push_back(const std::weak_ptr<AsyncState>& weak){std::vector<std::weak_ptr<AsyncState>>::push_back(weak);if(retainFirst&&!retained)retained=weak.lock();if(measure::active){InterlockedExchange(&measure::active->weakCount,static_cast<LONG>(size()));InterlockedExchange(&measure::active->weakLive,static_cast<LONG>(std::count_if(begin(),end(),[](const auto& p){return !p.expired();})));}}
};
inline bool closed(const WeakLedger& pending){return std::all_of(pending.begin(),pending.end(),[](const auto& state){return state.expired();});}
inline std::string transport(const std::string& kind,unsigned short port,std::shared_ptr<CancelToken> token,WeakLedger& pending){
    std::string failure;
    try{
        ymcc::steamlive::HttpHandle session(WinHttpOpen(L"RuntimeCancelFixture/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));
        ymcc::steamlive::HttpHandle connection(WinHttpConnect(session.h,L"127.0.0.1",port,0));
        const auto deadline=GetTickCount64()+7000;
#include "typed-legacy-async-request-original-io.inc"
    }catch(const std::exception& error){failure=error.what();}
    return failure;
}
inline bool tryChild(int& exitCode){
    const auto mode=environment(L"YMCC_BINDING_CLIENT_FIXTURE_MODE");if(mode!=L"typed-async-http"&&mode!=L"typed-async-ws")return false;
    int argc=0;auto** argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)return false;const auto classified=ChildChannel::classify(argc,argv);LocalFree(argv);if(classified==ChildMode::ordinary)return false;
    ChildChannel channel;ChildMode actual{};if(!channel.tryRunCommandLine(GetCommandLineW(),exitCode,actual)||actual!=ChildMode::worker){exitCode=87;return true;}
    detail::Handle mapping{OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,environment(L"YMCC_TYPED_ASYNC_TELEMETRY").c_str())};
    auto* t=mapping.value?static_cast<measure::Telemetry*>(MapViewOfFile(mapping.value,FILE_MAP_ALL_ACCESS,0,0,sizeof(measure::Telemetry))):nullptr;
    if(!t||t->magic!=0x594d4151){exitCode=88;return true;}measure::active=t;InterlockedExchange(&t->workerPid,static_cast<LONG>(GetCurrentProcessId()));
    try{
        const auto frame=channel.readFrame(7000);need(bool(frame),"typed-original-request-read");const auto request=worker::parse(std::string(frame.payload.begin(),frame.payload.end()));
        const auto port=worker::number(request.bootstrap.at("fixturePort"));need(port<=65535&&port>0,"owned-peer-port");
        WeakLedger pending;auto token=std::make_shared<CancelToken>(channel.cancelEvent());InterlockedExchange(&t->stage,1);
        const auto failure=transport(mode==L"typed-async-http"?"http-silent":"ws-silent",static_cast<unsigned short>(port),token,pending);
        InterlockedExchange(&t->returned,1);if(failure=="mirror-bootstrap-canceled")InterlockedExchange(&t->canceled,1);else InterlockedExchange(&t->unexpected,1);
        const auto before=GetTickCount64(),until=before+1500;while(!closed(pending)&&GetTickCount64()<until)Sleep(5);
        InterlockedExchange(&t->weakLive,static_cast<LONG>(std::count_if(pending.begin(),pending.end(),[](const auto& p){return !p.expired();})));
        if(closed(pending)&&!pending.empty())InterlockedExchange(&t->closed,1);InterlockedExchange(&t->finishMs,static_cast<LONG>(GetTickCount64()-before));InterlockedExchange(&t->stage,2);
        // Real measurements, not a claimed successful bootstrap/health result.
        const Json packet={{"schema",worker::schema},{"id",request.id},{"generation",std::to_string(request.generation)},{"kind","result"},{"detail",{{"ok",false},{"mutated",false},{"reason",failure}}}};
        const auto bytes=packet.dump();channel.writeFrame(std::vector<std::uint8_t>(bytes.begin(),bytes.end()),150);
        exitCode=t->closed&&t->canceled?0:2;
    }catch(...){InterlockedExchange(&t->unexpected,1);exitCode=89;}
    measure::active=nullptr;UnmapViewOfFile(t);return true;
}
inline Json invariant(const char* id,bool value,const Json& actual){return {{"id",id},{"status",value?"PASS":"FAIL"},{"passed",value},{"actual",actual}};}
inline Json typed(const std::filesystem::path& output,const std::string& kind,const std::string& stop,unsigned serial){
    LocalHttpFixture server(kind);Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));Mapping mapping(serial);
    const auto previousTelemetry=environment(L"YMCC_TYPED_ASYNC_TELEMETRY");setEnvironment(L"YMCC_TYPED_ASYNC_TELEMETRY",mapping.name);
    auto client=std::make_shared<Client>();Runtime runtime;std::atomic<unsigned> plans{0};std::atomic<bool> committed{false};
    Config config;config.executable=detail::executable();config.home=output.wstring();std::filesystem::create_directories(output);config.preflight=[]{return std::string{};};config.ownedBinding=client;
    config.bindingPlan=[&](BindingMode,std::uint64_t generation){++plans;auto request=makeRequest(generation);request["bootstrap"]["fixtureContextPid"]=context.lease.at("pid");request["bootstrap"]["fixturePort"]=server.port;return BindingPlan{std::move(request),{}};};runtime.initialize(std::move(config));runtime.observeSteam(1,true);runtime.setEnabled(true);
    const auto startDeadline=GetTickCount64()+4000;while(!runtime.snapshot().pid&&GetTickCount64()<startDeadline)Sleep(5);
    Json assertions=Json::array();auto add=[&](const char* id,bool okay,const Json& value){assertions.push_back(invariant(id,okay,value));};
    const auto pid=runtime.snapshot().pid;add("inert child did not start",pid!=0,pid);need(pid!=0,"inert child did not start");
    detail::Handle child{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid)};add("owned child handle unavailable",child.value!=nullptr,child.value!=nullptr);need(child.value!=nullptr,"owned child handle unavailable");
    wchar_t image[32768]{},observed[32768]{};GetModuleFileNameW(nullptr,image,32768);DWORD chars=32768;const bool identityOkay=QueryFullProcessImageNameW(child.value,0,observed,&chars)&&!_wcsicmp(observed,image);add("child identity mismatch",identityOkay,identity(child.value).at("path"));
    need(identityOkay,"child identity mismatch");
    setEnvironment(L"YMCC_BINDING_CLIENT_FIXTURE_MODE",kind=="http-silent"?L"typed-async-http":L"typed-async-ws");committed=runtime.requestBindingRefresh(2);
    const bool accepted=WaitForSingleObject(server.accepted,3000)==WAIT_OBJECT_0;add("production request did not reach silent fixture",accepted,accepted);need(accepted,"production request did not reach silent fixture");
    Sleep(40);const auto before=client->snapshot();const auto active=counters(*mapping.view);measure::targetWorker=before.processId;const auto forcedBefore=measure::forceCalls.load();
    detail::Handle binding{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,before.processId)};
    InterlockedExchange(&mapping.view->offStarted,1);const auto stopAt=GetTickCount64();bool harvested=false;
    if(stop=="client-harvest"){
        client->cancel();const auto until=stopAt+1500;while(!mapping.view->closed&&GetTickCount64()<until)Sleep(2);harvested=mapping.view->closed==1;runtime.shutdown();
    }else if(stop=="fatal")runtime.setEnabled(false,"decky-render-failed");else runtime.shutdown();
    const auto elapsed=GetTickCount64()-stopAt;const bool childExited=WaitForSingleObject(child.value,1000)==WAIT_OBJECT_0;
    const auto closeDeadline=GetTickCount64()+1500;while(!mapping.view->closed&&binding.value&&WaitForSingleObject(binding.value,0)==WAIT_TIMEOUT&&GetTickCount64()<closeDeadline)Sleep(5);
    const auto faultBeforeLate=runtime.snapshot().reason;const auto telemetry=counters(*mapping.view);const bool callbackClosed=mapping.view->closed==1;const auto destroyed=client->snapshot();
    add("elapsed < 1500",elapsed<1500,elapsed);add("returned",mapping.view->returned==1,telemetry.at("bindingReturned"));add("failure == mirror-bootstrap-canceled",mapping.view->canceled==1,telemetry.at("reason"));add("childExited",childExited,childExited);add("closed()",callbackClosed,telemetry.at("callbackOwnersReleasedObserved"));add("!runtime.snapshot().enabled",!runtime.snapshot().enabled,runtime.snapshot().enabled);add("runtime.snapshot().pid == 0",runtime.snapshot().pid==0,runtime.snapshot().pid);
    const auto plansBeforeLate=plans.load(),createsBeforeLate=measure::creates.load();runtime.requestBindingRefresh(3);runtime.requestTransportRecovery();runtime.requestLaunchAdmissionRefresh();runtime.shutdown();const bool stayedOff=!runtime.snapshot().enabled&&runtime.snapshot().pid==0;add("stayedOff",stayedOff,stayedOff);
    const bool zero=destroyed.fullyDestroyed&&destroyed.handles==0&&destroyed.pendingIo==0&&destroyed.activeCalls==0&&destroyed.jobActiveProcesses==0&&destroyed.childExited;
    bool passed=true;for(const auto& row:assertions)if(row.at("passed")!=true)passed=false;
    const auto forceCount=measure::forceCalls.load()-forcedBefore;const bool independent=committed&&before.pendingIo>0&&before.processId!=0&&active.at("weakCount").get<int>()==(kind=="ws-silent"?2:1)&&zero&&stayedOff&&plans==plansBeforeLate&&measure::creates==createsBeforeLate&&context.alive()&&binding.value&&WaitForSingleObject(binding.value,0)==WAIT_OBJECT_0&&mapping.view->unexpected==0&&(stop!="fatal"||faultBeforeLate=="decky-render-failed");
    auto beforeState=state(before);beforeState["processId"]=before.processId;auto exitState=state(destroyed);exitState["processId"]=destroyed.processId;
    auto result=Json{{"id",stop+"."+kind},{"kind",kind},{"stop",stop},{"passed",passed&&independent},{"originalAssertions",assertions},{"originalPredicate",passed},
        {"currentResourceAndIdentityChecks",independent},{"committedRefresh",committed.load()},{"beforeOff",beforeState},{"activeAsync",active},{"afterStopAsync",telemetry},{"atExit",exitState},
        {"shutdownMs",elapsed},{"deadlineMs",1500},{"nativeBudgetMs",20000},{"callbackCloseDeadlineMs",1500},{"workerJobForceCalls",forceCount},{"workerExitObserved",binding.value&&WaitForSingleObject(binding.value,0)==WAIT_OBJECT_0},{"ownedLoaderExitObserved",childExited},{"harvestedBeforeRuntimeStop",harvested},{"faultBeforeLate",faultBeforeLate},{"cefUnaffected",context.alive()},
        {"latePlansBefore",plansBeforeLate},{"latePlansAfter",plans.load()},{"lateCreatesBefore",createsBeforeLate},{"lateCreatesAfter",measure::creates.load()}};
    setEnvironment(L"YMCC_TYPED_ASYNC_TELEMETRY",previousTelemetry);measure::targetWorker=0;std::cout<<result.dump()<<std::endl;return result;
}

inline Json directConsumer(const std::string& kind,bool renew,bool retain){
    LocalHttpFixture server(kind);CancelSignal cancellation;cancellation.renew();auto previous=cancellation.current();
    measure::Telemetry telemetry;measure::active=&telemetry;WeakLedger pending;pending.retainFirst=retain;
    std::atomic<bool> accepted{false};const auto started=GetTickCount64();
    std::thread canceler([&]{if(WaitForSingleObject(server.accepted,2000)==WAIT_OBJECT_0){accepted=true;Sleep(40);InterlockedExchange(&telemetry.offStarted,1);if(renew)cancellation.renew();else cancellation.cancel();}});
    const auto failure=transport(kind,server.port,previous,pending);canceler.join();const auto elapsed=GetTickCount64()-started;
    const auto until=GetTickCount64()+1500;while(telemetry.closing<telemetry.registrations&&GetTickCount64()<until)Sleep(5);
    const bool retainedFailure=retain&&!closed(pending)&&pending.retained&&telemetry.closing==telemetry.registrations;
    const auto beforeRelease=counters(telemetry);pending.retained.reset();const auto closeDeadline=GetTickCount64()+1500;while(!closed(pending)&&GetTickCount64()<closeDeadline)Sleep(5);
    const bool ownersReleased=closed(pending)&&!pending.empty();
    auto current=cancellation.current();const bool fresh=!renew||(previous->canceled()&&current!=previous&&!current->canceled());
    if(renew){previous->cancel();need(!current->canceled(),"previous-token-canceled-new-generation");}
    bool lateIsolation=true;
    if(renew){
        // Actual new WinHTTP request must remain healthy after predecessor cancel
        // and all its real HANDLE_CLOSING callbacks, not just token pointer checks.
        LocalHttpFixture freshServer("http-body");WeakLedger later;
        {ymcc::steamlive::HttpHandle session(WinHttpOpen(L"AsyncFreshToken/1",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC));ymcc::steamlive::HttpHandle connection(WinHttpConnect(session.h,L"127.0.0.1",freshServer.port,0));AsyncRequest request(WinHttpOpenRequest(connection.h,L"GET",L"/fresh",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0),current,GetTickCount64()+1000);later.push_back(request.callbackState());previous->cancel();request.handshake();lateIsolation=request.httpBody(100)=="[]"&&!current->canceled();}
        const auto closeAt=GetTickCount64()+1500;while(!closed(later)&&GetTickCount64()<closeAt)Sleep(5);lateIsolation=lateIsolation&&closed(later);cancellation.cancel();lateIsolation=lateIsolation&&current->canceled();
    }
    const auto after=counters(telemetry);measure::active=nullptr;
    const bool passed=accepted&&failure=="mirror-bootstrap-canceled"&&elapsed<(renew?700u:1500u)&&ownersReleased&&fresh&&lateIsolation&&telemetry.closing==telemetry.registrations&&(!retain||retainedFailure);
    return {{"id",(retain?"negative.retained-callback-owner.":renew?"negative.old-cancel-and-late-callback-isolation.":"current.direct-real-async-cancel.")+kind},{"passed",passed},{"kind",kind},{"elapsedMs",elapsed},{"deadlineMs",renew?700:1500},{"callbackCloseDeadlineMs",1500},
        {"accepted",accepted.load()},{"reason",failure},{"callbackOwnersReleased",ownersReleased},{"retentionNegative",retain},{"originalClosedPredicateWhileRetained",retain?Json(!retainedFailure):Json(nullptr)},
        {"freshUncanceledToken",fresh},{"lateOldCallbacksDidNotAffectActualNewRequest",lateIsolation},{"beforeRetainedRelease",beforeRelease},{"atExit",after}};
}
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);int childExit=0;
    if(asyncfixture::tryChild(childExit)||rtfixture::tryStalledChild(childExit)||ymcc::deckybinding::fixture::tryRunChild(childExit))return childExit;
    if(argc==1){Sleep(30000);return 0;} // Exact original owned inert child behavior.
    if(argc!=2)return 3;using namespace asyncfixture;const auto output=std::filesystem::absolute(argv[1]);
    need(output.filename()==L"binding-fixtures"&&output.parent_path()==std::filesystem::path(detail::executable()).parent_path(),"original-owned-output-boundary");std::filesystem::create_directories(output);WSADATA sockets{};if(WSAStartup(MAKEWORD(2,2),&sockets))return 3;
    Json report={{"schema","ymcc.typed-legacy-async-request.v1"},{"productCallbackMode","typed-only; legacy macro absent"},{"fullPersistentAcceptance",false},{"officialLoaderExecuted",false},{"realSteamActions",0},{"physicalSleepPerformed",false},{"externalNetwork",false},{"originalNativeBudgetMs",20000},
        {"telemetryBoundary","Read-only real callback forwarding and shared owned measurement mapping. No callback is invoked synthetically. No stop policy, deadline, Job kill or callback owner is waived."}};
    const auto started=GetTickCount64();bool okay=false,calibrationPassed=true;
    try{
        report["sharedCryptoCalibration"]=initializeSharedCryptoForMeasurement();
        // Calibrate an independent exact production AsyncRequest before strict
        // parent baselines; all actual callbacks must complete, no handle waiver.
        for(unsigned i=0;i<2;++i){
            const auto before=handles();Json probes=Json::array();
            for(const auto& kind:{std::string("http-silent"),std::string("ws-silent")}){auto probe=directConsumer(kind,false,false);need(probe.at("passed")==true,"cold-real-async-consumer-failed");probes.push_back(probe);}
            // Callback weak-owner completion does not synchronously reap the
            // WinHTTP session's independent close notifications. Observe their
            // actual count within the unchanged1500ms release window.
            const auto until=GetTickCount64()+1500;DWORD last=handles();unsigned stable=0;Json samples=Json::array();
            while(GetTickCount64()<until&&stable<10){Sleep(10);const auto count=handles();samples.push_back(count);stable=count==last?stable+1:0;last=count;}
            report["coldAsyncCalibration"].push_back({{"index",i},{"beforeHandles",before},{"afterHandles",handles()},{"stableSamples",stable},{"samples",samples},{"probes",probes},{"table",handleTable()}});
            if(stable<10||(i==1&&before!=handles()))calibrationPassed=false; // Preserve FAIL, continue actual scenario measurements.
        }
        // Initialize unchanged self-image Context process creation before parent
        // resource measurements; own children leave naturally.
        for(unsigned i=0;i<2;++i){const auto before=handles();{Controls controls(L"success");Context context;}report["coldContextCalibration"].push_back({{"index",i},{"beforeHandles",before},{"afterHandles",handles()}});if(i==1)need(before==handles(),"cold-context-not-stable");}
        const auto handlesBefore=handles();const auto threadsBefore=threadCount();report["parentHandlesBefore"]=handlesBefore;report["parentThreadsBefore"]=threadsBefore;report["parentHandleSnapshotBefore"]=handleTable();
        for(const auto& kind:{std::string("http-silent"),std::string("ws-silent")}){
            direct.push_back(directConsumer(kind,false,false));negatives.push_back(directConsumer(kind,true,false));negatives.push_back(directConsumer(kind,false,true));
        }
        unsigned serial=0;
        for(const auto& stop:{std::string("shutdown"),std::string("fatal"),std::string("client-harvest")})for(const auto& kind:{std::string("http-silent"),std::string("ws-silent")}){
            try{rows.push_back(typed(output/(stop+"-"+kind),kind,stop,++serial));}catch(const std::exception& error){rows.push_back({{"id",stop+"."+kind},{"kind",kind},{"stop",stop},{"passed",false},{"error",error.what()}});}
        }
        report["parentHandlesAfter"]=handles();report["parentThreadsAfter"]=threadCount();report["parentHandleSnapshotAfter"]=handleTable();
        report["strictParentHandlesEqual"]=handles()==handlesBefore;report["strictParentThreadsEqual"]=threadCount()==threadsBefore;
        okay=calibrationPassed&&rows.size()==6&&direct.size()==2&&negatives.size()==4&&handles()==handlesBefore&&threadCount()==threadsBefore;
        for(const auto& row:rows)if(row.at("passed")!=true)okay=false;for(const auto& row:direct)if(row.at("passed")!=true)okay=false;for(const auto& row:negatives)if(row.at("passed")!=true)okay=false;
    }catch(const std::exception& error){measure::active=nullptr;report["error"]=error.what();}
    report["calibrationPassed"]=calibrationPassed;report["passed"]=okay;report["originalTypedRows"]=rows;report["directAsyncRequestConsumers"]=direct;report["negativeControls"]=negatives;report["elapsedMs"]=GetTickCount64()-started;report["noStopPolicyInterposition"]=true;
    std::ofstream(output.parent_path()/L"native-result.json")<<report.dump(2)<<"\n";std::cout<<report.dump()<<std::endl;WSACleanup();return okay?0:1;
}



