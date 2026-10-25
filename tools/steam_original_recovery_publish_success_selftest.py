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
    path.write_bytes((json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    root = args.evidence.resolve()
    if root != EVIDENCE_ROOT.resolve() and EVIDENCE_ROOT.resolve() not in root.parents:
        parser.error('evidence must stay within the assigned publisher evidence root')
    if root.exists():
        parser.error('evidence directory must be new; preserve previous failures')
    root.mkdir(parents=True)
    summary = {'schema': 'ymcc.original-recovery-publisher-success-audit.v1', 'at': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'success': False}
    try:
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
        fragments = []
        metadata = []
        for anchor in ['bool legacyQualification(', 'bool validHealthyProof(', 'std::string pinValues(', 'bool matchesSavedHealthy(', 'bool snapshot(', 'osd::CallbackResult __fastcall publish(']:
            fragment, proof = extractor.extract(text, anchor)
            fragments.append(fragment)
            metadata.append(proof)
        production = '\n\n'.join(fragments) + '\n'
        save(root / 'extraction.json', {'source': str(engine / 'running-publish-trial.cpp'), 'fragments': metadata, 'bodyEdits': 0})
        compiler, env, toolchain = extractor.toolchain(argparse.Namespace(vc_tools=None, sdk_root='C:/Program Files (x86)/Windows Kits/10', sdk_version='10.0.26100.0'))
        summary['toolchain'] = toolchain
        fixture = Path(__file__).with_suffix('.cpp').read_bytes()
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
            'productionBodiesExecuted': ['legacyQualification', 'validHealthyProof', 'pinValues', 'matchesSavedHealthy', 'snapshot', 'publish'],
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
