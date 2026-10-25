#!/usr/bin/env python3
"""Execute complete extracted sensor Ensure/Post/Reap/Shutdown under explicit leaves.

Uses existing ymcc_sim native/Python identities; no global suite, YMCC process,
COM, real sensor, device, driver, service, installed state, sleep or export.
Historical source is immutable evidence. Positive resource zero requires actual
kernel joins/closes before exit, separately from memory-model accounting.
"""
from __future__ import annotations
import argparse
import datetime as dt
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = Path('G:/YeManCC-Work/Testing/YMCC/Evidence/VC-SensorEnsure23')
FIX = 'tools/fixtures/input_capture_timer_retirement'
OLD_MAIN_SHA = '50ade4c88674fac353bf2445033fc6b34becc76ad7815da4ea0d28e8d6a307db'
INTERMEDIATE_MAIN_SHA = 'd7d3884307855dc7a12ad1296df4e4e37b3b69be55fabd1e8a2d8da807f5feca'
APPROVED_MAIN_SHAS = (INTERMEDIATE_MAIN_SHA, 'daa89a5e9d4c6e849eaf773c209a217af90349f166ea1cd64ed37e5b75dcde10')
NAMES = ('inputCaptureSensorThreadEnsure', 'inputCaptureSensorThreadPost',
         'inputCaptureSensorThreadReap', 'inputCaptureSensorThreadShutdown')
TOOLS = 'tools/input_capture_sensor_owners_selftest.py'
MASK = 'tools/input_timer_evidence_adapter_selftest.py'
AUTH = 'tools/ymcc_sim/global/controller-signal-evidence/tool-inputs.json'
SCHEMA = 'tools/ymcc_sim/global/controller-signal-evidence/consumer-schema.json'
SUPPLEMENT = 'tools/ymcc_sim/global/decky-persistent-lifecycle/owned-tools.json'
SUPPLEMENT_SHA = 'bd707e8124368840f34f14f61542b0c67de4c2c4c76e31e62c1cc014c03f70d1'
SOURCES = ('native/main.cpp', 'native/sensor_thread_mailbox.h', 'deps/json/json.hpp',
           'tools/input_capture_sensor_ensure_selftest.py', TOOLS, MASK, AUTH, SUPPLEMENT, SCHEMA,
           FIX+'/sensor_ensure_memory.cpp.in', FIX+'/sensor_ensure_kernel.cpp.in')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def need(ok, message):
    if not ok:
        raise RuntimeError(message)


def save(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def extract(text, name, mask):
    code = mask(text)
    matches = list(re.finditer(r'^static[^\n;{}]*\b'+re.escape(name)+r'\s*\([^;{}]*\)\s*(?:noexcept\s*)?\{', code, re.M))
    need(len(matches) == 1, 'Unique complete actual definition required: '+name)
    start, end, depth = matches[0].start(), matches[0].end(), 1
    while depth:
        need(end < len(code), 'Unclosed source definition: '+name)
        depth += (code[end] == '{')-(code[end] == '}')
        end += 1
    fragment = text[start:end]
    return {'text': fragment, 'line': text.count('\n', 0, start)+1, 'sha256': sha(fragment.encode('utf-8'))}


def mutate(ensure, kind):
    if kind == 'restore-unchecked-close':
        pattern = r'if\s*\(!CloseHandle\(g_sensorThread\)\)\s*return\s+rejected\("thread-close",state,GetLastError\(\)\);'
        changed, count = re.subn(pattern, 'CloseHandle(g_sensorThread);', ensure)
    else:
        pattern = r'state\s*==\s*WAIT_TIMEOUT'
        changed, count = re.subn(pattern, '(state==WAIT_TIMEOUT || state==WAIT_FAILED)', ensure)
    need(count == 1, 'Mutation must bind one actual Ensure statement: '+kind)
    return changed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-root', type=Path, default=OUT)
    parser.add_argument('--historical-source', type=Path, default=OUT/'main.before.cpp')
    parser.add_argument('--expected-main-sha256', default=INTERMEDIATE_MAIN_SHA)
    parser.add_argument('--runtime-source-closure', '--runtime_sourceClosure', dest='runtime_source_closure', type=Path)
    parser.add_argument('--source-identity-manifest', type=Path)
    parser.add_argument('--historical-only', action='store_true')
    parser.add_argument('--main-source', type=Path, help='Explicit immutable parent-authorized main snapshot')
    parser.add_argument('--profile', choices=('Release', 'Checked', 'both'), default='both')
    args = parser.parse_args()
    run = args.output_root.resolve()/('r'+dt.datetime.now().strftime('%H%M%S-%f')+'-'+str(os.getpid()))
    snapshot = run/'s'
    snapshot.mkdir(parents=True)
    source = {}
    runtime_pins = None
    external_inputs = []
    receipt = {'schemaVersion': 1, 'status': 'RUNNING', 'productionAcceptance': False,
               'expectedMainSha256': args.expected_main_sha256, 'historicalMainSha256': OLD_MAIN_SHA,
               'sourceFreeze': 'intermediate-parent-authorized-snapshot; final whole-source freeze pending',
               'execution': 'complete actual Ensure/Post/Reap/Shutdown plus actual ThreadMailbox',
               'substitutions': {
                   'threadproc': 'private event/mailbox helper only; no COM/sensors/devices or production threadproc',
                   'memory': 'CreateEvent/CreateThread/Wait/Close/SetEvent synthetic owned records',
                   'kernel': 'successful leaves actual private unnamed Win32 handles; explicit failure injection only',
                   'logging': 'appendNativeLifecycleLog JSON memory sink; writeLog memory no-op',
                   'capabilityGate': 'none added; Ensure called as its native worker consumer contract'},
               'remaining': ['full production STA threadproc/COM/sensor behavior UNKNOWN',
                             'arbitrarily stalled real sensor/COM bounded destruction UNKNOWN',
                             'installed/device/driver/service/sleep acceptance NOT_RUN',
                             'parent final whole-source freeze and Host consumers pending'], 'rows': []}

    def publish():
        receipt['sources'] = [{'path': name, 'sha256': sha(data)} for name, data in source.items()]
        save(run/'REPORT.json', receipt)
        if receipt.get('status') != 'RUNNING':
            save(args.output_root.resolve()/'RESULT.json', receipt)

    def freeze(name):
        if name not in source:
            data = (args.main_source if name == 'native/main.cpp' and args.main_source else ROOT/name).read_bytes()
            if runtime_pins is not None:
                need(name in runtime_pins and sha(data) == runtime_pins[name], 'Runtime source closure pin mismatch: '+name)
            target = snapshot/name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            source[name] = data
        return source[name]

    try:
        contract = json.loads((ROOT/SCHEMA).read_text(encoding='utf-8-sig'))['sensorEnsure']
        if args.runtime_source_closure:
            need(args.source_identity_manifest is not None and args.main_source is None and not args.historical_only and args.profile == 'both',
                 'Registered runtime mode requires approved manifest, live main and all profiles')
            approved = contract['currentMainManifest']
            manifest_path = args.source_identity_manifest.resolve()
            need(manifest_path == Path(approved['path']).resolve(), 'Source identity manifest is outside fixed approved path')
            manifest_bytes = manifest_path.read_bytes()
            need(sha(manifest_bytes) == approved['sha256'], 'Source identity manifest differs from fixed approved pin')
            identity = json.loads(manifest_bytes.decode('utf-8-sig'))
            expected_main = identity.get('mainSha256')
            need(re.fullmatch('[a-f0-9]{64}', expected_main or '') and identity.get('sourceBindings', {}).get('native/main.cpp') == expected_main,
                 'Approved identity manifest lacks consistent explicit main pin')
            closure_path = args.runtime_source_closure.resolve()
            closure_bytes = closure_path.read_bytes()
            runtime_pins = json.loads(closure_bytes.decode('utf-8-sig'))
            need(isinstance(runtime_pins, dict) and set(contract['requiredSources']) <= set(runtime_pins), 'Runtime source closure incomplete')
            for relative, pin in runtime_pins.items():
                need(not Path(relative).is_absolute() and '..' not in Path(relative).parts and re.fullmatch('[a-f0-9]{64}', pin or ''),
                     'Runtime source closure contains invalid path or hash')
                need(sha((ROOT/relative).read_bytes()) == pin, 'Runtime source closure differs from actual input: '+relative)
            need(runtime_pins['native/main.cpp'] == expected_main, 'Runtime closure main is not explicitly approved main')
            for relative, pin in identity['sourceBindings'].items():
                need(relative in runtime_pins and runtime_pins[relative] == pin, 'Runtime closure differs from approved source binding: '+relative)
            receipt['runtime_sourceClosure'] = runtime_pins
            receipt['runtimeSourceClosureIdentity'] = {'path': str(closure_path), 'sha256': sha(closure_bytes)}
            receipt['approvedIdentityManifest'] = {'path': str(manifest_path), 'sha256': approved['sha256']}
            receipt['identityMode'] = 'approved-manifest-and-runtime-source-closure'
            receipt['expectedMainSha256'] = expected_main
            external_inputs = [(manifest_path, sha(manifest_bytes)), (closure_path, sha(closure_bytes))]
        else:
            need(args.source_identity_manifest is None, 'Manifest without runtime source closure is not admitted')
        for name in SOURCES:
            freeze(name)
        supplement = json.loads(source[AUTH].decode('utf-8-sig'))
        freeze(supplement['nativeAuthority'])
        helper = load(snapshot/TOOLS, 'ensure_snapshot_tools')
        mask = load(snapshot/MASK, 'ensure_snapshot_mask').mask_cpp
        receipt['primaryToolsBefore'] = helper.primary_tools()[1]
        old_bytes = args.historical_source.read_bytes()
        if runtime_pins is not None:
            need(args.historical_source.resolve() == Path(contract['historicalMain']['path']).resolve(), 'Historical source outside fixed approved path')
        need(sha(old_bytes) == OLD_MAIN_SHA, 'Historical main source does not match fixed 50ade4c8 identity')
        external_inputs.append((args.historical_source.resolve(), OLD_MAIN_SHA))
        (run/'historical-main.cpp').write_bytes(old_bytes)
        receipt['historicalSourcePath'] = str(args.historical_source.resolve())
        current_sha = sha(source['native/main.cpp'])
        receipt['actualMainSha256'] = current_sha
        receipt['mainInputPath'] = str((args.main_source or ROOT/'native/main.cpp').resolve())
        if runtime_pins is not None:
            need(current_sha == receipt['expectedMainSha256'], 'Actual main differs from explicitly approved manifest')
        elif not args.historical_only:
            need(args.expected_main_sha256 in APPROVED_MAIN_SHAS, 'Main freeze is not an explicitly approved parent identity')
            need(current_sha == args.expected_main_sha256, 'Main differs from parent authorized fixed snapshot')
        current_text = source['native/main.cpp'].decode('utf-8-sig')
        old_text = old_bytes.decode('utf-8-sig')
        current = {name: extract(current_text, name, mask) for name in NAMES}
        historical = {name: extract(old_text, name, mask) for name in NAMES}
        receipt['extractedFunctions'] = {kind: {name: {k: v for k, v in part.items() if k != 'text'} for name, part in parts.items()}
                                         for kind, parts in [('current', current), ('historical', historical)]}
        for kind, parts in [('current', current), ('historical', historical)]:
            for name, part in parts.items():
                (run/(kind+'-'+name+'.cpp')).write_bytes(part['text'].encode('utf-8'))
        log_classifier = extract(current_text, 'nativeLifecycleEventIsBase', mask) if re.search(r'\bnativeLifecycleEventIsBase\s*\(', current_text) else None
        receipt['baseEventRegistration'] = {'execution': 'SOURCE_ONLY', 'event': 'input-capture-sensor-owner-rejected',
                                             'presentInMain': '"input-capture-sensor-owner-rejected"' in current_text,
                                             'classifierExtracted': log_classifier is not None,
                                             'presentInClassifier': log_classifier is not None and '"input-capture-sensor-owner-rejected"' in log_classifier['text']}
        compiler, env = helper.compiler_environment(run)
        receipt['compiler'] = {'path': str(compiler), 'sha256': sha(compiler.read_bytes()),
                               'vcvarsPath': str(helper.VCVARS), 'vcvarsSha256': sha(helper.VCVARS.read_bytes()),
                               'sdkVersion': env.get('WindowsSDKVersion')}
        receipt['snapshotPath'] = str(snapshot)
        receipt['runPath'] = str(run)
        temp = run/'tmp'
        temp.mkdir()
        env['TEMP'] = env['TMP'] = str(temp)
        authority = json.loads(source[supplement['nativeAuthority']].decode('utf-8-sig'))
        need(sha(source[SUPPLEMENT]) == SUPPLEMENT_SHA, 'Existing supplemental tool authority changed')
        extra_pins = json.loads(source[SUPPLEMENT].decode('utf-8-sig'))
        pins = {str(Path(row['path']).resolve()).lower(): row['sha256'] for row in [*authority['files'], *supplement['files'], *extra_pins['files']]}
        receipt['toolchain'] = {'include': authority['native']['include'], 'lib': authority['native']['lib']}
        receipt['authorityFilesBefore'] = []
        for path, pin in pins.items():
            need(sha(Path(path).read_bytes()) == pin, 'Existing declared tool input drift: '+path)
            receipt['authorityFilesBefore'].append({'path': path, 'sha256': pin})
        receipt['pythonModules'] = []
        for module_name, module in sorted(sys.modules.items()):
            filename = getattr(module, '__file__', None)
            if not filename:
                continue
            path = Path(filename).resolve()
            if path.suffix == '.pyc' and path.with_suffix('.py').is_file():
                path = path.with_suffix('.py')
            if path.is_relative_to(snapshot):
                continue
            if path.is_relative_to(ROOT):
                name = path.relative_to(ROOT).as_posix()
                need(name in source and path.read_bytes() == source[name], 'Python repository import escaped byte snapshot: '+name)
                continue
            actual = sha(path.read_bytes())
            need(pins.get(str(path).lower()) == actual, 'Imported Python module lacks existing exact identity: '+str(path))
            receipt['pythonModules'].append({'module': module_name, 'path': str(path), 'sha256': actual})
        observed_deps = {}
        profiles = [('Release', ['/O2', '/MT', '/DNDEBUG']), ('Checked', ['/Od', '/RTC1', '/MTd', '/D_DEBUG'])]
        if args.profile != 'both':
            profiles = [p for p in profiles if p[0] == args.profile]
        variants = ['historical'] if args.historical_only else ['current', 'historical', 'restore-unchecked-close', 'wait-failed-treated-alive']
        for profile, flags in profiles:
            for mode in ('memory', 'kernel'):
                for variant in variants:
                    directory = run/(profile[0]+mode[0]+str(variants.index(variant)))
                    directory.mkdir()
                    parts = historical if variant == 'historical' else current
                    functions = []
                    for name in (NAMES[1], NAMES[0], NAMES[2], NAMES[3]):
                        part = parts[name]
                        fragment = part['text']
                        if name == NAMES[0] and variant not in ('current', 'historical'):
                            fragment = mutate(fragment, variant)
                        functions.append('#line '+str(part['line'])+' "'+('historical-main.cpp' if variant == 'historical' else 'native/main.cpp')+'"\n'+fragment)
                    cpp = source[FIX+'/sensor_ensure_memory.cpp.in'].decode('utf-8-sig')
                    cpp = cpp.replace('@KERNEL_PORT@', source[FIX+'/sensor_ensure_kernel.cpp.in'].decode('utf-8-sig'))
                    cpp = cpp.replace('@ACTUAL@', '\n\n'.join(functions))
                    need('@ACTUAL@' not in cpp and '@KERNEL_PORT@' not in cpp, 'Unexpanded source placeholder')
                    (directory/'test.cpp').write_bytes(cpp.encode('utf-8'))
                    row = {'profile': profile, 'mode': mode, 'variant': variant, 'expectedNegative': variant != 'current',
                           'inputSha256': sha(cpp.encode('utf-8')), 'accepted': False}
                    receipt['rows'].append(row)
                    publish()
                    argv = [compiler, '/nologo', '/std:c++17', '/utf-8', '/EHsc', '/W4', *flags,
                            '/DYMCC_ENSURE_KERNEL='+str(int(mode == 'kernel')), '/I'+str(snapshot/'native'),
                            '/I'+str(snapshot/'deps/json'), '/sourceDependencies', directory/'deps.json',
                            directory/'test.cpp', '/Fo'+str(directory/'test.obj'), '/Fe'+str(directory/'test.exe')]
                    row['compileCommand'] = [str(value) for value in argv]
                    row['runtimeCommand'] = [str(directory/'test.exe')]
                    row['buildExit'], _, row['buildSeconds'] = helper.execute(argv, directory, env, directory/'build.log', 60)
                    need(row['buildExit'] == 0, 'Build failed: '+profile+'/'+mode+'/'+variant)
                    deps = json.loads((directory/'deps.json').read_text(encoding='utf-8-sig'))['Data']['Includes']
                    for item in deps:
                        path = Path(item).resolve()
                        data = path.read_bytes()
                        if path.is_relative_to(snapshot):
                            name = path.relative_to(snapshot).as_posix()
                            need(name in source and source[name] == data, 'Uncaptured local dependency: '+name)
                        else:
                            need(not path.is_relative_to(ROOT), 'Compiler escaped frozen snapshot: '+str(path))
                            actual = sha(data)
                            need(pins.get(str(path).lower()) == actual, 'Dependency missing or differs from existing tool identity: '+str(path))
                            observed_deps[str(path)] = actual
                    row['executableSha256'] = sha((directory/'test.exe').read_bytes())
                    row['exit'], text, row['runSeconds'] = helper.execute([directory/'test.exe'], directory, env, directory/'run.log', 35)
                    summaries = re.findall(r'^SUMMARY (\{.*\})\r?$', text, re.M)
                    need(len(summaries) == 1, 'No unique pre-exit resource summary: '+variant)
                    row['summary'] = summary = json.loads(summaries[0])
                    row['executionPaths'] = {key: str(directory/name) for key, name in [('cpp', 'test.cpp'), ('executable', 'test.exe'), ('buildLog', 'build.log'), ('runLog', 'run.log'), ('dependencies', 'deps.json')]}
                    row['buildLogSha256'] = sha((directory/'build.log').read_bytes())
                    row['runLogSha256'] = sha((directory/'run.log').read_bytes())
                    row['dependenciesSha256'] = sha((directory/'deps.json').read_bytes())
                    row['compiledFunctions'] = {name: sha((mutate(part['text'], variant) if name == NAMES[0] and variant not in ('current','historical') else part['text']).encode('utf-8')) for name, part in parts.items()}
                    row['failedAssertions'] = [line for line in text.splitlines() if line.startswith('FAIL ')]
                    if variant == 'current':
                        row['accepted'] = row['exit'] == 0 and summary['failures'] == 0 and summary['ownedBeforeExit'] == 0 and summary['orphanOwners'] == 0
                    else:
                        expected_label = 'unknown wait never promises worker availability' if variant == 'wait-failed-treated-alive' else 'failed close retains exact old owners without create'
                        row['requiredNegativeLabel'] = expected_label
                        row['accepted'] = row['exit'] == 1 and summary['failures'] > 0 and any(expected_label in line for line in row['failedAssertions']) and summary['ownedBeforeExit'] == 0
                    publish()
                    need(row['accepted'], 'Unexpected result: '+profile+'/'+mode+'/'+variant)
                    print(profile+'/'+mode+'/'+variant+': '+('EXPECTED_FAIL' if variant != 'current' else 'PASS'), flush=True)
        receipt['observedNativeDependencies'] = [{'path': p, 'sha256': h} for p, h in sorted(observed_deps.items())]
        receipt['primaryToolsAfter'] = helper.primary_tools()[1]
        need(receipt['primaryToolsBefore'] == receipt['primaryToolsAfter'], 'Primary tools drifted during execution')
        receipt['authorityFilesAfterMatched'] = True
        for path, pin in pins.items():
            need(sha(Path(path).read_bytes()) == pin, 'Declared tool input changed during execution: '+path)
        for path, pin in observed_deps.items():
            need(sha(Path(path).read_bytes()) == pin, 'Native dependency changed during run: '+path)
        receipt['sourceDrift'] = [name for name, data in source.items() if (ROOT/name).read_bytes() != data]
        need(not [p for p in receipt['sourceDrift'] if p != 'native/main.cpp'], 'Fixture/tool/header source drift')
        for path, pin in external_inputs:
            need(sha(path.read_bytes()) == pin, 'External approved identity/closure changed during run')
        receipt['sourceCurrent'] = not receipt['sourceDrift']
        if runtime_pins is not None:
            need(receipt['sourceCurrent'], 'Registered source changed during validation')
            for relative, pin in runtime_pins.items():
                need(sha((ROOT/relative).read_bytes()) == pin, 'Runtime source closure changed during execution: '+relative)
        receipt['status'] = 'HISTORICAL_EXPECTED_FAIL' if args.historical_only else 'INTERMEDIATE_TARGETED_PASS'
        receipt['targetedAcceptance'] = not args.historical_only
        receipt['wholeLifecycleAcceptance'] = False
        receipt['processExitUsedAsCleanup'] = False
        receipt['parentLifecycleAcceptance'] = 'INCOMPLETE'
        if runtime_pins is not None:
            receipt['status'] = 'PASS'
            receipt['sourceFreeze'] = 'explicit approved whole-source manifest plus fresh runtime closure'
            receipt['remaining'] = [v for v in receipt['remaining'] if not v.startswith('parent final whole-source')]
        receipt['pass'] = receipt['status'] == 'PASS'
        publish()
        print('Evidence: '+str(run/'REPORT.json'), flush=True)
        return 0
    except Exception as error:
        receipt.update(status='FAIL', error=repr(error))
        publish()
        print('FAIL '+str(error)+'\nEvidence: '+str(run/'REPORT.json'), file=sys.stderr, flush=True)
        return 1


if __name__ == '__main__':
    sys.exit(main())


