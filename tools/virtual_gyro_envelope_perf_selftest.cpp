// R15 isolated production-function fixture. Generated header contains exact
// source bodies; only entrypoint names distinguish baseline and candidate.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <malloc.h>
#include <memory>
#include <numeric>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include "json.hpp"
using json = nlohmann::json;
static_assert(std::is_same_v<json::allocator_type, std::allocator<json>>);
struct Allocations { std::uint64_t requests=0, bytes=0; };
static thread_local bool countAllocations=false;
static thread_local Allocations allocationCount;
// Allocation generations prevent ABA when baseline destroys an initializer
// temporary and malloc reuses a producer node address for a later copy.
struct AllocationIdentity { const void* pointer=nullptr; std::uint64_t generation=0; };
static thread_local bool trackIdentity=false;
static thread_local std::array<AllocationIdentity,32768> identities;
static thread_local std::size_t identityCount=0;
static void noteIdentity(const void* p) {
    if(trackIdentity) {
        if(identityCount>=identities.size())throw std::runtime_error("identity buffer exhausted");
        identities[identityCount]={p,identityCount+1};++identityCount;
    }
}
static std::uint64_t generationOf(const void* p) {
    for(std::size_t i=identityCount;i>0;--i)if(identities[i-1].pointer==p)return identities[i-1].generation;
    throw std::runtime_error("node allocation not bound");
}
static void allocationRequest(std::size_t n) {
    if(countAllocations) { ++allocationCount.requests; allocationCount.bytes+=n; }
}
void* operator new(std::size_t n) {
    allocationRequest(n); void* p=std::malloc(n?n:1); if(!p)throw std::bad_alloc(); noteIdentity(p); return p;
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p,std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p,std::size_t) noexcept { ::operator delete(p); }
void* operator new(std::size_t n,std::align_val_t a) {
    allocationRequest(n); void* p=_aligned_malloc(n?n:1,static_cast<std::size_t>(a));
    if(!p)throw std::bad_alloc(); noteIdentity(p); return p;
}
void* operator new[](std::size_t n,std::align_val_t a) { return ::operator new(n,a); }
void operator delete(void* p,std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p,std::align_val_t a) noexcept { ::operator delete(p,a); }
void operator delete(void* p,std::size_t,std::align_val_t a) noexcept { ::operator delete(p,a); }
void operator delete[](void* p,std::size_t,std::align_val_t a) noexcept { ::operator delete(p,a); }
static std::uint64_t assertionCount=0, correctnessCases=0;
static json correctnessMatrix=json::array(),traceWitnesses=json::array();
static void check(bool b,const char* message) {
    ++assertionCount; if(!b)throw std::runtime_error(message);
}
static bool sameRepresentation(const json& a,const json& b) {
    if(a.type()!=b.type())return false;
    if(a.is_number_float())return std::bit_cast<std::uint64_t>(a.get<double>())==std::bit_cast<std::uint64_t>(b.get<double>());
    if(a.is_structured()) {
        if(a.size()!=b.size())return false;
        auto x=a.begin(),y=b.begin();
        for(;x!=a.end();++x,++y) {
            if(a.is_object()&&x.key()!=y.key())return false;
            if(!sameRepresentation(x.value(),y.value()))return false;
        }
        return true;
    }
    if(a.is_discarded())return b.is_discarded();
    return a==b;
}
static json* activeTrace=nullptr;
static void record(json entry) { if(activeTrace)activeTrace->push_back(std::move(entry)); }
static void simpleRecord(const char* event) { if(activeTrace)record({event}); }
struct GateAtomic {
    std::atomic<bool> value{false}; std::vector<bool> script; std::size_t position=0;
    bool load(std::memory_order order) {
        if(position<script.size())value.store(script[position],std::memory_order_release);
        const bool v=value.load(order);
        if(activeTrace)record({"gate.load",position,v,static_cast<int>(order)});
        ++position; return v;
    }
};
static GateAtomic g_domainLogEnabled[2];
struct CounterAtomic {
    std::atomic<unsigned long long> value{0}; const char* name;
    explicit CounterAtomic(const char* n):name(n){}
    unsigned long long fetch_add(unsigned long long n,std::memory_order order) {
        const auto old=value.fetch_add(n,order);
        if(activeTrace)record({"counter.fetch_add",name,n,old,old+n,static_cast<int>(order)});
        return old;
    }
};
static CounterAtomic g_c17aOwnedEvents{"owned"},g_c17aParityChecks{"parity"},g_c17aParityFailures{"failures"};
static std::atomic<std::uint64_t> wireSink{0};
struct MockView {
    int id=0; bool failResult=false;
    long PostWebMessageAsJson(const wchar_t* message) {
        // Consume the actual production U2W result. No real renderer is invoked.
        if(activeTrace)record({"PostWebMessageAsJson",id,std::string(reinterpret_cast<const char*>(message),std::wcslen(message)*sizeof(wchar_t)),failResult?-1:0});
        wireSink.fetch_xor(static_cast<std::uint64_t>(message[0])+
            static_cast<std::uint64_t>(message[1])+static_cast<std::uint64_t>(id+19),std::memory_order_relaxed);
        return failResult?-1:0;
    }
};
using ICoreWebView2=MockView;
template<class T> struct ComPtr {
    T* p=nullptr;
    ComPtr()=default; ComPtr(std::nullptr_t):p(nullptr){} explicit ComPtr(T* q):p(q){}
    ComPtr(const ComPtr& other):p(other.p) {
        if(activeTrace)record({"target.resolve.copy",p?p->id:-1});
    }
    ComPtr& operator=(const ComPtr& other) { p=other.p; return *this; }
    explicit operator bool() const { return p!=nullptr; }
    T* operator->() const { return p; }
    T* Get() const { return p; }
};
static ComPtr<ICoreWebView2> g_view;
struct ChildWindow { int id=0; ComPtr<ICoreWebView2> view; };
static std::unordered_map<int,ChildWindow*> g_children;
static std::array<MockView,5> views;
static std::array<ChildWindow,4> children;
struct UiCostSlotTable {};
struct UiCostOverflow {};
static UiCostSlotTable g_uiEventTable;
static UiCostOverflow g_uiEventOverflow;
static constexpr int kUiCostIpcEmit=3;
static unsigned long long mockEpoch=0;
static int mockCostSlot=7;
static bool failCostAcquire=false;
static unsigned long long tickCostEpochUs() {
    mockEpoch+=100;
    if(activeTrace)record({"cost.epoch",mockEpoch});
    return mockEpoch;
}
static int uiCostSlotAcquire(UiCostSlotTable&,unsigned long long key,const char* name) {
    if(activeTrace)record({"cost.acquire",key,std::string(name),mockCostSlot});
    if(failCostAcquire)throw std::runtime_error("mock-cost-acquire-failure");
    return mockCostSlot;
}
static void uiCostStageAdd(int stage,unsigned long long us) {
    if(activeTrace)record({"cost.stage",stage,us});
}
static void uiCostSlotAdd(UiCostSlotTable&,int slot,unsigned long long us,UiCostOverflow&) {
    if(activeTrace)record({"cost.slot",slot,us});
}
static void appendNativeLifecycleLog(const char* event,json detail) {
    if(activeTrace)record({"log",std::string(event),std::move(detail)});
}
struct LocalObservation {
    bool called=false; std::string event; json parameter,envelope;
    std::vector<const void*> envelopeNodes;
    std::vector<std::uint64_t> envelopeGenerations;
};
static LocalObservation* localObserver=nullptr;
static void collectNodes(const json& j,std::vector<const void*>& out) {
    if(j.is_object())out.push_back(j.get_ptr<const json::object_t*>());
    if(j.is_array())out.push_back(j.get_ptr<const json::array_t*>());
    if(j.is_string())out.push_back(j.get_ptr<const json::string_t*>());
    if(j.is_structured())for(const auto& x:j)collectNodes(x,out);
}
static void observeLocals(const std::string& ev,const json& data,const json& envelope) {
    if(!localObserver)return;
    localObserver->called=true; localObserver->event=ev;
    localObserver->parameter=data; localObserver->envelope=envelope;
    collectNodes(envelope.at("data"),localObserver->envelopeNodes);
    if(trackIdentity)for(auto* p:localObserver->envelopeNodes)localObserver->envelopeGenerations.push_back(generationOf(p));
}
#include "production-extracted.h"
using Emitter=void(*)(std::string,json);
struct Outcome {
    json trace=json::array(); std::string exception,message,callerEvent;
    int exceptionId=0; json callerData; std::array<unsigned long long,3> counters{};
    std::size_t gateReads=0; LocalObservation local;
};
static std::vector<int> deliveryOrder() {
    std::vector<int> out;
    if(g_view)out.push_back(0);
    for(const auto& [id,cw]:g_children)if(cw&&cw->view)out.push_back(id);
    return out;
}
static void configureTopology(int topology,bool failResult=false) {
    g_children.clear(); g_view=ComPtr<ICoreWebView2>();
    for(int i=0;i<5;++i)views[i]={i,failResult};
    for(int i=0;i<4;++i)children[i]={i+1,ComPtr<ICoreWebView2>(&views[i+1])};
    // Preserve actual unordered_map iteration. Expected deliveries use this
    // very map, including null pointers and present children with null views.
    if(topology==1||topology==5||topology==7)g_view=ComPtr<ICoreWebView2>(&views[0]);
    if(topology==2||topology==5||topology==6||topology==7) {
        g_children.emplace(3,&children[2]); g_children.emplace(1,&children[0]);
    }
    if(topology==3||topology==5||topology==6)g_children.emplace(8,nullptr);
    if(topology==4||topology==5||topology==6) {
        children[3].view=ComPtr<ICoreWebView2>(); g_children.emplace(4,&children[3]);
    }
    if(topology==7)g_children.emplace(2,&children[1]);
}
static void resetObservations(const std::vector<bool>& script,int slot,bool failAcquire) {
    auto& gate=g_domainLogEnabled[1]; gate.script=script; gate.position=0;
    gate.value.store(script.empty()?false:script[0],std::memory_order_release);
    for(auto* counter:{&g_c17aOwnedEvents,&g_c17aParityChecks,&g_c17aParityFailures})counter->value.store(0,std::memory_order_relaxed);
    mockEpoch=0; mockCostSlot=slot; failCostAcquire=failAcquire;
}
static Outcome invoke(Emitter emit,const std::string& ev,const json& payload,
                      const std::vector<bool>& script,int slot=7,bool failAcquire=false,bool observe=false) {
    Outcome o; std::string caller=ev; json argument=payload;
    resetObservations(script,slot,failAcquire);
    activeTrace=&o.trace; localObserver=observe?&o.local:nullptr;
    try { emit(std::move(caller),std::move(argument)); }
    catch(const json::exception& e) { o.exception="json::exception";o.exceptionId=e.id;o.message=e.what(); }
    catch(const std::exception& e) { o.exception=typeid(e).name();o.message=e.what(); }
    activeTrace=nullptr; localObserver=nullptr;
    o.callerEvent=caller;o.callerData=std::move(argument);
    o.counters={g_c17aOwnedEvents.value.load(std::memory_order_relaxed),
        g_c17aParityChecks.value.load(std::memory_order_relaxed),g_c17aParityFailures.value.load(std::memory_order_relaxed)};
    o.gateReads=g_domainLogEnabled[1].position;
    return o;
}
static json dumpOutcome(const json& value,json::error_handler_t handler);
static void compareOutcomes(const Outcome& a,const Outcome& b) {
    check(sameRepresentation(a.trace,b.trace),"gate/counter/cost/log/target trace changed");
    check(a.exception==b.exception&&a.exceptionId==b.exceptionId&&a.message==b.message,"exception identity or diagnostic changed");
    check(a.callerEvent==b.callerEvent&&sameRepresentation(a.callerData,b.callerData),"caller moved-from arguments changed");
    check(a.counters==b.counters&&a.gateReads==b.gateReads,"counter totals/gate count changed");
    check(a.local.called==b.local.called,"local observation availability changed");
    if(a.local.called) {
        check(a.local.event==b.local.event,"local moved-from event changed");
        check(sameRepresentation(a.local.parameter,b.local.parameter),"local moved-from data changed");
        check(sameRepresentation(a.local.envelope,b.local.envelope),"JSON types/float bits/envelope content changed");
        for(auto handler:{json::error_handler_t::strict,json::error_handler_t::replace,json::error_handler_t::ignore})
            check(dumpOutcome(a.local.envelope,handler)==dumpOutcome(b.local.envelope,handler),"exact dump bytes or exception changed");
    }
}
static json dumpOutcome(const json& value,json::error_handler_t handler=json::error_handler_t::strict) {
    try { return {{"bytes",value.dump(-1,' ',false,handler)}}; }
    catch(const json::exception& e) { return {{"id",e.id},{"message",e.what()}}; }
}
static std::size_t countTrace(const Outcome& o,const char* name) {
    return std::count_if(o.trace.begin(),o.trace.end(),[&](const json& v){return v[0]==name;});
}
static void assertTraceOracle(const Outcome& o,const std::vector<bool>& script,int topology,
                             const std::vector<int>& expected,const std::string& ev,const json& payload,int slot,bool failAcquire) {
    std::size_t read=0;
    for(const auto& row:o.trace)if(row[0]=="gate.load") {
        check(row[1]==read&&row[2]==script[read]&&row[3]==static_cast<int>(std::memory_order_acquire),"gate sequence/memory ordering oracle");++read;
    }
    check(read==o.gateReads,"all gate loads traced");
    check(countTrace(o,"cost.epoch")== (script[0]?(failAcquire?1:2):0),"cost timebase/RAII lifetime oracle");
    check(countTrace(o,"cost.acquire")== (script[0]?1:0),"cost acquisition oracle");
    check(countTrace(o,"cost.stage")== (script[0]&&!failAcquire?1:0),"cost destructor stage oracle");
    check(countTrace(o,"cost.slot")== (script[0]&&!failAcquire?1:0),"cost destructor slot oracle");
    if(script[0]&&!failAcquire) {
        const auto n=o.trace.size(); check(o.trace[n-2]==json({"cost.stage",kUiCostIpcEmit,100}),"scope lifetime must cover delivery/exception/return");
        check(o.trace[n-1]==json({"cost.slot",slot,100}),"scope final slot/overflow accounting");
    }
    if(failAcquire&&script[0]) { check(o.gateReads==1&&o.counters==std::array<unsigned long long,3>{0,0,0},"scope constructor exception continued function");return; }
    if(topology==0) { check(o.gateReads==1&&o.counters==std::array<unsigned long long,3>{0,0,0},"no-view early return changed");return; }
    check(o.counters[0]==static_cast<unsigned long long>(script[1]),"owned counter oracle");
    if(o.exception.empty()) {
        check(o.gateReads==4,"success must perform four distinct gate reads");
        check(o.counters[1]==static_cast<unsigned long long>(script[3]),"parity counter oracle");
        const auto mismatches=static_cast<unsigned long long>(!script[2]&&script[3]);
        check(o.counters[2]==mismatches&&countTrace(o,"log")==mismatches,"successive snapshot mismatch/log oracle");
        std::vector<int> got;
        json normalized=payload.is_object()?payload:json::object();
        const json envelope={{"event",ev},{"data",normalized}};
        // No active trace: independent old envelope and actual production U2W.
        std::wstring wire;
        if(!expected.empty())wire=U2W(envelope.dump());
        for(const auto& row:o.trace)if(row[0]=="PostWebMessageAsJson") {
            got.push_back(row[1].get<int>());
            check(row[2].get<std::string>()==std::string(reinterpret_cast<const char*>(wire.data()),wire.size()*sizeof(wchar_t)),"exact UTF16 observed wire differs from old envelope dump");
        }
        check(got==expected,"renderer broadcast order/count oracle");
        check(countTrace(o,"target.resolve.copy")==1+std::count_if(g_children.begin(),g_children.end(),[](const auto& p){return p.second&&p.second->view;}),"main resolution plus filtered child calls changed");
    } else if(o.exceptionId==316) {
        check(countTrace(o,"PostWebMessageAsJson")==0,"strict UTF8 exception must precede first write");
        check(countTrace(o,"log")==0,"exception emitted mismatch log");
    }
}
static json hostFrame() {
    return {{"hostSubmission","frame-accepted"},{"hostActive",true},{"firstFrame",false},{"lifecycle","active"},
        {"frameProof",{{"sampleSequence",30556ULL},{"motionPairProofId","hc-same-provider-selector-v2"},
        {"hostReceipt",{{"runId",std::string(64,'r')},{"epoch",7ULL},{"powerGeneration",5ULL},{"hostInstanceId",std::string(32,'a')},{"hostAckAccepted",true}}}}},
        {"persona","steamdeck"},{"targetStick","right"},{"focusIsolationSemanticNeutral",false},
        {"motionAdmission","hc-motion-admitted"},{"analogAdmission","stable"},
        {"leftStick",{{"x",0.0},{"y",0.0}}},{"rightStick",{{"x",0.0},{"y",0.0}}},
        {"gyroDps",{{"x",1.25},{"y",-2.5},{"z",3.75}}},
        {"motionContribution",{{"x",0.0},{"y",0.0}}},{"layoutContribution",{{"x",0.0},{"y",0.0}}},
        {"motionManager",{{"mapped",true},{"triggered",false},{"velocityMode",false},{"gyroShort",{{"x",0.0},{"y",0.0}}},{"modifiedShort",{{"x",0.0},{"y",0.0}}}}},
        {"layoutManager",{{"physicalStick",{{"x",0.0},{"y",0.0}}},{"stickNorm",0.0},{"gyroWeight",1.0},{"weightFactor",1.0},{"mergeOrder","physical-stick-plus-motion-contribution"}}}};
}
static json fullPayload() {
    return {{"active",true},{"supported",true},{"sampleAgeMs",0ULL},{"sampleSequence",30556ULL},
        {"provider","hc-msi-motion"},{"sourceId","synthetic-g3e-shape"},{"targetStick","right"},
        {"persona","steamdeck"},{"motionPairProofId","hc-same-provider-selector-v2"},{"leaseLive",true},
        {"orientation",{{"x",0.0},{"y",0.0},{"z",0.0},{"w",1.0}}},
        {"gyroDps",{{"x",1.25},{"y",-2.5},{"z",3.75}}},{"accel",{{"x",0.0},{"y",0.0},{"z",9.8}}},
        {"gravity",{{"x",0.0},{"y",0.0},{"z",1.0}}},{"calibrating",false},{"calibrationProgress",0.0},
        {"mapped",true},{"triggered",false},{"velocityMode",false},{"sensitivity",1.0},{"deadzone",0.0},
        {"output",{{"x",0.0},{"y",0.0}}},{"leftStick",{{"x",0.0},{"y",0.0}}},
        {"rightStick",{{"x",0.0},{"y",0.0}}},{"motionContribution",{{"x",0.0},{"y",0.0}}},
        {"sensorSnapshots",{{"source","synthetic-fixture"},{"connected",true},{"sequence",30556ULL}}},{"hostFrame",hostFrame()}};
}
static json deepPayload() {
    json node={{"leaf",std::string(96,'x')},{"values",json::array({0ULL,-1LL,-0.0,true,nullptr})}};
    for(int i=0;i<14;++i)node={{"level",i},{"next",std::move(node)}};
    return {{"nested",std::move(node)},{"hostFrame",hostFrame()}};
}
static json unicodePayload() {
    json p=fullPayload();p["labels"]={"陀螺仪", "café", "🕹️🎮", "مرحبا", "e\u0301"};
    p["escapes"]="quote\" slash\\ backspace\b tab\t newline\n carriage\r formfeed\f";
    p["embeddedNul"]=std::string("before\0after",12);return p;
}
struct TestInput { std::string name,event="gyro.telemetry"; json payload; };
static std::vector<TestInput> correctnessInputs() {
    std::vector<TestInput> out;
    auto add=[&](const char* name,json value,std::string event="gyro.telemetry"){out.push_back({name,std::move(event),std::move(value)});};
    add("absent",nullptr); add("nonobject-array",json::array({1,2,{"x",4}}));
    add("nonobject-string","unused payload");add("nonobject-bool",false);add("nonobject-zero",0);
    add("nonobject-bad-utf8",std::string("\xc3\x28",2));add("empty-object",json::object());
    add("zero",{{"n",0},{"u",0ULL},{"f",0.0},{"negative-zero",-0.0},{"array",json::array()}});
    add("full",fullPayload());add("deep",deepPayload());add("unicode-escapes",unicodePayload());
    add("limits",{{"u",std::numeric_limits<std::uint64_t>::max()},{"imin",std::numeric_limits<std::int64_t>::min()},
        {"imax",std::numeric_limits<std::int64_t>::max()},{"denorm",std::numeric_limits<double>::denorm_min()},
        {"fmin",std::numeric_limits<double>::lowest()},{"fmax",std::numeric_limits<double>::max()}});
    add("nonfinite",{{"nan",std::numeric_limits<double>::quiet_NaN()},{"inf",std::numeric_limits<double>::infinity()},
        {"negativeInf",-std::numeric_limits<double>::infinity()},{"negzero",-0.0}});
    add("binary",{{"binary",json::binary({0,255,17})}});
    add("bad-utf8-value",{{"bad",std::string("\xc3\x28",2)}});
    add("bad-utf8-key",{{std::string("\xed\xa0\x80",3),1}});
    add("bad-utf8-event",fullPayload(),std::string("gyro.\xf0\x28\x8c\xbc",9));
    add("empty-event",fullPayload(),"");add("nul-event",unicodePayload(),std::string("gyro\0telemetry",14));
    add("long-event",fullPayload(),std::string(128,'g'));add("unicode-event",fullPayload(),"陀螺仪.telemetry");
    add("discarded-nested",{{"discarded",json(json::value_t::discarded)}});
    return out;
}
static std::string hexBytes(const std::string& value) {
    constexpr char hex[]="0123456789abcdef";std::string result;result.reserve(value.size()*2);
    for(unsigned char c:value) { result.push_back(hex[c>>4]);result.push_back(hex[c&15]); }return result;
}
static json describeValue(const json& value) {
    json out={{"type",value.type_name()}};
    if(value.is_string())out["utf8Hex"]=hexBytes(value.get<std::string>());
    else if(value.is_number_float())out["float64Bits"]=std::bit_cast<std::uint64_t>(value.get<double>());
    else if(value.is_number_unsigned())out["unsigned64"]=value.get<std::uint64_t>();
    else if(value.is_number_integer())out["signed64"]=value.get<std::int64_t>();
    else if(value.is_boolean())out["value"]=value.get<bool>();
    else if(value.is_binary())out["bytes"]=value.get_binary();
    else if(value.is_structured()) {
        out["items"]=json::array();
        for(auto it=value.begin();it!=value.end();++it) {
            json item=describeValue(it.value());if(value.is_object())item["keyUtf8Hex"]=hexBytes(it.key());
            out["items"].push_back(std::move(item));
        }
    }
    return out;
}
static json inputCorpus() {
    json inputs=json::array();
    for(const auto& input:correctnessInputs())inputs.push_back({{"name",input.name},{"eventUtf8Hex",hexBytes(input.event)},
        {"payload",describeValue(input.payload)}});
    return {{"synthetic",true},{"archiveReopened",false},{"inputs",inputs},
        {"perfShapes",json::array({"empty-object","full","deep","unicode-escapes"})},
        {"encoding","JSON types with hex UTF8 strings, signed/unsigned 64bit and exact float64 bit patterns"}};
}
static json proveCopyAndMove() {
    trackIdentity=true;identityCount=0;
    json source=unicodePayload();std::vector<const void*> addresses;collectNodes(source,addresses);
    std::vector<std::uint64_t> generations;for(auto* p:addresses)generations.push_back(generationOf(p));
    configureTopology(1);
    resetObservations({false,false,false,false},7,false);
    LocalObservation before,candidate;
    localObserver=&before; baseline_observed("gyro.telemetry",std::move(source)); localObserver=nullptr;
    std::size_t originalShared=0,addressReuses=0;
    for(std::size_t i=0;i<before.envelopeNodes.size();++i) {
        for(std::size_t j=0;j<addresses.size();++j)if(before.envelopeNodes[i]==addresses[j]) {
            ++addressReuses;
            if(before.envelopeGenerations[i]==generations[j])++originalShared;
        }
    }
    check(originalShared==addresses.size(),"current vendored initializer failed to preserve producer allocation identities");
    source=unicodePayload();addresses.clear();collectNodes(source,addresses);
    generations.clear();for(auto* p:addresses)generations.push_back(generationOf(p));
    resetObservations({false,false,false,false},7,false);
    localObserver=&candidate;candidate_observed("gyro.telemetry",std::move(source));localObserver=nullptr;
    check(candidate.envelopeNodes==addresses&&candidate.envelopeGenerations==generations,
        "candidate must preserve every original object/array/string allocation identity");
    check(before.parameter.is_null()&&candidate.parameter.is_null(),"local JSON parameter not moved-from null");
    check(before.event==candidate.event,"local string moved-from state differs");
    check(sameRepresentation(before.envelope,candidate.envelope),"ownership proof exact representation mismatch");
    trackIdentity=false;
    return {{"nodesCompared",addresses.size()},{"baselineNodesSharingProducerStorage",originalShared},
        {"baselineProducerAddressMatches",addressReuses},{"baselineAddressReusesWithDifferentGeneration",addressReuses-originalShared},
        {"candidateNodesPreservingProducerStorage",candidate.envelopeNodes.size()},
        {"baselineInitializerDeepCopyProven",originalShared==0},{"baselineWholePayloadMoveProven",originalShared==addresses.size()},{"candidateWholePayloadMoveProven",true},
        {"localDataMovedFromType","null"},{"localEventMovedFromBytes",candidate.event}};
}
static json runCorrectness() {
    auto inputs=correctnessInputs();std::uint64_t strictExceptions=0,mismatchLogs=0;
    for(const auto& input:inputs)for(int topology=0;topology<8;++topology)for(int mask=0;mask<16;++mask) {
        configureTopology(topology);const auto expected=deliveryOrder();
        std::vector<bool> script;for(int i=0;i<4;++i)script.push_back((mask&(1<<i))!=0);
        auto a=invoke(baseline_emit,input.event,input.payload,script);
        auto b=invoke(candidate_emit,input.event,input.payload,script);
        compareOutcomes(a,b);assertTraceOracle(a,script,topology,expected,input.event,input.payload,7,false);
        auto observedA=invoke(baseline_observed,input.event,input.payload,script,7,false,true);
        auto observedB=invoke(candidate_observed,input.event,input.payload,script,7,false,true);
        compareOutcomes(observedA,observedB);
        check(sameRepresentation(a.trace,observedA.trace)&&sameRepresentation(b.trace,observedB.trace),"observation hook altered external trace");
        ++correctnessCases;strictExceptions+=(a.exceptionId==316);mismatchLogs+=countTrace(a,"log");
        json delivered=json::array();for(const auto& row:a.trace)if(row[0]=="PostWebMessageAsJson")delivered.push_back(row[1]);
        correctnessMatrix.push_back({{"input",input.name},{"topology",topology},{"scriptMask",mask},
            {"gateReads",a.gateReads},{"counters",a.counters},{"deliveries",delivered},
            {"exception",a.exception},{"exceptionId",a.exceptionId},{"message",a.message},
            {"callerDataMovedFromType",a.callerData.type_name()},{"callerEventMovedFromUtf8Hex",hexBytes(a.callerEvent)},
            {"localObservationAvailable",observedA.local.called},{"baselineCandidateEquivalent",true}});
        if((input.name=="unicode-escapes"||input.name=="bad-utf8-value")&&topology==5)
            traceWitnesses.push_back({{"input",input.name},{"scriptMask",mask},{"trace",describeValue(a.trace)}});
    }
    for(int topology:{0,1,5})for(bool failAcquire:{false,true})for(int slot:{-1,7}) {
        configureTopology(topology,true);auto order=deliveryOrder();const std::vector<bool> script={true,false,true,true};
        auto a=invoke(baseline_emit,"gyro.telemetry",unicodePayload(),script,slot,failAcquire);
        auto b=invoke(candidate_emit,"gyro.telemetry",unicodePayload(),script,slot,failAcquire);
        compareOutcomes(a,b);assertTraceOracle(a,script,topology,order,"gyro.telemetry",unicodePayload(),slot,failAcquire);++correctnessCases;
    }
    return {{"pass",true},{"cases",correctnessCases},{"inputShapes",inputs.size()},{"topologies",8},
        {"successiveGateScripts",16},{"strictUtf8ExceptionsCompared",strictExceptions},
        {"existingMismatchLogCallsCompared",mismatchLogs},{"callerAndLocalMovedFromCompared",true},
        {"exactJsonTypesAndFloatingBitsCompared",true},{"costScopeConstructorAndDestructorCompared",true},
        {"costOverflowAndConstructorExceptionCompared",true},{"inertPostFailureReturnIgnoredAsOriginal",true},
        {"productionExceptionHandlersAdded",false}};
}
struct Prepared { std::string ev; json data; };
struct Measurement { double ns=0; Allocations allocations; };
static Measurement measure(Emitter emitter,const json& payload,int batches,bool count) {
    constexpr int items=64;double elapsed=0;Allocations total;
    for(int batch=0;batch<batches;++batch) {
        std::vector<Prepared> prepared;prepared.reserve(items);
        for(int i=0;i<items;++i)prepared.push_back({"gyro.telemetry",payload});
        allocationCount={};countAllocations=count;
        const auto started=std::chrono::steady_clock::now();
        for(auto& p:prepared)emitter(std::move(p.ev),std::move(p.data));
        const auto ended=std::chrono::steady_clock::now();
        countAllocations=false;
        elapsed+=std::chrono::duration<double,std::nano>(ended-started).count();
        total.requests+=allocationCount.requests;total.bytes+=allocationCount.bytes;
        // Caller setup and moved-from containers are outside the timed region.
        // Payload/envelope/reference/dump/U2W destruction occurs inside emitter.
    }
    const auto events=static_cast<double>(batches*items);
    return {elapsed/events,{total.requests/static_cast<std::uint64_t>(events),total.bytes/static_cast<std::uint64_t>(events)}};
}
struct Scenario {
    std::string shape;json payload;bool gate;int topology;
    std::vector<double> baseline,candidate;
    Measurement baselineAlloc,candidateAlloc;
};
static double median(std::vector<double> v) { std::sort(v.begin(),v.end());return v[v.size()/2]; }
static void perfSetup(const Scenario& s) {
    configureTopology(s.topology);resetObservations({},7,false);
    g_domainLogEnabled[1].value.store(s.gate,std::memory_order_release);
}
static json runBenchmarks(int batches) {
    std::vector<Scenario> scenarios;
    for(const auto& shape:std::vector<std::pair<std::string,json>>{{"empty",json::object()},{"full",fullPayload()},
            {"deep",deepPayload()},{"unicode",unicodePayload()}})
        for(bool gate:{false,true})for(int topology:{1,7})scenarios.push_back({shape.first,shape.second,gate,topology});
    // Thresholds and scenario list are saved by runner before executable starts.
    // Allocation runs are independent of wall-clock rounds; RSS is not used.
    for(auto& s:scenarios) {
        perfSetup(s);measure(baseline_emit,s.payload,2,false);measure(candidate_emit,s.payload,2,false);
        s.baselineAlloc=measure(baseline_emit,s.payload,2,true);
        s.candidateAlloc=measure(candidate_emit,s.payload,2,true);
    }
    json rounds=json::array();
    for(int round=0;round<11;++round) {
        for(std::size_t step=0;step<scenarios.size();++step) {
            const auto index=(step+round*5)%scenarios.size();auto& s=scenarios[index];perfSetup(s);
            // Rewarm both variants before each pair; each is timed separately.
            measure(baseline_emit,s.payload,1,false);measure(candidate_emit,s.payload,1,false);
            Measurement a,b;const bool baselineFirst=((round+index)%2==0);
            if(baselineFirst) { a=measure(baseline_emit,s.payload,batches,false);b=measure(candidate_emit,s.payload,batches,false); }
            else { b=measure(candidate_emit,s.payload,batches,false);a=measure(baseline_emit,s.payload,batches,false); }
            s.baseline.push_back(a.ns);s.candidate.push_back(b.ns);
            rounds.push_back({{"round",round+1},{"scenario",index},{"baselineFirst",baselineFirst},{"baselineNs",a.ns},{"candidateNs",b.ns}});
        }
    }
    json rows=json::array();bool allAccepted=true;
    for(const auto& s:scenarios) {
        int wins=0;for(int i=0;i<11;++i)wins+=(s.candidate[i]<s.baseline[i]);
        const auto bm=median(s.baseline),cm=median(s.candidate);const auto reduction=100*(bm-cm)/bm;
        const bool accepted=wins>=10&&reduction>=5&&s.candidateAlloc.allocations.requests<s.baselineAlloc.allocations.requests&&
            s.candidateAlloc.allocations.bytes<s.baselineAlloc.allocations.bytes;allAccepted&=accepted;
        rows.push_back({{"shape",s.shape},{"gate",s.gate},{"targets",s.topology==1?1:4},
            {"baselineMedianNs",bm},{"candidateMedianNs",cm},{"medianReductionPercent",reduction},{"fasterRounds",wins},
            {"baselineRequests",s.baselineAlloc.allocations.requests},{"candidateRequests",s.candidateAlloc.allocations.requests},
            {"baselineRequestedBytes",s.baselineAlloc.allocations.bytes},{"candidateRequestedBytes",s.candidateAlloc.allocations.bytes},
            {"baselineRoundsNs",s.baseline},{"candidateRoundsNs",s.candidate},{"accepted",accepted}});
    }
    return {{"decision",allAccepted?"accept-local-candidate":"reject"},{"allScenariosAccepted",allAccepted},
        {"rounds",11},{"eventsPerTimedVariantPerRound",batches*64},{"scenarios",rows},{"schedule",rounds},
        {"allocationsCountedInSeparateRuns",true},{"rssUsedAsAllocationMetric",false},
        {"realAtomicCounters",true},{"originalDumpAndUtf16ConversionPerTarget",true},
        {"includesPayloadAndEnvelopeDestruction",true},{"setupIncluded",false}};
}
int main(int argc,char** argv) {
    try {
        if(argc>1&&std::string(argv[1])=="--inputs") { std::cout<<inputCorpus().dump(2)<<'\n';return 0; }
        const int batches=argc>1?std::stoi(argv[1]):12;
        json result={{"correctness",runCorrectness()},{"ownership",proveCopyAndMove()}};
        result["performance"]=batches?runBenchmarks(batches):json(nullptr);
        result["caseMatrix"]=std::move(correctnessMatrix);result["traceWitnesses"]=std::move(traceWitnesses);
        result["assertions"]=assertionCount;result["sink"]=wireSink.load(std::memory_order_relaxed);
        std::cout<<result.dump(2)<<'\n';return 0;
    } catch(const std::exception& e) { activeTrace=nullptr;countAllocations=false;std::cerr<<e.what()<<'\n';return 2; }
}
