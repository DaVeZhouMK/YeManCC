#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#ifdef YMCC_BINDING_CONSTRUCTOR_FIXTURE
#include "fixtures/decky_binding_constructor_probe.inc"
namespace bindingReportProbe {
inline std::atomic<unsigned> selected{0},hits{0},site{0};inline std::atomic<bool> remaining{true},successfulResultSeen{false};
inline void arm(unsigned value) noexcept {
 if(selected.load()!=value||!remaining.exchange(false))return;
 ++hits;site=value;ymcc::deckybinding::fixture::constructionprobe::failures=0;ymcc::deckybinding::fixture::constructionprobe::failedBytes=0;
 ymcc::deckybinding::fixture::constructionprobe::skipAllocations=0;ymcc::deckybinding::fixture::constructionprobe::failNext=true;
}


inline thread_local bool insideJsonDestroy=false;
inline thread_local std::size_t jsonDestroyMembers=0;
inline std::atomic<unsigned> tailCallbacks{0};inline std::atomic<std::size_t> tailExpectedBytes{0};

inline thread_local bool borrowedOptional=false,borrowedReturned=false;
inline std::atomic<bool> borrowedPath{false},sourceImmutable{false},borrowedLogGolden{false};
inline std::atomic<unsigned> borrowedThrows{0};
struct BorrowedOptionalScope {
 bool active=selected.load()>=11&&selected.load()<=13;
 BorrowedOptionalScope() noexcept {if(active){borrowedOptional=true;borrowedReturned=false;}}
 ~BorrowedOptionalScope() noexcept {if(active){borrowedOptional=false;borrowedReturned=false;ymcc::deckybinding::fixture::constructionprobe::failNext=false;}}
};
inline void borrowedTerminate() noexcept {
 const char* kind="none";try{if(const auto error=std::current_exception())std::rethrow_exception(error);}catch(const std::bad_alloc&){kind="bad-alloc";}catch(...){kind="other";}
 std::fprintf(stderr,"BORROWED_DIAGNOSTIC_TERMINAL {\"exception\":\"%s\",\"failures\":%u,\"failedBytes\":%zu,\"insideJsonDestroy\":%u,\"members\":%zu,\"callbacks\":%u,\"site\":%u}\n",kind,ymcc::deckybinding::fixture::constructionprobe::failures.load(),ymcc::deckybinding::fixture::constructionprobe::failedBytes.load(),insideJsonDestroy?1u:0u,jsonDestroyMembers,tailCallbacks.load(),site.load());
 std::fflush(stderr);std::_Exit(std::strcmp(kind,"bad-alloc")==0?207:208);
}

struct JsonDestroyScope {
 bool active=selected.load()==9||selected.load()==10,prior=false;std::size_t priorMembers=0;
 explicit JsonDestroyScope(std::size_t count) noexcept {if(selected.load()>=11&&selected.load()<=13&&borrowedOptional&&borrowedReturned)active=true;if(active){prior=insideJsonDestroy;priorMembers=jsonDestroyMembers;insideJsonDestroy=true;jsonDestroyMembers=count;if(selected.load()==11&&borrowedOptional&&borrowedReturned&&count==5)arm(11);}}
 ~JsonDestroyScope() noexcept {if(active){insideJsonDestroy=prior;jsonDestroyMembers=priorMembers;}}
};

inline std::atomic<unsigned> reasonEntries{0},reasonExits{0},reasonUnused{0};
struct ReasonScope {
 bool active=selected.load()==5||selected.load()==6;
 ReasonScope() noexcept {if(active){++reasonEntries;const auto which=selected.load();arm(which);ymcc::deckybinding::fixture::constructionprobe::skipAllocations=which==6?1u:0u;}}
 ~ReasonScope() noexcept {if(active){if(ymcc::deckybinding::fixture::constructionprobe::failNext)++reasonUnused;ymcc::deckybinding::fixture::constructionprobe::failNext=false;ymcc::deckybinding::fixture::constructionprobe::skipAllocations=0;++reasonExits;}}
};
inline void armReason() noexcept {
 const auto which=selected.load();if(which!=5&&which!=6)return;
 arm(which);ymcc::deckybinding::fixture::constructionprobe::skipAllocations=which==6?1u:0u;
 successfulResultSeen=true;
}
}

#endif
// Real production Runtime + Client + Owner. Test peer and inert Loader/CEF
// are source-built copies in this evidence directory; no installed app is used.
#define main ymccBindingClientComponentSuite
#include "decky_binding_client_selftest.cpp"
#undef main
#ifdef YMCC_BINDING_LOADER_NATIVE_CLOSE_FIXTURE
#include "fixtures/decky_loader_native_close_probe.inc"
#endif
#ifdef YMCC_DECKY_REPORT_ALLOCATION_FIXTURE
namespace reportAllocationProbe {
inline std::atomic<unsigned> mode{0},budget{0},hits{0},lastAction{0};
inline std::atomic<bool> watchEndSelected{false};
inline bool watchEndConsumed() noexcept {return watchEndSelected.load()&&hits.load()==1&&lastAction.load()==9;}
struct Scope {
    bool armed=false;
    explicit Scope(unsigned action) noexcept {
        std::set_terminate([]{
            const char* kind="unknown";const char* message="";
            try {if(const auto error=std::current_exception())std::rethrow_exception(error);}
            catch(const std::bad_alloc&){kind="bad-alloc";}
            catch(const std::exception& error){kind="std-exception";message=error.what();}
            catch(...){kind="non-standard";}
            std::fprintf(stderr,"REPORT_ALLOCATION_TERMINAL kind=%s hits=%u failures=%u bytes=%zu action=%u message=%s\n",
                kind,hits.load(),ymcc::deckybinding::fixture::constructionprobe::failures.load(),
                ymcc::deckybinding::fixture::constructionprobe::failedBytes.load(),lastAction.load(),message);
            std::fflush(stderr);std::_Exit(std::strcmp(kind,"bad-alloc")==0?207:208);
        });
        // Action::stopped is original enum6. No timing or action is changed.
        const auto which=mode.load();
        if((which==1&&action!=6)||(which==2&&action==6)) {
            auto left=budget.load();
            if(left && budget.compare_exchange_strong(left,left-1)) {
                ++hits;lastAction=action;
                ymcc::deckybinding::fixture::constructionprobe::skipAllocations=0;
                ymcc::deckybinding::fixture::constructionprobe::failNext=true;armed=true;
            }
        }
    }
    ~Scope() noexcept {if(armed)ymcc::deckybinding::fixture::constructionprobe::failNext=false;}
};
}
#endif
#ifdef YMCC_BINDING_CONSTRUCTOR_FIXTURE
namespace runtimephaseprobe {
inline std::atomic<unsigned> samples{0},allocations{0},nested{0};
inline std::atomic<std::size_t> bytes{0};
struct Scope {
 bool done=false,prior=false;
 Scope() noexcept {
  namespace d=ymcc::deckybinding::fixture::diagnosticprobe;
  prior=d::armed;if(prior)++nested;d::localAllocations=0;d::localBytes=0;d::armed=true;
 }
 void complete() noexcept {
  if(done)return;done=true;namespace d=ymcc::deckybinding::fixture::diagnosticprobe;
  d::armed=prior;++samples;allocations+=d::localAllocations;bytes+=d::localBytes;
 }
 ~Scope() noexcept {complete();}
};
}
#endif
namespace rtHealthWakeProbe {
inline thread_local unsigned* signals = nullptr;
inline BOOL signal(HANDLE event) {
    if (signals) ++*signals;
    return ::SetEvent(event);
}
}
#define SetEvent(event) rtHealthWakeProbe::signal(event)
#include "../native/decky_sidebar_runtime.h"
#undef SetEvent
#include "../native/decky_resource_worker.h"
#include "../native/steam_ui_mode_observer.h"
#include "../native/steam_cssloader_mode.h"
#include "../native/decky_update_recovery.h"
#include "../native/decky_sidebar_diagnostics.h"
#include "../native/sleep_power_state.h"
#include <chrono>
#include <condition_variable>
namespace rtfixture {
using namespace ymcc::deckybinding;
using namespace ymcc::deckybinding::fixture;
using Json=nlohmann::json;
inline std::string preResultRetirementProbe;
inline std::string preResultFailureReason(){
    return preResultRetirementProbe=="callback"?"mirror-bootstrap-callback-failed":
        preResultRetirementProbe=="context"?"mirror-bootstrap-context-failed":"mirror-bootstrap-cleanup-incomplete";
}
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
        const std::string text(incoming.payload.begin(),incoming.payload.end());
#ifdef YMCC_BINDING_CONSTRUCTOR_FIXTURE
        const auto envelope=Json::parse(text);
        if(envelope.value("schema",std::string{})==ymcc::deckyresources::worker::schema){
            return ymcc::deckyresources::worker::tryRun(envelope,channel.cancelEvent(),[&](const Json& packet){
                auto reply=packet;
                const auto replyMode=environment(L"YMCC_RESOURCE_RESERVED_REPLY");
                if(reply.contains("detail")){
                    if(replyMode==L"stall")Sleep(10000); // Actual parent deadline must collect this owned child.
                    if(replyMode==L"1"||replyMode==L"deadline"){
                        reply["detail"]["ready"]=false;
                        reply["detail"]["reason"]=replyMode==L"deadline"?"resource-worker-deadline":"resource-worker-prewrite-memory-unavailable";
                    }
                }
                const auto body=reply.dump();need(bool(channel.writeFrame(bytes(body),1000)),"resource-real-worker-emit");
            },exit);
        }
#endif
        const auto request=worker::parse(text);(void)request;
        detail::Handle entered{OpenEventW(EVENT_MODIFY_STATE,FALSE,environment(L"YMCC_BINDING_CLIENT_FIXTURE_READY").c_str())};
        detail::Handle release{OpenEventW(SYNCHRONIZE,FALSE,environment(L"YMCC_BINDING_CLIENT_FIXTURE_RELEASE").c_str())};
        need(entered.value&&release.value,"stalled-controls");
        if(behavior==L"runtime-success-checkpoint"||behavior==L"runtime-late-result"||behavior==L"runtime-success-exit"){
            detail::Handle context{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,worker::number(request.bootstrap.at("fixtureContextPid")))};
            need(context.value!=nullptr,"checkpoint-context");
            const auto emit=[&](const char* kind,const Json& detail){const Json packet{{"schema",worker::schema},{"id",request.id},{"generation",std::to_string(request.generation)},{"kind",kind},{"detail",detail}};need(bool(channel.writeFrame(bytes(packet.dump()),5000)),"checkpoint-emit");};
            if(request.bootstrap.value("fixtureSessionReject",false)){
                Json rejected{{"ok",false},{"reason",request.bootstrap.value("fixtureFailureReason",std::string("mirror-bootstrap-session-changed"))},{"fixtureResultGeneration",std::to_string(request.generation)}};
                if(rejected.at("reason")=="mirror-bootstrap-cleanup-incomplete"){
                    rejected["error"]=request.bootstrap.value("fixtureCloseAccepted",false)?DWORD{WAIT_TIMEOUT}:DWORD{ERROR_ACCESS_DENIED};
                    rejected["closeAccepted"]=request.bootstrap.value("fixtureCloseAccepted",false);rejected["finalNotification"]=false;
                }
                if(request.bootstrap.contains("fixtureMutation"))rejected["mutated"]=request.bootstrap.at("fixtureMutation");
                emit("result",rejected);SetEvent(entered.value);
                // The original Client must cancel and collect this exact child.
                // This is a protocol fixture, not another WinHTTP cleanup actor.
                if(request.bootstrap.contains("fixtureFailureReason"))channel.waitCancelled(15000);
                exit=2;return true;
            }
            if(behavior==L"runtime-late-result"){
                SetEvent(entered.value);WaitForSingleObject(release.value,INFINITE);
                detail::Handle attempted{OpenEventW(EVENT_MODIFY_STATE,FALSE,(environment(L"YMCC_BINDING_CLIENT_FIXTURE_READY")+L"-late-attempt").c_str())};
                need(attempted.value!=nullptr,"late-attempt-event");SetEvent(attempted.value);
            }
            Json result{{"ok",request.bootstrap.value("fixtureResultOk",true)},{"mutated",request.bootstrap.value("fixtureResultMutated",true)},{"fixtureResultGeneration",std::to_string(request.generation)},{"contextLease",identity(context.value)}};
            if(request.bootstrap.contains("fixtureResultReason"))result["reason"]=request.bootstrap.at("fixtureResultReason");
            emit("result",result);
            emit("diagnostic",{{"fixtureHealthyGeneration",std::to_string(request.generation)}});
            SetEvent(entered.value);
            if(behavior==L"runtime-success-exit"){WaitForSingleObject(release.value,INFINITE);exit=93;return true;}
            if(const auto fixture=request.bootstrap.value("fixtureWatchReattach",std::string{});!fixture.empty()){
                need(WaitForSingleObject(release.value,5000)==WAIT_OBJECT_0,"watch-reattach-release");
                const auto tick=GetTickCount64();
                emit("diagnostic",{{"bootstrapStage","context-attached"},{"contextGeneration",2ULL},
                    {"contextPid",fixture=="foreign"?GetCurrentProcessId():GetProcessId(context.value)},
                    {"runId",request.bootstrap.at("runId")},{"contextAttachedTick",fixture=="stale"?tick-8000:tick}});
            }
            // The late-page control observes the original 20s settling window.
            channel.waitCancelled(request.bootstrap.value("fixtureLatePageWindow",false)?25000:15000);exit=0;return true;
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
inline bool blockedOwnerNativeSelected=false;inline std::atomic<unsigned> blockedOwnerNativeReserves{0};
template<class C> bool configureBorrowedDiagnostic(C& config,const std::function<void(const Json&,const char*)>& callback){
 if constexpr(requires(C& value){value.bindingDiagnosticBorrowed;}){config.bindingDiagnosticBorrowed=callback;return true;}
 else {(void)config;(void)callback;return false;}
}
struct Harness;
inline std::function<void(Harness&,ymcc::deckysidebar::Config&)> borrowedWatchSetup; // Explicit fixture parameter only.
struct Harness {
    detail::Handle preResultChild,preResultQuery;
    Json preResultChildIdentity;
    std::atomic<unsigned> preResultFailures{0};
    std::shared_ptr<Client> client=std::make_shared<Client>();
    ymcc::deckysidebar::Runtime runtime;
    std::atomic<unsigned> plans{0},diagnostics{0},preparations{0},results{0},watchFailures{0},planStarts{0},planFinishes{0},loaderFaultPublications{0},heldEndpointTerminals{0};std::atomic<std::uint64_t> resultGeneration{0};std::atomic<std::uint64_t> healthyGeneration{0},heldLeafBirth{0};std::atomic<bool> burstDone{false};std::atomic<DWORD> descendant{0};
    std::atomic<unsigned> tempFacts{0},tempUnits{0},globalConnected{0};std::atomic<bool> tempShort{false};
    Json globalRecoveryLog=Json::array(); // Worker-only writes; read after shutdown joins.
    Json failureTraceLog=Json::array();
    Json finalAdmissionLog=Json::array(); // Worker-only writes; read after shutdown joins.
    Json loaderStopLog=Json::array();std::atomic<bool> throwLoaderStopDiagnostic{false};
    std::atomic<bool> throwFailureTrace{false},throwFailureContext{false},poisonContextAfterResult{false};
    Harness(const std::filesystem::path& home,const Context& context,bool retained=false,std::function<ymcc::deckysidebar::LaunchAdmission()> admission={},std::function<void(Json&,unsigned)> plan={},std::function<std::string()> preflight={},std::function<void()> recovery={},std::function<DWORD()> recoveryTimeout={},std::function<ymcc::deckysidebar::ConnectionHealth()> health={},std::function<Json(ymcc::deckysidebar::ConnectionRecoveryWindow::Action)> step={},std::function<std::string(const std::function<bool()>&)> preparation={},std::function<void(const ymcc::deckysidebar::State&)> changed={},std::function<void(ymcc::deckysidebar::Config&)> customize={}){
        std::filesystem::create_directories(home);
        ymcc::deckysidebar::Config config;config.executable=detail::executable();config.home=home.wstring();
        config.changed=[this,changed=std::move(changed)](const auto& value){if(value.reason=="loader-exited"||value.reason=="loader-crash-loop")++loaderFaultPublications;if(changed)changed(value);};
        if(preflight)config.preflight=std::move(preflight);else config.preflight=[]{return std::string{};};config.ownedBinding=client;
        if(preparation)config.prepareEnvironment=[this,prepare=std::move(preparation)](const std::function<bool()>& current){++preparations;return prepare(current);};
        else config.prepareEnvironment=[this,retained](const std::function<bool()>& current){need(current(),"current resource preparation");++preparations;if(retained)Sleep(180);return std::string{};};
        if(admission)config.launchAdmission=std::move(admission);else config.launchAdmission=[retained](){return ymcc::deckysidebar::LaunchAdmission{true,{},false,{},retained};};
        const auto cef=worker::number(context.lease.at("pid"));
        config.bindingPlan=[this,cef,plan=std::move(plan)](ymcc::deckysidebar::BindingMode mode,std::uint64_t generation,const auto&){
            const auto attempt=++plans;auto request=makeRequest(generation);request["bootstrap"]["fixtureContextPid"]=cef;
            if(mode==ymcc::deckysidebar::BindingMode::retained){request["requireFreshContext"]=false;request["retainedFrontendOnly"]=true;request["bootstrap"]["frontendBuildId"]=std::string(64,'d');}
            if(plan)plan(request,attempt);
            return ymcc::deckysidebar::BindingPlan{std::move(request),{}};
        };
        config.connectionRecovery=std::move(recovery);config.connectionRecoveryTimeout=std::move(recoveryTimeout);config.connectionHealth=std::move(health);config.connectionStep=std::move(step);
        config.bindingDiagnostic=[this](const Json& packet){
            if(!preResultRetirementProbe.empty()&&packet.value("bindingPacketKind",std::string{})=="result"&&
               packet.value("ok",true)==false&&packet.value("reason",std::string{})==preResultFailureReason()){
                HANDLE ready=nullptr,child=nullptr;
                need(client->duplicateWaitHandles(ready,child),"pre-result-original-child-handles");
                if(ready)CloseHandle(ready);need(child!=nullptr,"pre-result-exact-child");
                preResultChild.value=child;
                const auto original=client->snapshot();
                need(WaitForSingleObject(child,0)==WAIT_TIMEOUT,"pre-result-original-child-live-before-query");
                preResultQuery.value=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,original.processId);
                need(preResultQuery.value&&WaitForSingleObject(child,0)==WAIT_TIMEOUT,"pre-result-child-live-through-query");
                preResultChildIdentity=identity(preResultQuery.value);++preResultFailures;
            }
            if(packet.value("scope",std::string{})=="loader-stop"){
                loaderStopLog.push_back(ymcc::deckydiag::record("bootstrap-watch",packet));
                if(throwLoaderStopDiagnostic.load())throw std::runtime_error("loader-stop-optional-log-fault");
            }
            if(packet.value("scope",std::string{})=="final-launch-admission"){
                finalAdmissionLog.push_back(ymcc::deckydiag::record("bootstrap-watch",packet));
                throw std::runtime_error("final-admission-optional-log-fault");
            }
            if(poisonContextAfterResult.load()&&packet.value("bindingPacketKind",std::string{})=="result")throwFailureContext=true;
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
            if(packet.contains("fixtureResultGeneration")){resultGeneration=worker::decimal(packet.at("fixtureResultGeneration"));++results;if(poisonContextAfterResult.load())throwFailureContext=true;}
            if(packet.value("bootstrapStage",std::string{})=="failed")++watchFailures;
            if(packet.contains("fixtureHealthyGeneration"))healthyGeneration=worker::decimal(packet.at("fixtureHealthyGeneration"));
            if(packet.contains("index"))++diagnostics;
            if(packet.value("fixtureDone",false))burstDone=true;
            if(packet.contains("fixtureLeaf"))heldLeafBirth=worker::decimal(packet.at("fixtureLeaf").at("birth"));
            if(packet.value("reason",std::string{})=="binding-worker-exited")++heldEndpointTerminals;
            if(packet.contains("descendantPid"))descendant=worker::number(packet.at("descendantPid"));
#ifdef YMCC_BINDING_CONSTRUCTOR_FIXTURE

            if(bindingReportProbe::selected.load()>=11&&bindingReportProbe::selected.load()<=13&&packet.value("bindingPacketKind",std::string{})=="result"&&packet.value("ok",false)){
                ++bindingReportProbe::tailCallbacks;bindingReportProbe::successfulResultSeen=true;bindingReportProbe::tailExpectedBytes=80;std::set_terminate(bindingReportProbe::borrowedTerminate);bindingReportProbe::borrowedReturned=true;
            }
            if((bindingReportProbe::selected==9||bindingReportProbe::selected==10)&&packet.value("bindingPacketKind",std::string{})=="result"&&packet.value("ok",false)){
                bindingReportProbe::tailExpectedBytes=packet.size()*sizeof(Json);++bindingReportProbe::tailCallbacks;
                if(bindingReportProbe::selected==9){std::set_terminate([]{
  const char* kind="none";try{if(const auto error=std::current_exception())std::rethrow_exception(error);}catch(const std::bad_alloc&){kind="bad-alloc";}catch(const std::exception&){kind="std-exception";}catch(...){kind="other";}
  std::fprintf(stderr,"DIAGNOSTIC_TAIL_TERMINAL {\"exception\":\"%s\",\"failures\":%u,\"failedBytes\":%zu,\"insideJsonDestroy\":%u,\"members\":%zu,\"expectedBytes\":%zu,\"callbacks\":%u,\"successfulResultObserved\":%u,\"probeHits\":%u,\"probeSite\":%u}\n",
   kind,ymcc::deckybinding::fixture::constructionprobe::failures.load(),ymcc::deckybinding::fixture::constructionprobe::failedBytes.load(),bindingReportProbe::insideJsonDestroy?1u:0u,bindingReportProbe::jsonDestroyMembers,bindingReportProbe::tailExpectedBytes.load(),bindingReportProbe::tailCallbacks.load(),bindingReportProbe::successfulResultSeen?1u:0u,bindingReportProbe::hits.load(),bindingReportProbe::site.load());
  std::fflush(stderr);std::_Exit(std::strcmp(kind,"bad-alloc")==0?207:208);
 });bindingReportProbe::arm(9);}
            }
#endif
        };
        config.failureContext=[this,cef,&context]{
            if(throwFailureContext.load())throw std::runtime_error("diagnostic-context-fault");
            ymcc::steamfailure::FailureContext value;value.steamPid=cef;value.steamBirth=worker::decimal(context.lease.at("birth"));
            value.powerGeneration=123;
#ifdef YMCC_BINDING_CONSTRUCTOR_FIXTURE
            bindingReportProbe::arm(4);
#endif
            return value;
        };
        config.failureDiagnostic=[this](const Json& receipt,const ymcc::steamfailure::FailureContext& context){
            auto row=ymcc::deckydiag::failureRecord(receipt,context);failureTraceLog.push_back(std::move(row));
            if(throwFailureTrace.load())throw std::runtime_error("diagnostic-write-fault");
#ifdef YMCC_BINDING_CONSTRUCTOR_FIXTURE
            if(context.phase==ymcc::steamfailure::Phase::Finished&&receipt.value("ok",false)&&receipt.contains("fixtureResultGeneration")){
                bindingReportProbe::successfulResultSeen=true;bindingReportProbe::arm(3);
            }
#endif
        };
        if(blockedOwnerNativeSelected)config.reserveConnectionStep=[](auto){++blockedOwnerNativeReserves;return std::shared_ptr<ymcc::steamcommand::Reservation>{};};
        if(borrowedWatchSetup)borrowedWatchSetup(*this,config);
        if(customize)customize(config);
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
    Harness* sessionOwner=nullptr;std::atomic<unsigned> positiveRetryHealthReads{0};
    bool previousReturnedBeforePlan=false;DWORD previousExit=STILL_ACTIVE;
    Json previousAtPlan;
    const auto health=[&]{
        const bool returned=shouldHeal&&sessionOwner&&sessionOwner->results.load()>0;
        if(returned)++positiveRetryHealthReads;
        ymcc::deckysidebar::ConnectionHealth value{shouldHeal,returned,"page-recovered-before-binding-retry",false,"session-retry-fixed-owner"};
        value.transportConnected=returned;return value;
    };
    Harness h(output,context,retained,[&]{return deny.load()?ymcc::deckysidebar::LaunchAdmission{false,"steam-session-unconfirmed",false}:ymcc::deckysidebar::LaunchAdmission{true,{},false,{},retained};},
        [&](Json& request,unsigned attempt){
            if(attempt!=1){
                if(!preResultRetirementProbe.empty()){
                    need(sessionOwner&&sessionOwner->preResultChild.value,"pre-result-held-first-child");
                    const auto prior=sessionOwner->client->snapshot();previousAtPlan=state(prior);
                    previousReturnedBeforePlan=prior.fullyDestroyed&&prior.handles==0&&prior.pendingIo==0&&prior.activeCalls==0&&prior.jobActiveProcesses==0&&
                        WaitForSingleObject(sessionOwner->preResultChild.value,0)==WAIT_OBJECT_0&&GetExitCodeProcess(sessionOwner->preResultQuery.value,&previousExit)&&previousExit==2;
                    need(previousReturnedBeforePlan,"pre-result-prior-child-not-returned-before-next-plan");
                }
                return;
            }
            auto& bootstrap=request["bootstrap"];bootstrap["fixtureSessionReject"]=true;
            if(!preResultRetirementProbe.empty()){
                bootstrap["fixtureFailureReason"]=preResultFailureReason();
                bootstrap["fixtureCloseAccepted"]=preResultRetirementProbe=="cleanup-final-pending";
            }
            if(variant=="true")bootstrap["fixtureMutation"]=true;
            else if(variant=="null")bootstrap["fixtureMutation"]=nullptr;
            else if(variant=="string")bootstrap["fixtureMutation"]="false";
            else if(variant=="number")bootstrap["fixtureMutation"]=0;
            else if(variant!="missing")bootstrap["fixtureMutation"]=false;
            if(admission)deny=true;
        },{},{},{},health);sessionOwner=&h;
    const auto before=handles();need(h.runtime.setEnabled(true),"session-revalidation-enable");
    const bool invalidProtocol=variant=="null"||variant=="missing"||variant=="string"||variant=="number";
    need(waitFor([&]{return invalidProtocol?h.runtime.snapshot().phase=="unavailable":h.results>=1;}),"session-rejection-not-consumed");
    const auto began=GetTickCount64();bool healed=false;
    if(off){h.runtime.setEnabled(false);Sleep(400);}
    else if(shouldHeal)healed=waitFor([&]{return h.healthyGeneration>=2;},1500);
    else Sleep(700); // Original first retry is 250 ms. Observe beyond it; never advance a fake clock.
    const auto measured=h.runtime.snapshot();const auto plans=h.plans.load();
    const bool admissionHeld=!admission||measured.phase=="waiting-manual-steam";
    const auto preResultChildIdentity=h.preResultChildIdentity;
    const auto preResultFailureCount=h.preResultFailures.load();
    DWORD childLeaseCloseError=0;need(h.preResultChild.close(childLeaseCloseError),"pre-result-fixture-read-lease-close");
    need(h.preResultQuery.close(childLeaseCloseError),"pre-result-fixture-query-lease-close");
    const bool malformedRefused=!invalidProtocol||(h.results==0&&measured.reason!="mirror-bootstrap-session-changed");
    h.runtime.shutdown();const auto destroyed=h.client->snapshot();
    const bool ownership=retained?measured.pid==0:(shouldHeal?measured.pid!=0:measured.pid==0);
    const bool pass=(shouldHeal?(healed&&plans==2&&positiveRetryHealthReads>0):(!healed&&plans==1))&&ownership&&admissionHeld&&malformedRefused&&
        (!off||!measured.enabled)&&destroyed.fullyDestroyed&&destroyed.handles==0&&destroyed.pendingIo==0&&before==handles()&&context.alive();
    const bool finiteRetryOkay=preResultRetirementProbe.empty()||!shouldHeal||previousReturnedBeforePlan;
    return {{"id","native.typed-history-session-"+variant+(retained?"-retained":"-owned")},{"passed",pass&&finiteRetryOkay},
        {"preResultRetirementProbe",preResultRetirementProbe},{"preResultFailureReason",preResultFailureReason()},
        {"preResultFailures",preResultFailureCount},{"firstChildIdentity",preResultChildIdentity},
        {"previousReturnedBeforePlan",previousReturnedBeforePlan},{"previousAtPlan",previousAtPlan},{"previousExitCode",previousExit},
        {"positiveHealthReadsDuringRetry",positiveRetryHealthReads.load()},
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
// Same original automatic resource/admission case: a UI readiness hint must
// not revoke resource work, while actual OFF/session/Ready boundaries still do.
inline Json preparationAdmissionHints(const std::filesystem::path& output,const Context& context){
    const auto initialHandles=handles();Json rows=Json::array();bool passed=true;
    for(const std::string boundary:{"hint-only","off","session","ready"}){
        std::atomic<unsigned> reads{0},canceled{0},completed{0};std::atomic<bool> entered{false},release{false};
        const auto prepare=[&](const std::function<bool()>& current){
            if(++reads==1)return std::string{};
            entered=true;const auto until=GetTickCount64()+1500;
            while(!release&&current()&&GetTickCount64()<until)Sleep(1);
            if(!current()){++canceled;return std::string("preparation-cancelled");}
            if(!release)return std::string("resource-worker-deadline");
            ++completed;return std::string{};
        };
        Harness h(output/boundary,context,false,[]{return ymcc::deckysidebar::LaunchAdmission{false,"steam-ui-state-pending",true};},{},{},{},{},{},{},prepare);
        struct ReleaseBeforeOwner{std::atomic<bool>& value;~ReleaseBeforeOwner(){value=true;}} cleanup{release};
        const auto before=handles();need(h.runtime.setEnabled(true),"preparation-hint-enable");
        need(waitFor([&]{return reads==1&&h.runtime.snapshot().phase=="waiting-manual-steam";},1500),"preparation-hint-initial-wait");
        need(h.runtime.requestPreparationRefresh(77),"preparation-hint-ready");need(waitFor([&]{return entered.load();},1500),"preparation-hint-read-entered");
        unsigned accepted=0;for(unsigned n=0;n<32;++n)accepted+=h.runtime.requestLaunchAdmissionRefresh()?1u:0u;
        Sleep(20);const auto canceledBeforeBoundary=canceled.load();bool boundaryAccepted=true,duplicateRejected=true,offRejected=true;
        if(boundary=="off"){boundaryAccepted=h.runtime.setEnabled(false);offRejected=!h.runtime.requestLaunchAdmissionRefresh();}
        else if(boundary=="session")h.runtime.observeSteam(1,true);
        else if(boundary=="ready"){boundaryAccepted=h.runtime.requestPreparationRefresh(78);duplicateRejected=!h.runtime.requestPreparationRefresh(78);}
        if(boundary=="session"||boundary=="ready")need(waitFor([&]{return canceled.load()>canceledBeforeBoundary;},1500),"real-boundary-must-cancel-preparation");
        release=true;if(boundary!="off")need(waitFor([&]{return completed.load()>0;},1500),"preparation-hint-completion");
        h.runtime.shutdown();const auto resources=h.client->snapshot();
        const bool expected=boundary=="hint-only"?reads==2&&canceled==0&&completed==1:
            boundary=="off"?reads==2&&canceled==1&&completed==0:reads==3&&canceled==1&&completed==1;
        const bool ok=accepted==32&&canceledBeforeBoundary==0&&boundaryAccepted&&duplicateRejected&&offRejected&&expected&&
            h.plans==0&&resources.fullyDestroyed&&resources.handles==0&&resources.pendingIo==0&&resources.jobActiveProcesses==0&&before==handles()&&context.alive();
        passed=passed&&ok;rows.push_back({{"boundary",boundary},{"passed",ok},{"acceptedHints",accepted},{"reads",reads.load()},{"canceledBeforeBoundary",canceledBeforeBoundary},
            {"canceled",canceled.load()},{"completed",completed.load()},{"boundaryAccepted",boundaryAccepted},{"duplicateReadyRejected",duplicateRejected},{"offHintRejected",offRejected},
            {"bindingPlans",h.plans.load()},{"afterOff",state(resources)},{"beforeHandles",before},{"afterHandles",handles()}});
    }
    for(const std::string boundary:{"deadline-ready","deadline-session","ordinary-failure","cleanup-ready","cleanup-session","cleanup-preflight"}){
        const bool parked=boundary!="ordinary-failure",launch=boundary=="cleanup-preflight";
        const std::string failure=boundary.starts_with("cleanup-")?"resource-worker-cleanup-incomplete":
            parked?"resource-worker-deadline":"resource-hash-mismatch";
        std::atomic<unsigned> reads{0},admissions{0},preflights{0};std::atomic<bool> allowed{false};
        Harness h(output/boundary,context,false,[&]{++admissions;return allowed.load()?ymcc::deckysidebar::LaunchAdmission{true}:
                ymcc::deckysidebar::LaunchAdmission{false,"steam-ui-state-pending",true};},
            {},[&]{++preflights;return std::string("resource-hash-mismatch");},{},{},{},{},[&](const std::function<bool()>&){
                if(++reads==1)return failure;
                return std::string{};
            });
        const auto before=handles();need(h.runtime.setEnabled(true),"prewarm-deadline-enable");
        need(waitFor([&]{return reads==1&&h.runtime.snapshot().phase=="waiting-manual-steam";},1500),"prewarm-deadline-initial-result");
        for(unsigned hint=0;hint<4;++hint){
            const auto prior=admissions.load();need(h.runtime.requestLaunchAdmissionRefresh(),"prewarm-deadline-hint-accepted");
            need(waitFor([&]{return admissions>prior;},500),"prewarm-deadline-hint-consumed");
        }
        // Observe the completed worker turn, not just entry to its admission read.
        if(!parked)need(waitFor([&]{return reads>=2;},500),"ordinary-failure-no-longer-rechecked");
        const auto beforeBoundary=reads.load();
        bool boundaryAccepted=true;
        if(boundary.ends_with("-ready"))boundaryAccepted=h.runtime.requestPreparationRefresh(79);
        if(boundary.ends_with("-session"))h.runtime.observeSteam(1,true);
        if(launch){
            allowed=true;boundaryAccepted=h.runtime.requestLaunchAdmissionRefresh();
            need(waitFor([&]{return preflights==1&&h.runtime.snapshot().reason=="resource-hash-mismatch";},1500),"parked-prewarm-blocked-admitted-preflight");
        }else if(parked)need(waitFor([&]{return reads>=2;},1500),"prewarm-real-boundary-did-not-recheck");
        h.runtime.shutdown();const auto resources=h.client->snapshot();
        const bool ok=beforeBoundary==(parked?1u:2u)&&reads==(launch?1u:2u)&&preflights==(launch?1u:0u)&&boundaryAccepted&&
            !h.runtime.requestLaunchAdmissionRefresh()&&h.plans==0&&resources.fullyDestroyed&&
            resources.handles==0&&resources.pendingIo==0&&resources.jobActiveProcesses==0&&before==handles()&&context.alive();
        passed=passed&&ok;rows.push_back({{"boundary",boundary},{"passed",ok},{"readsBeforeBoundary",beforeBoundary},
            {"reads",reads.load()},{"preflightReads",preflights.load()},{"bindingPlans",h.plans.load()},{"beforeHandles",before},{"afterHandles",handles()}});
    }
    return {{"passed",passed&&initialHandles==handles()&&context.alive()},{"cases",rows},{"hintsPerCase",32},{"beforeHandles",initialHandles},{"afterHandles",handles()},{"cefUnaffected",context.alive()}};
}
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
    const auto preparationHints=variant=="automatic"?preparationAdmissionHints(output/"preparation-hints",context):Json{{"passed",true},{"selected",false}};
    Json retryOnHint={{"passed",true},{"selected",false}};
    if(variant=="automatic"){
        std::atomic<unsigned> hintReads{0};std::atomic<bool> hintDenied{false};
        std::atomic<ULONGLONG> opened{0},earlyCheck{0},second{0},third{0};
        bool firstHealth=true; // Read only by Runtime's worker.
        Harness hinted(output/"resource-retry-with-mode-hints",context,false,[&]{
            return hintDenied.load()?ymcc::deckysidebar::LaunchAdmission{false,"steam-ui-state-pending",true,"shared-mode-pending"}:
                ymcc::deckysidebar::LaunchAdmission{true};
        },{},[&]{
            const auto read=++hintReads;
            if(read==1)Sleep(2400); // Cleaned timeout after a slow prewarm.
            const auto now=GetTickCount64();
            if(read==1)opened=now;
            if(read==2)second=now;
            if(read==3)third=now;
            if(read<=2){hintDenied=true;return std::string("resource-worker-deadline");}
            return std::string{};
        },{},{},[&]{
            // Health can sample pending before admission sees the shared mode
            // publication. Its existing 5s check then overlaps the resource wait.
            if(std::exchange(firstHealth,false))return ymcc::deckysidebar::ConnectionHealth{false,false,"steam-bigpicture-unconfirmed",true};
            if(hintDenied&&hintReads==1)earlyCheck=GetTickCount64();
            return hintDenied?ymcc::deckysidebar::ConnectionHealth{false,false,"steam-bigpicture-unconfirmed",true}:
                ymcc::deckysidebar::ConnectionHealth{true,false,"authenticated-peer-missing",false,"same-steam-session"};
        },{},[](const auto& current){
            Sleep(2350);need(current(),"resource-retry-hint-current-prewarm");return std::string{};
        });
        const auto hintHandles=handles();need(hinted.runtime.setEnabled(true),"resource-retry-hint-enable");
        const auto parked=[&](unsigned n){return hintReads==n&&hinted.runtime.snapshot().phase=="waiting-manual-steam";};
        need(waitFor([&]{return parked(1);},6500),"resource-retry-hint-first-park");
        const auto openedTick=opened.load(),firstHint=GetTickCount64();
        hintDenied=false;need(hinted.runtime.requestLaunchAdmissionRefresh(),"resource-retry-first-mode-return");
        // Nothing independently wakes the second denial: it is first visible
        // at the original 3s resource slot. Do not assume the 1s attempt reads
        // admission again after it fails.
        need(waitFor([&]{return parked(2);},3500),"resource-retry-hint-second-park");
        while(GetTickCount64()-opened<3200)Sleep(20);
        const bool quiet=hintReads==2&&hinted.plans==0;
        const auto lateHint=GetTickCount64();
        hintDenied=false;need(hinted.runtime.requestLaunchAdmissionRefresh(),"resource-retry-second-mode-return");
        const bool resumed=waitFor([&]{return hinted.healthyGeneration==1;},2500);
        hinted.runtime.shutdown();const auto resources=hinted.client->snapshot();
        const auto delay=second-opened;
        const auto check=earlyCheck.load();
        const bool early=check>=openedTick&&check<=firstHint&&firstHint<openedTick+1000;
        retryOnHint={{"passed",early&&delay>=1000&&quiet&&resumed&&hintReads==3&&hinted.plans==1&&third>=lateHint&&
                resources.fullyDestroyed&&resources.handles==0&&hintHandles==handles()},
            {"selected",true},{"earlyBoundaryExercised",early},{"healthWakeAfterFailureMs",check>=openedTick?check-openedTick:0},
            {"firstHintAfterFailureMs",firstHint-openedTick},{"secondPreflightAfterMs",delay},
            {"lateHintAfterFailureMs",lateHint-openedTick},{"thirdPreflightAfterFailureMs",third>=openedTick?third-openedTick:0},
            {"quietWhileAdmissionUnknown",quiet},
            {"resumed",resumed},{"preflightReads",hintReads.load()},{"bindingPlans",hinted.plans.load()},
            {"beforeHandles",hintHandles},{"afterHandles",handles()}};
    }
    return {{"id","native.typed-history-resource-handoff-"+variant},{"preparationHints",preparationHints},{"retryOnHint",retryOnHint},
        {"passed",preparationHints.value("passed",false)&&retryOnHint.value("passed",false)&&deniedReads==1&&deniedPlans==0&&expected&&pairedPlanDiagnostics&&
        destroyed.fullyDestroyed&&destroyed.handles==0&&destroyed.pendingIo==0&&before==handles()&&context.alive()},
        {"variant",variant},{"preflightReads",inspections},{"bindingPlans",plans},{"readsWhileDenied",deniedReads},{"plansWhileDenied",deniedPlans},
        {"recovered",healed},{"pairedPlanDiagnostics",pairedPlanDiagnostics},{"elapsedFromFirstFailureMs",elapsed},{"phase",phase.phase},{"reason",phase.reason},
        {"afterOff",state(destroyed)},{"beforeHandles",before},{"afterHandles",handles()},{"cefUnaffected",context.alive()}};
}
inline Json rejectedFinalAdmission(const std::filesystem::path& output,std::string_view variant={}){
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    const ymcc::steamsession::Session session{L"inert-steam",41,1,7};
    const bool localBusy=variant=="local-busy"||variant=="local-busy-off"||variant=="local-busy-page";
    const bool malformedBusy=variant=="malformed-busy",externalBusy=variant=="transport-busy";
    const bool busy=localBusy||malformedBusy||externalBusy;
    const bool early=variant=="connection-early";
    const bool stopDuringPreflight=variant=="connection-preflight-off";
    const bool scoped=!variant.empty(),connection=scoped&&!busy&&variant!="mode-same";
    const ymcc::deckysidebar::LaunchBindingScope prepared{session,session.account,1,"doc:1"};
    std::atomic<unsigned> reads{0},preflights{0};std::atomic<bool> settle{false};
    std::atomic<bool> secondPreflightEntered{false},preflightRevoked{false},loaderPublished{false};
    ymcc::steamui::ModeObserver shared;
    ymcc::steamui::ModeObserverConfig mode;mode.debounceMs=1;mode.minimumGapMs=1;
    mode.readOwned=[&](const auto& s,const auto& current){
        const auto read=++reads;
        if(busy&&read==2){
            Json facts={{"known",true},{"running",true},{"mode","unknown"},{"pid",malformedBusy?s.pid+1:s.pid},
                {"birth",std::to_string(s.created)},{"reason","live-busy"}};
            if(!externalBusy)facts["localReadBusy"]=true;
            return facts;
        }
        if(read>1){while(current()&&!settle.load())Sleep(1);if(!current())return Json{{"reason","shared-mode-read-cancelled"}};}
        return Json{{"known",true},{"running",true},{"mode","bigpicture"},{"pid",s.pid},{"birth",std::to_string(s.created)},
            {"modeEpoch",read>1&&(variant=="connection-page"||variant=="local-busy-page")?"next:1":"doc:1"}};
    };
    std::atomic<unsigned> admissions{0};Harness* owner=nullptr;
    std::atomic<bool> earlyReadHeld{false},earlyDelivered{false},earlyAccepted{false},earlyStateMatched{false};
    if(early)mode.stableBigPicture=[&](const auto& s){
        if(!(s==session)||!earlyReadHeld||earlyDelivered)return;
        const auto at=owner->runtime.snapshot();
        earlyStateMatched=at.phase=="starting"&&at.reason.empty()&&!at.pid;
        earlyAccepted=owner->runtime.requestLaunchAdmissionRefresh();
        earlyDelivered=true;
    };
    unsigned originalLine=0;
    Harness harness(output,context,false,[&]{
        ++admissions;
        if(!owner||owner->plans==0)return ymcc::deckysidebar::LaunchAdmission{true};
        ymcc::steamui::ModeCacheMiss miss;
        const auto state=shared.snapshot(session,&miss);
        if(scoped&&state.value("mode",std::string{})=="bigpicture"){
            ymcc::deckysidebar::LaunchAdmission result{true};result.bindingScope=prepared;
            result.bindingScope.modeEpoch=state.at("modeEpoch").get<std::string>();return result;
        }
        ymcc::deckysidebar::LaunchAdmission result{false,"steam-ui-state-pending",true,state.value("reason",std::string{})};
        result.modeCacheMiss=miss;result.modeSteamPid=session.pid;result.modeSteamBirth=session.created;
        if(early&&!earlyReadHeld.exchange(true)){
            // Hold the stale result until the real observer delivers its fresh mode.
            settle=true;
            (void)waitFor([&]{return earlyDelivered.load();},1500);
        }
        return result;
    },[&](auto&,unsigned attempt){
        if(attempt!=1)return;
        if(busy){
            need(shared.requestRefresh(session,false),"busy-read-after-plan");
            need(waitFor([&]{return shared.snapshot(session).value("reason",std::string{})=="live-busy";}),"busy-result-not-published");
        }else if(connection){
            originalLine=__LINE__+1;
            need(shared.requestConnectionRefresh(session),"connection-refresh-after-plan");
        }else{
            originalLine=__LINE__+1;
            need(shared.requestRefresh(session),"mode-refresh-after-plan");
            need(shared.requestConnectionRefresh(session),"later-connection-refresh-must-not-overwrite-first-cache-clear");
        }
    },[&]{++preflights;return std::string{};},{},{},{},{},{},{},[&](ymcc::deckysidebar::Config& config){
        if(!scoped)return;
        if(stopDuringPreflight){
            auto changed=std::move(config.changed);
            config.changed=[&,changed=std::move(changed)](const auto& state){if(state.pid)loaderPublished=true;changed(state);};
            config.scopedPreflight=[&](const std::function<bool()>& current){
                if(++preflights!=2)return std::string{};
                secondPreflightEntered=current();
                preflightRevoked=waitFor([&]{return !current();},2000);
                return std::string(preflightRevoked?"preparation-cancelled":"fixture-preflight-revocation-missing");
            };
        }
        auto plan=std::move(config.bindingPlan);
        config.bindingPlan=[&,plan=std::move(plan)](auto kind,auto generation,const auto& current){
            auto result=plan(kind,generation,current);result.launchScope=prepared;return result;
        };
    });owner=&harness;
    const auto before=handles(); // Compare after both owners have stopped.
    need(shared.initialize(std::move(mode)),"final-admission-observer-init");
    shared.observeSteam(session);shared.setDemand(ymcc::steamui::ModeConsumer::Decky,true);
    need(waitFor([&]{return shared.snapshot(session).value("mode",std::string{})=="bigpicture";}),"final-admission-observer-ready");
    ymcc::steamui::ModeCacheMiss cachedMiss;
    need((busy||shared.requestRefresh(session,false))&&shared.snapshot(session,&cachedMiss).value("mode",std::string{})=="bigpicture"&&!cachedMiss.reason,
        "noninvalidating-request-keeps-mode-and-emits-no-miss");
    auto foreign=session;++foreign.created;
    ymcc::steamui::ModeCacheMiss foreignMiss;shared.snapshot(foreign,&foreignMiss);
    need(foreignMiss.reason&&std::string_view(foreignMiss.reason)=="session-mismatch"&&!foreignMiss.sequence&&!foreignMiss.originLine,
        "foreign-session-miss-cannot-borrow-origin");
    need(harness.runtime.setEnabled(true),"deferred-enable");
    if(early){
        harness.waitForCheckpoint([&]{return harness.runtime.snapshot().pid!=0;},"early-final-admission-loader-did-not-start");
        const auto launched=harness.runtime.snapshot();
        harness.runtime.shutdown();const auto after=harness.client->snapshot();shared.shutdown();const auto afterHandles=handles();
        const auto& logs=harness.finalAdmissionLog;
        const bool originalRefusal=logs.size()==1&&logs[0].value("reason",std::string{})=="shared-mode-pending"&&
            logs[0].value("modeCacheCause",std::string{})=="connection-refresh"&&logs[0].value("modeCacheOriginLine",0u)==originalLine;
        const bool holdAttributed=logs.size()==1&&logs[0].value("preparedBindingHold",std::string{})=="held";
        const bool sameOwner=harness.plans==1&&preflights==2&&after.fullyDestroyed&&after.handles==0&&after.pendingIo==0&&before==afterHandles&&context.alive();
        return {{"id","native.typed-rejected-final-admission-releases-watch-before-off"},{"parameter",variant},
            {"passed",earlyReadHeld&&earlyDelivered&&earlyStateMatched&&earlyAccepted&&originalRefusal&&sameOwner&&holdAttributed},
            {"earlyDelivered",earlyDelivered.load()},{"earlyPhaseMatched",earlyStateMatched.load()},{"freshWakeAccepted",earlyAccepted.load()},
            {"originalRefusal",originalRefusal},{"sameOwnerAndClean",sameOwner},{"holdAttributed",holdAttributed},{"launchPid",launched.pid},
            {"bindingPlans",harness.plans.load()},{"preflights",preflights.load()},{"admissionReads",admissions.load()},
            {"beforeHandles",before},{"afterHandles",afterHandles},{"atExit",state(after)},{"modeCacheMissLog",logs}};
    }
    need(waitFor([&]{const auto state=harness.runtime.snapshot();return (state.phase=="waiting-manual-steam"||(scoped&&state.phase=="starting"))&&state.reason=="steam-ui-state-pending";}),"final-admission-did-not-defer");
    const auto waiting=harness.client->snapshot();const auto phase=harness.runtime.snapshot();
    if(scoped)need(harness.plans==1&&phase.pid==0,"final-admission-pending-cannot-launch");
    bool wakeAccepted=false;
    const bool off=variant=="connection-off"||variant=="local-busy-off";
    if(scoped&&!off){
        if(phase.phase=="starting")need(waitFor([&]{return harness.healthyGeneration>=1;}),"held-watch-not-drained");
        settle=true;
        need(waitFor([&]{return shared.snapshot(session).value("mode",std::string{})=="bigpicture";}),"final-admission-shared-mode-did-not-settle");
        wakeAccepted=harness.runtime.requestLaunchAdmissionRefresh();
        if(stopDuringPreflight)need(waitFor([&]{return secondPreflightEntered.load();}),"held-second-preflight-not-entered");
        else harness.waitForCheckpoint([&]{return harness.runtime.snapshot().pid!=0;},"final-admission-loader-did-not-start");
    }
    const auto launched=harness.runtime.snapshot();
    const auto beforeShutdown=GetTickCount64();
    harness.runtime.shutdown();const auto after=harness.client->snapshot();
    const auto shutdownElapsedMs=GetTickCount64()-beforeShutdown;
    ymcc::steamui::ModeCacheMiss freshMiss;
    const auto lastMode=shared.snapshot(session,&freshMiss);
    const bool freshReceiptCleared=!busy||off||(lastMode.value("mode",std::string{})=="bigpicture"&&!freshMiss.reason);
    shared.shutdown();const auto afterHandles=handles();
    const auto& logs=harness.finalAdmissionLog;
    const bool attributed=(malformedBusy||externalBusy)?logs.empty():logs.size()==1&&logs[0].value("bootstrapStage",std::string{})=="mode-cache-miss"&&
        logs[0].value("reason",std::string{})==(localBusy?"live-busy":"shared-mode-pending")&&logs[0].value("modeCacheCause",std::string{})==(localBusy?"native-read-lock":connection?"connection-refresh":"mode-refresh")&&
        logs[0].value("modeCacheOriginLine",0u)==originalLine&&logs[0].value("modeCacheSequence",0u)>0&&
        logs[0].value("steamPid",0u)==session.pid&&logs[0].value("steamBirth",std::string{})==std::to_string(session.created);
    const bool page=variant=="connection-page"||variant=="local-busy-page";
    const bool expectHold=scoped&&!malformedBusy&&!externalBusy;
    const bool holdAttributed=(malformedBusy||externalBusy)?logs.empty():logs.size()==1&&
        logs[0].value("preparedBindingHold",std::string{})==(expectHold?"held":"binding-steam-scope-mismatch");
    auto synthetic=Json{{"scope","final-launch-admission"},{"bootstrapStage","mode-cache-miss"},{"preparedBindingHold","unrecognized-private-detail"}};
    const bool unknownOmitted=!ymcc::deckydiag::record("bootstrap-watch",synthetic).contains("preparedBindingHold");
    synthetic["preparedBindingHold"]="held";synthetic["scope"]="other-stage";
    const bool wrongScopeOmitted=!ymcc::deckydiag::record("bootstrap-watch",synthetic).contains("preparedBindingHold");
    const bool expectedLaunch=stopDuringPreflight?launched.pid==0&&wakeAccepted&&preflightRevoked&&!loaderPublished&&shutdownElapsedMs<=3200:
        off?launched.pid==0:launched.pid!=0&&wakeAccepted;
    const bool result=expectHold?phase.phase=="starting"&&!waiting.fullyDestroyed&&waiting.handles>0&&
        harness.plans==(page?2u:1u)&&expectedLaunch&&preflights==(off?1u:2u):
        busy?phase.phase=="waiting-manual-steam"&&waiting.fullyDestroyed&&waiting.handles==0&&waiting.pendingIo==0&&
            harness.plans==2&&launched.pid!=0&&wakeAccepted&&preflights==2:
        harness.plans==1&&waiting.fullyDestroyed&&waiting.handles==0&&waiting.pendingIo==0;
    return {{"id","native.typed-rejected-final-admission-releases-watch-before-off"},
        {"passed",attributed&&holdAttributed&&unknownOmitted&&wrongScopeOmitted&&freshReceiptCleared&&admissions>=2&&phase.pid==0&&result&&after.fullyDestroyed&&after.handles==0&&after.pendingIo==0&&before==afterHandles&&context.alive()},
        {"parameter",variant},{"phaseBeforeFreshRead",phase.phase},{"launchPid",launched.pid},{"freshWakeAccepted",wakeAccepted},{"preflights",preflights.load()},
        {"secondPreflightEntered",secondPreflightEntered.load()},{"preflightRevoked",preflightRevoked.load()},{"loaderPublished",loaderPublished.load()},{"shutdownElapsedMs",shutdownElapsedMs},
        {"admissionReads",admissions.load()},{"completedBindingPlans",harness.plans.load()},{"loaderPid",phase.pid},{"beforeOff",state(waiting)},{"afterOff",state(after)},
        {"beforeHandles",before},{"afterHandles",afterHandles},{"cefUnaffected",context.alive()},
        {"modeCacheMissLog",logs},{"attributed",attributed},{"holdAttributed",holdAttributed},{"holdUnknownOmitted",unknownOmitted},
        {"holdWrongScopeOmitted",wrongScopeOmitted},{"expectedOriginLine",originalLine},{"freshReceiptCleared",freshReceiptCleared}};
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
inline bool runQueuedStableProbe=false;
inline bool runFreshHandshakeProbe=false;
inline bool runRetainedHandshakeProbe=false;
inline bool runRetainedFirstPeerProbe=false;
inline bool runRetainedVerifiedFaultProbe=false;
inline bool runGlobalMissedEventProbe=false; // Original optional real missed-event wait, unchanged45s parent cap; actual20s observed separately.
// Parameter of the existing admission/ladder case, not a new registration.
inline bool runTerminalReconnectProbe=false;
inline bool runRetainedFirstProofProbe=false;
inline bool runRetainedLocalBusyProbe=false;
inline bool runRetainedRefreshProbe=false;
inline bool runRetainedBusyProbe=false;
inline bool runPresencePublicationProbe=false;
inline Json terminalReconnectRefusal(const std::filesystem::path& output){
    using Action=ymcc::deckysidebar::ConnectionRecoveryWindow::Action;
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    const auto before=handles();Json observations=Json::array();
    for(const std::string variant:{"terminal","pending-to-terminal","retained"}){
        std::atomic<bool> healthy{false},terminal{variant!="pending-to-terminal"};std::atomic<unsigned> calls{0};
        std::vector<int> steps;
        Harness h(output/variant,context,variant=="retained",
            [&]{if(variant=="retained")return ymcc::deckysidebar::LaunchAdmission{true,{},false,{},true};
                return terminal.load()?ymcc::deckysidebar::LaunchAdmission{false,"decky-context-already-loaded",false}:
                    ymcc::deckysidebar::LaunchAdmission{false,"steam-ui-state-pending",true};},
            {},{},{},{},
            [&]{return ymcc::deckysidebar::ConnectionHealth{true,healthy.load(),"authenticated-peer-missing",false,"field-verified-session"};},
            [&](Action action)->Json{steps.push_back(static_cast<int>(action));healthy=true;++calls;return {{"ok",true},{"mutated",true}};});
        const auto opened=GetTickCount64();need(h.runtime.setEnabled(true),"terminal-reconnect-enable");
        bool temporaryPreserved=true;ULONGLONG terminalAt=opened;
        if(variant=="pending-to-terminal"){
            need(waitFor([&]{return h.runtime.snapshot().admissionRetry;},2000),"temporary-admission-not-observed");
            Sleep(80);temporaryPreserved=calls==0&&h.plans==0;
            terminalAt=GetTickCount64();terminal=true;
            need(h.runtime.requestLaunchAdmissionRefresh(),"terminal-refusal-change-wake");
        }
        bool retainedPreserved=true,fast=true;
        if(variant=="retained"){
            need(waitFor([&]{return h.healthyGeneration==1&&h.runtime.snapshot().phase=="frontend-attached";},2000),"retained-rebind-not-observed");
            Sleep(80);retainedPreserved=calls==0&&h.plans==1;
            healthy=true;need(h.runtime.requestConnectionCheck(),"retained-health-check");
        }else fast=waitFor([&]{return calls.load()==1;},2000);
        const auto firstStepMs=GetTickCount64()-terminalAt;
        if(variant!="retained"&&fast&&healthy.load())need(h.runtime.requestConnectionCheck(),"terminal-reconnect-current-health-wake");
        const bool settled=healthy.load()&&waitFor([&]{return h.globalConnected.load()==1;},2000);
        h.runtime.shutdown();const auto stopped=h.client->snapshot();
        const auto plans=h.plans.load();
        const bool off=!h.runtime.requestConnectionCheck()&&!h.runtime.requestTransportRecovery()&&h.plans==plans;
        Json row{{"variant",variant},{"actualRuntime",true},{"firstStepMs",firstStepMs},{"pageCallbacks",calls.load()},
            {"fastAfterTerminalRefusal",fast},{"temporaryAdmissionPreserved",temporaryPreserved},{"retainedBindingPreserved",retainedPreserved},
            {"settled",settled},{"bindingPlans",plans},{"steps",steps},{"recoveryLog",h.globalRecoveryLog},
            {"offCannotRevive",off},{"fullyDestroyed",stopped.fullyDestroyed},{"clientAfterStop",state(stopped)}};
        std::cerr<<"TERMINAL_RECONNECT_OBSERVATION "<<row.dump()<<'\n';
        need(temporaryPreserved&&retainedPreserved,"nonterminal-admission-lost-original-reconnect-window");
        need(fast,"terminal-reconnect-refusal-still-waits-for7s");
        need(settled,"terminal-reconnect-positive-health-not-observed");
        need(stopped.fullyDestroyed&&off&&context.alive(),"terminal-reconnect-resources-or-OFF-not-preserved");
        if(variant=="retained")need(calls==0&&plans==1&&steps.empty(),"retained-binding-was-skipped-or-replayed");
        else need(calls==1&&plans==0&&steps==std::vector<int>{static_cast<int>(Action::rebuild)},"terminal-refusal-did-not-skip-only-impossible-lower-rungs");
        observations.push_back(std::move(row));
    }
    Json reservationBoundaries=Json::array();bool reservationFencePassed=true;
    for(const std::string variant:{"current-intent","exit-fence","new-session"}){
        std::atomic<unsigned> reads{0},reservations{0},steps{0};
        std::atomic<bool> captured{false},release{false},healthy{false},barrierExpired{false};
        Harness h(output/("reserve-"+variant),context,false,
            []{return ymcc::deckysidebar::LaunchAdmission{false,"decky-context-already-loaded",false};},{},{},{},{},
            [&]{
                const auto recovered=healthy.load();
                if(++reads==2){
                    captured=true;
                    if(!waitFor([&]{return release.load();},4000))barrierExpired=true;
                }
                ymcc::deckysidebar::ConnectionHealth health{true,recovered,"official-ws-dead",false,"reservation-boundary"};
                health.transportConnected=true;return health;
            },[&](Action)->Json{healthy=true;++steps;return {{"ok",true},{"mutated",true}};},{},{},
            [&](auto& config){config.reserveConnectionStep=[&](Action){
                ++reservations;
                return std::make_shared<ymcc::steamcommand::Reservation>(ymcc::steamcommand::Ticket{},false);
            };});
        need(h.runtime.setEnabled(true),"reservation-boundary-enable");
        need(waitFor([&]{return captured.load();},2000),"reservation-boundary-missing-stale-read");
        if(variant=="exit-fence")h.runtime.requestExitFence();
        if(variant=="new-session"){healthy=true;h.runtime.observeSteam(1,true);}
        release=true;
        if(variant=="current-intent")need(waitFor([&]{return steps.load()==1;},2000),"current-intent-rebuild-no-longer-admitted");
        if(variant=="new-session")need(waitFor([&]{return reads.load()>=3;},2000),"current-session-health-not-observed");
        h.runtime.shutdown();
        const auto expected=variant=="current-intent"?1u:0u;
        bool capturedOutcome=!barrierExpired&&reservations==expected&&steps==expected;
        reservationFencePassed=reservationFencePassed&&capturedOutcome;
        need(h.plans==0&&h.client->snapshot().fullyDestroyed&&context.alive(),"reservation-boundary-resources-or-extra-bind");
        reservationBoundaries.push_back({{"variant",variant},{"readCalls",reads.load()},{"reserveCalls",reservations.load()},
            {"pageCallbacks",steps.load()},{"barrierExpired",barrierExpired.load()},{"passed",capturedOutcome}});
    }
    need(before==handles(),"terminal-reconnect-handle-leak");
    return {{"id","native.typed-admission-park-resume-without-budget-refill"},{"passed",reservationFencePassed},
        {"reservationBoundaries",reservationBoundaries},
        {"parameter","terminal-reconnect"},{"observations",observations},{"beforeHandles",before},{"afterHandles",handles()},
        {"cefUnaffected",context.alive()},{"fieldRootCauseProven",false},{"realSteamOrDeviceActions",0}};
}

inline Json admissionParkResume(const std::filesystem::path& output){
    if(runTerminalReconnectProbe)return terminalReconnectRefusal(output);
    using Fallback=ymcc::deckysidebar::ConnectionRecoveryWindow;using Action=Fallback::Action;
    using FContext=ymcc::steamfailure::FailureContext;using FOperation=ymcc::steamfailure::Operation;
    FContext traceContext;traceContext.actionId=1;traceContext.operation=FOperation::RebuildDocument;
    const Json poison={{"reason","https://secret/token"},{"runtimeReason",std::string(4096,'x')},{"url","private"},{"stack","private"},
        {"steamFailure",{{"schema",1},{"domain","javascript"},{"sourceStage","evaluate"},{"exceptionClassKnown",true},{"exceptionClass","TypeError"},
            {"codeKnown",false},{"code",999},{"signatures",Json::array({"undefined-property","arbitrary-private-text"})},{"message","private"},{"url","private"}}}};
    const auto safe=ymcc::deckydiag::failureRecord(poison,traceContext);
    need(safe.dump().size()<2304&&!safe.contains("reason")&&!safe.contains("runtimeReason")&&!safe.contains("url")&&!safe.contains("stack"),"trace-admitted-private-or-unbounded-control-text");
    need(safe["steamFailure"].value("exceptionClass",std::string{})=="TypeError"&&!safe["steamFailure"].contains("message")&&!safe["steamFailure"].contains("code")&&safe["steamFailure"]["signatures"]==Json::array({"undefined-property"}),"common-steam-failure-normalization-did-not-survive-native-logger");
    using FPhase=ymcc::steamfailure::Phase;
    auto refusalContext=traceContext;refusalContext.phase=FPhase::Finished;
    const Json refusal={{"ok",false},{"mutated",false},{"reason","decky-cleanup-account-unconfirmed"},{"observedAccount",42u},{"root","private"}};
    for(const auto operation:{FOperation::RebuildDocument,FOperation::ReopenBigPicture}){
        refusalContext.operation=operation;
        for(const auto* stage:{"parent-account-source","shared-process-receipt","shared-process-delegation","child-mode-read","child-account-mismatch","child-account-proof"}){
            auto result=refusal;result["sessionCheck"]=stage;
            const auto row=ymcc::deckydiag::failureRecord(result,refusalContext);
            need(row.value("cleanupAccountStage",std::string{})==stage&&!row.contains("observedAccount")&&!row.contains("root")&&!row.contains("sessionCheck"),"cleanup-terminal-stage-missing-or-private");
        }
    }
    auto denied=refusal;denied["sessionCheck"]="parent-account-source";
    const auto absent=[&](const auto& result,const auto& context){return !ymcc::deckydiag::failureRecord(result,context).contains("cleanupAccountStage");};
    for(const auto& stage:std::vector<Json>{"private/account",42u,Json{{"stage","child-mode-read"}}}){
        auto result=denied;result["sessionCheck"]=stage;need(absent(result,refusalContext),"cleanup-stage-logged-untyped-data");
    }
    for(const auto* key:{"ok","mutated","uncertain"}){
        auto result=denied;result[key]=true;need(absent(result,refusalContext),"cleanup-stage-logged-nonrefusal");
    }
    auto wrong=denied;wrong["reason"]="unrelated";need(absent(wrong,refusalContext),"cleanup-stage-logged-unrelated-reason");
    refusalContext.phase=FPhase::Started;need(absent(denied,refusalContext),"cleanup-stage-logged-nonterminal-phase");
    refusalContext.phase=FPhase::Finished;refusalContext.operation=FOperation::Binding;need(absent(denied,refusalContext),"cleanup-stage-logged-other-operation");
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
                return {{"ok",false},{"mutated",false},{"reason","decky-cleanup-account-unconfirmed"},
                    {"sessionCheck",step==Action::rebuild?"shared-process-delegation":"child-account-proof"}};
            });
        if(scenario=="cancelled-stops"){ladder.throwFailureTrace=true;ladder.throwFailureContext=true;}
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
        std::map<std::string,Json> startedTrace;unsigned pageFinishes=0,selectedTrace=0;
        for(const auto& row:ladder.failureTraceLog){
            need(row.value("event",std::string{})=="steam-recovery-trace"&&row.dump().size()<2304,"trace-record-not-bounded");
            const auto tracePhase=row.value("actionPhase",std::string{}),operation=row.value("actionOperation",std::string{}),id=row.value("actionId",std::string{});
            if(tracePhase=="selected"||tracePhase=="rejected"||tracePhase=="observation"){need(id=="0","selected-observation-fabricated-executed-action");++selectedTrace;continue;}
            if(tracePhase=="started"){need(id!="0"&&!startedTrace.contains(id),"action-id-was-reused");startedTrace.emplace(id,row);}
            if(tracePhase=="finished"){
                need(startedTrace.contains(id),"finished-action-missing-started-receipt");
                const auto& first=startedTrace.at(id);
                for(const auto* key:{"actionOperation","generation","nativePowerGeneration","steamPid","steamBirth","recoveryEpisode"})need(first.at(key)==row.at(key),"finished-action-retagged-with-late-identity");
                if(operation=="rebuild-ui-document"||operation=="reopen-bigpicture"){
                    ++pageFinishes;
                    if(scenario=="failed-fast")need(row.value("cleanupAccountStage",std::string{})==
                        (operation=="rebuild-ui-document"?"shared-process-delegation":"child-account-proof"),"cleanup-stage-lost-before-terminal-log");
                    else need(!row.contains("cleanupAccountStage"),"cleanup-stage-on-nonrefusal");
                }
            }
        }
        need(pageFinishes==calls.load()&&selectedTrace>0,"actual-recovery-callback-not-correlated-to-one-start-and-finish");
        if(scenario=="cancelled-stops")need(ladder.failureTraceLog.front().at("steamPid")==0,"failed-diagnostic-context-fabricated-process-proof");
        stageScenarios.push_back({{"scenario",scenario},{"callbackCount",calls.load()},{"elapsedMs",elapsed},{"actualRuntime",true},{"realSteamActions",0},
            {"failureTrace",ladder.failureTraceLog},{"traceCallbacksThrow",scenario=="cancelled-stops"}});
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
inline Json deferredPreflightSession(const std::filesystem::path& output){
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    std::atomic<unsigned> attempts{0},preflights{0};
    std::atomic<bool> secondEntered{false},revoked{false},newSession{false},staleLoader{false};
    Harness h(output,context,false,{},{},{},{},{},{},{},{},[&](const auto& state){if(state.pid&&!newSession)staleLoader=true;},[&](ymcc::deckysidebar::Config& config){
        auto plan=std::move(config.bindingPlan);
        config.bindingPlan=[&,plan=std::move(plan)](auto mode,auto generation,const auto& current){
            if(++attempts==1)return ymcc::deckysidebar::BindingPlan{{},"live-debug-unavailable"};
            return plan(mode,generation,current);
        };
        config.scopedPreflight=[&](const std::function<bool()>& current){
            if(++preflights!=2)return std::string{};
            secondEntered=current();revoked=waitFor([&]{return !current();},2000);
            return std::string(revoked?"preparation-cancelled":"fixture-preflight-revocation-missing");
        };
    });
    const auto beforeHandles=handles();need(h.runtime.setEnabled(true),"deferred-preflight-enable");
    need(waitFor([&]{return secondEntered.load();}),"deferred-preflight-did-not-retry");
    const auto bound=h.client->snapshot();const auto revokedAt=GetTickCount64();
    const bool noOldLoader=h.runtime.snapshot().pid==0&&attempts==2&&h.plans==1;
    h.runtime.observeSteam(0,true);
    const bool retired=waitFor([&]{const auto s=h.runtime.snapshot();return s.phase=="waiting-steam"&&s.pid==0;},3200);
    const auto retiredAt=GetTickCount64();const auto returned=h.client->snapshot();
    newSession=true;h.runtime.observeSteam(1,true);
    const bool currentStarted=waitFor([&]{return h.runtime.snapshot().pid!=0;});
    const auto current=h.runtime.snapshot();h.runtime.shutdown();const auto done=h.client->snapshot();
    const auto afterHandles=handles();
    const bool passed=noOldLoader&&bound.handles>0&&bound.jobActiveProcesses>0&&revoked&&retired&&retiredAt-revokedAt<=3200&&returned.fullyDestroyed&&returned.childExited&&
        returned.handles==0&&returned.pendingIo==0&&returned.jobActiveProcesses==0&&!staleLoader&&currentStarted&&preflights==3&&attempts==3&&h.plans==2&&done.fullyDestroyed&&afterHandles==beforeHandles&&context.alive();
    return {{"id","native.typed-preparation-rechecks-admission-before-binding"},{"parameter","deferred-session"},{"passed",passed},
        {"secondPreflightEntered",secondEntered.load()},{"preflightRevoked",revoked.load()},{"oldLoaderPublished",staleLoader.load()},{"newLoaderPid",current.pid},
        {"preflights",preflights.load()},{"planAttempts",attempts.load()},{"bindingPlans",h.plans.load()},{"retired",retired},{"retiredElapsedMs",retiredAt-revokedAt},
        {"beforeSessionLeave",state(bound)},{"afterSessionLeave",state(returned)},{"afterOff",state(done)},{"beforeHandles",beforeHandles},{"afterHandles",afterHandles}};
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
inline Json queuedRecoveryHealth(const std::filesystem::path& output,bool previouslyConnected=false){
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    Json scenarios=Json::array();
    enum class Return { ui,peer,flap,peerSilently };
    for(const auto returned:{Return::ui,Return::peer,Return::flap,Return::peerSilently})for(const bool watch:{false,true}){
        const bool peerOnly=returned!=Return::ui,flapped=returned==Return::flap,silent=returned==Return::peerSilently;
        if(peerOnly&&!previouslyConnected)continue;
        if(watch&&(flapped||silent))continue;
        std::atomic<unsigned> phase{0};std::atomic<bool> missingCycleComplete{false},positiveCycleComplete{false},peerRead{false},secondLossRead{false};
        const auto health=[&]{const auto value=phase.load();if(value==2)positiveCycleComplete=true;if(value==3)peerRead=true;
            if(value==4)secondLossRead=true;
            ymcc::deckysidebar::ConnectionHealth result{value!=0,value==2,"queued-mirror-observation",false,"queued-health-fixed-session"};
            result.transportConnected=value==2||value==3;return result;
        };
        const auto scope=flapped?output/"flap":silent?output/"peer-silent":peerOnly?output/"peer-only":output;
        Harness h(scope/(watch?"watch-pending":"automatic-only"),context,false,{},{},{},[&]{if(phase==1)missingCycleComplete=true;},{},health);
        const auto before=handles();need(h.runtime.setEnabled(true),"queued-health-enable");
        need(waitFor([&]{return h.healthyGeneration==1;}),"queued-health-first-binding");
        const auto initial=h.runtime.snapshot();const auto originalBinding=h.client->snapshot().processId;
        if(previouslyConnected){
            phase=2;need(h.runtime.requestConnectionCheck(),"queued-health-prior-positive-check");
            need(waitFor([&]{return positiveCycleComplete.load();},1000),"queued-health-prior-positive-not-consumed");
        }
        phase=1;const auto missingAt=GetTickCount64();need(h.runtime.requestConnectionCheck(),"queued-health-first-check");
        need(waitFor([&]{return missingCycleComplete.load();},1000),"queued-health-missing-cycle-not-consumed");
        if(watch)need(h.runtime.requestTransportRecovery(),"queued-health-independent-watch-refused");
        phase=peerOnly?3:2;
        if(!silent){
            need(h.runtime.requestConnectionCheck(),"queued-health-returned-check");
            need(waitFor([&]{return peerOnly?peerRead.load():h.globalConnected.load()>0;},1000),"queued-health-return-not-consumed");
        }
        const auto returnedMs=GetTickCount64()-missingAt;need(returnedMs<200,"queued-health-restored-after-original-retry-window");
        if(flapped){
            phase=4;need(h.runtime.requestConnectionCheck(),"queued-peer-second-loss-check");
            need(waitFor([&]{return secondLossRead.load();},1000)&&GetTickCount64()-missingAt<200,
                "queued-peer-second-loss-not-consumed-before-original-retry");
        }
        Sleep(450); // Cross the actual existing 250ms retry, without advancing its clock.
        const auto plans=h.plans.load();const auto binding=h.client->snapshot().processId;const auto measured=h.runtime.snapshot();
        const bool waitedForUiProof=peerOnly&&h.globalConnected.load()==0;
        const bool peerReadBeforeUiProof=peerRead.load();
        if(peerOnly){
            need(waitedForUiProof,"queued-peer-mistaken-for-complete-ui-proof");
            phase=2;need(h.runtime.requestConnectionCheck(),"queued-peer-ui-proof-check");
            need(waitFor([&]{return h.globalConnected.load()>0;},1000),"queued-peer-ui-proof-not-consumed");
        }
        h.runtime.shutdown();const auto destroyed=h.client->snapshot();
        const bool shouldReplace=watch||flapped;
        need(shouldReplace?plans==2&&binding!=originalBinding:plans==1&&binding==originalBinding,
             flapped?"queued-peer-flap-lost-original-retry":watch?"queued-health-lost-independent-watch-repair":
             peerOnly?"queued-automatic-repair-ran-after-peer-return":"queued-automatic-repair-ran-after-connected");
        if(silent)need(peerReadBeforeUiProof,"queued-silent-peer-not-confirmed-before-retry");
        need(measured.pid==initial.pid&&destroyed.fullyDestroyed&&before==handles()&&context.alive(),"queued-health-resource-return-or-loader-identity");
        scenarios.push_back({{"independentWatchFault",watch},{"peerOnly",peerOnly},{"peerFlapped",flapped},{"peerReturnWithoutHint",silent},
            {"peerReadBeforeUiProof",peerReadBeforeUiProof},{"waitedForUiProof",waitedForUiProof},{"bindingPlans",plans},{"returnedBeforeRetryMs",returnedMs},
            {"sameLoader",measured.pid==initial.pid},{"bindingReplaced",binding!=originalBinding},{"beforeHandles",before},{"afterHandles",handles()},{"atExit",state(destroyed)}});
    }
    return {{"passed",true},{"scenarios",scenarios},{"previouslyConnected",previouslyConnected},{"actualRuntime",true},{"realSteamActions",0}};
}
// Parameter of the existing ready/rebind case; actual Runtime, Client, Owner,
// events and source-built inert Loader/CEF. No virtual grace clock.
inline Json freshBindingHandshake(const std::filesystem::path& output){
    using Window=ymcc::deckysidebar::ConnectionRecoveryWindow;
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    Json rows=Json::array();
    for(const auto variant:{"fresh","watch","deadline","off"}){
        std::atomic<unsigned> phase{0},positiveReads{0};std::atomic<bool> missingCycle{false};
        const auto health=[&]{const auto value=phase.load();if(value==2)++positiveReads;
            ymcc::deckysidebar::ConnectionHealth result{value!=0,value==2,"fresh-binding-authenticated-peer-pending",false,"fresh-binding-fixed-session"};result.transportConnected=value==2;return result;};
        Harness h(output/variant,context,false,{},{},{},[&]{if(phase==1)missingCycle=true;},{},health);
        const auto before=handles();need(h.runtime.setEnabled(true),"fresh-handshake-enable");
        h.waitForCheckpoint([&]{return h.healthyGeneration==1;},"fresh-handshake-initial-watch");
        const auto initial=h.runtime.snapshot();const auto originalLoader=initial.pid;
        detail::Handle loader{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,originalLoader)};need(loader.value!=nullptr,"fresh-handshake-loader-lease");
        phase=2;need(h.runtime.requestConnectionCheck(),"fresh-handshake-prior-health-check");need(waitFor([&]{return positiveReads>0;},1000),"fresh-handshake-prior-health-not-consumed");
        phase=0;need(h.runtime.requestBindingRefresh(9),"fresh-handshake-wake-refresh");
        h.waitForCheckpoint([&]{return h.healthyGeneration==2;},"fresh-handshake-wake-watch");
        const auto freshPid=h.client->snapshot().processId;const auto began=GetTickCount64();phase=1;need(h.runtime.requestConnectionCheck(),"fresh-handshake-missing-check");
        need(waitFor([&]{return missingCycle.load();},1000),"fresh-handshake-health-cycle-not-consumed");
        bool deadlineEscalated=false,offAccepted=true,connectedReturned=true;
        if(std::string_view(variant)=="watch"){
            need(h.runtime.requestTransportRecovery(),"fresh-handshake-independent-watch-request");
            h.waitForCheckpoint([&]{return h.healthyGeneration==3;},"fresh-handshake-independent-watch-not-replaced");
        }else if(std::string_view(variant)=="deadline"){
            deadlineEscalated=waitFor([&]{return h.runtime.snapshot().pid!=originalLoader;},Window::transportGraceMs+2000);
            deadlineEscalated=deadlineEscalated&&WaitForSingleObject(loader.value,1000)==WAIT_OBJECT_0;
        }else if(std::string_view(variant)=="off"){
            offAccepted=h.runtime.setEnabled(false);Sleep(100);
            offAccepted=offAccepted&&h.runtime.snapshot().phase=="disabled"&&!h.runtime.requestConnectionCheck();
        }else Sleep(450); // Cross the ORIGINAL250ms generic rebind retry.
        const auto elapsed=GetTickCount64()-began;const auto plans=h.plans.load();const auto binding=h.client->snapshot().processId;const auto observed=h.runtime.snapshot();
        if(std::string_view(variant)!="off"&&std::string_view(variant)!="deadline"){
            phase=2;need(h.runtime.requestConnectionCheck(),"fresh-handshake-returned-health-check");connectedReturned=waitFor([&]{return h.globalConnected>0;},1000);
        }
        h.runtime.shutdown();const auto destroyed=h.client->snapshot();CloseHandle(loader.value);loader.value=nullptr;
        bool reuseFact=false;
        for(const auto& record:h.globalRecoveryLog)if(record.value("stage",std::string{})=="reconnect"){
            const auto filtered=ymcc::deckydiag::record("connection-recovery",record);
            if(record.value("bindingReused",false)&&filtered.value("bindingReused",false))reuseFact=true;
        }
        need(reuseFact,"fresh-handshake-log-confirms-actual-existing-binding-reuse");
        for(const auto& poison:std::vector<Json>{"true",1,-1,nullptr,Json::object()}){
            const auto filtered=ymcc::deckydiag::record("connection-recovery",{{"scope","global-connection"},{"bindingReused",poison}});
            need(!filtered.contains("bindingReused"),"binding-reuse-log-requires-an-actual-boolean");
        }
        need(!ymcc::deckydiag::record("bootstrap-watch",{{"scope","global-connection"},{"bindingReused",true}}).contains("bindingReused")&&
             !ymcc::deckydiag::record("connection-recovery",{{"scope","other"},{"bindingReused",true}}).contains("bindingReused"),"binding-reuse-fact-stays-in-original-event-scope");
        const bool base=destroyed.fullyDestroyed&&before==handles()&&context.alive();
        const bool behavior=std::string_view(variant)=="fresh"?plans==2&&binding==freshPid&&observed.pid==originalLoader&&connectedReturned:
            std::string_view(variant)=="watch"?plans==3&&binding!=freshPid&&observed.pid==originalLoader&&connectedReturned:
            std::string_view(variant)=="deadline"?deadlineEscalated&&elapsed<=Window::transportGraceMs+2000:offAccepted&&plans==2;
        rows.push_back({{"variant",variant},{"passed",base&&behavior},{"bindingPlans",plans},{"freshBindingPid",freshPid},{"observedBindingPid",binding},{"sameLoader",observed.pid==originalLoader},
            {"elapsedMs",elapsed},{"originalTransportGraceMs",Window::transportGraceMs},{"deadlineEscalated",deadlineEscalated},{"offAccepted",offAccepted},{"returnedHealthy",connectedReturned},
            {"beforeHandles",before},{"afterHandles",handles()},{"atExit",state(destroyed)},{"globalRecoveryLog",h.globalRecoveryLog},{"originalContextAlive",context.alive()}});
        if(!base||!behavior)break;
    }
    const bool passed=rows.size()==4&&std::all_of(rows.begin(),rows.end(),[](const auto& row){return row.value("passed",false);});
    return {{"id","native.typed-ready-rebind"},{"parameter","fresh-handshake"},{"passed",passed},{"actualRuntime",true},{"actualClock",true},{"scenarios",rows},{"realSteamOrDeviceActions",0}};
}
// Same ready/rebind case, retained frontend parameter: actual worker, client,
// owner, clock and inert context. Keep the original owned parameter unchanged.
inline Json watchReattachHandshake(const std::filesystem::path& output){
    using Window=ymcc::deckysidebar::ConnectionRecoveryWindow;
    Json rows=Json::array();
    for(const std::string variant:{"fresh","watch","deadline","off","stale","foreign"}){
        Controls controls(L"runtime-success-checkpoint");Context cef;ContextJob owner(worker::number(cef.lease.at("pid")));
        std::atomic<unsigned> phase{0},positiveReads{0};std::atomic<bool> seenMissing{false},attachmentObserved{false};
        Harness h(output/variant,cef,false,{},[&](Json& request,unsigned attempt){
            if(attempt==1)request["bootstrap"]["fixtureWatchReattach"]=variant;
        },{},[&]{if(phase==2)seenMissing=true;},{},[&]{
            const auto p=phase.load();if(p==1)++positiveReads;
            ymcc::deckysidebar::ConnectionHealth health{p!=0,p==1,"watch-reattach",false,"same-watch-session"};
            health.transportConnected=p==1;return health;
        },{},{},{},[&](auto& config){
            auto consume=std::move(config.bindingDiagnostic);
            config.bindingDiagnostic=[&,consume=std::move(consume)](const Json& packet){
                // Remote close precedes the watch receipt in the field. Publish
                // both in one owner turn, not across a stale healthy sample.
                if(packet.value("bootstrapStage",std::string{})=="context-attached"){
                    phase=2;attachmentObserved=true;
                }
                consume(packet);
            };
        });
        const auto before=handles();need(h.runtime.setEnabled(true),"watch-reattach-enable");
        h.waitForCheckpoint([&]{return h.healthyGeneration==1;},"watch-reattach-started");
        const auto loader=h.runtime.snapshot().pid,binding=h.client->snapshot().processId;
        need(loader&&binding,"watch-reattach-initial-owners");
        phase=1;need(h.runtime.requestConnectionCheck(),"watch-reattach-healthy");
        need(waitFor([&]{return positiveReads>0;},1000),"watch-reattach-positive-read");
        const auto missingAt=GetTickCount64();controls.releaseNow();
        need(waitFor([&]{return attachmentObserved.load();},1000),"watch-reattach-delivered");
        need(h.runtime.requestConnectionCheck(),"watch-reattach-missing");
        need(waitFor([&]{return seenMissing.load();},1000),"watch-reattach-consumed");
        Sleep(500); // Cross the existing queued reconnect tick.
        const auto initialPlans=h.plans.load();
        const bool rejected=variant=="stale"||variant=="foreign";
        bool ownBehavior=false;
        if(variant=="watch"){
            need(h.runtime.requestTransportRecovery(),"watch-reattach-independent-repair");
            ownBehavior=waitFor([&]{return h.healthyGeneration>=2;},2000);
        }else if(variant=="deadline"){
            ownBehavior=waitFor([&]{return h.runtime.snapshot().pid!=loader;},Window::transportGraceMs+1000);
        }else if(variant=="off"){
            ownBehavior=h.runtime.setEnabled(false)&&h.runtime.snapshot().pid==0;
        }else{
            phase=1;need(h.runtime.requestConnectionCheck(),"watch-reattach-confirm-return");
            ownBehavior=waitFor([&]{return h.globalConnected>0;},1000);
        }
        const auto elapsed=GetTickCount64()-missingAt;
        const auto observed=h.runtime.snapshot();const auto finalPlans=h.plans.load();
        h.runtime.shutdown();const auto returned=h.client->snapshot();
        bool reused=false;for(const auto& record:h.globalRecoveryLog)
            if(record.value("stage",std::string{})=="reconnect")reused=reused||record.value("bindingReused",false);
        const bool passed=ownBehavior&&reused!=rejected&&(rejected?initialPlans==2:initialPlans==1)&&
            (variant!="fresh"||finalPlans==1&&observed.pid==loader)&&
            (variant!="watch"||finalPlans==2&&observed.pid==loader)&&
            (variant!="off"||finalPlans==1&&!observed.pid)&&returned.fullyDestroyed&&handles()==before&&cef.alive();
        rows.push_back({{"variant",variant},{"passed",passed},{"bindingReused",reused},{"initialPlans",initialPlans},
            {"finalPlans",finalPlans},{"actionObserved",ownBehavior},{"initialLoader",loader},{"observedLoader",observed.pid},
            {"elapsedMs",elapsed},{"beforeHandles",before},{"afterHandles",handles()},{"afterOff",state(returned)}});
    }
    const auto passed=std::all_of(rows.begin(),rows.end(),[](const auto& row){return row.value("passed",false);});
    return {{"id","native.typed-ready-rebind"},{"parameter","watch-reattach"},{"passed",passed},{"scenarios",rows},
        {"actualRuntime",true},{"watchReceiptFixture",true},{"realSteamOrDeviceActions",0}};
}
inline Json retainedBindingHandshake(const std::filesystem::path& output){

    using Window=ymcc::deckysidebar::ConnectionRecoveryWindow;
    using Action=Window::Action;
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    Json rows=Json::array();
    const std::vector<const char*> variants=runRetainedVerifiedFaultProbe?std::vector<const char*>{"verified-fault"}:
        std::vector<const char*>{"fresh","watch","deadline","off"};
    for(const auto variant:variants){
        std::atomic<unsigned> phase{0};std::atomic<bool> missingCycle{false},advanced{false};
        const auto health=[&]{const auto value=phase.load();
            ymcc::deckysidebar::ConnectionHealth result{value!=0,value==2,"retained-authenticated-peer-pending",false,"retained-fixed-session"};result.transportConnected=value==2||(runRetainedFirstPeerProbe&&value==1);return result;};
        const auto step=[&](Action action){if(action==Action::rebuild)advanced=true;return Json{{"ok",false},{"mutated",false},{"uncertain",false}};};
        Harness h(output/variant,context,true,{},{},{},[&]{if(phase==1)missingCycle=true;},{},health,step);
        const auto before=handles();need(h.runtime.setEnabled(true),"retained-handshake-enable");
        h.waitForCheckpoint([&]{return h.healthyGeneration==1&&h.runtime.snapshot().phase=="frontend-attached";},"retained-handshake-initial-watch");
        const auto freshPid=h.client->snapshot().processId;const auto began=GetTickCount64();phase=1;
        need(h.runtime.requestConnectionCheck(),"retained-handshake-missing-check");
        need(waitFor([&]{return missingCycle.load();},1000),"retained-handshake-health-not-consumed");
        bool deadlineEscalated=false,offAccepted=true,connectedReturned=true;
        if(std::string_view(variant)=="verified-fault"){
            phase=2;need(h.runtime.requestConnectionCheck(),"retained-first-complete-proof-check");
            need(waitFor([&]{return h.globalConnected.load()==1;},1000),"retained-first-complete-proof-observed");
            phase=1;missingCycle=false;need(h.runtime.requestConnectionCheck(),"retained-new-real-fault-check");
            need(waitFor([&]{return missingCycle.load();},1000),"retained-new-real-fault-consumed");
            deadlineEscalated=waitFor([&]{return advanced.load();},1500);
        }else if(std::string_view(variant)=="watch"){
            need(h.runtime.requestTransportRecovery(),"retained-independent-watch-request");
            h.waitForCheckpoint([&]{return h.healthyGeneration==2;},"retained-independent-watch-not-replaced");
        }else if(std::string_view(variant)=="deadline"){
            deadlineEscalated=waitFor([&]{return advanced.load();},Window::transportGraceMs+2000);
        }else if(std::string_view(variant)=="off"){
            offAccepted=h.runtime.setEnabled(false);Sleep(100);
            offAccepted=offAccepted&&h.runtime.snapshot().phase=="disabled"&&!h.runtime.requestConnectionCheck();
        }else Sleep(450); // Cross the original250ms retry, as in owned parameter.
        const auto elapsed=GetTickCount64()-began;const auto plans=h.plans.load();const auto binding=h.client->snapshot().processId;const auto observed=h.runtime.snapshot();
        if(std::string_view(variant)!="off"&&std::string_view(variant)!="deadline"&&std::string_view(variant)!="verified-fault"){
            phase=2;need(h.runtime.requestConnectionCheck(),"retained-returned-health-check");connectedReturned=waitFor([&]{return h.globalConnected>0;},1000);
        }
        h.runtime.shutdown();const auto destroyed=h.client->snapshot();
        const bool base=destroyed.fullyDestroyed&&before==handles()&&context.alive()&&observed.pid==0&&h.loaderStopLog.empty();
        const bool behavior=std::string_view(variant)=="verified-fault"?deadlineEscalated&&elapsed<2000&&plans>1:
            std::string_view(variant)=="fresh"?plans==1&&binding==freshPid&&connectedReturned:
            std::string_view(variant)=="watch"?plans==2&&binding!=freshPid&&connectedReturned:
            std::string_view(variant)=="deadline"?deadlineEscalated&&elapsed>=Window::transportGraceMs-1000&&elapsed<=Window::transportGraceMs+2000:offAccepted&&plans==1;
        rows.push_back({{"variant",variant},{"passed",base&&behavior},{"bindingPlans",plans},{"freshBindingPid",freshPid},{"observedBindingPid",binding},{"loaderPid",observed.pid},
            {"elapsedMs",elapsed},{"originalTransportGraceMs",Window::transportGraceMs},{"deadlineEscalated",deadlineEscalated},{"offAccepted",offAccepted},{"returnedHealthy",connectedReturned},
            {"beforeHandles",before},{"afterHandles",handles()},{"atExit",state(destroyed)},{"globalRecoveryLog",h.globalRecoveryLog},{"originalContextAlive",context.alive()}});
        if(!base||!behavior)break;
    }
    const bool passed=rows.size()==(runRetainedVerifiedFaultProbe?1u:4u)&&std::all_of(rows.begin(),rows.end(),[](const auto& row){return row.value("passed",false);});
    return {{"id","native.typed-ready-rebind"},{"parameter",runRetainedVerifiedFaultProbe?"retained-verified-fault":runRetainedFirstPeerProbe?"retained-first-peer":"retained-handshake"},{"passed",passed},{"actualRuntime",true},{"actualClock",true},{"scenarios",rows},{"realSteamOrDeviceActions",0}};
}
// Keep the original verified-fault case; exercise the reserved production port
// on the same retained owner with no actual Steam command.
inline Json retainedPageGrant(const std::filesystem::path& output,std::string_view selection={}){
    using Window=ymcc::deckysidebar::ConnectionRecoveryWindow;
    using Action=Window::Action;
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    Json rows=Json::array();
    for(const std::string scenario:{"refused","granted","uncertain","await-identity","await-without-proof","late-healthy","late-unhealthy"}){
        const bool pending=scenario=="await-identity",missingProof=scenario=="await-without-proof";
        const bool lateHealthy=scenario=="late-healthy",late=lateHealthy||scenario=="late-unhealthy";
        if((selection=="late-health")!=late||(selection=="await-identity"&&!pending))continue;
        std::atomic<unsigned> healthPhase{0},steps{0},heldAtStep{0},releasedAtGate{0},gateCalls{0};
        std::atomic<unsigned> staleReads{0};std::atomic<ULONGLONG> lateReceiptAt{0};
        std::atomic<Action> nextPage{Action::none};std::atomic<ULONGLONG> pendingAt{0},resolvedAt{0};
        std::atomic<bool> sawFirstHealth{false};
        Harness* target=nullptr;DWORD originalPid=0;
        Harness h(output/scenario,context,true,{},[&](Json& request,unsigned){
            if(late)request["bootstrap"]["fixtureLatePageWindow"]=true;
        },{},{},{},[&]{
            const auto phase=healthPhase.load();
            if(phase==1)sawFirstHealth=true;
            if(lateReceiptAt&&phase==3)++staleReads;
            ymcc::deckysidebar::ConnectionHealth value{phase!=0,phase==2,"fixture-official-backend-unavailable",false,"fixture-retained-page"};
            value.transportConnected=phase!=0;return value;
        },{},{},{},[&](auto& config){
            // Assignable to both sides of the original private port revision.
            config.connectionStepReserved=[&](Action action,const auto&,const auto&... beforeEffect){
                if(target->client->snapshot().processId==originalPid)++heldAtStep;
                if((pending||missingProof)&&steps==0){
                    pendingAt=GetTickCount64();++steps;
                    Json reply={{"ok",false},{"mutated",false},{"uncertain",false},
                        {"reason","decky-cleanup-account-unconfirmed"},{"awaitCurrentIdentity",true}};
                    if(pending)reply["cleanupNotStarted"]=true;
                    return reply;
                }
                if(pending||missingProof){nextPage=action;resolvedAt=GetTickCount64();}
                const bool grant=scenario=="granted"||(pending&&action==Action::rebuild);
                if(grant){
                    ([&]{if(beforeEffect){++gateCalls;if(beforeEffect()&&target->client->snapshot().processId==0)++releasedAtGate;}}(),...);
                }
                if(late&&action==Action::reopen){
                    lateReceiptAt=GetTickCount64();++steps;
                    return Json{{"ok",false},{"mutated",false},{"uncertain",false},{"reason","decky-bigpicture-already-healthy"}};
                }
                if(!late&&(scenario!="refused"||action==Action::reopen))healthPhase=2;
                ++steps;
                if(grant)return Json{{"ok",true},{"mutated",true}};
                if(scenario=="uncertain")return Json{{"ok",false},{"mutated",true},{"uncertain",true}};
                return Json{{"ok",false},{"mutated",false},{"uncertain",false},{"reason","decky-cleanup-account-unconfirmed"}};
            };
        });
        target=&h;const auto before=handles();
        need(h.runtime.setEnabled(true),"reserved-page-enable");
        h.waitForCheckpoint([&]{return h.healthyGeneration==1&&h.runtime.snapshot().phase=="frontend-attached";},"reserved-page-attached");
        originalPid=h.client->snapshot().processId;need(originalPid!=0,"reserved-page-original-child");
        healthPhase=1;need(h.runtime.requestConnectionCheck(),"reserved-page-initial-health-request");
        need(waitFor([&]{return sawFirstHealth.load();},1000),"reserved-page-initial-observation");
        healthPhase=2;need(h.runtime.requestConnectionCheck(),"reserved-page-positive-proof-request");
        need(waitFor([&]{return h.globalConnected.load()==1;},1000),"reserved-page-positive-proof");
        healthPhase=3;need(h.runtime.requestConnectionCheck(),"reserved-page-official-fault-request");
        const unsigned expectedSteps=scenario=="refused"||pending||missingProof||late?2u:1u;
        if(pending){
            need(waitFor([&]{return steps>=1;},2000),"identity-refusal-not-observed");
            for(unsigned i=0;i<100;++i)need(h.runtime.requestConnectionCheck(),"identity-check-hint-refused");
        }
        if(late){
            need(waitFor([&]{return steps==expectedSteps;},2000),"late-reopen-receipt-not-observed");
            need(h.runtime.requestConnectionCheck(),"late-reopen-stale-check");
            need(waitFor([&]{return staleReads>0;},1000),"late-reopen-stale-read-not-observed");
            if(lateHealthy){healthPhase=2;need(h.runtime.requestConnectionCheck(),"late-reopen-confirmation-check");}
            else while(GetTickCount64()-lateReceiptAt<Window::settleGraceMs+500)Sleep(50);
        }
        const bool completed=late&&!lateHealthy?staleReads>1:waitFor([&]{return steps>=expectedSteps&&h.globalConnected.load()>=2;},
            pending?12500:7500); // Original retry and subsequent health confirmation are separate waits.
        const auto plans=h.plans.load();const auto finalPid=h.client->snapshot().processId;
        h.runtime.shutdown();const auto returned=h.client->snapshot();
        unsigned exhausted=0,reopen=0;bool expired=false;
        if(late)for(const auto& record:h.globalRecoveryLog){
            const auto stage=record.value("stage",std::string{});
            if(stage=="reopen-bigpicture")++reopen;
            if(stage=="recovery-exhausted"){++exhausted;expired=record.value("recoveryTrigger",std::string{})=="observation-budget-expired";}
        }
        const bool behavior=heldAtStep==expectedSteps&&(late?gateCalls==0&&plans==1&&finalPid==originalPid&&reopen==1&&
            (lateHealthy?exhausted==0&&h.globalConnected==2:exhausted==1&&expired&&h.globalConnected==1):
            (scenario=="refused"||missingProof)?gateCalls==0&&plans==1&&finalPid==originalPid&&
            (!missingProof||nextPage==Action::reopen):pending?nextPage==Action::rebuild&&resolvedAt>=pendingAt&&
            resolvedAt-pendingAt>=ymcc::deckysidebar::ConnectionRecoveryWindow::evidenceIntervalMs&&gateCalls==1&&releasedAtGate==1:
            scenario=="granted"?gateCalls==1&&releasedAtGate==1&&finalPid!=originalPid:gateCalls==0&&finalPid!=originalPid);
        rows.push_back({{"scenario",scenario},{"passed",completed&&steps==expectedSteps&&behavior&&returned.fullyDestroyed&&handles()==before},
            {"steps",steps.load()},{"heldAtStep",heldAtStep.load()},{"gateCalls",gateCalls.load()},{"releasedAtGate",releasedAtGate.load()},
            {"nextPage",nextPage==Action::rebuild?"rebuild":nextPage==Action::reopen?"reopen":"none"},
            {"identityWaitMs",pendingAt&&resolvedAt>=pendingAt?resolvedAt-pendingAt:0},
            {"originalChild",originalPid},{"observedChild",finalPid},{"plans",plans},{"completed",completed},{"beforeHandles",before},
            {"afterHandles",handles()},{"returned",returned.fullyDestroyed},{"realSteamOrDeviceActions",0}});
        if(late)rows.back().update(Json{{"staleReads",staleReads.load()},{"reopen",reopen},{"exhausted",exhausted},
            {"expiredNormally",expired},{"connected",h.globalConnected.load()},{"recoveryLog",h.globalRecoveryLog}});
    }
    return rows;
}

using ReadyBackendObserver=std::function<Json(ymcc::deckysidebar::Runtime&,DWORD,HANDLE)>;
using ReadyBackendFactory=std::function<ReadyBackendObserver()>;
inline Json ready(const std::filesystem::path& output,bool retained,const ReadyBackendFactory& backendFactory={}){
    const auto queuedHealthProof=retained?Json{{"exercised",false}}:queuedRecoveryHealth(output/"queued-health");
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    std::atomic<unsigned long long> proofDue{0};std::atomic<bool> proofReturned{false};std::atomic<unsigned> proofCalls{0};
    const auto recovery=[&]{const auto due=proofDue.load();if(due&&GetTickCount64()>=due){++proofCalls;proofDue=0;proofReturned=true;}};
    const auto recoveryTimeout=[&]()->DWORD{const auto due=proofDue.load();const auto now=GetTickCount64();return !due?INFINITE:due<=now?0:static_cast<DWORD>(due-now);};
    const auto backendObserver=backendFactory?backendFactory():ReadyBackendObserver{};
    std::atomic<unsigned> coldBackendPhase{0};
    const auto coldBackendHealth=[&]{
        const auto phase=coldBackendPhase.load();
        ymcc::deckysidebar::ConnectionHealth value{phase!=0,phase==2,"official-backend-unconfirmed",false,"cold-backend-original-ready"};
        value.transportConnected=true;return value;
    };
    Harness harness(output,context,retained,{},{},{},recovery,recoveryTimeout,coldBackendHealth);const auto before=handles();
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
    bool coldBackendKeptMirror=true;
    if(!retained){
        // Exercise the cold owned-Loader window after the independent
        // first-handshake grace has expired; that branch already reports reuse.
        Sleep(ymcc::deckysidebar::ConnectionRecoveryWindow::transportGraceMs);
        coldBackendPhase=1;need(harness.runtime.requestConnectionCheck(),"cold-backend-observation-refused");
        Sleep(450); // Past the original 250ms transport retry, inside actual cold grace.
        coldBackendKeptMirror=harness.plans==1&&harness.client->snapshot().processId==originalBinding&&harness.runtime.snapshot().pid==originalLoader;
        need(coldBackendKeptMirror,"cold-backend-settle-rebound-healthy-mirror");
        coldBackendPhase=2;need(harness.runtime.requestConnectionCheck(),"cold-backend-returned-observation-refused");
        need(waitFor([&]{return harness.globalConnected.load()>0;},1000),"cold-backend-returned-health-not-consumed");
    }
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
    harness.throwLoaderStopDiagnostic=true;
    harness.runtime.shutdown();const auto destroyed=harness.client->snapshot();
    const bool ownershipRevoked=!ownedLoader.value||!harness.runtime.ownsLiveProcess(ownedLoader.value);
    if(ownedLoader.value){CloseHandle(ownedLoader.value);ownedLoader.value=nullptr;}
    if(foreignContext.value){CloseHandle(foreignContext.value);foreignContext.value=nullptr;}
    need(harness.loaderStopLog.size()==(retained?0u:2u),"owned-loader-stop-edge-pair-missing-or-spurious");
    Json coldBackendReconnect=Json::array();
    if(!retained){
        for(const auto& row:harness.globalRecoveryLog){
            if(row.value("stage",std::string{})=="reconnect"&&row.value("reason",std::string{})=="official-backend-unconfirmed")
                coldBackendReconnect.push_back(ymcc::deckydiag::record("connection-recovery",row));
        }
        need(coldBackendReconnect.size()==1&&coldBackendReconnect[0].value("attempted",false)&&
            coldBackendReconnect[0].value("bindingReused",false),"cold-backend-observation-must-report-existing-binding-reuse");
        const auto& began=harness.loaderStopLog[0];const auto& ended=harness.loaderStopLog[1];
        need(began.value("bootstrapStage",std::string{})=="loader-stop-started"&&ended.value("bootstrapStage",std::string{})=="loader-stop-finished","loader-stop-stage-order");
        for(const auto& row:harness.loaderStopLog)need(row.at("pid")==originalLoader&&row.at("loaderStopCause")=="runtime-stop"&&row.at("loaderBirth100ns").is_string(),"loader-stop-held-root-identity-and-cause");
        need(began.at("loaderBirth100ns")==ended.at("loaderBirth100ns")&&ended.value("loaderStopRootExited",false)&&ended.value("loaderStopExitKnown",false)&&ended.at("loaderStopExitCode")==0&&ended.at("loaderStopWait")==WAIT_OBJECT_0&&ended.at("loaderStopWaitError")==0,"loader-stop-actual-root-exit");
        need(ended.value("loaderStopJobEmptyConfirmed",false)&&ended.value("loaderStopBindingDestroyed",false),"loader-stop-resource-return-not-proven");
        Json bad=ended;bad["loaderStopCause"]="raw/private/value";bad["loaderBirth100ns"]="private";bad["loaderStopExitKnown"]=false;bad["loaderStopExitCode"]=42u;
        const auto clean=ymcc::deckydiag::record("bootstrap-watch",bad);
        need(!clean.contains("loaderStopCause")&&!clean.contains("loaderBirth100ns")&&!clean.contains("loaderStopExitCode"),"loader-stop-unknown-facts-leak");
    }
    return {{"id",retained?"native.typed-retained-ready-rebind-never-starts-loader":"native.typed-ready-rebind-keeps-original-owned-loader"},
        {"queuedHealthProof",queuedHealthProof},
        {"coldBackendSettleExercised",!retained},{"coldBackendKeptMirror",coldBackendKeptMirror},
        {"coldBackendReconnect",coldBackendReconnect},
        {"loaderStopLog",harness.loaderStopLog},{"optionalStopDiagnosticsThrowDidNotSkipCleanup",true},
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
struct Session {unsigned pid=41;unsigned long long created=123;std::wstring root=L"C:\\FixtureSteam";bool operator==(const Session&) const=default;};
static struct {Session sessionSnapshot()const{return {};}} g_steamSettingsObserver;
static struct {
 unsigned reads=0;std::function<void()> refreshed;
 void requestRefresh(const Session&){++reads;if(refreshed)refreshed();}
 bool requestDeckyResumeRefresh(const Session&,const ymcc::sleeppower::SharedPowerV1&,std::string*){return false;}
} g_steamModeObserver;
[[maybe_unused]] static ymcc::sleeppower::SharedPowerV1 fixtureDeckyPower(unsigned long long generation){
    using namespace ymcc::sleeppower;
    SharedPowerV1 power;power.generation=power.suspendSerial=generation;power.publicationRevision=1;
    power.phase=g_powerLifecycle==PowerLifecycle::Ready?0u:3u;
    power.flags=kKnown|(power.phase==0&&!g_exitRequested?kHardwareAllowed:0u);
    return power;
}
[[maybe_unused]] static bool sharedPowerLifecycleCurrent(const ymcc::sleeppower::SharedPowerV1& power) noexcept {
    return ymcc::sleeppower::samePowerRevocation(power,fixtureDeckyPower(currentPowerGeneration()));
}
static Json diagnostics=Json::array();
static void deckySidebarDiagnostic(const char* event,const Json& detail,bool){diagnostics.push_back({{"event",event},{"detail",detail}});}
// The exact shared Ready slice also notifies lighting; its hardware boundary
// stays inert here, as in the existing baseline fixture.
#include "decky_wake_ready_consumer.inc"
#include "decky_wake_committed_consumer.inc"
static bool deliverReady(unsigned long long generation){
#ifdef YMCC_FIXTURE_PUBLIC_DECKY_READY
    const auto before=diagnostics.size();
    (void)deckySidebarRecheckPublicReady(fixtureDeckyPower(generation));
    return diagnostics.size()!=before;
#else
    return deckySidebarResumeAfterIntent(generation);
#endif
}
#ifdef YMCC_FIXTURE_PUBLIC_DECKY_READY
static constexpr auto readyDeliveryEntry="public-ready";
#else
static constexpr auto readyDeliveryEntry="legacy-committed-ready";
#endif
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
    for(auto phase:{wakeReady::PowerLifecycle::Suspended,wakeReady::PowerLifecycle::Resuming}){wakeReady::g_powerLifecycle=phase;need(!wakeReady::deliverReady(power),"nonready S0 recovery admitted");}
    wakeReady::g_powerLifecycle=wakeReady::PowerLifecycle::Ready;
    need(!wakeReady::deliverReady(0)&&!wakeReady::deliverReady(power-1),"stale power generation admitted");
    wakeReady::g_exitRequested=true;need(!wakeReady::deliverReady(power),"exit admitted wake recovery");wakeReady::g_exitRequested=false;
    wakeReady::g_aiFanMockSession.isolated=true;need(!wakeReady::deliverReady(power),"isolated wake reached IO");wakeReady::g_aiFanMockSession.isolated=false;
    need(h.plans==plans&&wakeReady::g_steamModeObserver.reads==reads,"invalid wake touched connection");
    need(wakeReady::deliverReady(power),"committed S0 Ready ignored");
    h.waitForCheckpoint([&]{return isHealthy(h,retained,2);},"S0 Ready must replace stale binding and reach healthy new snapshot");
    for(unsigned i=0;i<512;++i){need(!wakeReady::deliverReady(power),"duplicate S0 notification refilled recovery");wakeReady::productionCommittedReady();}
    need(h.plans==plans+1&&wakeReady::g_steamModeObserver.reads==reads+1&&h.runtime.snapshot().pid==loader,"wake storm restarts Loader or creates duplicate binding/mode reads");
    const auto after=runtimeState(h);h.runtime.setEnabled(false);++wakeReady::observedGeneration;need(!wakeReady::deliverReady(wakeReady::observedGeneration),"OFF revived on late wake");
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
        for(unsigned i=0;i<512;++i){wakeReady::productionCommittedReady();need(!wakeReady::deliverReady(wakeReady::observedGeneration),"S0 after committed Ready refilled recovery");}
        need(wakeReady::g_steamModeObserver.reads==priorReads+1&&parked.plans==healthyPlans,"committed Ready storm must coalesce with S0 recovery by generation");
        const auto readyState=runtimeState(parked);parked.runtime.setEnabled(false);
        ++wakeReady::observedGeneration;wakeReady::productionCommittedReady();
        need(wakeReady::g_steamModeObserver.reads==priorReads+1&&parked.client->snapshot().fullyDestroyed,"late committed Ready revived OFF or touched shared mode");
        wakeReady::g_steamModeObserver.refreshed={};wakeReady::runtime=nullptr;
        committed={{"readyDeliveryEntry",wakeReady::readyDeliveryEntry},{"noLoaderOnEntry",true},{"terminalAdmissionOnEntry",true},{"modeReads",wakeReady::g_steamModeObserver.reads-priorReads},{"duplicateSignals",512},{"after",readyState},{"atExit",state(parked.client->snapshot())}};
    }
    return {{"id",retained?"native.typed-s0-intent-ready-retained":"native.typed-s0-intent-ready-owned"},{"passed",true},{"productionConsumerExtracted",true},{"committedReady",committed},
        {"powerGeneration",power},{"plansBefore",plans},{"plansAfter",h.plans.load()},{"modeReads",wakeReady::g_steamModeObserver.reads-reads},
        {"duplicateSignals",512},{"preparationsBefore",preparations},{"preparationsAfter",h.preparations.load()},{"loaderPidUnchanged",true},{"cefAlive",context.alive()},{"after",after},
        {"atExit",state(h.client->snapshot())},{"beforeHandles",beforeHandles},{"afterHandles",handles()}};
}
inline Json healing(const std::filesystem::path& output,bool retained,const std::string& boundary,bool heldEndpoint=false){
    const bool childExit=boundary=="child-exit",late=boundary=="late-result",off=boundary=="off";
    Controls controls(childExit?(heldEndpoint?L"exited-held-runtime":L"runtime-success-exit"):L"runtime-success-checkpoint");
    Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    detail::Handle attempted{CreateEventW(nullptr,TRUE,FALSE,(controls.readyName+L"-late-attempt").c_str())};
    std::atomic<unsigned> childExitHealthReads{0};
    const auto health=[&]{
        if(childExit)++childExitHealthReads;
        ymcc::deckysidebar::ConnectionHealth value{childExit,childExit,"page-connected-watch-exited",false,"watch-exit-fixed-owner"};
        value.transportConnected=childExit;return value;
    };
    Harness h(output,context,retained,{},{},{},{},{},health);const auto beforeHandles=handles();
    need(h.runtime.setEnabled(true),"healing-enable");
    h.waitForCheckpoint([&]{return isHealthy(h,retained,1);},"initial-healthy-snapshot");
    const auto initial=runtimeState(h);const auto originalLoader=h.runtime.snapshot().pid;
    const auto originalBinding=h.client->snapshot().processId;const auto initialEpoch=h.client->snapshot().epoch;const auto initialOwnerGeneration=h.client->snapshot().generation;
    const auto prepared=h.preparations.load();bool accepted=true,duplicateRejected=true,zeroRejected=true,lateAttempt=false;
    Json middle=Json::object();std::uint64_t target=2;detail::Handle heldLeaf;bool heldIdentity=!heldEndpoint,heldLeafExited=!heldEndpoint;
    if(childExit){
        detail::Handle old{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,originalBinding)};
        need(old.value!=nullptr,"failed-binding-observer");
        if(heldEndpoint){
            need(waitFor([&]{return h.descendant.load()!=0&&h.heldLeafBirth.load()!=0;}),"held-endpoint-proof-not-drained");
            heldLeaf.value=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,h.descendant.load());
            need(heldLeaf.value!=nullptr,"held-endpoint-leaf-observer");
            const auto observed=identity(heldLeaf.value);
            heldIdentity=worker::decimal(observed.at("birth"))==h.heldLeafBirth.load()&&worker::number(observed.at("pid"))==h.descendant.load();
            need(heldIdentity&&WaitForSingleObject(heldLeaf.value,0)==WAIT_TIMEOUT,"held-endpoint-exact-live-leaf");
        }
        setEnvironment(L"YMCC_BINDING_CLIENT_FIXTURE_MODE",L"runtime-success-checkpoint");controls.releaseNow();
        need(WaitForSingleObject(old.value,5000)==WAIT_OBJECT_0,"binding-child-did-not-exit");
        DWORD code=0;need(GetExitCodeProcess(old.value,&code)&&code==(heldEndpoint?0u:93u),"binding-failure-exit-code");
#ifdef YMCC_DECKY_REPORT_ALLOCATION_FIXTURE
        if(reportAllocationProbe::watchEndSelected.load())
            need(waitFor([&]{return reportAllocationProbe::watchEndConsumed();},1000),"watch-terminal-not-consumed-before-health-check");
        else
#endif
        need(waitFor([&]{return h.watchFailures.load()>0;},1000),"watch-terminal-not-consumed-before-health-check");
        const auto healthBefore=childExitHealthReads.load();
        need(h.runtime.requestConnectionCheck(),"watch-exit-current-health-check-refused");
        need(waitFor([&]{return childExitHealthReads.load()>healthBefore;},1000),"watch-exit-current-health-not-consumed");
        middle={{"bindingChildExitCode",code},{"stateAfterExit",runtimeState(h)},{"freshPageHealthAfterWatchExit",true}};
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
    const bool failureObserved=!childExit||h.watchFailures>0
#ifdef YMCC_DECKY_REPORT_ALLOCATION_FIXTURE
        ||reportAllocationProbe::watchEndConsumed()
#endif
        ;
    h.runtime.shutdown();const auto destroyed=h.client->snapshot();
    const bool heldTerminal=!heldEndpoint||h.heldEndpointTerminals.load()>0;
    if(heldEndpoint){heldLeafExited=WaitForSingleObject(heldLeaf.value,0)==WAIT_OBJECT_0;DWORD closeError=0;need(heldLeaf.close(closeError),"held-endpoint-observer-close");}
    return {{"id","native.typed-healing-"+boundary+(retained?"-retained":"-owned")},
        {"passed",accepted&&duplicateRejected&&zeroRejected&&generationFence&&acceptedOnlyCurrent&&failureObserved&&
            (!late||lateAttempt)&&plans==target&&quietHealthy&&ownerStable&&bindingPid!=originalBinding&&ownerGeneration>initialOwnerGeneration&&
            destroyed.fullyDestroyed&&beforeHandles==handles()&&heldIdentity&&heldLeafExited&&heldTerminal},
        {"heldEndpointExercised",heldEndpoint},{"heldEndpointExactIdentity",heldIdentity},{"heldEndpointLeafExited",heldLeafExited},{"heldEndpointTerminalObserved",heldTerminal},{"manualHeldLeafRelease",false},
        {"initial",initial},{"interleaved",middle},{"recovered",recovered},{"afterQuiet",quiet},
        {"resumeAccepted",accepted},{"duplicateResumeRejected",duplicateRejected},{"zeroResumeRejected",zeroRejected},
        {"childExitHealthReads",childExitHealthReads.load()},
        {"lateResultAttempted",lateAttempt},{"acceptedResults",h.results.load()},{"watchFailures",h.watchFailures.load()},
        {"currentEpochHealthy",generationFence&&quietHealthy},{"quietObservationMs",650},{"noDuplicateWiring",quietHealthy},
        {"loaderAndCefPreserved",ownerStable},{"atExit",state(destroyed)},{"beforeHandles",beforeHandles},{"afterHandles",handles()}};
}
#ifdef YMCC_BINDING_LOADER_NATIVE_CLOSE_FIXTURE
#include "fixtures/decky_loader_native_close_cases.inc"
#endif
inline Json healthy(const std::filesystem::path& output){
#ifdef YMCC_BINDING_LOADER_NATIVE_CLOSE_FIXTURE
    if(loaderCloseProbe::mode=="joint-live")return loaderNativeJointLive(output);
    if(loaderCloseProbe::mode=="final"||loaderCloseProbe::mode=="joint")return loaderNativeFinalClose(output);
    if(loaderCloseProbe::mode=="reentry")return loaderNativeSameRuntime(output);
    if(loaderCloseProbe::mode=="paused")return loaderNativePausedRecovery(output);
#endif
    Controls controls(L"burst");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    Harness harness(output,context,false,{},[](Json& request,unsigned){request["bootstrap"]["fixtureFailureTrace"]=true;});
    // The original context provider now fails after its first captured use.
    // A post-start relay must retain that use, not sample the replacement state.
    harness.poisonContextAfterResult=true;
    need(!harness.runtime.diagnosticRuntimeThreadCpu(),"runtime-cpu-off-reported-a-thread");
    const auto beforeHandles=handles();const auto handleSnapshotBefore=handleTable();const auto beforeThreads=threadCount();harness.start();
    SetLastError(ERROR_MORE_DATA);
    const auto cpuBefore=harness.runtime.diagnosticRuntimeThreadCpu();
    need(GetLastError()==ERROR_MORE_DATA,"runtime-cpu-changed-caller-last-error");
    const bool drained=waitFor([&]{return harness.burstDone.load();});const auto activeThreads=threadCount();
    const auto cpuAfter=harness.runtime.diagnosticRuntimeThreadCpu();
    need(cpuBefore&&cpuAfter&&cpuBefore->id!=GetCurrentThreadId()&&cpuBefore->id==cpuAfter->id&&
        cpuBefore->birth&&cpuBefore->birth==cpuAfter->birth&&cpuAfter->kernel>=cpuBefore->kernel&&
        cpuAfter->user>=cpuBefore->user,"runtime-cpu-not-the-same-held-worker");
    {
        detail::Handle thread{OpenThread(THREAD_QUERY_LIMITED_INFORMATION,FALSE,cpuAfter->id)};
        FILETIME born{},exited{},kernel{},user{};
        need(thread.value&&GetProcessIdOfThread(thread.value)==GetCurrentProcessId()&&
            GetThreadTimes(thread.value,&born,&exited,&kernel,&user)&&
            ((static_cast<ULONGLONG>(born.dwHighDateTime)<<32)|born.dwLowDateTime)==cpuAfter->birth,
            "runtime-cpu-worker-owner-or-birth-mismatch");
    }
    const auto active=harness.client->snapshot();const auto plansBeforeExit=harness.plans.load();
    bool childExitDrained=false;
    {
        detail::Handle loader{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_TERMINATE|SYNCHRONIZE,FALSE,harness.runtime.snapshot().pid)};
        need(loader.value&&harness.runtime.ownsLiveProcess(loader.value),"exit-probe-target-is-exact-runtime-owned-inert-loader");
        harness.runtime.requestExitFence();
        need(!harness.runtime.requestTransportRecovery(),"terminal-session-exit-rejects-late-transport-recovery");
        need(!harness.runtime.requestConnectionCheck(),"terminal-session-exit-rejects-late-health-demand");
        need(!harness.runtime.requestLaunchAdmissionRefresh(),"terminal-session-exit-rejects-late-admission");
        need(!harness.runtime.requestPreparationRefresh(999),"terminal-session-exit-rejects-late-preparation");
        need(!harness.runtime.requestBindingRefresh(999),"terminal-session-exit-rejects-late-binding");
        need(!harness.runtime.setEnabled(true),"terminal-session-exit-cannot-be-reenabled-by-late-callback");
        need(TerminateProcess(loader.value,1073807364u)!=FALSE,"own-inert-loader-replays-historical-exit-code");
        need(WaitForSingleObject(loader.value,1000)==WAIT_OBJECT_0,"own-inert-loader-exit-observed");
        childExitDrained=waitFor([&]{return harness.client->snapshot().fullyDestroyed&&harness.runtime.snapshot().phase=="disabled";});
        need(childExitDrained&&harness.loaderFaultPublications==0,"confirmed-exit-child-death-is-cleanup-not-loader-failure");
    }
    harness.runtime.observeSteam(1,true);harness.runtime.shutdown();
    need(!harness.runtime.diagnosticRuntimeThreadCpu(),"runtime-cpu-reported-a-thread-after-return");
    const auto remaining=harness.client->snapshot();const bool cefAlive=context.alive();
    Json watchTrace=Json::array();Json bindingStart;
    for(const auto& row:harness.failureTraceLog){
        if(row.value("actionOperation",std::string{})!="binding")continue;
        if(row.value("actionPhase",std::string{})=="started")bindingStart=row;
        if(row.value("actionPhase",std::string{})=="observation")watchTrace.push_back(row);
    }
    need(harness.throwFailureContext.load()&&bindingStart.is_object()&&watchTrace.size()==1,"post-start-watch-missing-or-stale-packets-misattributed");
    const auto& fault=watchTrace.at(0);
    for(const auto* key:{"actionId","generation","nativePowerGeneration","steamPid","steamBirth","recoveryEpisode"})need(fault.at(key)==bindingStart.at(key),"watch-error-resampled-a-late-session-or-power");
    need(fault.at("steamPid")==worker::number(context.lease.at("pid"))&&fault.at("nativePowerGeneration")==123,"watch-error-lost-original-context-after-provider-failed");
    need(fault["steamFailure"].value("domain",std::string{})=="cdp-protocol"&&fault["steamFailure"].value("code",0)==-32000,"watch-failure-lost-real-protocol-code");
    need(fault.dump().size()<=2304&&fault.dump().find("secret")==std::string::npos&&fault.dump().find("private-token")==std::string::npos,"watch-failure-leaked-private-text");
    return {{"id","native.typed-runtime-drains-live-watch-without-extra-reader-thread"},
        {"passed",drained&&harness.diagnostics==192&&remaining.fullyDestroyed&&beforeHandles==handles()&&activeThreads==beforeThreads+1&&cefAlive&&harness.plans==plansBeforeExit},
        {"runtimeWorkerCpu",{{"threadId",cpuAfter->id},{"threadBirth100ns",std::to_string(cpuAfter->birth)},
            {"kernelDelta100ns",cpuAfter->kernel-cpuBefore->kernel},{"userDelta100ns",cpuAfter->user-cpuBefore->user},
            {"retiredAfterShutdown",true}}},
        {"watchFailureTrace",watchTrace},{"watchContextProviderFailedAfterResult",harness.throwFailureContext.load()},
        {"diagnosticFrames",harness.diagnostics.load()},{"watchDrained",drained},{"beforeThreads",beforeThreads},{"activeThreads",activeThreads},
        {"beforeHandles",beforeHandles},{"afterHandles",handles()},{"handleSnapshotBefore",handleSnapshotBefore},{"handleSnapshotAfter",handleTable()},{"active",state(active)},{"atExit",state(remaining)},{"cefUnaffected",cefAlive},
        {"terminalExitFenced",true},{"lateRecoveryOrEnableAccepted",false},{"plansAfterExitFence",harness.plans.load()-plansBeforeExit},
        {"historicalChildExitCode",1073807364u},{"childExitBeforeExplicitShutdownDrained",childExitDrained},{"loaderFaultPublicationsAfterFence",harness.loaderFaultPublications.load()}};
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

#ifdef YMCC_BINDING_CONSTRUCTOR_FIXTURE
namespace actualPostConsumer {
using Json=nlohmann::json;using json=Json;
inline bool enabled=false,postAccepted=true,metadataFault=false;
inline unsigned postCalls=0,metadataHits=0;
inline HWND g_hwnd=nullptr,lastWindow=nullptr;inline UINT lastMessage=0;inline WPARAM lastW=0;inline LPARAM lastL=0;
inline BOOL postPort(HWND hwnd,UINT message,WPARAM w,LPARAM l) noexcept {++postCalls;lastWindow=hwnd;lastMessage=message;lastW=w;lastL=l;return postAccepted?TRUE:FALSE;}
struct MetadataScope {
 bool active=false;
 MetadataScope() noexcept {if(metadataFault){metadataFault=false;active=true;++metadataHits;ymcc::deckybinding::fixture::constructionprobe::skipAllocations=0;ymcc::deckybinding::fixture::constructionprobe::failNext=true;}}
 ~MetadataScope() noexcept {if(active){ymcc::deckybinding::fixture::constructionprobe::failNext=false;ymcc::deckybinding::fixture::constructionprobe::skipAllocations=0;}}
};
#define PostMessageW postPort
#include "decky_post_consumer.inc"
#undef PostMessageW
#undef WM_DECKY_MIRROR_EVENT
inline void clear(){g_deckyEvents.clear();g_deckyEventPosted=false;g_hwnd=nullptr;lastWindow=nullptr;postCalls=0;lastMessage=0;lastW=0;lastL=0;metadataFault=false;postAccepted=true;}
}
#endif

} // namespace rtfixture
namespace ymcc::deckymirror {
inline thread_local unsigned fixtureFrontendBuildReads=0;
inline thread_local void (*fixtureAfterFrontendRead)()=nullptr;
inline std::string countFixtureFrontendBuildId(const std::wstring& path){
    ++fixtureFrontendBuildReads;
    auto id=frontendBuildId(path);
    if(fixtureAfterFrontendRead)fixtureAfterFrontendRead();
    return id;
}
}
namespace rtfixture {
namespace offlineConsumer {
using json=nlohmann::json;
using Session=ymcc::steamsession::Session;
inline ymcc::deckysidebar::Runtime* runtime=nullptr;
inline Session observedSession;
inline Json observedMode;
inline unsigned brokerStops=0,brokerStarts=0,refreshes=0;
inline std::atomic<unsigned> modeSnapshots{0};
inline std::atomic<ULONGLONG> fixedTick{0};
inline ULONGLONG clockNow(){const auto now=fixedTick.load();return now?now:GetTickCount64();}
inline const std::wstring POWER_CONTROL_DIR=L"inert-no-payload";
inline std::string W2U(const std::wstring& value){const auto utf8=std::filesystem::path(value).u8string();return {utf8.begin(),utf8.end()};}
// The receipt port replays the existing observer publication. Its Windows
// session is observed once at setup, never by the production plan consumer.
inline DWORD observedWindowsSession=0;
inline unsigned identityVariant=0,identityRefreshes=0,bindingWindowsQueries=0;
inline bool cachedIdentityPublication=false;
struct Observer {
 std::unique_ptr<ymcc::steamsession::Observer> publisher;
 Session sessionSnapshot(){return observedSession;}
 std::shared_ptr<const ymcc::steamsession::IdentitySnapshot> identitySnapshot(){
  if(cachedIdentityPublication)return publisher?publisher->identitySnapshot():nullptr;
  if(!publisher||identityVariant==3)publisher=std::make_unique<ymcc::steamsession::Observer>();
  if(identityVariant==1)return {};
  auto value=std::make_shared<ymcc::steamsession::IdentitySnapshot>();
  value->session=observedSession;value->presence=identityVariant==2?-1:1;
  value->registeredPid=observedSession.pid;
  value->rootReadError=value->registeredPidReadError=value->accountReadError=ERROR_SUCCESS; // Explicit task-owned fact port, not registry proof.
  value->windowsSession=observedWindowsSession;value->windowsSessionKnown=identityVariant!=3;
  value->windowsSessionError=identityVariant==3?ERROR_ACCESS_DENIED:ERROR_SUCCESS;
  if(identityVariant==7)++value->session.account; // UI-local account need not match registry.
  ymcc::steamsession::IdentityPublicationFixture::publish(*publisher,*value);
  const auto current=publisher->identitySnapshot();
  if(identityVariant>=4&&identityVariant<=6){
   // Deliver a stale/foreign receipt against the real current publication.
   // Publishing forged PID/birth/root as current would remove this boundary.
   auto foreign=std::make_shared<ymcc::steamsession::IdentitySnapshot>(*current);
   if(identityVariant==4)++foreign->session.pid;
   if(identityVariant==5)++foreign->session.created;
   if(identityVariant==6)foreign->session.root+=L"-foreign";
   return foreign;
  }
  return current;
 }
 std::shared_ptr<ymcc::steamsession::IdentityDelegation> delegateIdentity(const std::shared_ptr<const ymcc::steamsession::IdentitySnapshot>& expected,DWORD account,bool fromUi){
  return publisher?publisher->delegateIdentity(expected,account,fromUi):nullptr;
 }
 bool requestIdentityRefresh(){++identityRefreshes;return true;}
};
inline BOOL bindingWindowsQuery(DWORD pid,PDWORD value){++bindingWindowsQueries;return ::ProcessIdToSessionId(pid,value);}
inline ymcc::steamui::ModeObserver* realModeObserver=nullptr;
struct Modes {
 template<class M,class Predicate>static bool admissionRefresh(M* observer,const Session& session,Predicate satisfied){
  if constexpr(requires(M& value){value.requestAdmissionRefresh(session,satisfied);})return observer->requestAdmissionRefresh(session,satisfied);
  else return observer->requestRefresh(session);
 }
 template<class Predicate>bool requestAdmissionRefresh(const Session& session,Predicate satisfied){
  if(realModeObserver){++refreshes;return admissionRefresh(realModeObserver,session,satisfied);}
  return satisfied(session,observedMode,false)||requestRefresh(session);
 }
 template<class M>static bool connectionRefresh(M* observer,const Session& session,std::source_location origin,ymcc::steamui::ConnectionRefreshQueue* queue,bool probeWithPeer=false){
  if constexpr(requires(M& value){value.requestConnectionRefresh(session,origin,queue,probeWithPeer);})return observer->requestConnectionRefresh(session,origin,queue,probeWithPeer);
  else if constexpr(requires(M& value){value.requestConnectionRefresh(session,origin,queue);})return observer->requestConnectionRefresh(session,origin,queue);
  else if constexpr(requires(M& value){value.requestConnectionRefresh(session);})return observer->requestConnectionRefresh(session);
  else return observer->requestRefresh(session); // Compatibility port for the unchanged old-source semantic control only.
 }
 bool requestConnectionRefresh(const Session& session,std::source_location origin=std::source_location::current(),ymcc::steamui::ConnectionRefreshQueue* queue=nullptr,bool probeWithPeer=false){
  ++refreshes;return realModeObserver?connectionRefresh(realModeObserver,session,origin,queue,probeWithPeer):true;
 }
 template<class M>static Json readSnapshot(M* observer,const Session& session,ymcc::steamui::ModeCacheMiss* miss,std::uint64_t* publication,ULONGLONG* observationTick,unsigned long long* peerRevision=nullptr){
  if(observationTick)*observationTick=0;
  if(peerRevision)*peerRevision=0;
  if constexpr(requires(M& value){value.snapshot(session,miss,nullptr,publication,observationTick,peerRevision);})return observer->snapshot(session,miss,nullptr,publication,observationTick,peerRevision);
  else if constexpr(requires(M& value){value.snapshot(session,miss,nullptr,publication,observationTick);})return observer->snapshot(session,miss,nullptr,publication,observationTick);
  else if constexpr(requires(M& value){value.snapshot(session,miss,nullptr,publication);})return observer->snapshot(session,miss,nullptr,publication);
  else if constexpr(requires(M& value){value.snapshot(session,miss);})return observer->snapshot(session,miss);
  else return observer->snapshot(session);
 }
 bool refusePassiveRefresh=false;
 std::uint64_t controlledPublication=1; // Advanced by the controlled facts, never by cache reads.
 ULONGLONG controlledObservationTick=0;
 unsigned long long controlledPeerRevision=1;
 std::optional<Session> publishBeforeSnapshotReturns;
 void setDemand(ymcc::steamui::ModeConsumer consumer,bool enabled){if(realModeObserver)realModeObserver->setDemand(consumer,enabled);}
 Json snapshot(const Session& session,ymcc::steamui::ModeCacheMiss* miss=nullptr,ymcc::steamui::ModeCacheTiming* =nullptr,std::uint64_t* publication=nullptr,ULONGLONG* observationTick=nullptr,unsigned long long* peerRevision=nullptr){
  ++modeSnapshots;if(publication)*publication=realModeObserver?0:controlledPublication;
  if(observationTick)*observationTick=realModeObserver?0:controlledObservationTick;
  if(peerRevision)*peerRevision=realModeObserver?0:controlledPeerRevision;
  if(publishBeforeSnapshotReturns){
   observedSession=std::move(*publishBeforeSnapshotReturns);
   publishBeforeSnapshotReturns.reset();
  }
  return realModeObserver?readSnapshot(realModeObserver,session,miss,publication,observationTick,peerRevision):observedMode;
 }
 bool requestRefresh(const Session& session,bool invalidate=true){++refreshes;return realModeObserver?realModeObserver->requestRefresh(session,invalidate):invalidate||!refusePassiveRefresh;}
 bool requestDeckyHealthRefresh(const Session& session){++refreshes;return realModeObserver?realModeObserver->requestDeckyHealthRefresh(session):!refusePassiveRefresh;}};
inline bool retainedConsent=false;
inline ymcc::deckyupdate::Recovery* initialUpdateRecovery=nullptr;
struct Consent {
 bool allows(const Session& s){return initialUpdateRecovery?initialUpdateRecovery->allows(s):true;}
 bool allowsRetainedFrontend(const Session& s,const Json& state,const std::string& build,const char* loader){return initialUpdateRecovery?initialUpdateRecovery->allowsRetainedFrontend(s,state,build,loader):retainedConsent;}
 std::string frontendUpdateSource(const Session& s,const Json& state,const std::string& build,const char* loader){return initialUpdateRecovery?initialUpdateRecovery->frontendUpdateSource(s,state,build,loader):std::string{};}
 template<class Recovery> static bool pending(const Recovery& recovery,const Session& s,const Json& state,const std::string& build,const char* loader){
  if constexpr(requires{recovery.waitsForFrontendUpdateBackend(s,state,build,loader);})return recovery.waitsForFrontendUpdateBackend(s,state,build,loader);
  else return false; // Original source has no pending method; original main never calls it.
 }
 bool waitsForFrontendUpdateBackend(const Session& s,const Json& state,const std::string& build,const char* loader){return initialUpdateRecovery&&pending(*initialUpdateRecovery,s,state,build,loader);}
};
struct Broker {bool connected=false;void stop(){++brokerStops;connected=false;}Json bootstrap(){return makeRequest(1).at("bootstrap");}Json status(){return {{"active",true},{"connected",connected},{"runId",makeRequest(1).at("bootstrap").at("runId")}};}
    bool matchesRunId(const std::string& run){return !run.empty()&&run==bootstrap().at("runId").get_ref<const std::string&>();}
    std::optional<ULONGLONG> pinnedAuthTick;
    std::atomic<unsigned long long> authRevision{1};
    std::optional<ULONGLONG> authenticatedPeerTick(unsigned long long* revision=nullptr){if(revision)*revision=connected?authRevision.load():0;
        const auto now=clockNow();return connected?std::optional<ULONGLONG>{pinnedAuthTick.value_or(now>=64?now-64:0)}:std::nullopt;}};
struct Cost {void binding(const Json&,DWORD,ULONGLONG,ULONGLONG){}};
inline Observer g_steamSettingsObserver;
inline Modes g_steamModeObserver;
inline Consent g_steamLinkAdmission,g_deckyUpdateRecovery;
inline Broker g_deckyMirrorBroker;
template<class C> void stampModeReader(C& config){
 if constexpr(requires(C& value){value.readDeckyPeerRevision;})
    config.readDeckyPeerRevision=[] {return g_deckyMirrorBroker.authRevision.load();};
}
// CSS-use scheduling is an explicit memory leaf in this Decky consumer.
// Observe only publication forwarding; no CSS helper, request or native wake.
struct CssUsePublicationPort {
 unsigned notices=0;const ymcc::steamsession::IdentitySnapshot* last=nullptr;
 bool notifyIdentityPublished(const std::shared_ptr<const ymcc::steamsession::IdentitySnapshot>& value) noexcept {++notices;last=value.get();return true;}
};
inline CssUsePublicationPort g_steamCssUse;
inline Cost g_deckyCostContext;
inline std::string deckySidebarStartMirrorBroker(){++brokerStarts;return {};}
inline Json deckySidebarEnvironment(bool,const std::function<bool()>& = {},bool = false){return {{"ready",true}};}
inline std::atomic<bool> g_exitRequested{false};
// Overlay startup is outside this offline Runtime consumer scope; retain the
// exact production callback call through an inert notification boundary.
static void steamOverlayStartupIdentityPublished() {}
inline struct {bool isolated=false;} g_aiFanMockSession;
enum class PowerLifecycle{Ready,Suspended};
inline std::atomic<PowerLifecycle>g_powerLifecycle{PowerLifecycle::Ready};
inline void deckySidebarSessionDiagnostic(const Session&,const char*,DWORD=0){}
inline Json* diagnosticCapture=nullptr;
inline bool rejectOwnerDiagnostic=false;
inline void deckySidebarDiagnostic(const char* event,const Json& value,bool=false){
 if(rejectOwnerDiagnostic&&value.contains("loaderOwnerAtReceipt"))throw std::bad_alloc();
 if(diagnosticCapture)diagnosticCapture->push_back(ymcc::deckydiag::record(event,value));
}
inline std::atomic<unsigned long long> fixturePower{1};inline unsigned long long currentPowerGeneration(){return fixturePower.load();}
inline std::atomic<unsigned long long> fixtureSuspendSerial{0};
inline int __cdecl readFixturePower(ymcc::sleeppower::SharedPowerV1* out,std::uint32_t bytes){
    if(!out||bytes<sizeof(*out))return 0;
    *out={};out->generation=currentPowerGeneration();out->suspendSerial=fixtureSuspendSerial.load();
    out->flags=ymcc::sleeppower::kKnown|(g_powerLifecycle.load()==PowerLifecycle::Ready?ymcc::sleeppower::kHardwareAllowed:0);
    return 1;
}
inline bool captureSharedPowerLifecycle(ymcc::sleeppower::SharedPowerV1& output){return ymcc::sleeppower::capturePower(&readFixturePower,output);}
inline bool sharedPowerLifecycleCurrent(const ymcc::sleeppower::SharedPowerV1& value){return ymcc::sleeppower::powerCurrent(&readFixturePower,value);}
inline bool metadataAccepted=true;
inline unsigned metadataPosts=0;
inline Json lastMetadata;
inline bool deckySidebarPost(const char* event,const Json& value,const char* packetKind=nullptr){
#ifdef YMCC_BINDING_CONSTRUCTOR_FIXTURE
 if(actualPostConsumer::enabled)return actualPostConsumer::deckySidebarPost(event,value,packetKind);
#else
 (void)event;
#endif
 if(cachedIdentityPublication){++metadataPosts;lastMetadata=value;if(packetKind)lastMetadata["bindingPacketKind"]=packetKind;return metadataAccepted;}
 return true;
}
inline std::atomic<unsigned> stableCssHints{0},stableWallpaperHints{0},stableManagedHints{0},stableAdmissionWakes{0};
struct StableCssGuard {void observeMode(const Session&,const ymcc::steamcss::ModeSample&,bool){++stableCssHints;}};
inline StableCssGuard g_steamCssGuard;
inline void steamWallpaperForegroundWake(){++stableWallpaperHints;}
inline void deckySidebarResumeManagedMode(const Session&){++stableManagedHints;}
inline void deckySidebarAdmissionWakeDiagnostic(ymcc::deckysidebar::AdmissionRefreshReceipt) noexcept {++stableAdmissionWakes;}
#define g_deckySidebarRuntime (*runtime)
#define GetTickCount64() clockNow()
#define ProcessIdToSessionId bindingWindowsQuery
#define frontendBuildId countFixtureFrontendBuildId
#include "decky_offline_consumers.inc"
inline auto bindingPlanForCurrentIntent(ymcc::deckysidebar::BindingMode mode,std::uint64_t generation){
    return deckySidebarOwnedBindingPlan(mode,generation,[]{return true;});
}
#undef frontendBuildId
#undef ProcessIdToSessionId
#undef GetTickCount64
#undef g_deckySidebarRuntime
}
// Private opt-in extension of the existing ready/rebind case. No default selector change.

// Explicit parameter of native.typed-runtime-live-watch. Actual main diagnostic
// and Runtime-held process HANDLE; no installed Loader/Steam/device operation.
inline Json officialLoaderOwnerClose(const std::filesystem::path& output){
 namespace actual=offlineConsumer;using Runtime=ymcc::deckysidebar::Runtime;
 Json captured=Json::array(),boundaries=Json::array();
 struct Ports {~Ports(){actual::diagnosticCapture=nullptr;actual::rejectOwnerDiagnostic=false;actual::runtime=nullptr;}} ports;
 actual::diagnosticCapture=&captured;actual::observedSession={};actual::fixedTick=0;
 const Json socket={{"schema",1},{"ordinal",1},{"closeCode",1006},{"codeKnown",true},{"cleanKnown",true},{"wasClean",false},{"opened",false},{"pendingCallsKnown",true},{"pendingCalls",0}};
 const Json packet={{"bootstrapStage","official-socket-close"},{"contextPid",1},{"officialSocketClose",socket}};
 const auto check=[&](bool okay,const char* name){boundaries.push_back({{"name",name},{"passed",okay}});need(okay,name);};
 Runtime empty;actual::runtime=&empty;
 SetLastError(0x54321);const auto absent=empty.diagnosticLoaderOwner();
 check(std::string_view(absent.state)=="absent"&&GetLastError()==0x54321,"absent-held-owner-and-last-error");
 actual::deckySidebarOwnedBindingDiagnostic(packet);
 check(captured.size()==1&&captured.back().contains("loaderOwnerAtReceipt"),"original-log-path-must-deliver-native-owner");
 check(captured.back()["loaderOwnerAtReceipt"]["state"]=="absent","absence-is-distinct-from-unknown");
 captured.clear();
 Controls controls(L"burst");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
 std::atomic<bool> entered{false},release{false};
 auto changed=[&](const ymcc::deckysidebar::State& value){if(value.phase=="loader-started"&&!entered.exchange(true)){
  const auto until=GetTickCount64()+2000;while(!release.load()&&GetTickCount64()<until)Sleep(1);
 }};
 Harness harness(output,context,false,{},{},{},{},{},{},{},{},changed);
 struct Release {std::atomic<bool>& value;~Release(){value=true;}} releaseOnFailure{release};
 actual::runtime=&harness.runtime;const auto processBefore=handles();harness.start();need(waitFor([&]{return entered.load();}),"original-owned-loader-publication-entered");
 const auto pid=harness.runtime.snapshot().pid;check(pid!=0&&harness.plans==1,"one-exact-owned-loader");
 detail::Handle held{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_TERMINATE|SYNCHRONIZE,FALSE,pid)};
 check(held.value&&harness.runtime.ownsLiveProcess(held.value),"independent-held-target-is-original-owned-inert-loader");
 FILETIME birth{},exit{},kernel{},user{};need(GetProcessTimes(held.value,&birth,&exit,&kernel,&user)!=FALSE,"independent-held-loader-birth");
 const auto expectedBirth=(static_cast<ULONGLONG>(birth.dwHighDateTime)<<32)|birth.dwLowDateTime;
 SetLastError(0x54321);const auto running=harness.runtime.diagnosticLoaderOwner();
 check(std::string_view(running.state)=="running"&&running.pid==pid&&running.birth==expectedBirth&&GetLastError()==0x54321,"held-running-identity-and-last-error");
 actual::deckySidebarOwnedBindingDiagnostic(packet);const auto loggedRunning=captured.back()["loaderOwnerAtReceipt"];
 check(loggedRunning["state"]=="running"&&loggedRunning["pid"]==pid&&loggedRunning["birth"]==std::to_string(expectedBirth),"main-path-current-owner-identity");
 const auto beforeReads=handles();for(unsigned n=0;n<1000;++n){const auto row=harness.runtime.diagnosticLoaderOwner();need(row.pid==pid&&std::string_view(row.state)=="running","repeat-owner-observation");}
 check(handles()==beforeReads,"thousand-held-observations-open-no-handle");
 const auto normalAt=captured.size();for(unsigned n=0;n<1000;++n)actual::deckySidebarOwnedBindingDiagnostic({{"bootstrapStage","context-attached"}});
 bool healthyUnchanged=true;for(std::size_t n=normalAt;n<captured.size();++n)healthyUnchanged=healthyUnchanged&&!captured[n].contains("loaderOwnerAtReceipt");
 check(healthyUnchanged,"unrelated-diagnostics-have-no-owner-field");
 actual::rejectOwnerDiagnostic=true;actual::deckySidebarOwnedBindingDiagnostic(packet);actual::rejectOwnerDiagnostic=false;
 check(captured.back().contains("officialSocketClose")&&!captured.back().contains("loaderOwnerAtReceipt"),"optional-owner-log-failure-keeps-original-close");
 Json malformed=packet;malformed["officialSocketClose"]["ordinal"]=17;actual::deckySidebarOwnedBindingDiagnostic(malformed);
 check(!captured.back().contains("loaderOwnerAtReceipt"),"invalid-close-cannot-add-owner-evidence");
 const Json unknown={{"schema",1},{"state","unknown"},{"error",ERROR_BUSY}};
 auto accept=[&](const Json& facts){auto row=packet;row["loaderOwnerAtReceipt"]=facts;return ymcc::deckydiag::record("bootstrap-watch",row).contains("loaderOwnerAtReceipt");};
 check(accept(unknown),"unknown-owner-is-not-absence");
 check(accept(loggedRunning),"normalizer-accepts-current-native-tuple");
 auto bad=loggedRunning;bad["schema"]=1.0;check(!accept(bad),"floating-schema-rejected");
 bad=loggedRunning;bad["birth"]="0";check(!accept(bad),"zero-birth-rejected");
 bad=loggedRunning;bad["birth"]="18446744073709551616";check(!accept(bad),"overflow-birth-rejected");
 bad=loggedRunning;bad["birth"]="+123";check(!accept(bad),"noncanonical-birth-rejected");
 bad=loggedRunning;bad["pid"]=0;check(!accept(bad),"zero-pid-rejected");
 bad=loggedRunning;bad["error"]=true;check(!accept(bad),"boolean-error-rejected");
 bad=loggedRunning;bad["extra"]="private-owner-text";check(!accept(bad),"extra-private-fields-rejected");
 harness.runtime.requestExitFence();check(TerminateProcess(held.value,37)!=FALSE&&WaitForSingleObject(held.value,1000)==WAIT_OBJECT_0,"exact-owned-inert-exit-fault-observed");
 SetLastError(0x54321);const auto ended=harness.runtime.diagnosticLoaderOwner();
 check(std::string_view(ended.state)=="exited"&&ended.pid==pid&&ended.birth==expectedBirth&&ended.exitCodeKnown&&ended.exitCode==37&&GetLastError()==0x54321,"exited-held-owner-retains-exact-birth-and-code");
 actual::deckySidebarOwnedBindingDiagnostic(packet);check(captured.back()["loaderOwnerAtReceipt"]["exitCode"]==37,"original-log-path-delivers-owned-exit-code");
 release=true;harness.runtime.shutdown();const auto returned=harness.client->snapshot();
 CloseHandle(held.value);held.value=nullptr;
 check(returned.fullyDestroyed&&returned.handles==0&&returned.pendingIo==0&&context.alive(),"original-binding-and-inert-context-resources-return");
 check(harness.plans==1&&actual::refreshes==0&&actual::brokerStops==0,"diagnostics-request-no-rebind-refresh-or-broker-stop");
 const auto after=harness.runtime.diagnosticLoaderOwner();check(std::string_view(after.state)=="absent","shutdown-held-owner-absent");
 check(captured.dump().find("private-owner-text")==std::string::npos,"owner-diagnostic-exposes-fixed-fields-only");
 return {{"passed",true},{"boundaries",boundaries},{"observedBeforeHandles",processBefore},{"observedAfterHandles",handles()},
  {"stableObservationHandlesBefore",beforeReads},{"stableObservationHandlesAfter",beforeReads},{"observedLoaderPid",pid},{"observedLoaderBirth",std::to_string(expectedBirth)},
  {"runningOwner",loggedRunning},{"exitedOwner",captured.at(captured.size()-1)["loaderOwnerAtReceipt"]},{"atExit",state(returned)},
  {"bindingPlans",harness.plans.load()},{"realSteamOrDeviceActions",0},{"actualMainDiagnostic",true},{"actualRuntimeHeldProcess",true},
  {"processHandleCountIsWholeProcessAcceptance",false},{"intentionalOwnedInertExitCode",37}};
}

#ifdef YMCC_BINDING_CONSTRUCTOR_FIXTURE

inline Json bindingReasonBoundary(const std::filesystem::path& output,unsigned selected){
 Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
 bindingReportProbe::selected=selected;bindingReportProbe::remaining=true;bindingReportProbe::hits=0;bindingReportProbe::site=0;bindingReportProbe::successfulResultSeen=false;
 bindingReportProbe::reasonEntries=0;bindingReportProbe::reasonExits=0;bindingReportProbe::reasonUnused=0;
 ymcc::deckybinding::fixture::constructionprobe::failures=0;ymcc::deckybinding::fixture::constructionprobe::failedBytes=0;
 const std::string longReason(96,'r'),fallback="mirror-bootstrap-failed";
 const auto expected=selected==6?longReason.capacity()+1:fallback.capacity()+1;
 std::atomic<bool> postLoop{false};
 auto plan=[&](Json& request,unsigned){if(selected==6)request["bootstrap"]["fixtureResultReason"]=longReason;};
 auto recovery=[&]{if(bindingReportProbe::reasonExits.load()>0)postLoop=true;};
 Harness h(output,context,false,{},plan,{},recovery);
 const auto before=handles();need(h.runtime.setEnabled(true),"reason-original-runtime-enable");need(waitFor([&]{return postLoop.load();}),"reason-original-worker-completes-result-window");
 const auto observed=h.runtime.snapshot();detail::Handle root;DWORD rootWait=WAIT_FAILED;unsigned long long birth=0;bool owned=false;
 if(observed.pid){root.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,observed.pid);FILETIME b{},e{},k{},u{};owned=root.value&&h.runtime.ownsLiveProcess(root.value)&&GetProcessTimes(root.value,&b,&e,&k,&u);if(owned){birth=(static_cast<unsigned long long>(b.dwHighDateTime)<<32)|b.dwLowDateTime;rootWait=WaitForSingleObject(root.value,0);}}
 const auto failures=ymcc::deckybinding::fixture::constructionprobe::failures.load(),plans=h.plans.load();const auto failedBytes=ymcc::deckybinding::fixture::constructionprobe::failedBytes.load();
 h.runtime.shutdown();const auto resources=h.client->snapshot();const auto final=h.runtime.snapshot();const bool returned=!root.value||CloseHandle(root.value)!=FALSE;if(returned)root.value=nullptr;
 Json result{{"id","native.typed-runtime-live-watch"},{"parameter","result-reason-read"},{"selectedSite",selected},{"probeHits",bindingReportProbe::hits.load()},{"probeSite",bindingReportProbe::site.load()},{"actualSuccessfulBindingResultObserved",bindingReportProbe::successfulResultSeen.load()},
  {"scopeEntries",bindingReportProbe::reasonEntries.load()},{"scopeExits",bindingReportProbe::reasonExits.load()},{"unusedFaultAtScopeExit",bindingReportProbe::reasonUnused.load()},{"faultFailures",failures},{"failedBytes",failedBytes},{"oldExpectedAllocationBytes",expected},{"reasonPresent",selected==6},
  {"phaseAfterFault",observed.phase},{"reasonAfterFault",observed.reason},{"pidAfterFault",observed.pid},{"rootBirth",std::to_string(birth)},{"rootOwned",owned},{"heldRootWait",rootWait},{"bindingPlans",plans},{"beforeHandles",before},{"afterHandles",handles()},{"rootHandleReturned",returned},{"atExit",state(resources)},{"runtimeDisabledAfterStop",!final.enabled&&final.pid==0},{"cefUnaffected",context.alive()},{"realSteamOrDeviceActions",0}};
 result["passed"]=bindingReportProbe::hits==1&&bindingReportProbe::site==selected&&bindingReportProbe::successfulResultSeen&&bindingReportProbe::reasonEntries==1&&bindingReportProbe::reasonExits==1&&bindingReportProbe::reasonUnused==1&&failures==0&&failedBytes==0&&plans==1&&owned&&rootWait==WAIT_TIMEOUT&&observed.phase=="loader-started"&&before==handles()&&resources.fullyDestroyed&&returned&&!final.enabled&&final.pid==0&&context.alive();return result;
}
inline Json bindingReasonControls(const std::filesystem::path& output,bool listenerOnly=false){
 bindingReportProbe::selected=0;Json rows=Json::array();
 const Json cases=Json::array({
  {{"name","success-absent"},{"ok",true}},{{"name","success-long"},{"ok",true},{"reason",std::string(96,'r')}},{{"name","success-numeric"},{"ok",true},{"reason",17}},
  {{"name","failure-absent"},{"ok",false}},{{"name","failure-empty"},{"ok",false},{"reason",""}},{{"name","failure-numeric"},{"ok",false},{"reason",17}},
  {{"name","failure-session"},{"ok",false},{"reason","mirror-bootstrap-session-changed"}},{{"name","failure-mutated"},{"ok",false},{"mutated",true},{"reason","mirror-bootstrap-session-changed"}},
  {{"name","failure-listener"},{"ok",false},{"reason","binding-listener-facts-unconfirmed"}}});
 for(const auto& sample:cases){
  if(listenerOnly&&sample.at("name")!="failure-listener")continue;
  Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));std::atomic<bool> postLoop{false};
  auto plan=[&](Json& request,unsigned){request["bootstrap"]["fixtureResultOk"]=sample.at("ok");request["bootstrap"]["fixtureResultMutated"]=sample.value("mutated",false);if(sample.contains("reason"))request["bootstrap"]["fixtureResultReason"]=sample.at("reason");};
  Harness h(output/worker::wide(sample.at("name")),context,false,{},plan,{},[&]{postLoop=true;});
  const auto before=handles();need(h.runtime.setEnabled(true),"reason-controls-runtime-enable");h.waitForCheckpoint([&]{return postLoop.load()||(sample.at("name")=="failure-listener"&&h.runtime.snapshot().reason=="binding-listener-facts-unconfirmed");},"reason-controls-original-result-loop");const auto observed=h.runtime.snapshot();const auto plans=h.plans.load();
  detail::Handle root;bool live=false;if(observed.pid){root.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,observed.pid);live=root.value&&h.runtime.ownsLiveProcess(root.value)&&WaitForSingleObject(root.value,0)==WAIT_TIMEOUT;}
  h.runtime.shutdown();const auto resources=h.client->snapshot();const auto final=h.runtime.snapshot();const bool returned=!root.value||CloseHandle(root.value)!=FALSE;if(returned)root.value=nullptr;
  const bool validSuccess=sample.at("ok").get<bool>()&&(!sample.contains("reason")||sample.at("reason").is_string());
  const bool passed=plans==1&&live==validSuccess&&before==handles()&&resources.fullyDestroyed&&returned&&!final.enabled&&final.pid==0&&context.alive();
  need(passed,"reason-controls-business-and-resource-contract");rows.push_back({{"name",sample.at("name")},{"phase",observed.phase},{"reason",observed.reason},{"bindingRetry",observed.bindingRetry},{"plans",plans},{"liveOwnedLoader",live},{"beforeHandles",before},{"afterHandles",handles()},{"fullyDestroyed",resources.fullyDestroyed},{"rootHandleReturned",returned},{"cefUnaffected",context.alive()},{"passed",passed}});std::cout<<"REASON_CONTROL_ROW "<<rows.back().dump()<<std::endl;
 }
 return {{"id","native.typed-runtime-live-watch"},{"parameter","result-reason-controls"},{"rows",rows},{"passed",rows.size()==(listenerOnly?1u:9u)},{"realSteamOrDeviceActions",0}};
}




template<class F> void originalBindingDiagnostic(F callback,const Json& packet,const char* kind){
 if constexpr(std::is_invocable_v<F,const Json&,const char*>)callback(packet,kind);
 else {auto legacy=packet;legacy["bindingPacketKind"]=kind;callback(legacy);}
}

// Explicit original typed-case parameters. The fault is restricted to the
// proven extra five-member object, while the real Runtime owns all actors.

// Same original healthy body and all original context/exit/resource assertions.
inline Json borrowedWatchStream(const std::filesystem::path& output){
 namespace actual=offlineConsumer;Json captured=Json::array();std::array<unsigned,192> seen{};
 unsigned packets=0,diagnosticPackets=0,resultPackets=0,throws=0;bool immutable=true,content=true;
 struct Restore {~Restore(){borrowedWatchSetup={};actual::runtime=nullptr;actual::diagnosticCapture=nullptr;actualPostConsumer::enabled=false;actualPostConsumer::clear();}} restore;
 actual::diagnosticCapture=&captured;actual::observedSession={};actualPostConsumer::clear();actualPostConsumer::enabled=true;actualPostConsumer::g_hwnd=reinterpret_cast<HWND>(1);
 borrowedWatchSetup=[&](Harness& harness,ymcc::deckysidebar::Config& config){
  actual::runtime=&harness.runtime;const auto observer=config.bindingDiagnostic;
  const bool accepted=configureBorrowedDiagnostic(config,[&,observer](const Json& packet,const char* kind){
   ++packets;const auto members=packet.size();const auto payload=packet.find("payload");const char* address=nullptr;
   if(payload!=packet.end()&&payload->is_string())address=payload->get_ref<const std::string&>().data();
   immutable=immutable&&!packet.contains("bindingPacketKind");if(std::strcmp(kind,"result")==0)++resultPackets;
   actual::deckySidebarOwnedBindingDiagnostic(packet,kind);
   // Only the existing fixture observer needs an owned row with kind. This
   // copy is outside production and is not used as allocation/CPU evidence.
   {auto observation=packet;observation["bindingPacketKind"]=kind;observer(observation);}
   immutable=immutable&&packet.size()==members&&!packet.contains("bindingPacketKind")&&(!address||address==packet.at("payload").get_ref<const std::string&>().data());
   if(packet.contains("index")){
    ++diagnosticPackets;const auto index=packet.at("index").get<unsigned>();content=content&&std::strcmp(kind,"diagnostic")==0&&index<seen.size();if(index<seen.size())++seen[index];
    const auto& text=packet.at("payload").get_ref<const std::string&>();content=content&&text.size()==8192&&std::all_of(text.begin(),text.end(),[](char c){return c=='d';});
    if(index==17||index==113){++throws;throw std::runtime_error("fixture-optional-watch-callback");}
   }
  });need(accepted,"original-healthy-uses-production-borrowed-path");
 };
 auto value=healthy(output);const bool allOnce=std::all_of(seen.begin(),seen.end(),[](unsigned n){return n==1;});
 const auto encoded=captured.dump();const bool redacted=encoded.find("private-token")==std::string::npos&&encoded.find("secret-untrusted-value")==std::string::npos;
 value["parameter"]="borrowed-watch";value["originalHealthyBodyUsed"]=true;value["borrowedPackets"]=packets;value["borrowedDiagnosticFrames"]=diagnosticPackets;value["borrowedResultFrames"]=resultPackets;
 value["borrowedSourceImmutable"]=immutable;value["payloadBytesChecked"]=diagnosticPackets*8192u;value["payloadContentValid"]=content;value["eachOriginalIndexOnce"]=allOnce;value["optionalCallbackThrows"]=throws;value["actualProductionLogs"]=captured.size();value["logRedactionPreserved"]=redacted;
 value["fixtureObserverCopiesNotCostEvidence"]=true;value["passed"]=value.value("passed",false)&&diagnosticPackets==192&&resultPackets==1&&throws==2&&immutable&&content&&allOnce&&redacted;return value;
}

inline Json borrowedPostQueue(bool fault){
 namespace post=actualPostConsumer;namespace probe=ymcc::deckybinding::fixture::constructionprobe;
 const auto before=handles();Json rows=Json::array();post::enabled=true;post::clear();post::metadataHits=0;probe::failures=0;probe::failedBytes=0;
 struct Restore{~Restore(){post::enabled=false;post::clear();}} restore;
 const Json packet={{"bootstrapStage","terminal"},{"reason","fixture-terminal"},{"runId","fixture-current"},{"mutated",false}};
 const auto observe=[](bool accepted,bool threw){return Json{{"accepted",accepted},{"threwBadAlloc",threw},{"queue",post::g_deckyEvents},{"posted",post::g_deckyEventPosted},{"posts",post::postCalls},{"message",post::lastMessage},{"w",post::lastW},{"l",post::lastL}};};
 if(fault){
  post::g_hwnd=reinterpret_cast<HWND>(1);post::metadataFault=true;bool accepted=false,threw=false;
  try{accepted=post::deckySidebarPost("deckySidebar.safetyStop",packet,"diagnostic");}catch(const std::bad_alloc&){threw=true;}
  auto value=observe(accepted,threw);const bool intact=post::g_deckyEvents.size()==1&&post::g_deckyEvents.back().at("data").at("runId")=="fixture-current"&&post::g_deckyEvents.back().at("data").at("reason")=="fixture-terminal";
  value["parameter"]="borrowed-post-metadata";value["metadataHits"]=post::metadataHits;value["allocationFailures"]=probe::failures.load();value["failedBytes"]=probe::failedBytes.load();value["dataIntact"]=intact;
  value["passed"]=accepted&&!threw&&post::g_deckyEventPosted&&post::postCalls==1&&post::metadataHits==1&&probe::failures==1&&intact;
  post::clear();value["beforeHandles"]=before;value["afterHandles"]=handles();value["passed"]=value["passed"].get<bool>()&&before==handles();return value;
 }
 for(unsigned which=0;which<10;++which){
  const auto seed=[&]{post::clear();post::g_hwnd=reinterpret_cast<HWND>(1);if(which==1){post::g_deckyEventPosted=true;post::g_deckyEvents.push_back({{"event","seed"},{"data",0}});}if(which>=2&&which<=5)for(unsigned n=0;n<32;++n)post::g_deckyEvents.push_back({{"event",which==4?"deckySidebar.safetyStop":which==2&&n==7?"deckySidebar.mirrorRequest":"seed"},{"data",n}});if(which==6)post::postAccepted=false;if(which==7)post::g_hwnd=nullptr;};
  const std::string event=which==5?"deckySidebar.mirrorRequest":"deckySidebar.safetyStop";const char* kind=which==8?"":"diagnostic";
  auto input=packet;if(which==9)input["bindingPacketKind"]="untrusted";auto original=input;original["bindingPacketKind"]=kind;
  seed();const bool acceptedLegacy=post::deckySidebarPost(event,original);const auto legacy=observe(acceptedLegacy,false);
  seed();const bool acceptedBorrowed=post::deckySidebarPost(event,input,kind);const auto actual=observe(acceptedBorrowed,false);
  bool contract=actual==legacy&&input==((which==9)?Json{{"bootstrapStage","terminal"},{"reason","fixture-terminal"},{"runId","fixture-current"},{"mutated",false},{"bindingPacketKind","untrusted"}}:packet);
  const auto size=post::g_deckyEvents.size();
  if(which==0||which==8||which==9)contract=contract&&acceptedBorrowed&&size==1&&post::postCalls==1;
  if(which==1)contract=contract&&acceptedBorrowed&&size==2&&post::postCalls==0;
  if(which==2)contract=contract&&acceptedBorrowed&&size==32&&std::none_of(post::g_deckyEvents.begin(),post::g_deckyEvents.end(),[](const Json& row){return row.at("event")=="deckySidebar.mirrorRequest";});
  if(which==3)contract=contract&&acceptedBorrowed&&size==32&&post::g_deckyEvents.front().at("data")==1;
  if(which==4||which==5)contract=contract&&!acceptedBorrowed&&size==32&&post::postCalls==0;
  if(which==6||which==7)contract=contract&&!acceptedBorrowed&&size==0&&!post::g_deckyEventPosted&&post::postCalls==(which==6?1u:0u);
  if(post::postCalls)contract=contract&&post::lastMessage==WM_USER+30&&post::lastW==0&&post::lastL==0;
  rows.push_back({{"index",which},{"legacy",legacy},{"actual",actual},{"passed",contract}});if(!contract)break;
 }
 const auto wanted=[](unsigned revision,bool subscribe,const char* source){
  return Json{{"connection","1"},{"connectionRevision",3u},{"revision",revision},{"runId","fixture-current"},
    {"request",{{"command","snapshot"},{"runId","fixture-current"},{"subscribe",subscribe},{"requestSource",source}}}};
 };
 const auto pause=wanted(1,false,"demand-stop"),newer=wanted(2,false,"hidden-snapshot");
 Json pauses=Json::array();
 for(const std::string_view boundary:{"adjacent","next-batch","resubscribe","safety-stop","connection","connection-revision","run","id-bearing","revision-regression","saturated"}){
  post::clear();post::g_hwnd=reinterpret_cast<HWND>(1);
  if(boundary=="saturated"){
   post::g_deckyEventPosted=true;
   for(unsigned i=0;i<31;++i)post::g_deckyEvents.push_back({{"event","seed"},{"data",i}});
  }
  const auto postRequest=[](const Json& input){return post::deckySidebarPost("deckySidebar.mirrorRequest",input);};
  bool accepted=postRequest(pause);auto second=newer;std::size_t expected=2;unsigned posts=1;
  if(boundary=="adjacent")expected=1;
  if(boundary=="saturated"){expected=32;posts=0;}
  if(boundary=="next-batch"){post::g_deckyEvents.clear();post::g_deckyEventPosted=false;expected=1;posts=2;}
  if(boundary=="resubscribe"){accepted=postRequest(wanted(1,true,"demand-start"))&&accepted;expected=3;}
  if(boundary=="safety-stop"){accepted=post::deckySidebarPost("deckySidebar.safetyStop",packet,"diagnostic")&&accepted;expected=3;}
  if(boundary=="connection")second["connection"]="2";
  if(boundary=="connection-revision")second["connectionRevision"]=4u;
  if(boundary=="run"){second["runId"]="fixture-other";second["request"]["runId"]="fixture-other";}
  if(boundary=="id-bearing")second["request"]["id"]="requires-receipt";
  if(boundary=="revision-regression")second["revision"]=0u;
  const bool secondAccepted=postRequest(second);const auto size=post::g_deckyEvents.size();
  const bool frontOkay=size>0&&(boundary=="saturated"?post::g_deckyEvents.front().at("event")=="seed":
    expected==1||post::g_deckyEvents.front().at("data")==pause);
  const bool middleOkay=(boundary!="resubscribe"||(size==3&&post::g_deckyEvents[1].at("data").at("request").at("subscribe")==true))&&
    (boundary!="safety-stop"||(size==3&&post::g_deckyEvents[1].at("event")=="deckySidebar.safetyStop"));
  const bool okay=accepted&&secondAccepted&&size==expected&&post::postCalls==posts&&post::g_deckyEventPosted&&
    frontOkay&&middleOkay&&post::g_deckyEvents.back().at("data")==second;
  pauses.push_back({{"boundary",boundary},{"queued",size},{"posts",post::postCalls},{"secondAccepted",secondAccepted},{"passed",okay}});
 }
 post::clear();return {{"parameter","borrowed-post-queue"},{"rows",rows},{"pauses",pauses},
  {"passed",rows.size()==10&&std::all_of(rows.begin(),rows.end(),[](const Json& row){return row.at("passed").get<bool>();})&&
    pauses.size()==10&&std::all_of(pauses.begin(),pauses.end(),[](const Json& row){return row.at("passed").get<bool>();})&&before==handles()},
  {"beforeHandles",before},{"afterHandles",handles()},{"actualOriginalFunction",true},{"windowsPostPort","fixed in-memory callback only; no real HWND/API action"}};
}


inline Json borrowedDiagnosticBoundary(const std::filesystem::path& output,unsigned selected){
 namespace actual=offlineConsumer;namespace probe=bindingReportProbe;
 Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
 probe::selected=selected;probe::remaining=true;probe::hits=0;probe::site=0;probe::successfulResultSeen=false;
 probe::tailCallbacks=0;probe::tailExpectedBytes=80;probe::borrowedPath=false;probe::sourceImmutable=false;probe::borrowedLogGolden=false;probe::borrowedThrows=0;
 ymcc::deckybinding::fixture::constructionprobe::failures=0;ymcc::deckybinding::fixture::constructionprobe::failedBytes=0;
 Json captured=Json::array(),rows=Json::array();actual::diagnosticCapture=&captured;actual::observedSession={};actual::fixedTick=0;
 struct Ports{~Ports(){actual::diagnosticCapture=nullptr;actual::runtime=nullptr;actual::cachedIdentityPublication=false;}} ports;
 std::atomic<bool> postLoop{false};
 auto customize=[&](auto& config){
  probe::borrowedPath=configureBorrowedDiagnostic(config,[&](const Json& packet,const char* kind){
   const bool result=std::strcmp(kind,"result")==0&&packet.value("ok",false);
   if(result){
    const auto original=packet.dump();
    {auto legacy=packet;legacy["bindingPacketKind"]=kind;const auto expected=ymcc::deckydiag::record("bootstrap",legacy);
     originalBindingDiagnostic(actual::deckySidebarOwnedBindingDiagnostic,packet,kind);probe::borrowedLogGolden=!captured.empty()&&captured.back()==expected;}
    probe::sourceImmutable=!packet.contains("bindingPacketKind")&&packet.dump()==original;
    ++probe::tailCallbacks;probe::successfulResultSeen=true;std::set_terminate(probe::borrowedTerminate);probe::borrowedReturned=true;
    if(selected==13){++probe::borrowedThrows;throw std::runtime_error("optional-borrowed-diagnostic-fault");}
   }else originalBindingDiagnostic(actual::deckySidebarOwnedBindingDiagnostic,packet,kind);
  });
 };
 Harness h(output,context,false,{},{},{},[&]{postLoop=true;},{},{},{},{},{},customize);actual::runtime=&h.runtime;
 const auto before=handles();need(h.runtime.setEnabled(true),"borrowed-diagnostic-enable");h.waitForCheckpoint([&]{return postLoop.load();},"borrowed-diagnostic-original-loop");
 const auto observed=h.runtime.snapshot();detail::Handle root;bool owned=false;DWORD waited=WAIT_FAILED;unsigned long long birth=0;
 if(observed.pid){root.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,observed.pid);FILETIME b{},e{},k{},u{};owned=root.value&&h.runtime.ownsLiveProcess(root.value)&&GetProcessTimes(root.value,&b,&e,&k,&u);if(owned){birth=(static_cast<unsigned long long>(b.dwHighDateTime)<<32)|b.dwLowDateTime;waited=WaitForSingleObject(root.value,0);}}
 const auto plans=h.plans.load();h.runtime.shutdown();const auto resources=h.client->snapshot();const auto final=h.runtime.snapshot();const bool returned=!root.value||CloseHandle(root.value)!=FALSE;if(returned)root.value=nullptr;
 if(selected==12){
  actual::cachedIdentityPublication=true;
  const Json socket={{"schema",1},{"ordinal",1},{"closeCode",1006},{"codeKnown",true},{"cleanKnown",true},{"wasClean",false},{"opened",false},{"pendingCallsKnown",true},{"pendingCalls",0}};
  const Json samples=Json::array({
   {{"kind","result"},{"detail",{{"ok",true},{"reason","fixture-original"},{"mutated",false}}}},
   {{"kind","diagnostic"},{"detail",{{"bootstrapStage","plan-finished"},{"elapsedMs",0u}}}},
   {{"kind","diagnostic"},{"detail",{{"bootstrapStage","terminal"},{"reason","fixture-terminal"},{"runId","fixture"},{"mutated",false}}}},
   {{"kind","diagnostic"},{"detail",{{"bootstrapStage","official-socket-close"},{"contextPid",1},{"officialSocketClose",socket}}}},
   {{"kind","diagnostic"},{"detail",{{"scope","global-connection"},{"recoveryPhase","connected"},{"recoveryEpisode",1u}}}},
   {{"kind","result"},{"detail",{{"ok",false},{"reason","mirror-bootstrap-session-changed"},{"mutated",false}}}},
   {{"kind","diagnostic"},{"detail",{{"bootstrapStage","connection-check"},{"reason","qam-open"},{"runId",""}}}},
   {{"kind","diagnostic"},{"detail",{{"bootstrapStage","failed"},{"reason","mirror-retained-frontend-changed"}}}}
  });
  for(const auto& sample:samples){
   const auto kind=sample.at("kind").get<std::string>();const auto& packet=sample.at("detail");auto legacy=packet;legacy["bindingPacketKind"]=kind;
   captured.clear();actual::lastMetadata=nullptr;actual::metadataPosts=0;actual::deckySidebarOwnedBindingDiagnostic(legacy);
   const auto originalLog=captured;const auto originalPost=actual::lastMetadata;const auto originalPosts=actual::metadataPosts;
   captured.clear();actual::lastMetadata=nullptr;actual::metadataPosts=0;originalBindingDiagnostic(actual::deckySidebarOwnedBindingDiagnostic,packet,kind.c_str());
   const bool equal=captured==originalLog&&actual::lastMetadata==originalPost&&actual::metadataPosts==originalPosts&&!packet.contains("bindingPacketKind");
   rows.push_back({{"index",rows.size()},{"kind",kind},{"logs",captured},{"post",actual::lastMetadata},{"postCount",actual::metadataPosts},{"passed",equal}});need(equal,"borrowed-original-production-log-and-terminal-wire");
  }
  actual::cachedIdentityPublication=false;
 }
 Json value={{"id","native.typed-runtime-live-watch"},{"parameter",selected==11?"borrowed-diagnostic":selected==12?"borrowed-payloads":"borrowed-throws"},{"borrowedConfigured",probe::borrowedPath.load()},{"callbackCount",probe::tailCallbacks.load()},{"sourceImmutable",probe::sourceImmutable.load()},{"logGolden",probe::borrowedLogGolden.load()},{"callbackThrows",probe::borrowedThrows.load()},{"faultHits",probe::hits.load()},{"faultFailures",ymcc::deckybinding::fixture::constructionprobe::failures.load()},{"expectedOldDestroyBytes",80},{"rows",rows},
 {"phase",observed.phase},{"reason",observed.reason},{"pid",observed.pid},{"birth",std::to_string(birth)},{"rootOwned",owned},{"heldRootWait",waited},{"plans",plans},{"beforeHandles",before},{"afterHandles",handles()},{"rootHandleReturned",returned},{"atExit",state(resources)},{"runtimeDisabled",!final.enabled&&final.pid==0},{"cefUnaffected",context.alive()},{"realSteamOrDeviceActions",0}};
 value["passed"]=probe::borrowedPath&&probe::sourceImmutable&&probe::borrowedLogGolden&&probe::tailCallbacks==1&&probe::successfulResultSeen&&probe::borrowedThrows==(selected==13?1u:0u)&&probe::hits==0&&ymcc::deckybinding::fixture::constructionprobe::failures==0&&plans==1&&owned&&waited==WAIT_TIMEOUT&&observed.phase=="loader-started"&&before==handles()&&resources.fullyDestroyed&&returned&&!final.enabled&&final.pid==0&&context.alive()&&(selected!=12||rows.size()==8);return value;
}

inline Json diagnosticTailBoundary(const std::filesystem::path& output,bool fault){
 Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
 bindingReportProbe::selected=fault?9u:10u;bindingReportProbe::remaining=true;bindingReportProbe::hits=0;bindingReportProbe::site=0;bindingReportProbe::successfulResultSeen=false;bindingReportProbe::tailCallbacks=0;bindingReportProbe::tailExpectedBytes=0;
 ymcc::deckybinding::fixture::constructionprobe::failures=0;ymcc::deckybinding::fixture::constructionprobe::failedBytes=0;
 if(fault)std::set_terminate([]{
  const char* kind="none";try{if(const auto error=std::current_exception())std::rethrow_exception(error);}catch(const std::bad_alloc&){kind="bad-alloc";}catch(const std::exception&){kind="std-exception";}catch(...){kind="other";}
  std::fprintf(stderr,"DIAGNOSTIC_TAIL_TERMINAL {\"exception\":\"%s\",\"failures\":%u,\"failedBytes\":%zu,\"insideJsonDestroy\":%u,\"members\":%zu,\"expectedBytes\":%zu,\"callbacks\":%u,\"successfulResultObserved\":%u,\"probeHits\":%u,\"probeSite\":%u}\n",
   kind,ymcc::deckybinding::fixture::constructionprobe::failures.load(),ymcc::deckybinding::fixture::constructionprobe::failedBytes.load(),bindingReportProbe::insideJsonDestroy?1u:0u,bindingReportProbe::jsonDestroyMembers,bindingReportProbe::tailExpectedBytes.load(),bindingReportProbe::tailCallbacks.load(),bindingReportProbe::successfulResultSeen?1u:0u,bindingReportProbe::hits.load(),bindingReportProbe::site.load());
  std::fflush(stderr);std::_Exit(std::strcmp(kind,"bad-alloc")==0?207:208);
 });
 std::atomic<bool> postLoop{false};Harness h(output,context,false,{}, {}, {}, [&]{postLoop=true;});
 const auto before=handles();need(h.runtime.setEnabled(true),"diagnostic-tail-enable");h.waitForCheckpoint([&]{return postLoop.load();},"diagnostic-tail-original-loop");const auto observed=h.runtime.snapshot();
 detail::Handle root;bool owned=false;DWORD waited=WAIT_FAILED;unsigned long long birth=0;
 if(observed.pid){root.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,observed.pid);FILETIME b{},e{},k{},u{};owned=root.value&&h.runtime.ownsLiveProcess(root.value)&&GetProcessTimes(root.value,&b,&e,&k,&u);if(owned){birth=(static_cast<unsigned long long>(b.dwHighDateTime)<<32)|b.dwLowDateTime;waited=WaitForSingleObject(root.value,0);}}
 const auto plans=h.plans.load();h.runtime.shutdown();const auto resources=h.client->snapshot();const auto final=h.runtime.snapshot();const bool returned=!root.value||CloseHandle(root.value)!=FALSE;if(returned)root.value=nullptr;
 Json result{{"id","native.typed-runtime-live-watch"},{"parameter","diagnostic-tail-control"},{"faultRequested",fault},{"callbackCount",bindingReportProbe::tailCallbacks.load()},{"expectedBytes",bindingReportProbe::tailExpectedBytes.load()},{"faultFailures",ymcc::deckybinding::fixture::constructionprobe::failures.load()},
 {"phase",observed.phase},{"reason",observed.reason},{"pid",observed.pid},{"birth",std::to_string(birth)},{"rootOwned",owned},{"heldRootWait",waited},{"plans",plans},{"beforeHandles",before},{"afterHandles",handles()},{"rootHandleReturned",returned},{"atExit",state(resources)},{"runtimeDisabled",!final.enabled&&final.pid==0},{"cefUnaffected",context.alive()},{"realSteamOrDeviceActions",0}};
 result["passed"]=!fault&&bindingReportProbe::tailCallbacks==1&&bindingReportProbe::successfulResultSeen&&ymcc::deckybinding::fixture::constructionprobe::failures==0&&plans==1&&owned&&waited==WAIT_TIMEOUT&&observed.phase=="loader-started"&&before==handles()&&resources.fullyDestroyed&&returned&&!final.enabled&&final.pid==0&&context.alive();return result;
}

inline Json bindingReportBoundary(const std::filesystem::path& output,unsigned selected){
 Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
 bindingReportProbe::selected=selected;bindingReportProbe::remaining=true;bindingReportProbe::hits=0;bindingReportProbe::site=0;bindingReportProbe::successfulResultSeen=false;
 std::atomic<bool> postLoop{false};
 auto changed=[&](const ymcc::deckysidebar::State& observed){if(observed.phase=="starting")bindingReportProbe::arm(1);};
 auto plan=[&](Json&,unsigned){bindingReportProbe::arm(2);};
 auto recovery=[&]{if(bindingReportProbe::hits==1&&ymcc::deckybinding::fixture::constructionprobe::failures==1)postLoop=true;};
 Harness harness(output,context,false,{},plan,{},recovery,{},{},{},{},changed);
 const auto before=handles();need(harness.runtime.setEnabled(true),"binding-report-original-runtime-enabled");
 need(waitFor([&]{return postLoop.load();}),"binding-report-fault-completes-original-worker-loop");
 const auto observed=harness.runtime.snapshot();const auto plans=harness.plans.load();
 detail::Handle root;bool rootOwned=false;DWORD rootWait=WAIT_FAILED;unsigned long long birth=0;
 if(observed.pid){root.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,observed.pid);FILETIME b{},e{},k{},u{};
  rootOwned=root.value&&harness.runtime.ownsLiveProcess(root.value)&&GetProcessTimes(root.value,&b,&e,&k,&u);
  if(rootOwned){birth=(static_cast<unsigned long long>(b.dwHighDateTime)<<32)|b.dwLowDateTime;rootWait=WaitForSingleObject(root.value,0);}}
 const auto failures=ymcc::deckybinding::fixture::constructionprobe::failures.load();const auto bytes=ymcc::deckybinding::fixture::constructionprobe::failedBytes.load();
 const std::size_t expectedBytes=selected>=3?sizeof(Json::object_t):sizeof(std::string);
 const bool selectedResult=selected!=3||bindingReportProbe::successfulResultSeen.load();
 harness.runtime.shutdown();const auto resources=harness.client->snapshot();const auto final=harness.runtime.snapshot();
 const bool rootReturned=!root.value||CloseHandle(root.value)!=FALSE;if(rootReturned)root.value=nullptr;
 Json result{{"id","native.typed-runtime-live-watch"},{"parameter","binding-optional-report"},{"selectedSite",selected},{"passed",false},
  {"probeHits",bindingReportProbe::hits.load()},{"probeSite",bindingReportProbe::site.load()},{"failures",failures},{"failedBytes",bytes},{"expectedFirstAllocationBytes",expectedBytes},
  {"actualSuccessfulBindingResultObserved",bindingReportProbe::successfulResultSeen.load()},
  {"phaseAfterFault",observed.phase},{"reasonAfterFault",observed.reason},{"pidAfterFault",observed.pid},{"rootBirth",std::to_string(birth)},{"rootOwned",rootOwned},{"heldRootWait",rootWait},
  {"bindingPlans",plans},{"beforeHandles",before},{"afterHandles",handles()},{"rootHandleReturned",rootReturned},{"atExit",state(resources)},
  {"runtimeDisabledAfterStop",!final.enabled&&final.pid==0},{"cefUnaffected",context.alive()},{"realSteamOrDeviceActions",0}};
 result["passed"]=bindingReportProbe::hits==1&&bindingReportProbe::site==selected&&failures==1&&bytes==expectedBytes&&selectedResult&&rootOwned&&rootWait==WAIT_TIMEOUT&&
  observed.phase=="loader-started"&&plans==1&&before==handles()&&resources.fullyDestroyed&&rootReturned&&!final.enabled&&final.pid==0&&context.alive();
 return result;
}
#endif

#include "fixtures/decky_initial_update_cases.inc"
inline Json wakeProbeReadSlot(const offlineConsumer::Session& session,Json mode,const char* control){
    namespace actual=offlineConsumer;
    using ymcc::steamui::ModeConsumer;using ymcc::steamui::ConnectionRefreshQueue;
    const auto serial=actual::fixtureSuspendSerial.exchange(1);
    struct RestoreSerial{unsigned long long serial;~RestoreSerial(){actual::fixtureSuspendSerial=serial;}} restore{serial};
    mode["deckyBackendConnected"]=true;mode["deckyClientState"]="socket-connecting";
    const std::string_view kind(control);
    const bool inFlightControl=kind=="reopen-in-flight"||kind=="power-css-in-flight";
    std::atomic<unsigned> reads{0};
    std::atomic<bool> held{false},released{false},retiredFence{false},stalePublished{false};
    ymcc::steamui::ModeObserver observer;ymcc::steamui::ModeObserverConfig cfg;
    cfg.debounceMs=180;cfg.admissionPowerReader=&actual::readFixturePower;
    cfg.readOwned=[&](const auto&,const auto& current){
        if(++reads==7&&inFlightControl){
            auto stale=mode;stale["deckyClientVerified"]=true;held=true;
            if(!waitFor([&]{return released.load();},2500))return Json{};
            retiredFence=!current();
            return stale; // Simulate a reply crossing the final transport fence.
        }
        return current()?mode:Json{};
    };
    cfg.cssBigPicturePublished=[&](const auto&,const auto& result,const auto&,bool){
        if(result.value("deckyClientVerified",false))stalePublished=true;
    };
    need(observer.initialize(std::move(cfg)),"wake-probe-slot-observer-started");
    observer.setDemand(ModeConsumer::Decky,true);observer.setDemand(ModeConsumer::Css,true);observer.observeSteam(session);
    const auto published=[&](unsigned n,DWORD budget=1800){return waitFor([&]{std::uint64_t p=0;observer.snapshot(session,nullptr,nullptr,&p);return p>=n;},budget);};
    need(published(1),"wake-probe-slot-known-first-sample");
    for(unsigned n=2;n<=6;++n){need(observer.requestRefresh(session,false)&&published(n),"wake-probe-slot-ordinary-cadence-after-finite-startup");}
    const auto origin=std::source_location::current();
    const auto refresh=[&](bool probe,ConnectionRefreshQueue* queue=nullptr){return actual::Modes::connectionRefresh(&observer,session,origin,queue,probe);};
    ConnectionRefreshQueue ordinary,early;
    need(refresh(false,&ordinary)&&refresh(true,&early),"wake-probe-slot-original-and-probe-accepted");
    const bool expedited=ordinary.slotWaitMs>=4000&&early.slotWaitMs<=180&&early.coalesced&&!early.inFlight;
    bool passed=false;DWORD repeatWait=0;
    if(kind=="consume"){
        const bool proof=published(7);
        ConnectionRefreshQueue repeat;
        for(unsigned i=0;i<64;++i)need(refresh(true,&repeat),"wake-probe-slot-repeat-coalesces");
        repeatWait=repeat.slotWaitMs;
        passed=proof&&reads==7&&repeatWait>=4000;
    }else if(inFlightControl){
        if(expedited&&waitFor([&]{return held.load();},1800)){
            bool admitted=true;
            if(kind=="reopen-in-flight"){
                observer.setDemand(ModeConsumer::Decky,false);
                observer.setDemand(ModeConsumer::Decky,true);
            }else{
                ++actual::fixturePower;
                ymcc::sleeppower::SharedPowerV1 next;
                admitted=actual::captureSharedPowerLifecycle(next)&&observer.requestPublicResumeRefresh(session,next);
            }
            released=true;
            const bool successor=published(7,6500);
            passed=admitted&&retiredFence&&successor&&reads==8&&!observer.snapshot(session).value("deckyClientVerified",true);
        }
    }else{
        if(kind=="off")observer.setDemand(ModeConsumer::Decky,false);
        else ++actual::fixturePower;
        Sleep(550);
        passed=reads==6;
    }
    observer.shutdown();
    return {{"control",control},{"ordinaryWaitMs",ordinary.slotWaitMs},{"probeWaitMs",early.slotWaitMs},
        {"repeatWaitMs",repeatWait},{"reads",reads.load()},{"retiredFence",retiredFence.load()},
        {"stalePublished",stalePublished.load()},{"passed",expedited&&passed&&!stalePublished}};
}
inline Json offlineRebuiltOwnedProof(const std::filesystem::path& output,const Context& context,Json good){
    namespace actual=offlineConsumer;
    const auto before=handles();
    good.erase("deckyBackendConnected");good["deckyBackendState"]="socket-missing";
    good["modeEpoch"]="offline-old:1";
    auto rebuilt=good;rebuilt["modeEpoch"]="offline-rebuilt:2";
    auto pending=rebuilt;pending["deckyAccountWatchAvailable"]=false;
    std::atomic<unsigned> phase{0},reads{0},loops{0},changed{0},stable{0},steps{0};
    std::atomic<bool> connected{false},paused{false};
    auto health=actual::actualConnectionHealth();auto publishStable=actual::actualStableBigPicture();
    ymcc::steamui::ModeObserver shared;
    ymcc::steamui::ModeObserverConfig config;config.debounceMs=1;config.minimumGapMs=1;
    config.read=[&](const auto&){const auto value=phase.load();return value==0?good:value==1?pending:rebuilt;};
    config.changed=[&](const auto&,const auto&){++changed;};
    config.stableBigPicture=[&](const auto& session){publishStable(session);++stable;};
    actual::stampModeReader(config);
    need(shared.initialize(std::move(config)),"offline-owned-real-shared-reader");
    shared.setDemand(ymcc::steamui::ModeConsumer::Decky,true);shared.observeSteam(actual::observedSession);
    need(waitFor([&]{return shared.snapshot(actual::observedSession)==good;}),"offline-owned-old-document-read");
    actual::g_deckyMirrorBroker.connected=true;actual::g_deckyMirrorBroker.pinnedAuthTick=1;
    Harness h(output,context,false,{},{},{},[&]{++loops;},{},[&]{
        const auto result=health();connected=result.connected;paused=result.paused;++reads;return result;
    },[&](auto){++steps;return Json{{"ok",false},{"mutated",false}};});
    struct Clear{~Clear(){actual::realModeObserver=nullptr;actual::runtime=nullptr;actual::g_deckyMirrorBroker.pinnedAuthTick.reset();}}clear;
    actual::runtime=&h.runtime;actual::realModeObserver=&shared;
    h.start();need(waitFor([&]{return h.burstDone.load();}),"offline-owned-binding-live");
    const auto pid=h.runtime.snapshot().pid;
    const auto plans=h.plans.load();
    phase=1;
    need(shared.requestRefresh(actual::observedSession,false)&&waitFor([&]{return shared.snapshot(actual::observedSession)==pending;}),
        "offline-rebuilt-document-published-without-account-proof");
    const auto gapReads=reads.load(),gapLoops=loops.load();
    need(h.runtime.requestConnectionCheck()&&waitFor([&]{return reads>gapReads&&loops>gapLoops&&paused&&!connected;},1000),
        "offline-rebuilt-document-parks-owned-loader-health");
    const auto lackProofStable=stable.load(),lackProofReads=reads.load();
    need(shared.requestRefresh(actual::observedSession,false)&&waitFor([&]{return stable>lackProofStable;},1000),
        "offline-rebuilt-document-repeats-missing-proof");
    need(reads==lackProofReads&&paused,"missing-account-proof-does-not-wake-owned-loader-health");
    const auto proofAt=GetTickCount64();
    const auto proofStable=stable.load();
    const auto proofChanges=changed.load(),proofLoops=loops.load();phase=2;
    need(shared.requestRefresh(actual::observedSession,false)&&waitFor([&]{return stable>proofStable&&shared.snapshot(actual::observedSession)==rebuilt;},1000),
        "offline-current-page-proof-only-changes-account-witness");
    const bool delivered=waitFor([&]{return reads>lackProofReads&&loops>proofLoops&&connected&&!paused;},900);
    const auto elapsed=GetTickCount64()-proofAt,proofReads=static_cast<ULONGLONG>(reads.load()-lackProofReads);
    const auto idleStable=stable.load(),idleReads=reads.load();
    need(shared.requestRefresh(actual::observedSession,false)&&waitFor([&]{return stable>idleStable;},1000),"offline-rebuilt-stable-proof-repeated");
    const bool idle=reads==idleReads;
    const bool unchanged=changed==proofChanges&&h.runtime.snapshot().pid==pid&&h.plans==plans&&steps==0;
    h.runtime.setEnabled(false);h.runtime.shutdown();const auto stoppedReads=reads.load();
    publishStable(actual::observedSession);
    const bool off=stoppedReads==reads&&!h.runtime.requestConnectionCheck(true);
    shared.shutdown();const auto returned=h.client->snapshot();
    const bool resources=returned.fullyDestroyed&&returned.handles==0&&returned.pendingIo==0&&before==handles()&&context.alive();
    const Json result={{"passed",delivered&&proofReads==1&&idle&&unchanged&&off&&resources},{"delivered",delivered},{"elapsedMs",elapsed},
        {"healthReads",proofReads},{"idle",idle},{"off",off},{"originalLoaderAndBinding",unchanged},{"returned",resources},
        {"beforeHandles",before},{"afterHandles",handles()},{"additionalBindingPlans",h.plans.load()-plans},{"recoverySteps",steps.load()}};
    std::cout<<"OFFLINE_REBUILT_PAGE_PROOF "<<result.dump()<<std::endl;
    return result;
}
inline Json retiredOfflineFirstProof(const std::filesystem::path& output){
    namespace actual=offlineConsumer;
    Controls controls(L"burst");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    const auto before=handles();
    const auto base=makeRequest(59);
    const auto session=actual::observedSession={worker::wide(base["steam"]["root"]),GetCurrentProcessId(),0,worker::decimal(base["steam"]["created"])};
    const Json initial={{"known",true},{"running",true},{"mode","bigpicture"},{"modeEpoch","offline-old:1"},
        {"pid",session.pid},{"birth",std::to_string(session.created)},{"observedAccount",12345678u},
        {"deckyAccountWatchAvailable",true},{"deckyFrontendLoaded",true},{"deckyHooksInstalled",true},
        {"deckyClientConnected",true},{"deckyClientVerified",true},{"deckyClientChecking",false},{"deckyBackendState","socket-missing"},
        {"deckyUpdateResidue",{{"protocol",1},{"active",true},{"bridgePresent",true},
            {"frontendBuildId",std::string(64,'d')},{"loaderVersion",ymcc::deckyidentity::version}}}};
    auto recovered=initial;recovered["modeEpoch"]="offline-current:2";
    auto missing=recovered;missing["deckyAccountWatchAvailable"]=false;
    std::mutex gate;std::atomic<unsigned> phase{0},stableProofs{0},changes{0},readyChecks{0},steps{0};
    std::atomic<bool> deliverUnconfirmed{false},unconfirmedDelivered{false};
    actual::retainedConsent=true;actual::g_deckyMirrorBroker.connected=true;actual::g_deckyMirrorBroker.pinnedAuthTick=1;
    ymcc::steamui::ModeObserver shared;ymcc::steamui::ModeObserverConfig mode;mode.debounceMs=1;mode.minimumGapMs=20;
    auto stable=actual::actualStableBigPicture();auto health=actual::actualConnectionHealth();auto admit=actual::actualAdmission();
    mode.read=[&](const auto&){const auto value=phase.load();return value==0?initial:value==1?missing:recovered;};
    mode.changed=[&](const auto&,const auto&){++changes;};
    mode.stableBigPicture=[&](const auto& source){
        std::lock_guard lock(gate);
        if(phase==1&&deliverUnconfirmed.exchange(false)){stable(source);unconfirmedDelivered=true;}
        else if(phase==2){stable(source);++stableProofs;}
    };
    actual::stampModeReader(mode);
    need(shared.initialize(std::move(mode)),"retired-offline-reader-starts");
    shared.setDemand(ymcc::steamui::ModeConsumer::Decky,true);shared.observeSteam(session);
    need(waitFor([&]{return shared.snapshot(session)==initial;}),"retired-offline-initial-proof");
    Harness h(output,context,true,[&]{std::lock_guard lock(gate);return admit();},[&](Json&,unsigned){
        std::lock_guard lock(gate);actual::g_deckyMirrorBroker.connected=true;
    },{},{},{},[&]{
        std::lock_guard lock(gate);auto result=health();
        if(phase==2&&result.connected&&!result.paused&&actual::runtime->snapshot().phase=="frontend-attached")++readyChecks;
        return result;
    },[&](auto){++steps;return Json{{"ok",false},{"mutated",false}};},{},[&](const auto& state){
        std::lock_guard lock(gate);actual::actualRuntimeChanged(state);
    });
    struct Clear{~Clear(){actual::runtime=nullptr;actual::realModeObserver=nullptr;actual::retainedConsent=false;actual::g_deckyMirrorBroker.pinnedAuthTick.reset();}}clear;
    actual::runtime=&h.runtime;actual::realModeObserver=&shared;
    need(h.runtime.setEnabled(true)&&waitFor([&]{return h.burstDone&&h.runtime.snapshot().phase=="frontend-attached";}),"retired-offline-initial-binding");
    phase=1;
    need(shared.requestRefresh(session,false)&&waitFor([&]{return shared.snapshot(session)==missing;}),"retired-offline-page-published-without-proof");
    need(h.runtime.requestConnectionCheck()&&waitFor([&]{return h.runtime.snapshot().phase=="waiting-manual-steam";}),"retired-offline-old-binding-retires-on-gap");
    need(waitFor([&]{return shared.snapshot(session)==missing;}),"retired-offline-gap-observation-settled");
    const auto beforeMissing=actual::stableAdmissionWakes.load(),oldPlans=h.plans.load();
    deliverUnconfirmed=true;
    need(shared.requestRefresh(session,false)&&waitFor([&]{return unconfirmedDelivered.load();}),"retired-offline-unconfirmed-stable-notification-delivered");
    const auto missingWakes=actual::stableAdmissionWakes.load()-beforeMissing;
    need(waitFor([&]{return shared.snapshot(session)==missing;}),"retired-offline-before-proof-returns");
    const auto changesBefore=changes.load();phase=2;const auto began=GetTickCount64();
    need(shared.requestRefresh(session,false)&&waitFor([&]{return stableProofs>0&&h.plans>oldPlans&&h.runtime.snapshot().phase=="frontend-attached";}),"retired-offline-normal-attachment-returns");
    const bool verified=waitFor([&]{return readyChecks>0;},1000);
    const auto elapsed=GetTickCount64()-began, checks=static_cast<ULONGLONG>(readyChecks.load());
    const auto stableBefore=stableProofs.load();
    need(shared.requestRefresh(session,false)&&waitFor([&]{return stableProofs>stableBefore;}),"retired-offline-repeated-stable-proof");
    const bool idle=checks==readyChecks, unchanged=changes==changesBefore;
    const auto additionalPlans=h.plans.load()-oldPlans;
    h.runtime.setEnabled(false);h.runtime.shutdown();shared.shutdown();
    const auto returned=h.client->snapshot();
    const bool resources=returned.fullyDestroyed&&returned.handles==0&&returned.pendingIo==0&&before==handles()&&context.alive();
    const Json result={{"passed",verified&&checks==1&&idle&&unchanged&&missingWakes==0&&additionalPlans==1&&steps==0&&resources},
        {"unconfirmedAdmissionWakes",missingWakes},{"changedPublications",changes.load()-changesBefore},{"additionalPlans",additionalPlans},
        {"readyChecks",checks},{"verified",verified},{"elapsedMs",elapsed},{"idle",idle},{"recoverySteps",steps.load()},
        {"resourcesReturned",resources},{"beforeHandles",before},{"afterHandles",handles()}};
    std::cout<<"RETIRED_OFFLINE_FIRST_PROOF "<<result.dump()<<std::endl;
    return result;
}
inline Json sharedModeWorkerCounters(const ymcc::steamsession::Session& session,const Json& good){
    constexpr unsigned healthCallsPerSlot=32;
    std::atomic<unsigned> entered{0},releaseThrough{0};
    ymcc::steamui::ModeObserver shared;
    ymcc::steamui::ModeObserverConfig cfg;cfg.debounceMs=1;cfg.minimumGapMs=1;
    cfg.read=[&](const auto&){const auto n=++entered;
        (void)waitFor([&]{return releaseThrough>=n;},2000);return good;};
    need(shared.initialize(std::move(cfg)),"shared-counter-worker-start");
    shared.setDemand(ymcc::steamui::ModeConsumer::Decky,true);
    shared.observeSteam(session);
    need(waitFor([&]{return entered==1;}),"initial-shared-read-paused");
    const auto initial=shared.diagnosticDeckyWorkerCpu();
    need(initial&&initial->readsStarted==1&&initial->deckyHealthSlotsArmed==0,"initial-read-is-not-a-decky-health-slot");
    need(shared.requestRefresh(session,false),"independent-refresh-arms-next-slot");
    for(unsigned i=0;i<healthCallsPerSlot;++i)need(shared.requestDeckyHealthRefresh(session),"health-can-join-independent-slot");
    const auto joined=shared.diagnosticDeckyWorkerCpu();
    need(joined&&joined->readsStarted==1&&joined->deckyHealthSlotsArmed==0,"joining-another-request-does-not-attribute-its-slot-to-health");
    releaseThrough=1;need(waitFor([&]{return entered==2;}),"independent-shared-read-paused");
    for(unsigned i=0;i<healthCallsPerSlot;++i)need(shared.requestDeckyHealthRefresh(session),"health-arms-and-joins-own-slot");
    const auto armed=shared.diagnosticDeckyWorkerCpu();
    need(armed&&armed->readsStarted==2&&armed->deckyHealthSlotsArmed==1,"queued-and-executing-counts-stay-distinct");
    releaseThrough=3;need(waitFor([&]{return entered==3;}),"health-only-schedules-one-additional-read");
    const auto dispatched=shared.diagnosticDeckyWorkerCpu();
    need(dispatched&&dispatched->readsStarted==3&&dispatched->deckyHealthSlotsArmed==1&&
        dispatched->id==initial->id&&dispatched->birth==initial->birth,"dispatch-is-counted-on-the-existing-shared-worker");
    shared.setDemand(ymcc::steamui::ModeConsumer::Css,true);
    shared.setDemand(ymcc::steamui::ModeConsumer::Decky,false);
    need(!shared.diagnosticDeckyWorkerCpu()&&!shared.requestDeckyHealthRefresh(session),"retired-decky-health-cannot-schedule-css-only-work");
    need(shared.requestRefresh(session,false),"css-consumer-can-still-queue-a-read");
    releaseThrough=4;need(waitFor([&]{return entered==4;}),"css-only-read-still-executes");
    shared.shutdown();
    return {{"initialReadsStarted",initial->readsStarted},{"independentCoalescedHealthSlots",joined->deckyHealthSlotsArmed},
        {"readsBeforeHealthDispatch",armed->readsStarted},{"totalReadsStarted",dispatched->readsStarted},
        {"deckyHealthSlotsArmed",dispatched->deckyHealthSlotsArmed},{"coalescedHealthCalls",healthCallsPerSlot*2-1},
        {"offRejectedWithCssDemand",true},{"cssOnlyReadAccepted",true}};
}
inline Json retainedFirstProof(const std::filesystem::path& output){
    namespace actual=offlineConsumer;
    Controls controls(L"burst");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    const auto before=handles();std::mutex gate;std::atomic<unsigned> reads{0},steps{0},readPhase{0};std::atomic<bool> release{false};
    std::atomic<bool> holdNextPeerRead{false},peerReadEntered{false},releasePeerRead{false};
    const auto base=makeRequest(59);actual::observedSession={worker::wide(base["steam"]["root"]),GetCurrentProcessId(),0,worker::decimal(base["steam"]["created"])};
    Json old={{"known",true},{"running",true},{"mode","bigpicture"},{"pid",actual::observedSession.pid},{"birth",std::to_string(actual::observedSession.created)},
        {"observedAccount",12345678u},{"deckyAccountWatchAvailable",true},{"deckyFrontendLoaded",true},{"deckyBackendConnected",false},{"deckyHooksInstalled",true},
        {"deckyClientConnected",false},{"deckyClientVerified",false},{"deckyClientChecking",false},{"deckyClientState","socket-missing"}};
    old["deckyUpdateResidue"]={{"protocol",1},{"active",true},{"bridgePresent",true},
        {"frontendBuildId",std::string(64,'a')},{"loaderVersion",ymcc::deckyidentity::version}};
    auto good=old;good["deckyClientConnected"]=true;good["deckyClientVerified"]=true;good["deckyClientState"]="connected";
    const auto workerCounters=sharedModeWorkerCounters(actual::observedSession,good);
    actual::observedMode=old;actual::retainedConsent=true;actual::g_deckyMirrorBroker.connected=true;actual::fixedTick=0;
    ymcc::steamui::ModeObserver shared;ymcc::steamui::ModeObserverConfig cfg;cfg.debounceMs=1;cfg.minimumGapMs=runRetainedLocalBusyProbe?500:1;
    std::atomic<DWORD> readerTid{0};
    actual::stampModeReader(cfg);
    cfg.read=[&](const auto& session){readerTid=GetCurrentThreadId();const auto n=++reads;if(n==1)return old;

        if(readPhase==2)return Json{{"known",true},{"running",true},{"mode","unknown"},{"pid",session.pid},{"birth",std::to_string(session.created)},{"reason","live-busy"},{"localReadBusy",true}};
        const auto end=GetTickCount64()+2000;
        if(holdNextPeerRead.exchange(false)){peerReadEntered=true;while(!releasePeerRead&&GetTickCount64()<end)Sleep(1);}
        while(!release&&GetTickCount64()<end)Sleep(1);return readPhase==1?Json{{"known",false},{"mode","unknown"},{"reason","inert-read-failure"}}:good;};
    need(!shared.diagnosticDeckyWorkerCpu(),"unstarted-shared-mode-worker-not-sampled");
    need(shared.initialize(std::move(cfg)),"first-proof-real-shared-observer-started");
    need(!shared.diagnosticDeckyWorkerCpu(),"no-Decky-demand-no-shared-mode-cpu-sample");
    shared.setDemand(ymcc::steamui::ModeConsumer::Decky,true);shared.observeSteam(actual::observedSession);
    need(waitFor([&]{return shared.snapshot(actual::observedSession).value("mode",std::string{})=="bigpicture";}),"first-proof-old-pre-binding-mode-present");
    const DWORD sentry=ERROR_BAD_UNIT;
    SetLastError(sentry);
    const auto sharedCpu=shared.diagnosticDeckyWorkerCpu();
    const bool preservedError=GetLastError()==sentry;
    need(sharedCpu&&sharedCpu->id==readerTid.load()&&sharedCpu->birth&&!sharedCpu->cssDemand&&preservedError,
        "shared-mode-cpu-identifies-actual-reader-and-preserves-last-error");
    {
        detail::Handle thread{OpenThread(THREAD_QUERY_LIMITED_INFORMATION,FALSE,sharedCpu->id)};
        FILETIME born{},exited{},kernel{},user{};
        const auto ticks=[](const FILETIME& t){return (static_cast<ULONGLONG>(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
        need(thread.value&&GetProcessIdOfThread(thread.value)==GetCurrentProcessId()&&
             GetThreadTimes(thread.value,&born,&exited,&kernel,&user)&&ticks(born)==sharedCpu->birth&&
             ticks(kernel)>=sharedCpu->kernel&&ticks(user)>=sharedCpu->user,"shared-mode-cpu-os-thread-identity-and-counters");
    }
    const unsigned beforeSamples=reads.load();
    for(unsigned i=0;i<16;++i)need(shared.diagnosticDeckyWorkerCpu().has_value(),"shared-mode-cpu-borrows-live-worker");
    need(beforeSamples==1&&reads==beforeSamples,"shared-mode-diagnostics-do-not-dispatch-read");
    auto sourceLease=shared.observedSource(actual::observedSession);need(sourceLease.current(),"first-proof-public-source-initially-current");
    actual::realModeObserver=&shared;
    struct Clear {~Clear(){actual::realModeObserver=nullptr;actual::runtime=nullptr;actual::retainedConsent=false;}}clear;
    auto admission=actual::actualAdmission();auto health=actual::actualConnectionHealth();
    std::atomic<bool> holdHealth{false},healthHeld{false},throwHealth{false},healthFaultEntered{false},lastHealthConnected{false};
    std::atomic<unsigned> healthReads{0},loopsAfterHealthFault{0};
    Harness h(output,context,true,[&]{std::lock_guard lock(gate);return admission();},[&](Json&,unsigned){std::lock_guard lock(gate);actual::g_deckyMirrorBroker.connected=true;},{},[&]{if(healthFaultEntered)++loopsAfterHealthFault;},{},
        [&]{++healthReads;if(holdHealth){healthHeld=true;const auto end=GetTickCount64()+1500;while(holdHealth&&GetTickCount64()<end)Sleep(1);healthHeld=false;}
            if(throwHealth.exchange(false)){healthFaultEntered=true;throw std::bad_alloc();}
            std::lock_guard lock(gate);const auto result=health();lastHealthConnected=result.connected&&!result.paused;return result;},[&](auto){++steps;return Json{{"ok",false},{"mutated",false},{"reason","inert-unexpected-recovery"}};},{},
        [&](const auto& state){std::lock_guard lock(gate);actual::actualRuntimeChanged(state);});
    actual::runtime=&h.runtime;need(h.runtime.setEnabled(true),"first-proof-runtime-enabled");
    need(waitFor([&]{return h.runtime.snapshot().phase=="frontend-attached"&&h.burstDone.load();}),"first-proof-retained-binding-accepted");
    need(shared.snapshot(actual::observedSession).value("reason",std::string{})=="shared-mode-pending","retained-attachment-must-retire-pre-binding-mode");
    need(sourceLease.current(),"retained-connection-observation-must-not-revoke-public-Steam-source");
    need(waitFor([&]{return reads>=2;}),"first-proof-original-reader-starts-current-request");
    holdHealth=true;
    need(h.runtime.requestConnectionCheck()&&waitFor([&]{return healthHeld.load();},1000),"first-proof-health-worker-holds-in-flight-check");
    const auto beforeHints=healthReads.load();
    Json queueRows=Json::array();unsigned wakeSignals=0;
    {
        std::lock_guard lock(gate);actual::diagnosticCapture=&queueRows;
        rtHealthWakeProbe::signals=&wakeSignals;
        struct ResetCapture{~ResetCapture(){actual::diagnosticCapture=nullptr;rtHealthWakeProbe::signals=nullptr;}}resetCapture;
        const auto run=actual::g_deckyMirrorBroker.bootstrap()["runId"];
        for(unsigned hint=0;hint<32;++hint)
            actual::deckySidebarOwnedBindingDiagnostic({{"bootstrapStage","connection-check"},{"reason",hint==0?"probe-result":"qam-open"},{"runId",run}});
        actual::deckySidebarOwnedBindingDiagnostic({{"bootstrapStage","connection-check"},{"reason","probe-result"},{"runId","retired"}});
    }
    const bool hintsRanWhileHeld=healthHeld.load();holdHealth=false;
    const bool followup=waitFor([&]{return healthReads.load()>beforeHints;},1000);
    std::cout<<"CONNECTION_WAKE_COALESCING "<<Json{{"hints",32},{"wakeSignals",wakeSignals},{"held",hintsRanWhileHeld},{"followup",followup}}.dump()<<std::endl;
    need(hintsRanWhileHeld&&followup&&wakeSignals==1,"first-proof-one-kernel-wake-per-pending-check-and-no-lost-successor");
    need(queueRows.size()==33&&queueRows.front().at("source")=="probe-result","queue-observation-records-existing-probe-and-open-events");
    for(unsigned i=0;i<32;++i){const auto& row=queueRows.at(i);
        need(row.at("attempted")==true&&row.at("readInFlight")==true&&row.at("readSlotWaitMs").is_number_unsigned()&&
             row.at("readSlotWaitMs")<=(runRetainedLocalBusyProbe?500u:1u)&&!row.contains("runId"),"accepted-inflight-queue-diagnostic-is-bounded");
        if(i>0)need(row.at("readCoalesced")==true,"repeated-hints-observe-the-same-queued-slot");
    }
    need(queueRows.back().at("attempted")==false&&!queueRows.back().contains("readSlotWaitMs"),"retired-run-logs-no-queue-observation");
    const auto scrub=[](Json row,const char* event,const char* field,Json value){
        row[field]=std::move(value);return ymcc::deckydiag::record(event,row).contains("readSlotWaitMs");};
    need(!scrub(queueRows.front(),"connection-check","readSlotWaitMs",60001u)&&
         !scrub(queueRows.front(),"connection-check","readCoalesced","true")&&
         !scrub(queueRows.front(),"connection-check","scope","another-diagnostic")&&
         !scrub(queueRows.front(),"other","readSlotWaitMs",1u),"queue-observation-filter-rejects-overlimit-and-wrong-scope-or-types");
    need(sourceLease.current()&&reads==2,"inflight-connection-hints-coalesce-without-revoking-public-source");Sleep(40);
    need(h.plans==1&&steps==0&&h.runtime.snapshot().phase=="frontend-attached","first-proof-pending-does-not-recycle-or-rebuild");
    release=true;need(waitFor([&]{return shared.snapshot(actual::observedSession).value("deckyClientVerified",false);}),"first-proof-current-native-reply-reaches-shared-reader");
    need(h.runtime.requestConnectionCheck(),"first-proof-original-health-check-wakes");Sleep(40);
    need(h.plans==1&&steps==0&&h.runtime.snapshot().phase=="frontend-attached","first-proof-native-confirmation-needs-no-recovery-step");
    need(sourceLease.current(),"same-association-native-proof-keeps-original-source-lease");    // The already attached peer does not reauthenticate on each health read.
    ULONGLONG verifiedAt=0;
    need(shared.snapshot(actual::observedSession,nullptr,nullptr,nullptr,&verifiedAt).value("deckyClientVerified",false)&&verifiedAt,
        "first-proof-fault-fixture-has-a-verified-observation-tick");
    {std::lock_guard lock(gate);actual::g_deckyMirrorBroker.pinnedAuthTick=verifiedAt-1;}
    throwHealth=true;
    need(h.runtime.requestConnectionCheck()&&waitFor([&]{return loopsAfterHealthFault.load()>0;},1000),"first-proof-health-exception-finished-on-real-worker");
    const auto afterFault=GetTickCount64();const auto afterFaultReads=healthReads.load(),afterFaultLoops=loopsAfterHealthFault.load();
    const bool publicationAccepted=h.runtime.requestConnectionCheck(true);
    const bool publicationRead=publicationAccepted&&waitFor([&]{return healthReads>afterFaultReads&&loopsAfterHealthFault>afterFaultLoops;},1000);
    const bool recovered=publicationRead&&lastHealthConnected;
    const bool healthyIdle=!h.runtime.requestConnectionCheck(true);
    {std::lock_guard lock(gate);actual::g_deckyMirrorBroker.pinnedAuthTick.reset();}
    Json healthPublication={{"accepted",publicationAccepted},{"read",publicationRead},{"connected",recovered},{"healthyIdle",healthyIdle},{"waitMs",GetTickCount64()-afterFault},
        {"reads",healthReads.load()-afterFaultReads},{"steps",steps.load()}};
    std::cout<<"HEALTH_PUBLICATION_AFTER_EXCEPTION "<<healthPublication.dump()<<std::endl;
    need(recovered&&healthyIdle&&healthReads==afterFaultReads+1&&h.plans==1&&steps==0&&sourceLease.current(),
        "first-proof-stable-publication-must-wake-health-once-after-exception");

    // Exercise a plain renewal after the hint burst has settled. The burst
    // itself invalidates its in-flight read before it can publish anything.
    std::uint64_t initialPublication=0,latePublication=0;
    unsigned long long initialPeer=0,latePeer=0;
    ULONGLONG lateReturnedAt=0;
    (void)actual::Modes::readSnapshot(&shared,actual::observedSession,nullptr,&initialPublication,nullptr,&initialPeer);
    holdNextPeerRead=true;
    need(shared.requestRefresh(actual::observedSession,false)&&waitFor([&]{return peerReadEntered.load();}),"replacement-peer-has-a-read-in-flight");
    const auto replacementPeerAt=GetTickCount64();
    {std::lock_guard lock(gate);actual::g_deckyMirrorBroker.pinnedAuthTick=replacementPeerAt;++actual::g_deckyMirrorBroker.authRevision;}
    releasePeerRead=true;
    need(waitFor([&]{
        (void)actual::Modes::readSnapshot(&shared,actual::observedSession,nullptr,&latePublication,&lateReturnedAt,&latePeer);
        return latePublication>initialPublication;
    }),"replacement-peer-old-in-flight-result-publishes");
    auto peerHealth=actual::actualConnectionHealth();ymcc::deckysidebar::ConnectionHealth crossed;
    {std::lock_guard lock(gate);crossed=peerHealth();}
    std::cout<<"PEER_REPLACED_DURING_MODE_READ "<<Json{{"paused",crossed.paused},{"connected",crossed.connected},{"reason",crossed.reason},
        {"publication",latePublication},{"readPeerRevision",latePeer},{"currentPeerRevision",actual::g_deckyMirrorBroker.authRevision.load()},
        {"returnedAfterAuthentication",lateReturnedAt>=replacementPeerAt}}.dump()<<std::endl;
    need(initialPeer&&latePeer==initialPeer&&lateReturnedAt>=replacementPeerAt,"replacement-peer-test-published-the-old-read-not-its-successor");
    need(crossed.paused&&!crossed.connected&&crossed.reason=="frontend-client-proof-stale","first-proof-older-in-flight-result-cannot-confirm-replacement-peer");
    need(shared.requestRefresh(actual::observedSession,false)&&waitFor([&]{
        std::uint64_t publication=0;unsigned long long peer=0;
        (void)actual::Modes::readSnapshot(&shared,actual::observedSession,nullptr,&publication,nullptr,&peer);
        return publication>latePublication&&peer==actual::g_deckyMirrorBroker.authRevision.load();
    }),"replacement-peer-gets-fresh-shared-read");
    {std::lock_guard lock(gate);const auto fresh=peerHealth();
        need(fresh.connected&&!fresh.paused&&h.plans==1&&steps==0,"fresh-read-confirms-replacement-peer-without-recovery-action");
        actual::g_deckyMirrorBroker.pinnedAuthTick.reset();}

    Json contentionRounds=Json::array();
    if(runRetainedLocalBusyProbe){
        {std::lock_guard lock(gate);actual::brokerStops=0;}
        const auto originalBinding=h.client->snapshot().processId;readPhase=2;
        need(shared.requestRefresh(actual::observedSession,false),"composition-original-shared-refresh-accepts-local-busy-observation");
        need(waitFor([&]{return shared.snapshot(actual::observedSession).value("reason",std::string{})=="live-busy";}),"composition-local-busy-reaches-real-shared-cache");
        for(unsigned round=0;round<5;++round){
            need(h.runtime.requestConnectionCheck(),"composition-busy-check-accepted");Sleep(50);
            const auto snapshot=h.runtime.snapshot();
            bool broker;unsigned stopped;
            {std::lock_guard lock(gate);broker=actual::g_deckyMirrorBroker.connected;stopped=actual::brokerStops;}
            const bool leaseCurrent=sourceLease.current();
            std::cout<<"SHARED_BUSY_COMPOSITION "<<Json{{"round",round},{"publicSourceCurrent",leaseCurrent},{"brokerConnected",broker},{"brokerStops",stopped},{"phase",snapshot.phase},{"plans",h.plans.load()},{"steps",steps.load()}}.dump()<<std::endl;
            need(leaseCurrent&&broker&&stopped==0&&snapshot.phase=="frontend-attached"&&h.plans==1&&steps==0&&h.client->snapshot().processId==originalBinding,
                "actual-shared-busy-must-preserve-existing-binding-and-broker");
            contentionRounds.push_back({{"round",round},{"publicSourceCurrent",leaseCurrent},{"brokerStops",stopped},{"plans",h.plans.load()}});
        }
        readPhase=0;need(shared.requestRefresh(actual::observedSession,false),"composition-normal-shared-read-resumes");
        need(waitFor([&]{return shared.snapshot(actual::observedSession).value("deckyClientVerified",false);}),"composition-fresh-proof-returns-without-new-binding");
        need(h.runtime.requestConnectionCheck(),"composition-fresh-health-check-accepted");Sleep(30);
        need(sourceLease.current()&&h.plans==1&&steps==0,"composition-current-proof-keeps-original-public-source-and-binding");
    }
    auto foreign=actual::observedSession;++foreign.pid;const auto beforeForeign=reads.load();
    need(!actual::g_steamModeObserver.requestConnectionRefresh(foreign)&&reads==beforeForeign,"connection-refresh-rejects-foreign-session-before-read");
    need(shared.requestRefresh(actual::observedSession),"original-normal-invalidation-still-accepted");
    need(!sourceLease.current(),"original-normal-invalidation-still-revokes-source");
    need(waitFor([&]{return shared.snapshot(actual::observedSession).value("deckyClientVerified",false);}),"original-normal-refresh-returns-current-facts");
    auto failureLease=shared.observedSource(actual::observedSession);need(failureLease.current(),"failure-control-holds-new-verified-source");
    release=false;readPhase=1;need(actual::g_steamModeObserver.requestConnectionRefresh(actual::observedSession),"connection-failure-control-starts-original-read");
    need(failureLease.current(),"pending-connection-failure-does-not-prejudge-source");
    need(!shared.snapshot(actual::observedSession).contains("deckyClientVerified"),"connection-refresh-removes-stale-positive-proof-before-reader");release=true;
    need(waitFor([&]{return !failureLease.current();}),"completed-invalid-shared-read-still-revokes-source");
    shared.setDemand(ymcc::steamui::ModeConsumer::Css,true);
    const auto jointCpu=shared.diagnosticDeckyWorkerCpu();
    need(jointCpu&&jointCpu->id==sharedCpu->id&&jointCpu->birth==sharedCpu->birth&&jointCpu->cssDemand&&
        jointCpu->kernel>=sharedCpu->kernel&&jointCpu->user>=sharedCpu->user,"shared-mode-cpu-labels-joint-demand");
    h.runtime.setEnabled(false);h.runtime.shutdown();const auto returned=h.client->snapshot();
    need(!shared.diagnosticDeckyWorkerCpu(),"CSS-only-worker-not-sampled-as-Decky-demand");
    need(!h.runtime.requestConnectionCheck(true),"OFF-publication-cannot-wake-retired-health-intent");
    need(!actual::g_steamModeObserver.requestConnectionRefresh(actual::observedSession),"OFF-connection-refresh-cannot-start-new-read-even-while-CSS-remains-ON");
    need(shared.requestRefresh(actual::observedSession,false),"ordinary-CSS-refresh-remains-available-while-Decky-is-OFF");
    shared.setDemand(ymcc::steamui::ModeConsumer::Css,false);shared.shutdown();actual::realModeObserver=nullptr;sourceLease={};failureLease={};
    need(!shared.diagnosticDeckyWorkerCpu(),"joined-shared-mode-worker-not-sampled");
    Json wakeProbeSlots=Json::array();bool wakeProbeSlotsPassed=true;
    if(!runRetainedLocalBusyProbe){
        for(const auto* control:{"consume","off","power","reopen-in-flight","power-css-in-flight"}){
            auto row=wakeProbeReadSlot(actual::observedSession,old,control);
            wakeProbeSlotsPassed&=row.value("passed",false);wakeProbeSlots.push_back(std::move(row));
        }
        std::cout<<"WAKE_PROBE_READ_SLOTS "<<wakeProbeSlots.dump()<<std::endl;
    }
    need(returned.fullyDestroyed&&returned.handles==0&&returned.pendingIo==0&&before==handles()&&context.alive(),"first-proof-all-resources-return-and-external-context-lives");
    const auto offlineProof=runRetainedLocalBusyProbe?Json{}:offlineRebuiltOwnedProof(output/"offline-rebuilt",context,good);
    return {{"passed",wakeProbeSlotsPassed&&(offlineProof.is_null()||offlineProof.value("passed",false))},{"actualRuntime",true},{"actualSharedModeObserver",true},{"actualChangedPrefix",true},{"actualHealth",true},
        {"wakeProbeSlots",wakeProbeSlots},
        {"sharedModeWorkerCounters",workerCounters},
        {"sharedModeWorkerCpu",{{"threadId",jointCpu->id},{"threadBirth100ns",std::to_string(jointCpu->birth)},
            {"kernel100ns",jointCpu->kernel},{"user100ns",jointCpu->user},{"cssDemandAtSample",jointCpu->cssDemand},
            {"extraReads",0},{"samplesWithoutRead",16}}},
        {"connectionReadQueueRows",queueRows},
        {"healthPublicationAfterException",healthPublication},
        {"offlineRebuiltPageProof",offlineProof},
        {"sharedLocalBusy",runRetainedLocalBusyProbe},{"contentionRounds",contentionRounds},{"modeReads",reads.load()},{"publicSourcePreservedWhilePending",true},{"ordinaryInvalidateStillRevokes",true},{"failedReadStillRevokes",true},{"bindingPlans",h.plans.load()},{"recoverySteps",steps.load()},{"beforeHandles",before},{"afterHandles",handles()}};
}
inline Json retainedRefresh(const std::filesystem::path& output){
    namespace actual=offlineConsumer;
    Controls controls(L"burst");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    const auto before=handles();std::mutex gate;std::atomic<unsigned> admissionReads{0};
    const auto base=makeRequest(51);actual::observedSession={worker::wide(base["steam"]["root"]),GetCurrentProcessId(),0,worker::decimal(base["steam"]["created"])};
    const Json good={{"known",true},{"running",true},{"mode","bigpicture"},{"pid",actual::observedSession.pid},
        {"birth",std::to_string(actual::observedSession.created)},{"observedAccount",12345678u},{"deckyAccountWatchAvailable",true},
        {"deckyFrontendLoaded",true},{"deckyBackendConnected",true},{"deckyHooksInstalled",true},{"deckyClientConnected",true},{"deckyClientVerified",true},{"reason",runRetainedBusyProbe?Json::object():Json("")}};
    actual::observedMode=good;actual::retainedConsent=true;actual::g_deckyMirrorBroker.connected=true;actual::brokerStops=0;
    std::atomic<bool> preparationEntered{false},preparationReleased{false};
    Json trace=Json::array();auto admission=actual::actualAdmission();
    Harness h(output,context,true,[&]{std::lock_guard lock(gate);++admissionReads;const auto result=admission();trace.push_back({{"stage","admission"},{"allowed",result.allowed},{"reason",result.reason},{"detail",result.detail},{"mode",actual::observedMode}});return result;},[&](Json&,unsigned){std::lock_guard lock(gate);actual::g_deckyMirrorBroker.connected=true;},{},{},{},{},{},[&](const std::function<bool()>& current){preparationEntered=true;const auto end=GetTickCount64()+2500;while(!preparationReleased.load()&&current()&&GetTickCount64()<end)Sleep(1);need(preparationReleased.load()||!current(),"bounded-preparation-control");return std::string{};},
        [&](const auto& state){std::lock_guard lock(gate);actual::actualRuntimeChanged(state);});
    actual::runtime=&h.runtime;need(h.runtime.setEnabled(true),"retained-refresh-enable");
    need(waitFor([&]{return h.runtime.snapshot().phase=="frontend-attached"&&h.burstDone.load();}),"retained-refresh-initial-current-document-attached");
    {std::lock_guard lock(gate);need(actual::g_deckyMirrorBroker.connected,"retained-refresh-initial-broker-live");actual::brokerStops=0;}
    const auto plans=h.plans.load();Json observations=Json::array();
    const char* pendingReason=runRetainedBusyProbe?"live-busy":"shared-mode-pending";
    for(unsigned round=0;round<5;++round){
        unsigned at;
        {std::lock_guard lock(gate);at=admissionReads;actual::observedMode={{"known",true},{"running",true},{"mode","unknown"},
            {"pid",actual::observedSession.pid},{"birth",std::to_string(actual::observedSession.created)},{"reason",pendingReason}};}
        // Execute the exact qam-open owner callback extracted from main.cpp.
        actual::deckySidebarOwnedBindingDiagnostic({{"bootstrapStage","connection-check"},{"reason","qam-open"},{"runId",actual::g_deckyMirrorBroker.bootstrap()["runId"]}});
        need(waitFor([&]{return admissionReads>at;}),"qam-refresh-actually-reaches-current-admission-consumer");Sleep(30);
        const auto state=h.runtime.snapshot();
        {std::lock_guard lock(gate);
            if(runRetainedBusyProbe)std::cout<<"RETAINED_BUSY_OBSERVATION "<<Json{{"round",round},{"reason",pendingReason},{"phase",state.phase},{"admissionDetail",state.admissionDetail},{"brokerConnected",actual::g_deckyMirrorBroker.connected},{"brokerStops",actual::brokerStops},{"registryAccount",actual::observedSession.account}}.dump()<<std::endl;
            need(actual::g_deckyMirrorBroker.connected&&actual::brokerStops==0&&state.phase=="frontend-attached",
            "retained-connection-must-survive-shared-mode-refresh");actual::observedMode=good;}
        need(h.plans==plans,"pending-refresh-must-not-create-another-binding");
        need(h.runtime.requestConnectionCheck(),"same-owner-fresh-read-completion-observed");
        observations.push_back({{"round",round},{"reason",pendingReason},{"phase",state.phase},{"bindingPlans",h.plans.load()},{"brokerStops",0}});
    }
    // An explicit known desktop still revokes the old connection normally.
    {std::lock_guard lock(gate);actual::observedMode=good;actual::observedMode["mode"]="desktop";}
    need(h.runtime.requestConnectionCheck(),"known-desktop-wake");
    need(waitFor([&]{return h.runtime.snapshot().reason=="steam-bigpicture-required";}),"known-desktop-still-parks-retained-owner");
    {std::lock_guard lock(gate);need(!actual::g_deckyMirrorBroker.connected&&actual::brokerStops>0,"known-desktop-still-stops-broker");actual::observedMode=good;}
    need(waitFor([&]{return preparationEntered.load();}),"real-resource-preparation-holds-original-owner");
    need(h.runtime.requestLaunchAdmissionRefresh(),"genuine-bigpicture-read-restores-admission");preparationReleased=true;
    try{h.waitForCheckpoint([&]{return h.runtime.snapshot().phase=="frontend-attached";},"verified-bigpicture-can-rebind-again");}catch(...){std::lock_guard lock(gate);std::cout<<"RETAINED_REFRESH_TRACE "<<trace.dump()<<std::endl;throw;}
    if(runRetainedBusyProbe){
        // A genuine failed read must still stop the retained broker normally.
        {std::lock_guard lock(gate);actual::observedMode=good;actual::observedMode["mode"]="unknown";actual::observedMode["reason"]="inert-read-failure";actual::brokerStops=0;}
        need(h.runtime.requestConnectionCheck(),"failed-read-check-accepted");
        need(waitFor([&]{return h.runtime.snapshot().admissionDetail=="inert-read-failure";}),"actual-read-failure-still-parks-retained-owner");
        {std::lock_guard lock(gate);need(!actual::g_deckyMirrorBroker.connected&&actual::brokerStops>0,"actual-read-failure-still-stops-broker");actual::observedMode=good;}
        need(h.runtime.requestLaunchAdmissionRefresh(),"fresh-valid-mode-after-read-failure");
        h.waitForCheckpoint([&]{return h.runtime.snapshot().phase=="frontend-attached";},"verified-mode-after-failure-rebinds");
    }
    {std::lock_guard lock(gate);actual::g_deckyMirrorBroker.connected=true;actual::observedMode={{"known",true},{"running",true},{"mode","unknown"},{"reason",pendingReason}};}
    h.runtime.requestConnectionCheck();Sleep(30);h.runtime.setEnabled(false);h.runtime.shutdown();
    const auto state=h.client->snapshot();actual::runtime=nullptr;actual::retainedConsent=false;
    need(!actual::g_deckyMirrorBroker.connected&&!h.runtime.snapshot().enabled,"explicit-off-during-pending-still-stops-broker");
    need(state.fullyDestroyed&&before==handles()&&context.alive(),"retained-refresh-returns-owned-resources-and-keeps-external-context");
    return {{"passed",true},{"variant",runRetainedBusyProbe?"retained-live-busy":"retained-shared-refresh"},{"failedReadStillStops",runRetainedBusyProbe},{"rounds",observations},{"actualRuntime",true},{"actualQamOwnerCallback",true},
        {"actualBrokerCleanupGuard",true},{"transportBoundary","inert connected-state port; not real Winsock or Steam"},
        {"initialBindingPlans",plans},{"finalBindingPlans",h.plans.load()},{"beforeHandles",before},{"afterHandles",handles()},
        {"explicitOff",true},{"knownDesktop",true},{"bindingDestroyed",state.fullyDestroyed},{"externalContextAlive",context.alive()}};
}
inline Json presencePublication(const std::filesystem::path& output){
    namespace actual=offlineConsumer;
    Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    const auto before=handles();
    actual::observedSession={L"inert-same-install",0,0,0};actual::cachedIdentityPublication=true;
    actual::g_steamSettingsObserver.publisher=std::make_unique<ymcc::steamsession::Observer>();
    auto publish=[&](int presence){
        ymcc::steamsession::IdentitySnapshot facts;facts.session=actual::observedSession;facts.presence=presence;
        facts.rootReadError=facts.registeredPidReadError=facts.accountReadError=ERROR_SUCCESS;
        ymcc::steamsession::IdentityPublicationFixture::publish(*actual::g_steamSettingsObserver.publisher,facts);
        return actual::g_steamSettingsObserver.publisher->identitySnapshot();
    };
    const auto original=publish(1);Json checkpoints=Json::array();
    Harness h(output,context);actual::runtime=&h.runtime;
    h.runtime.observeSteam(1,actual::observedSession);h.start();
    need(waitFor([&]{return h.healthyGeneration==1;}),"presence-initial-owned-binding-healthy");
    const auto cssBefore=actual::g_steamCssUse.notices;
    auto callback=actual::actualIdentityPublished();callback(original);
    need(actual::g_steamCssUse.notices==cssBefore+1&&actual::g_steamCssUse.last==original.get(),"current-shared-identity-reaches-existing-css-use-notification-port");
    const auto initialPlans=h.plans.load();
    detail::Handle firstLoader{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,h.runtime.snapshot().pid)};
    need(firstLoader.value&&WaitForSingleObject(firstLoader.value,0)==WAIT_TIMEOUT,"presence-observe-owned-loader");
    const auto absent=publish(0);need(absent->session==original->session&&absent->presence==0,"same-session-presence-only-publication");
    callback(absent);
    h.waitForCheckpoint([&]{const auto s=h.runtime.snapshot();return s.enabled&&s.steamKnown&&!s.steamRunning&&s.pid==0&&s.phase=="waiting-steam";},"presence-only-publication-must-stop-owned-loader");
    need(WaitForSingleObject(firstLoader.value,0)==WAIT_OBJECT_0,"presence-exit-returns-owned-loader");
    CloseHandle(firstLoader.value);firstLoader.value=nullptr;
    const auto stopped=h.runtime.snapshot();
    for(unsigned i=0;i<256;++i){callback(absent);callback(original);}
    Sleep(40);
    need(actual::g_steamCssUse.notices==cssBefore+258&&actual::g_steamCssUse.last==absent.get(),"stale-identity-never-reaches-css-use-notification-port");
    need(h.runtime.snapshot().revision==stopped.revision&&!h.runtime.snapshot().steamRunning&&h.plans==initialPlans,"duplicate-or-stale-publication-revives-or-revokes-owner");
    checkpoints.push_back({{"presence",0},{"phase",stopped.phase},{"ownedLoaderReturned",true},{"duplicateAndStaleDeliveries",512}});
    const auto unknown=publish(-1);callback(unknown);
    h.waitForCheckpoint([&]{return !h.runtime.snapshot().steamKnown&&h.runtime.snapshot().pid==0;},"unknown-presence-remains-unknown");
    callback({});need(!h.runtime.snapshot().steamKnown,"null-publication-cannot-fabricate-absence-or-presence");
    const auto returned=publish(1);callback(returned);
    h.waitForCheckpoint([&]{return h.runtime.snapshot().pid!=0&&h.healthyGeneration>=2;},"current-presence-return-resumes-original-owner");
    const auto returnPlans=h.plans.load();
    const auto failedDelivery=publish(0);actual::metadataAccepted=false;bool threw=false;
    try{callback(failedDelivery);}catch(const std::runtime_error& error){threw=std::string(error.what())=="steam-source-notification-unavailable";}
    need(threw,"metadata-rejection-keeps-original-observer-retry-contract");
    h.waitForCheckpoint([&]{return h.runtime.snapshot().steamKnown&&!h.runtime.snapshot().steamRunning&&h.runtime.snapshot().pid==0;},"presence-delivery-survives-independent-metadata-enqueue-rejection");
    actual::metadataAccepted=true;const auto retryRevision=h.runtime.snapshot().revision;callback(failedDelivery);Sleep(40);
    need(h.runtime.snapshot().revision==retryRevision&&h.plans==returnPlans,"metadata-retry-must-not-refund-or-restart-runtime");
    checkpoints.push_back({{"presence",-1},{"remainedUnknown",true},{"currentReturnWorked",true},{"metadataFailurePreserved",true}});
    need(h.runtime.setEnabled(false),"presence-explicit-off");const auto offPlans=h.plans.load();
    callback(publish(1));callback(publish(-1));callback(publish(1));Sleep(40);
    need(!h.runtime.snapshot().enabled&&h.runtime.snapshot().pid==0&&h.plans==offPlans,"late-current-publications-cannot-start-after-explicit-off");
    h.runtime.shutdown();const auto state=h.client->snapshot();actual::runtime=nullptr;
    const auto cssNotices=actual::g_steamCssUse.notices-cssBefore;actual::g_steamCssUse.last=nullptr;
    need(cssNotices==265,"all-current-identity-callbacks-notify-css-including-metadata-refusal-and-off");
    actual::g_steamSettingsObserver.publisher.reset();actual::cachedIdentityPublication=false;
    need(state.fullyDestroyed&&state.handles==0&&state.pendingIo==0&&state.jobActiveProcesses==0&&before==handles()&&context.alive(),"presence-publication-returns-owned-resources-and-keeps-external-context");
    return {{"passed",true},{"variant","same-session-presence-publication"},{"actualRuntime",true},{"actualMainIdentityCallback",true},
        {"actualSharedPublication",true},{"sameSessionFields",true},{"checkpoints",checkpoints},{"initialPlans",initialPlans},{"finalPlans",h.plans.load()},
        {"cssNotificationMemoryLeaf",true},{"cssNotificationCalls",cssNotices},{"staleCssNotifications",0},
        {"metadataPosts",actual::metadataPosts},{"beforeHandles",before},{"afterHandles",handles()},{"bindingDestroyed",state.fullyDestroyed},
        {"externalContextAlive",context.alive()},{"realSteamActions",0},{"defaultSuiteRun",false}};
}
inline Json offlineIdentity(const std::filesystem::path& output){
    if(runPresencePublicationProbe)return presencePublication(output);
    if(runRetainedFirstProofProbe)return retainedFirstProof(output);
    if(runRetainedRefreshProbe)return retainedRefresh(output);
    Controls controls(L"burst");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
    Harness h(output,context);const auto before=handles();h.start();
    need(waitFor([&]{return h.burstDone.load();}),"offline-inert-watch-live");
    namespace actual=offlineConsumer;actual::runtime=&h.runtime;
    const auto base=makeRequest(51);actual::observedSession={worker::wide(base["steam"]["root"]),GetCurrentProcessId(),0,worker::decimal(base["steam"]["created"])};
    actual::observedMode={{"known",true},{"running",true},{"mode","bigpicture"},{"pid",actual::observedSession.pid},
        {"birth",std::to_string(actual::observedSession.created)},{"observedAccount",12345678u},{"deckyAccountWatchAvailable",true},{"deckyFrontendLoaded",false}};
    need(ProcessIdToSessionId(GetCurrentProcessId(),&actual::observedWindowsSession)!=FALSE,"shared-receipt-setup-session");
    actual::identityVariant=0;actual::identityRefreshes=actual::bindingWindowsQueries=0;
    Json openingCadenceEvidence,refreshAdmissionCounts=Json::array(),sessionRace=Json::array();
    {
        using Window=ymcc::deckysidebar::ConnectionRecoveryWindow;
        using Action=Window::Action;
        const auto originalSession=actual::observedSession;
        const auto originalMode=actual::observedMode;
        const bool originalMirror=actual::g_deckyMirrorBroker.connected;
        auto known=originalSession;known.account=12345678u;
        auto foreignAccount=known;foreignAccount.account=23456789u;
        auto nextProcess=originalSession;++nextProcess.created;
        const auto trial=[&](const char* label,const auto& before,const auto& after,bool transient){
            constexpr ULONGLONG start=700000;
            actual::observedSession=before;actual::observedMode=originalMode;
            actual::g_deckyMirrorBroker.connected=false;
            auto health=actual::actualConnectionHealth();Window window;
            actual::fixedTick=start;
            need(window.observe(start,health())==Action::reconnect,"publication-race-original-reconnect-starts");
            const auto episode=window.episode();
            const auto at=actual::refreshes;
            // Publish new identity at the end of the old mode read; withhold the
            // Decky callback to reproduce the publication/intent ordering.
            actual::g_steamModeObserver.publishBeforeSnapshotReturns=after;
            const auto gap=health();
            const auto action=window.observe(start+1,gap);
            std::cout<<"IDENTITY_PUBLICATION_RACE "<<label<<" paused="<<gap.paused<<" stopped="<<(action==Action::stopped)<<std::endl;
            need(!gap.required&&!gap.connected&&gap.identity.empty()&&actual::refreshes==at,
                "superseded-mode-is-never-consumed-or-refreshed");
            need(gap.paused==transient&&action==(transient?Action::none:Action::stopped),
                "only-same-process-account-gap-parks-the-original-episode");
            if(transient){
                actual::fixedTick=start+Window::transportGraceMs-1;
                need(window.observe(actual::fixedTick,health())==Action::none&&window.attempts()==1&&window.episode()==episode,
                    "account-publication-gap-does-not-reset-or-shorten-original-rung");
                ++actual::fixedTick;
                need(window.observe(actual::fixedTick,health())==Action::recycle&&window.attempts()==2&&window.episode()==episode,
                    "current-health-continues-original-rung-at-original-deadline");
            }
            sessionRace.push_back({{"case",label},{"paused",gap.paused},{"stopped",action==Action::stopped},
                {"extraRefreshesOnGap",0},{"attempts",window.attempts()}});
        };
        trial("same-process-account-lost",known,originalSession,true);
        trial("same-process-account-returned",originalSession,known,true);
        trial("same-process-known-account-switch",known,foreignAccount,false);
        trial("same-pid-new-birth-account-missing",known,nextProcess,false);
        actual::observedSession=originalSession;
        actual::observedMode=originalMode;actual::g_deckyMirrorBroker.connected=originalMirror;actual::fixedTick=0;
    }
    {
        const auto originalMode=actual::observedMode;const bool originalMirror=actual::g_deckyMirrorBroker.connected;
        actual::g_deckyMirrorBroker.connected=true;
        for(const auto* field:{"deckyBackendConnected","deckyHooksInstalled","deckyClientConnected","deckyClientChecking"})actual::observedMode[field]=true;
        auto health=actual::actualConnectionHealth();const auto at=actual::refreshes;
        actual::fixedTick=800000;const auto first=health();
        need(first.required&&first.paused&&!first.connected,"actual-current-opening-check-parks-without-claiming-connected");
        using Window=ymcc::deckysidebar::ConnectionRecoveryWindow;Window window;
        need(window.observe(800000,first)==Window::Action::none,"current-opening-check-issues-no-recovery-action");
        actual::fixedTick=804999;health();need(actual::refreshes==at+1,"opening-health-does-not-read-before-original-evidence-gap");
        actual::fixedTick=805000;const auto stillPending=health();
        const auto pendingReads=actual::refreshes-at;
        std::cout<<"OPENING_HEALTH_CADENCE checking=1 cachedClient=1 elapsed=5000 refreshes="<<pendingReads<<'\n';
        need(pendingReads==2,"confirmed-opening-check-must-refresh-within-original-five-second-evidence-gap");
        need(stillPending.paused&&window.observe(805000,stillPending)==Window::Action::none,"refreshing-opening-evidence-never-restarts-an-unexpired-probe");
        for(unsigned repeat=0;repeat<1000;++repeat)health();need(actual::refreshes==at+2,"duplicate-opening-observations-do-not-create-extra-source-reads");
        actual::fixedTick=810000;health();need(actual::refreshes==at+3,"pending-opening-retains-the-original-capped-evidence-cadence");
        // These two flags are the actual frontend getter result after its
        // existing 15s bound, even if the browser timeout callback is delayed.
        actual::observedMode["deckyClientChecking"]=false;actual::observedMode["deckyClientConnected"]=false;
        actual::fixedTick=815000;const auto expired=health();
        need(expired.required&&!expired.paused&&!expired.connected&&expired.reason=="frontend-client-disconnected","expired-opening-getter-facts-reopen-the-existing-native-recovery");
        need(window.observe(815000,expired)==Window::Action::recycle,"expired-opening-reuses-the-existing-loader-rung");
        actual::observedMode["deckyClientConnected"]=true;actual::fixedTick=815001;const auto replied=health();
        need(replied.connected&&window.observe(815001,replied)==Window::Action::recovered,"late-current-correlated-reply-still-finishes-the-existing-episode");
        auto idle=actual::actualConnectionHealth();const auto idleAt=actual::refreshes;
        actual::fixedTick=900000;idle();actual::fixedTick=929999;idle();need(actual::refreshes==idleAt+1,"healthy-idle-has-no-extra-opening-probes");
        actual::fixedTick=930000;idle();need(actual::refreshes==idleAt+2,"healthy-idle-preserves-thirty-second-original-cadence");
        auto retried=actual::actualConnectionHealth();const auto retryAt=actual::refreshes;
        actual::g_steamModeObserver.refusePassiveRefresh=true;
        actual::fixedTick=940000;need(retried().connected,"read-refusal-does-not-change-current-health");
        refreshAdmissionCounts.push_back(actual::refreshes-retryAt);
        actual::g_steamModeObserver.refusePassiveRefresh=false;
        for(const auto tick:{940001ULL,970000ULL,970001ULL}){
            actual::fixedTick=tick;need(retried().connected,"accepted-read-does-not-change-current-health");
            refreshAdmissionCounts.push_back(actual::refreshes-retryAt);
        }
        actual::observedMode["mode"]="unknown";actual::observedMode["deckyClientChecking"]=true;
        auto unknown=actual::actualConnectionHealth();const auto unknownAt=actual::refreshes;
        for(ULONGLONG tick=1000000;tick<=1025000;tick+=1000){actual::fixedTick=tick;const auto value=unknown();need(!value.required&&value.paused,"unconfirmed-mode-keeps-the-original-admission-boundary");}
        need(actual::refreshes==unknownAt+1,"unknown-checking-flag-cannot-create-a-five-second-source-loop");
        actual::observedMode["mode"]="desktop";auto desktop=actual::actualConnectionHealth();const auto desktopAt=actual::refreshes;
        for(ULONGLONG tick=1100000;tick<=1160000;tick+=5000){actual::fixedTick=tick;desktop();}
        need(actual::refreshes==desktopAt,"desktop-checking-flag-has-zero-health-source-reads");
        openingCadenceEvidence={{"pendingReadsAtFiveSeconds",pendingReads},{"duplicateReads",0},{"healthyOriginalIntervalMs",30000},{"unknownRequestsIn25s",actual::refreshes-unknownAt},{"desktopRequests",actual::refreshes-desktopAt},{"expiredOpeningUsesOriginalRecycle",true},{"realClockElapsedMeasured",false}};
        actual::observedMode=originalMode;actual::g_deckyMirrorBroker.connected=originalMirror;actual::fixedTick=0;
    }

    const auto admission=actual::actualAdmission();need(admission().allowed,"actual-offline-main-admission");
    Json canceledIntent;
    {
        using namespace ymcc::deckysidebar;
        static std::atomic<bool> frontendRead{false};
        bool offObserved=false;
        BindingPlan retired;
        Runtime probe;Config config;config.home=output.wstring();config.executable=detail::executable();
        config.ownedBinding=std::make_shared<Client>();const auto client=config.ownedBinding;
        config.preflight=[] {return std::string{};};
        config.bindingPlan=[&](BindingMode mode,std::uint64_t generation,const auto& current){
            ymcc::deckymirror::fixtureAfterFrontendRead=[] {
                frontendRead=true;
                waitFor([]{return !actual::runtime->snapshot().enabled;},3000);
            };
            retired=actual::deckySidebarOwnedBindingPlan(mode,generation,current);
            offObserved=!probe.snapshot().enabled;
            ymcc::deckymirror::fixtureAfterFrontendRead=nullptr;
            return retired;
        };
        const auto originalPeer=actual::g_deckyMirrorBroker.connected;
        actual::g_deckyMirrorBroker.connected=true;
        const auto stops=actual::brokerStops,starts=actual::brokerStarts;
        const auto generation=actual::currentPowerGeneration(),serial=actual::fixtureSuspendSerial.load();
        const auto proof=handles();frontendRead=false;actual::runtime=&probe;
        probe.initialize(std::move(config));probe.observeSteam(1,true);
        const auto enabled=probe.setEnabled(true);
        const auto entered=enabled&&waitFor([]{return frontendRead.load();});
        const auto stopped=probe.setEnabled(false);actual::runtime=&h.runtime;
        const auto after=client->snapshot();
        canceledIntent={{"offObserved",offObserved},{"brokerStops",actual::brokerStops-stops},{"brokerStarts",actual::brokerStarts-starts},
            {"connectedBeforeOwnerCleanup",actual::g_deckyMirrorBroker.connected},{"powerUnchanged",actual::currentPowerGeneration()==generation&&actual::fixtureSuspendSerial==serial},
            {"reason",retired.reason},{"childReturned",after.fullyDestroyed&&after.handles==0&&after.pendingIo==0&&after.jobActiveProcesses==0}};
        std::cout<<"BINDING_OFF_INTENT "<<canceledIntent.dump()<<std::endl;
        need(entered&&stopped&&offObserved&&retired.request.empty()&&retired.reason=="mirror-bootstrap-canceled"&&
            canceledIntent.at("powerUnchanged")==true&&canceledIntent.at("childReturned")==true&&
            actual::g_deckyMirrorBroker.connected&&actual::brokerStops==stops&&actual::brokerStarts==starts&&proof==handles(),
            "off-intent-revokes-binding-before-broker-replacement");
        actual::g_deckyMirrorBroker.connected=originalPeer;
    }
    const auto stopsBeforeResleep=actual::brokerStops,startsBeforeResleep=actual::brokerStarts;
    const auto generationBeforeResleep=actual::currentPowerGeneration();
    const bool connectedBeforeResleep=actual::g_deckyMirrorBroker.connected;
    actual::g_deckyMirrorBroker.connected=true;
    actual::fixtureSuspendSerial=0;
    ymcc::deckymirror::fixtureAfterFrontendRead=[] { ++actual::fixtureSuspendSerial; };
    const auto revoked=actual::bindingPlanForCurrentIntent(ymcc::deckysidebar::BindingMode::refresh,50);
    ymcc::deckymirror::fixtureAfterFrontendRead=nullptr;
    const Json canceledBinding={{"sameGeneration",actual::currentPowerGeneration()==generationBeforeResleep},
        {"suspendSerial",actual::fixtureSuspendSerial.load()},{"brokerStops",actual::brokerStops-stopsBeforeResleep},
        {"brokerStarts",actual::brokerStarts-startsBeforeResleep},{"connected",actual::g_deckyMirrorBroker.connected},
        {"reason",revoked.reason}};
    std::cout<<"BINDING_RESLEEP "<<canceledBinding.dump()<<std::endl;
    need(actual::currentPowerGeneration()==generationBeforeResleep&&actual::fixtureSuspendSerial==1&&
        actual::g_deckyMirrorBroker.connected&&actual::brokerStops==stopsBeforeResleep&&
        actual::brokerStarts==startsBeforeResleep&&revoked.request.empty()&&revoked.reason=="mirror-binding-power-deferred",
        "same-generation-resleep-revokes-binding-before-broker-replacement");
    actual::g_deckyMirrorBroker.connected=connectedBeforeResleep;
    auto plan=actual::bindingPlanForCurrentIntent(ymcc::deckysidebar::BindingMode::prepare,51);
    const auto request=worker::parse(plan.request.dump());
    need(plan.reason.empty()&&request.liveUiAccount&&request.steam.account==12345678,"actual-offline-main-plan-provenance");
    need(request.windowsSession==actual::observedWindowsSession,"binding-plan-uses-shared-windows-session");
    need(actual::brokerStops==stopsBeforeResleep+1&&actual::brokerStarts==startsBeforeResleep+1,
        "current-preparation-still-replaces-broker-exactly-once");
    unsigned receiptRefusals=0;
    for(unsigned variant=1;variant<=6;++variant){
        actual::identityVariant=variant;const auto beforeRefresh=actual::identityRefreshes,beforeBroker=actual::brokerStops;
        const auto missing=actual::bindingPlanForCurrentIntent(ymcc::deckysidebar::BindingMode::prepare,500+variant);
        need(missing.request.empty()&&missing.reason=="steam-session-unconfirmed","unknown-or-foreign-receipt-never-builds-plan");
        need(actual::identityRefreshes==beforeRefresh+1&&actual::brokerStops==beforeBroker,"missing-shared-fact-only-requests-original-producer");
        ++receiptRefusals;
    }
    actual::identityVariant=7;
    auto uiLocal=actual::bindingPlanForCurrentIntent(ymcc::deckysidebar::BindingMode::prepare,507);
    need(uiLocal.reason.empty()&&worker::parse(uiLocal.request.dump()).windowsSession==actual::observedWindowsSession,"account-only-receipt-change-keeps-shared-process-fact");
    actual::identityVariant=0;
    need(actual::bindingWindowsQueries==0,"production-binding-plan-performs-zero-windows-session-queries");
    actual::observedMode["deckyAccountWatchAvailable"]=false;const auto pendingRefreshes=actual::refreshes;
    need(!admission().allowed,"actual-offline-missing-live-proof-refused");
    need(actual::refreshes==pendingRefreshes+1,"actual-offline-async-proof-requests-original-shared-refresh");
    need(!actual::bindingPlanForCurrentIntent(ymcc::deckysidebar::BindingMode::prepare,52).reason.empty(),"actual-offline-missing-proof-no-plan");
    actual::observedMode["deckyAccountWatchAvailable"]=true;actual::observedSession.account=12345678;
    need(admission().allowed,"same-account-online-admission");
    plan=actual::bindingPlanForCurrentIntent(ymcc::deckysidebar::BindingMode::refresh,53);
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
        need(!actual::bindingPlanForCurrentIntent(ymcc::deckysidebar::BindingMode::refresh,60+cycle*3).reason.empty(),"account-switch-stale-birth-plan-refused");
        ++staleProofRefusals;actual::observedMode["birth"]=currentBirth;
        actual::observedMode["pid"]=actual::observedSession.pid+1;
        need(!admission().allowed,"account-switch-stale-pid-admission-refused");
        need(!actual::bindingPlanForCurrentIntent(ymcc::deckysidebar::BindingMode::refresh,61+cycle*3).reason.empty(),"account-switch-stale-pid-plan-refused");
        ++staleProofRefusals;actual::observedMode["pid"]=actual::observedSession.pid;
        need(admission().allowed,"account-switch-current-offline-proof-admitted");
        auto switched=actual::bindingPlanForCurrentIntent(ymcc::deckysidebar::BindingMode::refresh,62+cycle*3);
        auto switchedRequest=worker::parse(switched.request.dump());
        need(switched.reason.empty()&&switchedRequest.liveUiAccount&&switchedRequest.steam.account==currentAccount,"account-switch-current-offline-plan");
        actual::observedSession.account=currentAccount;
        need(admission().allowed,"account-switch-online-readmitted");
        switched=actual::bindingPlanForCurrentIntent(ymcc::deckysidebar::BindingMode::refresh,100+cycle);
        switchedRequest=worker::parse(switched.request.dump());
        need(switched.reason.empty()&&!switchedRequest.liveUiAccount&&switchedRequest.steam.account==currentAccount,"account-switch-online-plan-retains-registry");
        // Replay a stale nonzero registry account on the SAME current Steam.
        // Admission, typed binding and outage recovery must all keep working.
        actual::observedSession.account=currentAccount==12345678u?87654321u:12345678u;
        need(admission().allowed,"stale-registry-current-ui-admission");
        switched=actual::bindingPlanForCurrentIntent(ymcc::deckysidebar::BindingMode::refresh,200+cycle);
        switchedRequest=worker::parse(switched.request.dump());
        need(switched.reason.empty()&&switchedRequest.liveUiAccount&&switchedRequest.steam.account==currentAccount,"stale-registry-current-ui-binding");
        need(actual::actualConnectionHealth()().required,"stale-registry-current-ui-recovery-required");
        ++accountTransitions;
    }
    // Exact main consumers distinguish unknown admission from an admitted
    // outage. No real CDP/Steam clock or account is changed by this replay.
    actual::observedSession.account=0;actual::observedMode["deckyAccountWatchAvailable"]=false;
    auto pendingHealth=actual::actualConnectionHealth();const auto refreshAt=actual::refreshes;
    for(ULONGLONG tick=100000;tick<=125000;tick+=5000){actual::fixedTick=tick;const auto health=pendingHealth();need(!health.required&&health.paused,"unconfirmed-account-no-recovery-authority");}
    const auto unconfirmedReads=actual::refreshes-refreshAt;
    actual::observedMode["mode"]="desktop";
    const auto desktopWithoutAccount=admission();actual::observedSession.account=12345678;
    const auto desktopWithAccount=admission();
    std::cout<<"ADMISSION_IDLE_OBSERVATION pendingRefreshesIn25s="<<unconfirmedReads
        <<" desktopRetry="<<desktopWithAccount.retryable<<" offlineDesktopReason="<<desktopWithoutAccount.reason<<'\n';
    need(!desktopWithAccount.allowed&&!desktopWithAccount.retryable&&desktopWithAccount.reason=="steam-bigpicture-required","confirmed-desktop-parks-admission");
    need(!desktopWithoutAccount.allowed&&!desktopWithoutAccount.retryable&&desktopWithoutAccount.reason=="steam-bigpicture-required","desktop-needs-no-account-proof-to-remain-parked");
    need(unconfirmedReads==1,"unconfirmed-admission-keeps-thirty-second-evidence-cadence");
    actual::observedSession.account=0;actual::observedMode["mode"]="bigpicture";actual::fixedTick=130000;
    pendingHealth();need(actual::refreshes==refreshAt+2,"unconfirmed-admission-still-rechecks-at-thirty-seconds");
    actual::observedSession.account=12345678;actual::observedMode["deckyAccountWatchAvailable"]=true;
    actual::observedMode["observedAccount"]=12345678;
    auto admittedHealth=actual::actualConnectionHealth();const auto admittedAt=actual::refreshes;
    actual::fixedTick=200000;need(admittedHealth().required,"confirmed-bigpicture-retains-recovery-authority");
    actual::fixedTick=204999;admittedHealth();need(actual::refreshes==admittedAt+1,"admitted-outage-no-early-evidence-read");
    actual::fixedTick=205000;admittedHealth();need(actual::refreshes==admittedAt+2,"admitted-outage-keeps-five-second-evidence");
    actual::observedMode["mode"]="desktop";actual::fixedTick=500000;admittedHealth();
    need(actual::refreshes==admittedAt+2,"confirmed-desktop-has-no-health-cdp-read");
    actual::observedMode["mode"]="bigpicture";
    // Original current-main health consumer, with all server-side facts healthy.
    // The browser client can still be disconnected; it must keep the ladder open.
    actual::g_deckyMirrorBroker.connected=true;actual::observedMode["deckyBackendConnected"]=true;actual::observedMode["deckyHooksInstalled"]=true;
    actual::observedMode["deckyClientConnected"]=false;const auto clientDown=admittedHealth();
    need(clientDown.required&&!clientDown.connected&&clientDown.reason=="frontend-client-disconnected","native-open-peer-cannot-certify-browser-client");
    actual::observedMode["deckyClientChecking"]=true;const auto checking=admittedHealth();
    need(checking.required&&checking.paused&&!checking.connected&&checking.reason=="frontend-client-check-pending","pending-real-opening-neither-recovers-nor-restarts");
    using Window=ymcc::deckysidebar::ConnectionRecoveryWindow;Window recovery;auto outage=clientDown;outage.identity=checking.identity;
    need(recovery.observe(100,outage)==Window::Action::recycle,"browser-failure-retains-original-service-rung");
    need(recovery.observe(10000,checking)==Window::Action::none,"pending-probe-parks-existing-episode");
    actual::observedMode["deckyBackendConnected"]=false;
    need(admittedHealth().paused,"opening-probe-must-finish-before-official-socket-loss-escalates");
    actual::observedMode["deckyClientChecking"]=false;actual::observedMode["deckyClientConnected"]=true;
    need(!admittedHealth().connected,"bare-mirror-does-not-hide-official-backend-loss");
    actual::observedMode["deckyClientVerified"]=true;
    const auto usable=admittedHealth();need(usable.connected&&usable.reason=="sidebar-native-confirmed-official-backend-unavailable","current-native-sidebar-proof-avoids-ui-recovery");
    need(recovery.observe(10001,usable)==Window::Action::recovered,"native-ready-sidebar-finishes-heavy-recovery");
    actual::observedMode["deckyHooksInstalled"]=false;need(!admittedHealth().connected,"native-proof-cannot-hide-lost-ui-hooks");actual::observedMode["deckyHooksInstalled"]=true;
    actual::g_deckyMirrorBroker.connected=false;need(!admittedHealth().connected,"native-proof-cannot-hide-lost-current-peer");actual::g_deckyMirrorBroker.connected=true;
    actual::observedMode["deckyClientConnected"]=false;need(!admittedHealth().connected,"native-proof-cannot-hide-expired-opening");actual::observedMode["deckyClientConnected"]=true;
    auto lightIdle=actual::actualConnectionHealth();const auto lightAt=actual::refreshes;
    actual::fixedTick=1500000;lightIdle();for(unsigned repeat=0;repeat<1000;++repeat)lightIdle();
    actual::fixedTick=1529999;lightIdle();need(actual::refreshes==lightAt+1,"usable-sidebar-does-not-poll-at-failed-backend-cadence");
    actual::fixedTick=1530000;lightIdle();need(actual::refreshes==lightAt+2,"usable-sidebar-keeps-original-idle-observation");
    actual::observedMode.erase("deckyClientVerified");actual::observedMode["deckyBackendConnected"]=true;
    recovery.observe(10002,clientDown);
    actual::observedMode["deckyClientChecking"]=false;actual::observedMode["deckyClientConnected"]=true;const auto ready=admittedHealth();need(ready.connected,"actual-browser-client-and-native-channels-certify-connection");
    need(recovery.observe(10001,ready)==Window::Action::recovered,"correlated-probe-readback-finishes-existing-episode");
    actual::observedMode.erase("deckyClientChecking");
    auto oldRun=makeRequest(1).at("bootstrap").at("runId").get<std::string>();const auto openRefresh=actual::refreshes;
    actual::deckySidebarOwnedBindingDiagnostic({{"bootstrapStage","connection-check"},{"reason","qam-open"},{"runId",oldRun}});
    need(actual::refreshes==openRefresh+1,"current-QAM-open-invalidates-shared-mode-before-connection-check");
    actual::deckySidebarOwnedBindingDiagnostic({{"bootstrapStage","connection-check"},{"reason","qam-open"},{"runId","old-run"}});
    need(actual::refreshes==openRefresh+1,"old-QAM-open-cannot-refresh-new-run");
    actual::g_powerLifecycle=actual::PowerLifecycle::Suspended;
    actual::deckySidebarOwnedBindingDiagnostic({{"bootstrapStage","connection-check"},{"reason","qam-open"},{"runId",oldRun}});
    need(actual::refreshes==openRefresh+1,"QAM-open-does-not-bypass-shared-sleep-authority");actual::g_powerLifecycle=actual::PowerLifecycle::Ready;
    actual::g_deckyMirrorBroker.connected=false;actual::observedMode.erase("deckyClientConnected");
    actual::observedMode["mode"]="desktop";
    ymcc::deckysidebar::AdmissionWindow parked;parked.blocked(1,100000,desktopWithAccount.retryable);
    for(ULONGLONG tick=100000;tick<=3700000;tick+=30000)need(parked.timeout(tick)==INFINITE&&!parked.take(tick),"desktop-hour-has-no-admission-deadline");
    parked.blocked(1,3700001,true);need(parked.timeout(3700001)==30000,"unknown-after-desktop-reuses-slow-recovery-without-refilling-fast-window");
    actual::fixedTick=0;
    h.runtime.shutdown();const auto remaining=h.client->snapshot();actual::runtime=nullptr;
    plan.delegation.reset();uiLocal.delegation.reset();actual::g_steamSettingsObserver.publisher.reset();
    need(remaining.fullyDestroyed&&before==handles()&&context.alive(),"offline-proof-resource-return");

    // The actual admission lambda on the production Runtime must still resume
    // from its original mode event, even after its timer was parked.
    ymcc::deckysidebar::Runtime desktop;actual::runtime=&desktop;
    std::mutex modeLock;std::atomic<unsigned> rejectedPreflight{0};ymcc::deckysidebar::Config desktopConfig;
    desktopConfig.executable=L"NeverLaunch.exe";
    desktopConfig.ownedBinding=std::make_shared<Client>();
    desktopConfig.launchAdmission=[&]{std::lock_guard lock(modeLock);return admission();};
    desktopConfig.preflight=[&]{++rejectedPreflight;return std::string("inert-preflight-stop");};
    desktop.initialize(std::move(desktopConfig));desktop.observeSteam(1,true);desktop.setEnabled(true);
    need(waitFor([&]{return desktop.snapshot().reason=="steam-bigpicture-required";}),"actual-runtime-desktop-waits-for-mode-event");
    const auto snapshotCalls=actual::modeSnapshots.load();Sleep(1100);
    need(!desktop.snapshot().admissionRetry&&actual::modeSnapshots==snapshotCalls&&rejectedPreflight==0,"actual-runtime-desktop-idle-has-no-retry-read-or-preflight");
    {std::lock_guard lock(modeLock);actual::observedMode["mode"]="bigpicture";}
    const auto modeEventAt=GetTickCount64();need(desktop.requestLaunchAdmissionRefresh(),"mode-event-reaches-original-admission-owner");
    need(waitFor([&]{return rejectedPreflight==1;},800),"event-enters-bigpicture-without-waiting-for-thirty-second-timer");
    const auto eventResumeMs=GetTickCount64()-modeEventAt;
    need(desktop.snapshot().pid==0,"event-continuation-still-rejects-inert-preflight-without-loader");
    desktop.setEnabled(false);desktop.shutdown();actual::runtime=nullptr;
    need(!desktop.snapshot().enabled&&before==handles(),"parked-desktop-off-and-event-resource-return");
    // Win5 2026-11-09: health alone stayed slow, but the actual admission
    // callback forced another read on every missing-account wake. Exercise
    // both consumers with the original Runtime's fast/slow admission state.
    // This Runtime never receives launch authority or a real Steam command.
    ymcc::deckysidebar::Runtime accountWait;
    ymcc::deckysidebar::Config accountConfig;
    accountConfig.launchAdmission=[] { return ymcc::deckysidebar::LaunchAdmission(false,"steam-session-unconfirmed",true); };
    accountWait.initialize(std::move(accountConfig));accountWait.observeSteam(1,true);accountWait.setEnabled(true);
    actual::runtime=&accountWait;actual::observedSession.account=0;
    actual::observedMode["mode"]="bigpicture";actual::observedMode["observedAccount"]=0;
    actual::observedMode["deckyAccountWatchAvailable"]=false;
    need(waitFor([&]{return accountWait.snapshot().admissionRetry;}),"account-wait-original-fast-window-started");
    const auto accountAdmission=actual::actualAdmission();const auto fastRefreshAt=actual::refreshes;
    for(const auto tick:{600000ULL,605000ULL}){actual::fixedTick=tick;need(!accountAdmission().allowed,"fast-account-wait-has-no-launch-authority");}
    need(actual::refreshes==fastRefreshAt+2,"initial-account-proof-keeps-original-fast-read-requests");
    need(waitFor([&]{return accountWait.snapshot().admissionRecovery;},24000),"account-wait-original-runtime-enters-slow-window");
    auto accountHealth=actual::actualConnectionHealth();const auto slowRefreshAt=actual::refreshes;
    for(ULONGLONG tick=610000;tick<=635000;tick+=5000){
        actual::fixedTick=tick;const auto health=accountHealth();
        need(!health.required&&health.paused&&!accountAdmission().allowed,"combined-account-wait-remains-observation-only");
    }
    const auto combinedSlowRefreshes=actual::refreshes-slowRefreshAt;
    std::cout<<"ACCOUNT_WAIT_OBSERVATION combinedRefreshRequestsIn25s="<<combinedSlowRefreshes<<'\n';
    need(combinedSlowRefreshes==2,"slow-account-wait-admission-and-health-do-not-create-five-second-read-loop");
    actual::fixedTick=640000;accountHealth();accountAdmission();
    need(actual::refreshes==slowRefreshAt+4,"both-existing-slow-consumers-retain-their-thirty-second-renewal");
    actual::fixedTick=640001;actual::observedMode["observedAccount"]=12345678;
    actual::observedMode["deckyAccountWatchAvailable"]=true;
    need(accountAdmission().allowed,"new-offline-account-proof-admitted-before-next-slow-read");
    // Same original consumer, now with the actual shared cache invalidation.
    // A fresh known desktop must park slow admission before it clears that fact.
    ymcc::steamui::ModeObserver desktopObserver;
    std::atomic<unsigned> observerReads{0},desktopChanges{0};
    std::atomic<bool> releaseRefresh{false};
    auto desktopFacts=actual::observedMode;desktopFacts["mode"]="desktop";
    ymcc::steamui::ModeObserverConfig desktopObserverConfig;
    desktopObserverConfig.debounceMs=1;desktopObserverConfig.minimumGapMs=1;
    desktopObserverConfig.read=[&](const auto&){
        if(++observerReads>1){const auto until=GetTickCount64()+1000;while(!releaseRefresh&&GetTickCount64()<until)Sleep(1);}
        return desktopFacts;
    };
    desktopObserverConfig.changed=[&](const auto&,const auto&){++desktopChanges;};
    need(desktopObserver.initialize(std::move(desktopObserverConfig)),"real-desktop-observer-initialized");
    desktopObserver.setDemand(ymcc::steamui::ModeConsumer::Decky,true);desktopObserver.observeSteam(actual::observedSession);
    const bool desktopConfirmed=waitFor([&]{return desktopObserver.snapshot(actual::observedSession).value("mode",std::string{})=="desktop";});
    actual::realModeObserver=&desktopObserver;actual::fixedTick=670000;
    const auto renewalAt=actual::refreshes;
    const auto desktopRenewal=accountAdmission();
    const auto renewalRequests=actual::refreshes-renewalAt;
    const bool retainedDesktop=desktopObserver.snapshot(actual::observedSession).value("mode",std::string{})=="desktop";
    // The real observer can accept a renewal request without arming a read.
    const bool redundantRead=waitFor([&]{return observerReads.load()>1;},80);
    releaseRefresh=true;desktopObserver.shutdown();actual::realModeObserver=nullptr;
    std::cout<<"REAL_DESKTOP_RENEWAL confirmed="<<desktopConfirmed<<" retained="<<retainedDesktop<<" retry="<<desktopRenewal.retryable<<" reason="<<desktopRenewal.reason<<" requests="<<renewalRequests<<" reads="<<observerReads<<" changes="<<desktopChanges<<'\n';
    need(desktopConfirmed&&retainedDesktop&&!redundantRead&&observerReads.load()==1&&
        !desktopRenewal.allowed&&!desktopRenewal.retryable&&desktopRenewal.reason=="steam-bigpicture-required","slow-renewal-must-preserve-verified-desktop-in-real-observer");
    const auto slowRetryAt=actual::refreshes;
    Json slowAdmissionRetryCounts=Json::array();
    actual::realModeObserver=&desktopObserver; // The stopped observer refuses the renewal.
    actual::fixedTick=700000;
    const auto stoppedObserver=accountAdmission();
    need(!stoppedObserver.allowed&&stoppedObserver.retryable&&observerReads.load()==1,"stopped-observer-renewal-grants-no-launch");
    slowAdmissionRetryCounts.push_back(actual::refreshes-slowRetryAt);
    actual::realModeObserver=nullptr;
    actual::observedMode["observedAccount"]=0;
    actual::observedMode["deckyAccountWatchAvailable"]=false;
    for(const auto tick:{700005ULL,730004ULL,730005ULL}){
        actual::fixedTick=tick;
        need(!accountAdmission().allowed,"slow-read-retry-does-not-grant-account-proof");
        slowAdmissionRetryCounts.push_back(actual::refreshes-slowRetryAt);
    }
    accountWait.setEnabled(false);const auto offRefreshes=actual::refreshes;
    need(!accountAdmission().allowed&&actual::refreshes==offRefreshes,"off-cannot-renew-account-read-or-admit-loader");
    accountWait.shutdown();actual::runtime=nullptr;actual::fixedTick=0;
    need(before==handles(),"account-wait-owner-returns-original-runtime-resources");
    return {{"id","native.typed-offline-current-ui-account-consumers"},{"passed",refreshAdmissionCounts==Json::array({1,2,2,3})&&slowAdmissionRetryCounts==Json::array({1,2,2,3})},
        {"slowAdmissionRetryCounts",slowAdmissionRetryCounts},
        {"refreshAdmissionCounts",refreshAdmissionCounts},{"beforeHandles",before},{"afterHandles",handles()},
        {"atExit",state(remaining)},{"actualMainConsumers",true},{"offlineAdmission",true},{"typedNonzeroProof",true},
        {"missingProofRefused",true},{"sameAccountOnline",true},{"ownerRevocation",true},{"cefUnaffected",context.alive()},
        {"accountTransitions",accountTransitions},{"staleProofRefusals",staleProofRefusals},{"actualAccountLoginPerformed",false},
        {"sharedReceiptRefusals",receiptRefusals},{"bindingWindowsQueries",actual::bindingWindowsQueries},{"sharedIdentityRefreshes",actual::identityRefreshes},
        {"unconfirmedRefreshesIn25s",unconfirmedReads},{"desktopAdmissionDeadlinesPerHour",0},{"desktopEventResumeMs",eventResumeMs},
        {"actualAdmissionAndHealthConsumers",true},{"openingCadenceEvidence",openingCadenceEvidence},{"sessionReadPublicationRace",sessionRace},{"canceledBinding",canceledBinding},{"canceledIntent",canceledIntent},{"combinedSlowRefreshRequestsIn25s",combinedSlowRefreshes},
        {"initialFastAccountProofRetained",true},{"offlineProofBeforeSlowDeadline",true},{"realSteamModeOperations",0}};
}
}

#ifdef YMCC_BINDING_CONSTRUCTOR_FIXTURE
#include "fixtures/decky_binding_constructor_cases.inc"
#endif
#ifdef YMCC_DECKY_REPORT_ALLOCATION_FIXTURE
namespace rtfixture {
inline Json loaderStartReportAllocation(const std::filesystem::path& output){
 Controls controls(L"burst");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
 std::atomic<bool> firstPublication{false},postLoop{false};std::atomic<DWORD> firstPid{0};
 detail::Handle heldLoader;std::atomic<unsigned long long> firstBirth{0};std::atomic<unsigned> uiSteps{0};
 Harness* current=nullptr;
 auto changed=[&](const ymcc::deckysidebar::State& observed){
  if(observed.phase!="loader-started"||firstPublication.load())return;
  heldLoader.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,observed.pid);
  FILETIME born{},exited{},kernel{},user{};
  need(heldLoader.value&&current&&current->runtime.ownsLiveProcess(heldLoader.value)&&
       GetProcessTimes(heldLoader.value,&born,&exited,&kernel,&user),"fault-target-is-exact-new-owned-inert-loader");
  firstPid=observed.pid;firstBirth=(static_cast<unsigned long long>(born.dwHighDateTime)<<32)|born.dwLowDateTime;
  ymcc::deckybinding::fixture::constructionprobe::failures=0;ymcc::deckybinding::fixture::constructionprobe::failedBytes=0;
  firstPublication=true;ymcc::deckybinding::fixture::constructionprobe::skipAllocations=0;
  ymcc::deckybinding::fixture::constructionprobe::failNext=true;
 };
 auto health=[](){ymcc::deckysidebar::ConnectionHealth value;value.required=true;value.identity="fixture-start-report-ready1";return value;};
 auto recoveredLoop=[&]{if(firstPublication.load()&&ymcc::deckybinding::fixture::constructionprobe::failures==1)postLoop=true;};
 auto step=[&](auto){++uiSteps;return Json{{"ok",false},{"mutated",false}};};
 Harness harness(output,context,false,{},{},{},recoveredLoop,{},health,step,{},changed);current=&harness;
 const auto before=handles();need(harness.runtime.setEnabled(true),"original-runtime-start-enabled");
 need(waitFor([&]{const auto value=harness.runtime.snapshot();return postLoop.load()||(firstPublication.load()&&value.phase=="failed");}),"original-runtime-completed-start-after-fault");
 const auto observed=harness.runtime.snapshot();const auto wait=heldLoader.value?WaitForSingleObject(heldLoader.value,0):WAIT_FAILED;
 DWORD code=0;const bool codeKnown=heldLoader.value&&GetExitCodeProcess(heldLoader.value,&code)!=FALSE;
 const auto plans=harness.plans.load();const bool sameLive=firstPid!=0&&observed.pid==firstPid&&observed.phase=="loader-started"&&wait==WAIT_TIMEOUT;
 const auto failures=ymcc::deckybinding::fixture::constructionprobe::failures.load();const auto bytes=ymcc::deckybinding::fixture::constructionprobe::failedBytes.load();
 harness.runtime.shutdown();const auto resources=harness.client->snapshot();const auto final=harness.runtime.snapshot();const bool heldReturned=heldLoader.value&&CloseHandle(heldLoader.value)!=FALSE;if(heldReturned)heldLoader.value=nullptr;
 Json result{{"id","native.typed-runtime-live-watch"},{"parameter","loader-start-report-memory"},{"passed",false},
  {"faultAtOriginalColdReport",failures==1&&bytes==32},{"failures",failures},{"bytes",bytes},{"firstLoaderPid",firstPid.load()},{"firstLoaderBirth",std::to_string(firstBirth.load())},
  {"phaseAfterFault",observed.phase},{"reasonAfterFault",observed.reason},{"pidAfterFault",observed.pid},{"heldWaitAfterFault",wait},{"heldExitCodeKnown",codeKnown},{"heldExitCode",code},
  {"sameOwnedLoaderRemainsLive",sameLive},{"bindingPlans",plans},{"uiSteps",uiSteps.load()},{"beforeHandles",before},{"afterHandles",handles()},
  {"atExit",state(resources)},{"runtimeDisabledAfterStop",!final.enabled&&final.pid==0},{"heldLoaderHandleReturned",heldReturned},{"cefUnaffected",context.alive()},{"realSteamOrDeviceActions",0}};
 result["passed"]=sameLive&&failures==1&&bytes==32&&plans==1&&uiSteps==0&&before==handles()&&resources.fullyDestroyed&&heldReturned&&!final.enabled&&final.pid==0&&context.alive();
 return result;
}

inline Json loaderExitReportAllocation(const std::filesystem::path& output,bool beforeWait=false){
 Controls controls(L"burst");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
 std::atomic<bool> entered{false},release{false},firstPublication{false};
 std::function<void(const ymcc::deckysidebar::State&)> changed;
 if(beforeWait)changed=[&](const auto& observed){
  if(observed.phase=="loader-started"&&!firstPublication.exchange(true)){
   entered=true;const auto until=GetTickCount64()+2000;
   while(!release.load()&&GetTickCount64()<until)Sleep(1);
  }
 };
 Harness harness(output,context,false,{},{},{},{},{},{},{},{},std::move(changed));
 const auto before=handles();harness.start();
 need(waitFor([&]{return beforeWait?entered.load():harness.burstDone.load();}),"original-start-publication-or-burst-reached");
 const auto first=harness.runtime.snapshot().pid;const auto firstPlans=harness.plans.load();
 need(first!=0&&firstPlans==1,"one-current-owned-loader-before-fault");
 {
  detail::Handle loader{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_TERMINATE|SYNCHRONIZE,FALSE,first)};
  need(loader.value&&harness.runtime.ownsLiveProcess(loader.value),"fault-target-is-exact-runtime-owned-inert-loader");
  harness.burstDone=false;reportAllocationProbe::mode=1;reportAllocationProbe::budget=1;reportAllocationProbe::hits=0;
  ymcc::deckybinding::fixture::constructionprobe::failures=0;ymcc::deckybinding::fixture::constructionprobe::failedBytes=0;
  need(TerminateProcess(loader.value,37)!=FALSE,"only-owned-inert-loader-exits");
  need(WaitForSingleObject(loader.value,1000)==WAIT_OBJECT_0,"original-loader-death-confirmed");
  if(beforeWait){harness.runtime.notifyListenerFacts();release=true;}
 }
 need(waitFor([&]{return harness.runtime.snapshot().pid!=0&&harness.runtime.snapshot().pid!=first&&harness.burstDone.load()&&harness.plans.load()==2;}),"original-runtime-recovers-after-optional-exit-report-fault");
 const auto recovered=harness.runtime.snapshot();const auto plans=harness.plans.load();
 const auto failures=harness.loaderFaultPublications.load();harness.runtime.shutdown();
 const auto atExit=harness.client->snapshot();
 Json result{{"id","native.typed-runtime-loader-exit-optional-report-allocation"},{"passed",false},
  {"beforeHandles",before},{"afterHandles",handles()},{"atExit",state(atExit)},{"cefUnaffected",context.alive()},
  {"plansBefore",firstPlans},{"plansAfter",plans},{"oldLoaderPid",first},{"recoveredLoaderPid",recovered.pid},
  {"loaderFaultPublications",failures},{"optionalReportFault",{{"hits",reportAllocationProbe::hits.load()},
   {"failures",ymcc::deckybinding::fixture::constructionprobe::failures.load()},
   {"bytes",ymcc::deckybinding::fixture::constructionprobe::failedBytes.load()},{"site",reportAllocationProbe::lastAction.load()}}},
  {"realSteamActions",0},{"privateNewBoundary",true},{"originalHealthyDefaultCaseUnchanged",true}};
 result["passed"]=before==handles()&&atExit.fullyDestroyed&&context.alive()&&plans==2&&failures==1&&
  reportAllocationProbe::hits==1&&ymcc::deckybinding::fixture::constructionprobe::failures==1&&reportAllocationProbe::lastAction==(beforeWait?8u:7u);
 return result;
}
}
#endif

static nlohmann::json blockedOwnerNativeCase(const std::filesystem::path& output,bool automatic=false,bool stopBeforeUnprotect=false){
 using namespace rtfixture;using namespace ymcc::deckybinding;using Json=nlohmann::json;
 Controls controls(L"runtime-success-checkpoint");Context context;ContextJob contextOwner(worker::number(context.lease.at("pid")));
 std::atomic<bool> fault{false};std::atomic<unsigned> readCount{0},completedReads{0},ui{0};Harness* owner=nullptr;blockedOwnerNativeSelected=true;blockedOwnerNativeReserves=0;
 Harness h(output,context,true,{}, {}, {}, [&]{completedReads=readCount.load();}, {},[&]{++readCount;const bool ready=owner&&owner->healthyGeneration.load()>=(fault?2u:1u);
  ymcc::deckysidebar::ConnectionHealth value{true,ready,"native-close-owner-current",!owner||owner->healthyGeneration.load()==0,"native-close-fixed-session"};value.transportConnected=ready;return value;},
  [&](auto){++ui;return Json{{"ok",false},{"mutated",false}};});owner=&h;
 const auto before=handles();need(h.runtime.setEnabled(true),"native-blocked-enable");need(waitFor([&]{return h.healthyGeneration>=1;}),"native-blocked-first-binding");
 const auto first=h.client->snapshot();detail::Handle exact{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,first.processId)};
 need(exact.value&&WaitForSingleObject(exact.value,0)==WAIT_TIMEOUT,"native-blocked-exact-live-child");const auto expected=identity(exact.value);
 struct Protection {std::vector<HANDLE> values;bool clear(){bool ok=true;for(const auto value:values)if(!SetHandleInformation(value,HANDLE_FLAG_PROTECT_FROM_CLOSE,0))ok=false;values.clear();return ok;}~Protection(){clear();}} protection;
 struct Entry {HANDLE value;ULONG_PTR handles,pointers;ULONG access,type,attributes,reserved;};struct Table {ULONG_PTR count,reserved;Entry entries[1];};
 using Query=LONG(NTAPI*)(HANDLE,ULONG,PVOID,ULONG,PULONG);const auto query=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationProcess"));need(query!=nullptr,"native-blocked-handle-api");
 std::vector<unsigned char> tableBytes(65536);ULONG required=0;LONG status=0;for(unsigned n=0;n<4;++n){status=query(GetCurrentProcess(),51,tableBytes.data(),static_cast<ULONG>(tableBytes.size()),&required);if(status>=0)break;need(required>tableBytes.size()&&required<=16*1024*1024,"native-blocked-table-size");tableBytes.resize(static_cast<std::size_t>(required)+4096);}
 need(status>=0,"native-blocked-table-query");const auto* table=reinterpret_cast<const Table*>(tableBytes.data());need(table->count<=(tableBytes.size()-offsetof(Table,entries))/sizeof(Entry),"native-blocked-table-bounds");
 for(ULONG_PTR n=0;n<table->count;++n){const auto handle=table->entries[n].value;if(GetProcessId(handle)!=first.processId)continue;if(identity(handle)!=expected)continue;DWORD flags=0;need(GetHandleInformation(handle,&flags)&&!(flags&HANDLE_FLAG_PROTECT_FROM_CLOSE),"native-blocked-exact-process-handle");need(SetHandleInformation(handle,HANDLE_FLAG_PROTECT_FROM_CLOSE,HANDLE_FLAG_PROTECT_FROM_CLOSE),"native-blocked-real-protect");protection.values.push_back(handle);}
 const auto protectedCount=protection.values.size();need(protectedCount>=2,"native-blocked-owned-and-query-handles-not-found");
 fault=true;need(h.runtime.requestTransportRecovery(),"native-blocked-original-recovery");need(waitFor([&]{return h.runtime.snapshot().ownedBindingCleanupError!=ERROR_SUCCESS;}),"native-blocked-real-cleanup-refusal-missing");const auto refused=h.runtime.snapshot();
 for(unsigned n=0;n<8;++n){const auto previous=readCount.load();need(h.runtime.requestTransportRecovery(),"native-blocked-eight-original-wakes");need(waitFor([&]{return completedReads.load()>previous;}),"native-blocked-original-iteration-completed");}
 const auto reservationsWhileBlocked=blockedOwnerNativeReserves.load(),plansWhileBlocked=h.plans.load();const auto debt=h.client->snapshot();
 unsigned plansAtOff=0;bool offBeforeRelease=false;bool offStopReported=true;
 if(stopBeforeUnprotect){offStopReported=h.runtime.setEnabled(false);need(!offStopReported&&!h.runtime.snapshot().enabled&&h.runtime.snapshot().ownedBindingCleanupError==ERROR_INVALID_HANDLE,"automatic-off-keeps-real-cleanup-refusal-with-disabled-intent");need(waitFor([&]{return h.runtime.snapshot().phase=="disabled";}),"automatic-off-stop-observed");plansAtOff=h.plans.load();offBeforeRelease=!h.runtime.snapshot().enabled;}
 need(protection.clear(),"native-blocked-unprotect-exact-handles");const auto unprotectedAt=GetTickCount64();const auto readsAtUnprotect=readCount.load();
 if(!automatic)need(h.runtime.requestTransportRecovery(),"native-blocked-original-return-ingress");
 const DWORD recoveryWindow=automatic?ymcc::deckysidebar::ConnectionRecoveryWindow::evidenceIntervalMs+ymcc::deckysidebar::ConnectionRecoveryWindow::transportGraceMs:5000;
 bool recovered=false;
 if(stopBeforeUnprotect)Sleep(ymcc::deckysidebar::ConnectionRecoveryWindow::evidenceIntervalMs+250);
 else recovered=waitFor([&]{return h.healthyGeneration>=2;},recoveryWindow);
 const auto recoveryElapsed=GetTickCount64()-unprotectedAt;const auto readsAfterUnprotect=readCount.load()-readsAtUnprotect;
 const bool stayedOff=!stopBeforeUnprotect||(offBeforeRelease&&!h.runtime.snapshot().enabled&&h.plans.load()==plansAtOff&&readsAfterUnprotect==0);
 const bool lateRequestsRejected=!stopBeforeUnprotect||(!h.runtime.requestTransportRecovery()&&!h.runtime.requestConnectionCheck());
 const auto generationAtDeadline=h.healthyGeneration.load();const auto observedBeforeOff=h.runtime.snapshot();const auto observedClientBeforeOff=h.client->snapshot();
 h.runtime.shutdown();const auto after=h.client->snapshot();if(exact.value){CloseHandle(exact.value);exact.value=nullptr;}unsigned blockedRecords=0;for(const auto& row:h.globalRecoveryLog)if(row.value("recoveryPhase",std::string{})=="blocked")++blockedRecords;
 return {{"passed",reservationsWhileBlocked==0&&blockedRecords==0&&ui==0&&(stopBeforeUnprotect?stayedOff&&lateRequestsRejected:recovered)&&after.fullyDestroyed&&after.handles==0&&after.pendingIo==0&&before==handles()&&context.alive()},
 {"offStopReportedDestroyed",offStopReported},{"stopBeforeUnprotect",stopBeforeUnprotect},{"stayedOff",stayedOff},{"lateRequestsRejected",lateRequestsRejected},{"plansAtOff",plansAtOff},{"automaticAfterUnprotect",automatic},{"explicitWakesAfterUnprotect",automatic?0:1},{"observationWindowMs",recoveryWindow},{"recoveryElapsedMs",recoveryElapsed},{"readsAfterUnprotect",readsAfterUnprotect},{"realNativeCloseRefusal",true},{"refusedError",refused.ownedBindingCleanupError},{"protectedProcessHandles",protectedCount},{"blockedRecoveryRecords",blockedRecords},{"blockedReservationCalls",reservationsWhileBlocked},
 {"plansWhileBlocked",plansWhileBlocked},{"finalPlans",h.plans.load()},{"recoveredViaOriginalIngress",recovered},{"generationAtDeadline",generationAtDeadline},{"runtimePhaseAtDeadline",observedBeforeOff.phase},{"runtimeReasonAtDeadline",observedBeforeOff.reason},{"runtimeCleanupErrorAtDeadline",observedBeforeOff.ownedBindingCleanupError},{"clientAtDeadline",state(observedClientBeforeOff)},{"uiActions",ui.load()},{"debt",state(debt)},{"afterOff",state(after)},{"beforeHandles",before},{"afterHandles",handles()},{"cefUnaffected",context.alive()},{"realSteamActions",0}};
}
int main(int argc,char** argv){
    int code=0;if(rtfixture::tryStalledChild(code))return code;if(ymcc::deckybinding::fixture::tryRunChild(code))return code;
    if(argc==4&&(std::string_view(argv[3])=="--blocked-owner-native"||std::string_view(argv[3])=="--blocked-owner-native=automatic"||std::string_view(argv[3])=="--blocked-owner-native=automatic-off")){if(std::string_view(argv[2])!="native.typed-runtime-live-watch")return 64;
      const auto calibration=rtfixture::initializeSharedCryptoForMeasurement();nlohmann::json value;try{value=blockedOwnerNativeCase(std::filesystem::absolute(argv[1]),std::string_view(argv[3])!="--blocked-owner-native",std::string_view(argv[3])=="--blocked-owner-native=automatic-off");}catch(const std::exception& error){value={{"passed",false},{"error",error.what()}};}
      value["sharedCryptoInitialization"]=calibration;std::cout<<"NATIVE_BLOCKED_OWNER_RESULT "<<value.dump()<<std::endl;return value.value("passed",false)?0:1;}

#ifdef YMCC_BINDING_CONSTRUCTOR_FIXTURE
    if(argc==4&&std::string_view(argv[3])=="--watch-end-report=memory"){
        const bool retained=std::string_view(argv[2])=="native.typed-healing-child-exit-retained";
        if(!retained&&std::string_view(argv[2])!="native.typed-healing-child-exit-owned")return 64;
#ifndef YMCC_DECKY_REPORT_ALLOCATION_FIXTURE
        return 64;
#else
        const auto out=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(out);
        const auto calibration=rtfixture::initializeSharedCryptoForMeasurement();
        reportAllocationProbe::watchEndSelected=true;reportAllocationProbe::mode=1;reportAllocationProbe::budget=1;reportAllocationProbe::hits=0;reportAllocationProbe::lastAction=0;
        ymcc::deckybinding::fixture::constructionprobe::failures=0;ymcc::deckybinding::fixture::constructionprobe::failedBytes=0;
        nlohmann::json value;
        try{
            value=rtfixture::healing(out/"watch-exit",retained,"child-exit");
            value["originalChildExitPassed"]=value.value("passed",false);
            value["optionalReportFault"]={{"hits",reportAllocationProbe::hits.load()},{"failures",ymcc::deckybinding::fixture::constructionprobe::failures.load()},
                {"bytes",ymcc::deckybinding::fixture::constructionprobe::failedBytes.load()},{"site",reportAllocationProbe::lastAction.load()}};
            value["passed"]=value.value("passed",false)&&reportAllocationProbe::hits==1&&ymcc::deckybinding::fixture::constructionprobe::failures==1&&reportAllocationProbe::lastAction==9&&reportAllocationProbe::watchEndConsumed();
        }catch(const std::exception& error){value={{"passed",false},{"error",error.what()}};}
        value["registrationId"]=std::string(argv[2]);value["parameter"]="watch-end-report-memory";value["sharedCryptoInitialization"]=calibration;
        std::ofstream(out.parent_path()/"watch-end-report-result.json",std::ios::binary)<<value.dump(2)<<'\n';
        std::cout<<value.dump(2)<<'\n';return value.value("passed",false)?0:1;
#endif
    }
    if(argc==4&&std::string_view(argv[3])=="--client-construction=runtime-phase-state-cost"){
        if(std::string_view(argv[2])!="native.typed-runtime-live-watch")return 64;
        const auto out=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(out);
        const auto calibration=rtfixture::initializeSharedCryptoForMeasurement();
        runtimephaseprobe::samples=0;runtimephaseprobe::allocations=0;runtimephaseprobe::bytes=0;runtimephaseprobe::nested=0;
        nlohmann::json value;
        try{
            value=rtfixture::healthy(out/"healthy");
            value["originalHealthyCasePassed"]=value.value("passed",false);
            value["phaseStateCost"]={{"samples",runtimephaseprobe::samples.load()},{"allocations",runtimephaseprobe::allocations.load()},
                {"bytes",runtimephaseprobe::bytes.load()},{"nested",runtimephaseprobe::nested.load()}};
            value["passed"]=value.value("passed",false)&&runtimephaseprobe::samples>=192&&runtimephaseprobe::nested==0&&runtimephaseprobe::allocations==0&&runtimephaseprobe::bytes==0;
        }catch(const std::exception& error){value={{"passed",false},{"error",error.what()}};}
        value["registrationId"]="native.typed-runtime-live-watch";value["parameter"]="runtime-phase-state-cost";value["sharedCryptoInitialization"]=calibration;
        std::ofstream(out.parent_path()/"runtime-phase-state-result.json",std::ios::binary)<<value.dump(2)<<'\n';
        std::cout<<value.dump(2)<<'\n';return value.value("passed",false)?0:1;
    }
    if(argc==4&&std::string_view(argv[3]).rfind("--client-construction=",0)==0){
        using Json=nlohmann::json;
        const bool publication=std::string_view(argv[3])=="--client-construction=packet-publication-cost"||std::string_view(argv[3])=="--client-construction=runtime-detail-cost";
        if(std::string_view(argv[2])!=(publication?"native.typed-runtime-live-watch":"native.typed-ready-rebind"))return 64;
        const auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);
        Json result;try{result=construction137::execute(output,std::string(argv[3]).substr(std::string("--client-construction=").size()));}
        catch(const std::exception& error){result={{"passed",false},{"error",error.what()}};}
        result["registrationId"]=publication?"native.typed-runtime-live-watch":"native.typed-ready-rebind";result["parameter"]="client-construction";
        std::ofstream(output.parent_path()/"client-construction-result.json",std::ios::binary)<<result.dump(2)<<'\n';
        std::cout<<result.dump(2)<<std::endl;return result.value("passed",false)?0:1;
    }

#endif

    // Reuse this actual compiled component body; this explicit branch cannot
    // fall through to the default Runtime group or start its inert Loader path.
    if(argc>=2&&std::string_view(argv[1])=="--client-component"){
        if(argc!=3)return 64;char* componentArgs[]{argv[0],argv[2]};
        return ymccBindingClientComponentSuite(2,componentArgs);
    }
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
    if(argc==4&&std::string_view(argv[3])=="--official-loader-owner-close"){
        if(std::string_view(argv[2])!="native.typed-runtime-live-watch")return 64;
        const auto out=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(out);
        const auto calibration=rtfixture::initializeSharedCryptoForMeasurement();nlohmann::json value;
        try{value=rtfixture::officialLoaderOwnerClose(out);}catch(const std::exception& error){value={{"passed",false},{"error",error.what()}};}
        value["registrationId"]="native.typed-runtime-live-watch";value["parameter"]="official-loader-owner-close";value["sharedCryptoInitialization"]=calibration;
        std::ofstream(out.parent_path()/"official-loader-owner-result.json",std::ios::binary)<<value.dump(2)<<'\n';
        std::cout<<"OFFICIAL_LOADER_OWNER_RESULT "<<value.dump()<<'\n';return value.value("passed",false)?0:1;
    }
    if(argc==1){if(rtfixture::ownedbackend::tryInertLoader(code))return code;Sleep(30000);return 0;} // Only Runtime's inert, owned Loader stand-in.
    if(argc==4&&std::string_view(argv[3]).starts_with("--binding-report=")){
        if(std::string_view(argv[2])!="native.typed-runtime-live-watch")return 64;
        const std::string_view variant=std::string_view(argv[3]).substr(17);
        const unsigned selected=variant=="plan-started"?1u:variant=="plan-finished"?2u:variant=="result-copy"?3u:variant=="trace-started"?4u:variant=="reason-fallback"?5u:variant=="reason-copy"?6u:variant=="reason-controls"?7u:variant=="reason-listener-control"?8u:variant=="diagnostic-tail"?9u:variant=="diagnostic-tail-control"?10u:variant=="borrowed-diagnostic"?11u:variant=="borrowed-payloads"?12u:variant=="borrowed-throws"?13u:variant=="borrowed-watch"?14u:variant=="borrowed-post-queue"?15u:variant=="borrowed-post-metadata"?16u:0u;if(!selected)return 64;
#ifndef YMCC_BINDING_CONSTRUCTOR_FIXTURE
        return 64;
#else
        const auto out=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(out);
        const auto calibration=rtfixture::initializeSharedCryptoForMeasurement();nlohmann::json value;
        try{value=selected==16?rtfixture::borrowedPostQueue(true):selected==15?rtfixture::borrowedPostQueue(false):selected==14?rtfixture::borrowedWatchStream(out):selected>=11?rtfixture::borrowedDiagnosticBoundary(out,selected):selected>=9?rtfixture::diagnosticTailBoundary(out,selected==9):selected==8?rtfixture::bindingReasonControls(out,true):selected==7?rtfixture::bindingReasonControls(out):selected>=5?rtfixture::bindingReasonBoundary(out,selected):rtfixture::bindingReportBoundary(out,selected);}catch(const std::exception& error){value={{"passed",false},{"error",error.what()}};}
        value["sharedCryptoInitialization"]=calibration;
        std::cout<<"BINDING_REPORT_RESULT "<<value.dump()<<std::endl;return value.value("passed",false)?0:1;
#endif
    }
    if(argc==4&&std::string_view(argv[3]).starts_with("--loader-start-report=")){
        if(std::string_view(argv[2])!="native.typed-runtime-live-watch"||std::string_view(argv[3])!="--loader-start-report=memory")return 64;
#ifndef YMCC_DECKY_REPORT_ALLOCATION_FIXTURE
        return 64;
#else
        const auto out=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(out);
        const auto calibration=rtfixture::initializeSharedCryptoForMeasurement();nlohmann::json value;
        try{value=rtfixture::loaderStartReportAllocation(out);}catch(const std::exception& error){value={{"passed",false},{"error",error.what()}};}
        value["sharedCryptoInitialization"]=calibration;
        std::cout<<"LOADER_START_REPORT_RESULT "<<value.dump()<<std::endl;return value.value("passed",false)?0:1;
#endif
    }
    if(argc==4&&std::string_view(argv[3]).starts_with("--loader-exit-report=")){
        if(std::string_view(argv[2])!="native.typed-runtime-live-watch"||(std::string_view(argv[3])!="--loader-exit-report=memory"&&std::string_view(argv[3])!="--loader-exit-report=memory-prewait"))return 64;
#ifndef YMCC_DECKY_REPORT_ALLOCATION_FIXTURE
        return 64;
#else
        const auto out=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(out);
        const auto calibration=rtfixture::initializeSharedCryptoForMeasurement();
        nlohmann::json value;
        try{value=rtfixture::loaderExitReportAllocation(out,std::string_view(argv[3])=="--loader-exit-report=memory-prewait");}
        catch(const std::exception& error){value={{"passed",false},{"error",error.what()}};}
        value["registrationId"]="native.typed-runtime-live-watch";value["parameter"]=std::string_view(argv[3])=="--loader-exit-report=memory-prewait"?"loader-exit-report-memory-prewait":"loader-exit-report-memory";
        value["sharedCryptoInitialization"]=calibration;value["realSteamOrDeviceActions"]=0;
        std::ofstream(out.parent_path()/"loader-exit-report-result.json",std::ios::binary)<<value.dump(2)<<'\n';
        std::cout<<value.dump(2)<<'\n';return value.value("passed",false)?0:1;
#endif
    }
    if(argc==4&&std::string_view(argv[3]).starts_with("--initial-update-backend=")){
        if(std::string_view(argv[2])!="native.typed-ready-rebind")return 64;
        const auto variant=std::string(argv[3]).substr(std::string("--initial-update-backend=").size());
        const bool notification=variant=="backend-notify"||variant=="backend-notify-closed-client";
        if(!notification&&variant!="ready"&&variant!="deadline"&&variant!="off"&&variant!="boundaries"&&variant!="backend-outage"&&variant!="backend-outage-sidebar-only"&&variant!="backend-recovers"&&variant!="backend-off"&&variant!="backend-boundaries"&&variant!="backend-report-memory")return 64;
#ifndef YMCC_DECKY_REPORT_ALLOCATION_FIXTURE
        if(variant=="backend-report-memory")return 64;
#endif
        const auto out=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(out);
        const auto calibration=rtfixture::initializeSharedCryptoForMeasurement();
        nlohmann::json value;
        try{
#ifdef YMCC_DECKY_REPORT_ALLOCATION_FIXTURE
            if(variant=="backend-report-memory"){
                // Existing backend-outage body and resource baseline remain exact.
                // This parameter injects only one optional-report allocation fault.
                reportAllocationProbe::mode=1;reportAllocationProbe::budget=1;reportAllocationProbe::hits=0;
                ymcc::deckybinding::fixture::constructionprobe::failures=0;
                ymcc::deckybinding::fixture::constructionprobe::failedBytes=0;
                value=rtfixture::retainedOfficialBackend(out,"backend-outage");
                value["sourceVariant"]="backend-outage";
                value["optionalReportFault"]={{"hits",reportAllocationProbe::hits.load()},
                    {"failures",ymcc::deckybinding::fixture::constructionprobe::failures.load()},
                    {"bytes",ymcc::deckybinding::fixture::constructionprobe::failedBytes.load()},
                    {"action",reportAllocationProbe::lastAction.load()}};
                if(reportAllocationProbe::hits!=1||ymcc::deckybinding::fixture::constructionprobe::failures!=1)value["passed"]=false;
            }else
#endif
            value=notification?rtfixture::retainedBackendNotification(out,variant):variant.starts_with("backend-")?rtfixture::retainedOfficialBackend(out,variant):rtfixture::initialUpdateBackend(out,variant);
        }
        catch(const std::exception& error){value={{"passed",false},{"error",error.what()}};}
        value["registrationId"]="native.typed-ready-rebind";value["parameter"]="initial-update-backend";
        value["variant"]=variant;value["sharedCryptoInitialization"]=calibration;
        std::ofstream file(out.parent_path()/"initial-update-result.json");file<<value.dump(2)<<'\n';file.close();
        std::cout<<"INITIAL_UPDATE_RESULT "<<value.dump()<<'\n'<<std::flush;
        return value.value("passed",false)?0:1;
    }
    const std::string_view finalAdmissionPrefix="--final-admission=";
    std::string_view finalAdmissionVariant;
    if(argc==4&&std::string_view(argv[3]).starts_with(finalAdmissionPrefix)){
        if(std::string_view(argv[2])!="native.typed-rejected-final-admission")return 64;
        finalAdmissionVariant=std::string_view(argv[3]).substr(finalAdmissionPrefix.size());
        if(finalAdmissionVariant!="connection-same"&&finalAdmissionVariant!="connection-page"&&
           finalAdmissionVariant!="connection-off"&&finalAdmissionVariant!="mode-same"&&finalAdmissionVariant!="local-busy"&&
           finalAdmissionVariant!="local-busy-off"&&finalAdmissionVariant!="local-busy-page"&&
           finalAdmissionVariant!="transport-busy"&&finalAdmissionVariant!="malformed-busy"&&finalAdmissionVariant!="connection-early"&&
           finalAdmissionVariant!="connection-preflight-off")return 64;
    }
    const auto parameter=argc==4?std::string_view(argv[3]):std::string_view{};
    const bool retiredOfflineSelection=parameter=="--retired-offline-proof";
    if(retiredOfflineSelection&&std::string_view(argv[2])!="native.typed-offline-current-ui-account-consumers")return 64;
    const bool deferredPreflightSelection=parameter=="--preparation-revalidation=deferred-session";
    if(deferredPreflightSelection&&std::string_view(argv[2])!="native.typed-preparation-revalidation")return 64;
    const auto pageGrantSelection=parameter=="--retained-verified-fault-only=await-identity"?"await-identity":
        parameter=="--retained-verified-fault-only=late-health"?"late-health":"";
    if(*pageGrantSelection&&std::string_view(argv[2])!="native.typed-ready-rebind")return 64;
    if(!retiredOfflineSelection&&!deferredPreflightSelection&&!*pageGrantSelection&&finalAdmissionVariant.empty()&&argc!=2&&argc!=3&&!(argc==4&&(std::string_view(argv[3])=="--loader-native-close=joint-live"||std::string_view(argv[3])=="--loader-native-close=joint"||std::string_view(argv[3])=="--loader-native-close=final"||std::string_view(argv[3])=="--loader-native-close=reentry"||std::string_view(argv[3])=="--loader-native-close=paused"||std::string_view(argv[3]).rfind("--pre-result-retirement=",0)==0||std::string_view(argv[3])=="--global-missed-event"||std::string_view(argv[3])=="--terminal-reconnect"||std::string_view(argv[3])=="--retained-refresh"||std::string_view(argv[3])=="--retained-busy"||std::string_view(argv[3])=="--retained-first-proof"||std::string_view(argv[3])=="--retained-local-busy"||std::string_view(argv[3])=="--presence-publication"||std::string_view(argv[3])=="--queued-stable-only"||std::string_view(argv[3])=="--watch-reattach-only"||std::string_view(argv[3])=="--fresh-handshake-only"||std::string_view(argv[3])=="--retained-handshake-only"||std::string_view(argv[3])=="--retained-first-peer-only"||std::string_view(argv[3])=="--retained-verified-fault-only")))return 64;
    if(argc==4&&std::string_view(argv[3])=="--watch-reattach-only"&&std::string_view(argv[2])!="native.typed-ready-rebind")return 64;
    if(argc==4&&std::string_view(argv[3])=="--queued-stable-only"&&std::string_view(argv[2])!="native.typed-ready-rebind")return 64;
    if(argc==4&&std::string_view(argv[3])=="--fresh-handshake-only"&&std::string_view(argv[2])!="native.typed-ready-rebind")return 64;
    if(argc==4&&(std::string_view(argv[3])=="--retained-handshake-only"||std::string_view(argv[3])=="--retained-first-peer-only"||std::string_view(argv[3])=="--retained-verified-fault-only")&&std::string_view(argv[2])!="native.typed-ready-rebind")return 64;
    if(argc==4&&std::string_view(argv[3])=="--terminal-reconnect"&&std::string_view(argv[2])!="native.typed-admission-park-resume")return 64;
    if(argc==4&&std::string_view(argv[3])=="--presence-publication"&&std::string_view(argv[2])!="native.typed-offline-current-ui-account-consumers")return 64;
    if(argc==4&&(std::string_view(argv[3])=="--retained-first-proof"||std::string_view(argv[3])=="--retained-local-busy")&&std::string_view(argv[2])!="native.typed-offline-current-ui-account-consumers")return 64;
    if(argc==4&&(std::string_view(argv[3])=="--retained-refresh"||std::string_view(argv[3])=="--retained-busy")&&std::string_view(argv[2])!="native.typed-offline-current-ui-account-consumers")return 64;
    using namespace rtfixture;const auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);
    if(argc==4&&std::string_view(argv[3]).rfind("--pre-result-retirement=",0)==0){
        preResultRetirementProbe=std::string(argv[3]).substr(std::string("--pre-result-retirement=").size());
        if(preResultRetirementProbe!="cleanup-denied"&&preResultRetirementProbe!="cleanup-final-pending"&&preResultRetirementProbe!="callback"&&preResultRetirementProbe!="context")return 64;
        const std::string selected=argv[2];
        if(selected.find("native.typed-history-session-")!=0)return 64;
    }
    const std::string selection=argc>=3?argv[2]:"";
    if(argc==4&&std::string_view(argv[3]).starts_with("--loader-native-close=")){
#ifdef YMCC_BINDING_LOADER_NATIVE_CLOSE_FIXTURE
        loaderCloseProbe::mode=std::string(argv[3]).substr(22);
        if(selection!="native.typed-runtime-live-watch"||(loaderCloseProbe::mode!="final"&&loaderCloseProbe::mode!="reentry"&&loaderCloseProbe::mode!="paused"&&loaderCloseProbe::mode!="joint"&&loaderCloseProbe::mode!="joint-live"))return 64;
#else
        return 64;
#endif
    }
    runGlobalMissedEventProbe=argc==4&&std::string_view(argv[3])=="--global-missed-event";runTerminalReconnectProbe=argc==4&&std::string_view(argv[3])=="--terminal-reconnect";runRetainedFirstProofProbe=argc==4&&(std::string_view(argv[3])=="--retained-first-proof"||std::string_view(argv[3])=="--retained-local-busy");runRetainedLocalBusyProbe=argc==4&&std::string_view(argv[3])=="--retained-local-busy";runRetainedRefreshProbe=argc==4&&(std::string_view(argv[3])=="--retained-refresh"||std::string_view(argv[3])=="--retained-busy");runRetainedBusyProbe=argc==4&&std::string_view(argv[3])=="--retained-busy";runPresencePublicationProbe=argc==4&&std::string_view(argv[3])=="--presence-publication";runQueuedStableProbe=argc==4&&std::string_view(argv[3])=="--queued-stable-only";runFreshHandshakeProbe=argc==4&&std::string_view(argv[3])=="--fresh-handshake-only";runRetainedHandshakeProbe=argc==4&&(std::string_view(argv[3])=="--retained-handshake-only"||std::string_view(argv[3])=="--retained-first-peer-only"||std::string_view(argv[3])=="--retained-verified-fault-only");runRetainedVerifiedFaultProbe=argc==4&&std::string_view(argv[3])=="--retained-verified-fault-only";runRetainedFirstPeerProbe=runRetainedVerifiedFaultProbe||(argc==4&&std::string_view(argv[3])=="--retained-first-peer-only");
    DeferredResourceBatch deferred(output/"deferred-resource-branch",selection);deferredResourceBatch=&deferred;
    const auto sharedCryptoInitialization=initializeSharedCryptoForMeasurement();
    Json cases=Json::array(),caseObservations=Json::array();
    const auto selected=[&](const char* name){
        if(selection.empty())return true;
        for(std::size_t pos=0;pos<selection.size();){
            const auto end=selection.find(',',pos);const auto part=selection.substr(pos,end==std::string::npos?end:end-pos);
            if(!part.empty()&&std::string(name).find(part)!=std::string::npos)return true;
            if(end==std::string::npos)break;pos=end+1;
        }
        return false;
    };
    const auto test=[&](const char* name,const auto& run){
        if(!selected(name))return;
        const auto began=std::chrono::steady_clock::now();
        try{cases.push_back(run());}catch(const std::exception& error){cases.push_back({{"id",name},{"passed",false},{"error",error.what()}});}
        const auto& row=cases.back();
        Json observation{{"ordinal",caseObservations.size()+1},{"registrationId",name},{"resultId",row.value("id",std::string(name))},
            {"passed",row.value("passed",false)},{"bodyElapsedMs",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count()}};
        caseObservations.push_back(observation);
        std::cout<<"YMCC_TYPED_CASE "<<observation.dump()<<'\n'<<std::flush;
    };
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
    const auto deferredResourceExecution=deferred.summary();
    for(const auto retained:{false,true})test(retained?"native.typed-history-session-false-retained":"native.typed-history-session-false-owned",[&]{return sessionRevalidation(output/(retained?"session-false-retained":"session-false-owned"),"false",retained);});
    for(const auto variant:{"true","null","missing","string","number","off","admission"}){
        const auto name=std::string("native.typed-history-session-")+variant+"-retained";
        test(name.c_str(),[&]{return sessionRevalidation(output/(std::string("session-")+variant),variant,true);});
    }
    test("native.typed-offline-current-ui-account-consumers",[&]{return retiredOfflineSelection?retiredOfflineFirstProof(output/"retired-offline"):offlineIdentity(output/"offline-identity");});
    test("native.typed-loader-temp-path-budget",[&]{return loaderTempPathBudget(output/"temp-path");});
    test("native.typed-admission-park-resume",[&]{return admissionParkResume(output/"admission-park");});
    test("native.typed-runtime-live-watch",[&]{return healthy(output/"healthy");});
    test("native.typed-stalled-binding-stop",[&]{return stalled(output/"stalled-cv",L"runtime-stall-cv");});
    test("native.typed-stalled-binding-descendants",[&]{return stalled(output/"stalled-descendants",L"stubborn-descendant");});
    test("native.typed-rejected-final-admission",[&]{return rejectedFinalAdmission(output/"final-admission",finalAdmissionVariant);});
    test("native.typed-preparation-revalidation",[&]{return deferredPreflightSelection?deferredPreflightSession(output/"deferred-session"):rejectedAfterPreparation(output/"preparation-admission");});
    test("native.typed-s0-intent-ready-owned",[&]{return intentReady(output/"intent-ready-owned",false);});
    test("native.typed-s0-intent-ready-retained",[&]{return intentReady(output/"intent-ready-retained",true);});
    test("native.typed-ready-rebind",[&]{
        if(argc==4&&std::string_view(argv[3])=="--watch-reattach-only")return watchReattachHandshake(output/"wr");
        if(*pageGrantSelection){
            auto rows=retainedPageGrant(output/"page-grant",pageGrantSelection);
            const bool passed=rows.size()==(std::string_view(pageGrantSelection)=="late-health"?2u:1u)&&
                std::all_of(rows.begin(),rows.end(),[](const auto& row){return row.value("passed",false);});
            return Json{{"id","native.typed-ready-rebind"},{"passed",passed},{"parameter",pageGrantSelection},
                {"reservedPageScenarios",std::move(rows)},{"realSteamOrDeviceActions",0}};
        }
        if(runRetainedHandshakeProbe){
            auto result=retainedBindingHandshake(output/"retained-handshake");
            if(runRetainedVerifiedFaultProbe){
                auto rows=retainedPageGrant(output/"page-grant");
                result["passed"]=result.value("passed",false)&&rows.size()==5&&
                    std::all_of(rows.begin(),rows.end(),[](const auto& row){return row.value("passed",false);});
                result["reservedPageScenarios"]=std::move(rows);
            }
            return result;
        }

        if(runFreshHandshakeProbe)return freshBindingHandshake(output/"fresh-handshake");
        if(runQueuedStableProbe)return queuedRecoveryHealth(output/"queued-stable",true);
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
    for(const bool retained:{false,true}){
        const auto name=retained?"native.typed-healing-child-exit-retained":"native.typed-healing-child-exit-owned";
        test(name,[&]{
            auto normal=healing(output/(retained?"exit-retained":"exit-owned"),retained,"child-exit");
            if(!normal.value("passed",false))return normal;
            auto held=healing(output/(retained?"held-exit-retained":"held-exit-owned"),retained,"child-exit",true);
            normal["passed"]=held.value("passed",false);normal["heldEndpointRecovery"]=std::move(held);return normal;
        });
    }
    const auto passed=std::count_if(cases.begin(),cases.end(),[](const Json& row){return row.value("passed",false);});
    const Json result{{"schema","ymcc.typed-binding-runtime-gate.v1"},{"passed",!cases.empty()&&static_cast<std::size_t>(passed)==cases.size()},
        {"selection",selection},{"tests",cases.size()},{"passedCount",passed},{"cases",cases},{"caseObservations",caseObservations},{"deadlineMs",3200},{"productCallbackMode","typed-only; legacy macro absent"},
        {"sharedCryptoInitialization",sharedCryptoInitialization},
        {"deferredResourceExecution",deferredResourceExecution},
        {"realSteamOrDeviceActions",0},{"originalHistoricalFailurePreserved",true},{"fieldWakeOrS4Verified",false},{"hostPackagePassed",false}};
    std::ofstream(output.parent_path()/"typed-runtime-result.json",std::ios::binary)<<result.dump(2)<<'\n';
    std::cout<<result.dump(2)<<'\n';return result.at("passed").get<bool>()?0:1;
}
