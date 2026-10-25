#include "controller_motion_session.h"
#include "main_madgwick_oracle.generated.h"

#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

using namespace ymcc::controller_motion;
static_assert(sizeof(void*) == 8);
static_assert(!std::is_copy_constructible_v<Session> && !std::is_copy_assignable_v<Session>);
static_assert(!std::is_move_constructible_v<Session> && !std::is_move_assignable_v<Session>);
#if defined(_MSC_VER)
using ActualRead3 = void (__cdecl*)(void*, float*, float*, float*);
using ActualSpace = void (__cdecl*)(void*, float*, float*, float);
static_assert(std::is_same_v<decltype(ReadOps::getCal), ActualRead3>);
static_assert(std::is_same_v<decltype(ReadOps::getGravity), ActualRead3>);
static_assert(std::is_same_v<decltype(ReadOps::getPlayer), ActualSpace>);
static_assert(std::is_same_v<decltype(ReadOps::getWorld), ActualSpace>);
#endif
namespace {
unsigned checks = 0;
void require(bool condition, const std::string& message) {
    ++checks; if (!condition) throw std::runtime_error(message);
}
std::uint32_t bits(float value) { std::uint32_t result; std::memcpy(&result, &value, sizeof result); return result; }
bool same(float a, float b) { return bits(a) == bits(b) || (std::isnan(a) && std::isnan(b)); }
bool sameDouble(double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); }
struct Handle { unsigned serial = 0; bool alive = true; };
struct Event {
    std::string kind; Handle* handle; int bank = 0; std::array<float, 7> values{}; int integer = 0;
};
struct Sink {
    std::vector<std::unique_ptr<Handle>> handles;
    std::vector<Event> events;
    MotionVector3 calibrated{17, -29, 43};
    MotionVector3 gravity{0.3f, -0.4f, 0.85f};
    bool failCreate = false, reusePointer = false;
    unsigned violations = 0, serial = 0;
    unsigned count(const std::string& kind) const {
        unsigned result = 0; for (const auto& event : events) if (event.kind == kind) ++result; return result;
    }
    const Event& last(const std::string& kind) const {
        for (auto i = events.rbegin(); i != events.rend(); ++i) if (i->kind == kind) return *i;
        throw std::runtime_error("missing effect " + kind);
    }
    Handle* lookup(void* p) {
        for (const auto& h : handles) if (h.get() == p) return h.get();
        ++violations; return nullptr;
    }
    Handle* live(void* p) {
        auto h = lookup(p); if (!h || !h->alive) ++violations; return h;
    }
    void clean() const {
        require(violations == 0, "invalid native resource use");
        for (const auto& h : handles) require(!h->alive, "native resource leaked");
    }
};
Sink* sink = nullptr;
struct Fixture { Sink effects; Fixture() { sink = &effects; } ~Fixture() { sink = nullptr; } };
void* createIn(int bank) {
    if (sink->failCreate) { sink->failCreate = false; sink->events.push_back({"create-failed", nullptr, bank}); return nullptr; }
    Handle* p = nullptr;
    if (sink->reusePointer && !sink->handles.empty() && !sink->handles.front()->alive) p = sink->handles.front().get();
    else { auto h = std::make_unique<Handle>(); p = h.get(); sink->handles.push_back(std::move(h)); }
    p->alive = true; p->serial = ++sink->serial;
    sink->events.push_back({"create", p, bank}); return p;
}
void destroyIn(int bank, void* p) {
    auto h = sink->live(p); sink->events.push_back({"destroy", h, bank}); if (h) h->alive = false;
}
void modeIn(int bank, void* p, int m) {
    Event e{"mode", sink->live(p), bank}; e.integer = m; sink->events.push_back(e);
}
void offsetIn(int bank, void* p, float x, float y, float z, int w) {
    Event e{"offset", sink->live(p), bank}; e.values = {x, y, z, 0, 0, 0, 0}; e.integer = w; sink->events.push_back(e);
}
void processIn(int bank, void* p, float gx, float gy, float gz, float ax, float ay, float az, float dt) {
    Event e{"process", sink->live(p), bank}; e.values = {gx, gy, gz, ax, ay, az, dt}; sink->events.push_back(e);
}
void calIn(int bank, void* p, float* x, float* y, float* z) {
    sink->events.push_back({"cal", sink->live(p), bank});
    *x = sink->calibrated.x; *y = sink->calibrated.y; *z = sink->calibrated.z;
}
void gravityIn(int bank, void* p, float* x, float* y, float* z) {
    sink->events.push_back({"gravity", sink->live(p), bank});
    *x = sink->gravity.x; *y = sink->gravity.y; *z = sink->gravity.z;
}
void spaceIn(int bank, void* p, float* x, float* y, float parameter, bool player) {
    Event e{player ? "player" : "world", sink->live(p), bank}; e.values[0] = parameter; sink->events.push_back(e);
    *x = player ? 101.0f : -201.0f; *y = player ? -102.0f : 202.0f;
}
void* create0() { return createIn(0); } void* create1() { return createIn(1); }
void destroy0(void* p) { destroyIn(0,p); } void destroy1(void* p) { destroyIn(1,p); }
void mode0(void* p,int m) { modeIn(0,p,m); } void mode1(void* p,int m) { modeIn(1,p,m); }
void offset0(void* p,float x,float y,float z,int w) { offsetIn(0,p,x,y,z,w); }
void offset1(void* p,float x,float y,float z,int w) { offsetIn(1,p,x,y,z,w); }
void process0(void* p,float a,float b,float c,float d,float e,float f,float dt) { processIn(0,p,a,b,c,d,e,f,dt); }
void process1(void* p,float a,float b,float c,float d,float e,float f,float dt) { processIn(1,p,a,b,c,d,e,f,dt); }
void cal0(void* p,float* x,float* y,float* z) { calIn(0,p,x,y,z); }
void cal1(void* p,float* x,float* y,float* z) { calIn(1,p,x,y,z); }
void gravity0(void* p,float* x,float* y,float* z) { gravityIn(0,p,x,y,z); }
void gravity1(void* p,float* x,float* y,float* z) { gravityIn(1,p,x,y,z); }
void player0(void* p,float* x,float* y,float f) { spaceIn(0,p,x,y,f,true); }
void player1(void* p,float* x,float* y,float f) { spaceIn(1,p,x,y,f,true); }
void world0(void* p,float* x,float* y,float f) { spaceIn(0,p,x,y,f,false); }
void world1(void* p,float* x,float* y,float f) { spaceIn(1,p,x,y,f,false); }
Ops ops() { return {create0,destroy0,mode0,offset0,process0}; }
ReadOps reads() { return {cal0,gravity0,player0,world0}; }
Key key() { return {"legion/container/physical", 10, 20, 30, 1}; }
Calibration calibration() { return {0,0,0,0}; }
RawSixAxes raw() { return {1,-2,3,-4,5,-6}; }
void delta(const Sink& s, const MotionResult& result, double expected) {
    require(sameDouble(result.deltaSeconds, expected), "delta must use selection/previous processed steady double time");
    require(same(s.last("process").values[6], static_cast<float>(expected)), "native dt must be caller difference without clamp/cache/finite gate");
}
MotionQuaternion quaternion(const reference::MainFormula& formula) {
    return {formula.state[0],formula.state[1],formula.state[2],formula.state[3]};
}
void nearQuaternion(MotionQuaternion actual, MotionQuaternion expected, const std::string& message) {
    const float a[]{actual.w,actual.x,actual.y,actual.z}, e[]{expected.w,expected.x,expected.y,expected.z};
    for (unsigned i=0;i<4;++i) require(same(a[i],e[i]) || (std::isfinite(a[i]) && std::isfinite(e[i]) && std::fabs(a[i]-e[i]) <= 0.000001f), message);
}
void advance(reference::MainFormula& formula, const Sink& s, double dt) {
    const float rad = 3.14159265358979323846f / 180.0f;
    formula.update(-rad*s.calibrated.x,rad*s.calibrated.y,-rad*s.calibrated.z,
                   -s.gravity.x,s.gravity.y,-s.gravity.z,static_cast<float>(dt));
}
MotionResult processed(Session& session, const Key& token, const RawSixAxes& axes, double now, ReadRequest request={}) {
    auto result=session.process(token,axes,now,request); require(result.has_value(), "valid tick rejected"); return *result;
}
void testFirstAndTick() {
    Fixture f; auto& s=f.effects; Session session; const auto token=key();
    require(session.select(token,ops(),reads(),calibration(),100.25), "select failed");
    require(s.events.size()==3 && s.events[0].kind=="create" && s.events[1].kind=="mode" && s.events[1].integer==0 && s.events[2].kind=="offset", "selection must be Manual zero calibration without invented sample");
    const auto a=processed(session,token,raw(),100.375); delta(s,a,0.125);
    const auto b=processed(session,token,raw(),100.40625); delta(s,b,0.03125);
    require(s.count("process")==2, "same ControllerState sample must process each BUS tick");
    require(!a.player && !a.world, "unrequested spaces must be absent");
    require(a.axes.gx==1 && a.axes.az==-6 && a.calibrated.x==17 && a.gravity.z==0.85f, "raw/calibrated/gravity outputs lost");
    session.revoke(); s.clean();
}
void testIdempotence() {
    Fixture f; auto& s=f.effects; Session session; reference::MainFormula formula; const auto token=key();
    require(session.select(token,ops(),reads(),calibration(),10), "select failed");
    auto first=processed(session,token,raw(),10.125); advance(formula,s,0.125); nearQuaternion(first.orientation,quaternion(formula),"first reference mismatch");
    auto handle=session.handleFor(token);
    require(session.select(token,ops(),reads(),{55,-66,77,99},99), "same selection failed");
    auto second=processed(session,token,raw(),10.375); delta(s,second,0.25);
    advance(formula,s,0.25); nearQuaternion(second.orientation,quaternion(formula),"unchanged select reset orientation");
    require(session.handleFor(token)==handle && s.count("create")==1 && s.count("mode")==1 && s.count("offset")==1, "same selection recreated or rewrote calibration");
    session.clear(); s.clean();
}
void testReadsAndSpaces() {
    Fixture f; auto& s=f.effects; Session session; const auto token=key();
    require(session.select(token,ops(),reads(),calibration(),0),"select failed");
    auto a=processed(session,token,raw(),1);
    require(s.count("player")==0 && s.count("world")==0 && !a.player && !a.world,"inactive spaces must not call getters");
    require(s.events[3].kind=="process" && s.events[4].kind=="cal" && s.events[5].kind=="gravity", "processing must precede paired native reads");
    auto b=processed(session,token,raw(),2,{true,false});
    require(b.player && b.player->x==101 && b.player->y==-102 && !b.world,"player result missing");
    require(s.count("player")==1 && s.count("world")==0 && same(s.last("player").values[0],1.41f),"player getter/factor must be requested-only 1.41");
    auto c=processed(session,token,raw(),3,{false,true});
    require(c.world && c.world->x==-201 && c.world->y==202 && !c.player,"world result missing");
    require(s.count("player")==1 && s.count("world")==1 && same(s.last("world").values[0],0.125f),"world getter/factor must be requested-only 0.125");
    auto d=processed(session,token,raw(),4,{true,true});
    require(d.player && d.world && s.count("player")==2 && s.count("world")==2,"both spaces cannot be requested");
    require(s.count("cal")==4 && s.count("gravity")==4,"readback pair must execute once per tick");
    for(const auto& event:s.events) require(event.handle==session.handleFor(token),"read callback used a foreign handle");
    session.clear();s.clean();
}
void testOptionalReads() {
    Fixture f;auto& s=f.effects;Session session;auto rd=reads();rd.getPlayer=nullptr;rd.getWorld=nullptr;const auto token=key();
    require(session.select(token,ops(),rd,calibration(),0),"unrequested optional callbacks must not gate selection");
    processed(session,token,raw(),1);const auto count=s.events.size();
    require(!session.process(token,raw(),20,{true,false}) && !session.process(token,raw(),30,{false,true}),"missing requested getter must reject tick");
    require(s.events.size()==count,"missing requested getter called ProcessMotion");
    auto next=processed(session,token,raw(),2);delta(s,next,1);
    session.clear();s.clean();
}
void testMissingRequiredReads() {
    for(unsigned field=0;field<2;++field){Fixture f;auto& s=f.effects;Session session;auto rd=reads();if(field==0)rd.getCal=nullptr;else rd.getGravity=nullptr;
        require(!session.select(key(),ops(),rd,calibration(),0),"missing required read accepted");
        require(!session.process(key(),raw(),1) && s.events.empty(),"no-read session processed/created native resource");
        require(session.select(key(),ops(),reads(),calibration(),0),"valid select failed");
        require(!session.select(key(),ops(),rd,calibration(),1),"missing read replacement accepted");
        require(!session.process(key(),raw(),2) && s.count("destroy")==1,"missing read retained old authorization");
        session.revoke();s.clean();}
}
void testDeltaUnmodified() {
    const double increments[]={0.0,-0.0,-0.5,0.000001,0.0000001,10000.0,std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()};
    for(double dt:increments){Fixture f;auto& s=f.effects;s.gravity={0,0,0};Session session;
        require(session.select(key(),ops(),reads(),calibration(),0),"select failed");
        auto result=processed(session,key(),{},dt);delta(s,result,dt-0.0);
        nearQuaternion(result.orientation,{},"zero gravity changed quaternion");session.clear();s.clean();}
    Fixture f;auto& s=f.effects;s.gravity={0,0,0};Session session;
    const double selected=1000000000000.25, now=selected+0.001;
    require(session.select(key(),ops(),reads(),calibration(),selected),"large steady-time selection failed");
    const auto result=processed(session,key(),raw(),now);delta(s,result,now-selected);
    session.revoke();s.clean();
}
void testRejectedSamplesDoNotAdvance() {
    Fixture f;auto& s=f.effects;Session session;require(session.select(key(),ops(),reads(),calibration(),0),"select failed");
    const float invalid[]={std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()};
    for(unsigned field=0;field<6;++field)for(float v:invalid){std::array<float,6>a{1,2,3,4,5,6};a[field]=v;
        require(!session.process(key(),{a[0],a[1],a[2],a[3],a[4],a[5]},100),"nonfinite raw axis accepted");}
    auto wrong=key();++wrong.power;require(!session.process(wrong,raw(),200),"foreign key processed");
    require(s.count("process")==0 && s.count("cal")==0,"rejected sample emitted effects");
    const auto result=processed(session,key(),{},1);delta(s,result,1);session.clear();s.clean();
}
Key changed(Key token,unsigned field){switch(field){case 0:token.physicalIdentity+="/other";break;case 1:++token.power;break;case 2:++token.rawGeneration;break;case 3:++token.captureEpoch;break;case 4:++token.sensorIndex;break;}return token;}
void testKeyReplacement(){for(unsigned field=0;field<5;++field){Fixture f;auto& s=f.effects;s.reusePointer=true;Session session;auto old=key(),next=changed(old,field);
    require(session.select(old,ops(),reads(),calibration(),0),"select failed");processed(session,old,raw(),1);auto p=session.handleFor(old);
    const auto effects=s.events.size();require(session.select(next,ops(),reads(),calibration(),20),"new key selection failed");
    require(s.events[effects].kind=="destroy" && s.events[effects+1].kind=="create","key replacement must retire before create");
    require(session.handleFor(old)==nullptr && session.handleFor(next)==p,"same-pointer allocation must not preserve old token");
    require(!session.process(old,raw(),500),"retired token processed replacement");
    auto result=processed(session,next,raw(),20);delta(s,result,0);nearQuaternion(result.orientation,{},"key replacement must reset orientation even for same native pointer");
    session.clear();s.clean();}}
Ops changedOps(Ops o,unsigned field){if(field==0)o.create=create1;if(field==1)o.destroy=destroy1;if(field==2)o.setMode=mode1;if(field==3)o.setOffset=offset1;if(field==4)o.process=process1;return o;}
ReadOps changedReads(ReadOps r,unsigned field){if(field==0)r.getCal=cal1;if(field==1)r.getGravity=gravity1;if(field==2)r.getPlayer=player1;if(field==3)r.getWorld=world1;return r;}
void testAllCallbackReplacement(){for(unsigned field=0;field<9;++field){Fixture f;auto& s=f.effects;s.reusePointer=true;Session session;
    require(session.select(key(),ops(),reads(),calibration(),0),"select failed");processed(session,key(),raw(),1);
    auto op=field<5?changedOps(ops(),field):ops();auto rd=field>=5?changedReads(reads(),field-5):reads();
    const auto effects=s.events.size();require(session.select(key(),op,rd,calibration(),100),"callback replacement failed");
    require(s.count("create")==2 && s.count("destroy")==1 && s.events[effects].kind=="destroy" && s.events[effects+1].kind=="create","every native/read callback change must replace resource");
    auto result=processed(session,key(),raw(),100,{true,true});delta(s,result,0);nearQuaternion(result.orientation,{},"callback replacement must reset orientation");
    const char* kind=field==0?"create":field==1?"destroy":field==2?"mode":field==3?"offset":field==4?"process":field==5?"cal":field==6?"gravity":field==7?"player":"world";
    session.revoke();require(s.last(kind).bank==1,"replacement callback was not used");s.clean();}}
void testCreateFailure(){Fixture f;auto& s=f.effects;Session session;auto old=key(),next=changed(old,3);
    require(session.select(old,ops(),reads(),calibration(),0),"select failed");processed(session,old,raw(),1);s.failCreate=true;
    require(!session.select(next,ops(),reads(),calibration(),10),"failed native creation accepted");
    require(!session.process(old,raw(),20) && !session.process(next,raw(),20) && !session.handleFor(old) && !session.handleFor(next),"failed creation retained new/old handle");
    require(!session.select(old,ops(),reads(),calibration(),20) && !session.select(next,ops(),reads(),calibration(),20),"failed or retired token revived");
    ++next.captureEpoch;require(session.select(next,ops(),reads(),calibration(),30),"fresh epoch failed after creation failure");
    auto result=processed(session,next,raw(),30.125);delta(s,result,0.125);reference::MainFormula formula;advance(formula,s,0.125);nearQuaternion(result.orientation,quaternion(formula),"failure leaked previous orientation");session.clear();s.clean();}
void testReadReplacementHistory(){Fixture f;auto& s=f.effects;Session session;auto a=key(),b=changed(a,3);
    require(session.select(a,ops(),reads(),calibration(),0),"select A failed");require(session.select(b,ops(),reads(),calibration(),1),"select B failed");
    const auto rd=changedReads(reads(),0);require(session.select(b,ops(),rd,calibration(),2),"read-only replacement failed");
    require(!session.select(a,ops(),reads(),calibration(),3),"private Owner reconstruction revived historical token");
    require(!session.process(a,raw(),4) && !session.process(b,raw(),4),"rejected historical selection retained permission");
    require(!session.select(b,ops(),rd,calibration(),5),"revoked token revived after read-only reconstruction");session.clear();s.clean();}
void testStopRestart(){Fixture f;auto& s=f.effects;{Session session;auto old=key();require(session.select(old,ops(),reads(),calibration(),0),"select failed");processed(session,old,raw(),1);
    session.revoke();session.clear();session.revoke();require(s.count("destroy")==1,"repeated revoke destroyed twice");
    require(!session.select(old,ops(),reads(),calibration(),2),"Stop->Start reused same token");auto next=old;++next.captureEpoch;
    require(session.select(next,ops(),reads(),calibration(),5),"fresh capture epoch failed");auto result=processed(session,next,raw(),5);delta(s,result,0);nearQuaternion(result.orientation,{},"Stop->Start did not reset orientation");}
    require(s.count("create")==2 && s.count("destroy")==2,"destructor resource accounting failed");s.clean();}
void testFormulaTrajectory(){Fixture f;auto& s=f.effects;Session session;reference::MainFormula formula;require(session.select(key(),ops(),reads(),calibration(),100),"select failed");double previous=100;
    for(unsigned tick=1;tick<=60;++tick){const float t=static_cast<float>(tick);s.calibrated={17.0f+t,-29.0f+0.3f*t,43.0f-0.2f*t};s.gravity={0.3f+0.001f*t,-0.4f+0.002f*t,0.85f};
        const double now=100.0+0.008*static_cast<double>(tick);auto result=processed(session,key(),raw(),now);advance(formula,s,now-previous);previous=now;
        nearQuaternion(result.orientation,quaternion(formula),"Madgwick output differs from main formula and HC gyro/gravity remapping");
        require(std::isfinite(result.orientation.w) && result.orientationFinite,"nondegenerate trajectory must have finite quaternion");}
    session.clear();s.clean();}
void testInterleavedSessions(){Fixture f;auto& s=f.effects;Session left,right;auto l=key(),r=l;r.sensorIndex=2;reference::MainFormula lo,ro;
    require(left.select(l,ops(),reads(),calibration(),0) && right.select(r,ops(),reads(),calibration(),10),"independent sessions select failed");
    auto a=processed(left,l,raw(),0.125);advance(lo,s,0.125);nearQuaternion(a.orientation,quaternion(lo),"left orientation mismatch");
    s.calibrated={-130,70,230};s.gravity={0.7f,0.2f,0.1f};auto b=processed(right,r,raw(),10.25);advance(ro,s,0.25);nearQuaternion(b.orientation,quaternion(ro),"sessions shared Madgwick quaternion");
    s.calibrated={3,8,-13};s.gravity={-0.1f,0.6f,0.9f};a=processed(left,l,raw(),0.5);advance(lo,s,0.375);nearQuaternion(a.orientation,quaternion(lo),"interleaved session changed left state");
    require(!left.process(r,raw(),99) && !right.process(l,raw(),99),"independent sensor keys crossed");left.revoke();require(right.process(r,raw(),10.5).has_value(),"left revoke destroyed right resource");right.revoke();s.clean();}
void testExactAlignedStationary(){
    for(float magnitude:{0.5f,1.0f,2.0f}){Fixture f;auto& s=f.effects;Session session;s.calibrated={0,0,0};s.gravity={0,0,magnitude};
        require(session.select(key(),ops(),reads(),calibration(),100),"select failed");double previous=100;
        for(unsigned tick=1;tick<=64;++tick){const double now=100.0+0.008*static_cast<double>(tick);
            const auto result=processed(session,key(),{},now);delta(s,result,now-previous);previous=now;
            require(result.orientationFinite,"aligned stationary gravity must keep finite orientation");
            nearQuaternion(result.orientation,{},"aligned stationary gravity must preserve initial quaternion");
            const auto& native=s.last("process").values;for(unsigned axis=0;axis<6;++axis)require(native[axis]==0.0f,"raw zero stationary sample was fabricated or rejected");
            require(!result.player && !result.world,"stationary processing read unrequested planes");}
        require(s.count("process")==64 && s.count("cal")==64 && s.count("gravity")==64,"aligned stationary repeated samples must process every BUS tick");session.revoke();s.clean();}
}
void testExactAlignedGyroIntegration(){
    const MotionVector3 gyros[]={{90,0,0},{0,90,0},{0,0,90},{90,-60,30}};
    for(const auto cal:gyros){Fixture f;auto& s=f.effects;Session session;s.calibrated=cal;s.gravity={0,0,1};
        require(session.select(key(),ops(),reads(),calibration(),100),"select failed");
        constexpr double dt=0.125;const auto result=processed(session,key(),{cal.x,cal.y,cal.z,0,0,1},100+dt);delta(s,result,dt);
        // Independent closed-form one-step gyro integration at Q={0,0,-1,0}.
        // After HC remap, unnormalized WXYZ = {+cy*h,+cz*h,-1,-cx*h}.
        const double h=0.5*dt*3.1415926535897932384626433832795/180.0;
        const double w=static_cast<double>(cal.y)*h,x=static_cast<double>(cal.z)*h,z=-static_cast<double>(cal.x)*h;
        const double scale=1.0/std::sqrt(w*w+x*x+1.0+z*z);
        const MotionQuaternion expected{static_cast<float>(w*scale),static_cast<float>(x*scale),static_cast<float>(-scale),static_cast<float>(z*scale)};
        require(result.orientationFinite,"aligned nonzero gyro must produce finite orientation");
        nearQuaternion(result.orientation,expected,"aligned nonzero gyro must integrate instead of freeze");
        require(std::fabs(result.orientation.w)+std::fabs(result.orientation.x)+std::fabs(result.orientation.z)>0.0f,"aligned gyro cannot return unchanged quaternion");
        session.revoke();s.clean();}
}
void testNonfiniteReadback(){Fixture f;auto& s=f.effects;Session session;reference::MainFormula formula;
    s.calibrated={std::numeric_limits<float>::quiet_NaN(),0,0};s.gravity={0.3f,-0.4f,0.85f};
    require(session.select(key(),ops(),reads(),calibration(),0),"select failed");auto result=processed(session,key(),{},0.125);advance(formula,s,0.125);
    nearQuaternion(result.orientation,quaternion(formula),"nonfinite callback propagation mismatch");
    require(std::isnan(result.orientation.w) && !result.orientationFinite,"nonfinite output must be explicitly marked nonfinite");
    session.revoke();auto next=key();++next.captureEpoch;require(session.select(next,ops(),reads(),calibration(),10),"fresh selection failed");
    auto reset=processed(session,next,{},10);require(reset.orientationFinite,"new session quaternion must clear nonfinite state");
    nearQuaternion(reset.orientation,{},"reset quaternion mismatch");session.revoke();s.clean();}
void testInvalidSelection(){for(unsigned kind=0;kind<4;++kind){Fixture f;auto& s=f.effects;Session session;auto k=key();auto op=ops();auto cal=calibration();
    if(kind==0)k.physicalIdentity.clear();if(kind==1)k.power=0;if(kind==2)op.destroy=nullptr;if(kind==3)cal.x=std::numeric_limits<float>::quiet_NaN();
    require(!session.select(k,op,reads(),cal,0) && s.events.empty(),"invalid selection must not call create");require(!session.process(k,raw(),1),"invalid selection processed");
    require(session.select(key(),ops(),reads(),calibration(),0),"valid select failed");require(!session.select(k,op,reads(),cal,1),"invalid replacement accepted");require(!session.process(key(),raw(),2),"invalid replacement retained authorization");session.clear();s.clean();}}

void testActiveAndSelectionQueries(){Fixture f;auto& s=f.effects;Session session;const auto token=key();
    require(!session.active() && !session.handleFor(token) && !session.matchesSelection(token,ops(),reads()),"new session must be inactive");
    require(session.select(token,ops(),reads(),calibration(),0),"select failed");
    require(session.active() && session.handleFor(token) && session.matchesSelection(token,ops(),reads()),"active selection query failed");
    const auto effects=s.events.size();auto next=token;++next.captureEpoch;
    require(!session.matchesSelection(next,ops(),reads()) && !session.matchesSelection(token,changedOps(ops(),4),reads()) &&
        !session.matchesSelection(token,ops(),changedReads(reads(),3)),"selection query ignored key/callbacks");
    require(s.events.size()==effects,"selection query must not create/read/process native state");
    if(session.active())session.revoke();if(session.active())session.revoke();
    require(!session.active() && !session.matchesSelection(token,ops(),reads()) && !session.handleFor(token) && s.count("destroy")==1,
        "active loss-source revoke must detach exactly once");s.clean();}
void testSubsequentTimeSemantics(){Fixture f;auto& s=f.effects;s.gravity={0,0,0};Session session;
    require(session.select(key(),ops(),reads(),calibration(),1),"select failed");double previous=1;
    const double times[]={2,1.5,1.5,1.50000001,900000,std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),5,std::numeric_limits<double>::quiet_NaN(),6};
    for(double now:times){auto result=processed(session,key(),{},now);delta(s,result,now-previous);previous=now;}
    session.revoke();s.clean();}
} // namespace
int main(int argc,char**argv){const std::vector<std::pair<std::string,std::function<void()>>> cases{
    {"first-and-repeated-tick",testFirstAndTick},{"unchanged-selection",testIdempotence},{"reads-and-requested-spaces",testReadsAndSpaces},
    {"missing-optional-reads",testOptionalReads},{"missing-required-reads",testMissingRequiredReads},{"delta-unmodified",testDeltaUnmodified},
    {"invalid-sample-does-not-advance",testRejectedSamplesDoNotAdvance},{"key-replacement",testKeyReplacement},{"all-callback-replacements",testAllCallbackReplacement},
    {"create-failure",testCreateFailure},{"read-replacement-history",testReadReplacementHistory},{"stop-restart",testStopRestart},
    {"madgwick-trajectory",testFormulaTrajectory},{"independent-sessions",testInterleavedSessions},{"exact-aligned-stationary",testExactAlignedStationary},{"exact-aligned-gyro-integration",testExactAlignedGyroIntegration},
    {"nonfinite-readback-status",testNonfiniteReadback},
    {"invalid-selection",testInvalidSelection},{"active-and-selection-queries",testActiveAndSelectionQueries},
    {"subsequent-time-semantics",testSubsequentTimeSemantics}};
    const std::string filter=argc==3&&std::string(argv[1])=="--case"?argv[2]:"";if(argc!=1&&filter.empty()){std::cerr<<"invalid arguments\n";return 2;}unsigned ran=0;
    for(const auto& item:cases){if(!filter.empty()&&filter!=item.first)continue;try{auto before=checks;item.second();++ran;std::cout<<"PASS "<<item.first<<" checks="<<checks-before<<'\n';}
        catch(const std::exception&e){std::cerr<<"FAIL "<<item.first<<": "<<e.what()<<'\n';return 1;}}
    if(!ran){std::cerr<<"unknown case\n";return 2;}std::cout<<"SUMMARY cases="<<ran<<" checks="<<checks<<'\n';return 0;}
