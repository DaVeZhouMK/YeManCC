#include "controller_motion_calibration.h"
#include "controller_motion_session.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include <type_traits>

using namespace ymcc::controller_motion;
static_assert(!std::is_copy_constructible_v<CalibrationTransaction>);
static_assert(!std::is_move_constructible_v<CalibrationTransaction>);
static_assert(std::is_nothrow_destructible_v<CalibrationTransaction>);

namespace {
int checks = 0, failures = 0;
std::string scenario;
void expect(bool value, const char* message) {
    ++checks;
    if (!value) { ++failures; std::cout << "FAIL " << scenario << ": " << message << '\n'; }
}
#define EXPECT(value) expect(static_cast<bool>(value), #value)
bool same(const Calibration& a, const Calibration& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.weight == b.weight;
}
struct Engine {
    bool alive = true, omitZ = false;
    int mode = 0, processes = 0, destroys = 0;
    float poll = 0.5f;
    Calibration current{1.25f, -2.5f, 0.0f, 7};
    Calibration offset{0.125f, -0.25f, 0.5f, 0};
};
std::vector<Engine*> engines;
std::vector<std::string> events;
Engine fallback;
Engine& touch(void* handle, const char* action) {
    const auto found = std::find(engines.begin(), engines.end(), handle);
    const bool live = found != engines.end() && (*found)->alive;
    EXPECT(live);
    events.emplace_back(action);
    return live ? **found : fallback;
}
void reset(void* h) { touch(h, "reset"); }
void mode(void* h, int v) { touch(h, ("mode:" + std::to_string(v)).c_str()).mode = v; }
float confidence(void* h) { return touch(h, "confidence").poll; }
void offset(void* h, float* x, float* y, float* z) {
    auto& e = touch(h, "offset"); *x = e.offset.x; *y = e.offset.y;
    if (!e.omitZ) *z = e.offset.z;
}
void set(void* h, float x, float y, float z, int w) {
    touch(h, "setOffset").current = {x,y,z,w};
}
void reset2(void* h) { touch(h, "altReset"); }
void mode2(void* h, int v) { touch(h,"altMode").mode=v; }
float confidence2(void* h) { return touch(h,"altConfidence").poll; }
void offset2(void* h, float* x, float* y, float* z) { auto& e=touch(h,"altOffset"); *x=e.offset.x; *y=e.offset.y; *z=e.offset.z; }
void set2(void* h, float x, float y, float z, int w) { touch(h,"altSet").current={x,y,z,w}; }
CalibrationOps callbacks() { return {reset, mode, confidence, offset, set}; }
void process(void* h, float, float, float, float, float, float, float) {
    ++touch(h, "process").processes;
}
void* create() { EXPECT(!engines.empty()); auto* e = engines.front(); e->alive = true; events.emplace_back("create"); return e; }
void destroy(void* h) { auto& e = touch(h, "destroy"); ++e.destroys; e.alive = false; }
void calRead(void* h, float* x, float* y, float* z) { touch(h,"readCal"); *x = *y = *z = 0; }
void gravityRead(void* h, float* x, float* y, float* z) { touch(h,"readGravity"); *x=0; *y=0; *z=1; }
struct Rig {
    Engine a, b;
    Key key{"USB\\BASE-A|17EF:6182",1,2,3,0};
    Calibration baseline{1.25f,-2.5f,0.0f,7};
    CalibrationOps ops = callbacks();
    CalibrationTransaction tx;
    Rig() { engines = {&a,&b}; events.clear(); }
    bool begin(std::uint64_t now=10000, std::uint64_t window=5000, std::uint64_t age=1500) {
        return tx.begin(key,&a,ops,baseline,now,window,age);
    }
    bool step(std::uint64_t now=10001, std::uint64_t receipt=10001, float c=0.995f) {
        a.poll = c;
        if (!tx.prepare(key,&a,ops,now,receipt)) return false;
        process(&a,0,0,0,0,0,0,0);
        return tx.observe(key,&a,ops,now,receipt,true);
    }
};
void order(std::initializer_list<const char*> names) {
    std::vector<std::string> expected; for (const auto* n : names) expected.emplace_back(n);
    EXPECT(events == expected);
}
void baselineRestored(Rig& r, CalibrationOutcome why) {
    EXPECT(!r.tx.active()); EXPECT(r.tx.outcome()==why); EXPECT(!r.tx.candidate());
    EXPECT(same(r.a.current,r.baseline)); EXPECT(r.a.mode==0);
}
void defaultsAndBegin() {
    Rig r;
    EXPECT(!r.tx.active()); EXPECT(r.tx.outcome()==CalibrationOutcome::None);
    EXPECT(r.tx.confidence()==0); EXPECT(!r.tx.candidate()); EXPECT(r.ops.complete());
    EXPECT(r.ops==callbacks()); EXPECT(!(r.ops!=callbacks()));
    for (int i=0;i<5;++i) {
        auto p=r.ops;
        if(i==0)p.reset=nullptr; if(i==1)p.setMode=nullptr;
        if(i==2)p.getConfidence=nullptr; if(i==3)p.getOffset=nullptr; if(i==4)p.setOffset=nullptr;
        EXPECT(!p.complete()); EXPECT(p!=r.ops);
        EXPECT(!r.tx.begin(r.key,&r.a,p,r.baseline,10000));
    }
    Key invalid=r.key;
    for(int i=0;i<4;++i) {
        invalid=r.key; if(i==0)invalid.physicalIdentity.clear(); if(i==1)invalid.power=0;
        if(i==2)invalid.rawGeneration=0; if(i==3)invalid.captureEpoch=0;
        EXPECT(!r.tx.begin(invalid,&r.a,r.ops,r.baseline,10000));
    }
    EXPECT(!r.tx.begin(r.key,nullptr,r.ops,r.baseline,10000));
    for (int i=0;i<4;++i) {
        auto b=r.baseline; const float bad=std::numeric_limits<float>::quiet_NaN();
        if(i==0)b.x=bad; if(i==1)b.y=bad; if(i==2)b.z=bad; if(i==3)b.weight=-1;
        EXPECT(!r.tx.begin(r.key,&r.a,r.ops,b,10000));
    }
    EXPECT(events.empty()); EXPECT(r.begin()); EXPECT(events.empty());
    EXPECT(!r.begin()); EXPECT(r.tx.active()); EXPECT(events.empty());
    r.tx.cancel(r.key,&r.a,r.ops); baselineRestored(r,CalibrationOutcome::Aborted);
    order({"setOffset","mode:0"});
}
void successOrder() {
    Rig r; EXPECT(r.begin()); EXPECT(events.empty()); EXPECT(r.step());
    EXPECT(r.tx.active()); EXPECT(r.tx.outcome()==CalibrationOutcome::None);
    EXPECT(r.tx.candidate().has_value());
    if (r.tx.candidate()) { const auto c=*r.tx.candidate(); EXPECT(c.x==0.125f&&c.y==-0.25f&&c.z==0.5f&&c.weight==9); }
    EXPECT(same(r.a.current,r.baseline)); EXPECT(r.a.mode==3); EXPECT(r.tx.confidence()==0.995f);
    order({"reset","mode:3","process","confidence","offset"});
    const auto count=events.size(); EXPECT(!r.tx.prepare(r.key,&r.a,r.ops,10002,10002)); EXPECT(events.size()==count);
    EXPECT(r.tx.finish(r.key,&r.a,r.ops,true)); EXPECT(!r.tx.active()); EXPECT(r.tx.outcome()==CalibrationOutcome::Succeeded);
    EXPECT(r.a.mode==0); EXPECT(r.a.current.weight==9&&r.a.current.x==0.125f);
    order({"reset","mode:3","process","confidence","offset","setOffset","mode:0"});
    const auto done=events.size(); EXPECT(!r.tx.finish(r.key,&r.a,r.ops,true));
    EXPECT(!r.tx.observe(r.key,&r.a,r.ops,10003,10001,true));
    r.tx.cancel(r.key,&r.a,r.ops); EXPECT(events.size()==done);
    EXPECT(r.tx.outcome()==CalibrationOutcome::Succeeded);
}
void receiptAuthorization() {
    Rig r; EXPECT(r.begin()); EXPECT(r.step(10001,10001,0.5f));
    const auto first=events.size();
    EXPECT(!r.tx.prepare(r.key,&r.a,r.ops,10002,10001));
    EXPECT(!r.tx.prepare(r.key,&r.a,r.ops,10002,10000));
    EXPECT(!r.tx.observe(r.key,&r.a,r.ops,10002,10001,true));
    EXPECT(events.size()==first); EXPECT(r.a.processes==1); EXPECT(!r.tx.candidate());
    EXPECT(r.tx.prepare(r.key,&r.a,r.ops,10003,10003));
    EXPECT(!r.tx.observe(r.key,&r.a,r.ops,10003,10002,true));
    EXPECT(!r.tx.observe(r.key,&r.a,r.ops,10003,10003,true));
    EXPECT(events.size()==first); // mismatched observe consumed its authorization
    EXPECT(!r.tx.prepare(r.key,&r.a,r.ops,10003,10003));
    EXPECT(r.step(10004,10004,0.75f));
    EXPECT(r.a.processes==2); EXPECT(r.tx.confidence()==0.75f);
    EXPECT(std::count(events.begin(),events.end(),"reset")==1);
    EXPECT(std::count(events.begin(),events.end(),"mode:3")==1);
    EXPECT(!r.tx.observe(r.key,&r.a,r.ops,10004,10004,false));
    EXPECT(r.tx.active()); EXPECT(r.tx.outcome()==CalibrationOutcome::None);
    r.tx.cancel(r.key,&r.a,r.ops);
}
void receiptAge() {
    { Rig r; EXPECT(r.begin());
      for(auto receipt : {std::uint64_t{0},std::uint64_t{10001},std::uint64_t{8499}}) {
          EXPECT(!r.tx.prepare(r.key,&r.a,r.ops,10000,receipt)); EXPECT(events.empty());
      }
      EXPECT(r.tx.prepare(r.key,&r.a,r.ops,10000,8500));
      EXPECT(r.tx.observe(r.key,&r.a,r.ops,10000,8500,true)); EXPECT(r.tx.confidence()==0.5f);
    }
    { Rig r; EXPECT(r.begin(10000,5000,0));
      EXPECT(!r.tx.prepare(r.key,&r.a,r.ops,10000,9999)); EXPECT(events.empty());
      EXPECT(r.tx.prepare(r.key,&r.a,r.ops,10001,10001));
      EXPECT(!r.tx.observe(r.key,&r.a,r.ops,10002,10001,true));
      EXPECT(std::count(events.begin(),events.end(),"confidence")==0); EXPECT(r.tx.active());
    }
    { Rig r; EXPECT(r.begin()); EXPECT(r.tx.prepare(r.key,&r.a,r.ops,10001,10001));
      EXPECT(!r.tx.prepare(r.key,&r.a,r.ops,10002,0));
      EXPECT(!r.tx.observe(r.key,&r.a,r.ops,10002,10001,true));
      EXPECT(std::count(events.begin(),events.end(),"confidence")==0);
    }
    { Rig r; EXPECT(r.begin()); EXPECT(r.tx.prepare(r.key,&r.a,r.ops,10001,10001));
      EXPECT(!r.tx.observe(r.key,&r.a,r.ops,11502,10001,true)); EXPECT(r.tx.active());
      EXPECT(std::count(events.begin(),events.end(),"confidence")==0);
    }
}
void strictConfidence() {
    for(int pct=0;pct<=100;++pct) {
        Rig r; EXPECT(r.begin()); const float c=static_cast<float>(pct)/100.0f;
        EXPECT(r.step(10001,10001,c)); EXPECT(r.tx.confidence()==c);
        EXPECT(r.tx.candidate().has_value()==(pct==100));
        EXPECT(std::count(events.begin(),events.end(),"offset")== (pct==100 ? 1 : 0));
        if(pct==100) { EXPECT(r.tx.candidate()->weight==10); EXPECT(r.tx.finish(r.key,&r.a,r.ops,true)); }
    }
    for (const float c : {0.99f, std::nextafter(0.99f,0.0f), std::nextafter(0.99f,1.0f), 0.995f, 1.0f, -0.0f}) {
        Rig r; EXPECT(r.begin()); EXPECT(r.step(10001,10001,c));
        const bool ready=c==std::nextafter(0.99f,1.0f)||c==0.995f||c==1.0f;
        EXPECT(r.tx.candidate().has_value()==ready);
        if(ready) EXPECT(r.tx.candidate()->weight==(c==1.0f?10:9));
    }
}
void invalidConfidence() {
    for(float c : {std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),
                  -std::numeric_limits<float>::infinity(),-0.001f,1.001f}) {
        Rig r; EXPECT(r.begin()); EXPECT(!r.step(10001,10001,c));
        baselineRestored(r,CalibrationOutcome::InvalidConfidence);
        EXPECT(std::isfinite(r.tx.confidence()));
        order({"reset","mode:3","process","confidence","setOffset","mode:0"});
    }
}
void offsetsAndZero() {
    { Rig r; r.baseline={0,0,0,0}; r.a.current=r.baseline; r.a.offset={0,0,0,0};
      EXPECT(r.begin()); EXPECT(r.step()); EXPECT(r.tx.candidate().has_value());
      EXPECT(r.tx.finish(r.key,&r.a,r.ops,true)); EXPECT(r.a.current.x==0&&r.a.current.y==0&&r.a.current.z==0);
    }
    for(int axis=0;axis<3;++axis) for(float bad : {std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}) {
        Rig r; if(axis==0)r.a.offset.x=bad; if(axis==1)r.a.offset.y=bad; if(axis==2)r.a.offset.z=bad;
        EXPECT(r.begin()); EXPECT(!r.step()); baselineRestored(r,CalibrationOutcome::InvalidOffset);
        order({"reset","mode:3","process","confidence","offset","setOffset","mode:0"});
    }
    { Rig r; r.a.omitZ=true; EXPECT(r.begin()); EXPECT(!r.step()); baselineRestored(r,CalibrationOutcome::InvalidOffset); }
}
void timeoutAndClock() {
    { Rig r; EXPECT(r.begin()); EXPECT(r.step(14999,14999,0.5f));
      EXPECT(!r.tx.prepare(r.key,&r.a,r.ops,15000,15000)); baselineRestored(r,CalibrationOutcome::TimedOut);
    }
    { Rig r; EXPECT(r.begin()); EXPECT(r.tx.prepare(r.key,&r.a,r.ops,14999,14999));
      EXPECT(!r.tx.observe(r.key,&r.a,r.ops,15000,14999,true)); baselineRestored(r,CalibrationOutcome::TimedOut);
      EXPECT(std::count(events.begin(),events.end(),"confidence")==0);
    }
    { Rig r; EXPECT(r.begin()); EXPECT(!r.tx.prepare(r.key,&r.a,r.ops,9999,9999));
      baselineRestored(r,CalibrationOutcome::ClockRegressed); order({"setOffset","mode:0"});
    }
    { Rig r; EXPECT(r.begin()); EXPECT(r.tx.prepare(r.key,&r.a,r.ops,10020,10020));
      EXPECT(!r.tx.observe(r.key,&r.a,r.ops,10019,10020,true)); baselineRestored(r,CalibrationOutcome::ClockRegressed);
    }
    { Rig r; EXPECT(r.begin()); EXPECT(!r.tx.prepare(r.key,&r.a,r.ops,10020,0));
      EXPECT(!r.tx.prepare(r.key,&r.a,r.ops,10019,10019)); baselineRestored(r,CalibrationOutcome::ClockRegressed);
    }
    { Rig r; EXPECT(r.begin(10000,0)); EXPECT(!r.tx.prepare(r.key,&r.a,r.ops,10000,10000));
      baselineRestored(r,CalibrationOutcome::TimedOut);
    }
    { Rig r; const auto top=std::numeric_limits<std::uint64_t>::max();
      EXPECT(r.begin(top-1000,5000)); EXPECT(r.step(top,top,0.5f)); EXPECT(r.tx.active());
    }
    { Rig r; EXPECT(r.begin(0)); EXPECT(!r.tx.prepare(r.key,&r.a,r.ops,0,0)); EXPECT(r.step(1,1,0.5f)); }
}
void sourceRetirement() {
    // Each independent fence is tested at prepare/observe/finish/cancel, both
    // before initialization and after a candidate exists. No replacement call.
    for(int change=0;change<17;++change) for(int operation=0;operation<4;++operation) for(int phase=0;phase<2;++phase) {
        Rig r; EXPECT(r.begin()); if(phase) EXPECT(r.step());
        Key current=r.key; void* handle=&r.a; auto ops=r.ops;
        switch(change) {
        case 0:current.physicalIdentity="USB\\BASE-B|17EF:6182";break;
        case 1:++current.power;break; case 2:++current.rawGeneration;break;
        case 3:++current.captureEpoch;break; case 4:current.sensorIndex=1;break;
        case 5:handle=&r.b;break; case 6:handle=nullptr;break;
        case 7:ops.reset=reset2;break; case 8:ops.setMode=mode2;break;
        case 9:ops.getConfidence=confidence2;break; case 10:ops.getOffset=offset2;break;
        case 11:ops.setOffset=set2;break; case 12:ops.reset=nullptr;break;
        case 13:ops.setMode=nullptr;break; case 14:ops.getConfidence=nullptr;break;
        case 15:ops.getOffset=nullptr;break; case 16:ops.setOffset=nullptr;break;
        }
        const auto count=events.size();
        if(operation==0) EXPECT(!r.tx.prepare(current,handle,ops,10002,10002));
        if(operation==1) EXPECT(!r.tx.observe(current,handle,ops,10002,10001,true));
        if(operation==2) EXPECT(!r.tx.finish(current,handle,ops,true));
        if(operation==3) r.tx.cancel(current,handle,ops);
        EXPECT(!r.tx.active()); EXPECT(r.tx.outcome()==CalibrationOutcome::SourceRetired);
        EXPECT(!r.tx.candidate()); EXPECT(events.size()==count);
        EXPECT(r.b.mode==0); EXPECT(same(r.b.current,r.baseline));
        EXPECT(!r.tx.prepare(r.key,&r.a,r.ops,10003,10003));
        EXPECT(!r.tx.finish(r.key,&r.a,r.ops,true)); EXPECT(events.size()==count);
    }
}
void processAndPersistenceFailure() {
    { Rig r; EXPECT(r.begin()); EXPECT(r.tx.prepare(r.key,&r.a,r.ops,10001,10001));
      EXPECT(!r.tx.observe(r.key,&r.a,r.ops,10001,10001,false));
      baselineRestored(r,CalibrationOutcome::ProcessFailed);
      order({"reset","mode:3","setOffset","mode:0"});
    }
    { Rig r; EXPECT(r.begin()); EXPECT(r.step());
      EXPECT(!r.tx.finish(r.key,&r.a,r.ops,false)); baselineRestored(r,CalibrationOutcome::PersistFailed);
      order({"reset","mode:3","process","confidence","offset","setOffset","mode:0"});
    }
    { Rig r; EXPECT(r.begin()); EXPECT(!r.tx.finish(r.key,&r.a,r.ops,true)); EXPECT(r.tx.active()); EXPECT(events.empty());
      EXPECT(!r.tx.finish(r.key,&r.a,r.ops,false)); baselineRestored(r,CalibrationOutcome::PersistFailed);
    }
    { Rig r; EXPECT(r.begin()); EXPECT(r.step()); r.tx.cancel(r.key,&r.a,r.ops);
      baselineRestored(r,CalibrationOutcome::Aborted); const auto count=events.size();
      r.tx.cancel(r.key,&r.a,r.ops); EXPECT(events.size()==count);
      EXPECT(r.begin(20000)); EXPECT(r.tx.confidence()==0); EXPECT(!r.tx.candidate()); EXPECT(r.step(20001,20001,1.0f));
      EXPECT(r.tx.finish(r.key,&r.a,r.ops,true)); EXPECT(r.a.current.weight==10);
    }
}
void sideSwitchAndSleep() {
    for(int change=0;change<3;++change) {
        Rig r; EXPECT(r.begin()); EXPECT(r.step()); const auto count=events.size();
        auto next=r.key; if(change==0)++next.power; if(change==1)++next.captureEpoch;
        if(change==2){next.sensorIndex=1;++next.captureEpoch;}
        r.tx.cancel(next,&r.b,r.ops,CalibrationOutcome::SourceRetired);
        EXPECT(r.tx.outcome()==CalibrationOutcome::SourceRetired); EXPECT(events.size()==count);
        EXPECT(!r.tx.finish(r.key,&r.a,r.ops,true));
        CalibrationTransaction fresh; r.b.offset={-7.0f,8.0f,0.0f,0}; r.b.poll=1;
        EXPECT(fresh.begin(next,&r.b,r.ops,r.baseline,20000));
        EXPECT(fresh.prepare(next,&r.b,r.ops,20001,20001));
        EXPECT(fresh.observe(next,&r.b,r.ops,20001,20001,true));
        EXPECT(fresh.candidate()->x==-7.0f); EXPECT(fresh.finish(next,&r.b,r.ops,true));
        EXPECT(r.b.current.x==-7&&r.b.current.weight==10); EXPECT(same(r.a.current,r.baseline));
    }
}
void noDestructorCallbacks() {
    Rig r;
    { CalibrationTransaction borrowed; EXPECT(borrowed.begin(r.key,&r.a,r.ops,r.baseline,10000));
      EXPECT(borrowed.prepare(r.key,&r.a,r.ops,10001,10001)); r.a.alive=false; }
    order({"reset","mode:3"}); EXPECT(r.a.destroys==0);
    r.a.alive=true;
    const auto count=events.size();
    { CalibrationTransaction opaque; auto* sentinel=reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x77));
      EXPECT(opaque.begin(r.key,sentinel,r.ops,r.baseline,10000));
      opaque.cancel(r.key,&r.a,r.ops); EXPECT(opaque.outcome()==CalibrationOutcome::SourceRetired); }
    EXPECT(events.size()==count); EXPECT(r.a.destroys==0);
}
void actualSessionBorrowing() {
    Rig r; Session session;
    const Ops ownerOps{create,destroy,mode,set,process}; const ReadOps reads{calRead,gravityRead,nullptr,nullptr};
    EXPECT(session.select(r.key,ownerOps,reads,r.baseline,10.0));
    void* borrowed=session.handleFor(r.key); EXPECT(borrowed==&r.a); events.clear();
    {
        CalibrationTransaction tx; EXPECT(tx.begin(r.key,borrowed,r.ops,r.baseline,10000));
        EXPECT(tx.prepare(r.key,borrowed,r.ops,10001,10001));
        const auto result=session.process(r.key,RawSixAxes{},10.001);
        EXPECT(result.has_value()); if(result)EXPECT(result->orientationFinite);
        r.a.poll=1; r.a.offset={0,0,0,0};
        EXPECT(tx.observe(r.key,borrowed,r.ops,10001,10001,result.has_value()));
        EXPECT(tx.finish(r.key,borrowed,r.ops,true));
        order({"reset","mode:3","process","readCal","readGravity","confidence","offset","setOffset","mode:0"});
    }
    EXPECT(session.active()); EXPECT(session.handleFor(r.key)==borrowed); EXPECT(r.a.destroys==0);
    EXPECT(session.process(r.key,RawSixAxes{},10.002).has_value());
    session.revoke(); EXPECT(r.a.destroys==1); EXPECT(!session.active());
}
} // namespace
int main() {
    const std::pair<const char*, void(*)()> cases[]{
        {"defaults-begin-and-ops",defaultsAndBegin}, {"successful-callback-order",successOrder},
        {"one-receipt-one-process",receiptAuthorization}, {"sample-freshness",receiptAge},
        {"strict-confidence-boundary",strictConfidence}, {"invalid-confidence",invalidConfidence},
        {"offset-finiteness-and-zero",offsetsAndZero}, {"timeout-and-clock",timeoutAndClock},
        {"exact-live-source-retirement",sourceRetirement}, {"process-persistence-rollback",processAndPersistenceFailure},
        {"sleep-side-new-session",sideSwitchAndSleep}, {"borrowed-destructor-no-callbacks",noDestructorCallbacks},
        {"actual-session-borrowing",actualSessionBorrowing}
    };
    for(const auto& test : cases) {
        scenario=test.first; const auto beforeChecks=checks,beforeFailures=failures; test.second();
        std::cout<<"CASE "<<scenario<<" checks="<<checks-beforeChecks<<" failures="<<failures-beforeFailures<<'\n';
    }
    std::cout<<"SUMMARY scenarios="<<std::size(cases)<<" checks="<<checks<<" failures="<<failures<<'\n';
    return failures ? 1 : 0;
}
