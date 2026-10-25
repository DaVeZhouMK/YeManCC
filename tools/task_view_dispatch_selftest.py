#!/usr/bin/env python3
"""Pure-Ops TaskView dispatcher tests, frozen sources, Release/Checked and imports.

Writes only Build/Validation/TaskView-Api-20261015/dispatcher. No product, helper,
COM or input execution. WindowsOps API names are unresolved link tripwires;
only FakeOps is instantiated. Harness threads exercise Busy; the dispatcher
itself is audited to contain no threads, timers, background polling or loops.
"""
from __future__ import annotations
import argparse
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
OUT = REPO.parents[2] / 'Build/Validation/TaskView-Api-20261015/dispatcher'
VCVARS = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
HEADER = 'native/task_view_dispatch.h'
FIXTURE = 'tools/task_view_dispatch_selftest.cpp'
RUNNER = 'tools/task_view_dispatch_selftest.py'
TOOLCHAIN = 'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'
INPUTS = [HEADER, 'native/task_view_foreground_grant.h', FIXTURE, RUNNER, 'deps/json/json.hpp', TOOLCHAIN]
PROFILES = [('Release', ['/O2','/DNDEBUG','/MT']), ('Checked', ['/Od','/RTC1','/D_DEBUG','/MTd'])]
FORBIDDEN_IMPORTS = ['user32.dll','win32u.dll','ole32.dll','oleaut32.dll','combase.dll','shell32.dll',
                     'sendinput','getasynckeystate','getkeyboardstate','keybd_event','mouse_event',
                     'cocreateinstance','coinitialize','coinitializeex','cogetclassobject',
                     'createprocessw','openprocess','createjobobjectw','sendmessagetimeoutw','getforegroundwindow','setforegroundwindow','attachthreadinput',
                     'setinformationjobobject','assignprocesstojobobject','getexitcodeprocess','allowsetforegroundwindow']
TRIPWIRES = ['GetTickCount64','GetLastError','GetFileAttributesW','CreateJobObjectW',
             'SetInformationJobObject','CreateProcessW','AssignProcessToJobObject','ResumeThread',
             'WaitForSingleObject','GetExitCodeProcess','TerminateProcess','CloseHandle','AllowSetForegroundWindow','GetCurrentProcessId','SetLastError','CreateEventW','WaitForMultipleObjects','SetEvent',
             'GetCurrentThreadId','GetWindowThreadProcessId','GetForegroundWindow','SendMessageTimeoutW']


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
    before_include = cpp[:cpp.index('#include "../native/task_view_dispatch.h"')]
    wires = {}
    for api in TRIPWIRES:
        match = re.search(r'(?m)^#define '+api+r' (forbidden_os::\w+)$',before_include)
        if not match:
            raise AssertionError(('actual adapter API lacks unresolved tripwire',api))
        wires[api] = match.group(1)
    code = strip_cpp(header)
    forbidden = r'\b(?:SendInput|OpenProcess|TerminateJobObject|CreateThread|_beginthreadex|QueueUserWorkItem|CreateThreadpoolWork|CreateTimerQueueTimer|SetTimer|std::thread|std::jthread|std::async|sleep_for|Sleep|SleepEx|while|for|INFINITE)\b'
    hits = re.findall(forbidden,code)
    if hits:
        raise AssertionError(('background work, unbounded polling, foreign process access or input in dispatcher',hits))
    for token in ['JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK','CREATE_SUSPENDED|CREATE_NO_WINDOW',
                  'AllowSetForegroundWindow(pid)',
                  'AssignProcessToJobObject(job,process)','TerminateProcess(process,cancelledExit)',
                  'std::atomic_flag','std::memory_order_acquire','std::memory_order_release']:
        if token not in code:
            raise AssertionError(('owned process/lease invariant missing',token))
    if not re.search(r'CreateProcessW\(helper,mutableCommand\.data\(\),nullptr,nullptr,FALSE,',code):
        raise AssertionError('helper process arguments or non-inheritance adapter contract changed')
    if 'WindowsOps ' in strip_cpp(cpp) or 'WindowsOps{' in strip_cpp(cpp):
        raise AssertionError('fixture instantiates actual WindowsOps')
    return {'pass':True,'unresolvedApiTripwires':wires,'backgroundThreadsOrPollingLoops':False,
            'jobKillOnClose':True,'shellDescendantsMayBreakAway':True,'startSuspendedAndHidden':True,'onlyOwnedProcessTermination':True,
            'taskViewSpecificAtomicLease':True,'adapterExecuted':False,
            'runtimeScope':'FakeOps only; actual OS adapter constants/structure checked statically'}


def compile_and_run(run,name,flags):
    exe = run/(name+'.exe')
    imports_path = run/(name+'-imports.txt')
    args = ['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/Bv',*flags,
            str(run/'source'/FIXTURE),'/I'+str(run/'source/deps/json'),
            '/Fo'+str(run/(name+'.obj')),'/Fd'+str(run/(name+'-compiler.pdb')),
            '/Fe'+str(exe),'/link','/INCREMENTAL:NO','/NODEFAULTLIB:user32.lib',
            '/NODEFAULTLIB:ole32.lib','/NODEFAULTLIB:oleaut32.lib','/NODEFAULTLIB:shell32.lib']
    # Reuse the reviewed installed SDK setup, including machines where vcvars
    # alone omits UCRT headers. Import the frozen toolchain, not a moving source.
    preamble = subprocess.run(['node','--input-type=module','-e',
        'const m = await import(process.argv[2]); process.stdout.write(m.calibrationToolchainPreamble(process.argv[1]));',
        str(VCVARS),(run/'source'/TOOLCHAIN).as_uri()],capture_output=True,
        encoding='utf-8',errors='replace',check=True,timeout=15)
    command = preamble.stdout
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
    options = argparse.ArgumentParser(description=__doc__)
    options.add_argument('--header-snapshot',type=Path,help='test explicit header bytes from the authorized evidence directory; never production acceptance')
    args = options.parse_args()
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    run = OUT/('run-'+stamp+'-'+str(os.getpid()));run.mkdir(parents=True)
    result = {'pass':False,'productionAcceptance':False,'run':str(run),'startedUtc':stamp,'profiles':[],
              'actualOSInput':False,'productExecuted':False,'actualHelperExecuted':False,'comInvoked':False,
              'scope':'TaskView Dispatcher generic Ops contract; fake handle ownership, waits, time and all API outcomes.',
              'productionWrites':False,'writesOutsideEvidenceByRunner':False}
    try:
        if not VCVARS.is_file():raise FileNotFoundError(str(VCVARS))
        current_at_start = {name:(REPO/name).read_bytes() for name in INPUTS}
        sources = dict(current_at_start)
        if args.header_snapshot:
            path = args.header_snapshot.resolve()
            if not path.is_relative_to(OUT.resolve()):raise AssertionError('header snapshot must be inside authorized evidence directory')
            sources[HEADER]=path.read_bytes();result['headerSnapshotOverride']=str(path)
        result['sourceHashes']={name:sha(data) for name,data in sources.items()}
        result['currentProductionHashesAtStart']={name:sha(data) for name,data in current_at_start.items()}
        for name,data in sources.items():write(run/'source'/name,data)
        save(run/'dependencies.json',{'sourceHashes':result['sourceHashes'],'currentProductionHashesAtStart':result['currentProductionHashesAtStart']})
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
        changed = [name for name in INPUTS if current[name]!=current_at_start[name]]
        result['filesChangedDuringRun']=changed
        result['testedDependenciesStableThroughRun']=not changed
        result['pass']=not changed and all(profile['pass'] for profile in result['profiles'])
        result['productionAcceptance']=result['pass'] and not args.header_snapshot
        if changed:result['error']='tested input changed during run; frozen evidence retained, rerun required'
        elif not result['pass']:result['error']='; '.join(profile['profile']+': '+profile.get('error','fixture assertions failed') for profile in result['profiles'] if not profile['pass'])
    except Exception as error:
        result['error']=str(error);write(run/'runner-error.txt',traceback.format_exc())
    result['artifacts']={str(path.relative_to(run)).replace('\\','/'):sha(path.read_bytes()) for path in sorted(run.rglob('*')) if path.is_file()}
    save(run/'result.json',result)
    save(OUT/('latest-snapshot.json' if args.header_snapshot else 'latest.json'),result)
    print(json.dumps({'pass':result['pass'],'productionAcceptance':result['productionAcceptance'],'run':str(run),'error':result.get('error')},ensure_ascii=False),flush=True)
    return 0 if result['pass'] else 1


if __name__=='__main__':raise SystemExit(main())
