"""Compile original sleep accounting and source consumers with inert Windows ports.
No real ETW session, registry write, app, power or device action is performed.
"""
from pathlib import Path
import argparse,datetime,hashlib,json,os,re,shutil,subprocess,sys,time
sys.stdout.reconfigure(encoding='utf-8')

def static_wake_parser_binding(native_text,main_text):
    """Bind exact production helpers, types and parser; no replacement parser model."""
    def section(text,start,end):
        assert text.count(start)==1 and text.count(end)==1,'static armed parser extraction boundary changed'
        return text[text.index(start):text.index(end)]
    conversions_start=main_text.index('static std::string W2U(const wchar_t*')
    conversions_end=main_text.index('\n}\n',main_text.index('static std::wstring U2W(',conversions_start))+3
    conversions=main_text[conversions_start:conversions_end]
    helpers=section(native_text,'inline std::string trim(','// Microsoft C/C++ argv quoting.')
    splitter=section(native_text,'inline std::vector<std::string> lines(','inline bool decode(')
    parser=section(native_text,'struct Node {','inline std::wstring property(')
    return ('// Generated from exact production source definitions; no device/command APIs.\n'
            '#pragma once\n#define YMCC_STATIC_WAKE_NATIVE_PARSER_BOUND 1\n'
            '#include <windows.h>\n#include <cfgmgr32.h>\n#include <algorithm>\n#include <set>\n#include <sstream>\n#include <string>\n#include <vector>\n'
            'namespace ymcc { namespace staticwake { namespace native {\n'+conversions+helpers+splitter+parser+'\n}}}\n')

started=time.perf_counter()
p=argparse.ArgumentParser();p.add_argument('--output-dir',required=True)
p.add_argument('--component-scope',choices=['all','abnormal','timeout','late-wake'],default='all')
p.add_argument('--mutation',choices=['none','hardware-zero-confirmed','missing-as-zero','callback-success-ignored','accepted-s4-shutdown','terminal-cleanup-removed','stale-evidence-permitted','previous-session-health-permitted','owned-intent-veto-lost','owned-budget-refilled','etw-loss-counters-ignored','etw-post-query-veto-ignored','etw-post-query-freshness-ignored','etw-terminal-health-dropped','stale-scan-start-accepted','conservative-arm-regressed','conservative-owned-regressed','global-snapshot-failure-retried','wake-armed-locale-regressed','late-wake-monitor-sender'],default='none');args=p.parse_args()
if args.component_scope=='timeout' and args.mutation!='none':p.error('timeout exact scope supports only the independent none group')
if args.component_scope=='late-wake' and args.mutation not in ['none','late-wake-monitor-sender']:p.error('late-wake exact scope refuses unrelated mutations')
if args.mutation=='late-wake-monitor-sender' and args.component_scope!='late-wake':p.error('monitor negative requires exact late-wake scope')
late_enabled=args.component_scope=='late-wake' or (args.component_scope=='all' and args.mutation=='none')
exact_scope=args.component_scope in ['timeout','late-wake']
project=Path(__file__).resolve().parents[1];out=Path(os.path.abspath(args.output_dir));out.mkdir(parents=True,exist_ok=True)
for path in [project,out]:
    if any(c in str(path) for c in '"%\r\n'):raise SystemExit('Unsupported command path')
cpp=project/'tools/sleep_residency_evidence_selftest.cpp'
# Export-only helper and actual adapter are compiled in both scopes, same execution.
export_inputs=[project/'native/sleep_export_diagnostics.h',project/'native/sleep_export_diagnostics_native.inc',project/'tools/fixtures/sleep_residency/sleep_export_harness.inc']
# Identity is bound in every mutation; only none copies/compiles this independent group.
activity_inputs=[project/'native/sleep_activity_evidence.h',project/'native/sleep_activity_evidence_native.inc',project/'native/sleep_notification_evidence.h',project/'tools/fixtures/sleep_residency/sleep_activity_native_harness.inc']
timeout_inputs=[project/'native/sleep_timeout_policy.h',project/'native/sleep_timeout_native.inc',project/'tools/fixtures/sleep_residency/sleep_timeout_native_harness.inc']
late_inputs=[project/'tools/fixtures/sleep_residency/latewake_cases.inc',project/'tools/fixtures/sleep_non_user_wake/amd_rog_late_spontaneous_ac.json',project/'native/sleep_abnormal_resleep.h']
copy_paths=[project/'native'/name for name in ['sleep_abnormal_resleep.h','sleep_abnormal_resleep_native.inc','sleep_abnormal_etw_native.inc','sleep_request_fallback.h','sleep_retry_budget.h']]
copy_paths += [project/'tools/fixtures/sleep_residency'/name for name in ['abnormal_native_harness.inc','cases.json']]
if args.component_scope=='all':
    copy_paths += [project/'native'/name for name in ['sleep_static_wake_policy.h','sleep_static_wake_receipt.h','sleep_static_wake_native.inc','sleep_static_wake_schedule.h','sleep_static_wake_schedule_native.inc']]
    copy_paths += [project/'tools/fixtures/sleep_residency'/name for name in ['static_wake_harness.inc','static_wake_schedule_harness.inc']]
if exact_scope:
    copy_paths=[project/'native/sleep_request_fallback.h',project/'native/sleep_retry_budget.h']
if args.mutation=='none' or late_enabled:
    copy_paths += timeout_inputs
    if not exact_scope:copy_paths += activity_inputs
if late_enabled:copy_paths+=late_inputs
copy_paths += [cpp,project/'deps/json/json.hpp',*export_inputs]
header=project/'native/sleep_residency_evidence.h'
source_paths=[header,project/'native/main.cpp',Path(__file__).resolve(),project/'tools/fixtures/hc_reviewed_calibration/toolchain.mjs',*copy_paths,*activity_inputs,*timeout_inputs,*late_inputs]
source_hashes={f:hashlib.sha256(f.read_bytes()).hexdigest() for f in source_paths}
late_payload=project/'tools/fixtures/sleep_non_user_wake/amd_rog_late_spontaneous_ac.json'
late_provenance=None
if late_enabled:
    payload=json.loads(late_payload.read_text('utf-8-sig'))
    assert payload['schemaVersion']==1 and payload['realPowerApiCallsAllowed'] is False
    assert source_hashes[late_payload]=='7fd2cc4f2c964935237fb209a2a60d9468fc0b3faffbb1d3a92a6685c4327377','reviewed late-wake payload identity changed'
    assert len(payload['sources'])==2 and sum(len(s['facts']) for s in payload['sources'])==59
    source_rows={s['id']:s for s in payload['sources']}
    for row in payload['sources']:
        assert row['selectedLineNumbers']==[f['line'] for f in row['facts']]
        assert len(set(row['selectedLineNumbers']))==len(row['facts'])
        assert all(re.fullmatch('[0-9a-f]{64}',row[k]) for k in ['archiveSha256','memberSha256'])
    checked=[]
    for case in payload['cases']:
        if case['origin']=='synthetic-control':
            assert case['sourceId'] is None and case['acDuringWholeSleep'] is True and case['acChanges']==0 and case['deviceChanges']==0
            checked.append({'id':case['id'],'origin':case['origin'],'physicalSource':False});continue
        source_row=source_rows[case['sourceId']];facts={f['line']:f['record'] for f in source_row['facts']}
        entry=facts[case['entryLine']]['details'];exit=facts[case['exitLine']]['details'];res=facts[case['residencyLine']]['details'];values=res['values']
        assert entry['eventId']==506 and entry['reason']==1 and entry['fileTime']==case['entryFileTime']
        assert exit['eventId']==507 and exit['reason']==case['reason'] and exit['reason']!=1 and exit['fileTime']==case['exitFileTime']
        parse=lambda x:datetime.datetime.strptime(x,'%Y-%m-%d %H:%M:%S.%f')
        elapsed=round((parse(exit['fileTime'])-parse(entry['fileTime'])).total_seconds()*1000)
        assert elapsed==case['elapsedFromFormattedKernelTimesMs']
        for key,field in [('durationUs','DurationInUs'),('softwareDripsUs','DripsResidencyInUs'),('hardwareDripsUs','HwDripsResidencyInUs'),('sleepEntered','SleepEntered')]:assert case[key]==values[field]
        assert case['durationUs']//1000==elapsed
        dispatch=facts[case['acDispatchLine']]['details'];old=facts[case['oldDispatchLine']]['details']
        assert dispatch['environment']['acLineStatus']==int(case['acAtDispatch']) and dispatch['accepted']==case['oldDispatchAccepted'] and dispatch['api']==case['oldApi'] and old['api']==case['oldApi']
        if 'acAtSourceLine' in case:assert facts[case['acAtSourceLine']]['details']['ac']==case['acAtWake']
        if 'manualWakeLine' in case:
            manual=facts[case['manualWakeLine']]['details'];assert manual['eventId']==507 and manual['reason']==1 and manual['fileTime']==case['manualWakeFileTime']
            assert facts[case['manualCancelLine']]['event']=='s0-sleep-retry-canceled-user-wake'
        assert case['acDuringWholeSleep']=='UNKNOWN'
        checked.append({'id':case['id'],'origin':case['origin'],'sourceId':case['sourceId'],'entryLine':case['entryLine'],'exitLine':case['exitLine'],'residencyLine':case['residencyLine'],'rawReason':exit['reason'],'rawDurationUs':values['DurationInUs'],'elapsedFromOriginalEventTimesMs':elapsed,'acAtDispatch':case['acAtDispatch'],'wholeSleepAc':'UNKNOWN'})
    assert len(checked)==3
    late_provenance={'payloadPath':str(late_payload.relative_to(project)).replace('\\','/'),'payloadSha256':source_hashes[late_payload],'factRecordsChecked':59,'archiveExtraction':'payload fact records previously hash-verified by owner; no repeated unzip or archive replay','cases':checked}
for f in copy_paths:(out/f.name).write_bytes(f.read_bytes())
if args.component_scope!='all':
    text=(out/cpp.name).read_text('utf-8-sig').replace('#include "static_wake_harness.inc"','// Static-wake companion explicitly NOT_RUN in this partial execution.')
    text=text.replace('const auto staticWakeChecks=staticWakePolicyTests();','const unsigned staticWakeChecks=0;').replace('check(staticWakeChecks>0,"static wake harness executes nonzero checks");','')
    text=text.replace('#include "static_wake_schedule_harness.inc"','// Static-wake scheduler explicitly NOT_RUN in this partial execution.')
    text=text.replace('const auto staticWakeScheduleChecks=staticWakeScheduleTests();','const unsigned staticWakeScheduleChecks=0;').replace('check(staticWakeScheduleChecks>0,"static wake schedule harness executes nonzero checks");','')
    (out/cpp.name).write_text(text,encoding='utf-8')
source=(project/'native/main.cpp').read_text('utf-8-sig')
# Bind the exact post-journal production dispatch, never a copy of disk IO.
activity_hook_anchor='try { sgSleepActivityOnFact(event,details); } catch (...) {}'
assert source.count(activity_hook_anchor)==1,'activity fact hook missing or nonunique'
activity_hook_bytes=('// Exact unique production sgRecordFact observer dispatch.\n'+activity_hook_anchor+'\n').encode('utf-8')
activity_field_bindings=[]
for event,field,expression,clock_scope in [
    ('user-initiated-sleep','fileTime100ns','eventFileTime','original-kernel-506-event'),
    ('s0-wake-message','latestKernel507FileTime100ns','g_sgLastKernel507EventFileTime.load(std::memory_order_acquire)','latest-callback-high-water-not-exact-window-message')]:
    record_start='sgRecordFact("'+event+'", {'
    assert source.count(record_start)==1,'activity original fact missing or nonunique: '+event
    begin=source.index(record_start);end=source.index('\n        });',begin)
    record=source[begin:end]
    field_anchor='{"'+field+'", '+expression+'}'
    assert record.count(field_anchor)==1,'activity original field source changed: '+event+'.'+field
    activity_field_bindings.append({'event':event,'field':field,'expression':expression,'clockScope':clock_scope})
activity_hook_binding={'path':'production_activity_fact_hook.inc','sha256':hashlib.sha256(activity_hook_bytes).hexdigest(),
    'sourcePath':'native/main.cpp','sourceSha256':source_hashes[project/'native/main.cpp'],
    'sourceLine':source[:source.index(activity_hook_anchor)].count('\n')+1,'fieldBindings':activity_field_bindings}
if args.mutation=='none':(out/'production_activity_fact_hook.inc').write_bytes(activity_hook_bytes)
# Main.cpp declaration slices retain original bytes/order and carry explicit source offsets.
# Unrelated recovery/privilege/game IO are named inert ports in the native harness.
timeout_artifacts=[]
def timeout_slices(filename,names,kind,artifacts=None):
    if artifacts is None:artifacts=timeout_artifacts
    chunks=[];records=[]
    for name in names:
        if kind=='function':
            matches=list(re.finditer(r'^static [^\n]*\b'+re.escape(name)+r'\([^;]*?\)\s*\{',source,re.M))
            assert len(matches)==1,'timeout function definition missing or nonunique: '+name
            begin=matches[0].start();end=source.index('\n}\n',matches[0].end())+3
        elif kind in ['late-message','late-manual']:
            if kind=='late-message':
                begin=source.index('    case WM_SG_S0_WAKE: {')+len('    case WM_SG_S0_WAKE: {')
                end=source.index('    case WM_SG_S4_WAKE:',begin)
                end=source.rfind('    }',begin,end)
                assert source[begin:end].count('sgNoteExternalDeviceKernel507Reason5();')==1
                assert source[begin:end].count('if (w == 1)')==1
            else:
                cb=source.index('static DWORD WINAPI sgKernelPowerEventCallback(')
                begin=source.index('    if(leaving&&reason1) {',cb)
                end=source.index('\n    }',begin)+6
                assert end<source.index('sgRecordFact(',cb)<source.index('PostMessageW(',cb),'manual callback highwater must precede log and UI'
        elif kind=='late-declaration':
            matches=list(re.finditer(r'^static [^\n]*\b'+re.escape(name)+r'\b[^;]*;',source,re.M))
            assert len(matches)==1,'late declaration missing or nonunique: '+name
            begin,end=matches[0].span()
        elif kind=='s4-callback':
            callback_begin=source.index('static DWORD WINAPI sgKernelPowerEventCallback(')
            callback_end=source.index('\n}\n',callback_begin)+3
            callback=source[callback_begin:callback_end]
            if name=='hibernateWake':
                anchor='    const bool hibernateWake = eventId == 107 &&'
                local_begin=callback.index(anchor);local_end=callback.index(';',local_begin)+1
            else:
                anchor='    if(hibernateWake&&eventFileTime)sgSleepTimeoutRecordSystemSuccess(eventFileTime);'
                assert callback.count(anchor)==1,'S4 original-time success callback hook missing or nonunique'
                local_begin=callback.index(anchor);local_end=local_begin+len(anchor)
                assert callback.index('const ULONGLONG eventFileTime =')<local_begin<callback.index('sgRecordFact(')<callback.index('PostMessageW('),'S4 success must precede log/UI using original event time'
            begin=callback_begin+local_begin;end=callback_begin+local_end
        elif name in ['SgSleepMode','SgRetryKind']:
            matches=list(re.finditer(r'^enum class '+name+r'\b[^\n]+',source,re.M))
            assert len(matches)==1,'timeout enum definition missing or nonunique: '+name
            begin,end=matches[0].span()
        elif name=='SgSystemSleepRequestResult':
            begin=source.index('struct SgSystemSleepRequestResult {');end=source.index('\n};',begin)+3
        else:
            matches=list(re.finditer(r'^static (?:constexpr|bool)[^;\n]*\b'+re.escape(name)+r'\b[^;]*;',source,re.M))
            assert len(matches)==1,'timeout declaration missing or nonunique: '+name
            begin,end=matches[0].span()
        chunk=source[begin:end];chunks.append(chunk)
        records.append({'name':name,'begin':len(source[:begin].encode()),'end':len(source[:end].encode()),'sha256':hashlib.sha256(chunk.encode()).hexdigest()})
    prefix='// Exact main.cpp slices; inert ports supplied by sleep_timeout_native_harness.inc.\n'
    payload=(prefix+'\n\n'.join(chunks)+'\n').encode()
    artifacts.append({'path':filename,'sha256':hashlib.sha256(payload).hexdigest(),'prefix':prefix,'separator':'\n\n','suffix':'\n','slices':records})
    if args.mutation=='none' or late_enabled:(out/filename).write_bytes(payload)
timeout_slices('production_timeout_types.inc',['SgSleepMode','SgRetryKind','SgSystemSleepRequestResult','g_sgRetrySleepTimeout','SG_ENTRY_RETRY_DELAYS_MS','SG_PAUSE_READY_WAIT_MAX_MS','SG_PAUSE_READY_POLL_MS','SG_MAX_ENTRY_RETRIES','SG_NON_USER_WAKE_CLASSIFY_MS'],'declaration')
timeout_slices('production_timeout_bindings.inc',['sgUserWakeCancelsSleepIntent','sgPowerButtonSleepMode','sgRetryKindName','sgRequestSystemSleep','sgScheduleNextSleepRetry','sgDispatchSameModeRetry','sgStartSleepRetry','sgAdvanceSleepRetry'],'function')
timeout_slices('production_timeout_s4_hook.inc',['hibernateWake','original107-success-before-log'],'s4-callback')
timeout_binding={'sourcePath':'native/main.cpp','sourceSha256':source_hashes[project/'native/main.cpp'],'normalization':'utf8-sig-universal-newlines','artifacts':timeout_artifacts}
late_artifacts=[]
timeout_slices('production_late_wake_types.inc',['SG_USER_STANDBY_DEVICE_DELAY_MS','SG_S0_REASON7_FAILURE_WINDOW_MS','SG_INTERNAL_SLEEP_506_WINDOW_MS','SG_INTERNAL_SLEEP_506_CLOCK_SKEW_MS','g_sgFactMonitorEnabled'],'late-declaration',late_artifacts)
timeout_slices('production_late_wake_bindings.inc',['sgAutomaticClassicSleepPending','sgPublishSleepEventTime','sgInternalSleepRequestMatches506','sgSleepTriggerOrigin','sgRefreshSleepTriggerOrigin','sgNonUserWakeGuardEnabled','sgExternalDeviceWakeIntentAge','sgClearUnexpectedWake','sgArmNonUserWakeGuard','sgEvaluateExternalDeviceWake','sgObserveNonUserWake','sgNoteExternalDeviceNodeChange','sgNoteExternalDeviceAcDcChange','sgNoteExternalDeviceKernel507Reason5','sgNoteExternalDeviceKernel507Reason7','sgS0EntryFailureEligible'],'function',late_artifacts)
timeout_slices('production_late_wake_message.inc',['WM_SG_S0_WAKE-complete-body'],'late-message',late_artifacts)
timeout_slices('production_late_wake_manual_callback.inc',['manual507-callback-before-log-UI'],'late-manual',late_artifacts)
late_binding={'sourcePath':'native/main.cpp','sourceSha256':source_hashes[project/'native/main.cpp'],'normalization':'utf8-sig-universal-newlines','artifacts':late_artifacts}
if late_enabled:
    decision_names=['sgNonUserWakeGuardEnabled','sgExternalDeviceWakeIntentAge','sgObserveNonUserWake','sgEvaluateExternalDeviceWake','sgRequestSystemSleep']
    snippets=[source.encode()[s['begin']:s['end']].decode() for a in [*timeout_artifacts,*late_artifacts] for s in a['slices'] if s['name'] in decision_names]
    assert len(snippets)==len(decision_names) and all('g_domainLogEnabled' not in s and 'sensorDetailLoggingGate' not in s and 'g_sgFactMonitorEnabled' not in s for s in snippets),'late detection/sender gained log-frequency precondition'

start='    if (leaving) {\n        // Preserve the OS\'s completed-session accounting in ordinary exports.'
end='    traceLog("kernel power event id=%d reason=%s targetState=%d", eventId,'
assert source.count(start)==1 and source.count(end)==1,'native507capture boundary changed'
hook=source[source.index(start):source.index(end)]
assert hook.count('sgRecordFact("s0-low-power-residency", residency);')==1
(out/'production_capture_hook.inc').write_text(hook,encoding='utf-8')
original=header.read_text('utf-8-sig');mutated=original;expected_failure=None
if args.mutation=='late-wake-monitor-sender':
    request_file=out/'production_timeout_bindings.inc';text=request_file.read_text('utf-8');anchor='const BOOL accepted=SetSuspendState(FALSE,FALSE,FALSE);'
    assert text.count(anchor)==1
    request_file.write_text(text.replace(anchor,'const BOOL accepted=SendMessageTimeoutW(g_hwnd,WM_SYSCOMMAND,SC_MONITORPOWER,2,SMTO_ABORTIFHUNG,1000,nullptr)!=0;'),encoding='utf-8',newline='\n')
    expected_failure='late-wake shared sender requires tripleFALSE and zero monitor calls'
if args.mutation=='hardware-zero-confirmed':
    assert original.count('"hardware-zero-validity-unverified"')==1
    mutated=original.replace('"hardware-zero-validity-unverified"','"no-hardware-drips-confirmed"');expected_failure='hardware zero cannot prove physical failure: assessment'
elif args.mutation=='missing-as-zero':
    anchor='if (it == data.values.end()) continue; // Missing must never become zero.';assert original.count(anchor)==1
    mutated=original.replace(anchor,'if (it == data.values.end()) { values[field] = 0; continue; }');expected_failure='missing hardware on old event version: assessment'
(out/'sleep_residency_evidence.h').write_text(mutated,encoding='utf-8')
runtime=out/'sleep_abnormal_resleep_native.inc';policy=out/'sleep_abnormal_resleep.h'
if args.mutation=='callback-success-ignored':
    text=runtime.read_text('utf-8-sig');anchor='g_sgAbnormalSuccessOrigin.store(origin,std::memory_order_release);';assert text.count(anchor)>=2
    runtime.write_text(text.replace(anchor,'(void)origin;',1),encoding='utf-8');expected_failure='callback-side Windows success immediately revokes dispatch before UI queue'
elif args.mutation=='accepted-s4-shutdown':
    text=policy.read_text('utf-8-sig');anchor='if(stage==3&&(!hibernateRejected||hibernateAccepted))';assert text.count(anchor)==1
    policy.write_text(text.replace(anchor,'if(false)'),encoding='utf-8');expected_failure='accepted-but-unconfirmed S4 never authorizes shutdown'
elif args.mutation=='terminal-cleanup-removed':
    text=runtime.read_text('utf-8-sig');anchor='sgAbnormalStop("normal-poweroff-attempt");';assert text.count(anchor)==1
    runtime.write_text(text.replace(anchor,'// intentionally removed terminal cleanup'),encoding='utf-8');expected_failure='shutdown success or failure is terminal with no source or timer'
elif args.mutation=='stale-evidence-permitted':
    text=policy.read_text('utf-8-sig');anchor='const bool fresh=badEnd&&fileTime>=badEnd&&fileTime-badEnd<=freshMs*10000;';assert text.count(anchor)==1
    policy.write_text(text.replace(anchor,'const bool fresh=true;'),encoding='utf-8');expected_failure='stale positive evidence cannot authorize action'
elif args.mutation=='previous-session-health-permitted':
    text=policy.read_text('utf-8-sig');anchor='(o.eventId!=507||(o.begin>=phase&&o.begin<o.end))';assert text.count(anchor)==1
    policy.write_text(text.replace(anchor,'true'),encoding='utf-8');expected_failure='old507 spanning prior session cannot veto a new owned retry before UI queue'
elif args.mutation=='owned-intent-veto-lost':
    text=runtime.read_text('utf-8-sig');anchor='g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire)<g_sgAbnormalHumanIntent.load(std::memory_order_acquire);';assert text.count(anchor)==1
    runtime.write_text(text.replace(anchor,'g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire)<origin;'),encoding='utf-8');expected_failure='manual wake after original intent wins even when timestamp precedes owned request'
elif args.mutation=='owned-budget-refilled':
    # Historical entry is explicit; production Owned is observation-only.
    historical=out/'abnormal_native_harness.inc';text=historical.read_text('utf-8-sig');anchor='return sgAbnormalStartPhase(originalHumanIntent,requestFileTime,true);';assert text.count(anchor)==1
    historical.write_text(text.replace(anchor,'g_sgAutomaticSleepBudget.confirmedUserWake();\n    '+anchor),encoding='utf-8');expected_failure='owned phase cannot refill original shared budget'
elif args.mutation=='etw-loss-counters-ignored':
    etw=out/'sleep_abnormal_etw_native.inc';text=etw.read_text('utf-8-sig');anchor='status!=ERROR_SUCCESS||source.properties.value.EventsLost||source.properties.value.LogBuffersLost||source.properties.value.RealTimeBuffersLost';assert text.count(anchor)==1
    etw.write_text(text.replace(anchor,'status!=ERROR_SUCCESS'),encoding='utf-8');expected_failure='all ETW lost-event and buffer counters veto due power requests'
elif args.mutation=='etw-post-query-veto-ignored':
    text=runtime.read_text('utf-8-sig');anchor='return sgAbnormalPermitted()&&g_sgAbnormalDisplayOff&&!sgSleepRetryActive()&&';assert text.count(anchor)==1
    runtime.write_text(text.replace(anchor,'return g_sgAbnormalDisplayOff&&!sgSleepRetryActive()&&'),encoding='utf-8');expected_failure='human wake during either final blocking source query vetoes actual sleep adapter'
if args.mutation=='etw-post-query-freshness-ignored':
    text=runtime.read_text('utf-8-sig');anchor='const auto decision=same?g_sgAbnormalEpisode.dispatch(g_sgAbnormalDispatchStage,nowTick,nowFileTime):Decision{};';assert text.count(anchor)==1
    stale='const auto decision=same?g_sgAbnormalEpisode.dispatch(g_sgAbnormalDispatchStage,g_sgAbnormalEpisode.startTick+Episode::deadlines[g_sgAbnormalDispatchStage],g_sgAbnormalEpisode.badEnd):Decision{};'
    runtime.write_text(text.replace(anchor,stale),encoding='utf-8')
    expected_failure='all final blocking source queries recheck freshness and dispatch deadline before power actions'
if args.mutation=='etw-terminal-health-dropped':
    etw=out/'sleep_abnormal_etw_native.inc';text=etw.read_text('utf-8-sig');anchor='if(!event||event->UserContext!=&source||';assert text.count(anchor)==1
    etw.write_text(text.replace(anchor,anchor+'source.stopping.load(std::memory_order_acquire)||',1),encoding='utf-8')
    expected_failure='Windows healthy ETW delivered during terminal source drain vetoes shutdown'
elif args.mutation=='stale-scan-start-accepted':
    schedule=out/'sleep_static_wake_schedule_native.inc';text=schedule.read_text('utf-8-sig')
    anchor='    // A queued start cannot acquire the revision created by a later sleep/OFF.\n    if(static_cast<ULONGLONG>(revision)!=g_sgStaticScanRevision.load(std::memory_order_acquire))return;';assert text.count(anchor)==1
    text=text.replace(anchor,'    // Deliberately accept a stale queued start.',1)
    # The CAS is the second stale-message fence after the initial check.
    anchor='if(!g_sgStaticScanRevision.compare_exchange_strong(expected,armed,std::memory_order_acq_rel))return;';assert text.count(anchor)==1
    text=text.replace(anchor,'// Fault: commit without the carried-revision CAS.')
    anchor='const auto armed=expected+1;';assert text.count(anchor)==1
    schedule.write_text(text.replace(anchor,'const auto armed=g_sgStaticScanRevision.fetch_add(1,std::memory_order_acq_rel)+1;'),encoding='utf-8')
    expected_failure='old queued startup message cannot overtake a newer sleep cancellation'
if args.mutation=='conservative-arm-regressed':
    text=runtime.read_text('utf-8-sig');anchor='sgAbnormalRecordConservativeIntent(eventTime,eventTime,"power-button-506");';assert text.count(anchor)==1
    runtime.write_text(text.replace(anchor,anchor+'\n    sgAbnormalStartPhase(eventTime,eventTime,false);'),encoding='utf-8')
    expected_failure='real production Arm is receipt-only with zero ETW worker timer or power start'
elif args.mutation=='conservative-owned-regressed':
    text=runtime.read_text('utf-8-sig');anchor='sgAbnormalRecordConservativeIntent(originalHumanIntent,requestFileTime,"legacy-owned-retry");\n    return false;';assert text.count(anchor)==1
    runtime.write_text(text.replace(anchor,anchor.replace('return false;','return sgAbnormalStartPhase(originalHumanIntent,requestFileTime,true);')),encoding='utf-8')
    expected_failure='real production Owned hook always returns false without vetoing or replacing the legacy transaction'
elif args.mutation=='global-snapshot-failure-retried':
    static=out/'sleep_static_wake_policy.h';text=static.read_text('utf-8-sig')
    anchor='if(!stopped_)return false;out=state_;return true;';assert text.count(anchor)==1
    text=text.replace(anchor,'out=state_;return false;')
    anchor='if(stopped_){error=state_.error;return false;}';assert text.count(anchor)==1
    static.write_text(text.replace(anchor,'// Fault: repeat the globally failed query.'),encoding='utf-8')
    expected_failure='global snapshot failure cannot be reported as applied or authorize writes'
elif args.mutation=='wake-armed-locale-regressed':
    static=out/'sleep_static_wake_native.inc';text=static.read_text('utf-8-sig')
    anchor=r'ls.front()=="NONE"||ls.front()=="\xE6\x97\xA0"';assert text.count(anchor)==1
    static.write_text(text.replace(anchor,'ls.front()=="NONE"'),encoding='utf-8')
    expected_failure='actual armed parser accepts supported single empty marker without inventory collision'
if args.component_scope=='all':
    native_text=(out/'sleep_static_wake_native.inc').read_text('utf-8-sig')
    assert native_text.count('if(!parseArmed(text,inv,armedNames,deviceError))problem("devices",deviceError);')==1,'collection must bind the same actual armed parser'
    (out/'static_wake_parser_binding.inc').write_text(static_wake_parser_binding(native_text,source),encoding='utf-8')
    run_start='inline Json run(bool restore,std::function<bool()> canceled) {'
    run_end='\n}}} // namespace'
    assert native_text.count(run_start)==1,'static run receipt extraction start changed'
    begin=native_text.index(run_start);end=native_text.index(run_end,begin)
    (out/'static_wake_run_binding.inc').write_text('// Exact native operation function; inert ports supplied by the existing harness.\n'+native_text[begin:end],encoding='utf-8')
vcvars=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
if not vcvars.exists():raise SystemExit('MSVC unavailable; sources preserved')
exe=out/'selftest.exe';bat=out/'compile.bat'
toolchain_script='import {calibrationToolchainPreamble} from "./tools/fixtures/hc_reviewed_calibration/toolchain.mjs";process.stdout.write(calibrationToolchainPreamble(process.argv[1]));'
toolchain=subprocess.run(['node','--input-type=module','-e',toolchain_script,str(vcvars)],cwd=project,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace',check=True)
identity_commands='where cl.exe >compiler-path.txt\nwhere link.exe >linker-path.txt\n'
# Compile the frozen files from their own directory: absolute filenames can
# exceed legacy CL path limits under the registered consumer's evidence layout.
export_define=" /DYMCC_SLEEP_EXPORT_DIAGNOSTICS_SHARED=1 /DYMCC_SLEEP_ACTIVITY_NATIVE_SHARED=1 /DYMCC_SLEEP_TIMEOUT_NATIVE_SHARED=1" if args.mutation!="none" else ""
if args.component_scope=="timeout":export_define+=" /DYMCC_SLEEP_TIMEOUT_ONLY=1"
if late_enabled:export_define+=" /DYMCC_SLEEP_LATE_WAKE_ENABLE=1"
if args.component_scope=="late-wake":export_define=" /DYMCC_SLEEP_LATE_WAKE_ENABLE=1 /DYMCC_SLEEP_LATE_WAKE_ONLY=1"
if args.mutation=="late-wake-monitor-sender":export_define+=" /DYMCC_SLEEP_LATE_WAKE_MONITOR_MUTATION=1 /wd4505" # Exact one-case negative leaves other original static helpers unused; keep /WX and semantic checks.
bat.write_text(toolchain.stdout+identity_commands+f'cl /nologo {export_define} /std:c++20 /utf-8 /EHsc /W4 /WX /MT /sourceDependencies "dependencies.json" /I"." "{cpp.name}" /Fo"selftest.obj" /Fe"selftest.exe"\n',encoding='ascii')
prepared=time.perf_counter()
compile_run=subprocess.run([os.environ.get('COMSPEC','cmd.exe'),'/d','/c',str(bat)],cwd=out,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
(out/'compile.log').write_text(compile_run.stdout,encoding='utf-8');compiled=time.perf_counter()
if compile_run.returncode:
    (out/'receipt.json').write_text(json.dumps({'status':'failed','phase':'compile','exit':compile_run.returncode,'sourceInputs':[{'path':str(f.relative_to(project)),'sha256':v} for f,v in source_hashes.items()]},indent=2),encoding='utf-8')
    print(compile_run.stdout);raise SystemExit(compile_run.returncode)
run=subprocess.run([str(exe),'amd_rog_late_spontaneous_ac.json' if args.component_scope=='late-wake' else 'cases.json','result.json'],cwd=out,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
(out/'run.log').write_text(run.stdout,encoding='utf-8');finished=time.perf_counter()
ok=(run.returncode==0) if expected_failure is None else (run.returncode!=0 and expected_failure in run.stdout)
source_identity_stable=all(hashlib.sha256(f.read_bytes()).hexdigest()==value for f,value in source_hashes.items())
ok=ok and source_identity_stable
receipt={'status':'passed' if ok else 'failed','componentScope':args.component_scope,'staticWakeStatus':('EXECUTED' if run.returncode==0 else 'NOT_REACHED') if args.component_scope=='all' else 'NOT_RUN','mutation':args.mutation,'compileExit':compile_run.returncode,'runExit':run.returncode,'expectedFaultCaught':expected_failure is not None and ok,'expectedFailure':expected_failure,'output':run.stdout.strip(),'sourceIdentityStable':source_identity_stable,'realSleepTriggered':False,'realEtwStarted':False,'realSleepStudyQuery':False,'productionHeaderSha256':source_hashes[header],'compiledHeaderSha256':hashlib.sha256((out/'sleep_residency_evidence.h').read_bytes()).hexdigest(),'productionHeaderNormalizedSha256':hashlib.sha256(original.encode()).hexdigest(),'productionHookSha256':hashlib.sha256(hook.encode()).hexdigest()}
receipt['sleepExportDiagnostics']={'status':('EXECUTED' if run.returncode==0 else 'NOT_REACHED') if args.mutation=='none' and not exact_scope else ('NOT_RUN_'+args.component_scope.upper().replace('-','_')+'_SCOPE' if exact_scope else 'NOT_RUN_MUTATION_SCOPE'),'inputs':[{'path':str(f.relative_to(project)).replace('\\','/'),'sha256':source_hashes[f]} for f in export_inputs]}
receipt['sleepActivityEvidence']={'status':('EXECUTED' if run.returncode==0 else 'NOT_REACHED') if args.mutation=='none' and not exact_scope else ('NOT_RUN_'+args.component_scope.upper().replace('-','_')+'_SCOPE' if exact_scope else 'NOT_RUN_MUTATION_SCOPE'),'executedThisRun':args.mutation=='none' and not exact_scope,'actualPassedThisRun':args.mutation=='none' and not exact_scope and ok,'factHook':activity_hook_binding,'inputs':[{'path':str(f.relative_to(project)).replace('\\','/'),'sha256':source_hashes[f]} for f in activity_inputs]}
receipt['sleepTimeoutNative']={'status':'NOT_RUN_LATE_WAKE_SCOPE' if args.component_scope=='late-wake' else (('EXECUTED' if run.returncode==0 else 'NOT_REACHED') if args.mutation=='none' else 'NOT_RUN_MUTATION_SCOPE'),'executedThisRun':args.mutation=='none' and args.component_scope!='late-wake','actualPassedThisRun':args.mutation=='none' and args.component_scope!='late-wake' and ok,'binding':timeout_binding,'inputs':[{'path':str(f.relative_to(project)).replace('\\','/'),'sha256':source_hashes[f]} for f in timeout_inputs]}
receipt['lateWakeNative']={'status':('EXECUTED' if run.returncode==0 else 'EXPECTED_FAULT_CAUGHT' if ok else 'FAILED') if late_enabled else ('NOT_RUN_TIMEOUT_SCOPE' if args.component_scope=='timeout' else 'NOT_RUN_MUTATION_SCOPE'),'executedThisRun':late_enabled,'actualPassedThisRun':late_enabled and args.mutation=='none' and ok,'binding':late_binding,'inputs':[{'path':str(f.relative_to(project)).replace('\\','/'),'sha256':source_hashes[f]} for f in late_inputs],'provenance':late_provenance,'logGateBoundary':'Actual selected decision/sender snippets have no high-frequency/legacy-fact preference precondition. Ordinary fact output is an inert memory port, not disk writer validation.'}
if late_enabled and expected_failure is not None and (out/'result.json').exists():
    fault_raw=json.loads((out/'result.json').read_text('utf-8-sig'))
    receipt['lateWakeNative']['checks']=fault_raw['checks']
    receipt['lateWakeNative']['scenarios']=fault_raw['caseResults']
    assert fault_raw['componentScope']=='late-wake' and fault_raw['cases']==1 and fault_raw['caseResults'][0]['name']=='rog-stable-ac-20min-control' and fault_raw['caseResults'][0]['status']=='failed' and fault_raw['caseResults'][0]['monitorCalls']==1 and fault_raw['caseResults'][0]['mockSuspendCalls']==0,'MONITOR_FAULT_EXACT_SELECTION_NOT_PROVEN'
if run.returncode==0 and (args.mutation=='none' or late_enabled):
    native_result=json.loads((out/'result.json').read_text('utf-8-sig'))
    if args.component_scope!='late-wake':
        receipt['sleepTimeoutNative']['checks']=native_result['sleepTimeoutNativeChecks']
        receipt['sleepTimeoutNative']['scenarios']=native_result['sleepTimeoutNativeScenarios']
        assert receipt['sleepTimeoutNative']['checks']>0 and receipt['sleepTimeoutNative']['scenarios'],'TIMEOUT_COMPANION_NOT_EXECUTED'
    if late_enabled:
        receipt['lateWakeNative']['checks']=native_result['lateWakeNativeChecks']
        receipt['lateWakeNative']['scenarios']=native_result['lateWakeNativeScenarios']
        assert receipt['lateWakeNative']['checks']>0 and receipt['lateWakeNative']['scenarios'],'LATE_WAKE_COMPANION_NOT_EXECUTED'
    if not exact_scope:
        receipt['sleepExportDiagnostics']['checks']=native_result['sleepExportDiagnosticChecks']
        assert receipt['sleepExportDiagnostics']['checks']>0,'EXPORT_COMPANION_NOT_EXECUTED'
        receipt['sleepActivityEvidence']['checks']=native_result['sleepActivityNativeChecks']
        assert receipt['sleepActivityEvidence']['checks']>0,'ACTIVITY_COMPANION_NOT_EXECUTED'
receipt['sourceInputs']=[{'path':str(f.relative_to(project)).replace('\\','/'),'sha256':value} for f,value in source_hashes.items()]
receipt['compiledInputs']=[{'path':f.name,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in [out/'production_capture_hook.inc',out/'sleep_residency_evidence.h',*([out/'production_activity_fact_hook.inc'] if args.mutation=='none' else []),*([out/row['path'] for row in timeout_artifacts] if args.mutation=='none' or late_enabled else []),*([out/row['path'] for row in late_artifacts] if late_enabled else []),*[out/f.name for f in copy_paths],*([out/'static_wake_parser_binding.inc'] if args.component_scope=='all' else [])]]
dependencies=json.loads((out/'dependencies.json').read_text('utf-8-sig'))['Data']['Includes']
receipt['discoveredDependencies']=[{'path':str(f),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in map(Path,dependencies)]
tools=[Path(sys.base_prefix)/'python.exe',Path(os.environ.get('COMSPEC','C:/Windows/System32/cmd.exe')),Path(shutil.which('node'))]
for name in ['compiler-path.txt','linker-path.txt']:tools.append(Path((out/name).read_text('utf-8-sig').splitlines()[0]))
compiler_dir=tools[-2].parent
for name in ['c1xx.dll','c2.dll','mspdbcore.dll','msvcp140.dll','vcruntime140.dll']:
    if (compiler_dir/name).exists():tools.append(compiler_dir/name)
receipt['toolInputs']=[{'path':str(f),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in tools]
receipt['timingsMs']={'prepare':round((prepared-started)*1000,2),'compile':round((compiled-prepared)*1000,2),'execute':round((finished-compiled)*1000,2),'total':round((finished-started)*1000,2)}
receipt['selection']={'componentScope':args.component_scope,'mutation':args.mutation,'timeoutExecutedOnce':args.mutation=='none' and args.component_scope!='late-wake','lateWakeExecutedOnce':late_enabled,'otherResidencyGroupsExecuted':not exact_scope}
receipt['sourceDrift']=[{'path':str(f.relative_to(project)).replace('\\','/'),'before':value,'after':hashlib.sha256(f.read_bytes()).hexdigest()} for f,value in source_hashes.items() if hashlib.sha256(f.read_bytes()).hexdigest()!=value]
receipt['scope']='Late-wake scope executes only new actual nonuser helpers/S0 branch/manual callback/shared sender with synthetic and owner-verified archived payload facts; no physical root-cause or logging writer acceptance. Actual timeout policy/native include and exact shared sender/retry/action slices execute in none only; original506Reason1 required, no PBT/idle substitute, elapsed policy not high-power proof; exact timeout scope excludes other residency groups. Actual completed507 accounting and conservative production entries; staged engine/ETW regressions historical-only; static costs/scheduling per declared scope; all Windows boundaries mocked; activity observer compiles/executes in none only, other mutations require same-run reference; export-only SleepStudy adapter uses inert commands and owned local files; no physical CPU/sleep acceptance'
(out/'receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in receipt.items() if k not in ['sourceInputs','compiledInputs','discoveredDependencies','toolInputs']},ensure_ascii=False,indent=2))
raise SystemExit(0 if ok else 1)
