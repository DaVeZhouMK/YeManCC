#!/usr/bin/env python3
"""R15 bounded, unapplied gyro-envelope candidate. Runs only an inert C++ fixture."""
from __future__ import annotations
import argparse
import datetime as dt
import difflib
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SOURCE = REPO/'native/main.cpp'
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
    latest = SOURCE.read_bytes(); current = bind(latest)
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

REFERENCE_POLICY = {
    'mode': 'current-reference-copy', 'setBeforeMeasurement': True,
    'campaigns': 2, 'pairsPerCampaign': 11, 'batchesPerArm': 16, 'eventsPerArm': 1024,
    'payloadShapes': ['empty', 'full', 'deep', 'unicode'], 'gates': [False, True], 'targetCounts': [1, 4],
    'affected': {'gate': True, 'minimumMedianPairedCycleReductionPercent': 5,
                 'minimumFasterPairs': 10, 'allocationRequestsStrictlyDecrease': True, 'requestedBytesStrictlyDecrease': True},
    'control': {'gate': False, 'sourceBranchByteIdentical': True,
                'maximumMedianPairedCycleRegressionPercent': 5, 'allocationRequestsAndBytesEqual': True},
    'everyScenarioInBothCampaignsMustPass': True, 'retryOnMeasuredFailure': False,
    'metric': 'QueryThreadCycleTime deltas for full synchronous emitter; wall ns also retained',
    'scope': 'Whole current emitter, original reference/parity dumps, shared snapshot broadcast, UTF16 conversion and destruction; inert renderer/log/cost dependencies',
    'setupExcluded': 'Caller payload/event preparation and topology setup',
    'productionEnvelopeConstruction': 'Original initializer retained byte-for-byte; it already moves the payload',
    'diagnosticDumpAndGateCadenceUnchanged': True, 'productionWrites': [],
    'processCpuAcceptance': False, 'costsRequireSeparateExplicitInvocation': True,
}


def reference_write(path, data):
    with Path(path).open('xb') as stream:
        stream.write(data.encode('utf-8') if isinstance(data, str) else data)


def reference_json(path, value):
    reference_write(path, json.dumps(value, ensure_ascii=False, indent=2) + '\n')


def reference_identity(path):
    path = Path(path).resolve()
    raw = path.read_bytes()
    return {'path': str(path), 'bytes': len(raw), 'sha256': sha(raw)}


def reference_bind(raw):
    # The current consumer calls a shared broadcaster; the historical post
    # helper has a different signature and is outside this emitter's closure.
    functions = {name: spec for name, spec in FUNCTIONS.items() if name != 'target-post'}
    functions.update({
        'lifecycle-logger': (rb'^static void appendNativeLifecycleLog\(const char\* event, const json& observed\) \{', False),
        'snapshot-broadcast': (rb'^static void ipc_broadcast_controller_snapshot\(const json& message(?:, const std::string\* validatedUtf8 = nullptr)?\) \{', False),
        'producer-callback': (rb'^static void inputCaptureOnGamepad\([^;{]*\{', False),
        'producer-post': (rb'^static void gamepadSerialPostOwnedEvent\(std::string event, json data\) \{', False),
        'serial-event-type': (rb'^struct GamepadSerialEvent \{', True),
        'sensor-snapshot-gate': (rb'^static bool inputCaptureGyroPresentSnapshot\(\) \{', False),
        'serial-dispatch': (rb'^    case WM_GAMEPAD_SERIAL_EVENT: \{', False),
    })
    bound = {name: extract(raw, pattern, structure) for name, (pattern, structure) in functions.items()}
    declarations = dict(DECLARATIONS)
    if b'g_gamepadSerialEventMailbox' in raw:
        declarations.update({
            'serial-mailbox-include': rb'^#include "ipc_result_mailbox.h"[^\n]*\n',
            'serial-mailbox-root': rb'^static auto\* const g_gamepadSerialEventMailbox = new ymcc::ipc::ResultMailbox<GamepadSerialEvent>\(\);[^\n]*\n',
        })
        functions['producer-const-post'] = (rb'^static void gamepadSerialPostEvent\(const std::string& event, const json& data = \{\}\) \{', False)
        bound['producer-const-post'] = extract(raw, *functions['producer-const-post'])
    if b'g_gamepadSerialActionPower' in raw:
        declarations['serial-action-power-tls'] = rb'^static thread_local unsigned long long g_gamepadSerialActionPower\s*=\s*0;[^\n]*\n'
    for name, pattern in declarations.items():
        hits = list(re.finditer(pattern, raw, re.M))
        if len(hits) != 1:
            raise ValueError('Current declaration requires unique binding: ' + name)
        hit = hits[0]
        bound[name] = {'bytes': hit.group(), 'start': hit.start(), 'end': hit.end(), 'line': raw[:hit.start()].count(b'\n')+1}
    return bound


def reference_candidate(raw, bound):
    original = bound['owned-emitter']['bytes']
    eol = b'\r\n' if b'\r\n' in original else b'\n'
    old = b'        json reference = data;' + eol + b'        json oldEnvelope = {{"event", ev}, {"data", reference}};'
    new = b'        json oldEnvelope = {{"event", ev}, {"data", data}};'
    if original.count(old) != 1 or original.count(OLD) != 1:
        raise ValueError('Expected current diagnostic copy and original production initializer exactly once')
    if b'ipc_broadcast_controller_snapshot(m);' not in original:
        raise ValueError('Current shared snapshot broadcaster required')
    candidate = original.replace(old, new, 1)
    if candidate.replace(new, old, 1) != original:
        raise ValueError('Diagnostic-only reverse patch failed')
    before = bound['owned-emitter']
    full = raw[:before['start']] + candidate + raw[before['end']:]
    # This equality protects every gate read, return, counter, log, exception
    # boundary, production initializer and broadcast call outside the two lines.
    if full.replace(new, old, 1) != raw or candidate.count(OLD) != 1:
        raise ValueError('Unexpected change outside diagnostic reference construction')
    callback = bound['producer-callback']['bytes']
    if b'now - g_inputCaptureLastUi >= 80' not in callback:
        raise ValueError('Expected unchanged 80ms telemetry producer gate')
    if original.count(b'tickCostEnabled()') != 3:
        raise ValueError('Emitter must preserve three direct reads plus scope constructor read')
    return candidate, full, {'pass': True, 'diagnosticReferenceIntermediateOnly': True,
        'fullReversePatchExact': True, 'productionInitializerByteIdentical': True,
        'offBranchUnchangedByConstruction': True, 'directGateReads': 3, 'scopeGateReads': 1,
        'diagnosticGateSource': bound['diagnostic-gate']['bytes'].decode('utf-8'),
        'eightyMsProducerProtectedNotExecuted': True, 'addedCatchOrStateOrTimer': False,
        'unmodifiedPrefixSha256': sha(raw[:before['start']]), 'unmodifiedSuffixSha256': sha(raw[before['end']:]),
        'baselineLineEndings': line_endings(raw), 'candidateLineEndings': line_endings(full)}


def reference_header(bound, candidate, baseline_broadcast=None):
    shared = ['diagnostic-gate', 'cost-name-hash', 'ipc-cost-scope', 'utf8-to-utf16', 'target-resolver', 'snapshot-broadcast']
    header = b'#ifndef YMCC_CURRENT_REFERENCE_COPY\n#error Current source requires explicit current mode\n#endif\n'
    # An explicit paired consumer may compare the historical broadcaster with
    # the validated-wire broadcaster. Historical reference-copy mode stays exact.
    paired_broadcast = baseline_broadcast is not None
    if paired_broadcast:
        old_signature = b'static void ipc_broadcast_controller_snapshot(const json& message) {'
        new_signature = b'static void ipc_broadcast_controller_snapshot(const json& message, const std::string* validatedUtf8 = nullptr) {'
        old_call = b'ipc_broadcast_controller_snapshot(m);'
        new_call = b'ipc_broadcast_controller_snapshot(m, validatedWire ? &*validatedWire : nullptr);'
        if (not baseline_broadcast.startswith(old_signature) or
                not bound['snapshot-broadcast']['bytes'].startswith(new_signature) or
                bound['owned-emitter']['bytes'].count(old_call) != 1 or
                candidate.count(new_call) != 1):
            raise ValueError('Validated-wire pair requires both exact signatures and emitter calls')
        header += b''.join(bound[name]['bytes'] for name in shared if name != 'snapshot-broadcast')
        for name, body in [('baseline_broadcast', baseline_broadcast),
                           ('candidate_broadcast', bound['snapshot-broadcast']['bytes'])]:
            if body.count(b'ipc_broadcast_controller_snapshot(') != 1:
                raise ValueError('Broadcaster definition must be uniquely bound')
            header += body.replace(b'ipc_broadcast_controller_snapshot(', name.encode()+b'(', 1)
    else:
        header += b''.join(bound[name]['bytes'] for name in shared)
    original = bound['owned-emitter']['bytes']
    event_value = b'{"event", ev}, {"data", reference}' if paired_broadcast else b'{"event", ev}, {"data", data}'
    mutated_value = event_value.replace(b'{"event", ev}', b'{"event", "mutated.reference.event"}')
    event_negative = candidate.replace(event_value, mutated_value, 1)
    gate_negative = candidate.replace(b'if (tickCostEnabled()) {', b'if (!tickCostEnabled()) {', 1)
    if event_negative == candidate or gate_negative == candidate:
        raise ValueError('Semantic negative mutation did not apply')
    for name, observed in [('baseline_emit', False), ('candidate_emit', False),
                            ('baseline_observed', True), ('candidate_observed', True)]:
        variants = [original] if name.startswith('baseline') else [event_negative, gate_negative, candidate]
        if len(variants) > 1:
            header += b'#if defined(YMCC_REFERENCE_EVENT_NEGATIVE)\n'
        for index, body in enumerate(variants):
            if index == 1:
                header += b'#elif defined(YMCC_REFERENCE_GATE_NEGATIVE)\n'
            elif index == 2:
                header += b'#else\n'
            if observed:
                eol = b'\r\n' if b'\r\n' in body else b'\n'
                if body.count(OLD) != 1:
                    raise ValueError('Original ownership observation anchor must remain')
                body = body.replace(OLD, OLD + eol + b'    observeLocals(ev, data, m);', 1)
            if paired_broadcast:
                if body.count(b'ipc_broadcast_controller_snapshot(') != 1:
                    raise ValueError('Each emitter must call its paired broadcaster once')
                target = b'baseline_broadcast(' if name.startswith('baseline') else b'candidate_broadcast('
                body = body.replace(b'ipc_broadcast_controller_snapshot(', target, 1)
            header += body.replace(b'ipc_emit_owned_gyro_telemetry', name.encode(), 1)
        if len(variants) > 1:
            header += b'#endif\n'
    return header


def reference_process(command, output, name, env, cap, deadline, allowed=(0,)):
    remaining = deadline-time.monotonic()
    if remaining <= 0:
        raise TimeoutError('Shared invocation budget exhausted before ' + name)
    timeout = min(cap, remaining)
    started = dt.datetime.now(dt.timezone.utc).isoformat()
    tick = time.monotonic()
    stdout = stderr = b''
    code = pid = None
    timed_out = False
    spawn_error = cleanup = None
    image_identity = reference_identity(command[0])
    try:
        process = subprocess.Popen(list(map(str, command)), cwd=output, env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        pid = process.pid
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired as error:
            timed_out = True
            stdout, stderr = error.output or b'', error.stderr or b''
            # Exact owned PID, not a name-based product/tool sweep.
            cleanup_process = subprocess.run([r'C:\Windows\System32\taskkill.exe', '/PID', str(pid), '/T', '/F'],
                                             capture_output=True, timeout=15)
            cleanup = {'exitCode': cleanup_process.returncode, 'stdout': cleanup_process.stdout.decode(errors='replace'),
                       'stderr': cleanup_process.stderr.decode(errors='replace')}
            stdout, stderr = process.communicate(timeout=15)
        code = process.returncode
    except OSError as error:
        spawn_error = repr(error)
    finally:
        reference_write(output/(name+'.stdout.log'), stdout)
        reference_write(output/(name+'.stderr.log'), stderr)
        reference_json(output/(name+'.PROCESS.json'), {'command': list(map(str, command)), 'executableIdentityBeforeSpawn':image_identity, 'cwd': str(output), 'pid': pid,
            'startedAtUtc': started, 'endedAtUtc': dt.datetime.now(dt.timezone.utc).isoformat(),
            'wallMs': (time.monotonic()-tick)*1000, 'originalTimeoutSeconds': cap, 'effectiveTimeoutSeconds': timeout,
            'exitCode': code, 'timedOut': timed_out, 'ETIMEDOUT': timed_out, 'spawnError': spawn_error, 'cleanup': cleanup})
    if timed_out or spawn_error or code not in allowed:
        raise RuntimeError(name + ' failed; original process/logs preserved')
    return stdout, stderr, code


def reference_environment(output, deadline):
    env = os.environ.copy()
    for name in list(env):
        if name.upper() in {'PATH','INCLUDE','LIB','LIBPATH'} or name.upper().startswith(
                ('VSCMD','__VSCMD','VCTOOLS','VCINSTALL','VSINSTALL','WINDOWSSDK','UNIVERSALCRT','UCRTVERSION')):
            env.pop(name, None)
    env['PATH'] = r'C:\Windows\System32;C:\Windows;C:\Windows\System32\Wbem;C:\Windows\System32\WindowsPowerShell\v1.0'
    env['VSLANG'] = '1033'
    vc = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
    compiler_dir = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64')
    compiler = compiler_dir/'cl.exe'
    sdk = Path(r'C:\Program Files (x86)\Windows Kits\10')
    version = '10.0.26100.0'
    setup = '@echo off\r\ncall "'+str(vc)+'" '+version+' -vcvars_ver=14.44 >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\n'
    setup += 'set "PATH='+str(compiler_dir)+';C:\\Windows\\System32;%PATH%"\r\n'
    # Match the restored, verified SDK selection rather than ambient SDK paths.
    setup += 'set "INCLUDE='+';'.join(str(sdk/'Include'/version/name) for name in ['ucrt','shared','um','winrt','cppwinrt'])+';%INCLUDE%"\r\n'
    setup += 'set "LIB='+';'.join(str(sdk/'Lib'/version/name/'x64') for name in ['ucrt','um'])+';%LIB%"\r\n'
    setup += 'set PATH\r\nset INCLUDE\r\nset LIB\r\nexit /b 0\r\n'
    reference_write(output/'environment.cmd', setup)
    raw, _, _ = reference_process([r'C:\Windows\System32\cmd.exe', '/d','/c',str(output/'environment.cmd')],
                                  output,'environment',env,30,deadline)
    for line in raw.decode('utf-8',errors='replace').splitlines():
        if '=' in line:
            key, value = line.split('=',1)
            if key and not key.startswith('='):
                existing = next((name for name in env if name.lower()==key.lower()), key)
                env[existing] = value
    if not compiler.is_file() or not compiler.with_name('link.exe').is_file():
        raise FileNotFoundError('Exact restored x64 MSVC compiler/linker required')
    files = [vc, compiler, compiler_dir/'link.exe', Path(sys.executable), Path(r'C:\Windows\System32\cmd.exe')]
    files += [compiler_dir/name for name in ['c1xx.dll','c2.dll','mspdbcore.dll']]
    vcroot = compiler_dir.parents[2]
    files += [vcroot/'lib/x64'/name for name in ['libcpmt.lib','libcpmtd.lib','libcmt.lib','libcmtd.lib',
                                               'libvcruntime.lib','libvcruntimed.lib','oldnames.lib']]
    files += [sdk/'Lib'/version/family/'x64'/name for family,name in
              [('ucrt','libucrt.lib'),('ucrt','libucrtd.lib'),('um','kernel32.lib')]]
    toolchain = {'files': [reference_identity(path) for path in files], 'sdkVersion': version,
                 'pathLength': len(next(value for key,value in env.items() if key.lower()=='path')),
                 'compiler': str(compiler), 'childEnvironmentOnly': True,
                 'environment': {key:value for key,value in env.items() if key.upper() in {'PATH','INCLUDE','LIB','VSLANG'}}}
    reference_json(output/'TOOLCHAIN-IDENTITY.json',toolchain)
    return compiler, env, toolchain


def reference_verify_entries(entries):
    for entry in entries:
        if reference_identity(entry['path']) != entry:
            raise ValueError('Prepared input/tool/artifact drift: '+entry['path'])


def reference_correctness_result(result, corpus):
    expected = {'pass':True,'cases':2828,'inputShapes':22,'topologies':8,'successiveGateScripts':16,
                'strictUtf8ExceptionsCompared':312,'existingMismatchLogCallsCompared':0}
    if any(result['correctness'].get(key)!=value for key,value in expected.items()) or result['assertions']!=89737:
        raise ValueError('Original current-source case selection/semantic counts differ')
    expected_matrix = {(entry['name'],topology,mask) for entry in corpus['inputs'] for topology in range(8) for mask in range(16)}
    matrix = result['caseMatrix']
    actual_matrix = {(entry['input'],entry['topology'],entry['scriptMask']) for entry in matrix}
    # The original 12 constructor/slot/Post-failure controls increment the
    # total but do not append matrix rows. Preserve that original producer.
    if len(matrix)!=2816 or actual_matrix!=expected_matrix or not all(entry['baselineCandidateEquivalent'] for entry in matrix):
        raise ValueError('Original 2816 matrix rows differ; 12 extra controls are counted separately')
    if result['performance'] is not None or result['ownership']['nodesCompared']!=47:
        raise ValueError('Original ownership or cost isolation failed')


def reference_resume_release(previous, output, identities, toolchain, generated):
    previous = Path(previous).resolve()
    if not previous.is_relative_to((REPO.parents[2]/'Testing/YMCC/Evidence').resolve()):
        raise ValueError('Resume must reference development Evidence')
    binding = json.loads((previous/'SOURCE-BINDING.json').read_text(encoding='utf-8'))
    prior_tools = json.loads((previous/'TOOLCHAIN-IDENTITY.json').read_text(encoding='utf-8'))
    if binding['inputs'][:3] != identities[:3] or binding['inputs'][4:] != identities[4:] or prior_tools['files']!=toolchain['files']:
        raise ValueError('Release source/fixture/JSON/tool identity differs; reuse rejected')
    if (previous/'production-extracted.h').read_bytes()!=generated or (previous/'fixture.cpp').read_bytes()!=(output/'fixture.cpp').read_bytes():
        raise ValueError('Release actual compiled input differs; reuse rejected')
    if sha((previous/'runner.py').read_bytes())!=binding['inputs'][3]['sha256']:
        raise ValueError('Original runner snapshot drift')
    for name in ['Release-build','Release-run','InputCorpus']:
        process = json.loads((previous/(name+'.PROCESS.json')).read_text(encoding='utf-8'))
        if process['exitCode']!=0 or process['timedOut'] or process['spawnError']:
            raise ValueError('Original completed Release producer required')
    result = json.loads((previous/'Release-run.stdout.log').read_bytes())
    corpus = json.loads((previous/'InputCorpus.stdout.log').read_bytes())
    reference_correctness_result(result,corpus)
    names = ['SOURCE-BINDING.json','TOOLCHAIN-IDENTITY.json','FAILURE.json','baseline-main.cpp','fixture.cpp','production-extracted.h','json.hpp','runner.py',
             'Release.exe','Release.obj','Release-dependencies.json']
    names += [name+suffix for name in ['Release-build','Release-run','InputCorpus'] for suffix in ['.PROCESS.json','.stdout.log','.stderr.log']]
    references = [reference_identity(previous/name) for name in names]
    reference_write(output/'Release.exe',(previous/'Release.exe').read_bytes())
    reference_write(output/'Release.obj',(previous/'Release.obj').read_bytes())
    reference_write(output/'Release-dependencies.json',(previous/'Release-dependencies.json').read_bytes())
    reference_write(output/'InputCorpus.stdout.log',(previous/'InputCorpus.stdout.log').read_bytes())
    reference_json(output/'RELEASE-SHARED-REFERENCE.json',{'status':'SHARED_REFERENCE','executedThisInvocation':False,
        'actualCorrectnessWasExecutedInOriginalProducer':True,'references':references,
        'originalRawUnchanged':True,'compiledSourceBytesExact':True,'runnerChangedForConsumerRepair':True,
        'executableHashObservedAtIntake':reference_identity(previous/'Release.exe'),
        'limitation':'Original process receipt bound executable path but did not record its hash before execution; artifact hash frozen at this continuation intake, not backdated'})
    return result, corpus, references


def run_reference_current(output, prepared=None, budget_seconds=900, release_from=None):
    if output is None:
        raise ValueError('Current mode requires explicit --out under Testing/YMCC/Evidence')
    output = Path(output).resolve()
    evidence = (REPO.parents[2]/'Testing/YMCC/Evidence').resolve()
    if not output.is_relative_to(evidence) or output == evidence:
        raise ValueError('Current mode output must stay inside development Evidence')
    output.mkdir(parents=True, exist_ok=False)
    began = time.monotonic()
    deadline = began+budget_seconds
    try:
        if prepared:
            return run_reference_prepared_costs(Path(prepared).resolve(), output, deadline)
        reference_json(output/'ACCEPTANCE-POLICY.json',REFERENCE_POLICY)
        source = SOURCE
        raw = source.read_bytes()
        reference_write(output/'baseline-main.cpp',raw)
        bound = reference_bind(raw)
        candidate, full, audit = reference_candidate(raw,bound)
        generated = reference_header(bound,candidate)
        cpp = Path(__file__).with_suffix('.cpp')
        if not cpp.is_file():
            cpp = REPO/'tools/virtual_gyro_envelope_perf_selftest.cpp'
        inputs = [source, REPO/'deps/json/json.hpp', cpp, Path(__file__)]
        mailbox_path = REPO/'native/ipc_result_mailbox.h'
        if 'serial-mailbox-root' in bound:
            inputs.append(mailbox_path)
        identities = [reference_identity(path) for path in inputs]
        if identities[0]['sha256'] != sha(raw):
            raise ValueError('Current source changed during initial freeze')
        patch = ''.join(difflib.unified_diff(raw.decode('utf-8').splitlines(True),full.decode('utf-8').splitlines(True),
                            fromfile='a/native/main.cpp',tofile='b/native/main.cpp',n=3)).encode('utf-8')
        for name, data in [('candidate-main.cpp',full),('baseline-function.cpp',bound['owned-emitter']['bytes']),
                           ('candidate-function.cpp',candidate),('candidate-UNAPPLIED.patch',patch),('production-extracted.h',generated),
                           ('fixture.cpp',cpp.read_bytes()),('runner.py',Path(__file__).read_bytes()),('json.hpp',inputs[1].read_bytes())]:
            reference_write(output/name,data)
        if 'serial-mailbox-root' in bound:
            reference_write(output/'ipc_result_mailbox.h',mailbox_path.read_bytes())
        fragments = output/'bound-fragments'
        fragments.mkdir()
        for name, part in bound.items():
            reference_write(fragments/(name+'.cpp'),part['bytes'])
        reference_json(output/'SOURCE-BINDING.json',{'inputs':identities,'bindings':{name:{'sha256':sha(part['bytes']),
            'bytes':len(part['bytes']),'byteStart':part['start'],'byteEndExclusive':part['end'],'line':part['line']} for name,part in bound.items()},
            'candidateMainSha256':sha(full),'candidateFunctionSha256':sha(candidate),'patchSha256':sha(patch),'patchAudit':audit,
            'compiledExactProductionBodies':['owned-emitter','snapshot-broadcast','diagnostic-gate','cost-name-hash','ipc-cost-scope','utf8-to-utf16','target-resolver'],
            'protectedNotExecuted':[name for name in ['producer-callback','producer-post','producer-const-post','serial-event-type','serial-action-power-tls','serial-mailbox-root','serial-mailbox-include','sensor-snapshot-gate','serial-dispatch'] if name in bound],
            'inertSeams':'Original R15 target/log/UiCost helpers; real Windows conversion and atomics, scripted acquire loads; no app/device/Host/settings actions',
            'productionWrites':[],'candidateApplied':False,'wholeProcessCpuAcceptance':False})
        compiler, env, toolchain = reference_environment(output,deadline)
        profiles = {}
        dependencies = {}
        recipes = {}
        referenced_artifacts = []
        for name, flags in [('Release',['/O2','/DNDEBUG','/MT']),
                             ('Checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd']),
                             ('EventNegative',['/O2','/DNDEBUG','/MT','/DYMCC_REFERENCE_EVENT_NEGATIVE']),
                             ('GateNegative',['/O2','/DNDEBUG','/MT','/DYMCC_REFERENCE_GATE_NEGATIVE'])]:
            reference_verify_entries(identities)
            exe = output/(name+'.exe')
            dep = output/(name+'-dependencies.json')
            argv = ['/nologo','/std:c++20','/utf-8','/EHsc','/fp:precise','/DYMCC_CURRENT_REFERENCE_COPY',*flags,
                    str(output/'fixture.cpp'),'/I'+str(output),'/Fo'+str(output/(name+'.obj')),'/Fe'+str(exe),
                    '/sourceDependencies',str(dep),'/link','/INCREMENTAL:NO']
            recipes[name] = [str(compiler),*argv]
            if name=='Release' and release_from:
                shared, corpus, referenced_artifacts = reference_resume_release(release_from,output,identities,toolchain,generated)
                recipes[name] = json.loads((Path(release_from)/'Release-build.PROCESS.json').read_text(encoding='utf-8'))['command']
            else:
                reference_process(recipes[name],output,name+'-build',env,120,deadline)
            dependency_data = json.loads(dep.read_text(encoding='utf-8-sig'))['Data']
            for dependency in dependency_data['Includes']:
                path = Path(dependency).resolve()
                allowed = [output,compiler.parent.parents[2]/'include',Path(r'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0')]
                if name=='Release' and release_from:
                    allowed.append(Path(release_from).resolve())
                if not any(path.is_relative_to(root) for root in allowed):
                    raise ValueError('Undeclared compiler include root: '+str(path))
                dependencies[str(path)] = reference_identity(path)
            if name in {'EventNegative','GateNegative'}:
                stdout, stderr, code = reference_process([exe,'--reference-negative'],output,name+'-run',env,300,deadline,allowed=(2,))
                expected = b'gate/counter/cost/log/target trace changed'
                if stderr.strip() != expected or stdout:
                    raise ValueError('Negative must fail original behavior assertion, not a generic exception: '+name)
                profiles[name] = {'originalBehaviorAssertionCaught':True,'stderr':stderr.decode().strip(),'exitCode':code,
                                  'directedScope':'one unicode payload, four renderer topology, all four gates on; original compareOutcomes'}
            else:
                if name=='Release' and release_from:
                    result = shared
                else:
                    if name == 'Release':
                        corpus_raw, _, _ = reference_process([exe,'--inputs'],output,'InputCorpus',env,30,deadline)
                        corpus = json.loads(corpus_raw)
                    stdout, _, _ = reference_process([exe,'0'],output,name+'-run',env,300,deadline)
                    result = json.loads(stdout)
                reference_correctness_result(result,corpus)
                reference_json(output/(name+'-case-matrix.json'),result.pop('caseMatrix'))
                reference_json(output/(name+'-trace-witnesses.json'),result.pop('traceWitnesses'))
                profiles[name] = result
        reference_verify_entries(identities+toolchain['files']+list(dependencies.values()))
        command = [str(Path(sys.executable)),str(Path(__file__).resolve()),'--repo-root',str(REPO),'--source-main',str(SOURCE),'--current-reference-copy',
                   '--run-prepared-costs',str(output),'--out',str(output.parent/'Costs')]
        reference_json(output/'COST-PREPARED.json',{'status':'NOT_RUN_AWAITING_MAIN_AGENT_COORDINATION','command':command,
            'policy':REFERENCE_POLICY,'campaignCommands':[[str(output/'Release.exe'),'--cost-only','16']]*2,
            'onlyExplicitInvocationStartsCosts':True,'processCpuAcceptance':False})
        reference_json(output/'RESULT.json',{'status':'CURRENT_REFERENCE_COPY_CORRECTNESS_PASS_COSTS_NOT_RUN',
            'actualCorrectnessThisRun':True,'profiles':profiles,'inputs':identities,'policy':REFERENCE_POLICY,
            'releaseCorrectnessExecutedThisInvocation':not bool(release_from),'releaseSharedReference':str(release_from) if release_from else None,
            'costsExecuted':False,'productionWrites':[],'candidateApplied':False,'wholeProcessCpuAcceptance':False,
            'wallMs':(time.monotonic()-began)*1000})
        # Final immutable closure includes all raw/process logs and compiled
        # executables. The manifest never references or overwrites itself.
        artifacts = [reference_identity(path) for path in sorted(output.rglob('*')) if path.is_file()]
        reference_json(output/'MANIFEST.json',{'inputs':identities,'tools':toolchain['files'],
            'compilerIncludes':list(dependencies.values()),'recipes':recipes,'artifacts':artifacts,'referencedArtifacts':referenced_artifacts})
        print(json.dumps({'status':'CORRECTNESS_PASS_COSTS_NOT_RUN','out':str(output),
            'profiles':{name:result.get('assertions',result.get('originalBehaviorAssertionCaught')) for name,result in profiles.items()},
            'costCommand':command},ensure_ascii=False,indent=2))
    except Exception as error:
        reference_json(output/'FAILURE.json',{'error':repr(error),'wallMs':(time.monotonic()-began)*1000,
            'costsRequested':bool(prepared),'automaticRetry':False,'productionWrites':[]})
        raise


def run_reference_prepared_costs(prepared, output, deadline):
    if not prepared.is_relative_to((REPO.parents[2]/'Testing/YMCC/Evidence').resolve()):
        raise ValueError('Prepared correctness must be development Evidence')
    manifest = json.loads((prepared/'MANIFEST.json').read_text(encoding='utf-8'))
    for group in ['inputs','tools','compilerIncludes','artifacts','referencedArtifacts']:
        reference_verify_entries(manifest[group])
    result = json.loads((prepared/'RESULT.json').read_text(encoding='utf-8'))
    plan = json.loads((prepared/'COST-PREPARED.json').read_text(encoding='utf-8'))
    if result['status']!='CURRENT_REFERENCE_COPY_CORRECTNESS_PASS_COSTS_NOT_RUN' or plan['policy']!=REFERENCE_POLICY:
        raise ValueError('Exact current correctness and predeclared policy required')
    reference_json(output/'ACCEPTANCE-POLICY.json',REFERENCE_POLICY)
    # This append-only claim also prevents retries into a differently named
    # cost output after a measured failure. It is not part of any raw result.
    reference_json(prepared/'COST-CLAIM.json',{'output':str(output),
        'atUtc':dt.datetime.now(dt.timezone.utc).isoformat(),'campaigns':2,'retry':False})
    env = os.environ.copy()
    # Timing has no compiler/runtime discovery; reuse frozen Release.exe.
    campaigns = []
    expected = {(shape,gate,targets) for shape in REFERENCE_POLICY['payloadShapes'] for gate in [False,True] for targets in [1,4]}
    for index in range(2):
        for group in ['inputs','tools','compilerIncludes','artifacts','referencedArtifacts']:
            reference_verify_entries(manifest[group])
        stdout, _, _ = reference_process([prepared/'Release.exe','--cost-only','16'],output,'Campaign'+str(index+1),env,300,deadline)
        report = json.loads(stdout)
        perf = report['performance']
        if report['correctness'] is not None or report['assertions']!=0 or perf['rounds']!=11 or perf['eventsPerTimedVariantPerRound']!=1024:
            raise ValueError('Prepared cost scope or fixed campaign differs')
        rows = perf['scenarios']
        if len(rows)!=16 or {(row['shape'],row['gate'],row['targets']) for row in rows} != expected:
            raise ValueError('Fixed affected and control scenario matrix differs')
        for row in rows:
            if len(row['baselineRoundsCycles'])!=11 or len(row['candidateRoundsCycles'])!=11:
                raise ValueError('Incomplete paired campaign')
            baseline, candidate = row['baselineRoundsCycles'], row['candidateRoundsCycles']
            if any(not (0 < value < float('inf')) for value in baseline+candidate):
                raise ValueError('Invalid cycle samples')
            reductions = [100*(a-b)/a for a,b in zip(baseline,candidate)]
            median_reduction = sorted(reductions)[5]
            wins = sum(b<a for a,b in zip(baseline,candidate))
            counts = row['candidateRequests'] < row['baselineRequests'] if row['gate'] else row['candidateRequests'] == row['baselineRequests']
            sizes = row['candidateRequestedBytes'] < row['baselineRequestedBytes'] if row['gate'] else row['candidateRequestedBytes'] == row['baselineRequestedBytes']
            passed = counts and sizes and (median_reduction>=5 and wins>=10 if row['gate'] else median_reduction>=-5)
            if row['accepted']!=passed or row['fasterCyclePairs']!=wins or abs(row['medianPairedCycleReductionPercent']-median_reduction)>1e-9:
                raise ValueError('Producer acceptance disagrees with original cycle/allocation samples')
        all_rows = all(row['accepted'] for row in rows)
        if perf['allScenariosAccepted']!=all_rows or perf['decision']!=('accept-local-candidate' if all_rows else 'reject') or len(perf['schedule'])!=176:
            raise ValueError('Campaign decision or complete schedule differs')
        campaigns.append(perf)
    reference_verify_entries(manifest['inputs']+manifest['artifacts'])
    accepted = all(campaign['allScenariosAccepted'] for campaign in campaigns)
    reference_json(output/'RESULT.json',{'decision':'accept-local-candidate' if accepted else 'reject',
        'campaigns':campaigns,'preparedManifest':reference_identity(prepared/'MANIFEST.json'),'costPolicy':REFERENCE_POLICY,
        'productionWrites':[],'candidateApplied':False,'wholeProcessCpuAcceptance':False,'automaticRetry':False})
    print(json.dumps({'decision':'accept-local-candidate' if accepted else 'reject','out':str(output)}))


def main():
    global REPO, SOURCE
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--batches', type=int, default=12)
    parser.add_argument('--correctness-only', action='store_true')
    parser.add_argument('--current-reference-copy', action='store_true', help='Current source diagnostic-reference-only private candidate; correctness by default, never implicit costs')
    parser.add_argument('--out', type=Path)
    parser.add_argument('--run-prepared-costs', type=Path, help='Explicitly coordinated two fixed campaigns using a completed current correctness output')
    parser.add_argument('--reference-budget-seconds', type=float, default=900, help='Current invocation shared remaining budget, at most 900 seconds; prior failures are not refunded')
    parser.add_argument('--resume-release-from', type=Path, help='Explicit source/tool/compiled-input checked reuse of a completed Release producer after consumer-only repair')
    parser.add_argument('--repo-root', type=Path, default=REPO)
    parser.add_argument('--source-main', type=Path, help='Explicit private main; source identity remains bound')
    args = parser.parse_args()
    REPO = args.repo_root.resolve()
    SOURCE = (args.source_main or REPO/'native/main.cpp').resolve()
    if re.search(rb'^static void gamepadSerialPostOwnedGyroTelemetry\(', SOURCE.read_bytes(), re.M):
        parser.error('This JSON-only gyro-envelope candidate does not apply to typed telemetry. Use virtual_telemetry_hostframe_current_perf_selftest.py --typed-telemetry-build --phase prepare; --source-main remains available for an explicit historical JSON source.')
    if args.current_reference_copy:
        if args.batches != 12 or (args.run_prepared_costs and (args.correctness_only or args.resume_release_from)) or not (0<args.reference_budget_seconds<=900):
            parser.error('Current costs use the fixed plan; no custom batches or correctness/cost combination')
        return run_reference_current(args.out,args.run_prepared_costs,args.reference_budget_seconds,args.resume_release_from)
    if args.out or args.run_prepared_costs or args.resume_release_from or args.reference_budget_seconds!=900:
        parser.error('--out/--run-prepared-costs require explicit --current-reference-copy')
    assert 1 <= args.batches <= 100
    OUTPUT.mkdir(parents=True, exist_ok=True)
    run = OUTPUT/('run-'+dt.datetime.now().strftime('%Y%m%d-%H%M%S'))
    run.mkdir()
    # Establish the contract on disk before any compile or measurement.
    save_json(run/'acceptance-policy.json', POLICY)
    raw = SOURCE.read_bytes(); bound = bind(raw)
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
        'sourcePath': str(SOURCE), 'baselineMainSha256': sha(raw), 'expectedParentMainSha256': EXPECTED_MAIN,
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
