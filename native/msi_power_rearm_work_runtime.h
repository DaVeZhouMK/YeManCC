#pragma once
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <exception>
#include <mutex>
#include <limits>
#include <string>
#include <vector>
#include "input_host_shared_power.h"

// Historical namespace/wire/API retained. This is the ORIGINAL immutable power
// request for every family, not a second rearm lifecycle or a hardware policy.
// first/consumed and delivery metadata use g_inputHostPowerRequestMx; no IO there.
namespace ymcc::inputHost::msiPowerWork {
struct Work {
    std::uint64_t power=0,intent=0,request=0,epoch=0,signal=0,deadline=0;
    std::string persona,requestPersona,reason,wire;
    bool protectedPlayer=false,bound=false,writingAtSignal=false;
    sleeppower::SharedPowerV1 origin,admitted;
    bool deliveryAuthorized=false,queued=false,capturePending=false;
};
inline constexpr const char* prefix="msi-power-rearm:";
inline constexpr std::uint64_t maxMs=30000;
inline constexpr std::size_t reasonMax=96,wireMax=512;
static Work first; // Retained even after consumption/capture failure for dedup.
static bool consumed=false;
static bool personaValid(const std::string& value) {
    return ymcc::controllerPersona::current(value);
}
static std::string boundedReason(const char* text) {
    std::string result;
    if(text)for(std::size_t i=0;i<reasonMax && text[i];++i){
        const char c=text[i];
        result+=((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.')?c:'-';
    }
    return result.empty()?"power-resume-committed":result;
}
static std::string encode(const Work& w) {
    return std::string(prefix)+std::to_string(w.power)+":"+std::to_string(w.intent)+":"+
        std::to_string(w.request)+":"+std::to_string(w.epoch)+":"+std::to_string(w.signal)+":"+
        std::to_string(w.deadline)+":"+(w.protectedPlayer?"1":"0")+":"+w.persona+":"+
        w.requestPersona+":"+w.reason;
}
static bool decode(const std::string& wire,Work& w) {
    if(wire.rfind(prefix,0)!=0)return true;
    if(wire.size()>wireMax)return false;
    const auto text=wire.substr(std::char_traits<char>::length(prefix));
    std::vector<std::string> parts;std::size_t begin=0;
    for(;;){const auto end=text.find(':',begin);parts.push_back(text.substr(begin,end==std::string::npos?end:end-begin));
        if(end==std::string::npos)break;begin=end+1;if(parts.size()>10)return false;}
    if(parts.size()!=10)return false;
    const auto number=[](const std::string& text,std::uint64_t& value){
        if(text.empty())return false;const auto r=std::from_chars(text.data(),text.data()+text.size(),value);
        return r.ec==std::errc{}&&r.ptr==text.data()+text.size();};
    if(!number(parts[0],w.power)||!number(parts[1],w.intent)||!number(parts[2],w.request)||
        !number(parts[3],w.epoch)||!number(parts[4],w.signal)||!number(parts[5],w.deadline)||
        !w.power||w.deadline<=w.signal||w.deadline-w.signal>maxMs||
        (parts[6]!="0"&&parts[6]!="1")||!personaValid(parts[7])||
        (!parts[8].empty()&&!personaValid(parts[8]))||parts[9].size()>reasonMax||
        parts[9].empty()||boundedReason(parts[9].c_str())!=parts[9])return false;
    w.protectedPlayer=parts[6]=="1";
    if(w.protectedPlayer&&!w.request)return false;
    w.persona=parts[7];w.requestPersona=parts[8];w.reason="power-rearm:"+parts[9];
    w.wire=wire;w.bound=true;return true;
}
static void note(const Work& w,const char* action,const char* reason) noexcept {
    try{appendNativeLifecycleLog("input-host-msi-power-work",{{"action",action},{"reason",reason},
        {"powerGeneration",w.power},{"intentRevision",w.intent},{"requestId",w.request},
        {"persona",w.persona},{"requestPersona",w.requestPersona},{"hostEpoch",w.epoch},
        {"originalSignal",w.signal},{"originalDeadline",w.deadline},{"protectedPlayer",w.protectedPlayer},
        {"originalSuspendSerial",w.origin.suspendSerial},{"originalNotification",w.origin.observedNotificationSequence},
        {"budgetRenewed",false},{"newPlayerRequest",false},{"familyScope","shared-original-power-work"}});}catch(...){}
}
// Original callback time and owner, before leaf/Host/BUS/queue waits.
static Work signal(std::uint64_t power,const char* reason) {
    Work w;w.power=power;w.signal=inputHostStartProtectionNow();w.capturePending=true;
    w.intent=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
    w.epoch=g_inputHostEpochMirror.load(std::memory_order_acquire);
    w.writingAtSignal=g_inputHostVirtualIntentWriters.load(std::memory_order_acquire)!=0;
    w.reason=boundedReason(reason);
    sleeppower::capturePowerObservation(&readSharedSystemPower,w.origin);
    return w;
}
// Ownership survives same-cohort notification/phase changes; it grants no
// effect. The original signal and budget never move while capture is pending.
static bool signalOwnerCurrent(const Work& w,std::uint64_t now) {
    return sleeppower::powerObservationKnown(w.origin)&&w.origin.generation==w.power&&
        now>=w.signal&&now-w.signal<maxMs&&!w.writingAtSignal&&
        w.power==currentPowerGeneration()&&w.origin.suspendSerial==g_sharedPowerSuspendSerial.load(std::memory_order_acquire)&&
        w.intent==g_inputHostVirtualIntentRevision.load(std::memory_order_acquire)&&
        !g_inputHostVirtualIntentWriters.load(std::memory_order_acquire)&&
        w.epoch==g_inputHostEpochMirror.load(std::memory_order_acquire)&&
        !inputHostExitFenceRaised()&&!g_exitRequested.load(std::memory_order_acquire)&&
        !g_inputCaptureStop.load(std::memory_order_acquire);
}
static void capture(Work& w) {
    w.capturePending=false;
    w.persona=inputHostConfiguredPersona();
    const auto now=inputHostStartProtectionNow();const auto view=g_virtualStartProtection.view(now);
    w.request=view.requestId;w.requestPersona=view.persona;
    w.protectedPlayer=view.locked&&(view.phase=="pending"||view.phase=="verifying");
    if(w.signal>(std::numeric_limits<std::uint64_t>::max)()-maxMs||now<w.signal)return;
    w.deadline=w.signal+maxMs;
    if(w.protectedPlayer){
        if(!w.request||!view.remainingMs||view.remainingMs>maxMs||
            now>(std::numeric_limits<std::uint64_t>::max)()-view.remainingMs)return;
        w.deadline=(std::min)(w.deadline,now+view.remainingMs);
    }
    sleeppower::SharedPowerV1 current;
    const bool sameCohort=sleeppower::capturePowerObservation(&readSharedSystemPower,current)&&
        sleeppower::powerObservationKnown(w.origin)&&current.generation==w.origin.generation&&
        current.suspendSerial==w.origin.suspendSerial&&
        current.observedNotificationSequence>=w.origin.observedNotificationSequence&&
        current.publicationRevision>=w.origin.publicationRevision;
    w.bound=sameCohort&&signalOwnerCurrent(w,now)&&now<w.deadline&&personaValid(w.persona);
    if(w.bound){
        w.wire=encode(w);
        // A later notification invalidates this permission. Only the explicit
        // shared post-gate Ready delivery below may authorize another effect.
        if(sleeppower::powerAdmitted(w.origin)&&sleeppower::samePowerObservation(w.origin,current)){w.admitted=w.origin;w.deliveryAuthorized=true;}
    }
}
// No state locks held. A pending notification is a delivery pause, not an owner
// change. An actual new Suspend serial/generation, OFF or new ON retires it.
static const char* stale(const Work& w,std::uint64_t now,bool originalEpoch=true) {
    if(!w.bound||!sleeppower::powerObservationKnown(w.origin))return "unbound-power-work";
    if(now<w.signal||now>=w.deadline)return "original-power-budget-expired";
    if(w.power!=currentPowerGeneration()||w.origin.suspendSerial!=g_sharedPowerSuspendSerial.load(std::memory_order_acquire))return "power-generation-or-suspend-retired";
    if(w.intent!=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire)||
        g_inputHostVirtualIntentWriters.load(std::memory_order_acquire))return "player-intent-retired";
    if(originalEpoch&&w.epoch!=g_inputHostEpochMirror.load(std::memory_order_acquire))return "host-epoch-retired";
    if(w.persona!=inputHostConfiguredPersona())return "configured-persona-retired";
    const auto view=g_virtualStartProtection.view(now);
    if(view.requestId!=w.request||view.persona!=w.requestPersona||view.phase=="cancelled")return "player-request-retired";
    if(w.protectedPlayer&&(!view.locked||(view.phase!="pending"&&view.phase!="verifying")))return "original-player-period-finished";
    if(inputHostExitFenceRaised()||g_exitRequested.load(std::memory_order_acquire)||
        g_inputCaptureStop.load(std::memory_order_acquire))return "exit-or-capture-stop";
    if(w.intent!=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire)||
        g_inputHostVirtualIntentWriters.load(std::memory_order_acquire)||
        w.power!=currentPowerGeneration()||w.origin.suspendSerial!=g_sharedPowerSuspendSerial.load(std::memory_order_acquire)||
        (originalEpoch&&w.epoch!=g_inputHostEpochMirror.load(std::memory_order_acquire)))return "context-changed-during-snapshot";
    return nullptr;
}
static bool ready() {
    // This pending request owns lifting PowerAdmissionRevoked at final claim;
    // that scalar cannot itself veto its rearm. Public permission is separate.
    return g_inputReady.load(std::memory_order_acquire)&&
        g_powerLifecycle.load(std::memory_order_acquire)==PowerLifecycle::Ready&&
        !g_externalPriorityVirtualSuspended.load(std::memory_order_acquire);
}
// Caller holds only the original metadata mutex. No leaf/config/Host locks.
static bool atomicOwnerCurrent(const Work& w,std::uint64_t now) {
    return w.bound&&now>=w.signal&&now<w.deadline&&w.power==currentPowerGeneration()&&
        w.origin.suspendSerial==g_sharedPowerSuspendSerial.load(std::memory_order_acquire)&&
        w.intent==g_inputHostVirtualIntentRevision.load(std::memory_order_acquire)&&
        !g_inputHostVirtualIntentWriters.load(std::memory_order_acquire)&&
        w.epoch==g_inputHostEpochMirror.load(std::memory_order_acquire)&&
        !inputHostExitFenceRaised()&&!g_exitRequested.load(std::memory_order_acquire)&&
        !g_inputCaptureStop.load(std::memory_order_acquire);
}
static bool samePendingLocked(const Work& w) {
    return first.bound&&!consumed&&first.wire==w.wire&&
        g_inputHostPowerRearmGeneration.load(std::memory_order_acquire)==w.power&&g_inputHostPowerRearmReason==w.wire;
}
static void retire(const Work& w) {
    std::lock_guard<std::mutex> lock(g_inputHostPowerRequestMx);
    if(!samePendingLocked(w))return;
    consumed=true;first.queued=false;
    g_inputHostPowerRearmRequested.store(false,std::memory_order_release);
}
static void unqueue(const Work& w) {
    // Original settle/preflight/exception code may already have retained this
    // exact item. Observe that SAME queue, not a replacement retry queue.
    // Lock order: queue -> request; producers release request before enqueue.
    std::lock_guard<std::mutex> queueLock(g_inputHostLifecycleWorkMx);
    if(std::find(g_inputHostLifecycleWorkQueue.begin(),g_inputHostLifecycleWorkQueue.end(),w.wire)!=g_inputHostLifecycleWorkQueue.end())return;
    std::lock_guard<std::mutex> lock(g_inputHostPowerRequestMx);
    if(samePendingLocked(w))first.queued=false; // Original pending/deadline stay.
}
// Keep the historical ten-field queue format. An encoded string is never a
// capability: restore only the EXACT retained original signal, not live ON data.
static bool restoreQueued(Work& w) {
    std::lock_guard<std::mutex> lock(g_inputHostPowerRequestMx);
    if(!samePendingLocked(w)||!first.queued)return false;
    w=first;
    // first retains the original raw reason/wire. Only this decoded dispatch
    // view restores the existing main selector prefix; no new work or budget.
    w.reason="power-rearm:"+first.reason;return true;
}
// Ready-only, read-only delivery consumer of projectSharedPowerLifecycle.
// This updates delivery permission for the SAME original owner; it never calls
// signal/capture, opens admission, queues effects, resets an event or renews time.
static void readyDelivery(const sleeppower::SharedPowerV1& power) {
    if(!sleeppower::powerAdmitted(power)||!sharedPowerLifecycleCurrent(power))return;
    Work w;
    {std::lock_guard<std::mutex> lock(g_inputHostPowerRequestMx);
        if(consumed)return;
        if(first.capturePending){
            // The one original callback is still outside this metadata lock
            // reading leaves. Retain an actual projection, never a raw Ready
            // observation; capture commit must still validate the owner.
            if(signalOwnerCurrent(first,inputHostStartProtectionNow())&&
                first.power==power.generation&&first.origin.suspendSerial==power.suspendSerial&&
                power.observedNotificationSequence>=first.origin.observedNotificationSequence&&
                power.publicationRevision>=first.origin.publicationRevision&&sharedPowerLifecycleCurrent(power)&&
                (!first.deliveryAuthorized||power.observedNotificationSequence>=first.admitted.observedNotificationSequence)){
                first.admitted=power;first.deliveryAuthorized=true;
            }
            return;
        }
        if(!first.bound||!g_inputHostPowerRearmRequested.load(std::memory_order_acquire))return;
        w=first;}
    if(stale(w,inputHostStartProtectionNow())){retire(w);return;}
    if(w.power!=power.generation||w.origin.suspendSerial!=power.suspendSerial||
        power.observedNotificationSequence<w.origin.observedNotificationSequence)return;
    std::lock_guard<std::mutex> lock(g_inputHostPowerRequestMx);
    if(!samePendingLocked(w)||!atomicOwnerCurrent(w,inputHostStartProtectionNow())||!sharedPowerLifecycleCurrent(power))return;
    if(first.deliveryAuthorized&&power.observedNotificationSequence<first.admitted.observedNotificationSequence)return;
    first.admitted=power;first.deliveryAuthorized=true;
}
static bool deliveryCurrent(const Work& w) {
    return w.deliveryAuthorized&&ready()&&sharedPowerLifecycleCurrent(w.admitted)&&
        w.admitted.generation==w.power&&w.admitted.suspendSerial==w.origin.suspendSerial;
}
struct Scope;
static thread_local const Scope* active=nullptr;
struct Scope {
    const Work& work;const Scope* previous=active;
    std::uint64_t oldDeadline=g_inputHostSwitchRecoveryStartDeadline;
    std::uint64_t oldRequest=g_inputHostSwitchRecoveryStartRequestId,oldPower=g_inputHostSwitchRecoveryStartPower;
    std::uint64_t fixedDeadline=0;bool claimed=false,retainedInQueue=false;
    const bool* dispatchStarted=nullptr;
    const int exceptionsAtEntry=std::uncaught_exceptions();
    std::shared_ptr<sharedPower::Scope> publicPower;
    explicit Scope(const Work& w,const bool* originalDispatchStarted=nullptr):work(w),dispatchStarted(originalDispatchStarted){
        if(!w.bound)return;
        fixedDeadline=oldDeadline?(std::min)(oldDeadline,w.deadline):w.deadline;
        g_inputHostSwitchRecoveryStartDeadline=fixedDeadline;
        g_inputHostSwitchRecoveryStartRequestId=w.request;g_inputHostSwitchRecoveryStartPower=w.power;
    }
    bool claim() {
        if(claimed||!work.bound||stale(work,inputHostStartProtectionNow())||!ready())return false;
        sleeppower::SharedPowerV1 authorized;
        {
            std::lock_guard<std::mutex> lock(g_inputHostPowerRequestMx);
            if(!samePendingLocked(work)||!first.queued||!first.deliveryAuthorized)return false;
            authorized=first.admitted;
        }
        // Create/allocate/register outside metadata and Host locks. This is a
        // new permission for this effect; an old cancelled Scope never revives.
        auto permission=sharedPower::Scope::create(&readSharedSystemPower,g_sharedPowerRevocations,
            sgShouldKeepInputDuringStandby(),sharedPower::Use::Lifecycle);
        if(!permission||!sleeppower::samePowerRevocation(authorized,permission->power())||!permission->current())return false;
        {
            std::lock_guard<InputHostMutex> lock(g_inputHostMx);
            if(g_inputHostEpoch!=work.epoch||!atomicOwnerCurrent(work,inputHostStartProtectionNow())||!permission->current())return false;
        }
        if(stale(work,inputHostStartProtectionNow())||!ready()||!permission->current())return false;
        {
            std::lock_guard<std::mutex> lock(g_inputHostPowerRequestMx);
            const auto now=inputHostStartProtectionNow();
            if(!samePendingLocked(work)||!first.queued||!first.deliveryAuthorized||
                !sleeppower::samePowerRevocation(first.admitted,authorized)||
                !atomicOwnerCurrent(work,now)||now>=fixedDeadline||!ready()||!permission->current())return false;
            // Consume only at the final real branch, never at BUS dequeue.
            // After this commit uncertain/dispatched effects are never replayed.
            consumed=true;first.queued=false;
            g_inputHostPowerRearmRequested.store(false,std::memory_order_release);
        }
        publicPower=std::move(permission);claimed=true;active=this;return true;
    }
    bool effectCurrent(bool originalEpoch=true) const {
        return claimed&&publicPower&&publicPower->current()&&ready()&&
            !stale(work,inputHostStartProtectionNow(),originalEpoch)&&
            inputHostStartProtectionNow()<fixedDeadline&&publicPower->current();
    }
    void keepQueued() noexcept {retainedInQueue=true;}
    ~Scope(){
        // The original outer handler puts an UNATTEMPTED throwing dispatch
        // back at the queue head. Its existing queue ownership must survive;
        // settle also explicitly retains the exact item rather than redeliver.
        const bool originalRetainOnThrow=dispatchStarted&&!*dispatchStarted&&
            std::uncaught_exceptions()>exceptionsAtEntry;
        if(work.bound&&!claimed&&!retainedInQueue&&!originalRetainOnThrow){
            try{unqueue(work);}catch(...){/* Keep unresolved delivery, never invent completion. */}
        }
        active=previous;
        g_inputHostSwitchRecoveryStartDeadline=oldDeadline;
        g_inputHostSwitchRecoveryStartRequestId=oldRequest;g_inputHostSwitchRecoveryStartPower=oldPower;
    }
};
} // namespace ymcc::inputHost::msiPowerWork

// Historical exported name retained for the original Start/source consumers.
// Only a claimed original request and its current shared Scope grant time.
static std::uint64_t inputHostMsiPowerWorkRemaining(std::uint64_t ticket,const std::string& persona,std::uint64_t now,bool* activeExecution) {
    namespace runtime=ymcc::inputHost::msiPowerWork;
    const auto* scope=runtime::active;
    if(activeExecution)*activeExecution=scope&&scope->claimed&&scope->work.bound;
    if(!scope||!scope->claimed||!runtime::ready()||!scope->publicPower||!scope->publicPower->current())return 0;
    const auto& w=scope->work;
    if(!w.bound||w.request!=ticket||w.persona!=persona||(ticket&&w.requestPersona!=persona)||
        !ymcc::controllerPersona::currentOutput(persona)||!g_realStickTest.enabled||!g_realStickTest.virtualTargetEnabled||
        g_inputHostSwitchRecoveryStartRequestId!=ticket||g_inputHostSwitchRecoveryStartPower!=w.power||
        !scope->fixedDeadline||now>=scope->fixedDeadline||
        g_inputHostSwitchRecoveryStartDeadline!=scope->fixedDeadline)return 0;
    // A successful Start may create its OWN new Host epoch. All original
    // player/request/power bindings remain; actual Start checks its Host tuple.
    if(runtime::stale(w,now,false))return 0;
    const auto view=g_virtualStartProtection.view(now);
    if(view.requestId!=ticket||view.persona!=w.requestPersona||view.phase=="cancelled"||
        (w.protectedPlayer&&(!view.locked||(view.phase!="pending"&&view.phase!="verifying"))))return 0;
    const auto checkedNow=(std::max)(now,inputHostStartProtectionNow());
    if(checkedNow<w.signal||checkedNow>=scope->fixedDeadline||!runtime::ready()||
        !scope->publicPower->current()||w.power!=currentPowerGeneration()||
        w.origin.suspendSerial!=g_sharedPowerSuspendSerial.load(std::memory_order_acquire)||
        w.intent!=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire)||
        g_inputHostVirtualIntentWriters.load(std::memory_order_acquire)||inputHostExitFenceRaised()||
        g_exitRequested.load(std::memory_order_acquire)||g_inputCaptureStop.load(std::memory_order_acquire))return 0;
    return scope->fixedDeadline-checkedNow;
}
