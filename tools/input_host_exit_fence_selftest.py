#!/usr/bin/env python3
"""Execute complete exit-fence functions and the real startup-policy dependency.

Queue/log are explicit memory leaves. Only reviewed whole functions (CRLF/LF
normalized) and exact dependency bytes pass the source gate. Historical old,
monotonic candidate, current source and clear-restoration controls run in both
Release and Checked. No Host, device, product process or timer runs.
"""
from pathlib import Path
import argparse
import copy
import datetime as dt
import hashlib
import importlib.util
import json
import os
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
BASE = 'tools/fixtures/input_capture_timer_retirement/exit_fence_baseline.json'
TEMPLATE = 'tools/fixtures/input_capture_timer_retirement/exit_fence_fixture.cpp.in'
CONTRACT = 'tools/fixtures/input_capture_timer_retirement/exit_fence_source_contract.json'
AUTH = 'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'
DEPENDENCIES = ('native/input_startup_policy.h', 'native/controller_persona_settings.h',
                'native/controller_persona.h', 'deps/json/json.hpp')
SOURCES = ('native/main.cpp', 'tools/input_host_exit_fence_selftest.py', TEMPLATE, BASE,
           CONTRACT, 'tools/input_capture_worker_retirement_selftest.py',
           'tools/input_timer_evidence_adapter_selftest.py', AUTH, *DEPENDENCIES)
NAMES = {'IS_CLEANUP': 'inputHostLifecycleWorkIsCleanup', 'BLOCKS': 'inputHostExitFenceBlocks',
         'IS_RAISED': 'inputHostExitFenceRaised', 'RAISE': 'inputHostRaiseExitFence',
         'RESET': 'inputHostResetExitFence'}
FIXTURE_CALLS = {'inputHostExitFenceSelfTest': 2, 'msiG3eCapabilitySelfTest': 6}
GLOBAL_NAMES = ('g_inputHostExitFence', 'g_inputHostExitFenceRefusals', 'g_inputHostExitFenceRefusalRows')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def need(value, message):
    if not value:
        raise RuntimeError(message)


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def lf(text):
    # This deliberately does not strip comments, literals or other whitespace.
    return text.replace('\r\n', '\n')


def function_hashes(bundle, normalize=False):
    return {name: sha((lf(text) if normalize else text).encode()) for name, text in bundle.items()}


def candidate_function(old):
    nl = '\r\n' if '\r\n' in old else '\n'
    anchor = '    const bool previousRaised = g_inputHostExitFence.exchange(false, std::memory_order_acq_rel);'
    need(old.count(anchor) == 1, 'Unique historical exit-fence clear required')
    replacement = nl.join([
        '    // The process default is open. Capture initialization may reset diagnostic',
        '    // counters, but must never reopen a terminal fence raised by app exit.',
        '    const bool previousRaised = g_inputHostExitFence.load(std::memory_order_acquire);',
        '    if (previousRaised) {',
        '        appendNativeLifecycleLog("input-host-exit-fence", {',
        '            {"phase", "reset-refused"}, {"reason", "terminal-process-exit"},',
        '            {"previousRaised", true}, {"owner", owner ? owner : "unknown"},',
        '            {"refusals", g_inputHostExitFenceRefusals.load(std::memory_order_acquire)}',
        '        });',
        '        return;',
        '    }'])
    new = old.replace(anchor, replacement, 1)
    anchor = '        {"phase", "reset"},'
    need(new.count(anchor) == 1, 'Original startup diagnostic phase required')
    return new.replace(anchor, anchor + nl + '        {"resetScope", "diagnostic-counters-only"},', 1)


def restore_clear(bundle):
    bad = dict(bundle)
    target = 'g_inputHostExitFence.load(std::memory_order_acquire)'
    need(bad['inputHostResetExitFence'].count(target) == 1, 'Unique monotonic load target')
    bad['inputHostResetExitFence'] = bad['inputHostResetExitFence'].replace(
        target, 'g_inputHostExitFence.exchange(false, std::memory_order_acq_rel)', 1)
    return bad


def owner_noop_extension(bundle):
    extended = dict(bundle)
    name = 'inputHostResetExitFence'
    anchor = '    const bool previousRaised = '
    need(extended[name].count(anchor) == 1, 'Unique reviewed extension anchor required')
    nl = '\r\n' if '\r\n' in extended[name] else '\n'
    extended[name] = extended[name].replace(anchor, '    (void)owner;' + nl + anchor, 1)
    return extended


def verify_reviewed_sources(bundle, source, contract):
    need(contract.get('schemaVersion') == 1, 'Unknown exit-fence source contract schema')
    need(set(bundle) == set(NAMES.values()), 'Complete five-function bundle required')
    need(set(contract['dependencyHashes']) == set(DEPENDENCIES), 'Exact dependency closure required')
    for name in DEPENDENCIES:
        need(sha(source[name]) == contract['dependencyHashes'][name], 'Unreviewed dependency source: ' + name)
    hashes = function_hashes(bundle, normalize=True)
    matches = [entry for entry in contract['reviewedFunctionBundles'] if entry['functionHashesLf'] == hashes]
    need(len(matches) == 1, 'Current complete fence functions have an unreviewed source identity')
    return matches[0]['id']


def capture_scope(text, ret):
    functions = {name: ret.function(text, name)['text'] for name in NAMES.values()}
    declarations = []
    for name in GLOBAL_NAMES:
        matches = list(re.finditer(r'^static[^\n;]*\b' + name + r'\b[^;]*;', text, re.M))
        need(len(matches) == 1, 'Unique production fence declaration ' + name)
        declarations.append(matches[0].group())
    init = ret.function(text, 'inputCaptureInit')['text']
    need(init.count('inputHostResetExitFence("capture-init");') == 1, 'Actual delayed-init call site changed')
    helpers, native_hashes, calls = {}, {}, {}
    for name, count in FIXTURE_CALLS.items():
        native = ret.function(text, name)['text']
        masked = ret.helper.mask_cpp(native)
        match = re.search(r'const auto resetExitFenceFixture = \[\]\(const char\* owner\)\s*\{', masked)
        need(match is not None, 'Actual native selftest restoration helper missing ' + name)
        end = masked.index('{', match.start()) + 1
        depth = 1
        while depth:
            need(end < len(masked), 'Unclosed native selftest lambda')
            depth += (masked[end] == '{') - (masked[end] == '}')
            end += 1
        need(masked[end] == ';', 'Native selftest helper terminator changed')
        helpers[name] = native[match.start():end + 1]
        native_hashes[name] = sha(native.encode())
        calls[name] = len(re.findall(r'\bresetExitFenceFixture\s*\(', masked))
        need(calls[name] == count, 'Native isolated fixture call sites changed ' + name)
    need(len(set(helpers.values())) == 1, 'Native selftest restoration helpers drifted apart')
    # Whole native selftests are not executed. Bind their exact copied helper and
    # call counts; unrelated edits in those large functions are observations only.
    scope = {'completeFunctions': functions, 'globals': declarations,
             'sourceBoundOnlyInputCaptureInit': init, 'fixtureHelpers': helpers, 'fixtureCalls': calls}
    return scope, native_hashes


def gate_controls(current, source, contract, folder, reviewed_extension):
    folder.mkdir()
    results = []
    for name, bundle in [('current', current),
                         ('CRLF-only', {n: lf(v).replace('\n', '\r\n') for n, v in current.items()}),
                         ('LF-only', {n: lf(v) for n, v in current.items()}),
                         ('explicit-reviewed-owner-noop-extension', reviewed_extension)]:
        identity = verify_reviewed_sources(bundle, source, contract)
        results.append({'name': name, 'accepted': True, 'reviewedIdentity': identity,
                        'functionHashesLf': function_hashes(bundle, True)})
    unknown = dict(current)
    # A harmless but unreviewed extra statement must still fail exact identity.
    unknown['inputHostResetExitFence'] = unknown['inputHostResetExitFence'].replace(
        '    const bool previousRaised = ', '    (void)0;\n    const bool previousRaised = ', 1)
    mutants = [('unknown-function-statement', unknown, source),
               ('restore-terminal-clear', restore_clear(current), source)]
    for dep in DEPENDENCIES:
        changed = dict(source)
        changed[dep] += b'\n// unreviewed dependency change\n'
        mutants.append(('unknown-dependency-' + Path(dep).name, current, changed))
    for name, bundle, changed in mutants:
        case = folder / name
        case.mkdir()
        save(case / 'complete-functions.json', bundle)
        changed_deps = {n: sha(changed[n]) for n in DEPENDENCIES}
        save(case / 'dependency-hashes.json', changed_deps)
        try:
            verify_reviewed_sources(bundle, changed, contract)
        except RuntimeError as error:
            row = {'name': name, 'accepted': False, 'expectedFailureCaught': True,
                   'error': str(error), 'compiled': False, 'functionHashesLf': function_hashes(bundle, True),
                   'dependencyHashes': changed_deps}
            save(case / 'RESULT.json', row)
            results.append(row)
        else:
            raise RuntimeError('Source gate accepted a specified unknown input: ' + name)
    save(folder / 'RESULT.json', results)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-root', required=True, type=Path)
    parser.add_argument('--source-contract-manifest', type=Path)
    parser.add_argument('--source-contract-sha256')
    args = parser.parse_args()
    out = args.output_root.resolve()
    run = out / ('r' + dt.datetime.now().strftime('%H%M%S') + '-' + str(os.getpid()))
    run.mkdir(parents=True)
    receipt = {'schemaVersion': 2, 'pass': False, 'status': 'RUNNING', 'evidenceComplete': False,
               'productionMonotonicFenceAccepted': False, 'realControlIO': False,
               'wholeLifecycleAcceptance': False, 'wholeMainAcceptance': False,
               'run': str(run), 'profiles': [], 'controlProfiles': [],
               'startedAt': dt.datetime.now().astimezone().isoformat()}

    def publish():
        save(run / 'RESULT.json', receipt)
        save(out / 'RESULT.json', receipt)

    try:
        need(bool(args.source_contract_manifest) == bool(args.source_contract_sha256), 'Contract path and approved SHA must be supplied together')
        contract_key = CONTRACT
        if args.source_contract_manifest:
            need(args.source_contract_manifest.is_absolute() and args.source_contract_manifest.resolve().is_relative_to(ROOT.resolve()), 'Reviewed contract must be an absolute repository source')
            need(sha(args.source_contract_manifest.read_bytes()) == args.source_contract_sha256, 'Reviewed source contract identity changed')
            contract_key = args.source_contract_manifest.resolve().relative_to(ROOT.resolve()).as_posix()
        names = tuple(dict.fromkeys((*SOURCES, contract_key)))
        source = {name: (ROOT / name).read_bytes() for name in names}
        snap = run / 'source'
        for name, data in source.items():
            path = snap / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        receipt['sourceHashes'] = {name: sha(data) for name, data in source.items()}
        spec = importlib.util.spec_from_file_location('fence_snapshot_ret', snap / 'tools/input_capture_worker_retirement_selftest.py')
        ret = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(ret)
        need(ret.helper.__file__ == str(snap / 'tools/input_timer_evidence_adapter_selftest.py'), 'Producer helper escaped snapshot')
        text = source['native/main.cpp'].decode('utf-8-sig')
        contract = json.loads(source[contract_key])
        need(sha(source[BASE]) == contract['historicalBaselineSha256'], 'Original historical baseline source identity changed')
        frozen = json.loads(source[BASE])
        old = frozen['functions']
        need(function_hashes(old) == frozen['functionHashes'], 'Frozen function identity changed')
        candidate = dict(old)
        candidate['inputHostResetExitFence'] = candidate_function(old['inputHostResetExitFence'])
        scope, native_hashes = capture_scope(text, ret)
        current = scope['completeFunctions']
        identity = verify_reviewed_sources(current, source, contract)
        need(scope['globals'] == frozen['globals'], 'Production default/declarations drifted')
        fixture_helper = next(iter(scope['fixtureHelpers'].values()))
        # Keep the native test-only clear exception exact; never admit it in production reset.
        need(lf(fixture_helper) == 'const auto resetExitFenceFixture = [](const char* owner) {\n'
             '        g_inputHostExitFence.store(false, std::memory_order_release);\n'
             '        inputHostResetExitFence(owner);\n    };', 'Unreviewed native test-only restore helper')
        scope_file = run / 'SOURCE-SCOPE-START.json'
        save(scope_file, scope)
        receipt.update(currentProductionMonotonic=True, reviewedSourceIdentity=identity,
                       actualFunctionHashes=function_hashes(current), legacyFunctionHashes=frozen['functionHashes'],
                       actualCaptureInitFunctionSha256=sha(scope['sourceBoundOnlyInputCaptureInit'].encode()),
                       nativeSelftestFixtureResetBound=True,
                       nativeSelftestFixtureHelperHashes=function_hashes(scope['fixtureHelpers']),
                       nativeSelftestFunctionHashes=native_hashes, nativeSelftestFixtureCalls=scope['fixtureCalls'],
                       dependencyHashes=contract['dependencyHashes'], sourceContractPath=contract_key, sourceContractSha256=sha(source[contract_key]),
                       sourceGateControls=gate_controls(current, source, contract, run / 'source-gates', owner_noop_extension(candidate)),
                       leaves={'queue': 'memory deque + mutex + atomic requested flag, actual raise/filter code',
                               'log': 'memory JSON vector under mutex; no filesystem logger',
                               'startupPolicy': 'real complete pinned header and transitive headers; executed as pure JSON preparation',
                               'captureInit': 'full function source-bound only; its OS/Host lifecycle is not executed'},
                       sourceStabilityScope='complete five functions, globals, full source-bound capture init, copied native fixture helpers/call counts, all non-main inputs',
                       sourceScopeStartSha256=sha(scope_file.read_bytes()))
        publish()
        compiler, env = ret.helper.compiler_environment(run)
        authority = json.loads(source[AUTH])
        need(Path(authority['native']['compiler']).resolve() == compiler.resolve(), 'Compiler path mismatch')
        expected = next(row['sha256'] for row in authority['files'] if Path(row['path']).resolve() == compiler.resolve())
        need(sha(compiler.read_bytes()) == expected, 'Compiler changed')
        env['INCLUDE'] = ';'.join(authority['native']['include'])
        env['LIB'] = ';'.join(authority['native']['lib'])
        receipt['compiler'] = {'path': str(compiler), 'sha256': expected, 'sdkVersion': env.get('WindowsSDKVersion'),
                               'vcvarsPath': str(ret.helper.VCVARS), 'vcvarsSha256': sha(ret.helper.VCVARS.read_bytes())}

        def check_live_scope(label):
            live = {name: (ROOT / name).read_bytes() for name in names}
            for name in names:
                if name != 'native/main.cpp':
                    need(live[name] == source[name], 'Fence dependency/consumer changed ' + label + ': ' + name)
            observed_scope, observed_native = capture_scope(live['native/main.cpp'].decode('utf-8-sig'), ret)
            need(observed_scope == scope, 'Complete tested function/source-bound scope changed ' + label)
            return live, observed_scope, observed_native

        routes = [('legacy', old, True, False), ('monotonic', current, False, False),
                  ('negative-restore-clear', restore_clear(current), True, False),
                  ('historical-candidate', candidate, False, True),
                  ('historical-candidate-clear', restore_clear(candidate), True, True),
                  ('reviewed-owner-noop-extension', owner_noop_extension(candidate), False, True)]
        for profile, flags in [('Release', ['/O2', '/MT']), ('Checked', ['/Od', '/RTC1', '/D_DEBUG', '/MTd'])]:
            for route, bundle, negative, control in routes:
                check_live_scope('before ' + profile + '-' + route)
                if route in ('monotonic', 'reviewed-owner-noop-extension'):
                    verify_reviewed_sources(bundle, source, contract)
                folder = run / (profile + '-' + route)
                folder.mkdir()
                cpp, exe, deps = folder / 'fence.cpp', folder / 'fence.exe', folder / 'deps.json'
                body = source[TEMPLATE].decode().replace('@GLOBALS@', '\n'.join(scope['globals'])).replace('@SELFTEST_RESET@', fixture_helper)
                for marker, name in NAMES.items():
                    body = body.replace('@' + marker + '@', bundle[name])
                need(not re.search(r'@[A-Z_]+@', body), 'Unexpanded source marker')
                cpp.write_text(body, encoding='utf-8', newline='\n')
                # Verify the complete snippets that the compiler actually receives.
                compiled = {name: ret.function(cpp.read_bytes().decode(), name)['text'] for name in NAMES.values()}
                need(compiled == bundle, 'Complete compiled functions differ from bound input')
                build = ret.helper.execute([compiler, '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/permissive-', '/W4', *flags,
                    '/sourceDependencies', deps, '/I' + str(snap / 'deps/json'), '/I' + str(snap / 'native'), cpp,
                    '/Fo' + str(folder / 'fence.obj'), '/Fe' + str(exe), '/link', '/INCREMENTAL:NO'], folder, env, folder / 'build.log', 90)
                need(build.returncode == 0, 'Fence consumer compile failed: ' + str(folder / 'build.log'))
                includes = [Path(p).resolve() for p in json.loads(deps.read_text(encoding='utf-8-sig'))['Data']['Includes']]
                expected_includes = {(snap / name).resolve() for name in DEPENDENCIES}
                actual_includes = {p for p in includes if p.is_relative_to(snap.resolve())}
                need(actual_includes == expected_includes, 'Compiled snapshot dependency closure differs from reviewed pins')
                need(all(not p.is_relative_to(ROOT) for p in includes), 'Live repository dependency was compiled')
                # Any extra project header, including one outside the snapshot, is refused.
                tool_roots = [Path(p).resolve() for p in authority['native']['include']]
                need(all(p in expected_includes or any(p.is_relative_to(root) for root in tool_roots) for p in includes),
                     'Unreviewed include outside pinned snapshot/toolchain roots')
                observed = ret.helper.execute([exe], folder, env, folder / 'run.log', 30)
                value = json.loads(observed.stdout.strip().splitlines()[-1])
                row = {'profile': profile, 'route': route, 'isCurrentProductionRoute': route == 'monotonic',
                       'exitCode': observed.returncode, **value, 'compiledFunctionHashes': function_hashes(bundle),
                       'compiledDependencyHashes': {name: sha((snap / name).read_bytes()) for name in DEPENDENCIES},
                       'sourceSha256': sha(cpp.read_bytes()), 'executableSha256': sha(exe.read_bytes()),
                       'sourceDependenciesSha256': sha(deps.read_bytes())}
                receipt['controlProfiles' if control else 'profiles'].append(row)
                publish()
                need(value['allThreadsExplicitlyJoined'] and value['threadsStarted'] == 9 and value['threadsJoined'] == 9
                     and len(value['cases']) == 6, 'Fence fixture join/case coverage failed')
                need(len(value['dependencyCases']) == 2 and all(case['safetyPass'] for case in value['dependencyCases']),
                     'Real startup-policy contract/terminal purity failed')
                if negative:
                    need(observed.returncode == 1 and not value['pass'] and value['failures'] > 0
                         and 'late-init-must-not-clear-terminal-fence' in value['failedChecks'],
                         'Specified historical clear defect not rejected')
                    row['expectedFailureCaught'] = True
                else:
                    need(observed.returncode == 0 and value['pass'] and value['failures'] == 0,
                         'Actual-function positive route failed: ' + route)
                check_live_scope('after ' + profile + '-' + route)
                row['testedScopeStable'] = True
                print(profile + ' ' + route + ': checks=' + str(value['checks']) + ' exit=' + str(observed.returncode) + ' joined=9/9', flush=True)
                publish()
        live, end_scope, end_native = check_live_scope('at final publication')
        save(run / 'SOURCE-SCOPE-END.json', end_scope)
        receipt.update(sourceStable=True, sourceScopeUnchanged=True, evidenceComplete=True,
                       productionMonotonicFenceAccepted=True, sourceScopeEndSha256=sha((run / 'SOURCE-SCOPE-END.json').read_bytes()),
                       sourceEndHashes={name: sha(data) for name, data in live.items()},
                       wholeMainSourceStable=live['native/main.cpp'] == source['native/main.cpp'],
                       nativeSelftestFunctionEndHashes=end_native, finishedAt=dt.datetime.now().astimezone().isoformat())
        receipt['pass'] = True
        receipt['status'] = 'PASS'
        receipt['checks'] = sum(row['checks'] for row in receipt['profiles'])
        receipt['controlChecks'] = sum(row['checks'] for row in receipt['controlProfiles'])
        publish()
        print('PASS RESULT=' + str(out / 'RESULT.json'))
        return 0
    except Exception as error:
        receipt['status'] = 'FAIL'
        receipt['error'] = str(error)
        receipt['finishedAt'] = dt.datetime.now().astimezone().isoformat()
        publish()
        print(str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
