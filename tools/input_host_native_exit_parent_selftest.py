#!/usr/bin/env python3
"""Execute whole current native exit cleanup + real Stop/Clear + final ensure.

Current local-owner regression only; original r4 product FAIL is retained unchanged.
Only new fixture files are written. Existing 34/95 tools and native stay read-only.
"""
from __future__ import annotations
import sys
sys.dont_write_bytecode=True
import argparse,datetime as dt,hashlib,importlib.util,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
FIX=ROOT/'tools/fixtures/input_host_native_exit_parent'
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
    args=parser.parse_args();out=args.out.resolve();need(not out.exists(),'Fresh evidence required');out.mkdir(parents=True)
    files=[Path(__file__).resolve(),PROCESS,process.HELPER,*sorted(FIX.glob('*')),
      ROOT/'tools/fixtures/input_host_process_retirement/environment.hpp',ROOT/'tools/fixtures/input_host_process_retirement/suite.cpp.in',
      *[ROOT/'native'/name for name in ('main.cpp','input_host_process_retirement.h','virtual_controller_stop_runtime.h','virtual_controller_runtime_contract.h','virtual_start_protection.h','input_capture_completion.h','exit_cleanup_wait.h','input_host_exit_retirement.h','input_host_exit_owner_runtime.h','input_host_pipe_transport.h')],ROOT/'deps/json/json.hpp']
    files.extend([ROOT/'tools/input_pipe_retirement_memory_selftest.cpp',ROOT/'tools/input_pipe_test_allocation_fault.h'])
    inputs={p:p.read_bytes() for p in files if p.is_file()}
    receipt={'createdAt':dt.datetime.now(dt.timezone.utc).isoformat(),'status':'PREPARING','parentStatus':'INCOMPLETE','productPass':False,'localOwnerRegressionVerified':False,
      'realHostOrSdkExecuted':False,'realMainExecuted':False,'productionModified':False,'inputs':[{'path':str(p),'sha256':sha(data)} for p,data in inputs.items()]}
    try:
        mainText=inputs[ROOT/'native/main.cpp'].decode('utf-8-sig');stopText=inputs[ROOT/'native/virtual_controller_stop_runtime.h'].decode('utf-8-sig')
        receipt['mainSha256']=sha(inputs[ROOT/'native/main.cpp']);need(receipt['mainSha256']==args.expected_main_sha256.lower(),'Explicit main freeze mismatch')
        source=out/'source';source.mkdir()
        for path,data in inputs.items():
            dest=source/path.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
        bindings={}
        for name in ('inputHostPrimaryThreadRetirementProvenLocked','inputHostClearLocked','ymStartHostExitWait','inputHostQuiesceForCaptureExit',
          'inputHostPipeCloseOwned','inputHostPipeRetirementProvenLocked','inputHostPipeOnlyReturnAdmittedLocked','inputHostReconcileQuarantineOnLifecycleThread','inputHostExitFenceRaised','inputCaptureClearExitConfiguration','inputCaptureAwaitStopForExit','inputCaptureExitOwnershipReleased','postExitReadyOnce','exitCleanupThreadProc','tryStartExitCleanupWorker','inputCaptureEnsureExitOwner'):
            bindings[name]={'file':'native/main.cpp',**function(mainText,name)}
        for name in ('inputHostStopRestoresVisibilityWithoutProof','inputHostStopHoldsPhysicalVisibility','inputHostOwnedCleanupProvenLocked','inputHostDisabledCleanupProvenLocked','inputHostStopLocked','inputHostStop'):
            bindings[name]={'file':'native/virtual_controller_stop_runtime.h',**function(stopText,name)}
        hostNames=('g_inputHostCreatedIdentity','g_inputHostExitForceSpent','g_inputHostUnadoptedPipe','g_inputHostProcess','g_inputHostPrimaryThread','g_inputHostServerPipe','g_inputHostRunId','g_inputHostEpoch','g_exitSessionId')
        exitNames=('g_hwnd','g_summonQuit','g_autoCloseThread','g_exitRequested','g_exitReadyPosted','g_exitCode','g_exitCleanupThread','g_exitCleanupCompleted','g_exitCleanupStage',
          'g_exitBeginTick','g_exitCleanupWorkerFailureReported','g_recoveryExitEvent','g_captureExitReleased','g_inputHostExitFence')
        for name in hostNames+exitNames:bindings['global.'+name]={'file':'native/main.cpp',**declaration(mainText,name)}
        globals='\n'.join(bindings['global.'+name]['text'] for name in hostNames)
        envDir=source/'tools/fixtures/input_host_native_exit_parent'
        (envDir/'actual_host_handle_globals.hpp').write_bytes((globals+'\n').encode())
        env=inputs[ROOT/'tools/fixtures/input_host_process_retirement/environment.hpp'].decode('utf-8-sig')
        # Reuse existing explicit OS/protocol/physical ports, removing exactly
        # the two definitions now executed from actual production source.
        for pattern in (r'^static std::string g_exitSessionId=.*?;\n',r'^static DWORD ymStartHostExitWait\(HANDLE process\).*?\n',r'^static void inputHostPipeCloseOwned\(HANDLE\).*?\n',r'^static bool inputHostPipeRetirementProvenLocked\(\).*?\n'):
            env,n=re.subn(pattern,'',env,flags=re.M);need(n==1,'Bind reused memory leaf '+pattern)
        (envDir/'environment.hpp').write_bytes(env.encode())
        leafText=inputs[ROOT/'tools/input_pipe_retirement_memory_selftest.cpp'].decode('utf-8-sig')
        marker='using json=nlohmann::json;'
        need(leafText.count(marker)==1,'Actual pipe memory prefix anchor')
        (envDir/'actual_pipe_leaves.hpp').write_bytes(leafText[:leafText.index(marker)].encode())
        ownerText=inputs[ROOT/'native/input_host_exit_owner_runtime.h'].decode('utf-8-sig')
        for name in ('inputHostExitLocalOwnershipReleased','inputHostNoteExitStopAttempted','inputHostRetireLocalResourcesForExit','inputHostAwaitLocalRetirementForExit'):
            bindings[name]={'file':'native/input_host_exit_owner_runtime.h',**function(ownerText,name)}
        child=process.block(inputs[ROOT/'tools/fixtures/input_host_process_retirement/suite.cpp.in'].decode('utf-8-sig'),r'^struct Child\s*\{')['text']+';'
        bindings['reusedChild']={'file':'tools/fixtures/input_host_process_retirement/suite.cpp.in','sha256':sha(child.encode()),'adaptation':'retain real pi.hThread in fixture guard; fixture fallback checks/unprotects/closes it only after product observation'}
        child=child.replace('HANDLE process=nullptr,release=nullptr;','HANDLE process=nullptr,release=nullptr,primary=nullptr;')
        old='process=pi.hProcess;need(::CloseHandle(pi.hThread)!=FALSE,"close child primary thread");'
        need(child.count(old)==1,'Existing private child constructor anchor')
        child=child.replace(old,'process=pi.hProcess;primary=pi.hThread;')
        child=child.replace('        if(release)::CloseHandle(release);','        if(primary){SetHandleInformation(primary,HANDLE_FLAG_PROTECT_FROM_CLOSE,0);::CloseHandle(primary);}\n        if(release)::CloseHandle(release);',1)
        production='\n'.join(bindings[name]['text'] for name in ('ymStartHostExitWait','inputHostPipeCloseOwned','inputHostPipeRetirementProvenLocked','inputHostPrimaryThreadRetirementProvenLocked','inputHostClearLocked','inputHostStopRestoresVisibilityWithoutProof','inputHostStopHoldsPhysicalVisibility',
          'inputHostOwnedCleanupProvenLocked','inputHostDisabledCleanupProvenLocked','inputHostStopLocked','inputHostStop','inputHostPipeOnlyReturnAdmittedLocked','inputHostReconcileQuarantineOnLifecycleThread','inputHostExitFenceRaised','inputHostQuiesceForCaptureExit','inputCaptureClearExitConfiguration','inputCaptureAwaitStopForExit','inputCaptureExitOwnershipReleased'))
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
        for profile,flags in [('Release',['/MT','/O2','/DNDEBUG']),('Checked',['/MTd','/Od','/D_DEBUG'])]:
            target=out/profile;target.mkdir();exe=target/'fixture.exe'
            argv=[compiler,'/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/wd4505','/permissive-',*flags,'/I'+str(source/'native'),'/I'+str(source/'deps/json'),'/I'+str(envDir),'/I'+str(source/'tools'),fixture,'/Fo'+str(target/'fixture.obj'),'/Fe'+str(exe)]
            save(target/'build-command.json',[str(v) for v in argv]);built=helper.execute(argv,target,envVars,target/'build.log',60)
            need(built.returncode==0,'Compile failed '+str(target/'build.log'))
            scenarios=[]
            for scenario in ('healthy-ordinary','unprotected-uncooperative','protected-primary-pipe'):
                ran=helper.execute([exe,scenario],target,envVars,target/(scenario+'-run.log'),40)
                need(ran.stdout.strip(),'No scenario output: '+scenario)
                result=json.loads(ran.stdout);save(target/(scenario+'-result.json'),result)
                need(ran.returncode==0 and result['scopedLocalOwnerPass'] is True,'Actual local-owner regression failed: '+scenario+' '+str(target))
                need(result['fixtureCleanup']['privateHandlesClosed'] and not result['fixtureCleanup']['childKillOrEventBackstop'],'Production must retire child/primary/pipe before fixture handle cleanup')
                scenarios.append({'name':scenario,'exitCode':ran.returncode,'result':result})
            imported=helper.execute([compiler.parent/'dumpbin.exe','/imports',exe],target,envVars,target/'imports.txt',15)
            need(imported.returncode==0 and not any(v in imported.stdout.lower() for v in helper.FORBIDDEN),'Unexpected device/UI/product import')
            receipt['profiles'].append({'name':profile,'binarySha256':sha(exe.read_bytes()),'scenarios':scenarios})
        receipt['sourceStable']=all(path.read_bytes()==data for path,data in inputs.items());need(receipt['sourceStable'],'Inputs drifted during execution')
        receipt['status']='ACTUAL_NATIVE_EXIT_LOCAL_OWNERS_RETIRED_SCOPED_PASS';receipt['localOwnerRegressionVerified']=True
        receipt['scope']='Actual exit-retirement and exit-owner headers, real pipe Runtime. Whole current exitCleanupThreadProc + public inputHostStop/StopLocked/Clear + primary helper + complete final inputCaptureEnsureExitOwner and wait helper executed. Capture Stop is an explicit pre-retired typed memory receipt; protocol/endpoint/physical and non-controller cleanup are named memory leaves; pipe fault IO uses existing real private HANDLE memory API. Private child and primary are real kernel resources. Whole wWinMain/UI loop and real managed SDK not executed.'
    except Exception as exc:
        receipt['error']=str(exc);save(out/'RESULT.json',receipt);raise
    save(out/'RESULT.json',receipt)
    print(json.dumps({'out':str(out),'status':receipt['status'],'localOwnerRegressionVerified':True,'fullProductPass':False,'mainSha256':receipt['mainSha256'],'profiles':[{'name':p['name'],'scenarios':[v['name'] for v in p['scenarios']]} for p in receipt['profiles']]}))
if __name__=='__main__':main()
