#!/usr/bin/env python3
"""R32 source-extracted permitted-result cancellation ownership model.
No real pipes, event handles, device, product, service or registry calls.
"""
from pathlib import Path
import datetime,hashlib,json,os,subprocess,sys
sys.dont_write_bytecode=True
from virtual_sample_tree_perf_selftest import source_function
REPO=Path(__file__).resolve().parents[1];OUT=REPO.parents[2]/'Build/Validation/VirtualInputPerf-R32-20261015'
VCVARS=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
sha=lambda b:hashlib.sha256(b).hexdigest()
def save(p,x):p.write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def main():
 run=OUT/('ownership-model-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
 raw=(REPO/'native/main.cpp').read_bytes();src=raw.decode('utf-8-sig')
 funcs=[source_function(src,'static bool inputHostPipeIo('),source_function(src,'static bool inputHostWaitClientLocked(')]
 (run/'actual-helpers.hpp').write_bytes(('\n\n'.join(funcs)+'\n').encode('utf-8'));(run/'fixture.cpp').write_bytes((REPO/'tools/input_pipe_cancel_ownership_selftest.cpp').read_bytes());(run/'json.hpp').write_bytes((REPO/'deps/json/json.hpp').read_bytes())
 profiles=[]
 for name,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
  cmd=['cl','/nologo','/std:c++20','/utf-8','/EHsc',*flags,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(name+'.obj')),'/Fe'+str(run/(name+'.exe'))]
  bat=run/(name+'-build.bat');bat.write_text('@echo off\nset VSLANG=1033\ncall "'+str(VCVARS)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(cmd)+'\nexit /b %errorlevel%\n',encoding='utf-8')
  p=subprocess.run(['cmd.exe','/d','/c',str(bat)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=180);(run/(name+'-build.log')).write_text(p.stdout+p.stderr,encoding='utf-8')
  if p.returncode:raise RuntimeError('compile '+name+' failed: '+str(run/(name+'-build.log')))
  p=subprocess.run([str(run/(name+'.exe'))],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=60);(run/(name+'-result.json')).write_text(p.stdout,encoding='utf-8');(run/(name+'-stderr.log')).write_text(p.stderr,encoding='utf-8')
  if p.returncode:raise RuntimeError('run '+name+': '+p.stderr)
  result=json.loads(p.stdout);profiles.append({'profile':name,**result});print(name+': '+str(result['checks'])+' checks; '+str(result['conditionalLifetimeGapRows'])+' permitted pending-return cases',flush=True)
 now=(REPO/'native/main.cpp').read_bytes();current=now.decode('utf-8-sig');assert all(source_function(current,sig)==f for sig,f in zip(['static bool inputHostPipeIo(','static bool inputHostWaitClientLocked('],funcs)),'audited helper changed during test'
 binding={'pass':True,'snapshotMainSha256':sha(raw),'currentMainSha256':sha(now),'wholeMainStable':raw==now,'actualHelperBodiesStable':True,'sourceFunctions':[{'signature':f.split('{',1)[0].strip(),'sha256':sha(f.encode('utf-8'))} for f in funcs],'kernelOperationFunctionsReplacedByDeterministicTestModel':True,'realWin32ThreadLastErrorOnly':True,'modelsBackedByR30OfficialApiReferences':True,'noDereferenceOfExpiredOperationPointers':True,'productionEdited':False,'realKernelFaultReproduced':False,'artifacts':{p.name:sha(p.read_bytes()) for p in run.iterdir() if p.is_file()}}
 save(run/'source-binding.json',binding);save(run/'result.json',{'pass':True,'profiles':profiles,'sourceBinding':binding,'notRuntimeFaultEvidence':True,'notCpuBenefitEvidence':True})
 save(OUT/'latest.json',{'pass':True,'run':str(run),'profiles':[{'profile':p['profile'],'checks':p['checks'],'conditionalLifetimeGapRows':p['conditionalLifetimeGapRows']} for p in profiles]});print(json.dumps({'pass':True,'run':str(run),'productionEdited':False,'realKernelFaultReproduced':False}))
if __name__=='__main__':main()
