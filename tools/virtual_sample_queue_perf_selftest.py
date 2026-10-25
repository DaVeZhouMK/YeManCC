"""Extract real before/current sample Queue/Drain/Append; compile offline MSVC fixtures.

No product/device/registry/Host calls. JSON uses production nlohmann::json's default
allocator. Global operator new counters are enabled only for allocation runs;
wall-time runs disable them. Archives are streamed for a single intact sample.
"""
from __future__ import annotations
import argparse, copy, datetime, difflib, hashlib, json, os, pathlib, re, subprocess, zipfile

REPO = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_OUT = pathlib.Path('G:/YeManCC-Work/Build/Validation/VirtualGamepadPerf-20261014/sample-queue')
DEFAULT_BEFORE = DEFAULT_OUT.parent / 'before/native/main.cpp'
DEFAULT_ZIP = pathlib.Path('C:/Users/DaVe/Desktop/日志-侧边栏全开后/YeManCC-logs-20261014-160313--G3E虚拟手柄OK.zip')


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def mask(source: str, comments_only: bool = False) -> str:
    def replacement(m):
        return ' ' * len(m.group()) if not comments_only or m.group().startswith(('//', '/*')) else m.group()
    return re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', replacement, source, flags=re.S)


def extract(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index('{', start)
    masked = mask(source)
    depth = 0
    for i in range(opening, len(source)):
        if masked[i] == '{': depth += 1
        elif masked[i] == '}':
            depth -= 1
            if not depth: return source[start:i+1]
    raise ValueError('Unclosed production function ' + signature)


def normalize(source: str) -> str:
    return re.sub(r'\s+', ' ', mask(source, True)).strip()


def bound_parts(source: str, old: bool) -> dict:
    queue_signature = 'static void inputCaptureQueueSample(' + ('const json& rec' if old else 'json rec') + ') {'
    state_start = source.index('static std::mutex g_inputCaptureSampleQueueMx;')
    state_end = source.index(';', source.index('static constexpr size_t kInputCaptureSampleQueueLimit', state_start))+1
    limit = re.search(r'static constexpr unsigned long long kLogMaxBytes = [^;]+;', source).group()
    producer = extract(source, 'if (wantFullSample) {')
    # Include the preceding frequency gate, without pulling in unrelated BUS code.
    gate_start = source.rfind('bool wantFullSample', 0, source.index('if (wantFullSample) {'))
    gate = source[gate_start:source.index('if (wantFullSample) {')]
    family_start = source.index('inputCaptureQueueSample({{"t",')
    family = source[family_start:source.index(');', family_start)+2]
    return {
        'queue': extract(source, queue_signature),
        'append': extract(source, 'static void inputCaptureAppend(const json& rec) {'),
        'drain': extract(source, 'static void inputCaptureDrainQueuedSamples() {'),
        'queueState': source[state_start:state_end], 'logLimit': limit,
        'sampleProducer': producer, 'queueTransferScope': producer[producer.rfind('{', 0, producer.index('TickPreHostSubScope qScope')):],
        'frequencyGate': gate, 'familyCall': family,
    }


def sample_from_zip(path: pathlib.Path, out: pathlib.Path) -> tuple[dict, dict]:
    with zipfile.ZipFile(path) as z:
        entries = sorted((n for n in z.namelist() if pathlib.PurePosixPath(n).name == 'input-capture.jsonl'))
        if not entries: raise ValueError('No capture entry')
        for entry in entries:
            with z.open(entry) as f:
                for line_no, raw in enumerate(f, 1):
                    try: value = json.loads(raw)
                    except (ValueError, UnicodeDecodeError): continue
                    if isinstance(value, dict) and value.get('kind') == 'sample' and value.get('writerPid') == 10024:
                        (out / 'real-sample-original.jsonl').write_bytes(raw)
                        return value, {'archive': str(path), 'entry': entry, 'line': line_no,
                            'rawLineSha256': sha(raw), 'rawLineBytes': len(raw),
                            'fieldCount': len(value), 'writerPid': 10024,
                            'retainedAllOriginalFields': True}
    raise ValueError('No intact current-PID real sample')


def namespace(parts: dict, name: str) -> str:
    return '\nnamespace ' + name + ' {\n' + '''
static std::mutex g_inputCaptureMx;
static std::wstring g_inputCapturePath;
static std::ofstream g_inputCaptureLog;
static uint64_t g_inputCaptureBytes = 0;
static uint32_t g_inputCaptureSampleWrites = 0;
''' + '\n'.join(parts[k] for k in ('queueState', 'logLimit', 'append', 'queue', 'drain')) + '''
static void fixtureFamilyCall(double gx, double gy, double gz, double ax, double ay, double az) {
''' + parts['familyCall'] + '\n}\n}\n'


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=pathlib.Path, default=DEFAULT_OUT)
    parser.add_argument('--before', type=pathlib.Path, default=DEFAULT_BEFORE)
    parser.add_argument('--sample-zip', type=pathlib.Path, default=DEFAULT_ZIP)
    args = parser.parse_args()
    output = args.output_dir.resolve(); output.mkdir(parents=True, exist_ok=True)
    run = output / ('run-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + str(os.getpid()))
    run.mkdir()
    paths = {'before': args.before.resolve(), 'current': REPO/'native/main.cpp',
             'jsonHeader': REPO/'deps/json/json.hpp', 'policy': REPO/'native/log_frequency_policy.h',
             'harness': REPO/'tools/virtual_sample_queue_perf_selftest.cpp'}
    snapshots = {k: p.read_bytes() for k, p in paths.items()}
    before = bound_parts(snapshots['before'].decode('utf-8-sig').replace('\r\n', '\n'), True)
    current = bound_parts(snapshots['current'].decode('utf-8-sig').replace('\r\n', '\n'), False)
    expected_queue = before['queue'].replace('const json& rec', 'json rec').replace('push_back(rec)', 'push_back(std::move(rec))')
    checks = {
        'only-queue-signature-and-owned-push-changed': normalize(expected_queue) == normalize(current['queue']),
        'append-byte-identical': before['append'] == current['append'],
        'drain-byte-identical': before['drain'] == current['drain'],
        'queue-mutex-capacity-and-drop-state-identical': before['queueState'] == current['queueState'],
        'production-file-limit-identical': before['logLimit'] == current['logLimit'],
        'sample-frequency-gate-identical': before['frequencyGate'] == current['frequencyGate'] and bool(current['frequencyGate']),
        'sample-producer-final-transfer-scope-matches': normalize(before['queueTransferScope'].replace('inputCaptureQueueSample(rec);', 'inputCaptureQueueSample(std::move(rec));')) == normalize(current['queueTransferScope']),
        'family-temporary-call-identical': before['familyCall'] == current['familyCall'],
    }
    binding = {'paths': {k:str(v) for k,v in paths.items()},
        'sha256': {k:sha(v) for k,v in snapshots.items()}, 'sourceChecks': checks,
        'parts': {k:{'beforeSha256':sha(before[k].encode()), 'currentSha256':sha(current[k].encode())} for k in before},
        'scope': 'Extracted production Queue/Drain/Append + state/limit; default JSON allocator; deterministic PID/time only; offline private fixture files.',
        'productionEdited': False, 'hardwareOrProductExecuted': False,
        'wholeProducerHasOtherParentChanges': normalize(before['sampleProducer'].replace('inputCaptureQueueSample(rec);', 'inputCaptureQueueSample(std::move(rec));')) != normalize(current['sampleProducer']),
        'producerScopeNote': 'Other parent diagnostic-construction changes are recorded but excluded from queue-only timing and byte/ownership validation.'}
    (run/'producer-before-current.diff').write_text(''.join(difflib.unified_diff(before['sampleProducer'].splitlines(True),current['sampleProducer'].splitlines(True),fromfile='before-producer',tofile='current-producer')),encoding='utf-8')
    (run/'source-binding.json').write_text(json.dumps(binding,indent=2)+'\n',encoding='utf-8')
    if not all(checks.values()):
        (output/'result.json').write_text(json.dumps(binding,indent=2)+'\n',encoding='utf-8')
        raise ValueError('Bound before/current queue/writer/gate source changed beyond the queue transfer: ' + str(checks))
    sample, provenance = sample_from_zip(args.sample_zip.resolve(), run)
    (run/'real-sample.json').write_text(json.dumps(sample,ensure_ascii=False,separators=(',',':'))+'\n',encoding='utf-8')
    large = copy.deepcopy(sample)
    large['syntheticFullFrame'] = {
        'fixtureOnly': True, 'sequence': 18446744073709551615, 'timestamp100ns': 9223372036854775807,
        'pads': [{'slot':i,'connected':True,'buttons':0x1ffff,'axes':[-1.0,-0.0,0.125,1.0,0.5,0.875],
                  'motion':copy.deepcopy(sample.get('imu',{})),
                  'matrix':copy.deepcopy(sample.get('hcMatrixCandidate',{})),
                  'frameProof':{'source':'native-canonical-pipe-frame','fullSample':copy.deepcopy(sample)}} for i in range(4)],
        'escapeUtf8': '模拟整帧/gyro\\path\nquoted"🚀', 'diagnosticDetail': 'FULL-FIELD-'*256}
    assert all(large[k] == v for k,v in sample.items())
    (run/'large-full-frame.json').write_text(json.dumps(large,ensure_ascii=False,separators=(',',':'))+'\n',encoding='utf-8')
    provenance['syntheticRetainsEveryRealField'] = True
    provenance['syntheticNotes'] = 'Real sample plus four complete sample-bearing pad records and complete synthetic frame diagnostics; no original field removed or quantized.'
    (run/'payload-provenance.json').write_text(json.dumps(provenance,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (run/'production-extracted.h').write_text(namespace(before,'oldqueue')+namespace(current,'newqueue'),encoding='utf-8',newline='\n')
    (run/'bound-harness.cpp').write_bytes(snapshots['harness'])
    vcvars = pathlib.Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
    result = dict(binding, runDirectory=str(run), payload=provenance, profiles={})
    for profile, flags in [('release','/O2 /DNDEBUG /MT'), ('checked','/Od /RTC1 /D_DEBUG /MTd')]:
        exe = run/(profile+'.exe')
        command = f'call "{vcvars}" >nul\ncl.exe /nologo /std:c++17 /EHsc /W4 /WX /utf-8 {flags} "{run / "bound-harness.cpp"}" /I"{run}" /I"{REPO / "deps/json"}" /I"{REPO / "native"}" /Fo"{run / (profile+".obj")}" /Fe"{exe}" /link /INCREMENTAL:NO\n'
        batch = run/(profile+'-build.cmd'); batch.write_text('@echo off\n'+command,encoding='utf-8')
        build = subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,text=True,errors='replace',timeout=90)
        (run/(profile+'-build.log')).write_text(build.stdout+build.stderr,encoding='utf-8')
        if build.returncode:
            print(build.stdout+build.stderr); raise RuntimeError('Compile failed '+profile)
        completed = subprocess.run([str(exe),str(run/'real-sample.json'),str(run/'large-full-frame.json'),str(run/profile)],cwd=run,capture_output=True,text=True,errors='replace',timeout=120)
        (run/(profile+'-stdout.json')).write_text(completed.stdout,encoding='utf-8')
        (run/(profile+'-stderr.log')).write_text(completed.stderr,encoding='utf-8')
        if completed.returncode:
            print(completed.stdout+completed.stderr); raise RuntimeError('Runtime failed '+profile)
        result['profiles'][profile] = json.loads(completed.stdout)
        result['profiles'][profile]['binarySha256'] = sha(exe.read_bytes())
        print(json.dumps({'profile':profile, 'assertions':result['profiles'][profile]['assertions'],
                          'cases':len(result['profiles'][profile]['cases']),
                          'failures':result['profiles'][profile]['failures'],
                          'benchmarks':result['profiles'][profile]['benchmarks']},indent=2),flush=True)
    latest = {k:p.read_bytes() for k,p in paths.items()}
    latest_parts = bound_parts(latest['current'].decode('utf-8-sig').replace('\r\n','\n'), False)
    result['sourceChecks']['bound-parts-stable-during-test'] = latest_parts == current
    result['sourceChecks']['other-fixture-inputs-stable'] = all(latest[k] == snapshots[k] for k in paths if k != 'current')
    result['wholeMainStableDuringTest'] = latest['current'] == snapshots['current']
    result['latestMainSha256'] = sha(latest['current'])
    result['sourceDriftPolicy'] = 'Unrelated main.cpp edits disclosed; bound queue/writer/producer/gate changes fail.'
    result['passed'] = all(result['sourceChecks'].values()) and all(v['failures']==0 for v in result['profiles'].values())
    (run/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (output/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
