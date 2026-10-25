#!/usr/bin/env python3
"""Bounded cancellation-failure forensics on the complete actual capture worker.

Only private memory OS/capture/BUS/Host leaves run. Error 97 is retained as a
noncontract/unknown-error adversary; 11 is a documented error-code-shaped input.
A pending callback for either error is a conservative assumption, not documented
WinMM behavior or a confirmed product root cause. Completed conditional evidence
returns 2 and leaves production unaccepted. Reuses the existing full fixture.
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
import subprocess
import sys

# Importing the existing consumers must not create files beside their sources.
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
HISTORICAL_MAIN_SHA256 = '7dbc2cc39aa087f37cc7644156c06aaa71b7d1005cb10dcf0a47ac68159412a6'
DEFAULT_OUT = Path('G:/YeManCC-Work/Build/VC-CANCEL22')
BASE = 'tools/fixtures/input_capture_timer_retirement/actual_worker_fixture.cpp.in'
PORT = 'tools/fixtures/input_capture_timer_retirement/cancel_failure_port.cpp.in'
REUSE = 'tools/input_capture_worker_retirement_selftest.py'
SOURCES = ('native/main.cpp', 'native/input_timer_evidence.h', 'deps/json/json.hpp',
           'tools/input_timer_evidence_adapter_selftest.py', REUSE, BASE,
           'tools/input_capture_cancel_failure_selftest.py', PORT)
FAILURE_POINTS = (
    'CONDITIONAL_RETAIN_ID_UNTIL_ASSUMED_CALLBACK_DRAIN',
    'CONDITIONAL_NO_SHUTDOWN_WITH_ASSUMED_PENDING_CALLBACK',
    'CONDITIONAL_NO_CLOSE_WITH_ASSUMED_PENDING_CALLBACK',
    'CONDITIONAL_NO_RETIRED_EVENT_MARKER_SIGNAL',
)
MODE_SCENARIOS = {
    'success': {'result': 0, 'classification': 'MEMORY_SUCCESS_CONTROL'},
    'adversarial-97': {'result': 97, 'classification': 'NONCONTRACT_UNKNOWN_ERROR_ADVERSARY'},
    'document-shaped-11': {'result': 11, 'classification': 'DOCUMENTED_ERROR_CODE_WITH_UNPROVEN_PENDING_CALLBACK'},
}
DOCUMENT_CONTRACT = {
    'url': 'https://learn.microsoft.com/en-us/previous-versions/dd757630(v=vs.85)',
    'publisher': 'Microsoft Learn', 'title': 'timeKillEvent function',
    'checkedForTaskDate': '2026-10-23',
    'documentedReturns': {'success': 'TIMERR_NOERROR',
                          'specifiedEventDoesNotExist': 'MMSYSERR_INVALPARAM'},
    'numericValueSource': 'Windows SDK mmsyscom.h (MMSYSERR_BASE=0; MMSYSERR_INVALPARAM=base+11)',
    'error97': 'TIMERR_NOCANDO=97 is not a documented timeKillEvent return; noncontract/unknown-error adversary only',
    'pendingCallbackAfterError11': 'Not established by this document; the fixture keeps a private callback pending as a conservative assumption',
    'fixtureRegistrationState': 'No WinMM registration state is modeled or confirmed active',
    'conclusionLimit': 'Neither error run establishes a real OS reproduction or a confirmed product-defect root cause',
}



def sha(data):
    return hashlib.sha256(data).hexdigest()


def need(condition, message):
    if not condition:
        raise RuntimeError(message)


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def section(text, name):
    begin, end = '// BEGIN ' + name, '// END ' + name
    need(text.count(begin) == text.count(end) == 1, 'Expected one port section ' + name)
    return text.split(begin, 1)[1].split(end, 1)[0].strip()


def kill_sites(worker, first_line, mask):
    code = mask(worker)
    pattern = (r'if\s*\(captureTimerId\)\s*\{\s*'
               r'const MMRESULT timerKillReceipt = timeKillEvent\(captureTimerId\);'
               r'[\s\S]*?\n\s*\}')
    matches = list(re.finditer(pattern, code))
    need(len(matches) == 4, 'Expected the four existing whole-worker cancellation sites')
    sites = []
    for index, match in enumerate(matches):
        snippet = worker[match.start():match.end()]
        clearing = 'captureTimerId = 0;' in match.group()
        need(clearing == (index < 3), 'Unreviewed timer ID clear shape')
        need('if' not in match.group().split('timeKillEvent', 1)[1], 'Cancellation result acquired a branch')
        need('g_inputTimerEvidence.killResult' in match.group(), 'Actual cancellation receipt missing')
        sites.append({'path': ('normal', 'std', 'nonstandard', 'final-fallback')[index],
                      'line': first_line + worker.count('\n', 0, match.start()),
                      'sha256': sha(snippet.encode('utf-8')), 'text': snippet,
                      'unconditionalIdClearAfterReceipt': clearing,
                      'reachedBySchedules': index < 3})
    return sites


class UnknownSourceShape(RuntimeError):
    """A target or fixture shape changed and needs explicit review."""


def validate_target_shapes(reuse, parts):
    # Shape checks are scoped to extracted production functions. Unrelated Stop
    # or UI edits can change the whole-main hash without changing this contract.
    worker = parts['inputCaptureThreadProc']
    code = reuse.helper.mask_cpp(worker['text'])
    compact = lambda value: re.sub(r'\s+', '', value)
    creations = list(re.finditer(r'captureTimerId\s*=\s*timeSetEvent\([\s\S]*?\);', code))
    need(len(creations) == 1, 'Expected one actual multimedia timer creation')
    expected_creation = ('captureTimerId=timeSetEvent(kInputCaptureGamepadPublishIntervalMs,1,'
                         'inputCaptureTimerEventProc,reinterpret_cast<DWORD_PTR>(captureTimer),'
                         'TIME_PERIODIC|TIME_KILL_SYNCHRONOUS);')
    need(compact(creations[0].group()) == expected_creation, 'Unreviewed timeSetEvent callback/argument/flags shape')
    need(len(re.findall(r'\btimeKillEvent\s*\(', code)) == 4, 'Unreviewed cancellation call count')
    for catch in (r'catch\s*\(const std::exception& error\)', r'catch\s*\(\.\.\.\)'):
        pattern = (catch + r'\s*\{\s*g_inputCaptureStop\.store\(true,\s*std::memory_order_release\);'
                   r'\s*inputCaptureWorkerFault\(')
        need(len(re.findall(pattern, code)) == 1, 'Unreviewed outer worker fault-stop guard: ' + catch)
    sites = kill_sites(worker['text'], worker['line'], reuse.helper.mask_cpp)
    for site in sites[:3]:
        position = worker['text'].index(site['text']) + len(site['text'])
        suffix = reuse.helper.mask_cpp(worker['text'][position:])
        need(re.match(r'\s*\{\s*std::lock_guard<std::mutex> tickLock\(g_inputCaptureBusTickMx\);'
                      r'\s*\}\s*(?:try\s*\{\s*)?inputCaptureShutdown\(\);', suffix),
             'Unreviewed BUS drain/shutdown following cancellation: ' + site['path'])
    need(re.search(r'if\s*\(captureTimer\)\s*\{\s*if\s*\(!multimediaTimer\)\s*'
                   r'CancelWaitableTimer\(captureTimer\);\s*CloseHandle\(captureTimer\);\s*\}', code),
         'Unreviewed final event-close ownership shape')
    callback = compact(reuse.helper.mask_cpp(parts['inputCaptureTimerEventProc']['text']))
    need('elseif(!g_inputCaptureStop.load(std::memory_order_acquire)){'
         'inputCaptureBusTick();timerEvidence.finish(1,g_inputCaptureBusTickGlobal);}'
         'else{timerEvidence.finish(3);}' in callback, 'Unreviewed callback stop/BUS branch')
    need('constHANDLEevent=reinterpret_cast<HANDLE>(user);if(event)SetEvent(event);' in callback,
         'Unreviewed callback event tail')
    return {'periodicSynchronousCreationVerified': True, 'outerFaultStopGuardsVerified': 2,
            'cancellationSitesVerified': 4, 'primaryCancelBusDrainShutdownShapesVerified': 3,
            'callbackStopAndEventTailVerified': True, 'finalEventCloseVerified': True}


def prepare_fixture(reuse, template, port, parts, globals_text):
    # Replace one explicit fake OS leaf and the old test driver only. All other
    # memory leaves, event storage, gates and Scenario come from the old fixture.
    leaf = reuse.function(template, 'timeKillEvent')['text']
    need(template.count(leaf) == 1, 'Unique original fake timeKillEvent required')
    marker = 'static unsigned checks=0,failures=0;'
    need(template.count(marker) == 1, 'Original driver boundary changed')
    driver_start = template.index(marker)
    content = template[:driver_start].replace(leaf, section(port, 'CANCEL_LEAF'), 1)
    content += section(port, 'DRIVER') + '\n'
    original = parts['inputCaptureThreadProc']
    worker = ('\n#line ' + str(original['line']) + ' "actual-main.cpp"\n' +
              original['text'] + '\n#line 1 "fixture-generated.cpp"\n')
    rendered = reuse.render(content, parts, globals_text, worker)
    for name in ('inputCaptureTimerEventProc', 'inputCaptureWorkerFault', 'inputCaptureThreadProc'):
        need(reuse.function(rendered, name)['text'] == parts[name]['text'],
             'Production definition was changed: ' + name)
    need(rendered.count(parts['InputTimerCallbackEvidenceScope']['text']) == 1,
         'The actual callback evidence scope must remain verbatim')
    return rendered


def validate_runtime(value, result_code, mode):
    expected_result = MODE_SCENARIOS[mode]['result']
    failure = expected_result != 0
    need(result_code == (2 if failure else 0), 'Unexpected fixture exit code for ' + mode)
    need(value.get('evidenceComplete') is True and value.get('forensicFailures') == 0,
         'Fixture observation/cleanup failed for ' + mode)
    need(value.get('pass') is False and value.get('productionAccepted') is False,
         'A scoped control must not sign the product as PASS')
    need(value.get('successControlPassed') is (not failure) and
         value.get('conditionalRetirementContractSatisfiedForThisMatrix') is (not failure),
         'Control/conditional-contract result mismatch')
    need(value.get('realOsReproduction') is False and value.get('productDefectRootCauseConfirmed') is False
         and value.get('confirmedProductDefectFailures') == 0,
         'Conservative memory input must not become a confirmed OS/product-defect claim')
    cases = value.get('cases', [])
    need(len(cases) == 6 and {(c['exit'], c['interleaving']) for c in cases} ==
         {(exit_path, gate) for exit_path in ('normal', 'std', 'nonstandard')
          for gate in ('beforeBUS', 'busy-tail')}, 'Incomplete schedule matrix')
    for item in [value, *cases]:
        semantics = item['cancellationScenario']
        need(all(semantics[key] == expected for key, expected in MODE_SCENARIOS[mode].items()),
             'Wrong error code/classification metadata')
        need(semantics['errorCodeMatchesDocumentedTimeKillEventReturn'] is (expected_result in (0, 11)) and
             semantics['pendingCallbackConservativeAssumption'] is failure and
             semantics['pendingCallbackDocumentedForThisError'] is False and
             semantics['fixtureModelsWinmmRegistrationState'] is False and
             semantics['realOsReproduction'] is False and semantics['productDefectRootCauseConfirmed'] is False,
             'Incorrect documented-code/pending-callback inference')
    for case in cases:
        need(case['ownedThreadsJoined'] and case['ownedWorkerJoined'] and case['ownedCallbackJoined']
             and case['ownedActiveCallbacksAfter'] == 0 and case['remainingFixtureEvents'] == 0
             and not case['gateTimedOut'], 'Owned fixture resources did not retire')
        need(case['injectedResult'] == expected_result and case['ownerReceiptAfterJoin']['killResult'] == expected_result,
             'Actual Recorder did not retain the selected fake cancellation result')
        need(case['killCalls'] == case['shutdownCalls'] == case['eventCloseCalls'] == 1,
             'Unexpected worker cleanup counts')
        need(case['specifiedFailurePoints'] == (list(FAILURE_POINTS) if failure else []),
             'The designated conditional memory contract points were not reproduced')
        need(case['closedEventSignals'] == (1 if failure else 0) and case['lateBus'] == 0,
             'Unexpected safe-marker/BUS observation')
        need(case['timerIdClearEvidence']['localDirectlyInstrumented'] is False,
             'Must not claim direct local-ID instrumentation')
        need(case['conditionalUnsafeOrderingObserved'] is failure and case['realOsReproduction'] is False
             and case['productDefectRootCauseConfirmed'] is False and case['productionAccepted'] is False,
             'Conditional ordering was promoted to an unconditional product claim')
    need(value['conditionalContractFailures'] == (24 if failure else 0), 'Conditional rejection count mismatch')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-root', type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    output = args.output_root.resolve()
    run = output / ('r-' + dt.datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '-' + str(os.getpid()))
    run.mkdir(parents=True)
    previous_path = output / 'RESULT.json'
    previous_receipt = previous_path.read_bytes() if previous_path.is_file() else None
    receipt = {
        'schemaVersion': 2, 'pass': False, 'productionAccepted': False,
        'productionRetirementAccepted': False, 'evidenceComplete': False,
        'status': 'RUNNING', 'run': str(run), 'repo': str(ROOT),
        'createdAt': dt.datetime.now().astimezone().isoformat(), 'profiles': [],
        'scope': 'Conditional behavior of verbatim worker/callback/WorkerFault and Recorder on existing memory leaves',
        'realOsReproduction': False, 'productDefectRootCauseConfirmed': False,
        'confirmedProductDefectFailures': 0,
        'documentContract': DOCUMENT_CONTRACT,
        'scenarios': MODE_SCENARIOS,
        'claimCorrection': 'Earlier PRODUCTION_CANCEL_FAILURE_UNSAFE_CONFIRMED wording is withdrawn: raw 97 observations remain, but are noncontract/unknown-error conditional input, not a real WinMM bug reproduction or a confirmed product root cause',
        'productionAcceptanceReason': 'No real OS/lifecycle acceptance evidence; exit 2 means unaccepted, not a confirmed OS defect',
        'historicalMainSha256': HISTORICAL_MAIN_SHA256,
        'historicalMainHashEnforced': False,
        'sourceIdentityPolicy': 'Extract current complete target functions; freeze all inputs per run and require before/after byte and SHA identity; historical whole-main SHA is informational only',
        'realWinmmOrDeviceCalls': False, 'realApplicationOrHostOrServiceOrDriverRun': False,
        'productionSourceEdited': False, 'globalIntegrationPerformed': False,
        'functionClass': 'persistent periodic capture worker; fixture lifetime is bounded',
        'specifiedFailurePoints': list(FAILURE_POINTS),
        'memoryBoundary': {
            'injection': 'Only fake OS timeKillEvent: separate 97 adversary and documented-code-shaped 11 return without joining',
            'pendingCallbackAssumption': 'For both nonzero inputs, pending private callback is conservatively assumed, not proved by the API document',
            'registrationState': 'Not modeled; no claim that a WinMM event remains registered',
            'productionFunctions': ['inputCaptureThreadProc', 'inputCaptureTimerEventProc',
                                    'inputCaptureWorkerFault', 'InputTimerCallbackEvidenceScope'],
            'actualRecorder': 'native/input_timer_evidence.h',
            'reusedTemplate': BASE,
            'otherLeaves': ['safe Event marker/CreateEventW/SetEvent/CloseHandle', 'waitable timer and wait stubs',
                            'COM and timer-period counters', 'capture-init and deterministic init exceptions',
                            'capture-shutdown/emergency-shutdown counter', 'BUS counter', 'Host/startup/logging stubs',
                            'clock/thread ID values and disabled tick-cost instrumentation'],
            'safeEventLifetime': 'Scenario::Event is not freed until both owned threads joined; no invalid real HANDLE access',
            'timerIdEvidence': 'Source unconditional clear plus positive worker completion and absence of final retry; local not instrumented',
            'exitTriggers': {'normal': 'Set stop while callback gated, then let capture-init return',
                             'std': 'Existing memory capture-init leaf throws std::runtime_error',
                             'nonstandard': 'Existing memory capture-init leaf throws int 123'},
        },
        'bounds': {'profiles': 2, 'modesPerProfile': 3, 'schedulesPerMode': 6,
                   'maxConcurrentFixtureThreads': 2, 'callbacksPerSchedule': 1,
                   'eventMarkersPerSchedule': 1, 'gateWaitMs': 2000,
                   'failedWorkerPositiveObservationMs': 800, 'successEarlyObservationMs': 30,
                   'compilerTimeoutSeconds': 90, 'runtimeTimeoutSeconds': 20,
                   'timeoutCanPass': False, 'preserveAllRunArtifacts': True},
        'remaining': [
            'Whether an already-dispatched callback can remain pending after real MMSYSERR_INVALPARAM=11 is unproven by the cited document',
            'Error 97 is not a documented timeKillEvent return; no actual reachability or real OS error path is proved',
            'Real WinMM registration/error causes, API and internal thread behavior',
            'Actual inputCaptureShutdown/EmergencyCleanup resource disposal and transitive locks',
            'Host/application/device/service/driver integration and physical sleep/input/game consumption',
            'Arbitrarily stalled callbacks, callback bursts, repeated close/restart and owner generations',
            'Fourth final-fallback cancellation site is source-frozen but not reached by this matrix',
            'inputCaptureStop wait-result consumer is outside this task',
            'No production fix, deployment or full persistent-lifecycle acceptance',
        ],
    }

    def publish():
        write_json(run / 'RESULT.json', receipt)
        write_json(output / 'RESULT.json', receipt)

    publish()
    try:
        if previous_receipt is not None:
            (run / 'previous-RESULT.json').write_bytes(previous_receipt)
            receipt['retainedPreviousReceipt'] = {'path': str(run / 'previous-RESULT.json'),
                'sha256': sha(previous_receipt), 'previousRun': json.loads(previous_receipt).get('run'),
                'classification': 'Raw history retained unchanged; earlier unconditional bug/root-cause claims are superseded'}
        write_json(run / 'document-contract.json', DOCUMENT_CONTRACT)
        sources = {name: (ROOT / name).read_bytes() for name in SOURCES}
        receipt['sourceHashes'] = {name: sha(data) for name, data in sources.items()}
        for name, data in sources.items():
            target = run / 'source' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        receipt['matchesHistoricalMainReference'] = receipt['sourceHashes']['native/main.cpp'] == HISTORICAL_MAIN_SHA256
        spec = importlib.util.spec_from_file_location('cancel_failure_actual_worker_reuse', ROOT / REUSE)
        reuse = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(reuse)
        helper = reuse.helper
        try:
            parts, globals_text = reuse.extract(sources['native/main.cpp'].decode('utf-8-sig'))
            receipt['actualFunctions'] = {
                name: {'line': part['line'], 'sha256': sha(part['text'].encode('utf-8'))}
                for name, part in parts.items()}
            receipt['targetShapeChecks'] = validate_target_shapes(reuse, parts)
            worker = parts['inputCaptureThreadProc']
            receipt['cancellationSites'] = kill_sites(worker['text'], worker['line'], helper.mask_cpp)
            template = sources[BASE].decode('utf-8-sig')
            port = sources[PORT].decode('utf-8-sig')
            content = prepare_fixture(reuse, template, port, parts, globals_text)
        except RuntimeError as error:
            raise UnknownSourceShape(str(error)) from error
        receipt['generatedFixtureSha256'] = sha(content.encode('utf-8'))
        receipt['productionFunctionsCompiledVerbatim'] = True
        receipt['templateChanges'] = ['Replace existing fake timeKillEvent leaf with observed success/failure port',
                                      'Replace the test driver with paired forensic schedules and cleanup guard']
        git = helper.execute(['git', 'rev-parse', 'HEAD'], ROOT, os.environ.copy(), run / 'git-head.log', 10)
        need(git.returncode == 0, 'Cannot record repository HEAD')
        receipt['gitHead'] = git.stdout.strip()
        compiler, env = helper.compiler_environment(run)
        receipt['compiler'] = {'path': str(compiler), 'sha256': sha(compiler.read_bytes()),
                               'vcvarsPath': str(helper.VCVARS), 'vcvarsSha256': sha(helper.VCVARS.read_bytes()),
                               'vcToolsVersion': env.get('VCToolsVersion'), 'sdkVersion': env.get('WindowsSDKVersion')}
        sdk_sources = {}
        for header in ('mmsyscom.h', 'timeapi.h'):
            candidates = [Path(directory) / header for directory in env['INCLUDE'].split(os.pathsep)]
            sdk_path = next((candidate for candidate in candidates if candidate.is_file()), None)
            need(sdk_path is not None, 'Cannot freeze SDK numeric definitions: ' + header)
            data = sdk_path.read_bytes()
            sdk_sources[str(sdk_path)] = data
            target = run / 'sdk' / header
            target.parent.mkdir(exist_ok=True)
            target.write_bytes(data)
        mmsys = next(data.decode('utf-8-sig') for name, data in sdk_sources.items() if name.endswith('mmsyscom.h'))
        timeapi = next(data.decode('utf-8-sig') for name, data in sdk_sources.items() if name.endswith('timeapi.h'))
        for pattern, body in ((r'#define\s+MMSYSERR_BASE\s+0\b', mmsys),
                              (r'#define\s+MMSYSERR_INVALPARAM\s+\(MMSYSERR_BASE\s*\+\s*11\)', mmsys),
                              (r'#define\s+TIMERR_BASE\s+96\b', mmsys),
                              (r'#define\s+TIMERR_NOCANDO\s+\(TIMERR_BASE\s*\+\s*1\)', timeapi)):
            need(re.search(pattern, body), 'Unreviewed SDK numeric constant: ' + pattern)
        receipt['sdkConstantEvidence'] = {name: sha(data) for name, data in sdk_sources.items()}
        for profile, flags, runtime in [('Release', ['/O2'], '/MT'),
                                        ('Checked', ['/Od', '/RTC1', '/D_DEBUG'], '/MTd')]:
            folder = run / ('r' if profile == 'Release' else 'c')
            folder.mkdir()
            cpp, exe = folder / 'fixture.cpp', folder / 'fixture.exe'
            cpp.write_bytes(content.encode('utf-8'))
            argv = [compiler, '/nologo', '/std:c++20', '/utf-8', '/EHsc', runtime,
                    '/permissive-', '/W4', *flags,
                    '/I' + str(run / 'source/native'), '/I' + str(run / 'source/deps/json'), cpp,
                    '/Fo' + str(folder / 'fixture.obj'), '/Fe' + str(exe)]
            write_json(folder / 'compile-command.json', [str(arg) for arg in argv])
            build = helper.execute(argv, folder, env, folder / 'build.log', 90)
            need(build.returncode == 0, 'Fixture build failed: ' + str(folder / 'build.log'))
            imports = helper.execute([compiler.parent / 'dumpbin.exe', '/imports', exe],
                                     folder, env, folder / 'imports.txt', 20)
            need(imports.returncode == 0, 'Fixture import inspection failed')
            forbidden = [name for name in helper.FORBIDDEN if name in imports.stdout.lower()]
            need(not forbidden, 'Forbidden real timer/input/Steam imports: ' + ', '.join(forbidden))
            profile_receipt = {'profile': profile, 'compileSourceSha256': sha(cpp.read_bytes()),
                               'executableSha256': sha(exe.read_bytes()),
                               'importsSha256': sha((folder / 'imports.txt').read_bytes()),
                               'forbiddenImports': forbidden, 'modes': []}
            receipt['profiles'].append(profile_receipt)
            for mode in MODE_SCENARIOS:
                proc = helper.execute([exe, mode], folder, env, folder / (mode + '.log'), 20)
                value = json.loads(proc.stdout.strip().splitlines()[-1])
                # Keep raw parsed failure evidence even if later validation fails.
                write_json(folder / (mode + '.json'), value)
                profile_receipt['modes'].append({'mode': mode, 'exitCode': proc.returncode,
                    'runtimeLogSha256': sha((folder / (mode + '.log')).read_bytes()), **value})
                publish()
                validate_runtime(value, proc.returncode, mode)
                if mode != 'success':
                    need(proc.stderr.count('CONDITIONAL_CONTRACT_FAILED ') == 24 and
                         all(point in proc.stderr for point in FAILURE_POINTS),
                         'Specified rejection diagnostics missing')
                print(profile + ' ' + mode + ': ' + str(value['forensicChecks']) +
                      ' forensic checks; 6 schedules; conditional failures=' +
                      str(value['conditionalContractFailures']) + '; exit=' + str(proc.returncode), flush=True)
        sources_after = {name: (ROOT / name).read_bytes() for name in SOURCES}
        receipt['sourceHashesAfter'] = {name: sha(data) for name, data in sources_after.items()}
        need(receipt['sourceHashesAfter'] == receipt['sourceHashes'] and
             all(sources_after[name] == data for name, data in sources.items()),
             'Source/tool/template changed during the run; frozen evidence retained but incomplete')
        need(all(Path(name).read_bytes() == data for name, data in sdk_sources.items()),
             'SDK numeric-definition inputs changed during the run')
        receipt['sourceStable'] = True
        modes = [mode for profile in receipt['profiles'] for mode in profile['modes']]
        receipt['evidenceComplete'] = True
        receipt['forensicChecks'] = sum(mode['forensicChecks'] for mode in modes)
        receipt['conditionalContractFailures'] = sum(mode['conditionalContractFailures'] for mode in modes)
        receipt['schedulesExecuted'] = sum(len(mode['cases']) for mode in modes)
        receipt['allOwnedFixtureThreadsJoined'] = all(case['ownedThreadsJoined'] for mode in modes for case in mode['cases'])
        receipt['conditionalOrderingWitnessedUnderAssumption'] = all(
            case['conditionalUnsafeOrderingObserved'] for mode in modes if mode['mode'] != 'success' for case in mode['cases'])
        receipt['status'] = 'CONDITIONAL_MEMORY_EVIDENCE_COMPLETE_PRODUCTION_UNACCEPTED'
        receipt['meaning'] = ('Under a conservatively assumed pending private callback, both the noncontract/unknown '
                              '97 input and documented-code-shaped 11 input lead to receipt recording, unconditional ID '
                              'clear, shutdown and event close before callback release. The document does not establish '
                              'this pending-callback state for error 11. Neither input proves a real OS reproduction or '
                              'a confirmed product root cause. Production remains unaccepted.')
        receipt['scenarioEvidence'] = {}
        for mode in ('adversarial-97', 'document-shaped-11'):
            selected = [{'profile': profile['profile'], **item} for profile in receipt['profiles']
                        for item in profile['modes'] if item['mode'] == mode]
            artifact = run / (mode + '-RESULT.json')
            group = {'schemaVersion': 2, 'mode': mode, 'pass': False, 'productionAccepted': False,
                     'evidenceComplete': True, 'exitCode': 2, 'status': receipt['status'],
                     'realOsReproduction': False, 'productDefectRootCauseConfirmed': False,
                     'confirmedProductDefectFailures': 0, 'documentContract': DOCUMENT_CONTRACT,
                     'sourceHashes': receipt['sourceHashes'], 'profiles': selected,
                     'meaning': receipt['meaning'],
                     'forensicChecks': sum(item['forensicChecks'] for item in selected),
                     'conditionalContractFailures': sum(item['conditionalContractFailures'] for item in selected),
                     'schedulesExecuted': sum(len(item['cases']) for item in selected)}
            write_json(artifact, group)
            receipt['scenarioEvidence'][mode] = {'path': str(artifact), 'sha256': sha(artifact.read_bytes()),
                'schedulesExecuted': group['schedulesExecuted'], 'classification': MODE_SCENARIOS[mode]['classification']}
        receipt['exitCode'] = 2
        publish()
        print(receipt['status'] + ' RESULT=' + str(output / 'RESULT.json'), flush=True)
        return 2
    except UnknownSourceShape as error:
        receipt['evidenceComplete'] = False
        receipt['status'] = 'UNKNOWN_SOURCE_OR_FIXTURE_SHAPE'
        receipt['error'] = str(error)
        receipt['exitCode'] = 1
        publish()
        print(str(error), file=sys.stderr)
        return 1
    except subprocess.TimeoutExpired as error:
        receipt['evidenceComplete'] = False
        receipt['status'] = 'INCOMPLETE_TIMEOUT'
        receipt['error'] = str(error)
        receipt['timeoutAcceptedAsPass'] = False
        receipt['exitCode'] = 1
        # subprocess.run reaps the private process. This is diagnostic failure,
        # never a claim that its owned callback/worker were joined successfully.
        publish()
        print(str(error), file=sys.stderr)
        return 1
    except Exception as error:
        receipt['evidenceComplete'] = False
        receipt['status'] = 'INCOMPLETE_ERROR'
        receipt['error'] = str(error)
        receipt['exitCode'] = 1
        publish()
        print(str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
