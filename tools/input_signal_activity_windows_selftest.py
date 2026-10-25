#!/usr/bin/env python3
"""Current-source diagnostic/hook tests; only private synthetic executables."""
from pathlib import Path
import datetime,hashlib,json,os,subprocess,sys
sys.dont_write_bytecode=True
from virtual_sample_tree_perf_selftest import source_function
REPO=Path(__file__).resolve().parents[1]
OUT=REPO.parents[2]/'Build/Validation/CtrlAlt-Specialist-20261015/activity-integration'
VC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
def sha(b):return hashlib.sha256(b).hexdigest()
def save(p,j):p.write_text(json.dumps(j,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def main():
 run=OUT/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
 raw=(REPO/'native/main.cpp').read_bytes();text=raw.decode('utf-8-sig')
 names=['static void inputSignalUpdateGate(','static void inputSignalObserveMouse(','static void keyboardModifierObserveLowLevel(','static void keyboardModifierObserveRaw(','static void keyboardModifierObserveSelfSend(','static void inputSignalObserveScreenTouchpad(','static void inputSignalFlushEvidence(','static LRESULT CALLBACK oemBackMouseHookProc(']
 blocks={name:source_function(text,name) for name in names}
 (run/'actual_activity.inc').write_bytes(('\n\n'.join(blocks.values())+'\n').encode('utf-8'))
 before=(OUT/'oemBackMouseHookProc.before.cpp').read_bytes();assert before.count(b'oemBackMouseHookProc(')==1
 (run/'before_mouse_hook.inc').write_bytes(before.replace(b'oemBackMouseHookProc(',b'beforeOemBackMouseHookProc(',1))
 sources={p:(REPO/p).read_bytes() for p in ['tools/input_signal_activity_windows_selftest.cpp','native/keyboard_modifier_event_evidence.h','native/keyboard_modifier_retention.h','native/input_signal_activity_evidence.h','native/input_signal_activity_windows.h','native/input_signal_key_state_evidence.h','deps/json/json.hpp']}
 for p,b in sources.items(): (run/('fixture.cpp' if p.endswith('_selftest.cpp') else Path(p).name)).write_bytes(b)
 (run/'runner.snapshot.py').write_bytes(Path(__file__).read_bytes())
 save(run/'source-binding.initial.json',{'mainSha256':sha(raw),'functions':{k:sha(v.encode('utf-8')) for k,v in blocks.items()},'beforeHookSha256':sha(before),'inputs':{p:sha(b) for p,b in sources.items()},'realInputUsed':False})
 profiles=[]
 for name,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
  args=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4',*flags,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(name+'.obj')),'/Fe'+str(run/(name+'.exe'))]
  batch=run/(name+'-build.bat');batch.write_text('@echo off\nset VSLANG=1033\ncall "'+str(VC)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(args)+'\nif errorlevel 1 exit /b %errorlevel%\ndumpbin /imports "'+str(run/(name+'.exe'))+'" > "'+str(run/(name+'-imports.txt'))+'"\nexit /b %errorlevel%\n',encoding='utf-8')
  p=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=180);(run/(name+'-build.log')).write_text(p.stdout+p.stderr,encoding='utf-8')
  if p.returncode:raise RuntimeError(name+' compile failed: '+str(run))
  imports=(run/(name+'-imports.txt')).read_text(encoding='utf-8',errors='replace').lower()
  assert all(bad not in imports for bad in ['user32.dll','win32u.dll','sendinput','getasynckeystate','getkeyboardstate','getforegroundwindow','getwindowthreadprocessid','setwindowshookex']), 'real input/observer import present'
  p=subprocess.run([str(run/(name+'.exe'))],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=30)
  (run/(name+'-result.json')).write_text(p.stdout,encoding='utf-8');(run/(name+'-stderr.log')).write_text(p.stderr,encoding='utf-8')
  if p.returncode:raise RuntimeError(name+' failed: '+p.stderr+' '+str(run))
  result=json.loads(p.stdout);assert result['pass'];profiles.append({'profile':name,**result});print(name+': '+str(result['checks'])+' checks; no real input imports',flush=True)
 now=(REPO/'native/main.cpp').read_bytes();assert all(source_function(now.decode('utf-8-sig'),k)==v for k,v in blocks.items()), 'tested functions drifted'
 assert all((REPO/p).read_bytes()==b for p,b in sources.items()),'dependency drift'
 binding={'mainSha256Before':sha(raw),'mainSha256After':sha(now),'wholeMainStable':raw==now,'testedFunctionsStable':True,'sources':{p:sha(b) for p,b in sources.items()},'runnerSha256':sha(Path(__file__).read_bytes()),'artifacts':{p.name:sha(p.read_bytes()) for p in run.iterdir() if p.is_file()},'realInputImportsAbsent':True}
 save(run/'result.json',{'pass':True,'profiles':profiles,'binding':binding});save(OUT/'latest.json',{'pass':True,'run':str(run),'profiles':[{'profile':p['profile'],'checks':p['checks'],'cases':p['cases']} for p in profiles],'binding':binding})
 print(json.dumps({'pass':True,'run':str(run),'CtrlAltRootCauseProven':False}))
if __name__=='__main__':main()
