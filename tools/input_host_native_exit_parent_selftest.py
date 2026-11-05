#!/usr/bin/env python3
"""Execute whole current native exit cleanup + real Stop/Clear + final ensure.

Current local-owner regression only; original r4 product FAIL is retained unchanged.
R8 maintains this original consumer; native/product and older evidence stay read-only.
"""
from __future__ import annotations
import sys
sys.dont_write_bytecode=True
import argparse,datetime as dt,hashlib,importlib.util,json,re,time,uuid
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
FIX=ROOT/'tools/fixtures/input_host_native_exit_parent'
DEFAULT_SCENARIOS=('healthy-ordinary','unprotected-uncooperative','protected-primary-pipe','capture-nonproducer-debt','capture-worker-retained','capture-timer-retained','capture-generation-mismatch','capture-stop-exception','busy-host-budget','permanent-close-budget','busy-pipe-independent-force')
SCENARIOS=DEFAULT_SCENARIOS+('coordinator-observer-close',)
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
def observer_function(text):
    # The original signature has a {} default argument before its body.
    masked=helper.mask_cpp(text)
    signature=r'^static json fullPipeChildOutcome\(HANDLE exactChild=nullptr,const std::wstring& exactReceipt=\{\}\)\{'
    matches=list(re.finditer(signature,masked,re.M))
    need(len(matches)==1,'Unique actual observer consumer signature')
    start=matches[0].start();end=matches[0].end();depth=1
    while depth:
        need(end<len(masked),'Unclosed actual observer consumer')
        depth+=(masked[end]=='{')-(masked[end]=='}');end+=1
    value=text[start:end]
    return {'text':value,'line':text.count('\n',0,start)+1,'sha256':sha(value.encode())}

def load_managed_child(file):
    data=file.read_bytes();value=json.loads(data)
    need(value.get('kind')=='ymcc-managed-retained-child-v1' and value.get('preparedOnly') is True and value.get('executedCases')==0,'Prepared-only managed child descriptor required')
    spec=json.loads((ROOT/'tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json').read_bytes())
    need(Path(value['program']['path']).resolve()==(Path(spec['dotnetRoot'])/'dotnet.exe').resolve(),'Exact pinned dotnet host required')
    need(value.get('runtimeVersion')==spec['runtimeVersion'],'Pinned managed runtime version required')
    need(Path(value['assembly']['path']).resolve()==(file.parent/'integrated/bin/Release/net10.0/Fixture.dll').resolve(),'Exact prepared managed assembly required')
    need(value.get('argumentsTemplate')==['<assembly>','--retained-owner-child','<proofPath>','<nonce>','<nativeParentPid>'],'Managed retained child exact command contract')
    expected={str(Path(row['path']).resolve()).lower():row['sha256'] for row in spec['pinned']}
    actual={str(Path(row['path']).resolve()).lower():row['sha256'] for row in value['toolchainInputs']}
    need(actual==expected and len(actual)==len(value['toolchainInputs']),'Exact original managed toolchain closure required')
    need(value.get('sourceStable') is True and value.get('runtimeSourceClosureStable') is True and value.get('toolchainStable') is True and value.get('fixtureExecuted') is False and value.get('behaviorPass') is None,'Actual preparation closure required without a business PASS')
    commands=value['buildCommands'];need(len(commands)==1,'Single original managed preparation build required')
    build=commands[0]
    need(build['label']=='Release-build' and build['status']==0 and not build.get('signal') and not build.get('error'),'Successful managed preparation build receipt required')
    need(Path(build['exe']).resolve()==Path(value['program']['path']).resolve() and Path(build['cwd']).resolve()==(file.parent/'integrated').resolve() and build['argv']==['build','Fixture.csproj','-c','Release','--nologo','-v:q','-p:UseSharedCompilation=false','-nodeReuse:false'],'Exact managed preparation recipe required')
    groups=[value['program'],value['assembly'],value['sdkBinding'],value['toolchainManifest'],value['toolchainReceipt'],value['inputSnapshotManifest'],value['consumerExtractions'],*value['buildReceipts'],*value['sourceSnapshots'],*value['runtimeFiles'],*value['sourceInputs'],*value['generatedInputs'],*value['toolchainInputs']]
    pins={}
    for row in groups:
        path=Path(row['path']);need(path.is_absolute() and path.is_file() and not path.is_symlink() and not (getattr(path.lstat(),'st_file_attributes',0)&0x400),'Managed child regular absolute input required')
        item={'path':str(path.resolve()),'bytes':row['bytes'],'sha256':row['sha256']}
        key=item['path'].lower();need(key not in pins or pins[key]==item,'Conflicting managed input identity')
        pins[key]=item
    sources={str(Path(row['path']).resolve()).lower() for row in value['sourceInputs']}
    for rel in ('tools/input_host_managed_lifecycle_terminal_selftest.mjs','tools/fixtures/input_host_managed_lifecycle_20261023/TerminalCases.cs','InputHost/Program.cs','InputHost/HostTerminalCleanup.cs','tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json'):
        need(str((ROOT/rel).resolve()).lower() in sources,'Missing actual managed origin '+rel)
    pins[str(file).lower()]={'path':str(file),'bytes':len(data),'sha256':sha(data)}
    rows=list(pins.values());verify_managed_inputs(rows)
    return value,rows

def verify_managed_inputs(rows):
    for row in rows:
        path=Path(row['path']);data=path.read_bytes()
        need(len(data)==row['bytes'] and sha(data)==row['sha256'],'Managed handoff input changed: '+str(path))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,default=Path('G:/YeManCC-Work/Testing/YMCC/Evidence/VC-NativeExitParent23')/dt.datetime.now().strftime('%H%M%S-%f'))
    parser.add_argument('--expected-main-sha256',required=True)
    parser.add_argument('--profiles',nargs='+',choices=('Release','Checked'),default=['Release','Checked'])
    parser.add_argument('--build-only',action='store_true')
    parser.add_argument('--managed-retained-child',type=Path,help='Explicit prepared existing managed permanent-output child; only unprotected-uncooperative')
    parser.add_argument('--scenarios',nargs='+',choices=SCENARIOS,default=list(DEFAULT_SCENARIOS))
    parser.add_argument('--exit-owner-reference',type=Path,help='Explicit frozen historical header for a bounded negative control; never changes product')
    parser.add_argument('--coordinator-observer-reference',type=Path,help='Frozen original pipe_cases.inc only for the isolated coordinator-observer-close negative; never changes product')
    args=parser.parse_args()
    observerOnly=args.scenarios==['coordinator-observer-close']
    need(not args.managed_retained_child or args.scenarios==['unprotected-uncooperative'] and not args.exit_owner_reference and not args.coordinator_observer_reference,'Managed handoff requires the exact existing unprotected scenario and current product owner')
    managedFile=args.managed_retained_child.resolve() if args.managed_retained_child else None
    managed,managedPins=load_managed_child(managedFile) if managedFile else (None,[])
    need('coordinator-observer-close' not in args.scenarios or observerOnly,'Observer case must be selected alone')
    need(not args.coordinator_observer_reference or observerOnly and args.coordinator_observer_reference.is_absolute() and args.coordinator_observer_reference.is_file(),'Observer reference requires the single observer case and an existing absolute file')
    need(not observerOnly or not args.exit_owner_reference,'Observer case cannot replace the product exit-owner header')
    out=args.out.resolve();need(not out.exists(),'Fresh evidence required');out.mkdir(parents=True)
    files=[Path(__file__).resolve(),PROCESS,process.HELPER,*sorted(FIX.glob('*')),
      ROOT/'tools/fixtures/input_host_process_retirement/environment.hpp',ROOT/'tools/fixtures/input_host_process_retirement/suite.cpp.in',
      *[ROOT/'native'/name for name in ('main.cpp','input_host_process_retirement.h','virtual_controller_stop_runtime.h','virtual_controller_runtime_contract.h','virtual_start_protection.h','input_capture_completion.h','exit_cleanup_wait.h','input_host_exit_retirement.h','input_host_exit_owner_runtime.h','input_host_pipe_transport.h')],ROOT/'deps/json/json.hpp']
    files.extend([ROOT/'tools/input_pipe_retirement_memory_selftest.cpp',ROOT/'tools/input_pipe_test_allocation_fault.h'])
    files.append(ROOT/'tools/fixtures/input_coordinator_lifecycle/pipe_cases.inc')
    if args.coordinator_observer_reference:files.append(args.coordinator_observer_reference.resolve())
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
        hostNames=('g_inputHostCreatedIdentity','g_inputHostExitForceSpent','g_inputHostUnadoptedPipe','g_inputHostProcess','g_inputHostPrimaryThread','g_inputHostServerPipe','g_inputHostRunId','g_inputHostEpoch','g_exitSessionId','g_inputHostFrameReplyBinding','g_inputHostFaultHoldsStart')
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
        observerPath=args.coordinator_observer_reference.resolve() if args.coordinator_observer_reference else ROOT/'tools/fixtures/input_coordinator_lifecycle/pipe_cases.inc'
        observer=observer_function(inputs[observerPath].decode('utf-8-sig'))
        bindings['fixture.fullPipeChildOutcome']={'file':str(observerPath),'fixtureOnly':True,**observer}
        (source/'coordinator-observer-selected.inc').write_bytes(inputs[observerPath])
        receipt['coordinatorObserverBinding']={'path':str(observerPath),'sha256':sha(inputs[observerPath]),'functionSha256':observer['sha256'],'isCurrentOriginalFixture':not bool(args.coordinator_observer_reference),'fixtureOnly':True}
        child=process.block(inputs[ROOT/'tools/fixtures/input_host_process_retirement/suite.cpp.in'].decode('utf-8-sig'),r'^struct Child\s*\{')['text']+';'
        bindings['reusedChild']={'file':'tools/fixtures/input_host_process_retirement/suite.cpp.in','sha256':sha(child.encode()),'adaptation':'retain real primary; fixture child assigned to its own kill-on-close job before resume; fallback never counts as product pass'}
        child=child.replace('HANDLE process=nullptr,release=nullptr;','HANDLE process=nullptr,release=nullptr,primary=nullptr,job=nullptr;')
        old='process=pi.hProcess;need(::CloseHandle(pi.hThread)!=FALSE,"close child primary thread");'
        need(child.count(old)==1,'Existing private child constructor anchor')
        child=child.replace('TRUE,CREATE_NO_WINDOW,','TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,')
        childAnchor='        STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};'
        need(child.count(childAnchor)==1,'Private child command ownership anchor')
        child=child.replace(childAnchor,'        std::wstring application(exe);configureManagedChild(application,command);\n'+childAnchor)
        child=child.replace('CreateProcessW(exe,command.data(),','CreateProcessW(application.c_str(),command.data(),')
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
        for marker,value in [('@@EXIT_GLOBALS@@','\n'.join(bindings['global.'+name]['text'] for name in exitNames)),('@@PRODUCTION@@',production),('@@PRIVATE_CHILD@@',child),('@@COORDINATOR_OBSERVER_CONSUMER@@',observer['text'])]:
            need(template.count(marker)==1,'Fixture marker '+marker);template=template.replace(marker,value)
        fixture=out/'fixture.cpp';fixture.write_bytes(template.encode())
        save(out/'source-bindings.json',bindings)
        compiler,envVars=helper.compiler_environment(out)
        receipt['compiler']={'path':str(compiler),'sha256':sha(compiler.read_bytes()),'vcvarsSha256':sha(helper.VCVARS.read_bytes()),'windowsSDKVersion':envVars.get('WindowsSDKVersion')}
        receipt['profiles']=[]
        managedEnvKeys=('YMCC_NATIVE_EXIT_MANAGED_PROGRAM','YMCC_NATIVE_EXIT_MANAGED_ASSEMBLY','YMCC_NATIVE_EXIT_MANAGED_PROOF','YMCC_NATIVE_EXIT_MANAGED_NONCE','YMCC_NATIVE_EXIT_MANAGED_PROOF_MUTATION')
        for key in managedEnvKeys:envVars.pop(key,None)
        if managed:
            receipt['managedChild']={'descriptor':{'path':str(managedFile),'sha256':sha(managedFile.read_bytes())},'preparedInputs':managedPins,'originalManagedCase':'terminal-shutdown-output-fixed','handshakeOnly':True,'realSdkLoaded':False}
            for key in list(envVars):
                if key.upper().startswith(('COR_','CORECLR_','COMPLUS_','DOTNET_STARTUP_HOOKS','DOTNET_ADDITIONAL_DEPS','DOTNET_SHARED_STORE','DOTNET_ROOT','DOTNET_ROLL_FORWARD','DOTNET_HOST_PATH','DOTNET_MULTILEVEL_LOOKUP')):envVars.pop(key,None)
            envVars.update({'DOTNET_ROOT':str(Path(managed['program']['path']).parent),'DOTNET_ROLL_FORWARD':'Disable','DOTNET_MULTILEVEL_LOOKUP':'0','DOTNET_CLI_TELEMETRY_OPTOUT':'1'})
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
                if managed:
                    proof=target/'managed-retained-proof.json';need(not proof.exists(),'Fresh managed proof required')
                    envVars.update({'YMCC_NATIVE_EXIT_MANAGED_PROGRAM':managed['program']['path'],'YMCC_NATIVE_EXIT_MANAGED_ASSEMBLY':managed['assembly']['path'],'YMCC_NATIVE_EXIT_MANAGED_PROOF':str(proof),'YMCC_NATIVE_EXIT_MANAGED_NONCE':uuid.uuid4().hex})
                    save(target/'managed-invocation.json',{'scenario':scenario,'program':managed['program'],'assembly':managed['assembly'],'proof':str(proof),'nonce':envVars['YMCC_NATIVE_EXIT_MANAGED_NONCE'],'environment':{key:envVars.get(key) for key in (*managedEnvKeys,'DOTNET_ROOT','DOTNET_ROLL_FORWARD','DOTNET_MULTILEVEL_LOOKUP')}})
                ran=helper.execute([exe,scenario],target,envVars,target/(scenario+'-run.log'),30)
                need(ran.stdout.strip(),'No scenario output: '+scenario)
                result=json.loads(ran.stdout);save(target/(scenario+'-result.json'),result)
                need(ran.returncode==0 and result['scopedLocalOwnerPass'] is True,'Actual local-owner regression failed: '+scenario+' '+str(target))
                if managed:
                    handoff=result.get('managedRetainedOwnerHandoff',{})
                    need(handoff.get('nativeOriginalExitOwnerExecuted') is True and handoff.get('sameOwnedProcessAndPrimary') is True and handoff.get('fixtureGateOpenedBeforeRetirement') is False and handoff.get('forcedOnce') is True and handoff.get('jobActiveProcessesBeforeClose')==0,'Actual managed-to-native owner return proof required')
                    raw=proof.read_bytes();need(json.loads(raw)==handoff['proof'],'Native result must consume exact managed proof bytes')
                    receipt['managedChild'].setdefault('proofs',[]).append({'path':str(proof),'bytes':len(raw),'sha256':sha(raw)})
                need(result['fixtureCleanup']['privateHandlesClosed'] and not result['fixtureCleanup']['childKillOrEventBackstop'],'Production must retire child/primary/pipe before fixture handle cleanup')
                scenarios.append({'name':scenario,'exitCode':ran.returncode,'wallSeconds':time.perf_counter()-scenarioStart,'executedThisRun':True,'result':result})
            imported=helper.execute([compiler.parent/'dumpbin.exe','/imports',exe],target,envVars,target/'imports.txt',15)
            need(imported.returncode==0 and not any(v in imported.stdout.lower() for v in helper.FORBIDDEN),'Unexpected device/UI/product import')
            receipt['profiles'].append({'name':profile,'binarySha256':sha(exe.read_bytes()),'scenarios':scenarios})
        if managed:
            verify_managed_inputs(managedPins);receipt['managedChild']['inputsStable']=True
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
        receipt['status']='ACTUAL_COORDINATOR_OBSERVER_CLOSE_SCOPED_PASS' if observerOnly else 'ACTUAL_NATIVE_EXIT_LOCAL_OWNERS_RETIRED_SCOPED_PASS';receipt['localOwnerRegressionVerified']=not observerOnly
        receipt['executedCases']=sum(len(p['scenarios']) for p in receipt['profiles'])
        receipt['executedAssertions']=sum(v['result'].get('assertionsPassed',0) for p in receipt['profiles'] for v in p['scenarios'])
        receipt['unexecutedLeafBoundaries']=['physical source reader Stop','battery shutdown','MSI post-hide writer/cancel (no writer armed)','deferred hide clear','cold-OFF carry','cleanup diagnostic snapshot'];receipt['scope']='Actual exit-retirement and exit-owner headers, real pipe Runtime. Whole current exitCleanupThreadProc + public inputHostStop/StopLocked/Clear + primary helper + complete final inputCaptureEnsureExitOwner and wait helper executed. Capture Stop body is an explicit typed debt/fault receipt (not actual blocking Stop); original 3 cases pre-retire it, R8 drives actual await/producer getter/Host authority/exit worker with stepped-clock budget; protocol/endpoint/physical and non-controller cleanup are named memory leaves; pipe fault IO uses existing real private HANDLE memory API. Private child and primary are real kernel resources. Whole wWinMain/UI loop and real managed SDK not executed.'
        if observerOnly:
            receipt['coordinatorObserverRegressionVerified']=True
            receipt['scope']='Actual original fixture fullPipeChildOutcome only. Real private pre-exited process HANDLE, protected CloseHandle refusal, exact-owner retention and same-owner successful retry. Other native exit functions compiled but not executed; controller/managed/full coordinator/device release not accepted.'
    except Exception as exc:
        receipt['error']=str(exc);save(out/'RESULT.json',receipt);raise
    save(out/'RESULT.json',receipt)
    print(json.dumps({'out':str(out),'status':receipt['status'],'localOwnerRegressionVerified':not observerOnly,'coordinatorObserverRegressionVerified':observerOnly,'fullProductPass':False,'mainSha256':receipt['mainSha256'],'profiles':[{'name':p['name'],'scenarios':[v['name'] for v in p['scenarios']]} for p in receipt['profiles']]}))
if __name__=='__main__':main()
