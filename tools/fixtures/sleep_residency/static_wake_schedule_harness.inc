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
    return checks;
}

#include <functional>
#include <atomic>
#include <deque>
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
static ULONGLONG currentPowerGeneration(){const auto callback=duringGenerationRead;duringGenerationRead={};if(callback)callback();return testScheduleGeneration;}
static bool hardwareWriteAllowed(){return gate;}
static ULONGLONG clockNow(){const auto callback=duringClockRead;duringClockRead={};if(callback)callback();return tick;}
static UINT_PTR timerSet(HWND,UINT_PTR id,UINT delay,TIMERPROC){timerDue=tick+delay;return id;}
static BOOL timerKill(HWND,UINT_PTR){timerDue=0;return TRUE;}
static BOOL post(HWND,UINT,WPARAM cause,LPARAM revision){messages.push_back({cause,revision});const auto callback=duringPost;duringPost={};if(callback)callback();return TRUE;}
static bool poolSubmit(std::function<void()> job){if(!queueAvailable)return false;jobs.push_back(std::move(job));return true;}
static void sgRecordFact(const char* name,const json& data=json::object()){receipts.push_back({{"event",name},{"details",data}});}
static void ipc_emit(const char*,const json&){}
static json swScan(std::function<bool()> canceled){
 ++scans;if(canceled())return {{"ok",false},{"status",{{"lastError","canceled"}}}};
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
#define GetTickCount64 clockNow
#define SetTimer timerSet
#define KillTimer timerKill
#define PostMessageW post
#include "sleep_static_wake_schedule_native.inc"
#undef GetTickCount64
#undef SetTimer
#undef KillTimer
#undef PostMessageW
static void pump(){while(!messages.empty()){auto m=messages.front();messages.pop_front();sgStaticScanMessage(m.cause,m.revision);}}
static void work(){while(!jobs.empty()){auto f=std::move(jobs.front());jobs.pop_front();f();}}
static void reset(){g_sgStaticScanDelay={};g_sgStaticScanPreference=-1;g_sgStaticScanRevision=1;g_sgStaticScanArmedRevision=0;g_sgStaticScanRunning=false;
 tick=1000;testScheduleGeneration=1;timerDue=0;gate=true;queueAvailable=true;scans=scanWrites=restores=restoreWrites=0;jobs.clear();messages.clear();receipts.clear();duringScan={};duringRestore={};duringPost={};duringGenerationRead={};duringClockRead={};g_exitRequested=false;g_powerLifecycle=PowerLifecycle::Ready;}
static unsigned run(){unsigned checks=0;auto expect=[&](bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);};
 reset();sgStaticScanPreference(true,true);pump();expect(timerDue==61000&&scans==0,"actual startup queues a minute without immediate inventory");
 tick=60999;sgStaticScanTimer();expect(timerDue==61000&&jobs.empty(),"actual early timer rearms remaining absolute millisecond");
 tick=61000;sgStaticScanTimer();expect(jobs.size()==1&&scans==0,"actual delay dispatch uses existing worker queue");work();
 expect(scans==1&&scanWrites==2&&!g_sgStaticScanRunning,"actual worker performs one allowed inventory");
 tick=999999;sgStaticScanTimer();work();expect(scans==1&&timerDue==0,"actual auto scan has no periodic rearm");
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
 ++testScheduleGeneration;sgStaticScanSchedule(false);sgStaticScanMessage(2,static_cast<LPARAM>(g_sgStaticScanRevision.load()));const auto newerDue=timerDue;
 sgStaticScanMessage(0,static_cast<LPARAM>(oldRevision));expect(timerDue==newerDue&&g_sgStaticScanDelay.pending,"actual stale cancel cannot erase newer wake delay");
 reset();sgStaticScanPreference(true,true);pump();auto result=sgStaticScanNow();pump();
 expect(result["ok"]==true&&scans==1&&!g_sgStaticScanDelay.pending,"actual manual scan consumes the pending event");
 tick=61000;sgStaticScanTimer();work();expect(scans==1,"actual manual scan is not followed by redundant delayed scan");
 expect(!receipts.empty(),"actual scheduling preserves ordinary bounded receipts");
 reset();sgStaticScanPreference(true,true);sgStaticScanCancel("sleep-before-start-message");pump();
 expect(!g_sgStaticScanDelay.pending&&timerDue==0,"old queued startup message cannot overtake a newer sleep cancellation");
 tick=61000;sgStaticScanTimer();work();expect(scans==0,"old queued start cannot trigger inventory while Ready bookkeeping is stale");
 reset();sgStaticScanPreference(true,true);pump();sgStaticScanSchedule(false);sgStaticScanCancel("sleep-after-wake-post");pump();
 expect(!g_sgStaticScanDelay.pending,"old queued wake message cannot rebuild a canceled delay");
 reset();sgStaticScanPreference(false);auto restored=sgStaticRestoreNow();
 expect(restored["ok"]==true&&restores==1&&restoreWrites==2,"explicit restore stays allowed with preference OFF");
 reset();sgStaticScanPreference(true,true);pump();restored=sgStaticRestoreNow();pump();
 expect(restored["ok"]==true&&!g_sgStaticScanDelay.pending&&timerDue==0,"restore consumes pending scan without scheduling another inventory");
 reset();g_exitRequested=true;restored=sgStaticRestoreNow();
 expect(restored["ok"]==false&&restoreWrites==0,"exit before restore denies all writes while hardware Ready may still be stale");
 reset();duringRestore=[](){g_exitRequested=true;};restored=sgStaticRestoreNow();
 expect(restored["ok"]==false&&restoreWrites==1,"exit during restore stops subsequent writes without clearing the outstanding work");
 reset();duringRestore=[](){g_powerLifecycle=PowerLifecycle::Suspended;};restored=sgStaticRestoreNow();
 expect(restored["ok"]==false&&restoreWrites==1,"sleep during restore cancels even before shared write gate refresh");
 reset();duringRestore=[](){++testScheduleGeneration;};restored=sgStaticRestoreNow();
 expect(restored["ok"]==false&&restoreWrites==1,"restore cannot cross power generations while Ready is unchanged");
 reset();duringRestore=[](){sgStaticScanCancel("kernel-power-506");};restored=sgStaticRestoreNow();
 expect(restored["ok"]==false&&restoreWrites==1,"original sleep event revision cancels restore before the GUI processes its message");
 reset();duringRestore=[](){gate=false;};restored=sgStaticRestoreNow();
 expect(restored["ok"]==false&&restoreWrites==1,"closing hardware write ownership stops further restore writes");
 reset();duringRestore=[](){sgStaticScanCancel("newer-manual-action");};restored=sgStaticRestoreNow();
 expect(restored["ok"]==false&&restoreWrites==1,"newer manual operation invalidates older restore continuation");
 duringRestore={};restored=sgStaticRestoreNow();
 expect(restored["ok"]==true&&restoreWrites==3,"later fresh explicit restore can finish previous pending recovery");
 reset();sgStaticScanPreference(false);duringRestore=[](){sgStaticScanPreference(false);};restored=sgStaticRestoreNow();
 expect(restored["ok"]==true&&restoreWrites==2,"remembered OFF does not veto its own restore");
 reset();sgStaticScanPreference(false);duringPost=[](){g_sgStaticScanRevision.fetch_add(1);};restored=sgStaticRestoreNow();
 expect(restored["ok"]==false&&restoreWrites==0,"restore must retain its own cancellation ticket when a newer revision lands before PostMessage returns");
 reset();sgStaticScanPreference(true);duringPost=[](){g_sgStaticScanRevision.fetch_add(1);};result=sgStaticScanNow();
 expect(result["ok"]==false&&scanWrites==0,"manual scan cannot adopt a later cancellation revision after posting its own cancel");
 reset();sgStaticScanPreference(true,true);duringGenerationRead=[](){sgStaticScanCancel("sleep-during-delay-arm");};pump();
 expect(!g_sgStaticScanDelay.pending&&timerDue==0,"old schedule message cannot consume a newer cancellation between revision check and arm");
 reset();sgStaticScanPreference(true,true);pump();tick=61000;
 duringClockRead=[](){sgStaticScanCancel("sleep-during-timer-dispatch");};sgStaticScanTimer();work();
 expect(scans==0&&scanWrites==0,"expired timer must keep its armed ticket when cancellation arrives during the clock read");
 reset();return checks;
}
}
static unsigned staticWakeScheduleTests(){return staticWakeSchedulePolicyTests()+static_wake_schedule_runtime::run();}
