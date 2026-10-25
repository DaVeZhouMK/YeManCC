#!/usr/bin/env python3
"""Bounded production-header retirement fixtures; preserve old consumer failures."""
from pathlib import Path
import datetime, hashlib, importlib.util, json, os, subprocess, sys
sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[1]
OUT = REPO.parents[2] / 'Testing/YMCC/Evidence/VC-PipeRetire23'
VC = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
def sha(b): return hashlib.sha256(b).hexdigest()
def save(p, x): p.write_text(json.dumps(x, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
def main():
    run = OUT / (datetime.datetime.now().strftime('%H%M%S')+'-'+str(os.getpid()))
    run.mkdir(parents=True)
    paths = ['native/input_host_pipe_transport.h', 'tools/input_pipe_retirement_selftest.py',
             'tools/input_pipe_retirement_memory_selftest.cpp', 'tools/input_pipe_retirement_real_selftest.cpp',
             'tools/input_pipe_transport_control_selftest.cpp', 'tools/input_pipe_transport_real_selftest.cpp',
             'tools/input_pipe_reuse_control_selftest.cpp', 'tools/input_pipe_test_allocation_fault.h', 'deps/json/json.hpp']
    inputs = {p:(REPO/p).read_bytes() for p in paths}
    before = (OUT/'before.h').read_bytes()
    save(run/'pins.json', {'inputs':{p:sha(b) for p,b in inputs.items()}, 'beforeHeaderSha256':sha(before), 'runnerSha256':sha(Path(__file__).read_bytes())})
    tool_root = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC')
    compiler = sorted(tool_root.iterdir())[-1] / 'bin/Hostx64/x64'
    sdk = Path('C:/Program Files (x86)/Windows Kits/10')
    tool_paths = [VC, compiler/'cl.exe', compiler/'link.exe', compiler/'c1xx.dll',
                  sdk/'Include/10.0.26100.0/um/windows.h', sdk/'Lib/10.0.26100.0/um/x64/kernel32.lib',
                  sdk/'Lib/10.0.26100.0/um/x64/advapi32.lib', Path(os.__file__).parents[1]/'python.exe']
    save(run/'tool-pins.json', {'files':{str(p):sha(p.read_bytes()) for p in tool_paths},
                              'sdkVersion':'10.0.26100.0','compilerDirectory':str(compiler),'pythonExecutableAlias':sys.executable,
                              'systemPathPrefixAddedForVcvarsRegistryLookup':True})
    results = []
    for profile,flags in [('Release',['/O2','/DNDEBUG','/MT']),('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
        fixtures = [('memory','input_pipe_retirement_memory_selftest.cpp',[],False),
                    ('real','input_pipe_retirement_real_selftest.cpp',[],False),
                    ('existing-control','input_pipe_transport_control_selftest.cpp',[],False),
                    ('existing-real','input_pipe_transport_real_selftest.cpp',[],False),
                    ('existing-reuse','input_pipe_reuse_control_selftest.cpp',['/DR45_CANDIDATE'],False),
                    ('before-negative','input_pipe_retirement_memory_selftest.cpp',['/DPIPE_RETIRE_LEGACY'],True)]
        for scope,fixture,defines,negative in fixtures:
            target=run/(profile+'-'+scope);target.mkdir()
            for name,data in [('input_host_pipe_transport.h',before if negative else inputs[paths[0]]),
                              ('fixture.cpp',inputs['tools/'+fixture]),('json.hpp',inputs['deps/json/json.hpp']),
                              ('input_pipe_test_allocation_fault.h',inputs['tools/input_pipe_test_allocation_fault.h'])]:
                (target/name).write_bytes(data)
            args=['cl','/nologo','/std:c++20','/utf-8','/EHsc','/W4',*flags,*defines,str(target/'fixture.cpp'),'/I'+str(target),'/Fo'+str(target/'fixture.obj'),'/Fe'+str(target/'fixture.exe'),'advapi32.lib']
            batch=target/'build.bat';batch.write_text('@echo off\nset PATH=C:\\Windows\\System32;C:\\Windows;%PATH%\nset VSLANG=1033\ncall "'+str(VC)+'" 10.0.26100.0 >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(args)+'\nexit /b %errorlevel%\n',encoding='utf-8')
            build=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=target,capture_output=True,encoding='utf-8',errors='replace',timeout=180)
            (target/'build.log').write_text(build.stdout+build.stderr,encoding='utf-8')
            if build.returncode:
                results.append({'profile':profile,'scope':scope,'status':'BUILD_FAIL','exitCode':build.returncode});save(run/'progress.json',results)
                print(profile+' '+scope+' BUILD_FAIL '+str(target),flush=True);continue
            try:
                process=subprocess.run([str(target/'fixture.exe')],cwd=target,capture_output=True,encoding='utf-8',errors='replace',timeout=40)
            except subprocess.TimeoutExpired as failure:
                (target/'timeout-stdout.log').write_bytes(failure.stdout or b'')
                (target/'timeout-stderr.log').write_bytes(failure.stderr or b'')
                results.append({'profile':profile,'scope':scope,'status':'TIMEOUT','resourceReturnProven':False})
                save(run/'progress.json',results)
                print(profile+' '+scope+' TIMEOUT; no resource-return claim',flush=True)
                continue
            (target/'stdout.json').write_text(process.stdout,encoding='utf-8');(target/'stderr.log').write_text(process.stderr,encoding='utf-8')
            try: result=json.loads(process.stdout)
            except json.JSONDecodeError: result={}
            status='EXPECTED_FAIL' if negative and process.returncode==1 and result.get('pass') is False else ('PASS' if process.returncode==0 and result.get('pass') is True else 'FAIL')
            results.append({'profile':profile,'scope':scope,'status':status,'exitCode':process.returncode,'checks':result.get('checks'),'cases':len(result.get('cases',[]))})
            save(run/'progress.json',results);print(profile+' '+scope+' '+status+' '+str(result.get('checks',''))+' '+process.stderr.strip(),flush=True)
    stable={p:(REPO/p).read_bytes()==b for p,b in inputs.items()}
    passed=all(r['status'] in ('PASS','EXPECTED_FAIL') for r in results) and all(stable.values())
    save(run/'artifact-pins.json',{str(p.relative_to(run)):sha(p.read_bytes()) for p in run.rglob('*') if p.is_file()})
    save(run/'result.json',{'pass':passed,'results':results,'stableInputs':stable,'arbitraryOsBlockingBound':'UNKNOWN','fullHostLifecycleComplete':False})
    save(OUT/'latest.json',{'pass':passed,'run':str(run),'results':results})
    print(json.dumps({'pass':passed,'run':str(run)}));return 0 if passed else 1
if __name__=='__main__': sys.exit(main())
