"""Replay USB4 failures through production C++ functions, with OS sleep mocked.

The test extracts the actual wake handlers and retry functions from main.cpp.
It never sleeps this computer or suspends a user process. Run from repo root.
"""
from pathlib import Path
import argparse
import re
import subprocess
import hashlib
import json

parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=Path('native/main.cpp'))
parser.add_argument('--output', type=Path, default=Path('../../Build/Validation/SleepNonUserWake'))
selection = parser.add_mutually_exclusive_group()
selection.add_argument('--interrupted-entry-only', action='store_true', help='Execute only the current interrupted entry, display and human-veto controls')
selection.add_argument('--suspend-admission-only', action='store_true', help='Compile once and execute only the extracted suspend-admission scenarios')
selection.add_argument('--resume-classification-public-facts-only', action='store_true', help='Only actual held PBT ingress and public wake evidence lifecycle')
parser.add_argument('--prepare-only', action='store_true', help='Generate exact selected C++ without compiling or running')
selection.add_argument('--resume-classification-only', action='store_true', help='Execute actual common/worker early-resume classification only')
selection.add_argument('--resume-classification-edge-only', action='store_true', help='Continue only the two late original506 after classified507 cases')
selection.add_argument('--resume-classification-lifetime-only', action='store_true', help='Only critical resume, missing Kernel evidence and actual settings cancellation controls')
selection.add_argument('--resume-classification-publication-only', action='store_true', help='Only the original queued shared publication, coalescing and exit controls')
selection.add_argument('--sleep-request-notification-only', action='store_true', help='Execute actual sender and public notification supersession boundaries only')
parser.add_argument('--sleep-request-case', default=None, help='Exact sender case; valid only with --sleep-request-notification-only')
selection.add_argument('--resume-classification-sender-only', action='store_true', help='Original field classification and actual sender joined with public Reader only')
parser.add_argument('--resume-sender-case', default=None, help='Exact original joined field selection')
selection.add_argument('--broadcast-consumer-only', action='store_true', help='Execute complete original broadcast with independent observer fault ports')
parser.add_argument('--broadcast-consumer-case', choices=["gate-query-reopen-refused","gate-query-normal","gate-query-event-refused","gate-query-handle-refused","gate-query-both-refused","gate-query-contended","gate-query-repeat-refused","sensor-query-normal","sensor-query-capture-allocation","sensor-query-refused-allocation","sensor-query-diagnostic-double","sensor-query-already-paused","sensor-query-inactive","journal-query-normal","journal-bare-normal","journal-query-armed-record","journal-bare-armed-record","journal-query-origin-record","journal-bare-origin-record","journal-query-classification-record","journal-bare-classification-record","journal-bare-monitor-record","journal-query-marker-allocation","journal-bare-marker-allocation","suspend-query-normal","suspend-bare-normal","suspend-query-notification-allocation","suspend-bare-notification-allocation","suspend-query-notification-allocation-double","normal","journal","capture","device-log","focus","radial","steam-close","steam-interactive","focus-observation","msi-observation","msi-completion","failure-log","suspend-prepare","suspend-public-consumer-handoff","suspend-foreground-queue-failure","argument-allocation","common-normal","common-entry-allocation","common-entry-log","common-keep-journal-allocation","common-input-release","common-input-reset","common-input-rescan","common-handheld","common-resuming","common-notification-allocation","common-msi-observation","common-diagnostic-double","common-retry-automatic","common-duplicate-automatic","common-duplicate-interactive","common-native-queue-normal","common-native-queue-entry-allocation","common-native-queue-keep-allocation","common-native-queue-allocation-fails"], default=None, help='Exact complete-broadcast scenario; requires --broadcast-consumer-only')
parser._option_string_actions['--broadcast-consumer-case'].choices.extend([
    'common-s4-entry-boundary', 'common-s4-queue-revocation', 'common-s4-own-gate-close'])
args = parser.parse_args()
if args.broadcast_consumer_case is not None and not args.broadcast_consumer_only:
    parser.error('--broadcast-consumer-case requires --broadcast-consumer-only')
if args.broadcast_consumer_only:
    import sys
    if sys.argv.count('--broadcast-consumer-only') != 1 or sys.argv.count('--broadcast-consumer-case') > 1:
        parser.error('duplicate broadcast selection')
if args.resume_classification_sender_only:
    import sys
    if sys.argv.count('--resume-classification-sender-only') != 1 or sys.argv.count('--resume-sender-case') > 1:
        parser.error('duplicate joined selection')
if args.resume_sender_case is not None and not args.resume_classification_sender_only:
    parser.error('--resume-sender-case requires --resume-classification-sender-only')
if args.sleep_request_case is not None and not args.sleep_request_notification_only:
    parser.error('--sleep-request-case requires --sleep-request-notification-only')
source = args.source.read_text(encoding='utf-8-sig')
dispatch_header_path = args.source.parent / 'sleep_power_dispatch.h'
dispatch_header_bytes = dispatch_header_path.read_bytes()
isolation_header_path = args.source.parent / 'msi_native_foreground_isolation_runtime.h'
isolation_header_bytes = isolation_header_path.read_bytes()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
def mask_cpp(text):
    tokens = re.compile(r'//[^\n]*|/\*.*?\*/|(?:u8|u|U|L)?R"(?P<rawtag>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=rawtag)"|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27', re.S)
    return tokens.sub(lambda match: ''.join('\n' if c == '\n' else ' ' for c in match.group()), text)

masked = mask_cpp(source)

def braced(opening, text=None):
    text = masked if text is None else text
    if opening < 0 or opening >= len(text) or text[opening] != '{':
        raise RuntimeError('native body opening is not a brace')
    end, depth = opening + 1, 1
    while depth and end < len(text):
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    if depth:
        raise RuntimeError('native body is incomplete')
    return end

# A match in the original pattern cannot cross ;, { or } before its final
# opening brace. Index those disjoint intervals once for the current immutable
# mask, then apply that exact original pattern in every eligible interval.
# Keep only one mask: source changes replace this local index, never a file or
# cross-process cache. Duplicate/missing definitions still fail every lookup.
_function_region_mask = None
_function_region_spans = ()

def function(name, native_text=None, *, noexcept=False):
    global _function_region_mask, _function_region_spans
    native_text = source if native_text is None else native_text
    native_mask = masked if native_text is source else mask_cpp(native_text)
    if native_mask is not _function_region_mask:
        spans = []
        start = 0
        static_keyword = re.compile(r'\bstatic\s+')
        for boundary in re.finditer(r'[;{}]', native_mask):
            end = boundary.end()
            if boundary.group() == '{' and static_keyword.search(native_mask, start, end):
                spans.append((start, end))
            start = end
        _function_region_mask, _function_region_spans = native_mask, tuple(spans)
    qualifier = r'\s*noexcept' if noexcept else ''
    pattern = re.compile(r'\bstatic\s+[^;{}]*?\b' + re.escape(name) + r'\s*\([^;{}]*?\)' + qualifier + r'\s*\{')
    matches = [match for start, end in _function_region_spans for match in pattern.finditer(native_mask, start, end)]
    if len(matches) != 1:
        raise RuntimeError('native function not unique: ' + name)
    match = matches[0]; opening = native_mask.index('{', match.start())
    return native_text[match.start():braced(opening, native_mask)]

if args.sleep_request_notification_only:
    import runpy
    support = Path(__file__).parent / 'fixtures/sleep_non_user_wake/sleep_request_notifications.py'
    raise SystemExit(runpy.run_path(str(support))['run'](globals()))

def unique_anchor(anchor):
    positions = [match.start() for match in re.finditer(re.escape(anchor), masked)]
    if len(positions) != 1:
        raise RuntimeError('native handler anchor not unique: ' + anchor)
    return positions[0]

def body_after(anchor):
    start = unique_anchor(anchor); opening = masked.index('{', start)
    return source[opening + 1:braced(opening) - 1]

def case_body(start, end):
    # The next requested case may have an unrelated new case inserted before it.
    # Match the complete target braces, never the whole textual interval.
    # Only a complete braced case owns this handler; other switches may
    # observe the same Windows message without consuming its broadcast.
    matches = list(re.finditer(re.escape(start) + r'\s*\{', masked))
    if len(matches) != 1:
        raise RuntimeError('native braced handler not unique: ' + start)
    begin = matches[0].start(); opening = masked.index('{', begin)
    finish = braced(opening); next_case = unique_anchor(end)
    if finish > next_case:
        raise RuntimeError('native case crosses its requested successor')
    return source[opening + 1:finish - 1]

# The lexical scanner must ignore decoy braces and stop before a neighboring case.
_probe = 'case TARGET: { const char* p=R"x(} case NEXT: {)x"; /* } */ if(true){} } case OTHER: {} case NEXT: {}'
_probe_mask = mask_cpp(_probe)
_probe_open = _probe_mask.index('{')
_probe_end = braced(_probe_open, _probe_mask)
assert 'case OTHER:' not in _probe[:_probe_end] and _probe_mask[_probe_end:].lstrip().startswith('case OTHER:')
try: braced(0, '{')
except RuntimeError: pass
else: raise AssertionError('incomplete native case accepted')


assert 'SG_USER_STANDBY_DEVICE_DELAY_MS = 120000ULL' in source
assert 'SG_NON_USER_WAKE_CLASSIFY_MS = 2000ULL' in source
for broadcast in ['PBT_APMRESUMEAUTOMATIC', 'PBT_APMRESUMESUSPEND']:
    body = body_after('else if (w == ' + broadcast + ')')
    assert 'sgObserveNonUserWake(' in body
    if broadcast == 'PBT_APMRESUMESUSPEND':
        assert body.index('!sgModernStandbyRepairSupported()') < body.index('sgObserveNonUserWake(')
        assert body.index('confirmedUserWake()') < body.index('sgObserveNonUserWake(')
assert 'sgArmNonUserWakeGuard("sleep-trigger")' in function('sgMarkSleepTrigger')
assert 'request(caps,' in function('sgRequestSystemSleep')
request_terminal = function('sgRequestSystemSleep')
assert 'SetSuspendState(FALSE,FALSE,FALSE)' in request_terminal
assert 'SendMessageTimeoutW(' not in request_terminal and 'SC_MONITORPOWER' not in request_terminal
assert 'SetSuspendState(TRUE' not in request_terminal
assert 'g_sgPauseResume' not in function('sgEvaluateExternalDeviceWake')
assert 'sgCancelEntryFailureRetry("entry-failure-setting-disabled")' in function('sgSaveConfig')

assert 'if (entering) sgRememberKernelSleepEntry(reason, eventFileTime);' in function('sgKernelPowerEventCallback')
assert 'sgPublishSleepEventTime(g_sgOriginalPowerButtonWakeFileTime,eventFileTime);' in function('sgKernelPowerEventCallback')
names = [
    'sgRenewCancellationReady',
    'sgUserWakeCancelsSleepIntent', 'sgPublishSleepEventTime', 'sgRememberKernelSleepEntry',
    'sgSleepRetryActive', 'sgMarkInternalSleepRequest', 'sgClearInternalSleepRequest',
    'sgInternalSleepRequestMatches506', 'sgModernStandbyRepairSupported', 'sgNonUserWakeGuardEnabled',
    'sgSleepTriggerOrigin', 'sgRefreshSleepTriggerOrigin', 'sgExternalDeviceWakeIntentAge', 'sgClearUnexpectedWake', 'sgArmNonUserWakeGuard',
    'sgEvaluateExternalDeviceWake', 'sgObserveNonUserWake',
    'sgNoteExternalDeviceNodeChange', 'sgNoteExternalDeviceAcDcChange',
    'sgNoteExternalDeviceKernel507Reason5', 'sgNoteExternalDeviceKernel507Reason7',
    'sgMarkSleepTrigger', 'sgStopSleepRetry', 'sgRealWake',
    'sgFinishSleepRetryFailure', 'sgScheduleNextSleepRetry', 'sgCancelEntryFailureRetry', 'sgDispatchSameModeRetry',
    'sgStartSleepRetry', 'sgObserveUnattributedSleepRetryFailure', 'sgEntryFailureWindowEligible', 'sgS0EntryFailureEligible',
    'sgHandleModernStandbyWake','sgStopMonitorEntryFailureTimer','sgArmMonitorEntryFailureTimer','sgObserveEntryFailureDisplay','sgMonitorEntryFailureTick',
    'sgKernelPowerEventFileTime',
    *(['sgAdmitS4Wake'] if 'static bool sgAdmitS4Wake(' in source else []),
    *(['fanHostRecordSharedCriticalWake'] if 'static void fanHostRecordSharedCriticalWake(' in source else []),
]
has_retry_timer_scope = 'struct SgSleepRetryFailureScope {' in source
if has_retry_timer_scope:names.insert(names.index('sgScheduleNextSleepRetry'),'sgArmSleepRetryTimer')
functions = '\n\n'.join(function(name) for name in names)
if 'static void sgArmS4WakeRedelivery(' in source:
    s4_begin=unique_anchor('static std::mutex g_sgFailedS4Mutex;')
    post_name='sgPostKernelWake' if 'static void sgPostKernelWake(' in source else 'sgPostS4Wake'
    s4_end=unique_anchor(f'static void {post_name}(')
    assert s4_begin<s4_end
    functions += '\n'+source[s4_begin:s4_end]+function('sgArmS4WakeRedelivery')+'\n'
# The Native callback now consumes this actual source-order predicate.
timeout_native=(args.source.parent/'sleep_timeout_native.inc').read_text('utf-8-sig')
uses_timeout_owner=any([args.resume_classification_public_facts_only,args.resume_classification_only,
    args.resume_classification_edge_only,args.resume_classification_lifetime_only,
    args.resume_classification_publication_only,args.resume_classification_sender_only])
functions += '\n'+('static bool sgKernelPowerWakePredatesLatestEntry(int,ULONGLONG,ULONGLONG*);' if uses_timeout_owner
    else function('sgKernelPowerWakePredatesLatestEntry',timeout_native))+'\n'

hardware_write_allowed = function('hardwareWriteAllowed')
shared_power_ready = function('msiNativeForegroundSharedPowerReady', isolation_header_bytes.decode('utf-8-sig'))
shared_power_hold_release = function('msiNativeForegroundTryReleasePowerHold', isolation_header_bytes.decode('utf-8-sig'))
uses_deferred_ready = 'msiNativeForegroundContinueDeferredReady(' in shared_power_ready
deferred_ready = function('msiNativeForegroundContinueDeferredReady', isolation_header_bytes.decode('utf-8-sig')) if uses_deferred_ready else ''
ready_work_current = function('msiNativeForegroundReadyWorkCurrent', isolation_header_bytes.decode('utf-8-sig'), noexcept=True) if 'msiNativeForegroundReadyWorkCurrent(' in deferred_ready else ''
if uses_deferred_ready:
    ready_work = re.search(r'\bstruct MsiNativeForegroundReadyWork \{[^}]+\};', source).group(0)
    ready_work_ports = ready_work + r'''
std::atomic<uint64_t> g_msiNativeForegroundEdge{1};
void msiNativeForegroundQueue(const MsiNativeForegroundReadyWork* work){
 assert(work&&ymcc::sleeppower::powerAdmitted(work->power)&&work->edge==g_msiNativeForegroundEdge.load());
 ++sharedReadyUiEvents;
}
std::atomic<bool>& isolationReadyFixtureDeferred=g_msiNativeForegroundWorkPowerDeferred;
'''
    if ready_work_current:
        ready_work_ports += 'static bool inputHostExitFenceRaised(){return false;}\n'
else:
    ready_work_ports = 'std::atomic<bool>& isolationReadyFixtureDeferred=g_msiNativeForegroundReturnPowerDeferred;'
functions += '\n' + hardware_write_allowed + r'''
static ymcc::sleeppower::SharedPowerV1 isolationReadyFixtureObservation(){
 ymcc::sleeppower::SharedPowerV1 value;
 value.generation=currentPowerGeneration();value.phase=static_cast<std::uint32_t>(g_powerLifecycle.load());
 value.flags=ymcc::sleeppower::kKnown|(hardwareWriteAllowed()?ymcc::sleeppower::kHardwareAllowed:0u)|
    (g_sleepPowerDispatch.pendingSuspend()?ymcc::sleeppower::kNotificationPending:0u);
 value.observedNotificationSequence=g_sleepPowerDispatch.observedSequence();
 value.suspendSerial=isolationReadyFixtureSuspendSerial;
 return value;
}
static int __cdecl isolationReadyFixtureRead(ymcc::sleeppower::SharedPowerV1* output,std::uint32_t bytes) noexcept {
 if(!output||bytes<sizeof(*output)||g_exitRequested.load())return 0;
 *output=isolationReadyFixtureObservation();return 1;
}
static bool captureSharedPowerLifecycle(ymcc::sleeppower::SharedPowerV1& power) noexcept {
 return ymcc::sleeppower::capturePower(&isolationReadyFixtureRead,power);
}
static bool sharedPowerLifecycleCurrent(const ymcc::sleeppower::SharedPowerV1& power) noexcept {
 return ymcc::sleeppower::powerCurrent(&isolationReadyFixtureRead,power);
}
''' + '\n' + ready_work_current + '\n' + deferred_ready + '\n' + shared_power_ready + '\n' + shared_power_hold_release + '\n'
# Only the added source callback gets a fake window post; restore the original
# boundary macro before compiling every existing wake/retry handler.
notification_callback = function('suspendResumeSubscriptionCallback')
dispatch_constant_names = ('kSleepPowerForwardMessage', 'kSharedPowerStateMessage', 'kSleepPowerRedeliveryTimer')
dispatch_constant_declarations = []
for constant_name in dispatch_constant_names:
    matches = list(re.finditer(r'\bstatic\s+constexpr\s+(?:UINT|UINT_PTR)\s+' + re.escape(constant_name) + r'\s*=\s*[^;{}]+;', masked))
    if len(matches) != 1:
        raise RuntimeError('native dispatch constant not unique: ' + constant_name)
    match = matches[0]
    dispatch_constant_declarations.append(source[match.start():match.end()])
dispatch_constants = '\n'.join(dispatch_constant_declarations)
assert 'g_sleepPowerDispatch.callback(' in notification_callback, 'notification callback must consume the actual shared Dispatch'
assert 'kSleepPowerForwardMessage' in notification_callback, 'notification callback must forward its sequenced private message'
functions += '\n#pragma push_macro("PostMessageW")\n#undef PostMessageW\n#define PostMessageW notificationPostMessage\n' + notification_callback + '\n#pragma pop_macro("PostMessageW")\n'
assert 'availability["sleepPowerNotifications"]=g_sleepNotificationEvidence.snapshot();' in source
assert 'g_sleepNotificationEvidence.registered(rc);' in source
assert 'g_sleepNotificationEvidence.handled(static_cast<ULONG>(w),GetTickCount64());' in case_body('case WM_POWERBROADCAST:', 'case WM_TRAYICON:')

handlers = '\n'.join([
    'int handle506(WPARAM w, LPARAM l) {\n' + case_body('case WM_SG_S0_INTENT:', 'case WM_SG_S0_WAKE:') + '\n}',
    'int handle507Native(WPARAM w, LPARAM l) {\n' + case_body('case WM_SG_S0_WAKE:', 'case WM_SG_S4_WAKE:') + '\n}',
    'int resumeAutomatic() {\n' + body_after('else if (w == PBT_APMRESUMEAUTOMATIC)') + '\nreturn TRUE;\n}',
    'int resumeInteractive() {\n' + body_after('else if (w == PBT_APMRESUMESUSPEND)') + '\nreturn TRUE;\n}',
    'int resumeCritical() {\n' + body_after('else if (w == PBT_APMRESUMECRITICAL)') + '\nreturn TRUE;\n}',
    'int handleS4(WPARAM w=currentPowerGeneration(), LPARAM l=static_cast<LPARAM>(sgNowFileTime())) {\n' + source[unique_anchor('case WM_SG_S4_WAKE:')+len('case WM_SG_S4_WAKE:'):unique_anchor('case WM_SG_RESUME_GAME_FOCUS:')] + '\n}',
])
suspend = body_after('else if (w == PBT_APMSUSPEND)')
# Extract the actual entry classifier and types as well as the complete body.
# Memory ports below replace only leaves; no admission predicate is copied.
suspend_admission_types = source[
    unique_anchor('enum class SgSuspendAdmissionKind'):
    unique_anchor('static SgSuspendAdmission sgAdmitSuspendNotification(')]
suspend_admission_helper = function('sgAdmitSuspendNotification')
suspend_ignore_helper = function('sgIgnoreLateSuspendNotification')
functions += '\n' + suspend_admission_types + '\n' + suspend_admission_helper + '\n' + suspend_ignore_helper + '\n'
power_broadcast = case_body('case WM_POWERBROADCAST:', 'case WM_TRAYICON:')
power_prefix = power_broadcast[:power_broadcast.index('else if (w == PBT_APMSUSPEND)')]
assert not re.search(r'if\s*\(\s*w\s*==\s*PBT_APMSUSPEND\s*\)\s*msiNativeForegroundPowerEvent\s*\(', mask_cpp(power_prefix)), 'unconditional outer suspend hook retained'
entry_admission = re.search(
    r'const\s+auto\s+suspendAdmission\s*=\s*w\s*==\s*PBT_APMSUSPEND\s*\?\s*'
    r'sgAdmitSuspendNotification\s*\(\s*powerReceipt\s*\)\s*:\s*SgSuspendAdmission\s*\{\s*\}\s*;',
    mask_cpp(power_prefix))
assert entry_admission, 'shared body must consume the conditional entry admission'
assert len(re.findall(r'\bsgAdmitSuspendNotification\s*\(', mask_cpp(power_prefix))) == 1, 'entry admission must be classified once'
entry_ignore = re.search(
    r'if\s*\(\s*w\s*==\s*PBT_APMSUSPEND\s*&&\s*sgIgnoreLateSuspendNotification\s*\(\s*suspendAdmission\s*\)\s*\)\s*return\s+TRUE\s*;',
    mask_cpp(power_prefix))
assert entry_ignore and entry_admission.end() <= entry_ignore.start(), 'late suspend must return before independent consumers'
slow_suspend_leaves = ['sleepRecoveryAutoPrepareSuspend(']
screenpads_suspend_route = 'direct-before-independent-consumers'
if 'ymcc::screenpads::ownerOnSuspend()' in mask_cpp(power_prefix):
    slow_suspend_leaves.append('ymcc::screenpads::ownerOnSuspend()')
else:
    # Screenpads moved from a direct suspend call to the shared UI projection.
    # Require the actual replacement route; absence alone is not a waiver.
    assert 'ymcc::screenpads::ownerOnSuspend()' not in mask_cpp(power_broadcast)
    assert 'publishSharedPowerLifecycle("ordered-power-delivery-complete")' in power_prefix
    screenpads_projection = function('projectSharedPowerLifecycle')
    assert 'ymcc::screenpads::ownerSharedPowerChanged(power)' in screenpads_projection
    assert 'readSharedSystemPower(&power,sizeof(power))' in screenpads_projection
    screenpads_suspend_route = 'shared-current-power-projection'
for slow_leaf in slow_suspend_leaves:
    assert entry_admission.end() < power_prefix.index(slow_leaf), 'private admission must precede ' + slow_leaf
    assert entry_ignore.end() < power_prefix.index(slow_leaf), 'late suspend rejection must precede ' + slow_leaf
receipt_preamble = 'const auto powerReceipt=received?*received:g_sleepPowerDispatch.window(static_cast<ULONG>(w),GetTickCount64(),sgNowFileTime());\nstruct Done{ymcc::sleeppower::Dispatch::Receipt r;~Done(){g_sleepPowerDispatch.complete(r,GetTickCount64());}} done{powerReceipt};\n'
# The exact private observer now runs at the broadcast call site. Keep that
# part in selective extracts as well, while supporting an old-source control.
private_hook_start = power_prefix.find('if(suspendAdmission.privateIsolationRequired())', entry_ignore.end())
if private_hook_start < 0:
    assert 'if(admission.privateIsolationRequired())msiNativeForegroundPowerEvent(PBT_APMSUSPEND);' in suspend_admission_helper
    private_suspend_hook = ''
else:
    private_hook_end = power_prefix.index('});', private_hook_start) + len('});')
    private_suspend_hook = power_prefix[private_hook_start:private_hook_end] + '\n'
    assert 'powerConsumer("msi-foreground-suspend",' in private_suspend_hook
    assert 'msiNativeForegroundPowerEvent(' not in suspend_admission_helper
# Bind the same original observer boundary in the extracted query/suspend bodies.
# The complete-broadcast selection below keeps its own original local lambda.
consumer_at=power_broadcast.index('const auto powerConsumer=[&](')
consumer_open=power_broadcast.index('{',consumer_at)
consumer_end=braced(consumer_open,mask_cpp(power_broadcast))
assert power_broadcast[consumer_end]==';','complete original power consumer lambda'
consumer_binding='const auto type=static_cast<ULONG>(w);\n'+power_broadcast[consumer_at:consumer_end+1]+'\n'
receipt_preamble+=consumer_binding
handlers += '\nint handleSuspendAdmission(WPARAM w=PBT_APMSUSPEND,const ymcc::sleeppower::Dispatch::Receipt* received=nullptr) {\n'+receipt_preamble+power_prefix[entry_admission.start():entry_admission.end()]+'\nif(sgIgnoreLateSuspendNotification(suspendAdmission))return TRUE;\n' + private_suspend_hook + suspend + '\nreturn TRUE;\n}\n'
handlers += '\nint handleQueryAdmission(const ymcc::sleeppower::Dispatch::Receipt* received=nullptr) {\nconst WPARAM w=PBT_APMQUERYSUSPEND;\n'+receipt_preamble+body_after('else if (w == PBT_APMQUERYSUSPEND)')+'\nreturn TRUE;\n}\n'
# Execute the production retry-confirmation branch; non-retry setup is covered
# by the production sgMarkSleepTrigger and the single-PID companion selftest.
retry_start = suspend.index('if (suspendAdmission.kind==SgSuspendAdmissionKind::Retry)')
retry_open = suspend.index('{', retry_start)
retry_mask = mask_cpp(suspend)
retry_end = braced(retry_open, retry_mask)
handlers += '\nint confirmRetrySuspend() {\nconst WPARAM w=PBT_APMSUSPEND;\nconst ymcc::sleeppower::Dispatch::Receipt powerReceipt{};\n' + consumer_binding + '\nconst auto suspendAdmission=sgAdmitSuspendNotification({});\nif(sgIgnoreLateSuspendNotification(suspendAdmission))return FALSE;\n' + private_suspend_hook + suspend[retry_start:retry_end] + '\nreturn FALSE;\n}\n'

preamble = r'''
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>
#include "json.hpp"
#include "sleep_retry_budget.h"
#include "sleep_request_fallback.h"
#include "sleep_power_observation.h"
#include "sleep_notification_evidence.h"
#include "sleep_power_dispatch.h"
#include "sleep_cancel_ready.h"
#include "sleep_kernel_cycle.h"
#include "fan_physical_wake.h"
#include <new>
using json = nlohmann::json;
#define GetTickCount64 fakeGetTickCount64
#define SetTimer fakeSetTimer
#define KillTimer fakeKillTimer
static constexpr UINT_PTR SG_RETRY_TIMER_ID=0xA20C;
static constexpr ULONGLONG SG_USER_STANDBY_DEVICE_DELAY_MS=120000, SG_NON_USER_WAKE_CLASSIFY_MS=2000, SG_PAUSE_READY_WAIT_MAX_MS=2000;
static constexpr UINT SG_PAUSE_READY_POLL_MS=50;
static constexpr ULONGLONG SG_ENTRY_RETRY_DELAYS_MS[]={500,1000,2000};
static constexpr unsigned SG_MAX_ENTRY_RETRIES=3;
ymcc::sleeprepair::Budget g_sgAutomaticSleepBudget{3};
ymcc::fan_wake::Observation g_fanPhysicalWake;
static std::atomic<uint64_t> g_gamepadUiWakeGeneration{0},g_gamepadUiWakeSuspendSerial{0};
struct ClassificationMirrorPort {unsigned revocations=0;void revoke() noexcept {++revocations;}};
static ClassificationMirrorPort g_recoveryPowerBoundary; // External watchdog projection.
static void fanHostObserveKernelEntry(unsigned long long generation,ULONGLONG at){
 (void)g_fanPhysicalWake.recordSleep(generation,at);
}
// Record the public handoff; the physical Fan owner runs in its own fixture.
static struct {bool startup=false;std::uint64_t generation=0,wake=0,suspendSequence=0,entry=0;} fixtureFanS0Exit;
static void fanHostObserveKernelExit(unsigned long long generation,ULONGLONG wake,bool startup=false,
 std::uint64_t sequence=0,ULONGLONG entry=0) noexcept {fixtureFanS0Exit={startup,generation,wake,sequence,entry};}
static void fanHostRecordSharedHibernateWake(unsigned long long generation,ULONGLONG at){
 (void)g_fanPhysicalWake.recordSharedHibernateWake(generation,at);
}
static constexpr ULONGLONG SG_INTERNAL_SLEEP_506_WINDOW_MS=60000, SG_INTERNAL_SLEEP_506_CLOCK_SKEW_MS=2000;
static constexpr ULONGLONG SG_S0_ENTRY_FAILURE_WINDOW_MS=30000;
static constexpr UINT_PTR SG_MONITOR_ENTRY_FAILURE_TIMER_ID=0xA20E;
static constexpr UINT SG_MONITOR_ENTRY_FAILURE_WINDOW_MS=5000;
static constexpr ULONGLONG epoch=133000000000000000ULL;
enum class PowerLifecycle : uint8_t {Ready,Suspending,Suspended,Resuming};
enum class SgSleepMode : uint8_t {Unknown,S3,S4};
enum class SgRetryKind : uint8_t {None,EntryFailure,UnexpectedWake,SleepTimeout};
static void sgCancelEntryFailureRetry(const char*,SgRetryKind=SgRetryKind::EntryFailure,bool=false);
enum class SgWork : uint8_t {Suspend,WakeAutomatic,WakeSuspend,WakeHibernate};
/* actual-sg-work-item */
static std::atomic<uint64_t> g_sharedPowerSuspendSerial{1};
struct SgSleepTask {unsigned long long generation=0;SgSleepMode mode=SgSleepMode::Unknown;SgRetryKind retryKind=SgRetryKind::None;unsigned retryAttempt=0;ULONGLONG retryEntryFileTime=0;bool unexpectedWakeConsumed=false;} g_sgTask;
struct Internal {SgRetryKind kind=SgRetryKind::None;ULONGLONG dispatchTick=0,dispatchFileTime=0;} g_sgInternalSleepRequest;
struct SgSleepTarget {bool markerOwned=false;unsigned long long powerGeneration=0;};
struct SgResumeResult {int count=0;};
struct SgSystemSleepRequestResult {
 bool accepted=true,shutdownPrivilegeEnabled=true;DWORD shutdownPrivilegeError=0,requestError=0;
 const char* api="SetSuspendState";ymcc::sleeprequest::Mode target=ymcc::sleeprequest::Mode::Sleep;
 struct StubReceipt {bool called=false,accepted=false;std::uint64_t callSerial=0,generation=0,notificationSequence=0;};
 std::array<StubReceipt,static_cast<unsigned>(ymcc::sleeprequest::Mode::Hibernate)+1> apiCalls{};
};
std::atomic<ymcc::sleeprequest::Mode> g_sgAutomaticSleepTarget{ymcc::sleeprequest::Mode::None};
bool sgAutomaticClassicSleepPending(){const auto mode=g_sgAutomaticSleepTarget.load();return mode==ymcc::sleeprequest::Mode::Sleep||mode==ymcc::sleeprequest::Mode::Hibernate;}
HWND g_hwnd=reinterpret_cast<HWND>(1);
ULONGLONG nowTick=10000, nowFile=epoch+10000*10000ULL, generation=2, timerDue=0;
// The existing scalar generation remains the single in-memory authority.
struct SuspendGenerationPort {ULONGLONG fetch_add(ULONGLONG value,std::memory_order){const auto old=generation;generation+=value;return old;}ULONGLONG load(std::memory_order)const{return generation;}} g_powerGeneration;
std::atomic<ULONGLONG> g_resumeReadyGeneration{0};
ULONGLONG g_lastSuspendNotifyTick=0,g_lastSuspendReceiptTick=0;
bool g_sgPowerButtonSleepConfigured=false;
static constexpr UINT_PTR FOCUS_DISPLAY_TIMER_ID=0xA2FE;
unsigned isolationSuspendCalls=0,suspendLeafCalls=0,suspendQueued=0;
bool isolationHold=false,isolationResumePending=false;
SgSleepMode suspendConfiguredMode=SgSleepMode::S3;
void msiNativeForegroundPowerEvent(WPARAM event){assert(event==PBT_APMSUSPEND);++isolationSuspendCalls;isolationHold=true;isolationResumePending=false;}
void focusClearSession(){++suspendLeafCalls;}
void hidHideInvalidateRawInputIdentityCache(){++suspendLeafCalls;}
void inputHostArmWakePhysicalHold(const char*){++suspendLeafCalls;}
void sgHandheldSleepSensorsOnly(const char*){++suspendLeafCalls;}
void gamepadResetNativeState(bool){++suspendLeafCalls;}
void rogEnsureXboxFaceEnabled(const char*){++suspendLeafCalls;}
void fanHostScheduleEmergencySuspend(const char*,ULONGLONG,ULONGLONG=0){++suspendLeafCalls;}
bool sgHibernateAvailable(){++suspendLeafCalls;return false;}
SgSleepMode sgPowerButtonSleepMode(){++suspendLeafCalls;return suspendConfiguredMode;}
ULONGLONG g_sgIgnoredEarlyWakeFileTime=0,g_sgRetryDueTick=0,g_sgSleepTriggerTick=0,g_sgLastS0WakeTick=0,g_sgLastPowerButtonWakeTick=0,g_sgModernStandbyGeneration=0,g_sgModernWakeClassifiedTick=0,g_lastResumeNotifyTick=0;
double g_sgSleepTriggerEpoch=0;
std::atomic<ULONGLONG> g_sgLastPowerButtonSleepIntentFileTime{0},g_sgLastPowerButtonWakeEventFileTime{0},g_sgPauseWorkCompletedGeneration{0};
std::atomic<int> g_sgLastKernel506Reason{-1};
std::atomic<ULONGLONG> g_sgLatestUserSleepEntryFileTime{0},g_sgLatestSleepEntryFileTime{0},g_sgClosedSleepIntentFileTime{0},g_sgOriginalPowerButtonWakeFileTime{0};
ULONGLONG g_sgAcceptedS4WakeFileTime=0;
std::atomic<ULONGLONG> g_sgLastKernel506EventFileTime{0},g_sgLastKernel507EventFileTime{0};
std::atomic<bool> g_sensorSuspendCallbackActive{false};
static std::atomic<bool> g_exitRequested{false};
std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
std::atomic<bool> g_sgSleepCycleActive{false},g_sgGameActuallySuspended{false},g_sgInSuspend{false},g_inputReady{true},g_inputHostPowerAdmissionRevoked{false};
bool g_sgRetrySleepTimeout=false;
bool g_guardEnabled=true,g_sgResleepEnabled=true,g_sgRetryEntryFailure=true,g_sgRepairEligible=false,g_sgRetryInProgress=false,g_sgNonUserWakeObserved=false,g_sgSleepIntentArmed=false,g_sgModernStandbyActive=false,g_sgModernWakeClassified=false;
bool g_sgSleepQueryGateClosed=false,g_sgSleepQueryOwnsGeneration=false;
unsigned long long g_sgSleepQueryGeneration=0;
unsigned long long g_sgConfirmedSuspendGeneration=0;
int g_sgLastS0WakeReason=-1;
bool requestAccepted=true,keepInput=true,frozen=false,platformAoAc=true,platformCapsKnown=true;
int platformQueries=0;
bool sgPlatformIsAlwaysOnAlwaysConnected(){++platformQueries;return platformCapsKnown&&platformAoAc;}
ULONGLONG monitorTimerDue=0,g_entryFailureInterruptedAt=0,g_entryFailureQuickDeadline=0;
bool g_entryFailureExpanded=false;
int g_entryFailureDisplayState=-1;
int requestCount=0,resumeCount=0,beginCount=0,abortCount=0;
std::vector<std::pair<std::string,json>> facts;
const std::wstring SG_SLEEP_TRIGGER_MARKER=L"mock";
ULONGLONG fakeGetTickCount64(){return nowTick;}
ymcc::sleepnotifications::Evidence g_sleepNotificationEvidence;
ymcc::sleeppower::Dispatch g_sleepPowerDispatch;
ymcc::sleeppower::CancellationReady g_sgCancellationReady;
struct {bool isolated=false;} g_aiFanMockSession;
std::atomic<bool> g_hardwareWriteGate{true};
std::mutex g_msiNativeForegroundPowerMx;
static uint64_t isolationReadyFixtureSuspendSerial=0;
std::atomic<bool> g_msiNativeForegroundPowerHold{false},g_msiNativeForegroundResumePending{false},g_msiNativeForegroundPowerTransitionObserved{false};
std::atomic<bool> g_msiNativeForegroundReturnPowerDeferred{false};
std::atomic<bool> g_msiNativeForegroundWorkPowerDeferred{false};
static uint64_t g_msiNativeForegroundLastReadySuspendSerial=0;
std::atomic<uint64_t> g_msiNativeForegroundLastResumeGeneration{0};
std::atomic<uint64_t> g_lastPowerResumeCommitTick{0},g_msiNativeForegroundPowerHoldCommitTick{0},g_msiNativeForegroundSuspendedGeneration{0};
bool sharedReadyFamilyAllowed=true;
unsigned sharedReadyUiEvents=0;
void msiNativeForegroundQueue(){++sharedReadyUiEvents;}
bool nativeForegroundFamilyAllowed(){return sharedReadyFamilyAllowed;}
void msiNativeForegroundUiEvent(bool force=false,bool sourceReturn=false,const ymcc::sleeppower::SharedPowerV1* power=nullptr){
 assert(force&&!sourceReturn);
 if(power)assert(ymcc::sleeppower::powerAdmitted(*power));
 ++sharedReadyUiEvents;
}
/* actual-isolation-ready-deferred-binding */
/* actual-shared-power-dispatch-constants */
static bool notificationPostSucceeds=true;
static unsigned notificationPostCalls=0;
static unsigned notificationRedeliveryArms=0;
void sleepPowerArmRedelivery(HWND){++notificationRedeliveryArms;}
static unsigned fixtureWakeReadbackHints=0;
[[maybe_unused]] static void sgProbeMissingKernelWake() noexcept {++fixtureWakeReadbackHints;} // Separate source-readback boundary.
void inputCaptureSignalSensorPowerPause(){g_sensorSuspendCallbackActive.store(true);}
BOOL notificationPostMessage(HWND hwnd,UINT message,WPARAM type,LPARAM value){
 assert(hwnd==g_hwnd&&message==kSleepPowerForwardMessage&&value>0&&ymcc::sleepnotifications::Evidence::index(static_cast<ULONG>(type))>=0);
 ++notificationPostCalls;SetLastError(ERROR_INVALID_WINDOW_HANDLE);return notificationPostSucceeds?TRUE:FALSE;
}

ULONGLONG sgNowFileTime(){return nowFile;}
double sgNowEpoch(){return double(nowFile)/10000000;}
ULONGLONG currentPowerGeneration(){return generation;}
UINT_PTR fakeSetTimer(HWND,UINT_PTR id,UINT delay,TIMERPROC){if(id==SG_MONITOR_ENTRY_FAILURE_TIMER_ID){monitorTimerDue=nowTick+delay;return id;}assert(id==SG_RETRY_TIMER_ID);timerDue=nowTick+delay;return id;}
BOOL fakeKillTimer(HWND,UINT_PTR id){if(id==SG_MONITOR_ENTRY_FAILURE_TIMER_ID){monitorTimerDue=0;return TRUE;}if(id==FOCUS_DISPLAY_TIMER_ID){++suspendLeafCalls;return TRUE;}assert(id==SG_RETRY_TIMER_ID);timerDue=0;return TRUE;}
static void (*fixtureSleepFactHook)(const char*)=nullptr;
void sgRecordFact(const char* e,const json& d=json::object()){facts.emplace_back(e,d);if(fixtureSleepFactHook)fixtureSleepFactHook(e);}
const char* sgRetryKindName(SgRetryKind kind){return kind==SgRetryKind::EntryFailure?"entry-failure":kind==SgRetryKind::UnexpectedWake?"unexpected-wake":kind==SgRetryKind::SleepTimeout?"sleep-timeout":"none";}
std::string sgFormatFactFileTime(ULONGLONG t){return std::to_string(t);}
const char* sgKernelPowerReasonName(int){return "test";}
const char* powerLifecycleName(PowerLifecycle){return "test";}
template<class... T> void traceLog(const char*,T...){}
void stopPowerResumeWatchdog(){++suspendLeafCalls;}
void armPowerResumeWatchdog(ULONGLONG){}
void closeHardwareWriteGate(const char*){++suspendLeafCalls;}
void openHardwareWriteGate(){}
void closeJoyXoffAsync(const char*){}
void sgWriteFileAtomic(const std::wstring&,const std::string&){}
bool g_monitorEntryFailureTimerArmed=false,g_entryFailureDisplayOn=false;
ULONGLONG g_monitorEntryFailureGeneration=0,g_monitorEntryFailureIntent=0,g_monitorEntryFailureDeadline=0;
ULONGLONG g_entryFailureDisplayOnFileTime=0,g_entryFailureDisplayOffFileTime=0,g_entryFailureNewOffFileTime=0;
ULONGLONG successfulExit=0;
static bool sgSleepTimeoutSuccessAfter(ULONGLONG intent){return intent&&successfulExit>=intent;}
static void sgStopMonitorEntryFailureTimer(const char*);
void inputHostRequestPowerRelease(ULONGLONG,const char*){++suspendLeafCalls;}
void ipc_emit(const char*,const json&){++suspendLeafCalls;}
bool sgShouldKeepInputDuringStandby(){return keepInput;}
bool sgReadSleepTarget(SgSleepTarget& target){target={frozen,generation};return frozen;}
bool sgSleepLeaseOwnedByGeneration(ULONGLONG g){return frozen&&g==generation;}
SgResumeResult sgResumeSleepTarget(ULONGLONG g,bool){assert(g==generation);if(frozen){frozen=false;++resumeCount;}return {resumeCount};}
// Single fake-PID terminal only. These are mock owner facts, not acceptance
// of the actual S4 helper, recovery queue or retained on-disk orphan markers.
SgResumeResult sgResumeHibernateSleepTarget(ULONGLONG expectedGen,const std::function<bool()>& current=nullptr,ULONGLONG=0){
 if(current&&!current())return {};
 return sgResumeSleepTarget(expectedGen,false);
}
void sgReconcileSleepGameFlagsAfterRecoveryCancel(const char*){
 SgSleepTarget target;
 const bool mockOwnerHeld=sgReadSleepTarget(target)&&target.markerOwned;
 g_sgInSuspend.store(mockOwnerHeld,std::memory_order_release);
 g_sgGameActuallySuspended.store(mockOwnerHeld,std::memory_order_release);
}
// Inert radial leaves: no HWND, window creation, input injection or device I/O.
unsigned mockRadialInvalidations=0,mockRadialShutdownCalls=0;
void radialInputIsolationInvalidate(){++mockRadialInvalidations;}
struct MockRadialOverlay {
 bool open=false;
 bool active() const noexcept {return open;}
 void shutdown() noexcept {++mockRadialShutdownCalls;open=false;}
} g_radialOverlay;
// New timeout hooks are disabled memory ports in the original wake replay.
// The separate timeout fixture owns its timer, evidence, and shared-budget policy.
struct {bool active() const {return false;}} g_sgSleepTimeoutWindow;
static bool sgSleepTimeoutCurrent(){return false;}
// The existing default fixture keeps the independent timeout reentry owner inactive.
static bool sgSleepTimeoutReentryPending(){return false;}
static void sgSleepTimeoutStageReentry(ULONGLONG,const ymcc::sleeppower::Dispatch::Receipt&){}
static ymcc::sleeppower::KernelCycleObservations g_sgKernelCycles;
static void sgSleepTimeoutStop(const char*) noexcept {}
static void sgSleepTimeoutArm(int,ULONGLONG){assert(!g_sgRetrySleepTimeout);}
static bool sgSleepTimeoutDispatchPermitted(){return false;}
static void sgSleepTimeoutObserveSuspend(){}
static bool sgSleepTimeoutDeferClassification(){return false;}
static bool sgSleepTimeoutObservationHeld(unsigned long long){return false;}
static bool sgSleepTimeoutSettleClassification(unsigned long long){return false;}
static bool sgSleepTimeoutJoinEntry(int,ULONGLONG){return false;}
static void sgAbnormalArm(int,ULONGLONG){}
static void sgAbnormalStop(const char*){}
static void sgAbnormalInternalEntry(){}
// Companion runtime consumes the real implementation. Here unavailable observation
// must never authorize/block an already authorized parent request or reset its budget.
static bool sgAbnormalObserveOwnedRetry(ULONGLONG human,ULONGLONG request){
 assert(human && human<=request && request==g_sgInternalSleepRequest.dispatchFileTime);
 assert(g_sgInternalSleepRequest.kind==SgRetryKind::UnexpectedWake && g_sgAutomaticSleepBudget.requests()>0);
 return false;
}
static void sgAbnormalOwnedRetryResult(ULONGLONG human,ULONGLONG request,bool){assert(human && request>=human);}

static void sleepRecoveryAutoArm(unsigned long long){} // automatic owner separately tested; this fixture keeps opt-in off
bool sgBeginWakeGameRecovery(ULONGLONG g,const SgWorkItem* =nullptr){(void)sgResumeSleepTarget(g,true);return true;}
SgSystemSleepRequestResult sgRequestSystemSleep(ymcc::sleeprequest::Mode=ymcc::sleeprequest::Mode::ModernStandby,ymcc::sleeprequest::Mode=ymcc::sleeprequest::Mode::Sleep,bool=false,const ymcc::sleeppower::SharedPowerV1* =nullptr){
 ++requestCount;if(requestAccepted)timerDue=0;
 SgSystemSleepRequestResult result{requestAccepted,true,0,requestAccepted?0UL:5UL};
 result.apiCalls[static_cast<unsigned>(result.target)]={true,requestAccepted,static_cast<std::uint64_t>(requestCount),generation,g_sleepPowerDispatch.observedSequence()};
 return result;
} // mockSleepSenderEnd
json sgSleepEnvironmentSnapshot(){return json::object();}
static bool sgSleepRetryActive();
static void sgStartSleepRetry(SgRetryKind,const char*);
static void sgScheduleNextSleepRetry(const char*);
static void sgFinishSleepRetryFailure(const char*);
static void sgStopSleepRetry();
static void sgMarkSleepTrigger();
static void sgRealWake(const char*,unsigned long long,const SgWorkItem*);
static void sgClearUnexpectedWake(const char*,bool);
static bool sgS0EntryFailureEligible(int,ULONGLONG);
static bool sgKernelPowerWakePredatesLatestEntry(int,ULONGLONG,ULONGLONG*);
static void sgObserveUnattributedSleepRetryFailure(const char*,int=-1,ULONGLONG=0);
void sgQueueWork(SgWork w,ULONGLONG g,ULONGLONG=0){if(w==SgWork::Suspend)++suspendQueued;else if(w==SgWork::WakeSuspend){sgRealWake("resume_suspend",g,nullptr);g_powerLifecycle=PowerLifecycle::Ready;}}
void sgAbortSleepIntent(const char*, ULONGLONG){++abortCount;sgStopSleepRetry();sgRealWake("resume_suspend",generation,nullptr);g_powerLifecycle=PowerLifecycle::Ready;}
void sgMarkUserStandby(const char*,int,ULONGLONG){}
void sgBeginModernStandbyIntent(ULONGLONG=0){
 ++beginCount;
 if(g_sgTask.mode==SgSleepMode::Unknown){g_sgTask.mode=SgSleepMode::S3;g_sgSleepIntentArmed=true;sgMarkSleepTrigger();g_powerLifecycle=PowerLifecycle::Suspended;}
 else g_sgModernStandbyActive=true;
}
void handlePowerResumeNotification(SgWork w,const char*,SgWorkItem* =nullptr){
 if(sgSleepRetryActive()&&w!=SgWork::WakeSuspend)return;
 // The real common owner enters Resuming before it queues sgRealWake. This
 // inert port executes that queue synchronously; it must preserve the phase
 // seen by the real S4 worker, not call it while still Suspended.
 if(g_powerLifecycle==PowerLifecycle::Suspending||g_powerLifecycle==PowerLifecycle::Suspended)
  g_powerLifecycle=PowerLifecycle::Resuming;
 sgRealWake(w==SgWork::WakeSuspend?"resume_suspend":w==SgWork::WakeHibernate?"resume_hibernate":"resume_auto",generation,nullptr);
 g_powerLifecycle=PowerLifecycle::Ready;
}
static void sgHandleModernStandbyWake(bool,ULONGLONG);
void sgHandheldResumeInputOnWake(const char*){}
void inputHostRequestWakeRescan(ULONGLONG){}

'''
preamble = preamble.replace('/* actual-shared-power-dispatch-constants */', dispatch_constants)
if 'static bool sgContinuedWakeAge(' in timeout_native:
    anchor='SgSystemSleepRequestResult sgRequestSystemSleep('
    ports=('static bool sgContinuedWakeAge(ULONGLONG&);\nstatic void sgBindContinuedWake(int,ULONGLONG);\n'
        if uses_timeout_owner else 'static bool sgContinuedWakeAge(ULONGLONG& age){age=0;return true;}\nstatic void sgBindContinuedWake(int,ULONGLONG){}\n')
    assert preamble.count(anchor)==1
    preamble=preamble.replace(anchor,ports+anchor)
preamble = preamble.replace('/* actual-isolation-ready-deferred-binding */', ready_work_ports)
item_at=masked.index('struct SgWorkItem {')
work_item=source[item_at:braced(masked.index('{',item_at))]+';'
preamble=preamble.replace('/* actual-sg-work-item */',work_item)

if has_retry_timer_scope:
    at=source.index('struct SgSleepRetryFailureScope {');opening=source.index('{',at);masked_scope=mask_cpp(source);end=opening+1;depth=1
    while depth:depth+=(masked_scope[end]=='{')-(masked_scope[end]=='}');end+=1
    retry_scope=source[at:end]+';'
    old='static void sgFinishSleepRetryFailure(const char*);';assert preamble.count(old)==1
    preamble=preamble.replace(old,retry_scope+'\nstatic void sgFinishSleepRetryFailure(const char* reason, const SgSleepRetryFailureScope* failureScope = nullptr);')


checks = r'''
void at(ULONGLONG tick){nowTick=tick;nowFile=epoch+tick*10000;}
int handle507(WPARAM user,LPARAM reason){
 const auto stamp=user==1?g_sgLastPowerButtonWakeEventFileTime.load():g_sgLastKernel507EventFileTime.load();
 return handle507Native(static_cast<WPARAM>(reason),static_cast<LPARAM>(stamp?stamp:nowFile));
}
void reset(bool pause=false){
 g_exitRequested=false;
 at(10000);generation=2;timerDue=0;g_sgTask={};g_sgInternalSleepRequest={};g_sgConfirmedSuspendGeneration=0;
 monitorTimerDue=0;g_entryFailureInterruptedAt=0;g_entryFailureExpanded=false;g_entryFailureQuickDeadline=0;g_entryFailureDisplayState=-1;g_monitorEntryFailureTimerArmed=false;g_monitorEntryFailureGeneration=0;
 g_monitorEntryFailureIntent=g_monitorEntryFailureDeadline=0;g_entryFailureDisplayOn=false;
 g_entryFailureDisplayOnFileTime=g_entryFailureDisplayOffFileTime=g_entryFailureNewOffFileTime=0;successfulExit=0;
 g_sgLastKernel506Reason=-1;suspendConfiguredMode=SgSleepMode::S3;
 g_sgAutomaticSleepBudget.confirmedUserWake();g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::None;
 g_fanPhysicalWake.~Observation();new (&g_fanPhysicalWake) ymcc::fan_wake::Observation();g_fanPhysicalWake.generation(generation);
 g_sgLastPowerButtonSleepIntentFileTime=0;g_sgLastPowerButtonWakeEventFileTime=0;g_sgPauseWorkCompletedGeneration=2;
 g_sgLatestUserSleepEntryFileTime=0;g_sgLatestSleepEntryFileTime=0;g_sgClosedSleepIntentFileTime=0;g_sgOriginalPowerButtonWakeFileTime=0;g_sgAcceptedS4WakeFileTime=0;
 g_guardEnabled=true;g_sgResleepEnabled=true;g_sgRetryEntryFailure=true;g_sgRetrySleepTimeout=false;g_sgRepairEligible=false;
 g_sgLastKernel506EventFileTime=0;g_sgLastKernel507EventFileTime=0;g_sensorSuspendCallbackActive=false;
 g_sgRetryInProgress=false;g_sgNonUserWakeObserved=false;g_sgIgnoredEarlyWakeFileTime=0;g_sgSleepIntentArmed=false;
 g_sgModernStandbyActive=false;g_sgModernWakeClassified=false;g_sgModernWakeClassifiedTick=0;
 g_sgSleepCycleActive=false;g_sgGameActuallySuspended=false;g_sgInSuspend=false;
 g_powerLifecycle=PowerLifecycle::Ready;g_sgRetryDueTick=0;g_sgSleepTriggerTick=0;
 requestAccepted=true;keepInput=true;frozen=pause;requestCount=resumeCount=beginCount=abortCount=0;facts.clear();
}
void sleepIntent(){
 g_sgTask.generation=generation;g_sgTask.mode=SgSleepMode::S3;g_sgSleepIntentArmed=true;
 g_powerLifecycle=PowerLifecycle::Suspending;sgMarkSleepTrigger();g_powerLifecycle=PowerLifecycle::Suspended;
 g_sgInSuspend=frozen;g_sgGameActuallySuspended=frozen;g_sgPauseWorkCompletedGeneration=generation;
}
void dispatch(){assert(timerDue);at((std::max)(nowTick,timerDue));sgDispatchSameModeRetry();}
void powerButton(){g_sgLastPowerButtonWakeEventFileTime=nowFile;handle507(1,1);}
int passed=0;
void pass(const char* label){++passed;std::cout<<"PASS "<<label<<"\n";}
void advance(ULONGLONG tick){
 unsigned loops=0;
 while(timerDue&&timerDue<tick){assert(++loops<8);dispatch();}
 at(tick);
}
void replay(const char* path){
 std::ifstream f(path);json fixture;f>>fixture;reset();
 if(fixture.value("kind",std::string{})=="archived-delayed-entry"){
  const auto original=std::stoull(fixture["entryFileTime"].get<std::string>());
  const auto normalized=epoch+10000*10000ULL;
  const auto eventTime=[&](const json& value){return normalized+std::stoull(value.get<std::string>())-original;};
  for(const auto& e:fixture["events"]){
   nowFile=eventTime(e["observedFileTime"]);nowTick=(nowFile-epoch)/10000ULL;
   const auto kind=e["kind"].get<std::string>();
   if(kind=="kernel-callback"){
    const int id=e["id"],reason=e["reason"];const auto stamp=eventTime(e["eventFileTime"]);
    if(id==506){sgRememberKernelSleepEntry(reason,stamp);g_sgLastKernel506Reason=reason;g_sgLastKernel506EventFileTime=stamp;}
    else{g_sgLastKernel507EventFileTime=stamp;if(reason==1){g_sgLastPowerButtonWakeEventFileTime=stamp;sgPublishSleepEventTime(g_sgOriginalPowerButtonWakeFileTime,stamp);}}
   }else if(kind=="suspend")sleepIntent();
   else if(kind=="resume-auto")resumeAutomatic();
   else if(kind=="resume-interactive")resumeInteractive();
   else if(kind=="kernel-gui"){
    const int id=e["id"],reason=e["reason"];const auto stamp=eventTime(e["eventFileTime"]);
    if(id==506)handle506(reason,stamp);else handle507(reason==1?1:2,reason);
   }else assert(false&&"unknown archived event kind");
  }
  ULONGLONG originalAgeMs=0;
  assert(sgExternalDeviceWakeIntentAge(originalAgeMs));
  assert(originalAgeMs==(g_sgLastKernel507EventFileTime.load()-normalized)/10000ULL);
  if(timerDue)dispatch();
  assert(requestCount==fixture["expectedWakeCyclesAfterFix"].get<int>());
  assert(resumeCount==0);
  std::cout<<"PASS fixture "<<path<<": original delayed506 age is retained; "<<requestCount<<" guarded wake request (mocked)\n";++passed;return;
 }
 if(fixture.value("kind",std::string{})=="archived-entry-interruption"){
  for(const auto& e:fixture["events"]){
   reset();g_sgResleepEnabled=false;g_sgLastKernel506Reason=1;
   sgRememberKernelSleepEntry(1,nowFile);handle506(1,nowFile);
   const auto intent=nowFile,delta=e["exitDelta100ns"].get<ULONGLONG>();
   at(10000+(delta+9999)/10000);
   handle507Native(e["reason"].get<int>(),static_cast<LPARAM>(intent+delta));
   assert(sgSleepRetryActive()&&g_sgTask.retryKind==SgRetryKind::EntryFailure);
   dispatch();assert(requestCount==1);
  }
  std::cout<<"PASS fixture "<<path<<": "<<fixture["events"].size()<<" archived Win5 mouse interruptions, isolated per-record counterfactuals (mocked)\n";++passed;return;
 }
 for(const auto& e:fixture["events"]){
  advance(e["atMs"].get<ULONGLONG>());std::string kind=e["kind"];
  if(kind=="broadcast"){
   int code=e["code"];
   if(code==4){if(!confirmRetrySuspend())sleepIntent();}
   else if(code==18)resumeAutomatic();else if(code==7)resumeInteractive();
  }else if(kind=="kernel"){
   int id=e["id"],reason=e["reason"];
   if(id==506){const auto stamp=epoch+e["eventAtMs"].get<ULONGLONG>()*10000;sgRememberKernelSleepEntry(reason,stamp);g_sgLastKernel506Reason=reason;handle506(reason,stamp);}
   else{const auto stamp=epoch+e["eventAtMs"].get<ULONGLONG>()*10000;g_sgLastKernel507EventFileTime=stamp;if(reason==1){g_sgLastPowerButtonWakeEventFileTime=stamp;sgPublishSleepEventTime(g_sgOriginalPowerButtonWakeFileTime,stamp);}handle507(reason==1?1:2,reason);}
  }else if(kind=="device")sgNoteExternalDeviceNodeChange();else sgNoteExternalDeviceAcDcChange();
 }
 advance(nowTick+10000);
 assert(requestCount==fixture["expectedWakeCycles"].get<int>());
 assert(resumeCount==0);
 std::cout<<"PASS fixture "<<path<<": "<<requestCount<<" guarded USB4 wake cycles submit SetSuspendState (mocked)\n";++passed;
}

void verifyActualFanRetryBoundary(){
 reset();sleepIntent();const auto physical=g_fanPhysicalWake.recordSleep(generation,nowFile-20);
 assert(physical&&g_fanPhysicalWake.recordWake(generation,nowFile-10,false));assert(g_fanPhysicalWake.physical(physical));
 sgStartSleepRetry(SgRetryKind::EntryFailure,"pure-fan-retry-boundary");
 assert(sgSleepRetryActive()&&timerDue&&requestCount==0&&!g_fanPhysicalWake.physical(physical)&&g_fanPhysicalWake.awaitingWake(physical));
 auto stale=ymcc::fan_wake::Observation();const auto g=stale.recordSleep(1,100);assert(g&&stale.recordWake(1,110,false));
 stale.retryBoundary(2,120);assert(stale.physical(g));stale.retryBoundary(1,120);assert(!stale.physical(g)&&stale.awaitingWake(g));
 assert(!stale.recordWake(1,119,false)&&!stale.recordWake(1,130,true));
 std::cout<<"PASS actual fan pure-state retry boundary clears wake only for the matching shared generation; no hardware terminal\n";
}

void verifyPowerNotificationEvidence(){
 unsigned checks=0;
 const auto require=[&](bool value){++checks;assert(value);};
 auto row=g_sleepNotificationEvidence.snapshot();
 require(row["registrationAttempted"]==false&&row["registrationError"].is_null());
 require(row["forwarding"]["lastFailureWin32Error"].is_null()&&row["powerActionAuthorized"]==false&&row["userWakeProven"]==false);
 g_sleepNotificationEvidence.registered(ERROR_ACCESS_DENIED);row=g_sleepNotificationEvidence.snapshot();
 require(row["registrationAttempted"]==true&&row["registrationError"]==ERROR_ACCESS_DENIED);
 g_sleepNotificationEvidence.registered(ERROR_SUCCESS);
 const auto phase=g_powerLifecycle.load();const auto gen=generation;
 const auto savedHwnd=g_hwnd;const auto requests=requestCount;const auto resumes=resumeCount;
 require(suspendResumeSubscriptionCallback(nullptr,PBT_APMPOWERSTATUSCHANGE,nullptr)==ERROR_SUCCESS&&notificationPostCalls==0);
 g_hwnd=nullptr;
 require(suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr)==ERROR_SUCCESS&&g_sensorSuspendCallbackActive.load());
 row=g_sleepNotificationEvidence.snapshot();require(row["callbackEntries"]["suspend"]==1&&row["forwarding"]["noWindow"]==1&&row["forwarding"]["posted"]==0);
 g_hwnd=savedHwnd;notificationPostSucceeds=true;
 require(suspendResumeSubscriptionCallback(nullptr,PBT_APMSUSPEND,nullptr)==ERROR_SUCCESS&&notificationPostCalls==1);
 notificationPostSucceeds=false;
 require(suspendResumeSubscriptionCallback(nullptr,PBT_APMRESUMEAUTOMATIC,nullptr)==ERROR_SUCCESS&&!g_sensorSuspendCallbackActive.load());
 row=g_sleepNotificationEvidence.snapshot();require(row["forwarding"]["failed"]==1&&row["forwarding"]["lastFailureWin32Error"]==ERROR_INVALID_WINDOW_HANDLE);
 notificationPostSucceeds=true;
 require(suspendResumeSubscriptionCallback(nullptr,PBT_APMRESUMESUSPEND,nullptr)==ERROR_SUCCESS);
 require(suspendResumeSubscriptionCallback(nullptr,PBT_APMRESUMECRITICAL,nullptr)==ERROR_SUCCESS);
 row=g_sleepNotificationEvidence.snapshot();
 require(row["callbackEntries"]==json({{"suspend",2},{"resumeAutomatic",1},{"resumeSuspend",1},{"resumeCritical",1}}));
 require(row["forwarding"]["posted"]==3&&row["forwarding"]["failed"]==1&&row["forwarding"]["lastFailureWin32Error"]==ERROR_INVALID_WINDOW_HANDLE&&notificationPostCalls==4);
 for(const ULONG type:{PBT_APMSUSPEND,PBT_APMRESUMEAUTOMATIC,PBT_APMRESUMESUSPEND,PBT_APMRESUMECRITICAL})g_sleepNotificationEvidence.handled(type,nowTick);
 g_sleepNotificationEvidence.handled(PBT_APMPOWERSTATUSCHANGE,nowTick);
 row=g_sleepNotificationEvidence.snapshot();
 require(row["windowHandlerEntries"]==json({{"suspend",1},{"resumeAutomatic",1},{"resumeSuspend",1},{"resumeCritical",1}}));
 require(row["registrationError"]==ERROR_SUCCESS&&row["lastCallbackTickMs"]==nowTick&&row["lastWindowHandlerTickMs"]==nowTick);
 require(row["mandatoryEvidence"]==true&&row["detailLogsRequired"]==false&&row["snapshotAtomic"]==false&&row["callbackDiskWrites"]==false);
 require(row["powerActionAuthorized"]==false&&row["sleepSuccessProven"]==false&&row["userWakeProven"]==false);
 require(g_powerLifecycle.load()==phase&&generation==gen&&requestCount==requests&&resumeCount==resumes);
 std::cout<<"POWER_NOTIFICATION_EVIDENCE "<<json({{"checks",checks},{"actualCallbackBound",true},{"realWindowPosts",0},{"newPowerActions",0}}).dump()<<"\n";
}

void verifySharedPowerDispatch(){
 using Dispatch=ymcc::sleeppower::Dispatch;
 using Receipt=Dispatch::Receipt;
 std::vector<std::string> scenarios;
 // All deliveries below call the actual header directly. A captured receipt
 // is the only fake PostMessage leaf: no queue, predicate model or OS thread.
 {
  Dispatch dispatch;Receipt queued;unsigned posts=0;
  assert(dispatch.callback(PBT_APMSUSPEND,100,[&](const Receipt& receipt){queued=receipt;++posts;return true;}));
  assert(posts==1&&queued&&queued.callback&&queued.tick==100&&dispatch.pendingSuspend()&&dispatch.hasPending());
  assert(!dispatch.window(PBT_APMRESUMEAUTOMATIC,101));
  const auto first=dispatch.forwarded(queued.sequence,queued.type);
  assert(first&&first.tick==100&&dispatch.pendingSuspend());
  dispatch.complete(first);
  const auto pending=dispatch.pendingReplay();
  assert(pending&&pending.sequence>queued.sequence&&!pending.callback&&pending.type==PBT_APMRESUMEAUTOMATIC);
  const auto resume=dispatch.forwarded(pending.sequence,pending.type);
  assert(resume&&resume.tick==101);
  dispatch.complete(resume);
  assert(!dispatch.pendingSuspend()&&!dispatch.hasPending());
  assert(!dispatch.forwarded(queued.sequence,queued.type));
  const auto stale=dispatch.snapshot();
  assert(stale["staleForwardedIgnored"]==1&&stale["claimed"]==resume.sequence&&stale["completed"]==resume.sequence&&stale["consumerActive"]==false);
  scenarios.emplace_back("callback-suspend-delivers-before-direct-resume-and-rejects-replay");
  Receipt later;
  assert(dispatch.callback(PBT_APMSUSPEND,102,[&](const Receipt& receipt){later=receipt;++posts;return true;}));
  assert(later.sequence>resume.sequence&&dispatch.pendingSuspend());
  const auto accepted=dispatch.forwarded(later.sequence,later.type);
  assert(accepted&&accepted.sequence==later.sequence&&accepted.tick==102&&accepted.type==PBT_APMSUSPEND);
  dispatch.complete(accepted);
  assert(!dispatch.pendingSuspend()&&!dispatch.hasPending()&&!dispatch.pendingReplay());
  assert(dispatch.snapshot()["completed"]==later.sequence);
  scenarios.emplace_back("genuine-successor-suspend-still-accepted");
 }
 {
  Dispatch dispatch;Receipt failed;
  assert(!dispatch.callback(PBT_APMSUSPEND,200,[&](const Receipt& receipt){failed=receipt;return false;}));
  assert(failed&&dispatch.pendingSuspend()&&dispatch.hasPending()&&dispatch.snapshot()["failedPosts"]==1);
  const auto candidate=dispatch.pendingReplay();
  assert(candidate&&candidate.sequence==failed.sequence&&candidate.type==failed.type&&candidate.tick==200);
  const auto replay=dispatch.forwarded(candidate.sequence,candidate.type);
  assert(replay&&replay.sequence==failed.sequence&&dispatch.pendingSuspend()&&!dispatch.pendingReplay());
  dispatch.complete(replay);
  assert(!dispatch.pendingSuspend()&&!dispatch.hasPending()&&!dispatch.pendingReplay());
  assert(!dispatch.forwarded(replay.sequence,replay.type));
  dispatch.complete(replay); // repeated completion must not create another job
  const auto state=dispatch.snapshot();
  assert(state["replayDeliveries"]==1&&state["failedPosts"]==1&&state["completed"]==failed.sequence&&state["consumerActive"]==false);
  scenarios.emplace_back("failed-post-replayed-completed-once-with-no-pending");
 }
 {
  Dispatch dispatch;
  const auto oldResume=dispatch.window(PBT_APMRESUMESUSPEND,300);
  assert(oldResume&&!dispatch.pendingSuspend());
  Receipt newerSuspend;
  assert(dispatch.callback(PBT_APMSUSPEND,301,[&](const Receipt& receipt){newerSuspend=receipt;return true;}));
  assert(dispatch.pendingSuspend()&&dispatch.hasPending()&&!dispatch.pendingReplay());
  dispatch.complete(oldResume);
  assert(dispatch.pendingSuspend()&&dispatch.hasPending());
  const auto candidate=dispatch.pendingReplay();
  assert(candidate&&candidate.sequence==newerSuspend.sequence);
  const auto next=dispatch.forwarded(candidate.sequence,candidate.type);
  assert(next&&next.type==PBT_APMSUSPEND&&dispatch.pendingSuspend());
  dispatch.complete(next);
  assert(!dispatch.pendingSuspend()&&!dispatch.hasPending()&&!dispatch.pendingReplay());
  scenarios.emplace_back("old-resume-completion-preserves-new-suspend-public-fence");
 }
 {
  Dispatch dispatch;
  const auto outer=dispatch.window(PBT_APMRESUMEAUTOMATIC,400);
  assert(outer);
  Receipt forwardedSuspend;
  assert(dispatch.callback(PBT_APMSUSPEND,401,[&](const Receipt& receipt){forwardedSuspend=receipt;return true;}));
  assert(!dispatch.forwarded(forwardedSuspend.sequence,forwardedSuspend.type));
  assert(!dispatch.window(PBT_APMSUSPEND,402));
  assert(!dispatch.window(PBT_APMRESUMESUSPEND,403));
  assert(!dispatch.pendingReplay()&&dispatch.pendingSuspend()&&dispatch.hasPending());
  const auto busy=dispatch.snapshot();
  assert(busy["consumerActive"]==true&&busy["claimed"]==outer.sequence&&busy["deferredReentrantDeliveries"]==3);
  dispatch.complete(outer);
  assert(dispatch.pendingSuspend());
  const auto selected=dispatch.pendingReplay();
  assert(selected&&selected.tick==402&&selected.type==PBT_APMSUSPEND);
  assert(!dispatch.forwarded(forwardedSuspend.sequence,forwardedSuspend.type));
  const auto suspend=dispatch.forwarded(selected.sequence,selected.type);
  assert(suspend&&dispatch.pendingSuspend());
  dispatch.complete(suspend);
  const auto candidate=dispatch.pendingReplay();
  assert(candidate&&candidate.tick==403&&candidate.type==PBT_APMRESUMESUSPEND);
  const auto latest=dispatch.forwarded(candidate.sequence,candidate.type);
  assert(latest&&latest.sequence==candidate.sequence);
  dispatch.complete(latest);
  assert(!dispatch.pendingSuspend()&&!dispatch.hasPending()&&!dispatch.pendingReplay());
  assert(!dispatch.forwarded(forwardedSuspend.sequence,forwardedSuspend.type));
  const auto done=dispatch.snapshot();
  assert(done["issued"]==4&&done["claimed"]==latest.sequence&&done["completed"]==latest.sequence&&done["replayDeliveries"]==2&&done["consumerActive"]==false);
  scenarios.emplace_back("reentrant-delivery-retains-latest-suspend-and-successor-resume");
 }
 assert(scenarios.size()==5);
 std::cout<<"SHARED_POWER_DISPATCH "<<json({{"scenarios",scenarios},{"groups",scenarios.size()},
  {"actualHeader","native/sleep_power_dispatch.h"},{"inMemoryOnly",true},
  {"realWindowPosts",0},{"realTimers",0},{"createdHandles",0},{"createdThreads",0},
  {"realOsSleep",false},{"hardwarePassed",false}}).dump()<<"\n";
}

void verifySharedPowerReady(){
 // Finish the original evidence callback's actual pending receipt in memory.
 // No new Dispatch model, timer, message, Host or device terminal is used.
 const auto drain=[](){
  const auto pending=g_sleepPowerDispatch.pendingReplay();
  if(pending){const auto receipt=g_sleepPowerDispatch.forwarded(pending.sequence,pending.type);assert(receipt);g_sleepPowerDispatch.complete(receipt);}
  assert(!g_sleepPowerDispatch.hasPending()&&!g_sleepPowerDispatch.pendingSuspend()&&g_sleepPowerDispatch.snapshot()["consumerActive"]==false);
 };
 const auto prepare=[&](){
  drain();reset();g_inputReady=false;g_hardwareWriteGate=true;g_aiFanMockSession.isolated=false;
  sharedReadyFamilyAllowed=true;sharedReadyUiEvents=0;
  g_msiNativeForegroundPowerHold=true;g_msiNativeForegroundResumePending=false;
  g_msiNativeForegroundPowerTransitionObserved=true;g_msiNativeForegroundLastResumeGeneration=0;
  isolationReadyFixtureDeferred=false;
  isolationReadyFixtureSuspendSerial=0;g_msiNativeForegroundLastReadySuspendSerial=0;
 };
 prepare();const auto ready=isolationReadyFixtureObservation();
 assert(hardwareWriteAllowed());msiNativeForegroundSharedPowerReady(ready);
 assert(!g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundResumePending&&!g_msiNativeForegroundPowerTransitionObserved);
 assert(g_msiNativeForegroundLastResumeGeneration==ready.generation&&sharedReadyUiEvents==1&&!g_inputReady);
 msiNativeForegroundSharedPowerReady(ready);
 assert(sharedReadyUiEvents==1&&!g_inputReady);
 prepare();auto stale=isolationReadyFixtureObservation();++stale.generation;msiNativeForegroundSharedPowerReady(stale);
 assert(g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundResumePending&&g_msiNativeForegroundPowerTransitionObserved&&sharedReadyUiEvents==0);
 prepare();stale=isolationReadyFixtureObservation();g_powerLifecycle=PowerLifecycle::Suspending;msiNativeForegroundSharedPowerReady(stale);
 assert(g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundResumePending&&sharedReadyUiEvents==0);
 prepare();stale=isolationReadyFixtureObservation();g_exitRequested=true;msiNativeForegroundSharedPowerReady(stale);
 assert(g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundResumePending&&sharedReadyUiEvents==0);
 prepare();stale=isolationReadyFixtureObservation();ymcc::sleeppower::Dispatch::Receipt pending;
 assert(!g_sleepPowerDispatch.callback(PBT_APMSUSPEND,500,[&](const auto& receipt){pending=receipt;return false;}));
 assert(pending&&g_sleepPowerDispatch.pendingSuspend()&&!hardwareWriteAllowed());
 msiNativeForegroundSharedPowerReady(stale);
 assert(g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundResumePending&&sharedReadyUiEvents==0);
 drain();msiNativeForegroundSharedPowerReady(isolationReadyFixtureObservation());
 assert(!g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundResumePending&&sharedReadyUiEvents==1&&!g_inputReady);
 prepare();g_msiNativeForegroundPowerHold=false;g_msiNativeForegroundPowerTransitionObserved=false;
 const auto previousReady=isolationReadyFixtureObservation();
 g_msiNativeForegroundLastResumeGeneration=previousReady.generation;
 g_msiNativeForegroundLastReadySuspendSerial=previousReady.suspendSerial;
 msiNativeForegroundSharedPowerReady(previousReady);
 assert(sharedReadyUiEvents==0);
 ++generation;g_inputReady=true;
 const auto resumeOnlyReady=isolationReadyFixtureObservation();
 assert(resumeOnlyReady.generation!=previousReady.generation&&resumeOnlyReady.suspendSerial==previousReady.suspendSerial);
 msiNativeForegroundSharedPowerReady(previousReady);
 assert(sharedReadyUiEvents==0&&g_msiNativeForegroundLastResumeGeneration==previousReady.generation);
 msiNativeForegroundSharedPowerReady(resumeOnlyReady);msiNativeForegroundSharedPowerReady(resumeOnlyReady);
 assert(sharedReadyUiEvents==1&&g_msiNativeForegroundLastResumeGeneration==resumeOnlyReady.generation);
 std::cout<<"SHARED_POWER_READY "<<json({{"groups",1},{"sameGeneration",true},{"inputSourceStillNotReady",true},
  {"rawResumeRequired",false},{"staleGenerationRejected",true},{"suspendingRejected",true},{"exitRejected",true},
  {"publicPendingRejected",true},{"readyReplayUiEvents",1},{"resumeOnlyNewGenerationUiEvents",1},
  {"realHardwareOperations",0},{"realHostOperations",0}}).dump()<<"\n";

 // The old maintenance path can run between the local Ready store and the
 // public gate opening, or after a newer notification arrived at its source.
 const auto prepareHold=[&](){
  prepare();g_inputReady=true;g_resumeReadyGeneration=currentPowerGeneration();
  g_lastPowerResumeCommitTick=200;g_msiNativeForegroundPowerHoldCommitTick=100;
  g_msiNativeForegroundSuspendedGeneration=currentPowerGeneration();
 };
 const auto held=[](){return !msiNativeForegroundTryReleasePowerHold()&&
  g_msiNativeForegroundPowerHold&&g_msiNativeForegroundPowerTransitionObserved&&
  g_msiNativeForegroundLastResumeGeneration==0&&sharedReadyUiEvents==0;};
 const auto releasedOnce=[](){return msiNativeForegroundTryReleasePowerHold()&&
  !g_msiNativeForegroundPowerHold&&!g_msiNativeForegroundResumePending&&
  g_msiNativeForegroundLastResumeGeneration==currentPowerGeneration()&&
  !msiNativeForegroundTryReleasePowerHold()&&sharedReadyUiEvents==1;};
 prepareHold();g_hardwareWriteGate=false;
 const bool closedGate=held();g_hardwareWriteGate=true;
 const bool afterGate=releasedOnce();
 prepareHold();
 assert(!g_sleepPowerDispatch.callback(PBT_APMSUSPEND,501,[](const auto&){return false;}));
 const bool pendingGate=held();
 drain();const bool afterDrain=releasedOnce();
 prepareHold();g_lastPowerResumeCommitTick=100;
 const bool unchangedCommitHeld=held();
 msiNativeForegroundSharedPowerReady(isolationReadyFixtureObservation());
 const bool cancelledByProjection=!g_msiNativeForegroundPowerHold&&sharedReadyUiEvents==1&&
  !msiNativeForegroundTryReleasePowerHold();
 prepareHold();g_exitRequested=true;const bool exitHeld=held();
 prepareHold();g_inputReady=false;const bool inputHeld=held();
 g_inputReady=true;const bool afterInput=releasedOnce();
 std::cout<<"SHARED_POWER_HOLD_MAINTENANCE "<<json({{"closedGate",closedGate},{"afterGate",afterGate},
  {"pendingGate",pendingGate},{"afterDrain",afterDrain},{"unchangedCommitHeld",unchangedCommitHeld},
  {"cancelledByProjection",cancelledByProjection},{"exitHeld",exitHeld},{"inputHeld",inputHeld},
  {"afterInput",afterInput},{"realHardwareOperations",0}}).dump()<<"\n";
 assert(closedGate&&afterGate&&pendingGate&&afterDrain&&unchangedCommitHeld&&cancelledByProjection&&exitHeld&&inputHeld&&afterInput);
 for(const bool maintenanceFirst:{false,true}){
  prepareHold();
  const auto oldReady=isolationReadyFixtureObservation();
  ++isolationReadyFixtureSuspendSerial;
  const auto currentReady=isolationReadyFixtureObservation();
  if(maintenanceFirst){
   assert(releasedOnce());
   msiNativeForegroundSharedPowerReady(oldReady);
   assert(sharedReadyUiEvents==1);
   msiNativeForegroundSharedPowerReady(currentReady);
   assert(sharedReadyUiEvents==1&&"late current Ready requeued an already consumed isolation period");
  }else{
   msiNativeForegroundSharedPowerReady(currentReady);
   assert(sharedReadyUiEvents==1&&!msiNativeForegroundTryReleasePowerHold());
  }
  msiNativeForegroundSharedPowerReady(currentReady);
  assert(sharedReadyUiEvents==1&&g_msiNativeForegroundLastReadySuspendSerial==currentReady.suspendSerial);
 }
 std::cout<<"ISOLATION_READY_ORDER {\"maintenanceFirst\":true,\"publicationFirst\":true,\"uiEventsPerPeriod\":1}\n";
 prepare();
 const auto readyBeforeReader=isolationReadyFixtureObservation();
 msiNativeForegroundSharedPowerReady(readyBeforeReader);
 assert(!g_msiNativeForegroundPowerHold&&sharedReadyUiEvents==1);
 isolationReadyFixtureDeferred=true;
 msiNativeForegroundSharedPowerReady(readyBeforeReader);
 assert(isolationReadyFixtureDeferred&&sharedReadyUiEvents==1&&"duplicate Ready spent deferred isolation return before input was ready");
 g_inputReady=true;
 msiNativeForegroundSharedPowerReady(readyBeforeReader);
 msiNativeForegroundSharedPowerReady(readyBeforeReader);
 assert(!isolationReadyFixtureDeferred&&sharedReadyUiEvents==2);
 std::cout<<"ISOLATION_DEFERRED_RETURN {\"beforeReaderPreserved\":true,\"afterReaderQueues\":1}\n";
 drain();reset();g_inputReady=true;g_hardwareWriteGate=true;g_aiFanMockSession.isolated=false;
 g_msiNativeForegroundPowerHold=false;g_msiNativeForegroundResumePending=false;g_msiNativeForegroundPowerTransitionObserved=false;
}

void verifySuspendAdmission(){
 const auto prepare=[](){
  reset();g_lastSuspendNotifyTick=g_lastSuspendReceiptTick=0;g_sgSleepQueryGateClosed=false;
  g_sgSleepQueryOwnsGeneration=false;g_sgSleepQueryGeneration=0;
  g_resumeReadyGeneration=0;g_inputReady=true;g_sgPowerButtonSleepConfigured=false;
  isolationSuspendCalls=suspendLeafCalls=suspendQueued=0;
  isolationHold=false;isolationResumePending=false;suspendConfiguredMode=SgSleepMode::S3;
 };
 unsigned scenarios=0;
 // Both keep-input branches execute the actual first accepted suspend body.
 for(const bool keep:{false,true}){
  prepare();keepInput=keep;const auto beforeGeneration=generation;
  handleSuspendAdmission();
  assert(isolationSuspendCalls==1&&isolationHold&&!isolationResumePending);
  assert(generation==beforeGeneration+1&&g_powerLifecycle==PowerLifecycle::Suspending&&suspendQueued==1);
  assert(g_lastSuspendNotifyTick==10000&&g_inputReady.load()==keep);++scenarios;
  // Model a received private resume receipt as an input, not its implementation.
  g_powerLifecycle=PowerLifecycle::Resuming;isolationResumePending=true;isolationHold=false;
  const auto beforeDuplicate=isolationSuspendCalls;
  const auto beforeLeaves=suspendLeafCalls,beforeQueue=suspendQueued;
  const auto resumedGeneration=generation;at(10050);handleSuspendAdmission();
  assert(isolationSuspendCalls == beforeDuplicate);
  assert(!isolationHold&&isolationResumePending&&g_powerLifecycle==PowerLifecycle::Resuming);
  assert(generation==resumedGeneration&&g_lastSuspendNotifyTick==10000&&suspendQueued==beforeQueue&&suspendLeafCalls==beforeLeaves);
  assert(!facts.empty()&&facts.back().first=="power-suspend-ignored"&&facts.back().second["reason"]=="shared-suspend-duplicate");++scenarios;
  // A genuinely later notification must still close isolation and advance once.
  at(11001);handleSuspendAdmission();
  assert(isolationSuspendCalls==beforeDuplicate+1&&isolationHold&&!isolationResumePending);
  assert(generation==resumedGeneration+1&&g_powerLifecycle==PowerLifecycle::Suspending&&suspendQueued==beforeQueue+1);++scenarios;
 }
 for(const auto phase:{PowerLifecycle::Resuming,PowerLifecycle::Suspending}){
  prepare();g_sgSleepQueryOwnsGeneration=true;g_sgSleepQueryGateClosed=true;
  g_sgSleepQueryGeneration=generation;g_powerLifecycle=phase;
  isolationResumePending=true;const auto queryGeneration=generation;handleSuspendAdmission();
  if(phase==PowerLifecycle::Resuming){
   assert(isolationSuspendCalls==0&&!isolationHold&&isolationResumePending&&g_powerLifecycle==phase);
   assert(facts.back().first=="power-suspend-ignored"&&facts.back().second["reason"]=="query-already-resuming");
  }else assert(isolationSuspendCalls==1&&isolationHold&&!isolationResumePending&&g_powerLifecycle==PowerLifecycle::Suspended);
  assert(generation==queryGeneration&&!g_sgSleepQueryOwnsGeneration&&!g_sgSleepQueryGateClosed&&g_sgSleepQueryGeneration==0&&suspendQueued==0);++scenarios;
 }
 for(const auto phase:{PowerLifecycle::Suspending,PowerLifecycle::Suspended}){
  prepare();g_sgModernStandbyActive=true;g_powerLifecycle=phase;g_sgSleepQueryGateClosed=true;
  const auto s0Generation=generation;handleSuspendAdmission();
  assert(isolationSuspendCalls==1&&isolationHold&&!isolationResumePending);
  assert(generation==s0Generation&&g_powerLifecycle==phase&&!g_sgSleepQueryGateClosed&&suspendQueued==0);++scenarios;
 }
 prepare();g_sgRetryInProgress=true;g_sgTask.retryKind=SgRetryKind::EntryFailure;g_sgTask.retryAttempt=1;
 const auto retryGeneration=generation;handleSuspendAdmission();
 assert(isolationSuspendCalls==1&&isolationHold&&!isolationResumePending);
 assert(!sgSleepRetryActive()&&g_sgTask.retryAttempt==0&&g_powerLifecycle==PowerLifecycle::Suspended&&generation==retryGeneration&&suspendQueued==0);++scenarios;
 using Receipt=ymcc::sleeppower::Dispatch::Receipt;
 const auto enqueue=[](ULONG type){
  Receipt receipt;
  assert(g_sleepPowerDispatch.callback(type,nowTick,[&](const Receipt& queued){receipt=queued;return true;}));
  return receipt;
 };
 const auto claim=[](const Receipt& queued){return g_sleepPowerDispatch.forwarded(queued.sequence,queued.type,nowTick);};
 const auto resumeBoundary=[&](){
  const auto receipt=claim(enqueue(PBT_APMRESUMEAUTOMATIC));
  assert(receipt);g_sleepPowerDispatch.complete(receipt,nowTick);
  g_powerLifecycle=PowerLifecycle::Resuming;isolationHold=false;isolationResumePending=true;
 };
 const auto noNewSuspend=[](ULONGLONG beforeGeneration,unsigned beforeLeaves){
  assert(isolationSuspendCalls==1&&!isolationHold&&isolationResumePending&&"late duplicate ran private isolation suspend");
  assert(generation==beforeGeneration&&suspendQueued==1&&suspendLeafCalls==beforeLeaves);
  assert(g_powerLifecycle==PowerLifecycle::Resuming);
 };
 prepare();handleSuspendAdmission();at(10020);resumeBoundary();
 at(10050);const auto duplicate=enqueue(PBT_APMSUSPEND);
 assert(g_sleepPowerDispatch.pendingSuspend());
 at(11200);const auto delayed=claim(duplicate);
 assert(delayed&&delayed.tick==10050&&delayed.sequence==duplicate.sequence);
 const auto duplicateGeneration=generation;const auto duplicateLeaves=suspendLeafCalls;
 handleSuspendAdmission(delayed.type,&delayed);
 noNewSuspend(duplicateGeneration,duplicateLeaves);
 assert(!g_sleepPowerDispatch.pendingSuspend());
 assert(facts.back().first=="power-suspend-ignored"&&facts.back().second["reason"]=="shared-suspend-duplicate");++scenarios;
 assert(facts.back().second["ageClock"]=="process-receipt"&&facts.back().second["ageMs"]==50);

 prepare();const auto first=enqueue(PBT_APMSUSPEND);
 at(11050);const auto lateFirst=claim(first);
 assert(lateFirst&&lateFirst.tick==10000);handleSuspendAdmission(lateFirst.type,&lateFirst);
 at(11060);resumeBoundary();at(11100);const auto next=enqueue(PBT_APMSUSPEND);
 at(11150);const auto lateNext=claim(next);
 assert(lateNext&&lateNext.tick==11100);
 const auto beforeNew=generation;handleSuspendAdmission(lateNext.type,&lateNext);
 assert(isolationSuspendCalls==2&&isolationHold&&!isolationResumePending&&"genuinely later receipt was lost to clustered UI dispatch");
 assert(generation==beforeNew+1&&suspendQueued==2&&!g_sleepPowerDispatch.pendingSuspend());++scenarios;

 prepare();handleSuspendAdmission();at(10025);resumeBoundary();
 at(10050);const auto agedOut=enqueue(PBT_APMSUSPEND);
 for(unsigned index=0;index<8;++index){at(10051+index);(void)enqueue(PBT_APMRESUMEAUTOMATIC);}
 at(10090);const auto precise=claim(agedOut);
 assert(precise&&precise.tick==10050&&precise.sequence==agedOut.sequence);
 assert(g_sleepPowerDispatch.snapshot()["diagnosticHistoryEvictions"].get<unsigned>()>0);
 const auto unchangedGeneration=generation;const auto unchangedLeaves=suspendLeafCalls;
 handleSuspendAdmission(precise.type,&precise);
 noNewSuspend(unchangedGeneration,unchangedLeaves);
 assert(facts.back().second["ageClock"]=="process-receipt"&&facts.back().second["ageMs"]==50);
 assert(g_sleepPowerDispatch.pendingSuspend());
 const auto pending=g_sleepPowerDispatch.pendingReplay();assert(pending);
 const auto retained=claim(pending);assert(retained);g_sleepPowerDispatch.complete(retained,nowTick);
 assert(!g_sleepPowerDispatch.hasPending()&&!g_sleepPowerDispatch.pendingSuspend());++scenarios;

 prepare();handleSuspendAdmission();at(10020);resumeBoundary();
 at(10050);const auto older=enqueue(PBT_APMSUSPEND);
 at(10060);const auto successor=enqueue(PBT_APMRESUMESUSPEND);
 assert(!claim(successor)&&"a successor wake cannot bypass a pending suspend decision");
 at(11200);const auto ordered=claim(older);assert(ordered);
 const auto sameGeneration=generation;const auto sameLeaves=suspendLeafCalls;
 handleSuspendAdmission(ordered.type,&ordered);noNewSuspend(sameGeneration,sameLeaves);
 const auto wake=claim(successor);assert(wake&&wake.tick==10060);
 g_sleepPowerDispatch.complete(wake,nowTick);
 assert(!claim(older)&&"replaying the retired suspend cannot reopen admission");
 assert(isolationSuspendCalls==1&&isolationResumePending&&suspendQueued==1&&!g_sleepPowerDispatch.hasPending());++scenarios;
 assert(scenarios==15);
 std::cout<<"SUSPEND_ADMISSION "<<json({{"scenarios",scenarios},{"actualCompleteSuspendBody",true},{"privateHook","counter-memory-port"},{"realOsSleep",false},{"hardwarePassed",false}}).dump()<<"\n";
 // Do not leave the extra directed scenario's state in the old default replay.
 reset();g_lastSuspendNotifyTick=g_lastSuspendReceiptTick=0;g_sgSleepQueryGateClosed=false;g_sgSleepQueryOwnsGeneration=false;g_sgSleepQueryGeneration=0;g_inputReady=true;
}


void genuineButtonIntent(){
 g_sgLastKernel506Reason=1;g_sgLastKernel506EventFileTime=nowFile;
 sgRememberKernelSleepEntry(1,nowFile);handle506(1,nowFile);
}
void verifyInterruptedEntry(){
 for(int reason:{0,5,7,8,28,32,55}){
  reset();g_sgResleepEnabled=false;genuineButtonIntent();at(22000);
  handle507Native(reason,nowFile);assert(sgSleepRetryActive());dispatch();assert(requestCount==1);
 }
 pass("short exits share entry retry without a reason whitelist or the non-user option");
 for(int reason:{1,20,-1}){
  reset();g_sgResleepEnabled=false;genuineButtonIntent();at(11000);
  if(reason==1)g_sgLastPowerButtonWakeEventFileTime=nowFile;
  handle507Native(static_cast<WPARAM>(reason),nowFile);assert(!timerDue&&!requestCount);
 }
 pass("human wake hibernate handoff and missing reason never grant quick retry");
 reset();g_sgResleepEnabled=false;genuineButtonIntent();at(40001);handle507Native(32,nowFile);
 assert(!timerDue&&!requestCount);
 pass("entry window ends at thirty seconds without treating duration or zero DRIPS as failure");
 reset();genuineButtonIntent();const auto original=nowFile;at(11000);
 handle507Native(32,original-1);assert(!timerDue);handle507Native(32,0);assert(!timerDue);
 pass("each exit carries its own original time and missing or older events cannot borrow a new callback");
 reset();g_sgResleepEnabled=false;genuineButtonIntent();at(11000);handle507Native(32,nowFile);dispatch();
 const auto request=g_sgInternalSleepRequest.dispatchFileTime;
 handle507Native(32,request-1);assert(requestCount==1&&!timerDue);
 pass("an old exit cannot advance the already dispatched next attempt");
 reset();g_sgResleepEnabled=false;genuineButtonIntent();at(11000);
 g_sgLastPowerButtonWakeEventFileTime=nowFile;handle507Native(32,nowFile);
 assert(!timerDue&&!requestCount);
 pass("callback-side human veto wins before its UI message even against a later mouse exit");
 for(const auto phase:{PowerLifecycle::Suspending,PowerLifecycle::Suspended}){
  reset();genuineButtonIntent();g_powerLifecycle=phase;at(11000);sgObserveEntryFailureDisplay(0,"session");
  at(12000);sgObserveEntryFailureDisplay(1,"session");assert(monitorTimerDue==17000);
  at(16000);sgObserveEntryFailureDisplay(1,"duplicate");assert(monitorTimerDue==17000);
  sgMonitorEntryFailureTick();assert(!timerDue&&monitorTimerDue==17000);
  at(17000);sgMonitorEntryFailureTick();assert(timerDue);dispatch();assert(requestCount==1);
 }
 pass("relight in suspending and suspended uses one five-second wall deadline without duplicate extension");
 reset();genuineButtonIntent();at(11000);sgObserveEntryFailureDisplay(1,"initial-on");assert(!monitorTimerDue);
 sgObserveEntryFailureDisplay(2,"dim");sgObserveEntryFailureDisplay(1,"dim-to-on");assert(!monitorTimerDue);
 pass("initial ON and DIM to ON without this intent's OFF do not manufacture a failed entry");
 reset();genuineButtonIntent();at(11000);sgObserveEntryFailureDisplay(0,"off");
 at(11100);sgObserveEntryFailureDisplay(2,"dim");
 at(11200);sgObserveEntryFailureDisplay(1,"dim-to-on");assert(!monitorTimerDue&&!requestCount);
 pass("dim breaks the original OFF to ON entry-failure evidence");
 for(int cancel:{0,1,2,3,4,5}){
  reset();genuineButtonIntent();at(11000);sgObserveEntryFailureDisplay(0,"off");
  at(12000);sgObserveEntryFailureDisplay(1,"on");assert(monitorTimerDue);
  if(cancel==0)sgObserveEntryFailureDisplay(0,"off-again");
  if(cancel==1)g_sgLastPowerButtonWakeEventFileTime=nowFile;
  if(cancel==2)successfulExit=nowFile;
  if(cancel==3)++generation;
  if(cancel==4)g_sgRetryEntryFailure=false;
  if(cancel==5)g_exitRequested=true;
  at(17000);sgMonitorEntryFailureTick();assert(!timerDue&&!requestCount);
 }
 pass("OFF human wake Windows success new generation disabled and exit cancel the display decision");
 reset();genuineButtonIntent();at(11000);sgObserveEntryFailureDisplay(0,"off");
 at(12000);sgObserveEntryFailureDisplay(1,"on");at(999000);sgMonitorEntryFailureTick();
 assert(!timerDue&&!requestCount);
 pass("a delayed timer after long sleep cannot reinterpret elapsed wall time as a fresh entry failure");
 reset();genuineButtonIntent();at(11000);sgObserveEntryFailureDisplay(0,"off");
 at(12000);sgObserveEntryFailureDisplay(1,"on");g_sgLastPowerButtonWakeEventFileTime=nowFile;
 at(13000);g_sgLastPowerButtonSleepIntentFileTime=nowFile;g_sgLatestUserSleepEntryFileTime=nowFile;
 at(17000);sgMonitorEntryFailureTick();assert(!timerDue);
 sgArmMonitorEntryFailureTimer("late-old-on");assert(!monitorTimerDue);
 pass("old display evidence and its timer cannot enter a newer human sleep intent");
 for(const auto mode:{SgSleepMode::Unknown,SgSleepMode::S4}){
  reset();genuineButtonIntent();at(11000);handle507Native(32,nowFile);assert(timerDue);
  suspendConfiguredMode=mode;dispatch();assert(!requestCount);
 }
 reset();genuineButtonIntent();at(11000);handle507Native(32,nowFile);successfulExit=nowFile;
 dispatch();assert(!requestCount);
 pass("the actual dispatch rereads sleep action and Windows success before charging an API request");
 reset();g_sgResleepEnabled=false;genuineButtonIntent();requestAccepted=false;
 at(11000);handle507Native(32,nowFile);dispatch();dispatch();dispatch();
 assert(requestCount==3&&!timerDue&&!sgSleepRetryActive());
 pass("broadened interruption repair still stops at the original cumulative three requests");
 reset();genuineButtonIntent();at(11000);handle507Native(32,nowFile);assert(timerDue);
 at(11100);g_sgLastKernel506EventFileTime=nowFile;dispatch();assert(!requestCount&&!timerDue);
 pass("a fresh Windows entry during the retry grace cancels the pending request");
 reset();genuineButtonIntent();at(11000);handle507Native(32,nowFile);assert(timerDue==13000);
 at(12500);g_sgLastPowerButtonWakeEventFileTime=nowFile;dispatch();assert(!requestCount&&!timerDue);
 pass("expanded exits reserve the existing two-second grace for a delayed human callback");
 reset();genuineButtonIntent();sgObserveEntryFailureDisplay(1,"known-on");at(11000);handle507Native(32,nowFile);at(11100);
 sgObserveEntryFailureDisplay(0,"Windows-screen-off-again");dispatch();assert(!requestCount&&!timerDue);
 pass("screen relight that returns OFF before dispatch yields to Windows without another sleep request");
 reset();genuineButtonIntent();sgObserveEntryFailureDisplay(1,"known-on");at(11000);handle507Native(32,nowFile);const auto due=timerDue;
 at(11500);handle507Native(7,nowFile);assert(timerDue==due&&g_entryFailureExpanded);
 at(11600);sgObserveEntryFailureDisplay(0,"off");dispatch();assert(!requestCount&&!timerDue);
 pass("duplicate exit cannot change the latched failure source or extend its deadline");
 reset();genuineButtonIntent();at(11000);handle507Native(32,nowFile);dispatch();assert(requestCount==1&&!timerDue);
 sgDispatchSameModeRetry();sgDispatchSameModeRetry();assert(requestCount==1);
 pass("accepted request ignores duplicate timer delivery while waiting for Windows");
 reset();genuineButtonIntent();at(39000);handle507Native(32,nowFile);assert(timerDue==41000);
 dispatch();assert(!requestCount&&!timerDue);
 pass("expanded quick failure cannot dispatch after the thirty-second window expires");
 reset();genuineButtonIntent();at(10500);sgObserveEntryFailureDisplay(0,"off");
 at(11000);handle507Native(32,nowFile);at(11100);sgObserveEntryFailureDisplay(0,"duplicate-off");
 assert(g_entryFailureDisplayOffFileTime==epoch+10500*10000ULL);dispatch();assert(requestCount==1);
 pass("duplicate OFF notifications cannot become a new normal-sleep cancellation edge");
 reset();genuineButtonIntent();at(11000);handle507Native(32,nowFile);at(11100);sgObserveEntryFailureDisplay(0,"unknown-first-off");
 assert(!g_entryFailureNewOffFileTime);dispatch();assert(requestCount==1);
 for(int legacyReason:{7,8}){
  reset();genuineButtonIntent();sgObserveEntryFailureDisplay(1,"on");at(11000);handle507Native(legacyReason,nowFile);
  at(11100);sgObserveEntryFailureDisplay(0,"off");dispatch();assert(requestCount==1);
 }
 pass("unknown initial OFF is observation only and legacy explicit failures retain their original retry policy");




}

int main(int argc,char** argv){
 std::cout<<std::unitbuf;
 if(argc==2&&strcmp(argv[1],"--interrupted-entry-only")==0){verifyInterruptedEntry();std::cout<<"INTERRUPTED_ENTRY_COMPLETE "<<passed<<"\n";return 0;}
 if(argc==2&&strcmp(argv[1],"--suspend-admission-only")==0){verifySuspendAdmission();verifySharedPowerDispatch();verifySharedPowerReady();return 0;}
 verifyPowerNotificationEvidence();
 if(argc>1&&(strcmp(argv[1],"--s3")==0||strcmp(argv[1],"--caps-unknown")==0)){
  platformAoAc=false;platformCapsKnown=strcmp(argv[1],"--caps-unknown")!=0;
  for(bool pause:{false,true})for(bool keep:{false,true})for(ULONGLONG age:{0ULL,119999ULL,120000ULL,600000ULL}){
   reset(pause);keepInput=keep;sleepIntent();at(10000+age);
   resumeAutomatic();assert(!requestCount&&!timerDue);resumeInteractive();
   assert(!requestCount&&!timerDue&&!sgSleepRetryActive()&&!frozen);
   assert(resumeCount==(pause?1:0));
   for(int i=0;i<100;++i){sgNoteExternalDeviceNodeChange();sgNoteExternalDeviceAcDcChange();
    handle506(1,nowFile);handle507(2,7);resumeAutomatic();resumeInteractive();}
   assert(!requestCount&&!timerDue&&!sgSleepRetryActive()&&!g_sgModernStandbyActive);
  }
  pass("S3 user wake wins before S0 classification, every age/game/input combination and event storm");
  reset(true);sleepIntent();g_sgSleepTriggerTick=nowTick;sgStartSleepRetry(SgRetryKind::EntryFailure,"s3-entry-failure");
  assert(timerDue);resumeInteractive();assert(!timerDue&&!sgSleepRetryActive()&&!requestCount&&!frozen);
  pass("S3 interactive wake cancels pending failure retry and recovers the owned game");
  reset(true);sleepIntent();requestAccepted=false;sgStartSleepRetry(SgRetryKind::EntryFailure,"s3-entry-failure");
  dispatch();dispatch();dispatch();assert(requestCount==3&&!timerDue&&!frozen);
  for(int i=0;i<100;++i){++generation;sleepIntent();sgStartSleepRetry(SgRetryKind::EntryFailure,"repeat-failure");
   if(timerDue)dispatch();}
  assert(requestCount==3&&!timerDue);
  pass("S3 request rejection storm is bounded across generation and task resets");
  resumeInteractive();sleepIntent();requestAccepted=true;sgStartSleepRetry(SgRetryKind::EntryFailure,"new-user-sleep");
  dispatch();assert(requestCount==4);assert(platformQueries==1);
  pass("confirmed S3 user wake allows a fresh bounded repair; capabilities queried only once");
  std::cout<<"S3/capability fail-closed native selftest: "<<passed<<" groups PASS (actual production handlers; OS/PID boundaries mocked)\n";
  verifyActualFanRetryBoundary();
  return 0;
 }
 verifySuspendAdmission();
 verifySharedPowerDispatch();
 verifySharedPowerReady();
 // Accepted requests used to reset the counter at each confirmed suspend.
 reset(true);sleepIntent();at(140000);
 for(int i=0;i<200;++i){resumeAutomatic();resumeInteractive();handle507(2,5);
  if(timerDue)dispatch();if(sgSleepRetryActive())confirmRetrySuspend();
  assert(requestCount<=3);at(nowTick+130000);}
 assert(requestCount==3&&!timerDue&&!frozen&&resumeCount==1);
 pass("200 accepted sleep/wake cycles stop after 3 automatic requests and recover the game");
 for(int i=0;i<100;++i){++generation;sleepIntent();sgStartSleepRetry(SgRetryKind::EntryFailure,"late-failure");
  if(timerDue)dispatch();handle506(3,nowFile);handle507(2,7);sgObserveUnattributedSleepRetryFailure("duplicate-failure");
  assert(requestCount==3&&!timerDue);}
 pass("generation resets, late 506, entry failures and duplicate notifications cannot refill exhausted budget");
 powerButton();at(nowTick+10);sleepIntent();at(nowTick+130000);resumeAutomatic();dispatch();assert(requestCount==4);
 pass("confirmed S0 power-button wake permits a new bounded chain");
 reset();sleepIntent();
 for(int i=0;i<3;++i){sgStartSleepRetry(SgRetryKind::EntryFailure,"entry-failure-cycle");dispatch();assert(confirmRetrySuspend());}
 assert(g_sgTask.retryAttempt==0&&g_sgAutomaticSleepBudget.requests()==3);
 sgStartSleepRetry(SgRetryKind::EntryFailure,"fourth-entry-failure");assert(requestCount==3&&!timerDue);
 pass("successful EntryFailure sleeps do not reset the chain budget");
 reset();sleepIntent();requestAccepted=false;sgStartSleepRetry(SgRetryKind::EntryFailure,"mixed-entry");dispatch();
 requestAccepted=true;dispatch();assert(confirmRetrySuspend());at(140000);resumeAutomatic();dispatch();assert(confirmRetrySuspend());
 at(270000);resumeAutomatic();assert(requestCount==3&&!timerDue);
 pass("entry failure and unexpected wake share one cumulative request budget");
 for(ULONGLONG age:{0ULL,6000ULL,14000ULL,119999ULL}){
  reset();sleepIntent();at(10000+age);resumeAutomatic();resumeInteractive();handle507(2,5);
  sgNoteExternalDeviceNodeChange();assert(!timerDue&&!requestCount&&!sgSleepRetryActive());
 }
 pass("USB4 wakes before 120 seconds never submit or defer re-sleep");
 for(bool pause:{false,true}){
  reset(pause);sleepIntent();at(130000);resumeAutomatic();resumeInteractive();handle507(2,5);
  for(int i=0;i<100;++i)sgNoteExternalDeviceNodeChange();
  assert(sgSleepRetryActive()&&g_sgTask.retryKind==SgRetryKind::UnexpectedWake&&requestCount==0&&resumeCount==0);
  assert(timerDue==132000);dispatch();assert(requestCount==1&&resumeCount==0);
  pass(pause?"USB4 at 120 seconds repairs with game lease held":"USB4 at 120 seconds repairs without a game");
 }
 reset();sleepIntent();at(130001);resumeAutomatic();dispatch();assert(requestCount==1);
 pass("USB4 after 120 seconds repairs normally");
 reset();sleepIntent();at(16000);resumeAutomatic();int began=beginCount;
 handle506(1,epoch+9000*10000ULL);assert(beginCount==began&&!timerDue);
 at(150000);handle507(2,5);sgNoteExternalDeviceNodeChange();assert(!timerDue&&!requestCount);
 pass("early wake and its delayed 506 cannot trigger a late retry after 120 seconds");
 reset();for(int i=0;i<100;++i){sgNoteExternalDeviceNodeChange();sgNoteExternalDeviceAcDcChange();}resumeInteractive();
 assert(!timerDue&&!requestCount);pass("awake USB/charger insertion cannot trigger sleep");
 reset();sleepIntent();at(140000);sgNoteExternalDeviceAcDcChange();sgNoteExternalDeviceNodeChange();assert(!timerDue);
 pass("sleep-entry enumeration and charger changes alone are not a wake");
 for(int reason:{5,7,8,25,28,55,16777220,-1}){
  reset();sleepIntent();at(140000);handle507(2,reason);assert(sgSleepRetryActive());dispatch();assert(requestCount==1);
 }
 pass("all non-power-button reasons repair after 120 seconds, not an AMD-only whitelist");
 reset(true);sleepIntent();at(140000);resumeAutomatic();resumeInteractive();at(141000);powerButton();advance(150000);
 assert(!requestCount&&resumeCount==1&&!sgSleepRetryActive());sgNoteExternalDeviceNodeChange();assert(!timerDue);
 pass("physical button cancels pending USB4 retry and recovers exact lease");
 reset(true);sleepIntent();at(140000);resumeAutomatic();g_sgLastPowerButtonWakeEventFileTime=nowFile+10000;dispatch();
 assert(!requestCount&&!sgSleepRetryActive()&&resumeCount==1);
 pass("callback-side button evidence wins even before UI message dispatch");
 reset();sleepIntent();at(140000);resumeAutomatic();int beganLong=beginCount;
 handle506(1,epoch+9000*10000ULL);assert(beginCount==beganLong&&sgSleepRetryActive());dispatch();assert(requestCount==1);
 pass("delayed old 506 cannot rebuild generation or erase eligible scheduled retry");
 reset();sleepIntent();at(140000);powerButton();int b=beginCount;handle506(1,epoch+10000*10000ULL);
 assert(!g_sgLastPowerButtonSleepIntentFileTime.load()&&beginCount==b);sgNoteExternalDeviceNodeChange();assert(!timerDue);
 pass("stale 506 after button wake cannot re-arm sleep guard");
 reset();sleepIntent();at(140000);resumeAutomatic();handle507(2,5);dispatch();assert(requestCount==1);assert(confirmRetrySuspend());
 at(143000);handle506(3,g_sgInternalSleepRequest.dispatchFileTime);assert(beginCount==0);
 at(170000);resumeAutomatic();dispatch();assert(requestCount==2);
 pass("successful re-sleep retains original eligible intent; internal 506 stays internal");
 reset();sleepIntent();g_sgTask={};g_sgRepairEligible=false;g_powerLifecycle=PowerLifecycle::Ready;
 at(140000);resumeAutomatic();dispatch();assert(requestCount==1&&g_sgTask.generation==generation);
 pass("UI/PID lifecycle already Ready does not lose eligible protected sleep intent");
 reset(true);sleepIntent();g_sgResleepEnabled=false;at(140000);resumeInteractive();assert(!timerDue&&resumeCount==1);
 pass("disabled button leaves normal interactive wake unchanged");
 reset(true);sleepIntent();at(140000);resumeAutomatic();g_sgResleepEnabled=false;dispatch();assert(!requestCount&&resumeCount==1);
 pass("disabling during classification cancels without sleeping");
 reset();g_sgTask.mode=SgSleepMode::S4;sgArmNonUserWakeGuard("s4");resumeAutomatic();assert(!timerDue&&!requestCount);
 pass("hibernate is excluded");
 reset(true);sleepIntent();at(140000);resumeAutomatic();handle507(2,5);requestAccepted=false;dispatch();dispatch();dispatch();
 assert(requestCount==3&&!timerDue&&!sgSleepRetryActive()&&resumeCount==1&&!g_sgLastPowerButtonSleepIntentFileTime.load());
 sgNoteExternalDeviceNodeChange();assert(!timerDue);pass("three rejected API requests stop and recover, no endless retry");
 reset();sleepIntent();nowFile=epoch;ULONGLONG age=1;assert(!sgExternalDeviceWakeIntentAge(age)&&age==0);
 pass("wall-clock rollback cannot bypass the 120-second gate");
 reset();g_guardEnabled=false;resumeAutomatic();handle507(2,5);sgNoteExternalDeviceNodeChange();assert(!timerDue&&!requestCount);
 pass("master sleep optimization disabled has no non-user retry");
 reset();handle506(1,epoch+10000*10000ULL);g_sgLastKernel506Reason=1;g_sgRetryEntryFailure=false;at(11000);handle507(2,7);
 assert(!timerDue&&!requestCount);
 pass("non-user toggle cannot bypass 120 seconds when entry-failure retry is disabled");
 reset();g_sgRetryEntryFailure=false;sleepIntent();at(140000);handle507(2,5);dispatch();assert(requestCount==1);
 pass("eligible non-user wake repair is independent of entry-failure toggle");
 reset();handle506(1,epoch+10000*10000ULL);g_sgLastKernel506Reason=1;at(11000);handle507(2,7);
 assert(g_sgTask.retryKind==SgRetryKind::EntryFailure);pass("separate rapid S0 entry-failure feature remains unchanged");
 for(auto target:{ymcc::sleeprequest::Mode::Sleep,ymcc::sleeprequest::Mode::Hibernate}){
  reset(true);sleepIntent();g_sgAutomaticSleepTarget=target;at(140000);resumeAutomatic();
  assert(!timerDue&&!requestCount&&resumeCount==0&&frozen);handle507(2,5);assert(!timerDue&&!requestCount);
  resumeInteractive();assert(resumeCount==1&&!frozen&&!sgSleepRetryActive()&&g_sgAutomaticSleepTarget==ymcc::sleeprequest::Mode::None);
 }
 pass("ordinary/hibernate fallback automatic wake stays held; final interactive wake cancels and recovers exact lease");
 reset(true);sleepIntent();g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::Hibernate;
 const auto s4Ingress=g_sleepPowerDispatch.window(PBT_APMSUSPEND,nowTick,nowFile);g_sleepPowerDispatch.complete(s4Ingress);handleS4();
 assert(resumeCount==1&&!frozen&&g_sgAutomaticSleepTarget==ymcc::sleeprequest::Mode::None&&!g_sgModernStandbyActive);
 pass("actual S4 kernel wake finalizes fallback and recovers exact original lease");
 reset(true);sleepIntent();g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::Hibernate;resumeCritical();
 assert(resumeCount==1&&!frozen&&g_sgAutomaticSleepTarget==ymcc::sleeprequest::Mode::None&&!g_sgModernStandbyActive);
 pass("critical-only S4 fallback resume cannot strand the lease");
 reset();handle506(4,nowFile);assert(!g_sgRepairEligible&&!g_sgModernStandbyActive&&!beginCount);
 pass("ordinary non-user506does not manufacture a new user sleep intent");
 reset();sleepIntent();g_sgTask.retryKind=SgRetryKind::EntryFailure;g_sgRetryInProgress=true;
 sgMarkInternalSleepRequest(SgRetryKind::EntryFailure);g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::ModernStandby;
 assert(sgInternalSleepRequestMatches506(4,nowFile));handle506(4,nowFile);assert(beginCount==0&&g_sgModernStandbyActive);
 g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::Sleep;assert(!sgInternalSleepRequestMatches506(4,nowFile));
 pass("monitor-off506can only join the timestamp-correlated original modern retry");
 ULONGLONG second=sgKernelPowerEventFileTime("<TimeCreated SystemTime='2026-09-30T15:14:50Z'/>");
 assert(sgKernelPowerEventFileTime("<TimeCreated SystemTime='2026-09-30T15:14:50.1234567Z'/>")-second==1234567);
 assert(sgKernelPowerEventFileTime("<TimeCreated SystemTime=\"2026-09-30T15:14:50.5Z\"/>")-second==5000000);
 pass("power event parser preserves sub-second button/sleep ordering");
 reset(true);sleepIntent();g_powerLifecycle=PowerLifecycle::Suspending;sgStartSleepRetry(SgRetryKind::EntryFailure,"off-pending");
 g_sgRetryEntryFailure=false;sgCancelEntryFailureRetry("off");g_sgRetryEntryFailure=true;at(nowTick+1000);sgDispatchSameModeRetry();
 assert(!requestCount&&!timerDue&&!sgSleepRetryActive()&&!frozen);
 pass("OFF retires old EntryFailure work; ON cannot revive it; failed owned game is recovered");
 reset(true);sleepIntent();sgStartSleepRetry(SgRetryKind::EntryFailure,"off-during-standby");g_sgRetryEntryFailure=false;
 sgCancelEntryFailureRetry("off");assert(!requestCount&&!timerDue&&!sgSleepRetryActive()&&frozen&&resumeCount==0);
 powerButton();assert(!frozen&&resumeCount==1);
 pass("OFF during Windows standby preserves lease until confirmed button wake");
 reset(true);sleepIntent();sgStartSleepRetry(SgRetryKind::EntryFailure,"wake-before-timer");at(timerDue-1);
 g_sgLastPowerButtonWakeEventFileTime=nowFile;dispatch();assert(!requestCount&&!sgSleepRetryActive()&&!frozen&&g_sgAutomaticSleepBudget.requests()==0);
 pass("callback-side current button wake before EntryFailure dispatch cancels without spending budget");
 reset();sleepIntent();g_sgLastPowerButtonWakeEventFileTime=g_sgLastPowerButtonSleepIntentFileTime.load()-1;
 sgStartSleepRetry(SgRetryKind::EntryFailure,"new-manual");dispatch();assert(requestCount==1);
 pass("previous-cycle button wake never blocks a new eligible manual sleep");
 reset(true);sleepIntent();g_powerLifecycle=PowerLifecycle::Suspending;sgStartSleepRetry(SgRetryKind::EntryFailure,"accepted");
 dispatch();g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::ModernStandby;g_sgRetryEntryFailure=false;sgCancelEntryFailureRetry("off");
 assert(requestCount==1&&!sgSleepRetryActive()&&frozen&&g_sgInternalSleepRequest.kind==SgRetryKind::EntryFailure);
 pass("OFF cannot undo accepted OS call; late Windows correlation remains owned");

 reset(true);g_sgResleepEnabled=false;sleepIntent();const auto separateIntent=g_sgLastPowerButtonSleepIntentFileTime.load();
 assert(separateIntent==nowFile&&g_sgRepairEligible);
 sgStartSleepRetry(SgRetryKind::EntryFailure,"entry-only-button");at(timerDue-1);
 g_sgLastPowerButtonWakeEventFileTime=nowFile;dispatch();
 assert(!requestCount&&!sgSleepRetryActive()&&!frozen&&resumeCount==1&&g_sgAutomaticSleepBudget.requests()==0);
 pass("EntryFailure records intent with non-user option OFF and no506; current callback wake vetoes before budget");
 reset();g_sgResleepEnabled=false;sleepIntent();sgStartSleepRetry(SgRetryKind::EntryFailure,"entry-only-valid");dispatch();
 assert(requestCount==1&&sgSleepRetryActive());
 pass("non-user option OFF still permits an eligible EntryFailure without a user wake");
 reset();g_sgResleepEnabled=false;sleepIntent();g_sgLastPowerButtonWakeEventFileTime=g_sgLastPowerButtonSleepIntentFileTime.load()-1;
 sgStartSleepRetry(SgRetryKind::EntryFailure,"entry-only-old-wake");dispatch();assert(requestCount==1);
 pass("non-user option OFF never lets an old wake veto fresh manual sleep");
 reset();g_sgResleepEnabled=false;sleepIntent();g_sgLastPowerButtonWakeEventFileTime=g_sgLastPowerButtonSleepIntentFileTime.load();
 sgStartSleepRetry(SgRetryKind::EntryFailure,"entry-only-equal-wake");if(timerDue)dispatch();
 assert(!timerDue);
 assert(!requestCount&&!sgSleepRetryActive()&&g_sgAutomaticSleepBudget.requests()==0);
 pass("same-timestamp current wake veto remains effective with non-user option OFF");
 reset(true);g_sgResleepEnabled=false;sleepIntent();sgStartSleepRetry(SgRetryKind::EntryFailure,"entry-only-processed");
 at(timerDue-1);powerButton();at(nowTick+10);sleepIntent();const auto freshIntent=g_sgLastPowerButtonSleepIntentFileTime.load();
 assert(freshIntent==nowFile&&freshIntent>separateIntent);sgStartSleepRetry(SgRetryKind::EntryFailure,"entry-only-fresh");dispatch();assert(requestCount==1);
 pass("processed manual wake then a fresh sleep obtains a new independent anchor");
 reset();g_sgResleepEnabled=false;g_sgTask.mode=SgSleepMode::S3;sgMarkSleepTrigger();
 assert(!g_sgRepairEligible&&!g_sgLastPowerButtonSleepIntentFileTime.load());
 reset();g_sgResleepEnabled=false;g_sgTask.mode=SgSleepMode::S4;g_sgSleepIntentArmed=true;sgMarkSleepTrigger();
 assert(!g_sgLastPowerButtonSleepIntentFileTime.load());
 pass("independent anchor never manufactures an unarmed or hibernate sleep intent");
 reset();g_sgResleepEnabled=false;const auto kernelIntent=nowFile-1000;g_sgLastPowerButtonSleepIntentFileTime=kernelIntent;sleepIntent();
 assert(g_sgLastPowerButtonSleepIntentFileTime.load()==kernelIntent);
 pass("existing kernel origin remains exact when the non-user option is OFF");
 reset();g_sgResleepEnabled=false;sleepIntent();const auto originalIntent=g_sgLastPowerButtonSleepIntentFileTime.load();at(nowTick+100);sleepIntent();
 assert(g_sgLastPowerButtonSleepIntentFileTime.load()==originalIntent);
 pass("duplicate sleep trigger cannot refresh the original intent age");
 reset();g_sgResleepEnabled=false;sleepIntent();at(140000);resumeAutomatic();sgNoteExternalDeviceNodeChange();sgNoteExternalDeviceAcDcChange();
 assert(!requestCount&&!timerDue&&!sgSleepRetryActive());
 pass("recording an independent anchor never enables the disabled power-change guard");


 reset();sgRememberKernelSleepEntry(28,nowFile);sgRememberKernelSleepEntry(1,0);sgRememberKernelSleepEntry(3,nowFile+1);
 assert(!g_sgLatestUserSleepEntryFileTime.load());
 pass("non-user reasons, missing original time and future events never create a callback origin");
 reset();const auto callbackFirst=nowFile;sgRememberKernelSleepEntry(1,callbackFirst);at(nowTick+1);const auto callbackSecond=nowFile;
 sgRememberKernelSleepEntry(3,callbackSecond);sgRememberKernelSleepEntry(1,callbackFirst);sgRememberKernelSleepEntry(28,nowFile);
 assert(g_sgLatestUserSleepEntryFileTime.load()==callbackSecond);
 sgNoteExternalDeviceNodeChange();sgNoteExternalDeviceAcDcChange();assert(!g_sgLastPowerButtonSleepIntentFileTime.load()&&!requestCount&&!timerDue);
 pass("callback watermark is monotonic event data only; awake USB activity cannot arm sleep");
 reset();const auto lateOriginal=nowFile;sgRememberKernelSleepEntry(1,lateOriginal);at(nowTick+658615);sleepIntent();
 assert(g_sgLastPowerButtonSleepIntentFileTime.load()==lateOriginal);
 at(nowTick+182);resumeAutomatic();dispatch();assert(requestCount==1);
 pass("delayed suspend GUI handling inherits original506 age instead of182ms");
 reset();const auto closedEntry=nowFile;sgRememberKernelSleepEntry(1,closedEntry);at(nowTick+1);sgClearUnexpectedWake("closed-cycle",true);
 at(nowTick+1000);sleepIntent();assert(g_sgLastPowerButtonSleepIntentFileTime.load()==nowFile&&g_sgLastPowerButtonSleepIntentFileTime.load()!=closedEntry);
 pass("closed cycle watermark prevents reusing an old pending506 for a new sleep");
 reset();const auto beforeButton=nowFile;sgRememberKernelSleepEntry(1,beforeButton);at(nowTick+1);g_sgLastPowerButtonWakeEventFileTime=nowFile;
 at(nowTick+1000);sleepIntent();assert(g_sgLastPowerButtonSleepIntentFileTime.load()==nowFile);
 sgStartSleepRetry(SgRetryKind::EntryFailure,"fresh-after-callback-button");dispatch();assert(requestCount==1);
 pass("old callback entry before confirmed button wake cannot replace a fresh manual origin");
 reset();sgRememberKernelSleepEntry(1,nowFile);g_sgIgnoredEarlyWakeFileTime=nowFile+1;at(nowTick+1000);sleepIntent();
 assert(g_sgLastPowerButtonSleepIntentFileTime.load()==nowFile);
 pass("an already ignored early-wake cycle cannot be inherited by the next trigger");
 reset();sgRememberKernelSleepEntry(1,nowFile);sgMarkInternalSleepRequest(SgRetryKind::EntryFailure);at(nowTick+1);sleepIntent();
 assert(g_sgLastPowerButtonSleepIntentFileTime.load()==nowFile);
 pass("timestamp-correlated internal506 is never inherited as a new user intent");
 reset();sgRememberKernelSleepEntry(1,nowFile);g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::ModernStandby;at(nowTick+1);sleepIntent();
 assert(g_sgLastPowerButtonSleepIntentFileTime.load()==nowFile);
 pass("accepted automatic sleep target cannot supply an inherited user origin");
 reset();const auto exactOrigin=nowFile-10000;g_sgLastPowerButtonSleepIntentFileTime=exactOrigin;sgRememberKernelSleepEntry(1,nowFile);
 at(nowTick+100);sleepIntent();assert(g_sgLastPowerButtonSleepIntentFileTime.load()==exactOrigin);
 pass("already admitted original event time is not replaced by a callback candidate");
 reset();sgRememberKernelSleepEntry(1,nowFile);g_sgAutomaticSleepBudget.consume();g_sgAutomaticSleepBudget.consume();
 sgClearUnexpectedWake("no-owned-origin",true);assert(g_sgClosedSleepIntentFileTime.load()==nowFile&&g_sgAutomaticSleepBudget.requests()==2);
 pass("retiring pending data without a current origin neither arms sleep nor refills budget");
 reset();sgClearUnexpectedWake("past-cycle",true);at(nowTick+10);const auto nextEntry=nowFile;sgRememberKernelSleepEntry(1,nextEntry);
 at(nowTick+10);g_sgLastKernel507EventFileTime=nowFile;at(nowTick+200000);sleepIntent();
 assert(g_sgLastPowerButtonSleepIntentFileTime.load()==nextEntry);
 pass("a new unretired entry remains usable when its507callback also precedes the GUI suspend");
 reset(true);sgRememberKernelSleepEntry(1,nowFile);at(nowTick+130000);sleepIntent();resumeAutomatic();
 g_sgLastPowerButtonWakeEventFileTime=nowFile;handle507(1,1);if(timerDue)dispatch();
 assert(!requestCount&&!sgSleepRetryActive()&&!frozen);
 pass("confirmed manual button wake still vetoes a retry armed from inherited event time");


 reset();const auto callbackAfterGui=nowFile;at(nowTick+658615);sleepIntent();
 assert(g_sgLastPowerButtonSleepIntentFileTime.load()==nowFile);sgRememberKernelSleepEntry(1,callbackAfterGui);
 at(nowTick+182);resumeAutomatic();dispatch();assert(requestCount==1);
 pass("a506callback after suspend GUI handling is reconciled before the120-second wake decision");

 reset();sleepIntent();const auto frozenTick=nowTick;const auto frozenOrigin=nowFile;
 nowFile+=658419ULL*10000;g_sgLastKernel507EventFileTime=nowFile;ULONGLONG absoluteAgeMs=0;
 assert(sgExternalDeviceWakeIntentAge(absoluteAgeMs)&&absoluteAgeMs==658419&&nowTick==frozenTick&&!timerDue&&!requestCount);
 assert(g_sgLastPowerButtonSleepIntentFileTime.load()==frozenOrigin);
 pass("sleep duration follows absolute event times without any timer callback during sleep");
 reset();sleepIntent();const auto shortOrigin=nowFile;at(nowTick+200000);
 g_sgLastKernel507EventFileTime=shortOrigin+99000ULL*10000;ULONGLONG shortAgeMs=0;
 assert(!sgExternalDeviceWakeIntentAge(shortAgeMs)&&shortAgeMs==99000);resumeAutomatic();
 assert(!requestCount&&!timerDue&&!g_sgLastPowerButtonSleepIntentFileTime.load());
 pass("a99-second sleep stays short when its wake callback arrives after200seconds");
 reset();sleepIntent();const auto lateWakeOrigin=nowFile;at(nowTick+140000);resumeAutomatic();assert(timerDue);
 g_sgLastKernel507EventFileTime=lateWakeOrigin+99000ULL*10000;dispatch();
 assert(!requestCount&&!sgSleepRetryActive()&&!g_sgAutomaticSleepBudget.requests());
 pass("late original507time cancels a tentative retry before any request or budget charge");
 reset();sleepIntent();g_sgLastKernel507EventFileTime=nowFile-1;at(nowTick+130000);ULONGLONG oldWakeAgeMs=0;
 assert(sgExternalDeviceWakeIntentAge(oldWakeAgeMs)&&oldWakeAgeMs==130000);
 pass("a507from before the current sleep intent cannot shorten the current standby age");
 reset();sleepIntent();at(nowTick+130000);g_sgLastKernel507EventFileTime=nowFile+1;ULONGLONG futureWakeAgeMs=0;
 assert(sgExternalDeviceWakeIntentAge(futureWakeAgeMs)&&futureWakeAgeMs==130000);
 pass("a future507timestamp is not accepted as this cycle's wake endpoint");
 for(const auto elapsed:{119999ULL,120000ULL}){
  reset();sleepIntent();const auto origin=nowFile;at(nowTick+300000);
  g_sgLastKernel507EventFileTime=origin+elapsed*10000;ULONGLONG boundaryAgeMs=0;
  assert(sgExternalDeviceWakeIntentAge(boundaryAgeMs)==(elapsed==120000)&&boundaryAgeMs==elapsed);
 }
 pass("the120-second policy boundary uses original wake time even with delayed collection");

 reset();sleepIntent();at(nowTick+130000);resumeAutomatic();assert(timerDue);
 at(timerDue+2000);const auto delayedDispatchTick=nowTick;const auto delayedDispatchFileTime=nowFile;dispatch();
 assert(requestCount==1&&nowTick==delayedDispatchTick&&nowFile==delayedDispatchFileTime);
 pass("a delayed timer message never rewinds the current time to its scheduled deadline");

 for(const bool nonUserEnabled:{false,true}){
  reset(true);g_sgResleepEnabled=nonUserEnabled;sleepIntent();
  at(nowTick+130000);sgStartSleepRetry(SgRetryKind::EntryFailure,"accepted-before-late-button-callback");dispatch();
  const auto acceptedEntry=nowFile+10000;const auto actualButton=nowFile-10000;
  at(nowTick+2);g_sgLastPowerButtonWakeEventFileTime=actualButton;handle507(1,1);
  const auto afterWakeGeneration=generation;
  handle506(3,acceptedEntry);
  assert(generation==afterWakeGeneration&&!g_sgLastPowerButtonSleepIntentFileTime.load()&&!sgSleepRetryActive()&&!frozen);
  assert(g_sgLastPowerButtonWakeEventFileTime.load()==actualButton);
  sgNoteExternalDeviceNodeChange();resumeAutomatic();if(timerDue)dispatch();
  assert(requestCount==1&&!sgSleepRetryActive());
 }
 pass("late internal506after a completed manual wake cannot resurrect either option's retired cycle");
 for(const bool nonUserEnabled:{false,true}){
  reset();g_sgResleepEnabled=nonUserEnabled;g_sgLastPowerButtonWakeEventFileTime=nowFile-10000;
  const auto humanWatermark=g_sgLastPowerButtonWakeEventFileTime.load();sleepIntent();
  assert(g_sgLastPowerButtonWakeEventFileTime.load()==humanWatermark);
  at(nowTick+130000);sgStartSleepRetry(SgRetryKind::EntryFailure,"fresh-sleep-after-old-wake");dispatch();assert(requestCount==1);
 }
 pass("fresh sleep preserves the human wake watermark and compares it to the new intent instead of erasing it");

 for(const bool nonUserEnabled:{false,true}){
  reset(true);g_sgResleepEnabled=nonUserEnabled;const auto oldWake=nowFile;
  g_sgLastPowerButtonWakeEventFileTime=oldWake;at(nowTick+10);sleepIntent();
  sgStartSleepRetry(SgRetryKind::EntryFailure,"new-sleep-before-old-wake-message");handle507(1,1);
  assert(sgSleepRetryActive()&&frozen&&g_sgLastPowerButtonSleepIntentFileTime.load()>oldWake);
  dispatch();assert(requestCount==1);at(nowTick+10);g_sgLastPowerButtonWakeEventFileTime=nowFile;handle507(1,1);
  assert(!sgSleepRetryActive()&&!frozen&&!g_sgLastPowerButtonSleepIntentFileTime.load());
 }
 pass("an older queued manual wake cannot cancel a newer manual sleep; its own current wake still cancels");

 for(const bool nonUserEnabled:{false,true}){
  reset(true);g_sgResleepEnabled=nonUserEnabled;const auto originalEntry=nowFile;
  sgRememberKernelSleepEntry(1,originalEntry);at(nowTick+130000);
  g_sgLastPowerButtonWakeEventFileTime=nowFile;g_sgLastKernel507EventFileTime=nowFile;
  sgPublishSleepEventTime(g_sgOriginalPowerButtonWakeFileTime,nowFile);
  // An older later-delivered non-user507 cannot erase the original button witness.
  g_sgLastKernel507EventFileTime=originalEntry+10000;
  at(nowTick+2000);sleepIntent();resumeAutomatic();handle507(1,1);
  assert(!sgSleepRetryActive()&&!frozen&&!g_sgLastPowerButtonSleepIntentFileTime.load());
  at(nowTick+130000);sgNoteExternalDeviceNodeChange();if(timerDue)dispatch();assert(!requestCount);
 }
 pass("a delayed suspend GUI after both entry and manual wake callbacks cannot recast the wake as older than the current intent");

 verifyInterruptedEntry();
 for(int i=1;i<argc;++i)replay(argv[i]);
 std::cout<<"non-user wake native selftest: "<<passed<<" groups PASS (120-second gate; actual production functions; OS/PID boundaries mocked)\n";
 verifyActualFanRetryBoundary();
}
'''

if 'static void sgMarkSleepTrigger(const ymcc::sleeppower::Dispatch::Receipt* receipt)' in source:
    preamble=preamble.replace('static void sgMarkSleepTrigger();','static void sgMarkSleepTrigger(const ymcc::sleeppower::Dispatch::Receipt* receipt=nullptr);')
resume_classification_binding = None
if args.resume_classification_public_facts_only or args.resume_classification_only or args.resume_classification_edge_only or args.resume_classification_lifetime_only or args.resume_classification_publication_only or args.resume_classification_sender_only:
    import runpy
    support = Path(__file__).parent / 'fixtures/sleep_non_user_wake/resume_classification.py'
    prepared = runpy.run_path(str(support))['prepare'](globals())
    if args.resume_classification_sender_only:
        joined = Path(__file__).parent / 'fixtures/sleep_non_user_wake/resume_sender_join.py'
        raise SystemExit(runpy.run_path(str(joined))['run'](globals(), prepared))
    preamble, functions, handlers, checks, resume_classification_binding = prepared
    with (out/'resume-classification-bindings.json').open('x',encoding='utf-8') as f:
        json.dump(resume_classification_binding,f,ensure_ascii=False,indent=2)
else:
    import runpy
    support = Path(__file__).parent / 'fixtures/sleep_non_user_wake/resume_legacy_binding.py'
    preamble, functions, handlers = runpy.run_path(str(support))['bind'](globals())

if args.broadcast_consumer_only:
    import runpy
    support = Path(__file__).parent / 'fixtures/sleep_non_user_wake/broadcast_consumers.py'
    preamble, functions, handlers, checks = runpy.run_path(str(support))['bind'](globals())

if 'powerConsumer(' in handlers and 'void appendNativeLifecycleLog(' not in preamble:
    preamble+='\nvoid appendNativeLifecycleLog(const char* event,const json& details){facts.emplace_back(event,details);}\n'

# Bind exact actual function/handler text before compiling; no neighboring fan case.
assert 'WM_FAN_KERNEL_POWER_FACT' not in handlers and 'fanHostQueueSuspend' not in handlers
(out/'actual-production-source-bindings.json').write_text(json.dumps({
 'schemaVersion':1,'source':str(args.source.resolve()),'sourceSha256':hashlib.sha256(args.source.read_bytes()).hexdigest(),
 'notificationCallbackSha256':hashlib.sha256(notification_callback.encode()).hexdigest(),'notificationCallback':notification_callback,
 'sharedPowerDispatchHeader':str(dispatch_header_path.resolve()),'sharedPowerDispatchHeaderSha256':hashlib.sha256(dispatch_header_bytes).hexdigest(),
 'sharedPowerDispatchConstants':dispatch_constants,'sharedPowerDispatchConstantsSha256':hashlib.sha256(dispatch_constants.encode()).hexdigest(),
 'sharedPowerDispatchScenarios':5,'sharedPowerDispatchUsesActualHeader':True,
 'screenpadsSuspendRoute':screenpads_suspend_route,
 'sharedPowerReadyHeader':str(isolation_header_path.resolve()),'sharedPowerReadyHeaderSha256':hashlib.sha256(isolation_header_bytes).hexdigest(),
 'sharedPowerReadyFunction':shared_power_ready,'sharedPowerReadyFunctionSha256':hashlib.sha256(shared_power_ready.encode()).hexdigest(),
 'sharedPowerHoldReleaseFunction':shared_power_hold_release,'sharedPowerHoldReleaseFunctionSha256':hashlib.sha256(shared_power_hold_release.encode()).hexdigest(),
 'isolationReadyWorkCurrentFunction':ready_work_current,'isolationReadyWorkCurrentFunctionSha256':hashlib.sha256(ready_work_current.encode()).hexdigest(),
 'hardwareWriteAllowedFunction':hardware_write_allowed,'hardwareWriteAllowedFunctionSha256':hashlib.sha256(hardware_write_allowed.encode()).hexdigest(),
 'sharedPowerReadyGroups':1,'sharedPowerReadyFunctionBodyRewritten':False,
 'suspendAdmissionBodySha256':hashlib.sha256(suspend.encode()).hexdigest(),'suspendAdmissionBody':suspend,
 'suspendAdmissionTypesSha256':hashlib.sha256(suspend_admission_types.encode()).hexdigest(),'suspendAdmissionTypes':suspend_admission_types,
 'suspendAdmissionHelperSha256':hashlib.sha256(suspend_admission_helper.encode()).hexdigest(),'suspendAdmissionHelper':suspend_admission_helper,
 'suspendIgnoreHelperSha256':hashlib.sha256(suspend_ignore_helper.encode()).hexdigest(),'suspendIgnoreHelper':suspend_ignore_helper,
 'suspendIgnoreBeforeIndependentSlowConsumers':True,
 'suspendAdmissionBeforeSlowPreparation':True,
 'suspendAdmissionOnly':args.suspend_admission_only,'suspendAdmissionScenarios':15,
 'functions':[{'name':name,'sha256':hashlib.sha256(function(name).encode()).hexdigest(),'text':function(name)} for name in names],
 'handlersSha256':hashlib.sha256(handlers.encode()).hexdigest(),'handlers':handlers,
 'functionBodiesUniqueAndComplete':True,'neighborHardwareCaseExcluded':True,
 'systemSleepFunctionIsSourceOnly':True,'actualSystemSleepTerminal':'original counted in-memory sgRequestSystemSleep',
 'pureFanBoundaryHeader':'native/fan_physical_wake.h','actual120SecondGateUnchanged':True,
 'originalReplayFixturesUnchanged':True,'realOsSleep':False
},indent=2)+'\n',encoding='utf-8')

cpp = out / 'sleep_non_user_wake_native_selftest.cpp'
cpp.write_text(preamble + '\n' + functions + '\n' + handlers + '\n' + checks, encoding='utf-8')
if args.prepare_only:
    print(json.dumps({'status':'PREPARED_NOT_RUN','publicFactsOnly':args.resume_classification_public_facts_only,'source':str(cpp)}))
    raise SystemExit(0)
repo = Path.cwd()
vs = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
assert vs.is_file(), 'MSVC environment not found'
exe = out / 'sleep_non_user_wake_native_selftest.exe'
fixtures = [repo / 'tools/fixtures/sleep_non_user_wake' / name for name in
            ['amd_usb4_failure.json', 'intel_usb4_failure.json', 'amd_power_button_wake.json', 'g3e_usb4_delayed_entry.json','win5_mouse_entry_failure.json']]
cmd = out / 'run-native-selftest.cmd'
toolchain_js='import {calibrationToolchainPreamble} from "./tools/fixtures/hc_reviewed_calibration/toolchain.mjs";process.stdout.write(calibrationToolchainPreamble(process.argv[1]));'
toolchain=subprocess.run(['node','--input-type=module','-e',toolchain_js,str(vs)],cwd=repo,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',check=True).stdout
cmd_text = (toolchain + f'cl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 /D_CRT_SECURE_NO_WARNINGS "{cpp}" /I"{repo / "deps/json"}" /I"{repo / "native"}" '
               f'/Fo"{out / "sleep_non_user_wake_native_selftest.obj"}" /Fe"{exe}" /sourceDependencies "{out / "dependencies.json"}" /link /VERBOSE:LIB\r\n'
               f'if not "%errorlevel%"=="0" exit /b %errorlevel%\r\n"{exe}" --s3\r\nif not "%errorlevel%"=="0" exit /b %errorlevel%\r\n'
               f'"{exe}" --caps-unknown\r\nif not "%errorlevel%"=="0" exit /b %errorlevel%\r\n'
               f'"{exe}" ' + ' '.join(f'"{p}"' for p in fixtures) + '\r\n')
if args.broadcast_consumer_only:
    cases = [args.broadcast_consumer_case] if args.broadcast_consumer_case else ["normal","journal","capture","device-log","focus","radial","steam-close","steam-interactive","focus-observation","msi-observation","msi-completion","failure-log","suspend-prepare","argument-allocation","common-normal","common-entry-allocation","common-entry-log","common-keep-journal-allocation","common-input-release","common-input-reset","common-input-rescan","common-handheld","common-resuming","common-notification-allocation","common-msi-observation","common-diagnostic-double","common-retry-automatic","common-duplicate-automatic","common-duplicate-interactive","common-native-queue-normal","common-native-queue-entry-allocation","common-native-queue-keep-allocation","common-native-queue-allocation-fails"]
    cmd_text = cmd_text[:cmd_text.index(f'"{exe}" --s3')] + ''.join(f'"{exe}" {case}\r\nif not "%errorlevel%"=="0" exit /b %errorlevel%\r\n' for case in cases)
elif args.resume_classification_public_facts_only:
    cmd_text = cmd_text[:cmd_text.index(f'"{exe}" --s3')] + f'"{exe}" --classification-public-facts-only\r\n'
elif args.resume_classification_publication_only:
    cmd_text = cmd_text[:cmd_text.index(f'"{exe}" --s3')] + f'"{exe}" --classification-publication-only\r\n'
elif args.resume_classification_lifetime_only:
    cmd_text = cmd_text[:cmd_text.index(f'"{exe}" --s3')] + f'"{exe}" --classification-lifetime-only\r\n'
elif args.resume_classification_edge_only:
    cmd_text = cmd_text[:cmd_text.index(f'"{exe}" --s3')] + f'"{exe}" --classification-edge-only\r\n'
elif args.resume_classification_only:
    cmd_text = cmd_text[:cmd_text.index(f'"{exe}" --s3')] + f'"{exe}"\r\nif not "%errorlevel%"=="0" exit /b %errorlevel%\r\n"{exe}" --s3\r\nif not "%errorlevel%"=="0" exit /b %errorlevel%\r\n"{exe}" --caps-unknown\r\n'
elif args.interrupted_entry_only:
    cmd_text = cmd_text[:cmd_text.index(f'"{exe}" --s3')] + f'"{exe}" --interrupted-entry-only\r\n'
elif args.suspend_admission_only:
    cmd_text = cmd_text[:cmd_text.index(f'"{exe}" --s3')] + f'"{exe}" --suspend-admission-only\r\n'
cmd.write_text(cmd_text, encoding='ascii')
result = subprocess.run(['cmd.exe', '/d', '/c', str(cmd)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(out / 'native-selftest.log').write_bytes(result.stdout)
print(result.stdout.decode('utf-8', errors='replace'))
raise SystemExit(result.returncode)
