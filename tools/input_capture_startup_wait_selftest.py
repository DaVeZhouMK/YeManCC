#!/usr/bin/env python3
"""MSVC Release/Checked unit of StartupWait, including two bounded mutations.

Run with local portablePython. Only private fixture processes/threads are run;
no product main, producer, device, Host, service, install, or system sleep.
Builds and receipts default to G:/YeManCC-Work/Build/CSW23.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

# Importing the existing compiler helper must not write __pycache__ in the repo.
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = Path('G:/YeManCC-Work/Build/CSW23')
HEADER = 'native/input_capture_startup_wait.h'
FIXTURE = 'tools/input_capture_startup_wait_selftest.cpp'
RUNNER = 'tools/input_capture_startup_wait_selftest.py'
HELPER = 'tools/input_timer_evidence_adapter_selftest.py'
PROFILES = (
    ('Release', ['/O2', '/DNDEBUG', '/MT']),
    ('Checked', ['/Od', '/RTC1', '/D_DEBUG', '/D_ITERATOR_DEBUG_LEVEL=2', '/MTd']),
)
MUTATIONS = (
    ('delete-notify', '        cv_.notify_all();',
     '        // Negative control: notification removed.',
     'stop-during-wait', 'stop-during-wait.prompt'),
    ('delete-predicate',
     '        return cv_.wait_for(lock, budget, [&stop] {\n'
     '            return stop.load(std::memory_order_acquire);\n'
     '        });',
     '        cv_.wait_for(lock, budget);\n'
     '        return stop.load(std::memory_order_acquire);',
     'stop-before-wait', 'stop-before-wait.prompt'),
)
FORBIDDEN_IMPORTS = (
    'winmm.dll', 'user32.dll', 'win32u.dll', 'xinput', 'dinput8.dll',
    'hid.dll', 'setupapi.dll', 'steam_api', 'deviceiocontrol', 'sendinput',
    'timesetevent', 'timekillevent', 'setsuspendstate',
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def need(condition, message):
    if not condition:
        raise RuntimeError(message)


def execute(command, directory, env, log, *, build=False):
    # Runtime intentionally has no process-killing timeout. Every C++ test gate
    # and actual wait has a finite budget, and ALL threads join before reporting,
    # including when the specified negative assertion fails. A killed process
    # can never count as a passing negative or as resource destruction evidence.
    result = subprocess.run(
        [str(item) for item in command], cwd=directory, env=env,
        capture_output=True, timeout=120 if build else None,
        creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    for stream in ('stdout', 'stderr'):
        raw = getattr(result, stream)
        try:
            text = raw.decode('utf-8')
        except UnicodeDecodeError:
            text = raw.decode('mbcs' if os.name == 'nt' else 'utf-8', errors='replace')
        setattr(result, stream, text)
    log.write_text(result.stdout + result.stderr, encoding='utf-8')
    return result


def compile_fixture(compiler, env, flags, directory, cpp, header):
    directory.mkdir(parents=True)
    exe = directory / 'wait.exe'
    dependencies = directory / 'dependencies.json'
    command = [compiler, '/nologo', '/std:c++17', '/utf-8', '/EHsc', '/W4', '/WX',
               '/permissive-', *flags, '/sourceDependencies', dependencies,
               cpp, '/I' + str(header.parent), '/Fo' + str(directory / 'wait.obj'),
               '/Fd' + str(directory / 'wait.pdb'), '/Fe' + str(exe),
               '/link', '/INCREMENTAL:NO']
    built = execute(command, directory, env, directory / 'compile.log', build=True)
    need(built.returncode == 0, 'Compilation failed: ' + str(directory / 'compile.log'))
    dep = json.loads(dependencies.read_text(encoding='utf-8-sig'))['Data']
    inputs = [Path(dep['Source']), *(Path(item) for item in dep['Includes'])]
    need(Path(dep['Source']).resolve() == cpp.resolve(), 'Unexpected fixture compilation input')
    need(header.resolve() in [item.resolve() for item in inputs], 'Actual header absent from compiler dependencies')
    receipt = [{'path': str(item), 'sha256': sha(item.read_bytes())} for item in inputs]
    save(directory / 'compiled-inputs.json', receipt)
    imports = execute([compiler.parent / 'dumpbin.exe', '/imports', exe],
                      directory, env, directory / 'imports.txt', build=True)
    need(imports.returncode == 0, 'dumpbin failed')
    forbidden = [item for item in FORBIDDEN_IMPORTS if item in imports.stdout.lower()]
    need(not forbidden, 'Unexpected control import: ' + repr(forbidden))
    return exe, {
        'buildArguments': [str(item) for item in command],
        'executableSha256': sha(exe.read_bytes()),
        'headerSha256': sha(header.read_bytes()),
        'fixtureSha256': sha(cpp.read_bytes()),
        'compiledInputs': len(inputs),
        'controlImportsAbsent': True,
    }


def run_fixture(exe, selected, directory, env):
    result = execute([exe, selected], directory, env, directory / 'run.log')
    observed = json.loads(result.stdout)
    save(directory / 'observations.json', observed)
    need(not observed['unexpectedException'], 'Fixture threw: ' + str(directory / 'run.log'))
    need(observed['allThreadsExplicitlyJoined'] and observed['fallbackJoins'] == 0 and
         observed['threadsStarted'] == observed['threadsJoined'],
         'Threads were not explicitly joined: ' + str(directory / 'run.log'))
    return result, observed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-root', type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    output = args.output_root.resolve()
    output.mkdir(parents=True, exist_ok=True)
    run = output / ('r' + dt.datetime.now().strftime('%m%d-%H%M%S') + '-' + str(os.getpid()))
    run.mkdir()
    receipt = {
        'pass': False, 'status': 'RUNNING', 'runDirectory': str(run),
        'python': {'executable': sys.executable, 'version': sys.version},
        'profiles': [],
        'scope': 'Actual StartupWait header + private std::thread; startup/stop boundaries only.',
        'resourceKind': 'Synchronization component with externally owned worker/lifecycle.',
        'limitations': [
            'requestStop/reset are bool noexcept; false reports failed mutex synchronization.',
            'requestStop always publishes stop=true and best-effort notifies even on lock failure.',
            'Fallback wake reliability is unproven; reset failure must prevent a new worker.',
            'No injected mutex/CV failure: production header has no test hook.',
            'requestStop takes a short mutex critical section; no hard scheduler/lock bound.',
            'Finite startup waits/gates and explicit joins cover these fixtures, not arbitrary blocking.',
            'Timing assertions cover local wait only, never a production Stop or destruction total budget.',
            'Reset requires externally serialized lifecycle: previous worker left, no new worker publication.',
            'During-wait handshake observes a pending call; no hook exposes exact CV enrollment.',
            'Finite interleavings are regression evidence, not exhaustive model checking.',
            'No full worker/Start/Stop/kernel consumer, installed product, device, service, or real sleep tested.',
        ],
        'runtimeCleanup': 'No detach, forced thread/process termination, or runtime timeout cleanup.',
    }
    try:
        need(os.name == 'nt', 'This runner requires local Windows MSVC')
        sources = {name: (ROOT / name).read_bytes() for name in (HEADER, FIXTURE, RUNNER, HELPER)}
        for name, data in sources.items():
            target = run / 's' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        receipt['sources'] = {name: sha(data) for name, data in sources.items()}
        # The helper was read and its main entry is guarded by __name__; importing
        # it defines compiler_environment without running its tests or product code.
        spec = importlib.util.spec_from_file_location('startup_wait_compiler_helper', run / 's' / HELPER)
        need(spec is not None and spec.loader is not None, 'Cannot load compiler helper')
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        compiler, env = helper.compiler_environment(run)
        receipt['compiler'] = {
            'path': str(compiler), 'sha256': sha(compiler.read_bytes()),
            'vcToolsVersion': env.get('VCToolsVersion'),
            'windowsSdkVersion': env.get('WindowsSDKVersion'),
        }
        header = run / 's' / HEADER
        cpp = run / 's' / FIXTURE
        text = sources[HEADER].decode('utf-8')
        mutants = []
        for name, old, new, selected, expected in MUTATIONS:
            need(text.count(old) == 1, 'Mutation target must be unique: ' + name)
            mutant = run / 'm' / name / 'input_capture_startup_wait.h'
            mutant.parent.mkdir(parents=True)
            mutant.write_text(text.replace(old, new, 1), encoding='utf-8', newline='\n')
            mutants.append((name, mutant, selected, expected))
        for profile, flags in PROFILES:
            directory = run / ('R' if profile == 'Release' else 'C')
            exe, build = compile_fixture(compiler, env, flags, directory / 'positive', cpp, header)
            result, observed = run_fixture(exe, 'all', directory / 'positive', env)
            need(result.returncode == 0 and observed['pass'] is True and observed['failures'] == 0,
                 profile + ' positive failed: ' + str(directory / 'positive' / 'run.log'))
            need(len(observed['cases']) == 8 and observed['threadsStarted'] == 129,
                 'Missing required cases or real threads')
            entry = {'profile': profile, 'pass': True, 'build': build,
                     'positive': observed, 'negativeControls': []}
            for name, mutant, selected, expected in mutants:
                target = directory / name
                neg_exe, neg_build = compile_fixture(compiler, env, flags, target, cpp, mutant)
                negative, findings = run_fixture(neg_exe, selected, target, env)
                failed = [check['name'] for case in findings['cases'] for check in case['checks']
                          if not check['pass']]
                need(negative.returncode == 1 and findings['pass'] is False and failed == [expected],
                     profile + ' negative must fail exactly at ' + expected + ': ' + repr(failed))
                entry['negativeControls'].append({
                    'mutation': name, 'expectedFailure': expected,
                    'rejectedAsExpected': True, 'exitCode': negative.returncode,
                    'build': neg_build, 'observations': findings,
                })
            receipt['profiles'].append(entry)
            print(profile + ': ' + str(observed['checks']) + ' checks; 129/129 threads joined; '
                  'delete-notify and delete-predicate rejected and joined', flush=True)
        need(all((ROOT / name).read_bytes() == data for name, data in sources.items()),
             'Unit inputs changed during verification; rerun against the current files')
        receipt['sourceInputsStable'] = True
        receipt['pass'] = True
        receipt['status'] = 'PASS'
    except Exception as error:
        receipt['status'] = 'FAIL'
        receipt['error'] = str(error)
    save(run / 'result.json', receipt)
    save(output / 'result.json', receipt)
    print(json.dumps({'pass': receipt['pass'], 'status': receipt['status'],
                      'result': str(output / 'result.json'), 'runDirectory': str(run),
                      **({'error': receipt['error']} if 'error' in receipt else {})}, ensure_ascii=False))
    return 0 if receipt['pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
