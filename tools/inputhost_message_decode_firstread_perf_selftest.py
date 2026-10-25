#!/usr/bin/env python3
"""R27-H1: source-extracted MessageCodec correctness and permission-gated measurement.

Default execution only verifies correctness, in Release AND Debug. No production
project is compiled or loaded. All generated files stay under decode-worker.
Measurement requires a new parent release under user standing task authorization
and BOTH CLI switches. A single R27 release covers the fixed four campaigns.
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
ALLOWED_OUTPUT = Path('G:/YeManCC-Work/Build/Validation/VirtualInputPerf-R27-20261015/decode-worker')
AUDIT = Path('G:/YeManCC-Work/Build/Validation/VirtualInputPerf-R23-20261015/host-sidecar-audit.md')
FIXTURE = ROOT / 'tools/inputhost_message_decode_firstread_perf_selftest.cs'
# Frozen before any measurements. A failed gate remains a failed gate.
CRITERIA = {
    'version': 'R27-H1-predeclared-v1',
    'measurementPermission': 'Explicit user release required; no benchmark executed by verify/prepare.',
    'scope': 'New separate-first-read variant; extracted ReadAsync only; deterministic synchronous read seam copies the same input bytes in both arms. No overall controller or product CPU claim.',
    'runtime': 'Microsoft.NETCore.App 10.0.11 x64; tiered compilation and tiered PGO enabled',
    'profiles': ['Release', 'Debug'],
    'campaignsPerProfile': 2,
    'campaignExecutionOrder': ['campaign-1/Release', 'campaign-1/Debug', 'campaign-2/Release', 'campaign-2/Debug'],
    'campaignIsolation': 'Each profile/campaign is a fresh process; every row must pass independently, no pooling or selective reruns.',
    'rounds': 11,
    'order': 'Campaign 1 starts AB and alternates; campaign 2 starts BA and alternates; each scenario has 11 paired rounds',
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



def write_order_audit(out: Path, manifest: dict) -> None:
    text = f"""# R27 first-read ordering audit (new variant, R24 immutable)

Actual production raw source SHA256: `{manifest['productionSourceSha256']}`.
Full source-extracted MessageCodec baseline; no Host Main/device SDK references.

## Real API count and capacity contract

The real BCL ReadAsync destination is the actual source `_readBuffer`, allocated
at the actual source MaxMessageBytes = 16 * 1024. A successful read returns a
nonnegative count <= destination length. No fixture test treats a count greater
than that destination length, or a negative count, as a real PipeStream input.
The seam rejects an oversized scripted chunk as a harness construction error.
The existing 16 KiB accumulator is reset first and never shrunk in capacity.
Therefore the first positive fragment fits the buffer, the byte cap and the
existing backing store. The explicit first-fragment copy/cap statements are
retained for the incomplete case even though its first cap check cannot fail.

## Property observation and first-copy order

Baseline first positive read:
`Read -> MemoryStream.Write -> cap check -> IsMessageComplete (once)`.

R27 first positive read:
`Read -> IsMessageComplete (once) -> complete: direct UTF8 decode/return`;
`incomplete: MemoryStream.Write -> cap check -> original remaining do-loop`.

This intentionally moves the FIRST completion observation ahead of the FIRST
copy/cap. It does not preserve their instruction order. The BCL property reads
pipe state; it does not mutate the received buffer. Under the initialized,
private, single-reader codec contract, the first copy is in bounds and does not
need to allocate or grow. Thus moving this one observation preserves normal
returned strings, exception/cancellation outcomes, cap decisions and EOF nulls.
If the completion getter throws (e.g. real byte read mode), baseline has already
copied the first bytes into its private accumulator, while R27 has not. Tests
expose and record this internal-state difference rather than claim equivalence.
Both propagate the same exception, issue no further read and retain the initial
reset on the next invocation. Production does not expose the accumulator to an
external observer. Arbitrary external disposal/mutation of this private stream,
mutable side-effecting property seams, and impossible read counts are not
claimed to be real production inputs.

For every SUBSEQUENT positive fragment, the exact original source executes:
`Read -> copy -> cap -> completion`. Over-cap throws before its completion
query. EOF on any read returns null before copying/querying that read. There is
no additional IsMessageComplete observation for an incomplete first read and
no eligibility test in the original remaining fragment loop. Scripted tests
compare query counts and read-index traces and inspect accumulator lengths at
each query; later-fragment observation lengths must be identical.

## Ownership and error boundary

The existing initial SetLength(0) reset remains first. Every await retains the
original token and ConfigureAwait(false); one outstanding read is required.
Complete-first-read uses exactly `[0..firstCount]`, default Encoding.UTF8
replacement fallback, and an owned string. Incomplete reads use the retained
full original loop, assembled-byte cap, GetBuffer and checked length decode.
Partial EOF remains null, not a partial string. Read/getter exceptions are
propagated; follow-up calls test reset after EOF, cancellation, read/getter
exceptions and oversize. WriteAsync and all session buffers are extracted and
preserved unchanged. Actual Windows CurrentUserOnly private message pipes test
the unmodified PipeStream signature. Accepted fragmented real reads also run
through an explicitly constrained read adapter, separately labeled.

## Distinct candidate and fixed pending campaign

R24 is rejected and immutable. R27 removes its accumulator-length eligibility
check on every fragment and its duplicate first-incomplete completion query.
This is a new source variant, not a retuned R24 timing result. Two campaigns
per profile, Release and Debug, eleven pairs per scenario, are predeclared in
benchmark-criteria.json. All 24 profile/campaign/scenario rows must pass their
own fixed gates. Actual loopback proves semantics only. Isolated completed-read
seam timing does not measure kernel transport, in-product eligibility frequency,
controller/device CPU or overall CPU improvement. R27 timing requires a NEW
parent release; the old R24 grant does not authorize it.
"""
    (out / 'first-read-order-audit.md').write_text(text, encoding='utf-8')


def verify_prior_immutable(output: Path, manifest: dict) -> None:
    guard = output / 'r24-immutability-before.json'
    before = json.loads(guard.read_text(encoding='utf-8'))
    mismatches = []
    for row in before['files']:
        path = Path(row['path'])
        if not path.is_file() or sha(path.read_bytes()) != row['sha256'] or path.stat().st_mtime_ns != row['mtimeNs']:
            mismatches.append(row['path'])
    dump(output / 'r24-immutability-after.json', {
        'pass': not mismatches, 'filesChecked': len(before['files']), 'mismatches': mismatches,
        'r24CriteriaResultsToolsAndArtifactsReadOnly': True,
        'r27ProductionSourceSha256': manifest['productionSourceSha256'],
    })
    assert not mismatches, 'R24 immutability check failed: ' + repr(mismatches)


def write_bindings(out: Path, manifest: dict, profiles: list) -> None:
    for path, expected in [(ROOT / 'InputHost/Program.cs', manifest['productionSourceSha256']),
                           (FIXTURE, manifest['fixtureSha256']), (Path(__file__), manifest['runnerSha256']),
                           (out / 'benchmark-criteria.json', manifest['criteriaSha256'])]:
        assert sha(path.read_bytes()) == expected, 'Final binding drift: ' + str(path)
    lines = '\n'.join(f"- {p['configuration']}: {p['assertions']} assertions, {p['seamCases']} deterministic cases, {p['realPipeCases']} real-pipe cases." for p in profiles)
    readiness = f"""# R27 first-read candidate readiness

Correctness passed; benchmarkExecuted={manifest['benchmarkExecuted']}.
R27 measurement requires its own parent release under user standing task authorization; the R24 grant does not apply. One R27 release covers all four fixed campaigns.

{lines}

Candidate patch: `{out / 'candidate.patch'}`
Ordering proof: `{out / 'first-read-order-audit.md'}`
Criteria SHA256: `{manifest['criteriaSha256']}`.
Source SHA256: `{manifest['productionSourceSha256']}`.

Frozen plan: Release/Debug x two fresh-process campaigns x six scenarios x eleven
paired rounds. Each of 16 eligible rows requires >=10/11 faster, >=5% median
paired savings / baseline median, median candidate/baseline <=0.95 and no higher
allocation in ANY pair. Each of eight fragmented controls independently requires
median ratio <=1.05 and equal-or-lower allocation in EVERY pair. All 24 rows must
pass; no pooling, changing workloads/gates, trimming pairs or selective reruns.

The full current baseline and complete write declarations are source-extracted.
The candidate separates the first read, then retains the exact original fragment
loop/decode source. One completion query per first positive read; the first
query moves before its copy. Later fragments have original copy/cap/property
order. The ordering audit and query traces explicitly cover getter exceptions.
No impossible >buffer ReadAsync counts are treated as real API inputs.

Windows private pipe tests establish message-mode semantics. Future seam timing
will measure isolated codec cost only; no overall CPU claim. Parent owns any
production decision. R24 is rejected and unchanged.
"""
    (out / 'readiness.md').write_text(readiness, encoding='utf-8')
    files = [ROOT / 'InputHost/Program.cs', FIXTURE, Path(__file__),
             *[p for p in out.rglob('*') if p.is_file() and p.suffix in ('.cs', '.csproj', '.json', '.log', '.md', '.patch', '.dll', '.pdb')
               and not any(x in p.parts for x in ('obj', 'dotnet-home', 'temp', 'nuget-packages'))]]
    dump(out / 'artifact-bindings.json', {
        'worker': 'R27-H1', 'productionModified': False, 'benchmarkExecuted': manifest['benchmarkExecuted'],
        'allCurrentInputsStable': True, 'r24Immutable': True,
        'artifacts': [{'path': str(p.resolve()), 'sha256': sha(p.read_bytes()), 'bytes': p.stat().st_size} for p in dict.fromkeys(files)],
    })


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, default=ALLOWED_OUTPUT)
    ap.add_argument('--mode', choices=('prepare', 'verify', 'benchmark'), default='verify')
    ap.add_argument('--measurement-authorized', action='store_true', help='Use only after the parent releases new R27 measurement under user standing task authorization.')
    args = ap.parse_args()
    if args.mode == 'benchmark' and not args.measurement_authorized:
        ap.error('R27_BENCHMARK_LOCKED: new parent release under standing task authorization and --measurement-authorized required')
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
    first_marker = '            _readBody.SetLength(0);\n'
    assert read.count(first_marker) == 1 and read.count('do\n') == 1, 'Reviewed reset/loop source drift'
    first_read = """            // R27: classify the first positive read once, outside the fragment loop.
            var firstCount = await pipe.ReadAsync(_readBuffer, cancellationToken).ConfigureAwait(false);
            if (firstCount == 0) return null;
            // This read-only property moves before the first copy. A real read
            // returns at most the destination length, so this first read fits.
            if (pipe.IsMessageComplete)
                return Encoding.UTF8.GetString(_readBuffer, 0, firstCount);
            _readBody.Write(_readBuffer, 0, firstCount);
            if (_readBody.Length > MaxMessageBytes) throw new InvalidDataException("message-too-large");
"""
    candidate_read = read.replace(first_marker, first_marker + first_read)
    candidate = codec.replace(read, candidate_read)
    candidate_write, _, _ = extract_block(candidate, 'public async Task WriteAsync(')
    assert candidate_write == write, 'Read-only candidate must preserve the complete write method'
    assert candidate_read.replace(first_read, '') == read, 'Only the separate first-read prefix may differ'
    original_loop = read[read.index('            do\n'):]
    assert candidate_read.endswith(original_loop), 'Entire remaining fragment loop and decode must be exact source'
    assert candidate_read.count('pipe.IsMessageComplete') == read.count('pipe.IsMessageComplete') + 1
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
    <AssemblyName>MessageDecodeFirstReadFixture</AssemblyName><PlatformTarget>x64</PlatformTarget>
    <StartupObject>DecodeFixture</StartupObject><NuGetAudit>false</NuGetAudit>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <UseSharedCompilation>false</UseSharedCompilation><EnableNETAnalyzers>false</EnableNETAnalyzers><TreatWarningsAsErrors>true</TreatWarningsAsErrors>
    <RuntimeFrameworkVersion>10.0.11</RuntimeFrameworkVersion><RollForward>Disable</RollForward>
  </PropertyGroup>
  <ItemGroup><Compile Include="ExtractedCodec.cs"/><Compile Include="DecodeFixture.cs"/></ItemGroup>
</Project>
''', encoding='utf-8')
    (project / 'NuGet.Config').write_text('<configuration><packageSources><clear/></packageSources></configuration>\n', encoding='utf-8')
    audit_bytes = AUDIT.read_bytes()
    manifest = {
        'worker': 'R27-H1', 'mode': args.mode, 'productionModified': False,
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
        'candidateOnly': 'Separate first positive read before the exact original loop/decode. Exactly one completion query for the first positive read; no eligibility check in the remaining fragment loop. First completion query precedes first copy, unlike baseline; subsequent fragment copy/cap/completion order is exact source.',
        'benchmarkExecuted': False, 'correctnessComplete': False,
        'firstReadOrderingAudit': str(out / 'first-read-order-audit.md'),
        'originalFragmentLoopAndDecodeSha256': sha(original_loop.encode()),
        'fragmentLoopAndDecodeExactSource': True,
        'priorR24State': 'Rejected and immutable; new variant R27 addresses its repeated fragment eligibility checks.',
    }
    dump(out / 'source-manifest.json', manifest)
    write_order_audit(out, manifest)
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
        dll = project / 'bin' / config / 'net10.0-windows/MessageDecodeFirstReadFixture.dll'
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
        # A new explicit R27 release is required. R24's earlier grant does not apply.
        authorization_path = ALLOWED_OUTPUT.resolve() / 'measurement-authorization.json'
        authorization = json.loads(authorization_path.read_text(encoding='utf-8'))
        assert authorization['worker'] == 'R27-H1' and authorization['released'], 'New R27 parent authorization missing'
        assert authorization['criteriaSha256'] == manifest['criteriaSha256'], 'Authorization/criteria binding mismatch'
        assert authorization['productionSourceSha256'] == manifest['productionSourceSha256'], 'Authorization/source binding mismatch'
        assert authorization['releaseCoversAllFourCampaigns'] and not authorization['humanPerCampaignApprovalClaimed']
        dump(out / 'measurement-authorization.json', authorization)
        manifest['measurementAuthorization'] = authorization
        campaigns = []
        for campaign in (1, 2):
            for config in ('Release', 'Debug'):
                measure = out / 'measurement' / f'campaign-{campaign}' / config
                measure.mkdir(parents=True)
                dll = project / 'bin' / config / 'net10.0-windows/MessageDecodeFirstReadFixture.dll'
                print(f'FORMAL R27 campaign-{campaign}/{config}: fixed 11 pairs; no concurrent work', flush=True)
                run([str(dll), 'benchmark', str(measure), str(out / 'benchmark-criteria.json'),
                     '--measurement-authorized', str(campaign), config], f'benchmark-{campaign}-{config}.log', 600)
                measured = json.loads((measure / 'benchmark.json').read_text(encoding='utf-8'))
                campaigns.append({'campaign': campaign, 'configuration': config, **measured})
                assert sha(source_path.read_bytes()) == sha(source_bytes), 'Production source drift during measurement'
                assert sha((out / 'benchmark-criteria.json').read_bytes()) == manifest['criteriaSha256'], 'Criteria drift'
        performance = {'pass': all(p['pass'] for p in campaigns), 'campaigns': campaigns}
        manifest['benchmarkExecuted'] = True
        manifest['measurementPermission'] = 'Parent task released R27 measurement under user standing task authorization after R26 finished. One release covers the fixed two profiles and two campaigns after correctness; no new human per-campaign approval claimed.'
        manifest['measurementConfiguration'] = 'Release and Debug, two fresh-process campaigns each, all 24 scenario rows required'
    dump(out / 'source-manifest.json', manifest)
    result = {'pass': performance['pass'] if performance is not None else True, 'correctnessPass': True,
              'performancePass': performance['pass'] if performance is not None else None,
              'performance': performance, 'readyForMeasurement': True, 'measurementPermissionRequired': args.mode != 'benchmark',
              'benchmarkExecuted': manifest['benchmarkExecuted'], 'runDirectory': str(out), 'manifest': manifest, 'profiles': profiles}
    dump(out / 'result.json', result)
    dump(output / ('measurement-result.json' if args.mode == 'benchmark' else 'readiness.json'), result)
    verify_prior_immutable(output, manifest)
    write_bindings(out, manifest, profiles)
    print('Evidence:', out, flush=True)


if __name__ == '__main__':
    main()
