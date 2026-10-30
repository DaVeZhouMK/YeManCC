// Real production Runtime + Client + Owner. Test peer and inert Loader/CEF
// are source-built copies in this evidence directory; no installed app is used.
#define main ymccBindingClientComponentSuite
#include "decky_binding_client_selftest.cpp"
#undef main
#include "../native/decky_sidebar_runtime.h"
#include <condition_variable>
namespace rtfixture {
using namespace ymcc::deckybinding;
using namespace ymcc::deckybinding::fixture;
using Json=nlohmann::json;
inline bool tryStalledChild(int& exit) {
    const auto behavior=environment(L"YMCC_BINDING_CLIENT_FIXTURE_MODE");
    if(behavior!=L"runtime-stall-cv"&&behavior!=L"runtime-success-checkpoint"&&behavior!=L"runtime-late-result"&&behavior!=L"runtime-success-exit")return false;
    int argc=0;auto** argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)return false;
    const auto classified=ChildChannel::classify(argc,argv);LocalFree(argv);
    if(classified==ChildMode::ordinary)return false;
    ChildChannel channel;ChildMode mode{};
    if(!channel.tryRunCommandLine(GetCommandLineW(),exit,mode)||mode!=ChildMode::worker){exit=87;return true;}
    try {
        const auto incoming=channel.readFrame(7000);need(bool(incoming),"stalled-request-read");
        const std::string text(incoming.payload.begin(),incoming.payload.end());const auto request=worker::parse(text);(void)request;
        detail::Handle entered{OpenEventW(EVENT_MODIFY_STATE,FALSE,environment(L"YMCC_BINDING_CLIENT_FIXTURE_READY").c_str())};
        detail::Handle release{OpenEventW(SYNCHRONIZE,FALSE,environment(L"YMCC_BINDING_CLIENT_FIXTURE_RELEASE").c_str())};
        need(entered.value&&release.value,"stalled-controls");
        if(behavior==L"runtime-success-checkpoint"||behavior==L"runtime-late-result"||behavior==L"runtime-success-exit"){
            detail::Handle context{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,worker::number(request.bootstrap.at("fixtureContextPid")))};
            need(context.value!=nullptr,"checkpoint-context");
            const auto emit=[&](const char* kind,const Json& detail){const Json packet{{"schema",worker::schema},{"id",request.id},{"generation",std::to_string(request.generation)},{"kind",kind},{"detail",detail}};need(bool(channel.writeFrame(bytes(packet.dump()),5000)),"checkpoint-emit");};
            if(request.bootstrap.value("fixtureSessionReject",false)){
                Json rejected{{"ok",false},{"reason","mirror-bootstrap-session-changed"},{"fixtureResultGeneration",std::to_string(request.generation)}};
                if(request.bootstrap.contains("fixtureMutation"))rejected["mutated"]=request.bootstrap.at("fixtureMutation");
                emit("result",rejected);SetEvent(entered.value);exit=2;return true;
            }
            if(behavior==L"runtime-late-result"){
                SetEvent(entered.value);WaitForSingleObject(release.value,INFINITE);
                detail::Handle attempted{OpenEventW(EVENT_MODIFY_STATE,FALSE,(environment(L"YMCC_BINDING_CLIENT_FIXTURE_READY")+L"-late-attempt").c_str())};
                need(attempted.value!=nullptr,"late-attempt-event");SetEvent(attempted.value);
            }
            emit("result",{{"ok",true},{"mutated",true},{"fixtureResultGeneration",std::to_string(request.generation)},{"contextLease",identity(context.value)}});
            emit("diagnostic",{{"fixtureHealthyGeneration",std::to_string(request.generation)}});
            SetEvent(entered.value);
            if(behavior==L"runtime-success-exit"){WaitForSingleObject(release.value,INFINITE);exit=93;return true;}
            channel.waitCancelled(15000);exit=0;return true;
        }

        std::mutex mutex;std::condition_variable condition;bool released=false;
        std::thread fixtureRelease([&]{WaitForSingleObject(release.value,INFINITE);{std::lock_guard lock(mutex);released=true;}condition.notify_all();});
        SetEvent(entered.value);
        {std::unique_lock lock(mutex);condition.wait(lock,[&]{return released;});}
        fixtureRelease.join();exit=0;return true; // No inherited cancellation ever releases this wait.
    }catch(...){exit=91;return true;}
}
struct ContextJob {
    detail::Handle job{CreateJobObjectW(nullptr,nullptr)};
    explicit ContextJob(DWORD pid){
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        need(job.value&&SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"context-job");
        detail::Handle process{OpenProcess(PROCESS_SET_QUOTA|PROCESS_TERMINATE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid)};
        need(process.value&&AssignProcessToJobObject(job.value,process.value),"context-job-assign");
    }
};
inline Json handleTable(){
    struct Entry{HANDLE value;ULONG_PTR handles,pointers;ULONG access,type,attributes,reserved;};
    struct Table{ULONG_PTR count,reserved;Entry entries[1];};
    struct Unicode{USHORT length,capacity;PWSTR value;};
    using QueryProcess=LONG(NTAPI*)(HANDLE,ULONG,PVOID,ULONG,PULONG);
    using QueryObject=LONG(NTAPI*)(HANDLE,ULONG,PVOID,ULONG,PULONG);
    const auto module=GetModuleHandleW(L"ntdll.dll");
    const auto query=reinterpret_cast<QueryProcess>(GetProcAddress(module,"NtQueryInformationProcess"));
    const auto object=reinterpret_cast<QueryObject>(GetProcAddress(module,"NtQueryObject"));
    if(!query||!object)return {{"error","snapshot-api-unavailable"}};
    std::vector<unsigned char> bytes(65536);ULONG required=0;LONG status=0;
    for(unsigned i=0;i<4;++i){status=query(GetCurrentProcess(),51,bytes.data(),static_cast<ULONG>(bytes.size()),&required);if(status>=0)break;if(required<=bytes.size()||required>16*1024*1024)break;bytes.resize(required+4096);}
    if(status<0)return {{"error","snapshot-call"},{"status",static_cast<unsigned long>(status)}};
    const auto* table=reinterpret_cast<const Table*>(bytes.data());
    if(table->count>(bytes.size()-offsetof(Table,entries))/sizeof(Entry))return {{"error","snapshot-bounds"}};
    Json rows=Json::array();
    for(ULONG_PTR i=0;i<table->count;++i){
        alignas(void*) unsigned char typeBytes[4096]{};ULONG size=0;std::string name="unknown";
        if(object(table->entries[i].value,2,typeBytes,sizeof(typeBytes),&size)>=0){const auto* type=reinterpret_cast<const Unicode*>(typeBytes);if(type->length<=512&&type->value){name.clear();for(USHORT n=0;n<type->length/sizeof(wchar_t);++n)name.push_back(static_cast<char>(type->value[n]));}}
        Json row{{"handle",reinterpret_cast<std::uintptr_t>(table->entries[i].value)},{"type",name},{"access",table->entries[i].access}};
        // Only this known retained File-access pattern, at quiescent baseline/
        // post-stop snapshots. Never query names of pending pipe IO handles.
        if(name=="File"&&table->entries[i].access==0x100001u){
            alignas(void*) unsigned char nameBytes[32768]{};ULONG returned=0;
            const auto named=object(table->entries[i].value,1,nameBytes,sizeof(nameBytes),&returned);
            row["nameQueryStatus"]=static_cast<unsigned long>(named);
            if(named>=0){const auto* text=reinterpret_cast<const Unicode*>(nameBytes);if(text->length<=16000&&text->value){std::string value;for(USHORT n=0;n<text->length/sizeof(wchar_t);++n)value.push_back(static_cast<char>(text->value[n]));row["objectName"]=value;}}
        }
        rows.push_back(std::move(row));
    }
    return rows;
}
inline DWORD handles(){DWORD count=0;need(GetProcessHandleCount(GetCurrentProcess(),&count)!=FALSE,"parent-handle-count");return count;}
inline Json initializeSharedCryptoForMeasurement(){
    const auto before=handleTable();const auto beforeCount=handles();
    unsigned char bytes[32]{};
    for(unsigned i=0;i<32;++i)need(BCryptGenRandom(nullptr,bytes,sizeof(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"crypto-calibration-call");
    Json samples=Json::array();const auto start=GetTickCount64();DWORD last=handles();unsigned stable=0;
    for(unsigned i=0;i<50;++i){Sleep(20);const auto count=handles();samples.push_back({{"elapsedMs",GetTickCount64()-start},{"handles",count}});stable=count==last?stable+1:0;last=count;if(GetTickCount64()-start>=500&&stable>=10)break;}
    return {{"scope","Shared CNG cold initialization before any Runtime fixture baseline; not a resource waiver"},
        {"beforeHandles",beforeCount},{"afterHandles",handles()},{"samples",samples},{"before",before},{"after",handleTable()}};
}
template<class F>bool waitFor(F predicate,DWORD ms=5000){const auto until=GetTickCount64()+ms;while(!predicate()&&GetTickCount64()<until)Sleep(2);return predicate();}
struct Harness {
    std::shared_ptr<Client> client=std::make_shared<Client>();
    ymcc::deckysidebar::Runtime runtime;
    std::atomic<unsigned> plans{0},diagnostics{0},preparations{0},results{0},watchFailures{0},planStarts{0},planFinishes{0};std::atomic<std::uint64_t> resultGeneration{0};std::atomic<std::uint64_t> healthyGeneration{0};std::atomic<bool> burstDone{false};std::atomic<DWORD> descendant{0};
    Harness(const std::filesystem::path& home,const Context& context,bool retained=false,std::function<ymcc::deckysidebar::LaunchAdmission()> admission={},std::function<void(Json&,unsigned)> plan={},std::function<std::string()> preflight={}){
        std::filesystem::create_directories(home);
        ymcc::deckysidebar::Config config;config.executable=detail::executable();config.home=home.wstring();
        if(preflight)config.preflight=std::move(preflight);else config.preflight=[]{return std::string{};};config.ownedBinding=client;
        config.prepareEnvironment=[this,retained](const std::function<bool()>& current){need(current(),"current resource preparation");++preparations;if(retained)Sleep(180);return std::string{};};
        if(admission)config.launchAdmission=std::move(admission);else config.launchAdmission=[retained](){return ymcc::deckysidebar::LaunchAdmission{true,{},false,{},retained};};
        const auto cef=worker::number(context.lease.at("pid"));
        config.bindingPlan=[this,cef,plan=std::move(plan)](ymcc::deckysidebar::BindingMode mode,std::uint64_t generation){
            const auto attempt=++plans;auto request=makeRequest(generation);request["bootstrap"]["fixtureContextPid"]=cef;
            if(mode==ymcc::deckysidebar::BindingMode::retained){request["requireFreshContext"]=false;request["retainedFrontendOnly"]=true;request["bootstrap"]["frontendBuildId"]=std::string(64,'d');}
            if(plan)plan(request,attempt);
            return ymcc::deckysidebar::BindingPlan{std::move(request),{}};
        };
        config.bindingDiagnostic=[this](const Json& packet){
            if(packet.value("bootstrapStage",std::string{})=="plan-started")++planStarts;
            if(packet.value("bootstrapStage",std::string{})=="plan-finished"){
                need(packet.contains("elapsedMs")&&packet["elapsedMs"].is_number_unsigned(),"plan-timing-is-not-measured");++planFinishes;
            }
            if(packet.contains("fixtureResultGeneration")){resultGeneration=worker::decimal(packet.at("fixtureResultGeneration"));++results;}
            if(packet.value("bootstrapStage",std::string{})=="failed")++watchFailures;
            if(packet.contains("fixtureHealthyGeneration"))healthyGeneration=worker::decimal(packet.at("fixtureHealthyGeneration"));
            if(packet.contains("index"))++diagnostics;
            if(packet.value("fixtureDone",false))burstDone=true;
            if(packet.contains("descendantPid"))descendant=worker::number(packet.at("descendantPid"));
        };
        runtime.initialize(std::move(config));runtime.observeSteam(1,true);
    }
    // Callbacks capture this; stop the worker before callback state is destroyed.
    ~Harness(){runtime.shutdown();}
    void waitForCheckpoint(const std::function<bool()>& predicate,const char* checkpoint){
        if(waitFor(predicate))return;
        const auto observed=runtime.snapshot();
        const Json evidence{{"phase",observed.phase},{"reason",observed.reason},{"error",observed.error},
            {"launchStage",observed.launchStage},{"launchMode",observed.launchMode},{"loaderPid",observed.pid},
            {"binding",state(client->snapshot())},{"ownerGeneration",client->snapshot().generation},{"bindingEnabled",client->snapshot().enabled},{"bindingCanceled",client->snapshot().canceled},{"bindingRetry",observed.bindingRetry},{"healthyGeneration",healthyGeneration.load()},{"resultGeneration",resultGeneration.load()},{"plans",plans.load()}};
        throw std::runtime_error(std::string(checkpoint)+": "+evidence.dump());
    }
    void start(){need(runtime.setEnabled(true),"runtime-enable");waitForCheckpoint([&]{return runtime.snapshot().pid!=0;},"owned-inert-loader-start");}
};
inline Json stalled(const std::filesystem::path& output,const wchar_t* mode){
    Controls controls(L"success");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    Harness harness(output,context);detail::Handle stopped{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    const auto parentHandlesBefore=handles();const auto handleSnapshotBefore=handleTable();harness.start();
    detail::Handle loader{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,harness.runtime.snapshot().pid)};
    need(loader.value&&WaitForSingleObject(loader.value,0)==WAIT_TIMEOUT,"loader-observer");
    ResetEvent(controls.ready.value);setEnvironment(L"YMCC_BINDING_CLIENT_FIXTURE_MODE",mode);
    need(harness.runtime.requestBindingRefresh(1),"committed-refresh");controls.waitReady();
    if(std::wstring_view(mode)==L"stubborn-descendant")need(waitFor([&]{return harness.descendant.load()!=0;}),"descendant-message-not-drained");
    const auto before=harness.client->snapshot();
    detail::Handle binding{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,before.processId)};
    detail::Handle descendant{harness.descendant?OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,harness.descendant):nullptr};
    need(binding.value&&WaitForSingleObject(binding.value,0)==WAIT_TIMEOUT,"binding-child-live");
    if(descendant.value)need(WaitForSingleObject(descendant.value,0)==WAIT_TIMEOUT,"descendant-not-live");
    std::atomic<bool> returned{false};const auto began=GetTickCount64();
    std::thread stopper([&]{harness.runtime.shutdown();returned=true;SetEvent(stopped.value);});
    const bool completedBeforeRelease=WaitForSingleObject(stopped.value,3200)==WAIT_OBJECT_0;
    const auto elapsed=GetTickCount64()-began;const auto atDeadline=harness.client->snapshot();
    const bool releaseUnsent=controls.releaseUnsent();
    const bool loaderExited=WaitForSingleObject(loader.value,0)==WAIT_OBJECT_0;
    const bool bindingExited=WaitForSingleObject(binding.value,0)==WAIT_OBJECT_0;
    const bool descendantExited=!descendant.value||WaitForSingleObject(descendant.value,0)==WAIT_OBJECT_0;
    const bool cefUnaffected=context.alive();
    const auto plans=harness.plans.load();
    // Measurement is finished. Only now may fixture rescue an uncooperative wait.
    if(!completedBeforeRelease)controls.releaseNow();
    stopper.join();
    const bool lateRefresh=harness.runtime.requestBindingRefresh(2),lateRecovery=harness.runtime.requestTransportRecovery();
    harness.runtime.observeSteam(1,true);harness.runtime.shutdown();
    if(binding.value){CloseHandle(binding.value);binding.value=nullptr;}if(loader.value){CloseHandle(loader.value);loader.value=nullptr;}
    if(descendant.value){CloseHandle(descendant.value);descendant.value=nullptr;}
    const auto parentHandlesAfter=handles();const auto handleSnapshotAfter=handleTable();
    const bool pass=completedBeforeRelease&&releaseUnsent&&returned&&loaderExited&&bindingExited&&descendantExited&&cefUnaffected&&
        atDeadline.fullyDestroyed&&atDeadline.handles==0&&atDeadline.pendingIo==0&&atDeadline.activeCalls==0&&atDeadline.jobActiveProcesses==0&&
        !harness.runtime.snapshot().enabled&&harness.runtime.snapshot().pid==0&&!lateRefresh&&!lateRecovery&&harness.plans==plans&&parentHandlesBefore==parentHandlesAfter;
    return {{"id",std::wstring_view(mode)==L"runtime-stall-cv"?"native.typed-stalled-binding-stop-before-fixture-release":"native.typed-stalled-binding-descendants-stop-before-release"},
        {"passed",pass},{"deadlineMs",3200},{"stopMs",elapsed},{"completedBeforeFixtureRelease",completedBeforeRelease},
        {"fixtureReleaseUnsentAtMeasurement",releaseUnsent},{"atDeadline",state(atDeadline)},{"beforeStop",state(before)},
        {"ownedLoaderExited",loaderExited},{"bindingChildExited",bindingExited},{"descendantExited",descendantExited},{"cefUnaffected",cefUnaffected},
        {"lateRefreshRejected",!lateRefresh},{"lateRecoveryRejected",!lateRecovery},{"parentHandlesBefore",parentHandlesBefore},{"parentHandlesAfter",parentHandlesAfter},{"handleSnapshotBefore",handleSnapshotBefore},{"handleSnapshotAfter",handleSnapshotAfter}};
}
// Field 2026-10-23: a proven no-op session rejection consumed the only retained
// attempt. Replay actual typed worker packets; uncertain writes must stay stopped.
inline Json sessionRevalidation(const std::filesystem::path& output,const std::string& variant,bool retained){
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    std::atomic<bool> deny{false};const bool off=variant=="off",admission=variant=="admission";
    const bool shouldHeal=variant=="false";
    Harness h(output,context,retained,[&]{return deny.load()?ymcc::deckysidebar::LaunchAdmission{false,"steam-session-unconfirmed",false}:ymcc::deckysidebar::LaunchAdmission{true,{},false,{},retained};},
        [&](Json& request,unsigned attempt){
            if(attempt!=1)return;auto& bootstrap=request["bootstrap"];bootstrap["fixtureSessionReject"]=true;
            if(variant=="true")bootstrap["fixtureMutation"]=true;
            else if(variant=="null")bootstrap["fixtureMutation"]=nullptr;
            else if(variant=="string")bootstrap["fixtureMutation"]="false";
            else if(variant=="number")bootstrap["fixtureMutation"]=0;
            else if(variant!="missing")bootstrap["fixtureMutation"]=false;
            if(admission)deny=true;
        });
    const auto before=handles();need(h.runtime.setEnabled(true),"session-revalidation-enable");
    const bool invalidProtocol=variant=="null"||variant=="missing"||variant=="string"||variant=="number";
    need(waitFor([&]{return invalidProtocol?h.runtime.snapshot().phase=="unavailable":h.results>=1;}),"session-rejection-not-consumed");
    const auto began=GetTickCount64();bool healed=false;
    if(off){h.runtime.setEnabled(false);Sleep(400);}
    else if(shouldHeal)healed=waitFor([&]{return h.healthyGeneration>=2;},1500);
    else Sleep(700); // Original first retry is 250 ms. Observe beyond it; never advance a fake clock.
    const auto measured=h.runtime.snapshot();const auto plans=h.plans.load();
    const bool admissionHeld=!admission||measured.phase=="waiting-manual-steam";
    const bool malformedRefused=!invalidProtocol||(h.results==0&&measured.reason!="mirror-bootstrap-session-changed");
    h.runtime.shutdown();const auto destroyed=h.client->snapshot();
    const bool ownership=retained?measured.pid==0:(shouldHeal?measured.pid!=0:measured.pid==0);
    const bool pass=(shouldHeal?(healed&&plans==2):(!healed&&plans==1))&&ownership&&admissionHeld&&malformedRefused&&
        (!off||!measured.enabled)&&destroyed.fullyDestroyed&&destroyed.handles==0&&destroyed.pendingIo==0&&before==handles()&&context.alive();
    return {{"id","native.typed-history-session-"+variant+(retained?"-retained":"-owned")},{"passed",pass},
        {"variant",variant},{"retained",retained},{"bindingPlans",plans},{"recovered",healed},{"measurementMs",GetTickCount64()-began},
        {"phase",measured.phase},{"reason",measured.reason},{"loaderPid",measured.pid},{"afterOff",state(destroyed)},
        {"beforeHandles",before},{"afterHandles",handles()},{"cefUnaffected",context.alive()}};
}
// Historical 2.5-second worker deadlines are not evidence of corrupt bytes.
// Runtime must retry a destroyed worker's preflight without binding or launching
// before it passes, while permanent resource errors and OFF remain terminal.
inline Json resourceRevalidation(const std::filesystem::path& output,const std::string& variant){
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    std::atomic<unsigned> reads{0};std::atomic<bool> deny{false};
    const auto failure=variant=="hash"?"resource-hash-mismatch":variant=="cleanup"?"resource-worker-cleanup-incomplete":"resource-worker-deadline";
    Harness h(output,context,false,[&]{return deny.load()?ymcc::deckysidebar::LaunchAdmission{false,"steam-ui-state-pending",false}:ymcc::deckysidebar::LaunchAdmission{true};},{},[&]{const auto n=++reads;if(variant=="admission")deny=true;return n==1||(variant!="recover"&&variant!="ready")?std::string(failure):std::string{};});
    const auto before=handles();need(h.runtime.setEnabled(true),"resource-revalidation-enable");
    need(waitFor([&]{return h.runtime.snapshot().phase=="unavailable"&&reads==1;}),"resource-rejection-not-published");
    const auto rejected=h.runtime.snapshot();const auto beforePlans=h.plans.load();bool recovered=false;
    if(variant=="off"){h.runtime.setEnabled(false);Sleep(1200);}
    else if(variant=="recover"||variant=="ready"){
        if(variant=="ready"){need(h.runtime.requestPreparationRefresh(2),"resource-ready-not-accepted");need(!h.runtime.requestPreparationRefresh(2),"resource-duplicate-ready-accepted");}
        recovered=waitFor([&]{return h.healthyGeneration>=1;},2200);
    }
    else Sleep(1200);
    const auto measured=h.runtime.snapshot();const auto plans=h.plans.load();const auto inspections=reads.load();h.runtime.shutdown();const auto destroyed=h.client->snapshot();
    const bool pass=beforePlans==0&&rejected.pid==0&&((variant=="recover"||variant=="ready")?(recovered&&inspections==2&&plans==1&&measured.pid!=0):(!recovered&&inspections==1&&plans==0&&measured.pid==0))&&
        (variant!="admission"||measured.phase=="waiting-manual-steam")&&(variant!="off"||!measured.enabled)&&destroyed.fullyDestroyed&&before==handles()&&context.alive();
    return {{"id","native.typed-history-resource-"+variant},{"passed",pass},{"preflightReads",inspections},{"bindingPlansBeforeValidResources",beforePlans},{"bindingPlans",plans},
        {"recovered",recovered},{"phase",measured.phase},{"reason",measured.reason},{"afterOff",state(destroyed)},{"beforeHandles",before},{"afterHandles",handles()},{"cefUnaffected",context.alive()}};
}
// Two historical families interleave: resource deadline -> unavailable UI mode
// -> verified mode returns. A mode handoff must preserve the already opened
// resource retry budget without spending it while permission is absent.
inline Json resourceAdmissionHandoff(const std::filesystem::path& output,const std::string& variant){
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    std::atomic<unsigned> reads{0};std::atomic<bool> deny{false};std::atomic<ULONGLONG> firstFailure{0};
    Harness h(output,context,false,[&]{return deny.load()?ymcc::deckysidebar::LaunchAdmission{false,"steam-ui-state-pending",variant=="automatic"}:ymcc::deckysidebar::LaunchAdmission{true};},{},[&]{
        const auto n=++reads;
        if(n==1){firstFailure=GetTickCount64();deny=true;return std::string("resource-worker-deadline");}
        if(n==2)return std::string(variant=="reject"?"resource-hash-mismatch":"resource-worker-deadline");
        return std::string{};
    });
    const auto before=handles();need(h.runtime.setEnabled(true),"resource-handoff-enable");
    need(waitFor([&]{return h.runtime.snapshot().phase=="waiting-manual-steam";},2200),"resource-handoff-did-not-defer");
    const auto deniedReads=reads.load(),deniedPlans=h.plans.load();deny=false;
    if(variant!="automatic")need(h.runtime.requestLaunchAdmissionRefresh(),"resource-handoff-current-mode-not-admitted");
    need(waitFor([&]{return reads>=2&&h.runtime.snapshot().phase=="unavailable";}),"resource-handoff-second-preflight-missing");
    bool healed=false;
    if(variant=="recover"||variant=="automatic")healed=waitFor([&]{return h.healthyGeneration>=1;},3500);
    else if(variant=="off"){h.runtime.shutdown();Sleep(2200);}
    else Sleep(2200);
    const auto elapsed=GetTickCount64()-firstFailure.load();const auto phase=h.runtime.snapshot();const auto inspections=reads.load(),plans=h.plans.load();
    const bool pairedPlanDiagnostics=h.planStarts==plans&&h.planFinishes==plans;
    h.runtime.shutdown();const auto destroyed=h.client->snapshot();
    const bool expected=(variant=="recover"||variant=="automatic")?healed&&inspections==3&&plans==1&&phase.pid!=0&&elapsed>=2900:
        !healed&&inspections==2&&plans==0&&phase.pid==0&&(variant!="off"||!phase.enabled);
    return {{"id","native.typed-history-resource-handoff-"+variant},{"passed",deniedReads==1&&deniedPlans==0&&expected&&pairedPlanDiagnostics&&
        destroyed.fullyDestroyed&&destroyed.handles==0&&destroyed.pendingIo==0&&before==handles()&&context.alive()},
        {"variant",variant},{"preflightReads",inspections},{"bindingPlans",plans},{"readsWhileDenied",deniedReads},{"plansWhileDenied",deniedPlans},
        {"recovered",healed},{"pairedPlanDiagnostics",pairedPlanDiagnostics},{"elapsedFromFirstFailureMs",elapsed},{"phase",phase.phase},{"reason",phase.reason},
        {"afterOff",state(destroyed)},{"beforeHandles",before},{"afterHandles",handles()},{"cefUnaffected",context.alive()}};
}
inline Json rejectedFinalAdmission(const std::filesystem::path& output){
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    std::atomic<unsigned> admissions{0};Harness* owner=nullptr;
    Harness harness(output,context,false,[&]{++admissions;return owner&&owner->plans>0?ymcc::deckysidebar::LaunchAdmission{false,"steam-ui-state-pending",true}:ymcc::deckysidebar::LaunchAdmission{true};});owner=&harness;
    const auto before=handles();need(harness.runtime.setEnabled(true),"deferred-enable");
    need(waitFor([&]{const auto state=harness.runtime.snapshot();return state.phase=="waiting-manual-steam"&&state.reason=="steam-ui-state-pending";}),"final-admission-did-not-defer");
    const auto waiting=harness.client->snapshot();const auto phase=harness.runtime.snapshot();
    harness.runtime.shutdown();const auto after=harness.client->snapshot();
    return {{"id","native.typed-rejected-final-admission-releases-watch-before-off"},
        {"passed",admissions>=2&&harness.plans==1&&phase.pid==0&&waiting.fullyDestroyed&&waiting.handles==0&&waiting.pendingIo==0&&after.fullyDestroyed&&before==handles()&&context.alive()},
        {"admissionReads",admissions.load()},{"completedBindingPlans",harness.plans.load()},{"loaderPid",phase.pid},{"beforeOff",state(waiting)},{"afterOff",state(after)},
        {"beforeHandles",before},{"afterHandles",handles()},{"cefUnaffected",context.alive()}};
}
// Final admission can disappear AFTER successful binding IO. Waiting UI must
// retain its existing retryable permission check, without an extra window event.
inline Json finalAdmissionRecovery(const std::filesystem::path& output,const std::string& variant){
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    std::atomic<bool> pending{true};Harness* owner=nullptr;
    Harness h(output,context,false,[&]{
        if(!owner||owner->plans==0)return ymcc::deckysidebar::LaunchAdmission{true};
        if(variant=="retained")return ymcc::deckysidebar::LaunchAdmission{true,"retained-frontend-rebind",false,{},true};
        return pending.load()?ymcc::deckysidebar::LaunchAdmission{false,"steam-ui-state-pending",variant!="terminal"}:ymcc::deckysidebar::LaunchAdmission{true};
    });owner=&h;
    const auto before=handles();need(h.runtime.setEnabled(true),"final-admission-recovery-enable");
    need(waitFor([&]{return h.runtime.snapshot().phase=="waiting-manual-steam";}),"final-admission-not-deferred");
    const auto waiting=h.client->snapshot();const auto waitState=h.runtime.snapshot();const bool inactive=waiting.fullyDestroyed&&waiting.handles==0;
    if(variant=="automatic")pending=false; // Deliberately no requestLaunchAdmissionRefresh().
    bool recovered=false;
    if(variant=="automatic"||variant=="retained")recovered=waitFor([&]{return h.healthyGeneration>=2;},2200);
    else if(variant=="off"){h.runtime.shutdown();Sleep(1200);}
    else Sleep(1200);
    const auto measured=h.runtime.snapshot();const auto plans=h.plans.load();h.runtime.shutdown();const auto destroyed=h.client->snapshot();
    const bool expected=(variant=="automatic"||variant=="retained")?
        recovered&&plans==2&&(variant=="retained"?measured.pid==0&&measured.phase=="frontend-attached":measured.pid!=0&&measured.phase=="loader-started"):
        !recovered&&plans==1&&measured.pid==0&&(variant!="off"||!measured.enabled);
    return {{"id","native.typed-final-admission-"+variant},{"passed",inactive&&expected&&destroyed.fullyDestroyed&&before==handles()&&context.alive()},
        {"variant",variant},{"waitingRetryScheduled",waitState.admissionRetry},{"bindingPlans",plans},{"recovered",recovered},
        {"phase",measured.phase},{"loaderPid",measured.pid},{"inactiveBeforeRetry",inactive},{"cefUnaffected",context.alive()},
        {"afterOff",state(destroyed)},{"beforeHandles",before},{"afterHandles",handles()}};
}
inline Json rejectedAfterPreparation(const std::filesystem::path& output){
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    std::atomic<unsigned> admissions{0};
    Harness harness(output,context,false,[&]{return ++admissions==1?ymcc::deckysidebar::LaunchAdmission{true}:ymcc::deckysidebar::LaunchAdmission{false,"steam-ui-state-pending",true};});
    const auto before=handles();need(harness.runtime.setEnabled(true),"preparation-revalidation-enable");
    need(waitFor([&]{const auto v=harness.runtime.snapshot();return v.phase=="waiting-manual-steam"&&v.reason=="steam-ui-state-pending";}),"preparation must revalidate admission before binding");
    const auto prepared=harness.preparations.load(),plans=harness.plans.load();const auto waiting=harness.client->snapshot();const auto phase=harness.runtime.snapshot();harness.runtime.shutdown();const auto after=harness.client->snapshot();
    return {{"id","native.typed-preparation-rechecks-admission-before-binding"},{"passed",admissions>=2&&prepared==1&&plans==0&&phase.pid==0&&waiting.fullyDestroyed&&after.fullyDestroyed&&before==handles()&&context.alive()},
        {"admissionReads",admissions.load()},{"resourcePreparations",prepared},{"bindingPlans",plans},{"loaderPid",phase.pid},{"afterOff",state(after)},{"beforeHandles",before},{"afterHandles",handles()},{"cefUnaffected",context.alive()}};
}
inline Json ready(const std::filesystem::path& output,bool retained){
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    Harness harness(output,context,retained);const auto before=handles();
    const auto initialBegin=GetTickCount64();
    need(harness.runtime.setEnabled(true),"ready-enable");
    harness.waitForCheckpoint([&]{return harness.healthyGeneration==1;},"initial-binding-checkpoint");
    const auto initialElapsedMs=GetTickCount64()-initialBegin;
    const auto initial=harness.runtime.snapshot();const auto originalLoader=initial.pid;
    const auto originalBinding=harness.client->snapshot().processId;
    need(retained?(initial.phase=="frontend-attached"&&originalLoader==0):(initial.phase=="loader-started"&&originalLoader!=0),"initial-loader-ownership");
    const auto preparedBefore=harness.preparations.load();
    bool preparationAccepted=true;
    for(unsigned long long generation=1;generation<=1024;++generation)preparationAccepted=harness.runtime.requestPreparationRefresh(generation)&&preparationAccepted;
    Sleep(60); // Give the existing worker the preparation-only wake before any explicit rebind.
    const auto maintenanceState=harness.runtime.snapshot();
    const bool connectionUnaffected=harness.plans==1&&harness.healthyGeneration==1&&harness.client->snapshot().processId==originalBinding&&maintenanceState.phase==initial.phase&&maintenanceState.pid==originalLoader&&context.alive();
    const bool accepted=harness.runtime.requestBindingRefresh(9);
    const bool duplicate=harness.runtime.requestBindingRefresh(9),zero=harness.runtime.requestBindingRefresh(0);
    need(waitFor([&]{return harness.healthyGeneration==2;}),"ready-binding-checkpoint");
    const auto after=harness.runtime.snapshot();const auto nextBinding=harness.client->snapshot().processId;
    const bool sameOwnership=after.pid==originalLoader&&after.phase==initial.phase&&context.alive();
    harness.runtime.shutdown();const auto destroyed=harness.client->snapshot();
    return {{"id",retained?"native.typed-retained-ready-rebind-never-starts-loader":"native.typed-ready-rebind-keeps-original-owned-loader"},
        {"passed",accepted&&!duplicate&&!zero&&preparationAccepted&&connectionUnaffected&&preparedBefore==(retained?0u:1u)&&harness.preparations==preparedBefore&&harness.plans==2&&harness.healthyGeneration==2&&sameOwnership&&nextBinding&&nextBinding!=originalBinding&&destroyed.fullyDestroyed&&before==handles()},
        {"acceptedReady",accepted},{"duplicateReadyRejected",!duplicate},{"zeroReadyRejected",!zero},{"plans",harness.plans.load()},
        {"resourcePreparationAccepted",preparationAccepted},{"resourcePreparationStormSignals",1024},{"initialReadyElapsedMs",initialElapsedMs},{"initialRetainedResourcePreparationSkipped",retained&&preparedBefore==0},{"resourceOnlyWakePreservedConnection",connectionUnaffected},{"preparationsBefore",preparedBefore},{"preparationsAfter",harness.preparations.load()},{"healthyResourceMaintenanceDeferred",harness.preparations==preparedBefore},
        {"initialLoaderPid",originalLoader},{"afterLoaderPid",after.pid},{"initialBindingPid",originalBinding},{"nextBindingPid",nextBinding},
        {"originalCefAlive",context.alive()},{"beforeHandles",before},{"afterHandles",handles()},{"atExit",state(destroyed)}};
}
// Only OS/session boundaries are memory adapters. The recovery function is
// extracted verbatim from main.cpp and drives the real Runtime/Client/Owner.
namespace wakeReady {
static ymcc::deckysidebar::Runtime* runtime=nullptr;
#define g_deckySidebarRuntime (*runtime)
enum class PowerLifecycle {Ready,Suspended,Resuming};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Suspended};
static std::atomic<bool> g_exitRequested{false};
static struct {bool isolated=false;} g_aiFanMockSession;
static unsigned long long observedGeneration=177;
static unsigned long long currentPowerGeneration(){return observedGeneration;}
struct Session {unsigned pid=41;unsigned long long created=123;std::wstring root=L"C:\\FixtureSteam";};
static struct {Session sessionSnapshot()const{return {};}} g_steamSettingsObserver;
static struct {unsigned reads=0;void requestRefresh(const Session&){++reads;}} g_steamModeObserver;
static Json diagnostics=Json::array();
static void deckySidebarDiagnostic(const char* event,const Json& detail,bool){diagnostics.push_back({{"event",event},{"detail",detail}});}
#include "decky_wake_ready_consumer.inc"
#undef g_deckySidebarRuntime
}
// WM_POWER_RESUME_COMMIT calls this same production consumer with the current
// power generation; the runner pins and extracts that exact main.cpp call.
inline bool resume(ymcc::deckysidebar::Runtime& runtime,unsigned long long generation){
    return runtime.requestBindingRefresh(generation);
}
inline Json runtimeState(Harness& h){
    const auto r=h.runtime.snapshot();const auto c=h.client->snapshot();
    return {{"phase",r.phase},{"reason",r.reason},{"enabled",r.enabled},{"loaderPid",r.pid},
        {"bindingRetry",r.bindingRetry},{"bindingPid",c.processId},{"ownerGeneration",c.generation},
        {"binding",state(c)},{"resultGeneration",h.resultGeneration.load()},{"healthyGeneration",h.healthyGeneration.load()},{"plans",h.plans.load()}};
}
inline bool isHealthy(Harness& h,bool retained,std::uint64_t generation){
    const auto r=h.runtime.snapshot();const auto c=h.client->snapshot();
    return h.healthyGeneration==generation&&h.resultGeneration==generation&&c.generation!=0&&
        c.enabled&&!c.canceled&&c.processId&&!c.childExited&&!c.fullyDestroyed&&!r.bindingRetry&&
        r.enabled&&(retained?(r.phase=="frontend-attached"&&r.reason=="retained-frontend-connected"&&!r.pid):
        (r.phase=="loader-started"&&r.reason=="mirror-handshake-pending"&&r.pid!=0));
}
inline Json intentReady(const std::filesystem::path& output,bool retained){
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    Harness h(output,context,retained);const auto beforeHandles=handles();need(h.runtime.setEnabled(true),"intent-ready-enable");h.waitForCheckpoint([&]{return isHealthy(h,retained,1);},"intent-ready-initial");
    wakeReady::runtime=&h.runtime;wakeReady::observedGeneration=retained?178:177;const auto power=wakeReady::observedGeneration;
    const auto loader=h.runtime.snapshot().pid;const auto plans=h.plans.load(),preparations=h.preparations.load(),reads=wakeReady::g_steamModeObserver.reads;
    for(auto phase:{wakeReady::PowerLifecycle::Suspended,wakeReady::PowerLifecycle::Resuming}){wakeReady::g_powerLifecycle=phase;need(!wakeReady::deckySidebarResumeAfterIntent(power),"nonready S0 recovery admitted");}
    wakeReady::g_powerLifecycle=wakeReady::PowerLifecycle::Ready;
    need(!wakeReady::deckySidebarResumeAfterIntent(0)&&!wakeReady::deckySidebarResumeAfterIntent(power-1),"stale power generation admitted");
    wakeReady::g_exitRequested=true;need(!wakeReady::deckySidebarResumeAfterIntent(power),"exit admitted wake recovery");wakeReady::g_exitRequested=false;
    wakeReady::g_aiFanMockSession.isolated=true;need(!wakeReady::deckySidebarResumeAfterIntent(power),"isolated wake reached IO");wakeReady::g_aiFanMockSession.isolated=false;
    need(h.plans==plans&&wakeReady::g_steamModeObserver.reads==reads,"invalid wake touched connection");
    need(wakeReady::deckySidebarResumeAfterIntent(power),"committed S0 Ready ignored");
    h.waitForCheckpoint([&]{return isHealthy(h,retained,2);},"S0 Ready must replace stale binding and reach healthy new snapshot");
    for(unsigned i=0;i<512;++i)need(!wakeReady::deckySidebarResumeAfterIntent(power),"duplicate S0 notification refilled recovery");
    need(h.plans==plans+1&&wakeReady::g_steamModeObserver.reads==reads+1&&h.runtime.snapshot().pid==loader,"wake storm restarts Loader or creates duplicate binding/mode reads");
    const auto after=runtimeState(h);h.runtime.setEnabled(false);++wakeReady::observedGeneration;need(!wakeReady::deckySidebarResumeAfterIntent(wakeReady::observedGeneration),"OFF revived on late wake");
    wakeReady::runtime=nullptr;
    return {{"id",retained?"native.typed-s0-intent-ready-retained":"native.typed-s0-intent-ready-owned"},{"passed",true},{"productionConsumerExtracted",true},
        {"powerGeneration",power},{"plansBefore",plans},{"plansAfter",h.plans.load()},{"modeReads",wakeReady::g_steamModeObserver.reads-reads},
        {"duplicateSignals",512},{"preparationsBefore",preparations},{"preparationsAfter",h.preparations.load()},{"loaderPidUnchanged",true},{"cefAlive",context.alive()},{"after",after},
        {"atExit",state(h.client->snapshot())},{"beforeHandles",beforeHandles},{"afterHandles",handles()}};
}
inline Json healing(const std::filesystem::path& output,bool retained,const std::string& boundary){
    const bool childExit=boundary=="child-exit",late=boundary=="late-result",off=boundary=="off";
    Controls controls(childExit?L"runtime-success-exit":L"runtime-success-checkpoint");
    Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    detail::Handle attempted{CreateEventW(nullptr,TRUE,FALSE,(controls.readyName+L"-late-attempt").c_str())};
    Harness h(output,context,retained);const auto beforeHandles=handles();
    need(h.runtime.setEnabled(true),"healing-enable");
    h.waitForCheckpoint([&]{return isHealthy(h,retained,1);},"initial-healthy-snapshot");
    const auto initial=runtimeState(h);const auto originalLoader=h.runtime.snapshot().pid;
    const auto originalBinding=h.client->snapshot().processId;const auto initialEpoch=h.client->snapshot().epoch;const auto initialOwnerGeneration=h.client->snapshot().generation;
    const auto prepared=h.preparations.load();bool accepted=true,duplicateRejected=true,zeroRejected=true,lateAttempt=false;
    Json middle=Json::object();std::uint64_t target=2;
    if(childExit){
        detail::Handle old{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,originalBinding)};
        need(old.value!=nullptr,"failed-binding-observer");
        setEnvironment(L"YMCC_BINDING_CLIENT_FIXTURE_MODE",L"runtime-success-checkpoint");controls.releaseNow();
        need(WaitForSingleObject(old.value,5000)==WAIT_OBJECT_0,"binding-child-did-not-exit");
        DWORD code=0;need(GetExitCodeProcess(old.value,&code)&&code==93,"binding-failure-exit-code");
        middle={{"bindingChildExitCode",code},{"stateAfterExit",runtimeState(h)}};
        h.waitForCheckpoint([&]{return isHealthy(h,retained,2);},"automatic-child-failure-recovery");
    }else if(late||off){
        ResetEvent(controls.ready.value);setEnvironment(L"YMCC_BINDING_CLIENT_FIXTURE_MODE",L"runtime-late-result");
        accepted=resume(h.runtime,100);need(accepted,"first-resume-rejected");controls.waitReady();
        middle=runtimeState(h);setEnvironment(L"YMCC_BINDING_CLIENT_FIXTURE_MODE",L"runtime-success-checkpoint");
        if(off){
            std::atomic<bool> stopped{false};std::thread stopper([&]{h.runtime.shutdown();stopped=true;});
            const bool disabled=waitFor([&]{return !h.runtime.snapshot().enabled;});
            controls.releaseNow();stopper.join();
            lateAttempt=WaitForSingleObject(attempted.value,0)==WAIT_OBJECT_0;
            const bool resumeRejected=!resume(h.runtime,101),recoveryRejected=!h.runtime.requestTransportRecovery();
            const auto after=runtimeState(h);Sleep(350);const auto c=h.client->snapshot();
            return {{"id","native.typed-healing-off-beats-late-resume-result"},
                {"passed",disabled&&stopped&&lateAttempt&&resumeRejected&&recoveryRejected&&!h.runtime.snapshot().enabled&&
                    h.runtime.snapshot().phase=="disabled"&&h.plans==2&&h.results==1&&c.fullyDestroyed&&beforeHandles==handles()&&context.alive()},
                {"initial",initial},{"pending",middle},{"afterOff",after},{"lateResultAttempted",lateAttempt},
                {"resumeAfterOffRejected",resumeRejected},{"recoveryAfterOffRejected",recoveryRejected},
                {"acceptedResults",h.results.load()},{"plansAfterQuiet",h.plans.load()},{"atExit",state(c)},{"afterHandles",handles()},{"beforeHandles",beforeHandles}};
        }
        accepted=resume(h.runtime,101)&&accepted;
        duplicateRejected=!resume(h.runtime,101);zeroRejected=!resume(h.runtime,0);
        controls.releaseNow();lateAttempt=WaitForSingleObject(attempted.value,5000)==WAIT_OBJECT_0;target=3;
        h.waitForCheckpoint([&]{return isHealthy(h,retained,3);},"new-epoch-after-late-result");
    }else{
        // Both event orderings must settle on one current healthy watch.
        if(boundary=="resume-then-disconnect")accepted=resume(h.runtime,100)&&h.runtime.requestTransportRecovery();
        else accepted=h.runtime.requestTransportRecovery()&&resume(h.runtime,100);
        for(unsigned i=0;i<32;++i)duplicateRejected=!resume(h.runtime,100)&&duplicateRejected;
        zeroRejected=!resume(h.runtime,0);
        need(accepted,"interleaved-resume-rejected");
        h.waitForCheckpoint([&]{return isHealthy(h,retained,2);},"resume-disconnect-current-epoch");
        duplicateRejected=!resume(h.runtime,100)&&duplicateRejected;
        middle=runtimeState(h);
    }
    const auto recovered=runtimeState(h);const auto epoch=h.client->snapshot().epoch;
    const auto bindingPid=h.client->snapshot().processId;const auto ownerGeneration=h.client->snapshot().generation;const auto plans=h.plans.load();
    // Covers the first 250ms connection retry deadline, plus scheduler margin.
    Sleep(650);const auto quiet=runtimeState(h);
    const bool quietHealthy=isHealthy(h,retained,target)&&h.plans==plans&&h.client->snapshot().processId==bindingPid;
    const bool ownerStable=h.runtime.snapshot().pid==originalLoader&&context.alive()&&h.preparations==prepared;
    const bool generationFence=childExit?epoch==initialEpoch:epoch>initialEpoch;
    const bool acceptedOnlyCurrent=late?h.results==2:h.results==target;
    const bool failureObserved=!childExit||h.watchFailures>0;
    h.runtime.shutdown();const auto destroyed=h.client->snapshot();
    return {{"id","native.typed-healing-"+boundary+(retained?"-retained":"-owned")},
        {"passed",accepted&&duplicateRejected&&zeroRejected&&generationFence&&acceptedOnlyCurrent&&failureObserved&&
            (!late||lateAttempt)&&plans==target&&quietHealthy&&ownerStable&&bindingPid!=originalBinding&&ownerGeneration>initialOwnerGeneration&&
            destroyed.fullyDestroyed&&beforeHandles==handles()},
        {"initial",initial},{"interleaved",middle},{"recovered",recovered},{"afterQuiet",quiet},
        {"resumeAccepted",accepted},{"duplicateResumeRejected",duplicateRejected},{"zeroResumeRejected",zeroRejected},
        {"lateResultAttempted",lateAttempt},{"acceptedResults",h.results.load()},{"watchFailures",h.watchFailures.load()},
        {"currentEpochHealthy",generationFence&&quietHealthy},{"quietObservationMs",650},{"noDuplicateWiring",quietHealthy},
        {"loaderAndCefPreserved",ownerStable},{"atExit",state(destroyed)},{"beforeHandles",beforeHandles},{"afterHandles",handles()}};
}
inline Json healthy(const std::filesystem::path& output){
    Controls controls(L"burst");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    Harness harness(output,context);const auto beforeHandles=handles();const auto handleSnapshotBefore=handleTable();const auto beforeThreads=threadCount();harness.start();
    const bool drained=waitFor([&]{return harness.burstDone.load();});const auto activeThreads=threadCount();
    const auto active=harness.client->snapshot();harness.runtime.shutdown();
    const auto remaining=harness.client->snapshot();const bool cefAlive=context.alive();
    return {{"id","native.typed-runtime-drains-live-watch-without-extra-reader-thread"},
        {"passed",drained&&harness.diagnostics==192&&remaining.fullyDestroyed&&beforeHandles==handles()&&activeThreads==beforeThreads+1&&cefAlive},
        {"diagnosticFrames",harness.diagnostics.load()},{"watchDrained",drained},{"beforeThreads",beforeThreads},{"activeThreads",activeThreads},
        {"beforeHandles",beforeHandles},{"afterHandles",handles()},{"handleSnapshotBefore",handleSnapshotBefore},{"handleSnapshotAfter",handleTable()},{"active",state(active)},{"atExit",state(remaining)},{"cefUnaffected",cefAlive}};
}
}
int main(int argc,char** argv){
    int code=0;if(rtfixture::tryStalledChild(code))return code;if(ymcc::deckybinding::fixture::tryRunChild(code))return code;
    if(argc==1){Sleep(30000);return 0;} // Only Runtime's inert, owned Loader stand-in.
    if(argc!=2&&argc!=3)return 64;
    using namespace rtfixture;const auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);
    const auto sharedCryptoInitialization=initializeSharedCryptoForMeasurement();
    const std::string selection=argc==3?argv[2]:"";
    Json cases=Json::array();
    const auto selected=[&](const char* name){
        if(selection.empty())return true;
        for(std::size_t pos=0;pos<selection.size();){
            const auto end=selection.find(',',pos);const auto part=selection.substr(pos,end==std::string::npos?end:end-pos);
            if(!part.empty()&&std::string(name).find(part)!=std::string::npos)return true;
            if(end==std::string::npos)break;pos=end+1;
        }
        return false;
    };
    const auto test=[&](const char* name,const auto& run){if(!selected(name))return;try{cases.push_back(run());}catch(const std::exception& error){cases.push_back({{"id",name},{"passed",false},{"error",error.what()}});}};
    for(const auto variant:{"automatic","retained","off","terminal"}){
        const auto name=std::string("native.typed-final-admission-")+variant;
        test(name.c_str(),[&]{return finalAdmissionRecovery(output/(std::string("final-")+variant),variant);});
    }
    for(const auto variant:{"recover","automatic","reject","off"}){
        const auto name=std::string("native.typed-history-resource-handoff-")+variant;
        test(name.c_str(),[&]{return resourceAdmissionHandoff(output/(std::string("handoff-")+variant),variant);});
    }
    for(const auto variant:{"recover","ready","off","hash","cleanup","admission"}){
        const auto name=std::string("native.typed-history-resource-")+variant;
        test(name.c_str(),[&]{return resourceRevalidation(output/(std::string("resource-")+variant),variant);});
    }
    for(const auto retained:{false,true})test(retained?"native.typed-history-session-false-retained":"native.typed-history-session-false-owned",[&]{return sessionRevalidation(output/(retained?"session-false-retained":"session-false-owned"),"false",retained);});
    for(const auto variant:{"true","null","missing","string","number","off","admission"}){
        const auto name=std::string("native.typed-history-session-")+variant+"-retained";
        test(name.c_str(),[&]{return sessionRevalidation(output/(std::string("session-")+variant),variant,true);});
    }
    test("native.typed-runtime-live-watch",[&]{return healthy(output/"healthy");});
    test("native.typed-stalled-binding-stop",[&]{return stalled(output/"stalled-cv",L"runtime-stall-cv");});
    test("native.typed-stalled-binding-descendants",[&]{return stalled(output/"stalled-descendants",L"stubborn-descendant");});
    test("native.typed-rejected-final-admission",[&]{return rejectedFinalAdmission(output/"final-admission");});
    test("native.typed-preparation-revalidation",[&]{return rejectedAfterPreparation(output/"preparation-admission");});
    test("native.typed-s0-intent-ready-owned",[&]{return intentReady(output/"intent-ready-owned",false);});
    test("native.typed-s0-intent-ready-retained",[&]{return intentReady(output/"intent-ready-retained",true);});
    test("native.typed-ready-rebind",[&]{return ready(output/"ready",false);});
    test("native.typed-retained-ready-rebind",[&]{return ready(output/"retained-ready",true);});
    test("native.typed-healing-resume-disconnect-owned",[&]{return healing(output/"resume-owned",false,"resume-disconnect");});
    test("native.typed-healing-resume-disconnect-retained",[&]{return healing(output/"resume-retained",true,"resume-disconnect");});
    test("native.typed-healing-resume-then-disconnect-owned",[&]{return healing(output/"resume-first-owned",false,"resume-then-disconnect");});
    test("native.typed-healing-late-result-owned",[&]{return healing(output/"late-owned",false,"late-result");});
    test("native.typed-healing-off",[&]{return healing(output/"off",false,"off");});
    test("native.typed-healing-child-exit-owned",[&]{return healing(output/"exit-owned",false,"child-exit");});
    test("native.typed-healing-child-exit-retained",[&]{return healing(output/"exit-retained",true,"child-exit");});
    const auto passed=std::count_if(cases.begin(),cases.end(),[](const Json& row){return row.value("passed",false);});
    const Json result{{"schema","ymcc.typed-binding-runtime-gate.v1"},{"passed",!cases.empty()&&static_cast<std::size_t>(passed)==cases.size()},
        {"selection",selection},{"tests",cases.size()},{"passedCount",passed},{"cases",cases},{"deadlineMs",3200},{"productCallbackMode","typed-only; legacy macro absent"},
        {"sharedCryptoInitialization",sharedCryptoInitialization},
        {"realSteamOrDeviceActions",0},{"originalHistoricalFailurePreserved",true},{"fieldWakeOrS4Verified",false},{"hostPackagePassed",false}};
    std::ofstream(output.parent_path()/"typed-runtime-result.json",std::ios::binary)<<result.dump(2)<<'\n';
    std::cout<<result.dump(2)<<'\n';return result.at("passed").get<bool>()?0:1;
}
