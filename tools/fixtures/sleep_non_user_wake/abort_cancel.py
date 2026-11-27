"""Optional full cancellation body for existing sleep-game-pause consumer.
Real mutex/threads, original abort/Kernel/reader/process-loop bodies; only SDK,
file/process transport and notifications are controlled leaves. No real sleep.
"""
from pathlib import Path
import hashlib,json,os,re,subprocess

def run(c):
    args,source,function=c['args'],c['source'],c['function']
    out=args.output.resolve();root=args.source.resolve().parent.parent
    stages=['classification-stop','timeout-stop','internal-clear','standby-clear','retry-stop','operation-wait','trace-log','nt-return','episode-close','gate-open-log','manual-marker-read','lease-file-exists','marker-read','lease-validate','identity-query','process-open','opened-birth','resume-delay']
    events=['new506-user','new506-nonuser','new-suspend','new-query','exit','late506-old','missing506','future506','resume-only']
    cases=[('none','no-event')]+[(s,e) for s in stages for e in events]+[('before-abort','new506-user'),('before-abort','new506-nonuser'),('classification-stop','late506-before-cancel'),('operation-wait','late506-before-cancel'),('nt-return','partial-running-confirmation'),('nt-return','unknown-running-confirmation')]
    selected=args.cancel_boundary_case
    if selected is not None:
        exact={'owned-lease-checked-existing-stale':[('owned-lease','transient-worker-checked-batch-stale-owner')],
               'owned-lease-checked-existing':[('owned-lease','transient-worker-checked-batch-'+v) for v in ['append','merge-known','merge-empty','merge-replaced','merge-done','stale-owner','stale-identity','stale-batch','stale-owner-done','known-into-empty','stale-owner-final']],
               'owned-lease-checked-boundaries':[('owned-lease','transient-worker-checked-veto-'+v) for v in ['kernel','suspend','query','manual','owner','generation','exit']]+[('owned-lease','transient-worker-checked-permanent')],
               'owned-lease-checked-failure':[('owned-lease',v) for v in ['transient-worker-checked-alloc','transient-worker-checked-io']],
               'owned-lease-checked-veto':[('owned-lease','transient-worker-checked-bad-generation')],
               'owned-lease-worker-readers':[('owned-lease',v) for v in ['transient-worker-read-raw','transient-worker-read-bom','transient-worker-replace']],
               'owned-lease-worker':[('owned-lease','transient-worker')],
               'owned-lease-worker-read':[('owned-lease','transient-worker-read')],
               'owned-lease-transient-vetoes':[('owned-lease','transient-'+v) for v in ['kernel','suspend','query','manual','owner','marker','exit','generation','kernel-open','kernel-birth','kernel-nt']],
               'owned-lease-batch-reuse':[('owned-lease','transient-batch-append'),('owned-lease','transient-batch-merge')],
               'owned-lease-ready-hold':[('owned-lease','transient-ready-hold')],
               'owned-lease-permanent':[('owned-lease','transient-permanent')],
               'owned-lease-transient':[('owned-lease','transient-resume')],
               'owned-lease-continued':[('owned-lease','continued-generation')],
               'owned-lease-ready-current':[('owned-lease',e) for e in ['same-generation','continued-generation','newer-boundary','older-task-generation']],
               'owned-lease-vetoes':[('owned-lease',e) for e in ['manual-marker','wrong-birth','newer-boundary']],
               'owned-lease':[('owned-lease',e) for e in ['same-generation','continued-generation','manual-marker','wrong-birth','newer-boundary']],
               'gate-current':[('gate-open-log','new506-user'),('gate-open-log','new506-nonuser')],
               'steam-query-cancel':[('none','steam-query-canceled'),('none','steam-query-canceled-fault'),('gate-open-log','new-suspend'),('operation-wait','late506-before-cancel')],
               'deep-current':[('process-open','new506-user'),('opened-birth','new506-nonuser')],
               'partial-return':[('nt-return','partial-running-confirmation'),('nt-return','unknown-running-confirmation')],
               'ready-wake':[('lifetime',e) for e in ['entry-current','entry-stale','entry-missing','entry-future','query-current','query-stale','exit']],
               'kernel-wake-redelivery':[('kernel-wake-redelivery',e) for e in ['failed507','success507','new-generation','new-suspend','older-suspend','continue-to-s4','coalesce507','manual507','manual507-delayed','exit','failed506','success506','replace506','retired506','generation506','exit506','cross107failed','cross107posted','cross507failed','posted107-after507','posted107-before507']],
               'kernel-wake-readback':[('kernel-wake-readback',e) for e in ['only507','only107','live-first','readback-first','new506-before','new506-queued','new-generation','new-suspend','exit']]}
        if selected not in exact:raise RuntimeError('unknown cancellation selection')
        cases=exact[selected]
    def write(name,data):
        with (out/name).open('xb') as f:f.write(data)
    def text(name,value):write(name,value.encode('utf-8'))
    mask=c['mask'](source)
    def type_body(name):
        ms=list(re.finditer(r'(?:enum class|struct) '+re.escape(name)+r'\b[^;{}]*\{',mask));assert len(ms)==1,(name,len(ms))
        a=ms[0].start();b=mask.index('{',a)+1;d=1
        while d:d+=(mask[b]=='{')-(mask[b]=='}');b+=1
        assert mask[b]==';';return source[a:b+1]
    names=['sgMarkSleepTrigger','sgRenewCancellationReady','sgAbortSleepIntent','sgKernelPowerEventCallback','sgPublishSleepEventTime','sgRememberKernelSleepEntry','currentPowerGeneration','openHardwareWriteGate','sgResumeSleepTargetUnlocked','hardwareWriteAllowed','sgReadSleepTarget','sgClearSleepTargetIfMatches','sgSleepLeaseOwnedByGeneration','sgDrainProcessSuspendCountIfCurrent','readSharedSystemPower']
    fragments=[]
    ready=source[source.index('static bool powerLifecycleMatches('):source.index('static uint64_t recordPowerResumeReady(')]
    text('ready_lifecycle.inc',ready)
    fragments.append(dict(name='ready_lifecycle',sha256=hashlib.sha256(ready.encode()).hexdigest(),verbatim=True))
    for n in names:
        s=function(n)
        if n=='sgDrainProcessSuspendCountIfCurrent':assert 'template<class Current>\n'+s in source;s='template<class Current>\n'+s
        text(n+'.inc',s+'\n');fragments.append(dict(name=n,sha256=hashlib.sha256(s.encode()).hexdigest(),verbatim=True))
    for n in ['PowerLifecycle','SgSleepMode','SgRetryKind','SgSleepTask','SgSleepTarget','SgResumeResult','SgSuspendResult','SgWorkItem','SgDirectoryResumePurpose']:text(n+'.inc',type_body(n)+'\n')
    lease_start='template<class Current>\nstatic ymcc::sleepLegacyResume::ResumeLease sgAcquireResumeFiles('
    assert source.count(lease_start)==1
    a=source.index(lease_start); b=source.index('\n}\n',a)+2
    lease=source[a:b]
    text('sgAcquireResumeFiles.inc',lease+'\n')
    fragments.append(dict(name='sgAcquireResumeFiles',sha256=hashlib.sha256(lease.encode()).hexdigest(),verbatim=True))
    text('SgWork.inc', 'enum class SgWork : uint8_t { Suspend, WakeAutomatic, WakeSuspend, WakeHibernate };\n')
    returned=(args.source.resolve().parent/'sleep_returned_pause_native.inc').read_text(encoding='utf-8-sig')
    for n in ['sgOrdinaryPauseCurrent','sgReturnedPauseCurrent','sgQueueReturnedPause']:text(n+'.inc',function(n,returned,'native/sleep_returned_pause_native.inc')+'\n')
    delays=re.findall(r'^static constexpr ULONGLONG SG_SLEEP_RESUME_RETRY_DELAYS_MS\[\] = [^;]+;',source,re.M);assert len(delays)==1;text('resume_delays.inc',delays[0]+'\n')
    header=(root/'native/wake_game_recovery.h').read_text(encoding='utf-8-sig')
    strict_declarations=[]
    for name in ('sgForgetAcceptedWakeMarker','sgConfirmStrictWakeLeaseResume'):
        if name in header:
            declaration=re.findall(r'^static (?:void|bool) '+name+r'\([^\n]+\);$',source,re.M)
            assert len(declaration)==1,name
            strict_declarations.extend(declaration)
    text('strict_resume_declarations.inc','\n'.join(strict_declarations)+'\n')
    hm=c['mask'](header)
    def header_type(name):
        ms=list(re.finditer(r'struct '+name+r'\b[^;{}]*\{',hm));assert len(ms)==1
        a=ms[0].start();b=hm.index('{',a)+1;d=1
        while d:d+=(hm[b]=='{')-(hm[b]=='}');b+=1
        assert hm[b]==';';return header[a:b+1]
    retirement=re.findall(r'^enum class SgWakeMarkerRetirement \{[^\n]*\};$',header,re.M);assert len(retirement)==1
    batch=[header_type('SgWakeRecoveryEntry'),header_type('SgWakeRecoveryBatch'),'static SgWakeRecoveryBatch g_sgWakeRecovery;',retirement[0]]
    parked=re.findall(r'^static std::atomic<bool> g_sgWakeRecoveryParked\{false\};$',header,re.M);assert len(parked)==1
    batch.extend(parked)
    if 'SgWakeAcceptedMarker' in header:
        map_include=re.findall(r'^#include <map>$',header,re.M);assert len(map_include)==1
        batch[:0]=map_include
        key=re.findall(r'^using SgWakeAcceptedKey = [^;]+;$',header,re.M);assert len(key)==1
        table=re.findall(r'^static std::map<[^;]+> g_sgWakeAcceptedMarkers;$',header,re.M);assert len(table)==1
        limit=re.findall(r'^static constexpr size_t kSgWakeAcceptedMarkerLimit = [^;]+;$',header,re.M);assert len(limit)==1
        batch.extend([key[0],header_type('SgWakeAcceptedMarker'),table[0],limit[0]])
        helpers=['sgWakeAcceptedMarkerMatches','sgWakeReserveAcceptedMarkerRoom']
        helpers.extend(name for name in ('sgForgetAcceptedWakeMarker','sgConfirmStrictWakeLeaseResume') if name in header)
        for name in helpers:
            body=function(name,header,'native/wake_game_recovery.h')
            if name=='sgWakeReserveAcceptedMarkerRoom':
                body='template<class Current>\n'+body
                assert header.count(body)==1
            batch.append(body);fragments.append(dict(name=name,sha256=hashlib.sha256(body.encode()).hexdigest(),verbatim=True))
    names=['sgWakeGameRecoveryHasWork','sgWakeRecoveryMarker','sgWakeRecoveryClearTarget','sgCanceledSleepRecoveryBoundaryCurrent','sgCanceledSleepRecoveryEntryCurrent','sgQueueCanceledSleepLeaseRecoveryUnlocked','sgReconcileSleepGameFlagsAfterRecoveryCancel','sgRetireAcceptedWakeMarkerUnlocked','sgCancelWakeGameRecoveryUnlocked','sgWakeGameRecoveryCanWaitForNextWakeUnlocked','sgInterruptWakeGameRecoveryUnlocked','sgParkedGameWakeInterrupted','sgCaptureGameWakeReady','sgGameWakeReady','sgParkGameWakeIfNotReady','sgWakeGameRecoveryWaitMs','sgWakeGameRecoveryTick','sgPrepareWakeRecoveryForSleep']
    for name in names:
        if name=='sgInterruptWakeGameRecoveryUnlocked':
            for helper in ('sgWakeGameRecoveryTerminalBoundaryUnlocked','sgFailWakeGameRecoveryUnlocked'):
                body=function(helper,header,'native/wake_game_recovery.h')
                batch.append(body);fragments.append(dict(name=helper,sha256=hashlib.sha256(body.encode()).hexdigest(),verbatim=True))
            helper='sgGameWakeBoundaryChanged' if 'sgGameWakeBoundaryChanged(' in header else 'sgUndispatchedGameWakeBoundaryChanged'
            body=function(helper,header,'native/wake_game_recovery.h')
            batch.append(body);fragments.append(dict(name=helper,sha256=hashlib.sha256(body.encode()).hexdigest(),verbatim=True))
        if name=='sgWakeGameRecoveryTick':
            helper='sgWakeGameRecoveryTickUnlocked';body=function(helper,header,'native/wake_game_recovery.h')
            batch.append(body);fragments.append(dict(name=helper,sha256=hashlib.sha256(body.encode()).hexdigest(),verbatim=True))
        body=function(name,header,'native/wake_game_recovery.h');batch.append(body);fragments.append(dict(name=name,sha256=hashlib.sha256(body.encode()).hexdigest(),verbatim=True))
    text('wake_batch.inc','\n'.join(batch)+'\n')
    text('sgNotifyWork.inc',function('sgNotifyWork')+'\n')
    resume=(root/'native/wake_game_resume.h').read_text(encoding='utf-8-sig')
    offsets=re.findall(r'^inline constexpr (?:ULONGLONG attemptOffsetsMs\[\]|size_t attemptCount) = [^;]+;',resume,re.M);assert len(offsets)==2
    text('wake_offsets.inc','\n'.join(offsets)+'\n')
    for name in ['sgWaitForWork','sgServiceEntryObservationHandoff','sgWorkLoop','sgStartWorkThread','sgStopWorkThread','powerLifecycleMatches','sgReadFile','sgReadResumeMarkerChecked','sgMarkerCreated','sgMarkerIsSuspended','nativeValveValidateOwnedSuspendedTarget']:
        body=function(name);text(name+'.inc',body+'\n');fragments.append(dict(name=name,sha256=hashlib.sha256(body.encode()).hexdigest(),verbatim=True))
    ports=Path(__file__).with_name('abort_cancel_ports.inc').read_text(encoding='utf-8-sig')
    if 'g_sgKernelCycles.claimLongScreenOffExit(' in source:
        cycle_path=root/'native/sleep_kernel_cycle_native.inc'
        cycle=cycle_path.read_text(encoding='utf-8-sig')
        declarations=re.findall(r'^static (?:ymcc::sleeppower::KernelCycleObservations g_sgKernelCycles;|std::atomic<ULONGLONG> g_sgKernelSystemWake\{0\};)$',cycle,re.M)
        assert len(declarations)==2
        dependencies='#include "sleep_kernel_cycle.h"\n#include "sleep_timeout_policy.h"\n'+'\n'.join(declarations)+'\n'
        for name in ('sgKernelEntryIdentity','sgKernelExitIdentity'):
            if name+'(' in source:
                body=function(name,cycle,'native/sleep_kernel_cycle_native.inc')
                dependencies+=body+'\n'
                fragments.append(dict(name=name,origin='native/sleep_kernel_cycle_native.inc',sha256=hashlib.sha256(body.encode()).hexdigest(),verbatim=True))
        text('kernel_cycle_journal_state.inc',dependencies)
        fragments.append(dict(name='kernel_cycle_journal_state',origin='native/sleep_kernel_cycle_native.inc',sha256=hashlib.sha256('\n'.join(declarations).encode()).hexdigest(),verbatim=True))
        anchor='#include "sgKernelPowerEventCallback.inc"'
        assert ports.count(anchor)==1
        ports=ports.replace(anchor,'#include "kernel_cycle_journal_state.inc"\n'+anchor)
    if 'static bool sgRenderKernelPowerIngress(' in source:
        a=source.index('struct SgOriginalWakeDelivery {');b=source.index('static DWORD WINAPI sgKernelPowerEventCallback(',a)
        ingress=source[a:b]
        text('kernel_wake_ingress.inc',ingress)
        fragments.append(dict(name='kernel_wake_ingress',sha256=hashlib.sha256(ingress.encode()).hexdigest(),verbatim=True))
        anchor='#include "sgKernelPowerEventCallback.inc"'
        assert ports.count(anchor)==1
        ports=ports.replace(anchor,'#define YMCC_FIXTURE_KERNEL_WAKE_READBACK 1\n#include "sleep_kernel_cycle.h"\n#include "kernel_wake_ingress.inc"\n'+anchor)
    post_name='sgPostKernelWake' if 'static void sgPostKernelWake(' in source else 'sgPostS4Wake'
    if f'static void {post_name}(' in source:
        first='static std::mutex g_sgFailedS4Mutex;'
        last=f'static void {post_name}('
        assert source.count(first)==source.count(last)==1
        a,b=source.index(first),source.index(last)
        assert a<b
        post=function(post_name)
        drain=function('sgRedeliverS4Wake')
        text('sgPostS4Wake.inc',source[a:b]+post+'\n'+drain+'\n')
        fragments.append(dict(name=post_name,sha256=hashlib.sha256(post.encode()).hexdigest(),verbatim=True))
        fragments.append(dict(name='sgRedeliverS4Wake',sha256=hashlib.sha256(drain.encode()).hexdigest(),verbatim=True))
        anchor='#include "sgKernelPowerEventCallback.inc"'
        assert ports.count(anchor)==1
        ingress_anchor='#include "kernel_wake_ingress.inc"'
        first_include=ingress_anchor if ingress_anchor in ports else anchor
        ports=ports.replace(first_include,'static void sleepPowerArmRedelivery(HWND){throw std::runtime_error("unexpected retry timer in cancellation selector");}\n#define YMCC_FIXTURE_KERNEL_WAKE_REDELIVERY 1\n#pragma push_macro("SendMessageW")\n#define SendMessageW fakeSendKernelWake\n#include "sgPostS4Wake.inc"\n#pragma pop_macro("SendMessageW")\n'+first_include)
    import runpy
    ports=runpy.run_path(str(Path(__file__).with_name('retained_delivery.py')))['bind_abort'](c,ports,returned,text)
    text('Fixture.cpp',ports)
    generated={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file()}
    text('TASK.json',json.dumps(dict(selection=selected,cases=cases,fragments=fragments,generated=generated,defaultParentExecuted=False,physicalCalls=None if selected and selected.startswith('owned-lease') else 0,threadQueryIsControlledLeaf=True),indent=2)+'\n')
    toolchain_js='import {calibrationToolchainPreamble} from "./tools/fixtures/hc_reviewed_calibration/toolchain.mjs";process.stdout.write(calibrationToolchainPreamble(process.argv[1]));'
    vcvars=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
    pre=subprocess.run(['node','--input-type=module','-e',toolchain_js,str(vcvars)],cwd=root,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',check=True,timeout=10000).stdout
    recipe=pre+'cl /nologo /std:c++20 /utf-8 /EHsc /MT /W4 /WX /sourceDependencies "'+str(out/'dependencies.json')+'" /I"'+str(out)+'" /I"'+str(root/'native')+'" /I"'+str(root/'deps/json')+'" "'+str(out/'Fixture.cpp')+'" /Fo"'+str(out/'Fixture.obj')+'" /Fe"'+str(out/'Fixture.exe')+'" /link /VERBOSE:LIB user32.lib crypt32.lib\n'
    text('compile.cmd',recipe)
    if getattr(args,'prepare_only',False):
        print(json.dumps(dict(status='PREPARED_NOT_RUN',source=str(out/'Fixture.cpp'),selection=selected)));return 0
    built=subprocess.run([os.environ.get('COMSPEC','cmd.exe'),'/d','/c',str(out/'compile.cmd')],cwd=out,capture_output=True,timeout=90000)
    write('compile.log',built.stdout+built.stderr)
    if built.returncode:raise RuntimeError('cancellation body compile failed: '+str(built.returncode))
    results=[]
    for stage,event in cases:
        done=subprocess.run([str(out/'Fixture.exe'),stage,event],cwd=out,capture_output=True,text=True,encoding='utf-8',timeout=15000)
        text(stage+'-'+event+'.stdout',done.stdout);text(stage+'-'+event+'.stderr',done.stderr)
        if not done.stdout:raise RuntimeError('cancellation process did not produce result')
        row=json.loads(done.stdout);row['exitCode']=done.returncode;results.append(row)
        if done.returncode or row['callbackPublished']!=row.get('callbackExpected',True) or not row['safe'] or not row['normal'] or (stage=='owned-lease' and not row['ownedResourcesReturned']):
            text('RESULT.json',json.dumps(dict(status='FAIL',results=results,cases=cases),indent=2)+'\n')
            raise RuntimeError('cancellation boundary failed: '+stage+'/'+event)
    result=dict(status='PASS_SCOPED_OWNED_LEASE_COLD_BASELINE_OPEN' if selected and selected.startswith('owned-lease') else 'PASS_DIRECTED_CANCEL_BOUNDARIES',selected=selected,cases=cases,results=results,defaultParentExecuted=False,physicalCalls=sum(row.get('productNtCalls',0) for row in results),ownedProcessCallsOnly=True,coldWholeProcessBaselinePassed=all(row.get('coldProcessBaselineEqual',True) for row in results))
    text('RESULT.json',json.dumps(result,indent=2)+'\n');print(json.dumps(dict(status=result['status'],cases=len(results),selected=selected)))
    return 0
