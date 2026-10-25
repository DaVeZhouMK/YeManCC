#!/usr/bin/env python3
"""R45 prepare/build/correctness only; benchmark has a separate explicit gate."""
from pathlib import Path
import datetime,hashlib,json,os,subprocess,sys
sys.dont_write_bytecode=True
from virtual_sample_tree_perf_selftest import source_function
REPO=Path(__file__).resolve().parents[1]
OUT=REPO.parents[2]/'Build/Validation/VirtualInputPerf-R45-20261015'
VC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
def sha(b):return hashlib.sha256(b).hexdigest()
def save(p,j):p.write_text(json.dumps(j,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def main():
 run=OUT/('reuse-correctness-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
 inputs={p:(REPO/p).read_bytes() for p in ['native/input_host_pipe_transport.h','tools/input_pipe_test_allocation_fault.h','tools/input_pipe_transport_control_selftest.cpp','tools/input_pipe_transport_real_selftest.cpp','tools/input_pipe_transport_extracted_selftest.cpp','tools/input_pipe_reuse_control_selftest.cpp','deps/json/json.hpp','InputHost/Program.cs']}
 raw=(REPO/'native/main.cpp').read_bytes();src=raw.decode('utf-8-sig');signatures=['static void inputHostPipeCloseOwned(','static HANDLE inputHostCreateServerPipeLocked(','static bool inputHostPipeIo(','static bool inputHostWaitClientLocked('];helpers={s:source_function(src,s) for s in signatures}
 request=source_function(src,'static bool inputHostRequestPreparedLocked(');start=request.index('    DWORD lastError = ERROR_SUCCESS;');end=request.index('    std::vector<char> responseBuffer',start);fragment=request[start:end]
 include=('\n\n'.join(helpers.values())+'\nstatic HANDLE g_inputHostServerPipe=nullptr;\nstatic DWORD extractedFactoryFailureError(){\n'+fragment+'\nreturn lastError;\n}\n').encode()
 base=(OUT/'baseline.h').read_bytes();candidate=(OUT/'candidate.h').read_bytes();assert base==inputs['native/input_host_pipe_transport.h'];plan=(OUT/'PLAN.json').read_bytes()
 (run/'baseline.h').write_bytes(base);(run/'candidate.h').write_bytes(candidate);(run/'PLAN.json').write_bytes(plan);(run/'runner.snapshot.py').write_bytes(Path(__file__).read_bytes());results=[]
 for profile,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
  for variant,header in [('baseline',base),('candidate',candidate)]:
   for scope,fixture in [('r44-control','input_pipe_transport_control_selftest.cpp'),('r44-real','input_pipe_transport_real_selftest.cpp'),('r44-extracted','input_pipe_transport_extracted_selftest.cpp'),('reuse-control','input_pipe_reuse_control_selftest.cpp')]:
    target=run/(profile+'-'+variant+'-'+scope);target.mkdir();(target/'input_host_pipe_transport.h').write_bytes(header);(target/'json.hpp').write_bytes(inputs['deps/json/json.hpp']);(target/'helpers.inc').write_bytes(include);(target/'input_pipe_test_allocation_fault.h').write_bytes(inputs['tools/input_pipe_test_allocation_fault.h']);(target/'fixture.cpp').write_bytes(inputs['tools/'+fixture])
    args=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4',*flags,*(['/DR45_CANDIDATE'] if variant=='candidate' else []),str(target/'fixture.cpp'),'/I'+str(target),'/Fo'+str(target/'fixture.obj'),'/Fe'+str(target/'fixture.exe'),'advapi32.lib']
    batch=target/'build.bat';batch.write_text('@echo off\ncall "'+str(VC)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(args)+'\nexit /b %errorlevel%\n',encoding='utf-8')
    p=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=target,capture_output=True,encoding='utf-8',errors='replace',timeout=180);(target/'build.log').write_text(p.stdout+p.stderr,encoding='utf-8')
    if p.returncode:raise RuntimeError(str(target)+' build failed')
    p=subprocess.run([str(target/'fixture.exe')],cwd=target,capture_output=True,encoding='utf-8',errors='replace',timeout=35);(target/'result.json').write_text(p.stdout,encoding='utf-8');(target/'stderr.log').write_text(p.stderr,encoding='utf-8')
    if p.returncode:raise RuntimeError(str(target)+' verify failed: '+p.stderr)
    result=json.loads(p.stdout);assert result['pass'];results.append({'profile':profile,'variant':variant,'scope':scope,**result});print(profile+' '+variant+' '+scope+': '+str(result['checks'])+' checks',flush=True)
 now=(REPO/'native/main.cpp').read_bytes();assert all(source_function(now.decode('utf-8-sig'),s)==b for s,b in helpers.items()),'wrapper drift';assert all((REPO/p).read_bytes()==b for p,b in inputs.items()),'input drift';assert (OUT/'PLAN.json').read_bytes()==plan and (OUT/'candidate.h').read_bytes()==candidate
 binding={'pass':True,'productionCandidateApplied':False,'mainSha256AtStart':sha(raw),'mainSha256AtEnd':sha(now),'currentInputs':{p:sha(b) for p,b in inputs.items()},'baselineSha256':sha(base),'candidateSha256':sha(candidate),'planSha256':sha(plan),'runnerSha256':sha(Path(__file__).read_bytes()),'wrappers':{s:sha(b.encode()) for s,b in helpers.items()},'factoryErrorFragmentSha256':sha(fragment.encode()),'artifacts':{str(p.relative_to(run)):sha(p.read_bytes()) for p in run.rglob('*') if p.is_file()},'noProductsDevicesOrBenchmarksExecuted':True}
 save(run/'source-binding.json',binding);save(run/'result.json',{'pass':True,'results':results,'binding':binding});save(OUT/'latest-correctness.json',{'pass':True,'run':str(run),'profiles':[{'profile':r['profile'],'variant':r['variant'],'scope':r['scope'],'checks':r['checks'],'eventCreates':r.get('eventCreates'),'eventResets':r.get('eventResets')} for r in results],'benchmarkReleased':False,'candidateSha256':sha(candidate),'productionApplied':False})
 print(json.dumps({'pass':True,'run':str(run),'benchmarkExecuted':False,'productionEdited':False}))
if __name__=='__main__':main()
