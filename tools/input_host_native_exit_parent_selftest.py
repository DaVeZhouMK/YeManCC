#!/usr/bin/env python3
"""Execute whole current native exit cleanup + real Stop/Clear + final ensure.

Current local-owner regression only; original r4 product FAIL is retained unchanged.
R8 maintains this original consumer; native/product and older evidence stay read-only.
"""
from __future__ import annotations
import sys
sys.dont_write_bytecode=True
import argparse,datetime as dt,hashlib,importlib.util,json,re,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
FIX=ROOT/'tools/fixtures/input_host_native_exit_parent'
SCENARIOS=('healthy-ordinary','unprotected-uncooperative','protected-primary-pipe','capture-nonproducer-debt','capture-worker-retained','capture-timer-retained','capture-generation-mismatch','capture-stop-exception','busy-host-budget','permanent-close-budget','busy-pipe-independent-force')
PROCESS=ROOT/'tools/input_host_process_retirement_selftest.py'
spec=importlib.util.spec_from_file_location('existing_process_fixture_tools',PROCESS)
process=importlib.util.module_from_spec(spec);spec.loader.exec_module(process)
helper=process.helper
sha=lambda data:hashlib.sha256(data).hexdigest()
def need(value,message):
    if not value:raise RuntimeError(message)
def save(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
def function(text,name):
    return process.block(text,r'^static[^\n;{}]*\b'+name+r'\s*\([^;{}]*\)\s*(?:noexcept\s*)?\{')
def declaration(text,name):
    matches=list(re.finditer(r'^static[^\n;{}]*\b'+name+r'\b[^\n;]*;',text,re.M))
    need(len(matches)==1,'Actual unique global '+name)
    value=matches[0].group()
    return {'text':value,'line':text.count('\n',0,matches[0].start())+1,'sha256':sha(value.encode())}
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,default=Path('G:/YeManCC-Work/Testing/YMCC/Evidence/VC-NativeExitParent23')/dt.datetime.now().strftime('%H%M%S-%f'))
    parser.add_argument('--expected-main-sha256',required=True)
    parser.add_argument('--profiles',nargs='+',choices=('Release','Checked'),default=['Release','Checked'])
    parser.add_argument('--build-only',action='store_true')
    parser.add_argument('--scenarios',nargs='+',choices=SCENARIOS,default=list(SCENARIOS))
    parser.add_argument('--exit-owner-reference',type=Path,help='Explicit frozen historical header for a bounded negative control; never changes product')
    args=parser.parse_args();out=args.out.resolve();need(not out.exists(),'Fresh evidence required');out.mkdir(parents=True)
    files=[Path(__file__).resolve(),PROCESS,process.HELPER,*sorted(FIX.glob('*')),
      ROOT/'tools/fixtures/input_host_process_retirement/environment.hpp',ROOT/'tools/fixtures/input_host_process_retirement/suite.cpp.in',
      *[ROOT/'native'/name for name in ('main.cpp','input_host_process_retirement.h','virtual_controller_stop_runtime.h','virtual_controller_runtime_contract.h','virtual_start_protection.h','input_capture_completion.h','exit_cleanup_wait.h','input_host_exit_retirement.h','input_host_exit_owner_runtime.h','input_host_pipe_transport.h')],ROOT/'deps/json/json.hpp']
    files.extend([ROOT/'tools/input_pipe_retirement_memory_selftest.cpp',ROOT/'tools/input_pipe_test_allocation_fault.h'])
    if args.exit_owner_reference:files.append(args.exit_owner_reference.resolve())
    inputs={p:p.read_bytes() for p in files if p.is_file()}
    receipt={'createdAt':dt.datetime.now(dt.timezone.utc).isoformat(),'status':'PREPARING','parentStatus':'INCOMPLETE','productPass':False,'localOwnerRegressionVerified':False,
      'realHostOrSdkExecuted':False,'realMainExecuted':False,'productionModified':False,'inputs':[{'path':str(p),'sha256':sha(data)} for p,data in inputs.items()]}
    try:
        mainText=inputs[ROOT/'native/main.cpp'].decode('utf-8-sig');stopText=inputs[ROOT/'native/virtual_controller_stop_runtime.h'].decode('utf-8-sig')
        receipt['mainSha256']=sha(inputs[ROOT/'native/main.cpp']);need(receipt['mainSha256']==args.expected_main_sha256.lower(),'Explicit main freeze mismatch')
        source=out/'source';source.mkdir()
        for path,data in inputs.items():
            if not path.is_relative_to(ROOT):continue
            dest=source/path.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
        if args.exit_owner_reference:
            ref=args.exit_owner_reference.resolve();need(ref in inputs,'Missing frozen reference header')
            (source/'native/input_host_exit_owner_runtime.h').write_bytes(inputs[ref])
            receipt['historicalHeaderReference']={'path':str(ref),'sha256':sha(inputs[ref]),'compiledAs':'native/input_host_exit_owner_runtime.h','isCurrentProductHeader':False}
        bindings={}
        for name in ('inputHostPrimaryThreadRetirementProvenLocked','inputHostClearLocked','ymStartHostExitWait','inputHostQuiesceForCaptureExit',
          'inputHostPipeCloseOwned','inputHostPipeRetirementProvenLocked','inputHostPipeOnlyReturnAdmittedLocked','inputHostReconcileQuarantineOnLifecycleThread','inputHostExitFenceRaised','inputCaptureClearExitConfiguration','inputCaptureAwaitStopForExit','inputCaptureExitOwnershipReleased','inputCaptureExitProducerOwnershipReleased','postExitReadyOnce','exitCleanupThreadProc','tryStartExitCleanupWorker','inputCaptureEnsureExitOwner'):
            bindings[name]={'file':'native/main.cpp',**function(mainText,name)}
        for name in ('inputHostStopRestoresVisibilityWithoutProof','inputHostStopHoldsPhysicalVisibility','inputHostOwnedCleanupProvenLocked','inputHostDisabledCleanupProvenLocked','inputHostStopLocked','inputHostStop'):
            bindings[name]={'file':'native/virtual_controller_stop_runtime.h',**function(stopText,name)}
        hostNames=('g_inputHostCreatedIdentity','g_inputHostExitForceSpent','g_inputHostUnadoptedPipe','g_inputHostProcess','g_inputHostPrimaryThread','g_inputHostServerPipe','g_inputHostRunId','g_inputHostEpoch','g_exitSessionId')
        exitNames=('g_hwnd','g_summonQuit','g_autoCloseThread','g_exitRequested','g_exitReadyPosted','g_exitCode','g_exitCleanupThread','g_exitCleanupCompleted','g_exitCleanupStage',
          'g_exitBeginTick','g_exitCleanupWorkerFailureReported','g_recoveryExitEvent','g_captureExitReleased','g_inputHostExitFence','g_captureExitProducerReturned','g_captureExitProducerGeneration','g_inputCaptureStop','g_captureGeneration','g_captureMmTimerRetired')
        for name in hostNames+exitNames:bindings['global.'+name]={'file':'native/main.cpp',**declaration(mainText,name)}
        globals='\n'.join(bindings['global.'+name]['text'] for name in hostNames)
        envDir=source/'tools/fixtures/input_host_native_exit_parent'
        (envDir/'actual_host_handle_globals.hpp').write_bytes((globals+'\n').encode())
        env=inputs[ROOT/'tools/fixtures/input_host_process_retirement/environment.hpp'].decode('utf-8-sig')
        # Reuse existing explicit OS/protocol/physical ports, removing exactly
        # the two definitions now executed from actual production source.
        for pattern in (r'^static std::string g_exitSessionId=.*?;\n',r'^static DWORD ymStartHostExitWait\(HANDLE process\).*?\n',r'^static void inputHostPipeCloseOwned\(HANDLE\).*?\n',r'^static bool inputHostPipeRetirementProvenLocked\(\).*?\n'):
            env,n=re.subn(pattern,'',env,flags=re.M);need(n==1,'Bind reused memory leaf '+pattern)
        oldLeaf='static void msiDinputStopForLifecycle(const char*,bool){g_msiDinputReturnOwed=false;}'
        need(env.count(oldLeaf)==1,'Existing typed MSI stop leaf')
        env=env.replace(oldLeaf,'static void msiDinputStopForLifecycle(const char*,bool,bool){g_msiDinputReturnOwed=false;}\nstatic bool msiOwnedReturnOffEndpointAbsent(){return ports.endpointAbsent;}')
        oldNeed='static void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}'
        need(env.count(oldNeed)==1,'Existing need assertion leaf')
        env=env.replace(oldNeed,'static std::atomic<unsigned> fixtureAssertionCount{0};\nstatic void need(bool value,const char* message){++fixtureAssertionCount;if(!value)throw std::runtime_error(message);}')
        (envDir/'environment.hpp').write_bytes(env.encode())
        leafText=inputs[ROOT/'tools/input_pipe_retirement_memory_selftest.cpp'].decode('utf-8-sig')
        marker='using json=nlohmann::json;'
        need(leafText.count(marker)==1,'Actual pipe memory prefix anchor')
        (envDir/'actual_pipe_leaves.hpp').write_bytes(leafText[:leafText.index(marker)].encode())
        ownerPath=args.exit_owner_reference.resolve() if args.exit_owner_reference else ROOT/'native/input_host_exit_owner_runtime.h'
        ownerText=inputs[ownerPath].decode('utf-8-sig')
        for name in ('inputHostExitLocalOwnershipReleased','inputHostNoteExitStopAttempted','inputHostTryOrdinaryStopForExit','inputHostRetireLocalResourcesForExit','inputHostAwaitLocalRetirementForExit'):
            bindings[name]={'file':str(ownerPath),'compiledAs':'native/input_host_exit_owner_runtime.h',**function(ownerText,name)}
        child=process.block(inputs[ROOT/'tools/fixtures/input_host_process_retirement/suite.cpp.in'].decode('utf-8-sig'),r'^struct Child\s*\{')['text']+';'
        bindings['reusedChild']={'file':'tools/fixtures/input_host_process_retirement/suite.cpp.in','sha256':sha(child.encode()),'adaptation':'retain real primary; fixture child assigned to its own kill-on-close job before resume; fallback never counts as product pass'}
        child=child.replace('HANDLE process=nullptr,release=nullptr;','HANDLE process=nullptr,release=nullptr,primary=nullptr,job=nullptr;')
        old='process=pi.hProcess;need(::CloseHandle(pi.hThread)!=FALSE,"close child primary thread");'
        need(child.count(old)==1,'Existing private child constructor anchor')
        child=child.replace('TRUE,CREATE_NO_WINDOW,','TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,')
        child=child.replace(old,'''process=pi.hProcess;primary=pi.hThread;
        job=CreateJobObjectW(nullptr,nullptr);need(job!=nullptr,"create private child job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        need(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))!=FALSE,"configure private child job");
        need(AssignProcessToJobObject(job,process)!=FALSE,"assign only newly created private child");
        need(ResumeThread(primary)!=static_cast<DWORD>(-1),"resume private child after job ownership");''')
        child=child.replace('        if(release)::CloseHandle(release);','        if(primary){SetHandleInformation(primary,HANDLE_FLAG_PROTECT_FROM_CLOSE,0);::CloseHandle(primary);}\n        if(release)::CloseHandle(release);\n        if(job)::CloseHandle(job);',1)
        production='\n'.join(bindings[name]['text'] for name in ('ymStartHostExitWait','inputHostPipeCloseOwned','inputHostPipeRetirementProvenLocked','inputHostPrimaryThreadRetirementProvenLocked','inputHostClearLocked','inputHostStopRestoresVisibilityWithoutProof','inputHostStopHoldsPhysicalVisibility',
          'inputHostOwnedCleanupProvenLocked','inputHostDisabledCleanupProvenLocked','inputHostStopLocked','inputHostStop','inputHostPipeOnlyReturnAdmittedLocked','inputHostReconcileQuarantineOnLifecycleThread','inputHostExitFenceRaised','inputHostQuiesceForCaptureExit','inputCaptureClearExitConfiguration','inputCaptureAwaitStopForExit','inputCaptureExitOwnershipReleased','inputCaptureExitProducerOwnershipReleased'))
        production+='\n#include "input_host_exit_owner_runtime.h"\n'
        production+='\n'.join(bindings[name]['text'] for name in ('postExitReadyOnce','exitCleanupThreadProc','tryStartExitCleanupWorker','inputCaptureEnsureExitOwner'))
        template=inputs[FIX/'suite.cpp.in'].decode('utf-8-sig')
        for marker,value in [('@@EXIT_GLOBALS@@','\n'.join(bindings['global.'+name]['text'] for name in exitNames)),('@@PRODUCTION@@',production),('@@PRIVATE_CHILD@@',child)]:
            need(template.count(marker)==1,'Fixture marker '+marker);template=template.replace(marker,value)
        fixture=out/'fixture.cpp';fixture.write_bytes(template.encode())
        save(out/'source-bindings.json',bindings)
        compiler,envVars=helper.compiler_environment(out)
        receipt['compiler']={'path':str(compiler),'sha256':sha(compiler.read_bytes()),'vcvarsSha256':sha(helper.VCVARS.read_bytes()),'windowsSDKVersion':envVars.get('WindowsSDKVersion')}
        receipt['profiles']=[]
        receipt['historicalFailureRetained']={'path':'G:/YeManCC-Work/Testing/YMCC/Evidence/VC-NativeExitParent23/r4/RESULT.json','status':'FAIL; not rewritten or promoted'}
        for profile in dict.fromkeys(args.profiles):
            flags={'Release':['/MT','/O2','/DNDEBUG'],'Checked':['/MTd','/Od','/D_DEBUG']}[profile]
            target=out/profile;target.mkdir();exe=target/'fixture.exe'
            argv=[compiler,'/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/wd4505','/permissive-',*flags,'/I'+str(source/'native'),'/I'+str(source/'deps/json'),'/I'+str(envDir),'/I'+str(source/'tools'),fixture,'/Fo'+str(target/'fixture.obj'),'/Fe'+str(exe)]
            save(target/'build-command.json',[str(v) for v in argv]);built=helper.execute(argv,target,envVars,target/'build.log',60)
            need(built.returncode==0,'Compile failed '+str(target/'build.log'))
            scenarios=[]
            if args.build_only:
                receipt['profiles'].append({'name':profile,'binarySha256':sha(exe.read_bytes()),'executedThisRun':False,'status':'BUILD_ONLY_NOT_RUN'})
                continue
            for scenario in dict.fromkeys(args.scenarios):
                scenarioStart=time.perf_counter()
                ran=helper.execute([exe,scenario],target,envVars,target/(scenario+'-run.log'),30)
                need(ran.stdout.strip(),'No scenario output: '+scenario)
                result=json.loads(ran.stdout);save(target/(scenario+'-result.json'),result)
                need(ran.returncode==0 and result['scopedLocalOwnerPass'] is True,'Actual local-owner regression failed: '+scenario+' '+str(target))
                need(result['fixtureCleanup']['privateHandlesClosed'] and not result['fixtureCleanup']['childKillOrEventBackstop'],'Production must retire child/primary/pipe before fixture handle cleanup')
                scenarios.append({'name':scenario,'exitCode':ran.returncode,'wallSeconds':time.perf_counter()-scenarioStart,'executedThisRun':True,'result':result})
            imported=helper.execute([compiler.parent/'dumpbin.exe','/imports',exe],target,envVars,target/'imports.txt',15)
            need(imported.returncode==0 and not any(v in imported.stdout.lower() for v in helper.FORBIDDEN),'Unexpected device/UI/product import')
            receipt['profiles'].append({'name':profile,'binarySha256':sha(exe.read_bytes()),'scenarios':scenarios})
        receipt['wholeMainIdentical']=inputs[ROOT/'native/main.cpp']==(ROOT/'native/main.cpp').read_bytes()
        continuity={}
        for name,bound in bindings.items():
            if bound.get('file')!='native/main.cpp':continue
            text=(ROOT/'native/main.cpp').read_bytes().decode('utf-8-sig')
            current=declaration(text,name[7:]) if name.startswith('global.') else function(text,name)
            continuity[name]={'boundSha256':bound['sha256'],'currentSha256':current['sha256'],'identical':bound['text']==current['text']}
        save(out/'main-fragment-continuity.json',{'boundMainSha256':receipt['mainSha256'],'currentMainSha256':sha((ROOT/'native/main.cpp').read_bytes()),'wholeMainIdentical':receipt['wholeMainIdentical'],'bindings':continuity})
        receipt['boundMainFragmentsStable']=all(v['identical'] for v in continuity.values())
        receipt['sourceStable']=all(path.read_bytes()==data for path,data in inputs.items() if path!=ROOT/'native/main.cpp')
        need(receipt['sourceStable'] and receipt['boundMainFragmentsStable'],'Bound product/test inputs drifted during execution; see continuity receipt')
        if args.build_only:
            receipt['status']='BUILD_ONLY_NOT_RUN';save(out/'RESULT.json',receipt)
            print(json.dumps({'out':str(out),'status':receipt['status'],'profiles':[p['name'] for p in receipt['profiles']]}));return
        receipt['status']='ACTUAL_NATIVE_EXIT_LOCAL_OWNERS_RETIRED_SCOPED_PASS';receipt['localOwnerRegressionVerified']=True
        receipt['executedCases']=sum(len(p['scenarios']) for p in receipt['profiles'])
        receipt['executedAssertions']=sum(v['result'].get('assertionsPassed',0) for p in receipt['profiles'] for v in p['scenarios'])
        receipt['scope']='Actual exit-retirement and exit-owner headers, real pipe Runtime. Whole current exitCleanupThreadProc + public inputHostStop/StopLocked/Clear + primary helper + complete final inputCaptureEnsureExitOwner and wait helper executed. Capture Stop body is an explicit typed debt/fault receipt (not actual blocking Stop); original 3 cases pre-retire it, R8 drives actual await/producer getter/Host authority/exit worker with stepped-clock budget; protocol/endpoint/physical and non-controller cleanup are named memory leaves; pipe fault IO uses existing real private HANDLE memory API. Private child and primary are real kernel resources. Whole wWinMain/UI loop and real managed SDK not executed.'
    except Exception as exc:
        receipt['error']=str(exc);save(out/'RESULT.json',receipt);raise
    save(out/'RESULT.json',receipt)
    print(json.dumps({'out':str(out),'status':receipt['status'],'localOwnerRegressionVerified':True,'fullProductPass':False,'mainSha256':receipt['mainSha256'],'profiles':[{'name':p['name'],'scenarios':[v['name'] for v in p['scenarios']]} for p in receipt['profiles']]}))
if __name__=='__main__':main()
