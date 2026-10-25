#!/usr/bin/env python3
"""Actual source timer callback/worker retirement under explicit memory OS leaves.

No product, real WinMM timer, input device, Host, service, driver or sleep runs.
The production capture init/shutdown and BUS leaves are substituted; ordering
around those leaves is executed by the full original worker and callback.
Startup-delay is an explicit immediate fake leaf. The real startup header,
helpers and Start/Stop are source-bound only; startup synchronization is not tested.
Current worker (including cancellation receipts, retained events and period debt)
is compiled verbatim. Historical flag/fault-stop mutations remain explicit
negative controls. No complete persistent lifecycle acceptance is claimed.
"""
from __future__ import annotations
import argparse
import datetime as dt
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import sys

ROOT=Path(__file__).resolve().parents[1]
HELPER=ROOT/'tools/input_timer_evidence_adapter_selftest.py'
spec=importlib.util.spec_from_file_location('actual_timer_adapter_tools',HELPER)
helper=importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
SOURCES=('native/main.cpp','native/input_timer_evidence.h','native/input_capture_startup_wait.h','native/input_capture_completion.h','deps/json/json.hpp',
         'tools/input_timer_evidence_adapter_selftest.py',
         'tools/input_capture_worker_retirement_selftest.py',
         'tools/fixtures/input_capture_timer_retirement/actual_worker_fixture.cpp.in')
STARTUP_FUNCTIONS=('inputCaptureAwaitStartupDelay','inputCaptureRequestStop',
                   'inputCaptureResetStartupStop','inputCaptureStart','inputCaptureStop')
GLOBALS=('g_inputCaptureStop','g_inputCaptureBusTickMx','g_inputCaptureBusBusySkips',
         'g_inputCaptureBusTickGlobal','g_inputTimerEvidence','g_inputTimerEvidenceNextOwner',
         'g_tickCostBusySkipCount','g_tickCostWindowTicks')
def sha(data):return hashlib.sha256(data).hexdigest()
def write_json(file,value):file.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def need(ok,message):
    if not ok:raise RuntimeError(message)
def function(text,name):
    code=helper.mask_cpp(text)
    matches=list(re.finditer(r'^(?:\[\[[^\n]*?\]\]\s*)?static[^\n;{}]*\b'+re.escape(name)+r'\s*\([^;{}]*\)\s*(?:noexcept\s*)?\{',code,re.M))
    need(len(matches)==1,'Expected one whole actual definition '+name)
    start=matches[0].start();end=code.index('{',start)+1;depth=1
    while depth:
        need(end<len(code),'Unclosed source '+name)
        depth+=(code[end]=='{')-(code[end]=='}');end+=1
    return {'text':text[start:end],'line':text.count('\n',0,start)+1,'sha256':sha(text[start:end].encode('utf-8'))}
PREVIOUS_FREEZE_MAIN_SHA='bfd4836ec2f973f2b1ca958e3e0deb7366d0bee42b5517f04c2ad6d831e99c3e'
# Historical identities are evidence, not alternate currently approved inputs.
HISTORICAL_FREEZE_MAIN_SHAS=(
    '50ade4c88674fac353bf2445033fc6b34becc76ad7815da4ea0d28e8d6a307db',
    PREVIOUS_FREEZE_MAIN_SHA,
    '01a00d9424e1ccb26af3daa88f6ba3be7f79fdf3eb99b20ec467753c804d1343',
    '649d1e0c1c6e8efff6154516df129b762a0e01d2a265702ae7d9b20d5ca48392',
)
# Current whole-main identity explicitly authorized for the final 20-group run.
# Never derive an approved identity from the live source or a caller's argument.
# Explicit offline frozen22 batch; original authority retained below.
# This does not approve installed application or hardware behavior.
PREVIOUS_20_GROUP_MAIN_SHA='27647188abf7717f62a14060c04908e393b0a3da00fea5eb335c3546eb1b1a7d'
FREEZE_MAIN_SHA='b7545386f6bd7b0f8375108fcfd98e406621059a18fef8bc0eea70d63b39cb7d'
def verify_freeze(source,receipt,expected_main_sha256=None):
    actual=sha(source['native/main.cpp'])
    expected=FREEZE_MAIN_SHA if expected_main_sha256 is None else expected_main_sha256
    receipt['sourceFreezeExpectedMainSha256']=expected
    receipt['sourceFreezeApprovedMainSha256']=FREEZE_MAIN_SHA
    receipt['previousSourceFreezeMainSha256']=PREVIOUS_FREEZE_MAIN_SHA
    receipt['historicalSourceFreezeMainSha256s']=list(HISTORICAL_FREEZE_MAIN_SHAS)
    receipt['sourceFreezeIdentityApproved']=expected==FREEZE_MAIN_SHA
    receipt['sourceFreezeMainMatched']=actual==expected
    need(receipt['sourceFreezeIdentityApproved'],
         'Requested source freeze is not the currently explicitly approved identity; historical or arbitrary SHA cannot authorize execution')
    need(receipt['sourceFreezeMainMatched'],
         'Production main differs from the explicitly confirmed source freeze; await revised identity')
def capture_declarations(text):
    # Copy the actual production definitions, including newly added owners.
    declarations=re.findall(r'^static[^\n;{}]*\bg_capture\w+\b[^\n;]*;',text,re.M)
    declarations=[v for v in declarations if not re.search(r'\bg_capture\w+\s*\(',v)]
    need(len(declarations)>=8,'Missing production capture ownership globals')
    return '\n'.join(declarations)
CAPTURE_RUNTIME=r"""
static void resetCaptureFixture(){
    g_captureAdmission.state.store(ymcc::captureLifecycle::AdmissionState::Idle);
    g_captureStopOwner.clear();g_captureStopIssued.store(false);g_captureGeneration.store(7);g_captureShutdownAttempted.store(false);
    g_captureMmTimerRetired.store(true);g_captureResolutionOwed.store(false);
    g_captureRetainedEvents.fill(nullptr);g_captureEventOverflow.store(false);g_captureExitReleased.store(false);
}
template<class F>static ymcc::captureLifecycle::StopReceipt observeCaptureStop(F&& function){
    if constexpr(std::is_void_v<decltype(function())>){function();return {};}
    else return function();
}
"""
def capture_support(text):
    return capture_declarations(text)+CAPTURE_RUNTIME
def bind_stop(template,text,stop,classifier):
    return template.replace('@CAPTURE_SUPPORT@',capture_support(text)).replace('@STOP_FUNCTION@',stop).replace('@BASE_CLASSIFIER@',classifier)

def extract(text):
    parts={name:function(text,name) for name in ('inputCaptureTimerEventProc','inputCaptureWorkerFault','inputCaptureThreadProc')}
    parts['InputTimerCallbackEvidenceScope']=helper.extract(text)['scope']
    globals=[]
    for name in GLOBALS:
        matches=list(re.finditer(r'^static[^\n;]*\b'+re.escape(name)+r'\b[^;]*;',text,re.M))
        need(len(matches)==1,'Expected one actual global '+name)
        globals.append(matches[0].group())
    return parts,'\n'.join(globals)+'\n'+capture_declarations(text)+'\n'+function(text,'inputCaptureRetainEvent')['text']
def render(template,parts,globals,worker):
    content=template
    for tag,name in [('SCOPE','InputTimerCallbackEvidenceScope'),('CALLBACK','inputCaptureTimerEventProc'),('FAULT','inputCaptureWorkerFault')]:
        need(content.count('@'+tag+'@')==1,'Fixture tag '+tag)
        p=parts[name];content=content.replace('@'+tag+'@','\n#line '+str(p['line'])+' "actual-main.cpp"\n'+p['text']+'\n#line 1 "fixture-generated.cpp"\n')
    content=content.replace('@GLOBALS@',globals).replace('@WORKER@',worker)
    need(not re.search(r'@[A-Z]+@',content),'Unexpanded actual source tag')
    return content

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output-root',required=True,type=Path);parser.add_argument('--prepare-only',action='store_true');parser.add_argument('--profile',choices=['Release','Checked']);args=parser.parse_args()
    output=args.output_root.resolve();run=output/('run-'+dt.datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'-'+str(os.getpid()));run.mkdir(parents=True)
    receipt={'schemaVersion':1,'pass':False,'evidenceComplete':False,'status':'RUNNING','run':str(run),
             'productionRetirementAccepted':False,'physicalInputOrGameConsumptionProven':False,
             'realWinmmOrDeviceCalls':False,'privateOwnedThreadsExecuted':True,
             'scope':'Whole verbatim current callback + WorkerFault + worker, real Recorder and MM-cancel/event-retention/period-owed atomics; explicit memory OS/startup-delay/init/shutdown/BUS/Host leaves',
             'productionStartupWaitExecuted':False,'startupSynchronizationProven':False,
             'profiles':[],'remaining':['all transitive callback/worker lock dependencies','real capture shutdown resources and Host','bounded stalled callback disposal','fourth final cancellation site is source-bound but not reached by these normal/catch schedules','physical sleep, suppression and game consumption','real startup delay/stop/reset synchronization (explicit fake delay leaf)']}
    def publish():write_json(run/'RESULT.json',receipt);write_json(output/'RESULT.json',receipt)
    try:
        sources={n:(ROOT/n).read_bytes() for n in SOURCES}
        receipt['sourceHashes']={n:sha(b) for n,b in sources.items()}
        verify_freeze(sources,receipt)
        for n,b in sources.items():
            f=run/'source'/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(b)
        text=sources['native/main.cpp'].decode('utf-8-sig');parts,globals=extract(text)
        receipt['actualFunctions']={k:{'line':v['line'],'sha256':sha(v['text'].encode('utf-8'))} for k,v in parts.items()}
        retain=function(text,'inputCaptureRetainEvent');receipt['actualFunctions']['inputCaptureRetainEvent']={k:v for k,v in retain.items() if k!='text'}
        receipt['actualCaptureGlobalsSha256']=sha(capture_declarations(text).encode())
        receipt['negativeConstruction']='Current full worker with synchronous cancellation and/or outer fault-stop guards removed; production ownership hooks remain intact'
        receipt['actualRetireResourcesBodyExecuted']=False
        startup_parts={name:function(text,name) for name in STARTUP_FUNCTIONS}
        startup_globals=list(re.finditer(r'^static\s+ymcc::inputCapture::StartupWait\s+g_inputCaptureStartupWait\s*;',text,re.M))
        need(len(startup_globals)==1,'Expected one actual startup wait owner')
        need(text.count('#include "input_capture_startup_wait.h"')==1,'Actual startup header include missing or ambiguous')
        startup_global=startup_globals[0]
        receipt['startupSourceClosure']={
            'header':'native/input_capture_startup_wait.h',
            'headerSha256':receipt['sourceHashes']['native/input_capture_startup_wait.h'],
            'owner':{'line':text.count('\n',0,startup_global.start())+1,'sha256':sha(startup_global.group().encode('utf-8'))},
            'functions':{name:{'line':part['line'],'sha256':part['sha256']} for name,part in startup_parts.items()},
            'binding':'Frozen actual source only; these startup helpers and Start/Stop are not executed by this worker fixture',
            'runtimeLeaf':{'name':'inputCaptureAwaitStartupDelay','implementation':'explicit immediate fake; no StartupWait/CV'}}
        worker=parts['inputCaptureThreadProc']['text']
        need(worker.count('inputCaptureAwaitStartupDelay();')==1,'Actual worker must call the source-bound startup helper once')
        creation=re.search(r'captureTimerId = timeSetEvent\([\s\S]*?\);',worker)
        need(creation is not None,'Actual timeSetEvent creation missing')
        current_sync='TIME_KILL_SYNCHRONOUS' in creation.group()
        need(creation.group().count('TIME_PERIODIC')==1,'Actual periodic creation must be preserved')
        plain_creation=re.sub(r'TIME_PERIODIC\s*\|\s*TIME_KILL_SYNCHRONOUS','TIME_PERIODIC',creation.group())
        need('TIME_KILL_SYNCHRONOUS' not in plain_creation,'Unreviewed cancellation flag expression')
        sync_creation=plain_creation.replace('TIME_PERIODIC','TIME_PERIODIC | TIME_KILL_SYNCHRONOUS')
        stop_store='g_inputCaptureStop.store(true, std::memory_order_release);'
        catch_headers=('} catch (const std::exception& error) {','} catch (...) {')
        # Remove only immediate worker-outer catch stores to construct the old
        # source negative. No logger, callback or nested cleanup catch is edited.
        canonical_worker=worker
        source_nl='\r\n' if '\r\n' in worker else '\n'
        observed_stop_guards=0
        for header in catch_headers:
            match=re.search(re.escape(header)+r'\s*(?:'+re.escape(stop_store)+r'\s*)?inputCaptureWorkerFault',canonical_worker)
            need(match is not None,'Actual outer fault path missing '+header)
            if stop_store in match.group():observed_stop_guards+=1
            canonical_worker=canonical_worker[:match.start()]+header+source_nl+'        inputCaptureWorkerFault'+canonical_worker[match.end():]
        legacy_worker=canonical_worker.replace(creation.group(),plain_creation,1)
        no_stop_worker=canonical_worker.replace(creation.group(),sync_creation,1)
        candidate_worker=no_stop_worker
        for header in catch_headers:
            marker=header+source_nl+'        inputCaptureWorkerFault'
            need(candidate_worker.count(marker)==1,'Unique worker catch mutation')
            candidate_worker=candidate_worker.replace(marker,header+source_nl+'        '+stop_store+source_nl+'        inputCaptureWorkerFault',1)
        receipt['currentProductionFaultStopGuards']=observed_stop_guards
        current_safe=current_sync and observed_stop_guards==2
        if current_safe:need(candidate_worker==worker,'Current production worker must be compiled verbatim')
        elif not current_sync and observed_stop_guards==0:need(legacy_worker==worker,'Legacy production worker must be compiled verbatim')
        else:raise RuntimeError('Partial retirement guard state has no reviewed current-source route')

        receipt['currentProductionSynchronous']=current_sync
        receipt['frozenCandidateOnlyChange']={'before':plain_creation,'after':sync_creation,'productionSourceEdited':False,'candidateFaultStopStores':2}
        compiler,env=helper.compiler_environment(run)
        receipt['compiler']={'path':str(compiler),'sha256':sha(compiler.read_bytes()),'vcvarsPath':str(helper.VCVARS),
                            'vcvarsSha256':sha(helper.VCVARS.read_bytes()),'sdkVersion':env.get('WindowsSDKVersion')}
        template=sources['tools/fixtures/input_capture_timer_retirement/actual_worker_fixture.cpp.in'].decode('utf-8')
        for profile,flags in [('Release',['/O2']),('Checked',['/Od','/RTC1','/D_DEBUG'])]:
            if args.profile and args.profile!=profile:continue
            for route,selected,expectation in [('legacy',legacy_worker,'legacy'),('synchronous',candidate_worker,'safe'),('negative-sync-without-fault-stop',no_stop_worker,'safe')]:
                folder=run/(profile+'-'+route);folder.mkdir();cpp=folder/'actual-worker.cpp';exe=folder/'actual-worker.exe'
                cpp.write_text(render(template,parts,globals,selected),encoding='utf-8')
                argv=[compiler,'/nologo','/std:c++20','/utf-8','/EHsc',('/MTd' if profile=='Checked' else '/MT'),'/permissive-','/W4',*flags,
                      '/I'+str(run/'source/native'),'/I'+str(run/'source/deps/json'),cpp,
                      '/Fo'+str(folder/'actual-worker.obj'),'/Fe'+str(exe)]
                build=helper.execute(argv,folder,env,folder/'build.log',90)
                need(build.returncode==0,'Actual worker fixture compile failed: '+str(folder/'build.log'))
                if args.prepare_only:
                    receipt['profiles'].append({'profile':profile,'route':route,'compiled':True,'executed':False});publish();continue
                proc=helper.execute([exe,expectation]+(['debts'] if route=='synchronous' else []),folder,env,folder/'runtime.log',20)
                value=json.loads(proc.stdout.strip().splitlines()[-1])
                if route=='negative-sync-without-fault-stop':
                    need(proc.returncode==1 and value.get('pass') is False and value.get('failures')==6 and
                         'CHECK_FAILED cancellation observes terminal capture stop' in proc.stderr and
                         'CHECK_FAILED terminal callback does not enter capture BUS leaf' in proc.stderr,
                         'Removing only fault-stop guards must fail at terminal-stop and late-BUS checks')
                    receipt['profiles'].append({'profile':profile,'route':route,'expectedFailureCaught':True,'exitCode':proc.returncode,
                                                'checks':value['checks'],'failures':value['failures'],'cases':value['cases'],
                                                'executableSha256':sha(exe.read_bytes()),'compileSourceSha256':sha(cpp.read_bytes())})
                    print(profile+' '+route+': 6 specified terminal-stop/late-BUS failures caught',flush=True);publish();continue
                need(proc.returncode==0,'Actual worker schedule assertion failed: '+str(folder/'runtime.log'))
                need(value.get('pass') is True and value.get('failures')==0,'Actual worker assertions failed')
                need(len(value['cases'])==6 and all(c['ownedThreadsJoined'] and c['ownedActiveCallbacksAfter']==0 and c['remainingFixtureEvents']==0 and not c['gateTimedOut'] for c in value['cases']),'Owned fixture resources not retired')
                if route=='synchronous':
                    need(len(value['debtCases'])==3 and all(c['ownedThreadsJoined'] and c['remainingFixtureEvents']==0 and not c['processExitUsedAsCleanup'] for c in value['debtCases']),'Actual worker debt schedules must join and dispose explicit memory leaves')
                    receipt.setdefault('workerDebtProfiles',[]).append({'profile':profile,'cases':value['debtCases']})
                legacy_rejection=None
                if route=='legacy':
                    need(all(c['unsafeObserved'] and c['closedEventSignals']==1 for c in value['cases']),'Legacy source defect must reproduce at exact callback event tail')
                    rejection=helper.execute([exe,'safe'],folder,env,folder/'legacy-as-current-negative.log',20)
                    rejected=json.loads(rejection.stdout.strip().splitlines()[-1])
                    need(rejection.returncode==1 and rejected.get('pass') is False and rejected.get('failures')==24 and
                         'CHECK_FAILED candidate no closed-event signal or retired BUS admission' in rejection.stderr,
                         'Legacy worker must fail current retirement contract at the precise late callback boundary')
                    legacy_rejection={'legacyRejectedAsSafe':True,'negativeExitCode':rejection.returncode,'negativeFailures':rejected['failures']}
                else:need(all(not c['unsafeObserved'] and c['closedEventSignals']==0 and c['lateBus']==0 for c in value['cases']),'Candidate must avoid observed retirement gap')
                receipt['profiles'].append({'profile':profile,'route':route,'isCurrentProductionRoute':(route=='synchronous' and current_safe) or (route=='legacy' and not current_sync and observed_stop_guards==0),
                                             'checks':value['checks'],'cases':value['cases'],'executableSha256':sha(exe.read_bytes()),'compileSourceSha256':sha(cpp.read_bytes()),**(legacy_rejection or {})})
                print(profile+' '+route+': '+str(value['checks'])+' actual-source checks; 6 controlled schedules',flush=True)
                publish()
        need(all((ROOT/n).read_bytes()==b for n,b in sources.items()),'Actual source or fixture changed while testing')
        if args.prepare_only:
            receipt.update(status='PREPARED_NOT_EXECUTED',sourceStable=True);publish();return 0
        receipt['sourceStable']=True;receipt['evidenceComplete']=True;receipt['productionRetirementAccepted']=current_safe
        receipt['pass']=current_safe
        receipt['status']='PASS' if current_safe else 'PRODUCTION_RETIREMENT_GAP_CONFIRMED_CANDIDATE_ONLY'
        receipt['meaning']='Observed retirement order of actual production functions under explicit memory leaves; no full persistent lifecycle acceptance'
        receipt['checks']=sum(p['checks'] for p in receipt['profiles'])
        publish();print(receipt['status']+' RESULT='+str(output/'RESULT.json'))
        return 0 if current_safe else 2
    except Exception as error:
        receipt['status']='FAIL';receipt['error']=str(error);publish();print(str(error),file=sys.stderr);return 1
if __name__=='__main__':raise SystemExit(main())