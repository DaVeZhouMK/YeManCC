#!/usr/bin/env python3
"""Offline source-bound Win32 substitute tests; writes only the evidence subtree.

Run with Python on Windows + VS 2022 BuildTools. Both /W4 /WX profiles are
compiled from frozen actual headers and extracted main.cpp definitions. Import
and preprocessor audits must pass before either fixture executable is launched.
No product executable, real input, token, device, service, or registry is used.
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

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[1]
OUT = REPO.parents[2] / 'Build/Validation/KeyboardSelfSendBatchEvidence-20261016/tests/R3'
R2_FAILURE = OUT.parent / 'run-20261015T170650.843900Z-24204/result.json'
VCVARS = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
CORE_ONLY = '--core-only' in sys.argv[1:]
OOM_PROBE = next((int(arg.split('=', 1)[1]) for arg in sys.argv[1:] if arg.startswith('--oom-probe=')), 22)
CPP = 'tools/keyboard_self_send_batch_selftest.cpp'
PY = 'tools/keyboard_self_send_batch_selftest.py'
PARSER = 'tools/virtual_sample_tree_perf_selftest.py'
SIGNATURE = 'static UINT keyboardSelfSendWithEvidence('
ATOMIC_NAMES = ['g_keyboardSelfSendBatchSequence', 'g_keyboardSelfSendBatchFailures']
FUNCTION_SIGNATURES = {'record': 'static void keyboardSelfSendRecordEvidence(', 'flush': 'static void keyboardSelfSendFlushEvidence('}
APIS = {
    'GetLastError': 'getLastError', 'SetLastError': 'setLastError',
    'GetTickCount64': 'tick', 'GetCurrentProcessId': 'processId',
    'GetCurrentThreadId': 'threadId', 'GetCurrentProcess': 'processHandle',
    'GetCurrentThread': 'threadHandle', 'OpenProcessToken': 'openProcess',
    'OpenThreadToken': 'openThread', 'GetTokenInformation': 'tokenInformation',
    'CloseHandle': 'close', 'GetSidSubAuthorityCount': 'sidCount',
    'IsValidSid': 'validSid', 'GetSidSubAuthority': 'sidAuthority', 'SendInput': 'sendInput',
}
INPUTS = ['native/main.cpp', 'native/keyboard_self_send_evidence.h',
          'native/keyboard_modifier_event_evidence.h', 'deps/json/json.hpp', PARSER, CPP, PY]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def write(path, data):
    path = path.resolve()
    if not path.is_relative_to(OUT.resolve()):
        raise RuntimeError('write outside evidence directory: ' + str(path))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data.encode('utf-8') if isinstance(data, str) else data)


def save(path, value):
    write(path, json.dumps(value, ensure_ascii=False, indent=2) + '\n')


def parser(raw):
    module = ast.parse(raw.decode('utf-8-sig'))
    names = {'clean', 'match_brace', 'source_function'}
    functions = [node for node in module.body if isinstance(node, ast.FunctionDef) and node.name in names]
    assert {node.name for node in functions} == names, 'parser helper definitions changed'
    context = {'re': re}
    exec(compile(ast.Module(body=functions, type_ignores=[]), '<frozen-source-parser>', 'exec'), context)
    return context


def binding(raw, parse, header_raw):
    text = raw.decode('utf-8-sig')
    function = parse['source_function'](text, SIGNATURE)
    # A forward declaration cannot satisfy this signature + body check.
    declaration = function[:function.index('{')].strip()
    assert re.fullmatch(r'static UINT keyboardSelfSendWithEvidence\(ymcc::keyboardEvents::SourceId source,\s*UINT requested,\s*INPUT\* inputs\)', declaration), declaration
    blocks = {'wrapper': function}
    for name in ATOMIC_NAMES:
        matches = list(re.finditer(r'(?m)^static std::atomic<std::uint64_t>\s+' + name + r'\s*\{0\}\s*;', parse['clean'](text)))
        assert len(matches) == 1 and matches[0].end() < text.index(function), 'atomic definition missing/moved: ' + name
        match = matches[0]
        blocks[name] = text[match.start():match.end()]
    for name, signature in FUNCTION_SIGNATURES.items():
        blocks[name] = parse['source_function'](text, signature)
    queue = list(re.finditer(r'(?m)^static ymcc::selfSendEvidence::Queue g_keyboardSelfSendBatchQueue\s*;', parse['clean'](text)))
    assert len(queue) == 1 and queue[0].end() < text.index(function), 'queue definition missing/moved'
    blocks['g_keyboardSelfSendBatchQueue'] = text[queue[0].start():queue[0].end()]
    assert 'appendDomainLog(' not in blocks['wrapper'] + blocks['record'], 'sender must have no sink calls'
    assert not re.search(r'\b(?:json|nlohmann|toJson)\b', parse['clean'](blocks['wrapper'] + blocks['record'])), 'sender must use POD only'
    assert 'captureRequest(' in blocks['wrapper'] and 'captureContext(' in blocks['wrapper']
    assert re.search(r'keyboardSelfSendRecordEvidence\(const ymcc::selfSendEvidence::BatchRecord& row\) noexcept', blocks['record']), 'typed record signature changed'
    for name, signature in {'captureToken': 'inline TokenSnapshot captureToken(',
                            'captureContext': 'inline ContextSnapshot captureContext(',
                            'captureRequest': 'inline RequestSnapshot captureRequest('}.items():
        blocks[name] = parse['source_function'](header_raw.decode('utf-8-sig'), signature)
        assert not re.search(r'\b(?:json|nlohmann|toJson)\b', parse['clean'](blocks[name])), name + ' must use fixed storage'
    assert blocks['wrapper'].count('keyboardSelfSendRecordEvidence(') == 2
    assert 'appendDomainLog(' in blocks['flush']
    blocks['inputHostDisplayIdleAuditTick'] = parse['source_function'](text, 'static void inputHostDisplayIdleAuditTick(')
    tick = blocks['inputHostDisplayIdleAuditTick']
    assert tick.index('now-lastAudit<1000') < tick.index('keyboardSelfSendFlushEvidence(true,now)'), 'flush must follow existing 1Hz boundary'
    assert 'keyboardSelfSendFlushEvidence(false,now)' in tick
    assert len(re.findall(r'\bkeyboardSelfSendFlushEvidence\s*\(', parse['clean'](text))) == 4, 'flush callsite scope changed (definition + audit enabled/disabled + export)'
    blocks['refocusWebView'] = parse['source_function'](text, 'static bool refocusWebView(')
    blocks['nativeKeyboardShortcutSend'] = parse['source_function'](text, 'static void nativeKeyboardShortcutSend(')
    assert len(re.findall(r'\bkeyboardSelfSendWithEvidence\s*\(', parse['clean'](text))) == 4, 'wrapper callsite scope changed (definition + declaration + two producers)'
    assert 'SourceId::FocusAltFallback' in blocks['refocusWebView']
    assert 'SourceId::NativeKeyboardShortcut' in blocks['nativeKeyboardShortcutSend']
    return blocks


def run_command(run, name, command, timeout=120):
    save(run / (name + '-command.json'), command)
    completed = subprocess.run(command, cwd=run, capture_output=True, timeout=timeout)
    write(run / (name + '-stdout.txt'), completed.stdout)
    write(run / (name + '-stderr.txt'), completed.stderr)
    return completed


def batch(run, name, commands):
    path = run / (name + '.cmd')
    lines = ['@echo off', 'call "' + str(VCVARS) + '" >nul', 'if errorlevel 1 exit /b %errorlevel%']
    for command in commands:
        lines.extend([subprocess.list2cmdline([str(x) for x in command]), 'if errorlevel 1 exit /b %errorlevel%'])
    lines.append('exit /b 0')
    write(path, '\n'.join(lines) + '\n')
    p = run_command(run, name, ['cmd.exe', '/d', '/c', str(path)])
    assert p.returncode == 0, name + ' failed; see ' + str(run / (name + '-stdout.txt'))


def preprocess_audit(run, common, parse, blocks, frozen):
    path = run / 'fixture-preprocessed.cpp'
    batch(run, 'preprocess', [common + ['/DNDEBUG', '/MT', '/P', '/Fi' + str(path)]])
    text = path.read_text(encoding='utf-8-sig', errors='replace')
    wrapper = parse['source_function'](text, SIGNATURE)
    start = text.index('namespace ymcc::selfSendEvidence')
    opening = text.index('{', start)
    header = text[start:parse['match_brace'](text, opening) + 1]
    record = parse['source_function'](text, FUNCTION_SIGNATURES['record'])
    flush = parse['source_function'](text, FUNCTION_SIGNATURES['flush'])
    original = blocks['wrapper'] + blocks['record'] + blocks['flush'] + frozen['native/keyboard_self_send_evidence.h'].decode('utf-8-sig')
    relevant = wrapper + record + flush + header
    matches = []
    for api, replacement in APIS.items():
        assert re.search(r'\b' + api + r'\s*\(', original), 'expected production API missing: ' + api
        assert not re.search(r'\b' + api + r'\s*\(', relevant), 'unsubstituted API: ' + api
        assert 'fake::' + replacement in relevant, 'missing fake binding: ' + api
        matches.append({'api': api, 'boundTo': 'fake::' + replacement})
    for name in ['domainLogEnabled', 'appendDomainLog', 'keyboardModifierObserveSelfSend']:
        assert name + '(' in wrapper + record + flush, 'dependency changed: ' + name
        parse['source_function'](text, 'static ' + ('bool ' if name == 'domainLogEnabled' else 'void ') + name + '(')
    save(run / 'api-substitution-audit.json', {'pass': True, 'bindings': matches,
         'logGateSinkObserverProvidedByFixture': True, 'senderHasNoSinkCalls': True, 'senderUsesPodCaptureAndTypedQueue': True, 'preprocessedSha256': sha(path.read_bytes())})


def import_audit(run, name):
    imports = (run / (name + '-imports-stdout.txt')).read_text(encoding='utf-8', errors='replace')
    dependencies = (run / (name + '-dependents-stdout.txt')).read_text(encoding='utf-8', errors='replace')
    banned_dlls = ['user32.dll', 'advapi32.dll', 'win32u.dll', 'shell32.dll', 'ole32.dll', 'sechost.dll']
    banned_symbols = ['SendInput', 'OpenProcessToken', 'OpenThreadToken', 'GetTokenInformation',
                      'GetSidSubAuthorityCount', 'GetSidSubAuthority', 'IsValidSid', 'SetThreadToken',
                      'DuplicateToken', 'AdjustTokenPrivileges', 'GetAsyncKeyState', 'GetKeyboardState',
                      'keybd_event', 'SetWindowsHookExW', 'RegisterRawInputDevices']
    found = [x for x in banned_dlls if x in (imports + dependencies).lower()]
    found += [x for x in banned_symbols if re.search(r'\b' + re.escape(x) + r'\b', imports, re.I)]
    value = {'pass': not found, 'forbiddenMatches': found, 'forbiddenDlls': banned_dlls,
             'forbiddenSymbols': banned_symbols,
             'note': 'CRT/STL may import kernel32 thread/clock/identity primitives; production header/wrapper API calls are separately proven fake in api-substitution-audit.json.'}
    save(run / (name + '-import-audit.json'), value)
    assert value['pass'], 'unsafe executable imports; fixture will not run: ' + repr(found)


def input_hashes(result):
    out = []
    for row in result['cases']:
        before, sent = bytes.fromhex(row['inputHex']), bytes.fromhex(row['sentHex'])
        assert before == sent, 'input bytes changed: ' + row['name']
        out.append({'name': row['name'], 'requested': row['requested'], 'byteCount': len(before),
                    'inputSha256': sha(before), 'sentSha256': sha(sent)})
    return out


def main():
    run = OUT / ('run-R3-' + datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ') + '-' + str(os.getpid()))
    run.mkdir(parents=True)
    result = {'revision': 'R3-POD-sender', 'pass': False, 'run': str(run), 'profiles': [], 'sourceDrift': [],
              'productOrRealInputExecuted': False, 'coreOnly': CORE_ONLY, 'sourceEditsAllowed': [CPP, PY]}
    frozen = {}
    try:
        assert VCVARS.is_file(), 'VS 2022 BuildTools vcvars64.bat missing'
        frozen = {name: (REPO / name).read_bytes() for name in INPUTS}
        if R2_FAILURE.is_file():
            result['preservedR2FailureEvidence'] = {'path': str(R2_FAILURE), 'sha256': sha(R2_FAILURE.read_bytes())}
        for name, raw in frozen.items():
            write(run / 'frozen' / name, raw)
        parse = parser(frozen[PARSER])
        blocks = binding(frozen['native/main.cpp'], parse, frozen['native/keyboard_self_send_evidence.h'])
        result['sourceHashes'] = {name: {'sha256': sha(raw), 'bytes': len(raw)} for name, raw in frozen.items()}
        result['bindingHashes'] = {name: sha(text.encode('utf-8')) for name, text in blocks.items()}
        save(run / 'source-manifest.json', {'inputs': result['sourceHashes'], 'bindings': result['bindingHashes'],
             'extraction': 'AST-load only clean/match_brace/source_function; actual definition excluding forward declaration; verbatim same-name atomic definitions'})
        for name, text in blocks.items():
            write(run / 'extracted' / (name + '.inc'), text + '\n')
        write(run / 'actual_keyboard_self_send.inc', '\n\n'.join(blocks[name] for name in [*ATOMIC_NAMES, 'g_keyboardSelfSendBatchQueue', 'record', 'flush', 'wrapper']) + '\n')
        common = ['cl', '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/W4', '/WX', '/permissive-', '/Zc:__cplusplus',
                  str(run / 'frozen' / CPP), '/I' + str(run / 'frozen/native'),
                  '/I' + str(run / 'frozen/deps/json'), '/I' + str(run)]
        preprocess_audit(run, common, parse, blocks, frozen)
        print('API substitution audit passed; frozen wrapper and headers ready.', flush=True)
        corpus = None
        for name, flags in [('Release', ['/O2', '/DNDEBUG', '/MT']), ('Checked', ['/Od', '/RTC1', '/D_DEBUG', '/MTd'])]:
            print('Compiling ' + name + ' /W4 /WX ...', flush=True)
            exe = run / (name + '.exe')
            command = common + flags + ['/Fo' + str(run / (name + '.obj')), '/Fe' + str(exe),
                       '/Fd' + str(run / (name + '.pdb')), '/link', '/INCREMENTAL:NO',
                       '/NODEFAULTLIB:user32.lib', '/NODEFAULTLIB:advapi32.lib', '/NODEFAULTLIB:win32u.lib',
                       '/MAP:' + str(run / (name + '.map'))]
            batch(run, name + '-compile', [command])
            for audit in ['imports', 'dependents']:
                batch(run, name + '-' + audit, [['dumpbin', '/' + audit, str(exe)]])
            import_audit(run, name)
            p = run_command(run, name + '-fixture', [str(exe)] + (['--core-only'] if CORE_ONLY else []))
            save(run / (name + '-execution.json'), {'returnCode': p.returncode, 'stdoutBytes': len(p.stdout), 'stderrBytes': len(p.stderr)})
            assert p.stdout, name + ' fixture produced no JSON; exit code ' + str(p.returncode)
            row = json.loads(p.stdout.decode('utf-8'))
            save(run / (name + '-result.json'), row)
            result['profiles'].append({'name': name, 'pass': row['pass'], 'returnCode': p.returncode,
                'checks': row['checks'], 'cases': len(row['cases']), 'tokenCases': len(row['tokenCases']),
                'metadataAndLogExceptionScenarios': row.get('metadataAndLogExceptionScenarios'),
                'realJsonAllocationFaultSites': row.get('realJsonAllocationFaultSites', 0),
                'batchRecordBytes': row.get('batchRecordBytes'),
                'senderAllocationFaultCases': len(row.get('senderAllocationFaultCases', [])),
                'senderAllocationFaultOrdinals': row.get('senderAllocationFaultOrdinals'),
                'allNormalSenderAllocationCallsZero': all(x['allocations'] == 0 for x in row['cases']),
                'drainSinkExceptionScenarios': row.get('drainSinkExceptionScenarios'),
                'senderQueueLossScenarios': row.get('senderQueueLossScenarios'),
                'exeSha256': sha(exe.read_bytes())})
            assert p.returncode == 0 and row['pass'], name + ' fixture failed: ' + row.get('error', str(p.returncode))
            if not CORE_ONLY:
                assert row['batchRecordBytes'] <= 1024
                assert all(x['allocations'] == 0 for x in row['cases']), 'R3 normal sender allocated'
                assert row['senderAllocationFaultCases'] and all(
                    x['allocationCalls'] == 0 and not x['faultInjected'] and x['noInjectionPoint'] and
                    x['sendCalls'] == 1 and x['observerCalls'] == 1 for x in row['senderAllocationFaultCases']), 'R3 zero-allocation fault contract failed'
            hashes = input_hashes(row)
            save(run / (name + '-input-byte-hashes.json'), hashes)
            if corpus is None:
                corpus = hashes
            else:
                assert corpus == hashes, 'Release/Checked frozen INPUT corpus differs'
            print(name + ': PASS; ' + str(row['checks']) + ' checks; normal sender allocations=0; ' + str(len(row.get('senderAllocationFaultCases', []))) + ' fault-ordinal cases with no injection point.', flush=True)
            if OOM_PROBE is not None and not CORE_ONLY:
                probe = run_command(run, name + '-supplemental-oom', [str(exe), '--allocation-fault=' + str(OOM_PROBE)])
                detail = {'allocationOrdinal': OOM_PROBE, 'returnCode': probe.returncode,
                          'result': json.loads(probe.stdout.decode('utf-8')) if probe.stdout else None}
                save(run / (name + '-supplemental-oom.json'), detail)
                result.setdefault('supplementalOomProbes', []).append({'profile': name, **detail})
                print(name + ': supplemental OOM probe exit ' + str(probe.returncode) + '; evidence retained.', flush=True)
        result['inputCorpusSha256'] = sha(json.dumps(corpus, sort_keys=True).encode('utf-8'))
        result['testsPass'] = True
        probes = result.get('supplementalOomProbes', [])
        result['supplementalOomProbePass'] = all(
            x['returnCode'] == 0 and x['result'] and x['result'].get('pass') and
            x['result'].get('allocationCalls') == 0 and not x['result'].get('faultInjected') and x['result'].get('noInjectionPoint') and
            x['result'].get('sendCalls') == 1 and x['result'].get('observerCalls') == 1 for x in probes)
        result['pass'] = result['supplementalOomProbePass']
        if not result['supplementalOomProbePass']:
            result['productionRisk'] = 'R3 allocation-free sender contract failed; inspect supplemental probe evidence. No untriggered fault is counted as a caught exception.'
    except Exception as error:
        result['error'] = str(error)
        print('FAIL: ' + str(error), flush=True)
    finally:
        if frozen:
            result['currentSourceHashes'] = {}
            for name, raw in frozen.items():
                current = (REPO / name).read_bytes()
                result['currentSourceHashes'][name] = {'sha256': sha(current), 'bytes': len(current)}
                if current != raw:
                    drift = {'path': name, 'beforeSha256': sha(raw), 'afterSha256': sha(current), 'boundSourceChanged': True}
                    if name == 'native/main.cpp' and 'blocks' in locals():
                        try:
                            drift['boundSourceChanged'] = binding(current, parse, (REPO / 'native/keyboard_self_send_evidence.h').read_bytes()) != blocks
                        except Exception:
                            pass
                    result['sourceDrift'].append(drift)
                    if drift['boundSourceChanged']:
                        result['pass'] = False
            result['frozenInputsUnchanged'] = not result['sourceDrift']
            result['frozenEvidenceMatchesCurrentBoundSource'] = not any(x['boundSourceChanged'] for x in result['sourceDrift'])
        save(run / 'result.json', result)
        save(OUT / ('latest-core.json' if CORE_ONLY else 'latest.json'), {'run': str(run), 'pass': result['pass'], 'coreOnly': CORE_ONLY, 'testsPass': result.get('testsPass', False), 'supplementalOomProbePass': result.get('supplementalOomProbePass'), 'sourceDrift': result['sourceDrift']})
        print('Evidence: ' + str(run), flush=True)
    return 0 if result['pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
