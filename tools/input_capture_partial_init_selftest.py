#!/usr/bin/env python3
"""Whole actual Shutdown/RetireResources/checked-close functions on memory ports.

Current source is extracted verbatim. Full Init is audited, never executed.
Frozen baseline and explicitly named mutants are diagnostic negative routes;
their successful reproduction never becomes a passing production receipt.
No installed application, real capture/Host, DSU socket, sensor, device, driver,
service, worker thread or sleep is executed. Process exit is never cleanup proof.
"""
from __future__ import annotations
import argparse
import copy
import datetime as dt
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
RUNNER = 'tools/input_capture_partial_init_selftest.py'
TEMPLATE = 'tools/fixtures/input_capture_timer_retirement/partial_init_fixture.cpp.in'
RETIREMENT = 'tools/fixtures/input_capture_timer_retirement/partial_retirement_fixture.cpp.in'
COMPLETION = 'native/input_capture_completion.h'
BASELINE = 'tools/fixtures/input_capture_timer_retirement/partial_init_baseline.json'
BASELINE_SHA256 = '8ddb2bacc55721256744c81d4dfdb8e02d80c75d738b04894be7140088605529'
ADAPTER = 'tools/input_timer_evidence_adapter_selftest.py'
AUTH = 'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'
SOURCES = ('native/main.cpp', 'deps/json/json.hpp', RUNNER, TEMPLATE, RETIREMENT, COMPLETION, BASELINE, ADAPTER, AUTH)
VARIANTS = ('current', 'baseline', 'ready-guard-mutant', 'skip-gm-delete-mutant',
            'skip-gm-free-mutant', 'drop-sink-on-failure-mutant',
            'unsettled-sensor-unloads-gm-mutant', 'unchecked-sensor-close-mutant',
            'unchecked-oem-close-mutant', 'unchecked-onex-close-mutant',
            'unchecked-timer-close-mutant', 'unchecked-notification-close-mutant',
            'failed-free-nulls-owner-mutant', 'legacy-emergency-mutant',
            'gm-without-dll-loses-motion-debt-mutant')
ACTUAL_HELPERS = {
    'retireResources': 'inputCaptureRetireResources',
    'clearExitConfiguration': 'inputCaptureClearExitConfiguration',
    'sensorReap': 'inputCaptureSensorThreadReap',
    'sensorShutdown': 'inputCaptureSensorThreadShutdown',
    'oemTakeCompletion': 'oemHidReadTakeCompletion',
    'oemFinalizeClose': 'oemHidReadFinalizeClose',
    'oemServiceClosing': 'oemHidReadServiceClosing',
    'oemRequestClose': 'oemHidReadRequestClose',
    'onexFinalizeClose': 'onexBackFinalizeClose',
    'onexSettleWrite': 'onexBackSettleWrite',
    'onexRetireStuck': 'onexBackRetireStuck',
    'onexRetireReadStuck': 'onexBackRetireReadStuck',
    'onexServiceWrite': 'onexBackServiceWrite',
    'onexServiceRead': 'onexBackServiceRead',
    'emergencyCleanup': 'inputCaptureEmergencyCleanup',
}
RETIREMENT_CASES = (
    'all-debts', 'sta-timeout', 'sta-wait-failed', 'msi-provider', 'hc-provider',
    'running-provider', 'census', 'dsu', 'oem-pending', 'oem-observation-failed',
    'onex-read-pending', 'onex-write-pending', 'x2', 'timer-unproven',
    'timer-overflow', 'timer-resolution-failure', 'timer-close-failure',
    'sensor-thread-close-failure', 'sensor-wake-close-failure',
    'oem-device-close-failure', 'oem-event-close-failure',
    'onex-device-close-failure', 'onex-read-event-close-failure',
    'onex-write-event-close-failure', 'notification-close-failure',
    'free-library-failure', 'missing-delete-export', 'gamebar-com-unavailable',
    'gamebar-revoke-failure', 'subscription-revoke-retained',
    'subscription-token-with-sta-exited', 'metadata-is-not-destruction',
)
EMERGENCY_CASES = ('pending-children', 'missing-delete-export', 'free-library-failure',
                   'gm-without-dll', 'stop-onex-throws', 'stop-windows-throws',
                   'stop-oem-throws', 'stop-legion-throws', 'sensor-shutdown-throws',
                   'retirement-observation-throws', 'already-empty')
LEGACY_EMERGENCY_CASES = ('pending-children', 'missing-delete-export', 'free-library-failure')
sha = lambda b: hashlib.sha256(b).hexdigest()


def need(value, message):
    if not value:
        raise RuntimeError(message)


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


class SnapshotTools:
    """Local extraction/include gate; only compiler/mask utilities are shared.

    Other lifecycle fixtures are independently edited and are not dependencies
    of this consumer. Every imported utility still comes from the byte snapshot.
    """
    def __init__(self, helper):
        self.helper = helper
        self.ret = self

    def function(self, text, name):
        code = self.helper.mask_cpp(text)
        matches = list(re.finditer(r'^(?:\[\[[^\n]*?\]\]\s*)?static[^\n;{}]*\b' + re.escape(name) +
                                  r'\s*\([^;{}]*\)\s*(?:noexcept\s*)?\{', code, re.M))
        need(len(matches) == 1, 'Expected one whole actual definition ' + name)
        start, end, depth = matches[0].start(), matches[0].end(), 1
        while depth:
            need(end < len(code), 'Unclosed actual definition ' + name)
            depth += (code[end] == '{') - (code[end] == '}')
            end += 1
        fragment = text[start:end]
        return {'text': fragment, 'line': text.count('\n', 0, start) + 1, 'sha256': sha(fragment.encode())}

    @staticmethod
    def checked_local_includes(dependencies, snapshot, source):
        included = set()
        for item in dependencies['Data']['Includes']:
            path = Path(item).resolve()
            if path.is_relative_to(snapshot):
                name = path.relative_to(snapshot).as_posix()
                need(name in source and path.read_bytes() == source[name], 'Uncaptured or changed local include ' + name)
                included.add(name)
            else:
                need(not path.is_relative_to(ROOT), 'Compile read a live repository dependency ' + str(path))
        return sorted(included)


def load_identity(snapshot):
    spec = importlib.util.spec_from_file_location('partial_init_snapshot_tools', snapshot / ADAPTER)
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    need(Path(helper.__file__).resolve() == (snapshot / ADAPTER).resolve(), 'Compiler/mask utility escaped snapshot')
    return SnapshotTools(helper)


def extract(ret, text):
    names = (*ACTUAL_HELPERS.values(), 'inputCaptureShutdown', 'inputCaptureInit', 'inputCaptureLoadGamepadMotion',
             'inputCaptureSensorThreadEnsure', 'inputCaptureSensorThreadPost',
             'inputCaptureSensorThreadShutdown', 'inputHostResetExitFence', 'inputHostExitFenceRaised')
    parts = {name: ret.function(text, name) for name in names}
    init = parts['inputCaptureInit']['text']
    # Contiguous original signature/body through DSU's start-result log; only the
    # unexecuted tail is omitted, with one explicitly synthetic closing brace.
    match = re.search(r'if\s*\(dsu_start\(\)\)\s*appendNativeLifecycleLog\(.*?;', init, re.S)
    need(match is not None, 'Init-through-DSU boundary changed; review required')
    prefix = init[:match.end()]
    masked = ret.helper.mask_cpp(prefix)
    need(masked.count('{') - masked.count('}') == 1, 'Init prefix must end at outer function depth')
    prefix_part = {'text': prefix, 'line': parts['inputCaptureInit']['line'], 'sha256': sha(prefix.encode())}
    anchors = ['dsu_start()', 'inputCaptureLoadGamepadMotion();', 'inputCaptureSensorThreadEnsure()',
               'std::make_shared<InputCaptureSensorSink>()', 'inputCaptureSensorThreadPost(SensorThreadRequest::Start)',
               'g_inputCaptureReady = true;']
    positions = [init.index(s) for s in anchors]
    need(positions == sorted(positions), 'Init resource-acquisition ordering changed')
    loader = parts['inputCaptureLoadGamepadMotion']['text']
    need(re.search(r'g_gm\s*=\s*create\(\);\s*if\s*\(!g_gm\)\s*return;', loader), 'GM create-null DLL retention shape changed')
    ensure = parts['inputCaptureSensorThreadEnsure']['text']
    need(ensure.index('CreateEventW(') < ensure.index('CreateThread('), 'Sensor event/thread acquisition ordering changed')
    audit = {
        'execution': 'SOURCE_AUDIT_ONLY; full Init/GM loader/sensor functions NOT executed',
        'readyAssignmentLine': parts['inputCaptureInit']['line'] + init[:positions[-1]].count('\n'),
        'orderedPreReadySites': [{'source': 'native/main.cpp', 'line': parts['inputCaptureInit']['line'] + init[:p].count('\n'), 'site': s}
                                for s, p in zip(anchors[:-1], positions[:-1])],
        'resourcesPossibleBeforeReady': [
            'DSU starts before the remaining resource acquisition',
            'GM DLL and object; DLL alone is retained when Create returns null',
            'sensor wake event can remain when CreateThread fails',
            'sensor owner thread before sink allocation and Start request',
            'sink before Start request failure throws with ready still false'],
        'queueAndLogLimit': 'Init clears queue and truncates via a temporary ofstream before ready. Partial cases seed no queued samples or open capture log; normal-ready cases test their memory cleanup. Real file/queue leaves are not executed.',
        'sensorFailureLimit': 'Whole Shutdown and checked Reap execute on explicit memory Post/Wait/Close ports. STA apartment, mailbox, WinRT revoke/re-subscribe and real providers remain UNKNOWN.',
        'outOfScope': ['HidHide exemptions/cloak', 'config/crash-recovery side effects', 'other full-Init transitive owners', 'real DSU sockets or clients'],
        'functionIdentities': {n: {'line': p['line'], 'sha256': p['sha256']} for n, p in parts.items()},
    }
    selected = {'shutdown': parts['inputCaptureShutdown'], 'initPrefix': prefix_part,
                'resetFence': parts['inputHostResetExitFence'], 'readFence': parts['inputHostExitFenceRaised']}
    selected.update({key: parts[name] for key, name in ACTUAL_HELPERS.items()})
    # Keep actual resource layouts/constants; no mirrored debt enum or HID fields.
    types = []
    for name in ('OnexInitProfile', 'OnexCapDescriptor', 'OemHidReadSession', 'OemHidReadCompletion', 'OnexBackSession'):
        code = ret.helper.mask_cpp(text)
        match = re.search(r'^(?:struct|enum class)\s+' + name + r'[^;{\n]*\{', code, re.M)
        need(match is not None, 'Missing actual resource type ' + name)
        start, end, depth = match.start(), match.end(), 1
        while depth:
            need(end < len(code), 'Unclosed actual type ' + name)
            depth += (code[end] == '{') - (code[end] == '}')
            end += 1
        need(code[end] == ';', 'Actual type terminator changed ' + name)
        fragment = text[start:end + 1]
        types.append(fragment)
    for name in ('kOemHidReadDrainNoticeMs', 'kOnexBackWriteDrainLimitMs', 'kOnexBackReadDrainLimitMs'):
        matches = list(re.finditer(r'^static constexpr[^;]*\b' + name + r'\b[^;]*;', text, re.M))
        need(len(matches) == 1, 'Actual constant changed ' + name)
        types.append(matches[0].group())
    selected['resourceTypes'] = {'text': '\n'.join(types), 'line': 1,
                               'sha256': sha('\n'.join(types).encode())}
    audit['executedWholeHelpers'] = ACTUAL_HELPERS
    return selected, audit, parts


def variant_parts(current, frozen, variant, legacy_emergency=None):
    # Historical baseline is independently linked to its exact historical
    # memory dependency closure; no current helper is injected into that route.
    selected = copy.deepcopy(frozen['fragments'] if variant == 'baseline' else current)
    mutation = None
    if variant.endswith('-mutant'):
        key = 'retireResources'
        if variant == 'legacy-emergency-mutant':
            need(legacy_emergency is not None, 'legacy Emergency requires matched dependency closure')
            key = 'emergencyCleanup'
            old, new = selected[key]['text'], legacy_emergency['text']
        elif variant == 'gm-without-dll-loses-motion-debt-mutant':
            old, new = 'if(g_gm&&!g_gmDll){debts|=Debt::Motion;return debts;}', '(void)g_gm; /* diagnostic lost orphan-object debt */'
        elif variant == 'ready-guard-mutant':
            key, old = 'shutdown', '    inputCaptureDrainQueuedSamples();'
            body = selected[key]['text']
            need(not re.search(r'if\s*\(!g_inputCaptureReady\)\s*return;', body), 'Use frozen baseline for original guard')
            nl = '\r\n' if '\r\n' in body else '\n'
            new = '    if (!g_inputCaptureReady) return;' + nl + old
        elif variant == 'skip-gm-delete-mutant':
            old, new = 'del(g_gm);', '(void)g_gm; /* diagnostic omitted delete */'
        elif variant == 'skip-gm-free-mutant':
            old, new = 'FreeLibrary(g_gmDll)', '(true /* diagnostic omitted library release */)'
        elif variant == 'drop-sink-on-failure-mutant':
            old = '    const auto dependencyDebts='
            new = '    g_inputCaptureSink.reset(); /* diagnostic premature destruction */\n' + old
        elif variant == 'unsettled-sensor-unloads-gm-mutant':
            old, new = 'if(!dependencyDebts){', 'if(true){ /* diagnostic ignores dependency debt */'
        elif variant == 'unchecked-sensor-close-mutant':
            key = 'sensorReap'
            old, new = 'if(!CloseHandle(g_sensorThread))return false;', '(void)CloseHandle(g_sensorThread);'
        elif variant == 'unchecked-oem-close-mutant':
            key = 'oemFinalizeClose'
            old, new = 'if(!CloseHandle(session.device))return;', '(void)CloseHandle(session.device);'
        elif variant == 'unchecked-onex-close-mutant':
            key = 'onexFinalizeClose'
            old, new = 'if(!CloseHandle(g_onexBack.device))return;', '(void)CloseHandle(g_onexBack.device);'
        elif variant == 'unchecked-timer-close-mutant':
            old = 'if(CloseHandle(handle))handle=nullptr;else debts|=Debt::Timer;'
            new = '(void)CloseHandle(handle);handle=nullptr;'
        elif variant == 'unchecked-notification-close-mutant':
            old = 'if(UnregisterDeviceNotification(g_onexX2DeviceNotify))g_onexX2DeviceNotify=nullptr;else debts|=Debt::OemIo;'
            new = '(void)UnregisterDeviceNotification(g_onexX2DeviceNotify);g_onexX2DeviceNotify=nullptr;'
        elif variant == 'failed-free-nulls-owner-mutant':
            old = 'if(!FreeLibrary(g_gmDll)){debts|=Debt::Motion;return debts;}g_gmDll=nullptr;'
            new = '(void)FreeLibrary(g_gmDll);g_gmDll=nullptr;'
        else:
            raise RuntimeError('Unreviewed mutation ' + variant)
        body = selected[key]['text']
        need(body.count(old) == 1, 'Reviewed mutation anchor changed: ' + old)
        selected[key]['text'] = body.replace(old, new, 1)
        selected[key]['sha256'] = sha(selected[key]['text'].encode())
        mutation = {'name': variant, 'function': key, 'before': old, 'after': new,
                    'diagnosticOnly': True, 'originalFunctionSha256': current[key]['sha256']}
    return selected, mutation


def render(template, selected, retirement, legacy=False, emergency_cases=EMERGENCY_CASES):
    bodies = selected['shutdown']['text'] + '\n' + selected.get('retireResources', {}).get('text', '')
    names = sorted(set(re.findall(r'\b(g_gm\w+|g_controllerGm\w+)\s*=\s*nullptr\s*;', bodies)) - {'g_gmDll'})
    need(len(names) >= 18, 'Expected full GM pointer cleanup surface')
    def fragment(name):
        part = selected[name]
        return '#line ' + str(part['line']) + ' "actual-main.cpp"\n' + part['text'] + '\n#line 1 "memory-fixture.cpp"\n'
    replacements = {
        '@GM_GLOBALS@': '\n'.join('static void* ' + n + '=nullptr;' for n in names),
        '@GM_SET@': '\n'.join('    ' + n + '=value?&functionToken:nullptr;' for n in names),
        '@GM_CLEAR_CHECK@': ' && '.join('!' + n for n in names),
        '@EXIT_FENCE_FUNCTIONS@': fragment('readFence') + fragment('resetFence'),
        '@SHUTDOWN_FUNCTION@': fragment('shutdown'),
        '@INIT_PREFIX@': fragment('initPrefix'),
    }
    if not legacy:
        replacements.update({
            '@ACTUAL_TYPES@': selected['resourceTypes']['text'],
            '@ACTUAL_RETIREMENT_FUNCTIONS@': ''.join(fragment(n) for n in ACTUAL_HELPERS),
            '@RETIREMENT_CASES@': retirement,
            '@EMERGENCY_DISPATCH@': '\n'.join('    cases.push_back(emergencyCase("' + n + '"));' for n in emergency_cases),
            '@RETIREMENT_DISPATCH@': '\n'.join('    cases.push_back(retirementCase("' + n + '"));' for n in RETIREMENT_CASES),
        })
    for marker, value in replacements.items():
        need(template.count(marker) == 1, 'Expected one fixture marker ' + marker)
        template = template.replace(marker, value)
    need(not re.search(r'@[A-Z_]+@', template), 'Unexpanded fixture marker')
    return template, names


def negative_observed(variant, cases):
    by_name = {c['name']: c for c in cases}
    partial = by_name['partial-sensor-start-failed']['observations'][0]
    guard_gap = (partial['sinksAlive'] == 1 and partial['sensorThreadsAlive'] == 1 and partial['dllRefsAlive'] == 1
                 and partial['gmObjectsAlive'] == 1 and partial['calls'].get('sensorShutdown', 0) == 0
                 and partial['calls'].get('gmDelete', 0) == 0 and partial['calls'].get('freeLibrary', 0) == 0)
    normal = by_name['normal-ready']['observations'][0]
    sensor = by_name['normal-sensor-shutdown-fails-then-settles']['observations'][0]
    expected = {'baseline': guard_gap, 'ready-guard-mutant': guard_gap,
                'skip-gm-delete-mutant': normal['gmObjectsAlive'] == 1 and normal['calls'].get('gmDelete', 0) == 0,
                'skip-gm-free-mutant': normal['dllRefsAlive'] == 1 and normal['calls'].get('freeLibrary', 0) == 0,
                'drop-sink-on-failure-mutant': sensor['sensorThreadsAlive'] == 1 and sensor['sinksAlive'] == 0}
    targeted = {
        'unsettled-sensor-unloads-gm-mutant': ('sta-timeout', 'gmObjectsAlive'),
        'unchecked-sensor-close-mutant': ('sensor-thread-close-failure', 'sensorThreadPointer'),
        'unchecked-oem-close-mutant': ('oem-device-close-failure', 'oemDevicePointer'),
        'unchecked-onex-close-mutant': ('onex-device-close-failure', 'onexDevicePointer'),
        'unchecked-timer-close-mutant': ('timer-close-failure', 'timerEventPointer'),
        'unchecked-notification-close-mutant': ('notification-close-failure', 'notificationPointer'),
        'failed-free-nulls-owner-mutant': ('free-library-failure', 'dllPointer'),
    }
    if variant in targeted:
        name, field = targeted[variant]
        row = by_name['retire-' + name]
        first = row['observations'][0]
        # Both actual liveness and the lost owner/specified early destruction
        # must be witnessed; an unrelated failing assertion is insufficient.
        if variant == 'unsettled-sensor-unloads-gm-mutant':
            expected[variant] = first['sensorThreadsAlive'] == 1 and first[field] == 0
        else:
            live_key = {'sensorThreadPointer': 'sensorThread', 'oemDevicePointer': 'oemDevice',
                        'onexDevicePointer': 'onexDevice', 'timerEventPointer': 'timerEvent',
                        'notificationPointer': 'notification', 'dllPointer': 'dll'}[field]
            expected[variant] = not first[field] and first['liveByKind'].get(live_key, 0) > 0
        expected[variant] = expected[variant] and not row['pass']
    if variant == 'legacy-emergency-mutant':
        pending = by_name['emergency-pending-children']['observations'][0]
        missing = by_name['emergency-missing-delete-export']['observations'][0]
        failed = by_name['emergency-free-library-failure']['observations'][0]
        expected[variant] = (pending['sensorThreadsAlive'] == 1 and pending['gmObjectsAlive'] == 0
                             and missing['gmObjectsAlive'] == 1 and not missing['gmPointer']
                             and failed['dllRefsAlive'] == 1 and not failed['dllPointer']
                             and all(not by_name['emergency-' + n]['pass'] for n in LEGACY_EMERGENCY_CASES))
    if variant == 'gm-without-dll-loses-motion-debt-mutant':
        orphan = by_name['emergency-gm-without-dll']
        expected[variant] = orphan['observations'][0]['gmObjectsAlive'] == 1 and orphan['finalDebt'] == 0 and not orphan['pass']
    return guard_gap, expected.get(variant, False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-root', required=True, type=Path)
    parser.add_argument('--variant', choices=VARIANTS, default='current')
    parser.add_argument('--main', type=Path, help='Explicit immutable main.cpp input; external input never accepts current production')
    parser.add_argument('--main-sha256', help='Expected SHA256 for --main; otherwise require sibling BASELINE-SOURCE.json')
    parser.add_argument('--legacy-emergency-closure', type=Path, help='Required for legacy Emergency: pinned old main and unchanged dependency closure')
    parser.add_argument('--baseline-closure', type=Path, help='Required for baseline: matching historical main/template/header and original receipt manifest')
    parser.add_argument('--freeze', type=Path, help='Read-only SOURCE-FREEZE.json with mainSha256; pinned in source closure')
    parser.add_argument('--extract-only', action='store_true', help='Snapshot/extract only; exit 12 and pass=false, no execution')
    args = parser.parse_args()
    out = args.output_root.resolve()
    need(not out.is_relative_to(ROOT), 'Evidence must be outside the source repository')
    run = out / ('run-' + dt.datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '-' + str(os.getpid()))
    run.mkdir(parents=True)
    result = {
        'schemaVersion': 1, 'pass': False, 'status': 'RUNNING', 'run': str(run), 'variant': args.variant,
        'diagnosticEvidenceComplete': False, 'currentSourceResourceContractAccepted': False,
        'wholeLifecycleAcceptance': False, 'sourceStable': False, 'sourceClosureVerified': False,
        'actualEmergencyCleanupExecuted': False, 'actualWholeShutdownExecuted': False, 'actualRetireResourcesExecuted': False, 'actualCheckedCloseExecuted': False, 'actualInitPrefixExecuted': False, 'fullInitExecuted': False,
        'actualWorkerThreadExecuted': False, 'realCaptureOrHostStatus': 'UNKNOWN_NOT_EXECUTED',
        'realSensorDsuDeviceDriverServiceOrSleepExecuted': False, 'installedApplicationExecuted': False,
        'productSourceEdited': False, 'formalEntryIntegrationPerformed': False, 'processExitUsedAsCleanup': False,
        'memoryLeaves': {
            'OS': ['WaitForSingleObject', 'CloseHandle', 'GetLastError', 'timeEndPeriod',
                   'GetTickCount64', 'UnregisterDeviceNotification', 'ZeroMemory'],
            'COM_and_child_owners': ['inputCaptureWindowsProcessingRetire and inputCaptureLegionMotionSessionRevoke',
                 'SensorThreadPost/PostPower and legacy StopWinRtSensorsImpl; no real mailbox/apartment',
                 'MSI/HC provider stopped/running/stop(0)', 'Census retire/stopped',
                 'Gamebar stopped/Shutdown(0); COM readiness supplied by actor',
                 'X2 pollRetirement/stopped; owner Stop', 'intent mutation scope'],
            'HID': ['HasOverlappedIoCompleted', 'GetOverlappedResult', 'CancelIoEx',
                    'parse/fault and producer revocation leaves; no HID submit/open API'],
            'other': ['DSU start/stop/stopped', 'GM GetProcAddress/Delete/FreeLibrary',
                      'queue/log/clock/assets/startup state'],
        },
        'realInProcessMechanisms': ['actual C++ shared_ptr sink destructor',
            'separate C++ Resource destructors behind every memory owner',
            'actual extracted resource structs/buffers', 'atomics/mutex scopes',
            'actual native/input_capture_completion.h Debt values'],
        'explicitFixtureActorCallsAreProductionSelfHealing': False,
        'historicalBaselineAndReceiptsRewritten': False,
        'historicalPositive422IsCurrentUnloadRequirement': False,
        'identityImplementation': RUNNER + ':SnapshotTools; snapshot-only ' + ADAPTER + ':mask_cpp/compiler_environment',
        'profiles': [], 'remaining': ['UNKNOWN: full Init and transitive ownership', 'UNKNOWN: real STA/WinRT revoke, same-object token reuse and apartment lifetime',
                                    'UNKNOWN: real DSU sockets, capture, Host and arbitrary stuck-worker destruction',
                                    'UNKNOWN: installed application, real inputs, physical sleep and device lifecycle',
                                    'UNRESOLVED_BY_DESIGN: GM object without DLL retains Motion debt; fixture never fabricates a missing owner'],
    }
    def publish():
        save(run / 'RESULT.json', result)
        save(out / 'RESULT.json', result)
    try:
        main_input = args.main.resolve() if args.main else (ROOT / 'native/main.cpp').resolve()
        external = main_input != (ROOT / 'native/main.cpp').resolve()
        inputs = {name: (main_input if name == 'native/main.cpp' else ROOT / name) for name in SOURCES}
        if args.freeze:
            freeze_path = args.freeze.resolve()
            inputs['identity/SOURCE-FREEZE.json'] = freeze_path
            freeze = json.loads(freeze_path.read_text(encoding='utf-8-sig'))
            need(sha(main_input.read_bytes()) == freeze['mainSha256'], 'Frozen main identity mismatch')
            result['freeze'] = {'path': str(freeze_path), 'sha256': sha(freeze_path.read_bytes()),
                                'mainSha256': freeze['mainSha256']}
        legacy_manifest = None
        if args.variant == 'legacy-emergency-mutant':
            need(args.legacy_emergency_closure is not None, 'legacy Emergency requires --legacy-emergency-closure')
            legacy_path = args.legacy_emergency_closure.resolve()
            legacy_manifest = json.loads(legacy_path.read_text(encoding='utf-8-sig'))
            inputs['identity/LEGACY-EMERGENCY-DEPENDENCIES.json'] = legacy_path
            inputs['legacy-emergency/native/main.cpp'] = Path(legacy_manifest['main']['path']).resolve()
        baseline_manifest = None
        if args.variant == 'baseline':
            need(args.baseline_closure is not None, 'baseline requires explicit matching --baseline-closure; current helpers cannot stand in for historical ownership')
            baseline_manifest_path = args.baseline_closure.resolve()
            baseline_manifest = json.loads(baseline_manifest_path.read_text(encoding='utf-8-sig'))
            inputs['identity/BASELINE-DEPENDENCIES.json'] = baseline_manifest_path
            inputs['baseline-dependencies/ORIGINAL-RESULT.json'] = Path(baseline_manifest['receipt']['path']).resolve()
            for name, entry in baseline_manifest['files'].items():
                need(name in ('native/main.cpp', TEMPLATE, 'deps/json/json.hpp', BASELINE), 'Unexpected baseline dependency ' + name)
                inputs['baseline-dependencies/' + name] = Path(entry['path']).resolve()
        main_identity = None
        expected_main = args.main_sha256
        if external and not expected_main:
            identity_path = main_input.parent / 'BASELINE-SOURCE.json'
            main_identity = json.loads(identity_path.read_text(encoding='utf-8-sig'))
            need(Path(main_identity['snapshot']).resolve() == main_input, 'External main manifest snapshot path mismatch')
            expected_main = main_identity['sha256']
            inputs['identity/BASELINE-SOURCE.json'] = identity_path
        need(not external or expected_main is not None, 'External --main requires expected SHA256 or BASELINE-SOURCE.json')
        source = {name: path.read_bytes() for name, path in inputs.items()}
        if expected_main:
            need(re.fullmatch(r'[0-9a-fA-F]{64}', expected_main) is not None, 'Invalid expected main SHA256')
            need(sha(source['native/main.cpp']) == expected_main.lower(), 'Explicit main input identity mismatch')
        result['mainInput'] = {'path': str(main_input), 'sha256': sha(source['native/main.cpp']),
                               'expectedSha256': expected_main, 'isCurrentMainline': not external,
                               'externalBaselineManifest': main_identity}
        result['scope'] = 'Whole selected Shutdown/EmergencyCleanup + actual RetireResources/ClearExitConfiguration/checked-close helpers + Init-through-DSU prefix; all OS/COM/HID/child ports memory-only'
        def inputs_stable():
            return all(inputs[n].read_bytes() == b for n, b in source.items())
        result['sourceHashes'] = {n: sha(b) for n, b in source.items()}
        snapshot = run / 'source'
        for name, data in source.items():
            file = snapshot / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(data)
        identity = load_identity(snapshot)
        ret = identity.ret
        need(sha(source[BASELINE]) == BASELINE_SHA256, 'Frozen baseline JSON identity mismatch')
        frozen = json.loads(source[BASELINE].decode('utf-8-sig'))
        for name, part in frozen['fragments'].items():
            need(sha(part['text'].encode()) == part['sha256'], 'Frozen complete fragment identity mismatch: ' + name)
        if baseline_manifest is None:
            current, audit, all_parts = extract(ret, source['native/main.cpp'].decode('utf-8-sig'))
        else:
            # The historical route has no current main/helper ownership contract.
            need(sha(source['native/main.cpp']) == frozen['mainSha256'], 'baseline --main must be the exact historical main from its closure')
            current, audit = copy.deepcopy(frozen['fragments']), frozen['initSourceAudit']
            all_parts = {name: ret.function(source['native/main.cpp'].decode('utf-8-sig'), name)
                         for name in audit['functionIdentities']}

        baseline_receipt = None
        if baseline_manifest is not None:
            prefix = 'baseline-dependencies/'
            need(sha(source[prefix + 'ORIGINAL-RESULT.json']) == baseline_manifest['receipt']['sha256'], 'Historical receipt identity mismatch')
            baseline_receipt = json.loads(source[prefix + 'ORIGINAL-RESULT.json'].decode('utf-8-sig'))
            for name, entry in baseline_manifest['files'].items():
                need(sha(source[prefix + name]) == entry['sha256'] == baseline_receipt['sourceHashes'][name], 'Historical dependency identity mismatch ' + name)
            need(sha(source[prefix + 'native/main.cpp']) == frozen['mainSha256'], 'Historical complete main must match frozen baseline')
            need(source[prefix + 'deps/json/json.hpp'] == source['deps/json/json.hpp'], 'Compile header differs from historical dependency')
            need(source[prefix + BASELINE] == source[BASELINE], 'Historical frozen baseline differs')
            old_text = source[prefix + 'native/main.cpp'].decode('utf-8-sig')
            for name, expected in frozen['initSourceAudit']['functionIdentities'].items():
                need(ret.function(old_text, name)['sha256'] == expected['sha256'], 'Historical audit/function closure mismatch ' + name)
            for key, name in (('shutdown', 'inputCaptureShutdown'), ('readFence', 'inputHostExitFenceRaised'), ('resetFence', 'inputHostResetExitFence')):
                need(ret.function(old_text, name)['text'] == frozen['fragments'][key]['text'], 'Historical complete fragment mismatch ' + key)
            need(ret.function(old_text, 'inputCaptureInit')['text'].startswith(frozen['fragments']['initPrefix']['text']), 'Historical contiguous Init prefix mismatch')
            result.update(historicalDependencyClosureVerified=True, selectedGeneration='frozen-baseline',
                          scope='Frozen historical Shutdown/Init prefix/fence only; exact historical memory dependency template; current RetireResources NOT linked',
                          baselineOriginalReceiptSha256=baseline_manifest['receipt']['sha256'])
        legacy_emergency = None
        if legacy_manifest is not None:
            old_bytes = source['legacy-emergency/native/main.cpp']
            need(sha(old_bytes) == legacy_manifest['main']['sha256'], 'Legacy Emergency main identity mismatch')
            old_parts, _, _ = extract(ret, old_bytes.decode('utf-8-sig'))
            legacy_emergency = old_parts['emergencyCleanup']
            need(legacy_emergency['sha256'] == legacy_manifest['functionSha256'], 'Legacy whole Emergency identity mismatch')
            shared = [k for k in ACTUAL_HELPERS if k not in ('retireResources', 'clearExitConfiguration', 'emergencyCleanup')]
            shared.append('resourceTypes')
            # Keep the preserved main/body and every fragment's raw identity.
            # Only physical CRLF/LF spelling may differ in the complete content;
            # comments, whitespace, code, type fields and constants stay exact.
            comparison = {}
            for key in shared:
                preserved, actual = old_parts[key], current[key]
                need(all(sha(part['text'].encode()) == part['sha256'] for part in (preserved, actual)),
                     'Legacy Emergency dependency raw fragment identity mismatch: ' + key)
                preserved_lf = preserved['text'].replace('\r\n', '\n')
                actual_lf = actual['text'].replace('\r\n', '\n')
                comparison[key] = {
                    'preservedRawSha256': preserved['sha256'], 'currentRawSha256': actual['sha256'],
                    'rawEqual': preserved['text'] == actual['text'],
                    'preservedCrLfToLfSha256': sha(preserved_lf.encode()),
                    'currentCrLfToLfSha256': sha(actual_lf.encode()),
                    'completeContentEqualAfterCRLFtoLFOnly': preserved_lf == actual_lf,
                }
            result['legacyEmergencyDependencyContentContract'] = {
                'normalization': 'CRLF-to-LF-only; complete helper/type content', 'fragments': comparison,
            }
            need(all(row['completeContentEqualAfterCRLFtoLFOnly'] for row in comparison.values()),
                 'Legacy Emergency dependency helpers/types differ; cannot inject current ownership')
            result['legacyEmergencyDependencyClosureMatched'] = {k: current[k]['sha256'] for k in shared}
            result['legacyEmergencyDirectChildApisAreExplicitMemoryPorts'] = True
        selected, mutation = variant_parts(current, frozen, args.variant, legacy_emergency)
        result['initSourceAudit'] = frozen['initSourceAudit'] if baseline_receipt else audit
        result['selectedGeneration'] = 'frozen-baseline' if baseline_receipt else 'current-helper-closure'
        result['selectedHelpersVerbatimInput'] = {k: selected[k]['sha256'] == current[k]['sha256'] for k in ACTUAL_HELPERS if k in selected}
        result['selectedFragments'] = {n: {k: v for k, v in p.items() if k != 'text'} for n, p in selected.items()}
        result['currentFragmentHashes'] = {n: p['sha256'] for n, p in current.items()}
        result['selectedShutdownIsVerbatimInput'] = selected['shutdown']['text'] == current['shutdown']['text']
        result['selectedPrefixIsVerbatimInput'] = selected['initPrefix']['text'] == current['initPrefix']['text']
        result['selectedShutdownIsVerbatimCurrentMainline'] = not external and result['selectedShutdownIsVerbatimInput']
        result['selectedPrefixIsVerbatimCurrentMainline'] = not external and result['selectedPrefixIsVerbatimInput']
        result['baselineMainSha256InformationalOnly'] = frozen['mainSha256']
        result['mutation'] = mutation
        for name, part in all_parts.items():
            (run / (name + '.cpp-fragment')).write_text(part['text'], encoding='utf-8', newline='')
        save(run / 'selected-fragments.json', selected)
        template_bytes = source['baseline-dependencies/' + TEMPLATE] if baseline_receipt else source[TEMPLATE]
        cpp_text, gm_names = render(template_bytes.decode('utf-8-sig'), selected, source[RETIREMENT].decode('utf-8-sig'), legacy=baseline_receipt is not None,
                                    emergency_cases=LEGACY_EMERGENCY_CASES if legacy_manifest else EMERGENCY_CASES)
        result['gmFunctionPointersChecked'] = gm_names
        (run / 'fixture.cpp').write_text(cpp_text, encoding='utf-8', newline='')
        need(inputs_stable(), 'Source changed during extraction')
        if args.extract_only:
            result.update(status='EXTRACT_ONLY_UNEXECUTED', sourceStable=True)
            publish()
            print(result['status'] + ' RESULT=' + str(out / 'RESULT.json'))
            return 12
        helper = ret.helper
        compiler, env = helper.compiler_environment(run)
        authority = json.loads(source[AUTH])
        pins = {str(Path(r['path']).resolve()).lower(): r['sha256'] for r in authority['files']}
        need(compiler.resolve() == Path(authority['native']['compiler']).resolve(), 'Compiler authority mismatch')
        for path in (compiler, Path(authority['native']['linker']), Path(authority['native']['dumpbin'])):
            need(sha(path.read_bytes()) == pins[str(path.resolve()).lower()], 'Native tool identity changed: ' + str(path))
        env['INCLUDE'] = ';'.join(authority['native']['include'])
        env['LIB'] = ';'.join(authority['native']['lib'])
        result['compiler'] = {'path': str(compiler), 'sha256': sha(compiler.read_bytes()), 'sdkVersion': env.get('WindowsSDKVersion'),
                              'vcvarsPath': str(helper.VCVARS), 'vcvarsSha256': sha(helper.VCVARS.read_bytes())}
        for profile, flags in [('Release', ['/O2', '/MT']), ('Checked', ['/Od', '/RTC1', '/D_DEBUG', '/MTd'])]:
            folder = run / profile
            folder.mkdir()
            cpp, exe, deps = folder / 'partial.cpp', folder / 'partial.exe', folder / 'dependencies.json'
            cpp.write_text(cpp_text, encoding='utf-8', newline='')
            build = helper.execute([compiler, '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/permissive-', '/W4', *flags,
                                    '/sourceDependencies', deps, '/I' + str(snapshot / 'deps/json'), '/I' + str(snapshot / 'native'), cpp,
                                    '/Fo' + str(folder / 'partial.obj'), '/Fe' + str(exe), '/link', '/INCREMENTAL:NO'],
                                   folder, env, folder / 'build.log', 90)
            need(build.returncode == 0, 'Consumer compile failed: ' + str(folder / 'build.log'))
            dependency_data = json.loads(deps.read_text(encoding='utf-8-sig'))
            local = identity.checked_local_includes(dependency_data, snapshot, source)
            need(({'deps/json/json.hpp'} if baseline_receipt else {'deps/json/json.hpp', COMPLETION}).issubset(local), 'Required frozen headers were not compiled')
            includes = {str(Path(p).resolve()): sha(Path(p).read_bytes()) for p in dependency_data['Data']['Includes']}
            save(folder / 'include-identities.json', includes)
            imports = helper.execute([authority['native']['dumpbin'], '/imports', exe], folder, env, folder / 'imports.log', 30)
            need(imports.returncode == 0, 'Import inspection failed')
            forbidden = (*helper.FORBIDDEN, 'ws2_32.dll', 'winhttp.dll', 'wininet.dll', 'ole32.dll', 'sensorsapi.dll')
            need(not any(word in imports.stdout.lower() for word in forbidden), 'Unexpected hardware/network/COM import')
            need(inputs_stable(), 'Source changed before execution')
            execution = helper.execute([exe], folder, env, folder / 'run.log', 30)
            need(execution.returncode in (0, 2), 'Consumer did not complete its observations: ' + str(folder / 'run.log'))
            value = json.loads(execution.stdout.strip().splitlines()[-1])
            result.update(actualEmergencyCleanupExecuted=baseline_receipt is None, actualWholeShutdownExecuted=True, actualRetireResourcesExecuted=baseline_receipt is None, actualCheckedCloseExecuted=baseline_receipt is None, actualInitPrefixExecuted=True)
            expected_names = {'normal-ready', 'partial-sensor-start-failed',
                              'partial-sensor-thread-create-failed-event-owned', 'partial-gm-create-null-dll-owned',
                              'no-init', 'normal-sensor-shutdown-fails-then-settles', 'partial-sensor-shutdown-fails-then-settles',
                              'prefix-admitted', 'prefix-stop-refused', 'prefix-exit-requested-refused',
                              'prefix-terminal-fence-refused', 'prefix-already-ready', 'prefix-assets-absent',
                              'same-worker-late-dsu-start-before-ready'}
            if not baseline_receipt:
                expected_names |= {'retire-' + n for n in RETIREMENT_CASES}
                expected_names |= {'emergency-' + n for n in (LEGACY_EMERGENCY_CASES if legacy_manifest else EMERGENCY_CASES)}
            need(len(value['cases']) == len(expected_names) and {c['name'] for c in value['cases']} == expected_names,
                 'Expected complete bounded scenario set')
            need(all(c['checks'] >= 3 for c in value['cases']), 'Scenario produced no meaningful observations')
            need(value['fixturePass'] == (value['failures'] == 0) == (execution.returncode == 0), 'Runtime failure was masked')
            need(value['allLeavesMemoryOnly'] and not value['fullInitExecuted'] and not value['processExitUsedAsCleanup'], 'Scope contract changed')
            need(value['failures'] == sum(len(c['failures']) for c in value['cases']), 'Incomplete failure accounting')
            if baseline_receipt:
                old_profile = next(p for p in baseline_receipt['profiles'] if p['profile'] == profile)
                need((value['checks'], value['failures']) == (old_profile['checks'], old_profile['failures']), 'Historical baseline counts changed despite matched closure')
            guard, negative = negative_observed(args.variant, value['cases'])
            row = {'profile': profile, **value, 'processExitCode': execution.returncode, 'guardResourceLeakReproduced': guard,
                   'specifiedNegativeObserved': negative, 'compiledLocalIncludes': local, 'includeIdentitiesSha256': sha((folder / 'include-identities.json').read_bytes()),
                   'sourceSha256': sha(cpp.read_bytes()), 'executableSha256': sha(exe.read_bytes())}
            result['profiles'].append(row)
            publish()
            print(profile + ': checks=' + str(value['checks']) + ' failures=' + str(value['failures']) +
                  ' guardResourceLeak=' + str(guard) + ('; frozen historical closure only' if baseline_receipt else '; whole Shutdown/Emergency/RetireResources/checked-close + Init prefix') + '; full Init NOT executed', flush=True)
        need(inputs_stable(), 'Source changed during replay; receipt is stale')
        result.update(sourceStable=True, sourceClosureVerified=True, diagnosticEvidenceComplete=True,
                      actualEmergencyCleanupExecuted=baseline_receipt is None, actualWholeShutdownExecuted=True, actualRetireResourcesExecuted=baseline_receipt is None, actualCheckedCloseExecuted=baseline_receipt is None, actualInitPrefixExecuted=True,
                      checks=sum(p['checks'] for p in result['profiles']), failures=sum(p['failures'] for p in result['profiles']))
        result['baselineGuardGapReproduced'] = all(p['guardResourceLeakReproduced'] for p in result['profiles'])
        clean = all(p['fixturePass'] for p in result['profiles'])
        result['selectedSourceResourceContractPass'] = clean
        if args.variant == 'current':
            result['pass'] = clean and not external
            result['currentSourceResourceContractAccepted'] = clean and not external
            result['status'] = ('PASS_MEMORY_RETIREMENT_AND_INIT_PREFIX_ONLY' if clean else 'RESOURCE_CLEANUP_OR_INIT_ADMISSION_FAILED') if not external else (
                'EXTERNAL_SOURCE_CONTRACT_SATISFIED_CURRENT_UNACCEPTED' if clean else 'EXTERNAL_BASELINE_RESOURCE_CLEANUP_OR_INIT_ADMISSION_FAILED')
        else:
            result['negativeReproductionComplete'] = all(p['specifiedNegativeObserved'] and not p['fixturePass'] for p in result['profiles'])
            need(result['negativeReproductionComplete'], 'Specified negative was not detected by actual resource counts')
            result['status'] = 'NEGATIVE_REPRODUCED_PRODUCTION_UNACCEPTED'
        result['artifacts'] = {str(p.relative_to(run)): sha(p.read_bytes()) for p in run.rglob('*') if p.is_file() and p.name != 'RESULT.json'}
        publish()
        print(result['status'] + ' RESULT=' + str(out / 'RESULT.json'))
        return 0 if result['pass'] else 2
    except Exception as error:
        result['status'] = 'FAIL_TOOL_OR_IDENTITY'
        result['error'] = str(error)
        publish()
        print(str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
