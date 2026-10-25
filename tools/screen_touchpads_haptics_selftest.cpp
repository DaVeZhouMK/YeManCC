#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "screen_touchpads.h"
#include "touch_haptic_dispatch.h"
#include "radial_haptics.h"
#include <iostream>
using namespace ymcc::screenpads;
static unsigned sinkCalls=0,sinkContacts=0,sinkPulses=0;
static int sinkStrength=0;
static void fixtureSink(unsigned contacts,bool pulse,int strength){++sinkCalls;sinkContacts=contacts;sinkPulses+=pulse?1:0;sinkStrength=strength;}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;
    Json cases=Json::array();bool ok=true;
    auto check=[&](const std::string& name,bool passed){ok=ok&&passed;cases.push_back({{"name",name},{"ok",passed}});};
    Config cfg=profileDefault(0);cfg.enabled=true;cfg.touchFeedback=TouchFeedbackMode::Minimal;cfg.left=cfg.right=Mode::Mouse;
    Core c;c.config=cfg;
    check("initial-touch-is-one-small-pulse",c.event(0,1,0,20,20,200,200,0).hapticPulses==1);
    check("motion-below-distance-does-not-pulse",c.event(0,1,1,25,20,200,200,100).hapticPulses==0);
    check("distance-accumulates-between-moves",c.event(0,1,1,32,20,200,200,150).hapticPulses==1);
    check("next-segment-pulses-again",c.event(0,1,1,44,20,200,200,200).hapticPulses==1);
    check("minimum-interval-rejects-fast-repeat",c.event(0,1,1,80,20,200,200,210).hapticPulses==0);
    check("stationary-after-fast-motion-does-not-flush-backlog",c.event(0,1,1,80,20,200,200,300).hapticPulses==0);
    check("fresh-sub-detent-movement-does-not-replay-skipped-detents",c.event(0,1,1,84,20,200,200,310).hapticPulses==0);
    check("next-fresh-detent-pulses-after-throttle",c.event(0,1,1,94,20,200,200,350).hapticPulses==1);
    check("large-jump-does-not-burst",c.event(0,1,1,200,20,200,200,400).hapticPulses==1);
    for(int i=0;i<100;++i)check("stationary-large-jump-"+std::to_string(i),c.event(0,1,1,200,20,200,200,500+i*60).hapticPulses==0);
    check("other-finger-ignored",c.event(0,99,1,100,100,200,200,6600).hapticPulses==0&&c.pads[0].id==1);
    check("UP-clears-ownership-no-extra-pulse",c.event(0,1,2,200,20,200,200,6700).hapticContacts==0&&!c.pads[0].down);
    check("move-after-UP-is-no-op",c.event(0,1,1,100,100,200,200,6800).hapticPulses==0);
    c.event(0,1,0,100,100,200,200,7000);
    bool jitterSafe=true;for(int i=0;i<1000;++i)jitterSafe &= c.event(0,1,1,100+(i%2?.3f:-.3f),100,200,200,7100+i*50).hapticPulses==0;
    check("stationary-noise-never-accumulates-rumble",jitterSafe);
    c.cancel();check("cancel-clears-all-haptic-state",c.current().hapticContacts==0&&c.pads[0].haptic.distance==0&&!c.pads[0].haptic.fired);
    c.event(0,1,0,20,20,200,200,100);c.event(1,2,0,100,20,200,200,100);
    check("left-and-right-have-independent-distance",c.event(0,1,1,32,20,200,200,200).hapticPulses==1&&c.event(1,2,1,105,20,200,200,210).hapticPulses==0);
    check("releasing-left-preserves-right",c.event(0,1,2,32,20,200,200,250).hapticContacts==2&&c.pads[1].down);
    check("right-continues-pulsing",c.event(1,2,1,112,20,200,200,300).hapticPulses==1);
    for(int size:{75,100,150,200,250,300,500}){
        Core scaled;scaled.config=cfg;scaled.event(0,1,0,size*.2f,size*.2f,size,size,0);
        check("same-logical-distance-all-scales-"+std::to_string(size),scaled.event(0,1,1,size*.25f,size*.2f,size,size,100).hapticPulses==1);
    }
    for(Mode mode:{Mode::Deck,Mode::DualSense,Mode::Wasd,Mode::Arrows,Mode::Mouse}) {
        Core on,off;on.config=off.config=cfg;on.config.left=off.config.left=mode;off.config.touchFeedback=TouchFeedbackMode::Off;
        bool unchanged=true;for(int phase:{0,1,1,2}){
            const float x=phase==0?20:phase==2?100:150;
            auto a=on.event(0,1,phase,x,50,200,200,100+phase*100),b=off.event(0,1,phase,x,50,200,200,100+phase*100);
            unchanged &= a.keys==b.keys&&a.dx==b.dx&&a.dy==b.dy&&a.tap==b.tap&&a.heldClick==b.heldClick&&snapshotJson(on.snapshot(200))==snapshotJson(off.snapshot(200));
        }check(std::string("mapping-wire-unchanged-")+modeName(mode),unchanged);
    }
    Core ds;ds.config=profileDefault(2);ds.config.touchFeedback=TouchFeedbackMode::Minimal;
    check("single-PS5-two-independent-contacts",ds.event(0,1,0,30,30,360,200,100).hapticPulses==1&&ds.event(1,2,0,280,100,360,200,100).hapticContacts==3);
    check("single-wide-distance-uses-short-side",ds.event(1,2,1,292,100,360,200,200).hapticPulses==1);
    Core off;off.config=cfg;off.config.touchFeedback=TouchFeedbackMode::Off;check("feedback-off-is-silent",off.event(0,1,0,20,20,200,200,0).hapticPulses==0&&off.event(0,1,1,100,100,200,200,100).hapticPulses==0);
    off.config.enabled=false;off.cancel();check("layout-off-is-silent",off.event(0,1,0,20,20,200,200,0).hapticContacts==0);
    check("invalid-input-is-silent",off.event(-1,1,0,0,0,200,200,0).hapticPulses==0);
    for(int profile=0;profile<4;++profile){ProfileBank bank;
        check("all-profiles-default-medium-"+std::to_string(profile),bank.slots[profile].touchFeedback==TouchFeedbackMode::Medium);
        Core defaults;defaults.config=bank.slots[profile];
        check("disabled-layout-does-not-rumble-"+std::to_string(profile),defaults.config.enabled||defaults.event(0,1,0,20,20,200,200,0).hapticPulses==0);
        Config enabled=bank.slots[profile];const bool enabledOk=parseConfig({{"layout","dual"}},enabled,enabled);defaults.config=enabled;
        check("enabling-profile-inherits-medium-feedback-"+std::to_string(profile),enabledOk&&defaults.event(0,1,0,20,20,200,200,0).hapticPulses==1);
        ProfileBank legacy({{"profiles",{{profileName(profile),{{"layout","dual"}}}}}});
        check("missing-saved-feedback-inherits-medium-"+std::to_string(profile),legacy.slots[profile].touchFeedback==TouchFeedbackMode::Medium);
        bank.slots[profile].touchFeedback=TouchFeedbackMode::Off;ProfileBank saved(bank.json());
        bool independent=true;for(int i=0;i<4;++i)independent &= saved.slots[i].touchFeedback==(i==profile?TouchFeedbackMode::Off:TouchFeedbackMode::Medium);
        check("per-profile-explicit-off-persistence-"+std::to_string(profile),independent);
        Config optOut=saved.slots[profile];const bool toggleOk=parseConfig({{"layout","off"}},optOut,optOut)&&parseConfig({{"layout","single"}},optOut,optOut);
        check("layout-toggle-preserves-explicit-off-"+std::to_string(profile),toggleOk&&optOut.touchFeedback==TouchFeedbackMode::Off);}
    check("strength-tiers-20-40-60-percent",touchFeedbackStrength(TouchFeedbackMode::Minimal)==20&&
        touchFeedbackStrength(TouchFeedbackMode::Medium)==40&&touchFeedbackStrength(TouchFeedbackMode::Strong)==60&&touchFeedbackStrength(TouchFeedbackMode::Off)==0);
    check("gesture-timing-and-detents-unchanged",ymcc::touchhaptic::kDurationMs==18&&ymcc::touchhaptic::kMinIntervalMs==45&&
        ymcc::touchhaptic::kMaxQueueAgeMs==80&&ymcc::touchhaptic::kDistanceStep==0.05&&ymcc::touchhaptic::kJitterFraction==0.006);
    for(int profile=0;profile<4;++profile)for(const auto tier:{TouchFeedbackMode::Off,TouchFeedbackMode::Minimal,TouchFeedbackMode::Medium,TouchFeedbackMode::Strong}) {
        const std::string name=std::to_string(profile)+"-"+touchFeedbackName(tier);
        ProfileBank bank;bank.slots[profile].touchFeedback=tier;ProfileBank restored(bank.json());
        check("saved-tier-keeps-exact-value-"+name,restored.slots[profile].touchFeedback==tier);
        bool otherDefaults=true;for(int i=0;i<4;++i)if(i!=profile)otherDefaults &= restored.slots[i].touchFeedback==TouchFeedbackMode::Medium;
        check("tier-profile-isolation-"+name,otherDefaults);
        Config toggled=restored.slots[profile];
        check("layout-toggle-preserves-saved-tier-"+name,parseConfig({{"layout","off"}},toggled,toggled)&&
            parseConfig({{"layout","single"}},toggled,toggled)&&toggled.touchFeedback==tier);
        Config parsedTier;check("parser-and-json-tier-roundtrip-"+name,parseConfig({{"touchFeedback",touchFeedbackName(tier)}},parsedTier,parsedTier)&&
            parsedTier.touchFeedback==tier&&configJson(parsedTier).value("touchFeedback","")==touchFeedbackName(tier));
    }
    for(const auto tier:{TouchFeedbackMode::Minimal,TouchFeedbackMode::Medium,TouchFeedbackMode::Strong}) {
        Core baseline,graded;baseline.config=graded.config=cfg;graded.config.touchFeedback=tier;
        bool equal=true,correctStrength=true;
        const int phases[]={0,1,1,1,1,1,1,2};const float xs[]={20,25,32,44,80,80,94,94};
        const ULONGLONG times[]={0,100,150,200,210,300,350,400};
        for(int i=0;i<8;++i){auto a=baseline.event(0,1,phases[i],xs[i],20,200,200,times[i]),b=graded.event(0,1,phases[i],xs[i],20,200,200,times[i]);
            equal &= a.hapticPulses==b.hapticPulses&&a.hapticContacts==b.hapticContacts&&a.keys==b.keys&&a.dx==b.dx&&a.dy==b.dy&&a.tap==b.tap&&a.heldClick==b.heldClick;
            correctStrength &= b.hapticStrengthPercent==(b.hapticContacts?touchFeedbackStrength(tier):0);
        }
        check(std::string("tier-changes-strength-not-gesture-")+touchFeedbackName(tier),equal&&correctStrength);
    }
    Config parsed;check("legacy-config-defaults-medium",parseConfig({{"layout","dual"}},parsed,parsed)&&parsed.touchFeedback==TouchFeedbackMode::Medium);
    for(auto bad:{Json(true),Json(3),Json(nullptr),Json("maximal")})check("reject-invalid-config-"+bad.dump(),!parseConfig({{"touchFeedback",bad}},parsed,parsed));
    // The live configuration owns feedback tier; stale Action strengths cannot override it.
    core.config.touchFeedback=TouchFeedbackMode::Minimal;
    setTouchHapticPulseSink(fixtureSink);Action pulse;pulse.hapticPulses=1;pulse.hapticContacts=1;pulse.hapticStrengthPercent=20;
    dryRun=true;output(pulse);check("dryRun-never-dispatches-device-feedback",sinkCalls==0);
    dryRun=false;output(pulse);check("native-mode-pulse-dispatches-without-key-or-mouse-output",sinkCalls==1&&sinkContacts==1&&sinkPulses==1&&sinkStrength==20&&injectedCount==0);
    Action held;held.hapticContacts=1;held.hapticStrengthPercent=20;
    for(int i=0;i<10000;++i)output(held);
    check("ten-thousand-held-updates-do-not-resubmit-haptic-work",sinkCalls==1);
    held.hapticContacts=3;output(held);check("second-contact-ownership-edge-is-delivered",sinkCalls==2&&sinkContacts==3);
    output({});check("cancel-reaches-sink-without-SendInput",sinkCalls==3&&sinkContacts==0&&injectedCount==0);
    for(int i=0;i<10000;++i)output({});check("idle-cancel-does-not-resubmit-work",sinkCalls==3);
    setTouchHapticPulseSink(fixtureSink);output(held);check("sink-replacement-republishes-held-ownership",sinkCalls==4);
    core.config.touchFeedback=TouchFeedbackMode::Medium;held.hapticStrengthPercent=40;output(held);check("strength-edge-reaches-sink-without-pulse",sinkCalls==5&&sinkStrength==40);
    for(int i=0;i<10000;++i)output(held);check("medium-held-events-still-do-not-resubmit-work",sinkCalls==5);
    core.config.touchFeedback=TouchFeedbackMode::Strong;held.hapticStrengthPercent=60;output(held);check("strong-strength-edge-reaches-sink",sinkCalls==6&&sinkStrength==60);
    for(int i=0;i<10000;++i)output(held);check("strong-held-events-still-do-not-resubmit-work",sinkCalls==6);
    output({});setTouchHapticPulseSink(nullptr);
    ymcc::touchhaptic::Motion motion;motion.begin(20,20,200,200,0);
    check("five-percent-detent",motion.move(30,20,100));
    motion.begin(20,20,200,200,0);
    check("Euclidean-diagonal-detent-not-Manhattan",!motion.move(24,24,100)&&motion.move(28,28,200));
    motion.begin(20,20,200,200,100);
    check("backwards-time-never-starts-pulse",!motion.move(40,20,99));
    check("skipped-detent-keeps-bounded-remainder",motion.distance>=0&&motion.distance<ymcc::touchhaptic::kDistanceStep);
    motion.begin(20,20,200,200,0);bool slowPulse=false;
    for(int i=1;i<=40;++i)slowPulse=motion.move(20+i*.3f,20,100+i*50)||slowPulse;
    check("slow-sub-jitter-steps-still-accumulate",slowPulse);
    check("successful-touch-pulses-are-not-immediate-log-writes",!ymcc::touchhaptic::logPulseImmediately(true,true));
    check("failed-touch-and-manual-pulses-keep-immediate-log-policy",ymcc::touchhaptic::logPulseImmediately(true,false)&&ymcc::touchhaptic::logPulseImmediately(false,true));
    ymcc::touchhaptic::Summary summary;summary.record(true,false);summary.record(true,true);summary.record(false,false);++summary.dropped;
    auto counts=summary.take();check("summary-is-bounded-counters-and-clears-on-take",summary.empty()&&counts.sent==2&&counts.xinput==1&&counts.msi==1&&counts.failed==1&&counts.dropped==1);
    summary.record(true,false,40);summary.record(true,false,60);summary.record(true,true,20);
    counts=summary.take();check("summary-keeps-bounded-tier-counters",counts.sent==3&&counts.minimal==1&&counts.medium==1&&counts.strong==1&&summary.empty());
    Core delayed;delayed.config=profileDefault(2);
    delayed.event(0,1,0,20,20,200,200,1000,2000);delayed.event(0,1,2,20,20,200,200,1050,2300);
    check("delayed-short-native-tap-keeps-full-delivery-window",delayed.snapshot(2310).left.click&&!delayed.snapshot(2361).present);
    delayed.cancel();delayed.event(0,1,0,20,20,200,200,1000,2000);delayed.event(0,1,2,20,20,200,200,1500,2300);
    check("delivery-window-does-not-turn-delayed-long-hold-into-tap",!delayed.snapshot(2310).present);
    using ymcc::touchhaptic::Dispatch;
    Dispatch queue;auto t=queue.request(1,true,0,1);check("queue-initial-request",t.has_value());
    bool bounded=true;for(int i=0;i<1000;++i)bounded &= !queue.request(3,true,i,1);check("two-fingers-1000-requests-still-one-job",bounded);
    check("queue-admits-fresh-request",queue.admit(*t,0,1,true));check("same-job-cannot-admit-twice",!queue.admit(*t,100,1,true));queue.finish(*t);
    t=queue.request(1,true,10,1);check("global-physical-output-rate-cap",!queue.admit(*t,10,1,true));queue.finish(*t);
    t=queue.request(1,true,100,1);queue.request(0,false,101,1);check("UP-cancels-queued-pulse",!queue.admit(*t,102,1,true));queue.finish(*t);
    t=queue.request(1,true,200,1);check("expired-job-is-dropped-not-replayed",!queue.admit(*t,281,1,true));queue.finish(*t);
    t=queue.request(1,true,300,1);check("power-transition-invalidates-queued-pulse",!queue.admit(*t,310,2,true));queue.finish(*t);
    t=queue.request(1,true,400,1);check("power-gate-blocks-request",!queue.admit(*t,410,1,false));queue.finish(*t);
    t=queue.request(1,true,500,1);queue.finish(*t);check("rejected-queue-does-not-stick-pending",queue.request(1,true,510,1).has_value());
    for(int strength:{20,40,60}) {
        Dispatch graded;auto ticket=graded.request(1,true,0,1,strength);
        check("queue-captures-selected-strength-"+std::to_string(strength),ticket&&ticket->strength==strength&&graded.admit(*ticket,0,1,true));
        if(ticket)graded.finish(*ticket);
    }
    Dispatch switched;auto old=switched.request(1,true,0,1,20);switched.request(1,false,1,1,40);
    check("queued-minimum-cannot-leak-after-tier-switch",old&&!switched.admit(*old,2,1,true));if(old)switched.finish(*old);
    auto next=switched.request(1,true,10,1,40);check("tier-switch-unblocks-new-medium-request",next&&next->strength==40&&switched.admit(*next,10,1,true));if(next)switched.finish(*next);
    for(int invalid:{0,-1,11,100,101}){Dispatch rejected;check("invalid-strength-fails-closed-"+std::to_string(invalid),!rejected.request(1,true,0,1,invalid));}
    // Exercise the actual HWND handler route without SetForegroundWindow or input injection.
    dryRun=true;bool ownUi=true;feedbackOnlyForegroundFixture=&ownUi;
    core={};core.config=profileDefault(0);core.config.enabled=true;core.config.left=Mode::Mouse;core.config.right=Mode::Wasd;
    core.config.touchFeedback=TouchFeedbackMode::Medium;sizes={SIZE{200,200},SIZE{200,200}};
    enabled=true;activeProfile=appliedProfile.load();setTouchHapticPulseSink(fixtureSink);
    sinkCalls=sinkPulses=0;const auto injectedBefore=injectedCount;
    handlePointer(nullptr,0,WM_POINTERDOWN,11,20,20,0);
    check("own-ui-down-is-feedback-only",sinkPulses==1&&sinkStrength==40&&feedbackOnlyContacts.mask()==1&&!core.pads[0].down&&sentKeys==0&&!sentClick);
    ownUi=false;handlePointer(nullptr,0,WM_POINTERUPDATE,11,40,20,0);
    check("foreground-leave-never-upgrades-half-gesture-to-mouse",!core.pads[0].down&&injectedCount==injectedBefore);
    handlePointer(nullptr,1,WM_POINTERDOWN,12,20,20,0);const auto heldKeys=sentKeys;
    handlePointer(nullptr,0,WM_POINTERUPDATE,11,60,20,0);
    check("feedback-only-move-does-not-release-other-mapped-contact",heldKeys!=0&&sentKeys==heldKeys&&core.pads[1].down);
    handlePointer(nullptr,0,WM_POINTERUP,11,60,20,0);
    check("feedback-only-up-preserves-other-contact",feedbackOnlyContacts.mask()==0&&core.pads[1].down&&sentKeys==heldKeys);
    handlePointer(nullptr,1,WM_POINTERUP,12,20,20,0);check("mixed-routes-release-all-keys",sentKeys==0&&!core.pads[1].down);
    ownUi=true;handlePointer(nullptr,0,WM_POINTERDOWN,21,20,20,0);handleCaptureLoss(nullptr,0,21);
    check("feedback-only-capture-loss-clears-contact",feedbackOnlyContacts.mask()==0);
    handlePointer(nullptr,0,WM_POINTERUPDATE,21,80,20,0);
    check("late-move-cannot-recreate-feedback-contact",feedbackOnlyContacts.mask()==0);
    core.config.layout=Layout::Single;core.config.single=Mode::DualSense;psAvailable=true;
    handlePointer(nullptr,0,WM_POINTERDOWN,31,20,20,0);handlePointer(nullptr,0,WM_POINTERDOWN,32,120,20,0);
    handlePointer(nullptr,0,WM_POINTERDOWN,33,80,20,0);
    check("feedback-only-shared-surface-is-bounded-to-two-contacts",feedbackOnlyContacts.mask()==3&&feedbackOnlyContacts.find(33,0)<0);
    ownUi=false;handlePointer(nullptr,0,WM_POINTERDOWN,34,80,20,0);
    check("shared-feedback-and-mapping-keep-total-two-contact-budget",!core.pads[0].down&&!core.pads[1].down&&feedbackOnlyContacts.mask()==3);ownUi=true;
    handleCaptureLoss(nullptr,0,31);handlePointer(nullptr,0,WM_POINTERUP,32,120,20,0);
    check("feedback-only-shared-contacts-release-independently",feedbackOnlyContacts.mask()==0);
    release();core.config.layout=Layout::Dual;core.config.left=Mode::Mouse;core.config.right=Mode::Wasd;psAvailable=false;
    ownUi=false;handlePointer(nullptr,0,WM_POINTERDOWN,41,20,20,0);handlePointer(nullptr,1,WM_POINTERDOWN,41,20,20,0);
    ownUi=true;handlePointer(nullptr,0,WM_POINTERUPDATE,41,40,20,0);
    check("own-ui-transition-adopts-both-mapped-contacts-by-surface",feedbackOnlyContacts.mask()==3&&!core.pads[0].down&&!core.pads[1].down&&sentKeys==0);
    ownUi=false;handlePointer(nullptr,1,WM_POINTERUPDATE,41,40,20,0);handlePointer(nullptr,0,WM_POINTERUP,41,40,20,0);
    check("same-id-different-surfaces-release-independently",feedbackOnlyContacts.mask()==2&&sentKeys==0&&!core.pads[1].down);
    handlePointer(nullptr,1,WM_POINTERUP,41,40,20,0);check("same-id-surface-pair-no-stuck-contact",feedbackOnlyContacts.mask()==0);
    ownUi=true;handlePointer(nullptr,0,WM_POINTERDOWN,51,20,20,0);
    ownUi=false;handlePointer(nullptr,0,WM_POINTERDOWN,53,40,20,0);
    check("feedback-owned-pad-does-not-admit-a-second-game-finger",!core.pads[0].down&&feedbackOnlyContacts.mask()==1);
    handlePointer(nullptr,1,WM_POINTERDOWN,52,20,20,0);check("mixed-route-fixture-holds-game-binding",sentKeys!=0);
    ownUi=true;handlePointer(nullptr,0,WM_POINTERUPDATE,51,40,20,0);
    check("own-ui-reentry-releases-mapping-even-from-existing-feedback-gesture",sentKeys==0&&!core.pads[1].down&&feedbackOnlyContacts.mask()==3);
    ownUi=false;handlePointer(nullptr,1,WM_POINTERUPDATE,52,40,20,0);
    check("adopted-other-contact-never-upgrades-back-to-game",sentKeys==0&&!core.pads[1].down);
    handlePointer(nullptr,0,WM_POINTERUP,51,40,20,0);handlePointer(nullptr,1,WM_POINTERUP,52,40,20,0);
    check("own-ui-reentry-pair-releases-without-backlog",feedbackOnlyContacts.mask()==0&&sentKeys==0);
    release();
    radialHaptic(true,true);check("radial-uses-separate-feedback-participant",(sinkContacts&(1u<<10))!=0);
    monitorHaptic(true,false);radialHaptic(false,false);check("radial-close-preserves-monitor-feedback",(sinkContacts&(1u<<9))!=0&&(sinkContacts&(1u<<10))==0);
    radialHaptic(true,false);monitorHaptic(false,false);check("monitor-close-preserves-radial-feedback",(sinkContacts&(1u<<10))!=0&&(sinkContacts&(1u<<9))==0);
    radialHaptic(false,false);check("all-ui-participants-release",sinkContacts==0);
    setTouchHapticPulseSink(nullptr);feedbackOnlyForegroundFixture=nullptr;psAvailable=false;enabled=false;
    using ymcc::touchhaptic::Reason;
    Dispatch classified;auto classifiedTicket=classified.request(1,true,0,1);
    check("dispatch-expiry-has-explicit-reason",classified.admitReason(*classifiedTicket,81,1,true)==Reason::QueueExpired);
    classified.finish(*classifiedTicket);classifiedTicket=classified.request(1,true,100,1);
    check("dispatch-power-change-has-explicit-reason",classified.admitReason(*classifiedTicket,110,2,true)==Reason::PowerTransition);
    classified.finish(*classifiedTicket);classifiedTicket=classified.request(1,true,200,1);classified.request(0,false,201,1);
    check("dispatch-cancellation-has-explicit-reason",classified.admitReason(*classifiedTicket,202,1,true)==Reason::Cancelled);
    summary={};summary.failure(Reason::SourcePending);summary.failure(Reason::QueueExpired,true);counts=summary.take();
    check("summary-retains-fixed-reason-counters",counts.failed==1&&counts.dropped==1&&counts.reasons[(size_t)Reason::SourcePending]==1&&counts.reasons[(size_t)Reason::QueueExpired]==1);
    // Execute the same event stream through the actual touchpad Core and wheel adapter.
    using WheelFeedback=ymcc::radial::TouchFeedback;using Phase=WheelFeedback::Phase;
    Config wheelCfg=profileDefault(0);wheelCfg.enabled=true;wheelCfg.left=wheelCfg.right=Mode::Mouse;
    for(const auto mode:{TouchFeedbackMode::Off,TouchFeedbackMode::Minimal,TouchFeedbackMode::Medium,TouchFeedbackMode::Strong}){
        Core reference;reference.config=wheelCfg;reference.config.touchFeedback=mode;
        WheelFeedback wheel;const auto strength=touchFeedbackStrength(mode);
        for(unsigned step=0;step<240;++step){
            const int phase=step==0?0:step==239?2:1;
            const float x=40.f+float((step*17)%290),y=35.f+float((step*11)%210);
            const auto at=1000+step*9;const auto pad=reference.event(0,73,phase,x,y,400,300,at);
            const auto tick=wheel.event(true,strength,phase==0?Phase::Down:phase==2?Phase::Up:Phase::Move,73,x,y,400,300,at);
            check("wheel-touchpad-event-parity-"+std::string(touchFeedbackName(mode))+"-"+std::to_string(step),
                tick.contact==bool(pad.hapticContacts)&&tick.pulse==bool(pad.hapticPulses));
        }
    }
    WheelFeedback wheel;
    wheel.event(true,40,Phase::Down,1,0,0,400,300,100);
    check("wheel-other-finger-does-not-own-or-pulse",!wheel.event(true,40,Phase::Down,2,100,100,400,300,160).pulse&&wheel.contacting());
    check("wheel-other-finger-up-keeps-original-owner",wheel.event(true,40,Phase::Up,2,100,100,400,300,170).contact);
    check("wheel-cancel-clears-contact-even-with-unusable-coordinates",!wheel.event(true,40,Phase::Cancel,1,NAN,NAN,0,0,180).contact&&!wheel.contacting());
    wheel.event(true,40,Phase::Down,1,0,0,400,300,200);
    check("wheel-strength-change-cancels-until-fresh-down",!wheel.event(true,60,Phase::Move,1,100,100,400,300,260).contact&&!wheel.event(true,60,Phase::Move,1,200,100,400,300,320).pulse);
    check("wheel-fresh-down-uses-new-setting",wheel.event(true,60,Phase::Down,1,200,100,400,300,350).pulse);
    check("wheel-disabled-feedback-revokes-contact",!wheel.event(false,60,Phase::Move,1,300,100,400,300,400).contact&&!wheel.contacting());
    dryRun=false;core={};core.config=wheelCfg;radialHapticContact=false;monitorHapticContact=false;setTouchHapticPulseSink(fixtureSink);touchHapticEventGate.reset();
    for(const auto mode:{TouchFeedbackMode::Off,TouchFeedbackMode::Minimal,TouchFeedbackMode::Medium,TouchFeedbackMode::Strong}){
        core.config.touchFeedback=mode;radialHaptic(false,false);const auto before=sinkPulses;radialHaptic(true,true);
        check("wheel-output-follows-touchpad-setting-"+std::string(touchFeedbackName(mode)),mode==TouchFeedbackMode::Off?sinkContacts==0&&sinkPulses==before:sinkContacts==(1u<<10)&&sinkStrength==touchFeedbackStrength(mode)&&sinkPulses==before+1);
        radialHaptic(false,false);check("wheel-release-clears-only-own-contact-"+std::string(touchFeedbackName(mode)),sinkContacts==0);
        const auto monitorBefore=sinkPulses;monitorHaptic(true,true);
        check("monitor-output-follows-touchpad-setting-"+std::string(touchFeedbackName(mode)),mode==TouchFeedbackMode::Off?sinkContacts==0&&sinkPulses==monitorBefore:sinkContacts==(1u<<9)&&sinkStrength==touchFeedbackStrength(mode)&&sinkPulses==monitorBefore+1);
        monitorHaptic(false,false);check("monitor-release-clears-own-contact-"+std::string(touchFeedbackName(mode)),sinkContacts==0);
    }
    core.config.touchFeedback=TouchFeedbackMode::Medium;core.event(0,5,0,30,30,400,300,1000);radialHaptic(true,true);
    check("wheel-and-touchpad-use-existing-merged-contact-mask",sinkContacts==((1u<<10)|1u)&&sinkStrength==40);
    radialHaptic(false,false);check("wheel-release-preserves-other-touchpad-feedback",sinkContacts==1u&&sinkStrength==40);
    core.cancel();outputHaptic({});setTouchHapticPulseSink(nullptr);
    std::ofstream out(std::filesystem::path(argv[1]),std::ios::binary);out<<Json{{"ok",ok},{"cases",cases},{"hardwareWrites",0},{"systemInputInjected",false},{"virtualTargetStarted",false}}.dump(2);
    std::cout<<"TOUCH_HAPTICS_STATE_OK="<<ok<<" checks="<<cases.size()<<" hardwareWrites=0\n";return ok&&out.good()?0:3;
}
