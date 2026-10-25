#!/usr/bin/env python3
"""R14 current-source UI-cost JSON tree fixture. No YMCC/Windows/device execution.

Generate an unapplied, five-function ownership patch, verify exact JSON and drain
traces, then benchmark two warmed rotating campaigns with actual atomic counters.
With --final --baseline-main <historical run/baseline-main.cpp>, authenticate the
measured candidate from that run, require exact current production function text,
and compile the actual current functions. Final mode always verifies without
remeasuring timing; --verify-only is supported in both modes. Candidate evidence
is never overwritten by final mode. All generated files stay in the explicit
evidence directory. Full build/package and integration belong to the parent.
Changes elsewhere in main (R13) are allowed.
"""
from __future__ import annotations
import argparse
import datetime as dt
import difflib
import hashlib
import json
from pathlib import Path
import re
import subprocess

REPO = Path(__file__).resolve().parents[1]
DEFAULT_EVIDENCE = REPO.parents[2] / 'Build/Validation/VirtualInputPerf-R14-20261015/ui-tree-local'
VCVARS = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
FUNCTIONS = ['uiCostStageJson', 'uiCostOverflowJson', 'uiCostCollect', 'uiCostTimersJson', 'uiCostTopJson', 'emitUiCostWindow']
CHANGED = [name for name in FUNCTIONS if name != 'uiCostCollect']


def sha(data: str | bytes) -> str:
    return hashlib.sha256(data.encode('utf-8') if isinstance(data, str) else data).hexdigest()


def write(path: Path, text: str):
    path.write_bytes(text.encode('utf-8'))


def clean(text: str) -> str:
    pattern = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/'
    def sub(match):
        value = match.group()
        return ''.join('\n' if c == '\n' else ' ' for c in value) if value.startswith(('//', '/*')) else value
    return re.sub(pattern, sub, text)


def matching(text: str, start: int, left='{', right='}') -> int:
    depth, literal, escape = 0, None, False
    for i in range(start, len(text)):
        c = text[i]
        if literal:
            if escape: escape = False
            elif c == '\\': escape = True
            elif c == literal: literal = None
        elif c in ('"', "'"): literal = c
        elif c == left: depth += 1
        elif c == right:
            depth -= 1
            if depth == 0: return i
    raise ValueError('unclosed expression')


def bounds(source: str, name: str) -> tuple[int, int]:
    masked = clean(source)
    found = list(re.finditer(r'static (?:void|json)\s+' + re.escape(name) + r'\([^;{]*\)\s*\{', masked))
    assert len(found) == 1, (name, len(found))
    start = found[0].start()
    return start, matching(masked, masked.index('{', start)) + 1


def function(source: str, name: str) -> str:
    start, end = bounds(source, name)
    return source[start:end]


def parts(text: str) -> list[str]:
    result, begin, stack, literal, escape = [], 0, [], None, False
    closes = {'}': '{', ')': '(', ']': '['}
    for i, c in enumerate(text):
        if literal:
            if escape: escape = False
            elif c == '\\': escape = True
            elif c == literal: literal = None
        elif c in ('"', "'"): literal = c
        elif c in '{([': stack.append(c)
        elif c in '})]':
            assert stack and stack.pop() == closes[c]
        elif c == ',' and not stack:
            result.append(text[begin:i].strip()); begin = i + 1
    result.append(text[begin:].strip())
    return [value for value in result if value]


def assignments(target: str, expression: str, moved=()) -> str:
    rows = []
    for entry in parts(clean(expression)[1:-1]):
        assert entry.startswith('{') and entry.endswith('}'), entry
        key, value = parts(entry[1:-1])
        assert re.fullmatch(r'"[^"\n]+"', key), key
        if value in moved: value = 'std::move(' + value + ')'
        rows.append(target + '[' + key + '] = ' + value + ';')
    return '\n'.join(rows)


def replace_objects(code: str) -> str:
    # Direct json variables only; return branches are handled explicitly below.
    pattern = r'(?:const )?json\s+(\w+)\s*=\s*(?:json)?\{'
    while (match := re.search(pattern, clean(code))):
        start = code.index('{', match.start()); end = matching(clean(code), start) + 1
        assert code[end] == ';'
        target = match.group(1)
        indent = code[:match.start()].rsplit('\n', 1)[-1]
        assert not indent.strip()
        replacement = 'json ' + target + ' = json::object();\n' + indent + assignments(target, code[start:end]).replace('\n', '\n' + indent)
        code = code[:match.start()] + replacement + code[end+1:]
    return code


def candidate(name: str, original: str) -> str:
    code = original
    if name in ('uiCostStageJson', 'uiCostOverflowJson'):
        zero = 'if (n == 0) return json{{"n", 0}};'
        assert code.count(zero) == 1
        code = code.replace(zero, 'json result = json::object();\n    if (n == 0) {\n        result["n"] = 0;\n        return result;\n    }')
        match = re.search(r'return json\{', clean(code)); assert match
        opening = code.index('{', match.start()); end = matching(clean(code), opening) + 1
        assert code[end] == ';'
        replacement = assignments('result', code[opening:end]).replace('\n', '\n    ') + '\n    return result;'
        code = code[:match.start()] + replacement + code[end+1:]
    if name == 'uiCostTimersJson':
        pattern = 'out[std::to_string(r.key)] = json{'
        assert code.count(pattern) == 1
        start = code.index(pattern); opening = code.index('{', start); end = matching(clean(code), opening) + 1
        values = assignments('row', code[opening:end]).replace('\n', '\n        ')
        replacement = 'json row = json::object();\n        ' + values + '\n        out[std::to_string(r.key)] = std::move(row)'
        code = code[:start] + replacement + code[end:]
    code = replace_objects(code)
    if name == 'uiCostTopJson':
        assert code.count('out.push_back(row);') == 1
        code = code.replace('out.push_back(row);', 'out.push_back(std::move(row));')
    if name == 'emitUiCostWindow':
        moved = {'timerIds', 'timerOverflow', 'serialTop', 'serialOverflow', 'ipcTop', 'ipcOverflow'}
        for child in moved:
            assert code.count('const json ' + child + ' =') == 1
            code = code.replace('const json ' + child + ' =', 'json ' + child + ' =')
            match = re.search(r'(\w+\["[^"]+"\] = )' + child + ';', code)
            assert match, child
            code = code[:match.start()] + match.group(1) + 'std::move(' + child + ');' + code[match.end():]
        call = 'appendDomainLog("virtual", "input-capture-ui-cost", '
        start = code.index(call); opening = code.index('{', start); end = matching(clean(code), opening) + 1
        children = {'wndProc', 'wmInput', 'wmTimer', 'serialEvent', 'ipcEmit', 'c15a', 'c17a'}
        replacement = 'json detail = json::object();\n    ' + assignments('detail', code[opening:end], children).replace('\n', '\n    ')
        replacement += '\n    ' + call + 'detail'
        code = code[:start] + replacement + code[end:]
    return code


def effects(code: str) -> list[str]:
    return [re.sub(r'\s+', '', m.group()) for m in re.finditer(r'\b[\w.]+(?:\[[^\]\n]*\])*\.(?:exchange|load)\([^)]*\)|uiCost(?:StageJson|OverflowJson|Collect|TimersJson|TopJson)\([^;\n]*?\)', clean(code))]


def bind(code: str, prefix: str) -> str:
    for name in FUNCTIONS: code = re.sub(r'\b' + name + r'\b', prefix + name, code)
    return code


def context(source: str) -> str:
    masked = clean(source)
    start = masked.index('enum UiCostStage {')
    end = masked.index('static inline unsigned long long uiCostNameHash', start)
    code = source[start:end]
    assert 'constexpr int kUiCostTimerSlots = 12;' in code
    assert 'constexpr int kUiCostEventSlots = 24;' in code
    assert 'constexpr int kUiCostSerialSlots = 12;' in code
    code = code.replace('std::atomic<unsigned long long>', 'Counter')
    row = masked.index('struct UiCostRow {'); row_end = masked.index(';', matching(masked, masked.index('{', row))) + 1
    code += source[row:row_end] + '\n'
    declarations = re.findall(r'static Counter\s+(g_\w+)((?:\[[^\]]+\])*)(?:\{[^}]*\})?\s*;', clean(code))
    assert len(declarations) == 22, declarations
    code += 'static void bindCounters() {\n'
    for name, dims in declarations:
        if dims:
            dimension = re.fullmatch(r'\[([^\]]+)\]', dims).group(1)
            code += f'    for (int i = 0; i < {dimension}; ++i) registerCounter({name}[i], "{name}[" + std::to_string(i) + "]", ' + ('true' if name.endswith('Keys') else 'false') + ');\n'
        else: code += f'    registerCounter({name}, "{name}", false);\n'
    for table in ['Timer', 'Serial', 'Event']:
        for member in ['count', 'sumUs', 'maxUs']:
            code += f'    registerCounter(g_ui{table}Overflow.{member}, "g_ui{table}Overflow.{member}", false);\n'
    return code + '}\n'


def compile_run(run: Path, profile: str, options: list[str], mode: str):
    command = ['cl', '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/fp:precise', *options,
               str(run/'virtual_ui_cost_tree_perf_selftest.cpp'), '/I'+str(run), '/Fo'+str(run/(profile+'.obj')), '/Fe'+str(run/(profile+'.exe'))]
    bat = run/(profile+'-build.bat')
    write(bat, '@echo off\nset VSLANG=1033\ncall "'+str(VCVARS)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(command)+'\n')
    built = subprocess.run(['cmd.exe', '/d', '/c', str(bat)], cwd=run, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=90)
    write(run/(profile+'-build.log'), built.stdout+built.stderr)
    assert built.returncode == 0, str(run/(profile+'-build.log'))
    results = []
    for campaign in range(2 if mode == 'benchmark' else 1):
        ran = subprocess.run([str(run/(profile+'.exe')), mode, str(campaign)], cwd=run, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=60)
        write(run/f'{profile}-{campaign}-result.json', ran.stdout)
        write(run/f'{profile}-{campaign}-stderr.log', ran.stderr)
        assert ran.returncode == 0, (profile, ran.returncode, ran.stderr)
        result = json.loads(ran.stdout); assert result['pass']
        witness = result.pop('witness')
        write(run/f'{profile}-{campaign}-parity-witness.json', json.dumps(witness, ensure_ascii=False, indent=2)+'\n')
        results.append(result)
        print(f'{profile} campaign {campaign}: {result["assertions"]} assertions; {len(result["benchmark"])} measurements', flush=True)
    return {'profile': profile, 'options': options, 'campaigns': results}


INPUTS = ['deps/json/json.hpp', 'native/log_frequency_policy.h',
          'tools/virtual_ui_cost_tree_perf_selftest.cpp', 'tools/virtual_ui_cost_tree_perf_selftest.py']


def normalized_source(raw: bytes) -> str:
    return raw.decode('utf-8-sig').replace('\r\n', '\n')


def ui_functions(functions: dict[str, str]) -> str:
    return '\n\n'.join(functions[name] for name in FUNCTIONS)+'\n'


def measured_candidate(baseline_path: Path, baseline_raw: bytes, original: dict[str, str], expected: dict[str, str]) -> dict:
    """Authenticate the historical measured result, not today's candidate pointer."""
    historical = baseline_path.parent
    index_path = historical/'SHA256SUMS.json'
    index_raw = index_path.read_bytes()
    index = json.loads(index_raw)
    needed = ['baseline-main.cpp', 'source-manifest.json', 'result.json',
              'original-ui-functions.cpp', 'candidate-ui-functions.cpp',
              'context.hpp', 'logger.hpp', *[Path(path).name for path in INPUTS]]
    artifacts = {'SHA256SUMS.json': sha(index_raw)}
    data = {}
    for name in needed:
        data[name] = (historical/name).read_bytes()
        assert name in index and sha(data[name]) == index[name], ('historical artifact hash mismatch', name)
        artifacts[name] = sha(data[name])
    manifest = json.loads(data['source-manifest.json'])
    result = json.loads(data['result.json'])
    assert result['pass'] and result['candidateAcceptedForParentReview'] and result['decision'] == 'ACCEPT_LOCAL_CANDIDATE', 'historical candidate was not accepted'
    assert result['manifest'] == manifest, 'historical result/manifest mismatch'
    measurements = [entry for profile in result['profiles'] for campaign in profile['campaigns'] for entry in campaign['benchmark']]
    assert len(measurements) == 24 and all(entry['consistentBenefit'] for entry in measurements), 'historical two-campaign benefit proof missing'
    assert baseline_raw == data['baseline-main.cpp'], 'supplied baseline differs from measured baseline'
    assert sha(baseline_raw) == manifest['sourceMainSha256'], 'baseline main hash mismatch'
    assert {name: sha(code) for name,code in original.items()} == manifest['functionSha256'], 'baseline UI functions differ from measured functions'
    assert {name: sha(expected[name]) for name in CHANGED} == manifest['candidateFunctionSha256'], 'regenerated expected candidate differs from measured candidate'
    assert ui_functions(original).encode('utf-8') == data['original-ui-functions.cpp'], 'historical original function text mismatch'
    assert ui_functions(expected).encode('utf-8') == data['candidate-ui-functions.cpp'], 'expected function text differs from measured artifact'
    for path in INPUTS:
        assert sha(data[Path(path).name]) == manifest['inputsSha256'][path], ('historical input hash mismatch', path)
    baseline = normalized_source(baseline_raw)
    assert sha(context(baseline)) == manifest['contextSha256'] and context(baseline).encode('utf-8') == data['context.hpp'], 'historical context mismatch'
    logger = function(baseline, 'appendDomainLog')
    assert sha(logger) == manifest['loggerBodySha256'], 'historical logger mismatch'
    assert (logger.replace('static void appendDomainLog(', 'static void fixedAppendDomainLog(')+'\n').encode('utf-8') == data['logger.hpp'], 'historical bound logger mismatch'
    return {'directory': str(historical), 'manifest': manifest, 'artifactHashes': artifacts,
            'measuredCandidateAccepted': True, 'regeneratedCandidateExactlyMatchesMeasuredArtifact': True,
            'historicalTimingReused': True}


def final_binding(raw: bytes, baseline_raw: bytes, original: dict[str, str], expected: dict[str, str],
                  historical: dict, snapshots: dict[str, bytes]) -> tuple[dict[str, str], str, str, dict]:
    """Fail closed on raw function bytes, collection/context/logger and inputs.

    Runner updates are intentional; its current snapshot is recorded separately.
    The C++ fixture and vendor/header inputs must remain those measured earlier.
    """
    source = raw.decode('utf-8-sig')
    normalized = normalized_source(raw)
    baseline = normalized_source(baseline_raw)
    actual = {name: function(normalized, name) for name in FUNCTIONS}
    eol = '\r\n' if b'\r\n' in baseline_raw else '\n'
    raw_hashes = {}
    for name in CHANGED:
        expected_raw = expected[name].replace('\n', eol)
        actual_raw = function(source, name)
        assert actual_raw == expected_raw, ('actual production function bytes differ from measured candidate', name)
        assert actual[name] == expected[name], ('actual normalized function text mismatch', name)
        raw_hashes[name] = {'expectedSha256': sha(expected_raw), 'actualSha256': sha(actual_raw), 'exact': True}
    assert function(source, 'uiCostCollect') == function(baseline_raw.decode('utf-8-sig'), 'uiCostCollect'), 'actual uiCostCollect changed'
    assert actual['uiCostCollect'] == original['uiCostCollect'], 'actual collection text changed'
    logger = function(normalized, 'appendDomainLog')
    globals_code = context(normalized)
    assert function(source, 'appendDomainLog') == function(baseline_raw.decode('utf-8-sig'), 'appendDomainLog'), 'actual raw logger changed'
    assert logger == function(baseline, 'appendDomainLog'), 'actual logger changed'
    assert globals_code == context(baseline), 'actual UI context/row/declarations changed'
    assert sha(logger) == historical['manifest']['loggerBodySha256'], 'actual logger differs from measured logger'
    assert sha(globals_code) == historical['manifest']['contextSha256'], 'actual context differs from measured context'
    unchanged = [path for path in INPUTS if path != 'tools/virtual_ui_cost_tree_perf_selftest.py']
    for path in unchanged:
        assert sha(snapshots[path]) == historical['manifest']['inputsSha256'][path], ('current fixture/vendor/header differs from measured input', path)
    return actual, logger, globals_code, {'actualFiveFunctionsExactlyMatchMeasuredCandidate': True,
            'rawFunctionBytes': raw_hashes, 'actualCollectUnchanged': True,
            'actualContextLoggerAndFixtureVendorHeadersUnchanged': True,
            'currentRunnerSha256': sha(snapshots['tools/virtual_ui_cost_tree_perf_selftest.py']),
            'historicalRunnerSha256': historical['manifest']['inputsSha256']['tools/virtual_ui_cost_tree_perf_selftest.py'],
            'compiledCandidateOrigin': 'actual current native/main.cpp functions; only names prefixed for fixture isolation'}


def argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence-dir', type=Path, default=DEFAULT_EVIDENCE)
    parser.add_argument('--verify-only', action='store_true')
    parser.add_argument('--final', action='store_true', help='verify actual production functions against the historical measured candidate; never remeasure timing')
    parser.add_argument('--baseline-main', type=Path, help='historical baseline-main.cpp with adjacent measured result, source manifest and SHA256SUMS.json')
    return parser


def verification_mode(args) -> str:
    return 'verify' if args.final or args.verify_only else 'benchmark'


def execute(args, root: Path, run: Path) -> dict:
    raw = (REPO/'native/main.cpp').read_bytes()
    source = raw.decode('utf-8-sig')
    normalized = normalized_source(raw)
    baseline_path = args.baseline_main.resolve() if args.final else REPO/'native/main.cpp'
    baseline_raw = baseline_path.read_bytes() if args.final else raw
    baseline = normalized_source(baseline_raw)
    original = {name: function(baseline, name) for name in FUNCTIONS}
    changed = {name: candidate(name, original[name]) for name in CHANGED}
    expected = {name: changed.get(name, original[name]) for name in FUNCTIONS}
    for name, code in changed.items(): assert effects(code) == effects(original[name]), ('side effect order', name)
    comparator = '[](const UiCostRow& a, const UiCostRow& b) { return a.sum > b.sum; }'
    assert comparator in original['uiCostTopJson'] and comparator in changed['uiCostTopJson']
    snapshots = {path: (REPO/path).read_bytes() for path in INPUTS}
    (run/'baseline-main.cpp').write_bytes(baseline_raw)
    (run/('actual-main.cpp' if args.final else 'source-main.cpp')).write_bytes(raw)
    candidate_pointer = root/'candidate-result.json'
    candidate_pointer_before = candidate_pointer.read_bytes() if args.final and candidate_pointer.is_file() else None
    historical = None
    binding = {}
    if args.final:
        historical = measured_candidate(baseline_path, baseline_raw, original, expected)
        actual, logger, globals_code, binding = final_binding(raw, baseline_raw, original, expected, historical, snapshots)
        # Crucial: compile actual extracted current function bodies, not expected.
        compiled = actual
        write(run/'actual-ui-functions.cpp', ui_functions(actual))
        write(run/'expected-candidate-ui-functions.cpp', ui_functions(expected))
    else:
        actual = {name: function(normalized, name) for name in FUNCTIONS}
        logger = function(normalized, 'appendDomainLog')
        globals_code = context(normalized)
        compiled = expected
        # Candidate mode retains its unapplied raw patch and read-only check.
        eol = '\r\n' if b'\r\n' in raw else '\n'
        transformed = source
        for name in reversed(CHANGED):
            start, end = bounds(transformed, name)
            transformed = transformed[:start] + changed[name].replace('\n', eol) + transformed[end:]
        (run/'candidate-main.cpp').write_bytes((b'\xef\xbb\xbf' if raw.startswith(b'\xef\xbb\xbf') else b'') + transformed.encode('utf-8'))
        patch = ''.join(difflib.unified_diff(source.splitlines(keepends=True), transformed.splitlines(keepends=True), fromfile='a/native/main.cpp', tofile='b/native/main.cpp', n=3))
        write(run/'candidate.raw.patch', patch)
        checked = subprocess.run(['git', 'apply', '--check', str(run/'candidate.raw.patch')], cwd=REPO, capture_output=True, text=True, timeout=15)
        write(run/'patch-check.log', checked.stdout+checked.stderr); assert checked.returncode == 0, checked.stderr
    write(run/'original-ui-functions.cpp', ui_functions(original))
    write(run/'candidate-ui-functions.cpp', ui_functions(compiled))
    write(run/'before.hpp', bind(ui_functions(original), 'before_'))
    write(run/'candidate.hpp', bind(ui_functions(compiled), 'candidate_'))
    write(run/'context.hpp', globals_code)
    write(run/'logger.hpp', logger.replace('static void appendDomainLog(', 'static void fixedAppendDomainLog(')+'\n')
    for path,data in snapshots.items(): (run/Path(path).name).write_bytes(data)
    manifest = {'mode': 'final' if args.final else 'candidate', 'finalBinding': args.final,
                'sourceMainSha256': sha(raw), 'baselineMainSha256': sha(baseline_raw), 'baselineMainPath': str(baseline_path),
                'functionSha256': {k: sha(v) for k,v in original.items()}, 'candidateFunctionSha256': {k: sha(v) for k,v in changed.items()},
                'actualFunctionSha256': {k: sha(v) for k,v in actual.items()}, 'compiledFunctionSha256': {k: sha(v) for k,v in compiled.items()},
                'loggerBodySha256': sha(logger), 'contextSha256': sha(globals_code), 'inputsSha256': {k: sha(v) for k,v in snapshots.items()},
                'patchApplyCheckPassed': None if args.final else True, 'unappliedGitApplyCheckExecuted': not args.final,
                'tableCollectAndPartialSortUnchanged': True, 'textualDrainOrderPreserved': True, 'binding': binding,
                'historicalMeasuredEvidence': historical,
                'scope': 'Only five UI cost JSON builders. No main build/package, cycle test, frame sampling/control changes, files/current timer/HWND/WM/device APIs. Logger source body executes with inert bindings, unchanged copy+dump.'}
    if not args.final: manifest['unappliedPatchSha256'] = sha(patch)
    write(run/'source-manifest.json', json.dumps(manifest, indent=2)+'\n')
    write(run/'status.json', json.dumps({'status': 'source-bound-ready-to-verify', 'mode': manifest['mode'], 'mainSha256': sha(raw), 'baselineMainSha256': sha(baseline_raw), 'timingRequested': verification_mode(args) == 'benchmark'}, indent=2)+'\n')
    assert VCVARS.is_file(), VCVARS
    profiles = [compile_run(run, 'Release-trace', ['/O2', '/DNDEBUG', '/MT', '/DUI_TREE_TRACE'], 'verify'),
                compile_run(run, 'Checked-trace', ['/Od', '/RTC1', '/D_DEBUG', '/D_ITERATOR_DEBUG_LEVEL=2', '/MTd', '/DUI_TREE_TRACE'], 'verify'),
                compile_run(run, 'Release-atomic', ['/O2', '/DNDEBUG', '/MT'], verification_mode(args))]
    for profile in profiles:
        for result in profile['campaigns']:
            assert result['parity']['cases'] == 282 and result['parity']['expectedReadsPerEmit'] == 223, 'incomplete parity suite'
            assert result['traceBuild'] == (profile['profile'] != 'Release-atomic'), 'wrong atomic fixture binding'
            if result['traceBuild']: assert result['parity']['traceEventsCompared'] == 128448, 'incomplete atomics oracle'
            if args.final: assert not result['benchmark'], 'final mode must not remeasure timing'
    current = (REPO/'native/main.cpp').read_bytes()
    current_text = normalized_source(current)
    manifest['wholeMainStableDuringRun'] = current == raw
    manifest['relevantFunctionsAndLoggerStableDuringRun'] = all(function(current_text, name) == actual[name] for name in FUNCTIONS) and function(current_text, 'appendDomainLog') == logger and context(current_text) == globals_code
    manifest['toolsAndVendorStableDuringRun'] = all((REPO/k).read_bytes() == v for k,v in snapshots.items())
    assert manifest['relevantFunctionsAndLoggerStableDuringRun'] and manifest['toolsAndVendorStableDuringRun'], 'relevant source changed; rebind'
    if args.final:
        # Repeat raw byte equality and provenance checks after all fixture runs.
        final_binding(current, baseline_raw, original, expected, historical, snapshots)
        manifest['historicalEvidenceStableDuringRun'] = all(sha((Path(historical['directory'])/name).read_bytes()) == digest for name,digest in historical['artifactHashes'].items())
        assert manifest['historicalEvidenceStableDuringRun'], 'historical evidence changed during final verification'
        pointer_after = candidate_pointer.read_bytes() if candidate_pointer.is_file() else None
        manifest['candidateResultPointerPreserved'] = pointer_after == candidate_pointer_before
        assert manifest['candidateResultPointerPreserved'], 'candidate-result changed during final verification'
    benchmarks = [entry for campaign in profiles[-1]['campaigns'] for entry in campaign['benchmark']]
    accepted = bool(benchmarks) and all(entry['consistentBenefit'] for entry in benchmarks)
    decision = 'FINAL_VERIFIED_MEASURED_CANDIDATE' if args.final else ('ACCEPT_LOCAL_CANDIDATE' if accepted else ('VERIFY_ONLY' if args.verify_only else 'REJECT_NO_CONSISTENT_BENEFIT'))
    report = {'pass': True, 'mode': manifest['mode'], 'candidateAcceptedForParentReview': True if args.final else accepted,
              'finalVerificationPassed': args.final, 'timingRerun': bool(benchmarks), 'decision': decision,
              'runDirectory': str(run), 'manifest': manifest, 'profiles': profiles, 'productionEditedByRunner': False, 'mainBuildOrPackageRun': False}
    write(run/'source-manifest.json', json.dumps(manifest, indent=2)+'\n')
    write(run/'result.json', json.dumps(report, indent=2)+'\n')
    write(root/('final-result.json' if args.final else 'candidate-result.json'), json.dumps(report, indent=2)+'\n')
    write(run/'status.json', json.dumps({'status': 'complete', 'decision': decision, 'mainSha256': sha(raw), 'timingRerun': bool(benchmarks)}, indent=2)+'\n')
    artifacts = {p.name: sha(p.read_bytes()) for p in sorted(run.iterdir()) if p.is_file()}
    write(run/'SHA256SUMS.json', json.dumps(artifacts, indent=2)+'\n')
    print(report['decision']+' '+str(run), flush=True)
    return report


def main(argv=None):
    parser = argument_parser()
    args = parser.parse_args(argv)
    if args.final and not args.baseline_main: parser.error('--final requires --baseline-main from the historical measured run')
    if args.baseline_main and not args.final: parser.error('--baseline-main is only valid with --final')
    root = args.evidence_dir.resolve(); root.mkdir(parents=True, exist_ok=True)
    run = root/(('final-' if args.final else 'candidate-')+dt.datetime.now().strftime('%Y%m%d-%H%M%S-%f')); run.mkdir()
    start_status = {'status': 'started-binding-source', 'mode': 'final' if args.final else 'candidate',
                    'runDirectory': str(run), 'baselineMainPath': str(args.baseline_main) if args.final else None,
                    'timingRequested': verification_mode(args) == 'benchmark'}
    write(run/'status.json', json.dumps(start_status, indent=2)+'\n')
    try:
        execute(args, root, run)
    except Exception as error:
        write(run/'status.json', json.dumps({**start_status, 'status': 'failed', 'error': str(error)}, indent=2)+'\n')
        raise


if __name__ == '__main__': main()
