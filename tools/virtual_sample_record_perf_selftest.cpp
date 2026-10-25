// Offline fixture for exact extracted diagnostic record initializer/owned transfers.
#define NOMINMAX
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <bit>
#include <fstream>
#include <iostream>
#include <limits>
#include <malloc.h>
#include <new>
#include <random>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>
#include "json.hpp"
using json = nlohmann::json;
static_assert(std::is_same<json::allocator_type, std::allocator<json>>::value);
struct Count { uint64_t calls=0,bytes=0; };
static thread_local bool track=false;
static thread_local Count allocations;
void* operator new(size_t n) { void* p=std::malloc(n?n:1); if(!p)throw std::bad_alloc(); if(track){++allocations.calls; allocations.bytes+=n;} return p; }
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
void operator delete[](void* p,size_t) noexcept {std::free(p);}
void* operator new(size_t n,std::align_val_t a) {void* p=_aligned_malloc(n?n:1,static_cast<size_t>(a));if(!p)throw std::bad_alloc();if(track){++allocations.calls;allocations.bytes+=n;}return p;}
void* operator new[](size_t n,std::align_val_t a) {return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t) noexcept {_aligned_free(p);}
void operator delete[](void* p,std::align_val_t) noexcept {_aligned_free(p);}
void operator delete(void* p,size_t,std::align_val_t) noexcept {_aligned_free(p);}
void operator delete[](void* p,size_t,std::align_val_t) noexcept {_aligned_free(p);}
static uint64_t assertions=0;
void check(bool x,const char* m){++assertions;if(!x)throw std::runtime_error(m);}
struct Fixture {
    struct Pad {uint16_t wButtons=0;uint8_t bLeftTrigger=0,bRightTrigger=0;int16_t sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0;}canonicalPad;
    struct Config {bool motionTriggerResolved=false,aimingSightsTriggerResolved=false,standaloneTestLane=false;}g_realStickTest;
    struct Selector {bool resolved=false;}g_hcMotionSelector;
    uint64_t now=0,g_inputCaptureBusTickGlobal=0;
    double motionDeltaSeconds=.008,ox=0,oy=0,oz=0,conf=0,psx=0,psy=0,wsx=0,wsy=0;
    bool live=false,hcMatrixAdmitted=false,steady=false,g_gm=false;
    uint32_t primarySlot=0,connectedMask=0;
    std::string inputSource,matrixIdentity;
    json imuObj,hcCandidateObj,eventObj,testLaneLiveObj;
    explicit Fixture(const json& sample):imuObj(sample.at("imu")),hcCandidateObj(sample.at("hcMatrixCandidate")),eventObj(sample.at("event")),testLaneLiveObj(sample.at("testLane")) {
        now=sample.at("t");g_inputCaptureBusTickGlobal=sample.at("busTick");motionDeltaSeconds=sample.at("dtMs").get<double>()/1000.;
        live=sample.at("connected");primarySlot=sample.at("slot");connectedMask=sample.at("slots");inputSource=sample.at("inputSource");matrixIdentity=sample.at("matrixIdentity");
        canonicalPad.wButtons=sample.at("buttons");canonicalPad.bLeftTrigger=sample.at("lt");canonicalPad.bRightTrigger=sample.at("rt");
        canonicalPad.sThumbLX=sample.at("lx");canonicalPad.sThumbLY=sample.at("ly");canonicalPad.sThumbRX=sample.at("rx");canonicalPad.sThumbRY=sample.at("ry");
        g_realStickTest.motionTriggerResolved=sample.at("motionTriggerResolved");g_realStickTest.aimingSightsTriggerResolved=sample.at("aimingSightsTriggerResolved");
        hcMatrixAdmitted=sample.at("matrixAdmission");g_realStickTest.standaloneTestLane=sample.at("matrixStage")=="active-test-lane";g_hcMotionSelector.resolved=sample.at("matrixStage")=="candidate-unbound-provider";
        ox=sample.at("offset").at("x");oy=sample.at("offset").at("y");oz=sample.at("offset").at("z");conf=sample.at("confidence");steady=sample.at("steady");g_gm=sample.at("gamepadMotion");
        psx=sample.at("playerSpace").at("x");psy=sample.at("playerSpace").at("y");wsx=sample.at("worldSpace").at("x");wsy=sample.at("worldSpace").at("y");
    }
#include "record-extracted.h"
};
using Builder=json(Fixture::*)();
static std::atomic<uint64_t> sink{0};
struct Measurement {double ns=0;Count count;};
Measurement measure(Builder fn,const Fixture& input,bool count,int batches) {
    constexpr int n=64;
    double elapsed=0;Count total;
    uint64_t sizes=0;
    for(int k=0;k<batches;++k) {
        std::vector<Fixture> prepared;prepared.reserve(n);for(int i=0;i<n;++i)prepared.push_back(input);
        allocations={};track=count;
        const auto start=std::chrono::steady_clock::now();
        for(auto& f:prepared){const auto out=(f.*fn)();sizes+=out.size();}
        elapsed+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();
        track=false;total.calls+=allocations.calls;total.bytes+=allocations.bytes;
    }
    sink.fetch_add(sizes,std::memory_order_relaxed);
    return {elapsed/(n*batches),total};
}
double median(std::vector<double> x){std::sort(x.begin(),x.end());return x[x.size()/2];}
json benchmark(const Fixture& f,const char* name) {
    measure(&Fixture::beforeRecord,f,false,4);measure(&Fixture::currentRecord,f,false,4);
    std::vector<double>a,b;
    for(int i=0;i<7;++i){if(i&1){b.push_back(measure(&Fixture::currentRecord,f,false,48).ns);a.push_back(measure(&Fixture::beforeRecord,f,false,48).ns);}else{a.push_back(measure(&Fixture::beforeRecord,f,false,48).ns);b.push_back(measure(&Fixture::currentRecord,f,false,48).ns);}}
    const auto ca=measure(&Fixture::beforeRecord,f,true,8),cb=measure(&Fixture::currentRecord,f,true,8);
    check(cb.count.calls<ca.count.calls,"no allocation improvement");check(cb.count.bytes<ca.count.bytes,"no allocated-byte improvement");
    return {{"name",name},{"beforeMedianNs",median(a)},{"afterMedianNs",median(b)},{"beforeSamplesNs",a},{"afterSamplesNs",b},
        {"beforeCallsPerRecord",ca.count.calls/512.},{"afterCallsPerRecord",cb.count.calls/512.},{"beforeBytesPerRecord",ca.count.bytes/512.},{"afterBytesPerRecord",cb.count.bytes/512.}};
}
bool sameRepresentation(const json& a,const json& b) {
    if(a.type()!=b.type())return false;
    if(a.is_number_float())return std::bit_cast<uint64_t>(a.get<double>())==std::bit_cast<uint64_t>(b.get<double>());
    if(a.is_object()||a.is_array()){
        if(a.size()!=b.size())return false;
        auto x=a.begin(),y=b.begin();
        for(;x!=a.end();++x,++y){if(a.is_object()&&x.key()!=y.key())return false;if(!sameRepresentation(x.value(),y.value()))return false;}return true;
    }
    return a==b;
}
void equivalent(Fixture a) {
    Fixture b=a;const auto original=a.imuObj;const auto originalEvent=a.eventObj;
    auto old=a.beforeRecord(),now=b.currentRecord();
    check(old.is_object()&&now.is_object(),"record object kind");check(old.dump()==now.dump(),"serialized bytes differ");
    check(old==now,"value mismatch");check(sameRepresentation(old,now),"exact numeric types/bits differ");check(old.size()==now.size(),"field count");check(a.imuObj==original&&a.eventObj==originalEvent,"original lvalues changed");
    check(b.imuObj.is_null()&&b.hcCandidateObj.is_null()&&b.eventObj.is_null()&&b.testLaneLiveObj.is_null(),"move did not transfer");
    // Original later inactive-IMU slimming still has the same record ownership.
    for(int inactive=0;inactive<2;++inactive){auto x=old,y=now;if(inactive){x["imu"]={{"imuActive",false}};y["imu"]={{"imuActive",false}};}check(x.dump()==y.dump(),"later IMU replacement mismatch");}
}
int main(int argc,char**argv) {
    try {
        if(argc!=2)throw std::runtime_error("sample required");std::ifstream file(argv[1]);json sample;file>>sample;
        const Fixture base(sample);equivalent(base);
        std::mt19937_64 rng(0x20261014);
        for(int i=0;i<2000;++i){Fixture f=base;f.now=rng();f.g_inputCaptureBusTickGlobal=rng();f.primarySlot=static_cast<uint32_t>(rng());f.connectedMask=static_cast<uint32_t>(rng());
            f.canonicalPad.wButtons=static_cast<uint16_t>(rng());f.canonicalPad.bLeftTrigger=static_cast<uint8_t>(rng());f.canonicalPad.bRightTrigger=static_cast<uint8_t>(rng());
            f.canonicalPad.sThumbLX=static_cast<int16_t>(rng());f.canonicalPad.sThumbLY=static_cast<int16_t>(rng());f.canonicalPad.sThumbRX=static_cast<int16_t>(rng());f.canonicalPad.sThumbRY=static_cast<int16_t>(rng());
            f.hcMatrixAdmitted=i&1;f.g_hcMotionSelector.resolved=i&2;f.g_realStickTest.standaloneTestLane=i&4;f.live=i&8;
            f.inputSource=(i%5==0)?"":((i%5==1)?"utf8-微星\n\"\\\t":std::string(i%96,'s'));
            f.imuObj["fixture-precise"]=(i%3==0)?-0.0:std::ldexp(static_cast<double>(static_cast<int32_t>(rng())),-30);
            f.eventObj["fixture-sequence"]=rng();f.testLaneLiveObj["fixture"]={{"nullable",nullptr},{"bool",(i&1)!=0},{"array",json::array({i,"utf8-陀螺仪"})}};
            if(i%37==0)f.imuObj={{"imuActive",false}};equivalent(std::move(f));}
        for(double value:{-0.,0.,std::numeric_limits<double>::denorm_min(),std::numeric_limits<double>::max(),std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
            Fixture f=base;f.motionDeltaSeconds=value;f.ox=value;f.conf=value;f.imuObj["fixture-float-boundary"]=value;
            // NaN JSON equality is false; compare exact types and serialized bytes instead.
            Fixture b=f;auto x=f.beforeRecord(),y=b.currentRecord();check(x.dump()==y.dump(),"floating boundary dump");check(sameRepresentation(x,y),"floating boundary bits");check(x.at("dtMs").type()==y.at("dtMs").type(),"floating type mismatch");}
        std::atomic<bool>ok{true};std::vector<std::thread>threads;
        for(int t=0;t<4;++t)threads.emplace_back([&](){for(int i=0;i<500;++i){Fixture a=base,b=base;if(a.beforeRecord().dump()!=b.currentRecord().dump())ok=false;}});
        for(auto&t:threads)t.join();check(ok,"concurrent fixture mismatch");
        Fixture large=base;for(int i=0;i<16;++i)large.imuObj["synthetic-"+std::to_string(i)]=base.imuObj;equivalent(large);
        json benches=json::array({benchmark(base,"real-G3E-record"),benchmark(large,"large-synthetic-IMU-record")});
        std::cout<<json({{"pass",true},{"assertions",assertions},{"randomCases",2000},{"concurrentCases",2000},{"benchmarks",benches},
            {"noRateOrPayloadReduction",true},{"hardwareOrProductExecuted",false},{"scope","Extracted production initializer/transfers; prepared nested tree construction excluded; record destruction included; allocation/time separate; scalar surroundings are value fixtures."}}).dump(2)<<'\n';return 0;
    }catch(const std::exception&e){track=false;std::cerr<<e.what()<<'\n';return 1;}
}
