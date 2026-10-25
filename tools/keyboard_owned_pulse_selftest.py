#!/usr/bin/env python3
"""Owned pulse state machine: memory-only test executable, no real input imports."""
from pathlib import Path
import datetime,hashlib,json,os,subprocess
REPO=Path(__file__).resolve().parents[1];OUT=REPO.parents[2]/'Build/Validation/CtrlAlt-Specialist-R2-20261015/owned-pulse'
VC=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
def sha(b):return hashlib.sha256(b).hexdigest()
def save(p,j):p.write_text(json.dumps(j,indent=2)+'\n',encoding='utf-8')
def main():
 run=OUT/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(parents=True)
 sources={p:(REPO/p).read_bytes() for p in ['native/keyboard_owned_pulse.h','tools/keyboard_owned_pulse_selftest.cpp','deps/json/json.hpp']}
 for name,b in sources.items():(run/('fixture.cpp' if name.endswith('_selftest.cpp') else Path(name).name)).write_bytes(b)
 (run/'runner.snapshot.py').write_bytes(Path(__file__).read_bytes());profiles=[]
 for name,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/MTd'])]:
  args=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4',*flags,str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(name+'.obj')),'/Fe'+str(run/(name+'.exe'))]
  bat=run/(name+'.cmd');bat.write_text('@echo off\ncall "'+str(VC)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(args)+'\nif errorlevel 1 exit /b %errorlevel%\ndumpbin /imports "'+str(run/(name+'.exe'))+'" > "'+str(run/(name+'-imports.txt'))+'"\nexit /b %errorlevel%\n',encoding='utf-8')
  p=subprocess.run(['cmd.exe','/d','/c',str(bat)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=120);(run/(name+'-compile.log')).write_text(p.stdout+p.stderr,encoding='utf-8');assert p.returncode==0,str(run)+'\n'+p.stdout
  imports=(run/(name+'-imports.txt')).read_text(encoding='utf-8',errors='replace').lower();assert all(k not in imports for k in ['user32.dll','win32u.dll','sendinput','getasynckeystate'])
  p=subprocess.run([str(run/(name+'.exe'))],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=30);(run/(name+'-result.json')).write_text(p.stdout,encoding='utf-8');(run/(name+'-stderr.txt')).write_text(p.stderr,encoding='utf-8');assert p.returncode==0,str(run)+'\n'+p.stderr
  result=json.loads(p.stdout);assert result['pass'];profiles.append({'profile':name,**result});print(name+': '+str(result['checks'])+' checks',flush=True)
 assert all((REPO/p).read_bytes()==b for p,b in sources.items())
 result={'pass':True,'run':str(run),'profiles':profiles,'sourceHashes':{p:sha(b) for p,b in sources.items()},'artifacts':{p.name:sha(p.read_bytes()) for p in run.iterdir() if p.is_file()},'actualOSInput':False}
 save(run/'result.json',result);save(OUT/'latest.json',result);print(json.dumps({'pass':True,'run':str(run)}))
if __name__=='__main__':main()
