#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
#ifdef YMCC_NATIVE_FOREGROUND_POWER_ORDER
#include <chrono>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
#include "sleep_power_state.h"
// Only the power consumers below are extracted from production. These atomic
// facts and lazy UI terminals cannot reach a device, application or writer.
using WPARAM=std::uintptr_t;
constexpr WPARAM PBT_APMSUSPEND=4,PBT_APMRESUMECRITICAL=6,PBT_APMRESUMESUSPEND=7,PBT_APMRESUMEAUTOMATIC=18;
enum class PowerLifecycle : uint8_t {Ready,Suspending,Suspended,Resuming};
namespace ymcc::nativeForeground {enum class Profile {Msi,Rog,Gpd,Unsupported};}
static ymcc::nativeForeground::Profile selectedProfile=ymcc::nativeForeground::Profile::Msi;
static auto nativeForegroundProfile(){return selectedProfile;}
@@POWER_FAMILY@@
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<uint64_t> g_powerGeneration{20},g_msiNativeForegroundSuspendedGeneration{0},g_msiNativeForegroundLastResumeGeneration{0};
static uint64_t currentPowerGeneration(){return g_powerGeneration.load(std::memory_order_acquire);}
static std::atomic<unsigned long long> g_lastPowerResumeCommitTick{100},g_resumeReadyGeneration{20};
static std::atomic<uint64_t> g_msiNativeForegroundPowerHoldCommitTick{0};
static std::atomic<uint64_t> g_msiNativeForegroundEdge{1},g_msiNativeForegroundSourceRetryGeneration{0};
static std::atomic<bool> g_inputReady{true},g_msiNativeForegroundPowerHold{false},g_msiNativeForegroundResumePending{false};
static std::atomic<bool> g_msiNativeForegroundPowerTransitionObserved{false},g_msiNativeForegroundDesiredUi{false};
static std::mutex g_msiNativeForegroundPowerMx;
static std::atomic<bool> g_exitRequested{false};
static std::atomic<bool> g_msiNativeForegroundWorkPowerDeferred{false};
static uint64_t notificationSequence=0,suspendSerial=0;
static bool exitFence=false,powerPending=false;
static bool inputHostExitFenceRaised(){return exitFence;}
static uint64_t g_msiNativeForegroundLastReadySuspendSerial=0;
static int __cdecl readFixturePower(ymcc::sleeppower::SharedPowerV1* output,uint32_t bytes) noexcept {
    if(!output||bytes<sizeof(*output)||g_exitRequested.load())return 0;
    *output={};output->generation=currentPowerGeneration();
    output->phase=static_cast<uint32_t>(g_powerLifecycle.load());
    output->observedNotificationSequence=notificationSequence;output->suspendSerial=suspendSerial;
    output->flags=ymcc::sleeppower::kKnown|(output->phase==0?ymcc::sleeppower::kHardwareAllowed:0u)|
        (powerPending?ymcc::sleeppower::kNotificationPending:0u);
    return 1;
}
static bool captureSharedPowerLifecycle(ymcc::sleeppower::SharedPowerV1& output) noexcept {
    return ymcc::sleeppower::capturePower(&readFixturePower,output);
}
static bool sharedPowerLifecycleCurrent(const ymcc::sleeppower::SharedPowerV1& power) noexcept {
    return ymcc::sleeppower::powerCurrent(&readFixturePower,power);
}
static unsigned queued=0,identityReady=0,eligibilityReads=0;
static unsigned queueAttempts=0,allocationFailures=0;
static void (*beforeQueueLock)()=nullptr;
static void (*duringUiRead)()=nullptr;
static void msiNativeForegroundIdentityReady(){++identityReady;}
static bool msiNativeForegroundUiEligible(){++eligibilityReads;if(duringUiRead)duringUiRead();return true;}
namespace ymcc::inputHost::diagnostics {struct Scope {};}
namespace ymcc::inputHost::sharedPower {struct Scope { bool current() const noexcept{return true;} };}
namespace ymcc::inputHost::queuedPlayerStart {
static std::string bind(const std::string& original){
    ++queueAttempts;
    if(allocationFailures){--allocationFailures;throw std::bad_alloc{};}
    if(beforeQueueLock)beforeQueueLock();
    return original;
}
}
struct InputHostLateHidePublication {bool alreadyCompleted=false;};
static bool inputHostLateHideReuseQueuedPublication(InputHostLateHidePublication*,bool){throw std::logic_error("late hide");}
static void inputHostLateHideCommitQueuedPublication(InputHostLateHidePublication*){throw std::logic_error("late hide");}
static bool msiNativeRearIntentCurrent(const std::string&){return false;}
static bool inputHostExitFenceBlocks(bool raised,const std::string&){return raised;}
static void inputHostExitFenceLogRefusal(const char*,const char*){}
@@AUDIT_APPLIES@@
static void inputHostSwitchAuditEnqueue(const std::string& reason,size_t){
    if(inputHostSwitchAuditApplies(reason))throw std::logic_error("foreground entered allocating post-insertion audit");
    ++queued;
}
@@QUEUE_TYPES@@
static std::mutex g_inputHostLifecycleWorkMx;
static std::atomic<bool> g_inputHostLifecycleWorkRequested{false};
static std::deque<InputHostLifecycleWork> g_inputHostLifecycleWorkQueue;
@@QUEUE_FUNCTIONS@@
@@POWER_QUEUE@@
@@POWER_UI@@
@@POWER_EVENT@@
@@POWER_RELEASE@@
@@POWER_READY@@
static unsigned actionTerminals=0;
static void msiNativeForegroundHandleWork(const MsiNativeForegroundReadyWork* ready){
    if(msiNativeForegroundReadyWorkAdmitted(ready))++actionTerminals;
}
static void consumeForeground(){
    std::string reason;
    std::shared_ptr<ymcc::inputHost::sharedPower::Scope> sleepRepairPower;
    std::optional<MsiNativeForegroundReadyWork> foregroundReady;
    {
        std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);
        if(g_inputHostLifecycleWorkQueue.empty())return;
        @@QUEUE_POP@@
    }
    if(inputHostSleepRepairWork(reason))throw std::logic_error("sleep repair");
    @@QUEUE_DISPATCH@@
}
struct PowerFieldObservation { WPARAM event; uint64_t generation; PowerLifecycle phase; unsigned sourceLine; };
@@POWER_FIELD@@
int main(int argc,char** argv){
    unsigned cases=0;std::vector<std::string> failures,caseNames,scenarios;
    json fieldTrace=json::array();
    std::string prefix;
    const auto check=[&](const char* name,bool ok){++cases;caseNames.push_back(prefix+name);if(!ok)failures.push_back(prefix+name);};
    const auto begin=[&](const std::string& family,const char* scenario){
        prefix=family+":"+scenario+":";scenarios.push_back(family+":"+scenario);
        g_powerLifecycle=PowerLifecycle::Ready;g_powerGeneration=20;g_inputReady=true;
        g_lastPowerResumeCommitTick=100;g_resumeReadyGeneration=20;g_msiNativeForegroundPowerHoldCommitTick=0;
        g_msiNativeForegroundPowerHold=false;g_msiNativeForegroundResumePending=false;
        g_msiNativeForegroundPowerTransitionObserved=false;g_msiNativeForegroundSuspendedGeneration=0;
        g_msiNativeForegroundLastResumeGeneration=0;g_msiNativeForegroundDesiredUi=false;
        g_msiNativeForegroundEdge=1;g_msiNativeForegroundSourceRetryGeneration=0;
        queued=identityReady=eligibilityReads=0;
        queueAttempts=allocationFailures=actionTerminals=0;beforeQueueLock=nullptr;duringUiRead=nullptr;
        exitFence=powerPending=false;notificationSequence=suspendSerial=0;
        g_exitRequested=false;g_msiNativeForegroundWorkPowerDeferred=false;
        g_msiNativeForegroundLastReadySuspendSerial=0;
        g_inputHostLifecycleWorkQueue.clear();g_inputHostLifecycleWorkRequested=false;
    };
    const auto suspend=[] {msiNativeForegroundPowerEvent(PBT_APMSUSPEND);};
    const auto resume=[] {msiNativeForegroundPowerEvent(PBT_APMRESUMEAUTOMATIC);};
    const auto commitReady=[] {
        g_resumeReadyGeneration=currentPowerGeneration();
        ++g_lastPowerResumeCommitTick;
        g_powerLifecycle=PowerLifecycle::Ready;
    };
    const bool mutationOnly=argc>2&&std::string(argv[2])=="old-generation-only";
    const bool fieldOnly=argc>2&&std::string(argv[2])=="field-replay-only";
    const bool queueOnly=argc>2&&std::string(argv[2])=="queue-only";
    const auto replayField=[&](const std::string& family){
        begin(family,"field-win5-111926-notification-order");
        for(std::size_t index=0;index<kWin5PowerObservations.size();++index){
            const auto& observation=kWin5PowerObservations[index];
            g_powerGeneration=observation.generation;g_powerLifecycle=observation.phase;
            const auto beforeQueues=queued;const auto beforeEdge=g_msiNativeForegroundEdge.load();
            msiNativeForegroundPowerEvent(observation.event);
            // Supply the shared commit in the fixture at the first recorded
            // Ready. The diagnostic log itself contains no commit tick.
            if(index==4)commitReady();
            const bool didRelease=msiNativeForegroundTryReleasePowerHold();
            fieldTrace.push_back({{"family",family},{"sourceLine",observation.sourceLine},{"event",observation.event},
                {"generation",observation.generation},{"released",didRelease},
                {"powerHold",g_msiNativeForegroundPowerHold.load()},{"resumePending",g_msiNativeForegroundResumePending.load()},
                {"transitionObserved",g_msiNativeForegroundPowerTransitionObserved.load()}});
            if(index<4)check(("step-"+std::to_string(index)+"-held").c_str(),!didRelease&&g_msiNativeForegroundPowerHold);
            else if(index==4)check("step-4-ready-releases",didRelease&&!g_msiNativeForegroundPowerHold&&
                !g_msiNativeForegroundResumePending&&!g_msiNativeForegroundPowerTransitionObserved&&g_msiNativeForegroundLastResumeGeneration==9893);
            else check("step-5-duplicate-is-inert",!didRelease&&!g_msiNativeForegroundPowerHold&&queued==beforeQueues&&g_msiNativeForegroundEdge==beforeEdge);
        }
    };
    for(const auto& family:std::vector<std::pair<std::string,ymcc::nativeForeground::Profile>>{
            {"msi",ymcc::nativeForeground::Profile::Msi},{"rog",ymcc::nativeForeground::Profile::Rog},{"gpd",ymcc::nativeForeground::Profile::Gpd}}){
        if(queueOnly||(mutationOnly&&family.first!="msi"))continue;
        if(fieldOnly&&family.first!="gpd")continue;
        selectedProfile=family.second;
        if(fieldOnly){replayField(family.first);continue;}
        if(!mutationOnly){
            begin(family.first,"ready-same-generation");suspend();resume();
            check("held",!msiNativeForegroundTryReleasePowerHold()&&g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundResumePending&&!g_msiNativeForegroundPowerTransitionObserved);
            begin(family.first,"cross-generation");suspend();g_powerGeneration=21;resume();commitReady();
            check("released",msiNativeForegroundTryReleasePowerHold()&&!g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundResumePending);
            check("recorded-and-cleared",g_msiNativeForegroundLastResumeGeneration==21&&!g_msiNativeForegroundPowerTransitionObserved);
        }
        begin(family.first,"late-suspend-same-generation");g_powerLifecycle=PowerLifecycle::Resuming;suspend();resume();
        check("nonready-held",!msiNativeForegroundTryReleasePowerHold()&&g_msiNativeForegroundPowerHold&&g_msiNativeForegroundPowerTransitionObserved);
        commitReady();
        check("released",msiNativeForegroundTryReleasePowerHold()&&!g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundResumePending&&!g_msiNativeForegroundPowerTransitionObserved&&g_msiNativeForegroundLastResumeGeneration==20);
        if(mutationOnly)continue;
        begin(family.first,"resume-without-suspended-owner");
        resume();msiNativeForegroundPowerEvent(PBT_APMRESUMESUSPEND);msiNativeForegroundPowerEvent(PBT_APMRESUMECRITICAL);
        check("no-new-hold",!msiNativeForegroundTryReleasePowerHold()&&!g_msiNativeForegroundPowerHold&&
            !g_msiNativeForegroundResumePending&&queued==0&&g_msiNativeForegroundEdge==1);
        begin(family.first,"duplicate-notifications");suspend();g_powerGeneration=21;resume();
        const auto firstResumeQueued=queued;
        msiNativeForegroundPowerEvent(PBT_APMRESUMESUSPEND);msiNativeForegroundPowerEvent(PBT_APMRESUMECRITICAL);
        commitReady();
        check("one-release",queued==firstResumeQueued&&msiNativeForegroundTryReleasePowerHold());
        const auto priorQueues=queued;const auto priorEdge=g_msiNativeForegroundEdge.load();
        resume();msiNativeForegroundPowerEvent(PBT_APMRESUMESUSPEND);msiNativeForegroundPowerEvent(PBT_APMRESUMECRITICAL);
        check("no-additional-work",!msiNativeForegroundTryReleasePowerHold()&&queued==priorQueues&&g_msiNativeForegroundEdge==priorEdge);
        begin(family.first,"input-not-ready");suspend();g_powerGeneration=21;resume();commitReady();g_inputReady=false;
        check("held",!msiNativeForegroundTryReleasePowerHold()&&g_msiNativeForegroundPowerHold);
        g_inputReady=true;check("released-after-input",msiNativeForegroundTryReleasePowerHold());
        begin(family.first,"power-not-ready");suspend();g_powerGeneration=21;resume();commitReady();
        for(auto phase:{PowerLifecycle::Suspending,PowerLifecycle::Suspended,PowerLifecycle::Resuming}){
            g_powerLifecycle=phase;check("held",!msiNativeForegroundTryReleasePowerHold()&&g_msiNativeForegroundPowerHold&&g_msiNativeForegroundPowerTransitionObserved);
        }
        g_powerLifecycle=PowerLifecycle::Ready;check("released-after-ready",msiNativeForegroundTryReleasePowerHold());
        begin(family.first,"maintenance-observes-transition");suspend();resume();g_powerLifecycle=PowerLifecycle::Resuming;
        check("observed",!msiNativeForegroundTryReleasePowerHold()&&g_msiNativeForegroundPowerTransitionObserved);
        commitReady();check("same-generation-release",msiNativeForegroundTryReleasePowerHold());
        begin(family.first,"two-rounds-no-transition-credit");g_powerLifecycle=PowerLifecycle::Resuming;suspend();resume();commitReady();
        check("first-release",msiNativeForegroundTryReleasePowerHold()&&!g_msiNativeForegroundPowerTransitionObserved);
        suspend();resume();check("second-held",!msiNativeForegroundTryReleasePowerHold()&&g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundPowerTransitionObserved);
        // Another suspend resets a transition observed in an unfinished round.
        g_powerLifecycle=PowerLifecycle::Resuming;(void)msiNativeForegroundTryReleasePowerHold();
        g_powerLifecycle=PowerLifecycle::Ready;suspend();resume();
        check("unfinished-round-credit-reset",!msiNativeForegroundTryReleasePowerHold()&&g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundPowerTransitionObserved);
        begin(family.first,"shared-ready-required");g_powerLifecycle=PowerLifecycle::Resuming;suspend();g_powerLifecycle=PowerLifecycle::Ready;
        check("no-new-commit-held",!msiNativeForegroundTryReleasePowerHold()&&g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundResumePending);
        begin(family.first,"power-mutex-defers-new-suspend");suspend();g_powerGeneration=21;resume();
        commitReady();
        std::promise<void> locked,release;auto held=locked.get_future();auto released=release.get_future();
        std::atomic<bool> ownerTimedOut{false};
        std::thread owner([&]{
            std::unique_lock lock(g_msiNativeForegroundPowerMx);locked.set_value();
            if(released.wait_for(std::chrono::seconds(1))!=std::future_status::ready)ownerTimedOut=true;
        });
        held.wait();const auto queuedBeforeBusy=queued;
        const auto busyStart=std::chrono::steady_clock::now();
        const bool busyReleased=msiNativeForegroundTryReleasePowerHold();
        const auto busyMs=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-busyStart).count();
        check("busy-defers",!busyReleased&&busyMs<250);
        check("busy-preserves-scalars",g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundResumePending&&
            !g_msiNativeForegroundPowerTransitionObserved&&g_msiNativeForegroundSuspendedGeneration==20&&
            g_msiNativeForegroundLastResumeGeneration==0&&queued==queuedBeforeBusy);
        release.set_value();owner.join();check("owner-released",!ownerTimedOut);
        suspend();
        check("new-suspend-remains-held",!msiNativeForegroundTryReleasePowerHold()&&g_msiNativeForegroundPowerHold&&
            !g_msiNativeForegroundResumePending&&g_msiNativeForegroundSuspendedGeneration==21);
        resume();check("new-suspend-needs-new-commit",!msiNativeForegroundTryReleasePowerHold()&&g_msiNativeForegroundPowerHold);
        g_powerLifecycle=PowerLifecycle::Resuming;(void)msiNativeForegroundTryReleasePowerHold();commitReady();
        check("current-resumes-after-commit",msiNativeForegroundTryReleasePowerHold()&&!g_msiNativeForegroundPowerHold&&
            !g_msiNativeForegroundResumePending&&!g_msiNativeForegroundPowerTransitionObserved&&g_msiNativeForegroundLastResumeGeneration==21);
        begin(family.first,"missing-resume-new-commit");suspend();
        check("baseline-captured",g_msiNativeForegroundPowerHoldCommitTick==100&&!g_msiNativeForegroundResumePending);
        g_lastPowerResumeCommitTick=101;
        check("new-commit-releases",msiNativeForegroundTryReleasePowerHold()&&!g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundResumePending&&g_msiNativeForegroundLastResumeGeneration==20);
        begin(family.first,"missing-resume-old-commit");suspend();
        check("old-commit-held",!msiNativeForegroundTryReleasePowerHold()&&g_msiNativeForegroundPowerHold&&g_msiNativeForegroundPowerHoldCommitTick==100);
        begin(family.first,"missing-resume-wrong-generation");suspend();g_lastPowerResumeCommitTick=101;g_resumeReadyGeneration=19;
        check("foreign-commit-held",!msiNativeForegroundTryReleasePowerHold()&&g_msiNativeForegroundPowerHold);
        g_resumeReadyGeneration=20;check("current-commit-releases",msiNativeForegroundTryReleasePowerHold()&&!g_msiNativeForegroundPowerHold);
        begin(family.first,"missing-resume-zero-commit");suspend();g_lastPowerResumeCommitTick=0;
        check("zero-commit-held",!msiNativeForegroundTryReleasePowerHold()&&g_msiNativeForegroundPowerHold);
        replayField(family.first);
        begin(family.first,"repeated-sleep-wake-cycles");
        for(unsigned round=0;round<4;++round){
            const auto checkRound=[&](const char* name,bool ok){const auto label="round-"+std::to_string(round)+":"+name;check(label.c_str(),ok);};
            const auto nextGeneration=g_powerGeneration.load()+(round%2==0?1:0);
            suspend();
            checkRound("new-suspend-held",!msiNativeForegroundTryReleasePowerHold()&&g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundResumePending);
            g_powerGeneration=nextGeneration;g_powerLifecycle=PowerLifecycle::Resuming;
            if(round%2)suspend(); // late suspend within the already resuming generation
            else (void)msiNativeForegroundTryReleasePowerHold();
            g_lastPowerResumeCommitTick.fetch_add(1);g_resumeReadyGeneration=nextGeneration;
            g_powerLifecycle=PowerLifecycle::Ready;g_inputReady=false;
            checkRound("commit-waits-input",!msiNativeForegroundTryReleasePowerHold()&&g_msiNativeForegroundPowerHold);
            g_inputReady=true;
            if(round%2)resume(); // other rounds deliberately omit the raw resume callback
            checkRound("one-current-release",msiNativeForegroundTryReleasePowerHold()&&!g_msiNativeForegroundPowerHold&&
                !g_msiNativeForegroundResumePending&&g_msiNativeForegroundLastResumeGeneration==nextGeneration);
            const auto queuesAfterRelease=queued;const auto edgeAfterRelease=g_msiNativeForegroundEdge.load();
            resume();msiNativeForegroundPowerEvent(PBT_APMRESUMESUSPEND);
            checkRound("duplicates-do-not-reopen",!msiNativeForegroundTryReleasePowerHold()&&!g_msiNativeForegroundPowerHold&&
                queued==queuesAfterRelease&&g_msiNativeForegroundEdge==edgeAfterRelease);
        }
        begin(family.first,"shared-commit-waits-input");suspend();g_lastPowerResumeCommitTick=101;g_inputReady=false;
        check("input-not-ready-held",!msiNativeForegroundTryReleasePowerHold()&&g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundResumePending);
        g_inputReady=true;check("current-input-releases",msiNativeForegroundTryReleasePowerHold()&&!g_msiNativeForegroundPowerHold);
        begin(family.first,"next-suspend-rebases-commit");suspend();g_lastPowerResumeCommitTick=101;
        check("first-commit-releases",msiNativeForegroundTryReleasePowerHold());suspend();
        check("old-commit-cannot-release-next",!msiNativeForegroundTryReleasePowerHold()&&g_msiNativeForegroundPowerHold&&
            !g_msiNativeForegroundResumePending&&g_msiNativeForegroundPowerHoldCommitTick==101);
        begin(family.first,"shared-commit-waits-power");suspend();g_lastPowerResumeCommitTick=101;g_powerLifecycle=PowerLifecycle::Resuming;
        check("nonready-commit-held",!msiNativeForegroundTryReleasePowerHold()&&g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundResumePending);
        g_powerLifecycle=PowerLifecycle::Ready;check("ready-commit-releases",msiNativeForegroundTryReleasePowerHold()&&!g_msiNativeForegroundPowerHold);
    }
    if(!mutationOnly&&!fieldOnly&&!queueOnly){
        selectedProfile=ymcc::nativeForeground::Profile::Unsupported;begin("unsupported","no-business");suspend();resume();
        check("inert",!msiNativeForegroundTryReleasePowerHold()&&!g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundResumePending&&queued==0&&identityReady==0&&eligibilityReads==0&&g_msiNativeForegroundEdge==1);
    }
    if(!mutationOnly&&!fieldOnly){
        selectedProfile=ymcc::nativeForeground::Profile::Msi;
        const auto ready=[] {
            ymcc::sleeppower::SharedPowerV1 power;
            if(!captureSharedPowerLifecycle(power))throw std::logic_error("not Ready");
            return msiNativeForegroundSharedPowerReady(power);
        };
        begin("msi","retry-queue-revocation");
        allocationFailures=1;beforeQueueLock=[] {++notificationSequence;};
        ready();
        check("retried-once",queueAttempts==2);
        check("stale-retry-refused",g_inputHostLifecycleWorkQueue.empty()&&!g_inputHostLifecycleWorkRequested);
        check("zero-action-deferred",g_msiNativeForegroundWorkPowerDeferred);
        if(!queueOnly){
            beforeQueueLock=nullptr;ready();ready();consumeForeground();
            check("next-ready-delivered-once",actionTerminals==1&&g_inputHostLifecycleWorkQueue.empty()&&!g_msiNativeForegroundWorkPowerDeferred);
            begin("msi","dequeued-ready-revocation");
            allocationFailures=1;ready();
            check("one-accepted-bound-work",queueAttempts==2&&g_inputHostLifecycleWorkQueue.size()==1&&
                g_inputHostLifecycleWorkQueue.front().foregroundReady.has_value());
            ++notificationSequence;powerPending=true;consumeForeground();
            check("stale-work-no-action",actionTerminals==0&&g_msiNativeForegroundWorkPowerDeferred);
            powerPending=false;ready();consumeForeground();
            check("same-generation-replacement-runs",actionTerminals==1&&g_inputHostLifecycleWorkQueue.empty());
            begin("msi","raw-suspend-before-public-latch");
            ready();g_msiNativeForegroundPowerHold=true;consumeForeground();
            check("old-ready-blocked-by-hold",actionTerminals==0);
            ++notificationSequence;++suspendSerial;ready();consumeForeground();
            check("new-ready-releases-hold",actionTerminals==1&&!g_msiNativeForegroundPowerHold);
            begin("msi","new-ready-during-old-ui-read");
            duringUiRead=[] {
                duringUiRead=nullptr;++notificationSequence;++suspendSerial;
                ymcc::sleeppower::SharedPowerV1 newer;
                if(!captureSharedPowerLifecycle(newer)||!msiNativeForegroundSharedPowerReady(newer))
                    throw std::logic_error("new Ready rejected");
            };
            ready();
            check("old-ready-cannot-replace-new-edge",g_msiNativeForegroundEdge==2&&g_inputHostLifecycleWorkQueue.size()==1);
            consumeForeground();
            check("newer-ready-dispatched-once",actionTerminals==1&&!g_msiNativeForegroundWorkPowerDeferred);
            begin("msi","bounded-allocation-failure");
            allocationFailures=3;ready();
            const bool bounded=queueAttempts==2&&actionTerminals==0&&g_inputHostLifecycleWorkQueue.empty()&&
                g_msiNativeForegroundWorkPowerDeferred;
            ready();consumeForeground();ready();
            check("bounded-failure-then-one-current-retry",bounded&&queueAttempts==4&&actionTerminals==1&&
                g_inputHostLifecycleWorkQueue.empty()&&!g_msiNativeForegroundWorkPowerDeferred);
            begin("msi","queue-must-not-coalesce-across-power");
            ready();
            ++suspendSerial;++notificationSequence;
            msiNativeForegroundContinueDeferredReady({}); // no authority to reinterpret old work
            const auto fresh=[] {ymcc::sleeppower::SharedPowerV1 power;captureSharedPowerLifecycle(power);return power;}();
            const MsiNativeForegroundReadyWork work{fresh,g_msiNativeForegroundEdge};
            inputHostQueueMsiNativeForegroundReady(work);inputHostQueueMsiNativeForegroundReady(work);
            check("only-exact-current-work-coalesces",g_inputHostLifecycleWorkQueue.size()==2);
            consumeForeground();consumeForeground();
            check("new-suspend-serial-runs-once",actionTerminals==1);
        }
    }
    const json report={{"passed",failures.empty()},{"cases",cases},{"caseNames",caseNames},{"scenarios",scenarios},{"failures",failures},{"fieldTrace",fieldTrace},
        {"mode","power-order"},{"hardwareAccess",false},{"realProcessesLaunched",false},{"businessConsumerExecuted",false},
        {"boundary","Real power/family/UI consumers; controlled atomic facts and lazy UI terminals. No downstream writer or full MSI proof."}};
    if(argc>1){std::ofstream file(argv[1]);file<<report.dump(2)<<"\n";}
    std::cout<<report.dump()<<"\n";return failures.empty()?0:1;
}
#else
using DWORD=unsigned long;
using HANDLE=void*;
using BOOL=int;
namespace fspath=std::filesystem;
constexpr DWORD ERROR_SUCCESS=0,STILL_ACTIVE=259,WAIT_OBJECT_0=0,WAIT_TIMEOUT=258;
constexpr DWORD INVALID_FILE_ATTRIBUTES=0xffffffff,STARTF_USESHOWWINDOW=1,SW_HIDE=0,CREATE_NO_WINDOW=0x08000000,EXTENDED_STARTUPINFO_PRESENT=0x80000;
constexpr BOOL TRUE=1,FALSE=0;
struct STARTUPINFOW {std::size_t cb=0;DWORD dwFlags=0;unsigned short wShowWindow=0;};
struct PROCESS_INFORMATION {HANDLE hProcess=nullptr,hThread=nullptr;};
struct HidHidePhysicalIdentity {std::string deviceInstanceId;};
struct HidHideVisibilitySnapshot {
    std::string hiddenRaw,applicationsRaw,cloakRaw,inverseRaw;
    std::vector<std::wstring> hidden,applications;
    bool valid=false,cloaking=false;
};
static std::string trim_ascii(std::string text){const auto begin=text.find_first_not_of(" \r\n\t");return begin==std::string::npos?std::string():text.substr(begin,text.find_last_not_of(" \r\n\t")-begin+1);}
static std::wstring U2W(const std::string& text){return std::wstring(text.begin(),text.end());}
static std::string W2U(const std::wstring& text){return std::string(text.begin(),text.end());}
static bool seamActive=false;
static bool ymStartSeamActive(){return seamActive;}
static void ymStartSeamTrace(const char*){}
static std::string reply;
static bool runOk=true;
static DWORD runExit=0;
static std::vector<std::wstring> commands;
static bool hidHideRun(const std::wstring& command,std::string* output=nullptr,DWORD* exitCode=nullptr){
    commands.push_back(command);
    if(exitCode)*exitCode=runExit;
    if(output){
        if(command==L"--dev-list")*output="--dev-hide \"HID\\GAME\"\n";
        else if(command==L"--app-list")*output="--app-reg \"C:\\YMCC\\YeManCC.exe\"\n";
        else if(command==L"--cloak-state")*output="--cloak-on\n";
        else if(command==L"--inv-state")*output="--inv-off\n";
        else *output=reply;
    }
    return runOk;
}
@@PATH_PARSER@@
@@LEGACY_SNAPSHOT@@
@@SNAPSHOT@@
// This fixture exercises snapshot/async transport, not admission or writer IO.
// Dependencies of other branches in the extracted functions stay lazy; any
// unexpected use is counted even when the production consumer catches it.
static unsigned unexpectedDependencyCalls=0;
template<class T> [[noreturn]] static T unexercisedDependency(const char* name){
    ++unexpectedDependencyCalls;throw std::logic_error(name);
}
namespace ymcc::nativeForeground {
enum class Profile {Msi,Rog,Gpd,Unsupported};
static bool gameNodeRefreshMutationAllowed(Profile,const std::string&,const std::string&){return true;}
static bool nativeRootPortCycleAllowed(Profile,const std::string&){return unexercisedDependency<bool>("root-cycle admission");}
}
struct InertTrackedJob {
    bool prepareTracked(STARTUPINFOW&,const std::wstring&,const std::vector<HANDLE>&){return unexercisedDependency<bool>("tracked job prepare");}
    DWORD error()const{return unexercisedDependency<DWORD>("tracked job error");}
    DWORD flags()const{return unexercisedDependency<DWORD>("tracked job flags");}
    STARTUPINFOW* startup(){return unexercisedDependency<STARTUPINFOW*>("tracked job startup");}
};
namespace ymcc::nativeRefreshWriter {
struct Session {};
struct LaunchScope {
    template<class... Args> explicit LaunchScope(Args&&...){unexercisedDependency<void>("writer construction");}
    bool ready()const{return unexercisedDependency<bool>("writer ready");}
    DWORD error()const{return unexercisedDependency<DWORD>("writer error");}
    std::string reason()const{return unexercisedDependency<std::string>("writer reason");}
    HANDLE inheritedLease()const{return unexercisedDependency<HANDLE>("writer lease");}
    InertTrackedJob& job(){return unexercisedDependency<InertTrackedJob&>("writer job");}
    std::wstring jobName()const{return unexercisedDependency<std::wstring>("writer job name");}
    bool reserve(){return unexercisedDependency<bool>("writer reserve");}
    bool creationReturned(HANDLE){return unexercisedDependency<bool>("writer creation receipt");}
    bool retire(HANDLE){return unexercisedDependency<bool>("writer retirement");}
    struct Attempt {std::string attemptId;};
    const Attempt* attempt()const{return unexercisedDependency<const Attempt*>("writer attempt");}
};
}
namespace ymcc::input {
struct OneShotHelperDiagnostic {
    bool prepare(){return false;}
    void started(){}
    STARTUPINFOW* startup(){return nullptr;}
    bool prepareTrackedLaunch(InertTrackedJob&,const std::wstring&,const std::vector<HANDLE>&){return unexercisedDependency<bool>("diagnostic tracked launch");}
};
}
struct NativeForegroundRefreshTarget {HidHidePhysicalIdentity source;bool xusb=false;};
struct MsiColdReturnReceipt {};
@@LEASE@@
static MsiNativeForegroundLease g_msiNativeForegroundLease;
static std::atomic<bool> g_msiNativeForegroundCyclePending{false};
static std::atomic<std::uint64_t> g_msiNativeForegroundEdge{7};
static std::atomic<std::uint64_t> g_sharedPowerSuspendSerial{0};
static std::uint64_t fixturePower=3;
static bool fixturePendingSuspend=false;
static std::atomic<bool> g_inputReady{true},g_msiNativeForegroundPowerHold{false};
static uint64_t currentPowerGeneration(){return fixturePower;}
static bool hardwareWriteAllowed(){return !fixturePendingSuspend;}
static struct FixturePowerDispatch {
    std::uint64_t boundary=0;
    uint64_t observedSleepBoundarySequence()const{return boundary;}
} g_sleepPowerDispatch;
static std::function<void()> duringHash;
static HANDLE childHandle=reinterpret_cast<HANDLE>(11),threadHandle=reinterpret_cast<HANDLE>(12);
static std::vector<DWORD> waits;
static std::vector<HANDLE> closed;
static unsigned launches=0,settles=0,receipts=0,launchFlags=0;
static bool launchOk=true,exitQueryOk=true;
static DWORD waitResult=WAIT_TIMEOUT,childExit=0;
static DWORD GetFileAttributesW(const wchar_t*){return 0;}
static DWORD GetLastError(){return 5;}
static DWORD GetCurrentProcessId(){return 42;}
static DWORD WaitForSingleObject(HANDLE handle,DWORD timeout){if(handle!=childHandle)throw std::runtime_error("foreign handle");waits.push_back(timeout);return waitResult;}
static BOOL GetExitCodeProcess(HANDLE handle,DWORD* code){if(handle!=childHandle)throw std::runtime_error("foreign exit query");*code=childExit;return exitQueryOk?TRUE:FALSE;}
static BOOL CloseHandle(HANDLE handle){closed.push_back(handle);return TRUE;}
static std::wstring inputHostExecutablePath(){return L"C:\\YMCC\\input-host\\YeManInputHost.exe";}
static std::string sha256File(const std::wstring&){if(duringHash)duringHash();return "fixture-image-identity";}
namespace ymcc::processEfficiency {
static bool createProcess(const wchar_t*,wchar_t*,void*,void*,BOOL,DWORD flags,void*,const wchar_t*,STARTUPINFOW*,PROCESS_INFORMATION* process){
    ++launches;launchFlags=flags;if(!launchOk)return false;process->hProcess=childHandle;process->hThread=threadHandle;return true;
}
}
static void msiNativeForegroundSettleHelperDiagnosticLocked(DWORD){++settles;}
static void msiNativeForegroundReceipt(const char*,const char*){++receipts;}
static bool msiNativeForegroundLeaseCurrent(){return true;}
static bool msiPostHideContextCurrentLocked(){return unexercisedDependency<bool>("post-hide admission");}
static bool msiPostHideExecutionCurrentLocked(){return unexercisedDependency<bool>("post-hide execution");}
static bool msiColdReturnContextCurrentLocked(){return unexercisedDependency<bool>("cold return admission");}
static std::shared_ptr<ymcc::nativeRefreshWriter::Session> g_postHideSharedWriterSession;
static std::wstring nativeRefreshWriterRecordPath(){return unexercisedDependency<std::wstring>("writer record path");}
static std::string inputHostTargetReturnMachineBinding(){return unexercisedDependency<std::string>("writer machine binding");}
static json nativeRefreshWriterEvidence(const ymcc::nativeRefreshWriter::LaunchScope&,const char*){return unexercisedDependency<json>("writer evidence");}
namespace ymcc::targetReturn::process {
struct Identity {DWORD pid;std::string creationTime;};
static std::optional<Identity> capture(HANDLE){return unexercisedDependency<std::optional<Identity>>("parent capture");}
}
struct SECURITY_ATTRIBUTES {DWORD nLength;void* lpSecurityDescriptor;BOOL bInheritHandle;};
static HANDLE GetCurrentProcess(){return unexercisedDependency<HANDLE>("parent handle");}
static HANDLE CreateEventW(SECURITY_ATTRIBUTES*,BOOL,BOOL,const wchar_t*){return unexercisedDependency<HANDLE>("cancellation event");}
static int msiNativeForegroundPollCycleLocked();
@@CYCLE@@
@@POLL@@
int main(int argc,char** argv){
    unsigned cases=0;
    std::vector<std::string> failures;
    const auto check=[&](const std::string& name,bool ok){++cases;if(!ok)failures.push_back(name);};
    const auto query=[&](const std::string& output,bool expected){
        reply=output;commands.clear();HidHideVisibilitySnapshot snapshot;snapshot.valid=true;snapshot.hidden={L"STALE"};
        const bool ok=msiNativeForegroundSnapshot(snapshot);
        check("snapshot-result:"+output,ok==expected);
        check("one-query-without-apply",commands==std::vector<std::wstring>{L"--dev-list --app-list --cloak-state --inv-state --cancel"});
        check("no-partial-publication",expected?snapshot.valid:!snapshot.valid&&snapshot.hidden.empty());
        return snapshot;
    };
    auto snapshot=query("--dev-hide \"HID\\GAME\"\r\n--app-reg \"C:\\YMCC\\YeManCC.exe\"\r\n--cloak-on\r\n--inv-off\r\n",true);
    check("full-fields-preserved",snapshot.hidden==std::vector<std::wstring>{L"HID\\GAME"}&&snapshot.applications==std::vector<std::wstring>{L"C:\\YMCC\\YeManCC.exe"}&&snapshot.cloaking&&snapshot.inverseRaw=="--inv-off");
    snapshot=query(" \r\n--cloak-off\r\n--inv-on\r\n",true);
    check("empty-lists-cloak-and-inverse-preserved",snapshot.hidden.empty()&&snapshot.applications.empty()&&!snapshot.cloaking&&snapshot.inverseRaw=="--inv-on");
    query("--dev-hide \"HID\\ONE\"\n--dev-hide \"HID\\TWO\"\n--app-reg \"C:\\A B\\reader.exe\"\n--app-reg \"C:\\second.exe\"\n--cloak-on\n--inv-off\n",true);
    for(const std::string& invalid:{"","--cloak-on\n","--inv-off\n","--inv-off\n--cloak-on\n","--cloak-on\n--cloak-off\n--inv-off\n","--cloak-on\n--inv-off\n--inv-on\n","--cloak-on\n--inv-unknown\n","--cloak-on\n--inv-off\nERROR\n","--app-reg \"C:\\reader.exe\"\n--dev-hide \"HID\\GAME\"\n--cloak-on\n--inv-off\n","--dev-hide \"\"\n--cloak-on\n--inv-off\n","--dev-hide \"HID\\GAME\n--cloak-on\n--inv-off\n","--dev-hide \"HID\"trailing\"\n--cloak-on\n--inv-off\n","--cloak-on\n--app-reg \"C:\\reader.exe\"\n--inv-off\n","--cloak-on\n--inv-off\n--dev-unhide \"HID\\GAME\"\n"})query(invalid,false);
    std::string embedded="--cloak-on\n--inv-off";embedded.push_back('\0');embedded+="\n";query(embedded,false);
    runOk=false;query("--cloak-on\n--inv-off\n",false);runOk=true;
    runExit=5;query("--cloak-on\n--inv-off\n",false);runExit=0;
    commands.clear();seamActive=true;snapshot.valid=true;
    check("start-seam-no-cli",!msiNativeForegroundSnapshot(snapshot)&&commands.empty()&&!snapshot.valid);seamActive=false;
    const auto reset=[&]{g_msiNativeForegroundLease=MsiNativeForegroundLease{};g_msiNativeForegroundLease.root="USB\\VID_0DB0&PID_1901\\PINNED";g_msiNativeForegroundLease.locations={L"PINNED-LOCATION"};g_msiNativeForegroundLease.power=fixturePower;g_msiNativeForegroundCyclePending=false;waits.clear();closed.clear();launches=settles=receipts=launchFlags=0;launchOk=exitQueryOk=true;waitResult=WAIT_TIMEOUT;childExit=0;fixturePendingSuspend=false;duringHash={};};
    reset();
    check("running-helper-returns-pending",msiNativeForegroundCycleLocked(false)==1);
    check("no-lock-held-wait",waits==std::vector<DWORD>{0});
    check("same-helper-retained",g_msiNativeForegroundCyclePending&&g_msiNativeForegroundLease.cycleProcess==childHandle&&!g_msiNativeForegroundLease.cycleAccepted&&closed==std::vector<HANDLE>{threadHandle});
    check("hidden-process-launch",launches==1&&(launchFlags&CREATE_NO_WINDOW));
    const auto oldRoot=g_msiNativeForegroundLease.root;
    HidHidePhysicalIdentity game;game.deviceInstanceId="HID\\SECOND";
    check("pending-does-not-relaunch-or-relabel",msiNativeForegroundCycleLocked(true,&game)==1&&launches==1&&!g_msiNativeForegroundLease.cycleForRestore&&g_msiNativeForegroundLease.helperGameTarget.empty()&&g_msiNativeForegroundLease.root==oldRoot);
    check("pending-poll-is-nonblocking",msiNativeForegroundPollCycleLocked()==-1&&waits.back()==0&&g_msiNativeForegroundCyclePending);
    waitResult=WAIT_OBJECT_0;
    check("same-helper-completes-on-existing-maintenance",msiNativeForegroundPollCycleLocked()==0&&!g_msiNativeForegroundCyclePending&&g_msiNativeForegroundLease.cycleProcess==nullptr&&g_msiNativeForegroundLease.cycleAccepted&&settles==1&&receipts==1);
    check("child-handle-closed-once",std::count(closed.begin(),closed.end(),childHandle)==1);
    check("idle-maintenance-no-wait",msiNativeForegroundPollCycleLocked()==-1&&waits.size()==3);
    for(DWORD failure:{DWORD(6),DWORD(8)}){
        reset();msiNativeForegroundCycleLocked(true);waitResult=WAIT_OBJECT_0;childExit=failure;
        check("failed-helper-not-success",msiNativeForegroundPollCycleLocked()==2&&!g_msiNativeForegroundCyclePending&&!g_msiNativeForegroundLease.cycleAccepted&&g_msiNativeForegroundLease.helperExitCode==failure);
    }
    reset();waitResult=WAIT_OBJECT_0;
    check("already-complete-helper",msiNativeForegroundCycleLocked(false)==0&&!g_msiNativeForegroundCyclePending&&settles==1&&waits==std::vector<DWORD>{0});
    reset();launchOk=false;
    check("launch-failure-has-no-wait",msiNativeForegroundCycleLocked(false)==2&&waits.empty()&&!g_msiNativeForegroundCyclePending);
    reset();g_msiNativeForegroundLease.locations.clear();
    check("missing-location-no-launch",msiNativeForegroundCycleLocked(false)==2&&launches==0);
    reset();g_msiNativeForegroundLease.profile=ymcc::nativeForeground::Profile::Rog;
    check("rog-root-rejected",msiNativeForegroundCycleLocked(false)==2&&launches==0);
    reset();g_msiNativeForegroundLease.profile=ymcc::nativeForeground::Profile::Gpd;
    check("gpd-root-rejected",msiNativeForegroundCycleLocked(false)==2&&launches==0);
    reset();g_msiNativeForegroundLease.profile=ymcc::nativeForeground::Profile::Rog;
    check("game-interface-keeps-same-async-owner",msiNativeForegroundCycleLocked(false,&game)==1&&g_msiNativeForegroundLease.helperGameTarget==game.deviceInstanceId&&waits==std::vector<DWORD>{0});
    reset();fixturePendingSuspend=true;
    check("public-suspend-denies-fresh-helper",msiNativeForegroundCycleLocked(false)==MsiNativeForegroundLease::kPowerDeferred&&launches==0&&waits.empty());
    for(const bool pending:{false,true}){
        reset();
        g_msiNativeForegroundLease.helperGameTarget="old-exact-target";
        g_msiNativeForegroundLease.retiredNativeWriterAttempt="old-attempt";
        g_msiNativeForegroundLease.helperExitCode=6;
        g_msiNativeForegroundLease.helperDiagnostic={{"old",true}};
        duringHash=[&]{fixturePendingSuspend=pending;++g_sleepPowerDispatch.boundary;};
        check(pending?"pending-during-helper-preparation":"new-sleep-boundary-during-helper-preparation",
            msiNativeForegroundCycleLocked(true)==MsiNativeForegroundLease::kPowerDeferred&&launches==0&&waits.empty()&&
            g_msiNativeForegroundLease.helperGameTarget=="old-exact-target"&&g_msiNativeForegroundLease.helperExitCode==6&&
            g_msiNativeForegroundLease.helperDiagnostic.value("old",false)&&g_msiNativeForegroundLease.retiredNativeWriterAttempt=="old-attempt"&&
            !g_msiNativeForegroundLease.cycleIssued&&!g_msiNativeForegroundCyclePending);
        fixturePendingSuspend=false;duringHash={};waitResult=WAIT_OBJECT_0;
        check("same-owner-can-submit-one-inverse-after-ready",msiNativeForegroundCycleLocked(true)==0&&launches==1&&g_msiNativeForegroundLease.cycleForRestore);
    }
    reset();duringHash=[] {++fixturePower;};
    check("fresh-generation-cannot-submit-old-helper",msiNativeForegroundCycleLocked(true)==MsiNativeForegroundLease::kPowerDeferred&&launches==0);
    reset();duringHash=[] {++g_sharedPowerSuspendSerial;};
    check("same-generation-revocation-cannot-submit-old-helper",msiNativeForegroundCycleLocked(true)==MsiNativeForegroundLease::kPowerDeferred&&launches==0);
    check("unexercised-dependencies-remain-inert",unexpectedDependencyCalls==0);
    const json report={{"passed",failures.empty()},{"cases",cases},{"failures",failures},{"hardwareAccess",false},{"realProcessesLaunched",false}};
    if(argc>1){std::ofstream file(argv[1]);file<<report.dump(2)<<"\n";}
    std::cout<<report.dump()<<"\n";
    return failures.empty()?0:1;
}
#endif
