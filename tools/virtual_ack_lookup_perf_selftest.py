#!/usr/bin/env python3
"""R29: immutable, source-bound lookup candidate; correctness before authorized timing.

Writes only the R29 lookup-worker directory. Production is never edited.
A pre-R28 preparation may correctly report DOM safety failures. Reprepare with the
parent's authoritative --baseline-sha256 after R28; never bless an old scanner.
Formal timing is a separate invocation requiring an explicit parent permission
receipt. Each prepared run gets exactly one two-campaign attempt, even if it fails.
"""
from __future__ import annotations
import argparse
import datetime as dt
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import uuid

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[1]
OUTPUT = REPO.parents[2] / 'Build/Validation/VirtualInputPerf-R29-20261015/lookup-worker'
VCVARS = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
PLAN = {
    'schemaVersion': 1, 'candidate': 'R29-fixed-length-then-first-byte-memcmp',
    'profiles': ['Release', 'Checked'], 'campaigns': 2, 'pairedRounds': 11,
    'minimumFasterRounds': 10, 'minimumMedianPairedReduction': .05,
    'minimumRatioOfMediansReduction': .05,
    'allocationRule': 'Each ordinary/aligned request count and requested-byte count must not increase; computation-only candidate.',
    'batchSize': 32, 'measurementBatches': 128, 'allocationBatches': 2,
    'warmupBatches': 6,
    'workloads': ['scanner-whole-ack', 'scanner-alternate-order',
                  'scanner-optional-defaults', 'scanner-unknown-valid-shapes',
                  'scanner-unknown-heavy', 'lookup-known-fields', 'lookup-unknown-bytes'],
    'decision': 'Every workload, profile and campaign must pass all gates. Preserve failures. No retries or threshold/workload retuning.',
    'order': 'Within each pair alternate baseline/candidate first by round plus campaign; reverse profile order in campaign 2.',
    'timingBoundary': 'Actual full scanSubmitFrameReceipt and helpers, or actual lookup; input construction, JSON reporting and allocation counting excluded from timed windows.',
    'domBoundary': 'Vendored actual nlohmann JSON and source-extracted verifyAgainstDom predicate; fast acceptance implies identical DOM acceptance and output. DOM-only acceptance may fall back.',
    'permission': 'No timing before parent permission bound to exact R28 native SHA256 and prepared run name.',
    'productionEdits': False,
}


def sha(value: bytes | str) -> str:
    return hashlib.sha256(value.encode('utf-8') if isinstance(value, str) else value).hexdigest()


def write(path: Path, value: bytes | str, *, exclusive: bool = False):
    path = path.resolve()
    if not path.is_relative_to(OUTPUT.resolve()):
        raise RuntimeError(f'write outside authorized output: {path}')
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb' if exclusive else 'wb') as stream:
        stream.write(value.encode('utf-8') if isinstance(value, str) else value)


def save(path: Path, value, *, exclusive=False):
    write(path, json.dumps(value, ensure_ascii=False, indent=2) + '\n', exclusive=exclusive)


def masked(text):
    pattern = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/'
    return re.sub(pattern, lambda m: ''.join('\n' if c == '\n' else ' ' for c in m.group()), text)


def close_brace(text, opening):
    clean = masked(text)
    depth = 0
    for pos in range(opening, len(text)):
        if clean[pos] == '{':
            depth += 1
        elif clean[pos] == '}':
            depth -= 1
            if depth == 0:
                return pos
    raise RuntimeError('unterminated source block')


def function(text, marker):
    # Definitions only: a declaration followed by a later unrelated brace is unsafe.
    starts = [m.start() for m in re.finditer(re.escape(marker), text)]
    definitions = []
    for start in starts:
        opening = text.find('{', start)
        semi = text.find(';', start)
        if opening >= 0 and (semi < 0 or opening < semi):
            definitions.append(text[start:close_brace(text, opening) + 1])
    if len(definitions) != 1:
        raise RuntimeError(f'expected one definition of {marker!r}, got {len(definitions)}')
    return definitions[0]


def declaration(text, name):
    pattern = r'^static [^\r\n;]*\b' + re.escape(name) + r'\b[^;]*;'
    matches = re.findall(pattern, text, re.M)
    if len(matches) != 1:
        raise RuntimeError(f'expected one declaration of {name}, got {len(matches)}')
    return matches[0]


def extract(source):
    start = source.index('namespace c7aReceiptFast {')
    end = close_brace(source, source.index('{', start)) + 1
    namespace = source[start:end]
    receipt_start = source.index('struct InputHostFastReceipt {')
    receipt = source[receipt_start:close_brace(source, source.index('{', receipt_start)) + 2]
    table_start = namespace.index('const FieldName kFieldNames[] = {')
    table = namespace[table_start:namespace.index('};', table_start) + 2]
    fields = re.findall(r'\{"([A-Za-z0-9]+)",\s*(\d+),\s*(kF\w+)\}', table)
    if len(fields) != 22 or len({name for name, _, _ in fields}) != 22:
        raise RuntimeError('actual source field table is no longer the reviewed 22 unique fields')
    if any(len(name.encode('ascii')) != int(length) for name, length, _ in fields):
        raise RuntimeError('field table length differs from exact raw bytes')
    lookup = function(namespace, 'inline int lookupField(')
    oracle = function(namespace, 'void verifyAgainstDom(')
    scanner = namespace[:namespace.index('// 诊断 parity')]
    if scanner.count(lookup) != 1 or namespace.count(oracle) != 1:
        raise RuntimeError('ambiguous scanner/lookup/oracle extraction')
    scanner += '\n} // namespace c7aReceiptFast\n'
    names = ['g_powerGeneration', 'g_inputHostPersonaMx', 'g_inputHostBuildPersona',
             'g_inputHostBoundPersona', 'g_inputHostHostInstanceId', 'g_inputHostRunId',
             'g_inputHostStartupNonce', 'g_inputHostEpoch', 'g_inputHostBoundPowerGeneration',
             'g_inputHostConfigRevision', 'kInputHostProtocolConfigHash', 'kInputHostTargetId',
             'kInputHostOwnerIdentity', 'kInputHostLockedCoreSha256', 'kInputHostLockedCoreMvid']
    declarations = {name: declaration(source, name) for name in names}
    helper_markers = ['static std::string inputHostConfiguredPersona(',
                      'static const char* inputHostPersona(',
                      'static const char* inputHostProfileIdentityOf(',
                      'static const char* inputHostProfileIdentity(',
                      'static const char* inputHostBoundOrCurrentPersona(',
                      'static const char* inputHostBoundOrCurrentProfileIdentity(',
                      'static unsigned long long inputHostProtocolPowerGenerationLocked(']
    helpers = {marker.split('(')[0].split()[-1]: function(source, marker) for marker in helper_markers}
    config = re.findall(r'^\s*std::string virtualTargetPersona[^;]*;', source, re.M)
    if len(config) != 1:
        raise RuntimeError('configured persona type/default is ambiguous')
    return {'namespace': namespace, 'scanner': scanner, 'receipt': receipt, 'table': table,
            'fields': fields, 'lookup': lookup, 'oracle': oracle,
            'declarations': declarations, 'helpers': helpers, 'configMember': config[0].strip()}


def candidate_lookup(fields):
    lengths = {}
    for name, length, field_id in fields:
        lengths.setdefault(int(length), {}).setdefault(name[0], []).append((name, field_id))
    lines = ['inline int lookupField(const char* ks, const char* ke) {',
             '    const int len = static_cast<int>(ke - ks);', '    switch (len) {']
    for length, first_bytes in sorted(lengths.items()):
        lines.append(f'    case {length}:')
        if len(first_bytes) > 1:
            lines.append('        switch (static_cast<unsigned char>(*ks)) {')
            for first, entries in sorted(first_bytes.items()):
                lines.append(f"        case '{first}':")
                for name, field_id in entries:
                    lines.append(f'            if (std::memcmp(ks, "{name}", {length}) == 0) return {field_id};')
                lines.append('            break;')
            lines.extend(['        default: break;', '        }'])
        else:
            for entries in first_bytes.values():
                for name, field_id in entries:
                    lines.append(f'        if (std::memcmp(ks, "{name}", {length}) == 0) return {field_id};')
        lines.append('        break;')
    lines.extend(['    default: break;', '    }', '    return -1;', '}'])
    return '\n'.join(lines)


def generated_header(p):
    candidate = candidate_lookup(p['fields'])
    baseline = p['scanner'].replace('namespace c7aReceiptFast {', 'namespace baseline {', 1)
    after = p['scanner'].replace(p['lookup'], candidate, 1).replace('namespace c7aReceiptFast {', 'namespace candidate {', 1)
    # Reversibility proves every scanner, validator, helper, enum and type byte is unchanged.
    if after.replace(candidate, p['lookup'], 1).replace('namespace candidate {', 'namespace c7aReceiptFast {', 1) != p['scanner']:
        raise RuntimeError('candidate changes more than lookupField')
    context = '\n'.join(p['declarations'].values())
    context += '\n// Fixture settings object contains the exact production member type/default.\n'
    context += 'struct FixtureSettings { ' + p['configMember'] + ' };\nstatic FixtureSettings g_realStickTest;\n'
    context += '\n\n'.join(p['helpers'].values()) + '\n' + p['receipt'] + '\n'
    oracle = p['oracle']
    body = oracle[oracle.index('    json response ='):oracle.index('    const bool agree =')]
    context += '''\nstruct DomResult { bool accepted, firstFrame; unsigned long long submitUs;
    std::string host; bool discarded; std::string diff; };
static DomResult actualDomOracle(const char* begin, const char* end, const char* expectedStatus,
    const std::string& requestIdValue, unsigned long long sequenceValue) {
'''
    context += body
    context += '    return {domAccept, domFirstFrame, domSubmitUs, domHostInstanceId, response.is_discarded(), firstDiff};\n}\n'
    return context + '\n' + baseline + '\n' + after, candidate


def run_process(command, cwd, log, timeout=240):
    result = subprocess.run(command, cwd=cwd, capture_output=True, encoding='utf-8', errors='replace', timeout=timeout)
    write(log, result.stdout + result.stderr)
    return result


def build(run, profile):
    if not VCVARS.is_file():
        raise RuntimeError(f'MSVC vcvars missing: {VCVARS}')
    options = ['/O2', '/DNDEBUG', '/MT'] if profile == 'Release' else ['/Od', '/RTC1', '/D_DEBUG', '/D_ITERATOR_DEBUG_LEVEL=2', '/MTd']
    exe = run / (profile + '.exe')
    command = ['cl', '/nologo', '/Bv', '/std:c++20', '/utf-8', '/EHsc', '/W4', '/fp:precise',
               *options, str(run / 'fixture.cpp'), '/I' + str(run),
               '/Fo' + str(run / (profile + '.obj')), '/Fe' + str(exe)]
    batch = run / (profile + '-build.bat')
    write(batch, '@echo off\nset VSLANG=1033\ncall "' + str(VCVARS) + '" >nul\nif errorlevel 1 exit /b %errorlevel%\n' + subprocess.list2cmdline(command) + '\n')
    result = run_process(['cmd.exe', '/d', '/c', str(batch)], run, run / (profile + '-build.log'))
    if result.returncode:
        raise RuntimeError(f'{profile} compile failed; see {run / (profile + "-build.log")}')
    return exe


def bindings(run):
    return {str(path.relative_to(run)).replace('\\', '/'): sha(path.read_bytes())
            for path in sorted(run.rglob('*')) if path.is_file() and path.name != 'manifest.json'}


def verify_files(run, manifest):
    for name, digest in manifest['artifacts'].items():
        path = (run / name).resolve()
        if not path.is_relative_to(run.resolve()) or sha(path.read_bytes()) != digest:
            raise RuntimeError(f'prepared artifact has changed: {name}')


def prepare(args):
    OUTPUT.mkdir(parents=True, exist_ok=True)
    plan_path = OUTPUT / 'PREDECLARED-PLAN.json'
    if plan_path.exists():
        if json.loads(plan_path.read_text(encoding='utf-8')) != PLAN:
            raise RuntimeError('predeclared plan changed; no silent workload/gate retuning')
    else:
        save(plan_path, PLAN, exclusive=True)
    run = OUTPUT / ('prepare-' + dt.datetime.now(dt.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ') + '-' + uuid.uuid4().hex[:6])
    run.mkdir()
    source_bytes = (REPO / 'native/main.cpp').read_bytes()
    source_hash = sha(source_bytes)
    if args.baseline_sha256 and source_hash != args.baseline_sha256.lower():
        save(run / 'baseline-refused.json', {'actual': source_hash, 'parentExpected': args.baseline_sha256})
        raise RuntimeError('parent R28 baseline hash does not match actual native source')
    source = source_bytes.decode('utf-8-sig')
    p = extract(source)
    header, candidate = generated_header(p)
    write(run / 'native-main.snapshot.cpp', source_bytes)
    write(run / 'json.hpp', (REPO / 'deps/json/json.hpp').read_bytes())
    write(run / 'fixture.cpp', Path(__file__).with_suffix('.cpp').read_bytes())
    write(run / 'runner.snapshot.py', Path(__file__).read_bytes())
    write(run / 'source-bound.h', header)
    write(run / 'baseline-namespace.cpp', p['namespace'])
    write(run / 'candidate-lookup.cpp', candidate + '\n')
    write(run / 'candidate.patch', ''.join(difflib.unified_diff(
        source.splitlines(keepends=True), source.replace(p['lookup'], candidate, 1).splitlines(keepends=True),
        fromfile='a/native/main.cpp', tofile='b/native/main.cpp')))
    save(run / 'extraction.json', {
        'nativeMainSha256': source_hash, 'parentAuthoritativeR28': bool(args.baseline_sha256),
        'fieldNamesAndIds': p['fields'], 'exactDeclarationsAndDefaults': p['declarations'],
        'configuredPersonaMember': p['configMember'],
        'sourceHelperHashes': {name: sha(value) for name, value in p['helpers'].items()},
        'sourceScannerSha256': sha(p['scanner']), 'sourceNamespaceSha256': sha(p['namespace']),
        'sourceFieldTableSha256': sha(p['table']), 'sourceLookupSha256': sha(p['lookup']),
        'candidateLookupSha256': sha(candidate), 'domOracleSourceSha256': sha(p['oracle']),
        'knownScannerSafetyFailuresAreNotAnAcceptedReference': True,
        'fixtureSeam': 'Only g_realStickTest object reduced to its exact source member; all seven helpers, globals, constants, receipt, scanner types/defaults are extracted verbatim. DOM logging removed by source-prefix extraction; predicate retained verbatim.',
    })
    save(run / 'plan.json', PLAN)
    results = {}
    for profile in PLAN['profiles']:
        print(f'building/verifying {profile}: {run.name}', flush=True)
        exe = build(run, profile)
        result = run_process([str(exe), 'verify', 'dom' if args.dom_oracle else 'no-dom'], run,
                             run / (profile + '-verify.log'), timeout=300)
        try:
            report = json.loads(result.stdout)
        except json.JSONDecodeError as error:
            raise RuntimeError(f'fixture failed without JSON report: {profile}, {result.returncode}') from error
        save(run / (profile + '-correctness.json'), report)
        results[profile] = {'exitCode': result.returncode, 'pass': report['pass'],
                            'assertions': report['assertions'], 'failures': report['failureCount'],
                            'baselineDomSafetyFailures': report['baselineDomSafetyFailures'],
                            'candidateDomSafetyFailures': report['candidateDomSafetyFailures']}
    if (REPO / 'native/main.cpp').read_bytes() != source_bytes:
        save(run / 'source-drift.json', {'snapshot': source_hash, 'current': sha((REPO / 'native/main.cpp').read_bytes())})
        stable = False
    else:
        stable = True
    correct = all(row['pass'] for row in results.values())
    manifest = {
        'schemaVersion': 1, 'recordType': 'immutable-preparation', 'runName': run.name, 'preparedUtc': dt.datetime.now(dt.timezone.utc).isoformat(),
        'nativeMainSha256': source_hash, 'jsonSha256': sha((run / 'json.hpp').read_bytes()),
        'parentAuthoritativeR28': bool(args.baseline_sha256), 'sourceStableDuringPreparation': stable,
        'domOracle': args.dom_oracle, 'correctnessPassed': correct, 'profiles': results,
        'benchmarkReady': correct and stable and bool(args.baseline_sha256) and args.dom_oracle,
        'benchmarkExecuted': False, 'productionEdited': False, 'planSha256': sha((run / 'plan.json').read_bytes()),
        'artifacts': bindings(run),
    }
    save(run / 'manifest.json', manifest)
    print(json.dumps({'run': str(run), 'ready': manifest['benchmarkReady'], 'correctnessPassed': correct,
                      'sourceSha256': source_hash, 'profiles': results, 'benchmarkExecuted': False}, indent=2))
    return 0 if correct else 2


def benchmark(args):
    run = Path(args.benchmark_ready_run).resolve()
    if not run.is_relative_to(OUTPUT.resolve()):
        raise RuntimeError('benchmark run must be within authorized lookup-worker output')
    manifest = json.loads((run / 'manifest.json').read_text(encoding='utf-8'))
    if not manifest['benchmarkReady'] or not manifest['domOracle']:
        raise RuntimeError('requires passing authoritative R28 source-bound preparation with actual DOM oracle')
    verify_files(run, manifest)
    permission = json.loads(Path(args.permission_file).read_text(encoding='utf-8'))
    for key, expected in {'benchmarkApproved': True, 'approver': 'parent',
                          'nativeMainSha256': manifest['nativeMainSha256'], 'preparedRunName': run.name}.items():
        if permission.get(key) != expected:
            raise RuntimeError(f'parent timing permission does not bind {key} to {expected!r}')
    if sha((REPO / 'native/main.cpp').read_bytes()) != manifest['nativeMainSha256']:
        raise RuntimeError('actual native source changed; reprepare correctness from new authoritative baseline')
    if sha((REPO / 'deps/json/json.hpp').read_bytes()) != manifest['jsonSha256']:
        raise RuntimeError('actual DOM dependency changed; reprepare correctness with vendored actual JSON')
    # O_EXCL is a permanent single-attempt claim, retained on failure or interruption.
    claim = {'startedUtc': dt.datetime.now(dt.timezone.utc).isoformat(), 'preparedRun': str(run),
             'permission': permission, 'noRetry': True, 'allR29PreparationsShareThisSingleAttempt': True}
    save(OUTPUT / 'FORMAL-BENCHMARK-ATTEMPT.json', claim, exclusive=True)
    save(run / 'BENCHMARK-ATTEMPT.json', claim, exclusive=True)
    rows, all_pass = [], True
    for campaign in (1, 2):
        profiles = PLAN['profiles'] if campaign == 1 else list(reversed(PLAN['profiles']))
        for profile in profiles:
            print(f'campaign {campaign}/2 {profile}: {run.name}', flush=True)
            result = run_process([str(run / (profile + '.exe')), 'benchmark', str(campaign)], run,
                run / f'{profile}-campaign-{campaign}.log', timeout=300)
            report = json.loads(result.stdout)
            save(run / f'{profile}-campaign-{campaign}.json', report)
            all_pass = all_pass and report['pass'] and result.returncode == 0
            rows.append({'profile': profile, 'campaign': campaign, 'exitCode': result.returncode, **report})
    source_stable = sha((REPO / 'native/main.cpp').read_bytes()) == manifest['nativeMainSha256']
    summary = {'pass': all_pass and source_stable, 'sourceStable': source_stable,
               'nativeMainSha256': manifest['nativeMainSha256'], 'rows': rows,
               'productionEdited': False, 'wholeProcessCpuClaim': False,
               'retainFailures': True, 'retries': 0}
    save(run / 'benchmark-summary.json', summary)
    print(json.dumps({'run': str(run), 'pass': summary['pass'], 'formalRows': sum(len(x['rows']) for x in rows),
                      'passedRows': sum(row['pass'] for x in rows for row in x['rows'])}, indent=2))
    return 0 if summary['pass'] else 2


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', action='store_true', help='create immutable preparation; default action')
    parser.add_argument('--baseline-sha256', help='parent-announced authoritative R28 native/main.cpp SHA256')
    parser.add_argument('--dom-oracle', action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument('--benchmark-ready-run', help='previously prepared authoritative run')
    parser.add_argument('--permission-file', help='explicit parent timing authorization JSON receipt')
    args = parser.parse_args()
    if args.benchmark_ready_run:
        if not args.permission_file or args.prepare or args.baseline_sha256:
            parser.error('benchmark requires --permission-file and cannot also prepare')
        return benchmark(args)
    if args.permission_file:
        parser.error('--permission-file is only used with --benchmark-ready-run')
    return prepare(args)


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f'R29 ERROR: {error}', file=sys.stderr)
        raise SystemExit(1)
