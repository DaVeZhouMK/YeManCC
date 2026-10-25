#!/usr/bin/env python3
"""R16 offline, source-extracted sample JSON construction fixture.

Default prepares an UNAPPLIED candidate and runs Release/Checked correctness only.
Formal timing requires a separate --benchmark-ready-run invocation after the parent
has explicitly released its measurement mutex. All writes stay in the R16 subtree.
Final verification: --final-actual-source --accepted-run <historical measured run>.
It compiles only current production blocks after exact accepted-source binding and
runs Release/Checked correctness without a new benchmark or git apply check.
No production executable, physical input, service, registry, driver, or Steam runs.
"""
from __future__ import annotations
import argparse, datetime, difflib, hashlib, json, math, os, re, subprocess, sys, zipfile
from pathlib import Path
sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[1]
WORKSPACE = REPO.parents[2]
OUTPUT = WORKSPACE / 'Build/Validation/VirtualInputPerf-R16-20261015/sample-tree-local'
PARENT = WORKSPACE / 'Build/Validation/VirtualInputPerf-NextNodes-20261015/parent-readonly-node-review.json'
VCVARS = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
EXPECTED_MAIN = '79340b9dcba916e2a02cc2e1da5d9aee8ba01a896ac570bb627df28c930d0ac2'
PLAN = {'campaigns': 2, 'roundsPerCampaign': 11, 'minimumFasterRounds': 10,
        'minimumMedianReductionPercent': 5.0, 'requireFewerAllocationRequests': True,
        'requireFewerRequestedBytes': True, 'batchSize': 32, 'batchesPerMeasurement': 24,
        'medianRule': 'Both median paired reduction and ratio of medians must be at least 5 percent',
        'warmupBatches': 6, 'scopes': ['trees', 'owned-record-dump', 'writer-copy-dump'],
        'decision': 'Every scenario and scope must meet every criterion in each campaign. No threshold relaxation.'}


def sha(data):
    return hashlib.sha256(data.encode('utf-8') if isinstance(data, str) else data).hexdigest()


def write(path, data):
    path = path.resolve()
    assert path.is_relative_to(OUTPUT.resolve()), ('write outside authorized output', path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data.encode('utf-8') if isinstance(data, str) else data)


def save_json(path, value):
    write(path, json.dumps(value, ensure_ascii=False, indent=2) + '\n')


def clean(text):
    pattern = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/'
    def sub(m):
        s = m.group()
        return ''.join('\n' if c == '\n' else ' ' for c in s) if s.startswith(('//', '/*')) else s
    return re.sub(pattern, sub, text)


def match_brace(text, opening):
    masked = re.sub(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', lambda m: ' ' * len(m.group()), clean(text))
    depth = 0
    for i in range(opening, len(text)):
        if masked[i] == '{': depth += 1
        elif masked[i] == '}':
            depth -= 1
            if not depth: return i
    raise AssertionError('unclosed source brace')


def parts(text):
    result, start, stack, quote, escape = [], 0, [], None, False
    for i, c in enumerate(text):
        if quote:
            if escape: escape = False
            elif c == '\\': escape = True
            elif c == quote: quote = None
        elif c in '\"\'': quote = c
        elif c in '{([': stack.append(c)
        elif c in '})]':
            assert stack.pop() == {'}': '{', ')': '(', ']': '['}[c]
        elif c == ',' and not stack:
            result.append(text[start:i].strip()); start = i + 1
    result.append(text[start:].strip())
    return [x for x in result if x]


def pairs(expr):
    expr = clean(expr).strip()
    assert expr.startswith('{') and match_brace(expr, 0) == len(expr)-1
    result = []
    for p in parts(expr[1:-1]):
        assert p.startswith('{') and p.endswith('}')
        entry = parts(p[1:-1])
        assert len(entry) == 2 and re.fullmatch(r'"\w+"', entry[0]), entry
        result.append((entry[0], entry[1]))
    assert len({k for k, _ in result}) == len(result)
    return result


def leaves(expr, prefix=()):
    result = []
    for key, val in pairs(expr):
        path = prefix + (json.loads(key),)
        if val.startswith('{'): result.extend(leaves(val, path))
        else:
            assert re.fullmatch(r'\w+', val), ('nontrivial scalar needs explicit review', val)
            result.append((path, val))
    return result


def explicit_object(target, expr, created):
    rows = ['        json ' + target + ' = json::object();']
    for key, val in pairs(expr):
        if val.startswith('{'):
            child = target + '_' + json.loads(key)
            created.append(child)
            rows.extend(explicit_object(child, val, created))
            val = 'std::move(' + child + ')'
        rows.append('        ' + target + '[' + key + '] = ' + val + ';')
    return rows


def block(text, name):
    matches = list(re.finditer(r'(?m)^        json ' + name + r' = \{', text))
    assert len(matches) == 1, (name, len(matches))
    start = matches[0].start(); opening = text.index('{', start)
    close = match_brace(text, opening)
    assert text[close+1] == ';'
    return start, close + 2, text[opening:close+1]


def source_function(text, signature):
    matches = list(re.finditer(re.escape(signature) + r'[^;{]*\{', clean(text)))
    assert len(matches) == 1, (signature, len(matches))
    start = matches[0].start(); opening = text.index('{', start)
    return text[start:match_brace(text, opening)+1]


def source_binding(text):
    callback = source_function(text, 'static void inputCaptureOnGamepad(')
    full = callback[callback.index('    if (wantFullSample) {'):]
    full = full[:match_brace(full, full.index('{'))+1]
    rec_start = full.index('        rec = {')
    rec_end = match_brace(full, full.index('{', rec_start))+2
    transfers = re.search(r'        rec\["imu"\] = std::move\(imuObj\);[\s\S]*?        rec\["testLane"\] = std::move\(testLaneLiveObj\);', full).group()
    inactive_start = full.index('        if (!(gyroSequence != 0 || accelSequence != 0)) {')
    inactive_end = match_brace(full, full.index('{', inactive_start))+1
    logger = source_function(text, 'static void inputCaptureAppend(const json& rec)')
    writer_start = logger.index('    auto record = rec;')
    writer_end = logger.index('    const auto line = record.dump();', writer_start)+len('    const auto line = record.dump();')
    bound = {'callback': callback, 'sample-producer': full, 'record': full[rec_start:rec_end],
             'record-transfers': transfers, 'inactive-override': full[inactive_start:inactive_end],
             'logger': logger, 'writer-copy-dump': logger[writer_start:writer_end]}
    # Full callback binds all diagnostics/gates/control/sampling/freshness/hash/submit
    # outside the two replacement anchors; unrelated main.cpp edits remain allowed.
    groups = ['double gx =', 'bool gyro =', 'bool accel =', 'UINT64 gyroTimestamp100ns =',
              'double gyroTimeOfDayMs =', 'ULONGLONG gyroReceiptTick =', 'UINT64 gyroSequence =',
              'UINT64 gyroBindingGeneration =', 'double mgx =', 'double cgx =', 'float gravityX =',
              'float linearAccelX =', 'float orientationW =', 'float psx =', 'float ox =',
              'bool steady =', 'const bool sameBindingGeneration =', 'const UINT64 activeBindingGeneration=',
              'const bool currentBindingGeneration=', 'const ULONGLONG now =', 'const float motionDeltaSeconds =',
              'const XINPUT_GAMEPAD canonicalPad =']
    declarations = []
    for needle in groups:
        start = callback.index(needle)
        declarations.append(callback[start:callback.index(';', start)+1])
    for needle in ['static void* g_gm =', 'static unsigned long long g_inputCaptureBusTickGlobal =']:
        start = text.index(needle); declarations.append(text[start:text.index(';', start)+1])
    config = source_function(text, 'struct RealStickTestConfig')
    selector = source_function(text, 'struct HcMotionSelectorReceipt')
    for name in ['standaloneTestLane', 'motionTriggerResolved', 'aimingSightsTriggerResolved']:
        declarations.append(re.search(r'bool ' + name + r'\s*=\s*[^;]+;', config).group())
    declarations.append(re.search(r'bool resolved\s*=\s*[^;]+;', selector).group())
    bound['type-declarations'] = '\n'.join(declarations)
    bound['callback-signature'] = callback[:callback.index('{')]
    return bound, declarations


def scalar_types(declarations, names):
    result = {}
    for d in declarations:
        m = re.match(r'(const\s+)?(unsigned long long|double|float|bool|UINT64|ULONGLONG|XINPUT_GAMEPAD)\s+(.*);', clean(d), re.S)
        if not m: continue
        for p in parts(m.group(3)):
            name = p.split('=')[0].strip()
            if name in names: result[name] = ((m.group(1) or '') + m.group(2)).strip()
    assert set(result) == set(names), (set(names)-set(result), result)
    assert result['currentBindingGeneration'] == 'const bool'
    return result


def aliases(types, prefix='values'):
    return '\n'.join('    ' + typ + ' ' + name + ' = ' + prefix + '.' + name + ';\n    static_assert(std::is_same_v<decltype(' + name + '), ' + typ + '>);' for name, typ in types.items())


def fixture_header(baseline, candidate, binding, declarations, types):
    fields = '\n'.join('    ' + typ.removeprefix('const ') + ' ' + name + '{};' for name, typ in types.items())
    generated = 'struct Scalars {\n' + fields + '\n};\nstruct Trees {json imu; json event;};\n'
    for name, source in [('baselineTrees', baseline), ('candidateTrees', candidate)]:
        blocks = []
        for var in ['imuObj', 'eventObj']:
            if name == 'baselineTrees':
                a, b, _ = block(source, var); blocks.append(source[a:b])
            else: blocks.append(source[var])
        generated += '\nstatic NOINLINE Trees ' + name + '(const Scalars& values) {\n' + aliases(types) + '\n' + '\n'.join(blocks) + '\n    return {std::move(imuObj), std::move(eventObj)};\n}\n'
    generated += '\nstatic std::vector<uint64_t> scalarFingerprint(const Scalars& values) {\n    return {\n'
    for name, typ in types.items():
        base = typ.removeprefix('const ')
        code = 'std::bit_cast<uint64_t>(values.'+name+')' if base == 'double' else ('uint64_t(std::bit_cast<uint32_t>(values.'+name+'))' if base == 'float' else 'uint64_t(values.'+name+')')
        generated += '        ' + code + ',\n'
    generated += '    };\n}\n'
    for label, typ, ctype in [('double', 'double', 'double'), ('float', 'float', 'float'), ('unsigned', 'UINT64', 'uint64_t')]:
        matching = [name for name, t in types.items() if (t.removeprefix('const ') == typ or label == 'unsigned' and t.removeprefix('const ') == 'ULONGLONG')]
        generated += '\nstatic void fill' + label.title() + '(Scalars& values, '+ctype+' x) {\n' + '\n'.join('    values.'+name+' = x;' for name in matching) + '\n}\n'
    observer_types = {'now': 'const ULONGLONG', 'motionDeltaSeconds': 'const float', 'canonicalPad': 'const XINPUT_GAMEPAD',
                     'ox': 'float', 'oy': 'float', 'oz': 'float', 'conf': 'float', 'steady': 'bool',
                     'psx': 'float', 'psy': 'float', 'wsx': 'float', 'wsy': 'float'}
    alltypes = scalar_types(declarations, list(types) + list(observer_types))
    assert all(alltypes[k] == v for k,v in observer_types.items())
    generated += '\nstatic NOINLINE json observedRecord(const Scalars& values, const RecordInput& context, Trees&& trees, json&& hcCandidateObj, json&& testLaneLiveObj, json&& extras) {\n'
    generated += aliases({k: types[k] for k in ['gyroSequence', 'accelSequence']}) + '\n'
    generated += aliases(observer_types, 'context') + '''
    const unsigned long long g_inputCaptureBusTickGlobal = context.g_inputCaptureBusTickGlobal;
    const bool live = context.live;
    const DWORD primarySlot = context.primarySlot, connectedMask = context.connectedMask;
    const std::string& inputSource = context.inputSource;
    const std::string& matrixIdentity = context.matrixIdentity;
    const bool hcMatrixAdmitted = context.hcMatrixAdmitted;
    const auto& g_realStickTest = context.g_realStickTest;
    const auto& g_hcMotionSelector = context.g_hcMotionSelector;
    void* g_gm = context.g_gm;
    json imuObj = std::move(trees.imu), eventObj = std::move(trees.event), rec;
'''
    generated += binding['record'] + '\n' + binding['record-transfers'] + '''
    // Prepared observer-only delta fields; real fingerprint/gates are never executed.
    for (auto it = extras.begin(); it != extras.end(); ++it) rec[it.key()] = std::move(it.value());
''' + binding['inactive-override'] + '\n    return rec;\n}\n'
    generated += '\nstatic NOINLINE std::string writerDump(const json& rec, DWORD fixturePid) {\n'
    generated += binding['writer-copy-dump'].replace('GetCurrentProcessId()', 'fixturePid')
    generated += '\n    return line;\n}\n'
    return generated.replace('\r\n', '\n')


def archive_inputs(run, review_path=PARENT):
    parent_raw = review_path.read_bytes(); parent = json.loads(parent_raw.decode('utf-8-sig'))
    assert parent['onlySpecifiedMsi20261014Evidence'] and parent['currentMainSha256'] == EXPECTED_MAIN
    archive = Path(parent['archive']); archive_raw = archive.read_bytes()
    assert sha(archive_raw) == parent['archiveSha256']
    rows, refs = [], []
    with zipfile.ZipFile(archive) as z:
        for entry in parent['inputLogBindings']:
            names = [n for n in z.namelist() if n == entry['entry']]
            assert len(names) == 1
            raw = z.read(names[0]); assert len(raw) == entry['bytes'] and sha(raw) == entry['sha256']
            for line_no, line in enumerate(raw.splitlines(), 1):
                try: value = json.loads(line)
                except (ValueError, UnicodeDecodeError): continue
                if value.get('kind') == 'sample':
                    rows.append(value); refs.append({'entry': names[0], 'line': line_no, 'rawSha256': sha(line)})
    counts = {'totalSampleRows': len(rows), 'rowsWithImuFields': sum('gx' in r['imu'] for r in rows),
              'inactiveMarkerRows': sum(r['imu'].get('imuActive') is False for r in rows)}
    assert all(counts[k] == parent['sampleCounts'][k] for k in counts)
    active = [i for i,r in enumerate(rows) if 'gx' in r['imu']]
    inactive = next(i for i,r in enumerate(rows) if r['imu'].get('imuActive') is False)
    delta = max(active, key=lambda i: len(json.dumps(rows[i])))
    selected = [('active-real-early', active[0]), ('active-real-late', active[-1]),
                ('active-real-delta', delta), ('inactive-real', inactive),
                ('active-gyro-only-synthetic', active[len(active)//2]),
                ('active-accel-only-synthetic', active[len(active)//2])]
    save_json(run/'samples.json', [{'name': name, 'sample': rows[i], 'source': refs[i],
                                  'syntheticFlagVariation': 'synthetic' in name} for name,i in selected])
    save_json(run/'all-authorized-samples.json', rows)
    write(run/'parent-readonly-node-review.json', parent_raw)
    return {'archive': str(archive), 'archiveSha256': sha(archive_raw), 'parentReviewSha256': sha(parent_raw),
            'counts': counts, 'entries': parent['inputLogBindings'], 'selected': [{'name': n, **refs[i]} for n,i in selected]}


def check_rebind(run):
    manifest = json.loads((run/'source-hash-index.json').read_text(encoding='utf-8'))
    now_raw = (REPO/'native/main.cpp').read_bytes()
    bound, _ = source_binding(now_raw.decode('utf-8-sig'))
    checks = {k: sha(v) == expected for k,expected in manifest['boundSourceHashes'].items() for v in [bound[k]]}
    for path, expected in manifest['inputHashes'].items(): checks[path] = sha((REPO/path).read_bytes()) == expected
    for name, expected in manifest['artifactHashes'].items(): checks['artifact:'+name] = sha((run/name).read_bytes()) == expected
    checks['acceptance-plan-fixed'] = json.loads((run/'acceptance-plan.json').read_text(encoding='utf-8')) == PLAN
    save_json(run/'last-rebind.json', {'checks': checks, 'allBoundStable': all(checks.values()),
                                     'currentMainSha256': sha(now_raw), 'wholeMainUnchanged': sha(now_raw) == manifest['baselineMainSha256'],
                                     'unrelatedMainEditsAllowed': True})
    assert all(checks.values()), ('bound drift; re-prepare before measurement', checks)
    return sha(now_raw)


def invoke(exe, mode, run, timeout):
    result = subprocess.run([str(exe), mode, str(run/'samples.json'), str(run/'all-authorized-samples.json')],
                            cwd=run, capture_output=True, encoding='utf-8', errors='replace', timeout=timeout)
    write(run/(exe.stem+'-'+mode+'.json'), result.stdout)
    write(run/(exe.stem+'-'+mode+'.stderr.log'), result.stderr)
    assert result.returncode == 0, (exe.name, result.returncode, result.stderr)
    parsed = json.loads(result.stdout); assert parsed['correctnessPass']
    return parsed


def prepare():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    run = OUTPUT / ('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()))
    run.mkdir()
    raw = (REPO/'native/main.cpp').read_bytes(); source = raw.decode('utf-8-sig')
    assert sha(raw) == EXPECTED_MAIN, 'Initial capture must match the explicitly authorized main SHA'
    bound, declarations = source_binding(source)
    candidate_blocks, replacements, sequence, children = {}, [], {}, []
    for name in ['imuObj', 'eventObj']:
        a,b,expr = block(source, name)
        seq = leaves(expr); sequence[name] = [{'path': p, 'expression': x} for p,x in seq]
        rows = explicit_object(name, expr, children)
        comments = re.findall(r'//[^\r\n]*', source[a:b])
        rows.extend('        '+comment for comment in comments)
        nl = '\r\n' if '\r\n' in source[a:b] else '\n'
        new = nl.join(rows)
        expected_scalar_order = [x for _,x in seq]
        actual_scalar_order = re.findall(r'\["\w+"\] = (\w+);', clean(new))
        assert actual_scalar_order == expected_scalar_order, (name, actual_scalar_order, expected_scalar_order)
        assert not re.search(r'\b(if|else|return|throw|new)\b', clean(new))
        for child in children:
            if child.startswith(name+'_'):
                assert source.count(child) == 0 and new.count('std::move('+child+')') == 1
        candidate_blocks[name] = new
        replacements.append((a,b,new))
        write(run/(name+'.baseline.cpp'), source[a:b])
        write(run/(name+'.candidate.cpp'), new)
    candidate = source
    for a,b,new in reversed(replacements): candidate = candidate[:a] + new + candidate[b:]
    candidate_raw = (b'\xef\xbb\xbf' if raw.startswith(b'\xef\xbb\xbf') else b'') + candidate.encode('utf-8')
    assert raw.count(b'\n')-raw.count(b'\r\n') == 103
    assert candidate_raw.count(b'\n')-candidate_raw.count(b'\r\n') == 103
    # Reconstruct baseline by reversing exact replacements, proving outside bytes.
    restored = candidate
    for a,b,new in replacements:
        assert restored.count(new) == 1
        restored = restored.replace(new, source[a:b], 1)
    assert restored == source
    all_leaves = [x for name in sequence for x in sequence[name]]
    names = list(dict.fromkeys(x['expression'] for x in all_leaves))
    types = scalar_types(declarations, names)
    assert len(all_leaves) == 37 and len(names) == 37
    write(run/'baseline-main.cpp', raw); write(run/'candidate-main.cpp', candidate_raw)
    patch = ''.join(difflib.unified_diff(source.splitlines(True), candidate.splitlines(True), fromfile='a/native/main.cpp', tofile='b/native/main.cpp', n=3))
    write(run/'candidate-UNAPPLIED.patch', patch)
    for name,value in bound.items(): write(run/('source-'+name+'.cpp.txt'), value)
    write(run/'extracted.hpp', fixture_header(source, candidate_blocks, bound, declarations, types))
    write(run/'json.hpp', (REPO/'deps/json/json.hpp').read_bytes())
    write(run/'fixture.cpp', (REPO/'tools/virtual_sample_tree_perf_selftest.cpp').read_bytes())
    save_json(run/'acceptance-plan.json', PLAN)
    provenance = archive_inputs(run); save_json(run/'provenance.json', provenance)
    save_json(run/'expression-sequence.json', {'blocks': sequence, 'types': types, 'leafCount': 37,
             'scalarExpressionSequenceUnchanged': True, 'childObjectsMovedOnce': children,
             'sourceDeclarationBindings': declarations})
    profile_paths = []
    for profile, opts in [('Release', ['/O2', '/DNDEBUG', '/MT']), ('Checked', ['/Od', '/RTC1', '/D_DEBUG', '/D_ITERATOR_DEBUG_LEVEL=2', '/MTd'])]:
        exe = run/(profile+'.exe')
        command = ['cl', '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/fp:precise', *opts,
                   str(run/'fixture.cpp'), '/I'+str(run), '/Fo'+str(run/(profile+'.obj')), '/Fe'+str(exe)]
        bat = run/(profile+'-build.bat')
        write(bat, '@echo off\nset VSLANG=1033\ncall "'+str(VCVARS)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(command)+'\n')
        compiled = subprocess.run(['cmd.exe', '/d', '/c', str(bat)], cwd=run, capture_output=True, encoding='utf-8', errors='replace', timeout=150)
        write(run/(profile+'-build.log'), compiled.stdout+compiled.stderr)
        assert compiled.returncode == 0, str(run/(profile+'-build.log'))
        print(profile+': compiled source-extracted fixture', flush=True)
        profile_paths.append((profile,exe))
    results = []
    for profile,exe in profile_paths:
        result = invoke(exe, 'verify', run, 180)
        results.append({'profile': profile, **result})
        print(profile+': correctness passed; '+str(result['assertions'])+' assertions; no formal timing', flush=True)
    paths = ['deps/json/json.hpp', 'tools/virtual_sample_tree_perf_selftest.cpp', 'tools/virtual_sample_tree_perf_selftest.py']
    artifacts = ['baseline-main.cpp','candidate-main.cpp','candidate-UNAPPLIED.patch','extracted.hpp','fixture.cpp','json.hpp','samples.json','all-authorized-samples.json','acceptance-plan.json','Release.exe','Checked.exe']
    manifest = {'baselineMainSha256': sha(raw), 'candidateMainSha256': sha(candidate_raw),
                'boundSourceHashes': {k: sha(v) for k,v in bound.items()},
                'inputHashes': {p: sha((REPO/p).read_bytes()) for p in paths},
                'artifactHashes': {p: sha((run/p).read_bytes()) for p in artifacts},
                'sourceBlockHashes': {n:{'baseline':sha(source[a:b]), 'candidate':sha(new)} for n,(a,b,new) in zip(['imuObj','eventObj'],replacements)},
                'newlineCounts': {'baselineCRLF':raw.count(b'\r\n'), 'candidateCRLF':candidate_raw.count(b'\r\n'), 'baselineStandaloneLF':103, 'candidateStandaloneLF':103},
                'rawOutsideAnchorsPreserved': restored == source, 'candidateApplied': False,
                'jsonRefOwnership': 'Owned json_ref nodes move in moved_or_copied(); no claim of rvalue subtree deep copies. Allocation counters test temporary request savings.',
                'declarationBindingScope': 'Exact scalar declared types; values supplied offline. Callback/producer/logger bytes are bound, not executed.',
                'initialGitHead': subprocess.check_output(['git','-C',str(REPO),'rev-parse','HEAD'],text=True).strip()}
    save_json(run/'source-hash-index.json', manifest)
    current_sha = check_rebind(run)
    report = {'stage':'correctness-ready-awaiting-parent-benchmark-go','correctnessPass':True,'candidateAccepted':None,
              'formalTimingExecuted':False,'runDirectory':str(run),'manifest':manifest,'profiles':results,
              'provenance':provenance,'acceptancePlan':PLAN,'currentMainSha256':current_sha,
              'productionWritten':False,'installedProductOrPhysicalApisExecuted':False,
              'measurementScope':'Actual two builders + destruction; current owned record + dump; current writer copy + pid assignment + dump. Other prepared trees/setup excluded. No whole-process CPU or Ctrl/exit/release causality claim.'}
    save_json(run/'correctness-ready.json',report); save_json(OUTPUT/'correctness-ready.json',report)
    write(run/'READY.txt','Correctness ready. No formal timing run. Await explicit: R15 measurement finished, benchmark go.\n')
    print(json.dumps({'stage':report['stage'],'runDirectory':str(run),'candidateSha256':sha(candidate_raw),'patchSha256':sha(patch)},indent=2),flush=True)


def benchmark(run):
    run=run.resolve(); assert run.is_relative_to(OUTPUT.resolve())
    ready=json.loads((run/'correctness-ready.json').read_text(encoding='utf-8'))
    assert ready['correctnessPass'] and not ready['formalTimingExecuted']
    check_rebind(run)
    result=invoke(run/'Release.exe','benchmark',run,240)
    current_sha=check_rebind(run)
    report={**ready,'stage':'measured','formalTimingExecuted':True,'candidateAccepted':result['candidateAccepted'],
            'benchmark':result,'currentMainSha256':current_sha}
    save_json(run/'result.json',report);save_json(OUTPUT/'result.json',report)
    print(json.dumps({'stage':'measured','candidateAccepted':result['candidateAccepted'],'runDirectory':str(run)},indent=2),flush=True)



# This final mode is intentionally separate from legacy check_rebind(): the user
# authorized a runner edit, while the measured runner and all evidence stay frozen.
FINAL_ACCEPTED_RUN = OUTPUT / 'run-20261015-062238-40784'
FINAL_ACCEPTED_RESULT_SHA = '90b354c31ec607d0a1e7ee5435bf3e00c73c36120bbe321d9486cc741b78ab20'
FINAL_ACCEPTED_INDEX_SHA = '5eee425acd5d8770a9a078622c1b3ee534be59ac9f5847cc6e846d86911b87ea'
FINAL_CANDIDATE_SHA = '63bc9ba0564c2dda82627ebd8dce123db59e1c47baa5613509177531b8398ba9'


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def load_accepted_proof(run):
    run = run.resolve()
    require(run == FINAL_ACCEPTED_RUN.resolve(), 'Final verification must use the explicitly accepted R16 historical run')
    index_raw = (run/'source-hash-index.json').read_bytes()
    result_raw = (run/'result.json').read_bytes()
    require(sha(index_raw) == FINAL_ACCEPTED_INDEX_SHA, 'Historical source hash index changed')
    require(sha(result_raw) == FINAL_ACCEPTED_RESULT_SHA, 'Historical measured result changed')
    index, result = json.loads(index_raw), json.loads(result_raw)
    require(index['baselineMainSha256'] == EXPECTED_MAIN and index['candidateMainSha256'] == FINAL_CANDIDATE_SHA,
            'Historical main snapshots do not match the accepted candidate')
    require(result['candidateAccepted'] and result['formalTimingExecuted'], 'Historical acceptance proof missing')
    require(result['manifest'] == index and result['finalSourceHashIndexSha256'] == sha(index_raw),
            'Historical result/index binding differs')
    for name, expected in index['artifactHashes'].items():
        require(sha((run/name).read_bytes()) == expected, 'Historical artifact drift: '+name)
    for relative, frozen in [('deps/json/json.hpp','json.hpp'),
                             ('tools/virtual_sample_tree_perf_selftest.cpp','fixture.cpp'),
                             ('tools/virtual_sample_tree_perf_selftest.py','virtual_sample_tree_perf_selftest.py')]:
        require(sha((run/frozen).read_bytes()) == index['inputHashes'][relative],
                'Measured input snapshot drift: '+relative)
    require(json.loads((run/'acceptance-plan.json').read_text(encoding='utf-8')) == PLAN,
            'Frozen acceptance thresholds changed')
    require(result['acceptancePlan'] == PLAN, 'Historical report thresholds changed')
    for profile in result['profiles']:
        require(profile['correctnessPass'] and not profile['formalTimingExecuted'], 'Historical correctness profile failed')
    require({p['profile'] for p in result['profiles']} == {'Release','Checked'}, 'Historical correctness profiles incomplete')
    benchmark = result['benchmark']
    require(benchmark['correctnessPass'] and benchmark['candidateAccepted'] and benchmark['formalTimingExecuted'],
            'Historical benchmark proof is not accepted')
    require(len(benchmark['campaigns']) == PLAN['campaigns'], 'Historical campaign count differs')
    summaries = []
    for campaign in benchmark['campaigns']:
        require(len(campaign['results']) == 18, 'Historical scenario/scope count differs')
        for row in campaign['results']:
            rounds = row['rounds']
            require([r['round'] for r in rounds] == list(range(1,12)), 'Historical paired round sequence changed')
            before = [r['baselineNsPerOperation'] for r in rounds]
            after = [r['candidateNsPerOperation'] for r in rounds]
            require(all(math.isfinite(x) and x > 0 for x in before+after), 'Invalid historical timing value')
            faster = sum(a < b for b,a in zip(before,after))
            med = lambda xs: sorted(xs)[len(xs)//2]
            paired = med([(b-a)/b*100 for b,a in zip(before,after)])
            ratio = (med(before)-med(after))/med(before)*100
            alloc = row['allocation']
            fewer_requests = alloc['candidate']['requestsPerOperation'] < alloc['baseline']['requestsPerOperation']
            fewer_bytes = alloc['candidate']['requestedBytesPerOperation'] < alloc['baseline']['requestedBytesPerOperation']
            require(faster == row['fasterRounds'] and math.isclose(paired,row['medianPairedReductionPercent'],rel_tol=1e-10,abs_tol=1e-10)
                    and math.isclose(ratio,row['ratioOfMediansReductionPercent'],rel_tol=1e-10,abs_tol=1e-10),
                    'Historical accepted statistics do not match their recorded rounds')
            require(faster >= PLAN['minimumFasterRounds'] and paired >= PLAN['minimumMedianReductionPercent']
                    and ratio >= PLAN['minimumMedianReductionPercent'] and fewer_requests and fewer_bytes
                    and row['meetsAllPredeterminedCriteria'], 'Historical scenario no longer satisfies fixed thresholds')
            summaries.append({'campaign':campaign['campaign'],'scenario':row['scenario'],'scope':row['scope'],
                              'fasterRounds':faster,'medianPairedReductionPercent':paired,
                              'ratioOfMediansReductionPercent':ratio,'fewerRequests':fewer_requests,'fewerBytes':fewer_bytes})
    original_raw, accepted_raw = (run/'baseline-main.cpp').read_bytes(), (run/'candidate-main.cpp').read_bytes()
    original, accepted = original_raw.decode('utf-8-sig'), accepted_raw.decode('utf-8-sig')
    original_bound, _ = source_binding(original)
    require({k:sha(v) for k,v in original_bound.items()} == index['boundSourceHashes'], 'Historical baseline scope bindings differ')
    accepted_bound, _ = source_binding(accepted)
    sequence = json.loads((run/'expression-sequence.json').read_text(encoding='utf-8'))
    require(sequence['leafCount'] == 37, 'Historical scalar count differs')
    blocks = {name:(run/(name+'.candidate.cpp')).read_bytes().decode('utf-8') for name in ['imuObj','eventObj']}
    immutable = {str(p.relative_to(run)):sha(p.read_bytes()) for p in run.rglob('*') if p.is_file()}
    return {'run':run,'index':index,'result':result,'baselineText':original,'acceptedText':accepted,
            'acceptedBound':accepted_bound,'sequence':sequence,'blocks':blocks,
            'immutableHashes':immutable,'verifiedHistoricalScenarios':summaries}


def verify_history_unchanged(proof):
    for name, expected in proof['immutableHashes'].items():
        require(sha((proof['run']/name).read_bytes()) == expected, 'Historical evidence changed during final verification: '+name)


def explicit_leaf_sequence(raw_block, root):
    declarations, scalar_rows, moves = [], [], []
    for statement in clean(raw_block).split(';'):
        statement = statement.strip()
        if not statement:
            continue
        declaration = re.fullmatch(r'json\s+(\w+)\s*=\s*json::object\(\)', statement)
        if declaration:
            declarations.append(declaration.group(1)); continue
        move = re.fullmatch(r'(\w+)\["(\w+)"\]\s*=\s*std::move\((\w+)\)', statement)
        if move:
            moves.append(move.groups()); continue
        scalar = re.fullmatch(r'(\w+)\["(\w+)"\]\s*=\s*(\w+)', statement)
        require(scalar is not None, 'Unmeasured statement in current builder: '+statement)
        scalar_rows.append(scalar.groups())
    require(len(set(declarations)) == len(declarations) and declarations[0] == root, 'Current builder object declarations differ')
    paths = {root:()}
    for parent,key,child in moves:
        require(parent in paths and child in declarations and child not in paths, 'Current child ownership graph differs')
        paths[child] = paths[parent]+(key,)
    require(set(paths) == set(declarations) and len(moves) == len(declarations)-1, 'Current nested children are not each moved once')
    return [{'path':list(paths[target]+(key,)),'expression':expr} for target,key,expr in scalar_rows]


def audit_final_source(actual_raw, proof):
    text = actual_raw.decode('utf-8-sig')
    checks, blocks, spans = {}, {}, {}
    for name, expected in proof['blocks'].items():
        anchors = list(re.finditer(r'(?m)^        json '+name+r'\s*=',text))
        checks[name+'-single-production-anchor'] = len(anchors) == 1
        if len(anchors) == 1:
            start = anchors[0].start(); actual = text[start:start+len(expected)]
            blocks[name] = actual
            checks[name+'-raw-equals-measured-candidate'] = actual == expected
            spans[name] = {'byteOffset':len(text[:start].encode('utf-8'))+(3 if actual_raw.startswith(b'\xef\xbb\xbf') else 0),
                           'byteLength':len(actual.encode('utf-8')),'startLine':text[:start].count('\n')+1,
                           'sha256':sha(actual),'measuredCandidateSha256':sha(expected)}
        else:
            checks[name+'-raw-equals-measured-candidate'] = False
    try:
        current_bound, declarations = source_binding(text)
        checks['source-bindings-extracted'] = True
        for key, expected in proof['acceptedBound'].items():
            checks['strict-scope:'+key] = current_bound[key] == expected
        types = scalar_types(declarations,list(proof['sequence']['types']))
        checks['scalar-declared-types-equal-measured'] = types == proof['sequence']['types']
        checks['currentBindingGeneration-original-const-bool'] = types.get('currentBindingGeneration') == 'const bool'
    except (AssertionError,ValueError,AttributeError,KeyError) as error:
        current_bound, declarations, types = {}, [], {}
        checks['source-bindings-extracted'] = False
        extraction_error = str(error)
    sequence = {}
    for name, block_raw in blocks.items():
        try:
            sequence[name] = explicit_leaf_sequence(block_raw,name)
            checks[name+'-field-expression-evaluation-sequence'] = sequence[name] == proof['sequence']['blocks'][name]
        except RuntimeError:
            checks[name+'-field-expression-evaluation-sequence'] = False
    checks['exact-37-scalar-expressions'] = sum(len(s) for s in sequence.values()) == 37
    checks['standalone-LF-103-preserved'] = actual_raw.count(b'\n')-actual_raw.count(b'\r\n') == 103
    symbol = 'static void ipc_emit_owned_gyro_telemetry('
    definition_bound = any(symbol in value for value in proof['acceptedBound'].values())
    # No exemption: if this definition ever belongs to an R16 bound scope, its
    # complete body must match, and the exact diff is reported on rejection.
    old_emitter = source_function(proof['acceptedText'],symbol)
    new_emitter = source_function(text,symbol)
    emitter_diff = ''.join(difflib.unified_diff(old_emitter.splitlines(True),new_emitter.splitlines(True),
                                             fromfile='historical/owned_gyro_emitter',tofile='current/owned_gyro_emitter'))
    if definition_bound:
        checks['owned-gyro-emitter-strict-if-bound'] = old_emitter == new_emitter
    owned = {'symbol':'ipc_emit_owned_gyro_telemetry','bindingIncludesDefinition':definition_bound,
             'historicalSha256':sha(old_emitter),'actualSha256':sha(new_emitter),'sourceDifferenceObserved':old_emitter!=new_emitter,
             'scopeExemptionGranted':False,'exactDifference':emitter_diff,
             'reviewOwner':'Parent independent diagnostic gate actual-source proof' if not definition_bound else 'R16 strict binding; changed guard requires explicit separate verification',
             'note':'The emitter definition is outside all R16 bound scopes; its diagnostic gate is not executed by this fixture.' if not definition_bound else 'Bound emitter drift is rejected; no gate-guard change is ignored.'}
    return {'allChecksPassed':all(checks.values()),'checks':checks,'failedChecks':[k for k,v in checks.items() if not v],
            'blocks':blocks,'sourceSpans':spans,'bound':current_bound,'declarations':declarations,'types':types,
            'expressionSequence':sequence,'mainSha256':sha(actual_raw),'ownedGyroEmitter':owned,
            'extractionError':locals().get('extraction_error')}


def validate_final_inputs(proof):
    checks = {}
    for relative in ['deps/json/json.hpp','tools/virtual_sample_tree_perf_selftest.cpp']:
        checks[relative] = sha((REPO/relative).read_bytes()) == proof['index']['inputHashes'][relative]
    for name in ['samples.json','all-authorized-samples.json','fixture.cpp','json.hpp','expression-sequence.json','provenance.json']:
        checks['historical:'+name] = sha((proof['run']/name).read_bytes()) == proof['index']['artifactHashes'][name]
    require(all(checks.values()), 'Final vendor/fixture/input corpus drift: '+str(checks))
    return checks


def build_actual_profile(run, profile, options):
    exe = run/(profile+'.exe')
    command = ['cl','/nologo','/std:c++20','/utf-8','/EHsc','/fp:precise',*options,
               str(run/'fixture.cpp'),'/I'+str(run),'/Fo'+str(run/(profile+'.obj')),'/Fe'+str(exe)]
    bat = run/(profile+'-build.bat')
    write(run/'compiler-temp/.keep', '')
    write(bat,'@echo off\nset VSLANG=1033\nset "TMP='+str(run/'compiler-temp')+'"\nset "TEMP='+str(run/'compiler-temp')+'"\ncall "'+str(VCVARS)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(command)+'\n')
    compiled = subprocess.run(['cmd.exe','/d','/c',str(bat)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',timeout=150)
    write(run/(profile+'-build.log'),compiled.stdout+compiled.stderr)
    require(compiled.returncode == 0, 'Actual-source compilation failed: '+str(run/(profile+'-build.log')))
    print(profile+': compiled current production builder blocks',flush=True)
    result = invoke(exe,'verify',run,180)
    require(not result['formalTimingExecuted'] and result['candidateAccepted'] is None,
            'Final correctness unexpectedly executed benchmark')
    print(profile+': '+str(result['assertions'])+' correctness assertions; no benchmark',flush=True)
    return {'profile':profile,**result}


def final_actual_source(accepted_run):
    proof = load_accepted_proof(accepted_run)
    run = OUTPUT/('final-actual-source-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()))
    run.mkdir(parents=True)
    input_checks = validate_final_inputs(proof)
    actual_raw = (REPO/'native/main.cpp').read_bytes()
    audit = audit_final_source(actual_raw,proof)
    public_audit = {k:v for k,v in audit.items() if k not in ['blocks','bound','declarations']}
    save_json(run/'actual-binding-checks.json',public_audit)
    write(run/'actual-main.cpp',actual_raw)
    write(run/'owned-gyro-outside-r16.diff',audit['ownedGyroEmitter']['exactDifference'])
    if not audit['allChecksPassed']:
        rejection = {'stage':'actual-source-binding-rejected','actualSourceVerificationPass':False,
                     'historicalCandidateAccepted':True,'historicalRunDirectory':str(proof['run']),
                     'formalTimingExecuted':False,'benchmarkRerun':False,'productionWritten':False,'audit':public_audit}
        save_json(run/'final-result.json',rejection)
        raise RuntimeError('Final actual-source binding refused: '+', '.join(audit['failedChecks']))
    for name,raw_block in audit['blocks'].items():
        write(run/(name+'.actual.cpp'),raw_block)
    for name,value in audit['bound'].items():
        write(run/('actual-'+name+'.cpp.txt'),value)
    # baselineTrees uses the historical baseline. candidateTrees is generated
    # from current production bytes, never from the archived candidate snapshot.
    header = fixture_header(proof['baselineText'],audit['blocks'],audit['bound'],audit['declarations'],audit['types'])
    require(sha(header) == proof['index']['artifactHashes']['extracted.hpp'],
            'Actual-source extracted compiled fixture differs from measured code/context')
    write(run/'extracted.hpp',header)
    write(run/'fixture.cpp',(REPO/'tools/virtual_sample_tree_perf_selftest.cpp').read_bytes())
    write(run/'json.hpp',(REPO/'deps/json/json.hpp').read_bytes())
    write(run/'final-runner.py',Path(__file__).read_bytes())
    save_json(run/'acceptance-plan.json',PLAN)
    # Re-read only the originally authorized ZIP and frozen original review;
    # regenerate the complete and selected corpora and demand exact hashes.
    corpus = archive_inputs(run,proof['run']/'parent-readonly-node-review.json')
    require(corpus == proof['result']['provenance'], 'Final archive provenance differs from historical accepted corpus')
    for name in ['samples.json','all-authorized-samples.json']:
        require(sha((run/name).read_bytes()) == proof['index']['artifactHashes'][name], 'Regenerated input corpus differs: '+name)
    save_json(run/'provenance.json',corpus)
    input_hashes = {p:sha((REPO/p).read_bytes()) for p in ['deps/json/json.hpp','tools/virtual_sample_tree_perf_selftest.cpp','tools/virtual_sample_tree_perf_selftest.py']}
    compiled_input_names = ['extracted.hpp','fixture.cpp','json.hpp','final-runner.py','samples.json','all-authorized-samples.json','acceptance-plan.json','parent-readonly-node-review.json']
    compiled_input_hashes = {name:sha((run/name).read_bytes()) for name in compiled_input_names}
    profiles = []
    for label,opts in [('ActualRelease',['/O2','/DNDEBUG','/MT']),
                       ('ActualChecked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])]:
        profiles.append(build_actual_profile(run,label,opts))
    final_raw = (REPO/'native/main.cpp').read_bytes()
    after = audit_final_source(final_raw,proof)
    require(after['allChecksPassed'] and after['bound'] == audit['bound'], 'Actual bound source drift during final correctness; rebind required')
    for relative,expected in input_hashes.items():
        require(sha((REPO/relative).read_bytes()) == expected, 'Current final fixture/vendor/runner changed: '+relative)
    for name,expected in compiled_input_hashes.items():
        require(sha((run/name).read_bytes()) == expected, 'Compiled actual fixture/corpus snapshot drift: '+name)
    require(sha(Path(corpus['archive']).read_bytes()) == corpus['archiveSha256'], 'Authorized input archive drift during final verification')
    validate_final_inputs(proof);verify_history_unchanged(proof)
    if final_raw != actual_raw:
        write(run/'actual-main-at-end.cpp',final_raw)
    save_json(run/'actual-binding-checks-at-end.json',{k:v for k,v in after.items() if k not in ['blocks','bound','declarations']})
    artifact_hashes = {str(p.relative_to(run)):sha(p.read_bytes()) for p in run.rglob('*') if p.is_file()}
    index = {'stage':'final-actual-source','historicalRunDirectory':str(proof['run']),
             'historicalResultSha256':FINAL_ACCEPTED_RESULT_SHA,'historicalSourceIndexSha256':FINAL_ACCEPTED_INDEX_SHA,
             'historicalImmutableHashes':proof['immutableHashes'],'historicalInputHashes':proof['index']['inputHashes'],
             'historicalMeasuredCandidateMainSha256':FINAL_CANDIDATE_SHA,
             'actualMainStartSha256':sha(actual_raw),'actualMainEndSha256':sha(final_raw),
             'wholeMainStableDuringFinal':final_raw==actual_raw,'allBoundScopesStableDuringFinal':True,
             'boundSourceHashes':{k:sha(v) for k,v in audit['bound'].items()},'sourceSpans':audit['sourceSpans'],
             'currentInputHashes':input_hashes,'compiledInputHashes':compiled_input_hashes,'artifactHashes':artifact_hashes,
             'scalarTypes':audit['types'],'scalarExpressionSequence':audit['expressionSequence'],
             'compiledCurrentProductionBlocks':True,'compiledHeaderMatchesHistoricalMeasuredFixture':True,
             'ownedGyroEmitter':after['ownedGyroEmitter'],'scopeExceptions':[],
             'standaloneLF':final_raw.count(b'\n')-final_raw.count(b'\r\n'),
             'historicalEvidenceUnchanged':True,'formalTimingExecuted':False,'benchmarkRerun':False,
             'unappliedGitApplyCheckExecuted':False,'productionWritten':False}
    save_json(run/'final-source-hash-index.json',index)
    report = {'stage':'final-actual-source-verified','actualSourceVerificationPass':True,
              'historicalCandidateAccepted':True,'historicalRunDirectory':str(proof['run']),'runDirectory':str(run),
              'profiles':profiles,'audit':public_audit,'manifest':index,'inputChecks':input_checks,
              'historicalAcceptanceScenarios':proof['verifiedHistoricalScenarios'],'provenance':corpus,
              'finalSourceHashIndexSha256':sha((run/'final-source-hash-index.json').read_bytes()),
              'historicalEvidenceUnchanged':True,'formalTimingExecuted':False,'benchmarkRerun':False,
              'unappliedGitApplyCheckExecuted':False,'productionWritten':False,
              'installedProductOrPhysicalApisExecuted':False,
              'parentOwnsProductionBuildPackageAndIndependentGateProof':True}
    save_json(run/'final-result.json',report)
    text = '\n'.join(['# R16 final actual-source correctness verification','',
        'PASS. Current production imuObj/eventObj raw blocks exactly equal the accepted measured candidate. The compiler fixture embeds those blocks freshly read from production. Historical baseline code is the comparison.','',
        'Historical accepted measurement result SHA-256: `'+FINAL_ACCEPTED_RESULT_SHA+'`',
        'Historical accepted hash index SHA-256: `'+FINAL_ACCEPTED_INDEX_SHA+'`',
        'Actual production main SHA-256 at end: `'+sha(final_raw)+'`','',
        'ActualRelease and ActualChecked passed '+', '.join(str(p['assertions']) for p in profiles)+' assertions respectively. All 2301 authorized samples, 1600 random cases, type/number/float-bit/nonfinite/dump oracles, flags, zero/wide64 generations/sequences/timestamps/receipts, inactive override and input immutability were checked.','',
        'All 37 field expressions and their declared scalar types/evaluation order are rebound. Current record assignments, transfers, inactive override, writer/logger, callback and sample producer match the measured accepted scopes exactly. Vendored JSON and the C++ fixture match historical inputs. The new final runner has its own current hash; its measured original snapshot and original input hash remain intact. Selected and complete corpora were regenerated from the same authorized ZIP and hash-match the historical corpora.','',
        'No new benchmark was run. The original fixed-threshold acceptance was revalidated from all recorded rounds; the original timing/allocation results, runner snapshot, input hashes, source snapshots and hash index remain unchanged. No unapplied git apply check was executed.','',
        'Owned gyro emitter definition included in R16 binding: '+str(after['ownedGyroEmitter']['bindingIncludesDefinition'])+'. Source difference observed: '+str(after['ownedGyroEmitter']['sourceDifferenceObserved'])+'. '+after['ownedGyroEmitter']['note'],
        'Any observed difference is recorded in owned-gyro-outside-r16.diff for the parent independent diagnostic gate actual-source proof. No exemption was applied to any R16 bound scope.','',
        'Both source snapshots preserve 103 standalone LF lines. No production file was written and no installed product, sensor/device/service/registry/driver API executed. Parent retains final product build/package and its separate diagnostic gate proof.','',
        'Artifacts: final-result.json, final-source-hash-index.json, actual-binding-checks.json, current full main and block snapshots, freshly extracted.hpp, frozen final runner/fixture/JSON/corpora, and ActualRelease/ActualChecked compile logs and verify results.',''])
    write(run/'final-report.md',text)
    save_json(OUTPUT/'final-result.json',report)
    save_json(OUTPUT/'final-source-hash-index.json',index)
    write(OUTPUT/'final-report.md',text)
    print(json.dumps({'stage':report['stage'],'runDirectory':str(run),'actualMainSha256':sha(final_raw),
                      'historicalEvidenceUnchanged':True,'formalTimingExecuted':False},indent=2),flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    mode=parser.add_mutually_exclusive_group()
    mode.add_argument('--benchmark-ready-run',type=Path,help='Only after explicit parent benchmark-go; reruns bound drift checks before/after timing')
    mode.add_argument('--final-actual-source',action='store_true',help='Only after parent production ready: current production blocks/context, Release/Checked verify only; never benchmark or git apply check')
    parser.add_argument('--accepted-run',type=Path,default=FINAL_ACCEPTED_RUN,help='Frozen accepted measurement run used by final actual-source verification')
    args=parser.parse_args()
    if args.final_actual_source: final_actual_source(args.accepted_run)
    elif args.benchmark_ready_run: benchmark(args.benchmark_ready_run)
    else: prepare()


if __name__=='__main__': main()