#!/usr/bin/env python3
"""R15 bounded, unapplied gyro-envelope candidate. Runs only an inert C++ fixture."""
from __future__ import annotations
import argparse
import datetime as dt
import difflib
import hashlib
import json
import re
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
OUTPUT = Path('G:/YeManCC-Work/Build/Validation/VirtualInputPerf-R15-20261015/gyro-envelope-local')
EXPECTED_MAIN = '79340b9dcba916e2a02cc2e1da5d9aee8ba01a896ac570bb627df28c930d0ac2'
PARENT_EVIDENCE = Path('G:/YeManCC-Work/Build/Validation/VirtualInputPerf-R14-20261015/next-node-inspection.json')
OLD = b'    json m = {{"event", std::move(ev)}, {"data", std::move(data)}};'
NEW_LINES = [b'    json m = json::object();', b'    m["event"] = std::move(ev);', b'    m["data"] = std::move(data);']
POLICY = {
    'setBeforeMeasurement': True, 'timedRounds': 11,
    'minimumFasterRoundsPerScenario': 10, 'minimumMedianReductionPercentPerScenario': 5,
    'strictlyFewerAllocationRequestsPerScenario': True,
    'strictlyFewerRequestedBytesPerScenario': True,
    'everyPredeterminedScenarioMustPass': True,
    'payloadShapes': ['empty', 'full', 'deep', 'unicode'],
    'gates': [False, True], 'targetCounts': [1, 4],
    'decisionIfAnyThresholdFails': 'reject',
    'productionWrites': [], 'fullPackageAndBuild': 'parent-owned',
    'timingOrder': 'rotating scenario order; alternate baseline-first and candidate-first pairs; separate timings; rewarm both',
    'allocationMetric': 'ordinary/array/aligned operator new requested count and bytes; independent of timing; not RSS',
    'destructionIncluded': 'all emitter-owned payload/envelope/reference/dump/conversion destruction before return',
    'setupExcluded': 'JSON payload copies, event preparation and topology creation are outside timed interval',
}

def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def save_json(path: Path, value) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')

def close_brace(raw: bytes, start: int) -> int:
    """Lexical brace matching, including comments, escapes and C++ raw strings."""
    assert raw[start:start+1] == b'{'
    i, depth, state = start, 0, 'code'
    while i < len(raw):
        c, pair = raw[i:i+1], raw[i:i+2]
        if state == 'line':
            if c == b'\n': state = 'code'
        elif state == 'block':
            if pair == b'*/': state = 'code'; i += 1
        elif state in ('string', 'char'):
            if c == b'\\': i += 1
            elif c == (b'"' if state == 'string' else b"'"): state = 'code'
        elif pair == b'//': state = 'line'; i += 1
        elif pair == b'/*': state = 'block'; i += 1
        elif pair == b'R"':
            delimiter_end = raw.find(b'(', i+2)
            assert 0 <= delimiter_end-i <= 18, 'Unexpected raw literal'
            end_token = b')' + raw[i+2:delimiter_end] + b'"'
            end = raw.find(end_token, delimiter_end+1)
            assert end >= 0, 'Unclosed raw literal'
            i = end + len(end_token) - 1
        elif c == b'"': state = 'string'
        elif c == b"'": state = 'char'
        elif c == b'{': depth += 1
        elif c == b'}':
            depth -= 1
            if depth == 0: return i
        i += 1
    raise AssertionError('Unclosed block')

def extract(raw: bytes, pattern: bytes, is_struct=False):
    hits = list(re.finditer(pattern, raw, re.M))
    assert len(hits) == 1, f'Binding needs exactly one definition: {pattern!r}, got {len(hits)}'
    start = hits[0].start()
    opening = raw.find(b'{', hits[0].end()-1)
    assert opening >= 0
    end = close_brace(raw, opening)+1
    if is_struct:
        assert raw[end:end+1] == b';'
        end += 1
    if raw[end:end+2] == b'\r\n': end += 2
    elif raw[end:end+1] == b'\n': end += 1
    return {'bytes': raw[start:end], 'start': start, 'end': end,
            'line': raw[:start].count(b'\n')+1}

FUNCTIONS = {
    'owned-emitter': (rb'^static void ipc_emit_owned_gyro_telemetry\(std::string ev, json data\) \{', False),
    'target-resolver': (rb'^static ComPtr<ICoreWebView2> ipc_view_for_child\(int childId\) \{', False),
    'target-post': (rb'^static void ipc_post_json\(int childId, const json& message\) \{', False),
    'utf8-to-utf16': (rb'^static std::wstring U2W\(const std::string& s\) \{', False),
    'diagnostic-gate': (rb'^static inline bool tickCostEnabled\(\) \{', False),
    'ipc-cost-scope': (rb'^struct UiCostIpcScope \{', True),
    'cost-name-hash': (rb'^static inline unsigned long long uiCostNameHash\(const char\* s\) \{', False),
    'cost-epoch-point': (rb'^static inline unsigned long long tickCostEpochUs\(std::chrono::steady_clock::time_point point\) \{', False),
    'cost-epoch-now': (rb'^static inline unsigned long long tickCostEpochUs\(\) \{', False),
    'cost-slot-acquire': (rb'^static int uiCostSlotAcquire\(UiCostSlotTable& t, unsigned long long key, const char\* name\) \{', False),
    'cost-stage-add': (rb'^static inline void uiCostStageAdd\(int stage, unsigned long long us\) \{', False),
    'cost-slot-add': (rb'^static inline void uiCostSlotAdd\(UiCostSlotTable& t, int slot, unsigned long long us,\s*UiCostOverflow& overflow\) \{', False),
    'cost-bump': (rb'^static inline void uiCostBump\(std::atomic<unsigned long long>& cnt,\s*std::atomic<unsigned long long>& sum,\s*std::atomic<unsigned long long>& mx,\s*unsigned long long us\) \{', False),
    'lifecycle-logger': (rb'^static void appendNativeLifecycleLog\(const char\* event, json detail\) \{', False),
    'child-window-type': (rb'^struct ChildWindow \{', True),
    'cost-slot-type': (rb'^struct UiCostSlotTable \{', True),
    'cost-overflow-type': (rb'^struct UiCostOverflow \{', True),
}
DECLARATIONS = {
    'json-alias': rb'^using json = nlohmann::json;[^\n]*\n',
    'json-include': rb'^#include "json.hpp"[^\n]*\n',
    'main-view': rb'^static ComPtr<ICoreWebView2>\s+g_view;[^\n]*\n',
    'child-map': rb'^static std::unordered_map<int, ChildWindow\*> g_children;[^\n]*\n',
    'gate-atomic': rb'^static std::atomic<bool> g_domainLogEnabled\[2\][^\n]*\n',
    'owned-counter': rb'^static std::atomic<unsigned long long> g_c17aOwnedEvents[^\n]*\n',
    'parity-counter': rb'^static std::atomic<unsigned long long> g_c17aParityChecks[^\n]*\n',
    'failure-counter': rb'^static std::atomic<unsigned long long> g_c17aParityFailures[^\n]*\n',
}

def bind(raw: bytes):
    bound = {name: extract(raw, pattern, is_struct) for name, (pattern, is_struct) in FUNCTIONS.items()}
    for name, pattern in DECLARATIONS.items():
        hits = list(re.finditer(pattern, raw, re.M)); assert len(hits) == 1, name
        hit = hits[0]
        bound[name] = {'bytes': hit.group(), 'start': hit.start(), 'end': hit.end(),
                       'line': raw[:hit.start()].count(b'\n')+1}
    return bound

def line_endings(raw: bytes):
    return {'CRLF': raw.count(b'\r\n'), 'standaloneLF': raw.count(b'\n')-raw.count(b'\r\n'),
            'standaloneCR': raw.count(b'\r')-raw.count(b'\r\n'), 'utf8BOM': raw.startswith(b'\xef\xbb\xbf')}

def generated_header(bound, candidate: bytes):
    shared = ['diagnostic-gate', 'cost-name-hash', 'ipc-cost-scope', 'utf8-to-utf16', 'target-resolver', 'target-post']
    raw = b'// Exact production bodies; inert external bindings provided by fixture.\n' + b''.join(bound[k]['bytes'] for k in shared)
    original = bound['owned-emitter']['bytes']
    for name, body in [('baseline_emit', original), ('candidate_emit', candidate)]:
        raw += body.replace(b'ipc_emit_owned_gyro_telemetry', name.encode(), 1)
    for name, body, needle in [('baseline_observed', original, OLD),
                               ('candidate_observed', candidate, NEW_LINES[-1])]:
        # Test-only hook is omitted from baseline_emit/candidate_emit, which are
        # the functions timed and used for primary functional equivalence.
        eol = b'\r\n' if b'\r\n' in body else b'\n'
        instrumented = body.replace(needle, needle + eol + b'    observeLocals(ev, data, m);', 1)
        assert instrumented != body
        raw += instrumented.replace(b'ipc_emit_owned_gyro_telemetry', name.encode(), 1)
    return raw

def run_process(command, cwd, timeout):
    return subprocess.run(command, cwd=cwd, capture_output=True, text=True,
                          encoding='utf-8', errors='replace', timeout=timeout)

def audit_patch(raw, candidate_main, bound):
    before = bound['owned-emitter']
    eol = b'\r\n' if before['bytes'].split(OLD)[1].startswith(b'\r\n') else b'\n'
    replacement = eol.join(NEW_LINES)
    assert before['bytes'].count(OLD) == 1
    after = before['bytes'].replace(OLD, replacement, 1)
    assert candidate_main == raw[:before['start']]+after+raw[before['end']:]
    assert candidate_main.replace(replacement, OLD, 1) == raw
    old_ends, new_ends = line_endings(raw), line_endings(candidate_main)
    assert old_ends['standaloneLF'] == new_ends['standaloneLF'] == 103
    assert old_ends['standaloneCR'] == new_ends['standaloneCR']
    assert new_ends['CRLF'] == old_ends['CRLF']+2
    return {'pass': True, 'oneLocalEnvelopeConstructionOnly': True,
            'signatureGateReferenceParityDumpDeliveryUnchanged': True,
            'unmodifiedPrefixSha256': sha(raw[:before['start']]),
            'unmodifiedSuffixSha256': sha(raw[before['end']:]),
            'baselineLineEndings': old_ends, 'candidateLineEndings': new_ends}

def recheck(bound, header_raw, cpp_raw, runner_raw, parent_raw):
    latest = (REPO/'native/main.cpp').read_bytes(); current = bind(latest)
    drift = [name for name in bound if bound[name]['bytes'] != current[name]['bytes']]
    assert not drift, 'Bound production drift: '+', '.join(drift)
    assert (REPO/'deps/json/json.hpp').read_bytes() == header_raw, 'Vendored JSON drift'
    assert (REPO/'tools/virtual_gyro_envelope_perf_selftest.cpp').read_bytes() == cpp_raw, 'Fixture drift'
    assert Path(__file__).read_bytes() == runner_raw, 'Runner drift'
    assert PARENT_EVIDENCE.read_bytes() == parent_raw, 'Bound parent evidence drift'
    return latest

def report_text(result):
    m, perf = result['manifest'], result['release']['performance']
    lines = [f"R15 gyro envelope local decision: {result['decision']}", '',
        'Production remains untouched. The patch is UNAPPLIED. Parent owns full build/package and release acceptance.', '',
        f"Release correctness: {result['release']['correctness']['cases']} cases, {result['release']['assertions']} assertions.",
        f"Checked correctness: {result['checked']['correctness']['cases']} cases, {result['checked']['assertions']} assertions.",
        'Exact types, float bits, dump bytes, moved-from arguments, gates, atomic increments, mismatch logging, renderer order and cost scope lifetime were compared. Per-case matrices and byte-preserving trace witnesses accompany this report.',
        'All 16 successive four-read diagnostic scripts were exercised, including reference-off / parity-on mismatch and strict bad-UTF8 exceptions.',
        'Unmodified production bodies execute against inert target/log/cost dependencies; actual U2W uses the pure Windows UTF8 conversion API. No application, InputHost, Steam, device, service, registry or driver operation runs.', '',
        f"Ownership: initializer shares {result['release']['ownership']['baselineNodesSharingProducerStorage']} producer nodes; candidate retains {result['release']['ownership']['candidateNodesPreservingProducerStorage']} nodes.",
        'Both original and candidate preserve all 47 producer object/array/string allocation identities in this ownership fixture. The vendored initializer already moves the payload; this candidate does not eliminate a payload deep copy. It only removes initializer envelope temporaries. Allocation generations exclude address reuse from this proof.',
        'An observation hook exists only in additional correctness functions. The measured functions contain no observation hook.', '',
        'Predeclared acceptance: every scenario needs >=10/11 faster rounds, >=5% median reduction, strictly fewer allocation requests and requested bytes.',
        'Timing includes all original reference/parity/per-target dumps, actual UTF16 conversion, real atomic counters, inert target sink writes, and destruction. Payload setup is excluded. Allocation counts are separate runs; bytes are requests, not RSS.', '',
        '| Shape | Gate | Targets | Original ns | Candidate ns | Reduction | Wins/11 | Requests | Requested bytes | Accept |',
        '|---|---|---:|---:|---:|---:|---:|---:|---:|---|']
    for row in perf['scenarios']:
        lines.append(f"| {row['shape']} | {'on' if row['gate'] else 'off'} | {row['targets']} | {row['baselineMedianNs']:.1f} | {row['candidateMedianNs']:.1f} | {row['medianReductionPercent']:.2f}% | {row['fasterRounds']} | {row['baselineRequests']} → {row['candidateRequests']} | {row['baselineRequestedBytes']} → {row['candidateRequestedBytes']} | {row['accepted']} |")
    if result['decision']=='reject':
        lines += ['', 'At least one predeclared scenario failed. Reject this candidate for this acceptance contract; allocation/copy improvements alone do not justify adoption.']
    lines += ['', 'Historical frequency support comes only from the parent-supplied next-node-inspection.json: 669 UI cost windows, owned/parity 6987, failures 0; gyro in 662 top rows, historical nested row mean median 269 µs / P95 307 µs. These are historical elapsed data, not new CPU measurements. No archive was reopened.',
        f"Historical archive SHA256: {m['parentHistoricalEvidence']['archiveSha256']}", '',
        'Scope: local synchronous emitter throughput; inert targets cannot establish end-to-end product CPU, UI behavior, or full build/package validity. Per-target dump count/order and diagnostic cadence were unchanged. OOM index-by-index equivalence is not claimed because eliminating allocations changes failure positions; no production catch/recovery was introduced.', '',
        f"Baseline main SHA256: {m['baselineMainSha256']}",
        f"Full candidate main SHA256: {m['candidateMainSha256']}",
        f"Baseline function SHA256: {m['baselineFunctionSha256']}",
        f"Candidate function SHA256: {m['candidateFunctionSha256']}",
        f"UNAPPLIED raw patch SHA256: {m['rawPatchSha256']}",
        f"Vendored JSON SHA256: {m['jsonHeaderSha256']}",
        f"Runner SHA256: {m['runnerSha256']}",
        f"Fixture SHA256: {m['fixtureSha256']}",
        f"Main source at end SHA256: {result['mainSourceSha256AtEnd']}",
        f"Whole main stable: {result['wholeMainStable']}; relevant bound fragments stable: {result['boundFragmentsStable']}",
        'Mixed endings preserved: 103 standalone LF; raw replacement adds two CRLF lines only.', '']
    return '\n'.join(lines)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--batches', type=int, default=12)
    parser.add_argument('--correctness-only', action='store_true')
    args = parser.parse_args()
    assert 1 <= args.batches <= 100
    OUTPUT.mkdir(parents=True, exist_ok=True)
    run = OUTPUT/('run-'+dt.datetime.now().strftime('%Y%m%d-%H%M%S'))
    run.mkdir()
    # Establish the contract on disk before any compile or measurement.
    save_json(run/'acceptance-policy.json', POLICY)
    raw = (REPO/'native/main.cpp').read_bytes(); bound = bind(raw)
    before = bound['owned-emitter']['bytes']; assert before.count(OLD) == 1
    eol = b'\r\n' if before.split(OLD)[1].startswith(b'\r\n') else b'\n'
    candidate = before.replace(OLD, eol.join(NEW_LINES), 1)
    candidate_main = raw[:bound['owned-emitter']['start']]+candidate+raw[bound['owned-emitter']['end']:]
    patch = ''.join(difflib.unified_diff(raw.decode('utf-8').splitlines(True), candidate_main.decode('utf-8').splitlines(True),
        fromfile='a/native/main.cpp', tofile='b/native/main.cpp', n=3)).encode('utf-8')
    patch_check = audit_patch(raw, candidate_main, bound)
    header_path = REPO/'deps/json/json.hpp'; header_raw = header_path.read_bytes()
    cpp_raw = (REPO/'tools/virtual_gyro_envelope_perf_selftest.cpp').read_bytes(); runner_raw = Path(__file__).read_bytes()
    parent_raw = PARENT_EVIDENCE.read_bytes(); parent = json.loads(parent_raw)
    assert parent['archiveSha256'] == 'e886bcacb06e9250976235f4d1c0dae28c8f8e46902e7a1210f148c4cc01a479'
    generated = generated_header(bound, candidate)
    for filename, data in [('baseline-main.cpp', raw), ('candidate-main.cpp', candidate_main), ('baseline-function.cpp', before),
        ('candidate-function.cpp', candidate), ('candidate-UNAPPLIED.patch', patch), ('json.hpp', header_raw),
        ('fixture.cpp', cpp_raw), ('runner.py', runner_raw), ('production-extracted.h', generated),
        ('parent-next-node-inspection.json', parent_raw)]:
        (run/filename).write_bytes(data)
    (run/'bound-fragments').mkdir()
    for name, part in bound.items(): (run/'bound-fragments'/(name+'.cpp')).write_bytes(part['bytes'])
    manifest = {'schemaVersion': 1, 'createdAtLocal': dt.datetime.now().astimezone().isoformat(),
        'sourcePath': str(REPO/'native/main.cpp'), 'baselineMainSha256': sha(raw), 'expectedParentMainSha256': EXPECTED_MAIN,
        'matchesExpectedParentMain': sha(raw)==EXPECTED_MAIN, 'candidateMainSha256': sha(candidate_main),
        'baselineFunctionSha256': sha(before), 'candidateFunctionSha256': sha(candidate),
        'rawPatchSha256': sha(patch), 'jsonHeaderPath': str(header_path), 'jsonHeaderSha256': sha(header_raw),
        'fixtureSha256': sha(cpp_raw), 'runnerSha256': sha(runner_raw), 'generatedHeaderSha256': sha(generated),
        'productionModifiedByRunner': False, 'productionWriteSet': [], 'patchApplied': False,
        'bindingRule': 'Compare exact bytes of only listed relevant production function/logger/helpers and JSON/harness/runner. Main edits elsewhere are recorded without automatic invalidation.',
        'bindings': {name: {'sha256': sha(p['bytes']), 'bytes': len(p['bytes']), 'line': p['line'],
            'byteStart': p['start'], 'byteEndExclusive': p['end']} for name,p in bound.items()},
        'patchAudit': patch_check,
        'fixtureSeams': {'entrypoint': 'names changed for A/B; emitter bodies otherwise exact',
            'functionalObservation': 'additional test-only functions add one post-construction hook; measured functions do not',
            'gate': 'exact tickCostEnabled body, wrapped real acquire-load atomic with successive snapshots',
            'counters': 'wrapper executes real std::atomic fetch_add with original relaxed ordering; functional trace',
            'targets': 'same unordered_map topology; exact resolver/post functions; inert ComPtr and Post sink; actual production U2W',
            'cost': 'exact UiCostIpcScope and name hash; deterministic epoch/acquire/stage/slot mocks trace lifetime',
            'logger': 'inert logger records exact event and json argument; production logger source is bound but never executed'},
        'parentHistoricalEvidence': {'path': str(PARENT_EVIDENCE), 'sha256': sha(parent_raw), 'archiveSha256': parent['archiveSha256'],
            'archiveReadByThisRunner': False, 'uiCostWindows': parent['uiCostWindows'],
            'c17aOwnedObserved': parent['c17aOwnedObserved'], 'c17aParityChecksObserved': parent['c17aParityChecksObserved'],
            'c17aParityFailuresObserved': parent['c17aParityFailuresObserved'], 'historicalNestedElapsedNotNewCpu': True},
        'inputs': 'Deterministic synthetic JSON shapes defined in fixture and separately saved below; no new device or archive input',
        'acceptancePolicy': POLICY}
    save_json(run/'source-manifest.json', manifest)
    save_json(run/'patch-audit.json', patch_check)
    vcvars = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
    assert vcvars.is_file()
    profiles = {}
    for profile, options, batches in [('release', ['/O2','/DNDEBUG','/MT'], 0 if args.correctness_only else args.batches),
                                     ('checked', ['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'], 0)]:
        recheck(bound,header_raw,cpp_raw,runner_raw,parent_raw)
        exe = run/(profile+'.exe')
        command = ['cl','/nologo','/std:c++20','/utf-8','/EHsc','/fp:precise',*options,
            str(run/'fixture.cpp'), '/I'+str(run), '/Fo'+str(run/(profile+'.obj')), '/Fe'+str(exe)]
        batch = run/(profile+'-build.bat')
        batch.write_text('@echo off\nset VSLANG=1033\ncall "'+str(vcvars)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(command)+'\n',encoding='utf-8',newline='\r\n')
        built = run_process(['cmd.exe','/d','/c',str(batch)],run,120)
        (run/(profile+'-build.log')).write_text(built.stdout+built.stderr,encoding='utf-8')
        assert built.returncode==0, 'Compile failed: '+str(run/(profile+'-build.log'))
        if profile=='release':
            corpus=run_process([str(exe),'--inputs'],run,30)
            assert corpus.returncode==0, corpus.stderr
            corpus_raw=corpus.stdout.encode('utf-8')
            (run/'input-corpus.json').write_bytes(corpus_raw)
            manifest['inputCorpusSha256']=sha(corpus_raw)
            manifest['inputCorpusPath']='input-corpus.json'
            save_json(run/'source-manifest.json',manifest)
        executed = run_process([str(exe),str(batches)],run,300)
        (run/(profile+'-run.json')).write_text(executed.stdout,encoding='utf-8')
        (run/(profile+'-run.stderr')).write_text(executed.stderr,encoding='utf-8')
        assert executed.returncode==0, 'Fixture failed: '+str(run/(profile+'-run.stderr'))+' '+executed.stderr
        profiles[profile] = json.loads(executed.stdout)
        save_json(run/(profile+'-case-matrix.json'),profiles[profile].pop('caseMatrix'))
        save_json(run/(profile+'-trace-witnesses.json'),profiles[profile].pop('traceWitnesses'))
        manifest[profile+'ExecutableSha256'] = sha(exe.read_bytes())
    latest = recheck(bound,header_raw,cpp_raw,runner_raw,parent_raw)
    manifest['allBoundFragmentsStableAtEnd'] = True
    save_json(run/'source-manifest.json',manifest)
    result = {'pass': True, 'decision': 'correctness-only' if args.correctness_only else profiles['release']['performance']['decision'],
        'runDirectory': str(run), 'manifest':manifest, **profiles, 'mainSourceSha256AtEnd':sha(latest),
        'wholeMainStable':latest==raw,'boundFragmentsStable':True,'productionModifiedByRunner':False,'patchApplied':False}
    save_json(run/'results.json',result)
    if not args.correctness_only:
        (run/'REPORT.md').write_text(report_text(result),encoding='utf-8',newline='\n')
        save_json(OUTPUT/'result.json',result)
        (OUTPUT/'REPORT.md').write_text(report_text(result),encoding='utf-8',newline='\n')
    else: save_json(OUTPUT/'correctness-result.json',result)
    files = {str(p.relative_to(run)): {'sha256':sha(p.read_bytes()),'bytes':p.stat().st_size}
             for p in sorted(run.rglob('*')) if p.is_file()}
    save_json(run/'artifact-hashes.json',files)
    print(json.dumps({'pass':True,'decision':result['decision'],'run':str(run),
        'baselineMainSha256':sha(raw),'candidateMainSha256':sha(candidate_main),'patchSha256':sha(patch),
        'releaseCases':profiles['release']['correctness']['cases'],'checkedCases':profiles['checked']['correctness']['cases'],
        'performance':profiles['release']['performance']},ensure_ascii=False,indent=2))

if __name__ == '__main__':
    main()
