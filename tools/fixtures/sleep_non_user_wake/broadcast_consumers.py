"""Complete original broadcast, with inert observer/device/OS leaves only."""
def bind(c):
    import json,hashlib
    from pathlib import Path
    p,f,h=c['preamble'],c['functions'],c['handlers']
    # One-shot observer faults. Keep the original owner functions and all
    # transition predicates extracted by the existing generator unchanged.
    ports=r'''
#include <map>
#include "sleep_wake_admission.h"
#include <stdexcept>
static std::string fault;
static bool faultHit=false,failFailureLog=false,failNextAllocation=false;
static void armSuspendNotificationAllocation(){if(fault.rfind("suspend-",0)==0&&fault.find("allocation")!=std::string::npos&&!faultHit)failNextAllocation=true;}
static void armDiagnosticAllocation(){if(fault=="argument-allocation")failNextAllocation=true;}
void* operator new(std::size_t n){if(failNextAllocation){failNextAllocation=false;faultHit=true;throw std::bad_alloc();}if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
static std::map<std::string,unsigned> visits;
static void visit(const char* name){++visits[name];if(fault==name&&!faultHit){faultHit=true;throw std::bad_alloc();}}

#include <filesystem>
#include <deque>
#include "log_frequency_policy.h"
#include "sleep_fact_log_record.h"
namespace fspath=std::filesystem;
static std::mutex g_sgFactMx;
static std::deque<std::string> g_sgFacts;
static constexpr size_t SG_FACT_HISTORY_LIMIT=64;
static std::wstring SG_FACT_LOG;
static bool fixtureActualJournal=false,fixtureRecordWillFail=false,fixtureMarkerFault=false;
static const char* fixtureRecordTarget="";
static unsigned fixtureActualRecordCalls=0;
static std::string sgFactTimestamp(){if(fixtureRecordWillFail){fixtureRecordWillFail=false;failNextAllocation=true;}return "fixture-clock";}
static void sgSleepActivityOnFact(const char*,const json&){}
static bool fixtureMarkerFormatterReturned=false;
static std::string fixtureMarkerEpoch(double value){if(fixtureMarkerFault&&!faultHit)failNextAllocation=true;auto exact=std::to_string(value);fixtureMarkerFormatterReturned=true;return exact;}
static void fixtureActualSgRecordFact(const char*,const json&);
static void fixtureRecordBoundary(const char* event,const json& details){
 if(fixtureActualJournal){
  fixtureRecordWillFail=!faultHit&&std::strcmp(event,fixtureRecordTarget)==0;
  ++fixtureActualRecordCalls;fixtureActualSgRecordFact(event,details);
 }
 if(std::strcmp(event,"power-broadcast")==0)visit("journal");
 facts.emplace_back(event,details);

}

static std::atomic<ULONGLONG> g_focusLightProofNotBefore{0};
static void focusEndIsolationOnUi(){visit("focus-end");}
static constexpr UINT_PTR TIMER_ID_POWER_SOURCE_RECOVERY=0xFF01;
static bool g_powerSourceRecoveryRunning=false;
struct {void cancel(){visit("power-source-cancel");}} g_powerSourceHeartbeat;
static void cpuUvTrialStop(const char*){visit("cpu-undervolt-stop");}
static void cpuUvTrialEmit(){}
static void steamWallpaperForegroundSignal(){visit("wallpaper");}
struct FixtureWallpaperOwner {void revoke(){visit("wallpaper-revoke");}};
[[maybe_unused]] static FixtureWallpaperOwner& steamWallpaperForegroundOwner(){static FixtureWallpaperOwner owner;return owner;}
static ULONGLONG fixtureSteamPrepareBoundary=0;
static void sleepRecoveryAutoPrepareSuspend(ULONGLONG boundary){fixtureSteamPrepareBoundary=boundary;visit("steam-prepare");}
static void sleepRecoveryAutoCancelPreparedSuspend(){visit("steam-cancel");}
static void sgAbnormalSuspendBoundary(){}
static const char* sgSleepModeName(SgSleepMode){return "mock";}
static unsigned fixtureSensorCaptures=0;
static void inputCaptureAppend(const json& details){if(details.is_object()&&details.contains("kind")&&details["kind"]=="sensor-suspend")++fixtureSensorCaptures;visit("capture");}
static void msiVisibilityObserverNoteResume(ULONGLONG){visit("msi-observation");}
static void focusTraceSample(const char*){visit("focus-observation");}
static void sgSleepTimeoutPowerBroadcast(ULONG){}
namespace ymcc::powerSource {static bool refresh(bool){return false;}struct State{int ac=0;};struct Owner{State sourceEvent(DWORD,ULONGLONG){return {};}};static Owner& owner(){static Owner o;return o;}}
static void publishPowerSourceReceipt(HWND,const char*){}
static void beginPowerSourceRecovery(HWND,bool){}
static int g_lastPowerEventAc=-1;
namespace ymcc::media::powerPolicy {static void ticket(){}}
namespace ymcc::media::downloads {static void notify(const std::wstring&){}}
static void sharedGameMediaPowerChanged(){}
struct Store{std::wstring root(){return L"";}};static Store& sharedGameMediaStore(){static Store s;return s;}
namespace ymcc::idle{static const GUID sessionDisplay={};}
static const GUID YM_GUID_ACDC_POWER_SOURCE={1},YM_GUID_ACTIVE_POWERSCHEME={2},YM_GUID_MONITOR_POWER_ON={3};
static std::atomic<int> g_idleSessionDisplayState{-1},g_idleLegacyMonitorState{-1};static bool g_monitorOn=true;
static void inputCaptureSessionDisplayPowerChanged(DWORD){}
static void sgAbnormalDisplay(DWORD){}
static void inputCaptureResumeSensorsForPower(){visit("sensor-resume");}
'''
    p=p.replace('using json = nlohmann::json;', 'using json = nlohmann::json;\nstatic void fixtureRecordBoundary(const char*,const json&);\nstatic void visit(const char*);\nstatic void armDiagnosticAllocation();\nstatic void armSuspendNotificationAllocation();')
    p=p.replace('bool sgHibernateAvailable(){++suspendLeafCalls;return false;}','bool sgHibernateAvailable(){++suspendLeafCalls;armSuspendNotificationAllocation();return false;}')
    p=p.replace('void focusClearSession(){++suspendLeafCalls;}','void focusClearSession(){++suspendLeafCalls;visit("focus");}')
    p=p.replace('void radialInputIsolationInvalidate(){++mockRadialInvalidations;}','void radialInputIsolationInvalidate(){++mockRadialInvalidations;visit("radial");}')
    p=p.replace('static void sgSleepTimeoutStop(const char*) noexcept {}',
      'static unsigned fixtureQueryTimeoutStops=0;static void sgSleepTimeoutStop(const char* reason) noexcept {if(std::strcmp(reason,"query-suspend-failed")==0)++fixtureQueryTimeoutStops;}')
    p=p.replace('void msiNativeForegroundPowerEvent(WPARAM event){assert(event==PBT_APMSUSPEND);++isolationSuspendCalls;isolationHold=true;isolationResumePending=false;}','void msiNativeForegroundPowerEvent(WPARAM event){if(event==PBT_APMSUSPEND){++isolationSuspendCalls;isolationHold=true;isolationResumePending=false;visit("msi-suspend-queue");}else visit("msi-completion");}')
    record_stub='void sgRecordFact(const char* e,const json& d=json::object()){facts.emplace_back(e,d);if(fixtureSleepFactHook)fixtureSleepFactHook(e);}'
    assert p.count(record_stub)==1,'unique fixture fact recorder with notification hook'
    p=p.replace(record_stub,record_stub.replace('facts.emplace_back(e,d);','fixtureRecordBoundary(e,d);'),1)
    p=p.replace('if(id==FOCUS_DISPLAY_TIMER_ID){','if(id==TIMER_ID_POWER_SOURCE_RECOVERY)return TRUE;if(id==FOCUS_DISPLAY_TIMER_ID){')
    # The constant must precede the already existing fake KillTimer.
    p=p.replace('using json = nlohmann::json;', 'using json = nlohmann::json;\nstatic constexpr UINT_PTR TIMER_ID_POWER_SOURCE_RECOVERY=0xFF01;')
    ports=ports.replace('static constexpr UINT_PTR TIMER_ID_POWER_SOURCE_RECOVERY=0xFF01;','')
    # Existing legacy support provides the original classifier and publisher.
    p=p.replace('void sleepRecoveryAutoCloseSuspendEpisode(){}','void sleepRecoveryAutoCloseSuspendEpisode(){visit("steam-close");}')
    p=p.replace('void sleepRecoveryAutoInteractiveResume(ULONGLONG,int,ULONGLONG){}','void sleepRecoveryAutoInteractiveResume(ULONGLONG,int,ULONGLONG){visit("steam-interactive");armDiagnosticAllocation();}')
    # Its original diagnostics port is replaced, not added beside it.
    import re
    if 'void appendNativeLifecycleLog(' in p:raise RuntimeError('unexpected duplicate log port')
    p+=ports
    p+='void appendNativeLifecycleLog(const char* event,const json& =json::object()){if(std::string(event)=="power-broadcast-consumer-failed"){visit("failure-log");if(failFailureLog)throw std::bad_alloc();}else if(std::string(event)=="power-device-wake"||std::string(event)=="power-device-sleep")visit("device-log");}\n'
    # Consume the complete current shared entry as well as the caller case.
    common=c['function']('handlePowerResumeNotification')
    original='void handlePowerResumeNotification(SgWork w,const char*){'
    if p.count(original)!=1:raise RuntimeError('unique original memory common port')
    at=p.index(original);opening=p.index('{',at);end=c['braced'](opening,c['mask_cpp'](p))
    p=p[:at]+'static void handlePowerResumeNotification(SgWork,const char*);'+p[end:]
    p=p.replace('using json = nlohmann::json;', 'using json = nlohmann::json;\nstatic bool fixtureCommonScope=false;\nstatic unsigned fixtureWakeQueueCalls=0;\nstatic unsigned long long fixtureWakeQueueGeneration=0;\nstatic int fixtureWakeQueueKind=-1;\nstatic void armCommonNotificationAllocation();')
    p=p.replace('void sgQueueWork(SgWork w,ULONGLONG g,ULONGLONG=0){','void sgQueueWork(SgWork w,ULONGLONG g,ULONGLONG=0){if(fixtureCommonScope){++fixtureWakeQueueCalls;fixtureWakeQueueGeneration=g;fixtureWakeQueueKind=static_cast<int>(w);return;}')
    p=p.replace('void inputHostRequestPowerRelease(ULONGLONG,const char*){++suspendLeafCalls;}','void inputHostRequestPowerRelease(ULONGLONG,const char*){++suspendLeafCalls;visit("common-input-release");}')
    p=p.replace('void gamepadResetNativeState(bool){++suspendLeafCalls;}','void gamepadResetNativeState(bool){++suspendLeafCalls;visit("common-input-reset");}')
    p=p.replace('void sgHandheldResumeInputOnWake(const char*){}','void sgHandheldResumeInputOnWake(const char*){visit("common-handheld");if(fixtureCommonScope)armDiagnosticAllocation();}')
    p=p.replace('void inputHostRequestWakeRescan(ULONGLONG){}','void inputHostRequestWakeRescan(ULONGLONG){visit("common-input-rescan");}')
    p=p.replace('void ipc_emit(const char*,const json&){++suspendLeafCalls;}','void ipc_emit(const char* event,const json&){++suspendLeafCalls;if(std::string(event)=="power.resuming")visit("common-resuming");}')
    p=p.replace('void armPowerResumeWatchdog(ULONGLONG){}','void armPowerResumeWatchdog(ULONGLONG){armCommonNotificationAllocation();}')
    p=p.replace('static void msiVisibilityObserverNoteResume(ULONGLONG){visit("msi-observation");}','static void msiVisibilityObserverNoteResume(ULONGLONG){visit(fixtureCommonScope?"common-msi-observation":"msi-observation");}')
    p=p.replace('std::string(event)=="power-broadcast-consumer-failed"','(std::string(event)=="power-broadcast-consumer-failed"||std::string(event)=="power-resume-entry-consumer-failed")')
    p=p.replace('else if(std::string(event)=="power-device-wake"','else if(std::string(event)=="power-resume-notification")visit("common-entry-log");else if(std::string(event)=="power-device-wake"')
    p+='\nstatic void armCommonNotificationAllocation(){if(fixtureCommonScope&&fault=="common-notification-allocation")failNextAllocation=true;}\n'
    p=p.replace('if(fault=="argument-allocation")failNextAllocation=true;','if(fault=="argument-allocation"||fault=="common-keep-journal-allocation")failNextAllocation=true;')
    p+='\nstatic void appendFanLifecycleLog(const char*,const json&){visit("common-fan-retry");}\nstatic void xinputSlotsMarkDiscoveryDirty(){}\n'
    # Retained delivery reads the same inert captured target identity.
    p=p.replace('struct SgSleepTarget {bool markerOwned=false;unsigned long long powerGeneration=0;};','struct SgSleepTarget {bool markerOwned=false;unsigned long long powerGeneration=0;DWORD pid=0;ULONGLONG processCreated=0;};')
    p=p.replace('target={frozen,generation};return frozen;','target={frozen,generation,frozen?1u:0u,frozen?1ull:0ull};return frozen;')
    # The stronger new selections reach the actual deque owner, not the counted
    # common-entry port used by the earlier cases. No worker thread is started.
    p=p.replace('static void armCommonNotificationAllocation();','static void armCommonNotificationAllocation();\nstatic bool fixtureUseNativeQueue=false;\nstatic void fixtureActualQueue(SgWork,unsigned long long,ULONGLONG);')
    # SgWork must be declared before that prototype.
    p=p.replace('static void fixtureActualQueue(SgWork,unsigned long long,ULONGLONG);','enum class SgWork : uint8_t;\nstatic void fixtureActualQueue(SgWork,unsigned long long,ULONGLONG);')
    p=p.replace('void sgQueueWork(SgWork w,ULONGLONG g,ULONGLONG=0){','void sgQueueWork(SgWork w,ULONGLONG g,ULONGLONG originalWake=0){if(fixtureUseNativeQueue){fixtureActualQueue(w,g,originalWake);return;}')
    p=p.replace('visit(fixtureCommonScope?"common-msi-observation":"msi-observation");}', 'visit(fixtureCommonScope?"common-msi-observation":"msi-observation");if(fault=="common-native-queue-allocation-fails")failNextAllocation=true;}')
    source=c['source'];mask=c['mask_cpp'](source);start=mask.index('struct SgWorkItem {');opening=mask.index('{',start);finish=c['braced'](opening,mask)
    workItem=source[start:finish]+';'
    originalQueue=c['function']('sgQueueWork')
    queue=originalQueue.replace('static void sgQueueWork(','static void fixtureActualQueue(',1)
    scalars=[]
    for key in ['g_sgManualPauseSerial','g_sgLastQueuedGameWake']:
        lines=[line for line in source.splitlines() if line.startswith(('static std::atomic<','static SgWorkItem ')) and key in line and line.endswith(';')]
        if len(lines)!=1:raise RuntimeError('exact native queue scalar '+key)
        scalars.append(lines[0])
    assert p.count(workItem)==1
    p+='\n#include <deque>\n'+ '\n'.join(scalars)+r'''
static std::condition_variable g_sgWorkCv;
static std::deque<SgWorkItem> g_sgWorkQ;
static bool g_sgWorkStop=false;
static unsigned long long g_sgWorkWakeRevision=0;
static bool sgOrdinaryPauseCurrent(const SgWorkItem&){throw std::runtime_error("unexpected ordinary Suspend in wake-only native queue selection");}
static bool sgReturnedPauseCurrent(const SgWorkItem&){throw std::runtime_error("unexpected returned-pause entry in wake-only native queue selection");}
static void sleepRecoveryAutoCaptureSuspend(unsigned long long,ULONGLONG){throw std::runtime_error("unexpected real Steam snapshot in wake-only native queue selection");}
'''
    retained_helpers=''
    if 'struct SgRetainedWorkDelivery {' in source:
        import runpy
        def retained_function(name,text,label=None):
            mask=c['mask_cpp'](text)
            matches=list(re.finditer(r'\bstatic\s+[^;{}]*?\b'+re.escape(name)+r'\s*\([^;{}]*?\)\s*(?:noexcept\s*)?\{',mask))
            if len(matches)!=1:raise RuntimeError('exact retained helper '+name)
            at=matches[0].start();opening=mask.index('{',at)
            return text[at:c['braced'](opening,mask)]
        retained=runpy.run_path(str(Path(__file__).parent/'retained_delivery.py'))
        returned=(c['args'].source.parent/'sleep_returned_pause_native.inc').read_text(encoding='utf-8-sig')
        _,cells,_=retained['parts']({'source':source,'mask':c['mask_cpp'],'function':retained_function},returned)
        retained_helpers='\n'.join(retained_function(name,returned) for name in retained['NAMES'][:8])
        p+='\n#include <array>\n'+cells+'\n'
    f+='\n'+retained_helpers+'\n'+queue+'\n'

    with (c['out']/'native-queue-bindings.json').open('x',encoding='utf-8') as output:
        json.dump({'originalQueue':originalQueue,'sha256':hashlib.sha256(originalQueue.encode()).hexdigest(),'onlyDefinitionNameRebound':True,'originalWorkItem':workItem,'actualQueueMutexDequeAndConditionVariable':True,'workerNotStarted':True,'suspendAndReturnedPausePortsReject':True,'retainedDeliveryHelpers':retained_helpers,'retainedDeliveryHelpersSha256':hashlib.sha256(retained_helpers.encode()).hexdigest()},output,indent=2)

    actual_record=c['function']('sgRecordFact')
    rebound_record=actual_record.replace('static void sgRecordFact(','static void fixtureActualSgRecordFact(',1)
    f=rebound_record+'\n'+f
    with (c['out']/'actual-journal-bindings.json').open('x',encoding='utf-8') as output:
        json.dump({'actualRecord':actual_record,'sha256':hashlib.sha256(actual_record.encode()).hexdigest(),'onlyDefinitionNameRebound':True,'actualDecoratorHeader':'native/sleep_fact_log_record.h','actualFrequencyHeader':'native/log_frequency_policy.h','actualFilesystemJournalUnderOwnedRoot':True,'timestampTerminal':'inert clock arms next original allocation only;does not throw itself','activityObserverTerminal':'no-op;does not grant actions'},output,indent=2)
    f+='\n'+common+'\n'
    with (c['out']/'common-entry-bindings.json').open('x',encoding='utf-8') as output:
        json.dump({'actualCommonEntry':common,'sha256':hashlib.sha256(common.encode()).hexdigest(),'completeOriginalFunction':True,'primaryClassifiersUnchanged':True,'queueTerminal':'original memory port; selected common cases count accepted calls without executing game worker'},output,indent=2)

    marker_formatter_adapter=None
    if 'marker-allocation' in (c['args'].broadcast_consumer_case or ''):
        original_formatter='std::to_string(g_sgSleepTriggerEpoch)'
        fixture_formatter='fixtureMarkerEpoch(g_sgSleepTriggerEpoch)'
        if f.count(original_formatter)!=1:raise RuntimeError('unique original trigger marker formatter')
        f=f.replace(original_formatter,fixture_formatter)
        marker_formatter_adapter={'original':original_formatter,'fixture':fixture_formatter,'onlyDiagnosticFormatterLeaf':True,'originalStdToStringInvokedWithOriginalValue':True,'nextAllocationArmedBeforeOriginalFormatter':True,'primaryPredicatesQueueAndReceiptUnchanged':True}
    with (c['out']/'marker-formatter-binding.json').open('x',encoding='utf-8') as output:
        json.dump({'adapter':marker_formatter_adapter,'selected':c['args'].broadcast_consumer_case},output,indent=2)
    sensor=c['function']('inputCaptureSuspendSensorsForPower')
    sensor_stub='static void inputCaptureSuspendSensorsForPower(){++legacySensorPowerSuspends;}'
    if p.count(sensor_stub)!=1:raise RuntimeError('exact original sensor suspend port')
    p=p.replace(sensor_stub,'static void inputCaptureSuspendSensorsForPower();')
    family_anchor='enum class YmccFamily : int {'
    if c['source'].count(family_anchor)!=1:raise RuntimeError('exact current family enum')
    family_at=c['source'].index(family_anchor);family_open=c['source'].index('{',family_at)
    family=c['source'][family_at:c['braced'](family_open,c['mask_cpp'](c['source']))]+';'
    p+='\n'+family+'\n'+r'''
#include "sensor_thread_mailbox.h"
#include <memory>
using SensorThreadRequest=ymcc::sensors::ThreadRequest;
struct {YmccFamily family=YmccFamily::Unknown;} g_machineIdentity;
static unsigned fixtureSensorCancels=0,fixtureSensorPosts=0,fixtureSensorBindings=0,fixtureSensorResets=0,fixtureSensorDomainLogs=0;
struct FixtureSensorLease {void cancel()noexcept{++fixtureSensorCancels;}};
static std::atomic<std::shared_ptr<FixtureSensorLease>> g_sensorActivePower;
static std::atomic<bool> g_inputCaptureReady{true},g_inputCaptureSensorsSuspended{false},g_sensorWakePollActive{true};
struct FixtureSensorSink {void SetBindingGeneration(unsigned long long value){if(value)throw std::runtime_error("unexpected sensor suspend binding");++fixtureSensorBindings;}void ResetSamples(){++fixtureSensorResets;}} fixtureSensorSink;
static FixtureSensorSink* g_inputCaptureSink=&fixtureSensorSink;
static int fixtureSensorFault=0;
static bool fixtureSensorAccepted=true;
static void legionMotionRevoke(){throw std::runtime_error("unexpected Legion terminal in generic sensor broadcast case");}
static bool inputCaptureSensorThreadPostPower(SensorThreadRequest req,unsigned long long predecessor,const void* retry,const char** refusal){
 if(req!=SensorThreadRequest::Suspended||predecessor||retry||!refusal)throw std::runtime_error("unexpected sensor terminal arguments");
 ++fixtureSensorPosts;*refusal=fixtureSensorAccepted?nullptr:"fixture-command-refused";
 if(fixtureSensorFault)failNextAllocation=true;
 return fixtureSensorAccepted;
}
static void appendDomainLog(const char* domain,const char* event,const json&){if(std::strcmp(domain,"gyro")||std::strcmp(event,"gyro-sleep"))throw std::runtime_error("unexpected domain log");++fixtureSensorDomainLogs;}
'''+'\n'
    f+='\n'+sensor+'\n'
    with (c['out']/'actual-sensor-suspend-binding.json').open('x',encoding='utf-8') as output:
        json.dump({'actualSensorSuspend':sensor,'sha256':hashlib.sha256(sensor.encode()).hexdigest(),'functionBodyBytePreserved':True,'actualCurrentFamilyEnum':family,'commandAndLeaseSinkTerminals':'Counted inert command acceptance/cancel/sink;not actual provider,Scope or cleanup completion proof','actualMailboxHeader':'native/sensor_thread_mailbox.h','realFault':'next operator new after typed Suspend submission result;actual JSON argument construction in original sensor function'},output,indent=2)
    body=c['case_body']('case WM_POWERBROADCAST:', 'case WM_TRAYICON:')
    raw_msi_completion='msiNativeForegroundPowerEvent(event)' in c['mask_cpp'](body)
    p+='\nstatic constexpr bool fixtureRawMsiCompletion='+str(raw_msi_completion).lower()+';\n'
    wallpaper_public='static bool steamWallpaperForegroundSharedPowerChanged(' in source
    p+='\nstatic constexpr bool fixtureWallpaperPublic='+str(wallpaper_public).lower()+';\n'
    if wallpaper_public:
        p+=r'''
#include "steam_wallpaper_foreground.h"
static ymcc::steamwallpaperForeground::Authority& fixtureWallpaperPublicOwner(){
    static ymcc::steamwallpaperForeground::Authority owner("broadcast-fixture");return owner;
}
static bool sharedPowerLifecycleCurrent(const ymcc::sleeppower::SharedPowerV1&) noexcept;
static bool steamWallpaperForegroundWake(){visit("wallpaper");return true;}
'''
        f+='\n'+c['function']('steamWallpaperForegroundSharedPowerChanged').replace('steamWallpaperForegroundOwner()','fixtureWallpaperPublicOwner()')+r'''
static uint64_t fixtureWallpaperOpen(){
    auto& owner=fixtureWallpaperPublicOwner();ymcc::steamwallpaperForeground::Facts facts;
    facts.power=currentPowerGeneration();facts.suspendSerial=1;
    const auto proof=owner.observe(facts,owner.ticket());return proof.at("revision").get<uint64_t>();
}
static bool fixtureWallpaperDeliverSuspend(uint64_t expected){
    ymcc::sleeppower::SharedPowerV1 power;power.generation=currentPowerGeneration();power.suspendSerial=2;
    power.phase=static_cast<uint32_t>(g_powerLifecycle.load());power.flags=ymcc::sleeppower::kKnown;
    return steamWallpaperForegroundSharedPowerChanged(power)&&fixtureWallpaperPublicOwner().ticket()==expected;
}
'''
    else:
        f+='\nstatic uint64_t fixtureWallpaperOpen(){return 0;}\nstatic bool fixtureWallpaperDeliverSuspend(uint64_t){return false;}\n'
    actual='LRESULT broadcast(HWND h,UINT m,WPARAM w,LPARAM l){\n'+body+'\n}\n'
    # Retain reset from the original fixture; no accepted old business executes.
    oldchecks=c['checks'];opening=oldchecks.index('void reset(bool pause=false){');end=oldchecks.index('\nvoid sleepIntent()',opening)
    reset=oldchecks[opening:end]
    checks=r'''
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void at(ULONGLONG tick){nowTick=tick;nowFile=epoch+tick*10000;}
'''+reset+r'''
int main(int argc,char** argv){
 if(argc!=2){std::cerr<<"exact scenario required\n";return 2;}
 const std::string selected=argv[1];unsigned assertions=0;
 auto check=[&](bool ok,const char* text){++assertions;require(ok,text);};
 try{
  reset(false);platformAoAc=false;faultHit=false;fault.clear();visits.clear();
  if(selected.rfind("sensor-query-",0)==0){
   if(!(selected=="sensor-query-normal"||selected=="sensor-query-capture-allocation"||selected=="sensor-query-refused-allocation"||selected=="sensor-query-diagnostic-double"||selected=="sensor-query-already-paused"||selected=="sensor-query-inactive"))throw std::runtime_error("unknown exact sensor broadcast scenario");
   g_machineIdentity.family=YmccFamily::Unknown;keepInput=false;fault.clear();
   const bool paused=selected=="sensor-query-already-paused",inactive=selected=="sensor-query-inactive";
   const bool inject=selected=="sensor-query-capture-allocation"||selected=="sensor-query-refused-allocation"||selected=="sensor-query-diagnostic-double";
   fixtureSensorFault=inject?1:0;fixtureSensorAccepted=selected!="sensor-query-refused-allocation";failFailureLog=selected=="sensor-query-diagnostic-double";
   g_sensorActivePower.store(std::make_shared<FixtureSensorLease>());g_inputCaptureReady=!inactive;g_inputCaptureSensorsSuspended=paused;g_sensorWakePollActive=true;
   const auto before=currentPowerGeneration();const auto requestsBefore=requestCount;
   bool escaped=false;try{broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMQUERYSUSPEND,0);}catch(...){escaped=true;}
   check(!escaped,"sensor diagnostic argument allocation must not escape shared query delivery");
   check(faultHit==inject,"exact original sensor JSON allocation must execute only when selected");
   check(currentPowerGeneration()==before+1&&suspendQueued==1,"sensor observer failure cannot block original generation and pause delivery");
   check(fixtureSensorCancels==1,"original sensor scope cancellation occurs once before diagnostics");
   check(fixtureSensorPosts==((paused||inactive)?0u:1u),"original sensor command submission remains exact and is never replayed");
   check(fixtureSensorBindings==((paused||inactive)?0u:1u)&&fixtureSensorResets==fixtureSensorBindings,"original sink invalidation precedes diagnostic failure");
   if(!paused&&!inactive)check(g_inputCaptureSensorsSuspended&&!g_sensorWakePollActive,"partial sensor pause remains true,not forged as cleanup completion");
   if(inject)check(fixtureSensorCaptures==0&&fixtureSensorDomainLogs==0,"failed argument cannot pretend that later diagnostics ran");
   if(!inject&&!paused&&!inactive)check(fixtureSensorCaptures==1&&fixtureSensorDomainLogs==1,"normal original sensor receipts remain emitted");
   fixtureSensorFault=0;broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMSUSPEND,0);
   check(currentPowerGeneration()==before+1&&suspendQueued==1&&fixtureSensorCancels==1,"confirmation does not replay an uncertain sensor call or duplicate pause");
   check(!g_sleepPowerDispatch.hasPending()&&g_sleepPowerDispatch.snapshot()["consumerActive"]==false,"original receipt owner fully returns");
   check(requestCount==requestsBefore&&!sgSleepRetryActive(),"sensor log failure cannot request sleep or spend retry allowance");
   broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMRESUMECRITICAL,0);check(g_powerLifecycle==PowerLifecycle::Ready&&!frozen,"later original shared wake still completes");
  }else if(selected.rfind("journal-",0)==0){
   if(!(selected=="journal-query-normal"||selected=="journal-bare-normal"||selected=="journal-query-armed-record"||selected=="journal-bare-armed-record"||selected=="journal-query-origin-record"||selected=="journal-bare-origin-record"||selected=="journal-query-classification-record"||selected=="journal-bare-classification-record"||selected=="journal-bare-monitor-record"||selected=="journal-query-marker-allocation"||selected=="journal-bare-marker-allocation"))throw std::runtime_error("unknown exact journal scenario");
   fixtureActualJournal=true;fixtureActualRecordCalls=0;fixtureRecordWillFail=false;fixtureMarkerFault=selected.find("marker-allocation")!=std::string::npos;
   platformAoAc=true;keepInput=false;SG_FACT_LOG=(fspath::path(argv[0]).parent_path()/L"sleep-facts.jsonl").wstring();
   const bool query=selected.rfind("journal-query-",0)==0;
   const bool normal=selected.find("normal")!=std::string::npos;
   const bool inherited=selected.find("origin-record")!=std::string::npos;
   const bool classified=selected.find("classification-record")!=std::string::npos;
   const bool monitor=selected.find("monitor-record")!=std::string::npos;
   fixtureRecordTarget=inherited?"sleep-intent-origin-inherited":classified?"resume-classification-stopped":monitor?"monitor-entry-failure-canceled":normal||fixtureMarkerFault?"":"non-user-wake-armed";
   fault=selected;const auto before=currentPowerGeneration();const auto requestsBefore=requestCount;
   if(inherited){g_sgLastPowerButtonSleepIntentFileTime=nowFile;g_sgLatestUserSleepEntryFileTime=nowFile-10000;}
   if(classified)g_sgResumeClassification.generation=before;
   if(monitor){g_monitorEntryFailureTimerArmed=true;g_monitorEntryFailureGeneration=before;g_monitorEntryFailureIntent=nowFile;g_monitorEntryFailureDeadline=nowTick+5000;monitorTimerDue=nowTick+5000;}
   bool escaped=false;try{broadcast(g_hwnd,WM_POWERBROADCAST,query?PBT_APMQUERYSUSPEND:PBT_APMSUSPEND,0);}catch(...){escaped=true;}
   check(!escaped,"actual journal allocation cannot escape original sleep delivery");
   check(faultHit!=normal,"selected real original recorder or trigger marker allocation must execute exactly once");
   if(fixtureMarkerFault)check(!fixtureMarkerFormatterReturned,"fault is in original std::to_string allocation before formatter returns");
   check(fixtureActualRecordCalls>0,"complete original record body runs with its real decorator and owned file journal");
   check(currentPowerGeneration()==before+1&&suspendQueued==1,"diagnostics cannot strand the owned generation before original pause delivery");
   check(g_sgTask.generation==before+1&&g_sgRepairEligible&&g_sgSleepCycleActive,"original complete trigger initialization remains current");
   check(g_sgResumeClassification.generation==0,"old classification wait is fully retired");
   if(inherited)check(g_sgLastPowerButtonSleepIntentFileTime==nowFile-10000,"original admitted kernel entry refines time despite failed diagnostic");
   if(monitor)check(!g_monitorEntryFailureTimerArmed&&!g_monitorEntryFailureGeneration&&!g_monitorEntryFailureIntent&&!g_monitorEntryFailureDeadline&&!monitorTimerDue,"canceled monitor owns no stale state after its real journal fails");
   if(query)broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMSUSPEND,0);
   check(suspendQueued==1&&currentPowerGeneration()==before+1,"confirming notification cannot duplicate or replace original pause");
   check(g_sleepPowerDispatch.snapshot()["consumerActive"]==false&&!g_sleepPowerDispatch.hasPending(),"original receipt ownership completes");
   check(requestCount==requestsBefore&&!sgSleepRetryActive(),"diagnostic failure cannot request sleep or spend original retry allowance");
   fixtureRecordTarget="";fixtureMarkerFault=false;fault.clear();broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMRESUMECRITICAL,0);
   check(g_powerLifecycle==PowerLifecycle::Ready&&!frozen,"later original critical wake finishes shared recovery");
  }else if(selected.rfind("common-",0)==0){
   fixtureUseNativeQueue=selected.rfind("common-native-queue-",0)==0;
   fixtureCommonScope=true;fixtureWakeQueueCalls=0;fixtureWakeQueueGeneration=0;fixtureWakeQueueKind=-1;
   g_powerLifecycle=PowerLifecycle::Suspended;g_inputReady=false;g_hardwareWriteGate=false;
   g_sgInSuspend=true;g_sgGameActuallySuspended=true;frozen=true;keepInput=selected=="common-native-queue-keep-allocation"||selected=="common-keep-journal-allocation"||selected=="common-handheld"||selected=="common-input-rescan";
   const auto originalGeneration=currentPowerGeneration();
   fault=selected=="common-native-queue-entry-allocation"?"common-entry-allocation":selected=="common-native-queue-keep-allocation"?"common-keep-journal-allocation":selected;failFailureLog=selected=="common-diagnostic-double";
   if(fixtureUseNativeQueue){g_sgWorkQ.clear();g_sgWorkWakeRevision=0;g_sgManualPauseSerial=13;g_sgLastQueuedGameWake={};}
   if(selected=="common-native-queue-allocation-fails"){bool prepared=false;for(unsigned i=0;i<64;++i){failNextAllocation=true;try{g_sgWorkQ.push_back(SgWorkItem{SgWork::WakeAutomatic,100+i});failNextAllocation=false;}catch(const std::bad_alloc&){prepared=true;break;}}check(prepared,"controlled deque capacity reaches its real next allocation");}
   if(fault=="common-entry-allocation"||selected=="common-diagnostic-double")failNextAllocation=true;
   const bool retry=selected=="common-retry-automatic";
   const bool duplicate=selected=="common-duplicate-automatic"||selected=="common-duplicate-interactive";
   if(retry){g_sgRetryInProgress=true;g_sgTask.retryKind=SgRetryKind::EntryFailure;g_sgTask.retryAttempt=2;g_sgTask.mode=SgSleepMode::S3;}
   if(duplicate){g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;g_resumeReadyGeneration=originalGeneration;g_lastResumeNotifyTick=nowTick;frozen=false;g_sgSleepCycleActive=false;g_sgTask={};}
   const auto work=retry||selected=="common-duplicate-automatic"?SgWork::WakeAutomatic:SgWork::WakeSuspend;
   bool escaped=false;try{handlePowerResumeNotification(work,"fixture-common-entry");}catch(...){escaped=true;}
   check(!escaped,"independent common entry consumer cannot escape and suppress lifecycle queue");
   check(currentPowerGeneration()==originalGeneration,"original sleeping/duplicate generation stays unchanged");
   const unsigned expectedQueue=retry||selected=="common-duplicate-automatic"?0:1;
   if(fixtureUseNativeQueue){SgWorkItem consumed{};fixtureWakeQueueCalls=0;while(sgTakeWorkDeliveryLocked(consumed))if(consumed.generation==originalGeneration){++fixtureWakeQueueCalls;fixtureWakeQueueGeneration=consumed.generation;fixtureWakeQueueKind=static_cast<int>(consumed.kind);check(consumed.wakeManualPauseFence==13,"delivered wake retains original manual-pause fence");}check(g_sgWorkWakeRevision==1&&g_sgLastQueuedGameWake.generation==originalGeneration&&g_sgLastQueuedGameWake.wakeManualPauseFence==13,"actual native deque accepted once and retained original explicit-wake fence");}
   check(fixtureWakeQueueCalls==expectedQueue,"original retry and duplicate policy decides exact lifecycle queue count");
   if(expectedQueue)check(fixtureWakeQueueGeneration==originalGeneration&&fixtureWakeQueueKind==static_cast<int>(work),"original queue kind and generation remain exact");
   if(!retry&&!duplicate){check(g_powerLifecycle==PowerLifecycle::Resuming&&frozen,"UI admission queues work without resuming exact game lease itself");check(g_lastResumeNotifyTick==nowTick,"original receive-time anchor is retained");}
   if(retry)check(sgSleepRetryActive()&&g_sgTask.retryAttempt==2&&frozen,"automatic retry observation cannot cancel or release held lease");
   if(duplicate)check(g_powerLifecycle==PowerLifecycle::Ready&&g_hardwareWriteGate,"recently committed duplicate cannot reopen a transition");
   if(!retry&&!duplicate&&selected!="common-normal"&&selected!="common-native-queue-normal")check(faultHit,"requested real argument allocation or observer boundary must execute");
   if(selected=="common-native-queue-normal"||selected=="common-native-queue-allocation-fails"){
    const bool retained=selected=="common-native-queue-allocation-fails";
    for(const bool projectedOnly:{false,true}){
    if(retained){bool primed=false;for(unsigned i=0;i<64;++i){failNextAllocation=true;try{g_sgWorkQ.push_back({SgWork::WakeAutomatic,100+i});failNextAllocation=false;}catch(const std::bad_alloc&){primed=true;break;}}check(primed,"real deque allocation boundary reachable");}
    const auto initial=g_sgWorkWakeRevision;
    const auto oldFence=g_sgLastQueuedGameWake.wakeManualPauseFence;
    failNextAllocation=retained;fixtureActualQueue(SgWork::WakeSuspend,originalGeneration,0);
    check(!failNextAllocation&&g_sgWorkWakeRevision==initial+1,"old WakeSuspend published through expected queue path");
    if(retained)check(g_sgRetainedWorkDelivery[static_cast<size_t>(SgWork::WakeSuspend)].order!=0,"old wake used retained slot");
    check(g_sgLastQueuedGameWake.wakeManualPauseFence==oldFence,"same-boundary redelivery kept first accepted fence after pop");
    if(projectedOnly)++g_sharedPowerSuspendSerial;
    else {const auto sleep=g_sleepPowerDispatch.window(PBT_APMSUSPEND,nowTick+1);check(bool(sleep),"new raw sleep boundary published");
     g_sleepPowerDispatch.complete(sleep,nowTick+1);}
    const auto currentFence=++g_sgManualPauseSerial;
    failNextAllocation=retained;fixtureActualQueue(SgWork::WakeSuspend,originalGeneration,0);
    const bool newAccepted=g_sgWorkWakeRevision==initial+2;
    const bool allocated=!failNextAllocation;failNextAllocation=false;
    check(newAccepted&&allocated,"old same-generation wake swallowed current wake");
    if(retained)check(g_sgRetainedWorkDelivery[static_cast<size_t>(SgWork::WakeSuspend)].order!=0,"new wake used retained slot");
    ++g_sgManualPauseSerial;
    fixtureActualQueue(SgWork::WakeSuspend,originalGeneration,0);
    check(g_sgWorkWakeRevision==initial+2,"same-boundary duplicate wake was queued again");
    unsigned current=0;SgWorkItem consumed{};while(sgTakeWorkDeliveryLocked(consumed))if(consumed.kind==SgWork::WakeSuspend){++current;check(consumed.wakeManualPauseFence==currentFence,"current wake owns the new boundary fence and duplicate cannot widen it");}
    check(current==1&&!sgRetainedWorkDeliveryPendingLocked(),"stale wake was delivered or retained after current wake");
    }
    if(!retained){
     g_sgConfirmedSuspendGeneration=originalGeneration;
     const auto sequence=g_sleepPowerDispatch.observedSleepBoundarySequence();
     const auto observed=g_sleepPowerDispatch.observedSequence();
     const auto publication=g_sharedPowerPublicationRevision.load();
     const auto issued=g_sleepPowerDispatch.snapshot()["issued"].get<uint64_t>();
     const auto fence=g_sgLastQueuedGameWake.wakeManualPauseFence;
     fixtureActualQueue(SgWork::WakeSuspend,originalGeneration,0);
     check(g_sgWorkQ.size()==1&&g_powerLifecycle==PowerLifecycle::Resuming,
           "the original wake is queued while a confirmed generation is resuming");
     at(nowTick+10);broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMQUERYSUSPEND,0);
     SgWorkItem delivered{};
     check(sgTakeWorkDeliveryLocked(delivered)&&delivered.kind==SgWork::WakeSuspend&&
           delivered.generation==originalGeneration&&delivered.wakeManualPauseFence==fence,
           "a late query discarded the already accepted game wake");
     check(!sgTakeWorkDeliveryLocked(delivered)&&g_sleepPowerDispatch.observedSleepBoundarySequence()==sequence&&
           g_sleepPowerDispatch.observedSequence()==observed&&g_sleepPowerDispatch.snapshot()["issued"]==issued+1&&
           !g_sleepPowerDispatch.pendingSuspend()&&!g_sleepPowerDispatch.hasPending(),
           "the ignored query completes as evidence without advancing the active power boundary");
     at(nowTick+10);broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMQUERYSUSPEND,0);
     check(g_sharedPowerPublicationRevision.load()==publication,
           "late queries before and after taking a wake must not invalidate the public scope");
     check(delivered.wakeBoundaryMatches(g_sleepPowerDispatch.observedSleepBoundarySequence(),g_sharedPowerSuspendSerial.load()),
           "a late query invalidated the wake already taken by the worker");
     const auto successor=g_sleepPowerDispatch.window(PBT_APMSUSPEND,nowTick+1);
     check(bool(successor)&&!delivered.wakeBoundaryMatches(g_sleepPowerDispatch.observedSleepBoundarySequence(),g_sharedPowerSuspendSerial.load()),
           "a genuine new suspend must still invalidate the same taken wake");
     g_sleepPowerDispatch.complete(successor,nowTick+1);
     const auto resumes=resumeCount;
     sgRealWake("resume_suspend",originalGeneration,&delivered);
     check(frozen&&resumeCount==resumes,"actual wake admission rejects the taken wake after a genuine new suspend");
     fixtureActualQueue(SgWork::WakeSuspend,originalGeneration,0);
     check(sgTakeWorkDeliveryLocked(delivered),"current wake is claimable under the new boundary");
     at(nowTick+10);broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMQUERYSUSPEND,0);
     sgRealWake("resume_suspend",originalGeneration,&delivered);
     check(!frozen&&resumeCount==resumes+1,"actual wake admission reaches the game recovery port once after the ignored query");
    }
   }
  }else if(selected=="focus"){
   const auto originalGeneration=currentPowerGeneration();fault="focus";
   bool escaped=false;try{broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMSUSPEND,0);}catch(...){escaped=true;}
   check(!escaped&&faultHit,"only admitted suspend runs focus cleanup, and its exception cannot escape");
   check(g_powerLifecycle==PowerLifecycle::Suspending&&currentPowerGeneration()==originalGeneration+1&&suspendQueued==1,"focus cleanup failure cannot block the original suspend owner");
   const auto proofCutoff=g_focusLightProofNotBefore.load();
   check(proofCutoff==nowTick&&visits["focus-end"]==1&&visits["focus"]==1,"admitted suspend retires isolation once");
   broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMSUSPEND,0);
   check(visits["focus-end"]==1&&visits["focus"]==1&&suspendQueued==1,"duplicate suspend cannot retire isolation again");
   broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMRESUMECRITICAL,0);
   broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMRESUMESUSPEND,0);
   check(visits["focus-end"]==1&&visits["focus"]==1&&g_focusLightProofNotBefore==proofCutoff,"wake and late interactive wake never clear a focus session or refresh its proof cutoff");
  }else if(selected=="argument-allocation"||selected=="normal"||selected=="journal"||selected=="capture"||selected=="device-log"||selected=="radial"||selected=="steam-close"||selected=="steam-interactive"||selected=="focus-observation"||selected=="msi-observation"||selected=="msi-completion"||selected=="failure-log"){
   g_powerLifecycle=PowerLifecycle::Suspended;g_sgInSuspend=true;g_sgGameActuallySuspended=true;frozen=true;
   if(selected!="normal")fault=selected=="failure-log"?"journal":selected;
   failFailureLog=selected=="failure-log";
   const auto prior=g_sleepPowerDispatch.observedSequence();
   bool escaped=false;try{broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMRESUMESUSPEND,0);}catch(...){escaped=true;}
   check(!escaped,"independent observer must not escape complete broadcast");
   check(!frozen&&g_powerLifecycle==PowerLifecycle::Ready,"actual primary wake branch must reach original resume owner");
   const auto receipt=g_sleepPowerDispatch.snapshot();
   check(receipt["consumerActive"]==false&&receipt["completed"]==prior+1,"receipt completes without active-consumer lock");
   if(selected=="msi-completion"&&!fixtureRawMsiCompletion)
    check(!faultHit,"removed MSI completion must not consume its old fault");
   else check(selected=="normal"||faultHit,"requested one-shot boundary must execute");
   check(visits["sensor-resume"]==0,"wake keeps original common owner rather than query-cancel sensor shortcut");
   check(visits["focus-end"]==0&&visits["focus"]==0&&g_focusLightProofNotBefore==0,"resume is not a focus isolation sleep boundary");
   check(visits["capture"]==1&&visits["device-log"]==1,"later independent observers execute once");
   check(visits["msi-completion"]==(fixtureRawMsiCompletion?1u:0u),"raw MSI completion matches the bound broadcast");
   check(visits["wallpaper"]==0&&visits["wallpaper-revoke"]==0,"raw resume does not queue wallpaper work or revoke its proof");
   if(selected=="radial"){
    const auto readyGeneration=currentPowerGeneration();
    // The synchronous worker port sets the phase only. Supply the same
    // committed Ready receipt that the original duplicate-entry cases use.
    g_resumeReadyGeneration=readyGeneration;
    const auto invalidations=mockRadialInvalidations,shutdowns=mockRadialShutdownCalls;
    fault.clear();g_radialOverlay.open=true;
    for(const auto edge:{PBT_APMRESUMEAUTOMATIC,PBT_APMRESUMESUSPEND,PBT_APMRESUMECRITICAL}){
     broadcast(g_hwnd,WM_POWERBROADCAST,edge,0);
     check(g_powerLifecycle==PowerLifecycle::Ready&&currentPowerGeneration()==readyGeneration,"late duplicate preserves the committed power episode");
     check(g_radialOverlay.active()&&mockRadialInvalidations==invalidations&&mockRadialShutdownCalls==shutdowns,"late duplicate cannot invalidate or close the newly opened wheel");
    }
    check(!g_sleepPowerDispatch.hasPending()&&g_sleepPowerDispatch.snapshot()["consumerActive"]==false,"late receipts return without holding delivery");
    // A real recovery discovered without a preceding Suspend creates a new
    // generation at the original common entry after the duplicate window.
    at(nowTick+6000);
    broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMRESUMESUSPEND,0);
    check(currentPowerGeneration()==readyGeneration+1&&!g_radialOverlay.active()&&mockRadialInvalidations==invalidations+1&&mockRadialShutdownCalls==shutdowns+1,"new recovery without a suspend retires the old wheel exactly once");
    g_radialOverlay.open=true;
    const auto wallpaperBefore=fixtureWallpaperOpen();
    at(nowTick+2000);
    broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMSUSPEND,0);
    check(currentPowerGeneration()==readyGeneration+2&&suspendQueued==1,"new shared suspend still owns one pause");
    check((fixtureWallpaperPublic?(visits["wallpaper-revoke"]==0&&fixtureWallpaperDeliverSuspend(wallpaperBefore+1)):
      visits["wallpaper-revoke"]==1)&&visits["wallpaper"]==0,"admitted suspend revokes wallpaper without scheduling a new read");
    check(!g_radialOverlay.active()&&mockRadialInvalidations==invalidations+2&&mockRadialShutdownCalls==shutdowns+2,"new shared suspend retires the active wheel exactly once");
    broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMSUSPEND,0);
    check(suspendQueued==1&&mockRadialInvalidations==invalidations+2&&mockRadialShutdownCalls==shutdowns+2,"duplicate suspend repeats neither pause nor wheel teardown");
    check(fixtureWallpaperPublic?(visits["wallpaper-revoke"]==0&&fixtureWallpaperDeliverSuspend(wallpaperBefore+1)):
      visits["wallpaper-revoke"]==1,"duplicate suspend cannot revoke a second time");
   }
  }else if(selected=="suspend-query-normal"||selected=="suspend-bare-normal"||selected=="suspend-query-notification-allocation"||selected=="suspend-bare-notification-allocation"||selected=="suspend-query-notification-allocation-double"){
   const bool query=selected.rfind("suspend-query-",0)==0;
   const bool inject=selected.find("allocation")!=std::string::npos;
   fault=selected;failFailureLog=selected=="suspend-query-notification-allocation-double";
   const auto before=currentPowerGeneration();const auto requestsBefore=requestCount;
   g_sgSleepQueryGateClosed=false;g_sgSleepQueryOwnsGeneration=false;
   bool escaped=false;try{if(query)broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMQUERYSUSPEND,0);else broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMSUSPEND,0);}catch(...){escaped=true;}
   check(!escaped,"suspending notification argument allocation must not escape original broadcast");
   check(faultHit==inject,"only selected real JSON argument allocation fails");
   check(currentPowerGeneration()==before+1&&suspendQueued==1,"original pause delivery is queued exactly once after the generation is owned");
   if(query){check(g_sgSleepQueryOwnsGeneration&&g_powerLifecycle==PowerLifecycle::Suspending,"query keeps original cancellable ownership");broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMSUSPEND,0);check(!g_sgSleepQueryOwnsGeneration&&g_powerLifecycle==PowerLifecycle::Suspended,"confirmation closes query ownership");}
   check(currentPowerGeneration()==before+1&&suspendQueued==1,"confirmation cannot replace or duplicate original queued pause");
   check(g_sleepPowerDispatch.snapshot()["consumerActive"]==false&&!g_sleepPowerDispatch.hasPending(),"all original receipts complete without an active delivery hold");
   check(requestCount==requestsBefore&&!sgSleepRetryActive(),"observer failure cannot request sleep or spend retry allowance");
   fault.clear();broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMRESUMESUSPEND,0);
   check(g_powerLifecycle==PowerLifecycle::Ready&&!frozen,"subsequent original wake can finish through common recovery owner");
   check(currentPowerGeneration()==before+1&&suspendQueued==1,"wake preserves exact pause generation and count");
  }else if(selected=="suspend-cpu-observer-failure"){
   fault="cpu-undervolt-stop";
   const auto before=currentPowerGeneration();
   g_powerSourceRecoveryRunning=true;
   bool escaped=false;
   try{broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMSUSPEND,0);}catch(...){escaped=true;}
   check(faultHit&&visits["cpu-undervolt-stop"]==1,"CPU trial cancellation reached the injected fault");
   check(!escaped,"CPU trial cancellation fault cannot escape the shared broadcast");
   check(visits["failure-log"]==1,"shared broadcast records the CPU trial cancellation failure once");
   check(!g_powerSourceRecoveryRunning&&visits["power-source-cancel"]==1,"power source recovery is retired independently");
   check(currentPowerGeneration()==before+1&&suspendQueued==1,"shared suspend owns exactly one generation and pause");
   check(!g_sleepPowerDispatch.hasPending()&&g_sleepPowerDispatch.snapshot()["consumerActive"]==false,"shared receipt is retired after the CPU observer fault");
   fault.clear();broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMRESUMECRITICAL,0);
   check(g_powerLifecycle==PowerLifecycle::Ready&&!frozen,"later critical wake reaches the shared Ready owner");
  }else if(selected=="suspend-foreground-queue-failure"){
   fault="msi-suspend-queue";
   const auto before=currentPowerGeneration();
   bool escaped=false;
   try{broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMSUSPEND,0);}catch(...){escaped=true;}
   check(faultHit&&isolationSuspendCalls==1&&isolationHold,"foreground suspend reached the queue fault after closing its private gate");
   check(!escaped,"foreground suspend queue fault cannot escape the shared broadcast");
   check(visits["failure-log"]==1,"shared broadcast records the foreground consumer failure once");
   check(currentPowerGeneration()==before+1&&suspendQueued==1,"shared suspend owns exactly one generation and pause");
   check(!g_sleepPowerDispatch.hasPending()&&g_sleepPowerDispatch.snapshot()["consumerActive"]==false,"shared receipt is retired after the foreground fault");
   broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMSUSPEND,0);
   check(isolationSuspendCalls==1&&suspendQueued==1,"duplicate suspend cannot replay the failed private observer or pause");
   fault.clear();broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMRESUMECRITICAL,0);
   check(g_powerLifecycle==PowerLifecycle::Ready&&!frozen,"later critical wake reaches the shared Ready owner");
  }else if(selected=="suspend-prepare"){
   fault="steam-prepare";g_sgSleepQueryGateClosed=false;g_sgSleepQueryOwnsGeneration=false;
   const auto before=currentPowerGeneration();bool escaped=false;try{broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMSUSPEND,0);}catch(...){escaped=true;}
   check(!escaped&&faultHit,"Steam prepare refusal cannot prevent original suspend admission");
   check(fixtureSteamPrepareBoundary==nowFile,"Steam preparation receives the original broadcast time even when its handler fails");
   check(currentPowerGeneration()==before+1&&suspendQueued==1,"confirmed suspend queues original pause exactly once");
   check(g_sleepPowerDispatch.snapshot()["consumerActive"]==false,"suspend receipt completes");
   // A late query rejection does not own the confirmed suspend or its capture.
   fault.clear();
   const auto aborts=abortCount;
   const auto suspendGeneration=currentPowerGeneration();
   const auto taskGeneration=g_sgTask.generation;
   const auto trigger=g_sgSleepTriggerTick;
   const auto sensorPauses=fixtureSensorPosts;
   const auto gateCalls=suspendLeafCalls;
   const auto repairEligible=g_sgRepairEligible;
   suspendConfiguredMode=SgSleepMode::S4;
   for(const auto phase:{PowerLifecycle::Suspending,PowerLifecycle::Suspended,PowerLifecycle::Resuming}){
    g_powerLifecycle=phase;
    at(nowTick+10);broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMQUERYSUSPEND,0);
    check(g_sgTask.mode==SgSleepMode::S3&&g_sgTask.generation==taskGeneration&&
          g_sgRepairEligible==repairEligible&&g_sgSleepTriggerTick==trigger,
          "late S4-configured query replaced the confirmed S3 task");
    check(g_powerLifecycle==phase&&currentPowerGeneration()==suspendGeneration&&suspendQueued==1&&
          !g_sgSleepQueryGateClosed&&!g_sgSleepQueryOwnsGeneration,
          "late query acquired a new gate or displaced current suspend work");
    check(fixtureSensorPosts==sensorPauses&&!g_inputCaptureSensorsSuspended&&suspendLeafCalls==gateCalls,
          "late query paused sensors or touched the hardware gate");
    check(!g_sleepPowerDispatch.pendingSuspend()&&!g_sleepPowerDispatch.hasPending()&&
          g_sleepPowerDispatch.snapshot()["consumerActive"]==false,
          "ignored query releases its ordered receipt and pending fence");
   }
   g_powerLifecycle=PowerLifecycle::Suspending;suspendConfiguredMode=SgSleepMode::S3;
   // The 506 owner may join this accepted suspend before the original worker
   // runs. The scalar phase stays Suspending in that interleaving.
   for(const bool modern:{true,false})for(const bool retry:{false,true})for(const auto code:{PBT_APMQUERYSUSPENDFAILED,PBT_APMQUERYSTANDBYFAILED}){
    g_sgModernStandbyActive=modern;g_sgModernStandbyGeneration=modern?suspendGeneration:0;g_sgRetryEntryFailure=retry;
    at(nowTick+10);broadcast(g_hwnd,WM_POWERBROADCAST,code,0);
    check(abortCount==aborts&&g_powerLifecycle==PowerLifecycle::Suspending&&g_sgModernStandbyActive==modern,
          "query failure cannot cancel the confirmed suspend, including with a joined 506");
    check(!sgSleepRetryActive()&&currentPowerGeneration()==suspendGeneration&&suspendQueued==1,
          "query failure cannot start another sleep attempt for a confirmed suspend");
    check(fixtureQueryTimeoutStops==0,"query failure cannot stop the confirmed suspend timeout owner");
    check(visits["steam-cancel"]==0,"unowned query failure revoked the confirmed Steam capture");
    check(visits["sensor-resume"]==0,"unowned query failure invoked sensor recovery outside public Ready");
    check(!g_sleepPowerDispatch.hasPending()&&g_sleepPowerDispatch.snapshot()["consumerActive"]==false,
          "the non-authoritative query failure releases its public receipt");
   }
   broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMRESUMECRITICAL,0);
   check(g_powerLifecycle==PowerLifecycle::Ready&&visits["steam-interactive"]==1,
         "the following critical wake keeps the original Steam and shared recovery paths reachable");
   g_sgRetryEntryFailure=false;
   at(nowTick+2000);broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMQUERYSUSPEND,0);
   check(g_sgSleepQueryOwnsGeneration&&currentPowerGeneration()==suspendGeneration+1,
         "a later unconfirmed query owns a new generation");
   at(nowTick+10);broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMQUERYSUSPENDFAILED,0);
   check(abortCount==aborts+1&&g_powerLifecycle==PowerLifecycle::Ready&&fixtureQueryTimeoutStops==1,
         "an owned query still cancels to Ready and retires the unconfirmed timeout observation");
  }else throw std::runtime_error("unknown exact scenario");
  std::cout<<json{{"scenario",selected},{"assertions",assertions},{"faultHit",faultHit},{"visits",visits},{"completeActualBroadcast",selected.rfind("common-",0)!=0},{"completeActualCommonEntry",true},{"queueCalls",fixtureWakeQueueCalls},{"queueGeneration",fixtureWakeQueueGeneration},{"queueKind",fixtureWakeQueueKind},{"lifecycle",static_cast<int>(g_powerLifecycle.load())},{"frozen",frozen},{"osSleepDeviceOrUserProcesses",false}}.dump()<<'\n';return 0;
 }catch(const std::exception& e){std::cout<<json{{"scenario",selected},{"assertions",assertions},{"failed",e.what()},{"faultHit",faultHit},{"suspendQueued",suspendQueued},{"sensorCancels",fixtureSensorCancels},{"sensorPosts",fixtureSensorPosts},{"sensorBindings",fixtureSensorBindings},{"sensorResets",fixtureSensorResets},{"recordCalls",fixtureActualRecordCalls},{"monitorArmed",g_monitorEntryFailureTimerArmed},{"classificationGeneration",g_sgResumeClassification.generation.load()},{"queueCalls",fixtureWakeQueueCalls},{"queueGeneration",fixtureWakeQueueGeneration},{"lifecycle",static_cast<int>(g_powerLifecycle.load())},{"frozen",frozen},{"nativeQueueSize",g_sgWorkQ.size()},{"nativeWakeRevision",g_sgWorkWakeRevision},{"explicitWakeGeneration",g_sgLastQueuedGameWake.generation},{"pauseFence",g_sgLastQueuedGameWake.wakeManualPauseFence},{"visits",visits},{"power",g_sleepPowerDispatch.snapshot()}}.dump()<<'\n';return 1;}
}
'''
    with (c['out']/'broadcast-consumer-bindings.json').open('x',encoding='utf-8') as output:
        json.dump({'actualBroadcast':body,'sha256':hashlib.sha256(body.encode()).hexdigest(),'completeOriginalCase':True,'primaryPredicatesUnchanged':True,'leafScope':'inert OS/device/observer; original scalar common owner and classifier; not actual Steam or Win5'},output,indent=2)
    if (c['args'].broadcast_consumer_case or '').startswith('gate-query-'):
        return bind_write_gate(c,p,f,actual,checks)
    return p,f,actual,checks


def bind_write_gate(c,p,f,h,checks):
    from pathlib import Path
    import re,json,hashlib
    source=c['source']; root=c['args'].source.parent
    def function(name,text=source,inline=False):
        mask=c['mask_cpp'](text)
        prefix=r'\binline\s+' if inline else r'\bstatic\s+'
        matches=list(re.finditer(prefix+r'[^;{}]*?\b'+re.escape(name)+r'\s*\([^;{}]*?\)\s*(?:noexcept\s*)?\{',mask))
        if len(matches)!=1:raise RuntimeError('gate exact function '+name+':'+str(len(matches)))
        a=matches[0].start();return text[a:c['braced'](mask.index('{',a),mask)]
    def struct(name):
        mask=c['mask_cpp'](source);matches=list(re.finditer(r'\bstruct\s+'+name+r'\s*\{',mask))
        if len(matches)!=1:raise RuntimeError('gate exact type '+name)
        a=matches[0].start();return source[a:c['braced'](mask.index('{',a),mask)+1]
    gate=function('closeHardwareWriteGate');revoke=function('revokeSharedPowerMirrors')
    gate_observations=[line for line in source.splitlines() if re.fullmatch(r'static std::atomic<ULONGLONG> g_focusLightSuspendProofAfter\{0\};',line)]
    if len(gate_observations)!=1:raise RuntimeError('gate focus suspend cutoff identity')
    inc=(root/'sleep_static_wake_schedule_native.inc').read_text(encoding='utf-8-sig')
    scan=function('sgStaticScanInvalidate',inc)+'\n'+function('sgStaticScanCancel',inc)
    battery=(root/'battery_charge_client.h').read_text(encoding='utf-8-sig')
    managed=(root/'battery_managed_runtime.h').read_text(encoding='utf-8-sig')
    managed_parts='\n'.join(function(n,managed,True) for n in ['publishSystemPowerNotification','notifySystemPower','sealSystemPowerNotifications'])
    battery_parts='\n'.join(function(n,battery,True) for n in ['stopping','bindSystemPowerReader','notifySystemPower'])
    marker='void closeHardwareWriteGate(const char*){++suspendLeafCalls;}'
    if p.count(marker)!=1:raise RuntimeError('gate original port missing')
    p=p.replace(marker,'static void closeHardwareWriteGate(const char*);')
    constants='\n'.join(line for line in source.splitlines() if line.startswith('static constexpr ') and any(name in line for name in ['kMsiClawVendorId','kMsiClawPidDInput','kMsiDinputFeedbackReportLength','kMsiDinputFeedbackReportId']))
    scan_message=[line for line in source.splitlines() if line.startswith('static constexpr ') and re.search(r'\bWM_SG_STATIC_SCAN\s*=',line)]
    if len(scan_message)!=1:raise RuntimeError('gate static scan message identity')
    declaration=r'''
#include "touch_feedback_lease.h"
#include <memory>
#include <thread>
#include <condition_variable>
static unsigned fixtureGateRevokes=0,fixtureGateMirrorRevokes=0,fixtureGatePosts=0,fixtureGateBatteryNotes=0;
struct FixtureGateRevoke{unsigned* counter;void revoke()noexcept{++*counter;}};

static std::atomic<ULONGLONG> g_sgStaticScanRevision{1};
static BOOL fixtureGatePostMessage(HWND,UINT,WPARAM,LPARAM){++fixtureGatePosts;return TRUE;}
namespace ymcc::batterycharge::managed {
using PowerNotify=void(__cdecl*)();
inline std::mutex systemPowerNotificationGate;
inline PowerNotify systemPowerNotification=nullptr;
inline bool systemPowerNotificationsSealed=false;
MANAGED_PARTS
}
namespace ymcc::batterycharge {
using SystemPowerReader=int(__cdecl*)(ymcc::sleeppower::SharedPowerV1*,std::uint32_t);
inline std::atomic<SystemPowerReader> systemPowerReader{nullptr};
BATTERY_PARTS
}
static void fixtureGateBatteryNote(){++fixtureGateBatteryNotes;fixtureClassificationBatteryNotice();}
'''.replace('MANAGED_PARTS',managed_parts).replace('BATTERY_PARTS',battery_parts)
    native=constants+'\n'+struct('HidHideMsiMainSourceRecord')+'\n'+struct('MsiClawHidDiag')+'\n'+function('msiDinputBuildFeedbackFrame')+'\n'+function('msiDinputBuildFeedbackFrameForLength')+r'''
static HANDLE msiDinputOpenMainGameHidOutput(const std::string&,MsiClawHidDiag&){throw std::runtime_error("unselected real HID open");}
'''+struct('MsiFeedbackLeaseOps')+r'''
static std::mutex g_msiFeedbackLeaseMx;
static std::atomic<bool> g_msiFeedbackLeaseInvalidated{false};
static ymcc::touchhaptic::FeedbackLease<MsiFeedbackLeaseOps> g_msiFeedbackLease;
static unsigned fixtureGateReturnedResources=0,fixtureGateOrphanCleanup=0;
static bool fixtureGateRetainedAtClose=false;
static unsigned fixtureGateClosesAtClose=0;
static bool fixtureGateEventAliveAtClose=false,fixtureGateHandleAliveAtClose=false;
static HANDLE fixtureGateEvent=nullptr,fixtureGateHandle=nullptr;
static bool fixtureGateHandleAlive(HANDLE h){DWORD flags=0;return h&&GetHandleInformation(h,&flags)!=FALSE;}
struct FixtureGateCleanup {
 ~FixtureGateCleanup(){
  for(auto h:{fixtureGateEvent,fixtureGateHandle})if(fixtureGateHandleAlive(h))SetHandleInformation(h,HANDLE_FLAG_PROTECT_FROM_CLOSE,0);
  (void)g_msiFeedbackLease.invalidate();
  for(auto h:{fixtureGateEvent,fixtureGateHandle})if(fixtureGateHandleAlive(h)){if(CloseHandle(h))++fixtureGateOrphanCleanup;}
 }
};
'''+function('msiDinputInvalidateFeedbackLease')+'\n'+scan_message[0]+'\n#define PostMessageW fixtureGatePostMessage\n'+scan+'\n#undef PostMessageW\n'+gate+'\n'
    battery_port='''namespace ymcc::batterycharge {void notifySystemPower(){
 const auto notices=++classificationBatteryNotices;
 auto* interleave=classificationPublicationInterleave;
 if(interleave&&notices==interleave->secondBattery)interleave->secondPublication.count_down();
}}'''
    if p.count(battery_port)!=1:raise RuntimeError('exact gate battery observation port')
    p=p.replace(battery_port,battery_port.replace('namespace ymcc::batterycharge {void notifySystemPower(){',
        'static void fixtureClassificationBatteryNotice(){')[:-1]+'\nnamespace ymcc::batterycharge {inline void notifySystemPower() noexcept;}')
    p+='\n'+declaration+'\n'+gate_observations[0]+'\n'+native+'\n'
    cases=r'''
  if(selected.rfind("gate-query-",0)==0){
   if(!(selected=="gate-query-normal"||selected=="gate-query-event-refused"||selected=="gate-query-handle-refused"||selected=="gate-query-both-refused"||selected=="gate-query-contended"||selected=="gate-query-repeat-refused"||selected=="gate-query-reopen-refused"))throw std::runtime_error("unknown exact write gate scenario");
   keepInput=false;g_inputCaptureReady=false;g_sensorActivePower.store({});
   check(ymcc::batterycharge::bindSystemPowerReader(&readSharedSystemPower),"original startup battery reader binding succeeds");
   ymcc::batterycharge::managed::publishSystemPowerNotification(&fixtureGateBatteryNote);
   json reports=json::array();MsiFeedbackLeaseOps::fixtureWrites=&reports;
   ymcc::touchhaptic::SourceToken key{ymcc::touchhaptic::Backend::Msi,1,3,currentPowerGeneration(),4};
   HidHideMsiMainSourceRecord spec;auto receipt=g_msiFeedbackLease.write(key,spec,0,0,[]{return true;});
   check(receipt.ok&&g_msiFeedbackLease.resource(),"original MSI fixture opens exact lease without hardware APIs");
   auto* original=g_msiFeedbackLease.resource();
   fixtureGateEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);fixtureGateHandle=CreateEventW(nullptr,TRUE,FALSE,nullptr);
   check(fixtureGateEvent&&fixtureGateHandle,"both real owned Win32 event handles created");
   original->event=fixtureGateEvent;original->handle=fixtureGateHandle;
   FixtureGateCleanup cleanup;
   const bool refuseEvent=selected=="gate-query-event-refused"||selected=="gate-query-both-refused"||selected=="gate-query-repeat-refused"||selected=="gate-query-reopen-refused";
   const bool refuseHandle=selected=="gate-query-handle-refused"||selected=="gate-query-both-refused"||selected=="gate-query-repeat-refused"||selected=="gate-query-reopen-refused";
   const bool contended=selected=="gate-query-contended",pending=false;
   if(refuseEvent)check(SetHandleInformation(fixtureGateEvent,HANDLE_FLAG_PROTECT_FROM_CLOSE,HANDLE_FLAG_PROTECT_FROM_CLOSE)!=FALSE,"protect exact owned event close");
   if(refuseHandle)check(SetHandleInformation(fixtureGateHandle,HANDLE_FLAG_PROTECT_FROM_CLOSE,HANDLE_FLAG_PROTECT_FROM_CLOSE)!=FALSE,"protect exact owned feedback handle close");
   if(pending){original->pending=true;original->cancelled=true;}
   std::mutex peerMx;std::condition_variable peerCv;bool locked=false,release=false;
   std::thread holder;
   if(contended){holder=std::thread([&]{std::unique_lock<std::mutex> lease(g_msiFeedbackLeaseMx);{std::lock_guard<std::mutex> lk(peerMx);locked=true;}peerCv.notify_one();std::unique_lock<std::mutex> lk(peerMx);peerCv.wait(lk,[&]{return release;});});std::unique_lock<std::mutex> lk(peerMx);peerCv.wait(lk,[&]{return locked;});}
   const auto oldGeneration=currentPowerGeneration(),oldSerial=g_sharedPowerSuspendSerial.load(),oldScan=g_sgStaticScanRevision.load();
   const auto oldCloses=g_msiFeedbackLease.stats.closes;bool escaped=false;
   try{broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMQUERYSUSPEND,0);}catch(...){escaped=true;}
   if(contended){{std::lock_guard<std::mutex> lk(peerMx);release=true;}peerCv.notify_one();holder.join();}
   fixtureGateRetainedAtClose=g_msiFeedbackLease.resource()==original;
   fixtureGateClosesAtClose=static_cast<unsigned>(g_msiFeedbackLease.stats.closes-oldCloses);
   fixtureGateEventAliveAtClose=fixtureGateHandleAlive(fixtureGateEvent);fixtureGateHandleAliveAtClose=fixtureGateHandleAlive(fixtureGateHandle);
   check(!escaped,"actual write gate cannot interrupt shared query delivery");
   check(currentPowerGeneration()==oldGeneration+1&&suspendQueued==1,"original query generation and pause delivered exactly once");
   check(!g_hardwareWriteGate&&g_sharedPowerSuspendSerial==oldSerial+1,"original gate closes and invalidation serial advances once");
   check(g_sgStaticScanRevision==oldScan+1&&fixtureGatePosts==1,"actual static scan cancel invalidates and posts once");
   check(g_recoveryPowerBoundary.revocations>=1&&g_sharedPowerRevocations.revocations==g_recoveryPowerBoundary.revocations&&fixtureGateBatteryNotes>=1,"actual gate dispatches both revocations and warmed battery notifier");
   if(refuseEvent||refuseHandle){
    check(fixtureGateEventAliveAtClose==refuseEvent&&fixtureGateHandleAliveAtClose==refuseHandle,"real CloseHandle protection rejects only selected owned handles");
    check(fixtureGateRetainedAtClose&&fixtureGateClosesAtClose==0,"failed CloseHandle must retain original feedback lease");
    if(selected=="gate-query-reopen-refused"){auto nextKey=key;++nextKey.generation;const auto priorOpens=g_msiFeedbackLease.stats.opens,priorWrites=g_msiFeedbackLease.stats.writes;const auto blocked=g_msiFeedbackLease.write(nextKey,spec,0,0,[]{return true;});check(!blocked.ok&&!blocked.attempted&&blocked.reason==ymcc::touchhaptic::Reason::IoPending,"new-source safety stop cannot bypass retained close debt");check(g_msiFeedbackLease.resource()==original&&g_msiFeedbackLease.stats.opens==priorOpens&&g_msiFeedbackLease.stats.writes==priorWrites,"new-source demand cannot open replacement or replay feedback while close is refused");}
    if(selected=="gate-query-repeat-refused"){for(unsigned repeat=0;repeat<8;++repeat)msiDinputInvalidateFeedbackLease();check(g_msiFeedbackLease.resource()==original&&g_msiFeedbackLease.stats.closes==oldCloses&&fixtureGateHandleAlive(fixtureGateEvent)&&fixtureGateHandleAlive(fixtureGateHandle),"repeated refused invalidation retains same single resource without writes");}
    check((refuseEvent?original->event==fixtureGateEvent:original->event==nullptr)&&(refuseHandle?original->handle==fixtureGateHandle:original->handle==INVALID_HANDLE_VALUE),"successful partial closes clear only their exact original fields");
   }else if(contended||pending){
    check(fixtureGateRetainedAtClose&&fixtureGateEventAliveAtClose&&fixtureGateHandleAliveAtClose&&fixtureGateClosesAtClose==0,"contended or pending lease remains owned without premature close");
    if(contended)check(g_msiFeedbackLeaseInvalidated,"nonblocking UI cancellation leaves writer invalidation set");
   }else check(!g_msiFeedbackLease.resource()&&!fixtureGateEventAliveAtClose&&!fixtureGateHandleAliveAtClose&&fixtureGateClosesAtClose==1,"normal gate fully retires exact original feedback resources");
   if(refuseEvent)SetHandleInformation(fixtureGateEvent,HANDLE_FLAG_PROTECT_FROM_CLOSE,0);
   if(refuseHandle)SetHandleInformation(fixtureGateHandle,HANDLE_FLAG_PROTECT_FROM_CLOSE,0);
   if(pending)original->pending=false;
   msiDinputInvalidateFeedbackLease();
   check(!g_msiFeedbackLease.resource()&&!fixtureGateHandleAlive(fixtureGateEvent)&&!fixtureGateHandleAlive(fixtureGateHandle),"next existing invalidation returns retained feedback resources");
   check(g_msiFeedbackLease.stats.closes==oldCloses+1,"resource completion counted once across partial close and retry");
   const auto writesBefore=g_msiFeedbackLease.stats.writes;
   broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMSUSPEND,0);
   check(suspendQueued==1&&currentPowerGeneration()==oldGeneration+1,"confirmation preserves original admitted pause");
   broadcast(g_hwnd,WM_POWERBROADCAST,PBT_APMRESUMECRITICAL,0);
   check(g_powerLifecycle==PowerLifecycle::Ready&&!frozen,"later original shared wake completes with no feedback self lock");
   check(g_msiFeedbackLease.stats.writes==writesBefore,"cleanup and wake never replay prior feedback output");
   check(!g_sleepPowerDispatch.hasPending()&&g_sleepPowerDispatch.snapshot()["consumerActive"]==false,"public receipt ownership fully returns");
   ymcc::batterycharge::managed::sealSystemPowerNotifications();MsiFeedbackLeaseOps::fixtureWrites=nullptr;
  }else
'''
    if checks.count('  if(selected.rfind("sensor-query-",0)==0){')!=1:raise RuntimeError('gate case insertion')
    checks=checks.replace('  if(selected.rfind("sensor-query-",0)==0){',cases+'  if(selected.rfind("sensor-query-",0)==0){',1)
    for marker in ['{"assertions",assertions},{"faultHit",faultHit}', '{"assertions",assertions},{"failed",e.what()}']:
        if checks.count(marker)!=1:raise RuntimeError('gate result fields')
        checks=checks.replace(marker,marker+',{"gateBatteryNotes",fixtureGateBatteryNotes},{"gateRevocations",g_recoveryPowerBoundary.revocations},{"gateMirrorRevocations",g_sharedPowerRevocations.revocations},{"gateRetainedAtClose",fixtureGateRetainedAtClose},{"gateClosesAtClose",fixtureGateClosesAtClose},{"gateEventAliveAtClose",fixtureGateEventAliveAtClose},{"gateHandleAliveAtClose",fixtureGateHandleAliveAtClose},{"gateOrphanFixtureCleanup",fixtureGateOrphanCleanup},{"gateResourceRemaining",g_msiFeedbackLease.resource()!=nullptr}')
    with (c['out']/'actual-write-gate-bindings.json').open('x',encoding='utf-8') as output:
        json.dump({'gate':gate,'revoke':revoke,'staticScanCancel':scan,'msiLeaseOps':struct('MsiFeedbackLeaseOps'),'msiInvalidate':function('msiDinputInvalidateFeedbackLease'),'managedNotification':managed_parts,'clientNotification':battery_parts,'completeActualGateAndMsiResourceOwner':True,'realCloseBoundary':'Two fixture-owned Win32 events;exact protect-from-close failures;no real HID open/write,device,process control or OS sleep','remainingPorts':'Revocation consumer objects counted;static-scan PostMessage counted;managed callback counted;nonfixture HID open refuses;no provider lifetime or CLR execution claim'},output,indent=2)
    return p,f,h,checks
