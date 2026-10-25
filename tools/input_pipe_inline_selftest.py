#!/usr/bin/env python3
"""R43 adapter-boundary A5 controls, real private traces, R42 regression.
No product/device execution. Baseline defects are expected observations, not passes.
"""
from pathlib import Path
import datetime,hashlib,json,os,subprocess,sys
sys.dont_write_bytecode=True
from virtual_sample_tree_perf_selftest import source_function
REPO=Path(__file__).resolve().parents[1]
OUT=REPO.parents[2]/'Build/Validation/VirtualInputPerf-R43-20261015'
VC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
def sha(b):return hashlib.sha256(b).hexdigest()
def save(p,j):p.write_text(json.dumps(j,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def main():
 run=OUT/('inline-proof-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
 paths=['tools/input_pipe_owned_operation_model.h','tools/input_pipe_owned_win32_backend.h','tools/input_pipe_owned_bridge.h','tools/input_pipe_inline_control_selftest.cpp','tools/input_pipe_inline_real_selftest.cpp','tools/input_pipe_owned_bridge_selftest.cpp','deps/json/json.hpp','InputHost/Program.cs']
 inputs={p:(REPO/p).read_bytes() for p in paths};raw=(REPO/'native/main.cpp').read_bytes();src=raw.decode('utf-8-sig');helpers={s:source_function(src,s) for s in ['static bool inputHostPipeIo(','static bool inputHostWaitClientLocked(']}
 baseline=(OUT/'baseline/input_pipe_owned_win32_backend.h').read_bytes();assert sha(baseline)=='6803c3e7f1f21c06f98f6f2ac5273c4016623d2c947d45d5de531bef1cf91d2c'
 for p,b in inputs.items():
  if p!='InputHost/Program.cs':(run/Path(p).name).write_bytes(b)
 (run/'baseline-backend.h').write_bytes(baseline);(run/'runner.snapshot.py').write_bytes(Path(__file__).read_bytes());(run/'legacy_helpers.inc').write_bytes(('\n\n'.join(helpers.values())+'\n').encode('utf-8'))
 results=[]
 for profile,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
  for scope,fixture,defines in [('baseline-controls','input_pipe_inline_control_selftest.cpp',[]),('candidate-controls','input_pipe_inline_control_selftest.cpp',['/DR43_CANDIDATE']),('candidate-real','input_pipe_inline_real_selftest.cpp',['/DR43_CANDIDATE']),('bridge-regression','input_pipe_owned_bridge_selftest.cpp',['/DR43_CANDIDATE'])]:
   if '--only-real' in sys.argv and scope!='candidate-real':continue
   target=run/(profile+'-'+scope);target.mkdir()
   for p in ['input_pipe_owned_operation_model.h','input_pipe_owned_bridge.h','json.hpp','legacy_helpers.inc']:(target/p).write_bytes((run/p).read_bytes())
   (target/'input_pipe_owned_win32_backend.h').write_bytes(baseline if scope=='baseline-controls' else inputs['tools/input_pipe_owned_win32_backend.h']);(target/'fixture.cpp').write_bytes((run/fixture).read_bytes())
   args=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4',*flags,*defines,str(target/'fixture.cpp'),'/I'+str(target),'/Fo'+str(target/'fixture.obj'),'/Fe'+str(target/'fixture.exe')]
   batch=target/'build.bat';batch.write_text('@echo off\nset VSLANG=1033\ncall "'+str(VC)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(args)+'\nexit /b %errorlevel%\n',encoding='utf-8')
   p=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=target,capture_output=True,encoding='utf-8',errors='replace',timeout=180);(target/'build.log').write_text(p.stdout+p.stderr,encoding='utf-8')
   if p.returncode:raise RuntimeError(profile+' '+scope+' build failed: '+str(target))
   print(profile+' '+scope+': starting',flush=True)
   p=subprocess.run([str(target/'fixture.exe')],cwd=target,capture_output=True,encoding='utf-8',errors='replace',timeout=45);(target/'result.json').write_text(p.stdout,encoding='utf-8');(target/'stderr.log').write_text(p.stderr,encoding='utf-8')
   if p.returncode:raise RuntimeError(profile+' '+scope+' verification failed: '+p.stderr+' '+str(target))
   result=json.loads(p.stdout);assert result['pass'];results.append({'profile':profile,'scope':scope,**result});print(profile+' '+scope+': '+str(len(result['cases']))+' cases / '+str(result['checks'])+' checks',flush=True)
 now=(REPO/'native/main.cpp').read_bytes();assert all(source_function(now.decode('utf-8-sig'),s)==body for s,body in helpers.items()),'production helper drift'
 assert all((REPO/p).read_bytes()==b for p,b in inputs.items()),'input drift'
 binding={'pass':True,'nativeSha256AtStart':sha(raw),'nativeSha256AtEnd':sha(now),'wholeNativeStable':raw==now,'helperHashes':{s:sha(b.encode()) for s,b in helpers.items()},'inputs':{p:sha(b) for p,b in inputs.items()},'runnerSha256':sha(Path(__file__).read_bytes()),'baselineBackendSha256':sha(baseline),'productionEdited':False,'A5BoundaryInjectionSyntheticNotRealKernelFault':True,'realDisconnectCasesDeliberatelySuppressCancel':True,'privatePipePrefix':'YMCC-R43-inline-','productsOrInputDevicesUsed':False,'artifacts':{str(p.relative_to(run)):sha(p.read_bytes()) for p in run.rglob('*') if p.is_file()}}
 save(run/'source-binding.json',binding);save(run/'result.json',{'pass':True,'results':results,'binding':binding})
 save(OUT/('latest-real.json' if '--only-real' in sys.argv else 'latest-inline.json'),{'pass':True,'run':str(run),'summary':[{'profile':r['profile'],'scope':r['scope'],'cases':len(r['cases']),'checks':r['checks'],'A5Controls':r.get('A5Controls'),'privatePipePairs':r.get('privatePipePairs')} for r in results],'productionIntegrated':False,'nativeSha256':sha(now),'candidateBackendSha256':sha(inputs['tools/input_pipe_owned_win32_backend.h'])})
 print(json.dumps({'pass':True,'run':str(run),'productionIntegrated':False,'noCPUOrCtrlCauseClaim':True}))
if __name__=='__main__':main()
