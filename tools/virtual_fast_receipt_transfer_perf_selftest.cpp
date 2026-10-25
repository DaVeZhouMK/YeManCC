// R31 offline MSVC fixture: actual source scanner and complete fast branch.
// Only the final owning receipt assignment differs between timed variants.
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <memory>
#include <optional>
#include <limits>
#include <malloc.h>
#include <map>
#include <mutex>
#include <new>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

struct Allocations {
    unsigned long long ordinaryRequests=0, ordinaryBytes=0, alignedRequests=0, alignedBytes=0;
    void add(const Allocations& x) {
        ordinaryRequests+=x.ordinaryRequests; ordinaryBytes+=x.ordinaryBytes;
        alignedRequests+=x.alignedRequests; alignedBytes+=x.alignedBytes;
    }
};
static bool counting=false;
static Allocations allocated;
static long long failAfter=-1;
static void* watchedPointer=nullptr;
static void* watchedData=nullptr;
static unsigned long long watchedFrees=0;
static bool watchedReleased=false;
struct AllocationSpan {void* base;std::size_t bytes;};
static std::array<AllocationSpan,16384> allocationSpans{};
static std::size_t spanCount=0;
static bool trackOwnership=false;
static void noteAllocation(void* pointer,std::size_t bytes) noexcept {
    if(trackOwnership&&spanCount<allocationSpans.size())allocationSpans[spanCount++]={pointer,bytes};
}
static void watchAllocation(void* data) {
    watchedData=data;watchedPointer=data;watchedFrees=0;watchedReleased=false;
    const auto address=reinterpret_cast<std::uintptr_t>(data);
    for(std::size_t i=spanCount;i>0;--i) {
        const auto& span=allocationSpans[i-1];const auto begin=reinterpret_cast<std::uintptr_t>(span.base);
        if(address>=begin&&address-begin<span.bytes) {watchedPointer=span.base;return;}
    }
}
static void maybeFail() {
    if(failAfter==0) {failAfter=-1;throw std::bad_alloc();}
    if(failAfter>0)--failAfter;
}
static void released(void* p) noexcept {if(p&&p==watchedPointer&&!watchedReleased) {++watchedFrees;watchedReleased=true;}}
void* operator new(std::size_t n) {
    maybeFail();
    if(counting) { ++allocated.ordinaryRequests; allocated.ordinaryBytes+=n; }
    if(void* p=std::malloc(n?n:1)) {noteAllocation(p,n?n:1);return p;}
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { released(p);std::free(p); }
void operator delete[](void* p) noexcept { released(p);std::free(p); }
void operator delete(void* p,std::size_t) noexcept { released(p);std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { released(p);std::free(p); }
void* operator new(std::size_t n,std::align_val_t a) {
    maybeFail();
    if(counting) { ++allocated.alignedRequests; allocated.alignedBytes+=n; }
    if(void* p=_aligned_malloc(n?n:1,static_cast<std::size_t>(a))) {noteAllocation(p,n?n:1);return p;}
    throw std::bad_alloc();
}
void* operator new[](std::size_t n,std::align_val_t a) { return ::operator new(n,a); }
void operator delete(void* p,std::align_val_t) noexcept { released(p);_aligned_free(p); }
void operator delete[](void* p,std::align_val_t) noexcept { released(p);_aligned_free(p); }
void operator delete(void* p,std::size_t,std::align_val_t) noexcept { released(p);_aligned_free(p); }
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept { released(p);_aligned_free(p); }
void* operator new(std::size_t n,const std::nothrow_t&) noexcept { try{return ::operator new(n);}catch(...){return nullptr;} }
void* operator new[](std::size_t n,const std::nothrow_t&) noexcept { return ::operator new(n,std::nothrow); }
void operator delete(void* p,const std::nothrow_t&) noexcept { ::operator delete(p); }
void operator delete[](void* p,const std::nothrow_t&) noexcept { ::operator delete[](p); }
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept { try{return ::operator new(n,a);}catch(...){return nullptr;} }
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept { return ::operator new(n,a,std::nothrow); }
void operator delete(void* p,std::align_val_t a,const std::nothrow_t&) noexcept { ::operator delete(p,a); }
void operator delete[](void* p,std::align_val_t a,const std::nothrow_t&) noexcept { ::operator delete[](p,a); }

#include "json.hpp"
using json=nlohmann::json;
#include "input_host_selftest_rules.h"
#include "source-bound.h"

static unsigned long long assertions=0,failures=0,caseCount=0,differentialFailures=0,domFailures=0;
static unsigned long long ownershipCases=0,localCases=0,exceptionCases=0;
static json failureDetails=json::array(),caseEvidence=json::array(),events=json::array();
static bool captureEvents=false,diagnosticsEnabled=false;
static std::string throwPoint,failPoint;
static bool watchSource=false,sourceHeap=false;
static std::uint64_t outputDigest=1469598103934665603ULL;
static volatile unsigned long long sink=0;
static void check(bool pass,const std::string& why) {
    ++assertions;if(pass)return;++failures;
    if(failureDetails.size()<100)failureDetails.push_back(why);
}
static void digest(const std::string& text) {
    for(unsigned char c:text) {outputDigest^=c;outputDigest*=1099511628211ULL;}
    outputDigest^=0xff;outputDigest*=1099511628211ULL;
}
static json receiptJson(const InputHostFastReceipt& x) {
    return {{"used",x.used},{"firstFrame",x.firstFrame},{"submitElapsedUs",x.submitElapsedUs},{"hostInstanceId",x.hostInstanceId}};
}
static json observationJson(const inputHostSelfTestRules::ReceiptObservation& x) {
    return {{"attempted",x.attempted},{"transportFault",x.transportFault},{"receiptObject",x.receiptObject},
        {"tupleVerified",x.tupleVerified},{"statusIsString",x.statusIsString},{"detailIsString",x.detailIsString},
        {"status",x.status},{"detail",x.detail},{"hostInstanceId",x.hostInstanceId}};
}
static json globalsJson() {
    return {{"sequence",g_inputHostSequence},{"healPending",g_inputHostTupleHealPending.load()},
        {"lastSubmitElapsedUs",g_inputHostLastSubmitElapsedUs.load()},
        {"hostInstanceId",g_inputHostHostInstanceId},{"runId",g_inputHostRunId},{"startupNonce",g_inputHostStartupNonce},
        {"epoch",g_inputHostEpoch},{"configRevision",g_inputHostConfigRevision},
        {"boundPowerGeneration",g_inputHostBoundPowerGeneration},{"powerGeneration",g_powerGeneration.load()},
        {"buildPersona",g_inputHostBuildPersona},{"boundPersona",g_inputHostBoundPersona},
        {"configuredPersona",g_realStickTest.virtualTargetPersona}};
}
static bool tickCostEnabled() {return diagnosticsEnabled;}
static void tickCostAccumulate(TickCostStage stage,unsigned long long elapsed) {
    if(captureEvents)events.push_back({{"event","tick-cost"},{"stage",static_cast<int>(stage)},
        {"elapsedUs",elapsed},{"sequence",g_inputHostSequence},{"lastSubmitElapsedUs",g_inputHostLastSubmitElapsedUs.load()}});
}
static unsigned long long tickCostElapsedUs(std::chrono::steady_clock::time_point) {return 37;}
static void appendNativeLifecycleLog(const char* name,const json& row) {
    if(captureEvents)events.push_back({{"event","parity-log"},{"name",name},{"row",row}});
    if(throwPoint=="parity-log")throw std::runtime_error("fixture-parity-log");
}
static void r31TracePoint(const char* name,const InputHostFastReceipt& source) {
    if(captureEvents)events.push_back({{"event",name},{"source",receiptJson(source)},
        {"sequence",g_inputHostSequence},{"healPending",g_inputHostTupleHealPending.load()},
        {"lastSubmitElapsedUs",g_inputHostLastSubmitElapsedUs.load()}});
    if(watchSource&&std::strcmp(name,"transfer")==0) {
        const auto pointer=reinterpret_cast<std::uintptr_t>(source.hostInstanceId.data());
        const auto begin=reinterpret_cast<std::uintptr_t>(&source);
        sourceHeap=pointer<begin||pointer>=begin+sizeof(source);
        if(sourceHeap)watchAllocation(const_cast<char*>(source.hostInstanceId.data()));else {watchedPointer=nullptr;watchedData=nullptr;watchedFrees=0;}
    }
    if(throwPoint==name)throw std::runtime_error("fixture-"+throwPoint);
    if(failPoint==name)failAfter=0; // Arm after tracing: only actual subsequent source work may fail.
}
struct Input {
    std::string host,run,persona="steamdeck";
    std::string request="01234567-89AB-4CDE-8123-0123456789AB";
    std::string nonce="89ABCDEF-0123-4567-89AB-CDEF01234567";
    unsigned long long epoch=1,power=1,config=1,sequence=1,submit=17;
    bool first=false;
};
static Input fromRow(const json& row,std::size_t index=0) {
    const auto& r=row.at("receipt");Input x;
    x.host=r.at("hostInstanceId").get<std::string>();x.run=r.at("runId").get<std::string>();
    x.persona=r.at("persona").get<std::string>();x.epoch=r.at("epoch").get<unsigned long long>();
    x.sequence=r.at("commandSequence").get<unsigned long long>();x.power=row.at("powerGeneration").get<unsigned long long>();
    x.first=index==0;return x;
}
static void bind(const Input& x) {
    g_inputHostHostInstanceId=x.host;g_inputHostRunId=x.run;g_inputHostStartupNonce=x.nonce;
    g_inputHostEpoch=x.epoch;g_inputHostBoundPowerGeneration=x.power;g_powerGeneration.store(x.power);
    g_inputHostConfigRevision=x.config;g_inputHostSequence=x.sequence?x.sequence-1:0;
    g_inputHostTupleHealPending.store(true);g_inputHostLastSubmitElapsedUs.store(999);
    g_inputHostBuildPersona.clear();g_inputHostBoundPersona=x.persona;g_realStickTest.virtualTargetPersona=x.persona;
}
static std::string wire(const Input& x) {
    json values={{"protocolVersion",1},{"ok",true},{"command","SUBMIT_FRAME"},{"requestId",x.request},
        {"sequence",x.sequence},{"hostInstanceId",x.host},{"runId",x.run},{"epoch",x.epoch},{"powerGeneration",x.power},
        {"targetId",kInputHostTargetId},{"ownerIdentity",kInputHostOwnerIdentity},{"persona",x.persona},
        {"profileIdentity",inputHostProfileIdentityOf(x.persona)},{"startupNonce",x.nonce},{"configRevision",x.config},
        {"configHash",kInputHostProtocolConfigHash},{"status","frame-accepted"},{"detail",""},{"assetVerified",true},
        {"lockedCoreSha256",kInputHostLockedCoreSha256},{"lockedCoreMvid",kInputHostLockedCoreMvid},
        {"virtualEndpointReconciled",true},{"targetPnpAdmitted",true},{"targetPnpRootCount",1},
        {"targetPnpPrimaryRootCount",1},{"targetPnpXboxCompanionRootCount",0},{"targetPnpHidChildCount",0},
        {"targetPnpAllStarted",true},{"targetPnpDetail",""},{"firstFrame",x.first},
        {"releaseDisposition",nullptr},{"submitElapsedUs",x.submit}};
    std::string text="{";
    for(const char* name:r31WireFieldOrder) {if(text.size()>1)text+=',';text+=json(name).dump();text+=':';text+=values.at(name).dump();}
    text+='}';return text;
}
using Fast=bool(*)(const std::string&,const char*,const char*,const std::string&,unsigned long long,
    InputHostFastReceipt*,bool,bool,inputHostSelfTestRules::ReceiptObservation*);
using Transfer=void(*)(InputHostFastReceipt&,InputHostFastReceipt*);
struct Options {
    bool observe=false,diagnostic=false,advance=true,nullDestination=false,heal=true;
    std::string command="SUBMIT_FRAME",binding="normal",throwAt,failAt;
    int prefill=0;
};
struct Outcome {json state;bool sourceWasHeap=false,bufferTaken=false;unsigned long long sourceFrees=0;};
static Outcome execute(const Input& x,const std::string& bytes,bool after,const Options& opt) {
    bind(x);diagnosticsEnabled=opt.diagnostic;captureEvents=true;events=json::array();
    throwPoint=opt.throwAt;failPoint=opt.failAt;watchSource=true;watchedPointer=nullptr;watchedData=nullptr;sourceHeap=false;watchedFrees=0;
    spanCount=0;trackOwnership=true;
    if(!opt.heal)g_inputHostTupleHealPending.store(false);
    if(opt.binding=="empty-host")g_inputHostHostInstanceId.clear();
    if(opt.binding=="wrong-host")g_inputHostHostInstanceId="wrong-current-bound-host-id-00000";
    if(opt.binding=="empty-run")g_inputHostRunId.clear();
    if(opt.binding=="empty-nonce")g_inputHostStartupNonce.clear();
    InputHostFastReceipt dest;
    if(opt.prefill) {
        dest.used=true;dest.firstFrame=true;dest.submitElapsedUs=0xffffffffffffffffULL;
        dest.hostInstanceId=opt.prefill==1?"old":std::string(opt.prefill==2?32:4096,'P');
    }
    const auto initial=receiptJson(dest),globalInitial=globalsJson();
    inputHostSelfTestRules::ReceiptObservation obs;
    obs.status="old-status";obs.detail="old-detail";obs.hostInstanceId="old-host";
    const auto initialObs=observationJson(obs);
    std::string liveWire=bytes;bool accepted=false;std::string exception;
    
    try {
        Fast fn=after?candidateTraced:baselineTraced;
        accepted=fn(liveWire,opt.command.c_str(),"frame-accepted",x.request,x.sequence,
            opt.nullDestination?nullptr:&dest,opt.diagnostic,opt.advance,opt.observe?&obs:nullptr);
    } catch(const std::bad_alloc&) {exception="bad_alloc";}
      catch(const std::exception&) {exception="runtime_error";}
    failAfter=-1;throwPoint.clear();failPoint.clear();captureEvents=false;watchSource=false;trackOwnership=false;
    Outcome result;result.sourceWasHeap=sourceHeap;result.bufferTaken=sourceHeap&&dest.hostInstanceId.data()==watchedData;
    result.sourceFrees=watchedFrees;watchedPointer=nullptr;
    const auto finalGlobals=globalsJson();
    const auto saved=dest.hostInstanceId;
    std::fill(liveWire.begin(),liveWire.end(),'#');
    bool independent=dest.hostInstanceId==saved;
    if(!g_inputHostHostInstanceId.empty()) {
        const char old=g_inputHostHostInstanceId[0];g_inputHostHostInstanceId[0]=old=='X'?'Y':'X';
        independent=independent&&dest.hostInstanceId==saved;g_inputHostHostInstanceId[0]=old;
    }
    const auto pointer=reinterpret_cast<std::uintptr_t>(dest.hostInstanceId.data());
    const auto wireBegin=reinterpret_cast<std::uintptr_t>(liveWire.data());
    independent=independent&&(pointer<wireBegin||pointer>=wireBegin+liveWire.size());
    result.state={{"fastAccepted",accepted},{"exception",exception},{"destination",receiptJson(dest)},
        {"initialDestination",initial},{"observation",observationJson(obs)},{"initialObservation",initialObs},
        {"globals",finalGlobals},{"initialGlobals",globalInitial},{"events",events},{"ownedBeyondSourceAndInput",independent}};
    if(accepted) {
        ++ownershipCases;check(independent,"receipt survives local disposal and wire/global mutation");
        const auto expected=actualDomOracle(bytes.data(),bytes.data()+bytes.size(),"frame-accepted",x.request,x.sequence);
        check(dest.used&&expected.accepted&&dest.firstFrame==expected.firstFrame&&dest.submitElapsedUs==expected.submitUs&&dest.hostInstanceId==expected.host,"all four successful receipt fields exact");
        if(result.sourceWasHeap) {
            check(result.bufferTaken==after,"actual owning heap buffer transfer vs independent copy");
            check(result.sourceFrees==(after?0ULL:1ULL),"source disposal releases only its retained owned buffer");
        }
        check(finalGlobals.at("sequence")==json(opt.advance?x.sequence:globalInitial.at("sequence").get<unsigned long long>()),"commit order and advanceSequence policy");
        check(!finalGlobals.at("healPending").get<bool>()&&finalGlobals.at("lastSubmitElapsedUs")==dest.submitElapsedUs,"existing accepted side effects retained");
        if(opt.observe)check(obs.attempted&&obs.receiptObject&&obs.tupleVerified&&obs.statusIsString&&obs.status=="frame-accepted"&&obs.hostInstanceId==x.host,"observation owns pre-move values and publishes after source disposal");
        else check(observationJson(obs)==initialObs,"null observation slot performs no publication");
        check(events.size()>=3&&events.front().at("event")=="scanner-enter"&&events[1].at("event")=="accepted"&&events.back().at("event")=="transfer","source accepted before all side effects and transfer is last");
        const auto& transferEvent=events.back();
        check(transferEvent.at("sequence")==finalGlobals.at("sequence")&&transferEvent.at("lastSubmitElapsedUs")==dest.submitElapsedUs&&!transferEvent.at("healPending").get<bool>(),"all production effects complete before transfer");
        std::vector<int> stages;bool sawParity=false,sawObservation=false;
        for(const auto& event:events) {
            const auto eventName=event.at("event").get<std::string>();
            if(eventName=="tick-cost")stages.push_back(event.at("stage").get<int>());
            if(eventName=="parity") {
                sawParity=true;check(event.at("sequence")==globalInitial.at("sequence")&&event.at("lastSubmitElapsedUs")==globalInitial.at("lastSubmitElapsedUs")&&!event.at("healPending").get<bool>(),"parity occurs after tuple heal and before accepted bookkeeping");
            }
            if(eventName=="observation") {
                sawObservation=true;check(event.at("sequence")==globalInitial.at("sequence")&&event.at("lastSubmitElapsedUs")==dest.submitElapsedUs,"observation precedes sequence commit and receives complete pre-move receipt");
            }
        }
        check(sawParity==opt.diagnostic&&sawObservation==opt.observe,"diagnostic and observation call gates exact");
        check(stages==(opt.diagnostic?std::vector<int>{kTickCostReceiptValidate,kTickCostPipeWrite,kTickCostPipeRead,kTickCostHostSubmit}:std::vector<int>{}),"actual shared helper cost-call order exact");
    } else if(exception.empty()) {
        check(receiptJson(dest)==initial,"fast fallthrough/null destination leaves destination unchanged");
        check(finalGlobals==globalInitial,"fast fallthrough leaves source globals unchanged");
    }
    for(const char* key:{"hostInstanceId","runId","startupNonce","epoch","configRevision","boundPowerGeneration","powerGeneration","buildPersona","boundPersona","configuredPersona"})
        check(finalGlobals.at(key)==globalInitial.at(key),"input identity globals unchanged");
    return result;
}
static void pairCase(const std::string& name,const Input& x,const std::string& bytes,const Options& opt={},int expect=1,bool evidence=false) {
    ++caseCount;const auto b=execute(x,bytes,false,opt),a=execute(x,bytes,true,opt);
    const bool same=b.state==a.state;differentialFailures+=!same;check(same,name+": exact outputs, observation, globals, exceptions and call order");
    if(expect>=0)check(a.state.at("fastAccepted").get<bool>()==(expect!=0),name+": expected fast success/fallthrough");
    if(a.state.at("fastAccepted").get<bool>()) {
        const auto dom=actualDomOracle(bytes.data(),bytes.data()+bytes.size(),"frame-accepted",x.request,x.sequence);
        const auto& dest=a.state.at("destination");
        const bool safe=dom.accepted&&json(dom.firstFrame)==dest.at("firstFrame")&&json(dom.submitUs)==dest.at("submitElapsedUs")&&json(dom.host)==dest.at("hostInstanceId");
        domFailures+=!safe;check(safe,name+": actual DOM accepts same output");
    }
    digest(bytes);digest(b.state.dump());digest(a.state.dump());
    if(evidence)caseEvidence.push_back({{"case",name},{"baseline",b.state},{"candidate",a.state},
        {"sourceHeap",a.sourceWasHeap},{"candidateTakesBuffer",a.bufferTaken},
        {"baselineSourceFreesAtReturn",b.sourceFrees},{"candidateSourceFreesAtReturn",a.sourceFrees}});
}
static void localCase(const std::string& text,int prefill,bool used,bool first,unsigned long long elapsed,bool nullDest=false) {
    ++localCases;InputHostFastReceipt before,after;
    if(prefill)before.hostInstanceId=after.hostInstanceId=std::string(prefill==1?3:4096,'P');
    before.used=after.used=!used;before.firstFrame=after.firstFrame=!first;before.submitElapsedUs=after.submitElapsedUs=~elapsed;
    const auto original=receiptJson(before);const json expected={{"used",used},{"firstFrame",first},{"submitElapsedUs",elapsed},{"hostInstanceId",text}};
    for(bool move:{false,true}) {
        auto& dest=move?after:before;void* pointer=nullptr;bool heap=false;
        spanCount=0;trackOwnership=true;
        {
            InputHostFastReceipt source;source.used=used;source.firstFrame=first;source.submitElapsedUs=elapsed;source.hostInstanceId=text;
            const auto p=reinterpret_cast<std::uintptr_t>(source.hostInstanceId.data()),begin=reinterpret_cast<std::uintptr_t>(&source);
            heap=p<begin||p>=begin+sizeof(source);pointer=heap?source.hostInstanceId.data():nullptr;
            if(heap)watchAllocation(pointer);else {watchedPointer=nullptr;watchedData=nullptr;watchedFrees=0;}
            if(move)candidateTransfer(source,nullDest?nullptr:&dest);else baselineTransfer(source,nullDest?nullptr:&dest);
            if(!nullDest)check(receiptJson(dest)==expected,"local transfer all fields");
            else check(receiptJson(source)==expected,"null destination leaves source ownership and all fields intact");
        }
        const auto sourceFreeCount=watchedFrees;watchedPointer=nullptr;trackOwnership=false;
        check(receiptJson(dest)==(nullDest?original:expected),"local source disposal preserves destination");
        if(heap)check(sourceFreeCount==((move&&!nullDest)?0ULL:1ULL),"local owning buffer disposal count");
        if(heap&&move&&!nullDest)check(dest.hostInstanceId.data()==pointer,"actual std::string allocator buffer ownership");
    }
    check(receiptJson(before)==receiptJson(after),"local copy and move result exact");
    digest(receiptJson(before).dump());digest(receiptJson(after).dump());
}
struct Scenario {std::string name;bool control;std::vector<Input> inputs;};
static std::vector<Scenario> scenarios(const json& rows) {
    auto first=fromRow(rows.front()),last=fromRow(rows.back());
    std::vector<Scenario> out={{"archive-first-32hex",false,{first}},{"archive-last-32hex",false,{last}}};
    Scenario mixed{"archive-six-hosts-32hex",false,{}};
    for(const auto& row:rows) {auto x=fromRow(row);if(std::none_of(mixed.inputs.begin(),mixed.inputs.end(),[&](const auto& y){return y.host==x.host;}))mixed.inputs.push_back(std::move(x));}
    out.push_back(std::move(mixed));auto sso=first;sso.host="sso-id-8";out.push_back({"sso-8byte-control",true,{sso}});
    auto stress=first;stress.host=std::string(4096,'H');out.push_back({"stress-4096byte-control",true,{stress}});return out;
}
static void correctness(const json& rows) {
    static_assert(std::is_nothrow_move_assignable_v<InputHostFastReceipt>);
    static_assert(std::is_nothrow_move_assignable_v<std::string>);
    static_assert(std::is_same_v<decltype(InputHostFastReceipt::submitElapsedUs),unsigned long long>);
    static_assert(std::is_same_v<decltype(InputHostFastReceipt::hostInstanceId),std::string>);
    check(rows.size()==918,"authorized observation count");
    InputHostFastReceipt defaults;check(receiptJson(defaults)==json({{"used",false},{"firstFrame",false},{"submitElapsedUs",0ULL},{"hostInstanceId",""}}),"actual struct defaults");
    for(std::size_t n=0;n<rows.size();++n) {
        const auto x=fromRow(rows[n],n);const auto text=wire(x);Options opt;
        opt.observe=(n%3)==0;opt.diagnostic=(n%5)==0;opt.advance=(n%7)!=0;opt.prefill=static_cast<int>(n%4);
        pairCase("archived-observation-"+std::to_string(n),x,text,opt,1,n<2||n+1==rows.size());
    }
    auto seed=fromRow(rows.front());
    for(bool diagnostic:{false,true})for(bool observe:{false,true})for(bool advance:{false,true})for(bool heal:{false,true})for(int prefill=0;prefill<4;++prefill) {
        Options opt;opt.diagnostic=diagnostic;opt.observe=observe;opt.advance=advance;opt.heal=heal;opt.prefill=prefill;
        pairCase("gate-prefill-combination",seed,wire(seed),opt);
    }
    for(const auto& scenario:scenarios(rows))for(const auto& x:scenario.inputs)pairCase("declared-workload-"+scenario.name,x,wire(x),{},1,true);
    for(const auto& text:std::vector<std::string>{"", "x","sso-id-8",std::string(15,'A'),std::string(16,'A'),std::string(31,'A'),std::string(32,'A'),std::string(33,'A'),std::string(4096,'L'),"宿主😀",std::string("a\0b",3)})
        for(int prefill=0;prefill<3;++prefill)for(bool used:{false,true})for(bool first:{false,true})for(auto elapsed:{0ULL,1ULL,0xffffffffffffffffULL})
            localCase(text,prefill,used,first,elapsed);
    localCase(std::string(32,'N'),0,false,true,9,true);
    for(const auto& binding:std::vector<std::string>{"empty-host","wrong-host","empty-run","empty-nonce"}) {
        Options opt;opt.binding=binding;opt.observe=true;opt.prefill=3;pairCase("source-global-"+binding,seed,wire(seed),opt,0,true);
    }
    {Options opt;opt.nullDestination=true;opt.observe=true;pairCase("nullable-fastOut",seed,wire(seed),opt,0,true);}
    {Options opt;opt.command="HELLO";opt.observe=true;pairCase("non-frame-command",seed,wire(seed),opt,0,true);}
    auto good=wire(seed);
    for(std::size_t n=0;n<good.size();++n)pairCase("truncated-ack",seed,good.substr(0,n),{},0);
    for(const std::string& value:std::vector<std::string>{"{}","[]","null","{\"ok\":false}",good+"x",good+std::string("\0",1)})pairCase("malformed-or-incomplete",seed,value,{},0,true);
    for(const char* field:r31WireFieldOrder) {
        json obj=json::parse(good);obj.erase(field);const bool required=baselineScanner::lookupField(field,field+std::strlen(field))>=0&&std::strcmp(field,"firstFrame")&&std::strcmp(field,"submitElapsedUs");
        pairCase(std::string("missing-")+field,seed,obj.dump(),{},required?0:1);
    }
    for(const auto& token:std::vector<std::string>{R"("\uD800")",R"("\uDC00")","1e9999","-1","1.5","18446744073709551616"}) {
        const auto bad=good.substr(0,good.size()-1)+",\"unknown\":"+token+'}';pairCase("R28B-DOM-fallback",seed,bad,{},0,true);
    }
    for(const auto& token:std::vector<std::string>{R"("\uD800\uDC00")",R"({"a":[true,null,18446744073709551615]})"})
        pairCase("unknown-valid",seed,good.substr(0,good.size()-1)+",\"unknown\":"+token+'}',{},1,true);
    for(int depth:{31,32,33,34}) {
        const auto value=std::string(depth,'[')+'0'+std::string(depth,']');
        pairCase("depth-boundary",seed,good.substr(0,good.size()-1)+",\"unknown\":"+value+'}',{},depth<=32?1:0);
    }
    for(auto n:{0ULL,1ULL,0x7fffffffULL,0xffffffffULL,0x7fffffffffffffffULL,0xffffffffffffffffULL}) {
        auto x=seed;x.epoch=x.config=x.power=n;x.submit=n;x.sequence=n;pairCase("unsigned-boundaries",x,wire(x));
    }
    for(const auto& stage:std::vector<std::string>{"accepted","parity"}) {
        Options opt;opt.diagnostic=true;opt.observe=true;opt.throwAt=stage;
        pairCase("exception-"+stage,seed,good,opt,stage=="parity"?1:0,true);++exceptionCases;
    }
    {Options opt;opt.failAt="scanner-enter";
     pairCase("scanner-source-allocation-failure",seed,good,opt,0,true);++exceptionCases;}
    {Options opt;opt.observe=true;opt.failAt="observation";
     pairCase("observation-copy-allocation-failure",seed,good,opt,0,true);++exceptionCases;}
    // Explicitly document the intentional exception narrowing at the only edited
    // expression: completed prior side effects agree, but the removed copy may
    // throw bad_alloc whereas actual standard-allocator move assignment is noexcept.
    {
        Options opt;opt.failAt="transfer";const auto b=execute(seed,good,false,opt),a=execute(seed,good,true,opt);++exceptionCases;
        check(b.state.at("exception")=="bad_alloc"&&!b.state.at("fastAccepted").get<bool>(),"baseline final copy fault reproduced");
        check(a.state.at("exception")==""&&a.state.at("fastAccepted").get<bool>(),"noexcept move removes only final copy fault");
        check(b.state.at("globals")==a.state.at("globals")&&b.state.at("events")==a.state.at("events"),"prior side-effect order unchanged when final allocation is removed");
        caseEvidence.push_back({{"case","intentional-final-copy-exception-narrowing"},{"baseline",b.state},{"candidate",a.state}});
    }
    // Destination reuse after completed source destruction, with changing IDs.
    for(bool move:{false,true}) {
        InputHostFastReceipt dest;
        for(int n=0;n<100;++n) {
            const auto text=std::string(static_cast<std::size_t>(n%2?32:4096),static_cast<char>('A'+n%26));
            {InputHostFastReceipt source;source.used=n%2!=0;source.firstFrame=n%3==0;source.submitElapsedUs=n;source.hostInstanceId=text;
             if(move)candidateTransfer(source,&dest);else baselineTransfer(source,&dest);}
            check(dest.hostInstanceId==text&&dest.used==(n%2!=0)&&dest.firstFrame==(n%3==0)&&dest.submitElapsedUs==static_cast<unsigned long long>(n),"repeated destination reuse after local destruction");
        }
    }
    for(std::size_t length:{32U,4096U}) {
        std::optional<InputHostFastReceipt> dest;dest.emplace();spanCount=0;trackOwnership=true;
        {
            InputHostFastReceipt source;source.hostInstanceId=std::string(length,'D');
            watchAllocation(source.hostInstanceId.data());candidateTransfer(source,&*dest);
        }
        check(watchedFrees==0,"moved allocation survives source destruction");
        dest.reset();check(watchedFrees==1,"destination destructor releases moved allocation exactly once");
        watchedPointer=nullptr;trackOwnership=false;
    }
    captureEvents=false;diagnosticsEnabled=false;throwPoint.clear();failPoint.clear();failAfter=-1;watchedPointer=nullptr;
}
struct Measurement {double ns;Allocations allocations;unsigned long long checksum,operations;};
static Measurement measure(const Scenario& scenario,bool after,int scope,int batches,bool count) {
    Fast fast=after?candidateFast:baselineFast;Transfer transfer=after?candidateTransfer:baselineTransfer;
    Allocations total{};double ns=0;unsigned long long checksum=0;
    captureEvents=false;diagnosticsEnabled=false;watchSource=false;
    for(int batch=0;batch<batches;++batch) {
        const auto& x=scenario.inputs[static_cast<std::size_t>(batch)%scenario.inputs.size()];bind(x);
        g_inputHostTupleHealPending.store(false);
        const auto text=wire(x);
        std::vector<InputHostFastReceipt> destinations(64);
        std::vector<std::optional<InputHostFastReceipt>> prepared;
        if(scope==1) {
            prepared.resize(64);
            for(auto& slot:prepared) {slot.emplace();slot->used=true;slot->firstFrame=x.first;slot->submitElapsedUs=x.submit;slot->hostInstanceId=x.host;}
        }
        allocated={};counting=count;
        const auto start=std::chrono::steady_clock::now();
        for(std::size_t n=0;n<64;++n) {
            auto& receipt=destinations[n];bool ok=true;
            if(scope==0)ok=fast(text,"SUBMIT_FRAME","frame-accepted",x.request,x.sequence,&receipt,false,true,nullptr);
            else {transfer(*prepared[n],&receipt);prepared[n].reset();}
            checksum+=static_cast<unsigned long long>(ok)+receipt.used+receipt.firstFrame+receipt.submitElapsedUs+receipt.hostInstanceId.size();
            if(!receipt.hostInstanceId.empty())checksum+=static_cast<unsigned char>(receipt.hostInstanceId.front())+static_cast<unsigned char>(receipt.hostInstanceId.back());
        }
        const auto finish=std::chrono::steady_clock::now();counting=false;
        ns+=std::chrono::duration<double,std::nano>(finish-start).count();total.add(allocated);
    }
    sink=checksum;const auto operations=static_cast<unsigned long long>(64*batches);
    return {ns/operations,total,checksum,operations};
}
static double median(std::vector<double> x) {std::sort(x.begin(),x.end());return x[x.size()/2];}
static json allocationJson(const Measurement& m) {
    const auto& a=m.allocations;return {{"ordinaryRequests",a.ordinaryRequests},{"ordinaryRequestedBytes",a.ordinaryBytes},
        {"alignedRequests",a.alignedRequests},{"alignedRequestedBytes",a.alignedBytes},{"operations",m.operations},
        {"requestsPerOperation",(a.ordinaryRequests+a.alignedRequests)/static_cast<double>(m.operations)},
        {"requestedBytesPerOperation",(a.ordinaryBytes+a.alignedBytes)/static_cast<double>(m.operations)}};
}
static json formal(const Scenario& scenario,bool afterUnused,int scope,int campaign) {
    (void)afterUnused;measure(scenario,false,scope,6,false);measure(scenario,true,scope,6,false);
    std::vector<double> b,a,p;json pairs=json::array();int wins=0;bool agree=true;
    for(int round=0;round<11;++round) {
        Measurement before,after;const bool baselineFirst=((round+campaign)&1)==0;
        if(baselineFirst) {before=measure(scenario,false,scope,128,false);after=measure(scenario,true,scope,128,false);}
        else {after=measure(scenario,true,scope,128,false);before=measure(scenario,false,scope,128,false);}
        b.push_back(before.ns);a.push_back(after.ns);p.push_back((before.ns-after.ns)/before.ns);wins+=after.ns<before.ns;
        agree=agree&&before.checksum==after.checksum;
        pairs.push_back({{"round",round+1},{"baselineFirst",baselineFirst},{"beforeNsPerOperation",before.ns},{"afterNsPerOperation",after.ns},
            {"pairedReduction",p.back()},{"beforeChecksum",before.checksum},{"afterChecksum",after.checksum}});
    }
    const auto before=measure(scenario,false,scope,4,true),after=measure(scenario,true,scope,4,true);
    const auto& ba=before.allocations;const auto& aa=after.allocations;
    const bool noIncrease=aa.ordinaryRequests<=ba.ordinaryRequests&&aa.ordinaryBytes<=ba.ordinaryBytes&&aa.alignedRequests<=ba.alignedRequests&&aa.alignedBytes<=ba.alignedBytes;
    agree=agree&&before.checksum==after.checksum;
    const double paired=median(p),ratio=1.0-median(a)/median(b);
    const bool cpu=scenario.control?(paired>=-.05&&ratio>=-.05):(wins>=10&&paired>=.05&&ratio>=.05);
    return {{"scenario",scenario.name},{"control",scenario.control},{"scope",scope==0?"actual-full-scanner-and-fast-branch-transfer":"actual-transfer-and-local-disposal"},
        {"campaign",campaign},{"fasterRounds",wins},{"pairedRounds",11},{"medianPairedReduction",paired},{"ratioOfMediansReduction",ratio},
        {"beforeMedianNsPerOperation",median(b)},{"afterMedianNsPerOperation",median(a)},
        {"allocationsBefore",allocationJson(before)},{"allocationsAfter",allocationJson(after)},
        {"allocationsDoNotIncrease",noIncrease},{"checksumsAgree",agree},{"pairs",pairs},{"pass",cpu&&noIncrease&&agree}};
}
int main(int argc,char** argv) {
    try {
        if(argc<3)throw std::runtime_error("usage: fixture verify receipts.json OR fixture benchmark receipts.json campaign");
        std::ifstream input(argv[2],std::ios::binary);json rows;input>>rows;
        const std::string mode=argv[1];
        if(mode=="verify") {
            correctness(rows);json workloadInputs=json::array();
            for(const auto& scenario:scenarios(rows)) {json frames=json::array();for(const auto& x:scenario.inputs)frames.push_back({{"hostInstanceId",x.host},{"wire",wire(x)}});
                workloadInputs.push_back({{"scenario",scenario.name},{"control",scenario.control},{"frames",frames}});}
            std::cout<<json({{"pass",failures==0},{"assertions",assertions},{"failureCount",failures},{"candidateSourceDifferences",differentialFailures},
                {"domSafetyViolations",domFailures},{"fullBranchCases",caseCount},{"owningReceiptChecks",ownershipCases},{"transferLocalCases",localCases},
                {"exceptionCases",exceptionCases},{"archiveObservations",rows.size()},{"fullOutputDigestFNV1a64",std::to_string(outputDigest)},
                {"actualStructMoveAssignmentNoexcept",std::is_nothrow_move_assignable_v<InputHostFastReceipt>},
                {"failureDetails",failureDetails},{"caseEvidence",caseEvidence},{"declaredWorkloadInputs",workloadInputs},
                {"benchmarkExecuted",false},{"hardwareOrHostExecuted",false}}).dump(2)<<'\n';return failures?2:0;
        }
        if(mode!="benchmark"||argc!=4)throw std::runtime_error("invalid benchmark args");
        const int campaign=std::stoi(argv[3]);if(campaign!=1&&campaign!=2)throw std::runtime_error("invalid campaign");
        json results=json::array();bool pass=true;
        for(const auto& scenario:scenarios(rows))for(int scope:{0,1}) {auto row=formal(scenario,false,scope,campaign);pass=pass&&row.at("pass").get<bool>();results.push_back(std::move(row));}
        std::cout<<json({{"pass",pass},{"campaign",campaign},{"rows",results},{"wholeHardwareClaim",false}}).dump(2)<<'\n';return pass?0:2;
    } catch(const std::exception& error) {counting=false;failAfter=-1;std::cerr<<error.what()<<'\n';return 1;}
}
