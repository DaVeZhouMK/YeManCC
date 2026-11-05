#!/usr/bin/env python3
"""Actual focus consumers with scalar fake ports and no real input/UI calls."""
from pathlib import Path
import argparse,ast,datetime,hashlib,json,os,re,subprocess,sys,time
sys.dont_write_bytecode=True
REPO=Path(__file__).resolve().parents[1]
OUT=REPO.parents[2]/'Testing/YMCC/Evidence/focus-shortcut-input-evidence'
VC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
FUNCTIONS=[
 'static void focusShortcutInputEvidence(',
 'static bool focusLightNoGameQualified(',
 'static bool focusLightIsolationCurrent(',
 'static bool focusLightIsolationBegin(',
 'static bool focusMainWindowIsForeground(',
 'static void focusReleaseOwnedTopmost(',
 'static void focusClearSession(',
 'static bool focusPrepareSummonSession(',
 'static void focusLightRecheckOnUi(',
 'static bool gamepadUiOutputIsolationEligible(',
 'static bool focusLightMayRefocus(',
 'static void focusLightForegroundChanged(',
 'static bool focusLightReturnTick(',
 'static void focusBeginReturnToPreviousWindow(',
 'static void bringToFront(',
 'static json nativeSummonCandidateSnapshot(',
 'static bool nativeYmccShortcutSummon(',
 'static json nativeAutoRegisterSummonGame(',
 'static const char* nativeGameDiscoveryStateName(',
 'static void nativeValvePublish('
]
MUTATIONS=[
 ('proof-expiry','now - observation->qualificationObservedAt <= 30000','true','expired proof admitted'),
 ('proof-inventory','observation->qualificationInventory == ymcc::gameInventory::invalidation.load(std::memory_order_acquire)','true','inventory invalidation admitted'),
 ('proof-cutoff','observation->qualificationObservedAt >= g_focusLightProofNotBefore.load(std::memory_order_acquire)','true','pre-sleep proof admitted after cutoff'),
 ('ticket-source','ticket->source == g_msiMainSourceRevision.load(std::memory_order_acquire)','true','changed source retained ticket'),
 ('refocus-user-choice','if (g_focusSession.lightForegroundAcquired) return focusMainWindowIsForeground();','if (g_focusSession.lightForegroundAcquired) return true;','acquired focus stole user choice'),
 ('return-identity','created != target.processCreated','false','reused process received return focus'),
 ('light-wrapper','if (!lightNoGame) queueSummonGameRegistration(target, generation);','queueSummonGameRegistration(target, generation);','light summon queued registration'),
 ('late-ready','&& focusLightMayRefocus(GetTickCount64()) &&','&& true &&','late Ready stole user choice'),
 ('stale-registration','if (g_summonRegistrationGeneration.load(std::memory_order_acquire) != summonGeneration ||\n                g_exitRequested.load(std::memory_order_acquire)) return result;',
  'if (g_exitRequested.load(std::memory_order_acquire)) return result;','stale worker wrote whitelist'),
 ('native-ui-eligibility','    if (focusLightIsolationCurrent() && !radialHasInputOwner() && !customSteamLibraryInputSuppressed()) return false;\n','','qualified light requested new physical borrowing'),
 ('old-return-timer-new-session','if (!g_focusSession.active || !g_focusSession.returning) return 0;','','old return timer cancelled new non-returning light session'),
 ('old-return-timer-new-session','if (g_focusSession.lightNoGame && (!g_focusSession.active || g_focusSession.returning ||\n                g_focusSession.lightForegroundAcquired)) {\n                KillTimer(g_hwnd, SUMMON_FOCUS_TIMER_ID);\n                return 0;\n            }','','old summon timer cancelled current return'),
 ('late-ready','if (g_focusSession.lightNoGame) g_deferFirstShow = false;','if (g_focusSession.lightNoGame && !g_focusSession.lightForegroundAcquired) g_deferFirstShow = false;','acquired cancellation retained deferred first show'),
 ('valve-publish-order','if (observed && target.generation < observed->generation) return;','(void)observed;','late no-game publication replaced newer positive observation')
]
def sha(data):return hashlib.sha256(data).hexdigest()
def write(path,data):
 with path.open('xb') as stream:stream.write(data if isinstance(data,bytes) else data.encode('utf-8'))
def save(path,value):write(path,json.dumps(value,ensure_ascii=False,indent=2)+'\n')
def replace_once(text,before,after):
 if text.count(before)!=1:raise AssertionError('source anchor is not unique: '+before)
 return text.replace(before,after,1)
def arguments():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('-EvidenceDir','--evidence-dir',dest='EvidenceDir',type=Path,default=OUT,help='Evidence parent; each invocation creates an exclusive run directory.')
 parser.add_argument('-BaselineSource','--baseline-source',dest='BaselineSource',type=Path,help='Explicit before/main.cpp; omitted means baseline comparison is NOT_RUN.')
 parser.add_argument('-CaseFilter','--case-filter',dest='CaseFilter',help='One existing C++ light scenario; default runs the complete original consumer fixture.')
 parser.add_argument('-BudgetSeconds','--budget-seconds',dest='BudgetSeconds',type=float,default=270,help='Shared invocation limit, at most the original 270 seconds.')
 args=parser.parse_args()
 if not 0<args.BudgetSeconds<=270:parser.error('BudgetSeconds must be greater than 0 and at most 270')
 return args
def main():
 args=arguments();started=time.perf_counter();deadline=started+args.BudgetSeconds
 output=args.EvidenceDir.resolve();output.mkdir(parents=True,exist_ok=True)
 run=output/('run-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')+'-'+str(os.getpid()));run.mkdir()
 result={'pass':False,'run':str(run),'profiles':[],'negativeControls':[],'realInputOrUiExecuted':False,
         'baselineComparison':'NOT_RUN','caseFilter':args.CaseFilter,'fullFixtureAcceptance':'NOT_RUN','compileLimitSeconds':120,'executionLimitSeconds':15,'sharedLimitSeconds':args.BudgetSeconds}
 def process(label,command,limit,env):
  remaining=deadline-time.perf_counter()
  if remaining<=0 or limit<=0:raise TimeoutError('shared fixture or step budget exhausted')
  began=time.perf_counter();effective=min(limit,remaining);receipt={'command':command,'cwd':str(run),'timeoutSeconds':effective,'exitCode':None}
  try:
   child=subprocess.run(command,cwd=run,env=env,capture_output=True,encoding='utf-8',errors='replace',timeout=effective)
   receipt['exitCode']=child.returncode
   stdout,stderr=child.stdout,child.stderr
  except subprocess.TimeoutExpired as error:
   receipt['timedOut']=True
   stdout=error.stdout or '';stderr=error.stderr or ''
   if isinstance(stdout,bytes):stdout=stdout.decode('utf-8','replace')
   if isinstance(stderr,bytes):stderr=stderr.decode('utf-8','replace')
   raise
  except OSError as error:
   receipt['spawnError']=str(error)
   raise
  finally:
   receipt['elapsedSeconds']=time.perf_counter()-began
   save(run/(label+'-process.json'),receipt)
   if 'stdout' in locals():write(run/(label+'-stdout.txt'),stdout);write(run/(label+'-stderr.txt'),stderr)
  return child
 try:
  paths=['native/input_idle_diagnostics.h','tools/focus_shortcut_input_evidence_selftest.cpp',
         'tools/focus_shortcut_input_evidence_selftest.py','tools/virtual_sample_tree_perf_selftest.py','deps/json/json.hpp','native/msi_native_foreground_isolation_runtime.h']
  inputs={path:(REPO/path).read_bytes() for path in paths}
  case_table=re.search(r'>> cases=\{([\s\S]*?)\n  \};',inputs[paths[1]].decode('utf-8-sig'))
  if case_table is None:raise AssertionError('canonical C++ case table missing')
  expected_cases=re.findall(r'\{"([^"]+)",\w+\}',case_table.group(1))
  if not expected_cases or len(set(expected_cases))!=len(expected_cases):raise AssertionError('empty or duplicate canonical cases')
  if args.CaseFilter is not None:
   if args.CaseFilter not in expected_cases:raise AssertionError('unknown case filter: '+args.CaseFilter)
   expected_cases=[args.CaseFilter]
  module=ast.parse(inputs[paths[3]].decode('utf-8-sig'));ctx={'re':re}
  exec(compile(ast.Module(body=[node for node in module.body if isinstance(node,ast.FunctionDef) and node.name in ['clean','match_brace','source_function']],type_ignores=[]),'<parser>','exec'),ctx)
  raw=(REPO/'native/main.cpp').read_bytes();source=raw.decode('utf-8-sig').replace('\r\n','\n')
  blocks={signature:ctx['source_function'](source,signature) for signature in FUNCTIONS}
  blocks[paths[-1]+':msiNativeForegroundUiEligible']=ctx['source_function'](inputs[paths[-1]].decode('utf-8-sig').replace('\r\n','\n'),'static bool msiNativeForegroundUiEligible(')
  result['mainSourceSha256']=sha(raw);write(run/'native-main.cpp',raw)
  def braced(text,anchor):
   masked=ctx['clean'](text)
   if masked.count(anchor)!=1:raise AssertionError('block anchor is not unique: '+anchor)
   begin=masked.index(anchor);opening=masked.index('{',begin)
   return text[begin:ctx['match_brace'](text,opening)+1]
  types=[braced(source,'struct '+name+' {')+';' for name in ['NativeDetectedGame','FocusTargetSnapshot','FocusSessionState','FocusLightIsolationTicket','NativeGameValveSnapshot']]
  types.append(braced(source,'enum class NativeGameDiscoveryState {')+';')
  types.insert(0,types.pop())
  types_text='\n\n'.join(types)+'\n';write(run/'actual_focus_types.inc',types_text);result['focusTypesSha256']=sha(types_text.encode())
  wndproc=ctx['source_function'](source,'static LRESULT CALLBACK WndProc(')
  ready=ctx['source_function'](source,'static void finalizeWebViewRenderReady(')
  timer_blocks=[braced(wndproc,'if (w == '+name+') {') for name in ['SUMMON_FOCUS_TIMER_ID','RETURN_GAME_FOCUS_TIMER_ID']]
  ready_block=braced(ready,'if (focusSequence == g_focusSessionSequence && g_focusSession.active && !g_focusSession.returning && focusLightMayRefocus(GetTickCount64()) &&')
  ready_show=braced(ready,'if (g_deferFirstShow) {')
  generated='\n\n'.join(blocks.values())
  generated=replace_once(generated,'static void focusBeginReturnToPreviousWindow(','static void actualFocusBeginReturnToPreviousWindow(')
  generated=replace_once(generated,'static void bringToFront(','static void actualBringToFront(')
  generated=replace_once(generated,'static bool gamepadUiOutputIsolationEligible(','static bool actualGamepadUiOutputIsolationEligible(')
  generated+='\nstatic LRESULT actualFocusTimer(WPARAM w) {\n'+'\n'.join(timer_blocks)+'\nreturn 0;\n}\n'
  generated+='\nstatic void actualLateReadyFocus(unsigned long long focusSequence = g_focusSessionSequence) {\n'+ready_show+'\n'+ready_block+'\n}\n'
  for case,before,after,expected in MUTATIONS:replace_once(generated,before,after)
  write(run/'actual_focus.inc',generated)
  result['functionHashes']={signature:sha(block.encode()) for signature,block in blocks.items()}
  result['consumerBlockHashes']={name:sha(block.encode()) for name,block in zip(['summon-timer','return-timer','render-ready-focus','render-ready-show'],timer_blocks+[ready_block,ready_show])}
  result['generatedSha256']=sha(generated.encode())
  baseline=None
  if args.BaselineSource is not None:
   baseline=args.BaselineSource.resolve();baseline_raw=baseline.read_bytes()
   old=ctx['source_function'](baseline_raw.decode('utf-8-sig').replace('\r\n','\n'),'static bool nativeYmccShortcutSummon(')
   write(run/'baseline-main.cpp',baseline_raw)
   write(run/'baseline_focus.inc',replace_once(old,'nativeYmccShortcutSummon(','baselineNativeYmccShortcutSummon(')+'\n')
   result.update(baselineSource=str(baseline),baselineSha256=sha(baseline_raw),baselineComparison='NOT_RUN_SELECTED_SCOPE' if args.CaseFilter else 'SELECTED')
  for path,data in inputs.items():
   write(run/('fixture.cpp' if path.endswith('_selftest.cpp') else Path(path).name),data)
  result['sourceHashes']={path:sha(data) for path,data in inputs.items()}
  version_file=VC.parent/'Microsoft.VCToolsVersion.default.txt';version=version_file.read_text(encoding='utf-8-sig').strip()
  toolroot=VC.parents[2]/'Tools/MSVC'/version/'bin/Hostx64/x64'
  sdkroot=Path('C:/Program Files (x86)/Windows Kits/10')
  sdk=max((entry.name for entry in (sdkroot/'Include').iterdir() if entry.is_dir()),key=lambda name:tuple(map(int,name.split('.'))))
  tools=[VC,version_file,toolroot/'cl.exe',toolroot/'link.exe',toolroot/'dumpbin.exe',toolroot/'c1xx.dll',toolroot/'c2.dll']
  result['tools']={str(path):sha(path.read_bytes()) for path in tools};result['msvcVersion']=version;result['windowsSdkVersion']=sdk
  env={key:value for key,value in os.environ.items() if key.upper() in {'SYSTEMROOT','WINDIR','SYSTEMDRIVE','COMSPEC','TEMP','TMP','PROGRAMFILES','PROGRAMFILES(X86)','PROGRAMW6432','PROCESSOR_ARCHITECTURE','NUMBER_OF_PROCESSORS'}}
  env['PATH']=str(toolroot)+os.pathsep+str(Path(os.environ['SystemRoot'])/'System32')+os.pathsep+os.environ['SystemRoot']
  result['environment']={key:value for key,value in env.items() if key.upper() not in {'TEMP','TMP'}}
  def build(name,flags,include):
   build_started=time.perf_counter()
   exe=run/(name+'.exe')
   if max(len(str(run/(name+suffix))) for suffix in ['.obj','.exe','.cmd'])>=250:raise AssertionError('native output path exceeds supported MAX_PATH recipe')
   command=[str(toolroot/'cl.exe'),'/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX',*flags,
            '/DFOCUS_BASELINE_AVAILABLE='+str(int(baseline is not None)),'/DFOCUS_ACTUAL_INCLUDE="'+include+'"',str(run/'fixture.cpp'),'/I'+str(run),
            '/Fo'+str(run/(name+'.obj')),'/Fe'+str(exe),'/link','/INCREMENTAL:NO','/NODEFAULTLIB:user32.lib']
   bat=run/(name+'.cmd')
   write(bat,'@echo off\ncall "'+str(VC)+'" '+sdk+' -vcvars_ver='+version+' >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(command)+'\nexit /b %errorlevel%\n')
   child=process(name+'-compile',[os.environ.get('COMSPEC','cmd.exe'),'/d','/c',str(bat)],120,env)
   if child.returncode:raise AssertionError('compile failed: '+name)
   imported=process(name+'-imports',[str(toolroot/'dumpbin.exe'),'/imports',str(exe)],120-(time.perf_counter()-build_started),env)
   if imported.returncode:raise AssertionError('import audit failed: '+name)
   lowered=imported.stdout.lower();banned=[name for name in ['user32.dll','win32u.dll','getasynckeystate','getkeyboardstate','setforegroundwindow','sendinput','ole32.dll','shell32.dll'] if name in lowered]
   save(run/(name+'-import-audit.json'),{'pass':not banned,'forbiddenMatches':banned})
   if banned:raise AssertionError('real input/UI import')
   return exe
  for name,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/MTd'])]:
   exe=build(name,flags,'actual_focus.inc');command=[str(exe)]+(['--case',args.CaseFilter] if args.CaseFilter else []);child=process(name+'-run',command,15,env)
   row=json.loads(child.stdout)
   result['profiles'].append({'profile':name,**row})
   if child.returncode:raise AssertionError('fixture failed: '+name+': '+child.stderr)
   if row.get('pass') is not True:raise AssertionError('fixture did not report pass')
   if row.get('controlScenarios')!=(0 if args.CaseFilter else 24) or row.get('selectedCase')!=(args.CaseFilter or ''):raise AssertionError('normal selection mismatch')
   actual_cases=row.get('lightScenarios',[])
   if len(actual_cases)!=len(expected_cases) or set(actual_cases)!=set(expected_cases):raise AssertionError('normal case set mismatch')
   if row['baselineComparisons']!=(24 if baseline is not None and not args.CaseFilter else 0):raise AssertionError('baseline selection mismatch')
   print(name+': '+str(row['checks'])+' checks, '+str(row['controlScenarios'])+' legacy scenarios, '+str(len(row['lightScenarios']))+' light scenarios',flush=True)
  for index,(case,before,after,expected) in enumerate(MUTATIONS,1):
   if args.CaseFilter is not None and args.CaseFilter!=case:continue
   include='mutation'+str(index)+'.inc';mutated=replace_once(generated,before,after);write(run/include,mutated)
   exe=build('neg'+str(index),['/O2','/DNDEBUG','/MT'],include)
   child=process('neg'+str(index)+'-run',[str(exe),'--case',case],15,env)
   row=json.loads(child.stdout)
   accepted=child.returncode==1 and child.stderr.strip()==expected and row.get('pass') is False and row.get('selectedCase')==case and row.get('checks',0)>0 and row.get('controlScenarios')==0 and row.get('baselineComparisons')==0 and row.get('lightScenarios')==[]
   receipt={'mutationIndex':index,'case':case,'expectedFailure':expected,'exitCode':child.returncode,'semanticFailure':accepted,'sourceSha256':sha(mutated.encode())}
   result['negativeControls'].append(receipt)
   if not accepted:raise AssertionError('negative did not fail its semantic assertion: '+case)
   print('Negative '+case+': semantic rejection',flush=True)
  if (REPO/'native/main.cpp').read_bytes()!=raw:raise AssertionError('main source drift; frozen result retained')
  if any((REPO/path).read_bytes()!=data for path,data in inputs.items()):raise AssertionError('input drift')
  if baseline is not None and baseline.read_bytes()!=baseline_raw:raise AssertionError('baseline drift')
  if any(sha(path.read_bytes())!=result['tools'][str(path)] for path in tools):raise AssertionError('tool drift')
  if baseline is not None and not args.CaseFilter:result['baselineComparison']='PASS'
  result['pass']=True
 except Exception as error:result['error']=str(error)
 result['elapsedSeconds']=time.perf_counter()-started
 save(run/'result.json',result)
 print('Evidence: '+str(run),flush=True)
 if not result['pass']:print(result.get('error','fixture failed'),file=sys.stderr,flush=True)
 return 0 if result['pass'] else 1
if __name__=='__main__':raise SystemExit(main())
