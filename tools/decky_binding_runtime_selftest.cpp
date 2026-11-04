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
    std::atomic<unsigned> tempFacts{0},tempUnits{0},globalConnected{0};std::atomic<bool> tempShort{false};
    Json globalRecoveryLog=Json::array(); // Worker-only writes; read after shutdown joins.
    Harness(const std::filesystem::path& home,const Context& context,bool retained=false,std::function<ymcc::deckysidebar::LaunchAdmission()> admission={},std::function<void(Json&,unsigned)> plan={},std::function<std::string()> preflight={},std::function<void()> recovery={},std::function<DWORD()> recoveryTimeout={},std::function<ymcc::deckysidebar::ConnectionHealth()> health={},std::function<Json(ymcc::deckysidebar::ConnectionRecoveryWindow::Action)> step={}){
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
        config.connectionRecovery=std::move(recovery);config.connectionRecoveryTimeout=std::move(recoveryTimeout);config.connectionHealth=std::move(health);config.connectionStep=std::move(step);
        config.bindingDiagnostic=[this](const Json& packet){
            if(packet.value("scope",std::string{})=="global-connection"){
                globalRecoveryLog.push_back(packet);if(packet.value("recoveryPhase",std::string{})=="connected")++globalConnected;
            }
            if(packet.value("scope",std::string{})=="loader-environment"&&packet.value("bootstrapStage",std::string{})=="loader-temp-prepared"){
                tempUnits=packet.at("tempPathUnits").get<unsigned>();tempShort=packet.at("tempShortRoot").get<bool>();++tempFacts;
            }
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
// The first resource read may pass, then a transient pre-write bootstrap
// refusal defers launch. The second read after that delay needs the SAME
// resource-retry/terminal/OFF rules; a healthy watch is not a started Loader.
inline Json deferredResourceRevalidation(const std::filesystem::path& output,const std::string& variant){
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    std::atomic<unsigned> reads{0};std::atomic<bool> deny{false};std::atomic<ULONGLONG> failedAt{0};
    const std::string failure=variant=="hash"?"resource-hash-mismatch":variant=="cleanup"?"resource-worker-cleanup-incomplete":"resource-worker-deadline";
    Harness h(output,context,false,[&]{return deny.load()?ymcc::deckysidebar::LaunchAdmission{false,"steam-ui-state-pending",false}:ymcc::deckysidebar::LaunchAdmission{true};},
        [](Json& request,unsigned attempt){if(attempt==1){request["bootstrap"]["fixtureSessionReject"]=true;request["bootstrap"]["fixtureMutation"]=false;}},
        [&]{const auto n=++reads;if(n==2){failedAt=GetTickCount64();if(variant=="admission")deny=true;return failure;}if(n==3&&variant=="recover")return failure;return std::string{};});
    const auto before=handles();need(h.runtime.setEnabled(true),"deferred-resource-enable");
    need(waitFor([&]{const auto s=h.runtime.snapshot();return reads>=2&&((s.phase=="unavailable"&&s.reason==failure)||(variant=="admission"&&s.phase=="waiting-manual-steam"));}),"deferred-resource-rejection-not-observed");
    const auto rejected=h.runtime.snapshot();const auto rejectedPlans=h.plans.load();const auto rejectedBinding=h.client->snapshot();bool healed=false;
    if(variant=="off"){h.runtime.setEnabled(false);Sleep(1200);}
    else if(variant=="recover"||variant=="ready"){
        if(variant=="ready"){need(h.runtime.requestPreparationRefresh(2),"deferred-resource-ready-not-accepted");need(!h.runtime.requestPreparationRefresh(2),"deferred-resource-duplicate-ready-accepted");}
        // A second timeout must continue at the original 3-second OFFSET,
        // measured from window opening, not a fresh 1-second window.
        if(variant=="recover")need(waitFor([&]{return reads>=3;},2200),"deferred-resource-second-timeout-missing");
        healed=waitFor([&]{return h.runtime.snapshot().pid!=0&&h.plans>=3&&h.healthyGeneration==h.resultGeneration;},variant=="recover"?3500:2500);
    }else Sleep(1200);
    const auto measured=h.runtime.snapshot();const auto inspections=reads.load(),plans=h.plans.load();const auto elapsed=GetTickCount64()-failedAt.load();
    const auto stopStarted=GetTickCount64();h.runtime.shutdown();const auto stopMs=GetTickCount64()-stopStarted;const auto remaining=h.client->snapshot();
    const bool recover=variant=="recover"||variant=="ready";
    const bool expected=recover?(healed&&inspections==(variant=="recover"?4u:3u)&&plans==3&&measured.pid!=0&&(variant!="recover"||elapsed>=2900)):(inspections==2&&plans==2&&measured.pid==0&&(variant!="off"||!measured.enabled));
    return {{"passed",expected&&rejectedPlans==2&&rejected.pid==0&&rejectedBinding.fullyDestroyed&&stopMs<=3200&&remaining.fullyDestroyed&&remaining.handles==0&&remaining.pendingIo==0&&before==handles()&&context.alive()},
        {"stage","resource-revalidation-after-deferred-bootstrap"},{"variant",variant},{"preflightReads",inspections},{"bindingPlans",plans},{"plansAtRejectedPreflight",rejectedPlans},
        {"resourcesAtRejectedPreflight",state(rejectedBinding)},{"recovered",healed},{"loaderPid",measured.pid},{"phase",measured.phase},{"reason",measured.reason},
        {"elapsedFromFailureMs",elapsed},{"stopElapsedMs",stopMs},{"originalStopDeadlineMs",3200},{"atExit",state(remaining)},{"beforeHandles",before},{"afterHandles",handles()},{"cefUnaffected",context.alive()}};
}
// Historical 2.5-second worker deadlines are not evidence of corrupt bytes.
// Runtime must retry a destroyed worker's preflight without binding or launching
// before it passes, while permanent resource errors and OFF remain terminal.
inline Json initialResourceRevalidation(const std::filesystem::path& output,const std::string& variant){
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
// Only the added, independent second-preflight boundary overlaps the original
// serial suite. It runs from this exact test image, in a kill-on-close Job; its
// work consumes the SAME outer45s, without extra registration or reader threads.
inline bool selectedCase(const std::string& selection,const std::string& name){
    if(selection.empty())return true;
    for(std::size_t pos=0;pos<selection.size();){const auto end=selection.find(',',pos);const auto part=selection.substr(pos,end==std::string::npos?end:end-pos);if(!part.empty()&&name.find(part)!=std::string::npos)return true;if(end==std::string::npos)break;pos=end+1;}
    return false;
}
struct DeferredResourceBatch {
    detail::Handle job,process;std::filesystem::path output;ULONGLONG started=GetTickCount64();Json rows=Json::object(),childIdentity;ULONGLONG childElapsed=0;bool observed=false;DWORD pid=0,exitCode=STILL_ACTIVE;
    DeferredResourceBatch(const std::filesystem::path& home,const std::string& selection):output(home){
        std::string selected;
        for(const auto variant:{"recover","ready","off","hash","cleanup","admission"}){const auto id=std::string("native.typed-history-resource-")+variant;if(selectedCase(selection,id)){if(!selected.empty())selected+=',';selected+=id;}}
        if(selected.empty())return;
        std::filesystem::create_directories(output);job.value=CreateJobObjectW(nullptr,nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        need(job.value&&SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"deferred-resource-branch-job");
        const auto image=detail::executable();std::wstring command=L"\""+image+L"\" --deferred-resource-branch \""+output.wstring()+L"\" \""+widen(selected)+L"\"";
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION child{};
        need(CreateProcessW(image.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&startup,&child)!=FALSE,"deferred-resource-branch-create");
        process.value=child.hProcess;detail::Handle thread{child.hThread};pid=child.dwProcessId;
        if(!AssignProcessToJobObject(job.value,process.value)){TerminateProcess(process.value,87);WaitForSingleObject(process.value,3200);need(false,"deferred-resource-branch-own");}
        childIdentity=identity(process.value); // Capture from the live suspended HANDLE, before any exit.
        if(ResumeThread(thread.value)==static_cast<DWORD>(-1)){TerminateJobObject(job.value,87);WaitForSingleObject(process.value,3200);need(false,"deferred-resource-branch-resume");}
    }
    ~DeferredResourceBatch(){if(process.value&&WaitForSingleObject(process.value,0)==WAIT_TIMEOUT){TerminateJobObject(job.value,87);WaitForSingleObject(process.value,3200);}}
    Json take(const std::string& variant){
        need(process.value!=nullptr,"deferred-resource-branch-not-selected");
        if(!observed){const auto elapsed=GetTickCount64()-started;need(elapsed<45000&&WaitForSingleObject(process.value,static_cast<DWORD>(45000-elapsed))==WAIT_OBJECT_0,"deferred-resource-branch-original-deadline");
            need(GetExitCodeProcess(process.value,&exitCode)!=FALSE,"deferred-resource-branch-exit-query");
            std::ifstream input(output/"result.json",std::ios::binary);need(bool(input),"deferred-resource-branch-receipt");Json result;input>>result;
            need(result.at("pid").get<DWORD>()==pid&&result.at("sourceImage")==childIdentity.at("path"),"deferred-resource-branch-image-identity");
            rows=result.at("cases");childElapsed=result.at("elapsedMs").get<ULONGLONG>();observed=true;
        }
        need(rows.contains(variant),"deferred-resource-branch-missing-case");return rows.at(variant);
    }
    Json summary(){
        if(!process.value)return {{"selected",false},{"childExecutions",0}};
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};need(observed&&GetExitCodeProcess(process.value,&exitCode)&&QueryInformationJobObject(job.value,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr),"deferred-resource-branch-summary");
        Json result{{"selected",true},{"childExecutions",1},{"pid",pid},{"image",childIdentity.at("path")},{"exitCode",exitCode},{"jobActiveProcesses",accounting.ActiveProcesses},{"originalSharedOuterBudgetMs",45000},{"elapsedMs",childElapsed},{"parentObservationElapsedMs",GetTickCount64()-started},{"parentReaderThreads",0},{"cases",rows.size()},{"raw",(output/"result.json").string()}};
        need(accounting.ActiveProcesses==0,"deferred-resource-branch-descendants-remain");
        DWORD error=0;need(process.close(error)&&job.close(error),"deferred-resource-branch-handles-close");
        result["ownedParentHandlesAfterClose"]=0;
        return result;
    }
};
inline DeferredResourceBatch* deferredResourceBatch=nullptr;
inline Json resourceRevalidation(const std::filesystem::path& output,const std::string& variant){
    auto result=initialResourceRevalidation(output,variant);
    need(deferredResourceBatch!=nullptr,"deferred-resource-branch-owner");
    const auto deferred=deferredResourceBatch->take(variant);
    result["passed"]=result.at("passed").get<bool>()&&deferred.at("passed").get<bool>();
    result["afterDeferredBootstrap"]=deferred;
    return result;
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
// G3E Oct31: loaded-context refusal repeatedly becomes pending on resume.
// Parked admission must resume its existing timer, without a new epoch/refill.
// Same deep test-install shape as the ROG 2026-10-31 export. Only inert
// extraction files are created: no official Loader, Steam or registry writes.
inline Json loaderTempPathBudget(const std::filesystem::path& output){
    namespace fs=std::filesystem;
    fs::create_directories(output);
    auto home=fs::absolute(output);
    while(home.wstring().size()<148)home/=L"deep-candidate-install";
    home/=L"decky";fs::create_directories(home);
    const auto sentinel=home/L"settings-sentinel.json";
    {std::ofstream f(sentinel);f<<"preserve";}
    ymcc::deckysidebar::LoaderTemporaryRun run;
    const auto extraction=run.prepare(home.wstring());
    const auto count=extraction.size();
    // 68 units reserve _MEI child + current official relative path + growth.
    need(count<=190,"loader-temp-owned-run-fits-win32-path-budget");
    const auto payload=fs::path(extraction)/L"_MEI123456"/L"markupsafe-3.0.4.dist-info"/L"licenses"/L"LICENSE.txt";
    need(payload.wstring().size()<MAX_PATH,"official-longest-extraction-path-is-bounded");
    fs::create_directories(payload.parent_path());{std::ofstream f(payload);f<<"inert payload";}
    need(!run.cleanupAfterJobEmpty(false)&&fs::exists(payload),"temp-live-job-cannot-clean");
    bool secondDenied=false;try{run.prepare(home.wstring());}catch(...){secondDenied=true;}
    need(secondDenied&&fs::exists(payload),"temp-live-run-cannot-be-replaced");
    need(run.cleanupAfterJobEmpty(true)&&!fs::exists(payload),"temp-only-owned-run-cleaned");
    need(fs::exists(sentinel),"temp-cleanup-preserves-business-settings");
    // Separate owned runs never clean each other, including short-root runs.
    ymcc::deckysidebar::LoaderTemporaryRun left,right;
    const auto leftPath=left.prepare(home.wstring()),rightPath=right.prepare(home.wstring());
    need(leftPath!=rightPath&&left.usedShortRoot()&&right.usedShortRoot(),"deep-temp-runs-are-isolated");
    need(left.cleanupAfterJobEmpty(true)&&fs::exists(rightPath),"temp-sibling-run-was-deleted");
    const auto locked=fs::path(rightPath)/L"readonly-inert.txt";{std::ofstream f(locked);f<<"retain";}
    need(SetFileAttributesW(locked.c_str(),FILE_ATTRIBUTE_READONLY)!=FALSE,"temp-readonly-fixture");
    need(!right.cleanupAfterJobEmpty(true)&&fs::exists(locked),"temp-readonly-is-not-mutated-or-deleted");
    need(SetFileAttributesW(locked.c_str(),FILE_ATTRIBUTE_NORMAL)!=FALSE,"temp-readonly-fixture-restore");
    need(right.cleanupAfterJobEmpty(true),"temp-readonly-released-run-cleaned");
    // Unusable inherited TEMP must fail before launching anything; no registry
    // LongPathsEnabled change and no fallback to an arbitrary drive root.
    {
        struct Restore {std::wstring t=environment(L"TEMP"),m=environment(L"TMP");~Restore(){SetEnvironmentVariableW(L"TEMP",t.empty()?nullptr:t.c_str());SetEnvironmentVariableW(L"TMP",m.empty()?nullptr:m.c_str());}} restore;
        setEnvironment(L"TEMP",home.wstring());setEnvironment(L"TMP",home.wstring());
        ymcc::deckysidebar::LoaderTemporaryRun refused;bool denied=false;
        try{refused.prepare(home.wstring());}catch(const std::exception& e){denied=std::string(e.what())=="loader-temp-path-too-long";}
        need(denied&&refused.path().empty(),"oversized-fallback-temp-must-fail-closed");
    }
    // Execute the real Runtime creator and diagnostic producer at the same deep
    // home, with the existing source-built inert Loader and protocol peer.
    Controls controls(L"burst");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    Harness h(home,context);const auto beforeHandles=handles();h.start();
    need(waitFor([&]{return h.burstDone.load();}),"deep-temp-runtime-watch");
    need(h.tempFacts==1&&h.tempShort&&h.tempUnits<=190,"deep-temp-runtime-diagnostic-not-wired");
    const auto started=h.runtime.snapshot().pid;const auto began=GetTickCount64();h.runtime.shutdown();const auto stopMs=GetTickCount64()-began;
    const auto remaining=h.client->snapshot();
    need(stopMs<3200&&remaining.fullyDestroyed&&handles()==beforeHandles&&context.alive(),"deep-temp-runtime-resource-return");
    return {{"id","native.typed-loader-temp-path-budget"},{"passed",true},{"homeUnits",home.wstring().size()},
        {"ownedRunUnits",count},{"officialPayloadUnits",payload.wstring().size()},
        {"officialLoaderExecuted",false},{"inertLoaderPid",started},{"runtimeTempFacts",h.tempFacts.load()},
        {"stopMs",stopMs},{"atExit",state(remaining)},{"beforeHandles",beforeHandles},{"afterHandles",handles()},
        {"cefUnaffected",context.alive()},{"originalJobGatePreserved",true}};
}
inline bool runGlobalMissedEventProbe=false; // Original optional real missed-event wait, unchanged45s parent cap; actual20s observed separately.
inline Json admissionParkResume(const std::filesystem::path& output){
    using Fallback=ymcc::deckysidebar::ConnectionRecoveryWindow;using Action=Fallback::Action;
    Fallback fallback;constexpr ULONGLONG clock=100;
    const ymcc::deckysidebar::ConnectionHealth missing{true,false,"unknown-future-error"};
    need(fallback.observe(clock,missing)==Action::reconnect,"unknown-error-enters-global-rule");
    for(unsigned i=0;i<10000;++i)need(fallback.observe(clock+1,missing)==Action::none,"duplicate-event-spent-global-slot");
    need(fallback.timeout(clock+1)==4999&&fallback.attempts()==1,"global-observation-debt-changed");
    need(fallback.observe(clock+6999,missing)==Action::none,"transport-window-ended-before-real-rebind-tail");
    need(fallback.observe(clock+7000,missing)==Action::recycle,"known-persistent-disconnect-waits90s");
    fallback.failed(Action::recycle,clock+7001);
    need(fallback.observe(clock+7001,missing)==Action::rebuild,"absent-owned-service-waits20s");
    fallback.started(Action::rebuild,clock+7002);
    need(fallback.observe(clock+20779,missing)==Action::none,"13.777s-real-context-recovery-was-interrupted");
    const auto episode=fallback.episode();need(fallback.observe(clock+20780,{true,true,"actual-three-channel-ready"})==Action::recovered,"actual-ready-does-not-close-global-episode");
    need(fallback.observe(clock+20781,missing)==Action::reconnect&&fallback.episode()==episode+1,"second-disconnect-loses-global-rule");
    need(fallback.observe(clock+20782,{false,false,"off-or-foreign"})==Action::stopped,"off-or-foreign-identity-does-not-stop-global-rule");
    Fallback longGap;need(longGap.observe(clock,missing)==Action::reconnect,"gap-first");
    need(longGap.observe(clock+86400000,missing)==Action::recycle&&longGap.attempts()==2,"long-gap-produces-catch-up-storm");
    need(longGap.observe(clock+86400001,missing)==Action::none,"duplicate-after-long-gap-skips-service-stage");
    ymcc::deckysidebar::AdmissionWindow window;constexpr ULONGLONG base=100000;
    window.blocked(41,base,true);need(window.take(base+1000),"first-check-not-consumed");
    window.blocked(41,base+1100,false);need(window.timeout(base+1200)==INFINITE,"refused-mode-still-scheduled");
    for(unsigned i=0;i<10000;++i)window.blocked(41,base+1200,false);
    window.blocked(41,base+1500,true);need(window.timeout(base+1500)==1500&&window.attempts()==2,"resumed-window-refilled-or-erased");
    need(window.take(base+3000)&&window.attempts()==3,"original-second-deadline-lost");
    window.blocked(41,base+3100,false);window.blocked(41,base+9000,true);
    need(window.take(base+9000)&&!window.take(base+9000)&&window.timeout(base+9000)==11000,"parked-expired-deadlines-burst-or-refill");
    window.stop();window.blocked(41,base+9100,true);need(window.timeout(base+9100)==INFINITE,"explicit-stop-was-revived");
    window.completed();window.blocked(42,base+10000,false);window.blocked(42,base+15000,true);
    need(window.timeout(base+15000)==30000&&window.recovering(),"terminal-first-mode-created-new-fast-budget");
    for(unsigned i=0;i<10000;++i)window.blocked(42,base+15001,true);
    need(window.timeout(base+16000)==29000&&window.take(base+45000),"slow-budget-postponed-or-lost");
    window.stop();window.blocked(42,base+46000,false);window.blocked(42,base+47000,true);
    need(window.timeout(base+47000)==INFINITE,"explicit-stop-must-be-sticky-even-through-refusals");
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    std::atomic<unsigned> gate{0};
    Harness h(output,context,false,[&]{const auto n=gate.load();return n==3?ymcc::deckysidebar::LaunchAdmission{true}:
        ymcc::deckysidebar::LaunchAdmission{false,n==1?"decky-context-already-loaded":"steam-ui-state-pending",n!=1};});
    const auto before=handles();need(h.runtime.setEnabled(true),"park-enable");
    need(waitFor([&]{return h.runtime.snapshot().admissionRetry;}),"initial-admission-window-not-started");
    gate=1;need(h.runtime.requestLaunchAdmissionRefresh(),"loaded-refusal-wake");
    need(waitFor([&]{return h.runtime.snapshot().reason=="decky-context-already-loaded";}),"loaded-refusal-not-seen");
    need(!h.runtime.snapshot().admissionRetry&&h.plans==0,"loaded-context-cannot-schedule-or-launch");
    gate=2;need(h.runtime.requestLaunchAdmissionRefresh(),"pending-mode-wake");
    need(waitFor([&]{return h.runtime.snapshot().reason=="steam-ui-state-pending";}),"pending-mode-not-seen");
    const bool resumed=h.runtime.snapshot().admissionRetry;gate=3;
    const bool recovered=waitFor([&]{return h.healthyGeneration==1;},1800);
    const auto phase=h.runtime.snapshot();h.runtime.shutdown();const auto destroyed=h.client->snapshot();
    // Actual product Runtime consumes actual step callback results. No fixed
    // clock replacement and no Steam mode/process actions: external ports inert.
    Json stageScenarios=Json::array();
    for(const std::string scenario:{"failed-fast","positive-stops","uncertain-waits","cancelled-stops"}){
        std::atomic<unsigned> calls{0};std::atomic<bool> allHealthy{false},required{true};std::vector<int> steps;
        Harness ladder(output/("actual-stage-"+scenario),context,false,
            []{return ymcc::deckysidebar::LaunchAdmission{false,"decky-context-already-loaded",false};},{},{},{},{},
            [&]{ymcc::deckysidebar::ConnectionHealth value{required.load(),allHealthy.load(),"official-ws-dead",false,"fixed-session-ready"};value.transportConnected=true;return value;},
            [&](Action step)->Json{
                steps.push_back(static_cast<int>(step));++calls;
                if(scenario=="positive-stops"){allHealthy=true;return {{"ok",true},{"mutated",true}};}
                if(scenario=="cancelled-stops"){required=false;return {{"ok",false},{"mutated",false},{"reason","cancelled"}};}
                if(scenario=="uncertain-waits")return {{"ok",false},{"mutated",true},{"uncertain",true}};
                return {{"ok",false},{"mutated",false},{"reason","actual-no-effect"}};
            });
        const auto opened=GetTickCount64();need(ladder.runtime.setEnabled(true),"actual-stage-enable");
        need(waitFor([&]{return calls.load()>0;},2000),"actual-step-callback-not-consumed");
        if(scenario=="failed-fast")need(waitFor([&]{return calls.load()==2;},2000),"definite-failure-still-waits20s");
        else if(scenario=="uncertain-waits"&&runGlobalMissedEventProbe){
            const auto first=GetTickCount64();
            need(waitFor([&]{return calls.load()==2;},22000),"uncertain20s-observation-never-completes");
            need(GetTickCount64()-first>=19500,"uncertain-dispatch-escalated-before20s");
        }else if(scenario=="positive-stops"){
            ladder.runtime.requestConnectionCheck();need(waitFor([&]{return ladder.globalConnected.load()>0;},2000),"actual-positive-health-not-cancelled");
        }else {ladder.runtime.requestConnectionCheck();Sleep(80);}
        // Exercise overlapping watch/visible/manual-check/window entrances. They
        // request observation only and cannot issue extra page-recovery callbacks.
        for(unsigned i=0;i<1000;++i){ladder.runtime.requestConnectionCheck();ladder.runtime.requestTransportRecovery();ladder.runtime.requestLaunchAdmissionRefresh();}
        const auto elapsed=GetTickCount64()-opened;ladder.runtime.shutdown();
        need(calls==((scenario=="failed-fast"||scenario=="uncertain-waits"&&runGlobalMissedEventProbe)?2u:1u),"overlap-duplicates-page-recovery");
        need(!steps.empty()&&steps.front()==static_cast<int>(Action::rebuild),"no-owned-service-did-not-skip-to-ui-stage");
        if(steps.size()==2)need(steps[1]==static_cast<int>(Action::reopen),"step-order-broken");
        stageScenarios.push_back({{"scenario",scenario},{"callbackCount",calls.load()},{"elapsedMs",elapsed},{"actualRuntime",true},{"realSteamActions",0}});
    }
    bool globalRecovered=false,globalOffStopped=false;unsigned globalPlans=0;ULONGLONG globalElapsed=0;Json globalLog;
    if(runGlobalMissedEventProbe){
        std::atomic<bool> peer{false},firstFailureSeen{false};
        Harness lost(output/"unknown-missed-event",context,false,{},[&](Json&,unsigned attempt){if(attempt==1)throw std::runtime_error("unknown-future-error");},{},{},{},[&]{ymcc::deckysidebar::ConnectionHealth value{true,peer.load(),"unknown-future-error"};value.paused=!firstFailureSeen.load();return value;});
        const auto begin=GetTickCount64();need(lost.runtime.setEnabled(true),"global-enable");lost.runtime.observeSteam(1,true);
        need(waitFor([&]{return lost.runtime.snapshot().reason=="mirror-binding-request-failed";}),"unknown-binding-failure-not-observed");
        need(lost.plans==1&&!lost.runtime.snapshot().bindingRetry,"unknown-error-unexpectedly-used-known-fast-path");firstFailureSeen=true; // No recovery/event request: original missed-notification check.
        globalRecovered=waitFor([&]{return lost.healthyGeneration==2&&lost.runtime.snapshot().pid!=0;},33000);
        globalElapsed=GetTickCount64()-begin;need(globalElapsed<10000,"pending-proof-still-blocked-by-idle30s");globalPlans=lost.plans;std::cerr<<"MISSED_EVENT_OBSERVED elapsed="<<globalElapsed<<" plans="<<globalPlans<<" healthy="<<lost.healthyGeneration.load()<<" phase="<<lost.runtime.snapshot().phase<<" reason="<<lost.runtime.snapshot().reason<<"\n";peer=true;lost.runtime.requestConnectionCheck();
        need(waitFor([&]{return lost.globalConnected==1;}),"actual-peer-recovery-log-missing");
        lost.runtime.shutdown();globalLog=lost.globalRecoveryLog;const auto afterOff=lost.plans.load();
        globalOffStopped=!lost.runtime.requestTransportRecovery()&&!lost.runtime.requestConnectionCheck()&&lost.plans==afterOff&&lost.client->snapshot().fullyDestroyed;
    }
    return {{"id","native.typed-admission-park-resume-without-budget-refill"},{"passed",(!runGlobalMissedEventProbe||(globalRecovered&&globalOffStopped&&globalPlans==2))&&resumed&&recovered&&h.plans==1&&phase.pid!=0&&
        destroyed.fullyDestroyed&&before==handles()&&context.alive()},
        {"globalMissedEventProbeExecuted",runGlobalMissedEventProbe},{"globalRecoveredWithoutEvent",runGlobalMissedEventProbe?Json(globalRecovered):Json()},{"globalElapsedMs",runGlobalMissedEventProbe?Json(globalElapsed):Json()},{"globalBindingPlans",globalPlans},{"globalOffStopped",runGlobalMissedEventProbe?Json(globalOffStopped):Json()},{"globalVirtualClockChecks",true},{"actualStageCallbackScenarios",stageScenarios},{"globalRecoveryLog",globalLog},{"timerResumed",resumed},{"admissionRecoveryPhase",phase.phase},{"admissionRecoveryReason",phase.reason},{"admissionRecoveryLaunchStage",phase.launchStage},{"admissionRecoveryHealthyGeneration",h.healthyGeneration.load()},{"recoveredWithoutExtraEvent",recovered},{"originalBudgetAndStopAssertions",true},{"refusalStormSignals",20000},{"bindingPlans",h.plans.load()},
        {"cefUnaffected",context.alive()},{"afterOff",state(destroyed)},{"beforeHandles",before},{"afterHandles",handles()}};
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
using ReadyBackendObserver=std::function<Json(ymcc::deckysidebar::Runtime&,DWORD,HANDLE)>;
using ReadyBackendFactory=std::function<ReadyBackendObserver()>;
inline Json ready(const std::filesystem::path& output,bool retained,const ReadyBackendFactory& backendFactory={}){
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    std::atomic<unsigned long long> proofDue{0};std::atomic<bool> proofReturned{false};std::atomic<unsigned> proofCalls{0};
    const auto recovery=[&]{const auto due=proofDue.load();if(due&&GetTickCount64()>=due){++proofCalls;proofDue=0;proofReturned=true;}};
    const auto recoveryTimeout=[&]()->DWORD{const auto due=proofDue.load();const auto now=GetTickCount64();return !due?INFINITE:due<=now?0:static_cast<DWORD>(due-now);};
    const auto backendObserver=backendFactory?backendFactory():ReadyBackendObserver{};
    Harness harness(output,context,retained,{},{},{},recovery,recoveryTimeout);const auto before=handles();
    const auto initialBegin=GetTickCount64();
    need(harness.runtime.setEnabled(true),"ready-enable");
    harness.waitForCheckpoint([&]{return harness.healthyGeneration==1;},"initial-binding-checkpoint");
    const auto initialElapsedMs=GetTickCount64()-initialBegin;
    const auto initial=harness.runtime.snapshot();const auto originalLoader=initial.pid;
    const auto originalBinding=harness.client->snapshot().processId;
    need(retained?(initial.phase=="frontend-attached"&&originalLoader==0):(initial.phase=="loader-started"&&originalLoader!=0),"initial-loader-ownership");
    detail::Handle ownedLoader{originalLoader?OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,originalLoader):nullptr};
    detail::Handle foreignContext{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,worker::number(context.lease.at("pid")))};
    const bool loaderOwnership=retained?!harness.runtime.ownsLiveProcess(GetCurrentProcess()):ownedLoader.value&&harness.runtime.ownsLiveProcess(ownedLoader.value);
    const bool foreignRejected=foreignContext.value&&!harness.runtime.ownsLiveProcess(foreignContext.value)&&!harness.runtime.ownsLiveProcess(GetCurrentProcess())&&!harness.runtime.ownsLiveProcess(nullptr);
    need(loaderOwnership&&foreignRejected,"native managed receipt must attest exact owned Loader Job and reject CEF/parent/empty handles");
    const Json ownedBackendProof=backendObserver?backendObserver(harness.runtime,originalLoader,ownedLoader.value):Json{{"passed",true},{"exercised",false}};
    const auto preparedBefore=harness.preparations.load();
    bool preparationAccepted=true;
    for(unsigned long long generation=1;generation<=1024;++generation)preparationAccepted=harness.runtime.requestPreparationRefresh(generation)&&preparationAccepted;
    Sleep(60); // Give the existing worker the preparation-only wake before any explicit rebind.
    const auto maintenanceState=harness.runtime.snapshot();
    const bool connectionUnaffected=harness.plans==1&&harness.healthyGeneration==1&&harness.client->snapshot().processId==originalBinding&&maintenanceState.phase==initial.phase&&maintenanceState.pid==originalLoader&&context.alive();
    proofDue=GetTickCount64()+250;
    need(harness.runtime.requestConnectionCheck(),"owned proof wake request refused");
    const bool proofScheduled=waitFor([&]{return proofReturned.load();},1000);
    need(proofScheduled&&proofCalls==1,"live owned Loader suppressed its bounded backend proof timeout");
    const bool accepted=harness.runtime.requestBindingRefresh(9);
    const bool duplicate=harness.runtime.requestBindingRefresh(9),zero=harness.runtime.requestBindingRefresh(0);
    need(waitFor([&]{return harness.healthyGeneration==2;}),"ready-binding-checkpoint");
    const auto after=harness.runtime.snapshot();const auto nextBinding=harness.client->snapshot().processId;
    const bool sameOwnership=after.pid==originalLoader&&after.phase==initial.phase&&context.alive();
    harness.runtime.shutdown();const auto destroyed=harness.client->snapshot();
    const bool ownershipRevoked=!ownedLoader.value||!harness.runtime.ownsLiveProcess(ownedLoader.value);
    if(ownedLoader.value){CloseHandle(ownedLoader.value);ownedLoader.value=nullptr;}
    if(foreignContext.value){CloseHandle(foreignContext.value);foreignContext.value=nullptr;}
    return {{"id",retained?"native.typed-retained-ready-rebind-never-starts-loader":"native.typed-ready-rebind-keeps-original-owned-loader"},
        {"ownedBackendProofScheduled",proofScheduled},{"ownedBackendProofCalls",proofCalls.load()},{"nativeOwnedLoaderAttested",loaderOwnership},{"foreignContextAndParentRejected",foreignRejected},{"ownershipRevokedAfterOff",ownershipRevoked},
        {"actualOwnedBackendProof",ownedBackendProof},{"passed",ownedBackendProof.at("passed").get<bool>()&&proofScheduled&&proofCalls==1&&loaderOwnership&&foreignRejected&&ownershipRevoked&&accepted&&!duplicate&&!zero&&preparationAccepted&&connectionUnaffected&&preparedBefore==(retained?0u:1u)&&harness.preparations==preparedBefore&&harness.plans==2&&harness.healthyGeneration==2&&sameOwnership&&nextBinding&&nextBinding!=originalBinding&&destroyed.fullyDestroyed&&before==handles()},
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
static struct {unsigned reads=0;std::function<void()> refreshed;void requestRefresh(const Session&){++reads;if(refreshed)refreshed();}} g_steamModeObserver;
static Json diagnostics=Json::array();
static void deckySidebarDiagnostic(const char* event,const Json& detail,bool){diagnostics.push_back({{"event",event},{"detail",detail}});}
#include "decky_wake_ready_consumer.inc"
#include "decky_wake_committed_consumer.inc"
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
    wakeReady::runtime=&h.runtime;wakeReady::observedGeneration=retained?278:177;const auto power=wakeReady::observedGeneration;
    const auto loader=h.runtime.snapshot().pid;const auto plans=h.plans.load(),preparations=h.preparations.load(),reads=wakeReady::g_steamModeObserver.reads;
    for(auto phase:{wakeReady::PowerLifecycle::Suspended,wakeReady::PowerLifecycle::Resuming}){wakeReady::g_powerLifecycle=phase;need(!wakeReady::deckySidebarResumeAfterIntent(power),"nonready S0 recovery admitted");}
    wakeReady::g_powerLifecycle=wakeReady::PowerLifecycle::Ready;
    need(!wakeReady::deckySidebarResumeAfterIntent(0)&&!wakeReady::deckySidebarResumeAfterIntent(power-1),"stale power generation admitted");
    wakeReady::g_exitRequested=true;need(!wakeReady::deckySidebarResumeAfterIntent(power),"exit admitted wake recovery");wakeReady::g_exitRequested=false;
    wakeReady::g_aiFanMockSession.isolated=true;need(!wakeReady::deckySidebarResumeAfterIntent(power),"isolated wake reached IO");wakeReady::g_aiFanMockSession.isolated=false;
    need(h.plans==plans&&wakeReady::g_steamModeObserver.reads==reads,"invalid wake touched connection");
    need(wakeReady::deckySidebarResumeAfterIntent(power),"committed S0 Ready ignored");
    h.waitForCheckpoint([&]{return isHealthy(h,retained,2);},"S0 Ready must replace stale binding and reach healthy new snapshot");
    for(unsigned i=0;i<512;++i){need(!wakeReady::deckySidebarResumeAfterIntent(power),"duplicate S0 notification refilled recovery");wakeReady::productionCommittedReady();}
    need(h.plans==plans+1&&wakeReady::g_steamModeObserver.reads==reads+1&&h.runtime.snapshot().pid==loader,"wake storm restarts Loader or creates duplicate binding/mode reads");
    const auto after=runtimeState(h);h.runtime.setEnabled(false);++wakeReady::observedGeneration;need(!wakeReady::deckySidebarResumeAfterIntent(wakeReady::observedGeneration),"OFF revived on late wake");
    wakeReady::runtime=nullptr;
    Json committed;
    {
        // Field sequence: no Loader, stale shared mode, terminal startup wait,
        // committed wake with no subsequent window/mode event. The existing
        // shared reader completion publishes fresh admission; never invent consent.
        std::atomic<bool> fresh{false};
        Harness parked(output/"committed-no-loader",context,retained,[&]{return fresh.load()?ymcc::deckysidebar::LaunchAdmission{true,{},false,{},retained}:ymcc::deckysidebar::LaunchAdmission{false,"ymcc-startup-observation-only",false};});
        need(parked.runtime.setEnabled(true),"parked committed enable");
        need(waitFor([&]{return parked.runtime.snapshot().reason=="ymcc-startup-observation-only";}),"terminal pre-wake admission not observed");
        need(parked.plans==0&&!parked.runtime.snapshot().admissionRetry,"terminal wait must not create a child or timer");
        wakeReady::runtime=&parked.runtime;++wakeReady::observedGeneration;
        const auto priorReads=wakeReady::g_steamModeObserver.reads;
        wakeReady::g_steamModeObserver.refreshed=[&]{fresh=true;parked.runtime.requestLaunchAdmissionRefresh();};
        wakeReady::productionCommittedReady();
        need(wakeReady::g_steamModeObserver.reads==priorReads+1,"committed Ready must refresh stale shared mode even without a Loader");
        parked.waitForCheckpoint([&]{return isHealthy(parked,retained,1);},"committed Ready missing-loader path must reach a fresh typed binding without another UI event");
        const auto healthyPlans=parked.plans.load();
        for(unsigned i=0;i<512;++i){wakeReady::productionCommittedReady();need(!wakeReady::deckySidebarResumeAfterIntent(wakeReady::observedGeneration),"S0 after committed Ready refilled recovery");}
        need(wakeReady::g_steamModeObserver.reads==priorReads+1&&parked.plans==healthyPlans,"committed Ready storm must coalesce with S0 recovery by generation");
        const auto readyState=runtimeState(parked);parked.runtime.setEnabled(false);
        ++wakeReady::observedGeneration;wakeReady::productionCommittedReady();
        need(wakeReady::g_steamModeObserver.reads==priorReads+1&&parked.client->snapshot().fullyDestroyed,"late committed Ready revived OFF or touched shared mode");
        wakeReady::g_steamModeObserver.refreshed={};wakeReady::runtime=nullptr;
        committed={{"actualCommittedConsumer",true},{"noLoaderOnEntry",true},{"terminalAdmissionOnEntry",true},{"modeReads",wakeReady::g_steamModeObserver.reads-priorReads},{"duplicateSignals",512},{"after",readyState},{"atExit",state(parked.client->snapshot())}};
    }
    return {{"id",retained?"native.typed-s0-intent-ready-retained":"native.typed-s0-intent-ready-owned"},{"passed",true},{"productionConsumerExtracted",true},{"committedReady",committed},
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
// Sidecar consumers reuse only the closed common namespace above this marker.
// Scenario-local generated dependencies belong to the complete producer below.
// YMCC_DECKY_RUNTIME_SHARED_PREAMBLE_END

#include "decky_managed_backend_consumer.inc"
namespace rtfixture {

// Same original ready case: the actual Runtime starts its usual inert image.
// Only that already-owned image binds an exclusive high loopback port. No real
// Steam/Loader/application or installed backend port participates in this test.
namespace ownedbackend {
struct Sockets {
    SOCKET four=INVALID_SOCKET,six=INVALID_SOCKET;
    ~Sockets(){close();}
    void close(){if(four!=INVALID_SOCKET)closesocket(four);if(six!=INVALID_SOCKET)closesocket(six);four=six=INVALID_SOCKET;}
    bool open(unsigned short port,bool serving){
        close();four=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);six=socket(AF_INET6,SOCK_STREAM,IPPROTO_TCP);
        if(four==INVALID_SOCKET||six==INVALID_SOCKET)return false;
        BOOL one=TRUE;
        if(setsockopt(four,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&one),sizeof(one))||
           setsockopt(six,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&one),sizeof(one))||
           setsockopt(six,IPPROTO_IPV6,IPV6_V6ONLY,reinterpret_cast<const char*>(&one),sizeof(one)))return false;
        sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        sockaddr_in6 z{};z.sin6_family=AF_INET6;z.sin6_port=htons(port);z.sin6_addr=in6addr_loopback;
        if(bind(four,reinterpret_cast<sockaddr*>(&a),sizeof(a))||bind(six,reinterpret_cast<sockaddr*>(&z),sizeof(z)))return false;
        return !serving||(listen(four,SOMAXCONN)==0&&listen(six,SOMAXCONN)==0);
    }
};
struct Winsock {
    bool started=false;
    void start(){WSADATA data{};need(WSAStartup(MAKEWORD(2,2),&data)==0,"owned backend Winsock initialization");started=true;}
    ~Winsock(){if(started)WSACleanup();}
};
inline Json observe(ymcc::deckysidebar::Runtime& runtime,unsigned short port,const char* label){
    actualManagedBackend::DeckyManagedBackendLease lease;bool absent=true;
    const bool accepted=actualManagedBackend::deckySidebarAcquireManagedBackend(lease,runtime,port,&absent);
    return {{"label",label},{"accepted",accepted},{"absent",absent},{"pid",lease.pid},
        {"openedPid",lease.value?GetProcessId(lease.value):0},{"birth",std::to_string(lease.birth)}};
}
class Probe {
    bool enabled_;Winsock winsock_;std::wstring oldPort_,oldPrefix_,prefix_;unsigned short port_=0;
    detail::Handle ready_,close_,closed_,release_;Json cold_;
public:
    explicit Probe(bool enabled):enabled_(enabled){
        if(!enabled_)return;
        winsock_.start();Sockets reserve;
        for(unsigned attempt=0;attempt<8;++attempt){
            unsigned random=0;need(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&random),sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"owned backend random port");
            port_=static_cast<unsigned short>(49152+random%16384);if(reserve.open(port_,false))break;
            port_=0;
        }
        need(port_>=49152&&port_!=1337,"owned backend high port reservation");
        ymcc::deckysidebar::Runtime unstarted;
        cold_=observe(unstarted,port_,"cold-complete-tables-no-listener");
        need(!cold_.at("accepted").get<bool>()&&cold_.at("absent").get<bool>(),"owned backend cold absence control");
        reserve.close();
        prefix_=L"Local\\YMCC-BackendRuntime-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
        ready_.value=CreateEventW(nullptr,TRUE,FALSE,(prefix_+L"-ready").c_str());
        close_.value=CreateEventW(nullptr,TRUE,FALSE,(prefix_+L"-close").c_str());
        closed_.value=CreateEventW(nullptr,TRUE,FALSE,(prefix_+L"-closed").c_str());
        release_.value=CreateEventW(nullptr,TRUE,FALSE,(prefix_+L"-release").c_str());
        need(ready_.value&&close_.value&&closed_.value&&release_.value,"owned backend control events");
        oldPort_=environment(L"YMCC_RUNTIME_BACKEND_PROBE_PORT");oldPrefix_=environment(L"YMCC_RUNTIME_BACKEND_PROBE_PREFIX");
        setEnvironment(L"YMCC_RUNTIME_BACKEND_PROBE_PORT",std::to_wstring(port_));
        setEnvironment(L"YMCC_RUNTIME_BACKEND_PROBE_PREFIX",prefix_);
    }
    ~Probe(){if(enabled_){SetEvent(release_.value);
        SetEnvironmentVariableW(L"YMCC_RUNTIME_BACKEND_PROBE_PORT",oldPort_.empty()?nullptr:oldPort_.c_str());
        SetEnvironmentVariableW(L"YMCC_RUNTIME_BACKEND_PROBE_PREFIX",oldPrefix_.empty()?nullptr:oldPrefix_.c_str());}}
    Json verify(ymcc::deckysidebar::Runtime& runtime,DWORD pid,HANDLE process){
        if(!enabled_)return {{"passed",true},{"exercised",false}};
        need(WaitForSingleObject(ready_.value,5000)==WAIT_OBJECT_0,"actual owned backend never listened");
        const auto actualBirth=ymcc::deckyrepair::cleanupworker::processBirth(process);
        const auto positive=observe(runtime,port_,"actual-runtime-owned-listener");
        need(pid&&pid!=GetCurrentProcessId()&&actualBirth&&positive.at("accepted").get<bool>()&&
            !positive.at("absent").get<bool>()&&positive.at("pid")==pid&&positive.at("openedPid")==pid&&
            positive.at("birth")==std::to_string(actualBirth),"actual OS listener and Runtime Job ownership failed positive backend proof");
        need(SetEvent(close_.value)&&WaitForSingleObject(closed_.value,5000)==WAIT_OBJECT_0,"owned backend listener close handshake");
        need(runtime.ownsLiveProcess(process),"closing listener revoked the still-live owned child");
        const auto missing=observe(runtime,port_,"live-owned-child-no-listener");
        need(!missing.at("accepted").get<bool>()&&missing.at("absent").get<bool>(),"owned child without listener did not certify absence");
        Json foreign;
        {Sockets parent;need(parent.open(port_,true),"parent exclusive high loopback listener");
         foreign=observe(runtime,port_,"actual-foreign-parent-listener");
         need(!foreign.at("accepted").get<bool>()&&!foreign.at("absent").get<bool>()&&
             foreign.at("openedPid")==GetCurrentProcessId(),"actual foreign parent listener passed owned backend proof");}
        return {{"passed",true},{"exercised",true},{"adaptedPort",port_},{"coldApiObservation",cold_},
            {"rows",Json::array({positive,missing,foreign})},{"ownershipActual",true},{"osListenerTablesActual",true},
            {"ownedBackendPid",pid},{"ownedBackendBirth",std::to_string(actualBirth)},
            {"scope","Existing Runtime launch/Job and actual OS IPv4/IPv6 listener proof. Only helper signature takes current Runtime and high test port. Not full native lifecycle/private-child continuation or physical acceptance."}};
    }
};
inline bool tryInertLoader(int& exit){
    const auto text=environment(L"YMCC_RUNTIME_BACKEND_PROBE_PORT");if(text.empty())return false;
    try{
        const auto value=_wcstoui64(text.c_str(),nullptr,10);const auto prefix=environment(L"YMCC_RUNTIME_BACKEND_PROBE_PREFIX");
        need(value>=49152&&value<=65535&&prefix.starts_with(L"Local\\YMCC-BackendRuntime-"),"inert backend fixture provenance");
        detail::Handle ready{OpenEventW(EVENT_MODIFY_STATE,FALSE,(prefix+L"-ready").c_str())};
        detail::Handle close{OpenEventW(SYNCHRONIZE,FALSE,(prefix+L"-close").c_str())};
        detail::Handle closed{OpenEventW(EVENT_MODIFY_STATE,FALSE,(prefix+L"-closed").c_str())};
        detail::Handle release{OpenEventW(SYNCHRONIZE,FALSE,(prefix+L"-release").c_str())};
        need(ready.value&&close.value&&closed.value&&release.value,"inert backend controls");
        Winsock winsock;winsock.start();Sockets sockets;
        need(sockets.open(static_cast<unsigned short>(value),true)&&SetEvent(ready.value),"inert backend listen");
        need(WaitForSingleObject(close.value,30000)==WAIT_OBJECT_0,"inert backend close request");
        sockets.close();need(SetEvent(closed.value),"inert backend close acknowledgement");
        exit=WaitForSingleObject(release.value,30000)==WAIT_OBJECT_0?0:93;
    }catch(...){exit=94;}
    return true;
}
}

}

namespace rtfixture {
namespace offlineConsumer {
using json=nlohmann::json;
using Session=ymcc::steamsession::Session;
inline ymcc::deckysidebar::Runtime* runtime=nullptr;
inline Session observedSession;
inline Json observedMode;
inline unsigned brokerStops=0,refreshes=0;
inline const std::wstring POWER_CONTROL_DIR=L"inert-no-payload";
inline std::string W2U(const std::wstring& value){const auto utf8=std::filesystem::path(value).u8string();return {utf8.begin(),utf8.end()};}
struct Observer { Session sessionSnapshot(){return observedSession;} };
struct Modes {Json snapshot(const Session&){return observedMode;}bool requestRefresh(const Session&){++refreshes;return true;}};
struct Consent {bool allows(const Session&){return true;}bool allowsRetainedFrontend(const Session&,const Json&,const std::string&,const char*){return false;}};
struct Broker {void stop(){++brokerStops;}Json bootstrap(){return makeRequest(1).at("bootstrap");}};
struct Cost {void binding(const Json&,DWORD,ULONGLONG,ULONGLONG){}};
inline Observer g_steamSettingsObserver;
inline Modes g_steamModeObserver;
inline Consent g_steamLinkAdmission,g_deckyUpdateRecovery;
inline Broker g_deckyMirrorBroker;
inline Cost g_deckyCostContext;
inline std::string deckySidebarStartMirrorBroker(){return {};}
inline Json deckySidebarEnvironment(bool){return {{"ready",true}};}
inline std::atomic<bool> g_exitRequested{false};
inline struct {bool isolated=false;} g_aiFanMockSession;
enum class PowerLifecycle{Ready,Suspended};
inline std::atomic<PowerLifecycle>g_powerLifecycle{PowerLifecycle::Ready};
inline void deckySidebarSessionDiagnostic(const Session&,const char*,DWORD=0){}
inline void deckySidebarDiagnostic(const char*,const Json&,bool=false){}
inline unsigned long long currentPowerGeneration(){return 1;}
inline void deckySidebarPost(const char*,const Json&){}
#define g_deckySidebarRuntime (*runtime)
#include "decky_offline_consumers.inc"
#undef g_deckySidebarRuntime
}
inline Json offlineIdentity(const std::filesystem::path& output){
    Controls controls(L"burst");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    Harness h(output,context);const auto before=handles();h.start();
    need(waitFor([&]{return h.burstDone.load();}),"offline-inert-watch-live");
    namespace actual=offlineConsumer;actual::runtime=&h.runtime;
    const auto base=makeRequest(51);actual::observedSession={worker::wide(base["steam"]["root"]),GetCurrentProcessId(),0,worker::decimal(base["steam"]["created"])};
    actual::observedMode={{"known",true},{"running",true},{"mode","bigpicture"},{"pid",actual::observedSession.pid},
        {"birth",std::to_string(actual::observedSession.created)},{"observedAccount",12345678u},{"deckyAccountWatchAvailable",true},{"deckyFrontendLoaded",false}};
    const auto admission=actual::actualAdmission();need(admission().allowed,"actual-offline-main-admission");
    auto plan=actual::deckySidebarOwnedBindingPlan(ymcc::deckysidebar::BindingMode::prepare,51);
    const auto request=worker::parse(plan.request.dump());
    need(plan.reason.empty()&&request.liveUiAccount&&request.steam.account==12345678,"actual-offline-main-plan-provenance");
    actual::observedMode["deckyAccountWatchAvailable"]=false;const auto pendingRefreshes=actual::refreshes;
    need(!admission().allowed,"actual-offline-missing-live-proof-refused");
    need(actual::refreshes==pendingRefreshes+1,"actual-offline-async-proof-requests-original-shared-refresh");
    need(!actual::deckySidebarOwnedBindingPlan(ymcc::deckysidebar::BindingMode::prepare,52).reason.empty(),"actual-offline-missing-proof-no-plan");
    actual::observedMode["deckyAccountWatchAvailable"]=true;actual::observedSession.account=12345678;
    need(admission().allowed,"same-account-online-admission");
    plan=actual::deckySidebarOwnedBindingPlan(ymcc::deckysidebar::BindingMode::refresh,53);
    need(!worker::parse(plan.request.dump()).liveUiAccount,"online-retains-original-registry-contract");
    actual::observedSession.account=0;
    const auto beforeNewDocument=actual::refreshes;
    actual::deckySidebarOwnedBindingDiagnostic({{"bindingPacketKind","result"},{"reason","mirror-bootstrap-session-changed"},{"mutated",false}});
    need(actual::refreshes==beforeNewDocument+1,"offline-new-document-reloads-current-proof-without-replaying-writes");
    actual::deckySidebarOwnedBindingDiagnostic({{"bindingPacketKind","result"},{"reason","mirror-bootstrap-session-changed"},{"mutated",nullptr}});
    need(actual::refreshes==beforeNewDocument+1,"uncertain-reply-does-not-open-offline-proof-retry");
    const auto stoppedBefore=actual::brokerStops,refreshBefore=actual::refreshes;
    actual::deckySidebarOwnedBindingDiagnostic({{"bootstrapStage","failed"},{"reason","mirror-bootstrap-account-changed"}});
    need(actual::brokerStops==stoppedBefore+1&&actual::refreshes==refreshBefore+1,"actual-owner-revokes-old-broker-and-refreshes");
    need(ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-account-changed"),"account-recovery-admitted");
    // Keep the original offline-consumer ID while alternating current accounts
    // and online/offline registry signals. PID/birth are memory observations;
    // a stale UI proof never grants an offline plan for a replaced process.
    unsigned accountTransitions=0,staleProofRefusals=0;
    for(unsigned cycle=0;cycle<12;++cycle){
        const DWORD currentAccount=cycle%2?87654321u:12345678u;
        actual::observedSession.account=0;
        actual::observedMode["observedAccount"]=currentAccount;
        actual::observedMode["deckyAccountWatchAvailable"]=true;
        const auto currentBirth=std::to_string(actual::observedSession.created);
        actual::observedMode["birth"]=std::to_string(actual::observedSession.created+1);
        need(!admission().allowed,"account-switch-stale-birth-admission-refused");
        need(!actual::deckySidebarOwnedBindingPlan(ymcc::deckysidebar::BindingMode::refresh,60+cycle*3).reason.empty(),"account-switch-stale-birth-plan-refused");
        ++staleProofRefusals;actual::observedMode["birth"]=currentBirth;
        actual::observedMode["pid"]=actual::observedSession.pid+1;
        need(!admission().allowed,"account-switch-stale-pid-admission-refused");
        need(!actual::deckySidebarOwnedBindingPlan(ymcc::deckysidebar::BindingMode::refresh,61+cycle*3).reason.empty(),"account-switch-stale-pid-plan-refused");
        ++staleProofRefusals;actual::observedMode["pid"]=actual::observedSession.pid;
        need(admission().allowed,"account-switch-current-offline-proof-admitted");
        auto switched=actual::deckySidebarOwnedBindingPlan(ymcc::deckysidebar::BindingMode::refresh,62+cycle*3);
        auto switchedRequest=worker::parse(switched.request.dump());
        need(switched.reason.empty()&&switchedRequest.liveUiAccount&&switchedRequest.steam.account==currentAccount,"account-switch-current-offline-plan");
        actual::observedSession.account=currentAccount;
        need(admission().allowed,"account-switch-online-readmitted");
        switched=actual::deckySidebarOwnedBindingPlan(ymcc::deckysidebar::BindingMode::refresh,100+cycle);
        switchedRequest=worker::parse(switched.request.dump());
        need(switched.reason.empty()&&!switchedRequest.liveUiAccount&&switchedRequest.steam.account==currentAccount,"account-switch-online-plan-retains-registry");
        ++accountTransitions;
    }
    h.runtime.shutdown();const auto remaining=h.client->snapshot();actual::runtime=nullptr;
    need(remaining.fullyDestroyed&&before==handles()&&context.alive(),"offline-proof-resource-return");
    return {{"id","native.typed-offline-current-ui-account-consumers"},{"passed",true},{"beforeHandles",before},{"afterHandles",handles()},
        {"atExit",state(remaining)},{"actualMainConsumers",true},{"offlineAdmission",true},{"typedNonzeroProof",true},
        {"missingProofRefused",true},{"sameAccountOnline",true},{"ownerRevocation",true},{"cefUnaffected",context.alive()},
        {"accountTransitions",accountTransitions},{"staleProofRefusals",staleProofRefusals},{"actualAccountLoginPerformed",false}};
}
}

int main(int argc,char** argv){
    int code=0;if(rtfixture::tryStalledChild(code))return code;if(ymcc::deckybinding::fixture::tryRunChild(code))return code;
    if(argc==4&&std::string_view(argv[1])=="--deferred-resource-branch"){
        using namespace rtfixture;const auto output=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(output);const std::string selection=argv[3];
        const auto began=GetTickCount64();
        const auto calibration=initializeSharedCryptoForMeasurement();Json rows=Json::object();bool passed=true;
        for(const auto variant:{"recover","ready","off","hash","cleanup","admission"})if(selectedCase(selection,std::string("native.typed-history-resource-")+variant)){
            try{rows[variant]=deferredResourceRevalidation(output/variant,variant);passed=passed&&rows[variant].at("passed").get<bool>();}
            catch(const std::exception& error){rows[variant]={{"passed",false},{"error",error.what()}};passed=false;}
        }
        std::ofstream(output/"result.json",std::ios::binary)<<Json{{"pid",GetCurrentProcessId()},{"sourceImage",identity().at("path")},{"cases",rows},{"elapsedMs",GetTickCount64()-began},{"sharedCryptoInitialization",calibration}}.dump(2)<<'\n';
        return passed&&!rows.empty()?0:1;
    }
    if(argc==1){if(rtfixture::ownedbackend::tryInertLoader(code))return code;Sleep(30000);return 0;} // Only Runtime's inert, owned Loader stand-in.
    if(argc!=2&&argc!=3&&!(argc==4&&std::string_view(argv[3])=="--global-missed-event"))return 64;
    using namespace rtfixture;const auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);
    const std::string selection=argc>=3?argv[2]:"";runGlobalMissedEventProbe=argc==4;
    DeferredResourceBatch deferred(output/"deferred-resource-branch",selection);deferredResourceBatch=&deferred;
    const auto sharedCryptoInitialization=initializeSharedCryptoForMeasurement();
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
    test("native.typed-offline-current-ui-account-consumers",[&]{return offlineIdentity(output/"offline-identity");});
    test("native.typed-loader-temp-path-budget",[&]{return loaderTempPathBudget(output/"temp-path");});
    test("native.typed-admission-park-resume",[&]{return admissionParkResume(output/"admission-park");});
    test("native.typed-runtime-live-watch",[&]{return healthy(output/"healthy");});
    test("native.typed-stalled-binding-stop",[&]{return stalled(output/"stalled-cv",L"runtime-stall-cv");});
    test("native.typed-stalled-binding-descendants",[&]{return stalled(output/"stalled-descendants",L"stubborn-descendant");});
    test("native.typed-rejected-final-admission",[&]{return rejectedFinalAdmission(output/"final-admission");});
    test("native.typed-preparation-revalidation",[&]{return rejectedAfterPreparation(output/"preparation-admission");});
    test("native.typed-s0-intent-ready-owned",[&]{return intentReady(output/"intent-ready-owned",false);});
    test("native.typed-s0-intent-ready-retained",[&]{return intentReady(output/"intent-ready-retained",true);});
    test("native.typed-ready-rebind",[&]{
        const auto result=ready(output/"ready",false,[]()->ReadyBackendObserver{
            auto probe=std::make_shared<ownedbackend::Probe>(true);
            return [probe](ymcc::deckysidebar::Runtime& runtime,DWORD pid,HANDLE process){return probe->verify(runtime,pid,process);};
        });
        need(result.at("actualOwnedBackendProof").value("exercised",false),"actual owned backend extension was not exercised");
        return result;
    });
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
        {"deferredResourceExecution",deferred.summary()},
        {"realSteamOrDeviceActions",0},{"originalHistoricalFailurePreserved",true},{"fieldWakeOrS4Verified",false},{"hostPackagePassed",false}};
    std::ofstream(output.parent_path()/"typed-runtime-result.json",std::ios::binary)<<result.dump(2)<<'\n';
    std::cout<<result.dump(2)<<'\n';return result.at("passed").get<bool>()?0:1;
}
