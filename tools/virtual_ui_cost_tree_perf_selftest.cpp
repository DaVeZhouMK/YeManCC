// R14 isolated current-source UI JSON fixture. No application/Windows headers.
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "json.hpp"
#include "log_frequency_policy.h"
using json = nlohmann::json;
static bool allocationCounting = false;
static std::uint64_t allocationCalls = 0, allocationBytes = 0;
static void allocation(std::size_t bytes) { if (allocationCounting) { ++allocationCalls; allocationBytes += bytes; } }
void* operator new(std::size_t bytes) { allocation(bytes); if (auto p = std::malloc(bytes ? bytes : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#ifdef _MSC_VER
#include <malloc.h>
void* operator new(std::size_t bytes, std::align_val_t alignment) { allocation(bytes); if (auto p = _aligned_malloc(bytes ? bytes : 1, std::size_t(alignment))) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t bytes, std::align_val_t alignment) { return ::operator new(bytes, alignment); }
void operator delete(void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { _aligned_free(p); }
#endif
static std::uint64_t assertions = 0, sink = 0;
static void check(bool okay, const char* message) { ++assertions; if (!okay) throw std::runtime_error(message); }
static std::uint64_t next(std::uint64_t& state) { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; }
struct Event { unsigned id, operation, order; std::uint64_t input, output; bool operator==(const Event&) const = default; };
static std::array<Event,512> trace;
static std::size_t traceSize = 0;
static bool tracing = false;
#ifdef UI_TREE_TRACE
struct Counter {
    std::atomic<unsigned long long> value{0}; unsigned id = 0;
    Counter() = default;
    Counter(unsigned long long v) : value(v) {}
    unsigned long long exchange(unsigned long long v, std::memory_order order) {
        const auto result = value.exchange(v, order);
        if (tracing) { check(traceSize < trace.size(), "trace full"); trace[traceSize++] = {id,1,unsigned(order),v,result}; }
        return result;
    }
    unsigned long long load(std::memory_order order) const {
        const auto result = value.load(order);
        if (tracing) { check(traceSize < trace.size(), "trace full"); trace[traceSize++] = {id,2,unsigned(order),0,result}; }
        return result;
    }
};
#else
using Counter = std::atomic<unsigned long long>;
#endif
struct CounterLink { Counter* pointer; std::string name; bool key; };
static std::vector<CounterLink> counters;
static void registerCounter(Counter& counter, std::string name, bool key) {
#ifdef UI_TREE_TRACE
    counter.id = unsigned(counters.size());
#endif
    counters.push_back({&counter,std::move(name),key});
}
static std::atomic<unsigned long long>& atomic(Counter& counter) {
#ifdef UI_TREE_TRACE
    return counter.value;
#else
    return counter;
#endif
}
#include "context.hpp"
static bool logEnabled = true, closeOnRecheck = false, loggerMode = true, captureRoot = true, captureWire = true;
static unsigned gateChecks = 0, appendCalls = 0, pathCalls = 0, pidCalls = 0, timestampCalls = 0, tickCalls = 0;
static bool lastUrgent = false;
static json rootCapture;
static std::string wireCapture;
static std::mutex g_domainLogMx;
static constexpr unsigned long long kLogMaxBytes = 1024 * 1024;
// Actual logger source calls bind to fixed values or a memory sink. No OS clocks,
// files, timer APIs, HWND/WM/device functions are linked or executed.
static bool domainLogEnabled(const char*) { ++gateChecks; return logEnabled && !(closeOnRecheck && gateChecks > 1); }
static int domainLogStreamIndex(const char* domain) { return domain && std::string_view(domain) == "virtual" ? 0 : -1; }
static std::wstring domainLogPath(const char*) { ++pathCalls; return L"memory-only"; }
static std::uint32_t GetCurrentProcessId() { ++pidCalls; return 321; }
static std::string nativeLifecycleTimestamp() { ++timestampCalls; return "2026-10-15T00:00:00.000+08:00"; }
static unsigned long long GetTickCount64() { ++tickCalls; return 1000; }
static void rotateOneLogFile(const std::wstring&, unsigned long long) { throw std::runtime_error("rotation must not run"); }
struct MemoryStream {
    template<class Rotate> void append(const std::wstring& path, const std::string& text, unsigned long long tick, bool urgent, unsigned long long max, Rotate) {
        ++appendCalls; lastUrgent = urgent;
        check(path == L"memory-only" && tick == 1000 && max == kLogMaxBytes, "logger arguments changed");
        sink += text.size(); if (captureWire) wireCapture = text;
    }
};
static MemoryStream g_domainLogStreams[1];
#include "logger.hpp"
static void appendDomainLog(const char* domain, const char* event, const json& detail) {
    if (captureRoot) rootCapture = detail;
    sink += detail.size();
    if (loggerMode) fixedAppendDomainLog(domain,event,detail);
}
#include "before.hpp"
#include "candidate.hpp"
static void emitted(bool candidate, long long window) {
    if (candidate) candidate_emitUiCostWindow(window); else before_emitUiCostWindow(window);
}
static void exact(const json& a, const json& b) {
    check(a.type() == b.type(), "JSON recursive type mismatch");
    switch (a.type()) {
    case json::value_t::object:
        check(a.size() == b.size(), "object size mismatch");
        for (auto i=a.begin(),j=b.begin(); i!=a.end(); ++i,++j) { check(i.key()==j.key(), "key bytes/order mismatch"); exact(i.value(),j.value()); } break;
    case json::value_t::array:
        check(a.size()==b.size(), "array length mismatch"); for (std::size_t i=0;i<a.size();++i) exact(a[i],b[i]); break;
    case json::value_t::number_integer: check(a.get<json::number_integer_t>()==b.get<json::number_integer_t>(), "signed integer mismatch"); break;
    case json::value_t::number_unsigned: check(a.get<json::number_unsigned_t>()==b.get<json::number_unsigned_t>(), "unsigned integer mismatch"); break;
    case json::value_t::number_float: check(std::bit_cast<std::uint64_t>(a.get<double>())==std::bit_cast<std::uint64_t>(b.get<double>()), "floating bits mismatch"); break;
    default: check(a==b, "scalar mismatch"); break;
    }
}
static void set(Counter& counter, unsigned long long v) { atomic(counter).store(v,std::memory_order_relaxed); }
static std::vector<unsigned long long> residues() {
    std::vector<unsigned long long> values; for (auto& c:counters) values.push_back(atomic(*c.pointer).load(std::memory_order_relaxed)); return values;
}
static constexpr unsigned scenarioCount = 6;
static const char* scenarioNames[scenarioCount] = {"zero-window","sparse-slots","all-slots-full","ties-at-top-boundary","wide-unsigned-values","unpublished-and-zero-count"};
static void seedFixture(unsigned scenario, std::uint64_t state) {
    for (auto& c:counters) set(*c.pointer,0);
    for (int i=0;i<kUiCostStageCount;++i) {
        auto n = scenario==0 ? 0ULL : next(state)%19;
        if (scenario==4) n=std::numeric_limits<unsigned long long>::max()-unsigned(i);
        set(g_uiCostCount[i],n); set(g_uiCostSumUs[i],scenario==0?0:next(state)); set(g_uiCostMaxUs[i],scenario==0?0:next(state));
    }
    for (auto table:{&g_uiTimerTable,&g_uiSerialTable,&g_uiEventTable}) {
        for (int i=0;i<table->slots;++i) {
            auto key = 100ULL + unsigned(i)*71;
            auto n = 1ULL + next(state)%29;
            auto sum = next(state)%100000;
            auto mx = next(state)%50000;
            if (scenario==0) key=n=sum=mx=0;
            if (scenario==1 && i%4) key=n=sum=mx=0;
            if (scenario==3) sum = i<2?9000:7000; // ties cross both top3/top5 cutoffs
            if (scenario==4) { key=std::numeric_limits<unsigned long long>::max()-unsigned(i); n=key; sum=key-7; mx=key-9; }
            if (scenario==5) { if(i%3==0)key=0; if(i%3==1)n=0; } // rejected rows still drain all three
            set(table->keys[i],key); set(table->count[i],n); set(table->sumUs[i],sum); set(table->maxUs[i],mx);
            if (table->names) {
                const char* names[]{"gyro.telemetry","quote\"\\line\n\t","\xe5\xad\x97\xe7\xac\xa6", "", "duplicate", "duplicate", "123456789012345678901234567890123456789"};
                std::snprintf(table->names[i],kUiCostNameMax,"%s",names[i%7]);
            }
        }
    }
    for (auto bucket:{&g_uiTimerOverflow,&g_uiSerialOverflow,&g_uiEventOverflow}) {
        set(bucket->count,scenario==0||scenario==1?0:next(state)|1ULL);
        set(bucket->sumUs,scenario==0?0:next(state)); set(bucket->maxUs,scenario==0?0:next(state));
    }
    for (auto c:{&g_uiCostWmInputFiltered,&g_c15aHeaderProbes,&g_c15aNegativeHits,&g_c15aHeaderFallbacks,&g_c17aOwnedEvents,&g_c17aParityChecks,&g_c17aParityFailures}) set(*c,scenario==0?0:next(state));
    traceSize=0; gateChecks=appendCalls=pathCalls=pidCalls=timestampCalls=tickCalls=0; wireCapture.clear(); rootCapture=nullptr;
}
static unsigned idOf(std::string_view name) {
    for(unsigned i=0;i<counters.size();++i)if(counters[i].name==name)return i;
    throw std::runtime_error("counter name absent");
}
static void expectedTrace(const std::vector<unsigned long long>& initial) {
#ifdef UI_TREE_TRACE
    std::vector<Event> expected;
    auto read=[&](std::string name,bool key=false){ auto id=idOf(name); expected.push_back({id,key?2u:1u,unsigned(key?std::memory_order_acquire:std::memory_order_acq_rel),0,initial[id]}); };
    auto stage=[&](int i){for(const char* field:{"g_uiCostCount","g_uiCostSumUs","g_uiCostMaxUs"})read(std::string(field)+"["+std::to_string(i)+"]");};
    auto table=[&](const char* prefix,int slots){for(int i=0;i<slots;++i)for(const char* field:{"Keys","Count","SumUs","MaxUs"})read(std::string(prefix)+field+"["+std::to_string(i)+"]",std::string_view(field)=="Keys");};
    auto overflow=[&](const char* prefix){for(const char* field:{"count","sumUs","maxUs"})read(std::string(prefix)+"."+field);};
    stage(0);stage(1);read("g_uiCostWmInputFiltered");stage(2);table("g_uiTimer",12);overflow("g_uiTimerOverflow");
    stage(3);table("g_uiSerial",12);overflow("g_uiSerialOverflow");stage(4);table("g_uiEvent",24);overflow("g_uiEventOverflow");
    for(const char* name:{"g_c15aHeaderProbes","g_c15aNegativeHits","g_c15aHeaderFallbacks","g_c17aOwnedEvents","g_c17aParityChecks","g_c17aParityFailures"})read(name);
    check(expected.size()==223 && traceSize==expected.size(),"expected 223 reads/drains");
    for(std::size_t i=0;i<expected.size();++i)check(trace[i]==expected[i],"read/drain value, order or memory order changed");
#else
    (void)initial;
#endif
}
static void checkResidue(const std::vector<unsigned long long>& initial) {
    auto remaining=residues(); for(std::size_t i=0;i<counters.size();++i)check(remaining[i]==(counters[i].key?initial[i]:0),"key persistence/drain residue changed");
}
static void schema(const json& value) {
    check(value["windowMs"].is_number_integer(),"window signed type changed");
    for(const char* stage:{"wndProc","wmInput","wmTimer","serialEvent","ipcEmit"}) {
        const auto& row=value[stage];
        check(row["n"].type()==(row["n"]==0?json::value_t::number_integer:json::value_t::number_unsigned),"stage literal signed zero changed");
        if(row.contains("totalUs"))for(const char* name:{"totalUs","meanUs","maxUs"})check(row[name].is_number_unsigned(),"stage number type changed");
        if(row.contains("overflow")) {const auto& overflow=row["overflow"];check(overflow.size()==3,"overflow schema changed");for(const char* field:{"n","totalUs","maxUs"})check(overflow[field].is_number_unsigned(),"overflow unsigned type changed");}
    }
    check(value["wmInput"]["filtered"].is_number_unsigned(),"filtered unsigned zero changed");
    for(const char* name:{"headerProbes","negativeHits","headerFallbacks"})check(value["c15a"][name].is_number_unsigned(),"c15a unsigned type changed");
    for(const char* name:{"ownedEvents","parityChecks","parityFailures"})check(value["c17a"][name].is_number_unsigned(),"c17a unsigned type changed");
}
static json witnesses = json::array();
static json typedTree(const json& value, std::string path = "") {
    json rows = json::array();
    rows.push_back({{"path",path},{"type",value.type_name()},{"dump",value.dump()}});
    // type_name() calls all numeric variants "number"; record value_t explicitly.
    rows.back()["valueType"] = int(value.type());
    if(value.is_object())for(auto it=value.begin();it!=value.end();++it) {
        auto child=typedTree(it.value(),path+"/"+it.key());for(auto& row:child)rows.push_back(std::move(row));
    }
    else if(value.is_array())for(std::size_t i=0;i<value.size();++i) {
        auto child=typedTree(value[i],path+"/"+std::to_string(i));for(auto& row:child)rows.push_back(std::move(row));
    }
    return rows;
}
static json traceWitness(const std::array<Event,512>& events, std::size_t count) {
    json rows=json::array();
    for(std::size_t i=0;i<count;++i) {const auto& e=events[i];rows.push_back({{"counter",counters[e.id].name},{"operation",e.operation==1?"exchange":"load"},{"memoryOrder",e.operation==1?"acq_rel":"acquire"},{"input",e.input},{"output",e.output}});}
    return rows;
}
static json paritySuite() {
    tracing=true;loggerMode=captureRoot=captureWire=true;logEnabled=true;closeOnRecheck=false;
    std::uint64_t cases=0, traceEvents=0, random=0x145763d92ULL;
    const std::array<long long,8> windows{0,-1,1,1000,1001,7999,std::numeric_limits<long long>::min(),std::numeric_limits<long long>::max()};
    for(unsigned test=0;test<144;++test) {
        const auto seed=next(random);const auto scenario=test%scenarioCount;const auto window=windows[test%windows.size()];
        seedFixture(scenario,seed);auto initial=residues();emitted(false,window);
        json before=rootCapture;auto wire=wireCapture;auto firstTrace=trace;auto count=traceSize;
        expectedTrace(initial);checkResidue(initial);schema(before);
        check(appendCalls==1 && gateChecks==2 && !lastUrgent,"original logger cadence/gates/flush changed");
        check(pidCalls==1&&timestampCalls==1&&tickCalls==1&&pathCalls==1,"original logger body did not execute");
        traceSize=0;auto drained=residues();emitted(false,window);json beforeEmpty=rootCapture;auto emptyWire=wireCapture;auto emptyTrace=trace;auto emptyCount=traceSize;
        expectedTrace(drained);checkResidue(drained);
        seedFixture(scenario,seed);emitted(true,window);exact(before,rootCapture);check(before.dump()==rootCapture.dump(),"root dump bytes changed");check(wire==wireCapture,"actual logger copy+dump bytes changed");
        expectedTrace(initial);checkResidue(initial);schema(rootCapture);
        check(traceSize==count,"trace length changed");for(std::size_t i=0;i<count;++i)check(trace[i]==firstTrace[i],"original/candidate trace changed");
        check(appendCalls==1&&gateChecks==2&&!lastUrgent,"candidate logger gates/flush changed");
        check(pidCalls==1&&timestampCalls==1&&tickCalls==1&&pathCalls==1,"candidate logger body did not execute");
        if(test<scenarioCount) witnesses.push_back({{"scenario",scenarioNames[scenario]},{"originalTypedTree",typedTree(before)},{"candidateTypedTree",typedTree(rootCapture)},{"originalDump",before.dump()},{"candidateDump",rootCapture.dump()},{"originalLoggerDump",wire},{"candidateLoggerDump",wireCapture},{"originalTrace",traceWitness(firstTrace,count)},{"candidateTrace",traceWitness(trace,traceSize)},{"originalSecondDrainTrace",traceWitness(emptyTrace,emptyCount)}});
        traceSize=0;emitted(true,window);exact(beforeEmpty,rootCapture);check(emptyWire==wireCapture,"second-drain logger bytes changed");
        expectedTrace(drained);checkResidue(drained);check(traceSize==emptyCount,"second drain trace length changed");
        for(std::size_t i=0;i<emptyCount;++i)check(trace[i]==emptyTrace[i],"second-drain trace changed");
        if(test<scenarioCount)witnesses.back()["candidateSecondDrainTrace"]=traceWitness(trace,traceSize);
        traceEvents+=2*(count+emptyCount); ++cases;
    }
    // Helpers outside emitter: numeric top rows, empty/nonempty overflows,
    // topN=0 and greater-than-table-size, names with duplicate/escaped UTF8 bytes.
    for(unsigned scenario=0;scenario<scenarioCount;++scenario)for(unsigned tableIndex=0;tableIndex<3;++tableIndex)for(int topN:{0,1,3,5,12,24,30}) {
        auto* table=tableIndex==0?&g_uiTimerTable:tableIndex==1?&g_uiSerialTable:&g_uiEventTable;
        seedFixture(scenario,71);auto initial=residues();auto before=before_uiCostTopJson(*table,topN);auto first=trace;auto count=traceSize;auto remaining=residues();
        seedFixture(scenario,71);auto after=candidate_uiCostTopJson(*table,topN);exact(before,after);check(before.dump()==after.dump(),"top helper exact bytes changed");check(residues()==remaining,"top helper residues changed");
        check(traceSize==count,"top helper trace length changed");for(std::size_t i=0;i<count;++i)check(trace[i]==first[i],"top helper trace changed");
        for(const auto& row:after) {for(const char* field:{"n","totalUs","meanUs","maxUs"})check(row[field].is_number_unsigned(),"top helper signed number");check(table->names?row["name"].is_string():row["timerId"].is_number_unsigned(),"name/key type changed");}
        ++cases;
    }
    for(auto overflow:{&g_uiTimerOverflow,&g_uiSerialOverflow,&g_uiEventOverflow})for(bool populated:{false,true}) {
        seedFixture(0,19);set(overflow->count,populated?std::numeric_limits<unsigned long long>::max():0);set(overflow->sumUs,123);set(overflow->maxUs,999);
        auto before=before_uiCostOverflowJson(*overflow);
        set(overflow->count,populated?std::numeric_limits<unsigned long long>::max():0);set(overflow->sumUs,123);set(overflow->maxUs,999);
        auto after=candidate_uiCostOverflowJson(*overflow);exact(before,after);check(before.dump()==after.dump(),"overflow helper dump changed");
        check(after["n"].type()==(populated?json::value_t::number_unsigned:json::value_t::number_integer),"overflow literal zero type changed");
        check(after.size()==(populated?3u:1u),"overflow zero fields changed");
        check(atomic(overflow->count).load()==0&&atomic(overflow->sumUs).load()==0&&atomic(overflow->maxUs).load()==0,"overflow zero-count values not drained");++cases;
    }
    // Emitter has no entry gate; the caller gates it. If it is called while the
    // logger gate closes, current source still drains before logger rejection.
    for(bool candidate:{false,true})for(unsigned mode=0;mode<3;++mode) {
        logEnabled=mode!=0;closeOnRecheck=mode==1;seedFixture(2,17);auto initial=residues();emitted(candidate,1000);expectedTrace(initial);checkResidue(initial);
        check(appendCalls==(mode==2?1u:0u),"logger off/recheck gate changed");check(gateChecks==(mode==0?1u:2u),"logger gate check count changed");
        check(pathCalls==(mode==2?1u:0u),"gate rejected too late");++cases;
    }
    logEnabled=true;closeOnRecheck=false;tracing=false;captureRoot=captureWire=false;
    return {{"cases",cases},{"traceEventsCompared",traceEvents},{"expectedReadsPerEmit",223},{"recursiveTypesNumbersAndDumpBytes",true},{"actualLoggerCopyDumpBytes",true},{"secondDrainAndKeyPersistence",true},{"allSlotsDrainedBeforeSkip",true},{"literalSignedZeroAndUnsignedCounters",true},{"arrayOrderAndPartialSortTieBehavior",true},{"offAndRecheckGates",true}};
}
struct Cost { double ns,calls,bytes; };
static Cost measure(bool candidate,unsigned scenario,bool logger,bool counting,int repetitions,std::uint64_t seed) {
    tracing=captureRoot=captureWire=false;loggerMode=logger;allocationCalls=allocationBytes=0;
    double elapsed=0;
    for(int i=0;i<repetitions;++i) {
        seedFixture(scenario,seed+unsigned(i)); // outside timing and allocation region
        allocationCounting=counting;
        const auto start=std::chrono::steady_clock::now();emitted(candidate,1000);const auto stop=std::chrono::steady_clock::now();
        allocationCounting=false;elapsed+=std::chrono::duration<double,std::nano>(stop-start).count();
    }
    return {elapsed/repetitions,double(allocationCalls)/repetitions,double(allocationBytes)/repetitions};
}
static double median(std::vector<double> values) {std::sort(values.begin(),values.end());return values[values.size()/2];}
static json benchmarkSuite(unsigned campaign) {
#ifdef UI_TREE_TRACE
    throw std::runtime_error("trace instrumentation must not be benchmarked");
#endif
    json rows=json::array();
    constexpr unsigned rounds=11;constexpr int repetitions=128,warm=64;
    // Rotate scenario order across campaigns and variant order within paired
    // rounds. No CPU affinity, priority, sleeps, real timers, files or devices.
    for(unsigned step=0;step<scenarioCount;++step)for(bool logger:{false,true}) {
        const unsigned scenario=(step+campaign)%scenarioCount;
        const auto seed=0x143ad625ULL+campaign*107ULL;
        measure(false,scenario,logger,false,warm,seed);measure(true,scenario,logger,false,warm,seed);
        std::vector<double> before,candidate;unsigned wins=0;
        for(unsigned round=0;round<rounds;++round) {
            Cost a{},b{};auto roundSeed=seed+round*1009;
            if((round+campaign)%2) {b=measure(true,scenario,logger,false,repetitions,roundSeed);a=measure(false,scenario,logger,false,repetitions,roundSeed);}
            else {a=measure(false,scenario,logger,false,repetitions,roundSeed);b=measure(true,scenario,logger,false,repetitions,roundSeed);}
            before.push_back(a.ns);candidate.push_back(b.ns);wins+=b.ns<a.ns;
        }
        const auto a=measure(false,scenario,logger,true,24,seed),b=measure(true,scenario,logger,true,24,seed);
        const auto gain=1-median(candidate)/median(before);
        const bool benefit=wins>=10&&gain>=0.05&&b.calls<a.calls&&b.bytes<a.bytes;
        rows.push_back({{"scenario",scenarioNames[scenario]},{"loggerCopyAndDumpIncluded",logger},{"realStdAtomicCounters",true},{"warmedEventsPerVariant",warm},{"rotatingPairedRounds",rounds},{"eventsPerRoundPerVariant",repetitions},{"beforeRoundsNs",before},{"candidateRoundsNs",candidate},{"beforeMedianNs",median(before)},{"candidateMedianNs",median(candidate)},{"medianReductionFraction",gain},{"candidateFasterRounds",wins},{"beforeAllocationRequests",a.calls},{"candidateAllocationRequests",b.calls},{"beforeRequestedBytes",a.bytes},{"candidateRequestedBytes",b.bytes},{"consistentBenefit",benefit},{"acceptanceRule","at least 10/11 faster pairs, at least 5% median reduction, fewer allocation requests and bytes in every scope/campaign"},{"setupExcludedAndEmissionDestructionIncluded",true}});
    }
    return rows;
}
int main(int argc,char** argv) {
    try {
        bindCounters();const auto parity=paritySuite();const bool benchmark=argc>1&&std::string_view(argv[1])=="benchmark";
        const auto campaign=argc>2?unsigned(std::stoul(argv[2])):0;
        auto costs=benchmark?benchmarkSuite(campaign):json::array();
#ifdef UI_TREE_TRACE
        constexpr bool traceBuild=true;
#else
        constexpr bool traceBuild=false;
#endif
        std::cout<<json{{"pass",true},{"assertions",assertions},{"traceBuild",traceBuild},{"counterCount",counters.size()},{"parity",parity},{"benchmark",costs},{"witness",witnesses},{"sink",sink},{"noApplicationFileCurrentTimerHwndWmOrDeviceApis",true}}.dump(2)<<'\n';return 0;
    }catch(const std::exception& error){allocationCounting=false;std::cerr<<error.what()<<'\n';return 1;}
}
