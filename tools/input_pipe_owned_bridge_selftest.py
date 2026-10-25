#!/usr/bin/env python3
"""R42 isolated owned-I/O/retirement bridge; no production or device execution.
Legacy functions are extracted verbatim and executed only using deterministic stubs.
"""
from pathlib import Path
import datetime,hashlib,json,os,subprocess,sys
sys.dont_write_bytecode=True
from virtual_sample_tree_perf_selftest import source_function
REPO=Path(__file__).resolve().parents[1]
OUT=REPO.parents[2]/'Build/Validation/VirtualInputPerf-R42-20261015'
VC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
def sha(b):return hashlib.sha256(b).hexdigest()
def save(p,j):p.write_text(json.dumps(j,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def main():
 run=OUT/('owned-bridge-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
 raw=(REPO/'native/main.cpp').read_bytes();src=raw.decode('utf-8-sig')
 paths=['tools/input_pipe_owned_operation_model.h','tools/input_pipe_owned_win32_backend.h','tools/input_pipe_owned_bridge.h','tools/input_pipe_owned_bridge_selftest.cpp','deps/json/json.hpp','InputHost/Program.cs']
 sources={p:(REPO/p).read_bytes() for p in paths}
 for source,target in [('tools/input_pipe_owned_operation_model.h','input_pipe_owned_operation_model.h'),('tools/input_pipe_owned_win32_backend.h','input_pipe_owned_win32_backend.h'),('tools/input_pipe_owned_bridge.h','input_pipe_owned_bridge.h'),('tools/input_pipe_owned_bridge_selftest.cpp','fixture.cpp'),('deps/json/json.hpp','json.hpp')]:
  (run/target).write_bytes(sources[source])
 (run/'runner.snapshot.py').write_bytes(Path(__file__).read_bytes())
 helpers={sig:source_function(src,sig) for sig in ['static bool inputHostPipeIo(','static bool inputHostWaitClientLocked(']}
 (run/'legacy_helpers.inc').write_bytes(('\n\n'.join(helpers.values())+'\n').encode('utf-8'))
 save(run/'production-scope-bindings.json',{'mainSha256':sha(raw),'HostSha256':sha(sources['InputHost/Program.cs']),'helperHashes':{sig:sha(s.encode()) for sig,s in helpers.items()},'productionEdited':False,'legacyExecutedOnlyWithDeterministicStubs':True})
 profiles=[]
 for name,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
  args=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4',*flags,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(name+'.obj')),'/Fe'+str(run/(name+'.exe'))]
  batch=run/(name+'-build.bat');batch.write_text('@echo off\nset VSLANG=1033\ncall "'+str(VC)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(args)+'\nexit /b %errorlevel%\n',encoding='utf-8')
  p=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=180);(run/(name+'-build.log')).write_text(p.stdout+p.stderr,encoding='utf-8')
  if p.returncode:raise RuntimeError(name+' compile failed: '+str(run))
  print(name+': deterministic legacy semantics + private-pipe bridge starting',flush=True)
  p=subprocess.run([str(run/(name+'.exe'))],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=45)
  (run/(name+'-result.json')).write_text(p.stdout,encoding='utf-8');(run/(name+'-stderr.log')).write_text(p.stderr,encoding='utf-8')
  if p.returncode:raise RuntimeError(name+' verification failed: '+p.stderr+' '+str(run))
  result=json.loads(p.stdout);assert result['pass'];profiles.append({'profile':name,**result});print(name+': '+str(result['checks'])+' checks; '+str(len(result['cases']))+' cases; '+str(result['privatePipePairs'])+' private pairs',flush=True)
 now=(REPO/'native/main.cpp').read_bytes();current=now.decode('utf-8-sig');assert all(source_function(current,sig)==body for sig,body in helpers.items()),'production helper changed'
 assert all((REPO/path).read_bytes()==b for path,b in sources.items()),'fixture/source dependency changed'
 binding={'pass':True,'mainSha256AtStart':sha(raw),'currentMainSha256':sha(now),'wholeNativeStable':raw==now,'productionHelpersStable':True,'inputs':{p:sha(b) for p,b in sources.items()},'runnerSha256':sha(Path(__file__).read_bytes()),'privatePipePrefix':'YMCC-R42-bridge-','remoteClientsRejected':True,'connectedPeerPidsVerifiedCurrentProcess':True,'realInputOrProductPipesUsed':False,'productionIntegrated':False,'realRetirementCasesUseDeliberatelySuppressedCancel':True,'noKernelCancellationFaultOrCPUOrCtrlCauseClaim':True,'artifacts':{p.name:sha(p.read_bytes()) for p in run.iterdir() if p.is_file()}}
 save(run/'source-binding.json',binding);save(run/'result.json',{'pass':True,'profiles':profiles,'binding':binding})
 save(OUT/'latest-bridge.json',{'pass':True,'run':str(run),'profiles':[{'profile':p['profile'],'checks':p['checks'],'cases':len(p['cases']),'privatePipePairs':p['privatePipePairs'],'adapterHandlesCreated':p['adapterHandlesCreated'],'adapterHandlesClosed':p['adapterHandlesClosed']} for p in profiles],'productionEdited':False,'nativeMainSha256':sha(now)})
 print(json.dumps({'pass':True,'run':str(run),'productionEdited':False,'realProductsUsed':False}))
if __name__=='__main__':main()
