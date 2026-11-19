#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <iostream>
#include <new>
#include <string>
#include <thread>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
static std::atomic<unsigned long long> allocations{0};
static std::atomic<bool> failAllocation{false};
void* operator new(std::size_t size) {
    if(failAllocation.load(std::memory_order_relaxed))throw std::bad_alloc();
    allocations.fetch_add(1,std::memory_order_relaxed);
    if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();
}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
static std::uint64_t fixtureTick=100;
static ULONGLONG witnessFixtureTick() noexcept{return fixtureTick;}
static DWORD witnessFixturePid() noexcept{return 77;}
static void witnessFixtureTime(LPSYSTEMTIME time) noexcept{*time={2026,11,1,16,12,34,56,789};}
static std::string nativeLifecycleTimestamp(){return "fixed-producer-time";}
static const std::string* inputHostCriticalDiagnosticReplayTime() noexcept;
static const std::string* inputHostCriticalDiagnosticReplayDomainPayload() noexcept;
static bool poolTrySubmitExisting(std::function<void()>);
static void appendNativeLifecyclePrepared(const char*,const json&,bool,const std::string*);
static void mirrorLifecycleToDomains(const char*,const json&);
static void appendDomainLog(const char*,const char*,const json&);
static void writeLog(const std::string&,const std::string&);
// Explicit deterministic metadata ports; final disk sinks and existing pool
// are value ports. The complete original Runtime/Dispatcher bodies are included.
#define GetTickCount64 witnessFixtureTick
#define GetCurrentProcessId witnessFixturePid
#define GetLocalTime witnessFixtureTime
#include "input_host_diagnostic_runtime.h"
#undef GetLocalTime
#undef GetCurrentProcessId
#undef GetTickCount64
using namespace inputHostCriticalDiagnostics;
enum class SubmitMode { Hold, Refuse, DropAccepted, Reenter, ThrowAllocation, ThrowArrayLength, ThrowStandard, ThrowUnknown };
static SubmitMode submitMode=SubmitMode::Hold;
static std::vector<std::function<void()>> jobs;
static json sinks=json::array();
static unsigned submitCalls=0;
static bool witnessChecks=true;
static bool poolTrySubmitExisting(std::function<void()> job) {
    ++submitCalls;
    if(submitMode==SubmitMode::ThrowAllocation)throw std::bad_alloc();
    if(submitMode==SubmitMode::ThrowArrayLength)throw std::bad_array_new_length();
    if(submitMode==SubmitMode::ThrowStandard)throw std::runtime_error("DO-NOT-COPY-WHAT");
    if(submitMode==SubmitMode::ThrowUnknown)throw 981;
    if(submitMode==SubmitMode::Refuse)return false;
    if(submitMode==SubmitMode::DropAccepted)return true;
    if(submitMode==SubmitMode::Reenter)inputHostDeferNativeDiagnostic("busy-mirror",{{"event","busy-mirror"}},false,true);
    jobs.push_back(std::move(job));return true;
}
static void appendNativeLifecyclePrepared(const char* event,const json& detail,bool base,const std::string* encoded) {
    const auto* time=inputHostCriticalDiagnosticReplayTime();
    sinks.push_back({{"sink","native"},{"event",event},{"detail",detail},{"base",base},
        {"encoded",encoded?*encoded:"MISSING"},{"time",time?*time:"MISSING"}});
}
static void mirrorLifecycleToDomains(const char* event,const json& detail) {
    const auto* time=inputHostCriticalDiagnosticReplayTime();
    sinks.push_back({{"sink","mirror"},{"event",event},{"detail",detail},{"time",time?*time:"MISSING"}});
}
static void appendDomainLog(const char* domain,const char* event,const json& detail) {
    const auto* time=inputHostCriticalDiagnosticReplayTime();const auto* encoded=inputHostCriticalDiagnosticReplayDomainPayload();
    sinks.push_back({{"sink","domain"},{"domain",domain},{"event",event},{"detail",detail},
        {"encoded",encoded?*encoded:"MISSING"},{"time",time?*time:"MISSING"}});
}
static void writeLog(const std::string& level,const std::string& message) {
    const auto* time=inputHostCriticalDiagnosticReplayTime();
    sinks.push_back({{"sink","app"},{"level",level},{"message",message},{"time",time?*time:"MISSING"}});
}
static void drainJobs(){while(!jobs.empty()){auto job=std::move(jobs.front());jobs.erase(jobs.begin());job();}}
static void disposeOwner() {
    inputHostCriticalDiagnosticsClose();jobs.clear();
    auto* prior=queue.exchange(nullptr,std::memory_order_acq_rel);
    if(prior){const auto s=prior->snapshot();if(s.pendingRows || s.payloadBytes || s.sinkActive)std::abort();delete prior;}
}
static void freshOwner() {
    disposeOwner();
#ifndef YMCC_WITNESS_OLD
    // Private fixture phase reset only, after all owned callbacks are disposed.
    // Production has no reset/restart operation for this witness storage.
    for(auto& slot:postWitnesses){slot.state.store(0);slot.kind=0;slot.prefixBytes=0;slot.nameBytes=0;slot.truncated=false;slot.sanitized=false;}
#endif
    unavailable.store(0);preparationFailed.store(0);sinks=json::array();submitCalls=0;submitMode=SubmitMode::Hold;fixtureTick=100;
    inputHostCriticalDiagnosticsStartup();if(!queue.load())std::abort();
}
static json evidence(){return inputHostCriticalDiagnosticsEvidence();}
static json witnessRow(const json& e,const char* reason) {
    if(!e.contains("postRejectionWitnesses") || !e["postRejectionWitnesses"].is_object())return nullptr;
    for(const auto& row:e["postRejectionWitnesses"]["rows"])if(row["reason"]==reason)return row;
    return nullptr;
}
static bool witness(const json& e,const char* reason,const std::string& name,const char* kind) {
#ifdef YMCC_WITNESS_OLD
    (void)e;(void)reason;(void)name;(void)kind;return true;
#else
    const auto row=witnessRow(e,reason);
    return row.is_object() && row["state"]=="published" && row["namePrefix"]==name && row["recordKind"]==kind;
#endif
}
static void rawOffer(const char* name,bool base=false) {
    Record record;record.name=name;record.encoded="{}";record.baseEvent=base;offer(std::move(record));
}
static int runSubmissionKinds() {
    DWORD handlesBefore=0,handlesAfter=0;GetProcessHandleCount(GetCurrentProcess(),&handlesBefore);
    json checks=json::array(),phases=json::array();bool typedComplete=true;
    auto check=[&](bool ok,const char* name){checks.push_back({{"name",name},{"pass",ok}});if(!ok)throw std::runtime_error(name);};
    auto phase=[&](const char* name){auto value=evidence();phases.push_back({{"name",name},{"evidence",value},{"submitCalls",submitCalls},{"sinks",sinks}});return value;};
    auto typed=[&](const json& value,const char* name,const char* kind){
        const auto row=witnessRow(value,"submission-not-accepted");
        typedComplete=typedComplete && row.is_object() && row.value("namePrefix",std::string{})==name
            && row.value("submissionFailureKind",std::string{})==kind;
    };
    try {
        freshOwner();rawOffer("ok");drainJobs();auto value=phase("accepted");
        check(value["accepted"]==1 && value["sinkReturned"]==1 && value["submitFailed"]==0 && witnessRow(value,"submission-not-accepted")["state"]=="empty","accepted-submit-has-no-failure-kind");

        freshOwner();submitMode=SubmitMode::Refuse;rawOffer("refuse",true);value=phase("returned-false");
        check(value["submitFailed"]==1 && value["pendingDiscarded"]==1 && value["accepted"]==0 && value["preparationFailed"]==0 && submitCalls==1,"returned-false-keeps-original-counters");
        typed(value,"refuse","returned-false");
        const auto allocationsBefore=allocations.load();
        for(unsigned i=0;i<1000;++i){fixtureTick+=1000;rawOffer("later");}
        const auto allocationsAfter=allocations.load();value=phase("repeated-false");
        phases.back()["allocationDelta"]=allocationsAfter-allocationsBefore;
        check(value["offered"]==1001 && value["submitFailed"]==1001 && value["pendingDiscarded"]==1001 && value["rejectedCooldown"]==0 && allocationsAfter>=allocationsBefore,"1000-repeat-failures-allocation-profile-for-paired-check");
        typed(value,"refuse","returned-false");

        freshOwner();submitMode=SubmitMode::ThrowAllocation;rawOffer("executor-alloc");value=phase("executor-bad-alloc");
        check(value["submitFailed"]==1 && value["pendingDiscarded"]==1 && value["accepted"]==0 && value["preparationFailed"]==0 && submitCalls==1,"executor-allocation-exception-stays-submission");
        typed(value,"executor-alloc","allocation-exception");

        freshOwner();submitMode=SubmitMode::ThrowArrayLength;rawOffer("array-length");value=phase("executor-array-length");
        check(value["submitFailed"]==1 && value["pendingDiscarded"]==1 && value["accepted"]==0 && submitCalls==1,"allocation-derived-exception-is-caught-as-allocation");
        typed(value,"array-length","allocation-exception");

        freshOwner();submitMode=SubmitMode::ThrowStandard;inputHostDeferNativeDiagnostic("standard",{{"privateBody","DO-NOT-COPY-BODY"}},false,false);value=phase("executor-standard");
        check(value["submitFailed"]==1 && value["pendingDiscarded"]==1 && value["preparationFailed"]==0 && submitCalls==1
            && value.dump().find("DO-NOT-COPY-WHAT")==std::string::npos && value.dump().find("DO-NOT-COPY-BODY")==std::string::npos,"standard-exception-without-what-or-record-body");
        typed(value,"standard","standard-exception");

        freshOwner();submitMode=SubmitMode::ThrowUnknown;rawOffer("unknown-throw");value=phase("executor-unknown");
        check(value["submitFailed"]==1 && value["pendingDiscarded"]==1 && value["accepted"]==0 && value["preparationFailed"]==0 && submitCalls==1,"nonstandard-exception-original-noexcept-return");
        typed(value,"unknown-throw","unknown-exception");

        freshOwner();Record prepared;prepared.name="callback-alloc";prepared.encoded="{}";
        failAllocation.store(true);offer(std::move(prepared));failAllocation.store(false);value=phase("callback-allocation");
        check(value["submitFailed"]==1 && value["pendingDiscarded"]==1 && value["accepted"]==0 && value["preparationFailed"]==0 && submitCalls==0,"actual-callback-allocation-failure-before-executor");
        typed(value,"callback-alloc","allocation-exception");

        freshOwner();submitMode=SubmitMode::ThrowStandard;rawOffer("first-standard");
        fixtureTick=1100;submitMode=SubmitMode::ThrowAllocation;rawOffer("later-alloc");
        fixtureTick=2100;submitMode=SubmitMode::Refuse;rawOffer("later-false");value=phase("mixed-first-claim");
        check(value["submitFailed"]==3 && value["pendingDiscarded"]==3 && value["rejectedCooldown"]==0 && submitCalls==3,"later-submission-kinds-do-not-replace-first-witness");
        typed(value,"first-standard","standard-exception");

        freshOwner();submitMode=SubmitMode::ThrowUnknown;rawOffer("cooldown-kind");
        fixtureTick=99;rawOffer("backwards");fixtureTick=1099;rawOffer("before-expiry");
        fixtureTick=1100;submitMode=SubmitMode::Hold;rawOffer("expiry-success");drainJobs();value=phase("typed-cooldown-expiry");
        check(value["submitFailed"]==1 && value["rejectedCooldown"]==2 && value["accepted"]==1 && value["sinkReturned"]==1 && submitCalls==2,"original-backwards-cooldown-and-exact-expiry-preserved");
        typed(value,"cooldown-kind","unknown-exception");

        freshOwner();submitMode=SubmitMode::Refuse;rawOffer("retain-evidence");
        failAllocation.store(true);const bool optionalWasNull=evidence().is_null();failAllocation.store(false);value=phase("optional-evidence-allocation");
        check(optionalWasNull && value["submitFailed"]==1 && value["accepted"]==0,"optional-json-allocation-does-not-change-stored-witness");typed(value,"retain-evidence","returned-false");

        freshOwner();postWitnesses[5].state.store(1,std::memory_order_release);value=phase("publishing");
        check(witnessRow(value,"submission-not-accepted")["state"]=="publishing" && witnessRow(value,"submission-not-accepted").size()==2,"publishing-never-reads-submission-payload");
        disposeOwner();GetProcessHandleCount(GetCurrentProcess(),&handlesAfter);check(handlesBefore==handlesAfter,"all-owned-callbacks-and-handles-returned");
        checks.push_back({{"name","typed-submission-first-witness-required"},{"pass",typedComplete}});
        std::cout<<json({{"checks",checks},{"phases",phases},{"handlesBefore",handlesBefore},{"handlesAfter",handlesAfter},{"queueBytes",sizeof(Queue)},{"witnessBytes",sizeof(postWitnesses)},
            {"metadataPorts","tick/local-time/pid;pool/final-sinks-value-only"},{"syntheticThrownTypesAreOsCause",false},{"hardwareActions",0}}).dump()<<std::endl;
        return typedComplete?0:2;
    }catch(const std::exception& error){failAllocation.store(false);disposeOwner();std::cout<<json({{"checks",checks},{"phases",phases},{"error",error.what()}}).dump()<<std::endl;return 3;}
}

int wmain(int argc,wchar_t** argv) {
    if(argc==2 && std::wstring(argv[1])==L"--submission-kinds")return runSubmissionKinds();
    if(argc!=1)return 2;
    DWORD handlesBefore=0,handlesAfter=0;GetProcessHandleCount(GetCurrentProcess(),&handlesBefore);
    json checks=json::array(),phases=json::array();
    auto check=[&](bool ok,const char* name){checks.push_back({{"name",name},{"pass",ok}});if(!ok)throw std::runtime_error(name);};
    auto phase=[&](const char* name){const auto e=evidence();phases.push_back({{"name",name},{"evidence",e},{"sinks",sinks},{"submitCalls",submitCalls}});return e;};
    try {
        freshOwner();
        inputHostDeferNativeDiagnostic("native-ok",{{"event","native-ok"},{"time","original-time"}},true,false);
        inputHostDeferNativeDiagnostic("mirror-ok",{{"event","mirror-ok"}},false,true);
        inputHostDeferDomainDiagnostic("domain-port","domain-ok",{{"v",1}});
        inputHostDeferAppDiagnostic("INFO","body-only-at-sink");drainJobs();
        auto e=phase("normal");bool empty=true;
#ifndef YMCC_WITNESS_OLD
        for(const auto& row:e["postRejectionWitnesses"]["rows"])empty=empty && row["state"]=="empty" && row.size()==2;
#endif
        check(e["offered"]==4 && e["accepted"]==4 && e["sinkReturned"]==4 && submitCalls==1 && sinks.size()==4
            && sinks[0]["time"]=="original-time" && sinks[2]["detail"]["writerPid"]==77 && empty
            && inputHostCriticalDiagnosticReplayTime()==nullptr && inputHostCriticalDiagnosticReplayDomainPayload()==nullptr,"normal-four-sinks-and-no-witness");

        freshOwner();submitMode=SubmitMode::Refuse;
        inputHostDeferNativeDiagnostic("submit-first",{{"time","t"}},true,false);
        fixtureTick=200;inputHostDeferNativeDiagnostic("cooldown-mirror",json::object(),false,true);
        fixtureTick=99;rawOffer("backwards");fixtureTick=1099;rawOffer("not-expired");
        submitMode=SubmitMode::Hold;fixtureTick=1100;rawOffer("at-expiry");drainJobs();e=phase("submit-cooldown");
        check(e["offered"]==5 && e["accepted"]==1 && e["submitFailed"]==1 && e["rejectedCooldown"]==3 && e["pendingDiscarded"]==1
            && e["sinkReturned"]==1 && submitCalls==2,"submission-cooldown-and-original-expiry");
        witnessChecks=witnessChecks && witness(e,"submission-not-accepted","submit-first","native-resident") && witness(e,"cooldown","cooldown-mirror","mirror");

        freshOwner();inputHostDeferDomainDiagnostic("do-not-capture-domain","large-domain",{{"secretBody",std::string(66000,'Z')}});e=phase("oversize");
        check(e["rejectedOversized"]==1 && e["accepted"]==0 && submitCalls==0,"oversized-domain-before-submit");
        witnessChecks=witnessChecks && witness(e,"oversized","large-domain","domain");

        freshOwner();for(unsigned i=0;i<32;++i)rawOffer("pending");rawOffer("detail-first");
        const auto allocatedBefore=allocations.load();failAllocation.store(true);
        for(unsigned i=0;i<50000;++i)rawOffer("repeat");
        failAllocation.store(false);const auto allocatedAfter=allocations.load();e=phase("full");
        check(e["accepted"]==32 && e["rejectedFull"]==50001 && e["pendingRows"]==32 && allocatedBefore==allocatedAfter && submitCalls==1,"full-and-50000-allocation-free-refusals");
        witnessChecks=witnessChecks && witness(e,"full","detail-first","native-detail");

        freshOwner();submitMode=SubmitMode::Reenter;rawOffer("outer");drainJobs();e=phase("busy");
        check(e["offered"]==2 && e["accepted"]==1 && e["rejectedBusy"]==1 && e["sinkReturned"]==1,"reentrant-busy-original-return");
        witnessChecks=witnessChecks && witness(e,"busy","busy-mirror","mirror");

        freshOwner();inputHostCriticalDiagnosticsClose();inputHostDeferAppDiagnostic("WARN","message-must-not-be-witnessed");e=phase("closed");
        check(e["rejectedClosed"]==1 && e["accepted"]==0 && submitCalls==0,"closed-app-without-submit");
        witnessChecks=witnessChecks && witness(e,"closed","WARN","app");

        freshOwner();submitMode=SubmitMode::DropAccepted;rawOffer("late-moved",true);e=phase("late-close");
        check(e["callbackAbandoned"]==1 && e["rejectedClosed"]==1 && e["pendingDiscarded"]==1 && e["accepted"]==0 && e["closed"]==true && e["pendingRows"]==0,"late-close-discard-and-original-moved-record");
        witnessChecks=witnessChecks && witness(e,"closed","late-moved","native-resident");

        freshOwner();Record prepared;prepared.name="alloc-submit";prepared.encoded="{}";
        failAllocation.store(true);offer(std::move(prepared));failAllocation.store(false);e=phase("allocation-submit");
        check(e["submitFailed"]==1 && e["preparationFailed"]==0 && e["pendingDiscarded"]==1 && e["accepted"]==0,"allocation-failure-in-original-submission");
        witnessChecks=witnessChecks && witness(e,"submission-not-accepted","alloc-submit","native-detail");

        freshOwner();inputHostCriticalDiagnosticsClose();std::string longName(120,'A');longName[0]='\x01';longName[2]='\x7f';longName[94]='\xff';rawOffer(longName.c_str());e=phase("prefix");
        check(e["rejectedClosed"]==1 && e["accepted"]==0,"bounded-name-does-not-change-refusal");
#ifndef YMCC_WITNESS_OLD
        auto row=witnessRow(e,"closed");std::string prefix(95,'A');prefix[0]='?';prefix[2]='?';prefix[94]='?';
        witnessChecks=witnessChecks && witness(e,"closed",prefix,"native-detail") && row["nameBytes"]==120 && row["nameTruncated"]==true && row["namePrefixSanitized"]==true;
#endif

        freshOwner();inputHostCriticalDiagnosticsClose();std::atomic<bool> start{false};std::vector<std::thread> threads;
        for(unsigned i=0;i<8;++i)threads.emplace_back([&,i]{const auto name="thread-"+std::to_string(i);while(!start.load(std::memory_order_acquire))std::this_thread::yield();for(unsigned j=0;j<5000;++j)rawOffer(name.c_str(),true);});
        start.store(true,std::memory_order_release);
#ifndef YMCC_WITNESS_OLD
        bool coherent=true;for(unsigned i=0;i<1000;++i){const auto observed=witnessRow(evidence(),"closed");if(observed["state"]=="published"){const auto name=observed["namePrefix"].get<std::string>();coherent=coherent && name.size()==8 && name.substr(0,7)=="thread-" && name[7]>='0' && name[7]<='7' && observed["nameBytes"]==8 && observed["recordKind"]=="native-resident";}}
#endif
        for(auto& thread:threads)thread.join();e=phase("concurrent");
        check(e["offered"]==40000 && e["rejectedClosed"]==40000 && e["accepted"]==0 && submitCalls==0,"concurrent-refusals-original-counts");
#ifndef YMCC_WITNESS_OLD
        witnessChecks=witnessChecks && coherent && witnessRow(e,"closed")["state"]=="published";
#endif

        using Default=ymcc::inputHost::diagnostics::Dispatcher<int>;
        auto refuse=[](std::function<void()>){return false;};auto sink=[](const int&){};
        Default two(refuse,sink);Default three(refuse,sink,10);Default four(refuse,sink,10,20);
        check(!two.tryPost(1,1,100) && !three.tryPost(1,11,100) && !four.tryPost(1,21,100)
            && two.snapshot().submitFailed==1 && three.snapshot().rejectedOversized==1 && four.snapshot().rejectedOversized==1,"original-default-constructors-compatible");
        disposeOwner();GetProcessHandleCount(GetCurrentProcess(),&handlesAfter);
        check(handlesBefore==handlesAfter,"owned-callbacks-and-handles-returned");
#ifdef YMCC_WITNESS_OLD
        witnessChecks=e.contains("postRejectionWitnesses") && e["postRejectionWitnesses"].is_object();
#endif
        checks.push_back({{"name","first-claim-witness-evidence-required"},{"pass",witnessChecks}});
        std::cout<<json({{"checks",checks},{"phases",phases},{"handlesBefore",handlesBefore},{"handlesAfter",handlesAfter},
            {"queueBytes",sizeof(Queue)},
#ifndef YMCC_WITNESS_OLD
            {"witnessBytes",sizeof(postWitnesses)},
#endif
            {"metadataPorts","tick/local-time/pid;pool/final-sinks-value-only"},{"privatePhaseResets",true},{"hardwareActions",0}}).dump()<<std::endl;
        return witnessChecks?0:2;
    }catch(const std::exception& error){failAllocation.store(false);disposeOwner();std::cout<<json({{"checks",checks},{"phases",phases},{"error",error.what()}}).dump()<<std::endl;return 3;}
}
