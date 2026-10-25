// R17: standalone source-extracted JSON builders; no product/device code executes.
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
static thread_local int64_t faultAt=-1;
static thread_local uint64_t faultRequests=0;
static uint64_t liveBlocks=0;
static void allocationAttempt() {auto i=faultRequests++;if(faultAt>=0&&i==uint64_t(faultAt))throw std::bad_alloc();}
static void account(size_t n,bool aligned) {
    if(!counting)return;
    if(aligned){++counter.alignedRequests;counter.alignedBytes+=n;}
    else {++counter.ordinaryRequests;counter.ordinaryBytes+=n;}
}
void* operator new(size_t n) {allocationAttempt();auto p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();++liveBlocks;account(n,false);return p;}
void* operator new[](size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {if(p)--liveBlocks;std::free(p);}
void operator delete[](void* p) noexcept {if(p)--liveBlocks;std::free(p);}
void operator delete(void* p,size_t) noexcept {if(p)--liveBlocks;std::free(p);}
void operator delete[](void* p,size_t) noexcept {if(p)--liveBlocks;std::free(p);}
void* operator new(size_t n,std::align_val_t a) {allocationAttempt();auto p=_aligned_malloc(n?n:1,size_t(a));if(!p)throw std::bad_alloc();++liveBlocks;account(n,true);return p;}
void* operator new[](size_t n,std::align_val_t a) {return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t) noexcept {if(p)--liveBlocks;_aligned_free(p);}
void operator delete[](void* p,std::align_val_t) noexcept {if(p)--liveBlocks;_aligned_free(p);}
void operator delete(void* p,size_t,std::align_val_t) noexcept {if(p)--liveBlocks;_aligned_free(p);}
void operator delete[](void* p,size_t,std::align_val_t) noexcept {if(p)--liveBlocks;_aligned_free(p);}
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
// Independent recursive equality: JSON equality alone allows signed/unsigned
// numeric equality and does not test negative-zero/NaN payload bits.
static bool exact(const json& a,const json& b) {
    if(a.type()!=b.type()||a.size()!=b.size())return false;
    switch(a.type()) {
    case json::value_t::object:
        {auto x=a.begin(),y=b.begin();for(;x!=a.end();++x,++y)if(x.key()!=y.key()||!exact(x.value(),y.value()))return false;}return true;
    case json::value_t::array:
        for(size_t i=0;i<a.size();++i)if(!exact(a[i],b[i]))return false;return true;
    case json::value_t::number_float:
        return std::bit_cast<uint64_t>(a.get_ref<const json::number_float_t&>())==std::bit_cast<uint64_t>(b.get_ref<const json::number_float_t&>());
    case json::value_t::number_unsigned:return a.get_ref<const json::number_unsigned_t&>()==b.get_ref<const json::number_unsigned_t&>();
    case json::value_t::number_integer:return a.get_ref<const json::number_integer_t&>()==b.get_ref<const json::number_integer_t&>();
    default:return a==b;
    }
}
static void exactAndDump(const json& a,const json& b,const char* label) {
    check(exact(a,b),label);check(a.dump()==b.dump(),"dump bytes differ");
}
// Trace/fault seams are correctness-only; timed receipt is exact source text.
enum class TraceEvent {Gx,Gy,Gz,Ax,Ay,Az,Resolved,ProviderFalse,ReceiptEnter,Capability,LegacyDigest,ReceiptReturn,ReceiptThrow};
struct ReceiptSentinel {int code;};
struct TraceState {
    std::array<TraceEvent,32> events{};size_t used=0;
    unsigned receiptCalls=0,digestCalls=0;int failStage=0;int64_t receiptFaultAt=-1;
    uint64_t receiptRequests=0;const void* receiptOwner=nullptr;
};
static TraceState trace;
static void traceEvent(TraceEvent e) {if(trace.used>=trace.events.size())throw std::runtime_error("trace overflow");trace.events[trace.used++]=e;}
template<typename T> static T tracedLeaf(TraceEvent e,T value) {traceEvent(e);return value;}
struct RecordInput {
    ULONGLONG now=0;unsigned long long g_inputCaptureBusTickGlobal=0;
    UINT64 gyroSequence=0,accelSequence=0;
    float motionDeltaSeconds=0;XINPUT_GAMEPAD canonicalPad{};
    float ox=0,oy=0,oz=0,conf=0,psx=0,psy=0,wsx=0,wsy=0;
    bool steady=false,live=false,hcMatrixAdmitted=false;
    DWORD primarySlot=0,connectedMask=0,writerPid=0;
    std::string inputSource,matrixIdentity;void* g_gm=nullptr;
    struct Config {bool motionTriggerResolved=false,aimingSightsTriggerResolved=false,standaloneTestLane=false;} g_realStickTest;
};
#include "extracted.hpp"
static_assert(std::is_same_v<decltype(HcInput::hcCandidateGx),double>);
static_assert(std::is_same_v<decltype(HcMotionSelectorReceipt::resolved),bool>);
static_assert(std::is_same_v<decltype(RecordInput::motionDeltaSeconds),float>);
static_assert(std::is_same_v<decltype(RecordInput::gyroSequence),UINT64>);
using Builder=json(*)(const HcInput&);
struct Prepared {HcInput values;RecordInput context;json imu,event,test,extras;};

// Independent lookup/branch oracle; no call to either receipt implementation.
static json receiptOracle(const HcInput& v) {
    const ymcc::msi_sensor::MotionCapability* cap=nullptr;
    for(const auto& row:ymcc::msi_sensor::kMotionCapabilities) {
        if(row.deviceClass!=v.machine.deviceClass)continue;
        for(auto token:row.boards)if(token.size()&&v.machine.product.find(token)!=std::string::npos){cap=&row;break;}
    }
    json r=json::object();
    for(bool gyro:{true,false}) {
        const auto& family=gyro?v.sensorMeta.gyroFamily:v.sensorMeta.accelFamily;
        bool legacy=cap&&cap->clawHidSensorApiLayout&&family=="sensorapi"&&(gyro?v.sensorMeta.gyroHcLegacyFields:v.sensorMeta.accelHcLegacyFields);
        json c=json::object();
        c["id"]=legacy?"hc-a250b19a:"+v.machine.deviceClass+(gyro?":gyro:x-z-neg-y":":accel:x-z-neg-y"):
            (gyro?v.machine.gyroMatrixId:v.machine.accelMatrixId);
        if(legacy) {
            if(cap->deviceClass=="HandheldCompanion.Devices.ClawCG3EM")c["sourceSha256"]="a1f4b96eb92c61c0405593a0abe93d2c007b5507867377f7f5f7002d8d663e90";
            else if(cap->deviceClass=="HandheldCompanion.Devices.ClawA2VM")c["sourceSha256"]="b9894fdb7bcbbc2e018e9c70bd54b9fda7a18fe7793ef4b2782308c606d1a7a3";
            else c["sourceSha256"]="";
        }else c["sourceSha256"]=v.selector.matrixSourceSha256;
        c["sourceScope"]=legacy?"hc-1.3.3.1-resource-json":(cap?"ordered-frozen-subclass-and-base-source-bundle":"frozen-hc-device-source");
        c["provider"]=family;c["legacyFieldOverride"]=bool(legacy);
        c["validationScope"]="declared-source-recipe;physical-axis-not-claimed";r[gyro?"gyro":"accel"]=std::move(c);
    }
    r["admissionIndependent"]=true;return r;
}
static json hcOracle(const HcInput& v) {
    json r=json::object();r["gyro"]=json::object();r["accel"]=json::object();
    r["gyro"]["gx"]=double(v.hcCandidateGx);r["gyro"]["gy"]=double(v.hcCandidateGy);r["gyro"]["gz"]=double(v.hcCandidateGz);
    r["accel"]["x"]=double(v.hcCandidateAx);r["accel"]["y"]=double(v.hcCandidateAy);r["accel"]["z"]=double(v.hcCandidateAz);
    r["resolved"]=bool(v.selector.resolved);r["providerBindingProven"]=false;r["recipeReceipt"]=receiptOracle(v);return r;
}
static json recordOracle(const Prepared& p,json hc) {
    const auto& c=p.context;json r=json::object();
    r["t"]=uint64_t(c.now);r["kind"]="sample";r["busTick"]=uint64_t(c.g_inputCaptureBusTickGlobal);
    r["dtMs"]=double(c.motionDeltaSeconds)*1000.0;r["connected"]=bool(c.live);r["slot"]=int(c.primarySlot);r["slots"]=int(c.connectedMask);
    r["inputSource"]=c.inputSource.empty()?std::string("none"):c.inputSource;
    r["buttons"]=uint64_t(c.canonicalPad.wButtons);r["lt"]=uint64_t(c.canonicalPad.bLeftTrigger);r["rt"]=uint64_t(c.canonicalPad.bRightTrigger);
    r["lx"]=int64_t(c.canonicalPad.sThumbLX);r["ly"]=int64_t(c.canonicalPad.sThumbLY);r["rx"]=int64_t(c.canonicalPad.sThumbRX);r["ry"]=int64_t(c.canonicalPad.sThumbRY);
    r["motionTriggerResolved"]=bool(c.g_realStickTest.motionTriggerResolved);r["aimingSightsTriggerResolved"]=bool(c.g_realStickTest.aimingSightsTriggerResolved);
    r["matrixIdentity"]=c.matrixIdentity;
    r["matrixStage"]=c.hcMatrixAdmitted?(c.g_realStickTest.standaloneTestLane?"active-test-lane":"active-hc-runtime"):
        (p.values.selector.resolved?"candidate-unbound-provider":"raw-unbound-device");
    r["matrixAdmission"]=bool(c.hcMatrixAdmitted);
    r["offset"]["x"]=double(c.ox);r["offset"]["y"]=double(c.oy);r["offset"]["z"]=double(c.oz);
    r["confidence"]=double(c.conf);r["steady"]=bool(c.steady);r["gamepadMotion"]=bool(c.g_gm);
    r["playerSpace"]["x"]=double(c.psx);r["playerSpace"]["y"]=double(c.psy);r["worldSpace"]["x"]=double(c.wsx);r["worldSpace"]["y"]=double(c.wsy);
    r["imu"]=p.imu;r["event"]=p.event;r["testLane"]=p.test;r["hcMatrixCandidate"]=std::move(hc);
    for(auto it=p.extras.begin();it!=p.extras.end();++it)r[it.key()]=it.value();
    if(!c.gyroSequence&&!c.accelSequence){r["imu"]=json::object();r["imu"]["imuActive"]=false;}
    return r;
}
static Prepared fromSample(const json& r) {
    Prepared p;auto& v=p.values;auto& c=p.context;const auto& hc=r.at("hcMatrixCandidate"),receipt=hc.at("recipeReceipt");
    v.hcCandidateGx=hc.at("gyro").at("gx");v.hcCandidateGy=hc.at("gyro").at("gy");v.hcCandidateGz=hc.at("gyro").at("gz");
    v.hcCandidateAx=hc.at("accel").at("x");v.hcCandidateAy=hc.at("accel").at("y");v.hcCandidateAz=hc.at("accel").at("z");v.selector.resolved=hc.at("resolved");
    // Board/manufacturer are explicit permitted source binding fixture values,
    // not recovered DMI. Base IDs are unused on the archived legacy path.
    v.machine.deviceClass="HandheldCompanion.Devices.ClawCG3EM";v.machine.product="MS-1T91";
    v.machine.manufacturer="MICRO-STAR";v.machine.family=YmccFamily::MsiClaw;v.machine.resolved=true;
    v.machine.gyroMatrixId="hc-06c0b954:msi-claw-sub:gyro:x-y-neg-z";v.machine.accelMatrixId="hc-06c0b954:msi-claw-a1m:accel:neg-x-neg-z-y";
    v.selector.matrixSourceSha256=std::string(ymcc::msi_sensor::kMotionCapabilities.at(2).frozenMatrixSourcesSha256);
    v.sensorMeta.gyroFamily=receipt.at("gyro").at("provider");v.sensorMeta.accelFamily=receipt.at("accel").at("provider");
    v.sensorMeta.gyroHcLegacyFields=receipt.at("gyro").at("legacyFieldOverride");v.sensorMeta.accelHcLegacyFields=receipt.at("accel").at("legacyFieldOverride");
    c.now=r.at("t");c.g_inputCaptureBusTickGlobal=r.at("busTick");c.motionDeltaSeconds=float(r.at("dtMs").get<double>()/1000.0);
    c.live=r.at("connected");c.primarySlot=r.at("slot");c.connectedMask=r.at("slots");c.writerPid=r.at("writerPid");
    c.inputSource=r.at("inputSource");c.matrixIdentity=r.at("matrixIdentity");c.hcMatrixAdmitted=r.at("matrixAdmission");
    c.g_realStickTest.motionTriggerResolved=r.at("motionTriggerResolved");c.g_realStickTest.aimingSightsTriggerResolved=r.at("aimingSightsTriggerResolved");c.g_realStickTest.standaloneTestLane=r.at("matrixStage")=="active-test-lane";
    c.canonicalPad.wButtons=r.at("buttons");c.canonicalPad.bLeftTrigger=r.at("lt");c.canonicalPad.bRightTrigger=r.at("rt");
    c.canonicalPad.sThumbLX=r.at("lx");c.canonicalPad.sThumbLY=r.at("ly");c.canonicalPad.sThumbRX=r.at("rx");c.canonicalPad.sThumbRY=r.at("ry");
    c.ox=r.at("offset").at("x");c.oy=r.at("offset").at("y");c.oz=r.at("offset").at("z");c.conf=r.at("confidence");c.steady=r.at("steady");
    c.psx=r.at("playerSpace").at("x");c.psy=r.at("playerSpace").at("y");c.wsx=r.at("worldSpace").at("x");c.wsy=r.at("worldSpace").at("y");
    c.g_gm=r.at("gamepadMotion").get<bool>()?reinterpret_cast<void*>(uintptr_t(1)):nullptr;
    c.gyroSequence=r.at("event").at("gyro").at("sequence");c.accelSequence=r.at("event").at("accel").at("sequence");
    p.imu=r.at("imu");p.event=r.at("event");p.test=r.at("testLane");p.extras=json::object();
    for(const char* k:{"hcSelectorCandidate","testLaneConfig"})if(r.contains(k))p.extras[k]=r.at(k);
    return p;
}
static auto scalarFingerprint(const HcInput& v) {
    return std::array<uint64_t,6>{std::bit_cast<uint64_t>(v.hcCandidateGx),std::bit_cast<uint64_t>(v.hcCandidateGy),std::bit_cast<uint64_t>(v.hcCandidateGz),
        std::bit_cast<uint64_t>(v.hcCandidateAx),std::bit_cast<uint64_t>(v.hcCandidateAy),std::bit_cast<uint64_t>(v.hcCandidateAz)};
}
static json contextFingerprint(const Prepared& p) {
    json r=recordOracle(p,hcOracle(p.values));const auto& v=p.values;
    r["fixtureBinding"]={{"deviceClass",v.machine.deviceClass},{"product",v.machine.product},{"gyroMatrix",v.machine.gyroMatrixId},
        {"accelMatrix",v.machine.accelMatrixId},{"digest",v.selector.matrixSourceSha256},{"gyroProvider",v.sensorMeta.gyroFamily},
        {"accelProvider",v.sensorMeta.accelFamily},{"gyroLegacy",v.sensorMeta.gyroHcLegacyFields},{"accelLegacy",v.sensorMeta.accelHcLegacyFields},
        {"gyroSequence",p.context.gyroSequence},{"accelSequence",p.context.accelSequence},{"writerPid",p.context.writerPid}};
    return r;
}
static json buildRecord(Builder builder,Prepared& p,bool checked=false) {
    auto hc=builder(p.values);
    return checked?observedRecordChecked(p.values,p.context,std::move(hc),std::move(p.imu),std::move(p.event),std::move(p.test),std::move(p.extras)):
        observedRecord(p.values,p.context,std::move(hc),std::move(p.imu),std::move(p.event),std::move(p.test),std::move(p.extras));
}

static void equivalent(const Prepared& p) {
    auto before=scalarFingerprint(p.values);auto contextBefore=contextFingerprint(p);bindReceipt(p.values);
    auto b=baselineHc(p.values),c=candidateHc(p.values),o=hcOracle(p.values);
    exactAndDump(b,c,"HC baseline/candidate recursive types/floating bits");exactAndDump(b,o,"HC independent receipt and field oracle");
    check(b.is_object()&&b.size()==5&&b.at("gyro").size()==3&&b.at("accel").size()==3,"HC schema");
    check(b.at("resolved").is_boolean()&&b.at("providerBindingProven").is_boolean()&&!b.at("providerBindingProven").get<bool>(),"HC bool types/constants");
    auto bp=p,cp=p;auto br=buildRecord(baselineHc,bp,true),cr=buildRecord(candidateHc,cp,true),expected=recordOracle(p,hcOracle(p.values));
    exactAndDump(br,cr,"current owned record parity");exactAndDump(br,expected,"independent current record oracle");
    check(bp.imu.is_null()&&bp.event.is_null()&&bp.test.is_null()&&cp.imu.is_null()&&cp.event.is_null()&&cp.test.is_null(),"existing record moves consumed inputs");
    if(!p.context.gyroSequence&&!p.context.accelSequence)check(br.at("imu").size()==1&&br.at("imu").at("imuActive")==false,"actual inactive override retained");
    auto bw=writerDump(br,p.context.writerPid),cw=writerDump(cr,p.context.writerPid);
    check(bw==cw,"actual writer copy/pid/dump bytes");expected["writerPid"]=uint64_t(p.context.writerPid);check(bw==expected.dump(),"independent writer oracle");
    check(before==scalarFingerprint(p.values)&&before==scalarFingerprint(bp.values)&&before==scalarFingerprint(cp.values),"six declared double inputs unchanged");
    exactAndDump(contextBefore,contextFingerprint(p),"binding,prepared trees and context unchanged");
}
static bool sameTrace(const TraceState& a,const TraceState& b) {
    return a.used==b.used&&std::equal(a.events.begin(),a.events.begin()+a.used,b.events.begin())&&a.receiptCalls==b.receiptCalls&&a.digestCalls==b.digestCalls&&a.receiptRequests==b.receiptRequests;
}
static void tracePrefix(const TraceState& t) {
    check(t.used>=9&&t.receiptCalls==1,"receipt exactly once after scalar expressions");
    for(unsigned i=0;i<9;++i)check(t.events[i]==TraceEvent(i),"original left-to-right scalar/receipt evaluation order");
}
static uint64_t traceAndFaultChecks(const Prepared& p) {
    bindReceipt(p.values);trace={};auto b=tracedBaselineHc(p.values);auto bt=trace;
    check(bt.receiptOwner==&b.at("recipeReceipt").get_ref<const json::object_t&>(),"original receipt rvalue already moves ownership");
    trace={};auto c=tracedCandidateHc(p.values);auto ct=trace;
    check(ct.receiptOwner==&c.at("recipeReceipt").get_ref<const json::object_t&>(),"candidate receipt owner retained without copy");
    tracePrefix(bt);tracePrefix(ct);check(sameTrace(bt,ct),"real receipt capability/digest call trace identical");
    exactAndDump(b,c,"traced receipt result parity");exactAndDump(b,baselineHc(p.values),"trace copy agrees with unmodified receipt");
    std::vector<unsigned> stages={1};
    bool sawCapability=false;for(size_t i=0;i<bt.used;++i)sawCapability|=bt.events[i]==TraceEvent::Capability;
    if(sawCapability) stages.push_back(2);
    if(bt.digestCalls>=1) stages.push_back(3);
    if(bt.digestCalls>=2) stages.push_back(4);
    for(unsigned stage:stages) {
        std::array<TraceState,2> seen{};
        for(unsigned side=0;side<2;++side) {
            trace={};trace.failStage=int(stage);uint64_t liveBefore=liveBlocks;bool thrown=false;
            try{auto unused=(side?tracedCandidateHc:tracedBaselineHc)(p.values);}
            catch(const ReceiptSentinel& e){check(e.code==int(stage),"exact receipt exception identity propagates");thrown=true;}
            check(thrown,"receipt exception propagates from exact call site");
            check(liveBlocks==liveBefore,"receipt exception unwinds all temporary owners");
            check(trace.used>0&&trace.events[trace.used-1]==TraceEvent::ReceiptThrow,"receipt exception trace closes at throw");
            seen[side]=trace;tracePrefix(trace);
        }
        check(sameTrace(seen[0],seen[1]),"exception call count/order parity");
    }
    trace={};return bt.receiptRequests;
}

struct Scenario {std::string name;Prepared p;};
static std::vector<Scenario> scenarios(const json& samples) {
    std::vector<Scenario> r;for(const auto& s:samples)r.push_back({s.at("name"),fromSample(s.at("sample"))});
    r.reserve(10);
    auto add=[&](const char* n){r.push_back({n,r.front().p});return &r.back().p;};
    auto p=add("base-winrt-both");p->values.sensorMeta.gyroFamily=p->values.sensorMeta.accelFamily="winrt";p->values.sensorMeta.gyroHcLegacyFields=p->values.sensorMeta.accelHcLegacyFields=false;
    p=add("mixed-legacy-gyro-base-accel");p->values.sensorMeta.accelFamily="winrt";p->values.sensorMeta.accelHcLegacyFields=false;
    p=add("mixed-base-gyro-legacy-accel");p->values.sensorMeta.gyroFamily="winrt";p->values.sensorMeta.gyroHcLegacyFields=false;
    p=add("empty-unresolved-no-capability");p->values.machine={};p->values.selector={};p->values.sensorMeta={};p->context.hcMatrixAdmitted=false;
    p=add("long-utf8-no-capability");p->values.machine.deviceClass="fixture-unknown";p->values.machine.product="fixture-unmatched";
    p->values.machine.gyroMatrixId=std::string(160,'g')+"陀螺仪";p->values.machine.accelMatrixId=std::string(160,'a')+"加速度";p->values.selector.matrixSourceSha256=std::string(64,'f');p->values.sensorMeta.gyroFamily=std::string(80,'p')+"传感器";p->values.sensorMeta.accelFamily=std::string(80,'q')+"\"\\\n";
    p=add("known-nonlegacy-sensorapi");const auto& cap=ymcc::msi_sensor::kMotionCapabilities.front();p->values.machine.deviceClass=std::string(cap.deviceClass);p->values.machine.product=std::string(cap.boards[0]);p->values.selector.matrixSourceSha256=std::string(cap.frozenMatrixSourcesSha256);
    check(r.size()==10,"ten predeclared scenarios");return r;
}
static json counterJson(const Count& c,size_t n=1) {
    return {{"requestsPerOperation",double(c.requests())/n},{"requestedBytesPerOperation",double(c.bytes())/n},
        {"ordinaryRequestsPerOperation",double(c.ordinaryRequests)/n},{"ordinaryBytesPerOperation",double(c.ordinaryBytes)/n},
        {"alignedRequestsPerOperation",double(c.alignedRequests)/n},{"alignedBytesPerOperation",double(c.alignedBytes)/n}};
}
static void validateCounter() {
    uint64_t liveBefore=liveBlocks;counter={};counting=true;
    auto a=::operator new(3);auto b=::operator new[](17);auto c=::operator new(128,std::align_val_t(64));
    auto d=::operator new(5,std::nothrow);auto e=::operator new[](192,std::align_val_t(64),std::nothrow);
    ::operator delete(a);::operator delete[](b);::operator delete(c,std::align_val_t(64));::operator delete(d,std::nothrow);::operator delete[](e,std::align_val_t(64),std::nothrow);
    counting=false;check(counter.ordinaryRequests==3&&counter.ordinaryBytes==25&&counter.alignedRequests==2&&counter.alignedBytes==320,"ordinary/aligned counter calibration");
    check(liveBefore==liveBlocks,"ordinary/aligned live allocation calibration");
}
static void fillDouble(HcInput& v,double x) {v.hcCandidateGx=v.hcCandidateGy=v.hcCandidateGz=v.hcCandidateAx=v.hcCandidateAy=v.hcCandidateAz=x;}
static json correctness(const std::vector<Scenario>& cases,const json& all) {
    validateCounter();for(const auto& c:cases)equivalent(c.p);
    for(const auto& r:all) {auto p=fromSample(r);equivalent(p);bindReceipt(p.values);exactAndDump(baselineHc(p.values),r.at("hcMatrixCandidate"),"archived HC receipt matches current source");}
    uint64_t faultPoints=0;for(const auto& c:cases){faultPoints+=traceAndFaultChecks(c.p);}
    const std::array<double,14> xs={0.0,-0.0,std::numeric_limits<double>::max(),std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::min(),std::numeric_limits<double>::denorm_min(),-std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),
        std::bit_cast<double>(uint64_t(0x7ff8000000000042ULL)),std::bit_cast<double>(uint64_t(0xfff800000000007fULL)),86399999.99999999,.125,-23.75};
    for(double x:xs){auto p=cases.front().p;fillDouble(p.values,x);equivalent(p);}
    const std::array<float,13> fs={0.0f,-0.0f,std::numeric_limits<float>::max(),std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::min(),std::numeric_limits<float>::denorm_min(),-std::numeric_limits<float>::denorm_min(),
        std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),
        std::bit_cast<float>(uint32_t(0x7fc00042)),std::bit_cast<float>(uint32_t(0xffc0007f)),.125f,-23.75f};
    for(float x:fs){auto p=cases.front().p;fillDouble(p.values,double(x));p.context.ox=p.context.oy=p.context.oz=p.context.conf=p.context.psx=p.context.psy=p.context.wsx=p.context.wsy=x;p.context.motionDeltaSeconds=x;equivalent(p);}
    const std::array<uint64_t,10> wide={0,1,0xffffffffULL,0x100000001ULL,0x1fffffffffffffULL,0x20000000000001ULL,0x7fffffffffffffffULL,0x8000000000000000ULL,0xfffffffffffffffeULL,0xffffffffffffffffULL};
    for(auto x:wide){auto p=cases.front().p;p.context.now=x;p.context.g_inputCaptureBusTickGlobal=x;p.context.gyroSequence=x;p.context.accelSequence=x;equivalent(p);}
    for(unsigned mask=0;mask<64;++mask){auto p=cases.front().p;p.context.gyroSequence=mask&1?0:1;p.context.accelSequence=mask&2?0:UINT64(-1);p.values.selector.resolved=bool(mask&4);p.context.hcMatrixAdmitted=bool(mask&8);p.context.g_realStickTest.standaloneTestLane=bool(mask&16);p.context.live=bool(mask&32);equivalent(p);}
    unsigned capabilityCases=0;
    for(const auto& row:ymcc::msi_sensor::kMotionCapabilities)for(unsigned mask=0;mask<16;++mask){
        auto p=cases.front().p;p.values.machine.deviceClass=std::string(row.deviceClass);p.values.machine.product=std::string(row.boards[0]);p.values.selector.matrixSourceSha256=std::string(row.frozenMatrixSourcesSha256);
        p.values.sensorMeta.gyroFamily=mask&1?"sensorapi":"winrt";p.values.sensorMeta.accelFamily=mask&2?"sensorapi":"sdk";p.values.sensorMeta.gyroHcLegacyFields=bool(mask&4);p.values.sensorMeta.accelHcLegacyFields=bool(mask&8);equivalent(p);++capabilityCases;
    }
    std::mt19937_64 rng(0x20261015a017ULL);
    for(unsigned i=0;i<1200;++i){auto p=cases[i%cases.size()].p;p.values.hcCandidateGx=std::bit_cast<double>(rng());p.values.hcCandidateGy=std::bit_cast<double>(rng());p.values.hcCandidateGz=std::bit_cast<double>(rng());p.values.hcCandidateAx=std::bit_cast<double>(rng());p.values.hcCandidateAy=std::bit_cast<double>(rng());p.values.hcCandidateAz=std::bit_cast<double>(rng());p.context.motionDeltaSeconds=std::bit_cast<float>(uint32_t(rng()));p.context.ox=std::bit_cast<float>(uint32_t(rng()));p.context.now=rng();p.context.g_inputCaptureBusTickGlobal=rng();p.context.gyroSequence=i%17?rng():0;p.context.accelSequence=i%17?rng():0;p.context.canonicalPad.wButtons=WORD(rng());p.context.canonicalPad.bLeftTrigger=BYTE(rng());p.context.canonicalPad.bRightTrigger=BYTE(rng());p.context.canonicalPad.sThumbLX=SHORT(rng());p.context.canonicalPad.sThumbLY=SHORT(rng());p.context.canonicalPad.sThumbRX=SHORT(rng());p.context.canonicalPad.sThumbRY=SHORT(rng());p.values.selector.resolved=bool(rng()&1);p.context.live=bool(rng()&1);p.context.hcMatrixAdmitted=bool(rng()&1);p.context.g_realStickTest.standaloneTestLane=bool(rng()&1);p.context.inputSource=i%3?"msi-dinput":"";equivalent(p);}
    auto p=cases.front().p;auto correct=hcOracle(p.values),wrong=correct;wrong["gyro"]["gx"]=int64_t(1);check(!exact(correct,wrong),"oracle rejects signed type");wrong=correct;wrong["gyro"]["gx"]=uint64_t(1);check(!exact(correct,wrong),"oracle rejects unsigned type");json signedOne=int64_t(1),unsignedOne=uint64_t(1);check(!exact(signedOne,unsignedOne),"oracle rejects signed/unsigned");auto z=p.values;z.hcCandidateGx=-0.0;wrong=hcOracle(z);wrong["gyro"]["gx"]=0.0;check(!exact(hcOracle(z),wrong),"oracle rejects negative zero");wrong=correct;wrong["gyro"]["gy"]=p.values.hcCandidateGx;check(!exact(correct,wrong),"oracle rejects scalar exchange");wrong=correct;wrong["resolved"]=uint64_t(1);check(!exact(correct,wrong),"oracle rejects resolved type");wrong=correct;wrong["providerBindingProven"]=true;check(!exact(correct,wrong),"oracle rejects provider binding");wrong=correct;wrong["recipeReceipt"]["gyro"]["provider"]="fixture-wrong";check(!exact(correct,wrong),"oracle rejects receipt mutation");
    p=cases.front().p;p.values.sensorMeta.gyroFamily=std::string(1,char(0xff));bindReceipt(p.values);std::array<int,2> ids{};for(unsigned side=0;side<2;++side){bool threw=false;try{auto q=(side?candidateHc:baselineHc)(p.values);auto d=q.dump();}catch(const json::type_error& e){ids[side]=e.id;threw=true;}check(threw,"invalid UTF-8 dump exception retained");}check(ids[0]==316&&ids[0]==ids[1],"dump exception identity parity");
    return {{"authorizedSampleCases",all.size()},{"scenarioCases",cases.size()},{"randomBitPatternCases",1200},{"flagSequenceCases",64},{"wide64Cases",wide.size()},{"doubleExtremesNonfiniteCases",xs.size()},{"floatExtremesNonfiniteCases",fs.size()},{"capabilityFamilyFlagCases",capabilityCases},{"receiptAllocationRequestsObserved",faultPoints},{"receiptCallCountOrderExceptionsOwnershipPass",true},{"recursiveExactTypesNumbersFloatingBitsAndDumpBytes",true},{"allArchivedHcReceiptsMatchCurrentActualFunction",true},{"recordMovesInactiveOverridePass",true},{"independentFieldAndReceiptOracle",true},{"oracleMutationProbes",8},{"dumpExceptionParity",true},{"inputScalarsAndContextUnchanged",true},{"counterProbePass",true}};
}enum class Scope {Construction,RecordDump,WriterCopyDump};
static const std::array<const char*,3> scopeNames={"construction","owned-record-dump","writer-copy-dump"};
static NOINLINE uint64_t execute(Builder builder,Prepared& p,Scope scope) {
    if(scope==Scope::Construction){auto hc=builder(p.values);return hc.size();}
    auto rec=buildRecord(builder,p);
    if(scope==Scope::WriterCopyDump){auto wire=writerDump(rec,p.context.writerPid);return wire.size()+rec.size();}
    auto wire=rec.dump();return wire.size()+rec.size();
}
static std::vector<Prepared> preparedBatch(const Prepared& p,size_t n) {
    bindReceipt(p.values);std::vector<Prepared> r;r.reserve(n);for(size_t i=0;i<n;++i)r.push_back(p);return r;
}
static Count allocations(Builder builder,const Prepared& p,Scope scope) {
    auto batch=preparedBatch(p,32);counter={};counting=true;uint64_t sizes=0;
    for(auto& row:batch)sizes+=execute(builder,row,scope);counting=false;sink+=sizes;return counter;
}
static json allocationEvidence(const std::vector<Scenario>& cases) {
    json rows=json::array();
    for(const auto& c:cases)for(unsigned scope=0;scope<3;++scope){
        auto b=allocations(baselineHc,c.p,Scope(scope)),a=allocations(candidateHc,c.p,Scope(scope));auto b2=allocations(baselineHc,c.p,Scope(scope)),a2=allocations(candidateHc,c.p,Scope(scope));
        check(b.requests()==b2.requests()&&b.bytes()==b2.bytes()&&a.requests()==a2.requests()&&a.bytes()==a2.bytes(),"allocation determinism");
        rows.push_back({{"scenario",c.name},{"scope",scopeNames[scope]},{"baseline",counterJson(b,32)},{"candidate",counterJson(a,32)},{"fewerRequests",a.requests()<b.requests()},{"fewerBytes",a.bytes()<b.bytes()},{"scopeIncludesDestruction",true},{"setupExcluded",true},{"rssMeasured",false}});
    }
    return rows;
}
static void warm(Builder builder,const Prepared& p,Scope scope,unsigned batches) {for(unsigned k=0;k<batches;++k){auto batch=preparedBatch(p,32);for(auto& row:batch)sink+=execute(builder,row,scope);}}
static double measure(Builder builder,const Prepared& p,Scope scope,unsigned batches) {
    double elapsed=0;uint64_t sizes=0;for(unsigned k=0;k<batches;++k){auto batch=preparedBatch(p,32);const auto start=std::chrono::steady_clock::now();for(auto& row:batch)sizes+=execute(builder,row,scope);const auto stop=std::chrono::steady_clock::now();elapsed+=std::chrono::duration<double,std::nano>(stop-start).count();}sink+=sizes;return elapsed/double(batches*32);
}
static double median(std::vector<double> xs){std::sort(xs.begin(),xs.end());return xs[xs.size()/2];}
struct Series {std::vector<double> before,after,reduction;json rounds=json::array();};
static json benchmark(const std::vector<Scenario>& cases,const json& plan,const json& alloc,unsigned campaign,bool& accepted) {
    check(plan.at("campaigns")==2&&plan.at("independentCampaignProcesses")==true&&plan.at("roundsPerCampaign")==11&&plan.at("minimumFasterRounds")==10,"fixed campaign criteria");check(plan.at("minimumMedianReductionPercent")==5.0&&plan.at("batchSize")==32&&plan.at("batchesPerMeasurement")==24&&plan.at("warmupBatches")==6,"fixed timing criteria");check(plan.at("requireFewerAllocationRequests")==true&&plan.at("requireFewerRequestedBytes")==true,"fixed allocation criteria");
    accepted=true;const unsigned total=unsigned(cases.size())*3;std::vector<Series> series(total);counting=false;
    for(unsigned r=0;r<11;++r)for(unsigned pos=0;pos<total;++pos){unsigned idx=(pos+r*7+campaign*11)%total;const auto& c=cases[idx/3];auto scope=Scope(idx%3);bool candidateFirst=bool((r+campaign+idx)&1);if(candidateFirst){warm(candidateHc,c.p,scope,6);warm(baselineHc,c.p,scope,6);}else{warm(baselineHc,c.p,scope,6);warm(candidateHc,c.p,scope,6);}double b=0,a=0;if(candidateFirst){a=measure(candidateHc,c.p,scope,24);b=measure(baselineHc,c.p,scope,24);}else{b=measure(baselineHc,c.p,scope,24);a=measure(candidateHc,c.p,scope,24);}auto& s=series[idx];s.before.push_back(b);s.after.push_back(a);s.reduction.push_back((b-a)/b*100.0);s.rounds.push_back({{"round",r+1},{"candidateFirst",candidateFirst},{"orderPosition",pos},{"baselineNsPerOperation",b},{"candidateNsPerOperation",a},{"reductionPercent",(b-a)/b*100.0}});}
    json rows=json::array();for(unsigned idx=0;idx<total;++idx){auto& s=series[idx];unsigned faster=0;for(size_t r=0;r<s.before.size();++r)faster+=s.after[r]<s.before[r];double b=median(s.before),a=median(s.after),paired=median(s.reduction),ratio=(b-a)/b*100.0;const auto& allocation=alloc.at(idx);bool passed=faster>=10&&paired>=5.0&&ratio>=5.0&&allocation.at("fewerRequests").get<bool>()&&allocation.at("fewerBytes").get<bool>();accepted=accepted&&passed;rows.push_back({{"scenario",cases[idx/3].name},{"scope",scopeNames[idx%3]},{"fasterRounds",faster},{"totalRounds",11},{"baselineMedianNsPerOperation",b},{"candidateMedianNsPerOperation",a},{"medianPairedReductionPercent",paired},{"ratioOfMediansReductionPercent",ratio},{"allocation",allocation},{"meetsAllPredeterminedCriteria",passed},{"rounds",s.rounds}});}
    return json::array({{{"campaign",campaign},{"independentProcess",true},{"results",rows}}});
}
static json readJson(const char* f){std::ifstream s(f,std::ios::binary);if(!s)throw std::runtime_error("fixture input unavailable");json r;s>>r;return r;}
int main(int argc,char** argv){try{check(argc==4,"usage: fixture verify|benchmark-1|benchmark-2 samples.json all-authorized-samples.json");std::string mode=argv[1];check(mode=="verify"||mode=="benchmark-1"||mode=="benchmark-2","invalid mode");auto items=readJson(argv[2]),all=readJson(argv[3]);auto cases=scenarios(items);auto verified=correctness(cases,all);auto alloc=allocationEvidence(cases);bool accepted=false;json campaigns=json::array();if(mode!="verify"){auto gate=readJson("parent-benchmark-go.json");check(gate.at("authorization")=="benchmark-go","parent timing authorization required");auto plan=readJson("acceptance-plan.json");campaigns=benchmark(cases,plan,alloc,mode=="benchmark-1"?1:2,accepted);}json result={{"correctnessPass",true},{"assertions",assertions},{"correctness",verified},{"allocationEvidence",alloc},{"formalTimingExecuted",mode!="verify"},{"campaigns",campaigns},{"allocationCountersDisabledDuringTiming",true},{"destructionIncluded",true},{"setupExcluded",true},{"receiptFunctionUnmodifiedInTimedScope",true},{"physicalSensorFunctionsExecuted",false},{"productOrInstalledApisExecuted",false},{"notes","Exact current HC builder and receipt; vendored default JSON allocator; other trees/scalars/binding setup prepared outside timed region. Counter measures requested new bytes, not RSS. Writer PID is supplied to exact copy+dump snippet. Original initializer already moves receipt rvalues."},{"sink",sink}};result["candidateAccepted"]=mode=="verify"?json(nullptr):json(accepted);std::cout<<result.dump(2)<<'\n';return 0;}catch(const std::exception& e){counting=false;faultAt=-1;std::cerr<<e.what()<<'\n';return 1;}}