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
    if(behavior!=L"runtime-stall-cv"&&behavior!=L"runtime-success-checkpoint")return false;
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
        if(behavior==L"runtime-success-checkpoint"){
            detail::Handle context{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,worker::number(request.bootstrap.at("fixtureContextPid")))};
            need(context.value!=nullptr,"checkpoint-context");
            const auto emit=[&](const char* kind,const Json& detail){const Json packet{{"schema",worker::schema},{"id",request.id},{"generation",std::to_string(request.generation)},{"kind",kind},{"detail",detail}};need(bool(channel.writeFrame(bytes(packet.dump()),5000)),"checkpoint-emit");};
            emit("result",{{"ok",true},{"mutated",true},{"contextLease",identity(context.value)}});
            emit("diagnostic",{{"fixtureHealthyGeneration",std::to_string(request.generation)}});
            SetEvent(entered.value);channel.waitCancelled(15000);exit=0;return true;
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
    std::atomic<unsigned> plans{0},diagnostics{0};std::atomic<std::uint64_t> healthyGeneration{0};std::atomic<bool> burstDone{false};std::atomic<DWORD> descendant{0};
    Harness(const std::filesystem::path& home,const Context& context,bool retained=false,std::function<ymcc::deckysidebar::LaunchAdmission()> admission={}){
        std::filesystem::create_directories(home);
        ymcc::deckysidebar::Config config;config.executable=detail::executable();config.home=home.wstring();
        config.preflight=[]{return std::string{};};config.ownedBinding=client;
        if(admission)config.launchAdmission=std::move(admission);else config.launchAdmission=[retained](){return ymcc::deckysidebar::LaunchAdmission{true,{},false,{},retained};};
        const auto cef=worker::number(context.lease.at("pid"));
        config.bindingPlan=[this,cef](ymcc::deckysidebar::BindingMode mode,std::uint64_t generation){
            ++plans;auto request=makeRequest(generation);request["bootstrap"]["fixtureContextPid"]=cef;
            if(mode==ymcc::deckysidebar::BindingMode::retained){request["requireFreshContext"]=false;request["retainedFrontendOnly"]=true;request["bootstrap"]["frontendBuildId"]=std::string(64,'d');}
            return ymcc::deckysidebar::BindingPlan{std::move(request),{}};
        };
        config.bindingDiagnostic=[this](const Json& packet){
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
            {"binding",state(client->snapshot())}};
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
inline Json rejectedFinalAdmission(const std::filesystem::path& output){
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    std::atomic<unsigned> admissions{0};
    Harness harness(output,context,false,[&]{const auto count=++admissions;return count==1?ymcc::deckysidebar::LaunchAdmission{true}:ymcc::deckysidebar::LaunchAdmission{false,"steam-ui-state-pending",true};});
    const auto before=handles();need(harness.runtime.setEnabled(true),"deferred-enable");
    need(waitFor([&]{const auto state=harness.runtime.snapshot();return state.phase=="waiting-manual-steam"&&state.reason=="steam-ui-state-pending";}),"final-admission-did-not-defer");
    const auto waiting=harness.client->snapshot();const auto phase=harness.runtime.snapshot();
    harness.runtime.shutdown();const auto after=harness.client->snapshot();
    return {{"id","native.typed-rejected-final-admission-releases-watch-before-off"},
        {"passed",admissions>=2&&phase.pid==0&&waiting.fullyDestroyed&&waiting.handles==0&&waiting.pendingIo==0&&after.fullyDestroyed&&before==handles()&&context.alive()},
        {"admissionReads",admissions.load()},{"loaderPid",phase.pid},{"beforeOff",state(waiting)},{"afterOff",state(after)},
        {"beforeHandles",before},{"afterHandles",handles()},{"cefUnaffected",context.alive()}};
}
inline Json ready(const std::filesystem::path& output,bool retained){
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    Harness harness(output,context,retained);const auto before=handles();
    need(harness.runtime.setEnabled(true),"ready-enable");
    harness.waitForCheckpoint([&]{return harness.healthyGeneration==1;},"initial-binding-checkpoint");
    const auto initial=harness.runtime.snapshot();const auto originalLoader=initial.pid;
    const auto originalBinding=harness.client->snapshot().processId;
    need(retained?(initial.phase=="frontend-attached"&&originalLoader==0):(initial.phase=="loader-started"&&originalLoader!=0),"initial-loader-ownership");
    const bool accepted=harness.runtime.requestBindingRefresh(9);
    const bool duplicate=harness.runtime.requestBindingRefresh(9),zero=harness.runtime.requestBindingRefresh(0);
    need(waitFor([&]{return harness.healthyGeneration==2;}),"ready-binding-checkpoint");
    const auto after=harness.runtime.snapshot();const auto nextBinding=harness.client->snapshot().processId;
    const bool sameOwnership=after.pid==originalLoader&&after.phase==initial.phase&&context.alive();
    harness.runtime.shutdown();const auto destroyed=harness.client->snapshot();
    return {{"id",retained?"native.typed-retained-ready-rebind-never-starts-loader":"native.typed-ready-rebind-keeps-original-owned-loader"},
        {"passed",accepted&&!duplicate&&!zero&&harness.plans==2&&harness.healthyGeneration==2&&sameOwnership&&nextBinding&&nextBinding!=originalBinding&&destroyed.fullyDestroyed&&before==handles()},
        {"acceptedReady",accepted},{"duplicateReadyRejected",!duplicate},{"zeroReadyRejected",!zero},{"plans",harness.plans.load()},
        {"initialLoaderPid",originalLoader},{"afterLoaderPid",after.pid},{"initialBindingPid",originalBinding},{"nextBindingPid",nextBinding},
        {"originalCefAlive",context.alive()},{"beforeHandles",before},{"afterHandles",handles()},{"atExit",state(destroyed)}};
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
    if(argc!=2)return 64;
    using namespace rtfixture;const auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);
    const auto sharedCryptoInitialization=initializeSharedCryptoForMeasurement();
    Json cases=Json::array();
    const auto test=[&](const char* name,const auto& run){try{cases.push_back(run());}catch(const std::exception& error){cases.push_back({{"id",name},{"passed",false},{"error",error.what()}});}};
    test("native.typed-runtime-live-watch",[&]{return healthy(output/"healthy");});
    test("native.typed-stalled-binding-stop",[&]{return stalled(output/"stalled-cv",L"runtime-stall-cv");});
    test("native.typed-stalled-binding-descendants",[&]{return stalled(output/"stalled-descendants",L"stubborn-descendant");});
    test("native.typed-rejected-final-admission",[&]{return rejectedFinalAdmission(output/"final-admission");});
    test("native.typed-ready-rebind",[&]{return ready(output/"ready",false);});
    test("native.typed-retained-ready-rebind",[&]{return ready(output/"retained-ready",true);});
    const auto passed=std::count_if(cases.begin(),cases.end(),[](const Json& row){return row.value("passed",false);});
    const Json result{{"schema","ymcc.typed-binding-runtime-gate.v1"},{"passed",static_cast<std::size_t>(passed)==cases.size()},
        {"tests",cases.size()},{"passedCount",passed},{"cases",cases},{"deadlineMs",3200},{"productCallbackMode","typed-only; legacy macro absent"},
        {"sharedCryptoInitialization",sharedCryptoInitialization},
        {"realSteamOrDeviceActions",0},{"originalHistoricalFailurePreserved",true},{"fieldWakeOrS4Verified",false},{"hostPackagePassed",false}};
    std::ofstream(output.parent_path()/"typed-runtime-result.json",std::ios::binary)<<result.dump(2)<<'\n';
    std::cout<<result.dump(2)<<'\n';return result.at("passed").get<bool>()?0:1;
}
