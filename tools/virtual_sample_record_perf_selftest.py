"""Measure and validate actual before/current diagnostic record assembly offline.

Four owned nested JSON trees are transferred after the scalar object initializer.
Builders/queues/rates remain bound byte-for-byte. No product or device SDK runs.
"""
from pathlib import Path
import argparse, datetime, difflib, hashlib, json, os, subprocess
from virtual_sample_metadata_perf_selftest import matching_brace, declaration
from virtual_sample_queue_perf_selftest import sample_from_zip, DEFAULT_ZIP, extract

REPO = Path(__file__).resolve().parent.parent
TREES = {'imuObj': 'imu', 'hcCandidateObj': 'hcMatrixCandidate', 'eventObj': 'event', 'testLaneLiveObj': 'testLane'}
SHA = lambda b: hashlib.sha256(b).hexdigest()


def assembly(text):
    producer = extract(text, 'if (wantFullSample) {')
    start = producer.index('        rec = {')
    end = matching_brace(producer, producer.index('{', start)) + 2
    while producer[end:end+1] == '\n': end += 1
    if producer[end:].startswith('        // Transfer completed nested diagnostic trees'):
        end = producer.index('        // 会话常量 delta', end)
    return producer[start:end].rstrip()


def candidate(text):
    old = assembly(text)
    new = old
    for var, key in TREES.items():
        entry = f'            {{"{key}", {var}}},\n'
        assert new.count(entry) == 1, entry
        new = new.replace(entry, '')
    new += '\n        // Transfer completed nested diagnostic trees into their sole record owner.\n'
    new += '\n'.join(f'        rec["{key}"] = std::move({var});' for var, key in TREES.items())
    assert text.count(old) == 1
    text = text.replace(old, new)
    for var in TREES:
        assert text.count(f'const json {var} =') == 1
        text = text.replace(f'const json {var} =', f'json {var} =')
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sample-zip', type=Path, default=DEFAULT_ZIP)
    parser.add_argument('--candidate', action='store_true', help='Test proposed source in evidence directory without editing production')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    out = args.output / ('run-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + str(os.getpid()))
    out.mkdir()
    before = args.before.read_text(encoding='utf-8-sig')
    current = candidate(before) if args.candidate else (REPO/'native/main.cpp').read_text(encoding='utf-8-sig')
    proposed = candidate(before)
    checks = {'assembly-only-owned-transfer': assembly(proposed) == assembly(current)}
    for var in TREES:
        a = declaration(before, f'const json {var} =')
        b = declaration(current, f'json {var} =')
        checks[var + '-builder-unchanged'] = a.replace('const json', 'json', 1) == b
    # Bind every other byte in the sample producer, including delta/IMU slimming and queue transfer.
    checks['whole-producer-only-four-transfers'] = extract(proposed, 'if (wantFullSample) {') == extract(current, 'if (wantFullSample) {')
    checks['other-source-unchanged'] = proposed == current
    binding = {'paths': {'before': str(args.before.resolve()), 'current': str(REPO/'native/main.cpp')},
        'beforeSha256': SHA(args.before.read_bytes()), 'testedCurrentSha256': SHA(current.encode('utf-8')),
        'jsonHeaderSha256': SHA((REPO/'deps/json/json.hpp').read_bytes()),
        'harnessSha256': SHA((REPO/'tools/virtual_sample_record_perf_selftest.cpp').read_bytes()),
        'candidateOnly': args.candidate, 'checks': checks}
    (out/'source-binding.json').write_text(json.dumps(binding, indent=2)+'\n', encoding='utf-8')
    assert all(checks.values()), checks
    (out/'candidate-main.cpp').write_text(current, encoding='utf-8')
    (out/'record-assembly.diff').write_text(''.join(difflib.unified_diff(before.splitlines(True),current.splitlines(True),fromfile='before/main.cpp',tofile='current/main.cpp')),encoding='utf-8')
    sample, provenance = sample_from_zip(args.sample_zip.resolve(), out)
    for key in TREES.values(): assert key in sample, key
    (out/'real-sample.json').write_text(json.dumps(sample,ensure_ascii=False,separators=(',',':'))+'\n',encoding='utf-8')
    (out/'provenance.json').write_text(json.dumps(provenance,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    methods = ''
    for name, source in [('beforeRecord', before), ('currentRecord', current)]:
        methods += f'json {name}() {{ json rec;\n' + assembly(source) + '\nreturn rec; }\n'
    (out/'record-extracted.h').write_text(methods, encoding='utf-8')
    vcvars = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
    harness = (REPO/'tools/virtual_sample_record_perf_selftest.cpp').read_bytes()
    assert SHA(harness) == binding['harnessSha256']
    (out/'fixture.cpp').write_bytes(harness)
    results = []
    for name, opts in [('release',['/O2','/DNDEBUG']), ('checked',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2'])]:
        exe = out/(name+'.exe')
        command = ['cl','/nologo','/std:c++20','/utf-8','/EHsc',('/MTd' if name == 'checked' else '/MT'), *opts,
            str(out/'fixture.cpp'), '/I'+str(REPO/'deps/json'), '/I'+str(out),
            '/Fo'+str(out/(name+'.obj')), '/Fe'+str(exe)]
        batch = out/(name+'-build.bat')
        batch.write_text('@echo off\nset VSLANG=1033\ncall "'+str(vcvars)+'" >nul\nif errorlevel 1 exit /b %errorlevel%\n'+subprocess.list2cmdline(command)+'\n',encoding='utf-8')
        built = subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=out,capture_output=True,encoding='utf-8',errors='replace',timeout=120)
        (out/(name+'-build.log')).write_text(built.stdout+built.stderr,encoding='utf-8')
        if built.returncode: raise RuntimeError('Build failed: '+str(batch))
        ran = subprocess.run([str(exe),str(out/'real-sample.json')],cwd=out,capture_output=True,encoding='utf-8',errors='replace',timeout=90)
        (out/(name+'-run.json')).write_text(ran.stdout,encoding='utf-8')
        (out/(name+'-run.stderr')).write_text(ran.stderr,encoding='utf-8')
        if ran.returncode: raise RuntimeError('Run failed: '+str(exe)+': '+ran.stderr)
        result = json.loads(ran.stdout); result['profile'] = name
        results.append(result)
    assert (REPO/'deps/json/json.hpp').read_bytes() and SHA((REPO/'deps/json/json.hpp').read_bytes()) == binding['jsonHeaderSha256']
    assert SHA((REPO/'tools/virtual_sample_record_perf_selftest.cpp').read_bytes()) == binding['harnessSha256']
    assert args.candidate or (REPO/'native/main.cpp').read_text(encoding='utf-8-sig') == current
    result = {'pass':True,'run':str(out),'binding':binding,'provenance':provenance,'profiles':results,
        'scope':'Actual extracted record initializer + four transfers. Real nested trees prepared outside measured assembly; tree construction, delta, serialization, writer and hardware excluded. Timed scope includes resulting record destruction. No whole-process CPU claim.'}
    (args.output/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'pass':True,'run':str(out),'profiles':[{'profile':r['profile'],'assertions':r['assertions'],'benchmarks':r['benchmarks']} for r in results]},indent=2))

if __name__ == '__main__': main()
