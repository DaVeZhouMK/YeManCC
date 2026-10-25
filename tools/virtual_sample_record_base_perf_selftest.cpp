// R16: standalone source-extracted JSON builders; no product/device code executes.
#define NOMINMAX
#include <Windows.h>   // Production Windows scalar typedefs only; no OS calls.
#include <Xinput.h>    // Production XINPUT_GAMEPAD field types only.
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <malloc.h>
#include <new>
#include <random>
#include <string>
#include <type_traits>
#include <vector>
#include "json.hpp"
using json = nlohmann::json;
static_assert(std::is_same_v<json::allocator_type,std::allocator<json>>);
#define NOINLINE __declspec(noinline)
struct Count {
    uint64_t ordinaryRequests=0,ordinaryBytes=0,alignedRequests=0,alignedBytes=0;
    uint64_t requests() const {return ordinaryRequests+alignedRequests;}
    uint64_t bytes() const {return ordinaryBytes+alignedBytes;}
};
static thread_local bool counting=false;
static thread_local Count counter;
static void account(size_t n,bool aligned) {
    if(!counting)return;
    if(aligned){++counter.alignedRequests;counter.alignedBytes+=n;}
    else {++counter.ordinaryRequests;counter.ordinaryBytes+=n;}
}
void* operator new(size_t n) {auto p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();account(n,false);return p;}
void* operator new[](size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
void operator delete[](void* p,size_t) noexcept {std::free(p);}
void* operator new(size_t n,std::align_val_t a) {auto p=_aligned_malloc(n?n:1,size_t(a));if(!p)throw std::bad_alloc();account(n,true);return p;}
void* operator new[](size_t n,std::align_val_t a) {return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t) noexcept {_aligned_free(p);}
void operator delete[](void* p,std::align_val_t) noexcept {_aligned_free(p);}
void operator delete(void* p,size_t,std::align_val_t) noexcept {_aligned_free(p);}
void operator delete[](void* p,size_t,std::align_val_t) noexcept {_aligned_free(p);}
void* operator new(size_t n,const std::nothrow_t&) noexcept {try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](size_t n,const std::nothrow_t&) noexcept {try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&) noexcept {::operator delete(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept {::operator delete[](p);}
void* operator new(size_t n,std::align_val_t a,const std::nothrow_t&) noexcept {try{return ::operator new(n,a);}catch(...){return nullptr;}}
void* operator new[](size_t n,std::align_val_t a,const std::nothrow_t&) noexcept {try{return ::operator new[](n,a);}catch(...){return nullptr;}}
void operator delete(void* p,std::align_val_t a,const std::nothrow_t&) noexcept {::operator delete(p,a);}
void operator delete[](void* p,std::align_val_t a,const std::nothrow_t&) noexcept {::operator delete[](p,a);}
static uint64_t assertions=0,sink=0;
static void check(bool ok,const char* message) {++assertions;if(!ok)throw std::runtime_error(message);}
struct Input {
    ULONGLONG now=0;
    unsigned long long busTick=0;
    float motionDeltaSeconds=0;
    XINPUT_GAMEPAD canonicalPad{};
    float ox=0,oy=0,oz=0,conf=0,psx=0,psy=0,wsx=0,wsy=0;
    bool live=false,hcMatrixAdmitted=false,steady=false;
    DWORD primarySlot=0,connectedMask=0,writerPid=0;
    UINT64 gyroSequence=0,accelSequence=0;
    void* gm=nullptr;
    std::string inputSource,matrixIdentity;
    struct Config {bool motionTriggerResolved=false,aimingSightsTriggerResolved=false,standaloneTestLane=false;} config;
    struct Selector {bool resolved=false;} selector;
    json imu,hc,event,test,extras=json::object();
};
static_assert(std::is_same_v<decltype(Input::motionDeltaSeconds),float>);
static_assert(std::is_same_v<decltype(Input::now),ULONGLONG>);
static_assert(std::is_same_v<decltype(Input::canonicalPad.wButtons),WORD>);
#include "generated.hpp"
using Builder=json(*)(Input&);
static bool exact(const json& a,const json& b) {
    if(a.type()!=b.type()||a.size()!=b.size())return false;
    if(a.is_number_float())return std::bit_cast<uint64_t>(a.get<double>())==std::bit_cast<uint64_t>(b.get<double>());
    if(a.is_object()||a.is_array()) {
        auto x=a.begin(),y=b.begin();
        for(;x!=a.end();++x,++y)if((a.is_object()&&x.key()!=y.key())||!exact(x.value(),y.value()))return false;
        return true;
    }
    return a==b;
}
static json oracle(const Input& i) {
    // Independent expression and type oracle, without initializer-list parsing.
    json r=json::object();
    r["t"]=uint64_t(i.now);r["kind"]="sample";r["busTick"]=uint64_t(i.busTick);
    r["dtMs"]=double(i.motionDeltaSeconds)*1000.0;r["connected"]=bool(i.live);
    r["slot"]=int(i.primarySlot);r["slots"]=int(i.connectedMask);
    r["inputSource"]=i.inputSource.empty()?std::string("none"):i.inputSource;
    r["buttons"]=uint64_t(i.canonicalPad.wButtons);r["lt"]=uint64_t(i.canonicalPad.bLeftTrigger);r["rt"]=uint64_t(i.canonicalPad.bRightTrigger);
    r["lx"]=int64_t(i.canonicalPad.sThumbLX);r["ly"]=int64_t(i.canonicalPad.sThumbLY);
    r["rx"]=int64_t(i.canonicalPad.sThumbRX);r["ry"]=int64_t(i.canonicalPad.sThumbRY);
    r["motionTriggerResolved"]=bool(i.config.motionTriggerResolved);r["aimingSightsTriggerResolved"]=bool(i.config.aimingSightsTriggerResolved);
    r["matrixIdentity"]=i.matrixIdentity;
    const char* stage="raw-unbound-device";
    if(i.hcMatrixAdmitted)stage=i.config.standaloneTestLane?"active-test-lane":"active-hc-runtime";
    else if(i.selector.resolved)stage="candidate-unbound-provider";
    r["matrixStage"]=stage;r["matrixAdmission"]=bool(i.hcMatrixAdmitted);
    r["offset"]["x"]=double(i.ox);r["offset"]["y"]=double(i.oy);r["offset"]["z"]=double(i.oz);
    r["confidence"]=double(i.conf);r["steady"]=bool(i.steady);r["gamepadMotion"]=i.gm!=nullptr;
    r["playerSpace"]["x"]=double(i.psx);r["playerSpace"]["y"]=double(i.psy);
    r["worldSpace"]["x"]=double(i.wsx);r["worldSpace"]["y"]=double(i.wsy);
    r["imu"]=i.imu;r["hcMatrixCandidate"]=i.hc;r["event"]=i.event;r["testLane"]=i.test;
    for(auto it=i.extras.begin();it!=i.extras.end();++it)r[it.key()]=it.value();
    if(!i.gyroSequence&&!i.accelSequence) {r["imu"]=json::object();r["imu"]["imuActive"]=false;}
    return r;
}
static Input fromSample(const json& r) {
    Input v;v.now=r.at("t");v.busTick=r.at("busTick");v.motionDeltaSeconds=float(r.at("dtMs").get<double>()/1000.0);
    v.live=r.at("connected");v.primarySlot=r.at("slot");v.connectedMask=r.at("slots");v.inputSource=r.at("inputSource");
    v.canonicalPad.wButtons=r.at("buttons");v.canonicalPad.bLeftTrigger=r.at("lt");v.canonicalPad.bRightTrigger=r.at("rt");
    v.canonicalPad.sThumbLX=r.at("lx");v.canonicalPad.sThumbLY=r.at("ly");v.canonicalPad.sThumbRX=r.at("rx");v.canonicalPad.sThumbRY=r.at("ry");
    v.config.motionTriggerResolved=r.at("motionTriggerResolved");v.config.aimingSightsTriggerResolved=r.at("aimingSightsTriggerResolved");
    v.config.standaloneTestLane=r.at("matrixStage")=="active-test-lane";v.selector.resolved=r.at("matrixStage")=="candidate-unbound-provider";
    v.hcMatrixAdmitted=r.at("matrixAdmission");v.matrixIdentity=r.at("matrixIdentity");
    v.ox=r.at("offset").at("x");v.oy=r.at("offset").at("y");v.oz=r.at("offset").at("z");v.conf=r.at("confidence");v.steady=r.at("steady");
    v.psx=r.at("playerSpace").at("x");v.psy=r.at("playerSpace").at("y");v.wsx=r.at("worldSpace").at("x");v.wsy=r.at("worldSpace").at("y");
    v.gm=r.at("gamepadMotion").get<bool>()?reinterpret_cast<void*>(uintptr_t(1)):nullptr;
    v.gyroSequence=r.at("event").at("gyro").at("sequence");v.accelSequence=r.at("event").at("accel").at("sequence");
    v.imu=r.at("imu");v.hc=r.at("hcMatrixCandidate");v.event=r.at("event");v.test=r.at("testLane");v.writerPid=r.at("writerPid");
    for(const char* k:{"hcSelectorCandidate","testLaneConfig"})if(r.contains(k))v.extras[k]=r.at(k);
    return v;
}
static std::string dumpResult(const json& r) {
    try{return std::string("ok:")+r.dump();}catch(const json::exception& e){return std::string("error:")+std::to_string(e.id);}
}
static void equivalent(const Input& input) {
    const auto snapshot=oracle(input);auto a=input,b=input;
    auto baseline=baselineRecord(a),candidate=candidateRecord(b);
    check(exact(baseline,candidate),"record type/bits mismatch");check(exact(baseline,snapshot),"independent record oracle");
    check(dumpResult(baseline)==dumpResult(candidate),"dump bytes or exception mismatch");
    check(a.imu.is_null()&&a.hc.is_null()&&a.event.is_null()&&a.test.is_null(),"baseline four tree moves");
    check(b.imu.is_null()&&b.hc.is_null()&&b.event.is_null()&&b.test.is_null(),"candidate four tree moves");
    auto expected=snapshot;expected["writerPid"]=uint64_t(input.writerPid);
    if(dumpResult(expected).starts_with("ok:")) {
        const auto left=writerDump(baseline,input.writerPid),right=writerDump(candidate,input.writerPid);
        check(left==right,"actual writer copy dump parity");check(left==expected.dump(),"writerPid independent oracle");
    }
    check(exact(snapshot,oracle(input)),"caller input mutation");
}
struct Scenario{std::string name;Input input;};
static std::vector<Scenario> scenarios(const json& selected) {
    std::vector<Scenario> out;for(auto& v:selected)out.push_back({v.at("name"),fromSample(v.at("sample"))});
    auto empty=out.front().input;empty.inputSource.clear();empty.live=false;empty.hcMatrixAdmitted=false;out.push_back({"empty-source-synthetic",empty});
    auto active=out.front().input;active.hcMatrixAdmitted=true;active.config.standaloneTestLane=true;active.gm=reinterpret_cast<void*>(uintptr_t(1));out.push_back({"test-lane-synthetic",active});
    return out;
}
static json correctness(const std::vector<Scenario>& cases,const json& all) {
    for(auto& row:all)equivalent(fromSample(row));
    for(auto& c:cases)equivalent(c.input);
    auto input=cases.front().input;
    for(unsigned flags=0;flags<256;++flags){auto v=input;v.live=flags&1;v.hcMatrixAdmitted=flags&2;v.config.standaloneTestLane=flags&4;
        v.selector.resolved=flags&8;v.config.motionTriggerResolved=flags&16;v.config.aimingSightsTriggerResolved=flags&32;
        v.gyroSequence=(flags&64)?0:UINT64(-1);v.accelSequence=(flags&128)?0:UINT64(0x100000001);equivalent(v);}
    for(uint64_t n:{0ULL,1ULL,0xFFFFFFFFULL,0x100000001ULL,0x20000000000001ULL,0x7fffffffffffffffULL,0xffffffffffffffffULL}) {
        auto v=input;v.now=n;v.busTick=n;v.primarySlot=DWORD(n);v.connectedMask=DWORD(n);v.gyroSequence=n;v.accelSequence=n;equivalent(v);
    }
    const std::array<float,11> extremes={0.0f,-0.0f,std::numeric_limits<float>::max(),std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::denorm_min(),-std::numeric_limits<float>::denorm_min(),std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),std::bit_cast<float>(uint32_t(0x7fc00042)),std::bit_cast<float>(uint32_t(0xffc0007f)),.125f};
    for(auto n:extremes){auto v=input;v.motionDeltaSeconds=n;v.ox=v.oy=v.oz=v.conf=v.psx=v.psy=v.wsx=v.wsy=n;equivalent(v);}
    std::mt19937_64 rng(0x20261015a020ULL);
    for(int n=0;n<1600;++n) {
        auto v=cases[n%cases.size()].input;v.now=rng();v.busTick=rng();v.primarySlot=DWORD(rng());v.connectedMask=DWORD(rng());
        v.canonicalPad.wButtons=WORD(rng());v.canonicalPad.bLeftTrigger=BYTE(rng());v.canonicalPad.bRightTrigger=BYTE(rng());
        v.canonicalPad.sThumbLX=SHORT(rng());v.canonicalPad.sThumbLY=SHORT(rng());v.canonicalPad.sThumbRX=SHORT(rng());v.canonicalPad.sThumbRY=SHORT(rng());
        v.live=rng()&1;v.hcMatrixAdmitted=rng()&1;v.selector.resolved=rng()&1;v.config.standaloneTestLane=rng()&1;
        v.config.motionTriggerResolved=rng()&1;v.config.aimingSightsTriggerResolved=rng()&1;v.gm=(rng()&1)?reinterpret_cast<void*>(uintptr_t(1)):nullptr;
        v.motionDeltaSeconds=float(double(rng()%16000)/1000000.0);v.ox=float(int(rng()%10000)-5000)/37.f;
        v.oy=v.ox+.125f;v.oz=v.oy-.25f;v.conf=float(rng()%10000)/10000.f;v.psx=v.ox/2;v.psy=-v.oy/3;v.wsx=-v.oz/4;v.wsy=v.ox/7;
        v.inputSource=n%3?std::string(n%80,'s'):"";v.matrixIdentity=n%2?"utf8-\xe5\xbe\xae\xe6\x98\x9f\n\t\"\\":"";
        equivalent(v);
    }
    for(const auto& s:{std::string("\x80"),std::string("\xc3\x28"),std::string("\xf0\x80\x80\x80")}){auto v=input;v.inputSource=s;equivalent(v);}
    auto expected=oracle(input);auto wrong=expected;wrong["slot"]=uint64_t(input.primarySlot);check(!exact(wrong,expected),"signedness oracle probe");
    wrong=expected;wrong["offset"]["x"]=input.ox+1.f;check(!exact(wrong,expected),"nested-value oracle probe");
    wrong=expected;wrong.erase("testLane");check(!exact(wrong,expected),"whole-tree oracle probe");
    counter={};counting=true;auto a=::operator new(3);auto b=::operator new(128,std::align_val_t(64));::operator delete(a);::operator delete(b,std::align_val_t(64));counting=false;
    check(counter.ordinaryRequests==1&&counter.ordinaryBytes==3&&counter.alignedRequests==1&&counter.alignedBytes==128,"allocation counter probe");
    return {{"allAuthorizedSampleCount",all.size()},{"randomCases",1600},{"flagCases",256},{"floatBoundaries",11},
        {"wide64Cases",7},{"invalidUtf8Cases",3},{"independentTypedOracle",true},{"allFourTreesRetained",true},
        {"writerCopyDumpSourceExtracted",true},{"productionTypeAliases",true}};
}
enum class Scope{Assembly,RecordDump,WriterCopyDump};
static constexpr const char* scopeNames[]={"record-assembly","record-dump","writer-copy-dump"};
static NOINLINE uint64_t execute(Builder builder,Input& input,Scope scope) {
    auto record=builder(input);
    if(scope==Scope::WriterCopyDump){const auto wire=writerDump(record,input.writerPid);return wire.size()+record.size();}
    if(scope==Scope::RecordDump){const auto wire=record.dump();return wire.size()+record.size();}
    return record.size();
}
struct Measurement{double ns=0;Count count;};
static Measurement measure(Builder builder,const Input& input,Scope scope,int batches,bool allocations=false) {
    double elapsed=0;Count count;uint64_t value=0;
    for(int b=0;b<batches;++b) {
        std::vector<Input> prepared(32,input);counter={};counting=allocations;
        const auto start=std::chrono::steady_clock::now();
        for(auto& item:prepared)value+=execute(builder,item,scope);
        elapsed+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();counting=false;
        count.ordinaryRequests+=counter.ordinaryRequests;count.ordinaryBytes+=counter.ordinaryBytes;
        count.alignedRequests+=counter.alignedRequests;count.alignedBytes+=counter.alignedBytes;
    }
    sink+=value;return {elapsed/(32*batches),count};
}
static double median(std::vector<double> x){std::sort(x.begin(),x.end());return x[x.size()/2];}
static json benchmark(const std::vector<Scenario>& cases,int campaign) {
    struct Series{std::vector<double> before,after,paired;};
    std::vector<Series> series(cases.size()*3);
    // Alternate builder order and rotate row order, keeping preparations outside
    // the measured region equally. Complete trees and record destruction remain.
    for(int round=0;round<11;++round)for(size_t pos=0;pos<series.size();++pos) {
        const auto index=(pos+round*7+campaign*11)%series.size();const auto& c=cases[index/3];auto scope=Scope(index%3);
        const bool candidateFirst=(round+campaign+index)&1;
        measure(baselineRecord,c.input,scope,6);measure(candidateRecord,c.input,scope,6);
        double b=0,a=0;
        if(candidateFirst){a=measure(candidateRecord,c.input,scope,24).ns;b=measure(baselineRecord,c.input,scope,24).ns;}
        else{b=measure(baselineRecord,c.input,scope,24).ns;a=measure(candidateRecord,c.input,scope,24).ns;}
        series[index].before.push_back(b);series[index].after.push_back(a);series[index].paired.push_back((b-a)/b);
    }
    json rows=json::array();bool accepted=true;
    for(size_t idx=0;idx<series.size();++idx) {
        auto& s=series[idx];auto scope=Scope(idx%3);auto& c=cases[idx/3];
        int faster=0;for(size_t n=0;n<s.before.size();++n)faster+=s.after[n]<s.before[n];
        double before=median(s.before),after=median(s.after),paired=median(s.paired),ratio=(before-after)/before;
        auto b=measure(baselineRecord,c.input,scope,2,true).count,a=measure(candidateRecord,c.input,scope,2,true).count;
        bool alloc=a.requests()<b.requests()&&a.bytes()<b.bytes();bool pass=faster>=10&&paired>=.05&&ratio>=.05&&alloc;accepted=accepted&&pass;
        rows.push_back({{"scenario",c.name},{"scope",scopeNames[idx%3]},{"fasterRounds",faster},{"rounds",11},
            {"pairedMedianReduction",paired},{"ratioOfMediansReduction",ratio},{"beforeMedianNs",before},{"afterMedianNs",after},
            {"beforeAllocationRequests",b.requests()/64.0},{"candidateAllocationRequests",a.requests()/64.0},
            {"beforeRequestedBytes",b.bytes()/64.0},{"candidateRequestedBytes",a.bytes()/64.0},
            {"allocationsLower",alloc},{"pass",pass},{"beforeSamplesNs",s.before},{"afterSamplesNs",s.after},{"pairedReductions",s.paired}});
    }
    return {{"candidateAccepted",accepted},{"campaign",campaign},{"rows",rows}};
}
static json readJson(const char* p){std::ifstream f(p);json v;f>>v;return v;}
int main(int argc,char** argv) {
    try {
        check(argc>=4,"usage: verify|benchmark samples.json all-samples.json [campaign]");
        auto selected=readJson(argv[2]),all=readJson(argv[3]);auto cases=scenarios(selected);
        auto correctnessResult=correctness(cases,all);
        const std::string mode=argv[1];json result;
        if(mode=="verify")result={{"pass",true},{"assertions",assertions},{"coverage",correctnessResult},{"formalTimingExecuted",false}};
        else {check(mode=="benchmark"&&argc==5,"invalid mode");result=benchmark(cases,std::stoi(argv[4]));result["correctnessAssertions"]=assertions;}
        result["hardwareOrProductExecuted"]=false;result["wholeProcessClaim"]=false;
        result["scopeNote"]="All four complete diagnostic trees move into the record; extras, inactive override and actual writer copy+dump retained. Tree preparation and file I/O excluded. Destruction included; requested new bytes are not live memory/RSS.";
        std::cout<<result.dump(2)<<'\n';return 0;
    }catch(const std::exception& e){counting=false;std::cerr<<e.what()<<'\n';return 1;}
}