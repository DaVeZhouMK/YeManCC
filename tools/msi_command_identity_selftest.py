#!/usr/bin/env python3
"""Extract actual MSI command/admission/return bodies and run fake-boundary fixtures.

Writes only the requested output directory (default: this R53 validation lane).
Never defines YMCC_FAULT_INJECT, loads main.cpp as an application, or calls HID.
"""
import argparse
import datetime
import hashlib
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
WORK = ROOT.parents[2]
LANE = WORK / 'Build/Validation/VirtualInputPerf-R53-20261015'
EXPECTED_FIXED_SHA = 'ad4c4f8a648600a3de6d1d776f3abea6c689d0842b67e2d921cbcdd3b95c4cd4'
TYPES = [
    ('struct', 'MsiClawHidDiag'), ('enum class', 'MsiClawIoState'),
    ('struct', 'MsiClawWriteResult'), ('struct', 'MsiClawCommandOutcome'),
    ('struct', 'MsiModeReadbackFacts'), ('struct', 'MsiMode1WritebackVerdict'),
    ('enum class', 'MsiReturnDebtBasis'),
]
MAIN_FUNCTIONS = [
    'ymStartSeamActive', 'msiClawKnownBoardAuthority', 'msiPathMentionsVendor',
    'msiClawOpenCommandHid', 'msiClawIoStateName', 'msiModeReadbackFacts',
    'msiMode1TerminalReason', 'msiMode1WritebackVerdict', 'msiReturnDebtDischarge',
    'msiReturnDebtBasisName',
]
RUNTIME_FUNCTIONS = [
    'msiCrashRecoveryPath', 'msiCrashBoardKey', 'msiCrashProcessCreation',
    'msiCrashCurrentOwner', 'msiCrashHasDuty', 'msiCrashStoreLocked',
    'msiCrashLocations', 'msiCrashPrepareOwner', 'msiCrashRecoveryBeforeModeWrite',
    'msiCrashRecoveryAfterModeDischarge',
]
TAIL_FUNCTIONS = [
    'msiDinputTryDischargeReturnDebt', 'msiClawOpenModeCommand',
    'msiClawCloseModeCommand', 'msiClawSwitchModeOnHandle', 'msiClawSwitchMode',
    'msiClawRequestDInputMode', 'msiClawFinishXinputRestore', 'msiClawRestoreXInputMode',
]


def digest(b):
    return hashlib.sha256(b).hexdigest()


def masked_cpp(text):
    """Retain byte positions/newlines while masking comments and C++ literals."""
    result = list(text)
    pos = 0
    while pos < len(text):
        end = None
        if text.startswith('//', pos):
            end = text.find('\n', pos)
            if end < 0:
                end = len(text)
        elif text.startswith('/*', pos):
            end = text.find('*/', pos + 2)
            if end < 0:
                raise ValueError('unterminated comment')
            end += 2
        elif text.startswith('R"', pos):
            opening = text.find('(', pos + 2, pos + 19)
            if opening >= 0:
                delimiter = ')' + text[pos + 2:opening] + '"'
                end = text.find(delimiter, opening + 1)
                if end < 0:
                    raise ValueError('unterminated raw string')
                end += len(delimiter)
        elif text[pos] in '\"\'':
            quote = text[pos]
            end = pos + 1
            while end < len(text):
                if text[end] == '\\':
                    end += 2
                elif text[end] == quote:
                    end += 1
                    break
                else:
                    end += 1
        if end is None:
            pos += 1
        else:
            for i in range(pos, end):
                if result[i] not in '\r\n':
                    result[i] = ' '
            pos = end
    return ''.join(result)


def extract(text, mask, name, kind=None):
    if kind:
        pattern = r'(?m)^' + re.escape(kind) + r'\s+' + re.escape(name) + r'\s*\{'
    else:
        pattern = r'(?m)^static\s+[^;{}\n]*\b' + re.escape(name) + r'\s*\([^;{}]*?\)\s*(?:noexcept\s*)?\{'
    matches = list(re.finditer(pattern, mask))
    if len(matches) != 1:
        raise ValueError(f'expected exactly one definition of {name}, found {len(matches)}')
    match = matches[0]
    depth = 1
    end = match.end()
    while depth:
        if mask[end] == '{':
            depth += 1
        elif mask[end] == '}':
            depth -= 1
        end += 1
    if kind:
        if text[end] != ';':
            raise ValueError(f'missing declaration semicolon: {name}')
        end += 1
    exact = text[match.start():end]
    return exact, {'symbol': name, 'startLine': text.count('\n', 0, match.start()) + 1,
                   'endLine': text.count('\n', 0, end) + 1,
                   'extractedUtf8Sha256': digest(exact.encode('utf-8')),
                   'exactSourceSubstringVerified': text[match.start():end] == exact}


def code_tokens(text):
    # Keep literals as tokens and ignore only comments/whitespace, for the narrow
    # proof that selected opener behavior changed by the one authorized statement.
    pat = r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|0[xX][0-9a-fA-F]+|\d+|[^\s]'
    return [m.group() for m in re.finditer(pat, text, re.S)
            if not m.group().startswith(('//', '/*'))]


def save_json(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--current-only', action='store_true', help='Run current admission/ownership gates without claiming the historical one-statement opener comparison')
    parser.add_argument('--old-source', type=pathlib.Path,
                        default=LANE / 'selfheal-backup/main-before-command-identity-fix.cpp')
    parser.add_argument('--current-source', type=pathlib.Path, default=ROOT / 'native/main.cpp')
    parser.add_argument('--expected-current-sha', default='', help='Optional full-file capture pin; extracted opener is always checked for the exact one-statement fix')
    parser.add_argument('--output-dir', type=pathlib.Path, default=LANE / 'msi-command-identity')
    parser.add_argument('--vcvars', type=pathlib.Path,
                        default=pathlib.Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'))
    args = parser.parse_args()
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    output = args.output_dir.resolve() / ('run-' + stamp)
    output.mkdir(parents=True)
    runtime_path = ROOT / 'native/msi_crash_recovery_runtime.h'
    journal_path = ROOT / 'native/msi_crash_recovery_journal.h'
    fixture_path = ROOT / 'tools/msi_command_identity_selftest.cpp'
    json_path = ROOT / 'deps/json/json.hpp'
    if not args.vcvars.is_file():
        raise FileNotFoundError(args.vcvars)
    main_inputs = [args.current_source] if args.current_only else [args.old_source, args.current_source]
    raw = {str(p): p.read_bytes() for p in [*main_inputs, runtime_path, journal_path, fixture_path, json_path, pathlib.Path(__file__).resolve()]}
    current_hash = digest(raw[str(args.current_source)])
    if args.expected_current_sha and current_hash != args.expected_current_sha:
        raise ValueError(f'current source hash mismatch: {current_hash}; expected {args.expected_current_sha}')
    runtime = raw[str(runtime_path)].decode('utf-8-sig')
    runtime_mask = masked_cpp(runtime)
    source_manifest = {'schemaVersion': 1, 'runDirectory': str(output),
                       'capturedAtUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                       'sources': [{'path': p, 'rawSha256': digest(b), 'bytes': len(b)} for p, b in raw.items()],
                       'variants': {}, 'faultInjectionDefined': False, 'parentReportedFixedSha256': EXPECTED_FIXED_SHA, 'capturedMainMatchesEarlierParentFullFileHash': current_hash == EXPECTED_FIXED_SHA,
                       'boundaryScope': ['SetupAPI enumeration and handles', 'HID attributes/caps',
                                         'USB root and location queries', 'process identity/death facts',
                                         'in-memory atomic persistence', 'bounded HID transport receipt',
                                         'epoch/power/exit/target-absence facts', 'diagnostic sinks'],
                       'preservedGates': ['actual known-board authority and request wrappers',
                                          'actual endpoint VID/PID/usage selection',
                                          'actual root/location/owner and journal validation',
                                          'actual write-ahead persistence and post-store context checks',
                                          'actual mode1 terminal/route/duty checks and discharge decisions'],
                       'realDeviceAcceptanceProven': False}
    openers = {}
    extracted_blocks = {}
    generated_hashes = {}
    builds = []
    variants = [('current', args.current_source, False)] if args.current_only else [('old', args.old_source, True), ('current', args.current_source, False)]
    for variant, path, expect_old in variants:
        directory = output / variant
        directory.mkdir()
        text = raw[str(path)].decode('utf-8-sig')
        mask = masked_cpp(text)
        declarations, bodies, records = [], [], []
        extracted_blocks[variant] = {}
        for kind, name in TYPES:
            code, record = extract(text, mask, name, kind)
            declarations.append(code)
            extracted_blocks[variant][name] = code
            records.append(dict(record, source=str(path), role='declaration'))
        for name in MAIN_FUNCTIONS:
            code, record = extract(text, mask, name)
            bodies.append(code)
            extracted_blocks[variant][name] = code
            records.append(dict(record, source=str(path), role='function'))
            if name == 'msiClawOpenCommandHid':
                openers[variant] = code
        for name in RUNTIME_FUNCTIONS:
            code, record = extract(runtime, runtime_mask, name)
            bodies.append(code)
            extracted_blocks[variant][name] = code
            records.append(dict(record, source=str(runtime_path), role='function'))
        for name in TAIL_FUNCTIONS:
            code, record = extract(text, mask, name)
            bodies.append(code)
            extracted_blocks[variant][name] = code
            records.append(dict(record, source=str(path), role='function'))
        (directory / 'msi_command_identity_types.h').write_text('\n\n'.join(declarations) + '\n', encoding='utf-8', newline='')
        (directory / 'msi_command_identity_body.h').write_text('\n\n'.join(bodies) + '\n', encoding='utf-8', newline='')
        (directory / 'msi_crash_recovery_journal.h').write_bytes(raw[str(journal_path)])
        for generated_name in ['msi_command_identity_types.h', 'msi_command_identity_body.h', 'msi_crash_recovery_journal.h']:
            generated_path = directory / generated_name
            generated_hashes[str(generated_path)] = digest(generated_path.read_bytes())
        source_manifest['variants'][variant] = {'mainSource': str(path), 'rawMainSha256': digest(raw[str(path)]),
                                               'runtimeSource': str(runtime_path), 'extractions': records,
                                               'unchangedProductionBodies': True}
        for config, options in [('release', ['/O2', '/DNDEBUG', '/MT']),
                                ('checked', ['/Od', '/D_DEBUG', '/RTC1', '/MTd', '/Z7'])]:
            build_dir = directory / config
            build_dir.mkdir()
            exe = build_dir / 'fixture.exe'
            command = ['cl', '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/W4', '/WX', '/permissive-',
                       '/Zc:__cplusplus', *options, '/DR53_EXPECT_OLD=' + ('1' if expect_old else '0'),
                       str(fixture_path), '/I' + str(directory), '/external:I' + str(json_path.parent), '/external:W0',
                       '/Fo' + str(build_dir / 'fixture.obj'), '/Fe' + str(exe), '/Fd' + str(build_dir / 'fixture.pdb')]
            batch = build_dir / 'build.cmd'
            batch.write_text('@echo off\ncall "' + str(args.vcvars) + '" >nul\nif errorlevel 1 exit /b %errorlevel%\n'
                             + subprocess.list2cmdline(command) + '\nexit /b %errorlevel%\n', encoding='utf-8')
            builds.append((variant, config, build_dir, exe, batch, command))
    source_manifest['currentAdmissionOnly'] = args.current_only
    source_manifest['generatedHeaderSha256'] = generated_hashes
    if args.current_only:
        source_manifest['singleStatementOpenerFixVerified'] = None
    else:
        old_line = 'if(msiControllerTraceEnabled())diag.interfacePath=W2U(detail->DevicePath);'
        if openers['old'].count(old_line) != 1:
            raise ValueError('frozen old opener does not contain exactly one reported defect')
        expected_opener = openers['old'].replace(old_line, 'diag.interfacePath=W2U(detail->DevicePath);')
        source_manifest['singleStatementOpenerFixVerified'] = code_tokens(expected_opener) == code_tokens(openers['current'])
        if not source_manifest['singleStatementOpenerFixVerified']:
            raise ValueError('opener differs beyond the authorized identity assignment (ignoring comments/whitespace)')
        source_manifest['differentProductionSymbolsIgnoringComments'] = [name for name in extracted_blocks['old'] if code_tokens(extracted_blocks['old'][name]) != code_tokens(extracted_blocks['current'][name])]
        if source_manifest['differentProductionSymbolsIgnoringComments'] != ['msiClawOpenCommandHid']:
            raise ValueError('unexpected changes in extracted production functions/declarations')
    save_json(output / 'source-proof.json', source_manifest)
    results = []
    for variant, config, build_dir, exe, batch, command in builds:
        print(f'Building {variant}/{config} with /W4 /WX', flush=True)
        compiled = subprocess.run(['cmd.exe', '/d', '/c', str(batch)], cwd=build_dir,
                                  capture_output=True, timeout=180)
        compile_text = (compiled.stdout + compiled.stderr).decode('utf-8', errors='replace')
        (build_dir / 'compile.log').write_text(compile_text, encoding='utf-8')
        entry = {'variant': variant, 'configuration': config, 'compileExitCode': compiled.returncode,
                 'command': command, 'compileLog': str(build_dir / 'compile.log')}
        if compiled.returncode == 0:
            tested = subprocess.run([str(exe)], cwd=build_dir, capture_output=True, timeout=30)
            (build_dir / 'stderr.txt').write_bytes(tested.stderr)
            (build_dir / 'result.json').write_bytes(tested.stdout)
            entry.update(runExitCode=tested.returncode, executableSha256=digest(exe.read_bytes()),
                         report=str(build_dir / 'result.json'))
            try:
                data = json.loads(tested.stdout)
                entry.update(checkCount=len(data['checks']), failures=data['failures'],
                             failedChecks=[c['name'] for c in data['checks'] if not c['pass']])
            except (ValueError, KeyError):
                entry['invalidResult'] = True
        else:
            print(compile_text, flush=True)
        results.append(entry)
        print(json.dumps({k:v for k,v in entry.items() if k not in ['command']}, ensure_ascii=False), flush=True)
        if compiled.returncode:
            break
    changed = [p for p, b in raw.items() if pathlib.Path(p).read_bytes() != b]
    changed_generated = [p for p,h in generated_hashes.items() if digest(pathlib.Path(p).read_bytes()) != h]
    summary = {'schemaVersion': 1, 'status': 'passed' if len(results) == 2 * len(variants) and not changed and not changed_generated and all(
        r['compileExitCode'] == 0 and r.get('runExitCode') == 0 and r.get('failures') == 0 for r in results) else 'failed',
        'sourceProof': str(output / 'source-proof.json'), 'sourceChangedDuringRun': changed, 'generatedHeadersChangedDuringRun': changed_generated,
        'singleStatementOpenerFixVerified': source_manifest['singleStatementOpenerFixVerified'],
        'currentAdmissionOnly': args.current_only,
        'results': results, 'realDeviceAccess': False, 'realPersistenceWrites': False,
        'productionModifiedByRunner': False, 'faultInjectionDefined': False,
        'realDeviceAcceptanceProven': False, 'scope': 'source-exact selection/admission/return gates; synthetic external boundaries only'}
    save_json(output / 'SUMMARY.json', summary)
    save_json(args.output_dir.resolve() / 'LATEST.json', {'runDirectory': str(output), 'summary': str(output / 'SUMMARY.json'),
                                                       'sourceProof': str(output / 'source-proof.json'), 'status': summary['status']})
    print(json.dumps({'status':summary['status'],'summary':str(output / 'SUMMARY.json'),'sourceChangedDuringRun':changed}, ensure_ascii=False), flush=True)
    return 0 if summary['status'] == 'passed' else 1


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, OSError, subprocess.TimeoutExpired) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        raise SystemExit(1)
