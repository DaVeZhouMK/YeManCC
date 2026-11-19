#include "sleep_static_wake_schedule.h"
static unsigned staticWakeSchedulePolicyTests() {
    using ymcc::staticwake::ScanDelay;unsigned checks=0;
    const auto expect=[&](bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);};
    ScanDelay d;
    expect(!d.poll(100,true,true).dispatch,"no event never schedules inventory");
    expect(d.arm(100,true)&&d.due==60100,"startup delays exactly one minute");
    expect(!d.arm(500,true)&&d.due==60100,"duplicate startup cannot move deadline");
    auto q=d.poll(60100-1,true,true);expect(!q.dispatch&&q.remaining==1,"early timer compares timestamp");
    q=d.poll(60100,true,true);expect(q.dispatch&&!d.pending,"one minute dispatch consumes pending event");
    expect(!d.poll(999999,true,true).dispatch,"no periodic rescan after consumption");
    expect(d.arm(100000,false,2),"confirmed wake schedules one scan");
    expect(!d.arm(101000,false,2)&&d.due==160000,"same wake notification storm remains one scan");
    expect(d.arm(102000,false,3)&&d.due==162000,"new wake replaces old pending deadline");
    expect(!d.poll(162000,true,false).dispatch&&!d.pending,"sleep before deadline cancels without inventory");
    expect(d.arm(200000,false,4),"new wake after canceled delay can schedule");
    expect(!d.poll(260000,false,true).dispatch&&!d.pending,"OFF cancels delayed inventory");
    expect(d.arm(300000,false,5),"later real wake remains usable after OFF");d.cancel();
    expect(!d.poll(900000,true,true).dispatch,"canceled timer cannot resurrect after long sleep");
    expect(d.arm(1000000,false,6),"resume control arms");
    expect(d.poll(1065000,true,true).dispatch,"late callback dispatches once based on absolute time");
    expect(!d.poll(1100000,true,true).dispatch,"late message storm cannot repeat scan");
    expect(!d.arm(UINT64_MAX-1000,false,7),"overflow cannot wrap to immediate inventory");
    // Actual production read-admission algorithm, with an inert clock/wait.
    // These waits never invoke a device API or sleep the test process.
    using ymcc::staticwake::CollectionReadPace;
    CollectionReadPace pace;std::uint64_t now=100;bool allowed=true;unsigned sleeps=0;
    const auto clock=[&](){return now;};const auto permit=[&](){return allowed;};
    const auto wait=[&](std::uint64_t ms){expect(ms>0&&ms<=16,"collection waits are bounded to sixteen ms");now+=ms;++sleeps;};
    expect(pace.before(clock,permit,wait)&&sleeps==0&&pace.admitted==1,"first collection read has no startup wait");
    expect(pace.before(clock,permit,wait)&&now==116&&sleeps==1&&pace.admitted==2,"subsequent collection read is spaced");
    now=1000;expect(pace.before(clock,permit,wait)&&pace.next==1016,"slow API or suspension admits once without banked credits");
    expect(pace.before(clock,permit,wait)&&now==1016,"late work cannot catch up with a burst");
    allowed=false;const auto admitted=pace.admitted;
    expect(!pace.before(clock,permit,wait)&&pace.admitted==admitted,"OFF or stale generation cancels before a query");
    allowed=true;
    expect(!pace.before(clock,permit,[&](std::uint64_t ms){now+=ms;allowed=false;})&&pace.admitted==admitted,"cancel during wait cannot admit a query");
    allowed=true;pace.reset();now=2000;
    expect(pace.before(clock,permit,wait),"new collection starts with one unbanked admission");
    const auto deadline=now+2;
    expect(!pace.before(clock,[&](){return now<deadline;},wait),"pacing consumes the original absolute deadline");
    pace.reset();now=0;allowed=true;sleeps=0;
    for(unsigned item=0;item<65;++item)expect(pace.before(clock,permit,wait),"paced collection retains every read unit");
    expect(pace.admitted==65&&now==1024&&sleeps==64,"complete inventory uses one wait per gap without dropping or batching reads");
    return checks;
}

#include <functional>
#include <atomic>
#include <deque>
#include <optional>
#include "sleep_power_revocation.h"
namespace static_wake_schedule_runtime {
using json=nlohmann::json;
enum class PowerLifecycle{Ready,Suspended};
static HWND g_hwnd=reinterpret_cast<HWND>(1);
static std::atomic<bool> g_exitRequested{false};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static constexpr UINT WM_SG_STATIC_SCAN=WM_APP+826;
static constexpr UINT_PTR SG_STATIC_SCAN_TIMER_ID=0xA213;
static ULONGLONG tick=1000,testScheduleGeneration=1,timerDue=0;
static bool gate=true,queueAvailable=true;
static unsigned scans=0,scanWrites=0,restores=0,restoreWrites=0;
static std::deque<std::function<void()>> jobs;
struct Message{WPARAM cause;LPARAM revision;};
static std::deque<Message> messages;
static std::function<void()> duringScan,duringRestore,duringPost,duringGenerationRead,duringClockRead;
static std::vector<json> receipts;
using ymcc::sleeppower::SharedPowerV1;
static std::uint64_t notificationSequence=0,publicationRevision=1;
static bool pendingNotification=false,powerKnown=true;
static ymcc::sleeppower::RevocationMirrors fixtureRevocations;
#if defined(YMCC_STATIC_WAKE_LEGACY_AUTHORITY_CONTROL)
static ULONGLONG currentPowerGeneration(){const auto callback=duringGenerationRead;duringGenerationRead={};if(callback)callback();return testScheduleGeneration;}
static bool hardwareWriteAllowed(){return gate;}
#endif
static int __cdecl staticWakePowerRead(SharedPowerV1* power,std::uint32_t bytes) {
    const auto callback=duringGenerationRead;duringGenerationRead={};if(callback)callback();
    if(bytes!=sizeof(*power)||g_exitRequested||!powerKnown)return 0;
    *power={};power->generation=testScheduleGeneration;
    power->publicationRevision=publicationRevision;power->observedNotificationSequence=notificationSequence;
    power->phase=g_powerLifecycle==PowerLifecycle::Ready?0u:2u;
    power->flags=ymcc::sleeppower::kKnown|(gate?ymcc::sleeppower::kHardwareAllowed:0u)|
        (pendingNotification?ymcc::sleeppower::kNotificationPending:0u);
    return 1;
}
[[maybe_unused]] static bool captureSharedPowerLifecycle(SharedPowerV1& power) noexcept {
    return ymcc::sleeppower::capturePower(&staticWakePowerRead,power);
}
[[maybe_unused]] static bool sharedPowerLifecycleCurrent(const SharedPowerV1& power) noexcept {
    return ymcc::sleeppower::powerCurrent(&staticWakePowerRead,power);
}
[[maybe_unused]] static auto observeSharedPowerRevocation(const SharedPowerV1& power) noexcept {
    return fixtureRevocations.observe(&staticWakePowerRead,power);
}
static ULONGLONG clockNow(){const auto callback=duringClockRead;duringClockRead={};if(callback)callback();return tick;}
static UINT_PTR timerSet(HWND,UINT_PTR id,UINT delay,TIMERPROC){timerDue=tick+delay;return id;}
static BOOL timerKill(HWND,UINT_PTR){timerDue=0;return TRUE;}
static BOOL post(HWND,UINT,WPARAM cause,LPARAM revision){messages.push_back({cause,revision});const auto callback=duringPost;duringPost={};if(callback)callback();return TRUE;}

#if defined(YMCC_STATIC_WAKE_ACTUAL_POOL)
static std::mutex g_poolMx;
#if defined(YMCC_STATIC_WAKE_REAL_POOL)
#include "static_wake_pool_harness.inc"
#else
static std::condition_variable g_poolCv;
static auto& g_poolQ=jobs;
static constexpr size_t IPC_POOL_QUEUE_LIMIT=32;
static unsigned schedulePoolStarts=0;
static void poolStartLocked(){++schedulePoolStarts;}
#define g_poolStopping (!queueAvailable)
#include "production_static_schedule_pool.inc"
#undef g_poolStopping
#endif
#else
static bool poolSubmit(std::function<void()> job){if(!queueAvailable)return false;jobs.push_back(std::move(job));return true;}
#endif

static bool fixtureScheduleLogFailure=false,fixtureScheduleScanFailure=false;
static void sgRecordFact(const char* name,const json& data=json::object()){if(fixtureScheduleLogFailure)throw std::bad_alloc();receipts.push_back({{"event",name},{"details",data}});}
static unsigned statusHints=0;
static bool fixtureWorkerRunning=false,workerTouchedWebView=false;
static void ipc_emit(const char* event,const json&){
 if(std::string_view(event)=="sleep.staticWake.updated"){
  ++statusHints;if(fixtureWorkerRunning)workerTouchedWebView=true;
 }
}
static json swScan(std::function<bool()> canceled){
 ++scans;if(fixtureScheduleScanFailure)throw std::runtime_error("fixture scan failure");if(canceled())return {{"ok",false},{"status",{{"lastError","canceled"}}}};
 ++scanWrites;if(duringScan)duringScan();
 if(!canceled())++scanWrites;
 return {{"ok",!canceled()},{"status",{{"lastError",canceled()?"canceled":""}}}};
}
static json swRestore(std::function<bool()> canceled={}){
 ++restores;if(canceled&&canceled())return {{"ok",false},{"status",{{"lastError","canceled"}}}};
 ++restoreWrites;if(duringRestore)duringRestore();
 if(!canceled||!canceled())++restoreWrites;
 return {{"ok",!canceled||!canceled()},{"status",{{"lastError",canceled&&canceled()?"canceled":""}}}};
}
[[maybe_unused]] static json swGetStatus(){return {{"lastError",""}};}
#define GetTickCount64 clockNow
#define SetTimer timerSet
#define KillTimer timerKill
#define PostMessageW post
#include "sleep_static_wake_schedule_native.inc"
#undef GetTickCount64
#undef SetTimer
#undef KillTimer
#undef PostMessageW
static json currentManualRequest(){return {{"expectedPowerGeneration",testScheduleGeneration},{"expectedPowerNotificationSequence",notificationSequence}};}
static json manualWake(bool restore,const json& request){
#ifdef YMCC_STATIC_WAKE_PRE_IPC_AUTHORITY_CONTROL
    (void)request;
    return restore?sgStaticRestoreNow():sgStaticScanNow();
#else
    return sgStaticWakeNow(restore,request);
#endif
}
static json manualReview(bool restore){return manualWake(restore,currentManualRequest());}
static void pump(){while(!messages.empty()){auto m=messages.front();messages.pop_front();sgStaticScanMessage(m.cause,m.revision);}}
static void work(){
 struct WorkerScope {WorkerScope(){fixtureWorkerRunning=true;}~WorkerScope(){fixtureWorkerRunning=false;}} scope;
 while(!jobs.empty()){auto f=std::move(jobs.front());jobs.pop_front();f();}
}
static void reset(){g_sgStaticScanDelay={};g_sgStaticScanPreference=-1;g_sgStaticScanRevision=1;g_sgStaticScanArmedRevision=0;g_sgStaticScanRunning=false;
 g_sgStaticScanQueuedArm={};
 notificationSequence=0;publicationRevision=1;pendingNotification=false;powerKnown=true;statusHints=0;workerTouchedWebView=false;
 tick=1000;testScheduleGeneration=1;timerDue=0;gate=true;queueAvailable=true;scans=scanWrites=restores=restoreWrites=0;jobs.clear();messages.clear();receipts.clear();duringScan={};duringRestore={};duringPost={};duringGenerationRead={};duringClockRead={};g_exitRequested=false;g_powerLifecycle=PowerLifecycle::Ready;}
static unsigned run(){unsigned checks=0;auto expect=[&](bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);};
 expect(sgStaticScanEnabled(json::object()),"new installs preserve enabled automatic scanning default");
 expect(!sgStaticScanEnabled({{"sleepPowerPlanOptimizationEnabled",false}}),"old saved OFF migrates without activating scans");
 expect(sgStaticScanEnabled({{"sleepPowerPlanOptimizationEnabled",true}}),"old saved ON migrates to automatic scanning");
 expect(sgStaticScanEnabled({{"sleepPowerPlanOptimizationEnabled",false},{"staticWakeAutoScanEnabled",true}}),"independent automatic ON works with power repair OFF");
 expect(!sgStaticScanEnabled({{"sleepPowerPlanOptimizationEnabled",true},{"staticWakeAutoScanEnabled",false}}),"independent automatic OFF overrides power repair ON");
 reset();sgStaticScanPreference(sgStaticScanEnabled({{"staticWakeAutoScanEnabled",false}}),true);pump();
 expect(timerDue==0&&scans==0,"saved automatic OFF schedules no startup scan");
 sgStaticScanPreference(sgStaticScanEnabled({{"staticWakeAutoScanEnabled",true}}));pump();
 expect(timerDue==61000&&scans==0,"explicit automatic ON schedules one minute with no immediate inventory");
 tick=61000;sgStaticScanTimer();work();expect(scans==1,"explicit automatic ON actually reaches native inventory once");
 sgStaticScanPreference(true);pump();tick=121000;sgStaticScanTimer();work();
 expect(scans==1&&timerDue==0,"unrelated saved settings cannot reschedule an already enabled scan");
 reset();sgStaticScanPreference(true,true);pump();expect(timerDue==61000&&scans==0,"actual startup queues a minute without immediate inventory");
 tick=60999;sgStaticScanTimer();expect(timerDue==61000&&jobs.empty(),"actual early timer rearms remaining absolute millisecond");
 tick=61000;sgStaticScanTimer();expect(jobs.size()==1&&scans==0,"actual delay dispatch uses existing worker queue");work();
 expect(scans==1&&scanWrites==2&&!g_sgStaticScanRunning,"actual worker performs one allowed inventory");
 expect(statusHints==0&&!workerTouchedWebView&&messages.size()==1,"background completion posts a window message without touching WebView");
 pump();expect(statusHints==1&&!workerTouchedWebView&&scans==1,"window broadcasts one hint without another scan");
 tick=999999;sgStaticScanTimer();work();expect(scans==1&&timerDue==0,"actual auto scan has no periodic rearm");
 reset();sgStaticScanPreference(true,true);pump();tick=61000;sgStaticScanTimer();work();
 g_exitRequested=true;pump();expect(statusHints==0&&scans==1&&messages.empty(),"exit retires a pending status hint without replay");
 reset();sgStaticScanPreference(true,true);pump();gate=false;g_powerLifecycle=PowerLifecycle::Suspended;sgStaticScanCancel("sleep");pump();
 tick=61000;sgStaticScanTimer();work();expect(scans==0&&timerDue==0,"actual sleep cancels pending auto scan");
 gate=true;g_powerLifecycle=PowerLifecycle::Ready;++testScheduleGeneration;sgStaticScanSchedule(false);pump();
 expect(timerDue==121000,"actual confirmed wake starts a fresh minute");const auto due=timerDue;tick+=2000;sgStaticScanSchedule(false);pump();
 expect(timerDue==due,"actual repeated same wake cannot postpone scan");
 reset();sgStaticScanPreference(true,true);pump();sgStaticScanPreference(false);tick=61000;sgStaticScanTimer();work();
 expect(scans==0,"actual OFF veto acts before queued cancel message");pump();expect(!g_sgStaticScanDelay.pending,"actual OFF closes pending delay");
 sgStaticScanPreference(true);pump();expect(timerDue==121000,"actual re-enable in same generation still schedules");
 reset();sgStaticScanPreference(true,true);pump();sgStaticScanCancel("kernel-power-506-guard-off");tick=61000;sgStaticScanTimer();work();
 expect(scans==0&&jobs.empty(),"actual kernel entry veto prevents scan before cancel message even with old Ready bookkeeping");
 reset();sgStaticScanPreference(true,true);pump();tick=61000;sgStaticScanTimer();sgStaticScanPreference(false);work();
 expect(scans==0&&!g_sgStaticScanRunning,"actual OFF after queue before execution forbids inventory");
 reset();sgStaticScanPreference(true,true);pump();tick=61000;sgStaticScanTimer();
 duringScan=[](){gate=false;g_powerLifecycle=PowerLifecycle::Suspended;sgStaticScanCancel("sleep-during-scan");};work();
 expect(scans==1&&scanWrites==1,"actual cancellation predicate stops further scan writes during sleep");
 reset();sgStaticScanPreference(true,true);pump();tick=61000;sgStaticScanTimer();g_exitRequested=true;work();
 expect(scans==0&&!g_sgStaticScanRunning,"actual exit invalidates already queued inventory");
 reset();sgStaticScanPreference(true,true);pump();queueAvailable=false;tick=61000;sgStaticScanTimer();
 expect(scans==0&&!g_sgStaticScanRunning&&timerDue==0,"actual unavailable worker does not retain ownership or retry");
 reset();sgStaticScanPreference(true,true);pump();sgStaticScanCancel("old-sleep");const auto oldRevision=g_sgStaticScanRevision.load();
 ++testScheduleGeneration;sgStaticScanSchedule(false);const auto newWake=messages.back();sgStaticScanMessage(newWake.cause,newWake.revision);const auto newerDue=timerDue;
 sgStaticScanMessage(0,static_cast<LPARAM>(oldRevision));expect(timerDue==newerDue&&g_sgStaticScanDelay.pending,"actual stale cancel cannot erase newer wake delay");
 reset();sgStaticScanPreference(true,true);pump();auto result=manualReview(false);pump();
 expect(result["ok"]==true&&scans==1&&!g_sgStaticScanDelay.pending,"actual manual scan consumes the pending event");
 tick=61000;sgStaticScanTimer();work();expect(scans==1,"actual manual scan is not followed by redundant delayed scan");
 expect(!receipts.empty(),"actual scheduling preserves ordinary bounded receipts");
 reset();sgStaticScanPreference(true,true);sgStaticScanCancel("sleep-before-start-message");pump();
 expect(!g_sgStaticScanDelay.pending&&timerDue==0,"old queued startup message cannot overtake a newer sleep cancellation");
 tick=61000;sgStaticScanTimer();work();expect(scans==0,"old queued start cannot trigger inventory while Ready bookkeeping is stale");
 reset();sgStaticScanPreference(true,true);pump();sgStaticScanSchedule(false);sgStaticScanCancel("sleep-after-wake-post");pump();
 expect(!g_sgStaticScanDelay.pending,"old queued wake message cannot rebuild a canceled delay");
 reset();sgStaticScanPreference(true,true);++notificationSequence;pump();
 const auto oldStartArmed=g_sgStaticScanDelay.pending;
 tick=61000;sgStaticScanTimer();work();
 expect(!oldStartArmed&&scans==0&&scanWrites==0,"queued startup cannot acquire a newer public notification");
 reset();sgStaticScanPreference(true,true);++publicationRevision;pump();
 expect(timerDue==61000,"publication-only change preserves the original queued admission");
 reset();sgStaticScanPreference(true,true);pump();++testScheduleGeneration;sgStaticScanSchedule(false);
 const auto retiredWake=messages.front();messages.pop_front();++testScheduleGeneration;sgStaticScanSchedule(false);
 const auto successorWake=messages.front();
 expect(retiredWake.revision!=successorWake.revision,"successor queued wake has its own message identity");
 sgStaticScanMessage(retiredWake.cause,retiredWake.revision);
 expect(g_sgStaticScanArmedPower.generation!=testScheduleGeneration,"retired message cannot claim the successor wake");
 pump();expect(g_sgStaticScanArmedPower.generation==testScheduleGeneration&&g_sgStaticScanDelay.pending,"fresh wake carries its own public admission");
 reset();sgStaticScanPreference(true);++testScheduleGeneration;pump();
 expect(!g_sgStaticScanDelay.pending&&timerDue==0,"queued ON cannot acquire another generation");
 reset();sgStaticScanPreference(false);auto restored=manualReview(true);
 expect(restored["ok"]==true&&restores==1&&restoreWrites==2,"explicit restore stays allowed with preference OFF");
 reset();sgStaticScanPreference(true,true);pump();restored=manualReview(true);pump();
 expect(restored["ok"]==true&&!g_sgStaticScanDelay.pending&&timerDue==0,"restore consumes pending scan without scheduling another inventory");
 reset();g_exitRequested=true;restored=manualReview(true);
 expect(restored["ok"]==false&&restoreWrites==0,"exit before restore denies all writes while hardware Ready may still be stale");
 reset();duringRestore=[](){g_exitRequested=true;};restored=manualReview(true);
 expect(restored["ok"]==false&&restoreWrites==1,"exit during restore stops subsequent writes without clearing the outstanding work");
 reset();duringRestore=[](){g_powerLifecycle=PowerLifecycle::Suspended;};restored=manualReview(true);
 expect(restored["ok"]==false&&restoreWrites==1,"sleep during restore cancels even before shared write gate refresh");
 reset();duringRestore=[](){++testScheduleGeneration;};restored=manualReview(true);
 expect(restored["ok"]==false&&restoreWrites==1,"restore cannot cross power generations while Ready is unchanged");
 reset();duringRestore=[](){sgStaticScanCancel("kernel-power-506");};restored=manualReview(true);
 expect(restored["ok"]==false&&restoreWrites==1,"original sleep event revision cancels restore before the GUI processes its message");
 reset();duringRestore=[](){gate=false;};restored=manualReview(true);
 expect(restored["ok"]==false&&restoreWrites==1,"closing hardware write ownership stops further restore writes");
 reset();duringRestore=[](){sgStaticScanCancel("newer-manual-action");};restored=manualReview(true);
 expect(restored["ok"]==false&&restoreWrites==1,"newer manual operation invalidates older restore continuation");
 duringRestore={};restored=manualReview(true);
 expect(restored["ok"]==true&&restoreWrites==3,"later fresh explicit restore can finish previous pending recovery");
 reset();sgStaticScanPreference(false);duringRestore=[](){sgStaticScanPreference(false);};restored=manualReview(true);
 expect(restored["ok"]==true&&restoreWrites==2,"remembered OFF does not veto its own restore");
 reset();sgStaticScanPreference(false);duringPost=[](){g_sgStaticScanRevision.fetch_add(1);};restored=manualReview(true);
 expect(restored["ok"]==false&&restoreWrites==0,"restore must retain its own cancellation ticket when a newer revision lands before PostMessage returns");
 reset();sgStaticScanPreference(true);duringPost=[](){g_sgStaticScanRevision.fetch_add(1);};result=manualReview(false);
 expect(result["ok"]==false&&scanWrites==0,"manual scan cannot adopt a later cancellation revision after posting its own cancel");
 reset();sgStaticScanPreference(true,true);duringGenerationRead=[](){sgStaticScanCancel("sleep-during-delay-arm");};pump();
 expect(!g_sgStaticScanDelay.pending&&timerDue==0,"old schedule message cannot consume a newer cancellation between revision check and arm");
 reset();sgStaticScanPreference(true,true);pump();tick=61000;
 duringClockRead=[](){sgStaticScanCancel("sleep-during-timer-dispatch");};sgStaticScanTimer();work();
 expect(scans==0&&scanWrites==0,"expired timer must keep its armed ticket when cancellation arrives during the clock read");
 reset();sgStaticScanPreference(true,true);pump();++notificationSequence;tick=61000;sgStaticScanTimer();work();
 expect(scans==0&&timerDue==0,"same-generation new public notification retires an armed delay");
 reset();sgStaticScanPreference(true,true);pump();tick=61000;sgStaticScanTimer();++notificationSequence;work();
 expect(scans==0&&!g_sgStaticScanRunning,"queued worker cannot inherit a newer public notification");
 reset();sgStaticScanPreference(true,true);pump();tick=61000;sgStaticScanTimer();pendingNotification=true;work();
 expect(scans==0&&!g_sgStaticScanRunning,"pending public notification denies work while legacy Ready stays true");
 reset();sgStaticScanPreference(true,true);pump();tick=61000;sgStaticScanTimer();++publicationRevision;work();
 expect(scans==1&&scanWrites==2,"publication-only update permits same admitted operation");
 reset();sgStaticScanPreference(false);duringRestore=[](){++notificationSequence;};restored=manualReview(true);
 expect(restored["ok"]==false&&restoreWrites==1,"manual restore stops at a changed public notification even with the switch OFF");
 reset();sgStaticScanPreference(true);duringScan=[](){fixtureRevocations.revoke();};result=manualReview(false);
 expect(result["ok"]==false&&scanWrites==1,"sticky revocation vetoes the second write even with unchanged visible Ready fields");
 reset();sgStaticScanPreference(true,true);powerKnown=false;pump();
 expect(timerDue==0&&scans==0,"unknown public power cannot arm a scan");
 reset();const auto oldManual=currentManualRequest();++notificationSequence;
 sgStaticScanPreference(true,true);pump();const auto armed=g_sgStaticScanRevision.load();
 restored=manualWake(true,oldManual);pump();
 if(restored["ok"]!=false||restores||restoreWrites)throw std::runtime_error("queued manual restore cannot acquire a newer public notification: restoreWrites="+std::to_string(restoreWrites));
 ++checks;
 expect(g_sgStaticScanDelay.pending&&g_sgStaticScanRevision.load()==armed,"retired manual restore cannot supersede a newer pending scan");
 restored=manualReview(true);expect(restored["ok"]==true&&restoreWrites==2,"fresh explicit restore can still complete");
 reset();sgStaticScanPreference(true);const auto beforeConcurrentArm=currentManualRequest();
 duringGenerationRead=[](){sgStaticScanSchedule(false);pump();};
 restored=manualWake(true,beforeConcurrentArm);pump();
 expect(restored["ok"]==false&&restores==0&&restoreWrites==0&&g_sgStaticScanDelay.pending&&timerDue==tick+60000,
        "manual admission leaves a scan armed during the public read in place");
 reset();sgStaticScanPreference(true);const auto oldScan=currentManualRequest();++testScheduleGeneration;
 result=manualWake(false,oldScan);expect(result["ok"]==false&&scans==0&&scanWrites==0,"queued manual scan cannot acquire a new sleep generation");
 reset();const auto beforePending=currentManualRequest();pendingNotification=true;
 restored=manualWake(true,beforePending);expect(restored["ok"]==false&&restores==0,"pending public notification denies manual dispatch");
 reset();const auto beforePublication=currentManualRequest();++publicationRevision;
 restored=manualWake(true,beforePublication);expect(restored["ok"]==true&&restoreWrites==2,"metadata-only publication does not cancel a manual request");
 reset();for(const auto& bad:std::vector<json>{json::object(),{{"expectedPowerGeneration",1}},{{"expectedPowerGeneration",0},{"expectedPowerNotificationSequence",0}},
  {{"expectedPowerGeneration",1},{"expectedPowerNotificationSequence",-1}},{{"expectedPowerGeneration",1},{"expectedPowerNotificationSequence",0.0}},
  {{"expectedPowerGeneration",1},{"expectedPowerNotificationSequence",9007199254740992ull}}}) {
  restored=manualWake(true,bad);expect(restored["ok"]==false&&restores==0&&restoreWrites==0,"unscoped or malformed request cannot run") ;
 }
 reset();return checks;
}
}
static unsigned staticWakeScheduleTests(){return staticWakeSchedulePolicyTests()+static_wake_schedule_runtime::run();}

#if defined(YMCC_STATIC_WAKE_ACTUAL_POOL)
namespace static_wake_schedule_runtime {
static int admissionCase(const std::string& selected){unsigned checks=0;
 const auto expect=[&](bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);};
 try{
#if defined(YMCC_STATIC_WAKE_REAL_POOL)
#if defined(YMCC_STATIC_WAKE_REAL_DIAGNOSTIC)
  if(selected.rfind("schedule-pool-exit-diag-",0)==0)return realPoolDiagnosticCase(selected);
  if(selected.rfind("schedule-pool-exit-diag-",0)!=0)throw std::runtime_error("diagnostic pool binary refuses other component cases");
#elif defined(YMCC_STATIC_WAKE_REAL_CRT)
  if(selected.rfind("schedule-pool-exit-crt-",0)==0)return realPoolCrtCase(selected);
  if(selected.rfind("schedule-pool-exit-crt-",0)!=0)throw std::runtime_error("CRT pool binary refuses other component cases");
#elif defined(YMCC_STATIC_WAKE_REAL_EXIT_JOIN)
  if(selected.rfind("schedule-pool-exit-",0)==0)return realPoolExitCase(selected);
  if(selected.rfind("schedule-pool-exit-",0)!=0)throw std::runtime_error("exit join binary refuses other component cases");
#else
  if(selected.rfind("schedule-pool-",0)==0)return realPoolCase(selected);
  if(selected.rfind("schedule-pool-",0)!=0)throw std::runtime_error("cold pool binary refuses other component cases");
#endif
#endif
  if(selected=="schedule-original"){
   const auto total=::staticWakeScheduleTests();
   std::cout<<json{{"scenario",selected},{"assertions",total},{"originalPolicyAndRuntime",true},{"actualPoolSubmit",true},{"realSleep",false},{"hardwareCalls",0}}.dump()<<'\n';return 0;
  }
  if(!(selected=="schedule-admission-normal"||selected=="schedule-admission-queue-allocation"||selected=="schedule-admission-queue-allocation-log-failure"||selected=="schedule-admission-unavailable"||selected=="schedule-admission-worker-log-failure"||selected=="schedule-admission-worker-scan-failure"||selected=="schedule-admission-stale-before-worker"||selected=="schedule-admission-off-reenable"))throw std::runtime_error("unknown exact static schedule scenario");
  reset();scheduleAllocationHits=0;schedulePoolStarts=0;fixtureScheduleLogFailure=false;fixtureScheduleScanFailure=false;
  sgStaticScanPreference(true,true);pump();expect(timerDue==61000,"original one-minute startup delay remains exact");
  const bool allocation=selected=="schedule-admission-queue-allocation"||selected=="schedule-admission-queue-allocation-log-failure";
  if(allocation){bool prepared=false;for(unsigned i=0;i<IPC_POOL_QUEUE_LIMIT;++i){scheduleFailNextAllocation=true;try{jobs.emplace_back([]{});scheduleFailNextAllocation=false;}catch(const std::bad_alloc&){prepared=true;break;}}
   expect(prepared&&jobs.size()<IPC_POOL_QUEUE_LIMIT,"real original deque reaches next allocation before admission limit");scheduleAllocationHits=0;
  }
  const auto queuedBefore=jobs.size();queueAvailable=selected!="schedule-admission-unavailable";
  fixtureScheduleLogFailure=selected=="schedule-admission-queue-allocation-log-failure";
  tick=61000;if(allocation)scheduleFailNextAllocation=true;
  bool escaped=false;try{sgStaticScanTimer();}catch(...){escaped=true;}scheduleFailNextAllocation=false;
  if(allocation||!queueAvailable){
   expect(!g_sgStaticScanRunning,"queue refusal must release static scan running ownership");
   expect(!escaped,"queue admission and failure diagnostics cannot escape original timer");
   expect(jobs.size()==queuedBefore&&!g_sgStaticScanDelay.pending&&timerDue==0,"failed admission owns no scan callback or pending timer");
   expect(scans==0&&scanWrites==0,"failed admission performs no inventory or writes");
   if(allocation)expect(scheduleAllocationHits==1,"next actual original queue allocation fails exactly once");
  }else{
   expect(!escaped&&g_sgStaticScanRunning&&jobs.size()==1,"original successful admission owns exactly one queued callback");
   if(selected=="schedule-admission-stale-before-worker")sgStaticScanCancel("fixture-new-sleep-before-worker");
   if(selected=="schedule-admission-off-reenable"){sgStaticScanPreference(false);pump();expect(!g_sgStaticScanDelay.pending,"OFF cancels the pending task while old callback remains owned");sgStaticScanPreference(true);pump();expect(g_sgStaticScanDelay.pending&&timerDue==121000,"explicit ON creates one fresh delayed demand without adopting old callback");}
   fixtureScheduleLogFailure=selected=="schedule-admission-worker-log-failure";
   fixtureScheduleScanFailure=selected=="schedule-admission-worker-scan-failure";
   bool workerEscaped=false;try{work();}catch(...){workerEscaped=true;}
   const bool workerFault=selected=="schedule-admission-worker-log-failure"||selected=="schedule-admission-worker-scan-failure";
   expect(workerEscaped==workerFault,"original worker exceptions retain their actual pool-observable result");
   expect(!g_sgStaticScanRunning&&jobs.empty(),"original worker Release returns ownership on normal cancellation and exceptions");
   if(selected=="schedule-admission-stale-before-worker"||selected=="schedule-admission-off-reenable")expect(scans==0&&scanWrites==0,"new sleep cancels already queued scan before inventory");
  }
  fixtureScheduleLogFailure=false;fixtureScheduleScanFailure=false;queueAvailable=true;jobs.clear();pump();
  const auto scansBefore=scans;tick=70000;++testScheduleGeneration;sgStaticScanSchedule(false);pump();
  expect(timerDue==130000&&g_sgStaticScanDelay.pending,"later confirmed wake keeps original one-minute deadline");
  tick=129999;sgStaticScanTimer();expect(jobs.empty()&&timerDue==130000,"early wake timer cannot spend the new work");
  tick=130000;sgStaticScanTimer();expect(jobs.size()==1&&g_sgStaticScanRunning,"later wake can enter after prior failed or cancelled task");
  work();expect(scans==scansBefore+1&&!g_sgStaticScanRunning&&jobs.empty(),"later original wake completes once without self lock");
  tick=999999;sgStaticScanTimer();work();expect(scans==scansBefore+1&&timerDue==0&&!g_sgStaticScanDelay.pending,"no repeating retry or replay after completion");
  std::cout<<json{{"scenario",selected},{"assertions",checks},{"actualPoolSubmit",true},{"allocationHits",scheduleAllocationHits},{"running",g_sgStaticScanRunning.load()},{"pending",g_sgStaticScanDelay.pending},{"jobs",jobs.size()},{"scans",scans},{"scanWrites",scanWrites},{"realSleep",false},{"hardwareCalls",0}}.dump()<<'\n';return 0;
 }catch(const std::exception& e){scheduleFailNextAllocation=false;std::cout<<json{{"scenario",selected},{"assertions",checks},{"failed",e.what()},{"allocationHits",scheduleAllocationHits},{"running",g_sgStaticScanRunning.load()},{"pending",g_sgStaticScanDelay.pending},{"jobs",jobs.size()},{"scans",scans},{"scanWrites",scanWrites},{"realSleep",false},{"hardwareCalls",0}}.dump()<<'\n';return 1;}
}
}
#endif

#if defined(YMCC_STATIC_WAKE_REAL_POOL)
#define YMCC_STATIC_POOL_CASES_ONLY 1
#include "static_wake_pool_harness.inc"
#undef YMCC_STATIC_POOL_CASES_ONLY
#endif

#if defined(YMCC_STATIC_WAKE_REAL_CRT)
#define YMCC_STATIC_POOL_CRT_CASES_ONLY 1
#include "static_wake_pool_crt_harness.inc"
#undef YMCC_STATIC_POOL_CRT_CASES_ONLY
#endif
