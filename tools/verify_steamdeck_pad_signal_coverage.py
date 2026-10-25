#!/usr/bin/env python3
"""Offline production-decoder regressions and mutation controls; never starts Host/SDK.

python tools/verify_steamdeck_pad_signal_coverage.py --output-dir <fresh directory>
Optional --baseline-dir points to this run's frozen before/ tree. Its approved
1.12.0 observer is compiled against FakeSdk; no dependency DLL is loaded.
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SUB = 'InputHost/SteamDeckPadSubmissionEvidence.cs'
CACHE = 'InputHost/SteamDeckPadReportObservation.cs'
OBSERVER = 'InputHost/UsbipTransportObservation.cs'
SUB_TEST = 'tools/steamdeck_pad_submission_selftest.cs'
CACHE_TEST = 'tools/steamdeck_pad_report_observation_selftest.cs'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def changed(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError(f'Mutation anchor changed: {old!r}')
    return text.replace(old, new, 1)


def run_probe(out, name, kind, frozen, mutation=None, observer=None):
    work = out / name
    work.mkdir()
    rels = [SUB, SUB_TEST] if kind == 'submission' else [CACHE, CACHE_TEST, OBSERVER]
    identities = []
    for rel in rels:
        data = observer if rel == OBSERVER and observer is not None else frozen[rel]
        if mutation and rel == (SUB if kind == 'submission' else CACHE):
            data = mutation(data.decode('utf-8-sig')).encode('utf-8')
        (work / Path(rel).name).write_bytes(data)
        identities.append({'path': rel, 'compiledSha256': sha(data), 'productionSha256': sha(frozen[rel]),
                           'mutated': rel == (SUB if kind == 'submission' else CACHE) and mutation is not None})
    defines = '<DefineConstants>USBIP_OBSERVER_FIXTURE</DefineConstants>' if kind == 'cached' else ''
    (work / 'Probe.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>'
        '<OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework>'
        '<ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable>'
        '<UseSharedCompilation>false</UseSharedCompilation><NuGetAudit>false</NuGetAudit>'
        + defines + '</PropertyGroup></Project>\n', encoding='utf-8')
    (work / 'NuGet.Config').write_text('<configuration><packageSources><clear /></packageSources></configuration>\n', encoding='utf-8')
    env = dict(os.environ, DOTNET_CLI_TELEMETRY_OPTOUT='1', DOTNET_NOLOGO='1',
               DOTNET_GENERATE_ASPNET_CERTIFICATE='false')
    build = subprocess.run(['dotnet', 'build', 'Probe.csproj', '-c', 'Release', '--nologo'],
        cwd=work, env=env, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=60)
    (work / 'build.log').write_text(build.stdout + build.stderr, encoding='utf-8')
    if build.returncode:
        raise RuntimeError(f'{name}: compilation failed, not an accepted negative control; see {work / "build.log"}')
    result_path = work / 'result.json'
    run = subprocess.run(['dotnet', str(work / 'bin/Release/net10.0-windows/Probe.dll'), str(result_path)],
        cwd=work, env=env, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=60)
    (work / 'run.log').write_text(run.stdout + run.stderr, encoding='utf-8')
    result = json.loads(result_path.read_text(encoding='utf-8-sig'))
    failures = [c['name'] for c in result['cases'] if not c['ok']]
    accepted = run.returncode == 3 and not result['ok'] and bool(failures) if mutation else run.returncode == 0 and result['ok'] and not failures
    receipt = {'name': name, 'kind': kind, 'accepted': accepted, 'mutation': mutation is not None,
               'exitCode': run.returncode, 'checks': result['checks'], 'failedCases': failures,
               'sources': identities, 'allocationWindows': result['allocationWindows'],
               'signalAllocationWindows': result.get('signalAllocationWindows'),
               'hostExecuted': False, 'actualSdkLoaded': False, 'actualDeviceReads': 0}
    write_json(work / 'receipt.json', receipt)
    print(name, 'PASS' if accepted else 'FAIL', f'checks={result["checks"]}', f'failures={len(failures)}', flush=True)
    if not accepted:
        raise RuntimeError(f'{name}: unexpected result: {failures}')
    return receipt


def gate_audit(out, frozen):
    patterns = {
        'InputHost/ScreenTouchpadReport.cs': [
            'bool observePadData = false', 'controller.SubmitRawReport(report);',
            'ScreenPadSubmissionEvidence.Record(observePadData, report,'],
        'InputHost/Program.cs': [
            'private bool _costFlagEnabled = CostLoggingRequestedByFlag();',
            'SubmitSteamDeckWithScreenPads(_controller, profile, envelope, _costFlagEnabled)',
            'SubmitSteamDeckWithScreenPads(_controller, profile, neutral, _costFlagEnabled, isNeutralCommand: true)',
            'if (!File.Exists(flag)) return false;',
            'if (_msiCostDeferredRequested) MaybeEmitCostWindow();',
            'if (windowMs < 1000) return;',
            '_costFlagEnabled = CostLoggingRequestedByFlag();',
            '_costFlagEnabled = diagnostic?.LoggingEnabled ?? false;',
            'steamDeckPadSubmissions = _tuple?.Persona == "steamdeck" ? ScreenPadSubmissionEvidence.Capture() : null,',
            'SteamDeckPadSubmissions = _costFlagEnabled && _tuple?.Persona == "steamdeck"',
            'if (!loggingRequested || !_assetVerified || _tuple?.Persona != "steamdeck") return null;',
            'CaptureUsbipTransportSafely(phase, CostLoggingRequestedByFlag())'],
        'InputHost/MsiCostWindowDiagnostics.cs': [
            'var enabled = _readFlag();', 'Volatile.Write(ref _loggingEnabled, enabled ? 1 : 0);',
            'steamDeckPadSubmissions = window.SteamDeckPadSubmissions,'],
        'native/main.cpp': [
            'static void inputHostCostFlagSync() {',
            'if (g_domainLogEnabled[1].load(std::memory_order_acquire)) {',
            'const bool unifiedEnabled = logSettingExplicitlyEnabled(section, "virtual");',
            'inputHostCostFlagSync();',
            'addRotated(dataDir + L"\\\\input-host-cost.log");']}
    evidence = []
    for rel, anchors in patterns.items():
        text = frozen[rel].decode('utf-8-sig')
        hits = []
        for anchor in anchors:
            # Match actual C++ literal: two backslashes in source.
            if anchor not in text:
                raise RuntimeError(f'Gating source anchor changed: {rel}: {anchor}')
            line = text[:text.index(anchor)].count('\n') + 1
            hits.append({'line': line, 'source': anchor})
        evidence.append({'path': rel, 'sha256': sha(frozen[rel]), 'anchors': hits})
    program = frozen['InputHost/Program.cs'].decode('utf-8-sig')
    off = re.search(r'if \(!_costFlagEnabled\)\s*\{\s*// MSI.*?if \(_msiCostDeferredRequested\) MaybeEmitCostWindow\(\);\s*return;', program, re.S)
    if not off:
        raise RuntimeError('Off-gate refresh branch changed; re-audit manually')
    screen = frozen['InputHost/ScreenTouchpadReport.cs'].decode('utf-8-sig')
    if screen.index('controller.SubmitRawReport(report);') >= screen.index('ScreenPadSubmissionEvidence.Record(observePadData, report,'):
        raise RuntimeError('Submission observer moved before successful API return')
    audit = {'kind': 'static-production-source-audit; no Host/SDK or flag I/O executed', 'sources': evidence,
        'successfulApiReturnPrecedesRecord': True, 'frameAndNeutralPassCostFlag': True,
        'flagAuthority': 'domainLogs.virtual; explicit opt-in; missing/false => derived flag removed',
        'defaultNormalUseCollectsHostSignalHistory': False,
        'offRecordBehavior': 'No decode/count/edge; clears latest/continuity; retains historical ring until Reset.',
        'enabledBehavior': 'Every successful Host API return decoded; ring holds finite edges; export on existing ~1Hz cost window.',
        'offToOnSynchronous': 'RecordCostFrame returns when cached flag is false; no further flag refresh through this branch. New HostSession or other external refresh is needed.',
        'offToOnDeferredMsi': 'Off frames still reach MaybeEmitCostWindow; worker reads flag on admitted queued windows, later producer windows copy LoggingEnabled. Requires frames and worker progress; no strict latency bound.',
        'onToOff': 'Sampling follows cached gate until flag refresh; after next disabled Record latest/continuity clear. No historical backfill.',
        'cachedObservation': 'Cost window/lifecycle only; also gated; not per-report history or Steam receipt.',
        'exportGap': 'Ordinary export copies existing input-host-cost.log; it does not enable Host recording or recover off-gate history.',
        'controlGatingChanged': False}
    write_json(out / 'GATING-AUDIT.json', audit)
    return audit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--baseline-dir', type=Path)
    args = parser.parse_args()
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    rels = [SUB, CACHE, SUB_TEST, CACHE_TEST, OBSERVER, 'InputHost/ScreenTouchpadReport.cs',
            'InputHost/Program.cs', 'InputHost/MsiCostWindowDiagnostics.cs', 'native/main.cpp',
            'InputHost/YeManInputHost.csproj', 'deps/hidmaestro/dependency-lock.json',
            'tools/verify_steamdeck_pad_signal_coverage.py']
    frozen = {rel: (ROOT / rel).read_bytes() for rel in rels}
    for rel, data in frozen.items():
        dst = out / 'source' / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(data)
    result = {'startedAtUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'ok': False,
        'hostExecuted': False, 'actualSdkLoaded': False, 'devicePathExecuted': False,
        'steamConsumptionProven': False, 'profiles': [], 'negativeControls': []}
    try:
        audit = gate_audit(out, frozen)
        result['defaultNormalUseCollectsHostSignalHistory'] = audit['defaultNormalUseCollectsHostSignalHistory']
        result['profiles'].append(run_probe(out, 'submission-release', 'submission', frozen))
        result['profiles'].append(run_probe(out, 'cached-current-observer', 'cached', frozen))
        if args.baseline_dir:
            original_observer = (args.baseline_dir / OBSERVER).read_bytes()
            result['profiles'].append(run_probe(out, 'cached-frozen-1.12-observer', 'cached', frozen, observer=original_observer))
        mutants = [
            ('no-button-word-edge', 'submission', lambda s: changed(s, 'bits != _previous.ButtonBits || ', '')),
            ('no-stick-class-edge', 'submission', lambda s: changed(s, 'row.StickClassBits != _previous.StickClassBits || ', '')),
            ('no-IMU-component-edge', 'submission', lambda s: changed(s, 'row.ImuNonzeroMask != _previous.ImuNonzeroMask ||', 'false ||')),
            ('IMU-six-components-only', 'submission', lambda s: changed(s, 'axis < 10;', 'axis < 6;')),
            ('no-pressure-zero-edge', 'submission', lambda s: changed(s, 'row.PressureNonzeroMask != _previous.PressureNonzeroMask || ', '')),
            ('no-pressure-domain-edge', 'submission', lambda s: changed(s, 'row.PressureOutOfRangeMask != _previous.PressureOutOfRangeMask ||', 'false ||')),
            ('no-inactive-residue-edge', 'submission', lambda s: changed(s, 'row.InactivePadDataMask != _previous.InactivePadDataMask ||', 'false ||')),
            ('stick-deadzone-upper-exclusive', 'submission', lambda s: changed(s, '<= 4096 => 0', '< 4096 => 0')),
            ('cached-stick-offset-swapped', 'cached', lambda s: changed(s, 'LeftStickX = I16(bytes, 48), LeftStickY = I16(bytes, 50)', 'LeftStickX = I16(bytes, 50), LeftStickY = I16(bytes, 48)')),
            ('cached-truncated-button-word', 'cached', lambda s: changed(s, 'ButtonBits = bits,', 'ButtonBits = (uint)bits,')),
            ('cached-six-IMU-components', 'cached', lambda s: changed(s, 'axis < 10;', 'axis < 6;')),
            ('cached-wrong-trigger-bit', 'cached', lambda s: changed(s, 'LeftTriggerButton => ButtonBits is ulong b ? (b & (1UL << 1))', 'LeftTriggerButton => ButtonBits is ulong b ? (b & (1UL << 0))')),
        ]
        for name, kind, mutate in mutants:
            result['negativeControls'].append(run_probe(out, name, kind, frozen, mutate))
        # Changes made by a concurrent task are never silently accepted into a receipt.
        stable = [{'path': rel, 'sha256': sha(data), 'unchangedDuringRun': (ROOT / rel).read_bytes() == data}
                  for rel, data in frozen.items()]
        result['sources'] = stable
        if not all(row['unchangedDuringRun'] for row in stable):
            raise RuntimeError('A source changed during this run; preserve evidence and re-run against a fresh snapshot')
        result['ok'] = True
    finally:
        result['finishedAtUtc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        write_json(out / 'VERIFICATION.json', result)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
