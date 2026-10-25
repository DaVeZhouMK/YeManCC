#!/usr/bin/env python3
"""Offline action-evidence regression: frozen sources, fake Win32, real IPC lambdas.

Writes only under Build/Validation/CtrlAlt-TaskView-ActionTrace-20261015/tests.
Runs standalone Release/Checked fixtures, never the product or live input APIs.
nativeSendTaskView is a command-contract fake; system API helper/timeout behavior
is outside this fixture, regardless of the production producer implementation.
The existing source_function parser is loaded as three AST-selected definitions;
no candidate producer, parser module entry point, or product initializer executes.
"""
from __future__ import annotations

import ast
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
OUT = REPO.parents[2] / 'Build/Validation/CtrlAlt-TaskView-ActionTrace-20261015/tests'
VCVARS = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
PARSER = 'tools/virtual_sample_tree_perf_selftest.py'
FIXTURE = 'tools/task_view_action_evidence_selftest.cpp'
RUNNER = 'tools/task_view_action_evidence_selftest.py'
INPUTS = [
    'native/task_view_action_evidence.h',
    'native/keyboard_modifier_snapshot.h',
    'native/main.cpp',
    'deps/json/json.hpp',
    PARSER, FIXTURE, RUNNER,
]
PROFILES = [('Release', ['/O2', '/DNDEBUG', '/MT']),
            ('Checked', ['/Od', '/RTC1', '/D_DEBUG', '/MTd'])]
FORBIDDEN_IMPORTS = [
    'user32.dll', 'win32u.dll', 'sendinput', 'getasynckeystate',
    'getkeyboardstate', 'getforegroundwindow', 'getwindowthreadprocessid',
    'postmessagew', 'keybd_event', 'mouse_event', 'setwindowshookex',
]
SNAPSHOT_APIS = ['GetLastError', 'SetLastError', 'GetTickCount64', 'GetCurrentThreadId',
                 'GetForegroundWindow', 'GetWindowThreadProcessId', 'GetAsyncKeyState', 'GetKeyboardState']
SIGNATURES = [
    ('sink', 'static void taskViewActionEvidenceSink('),
    ('begin', 'static ymcc::taskViewEvidence::Context taskViewActionEvidenceBegin('),
    ('end', 'static void taskViewActionEvidenceEnd('),
    ('minimize', 'ipc_on("window.minimize",'),
    ('taskView', 'ipc_on("shell.taskView",'),
]


def sha(data):
    return hashlib.sha256(data.encode('utf-8') if isinstance(data, str) else data).hexdigest()


def write(path, data):
    path = Path(path).resolve()
    if not path.is_relative_to(OUT.resolve()):
        raise AssertionError(('write outside authorized evidence output', str(path)))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data.encode('utf-8') if isinstance(data, str) else data)


def save(path, value):
    write(path, json.dumps(value, ensure_ascii=False, indent=2) + '\n')


def parser_functions(data):
    text = data.decode('utf-8-sig')
    module = ast.parse(text, filename=PARSER)
    wanted = ['clean', 'match_brace', 'source_function']
    selected = [node for node in module.body if isinstance(node, ast.FunctionDef) and node.name in wanted]
    if [node.name for node in selected] != wanted:
        raise AssertionError('existing source_function parser dependencies changed')
    namespace = {'re': re}
    exec(compile(ast.Module(body=selected, type_ignores=[]), PARSER, 'exec'), namespace)
    snapshot = '\n\n'.join(ast.get_source_segment(text, node) for node in selected) + '\n'
    return namespace, snapshot


def extract(main_bytes, parser):
    text = main_bytes.decode('utf-8-sig')
    blocks = {name: parser['source_function'](text, signature) for name, signature in SIGNATURES}
    declaration = re.findall(r'(?m)^static ymcc::taskViewEvidence::Recorder g_taskViewActionEvidence;\s*$', text)
    if len(declaration) != 1:
        raise AssertionError(('recorder declaration count', len(declaration)))
    blocks['recorder'] = declaration[0].strip()
    for name in ('minimize', 'taskView'):
        # source_function ends at the lambda's closing brace; verify and retain
        # the actual registration suffix before placing it in a fixture wrapper.
        signature = dict(SIGNATURES)[name]
        start = text.index(signature)
        after = text[start + len(blocks[name]):]
        if not re.match(r'\s*\);', after):
            raise AssertionError(('IPC registration suffix changed', name))
    manifest = re.findall(r'(?m)^\s*manifest << "taskViewActionEvidence[^\n]*$', text)
    if len(manifest) != 3 or not any('taskViewActionEvidenceVersion=2' in line for line in manifest):
        raise AssertionError(('action-evidence manifest declarations changed', manifest))
    blocks['manifest'] = '\n'.join(line.strip() for line in manifest)
    generated = '// Exact production helpers and IPC lambdas; original producers are memory fakes.\n'
    generated += '\n\n'.join(blocks[name] for name in ('recorder', 'sink', 'begin', 'end'))
    generated += '\n\nstatic void registerTaskViewExtractedHandlers() {\n'
    generated += '\n'.join(blocks[name] + ');' for name in ('minimize', 'taskView')) + '\n}\n'
    return blocks, generated


def source_audit(sources, parser, blocks):
    cpp = sources[FIXTURE].decode('utf-8-sig')
    header = sources['native/task_view_action_evidence.h'].decode('utf-8-sig')
    snapshot = sources['native/keyboard_modifier_snapshot.h'].decode('utf-8-sig')
    include = cpp.index('#include "../native/task_view_action_evidence.h"')
    mappings = {}
    for api in SNAPSHOT_APIS:
        match = re.search(r'(?m)^#define ' + api + r' (fixture::\w+)$', cpp[:include])
        if not match:
            raise AssertionError(('snapshot API is not replaced before production include', api))
        mappings[api] = match.group(1)
    # Static guard complements the actual concurrent cases and import audit.
    production = parser['clean'](header + '\n' + snapshot + '\n' + '\n'.join(blocks.values()))
    forbidden = r'\b(?:SendInput|keybd_event|mouse_event|keyboardPulse|keyboard_owned_pulse|WaitForSingleObject|Sleep|SleepEx|mutex|recursive_mutex|lock_guard|unique_lock)\b'
    hits = re.findall(forbidden, production)
    if hits:
        raise AssertionError(('unexpected input producer, shared owner, wait or lock in diagnostic scope', hits))
    return {'pass': True, 'snapshotApiReplacements': mappings,
            'scopeHasNoInputInjectionOrSharedOwnerLock': True,
            'mainProducerNotExtracted': True, 'exactMainHelpersAndIpcLambdasExtracted': True,
            'manifestLines': blocks['manifest'].splitlines()}


def invoke_compile(run, name, flags):
    exe = run / (name + '.exe')
    imports_path = run / (name + '-imports.txt')
    args = ['cl', '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/W4', '/WX', '/Bv', *flags,
            str(run / 'source' / FIXTURE), '/I' + str(run / 'source/deps/json'),
            '/I' + str(run), '/Fo' + str(run / (name + '.obj')),
            '/Fd' + str(run / (name + '-compiler.pdb')), '/Fe' + str(exe),
            '/link', '/INCREMENTAL:NO', '/NODEFAULTLIB:user32.lib', '/NODEFAULTLIB:win32u.lib']
    batch = '@echo off\ncall "' + str(VCVARS) + '" >nul\nif errorlevel 1 exit /b %errorlevel%\n'
    batch += subprocess.list2cmdline(args) + '\nif errorlevel 1 exit /b %errorlevel%\n'
    batch += 'dumpbin /imports "' + str(exe) + '" > "' + str(imports_path) + '"\nexit /b %errorlevel%\n'
    command_file = run / (name + '.cmd')
    write(command_file, batch)
    env = os.environ.copy()
    temp = run / 'temp'
    temp.mkdir(exist_ok=True)
    env.update(TMP=str(temp), TEMP=str(temp), PYTHONDONTWRITEBYTECODE='1')
    process = subprocess.run(['cmd.exe', '/d', '/c', str(command_file)], cwd=run, env=env,
                             capture_output=True, encoding='utf-8', errors='replace', timeout=120)
    write(run / (name + '-compile.log'), process.stdout + process.stderr)
    profile = {'profile': name, 'flags': flags, 'compileExitCode': process.returncode,
               'pass': False, 'executableRun': False}
    if process.returncode:
        profile['error'] = 'fixture compilation/import dump failed; see compile log'
        return profile
    imports = imports_path.read_text(encoding='utf-8', errors='replace').lower()
    banned = [symbol for symbol in FORBIDDEN_IMPORTS if symbol in imports]
    import_audit = {'pass': not banned, 'forbiddenMatches': banned,
                    'forbiddenSymbols': FORBIDDEN_IMPORTS,
                    'dlls': re.findall(r'(?m)^\s+([\w.-]+\.dll)\s*$', imports),
                    'sha256': sha(imports_path.read_bytes())}
    save(run / (name + '-import-audit.json'), import_audit)
    profile['importAudit'] = import_audit
    if banned:
        profile['error'] = 'unsafe fixture imports; executable was not run'
        return profile
    # Execution is conditional on the import audit; only the memory fixture runs.
    process = subprocess.run([str(exe)], cwd=run, env=env, capture_output=True,
                             encoding='utf-8', errors='replace', timeout=40)
    write(run / (name + '-result.json'), process.stdout)
    write(run / (name + '-stderr.txt'), process.stderr)
    profile.update(executableRun=True, testExitCode=process.returncode)
    try:
        result = json.loads(process.stdout)
        profile['test'] = result
        profile['failedCases'] = [case for case in result.get('cases', []) if not case.get('pass')]
        profile['pass'] = process.returncode == 0 and result.get('pass') is True
    except (ValueError, TypeError) as error:
        profile['error'] = 'invalid fixture result: ' + str(error)
    return profile


def main():
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    run = OUT / ('run-' + stamp + '-' + str(os.getpid()))
    run.mkdir(parents=True)
    result = {'pass': False, 'run': str(run), 'startedUtc': stamp, 'profiles': [],
              'actualOSInput': False, 'productExecuted': False,
              'commandWrapperContractOnly': True, 'realInputProducerTested': False,
              'systemApiHelperOrTimeoutTested': False,
              'writesOutsideEvidenceByRunner': False,
              'scope': 'Frozen recorder, modifier header, actual main helpers and two IPC lambdas; memory-only Win32 and action fakes.'}
    sources = {}
    try:
        if not VCVARS.is_file():
            raise FileNotFoundError(str(VCVARS))
        sources = {name: (REPO / name).read_bytes() for name in INPUTS}
        for name, data in sources.items():
            write(run / 'source' / name, data)
        result['sourceHashes'] = {name: sha(data) for name, data in sources.items()}
        parser, parser_snapshot = parser_functions(sources[PARSER])
        write(run / 'source_function.snapshot.py', parser_snapshot)
        blocks, generated = extract(sources['native/main.cpp'], parser)
        write(run / 'task_view_action_evidence_main.inc', generated)
        result['extractedHashes'] = {name: sha(text) for name, text in blocks.items()}
        result['parserFunctionsSha256'] = sha(parser_snapshot)
        save(run / 'dependencies.json', {'sourceHashes': result['sourceHashes'],
                                        'extractedHashes': result['extractedHashes'],
                                        'parserFunctionsSha256': result['parserFunctionsSha256']})
        result['sourceAudit'] = source_audit(sources, parser, blocks)
        save(run / 'source-audit.json', result['sourceAudit'])
        for name, flags in PROFILES:
            try:
                profile = invoke_compile(run, name, flags)
            except Exception as error:
                write(run / (name + '-runner-error.txt'), traceback.format_exc())
                profile = {'profile': name, 'pass': False, 'error': str(error)}
            result['profiles'].append(profile)
            checks = profile.get('test', {}).get('checks', 0)
            print(name + ': ' + ('PASS' if profile['pass'] else 'FAIL') + ', checks=' + str(checks), flush=True)
        # Other agents may edit unrelated main.cpp sections. Compare the exact
        # tested blocks as well as recording the complete before/after hashes.
        current = {name: (REPO / name).read_bytes() for name in INPUTS}
        result['currentSourceHashes'] = {name: sha(data) for name, data in current.items()}
        result['filesChangedDuringRun'] = [name for name in INPUTS if current[name] != sources[name]]
        current_blocks, _ = extract(current['native/main.cpp'], parser)
        required_stable = [name for name in INPUTS if name != 'native/main.cpp']
        stable = all(current[name] == sources[name] for name in required_stable) and current_blocks == blocks
        result['testedDependenciesStableThroughRun'] = stable
        result['pass'] = stable and all(profile['pass'] for profile in result['profiles'])
        if not stable:
            result['error'] = 'tested dependency changed during run; frozen evidence retained, rerun required'
        elif not result['pass']:
            result['error'] = '; '.join(profile['profile'] + ': ' + profile.get('error', 'fixture assertions failed') for profile in result['profiles'] if not profile['pass'])
    except Exception as error:
        result['error'] = str(error)
        write(run / 'runner-error.txt', traceback.format_exc())
    result['artifacts'] = {str(path.relative_to(run)).replace('\\', '/'): sha(path.read_bytes())
                           for path in sorted(run.rglob('*')) if path.is_file()}
    save(run / 'result.json', result)
    save(OUT / 'latest.json', result)
    print(json.dumps({'pass': result['pass'], 'run': str(run), 'error': result.get('error')}, ensure_ascii=False), flush=True)
    return 0 if result['pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
