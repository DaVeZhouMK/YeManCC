"""Offline production-consumer replay. No real HID terminal is ever invoked.

ReplayCore compiles the actual API/orchestration/pinned functions, replacing only
WindowsHidTransport. ReplayConsumer compiles the actual setter sources with a
scratch-only compiled fixture pin. The shipping setter is separately checked to
reject that fixture and may only execute unsupported (pre-open) capability tests.
"""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
CLASS = 'HandheldCompanion.Devices.OneXPlayerX1Mini'
BLOCKED = {'HandheldCompanion', 'Shared', 'PresentationCore', 'PresentationFramework',
           'WindowsBase', 'Microsoft.Windows.SDK.NET', 'WinRT.Runtime', 'HidLibrary'}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--scratch', required=True)
    args = parser.parse_args()
    scratch = pathlib.Path(args.scratch).resolve()
    build = scratch / 'build'
    core = build / 'replay-core/bin/ReplayCore/release/YeManLightDeviceCore.dll'
    consumer = build / 'replay-consumer/bin/ReplayConsumer/release_win-x64/LightSetterDeviceCoreConsumerReplay.dll'
    shipping_core = build / 'core/bin/YeManLightDeviceCore/release/YeManLightDeviceCore.dll'
    shipping = build / 'setter-core-only/bin/YeManLightSetter/release_win-x64/YeManLightSetter.dll'
    core_hash = hashlib.sha256(core.read_bytes()).hexdigest()
    shipping_hash = hashlib.sha256(shipping_core.read_bytes()).hexdigest()
    golden = json.loads((HERE / 'golden-v1-reports.json').read_text(encoding='utf-8-sig'))
    cases = []
    checks = 0
    outputs = scratch / 'replay'
    outputs.mkdir(exist_ok=True)

    def check(condition, message):
        nonlocal checks
        if not condition:
            raise AssertionError(message)
        checks += 1

    def run(name, action, *, mode='normal', extra=(), expected_note='ok', expected_reports=None,
            shipping_setter=False, component=None, expected_error=None, device_class=CLASS):
        log = outputs / (name + '.jsonl')
        if log.exists():
            log.unlink()
        env = os.environ.copy()
        env['YMCC_LIGHT_REPLAY_LOG'] = str(log)
        env['YMCC_LIGHT_REPLAY_MODE'] = mode
        command = ['dotnet', str(shipping if shipping_setter else consumer),
                   '--device-core-assembly', str(component or (shipping_core if shipping_setter else core)),
                   '--expected-device-class', device_class, '--action', action, *extra]
        result = subprocess.run(command, env=env, capture_output=True, text=True, encoding='utf-8', timeout=15)
        receipt = json.loads(result.stdout)
        events = [json.loads(line) for line in log.read_text(encoding='utf-8').splitlines()] if log.exists() else []
        observed = [e['hex'] for e in events if e['eventName'] == 'write']
        if expected_error:
            check(result.returncode == 1 and receipt['ok'] is False, name + ' failure exit')
            check(expected_error in receipt['error'], name + ' error contract: ' + receipt.get('error', ''))
        else:
            check(receipt['ok'] is True, name + ' receipt')
            check(receipt['result']['note'] == expected_note, name + ' note')
            check(result.returncode == (0 if expected_note == 'ok' else 1), name + ' exit-code')
            check(receipt['result']['backend'] == 'ymcc-onex-hid-v1', name + ' actual dispatch')
            check(receipt['result']['hcLoaded'] is False, name + ' HC receipt')
            check(receipt['result']['released'] is True, name + ' release receipt')
            check(receipt['result']['physicalEffectVerified'] is False, name + ' no physical claim')
        if expected_reports is not None:
            check(observed == expected_reports, name + ' complete 65-byte golden reports')
        for event in events:
            check(not (set(event['loaded']) & BLOCKED), name + ' HC/UI/SDK dependency leak')
        # A successful open must release once even on disconnect/write/disposing
        # exceptions. An Open failure owns no handle to release.
        opens = [e for e in events if e['eventName'] == 'open']
        releases = [e for e in events if e['eventName'] == 'release']
        if opens and mode != 'no-device':
            check(len(releases) == 1, name + ' exactly one actual release')
            check(events[-1]['eventName'] == 'release', name + ' release follows every write')
        else:
            check(not releases, name + ' no unowned release')
        row = {'name': name, 'exitCode': result.returncode, 'receipt': receipt,
               'events': [e['eventName'] for e in events], 'reports': observed}
        (outputs / (name + '.json')).write_text(json.dumps(row, indent=2), encoding='utf-8')
        cases.append(row)
        return row

    solid = golden['solid-10-20-30']
    rainbow = golden['rainbow']
    run('solid-plus-brightness', 'color', extra=['--level', 'solid', '--r', '10', '--g', '20', '--b', '30',
        '--r2', '99', '--g2', '88', '--b2', '77', '--speed', '23', '--brightness', '50'],
        expected_reports=[solid, golden['brightness-50']])
    run('rainbow', 'color', extra=['--level', 'RaInBoW'], expected_reports=[rainbow])
    run('default-color', 'color', expected_reports=[golden['solid-default-red']])
    run('clamped-color', 'color', extra=['--r', '-12', '--g', '999', '--b', '30'],
        expected_reports=[golden['solid-clamped-0-255-30']])
    for brightness in [-1, 0, 1, 33, 34, 50, 66, 67, 100, 255, 300]:
        run('brightness-' + str(brightness), 'brightness', extra=['--brightness', str(brightness)],
            expected_reports=[golden['brightness-' + str(max(0, min(255, brightness)))]])
    run('default-brightness', 'brightness', expected_reports=[golden['brightness-50']])
    run('false-color-still-brightness', 'color', mode='false:1',
        extra=['--r', '10', '--g', '20', '--b', '30', '--brightness', '50'],
        expected_note='setLedColor-false', expected_reports=[solid, golden['brightness-50']])
    run('false-brightness-after-color', 'color', mode='false:2',
        extra=['--r', '10', '--g', '20', '--b', '30', '--brightness', '50'],
        expected_note='setLedBrightness-false', expected_reports=[solid, golden['brightness-50']])
    run('false-brightness', 'brightness', mode='false:1',
        expected_note='setLedBrightness-false', expected_reports=[golden['brightness-50']])
    # Carry an explicit legacy path that must not be loaded on write failure.
    run('throw-no-legacy-retry', 'color', mode='throw:1', extra=['--hc-assembly', str(scratch / 'never-load-HC.dll')],
        expected_error='fixture write failure', expected_reports=[golden['solid-default-red']])
    run('throw-second-write-releases', 'color', mode='throw:2', extra=['--brightness', '50'],
        expected_error='fixture write failure', expected_reports=[golden['solid-default-red'], golden['brightness-50']])
    run('disconnected-release', 'brightness', mode='disconnected',
        expected_error='disconnected', expected_reports=[])
    run('missing-device', 'brightness', mode='no-device', expected_error='fixture HID endpoint missing', expected_reports=[])
    run('release-error-fails', 'brightness', mode='release-throw',
        expected_error='fixture release failure', expected_reports=[golden['brightness-50']])
    for action in ['preset', 'status', 'off']:
        run('unsupported-' + action, action, expected_error='explicit --hc-assembly legacy adapter required', expected_reports=[])
    run('unsupported-effect', 'color', extra=['--level', 'breathing'],
        expected_error='explicit --hc-assembly legacy adapter required', expected_reports=[])
    run('unknown-class', 'brightness', device_class='HandheldCompanion.Devices.OneXPlayerX1',
        expected_error='explicit --hc-assembly legacy adapter required', expected_reports=[])
    run('wrong-caller-hash', 'brightness', extra=['--device-core-sha256', '0' * 64],
        expected_error='not an allowed pin', expected_reports=[])
    run('matching-caller-hash', 'brightness', extra=['--device-core-sha256', core_hash],
        expected_reports=[golden['brightness-50']])
    altered = scratch / 'tampered-lighting-core.dll'
    altered.write_bytes(core.read_bytes() + b'\x00')
    run('tampered-core', 'brightness', component=altered,
        expected_error='assembly SHA-256 mismatch', expected_reports=[])
    # Uncovered requests may reach legacy only through explicit HC arguments.
    run('unsupported-explicit-legacy', 'status', extra=['--hc-assembly', str(scratch / 'absent-legacy.dll')],
        expected_error='HandheldCompanion.dll not found', expected_reports=[])
    run('approved-derivative-profile-missing-assembly', 'status', extra=['--hc-assembly', str(scratch / 'absent-legacy.dll'),
        '--hc-profile', 'ymcc-device-core-v1'], expected_error='HandheldCompanion.dll not found', expected_reports=[])
    run('unknown-profile', 'status', extra=['--hc-assembly', str(scratch / 'absent-legacy.dll'),
        '--hc-profile', 'self-declared-manifest'], expected_error='unknown HC runtime profile', expected_reports=[])
    # Shipping trust root cannot be expanded by a caller naming the actual
    # fixture hash. These shipping executions all fail BEFORE the real terminal.
    run('shipping-rejects-replay', 'brightness', shipping_setter=True, component=core,
        extra=['--device-core-sha256', core_hash], expected_error='not an allowed pin', expected_reports=[])
    run('shipping-rejects-replay-no-caller-hash', 'brightness', shipping_setter=True, component=core,
        expected_error='assembly SHA-256 mismatch', expected_reports=[])
    run('shipping-pin-unsupported-preopen', 'preset', shipping_setter=True,
        extra=['--device-core-sha256', shipping_hash],
        expected_error='explicit --hc-assembly legacy adapter required', expected_reports=[])
    for config in [consumer.with_suffix('.runtimeconfig.json'), shipping.with_suffix('.runtimeconfig.json')]:
        runtime = json.loads(config.read_text(encoding='utf-8'))['runtimeOptions']
        check(runtime['framework']['name'] == 'Microsoft.NETCore.App' and 'frameworks' not in runtime,
              'BCL-only consumer framework: ' + str(config))
    deps = json.loads(shipping_core.with_suffix('.deps.json').read_text(encoding='utf-8'))
    check(list(deps['libraries']) == ['YeManLightDeviceCore/1.0.0'], 'no runtime NuGet/HC/UI dependencies')
    summary = {'ok': True, 'checks': checks, 'cases': len(cases), 'hardwareIo': False,
               'physicalEffectVerified': False, 'sourceFunctionReuse': 7,
               'shippingCoreSha256': shipping_hash, 'fixtureCoreSha256': core_hash,
               'shippingRejectsFixture': True, 'fixturePinCompiledOnlyInScratch': True,
               'casesDetail': cases}
    (scratch / 'replay-report.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in summary.items() if k != 'casesDetail'}, indent=2))


if __name__ == '__main__':
    main()
