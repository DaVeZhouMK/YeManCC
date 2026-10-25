// R119 inert fixture. @@ blocks are extracted verbatim from the bound source.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <Xinput.h>
#include <intrin.h>
#include <atomic>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <new>
#include <string>
#include <vector>
#include "json.hpp"
using json = nlohmann::json;
enum Event { Context, Source, Tracking, ObserveAdapter, AcceptedAdapter, Latest,
    ConfiguredCopy, BoundCopy, Persona, Board, Repair, Eligible,
    RouteCompare, BoardFind, WindowClear, WindowObserve, WindowAccepted, WindowValid,
    Fresh, CanonicalFresh, Clock, SameSource, WindowAdvance, SinceInit, EventEnd };
static constexpr const char* eventNames[] = {"contexts","sourceProjections","trackingChecks","observeAdapters",
    "acceptedAdapters","latestSourceCalls","configuredPersonaCopies","boundPersonaCopies","personaCalls",
    "boardQueries","repairScopeQueries","sourceEligibilityQueries","routeStringComparisons","boardStringFinds",
    "windowClears","windowObserves","windowAccepts","windowValidChecks","freshChecks","canonicalFreshChecks","clockReads","sameSourceChecks","windowEndpointAdvances","windowSinceInitializations"};
enum Atom { PowerGeneration, Presentation, Topology, Identity, MsiRevision, Armed, Confirmed, SourceValid,
    MsiEnabled, MsiMatched, MsiPid, ExitFence, Revoked, ExternalSuspended, InputReady, FaultHold, IntentReset,
    Terminal, Pending, TuplePending, Attempts, HealthySince, ReleaseNeutral, PowerPhase, AtomEnd };
static constexpr const char* atomNames[] = {"powerGeneration","presentationGeneration","topology","identity","msiRevision",
    "msiArmed","msiConfirmed","msiSourceValid","msiEnabled","msiInterfaceMatched","msiPid","exitFence","powerRevoked",
    "externalSuspended","inputReady","faultHold","intentReset","terminalLogged","recoveryPending","tuplePending",
    "episodeAttempts","healthySince","releaseNeutral","powerPhase"};
struct Counts {
    std::array<unsigned long long,EventEnd> events{};
    std::array<unsigned long long,AtomEnd> loads{}, stores{};
    std::array<unsigned long long,3> locks{}, unlocks{}; // Host, persona, snapshot
    unsigned long long acquireLoads=0, relaxedLoads=0, releaseStores=0, otherOrders=0;
    unsigned long long copiedBytes=0, allocations=0, allocationBytes=0;
} counts;
static bool measureAllocations=false;
void* operator new(std::size_t n) {
    if(measureAllocations){++counts.allocations;counts.allocationBytes+=n;}
    if(void* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
[[maybe_unused]] static void event(Event e){
#if R119_COUNT
    ++counts.events[e];
#else
    (void)e;
#endif
}
#if R119_COUNT
#define R119_EVENT(e) event(e)
template<class T> struct ProbeAtomic {
    std::atomic<T> value; Atom id;
    ProbeAtomic(T v,Atom a):value(v),id(a){}
    T load(std::memory_order order=std::memory_order_seq_cst) const {
        ++counts.loads[id];
        if(order==std::memory_order_acquire)++counts.acquireLoads;
        else if(order==std::memory_order_relaxed)++counts.relaxedLoads;
        else ++counts.otherOrders;
        return value.load(order);
    }
    void store(T v,std::memory_order order=std::memory_order_seq_cst){
        ++counts.stores[id];
        if(order==std::memory_order_release)++counts.releaseStores;else ++counts.otherOrders;
        value.store(v,order);
    }
    void operator=(T v){store(v);}
};
#define ATOM(type,name,value,id) static ProbeAtomic<type> name{value,id}
struct FixtureMutex {
    std::mutex value; unsigned id;
    explicit FixtureMutex(unsigned n):id(n){}
    void lock(){++counts.locks[id];value.lock();}
    void unlock(){++counts.unlocks[id];value.unlock();}
};
static FixtureMutex g_inputHostMx{0},g_inputHostPersonaMx{1},g_inputCaptureGamepadSnapshotMx{2};
static ULONGLONG fakeNow=10000;
static ULONGLONG fixtureTick(){event(Clock);return fakeNow;}
#define GetTickCount64 fixtureTick
#else
#define R119_EVENT(e) ((void)0)
#define ATOM(type,name,value,id) static std::atomic<type> name{value}
using FixtureMutex=std::mutex;
static FixtureMutex g_inputHostMx,g_inputHostPersonaMx,g_inputCaptureGamepadSnapshotMx;
#endif
#include "input_host_recovery_health.h"
ATOM(unsigned long long,g_powerGeneration,1,PowerGeneration);
ATOM(unsigned long long,g_inputHostPresentationWorkGeneration,1,Presentation);
ATOM(unsigned,g_xinputTopologyEpoch,1,Topology);
ATOM(unsigned,g_xinputSlotIdentityEpoch,1,Identity);
ATOM(unsigned long long,g_msiMainSourceRevision,1,MsiRevision);
ATOM(bool,g_msiDinputRouteDinputArmed,true,Armed);
ATOM(bool,g_msiDinputRouteConfirmed,true,Confirmed);
ATOM(bool,g_msiDinputSourceValid,true,SourceValid);
struct {
#if R119_COUNT
    ProbeAtomic<bool> enabled{true,MsiEnabled},mainInterfaceMatched{true,MsiMatched};
    ProbeAtomic<unsigned> productId{0x1902,MsiPid};
#else
    std::atomic<bool> enabled{true},mainInterfaceMatched{true};std::atomic<unsigned> productId{0x1902};
#endif
} g_msiDinput;
static constexpr struct {unsigned pidDInput=0x1902;} kMsiClawCapability;
enum class YmccFamily {Unknown,MsiClaw,Other};
struct {YmccFamily family=YmccFamily::MsiClaw;std::string manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.",product="MS-1T91";} g_machineIdentity;
struct {bool enabled=true,virtualTargetEnabled=true;std::string virtualTargetPersona="elite";} g_realStickTest;
static std::string g_inputHostBoundPersona="elite";
ATOM(bool,g_inputHostExitFence,false,ExitFence);
ATOM(bool,g_inputHostPowerAdmissionRevoked,false,Revoked);
ATOM(bool,g_externalPriorityVirtualSuspended,false,ExternalSuspended);
ATOM(bool,g_inputReady,true,InputReady);
ATOM(bool,g_inputHostFaultHoldsStart,false,FaultHold);
ATOM(bool,g_inputHostRecoveryIntentReset,false,IntentReset);
ATOM(bool,g_inputHostRecoveryTerminalLogged,false,Terminal);
ATOM(bool,g_inputHostRecoveryPending,false,Pending);
ATOM(bool,g_inputHostTupleHealPending,false,TuplePending);
ATOM(int,g_inputHostRecoveryEpisodeAttempts,0,Attempts);
ATOM(ULONGLONG,g_inputHostHealthySinceTick,0,HealthySince);
ATOM(bool,g_physicalReleaseWindowNeutral,false,ReleaseNeutral);
enum class PowerLifecycle {Ready,Suspended,Resuming};
ATOM(PowerLifecycle,g_powerLifecycle,PowerLifecycle::Ready,PowerPhase);
static unsigned long long g_inputHostEpoch=7,g_inputHostBoundPowerGeneration=1;
static HANDLE g_inputHostProcess=nullptr;
static bool g_inputHostPrepared=false,g_inputHostNeutralized=false,g_inputHostTransportFault=true;
static DWORD fixtureProcessId=0;
// Runtime initialization prevents /O2 specializing plain production globals to
// compile-time constants. Only this fixture process ID is read, outside timing.
__declspec(noinline) static void configureRuntimeFacts(){
    fixtureProcessId=GetCurrentProcessId();const bool ready=fixtureProcessId!=0;
    g_inputHostProcess=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(fixtureProcessId));
    g_realStickTest.enabled=ready;g_realStickTest.virtualTargetEnabled=ready;
    g_inputHostPrepared=ready;g_inputHostNeutralized=ready;g_inputHostTransportFault=!ready;
    g_inputHostEpoch=ready?7:0;g_inputHostBoundPowerGeneration=ready?1:0;
    g_machineIdentity.family=ready?YmccFamily::MsiClaw:YmccFamily::Unknown;
}
static bool sgShouldKeepInputDuringStandby(){return false;}
static ymcc::input::recoveryHealth::Window g_inputHostRecoveryHealth;
static bool g_inputHostRecoveryHealthWasTracking=false;
@@SNAPSHOT@@
static InputCaptureGamepadSnapshot g_inputCaptureGamepadSnapshot;
static const InputCaptureGamepadSnapshot* t_commonFrameSource=nullptr;
@@FUNCTIONS@@
using ContextType=ymcc::input::recoveryHealth::Context;
using SourceType=ymcc::input::recoveryHealth::Source;
static ContextType savedContext;
static SourceType savedSource;
static volatile unsigned long long sink=0;
[[maybe_unused]] static void touchSample() {
    auto& s=g_inputCaptureGamepadSnapshot;
    ++s.readAttemptSequence;
    s.readStartedTick=s.readCompletedTick=GetTickCount64();
}
static void reset(const std::string& route,bool tracking=false) {
    g_inputHostRecoveryEpisodeAttempts=tracking?1:0;
    g_inputHostRecoveryTerminalLogged=false;g_inputHostRecoveryPending=false;g_inputHostTupleHealPending=false;
    g_inputHostRecoveryHealth.clear();g_inputHostRecoveryHealthWasTracking=false;g_inputHostHealthySinceTick=0;
    g_inputCaptureGamepadSnapshot={};auto& s=g_inputCaptureGamepadSnapshot;
    s.live=true;s.connectedMask=1;s.primarySlot=fixtureProcessId%4;s.inputSource=route;s.readAttemptSequence=10;
    s.readPowerGeneration=1;s.readTopologyGeneration=1;s.readIdentityGeneration=1;
    s.readContinuityGeneration=1;s.readMsiSourceRevision=1;s.readContextStable=true;
    s.readStartedTick=s.readCompletedTick=GetTickCount64();
    t_commonFrameSource=&s;
    savedContext=inputHostRecoveryHealthContextLocked(false);
    savedSource=inputHostRecoveryHealthSource(t_commonFrameSource,true);
    counts={};
}
__declspec(noinline) static void requestOnly() {
    const bool physicalSourceAdmitted=true,semanticNeutral=false;
@@REQUEST@@
    savedContext=recoveryHealthRequestContext;savedSource=recoveryHealthRequestSource;
    sink=recoveryHealthRequestContext.host+recoveryHealthRequestSource.sequence+recoveryHealthRequestSource.live;
}
__declspec(noinline) static void acceptedOnly() {
    const bool physicalSourceAdmitted=true,semanticNeutral=false;
    const auto& recoveryHealthRequestContext=savedContext;
    const auto& recoveryHealthRequestSource=savedSource;
@@ACCEPTED@@
    sink=g_inputHostRecoveryHealth.since()+g_inputHostRecoveryHealth.through();
}
__declspec(noinline) static void observeOnly() {
    const bool physicalSourceAdmitted=true,semanticNeutral=false;
@@OBSERVE@@
    sink=g_inputHostRecoveryHealth.since();
}
// Exactly the two caller-lock regions that surround health in inputHostSubmitPad.
// Frame assembly, transport, Host, receipt parsing and all device work are absent.
__declspec(noinline) static void combined(bool eligible=true,bool neutral=false) {
    const bool physicalSourceAdmitted=eligible,semanticNeutral=neutral;
    {
        std::lock_guard<FixtureMutex> lock(g_inputHostMx);
@@OBSERVE@@
    }
    {
        std::lock_guard<FixtureMutex> lock(g_inputHostMx);
@@REQUEST@@
@@ACCEPTED@@
    }
    sink=g_inputHostRecoveryHealth.since()+g_inputHostRecoveryHealth.through();
}
static json countsJson(const Counts& c) {
    json j=json::object(),loads=json::object(),stores=json::object();
    for(unsigned i=0;i<EventEnd;++i)j[eventNames[i]]=c.events[i];
    unsigned long long loadSum=0,storeSum=0;
    for(unsigned i=0;i<AtomEnd;++i){if(c.loads[i])loads[atomNames[i]]=c.loads[i];if(c.stores[i])stores[atomNames[i]]=c.stores[i];loadSum+=c.loads[i];storeSum+=c.stores[i];}
    j["atomicLoads"]=loadSum;j["atomicStores"]=storeSum;j["loadsByGlobal"]=loads;j["storesByGlobal"]=stores;
    j["acquireLoads"]=c.acquireLoads;j["relaxedLoads"]=c.relaxedLoads;j["releaseStores"]=c.releaseStores;j["otherAtomicOrders"]=c.otherOrders;
    j["hostLocks"]=c.locks[0];j["personaLocks"]=c.locks[1];j["snapshotLocks"]=c.locks[2];
    j["unlocks"]=c.unlocks;j["personaCopiedBytes"]=c.copiedBytes;j["allocations"]=c.allocations;j["allocationBytes"]=c.allocationBytes;
    return j;
}
static unsigned checks=0;
static void expect(bool v,const char* reason){++checks;if(!v)throw std::runtime_error(reason);}
template<class F> static Counts measure(F f){
    counts={};measureAllocations=true;f();measureAllocations=false;return counts;
}
static json tests(){
    json rows=json::array();
    for(const std::string route:{"xinput","xinput-any-connected","msi-dinput"}){
        reset(route);
        const auto request=measure(requestOnly);
        const auto accepted=measure(acceptedOnly);
        expect(g_inputHostRecoveryHealth.since()==g_inputCaptureGamepadSnapshot.readCompletedTick,"no-obligation accepted still establishes T1 timestamp");
        expect(g_inputHostHealthySinceTick.load()==g_inputHostRecoveryHealth.since(),"T1 published timestamp");
        const auto observe=measure(observeOnly);
        const auto reused=measure([]{combined();});
#if R119_COUNT
        fakeNow+=8;
#endif
        touchSample();
        const auto steady=measure([]{combined();});
        expect(g_inputHostRecoveryHealth.through()==g_inputCaptureGamepadSnapshot.readCompletedTick,"normal fresh read advances health endpoint");
#if R119_COUNT
        expect(request.events[Context]==1&&request.events[Source]==1,"unconditional request Context/Source must execute");
        expect(accepted.events[Context]==1&&accepted.events[AcceptedAdapter]==1,"accepted context still executes");
        expect(steady.events[Context]==2&&steady.events[Source]==1&&steady.events[Tracking]==2,"normal projection call counts");
        expect(steady.events[ConfiguredCopy]==0&&steady.events[BoundCopy]==0&&steady.copiedBytes==0,"normal health must not copy persona strings");
        expect(steady.locks[0]==2&&steady.locks[1]==0&&steady.locks[2]==0,"only existing caller Host locks on no-obligation path");
        expect(steady.allocations==0,"normal projection must not allocate");
        expect(reused.events[WindowAdvance]==0&&steady.events[WindowAdvance]==1,"reused and fresh sequence work distinguished");
        expect(steady.events[SameSource]==1,"current reader identity check counted");
        expect(steady.events[WindowAccepted]==1&&steady.events[WindowObserve]==1&&steady.events[WindowValid]==2,"actual header still executes");
        expect(observe.events[Context]==0&&observe.events[Source]==0&&observe.locks[1]==0&&observe.locks[2]==0,"idle observe adapter has no projection or nested lock");
#endif
        rows.push_back({{"route",route},{"tracking",false},{"request",countsJson(request)},{"acceptedFirst",countsJson(accepted)},
            {"observeIdle",countsJson(observe)},{"combinedReusedSample",countsJson(reused)},{"combinedSteadyFreshSample",countsJson(steady)}});
    }
#if R119_COUNT
    // Calibration, not a recovery correctness audit: force persona/snapshot work.
    reset("xinput",true);const auto control=measure(acceptedOnly);
    expect(control.events[ConfiguredCopy]==1&&control.events[BoundCopy]==1,"copy instrumentation must detect tracking persona copies");
    expect(control.locks[1]==2&&control.locks[2]==1,"lock instrumentation calibration");
    expect(control.copiedBytes==10,"two five-byte persona copies in calibration");
    // The empty/neutral row is a cost boundary only; no late-source/recovery cases.
    reset("msi-dinput");const auto neutral=measure([]{combined(true,true);});
    expect(g_inputHostHealthySinceTick.load()==0,"neutral projection has no T1 coverage");
    json boundaries=json::array();
    for(const std::string route:{"xinput","xinput-any-connected","msi-dinput","none"})
    for(const bool tracking:{false,true})
    for(const bool observeConsumer:{false,true}){
        reset(route,tracking);combined();
        const auto boundary=measure([&]{
            if(observeConsumer){
                std::lock_guard<FixtureMutex> lock(g_inputHostMx);
                inputHostRecoveryHealthObserveLocked(false);
            }else combined(true,true);
        });
        const auto outcome=json{{"since",g_inputHostRecoveryHealth.since()},{"through",g_inputHostRecoveryHealth.through()},
            {"complete250",g_inputHostRecoveryHealth.complete(250)},{"route",static_cast<unsigned>(g_inputHostRecoveryHealth.route())},{"incarnation",g_inputHostRecoveryHealth.incarnation()},{"continuity",g_inputHostRecoveryHealth.continuity()},{"healthySince",g_inputHostHealthySinceTick.load()},
            {"wasTracking",g_inputHostRecoveryHealthWasTracking}};
        expect(outcome["since"]==0&&outcome["through"]==0&&outcome["healthySince"]==0,"known-invalid consumers clear health");
        fakeNow+=8;touchSample();combined();
        const auto next=json{{"route",static_cast<unsigned>(g_inputHostRecoveryHealth.route())},{"incarnation",g_inputHostRecoveryHealth.incarnation()},{"continuity",g_inputHostRecoveryHealth.continuity()},{"since",g_inputHostRecoveryHealth.since()},{"through",g_inputHostRecoveryHealth.through()},
            {"healthySince",g_inputHostHealthySinceTick.load()},{"wasTracking",g_inputHostRecoveryHealthWasTracking}};
        boundaries.push_back({{"route",route},{"tracking",tracking},{"consumer",observeConsumer?"observe(false)":"submit semanticNeutral"},
            {"counts",countsJson(boundary)},{"outcome",outcome},{"nextNormalRead",next}});
    }
    return {{"passed",true},{"checks",checks},{"ineligibleConsumers",boundaries},{"sizes",{{"Context",sizeof(ContextType)},{"Source",sizeof(SourceType)},{"Window",sizeof(ymcc::input::recoveryHealth::Window)}}},{"rows",rows},{"trackingCalibration",countsJson(control)},{"neutralBoundary",countsJson(neutral)}};
#else
    return {{"passed",true},{"checks",checks},{"rows",rows}};
#endif
}
#if !R119_COUNT
static double threadCycles(){ULONG64 cycles=0;if(!QueryThreadCycleTime(GetCurrentThread(),&cycles))throw std::runtime_error("QueryThreadCycleTime");return static_cast<double>(cycles);}
static json benchmark(unsigned rounds,unsigned iterations){
    json rows=json::array();
    struct Stage{const char* name;void(*fn)();};
    for(const std::string route:{"xinput","xinput-any-connected","msi-dinput"})
    for(const Stage stage:std::array<Stage,6>{{{"observeIdle",observeOnly},{"request",requestOnly},{"acceptedReusedSample",acceptedOnly},{"acceptedNewSequencePlusOneIncrement",[]{++savedSource.sequence;acceptedOnly();}},{"combinedReusedSampleTwoExistingHostLocks",[]{combined();}},{"combinedNewSequenceTwoExistingHostLocksPlusOneIncrement",[]{++g_inputCaptureGamepadSnapshot.readAttemptSequence;combined();}}}}){
        reset(route);requestOnly();acceptedOnly();
        for(unsigned i=0;i<10000;++i){touchSample();savedSource=inputHostRecoveryHealthSource(t_commonFrameSource,true);stage.fn();}
        std::vector<double> cycleSamples,nsSamples;
        const auto batchSince=g_inputHostRecoveryHealth.since();
        LARGE_INTEGER freq;QueryPerformanceFrequency(&freq);
        // Sample preparation happens once before each batch. The benchmark is
        // fixed metadata projection; no reader/producer algorithm is measured.
        for(unsigned r=0;r<rounds;++r){
            g_inputCaptureGamepadSnapshot.readAttemptSequence=std::max(g_inputCaptureGamepadSnapshot.readAttemptSequence,savedSource.sequence);
            touchSample();savedSource=inputHostRecoveryHealthSource(t_commonFrameSource,true);
            LARGE_INTEGER a,b;QueryPerformanceCounter(&a);const auto first=threadCycles();
            for(unsigned i=0;i<iterations;++i){stage.fn();_ReadWriteBarrier();}
            const auto last=threadCycles();QueryPerformanceCounter(&b);
            cycleSamples.push_back((last-first)/iterations);
            nsSamples.push_back(static_cast<double>(b.QuadPart-a.QuadPart)*1e9/freq.QuadPart/iterations);
        }
        expect(g_inputHostRecoveryHealth.since()==batchSince,"timing batch source remained fresh and context stable");
        rows.push_back({{"route",route},{"stage",stage.name},{"threadCyclesPerCall",cycleSamples},{"wallNsPerCall",nsSamples}});
    }
    return {{"rounds",rounds},{"iterations",iterations},{"rows",rows},{"sink",static_cast<unsigned long long>(sink)}};
}
#endif
int main(int argc,char** argv){
    try{
        configureRuntimeFacts();
        json result=tests();
#if !R119_COUNT
        if(argc==4&&std::string(argv[1])=="--bench")result["benchmark"]=benchmark(std::stoul(argv[2]),std::stoul(argv[3]));
#else
        (void)argc;(void)argv;
#endif
        std::cout<<result.dump(2)<<'\n';return 0;
    }catch(const std::exception& e){measureAllocations=false;std::cout<<json({{"passed",false},{"checks",checks},{"error",e.what()}}).dump(2)<<'\n';return 1;}
}
