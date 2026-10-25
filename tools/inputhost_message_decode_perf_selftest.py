#!/usr/bin/env python3
"""R24-H1: source-extracted MessageCodec correctness and permission-gated measurement.

Default execution only verifies correctness, in Release AND Debug. No production
project is compiled or loaded. All generated files stay under decode-worker.
Measurement requires a separate explicit user release and BOTH CLI switches.
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import uuid
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
ALLOWED_OUTPUT = Path('G:/YeManCC-Work/Build/Validation/VirtualInputPerf-R24-20261015/decode-worker')
AUDIT = Path('G:/YeManCC-Work/Build/Validation/VirtualInputPerf-R23-20261015/host-sidecar-audit.md')
FIXTURE = ROOT / 'tools/inputhost_message_decode_perf_selftest.cs'
# Frozen before any measurements. A failed gate remains a failed gate.
CRITERIA = {
    'version': 'R24-H1-predeclared-v1',
    'measurementPermission': 'Explicit user release required; no benchmark executed by verify/prepare.',
    'scope': 'Extracted ReadAsync only; deterministic synchronous read seam copies the same input bytes in both arms. No overall controller or product CPU claim.',
    'runtime': 'Microsoft.NETCore.App 10.0.11 x64; tiered compilation and tiered PGO enabled',
    'rounds': 11,
    'order': 'AB on even zero-based rounds; BA on odd rounds; every scenario has 11 paired rounds',
    'warmupOperationsPerArmPerScenario': 10000,
    'eligibleGatesPerScenario': {
        'minimumCandidateFasterPairs': 10,
        'minimumMedianPairedSavingsFractionOfBaselineMedian': 0.05,
        'maximumMedianCandidateToBaselineRatio': 0.95,
        'maximumCandidateAllocatedBytesPerOperationRelativeToBaseline': 1.0,
        'allocationRule': 'Candidate <= baseline in every pair, no tolerance; equal is acceptable for a copy-only change.',
    },
    'fragmentedControls': {
        'copyBypassEligible': False,
        'speedupRequired': False,
        'maximumMedianCandidateToBaselineRatio': 1.05,
        'allocationRule': 'Candidate <= baseline in every pair, no tolerance.',
        'report': 'Separately report unchanged accumulation path, all raw pairs, and any regression. Never pool into eligible-path gains.',
    },
    'cases': [
        {'name': 'complete-short-64', 'bytes': 64, 'chunkBytes': 64, 'iterations': 200000, 'eligible': True},
        {'name': 'complete-frame-sized-768', 'bytes': 768, 'chunkBytes': 768, 'iterations': 100000, 'eligible': True},
        {'name': 'complete-large-8192', 'bytes': 8192, 'chunkBytes': 8192, 'iterations': 40000, 'eligible': True},
        {'name': 'complete-cap-16384', 'bytes': 16384, 'chunkBytes': 16384, 'iterations': 20000, 'eligible': True},
        {'name': 'fragmented-large-8192', 'bytes': 8192, 'chunkBytes': 1024, 'iterations': 40000, 'eligible': False},
        {'name': 'fragmented-cap-16384', 'bytes': 16384, 'chunkBytes': 1024, 'iterations': 20000, 'eligible': False},
    ],
    'statistics': 'Per pair: ns/op and allocated bytes/op. Paired median = median(baseline ns/op - candidate ns/op); ratio = median(candidate ns/op / baseline ns/op). All eligible gates must pass separately for every eligible case. Fragmented regression gates are separate and also required for overall local acceptance.',
    'excludedFromMeasurement': 'JIT warmup, construction, reflection, source extraction, correctness cases, real pipe setup, and transport timing. Real Windows message-mode pipe tests prove boundaries separately; seam timing cannot establish in-product eligibility frequency or overall CPU savings.',
    'noAdaptiveChanges': 'No changing round count, warmup, workload, thresholds, dropping pairs, retry-selecting wins, or lowering gates after seeing data.',
}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def dump(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def extract_block(text: str, marker: str) -> tuple[str, int, int]:
    assert text.count(marker) == 1, f'Expected unique source marker: {marker}'
    begin = text.index(marker)
    opening = text.index('{', begin)
    # Ignore C# comments and string/character literals when matching braces.
    tokens = re.compile(r'//[^\n]*|/\*.*?\*/|@"(?:""|[^"])*"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
    depth = 0
    for match in tokens.finditer(text, opening):
        if match[0] == '{':
            depth += 1
        elif match[0] == '}':
            depth -= 1
            if depth == 0:
                return text[begin:match.end()], begin, match.end()
    raise AssertionError(f'Unclosed C# block: {marker}')


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, default=ALLOWED_OUTPUT)
    ap.add_argument('--mode', choices=('prepare', 'verify', 'benchmark'), default='verify')
    ap.add_argument('--measurement-authorized', action='store_true', help='Use only after the user explicitly releases measurement permission.')
    args = ap.parse_args()
    if args.mode == 'benchmark' and not args.measurement_authorized:
        ap.error('BENCHMARK_LOCKED: explicit user release and --measurement-authorized required')
    if args.measurement_authorized and args.mode != 'benchmark':
        ap.error('--measurement-authorized only belongs to benchmark mode')
    output = args.output.resolve()
    allowed = ALLOWED_OUTPUT.resolve()
    if output != allowed and allowed not in output.parents:
        ap.error(f'Output must stay within {allowed}')
    if sys.platform != 'win32':
        ap.error('Windows is required for true private message-mode pipe correctness')
    output.mkdir(parents=True, exist_ok=True)
    out = output / (datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + args.mode + '-' + uuid.uuid4().hex[:8])
    out.mkdir()
    source_path = ROOT / 'InputHost/Program.cs'
    source_bytes = source_path.read_bytes()
    source = source_bytes.decode('utf-8-sig').replace('\r\n', '\n')
    codec, start, end = extract_block(source, 'private sealed class MessageCodec')
    cap = re.findall(r'private const int MaxMessageBytes\s*=\s*[^;]+;', source)
    assert len(cap) == 1 and cap[0] == 'private const int MaxMessageBytes = 16 * 1024;', 'Cap drift requires renewed review'
    assert codec.count('public async Task') == 2, 'Extract the entire class with all read/write declarations'
    read, _, _ = extract_block(codec, 'public async Task<string?> ReadAsync(')
    write, _, _ = extract_block(codec, 'public async Task WriteAsync(')
    anchor = '                if (count == 0) return null;\n                _readBody.Write(_readBuffer, 0, count);'
    assert codec.count(anchor) == 1, 'Current source does not match reviewed copy site'
    replacement = '''                if (count == 0) return null;
                // Only an empty accumulator can be the first positive read.
                // ReadAsync cannot return more than _readBuffer.Length (the cap).
                if (_readBody.Length == 0 && pipe.IsMessageComplete)
                    return Encoding.UTF8.GetString(_readBuffer, 0, count);
                _readBody.Write(_readBuffer, 0, count);'''
    candidate = codec.replace(anchor, replacement)
    candidate_write, _, _ = extract_block(candidate, 'public async Task WriteAsync(')
    assert candidate_write == write, 'Read-only candidate must preserve the complete write method'
    candidate_read, _, _ = extract_block(candidate, 'public async Task<string?> ReadAsync(')
    assert candidate_read.replace(replacement, anchor) == read, 'Candidate changed more than the declared copy bypass'
    (out / 'baseline-MessageCodec.cs').write_text(codec + '\n', encoding='utf-8')
    (out / 'candidate-MessageCodec.cs').write_text(candidate + '\n', encoding='utf-8')
    (out / 'candidate.patch').write_text(''.join(difflib.unified_diff(
        (codec + '\n').splitlines(True), (candidate + '\n').splitlines(True),
        fromfile='source-extracted/baseline-MessageCodec.cs', tofile='fixture-only/candidate-MessageCodec.cs')), encoding='utf-8')
    dump(out / 'benchmark-criteria.json', CRITERIA)
    project = out / 'standalone'
    project.mkdir()
    generated = ['using System.IO.Pipes;\nusing System.Text;\n']
    for name, body, seam in [('BaselinePipe', codec, False), ('CandidatePipe', candidate, False),
                             ('BaselineSeam', codec, True), ('CandidateSeam', candidate, True)]:
        if seam:
            # The only seam substitution is the ReadAsync parameter type. The
            # full WriteAsync signature/body remains byte-for-byte source text.
            assert body.count('ReadAsync(PipeStream pipe,') == 1
            body = body.replace('ReadAsync(PipeStream pipe,', 'ReadAsync(ReadSeam pipe,')
        handle = 'SeamHandle' if seam else 'PipeHandle'
        generated.append(f'internal static class {name}\n{{\n    {cap[0]}\n    {body}\n'
                         f'    internal static {handle} Create()\n    {{\n        var c = new MessageCodec();\n'
                         f'        return new {handle}(c, c.ReadAsync, c.WriteAsync);\n    }}\n}}\n')
    (project / 'ExtractedCodec.cs').write_text('\n'.join(generated), encoding='utf-8')
    fixture_bytes = FIXTURE.read_bytes()
    (project / 'DecodeFixture.cs').write_bytes(fixture_bytes)
    (project / 'DecodeFixture.csproj').write_text('''<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable>
    <AssemblyName>MessageDecodeFixture</AssemblyName><PlatformTarget>x64</PlatformTarget>
    <StartupObject>DecodeFixture</StartupObject><NuGetAudit>false</NuGetAudit>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <EnableNETAnalyzers>false</EnableNETAnalyzers><TreatWarningsAsErrors>true</TreatWarningsAsErrors>
    <RuntimeFrameworkVersion>10.0.11</RuntimeFrameworkVersion><RollForward>Disable</RollForward>
  </PropertyGroup>
  <ItemGroup><Compile Include="ExtractedCodec.cs"/><Compile Include="DecodeFixture.cs"/></ItemGroup>
</Project>
''', encoding='utf-8')
    (project / 'NuGet.Config').write_text('<configuration><packageSources><clear/></packageSources></configuration>\n', encoding='utf-8')
    audit_bytes = AUDIT.read_bytes()
    manifest = {
        'worker': 'R24-H1', 'mode': args.mode, 'productionModified': False,
        'productionSource': str(source_path), 'productionSourceSha256': sha(source_bytes),
        'sourceClassStartLine': source[:start].count('\n') + 1,
        'sourceClassEndLine': source[:end].count('\n') + 1,
        'baselineClassSha256': sha(codec.encode()), 'candidateClassSha256': sha(candidate.encode()),
        'baselineReadSha256': sha(read.encode()), 'candidateReadSha256': sha(candidate_read.encode()),
        'writeMethodSha256': sha(write.encode()), 'writeUnchanged': True,
        'fixtureSha256': sha(fixture_bytes), 'runnerSha256': sha(Path(__file__).read_bytes()),
        'auditPath': str(AUDIT), 'auditSha256': sha(audit_bytes),
        'criteriaSha256': sha((out / 'benchmark-criteria.json').read_bytes()),
        'generatedSourceSha256': sha((project / 'ExtractedCodec.cs').read_bytes()),
        'sourceNormalization': 'UTF-8 BOM removed and CRLF normalized to LF for extracted text; production raw hash retained',
        'dependencies': ['MaxMessageBytes exact source declaration', 'System.IO.MemoryStream', 'System.IO.Pipes.PipeStream',
                         'System.Text.Encoding.UTF8', 'System.Threading.CancellationToken', 'System.Threading.Tasks.Task', 'System.IO.InvalidDataException'],
        'projectCompileInputs': ['ExtractedCodec.cs', 'DecodeFixture.cs'],
        'projectReferences': 'BCL framework only; no production project, package, native, device SDK, or Host Main',
        'seamSubstitution': 'ReadAsync(PipeStream pipe, ...) -> ReadAsync(ReadSeam pipe, ...), otherwise same full class including unchanged WriteAsync',
        'candidateOnly': 'Preserves session allocations/reset and the original fragmented Write/cap/completion/decode statements. First read adds one completion observation before deciding eligibility; later fragments retain original completion/cap ordering.',
        'benchmarkExecuted': False, 'correctnessComplete': False,
    }
    dump(out / 'source-manifest.json', manifest)
    print('Prepared:', out, flush=True)
    if args.mode == 'prepare':
        return
    env = os.environ | {'DOTNET_CLI_TELEMETRY_OPTOUT': '1', 'DOTNET_SKIP_FIRST_TIME_EXPERIENCE': '1',
                        'DOTNET_NOLOGO': '1', 'DOTNET_CLI_HOME': str(out / 'dotnet-home'),
                        'NUGET_PACKAGES': str(out / 'nuget-packages'),
                        'DOTNET_TieredCompilation': '1', 'DOTNET_TieredPGO': '1', 'DOTNET_ReadyToRun': '1',
                        'TEMP': str(out / 'temp'), 'TMP': str(out / 'temp')}
    (out / 'temp').mkdir()

    def run(arguments: list[str], log: str, timeout: int = 180) -> None:
        p = subprocess.run(['dotnet', *arguments], cwd=project, env=env, capture_output=True,
                           text=True, encoding='utf-8', errors='replace', timeout=timeout)
        (out / log).write_text(p.stdout + p.stderr, encoding='utf-8')
        if p.returncode:
            raise RuntimeError(f'{log}: exit {p.returncode}\n{p.stdout}\n{p.stderr}')

    run(['--info'], 'dotnet-info.log')
    run(['restore', 'DecodeFixture.csproj', '--configfile', 'NuGet.Config', '-p:NuGetAudit=false'], 'restore.log')
    profiles = []
    build_artifacts = []
    # Always finish BOTH correctness configurations before any optional measurement.
    for config in ('Release', 'Debug'):
        run(['build', 'DecodeFixture.csproj', '-c', config, '--no-restore', '--nologo'], 'build-' + config + '.log')
        result_dir = out / config
        result_dir.mkdir()
        dll = project / 'bin' / config / 'net10.0-windows/MessageDecodeFixture.dll'
        run([str(dll), 'verify', str(result_dir), str(out / 'benchmark-criteria.json')], 'verify-' + config + '.log')
        result = json.loads((result_dir / 'correctness.json').read_text(encoding='utf-8'))
        assert result['pass'] and not result['benchmarkExecuted'], result
        profiles.append({'configuration': config, **result})
        for artifact in sorted(dll.parent.iterdir()):
            if artifact.is_file():
                build_artifacts.append({'configuration': config, 'path': str(artifact), 'sha256': sha(artifact.read_bytes())})
        print(f'{config}: {result["assertions"]} assertions; {result["realPipeCases"]} real-pipe cases; no benchmark', flush=True)
    source_stable = sha(source_path.read_bytes()) == sha(source_bytes)
    tools_stable = sha(FIXTURE.read_bytes()) == sha(fixture_bytes) and sha(Path(__file__).read_bytes()) == manifest['runnerSha256']
    assert source_stable and tools_stable, 'Source/tool drift: retain evidence but do not declare current readiness'
    manifest.update(correctnessComplete=True, sourceStableDuringRun=source_stable, toolsStableDuringRun=tools_stable, buildArtifacts=build_artifacts)
    performance = None
    if args.mode == 'benchmark':
        # Reached only with explicit mode AND permission flag, after correctness.
        measure = out / 'measurement'
        measure.mkdir()
        dll = project / 'bin/Release/net10.0-windows/MessageDecodeFixture.dll'
        run([str(dll), 'benchmark', str(measure), str(out / 'benchmark-criteria.json'), '--measurement-authorized'], 'benchmark.log', 600)
        performance = json.loads((measure / 'benchmark.json').read_text(encoding='utf-8'))
        manifest['benchmarkExecuted'] = True
        manifest['measurementPermission'] = 'User explicitly RELEASED R24 measurement after correctness; CLI permission flag supplied.'
        manifest['measurementConfiguration'] = 'Release; Debug correctness only, as prepared before measurement'
        assert sha((out / 'benchmark-criteria.json').read_bytes()) == manifest['criteriaSha256'], 'Criteria drift'
        assert sha(source_path.read_bytes()) == sha(source_bytes), 'Production source drift during measurement'
    dump(out / 'source-manifest.json', manifest)
    result = {'pass': performance['pass'] if performance is not None else True, 'correctnessPass': True,
              'performancePass': performance['pass'] if performance is not None else None,
              'performance': performance, 'readyForMeasurement': True, 'measurementPermissionRequired': args.mode != 'benchmark',
              'benchmarkExecuted': manifest['benchmarkExecuted'], 'runDirectory': str(out), 'manifest': manifest, 'profiles': profiles}
    dump(out / 'result.json', result)
    dump(output / ('measurement-result.json' if args.mode == 'benchmark' else 'readiness.json'), result)
    print('Evidence:', out, flush=True)


if __name__ == '__main__':
    main()
