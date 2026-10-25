"""Compile the actual product's constant C++ script writer; test only its validator functions."""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--scratch',required=True);args=parser.parse_args()
repo=Path(__file__).resolve().parents[2];scratch=Path(args.scratch).resolve()
if scratch.exists():raise RuntimeError('Use a new scratch directory')
scratch.mkdir(parents=True)
source=(repo/'native/main.cpp').read_text(encoding='utf-8-sig')
start=source.index('f << "function Assert-FanHostV2Payload {')
end=source.index('f << "function Get-UpdateLayoutRoots {',start)
lines=source[source.rfind('\n',0,start)+1:source.rfind('\n',0,end)+1].splitlines()
statements=[]
for line in lines:
    if re.fullmatch(r'\s*f << "(?:[^"\\]|\\.)*";\s*',line):statements.append(line.strip())
    elif line.strip() and not line.strip().startswith('//'):raise RuntimeError('Unexpected nonconstant C++ statement: '+line)
cpp='#include <fstream>\nint main(int argc,char** argv){if(argc!=2)return 2;std::ofstream f(argv[1],std::ios::binary);\n'+'\n'.join(statements)+'\nreturn f.good()?0:3;}\n'
(scratch/'writer.cpp').write_text(cpp,encoding='utf-8')
vcvars = r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
cmd='@echo off\r\ncall "'+vcvars+'" >nul\r\nif errorlevel 1 exit /b 1\r\n'
cmd+='set "WindowsSdkDir=C:\\Program Files (x86)\\Windows Kits\\10\\"\r\nset "WindowsSDKVersion=10.0.26100.0\\"\r\nset "INCLUDE=C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Tools\\MSVC\\14.44.35207\\include;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\ucrt;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\shared;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\um;C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\winrt;%INCLUDE%"\r\nset "LIB=C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Tools\\MSVC\\14.44.35207\\lib\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\ucrt\\x64;C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\10.0.26100.0\\um\\x64;%LIB%"\r\n'
cmd+='cl /nologo /std:c++17 /EHsc /W4 /WX "'+str(scratch/'writer.cpp')+'" /Fo"'+str(scratch/'writer.obj')+'" /Fe"'+str(scratch/'writer.exe')+'"\r\nif errorlevel 1 exit /b 1\r\n'
cmd+='"'+str(scratch/'writer.exe')+'" "'+str(scratch/'installer-functions.ps1')+'"\r\nexit /b %ERRORLEVEL%\r\n'
(scratch/'build-writer.cmd').write_text(cmd,encoding='ascii',newline='')
def run(name,command,environment=None):
    p=subprocess.run(command,cwd=scratch,capture_output=True,env=environment)
    (scratch/(name+'.log')).write_bytes(p.stdout+p.stderr)
    if p.returncode:raise RuntimeError(name+' failed: '+str(scratch/(name+'.log')))
    print(name+': passed',flush=True)
run('compile-actual-script-writer',['cmd.exe','/d','/c',str(scratch/'build-writer.cmd')])
env=os.environ.copy();env['PSModulePath']=r'C:\Windows\System32\WindowsPowerShell\v1.0\Modules'+';'+env.get('PSModulePath','')
run('actual-installer-validator',['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(repo/'tools/hc-shared-device-runtime/installer-selftest.ps1'),
 '-ProjectRoot',str(repo),'-FunctionsPath',str(scratch/'installer-functions.ps1'),'-ScratchRoot',str(scratch/'fixtures')],env)
receipt={'status':'passed','source':str(repo/'native/main.cpp'),'nativeSourceSha256':hashlib.sha256(source.encode()).hexdigest(),
 'functionWriterStatements':len(statements),'functionsSha256':hashlib.sha256((scratch/'installer-functions.ps1').read_bytes()).hexdigest(),
 'result':json.loads((scratch/'fixtures/verification.json').read_text(encoding='utf-8-sig')),
 'productHostExecuted':False,'installedDeploymentPerformed':False}
(scratch/'verification.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'status':'passed','checks':receipt['result']['checks']}))
