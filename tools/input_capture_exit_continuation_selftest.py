#!/usr/bin/env python3
"""Current WM_DESTROY exitRequested path + main finally on owned kernel leaves.

Current EnsureExitOwner, AwaitStopForExit and complete background owner run
verbatim. Stop notification is an explicit atomic-only leaf. Other application subsystems are memory leaves. The owned
worker is a bounded held-leaf adversary, not the real capture/Host. A diagnostic
reproduction is not production continuation, resource disposal or field proof.
"""
from pathlib import Path
import argparse,datetime as dt,hashlib,importlib.util,json,os,re,sys
ROOT=Path(__file__).resolve().parents[1]
RUNNER='tools/input_capture_exit_continuation_selftest.py'
TEMPLATE='tools/fixtures/input_capture_timer_retirement/exit_continuation_fixture.cpp.in'
BASE='tools/fixtures/input_capture_timer_retirement/stop_wait_kernel_fixture.cpp.in'
AUTH='tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'
HISTORICAL=Path(r'G:\YeManCC-Work\Testing\YMCC\Evidence\VC-CaptureReceipt23\main.before.cpp')
HISTORICAL_SHA='3469970958a8050f95d1041ad8b59c25d983a1fb35faff57e6c64f7699d7e2c0'
SOURCES=('native/main.cpp','native/input_capture_completion.h','native/exit_cleanup_wait.h','deps/json/json.hpp',RUNNER,TEMPLATE,BASE,
 'tools/input_capture_worker_retirement_selftest.py','tools/input_timer_evidence_adapter_selftest.py',AUTH)
sha=lambda b:hashlib.sha256(b).hexdigest()
def need(v,m):
 if not v:raise RuntimeError(m)
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def block_end(masked,opening):
 need(masked[opening]=='{','Expected opening brace');depth=1;at=opening+1
 while depth:
  need(at<len(masked),'Unclosed actual source block');depth+=(masked[at]=='{')-(masked[at]=='}');at+=1
 return at

def extract_current(ret,text,typed=True):
 wnd=ret.function(text,'WndProc')['text'];masked=ret.helper.mask_cpp(wnd)
 cases=list(re.finditer(r'\bcase\s+WM_DESTROY\s*:',masked));need(len(cases)==1,'Unique real WndProc WM_DESTROY required')
 begin=cases[0].end();matches=list(re.finditer(r'\bif\s*\(g_exitRequested\.load\(\)\)\s*\{',masked[begin:]));need(matches,'Actual exitRequested path missing')
 opening=masked.index('{',begin+matches[0].start());end=block_end(masked,opening);destroy=wnd[begin:end]
 need(destroy.count('inputCaptureRequestStop();' if typed else 'inputCaptureStop();')==1 and destroy.count('PostQuitMessage(')==1,'Actual destroy/quit boundary changed')
 main_at=text.index('int WINAPI wWinMain(');m=ret.helper.mask_cpp(text);main_open=m.index('{',main_at);main_end=block_end(m,main_open)
 main=text[main_at:main_end];anchor='closeScreenTouchpadsForExit("message-loop-finally");';need(main.count(anchor)==1,'Unique real main-finally boundary required')
 start=main.rfind('    g_optiCatalogStartup.stop();',0,main.index(anchor));need(start>=0,'Actual final cleanup start changed')
 if typed:
  gate=main.rfind('    inputCaptureEnsureExitOwner(msg.wParam);',0,start);need(gate>=0,'Actual final gate must be included');start=gate
 final=main[start:main.rfind('}')];need(final.count('CoUninitialize();')==1 and 'return (int)msg.wParam;' in final,'Actual final-return boundary changed')
 stop=ret.function(text,'inputCaptureStop')['text'];classifier=ret.function(text,'nativeLifecycleEventIsBase')['text']
 return {'destroyExitRequestedPath':destroy,'mainFinally':final,'inputCaptureStop':stop,'nativeLifecycleEventIsBase':classifier}

UI_BOUNDARY='// UI_LOOP_CONSUMER_BOUNDARY'
PRE_LOOP=HISTORICAL.parent/'main.before-emergency.cpp'
PRE_LOOP_SHA='01a00d9424e1ccb26af3daa88f6ba3be7f79fdf3eb99b20ec467753c804d1343'
def extract_ui(ret,text):
 wnd=ret.function(text,'WndProc')['text'];mask=ret.helper.mask_cpp(wnd)
 label=list(re.finditer(r'\bcase\s+WM_APP_EXIT_READY\s*:',mask));need(len(label)==1,'Unique actual READY case required')
 opening=mask.index('{',label[0].end());ready=wnd[label[0].start():block_end(mask,opening)]
 at=text.index('int WINAPI wWinMain(');masked=ret.helper.mask_cpp(text)
 opening=masked.index('{',at);main=text[at:block_end(masked,opening)]
 start=main.index('    MSG msg{};');loop_at=main.index('for (;;)',start);m=ret.helper.mask_cpp(main)
 opening=m.index('{',loop_at);loop=main[start:block_end(m,opening)]
 need('waitForCleanupOrMessages(cleanup)' in loop and 'SendMessageW(' in loop and 'PeekMessageW(' in loop,'Incomplete actual normal-loop extraction')
 return {'normalMainLoop':loop,'wmAppExitReady':ready}
def render_ui(ret,template,text,parts):
 constants=sorted(set(re.findall(r'\b(?:[A-Z][A-Z0-9_]*TIMER_ID|TIMER_ID_[A-Z0-9_]+)\b',parts['wmAppExitReady'])))
 constants=['static constexpr UINT_PTR '+n+'='+str(i+1)+';' for i,n in enumerate(constants)]
 rendered=template.replace('@UI_CAPTURE_GLOBALS@',ret.capture_declarations(text)).replace('@UI_CAPTURE_GATE@',ret.function(text,'inputCaptureExitOwnershipReleased')['text'])
 rendered=rendered.replace('@UI_ENSURE_EXIT_OWNER@',ret.function(text,'inputCaptureEnsureExitOwner')['text'])
 rendered=rendered.replace('@UI_TIMER_CONSTANTS@','\n'.join(constants)).replace('@UI_READY_CASE@',parts['wmAppExitReady']).replace('@UI_MAIN_LOOP@',parts['normalMainLoop'])
 need(not re.search(r'@[A-Z_]+@',rendered),'Unexpanded UI loop consumer marker')
 return rendered

def run_ui_profiles(ret,source,text,prior,run,snap,compiler,env,result,publish,args):
 current=extract_ui(ret,text);old=extract_ui(ret,prior.decode('utf-8-sig'))
 result['uiActualFragments']={n:sha(v.encode()) for n,v in current.items()}
 result['uiPriorFragments']={n:sha(v.encode()) for n,v in old.items()}
 result['uiPriorMainSha256']=PRE_LOOP_SHA;result['uiProfiles']=[];result['helperWaitFailureProfiles']=[]
 template=source[TEMPLATE].decode().split(UI_BOUNDARY,1)[1]
 for profile,flags in [('Release',['/O2','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/MTd'])]:
  if args.profile and args.profile!=profile:continue
  for route,fragments in [('current',current),('prior-negative',old)]:
   folder=run/('ui-'+profile+'-'+route);folder.mkdir();cpp=folder/'ui-loop.cpp';exe=folder/'ui-loop.exe';deps=folder/'deps.json'
   cpp.write_text(render_ui(ret,template,text,fragments),encoding='utf-8',newline='\n')
   built=ret.helper.execute([compiler,'/nologo','/std:c++20','/utf-8','/EHsc','/permissive-','/W4',*flags,'/sourceDependencies',deps,
    '/I'+str(snap/'native'),'/I'+str(snap/'deps/json'),cpp,'/Fo'+str(folder/'ui-loop.obj'),'/Fe'+str(exe),'/link','kernel32.lib','user32.lib','/INCREMENTAL:NO'],folder,env,folder/'build.log',90)
   need(built.returncode==0,'Actual normal-loop/READY compile failed: '+str(folder/'build.log'))
   for path in json.loads(deps.read_text(encoding='utf-8-sig'))['Data']['Includes']:
    need(not Path(path).resolve().is_relative_to(ROOT),'UI consumer compiled live repository include '+path)
   row={'profile':profile,'route':route,'compiled':True,'executed':False,'sourceSha256':sha(cpp.read_bytes()),'executableSha256':sha(exe.read_bytes())}
   result['uiProfiles'].append(row);publish()
   if args.prepare_only:continue
   proc=ret.helper.execute([exe],folder,env,folder/'run.log',40);value=json.loads(proc.stdout.strip().splitlines()[-1])
   need(len(value['cases'])==9,'Normal-loop/READY matrix incomplete')
   need(all(c['ownedCleanupThreadJoined'] and c['controllerJoined'] and c['ownedHandlesRemaining']==0 and not c['processExitUsedAsCleanup'] for c in value['cases']),'UI case lost owned resources')
   if route=='current':
    need(proc.returncode==0 and value['pass'] and value['failures']==0,'Actual normal-loop/READY assertions failed: '+str(folder/'run.log'))
    need(all(c['messageOnlyWaits']>0 and c['dispatches']>0 and not c['controllerSalvagedProductHandle'] and not c['immediateWakeBudgetCaught'] for c in value['cases']),'Current loop missed message-only park/Peek or needed controller salvage')
   else:
    need(proc.returncode==1 and not value['pass'],'Prior normal-loop/READY must fail current ownership/spin contract')
    need(any(c['immediateWakeBudgetCaught'] for c in value['cases']) and any(c['completeBeforeClose'] and c['closeFailures']==1 for c in value['cases']) and any(c['closeWithoutBothPermits'] for c in value['cases']),'Prior source did not retain spin, failed-Close and lost-window permit negatives')
   row.update(executed=True,checks=value['checks'],failures=value['failures'],cases=value['cases'],expectedFailureCaught=route!='current',exitCode=proc.returncode)
   print(profile+' normal-loop '+route+': '+str(value['checks'])+' checks; failures='+str(value['failures']),flush=True);publish()
   if route=='current':
    failure=ret.helper.execute([exe,'helper-wait-failed'],folder,env,folder/'helper-wait-failed.log',20)
    fault=json.loads(failure.stdout.strip().splitlines()[-1]);need(len(fault['cases'])==1,'Actual helper failure consumer missing')
    case=fault['cases'][0]
    need(case['ownedCleanupThreadJoined'] and case['controllerJoined'] and case['ownedHandlesRemaining']==0 and not case['processExitUsedAsCleanup'],'Helper-fault ownership cleanup not positively proven')
    known_gap=failure.returncode==1 and not fault['pass'] and case['immediateWakeBudgetCaught'] and case['messageOnlyWaits']==0
    if known_gap:need(case['forcedWaitFailures']==8 and case['ownerRetainedAfterFailedWait'] and 'CHECK_FAILED EnsureExitOwner WAIT_FAILED enters message-only blocking wait' in failure.stderr,'Helper WAIT_FAILED gap not reproduced at intended boundary')
    else:need(failure.returncode==0 and fault['pass'] and case['messageOnlyWaits']>0,'Unexpected helper-fault consumer failure: '+str(folder/'helper-wait-failed.log'))
    result['helperWaitFailureProfiles'].append({'profile':profile,'pass':not known_gap,'checks':fault['checks'],'failures':fault['failures'],
     'status':'PRODUCTION_ENSURE_EXIT_OWNER_WAIT_FAILED_SPIN' if known_gap else 'PASS','cases':fault['cases'],'exitCode':failure.returncode})
    print(profile+' EnsureExitOwner WAIT_FAILED: '+('FAIL (spin and missing message-only wait reproduced)' if known_gap else 'PASS'),flush=True);publish()


def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--output-root',required=True,type=Path)
 ap.add_argument('--prepare-only',action='store_true');ap.add_argument('--profile',choices=['Release','Checked'])
 ap.add_argument('--ui-only',action='store_true',help='Only execute the normal-loop/READY and helper-fault consumers')
 ap.add_argument('--main-sha256',help='Assert the currently approved freeze identity; historical or arbitrary SHA is rejected')
 args=ap.parse_args();out=args.output_root.resolve();run=out/('r'+dt.datetime.now().strftime('%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
 result={'schemaVersion':1,'status':'RUNNING','pass':False,'diagnosticEvidenceComplete':False,'productionExitContinuationAccepted':False,
  'wholeLifecycleAcceptance':False,'installedApplicationExecuted':False,'realOwnedKernelObjectsOnly':True,'realCaptureOrHostExecuted':False,
  'startupSynchronizationProven':False,'run':str(run),'profiles':[],
  'historicalStatus':'PRODUCTION_QUIT_BEFORE_CAPTURE_SETTLED','historicalPass':False,
  'arbitraryBlockedParentBoundedDestruction':'UNKNOWN','realChildResourcesExecuted':False,
  'retainedDeadlineClockLeaf':'explicit advancing memory clock; no real 30-second elapsed-time claim',
  'actualRetireResourcesBodyExecuted':False,'exitScope':'Actual normal main loop and complete WM_APP_EXIT_READY, exitRequested=true WM_DESTROY and full main finally; actual gate/background owner/AwaitStopForExit; memory message/child leaves'}
 def publish():save(run/'RESULT.json',result);save(out/'RESULT.json',result)
 try:
  source={n:(ROOT/n).read_bytes() for n in SOURCES};snap=run/'source'
  for n,b in source.items():p=snap/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
  prior=PRE_LOOP.read_bytes();need(sha(prior)==PRE_LOOP_SHA,'Pre-emergency normal-loop baseline identity mismatch')
  historical=HISTORICAL.read_bytes();need(sha(historical)==HISTORICAL_SHA,'Frozen pre-change main identity mismatch')
  (snap/'historical').mkdir();(snap/'historical/main.before.cpp').write_bytes(historical);(snap/'historical/main.before-emergency.cpp').write_bytes(prior)
  result['sourceHashes']={n:sha(b) for n,b in source.items()};result['historicalMainSha256']=HISTORICAL_SHA
  spec=importlib.util.spec_from_file_location('exit_snapshot_ret',snap/'tools/input_capture_worker_retirement_selftest.py');ret=importlib.util.module_from_spec(spec);spec.loader.exec_module(ret)
  need(ret.helper.__file__==str(snap/'tools/input_timer_evidence_adapter_selftest.py'),'Producer dependency escaped snapshot')
  ret.verify_freeze(source,result,args.main_sha256)
  text=source['native/main.cpp'].decode('utf-8-sig')
  actual=extract_current(ret,text);old=extract_current(ret,historical.decode('utf-8-sig'),False)
  owners={n:ret.function(text,n)['text'] for n in ['inputCaptureExitOwnershipReleased','inputCaptureAwaitStopForExit','exitCleanupThreadProc','tryStartExitCleanupWorker','inputCaptureEnsureExitOwner']}
  result['actualFragments']={n:sha(b.encode()) for n,b in {**actual,**owners}.items()}
  result['historicalFragments']={n:sha(b.encode()) for n,b in old.items()}
  for n,b in {**actual,**owners}.items():(run/(n+'.cpp-fragment')).write_text(b,encoding='utf-8',newline='\n')
  compiler,env=ret.helper.compiler_environment(run);authority=json.loads(source[AUTH]);need(Path(authority['native']['compiler']).resolve()==compiler.resolve(),'Compiler declaration mismatch')
  expected=next(row['sha256'] for row in authority['files'] if Path(row['path']).resolve()==compiler.resolve());need(sha(compiler.read_bytes())==expected,'Compiler changed')
  env['INCLUDE']=';'.join(authority['native']['include']);env['LIB']=';'.join(authority['native']['lib'])
  result['compiler']={'path':str(compiler),'sha256':expected,'sdkVersion':env.get('WindowsSDKVersion')}
  for profile,flags in [('Release',['/O2','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/MTd'])]:
   if args.ui_only or (args.profile and profile!=args.profile):continue
   for route,parts,typed in [('historical',old,False),('current',actual,True)]:
    base=source[BASE].decode();marker='static void verifyLog(';need(base.count(marker)==1,'Reviewed kernel prefix changed');prefix=base[:base.index(marker)]
    prefix=ret.bind_stop(prefix,text,parts['inputCaptureStop'],parts['nativeLifecycleEventIsBase'])
    # Child retirement is a declared memory leaf, never a fake completion gate.
    prefix=prefix.replace('static void inputHostStop() {','static void inputHostStop(const char* = nullptr) {')
    prefix=prefix.replace('static std::uint64_t inputCaptureRetireResources(bool){dsu_stop();inputHostStop();return 0;}',
     'static unsigned childRetirement();\nstatic std::uint64_t inputCaptureRetireResources(bool){dsu_stop();inputHostStop();return childRetirement();}')
    cpp_text=source[TEMPLATE].decode().split(UI_BOUNDARY,1)[0].replace('@KERNEL_PREFIX@',prefix).replace('@TYPED_EXIT@','1' if typed else '0')
    cpp_text=cpp_text.replace('@ACTUAL_EXIT_OWNERS@','\n'.join(owners.values()) if typed else '')
    cpp_text=cpp_text.replace('@DESTROY_EXIT_PATH@',parts['destroyExitRequestedPath']).replace('@MAIN_FINALLY@',parts['mainFinally'])
    need(not re.search(r'@[A-Z_]+@',cpp_text),'Unexpanded consumer marker')
    folder=run/(profile+'-'+route);folder.mkdir();cpp=folder/'exit.cpp';exe=folder/'exit.exe';deps=folder/'deps.json';cpp.write_text(cpp_text,encoding='utf-8',newline='\n')
    build=ret.helper.execute([compiler,'/nologo','/std:c++20','/utf-8','/EHsc','/permissive-','/W4',*flags,'/sourceDependencies',deps,
     '/I'+str(snap/'native'),'/I'+str(snap/'deps/json'),cpp,'/Fo'+str(folder/'exit.obj'),'/Fe'+str(exe),'/link','kernel32.lib','user32.lib','/INCREMENTAL:NO'],folder,env,folder/'build.log',90)
    need(build.returncode==0,'Actual exit owner consumer compile failed: '+str(folder/'build.log'))
    includes=json.loads(deps.read_text(encoding='utf-8-sig'))['Data']['Includes']
    for dep in includes:need(not Path(dep).resolve().is_relative_to(ROOT),'Compiler consumed live repository dependency '+dep)
    if typed:need(any(Path(dep).resolve()==(snap/'native/exit_cleanup_wait.h').resolve() for dep in includes),'Actual exit message/owner wait header not compiled')
    row={'profile':profile,'route':route,'compiled':True,'executed':False,'sourceSha256':sha(cpp.read_bytes()),'executableSha256':sha(exe.read_bytes())}
    result['profiles'].append(row);publish()
    if args.prepare_only:continue
    observed=ret.helper.execute([exe],folder,env,folder/'run.log',45);need(observed.returncode==0,'Owned exit consumer runtime failed: '+str(folder/'run.log'));value=json.loads(observed.stdout.strip().splitlines()[-1])
    need(value['fixturePass'] and value['failures']==0 and len(value['cases'])==(3 if typed else 2),'Actual exit fragments not fully observed')
    need(all(c['ownedThreadJoined'] and c['ownedHandlesRemaining']==0 and c['ownedEventsRemaining']==0 and not c['contextRetained'] and not c['processExitUsedAsCleanup'] for c in value['cases']),'Owned exit resources not settled')
    gaps=[c for c in value['cases'] if c['productionExitContinuationGapObserved']]
    if typed:need(not gaps and all(c['actualExitGateAndBackgroundOwnerExecuted'] and c['cleanupJoinedBeforeClose']==1 and c['cleanupClosed']==1 for c in value['cases']),'Actual current gate or background owner incomplete')
    else:need(len(gaps)==1 and gaps[0]['held'],'Historical quit-before-settlement failure must still reproduce')
    row.update(executed=True,fixturePass=True,checks=value['checks'],cases=value['cases'],status='PASS' if typed else 'PRODUCTION_QUIT_BEFORE_CAPTURE_SETTLED',pass_=typed)
    row['pass']=row.pop('pass_');print(profile+' '+route+': '+row['status']+'; '+str(value['checks'])+' checks',flush=True);publish()
  run_ui_profiles(ret,source,text,prior,run,snap,compiler,env,result,publish,args)
  need(all((ROOT/n).read_bytes()==b for n,b in source.items()) and HISTORICAL.read_bytes()==historical and PRE_LOOP.read_bytes()==prior,'Inputs changed during replay')
  result['sourceStable']=True
  if args.prepare_only:result['status']='PREPARED_NOT_EXECUTED';publish();return 0
  helper_gap=any(not row['pass'] for row in result['helperWaitFailureProfiles'])
  result.update(diagnosticEvidenceComplete=True,productionExitContinuationAccepted=not args.ui_only and not helper_gap,pass_=not helper_gap,
   status='PRODUCTION_ENSURE_EXIT_OWNER_WAIT_FAILED_SPIN' if helper_gap else 'PASS')
  result['pass']=result.pop('pass_');result['checks']=sum(p['checks'] for p in result['profiles']+result['uiProfiles'])
  result['productionNormalExitLoopAccepted']=True;result['productionReadyCloseAccepted']=True
  result['actualOwnerProfilesExecuted']=not args.ui_only
  result['productionEnsureExitOwnerWaitFailureAccepted']=not helper_gap
  result['minimalProductionChange']='Add wait==WAIT_FAILED to EnsureExitOwner message-only wait condition, preserving Peek/dispatch and owned cleanup handle'
  result['remaining']=['UNKNOWN: arbitrary blocked child/parent bounded disposal','Actual child lifecycle, Host, physical return and Game Bar remain unaccepted; child leaves here are explicit memory ports']
  publish();print(result['status']+' RESULT='+str(out/'RESULT.json'));return 2 if helper_gap else 0
 except Exception as error:
  result.update(status='FAIL',error=str(error));publish();print(str(error),file=sys.stderr);return 1
if __name__=='__main__':raise SystemExit(main())
