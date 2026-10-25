#!/usr/bin/env python3
"""Actual capture Stop wait/handle proof; no product/device/Host execution.

Current typed Stop, its header/globals and base-event classifier are copied
from production. The historical void Stop remains a hash-locked negative.
Current-source acceptance requires actual execution of the whole current Stop. Retained ownership is
not disposal: bounded destruction of a genuinely stalled worker stays unproven.
The stop-request leaf is an explicit atomic-only fake, with no startup CV proof.
"""
from __future__ import annotations
import argparse,datetime as dt,hashlib,importlib.util,json,os,re,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('capture_retirement',ROOT/'tools/input_capture_worker_retirement_selftest.py')
ret=importlib.util.module_from_spec(spec);spec.loader.exec_module(ret)
BASELINE_SHA='abb99773f49f3dabde96c5bbbfcf0c0917430579d6f2c501f1244c4b702e4812'
SOURCES=('native/main.cpp','native/input_capture_completion.h','native/input_capture_startup_wait.h','deps/json/json.hpp','tools/input_capture_worker_retirement_selftest.py','tools/input_timer_evidence_adapter_selftest.py',
         'tools/input_capture_stop_wait_selftest.py','tools/fixtures/input_capture_timer_retirement/stop_wait_baseline.json','tools/fixtures/input_capture_timer_retirement/stop_wait_fixture.cpp.in','tools/fixtures/input_capture_timer_retirement/stop_wait_kernel_fixture.cpp.in',
         'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json')
sha=lambda b:hashlib.sha256(b).hexdigest()
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def need(c,m):
    if not c:raise RuntimeError(m)
def candidate_stop(old):
    nl='\r\n' if '\r\n' in old else '\n'
    before=nl.join(['        WaitForSingleObject(g_inputCaptureThread, 2000);','        CloseHandle(g_inputCaptureThread);','        g_inputCaptureThread = nullptr;'])
    after=nl.join([
        '        const DWORD waitResult = WaitForSingleObject(g_inputCaptureThread, 2000);',
        '        const DWORD waitError = waitResult == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;',
        '        bool handleClosed = false;',
        '        DWORD closeError = ERROR_SUCCESS;',
        '        if (waitResult == WAIT_OBJECT_0) {',
        '            handleClosed = CloseHandle(g_inputCaptureThread) != FALSE;',
        '            if (handleClosed) g_inputCaptureThread = nullptr;',
        '            else closeError = GetLastError();',
        '        }',
        '        try {',
        '            appendNativeLifecycleLog("input-capture-worker-stop", {',
        '                {"threadWaitBudgetMs", 2000}, {"waitResult", waitResult}, {"waitWin32", waitError},',
        '                {"threadExitVerified", waitResult == WAIT_OBJECT_0},',
        '                {"handleClosed", handleClosed}, {"closeWin32", closeError},',
        '                {"ownerRetained", g_inputCaptureThread != nullptr}',
        '            });',
        '        } catch (...) {}',
    ])
    need(old.count(before)==1,'Historical wait/close block shape changed')
    candidate=old.replace(before,after,1)
    stop_prefix=nl.join(['static void inputCaptureStop() {','    g_inputCaptureStop.store(true, std::memory_order_release);'])
    need(candidate.startswith(stop_prefix),'Historical first stop publication changed')
    return candidate.replace(stop_prefix,nl.join(['static void inputCaptureStop() {','    inputCaptureRequestStop();']),1)
def classifier_pair(current):
    nl='\r\n' if '\r\n' in current else '\n';entry=nl+'        "input-capture-worker-stop",'
    legacy=current.replace(entry,'',1)
    need('"input-capture-worker-stop"' not in legacy,'Unreviewed log classifier admission')
    anchor='    static const char* const kBaseEvents[] = {'
    need(legacy.count(anchor)==1,'Unique base registry required')
    # Keep the actual complete classifier byte-for-byte; the old route removes
    # only the target event, including when other base events share its line.
    return legacy,current
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--output-root',required=True,type=Path);ap.add_argument('--prepare-only',action='store_true');ap.add_argument('--profile',choices=['Release','Checked']);args=ap.parse_args()
    out=args.output_root.resolve();run=out/('r-'+dt.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
    receipt={'schemaVersion':1,'pass':False,'evidenceComplete':False,'productionOwnershipCheckAccepted':False,'status':'RUNNING','run':str(run),
      'scope':'Whole actual inputCaptureStop and default-log classifier; explicit wait/close/log/Host/family leaves with joined real owned fixture thread',
      'productionStartupWaitExecuted':False,'startupSynchronizationProven':False,
      'runtimeStopRequestLeaf':'explicit atomic-only fake; no StartupWait mutex/CV/notification',
      'boundedStalledProductionDestructionProven':False,'realDeviceDriverServiceWinmmOrHost':False,'profiles':[],'kernelProfiles':[]}
    def publish():save(run/'RESULT.json',receipt);save(out/'RESULT.json',receipt)
    try:
        source={n:(ROOT/n).read_bytes() for n in SOURCES};receipt['sourceHashes']={n:sha(b) for n,b in source.items()};ret.verify_freeze(source,receipt)
        for n,b in source.items():p=run/'source'/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
        text=source['native/main.cpp'].decode('utf-8-sig');actual=ret.function(text,'inputCaptureStop');classified=ret.function(text,'nativeLifecycleEventIsBase')
        baseline=json.loads(source['tools/fixtures/input_capture_timer_retirement/stop_wait_baseline.json'])
        old=baseline['function'];need(sha(old.encode('utf-8'))==baseline['functionSha256']==BASELINE_SHA,'Frozen historical source identity mismatch')
        legacy_class,safe_class=classifier_pair(classified['text'])
        current_safe='ymcc::captureLifecycle::StopReceipt' in actual['text'] and classified['text']==safe_class
        need(current_safe,'Current Stop must expose the production typed receipt and ordinary log classifier')
        receipt['actualFunctions']={n:{'line':x['line'],'sha256':x['sha256']} for n,x in [('inputCaptureStop',actual),('nativeLifecycleEventIsBase',classified)]}
        receipt['currentProductionCheckedWait']=current_safe
        checked_stop=actual['text']
        request_stop=ret.function(text,'inputCaptureRequestStop')
        receipt['sourceBoundStopRequestHelper']={'line':request_stop['line'],'sha256':request_stop['sha256'],'executed':False}
        receipt['checkedStopMatchesWholeProductionFunction']=checked_stop==actual['text']
        receipt['historicalNegativeFunctionSha256']=BASELINE_SHA
        receipt['actualRetireResourcesBodyExecuted']=False
        receipt['actualCaptureGlobalsSha256']=sha(ret.capture_declarations(text).encode())
        compiler,env=ret.helper.compiler_environment(run);receipt['compiler']={'path':str(compiler),'sha256':sha(compiler.read_bytes()),'vcvarsPath':str(ret.helper.VCVARS),'vcvarsSha256':sha(ret.helper.VCVARS.read_bytes()),'sdkVersion':env.get('WindowsSDKVersion')}
        template=source['tools/fixtures/input_capture_timer_retirement/stop_wait_fixture.cpp.in'].decode('utf-8')
        for profile,flags in [('Release',['/O2','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/MTd'])]:
            if args.profile and args.profile!=profile:continue
            for route,stop,classification,mode in [('legacy',old,legacy_class,'legacy'),('checked',checked_stop,safe_class,'safe')]:
                folder=run/(profile+'-'+route);folder.mkdir();cpp=folder/'stop.cpp';exe=folder/'stop.exe'
                cpp.write_text(ret.bind_stop(template,text,stop,classification),encoding='utf-8')
                argv=[compiler,'/nologo','/std:c++20','/utf-8','/EHsc','/permissive-','/W4',*flags,'/I'+str(run/'source/native'),'/I'+str(run/'source/deps/json'),cpp,'/Fo'+str(folder/'stop.obj'),'/Fe'+str(exe)]
                build=ret.helper.execute(argv,folder,env,folder/'build.log',90);need(build.returncode==0,'Stop consumer compile failed: '+str(folder/'build.log'))
                if args.prepare_only:
                    receipt['profiles' if 'kernel-' not in folder.name else 'kernelProfiles'].append({'profile':profile,'route':route,'compiled':True,'executed':False});publish();continue
                proc=ret.helper.execute([exe,mode],folder,env,folder/'run.log',20);need(proc.returncode==0,'Stop consumer runtime failed: '+str(folder/'run.log'))
                value=json.loads(proc.stdout.strip().splitlines()[-1]);need(value['pass'] and value['failures']==0 and len(value['cases'])==24,'Actual Stop scenario assertions incomplete')
                need(all(c['ownedFixtureThreadJoined'] for c in value['cases']),'Owned fixture thread was not joined')
                row={'profile':profile,'route':route,'pass':True,'isCurrentProductionRoute':current_safe==(route=='checked'),
                     'checks':value['checks'],'cases':value['cases'],'executableSha256':sha(exe.read_bytes()),'sourceSha256':sha(cpp.read_bytes())}
                if route=='legacy':
                    need(sum(c['lostUnverifiedOwner'] for c in value['cases'])==16,'Historical lost-owner scenarios must reproduce')
                    negative=ret.helper.execute([exe,'safe'],folder,env,folder/'legacy-current-contract-negative.log',20)
                    rejected=json.loads(negative.stdout.strip().splitlines()[-1]);need(negative.returncode==1 and not rejected['pass'] and
                      'CHECK_FAILED unproven or unclosed worker handle remains owned' in negative.stderr and
                      'CHECK_FAILED no live-worker handle retirement after unproven wait' in negative.stderr,'Old Stop must fail current ownership checks')
                    row.update(legacyRejectedAsSafe=True,negativeExitCode=1,negativeFailures=rejected['failures'])
                else:
                    need(not any(c['lostUnverifiedOwner'] or c['liveHandleCloses'] for c in value['cases']),'Checked Stop lost owner or retired live handle')
                    need(all(not c['shouldRetain'] or c['lateReclaimedAfterSignal'] for c in value['cases']),'Retained ownership did not settle after actual fixture signal')
                receipt['profiles'].append(row);print(profile+' '+route+': '+str(value['checks'])+' checks, 24 actual-source scenarios',flush=True);publish()
        # Actual Windows kernel wait/close runs only on private test-owned
        # threads and events. The whole production Stop remains verbatim.
        kernel_template=source['tools/fixtures/input_capture_timer_retirement/stop_wait_kernel_fixture.cpp.in'].decode('utf-8')
        authority=json.loads(source['tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'])
        need(Path(authority['native']['compiler']).resolve()==compiler.resolve(),'Kernel leaf compiler must match reviewed declaration')
        kernel_env=dict(env);kernel_env['INCLUDE']=';'.join(authority['native']['include']);kernel_env['LIB']=';'.join(authority['native']['lib'])
        for profile,flags in [('Release',['/O2','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/MTd'])]:
            if args.profile and args.profile!=profile:continue
            for route,stop,classification,mode in [('legacy',old,legacy_class,'legacy'),('checked',checked_stop,safe_class,'safe')]:
                folder=run/('kernel-'+profile+'-'+route);folder.mkdir();cpp=folder/'kernel.cpp';exe=folder/'kernel.exe'
                cpp.write_text(ret.bind_stop(kernel_template,text,stop,classification),encoding='utf-8')
                argv=[compiler,'/nologo','/std:c++20','/utf-8','/EHsc','/permissive-','/W4',*flags,'/I'+str(run/'source/native'),'/I'+str(run/'source/deps/json'),cpp,'/Fo'+str(folder/'kernel.obj'),'/Fe'+str(exe),'/link','kernel32.lib','/INCREMENTAL:NO']
                build=ret.helper.execute(argv,folder,kernel_env,folder/'build.log',90);need(build.returncode==0,'Kernel Stop consumer compile failed: '+str(folder/'build.log'))
                if args.prepare_only:
                    receipt['profiles' if 'kernel-' not in folder.name else 'kernelProfiles'].append({'profile':profile,'route':route,'compiled':True,'executed':False});publish();continue
                proc=ret.helper.execute([exe,mode],folder,kernel_env,folder/'run.log',45);need(proc.returncode==0,'Kernel Stop consumer assertions failed: '+str(folder/'run.log'))
                observed=json.loads(proc.stdout.strip().splitlines()[-1])
                need(observed['pass'] and observed['failures']==0 and len(observed['cases'])==2 and observed['realKernelWaitAndClose'] and observed['productionStopFunctionExecuted'],'Actual kernel Stop observations incomplete')
                need(observed['ownedThreadJoined'] and observed['ownedEventsRemaining']==0 and observed['ownedHandlesRemaining']==0 and observed['ownedThreadsRemaining']==0 and observed['cooperativeCompletion'] and not observed['processExitUsedAsCleanup'],'Private kernel ownership did not positively settle')
                need(all(c['controllerJoinHandleDuplicatedBeforeStop'] and c['ownedThreadJoined'] and c['blockedInvalidApiAttempts']==0 and c['workerCooperativeCompletion'] for c in observed['cases']),'Kernel leaf called invalid handles or lost actual join proof')
                held=[c for c in observed['cases'] if c['case']=='worker-held-until-controller-release'];need(len(held)==1,'Unique actual kernel timeout case missing')
                timed=held[0];need(timed['firstStopWait']['waitResult']==258 and timed['workerStillIncompleteAtFirstReturn'],'Thread still-live proof must come from actual kernel timeout')
                if route=='legacy':need(timed['lostUnverifiedOwner'] and timed['liveHandleCloses']==1,'Actual legacy kernel ownership gap must reproduce')
                else:need(timed['retainedAtFirstReturn'] and not timed['lostUnverifiedOwner'] and timed['liveHandleCloses']==0 and timed['secondStopAfterControllerJoin'] and timed['lateReclaimedAfterSignal'],'Checked kernel ownership did not preserve and later reclaim the actual handle')
                receipt['kernelProfiles'].append({'profile':profile,'route':route,'pass':True,'isCurrentProductionRoute':current_safe==(route=='checked'),
                    'checks':observed['checks'],'cases':observed['cases'],'realKernelWaitAndClose':True,'ownedThreadJoined':True,'ownedEventsRemaining':0,'ownedHandlesRemaining':0,'ownedThreadsRemaining':0,
                    'productionStopFunctionExecuted':True,'processExitUsedAsCleanup':False,'executableSha256':sha(exe.read_bytes()),'sourceSha256':sha(cpp.read_bytes())})
                print(profile+' kernel '+route+': '+str(observed['checks'])+' checks; 2 actual Windows wait/close scenarios',flush=True);publish()
        need(all((ROOT/n).read_bytes()==b for n,b in source.items()),'Stop source changed during test')
        if args.prepare_only:
            receipt.update(status='PREPARED_NOT_EXECUTED',sourceStable=True);publish();return 0
        receipt.update(sourceStable=True,evidenceComplete=True,productionOwnershipCheckAccepted=current_safe,pass_=current_safe)
        receipt['pass']=receipt.pop('pass_');receipt['status']='PASS' if current_safe else 'PRODUCTION_STOP_WAIT_OWNERSHIP_GAP_CONFIRMED_CANDIDATE_ONLY'
        receipt['remaining']=['real worker/Host cleanup completion','bounded destruction when production worker stalls','actual exit continuation is verified separately','cancellation failure retirement','real startup stop-request synchronization (explicit atomic-only fake leaf)']
        receipt['checks']=sum(row['checks'] for row in receipt['profiles']);receipt['kernelChecks']=sum(row['checks'] for row in receipt['kernelProfiles']);receipt['ordinaryStopLogClassified']=classified['text']==safe_class;publish();print(receipt['status']+' RESULT='+str(out/'RESULT.json'));return 0 if current_safe else 2
    except Exception as error:receipt['status']='FAIL';receipt['error']=str(error);publish();print(str(error),file=sys.stderr);return 1
if __name__=='__main__':raise SystemExit(main())