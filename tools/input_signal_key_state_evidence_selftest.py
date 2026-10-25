#!/usr/bin/env python3
"""Bounded activity-drain key/button observations: fake Ops only, frozen inputs.

No product, live input, COM or WindowsOps execution. Static and binary checks
run before each fixture. Output stays under InputSignalKeyState-20261015.
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
OUT = REPO.parents[2] / 'Build/Validation/InputSignalKeyState-20261015'
VCVARS = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
HEADER = 'native/input_signal_key_state_evidence.h'
FIXTURE = 'tools/input_signal_key_state_evidence_selftest.cpp'
RUNNER = 'tools/input_signal_key_state_evidence_selftest.py'
INPUTS = [HEADER,'native/input_signal_activity_evidence.h',FIXTURE,RUNNER,'deps/json/json.hpp']
PROFILES = [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/MTd'])]
FORBIDDEN_IMPORTS = ['user32.dll','win32u.dll','ole32.dll','oleaut32.dll','combase.dll','shell32.dll',
    'sendinput','getasynckeystate','getkeyboardstate','getforegroundwindow','getwindowthreadprocessid',
    'setwindowshookex','registerrawinputdevices','mapvirtualkey','keybd_event','mouse_event',
    'coinitialize','cocreateinstance','createprocessw']
TRIPWIRES = ['GetLastError','SetLastError','GetTickCount64','GetCurrentThreadId',
    'GetForegroundWindow','GetWindowThreadProcessId','GetAsyncKeyState','GetKeyboardState']


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
    header = sources[HEADER].decode('utf-8-sig')
    cpp = sources[FIXTURE].decode('utf-8-sig')
    before_include = cpp[:cpp.index('#include "../native/input_signal_key_state_evidence.h"')]
    wires = {}
    for api in TRIPWIRES:
        match = re.search(r'(?m)^#define '+api+r' (forbidden_os::\w+)$',before_include)
        if not match:raise AssertionError(('actual API boundary lacks link tripwire',api))
        wires[api]=match.group(1)
    code = strip_cpp(header)
    forbidden = r'\b(?:SendInput|keybd_event|mouse_event|MapVirtualKey\w*|GetSystemMetrics|SM_SWAPBUTTON|ToUnicode\w*|SetWindowsHookEx\w*|RegisterRawInputDevices|CreateThread|SetTimer|CreateTimerQueueTimer|Sleep|SleepEx|while|mutex|lock_guard|unique_lock|atomic_flag|Recorder)\b'
    hits = re.findall(forbidden,code)
    if hits:raise AssertionError(('input/hook/poll/lock/scan conversion forbidden',hits))
    if re.search(r'#include\s*<(?:thread|mutex)>|std::(?:thread|jthread|async)',code):raise AssertionError('new background framework in header')
    before_json = code[:code.index('inline nlohmann::json toJson(')]
    if re.search(r'\bnew\b|std::(?:vector|map|string)\s*[<{]',before_json):raise AssertionError('dynamic storage before caller toJson')
    for pattern in [r'candidateCapacity\s*=\s*16',r'std::array<Candidate,\s*candidateCapacity>',r'std::array<Candidate,\s*inputActivity::capacity>']:
        if not re.search(pattern,code):raise AssertionError(('fixed bounds missing',pattern))
    if re.search(r'\bWindowsOps\s+\w+',strip_cpp(cpp)):raise AssertionError('fixture instantiates actual adapter')
    return {'pass':True,'apiLinkTripwires':wires,'noNewHookPollingInputOrLock':True,
            'scanCodeConversion':False,'fixedStorageBeforeToJson':True,
            'actualWindowsOpsExecuted':False,'candidateCapacity':16,'upstreamSnapshotRowCapacity':64}


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
             'runtimeImportNote':'Static CRT may import TerminateProcess for fatal termination; fixture object has no direct adapter API imports and all adapter calls are unresolved tripwires.',
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
        'actualOSInput':False,'productExecuted':False,'windowsOpsExecuted':False,
        'scope':'Bounded drain-time selection and state evidence; pure fake Ops, no main.cpp integration.',
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
