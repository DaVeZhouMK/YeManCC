#!/usr/bin/env python3
"""Verbatim Start/worker/Stop startup-delay replay on private kernel threads.

Only the owned thread/Sleep/wait/close port is real. COM is deliberately rejected
before any timer, capture, Host, device or service admission. Legacy and candidate
retain their frozen legacy identities; the current full Start/worker/typed Stop
bundle, Admission globals and real startup helpers execute verbatim. Arbitrary stalled disposal and hardware remain unaccepted.
"""
from __future__ import annotations
import argparse,datetime as dt,hashlib,importlib.util,json,os,re,sys,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
ret=None  # Producer dependencies are loaded only from the captured source closure.
BASE='tools/fixtures/input_capture_timer_retirement/startup_wait_baseline.json'
BASELINE_SHA256='7ff8eaa677c4e80907c70ad481af1e2eba2f07fba31393eee2bd1dc1adaa6fd8'
HELPERS=('inputCaptureAwaitStartupDelay','inputCaptureRequestStop','inputCaptureResetStartupStop')
SOURCES=('native/main.cpp','native/input_timer_evidence.h','native/input_capture_startup_wait.h','native/input_capture_completion.h','deps/json/json.hpp',
 'tools/input_capture_worker_retirement_selftest.py','tools/input_timer_evidence_adapter_selftest.py','tools/input_capture_startup_lifecycle_selftest.py',
 'tools/fixtures/input_capture_timer_retirement/actual_worker_fixture.cpp.in','tools/fixtures/input_capture_timer_retirement/startup_wait_fixture.cpp.in',
 'tools/fixtures/input_capture_timer_retirement/startup_kernel_port.cpp',BASE,
 'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json')
sha=lambda b:hashlib.sha256(b).hexdigest()
def need(c,m):
 if not c:raise RuntimeError(m)
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def helper_source(nl):
 return nl.join([
 'static ymcc::inputCapture::StartupWait g_inputCaptureStartupWait;',
 'static void inputCaptureAwaitStartupDelay() {',
 '    (void)g_inputCaptureStartupWait.waitForStop(g_inputCaptureStop, std::chrono::milliseconds(2500));',
 '}',
 'static void inputCaptureRequestStop() noexcept {',
 '    (void)g_inputCaptureStartupWait.requestStop(g_inputCaptureStop);',
 '}',
 'static bool inputCaptureResetStartupStop() noexcept {',
 '    if (g_inputCaptureStartupWait.reset(g_inputCaptureStop)) return true;',
 '    g_inputCaptureStop.store(true, std::memory_order_release);',
 '    return false;',
 '}',
 ])
def candidates(old):
 nl='\r\n' if '\r\n' in old['inputCaptureThreadProc'] else '\n';out=dict(old)
 need(out['inputCaptureThreadProc'].count('Sleep(2500);')==1,'Unique original startup Sleep')
 out['inputCaptureThreadProc']=out['inputCaptureThreadProc'].replace('Sleep(2500);','inputCaptureAwaitStartupDelay();',1)
 reset='    g_inputCaptureStop.store(false, std::memory_order_release);'
 new=nl.join(['    if (!inputCaptureResetStartupStop()) {','        publishStartupInputState(ymcc::StartupInputState::Failed, "capture-startup-wait-reset-failed");','        return;','    }'])
 need(out['inputCaptureStart'].count(reset)==1,'Unique original Start reset');out['inputCaptureStart']=out['inputCaptureStart'].replace(reset,new,1)
 stop='    g_inputCaptureStop.store(true, std::memory_order_release);'
 need(out['inputCaptureStop'].count(stop)==1,'Unique original Stop publication');out['inputCaptureStop']=out['inputCaptureStop'].replace(stop,'    inputCaptureRequestStop();',1)
 return out,helper_source(nl)

STATE=r'''
extern "C" {
void* startupKernelCreateThread(unsigned long(*)(void*),void*);
unsigned long startupKernelWait(void*,unsigned long);
int startupKernelClose(void*);
void* startupKernelDuplicate(void*);
void startupKernelSleep(unsigned long);
unsigned long startupKernelGetLastError();
void startupKernelSetLastError(unsigned long);
}
constexpr DWORD WAIT_FAILED=0xffffffff,ERROR_SUCCESS=0;
static HANDLE g_inputCaptureThread=nullptr;
struct StartupCase {
 std::mutex mutex;std::condition_variable cv;bool holdRole=false,holdRoleUntilRecovery=false,roleEntered=false,roleRelease=false,roleGateTimedOut=false,delayEntered=false;
 std::chrono::steady_clock::time_point delayBegan{};
 HANDLE product=nullptr;unsigned productCloseSuccesses=0,startCalls=0,applyCalls=0,assetsCalls=0;
 std::atomic<unsigned> coInitCalls{0},unexpectedCalls{0};std::string reason;
 std::vector<DWORD> waitResults,waitBudgets;std::vector<json> stopLogs;
 template<class F>bool waitFor(F predicate,unsigned budget){std::unique_lock<std::mutex> lock(mutex);return cv.wait_for(lock,std::chrono::milliseconds(budget),predicate);}
 void releaseRole(){std::lock_guard<std::mutex> lock(mutex);roleRelease=true;cv.notify_all();}
};
static StartupCase* startupCurrent=nullptr;
static void startupMarkDelay(){std::lock_guard<std::mutex> lock(startupCurrent->mutex);startupCurrent->delayEntered=true;startupCurrent->delayBegan=std::chrono::steady_clock::now();startupCurrent->cv.notify_all();}
'''
PORTS={
 'Sleep':r'''static void Sleep(DWORD budget){startupMarkDelay();startupKernelSleep(budget);}''',
 'CoInitializeEx':r'''static HRESULT CoInitializeEx(LPVOID,unsigned){++startupCurrent->coInitCalls;return -1;}''',
 'ymccLogThreadRole':r'''static void ymccLogThreadRole(const char*){
 std::unique_lock<std::mutex> lock(startupCurrent->mutex);startupCurrent->roleEntered=true;startupCurrent->cv.notify_all();
 if(startupCurrent->holdRole&&!startupCurrent->cv.wait_for(lock,std::chrono::seconds(5),[]{return startupCurrent->roleRelease;}))startupCurrent->roleGateTimedOut=true;
}''',
 'WaitForSingleObject':r'''static DWORD WaitForSingleObject(HANDLE handle,DWORD budget){
 if(!startupCurrent->holdRoleUntilRecovery)startupCurrent->releaseRole();const DWORD value=startupKernelWait(handle,budget);const DWORD error=startupKernelGetLastError();
 startupCurrent->waitResults.push_back(value);startupCurrent->waitBudgets.push_back(budget);startupKernelSetLastError(error);return value;
}''',
 'CloseHandle':r'''static bool CloseHandle(HANDLE handle){const int value=startupKernelClose(handle);const DWORD error=startupKernelGetLastError();if(value&&handle==startupCurrent->product)++startupCurrent->productCloseSuccesses;startupKernelSetLastError(error);return value!=0;}''',
 'appendNativeLifecycleLog':r'''static void appendNativeLifecycleLog(const char*event,const json&detail){if(std::string(event)=="input-capture-worker-stop")startupCurrent->stopLogs.push_back(detail);}''',
 'publishStartupInputState':r'''static void publishStartupInputState(ymcc::StartupInputState,const char*reason){std::lock_guard<std::mutex> lock(startupCurrent->mutex);startupCurrent->reason=reason;}''',
}
AUX=r'''
static DWORD GetLastError(){return startupKernelGetLastError();}
enum class YmccFamily {Generic,LenovoLegionGo};static struct {YmccFamily family=YmccFamily::Generic;}g_machineIdentity;
static void legionMotionRevoke(){}
static void dsu_stop(){}
static std::atomic<bool> g_exitRequested{false};
static bool inputHostExitFenceRaised(){return false;}
static void gamebarTraceRequestStop(){}
static std::uint64_t inputCaptureRetireResources(bool){return 0;}
static void inputHostStop(){}
static void gamebarTraceStartInitThread(){++startupCurrent->startCalls;}
static bool applyInputStartupOnce(){++startupCurrent->applyCalls;return true;}
static bool inputCaptureAssetsPresent(){++startupCurrent->assetsCalls;return true;}
static HANDLE CreateThread(LPVOID,std::size_t,DWORD(*entry)(LPVOID),LPVOID arg,DWORD,LPVOID){HANDLE result=startupKernelCreateThread(entry,arg);startupCurrent->product=result;return result;}
'''
def build_cpp(base_text,outer,parts,globals,bundle,helpers,safe):
 # Take only the source-consumer prefix. Other worker schedules can add fake
 # startup helpers; remove those leaves before installing the real helper bundle.
 need(base_text.count('@WORKER@')==1,'Reviewed worker fixture boundary changed')
 prefix=base_text[:base_text.index('@WORKER@')+len('@WORKER@')]
 for name in HELPERS:
  if re.search(r'\b'+name+r'\s*\(',ret.helper.mask_cpp(prefix)):
   existing=ret.function(prefix,name)['text']
   need(prefix.count(existing)==1,'Unique fake startup helper '+name)
   prefix=prefix.replace(existing,'',1)
 for name,port in PORTS.items():
  existing=ret.function(prefix,name)['text'];need(prefix.count(existing)==1,'Unique startup port '+name);prefix=prefix.replace(existing,port,1)
 runtime=helpers if safe else ''
 runtime+='\n'+ret.CAPTURE_RUNTIME
 if safe:
  include='#include "input_timer_evidence.h"'
  need(prefix.count(include)==1,'Unique timer header include')
  prefix=prefix.replace(include,include+'\n#include "input_capture_startup_wait.h"',1)
  runtime+='\nstatic void observedInputCaptureAwaitStartupDelay(){startupMarkDelay();inputCaptureAwaitStartupDelay();}\n'
  prefix=prefix.replace('@WORKER@','#define inputCaptureAwaitStartupDelay observedInputCaptureAwaitStartupDelay\n@WORKER@\n#undef inputCaptureAwaitStartupDelay')
 need(prefix.count('@GLOBALS@')==1,'Unique startup globals boundary')
 prefix=prefix.replace('@GLOBALS@','@GLOBALS@\n'+STATE+'\n'+runtime+'\nstatic DWORD GetLastError();')
 text=outer.replace('@BASE_PREFIX@',prefix).replace('@START_AUXILIARY@',AUX).replace('@PRODUCTION_START@',bundle['inputCaptureStart']).replace('@PRODUCTION_STOP@',bundle['inputCaptureStop'])
 rendered=ret.render(text,parts,globals,bundle['inputCaptureThreadProc'])
 need(not re.search(r'@[A-Z_]+@',rendered),'Unexpanded startup source tag')
 return rendered

def load_snapshot_tools(root):
 global ret
 spec=importlib.util.spec_from_file_location('startup_snapshot_ret',root/'tools/input_capture_worker_retirement_selftest.py')
 ret=importlib.util.module_from_spec(spec);spec.loader.exec_module(ret)
 need(ret.helper.__file__==str(root/'tools/input_timer_evidence_adapter_selftest.py'),'Python producer dependency escaped source snapshot')

def checked_local_includes(dependencies,snapshot,source):
 included=set()
 for name in dependencies['Data']['Includes']:
  path=Path(name).resolve()
  if path.is_relative_to(snapshot):
   name=path.relative_to(snapshot).as_posix()
   need(name in source and path.read_bytes()==source[name],'Uncaptured or changed local include '+name)
   included.add(name)
  else:
   need(not path.is_relative_to(ROOT),'Compile read a live repository dependency '+str(path))
 return sorted(included)

def settled_case(case):
 return (case['actualStartWorkerStopExecuted'] and case['ownedThreadJoined'] and
         case['ownershipSettled'] and case['ownedHandlesRemaining']==0 and
         case['productHandleClosed'] and case['controllerHandleClosed'] and
         case['contextDestroyed'] and not case['contextRetained'] and
         not case['controllerClosedBeforeJoin'] and not case['processExitCountsAsCleanup'] and
         case['unexpectedHardwareTimerCalls']==0 and not case['roleGateTimedOut'])

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--output-root',required=True,type=Path)
 ap.add_argument('--prepare-only',action='store_true');ap.add_argument('--profile',choices=['Release','Checked'])
 args=ap.parse_args();out=args.output_root.resolve()
 run=out/('r'+dt.datetime.now().strftime('%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
 receipt={'schemaVersion':1,'pass':False,'status':'RUNNING','evidenceComplete':False,
  'productionStartupWaitAccepted':False,'currentProductionCooperativeWait':False,
  'run':str(run),'profiles':[],'negativeProfiles':[],
  'sourceStable':False,'sourceClosureVerified':False,'wholeLifecycleAcceptance':False,
  'realOwnedKernelThreadWaitCloseAndSleep':False,'realOwnedKernelThreadWaitCloseAndEventDelay':True,'realKernelSleepCalled':False,
  'legacyDelayLeaf':'owned unnamed kernel event timed wait; no Sleep API',
  'realDeviceHostServiceOrWinmm':False,'actualRetireResourcesBodyExecuted':False,
  'arbitraryStalledProductionDestructionProven':False,'processExitCountsAsCleanup':False,
  'remaining':['UNKNOWN: arbitrary stuck capture/Host shutdown',
   'UNKNOWN: actual physical source/controller return',
   'UNKNOWN: normal COM/HID/timer admission is not exercised by this startup-only consumer']}
 def publish():save(run/'RESULT.json',receipt);save(out/'RESULT.json',receipt)
 source={}
 try:
  source={n:(ROOT/n).read_bytes() for n in SOURCES}
  receipt['sourceHashes']={n:sha(b) for n,b in source.items()}
  snapshot=run/'source'
  for n,b in source.items():
   p=snapshot/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
  load_snapshot_tools(snapshot);ret.verify_freeze(source,receipt)
  text=source['native/main.cpp'].decode('utf-8-sig');parts,globals=ret.extract(text)
  names=('inputCaptureStart','inputCaptureThreadProc','inputCaptureStop')
  current={n:ret.function(text,n)['text'] for n in names}
  need(sha(source[BASE])==BASELINE_SHA256,'Frozen legacy baseline bytes changed')
  frozen=json.loads(source[BASE]);old=frozen['functions']
  need(set(old)==set(names),'Frozen startup bundle is incomplete')
  need(all(sha(code.encode())==frozen['functionHashes'][n] for n,code in old.items()),'Frozen startup source identities changed')
  _,helpers=candidates(old);candidate=current
  current_safe='ymcc::captureLifecycle::StopReceipt' in current['inputCaptureStop'] and 'g_captureAdmission.beginStart()' in current['inputCaptureStart']
  receipt['actualFunctionHashes']={n:sha(code.encode()) for n,code in current.items()}
  receipt['legacyFunctionHashes']=frozen['functionHashes']
  receipt['candidateFunctionHashes']={n:sha(code.encode()) for n,code in candidate.items()}
  receipt['consumerSourceClosure']={n:sha(part['text'].encode()) for n,part in parts.items()}
  receipt['consumerSourceClosure']['globals']=sha(globals.encode())
  need(current_safe,'UNKNOWN: current Start/Stop typed admission contract unavailable')
  if current_safe:
   actual_helpers={name:ret.function(text,name)['text'] for name in HELPERS}
   declaration='static ymcc::inputCapture::StartupWait g_inputCaptureStartupWait;'
   matches=re.findall(r'^static[^;\n]*\bg_inputCaptureStartupWait\b[^;]*;',text,re.M)
   need(matches==[declaration],'Current startup synchronization owner declaration changed')
   need(re.search(r'^#include "input_capture_startup_wait.h"\s*$',text,re.M),'Production startup helper header missing')
   # Compile actual current auxiliary definitions, after exact candidate review.
   helpers=declaration+'\n'+'\n'.join(actual_helpers.values())
   receipt['actualAuxiliaryHashes']={n:sha(code.encode()) for n,code in actual_helpers.items()}
   receipt['actualAuxiliaryHashes']['ownerDeclaration']=sha(declaration.encode())
  receipt['currentProductionCooperativeWait']=current_safe
  receipt['sourceRoute']='cooperative' if current_safe else 'legacy'
  compiler,env=ret.helper.compiler_environment(run)
  authority=json.loads(source['tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'])
  need(Path(authority['native']['compiler']).resolve()==compiler.resolve(),'Current compiler authority mismatch')
  env['INCLUDE']=';'.join(authority['native']['include']);env['LIB']=';'.join(authority['native']['lib'])
  temp=run/'t';temp.mkdir();env['TEMP']=str(temp);env['TMP']=str(temp)
  receipt['compiler']={'path':str(compiler),'sha256':sha(compiler.read_bytes()),
   'vcvarsPath':str(ret.helper.VCVARS),'vcvarsSha256':sha(ret.helper.VCVARS.read_bytes()),'sdkVersion':env.get('WindowsSDKVersion')}
  publish()
  for profile,short,flags in [('Release','r',['/O2','/MT']),('Checked','c',['/Od','/RTC1','/D_DEBUG','/MTd'])]:
   if args.profile and args.profile!=profile:continue
   for route,bundle,safe in [('legacy',old,False),('cooperative',candidate,True)]:
    is_current=current_safe==safe
    if is_current:bundle=current
    folder=run/(short+('l' if route=='legacy' else 'c'));folder.mkdir()
    cpp=folder/'startup.cpp';port=folder/'kernel.cpp';exe=folder/'startup.exe'
    cpp.write_text(build_cpp(source['tools/fixtures/input_capture_timer_retirement/actual_worker_fixture.cpp.in'].decode('utf-8'),
     source['tools/fixtures/input_capture_timer_retirement/startup_wait_fixture.cpp.in'].decode('utf-8'),parts,globals,bundle,helpers,safe).replace('\r\n','\n'),encoding='utf-8',newline='\n')
    port.write_bytes(source['tools/fixtures/input_capture_timer_retirement/startup_kernel_port.cpp'])
    common=[compiler,'/nologo','/std:c++20','/utf-8','/EHsc','/permissive-','/W4',*flags,
     '/I'+str(snapshot/'native'),'/I'+str(snapshot/'deps/json')]
    local_includes=set()
    for name in ['startup','kernel']:
     dependencies=folder/(name+'-deps.json')
     built=ret.helper.execute([*common,'/sourceDependencies',dependencies,'/c',folder/(name+'.cpp'),'/Fo'+str(folder/(name+'.obj'))],folder,env,folder/(name+'-build.log'),90)
     need(built.returncode==0,'Startup source consumer compile failed: '+str(folder/(name+'-build.log')))
     dep=json.loads(dependencies.read_text(encoding='utf-8-sig'))
     local_includes.update(checked_local_includes(dep,snapshot,source))
     inputs=[dep['Data']['Source'],*dep['Data']['Includes']]
     save(folder/(name+'-inputs.json'),[{'path':n,'sha256':sha(Path(n).read_bytes())} for n in inputs])
    expected={'native/input_timer_evidence.h','deps/json/json.hpp'}
    if safe:expected.add('native/input_capture_startup_wait.h')
    need(expected<=local_includes,'Compiler did not verify the expected local header closure')
    linked=ret.helper.execute([compiler,'/nologo',*flags,folder/'startup.obj',folder/'kernel.obj','/Fe'+str(exe),
     '/link','kernel32.lib','/INCREMENTAL:NO'],folder,env,folder/'link.log',60)
    need(linked.returncode==0,'Startup consumer link failed: '+str(folder/'link.log'))
    identity={'profile':profile,'route':route,'isCurrentProductionRoute':is_current,
     'executableSha256':sha(exe.read_bytes()),'sourceSha256':sha(cpp.read_bytes()),'localIncludes':sorted(local_includes),
     'compiledFunctionHashes':{n:sha(code.encode()) for n,code in bundle.items()}}
    if args.prepare_only:
     receipt['profiles'].append({**identity,'compiled':True,'executed':False});publish();continue
    for negative in ['', 'controller-timeout','controller-wait-failed']:
     argv=[exe,'safe' if safe else 'legacy']+([negative] if negative else [])
     proc=ret.helper.execute(argv,folder,env,folder/((negative or 'run')+'.log'),30)
     value=json.loads(proc.stdout.strip().splitlines()[-1])
     row={**identity,'pass':value.get('pass') is True,'exitCode':proc.returncode,
      'checks':value['checks'],'failures':value['failures'],'cases':value['cases'],'retainedOwners':value['retainedOwners']}
     if negative:
      row['negative']=negative;row['expectedFailureCaught']=False;receipt['negativeProfiles'].append(row)
      publish()
      need(proc.returncode==1 and value['pass'] is False and value['failures']==1 and
       'CHECK_FAILED controller positively joined actual production worker' in proc.stderr,
       'Failure-path negative did not retain the original join failure: '+negative)
      need(len(value['cases'])==1 and value['retainedOwners']==0 and not value['processExitCountsAsCleanup'],
       'Failure-path negative ownership accounting incomplete')
      case=value['cases'][0];wanted=258 if negative=='controller-timeout' else 0xffffffff
      need(case['controllerJoinResult']==wanted and not case['pass'] and
       case['retainedAfterInitialJoinFailure'] and case['recoveryWaitPerformed'] and
       case['recoveryJoinResult']==0 and settled_case(case),
       'Failure-path negative did not positively join/close before destroying its context')
      if negative=='controller-wait-failed':
       need(case['controllerWaitUsesInvalidNullProbe'] and case['controllerJoinWin32']!=0,'Failed-wait negative did not use the explicit kernel failure probe')
       if safe:need(case['productHandleClosed'] and not case['retainedAtFirstReturn'],
        'Cooperative failure probe must cover an already-closed original product handle')
      else:need(case['firstWaitResult']==258 and case['retainedAtFirstReturn'],'Timeout negative must hold a real worker across Stop')
      row['expectedFailureCaught']=True
     else:
      receipt['profiles'].append(row);publish()
      need(proc.returncode==0 and value['pass'] and value['failures']==0,'Startup source runtime failed: '+str(folder/'run.log'))
      need(len(value['cases'])==3 and {c['scenario'] for c in value['cases']}=={0,1,2} and
       value['retainedOwners']==0 and not value['processExitCountsAsCleanup'] and
       all(c['pass'] and settled_case(c) for c in value['cases']),'Startup fixture ownership or no-hardware boundary failed')
     print(profile+' '+route+' '+(negative or 'normal')+': '+str(value['checks'])+' checks'+(' (expected FAIL, safely settled)' if negative else ''),flush=True)
     publish()
  changed=[n for n,b in source.items() if (ROOT/n).read_bytes()!=b]
  receipt['changedSources']=changed;need(not changed,'Startup source changed while running: '+', '.join(changed))
  if args.prepare_only:
   receipt.update(status='PREPARED_NOT_EXECUTED',sourceStable=True,sourceClosureVerified=True);publish();return 0
  receipt.update(sourceStable=True,sourceClosureVerified=True,evidenceComplete=True,productionStartupWaitAccepted=current_safe)
  receipt['pass']=current_safe
  receipt['status']='PASS' if current_safe else 'PRODUCTION_STARTUP_DELAY_GAP_CONFIRMED_CANDIDATE_ONLY'
  receipt['checks']=sum(p['checks'] for p in receipt['profiles']+receipt['negativeProfiles'])
  publish();print(receipt['status']+' RESULT='+str(out/'RESULT.json'));return 0 if current_safe else 2
 except Exception as error:
  receipt.update(status='FAIL',evidenceComplete=False,productionStartupWaitAccepted=False)
  receipt['pass']=False;receipt['error']=str(error)
  if source:
   receipt['changedSources']=[n for n,b in source.items() if not (ROOT/n).is_file() or (ROOT/n).read_bytes()!=b]
  if isinstance(error,subprocess.TimeoutExpired):
   receipt['resourceDisposalStatus']='UNKNOWN: timed-out child execution is not evidence of joined/closed resources'
  publish();print(str(error),file=sys.stderr);return 1
if __name__=='__main__':raise SystemExit(main())
