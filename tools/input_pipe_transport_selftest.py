#!/usr/bin/env python3
"""R44 production Runtime, exact wrappers/factory, isolated private pipes/controls."""
from pathlib import Path
import datetime,hashlib,json,os,subprocess,sys
sys.dont_write_bytecode=True
from virtual_sample_tree_perf_selftest import source_function
REPO=Path(__file__).resolve().parents[1]
OUT=REPO.parents[2]/'Build/Validation/VirtualInputPerf-R44-20261015'
VC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
def sha(b):return hashlib.sha256(b).hexdigest()
def save(p,j):p.write_text(json.dumps(j,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def main():
 run=OUT/('production-owner-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
 paths=['native/input_host_pipe_transport.h','tools/input_pipe_test_allocation_fault.h','tools/input_pipe_transport_control_selftest.cpp','tools/input_pipe_transport_real_selftest.cpp','tools/input_pipe_transport_extracted_selftest.cpp','deps/json/json.hpp','InputHost/Program.cs']
 inputs={p:(REPO/p).read_bytes() for p in paths};raw=(REPO/'native/main.cpp').read_bytes();src=raw.decode('utf-8-sig')
 signatures=['static void inputHostPipeCloseOwned(','static HANDLE inputHostCreateServerPipeLocked(','static bool inputHostPipeIo(','static bool inputHostWaitClientLocked(']
 helpers={s:source_function(src,s) for s in signatures}
 request=source_function(src,'static bool inputHostRequestPreparedLocked(');start=request.index('    DWORD lastError = ERROR_SUCCESS;');end=request.index('    std::vector<char> responseBuffer',start);factory_fragment=request[start:end]
 (run/'factory-error-fragment.inc').write_bytes(factory_fragment.encode('utf-8'))
 (run/'helpers.inc').write_bytes(('\n\n'.join(helpers.values())+'\nstatic HANDLE g_inputHostServerPipe=nullptr;\nstatic DWORD extractedFactoryFailureError(){\n'+factory_fragment+'\nreturn lastError;\n}\n').encode('utf-8'))
 for p,b in inputs.items():
  if p!='InputHost/Program.cs':(run/Path(p).name).write_bytes(b)
 (run/'runner.snapshot.py').write_bytes(Path(__file__).read_bytes());results=[]
 for profile,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
  for scope,fixture in [('control','input_pipe_transport_control_selftest.cpp'),('real','input_pipe_transport_real_selftest.cpp'),('extracted','input_pipe_transport_extracted_selftest.cpp')]:
   if '--only-real' in sys.argv and scope!='real':continue
   target=run/(profile+'-'+scope);target.mkdir()
   for p in ['input_host_pipe_transport.h','input_pipe_test_allocation_fault.h','json.hpp','helpers.inc']:(target/p).write_bytes((run/p).read_bytes())
   (target/'fixture.cpp').write_bytes((run/fixture).read_bytes());args=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4',*flags,str(target/'fixture.cpp'),'/I'+str(target),'/Fo'+str(target/'fixture.obj'),'/Fe'+str(target/'fixture.exe'),'advapi32.lib']
   batch=target/'build.bat';batch.write_text('@echo off\ncall "'+str(VC)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(args)+'\nexit /b %errorlevel%\n',encoding='utf-8')
   p=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=target,capture_output=True,encoding='utf-8',errors='replace',timeout=180);(target/'build.log').write_text(p.stdout+p.stderr,encoding='utf-8')
   if p.returncode:raise RuntimeError(profile+' '+scope+' build failed: '+str(target))
   print(profile+' '+scope+': starting',flush=True);p=subprocess.run([str(target/'fixture.exe')],cwd=target,capture_output=True,encoding='utf-8',errors='replace',timeout=35);(target/'result.json').write_text(p.stdout,encoding='utf-8');(target/'stderr.log').write_text(p.stderr,encoding='utf-8')
   if p.returncode:raise RuntimeError(profile+' '+scope+' verify failed: '+p.stderr+' '+str(target))
   result=json.loads(p.stdout);assert result['pass'];results.append({'profile':profile,'scope':scope,**result});print(profile+' '+scope+': '+str(result['checks'])+' checks',flush=True)
 now=(REPO/'native/main.cpp').read_bytes();assert all(source_function(now.decode('utf-8-sig'),s)==b for s,b in helpers.items()),'wrapper drift'
 assert all((REPO/p).read_bytes()==b for p,b in inputs.items()),'input drift'
 binding={'pass':True,'nativeSha256AtStart':sha(raw),'nativeSha256AtEnd':sha(now),'wholeNativeStable':raw==now,'wrapperHashes':{s:sha(b.encode()) for s,b in helpers.items()},'factoryErrorFragmentSha256':sha(factory_fragment.encode()),'inputs':{p:sha(b) for p,b in inputs.items()},'runnerSha256':sha(Path(__file__).read_bytes()),'privatePipesOnly':True,'productsOrInputDevicesUsed':False,'productionIntegrated':True,'noCPUOrCtrlCauseClaim':True,'artifacts':{str(p.relative_to(run)):sha(p.read_bytes()) for p in run.rglob('*') if p.is_file()}}
 save(run/'source-binding.json',binding);save(run/'result.json',{'pass':True,'results':results,'binding':binding});save(OUT/('latest-real-only.json' if '--only-real' in sys.argv else 'latest-owner.json'),{'pass':True,'run':str(run),'summary':[{'profile':r['profile'],'scope':r['scope'],'checks':r['checks'],'cases':len(r.get('cases',[])),'privatePipePairs':r.get('privatePipePairs')} for r in results],'productionIntegrated':True,'headerSha256':sha(inputs['native/input_host_pipe_transport.h']),'noCPUOrCtrlCauseClaim':True})
 print(json.dumps({'pass':True,'run':str(run),'productionIntegrated':True,'productOrDevicesRun':False}))
if __name__=='__main__':main()
