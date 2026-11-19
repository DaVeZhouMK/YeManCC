
"""Exact optional scheduler component of the original residency producer."""
from pathlib import Path
import hashlib,json,os,re,subprocess,time
CASES=["schedule-pool-exit-diag-active","schedule-pool-exit-diag-abandoned","schedule-pool-exit-diag-refusal-retry","schedule-pool-exit-crt-all-worker-handles","schedule-pool-exit-crt-first-direct","schedule-pool-exit-crt-empty-refusal-stop","schedule-pool-exit-crt-never-started-stop","schedule-pool-exit-crt-partial-wake","schedule-pool-exit-crt-partial-stop","schedule-pool-exit-crt-repeat-wake","schedule-pool-exit-crt-off-reenable","schedule-pool-exit-crt-tls-isolation","schedule-pool-exit-trace-format","schedule-pool-exit-trace-contention","schedule-pool-exit-timeout-retains","schedule-pool-exit-partial-actual-join","schedule-pool-exit-finite-complete","schedule-pool-exit-no-trace","schedule-pool-exit-infinite","schedule-pool-exit-idle-trace","schedule-pool-exit-empty-join",'schedule-pool-cold-off-reenable','schedule-pool-normal', 'schedule-pool-reserve-failure', 'schedule-pool-first-thread-failure', 'schedule-pool-partial-thread-failure', 'schedule-pool-partial-stop', 'schedule-pool-one-worker', 'schedule-pool-sixteen-workers', 'schedule-pool-worker-exception', 'schedule-pool-stop-drops-pending','schedule-admission-off-reenable','schedule-original', 'schedule-admission-normal', 'schedule-admission-queue-allocation', 'schedule-admission-queue-allocation-log-failure', 'schedule-admission-unavailable', 'schedule-admission-worker-log-failure', 'schedule-admission-worker-scan-failure', 'schedule-admission-stale-before-worker']
def prepare(project,out,case):
    if case not in CASES:raise ValueError('unknown exact schedule scenario')
    out.mkdir(parents=True,exist_ok=True)
    inputs=['native/main.cpp','native/sleep_static_wake_schedule.h','native/sleep_static_wake_schedule_native.inc','tools/fixtures/sleep_residency/static_wake_schedule_harness.inc','tools/fixtures/sleep_residency/static_wake_schedule_admission.py','tools/sleep_residency_evidence_selftest.py','deps/json/json.hpp']
    power_headers=['native/sleep_power_state.h','native/sleep_power_observation.h','native/sleep_power_revocation.h']
    inputs.extend(power_headers)
    def pin(p):
        b=p.read_bytes();return {'path':str(p),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
    def save(name,text):
        with (out/name).open('x',encoding='utf-8',newline='') as f:f.write(text)
    source=(project/'native/main.cpp').read_text(encoding='utf-8-sig')
    mask=re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27',lambda m:re.sub(r'[^\n]',' ',m.group()),source)
    match=list(re.finditer(r'\bstatic\s+bool\s+poolSubmit\s*\(std::function<void\(\)>\s+job\)\s*\{',mask))
    if len(match)!=1:raise RuntimeError('exact original poolSubmit body missing')
    a=match[0].start();opening=mask.index('{',a);i=opening+1;depth=1
    while depth and i<len(mask):depth+=(mask[i]=='{')-(mask[i]=='}');i+=1
    if depth:raise RuntimeError('incomplete original poolSubmit')
    pool=source[a:i]
    save('production_static_schedule_pool.inc',pool+'\n')

    cold=case.startswith('schedule-pool-')
    actual_exit=case.startswith('schedule-pool-exit-')
    actual_crt=case.startswith('schedule-pool-exit-crt-')
    actual_diag=case.startswith('schedule-pool-exit-diag-')
    if cold:
        inputs.append('tools/fixtures/sleep_residency/static_wake_pool_harness.inc')
        for name,output in [('poolStartLocked','production_static_pool_start.inc'),('poolStop','production_static_pool_stop.inc')]:
            matches=list(re.finditer(r'\bstatic\s+void\s+'+name+r'\s*\([^;{}]*\)\s*\{',mask))
            if len(matches)!=1:raise RuntimeError('exact cold pool function '+name)
            begin=matches[0].start();opening=mask.index('{',begin);end=opening+1;depth=1
            while depth and end<len(mask):depth+=(mask[end]=='{')-(mask[end]=='}');end+=1
            if depth:raise RuntimeError('incomplete cold pool function '+name)
            save(output,source[begin:end]+'\n')
        with (out/'static_wake_pool_harness.inc').open('xb') as f:f.write((project/'tools/fixtures/sleep_residency/static_wake_pool_harness.inc').read_bytes())

    if actual_exit:
        inputs.append('tools/fixtures/sleep_residency/static_wake_pool_exit_harness.inc')
        def function_text(name,optional=False):
            matches=list(re.finditer(r'\bstatic\s+[^;{}]*?\b'+name+r'\s*\([^;{}]*\)\s*(?:noexcept\s*)?\{',mask))
            if not matches and optional:return ''
            if len(matches)!=1:raise RuntimeError('exact exit function '+name)
            begin=matches[0].start();opening=mask.index('{',begin);end=opening+1;depth=1
            while depth and end<len(mask):depth+=(mask[end]=='{')-(mask[end]=='}');end+=1
            if depth:raise RuntimeError('incomplete exit function '+name)
            return source[begin:end]+'\n'
        for name,output,optional in [('joinThreadBoundedForExit','production_static_exit_join.inc',False),('traceLog','production_static_trace_log.inc',False),('traceLogLockedV','production_static_trace_locked.inc',True),('traceLogTry','production_static_trace_try.inc',True)]:save(output,function_text(name,optional))
        states=list(re.finditer(r'static std::atomic<bool>\s+g_traceOn[^\n]*\nstatic std::mutex\s+g_traceMx[^\n]*\nstatic FILE\*\s+g_traceFile[^\n]*\nstatic ULONGLONG\s+g_traceT0[^\n]*',source))
        if len(states)!=1:raise RuntimeError('exact original trace state declarations')
        save('production_static_trace_state.inc',states[0].group()+'\n')
        with (out/'static_wake_pool_exit_harness.inc').open('xb') as f:f.write((project/'tools/fixtures/sleep_residency/static_wake_pool_exit_harness.inc').read_bytes())

    if actual_diag:
        for relative in ['native/input_host_diagnostic_dispatch.h','native/input_host_diagnostic_runtime.h']:
            inputs.append(relative)
            with (out/Path(relative).name).open('xb') as f:f.write((project/relative).read_bytes())
        save('production_static_pool_try.inc',function_text('poolTrySubmitExisting'))

    if actual_crt:
        inputs.append('tools/fixtures/sleep_residency/static_wake_pool_crt_harness.inc')
        save('static_crt_prelude.h','#ifndef YMCC_STATIC_CRT_PRELUDE\n#define YMCC_STATIC_CRT_PRELUDE 1\n#ifndef NOMINMAX\n#define NOMINMAX\n#endif\n#ifndef WIN32_LEAN_AND_MEAN\n#define WIN32_LEAN_AND_MEAN\n#endif\n#include <windows.h>\n#include \"static_wake_pool_crt_harness.inc\"\n#endif\n')
        with (out/'static_wake_pool_crt_harness.inc').open('xb') as f:f.write((project/'tools/fixtures/sleep_residency/static_wake_pool_crt_harness.inc').read_bytes())

    for relative in ['native/sleep_static_wake_schedule.h','native/sleep_static_wake_schedule_native.inc','tools/fixtures/sleep_residency/static_wake_schedule_harness.inc','deps/json/json.hpp',*power_headers]:
        with (out/Path(relative).name).open('xb') as f:f.write((project/relative).read_bytes())
    code=r"""
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define YMCC_STATIC_WAKE_ACTUAL_POOL 1
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
#include <new>
#include <stdexcept>
#include <vector>
#include "json.hpp"
static bool scheduleFailNextAllocation=false;
static unsigned scheduleAllocationHits=0;

#include <chrono>
#include <thread>
static thread_local int schedulePoolFaultAfter=-1;
static std::atomic<unsigned> schedulePoolFaultHits{0},fixturePoolThreadEntries{0};
static std::atomic<size_t> schedulePoolLastFaultBytes{0};
namespace ymcc::threadRole {[[maybe_unused]] static void describe(const wchar_t*) noexcept {fixturePoolThreadEntries.fetch_add(1);}}

void* operator new(std::size_t n){if(schedulePoolFaultAfter>=0){if(schedulePoolFaultAfter==0){schedulePoolFaultAfter=-1;++schedulePoolFaultHits;schedulePoolLastFaultBytes=n;throw std::bad_alloc();}--schedulePoolFaultAfter;}if(scheduleFailNextAllocation){scheduleFailNextAllocation=false;++scheduleAllocationHits;throw std::bad_alloc();}if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
#include "static_wake_schedule_harness.inc"
int main(int argc,char** argv){
 if(argc!=2)return 2;
 return static_wake_schedule_runtime::admissionCase(argv[1]);
}
"""
    if cold:code='#define YMCC_STATIC_WAKE_REAL_POOL 1\n'+code
    if actual_exit:code='#define YMCC_STATIC_WAKE_REAL_EXIT_JOIN 1\n'+code.replace('#include <cstdlib>','#include <cstdlib>\n#include <cstdio>\n#include <stdarg.h>\n#include <memory>')
    if actual_exit and function_text('traceLogTry',True):code='#define YMCC_STATIC_WAKE_HAS_TRACE_TRY 1\n'+code
    if actual_crt:code='#define YMCC_STATIC_WAKE_REAL_CRT 1\n'+code.replace('#include <windows.h>','#include \"static_crt_prelude.h\"')
    if actual_diag:code='#define YMCC_STATIC_WAKE_REAL_DIAGNOSTIC 1\n'+code.replace('#include "static_wake_schedule_harness.inc"','#include "input_host_diagnostic_dispatch.h"\n#include "static_wake_schedule_harness.inc"')
    save('static_schedule.cpp',code)
    binding={'status':'PREPARED_NOT_RUN','scenario':case,'completeOriginalPoolSubmit':pool,'poolSha256':hashlib.sha256(pool.encode()).hexdigest(),'poolStartPort':'Warmed existing pool only;counted inert poolStartLocked. No thread creation or cold pool startup proof. Actual mutex/deque/condition-variable/push run.','clockMessagesScanAndJournal':'Original existing static schedule harness ports;no real query,device or OS sleep','sourceInputs':[{'relative':p,**pin(project/p)} for p in inputs],'generated':[pin(out/n) for n in ['production_static_schedule_pool.inc','static_schedule.cpp','sleep_static_wake_schedule.h','sleep_static_wake_schedule_native.inc','static_wake_schedule_harness.inc','json.hpp']]}
    binding['generated'] += [pin(out/Path(p).name) for p in power_headers]
    if cold:
        binding['poolStartPort']='Actual original poolStartLocked/std::vector/std::thread workers. Diagnostic startup/close and thread role are inert counted terminals;join boundary uses exact owned native_handle wait and real join.'
        binding['generated'] += [pin(out/n) for n in ['production_static_pool_start.inc','production_static_pool_stop.inc','static_wake_pool_harness.inc']]
    if actual_exit:
        binding['poolStartPort']='Actual complete original pool start/submit/stop and joinThreadBoundedForExit plus trace state/logger. Fixture owns logger mutex contention,callback release and exact observer duplicate;no app/InputHost/system sleep.'
        binding['generated'] += [pin(out/n) for n in ['production_static_exit_join.inc','production_static_trace_log.inc','production_static_trace_locked.inc','production_static_trace_try.inc','production_static_trace_state.inc','static_wake_pool_exit_harness.inc']]
    if actual_crt:
        binding['crtFailurePort']='Installed exact std::thread::_Start includes actual original _beginthreadex call redirected once to fixture TLS shim before first thread header include. Refusal returns0/errno EAGAIN,doserrno8;success calls original CRT. Actual original MSVC _Throw_Cpp_error executes. Controlled refusal,not natural OS exhaustion.'
        binding['generated'] += [pin(out/'static_wake_pool_crt_harness.inc'),pin(out/'static_crt_prelude.h')]
    if actual_diag:
        binding['poolStartPort']='Actual complete original pool start/try-submit/submit/stop plus actual Dispatcher<Record> runtime startup/offer/close/write routing. Only explicit finite owned sink replaces file I/O;no real InputHost/app/device/system sleep.'
        binding['generated'] += [pin(out/n) for n in ['production_static_pool_try.inc','input_host_diagnostic_dispatch.h','input_host_diagnostic_runtime.h']]
    save('static-schedule-binding.json',json.dumps(binding,ensure_ascii=False,indent=2)+'\n')
    return binding
def run(project,out,case,prepare_only):
    started=time.perf_counter();binding=prepare(project,out,case)
    if prepare_only:
        print(json.dumps({'status':'PREPARED_NOT_RUN','source':str(out/'static_schedule.cpp'),'scenario':case}));return 0
    vcvars=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
    expression='import {calibrationToolchainPreamble} from "./tools/fixtures/hc_reviewed_calibration/toolchain.mjs";process.stdout.write(calibrationToolchainPreamble(process.argv[1]));'
    calibration=subprocess.run(['node','--input-type=module','-e',expression,str(vcvars)],cwd=project,capture_output=True,text=True,encoding='utf-8',check=True,timeout=15000/1000)
    batch=out/'compile.cmd'
    with batch.open('x',encoding='ascii',newline='') as f:f.write(calibration.stdout+'cl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /MT /sourceDependencies "dependencies.json" /I"." "static_schedule.cpp" /Fo"static_schedule.obj" /Fe"static_schedule.exe" /link /VERBOSE:LIB\nexit /b %errorlevel%\n')
    compile_run=subprocess.run([os.environ.get('COMSPEC','cmd.exe'),'/d','/c',str(batch)],cwd=out,capture_output=True,timeout=90)
    for name,b in [('compile.stdout.raw',compile_run.stdout),('compile.stderr.raw',compile_run.stderr)]:
        with (out/name).open('xb') as f:f.write(b)
    if compile_run.returncode:return compile_run.returncode
    execution=subprocess.run([str(out/'static_schedule.exe'),case],cwd=out,capture_output=True,timeout=15)
    for name,b in [('native.stdout.raw',execution.stdout),('native.stderr.raw',execution.stderr)]:
        with (out/name).open('xb') as f:f.write(b)
    stable=all(hashlib.sha256(Path(r['path']).read_bytes()).hexdigest()==r['sha256'] for r in binding['sourceInputs'])
    receipt={'status':'passed' if execution.returncode==0 and stable else 'failed','componentScope':'static-wake-schedule','scenario':case,'compileExit':compile_run.returncode,'runExit':execution.returncode,'sourceIdentityStable':stable,'sourceInputs':binding['sourceInputs'],'selection':{'exactOneScheduleCase':True,'otherResidencyGroupsExecuted':False},'realSleepTriggered':False,'hardwareCalls':0,'elapsedMs':(time.perf_counter()-started)*1000}
    with (out/'receipt.json').open('x',encoding='utf-8') as f:json.dump(receipt,f,ensure_ascii=False,indent=2)
    print(execution.stdout.decode('utf-8'));return 0 if receipt['status']=='passed' else 1
