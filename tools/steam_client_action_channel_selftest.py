"""Prepare the directed Steam action fixture; never compile or execute it.

Usage: python steam_client_action_channel_selftest.py --source-root REPO --out NEW_DIR
       [--scope all|auth-successor|cross-process|native-composition|native-presence|native-source] (default: all)
Only original include/pragma-once lines are removed. Full product bodies remain.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re


def digest(data):
    return hashlib.sha256(data).hexdigest()


def strip_directives(raw):
    # Same extraction as the original prepare_action_fixture.py template.
    return b''.join(line for line in raw.splitlines(keepends=True)
                    if not re.match(rb'^\s*#\s*(?:include\b|pragma\s+once\b)', line))


def write_new(path, raw):
    with path.open('xb') as stream:
        stream.write(raw)
        stream.flush()
        os.fsync(stream.fileno())


def prepare_cross_process(source_root, out, here, expect_channel=None, expect_producer=None):
    generator_file = Path(__file__).resolve()
    generator_bytes = generator_file.read_bytes()
    template = here / 'steam_client_action_channel_fixtures' / 'channel_process.cpp'
    raw = template.read_bytes()
    modes = ['normal', 'exception-child-return', 'repeated-roundtrip', 'queued-cancel',
             'queued-expiry', 'native-revoked', 'queue-reject', 'dispatch-throws',
             'dispatch-false', 'ambiguous-provider', 'wrong-client-image', 'worker-image',
             'forged-host-window', 'ack-timeout-cancel', 'ack-timeout-submitted', 'submit-throws']
    inputs = []
    for name in ['steam_client_action_channel.h', 'steam_client_action_producer.h',
                 'steam_source_owner.h',
                 'steam_information_client.h', 'steam_information_producer.h',
                 'steam_installation_facts.h', 'process_inventory.h']:
        file = source_root / 'native' / name
        body = file.read_bytes()
        expected = expect_channel if name == 'steam_client_action_channel.h' else expect_producer if name == 'steam_client_action_producer.h' else None
        if expected and digest(body).lower() != expected.lower():
            raise ValueError('explicit source pin mismatch; no output created')
        inputs.append({'path': str(file), 'bytes': len(body), 'sha256': digest(body)})
    manifest = {'schemaVersion': 1, 'status': 'PREPARED_NOT_COMPILED_NOT_RUN',
                'scope': 'cross-process', 'cases': modes, 'canonicalId': None,
                'newCanonicalIds': 0, 'newMinimum': 0, 'sourcePins': inputs,
                'template': {'path': str(template), 'bytes': len(raw), 'sha256': digest(raw)},
                'generator': {'path': str(generator_file), 'bytes': len(generator_bytes),
                              'sha256': digest(generator_bytes)},
                'sourceRoot': str(source_root), 'compileExecuted': False,
                'fixtureExecuted': False, 'actualPassedThisRun': 0,
                'selector': ['--cross-process', '<new owned output directory>', '<optional exact case>'],
                'buildRecipe': ['cl.exe', '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/MT', '/O2',
                                '/bigobj', '/DNOMINMAX', '/DUNICODE', '/D_UNICODE',
                                '/I' + str(source_root / 'native'),
                                '/I' + str(source_root / 'deps/json'),
                                'channel_process.cpp', '/Fo:s/f.obj', '/Fe:s/f.exe',
                                '/link', 'advapi32.lib', 'user32.lib', 'shell32.lib',
                                'bcrypt.lib', 'ws2_32.lib', 'winhttp.lib'],
                'outputContract': 'Run f.exe from output/s; the fresh case directory must be a direct child of output. Inert PE copies stay there.',
                'limits': ['Full actual public client/producer/Peer/ancestry/inventory code is included, with only actual kernel PID query observation.',
                           'Creates self-owned fixture processes, hidden windows and a private desktop; never switches visible desktop or executes product/Steam effects.',
                           'Native currentness and final action are controlled ports; main/Host typed wiring and real power remain separate.',
                           'Same Windows account/session/integrity only; no installed ACL/UAC/cross-integrity or physical Steam proof.',
                           'Default cross-process case source is a plan, never inferred PASS from earlier epoch subsets.']}
    for item in inputs:
        current = Path(item['path']).read_bytes()
        if len(current) != item['bytes'] or digest(current) != item['sha256']:
            raise ValueError('source changed before output creation')
    if template.read_bytes() != raw or generator_file.read_bytes() != generator_bytes:
        raise ValueError('generator/template changed before output creation')
    out.mkdir(parents=True, exist_ok=False)
    (out / 's').mkdir()
    write_new(out / 'channel_process.cpp', raw)
    write_new(out / 'expectedNames.json', (json.dumps(modes, indent=2) + '\n').encode())
    write_new(out / 'manifest.json', (json.dumps(manifest, ensure_ascii=False, indent=2) + '\n').encode())
    print(json.dumps({'status': manifest['status'], 'scope': 'cross-process', 'cases': modes,
                      'output': str(out), 'compileExecuted': False, 'fixtureExecuted': False}))


def prepare_native_composition(source_root, out, here, expect_channel=None, expect_producer=None,
                               expect_main=None, expect_ui=None, expect_host=None, scope='native-composition'):
    # Explicit preparation only. The fixed helper extracts the real product
    # bodies; the fixture keeps the shell and live observer as declared seams.
    import subprocess
    import sys
    generator_file = Path(__file__).resolve()
    helper = here / 'steam_client_action_channel_fixtures' / 'prepare_composition.py'
    template = here / 'steam_client_action_channel_fixtures' / 'channel_process.cpp'
    adapter = here / 'steam_client_action_channel_fixtures' / 'native_composition_adapter.inc'
    resources = here / 'steam_client_action_channel_fixtures' / 'native_resource_observation.inc'
    owned_inputs = {file: file.read_bytes() for file in [generator_file, helper, template, adapter, resources]}
    def describe_source(file, body):
        return {'path': str(file.resolve()), 'bytes': len(body), 'sha256': digest(body)}
    source_inputs = {}
    for name in ['main.cpp', 'custom-steam-library/workspace_host.cpp', 'steam_ui_mode.h',
                 'steam_session_observer.h', 'sleep_power_state.h', 'sleep_power_dispatch.h', 'sleep_cancel_ready.h',
                 'steam_client_action_channel.h', 'steam_client_action_producer.h',
                 'steam_source_owner.h', 'steam_command_coordinator.h',
                 'steam_information_client.h', 'steam_information_producer.h',
                 'steam_installation_facts.h', 'process_inventory.h']:
        file = source_root / 'native' / name
        source_inputs[file] = file.read_bytes()
    expected = {'steam_client_action_channel.h': expect_channel, 'steam_client_action_producer.h': expect_producer,
                'main.cpp': expect_main, 'steam_ui_mode.h': expect_ui,
                'custom-steam-library/workspace_host.cpp': expect_host}
    for name, wanted in expected.items():
        if wanted and digest(source_inputs[source_root / 'native' / name]).lower() != wanted.lower():
            raise ValueError('explicit composition source pin mismatch; no output created')
    match = re.search(rb'#ifdef ACTION_NATIVE_COMPOSITION\r?\n\s*const std::vector<std::wstring> modes=\{([^}]+)\};',
                      owned_inputs[template])
    if not match:
        raise ValueError('native composition case selector is missing')
    cases = [name.decode() for name in re.findall(rb'L"([a-z0-9-]+)"', match.group(1))]
    if not cases or len(cases) != len(set(cases)):
        raise ValueError('empty or duplicate native composition selection')
    template_output = owned_inputs[template]
    selection_projection = None
    if scope in ('native-presence', 'native-source'):
        prefix = 'native-compose-presence-' if scope == 'native-presence' else 'native-compose-source-'
        selected = [name for name in cases if name.startswith(prefix)]
        if not selected or len(selected) >= len(cases):
            raise ValueError('composition subset must be nonempty and exclude the other cases')
        start, end = match.span(1)
        replacement = ','.join('L"' + name + '"' for name in selected).encode()
        selection_projection = {'kind': 'EXACT_NATIVE_SELECTOR_LIST_SUBSET_ONLY',
            'sourceStartByte': start, 'sourceEndByteExclusive': end,
            'originalHex': template_output[start:end].hex(), 'replacementHex': replacement.hex(),
            'originalCases': cases, 'selectedCases': selected,
            'sourcePin': describe_source(template, owned_inputs[template])}
        template_output = template_output[:start] + replacement + template_output[end:]
        cases = selected
    for file, body in {**owned_inputs, **source_inputs}.items():
        if file.read_bytes() != body:
            raise ValueError('composition input changed before preparation')
    command = [sys.executable, '-B', str(helper), '--source-root', str(source_root), '--out', str(out)]
    for role, name in [('main', 'main.cpp'), ('ui', 'steam_ui_mode.h'),
                       ('host', 'custom-steam-library/workspace_host.cpp')]:
        command.extend(['--expect-' + role, digest(source_inputs[source_root / 'native' / name])])
    prepared = subprocess.run(command, capture_output=True, timeout=30)
    if prepared.returncode != 0:
        raise ValueError('composition extraction refused: ' + prepared.stderr.decode('utf-8', errors='replace')[:2048])
    extracted_file = out / 'manifest.json'
    extracted = json.loads(extracted_file.read_bytes())
    if extracted.get('status') != 'PREPARED_NOT_COMPILED_NOT_RUN' or extracted.get('compiled') is not False or extracted.get('businessExecuted') is not False:
        raise ValueError('composition extraction manifest is incomplete')
    for output in extracted['outputs']:
        expected_output = output['pin']
        file = out / output['name']
        body = file.read_bytes()
        if str(file.resolve()) != expected_output['path'] or len(body) != expected_output['bytes'] or digest(body) != expected_output['sha256']:
            raise ValueError('extracted output pin mismatch')
    for file, body in {**owned_inputs, **source_inputs}.items():
        if file.read_bytes() != body:
            raise ValueError('composition input changed during preparation; final manifest not written')
    (out / 's').mkdir(exist_ok=False)
    write_new(out / 'channel_process.cpp', template_output)
    write_new(out / 'native_composition_adapter.inc', owned_inputs[adapter])
    write_new(out / 'native_resource_observation.inc', owned_inputs[resources])
    write_new(out / 'expectedNames.json', (json.dumps(cases, indent=2) + '\n').encode())
    def describe(file, body):
        return {'path': str(file.resolve()), 'bytes': len(body), 'sha256': digest(body)}
    manifest = {'schemaVersion': 1, 'status': 'PREPARED_NATIVE_COMPOSITION_NOT_COMPILED_NOT_RUN',
        'scope': scope, 'cases': cases, 'caseCount': len(cases),
        'selectionProjection': selection_projection,
        'sourceRoot': str(source_root), 'sourcePins': [describe(p, b) for p, b in source_inputs.items()],
        'ownedInputs': [describe(p, b) for p, b in owned_inputs.items()],
        'extractionManifest': describe(extracted_file, extracted_file.read_bytes()),
        'outputs': [describe(out / name, (out / name).read_bytes()) for name in
                    ['channel_process.cpp', 'native_composition_adapter.inc', 'native_resource_observation.inc', 'expectedNames.json']],
        'helperArguments': command, 'helperExitCode': prepared.returncode,
        'canonicalId': None, 'newCanonicalIds': 0, 'newMinimum': 0,
        'compileExecuted': False, 'fixtureExecuted': False, 'actualPassedThisRun': 0,
        'selector': ['--native-composition', '<new owned output directory>', '<optional exact case>'],
        'buildRecipe': ['cl.exe', '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/MT', '/O2', '/bigobj',
                        '/DNOMINMAX', '/DUNICODE', '/D_UNICODE', '/DACTION_NATIVE_COMPOSITION',
                        '/I' + str(source_root / 'native'), '/I' + str(source_root / 'deps/json'),
                        'channel_process.cpp', '/Fo:s/f.obj', '/Fe:s/f.exe', '/link',
                        'advapi32.lib', 'user32.lib', 'shell32.lib', 'bcrypt.lib', 'ws2_32.lib', 'winhttp.lib'],
        'limits': ['Actual Host method, transport, Native handler, shared power reader and typed dispatcher bodies run only if a separate owned runner executes the prepared fixture.',
                   'The live Steam observer/facts source is an adapter restricted to a self-owned inert target. It does not prove installed registry/UI/account observation.',
                   'The real process verifier checks the self-owned inert Steam.exe. ShellExecute remains a counted inert leaf and never starts or shuts down real Steam.',
                   'Native lifecycle state is driven inside the fixture; this is not real Windows power delivery, complete main/Host TU, matched package or physical acceptance.',
                   'Claim observations bind the live action mapping, immutable full request/nonce and exact accepted/claimed field address. This is not a multi-request scheduler proof.',
                   'Actual request/typed resource observations return to the original held-provider baseline and verify three owned target handle returns; this is not all Windows/COM/SDK resource quiescence.',
                   'No old default sixteen, standalone Native23/Host22, full suite, deployment, global CPU or all-resource-quiescence proof.']}
    write_new(out / 'native-composition-manifest.json', (json.dumps(manifest, ensure_ascii=False, indent=2) + '\n').encode())
    print(json.dumps({'status': manifest['status'], 'scope': manifest['scope'], 'output': str(out),
                      'manifest': str(out / 'native-composition-manifest.json'), 'cases': cases,
                      'compileExecuted': False, 'fixtureExecuted': False}))


def main():
    here = Path(__file__).resolve().parent
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source-root', type=Path, default=here.parent)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--scope', choices=['all', 'auth-successor', 'cross-process', 'native-composition', 'native-presence', 'native-source'], default='all')
    p.add_argument('--expect-channel')
    p.add_argument('--expect-producer')
    p.add_argument('--expect-main')
    p.add_argument('--expect-ui')
    p.add_argument('--expect-host')
    args = p.parse_args()
    source_root = args.source_root.resolve(strict=True)
    out = args.out.resolve()
    if out == source_root or source_root in out.parents:
        p.error('--out must be outside the source root')
    if out.exists():
        p.error('--out must not exist; sealed outputs are never overwritten')
    if args.scope in ('native-composition', 'native-presence', 'native-source'):
        prepare_native_composition(source_root, out, here, args.expect_channel, args.expect_producer,
                                   args.expect_main, args.expect_ui, args.expect_host, args.scope)
        return
    if args.expect_main or args.expect_ui or args.expect_host:
        p.error('composition source pins require a native composition scope')
    if args.scope == 'cross-process':
        prepare_cross_process(source_root, out, here, args.expect_channel, args.expect_producer)
        return
    template = here / 'steam_client_action_channel_fixtures' / 'action_fixture.cpp'
    outputs = {}
    sources = []
    for role, expected in [('channel', args.expect_channel), ('producer', args.expect_producer)]:
        src = (source_root / 'native' / ('steam_client_action_' + role + '.h')).resolve(strict=True)
        raw = src.read_bytes()
        if expected and digest(raw).lower() != expected.lower():
            p.error(role + ' source pin differs; no output created')
        sources.append({'role': role, 'path': str(src), 'bytes': len(raw), 'sha256': digest(raw)})
        outputs[role + '.original.h'] = raw
        outputs['action_' + role + '.inc'] = strip_directives(raw)
    channel = outputs['channel.original.h']
    has_power = all(x in channel for x in [b'powerGeneration', b'powerSequence', b'powerSerial'])
    if any(x in channel for x in [b'powerGeneration', b'powerSequence', b'powerSerial']) and not has_power:
        p.error('partial power expectation layout refused')
    for marker in [b'struct Pending final', b'inline LRESULT serve(']:
        if marker not in outputs['producer.original.h']:
            p.error('missing full product definition: ' + marker.decode())
    for marker in [b'inline bool matches(', b'inline bool valid(', b'inline Outcome shutdown(']:
        if marker not in channel:
            p.error('missing full channel definition: ' + marker.decode())
    has_cancel = b'struct CancelOnReturn' in channel
    has_proof = b'inline std::wstring proofPipeName(' in channel
    if has_proof and b'GetNamedPipeServerProcessId(' not in outputs['producer.original.h']:
        p.error('proof channel without producer server PID validation')
    outputs['action_config.h'] = ('#pragma once\n#define ACTION_HAS_POWER ' + str(int(has_power)) +
        '\n#define ACTION_HAS_CANCEL_ON_RETURN ' + str(int(has_cancel)) +
        '\n#define ACTION_HAS_PROOF ' + str(int(has_proof)) +
        '\n#define ACTION_AUTH_SUCCESSOR ' + str(int(args.scope == 'auth-successor')) + '\n').encode()
    outputs['action_fixture.cpp'] = template.read_bytes()
    case_names = re.findall(r'\{"([a-z0-9-]+)",\s*\[', outputs['action_fixture.cpp'].decode())
    if not case_names or len(set(case_names)) != len(case_names):
        p.error('empty or duplicate fixture case names')
    inactive = [] if has_power else ['power-generation-zero', 'power-expectation-matches']
    if not has_cancel:
        inactive += ['client-success-cancels-on-return']
    if args.scope == 'auth-successor':
        inactive += ['malformed-command', 'malformed-size', 'reserved-version-envelope',
            'deadline-boundaries', 'nonce-zero', 'power-generation-zero', 'power-expectation-matches',
            'forged-caller-pid', 'forged-caller-birth', 'worker-path-refused', 'peer-ancestry-refused',
            'inventory-incomplete']
    if not has_proof:
        inactive += ['forged-pipe-server-pid', 'pipe-open-fail', 'proof-replay-refused', 'proof-kernel-current-pid']
    expected_names = [name for name in case_names if name not in inactive]
    outputs['expectedNames.json'] = (json.dumps(expected_names, indent=2) + '\n').encode()
    generator = Path(__file__).resolve()
    generator_bytes = generator.read_bytes()
    manifest = {
        'schemaVersion': 1, 'status': 'PREPARED_NOT_COMPILED_NOT_RUN',
        'canonicalId': None, 'newCanonicalIds': 0, 'newMinimum': 0,
        'sourceRoot': str(source_root), 'sourcePins': sources,
        'generator': {'path': str(generator), 'bytes': len(generator_bytes), 'sha256': digest(generator_bytes)},
        'template': {'path': str(template), 'bytes': len(outputs['action_fixture.cpp']),
                     'sha256': digest(outputs['action_fixture.cpp'])},
        'hasPowerExpectation': has_power, 'hasCancelOnReturn': has_cancel,
        'hasProofPipe': has_proof, 'scope': args.scope,
        'selector': ['--action-channel', 'optional exact case name'],
        'cases': expected_names, 'expectedNames': expected_names, 'caseCount': len(expected_names),
        'excludedForThisEpoch': inactive,
        'outputs': [{'path': name, 'bytes': len(raw), 'sha256': digest(raw)} for name, raw in outputs.items()],
        'productExecuted': False, 'defaultExecuted': False, 'compileExecuted': False,
        'fixtureExecuted': False, 'casesStarted': 0, 'casesPassed': 0, 'checks': 0,
        'transform': 'Remove only include/pragma-once lines; retain full channel and producer bodies byte for byte.',
        'limits': [
            'Peer/callerOwned/Ancestry/processInventory/window/time are inert leaves; actual identity and ancestry policy are not executed.',
            'Native Current is injected; real 48-byte power capture/tuple validation/main/Host typed wiring are outside this fixture.',
            'No real YMCC/Steam/task/shell/device/child/deployment/sleep action.',
            'Real owned Win32 mapping/event/interlocked and RNG are used only if a separate runner later executes the C++ fixture.',
            'Proof handles use real local named pipes. The PID leaf first queries the kernel, then maps this process to synthetic Peer callerId; foreign-server rejection injects only that identity projection.',
            'proof-kernel-current-pid bypasses the PID mapping and checks actual kernel server PID against actual current process PID; this is not a cross-process Host ancestry test.',
            'No ConnectNamedPipe, pipe read/write, OVERLAPPED operation, or new poller is started by the fixture.',
            'Deterministic boundaries do not prove all scheduler races, cross-process ACLs, malicious handle duplication, or actual exit.',
            'Submitted is diagnostic submission only; Unknown never authorizes automatic replay.',
        ],
        'buildRecipe': ['cl.exe', '/nologo', '/std:c++17', '/EHsc', '/W4', '/WX', '/DUNICODE',
                        '/D_UNICODE', '/Fe:action_fixture.exe', 'action_fixture.cpp', '/link', '/SUBSYSTEM:CONSOLE', 'bcrypt.lib'],
    }
    # Refuse a changed input epoch before creating any output directory.
    for pin in sources:
        current = Path(pin['path']).read_bytes()
        if len(current) != pin['bytes'] or digest(current) != pin['sha256']:
            p.error('source changed during preparation; no output created')
    if template.read_bytes() != outputs['action_fixture.cpp'] or generator.read_bytes() != generator_bytes:
        p.error('generator/template changed during preparation; no output created')
    manifest_bytes = (json.dumps(manifest, ensure_ascii=False, indent=2) + '\n').encode('utf-8')
    out.mkdir(parents=True, exist_ok=False)
    for name, raw in outputs.items():
        write_new(out / name, raw)
    # The manifest is the final durable file; partial preparations remain unsealed.
    write_new(out / 'manifest.json', manifest_bytes)
    print(json.dumps({'status': manifest['status'], 'output': str(out), 'cases': expected_names,
                      'sourcePins': sources, 'compileExecuted': False, 'fixtureExecuted': False}))


if __name__ == '__main__':
    main()
