// R12 standalone, portable JSON fixture. No Windows/application/device headers.
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include "json.hpp"
#include "gamepad_backend_cost_json.h"
#include "log_frequency_policy.h"
#include "input_callback_cost.h"
namespace callbackCost = ymcc::input::callbackCost;
using json = nlohmann::json;
using ULONGLONG = unsigned long long;
static bool allocationCounting = false;
static std::uint64_t allocationCalls = 0, allocationBytes = 0;
static void allocation(std::size_t bytes) { if (allocationCounting) { ++allocationCalls; allocationBytes += bytes; } }
void* operator new(std::size_t bytes) { allocation(bytes); if (auto p=std::malloc(bytes?bytes:1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }
#ifdef _MSC_VER
#include <malloc.h>
void* operator new(std::size_t bytes,std::align_val_t alignment) { allocation(bytes); if(auto p=_aligned_malloc(bytes?bytes:1,std::size_t(alignment))) return p; throw std::bad_alloc(); }
void operator delete(void* p,std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void* p,std::size_t,std::align_val_t) noexcept { _aligned_free(p); }
void* operator new[](std::size_t bytes,std::align_val_t alignment) { return ::operator new(bytes,alignment); }
void operator delete[](void* p,std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept { _aligned_free(p); }
#endif
static std::uint64_t assertions=0, sink=0;
static void check(bool ok,const char* message) { ++assertions; if(!ok) throw std::runtime_error(message); }
static std::uint64_t next(std::uint64_t& seed) { seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed; }
struct Event { unsigned id{},operation{},order{};std::uint64_t input{},output{}; bool operator==(const Event&) const=default; };
static std::array<Event,4096> trace;
static std::size_t traceSize=0;
static bool tracing=false;
static void traceEvent(unsigned id,unsigned operation,std::memory_order order,std::uint64_t input,std::uint64_t output) {
    if(tracing) { if(traceSize==trace.size()) throw std::runtime_error("trace capacity exceeded");trace[traceSize++]={id,operation,unsigned(order),input,output}; }
}
template<class T> struct Tracked {
    std::atomic<T> value{0};unsigned id=0;
    T exchange(T v,std::memory_order order) { auto result=value.exchange(v,order);traceEvent(id,1,order,std::uint64_t(v),std::uint64_t(result));return result; }
    T load(std::memory_order order) const { auto result=value.load(order);traceEvent(id,2,order,0,std::uint64_t(result));return result; }
};
struct CounterLink { void* pointer;void(*set)(void*,std::uint64_t);std::uint64_t(*get)(void*); };
static std::vector<CounterLink> counters;
template<class T> void registerCounter(Tracked<T>& value) {
    value.id=unsigned(counters.size());
    counters.push_back({&value,[](void* p,std::uint64_t v){static_cast<Tracked<T>*>(p)->value.store(T(v),std::memory_order_relaxed);},[](void* p){return std::uint64_t(static_cast<Tracked<T>*>(p)->value.load(std::memory_order_relaxed));}});
}
#include "context.hpp"
static ymcc::input::backendCost::Counters g_gamepadBackendCost;
static json boundBackendWindowJson() {traceEvent(4000,3,std::memory_order_relaxed,0,0);return ymcc::input::backendCost::windowJson<json>(g_gamepadBackendCost);}
// Parent owns scope/start/end logic. Bind the actual portable window drain here.
#include "callback.hpp"
static json tickCostCallbackWindowJson() {traceEvent(4001,3,std::memory_order_relaxed,0,0);return boundCurrentCallbackWindowJson();}
static bool logEnabled=true, gateClosesOnRecheck=false, loggerMode=false, captureRoot=false, captureWire=true;
static unsigned gateChecks=0, appendCalls=0, mirrorCalls=0, rotations=0, opens=0;
static json rootCapture;
static std::string wireCapture;
static std::mutex g_domainLogMx, g_nativeLifecycleLogMx;
static std::atomic<bool> g_domainLogEnabled[2]{{false},{true}};
static std::atomic<int> g_powerLifecycle{2};
static std::atomic<unsigned long long> g_powerGeneration{7};
static void* g_hwnd=reinterpret_cast<void*>(std::uintptr_t(0xabcdef));
static unsigned long long g_nativeLifecycleFrameWrites=0,g_nativeDetailWrites=0,g_nativeDetailLastFlushTick=0;
static constexpr unsigned long long kLogMaxBytes=1024*1024;
static bool domainLogEnabled(const char*) {++gateChecks;return logEnabled&&!(gateClosesOnRecheck&&gateChecks>1);}
static int domainLogStreamIndex(const char* domain) {return domain&&std::string_view(domain)=="virtual"?0:-1;}
static std::wstring domainLogPath(const char*) {return L"fixture-memory-domain";}
static std::string nativeLifecycleTimestamp() {return "2026-10-15T12:34:56.789";}
static unsigned long GetCurrentProcessId() {return 12345;}
static ULONGLONG GetTickCount64() {return 8000;}
static const char* powerLifecycleName(int) {return "active";}
static void rotateOneLogFile(const std::wstring&,unsigned long long=0) {++rotations;}
struct DomainSink {
    template<class Rotate> void append(const std::wstring&,const std::string& text,ULONGLONG,bool,unsigned long long,Rotate) {
        ++appendCalls;sink+=text.size();if(captureWire)wireCapture=text;
    }
};
static DomainSink g_domainLogStreams[1];
#include "logger.hpp"
static void appendDomainLog(const char* domain,const char* event,const json& detail) {
    if(loggerMode) {if(captureRoot)rootCapture=detail;fixedAppendDomainLog(domain,event,detail);}
    else rootCapture=std::move(const_cast<json&>(detail)); // fixture consumes expiring root only
}
#include "before.hpp"
#include "candidate.hpp"

static bool nativeLifecycleRecordIsStreaming(const char*,const json& detail) {return detail.value("streaming",false);}
static bool nativeLifecycleEventIsBase(const char* event) {return event&&std::string_view(event)=="base";}
static void mirrorLifecycleToDomains(const char*,const json&) {++mirrorCalls;}
static const wchar_t* app_data_dir() {return L"fixture-memory-native";}
namespace fspath {
struct path {
    std::wstring value;
    path(const wchar_t* v):value(v){}
    path operator/(const wchar_t* suffix) const {path p(*this);p.value+=suffix;return p;}
    std::wstring wstring() const {return value;}
};
static unsigned long long file_size(const path&,std::error_code&) {return 0;}
}
struct NativeSink {
    bool opened=true;unsigned flushes=0;
    bool is_open() const {return opened;}
    explicit operator bool() const {return opened;}
    void close() {opened=false;}
    void clear() {}
    void flush() {++flushes;}
    NativeSink& operator<<(const std::string& text) {sink+=text.size();if(captureWire)wireCapture=text;return *this;}
    NativeSink& operator<<(char value) {++sink;if(captureWire)wireCapture+=value;return *this;}
};
static NativeSink g_nativeLifecycleLog,g_nativeDetailLog;
namespace ymcc::logging {static bool openJsonLineAppend(NativeSink& stream,const fspath::path&) {++opens;stream.opened=true;return true;}}
#include "native-logger.hpp"

static void exact(const json& a,const json& b) {
    check(a.type()==b.type(),"JSON type mismatch (including signed vs unsigned zero)");
    switch(a.type()) {
        case json::value_t::object:
            check(a.size()==b.size(),"object key count mismatch");
            for(auto i=a.begin(),j=b.begin();i!=a.end();++i,++j) {check(i.key()==j.key(),"object key order mismatch");exact(i.value(),j.value());}break;
        case json::value_t::array:
            check(a.size()==b.size(),"array length mismatch");for(std::size_t i=0;i<a.size();++i)exact(a[i],b[i]);break;
        case json::value_t::number_float:
            check(std::bit_cast<std::uint64_t>(a.get<json::number_float_t>())==std::bit_cast<std::uint64_t>(b.get<json::number_float_t>()),"double bits mismatch");break;
        case json::value_t::number_integer:check(a.get<json::number_integer_t>()==b.get<json::number_integer_t>(),"signed integer mismatch");break;
        case json::value_t::number_unsigned:check(a.get<json::number_unsigned_t>()==b.get<json::number_unsigned_t>(),"unsigned integer mismatch");break;
        case json::value_t::binary:
            check(a.get_binary()==b.get_binary(),"binary byte mismatch");check(a.get_binary().has_subtype()==b.get_binary().has_subtype(),"binary subtype presence mismatch");check(a.get_binary().subtype()==b.get_binary().subtype(),"binary subtype mismatch");break;
        default:check(a==b,"scalar/nested value mismatch");break;
    }
}
static json specialValues() {
    json value=json::object();
    value["signedZero"]=std::int64_t(0);value["unsignedZero"]=std::uint64_t(0);
    value["signedMin"]=std::numeric_limits<std::int64_t>::min();value["signedMax"]=std::numeric_limits<std::int64_t>::max();value["unsignedMax"]=std::numeric_limits<std::uint64_t>::max();
    value["floats"]=json::array();
    for(std::uint64_t bits:{0ULL,0x8000000000000000ULL,1ULL,0x8000000000000001ULL,0x3ff0000000000000ULL,0x3fb999999999999aULL,0x7fefffffffffffffULL,0xffefffffffffffffULL,0x7ff0000000000000ULL,0xfff0000000000000ULL,0x7ff8000000000012ULL})value["floats"].push_back(std::bit_cast<double>(bits));
    value["null"]=nullptr;value["bool"]=false;value["escaped"]=std::string("UTF8 \xe5\xad\x97 quote \" \n\t");value["escaped"].get_ref<std::string&>().push_back('\0');value["escaped"].get_ref<std::string&>()+="tail";
    value["array"]=json::array({json::object(),json::array(),true,std::int64_t(-1)});
    value["binary"]=json::binary({0,1,255},7);
    return value;
}
static void seedFixture(unsigned scenario,std::uint64_t seed) {
    for(auto& counter:counters) {
        auto value=scenario==0?0ULL:next(seed);
        if(scenario==1)value%=200;
        if(scenario==2&&value%4==0)value=0;
        counter.set(counter.pointer,value);
    }
    g_gamepadBackendCost.~Counters();new(&g_gamepadBackendCost)ymcc::input::backendCost::Counters;
    if(scenario) {
        for(std::size_t i=0;i<ymcc::input::backendCost::kXinputRouteCount;++i)
            for(unsigned c=0;c<(scenario==2?11u:3u);++c)g_gamepadBackendCost.xinput[i].record((i+1)*(c+1),c?0xffffffffu-c:0u,c==0);
        g_gamepadBackendCost.system32.record(0,0,true);g_gamepadBackendCost.msiPoll.record(500,0x80004005u,false);g_gamepadBackendCost.msiGetDeviceState.record(0,0,true);
    }
    traceSize=0;gateChecks=0;appendCalls=0;wireCapture.clear();rootCapture=nullptr;
}
static std::vector<std::uint64_t> residues() {std::vector<std::uint64_t> result;for(const auto& counter:counters)result.push_back(counter.get(counter.pointer));return result;}
static void emitted(bool candidate,long long window) {if(candidate)candidateEmit(window);else beforeEmit(window);}
static json paritySuite() {
    tracing=true;loggerMode=true;captureRoot=true;captureWire=true;logEnabled=true;
    const std::array<long long,8> windows{0,-1,1,1000,1001,7999,std::numeric_limits<long long>::min(),std::numeric_limits<long long>::max()};
    std::uint64_t cases=0, traces=0;std::uint64_t random=0x983154662ULL;
    for(unsigned test=0;test<96;++test) {
        unsigned scenario=test%3;auto seed=next(random);const auto window=windows[test%windows.size()];
        seedFixture(scenario,seed);emitted(false,window);json baseline=rootCapture;auto wire=wireCapture;auto remaining=residues();auto baselineTrace=trace;auto count=traceSize;
        check(appendCalls==1,"baseline row cadence changed");check(gateChecks==2,"logger gate recheck changed");
        emitted(false,window);json baselineEmpty=rootCapture;auto emptyTrace=trace;auto emptyCount=traceSize;
        seedFixture(scenario,seed);emitted(true,window);exact(baseline,rootCapture);check(baseline.dump()==rootCapture.dump(),"root dump bytes differ");check(wire==wireCapture,"fixed logger copy+dump bytes differ");json candidateRoot=rootCapture;check(residues()==remaining,"counter residue mismatch");
        check(traceSize==count,"atomic extraction count mismatch");for(std::size_t i=0;i<count;++i)check(trace[i]==baselineTrace[i],"atomic operation/order/value mismatch");
        check(appendCalls==1,"candidate row cadence changed");check(gateChecks==2,"candidate logger gate count changed");
        emitted(true,window);exact(baselineEmpty,rootCapture);check(traceSize==emptyCount,"second drain trace size mismatch");for(std::size_t i=0;i<emptyCount;++i)check(trace[i]==emptyTrace[i],"second drain order mismatch");
        traces+=count;
        for(auto it=baseline["stages"].begin();it!=baseline["stages"].end();++it) {
            const auto& value=it.value();check(value["n"].type()==(value["n"]==0?json::value_t::number_integer:json::value_t::number_unsigned),"stage n type lost literal signed zero");
            if(value.size()>1)check(value["meanUs"].is_number_unsigned()&&value["maxUs"].is_number_unsigned(),"integer stage mean/max type changed");
        }
#ifdef YMCC_FAULT_INJECT
        check(baseline.contains("fiPwrCapsCalls")&&baseline["fiPwrCapsCalls"].is_number_unsigned(),"FI field missing or changed type");
#else
        check(!baseline.contains("fiPwrCapsCalls"),"FI field leaked into production fixture");
#endif
        // Actual Native logger has unchanged ownership-by-value and dump body.
        // Its file/clock/stream calls bind to memory, preserving metadata types.
        g_nativeLifecycleFrameWrites=254;g_nativeDetailWrites=254;g_nativeDetailLastFlushTick=0;mirrorCalls=0;g_domainLogEnabled[1]=true;
        appendNativeLifecycleLog("fixture",baseline);auto nativeWire=wireCapture;
        g_nativeLifecycleFrameWrites=254;g_nativeDetailWrites=254;g_nativeDetailLastFlushTick=0;mirrorCalls=0;
        appendNativeLifecycleLog("fixture",candidateRoot);check(nativeWire==wireCapture,"native fixed metadata/dump bytes differ");
        ++cases;
    }
    // Child transfer types cover values absent from today's
    // integer-only cost schema without inventing float conversions in production.
    auto child=specialValues();
    for(unsigned i=0;i<64;++i) {
        json original={{"child",child},{"rootZero",0}};json candidate=json::object();json moved=child;candidate["child"]=std::move(moved);candidate["rootZero"]=0;
        exact(original,candidate);check(original.dump()==candidate.dump(),"nested transfer exact dump mismatch");check(moved.is_null(),"single-use child not moved");
    }
    // Demonstrate why adding move inside an initializer-list is insufficient.
    json moved=child;allocationCalls=0;allocationCounting=true;json list={{"child",std::move(moved)}};allocationCounting=false;const auto listAllocations=allocationCalls;
    moved=child;allocationCalls=0;allocationCounting=true;json direct=json::object();direct["child"]=std::move(moved);allocationCounting=false;
    check(listAllocations>allocationCalls,"initializer-list move no longer copies; reevaluate candidate");exact(list,direct);check(list.dump()==direct.dump(),"initializer-list vs direct transfer bytes differ");
    for(bool closeOnRecheck:{false,true}) {
        logEnabled=false;gateClosesOnRecheck=closeOnRecheck;seedFixture(1,3);emitted(true,1000);check(appendCalls==0,"gate-off wrote a row");
        logEnabled=true;gateClosesOnRecheck=closeOnRecheck;seedFixture(1,3);emitted(true,1000);check(appendCalls==(closeOnRecheck?0u:1u),"logger recheck gate changed");
    }
    gateClosesOnRecheck=false;logEnabled=true;tracing=false;captureRoot=false;
    return {{"emissionCases",cases},{"atomicEventsChecked",traces},{"secondDrainVerified",true},{"exactTypeAndIntegerDoubleBits",true},{"rootAndFixedLoggerDumpBytes",true},{"nativeLoggerBoundToMemory",true},{"actualCallbackWindowDrainBound",true},{"callbackScopeStartEndImplementationOwnedByParent",true},{"initializerListMoveDoesNotTransferChild",true}};
}
struct Cost {double ns{},calls{},bytes{};};
static Cost measure(bool candidate,unsigned scenario,bool logger,bool counting,int repetitions) {
    tracing=false;loggerMode=logger;captureWire=false;captureRoot=false;
    allocationCalls=allocationBytes=0;double elapsed=0;
    for(int i=0;i<repetitions;++i) {
        seedFixture(scenario,0xfdbc661ULL+unsigned(i));
        allocationCounting=counting;
        const auto start=std::chrono::steady_clock::now();
        emitted(candidate,1000);sink+=rootCapture.size();rootCapture=nullptr;
        const auto stop=std::chrono::steady_clock::now();
        allocationCounting=false;elapsed+=std::chrono::duration<double,std::nano>(stop-start).count();
    }
    return {elapsed/repetitions,double(allocationCalls)/repetitions,double(allocationBytes)/repetitions};
}
static double median(std::vector<double> values) {std::sort(values.begin(),values.end());return values[values.size()/2];}
static json benchmarkSuite() {
    json rows=json::array();const char* names[]{"zero-window","populated-window","overflow-and-wide-integers"};
    for(unsigned scenario=0;scenario<3;++scenario)for(bool logger:{false,true}) {
        measure(false,scenario,logger,false,32);measure(true,scenario,logger,false,32);
        std::vector<double> before,candidate;unsigned wins=0;
        for(unsigned round=0;round<11;++round) {
            Cost a,b;
            // Rotate the first measured variant each round; no priority changes.
            if(round%2) {b=measure(true,scenario,logger,false,48);a=measure(false,scenario,logger,false,48);}
            else {a=measure(false,scenario,logger,false,48);b=measure(true,scenario,logger,false,48);}
            before.push_back(a.ns);candidate.push_back(b.ns);wins+=b.ns<a.ns;
        }
        const auto a=measure(false,scenario,logger,true,12),b=measure(true,scenario,logger,true,12);
        check(b.calls<a.calls&&b.bytes<a.bytes,"ownership candidate lacks allocation reduction");
        rows.push_back({{"scenario",names[scenario]},{"fixedLoggerCopyAndDumpIncluded",logger},{"warmedEventsPerVariant",32},{"rotatingRounds",11},{"eventsPerRoundPerVariant",48},{"beforeRoundsNs",before},{"candidateRoundsNs",candidate},{"beforeMedianNs",median(before)},{"candidateMedianNs",median(candidate)},{"candidateFasterRounds",wins},{"consistentBenefit",wins>=10&&median(candidate)<median(before)},{"beforeAllocationCalls",a.calls},{"candidateAllocationCalls",b.calls},{"beforeRequestedBytes",a.bytes},{"candidateRequestedBytes",b.bytes},{"setupOutsideTimingAndAllocationCount",true},{"scope","in-process emission plus destruction; fixed logger uses inert stream/clock; no disk I/O"}});
    }
    return rows;
}
int main(int argc,char** argv) {
    try {
        bindCounters();const auto parity=paritySuite();const bool benchmark=argc>1&&std::string_view(argv[1])=="benchmark";
        auto costs=benchmark?benchmarkSuite():json::array();
        std::cout<<json{{"pass",true},{"assertions",assertions},{"parity",parity},{"benchmark",costs},{"sink",sink},{"noApplicationDeviceOrOSAPIExecution",true}}.dump(2)<<'\n';return 0;
    } catch(const std::exception& error) {allocationCounting=false;std::cerr<<error.what()<<'\n';return 1;}
}
