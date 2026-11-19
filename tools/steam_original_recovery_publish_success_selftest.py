#!/usr/bin/env python3
"""Run exact production publisher callback/guards in owned memory, without Steam.

Requires a fresh evidence subdirectory of the assigned publisher evidence root.
The Steam P2 function itself, process identities, dispatcher/handles, and native
Steam outcome are NOT validated by this fixture. No product build script runs.
"""
from __future__ import annotations
import argparse
import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import traceback

EVIDENCE_ROOT = Path('G:/YeManCC-Work/Testing/YMCC/Evidence/SuccessChainRetention-20261023/publisher')
RECEIPTS = [
    Path('D:/Codex/2026-10-21/steam-ai-steam-running-steam-api/work/observer-readiness/single-publish-recovery-01/receipt.json'),
    Path('D:/Codex/2026-10-21/steam-ai-steam-running-steam-api/work/warm-source-context-20261022/candidate-recovery-trial/receipt.json'),
]


def pin(path: Path) -> dict:
    raw = path.read_bytes()
    return {'path': str(path.resolve()), 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}


def save(path: Path, value: object) -> None:
    save_bytes(path, (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))



REVOCATION_EVIDENCE_ROOT = Path('G:/YeManCC-Work/Testing/YMCC/Evidence/SteamUnifiedChannel-20261112/NativeReadScope-20261113/FinalPublisherRevocation-20261113')


def save_bytes(path: Path, raw: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())


# Fixed repository fixture closure; no per-run source/Evidence read is needed.
LEGACY_ABI6_SHAPE_FIXTURE = Path('tools/steam_original_recovery_legacy_abi6_role_group_shape_fixture.inc')
LEGACY_ABI6_SHAPE_INPUTS = [
    Path('native/steam_original_recovery_engine/running-publish-trial.h'),
    Path('native/steam_game_role_contract.h'),
]
# Explicit independent fixture fact: selecting a route does not prove the fixed
# original module pair. This is the same false fact used by the sealed fixture.
PUBLISHER_FIXTURE_FACTS = [{
    'name': 'fixedOriginalQualification', 'cpp': 'bool fixedOriginalQualification=false;',
    'value': False, 'routeDerived': False,
    'meaning': 'initial false; the explicit original case supplies independently disk-qualified and simulated-loaded fixed/content module facts before selecting each route',
}]


def publisher_fragments(text: str, extractor) -> tuple[str, list[dict]]:
    fragments, metadata = [], []
    for anchor in ['ProcessReadViews qualifiedProcessReadViews(', 'bool legacyQualification(', 'bool rawQualification(', 'bool validHealthyProof(', 'std::string pinValues(',
                   'bool matchesSavedHealthy(', 'bool targetRolesMatch(', 'bool groupCancellationCurrent(', 'bool snapshot(', 'osd::CallbackResult __fastcall publish(']:
        body, proof = extractor.extract(text, anchor)
        fragments.append(body)
        metadata.append(proof)
    production = 'using ProcessReadViews=ymcc::steamdiagnostic::ReadViews;\n' + PUBLISHER_FIXTURE_FACTS[0]['cpp'] + '\n' + '\n\n'.join(fragments) + '\n'
    return production, metadata


def publication_revocation_scope(args: argparse.Namespace, repo: Path, root: Path, summary: dict) -> int:
    sys.dont_write_bytecode = True
    helper = repo / 'tools/steam_original_recovery_source_context_selftest.py'
    spec = importlib.util.spec_from_file_location('publisher_exact_extractor', helper)
    extractor = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(extractor)
    source_roots = [p.resolve() for p in args.source_root] + [repo]
    originals = {}
    inputs = {}

    def snapshot(relative: Path) -> Path:
        relative = Path(os.path.normpath(str(relative)))
        if relative.is_absolute() or '..' in relative.parts:
            raise RuntimeError('dependency escaped the source tree: ' + str(relative))
        key = relative.as_posix()
        if key in inputs:
            return inputs[key]
        actual = next((base / relative for base in source_roots if (base / relative).is_file()), None)
        if actual is None:
            raise RuntimeError('missing candidate/repository input: ' + key)
        raw = actual.read_bytes()
        destination = root / 'inputs' / relative
        save_bytes(destination, raw)
        originals[actual] = raw
        inputs[key] = destination
        if relative.suffix in {'.h', '.hpp', '.cpp', '.inc'}:
            for quoted in re.findall(r'^\s*#\s*include\s*"([^"\r\n]+)"', raw.decode('utf-8-sig'), re.M):
                if quoted == 'production-publisher.inc' and relative == Path('tools/steam_original_recovery_publish_success_selftest.cpp'):
                    if not (root / 'baseline/production-publisher.inc').is_file():
                        raise RuntimeError('missing declared generated production include')
                    continue
                candidates = [relative.parent / quoted, Path('native/steam_original_recovery_engine') / quoted, Path('native/steam_original_recovery_engine/vendor') / quoted]
                dependency = next((p for p in candidates if any((base / p).is_file() for base in source_roots)), None)
                if dependency is None:
                    raise RuntimeError('unresolved quoted include: ' + key + ': ' + quoted)
                snapshot(dependency)
        return destination

    engine_relative = Path('native/steam_original_recovery_engine')
    trial = snapshot(engine_relative / 'running-publish-trial.cpp')
    snapshot(engine_relative / 'running-publish-trial.h')
    snapshot(engine_relative / 'publication_revocation.h')
    text = trial.read_text(encoding='utf-8-sig')
    production, metadata = publisher_fragments(text, extractor)
    baseline = root / 'baseline'
    save_bytes(baseline / 'production-publisher.inc', production.encode('utf-8'))
    fixture = snapshot(Path('tools/steam_original_recovery_publish_success_selftest.cpp'))
    case_source = snapshot(Path('tools/steam_original_recovery_publication_revocation_cases.inc'))
    legacy_shape = snapshot(LEGACY_ABI6_SHAPE_FIXTURE)
    for dependency in LEGACY_ABI6_SHAPE_INPUTS:
        snapshot(dependency)
    save_bytes(baseline / 'cases.cpp', fixture.read_bytes())
    save_bytes(baseline / case_source.name, case_source.read_bytes())
    save_bytes(baseline / legacy_shape.name, legacy_shape.read_bytes())
    qualified_model = snapshot(Path('tools/steam_original_recovery_publication_qualified_model.inc'))
    save_bytes(baseline / qualified_model.name, qualified_model.read_bytes())
    targets = [{'label': 'owned-sparse-fixture', 'gameId': str((0x81234567 << 32) | (2 << 24)),
                'originalPid': 4242, 'callbackFlagsAfter': 4, 'evidenceAt': 'controlled-fixture-input'}]
    save(root / 'historical-targets.json', targets)
    save(root / 'extraction.json', {'scope': args.scope, 'source': pin(trial), 'fragments': metadata, 'bodyEdits': 0, 'fixtureFacts': PUBLISHER_FIXTURE_FACTS})
    save(root / 'source-pins.json', [dict(pin(p), relativePath=str(p.relative_to(base)))
                                  for p in originals for base in source_roots if p.is_relative_to(base)][:len(originals)])
    summary.update(scope=args.scope, selectedCase=args.case, legacyQualificationParameter=args.legacy_qualification,
                   qualificationModuleInputs=[pin(p) for p in (args.client_module, args.ui_module) if p is not None],
                   sourceRoots=list(map(str, source_roots)),
                   runnerInputs=[pin(Path(__file__)), pin(fixture), pin(case_source), pin(legacy_shape), pin(helper)],
                   legacyShapeClosure=[str(LEGACY_ABI6_SHAPE_FIXTURE), *map(str, LEGACY_ABI6_SHAPE_INPUTS)],
                   fixtureFacts=PUBLISHER_FIXTURE_FACTS,
                   sourceInputs=[pin(p) for p in originals], preparedOnly=args.prepare_only,
                   oldDefaultCasesExecuted=False, compilerInvoked=False, behaviorExecuted=False,
                   generatedCpp=pin(baseline / 'cases.cpp'), generatedProduction=pin(baseline / 'production-publisher.inc'),
                   outputDirectory=str(baseline), budgetAuthority='caller formal owned Job; preparer does not create an execution budget')
    summary['coverageLimits'] = {
        'realWin32Objects': ['fixture actual owner process', 'manual-reset power/identity source events', 'target duplicate handles',
                             'same fixture EXE inert owner child', 'single inherited release event via HANDLE_LIST',
                             'actual child PID/creation time and normal process return'],
        'ports': ['readLocal=owned sparse memory', 'identity=fixture boolean', 'GetTickCount64=fixture ticks', 'SDK publisher=capturing stub'],
        'exactBodies': [m['anchor'] for m in metadata],
        'notCovered': ['actual Steam SDK/machine code', 'real remote owner/dispatcher and DLL prepare',
                       'actual Native power producer', 'physical sleep/game/Steam', 'complete paired engine/host/runtime'],
        'gameInstancesStarted': 0, 'steamCalls': 0,
    }
    summary['sourceDrift'] = [str(p) for p, raw in originals.items() if p.read_bytes() != raw]
    if summary['sourceDrift']:
        raise RuntimeError('prepared source changed; preserve this snapshot and qualify a successor')
    if args.prepare_only:
        summary.update(success=True, status='PREPARED_NOT_EXECUTED', testsExecuted=0)
        save(root / 'summary.json', summary)
        print(json.dumps({'success': True, 'status': summary['status'], 'scope': args.scope,
                          'evidence': str(root), 'cpp': str(baseline / 'cases.cpp'),
                          'compilerInvoked': False, 'testsExecuted': 0}, ensure_ascii=False))
        return 0
    # Execution remains explicit; this authoring task uses --prepare-only only.
    compiler, env, toolchain = extractor.toolchain(argparse.Namespace(vc_tools=None,
        sdk_root='C:/Program Files (x86)/Windows Kits/10', sdk_version='10.0.26100.0'))
    summary.update(compilerInvoked=True, toolchain=toolchain)
    command = [str(compiler), '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/MT', '/O2', '/W4', '/WX', '/bigobj',
               '/showIncludes', '/I' + str(root / 'inputs/native/steam_original_recovery_engine'),
               'cases.cpp', '/Focases.obj', '/Fecases.exe', '/link', 'bcrypt.lib', '/INCREMENTAL:NO', '/DYNAMICBASE', '/NXCOMPAT']
    env['TMP'] = env['TEMP'] = str(baseline)
    built = extractor.run(command, baseline, 'compile', env)
    if built.returncode:
        raise RuntimeError('publication-revocation compile failed; see preserved compile.stdout')
    execute = [str(baseline / 'cases.exe'), str(root / 'historical-targets.json'), '--scope', args.scope]
    if args.case:
        execute += ['--case', args.case]
    if args.legacy_qualification is not None:
        execute += ['--legacy-qualification', args.legacy_qualification]
        execute += ['--client-module', str(args.client_module), '--ui-module', str(args.ui_module)]
    executed = extractor.run(execute, baseline, 'execute', env)
    summary['behaviorExecuted'] = True
    result = json.loads(executed.stdout.decode('utf-8-sig'))
    save(baseline / 'result.json', result)
    summary['runs'] = [{'name': args.scope, 'returnCode': executed.returncode, 'tests': result['tests'],
                        'passed': result['passed'], 'checks': result['checks']}]
    if args.case and ([item['name'] for item in result['cases']] != [args.case] or result['tests'] != 1):
        raise RuntimeError('actual selected case list differs from the requested original case')
    if executed.returncode != 0 or not result['success'] or not result['tests']:
        raise RuntimeError('publication-revocation behavior failed; original raw retained')
    summary.update(success=True, status='EXPLICIT_SELECTED_EXECUTION')
    save(root / 'summary.json', summary)
    print(json.dumps({'success': True, 'scope': args.scope, 'evidence': str(root), 'runs': summary['runs']}, ensure_ascii=False))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--scope', choices=['all', 'publication-revocation'], default='all')
    parser.add_argument('--case', default='')
    parser.add_argument('--legacy-qualification', choices=['fixed', 'content'], help='Independent qualification fact for the one original revocation case')
    parser.add_argument('--client-module', type=Path)
    parser.add_argument('--ui-module', type=Path)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--repository-root', type=Path)
    parser.add_argument('--source-root', action='append', type=Path, default=[])
    parser.add_argument('--driver-cleanup-workspace', type=Path, help='Explicit generated workspace for the exact driver cleanup prepare-only case')
    args = parser.parse_args()
    if args.driver_cleanup_workspace is not None and (args.scope != 'publication-revocation' or args.case != 'driver-cleanup-diagnostic-unwind'):
        parser.error('--driver-cleanup-workspace requires the exact publication-revocation driver cleanup case')
    if any(option == '--case' or option.startswith('--case=') for option in sys.argv[1:]) and not args.case:
        parser.error('empty explicit case selection')
    if args.legacy_qualification is not None and (args.scope != 'publication-revocation' or args.case != 'publication-revocation-real-source-preserves-original-full-gameid'):
        parser.error('qualification mode requires the exact original publication revocation case')
    if (args.legacy_qualification is not None) != (args.client_module is not None and args.ui_module is not None):
        parser.error('qualification mode requires both explicit module input paths')
    if args.legacy_qualification is None and (args.client_module is not None or args.ui_module is not None):
        parser.error('module paths require explicit qualification mode')
    repo = (args.repository_root or Path(__file__).resolve().parents[1]).resolve()
    if args.prepare_only and args.scope != 'publication-revocation':
        parser.error('--prepare-only requires explicit --scope publication-revocation')
    if args.scope == 'all' and (args.source_root or args.case):
        parser.error('source overlays and --case are explicit publication-revocation scope only')
    if args.scope == 'publication-revocation' and args.case == 'driver-cleanup-diagnostic-unwind':
        sys.dont_write_bytecode = True
        helper = Path(__file__).with_name('steam_original_recovery_driver_cleanup_fixture.py')
        spec = importlib.util.spec_from_file_location('original_driver_cleanup_fixture', helper)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        args._driver_entry = str(Path(__file__).resolve())
        return module.prepare(args, repo)
    root = args.evidence.resolve()
    allowed_root = REVOCATION_EVIDENCE_ROOT.resolve() if args.scope == 'publication-revocation' else EVIDENCE_ROOT.resolve()
    if root != allowed_root and allowed_root not in root.parents:
        parser.error('evidence must stay within the assigned publisher evidence root')
    if root.exists():
        parser.error('evidence directory must be new; preserve previous failures')
    root.mkdir(parents=True)
    summary = {'schema': 'ymcc.original-recovery-publisher-success-audit.v1', 'at': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'success': False}
    try:
        if args.scope == 'publication-revocation':
            return publication_revocation_scope(args, repo, root, summary)
        # Reuse the existing complete-body extractor and compiler discovery.
        # Suppress import bytecode so this runner writes only its assigned paths.
        sys.dont_write_bytecode = True
        helper = repo / 'tools/steam_original_recovery_source_context_selftest.py'
        spec = importlib.util.spec_from_file_location('publisher_exact_extractor', helper)
        extractor = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(extractor)
        summary['runnerInputs'] = [pin(Path(__file__)), pin(Path(__file__).with_suffix('.cpp')), pin(helper)]
        engine = repo / 'native/steam_original_recovery_engine'
        originals: dict[Path, bytes] = {}

        def snapshot(path: Path) -> None:
            path = path.resolve()
            if path in originals:
                return
            relative = path.relative_to(repo)
            raw = path.read_bytes()
            originals[path] = raw
            dest = root / 'inputs' / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(raw)
            if path.suffix not in {'.h', '.hpp', '.cpp', '.inc'}:
                return
            for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"', raw.decode('utf-8-sig'), re.M):
                if name == 'production-publisher.inc' and relative == Path('tools/steam_original_recovery_publish_success_selftest.cpp'):
                    continue  # separately generated for baseline and existing variants
                dependency = path.parent / name
                if not dependency.is_file():
                    dependency = engine / 'vendor' / name
                if not dependency.is_file():
                    raise RuntimeError(f'unresolved quoted include: {path}: {name}')
                snapshot(dependency)

        for name in ['generic_trial_offline.cpp', 'running-publish-trial.cpp', 'running-publish-trial.h', 'Test.cmd', 'Build-Trial.cmd', 'sourcepins.json', 'embedded_trial.h', 'embed-trial.mjs']:
            snapshot(engine / name)
        initial_pins = [{'relativePath': str(p.relative_to(repo)), 'sha256': hashlib.sha256(raw).hexdigest(), 'bytes': len(raw)} for p, raw in originals.items()]
        save(root / 'source-pins.json', initial_pins)
        summary['producerSnapshotFiles'] = len(originals)
        summary['historicalReceipts'] = [pin(p) for p in RECEIPTS]
        receipts = [json.loads(p.read_bytes().decode('utf-8-sig')) for p in RECEIPTS]
        first = receipts[0]
        later = receipts[1]
        first_driver = next(e['result'] for e in first['driverEvents'] if 'result' in e and e['result'].get('publisherCalls') == 1)
        later_driver = later['productSummary']['driverResult']
        if not (first['sameOriginalCohort'] and not first['newGamePidsAtEnd'] and first_driver['publisherCalls'] == 1 and first_driver['directPidWrites'] == 0):
            raise RuntimeError('first success historical identity/call evidence mismatch')
        if not (later['productSummary']['status'] == 'recovered-native' and later['noExtraGameStartObserved'] and later_driver['publisherCalls'] == 1 and later_driver['directPidWrites'] == 0):
            raise RuntimeError('later success historical identity/call evidence mismatch')
        targets = [
            {'label': 'first-success', 'gameId': first['uiAfter']['gameid'], 'originalPid': first['before']['game']['pid'], 'callbackFlagsAfter': first_driver['statusAfterDisarm']['flagsAfterImmediate'], 'evidenceAt': first['finishedAt']},
            {'label': 'later-qualified-success', 'gameId': later['productSummary']['targetGameId'], 'originalPid': later_driver['gamePid'], 'callbackFlagsAfter': later_driver['statusAfterDisarm']['flagsAfterImmediate'], 'evidenceAt': later['finishedAt']},
        ]
        save(root / 'historical-targets.json', targets)
        summary['historicalTargets'] = targets
        summary['firstHistorySourceHashVerdicts'] = [dict(pin(RECEIPTS[0].parent.parent / name), verdict='PRESENT_AND_HASHED_NOW__FIRST_EXECUTION_BYTE_BINDING_UNPROVEN') for name in ['running-publish-trial.cpp', 'running-publish-trial.h', 'running-publish-driver.cpp', 'build-running-publish-trial.cmd', 'build/running-publish-trial.dll']]
        summary['firstReceiptHashBindingLimit'] = 'Receipt records native Steam module hashes and observed success, but no publisher source/DLL hash. Current surviving history-file hashes are not asserted as the exact first executed build.'
        summary['historicalImmediateState'] = {'firstCallbackFlags': first_driver['statusAfterDisarm']['flagsAfterImmediate'], 'firstLaterObservedFlags': first['nativeAfter']['nativeUi']['nativeAppFlags'], 'laterCallbackFlags': later_driver['statusAfterDisarm']['flagsAfterImmediate'], 'laterObservedFlags': later['nativeAfter']['value']['nativeUi']['nativeAppFlags']}
        generic = originals[(engine / 'generic_trial_offline.cpp').resolve()].decode('utf-8-sig')
        summary['existingCoverageAudit'] = {
            'genericNamedCases': len(re.findall(r' test\(', generic)),
            'genericRunningTrialControlCalls': len(re.findall(r'\bRunningTrialControl\(', generic)),
            'genericDirectPublishCalls': len(re.findall(r'\bpublish\(', generic)),
            'genericScope': 'request validation, early DLL prepare rejection, saved pins, deadline helpers; source audit only',
            'TestCmd': 'builds engine offline_tests; does not build generic_trial_offline or execute native publisher',
            'BuildTrialCmd': 'builds DLL and rewrites shared embedded_trial.h; intentionally not executed',
        }
        text = originals[(engine / 'running-publish-trial.cpp').resolve()].decode('utf-8-sig')
        production, metadata = publisher_fragments(text, extractor)
        save(root / 'extraction.json', {'source': str(engine / 'running-publish-trial.cpp'), 'fragments': metadata, 'bodyEdits': 0})
        compiler, env, toolchain = extractor.toolchain(argparse.Namespace(vc_tools=None, sdk_root='C:/Program Files (x86)/Windows Kits/10', sdk_version='10.0.26100.0'))
        summary['toolchain'] = toolchain
        for relative in [Path('tools/steam_original_recovery_publish_success_selftest.cpp'), Path('tools/steam_original_recovery_publication_revocation_cases.inc'), LEGACY_ABI6_SHAPE_FIXTURE, *LEGACY_ABI6_SHAPE_INPUTS]:
            snapshot(repo / relative)
        fixture = originals[(repo / 'tools/steam_original_recovery_publish_success_selftest.cpp').resolve()]
        case_bytes = originals[(repo / 'tools/steam_original_recovery_publication_revocation_cases.inc').resolve()]
        legacy_bytes = originals[(repo / LEGACY_ABI6_SHAPE_FIXTURE).resolve()]
        summary['runnerInputs'].extend([pin(repo / 'tools/steam_original_recovery_publication_revocation_cases.inc'), pin(repo / LEGACY_ABI6_SHAPE_FIXTURE)])
        summary['fixtureFacts'] = PUBLISHER_FIXTURE_FACTS
        summary['legacyShapeClosure'] = [str(LEGACY_ABI6_SHAPE_FIXTURE), *map(str, LEGACY_ABI6_SHAPE_INPUTS)]
        mutations = [
            ('full-gameid-narrowed', 'const auto fullGameId=bound.gameId;', 'const auto fullGameId=std::uint64_t(static_cast<std::uint32_t>(bound.gameId));', 'first-success-full-GameID-originalPID-no-second-instance-no-primary-substitution'),
            ('primary-guard-removed', '||t.primary!=bound.gamePid', '', 'second-instance-primary-cannot-substitute-originalPID'),
            ('wrong-publisher-receiver', 'originalUser.user+0x3800', 'originalUser.user+0x3808', 'first-success-full-GameID-originalPID-no-second-instance-no-primary-substitution'),
            ('synchronous-ui-required', 'const bool good=snapshot(false);', 'const bool good=snapshot(false)&&live.flagsAfter==8196;', 'first-success-full-GameID-originalPID-no-second-instance-no-primary-substitution'),
        ]
        runs = [('baseline', production, None)]
        for name, before, after, caught_by in mutations:
            if production.count(before) != 1:
                raise RuntimeError('mutation anchor changed: ' + name)
            runs.append((name, production.replace(before, after), caught_by))
        results = []
        for name, body, caught_by in runs:
            run_root = root / name
            run_root.mkdir()
            (run_root / 'production-publisher.inc').write_bytes(body.encode('utf-8'))
            (run_root / 'cases.cpp').write_bytes(fixture)
            save_bytes(run_root / 'steam_original_recovery_publication_revocation_cases.inc', case_bytes)
            save_bytes(run_root / LEGACY_ABI6_SHAPE_FIXTURE.name, legacy_bytes)
            build = [str(compiler), '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/MT', '/O2', '/W4', '/WX', '/bigobj',
                     '/I' + str(root / 'inputs/native/steam_original_recovery_engine'), 'cases.cpp', '/Focases.obj', '/Fecases.exe', '/link', 'bcrypt.lib', '/INCREMENTAL:NO', '/DYNAMICBASE', '/NXCOMPAT']
            run_env = env.copy()
            run_env['TMP'] = run_env['TEMP'] = str(run_root)
            built = extractor.run(build, run_root, 'compile', run_env)
            if built.returncode:
                raise RuntimeError(f'{name} compile failed; see preserved compile.stdout')
            executed = extractor.run([str(run_root / 'cases.exe'), str(root / 'historical-targets.json')], run_root, 'execute', run_env)
            result = json.loads(executed.stdout.decode('utf-8-sig'))
            save(run_root / 'result.json', result)
            if caught_by is None:
                if executed.returncode != 0 or not result['success']:
                    raise RuntimeError('baseline failed; see baseline/result.json')
            elif executed.returncode != 1 or not any(c['name'] == caught_by and not c['passed'] for c in result['cases']):
                raise RuntimeError(f'mutation was not caught by required case: {name}')
            results.append({'name': name, 'tests': result['tests'], 'passed': result['passed'], 'checks': result['checks'], 'returnCode': executed.returncode, 'requiredFailureCase': caught_by})
        summary['runs'] = results
        summary['sourceDrift'] = [str(p) for p, raw in originals.items() if p.read_bytes() != raw]
        if summary['sourceDrift']:
            raise RuntimeError('producer input changed during run; snapshot retained, rerun current inputs')
        summary['coverageLimits'] = {
            'productionBodiesExecuted': ['legacyQualification', 'validHealthyProof', 'pinValues', 'matchesSavedHealthy', 'targetRolesMatch', 'groupCancellationCurrent', 'snapshot', 'publish'],
            'productionRecordAndUiReadersExecuted': True,
            'ports': ['readLocal: owned sparse memory', 'identity: fixture boolean', 'GetTickCount64: deterministic fixture ticks', 'native Publisher: capturing stub with supplied UI outcome'],
            'actualSteamP2BodySafeWithoutRemoteSteam': False,
            'reason': 'Private Steam publisher requires qualified native CUser/module/thread context; this runner executes the product callback against a capturing stub only.',
            'notCovered': ['native Steam P2 machine code or semantics', 'process/handle identity and Osd dispatcher/orchestration (parent fixture)', 'loaded-image qualification', 'real UI/controller/Continue/input', 'sleep/devices', 'live recovery'],
            'gameInstancesStarted': 0, 'steamCalls': 0, 'productionPrimaryPidWrites': 0,
        }
        summary['success'] = True
    except Exception as error:
        summary['error'] = str(error)
        (root / 'failure.txt').write_bytes(traceback.format_exc().encode('utf-8'))
    save(root / 'summary.json', summary)
    report = ['Publisher success retention sidecar', '',
              f"Success: {summary['success']}",
              'Existing generic tests never invoke publish(); this adds execution of exact current production callback and snapshot bodies.',
              'Historical receipts supply only full type2 GameID/original PID fixture data; hashes preserve their provenance.',
              'Fixture identity/memory/clock/native-call ports are explicit. Historical callback flags remain 4; later observed flags are 8196. Supplied native-call outcomes are not Steam recovery.',
              'Actual private Steam P2 machine code was not executed and cannot be safely established by this owned fixture.',
              'Full process/handle/dispatcher orchestration belongs to the parent fixture. No product or build/release gate was changed.',
              'See source-pins.json, extraction.json, baseline/result.json and each mutation result/log for exact inputs and checks.']
    (root / 'REPORT.md').write_bytes(('\n'.join(report) + '\n').encode('utf-8'))
    print(json.dumps({'success': summary['success'], 'evidence': str(root), 'runs': summary.get('runs'), 'error': summary.get('error')}, ensure_ascii=False))
    return 0 if summary['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
