#include "controller_motion_session.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
using namespace ymcc::controller_motion;
static_assert(noexcept(std::declval<Session&>().invalidateCalibrationMode()));
static_assert(std::is_same_v<decltype(std::declval<const Session&>().calibrationModeFor(std::declval<const Key&>())),std::optional<int>>);
namespace {
std::uint64_t checks=0,failures=0,scenarios=0,events=0;
std::string currentCase;
std::vector<std::string> failedIds;
void check(bool value,const char* id) {
    ++checks;if(value)return;++failures;
    bool seen=false;for(const auto& existing:failedIds)if(existing==id)seen=true;
    if(!seen){failedIds.emplace_back(id);std::cout<<"FAIL|"<<id<<"|case="<<currentCase<<'\n';}
}
#define CHECK(value,id) check(static_cast<bool>(value),id)
void begin(std::string name){++scenarios;currentCase=std::move(name);}
struct Slot {
    bool alive=false;unsigned index=0,generation=0;int mode=-77;
    Calibration offset;std::array<float,7> lastProcess{};
};
struct Event {
    std::string kind;int bank=0;unsigned index=0,generation=0;int mode=0;
};
struct Sink {
    std::array<Slot,32> slots{};std::vector<Event> trace;
    unsigned allocated=0,generation=0,violations=0;bool reuse=true,failCreate=false;
    MotionVector3 calibrated{0,0,90},gravity{0,0,1};
    Slot* live(void* pointer){for(auto& slot:slots)if(pointer==&slot){if(slot.alive)return &slot;break;}++violations;return nullptr;}
    void record(const char* kind,int bank,Slot* slot,int mode=0) {
        Event event{kind,bank,slot?slot->index:999,slot?slot->generation:0,mode};trace.push_back(event);++events;
        std::cout<<"EVENT {\"case\":\""<<currentCase<<"\",\"kind\":\""<<kind<<"\",\"bank\":"<<bank
            <<",\"slot\":"<<event.index<<",\"generation\":"<<event.generation<<",\"mode\":"<<mode<<"}\n";
    }
    unsigned count(const char* kind) const{unsigned result=0;for(const auto& e:trace)if(e.kind==kind)++result;return result;}
    void clean(){CHECK(violations==0,"callback.no-invalid-handle-use");for(const auto& slot:slots)CHECK(!slot.alive,"owner.no-live-handle-at-end");}
};
Sink* sink=nullptr;
struct Fixture { Sink state;Fixture(){sink=&state;}~Fixture(){state.clean();sink=nullptr;} };
void* createIn(int bank) {
    if(sink->failCreate){sink->failCreate=false;sink->record("create-failed",bank,nullptr);return nullptr;}
    Slot* selected=nullptr;
    if(sink->reuse)for(unsigned i=0;i<sink->allocated;++i)if(!sink->slots[i].alive){selected=&sink->slots[i];break;}
    if(!selected && sink->allocated<sink->slots.size()) {selected=&sink->slots[sink->allocated];selected->index=sink->allocated++;}
    if(!selected){++sink->violations;return nullptr;}
    selected->alive=true;selected->generation=++sink->generation;selected->mode=-77;selected->offset={};
    sink->record("create",bank,selected);return selected;
}
void destroyIn(int bank,void* pointer){auto* slot=sink->live(pointer);sink->record("destroy",bank,slot);if(slot)slot->alive=false;}
void modeIn(int bank,void* pointer,int mode){auto* slot=sink->live(pointer);sink->record("mode",bank,slot,mode);if(slot)slot->mode=mode;}
void offsetIn(int bank,void* pointer,float x,float y,float z,int weight){auto* slot=sink->live(pointer);sink->record("offset",bank,slot,weight);if(slot)slot->offset={x,y,z,weight};}
void processIn(int bank,void* pointer,float gx,float gy,float gz,float ax,float ay,float az,float dt){
    auto* slot=sink->live(pointer);sink->record("process",bank,slot);if(slot)slot->lastProcess={gx,gy,gz,ax,ay,az,dt};
}
void calIn(int bank,void* pointer,float* x,float* y,float* z){sink->record("cal",bank,sink->live(pointer));*x=sink->calibrated.x;*y=sink->calibrated.y;*z=sink->calibrated.z;}
void gravityIn(int bank,void* pointer,float* x,float* y,float* z){sink->record("gravity",bank,sink->live(pointer));*x=sink->gravity.x;*y=sink->gravity.y;*z=sink->gravity.z;}
void spaceIn(int bank,void* pointer,float* x,float* y,float scale,bool player){sink->record(player?"player":"world",bank,sink->live(pointer));*x=scale;*y=-scale;}
void* create0(){return createIn(0);}void* create1(){return createIn(1);}
void destroy0(void* p){destroyIn(0,p);}void destroy1(void* p){destroyIn(1,p);}
void mode0(void* p,int m){modeIn(0,p,m);}void mode1(void* p,int m){modeIn(1,p,m);}
void offset0(void* p,float x,float y,float z,int w){offsetIn(0,p,x,y,z,w);}void offset1(void* p,float x,float y,float z,int w){offsetIn(1,p,x,y,z,w);}
void process0(void* p,float a,float b,float c,float d,float e,float f,float dt){processIn(0,p,a,b,c,d,e,f,dt);}
void process1(void* p,float a,float b,float c,float d,float e,float f,float dt){processIn(1,p,a,b,c,d,e,f,dt);}
void cal0(void* p,float* x,float* y,float* z){calIn(0,p,x,y,z);}void cal1(void* p,float* x,float* y,float* z){calIn(1,p,x,y,z);}
void gravity0(void* p,float* x,float* y,float* z){gravityIn(0,p,x,y,z);}void gravity1(void* p,float* x,float* y,float* z){gravityIn(1,p,x,y,z);}
void player0(void* p,float* x,float* y,float f){spaceIn(0,p,x,y,f,true);}void player1(void* p,float* x,float* y,float f){spaceIn(1,p,x,y,f,true);}
void world0(void* p,float* x,float* y,float f){spaceIn(0,p,x,y,f,false);}void world1(void* p,float* x,float* y,float f){spaceIn(1,p,x,y,f,false);}
Ops ops(){return {create0,destroy0,mode0,offset0,process0};}
ReadOps reads(){return {cal0,gravity0,player0,world0};}
Key key(){return {"controller/physical-A",11,22,33,1};}
Calibration calibration(){return {1.25f,-2.5f,3.75f,4};}
RawSixAxes raw(){return {1,-2,3,-4,5,-6};}
bool near(float a,float b){return std::isfinite(a)&&std::isfinite(b)&&std::fabs(a-b)<=0.000001f;}
bool sameQuaternion(const MotionQuaternion& a,const MotionQuaternion& b){return near(a.w,b.w)&&near(a.x,b.x)&&near(a.y,b.y)&&near(a.z,b.z);}
void known(const Session& session,const Key& selected,int mode,const char* id="query.known-mode"){
    const auto value=session.calibrationModeFor(selected);CHECK(value&&*value==mode,id);
}
void noSideEffects(const Sink& state,std::size_t before,const char* id){CHECK(state.trace.size()==before,id);}
void manualInitialization(const Session& session,const Key& selected,const Sink& state,std::size_t offset) {
    CHECK(state.trace.size()==offset+3,"select.initialization-exactly-create-manual-offset");
    if(state.trace.size()>=offset+3){CHECK(state.trace[offset].kind=="create","select.create-first");
        CHECK(state.trace[offset+1].kind=="mode"&&state.trace[offset+1].mode==0,"select.manual-before-offset");
        CHECK(state.trace[offset+2].kind=="offset","select.offset-after-manual");}
    known(session,selected,0,"select.cache-manual-zero");
}
void defaultAndInitialRequests(){
    for(const bool automatic:{false,true}){
        begin(automatic?"initial-automatic-true":"initial-automatic-false");Fixture fixture;Session session;const auto selected=key();
        CHECK(!session.calibrationModeFor(selected),"query.default-unknown");
        const auto empty=fixture.state.trace.size();session.invalidateCalibrationMode();
        CHECK(!session.reconcileCalibrationMode(selected,automatic),"mode.unselected-rejected");
        noSideEffects(fixture.state,empty,"mode.unselected-no-callback");
        CHECK(session.select(selected,ops(),reads(),calibration(),10),"select.initial");manualInitialization(session,selected,fixture.state,empty);
        const auto before=fixture.state.count("mode");
        CHECK(session.reconcileCalibrationMode(selected,automatic),"mode.initial-reconcile-success");
        known(session,selected,automatic?3:0,"mode.initial-bool-mapping");
        CHECK(fixture.state.count("mode")==before+(automatic?1:0),"mode.initial-write-count");
        const auto modeCount=fixture.state.count("mode"),offsetCount=fixture.state.count("offset");
        for(unsigned frame=0;frame<32;++frame){
            CHECK(session.reconcileCalibrationMode(selected,automatic),"mode.repeated-reconcile-success");
            CHECK(session.process(selected,raw(),10.125+frame*0.125),"process.repeated-frame");
            known(session,selected,automatic?3:0);
        }
        CHECK(fixture.state.count("mode")==modeCount,"mode.repeated-no-write");
        CHECK(fixture.state.count("offset")==offsetCount,"mode.repeated-no-offset");
        session.revoke();CHECK(!session.calibrationModeFor(selected),"query.retired-unknown");
    }
    begin("process-alone-never-enables-auto");Fixture fixture;Session session;const auto selected=key();
    CHECK(session.select(selected,ops(),reads(),calibration(),0),"select.initial");
    for(int i=0;i<8;++i)CHECK(session.process(selected,raw(),(i+1)*0.125),"process.default-behavior");
    known(session,selected,0);CHECK(fixture.state.count("mode")==1,"mode.process-no-implicit-auto");
}
void togglesAndInvalidation(){
    begin("toggle-invalidate-and-unchanged-selection");Fixture fixture;Session session;const auto selected=key();
    CHECK(session.select(selected,ops(),reads(),calibration(),10),"select.initial");
    for(const int desired:{3,0,3,0}){const auto before=fixture.state.trace.size();
        CHECK(session.reconcileCalibrationMode(selected,desired==3),"mode.toggle-success");known(session,selected,desired);
        CHECK(fixture.state.trace.size()==before+1,"mode.toggle-one-callback");
        CHECK(fixture.state.trace.back().kind=="mode"&&fixture.state.trace.back().mode==desired,"mode.toggle-callback-value");
    }
    for(const bool desired:{false,true}){
        auto* slot=fixture.state.live(session.handleFor(selected));if(slot)slot->mode=17; // External manual transaction simulation.
        const auto before=fixture.state.trace.size();session.invalidateCalibrationMode();
        CHECK(!session.calibrationModeFor(selected),"invalidate.cache-unknown");
        noSideEffects(fixture.state,before,"invalidate.no-native-callback");
        CHECK(session.select(selected,ops(),reads(),Calibration{99,98,97,96},999),"select.unchanged-unknown");
        CHECK(!session.calibrationModeFor(selected),"select.unchanged-preserves-unknown");
        noSideEffects(fixture.state,before,"select.unchanged-unknown-no-callback");
        CHECK(session.reconcileCalibrationMode(selected,desired),"invalidate.reconcile-restores");
        known(session,selected,desired?3:0);
        CHECK(fixture.state.trace.size()==before+1,"invalidate.reconcile-one-write");
        CHECK(slot&&slot->mode==(desired?3:0),"invalidate.native-mode-restored");
    }
    CHECK(session.reconcileCalibrationMode(selected,true),"mode.enable-for-unchanged");
    const auto before=fixture.state.trace.size();
    CHECK(session.select(selected,ops(),reads(),Calibration{44,55,66,77},999),"select.unchanged-known");
    known(session,selected,3,"select.unchanged-preserves-auto");
    CHECK(session.reconcileCalibrationMode(selected,true),"mode.unchanged-reconcile");
    noSideEffects(fixture.state,before,"select.unchanged-preserves-no-write");
    CHECK(fixture.state.count("offset")==1,"mode.offset-not-reapplied");
}
void wrongKeys(){
    for(int field=0;field<8;++field){begin("wrong-key-field-"+std::to_string(field));Fixture fixture;Session session;const auto selected=key();
        CHECK(session.select(selected,ops(),reads(),calibration(),0),"select.initial");
        CHECK(session.reconcileCalibrationMode(selected,true),"mode.enable");auto wrong=selected;
        if(field==0)wrong.physicalIdentity+="/other";if(field==1)++wrong.power;if(field==2)++wrong.rawGeneration;
        if(field==3)++wrong.captureEpoch;if(field==4)++wrong.sensorIndex;if(field==5)wrong.physicalIdentity.clear();
        if(field==6)wrong.power=0;if(field==7)wrong.captureEpoch=0;
        const auto before=fixture.state.trace.size();
        CHECK(!session.calibrationModeFor(wrong),"query.wrong-key-unknown");
        CHECK(!session.reconcileCalibrationMode(wrong,false)&&!session.reconcileCalibrationMode(wrong,true),"mode.wrong-key-rejected");
        noSideEffects(fixture.state,before,"mode.wrong-key-no-write");known(session,selected,3,"mode.wrong-key-does-not-alter-cache");
    }
}
void retirementAndFailure(){
    for(int failure=0;failure<7;++failure){begin("retire-or-failed-selection-"+std::to_string(failure));Fixture fixture;Session session;const auto selected=key();
        CHECK(session.select(selected,ops(),reads(),calibration(),0),"select.initial");CHECK(session.reconcileCalibrationMode(selected,true),"mode.enable");
        auto next=selected;++next.captureEpoch;
        if(failure==0)session.revoke();
        if(failure==1)session.clear();
        if(failure==2){auto bad=ops();bad.setMode=nullptr;CHECK(!session.select(next,bad,reads(),calibration(),1),"select.incomplete-ops-rejected");}
        if(failure==3){auto bad=reads();bad.getGravity=nullptr;CHECK(!session.select(next,ops(),bad,calibration(),1),"select.incomplete-reads-rejected");}
        if(failure==4){fixture.state.failCreate=true;CHECK(!session.select(next,ops(),reads(),calibration(),1),"select.create-failure-rejected");}
        if(failure==5){auto bad=calibration();bad.weight=-1;CHECK(!session.select(next,ops(),reads(),bad,1),"select.invalid-calibration-rejected");}
        if(failure==6){auto bad=next;bad.power=0;CHECK(!session.select(bad,ops(),reads(),calibration(),1),"select.invalid-key-rejected");}
        CHECK(!session.active()&&!session.handleFor(selected),"retire.no-current-owner");
        const auto before=fixture.state.trace.size();session.invalidateCalibrationMode();
        CHECK(!session.calibrationModeFor(selected)&&!session.calibrationModeFor(next),"query.failed-and-retired-unknown");
        CHECK(!session.reconcileCalibrationMode(selected,true)&&!session.reconcileCalibrationMode(next,false),"mode.retired-or-failed-no-revival");
        noSideEffects(fixture.state,before,"mode.retired-or-failed-no-write");
        CHECK(!session.select(selected,ops(),reads(),calibration(),2),"select.retired-token-not-reusable");
        if(failure==4)CHECK(!session.select(next,ops(),reads(),calibration(),2),"select.failed-create-token-not-reusable");
        auto fresh=next;++fresh.captureEpoch;const auto mark=fixture.state.trace.size();
        CHECK(session.select(fresh,ops(),reads(),calibration(),3),"select.fresh-token-recovers");manualInitialization(session,fresh,fixture.state,mark);
        CHECK(session.reconcileCalibrationMode(fresh,true),"mode.fresh-token-automatic-recovers");known(session,fresh,3);
    }
}
void callbackReplacement(){
    for(const bool reuse:{false,true})for(int field=0;field<12;++field){
        begin(std::string("replace-")+(reuse?"reused-address-":"different-address-")+std::to_string(field));Fixture fixture;fixture.state.reuse=reuse;
        Session session;const auto selected=key();const auto originalOps=ops();const auto originalReads=reads();
        CHECK(session.select(selected,originalOps,originalReads,calibration(),0),"select.initial");
        CHECK(session.reconcileCalibrationMode(selected,true),"mode.enable");
        void* old=session.handleFor(selected);const auto oldGeneration=fixture.state.live(old)->generation;
        auto replacementOps=originalOps;auto replacementReads=originalReads;
        if(field==0)replacementOps.create=create1;if(field==1)replacementOps.destroy=destroy1;
        if(field==2)replacementOps.setMode=mode1;if(field==3)replacementOps.setOffset=offset1;if(field==4)replacementOps.process=process1;
        if(field==5)replacementReads.getCal=cal1;if(field==6)replacementReads.getGravity=gravity1;
        if(field==7)replacementReads.getPlayer=player1;if(field==8)replacementReads.getWorld=world1;
        if(field==9)replacementReads.getPlayer=nullptr;if(field==10)replacementReads.getWorld=nullptr;
        if(field==11){replacementOps={create1,destroy1,mode1,offset1,process1};replacementReads={cal1,gravity1,player1,world1};}
        const auto mark=fixture.state.trace.size();
        CHECK(session.select(selected,replacementOps,replacementReads,Calibration{5,6,7,8},10),"select.callback-replacement");
        auto* handle=fixture.state.live(session.handleFor(selected));CHECK(handle,"replace.live-handle");
        CHECK((session.handleFor(selected)==old)==reuse,"replace.pointer-reuse-is-explicit");
        CHECK(handle&&handle->generation>oldGeneration,"replace.new-owner-generation");
        CHECK(fixture.state.trace.size()==mark+4,"replace.destroy-create-manual-offset-exact");
        if(fixture.state.trace.size()>=mark+4){CHECK(fixture.state.trace[mark].kind=="destroy"&&fixture.state.trace[mark].bank==0,"replace.old-destroy-callback");
            CHECK(fixture.state.trace[mark+1].kind=="create"&&fixture.state.trace[mark+1].bank==((field==0||field==11)?1:0),"replace.new-create-callback");
            CHECK(fixture.state.trace[mark+2].kind=="mode"&&fixture.state.trace[mark+2].mode==0&&fixture.state.trace[mark+2].bank==((field==2||field==11)?1:0),"replace.new-mode-callback-manual");
            CHECK(fixture.state.trace[mark+3].kind=="offset","replace.offset-reapplied-on-new-owner");}
        known(session,selected,0,"replace.cache-reset-to-manual");
        CHECK(session.reconcileCalibrationMode(selected,true),"replace.reconcile-auto");known(session,selected,3);
        CHECK(fixture.state.trace.back().bank==((field==2||field==11)?1:0)&&fixture.state.trace.back().mode==3,"replace.reconcile-uses-current-mode-callback");
        const auto result=session.process(selected,raw(),10.25);
        CHECK(result&&result->deltaSeconds==0.25,"replace.new-selection-time-initialized");
        CHECK(handle&&handle->offset.x==5&&handle->offset.weight==8,"replace.new-offset-retained");
        session.revoke();CHECK(!session.calibrationModeFor(selected),"replace.retired-cache-unknown");
        CHECK(fixture.state.trace.back().kind=="destroy"&&fixture.state.trace.back().bank==((field==1||field==11)?1:0),"replace.current-destroy-callback");
    }
}
void newKeySameAddress(){
    for(int field=0;field<5;++field){begin("new-key-reused-address-field-"+std::to_string(field));Fixture fixture;Session session;const auto selected=key();
        CHECK(session.select(selected,ops(),reads(),calibration(),0),"select.initial");CHECK(session.reconcileCalibrationMode(selected,true),"mode.enable");
        void* pointer=session.handleFor(selected);auto next=selected;
        if(field==0)next.physicalIdentity+="/B";if(field==1)++next.power;if(field==2)++next.rawGeneration;
        if(field==3)++next.captureEpoch;if(field==4)++next.sensorIndex;
        CHECK(session.select(next,ops(),reads(),calibration(),1),"select.new-key");
        CHECK(session.handleFor(next)==pointer,"new-key.actual-address-reuse");known(session,next,0,"new-key.initial-manual-cache");
        CHECK(!session.calibrationModeFor(selected),"new-key.old-key-query-unknown");
        const auto before=fixture.state.trace.size();CHECK(!session.reconcileCalibrationMode(selected,false),"new-key.old-key-cannot-write-reused-pointer");
        noSideEffects(fixture.state,before,"new-key.old-key-no-write");CHECK(session.reconcileCalibrationMode(next,true),"new-key.reconcile-auto");
    }
}
void stateContinuity(){
    begin("mode-only-keeps-offset-time-quaternion-and-native-axes");Fixture fixture;Session toggled,control;const auto selected=key();auto other=selected;other.physicalIdentity+="/control";
    CHECK(toggled.select(selected,ops(),reads(),calibration(),100),"select.initial");CHECK(control.select(other,ops(),reads(),calibration(),100),"select.control");
    auto* handle=fixture.state.live(toggled.handleFor(selected));const auto first=toggled.process(selected,raw(),100.25);const auto baseline=control.process(other,raw(),100.25);
    CHECK(first&&baseline,"state.initial-process");
    if(!first||!baseline)return;
    const float tangent=3.14159265358979323846f/16.0f;const float magnitude=std::sqrt(1+tangent*tangent);
    CHECK(near(first->orientation.w,0)&&near(first->orientation.x,tangent/magnitude)&&near(first->orientation.y,-1/magnitude)&&near(first->orientation.z,0),"state.one-step-quaternion-manual-pin");
    CHECK(first->deltaSeconds==0.25&&first->orientationFinite,"state.initial-dt-and-orientation");
    const auto creates=fixture.state.count("create"),offsets=fixture.state.count("offset"),processes=fixture.state.count("process");
    CHECK(toggled.reconcileCalibrationMode(selected,true),"mode.enable");
    CHECK(toggled.reconcileCalibrationMode(selected,true),"mode.repeat");
    toggled.invalidateCalibrationMode();CHECK(toggled.reconcileCalibrationMode(selected,false),"mode.restore-manual");
    CHECK(toggled.select(selected,ops(),reads(),Calibration{99,98,97,96},1000),"select.unchanged-state");
    CHECK(fixture.state.count("create")==creates&&fixture.state.count("offset")==offsets&&fixture.state.count("process")==processes,"state.mode-does-not-recreate-offset-or-process");
    const auto zero=toggled.process(selected,raw(),100.25);const auto controlZero=control.process(other,raw(),100.25);
    CHECK(zero&&zero->deltaSeconds==0,"state.mode-preserves-previous-seconds");
    CHECK(zero&&sameQuaternion(zero->orientation,first->orientation),"state.mode-preserves-quaternion-at-zero-dt");
    CHECK(zero&&controlZero&&sameQuaternion(zero->orientation,controlZero->orientation),"state.zero-dt-control-trajectory");
    CHECK(handle&&handle->lastProcess[6]==0,"state.native-zero-dt-forwarded");
    CHECK(toggled.reconcileCalibrationMode(selected,true),"mode.enable-again");
    const auto second=toggled.process(selected,raw(),100.75);const auto controlSecond=control.process(other,raw(),100.75);
    CHECK(second&&second->deltaSeconds==0.5,"state.mode-preserves-next-delta");
    CHECK(second&&controlSecond&&sameQuaternion(second->orientation,controlSecond->orientation),"state.nonzero-dt-control-trajectory");
    CHECK(handle&&handle->offset.x==1.25f&&handle->offset.y==-2.5f&&handle->offset.z==3.75f&&handle->offset.weight==4,"state.mode-preserves-original-offset");
    const auto before=fixture.state.count("mode");toggled.invalidateCalibrationMode();
    CHECK(toggled.process(selected,raw(),101),"state.unknown-cache-still-processes");
    CHECK(!toggled.calibrationModeFor(selected)&&fixture.state.count("mode")==before,"state.process-does-not-implicitly-reconcile-unknown");
}
} // namespace
int main(){defaultAndInitialRequests();togglesAndInvalidation();wrongKeys();retirementAndFailure();callbackReplacement();newKeySameAddress();stateContinuity();
    std::cout<<"SUMMARY cases="<<scenarios<<" checks="<<checks<<" failures="<<failures<<" events="<<events<<" failureIds="<<failedIds.size()<<'\n';return failures?1:0;}