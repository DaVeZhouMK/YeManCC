// R18 isolated current-source hostFrame ownership fixture.
// The Python runner extracts the current production block before compiling this fixture.
// No native main, Host, Windows messages, WebView, SDK, devices or registry run.
#define NOMINMAX
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <crtdbg.h>
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
using json=nlohmann::json;
static_assert(std::is_same_v<json::allocator_type,std::allocator<json>>);
struct Count { std::uint64_t calls=0,bytes=0; };
static thread_local bool countEnabled=false,liveEnabled=false;
static thread_local Count count;
static thread_local std::int64_t liveBalance=0,failAfter=-1;
static void beforeAllocation(std::size_t n) {
    if(failAfter>=0 && failAfter--==0)throw std::bad_alloc();
    if(countEnabled){++count.calls;count.bytes+=n;}
}
void* operator new(std::size_t n){beforeAllocation(n);void* p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();if(liveEnabled)++liveBalance;return p;}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {if(p&&liveEnabled)--liveBalance;std::free(p);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete(void* p,std::size_t) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
void* operator new(std::size_t n,std::align_val_t a){beforeAllocation(n);void* p=_aligned_malloc(n?n:1,static_cast<std::size_t>(a));if(!p)throw std::bad_alloc();if(liveEnabled)++liveBalance;return p;}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t) noexcept {if(p&&liveEnabled)--liveBalance;_aligned_free(p);}
void operator delete[](void* p,std::align_val_t a) noexcept {::operator delete(p,a);}
void operator delete(void* p,std::size_t,std::align_val_t a) noexcept {::operator delete(p,a);}
void operator delete[](void* p,std::size_t,std::align_val_t a) noexcept {::operator delete(p,a);}
static std::atomic<std::uint64_t> assertions{0},benchmarkSink{0};
static void check(bool condition,const char* label){++assertions;if(!condition)throw std::runtime_error(label);}
static bool sameRepresentation(const json&a,const json&b){
    if(a.type()!=b.type())return false;
    if(a.is_number_float())return std::bit_cast<std::uint64_t>(a.get<double>())==std::bit_cast<std::uint64_t>(b.get<double>());
    if(a.is_structured()){
        if(a.size()!=b.size())return false;auto x=a.begin(),y=b.begin();
        for(;x!=a.end();++x,++y){if(a.is_object()&&x.key()!=y.key())return false;if(!sameRepresentation(x.value(),y.value()))return false;}return true;
    }
    if(a.is_discarded())return b.is_discarded();return a==b;
}
static const void* objectAddress(const json& value){return value.is_object()?std::addressof(value.get_ref<const json::object_t&>()):nullptr;}
struct BoolLatch {bool value=false;bool load(std::memory_order)const noexcept{return value;}};
struct Counter {std::uint64_t value=0;std::uint64_t fetch_add(std::uint64_t n,std::memory_order){auto old=value;value+=n;return old;}};
struct UiCostIpcScope {explicit UiCostIpcScope(const char*){}};
struct ChildView {bool view=true;};
static ChildView childA,childB;
using LPARAM=std::intptr_t;
static constexpr int WM_GAMEPAD_SERIAL_EVENT=0x410;
#include "event-struct-extracted.h"

// Inert seam for the exact current production expression; no sensor/device call runs.

struct Fixture {
    struct Config {std::string targetStick="right";bool motionTriggerResolved=true,aimingSightsTriggerResolved=true;}g_realStickTest;
    std::uint64_t now=258671,g_inputCaptureLastUi=0;
    double cgx=.29897409677505493,cgy=.019747555255889893,cgz=.1081552505493164;
    double maxa=.015371999703347683,may=.9323239922523499,maz=-.40796801447868347;
    double ax=.015371999703347683,ay=.40796801447868347,az=.9323239922523499;
    float psx=0,psy=0,wsx=0,wsy=0;
    float gravityX=-.004524147603660822f,gravityY=-.8954063057899475f,gravityZ=.44507360458374023f;
    float linearAccelX=0,linearAccelY=0,linearAccelZ=0;
    float orientationW=-.020204337313771248f,orientationX=.03335440903902054f,orientationY=-.852171778678894f,orientationZ=.521807074546814f;
    bool gamepadMotionPlaneProven=false,motionPlaneSteeringYaw=true;
    float motionPlaneGyroMultiplier=1,motionPlaneAccelMultiplier=1;
    std::string telemetryTimestampUtc="2026-10-14T08:03:14.0000000Z";
    json hostFrame=json::object();
    bool g_hwnd=true,g_view=true,postSuccess=true,consumerAllowed=true,costEnabled=false,sensorPresent=true,dropWindowDuringSnapshot=false;
    int emitterDumpMode=3; // 2 = owned envelope dumps only; 3 = envelope plus renderer dumps.
    BoolLatch g_exitRequested,g_inputReady{true};
    std::vector<std::pair<int,ChildView*>> g_children;
    Counter g_c17aOwnedEvents,g_c17aParityChecks,g_c17aParityFailures;
    GamepadSerialEvent* posted=nullptr;
    const void* wrapperHostAddress=nullptr;const void* envelopeHostAddress=nullptr;
    unsigned sensorSnapshots=0,postCalls=0,ipcCalls=0;
    std::vector<std::pair<int,std::string>> wire;
    bool tickCostEnabled()const{return costEnabled;}
    bool hardwareWriteAllowed()const{return consumerAllowed;}
    struct SensorMeta {bool gyroPresent;};
    SensorMeta inputCaptureSensorMetaSnapshot(){++sensorSnapshots;if(dropWindowDuringSnapshot)g_hwnd=false;return{sensorPresent};}
    bool inputCaptureGyroPresentSnapshot(){return inputCaptureSensorMetaSnapshot().gyroPresent;}
    bool PostMessageW(bool hwnd,int message,int,LPARAM value){
        ++postCalls;auto* payload=reinterpret_cast<GamepadSerialEvent*>(value);
        wrapperHostAddress=objectAddress(payload->data.at("hostFrame"));
        if(postSuccess)posted=payload;return postSuccess;
    }
    void appendNativeLifecycleLog(const char*,const json&){throw std::runtime_error("Unexpected IPC parity mismatch");}
    void ipc_post_json(int id,const json& envelope){
        if(id==0&&!g_view)return;
        ++ipcCalls;envelopeHostAddress=objectAddress(envelope.at("data").at("hostFrame"));
        if(emitterDumpMode>=3) wire.emplace_back(id,envelope.dump());
        else wire.emplace_back(id,std::string{});
    }
    void ipc_emit(const std::string&,const json&){throw std::runtime_error("Unexpected generic IPC branch");}
#include "owned-wrappers-extracted.h"
#include "event-extracted.h"
    void consumePosted(){
        auto* event=posted;posted=nullptr;
        if(!event)return;
        try {
            if(g_inputReady.load(std::memory_order_acquire)&&hardwareWriteAllowed()){
                if(event->event=="gyro.telemetry")ipc_emit_owned_gyro_telemetry(std::move(event->event),std::move(event->data));
                else ipc_emit(event->event,event->data);
            }
        }catch(...){delete event;throw;}
        delete event;
    }
    ~Fixture(){delete posted;}
};
using Builder=json(Fixture::*)();using Poster=void(Fixture::*)();
struct Outcome {json data;int errorId=0;std::string exception,message;};
static Outcome build(Fixture&f,Builder method){
    try{return{(f.*method)(),0,"",""};}
    catch(const json::exception&e){return{nullptr,e.id,typeid(e).name(),e.what()};}
    catch(const std::exception&e){return{nullptr,0,typeid(e).name(),e.what()};}
}
static Outcome dumpOutcome(const json&data,json::error_handler_t handler=json::error_handler_t::strict){
    try{return{data.dump(-1,' ',false,handler),0,"",""};}
    catch(const json::exception&e){return{nullptr,e.id,typeid(e).name(),e.what()};}
}
static void equivalent(Fixture base){
    Fixture a=base,b=base;const json original=base.hostFrame;const void* source=objectAddress(b.hostFrame);
    const auto x=build(a,&Fixture::beforeInitializer),y=build(b,&Fixture::candidateInitializer);
    check(x.exception==y.exception&&x.errorId==y.errorId&&x.message==y.message,"Initializer exception identity differs");
    check(sameRepresentation(a.hostFrame,original),"Before initializer consumed or changed source");
    if(!x.exception.empty()){
        check(sameRepresentation(b.hostFrame,original),"Earlier value() exception consumed candidate source");
        check(a.sensorSnapshots==b.sensorSnapshots,"Exception changed snapshot count");return;
    }
    check(x.data.is_object()&&y.data.is_object(),"Full initializer did not produce object");
    check(x.data.size()==27&&y.data.size()==27,"Complete event field count changed");
    check(sameRepresentation(x.data,y.data),"Full event types/float bits/fields differ");
    check(sameRepresentation(x.data.at("hostFrame"),original),"Before event omitted source fields");
    check(sameRepresentation(y.data.at("hostFrame"),original),"Candidate event omitted source fields");
    check(b.hostFrame.is_null(),"Candidate did not consume hostFrame");
    if(source)check(objectAddress(y.data.at("hostFrame"))==source,"Initializer deep-copied the moved hostFrame");
    check(a.sensorSnapshots==1&&b.sensorSnapshots==1,"Sensor snapshot seam behavior changed");
    check(x.data.at("sensorPresent").is_boolean()&&x.data.at("sensorPresent")==base.sensorPresent,"source sensor snapshot value/type was not preserved");
    for(auto handler:{json::error_handler_t::strict,json::error_handler_t::replace,json::error_handler_t::ignore}){
        const auto xd=dumpOutcome(x.data,handler),yd=dumpOutcome(y.data,handler);
        check(xd.errorId==yd.errorId&&xd.exception==yd.exception&&xd.message==yd.message,"Dump exception identity differs");
        if(xd.exception.empty())check(xd.data==yd.data,"Serialized UTF8 event bytes differ");
    }
    if(base.hostFrame.is_object()){
        const auto left=base.hostFrame.value("leftStick",json{{"x",0.0},{"y",0.0}});
        const auto right=base.hostFrame.value("rightStick",json{{"x",0.0},{"y",0.0}});
        check(sameRepresentation(y.data.at("leftStick"),left),"Left stick value read after move");
        check(sameRepresentation(y.data.at("rightStick"),right),"Right stick value read after move");
        check(sameRepresentation(y.data.at("output"),base.g_realStickTest.targetStick=="left"?left:right),"Output value/fallback read after move");
    }
}
static void wrapperEquivalent(Fixture base,bool withEmitter){
    Fixture a=base,b=base;const void* source=objectAddress(b.hostFrame);const json original=base.hostFrame;
    a.beforePost();b.candidatePost();
    check(sameRepresentation(a.hostFrame,original)&&b.hostFrame.is_null(),"Wrapper producer source ownership differs");
    check(a.postCalls==b.postCalls&&bool(a.posted)==bool(b.posted),"Owned wrapper routing changed");
    if(a.posted){
        check(a.posted->event==b.posted->event,"Event name changed");
        check(sameRepresentation(a.posted->data,b.posted->data),"Owned message payload differs");
        if(source)check(b.wrapperHostAddress==source,"Owned wrapper copied moved hostFrame");
    }
    if(withEmitter){
        a.consumePosted();b.consumePosted();
        check(a.wire==b.wire,"Complete UI envelope wire or renderer order differs");
        check(a.g_c17aParityFailures.value==0&&b.g_c17aParityFailures.value==0,"Existing diagnostic parity guard failed");
        if(b.ipcCalls&&source)check(b.envelopeHostAddress==source,"Owned UI envelope copied candidate hostFrame");
    }
}
static json hostFrameFixture(const json&sample,bool receipt){
    const auto&imu=sample.at("imu");
    json host={{"hostSubmission","frame-accepted"},{"hostActive",true},{"firstFrame",false},{"lifecycle","active"},
        {"frameProof",{{"sampleSequence",30556ULL},{"motionPairProofId","hc-same-provider-selector-v2"}}},
        {"persona","steamdeck"},{"targetStick","right"},{"focusIsolationSemanticNeutral",false},
        {"motionAdmission","hc-motion-admitted"},{"analogAdmission","stable"},
        {"leftStick",{{"x",0.0},{"y",0.0}}},{"rightStick",{{"x",0.0},{"y",0.0}}},
        {"gyroDps",{{"x",imu.at("gx")},{"y",imu.at("gy")},{"z",imu.at("gz")}}},
        {"motionContribution",{{"x",0.0},{"y",0.0}}},{"layoutContribution",{{"x",0.0},{"y",0.0}}},
        {"motionManager",{{"mapped",true},{"triggered",false},{"velocityMode",false},{"gyroShort",{{"x",0.0},{"y",0.0}}},{"modifiedShort",{{"x",0.0},{"y",0.0}}}}},
        {"layoutManager",{{"physicalStick",{{"x",0.0},{"y",0.0}}},{"stickNorm",0.0},{"gyroWeight",1.0},{"weightFactor",1.0},{"mergeOrder","physical-stick-plus-motion-contribution"}}}};
    if(receipt)host["frameProof"]["hostReceipt"]={{"runId",std::string(64,'r')},{"epoch",7ULL},{"powerGeneration",5ULL},
        {"hostInstanceId",std::string(32,'a')},{"sampleSequence",30556ULL},{"hostAckAccepted",true},{"receiptCompletedTick",258671ULL}};
    return host;
}

struct Measurement {double ns=0;double calls=0;double bytes=0;};
static Measurement measure(const Fixture&base,int mode,bool candidate,bool allocations,int batches){
    constexpr int n=32;double elapsed=0,totalCalls=0,totalBytes=0;std::uint64_t sizeSum=0;
    for(int batch=0;batch<batches;++batch){
        std::vector<Fixture> prepared;prepared.reserve(n);
        for(int i=0;i<n;++i)prepared.push_back(base);
        count={};countEnabled=allocations;
        const auto started=std::chrono::steady_clock::now();
        for(auto&f:prepared){
            if(mode==0){auto data=(f.*(candidate?&Fixture::candidateInitializer:&Fixture::beforeInitializer))();sizeSum+=data.size();}
            else{
                (f.*(candidate?&Fixture::candidatePost:&Fixture::beforePost))();
                if(mode>=2){f.emitterDumpMode=mode==2?2:3;f.consumePosted();for(const auto&row:f.wire)sizeSum+=row.second.size();}
                else if(f.posted)sizeSum+=f.posted->data.size();
            }
        }
        // Owned event, envelope, wire strings and source hostFrame destruction are timed.
        prepared.clear();
        elapsed+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-started).count();
        countEnabled=false;totalCalls+=static_cast<double>(count.calls);totalBytes+=static_cast<double>(count.bytes);
    }
    benchmarkSink.fetch_add(sizeSum,std::memory_order_relaxed);
    const double operations=static_cast<double>(n*batches);return{elapsed/operations,totalCalls/operations,totalBytes/operations};
}
static double median(std::vector<double> samples){std::sort(samples.begin(),samples.end());return samples[samples.size()/2];}
static json scenario(const json&sample,int kind){
    Fixture f;f.hostFrame=hostFrameFixture(sample,kind!=0);
    if(kind>=2)for(unsigned i=0;i<8;++i)f.hostFrame["r18-inert-"+std::to_string(i)]={{"sequence",static_cast<unsigned long long>(i)},{"text",std::string(96+i*13,'x')},{"imu",sample.at("imu")},{"array",json::array({i,true,"微星",nullptr})}};
    if(kind>=3)f.hostFrame["r18-all-types"]={{"signed",std::numeric_limits<std::int64_t>::min()},{"unsigned",std::numeric_limits<std::uint64_t>::max()},{"binary",json::binary({0,127,255},42)},{"text","微星\n\"\\\t"},{"null",nullptr},{"bool",false},{"nested",json::array({json::object(),1,-0.0,"UTF8"})}};
    return f.hostFrame;
}
struct RowSpec{const char*name;int mode;int scenarioKind;bool diagnostics;bool child;};
static Fixture makeRow(const json&sample,const RowSpec&spec){Fixture f;f.hostFrame=scenario(sample,spec.scenarioKind);f.costEnabled=spec.diagnostics;f.emitterDumpMode=spec.mode>=3?3:2;if(spec.child)f.g_children={{2,&childA},{4,&childB}};return f;}
static json benchmarkRow(const Fixture&base,const char*name,int mode,int campaign,int batches){
    constexpr unsigned rounds=11;measure(base,mode,false,false,1);measure(base,mode,true,false,1);
    const auto beforeCount=measure(base,mode,false,true,1),candidateCount=measure(base,mode,true,true,1);
    std::vector<double>beforeNs,afterNs,pairedReductions,ratios;unsigned wins=0;
    for(unsigned round=0;round<rounds;++round){double before,after;if(((round+static_cast<unsigned>(campaign))&1u)==0u){before=measure(base,mode,false,false,batches).ns;after=measure(base,mode,true,false,batches).ns;}else{after=measure(base,mode,true,false,batches).ns;before=measure(base,mode,false,false,batches).ns;}beforeNs.push_back(before);afterNs.push_back(after);if(after<before)++wins;pairedReductions.push_back((before-after)/before);ratios.push_back(after/before);}
    const double beforeMedian=median(beforeNs),candidateMedian=median(afterNs),pairedMedianReduction=median(pairedReductions),ratioMedian=median(ratios);
    return json{{"name",name},{"mode",mode==0?"full-initializer":mode==1?"initializer-plus-owned-message":mode==2?"initializer-plus-owned-envelope-dump":"initializer-plus-owned-emitter-dump"},{"campaign",campaign},{"rounds",rounds},{"beforeMedianNs",beforeMedian},{"candidateMedianNs",candidateMedian},{"relativeReduction",(beforeMedian-candidateMedian)/beforeMedian},{"pairedFasterCount",wins},{"pairedFasterRequired",10},{"pairedMedianReduction",pairedMedianReduction},{"ratioMedian",ratioMedian},{"ratioMedianReduction",1.0-ratioMedian},{"ratioOfMediansReduction",(beforeMedian-candidateMedian)/beforeMedian},{"beforeAllocationRequestsPerEvent",beforeCount.calls},{"candidateAllocationRequestsPerEvent",candidateCount.calls},{"beforeAllocationBytesPerEvent",beforeCount.bytes},{"candidateAllocationBytesPerEvent",candidateCount.bytes},{"beforeSamplesNs",beforeNs},{"candidateSamplesNs",afterNs},{"pairedReductions",pairedReductions},{"setupExcluded",true},{"destructorIncluded",true},{"realJsonCpp",true}};
}
static json runBenchmark(const json&sample,int campaign,int batches){
    const std::array<RowSpec,11>specs={{
        {"actual-schema-no-receipt-full-initializer",0,0,false,false},{"actual-schema-receipt-full-initializer",0,1,false,false},{"wide-inert-schema-full-initializer",0,2,false,false},
        {"actual-schema-no-receipt-owned-message",1,0,false,false},{"actual-schema-receipt-owned-message",1,1,false,false},{"wide-inert-schema-owned-message",1,2,false,false},
        {"actual-schema-receipt-owned-envelope-dump",2,1,true,false},{"wide-inert-schema-owned-envelope-dump",2,2,true,false},{"wide-typed-schema-owned-envelope-dump",2,3,true,false},
        {"wide-inert-schema-owned-emitter-dump",3,2,true,true},{"wide-typed-schema-owned-emitter-dump",3,3,true,true}}};
    json rows=json::array();for(const auto&spec:specs){auto f=makeRow(sample,spec);rows.push_back(benchmarkRow(f,spec.name,spec.mode,campaign,batches));}
    return json{{"campaign",campaign},{"rounds",11},{"rowCount",rows.size()},{"rows",rows},{"criteria","Every row: pairedFasterCount>=10/11, pairedMedianReduction>=0.05, ratioMedianReduction>=0.05, candidate allocation requests and bytes lower."}};
}
static int runCorrectness(const json&sample){
    Fixture base;base.hostFrame=hostFrameFixture(sample,true);equivalent(base);wrapperEquivalent(base,false);wrapperEquivalent(base,true);
    Fixture empty=base;empty.hostFrame=json::object();equivalent(empty);wrapperEquivalent(empty,true);
    const std::vector<json>nonObjects={nullptr,json::array(),json::array({1,"x"}),true,42,std::uint64_t(42),.5,"text",json::binary({1,2,3}),json(json::value_t::discarded)};for(const auto&host:nonObjects){Fixture f=base;f.hostFrame=host;equivalent(f);}
    for(const auto&bad:{json(nullptr),json(false),json(1),json::array({1}),json::object()}){Fixture f=base;f.hostFrame["motionAdmission"]=bad;equivalent(f);}
    for(const char*key:{"leftStick","rightStick","motionContribution","layoutContribution","motionManager","layoutManager","latency"})for(const auto&value:{json(nullptr),json(7),json::array({false,"任意"}),json::object({{"all",true}})}){Fixture f=base;f.hostFrame[key]=value;equivalent(f);}
    for(const std::string&target:{std::string("left"),std::string("right"),std::string("custom"),std::string("微星\n\"\\\t"),std::string(160,'s')}){Fixture f=base;f.g_realStickTest.targetStick=target;equivalent(f);}
    for(double d:{-0.,0.,std::numeric_limits<double>::denorm_min(),std::numeric_limits<double>::max(),std::numeric_limits<double>::lowest(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN(),std::bit_cast<double>(0x7ff8000000000055ULL)}){Fixture f=base;f.cgx=f.cgy=f.cgz=f.maxa=f.ax=d;f.psx=f.gravityX=f.orientationW=static_cast<float>(d);f.hostFrame["floating-boundary"]=d;f.hostFrame["leftStick"]={{"x",d},{"y",-d}};f.gamepadMotionPlaneProven=true;equivalent(f);}
    for(const char*field:{"host","timestamp","target"}){Fixture f=base;const std::string invalid("\xff\xc0\xaf",3);if(std::string(field)=="host")f.hostFrame["invalid-utf8"]=invalid;else if(std::string(field)=="timestamp")f.telemetryTimestampUtc=invalid;else f.g_realStickTest.targetStick=invalid;equivalent(f);}
    Fixture typed=base;typed.hostFrame["all-types"]={{"i",std::numeric_limits<std::int64_t>::min()},{"u",std::numeric_limits<std::uint64_t>::max()},{"binary",json::binary({0,127,255},42)},{"text","微星\n\"\\\t"},{"null",nullptr},{"bool",false},{"nested",json::array({{},1,-0.,"UTF8"})}};typed.hostFrame["imu"]={{"admitted",true},{"gyro",sample.at("imu")},{"quat",sample.at("imu").at("orientation")}};typed.hostFrame["touchpads"]={{"left",{{"active",true},{"click",true},{"x",-32768},{"y",32767},{"pressure",32767}}},{"right",{{"active",false},{"click",false},{"x",0},{"y",0},{"pressure",0}}}};typed.hostFrame["screenButtons"]=63;typed.hostFrame["backButtons"]=3;typed.gamepadMotionPlaneProven=true;equivalent(typed);wrapperEquivalent(typed,true);
    for(int mode=0;mode<8;++mode){Fixture f=base;f.postSuccess=(mode&1)==0;f.g_exitRequested.value=(mode&2)!=0;f.consumerAllowed=(mode&4)==0;wrapperEquivalent(f,true);}for(int mode=0;mode<4;++mode){Fixture f=base;f.g_view=(mode&1)!=0;f.costEnabled=(mode&2)!=0;f.g_children={{2,&childA},{4,&childB}};wrapperEquivalent(f,true);}
    {Fixture a=base,b=base;a.g_hwnd=b.g_hwnd=false;a.hostFrame=b.hostFrame=json::array({1});a.beforeGated();b.candidateGated();check(a.sensorSnapshots==0&&b.sensorSnapshots==0&&a.postCalls==0&&b.postCalls==0,"Closed outer UI gate built telemetry");check(sameRepresentation(a.hostFrame,b.hostFrame),"Closed gate consumed source");}
    {Fixture a=base,b=base;a.g_inputCaptureLastUi=b.g_inputCaptureLastUi=a.now-79;a.beforeGated();b.candidateGated();check(a.sensorSnapshots==0&&b.sensorSnapshots==0,"79ms UI cadence gate built event");check(!b.hostFrame.is_null(),"Cadence gate moved source");}
    {Fixture a=base,b=base;a.g_inputCaptureLastUi=b.g_inputCaptureLastUi=a.now-80;a.beforeGated();b.candidateGated();check(a.sensorSnapshots==1&&b.sensorSnapshots==1&&a.g_inputCaptureLastUi==b.g_inputCaptureLastUi,"80ms telemetry gate scheduling changed");check(sameRepresentation(a.posted->data,b.posted->data),"80ms gate payload differs");}
    {Fixture f=base;f.dropWindowDuringSnapshot=true;wrapperEquivalent(f,true);}
    std::mt19937_64 rng(0x20261015);for(unsigned i=0;i<2000;++i){Fixture f=base;f.now=rng();f.sensorPresent=i&1;f.gamepadMotionPlaneProven=i&2;f.motionPlaneSteeringYaw=i&4;f.g_realStickTest.targetStick=i%2?"left":"right";f.g_realStickTest.motionTriggerResolved=i&8;f.g_realStickTest.aimingSightsTriggerResolved=i&16;f.cgx=std::ldexp(static_cast<double>(static_cast<std::int32_t>(rng())),-23);f.cgy=-f.cgx;f.cgz=f.cgx/3;f.orientationW=static_cast<float>(f.cgx);f.psx=static_cast<float>(f.cgy);f.gravityX=static_cast<float>(f.cgz);f.motionPlaneGyroMultiplier=static_cast<float>(static_cast<int>(i%11)-5);f.motionPlaneAccelMultiplier=static_cast<float>(i%13);f.hostFrame["random-extra"]={{"sequence",rng()},{"signed",static_cast<std::int64_t>(rng())},{"text",std::string(i%96,'s')},{"imu",sample.at("imu")},{"nullable",nullptr},{"array",json::array({i,true,"微星"})}};if(i%5==0)f.hostFrame.erase("leftStick");if(i%7==0)f.hostFrame.erase("motionManager");if(i%11==0)f.hostFrame["motionAdmission"]=false;equivalent(f);}
    std::atomic<bool>concurrentOK{true};std::vector<std::thread>threads;for(unsigned t=0;t<4;++t)threads.emplace_back([&,t]{try{for(unsigned i=0;i<300;++i){Fixture f=typed;f.now=t*1000+i;f.costEnabled=i%2;f.gamepadMotionPlaneProven=i%3;equivalent(f);wrapperEquivalent(f,true);}}catch(...){concurrentOK=false;}});for(auto&thread:threads)thread.join();check(concurrentOK,"Concurrent independent fixture mismatch");return 0;
}
int main(int argc,char**argv){
    try{
        if(argc!=3&&argc!=5&&argc!=6)throw std::runtime_error("Usage: fixture.exe SAMPLE.json correctness | benchmark CAMPAIGN BATCHES | fault before|candidate FAULT_INDEX");
        std::ifstream input(argv[1]);if(!input)throw std::runtime_error("sample open failed");json sample;input>>sample;const std::string command=argv[2];
        if(command=="correctness"){runCorrectness(sample);std::cout<<json{{"pass",true},{"assertions",assertions.load()},{"randomCases",2000},{"concurrentCases",1200},{"utf8ExceptionsCovered",true},{"productionModified",false},{"productMainHostDeviceExecuted",false},{"wholeProcessClaim",false}}.dump(2)<<'\n';return 0;}
        if(command=="benchmark"){if(argc!=5)throw std::runtime_error("benchmark requires campaign and batches");std::cout<<runBenchmark(sample,std::stoi(argv[3]),std::stoi(argv[4])).dump(2)<<'\n';return 0;}
        if(command=="fault"){
            if(argc!=5)throw std::runtime_error("fault requires variant and index");_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
#ifdef _DEBUG
            for(int report:{_CRT_ASSERT,_CRT_ERROR,_CRT_WARN}){_CrtSetReportMode(report,_CRTDBG_MODE_FILE);_CrtSetReportFile(report,_CRTDBG_FILE_STDERR);}
#endif
            const bool candidate=std::string(argv[3])=="candidate";const auto index=std::stoll(argv[4]);bool failed=false;std::uint64_t eventAllocations=0;liveBalance=0;liveEnabled=true;
            {Fixture f;f.hostFrame=hostFrameFixture(sample,true);failAfter=index;count={};countEnabled=true;try{(f.*(candidate?&Fixture::candidatePost:&Fixture::beforePost))();}catch(const std::bad_alloc&){failed=true;}failAfter=-1;countEnabled=false;eventAllocations=count.calls;}
            liveEnabled=false;std::cout<<json{{"pass",true},{"variant",candidate?"candidate":"before"},{"faultIndex",index},{"badAllocCaught",failed},{"eventAllocationCalls",eventAllocations},{"liveAllocationBalance",liveBalance}}.dump()<<'\n';return 0;
        }
        throw std::runtime_error("unknown command");
    }catch(const std::exception&e){countEnabled=false;liveEnabled=false;failAfter=-1;std::cerr<<e.what()<<'\n';return 3;}
}
