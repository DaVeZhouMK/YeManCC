#!/usr/bin/env python3
"""Frozen scalar-query fixture. Never executes live User32, COM, Shell or input.

Only generated test artifacts go under Build/Validation/TaskView-Foreground-Evidence-20261015.
Every sampled Win32 function is macro-replaced. Activation/input/privacy APIs are
unresolved link tripwires; compiled imports are audited before running each fake.
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

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[1]
OUT = REPO.parents[2] / 'Build/Validation/TaskView-Foreground-Evidence-20261015'
VCVARS = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
HEADER = 'native/task_view_foreground_evidence.h'
FIXTURE = 'tools/task_view_foreground_evidence_selftest.cpp'
RUNNER = 'tools/task_view_foreground_evidence_selftest.py'
INPUTS = [HEADER, FIXTURE, RUNNER, 'deps/json/json.hpp']
FAKES = {
    'GetLastError': 'error', 'SetLastError': 'setError', 'GetTickCount64': 'tick',
    'GetCurrentThreadId': 'currentThread', 'GetForegroundWindow': 'foreground',
    'GetWindowThreadProcessId': 'windowThread', 'GetGUIThreadInfo': 'gui',
}
TRIPWIRES = {
    'AllowSetForegroundWindow': 'allowForeground', 'SetForegroundWindow': 'setForeground',
    'SetFocus': 'setFocus', 'SetActiveWindow': 'setActive', 'BringWindowToTop': 'bringToTop',
    'AttachThreadInput': 'attachThread', 'SendInput': 'sendInput', 'keybd_event': 'keyEvent',
    'mouse_event': 'mouseEvent', 'GetWindowTextW': 'title', 'GetClassNameW': 'className',
    'GetWindowModuleFileNameW': 'moduleName', 'OpenProcess': 'openProcess',
    'QueryFullProcessImageNameW': 'imageName', 'IsWindowVisible': 'visible',
    'GetAsyncKeyState': 'asyncKey', 'GetKeyboardState': 'keyboard', 'GetLastInputInfo': 'lastInput',
    'Sleep': 'sleep', 'SleepEx': 'sleepEx', 'SetTimer': 'timer',
}
FORBIDDEN_IMPORTS = [
    'user32.dll', 'win32u.dll', 'ole32.dll', 'oleaut32.dll', 'combase.dll', 'shell32.dll',
    'allowsetforegroundwindow', 'setforegroundwindow', 'attachthreadinput',
    'getforegroundwindow', 'getwindowthreadprocessid', 'getguithreadinfo',
    'sendinput', 'keybd_event', 'mouse_event', 'getwindowtext', 'getclassname',
    'getwindowmodulefilename', 'openprocess', 'queryfullprocessimagename',
    'iswindowvisible', 'getasynckeystate', 'getkeyboardstate', 'getlastinputinfo',
    'createprocess', 'cocreateinstance', 'settimer',
]
PROFILES = [('Release', ['/O2', '/DNDEBUG', '/MT']), ('Checked', ['/Od', '/RTC1', '/D_DEBUG', '/MTd'])]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def write(path, data):
    path = Path(path).resolve()
    if not path.is_relative_to(OUT.resolve()):
        raise AssertionError(('write outside test artifact directory', str(path)))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data.encode('utf-8') if isinstance(data, str) else data)


def save(path, value):
    write(path, json.dumps(value, ensure_ascii=False, indent=2) + '\n')


def strip_cpp(text):
    return re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/',
                  lambda match: ''.join('\n' if char == '\n' else ' ' for char in match.group()), text)


def source_audit(sources):
    header = sources[HEADER].decode('utf-8-sig')
    fixture = sources[FIXTURE].decode('utf-8-sig')
    prefix = fixture[:fixture.index('#include "../native/task_view_foreground_evidence.h"')]
    for namespace, mapping in [('fake_os', FAKES), ('forbidden_os', TRIPWIRES)]:
        for api, replacement in mapping.items():
            if not re.search(r'(?m)^#define ' + re.escape(api) + r' ' + namespace + r'::' + replacement + r'\s*$', prefix):
                raise AssertionError(('missing macro fake/tripwire', api))
    code = strip_cpp(header)
    calls = set(re.findall(r'\b(?:Get\w+|Set\w+|Allow\w+|Send\w+|Create\w+|Open\w+|Query\w+|IsWindow\w*|Sleep\w*)\s*\(', code))
    calls = {re.sub(r'\s*\($', '', call) for call in calls}
    if calls != set(FAKES):
        raise AssertionError(('query allowlist changed', sorted(calls)))
    forbidden = r'\b(?:for|while|do|new|malloc|calloc|realloc|Sleep|SleepEx|SetTimer|SendInput|AllowSetForegroundWindow|SetForegroundWindow|AttachThreadInput|CreateThread|QueueUserWorkItem|GetWindowTextW|GetClassNameW|OpenProcess|QueryFullProcessImageNameW)\b|std::(?:thread|jthread|async|vector|map|function)'
    hits = re.findall(forbidden, code)
    if hits:
        raise AssertionError(('polling/allocation/side effect/privacy call in header', hits))
    scalar_code = code[:code.index('inline std::uint64_t hwndNumber')]
    capture_code = code[code.index('inline Snapshot capture('):code.index('inline nlohmann::json toJson(')]
    if re.search(r'\b(?:json|Json|string)\b', scalar_code + capture_code):
        raise AssertionError('allocation-capable type in scalar sampler')
    if 'if (!window.identityOk || !window.threadId) return;' not in code:
        raise AssertionError('unknown TID skip guard missing')
    if 'SetLastError(savedError);' not in capture_code or 'catch (...)' not in capture_code:
        raise AssertionError('LastError restoration/exception guard missing')
    return {'pass': True, 'macroQueryFakes': FAKES, 'unresolvedTripwires': TRIPWIRES,
            'sampledApiAllowlist': sorted(calls), 'pollingOrAuthorizationOrPrivacyCalls': False}


def compile_and_run(run, name, flags):
    exe = run / (name + '.exe')
    imports_file = run / (name + '-imports.txt')
    command = ['cl', '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/W4', '/WX', *flags,
               str(run / FIXTURE), '/I' + str(run / 'deps/json'),
               '/Fo' + str(run / (name + '.obj')), '/Fe' + str(exe),
               '/link', '/INCREMENTAL:NO', '/NODEFAULTLIB:user32.lib',
               '/NODEFAULTLIB:ole32.lib', '/NODEFAULTLIB:shell32.lib']
    batch = run / (name + '.cmd')
    write(batch, '@echo off\ncall "' + str(VCVARS) + '" >nul\nif errorlevel 1 exit /b %errorlevel%\n' +
          subprocess.list2cmdline(command) + '\nif errorlevel 1 exit /b %errorlevel%\n' +
          'dumpbin /imports "' + str(exe) + '" > "' + str(imports_file) + '"\nexit /b %errorlevel%\n')
    compiled = subprocess.run(['cmd.exe', '/d', '/c', str(batch)], cwd=run, capture_output=True,
                              encoding='utf-8', errors='replace', timeout=120)
    write(run / (name + '-compile.log'), compiled.stdout + compiled.stderr)
    if compiled.returncode:
        raise AssertionError('compile failed: ' + str(run / (name + '-compile.log')))
    imports = imports_file.read_text(errors='replace').lower()
    matches = [token for token in FORBIDDEN_IMPORTS if token in imports]
    audit = {'pass': not matches, 'forbiddenMatches': matches, 'forbiddenImports': FORBIDDEN_IMPORTS,
             'executableSha256': sha(exe.read_bytes())}
    save(run / (name + '-import-audit.json'), audit)
    if matches:
        raise AssertionError(('unsafe imports; executable was not run', matches))
    executed = subprocess.run([str(exe)], cwd=run, capture_output=True, encoding='utf-8',
                              errors='replace', timeout=15)
    write(run / (name + '-stdout.json'), executed.stdout)
    write(run / (name + '-stderr.txt'), executed.stderr)
    if executed.returncode:
        raise AssertionError('fixture failed: ' + executed.stderr)
    result = json.loads(executed.stdout)
    if result.get('pass') is not True or result.get('captureAllocationAttempts') != 0:
        raise AssertionError(('fixture result failed', result))
    print(name + ': ' + str(result['checks']) + ' checks, ' + str(len(result['cases'])) + ' case groups', flush=True)
    return {'profile': name, 'importAudit': audit, **result}


def main():
    run = OUT / ('run-' + datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ') + '-' + str(os.getpid()))
    result = {'pass': False, 'run': str(run), 'profiles': [], 'realUser32Executed': False,
              'realShellOrInputExecuted': False, 'fieldAcceptance': False}
    try:
        sources = {name: (REPO / name).read_bytes() for name in INPUTS}
        result['sourceHashes'] = {name: sha(data) for name, data in sources.items()}
        result['sourceAudit'] = source_audit(sources)
        if not VCVARS.is_file():
            raise FileNotFoundError('MSVC environment missing: ' + str(VCVARS))
        for name, data in sources.items():
            write(run / name, data)
        for name, flags in PROFILES:
            result['profiles'].append(compile_and_run(run, name, flags))
        if any((REPO / name).read_bytes() != data for name, data in sources.items()):
            raise AssertionError('sidecar input changed during testing')
        result['pass'] = True
    except Exception as error:
        result['error'] = str(error)
    save(run / 'result.json', result)
    print('Evidence: ' + str(run), flush=True)
    if not result['pass']:
        print(result.get('error', 'selftest failed'), file=sys.stderr, flush=True)
    return 0 if result['pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
