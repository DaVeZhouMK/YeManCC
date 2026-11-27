"""Bind newly referenced Native helpers for the unchanged legacy scenarios.
The legacy common/transport ports retain their original scope. The explicit
resume-classification selection separately executes the actual common/worker.
"""
def bind(c):
    from pathlib import Path
    import re,runpy
    p,f,h=c['preamble'],c['functions'],c['handlers'];source=c['source']
    inc=Path(c['args'].source).parent/'sleep_resume_classification_native.inc'
    if not inc.exists():return p,f,h
    support=runpy.run_path(str(Path(__file__).with_name('resume_classification.py')))
    p,f,_=support['bind_entry_observation_support'](c,p,f,inc.read_text(encoding='utf-8-sig'))
    keys=['sharedSleepWakeClassified','sgBlockSharedWakeClassification','sgCompleteSharedWakeClassification','sgResumeClassification','sgStopResumeClassification','sgFinishResumeClassification','sgDeferModernResume','sgJoinPendingResumeEntry','sgEntryFailureLifecycleAllowed','sgIgnorePowerReturnBeforeCurrentIntent','sgResumeEntryObservation','sgJoinResumeEntryObservation','sgRevokeResumeEntryObservation','sgConsumeEntryObservationHandoffReady','sgCancelRetryAdmission','sgDeferRetryAdmission','sgResumeRetryAdmission','sgRetryAdmissionHeld','sgSleepTimeoutSuccessfulExitTime','SG_RESUME_CLASSIFY_TIMER_ID']
    declarations=[line for line in source.splitlines() if line.startswith('static ') and line.rstrip().endswith(';') and any(k in line for k in keys)]
    p=p.replace('using json = nlohmann::json;','using json = nlohmann::json;\n'+'enum class SgRetryKind : uint8_t;\n'+'\n'.join(declarations))
    p=p.replace('struct SuspendGenerationPort {ULONGLONG fetch_add(ULONGLONG value,std::memory_order){const auto old=generation;generation+=value;return old;}} g_powerGeneration;',
        'struct SuspendGenerationPort {ULONGLONG load(std::memory_order)const{return generation;}ULONGLONG fetch_add(ULONGLONG value,std::memory_order){const auto old=generation;generation+=value;return old;}} g_powerGeneration;')
    p=p.replace('BOOL fakeKillTimer(HWND,UINT_PTR id){','BOOL fakeKillTimer(HWND,UINT_PTR id){if(id==SG_RESUME_CLASSIFY_TIMER_ID)return TRUE;')
    # This legacy port allocates no timeout continuation or real sensor IO.
    # Actual timeout and input ownership are covered by their selected consumers.
    p+='\nstatic void sgSleepTimeoutClassificationComplete(unsigned long long) noexcept {}\n'
    if c['args'].broadcast_consumer_only:
        p+='\nstatic void clearPowerResumeReady();\n' # The complete broadcast binds the actual Ready receipt.
    else:
        p+='\nstatic void clearPowerResumeReady() {} // Ready receipt state is outside this legacy port.\n'
    p+='\nstatic unsigned legacySensorPowerSuspends=0;\nstatic void inputCaptureSuspendSensorsForPower(){++legacySensorPowerSuspends;}\n'
    p+='\nvoid steamOriginalRecoveryPowerReady(ULONGLONG){}\nvoid sleepRecoveryAutoCloseSuspendEpisode(){}\nvoid sleepRecoveryAutoInteractiveResume(ULONGLONG,int,ULONGLONG){}\n'
    # Reuse the same exact publisher binding as the original directed consumer.
    # A declaration-only fake would leave current classifier calls disconnected.
    bind_publication=support['bind_shared_publication']
    p,f,_=bind_publication(c,p,f)
    # Same original in-memory Native scalar scope, never an alternate power owner.
    p+='\n#include "sleep_power_observation.h"\n'
    p+='\nstatic ULONGLONG sgSleepTimeoutSuccessfulExitTime(){return successfulExit;}\n'
    p+='\nstatic ULONGLONG sgSleepTimeoutOriginalIntent(){return 0;} // legacy has no timeout window\n'
    p+='''
static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* value,uint32_t) noexcept {
 *value={};value->generation=currentPowerGeneration();value->suspendSerial=1;
 value->observedNotificationSequence=g_sleepPowerDispatch.observedSequence();
 value->publicationRevision=g_sharedPowerPublicationRevision.load();
 value->phase=static_cast<uint32_t>(g_powerLifecycle.load());
 value->flags=ymcc::sleeppower::kKnown|(g_sleepPowerDispatch.pendingSuspend()?ymcc::sleeppower::kNotificationPending:0u);return 1;
}
'''
    native=inc.read_text(encoding='utf-8-sig')
    f+='\n'+native
    if 'static void sgReportSleepDisplayRelight() noexcept;' in native:
        f+='\n'+c['function']('sgReportSleepDisplayRelight',noexcept=True)
    for name in ['resumeAutomatic','resumeInteractive','resumeCritical']:
        a='int '+name+'() {\n';assert h.count(a)==1
        code={'resumeAutomatic':'PBT_APMRESUMEAUTOMATIC','resumeInteractive':'PBT_APMRESUMESUSPEND','resumeCritical':'PBT_APMRESUMECRITICAL'}[name]
        receipt='const auto powerReceipt=g_sleepPowerDispatch.window('+code+',GetTickCount64(),sgNowFileTime());\nstruct Complete{ymcc::sleeppower::Dispatch::Receipt receipt;~Complete(){g_sleepPowerDispatch.complete(receipt,GetTickCount64());}} complete{powerReceipt};\n'
        consumer=('const WPARAM w='+code+';\n'+c['consumer_binding'] if
                  'powerConsumer(' in h.split(a,1)[1].split('\n}\n',1)[0] else '')
        h=h.replace(a,a+receipt+consumer+'const bool resumeClassificationHeld=false; // original legacy common port, not shared classification execution\n')
    return p,f,h
