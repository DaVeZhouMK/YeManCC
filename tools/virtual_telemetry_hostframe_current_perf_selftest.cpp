// R18 isolated current-source hostFrame ownership fixture.
// The Python runner extracts the current production block before compiling this fixture.
// No native main, Host, Windows messages, WebView, SDK, devices or registry run.
#define NOMINMAX
#include <algorithm>
#include <array>
#include <atomic>
#include <thread>
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
#if defined(YMCC_TYPED_TELEMETRY_BUILD)
#include "ipc_result_mailbox.h"
#include "gyro_telemetry_record.h"
#else // YMCC_TYPED_TELEMETRY_BUILD
#include "ipc_result_mailbox.h"
#endif // YMCC_TYPED_TELEMETRY_BUILD
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
#ifdef YMCC_CURRENT_TREE_BUILD
#include <Windows.h>
#include <memory>
#include <optional>
#include <utility>
#include "controller-persona.h"

// Reuse the historical allocator counters and exact JSON comparator above.
void* operator new(std::size_t n,const std::nothrow_t&) noexcept {try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&) noexcept {try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&) noexcept {::operator delete(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept {::operator delete[](p);}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept {try{return ::operator new(n,a);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&) noexcept {try{return ::operator new[](n,a);}catch(...){return nullptr;}}
void operator delete(void* p,std::align_val_t a,const std::nothrow_t&) noexcept {::operator delete(p,a);}
void operator delete[](void* p,std::align_val_t a,const std::nothrow_t&) noexcept {::operator delete[](p,a);}

namespace current_tree {
struct BoolLatch {bool value=false;bool load(std::memory_order)const noexcept{return value;}};
struct Counter {std::uint64_t value=0;std::uint64_t fetch_add(std::uint64_t n,std::memory_order){const auto old=value;value+=n;return old;}};
struct UiCostIpcScope {explicit UiCostIpcScope(const char*){}};
struct UiCostSerialScope {explicit UiCostSerialScope(const char*){}};
struct ChildView {bool view=true;};
static ChildView childA,childB;
static constexpr UINT WM_GAMEPAD_SERIAL_EVENT=WM_USER+16;
static constexpr int kTickCostUiTelemetry=1;
#include "tree-support-extracted.h"
#ifdef YMCC_SERIAL_ACTION_POWER
static_assert(std::is_same_v<decltype(GamepadSerialEvent::actionPower),unsigned long long>);
static_assert(std::is_same_v<decltype(g_gamepadSerialActionPower),unsigned long long>);
#endif

struct Input {
    struct Config {std::string targetStick="right";bool motionTriggerResolved=true,aimingSightsTriggerResolved=true;}g_realStickTest;
    std::uint64_t now=258671,g_inputCaptureLastUi=0;
    UINT64 gyroTimestamp100ns=133700000000000001ULL,accelTimestamp100ns=133700000000000002ULL;
    double cgx=.29897409677505493,cgy=.019747555255889893,cgz=.1081552505493164;
    double maxa=.015371999703347683,may=.9323239922523499,maz=-.40796801447868347;
    double ax=.015371999703347683,ay=.40796801447868347,az=.9323239922523499;
    float psx=.125f,psy=-.25f,wsx=.5f,wsy=-.75f;
    float gravityX=-.004524147603660822f,gravityY=-.8954063057899475f,gravityZ=.44507360458374023f;
    float linearAccelX=.01f,linearAccelY=-.02f,linearAccelZ=.03f;
    float orientationW=-.020204337313771248f,orientationX=.03335440903902054f,orientationY=-.852171778678894f,orientationZ=.521807074546814f;
    bool gamepadMotionPlaneProven=false,motionPlaneSteeringYaw=true;
    float motionPlaneGyroMultiplier=1,motionPlaneAccelMultiplier=1;
    std::string fixtureTimestampUtc="2026-11-08T12:00:00.0000002Z";
    std::shared_ptr<const json> sharedHost;
    std::shared_ptr<const json> retainedInactiveSource;
    const json* hostFrameView=nullptr;
    bool g_hwnd=true,g_view=true,postSuccess=true,costEnabled=false,sensorPresent=true;
    bool snapshotEdge=false,dropWindowDuringSnapshot=false,exitRequested=false;
    unsigned childrenMask=0;
    void own(json value){retainedInactiveSource.reset();sharedHost=std::make_shared<const json>(std::move(value));hostFrameView=sharedHost.get();}
    void borrow(const json&value){retainedInactiveSource.reset();sharedHost.reset();hostFrameView=&value;}
};

struct Fixture:Input {
    BoolLatch g_exitRequested;
    Counter g_c17aOwnedEvents,g_c17aParityChecks,g_c17aParityFailures;
    std::vector<std::pair<int,ChildView*>> g_children;
    GamepadSerialEvent* posted=nullptr;
#if defined(YMCC_TYPED_TELEMETRY_BUILD)
    std::uintptr_t postedToken=0;
    bool holdPostedToken=false;
#else // YMCC_TYPED_TELEMETRY_BUILD
    std::uintptr_t postedToken=0;
#endif // YMCC_TYPED_TELEMETRY_BUILD
    unsigned sensorSnapshots=0,postCalls=0,timestampCalls=0,costCalls=0,rendererLookups=0;
    UINT64 lastTimestamp100ns=0;
    std::vector<std::pair<int,std::wstring>> delivered;
    struct Renderer {
        Fixture* owner=nullptr;int id=0;
        HRESULT PostWebMessageAsJson(const wchar_t*wire){owner->delivered.emplace_back(id,wire);return S_OK;}
    }renderer;
    explicit Fixture(const Input&input):Input(input){
        if(!hostFrameView)throw std::runtime_error("missing immutable Host fixture input");
        g_exitRequested.value=exitRequested;
        if(childrenMask&1)g_children.emplace_back(2,&childA);
        if(childrenMask&2)g_children.emplace_back(4,&childB);
    }
    Fixture(const Fixture&)=delete;Fixture&operator=(const Fixture&)=delete;
    ~Fixture(){
#ifdef YMCC_SERIAL_EVENT_MAILBOX
        if(postedToken)(void)g_gamepadSerialEventMailbox->take(postedToken);
#endif
        delete posted;
    }
    // Explicit in-memory power port for the actual serial case.
    // The extracted producer TLS stays zero; no Host/JSON field seeds it.
    unsigned long long fixturePowerGeneration=1;
    unsigned long long currentPowerGeneration()const noexcept{return fixturePowerGeneration;}
    bool tickCostEnabled()const{return costEnabled;}
    bool inputCaptureGyroPresentSnapshot(){
        ++sensorSnapshots;
        // An explicit seam makes the sole sensor expression observable. It is
        // not the product sensor provider/lock and is not a device measurement.
        if(snapshotEdge){
            now=17;fixtureTimestampUtc="snapshot-edge";
            cgx=17.25;cgy=-19.5;cgz=21.75;
            g_realStickTest.targetStick=g_realStickTest.targetStick=="left"?"right":"left";
        }
        if(dropWindowDuringSnapshot)g_hwnd=false;
        return sensorPresent;
    }
    std::string sgFormatIsoUtcFileTime(UINT64 stamp){++timestampCalls;lastTimestamp100ns=stamp;return fixtureTimestampUtc;}
    unsigned long long tickCostElapsedUs(std::chrono::steady_clock::time_point)const{return 0;}
    void tickCostAccumulate(int,unsigned long long){++costCalls;}
    bool PostMessageW(bool,UINT,int,LPARAM value){
        ++postCalls;
        if(postSuccess){
            if(posted||postedToken)throw std::runtime_error("fixture message slot already occupied");
#ifdef YMCC_SERIAL_EVENT_MAILBOX
            postedToken=static_cast<std::uintptr_t>(value);
#else
            posted=reinterpret_cast<GamepadSerialEvent*>(value);
#endif
        }
        return postSuccess;
    }
    Renderer* ipc_view_for_child(int id){
        ++rendererLookups;
        if((id==0&&!g_view)||(id==2&&!(childrenMask&1))||(id==4&&!(childrenMask&2)))return nullptr;
        renderer.owner=this;renderer.id=id;return &renderer;
    }
    void appendNativeLifecycleLog(const char*,const json&){throw std::runtime_error("actual owned parity guard failed");}
    void ipc_emit(const std::string&,const json&){throw std::runtime_error("unexpected generic event");}
#include "tree-wrappers-extracted.h"
#include "tree-methods-extracted.h"
#include "tree-dispatch-extracted.h"
    void capturePosted(){
#ifdef YMCC_SERIAL_EVENT_MAILBOX
#if defined(YMCC_TYPED_TELEMETRY_BUILD)
        if(holdPostedToken||!postedToken)return;
#else // YMCC_TYPED_TELEMETRY_BUILD
        if(!postedToken)return;
#endif // YMCC_TYPED_TELEMETRY_BUILD
        const auto token=postedToken;postedToken=0;
        auto event=g_gamepadSerialEventMailbox->take(token);
        if(posted)throw std::runtime_error("fixture message slot already occupied");
        posted=event.release();
#endif
    }
    void consumePosted(){
        auto* event=posted;posted=nullptr;
        // Actual take already ran after post returned, before payload observations.
#ifdef YMCC_SERIAL_EVENT_MAILBOX
        (void)dispatchSerial(std::unique_ptr<GamepadSerialEvent>(event));
#else
        (void)dispatchSerial(reinterpret_cast<LPARAM>(event));
#endif
    }
};
#if defined(YMCC_TYPED_TELEMETRY_BUILD)
static json postedValue(const GamepadSerialEvent& event){return event.gyroTelemetry?event.gyroTelemetry->tree():event.data;}
using Builder=json(Fixture::*)();
#else // YMCC_TYPED_TELEMETRY_BUILD
using Builder=json(Fixture::*)();
#endif // YMCC_TYPED_TELEMETRY_BUILD
struct Outcome {json value;int id=0;std::string kind,message;};
static std::uint64_t cases=0;
static Outcome build(Fixture&f,Builder method){
    try{return{(f.*method)(),0,{},{}};}
    catch(const json::exception&e){return{nullptr,e.id,typeid(e).name(),e.what()};}
    catch(const std::exception&e){return{nullptr,0,typeid(e).name(),e.what()};}
}
static Outcome dump(const json&value,json::error_handler_t handler){
    try{return{value.dump(-1,' ',false,handler),0,{},{}};}
    catch(const json::exception&e){return{nullptr,e.id,typeid(e).name(),e.what()};}
}
static void equivalent(const Input&input){
    ++cases;Fixture a(input),b(input);
    const auto* source=input.hostFrameView;const json original=*source;
    const auto useCount=input.sharedHost.use_count();
    const auto before=build(a,&Fixture::beforeInitializer),after=build(b,&Fixture::candidateInitializer);
    check(before.id==after.id&&before.kind==after.kind&&before.message==after.message,"initializer exception identity");
    check(sameRepresentation(*source,original),"read-only/shared Host input changed");
    check(a.hostFrameView==source&&b.hostFrameView==source&&input.sharedHost.use_count()==useCount,"Host reference/shared ownership changed");
    check(a.sensorSnapshots==1&&b.sensorSnapshots==1,"sensor snapshot call count or point changed");
    if(!before.kind.empty())return;
    check(before.value.is_object()&&before.value.size()==27&&after.value.is_object()&&after.value.size()==27,"complete telemetry schema");
    check(sameRepresentation(before.value,after.value),"full telemetry types/values/float-bits");
    check(sameRepresentation(after.value.at("hostFrame"),original),"complete Host payload missing");
    check(after.value.at("schemaVersion").is_number_integer()&&!after.value.at("schemaVersion").is_number_unsigned(),"schema signed integer type");
    check(after.value.at("sequence").is_number_unsigned(),"sequence unsigned integer type");
    check(after.value.at("sensorPresent").is_boolean()&&after.value.at("sensorPresent")==input.sensorPresent,"sensor value/boolean type");
    if(input.snapshotEdge){
        check(after.value.at("sequence")==input.now&&after.value.at("timestampUtc")==input.fixtureTimestampUtc,"snapshot moved before prefix evaluation");
        check(after.value.at("gyro").at("x")==17.25,"snapshot moved after gyro evaluation");
        check(after.value.at("targetStick")!=input.g_realStickTest.targetStick,"snapshot moved after target evaluation");
    }
    check(after.value.at("gamepadMotionPlane").empty()==!input.gamepadMotionPlaneProven,"motion-plane admission state");
    if(input.gamepadMotionPlaneProven){
        check(after.value.at("gamepadMotionPlane").at("steeringAxis")==(input.motionPlaneSteeringYaw?"yaw":"roll"),"motion-plane yaw/roll state");
    }
    if(source->is_object()){
        const auto left=source->value("leftStick",json{{"x",0.0},{"y",0.0}});
        const auto right=source->value("rightStick",json{{"x",0.0},{"y",0.0}});
        const auto target=input.snapshotEdge?(input.g_realStickTest.targetStick=="left"?"right":"left"):input.g_realStickTest.targetStick;
        check(sameRepresentation(after.value.at("leftStick"),left)&&sameRepresentation(after.value.at("rightStick"),right),"both stick values");
        check(sameRepresentation(after.value.at("output"),target=="left"?left:right),"left/right output selector");
    }
    for(const auto handler:{json::error_handler_t::strict,json::error_handler_t::replace,json::error_handler_t::ignore}){
        const auto x=dump(before.value,handler),y=dump(after.value,handler);
        check(x.id==y.id&&x.kind==y.kind&&x.message==y.message,"dump exception identity");
        if(x.kind.empty())check(x.value==y.value,"complete dump bytes");
    }
}
static std::wstring wide(const std::string&value){
    const int n=MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0);
    std::wstring result(n,L'\0');
    check(n>0&&MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),n)==n,"UTF8 conversion seam oracle");
    return result;
}
static void wrapperEquivalent(const Input&input,bool exitAtDelivery=false){
    ++cases;Fixture a(input),b(input);
    const json original=*input.hostFrameView;
    const auto uses=input.sharedHost.use_count();
    a.beforePost();b.candidatePost();
#if defined(YMCC_TYPED_TELEMETRY_BUILD)
    check(sameRepresentation(*input.hostFrameView,original)&&input.sharedHost.use_count()==uses+(b.posted&&b.posted->gyroTelemetry&&b.posted->gyroTelemetry->immutableHostFrame?1:0),"owned post retains exactly one immutable Host only for a queued typed record");
#else // YMCC_TYPED_TELEMETRY_BUILD
    check(sameRepresentation(*input.hostFrameView,original)&&input.sharedHost.use_count()==uses,"owned post consumed shared Host");
#endif // YMCC_TYPED_TELEMETRY_BUILD
    check(a.sensorSnapshots==1&&b.sensorSnapshots==1,"owned post snapshot count");
    check(a.postCalls==b.postCalls&&bool(a.posted)==bool(b.posted),"actual owned post route");
    if(a.posted){
        check(a.posted->event=="gyro.telemetry"&&b.posted->event==a.posted->event,"actual event name");
#if defined(YMCC_TYPED_TELEMETRY_BUILD)
        check(sameRepresentation(a.posted->data,postedValue(*b.posted)),"actual posted data types/values");
#else // YMCC_TYPED_TELEMETRY_BUILD
        check(sameRepresentation(a.posted->data,b.posted->data),"actual posted data types/values");
#endif // YMCC_TYPED_TELEMETRY_BUILD
#if defined(YMCC_TYPED_TELEMETRY_BUILD)
        check(objectAddress(a.posted->data.at("hostFrame"))!=objectAddress(*input.hostFrameView),"event owns a copy of shared Host");
        check(b.posted->gyroTelemetry&&b.posted->data.is_null(),"actual typed post owns record without JSON adapter");
        if(b.posted->gyroTelemetry->immutableHostFrame)
            check(b.posted->gyroTelemetry->immutableHostFrame==input.retainedInactiveSource&&&b.posted->gyroTelemetry->hostFrameValue()==input.hostFrameView,"typed event owns complete immutable Host lifetime");
        else check(objectAddress(b.posted->gyroTelemetry->hostFrame)!=objectAddress(*input.hostFrameView),"typed event owns complete independent Host");
#else // YMCC_TYPED_TELEMETRY_BUILD
        check(objectAddress(a.posted->data.at("hostFrame"))!=objectAddress(*input.hostFrameView),"event owns a copy of shared Host");
#endif // YMCC_TYPED_TELEMETRY_BUILD
    }
    std::optional<std::wstring> expected;
    if(a.posted&&!exitAtDelivery&&(input.g_view||input.childrenMask)){
        expected=wide(json{{"event",a.posted->event},{"data",a.posted->data}}.dump());
    }
    if(exitAtDelivery)a.g_exitRequested.value=b.g_exitRequested.value=true;
    a.consumePosted();b.consumePosted();
    check(!a.posted&&!b.posted,"serial case event slot not consumed");
    check(a.delivered==b.delivered&&a.rendererLookups==b.rendererLookups,"actual broadcast UTF16 bytes/order");
    check(a.g_c17aParityFailures.value==0&&b.g_c17aParityFailures.value==0,"actual diagnostic parity failure");
    if(expected){
        std::vector<int> ids;if(input.g_view)ids.push_back(0);if(input.childrenMask&1)ids.push_back(2);if(input.childrenMask&2)ids.push_back(4);
        check(a.delivered.size()==ids.size(),"actual renderer count");
        for(std::size_t i=0;i<ids.size();++i)check(a.delivered[i].first==ids[i]&&a.delivered[i].second==*expected,"complete envelope/reference renderer bytes");
    }else check(a.delivered.empty(),"delivery occurred after stop/absent target");
    check(sameRepresentation(*input.hostFrameView,original)&&input.sharedHost.use_count()==uses,"emitter consumed shared Host");
}
static Input active(const json&hosts,const char*persona,bool plane=false,bool yaw=true){
    check(ymcc::controllerPersona::currentOutput(persona),"non-current fixture output persona");
    Input input;input.own(hosts.at(persona));input.gamepadMotionPlaneProven=plane;input.motionPlaneSteeringYaw=yaw;return input;
}
static Input inactive(const std::string&persona="disabled",const std::string&target="right",const std::string&state="none",bool hostActive=false){
    Input input;input.g_realStickTest.targetStick=target;
#if defined(YMCC_TYPED_TELEMETRY_BUILD)
    input.sharedHost=inputCaptureInactiveHostFrameOwner(hostActive,state,persona,target);
    input.hostFrameView=input.sharedHost.get();input.retainedInactiveSource=input.sharedHost;
#else // YMCC_TYPED_TELEMETRY_BUILD
    input.borrow(inputCaptureInactiveHostFrame(hostActive,state,persona,target));
#endif // YMCC_TYPED_TELEMETRY_BUILD
    return input;
}
static void gateCases(const Input&input){
    for(const auto age:{0ULL,79ULL,80ULL,81ULL}){
        ++cases;Fixture a(input),b(input);a.g_inputCaptureLastUi=b.g_inputCaptureLastUi=input.now-age;
        a.beforeGated();b.candidateGated();const bool due=input.g_hwnd&&age>=80;
        check(a.sensorSnapshots==(due?1u:0u)&&b.sensorSnapshots==a.sensorSnapshots,"actual 80ms sensor gate");
        check(a.timestampCalls==(due?1u:0u)&&b.timestampCalls==a.timestampCalls,"actual timestamp gate");
        check(a.g_inputCaptureLastUi==b.g_inputCaptureLastUi&&a.g_inputCaptureLastUi==(due?input.now:input.now-age),"actual cadence update");
        if(due)check(a.lastTimestamp100ns==(std::max)(input.gyroTimestamp100ns,input.accelTimestamp100ns)&&a.lastTimestamp100ns==b.lastTimestamp100ns,"newest sensor timestamp selection");
#if defined(YMCC_TYPED_TELEMETRY_BUILD)
        if(a.posted)check(b.posted&&sameRepresentation(a.posted->data,postedValue(*b.posted)),"gated complete event");
#else // YMCC_TYPED_TELEMETRY_BUILD
        if(a.posted)check(b.posted&&sameRepresentation(a.posted->data,b.posted->data),"gated complete event");
#endif // YMCC_TYPED_TELEMETRY_BUILD
        a.consumePosted();b.consumePosted();check(a.delivered==b.delivered,"gated complete envelope");
        if(due){
            // Same tick and 79ms repeat do no work; the next 80ms tick does.
            for(const auto delta:{0ULL,79ULL,80ULL}){
                a.now=b.now=input.now+delta;a.beforeGated();b.candidateGated();
                check(a.sensorSnapshots==b.sensorSnapshots,"repeat/edge snapshot parity");
                check(a.sensorSnapshots==(delta<80?1u:2u),"unchanged cadence repeat created work");
                a.consumePosted();b.consumePosted();
            }
        }
    }
}
#if defined(YMCC_TYPED_TELEMETRY_BUILD)
#include "fixtures/virtual_sample_tree/telemetry_typed/lifecycle.inc"

#endif // YMCC_TYPED_TELEMETRY_BUILD
#if defined(YMCC_TYPED_TELEMETRY_BUILD)


static void immutableFieldRepresentations() {
    const std::vector<json> values={nullptr,true,1,std::uint64_t(1),1.0,-0.0,0.0,
        std::numeric_limits<double>::denorm_min(),std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN(),std::string("invalid-\xff",9),
        json::array({nullptr,1,1.0,-0.0}),json::binary({0,1,255}),json::object()};
    const auto wireOutcome=[](const ymcc::gyro_telemetry::Record& r,json::error_handler_t handler) -> Outcome {
        try{return{r.envelope(handler),0,{},{}};}
        catch(const json::exception& e){return{nullptr,e.id,typeid(e).name(),e.what()};}
    };
    for(const char* field:{"leftStick","rightStick","motionContribution","layoutContribution","motionManager","layoutManager","latency"}) {
        for(const auto& value:values) {
            Input input;input.g_realStickTest.targetStick="right";
            json host={{"leftStick",{{"x",-0.0},{"y",1}}},{"rightStick",{{"x",2u},{"y",0.5}}}};
            host[field]=value;input.own(std::move(host));input.retainedInactiveSource=input.sharedHost;
            Fixture before(input),candidate(input);const auto expected=build(before,&Fixture::beforeInitializer);
            auto actual=candidate.candidateRecord();const auto tree=actual.tree();
            check(expected.kind.empty()&&sameRepresentation(expected.value,tree),"each immutable subtree preserves independent oracle type/float/binary values");
            for(const auto handler:{json::error_handler_t::strict,json::error_handler_t::replace,json::error_handler_t::ignore}) {
                const auto original=dump(json{{"event","gyro.telemetry"},{"data",expected.value}},handler);
                const auto retained=wireOutcome(actual,handler);
                check(original.id==retained.id&&original.kind==retained.kind&&original.message==retained.message&&sameRepresentation(original.value,retained.value),"immutable subtree wire matches independent JSON serializer for all original error handlers");
            }
        }
    }
    for(const char* target:{"left","right","not-a-stick"}) {
        Input input;input.g_realStickTest.targetStick=target;input.own(json::object());input.retainedInactiveSource=input.sharedHost;
        Fixture a(input),b(input);const auto expected=build(a,&Fixture::beforeInitializer);auto r=b.candidateRecord();
        check(sameRepresentation(expected.value,r.tree()),"all missing immutable fields keep original literal fallback types and non-left selector");
        check(r.leftStickValue().at("x").is_number_float()&&r.motionManagerValue().is_object(),"immutable default axes and managers retain exact schema types");
    }
    Input source=inactive("disabled","right","cross-thread-owner",false);Fixture producer(source);auto queued=producer.candidateRecord();
    const json expected=queued.tree();std::weak_ptr<const json> previous=queued.immutableHostFrame;
    source.own(json::object());producer.retainedInactiveSource.reset();producer.sharedHost.reset();
    (void)inputCaptureInactiveHostFrameOwner(false,"next-owner","disabled","left");
    bool threadOkay=false;
    std::thread consumer([record=std::move(queued),&threadOkay,&expected]() mutable {
        (void)inputCaptureInactiveHostFrameOwner(true,"consumer-cache","steamdeck","left");
        threadOkay=record.hasImmutableHostFields()&&sameRepresentation(record.tree(),expected)&&json::parse(record.envelope()).at("data")==record.tree();
        auto copied=record;record={};threadOkay=threadOkay&&copied.hasImmutableHostFields()&&sameRepresentation(copied.tree(),expected);
        copied.immutableHostFrame.reset();threadOkay=threadOkay&&!copied.hasImmutableHostFields()&&copied.outputValue().is_null();
    });
    consumer.join();check(threadOkay&&previous.expired(),"immutable subtrees cross producer-consumer threads and return last owner without dangling views");
}

static void immutableInactiveLifetime() {
    immutableFieldRepresentations();
    ymcc::gyro_telemetry::Record pending;
    std::weak_ptr<const json> oldOwner;
    json expected;
    {
        auto input=inactive("disabled","right","immutable-lifetime",false);
        Fixture producer(input);pending=producer.candidateRecord();
        check(pending.immutableHostFrame&&pending.hostFrame.is_null(),"actual inactive producer retains immutable value instead of copying a tree");
        check(pending.hasImmutableHostFields()&&pending.output.is_null()&&pending.leftStick.is_null()&&pending.rightStick.is_null()&&
            pending.motionContribution.is_null()&&pending.layoutContribution.is_null()&&pending.motionManager.is_null()&&pending.layoutManager.is_null()&&pending.latency.is_null(),"actual inactive producer views all eight subtrees without owning duplicate trees");
        check(&pending.leftStickValue()==&pending.immutableHostFrame->at("leftStick")&&&pending.rightStickValue()==&pending.immutableHostFrame->at("rightStick"),"actual immutable views address their owning Host subtrees");
        expected=pending.hostFrameValue();oldOwner=pending.immutableHostFrame;
    }
    (void)inputCaptureInactiveHostFrameOwner(true,"other-owner","steamdeck","left");
    check(!oldOwner.expired()&&sameRepresentation(pending.hostFrameValue(),expected),"queued old telemetry survives new inactive cache ownership");
    check(sameRepresentation(pending.tree().at("hostFrame"),expected),"diagnostic oracle consumes retained Host");
    check(json::parse(pending.envelope()).at("data").at("hostFrame")==expected,"wire consumes retained old Host after cache replacement");
    auto moved=std::move(pending);
    check(!pending.hasImmutableHostFields()&&pending.outputValue().is_null(),"moved-from record has no dangling immutable views");
    pending=std::move(moved);
    const auto originalOutput=pending.outputValue();pending.targetStick="left";
    check(sameRepresentation(pending.outputValue(),originalOutput),"immutable output retains the original selector observation");
    pending={};check(oldOwner.expired(),"last queued record releases previous immutable Host");
    // Controlled immutable Host values exercise representation independently
    // of the product helper's fixed inactive literal schema. No source or
    // device authority is inferred from these synthetic views.
    for(const bool left:{false,true})for(const bool snapshotEdge:{false,true}) {
        Input controlled;controlled.g_realStickTest.targetStick=left?"left":"right";controlled.snapshotEdge=snapshotEdge;
        controlled.own(json{{"leftStick",{{"x",1},{"y",-0.0}}},{"rightStick",{{"x",2u},{"y",0.5}}},
            {"motionContribution",nullptr},{"layoutContribution",json::array({true,7})},
            {"motionManager",{{"finite",1.25},{"sign",-0.0}}},{"layoutManager",false},{"latency","retained-value"}});
        controlled.retainedInactiveSource=controlled.sharedHost;
        Fixture baseline(controlled),candidate(controlled);
        const auto expectedTree=build(baseline,&Fixture::beforeInitializer);
        const auto actualTree=build(candidate,&Fixture::candidateInitializer);
        check(expectedTree.kind==actualTree.kind&&expectedTree.id==actualTree.id&&expectedTree.message==actualTree.message&&sameRepresentation(expectedTree.value,actualTree.value),"controlled immutable fields retain complete source types/bits and snapshot evaluation");
        Fixture producer(controlled);auto retained=producer.candidateRecord();
        check(retained.hasImmutableHostFields()&&json::parse(retained.envelope()).at("data")==retained.tree(),"wire and tree consume the same complete immutable field views");
        const auto capturedOutput=retained.outputValue();retained.targetStick=retained.targetStick=="left"?"right":"left";
        check(sameRepresentation(retained.outputValue(),capturedOutput),"asymmetric output keeps captured selection independently of display target");
        controlled.own(json::object());check(!controlled.retainedInactiveSource,"replacing controlled Host clears obsolete retained source");
        controlled.retainedInactiveSource=retained.immutableHostFrame;controlled.borrow(retained.hostFrameValue());
        check(!controlled.retainedInactiveSource,"borrowing a new controlled Host clears obsolete retained source");
    }

}
#endif // YMCC_TYPED_TELEMETRY_BUILD
static void correctness(const json&hosts,bool negative){
#if defined(YMCC_TYPED_TELEMETRY_BUILD)
    immutableInactiveLifetime();
#endif // YMCC_TYPED_TELEMETRY_BUILD
    {auto input=inactive();equivalent(input);}
    if(negative)throw std::runtime_error("semantic mutation was not rejected");
    for(const char*persona:{"elite","steamdeck","dualsense-edge"}){
        for(bool plane:{false,true})for(bool yaw:{false,true})for(const char*target:{"left","right"}){
            auto input=active(hosts,persona,plane,yaw);input.g_realStickTest.targetStick=target;
            equivalent(input);wrapperEquivalent(input);gateCases(input);
            input.snapshotEdge=true;equivalent(input);
            input.snapshotEdge=false;input.own(hosts.at(persona));
            json edge=*input.hostFrameView;edge["firstFrame"]=false;edge["lifecycle"]="running";input.own(edge);equivalent(input);
            edge["hostSubmission"]="frame-skipped-context-retired";edge["hostActive"]=false;
            edge["sourceContextReason"]="fixture-retired";edge["focusIsolationSemanticNeutral"]=true;input.own(edge);equivalent(input);
        }
    }
    for(const char*persona:{"disabled","elite","steamdeck","dualsense-edge"})for(const char*target:{"left","right"}){
        for(const char*state:{"none","starting","active","fault","stopped"}){
            auto input=inactive(persona,target,state,std::string(state)=="active");
            const auto* address=input.hostFrameView;const auto* object=objectAddress(*address);const json before=*address;
            const auto&same=inputCaptureInactiveHostFrame(std::string(state)=="active",state,persona,target);
            check(&same==address&&objectAddress(same)==object&&sameRepresentation(before,same),"inactive cache unchanged-repeat ownership");
            equivalent(input);wrapperEquivalent(input);equivalent(input);
        }
    }
    for(const auto&value:std::vector<json>{nullptr,json::array(),json::array({1,"x"}),true,42,std::uint64_t(42),.5,"text",json::binary({1,2,3}),json(json::value_t::discarded)}){
        auto input=active(hosts,"steamdeck");input.own(value);equivalent(input);
    }
    for(const char*key:{"leftStick","rightStick","motionAdmission","motionContribution","layoutContribution","motionManager","layoutManager","latency"}){
        for(const auto&value:std::vector<json>{nullptr,false,7,json::array({false,"任意"}),json::object({{"all",true}})}){
            auto input=active(hosts,"steamdeck");json host=*input.hostFrameView;host[key]=value;input.own(host);equivalent(input);
        }
        auto input=active(hosts,"steamdeck");json host=*input.hostFrameView;host.erase(key);input.own(host);equivalent(input);
    }
    for(const auto bits:{0ULL,0x8000000000000000ULL,1ULL,0x7fefffffffffffffULL,0xffefffffffffffffULL,0x7ff0000000000000ULL,0xfff0000000000000ULL,0x7ff8000000000001ULL,0xfff8000000000055ULL}){
        auto input=active(hosts,"dualsense-edge",true);const double value=std::bit_cast<double>(bits);
        input.cgx=input.cgy=input.cgz=input.maxa=input.may=input.maz=input.ax=input.ay=input.az=value;
        json host=*input.hostFrameView;host["allFloatBits"]=value;host["leftStick"]={{"x",value},{"y",-value}};input.own(host);equivalent(input);
    }
    for(const auto bits:{0U,0x80000000U,1U,0x7f7fffffU,0xff7fffffU,0x7f800000U,0xff800000U,0x7fc00001U,0xffc00055U}){
        auto input=active(hosts,"steamdeck",true,false);const float value=std::bit_cast<float>(bits);
        input.psx=input.psy=input.wsx=input.wsy=value;
        input.gravityX=input.gravityY=input.gravityZ=value;input.linearAccelX=input.linearAccelY=input.linearAccelZ=value;
        input.orientationW=input.orientationX=input.orientationY=input.orientationZ=value;
        input.motionPlaneGyroMultiplier=input.motionPlaneAccelMultiplier=value;equivalent(input);
    }
    for(const char*field:{"host","timestamp","target"}){
        auto input=active(hosts,"steamdeck");const std::string invalid("\xff\xc0\xaf",3);
        if(std::string(field)=="host"){json host=*input.hostFrameView;host["invalidUTF8"]=invalid;input.own(host);}
        else if(std::string(field)=="timestamp")input.fixtureTimestampUtc=invalid;
        else input.g_realStickTest.targetStick=invalid;equivalent(input);
    }
    {
        auto input=active(hosts,"steamdeck",true);
        json host=*input.hostFrameView;
        host["allTypes"]={{"i",std::numeric_limits<std::int64_t>::min()},{"u",std::numeric_limits<std::uint64_t>::max()},
            {"binary",json::binary({0,127,255},42)},{"text","微星\n\"\\\t"},{"null",nullptr},{"bool",false},
            {"nested",json::array({json::object(),1,-0.0,"UTF8"})}};
        host["latency"]={{"sequence",std::numeric_limits<std::uint64_t>::max()},{"cost",-0.0}};
        host["imu"]={{"admitted",true},{"quaternion",{{"w",1.0},{"x",0.0},{"y",0.0},{"z",0.0}}}};
        host["touchpads"]={{"left",{{"active",true},{"click",true},{"x",-32768},{"y",32767},{"pressure",32767}}},{"right",{{"active",false},{"x",0},{"y",0}}}};
        host["screenButtons"]=63;host["backButtons"]=3;input.own(host);equivalent(input);wrapperEquivalent(input);
    }
    for(unsigned mode=0;mode<32;++mode){
        auto input=active(hosts,"dualsense-edge");input.g_hwnd=(mode&1)==0;input.postSuccess=(mode&2)==0;
        input.exitRequested=(mode&4)!=0;input.g_view=(mode&8)==0;input.childrenMask=(mode&16)?3:0;
        input.costEnabled=(mode&1)!=0;wrapperEquivalent(input);wrapperEquivalent(input,true);
    }
    {auto input=active(hosts,"steamdeck");input.dropWindowDuringSnapshot=true;wrapperEquivalent(input);}
    {auto input=active(hosts,"steamdeck");input.g_hwnd=false;gateCases(input);}
    {auto input=active(hosts,"steamdeck");input.costEnabled=true;gateCases(input);}
    {auto input=active(hosts,"steamdeck");input.own(*input.hostFrameView);input.now=0;gateCases(input);}
    std::mt19937_64 rng(0x20261108);
    for(unsigned i=0;i<256;++i){
        auto input=active(hosts,i%2?"steamdeck":"dualsense-edge",bool(i&1),bool(i&2));
        input.now=rng();input.sensorPresent=bool(i&4);input.g_realStickTest.targetStick=i%2?"left":"right";
        input.g_realStickTest.motionTriggerResolved=bool(i&8);input.g_realStickTest.aimingSightsTriggerResolved=bool(i&16);
        input.cgx=std::ldexp(static_cast<double>(static_cast<std::int32_t>(rng())),-23);input.cgy=-input.cgx;input.cgz=input.cgx/3;
        input.psx=static_cast<float>(input.cgx);input.gravityY=static_cast<float>(input.cgy);
        input.motionPlaneGyroMultiplier=static_cast<float>(static_cast<int>(i%11)-5);input.motionPlaneAccelMultiplier=static_cast<float>(i%13);
        json host=*input.hostFrameView;host["extra"]={{"sequence",rng()},{"signed",static_cast<std::int64_t>(rng())},{"text",std::string(i%96,'s')},{"null",nullptr}};
        if(i%5==0)host.erase("leftStick");if(i%7==0)host.erase("motionManager");input.own(host);equivalent(input);
    }
#if defined(YMCC_TYPED_TELEMETRY_BUILD)
    typedLifecycleCases(hosts);
#endif // YMCC_TYPED_TELEMETRY_BUILD
}

// Fixed cost preparation. This code is reachable only through the separate
// explicit benchmark command; correctness does not call either measurement API.
static std::uint64_t cycles(){
    ULONG64 value=0;if(!QueryThreadCycleTime(GetCurrentThread(),&value))throw std::runtime_error("QueryThreadCycleTime failed");
    return value;
}
struct Measurement {double ns=0,cycles=0,calls=0,bytes=0;};
static Measurement measure(const Input&input,int mode,bool candidate,bool allocations,unsigned batches){
    double totalNs=0,totalCycles=0,totalCalls=0,totalBytes=0;std::uint64_t sink=0;
    for(unsigned batch=0;batch<batches;++batch){
        std::array<std::optional<Fixture>,32> prepared;
        for(auto&entry:prepared)entry.emplace(input);
        count={};countEnabled=allocations;
        const auto beforeCycles=cycles();const auto before=std::chrono::steady_clock::now();
        for(auto&entry:prepared){
            auto&f=*entry;
            if(mode==0){auto value=(f.*(candidate?&Fixture::candidateInitializer:&Fixture::beforeInitializer))();sink+=value.size();}
            else{
                (f.*(candidate?&Fixture::candidatePost:&Fixture::beforePost))();
                if(mode==2){f.consumePosted();for(const auto&row:f.delivered)sink+=row.second.size();}
                else if(f.posted)sink+=f.posted->data.size();
            }
        }
        for(auto&entry:prepared)entry.reset();
        const auto after=std::chrono::steady_clock::now();const auto afterCycles=cycles();
        countEnabled=false;
        if(afterCycles<=beforeCycles)throw std::runtime_error("non-positive thread cycle interval");
        totalNs+=std::chrono::duration<double,std::nano>(after-before).count();
        totalCycles+=static_cast<double>(afterCycles-beforeCycles);totalCalls+=static_cast<double>(count.calls);totalBytes+=static_cast<double>(count.bytes);
    }
    benchmarkSink.fetch_add(sink,std::memory_order_relaxed);const double operations=32.0*batches;
    return{totalNs/operations,totalCycles/operations,totalCalls/operations,totalBytes/operations};
}
struct Row {const char*name;const char*persona;int mode;bool plane,yaw,diagnostics,children;};
static json benchmark(const json&hosts,int campaign){
    if(campaign!=0&&campaign!=1)throw std::runtime_error("fixed campaigns are 0 and 1");
    const std::array<Row,11> specs={{
        {"inactive-tree",nullptr,0,false,false,false,false},
        {"elite-tree","elite",0,false,false,false,false},
        {"steamdeck-yaw-tree","steamdeck",0,true,true,false,false},
        {"edge-roll-tree","dualsense-edge",0,true,false,false,false},
        {"inactive-message",nullptr,1,false,false,false,false},
        {"steamdeck-message","steamdeck",1,true,true,false,false},
        {"dualsense-edge-message","dualsense-edge",1,true,false,false,false},
        {"inactive-emitter",nullptr,2,false,false,false,false},
        {"steamdeck-children-emitter","steamdeck",2,true,true,false,true},
        {"edge-diagnostic-emitter","dualsense-edge",2,true,false,true,false},
        {"edge-children-diagnostic-emitter","dualsense-edge",2,true,true,true,true},
    }};
    json rows=json::array();
    for(const auto&spec:specs){
        auto input=spec.persona?active(hosts,spec.persona,spec.plane,spec.yaw):inactive();
        input.costEnabled=spec.diagnostics;input.childrenMask=spec.children?3:0;
        (void)measure(input,spec.mode,false,false,6);(void)measure(input,spec.mode,true,false,6);
        const auto bc=measure(input,spec.mode,false,true,1),cc=measure(input,spec.mode,true,true,1);
        std::vector<double>beforeCycles,candidateCycles,beforeNs,candidateNs;
        for(unsigned pair=0;pair<11;++pair){
            Measurement before,after;
            if(((pair+static_cast<unsigned>(campaign))&1u)==0u){before=measure(input,spec.mode,false,false,24);after=measure(input,spec.mode,true,false,24);}
            else{after=measure(input,spec.mode,true,false,24);before=measure(input,spec.mode,false,false,24);}
            beforeCycles.push_back(before.cycles);candidateCycles.push_back(after.cycles);beforeNs.push_back(before.ns);candidateNs.push_back(after.ns);
        }
        rows.push_back({{"name",spec.name},{"mode",spec.mode},{"beforeCycles",beforeCycles},{"candidateCycles",candidateCycles},{"beforeNs",beforeNs},{"candidateNs",candidateNs},
            {"beforeAllocationRequestsPerEvent",bc.calls},{"candidateAllocationRequestsPerEvent",cc.calls},
            {"beforeAllocationBytesPerEvent",bc.bytes},{"candidateAllocationBytesPerEvent",cc.bytes},
            {"setupExcluded",true},{"ownedTreesEventsWiresDestructionIncluded",true},{"sharedHostLifetimeExcluded",true}});
    }
    return{{"campaign",campaign},{"rounds",11},{"rows",rows},{"batchSize",32},{"batchesPerArm",24},
        {"warmupBatches",6},{"cycleCounter","QueryThreadCycleTime(GetCurrentThread())"},{"wholeProcessCpuAcceptance",false}};
}
} // namespace current_tree

int main(int argc,char**argv){
    using namespace current_tree;
    try{
        if(argc<3)throw std::runtime_error("host-inputs.json correctness|negative|benchmark CAMPAIGN");
        std::ifstream input(argv[1]);if(!input)throw std::runtime_error("host inputs open failed");json hosts;input>>hosts;
        const std::string command=argv[2];
        if(command=="correctness"||command=="negative"){
            correctness(hosts,command=="negative");
            std::cout<<json{{"pass",true},{"scopeVersion","current-tree-build-v1"},{"cases",cases},{"assertions",assertions.load()},
                {"realJsonTypesSpecialFloatBitsDump",true},{"currentWrappersAndSerialCase",true},
                {"snapshotCountAndEvaluationEdge",true},{"inactiveCacheAndSharedHost",true},{"exact80msGate",true},
#if defined(YMCC_TYPED_TELEMETRY_BUILD)
                {"activeHostInputsAreFixtureData",true},{"typedLifecycle",typedLifecycleReceipts},{"typedPipelineExecuted",true},{"costBenchmarkExecuted",false},{"productDeviceUiHostIntegrationExecuted",false}}.dump(2)<<'\n';return 0;
#else // YMCC_TYPED_TELEMETRY_BUILD
                {"activeHostInputsAreFixtureData",true},{"costBenchmarkExecuted",false},{"productDeviceUiHostIntegrationExecuted",false}}.dump(2)<<'\n';return 0;
#endif // YMCC_TYPED_TELEMETRY_BUILD
        }
        if(command=="benchmark"&&argc==4){std::cout<<benchmark(hosts,std::stoi(argv[3])).dump(2)<<'\n';return 0;}
        throw std::runtime_error("unknown explicit current-tree command");
    }catch(const std::exception&e){
        countEnabled=false;liveEnabled=false;failAfter=-1;
        std::cout<<json{{"pass",false},{"scopeVersion","current-tree-build-v1"},{"failure",e.what()},{"cases",cases},{"assertions",assertions.load()}}.dump(2)<<'\n';return 1;
    }
}
#else
struct BoolLatch {bool value=false;bool load(std::memory_order)const noexcept{return value;}};
struct Counter {std::uint64_t value=0;std::uint64_t fetch_add(std::uint64_t n,std::memory_order){auto old=value;value+=n;return old;}};
struct UiCostIpcScope {explicit UiCostIpcScope(const char*){}};
struct ChildView {bool view=true;};
static ChildView childA,childB;
using LPARAM=std::intptr_t;
static constexpr int WM_GAMEPAD_SERIAL_EVENT=0x410;
static constexpr int FALSE=0;
#include "event-struct-extracted.h"
#ifdef YMCC_SERIAL_ACTION_POWER
static_assert(std::is_same_v<decltype(GamepadSerialEvent::actionPower),unsigned long long>);
static_assert(std::is_same_v<decltype(g_gamepadSerialActionPower),unsigned long long>);
#endif

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
    std::uintptr_t postedToken=0;
    const void* wrapperHostAddress=nullptr;const void* envelopeHostAddress=nullptr;
    unsigned sensorSnapshots=0,postCalls=0,ipcCalls=0;
    std::vector<std::pair<int,std::string>> wire;
    bool tickCostEnabled()const{return costEnabled;}
    bool hardwareWriteAllowed()const{return consumerAllowed;}
    struct SensorMeta {bool gyroPresent;};
    SensorMeta inputCaptureSensorMetaSnapshot(){++sensorSnapshots;if(dropWindowDuringSnapshot)g_hwnd=false;return{sensorPresent};}
    bool inputCaptureGyroPresentSnapshot(){return inputCaptureSensorMetaSnapshot().gyroPresent;}
    bool PostMessageW(bool hwnd,int message,int,LPARAM value){
        ++postCalls;
#ifdef YMCC_SERIAL_EVENT_MAILBOX
        // ResultMailbox::post holds its lock here: never take in this callback.
        if(postSuccess)postedToken=static_cast<std::uintptr_t>(value);
#else
        auto* payload=reinterpret_cast<GamepadSerialEvent*>(value);
        wrapperHostAddress=objectAddress(payload->data.at("hostFrame"));
        if(postSuccess)posted=payload;
#endif
        return postSuccess;
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
    void capturePosted(){
#ifdef YMCC_SERIAL_EVENT_MAILBOX
        if(!postedToken)return;
        const auto token=postedToken;postedToken=0;
        auto event=g_gamepadSerialEventMailbox->take(token);
        if(posted)throw std::runtime_error("fixture message slot already occupied");
        posted=event.release();
        if(posted)wrapperHostAddress=objectAddress(posted->data.at("hostFrame"));
#endif
    }
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
    ~Fixture(){
#ifdef YMCC_SERIAL_EVENT_MAILBOX
        if(postedToken)(void)g_gamepadSerialEventMailbox->take(postedToken);
#endif
        delete posted;
    }
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
            const bool candidate=std::string(argv[3])=="candidate";const auto index=std::stoll(argv[4]);bool failed=false,deliveryQueued=false;unsigned observedPostCalls=0;std::uint64_t eventAllocations=0;liveBalance=0;liveEnabled=true;
            {Fixture f;f.hostFrame=hostFrameFixture(sample,true);failAfter=index;count={};countEnabled=true;try{(f.*(candidate?&Fixture::candidatePost:&Fixture::beforePost))();}catch(const std::bad_alloc&){failed=true;}failAfter=-1;countEnabled=false;eventAllocations=count.calls;deliveryQueued=f.posted!=nullptr;observedPostCalls=f.postCalls;}
            liveEnabled=false;std::cout<<json{{"pass",true},{"variant",candidate?"candidate":"before"},{"faultIndex",index},{"badAllocCaught",failed},{"deliveryQueued",deliveryQueued},{"postCalls",observedPostCalls},{"eventAllocationCalls",eventAllocations},{"liveAllocationBalance",liveBalance}}.dump()<<'\n';return 0;
        }
        throw std::runtime_error("unknown command");
    }catch(const std::exception&e){countEnabled=false;liveEnabled=false;failAfter=-1;std::cerr<<e.what()<<'\n';return 3;}
}
#endif // YMCC_CURRENT_TREE_BUILD: historical R18 implementation stays unchanged.
