#!/usr/bin/env python3
"""Bounded offline source-context admission tests; never access Steam or a live app.

Extracts complete production selectors, structs, Current guards and query methods.
Win32 queries and the opaque full-capture boundary use owned fixture ports. The
old direct call comes from --before; old/new full capture must be byte identical.
MSVC compiles only cases.cpp, never the engine or driver. Every evidence directory
must be new; failed runs and mutation failures remain available for review.

python tools/steam_original_recovery_source_context_selftest.py --before <engine-before.cpp> --evidence <new-directory>
"""
from __future__ import annotations
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import traceback

ENGINE_CHECKS = [
    'source-context-witness-default-unavailable',
    'source-context-witness-binds-full-steam-lifetime',
    'source-context-witness-cannot-cross-os-worker',
    'source-context-candidate-rejects-invalid-thread',
]


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def pin(path: Path) -> dict:
    b = path.read_bytes()
    return {'path': str(path.resolve()), 'bytes': len(b), 'sha256': sha(b)}


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def braced_end(text: str, start: int) -> int:
    """Find an entire C++ body, ignoring comments, quoted strings and raw strings."""
    i = text.index('{', start)
    depth = 0
    while i < len(text):
        if text.startswith('//', i):
            end = text.find('\n', i + 2)
            i = len(text) if end < 0 else end + 1
            continue
        if text.startswith('/*', i):
            end = text.find('*/', i + 2)
            if end < 0:
                raise ValueError('unterminated C++ comment')
            i = end + 2
            continue
        if text.startswith('R"', i):
            delim_end = text.index('(', i + 2)
            terminator = ')' + text[i + 2:delim_end] + '"'
            i = text.index(terminator, delim_end) + len(terminator)
            continue
        if text[i] in '\"\'':
            quote = text[i]
            i += 1
            while i < len(text):
                if text[i] == '\\':
                    i += 2
                elif text[i] == quote:
                    i += 1
                    break
                else:
                    i += 1
            continue
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError('unterminated C++ body')


def extract(text: str, anchor: str, suffix: str = '') -> tuple[str, dict]:
    if text.count(anchor) != 1:
        raise ValueError(f'expected one exact anchor: {anchor!r}')
    begin = text.index(anchor)
    end = braced_end(text, begin)
    if suffix:
        if not text.startswith(suffix, end):
            raise ValueError(f'expected {suffix!r} after {anchor!r}')
        end += len(suffix)
    fragment = text[begin:end]
    raw = fragment.encode('utf-8')
    return fragment, {'anchor': anchor, 'startLine': text.count('\n', 0, begin) + 1,
                      'startByte': len(text[:begin].encode('utf-8')), 'bytes': len(raw), 'sha256': sha(raw)}


def toolchain(args: argparse.Namespace) -> tuple[Path, dict, dict]:
    vc = Path(args.vc_tools or os.environ.get('VCToolsInstallDir',
              r'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC'))
    if not (vc / 'include').is_dir():
        vc = max((p for p in vc.iterdir() if (p / 'include').is_dir()), key=lambda p: p.name)
    sdk = Path(args.sdk_root)
    version = args.sdk_version or max(p.name for p in (sdk / 'Include').iterdir() if (p / 'um/Windows.h').exists())
    env = os.environ.copy()
    env['INCLUDE'] = ';'.join(map(str, [vc / 'include', sdk / 'Include' / version / 'ucrt',
                                      sdk / 'Include' / version / 'shared', sdk / 'Include' / version / 'um']))
    env['LIB'] = ';'.join(map(str, [vc / 'lib/x64', sdk / 'Lib' / version / 'ucrt/x64', sdk / 'Lib' / version / 'um/x64']))
    compiler = vc / 'bin/Hostx64/x64/cl.exe'
    env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
    return compiler, env, {'compiler': pin(compiler), 'vcTools': str(vc), 'sdkVersion': version}


def run(command: list[str], root: Path, name: str, env: dict) -> subprocess.CompletedProcess:
    result = subprocess.run(command, cwd=root, env=env, capture_output=True, timeout=120)
    (root / (name + '.stdout')).write_bytes(result.stdout)
    (root / (name + '.stderr')).write_bytes(result.stderr)
    write_json(root / (name + '.command.json'), {'argv': command, 'returnCode': result.returncode})
    return result


def fragments(source: str) -> tuple[dict[str, str], list[dict]]:
    anchors = [
        ('need', 'void need(bool value,const char* why)', ''),
        ('probe', 'struct ExecutionProbe {', ';'),
        ('current', 'bool current(){', ''),
        ('requireCurrent', 'void requireCurrent(){', ''),
        ('handle', 'struct H{', ';'),
        ('bound', 'struct BoundLive{', ';'),
        ('scope', 'struct Scope{', ';'),
        ('waitQuery', 'void waitQueryReturn(', ''),
        ('witness', 'struct SourceContextInit {', ';'),
        ('candidate', 'struct SourceThreadCandidate {', ';'),
        ('currentThread', 'bool currentSourceThread(', ''),
        ('findThread', 'std::optional<SourceThreadCandidate> findSourceThreadCandidate(', ''),
        ('selector', 'J sourceGetterContext(', ''),
        ('fire', 'void fire(){', ''),
        ('completed', 'bool completed()const{', ''),
        ('waitComplete', 'void waitComplete(){', ''),
        ('check', 'void check(){', ''),
    ]
    output, manifest = {}, []
    for name, anchor, suffix in anchors:
        output[name], item = extract(source, anchor, suffix)
        item['name'] = name
        manifest.append(item)
    for name, anchor in [('probeTLS', 'thread_local ExecutionProbe* gExecutionProbe=nullptr;'),
                         ('witnessTLS', 'thread_local SourceContextInit gSourceContextInit;')]:
        if source.count(anchor) != 1:
            raise ValueError('missing production TLS declaration: ' + anchor)
        output[name] = anchor
        manifest.append({'name': name, 'anchor': anchor, 'sha256': sha(anchor.encode())})
    for name in ENGINE_CHECKS:
        output[name], item = extract(source, 'test("' + name + '"', ');')
        item['name'] = name
        manifest.append(item)
    return output, manifest


def generate(root: Path, parts: dict[str, str], old_call: str) -> None:
    root.mkdir()
    def save(name: str, keys: list[str]) -> None:
        (root / name).write_text('\n'.join(parts[k] for k in keys) + '\n', encoding='utf-8')
    save('common.inc', ['need', 'probe', 'probeTLS', 'current', 'requireCurrent', 'handle', 'bound', 'scope', 'waitQuery'])
    save('source-types.inc', ['witness', 'witnessTLS', 'candidate'])
    save('thread-functions.inc', ['currentThread', 'findThread'])
    save('selection.inc', ['selector'])
    save('query-methods.inc', ['fire', 'completed', 'waitComplete', 'check'])
    save('engine-checks.inc', ENGINE_CHECKS)
    (root / 'production.inc').write_text(
        '#include "ports.hpp"\n#include "common.inc"\n#include "source-types.inc"\n'
        '#include "thread-functions.inc"\n#include "query_capture_ports.hpp"\n#include "selection.inc"\n'
        'J oldSourceGetterContext(Scope& scope,const Snapshot& sample,U64 account,QueryActor& query,J&){J context;'
        + old_call + 'return context;}\n', encoding='utf-8')


def mutations(parts: dict[str, str]):
    specifications = [
        ('delete-policy', 'selector', 'if(DebugSetProcessKillOnExit(FALSE)){', 'if(true){'),
        ('skip-entry-Current', 'selector', 'need(scope.owned(),"warm-source-scope-not-current");', ''),
        ('skip-post-census-Current', 'selector', 'need(scope.owned(),"warm-source-scope-changed-after-census");', ''),
        ('skip-post-query-Current', 'selector', 'need(scope.owned(),"warm-source-scope-changed-after-query");', ''),
        ('accept-multiple-candidates', 'findThread', 'matches!=1', 'matches<1'),
        ('ignore-census-GetLastError', 'findThread', 'error!=ERROR_NO_MORE_FILES||', ''),
        ('skip-post-query-thread-recheck', 'selector', 'if(currentSourceThread(*candidate,steam.pid)){', 'if(true){'),
        ('reuse-account-authorization', 'selector', '{"expectedAccountMatched",false}', '{"expectedAccountMatched",true}'),
        ('reuse-owner-authorization', 'selector', '{"originalInterfaceOwnerMatched",false}', '{"originalInterfaceOwnerMatched",true}'),
        ('ignore-steam-lifetime', 'witness', 'steamPid==steam.pid&&steamBirth==steam.born&&steamPath==steam.path', 'true'),
        ('skip-final-capture-Current', 'selector', 'requireCurrent();gSourceContextInit=', 'gSourceContextInit='),
        ('ignore-thread-birth', 'currentThread', '((U64(born.dwHighDateTime)<<32)|born.dwLowDateTime)!=thread.birth', 'false'),
        ('remove-worker-TLS', 'witnessTLS', 'thread_local SourceContextInit', 'SourceContextInit'),
        ('publish-witness-before-capture', 'selector',
         'gSourceContextInit={};const auto context=captureGetterContext(scope,snap,account,query);',
         'gSourceContextInit={steam.pid,steam.born,steam.path};const auto context=captureGetterContext(scope,snap,account,query);'),
    ]
    for name, key, old, new in specifications:
        if parts[key].count(old) != 1:
            raise ValueError('mutation anchor not unique: ' + name)
        changed = dict(parts)
        changed[key] = changed[key].replace(old, new)
        yield name, changed, {'fragment': key, 'before': old, 'after': new}


def compile_and_test(root: Path, compiler: Path, env: dict, fixture: Path, json_dir: Path) -> dict:
    exe = root / 'source-context-fixture.exe'
    argv = [str(compiler), '/nologo', '/std:c++17', '/EHsc', '/W4', '/utf-8', '/Od',
            '/I' + str(root), '/I' + str(fixture), '/I' + str(json_dir), str(fixture / 'cases.cpp'),
            '/Fe:' + str(exe), '/Fo:' + str(root / 'cases.obj'), '/link', '/INCREMENTAL:NO']
    build = run(argv, root, 'build', env)
    if build.returncode:
        return {'compiled': False, 'buildReturnCode': build.returncode}
    result = run([str(exe)], root, 'test', env)
    data = json.loads(result.stdout)
    write_json(root / 'result.json', data)
    return {'compiled': True, 'returnCode': result.returncode, 'success': data['success'],
            'passed': data['passed'], 'total': data['total'],
            'failures': [{'name': c['name'], 'error': c.get('error')} for c in data['cases'] if not c['passed']]}


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--before', type=Path, required=True)
    p.add_argument('--evidence', type=Path, required=True, help='new directory only; no overwrite')
    p.add_argument('--source', type=Path)
    p.add_argument('--vc-tools')
    p.add_argument('--sdk-root', default=r'C:/Program Files (x86)/Windows Kits/10')
    p.add_argument('--sdk-version')
    args = p.parse_args()
    repo = Path(__file__).resolve().parents[1]
    source = (args.source or repo / 'native/steam_original_recovery_engine.cpp').resolve()
    fixture = Path(__file__).resolve().parent / 'steam_original_recovery_source_context_fixtures'
    root = args.evidence.resolve()
    root.mkdir(parents=True, exist_ok=False)
    summary = {'success': False, 'at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'scope': 'bounded source-context selector and query consumer with fixture ports'}
    try:
        old_bytes, new_bytes = args.before.read_bytes(), source.read_bytes()
        old, new = old_bytes.decode('utf-8'), new_bytes.decode('utf-8')
        (root / 'engine-before.cpp').write_bytes(old_bytes)
        (root / 'engine-tested.cpp').write_bytes(new_bytes)
        write_json(root / 'inputs.json', {'source': pin(source), 'before': pin(args.before),
                   'selftest': pin(Path(__file__)), 'fixtures': [pin(f) for f in sorted(fixture.iterdir()) if f.is_file()],
                   'jsonHeader': pin(repo / 'deps/json/json.hpp')})
        old_capture, old_cap_pin = extract(old, 'J captureGetterContext(')
        new_capture, new_cap_pin = extract(new, 'J captureGetterContext(')
        if old_capture != new_capture:
            raise ValueError('full original capture changed; refuse old/new boundary equivalence')
        old_consume, _ = extract(old, 'void consume(')
        new_consume, _ = extract(new, 'void consume(')
        old_request = re.search(r'^      J request=.*;(?=\r?$)', old, re.M)
        new_request = re.search(r'^      J request=.*;(?=\r?$)', new, re.M)
        if old_consume != new_consume or not old_request or not new_request or old_request[0] != new_request[0]:
            raise ValueError('one-attempt ledger or driver request contract changed')
        old_call = 'context=captureGetterContext(scope,sample,account,query);'
        if old.count(old_call) != 1:
            raise ValueError('old production direct capture call not unique')
        for s in (old, new):
            call = old_call if s is old else 'context=sourceGetterContext(scope,sample,account,query,summary);'
            if s.count(call) != 1 or not s.index('consume(event,scope,*data);summary["consumed"]=true;') < s.index(call) < s.index(new_request[0]):
                raise ValueError('consume/selection/request ordering changed')
        if 'catch(const std::exception& e){gSourceContextInit={};' not in new:
            raise ValueError('production failure handler does not clear witness')
        parts, manifest = fragments(new)
        write_json(root / 'extraction.json', {'verbatimFragments': manifest, 'captureBefore': old_cap_pin,
                   'captureAfter': new_cap_pin, 'captureBodyIdentical': True, 'consumeBodyIdentical': True,
                   'driverRequestIdentical': True, 'consumeBeforeSelectionBeforeRequest': True,
                   'oldDirectCall': old_call, 'failureHandlerClearsWitness': True})
        compiler, env, tc = toolchain(args)
        write_json(root / 'toolchain.json', tc)
        baseline = root / 'baseline'
        generate(baseline, parts, old_call)
        normal = compile_and_test(baseline, compiler, env, fixture, repo / 'deps/json')
        summary['baseline'] = normal
        print('baseline: ' + json.dumps(normal), flush=True)
        if not normal.get('success'):
            raise RuntimeError('baseline failed; build/test logs preserved')
        detected = []
        for name, changed, modification in mutations(parts):
            target = root / name
            generate(target, changed, old_call)
            write_json(target / 'mutation.json', modification)
            result = compile_and_test(target, compiler, env, fixture, repo / 'deps/json')
            killed = result.get('compiled') and result.get('returnCode') == 1 and bool(result.get('failures'))
            detected.append({'name': name, 'detectedByExecutedAssertions': bool(killed), **result})
            print(name + ': ' + ('DETECTED' if killed else 'SURVIVED OR BUILD FAILED'), flush=True)
        summary['mutations'] = detected
        if source.read_bytes() != new_bytes:
            raise RuntimeError('engine changed during test; keep evidence and rerun latest source')
        inputs = json.loads((root / 'inputs.json').read_text(encoding='utf-8'))
        for item in inputs['fixtures'] + [inputs['selftest'], inputs['jsonHeader']]:
            if pin(Path(item['path'])) != item:
                raise RuntimeError('test input changed during execution: ' + item['path'])
        summary['source'] = inputs['source']
        summary['engineChecks'] = {'allOriginalFourIncludedVerbatimAndExecuted': True, 'names': ENGINE_CHECKS,
            'actualTLSAndStdThread': True,
            'additionalRealOwnedThreadEngineCheckPresent': 'source-context-real-owned-thread-selected-and-revalidated' in new,
            'additionalRealOwnedThreadEngineCheckExecutedHere': False}
        summary['boundaries'] = {'productionSelectionAndThreadQueriesExecuted': True,
            'productionCurrentAndScopeExecuted': True, 'productionQueryPublicMethodsExecuted': True,
            'steamSDKWorker': 'fixture completion port', 'fullDebuggerCapture': 'opaque scripted fixture port, body equality checked',
            'prepare': 'fixture ordering boundary, not actual driver', 'ledgerAndDriverExecuted': False,
            'steamAccess': False, 'liveApplicationControl': False, 'engineBuildOrFreeze': False,
            'warmFieldAcceptance': False}
        related = {}
        for file in ['debug-object-fixture-result.json', 'pinned-dispatch-tests/receipt.json', 'pinned-dispatch-tests/result.json']:
            path = args.before.resolve().parent / file
            if path.is_file():
                related[file] = {'pin': pin(path), 'provenance': 'main-thread evidence; not executed by this selftest'}
        summary['relatedEvidence'] = related
        summary['success'] = all(m['detectedByExecutedAssertions'] for m in detected)
        if not summary['success']:
            raise RuntimeError('one or more deliberate defects survived or did not compile')
        report = ['# Source-context bounded fixture report', '',
            f'Production engine SHA256: `{inputs["source"]["sha256"]}`', '',
            f'PASS: {normal["passed"]}/{normal["total"]} fixture cases; {len(detected)}/{len(detected)} deliberate defects compiled and were rejected by executed assertions.', '',
            'The old baseline uses its verbatim direct capture call. The new consumer uses complete verbatim sourceGetterContext, currentSourceThread and findSourceThreadCandidate, actual SourceContextInit/TLS, H/BoundLive/Scope, Current guards and QueryActor fire/completed/waitComplete/check plus waitQueryReturn.', '',
            'All original four engine checks are included verbatim and executed. The TLS check uses a real std::thread and the actual thread_local declaration. A separate new-worker scenario primes the parent through successful capture before starting its cold child. The added real-owned-thread engine check is only recorded as present; the main thread owns that execution.', '',
            'Win32 calls resolve to bounded in-memory fixture ports. The Steam SDK worker and full debugger capture are ports; identical full capture bodies and ledger/request/order were checked. Prepare is only an ordering port. This run does not execute the actual debugger capture, driver, ledger, engine build, freeze or any Steam/field operation. No warm field success is claimed.', '',
            'Current values are read by the actual guard after warmup/census/query/capture return. LastError is deliberately clobbered by subsequent mock APIs; the complete-census tests require the terminal Thread32Next error, before revalidation. Candidate uniqueness, complete enumeration and post-query revalidation each have executed destructive controls.', '',
            'See baseline/result.json for every case and trace; each mutation directory preserves its modified snippets, compile logs and failing assertions. Related main-thread Win32/static-library evidence is separately pinned in summary.json.']
        (root / 'REPORT.md').write_text('\n'.join(report) + '\n', encoding='utf-8')
    except Exception as exc:
        summary['error'] = str(exc)
        (root / 'failure.txt').write_text(traceback.format_exc(), encoding='utf-8')
        print(str(exc), file=sys.stderr, flush=True)
    write_json(root / 'summary.json', summary)
    print(str(root / 'summary.json'), flush=True)
    return 0 if summary['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
