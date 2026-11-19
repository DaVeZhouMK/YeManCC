"""Directed extension of sleep.non-user-wake-consumer; actual source bodies.
Only terminal clocks, timers, queues, publication and exact-PID transport are
memory ports. No classification predicate or common cleanup is reimplemented.
"""
from pathlib import Path
import hashlib,json,re

def bind_shared_publication(c, p, f):
    """Compile the actual queued publisher; control only external end points.

    The original joined sender selection owns its separate public Reader and
    terminal composition. Do not add a second publisher to that existing view.
    """
    if getattr(c['args'], 'resume_classification_sender_only', False):
        return p, f, None
    source = c['source']
    if 'publishSharedPowerLifecycle(' not in (Path(c['args'].source).parent/'sleep_resume_classification_native.inc').read_text(encoding='utf-8-sig'):
        return p, f, None
    actual = c['function']('publishSharedPowerLifecycle')
    # The existing function() parser accepts ordinary signatures only.
    # Use its original mask/braced helpers for this exact noexcept signature.
    masked = c['mask_cpp'](source)
    marker = 'static void revokeSharedPowerMirrors() noexcept {'
    assert masked.count(marker) == 1, 'actual mirror revocation signature not unique'
    start = masked.index(marker)
    revocation = source[start:c['braced'](masked.index('{', start), masked)]
    assert 'g_recoveryPowerBoundary.revoke();' in revocation and 'g_sharedPowerRevocations.revoke();' in revocation
    assert 'void sleepPowerArmRedelivery(HWND)' in p, 'original redelivery endpoint missing'
    assert 'g_sharedPowerPublicationRevision.fetch_add(1,std::memory_order_acq_rel)' in actual
    assert 'g_sharedPowerProjectionPending.exchange(true,std::memory_order_acq_rel)' in actual
    assert 'PostMessageW(g_hwnd,kSharedPowerStateMessage,0,0)' in actual
    assert 'g_sharedPowerPublicationRevision' not in p
    ports = r'''
#define YMCC_FIXTURE_ACTUAL_SHARED_PUBLICATION 1
static std::atomic<uint64_t> g_sharedPowerPublicationRevision{0};
static std::atomic<bool> g_sharedPowerProjectionPending{false};
static unsigned classificationPublicationPosts=0,classificationBatteryNotices=0;
struct ClassificationMirrorPort {unsigned revocations=0;void revoke() noexcept {++revocations;}};
static ClassificationMirrorPort g_recoveryPowerBoundary,g_sharedPowerRevocations;
static void revokeSharedPowerMirrors() noexcept;
namespace ymcc::batterycharge {void notifySystemPower(){++classificationBatteryNotices;}}
static void sharedPowerProjectionConsumerFailed(const char* name){sgRecordFact("fixture-shared-publication-consumer-failed",{{"consumer",name?name:"unknown"}});}
static BOOL classificationPublicationPostMessage(HWND window,UINT message,WPARAM,LPARAM){
 if(window!=g_hwnd||message!=kSharedPowerStateMessage)throw std::runtime_error("unadmitted-shared-publication-post");
 ++classificationPublicationPosts;return TRUE;
}
static void publishSharedPowerLifecycle(const char*);
'''
    if 'if(ymcc::sleeppower::task::allowed())notifySharedMediaDownloadsPower();' in actual:
        ports += r'''
// Media task ownership is outside this fixture. A wake projection cannot run it here.
namespace ymcc::sleeppower::task {inline bool allowed() noexcept {return false;}}
static void notifySharedMediaDownloadsPower(){throw std::logic_error("unselected media publication reached");}
'''
    wrapped = '\n#pragma push_macro("PostMessageW")\n#undef PostMessageW\n#define PostMessageW classificationPublicationPostMessage\n'+actual+'\n#pragma pop_macro("PostMessageW")\n'
    wrapped = revocation+'\n'+wrapped
    binding = {'source':'native/main.cpp', 'actualPublisher':actual,
               'actualMirrorRevocation':revocation,
               'actualMirrorRevocationSha256':hashlib.sha256(revocation.encode()).hexdigest(),
               'mirrorTerminals':'two counted revoke endpoints; no registry, mapping, observers or cross-process execution',
               'redeliveryTerminal':'reuse original notificationRedeliveryArms endpoint',
               'actualPublisherSha256':hashlib.sha256(actual.encode()).hexdigest(),
               'bodyRewritten':False, 'osPort':'PostMessageW macro only around the exact publisher',
               'batteryNotification':'counted endpoint only', 'projectedConsumersExecuted':False,
               'newPowerGenerationOrActionBudget':False}
    c['sharedPowerPublicationBinding'] = binding
    return p+ports, wrapped+f, binding


def bind_entry_observation_support(c, p, f, native):
    "Bind actual operation mutex/prototype/handoff; only external endpoints are ports."
    source=c['source'];function=c['function']
    def once(value,before,after):
        assert value.count(before)==1, 'entry support anchor not unique: '+before[:90]
        return value.replace(before,after)
    start=[line for line in source.splitlines() if re.fullmatch(r'static void sgStartSleepRetry\([^;{}]*\);',line)]
    assert len(start)==1
    p=once(p,'static void sgStartSleepRetry(SgRetryKind,const char*);',start[0])
    stop=[line for line in source.splitlines() if re.fullmatch(r'static void sgStopSleepRetry\([^;{}]*\);',line)]
    assert len(stop)==1, 'actual stop retry forward declaration not unique'
    p=once(p,'static void sgStopSleepRetry();',stop[0])
    binding={'operationMutex':'','stopRetryDeclaration':stop[0],'actualHandoffFunctions':[],
             'mutexThreadScope':'real mutex; only explicit busy case creates one owned other thread'}
    if 'g_sgRetryWindowsSuccessAtAdmission' in f+native:
        declarations=[line for line in source.splitlines() if line in (
            'static ULONGLONG g_sgRetryWindowsSuccessAtAdmission = 0;',
            'static ymcc::sleeprequest::CompletionWitness g_sgRetryWindowsSuccessAtAdmission;')]
        assert len(declarations)==1 and 'g_sgRetryWindowsSuccessAtAdmission' not in p
        p=once(p,'using json = nlohmann::json;','using json = nlohmann::json;\n'+declarations[0]+'\n')
        binding['successCancellationWitness']=declarations[0]
    if 'g_sgOpMtx' in f+native:
        declarations=[line for line in source.splitlines() if re.fullmatch(r'static std::mutex g_sgOpMtx;[ \t]*(?://[^\r\n]*)?',line)]
        assert len(declarations)==1 and 'g_sgOpMtx' not in p, 'operation mutex source/declaration changed'
        p+='\n'+declarations[0]+'\n';binding['operationMutex']=declarations[0]
    if re.search(r'^static void sgRequestEntryObservationHandoff\(\)\s*\{',source,re.M):
        pending=[line for line in source.splitlines() if re.fullmatch(r'static std::atomic<bool> g_sgEntryHandoffPending\{false\};',line)]
        assert len(pending)==1, 'actual pending handoff atomic declaration changed'
        work=[line for line in source.splitlines() if re.fullmatch(r'static std::mutex g_sgWorkMx;[ \t]*(?://[^\r\n]*)?',line)]
        assert len(work)==1 and 'g_sgWorkMx' not in p, 'actual queue mutex declaration changed'
        p+='\n'+work[0]+'\n';binding['workMutex']=work[0]
        ready=[line for line in source.splitlines() if re.fullmatch(r'static std::atomic<bool> g_sgEntryHandoffReady\{false\};',line)]
        if ready:
            assert len(ready)==1, 'actual Ready handoff atomic declaration not unique'
            p+='\n#define YMCC_FIXTURE_ENTRY_HANDOFF_READY_ENABLE 1\n'+ready[0]+'\n';binding['readyDeclaration']=ready[0]
        request=function('sgRequestEntryObservationHandoff');service=function('sgServiceEntryObservationHandoff')
        p+='\n#define YMCC_FIXTURE_ENTRY_HANDOFF_ENABLE 1\n#include <thread>\n#include <condition_variable>\n#include <chrono>\n'+pending[0]+r'''
static void sgRequestEntryObservationHandoff();
static bool sgServiceEntryObservationHandoff();
static unsigned fixtureEntryHandoffNotifies=0,fixtureEntryHandoffPostAttempts=0,fixtureEntryHandoffLockProbes=0;
static bool fixtureEntryHandoffPostAccepted=true,fixtureEntryHandoffProbeLocks=false;
static bool fixtureEntryHandoffOpAvailable=false,fixtureEntryHandoffWorkUnavailable=false;
struct FixtureEntryHandoffDelivery {HWND window;UINT message;WPARAM timer;LPARAM payload;};
static std::vector<FixtureEntryHandoffDelivery> fixtureEntryHandoffDeliveries;
static void sgNotifyWork(){++fixtureEntryHandoffNotifies;}
static BOOL fixtureEntryHandoffPost(HWND window,UINT message,WPARAM timer,LPARAM payload){
 if(window!=g_hwnd||message!=WM_TIMER||timer!=SG_RETRY_TIMER_ID||payload!=0)throw std::runtime_error("unexpected entry handoff delivery route");
 ++fixtureEntryHandoffPostAttempts;
 if(fixtureEntryHandoffProbeLocks){
  std::thread probe([]{
   fixtureEntryHandoffOpAvailable=g_sgOpMtx.try_lock();if(fixtureEntryHandoffOpAvailable)g_sgOpMtx.unlock();
   const bool workAvailable=g_sgWorkMx.try_lock();if(workAvailable)g_sgWorkMx.unlock();
   fixtureEntryHandoffWorkUnavailable=!workAvailable;
  });probe.join();++fixtureEntryHandoffLockProbes;
 }
 if(!fixtureEntryHandoffPostAccepted)return FALSE;
 fixtureEntryHandoffDeliveries.push_back({window,message,timer,payload});return TRUE;
}
'''
        f+='\n'+request+'\n#pragma push_macro("PostMessageW")\n#undef PostMessageW\n#define PostMessageW fixtureEntryHandoffPost\n'+service+'\n#pragma pop_macro("PostMessageW")\n'
        binding.update({'pendingDeclaration':pending[0],'actualHandoffFunctions':[request,service],
                        'handoffBodiesRewritten':False,'sgNotifyWorkPort':'count only; never starts worker',
                        'PostMessagePort':'capture actual WM_TIMER/SG_RETRY_TIMER_ID; never calls OS UI'})
    return p,f,binding

def prepare(c):
    p,f,h,old=c['preamble'],c['functions'],c['handlers'],c['checks']
    source,mask,braced,function=c['source'],c['mask_cpp'],c['braced'],c['function']
    def replace_function(text,name,replacement):
        m=mask(text);hits=list(re.finditer(r'(?m)^(?:static\s+)?[^;{}\n]*\b'+name+r'\s*\([^;{}]*\)\s*\{',m))
        assert len(hits)==1,(name,len(hits));a=hits[0].start();o=m.index('{',a);b=braced(o,m)
        return text[:a]+replacement+text[b:]
    def body(text,name):
        m=mask(text);hits=list(re.finditer(r'(?m)^(?:static\s+)?[^;{}\n]*\b'+name+r'\s*\([^;{}]*\)\s*\{',m))
        assert len(hits)==1,name;a=hits[0].start();o=m.index('{',a);return text[a:braced(o,m)]
    root=Path(c['args'].source).parent
    inc=root/'sleep_resume_classification_native.inc';new=inc.exists()
    native=inc.read_text(encoding='utf-8-sig') if new else ''
    timeout=(root/'sleep_timeout_native.inc').read_text(encoding='utf-8-sig')
    kernel=(root/'sleep_kernel_cycle_native.inc').read_text(encoding='utf-8-sig')
    assert timeout.count('#include "sleep_kernel_cycle_native.inc"')==1
    timeout=timeout.replace('#include "sleep_kernel_cycle_native.inc"',kernel)
    p=p.replace('static ymcc::sleeppower::KernelCycleObservations g_sgKernelCycles;\n','')
    p,f,entry_support=bind_entry_observation_support(c,p,f,native)
    # Exact source declarations, including the production timer value.
    declarations=[]
    if new:
        for line in source.splitlines():
            if line.startswith('static ') and any(key in line for key in ['sharedSleepWakeClassified','sgBlockSharedWakeClassification','sgCompleteSharedWakeClassification','sgResumeClassification','sgStopResumeClassification','sgFinishResumeClassification','sgDeferModernResume','sgJoinPendingResumeEntry','sgEntryFailureLifecycleAllowed','sgIgnorePowerReturnBeforeCurrentIntent','sgResumeEntryObservation','sgRevokeResumeEntryObservation','sgJoinResumeEntryObservation','sgConsumeEntryObservationHandoffReady','sgCancelRetryAdmission','sgDeferRetryAdmission','sgResumeRetryAdmission','sgRetryAdmissionHeld','sgSleepTimeoutSuccessfulExitTime','SG_RESUME_CLASSIFY_TIMER_ID']):
                if line.rstrip().endswith(';'):declarations.append(line)
    if not new:declarations=['static constexpr UINT_PTR SG_RESUME_CLASSIFY_TIMER_ID=0xA216;']
    p=p.replace('#include "sleep_retry_budget.h"','#include "sleep_timeout_policy.h"\n#include "sleep_retry_budget.h"')
    p=p.replace('using json = nlohmann::json;','using json = nlohmann::json;\n'+'enum class SgRetryKind : uint8_t;\n'+'\n'.join(declarations))
    p=p.replace('struct SgSleepTarget {bool markerOwned=false;unsigned long long powerGeneration=0;};','struct SgSleepTarget {bool markerOwned=false;unsigned long long powerGeneration=0;DWORD pid=100;};')
    p=p.replace('struct SuspendGenerationPort {ULONGLONG fetch_add(ULONGLONG value,std::memory_order){const auto old=generation;generation+=value;return old;}} g_powerGeneration;',
      'struct SuspendGenerationPort {ULONGLONG load(std::memory_order)const{return generation;}ULONGLONG fetch_add(ULONGLONG value,std::memory_order){const auto old=generation;generation+=value;return old;}} g_powerGeneration;')
    p=p.replace('static bool sgSleepTimeoutSuccessAfter(ULONGLONG intent){return intent&&successfulExit>=intent;}','static bool sgSleepTimeoutSuccessAfter(ULONGLONG);')
    a=p.index('struct {bool active() const {return false;}} g_sgSleepTimeoutWindow;');b=p.index('static void sgAbnormalArm',a)
    p=p[:a]+'''static bool sgSleepTimeoutCurrent();
static void sgSleepTimeoutStop(const char*) noexcept;
static void sgSleepTimeoutArm(int,ULONGLONG);
static bool sgSleepTimeoutDispatchPermitted();
static void sgSleepTimeoutTick();
static ULONGLONG sgSleepTimeoutOriginalIntent();
static void sgSleepTimeoutPowerBroadcast(ULONG);
static void sgSleepTimeoutObserveSuspend();
static bool sgSleepTimeoutDeferClassification();
static void sgSleepTimeoutClassificationComplete(unsigned long long) noexcept;
static bool sgSleepTimeoutObservationHeld(unsigned long long);
static bool sgSleepTimeoutSettleClassification(unsigned long long);
static bool sgSleepTimeoutJoinEntry(int,ULONGLONG);
'''+p[b:]
    p=p.replace('ULONGLONG nowTick=10000,','ULONGLONG classificationTimerDue=0,timeoutTimerDue=0,g_sgLastModernStandbyIntentTick=0;\nULONGLONG nowTick=10000,')
    p=p.replace('static constexpr UINT_PTR SG_RETRY_TIMER_ID=0xA20C;','static constexpr UINT_PTR SG_RETRY_TIMER_ID=0xA20C,SG_SLEEP_TIMEOUT_TIMER_ID=0xA214;')
    p=p.replace('bool requestAccepted=true,keepInput=true,','bool requestAccepted=true,keepInput=true,')
    timer_signature='UINT_PTR fakeSetTimer(HWND,UINT_PTR id,UINT delay,TIMERPROC){'
    assert p.count(timer_signature)==1
    p=p.replace(timer_signature,'static UINT_PTR timeoutConsumerFailTimer=0;\n'+timer_signature+'if(id==timeoutConsumerFailTimer){timeoutConsumerFailTimer=0;return 0;}')

    # Expand only timer/queue terminals, retaining the original test clock.
    p=p.replace('if(id==SG_MONITOR_ENTRY_FAILURE_TIMER_ID){monitorTimerDue=nowTick+delay;return id;}',
      'if(id==SG_RESUME_CLASSIFY_TIMER_ID){classificationTimerDue=nowTick+delay;return id;}if(id==SG_SLEEP_TIMEOUT_TIMER_ID){timeoutTimerDue=nowTick+delay;return id;}if(id==SG_MONITOR_ENTRY_FAILURE_TIMER_ID){monitorTimerDue=nowTick+delay;return id;}')
    p=p.replace('if(id==SG_MONITOR_ENTRY_FAILURE_TIMER_ID){monitorTimerDue=0;return TRUE;}',
      'if(id==SG_RESUME_CLASSIFY_TIMER_ID){classificationTimerDue=0;return TRUE;}if(id==SG_SLEEP_TIMEOUT_TIMER_ID){timeoutTimerDue=0;return TRUE;}if(id==SG_MONITOR_ENTRY_FAILURE_TIMER_ID){monitorTimerDue=0;return TRUE;}')
    p=replace_function(p,'handlePowerResumeNotification','static void handlePowerResumeNotification(SgWork,const char*);')
    p=replace_function(p,'sgBeginModernStandbyIntent','static void sgBeginModernStandbyIntent();')
    p=replace_function(p,'sgQueueWork','''struct SgWorkItem {SgWork kind;unsigned long long generation;};
std::vector<SgWorkItem> actualWakeQueue;
void sgQueueWork(SgWork w,ULONGLONG g,ULONGLONG=0){if(w==SgWork::Suspend){++suspendQueued;return;}actualWakeQueue.push_back({w,g});}''')
    p+='''
static bool sgPowerButtonSleepConfigured(){return sgPowerButtonSleepMode()!=SgSleepMode::Unknown;}
void sgClearSleepTarget(){frozen=false;}
void inputCaptureSuspendSensorsForPower(){}
void xinputSlotsMarkDiscoveryDirty(){}
void msiVisibilityObserverNoteResume(ULONGLONG){}
unsigned softwareReadyNotices=0;
void steamOriginalRecoveryPowerReady(ULONGLONG){++softwareReadyNotices;}
void sgApplyLongSleepResumeGrace(ULONGLONG){}
void appendNativeLifecycleLog(const char* event,const json& value){sgRecordFact(event,value);}
void appendFanLifecycleLog(const char* event,const json& value){sgRecordFact(event,value);}
void sleepRecoveryAutoCloseSuspendEpisode(){}
void sleepRecoveryAutoInteractiveResume(ULONGLONG,int,ULONGLONG){}
bool powerLifecycleMatches(PowerLifecycle p,ULONGLONG g){return g==generation&&g_powerLifecycle==p;}
void publishPowerResumeReady(ULONGLONG g){if(g==generation){g_powerLifecycle=PowerLifecycle::Ready;g_resumeReadyGeneration=g;}}
'''
    p,f,publication_binding=bind_shared_publication(c,p,f)
    common=function('handlePowerResumeNotification');begin=function('sgBeginModernStandbyIntent')
    if 'struct SgSleepTimeoutObservation {' in timeout:
        abort=function('sgAbortSleepIntent')
        abort_stop='sgSleepTimeoutStop(reason ? reason : "sleep-intent-canceled");'
        assert abort.count(abort_stop)==1, 'actual abort no longer retires timeout'
        abort_classification='sgStopResumeClassificationWait("sleep-intent-canceled");'
        assert abort.count(abort_classification)==1, 'actual abort no longer retires classification'
        p=replace_function(p,'sgAbortSleepIntent','void sgAbortSleepIntent(const char* reason, ULONGLONG){'+abort_classification+abort_stop+'++abortCount;sgStopSleepRetry();sgRealWake("resume_suspend",generation);g_powerLifecycle=PowerLifecycle::Ready;}')
        p+='\n#define YMCC_TIMEOUT_OBSERVATION_ENABLE 1\n'
        p+=(Path(__file__).with_name('timeout_observation_ports.inc')).read_text(encoding='utf-8')
        if not getattr(c['args'],'resume_classification_sender_only',False):
            p+='''
static bool hardwareWriteAllowed();
static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* value,uint32_t) noexcept {
 *value={};value->generation=currentPowerGeneration();value->suspendSerial=1;
 value->observedNotificationSequence=g_sleepPowerDispatch.observedSequence();
 value->phase=static_cast<uint32_t>(g_powerLifecycle.load());
 value->flags=ymcc::sleeppower::kKnown|(hardwareWriteAllowed()?ymcc::sleeppower::kHardwareAllowed:0u)|
   (g_sleepPowerDispatch.pendingSuspend()?ymcc::sleeppower::kNotificationPending:0u);return 1;
}
'''
        conversion_at=source.index('static std::string W2U(const wchar_t*')
        conversion=source[conversion_at:braced(mask(source).index('{',conversion_at),mask(source))]
        parsers=conversion+'\n'+'\n'.join(function(name) for name in ['trim_ascii','sgKernelPowerEventId','sgKernelPowerEventDataInt','sgKernelPowerReason'])
        f+='\n'+parsers
        timeout='\n#define EvtQuery timeoutSourceQuery\n#define EvtNext timeoutSourceNext\n#define EvtRender timeoutSourceRender\n#define EvtClose timeoutSourceClose\n#pragma push_macro("PostMessageW")\n#undef PostMessageW\n#define PostMessageW timeoutSourcePost\n'+timeout+'\n#pragma pop_macro("PostMessageW")\n#undef EvtQuery\n#undef EvtNext\n#undef EvtRender\n#undef EvtClose\n'
    f+='\n'+common+'\n'+begin+'\n'+timeout+'\n'+native
    # Bind only the original cancellation statement and active-retry arm.
    # The remaining game/resource query-failure return is outside this leaf.
    if 'struct SgSleepTimeoutObservation {' in timeout:
        anchor='else if (w == PBT_APMQUERYSUSPENDFAILED || w == PBT_APMQUERYSTANDBYFAILED) {'
        at=source.index(anchor)+len(anchor)
        cancel=source[at:source.index('            const bool queryGateWasClosed',at)]
        at=source.index('            if (sgSleepRetryActive()) {',at);o=mask(source).index('{',at)
        retry=source[at:braced(o,mask(source))]
        f+='\nstatic bool timeoutQueryFailureActiveBranch(){'+cancel+retry+'\nreturn FALSE;}\n'
        c['timeoutQueryFailureBinding']={'actualCancel':cancel,'actualActiveRetry':retry,'remainingResourceReturnExecuted':False}

    # Execute exact original worker arms; continue retains its loop semantics.
    worker=[]
    for kind in ['WakeAutomatic','WakeHibernate','WakeSuspend']:
        anchor='else if (item.kind == SgWork::'+kind+')';m=mask(source);a=m.index(anchor);o=m.index('{',a);b=braced(o,m)
        worker.append(('if' if not worker else 'else if')+' (item.kind == SgWork::'+kind+') '+source[o:b])
    h+='\nvoid consumeActualWake(SgWorkItem item){for(int once=0;once<1;++once){\n'+'\n'.join(worker)+'\n}}\n'
    # Bind the actual common prelude before each extracted PBT branch. Other
    # menu/touch/trace consumers are unrelated terminals, not causal models.
    prefix=c['power_prefix'];pm=mask(prefix)
    preludes=[]
    for name in ['sleepRecoveryAutoCloseSuspendEpisode','sleepRecoveryAutoInteractiveResume']:
        pos=pm.index(name+'(');a=pm.rfind('if (',0,pos)
        assert a>=0
        condition=pm.index('(',a);depth=1;end=condition+1
        while depth:depth+=(pm[end]=='(')-(pm[end]==')');end+=1
        while pm[end].isspace():end+=1
        if pm[end]=='{':
            end=braced(end,pm)
        else:
            # A consumer lambda may contain its own semicolon before the call ends.
            depth=0
            while pm[end]!=';' or depth:
                depth+=(pm[end] in '({[')-(pm[end] in ')}]')
                end+=1
            end+=1
        preludes.append(prefix[a:end])
    if any('powerConsumer(' in block for block in preludes):
        p+='\nstatic const auto powerConsumer=[](const char*,const auto& action){action();};\n'
    for name,code in [('resumeAutomatic','PBT_APMRESUMEAUTOMATIC'),('resumeInteractive','PBT_APMRESUMESUSPEND'),('resumeCritical','PBT_APMRESUMECRITICAL')]:
        before=body(h,name);o=before.index('{')
        admission=('if(sgIgnorePowerReturnBeforeCurrentIntent(powerReceipt))return TRUE;\nconst bool resumeClassificationHeld=sgDeferModernResume(w,powerReceipt.tick);' if new else 'const bool resumeClassificationHeld=false;')
        prelude='''const ULONG w='''+code+''';
const auto powerReceipt='''+('g_sleepPowerDispatch.window(w,nowTick,nowFile)' if new else 'g_sleepPowerDispatch.window(w,nowTick)')+''';
struct Done {ymcc::sleeppower::Dispatch::Receipt r;~Done(){g_sleepPowerDispatch.complete(r,nowTick);}} done{powerReceipt};
'''+admission+'\nsgSleepTimeoutPowerBroadcast(w);\n'+'\n'.join(preludes)+'\n'
        h=replace_function(h,name,before[:o+1]+'\n'+prelude+before[o+1:])
    p='#include \"sleep_wake_admission.h\"\n'+p
    if getattr(c['args'],'resume_classification_public_facts_only',False):
        block=preludes[1]
        h+='\nstatic void publicActualInteractiveIngress(ULONG w,const ymcc::sleeppower::Dispatch::Receipt& powerReceipt,bool resumeClassificationHeld){\n'+block+'\n}\n'
    # Consume the exact ordinary setConfig cancellation branch. Ownership
    # transport alone is a leaf; complete RPC validation/locking is not claimed.
    setting=[line.strip() for line in source.splitlines() if 'if(!g_sgRetryEntryFailure&&!g_sgRetrySleepTimeout)owned(' in line]
    assert len(setting)==(1 if new else 0)
    actualSettingsCancellation=setting[0] if new else ''
    if new:h+='\nvoid consumeActualSettingsCancellation(){const auto owned=[](const auto& action){action();};'+actualSettingsCancellation+'}\n'
    utility='\n'.join(body(old,name) for name in ['at','reset','sleepIntent','dispatch'])
    # No replacement for missing new algorithms is compiled in the old-source
    # counterexample. Its first causal field case uses original bodies only.
    tests=Path(__file__).with_name('resume_classification_cases.inc').read_text(encoding='utf-8')
    if not new:tests=tests[:tests.index('// CURRENT_COORDINATOR_CONTROLS')]+'''\nint main(){fieldScenario(false,false,0);return 0;}\n'''
    else:tests='#define CURRENT_COORDINATOR 1\n'+tests
    if getattr(c['args'],'resume_classification_public_facts_only',False):
        tests='#define YMCC_PUBLIC_CLASSIFICATION_FACTS 1\n'+tests
    checks=utility+'\n'+tests
    binding={'originalId':'sleep.non-user-wake-consumer','selection':'resume-classification-publication-only' if c['args'].resume_classification_publication_only else 'resume-classification-lifetime-only' if c['args'].resume_classification_lifetime_only else 'resume-classification-edge-only' if c['args'].resume_classification_edge_only else 'resume-classification-only',
      'actualCommon':common,'actualBegin':begin,'actualWorkerBranches':worker,'actualTimeout':(root/'sleep_timeout_native.inc').read_text(encoding='utf-8-sig'),'actualClassifier':native,
      'actualPrelude':preludes,'actualSettingsCancellation':actualSettingsCancellation,'hasCurrentCoordinator':new,
      'boundSha256':{k:hashlib.sha256(v.encode()).hexdigest() for k,v in {'common':common,'begin':begin,'worker':'\n'.join(worker),'timeout':(root/'sleep_timeout_native.inc').read_text(encoding='utf-8-sig'),'classifier':native,'settings':actualSettingsCancellation}.items()},
      'actualOperationMutexDeclaration':entry_support['operationMutex'],'entryObservationSupport':entry_support,
      'injectedLeaves':'clock/timer, queue transport, Ready publication, exact single-PID terminal; no OS sleep/process/device/network operations',
      'originalLegacyAssertionsUnchanged':True,'fullOriginalParentExecuted':False,
      'sharedPowerPublication':publication_binding,'timeoutQueryFailure':c.get('timeoutQueryFailureBinding')}
    return p,f,h,checks,binding
