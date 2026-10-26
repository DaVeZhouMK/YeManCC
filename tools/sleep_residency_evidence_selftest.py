"""Compile original sleep accounting and source consumers with inert Windows ports.
No real ETW session, registry write, app, power or device action is performed.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys,time
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
p.add_argument('--component-scope',choices=['all','abnormal'],default='all')
p.add_argument('--mutation',choices=['none','hardware-zero-confirmed','missing-as-zero','callback-success-ignored','accepted-s4-shutdown','terminal-cleanup-removed','stale-evidence-permitted','previous-session-health-permitted','owned-intent-veto-lost','owned-budget-refilled','etw-loss-counters-ignored','etw-post-query-veto-ignored','etw-post-query-freshness-ignored','etw-terminal-health-dropped','stale-scan-start-accepted','conservative-arm-regressed','conservative-owned-regressed','global-snapshot-failure-retried','wake-armed-locale-regressed'],default='none');args=p.parse_args()
project=Path(__file__).resolve().parents[1];out=Path(os.path.abspath(args.output_dir));out.mkdir(parents=True,exist_ok=True)
for path in [project,out]:
    if any(c in str(path) for c in '"%\r\n'):raise SystemExit('Unsupported command path')
cpp=project/'tools/sleep_residency_evidence_selftest.cpp'
# Export-only helper and actual adapter are compiled in both scopes, same execution.
export_inputs=[project/'native/sleep_export_diagnostics.h',project/'native/sleep_export_diagnostics_native.inc',project/'tools/fixtures/sleep_residency/sleep_export_harness.inc']
copy_paths=[project/'native'/name for name in ['sleep_abnormal_resleep.h','sleep_abnormal_resleep_native.inc','sleep_abnormal_etw_native.inc','sleep_request_fallback.h','sleep_retry_budget.h']]
copy_paths += [project/'tools/fixtures/sleep_residency'/name for name in ['abnormal_native_harness.inc','cases.json']]
if args.component_scope=='all':
    copy_paths += [project/'native'/name for name in ['sleep_static_wake_policy.h','sleep_static_wake_receipt.h','sleep_static_wake_native.inc','sleep_static_wake_schedule.h','sleep_static_wake_schedule_native.inc']]
    copy_paths += [project/'tools/fixtures/sleep_residency'/name for name in ['static_wake_harness.inc','static_wake_schedule_harness.inc']]
copy_paths += [cpp,project/'deps/json/json.hpp',*export_inputs]
header=project/'native/sleep_residency_evidence.h'
source_paths=[header,project/'native/main.cpp',Path(__file__).resolve(),project/'tools/fixtures/hc_reviewed_calibration/toolchain.mjs',*copy_paths]
source_hashes={f:hashlib.sha256(f.read_bytes()).hexdigest() for f in source_paths}
for f in copy_paths:(out/f.name).write_bytes(f.read_bytes())
if args.component_scope!='all':
    text=(out/cpp.name).read_text('utf-8-sig').replace('#include "static_wake_harness.inc"','// Static-wake companion explicitly NOT_RUN in this partial execution.')
    text=text.replace('const auto staticWakeChecks=staticWakePolicyTests();','const unsigned staticWakeChecks=0;').replace('check(staticWakeChecks>0,"static wake harness executes nonzero checks");','')
    text=text.replace('#include "static_wake_schedule_harness.inc"','// Static-wake scheduler explicitly NOT_RUN in this partial execution.')
    text=text.replace('const auto staticWakeScheduleChecks=staticWakeScheduleTests();','const unsigned staticWakeScheduleChecks=0;').replace('check(staticWakeScheduleChecks>0,"static wake schedule harness executes nonzero checks");','')
    (out/cpp.name).write_text(text,encoding='utf-8')
source=(project/'native/main.cpp').read_text('utf-8-sig')
start='    if (leaving) {\n        // Preserve the OS\'s completed-session accounting in ordinary exports.'
end='    traceLog("kernel power event id=%d reason=%s targetState=%d", eventId,'
assert source.count(start)==1 and source.count(end)==1,'native507capture boundary changed'
hook=source[source.index(start):source.index(end)]
assert hook.count('sgRecordFact("s0-low-power-residency", residency);')==1
(out/'production_capture_hook.inc').write_text(hook,encoding='utf-8')
original=header.read_text('utf-8-sig');mutated=original;expected_failure=None
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
export_define=" /DYMCC_SLEEP_EXPORT_DIAGNOSTICS_SHARED=1" if args.mutation!="none" else ""
bat.write_text(toolchain.stdout+identity_commands+f'cl /nologo {export_define} /std:c++20 /utf-8 /EHsc /W4 /WX /MT /sourceDependencies "dependencies.json" /I"." "{cpp.name}" /Fo"selftest.obj" /Fe"selftest.exe"\n',encoding='ascii')
prepared=time.perf_counter()
compile_run=subprocess.run([os.environ.get('COMSPEC','cmd.exe'),'/d','/c',str(bat)],cwd=out,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
(out/'compile.log').write_text(compile_run.stdout,encoding='utf-8');compiled=time.perf_counter()
if compile_run.returncode:
    (out/'receipt.json').write_text(json.dumps({'status':'failed','phase':'compile','exit':compile_run.returncode,'sourceInputs':[{'path':str(f.relative_to(project)),'sha256':v} for f,v in source_hashes.items()]},indent=2),encoding='utf-8')
    print(compile_run.stdout);raise SystemExit(compile_run.returncode)
run=subprocess.run([str(exe),'cases.json','result.json'],cwd=out,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
(out/'run.log').write_text(run.stdout,encoding='utf-8');finished=time.perf_counter()
ok=(run.returncode==0) if expected_failure is None else (run.returncode!=0 and expected_failure in run.stdout)
source_identity_stable=all(hashlib.sha256(f.read_bytes()).hexdigest()==value for f,value in source_hashes.items())
ok=ok and source_identity_stable
receipt={'status':'passed' if ok else 'failed','componentScope':args.component_scope,'staticWakeStatus':('EXECUTED' if run.returncode==0 else 'NOT_REACHED') if args.component_scope=='all' else 'NOT_RUN','mutation':args.mutation,'compileExit':compile_run.returncode,'runExit':run.returncode,'expectedFaultCaught':expected_failure is not None and ok,'expectedFailure':expected_failure,'output':run.stdout.strip(),'sourceIdentityStable':source_identity_stable,'realSleepTriggered':False,'realEtwStarted':False,'realSleepStudyQuery':False,'productionHeaderSha256':source_hashes[header],'compiledHeaderSha256':hashlib.sha256((out/'sleep_residency_evidence.h').read_bytes()).hexdigest(),'productionHeaderNormalizedSha256':hashlib.sha256(original.encode()).hexdigest(),'productionHookSha256':hashlib.sha256(hook.encode()).hexdigest()}
receipt['sleepExportDiagnostics']={'status':'EXECUTED' if args.mutation=='none' and run.returncode==0 else 'NOT_RUN_MUTATION_SCOPE','inputs':[{'path':str(f.relative_to(project)).replace('\\','/'),'sha256':source_hashes[f]} for f in export_inputs]}
if args.mutation=='none' and run.returncode==0:
    native_result=json.loads((out/'result.json').read_text('utf-8-sig'))
    receipt['sleepExportDiagnostics']['checks']=native_result['sleepExportDiagnosticChecks']
    assert receipt['sleepExportDiagnostics']['checks']>0,'EXPORT_COMPANION_NOT_EXECUTED'
receipt['sourceInputs']=[{'path':str(f.relative_to(project)).replace('\\','/'),'sha256':value} for f,value in source_hashes.items()]
receipt['compiledInputs']=[{'path':f.name,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in [out/'production_capture_hook.inc',out/'sleep_residency_evidence.h',*[out/f.name for f in copy_paths],*([out/'static_wake_parser_binding.inc'] if args.component_scope=='all' else [])]]
dependencies=json.loads((out/'dependencies.json').read_text('utf-8-sig'))['Data']['Includes']
receipt['discoveredDependencies']=[{'path':str(f),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in map(Path,dependencies)]
tools=[Path(sys.base_prefix)/'python.exe',Path(os.environ.get('COMSPEC','C:/Windows/System32/cmd.exe')),Path(shutil.which('node'))]
for name in ['compiler-path.txt','linker-path.txt']:tools.append(Path((out/name).read_text('utf-8-sig').splitlines()[0]))
compiler_dir=tools[-2].parent
for name in ['c1xx.dll','c2.dll','mspdbcore.dll','msvcp140.dll','vcruntime140.dll']:
    if (compiler_dir/name).exists():tools.append(compiler_dir/name)
receipt['toolInputs']=[{'path':str(f),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in tools]
receipt['timingsMs']={'prepare':round((prepared-started)*1000,2),'compile':round((compiled-prepared)*1000,2),'execute':round((finished-compiled)*1000,2),'total':round((finished-started)*1000,2)}
receipt['scope']='Actual completed507 accounting and conservative production entries; staged engine/ETW regressions historical-only; static costs/scheduling per declared scope; all Windows boundaries mocked; export-only SleepStudy adapter uses inert commands and owned local files; no physical CPU/sleep acceptance'
(out/'receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in receipt.items() if k not in ['sourceInputs','compiledInputs','discoveredDependencies','toolInputs']},ensure_ascii=False,indent=2))
raise SystemExit(0 if ok else 1)
