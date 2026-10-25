#!/usr/bin/env python3
"""Current-source Host transaction owner regression, private isolated test only."""
from pathlib import Path
import argparse,datetime,hashlib,json,os,subprocess
from input_pipe_wait_error_selftest import source_function
REPO=Path(__file__).resolve().parents[1]
VC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
def sha(b):return hashlib.sha256(b).hexdigest()
def save(p,o):p.write_text(json.dumps(o,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def main(out):
 run=out/('actual-source-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
 mainBytes=(REPO/'native/main.cpp').read_bytes();request=source_function(mainBytes.decode('utf-8-sig'),'static bool inputHostRequestPreparedLocked(')
 begin=request.index('    // A Locked caller');end=request.index('    if (!g_inputHostProcess',begin)
 inputs={'candidate-entry.inc':request[begin:end].encode('utf-8'),'input_host_transaction_mutex.h':(REPO/'native/input_host_transaction_mutex.h').read_bytes(),'fixture.cpp':(REPO/'tools/input_host_transaction_selftest.cpp').read_bytes(),'json.hpp':(REPO/'deps/json/json.hpp').read_bytes()}
 for name,data in inputs.items():(run/name).write_bytes(data)
 results=[]
 for profile,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
  args=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX',*flags,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(profile+'.obj')),'/Fe'+str(run/(profile+'.exe'))]
  batch=run/(profile+'-build.bat');batch.write_text('@echo off\ncall "'+str(VC)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(args)+'\nexit /b %errorlevel%\n',encoding='utf-8')
  process=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=180);(run/(profile+'-build.log')).write_text(process.stdout+process.stderr,encoding='utf-8');assert process.returncode==0,(profile,process.stdout,process.stderr)
  process=subprocess.run([str(run/(profile+'.exe'))],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=35);(run/(profile+'-result.json')).write_text(process.stdout,encoding='utf-8');(run/(profile+'-stderr.log')).write_text(process.stderr,encoding='utf-8');assert process.returncode==0,(profile,process.stdout,process.stderr)
  result=json.loads(process.stdout);assert result['pass'];results.append({'profile':profile,**result});print(profile+' '+str(result['groups'])+' groups '+str(result['checks'])+' checks',flush=True)
 currentRequest=source_function((REPO/'native/main.cpp').read_bytes().decode('utf-8-sig'),'static bool inputHostRequestPreparedLocked(')
 assert currentRequest==request and (REPO/'native/input_host_transaction_mutex.h').read_bytes()==inputs['input_host_transaction_mutex.h'],'owned source changed during regression'
 save(run/'source-binding.json',{'pass':True,'capturedMainSha256':sha(mainBytes),'actualCurrentRequestSha256':sha(request.encode()),'inputs':{n:sha(b) for n,b in inputs.items()},'productExecuted':False,'allProcessesTerminal':True,'results':results})
 save(out/'latest-actual-source.json',{'pass':True,'run':str(run),'actualSourceRequestUnchanged':True,'results':results})
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,default=REPO.parents[2]/'Build/Validation/VirtualInputPerf-R50-20261015/transaction-mutex');main(p.parse_args().out)
