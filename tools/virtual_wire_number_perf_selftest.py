"""Actual-source C6a number formatter/cache and complete SUBMIT_FRAME fixture.

Writes only this tool's evidence. No Host/native entry, SDK, crypto, device,
registry or installation access. --final binds the patched class to the saved
before class; this runner never writes production. MSVC and vendored json.hpp
are snapshotted by hash. Only context identity getters are inert value stubs.
"""
from pathlib import Path
import argparse, datetime, difflib, hashlib, json, re, subprocess

REPO = Path(__file__).resolve().parent.parent
DEFAULT = Path('G:/YeManCC-Work/Build/Validation/VirtualInputPerf-R4-20261015/wire-number')
SHA = lambda data: hashlib.sha256(data).hexdigest()


def close_brace(text, start):
    depth = 0; mode = 'code'; i = start
    while i < len(text):
        c = text[i]; two = text[i:i+2]
        if mode == 'line':
            if c == '\n': mode = 'code'
        elif mode == 'block':
            if two == '*/': mode = 'code'; i += 1
        elif mode in ('string', 'char'):
            if c == '\\': i += 1
            elif c == ('"' if mode == 'string' else "'"): mode = 'code'
        elif two == '//': mode = 'line'; i += 1
        elif two == '/*': mode = 'block'; i += 1
        elif c == '"': mode = 'string'
        elif c == "'": mode = 'char'
        elif c == '{': depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0: return i
        i += 1
    raise ValueError('Unclosed source block')


def extract(text, marker, is_class=False):
    hits = list(re.finditer(re.escape(marker), text))
    bodies = []
    for hit in hits:
        start = hit.start(); opening = text.find('{', start)
        semicolon = text.find(';', start)
        if opening >= 0 and (semicolon < 0 or opening < semicolon):
            end = close_brace(text, opening) + 1
            if is_class:
                assert text[end] == ';'; end += 1
            bodies.append((start, end, text[start:end]))
    if len(bodies) != 1: raise ValueError(f'Expected unique body: {marker}: {len(bodies)}')
    return bodies[0]


HELPER = '''    // Bound to deps/json/json.hpp 3.11.3 serializer::dump_float(double):
    // use the same non-finite rule, 64-byte buffer and Grisu2 formatter.
    static std::string format(double value) {
        static_assert(NLOHMANN_JSON_VERSION_MAJOR == 3 &&
                      NLOHMANN_JSON_VERSION_MINOR == 11 &&
                      NLOHMANN_JSON_VERSION_PATCH == 3,
                      "Revalidate C6a formatting when vendored json.hpp changes");
        static_assert(std::is_same<json::number_float_t, double>::value &&
                      std::numeric_limits<double>::is_iec559 &&
                      std::numeric_limits<double>::digits == 53 &&
                      std::numeric_limits<double>::max_exponent == 1024 &&
                      sizeof(double) == sizeof(uint64_t),
                      "C6a formatting requires the vendored IEEE binary64 path");
        if (!std::isfinite(value)) return "null";
        char buffer[64];
        const char* end = ::nlohmann::detail::to_chars(buffer, buffer + sizeof(buffer), value);
        return std::string(buffer, static_cast<size_t>(end - buffer));
    }
'''


def candidate(before):
    old = '            used_[i] = 1;\n            keys_[i] = bits;\n            vals_[i] = json(value).dump();'
    new = ('            vals_[i] = format(value);\n'
           '            // Publish only after formatting and string assignment succeed.\n'
           '            keys_[i] = bits;\n            used_[i] = 1;')
    assert before.count(old) == 1 and before.count('json(value).dump()') == 2
    value = before.replace(old, new, 1).replace('scratch_ = json(value).dump();', 'scratch_ = format(value);', 1)
    assert value.count('private:\n') == 1
    value = value.replace('private:\n', 'private:\n' + HELPER, 1)
    return value.replace('// 表满：永不错，只是慢', '// Probe exhausted: reuse scratch without publishing a slot')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=DEFAULT)
    parser.add_argument('--final', action='store_true')
    args = parser.parse_args()
    root = args.output.resolve(); root.mkdir(parents=True, exist_ok=True)
    run = root / (('final-' if args.final else 'candidate-') + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
    run.mkdir()
    source_path = REPO / 'native/main.cpp'; source_raw = source_path.read_bytes()
    text = source_raw.decode('utf-8-sig').replace('\r\n', '\n')
    header_raw = (REPO / 'deps/json/json.hpp').read_bytes()
    header = header_raw.decode('utf-8-sig').replace('\r\n', '\n')
    start, end, current_class = extract(text, 'class C6aNumberTextCache {', True)
    if args.final:
        before_class = (root / 'before-class.cpp').read_text(encoding='utf-8-sig')
        candidate_class = (root / 'candidate-class.cpp').read_text(encoding='utf-8-sig')
        assert current_class == candidate_class, 'Final production class differs from tested candidate'
    else:
        before_class = current_class; candidate_class = candidate(before_class)
        for name, data in [('before-class.cpp', before_class), ('candidate-class.cpp', candidate_class)]:
            target = root / name
            if target.exists(): assert target.read_text(encoding='utf-8-sig') == data, 'Existing baseline differs'
            else: target.write_text(data, encoding='utf-8')
    parts = []
    for marker in ['static C6aNumberTextCache& c6aNumberTextCache()',
                   'static void c6aAppendEscaped(std::string& out, const std::string& value)',
                   'static void c6aAppendEscaped(std::string& out, const char* value)',
                   'static void c6aAppendUInt(', 'static void c6aAppendInt(',
                   'static void c6aAppendJsonValue(', 'static std::string inputHostBuildSubmitFrameText(']:
        pos, stop, body = extract(text, marker)
        parts.append({'marker': marker, 'line': text[:pos].count('\n') + 1, 'source': body, 'sha256': SHA(body.encode())})
    _, _, assembler = extract(text, 'static json focusIsolationAssembleFrame(')
    touch_raw = (REPO / 'native/screen_touchpads.h').read_bytes()
    touch_source = touch_raw.decode('utf-8-sig').replace('\r\n', '\n')
    _, _, touch_snapshot = extract(touch_source, 'inline Json snapshotJson(')
    _, _, parity = extract(text, 'static void inputHostVerifySubmitFrameParity(')
    ref_start = parity.index('    json reference = json{')
    ref_end = parity.index('    const std::string referenceText = reference.dump();')
    oracle = '''static std::string wireOracle(const json& frame, const std::string& requestId,
        unsigned long long sequence, unsigned long long sampleSequence,
        unsigned long long monotonicTick, const std::string& normalizedInputHash,
        const char* motionPairProofId) {
''' + parity[ref_start:ref_end] + '    return reference.dump();\n}'
    constants = []
    for name in ['kInputHostProtocolConfigHash', 'kInputHostTargetId', 'kInputHostOwnerIdentity']:
        match = re.search(r'static constexpr const char\* ' + name + r'\s*=.*?;', text, re.S)
        assert match; constants.append(match.group())
    _, _, abi_float = extract(header, '    void dump_float(number_float_t x)')
    _, _, abi_ieee = extract(header, '    void dump_float(number_float_t x, std::true_type')
    assert 'indent_string(512, indent_char)' in header
    assert 'std::array<char, 64> number_buffer' in header
    assert 'class NumberFloatType = double' in header
    binding = {'phase': 'final' if args.final else 'candidate', 'productionModified': args.final,
               'mainPath': str(source_path), 'mainSha256': SHA(source_raw),
               'jsonHeaderPath': str(REPO / 'deps/json/json.hpp'), 'jsonHeaderSha256': SHA(header_raw),
               'classLine': text[:start].count('\n') + 1, 'beforeClassSha256': SHA(before_class.encode()),
               'candidateClassSha256': SHA(candidate_class.encode()), 'currentClassSha256': SHA(current_class.encode()),
               'fragments': parts, 'floatPath': {'dumpFloat': abi_float, 'ieeeDumpFloat': abi_ieee,
                'defaultType': 'double', 'version': '3.11.3', 'numberBuffer': 64, 'serializerIndentBytes': 512},
               'schemaSource': {'assembler': assembler, 'assemblerSha256': SHA(assembler.encode()), 'touchSnapshot': touch_snapshot, 'touchHeaderSha256': SHA(touch_raw)},
               'stubs': 'Value-only envelope identity getters; no Host, OS, crypto, SDK or main execution',
               'classPrivatePublicMacro': 'Test-only introspection; class/accessor/function bodies unchanged'}
    (run / 'source-binding.json').write_text(json.dumps(binding, ensure_ascii=False, indent=2), encoding='utf-8')
    (run / 'source-snapshot.cpp').write_bytes(source_raw)
    (run / 'json.hpp').write_bytes(header_raw)
    (run / 'before-class.cpp').write_text(before_class, encoding='utf-8')
    (run / 'candidate-class.cpp').write_text(candidate_class, encoding='utf-8')
    (run / 'class.patch').write_text(''.join(difflib.unified_diff(before_class.splitlines(True), candidate_class.splitlines(True), fromfile='before/C6aNumberTextCache', tofile='candidate/C6aNumberTextCache')), encoding='utf-8')
    context = '''static unsigned long long g_inputHostConfigRevision=17, g_inputHostEpoch=23;
static std::string g_inputHostRunId="fixture-run-\\\"-\\n", g_inputHostStartupNonce="fixture-nonce";
static const char* inputHostBoundOrCurrentPersona(){return "steamdeck";}
static const char* inputHostBoundOrCurrentProfileIdentity(){return "fixture-profile";}
static unsigned long long inputHostProtocolPowerGenerationLocked(){return 29;}
'''
    for name, code in [('before', before_class), ('candidate', candidate_class)]:
        body = '#define private public\nnamespace ' + name + ' {\n' + code + '\n' + '\n'.join(p['source'] for p in parts) + '\n}\n#undef private\n'
        (run / (name + '.hpp')).write_text(body, encoding='utf-8')
    (run / 'context.hpp').write_text('\n'.join(constants) + '\n' + context + oracle, encoding='utf-8')
    fixture_raw = (REPO / 'tools/virtual_wire_number_perf_selftest.cpp').read_bytes()
    (run / 'fixture.cpp').write_bytes(fixture_raw)
    binding['fixtureSha256'] = SHA(fixture_raw)
    binding['runnerSha256'] = SHA(Path(__file__).read_bytes())
    (run / 'source-binding.json').write_text(json.dumps(binding, ensure_ascii=False, indent=2), encoding='utf-8')
    vcvars = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
    result = {'ok': True, 'phase': binding['phase'], 'productionModified': args.final,
              'runDirectory': str(run), 'binding': {k: v for k, v in binding.items() if k not in ('fragments', 'floatPath')}, 'profiles': []}
    for profile, options in [('release', ['/O2', '/DNDEBUG', '/MT']),
                              ('checked', ['/Od', '/RTC1', '/D_DEBUG', '/D_ITERATOR_DEBUG_LEVEL=2', '/MTd'])]:
        exe = run / (profile + '.exe')
        command = ['cl', '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/fp:precise', *options,
                   str(run / 'fixture.cpp'), '/I' + str(run), '/Fo' + str(run / (profile + '.obj')), '/Fe' + str(exe)]
        batch = run / (profile + '-build.bat')
        batch.write_text('@echo off\nset VSLANG=1033\ncall "' + str(vcvars) + '" >nul\nif errorlevel 1 exit /b %errorlevel%\n' + subprocess.list2cmdline(command) + '\n', encoding='utf-8')
        built = subprocess.run(['cmd.exe', '/d', '/c', str(batch)], cwd=run, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
        (run / (profile + '-build.log')).write_text(built.stdout + built.stderr, encoding='utf-8')
        if built.returncode: raise RuntimeError('Build failed: ' + str(run / (profile + '-build.log')))
        print(profile + ': isolated fixture compiled; running suite', flush=True)
        ran = subprocess.run([str(exe), profile] + (['--verify-only'] if args.final else []), cwd=run, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=300)
        (run / (profile + '-run.json')).write_text(ran.stdout, encoding='utf-8')
        (run / (profile + '-run.stderr')).write_text(ran.stderr, encoding='utf-8')
        if ran.returncode: raise RuntimeError(f'Fixture {profile} exit {ran.returncode}: {ran.stderr}; {run}')
        parsed = json.loads(ran.stdout); parsed['profile'] = profile
        faults = []
        for variant in ['before', 'candidate']:
            for scenario in ['cache', 'fallback', 'wire']:
                def child(point):
                    completed = subprocess.run([str(exe), '--fault', variant, scenario, str(point)], cwd=run,
                        capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=20)
                    stem = f'{profile}-fault-{variant}-{scenario}-{point}'
                    (run / (stem + '.json')).write_text(completed.stdout, encoding='utf-8')
                    (run / (stem + '.stderr')).write_text(completed.stderr, encoding='utf-8')
                    if completed.returncode == 86 and 'TERMINATE during injected allocation' in completed.stderr:
                        return {'point': point, 'exitCode': 86, 'terminated': True, 'observationsUnavailable': True}
                    if completed.returncode: raise RuntimeError(f'Fault child failed {stem}: {completed.returncode}: {completed.stderr}')
                    return {'point': point, 'exitCode': 0, 'terminated': False, 'result': json.loads(completed.stdout)}
                control = child(-1)
                assert not control['terminated']
                cases = [child(point) for point in range(control['result']['controlAllocationCalls'])]
                faults.append({'variant': variant, 'scenario': scenario, 'control': control, 'cases': cases})
        parsed['allocationFaultSubprocesses'] = faults
        result['profiles'].append(parsed)
        print(profile + ': ' + str(parsed['assertions']) + ' assertions passed', flush=True)
    ending = source_path.read_bytes()
    ending_text = ending.decode('utf-8-sig').replace('\r\n', '\n')
    result['sourceAfterSha256'] = SHA(ending)
    result['wholeSourceStableDuringRun'] = ending == source_raw
    result['classStableDuringRun'] = extract(ending_text, 'class C6aNumberTextCache {', True)[2] == current_class
    result['extractedFunctionsStableDuringRun'] = all(extract(ending_text, p['marker'])[2] == p['source'] for p in parts)
    result['jsonHeaderStableDuringRun'] = (REPO / 'deps/json/json.hpp').read_bytes() == header_raw
    assert result['classStableDuringRun'] and result['extractedFunctionsStableDuringRun'] and result['jsonHeaderStableDuringRun']
    for target in [run / 'result.json', root / ('final-result.json' if args.final else 'candidate-result.json')]:
        target.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(str(run), flush=True)


if __name__ == '__main__': main()
