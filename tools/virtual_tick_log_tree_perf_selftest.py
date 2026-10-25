#!/usr/bin/env python3
"""R12 isolated JSON ownership fixture; never compiles/runs YMCC or touches devices.

Default: bind current main, generate a reviewable emitter-only candidate, measure it.
After parent integration: --final --baseline-main <recorded baseline-main.cpp> binds
actual final emit, adopts ONLY the new final callbackProvenance tail, and rejects
any difference from the generated ownership candidate. Logger stays byte-identical.
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
DEFAULT_EVIDENCE = REPO.parents[2] / 'Build/Validation/VirtualInputPerf-R12-20261015/log-tree-local'
VCVARS = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')


def sha(data: bytes | str) -> str:
    return hashlib.sha256(data.encode('utf-8') if isinstance(data, str) else data).hexdigest()


def clean(text: str) -> str:
    # Preserve literals, newlines, and preprocessor directives. Braces in comments
    # or explanatory strings cannot alter extraction or initializer parsing.
    pattern = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*[\s\S]*?\*/'
    def sub(match):
        value = match.group()
        return ''.join('\n' if c == '\n' else ' ' for c in value) if value.startswith(('//', '/*')) else value
    return re.sub(pattern, sub, text)


def matching(text: str, start: int, left='{', right='}') -> int:
    depth = 0
    literal = None
    escape = False
    for i in range(start, len(text)):
        c = text[i]
        if literal:
            if escape:
                escape = False
            elif c == '\\':
                escape = True
            elif c == literal:
                literal = None
        elif c in ('"', "'"):
            literal = c
        elif c == left:
            depth += 1
        elif c == right:
            depth -= 1
            if depth == 0:
                return i
    raise ValueError('unclosed expression')


def function(source: str, name: str) -> str:
    masked = clean(source)
    found = list(re.finditer(r'static (?:void|json)\s+' + re.escape(name) + r'\([^;{]*\)\s*\{', masked))
    assert len(found) == 1, (name, len(found))
    match = found[0]
    end = matching(masked, masked.index('{', match.start())) + 1
    return source[match.start():end]


def norm(text: str) -> str:
    return re.sub(r'\s+', '', clean(text))


def parts(text: str) -> list[str]:
    result, begin, stack = [], 0, []
    literal, escape = None, False
    closes = {'}': '{', ')': '(', ']': '['}
    for i, c in enumerate(text):
        if literal:
            if escape:
                escape = False
            elif c == '\\':
                escape = True
            elif c == literal:
                literal = None
        elif c in ('"', "'"):
            literal = c
        elif c in '{([':
            stack.append(c)
        elif c in '})]':
            assert stack and stack.pop() == closes[c]
        elif c == ',' and not stack:
            result.append(text[begin:i].strip())
            begin = i + 1
    result.append(text[begin:].strip())
    return [x for x in result if x]


def object_pairs(expression: str) -> list[tuple[str, str]]:
    expression = clean(expression).strip()
    if expression.startswith('json'):
        expression = expression[4:]
    assert expression[0] == '{' and matching(expression, 0) == len(expression) - 1, expression
    pairs = []
    for entry in parts(expression[1:-1]):
        assert entry.startswith('{') and entry.endswith('}'), entry
        pair = parts(entry[1:-1])
        assert len(pair) == 2 and re.fullmatch(r'"[^"\\]*"', pair[0]), pair
        pairs.append((pair[0], pair[1]))
    assert len({x[0] for x in pairs}) == len(pairs), 'duplicate keys require separate review'
    return pairs


def assign_object(target: str, expression: str, movable: set[str], indent='        ', declare=True) -> str:
    rows = [indent + 'json ' + target + ' = json::object();'] if declare else []
    for key, value in object_pairs(expression):
        # Nested literal objects become independent named children, then transfer
        # through operator=; initializer_list's const copy path is never used.
        if re.match(r'^\{\s*\{', value):
            child = target + '_' + key[1:-1]
            rows.append(assign_object(child, value, movable, indent))
            value = 'std::move(' + child + ')'
        elif value in movable:
            value = 'std::move(' + value + ')'
        rows.append(indent + target + '[' + key + '] = ' + value + ';')
    return '\n'.join(rows)


def replace_conditional(source: str, target: str, child: str, movable: set[str]) -> str:
    pattern = re.compile(re.escape(target) + r'\s*=\s*n == 0\s*\?\s*(json\{\{)')
    matches = list(pattern.finditer(source))
    assert len(matches) == 1, target
    match = matches[0]
    a = source.index('{', match.start(1))
    ae = matching(source, a) + 1
    colon = source.index(':', ae)
    b = source.index('{', colon)
    be = matching(source, b) + 1
    semicolon = source.index(';', be) + 1
    zero, nonzero = source[a:ae], source[b:be]
    code = 'json ' + child + ' = json::object();\n        if (n == 0) {\n'
    code += assign_object(child, zero, movable, '            ', False)
    code += '\n        } else {\n' + assign_object(child, nonzero, movable, '            ', False)
    code += '\n        }\n        ' + target + ' = std::move(' + child + ');'
    return source[:match.start()] + code + source[semicolon:]


def root_bounds(source: str):
    call = source.index('appendDomainLog("virtual", "input-capture-tick-cost",')
    start = source.index('{', call)
    end = matching(source, start) + 1
    return call, start, end


def candidate_emitter(original: str) -> str:
    # Parse the actual expressions, retaining their native C++ integer types and
    # textual order. The few named branches are fail-closed source anchors.
    code = original
    code = replace_conditional(code, 'stages[kTickCostStageNames[i]]', 'stageEntry', set())
    code = replace_conditional(code, 'gpRead[kGpReadSourceNames[i]]', 'readEntry', {'canonical', 'secret'})
    code = replace_conditional(code, 'gpResolve[names[i]]', 'resolveEntry', {'fillKinds'})
    match = re.search(r'slotApi\s*=\s*json\{', code)
    assert match
    start = code.index('{', match.start())
    end = matching(code, start) + 1
    assignment = assign_object('slotApi', code[start:end], {'bySlot', 'slotMaxGap'}, '        ', False)
    code = code[:match.start()] + assignment.lstrip() + code[code.index(';', end) + 1:]
    call, start, end = root_bounds(code)
    assert code[end:].lstrip().startswith(');')
    root = clean(code[start:end])
    # #ifdef appears between entries: keep the exact conditional position.
    directive = '#ifdef YMCC_FAULT_INJECT'
    fi = re.search(r'#ifdef YMCC_FAULT_INJECT\s*(\{"fiPwrCapsCalls"[\s\S]*?\}),\s*#endif', root)
    if fi:
        root = root[:fi.start()] + fi.group(1) + ',' + root[fi.end():]
    pairs = object_pairs(root)
    moved = {'gpRead', 'slotApi', 'gpResolve', 'stages'}
    rows = ['    json tickDetail = json::object();']
    for key, value in pairs:
        if key == '"fiPwrCapsCalls"':
            rows.append(directive)
        rows.append(assign_object('tickDetail', '{' + '{' + key + ', ' + value + '}' + '}', moved, '    ', False))
        if key == '"fiPwrCapsCalls"':
            rows.append('#endif')
    rows.append('    appendDomainLog("virtual", "input-capture-tick-cost", tickDetail);')
    return code[:call] + '\n'.join(rows).lstrip() + code[end + 2:]


def with_final_callback(original: str, current: str) -> str:
    if 'callbackProvenance' in original:
        return original
    # Candidate source may already be explicit assignment. Adopt the final
    # function call only, appended at the tail of the original initializer.
    expected = 'tickCostCallbackWindowJson()'
    masked = clean(current)
    if 'callbackProvenance' not in masked:
        return original
    assert masked.count('"callbackProvenance"') == 1 and expected in masked
    _, start, end = root_bounds(clean(original))
    return original[:end-1] + ',\n        {"callbackProvenance", ' + expected + '}\n    ' + original[end-1:]


def context(source: str, original: str) -> str:
    masked = clean(source)
    rows = []
    for enum in ['TickCostStage', 'GamepadReadSource', 'GamepadSlotProbePurpose']:
        m = re.search(r'enum ' + enum + r'\s*\{', masked)
        assert m
        end = matching(masked, masked.index('{', m.start()))
        rows.append(masked[m.start():masked.index(';', end)+1])
    for name in ['kTickCostStageNames', 'kGpReadSourceNames', 'kGpProbePurposeNames']:
        m = re.search(r'static const char\* const ' + name + r'\[[^;]+;', masked)
        assert m
        rows.append(m.group())
    m = re.search(r'static constexpr ULONGLONG kXInputSlotDiscoveryIntervalMs\s*=\s*\d+;', masked)
    assert m
    rows.append(m.group().replace('ULONGLONG', 'unsigned long long'))
    refs = sorted(set(re.findall(r'\b(g_\w+)(?:\[[^\]\n]*\])*\.(?:exchange|load)\(', clean(original))))
    dimensions = {}
    types = {}
    for name in refs:
        m = re.search(r'static std::atomic<([^>]+)>\s+' + name + r'((?:\[[^\]]+\])*)(?:\{[^}]*\})?\s*;', masked)
        assert m, name
        types[name] = m.group(1)
        dimensions[name] = re.findall(r'\[([^\]]+)\]', m.group(2))
        rows.append('static Tracked<' + m.group(1) + '> ' + name + m.group(2) + ';')
    rows.append('static void bindCounters() {')
    for name, dims in dimensions.items():
        if name == 'g_fiPwrCapsCalls':
            rows.append('#ifdef YMCC_FAULT_INJECT')
        loops = ''.join('for (int i'+str(i)+'=0; i'+str(i)+'<'+dim+'; ++i'+str(i)+') ' for i, dim in enumerate(dims))
        rows.append('    ' + loops + 'registerCounter(' + name + ''.join('[i'+str(i)+']' for i in range(len(dims))) + ');')
        if name == 'g_fiPwrCapsCalls':
            rows.append('#endif')
    rows.append('}')
    return '\n'.join(rows) + '\n'


def bound_emitter(source: str, name: str) -> str:
    code = source.replace('static void emitTickCostWindow(', 'static void ' + name + '(')
    code = code.replace('ymcc::input::backendCost::windowJson<json>(g_gamepadBackendCost)', 'boundBackendWindowJson()')
    return code


def write(path: Path, data: str):
    path.write_text(data, encoding='utf-8', newline='\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence-dir', type=Path, default=DEFAULT_EVIDENCE)
    parser.add_argument('--baseline-main', type=Path)
    parser.add_argument('--final', action='store_true')
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    root = args.evidence_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    run = root / ('final-' if args.final else 'candidate-')
    run = run.with_name(run.name + dt.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
    run.mkdir()
    main_path = REPO / 'native/main.cpp'
    raw = main_path.read_bytes()
    current = raw.decode('utf-8-sig').replace('\r\n', '\n')
    baseline_raw = args.baseline_main.read_bytes() if args.baseline_main else raw
    original = function(baseline_raw.decode('utf-8-sig').replace('\r\n', '\n'), 'emitTickCostWindow')
    actual = function(current, 'emitTickCostWindow')
    original = with_final_callback(original, actual)
    candidate = candidate_emitter(original)
    def effects(value):
        return [norm(m.group()) for m in re.finditer(r'\b(g_\w+)(?:\[[^\]\n]*\])*\.(?:exchange|load)\([^)]*\)|ymcc::input::backendCost::windowJson<json>\(g_gamepadBackendCost\)|tickCostCallbackWindowJson\(\)', clean(value))]
    assert effects(original) == effects(candidate), 'construction rewrite changed textual atomic/drain ordering'
    if args.final:
        assert args.baseline_main, '--final requires the recorded --baseline-main'
        assert norm(candidate) == norm(actual), 'final emit differs from source-derived ownership candidate'
        candidate = actual
    logger = function(current, 'appendDomainLog')
    native_logger = function(current, 'appendNativeLifecycleLog')
    callback = function(current, 'tickCostCallbackWindowJson') if 'callbackProvenance' in original else ''
    # The actual shared logger body executes with inert portable bindings for
    # lock/path/clock/stream sinks; its const-ref copy and dump remain unchanged.
    write(run / 'logger.hpp', logger.replace('static void appendDomainLog(', 'static void fixedAppendDomainLog('))
    write(run / 'before.hpp', bound_emitter(original, 'beforeEmit'))
    write(run / 'candidate.hpp', bound_emitter(candidate, 'candidateEmit'))
    write(run / 'context.hpp', context(current, original + '\n' + callback))
    write(run / 'callback.hpp', callback.replace('static json tickCostCallbackWindowJson(', 'static json boundCurrentCallbackWindowJson(') if callback else 'static json boundCurrentCallbackWindowJson() { return json::object(); }\n')
    write(run / 'candidate-emitter.cpp', candidate + '\n')
    write(run / 'original-emitter.cpp', original + '\n')
    write(run / 'append-native-reviewed.cpp', native_logger + '\n')
    write(run / 'native-logger.hpp', native_logger + '\n')
    write(run / 'candidate.patch', ''.join(difflib.unified_diff(original.splitlines(True), candidate.splitlines(True), fromfile='native/main.cpp:emitTickCostWindow-before', tofile='native/main.cpp:emitTickCostWindow-candidate')))
    (run / 'baseline-main.cpp').write_bytes(baseline_raw)
    inputs = ['deps/json/json.hpp', 'native/gamepad_backend_cost.h', 'native/gamepad_backend_cost_json.h', 'native/log_frequency_policy.h', 'native/input_callback_cost.h', 'tools/virtual_tick_log_tree_perf_selftest.cpp', 'tools/virtual_tick_log_tree_perf_selftest.py']
    snapshots = {}
    for relative in inputs:
        data = (REPO / relative).read_bytes()
        snapshots[relative] = data
        (run / Path(relative).name).write_bytes(data)
    manifest = {'mainSha256': sha(raw), 'baselineMainSha256': sha(baseline_raw), 'inputHashes': {key: sha(value) for key, value in snapshots.items()}, 'emitOriginalSha256': sha(original), 'emitCandidateSha256': sha(candidate), 'finalActualEmitSha256': sha(actual), 'appendDomainLogSha256': sha(logger), 'appendNativeLifecycleLogSha256': sha(native_logger), 'boundAtomicReferences': sorted(set(re.findall(r'\b(g_\w+)(?:\[[^\]\n]*\])*\.(?:exchange|load)\(', clean(original)))), 'callbackProvenanceBound': '"callbackProvenance"' in original, 'actualCallbackWindowFunctionSha256': sha(callback), 'actualCallbackDrainBound': bool(callback), 'finalBinding': args.final, 'sideEffectExpressionsOrderedSha256': sha('\n'.join(effects(original))), 'textualAtomicAndDrainOrderPreserved': True, 'scope': 'Only emit construction/ownership changes; actual appendDomainLog signature/body copy+dump fixed. Native logger reviewed and retained. Real OS APIs replaced by inert fixture functions; no file/device/Steam/application execution.'}
    write(run / 'source-manifest.json', json.dumps(manifest, indent=2) + '\n')
    assert VCVARS.is_file(), VCVARS
    profiles = []
    for checked, fi in [(False, False), (False, True), (True, False), (True, True)]:
        label = ('Checked' if checked else 'Release') + ('-FI' if fi else '-Production')
        options = ['/Od', '/RTC1', '/D_DEBUG', '/D_ITERATOR_DEBUG_LEVEL=2', '/MTd'] if checked else ['/O2', '/DNDEBUG', '/MT']
        if fi:
            options.append('/DYMCC_FAULT_INJECT')
        exe = run / (label + '.exe')
        command = ['cl', '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/fp:precise', *options, str(run / 'virtual_tick_log_tree_perf_selftest.cpp'), '/I'+str(run), '/Fo'+str(run/(label+'.obj')), '/Fe'+str(exe)]
        bat = run / (label + '-build.bat')
        write(bat, '@echo off\nset VSLANG=1033\ncall "'+str(VCVARS)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n' + subprocess.list2cmdline(command) + '\n')
        built = subprocess.run(['cmd.exe', '/d', '/c', str(bat)], cwd=run, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
        write(run/(label+'-build.log'), built.stdout+built.stderr)
        assert built.returncode == 0, str(run/(label+'-build.log'))
        print(label+': compiled actual-source fixture', flush=True)
        mode = 'verify' if checked or args.verify_only or args.final else 'benchmark'
        ran = subprocess.run([str(exe), mode], cwd=run, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
        write(run/(label+'-result.json'), ran.stdout)
        write(run/(label+'-stderr.log'), ran.stderr)
        assert ran.returncode == 0, (label, ran.returncode, ran.stderr)
        result = json.loads(ran.stdout)
        assert result['pass']
        profiles.append({'profile': label, **result})
        print(label+': '+str(result['assertions'])+' assertions; '+str(len(result['benchmark']))+' scenarios', flush=True)
    manifest['mainStableDuringRun'] = sha(main_path.read_bytes()) == sha(raw)
    manifest['emitterAndLoggersStableDuringRun'] = all(function(main_path.read_text(encoding='utf-8-sig').replace('\r\n','\n'), name) == value for name, value in [('emitTickCostWindow', actual), ('appendDomainLog', logger), ('appendNativeLifecycleLog', native_logger), *([('tickCostCallbackWindowJson', callback)] if callback else [])])
    manifest['fixtureAndVendorsStableDuringRun'] = all((REPO / key).read_bytes() == value for key, value in snapshots.items())
    assert manifest['fixtureAndVendorsStableDuringRun'] and manifest['emitterAndLoggersStableDuringRun'], 'source changed within measured run; rebind'
    report = {'pass': True, 'runDirectory': str(run), 'manifest': manifest, 'profiles': profiles, 'productionModifiedByRunner': False, 'installedApplicationsOrHardwareExecuted': False}
    write(run/'source-manifest.json', json.dumps(manifest, indent=2)+'\n')
    write(run/'result.json', json.dumps(report, indent=2)+'\n')
    write(root/('final-result.json' if args.final else 'candidate-result.json'), json.dumps(report, indent=2)+'\n')
    print(str(run), flush=True)


if __name__ == '__main__':
    main()
