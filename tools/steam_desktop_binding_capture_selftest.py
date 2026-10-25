#!/usr/bin/env python3
"""Pure existing-session Steam desktop-binding collector: fake FS/Ops only.

No actual IO, Steam, network, COM or OS input is performed by the fixture.
Compile frozen dependencies in Release/Checked; audit imports before running.
"""
from __future__ import annotations
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import traceback
sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[1]
OUT = REPO.parents[2] / 'Build/Validation/SteamDesktopBindingCapture-20261015'
VCVARS = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
HEADER = 'native/steam_desktop_binding_capture.h'
FIXTURE = 'tools/steam_desktop_binding_capture_selftest.cpp'
RUNNER = 'tools/steam_desktop_binding_capture_selftest.py'
INPUTS = [HEADER,'native/steam_desktop_binding_evidence.h','native/steamdeck_mouse_vdf.h',FIXTURE,RUNNER,'deps/json/json.hpp']
PROFILES = [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/MTd'])]
FORBIDDEN_IMPORTS = ['user32.dll','win32u.dll','ole32.dll','oleaut32.dll','combase.dll','shell32.dll',
    'steam_api','steamclient','winhttp.dll','wininet.dll','ws2_32.dll','sendinput','getasynckeystate',
    'getkeyboardstate','setwindowshookex','registerrawinputdevices','keybd_event','mouse_event',
    'coinitialize','cocreateinstance','createprocessw']
TRIPWIRES = ['CreateFileW','ReadFile','FindFirstFileW','FindFirstFileExW','GetFileAttributesW','GetTickCount64','SendInput','GetAsyncKeyState']


def sha(data):
    return hashlib.sha256(data.encode('utf-8') if isinstance(data,str) else data).hexdigest()

def write(path, data):
    path = Path(path).resolve()
    if not path.is_relative_to(OUT.resolve()):
        raise AssertionError(('write outside authorized evidence directory',str(path)))
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(data.encode('utf-8') if isinstance(data,str) else data)

def save(path, data):
    write(path,json.dumps(data,ensure_ascii=False,indent=2)+'\n')

def strip_cpp(text):
    return re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/',
                  lambda match: ''.join('\n' if c=='\n' else ' ' for c in match.group()),text)

def source_audit(sources):
    code=strip_cpp(sources[HEADER].decode('utf-8-sig'))
    forbidden=r'\b(?:SendInput|GetAsyncKeyState|GetKeyboardState|GetTickCount64|GetLastError|CreateFile\w*|ReadFile|FindFirstFile\w*|fopen|ifstream|ofstream|fstream|filesystem|CreateProcess\w*|CreateThread|Sleep|SetTimer|CoCreateInstance|SteamClient|WinHttp\w*|socket|mutex|atomic_flag)\b'
    hits=re.findall(forbidden,code)
    if hits:raise AssertionError(('side effect in pure collector',hits))
    ops=set(re.findall(r'ops\.(\w+)\s*\(',code))
    allowed={'session','validSession','sameSession','clientIdentity','read','widen','hash','now'}
    if not ops or ops-allowed:raise AssertionError(('unexpected Ops method',sorted(ops-allowed)))
    for symbol in ['steamdeck::libraryPaths(', 'steamdeck::desktopAutosave(', 'steamdeck::bindingevidence::summarize(']:
        if symbol not in code:raise AssertionError(('shared parser helper not reused',symbol))
    if '.what(' in code:raise AssertionError('raw exception message exported')
    if re.search(r'\bstatic\s+(?!constexpr)',code):raise AssertionError('mutable static state in collector')
    return {'pass':True,'pureOpsMethods':sorted(ops),'noActualIoOrLiveSteam':True,
            'existingLibraryAndSelectionHelpers':True,'GoodallSummaryOnly':True,
            'mutableGlobalState':False,'noRawExceptionMessages':True}


def compile_and_run(run,name,flags):
    exe = run/(name+'.exe')
    imports_path = run/(name+'-imports.txt')
    args = ['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/Bv',*flags,
            str(run/'source'/FIXTURE),'/I'+str(run/'source/deps/json'),
            '/Fo'+str(run/(name+'.obj')),'/Fd'+str(run/(name+'-compiler.pdb')),
            '/Fe'+str(exe),'/link','/INCREMENTAL:NO','/NODEFAULTLIB:user32.lib',
            '/NODEFAULTLIB:ole32.lib','/NODEFAULTLIB:oleaut32.lib','/NODEFAULTLIB:shell32.lib']
    command = '@echo off\ncall "'+str(VCVARS)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'
    command += subprocess.list2cmdline(args)+'\nif errorlevel 1 exit /b %errorlevel%\n'
    command += 'dumpbin /symbols "'+str(run/(name+'.obj'))+'" > "'+str(run/(name+'-object-symbols.txt'))+'"\nif errorlevel 1 exit /b %errorlevel%\n'
    command += 'dumpbin /imports "'+str(exe)+'" > "'+str(imports_path)+'"\nexit /b %errorlevel%\n'
    batch = run/(name+'.cmd');write(batch,command)
    temp = run/'temp';temp.mkdir(exist_ok=True)
    env = os.environ.copy();env.update(TMP=str(temp),TEMP=str(temp),PYTHONDONTWRITEBYTECODE='1')
    p = subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,env=env,capture_output=True,
                       encoding='utf-8',errors='replace',timeout=120)
    write(run/(name+'-compile.log'),p.stdout+p.stderr)
    profile = {'profile':name,'flags':flags,'pass':False,'compileExitCode':p.returncode,'executableRun':False}
    if p.returncode:
        profile['error']='fixture compilation/import dump failed';return profile
    imports = imports_path.read_text(encoding='utf-8',errors='replace').lower()
    banned = [name for name in FORBIDDEN_IMPORTS if name in imports]
    symbols = (run/(name+'-object-symbols.txt')).read_text(encoding='utf-8',errors='replace').lower()
    adapter_refs = [api for api in TRIPWIRES if '__imp_'+api.lower() in symbols]
    if adapter_refs:
        profile['error']='actual WindowsOps API referenced by fixture object: '+','.join(adapter_refs);return profile
    audit = {'pass':not banned,'forbiddenMatches':banned,'forbiddenSymbols':FORBIDDEN_IMPORTS,
             'fixtureObjectActualAdapterImports':adapter_refs,
             'runtimeTerminationImportPresent':'terminateprocess' in imports,
             'runtimeImportNote':'Static CRT standard streams may import file/termination APIs; the collector and fixture object have no direct OS IO/input references. All capture reads use in-memory Ops.',
             'dlls':re.findall(r'(?m)^\s+([\w.-]+\.dll)\s*$',imports),'sha256':sha(imports_path.read_bytes())}
    save(run/(name+'-import-audit.json'),audit);profile['importAudit']=audit
    if banned:
        profile['error']='forbidden imports; executable was not run';return profile
    p = subprocess.run([str(exe)],cwd=run,env=env,capture_output=True,
                       encoding='utf-8',errors='replace',timeout=40)
    write(run/(name+'-result.json'),p.stdout);write(run/(name+'-stderr.txt'),p.stderr)
    profile.update(executableRun=True,testExitCode=p.returncode)
    try:
        data = json.loads(p.stdout);profile['test']=data
        profile['failedCases']=[row for row in data.get('cases',[]) if not row.get('pass')]
        profile['pass']=p.returncode==0 and data.get('pass') is True
    except (ValueError,TypeError) as error:
        profile['error']='invalid fixture result: '+str(error)
    return profile

def main():
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    run = OUT/('run-'+stamp+'-'+str(os.getpid()));run.mkdir(parents=True)
    result = {'pass':False,'run':str(run),'startedUtc':stamp,'profiles':[],
        'actualOSInput':False,'productExecuted':False,'actualSteamOrFileIo':False,
        'scope':'Stored desktop bindings from exact declared current-account paths; all IO/session/hash/time Ops are fakes; no main.cpp integration.',
        'writesOutsideEvidenceByRunner':False}
    try:
        if not VCVARS.is_file():raise FileNotFoundError(str(VCVARS))
        sources = {name:(REPO/name).read_bytes() for name in INPUTS}
        result['sourceHashes']={name:sha(data) for name,data in sources.items()}
        for name,data in sources.items():write(run/'source'/name,data)
        save(run/'dependencies.json',{'sourceHashes':result['sourceHashes']})
        result['sourceAudit']=source_audit(sources);save(run/'source-audit.json',result['sourceAudit'])
        for name,flags in PROFILES:
            try:profile=compile_and_run(run,name,flags)
            except Exception as error:
                write(run/(name+'-runner-error.txt'),traceback.format_exc())
                profile={'profile':name,'pass':False,'error':str(error)}
            result['profiles'].append(profile)
            print(name+': '+('PASS' if profile['pass'] else 'FAIL')+', checks='+str(profile.get('test',{}).get('checks',0)),flush=True)
            for row in profile.get('failedCases',[]):print('  '+row['case']+': '+row.get('error','failed'),flush=True)
        current = {name:(REPO/name).read_bytes() for name in INPUTS}
        result['currentSourceHashes']={name:sha(data) for name,data in current.items()}
        result['filesChangedDuringRun']=[name for name in INPUTS if current[name]!=sources[name]]
        result['testedDependenciesStableThroughRun']=not result['filesChangedDuringRun']
        result['pass']=result['testedDependenciesStableThroughRun'] and all(profile['pass'] for profile in result['profiles'])
        if not result['pass']:result['error']='dependency changed or fixture/import/compile checks failed'
    except Exception as error:
        result['error']=str(error);write(run/'runner-error.txt',traceback.format_exc())
    result['artifacts']={str(path.relative_to(run)).replace('\\','/'):sha(path.read_bytes()) for path in sorted(run.rglob('*')) if path.is_file()}
    save(run/'result.json',result);save(OUT/'latest.json',result)
    print(json.dumps({'pass':result['pass'],'run':str(run),'error':result.get('error')},ensure_ascii=False),flush=True)
    return 0 if result['pass'] else 1

if __name__=='__main__':raise SystemExit(main())
