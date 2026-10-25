#!/usr/bin/env python3
"""Offline actual gamebar_trace_runtime.h consumer, private Win32 owners + fake WinRT.
No installation, export, GameBar activation, input, device, driver, service or system sleep.
"""
from __future__ import annotations
import argparse
import datetime as dt
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
DEFAULT = Path('G:/YeManCC-Work/Testing/YMCC/Evidence/VC-GamebarOwner23')
HEADER = 'native/gamebar_trace_runtime.h'
HELPER = 'tools/input_timer_evidence_adapter_selftest.py'
CASES = ('normal', 'partial', 'partial-stop', 'first-subscribe-failure', 'worker-six-retries', 'retry-budget', 'concurrent-init', 'stop-during-init',
         'stop-during-factory', 'revoke-failure', 'revoke-both-fail', 'revoke-redirect-failure', 'inflight-callback', 'inflight-refresh',
         'inflight-log', 'late-callback', 'wait-failure', 'thread-close-failure',
         'event-close-failure', 'wait-timeout', 'join-without-resource-lock',
         'wake-borrow', 'create-failures', 'blocked-remove')
PROFILES = (('Release', ['/O2', '/DNDEBUG', '/MT']),
            ('Checked', ['/Od', '/RTC1', '/D_DEBUG', '/D_ITERATOR_DEBUG_LEVEL=2', '/MTd']))
FORBIDDEN = ('windowsapp.dll', 'combase.dll', 'ole32.dll', 'rogetactivationfactory',
             'winmm.dll', 'user32.dll', 'win32u.dll', 'xinput', 'dinput8.dll', 'hid.dll',
             'setupapi.dll', 'steam_api', 'deviceiocontrol', 'sendinput', 'setsuspendstate')
# Deliberately wrong product variants must fail the actual consumer assertions.
MUTATIONS = (
    ('forget-callback-activity', '    Activity activity; // Must precede the stop check; count genuine in-flight callbacks.\n    if (!activity.allowed()) return;',
     '    if (stop.load(std::memory_order_acquire)) return;', 'inflight-callback'),
    ('unchecked-thread-close', '            if (!CloseHandle(thread)) return false;',
     '            (void)CloseHandle(thread);', 'thread-close-failure'),
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def save(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


def need(ok, message):
    if not ok:
        raise RuntimeError(message)


def execute(argv, cwd, env, log, build=False):
    start = time.monotonic()
    # Runtime has no kill-on-timeout. Fixture gates and waits have finite budgets;
    # all owned threads must finish and handles close before a PASS is emitted.
    result = subprocess.run([str(v) for v in argv], cwd=cwd, env=env, capture_output=True,
                            timeout=120 if build else None,
                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    text = result.stdout.decode('utf-8', errors='replace') + result.stderr.decode('utf-8', errors='replace')
    log.write_text(text, encoding='utf-8')
    return result.returncode, result.stdout.decode('utf-8', errors='replace'), round(time.monotonic()-start, 4)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-root',type=Path,default=DEFAULT)
    args=parser.parse_args()
    output=args.output_root.resolve(); output.mkdir(parents=True,exist_ok=True)
    run=output/(dt.datetime.now().strftime('r%Y%m%d-%H%M%S')+'-'+str(os.getpid())); run.mkdir()
    report={'status':'RUNNING','pass':False,'runDirectory':str(run),'profiles':[], 'mutations':[],
            'classification':'persistent observer: two static subscriptions, initialization thread and wake event',
            'scope':'Actual new header, fake WinRT factory/API/ABI tokens and real C++ delegates; owned real Win32 threads/events.',
            'limitations':[
                'waitMs budgets only waits. Activation, property, add/remove and COM Release may block; whole-call bounded shutdown is UNKNOWN.',
                'Busy owner lock returns false immediately, even with nonzero waitMs; retry after owner operation returns.',
                'Shutdown caller must already have initialized COM for actual WinRT. Fake tests do not prove apartment/agility behaviour.',
                'No forced cancellation, detach, timeout owner clearing or process termination is used as destruction evidence.',
                'No main.cpp integration, global registration, whole-product build, installation, export, device, driver, service or actual WinRT GameBar execution.',
                'Finite interleavings and local timing observations are regression evidence, not an exhaustive concurrency proof.',
                'Callbacks admitted before stop may already be inside the logger; Stopped remains false until their bodies return.',
                'A successfully removed provider may still retain a queued delegate: late delivery is inert, not proof that the external queue has drained.',
            ]}
    sources=[ROOT/HEADER,HERE/'fixture.cpp',HERE/'sdk_compile.cpp',Path(__file__).resolve(),ROOT/HELPER]
    pinned={str(p):sha(p.read_bytes()) for p in sources}; report['sourceHashes']=pinned
    snapshot=run/'source'; (snapshot/'native').mkdir(parents=True)
    for p in sources:
        relative=p.relative_to(ROOT); target=snapshot/relative; target.parent.mkdir(parents=True,exist_ok=True); shutil.copyfile(p,target)
    try:
        spec=importlib.util.spec_from_file_location('gamebar_existing_compiler_helper',ROOT/HELPER)
        helper=importlib.util.module_from_spec(spec); spec.loader.exec_module(helper)
        compiler,env=helper.compiler_environment(run)
        report['compiler']=str(compiler)
        dumpbin=compiler.with_name('dumpbin.exe')
        for profile,flags in PROFILES:
            folder=run/profile; folder.mkdir()
            cpp=snapshot/(HERE/'fixture.cpp').relative_to(ROOT); header=snapshot/HEADER
            exe=folder/'fixture.exe'; deps=folder/'dependencies.json'
            cmd=[compiler,'/nologo','/std:c++17','/utf-8','/EHsc','/W4','/WX','/wd4505','/permissive-',*flags,
                 '/sourceDependencies',deps,cpp,'/I'+str(header.parent),'/Fo'+str(folder/'fixture.obj'),
                 '/Fd'+str(folder/'fixture.pdb'),'/Fe'+str(exe),'/link','/INCREMENTAL:NO']
            code,_,_=execute(cmd,folder,env,folder/'compile.log',True)
            need(code==0,'Fixture compile failed: '+str(folder/'compile.log'))
            dependency=json.loads(deps.read_text(encoding='utf-8-sig'))['Data']
            includes=[Path(p) for p in dependency['Includes']]
            need(header.resolve() in [p.resolve() for p in includes],'Actual frozen product header absent from compiler dependencies')
            save(folder/'compiled-inputs.json',[{'path':str(p),'sha256':sha(p.read_bytes())} for p in [Path(dependency['Source']),*includes]])
            code,imports,_=execute([dumpbin,'/imports',exe],folder,env,folder/'imports.log',True)
            need(code==0,'dumpbin failed')
            need(not [s for s in FORBIDDEN if s in imports.lower()],'Forbidden runtime import found')
            sdk=snapshot/(HERE/'sdk_compile.cpp').relative_to(ROOT)
            sdkcmd=[compiler,'/nologo','/std:c++17','/utf-8','/EHsc','/W4','/WX','/wd4505','/permissive-',*flags,
                    '/c','/sourceDependencies',folder/'sdk-dependencies.json',sdk,'/I'+str(header.parent),
                    '/Fo'+str(folder/'sdk-compile-only.obj'),'/Fd'+str(folder/'sdk-compile-only.pdb')]
            code,_,_=execute(sdkcmd,folder,env,folder/'sdk-compile.log',True)
            need(code==0,'Real SDK compile-only probe failed: '+str(folder/'sdk-compile.log'))
            entry={'name':profile,'headerSha256':sha(header.read_bytes()),'executableSha256':sha(exe.read_bytes()),
                   'actualHeaderIncluded':True,'realSdkCompileOnly':True,'forbiddenImportsAbsent':True,'cases':[]}
            report['profiles'].append(entry)
            for case in CASES:
                code,text,seconds=execute([exe,case],folder,env,folder/(case+'.log'))
                observed=json.loads(text)
                entry['cases'].append({'name':case,'exit':code,'seconds':seconds,'observed':observed})
                need(code==0 and observed['failures']==0 and observed['remainingKernelHandles']==0 and
                     observed['remainingSubscriptions']==0 and observed['active']==0 and observed['fallbackCleanup']==0,'Case failed: '+profile+'/'+case)
            print(profile+': '+str(len(entry['cases']))+' cases, '+str(sum(c['observed']['checks'] for c in entry['cases']))+' checks',flush=True)
            # Both configurations also execute corruption controls against the same fixture.
            for name,old,new,case in MUTATIONS:
                mutation=folder/name; mutation.mkdir()
                mutant=mutation/'gamebar_trace_runtime.h'
                text=header.read_text(encoding='utf-8-sig'); need(text.count(old)==1,'Mutation anchor drift: '+name)
                mutant.write_text(text.replace(old,new,1),encoding='utf-8')
                mexe=mutation/'fixture.exe'
                mcmd=[compiler,'/nologo','/std:c++17','/utf-8','/EHsc','/W4','/WX','/wd4505','/permissive-',*flags,cpp,
                      '/I'+str(mutation),'/Fo'+str(mutation/'fixture.obj'),'/Fd'+str(mutation/'fixture.pdb'),'/Fe'+str(mexe),'/link','/INCREMENTAL:NO']
                code,_,_=execute(mcmd,mutation,env,mutation/'compile.log',True); need(code==0,'Mutation compile failed: '+name)
                code,text,seconds=execute([mexe,case],mutation,env,mutation/'run.log')
                observed=json.loads(text)
                report['mutations'].append({'profile':profile,'name':name,'case':case,'exit':code,'seconds':seconds,
                                            'detected':code!=0 and observed['failures']>0,'observed':observed})
                need(code!=0 and observed['failures']>0,'Unsafe mutation escaped consumer: '+name)
        need(all(sha(Path(p).read_bytes())==digest for p,digest in pinned.items()),'Task source changed during build/run')
        report.update(status='PASS',pass_=True)
        report['pass']=True; report.pop('pass_',None)
    except Exception as exc:
        report.update(status='FAIL',error=str(exc))
        print(str(exc),file=sys.stderr,flush=True)
    finally:
        report['sourceCurrentAtFinish']={p:Path(p).is_file() and sha(Path(p).read_bytes())==v for p,v in pinned.items()}
        save(run/'RESULT.json',report)
        save(output/'LATEST.json',{'runDirectory':str(run),'result':str(run/'RESULT.json'),'status':report['status']})
        print(str(run/'RESULT.json'),flush=True)
    return 0 if report['pass'] else 1

if __name__=='__main__':
    raise SystemExit(main())
