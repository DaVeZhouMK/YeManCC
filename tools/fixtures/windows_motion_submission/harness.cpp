#include "windows_motion_submission.h"
#include "input_frame_submission.h"
#include "legion_motion_source.h"
#include "json.hpp"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
using json=nlohmann::json;
using ULONGLONG=std::uint64_t;
namespace wm=ymcc::windows_motion;
namespace fsb=ymcc::input::frameSubmission;
static ULONGLONG clockTick=100;
static ULONGLONG GetTickCount64(){return clockTick;}
static fsb::Current ambient;
static unsigned notes=0,writes=0;
static bool legionCurrent=true;
static bool g_inputHostActive=true;
static std::string g_inputHostLifecycle="active";
static fsb::Current inputHostFrameSubmissionCurrent(bool,bool){return ambient;}
static bool legionMotionSubmissionCurrent(const ymcc::legion_motion::Sample&,ULONGLONG) noexcept{return legionCurrent;}
static void inputHostFrameSubmissionNote(fsb::Verdict,const char*,bool){++notes;}
// @@FRAME_TYPE@@
static thread_local InputFrameSubmission* t_inputFrameSubmission=nullptr;
static thread_local const wm::Stamp* t_windowsMotionSubmission=nullptr;
// @@WINDOWS_SCOPE@@
// @@WINDOWS_HELPER@@
// @@GUARDS@@
static json beforeAssembly(InputFrameSubmission& frameSubmission){
// @@BEFORE_ASSEMBLY@@
    return {{"frameContextRetired",false}};
}
struct SensorOwner{std::atomic<std::uint64_t> activeBindingGeneration{11};};
static json failures=json::array(),failureGroups=json::object(),cases=json::array();
static unsigned checks=0,failureCount=0;
static std::string activeCase;
static void check(bool good,const char* group,const char* text){++checks;if(good)return;++failureCount;
    failureGroups[group]=failureGroups.value(group,0u)+1;if(failures.size()<70)failures.push_back({{"case",activeCase},{"group",group},{"detail",text}});}
static void reset(){clockTick=100;notes=writes=0;legionCurrent=true;
    ambient={7,1,3,4,2,9,true,false,true,1};t_inputFrameSubmission=nullptr;t_windowsMotionSubmission=nullptr;}
static InputFrameSubmission frame(bool neutral=false){
    InputFrameSubmission f;f.ticket={7,1,3,neutral};f.physicalSourceAdmitted=true;
    f.sample.live=true;f.sample.routeKnown=true;f.sample.identity=9;
    f.sample.window.readAttemptSequence=1;f.sample.window.readStartedTick=100;f.sample.window.readCompletedTick=100;
    f.sample.window.readPowerGeneration=1;f.sample.window.readTopologyGeneration=2;f.sample.window.readContextStable=true;
    return f;
}
template<class F>static void attach(F& frameSubmission,const wm::Stamp* value){
    if constexpr(requires{frameSubmission.windowsMotion;})frameSubmission.windowsMotion=value;
}
template<class F>static void copyOrigin(F& frameSubmission){
    if constexpr(requires{frameSubmission.windowsMotion;}){
// @@ORIGIN_COPY@@
    }
}
static bool request(InputFrameSubmission& f,const char* phase,json* response=nullptr){
    t_inputFrameSubmission=&f;json ignored;const bool ok=inputHostFrameSubmissionRequestCurrent("SUBMIT_FRAME",response?response:&ignored,phase);
    t_inputFrameSubmission=nullptr;return ok;
}
static bool dispatch(InputFrameSubmission& f,const std::function<void(int)>& event={}){
    if(event)event(0);if(!request(f,"before-envelope"))return false;
    if(event)event(1);if(!request(f,"before-pipe-write"))return false;
    if(event)event(2);const auto oldNotes=notes;const bool allowed=inputHostFrameSubmissionMayIssue(&f);
    check(notes==oldNotes,"issue-boundary","last issue guard performs no logging");
    if(!allowed)return false;++writes;return true;
}
template<class Callback>static void fromProducer(const std::shared_ptr<SensorOwner>& windowsMotionSensorOwner,
    std::uint64_t gyroBindingGeneration,bool controllerMotionRoute,bool gyroSubmitAllowed,Callback&& consume){
// @@PRODUCER_VIEW_BOUNDARY@@
// @@PRODUCER_SCOPE@@
    consume();
}
static void sourceAndOwnerCases(){
    activeCase="retained-source-lifetime";auto owner=std::make_shared<SensorOwner>();std::weak_ptr<SensorOwner> weak=owner;
    auto stamp=wm::retain(owner,11);check(wm::current(stamp),"stamp","retained source starts current");
    owner->activeBindingGeneration=0;check(!wm::current(stamp),"stamp","source retirement invalidates retained stamp");
    owner.reset();check(!weak.expired(),"ownership","alias retains original sensor cache, including its atomic counter");
    stamp.activeBinding.reset();check(weak.expired(),"ownership","retained cache is released after the frame scope");
    activeCase="missing-or-zero-source";
    wm::Stamp empty;check(!wm::current(empty),"stamp","missing source cannot assert a current binding");
    owner=std::make_shared<SensorOwner>();auto zero=wm::retain(owner,0);check(!wm::current(zero),"stamp","zero captured generation cannot assert current");
}
static void validAndRecoveredWindows(){
    reset();activeCase="current-windows-source";auto owner=std::make_shared<SensorOwner>();
    const auto stamp=wm::retain(owner,11);auto f=frame();attach(f,&stamp);
    const auto assembled=beforeAssembly(f);
    check(!assembled.value("frameContextRetired",false),"current-source","current Windows source passes pre-assembly");
    check(dispatch(f)&&writes==1&&!f.rejected,"current-source","current Windows source passes all actual write guards");
    reset();activeCase="new-binding-resumes";const auto old=wm::retain(owner,11);owner->activeBindingGeneration=12;
    f=frame();attach(f,&old);check(!dispatch(f)&&writes==0,"source-stale-recovery","retired processed sample is skipped");
    const auto next=wm::retain(owner,12);f=frame();attach(f,&next);const auto priorWrites=writes;
    check(dispatch(f)&&writes==priorWrites+1,"current-source","a newly processed current binding resumes the same target");
    reset();activeCase="no-motion-base-input";owner->activeBindingGeneration=0;f=frame();
    check(dispatch(f)&&writes==1,"unrelated","ordinary base-only frame continues without a Windows motion stamp");
}
static void individualGuards(){
    for(unsigned problem=0;problem<4;++problem){reset();activeCase="each-window-guard-"+std::to_string(problem);
        auto owner=std::make_shared<SensorOwner>();auto stamp=wm::retain(owner,11);
        if(problem==0)owner->activeBindingGeneration=12;
        if(problem==1)owner->activeBindingGeneration=0;
        if(problem==2)stamp.bindingGeneration=0;
        if(problem==3)stamp.activeBinding.reset();
        for(const char* phase:{"before-envelope","before-pipe-write"}){
            auto f=frame();attach(f,&stamp);json response;
            check(!request(f,phase,&response),"source-stale-request","each actual request boundary rejects retired Windows motion");
            check(f.rejected&&f.reason==fsb::Verdict::SourceRetired,"source-stale-state","source rejection records retirement instead of Host failure");
            if(f.rejected)check(response.value("detail","")=="windows-motion-source-retired"&&response.value("deviceWriteAttempted",true)==false,
                "response","retired source reports no attempted device write");
        }
        auto f=frame();attach(f,&stamp);const auto early=beforeAssembly(f);
        check(early.value("frameContextRetired",false)&&!early.value("deviceWriteAttempted",true),
            "source-stale-assembly","actual pre-assembly branch skips an obsolete motion frame");
        check(g_inputHostActive&&g_inputHostLifecycle=="active","target-stable","source retirement keeps the target active");
        f=frame();attach(f,&stamp);const auto oldNotes=notes;
        check(!inputHostFrameSubmissionMayIssue(&f),"source-stale-issue","actual final issue callback rejects retired Windows motion");
        check(notes==oldNotes,"issue-boundary","actual final issue callback has no log side effect");
    }
}
static void stagedRaces(){
    for(int changeAt=0;changeAt<3;++changeAt)for(unsigned iteration=0;iteration<48;++iteration){
        reset();activeCase="source-race-"+std::to_string(changeAt)+"/"+std::to_string(iteration);
        auto owner=std::make_shared<SensorOwner>();owner->activeBindingGeneration=100+iteration;
        const auto stamp=wm::retain(owner,100+iteration);auto f=frame();attach(f,&stamp);
        const bool ok=dispatch(f,[&](int stage){if(stage==changeAt)owner->activeBindingGeneration=200+iteration;});
        check(!ok&&writes==0,"source-stale-dispatch","binding change at any stage permits no write");
        check(f.rejected&&f.reason==fsb::Verdict::SourceRetired,"source-stale-state","late binding change remains a transient source retirement");
    }
}
static void producerCases(){
    for(bool controller:{false,true})for(bool admitted:{false,true}){
        reset();activeCase="actual-producer-scope";auto owner=std::make_shared<SensorOwner>();
        fromProducer(owner,11,controller,admitted,[&]{auto f=frame();copyOrigin(f);
            if constexpr(producerWired){
                check((t_windowsMotionSubmission!=nullptr)==(!controller&&admitted),"producer","only admitted Windows source supplies a Windows stamp");
                if(!controller&&admitted){owner->activeBindingGeneration=12;
                    check(!dispatch(f)&&writes==0,"source-stale-producer","producer stamp reaches real issue/request guards");}
                else check(dispatch(f)&&writes==1,"unrelated","base-only and controller routes do not require Windows source");
            }else{check(false,"producer-missing","old source has no Windows motion carrier");}
        });
        check(t_windowsMotionSubmission==nullptr,"scope","Windows source scope restores its previous pointer");
    }
    reset();activeCase="source-rebound-after-sampling";auto owner=std::make_shared<SensorOwner>();
    owner->activeBindingGeneration=12; // Values were processed using binding 11.
    fromProducer(owner,11,false,true,[&]{auto f=frame();copyOrigin(f);
        check(!dispatch(f)&&writes==0,"source-stale-producer","binding captured with the sample cannot be replaced by current binding at submit time");});
    if constexpr(producerWired){activeCase="nested-source-scopes";
        const auto outer=wm::retain(owner,12);const WindowsMotionSubmissionScope guard(&outer);
        fromProducer(owner,12,true,false,[&]{check(t_windowsMotionSubmission==nullptr,"scope","unrelated nested route does not inherit Windows source");});
        check(t_windowsMotionSubmission==&outer,"scope","nested scope restores outer source");}
}
static void neutralAndExistingContext(){
    for(bool motion:{false,true})for(bool neutral:{false,true})for(unsigned context=0;context<7;++context){
        reset();activeCase="neutral-host-power-intent-"+std::to_string(context);
        auto owner=std::make_shared<SensorOwner>();auto stamp=wm::retain(owner,11);owner->activeBindingGeneration=12;
        auto f=frame(neutral);if(motion)attach(f,&stamp);
        if(context==1)++ambient.host;if(context==2)++ambient.power;if(context==3)++ambient.intent;
        if(context==4)ambient.intentChanging=true;if(context==5)ambient.ready=false;
        if(context==6)ambient.sourceEligible=false;
        const bool expected=(context==0||context==6&&neutral)&&(!motion||neutral);
        const bool result=dispatch(f);
        check(result==expected,"neutral-context","neutral bypasses source retirement while preserving Host/power/intent admission");
        check(writes==(expected?1u:0u),"neutral-context","neutral/base context determines exactly one or zero writes");
    }
    reset();activeCase="existing-legion-source-guard";
    ymcc::legion_motion::Sample input;auto f=frame();f.controllerMotion=&input;legionCurrent=false;
    check(!request(f,"before-envelope")&&!inputHostFrameSubmissionMayIssue(&f)&&writes==0,
        "controller-route","existing Legion source rejection remains active");
    reset();activeCase="non-frame-command";auto owner=std::make_shared<SensorOwner>();auto stamp=wm::retain(owner,11);
    owner->activeBindingGeneration=12;f=frame();attach(f,&stamp);t_inputFrameSubmission=&f;
    check(inputHostFrameSubmissionRequestCurrent("HELLO",nullptr,"other-command"),"unrelated","non-frame command keeps its current route");t_inputFrameSubmission=nullptr;
}
static void publicationCases(){
    auto capture=std::make_shared<SensorOwner>(),choice=std::make_shared<SensorOwner>();
    auto native=std::make_shared<std::atomic<bool>>(true),cpu=std::make_shared<std::atomic<bool>>(true);
    const auto raw=wm::guarded(wm::retain(capture,11),wm::retain(choice,11),native);
    const auto pendingFence=wm::PublicationFence::pending(raw);
    const auto pending=wm::forProcessing(wm::forPublication(raw,pendingFence),cpu);
    reset();activeCase="pending-private-motion";auto f=frame();attach(f,&pending);json response;
    check(!request(f,"before-envelope",&response)&&writes==0&&f.reason==fsb::Verdict::SourcePublicationPending,"publication-request","actual envelope guard withholds pending private motion");
    check(response.value("detail",std::string{})=="windows-motion-publication-not-committed"&&!response.value("deviceWriteAttempted",true),"publication-response","pending output has an accurate no-write response");
    f=frame();attach(f,&pending);const auto early=beforeAssembly(f);
    check(early.value("frameContextRetired",false)&&early.value("publicationReason",std::string{})=="windows-motion-publication-not-committed"&&!early.value("deviceWriteAttempted",true),"publication-assembly","actual preassembly reports withheld publication without writing");
    const auto grant=wm::PublicationFence::committed(pending);
    const auto committed=wm::forProcessing(wm::forPublication(raw,grant),cpu);
    reset();activeCase="committed-private-motion";f=frame();attach(f,&committed);
    check(dispatch(f)&&writes==1,"publication-commit","fresh committed private motion reaches all actual issue guards");
    reset();f=frame();attach(f,&pending);check(!dispatch(f)&&writes==0,"publication-no-revival","commitment cannot reopen the old pending frame");
    for(int revokeAt=0;revokeAt<3;++revokeAt){
        reset();activeCase="publication-revoke-at-"+std::to_string(revokeAt);
        const auto permission=wm::PublicationFence::committed(pending);
        const auto issued=wm::forProcessing(wm::forPublication(raw,permission),cpu);f=frame();attach(f,&issued);
        check(!dispatch(f,[&](int stage){if(stage==revokeAt)permission->revoke();})&&writes==0,"publication-final-boundary","revocation at every actual dispatch boundary prevents device write");
        check(wm::current(issued)&&f.reason==fsb::Verdict::SourcePublicationPending,"publication-source-alive","publication revocation preserves input and target context");
    }
    reset();grant->revoke();f=frame(true);attach(f,&committed);check(dispatch(f)&&writes==1,"publication-neutral","neutral release still reaches the same target with revoked publication");
}
int main(){
    sourceAndOwnerCases();validAndRecoveredWindows();individualGuards();stagedRaces();producerCases();neutralAndExistingContext();if constexpr(publicationWired)publicationCases();
    std::cout<<json({{"status",failureCount?"fail":"pass"},{"checks",checks},{"failureCount",failureCount},
        {"failureGroups",failureGroups},{"failures",failures},{"raceSchedules",144},{"producerWired",producerWired},
        {"hostExecuted",false},{"realPipeIo",false},{"physicalAcceptance",false}}).dump()<<'\n';return failureCount?1:0;
}
