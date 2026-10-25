// Current typed migration of the ordered historical Runtime assertions.
// Every intercepted API is forwarded unchanged; the only gate delays one real cancel.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>
#include <fstream>
#include <iostream>
namespace measured {
inline std::atomic<DWORD> thread{0};
inline std::atomic<bool> armed{false}, timedOut{false};
inline std::atomic<unsigned> calls{0}, gatedCalls{0};
inline HANDLE entered=nullptr, release=nullptr;
inline DWORD forwardedError=0; inline BOOL forwardedResult=FALSE;
inline BOOL WINAPI cancelIo(HANDLE pipe,LPOVERLAPPED overlapped) {
    ++calls;
    if(GetCurrentThreadId()==thread.load() && armed.exchange(false)) {
        ++gatedCalls;SetEvent(entered);
        if(WaitForSingleObject(release,2000)!=WAIT_OBJECT_0)timedOut=true;
        const BOOL result=::CancelIoEx(pipe,overlapped);const DWORD error=GetLastError();
        forwardedResult=result;forwardedError=error;SetLastError(error);return result;
    }
    return ::CancelIoEx(pipe,overlapped);
}
}
#define CancelIoEx measured::cancelIo
#include "typed-legacy-runtime-helpers.inc"
#undef CancelIoEx
#ifdef YMCC_DECKY_RUNTIME_LEGACY_FIXTURE
#error The historical Runtime branch is forbidden in this current typed consumer.
#endif
namespace migration {
using namespace rtfixture;
using ymcc::deckysidebar::Runtime;
using ymcc::deckysidebar::Config;
using ymcc::deckysidebar::BindingMode;
using ymcc::deckysidebar::BindingPlan;
using ymcc::deckysidebar::LaunchAdmission;
inline Json assertions=Json::array(), scenarios=Json::array();
inline Json observation=Json::object(),ownedContexts=Json::array();
struct ContextWitness {
    std::string scenario;detail::Handle process;Json lease;
    explicit ContextWitness(const char* name):scenario(name){}
    void watch(const Context& context){lease=context.lease;process.value=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,worker::number(lease.at("pid")));need(process.value!=nullptr,"owned-context-witness");}
    ~ContextWitness(){if(process.value)ownedContexts.push_back({{"scenario",scenario},{"lease",lease},{"exitedAfterOriginalContextDestructor",WaitForSingleObject(process.value,0)==WAIT_OBJECT_0}});}
};
inline void check(bool okay,const char* label) {
    assertions.push_back({{"label",label},{"passed",okay},{"measurements",observation}});
    std::cout<<(okay?"PASS ":"FAIL ")<<label<<std::endl;
}
inline Json snapshot(Runtime& runtime,const std::shared_ptr<Client>& client) {
    const auto s=runtime.snapshot();const auto c=client->snapshot();
    auto value=state(c);value["enabled"]=c.enabled;value["canceled"]=c.canceled;value["processId"]=c.processId;
    return {{"phase",s.phase},{"reason",s.reason},{"pid",s.pid},{"enabled",s.enabled},
        {"ownedBindingDestroyed",s.ownedBindingDestroyed},{"ownedBindingCleanupError",s.ownedBindingCleanupError},{"client",value}};
}
inline Config typed(const std::shared_ptr<Client>& client,const Context& context,std::atomic<unsigned>& plans) {
    Config config;config.executable=L"NEVER-LAUNCH.exe";config.home=L"NO-WRITE";config.ownedBinding=client;
    config.launchAdmission=[] {return LaunchAdmission{true,{},false,{},true};};
    const auto cef=worker::number(context.lease.at("pid"));
    config.bindingPlan=[client,cef,&plans](BindingMode mode,std::uint64_t generation) {
        ++plans;need(mode==BindingMode::retained,"retained-only-owned-protocol-plan");
        auto request=makeRequest(generation);request["bootstrap"]["fixtureContextPid"]=cef;
        request["requireFreshContext"]=false;request["retainedFrontendOnly"]=true;
        request["bootstrap"]["frontendBuildId"]=std::string(64,'d');
        return BindingPlan{std::move(request),{}};
    };return config;
}
inline void environmentAndNoLaunch() {
    using ymcc::deckysidebar::officialLoaderSearchPath;
    observation={{"source","production officialLoaderSearchPath; unchanged historical arguments and equality"}};
#include "typed-legacy-runtime-path-assertions.inc"
    Runtime runtime;auto client=std::make_shared<Client>();
    std::mutex mutex;std::condition_variable changed;std::atomic<int> attempts{0};
    Config config;config.executable=L"NEVER-LAUNCH.exe";config.home=L"NEVER-CREATE-DIRECTORY";
    config.preflight=[&] {++attempts;return std::string("test-no-launch");};
    config.changed=[&](const auto&) {changed.notify_all();};config.ownedBinding=client;
    runtime.initialize(std::move(config));
    auto awaitPhase=[&](const char* phase) {
        std::unique_lock lock(mutex);return changed.wait_for(lock,std::chrono::seconds(2),[&] {return runtime.snapshot().phase==phase;});
    };
    auto capture=[&] {observation=snapshot(runtime,client);observation["preflightAttempts"]=attempts.load();};
    capture();check(!runtime.snapshot().enabled,"default must be disabled");
    runtime.observeSteam(1);capture();check(attempts==0,"Steam cannot start disabled loader");
    runtime.observeSteam(0);const bool enabled=runtime.setEnabled(true);capture();check(enabled,"enable worker");
    const bool waiting=awaitPhase("waiting-steam");capture();check(waiting,"wait for Steam, no polling");
    check(attempts==0,"no preflight when Steam absent");
    runtime.observeSteam(1);const bool denied=awaitPhase("unavailable");capture();check(denied,"explicit source preflight blocks startup");
    check(attempts==1,"one startup attempt");check(runtime.snapshot().pid==0,"no external process started");
    runtime.observeSteam(1);capture();check(attempts==1,"duplicate Steam state no retry");
    runtime.observeSteam(0);const bool exited=awaitPhase("waiting-steam");capture();check(exited,"Steam exit releases linked runtime");
    runtime.observeSteam(1);const bool retry=awaitPhase("unavailable");capture();check(retry,"genuine next session retries once");
    check(attempts==2,"bounded session retry");
    runtime.setEnabled(false);capture();check(runtime.snapshot().phase=="disabled","disable stops native worker synchronously");
    runtime.setEnabled(false,"decky-render-failed");capture();check(runtime.snapshot().reason=="decky-render-failed","safety reason survives synchronous normal close");
    runtime.observeSteam(0);runtime.observeSteam(1,true);capture();
    check(!runtime.snapshot().enabled&&runtime.snapshot().reason=="decky-render-failed","session changes cannot reopen a safety-disabled loader");
    const int before=attempts;runtime.observeSteam(1,true);capture();check(attempts==before,"new Steam session while off does not preflight");
    runtime.setEnabled(true);const bool reenabled=awaitPhase("unavailable");capture();check(reenabled,"explicit re-enable works");
    check(runtime.snapshot().reason!="decky-render-failed","manual enable clears saved runtime fault");runtime.shutdown();
    capture();check(runtime.snapshot().pid==0,"shutdown no owned PID remains");needDestroyed(*client);
    scenarios.push_back({{"id","current.no-launch-environment-and-state"},{"atExit",observation}});
}
inline void cancellation() {
    // Actual current resource-preparation cancellation predicate, before typed begin.
    detail::Handle entered{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    Runtime preparing;auto preparedClient=std::make_shared<Client>();
    std::atomic<unsigned> beginnings{0},cancellations{0};
    Config c;c.executable=L"NEVER-LAUNCH.exe";c.home=L"NO-WRITE";c.ownedBinding=preparedClient;
    c.preflight=[] {return std::string("test-no-launch");};
    c.prepareEnvironment=[&](const std::function<bool()>& canContinue) {
        if(!preparing.snapshot().steamRunning)return std::string{}; // Resource preparation also runs for known-absent Steam; no bootstrap wait there.
        ++beginnings;SetEvent(entered.value);const auto until=GetTickCount64()+2500;
        while(canContinue()&&GetTickCount64()<until)Sleep(2);
        if(!canContinue()){++cancellations;return std::string("mirror-bootstrap-canceled");}
        return std::string("test-no-launch");
    };
    preparing.initialize(std::move(c));preparing.observeSteam(1);preparing.setEnabled(true);
    const bool preparationEntered=WaitForSingleObject(entered.value,1000)==WAIT_OBJECT_0;
    auto start=GetTickCount64();preparing.setEnabled(false);const auto preparationOffMs=GetTickCount64()-start;
    const bool onePreparation=beginnings==1&&cancellations>=1;
    const bool preparationNoLaunch=preparing.snapshot().phase=="disabled"&&preparing.snapshot().pid==0;
    ResetEvent(entered.value);preparing.setEnabled(true);
    const bool newPreparation=WaitForSingleObject(entered.value,1000)==WAIT_OBJECT_0;
    preparing.observeSteam(0);const bool preparationWait=waitFor([&] {return preparing.snapshot().phase=="waiting-steam";},1000);
    preparing.shutdown();needDestroyed(*preparedClient);
    const auto preparation=Json{{"entered",preparationEntered},{"offMs",preparationOffMs},{"oneOperationCanceled",onePreparation},
        {"noLaunch",preparationNoLaunch},{"freshOperation",newPreparation},{"steamExitWait",preparationWait},
        {"beginnings",beginnings.load()},{"cancellationsObservedByPredicate",cancellations.load()},{"atExit",snapshot(preparing,preparedClient)}};
    // The same six invariants additionally consume the real owned Client/Owner.
    ContextWitness witness("typed-cancellation");Controls controls(L"stubborn");Context context;witness.watch(context);ContextJob contextOwner(worker::number(context.lease.at("pid")));
    auto client=std::make_shared<Client>();std::atomic<unsigned> plans{0};Runtime waiting;
    waiting.initialize(typed(client,context,plans));waiting.observeSteam(1);waiting.setEnabled(true);
    const bool actualEntered=WaitForSingleObject(controls.ready.value,1000)==WAIT_OBJECT_0;
    const auto before=client->snapshot();const auto cancelsBefore=measured::calls.load();
    detail::Handle child{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,before.processId)};
    start=GetTickCount64();waiting.setEnabled(false);const auto offMs=GetTickCount64()-start;
    const auto after=client->snapshot();
    const bool actualExit=child.value&&WaitForSingleObject(child.value,0)==WAIT_OBJECT_0;
    const bool oneActual=plans==1&&measured::calls>cancelsBefore&&after.canceled;
    const bool actualNoLaunch=waiting.snapshot().phase=="disabled"&&waiting.snapshot().pid==0&&after.fullyDestroyed&&actualExit&&controls.releaseUnsent();
    ResetEvent(controls.ready.value);waiting.setEnabled(true);
    const bool fresh=WaitForSingleObject(controls.ready.value,1000)==WAIT_OBJECT_0;
    const auto newEpoch=client->snapshot().epoch;
    waiting.observeSteam(0);const bool steamWait=waitFor([&] {return waiting.snapshot().phase=="waiting-steam";},1000);
    waiting.shutdown();needDestroyed(*client);
    observation={{"resourcePreparation",preparation},{"typedBeforeOff",state(before)},{"typedAtExit",snapshot(waiting,client)},
        {"typedOffMs",offMs},{"cancelApiCalls",measured::calls.load()-cancelsBefore},{"ownedChildExited",actualExit},
        {"oldEpoch",before.epoch},{"newEpoch",newEpoch},{"plans",plans.load()},{"fixtureReleaseUnsent",controls.releaseUnsent()}};
    check(preparationEntered&&actualEntered&&before.pendingIo!=0&&before.processId!=0,"preflight actually entered its cancellation wait");
    check(preparationOffMs<1000&&offMs<1000,"off cancels pending bootstrap before joining worker");
    check(onePreparation&&oneActual,"one bootstrap operation token and explicit cancel callback");
    check(preparationNoLaunch&&actualNoLaunch,"canceled preflight cannot launch external process");
    check(newPreparation&&fresh&&newEpoch>before.epoch,"explicit enable gets fresh cancellation operation");
    check(preparationWait&&steamWait,"Steam exit cancels pending binding and returns to wait state");
    scenarios.push_back({{"id","current.resource-and-real-owned-binding-cancellation"},{"measurements",observation}});
}
inline void handoff() {
    ContextWitness witness("typed-handoff");Controls controls(L"stubborn");Context context;witness.watch(context);ContextJob contextOwner(worker::number(context.lease.at("pid")));
    detail::Handle cancelEntered{CreateEventW(nullptr,TRUE,FALSE,nullptr)},cancelRelease{CreateEventW(nullptr,TRUE,FALSE,nullptr)},
        newBeginning{CreateEventW(nullptr,TRUE,FALSE,nullptr)},newPreflight{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    measured::entered=cancelEntered.value;measured::release=cancelRelease.value;measured::timedOut=false;
    auto client=std::make_shared<Client>();Runtime runtime;std::atomic<unsigned> starts{0};std::atomic<bool> freshToken{false};
    std::uint64_t oldEpoch=0,newEpoch=0;Json requests=Json::array();
    auto config=typed(client,context,starts);auto plan=config.bindingPlan;
    config.bindingPlan=[&](BindingMode mode,std::uint64_t generation) {
        auto result=plan(mode,generation);const auto current=client->snapshot();
        requests.push_back({{"generation",generation},{"id",result.request.at("id")},{"epoch",current.epoch},{"enabled",current.enabled},{"canceled",current.canceled}});
        if(starts==2){newEpoch=current.epoch;freshToken=current.enabled&&!current.canceled&&newEpoch>oldEpoch;SetEvent(newBeginning.value);SetEvent(newPreflight.value);}
        return result;
    };
    runtime.initialize(std::move(config));runtime.observeSteam(1);runtime.setEnabled(true);
    const bool predecessorEntered=WaitForSingleObject(controls.ready.value,1000)==WAIT_OBJECT_0;
    const auto old=client->snapshot();oldEpoch=old.epoch;
    detail::Handle predecessor{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,old.processId)};
    // Only future source-built protocol children read this mode. The predecessor
    // has already parsed its request and is waiting on its original release event.
    setEnvironment(L"YMCC_BINDING_CLIENT_FIXTURE_MODE",L"success");measured::armed=true;
    const auto gateBefore=measured::gatedCalls.load();
    std::thread observer([&] {measured::thread=GetCurrentThreadId();runtime.observeSteam(1,true);});
    const bool cancellationEntered=WaitForSingleObject(cancelEntered.value,1000)==WAIT_OBJECT_0;
    controls.releaseNow();
    const bool predecessorExited=predecessor.value&&WaitForSingleObject(predecessor.value,1000)==WAIT_OBJECT_0;
    const bool serialized=WaitForSingleObject(newBeginning.value,200)==WAIT_TIMEOUT;
    const auto startsWhileGated=starts.load();
    SetEvent(cancelRelease.value);observer.join();measured::thread=0;measured::armed=false;
    const bool replacementRan=WaitForSingleObject(newPreflight.value,1000)==WAIT_OBJECT_0;
    const bool attached=waitFor([&] {return runtime.snapshot().phase=="frontend-attached";},1000);
    const auto replacement=client->snapshot();
    runtime.shutdown();needDestroyed(*client);
    observation={{"predecessor",state(old)},{"predecessorExitedWhileCancelGated",predecessorExited},
        {"cancelGateEntered",cancellationEntered},{"cancelGateTimedOut",measured::timedOut.load()},
        {"forwardedCancelCalls",measured::gatedCalls.load()-gateBefore},{"forwardedCancelResult",bool(measured::forwardedResult)},
        {"forwardedCancelLastError",measured::forwardedError},{"startsWhileGated",startsWhileGated},
        {"serializationObservationMs",200},{"oldEpoch",oldEpoch},{"newEpoch",newEpoch},{"requests",requests},
        {"replacement",state(replacement)},{"replacementAttached",attached},{"atExit",snapshot(runtime,client)}};
    check(predecessorEntered&&old.pendingIo!=0&&old.processId!=0,"handoff fixture entered predecessor operation");
    check(cancellationEntered&&!measured::timedOut&&measured::gatedCalls==gateBefore+1&&(measured::forwardedResult||measured::forwardedError==ERROR_NOT_FOUND),"handoff cancellation actually gated");
    check(serialized&&startsWhileGated==1&&predecessorExited,"replacement token cannot renew during predecessor cancellation");
    check(replacementRan&&freshToken&&attached&&!replacement.canceled,"replacement operation has a fresh uncanceled token");
    check(starts==2&&runtime.snapshot().pid==0&&requests.size()==2&&requests[0]["id"]!=requests[1]["id"]&&requests[0]["generation"]<requests[1]["generation"],
        "serialized handoff performs no extra attempts or external launch");
    scenarios.push_back({{"id","current.real-owned-generation-token-handoff"},{"measurements",observation}});
}
inline Json negatives=Json::array();
inline void negativeControls() {
    auto test=[&](const char* id,const auto& body) {
        try{auto row=body();row["id"]=id;row["passed"]=true;negatives.push_back(std::move(row));}
        catch(const std::exception& e){negatives.push_back({{"id",id},{"passed",false},{"error",e.what()}});}
    };
    // Original Client epoch rejection conditions and sequence; unchanged thresholds.
    test("negative.original-enable-cancel-epoch-fences",[] {
        Client zero;need(zero.enable(0),"zero-first-epoch");zero.cancel();need(!zero.enable(0)&&zero.enable(1),"zero-epoch-fence");
        Client client;need(!client.begin(makeRequest(1),1),"begin-without-enable");
        need(client.enable(1),"enable");needDestroyed(client);
        need(bool(client.cancel()),"cancel");need(!client.enable(1)&&!client.begin(makeRequest(1),1),"off-epoch-revived");
        need(client.enable(2)&&!client.enable(1),"new-epoch-fence");needDestroyed(client);return state(client.snapshot());
    });
    test("negative.original-refresh-stop-off-epoch-sequence",[] {
        Controls fixture(L"blocked-read");Client client;need(client.enable(1),"enable");need(bool(client.begin(makeRequest(1),1)),"begin1");
        need(bool(client.begin(makeRequest(2),1)),"refresh");need(client.stop().fullyDestroyed,"ordinary-stop");
        need(bool(client.begin(makeRequest(3),1)),"stop-permanently-disabled-epoch");client.cancel();need(client.stop().fullyDestroyed,"off-stop");
        need(!client.begin(makeRequest(4),1)&&!client.enable(1),"old-epoch-reopened");
        need(client.enable(2)&&bool(client.begin(makeRequest(1),2)),"new-epoch-start");need(client.stop().fullyDestroyed,"new-stop");needDestroyed(client);return state(client.snapshot());
    });
    for(const auto mode:{L"wrong-id",L"wrong-generation"}) {
        std::string id="negative.original-reject-";for(auto* ch=mode;*ch;++ch)id.push_back(static_cast<char>(*ch));
        test(id.c_str(),[mode] {
            Controls fixture(mode);Client client;need(client.enable(1),"enable");need(bool(client.begin(makeRequest(1),1)),"begin");
            auto packet=receive(client);
            need(packet.kind==Packet::failed&&!packet.reason.empty()&&packet.reason.find(std::string(64,'b'))==std::string::npos,"invalid-frame-accepted");
            need(client.takeContextProcess()==nullptr,"invalid-frame-context-admitted");need(client.stop().fullyDestroyed,"stop");needDestroyed(client);
            return Json{{"reason",packet.reason},{"atExit",state(client.snapshot())}};
        });
    }
}
} // namespace migration
int main(int argc,char** argv) {
    int childExit=0;if(rtfixture::tryStalledChild(childExit)||ymcc::deckybinding::fixture::tryRunChild(childExit))return childExit;
    if(argc!=2)return 3;
    using namespace migration;
    Json report{{"schema","ymcc.typed-legacy-runtime-regression.v1"},{"productCallbackMode","typed-only; legacy macro absent"},
        {"fullPersistentAcceptance",false},{"officialLoaderExecuted",false},{"realSteamExecuted",false},{"physicalSleepPerformed",false}};
    const auto start=GetTickCount64();
    report["sharedCryptoCalibration"]=initializeSharedCryptoForMeasurement();
    // Isolate cold self-image process initialization from all Runtime baselines.
    // Both probes use the unchanged original Context; each owned child must exit
    // naturally and its observation handle is closed before measuring the parent.
    report["coldContextCalibrationBefore"]={{"handles",handles()},{"table",handleTable()}};
    for(unsigned i=0;i<2;++i) {
        Json row{{"index",i},{"beforeHandles",handles()},{"before",handleTable()}};
        bool exited=false;DWORD pid=0;
        {
            detail::Handle observer;
            {Context owned;pid=worker::number(owned.lease.at("pid"));observer.value=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);need(observer.value!=nullptr,"cold-context-observer");}
            exited=WaitForSingleObject(observer.value,0)==WAIT_OBJECT_0;
        }
        row["ownedPid"]=pid;row["naturalExit"]=exited;row["afterHandles"]=handles();row["after"]=handleTable();
        report["coldContextCalibration"].push_back(row);
        need(exited,"cold-context-did-not-exit");
        if(i==1)need(row.at("afterHandles")==row.at("beforeHandles"),"context-calibration-not-stable");
    }
    const auto handlesBefore=handles();const auto threadsBefore=threadCount();report["parentHandleSnapshotBefore"]=handleTable();
    const auto run=[&](const char* id,const auto& body) {
        try{body();}catch(const std::exception& e){scenarios.push_back({{"id",id},{"passed",false},{"error",e.what()}});}
        report["stageResources"].push_back({{"id",id},{"handles",handles()},{"threads",threadCount()},{"table",handleTable()}});
    };
    run("current.no-launch-environment-and-state",environmentAndNoLaunch);
    run("current.resource-and-real-owned-binding-cancellation",cancellation);
    run("current.real-owned-generation-token-handoff",handoff);
    run("current.original-identity-negative-controls",negativeControls);
    const auto handlesAfter=handles();const auto threadsAfter=threadCount();report["parentHandleSnapshotAfter"]=handleTable();
    bool okay=assertions.size()==38&&negatives.size()==4&&handlesBefore==handlesAfter&&threadsBefore==threadsAfter;
    for(const auto& row:assertions)if(!row.at("passed").get<bool>())okay=false;
    for(const auto& row:negatives)if(!row.at("passed").get<bool>())okay=false;
    for(const auto& row:scenarios)if(row.contains("passed")&&!row.at("passed").get<bool>())okay=false;
    if(ownedContexts.size()!=2)okay=false;for(const auto& row:ownedContexts)if(!row.at("exitedAfterOriginalContextDestructor").get<bool>())okay=false;
    report["ownedContextExits"]=ownedContexts;report["passed"]=okay;report["assertions"]=assertions;report["scenarios"]=scenarios;report["negativeControls"]=negatives;
    report["elapsedMs"]=GetTickCount64()-start;report["parentHandlesBefore"]=handlesBefore;report["parentHandlesAfter"]=handlesAfter;
    report["parentThreadsBefore"]=threadsBefore;report["parentThreadsAfter"]=threadsAfter;report["cancelApiCalls"]=measured::calls.load();
    std::ofstream(argv[1])<<report.dump(2)<<"\n";
    std::cout<<report.dump()<<std::endl;return okay?0:1;
}





