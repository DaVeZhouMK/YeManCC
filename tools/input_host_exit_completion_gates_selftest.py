#!/usr/bin/env python3
"""Actual READY case and complete wWinMain message loop on explicit memory ports.

Only a private cleanup thread/event are real Windows resources. The actual full
Host-local predicate and capture-return getter execute. No product Host/worker,
window or module cleanup runs. Exact reviewed raw snippets/dependencies are
required; old code and host-gate deletion are executed negative controls.
"""
from pathlib import Path
import argparse
import datetime as dt
import hashlib
import importlib.util
import json
import os
import re
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
RUNNER = 'tools/input_host_exit_completion_gates_selftest.py'
DIRECTORY = 'tools/fixtures/input_host_exit_completion_gates/'
TEMPLATE = DIRECTORY + 'fixture.cpp.in'
CONTRACT = DIRECTORY + 'source-contract.json'
ADAPTER = 'tools/input_timer_evidence_adapter_selftest.py'
AUTH = 'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'
DEPENDENCIES = ('native/input_host_exit_owner_runtime.h', 'native/exit_cleanup_wait.h',
                'native/input_host_exit_retirement.h', 'deps/json/json.hpp', ADAPTER, AUTH)
SOURCES = ('native/main.cpp', RUNNER, TEMPLATE, CONTRACT, *DEPENDENCIES)
GLOBAL_NAMES = ('g_exitRequested', 'g_exitReadyPosted', 'g_exitCode', 'g_exitCleanupThread',
                'g_exitCleanupCompleted', 'g_exitCleanupStage', 'g_exitBeginTick',
                'g_exitLastWaitingStage', 'g_captureExitReleased')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def need(value, message):
    if not value:
        raise RuntimeError(message)


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def brace_end(code, start):
    end = code.index('{', start) + 1
    depth = 1
    while depth:
        need(end < len(code), 'Unclosed actual source block')
        depth += (code[end] == '{') - (code[end] == '}')
        end += 1
    return end


def function(text, name, mask):
    code = mask(text)
    matches = list(re.finditer(r'^static[^\n;{}]*\b' + re.escape(name) +
                              r'\s*\([^;{}]*\)\s*(?:noexcept\s*)?\{', code, re.M))
    need(len(matches) == 1, 'Unique complete actual function required: ' + name)
    start = matches[0].start()
    return text[start:brace_end(code, start)]


def main_blocks(text, mask, entry_name='wWinMain'):
    code = mask(text)
    cases = list(re.finditer(r'^    case WM_APP_EXIT_READY:\s*\{', code, re.M))
    need(len(cases) == 1, 'Unique complete READY case required')
    start = cases[0].start()
    ready = text[start:brace_end(code, start)]
    loops = list(re.finditer(r'^    MSG msg\{\};\s*\n    for \(;;\) \{', code, re.M))
    need(len(loops) == 1, 'Unique complete wWinMain message loop required')
    start = loops[0].start()
    opening = code.index('for (;;)', start)
    loop = text[start:brace_end(code, opening)]
    # Bind the loop to its real containing entry point, never to a lookalike.
    wmain = list(re.finditer(r'\b' + re.escape(entry_name) + r'\s*\([^;{}]*\)\s*\{', code))
    need(len(wmain) == 1 and wmain[0].start() < start < brace_end(code, wmain[0].start()),
         'Message loop is not inside the actual complete wWinMain definition')
    return {'readyCase': ready, 'messageLoop': loop}


def capture_scope(source, mask):
    text = source['native/main.cpp'].decode('utf-8-sig')
    scope = main_blocks(text, mask)
    scope['hostLocalPredicate'] = function(source['native/input_host_exit_owner_runtime.h'].decode('utf-8-sig'),
                                           'inputHostExitLocalOwnershipReleased', mask)
    scope['capturePredicate'] = function(text, 'inputCaptureExitOwnershipReleased', mask)
    for name in GLOBAL_NAMES:
        matches = list(re.finditer(r'^static[^\n;{}]*\b' + name + r'\b[^;]*;', text, re.M))
        need(len(matches) == 1, 'Unique actual global required: ' + name)
        scope['global:' + name] = matches[0].group()
    owner_text = source['native/input_host_exit_owner_runtime.h'].decode('utf-8-sig')
    name = 'g_inputHostExitLocalReturned'
    matches = list(re.finditer(r'^static[^\n;{}]*\b' + name + r'\b[^;]*;', owner_text, re.M))
    need(len(matches) == 1, 'Unique actual Host-return flag required')
    scope['global:' + name] = matches[0].group()
    return scope


def verify_sources(source, contract, mask):
    need(contract.get('schemaVersion') == 1, 'Unknown completion-gates source contract')
    need(set(contract['dependencyHashes']) == set(DEPENDENCIES), 'Exact reviewed dependency closure required')
    for name in DEPENDENCIES:
        need(sha(source[name]) == contract['dependencyHashes'][name], 'Unreviewed dependency raw source: ' + name)
    scope = capture_scope(source, mask)
    actual = {name: sha(text.encode()) for name, text in scope.items()}
    need(actual == contract['scopeRawHashes'], 'Unreviewed complete exit case/loop/predicate/global raw source shape')
    return scope


def variants(scope, historical):
    current = {name: scope[name] for name in ('readyCase', 'messageLoop')}
    remove_ready = dict(current)
    anchor = '||!inputHostExitLocalOwnershipReleased()'
    need(remove_ready['readyCase'].count(anchor) == 1, 'Exact READY host-gate mutation anchor changed')
    remove_ready['readyCase'] = remove_ready['readyCase'].replace(anchor, '', 1)
    remove_loop = dict(current)
    anchor = '&&inputHostExitLocalOwnershipReleased()'
    need(remove_loop['messageLoop'].count(anchor) == 1, 'Exact main-loop host-gate mutation anchor changed')
    remove_loop['messageLoop'] = remove_loop['messageLoop'].replace(anchor, '', 1)
    return [('current', current, []), ('historical-before-host-gate', historical, ['ready', 'no-window-loop']),
            ('delete-ready-host-gate', remove_ready, ['ready']),
            ('delete-no-window-host-gate', remove_loop, ['no-window-loop'])]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-root', type=Path, required=True)
    parser.add_argument('--source-only', action='store_true', help='Verify/extract only; exit 12, never a passing runtime receipt')
    args = parser.parse_args()
    out = args.output_root.resolve()
    need(not out.is_relative_to(ROOT), 'Evidence must be outside repository')
    run = out / ('run-' + dt.datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '-' + str(os.getpid()))
    run.mkdir(parents=True)
    receipt = {'schemaVersion': 1, 'status': 'RUNNING', 'pass': False, 'run': str(run),
               'profiles': [], 'runtimeEvidenceComplete': False, 'sourceStable': False,
               'wholeMainAcceptance': False, 'wholeLifecycleAcceptance': False,
               'realProductHostOrCaptureExecuted': False, 'realWindowsOrModuleCleanupExecuted': False,
               'privateCleanupThreadHandleIsReal': True, 'processExitUsedAsCleanup': False,
               'startedAt': dt.datetime.now().astimezone().isoformat()}

    def publish():
        save(run / 'RESULT.json', receipt)
        save(out / 'RESULT.json', receipt)

    try:
        source = {name: (ROOT / name).read_bytes() for name in SOURCES}
        contract = json.loads(source[CONTRACT])
        historical_path = Path(contract['historicalMain']['path'])
        historical_bytes = historical_path.read_bytes()
        need(sha(historical_bytes) == contract['historicalMain']['sha256'], 'Preserved historical whole-main raw identity changed')
        source['historical/native/main.cpp'] = historical_bytes
        snapshot = run / 'source'
        for name, data in source.items():
            path = snapshot / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        receipt['sourceHashes'] = {name: sha(data) for name, data in source.items()}
        spec = importlib.util.spec_from_file_location('completion_gate_snapshot_utilities', snapshot / ADAPTER)
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        need(Path(helper.__file__).resolve() == (snapshot / ADAPTER).resolve(), 'Utilities escaped immutable snapshot')
        scope = verify_sources(source, contract, helper.mask_cpp)
        historical = main_blocks(historical_bytes.decode('utf-8-sig'), helper.mask_cpp)
        need({n: sha(v.encode()) for n, v in historical.items()} == contract['historicalMain']['scopeRawHashes'],
             'Preserved complete historical case/loop identity changed')
        receipt.update(actualScopeRawHashes={n: sha(v.encode()) for n, v in scope.items()},
                       historicalScopeRawHashes={n: sha(v.encode()) for n, v in historical.items()},
                       dependencyRawHashes=contract['dependencyHashes'], syntaxR2Provenance=contract['syntaxR2Provenance'],
                       leaves=contract['leaves'], exclusiveSourceContract=True)
        save(run / 'COMPLETE-SCOPE-START.json', scope)
        save(run / 'HISTORICAL-COMPLETE-BLOCKS.json', historical)
        publish()

        def stable(label):
            live = {name: (ROOT / name).read_bytes() for name in SOURCES}
            for name in SOURCES:
                if name != 'native/main.cpp':
                    need(live[name] == source[name], 'Consumer/dependency source drift ' + label + ': ' + name)
            live_scope = capture_scope(live, helper.mask_cpp)
            need(live_scope == scope, 'Complete tested source scope changed ' + label)
            need(historical_path.read_bytes() == historical_bytes, 'Historical raw source changed ' + label)
            return live, live_scope

        stable('before compilation')
        if args.source_only:
            receipt['status'] = 'SOURCE_VERIFIED_RUNTIME_UNEXECUTED'
            receipt['sourceStable'] = True
            publish()
            return 12
        compiler, env = helper.compiler_environment(run)
        authority = json.loads(source[AUTH])
        pins = {str(Path(row['path']).resolve()).lower(): row['sha256'] for row in authority['files']}
        need(compiler.resolve() == Path(authority['native']['compiler']).resolve(), 'Native compiler path changed')
        for path in (compiler, Path(authority['native']['linker']), Path(authority['native']['dumpbin'])):
            need(sha(path.read_bytes()) == pins[str(path.resolve()).lower()], 'Native tool raw identity changed: ' + str(path))
        env['INCLUDE'] = ';'.join(authority['native']['include'])
        env['LIB'] = ';'.join(authority['native']['lib'])
        receipt['compiler'] = {'path': str(compiler), 'sha256': sha(compiler.read_bytes()),
                              'vcvarsPath': str(helper.VCVARS), 'vcvarsSha256': sha(helper.VCVARS.read_bytes()),
                              'sdkVersion': env.get('WindowsSDKVersion')}
        for profile, flags in [('Release', ['/O2', '/MT']), ('Checked', ['/Od', '/RTC1', '/D_DEBUG', '/MTd'])]:
            for route, blocks, bad_exits in variants(scope, historical):
                stable('before ' + profile + '-' + route)
                folder = run / (profile + '-' + route)
                folder.mkdir()
                cpp, exe, deps = folder / 'gates.cpp', folder / 'gates.exe', folder / 'deps.json'
                globals_text = '\n'.join(v for n, v in scope.items() if n.startswith('global:'))
                body = source[TEMPLATE].decode('utf-8-sig')
                for marker, text in {'GLOBALS': globals_text, 'HOST_PREDICATE': scope['hostLocalPredicate'],
                                     'CAPTURE_PREDICATE': scope['capturePredicate'],
                                     'READY_CASE': blocks['readyCase'], 'MESSAGE_LOOP': blocks['messageLoop']}.items():
                    need(body.count('@' + marker + '@') == 1, 'Unique exclusive fixture marker required: ' + marker)
                    body = body.replace('@' + marker + '@', text, 1)
                need(not re.search(r'@[A-Z_]+@', body), 'Unexpanded exclusive fixture source marker')
                cpp.write_bytes(body.encode())
                # Whole snippets received by the compiler must be byte-identical
                # to the selected actual/historical bodies, not mirrored logic.
                check = main_blocks(body, helper.mask_cpp, 'fixtureWWinMainLoop')
                need(check == blocks, 'Generated actual case/message loop differs from complete selected source')
                need(function(body, 'inputHostExitLocalOwnershipReleased', helper.mask_cpp) == scope['hostLocalPredicate'],
                     'Actual Host-local predicate was changed in fixture')
                need(function(body, 'inputCaptureExitOwnershipReleased', helper.mask_cpp) == scope['capturePredicate'],
                     'Actual capture-return getter was changed in fixture')
                build = helper.execute([compiler, '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/permissive-', '/W4', *flags,
                    '/sourceDependencies', deps, '/I' + str(snapshot / 'deps/json'), '/I' + str(snapshot / 'native'), cpp,
                    '/Fo' + str(folder / 'gates.obj'), '/Fe' + str(exe), '/link', '/INCREMENTAL:NO'], folder, env, folder / 'build.log', 90)
                need(build.returncode == 0, 'Completion-gates consumer compile failed: ' + str(folder / 'build.log'))
                includes = [Path(p).resolve() for p in json.loads(deps.read_text(encoding='utf-8-sig'))['Data']['Includes']]
                expected = {(snapshot / 'native/exit_cleanup_wait.h').resolve(), (snapshot / 'deps/json/json.hpp').resolve()}
                local = {p for p in includes if p.is_relative_to(snapshot.resolve())}
                need(local == expected, 'Actual compiled local dependency closure changed')
                tool_roots = [Path(p).resolve() for p in authority['native']['include']]
                need(all(p in expected or any(p.is_relative_to(root) for root in tool_roots) for p in includes),
                     'Unreviewed dependency outside exact snapshot and approved compiler/SDK')
                need(all(not p.is_relative_to(ROOT) for p in includes), 'Live repository include was compiled')
                observed = helper.execute([exe], folder, env, folder / 'run.log', 45)
                save(folder / 'PROCESS-EXIT.json', {'exitCode': observed.returncode})
                need(bool(observed.stdout.strip()), 'Fixture produced no JSON; process exit=' + str(observed.returncode))
                value = json.loads(observed.stdout.strip().splitlines()[-1])
                row = {'profile': profile, 'route': route, 'isCurrentSource': route == 'current',
                       'exitCode': observed.returncode, **value, 'compiledBlockRawHashes': {n: sha(v.encode()) for n, v in blocks.items()},
                       'compiledHostPredicateRawHash': sha(scope['hostLocalPredicate'].encode()),
                       'compiledLocalRawHashes': {p.relative_to(snapshot.resolve()).as_posix(): sha(p.read_bytes()) for p in local},
                       'cppSha256': sha(cpp.read_bytes()), 'executableSha256': sha(exe.read_bytes()),
                       'sourceDependenciesSha256': sha(deps.read_bytes())}
                receipt['profiles'].append(row)
                publish()
                need(len(value['cases']) == 28 and value['privateThreadsStarted'] == 28, 'Complete declared scenario set not executed')
                need(value['allPrivateResourcesZeroBeforeExit'] and value['privateHandlesRemaining'] == 0
                     and value['privateThreadsStarted'] == value['privateThreadsSignalledAndObserved']
                     and value['privateThreadsStillRunning'] == 0 and value['fixtureForcedRealCleanupCount'] == 0,
                     'Actual private resources were not returned before fixture exit')
                need(value['completeReadyCaseExecuted'] and value['completeWWinMainMessageLoopExecuted']
                     and value['actualHostLocalPredicateExecuted'] and not value['processExitUsedAsCleanup'], 'Actual consumer scope not executed')
                if bad_exits:
                    need(observed.returncode == 1 and not value['pass'] and value['failures'] > 0, 'Specified old/deleted host gate did not fail')
                    for exit_name in bad_exits:
                        prefix = exit_name + ':host-local-not-returned:'
                        need(prefix + 'retain-cleanup-owner' in value['failedChecks'] and
                             prefix + 'do-not-mark-completed' in value['failedChecks'], 'Specified actual host gate defect not witnessed: ' + exit_name)
                        if exit_name == 'ready':
                            need(prefix + 'no-window-or-module-teardown' in value['failedChecks'], 'Deleted READY host gate did not reach forbidden real case teardown')
                    need(all(any(name.startswith(exit_name + ':host-') for exit_name in bad_exits) for name in value['failedChecks']),
                         'Negative failed for unrelated assertions')
                    row['specifiedNegativeObserved'] = True
                else:
                    need(observed.returncode == 0 and value['pass'] and value['failures'] == 0, 'Current actual completion consumers failed')
                stable('after ' + profile + '-' + route)
                row['testedScopeStable'] = True
                publish()
                print(profile + ' ' + route + ': checks=' + str(value['checks']) + ' failures=' + str(value['failures']) +
                      ' exit=' + str(observed.returncode) + ' privateHandles=0', flush=True)
        live, end_scope = stable('at final publication')
        save(run / 'COMPLETE-SCOPE-END.json', end_scope)
        receipt.update(status='PASS_ACTUAL_EXIT_COMPLETION_GATES', runtimeEvidenceComplete=True, sourceStable=True,
                       sourceScopeRawUnchanged=True, sourceEndHashes={n: sha(b) for n, b in live.items()},
                       wholeMainSourceStable=live['native/main.cpp'] == source['native/main.cpp'],
                       finishedAt=dt.datetime.now().astimezone().isoformat())
        receipt['pass'] = True
        publish()
        print(receipt['status'] + ' RESULT=' + str(out / 'RESULT.json'))
        return 0
    except Exception as error:
        receipt.update(status='FAIL_TOOL_SOURCE_OR_ACTUAL_CONSUMER', error=str(error),
                       finishedAt=dt.datetime.now().astimezone().isoformat())
        publish()
        print(str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
