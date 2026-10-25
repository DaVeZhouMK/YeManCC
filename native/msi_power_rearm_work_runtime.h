#pragma once
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <mutex>
#include <limits>
#include <string>
#include <vector>

// Immutable provenance for one existing MSI power notification, not another
// lifecycle. Metadata uses only the existing power-request mutex; no IO there.
namespace ymcc::inputHost::msiPowerWork {
struct Work {
    std::uint64_t power=0,intent=0,request=0,epoch=0,signal=0,deadline=0;
    std::string persona,requestPersona,reason,wire;
    bool protectedPlayer=false,bound=false,writingAtSignal=false;
};
inline constexpr const char* prefix="msi-power-rearm:";
inline constexpr std::uint64_t maxMs=30000;
inline constexpr std::size_t reasonMax=96,wireMax=512;
static Work first; // g_inputHostPowerRequestMx; retained after consumption for dedup.
static bool consumed=false; // Same mutex; duplicates never renew/execute again.
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
    if(wire.rfind(prefix,0)!=0)return true; // Only this typed prefix is parsed.
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
        {"budgetRenewed",false},{"newPlayerRequest",false}});}catch(...){}
}
// Snapshot at the ORIGINAL callback, before BUS/queue delay. Zero request and
// disabled are valid normal power contexts; neither creates a player request.
static Work signal(std::uint64_t power,const char* reason) {
    Work w;w.power=power;w.signal=inputHostStartProtectionNow();
    w.intent=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
    w.epoch=g_inputHostEpochMirror.load(std::memory_order_acquire);
    w.writingAtSignal=g_inputHostVirtualIntentWriters.load(std::memory_order_acquire)!=0;
    w.reason=boundedReason(reason);return w;
}
static void capture(Work& w) {
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
    w.bound=!w.writingAtSignal && !g_inputHostVirtualIntentWriters.load(std::memory_order_acquire) &&
        w.intent==g_inputHostVirtualIntentRevision.load(std::memory_order_acquire) &&
        w.epoch==g_inputHostEpochMirror.load(std::memory_order_acquire) &&
        w.power==currentPowerGeneration() && now<w.deadline && personaValid(w.persona);
    if(w.bound)w.wire=encode(w);
}
// No state locks held by the caller. All leaf snapshots finish before taking
// the Host or metadata lock, then atomic revocation is checked again afterwards.
static const char* stale(const Work& w,std::uint64_t now,bool originalEpoch=true) {
    if(!w.bound||!msiClawKnownBoardAuthority())return "unbound-or-foreign-power-work";
    if(now<w.signal||now>=w.deadline)return "original-power-budget-expired";
    if(w.power!=currentPowerGeneration())return "power-generation-retired";
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
        w.power!=currentPowerGeneration()||(originalEpoch&&w.epoch!=g_inputHostEpochMirror.load(std::memory_order_acquire)))return "context-changed-during-snapshot";
    return nullptr;
}
static bool ready() {
    return g_inputReady.load(std::memory_order_acquire)&&
        g_powerLifecycle.load(std::memory_order_acquire)==PowerLifecycle::Ready&&
        !g_inputHostPowerAdmissionRevoked.load(std::memory_order_acquire)&&
        !g_externalPriorityVirtualSuspended.load(std::memory_order_acquire);
}
struct Scope;
static thread_local const Scope* active=nullptr;
struct Scope {
    const Work& work;const Scope* previous=active;
    std::uint64_t oldDeadline=g_inputHostSwitchRecoveryStartDeadline;
    std::uint64_t oldRequest=g_inputHostSwitchRecoveryStartRequestId,oldPower=g_inputHostSwitchRecoveryStartPower;
    std::uint64_t fixedDeadline=0;bool claimed=false;
    explicit Scope(const Work& w):work(w){
        if(!w.bound)return;
        fixedDeadline=oldDeadline?(std::min)(oldDeadline,w.deadline):w.deadline;
        g_inputHostSwitchRecoveryStartDeadline=fixedDeadline;
        g_inputHostSwitchRecoveryStartRequestId=w.request;g_inputHostSwitchRecoveryStartPower=w.power;
    }
    bool claim() {
        if(!work.bound)return false;
        if(stale(work,inputHostStartProtectionNow())||!ready())return false;
        // A branch snapshot can wait on Host liveness. Check its real epoch
        // after that wait, without nesting persona/request/metadata locks.
        {
            std::lock_guard<InputHostMutex> lock(g_inputHostMx);
            if(g_inputHostEpoch!=work.epoch||work.intent!=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire)||
                g_inputHostVirtualIntentWriters.load(std::memory_order_acquire)||work.power!=currentPowerGeneration())return false;
        }
        {
            std::lock_guard<std::mutex> lock(g_inputHostPowerRequestMx);
            if(consumed||first.wire!=work.wire)return false;
            consumed=true;
        }
        // The metadata lock itself can race a newer callback/settings/Host.
        if(stale(work,inputHostStartProtectionNow())||!ready()||inputHostStartProtectionNow()>=fixedDeadline)return false;
        claimed=true;active=this;return true;
    }
    ~Scope(){active=previous;
        g_inputHostSwitchRecoveryStartDeadline=oldDeadline;
        g_inputHostSwitchRecoveryStartRequestId=oldRequest;g_inputHostSwitchRecoveryStartPower=oldPower;}
};
} // namespace ymcc::inputHost::msiPowerWork

// Parent's source gate first handles ordinary pending/verifying execution.
// Only an actually claimed typed power branch can authorize its SAME terminal
// request, including boot request zero. An encoded queue item or a generic
// outer TLS deadline is insufficient; protected requests cannot downgrade.
static std::uint64_t inputHostMsiPowerWorkRemaining(std::uint64_t ticket,const std::string& persona,std::uint64_t now,bool* activeExecution) {
    namespace runtime=ymcc::inputHost::msiPowerWork;
    const auto* scope=runtime::active;
    if(activeExecution)*activeExecution=msiClawKnownBoardAuthority()&&scope&&scope->claimed&&scope->work.bound;
    if(!scope||!scope->claimed||!runtime::ready())return 0;
    const auto& w=scope->work;
    if(!w.bound||w.request!=ticket||w.persona!=persona||(ticket&&w.requestPersona!=persona)||
        !ymcc::controllerPersona::currentOutput(persona)||!g_realStickTest.enabled||!g_realStickTest.virtualTargetEnabled||
        g_inputHostSwitchRecoveryStartRequestId!=ticket||g_inputHostSwitchRecoveryStartPower!=w.power||
        !scope->fixedDeadline||now>=scope->fixedDeadline||
        g_inputHostSwitchRecoveryStartDeadline!=scope->fixedDeadline)return 0;
    // Source admission runs after this execution has created its own Host, so
    // its epoch can legitimately advance. All ORIGINAL player/power bindings
    // still apply; the caller separately checks its actual current Host tuple.
    if(runtime::stale(w,now,false))return 0;
    const auto view=g_virtualStartProtection.view(now);
    if(view.requestId!=ticket||view.persona!=w.requestPersona||view.phase=="cancelled"||
        (w.protectedPlayer&&(!view.locked||(view.phase!="pending"&&view.phase!="verifying"))))return 0;
    // Leaf snapshots can cross a deadline or a revocation. Charge their real
    // elapsed time and recheck the original bindings after the final View read.
    const auto checkedNow=(std::max)(now,inputHostStartProtectionNow());
    if(checkedNow<w.signal||checkedNow>=scope->fixedDeadline||!runtime::ready()||
        w.power!=currentPowerGeneration()||w.intent!=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire)||
        g_inputHostVirtualIntentWriters.load(std::memory_order_acquire)||inputHostExitFenceRaised()||
        g_exitRequested.load(std::memory_order_acquire)||g_inputCaptureStop.load(std::memory_order_acquire))return 0;
    return scope->fixedDeadline-checkedNow;
}
