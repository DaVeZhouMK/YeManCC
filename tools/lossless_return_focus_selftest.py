#!/usr/bin/env python3
"""Compile actual native LS focus consumer + main.cpp entries against Fake OS.
No YMCC, LS, real windows/input, games, process control or hardware is exercised.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import shutil
import sys
from datetime import datetime, timezone

REPO = Path(__file__).resolve().parents[1]
NODE = Path(r'C:\Users\DaVe\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe')
VCVARS = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
MASK = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)

def masked(source):
    return MASK.sub(lambda m: ' ' * len(m.group()), source)

def braced(source, start):
    text = masked(source)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return source[start:end]

def function(source, name):
    match = re.search(r'\bstatic\s+[^;{}]*?\b' + re.escape(name) + r'\s*\([^;{}]*?\)\s*\{', masked(source))
    assert match, f'missing production function {name}'
    return braced(source, match.start())

def sha(data):
    return hashlib.sha256(data).hexdigest()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, default=REPO.parents[2] / 'Build/Validation/Lossless-ReturnFocus-20261022')
    args = parser.parse_args()
    out = args.output.resolve() / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ') + '-' + str(os.getpid()))
    out.mkdir(parents=True)
    source = (REPO / 'native/main.cpp').read_text('utf-8-sig')
    names = ['sgBaseName', 'focusFileTimeTicks', 'focusKnownOverlayName', 'focusBackgroundMonitorName',
             'focusWindowProcessName', 'focusIsKnownOverlayWindow', 'focusIsBackgroundMonitorWindow',
             'focusTargetIdentityMatches', 'focusIsRestorableExactWindow', 'focusResolveTarget',
             'resolvePreviousFocusWindow', 'focusForegroundMatchesTarget', 'refocusPreviousWindow',
             'focusReleaseOwnedTopmost', 'focusClearSession', 'focusPrepareSummonSession',
             'focusForegroundIsTransientOrShell', 'focusBeginReturnToPreviousWindow', 'ipc_cmd_async']
    blocks = {name: function(source, name) for name in names}
    types = []
    for name in ['FocusTargetSnapshot', 'FocusSessionState']:
        start = source.index('struct ' + name + ' {')
        types.append(braced(source, start) + ';')
    (out / 'production_focus_types.inc').write_text('\n'.join(types), 'utf-8')
    ipc_start = source.index('ipc_on("game.losslessReturnTarget",')
    ipc_body = braced(source, ipc_start) + ');'
    tick_start = source.index('if (w == RETURN_GAME_FOCUS_TIMER_ID) {')
    timer_body = braced(source, tick_start)
    power_start = source.index('case WM_POWERBROADCAST: {')
    power_match = re.search(r'if \(w == PBT_APMSUSPEND[^;]+focusLosslessReturnCancel\(\);', source[power_start:])
    assert power_match, 'missing production power cancellation'
    power_body = power_match.group()
    assert 'focusLosslessReturnCancel();' in function(source, 'beginAsyncExit')
    assert 'focusLosslessReturnInvalidate();' in function(source, 'nativeValveCommitLocked')
    assert 'focusLosslessReturnInvalidate();' in function(source, 'nativeValveClearLocked')
    prefix = '\n'.join(blocks[n] for n in names[:-2])
    production = prefix + '\n#include "lossless_return_focus.h"\n' + blocks[names[-2]] + '\n' + blocks[names[-1]]
    production += '\nstatic void registerLosslessIpc(){\n' + ipc_body + '\n}\n'
    production += '\nstatic void dispatchReturnTimer(){const WPARAM w=RETURN_GAME_FOCUS_TIMER_ID;\n' + timer_body + '\n}\n'
    production += '\nstatic void dispatchPowerCancel(WPARAM w){\n' + power_body + '\n}\n'
    (out / 'production_focus.inc').write_text(production, 'utf-8')
    inputs = ['native/lossless_return_focus.h', 'native/resume_focus.h', 'native/monitor_identity.h',
              'tools/lossless_return_focus_selftest.cpp', 'tools/lossless_return_focus_selftest.py', 'deps/json/json.hpp',
              'tools/fixtures/hc_reviewed_calibration/toolchain.mjs']
    snapshots = {p: (REPO / p).read_bytes() for p in inputs}
    for p, data in snapshots.items():
        if p.endswith('.h') or p.endswith('.hpp') or p.endswith('.cpp'):
            (out / Path(p).name).write_bytes(data)
    # Reuse the complete installed SDK selection from the existing native fixtures.
    toolchain = REPO / 'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'
    script = ('import {calibrationToolchainPreamble} from ' + json.dumps(toolchain.as_uri()) +
              '; process.stdout.write(calibrationToolchainPreamble(' + json.dumps(str(VCVARS)) + '));')
    preamble = subprocess.run([str(NODE), '--input-type=module', '-e', script], check=True, capture_output=True, text=True).stdout
    results = []
    for profile, flags in [('Release', ['/O2', '/DNDEBUG', '/MT']), ('Checked', ['/Od', '/RTC1', '/MTd'])]:
        exe = out / (profile + '.exe')
        command = ['cl', '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/W3', *flags,
                   str(out / 'lossless_return_focus_selftest.cpp'), '/I' + str(out),
                   '/Fo' + str(out / (profile + '.obj')), '/Fe' + str(exe), '/link', '/INCREMENTAL:NO', '/NODEFAULTLIB:user32.lib']
        cmd = out / (profile + '.cmd')
        cmd.write_text(preamble + subprocess.list2cmdline(command) + '\r\nif errorlevel 1 exit /b 1\r\n' +
                       f'dumpbin /symbols "{out / (profile + ".obj")}" > "{out / (profile + "-symbols.txt")}"\r\n' +
                       f'dumpbin /imports "{exe}" > "{out / (profile + "-imports.txt")}"\r\nexit /b %errorlevel%\r\n', 'utf-8')
        compile_result = subprocess.run(['cmd.exe', '/d', '/c', str(cmd)], cwd=out, capture_output=True, timeout=90)
        (out / (profile + '-compile.log')).write_bytes(compile_result.stdout + compile_result.stderr)
        if compile_result.returncode:
            print((compile_result.stdout + compile_result.stderr).decode('utf-8', errors='replace'))
            raise RuntimeError(f'{profile} compilation failed; {out}')
        imports = (out / (profile + '-imports.txt')).read_text(errors='replace').lower()
        forbidden = [v for v in ['user32.dll', 'win32u.dll', 'setforegroundwindow', 'sendinput', 'attachthreadinput', 'openprocess', 'createprocess'] if v in imports]
        # The static CRT itself imports TerminateProcess for fail-fast. Audit
        # fixture object references separately to exclude any product call.
        symbols = (out / (profile + '-symbols.txt')).read_text(errors='replace').lower()
        forbidden += [v for v in ['__imp_terminateprocess', '__imp_openprocess', '__imp_createprocessw'] if v in symbols]
        assert not forbidden, f'fixture must not import real UI/input/process operations: {forbidden}'
        execution = subprocess.run([str(exe)], cwd=out, capture_output=True, timeout=15)
        (out / (profile + '-run.log')).write_bytes(execution.stdout + execution.stderr)
        stdout = execution.stdout.decode('utf-8', errors='replace')
        print(stdout.splitlines()[-1] if execution.returncode == 0 else stdout)
        assert execution.returncode == 0, execution.stderr.decode('utf-8', errors='replace')
        count = re.search(r'production selftest: (\d+) checks PASS', stdout)
        assert count, 'fixture did not report completed check count'
        results.append({'profile': profile, 'checks': int(count.group(1)), 'pass': True, 'realUiInputProcessImports': forbidden})
    controls = []
    for name, filename, old, new, expected in [
        ('without-target-priority', 'production_focus.inc', 'if (focusLosslessReturnBegin(now)) return;',
         '(void)now;', 'double-B overrides LS snapshot'),
        ('without-three-request-limit', 'lossless_return_focus.h', 'state.attempts >= kLosslessReturnMaxAttempts',
         'false', 'denied foreground requests spend exactly three attempts'),
    ]:
        control = out / name
        control.mkdir()
        for item in out.iterdir():
            if item.is_file() and item.suffix in {'.h', '.hpp', '.cpp', '.inc'}:
                shutil.copyfile(item, control / item.name)
        changed = (control / filename).read_text('utf-8-sig')
        assert changed.count(old) == 1, 'negative control mutation must be exact'
        (control / filename).write_text(changed.replace(old, new), 'utf-8')
        exe = control / 'control.exe'
        cmd = control / 'compile.cmd'
        command = ['cl', '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/MT', '/O2', '/DNDEBUG',
                   str(control / 'lossless_return_focus_selftest.cpp'), '/I' + str(control),
                   '/Fo' + str(control / 'control.obj'), '/Fe' + str(exe), '/link', '/NODEFAULTLIB:user32.lib']
        cmd.write_text(preamble + subprocess.list2cmdline(command) + '\r\nexit /b %errorlevel%\r\n', 'utf-8')
        build = subprocess.run(['cmd.exe', '/d', '/c', str(cmd)], cwd=control, capture_output=True, timeout=90)
        (control / 'compile.log').write_bytes(build.stdout + build.stderr)
        assert build.returncode == 0, 'negative control must compile'
        execution = subprocess.run([str(exe)], cwd=control, capture_output=True, timeout=15)
        (control / 'run.log').write_bytes(execution.stdout + execution.stderr)
        assert execution.returncode == 1 and expected in execution.stderr.decode('utf-8', errors='replace'), 'negative control did not fail at the required regression'
        controls.append({'name': name, 'rejectedAtExpectedRegression': True})
        print('Negative control rejected: ' + name)
    current = (REPO / 'native/main.cpp').read_text('utf-8-sig')
    assert all(function(current, name) == body for name, body in blocks.items()), 'consumed main.cpp functions changed during run'
    assert braced(current, current.index('ipc_on("game.losslessReturnTarget",')) + ');' == ipc_body, 'IPC entry changed during run'
    assert braced(current, current.index('if (w == RETURN_GAME_FOCUS_TIMER_ID) {')) == timer_body, 'timer entry changed during run'
    assert power_body in current[current.index('case WM_POWERBROADCAST: {'):], 'power entry changed during run'
    assert all((REPO / p).read_bytes() == data for p, data in snapshots.items()), 'consumed source changed during run'
    receipt = {'pass': True, 'classification': 'one-shot',
               'classificationReason': 'static identity until return/cancel; timer exists only for a maximum 10-second return episode',
               'sourceHashes': {p: sha(data) for p, data in snapshots.items()},
               'functionHashes': {name: sha(body.encode()) for name, body in blocks.items()},
               'entryHashes': {'ipc': sha(ipc_body.encode()), 'timer': sha(timer_body.encode()), 'power': sha(power_body.encode())},
               'profiles': results, 'negativeControls': controls, 'realApplicationsOrHardwareRun': False}
    (out / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n', 'utf-8')
    (args.output.resolve() / 'latest.json').write_text(json.dumps({'pass': True, 'run': str(out)}, indent=2) + '\n', 'utf-8')
    print('Evidence: ' + str(out))
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
