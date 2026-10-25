"""Compile the production507accounting parser and exact native capture snippet.
No Windows sleep, app, tracing, registry or device actions are performed.
"""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys
sys.stdout.reconfigure(encoding='utf-8')
p=argparse.ArgumentParser();p.add_argument('--output-dir',required=True);p.add_argument('--mutation',choices=['none','hardware-zero-confirmed','missing-as-zero','callback-success-ignored','accepted-s4-shutdown','terminal-cleanup-removed','stale-evidence-permitted'],default='none');args=p.parse_args()
project=Path(__file__).resolve().parents[1];out=Path(args.output_dir).resolve();out.mkdir(parents=True,exist_ok=True)
for path in [project,out]:
    if any(c in str(path) for c in '"%\r\n'):raise SystemExit('Unsupported command path')
source=(project/'native/main.cpp').read_text('utf-8-sig')
start='    if (leaving) {\n        // Preserve the OS\'s completed-session accounting in ordinary exports.'
end='    traceLog("kernel power event id=%d reason=%s targetState=%d", eventId,'
assert source.count(start)==1 and source.count(end)==1,'native507capture boundary changed'
hook=source[source.index(start):source.index(end)]
assert hook.count('sgRecordFact("s0-low-power-residency", residency);')==1
(out/'production_capture_hook.inc').write_text(hook,encoding='utf-8')
header=project/'native/sleep_residency_evidence.h';original=header.read_text('utf-8-sig');mutated=original
expected_failure=None
if args.mutation=='hardware-zero-confirmed':
    assert original.count('"hardware-zero-validity-unverified"')==1
    mutated=original.replace('"hardware-zero-validity-unverified"','"no-hardware-drips-confirmed"')
    expected_failure='hardware zero cannot prove physical failure: assessment'
elif args.mutation=='missing-as-zero':
    anchor='if (it == data.values.end()) continue; // Missing must never become zero.'
    assert original.count(anchor)==1
    mutated=original.replace(anchor,'if (it == data.values.end()) { values[field] = 0; continue; }')
    expected_failure='missing hardware on old event version: assessment'
(out/'sleep_residency_evidence.h').write_text(mutated,encoding='utf-8')
for name in ['sleep_abnormal_resleep.h','sleep_abnormal_resleep_native.inc','sleep_request_fallback.h']:
    (out/name).write_bytes((project/'native'/name).read_bytes())
runtime=out/'sleep_abnormal_resleep_native.inc';policy=out/'sleep_abnormal_resleep.h'
if args.mutation=='callback-success-ignored':
    text=runtime.read_text('utf-8');anchor='g_sgAbnormalSuccessOrigin.store(origin,std::memory_order_release);';assert text.count(anchor)==2
    runtime.write_text(text.replace(anchor,'(void)origin;',1),encoding='utf-8');expected_failure='callback-side Windows success immediately revokes dispatch before UI queue'
elif args.mutation=='accepted-s4-shutdown':
    text=policy.read_text('utf-8');anchor='if(stage==3&&(!hibernateRejected||hibernateAccepted))';assert text.count(anchor)==1
    policy.write_text(text.replace(anchor,'if(false)'),encoding='utf-8');expected_failure='accepted-but-unconfirmed S4 never authorizes shutdown'
elif args.mutation=='terminal-cleanup-removed':
    text=runtime.read_text('utf-8');anchor='sgAbnormalStop("normal-poweroff-attempt");';assert text.count(anchor)==1
    runtime.write_text(text.replace(anchor,'// intentionally removed terminal cleanup'),encoding='utf-8');expected_failure='shutdown success or failure is terminal with no source or timer'
elif args.mutation=='stale-evidence-permitted':
    text=policy.read_text('utf-8');anchor='const bool fresh=badEnd&&fileTime>=badEnd&&fileTime-badEnd<=freshMs*10000;';assert text.count(anchor)==1
    policy.write_text(text.replace(anchor,'const bool fresh=true;'),encoding='utf-8');expected_failure='stale positive evidence cannot authorize action'
vcvars=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
if not vcvars.exists():raise SystemExit('MSVC unavailable; sources preserved')
cpp=project/'tools/sleep_residency_evidence_selftest.cpp';exe=out/'selftest.exe'
bat=out/'compile.bat'
toolchain_script='import {calibrationToolchainPreamble} from "./tools/fixtures/hc_reviewed_calibration/toolchain.mjs";process.stdout.write(calibrationToolchainPreamble(process.argv[1]));'
toolchain=subprocess.run(['node','--input-type=module','-e',toolchain_script,str(vcvars)],cwd=project,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace',check=True)
bat.write_text(toolchain.stdout+f'cl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /MT /I"{out}" /I"{project / "deps/json"}" /I"{project / "native"}" /I"{project / "tools/fixtures/sleep_residency"}" "{cpp}" /Fo"{out / "selftest.obj"}" /Fe"{exe}"\n',encoding='ascii')
compile_run=subprocess.run([os.environ.get('COMSPEC','cmd.exe'),'/d','/c',str(bat)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
(out/'compile.log').write_text(compile_run.stdout,encoding='utf-8')
if compile_run.returncode:
    print(compile_run.stdout);raise SystemExit(compile_run.returncode)
run=subprocess.run([str(exe),str(project/'tools/fixtures/sleep_residency/cases.json'),str(out/'result.json')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
(out/'run.log').write_text(run.stdout,encoding='utf-8')
ok=(run.returncode==0) if expected_failure is None else (run.returncode!=0 and expected_failure in run.stdout)
receipt={'status':'passed' if ok else 'failed','mutation':args.mutation,'compileExit':compile_run.returncode,'runExit':run.returncode,'expectedFaultCaught':expected_failure is not None and ok,'expectedFailure':expected_failure,'output':run.stdout.strip(),'productionHeaderSha256':hashlib.sha256(header.read_bytes()).hexdigest(),'compiledHeaderSha256':hashlib.sha256((out/'sleep_residency_evidence.h').read_bytes()).hexdigest(),'productionHeaderNormalizedSha256':hashlib.sha256(original.encode()).hexdigest(),'productionHookSha256':hashlib.sha256(hook.encode()).hexdigest(),'scope':'Actual accounting capture and abnormal runtime with event/timer/power APIs mocked; no live residency proof','realSleepTriggered':False}
receipt['sourceInputs']=[{'path':str(f.relative_to(project)).replace('\\','/'),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in [header,project/'native/main.cpp',project/'native/sleep_abnormal_resleep.h',project/'native/sleep_abnormal_resleep_native.inc',project/'native/sleep_request_fallback.h',project/'native/sleep_retry_budget.h',cpp,Path(__file__).resolve(),project/'tools/fixtures/sleep_residency/abnormal_native_harness.inc',project/'tools/fixtures/sleep_residency/cases.json',project/'tools/fixtures/hc_reviewed_calibration/toolchain.mjs']]
receipt['compiledInputs']=[{'path':f.name,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f in [out/'production_capture_hook.inc',out/'sleep_residency_evidence.h',policy,runtime,out/'sleep_request_fallback.h']]
(out/'receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(receipt,ensure_ascii=False,indent=2))
raise SystemExit(0 if ok else 1)
