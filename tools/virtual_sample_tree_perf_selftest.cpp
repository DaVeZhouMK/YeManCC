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
struct RecordInput {
    ULONGLONG now=0;
    unsigned long long g_inputCaptureBusTickGlobal=0;
    float motionDeltaSeconds=0;
    XINPUT_GAMEPAD canonicalPad{};
    float ox=0,oy=0,oz=0,conf=0,psx=0,psy=0,wsx=0,wsy=0;
    bool steady=false,live=false,hcMatrixAdmitted=false;
    DWORD primarySlot=0,connectedMask=0,writerPid=0;
    std::string inputSource,matrixIdentity;
    void* g_gm=nullptr;
    struct Config {bool motionTriggerResolved=false,aimingSightsTriggerResolved=false,standaloneTestLane=false;} g_realStickTest;
    struct Selector {bool resolved=false;} g_hcMotionSelector;
};
#include "extracted.hpp"
static_assert(std::is_same_v<decltype(Scalars::currentBindingGeneration),bool>);
static_assert(std::is_same_v<decltype(Scalars::gyroSequence),UINT64>);
static_assert(std::is_same_v<decltype(Scalars::gravityX),float>);
static_assert(std::is_same_v<decltype(Scalars::cgx),double>);
static_assert(std::is_same_v<decltype(RecordInput::motionDeltaSeconds),float>);
using TreeBuilder=Trees(*)(const Scalars&);

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
// Explicit field oracle written independently from the extraction/transform.
// Conversions spell out the production number type (float inputs become double
// JSON numbers; sequence/timestamps/generations retain unsigned 64-bit type).
static Trees oracle(const Scalars& v) {
    json i=json::object(),e=json::object();
    i["gyro"]=bool(v.gyro);i["accel"]=bool(v.accel);
    i["gx"]=double(v.cgx);i["gy"]=double(v.cgy);i["gz"]=double(v.cgz);
    i["ax"]=double(v.maxa);i["ay"]=double(v.may);i["az"]=double(v.maz);
    i["raw"]["gx"]=double(v.gx);i["raw"]["gy"]=double(v.gy);i["raw"]["gz"]=double(v.gz);
    i["raw"]["ax"]=double(v.ax);i["raw"]["ay"]=double(v.ay);i["raw"]["az"]=double(v.az);
    i["gravity"]["x"]=double(v.gravityX);i["gravity"]["y"]=double(v.gravityY);i["gravity"]["z"]=double(v.gravityZ);
    i["processedAcceleration"]["x"]=double(v.linearAccelX);i["processedAcceleration"]["y"]=double(v.linearAccelY);i["processedAcceleration"]["z"]=double(v.linearAccelZ);
    i["orientation"]["w"]=double(v.orientationW);i["orientation"]["x"]=double(v.orientationX);
    i["orientation"]["y"]=double(v.orientationY);i["orientation"]["z"]=double(v.orientationZ);
    e["gyro"]["sequence"]=uint64_t(v.gyroSequence);e["gyro"]["timestamp100ns"]=uint64_t(v.gyroTimestamp100ns);
    e["gyro"]["hcTimeOfDayMs"]=double(v.gyroTimeOfDayMs);e["gyro"]["receiptTick"]=uint64_t(v.gyroReceiptTick);
    e["gyro"]["bindingGeneration"]=uint64_t(v.gyroBindingGeneration);
    e["accel"]["sequence"]=uint64_t(v.accelSequence);e["accel"]["timestamp100ns"]=uint64_t(v.accelTimestamp100ns);
    e["accel"]["hcTimeOfDayMs"]=double(v.accelTimeOfDayMs);e["accel"]["receiptTick"]=uint64_t(v.accelReceiptTick);
    e["accel"]["bindingGeneration"]=uint64_t(v.accelBindingGeneration);
    e["sameBindingGeneration"]=bool(v.sameBindingGeneration);
    e["currentBindingGeneration"]=bool(v.currentBindingGeneration);e["activeBindingGeneration"]=uint64_t(v.activeBindingGeneration);
    return {std::move(i),std::move(e)};
}
struct Prepared {Scalars values;RecordInput context;json hc,test,extras;};
static json recordOracle(const Prepared& p,Trees&& trees) {
    const auto& c=p.context;json r=json::object();
    r["t"]=uint64_t(c.now);r["kind"]="sample";r["busTick"]=uint64_t(c.g_inputCaptureBusTickGlobal);
    r["dtMs"]=double(c.motionDeltaSeconds)*1000.0;r["connected"]=bool(c.live);
    r["slot"]=int(c.primarySlot);r["slots"]=int(c.connectedMask);
    r["inputSource"]=c.inputSource.empty()?std::string("none"):c.inputSource;
    r["buttons"]=uint64_t(c.canonicalPad.wButtons);r["lt"]=uint64_t(c.canonicalPad.bLeftTrigger);r["rt"]=uint64_t(c.canonicalPad.bRightTrigger);
    r["lx"]=int64_t(c.canonicalPad.sThumbLX);r["ly"]=int64_t(c.canonicalPad.sThumbLY);
    r["rx"]=int64_t(c.canonicalPad.sThumbRX);r["ry"]=int64_t(c.canonicalPad.sThumbRY);
    r["motionTriggerResolved"]=bool(c.g_realStickTest.motionTriggerResolved);
    r["aimingSightsTriggerResolved"]=bool(c.g_realStickTest.aimingSightsTriggerResolved);
    r["matrixIdentity"]=c.matrixIdentity;
    r["matrixStage"]=c.hcMatrixAdmitted?(c.g_realStickTest.standaloneTestLane?"active-test-lane":"active-hc-runtime"):
        (c.g_hcMotionSelector.resolved?"candidate-unbound-provider":"raw-unbound-device");
    r["matrixAdmission"]=bool(c.hcMatrixAdmitted);
    r["offset"]["x"]=double(c.ox);r["offset"]["y"]=double(c.oy);r["offset"]["z"]=double(c.oz);
    r["confidence"]=double(c.conf);r["steady"]=bool(c.steady);r["gamepadMotion"]=bool(c.g_gm);
    r["playerSpace"]["x"]=double(c.psx);r["playerSpace"]["y"]=double(c.psy);
    r["worldSpace"]["x"]=double(c.wsx);r["worldSpace"]["y"]=double(c.wsy);
    r["imu"]=std::move(trees.imu);r["event"]=std::move(trees.event);r["hcMatrixCandidate"]=p.hc;r["testLane"]=p.test;
    for(auto it=p.extras.begin();it!=p.extras.end();++it)r[it.key()]=it.value();
    if(p.values.gyroSequence==0&&p.values.accelSequence==0) {r["imu"]=json::object();r["imu"]["imuActive"]=false;}
    return r;
}
static Prepared fromSample(const json& r) {
    Prepared p;auto& v=p.values;auto& c=p.context;const auto& i=r.at("imu");const auto& e=r.at("event");
    if(i.contains("gx")) {
        v.gyro=i.at("gyro");v.accel=i.at("accel");v.cgx=i.at("gx");v.cgy=i.at("gy");v.cgz=i.at("gz");
        v.maxa=i.at("ax");v.may=i.at("ay");v.maz=i.at("az");
        v.gx=i.at("raw").at("gx");v.gy=i.at("raw").at("gy");v.gz=i.at("raw").at("gz");
        v.ax=i.at("raw").at("ax");v.ay=i.at("raw").at("ay");v.az=i.at("raw").at("az");
        v.gravityX=i.at("gravity").at("x");v.gravityY=i.at("gravity").at("y");v.gravityZ=i.at("gravity").at("z");
        v.linearAccelX=i.at("processedAcceleration").at("x");v.linearAccelY=i.at("processedAcceleration").at("y");v.linearAccelZ=i.at("processedAcceleration").at("z");
        v.orientationW=i.at("orientation").at("w");v.orientationX=i.at("orientation").at("x");
        v.orientationY=i.at("orientation").at("y");v.orientationZ=i.at("orientation").at("z");
    } else {
        // The authorized inactive logs omit the discarded builder values.
        // Fixture supplies constructor defaults; this is not sensor reconstruction.
        v.orientationW=1;
    }
    v.gyroSequence=e.at("gyro").at("sequence");v.gyroTimestamp100ns=e.at("gyro").at("timestamp100ns");
    v.gyroTimeOfDayMs=e.at("gyro").at("hcTimeOfDayMs");v.gyroReceiptTick=e.at("gyro").at("receiptTick");v.gyroBindingGeneration=e.at("gyro").at("bindingGeneration");
    v.accelSequence=e.at("accel").at("sequence");v.accelTimestamp100ns=e.at("accel").at("timestamp100ns");
    v.accelTimeOfDayMs=e.at("accel").at("hcTimeOfDayMs");v.accelReceiptTick=e.at("accel").at("receiptTick");v.accelBindingGeneration=e.at("accel").at("bindingGeneration");
    v.activeBindingGeneration=e.at("activeBindingGeneration");v.sameBindingGeneration=e.at("sameBindingGeneration");v.currentBindingGeneration=e.at("currentBindingGeneration");
    c.now=r.at("t");c.g_inputCaptureBusTickGlobal=r.at("busTick");c.motionDeltaSeconds=float(r.at("dtMs").get<double>()/1000.0);
    c.live=r.at("connected");c.primarySlot=r.at("slot");c.connectedMask=r.at("slots");c.writerPid=r.at("writerPid");
    c.inputSource=r.at("inputSource");c.matrixIdentity=r.at("matrixIdentity");c.hcMatrixAdmitted=r.at("matrixAdmission");
    c.g_realStickTest.motionTriggerResolved=r.at("motionTriggerResolved");c.g_realStickTest.aimingSightsTriggerResolved=r.at("aimingSightsTriggerResolved");
    c.g_realStickTest.standaloneTestLane=r.at("matrixStage")=="active-test-lane";c.g_hcMotionSelector.resolved=r.at("matrixStage")=="candidate-unbound-provider";
    c.canonicalPad.wButtons=r.at("buttons");c.canonicalPad.bLeftTrigger=r.at("lt");c.canonicalPad.bRightTrigger=r.at("rt");
    c.canonicalPad.sThumbLX=r.at("lx");c.canonicalPad.sThumbLY=r.at("ly");c.canonicalPad.sThumbRX=r.at("rx");c.canonicalPad.sThumbRY=r.at("ry");
    c.ox=r.at("offset").at("x");c.oy=r.at("offset").at("y");c.oz=r.at("offset").at("z");c.conf=r.at("confidence");c.steady=r.at("steady");
    c.psx=r.at("playerSpace").at("x");c.psy=r.at("playerSpace").at("y");c.wsx=r.at("worldSpace").at("x");c.wsy=r.at("worldSpace").at("y");
    c.g_gm=r.at("gamepadMotion").get<bool>()?reinterpret_cast<void*>(uintptr_t(1)):nullptr;
    p.hc=r.at("hcMatrixCandidate");p.test=r.at("testLane");p.extras=json::object();
    for(const char* key:{"hcSelectorCandidate","testLaneConfig"})if(r.contains(key))p.extras[key]=r.at(key);
    return p;
}
static json contextFingerprint(const Prepared& p) {
    // Independent observer oracle also fingerprints all scalar context, strings,
    // pad fields and prepared payloads without reading C++ padding bytes.
    json r=recordOracle(p,oracle(p.values));
    r["writerPid"]=uint64_t(p.context.writerPid);return r;
}
static json buildRecord(TreeBuilder builder,Prepared& p) {
    auto t=builder(p.values);
    return observedRecord(p.values,p.context,std::move(t),std::move(p.hc),std::move(p.test),std::move(p.extras));
}
static void equivalent(const Prepared& p) {
    auto before=scalarFingerprint(p.values);auto contextBefore=contextFingerprint(p);
    auto b=baselineTrees(p.values),c=candidateTrees(p.values),o=oracle(p.values);
    exactAndDump(b.imu,c.imu,"IMU baseline/candidate types/numbers/bits");
    exactAndDump(b.event,c.event,"event baseline/candidate types/numbers/bits");
    exactAndDump(b.imu,o.imu,"IMU independent field oracle");exactAndDump(b.event,o.event,"event independent field oracle");
    check(b.imu.is_object()&&b.imu.size()==12,"IMU root schema");
    check(b.imu.at("raw").size()==6&&b.imu.at("gravity").size()==3&&b.imu.at("processedAcceleration").size()==3&&b.imu.at("orientation").size()==4,"IMU child schema");
    check(b.event.is_object()&&b.event.size()==5&&b.event.at("gyro").size()==5&&b.event.at("accel").size()==5,"event schema");
    check(b.event.at("currentBindingGeneration").is_boolean(),"original currentBindingGeneration bool type");
    auto bp=p,cp=p;auto br=buildRecord(baselineTrees,bp),cr=buildRecord(candidateTrees,cp);
    auto expected=recordOracle(p,oracle(p.values));
    exactAndDump(br,cr,"observed current record parity");exactAndDump(br,expected,"observed current record independent oracle");
    check(bp.hc.is_null()&&bp.test.is_null()&&cp.hc.is_null()&&cp.test.is_null(),"existing owned trees transfer");
    if(!p.values.gyroSequence&&!p.values.accelSequence)check(br.at("imu").size()==1&&br.at("imu").at("imuActive")==false,"inactive rec override retained");
    auto bw=writerDump(br,p.context.writerPid),cw=writerDump(cr,p.context.writerPid);
    check(bw==cw,"actual current logger copy/pid/dump parity");
    expected["writerPid"]=uint64_t(p.context.writerPid);check(bw==expected.dump(),"logger independent writerPid oracle");
    check(before==scalarFingerprint(p.values),"input scalar mutation");
    check(before==scalarFingerprint(bp.values)&&before==scalarFingerprint(cp.values),"observer input scalar mutation");
    exactAndDump(contextBefore,contextFingerprint(p),"input context mutation");
}
static json counterJson(const Count& c,size_t n=1) {
    return {{"requestsPerOperation",double(c.requests())/n},{"requestedBytesPerOperation",double(c.bytes())/n},
            {"ordinaryRequestsPerOperation",double(c.ordinaryRequests)/n},{"ordinaryBytesPerOperation",double(c.ordinaryBytes)/n},
            {"alignedRequestsPerOperation",double(c.alignedRequests)/n},{"alignedBytesPerOperation",double(c.alignedBytes)/n}};
}
static void validateCounter() {
    counter={};counting=true;
    auto a=::operator new(3);auto b=::operator new[](17);auto c=::operator new(128,std::align_val_t(64));
    auto d=::operator new(5,std::nothrow);auto e=::operator new[](192,std::align_val_t(64),std::nothrow);
    ::operator delete(a);::operator delete[](b);::operator delete(c,std::align_val_t(64));
    ::operator delete(d,std::nothrow);::operator delete[](e,std::align_val_t(64),std::nothrow);
    counting=false;
    check(counter.ordinaryRequests==3&&counter.ordinaryBytes==25&&counter.alignedRequests==2&&counter.alignedBytes==320,"ordinary/aligned requested-byte counter probe");
}
static void mutationSensitivity(const Prepared& p) {
    auto expected=oracle(p.values);auto wrong=baselineTrees(p.values);
    wrong.event["currentBindingGeneration"]=uint64_t(p.values.currentBindingGeneration);
    check(!exact(wrong.event,expected.event),"oracle missed bool-to-generation mutation");
    wrong=baselineTrees(p.values);wrong.event["gyro"]["sequence"]=uint64_t(p.values.gyroSequence+1);
    check(!exact(wrong.event,expected.event),"oracle missed wide sequence mutation");
    wrong=baselineTrees(p.values);wrong.imu["raw"]["gx"]=p.values.gx+1.0;
    check(!exact(wrong.imu,expected.imu),"oracle missed scalar-expression mutation");
    auto v=p.values;v.cgx=-0.0;auto z=oracle(v);z.imu["gx"]=0.0;
    check(!exact(z.imu,oracle(v).imu),"oracle missed negative-zero mutation");
    wrong=baselineTrees(p.values);wrong.imu.erase("gravity");
    check(!exact(wrong.imu,expected.imu),"oracle missed schema mutation");
}
struct Scenario {std::string name;Prepared p;};
static std::vector<Scenario> scenarios(const json& samples) {
    std::vector<Scenario> result;
    for(const auto& item:samples) {
        Scenario s{item.at("name").get<std::string>(),fromSample(item.at("sample"))};
        if(s.name=="active-gyro-only-synthetic") {
            s.p.values.accel=false;s.p.values.accelSequence=0;s.p.values.accelBindingGeneration=0;
            s.p.values.accelTimestamp100ns=0;s.p.values.accelTimeOfDayMs=0;s.p.values.accelReceiptTick=0;
            s.p.values.sameBindingGeneration=false;s.p.values.currentBindingGeneration=false;
            s.p.context.hcMatrixAdmitted=false;
        }
        if(s.name=="active-accel-only-synthetic") {
            s.p.values.gyro=false;s.p.values.gyroSequence=0;s.p.values.gyroBindingGeneration=0;
            s.p.values.gyroTimestamp100ns=0;s.p.values.gyroTimeOfDayMs=0;s.p.values.gyroReceiptTick=0;
            s.p.values.sameBindingGeneration=false;s.p.values.currentBindingGeneration=false;
            s.p.context.hcMatrixAdmitted=false;
        }
        result.push_back(std::move(s));
    }
    return result;
}
static json correctness(const std::vector<Scenario>& cases,const json& allSamples) {
    validateCounter();
    for(const auto& c:cases)equivalent(c.p);
    mutationSensitivity(cases.front().p);
    for(const auto& sample:allSamples)equivalent(fromSample(sample));
    Prepared p=cases.front().p;
    // All flag combinations independent of sequence-active/inactive state.
    for(unsigned flags=0;flags<16;++flags)for(unsigned seq=0;seq<4;++seq) {
        p.values.gyro=flags&1;p.values.accel=flags&2;
        p.values.sameBindingGeneration=flags&4;p.values.currentBindingGeneration=flags&8;
        p.values.gyroSequence=(seq&1)?UINT64(0x100000001):0;
        p.values.accelSequence=(seq&2)?UINT64(0xffffffffffffffffULL):0;
        equivalent(p);
    }
    const std::array<uint64_t,10> wide={0,1,0xffffffffULL,0x100000001ULL,0x1fffffffffffffULL,0x20000000000001ULL,
        0x7fffffffffffffffULL,0x8000000000000000ULL,0xfffffffffffffffeULL,0xffffffffffffffffULL};
    for(auto x:wide){p=cases.front().p;fillUnsigned(p.values,x);p.context.now=x;p.context.g_inputCaptureBusTickGlobal=x;equivalent(p);}
    const std::array<double,14> doubles={0.0,-0.0,std::numeric_limits<double>::max(),std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::min(),std::numeric_limits<double>::denorm_min(),-std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),
        std::bit_cast<double>(uint64_t(0x7ff8000000000042ULL)),std::bit_cast<double>(uint64_t(0xfff800000000007fULL)),
        86399999.99999999,0.125,-23.75};
    for(auto x:doubles){p=cases.front().p;fillDouble(p.values,x);equivalent(p);}
    const std::array<float,13> floats={0.0f,-0.0f,std::numeric_limits<float>::max(),std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::min(),std::numeric_limits<float>::denorm_min(),-std::numeric_limits<float>::denorm_min(),
        std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),
        std::bit_cast<float>(uint32_t(0x7fc00042)),std::bit_cast<float>(uint32_t(0xffc0007f)),0.125f,-23.75f};
    for(auto x:floats) {
        p=cases.front().p;fillFloat(p.values,x);
        p.context.ox=p.context.oy=p.context.oz=p.context.conf=p.context.psx=p.context.psy=p.context.wsx=p.context.wsy=x;
        p.context.motionDeltaSeconds=x;equivalent(p);
    }
    std::mt19937_64 rng(0x20261015a016ULL);
    for(unsigned n=0;n<1600;++n) {
        p=cases[n%cases.size()].p;
        fillDouble(p.values,(double(rng()%2000000)-1000000.0)/1000.0);
        fillFloat(p.values,float((double(rng()%2000000)-1000000.0)/100000.0));
        // Distinct inputs catch accidental exchanges among similar field names.
        p.values.cgx+=.031;p.values.cgy-=.062;p.values.cgz+=.093;
        p.values.gx-=.012;p.values.gy+=.024;p.values.gz-=.036;
        p.values.ax+=.011;p.values.ay-=.022;p.values.az+=.033;
        p.values.gravityY+=.071f;p.values.linearAccelZ-=.092f;p.values.orientationW+=.125f;
        p.values.gyroTimeOfDayMs=double(rng()%86400000)+.125;p.values.accelTimeOfDayMs=double(rng()%86400000)+.75;
        p.values.gyroTimestamp100ns=rng();p.values.accelTimestamp100ns=rng();
        p.values.gyroReceiptTick=rng();p.values.accelReceiptTick=rng();
        p.values.gyroSequence=n%17?rng():0;p.values.accelSequence=n%17?rng():0;
        p.values.gyroBindingGeneration=rng();p.values.accelBindingGeneration=rng();p.values.activeBindingGeneration=rng();
        p.values.gyro=bool(rng()&1);p.values.accel=bool(rng()&1);p.values.sameBindingGeneration=bool(rng()&1);p.values.currentBindingGeneration=bool(rng()&1);
        p.context.now=rng();p.context.g_inputCaptureBusTickGlobal=rng();p.context.motionDeltaSeconds=float(double(rng()%16000+1)/1000000.0);
        p.context.primarySlot=DWORD(rng()%4);p.context.connectedMask=DWORD(rng()%16);
        p.context.canonicalPad.wButtons=WORD(rng());p.context.canonicalPad.bLeftTrigger=BYTE(rng());p.context.canonicalPad.bRightTrigger=BYTE(rng());
        p.context.canonicalPad.sThumbLX=SHORT(rng());p.context.canonicalPad.sThumbLY=SHORT(rng());
        p.context.canonicalPad.sThumbRX=SHORT(rng());p.context.canonicalPad.sThumbRY=SHORT(rng());
        p.context.live=bool(rng()&1);p.context.hcMatrixAdmitted=bool(rng()&1);
        p.context.g_realStickTest.standaloneTestLane=bool(rng()&1);p.context.g_hcMotionSelector.resolved=bool(rng()&1);
        p.context.inputSource=n%3?"msi-dinput":"";p.context.matrixIdentity=n%2?"hc-active-msi-MS-1T91":"unresolved-provider-binding";
        equivalent(p);
    }
    return {{"authorizedSampleCases",allSamples.size()},{"randomTypicalCases",1600},{"flagSequenceCases",64},
            {"wide64Cases",wide.size()},{"doubleExtremesNonfiniteCases",doubles.size()},{"floatExtremesNonfiniteCases",floats.size()},
            {"recursiveExactTypesNumbersFloatingBitsAndDumpBytes",true},{"independentFieldOracle",true},
            {"oracleMutationProbes",5},{"inputScalarsUnchanged",true},{"inactiveOverrideRetained",true},
            {"ordinaryAndAlignedRequestCounterProbe",true}};
}
enum class Scope {Trees,RecordDump,WriterCopyDump};
static const std::array<const char*,3> scopeNames={"trees","owned-record-dump","writer-copy-dump"};
static NOINLINE uint64_t execute(TreeBuilder builder,Prepared& p,Scope scope) {
    if(scope==Scope::Trees) {auto t=builder(p.values);return t.imu.size()+t.event.size();}
    auto rec=buildRecord(builder,p);
    if(scope==Scope::WriterCopyDump) {auto wire=writerDump(rec,p.context.writerPid);return wire.size()+rec.size();}
    auto wire=rec.dump();return wire.size()+rec.size();
    // Both JSON trees / current record / dump string are destroyed before return.
}
static std::vector<Prepared> preparedBatch(const Prepared& input,size_t n) {
    std::vector<Prepared> result;result.reserve(n);
    for(size_t i=0;i<n;++i)result.push_back(input);
    return result;
}
static Count allocations(TreeBuilder builder,const Prepared& input,Scope scope) {
    constexpr size_t n=32;auto batch=preparedBatch(input,n);counter={};counting=true;
    uint64_t sizes=0;for(auto& p:batch)sizes+=execute(builder,p,scope);
    counting=false;sink+=sizes;return counter;
}
static json allocationEvidence(const std::vector<Scenario>& cases) {
    json result=json::array();
    for(const auto& c:cases)for(unsigned scope=0;scope<3;++scope) {
        auto b=allocations(baselineTrees,c.p,Scope(scope)),a=allocations(candidateTrees,c.p,Scope(scope));
        // Repeated untimed request runs verify deterministic request counts.
        auto b2=allocations(baselineTrees,c.p,Scope(scope)),a2=allocations(candidateTrees,c.p,Scope(scope));
        check(b.requests()==b2.requests()&&b.bytes()==b2.bytes()&&a.requests()==a2.requests()&&a.bytes()==a2.bytes(),"allocation request count instability");
        result.push_back({{"scenario",c.name},{"scope",scopeNames[scope]},
            {"baseline",counterJson(b,32)},{"candidate",counterJson(a,32)},
            {"fewerRequests",a.requests()<b.requests()},{"fewerBytes",a.bytes()<b.bytes()},
            {"scopeIncludesDestruction",true},{"setupExcluded",true},{"rssMeasured",false}});
    }
    return result;
}
static void warm(TreeBuilder builder,const Prepared& input,Scope scope,unsigned batches) {
    for(unsigned k=0;k<batches;++k){auto batch=preparedBatch(input,32);for(auto& p:batch)sink+=execute(builder,p,scope);}
}
static double measure(TreeBuilder builder,const Prepared& input,Scope scope,unsigned batches) {
    double elapsed=0;uint64_t sizes=0;
    for(unsigned k=0;k<batches;++k) {
        auto batch=preparedBatch(input,32); // Prepared trees/context are outside timing.
        const auto start=std::chrono::steady_clock::now();
        for(auto& p:batch)sizes+=execute(builder,p,scope);
        const auto stop=std::chrono::steady_clock::now();
        elapsed+=std::chrono::duration<double,std::nano>(stop-start).count();
    }
    sink+=sizes;return elapsed/double(batches*32);
}
static double median(std::vector<double> xs) {std::sort(xs.begin(),xs.end());return xs[xs.size()/2];}
struct Series {std::vector<double> before,after,reduction;json rounds=json::array();};
static json benchmarks(const std::vector<Scenario>& cases,const json& plan,const json& allocationRows,bool& accepted) {
    check(plan.at("campaigns")==2&&plan.at("roundsPerCampaign")==11&&plan.at("minimumFasterRounds")==10,"predetermined campaign thresholds");
    check(plan.at("minimumMedianReductionPercent")==5.0&&plan.at("batchSize")==32&&plan.at("batchesPerMeasurement")==24,"predetermined timing thresholds");
    accepted=true;json campaigns=json::array();
    const unsigned total=unsigned(cases.size())*3;
    // One process/thread, rotating scenario/scope order and alternating B/C order.
    // Allocation counters are disabled throughout all timed runs.
    for(unsigned campaign=0;campaign<2;++campaign) {
        std::vector<Series> series(total);counting=false;
        for(unsigned r=0;r<11;++r)for(unsigned pos=0;pos<total;++pos) {
            unsigned idx=(pos+r*5+campaign*7)%total;
            const auto& c=cases[idx/3];auto scope=Scope(idx%3);
            bool candidateFirst=bool((r+campaign+idx)&1);
            if(candidateFirst) {warm(candidateTrees,c.p,scope,6);warm(baselineTrees,c.p,scope,6);}
            else {warm(baselineTrees,c.p,scope,6);warm(candidateTrees,c.p,scope,6);}
            double b=0,a=0;
            if(candidateFirst){a=measure(candidateTrees,c.p,scope,24);b=measure(baselineTrees,c.p,scope,24);}
            else {b=measure(baselineTrees,c.p,scope,24);a=measure(candidateTrees,c.p,scope,24);}
            auto& s=series[idx];s.before.push_back(b);s.after.push_back(a);s.reduction.push_back((b-a)/b*100.0);
            s.rounds.push_back({{"round",r+1},{"candidateFirst",candidateFirst},{"orderPosition",pos},
                              {"baselineNsPerOperation",b},{"candidateNsPerOperation",a},{"reductionPercent",(b-a)/b*100.0}});
        }
        json scenarios=json::array();
        for(unsigned idx=0;idx<total;++idx) {
            auto& s=series[idx];unsigned faster=0;for(size_t r=0;r<s.before.size();++r)faster+=s.after[r]<s.before[r];
            double b=median(s.before),a=median(s.after),paired=median(s.reduction),ratio=(b-a)/b*100.0;
            const auto& alloc=allocationRows.at(idx);
            bool passed=faster>=10&&paired>=5.0&&ratio>=5.0&&alloc.at("fewerRequests").get<bool>()&&alloc.at("fewerBytes").get<bool>();
            accepted=accepted&&passed;
            scenarios.push_back({{"scenario",cases[idx/3].name},{"scope",scopeNames[idx%3]},{"fasterRounds",faster},
                {"totalRounds",11},{"baselineMedianNsPerOperation",b},{"candidateMedianNsPerOperation",a},
                {"medianPairedReductionPercent",paired},{"ratioOfMediansReductionPercent",ratio},
                {"allocation",alloc},{"meetsAllPredeterminedCriteria",passed},{"rounds",s.rounds}});
        }
        campaigns.push_back({{"campaign",campaign+1},{"results",scenarios}});
    }
    return campaigns;
}
static json readJson(const char* file) {std::ifstream f(file,std::ios::binary);if(!f)throw std::runtime_error("fixture input open failed");json r;f>>r;return r;}
int main(int argc,char** argv) {
    try {
        check(argc==4,"usage: fixture verify|benchmark samples.json all-authorized-samples.json");
        std::string mode=argv[1];check(mode=="verify"||mode=="benchmark","invalid fixture mode");
        auto items=readJson(argv[2]),all=readJson(argv[3]);auto cases=scenarios(items);
        auto verified=correctness(cases,all);auto alloc=allocationEvidence(cases);
        bool accepted=false;json campaigns=json::array();
        if(mode=="benchmark") {auto plan=readJson("acceptance-plan.json");campaigns=benchmarks(cases,plan,alloc,accepted);}
        json result={{"correctnessPass",true},{"assertions",assertions},{"correctness",verified},
            {"allocationEvidence",alloc},{"formalTimingExecuted",mode=="benchmark"},{"campaigns",campaigns},
            {"allocationCountersDisabledDuringTiming",true},{"destructionIncluded",true},{"setupExcluded",true},
            {"physicalSensorFunctionsExecuted",false},{"productOrInstalledApisExecuted",false},
            {"notes","Only actual source blocks compiled with vendored default JSON allocator. Inactive hidden scalars use supplied defaults; actual inactive override is compiled unchanged. Request counters are ordinary/aligned new requests, not RSS. Scalar setup and other diagnostic-tree preparation are excluded."},
            {"sink",sink}};
        result["candidateAccepted"]=mode=="benchmark"?json(accepted):json(nullptr);
        std::cout<<result.dump(2)<<'\n';return 0;
    }catch(const std::exception& e){counting=false;std::cerr<<e.what()<<'\n';return 1;}
}