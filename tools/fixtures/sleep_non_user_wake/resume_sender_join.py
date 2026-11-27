"""Only the explicit original classification + actual sender integration leaf."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import sys
import time

def run(c, prepared):
    started = time.perf_counter()
    p, f, h, checks, binding = prepared
    args, source, function = c['args'], c['source'], c['function']
    selected = args.resume_sender_case
    choices = re.findall(r'^\s*run\("([a-z0-9-]+)",', Path(__file__).with_suffix('.inc').read_text(encoding='utf-8'), re.M)
    if len(choices)!=133 or len(set(choices))!=133:raise RuntimeError('joined case catalog changed')
    exact = selected[5:] if selected and selected.startswith('from:') else selected
    if selected is not None and (not exact or exact not in choices):raise RuntimeError('unknown or empty joined selector')
    out = args.output.resolve()
    def write_owned(name, data):
        with (out/name).open('xb') as stream:stream.write(data)
    if (out/'JOIN-RESULT.json').exists() or (out/'joined.cpp').exists():
        raise RuntimeError('fresh joined evidence required')
    def replace_once(text, before, after):
        if text.count(before) != 1:
            raise RuntimeError('joined fixture anchor not unique: '+before[:100])
        return text.replace(before, after)
    p = replace_once(p, '#include "sleep_request_fallback.h"', '#include "sleep_request_fallback.h"\n#include <powrprof.h>\n#include "sleep_power_observation.h"\n#include "input_host_shared_power.h"')
    a = p.index('struct SgSystemSleepRequestResult {')
    end = p.index('\n};', a)+3
    raw_start = source.index('struct SgSystemSleepRequestResult {')
    raw_end = c['braced'](c['masked'].index('{', raw_start))
    result_type = source[raw_start:raw_end]+';'
    p = p[:a]+result_type+p[end:]
    # The actual old worker stops its timer after its final locked check.
    # Gate that OS leaf with a real other thread; no product predicate is replaced.
    kill='BOOL fakeKillTimer(HWND,UINT_PTR id){'
    assert p.count(kill)==1
    p=p.replace(kill,'static void(*joinCleanupHook)()=nullptr;\n'+kill+
        'if(id==SG_RETRY_TIMER_ID&&joinCleanupHook){const auto action=joinCleanupHook;joinCleanupHook=nullptr;action();}')
    timer='UINT_PTR fakeSetTimer(HWND,UINT_PTR id,UINT delay,TIMERPROC){'
    timer_port='static bool joinTimerFail=false;static unsigned joinTimerCalls=0,joinTimerFailures=0,joinTimerPhase=0;static void(*joinTimerApiHook)()=nullptr;static void(*joinTimerFinishHook)()=nullptr;\n'
    timer_body='if(id==SG_RETRY_TIMER_ID){++joinTimerCalls;joinTimerPhase=static_cast<unsigned>(g_powerLifecycle.load());const auto hook=joinTimerApiHook;joinTimerApiHook=nullptr;if(hook)hook();if(joinTimerFail){joinTimerFail=false;++joinTimerFailures;timerDue=0;SetLastError(ERROR_NOT_ENOUGH_MEMORY);return 0;}}'
    p=replace_once(p,timer,timer_port+timer+timer_body)
    if 'struct SgSleepRetryFailureScope {' in source:
        timer_helper=function('sgArmSleepRetryTimer');assert f.count(timer_helper)==1
        finish_port='static void joinTimerFinishPort(const char* reason,const SgSleepRetryFailureScope* scope=nullptr){const auto hook=joinTimerFinishHook;joinTimerFinishHook=nullptr;if(hook)hook();sgFinishSleepRetryFailure(reason,scope);}'
        f=f.replace(timer_helper,finish_port+'\n#pragma push_macro("sgFinishSleepRetryFailure")\n#define sgFinishSleepRetryFailure joinTimerFinishPort\n'+timer_helper+'\n#pragma pop_macro("sgFinishSleepRetryFailure")\n')
        binding['retryTimerFinishBoundary']={'scopeTypeFromActualSource':True,'helperAndFinishBodiesUnrewritten':True,'helperCalleeNameMacroOnly':True,'yieldBeforeActualFinish':True,'sameScopePointerForwarded':True,'fullWndProcExecuted':False}
    injection = '''
bool joinInjectSuspendOnDispatch=false,joinInjectManualOnDispatch=false;
using JoinEndpointHook=void(*)();
static JoinEndpointHook joinRecoveryHook=nullptr,joinPrivilegeHook=nullptr,joinDispatchHook=nullptr,joinStartPublishedHook=nullptr,joinPowerReadHook=nullptr;
static JoinEndpointHook joinAfterCallHook=nullptr,joinRouteResultHook=nullptr,joinRetryResultHook=nullptr;
static JoinEndpointHook joinCleanupFactHook=nullptr;
static JoinEndpointHook joinResumeIpcHook=nullptr;
static std::string joinCleanupFactEvent,joinCleanupFactSource;
static bool joinCleanupFactFired=false;
static void joinRunHook(JoinEndpointHook& slot){const auto action=slot;slot=nullptr;if(action)action();}
static DWORD WINAPI suspendResumeSubscriptionCallback(PVOID,ULONG,PVOID);
'''
    p = replace_once(p, 'void sgRecordFact(const char* e,const json& d=json::object()){facts.emplace_back(e,d);if(fixtureSleepFactHook)fixtureSleepFactHook(e);}', injection+'''
void sgRecordFact(const char* e,const json& d=json::object()){
 facts.emplace_back(e,d);
 if(fixtureSleepFactHook)fixtureSleepFactHook(e);
 if(std::string(e)=="sleep-request-dispatch")joinRunHook(joinDispatchHook);
 if(std::string(e)=="sleep-request-route-result")joinRunHook(joinRouteResultHook);
 if(std::string(e)=="sleep-retry-request")joinRunHook(joinRetryResultHook);
 if(joinCleanupFactHook&&std::string(e)==joinCleanupFactEvent&&
    (joinCleanupFactSource.empty()||d.value("source",std::string{})==joinCleanupFactSource)){
  joinCleanupFactFired=true;joinRunHook(joinCleanupFactHook);
 }

 if(joinInjectManualOnDispatch&&std::string(e)=="sleep-request-dispatch"){
  joinInjectManualOnDispatch=false;g_sgLastPowerButtonWakeEventFileTime=nowFile;g_sgOriginalPowerButtonWakeFileTime=nowFile;
 }
 if(joinInjectSuspendOnDispatch&&std::string(e)=="sleep-request-dispatch"){
  joinInjectSuspendOnDispatch=false;
  suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr);
 }
}
''')
    p = replace_once(p,'void ipc_emit(const char*,const json&){++suspendLeafCalls;}',
        'void ipc_emit(const char* event,const json&){++suspendLeafCalls;if(std::string_view(event)=="power.resuming")joinRunHook(joinResumeIpcHook);}')
    p = replace_once(p,
        'bool sgBeginWakeGameRecovery(ULONGLONG g,const SgWorkItem* =nullptr){(void)sgResumeSleepTarget(g,true);return true;}',
        'bool sgBeginWakeGameRecovery(ULONGLONG g,const SgWorkItem* =nullptr){(void)sgResumeSleepTarget(g,true);joinRunHook(joinRecoveryHook);return true;}')
    p = replace_once(p, 'void stopPowerResumeWatchdog(){++suspendLeafCalls;}', 'void stopPowerResumeWatchdog(){++suspendLeafCalls;joinRunHook(joinStartPublishedHook);}')
    sender = function('sgRequestSystemSleep')
    at = source.index('static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* output,uint32_t bytes) noexcept {')
    reader = source[at:c['braced'](c['masked'].index('{', at))]
    # Actual input phases/capture/current are retained. This integration owns
    # no second power authority; its scalar facts are the original fixture's.
    ports = '''
std::atomic<uint64_t> g_sharedPowerPublicationRevision{1};
static bool hardwareWriteAllowed();
static bool joinPowerReadEndpoint(){joinRunHook(joinPowerReadHook);return hardwareWriteAllowed();}
static bool sgUserWakeCancelsSleepIntent(ULONGLONG,ULONGLONG);
static bool sgAbnormalPermitted(){return false;}
static bool sgAbnormalDispatchReady(){return false;}
static bool sgPrepareSleepPrivilege(SgSystemSleepRequestResult& result){result.privilegeRequired=true;result.shutdownPrivilegeEnabled=true;joinRunHook(joinPrivilegeHook);return true;}
static BOOLEAN WINAPI joinCapabilities(PSYSTEM_POWER_CAPABILITIES value){*value={};value->AoAc=platformAoAc;value->SystemS3=!platformAoAc;return platformCapsKnown;}
static BOOLEAN WINAPI joinSuspend(BOOLEAN hibernate,BOOLEAN force,BOOLEAN disableWake){
 if(hibernate||force||disableWake)throw std::runtime_error("original suspend args changed");
 ++requestCount;joinRunHook(joinAfterCallHook);if(requestAccepted){timerDue=0;return TRUE;}SetLastError(ERROR_ACCESS_DENIED);return FALSE;
}
void revokeSharedPowerMirrors(){} // registry effects are covered by their original real event cases
void publishSharedPowerLifecycle(const char*){++softwareReadyNotices;}
'''
    # softwareReadyNotices is already defined at the end of the original
    # classification preamble. Move only that declaration before these ports.
    p = replace_once(p, 'unsigned softwareReadyNotices=0;', '')
    ports = 'unsigned softwareReadyNotices=0;\n'+ports+'\n#pragma push_macro("hardwareWriteAllowed")\n#define hardwareWriteAllowed joinPowerReadEndpoint\n'+reader+'\n#pragma pop_macro("hardwareWriteAllowed")\n#pragma push_macro("GetPwrCapabilities")\n#pragma push_macro("SetSuspendState")\n#undef GetPwrCapabilities\n#undef SetSuspendState\n#define GetPwrCapabilities joinCapabilities\n#define SetSuspendState joinSuspend\n'+sender+'\n#pragma pop_macro("SetSuspendState")\n#pragma pop_macro("GetPwrCapabilities")\n'
    fake_start = p.index('SgSystemSleepRequestResult sgRequestSystemSleep(')
    fake_end = p.index('} // mockSleepSenderEnd', fake_start)+len('} // mockSleepSenderEnd')
    fake = p[fake_start:fake_end]
    p = replace_once(p, fake, ports)

    # Bind only the actual header admission prefix, not PID/recovery IO below it.
    header_path=args.source.parent/'wake_game_recovery.h'
    header_bytes=header_path.read_bytes();header=header_bytes.decode('utf-8-sig')
    signature='static bool sgBeginWakeGameRecovery(unsigned long long generation,const SgWorkItem* delivery=nullptr) {'
    if header.count(signature)!=1:raise RuntimeError('wake recovery admission signature changed')
    begin=header.index(signature);body=begin+len(signature)
    end=header.index('    sgCancelWakeGameRecoveryUnlocked("new-user-wake");',body)
    prefix=header[body:end];masked_prefix=c['mask_cpp'](prefix)
    focus_reader=re.escape('''const auto captureFocusPower = [generation] {
        ymcc::sleeppower::SharedPowerV1 power;
        if (!ymcc::sleeppower::capturePowerObservation(&readSharedSystemPower, power) ||
            power.generation != generation) return ymcc::sleeppower::SharedPowerV1{};
        return power;
    };''')
    boundary_guard=r'''const auto wakeSleepSequence = g_sleepPowerDispatch\.observedSleepBoundarySequence\(\);\s*
    const auto wakeSuspendSerial = g_sharedPowerSuspendSerial\.load\(std::memory_order_acquire\);\s*
    const std::pair<uint64_t,uint64_t> wakeBoundary\{wakeSleepSequence,wakeSuspendSerial\};\s*
    if\(delivery&&!delivery->wakeBoundaryMatches\(wakeSleepSequence,wakeSuspendSerial\)\)return false;\s*
    const auto stillAdmitted = \[&\] \{\s*
    const auto phase = g_powerLifecycle\.load\(std::memory_order_acquire\);\s*
    return generation == currentPowerGeneration\(\) && !g_exitRequested\.load\(\) &&\s*
    phase != PowerLifecycle::Suspending && phase != PowerLifecycle::Suspended &&\s*
    !sgSleepRetryActive\(\) && !g_sleepPowerDispatch\.pendingSuspend\(\) &&\s*
    wakeSleepSequence == g_sleepPowerDispatch\.observedSleepBoundarySequence\(\) &&\s*
    wakeSuspendSerial == g_sharedPowerSuspendSerial\.load\(std::memory_order_acquire\);\s*\};\s*
    '''+focus_reader+r'''\s*
    const bool crossedSleepBoundary = g_sgWakeRecovery\.crossedSleepBoundary\(wakeSleepSequence, wakeSuspendSerial\);\s*
    const bool duplicateGeneration = g_sgLastGameWakeGeneration == generation;\s*
    const bool rebuild = duplicateGeneration && g_sgZeroDispatchWakeBoundary &&\s*
        \*g_sgZeroDispatchWakeBoundary != wakeBoundary &&\s*
        \(!sgWakeGameRecoveryCanWaitForNextWakeUnlocked\(\) \|\|\s*
         std::none_of\(g_sgWakeRecovery\.entries\.begin\(\), g_sgWakeRecovery\.entries\.end\(\),\s*
            \[\]\(const auto& e\) \{ return !e\.done; \}\)\);\s*'''
    boundary_guard=re.sub(r'\n +','',boundary_guard)
    guard=r'\s*std::lock_guard<std::mutex> opLock\(g_sgOpMtx\);\s*if\s*\([^{};]*\)\s*return false;\s*'+boundary_guard+r'if\s*\(duplicateGeneration && !rebuild\)\s*'
    final_guard=r'\s*if \(!stillAdmitted\(\)\) return false;\s*'
    if not re.fullmatch(guard+r'return true;'+final_guard,masked_prefix,re.S):
        # The same-generation continuation does not touch process IO, so
        # execute it with the original state types and counted NotifyWork port.
        if not re.fullmatch(guard+r'\{.*\}'+final_guard,masked_prefix,re.S):
            raise RuntimeError('wake recovery prefix is not exact initial opLock and return guard')
        declarations=re.search(r'^struct SgWakeRecoveryEntry \{.*?^static SgWakeRecoveryBatch g_sgWakeRecovery;$',header,re.M|re.S)
        pending=re.findall(r'^static std::atomic<bool> g_sgWakeRecoveryPending\{false\};$',source,re.M)
        parked=re.findall(r'^static std::atomic<bool> g_sgWakeRecoveryParked\{false\};$',header,re.M)
        if declarations is None or len(pending)!=1 or len(parked)!=1:
            raise RuntimeError('wake recovery continuation state declarations changed')
        p+='\n'+declarations[0]+'\n'+pending[0]+'\n'+parked[0]+'\n'
        for name in ('sgWakeGameRecoveryHasWork', 'sgWakeGameRecoveryCanWaitForNextWakeUnlocked'):
            if name+'(' in masked_prefix:
                p+='\n'+function(name,header)+'\n'
    last_wake=[line for line in header.splitlines() if re.fullmatch(r'static unsigned long long g_sgLastGameWakeGeneration = 0;',line)]
    zero_dispatch=re.findall(r'^static std::optional<std::pair<uint64_t,uint64_t>> g_sgZeroDispatchWakeBoundary;$',header,re.M)
    if len(last_wake)!=1 or len(zero_dispatch)!=1:raise RuntimeError('wake recovery last boundary declarations changed')
    if not re.search(r'^static std::mutex g_sgOpMtx;',p,re.M):
        operation=[line for line in source.splitlines() if re.fullmatch(r'static std::mutex g_sgOpMtx;[ \t]*(?://[^\r\n]*)?',line)]
        if len(operation)!=1:raise RuntimeError('actual operation mutex declaration missing for header prefix')
        p+='\n'+operation[0]+'\n'
    p='#include <optional>\n#include <utility>\n'+p+'\n'+last_wake[0]+'\n'+zero_dispatch[0]+'\n'
    admission_probe='static unsigned joinHeaderAdmissionCount=0;\n'+signature.replace('sgBeginWakeGameRecovery','joinActualWakeRecoveryAdmissionPrefix')+prefix+'    ++joinHeaderAdmissionCount;\n    return true;\n}\n'
    bom=3 if header_bytes.startswith(b'\xef\xbb\xbf') else 0
    header_binding={'source':'native/wake_game_recovery.h','sourcePath':str(header_path.resolve()),
        'sourceBytes':len(header_bytes),'sourceSha256':hashlib.sha256(header_bytes).hexdigest(),
        'prefixBeginByte':bom+len(header[:body].encode()),'prefixEndByte':bom+len(header[:end].encode()),
        'prefixBytes':len(prefix.encode()),'prefixSha256':hashlib.sha256(prefix.encode()).hexdigest(),'prefix':prefix,
        'signatureOriginal':signature,'onlySignatureNameChanged':True,'prefixBodyRewritten':False,
        'remainingRecoveryBody':'one counted terminal, not executed/extracted','fullGameRecoveryProven':False}
    binding['wakeRecoveryAdmissionPrefix']=header_binding

    # Retain original assertions/functions. Only add a counter, and replace the
    # default executable main with this declared smaller joined scenario set.
    checks = replace_once(checks, 'void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}',
                          'unsigned joinAssertions=0;\nvoid check(bool ok,const char* why){++joinAssertions;if(!ok)throw std::runtime_error(why);}')
    main = checks.index('int main(int argc,char** argv){try{')
    extra=Path(__file__).with_name('timeout_observation_cases.inc').read_text(encoding='utf-8')
    checks = checks[:main]+extra+'\n'+admission_probe+'\n'+Path(__file__).with_suffix('.inc').read_text(encoding='utf-8')
    # Execute the complete original WinEvt callback, with only rendering,
    # journal transport, posts and unrelated consumer leaves controlled.
    kernel_callback = function('sgKernelPowerEventCallback')
    post_name='sgPostKernelWake' if 'sgPostKernelWake(' in kernel_callback else 'sgPostS4Wake'
    s4_post = function(post_name) if f'{post_name}(' in kernel_callback else ''
    kernel_constants = []
    for name in ['WM_SG_S0_INTENT','WM_SG_S0_WAKE','WM_SG_S4_WAKE']:
        matches = re.findall(r'^#define '+name+r'[^\r\n]*',source,re.M)
        if len(matches)!=1:raise RuntimeError('kernel callback constant not unique: '+name)
        kernel_constants.append(matches[0])
    for name in ['WM_FAN_KERNEL_POWER_FACT']:
        matches = re.findall(r'^static constexpr UINT '+name+r'[^\r\n]*',source,re.M)
        if len(matches)!=1:raise RuntimeError('kernel callback constant not unique: '+name)
        kernel_constants.append(matches[0])
    kernel_ports = r'''
#include "sleep_abnormal_resleep.h"

static std::function<void()> joinKernelAfterOriginalWake,joinKernelAfterConfirmedWake;
static std::function<void()> joinKernelAfterTimeoutRecord;
static void joinKernelRecordTimeout(int id,int reason,ULONGLONG at,ULONGLONG generation,bool continuing=false,bool sessionEnded=false,
        ymcc::sleeppower::KernelCycleIdentity identity={}){
 sgSleepTimeoutRecordKernel(id,reason,at,generation,continuing,sessionEnded,identity);
 auto hook=std::move(joinKernelAfterTimeoutRecord);joinKernelAfterTimeoutRecord=nullptr;if(hook)hook();
}

static void joinKernelPublishOriginalWake(std::atomic<ULONGLONG>& marker,ULONGLONG at){
 sgPublishSleepEventTime(marker,at);
 auto hook=std::move(joinKernelAfterOriginalWake);joinKernelAfterOriginalWake=nullptr;if(hook)hook();
}
namespace ymcc::abnormalsleep {
static void joinKernelPublishConfirmedWake(std::atomic<ULONGLONG>& marker,ULONGLONG at){
 publishConfirmedWake(marker,at);
 auto hook=std::move(::joinKernelAfterConfirmedWake);::joinKernelAfterConfirmedWake=nullptr;if(hook)hook();
}
}

static std::atomic<int> g_sgLastKernel507Reason{-1};
static unsigned joinKernelEvidenceBuilds=0;
static bool joinKernelEvidenceThrows=false;
namespace ymcc::sleep_residency {
static json joinKernelMakeEvidence(const std::string& xml){
 ++joinKernelEvidenceBuilds;if(joinKernelEvidenceThrows){joinKernelEvidenceThrows=false;throw std::runtime_error("controlled evidence construction fault");}
 return makeEvidence(xml);
}
}
static std::string joinKernelXml;
static std::function<void()> joinKernelBeforeJournal;
static unsigned joinKernelJournalPoints=0,joinKernelPosts=0,joinKernelOtherConsumers=0;
static unsigned joinKernelFanPosts=0;
static ULONGLONG joinLastFanFact=0;
static unsigned joinKernelS0Posts=0;
static ULONGLONG joinLastS0At=0;
static bool sgRenderKernelPowerEvent(EVT_HANDLE event,std::string& xml){
 if(event!=reinterpret_cast<EVT_HANDLE>(0xb01))throw std::runtime_error("unadmitted kernel event handle");
 xml=joinKernelXml;return true;
}
static unsigned joinKernelStaticScanCancellations=0;
static void sgStaticScanCancel(const char*){++joinKernelOtherConsumers;++joinKernelStaticScanCancellations;}
static void sgAbnormalPostCompleted(const std::string&,ULONGLONG){++joinKernelOtherConsumers;}
static void sleepRecoveryAutoRecordKernelWake(ULONGLONG,int,int,int,ULONGLONG){++joinKernelOtherConsumers;}
static void joinKernelRecordFact(const char* event,const json& details){
 if(std::string(event)=="kernel-power"){
  ++joinKernelJournalPoints;auto hook=std::move(joinKernelBeforeJournal);joinKernelBeforeJournal=nullptr;if(hook)hook();
 }
 sgRecordFact(event,details);
}
static BOOL WINAPI joinKernelPost(HWND window,UINT message,WPARAM,LPARAM value){
 if(window!=g_hwnd)throw std::runtime_error("foreign callback post target");
 if(message==WM_FAN_KERNEL_POWER_FACT){
  if(!value)throw std::runtime_error("missing Fan kernel fact");
  joinLastFanFact=static_cast<ULONGLONG>(value);++joinKernelFanPosts;
 }
 else if(message==WM_SG_S0_WAKE){
  joinLastS0At=static_cast<ULONGLONG>(value);++joinKernelS0Posts;
 }
 else if(message!=WM_SG_S0_INTENT&&message!=WM_SG_S0_WAKE&&message!=WM_SG_S4_WAKE)
  throw std::runtime_error("unadmitted callback post");
 ++joinKernelPosts;return TRUE;
}
'''
    s4_window_lib = '\n#pragma comment(lib, "user32.lib")\n' if s4_post else ''
    ingress = ''
    if 'sgRenderKernelPowerIngress(' in kernel_callback:
        a=source.index('struct SgOriginalWakeDelivery {');b=source.index('static DWORD WINAPI sgKernelPowerEventCallback(',a)
        ingress=source[a:b]
    kernel = s4_window_lib+'\n'.join(kernel_constants)+'\n'+kernel_ports+ingress+'''
#pragma push_macro("sgSleepTimeoutRecordKernel")
#define sgSleepTimeoutRecordKernel joinKernelRecordTimeout
#pragma push_macro("publishConfirmedWake")
#pragma push_macro("sgPublishSleepEventTime")
#define publishConfirmedWake joinKernelPublishConfirmedWake
#define sgPublishSleepEventTime joinKernelPublishOriginalWake
#pragma push_macro("makeEvidence")
#pragma push_macro("sgRecordFact")
#pragma push_macro("PostMessageW")
#undef sgRecordFact
#undef PostMessageW
#define makeEvidence joinKernelMakeEvidence
#define sgRecordFact joinKernelRecordFact
#define PostMessageW joinKernelPost
'''+s4_post+'\n'+kernel_callback+'''
#pragma pop_macro("PostMessageW")
#pragma pop_macro("sgRecordFact")
#pragma pop_macro("makeEvidence")
#pragma pop_macro("sgPublishSleepEventTime")
#pragma pop_macro("publishConfirmedWake")
#pragma pop_macro("sgSleepTimeoutRecordKernel")
'''
    binding['kernelPowerCallback']={'actualBody':kernel_callback,'actualBodySha256':hashlib.sha256(kernel_callback.encode()).hexdigest(),
        'bodyAdaptations':0,'manualPublicationPorts':'both wrappers execute real original atomic publisher before one-shot interleave; no marker semantics replaced','eventRender':'exact XML fixture port','journal':'existing collected facts; synchronous controlled interleave before first journal',
        'posts':'counted only; fan payload allocated by real callback is deleted at the fake transport',
        'otherConsumers':'static inventory, abnormal followup and Steam witness are counted leaves','realWinEvtSubscription':False,'timeoutRecordPort':'actual record helper runs first, one-shot GUI interleave before original latest-entry atomic; callback body bytes unchanged, callee macro only'}
    if s4_post:
        binding['kernelPowerCallback']['actualS4PostSha256']=hashlib.sha256(s4_post.encode()).hexdigest()
    prefix_signature='static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {'
    prefix_start=source.index(prefix_signature)+len(prefix_signature)
    prefix_end=source.index(';',prefix_start)+1
    first_statement=source[prefix_start:prefix_end]
    guard=re.match(r'\s*if\s*\(\s*m\s*>=\s*0xC000\s*&&\s*w\s*==\s*0\s*&&\s*l\s*==\s*0\s*&&\s*h\s*==\s*g_hwnd\s*\)\s*\{',c['masked'][prefix_start:prefix_end])
    leading_guard=''
    deferred=first_statement
    if guard:
        guard_end=c['braced'](prefix_start+guard.end()-1)
        leading_guard=source[prefix_start:guard_end]
        deferred=source[guard_end:source.index(';',guard_end)+1]
    supported=bool(re.fullmatch(r'\s*sgSleepTimeoutResumeDeferredReentry\(\);',c['mask_cpp'](deferred)))
    prefix=deferred if supported else ''
    # The maintenance guard cannot run for the timer message used by this replay.
    scope_proof='static_assert(WM_TIMER < 0xC000);' if leading_guard else ''
    prefix_port='static constexpr bool joinDeferredPrefixSupported='+('true' if supported else 'false')+';\nstatic void joinActualDeferredWndProcPrefix(){'+scope_proof+prefix+'}\n'
    binding['deferredOriginalWndProcPrefix']={'signature':prefix_signature,'actualFirstStatement':leading_guard or first_statement,'actualDeferredCall':deferred,'supported':supported,'onlyAddedWrapperName':not leading_guard,'priorGuardExcludedForMessage':'WM_TIMER' if leading_guard else None,'remainingWndProcExecuted':False}
    checks=prefix_port+checks
    cpp = p+'\n'+f+'\n'+h+'\n'+kernel+'\n'+checks
    write_owned('joined.cpp',cpp.encode('utf-8'))
    binding.update({'selection':'resume-classification-sender-only','actualSender':sender,'actualReader':reader,
                    'actualSenderSha256':hashlib.sha256(sender.encode()).hexdigest(),
                    'actualReaderSha256':hashlib.sha256(reader.encode()).hexdigest(),
                    'senderBodyAdaptations':0,'actualCallbackCountedByOriginalFixture':True,
                    'compiledSha256':hashlib.sha256(cpp.encode()).hexdigest(),
                    'originalFieldScenarioAssertionsPreserved':True,'originalFieldDefaultInputsPreserved':True,'fieldOptionalDurationInput':True,'originalDefaultParentExecuted':False})
    write_owned('JOIN-TASK.json',(json.dumps(binding,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))
    if args.prepare_only:
        result={'status':'PREPARED_NOT_RUN','selection':'resume-classification-sender-only',
                'requestedCase':selected,'source':str(out/'joined.cpp'),'sourceSha256':binding['compiledSha256'],
                'compiledThisRun':False,'processes':[]}
        write_owned('JOIN-RESULT.json',(json.dumps(result,indent=2)+'\n').encode('utf-8'))
        print(json.dumps(result))
        return 0
    repo = args.source.parent.parent
    sdk = Path(r'C:\Program Files (x86)\Windows Kits\10')
    vcvars = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
    def quote(value):
        value = str(value)
        if any(t in value for t in ['"','%','\r','\n']):raise RuntimeError('unsupported joined build path')
        return '"'+value+'"'
    include = ';'.join(str(sdk/'Include'/'10.0.26100.0'/name) for name in ['ucrt','shared','um','winrt','cppwinrt'])
    libs = ';'.join(str(sdk/'Lib'/'10.0.26100.0'/name/'x64') for name in ['ucrt','um'])
    template_path=Path(__file__).with_suffix('.cmd.in')
    template=template_path.read_text(encoding='utf-8')
    values={'VCVARS':quote(vcvars),'INCLUDE':include,'LIB':libs,
      'JSON_ROOT':quote(repo/'deps/json'),'NATIVE_ROOT':quote(repo/'native'),
      'CPP':quote(out/'joined.cpp'),'DEPENDENCIES':quote(out/'deps'/'joined.json'),
      'OBJECT':quote(out/'joined.obj'),'EXE':quote(out/'joined.exe')}
    build=re.sub(r'\{\{([A-Z_]+)\}\}',lambda match:values[match.group(1)],template)
    if '{{' in build:raise RuntimeError('joined recipe token unresolved')
    (out/'deps').mkdir(exist_ok=True)
    write_owned('build.cmd',build.replace('\r\n','\n').replace('\n','\r\n').encode('utf-8'))
    reuse=getattr(sys.modules.get('sitecustomize'),'_cfg',{}).get('joinedBuildReuse')
    result={'pass':False,'selection':'resume-classification-sender-only','processes':[],
            'compiledThisRun':reuse is None,'compileReference':None if reuse is None else reuse['authority']}
    def execute(name, command, timeout):
        t=time.perf_counter()
        try:
            child=subprocess.run(command,cwd=repo,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=timeout,creationflags=subprocess.CREATE_NO_WINDOW)
        except subprocess.TimeoutExpired as error:
            write_owned(name+'.stdout.log',error.stdout or b'');write_owned(name+'.stderr.log',error.stderr or b'')
            result['processes'].append({'name':name,'exitCode':None,'elapsedMs':(time.perf_counter()-t)*1000,'timeoutMs':timeout*1000,'timedOut':True,'error':'TimeoutExpired','command':command})
            raise
        write_owned(name+'.stdout.log',child.stdout);write_owned(name+'.stderr.log',child.stderr)
        result['processes'].append({'name':name,'exitCode':child.returncode,'elapsedMs':(time.perf_counter()-t)*1000,'timeoutMs':timeout*1000,'timedOut':False,'command':command})
        return child
    try:
        if reuse is None:
            compile=execute('compile',[os.environ.get('ComSpec',r'C:\Windows\System32\cmd.exe'),'/d','/c',str(out/'build.cmd')],60)
            if compile.returncode:raise RuntimeError('joined actual source compile failed')
        else:
            if reuse.get('status')!='BUILD_REFERENCE':raise RuntimeError('joined build reference status invalid')
            for name,expected in [('joined.cpp',reuse['cppSha256']),('joined.exe',reuse['exeSha256']),('joined.obj',reuse['objectSha256'])]:
                if hashlib.sha256((out/name).read_bytes()).hexdigest()!=expected:
                    raise RuntimeError('joined build reference generated source or artifact changed: '+name)
        native=execute('behavior',[str(out/'joined.exe')]+([selected] if selected else []),5)
        result['nativeExitCode']=native.returncode
        text=native.stdout.decode('utf-8');lines=[line[len('RESUME_SENDER_JOIN '):] for line in text.splitlines() if line.startswith('RESUME_SENDER_JOIN ')]
        if lines:result['native']=json.loads(lines[-1])
        result['pass']=native.returncode==0
    except Exception as error:result['error']=str(error)
    result['elapsedMs']=(time.perf_counter()-started)*1000
    write_owned('JOIN-RESULT.json',(json.dumps(result,indent=2)+'\n').encode('utf-8'))
    print(json.dumps({'pass':result['pass'],'error':result.get('error'),'cases':result.get('native',{}).get('count')}))
    return 0 if result['pass'] else 1
