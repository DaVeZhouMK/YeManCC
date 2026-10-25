from pathlib import Path
import datetime,hashlib,json,os,subprocess,sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT.parents[2]/'Build/Validation/TaskView-SelfWorker-20261016/worker-memory'
VC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
def sha(b):return hashlib.sha256(b).hexdigest()
def main():
 run=OUT/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
 inputs=['native/task_view_worker.h','tools/task_view_worker_selftest.cpp','deps/json/json.hpp']
 sources={f:(ROOT/f).read_bytes() for f in inputs}
 for f,b in sources.items():p=run/'source'/f;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
 profiles=[]
 for name,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/MTd'])]:
  exe=run/(name+'.exe')
  command=['cl','/nologo','/std:c++20','/utf-8','/W4','/WX','/EHsc',*flags,str(run/'source/tools/task_view_worker_selftest.cpp'),'/I'+str(run/'source/deps/json'),'/Fo'+str(run/(name+'.obj')),'/Fe'+str(exe),'/link','/NODEFAULTLIB:user32.lib','/NODEFAULTLIB:shell32.lib','/NODEFAULTLIB:ole32.lib','/NODEFAULTLIB:oleaut32.lib']
  batch=run/(name+'.bat');batch.write_text('@echo off\ncall "'+str(VC)+'" >nul\nif errorlevel 1 exit /b 1\n'+subprocess.list2cmdline(command)+'\nif errorlevel 1 exit /b 1\ndumpbin /imports "'+str(exe)+'" > "'+str(run/(name+'-imports.txt'))+'"\nexit /b %errorlevel%\n',encoding='utf-8')
  p=subprocess.run(['cmd','/d','/c',str(batch)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=60);(run/(name+'-build.log')).write_text(p.stdout+p.stderr,encoding='utf-8');assert p.returncode==0,p.stdout+p.stderr
  imports=(run/(name+'-imports.txt')).read_text(encoding='utf-8',errors='replace').lower();assert all(x not in imports for x in ['user32.dll','shell32.dll','ole32.dll','oleaut32.dll','combase.dll','createprocessw','sendinput','openprocess'])
  p=subprocess.run([str(exe)],cwd=run,capture_output=True,text=True,timeout=20);assert p.returncode==0,p.stderr
  result=json.loads(p.stdout);assert result['pass'];profiles.append({'profile':name,**result});print(name+': '+str(result['checks'])+' checks / '+str(result['cases'])+' cases PASS',flush=True)
 assert all((ROOT/f).read_bytes()==b for f,b in sources.items()),'Source changed while testing'
 result={'pass':True,'run':str(run),'profiles':profiles,'sourceHashes':{f:sha(b) for f,b in sources.items()},'actualShellInvoked':False}
 (run/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');(OUT/'latest.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
if __name__=='__main__':main()