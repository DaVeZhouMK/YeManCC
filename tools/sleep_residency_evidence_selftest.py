"""Compile original sleep accounting and source consumers with inert Windows ports.
No real ETW session, registry write, app, power or device action is performed.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys,time
sys.stdout.reconfigure(encoding='utf-8')
started=time.perf_counter()
p=argparse.ArgumentParser();p.add_argument('--output-dir',required=True)
p.add_argument('--component-scope',choices=['all','abnormal'],default='all')
p.add_argument('--mutation',choices=['none','hardware-zero-confirmed','missing-as-zero','callback-success-ignored','accepted-s4-shutdown','terminal-cleanup-removed','stale-evidence-permitted','previous-session-health-permitted','owned-intent-veto-lost','owned-budget-refilled'],default='none');args=p.parse_args()
project=Path(__file__).resolve().parents[1];out=Path(args.output_dir).resolve();out.mkdir(parents=True,exist_ok=True)
for path in [project,out]:
    if any(c in str(path) for c in '"%\r\n'):raise SystemExit('Unsupported command path')
cpp=project/'tools/sleep_residency_evidence_selftest.cpp'
copy_paths=[project/'native'/name for name in ['sleep_abnormal_resleep.h','sleep_abnormal_resleep_native.inc','sleep_abnormal_etw_native.inc','sleep_request_fallback.h','sleep_retry_budget.h']]
copy_paths += [project/'tools/fixtures/sleep_residency'/name for name in ['abnormal_native_harness.inc','cases.json']]
if args.component_scope=='all':
    copy_paths += [project/'native'/name for name in ['sleep_static_wake_policy.h','sleep_static_wake_native.inc']]
    copy_paths += [project/'tools/fixtures/sleep_residency/static_wake_harness.inc']
copy_paths += [cpp,project/'deps/json/json.hpp']
header=project/'native/sleep_residency_evidence.h'
source_paths=[header,project/'native/main.cpp',Path(__file__).resolve(),project/'tools/fixtures/hc_reviewed_calibration/toolchain.mjs',*copy_paths]
source_hashes={f:hashlib.sha256(f.read_bytes()).hexdigest() for f in source_paths}
for f in copy_paths:(out/f.name).write_bytes(f.read_bytes())
if args.component_scope!='all':
    text=(out/cpp.name).read_text('utf-8-sig').replace('#include "static_wake_harness.inc"','// Static-wake companion explicitly NOT_RUN in this partial execution.')
    text=text.replace('const auto staticWakeChecks=staticWakePolicyTests();','const unsigned staticWakeChecks=0;').replace('check(staticWakeChecks>0,"static wake harness executes nonzero checks");','')
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
    text=runtime.read_text('utf-8-sig');anchor='return sgAbnormalStartPhase(originalHumanIntent,requestFileTime,true);';assert text.count(anchor)==1
    runtime.write_text(text.replace(anchor,'g_sgAutomaticSleepBudget.confirmedUserWake();\n    '+anchor),encoding='utf-8');expected_failure='log1 second black-screen activity needs no second507 to reach original thirtysecond retry'
vcvars=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
if not vcvars.exists():raise SystemExit('MSVC unavailable; sources preserved')
exe=out/'selftest.exe';bat=out/'compile.bat'
toolchain_script='import {calibrationToolchainPreamble} from "./tools/fixtures/hc_reviewed_calibration/toolchain.mjs";process.stdout.write(calibrationToolchainPreamble(process.argv[1]));'
toolchain=subprocess.run(['node','--input-type=module','-e',toolchain_script,str(vcvars)],cwd=project,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace',check=True)
identity_commands=f'where cl.exe >"{out / "compiler-path.txt"}"\nwhere link.exe >"{out / "linker-path.txt"}"\n'
bat.write_text(toolchain.stdout+identity_commands+f'cl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /MT /sourceDependencies "{out / "dependencies.json"}" /I"{out}" "{out / cpp.name}" /Fo"{out / "selftest.obj"}" /Fe"{exe}"\n',encoding='ascii')
prepared=time.perf_counter()
compile_run=subprocess.run([os.environ.get('COMSPEC','cmd.exe'),'/d','/c',str(bat)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
(out/'compile.log').write_text(compile_run.stdout,encoding='utf-8');compiled=time.perf_counter()
if compile_run.returncode:
    (out/'receipt.json').write_text(json.dumps({'status':'failed','phase':'compile','exit':compile_run.returncode,'sourceInputs':[{'path':str(f.relative_to(project)),'sha256':v} for f,v in source_hashes.items()]},indent=2),encoding='utf-8')
    print(compile_run.stdout);raise SystemExit(compile_run.returncode)
run=subprocess.run([str(exe),str(out/'cases.json'),str(out/'result.json')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
(out/'run.log').write_text(run.stdout,encoding='utf-8');finished=time.perf_counter()
ok=(run.returncode==0) if expected_failure is None else (run.returncode!=0 and expected_failure in run.stdout)
source_identity_stable=all(hashlib.sha256(f.read_bytes()).hexdigest()==value for f,value in source_hashes.items())
ok=ok and source_identity_stable
receipt={'status':'passed' if ok else 'failed','componentScope':args.component_scope,'staticWakeStatus':'EXECUTED' if args.component_scope=='all' else 'NOT_RUN','mutation':args.mutation,'compileExit':compile_run.returncode,'runExit':run.returncode,'expectedFaultCaught':expected_failure is not None and ok,'expectedFailure':expected_failure,'output':run.stdout.strip(),'sourceIdentityStable':source_identity_stable,'realSleepTriggered':False,'realEtwStarted':False,'productionHeaderSha256':source_hashes[header],'compiledHeaderSha256':hashlib.sha256((out/'sleep_residency_evidence.h').read_bytes()).hexdigest(),'productionHeaderNormalizedSha256':hashlib.sha256(original.encode()).hexdigest(),'productionHookSha256':hashlib.sha256(hook.encode()).hexdigest()}
receipt['sourceInputs']=[{'path':str(f.relative_to(project)).replace('\\','/'),'sha256':value} for f,value in source_hashes.items()]
receipt['compiledInputs']=[{'path':f.name,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in [out/'production_capture_hook.inc',out/'sleep_residency_evidence.h',*[out/f.name for f in copy_paths]]]
dependencies=json.loads((out/'dependencies.json').read_text('utf-8-sig'))['Data']['Includes']
receipt['discoveredDependencies']=[{'path':str(f),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in map(Path,dependencies)]
tools=[Path(sys.executable),Path(os.environ.get('COMSPEC','C:/Windows/System32/cmd.exe')),Path(shutil.which('node'))]
for name in ['compiler-path.txt','linker-path.txt']:tools.append(Path((out/name).read_text('utf-8-sig').splitlines()[0]))
compiler_dir=tools[-2].parent
for name in ['c1xx.dll','c2.dll','mspdbcore.dll','msvcp140.dll','vcruntime140.dll']:
    if (compiler_dir/name).exists():tools.append(compiler_dir/name)
receipt['toolInputs']=[{'path':str(f),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in tools]
receipt['timingsMs']={'prepare':round((prepared-started)*1000,2),'compile':round((compiled-prepared)*1000,2),'execute':round((finished-compiled)*1000,2),'total':round((finished-started)*1000,2)}
receipt['scope']='Actual accounting and abnormal/ETW consumers plus declared companion policies; every event/configuration/timer/power boundary mocked; no live source/residency acceptance'
(out/'receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:v for k,v in receipt.items() if k not in ['sourceInputs','compiledInputs','discoveredDependencies','toolInputs']},ensure_ascii=False,indent=2))
raise SystemExit(0 if ok else 1)
