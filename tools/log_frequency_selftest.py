"""Offline logging test: compile extracted real native writers, no product/device calls.

Uses the existing MSVC toolchain. All fixture logs and binaries remain under
--output-dir. No execution-policy changes, privilege elevation or recursive cleanup.
"""
from __future__ import annotations
import argparse, datetime, hashlib, json, os, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

def extract(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    # Mask strings and comments without moving offsets; brackets in a comment or
    # JSON string must not end the extracted production function.
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda m: " " * len(m.group(0)), source, flags=re.S)
    depth = 0
    for i in range(opening, len(masked)):
        if masked[i] == "{": depth += 1
        elif masked[i] == "}":
            depth -= 1
            if depth == 0: return source[start:i + 1]
    raise ValueError("Unclosed function: " + signature)


def run_http_failure_reason_case(args):
    """Original log-frequency consumer,only the HTTP failure metadata producer."""
    import time
    if any((args.native_sink_case,args.aya_keyboard_case,args.sensor_retirement_phase,args.fan_lifecycle_case,
            args.exit_event_case,args.exit_event_phase,args.adaptation_export_only,args.adaptation_selector_only,
            args.gpu_windows_result,args.sdk_first_response,args.review_existing_run,args.native_sink_before)):
        raise ValueError('http-failure-reason requires an exclusive directed selection')
    output=args.output_dir.resolve();output.mkdir(parents=True,exist_ok=True)
    run=output/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(exist_ok=False)
    def put(p,b):
        with p.open('xb') as f:f.write(b.encode('utf-8') if isinstance(b,str) else b)
    def ident(p):
        b=p.read_bytes();return dict(path=str(p.resolve()),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
    source=args.source.resolve() if args.source else ROOT/'native/main.cpp'
    files=[source,ROOT/'native/log_snapshot_state.h',ROOT/'deps/json/json.hpp',ROOT/'tools/fixtures/log_frequency_http_failure.cpp.in',pathlib.Path(__file__)]
    bound={p:p.read_bytes() for p in files};main=bound[source].decode('utf-8-sig')
    marker='appendNativeLifecycleLog("http-request-failed", {';start=main.index(marker);assert main.count(marker)==1
    end=main.index('});',start)+3;producer=main[start:end]
    template=bound[files[3]].decode('utf-8-sig');assert template.count('@@PRODUCER@@')==1
    cpp=run/'Actual.cpp';put(cpp,template.replace('@@PRODUCER@@',producer))
    recipe=json.loads((ROOT/'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json').read_text(encoding='utf-8-sig'))['native']
    compiler=pathlib.Path(recipe['compiler']);exe=run/'Fixture.exe'
    args=[str(compiler),'/nologo','/std:c++20','/utf-8','/EHsc','/O2','/MD','/showIncludes','/sourceDependencies',str(run/'dependencies.json'),
        *['/I'+p for p in recipe['include']],'/I'+str(ROOT/'native'),'/I'+str(ROOT/'deps/json'),str(cpp),'/Fo'+str(run/'Fixture.obj'),'/Fe'+str(exe),'/link','/VERBOSE:LIB','/INCREMENTAL:NO',*['/LIBPATH:'+p for p in recipe['lib']]]
    started=time.monotonic();build=subprocess.run(args,cwd=run,capture_output=True,timeout=75)
    put(run/'compile.stdout.raw',build.stdout);put(run/'compile.stderr.raw',build.stderr)
    if build.returncode:raise RuntimeError('HTTP failure metadata compile failed: '+str(run))
    executed=subprocess.run([str(exe)],cwd=run,capture_output=True,timeout=15)
    put(run/'native.stdout.raw',executed.stdout);put(run/'native.stderr.raw',executed.stderr)
    result=json.loads(executed.stdout);result.update(nativeExitCode=executed.returncode,sourceStable=all(p.read_bytes()==b for p,b in bound.items()),sourceInputs=[ident(p) for p in files],generatedCpp=ident(cpp),executable=ident(exe),runDirectory=str(run),elapsedMs=int((time.monotonic()-started)*1000))
    put(output/'report.json',json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(dict(report=str(output/'report.json'),checks=result['checks'],nativeExitCode=executed.returncode)))
    return executed.returncode if result['sourceStable'] else 4


def session_marker_source(source, failure_header=None):
    """Original session marker IO/producer with an owned directory and optional log sink."""
    parts={s:extract(source,s) for s in ('static std::wstring sessionMarkerPath() {','static bool sessionMarkerWrite(bool clean) {','static bool sessionMarkerMarkClean() {')}
    writer=parts['static bool sessionMarkerWrite(bool clean) {'];assert writer.count('sessionMarkerWrite(')==1
    parts['static bool sessionMarkerWrite(bool clean) {']=writer.replace('sessionMarkerWrite(','fixtureOriginalSessionMarkerWrite(',1)
    template=(ROOT/'tools/fixtures/log_frequency_session_marker.cpp.in').read_text('utf-8-sig')
    for tag,body in [('PATH',parts['static std::wstring sessionMarkerPath() {']),('WRITE',parts['static bool sessionMarkerWrite(bool clean) {']),('PRODUCER',parts['static bool sessionMarkerMarkClean() {'])]:
        token='@@'+tag+'@@';assert template.count(token)==1;template=template.replace(token,body,1)
    header=(ROOT/'native/log_snapshot_state.h').read_text('utf-8-sig') if failure_header is None else failure_header
    has_counter='struct SessionMarkerDiagnosticExceptionEvidence {' in header
    signature='static json sessionMarkerDiagnosticExceptionAvailability(const json& writer) {'
    assert has_counter==(signature in source),'main/header session diagnostic counter mismatch'
    helper=extract(source,signature) if has_counter else ''
    marker='        try {availability["sessionMarkerDiagnosticExceptionEvidence"]='
    if has_counter:
        assert source.count(marker)==1;start=source.index(marker);end=source.index('\n',source.index('catch(...){/* Optional evidence may remain missing/null',start))
        assignment=source[start:end]
    else:assignment='static_cast<void>(availability);'
    for tag,body in [('FAILURE_EVIDENCE',header),('HAS_COUNTER','1' if has_counter else '0'),('DIAGNOSTIC_EXPORT',helper),('EXPORT_ASSIGNMENT',assignment)]:
        token='@@'+tag+'@@';assert template.count(token)==1;template=template.replace(token,body,1)
    return template


def run_session_marker_case(args):
    """Existing exit-event-owner selection, seven finite owned IO/diagnostic cases only."""
    import time
    if any((args.native_sink_case,args.native_sink_before,args.exit_event_phase,args.sensor_retirement_phase,args.review_existing_run)):
        raise ValueError('session-marker requires an exclusive exit-event-owner selection')
    source=args.source.resolve() if args.source else ROOT/'native/main.cpp';data=source.read_bytes();header=(ROOT/'native/log_snapshot_state.h').read_bytes();code=session_marker_source(data.decode('utf-8-sig').replace('\r\n','\n'),header.decode('utf-8-sig').replace('\r\n','\n'))
    output=args.output_dir.resolve();output.mkdir(parents=True,exist_ok=True);run=output/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'));run.mkdir()
    def put(p,b):
        with p.open('xb') as f:f.write(b.encode() if isinstance(b,str) else b)
    cpp=run/'session-marker.cpp';exe=run/'session-marker.exe';put(cpp,code);put(run/'bound-main.cpp',data);put(run/'bound-log_snapshot_state.h',header)
    recipe=json.loads((ROOT/'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json').read_text('utf-8-sig'))['native']
    command=[recipe['compiler'],'/nologo','/std:c++20','/utf-8','/EHsc','/O2','/MT','/W4','/WX','/showIncludes',*['/I'+p for p in recipe['include']],'/I'+str(ROOT/'deps/json'),str(cpp),'/Fo'+str(run/'session-marker.obj'),'/Fe'+str(exe),'/link','/VERBOSE:LIB',*['/LIBPATH:'+p for p in recipe['lib']],'kernel32.lib']
    started=time.monotonic();built=subprocess.run(command,cwd=run,capture_output=True,timeout=90);put(run/'compile.stdout.raw',built.stdout);put(run/'compile.stderr.raw',built.stderr)
    if built.returncode:return built.returncode
    records=[]
    for case in ('normal','marker-io-failure','prepare-failure','sink-failure','failed-marker-prepare','counter-contention','counter-limits'):
        r=subprocess.run([str(exe),case,str(run/case)],cwd=run,capture_output=True,timeout=15);put(run/(case+'.stdout.raw'),r.stdout);put(run/(case+'.stderr.raw'),r.stderr);value=json.loads(r.stdout);records.append(dict(case=case,exitCode=r.returncode,result=value))
        if r.returncode:break
    stable=source.read_bytes()==data and (ROOT/'native/log_snapshot_state.h').read_bytes()==header;value=dict(selectedCase='exit-event-owner',boundarySelection='session-marker',cases=records,sourceStable=stable,sourceSha256=hashlib.sha256(data).hexdigest(),generatedCppSha256=hashlib.sha256(code.encode()).hexdigest(),elapsedMs=int((time.monotonic()-started)*1000),defaultSuiteExecuted=False,productOrDeviceUsed=False,markerIoOnlyOwnedDirectory=True)
    put(run/'RESULT.json',json.dumps(value,indent=2)+'\n');put(output/'report.json',json.dumps(value,indent=2)+'\n');print(json.dumps(value));return 0 if stable and len(records)==7 and all(x['exitCode']==0 for x in records) else 3


def run_exit_event_owner_case(args):
    if args.exit_event_case=='session-marker':return run_session_marker_case(args)
    # Existing log-frequency consumer, explicit small selection; never enters default suite.
    import ast
    from pathlib import Path
    output=args.output_dir.resolve();output.mkdir(parents=True,exist_ok=True)
    run=output/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()))
    run.mkdir(exist_ok=False)
    def put(p,b):
        with p.open('xb') as f:f.write(b)
    def save(p,j):put(p,(json.dumps(j,ensure_ascii=False,indent=2)+'\n').encode())
    def ident(p):
        b=p.read_bytes();return {'path':str(p),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
    raw=(ROOT/'native/main.cpp').read_bytes();source=raw.decode('utf-8-sig').replace('\r\n','\n')
    before_path=ROOT.parent/'before/native/main.cpp'
    before=before_path.read_bytes().decode('utf-8-sig').replace('\r\n','\n')
    tree=ast.parse(Path(__file__).read_text(encoding='utf-8-sig'))
    main_fn=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='main')
    original_tests=next(n.value.value for n in main_fn.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='tests' for t in n.targets))
    shared_test_helpers=original_tests[:original_tests.index('int wmain(')]
    original_off=original_tests[original_tests.index('    reset(root, L"off");'):original_tests.index('    reset(root, L"on");')]
    signatures=[
        'static bool domainLogEnabled(const char* domain) {',
        'static void rotateOneLogFile(const std::wstring& path, unsigned long long maxBytes) {',
        'static void mirrorLifecycleToDomains(const char* event, const json& detail) {',
        'static bool nativeLifecycleEventIsBase(const char* event) {',
        'static void flushNativeLifecycleLogs(bool closeDetail) {',
        'static json nativeSteamGameLinkLogDetail(const json& detail) {',
        'static bool nativeLifecycleRecordIsStreaming(const char* event, const json& detail) {',
        'static void appendNativeLifecyclePrepared(const char* event,const json& detail,bool baseEvent,const std::string* encoded) {',
        'static void appendNativeLifecycleLog(const char* event, const json& observed) {']
    functions=[extract(source,s) for s in signatures]
    original_classifier=extract(before,signatures[3]).replace('nativeLifecycleEventIsBase(','nativeLifecycleEventIsBaseBefore(',1)
    original_wait_failure_support=''
    if args.exit_event_case=='wait-failure':
        assert extract(before,signatures[7])==functions[7]
        old_stream=extract(before,signatures[6]).replace('nativeLifecycleRecordIsStreaming(','nativeLifecycleRecordIsStreamingBefore(',1)
        old_append=extract(before,signatures[8])
        for old_name,new_name in [('appendNativeLifecycleLog(', 'appendNativeLifecycleLogBefore('),
                ('nativeLifecycleEventIsBase(event)','nativeLifecycleEventIsBaseBefore(event)'),
                ('nativeLifecycleRecordIsStreaming(event,*evidence)','nativeLifecycleRecordIsStreamingBefore(event,*evidence)')]:
            assert old_append.count(old_name)==1
            old_append=old_append.replace(old_name,new_name,1)
        original_wait_failure_support=old_stream+'\n'+old_append+'\n'

    prelude=r"""
#define NOMINMAX
#define YMCC_ROG_FSE_DIAGNOSTICS 0
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include "json.hpp"
#include "log_frequency_policy.h"
#include "log_snapshot_state.h"
#include "log_jsonl_boundary.h"
#include "sleep_fact_log_record.h"
using json=nlohmann::json;
namespace fspath=std::filesystem;
static std::wstring g_testRoot,SG_FACT_LOG,g_inputCapturePath;
static std::mutex g_nativeLifecycleLogMx;
static std::ofstream g_nativeLifecycleLog,g_nativeDetailLog,g_inputCaptureLog;
static uint32_t g_nativeLifecycleFrameWrites=0,g_nativeDetailWrites=0,g_inputCaptureSampleWrites=0;
static uint64_t g_inputCaptureBytes=0,g_inputCaptureSampleQueueDrops=0;
static ULONGLONG g_nativeDetailLastFlushTick=0;
static unsigned long long kLogMaxBytes=256;
static std::atomic<bool> g_domainLogEnabled[2]={false,false},g_sensorDetailLogging{false},g_fanLogEnabled{false};
static std::atomic<int> g_powerLifecycle{0};
static std::atomic<unsigned long long> g_powerGeneration{0};
static HWND g_hwnd=nullptr;
static bool g_domainLogGateKnown=false;
static std::vector<std::string> g_domainLogGateTimeline;
static std::deque<json> g_inputCaptureSampleQueue;
static std::deque<std::string> g_sgFacts;
static unsigned configWrites=0,domainRows=0;
static std::wstring app_data_dir(){return g_testRoot;}
static std::string nativeLifecycleTimestamp(){return "2000-01-01T00:00:00.000";}
static const char* powerLifecycleName(int){return "offline-fixture";}
static void appendDomainLog(const char*,const char*,const json&){++domainRows;}
static void flushDomainLogs(bool){}
static void flushTdpObservationLogs(bool=false){}
static void modelAdaptationFlush(){}
static void modelAdaptationObserveRecord(const char*,const json&){}
static void virtualAdaptationObserveRecord(const char*,const json&){}
namespace ymcc::inputHost::diagnostics {struct Scope{static bool active(){return false;}};}
static void inputHostDeferNativeDiagnostic(const char*,json,bool,bool=false){throw std::runtime_error("Unexpected deferred scope");}
static void flushNativeLifecycleLogs(bool closeDetail=false);
static void rotateOneLogFile(const std::wstring&,unsigned long long maxBytes=kLogMaxBytes);
"""
    selected=r"""
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;const fspath::path root(argv[1]);
    using ymcc::logging::NativeLane;using ymcc::logging::nativeLane;
    check("lane-policy-base-stays-event-on-and-off",nativeLane(true,false,false)==NativeLane::event&&nativeLane(true,false,true)==NativeLane::event);
    check("lane-policy-details-off-suppressed-on-detailed",nativeLane(false,false,false)==NativeLane::suppressed&&nativeLane(false,false,true)==NativeLane::detail);
    check("lane-policy-successful-frame-never-base",nativeLane(true,true,false)==NativeLane::suppressed&&nativeLane(true,true,true)==NativeLane::detail);
/*ORIGINAL_OFF_CASE*/
    const char* names[]={"exit-serial-owner-retained","exit-cleanup-waiting","exit-cleanup-finished"};
    for(const char* event:names){
        check((std::string(event)+"-old-exact-classifier-rejects").c_str(),!nativeLifecycleEventIsBaseBefore(event));
        check((std::string(event)+"-new-exact-classifier-base").c_str(),nativeLifecycleEventIsBase(event));
        check((std::string(event)+"-not-streaming").c_str(),!nativeLifecycleRecordIsStreaming(event,json::object()));
        appendNativeLifecycleLog(event,{{"exitSessionId",37},{"elapsedMs",1234},{"stage","logging-fixture"},{"workerReturned",false},{"uiThreadWaiting",false},{"threadDetached",false}});
        const auto rows=readRows(L"native-lifecycle.log");
        check((std::string(event)+"-off-real-writer-retains-fields").c_str(),!rows.empty()&&rows.back()["event"]==event&&rows.back()["exitSessionId"]==37&&rows.back()["elapsedMs"]==1234&&rows.back()["stage"]=="logging-fixture"&&rows.back()["workerReturned"]==false&&rows.back()["uiThreadWaiting"]==false&&rows.back()["threadDetached"]==false);
    }
    for(const char* event:{"exit-serial-owner-retained-typo","exit-cleanup-waiting-typo","exit-cleanup-finished-typo"})appendNativeLifecycleLog(event,json::object());
    check("exit-exact-names-only-no-prefix-promotion",!nativeLifecycleEventIsBase("exit-serial-owner-retained-typo")&&!nativeLifecycleEventIsBase("exit-cleanup-waiting-typo")&&!nativeLifecycleEventIsBase("exit-cleanup-finished-typo"));
    check("exit-off-exact-four-rows-with-existing-base-control",readRows(L"native-lifecycle.log").size()==4);
    check("exit-off-no-detail-or-domain-mirror",!g_domainLogEnabled[0]&&!g_domainLogEnabled[1]&&!fspath::exists(fspath::path(g_testRoot)/L"native-detail.log")&&domainRows==0);
    flushNativeLifecycleLogs(true);unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();
    std::cout<<json{{"selectedCase","exit-event-owner"},{"checks",checks},{"failures",failed},{"scope","EXTRACTED_REAL_CLASSIFIER_STREAMING_WRITER;ORIGINAL_OFF_AND_SUPPRESSED_CONTROLS;NO_PRODUCT_OR_DEVICE"}}.dump(2)<<std::endl;
    return failed?3:0;
}
""".replace('/*ORIGINAL_OFF_CASE*/',original_off)
    if args.exit_event_case=='wait-failure':
        selected=r"""
int wmain(int argc,wchar_t** argv){
    if(argc!=2&&argc!=3)return 2;const fspath::path root(argv[1]);
    using ymcc::logging::NativeLane;using ymcc::logging::nativeLane;
    reset(root,L"exit-wait-failure");
    const json wait={{"exitSessionId",37},{"win32",6},{"ownerRetained",true},{"retryWithMessageWait",true}};
    const json kernel={{"exitSessionId",37},{"win32",6}};
    const json marker={{"pid",GetCurrentProcessId()},{"reason","session-marker-write-failed"}};
    const bool recoveryOnly=argc==3&&std::wstring(argv[2])==L"--recovery-only";
    if(argc==3&&!recoveryOnly){
        if(std::wstring(argv[2])!=L"--old-source-control")return 2;
        const auto count=readRows(L"native-lifecycle.log").size();
        appendNativeLifecycleLogBefore("exit-cleanup-wait-failed",wait);
        check("old-source-first-exit-failure-survives-detail-off",readRows(L"native-lifecycle.log").size()==count+1);
        flushNativeLifecycleLogs(true);
        const bool pass=checks[0]["pass"].get<bool>();
        std::cout<<json{{"selectedCase","exit-event-owner"},{"boundarySelection","wait-failure-old-source"},{"checks",checks},{"failures",pass?0:1}}.dump(2)<<std::endl;return pass?0:3;
    }
    auto count=readRows(L"native-lifecycle.log").size();json changed=wait;
    if(recoveryOnly){appendNativeLifecycleLog("session-clean-exit",{{"fixture","existing-good-seed"}});count=readRows(L"native-lifecycle.log").size();}
    else {
    check("exit-wait-failure-exact-three-registered",nativeLifecycleEventIsBase("exit-cleanup-wait-failed")&&nativeLifecycleEventIsBase("exit-kernel-wait-failed")&&nativeLifecycleEventIsBase("session-clean-exit-failed")&&!nativeLifecycleEventIsBase("exit-cleanup-wait-failed-typo"));
    count=readRows(L"native-lifecycle.log").size();
    appendNativeLifecycleLog("exit-cleanup-wait-failed",wait);appendNativeLifecycleLog("exit-kernel-wait-failed",kernel);appendNativeLifecycleLog("session-clean-exit-failed",marker);
    check("exit-wait-failure-first-three-default-off-retained",readRows(L"native-lifecycle.log").size()==count+3);
    count=readRows(L"native-lifecycle.log").size();
    for(unsigned i=0;i<50000;++i){appendNativeLifecycleLog("exit-cleanup-wait-failed",wait);appendNativeLifecycleLog("exit-kernel-wait-failed",kernel);appendNativeLifecycleLog("session-clean-exit-failed",marker);}
    check("exit-wait-failure-50000-repeats-default-off-no-file-writes",readRows(L"native-lifecycle.log").size()==count&&!fspath::exists(fspath::path(g_testRoot)/L"native-detail.log"));
    changed=wait;changed["win32"]=5;appendNativeLifecycleLog("exit-cleanup-wait-failed",changed);
    check("exit-wait-failure-error-change-retained",readRows(L"native-lifecycle.log").size()==++count);
    changed["exitSessionId"]=38;appendNativeLifecycleLog("exit-cleanup-wait-failed",changed);
    check("exit-wait-failure-session-change-retained",readRows(L"native-lifecycle.log").size()==++count);
    ++g_powerGeneration;appendNativeLifecycleLog("exit-cleanup-wait-failed",changed);
    check("exit-wait-failure-power-change-retained",readRows(L"native-lifecycle.log").size()==++count);
    auto invalid=changed;invalid["win32"]=false;appendNativeLifecycleLog("exit-cleanup-wait-failed",invalid);appendNativeLifecycleLog("exit-cleanup-wait-failed",invalid);count+=2;
    check("exit-wait-failure-invalid-fields-stay-resident",readRows(L"native-lifecycle.log").size()==count);
    appendNativeLifecycleLog("exit-cleanup-wait-failed",changed);++count;
    check("exit-wait-failure-unknown-rearms-first-supported",readRows(L"native-lifecycle.log").size()==count);
    g_domainLogEnabled[1]=true;appendNativeLifecycleLog("exit-cleanup-wait-failed",changed);flushNativeLifecycleLogs(false);
    check("exit-wait-failure-repeat-on-is-detail",readRows(L"native-lifecycle.log").size()==count&&readRows(L"native-detail.log").size()==1);
    g_domainLogEnabled[1]=false;
    }
    changed["win32"]=123;g_nativeLifecycleLog.setstate(std::ios::badbit);appendNativeLifecycleLog("exit-cleanup-wait-failed",changed);
    check("exit-wait-failure-controlled-stream-fault-does-not-write-row",readRows(L"native-lifecycle.log").size()==count);
    const auto epoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();g_nativeLifecycleLog.clear();appendNativeLifecycleLog("session-clean-exit",{{"fixture","original-good-append"}});
    appendNativeLifecycleLog("exit-cleanup-wait-failed",changed);count+=2;
    check("exit-wait-failure-original-good-append-rearms",ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()>epoch&&readRows(L"native-lifecycle.log").size()==count);
    appendNativeLifecycleLog("exit-cleanup-wait-failed",changed);
    check("exit-wait-failure-after-rearm-repeat-is-quiet",readRows(L"native-lifecycle.log").size()==count);
    if(!recoveryOnly)check("exit-wait-failure-no-domain-or-new-owner-actions",domainRows==0&&!g_domainLogEnabled[0]&&!g_domainLogEnabled[1]);
    flushNativeLifecycleLogs(true);unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();
    std::cout<<json{{"selectedCase","exit-event-owner"},{"boundarySelection","wait-failure"},{"checks",checks},{"failures",failed},{"scope","actual logging classifier and writer;controlled error payload/stream state;no OS wait or product/device action"}}.dump(2)<<std::endl;
    return failed?3:0;
}
"""
    code=prelude+'\n'+'\n\n'.join(functions)+'\n'+original_classifier+'\n'+original_wait_failure_support+shared_test_helpers+'\n'+selected
    cpp=run/'log-frequency-harness.cpp';exe=run/'log-frequency-harness.exe'
    put(cpp,code.encode());put(run/'bound-main.cpp',raw)
    save(run/'extracted-source.json',{'functions':[{'signature':s,'sha256':hashlib.sha256(f.encode()).hexdigest()} for s,f in zip(signatures,functions)],'existingOffCase':original_off,'isolatedPorts':['adaptation observers not selected by these event names','inactive deferred diagnostic scope','domain sink counter','timestamp and power facts'],'source':ident(ROOT/'native/main.cpp'),'test':ident(Path(__file__))})
    node=Path('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe')
    toolchain=ROOT/'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'
    vcvars=Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
    env_script='import {calibrationToolchainPreamble} from "./tools/fixtures/hc_reviewed_calibration/toolchain.mjs";process.stdout.write(calibrationToolchainPreamble(process.argv[1]));'
    env=subprocess.run([str(node),'--input-type=module','-e',env_script,str(vcvars)],cwd=ROOT,capture_output=True,timeout=15)
    put(run/'environment.stdout.raw',env.stdout);put(run/'environment.stderr.raw',env.stderr)
    if env.returncode:return env.returncode
    command=env.stdout.decode()+f'where cl.exe > "{run / "compiler-paths.txt"}"\r\nwhere link.exe > "{run / "linker-paths.txt"}"\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /sourceDependencies "{run / "dependencies.json"}" "{cpp}" /I"{ROOT / "deps/json"}" /I"{ROOT / "native"}" /Fo"{run / "harness.obj"}" /Fe"{exe}"\r\nexit /b %errorlevel%\r\n'
    batch=run/'build.cmd';put(batch,command.encode())
    tools=[ident(Path(sys.executable)),ident(node),ident(vcvars),ident(toolchain),ident(Path(os.environ.get('SystemRoot','C:/Windows'))/'System32/cmd.exe')]
    started=datetime.datetime.now(datetime.timezone.utc)
    build=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,timeout=75)
    put(run/'compile.stdout.raw',build.stdout);put(run/'compile.stderr.raw',build.stderr)
    for name in ('compiler-paths.txt','linker-paths.txt'):
        for p in (run/name).read_text().splitlines():
            if Path(p).is_file():tools.append(ident(Path(p)))
    save(run/'tool-identity.json',{'tools':tools,'compilerExitCode':build.returncode})
    if build.returncode:print(build.stdout.decode(errors='replace')+build.stderr.decode(errors='replace'));return build.returncode
    old_control=None
    if args.exit_event_case=='wait-failure' and args.exit_event_phase is None:
        old_result=subprocess.run([str(exe),str(run/'fixtures-old-source'),'--old-source-control'],cwd=run,capture_output=True,timeout=15)
        put(run/'old-source.stdout.raw',old_result.stdout);put(run/'old-source.stderr.raw',old_result.stderr)
        old_report=json.loads(old_result.stdout)
        old_control={'exitCode':old_result.returncode,'report':old_report,'stdout':ident(run/'old-source.stdout.raw'),'stderr':ident(run/'old-source.stderr.raw')}
        if old_result.returncode!=3 or old_report['failures']!=1 or len(old_report['checks'])!=1 or old_report['checks'][0]['pass'] is not False:
            save(run/'old-source-control-failed.json',old_control);return 4
    result=subprocess.run([str(exe),str(run/'fixtures')]+(['--recovery-only'] if args.exit_event_phase=='recovery' else []),cwd=run,capture_output=True,timeout=15)
    put(run/'result.stdout.raw',result.stdout);put(run/'result.stderr.raw',result.stderr)
    report=json.loads(result.stdout);report['nativeExitCode']=result.returncode
    report['sourceStable']=(ROOT/'native/main.cpp').read_bytes()==raw
    report['testSource']=ident(Path(__file__));report['mainSource']=ident(ROOT/'native/main.cpp')
    report['executable']=ident(exe);report['startedAtUtc']=started.isoformat();report['endedAtUtc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
    report['checksActuallyExecuted']=len(report['checks']);report['defaultSuiteExecuted']=False
    if old_control is not None:report['oldSourceControl']=old_control
    save(run/'result.json',report);save(output/'report.json',report)
    print(json.dumps(report,ensure_ascii=False,indent=2))
    return result.returncode if report['sourceStable'] else 4


def run_journal_failure_case(args):
    """Existing adaptation logging case: Journal branch evidence only."""
    import time
    output=args.output_dir.resolve();output.mkdir(parents=True,exist_ok=True)
    run=output/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()));run.mkdir(exist_ok=False)
    def put(p,b):
        with p.open('xb') as f:f.write(b.encode('utf8') if isinstance(b,str) else b)
    def ident(p):
        b=p.read_bytes();return dict(path=str(p.resolve()),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
    files=[ROOT/'native'/n for n in ('model_adaptation_log.h','log_jsonl_boundary.h','sleep_fact_log_record.h')]+[ROOT/'deps/json/json.hpp',ROOT/'tools/fixtures/log_frequency_journal_failure.cpp',pathlib.Path(__file__)]
    if getattr(args,'journal_failure_phase',None) in ('stores','stores-control','prefix','prefix-control','prefix-faults','prefix-utf8'):
        originalStoreHeader=pathlib.Path(args.store_status_before).resolve() if getattr(args,'store_status_before',None) else ROOT/'native/model_adaptation_log.h'
        files.append(originalStoreHeader)
    bound={p:p.read_bytes() for p in files}
    for p,b in bound.items():
        leaf="bound-original-store.h" if getattr(args,"journal_failure_phase",None) in ("stores","stores-control","prefix","prefix-control","prefix-faults","prefix-utf8") and p==originalStoreHeader and p!=ROOT/"native/model_adaptation_log.h" else p.name
        put(run/leaf,b)
    metadata=getattr(args,'journal_failure_phase',None) in ('budget','budget-tail','meta','meta-fields','stores','stores-control','prefix','prefix-control','prefix-faults','prefix-utf8')
    if metadata:
        header=bound[ROOT/'native/model_adaptation_log.h'].decode('utf-8-sig').replace('\r\n','\n')
        control=extract(header,'class Journal {')
        optional='        try {result["failureEvidence"]=failureEvidence_.snapshot();}catch(...){}\n'
        assert control.count(optional)==1
        budget_optional='        try {result["budgetEvidence"]=budgetEvidence();}catch(...){}\n'
        assert control.count(budget_optional)==1
        control=control.replace(budget_optional,'',1)
        control=control.replace(optional,'',1).replace('class Journal {','class JournalStatusControl {',1).replace('explicit Journal(','explicit JournalStatusControl(',1)
        text='namespace ymcc::adaptlog {\n'+control+';\n}\n'
        put(run/'journal-status-control.inc',text)
        if getattr(args,'journal_failure_phase',None) in ('stores','stores-control','prefix','prefix-control','prefix-faults','prefix-utf8'):
            signals=extract(header,'class Signals {').replace('class Signals {','class SignalsStatusControl {',1)
            facts=extract(header,'template<size_t Capacity> class BasicFacts {').replace('class BasicFacts {','class BasicFactsStatusControl {',1)
            for block in (signals,facts):assert block.count('        appendRejectedEvidence(result);return result;')==1
            signals=signals.replace('        appendRejectedEvidence(result);return result;','        return result;',1)
            facts=facts.replace('        appendRejectedEvidence(result);return result;','        return result;',1)
            controlText='namespace ymcc::adaptlog {\n'+signals+';\n'+facts+';\n}\n'
            put(run/'adaptation-status-controls.inc',controlText)
            originalHeader=bound[originalStoreHeader].decode('utf-8-sig').replace('\r\n','\n')
            originalSignals=extract(originalHeader,'class Signals {').replace('class Signals {','class SignalsOriginal {',1)
            originalFacts=extract(originalHeader,'template<size_t Capacity> class BasicFacts {').replace('class BasicFacts {','class BasicFactsOriginal {',1)
            originalText='namespace ymcc::adaptlog {\n'+originalSignals+';\n'+originalFacts+';\n}\n'
            put(run/'adaptation-original-stores.inc',originalText)
            put(run/'ORIGINAL-STORE-BINDING.json',json.dumps(dict(source=ident(originalStoreHeader),sha256=hashlib.sha256(originalText.encode()).hexdigest(),transformation='rename actual old Signals and BasicFacts classes only;normalized EOL')))
        put(run/'STATUS-CONTROL-BINDING.json',json.dumps(dict(source=ident(ROOT/'native/model_adaptation_log.h'),sha256=hashlib.sha256(text.encode()).hexdigest(),transformation='remove only optional status append;rename class and constructor;normalize source EOL')))
    recipe=json.loads((ROOT/'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json').read_text(encoding='utf-8-sig'))['native']
    exe=run/'Journal.exe';compiler=pathlib.Path(recipe['compiler']);cpp=run/'log_frequency_journal_failure.cpp'
    arguments=[str(compiler),'/nologo','/std:c++20','/utf-8','/EHsc','/O2','/MT','/W4','/WX','/showIncludes','/sourceDependencies',str(run/'dependencies.json'),
        *(['/DYMCC_JOURNAL_METADATA_FIXTURE'] if metadata else []),*['/I'+p for p in recipe['include']],'/I'+str(run),str(cpp),'/Fo'+str(run/'Journal.obj'),'/Fe'+str(exe),'/link','/VERBOSE:LIB','/INCREMENTAL:NO',*(['/MAP:'+str(run/'Journal.map')] if metadata else []),*['/LIBPATH:'+p for p in recipe['lib']],'kernel32.lib','user32.lib','advapi32.lib']
    started=time.monotonic();build=subprocess.run(arguments,cwd=run,capture_output=True,timeout=100)
    put(run/'compile.stdout.raw',build.stdout);put(run/'compile.stderr.raw',build.stderr)
    put(run/'COMPILE.json',json.dumps(dict(arguments=arguments,exitCode=build.returncode,elapsedMs=round((time.monotonic()-started)*1000,3))))
    if build.returncode:print(build.stdout.decode(errors='replace')+build.stderr.decode(errors='replace'));return build.returncode
    native=subprocess.run([str(exe),str(run/'fixtures')]+(['--'+getattr(args,'journal_failure_phase',None)] if getattr(args,'journal_failure_phase',None) else ['--control'] if args.journal_failure_control else []),cwd=run,capture_output=True,timeout=25)
    put(run/'native.stdout.raw',native.stdout);put(run/'native.stderr.raw',native.stderr);put(run/'NATIVE-EXIT.json',json.dumps(dict(exitCode=native.returncode,stdoutBytes=len(native.stdout),stderrBytes=len(native.stderr))));report=(json.loads(native.stdout) if native.stdout.strip() else {'checks':[],'failures':None,'nativeOutputState':'empty-output;outcome-unproven'})
    report.update(nativeExitCode=native.returncode,sourceStable=all(p.read_bytes()==b for p,b in bound.items()),boundInputs=[ident(p) for p in files],executable=ident(exe),runDirectory=str(run),defaultSuiteExecuted=False,productOrHardwareExecuted=False,wallMs=round((time.monotonic()-started)*1000,3))
    put(run/'result.json',json.dumps(report,ensure_ascii=False,indent=2)+'\n');put(output/'report.json',json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(dict(result=ident(run/'result.json'),failures=report['failures'],nativeExitCode=native.returncode)))
    return native.returncode if report['sourceStable'] else 4


def open_visibility_source(start_source):
    """Original low-frequency Start log initializer only;no Start/device execution."""
    marker='    appendNativeLifecycleLog("input-host-open-result", {'
    assert start_source.count(marker)==1
    producer=extract(start_source,marker)+');'
    template=(ROOT/'tools/fixtures/log_frequency_open_visibility.cpp.in').read_text('utf-8-sig')
    assert template.count('@@PRODUCER@@')==1
    return template.replace('@@PRODUCER@@',producer,1)


def run_open_visibility_case(args):
    """Existing adaptation-facts consumer,only original open-result log arguments."""
    import time
    output=args.output_dir.resolve();output.mkdir(parents=True,exist_ok=True)
    run=output/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'));run.mkdir()
    def put(p,b):
        with p.open('xb') as f:f.write(b.encode() if isinstance(b,str) else b)
    source=ROOT/'native/virtual_controller_start_runtime.h';bound=source.read_bytes();code=open_visibility_source(bound.decode('utf-8-sig').replace('\r\n','\n'))
    cpp=run/'open-visibility.cpp';exe=run/'open-visibility.exe';put(cpp,code);put(run/'bound-start.h',bound)
    recipe=json.loads((ROOT/'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json').read_text('utf-8-sig'))['native']
    command=[recipe['compiler'],'/nologo','/std:c++20','/utf-8','/EHsc','/O2','/MT','/W4','/WX','/showIncludes',*['/I'+p for p in recipe['include']],'/I'+str(ROOT/'deps/json'),str(cpp),'/Fo'+str(run/'open-visibility.obj'),'/Fe'+str(exe),'/link','/VERBOSE:LIB',*['/LIBPATH:'+p for p in recipe['lib']]]
    started=time.monotonic();built=subprocess.run(command,cwd=run,capture_output=True,timeout=90);put(run/'compile.stdout.raw',built.stdout);put(run/'compile.stderr.raw',built.stderr)
    if built.returncode:return built.returncode
    result=subprocess.run([str(exe),'all'],cwd=run,capture_output=True,timeout=15);put(run/'native.stdout.raw',result.stdout);put(run/'native.stderr.raw',result.stderr)
    stable=source.read_bytes()==bound;value=dict(selectedCase='adaptation-facts',boundarySelection='open-visibility',nativeExitCode=result.returncode,result=json.loads(result.stdout),sourceStable=stable,sourceSha256=hashlib.sha256(bound).hexdigest(),generatedCppSha256=hashlib.sha256(code.encode()).hexdigest(),elapsedMs=int((time.monotonic()-started)*1000),defaultSuiteExecuted=False,completeStartExecuted=False,productOrDeviceUsed=False)
    put(run/'RESULT.json',json.dumps(value,indent=2)+'\n');put(output/'report.json',json.dumps(value,indent=2)+'\n');print(json.dumps(value));return result.returncode if stable else 4


def run_adaptation_facts_case(args):
    """Original logging consumer, directed first-fact storage/observer boundary only."""
    if args.adaptation_record_boundary:return run_open_visibility_case(args)
    import time
    output=args.output_dir.resolve();output.mkdir(parents=True,exist_ok=True)
    run=output/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()))
    run.mkdir(exist_ok=False)
    started=time.monotonic();deadline=started+120
    def put(p,b):
        with p.open('xb') as f:f.write(b)
    def save(p,value):put(p,(json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode())
    def ident(p):
        b=p.read_bytes();return {'path':str(p),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
    files=[ROOT/'native'/n for n in ('model_adaptation_log.h','virtual_adaptation_log_native.inc','virtual_adaptation_evidence.h','log_jsonl_boundary.h','sleep_fact_log_record.h','log_snapshot_state.h','log_frequency_policy.h')]+[ROOT/'native/main.cpp',ROOT/'deps/json/json.hpp',pathlib.Path(__file__)]
    if args.adaptation_export_only:files += [ROOT/'native'/n for n in ('adaptation_process_export.h','log_ai_index.h','log_export_evidence.h','buffered_domain_log.h')]+[ROOT/'tools/fixtures/log_frequency_adaptation_export.cpp']
    if args.adaptation_selector_only:
        files += [ROOT/'tools/fixtures/log_frequency_frame_context.cpp']+[ROOT/'native'/n for n in (
            'log_ai_index.h','log_export_evidence.h','buffered_domain_log.h','gamepad_backend_cost_json.h',
            'gamepad_backend_cost.h','ui_gamepad_cycle_log.h','input_idle_scope.h')]
    bound={p:p.read_bytes() for p in files}
    for p,b in bound.items():put(run/p.name,b)
    observer=bound[ROOT/'native/virtual_adaptation_log_native.inc'].decode('utf-8-sig').replace('\r\n','\n')
    selected=['static json virtualAdaptationSanitized(', 'static ymcc::adaptlog::VirtualFacts& virtualAdaptationFacts()',
              'static ymcc::adaptlog::Journal& virtualAdaptationJournal()', 'static ymcc::virtualAdaptation::FrameSelector& virtualAdaptationSelector()',
              'static void virtualAdaptationObserveRecord(', 'static json virtualAdaptationCapacityWitness(',
              'static json virtualAdaptationContextEvidence(', 'static json virtualAdaptationStatus()']
    functions={s:extract(observer,s) for s in selected}
    main_source=bound[ROOT/'native/main.cpp'].decode('utf-8-sig').replace('\r\n','\n')
    reader_code=''
    if not args.adaptation_selector_only:
        reader_signatures=['static bool nativeLifecycleEventIsBase(const char* event) {',
            'static bool nativeLifecycleRecordIsStreaming(const char* event, const json& detail) {',
            'static void systemXInputReadbackTick() {']
        reader_functions=[extract(main_source,sig) for sig in reader_signatures]
        gate='if (wasFirst || maskChanged || domainLogEnabled("virtual")) {'
        branch='    if (std::strcmp(event, "system-xinput-readback") == 0)\n        return detail.value("reason", "") == "buttons-change";\n'
        assert reader_functions[2].count(gate)==1 and reader_functions[1].count(branch)==1
        old_reader=reader_functions[2].replace('systemXInputReadbackTick()', 'systemXInputReadbackTickUngated()',1).replace(gate,'if (true) {',1)
        failed_reader=reader_functions[2].replace('systemXInputReadbackTick()', 'systemXInputReadbackTickUnavailable()',1)
        old_classifier=reader_functions[1].replace('nativeLifecycleRecordIsStreaming(', 'nativeLifecycleRecordIsStreamingUngated(',1).replace(branch,'',1)
        reader_code='\n'.join(reader_functions[:2])+'\n'+old_classifier+'\n#define GetTickCount64 fixtureGetTickCount64\n#define LoadLibraryW fixtureLoadLibraryW\n#define GetProcAddress fixtureGetProcAddress\n'+'\n'.join([reader_functions[2],old_reader,failed_reader])+'\n#undef GetTickCount64\n#undef LoadLibraryW\n#undef GetProcAddress\n'
        save(run/'extracted-source.json',{'functions':[{'signature':sig,'sha256':hashlib.sha256(f.encode()).hexdigest()} for sig,f in zip(reader_signatures,reader_functions)],
            'ports':['clock','System32 module resolution and four XInput slot results','readback evidence sink','backend cost sink','focus snapshot','domain sink'],
            'negative':'remove only button prebuild gate and exact streaming predicate;full original functions otherwise retained'})
    prelude=r"""
#define NOMINMAX
#include <windows.h>
#include <array>
#include <atomic>
#include <barrier>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>
#include <vector>
#include "model_adaptation_log.h"
#include "virtual_adaptation_evidence.h"
#include "log_snapshot_state.h"
#include "log_frequency_policy.h"
#include <algorithm>
#include <cstring>
#include <xinput.h>
using json=nlohmann::json;
static thread_local bool tracking=false;
static thread_local unsigned long long allocationCount=0;
void* operator new(std::size_t n){if(tracking)++allocationCount;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
static std::atomic<unsigned long long> g_virtualAdaptationBuildFailures{0},g_virtualAdaptationStepBuildFailures{0};
static json modelAdaptationMachine(){return {{"class","fixture"}};}
static json checks=json::array();
static void check(const char* name,bool pass){checks.push_back({{"name",name},{"pass",pass}});}
struct EagerFactsFixture:ymcc::adaptlog::VirtualFacts{
    template<class Build>void offerLazy(json model,json identity,std::string category,Build&& build){
        offer({std::move(model),std::move(identity),std::forward<Build>(build)(),std::move(category)});
    }
};
static EagerFactsFixture& virtualAdaptationFactsEager(){static EagerFactsFixture value;return value;}
"""
    reader_prelude=r"""
#define YMCC_ROG_FSE_DIAGNOSTICS 0
static std::atomic<unsigned long long> g_powerGeneration{0},g_systemXInputHeartbeatUntilTick{0};
static ULONGLONG fixtureTick=1000;
static unsigned fixtureMask=1,fixtureButtons=0,fixtureReads=0,fixturePhysical=0,fixtureEvidence=0,fixtureFinishes=0,fixtureFocus=0;
static bool fixtureDetail=false,fixtureCost=false,fixtureUnavailable=false,fixtureUseOldClassifier=false,fixtureEvidenceExact=true;
static json fixtureLow=json::array(),fixtureHigh=json::array(),fixtureBeats=json::array();
static struct{bool virtualTargetEnabled=true;}g_realStickTest;
static bool g_inputHostPrepared=true;
static struct{int system32=0;}g_gamepadBackendCost;
static ULONGLONG fixtureGetTickCount64(){return fixtureTick;}
static bool domainLogEnabled(const char*){return fixtureDetail;}
static bool tickCostEnabled(){return fixtureCost;}
static bool focusMainWindowIsForeground(){++fixtureFocus;return false;}
static void physicalSourceReaderTick(){++fixturePhysical;}
static unsigned long WINAPI fixtureGetState(unsigned long slot,void* data){
    ++fixtureReads;auto& state=*static_cast<XINPUT_STATE*>(data);
    if(!(fixtureMask&(1u<<slot)))return ERROR_DEVICE_NOT_CONNECTED;
    state.dwPacketNumber=static_cast<DWORD>(fixtureTick+slot);state.Gamepad.wButtons=static_cast<WORD>(fixtureButtons);return ERROR_SUCCESS;
}
static HMODULE fixtureLoadLibraryW(LPCWSTR){return fixtureUnavailable?nullptr:reinterpret_cast<HMODULE>(1);}
static FARPROC fixtureGetProcAddress(HMODULE,LPCSTR){return reinterpret_cast<FARPROC>(fixtureGetState);}
static void inputSourceReadbackEvidenceTick(ULONGLONG begin,ULONGLONG end,const std::array<DWORD,4>& results,const std::array<XINPUT_STATE,4>& states,FARPROC api){
    ++fixtureEvidence;fixtureEvidenceExact&=begin==fixtureTick&&end==fixtureTick&&api==reinterpret_cast<FARPROC>(fixtureGetState);
    for(unsigned i=0;i<4;++i)fixtureEvidenceExact&=results[i]==((fixtureMask&(1u<<i))?ERROR_SUCCESS:ERROR_DEVICE_NOT_CONNECTED)&&
        (!(fixtureMask&(1u<<i))||(states[i].Gamepad.wButtons==fixtureButtons&&states[i].dwPacketNumber==fixtureTick+i));
}
namespace ymcc::input::backendCost{template<class=void>struct Span{
    explicit Span(bool){}template<class Enable>void finish(int&,std::uint32_t,bool,Enable&&){++fixtureFinishes;}
};}
static bool nativeLifecycleEventIsBase(const char*);
static bool nativeLifecycleRecordIsStreaming(const char*,const json&);
static bool nativeLifecycleRecordIsStreamingUngated(const char*,const json&);
static void appendNativeLifecycleLog(const char* event,const json& value){
    const auto streaming=fixtureUseOldClassifier?nativeLifecycleRecordIsStreamingUngated(event,value):nativeLifecycleRecordIsStreaming(event,value);
    const auto lane=ymcc::logging::nativeLane(nativeLifecycleEventIsBase(event),streaming,fixtureDetail);
    if(lane==ymcc::logging::NativeLane::event)fixtureLow.push_back(value);
    if(lane==ymcc::logging::NativeLane::detail)fixtureHigh.push_back(value);
}
static void appendDomainLog(const char*,const char*,const json& value){fixtureBeats.push_back(value);}
"""
    reader_tests=r"""
    systemXInputReadbackTick();
    check("system-readback-detail-off-first-resident-and-four-existing-reads",fixtureLow.size()==1&&fixtureLow[0]["reason"]=="first"&&fixtureReads==4&&fixturePhysical==1&&fixtureEvidence==1&&fixtureFocus==1);
    fixtureTick=1500;systemXInputReadbackTick();
    check("system-readback-original-one-hertz-bound-preserved",fixtureReads==4&&fixtureEvidence==1&&fixturePhysical==1);
    fixtureTick=2000;fixtureButtons=XINPUT_GAMEPAD_A;systemXInputReadbackTick();
    check("system-readback-detail-off-button-change-no-json-sink-or-focus",fixtureLow.size()==1&&fixtureHigh.empty()&&fixtureFocus==1&&fixtureReads==8&&fixtureEvidence==2&&fixtureBeats.empty());
    fixtureTick=3000;fixtureDetail=true;systemXInputReadbackTick();
    check("system-readback-detail-off-still-updated-last-button-state",fixtureLow.size()==1&&fixtureHigh.empty()&&fixtureFocus==1&&fixtureReads==12);
    fixtureTick=4000;fixtureButtons=XINPUT_GAMEPAD_B;systemXInputReadbackTick();
    check("system-readback-detail-on-button-change-in-detail-only",fixtureLow.size()==1&&fixtureHigh.size()==1&&fixtureHigh[0]["reason"]=="buttons-change"&&fixtureHigh[0]["buttons0"]==XINPUT_GAMEPAD_B);
    fixtureTick=5000;fixtureDetail=false;fixtureMask=0;systemXInputReadbackTick();
    check("system-readback-detail-off-disconnection-resident-and-heartbeat-armed",fixtureLow.size()==2&&fixtureLow[1]["reason"]=="mask-change"&&fixtureLow[1]["connectedMask"]==0&&g_systemXInputHeartbeatUntilTick==65000);
    fixtureTick=6000;fixtureMask=1;systemXInputReadbackTick();
    check("system-readback-detail-off-reconnection-resident",fixtureLow.size()==3&&fixtureLow[2]["connectedMask"]==1&&g_systemXInputHeartbeatUntilTick==66000);
    fixtureTick=7000;fixtureDetail=true;fixtureCost=true;fixtureButtons=XINPUT_GAMEPAD_A;systemXInputReadbackTick();
    check("system-readback-original-armed-heartbeat-and-observer-payload-preserved",fixtureHigh.size()==2&&fixtureBeats.size()==1&&fixtureEvidence==7&&fixtureReads==28&&fixtureFinishes==28&&fixtureEvidenceExact);
    fixtureTick=68000;systemXInputReadbackTick();
    check("system-readback-heartbeat-expiry-still-clears-window",fixtureBeats.size()==1&&g_systemXInputHeartbeatUntilTick==0&&fixtureReads==32&&fixtureEvidence==8);
    bool resident=true;for(const char* reason:{"first","mask-change","module-or-entrypoint-unavailable","future-unknown"})resident&=
        ymcc::logging::nativeLane(nativeLifecycleEventIsBase("system-xinput-readback"),nativeLifecycleRecordIsStreaming("system-xinput-readback",{{"reason",reason}}),false)==ymcc::logging::NativeLane::event;
    check("system-readback-errors-and-unknown-retain-resident-lane",resident);
    fixtureUnavailable=true;fixtureDetail=false;fixtureCost=false;fixtureTick=1000;const auto beforeUnavailable=fixtureLow.size();const auto beforeReads=fixtureReads;
    systemXInputReadbackTickUnavailable();fixtureTick=2000;systemXInputReadbackTickUnavailable();
    check("system-readback-unavailable-emitted-once-off-without-api-calls",fixtureLow.size()==beforeUnavailable+1&&fixtureLow.back()["reason"]=="module-or-entrypoint-unavailable"&&fixtureReads==beforeReads);
    fixtureUnavailable=false;fixtureUseOldClassifier=true;fixtureTick=1000;fixtureButtons=0;systemXInputReadbackTickUngated();
    const auto oldLow=fixtureLow.size();const auto oldFocus=fixtureFocus;fixtureTick=2000;fixtureButtons=XINPUT_GAMEPAD_A;systemXInputReadbackTickUngated();
    check("system-readback-old-gate-and-classifier-negative-reproduces-low-leak-and-focus",fixtureLow.size()==oldLow+1&&fixtureLow.back()["reason"]=="buttons-change"&&fixtureFocus==oldFocus+1);
"""
    eager=functions[selected[4]].replace('virtualAdaptationObserveRecord(', 'virtualAdaptationObserveRecordEager(',1).replace('virtualAdaptationFacts()', 'virtualAdaptationFactsEager()')
    tests=r"""
int main(){
    using ymcc::adaptlog::BasicFacts;using ymcc::adaptlog::Fact;
    BasicFacts<3> facts;unsigned builds=0;
    for(unsigned i=0;i<50000;++i)facts.offerLazy({{"model",1}},{{"type",i%3}},"key",[&]{++builds;return json{{"first",i}};});
    check("adaptation-50000-three-types-only-three-sample-builds",builds==3&&facts.status()["knownFacts"]==3&&facts.status()["pendingFacts"]==3);
    facts.offerLazy({{"model",1}},{{"type",0}},"key",[&]{++builds;return json::object();});
    check("adaptation-full-journal-known-type-does-not-build-or-count-drop",builds==3&&facts.status()["boundedDrops"]==0);
    facts.offerLazy({{"model",1}},{{"type",3}},"key",[&]{++builds;return json::object();});
    auto status=facts.status();
    check("adaptation-capacity-drop-is-separate-from-busy-and-prebuild",builds==3&&status["capacityDrops"]==1&&status["busyDrops"]==0&&status["boundedDrops"]==1);
    size_t index=0;Fact first;
    bool taken=facts.take(index,first);facts.finish(index,false);Fact retried;size_t retryIndex=0;
    const bool retry=facts.take(retryIndex,retried);
    check("adaptation-retry-keeps-original-sample-and-pending",taken&&retry&&index==retryIndex&&first.sample==retried.sample&&first.sample["first"]==0&&facts.status()["pendingFacts"]==3);
    facts.finish(retryIndex,true);
    unsigned n=1;bool originals=true;Fact row;
    while(facts.take(index,row)){originals&=row.sample["first"]==n++;facts.finish(index,true);}
    check("adaptation-committed-facts-stay-known-with-zero-pending",originals&&n==3&&facts.status()["knownFacts"]==3&&facts.status()["pendingFacts"]==0&&!facts.pending());
    BasicFacts<8> contexts;
    for(unsigned i=0;i<3;++i)contexts.offerLazy({{"model",i==1?2:1}},{{"type",0}},i==2?"other":"key",[&]{return json{{"seen",true}};});
    check("adaptation-model-and-category-remain-exact-identity",contexts.status()["knownFacts"]==3);
    BasicFacts<2> throwing;bool caught=false;
    try{throwing.offerLazy({},{{"type",1}},"key",[]{throw std::runtime_error("fixture");return json::object();});}catch(...){caught=true;}
    throwing.offerLazy({},{{"type",1}},"key",[&]{return json{{"statusSnapshot",throwing.status()}};});
    check("adaptation-sample-build-outside-lock-and-exception-does-not-reserve-slot",caught&&throwing.status()["knownFacts"]==1&&throwing.status()["pendingFacts"]==1);
    std::barrier gate(8);BasicFacts<16> concurrent;std::vector<std::thread> threads;
    for(unsigned t=0;t<8;++t)threads.emplace_back([&,t]{gate.arrive_and_wait();for(unsigned n=0;n<2000;++n)concurrent.offerLazy({{"model",1}},{{"type",t}},"key",[t]{return json{{"thread",t}};});});
    for(auto& thread:threads)thread.join();
    auto concurrentStatus=concurrent.status();
    check("adaptation-concurrent-accounting-no-capacity-or-eviction",concurrentStatus["capacityDrops"]==0&&concurrentStatus["boundedDrops"]==concurrentStatus["busyDrops"]&&concurrentStatus["knownFacts"].get<unsigned>()<=8&&concurrentStatus["eviction"]==false);
    json detail={{"phase","first"},{"requestId",1},{"payload",{{"large",std::string(8192,'X')},{"nested",json::array({1,2,3})}}},{"startupNonce","transport-secret"}};
    for(unsigned i=0;i<3;++i){detail["phase"]=std::to_string(i);virtualAdaptationObserveRecord("input-host-open-result",detail);virtualAdaptationObserveRecordEager("input-host-open-result",detail);}
    allocationCount=0;tracking=true;
    for(unsigned i=0;i<5000;++i){detail["phase"]=std::to_string(i%3);virtualAdaptationObserveRecord("input-host-open-result",detail);}
    tracking=false;const auto lazyAllocations=allocationCount;allocationCount=0;tracking=true;
    for(unsigned i=0;i<5000;++i){detail["phase"]=std::to_string(i%3);virtualAdaptationObserveRecordEager("input-host-open-result",detail);}
    tracking=false;const auto eagerAllocations=allocationCount;
    check("adaptation-real-observer-duplicate-sample-lazy-control-reduces-allocations",lazyAllocations<eagerAllocations&&virtualAdaptationFacts().status()["knownFacts"]==3);
    bool same=true,secretsRemoved=true;Fact eagerRow,actualRow;size_t eagerIndex=0,actualIndex=0;
    for(unsigned i=0;i<3;++i){same&=virtualAdaptationFacts().take(actualIndex,actualRow)&&virtualAdaptationFactsEager().take(eagerIndex,eagerRow);
        same&=actualRow.model==eagerRow.model&&actualRow.identity==eagerRow.identity&&actualRow.sample==eagerRow.sample;
        secretsRemoved&=!actualRow.sample["producer"].contains("startupNonce");
        virtualAdaptationFacts().finish(actualIndex,true);virtualAdaptationFactsEager().finish(eagerIndex,true);}
    check("adaptation-real-observer-sample-and-identity-match-eager-control",same&&secretsRemoved);
    const auto failuresBefore=g_virtualAdaptationStepBuildFailures.load();
    virtualAdaptationObserveRecord("input-host-command-result",{{"command",17}});
    check("adaptation-real-observer-build-exception-is-explicit",g_virtualAdaptationStepBuildFailures.load()==failuresBefore+1&&virtualAdaptationStatus()["stepBuildFailures"]==failuresBefore+1&&virtualAdaptationStatus()["frameBuildFailures"]==0);
    json slot={{"slot",0},{"stage","target-mismatch"},{"stateAttempted",true},{"pathAttempted",true},{"targetInterfaceAttempted",true},{"targetInterfaceMatched",false},{"indexAttempted",false},{"confirmAttempted",false},{"stateResult",0},{"confirmResult",1167},{"mappedSlot",255}};
    json startup={{"phase","xinput-missing"},{"slotReadback",json::array({slot})}};
    const auto beforeStartup=virtualAdaptationFacts().status()["knownFacts"].get<unsigned>();
    for(unsigned i=0;i<50000;++i){startup["elapsedMs"]=i;startup["slotReadback"][0]["confirmAttempted"]=i%3==2;
        startup["slotReadback"][0]["indexAttempted"]=i%3!=0;virtualAdaptationObserveRecord("input-host-xbox-startup",startup);}
    check("adaptation-real-startup-50000-three-shapes-retained",virtualAdaptationFacts().status()["knownFacts"]==beforeStartup+3);


    const auto cleanupStart=virtualAdaptationFacts().status()["knownFacts"].get<unsigned>();
    json cleanup={{"reason","virtual-target-disabled"},{"closingRequestId",5},{"localCleanupResult","success"},
        {"cleanupEvidence",{{"checks",{{"hostProcessAbsent",true},{"configuredOff",true}}},{"blockers",json::array()},{"unknown",json::array()},
            {"observedTick",100},{"physicalReturnVerified",false}}}};
    virtualAdaptationObserveRecord("input-host-cleanup-evidence",cleanup);
    cleanup["closingRequestId"]=7;virtualAdaptationObserveRecord("input-host-cleanup-evidence",cleanup);
    for(unsigned i=0;i<50000;++i){cleanup["cleanupEvidence"]["observedTick"]=i;virtualAdaptationObserveRecord("input-host-cleanup-evidence",cleanup);}
    check("terminal-cleanup-distinct-closing-requests-not-collapsed-and-repeats-bounded",virtualAdaptationFacts().status()["knownFacts"]==cleanupStart+2);
    cleanup["closingRequestId"]=0;virtualAdaptationObserveRecord("input-host-cleanup-evidence",cleanup);
    cleanup["localCleanupResult"]="failure";cleanup["cleanupEvidence"]["checks"]["hostProcessAbsent"]=false;
    cleanup["cleanupEvidence"]["blockers"]=json::array({"hostProcessAbsent"});virtualAdaptationObserveRecord("input-host-cleanup-evidence",cleanup);
    cleanup["cleanupEvidence"].erase("checks");cleanup["cleanupEvidence"]["unknown"]=json::array({"pipe-snapshot"});virtualAdaptationObserveRecord("input-host-cleanup-evidence",cleanup);
    check("terminal-cleanup-success-failure-and-unknown-shapes-distinct",virtualAdaptationFacts().status()["knownFacts"]==cleanupStart+5);
    json visibility={{"reason","virtual-target-disabled"},{"readback",true},{"snapshotPhase","before-restore"},{"localReleaseAck",true},
        {"shutdownAck",true},{"hostAlive",true},{"physicalHiddenByHidHide",false},{"virtualEndpointAbsent",true}};
    virtualAdaptationObserveRecord("input-host-release-visibility",visibility);
    visibility["localReleaseAck"]=false;visibility["shutdownAck"]=false;visibility["hostAlive"]=false;
    virtualAdaptationObserveRecord("input-host-release-visibility",visibility);
    check("terminal-visibility-same-readback-different-ack-and-host-state-retained",virtualAdaptationFacts().status()["knownFacts"]==cleanupStart+7);
    bool scoped=false,unknownScope=false,originalPreserved=false;Fact terminal;size_t terminalIndex=0;
    const auto& thisWriter=ymcc::sleepfactlog::writerIdentity();
    while(virtualAdaptationFacts().take(terminalIndex,terminal)){
        if(terminal.sample.value("observedFrom","")=="input-host-cleanup-evidence"){
            const auto& request=terminal.identity["cleanupRequestContext"];
            if(request.is_object())scoped|=request["closingRequestId"]==5&&request["writerPid"]==thisWriter.pid&&terminal.sample["operationContextKnown"]==true;
            else unknownScope|=request.is_null()&&terminal.sample["operationContextKnown"]==false;
            originalPreserved|=terminal.sample["producer"].contains("closingRequestId")&&!terminal.sample["producer"].contains("writerProcessCreation100ns");
        }
        virtualAdaptationFacts().finish(terminalIndex,true);
    }
    check("terminal-request-scope-known-only-for-positive-producer-id-and-original-preserved",scoped&&unknownScope&&originalPreserved);
    // Existing frame-context/outcome budget case, parameterized for saturation.
    struct LinearSelectorReference {
        struct Entry {std::uint64_t epoch=0,power=0,source=0,topology=0;unsigned seen=0;};
        std::array<Entry,64> entries{};unsigned known=0;unsigned long long drops=0,unknown=0,selected=0;
        bool select(const ymcc::virtualAdaptation::FrameStamp& s){
            if(!s.epoch){++unknown;return false;}Entry* found=nullptr;
            for(unsigned i=0;i<known;++i){auto& e=entries[i];if(e.epoch==s.epoch&&e.power==s.power&&e.source==s.sourceGeneration&&e.topology==s.topology){found=&e;break;}}
            if(!found){if(known==entries.size()){++drops;return false;}found=&entries[known++];*found={s.epoch,s.power,s.sourceGeneration,s.topology,0};}
            const auto bit=1u<<((s.neutral?1:0)|(s.accepted?2:0)|(s.sourceAdmitted?4:0));
            if(found->seen&bit)return false;found->seen|=bit;++selected;return true;
        }
    };
    using ymcc::virtualAdaptation::FrameStamp;using ymcc::virtualAdaptation::FrameSelector;
    FrameSelector indexed;LinearSelectorReference linear;bool equal=true;FrameStamp frame;
    // Fill with deliberately colliding hashes, verifying exact tuple equality.
    const auto hash=[](const FrameStamp& s){std::uint64_t h=14695981039346656037ull;
        for(auto word:{s.epoch,s.power,s.sourceGeneration,s.topology}){h^=word;h*=1099511628211ull;}h^=h>>32;return h&127u;};
    std::vector<FrameStamp> collisionContexts;frame.epoch=1;frame.power=7;frame.topology=9;const auto targetBucket=hash(frame);
    for(unsigned source=0;collisionContexts.size()<64;++source){frame.sourceGeneration=source;if(hash(frame)==targetBucket)collisionContexts.push_back(frame);}
    for(auto stamp:collisionContexts){equal&=indexed.select(stamp)==linear.select(stamp);}
    auto overflow=frame;overflow.epoch=99;equal&=indexed.select(overflow)==linear.select(overflow);
    for(auto stamp:collisionContexts)for(unsigned flags=0;flags<8;++flags){stamp.neutral=flags&1;stamp.accepted=flags&2;stamp.sourceAdmitted=flags&4;equal&=indexed.select(stamp)==linear.select(stamp);}
    check("virtual-context-collisions-retain-all-64-and-eight-outcomes",equal&&indexed.selected()==512&&indexed.knownContexts()==64&&indexed.capacityDrops()==1);
    for(unsigned i=0;i<100000;++i){auto stamp=collisionContexts[i%64];stamp.epoch=i%5?1:2;stamp.power=i%7?7:8;stamp.topology=i%11?9:10;
        stamp.neutral=i&1;stamp.accepted=i&2;stamp.sourceAdmitted=i&4;if(i%13==0)stamp.epoch=0;
        equal&=indexed.select(stamp)==linear.select(stamp);}
    check("virtual-context-index-matches-linear-rejection-and-exact-tuple-control",equal&&indexed.selected()==linear.selected&&indexed.capacityDrops()==linear.drops&&indexed.unknownDrops()==linear.unknown&&indexed.knownContexts()==linear.known);
    FrameSelector full;for(unsigned i=1;i<=64;++i){FrameStamp stamp;stamp.epoch=i;full.select(stamp);}
    FrameStamp retained;retained.epoch=1;retained.accepted=true;
    check("virtual-context-full-budget-still-allows-new-known-context-outcome",full.select(retained)&&!full.select(retained)&&full.knownContexts()==64);
    FrameStamp newContext;newContext.epoch=65;const auto dropsBefore=full.capacityDrops();allocationCount=0;tracking=true;
    for(unsigned i=0;i<69510;++i){newContext.sourceGeneration=i;full.select(newContext);}
    tracking=false;
    check("virtual-context-69510-saturation-rejections-no-heap-no-eviction",allocationCount==0&&full.capacityDrops()==dropsBefore+69510&&full.knownContexts()==64&&full.selected()==65);
    const auto coverage=virtualAdaptationStatus();
    check("virtual-context-export-exposes-count-limit-and-rejection-scope",coverage["frameMaxContexts"]==64&&coverage["frameKnownContexts"]==virtualAdaptationSelector().knownContexts()&&coverage.contains("frameContextLimitReached")&&coverage["frameCapacityDropScope"].get<std::string>().find("not-unique-input-types")!=std::string::npos);
    unsigned long long indexedCycles=0,linearCycles=0;FrameSelector fast;LinearSelectorReference slow;
    for(unsigned i=1;i<=64;++i){FrameStamp stamp;stamp.epoch=i;fast.select(stamp);slow.select(stamp);}
    ULONG64 begin=0,end=0;const bool beginOk=QueryThreadCycleTime(GetCurrentThread(),&begin)!=0;
    for(unsigned i=0;i<200000;++i){FrameStamp stamp;stamp.epoch=65;stamp.sourceGeneration=i;fast.select(stamp);}
    const bool endOk=QueryThreadCycleTime(GetCurrentThread(),&end)!=0;if(beginOk&&endOk)indexedCycles=end-begin;
    const bool slowBegin=QueryThreadCycleTime(GetCurrentThread(),&begin)!=0;
    for(unsigned i=0;i<200000;++i){FrameStamp stamp;stamp.epoch=65;stamp.sourceGeneration=i;slow.select(stamp);}
    const bool slowEnd=QueryThreadCycleTime(GetCurrentThread(),&end)!=0;if(slowBegin&&slowEnd)linearCycles=end-begin;
    check("virtual-context-saturated-control-matches-rejected-attempt-total",fast.capacityDrops()==slow.drops&&slow.drops==200000);
    const json selectorControl={{"iterations",200000},{"indexedCycles",indexedCycles},{"linearCycles",linearCycles},{"cycleCountersAvailable",beginOk&&endOk&&slowBegin&&slowEnd},{"scope","same-context-budget selector-only;linear exact reference;not whole-app CPU"}};
    unsigned failed=0;for(const auto& item:checks)failed+=!item["pass"].get<bool>();
    std::cout<<json{{"selectedCase","adaptation-facts"},{"checks",checks},{"failures",failed},{"selectorControl",selectorControl},{"duplicateObserverAllocationControl",{{"iterations",5000},{"lazyAllocations",lazyAllocations},{"eagerAllocations",eagerAllocations},{"scope","same source observer; eager-sample-only mutation;not whole-app CPU"}}},{"concurrentStatus",concurrentStatus}}.dump(2)<<std::endl;
    return failed?3:0;
}
"""
    tests=tests.replace('    unsigned failed=0;for(const auto& item:checks)',reader_tests+'\n    unsigned failed=0;for(const auto& item:checks)',1)
    code=prelude+'\n'+'\n'.join(functions.values())+'\n'+eager+'\n'+reader_prelude+'\n'+reader_code+'\n'+tests
    if args.adaptation_export_only:
        export_start='        const auto virtualProcessExport=ymcc::adaptlog::exportRetainedFacts('
        export_end='            {"missingReason","current-process-evidence-export-failed"}});'
        assert main_source.count(export_start)==1 and main_source.count(export_end)==1
        block=main_source[main_source.index(export_start):main_source.index(export_end)+len(export_end)]
        template=bound[ROOT/'tools/fixtures/log_frequency_adaptation_export.cpp'].decode('utf-8-sig')
        assert template.count('// ACTUAL_EXPORT_BLOCK')==1
        code=template.replace('// ACTUAL_EXPORT_BLOCK',block,1)
        model_start='        const auto modelFactsProcessExport='
        assert main_source.count(model_start)==1 and template.count('// ACTUAL_MODEL_EXPORT_BLOCK')==1
        model_block=main_source[main_source.index(model_start):main_source.index(export_start)]
        code=code.replace('// ACTUAL_MODEL_EXPORT_BLOCK',model_block,1)
        save(run/'export-binding.json',{'block':block,'sha256':hashlib.sha256(block.encode()).hexdigest(),'productionHeader':ident(ROOT/'native/adaptation_process_export.h'),'scope':'actual logs.exportAll call/add-file/availability block;not full UI/Steam export handler'})
    if args.adaptation_selector_only:
        header=bound[ROOT/'native/virtual_adaptation_evidence.h'].decode('utf-8-sig').replace('\r\n','\n')
        original=extract(header,'class FrameSelector {')
        metadata_start=original.index('    // First capacity rejection per outcome shape only.')
        metadata_end=original.index('public:',metadata_start)
        original=original[:metadata_start]+original[metadata_end:]
        export_start=original.index('public:')+len('public:')
        export_end=original.index('    bool select(const FrameStamp& s) noexcept {',export_start)
        original=original[:export_start]+'\n'+original[export_end:]
        rejection_start=original.index('                if(used==contexts_.size()){')
        rejection=extract(original[rejection_start:],'                if(used==contexts_.size()){')
        original=original[:rejection_start]+original[rejection_start:].replace(rejection,'                if(used==contexts_.size()){++capacity_;return false;}',1)
        original=original.replace('class FrameSelector {','class FrameSelectorBefore {',1)
        control='namespace ymcc::virtualAdaptation {\n'+original+';\n}\n'
        put(run/'selector-before-control.inc',control.encode())
        actual_receipt=extract(observer,'static void virtualAdaptationFrameReceipt(')
        template=bound[ROOT/'tools/fixtures/log_frequency_frame_context.cpp'].decode('utf-8-sig')
        assert template.count('// ORIGINAL_SELECTOR_CONTROL')==1 and template.count('// ACTUAL_ADAPTATION_FUNCTIONS')==1
        code=template.replace('// ORIGINAL_SELECTOR_CONTROL',control,1).replace('// ACTUAL_ADAPTATION_FUNCTIONS','\n'.join(functions.values())+'\n'+actual_receipt,1)
        save(run/'selector-binding.json',{'controlSha256':hashlib.sha256(control.encode()).hexdigest(),
            'controlAdaptation':'remove only new capacity first-witness metadata,export API and reject-capture branch;old exact select algorithm retained',
            'actualProjectionSha256':hashlib.sha256(functions['static json virtualAdaptationContextEvidence('].encode()).hexdigest(),
            'actualReceiptSha256':hashlib.sha256(actual_receipt.encode()).hexdigest(),
            'scope':'complete actual selector+existing receipt observer/status with cached fixture machine and null source leaf;no devices'})
    cpp=run/'log-frequency-harness.cpp';exe=run/'log-frequency-harness.exe';put(cpp,code.encode())
    node=pathlib.Path('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe')
    toolchain=ROOT/'tools/fixtures/hc_reviewed_calibration/toolchain.mjs';vcvars=pathlib.Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
    env_script='import {calibrationToolchainPreamble} from "./tools/fixtures/hc_reviewed_calibration/toolchain.mjs";process.stdout.write(calibrationToolchainPreamble(process.argv[1]));'
    env=subprocess.run([str(node),'--input-type=module','-e',env_script,str(vcvars)],cwd=ROOT,capture_output=True,timeout=min(15,deadline-time.monotonic()))
    put(run/'environment.stdout.raw',env.stdout);put(run/'environment.stderr.raw',env.stderr)
    if env.returncode:return env.returncode
    command=env.stdout.decode()+f'where cl.exe > "{run / "compiler-paths.txt"}"\r\nwhere link.exe > "{run / "linker-paths.txt"}"\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /sourceDependencies "{run / "dependencies.json"}" "{cpp}" /I"{run}" /Fo"{run / "harness.obj"}" /Fe"{exe}"\r\nexit /b %errorlevel%\r\n'
    batch=run/'build.cmd';put(batch,command.encode())
    build=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,timeout=min(75,deadline-time.monotonic()))
    put(run/'compile.stdout.raw',build.stdout);put(run/'compile.stderr.raw',build.stderr)
    tools=[ident(p) for p in [pathlib.Path(sys.executable),node,vcvars,toolchain,pathlib.Path(os.environ.get('SystemRoot','C:/Windows'))/'System32/cmd.exe']]
    for name in ['compiler-paths.txt','linker-paths.txt']:
        for path in (run/name).read_text().splitlines():
            if pathlib.Path(path).is_file():tools.append(ident(pathlib.Path(path)))
    save(run/'tool-identity.json',{'tools':tools,'compilerExitCode':build.returncode})
    if build.returncode:print(build.stdout.decode(errors='replace')+build.stderr.decode(errors='replace'));return build.returncode
    result=subprocess.run([str(exe)]+([str(run/'fixtures')]+(['--index-only'] if args.adaptation_export_boundary=='index' else ['--model-only'] if args.adaptation_export_boundary=='model' else ['--facts-rejected-only'] if args.adaptation_export_boundary=='facts-rejected' else ['--signals-rejected-only'] if args.adaptation_export_boundary=='signals-rejected' else ['--facts-rejected-resume'] if args.adaptation_export_boundary=='facts-rejected-resume' else []) if args.adaptation_export_only else ['--guide-only'] if args.adaptation_selector_boundary=='guide' else []),cwd=run,capture_output=True,timeout=min(45 if args.adaptation_selector_only else 20,deadline-time.monotonic()))
    put(run/'result.stdout.raw',result.stdout);put(run/'result.stderr.raw',result.stderr)
    report=json.loads(result.stdout);report['nativeExitCode']=result.returncode
    report['sourceStable']=all(p.read_bytes()==b for p,b in bound.items());report['boundInputs']=[ident(p) for p in files]
    report['executable']=ident(exe);report['generatedCpp']=ident(cpp);report['rawResult']=ident(run/'result.stdout.raw');report['compileDependencies']=ident(run/'dependencies.json');report['runDirectory']=str(run)
    report['checksActuallyExecuted']=len(report['checks']);report['defaultSuiteExecuted']=False;report['productOrHardwareExecuted']=False;report['wallMs']=(time.monotonic()-started)*1000
    save(run/'result.json',report);save(output/'report.json',report);print(json.dumps(report,ensure_ascii=False,indent=2))
    return result.returncode if report['sourceStable'] else 4

def run_decky_mode_health_case(args):
    # Directed original log-frequency writer; ordinary/default suite stays separate.
    phase=getattr(args,'decky_log_phase',None)
    if phase not in (None,'memo-exception','memo-last-commit','jsonl-boundary','failure-counters'):raise ValueError('unknown original Decky logging phase')
    phaseInputs={ROOT/'native/log_jsonl_boundary.h':(ROOT/'native/log_jsonl_boundary.h').read_bytes()}
    import ast, shutil, time
    started=time.monotonic()
    output=args.output_dir.resolve();output.mkdir(parents=True,exist_ok=True)
    run=output/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()))
    run.mkdir(exist_ok=False)
    def put(p,b):
        with p.open('xb') as stream:stream.write(b)
    def save(p,value):put(p,(json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode())
    def ident(p):
        p=pathlib.Path(p);data=p.read_bytes();return {'path':str(p.resolve()),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    source_file=ROOT/'native/main.cpp';raw=source_file.read_bytes();source=raw.decode('utf-8-sig').replace('\r\n','\n')
    own_file=pathlib.Path(__file__);own=own_file.read_bytes();tree=ast.parse(own.decode('utf-8-sig'))
    main_fn=next(node for node in tree.body if isinstance(node,ast.FunctionDef) and node.name=='main')
    original=next(node.value.value for node in main_fn.body if isinstance(node,ast.Assign) and any(isinstance(target,ast.Name) and target.id=='tests' for target in node.targets))
    original_decky=original[original.index('        const auto beforeDecky='):original.index('        bool survived=true;try{recordInspection')]
    original_sink=original[original.index('    reset(root,L"decky-sink-recovery");'):original.index('    {\n    reset(root,L"model-adaptation");')]
    signatures=['static std::string W2U(const wchar_t* w, int len = -1) {',
                'static std::string W2U(const std::wstring& w) {',
                'static void rotateOneLogFile(const std::wstring& path, unsigned long long maxBytes) {',
                'static std::wstring deckySidebarLogPath() {',
                'static void deckySidebarDiagnostic(const char* event,const json& detail,bool force,const ymcc::steamfailure::FailureContext* failure) {']
    functions=[extract(source,signature) for signature in signatures]
    prelude=r'''
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "json.hpp"
#include "decky_sidebar_diagnostics.h"
#include "log_snapshot_state.h"
#include "log_jsonl_boundary.h"
using json=nlohmann::json;
namespace fspath=std::filesystem;
static std::wstring g_testRoot;
static const std::wstring POWER_CONTROL_DIR=L"isolated-log-fixture";
static std::atomic<unsigned long long> g_powerGeneration{1};
static bool g_detail=false;
static unsigned detailRows=0;
static bool domainLogEnabled(const char*) {return g_detail;}
static void appendNativeLifecycleLog(const char*,const json&) {++detailRows;}
static std::wstring yemancc_data_dir(){return g_testRoot;}
static std::string nativeLifecycleTimestamp(){return "2000-01-01T00:00:00.000";}
static unsigned long long recoveryStateOwnProcessStartKey(){FILETIME born{},end{},kernel{},user{};if(!GetProcessTimes(GetCurrentProcess(),&born,&end,&kernel,&user))return 0;return (static_cast<unsigned long long>(born.dwHighDateTime)<<32)|born.dwLowDateTime;}
static void deckySidebarDiagnostic(const char*,const json&,bool,const ymcc::steamfailure::FailureContext* = nullptr);
static json checks=json::array();
static void check(const char* name,bool pass){checks.push_back({{"name",name},{"pass",pass}});}
static std::vector<json> readRows(const wchar_t* name){std::ifstream input(fspath::path(g_testRoot)/name);std::vector<json> rows;for(std::string line;std::getline(input,line);)if(!line.empty())rows.push_back(json::parse(line));return rows;}
static void reset(const fspath::path& root,const wchar_t* name){g_testRoot=(root/name).wstring();fspath::create_directories(g_testRoot);++g_powerGeneration;detailRows=0;g_detail=false;}
'''
    tests=r'''
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;const fspath::path root(argv[1]);
    for(const bool detailed:{false,true}){
        reset(root,detailed?L"old-controls-on":L"old-controls-off");g_detail=detailed;
/*ORIGINAL_DECKY*/
    }
/*ORIGINAL_SINK*/
    reset(root,L"cached-mode");
    json healthy={{"elapsedMs",0u},{"reason",""},{"launchMode","bigpicture"},{"modeKnown",true},{"modeRunning",true},{"modeIdentityConfirmed",true},
        {"deckyBackendKnown",true},{"deckyBackendConnected",true},{"deckyClientKnown",true},{"deckyClientConnected",true},{"deckyHooksKnown",true},{"deckyHooksInstalled",true},
        {"deckyClientCheckingKnown",true},{"deckyClientChecking",false},{"steamPid",37u},{"steamBirth","400"},{"proofStage","registry-account"}};
    for(unsigned i=0;i<128;++i)deckySidebarDiagnostic("steam-mode-read",healthy,true);
    const auto observedHealthyRows=readRows(L"decky-sidebar.log").size();
    check("decky-healthy-cached-mode-repetition-bounded",observedHealthyRows==1);
    auto count=readRows(L"decky-sidebar.log").size();healthy["steamPid"]=38u;healthy["steamBirth"]="401";deckySidebarDiagnostic("steam-mode-read",healthy,true);
    check("decky-healthy-new-steam-birth-retained",readRows(L"decky-sidebar.log").size()==count+1);
    count=readRows(L"decky-sidebar.log").size();++g_powerGeneration;deckySidebarDiagnostic("steam-mode-read",healthy,true);
    check("decky-healthy-same-payload-new-power-retained",readRows(L"decky-sidebar.log").size()==count+1);
    count=readRows(L"decky-sidebar.log").size();healthy["elapsedMs"]=7u;for(unsigned i=0;i<2;++i)deckySidebarDiagnostic("steam-mode-read",healthy,true);
    check("decky-real-read-duration-each-preserved-without-time-threshold",readRows(L"decky-sidebar.log").size()==count+2);
    count=readRows(L"decky-sidebar.log").size();healthy["elapsedMs"]=0u;for(unsigned i=0;i<2;++i)deckySidebarDiagnostic("steam-mode-read",healthy,true);
    check("decky-cached-health-reentry-after-real-read-retained-once",readRows(L"decky-sidebar.log").size()==count+1);
    auto failed=healthy;failed["modeKnown"]=false;failed["reason"]="steam-session-unconfirmed";count=readRows(L"decky-sidebar.log").size();
    for(unsigned i=0;i<2;++i)deckySidebarDiagnostic("steam-mode-read",failed,true);
    check("decky-mode-failure-evidence-never-health-suppressed",readRows(L"decky-sidebar.log").size()==count+2);
    count=readRows(L"decky-sidebar.log").size();for(unsigned i=0;i<2;++i)deckySidebarDiagnostic("steam-mode-read",healthy,true);
    check("decky-health-return-after-failure-retained-once",readRows(L"decky-sidebar.log").size()==count+1);
    count=readRows(L"decky-sidebar.log").size();healthy["deckyBackendConnected"]=false;deckySidebarDiagnostic("steam-mode-read",healthy,true);healthy["deckyBackendConnected"]=true;deckySidebarDiagnostic("steam-mode-read",healthy,true);
    check("decky-official-channel-loss-and-return-both-retained",readRows(L"decky-sidebar.log").size()==count+2);
    count=readRows(L"decky-sidebar.log").size();healthy["proofStage"]="ui-account";deckySidebarDiagnostic("steam-mode-read",healthy,true);
    check("decky-healthy-proof-source-change-retained",readRows(L"decky-sidebar.log").size()==count+1);
    auto unknown=healthy;unknown["deckyHooksKnown"]="unknown";count=readRows(L"decky-sidebar.log").size();for(unsigned i=0;i<2;++i)deckySidebarDiagnostic("steam-mode-read",unknown,true);
    check("decky-malformed-or-unknown-health-not-promoted-to-cache",readRows(L"decky-sidebar.log").size()==count+2);
    auto noDuration=healthy;noDuration.erase("elapsedMs");auto signedZero=healthy;signedZero["elapsedMs"]=0;count=readRows(L"decky-sidebar.log").size();
    for(unsigned i=0;i<2;++i){deckySidebarDiagnostic("steam-mode-read",noDuration,true);deckySidebarDiagnostic("steam-mode-read",signedZero,true);}
    check("decky-missing-or-untyped-cache-duration-not-invented",readRows(L"decky-sidebar.log").size()==count+4);
    ymcc::steamfailure::FailureContext context;context.actionId=31;context.operation=ymcc::steamfailure::Operation::Reconnect;
    const json trace={{"ok",false},{"steamFailure",ymcc::steamfailure::native(ymcc::steamfailure::Domain::WinHttp,12002,ymcc::steamfailure::SourceStage::Receive)}};
    count=readRows(L"decky-sidebar.log").size();for(unsigned i=0;i<2;++i)deckySidebarDiagnostic("steam-recovery-trace",trace,true,&context);
    check("decky-failure-action-correlation-writes-are-unchanged",readRows(L"decky-sidebar.log").size()==count+2&&readRows(L"decky-sidebar.log").back()["actionId"]=="31");
    reset(root,L"mode-sink-recovery");g_testRoot+=L"\\missing";deckySidebarDiagnostic("steam-mode-read",healthy,true);fspath::create_directories(g_testRoot);deckySidebarDiagnostic("steam-mode-read",healthy,true);
    check("decky-healthy-failed-write-does-not-consume-first-evidence",readRows(L"decky-sidebar.log").size()==1);
    unsigned failures=0;for(const auto& row:checks)failures+=!row["pass"].get<bool>();
    std::cout<<json{{"selectedCase","decky-mode-health"},{"checks",checks},{"failures",failures},{"originalDeckyControls",7},{"cachedHealthyObservations",128},{"actualHealthyRows",observedHealthyRows},{"scope","EXTRACTED_REAL_DECKY_WRITER_AND_ROTATION;ISOLATED_REAL_FILES;NO_STEAM_OR_PRODUCT"}}.dump(2)<<std::endl;
    return failures?3:0;
}
'''.replace('/*ORIGINAL_DECKY*/',original_decky).replace('/*ORIGINAL_SINK*/',original_sink)
    if phase in ('memo-exception','memo-last-commit','jsonl-boundary','failure-counters'):
        fixture=ROOT/'tools/fixtures/log_frequency_decky_memo_exception.inc'
        paths=[fixture,ROOT/'tools/input_pipe_test_allocation_fault.h',ROOT/'native/log_snapshot_state.h',ROOT/'native/log_jsonl_boundary.h',ROOT/'native/decky_sidebar_diagnostics.h',ROOT/'native/decky_frontend_state.h',ROOT/'native/steam_failure_diagnostics.h',ROOT/'deps/json/json.hpp']
        phaseInputs={p:p.read_bytes() for p in paths}
        prelude=prelude.replace('#include "json.hpp"','#include "input_pipe_test_allocation_fault.h"\n#include <thread>\n#include "json.hpp"',1)
        prelude=prelude.replace('static std::wstring yemancc_data_dir(){return g_testRoot;}','''enum class FaultSite {none,path,timestamp};
static FaultSite fixtureFault=FaultSite::none;static unsigned fixtureFaults=0;
static std::wstring yemancc_data_dir(){if(fixtureFault==FaultSite::path){++fixtureFaults;ymcc::testing::allocationFault::failAfter(0);}return g_testRoot;}''',1)
        prelude=prelude.replace('static std::string nativeLifecycleTimestamp(){return "2000-01-01T00:00:00.000";}','''static std::string nativeLifecycleTimestamp(){if(fixtureFault==FaultSite::timestamp){++fixtureFaults;ymcc::testing::allocationFault::failAfter(0);return std::string(128,'T');}return "2000-01-01T00:00:00.000";}''',1)
        tests=phaseInputs[fixture].decode('utf-8-sig').replace('\r\n','\n')
        if phase=='jsonl-boundary':
            prelude='#define YMCC_DECKY_JSONL_BOUNDARY_ONLY 1\n'+prelude
            prelude=prelude.replace('static std::wstring yemancc_data_dir(){','static unsigned boundaryPathQueries=0;\nstatic std::wstring yemancc_data_dir(){++boundaryPathQueries;',1)
        if phase=='failure-counters':
            prelude='#define YMCC_DECKY_FAILURE_COUNTERS_ONLY 1\n'+prelude
            prelude=prelude.replace('static std::wstring yemancc_data_dir(){','static unsigned failurePathQueries=0;\nstatic std::wstring yemancc_data_dir(){++failurePathQueries;',1)
            exportStart=source.index('        try {\n            availability["deckyLogFailureEvidence"]')
            exportEnd=source.index('        availability["nativeIpcFailureEvidence"]',exportStart)
            exportBlock=source[exportStart:exportEnd]
            prelude+='\n'+'enum class OutputFault {none,openException,writeFailure,writeException,flushFailure,flushException};\nstatic OutputFault outputFault=OutputFault::none;\nstruct FailureOutput:std::ofstream {\n    struct Buffer:std::streambuf {\n        std::streambuf* original=nullptr;\n        std::streamsize xsputn(const char* text,std::streamsize size) override {\n            if(outputFault==OutputFault::writeFailure)return 0;\n            if(outputFault==OutputFault::writeException)throw std::runtime_error("fixture-write");\n            return original->sputn(text,size);\n        }\n        int_type overflow(int_type value) override {\n            if(traits_type::eq_int_type(value,traits_type::eof()))return traits_type::not_eof(value);\n            return original->sputc(traits_type::to_char_type(value));\n        }\n        int sync() override {\n            if(outputFault==OutputFault::flushFailure)return -1;\n            if(outputFault==OutputFault::flushException)throw std::runtime_error("fixture-flush");\n            return original->pubsync();\n        }\n    } buffer;\n    bool attached=false;\n    FailureOutput(){\n        buffer.original=std::ofstream::rdbuf();\n        if(outputFault==OutputFault::openException)exceptions(std::ios::failbit|std::ios::badbit);\n        else if(outputFault==OutputFault::writeException||outputFault==OutputFault::flushException)exceptions(std::ios::badbit);\n    }\n    FailureOutput& operator<<(const std::string& value){\n        if(outputFault!=OutputFault::none&&outputFault!=OutputFault::openException&&!attached){\n            static_cast<std::ostream&>(*this).rdbuf(&buffer);attached=true;\n        }\n        static_cast<std::ostream&>(*this)<<value;return *this;\n    }\n    FailureOutput& operator<<(const char* value){static_cast<std::ostream&>(*this)<<value;return *this;}\n    ~FailureOutput(){\n        ymcc::testing::allocationFault::failAfter(-1);\n        exceptions(std::ios::goodbit);\n        if(attached)static_cast<std::ostream&>(*this).rdbuf(buffer.original);\n    }\n};\nnamespace ymcc::sleepfactlog {\nstruct FixtureWriter {DWORD pid;};\nstatic const FixtureWriter& writerIdentity(){static const FixtureWriter value{GetCurrentProcessId()};return value;}\nstatic json writerCreation100nsJson(){return std::to_string(recoveryStateOwnProcessStartKey());}\n}\nstatic void appendFailureAvailability(json& availability){\n// ACTUAL_DECKY_FAILURE_EXPORT\n}\nstatic json failureAvailability(){json availability=json::object();appendFailureAvailability(availability);return availability;}\n\n'.replace('// ACTUAL_DECKY_FAILURE_EXPORT',exportBlock,1)
            needle='std::ofstream output;'
            if functions[-1].count(needle)!=1:raise ValueError('original Decky output port changed')
            functions[-1]=functions[-1].replace(needle,'FailureOutput output;',1)
        if phase=='memo-last-commit':
            prelude='#define YMCC_DECKY_LAST_COMMIT_ONLY 1\n'+prelude+'\n'+'''static unsigned fixtureSuccessfulFlushes=0;static int fixtureArmAtDestruction=-2;
struct FixtureOutput:std::ofstream {
    using std::ofstream::ofstream;
    void flush(){std::ofstream::flush();if(good()){++fixtureSuccessfulFlushes;ymcc::testing::allocationFault::failAfter(0);}}
    ~FixtureOutput(){fixtureArmAtDestruction=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);}
};'''
            needle='std::ofstream output;'
            if functions[-1].count(needle)!=1:raise ValueError('original Decky output port changed')
            functions[-1]=functions[-1].replace(needle,'FixtureOutput output;',1)

    code=prelude+'\n'+'\n\n'.join(functions)+'\n'+tests
    cpp=run/'log-frequency-harness.cpp';exe=run/'log-frequency-harness.exe';put(cpp,code.encode())
    save(run/'extracted-source.json',{'source':ident(source_file),'generator':ident(own_file),'functions':[{'signature':s,'sha256':hashlib.sha256(f.encode()).hexdigest()} for s,f in zip(signatures,functions)],'originalDeckyControlsUnchanged':original_decky,'originalSinkControlUnchanged':original_sink,'sourcePorts':['isolated path','timestamp','existing detail sink counter','actual self-process birth query'],'selectedPhase':phase,'phaseSourceInputs':[ident(p) for p in phaseInputs],'originalDeckyControlsExecuted':phase is None,'generatedCpp':ident(cpp)})
    compiler=shutil.which('cl.exe');assert compiler,'Reviewed compiler must be supplied in PATH'
    compile_args=[compiler,'/nologo','/Bv','/std:c++20','/utf-8','/EHsc','/MT','/O2','/WX','/DNOMINMAX','/DUNICODE','/D_UNICODE','/sourceDependencies',str(run/'dependencies.json'),str(cpp),'/I'+str(ROOT/'native'),'/I'+str(ROOT/'deps/json'),'/I'+str(ROOT/'tools'),'/Fo'+str(run/'harness.obj'),'/Fe'+str(exe),'/link','/verbose:lib']
    compile_start=time.monotonic();build=subprocess.run(compile_args,cwd=run,capture_output=True,timeout=60)
    put(run/'compile.stdout.raw',build.stdout);put(run/'compile.stderr.raw',build.stderr)
    save(run/'compile-process.json',{'program':ident(compiler),'arguments':compile_args[1:],'cwd':str(run),'exitCode':build.returncode,'elapsedMs':(time.monotonic()-compile_start)*1000,'timeoutMs':60000})
    if build.returncode:print(build.stdout.decode(errors='replace')+build.stderr.decode(errors='replace'));return build.returncode
    execute_start=time.monotonic();result=subprocess.run([str(exe),str(run/'logs')],cwd=run,capture_output=True,timeout=20)
    put(run/'result.stdout.raw',result.stdout);put(run/'result.stderr.raw',result.stderr)
    report=json.loads(result.stdout);report['nativeExitCode']=result.returncode;report['sourceStable']=source_file.read_bytes()==raw and own_file.read_bytes()==own and all(p.read_bytes()==b for p,b in phaseInputs.items())
    report['actualProcess']={'program':ident(exe),'arguments':[str(run/'logs')],'cwd':str(run),'exitCode':result.returncode,'elapsedMs':(time.monotonic()-execute_start)*1000,'timeoutMs':20000}
    report['executable']=ident(exe);report['generatedCpp']=ident(cpp);report['rawResult']=ident(run/'result.stdout.raw');report['compileDependencies']=ident(run/'dependencies.json');report['runDirectory']=str(run)
    report['pythonRuntimeFiles']=[ident(p) for p in sorted({getattr(module,'__file__','') for module in sys.modules.values() if getattr(module,'__file__','') and pathlib.Path(getattr(module,'__file__','')).is_file()})]
    report['checksActuallyExecuted']=len(report['checks']);report['defaultSuiteExecuted']=False;report['productOrHardwareExecuted']=False;report['wallMs']=(time.monotonic()-started)*1000
    save(run/'result.json',report);save(output/'report.json',report)
    summary={'selectedCase':'decky-mode-health','failures':report['failures'],'checks':len(report['checks']),'report':str(output/'report.json')}
    if phase is None:summary.update(healthyObservations=128,actualHealthyRows=report['actualHealthyRows'])
    else:summary['selectedPhase']=phase
    print(json.dumps(summary))
    return result.returncode if report['sourceStable'] else 4


def coordinator_rewrite_fixture(source: str) -> str:
    """Full original coordinator source read and owned staged-commit ports."""
    common=(ROOT/'tools/fixtures/log_frequency_fan_failure.cpp.in').read_text(encoding='utf-8-sig').split('@@ACTUAL_WRITER@@',1)[0]
    common=common.replace('#include <fstream>','#include <fstream>\n#include <deque>',1)
    common=common.replace('static std::string nativeLifecycleTimestamp','[[maybe_unused]] static std::string nativeLifecycleTimestamp',1)
    template=common+(ROOT/'tools/fixtures/log_frequency_coordinator_rewrite.inc').read_text(encoding='utf-8-sig')
    writer=extract(source,'static void writeCoordinatorDiagnosticLog(const std::string& event, const json& details) {')
    if writer.count('std::ifstream in')!=1 or writer.count('std::ofstream out') not in (0,1):raise ValueError('coordinator source stream port changed')
    writer=writer.replace('std::ifstream in','CoordinatorInput in',1).replace('std::ofstream out','CoordinatorOutput out',1)
    writer='[[maybe_unused]] '+extract(source,'static void decorateDomainLogWriter(json& line) {')+'\n'+writer
    declaration=extract(source,'enum class CoordinatorRewriteResult {')+';'
    helper=extract(source,'static CoordinatorRewriteResult commitCoordinatorLogRewrite(const std::wstring& path,const std::string& content,const char* event) noexcept {')
    transaction=declaration+'\n#define CreateFileW coordinatorCreatePort\n#define WriteFile coordinatorWritePort\n#define CloseHandle coordinatorClosePort\n#define MoveFileExW coordinatorMovePort\n#define GetFileInformationByHandle coordinatorInformationPort\n#define SetFileInformationByHandle coordinatorDispositionPort\n[[maybe_unused]] '+helper+'\n#undef CreateFileW\n#undef WriteFile\n#undef CloseHandle\n#undef MoveFileExW\n#undef GetFileInformationByHandle\n#undef SetFileInformationByHandle\n'
    marker='        try {\n            availability["coordinatorRewriteCommitEvidence"]';start=source.index(marker);end=source.index('        try {\n            availability["fanDetailLogFailureEvidence"]',start)
    for token,value in (('@@ACTUAL_TRANSACTION@@',transaction),('@@ACTUAL_CLASSIFIER@@',extract(source,'static bool isCoordinatorDiagnosticEvent(const std::string& event) {')),('@@ACTUAL_WRITER@@',writer),('@@ACTUAL_EXPORT@@',source[start:end])):
        if template.count(token)!=1:raise ValueError('coordinator fixture token changed:'+token)
        template=template.replace(token,value,1)
    return template


def fan_detail_failure_fixture(source: str) -> str:
    """Original detailed writer with the existing owned stream-fault port."""
    common=(ROOT/'tools/fixtures/log_frequency_fan_failure.cpp.in').read_text(encoding='utf-8-sig').split('@@ACTUAL_WRITER@@',1)[0]
    common=common.replace(' FailureOutput(){',' using std::ofstream::ofstream;\n FailureOutput(){',1)
    common=common.replace('static std::wstring yemancc_data_dir','[[maybe_unused]] static std::wstring yemancc_data_dir',1).replace('static std::string nativeLifecycleTimestamp','[[maybe_unused]] static std::string nativeLifecycleTimestamp',1)
    common=common.replace(' FailureOutput& operator<<(char value){',' FailureOutput& operator<<(const char* value){static_cast<std::ostream&>(*this)<<value;return *this;}'+chr(10)+' FailureOutput& operator<<(char value){',1)
    template=common+(ROOT/'tools/fixtures/log_frequency_fan_detail_failure.inc').read_text(encoding='utf-8-sig')
    writer=extract(source,'static void writeFanDiagnosticLog(const std::string& event, const json& details) {')
    if writer.count('std::ofstream f')!=1:raise ValueError('original Fan detail stream port changed')
    writer=writer.replace('std::ofstream f','FailureOutput f',1)
    writer='[[maybe_unused]] '+extract(source,'static void decorateDomainLogWriter(json& line) {')+'\n'+writer
    marker='        try {\n            availability["fanDetailLogFailureEvidence"]'
    start=source.index(marker);end=source.index('        try {\n            availability["fanLifecycleLogFailureEvidence"]',start)
    for token,value in (('@@ACTUAL_WRITER@@',writer),('@@ACTUAL_EXPORT@@',source[start:end])):
        if template.count(token)!=1:raise ValueError('Fan detail fixture token changed:'+token)
        template=template.replace(token,value,1)
    return template


def fan_lifecycle_failure_fixture(source: str) -> str:
    """Full original Fan writer and original optional export block,owned file ports."""
    template=(ROOT/'tools/fixtures/log_frequency_fan_failure.cpp.in').read_text(encoding='utf-8-sig')
    writer=extract(source,'static void appendFanLifecycleLog(const char* event, json detail) {')
    if writer.count('std::ofstream out;')!=1:raise ValueError('original Fan stream port changed')
    writer=writer.replace('std::ofstream out;','FailureOutput out;',1)
    marker='        try {\n            availability["fanLifecycleLogFailureEvidence"]'
    start=source.index(marker);end=source.index('        try {\n            availability["touchHapticSummaryPreparationEvidence"]',start)
    for token,value in (('@@ACTUAL_WRITER@@',writer),('@@ACTUAL_EXPORT@@',source[start:end])):
        if template.count(token)!=1:raise ValueError('Fan failure fixture token changed:'+token)
        template=template.replace(token,value,1)
    return template


def fan_lifecycle_envelope_fixture(source: str) -> str:
    """Existing log-frequency consumer: full Fan projector/writer, private files."""
    template = (ROOT / 'tools/fixtures/log_frequency_fan_lifecycle.cpp').read_text(encoding='utf-8-sig')
    start = source.index('static const char* const kFanLifecycleEvidenceFields[] = {')
    end = source.index('static json projectFanLifecycleEvidence(', start)
    parts = {'FIELDS': source[start:end],
        'CLASSIFIER': extract(source, 'static bool fanLifecycleEvidenceAlwaysLogged(const std::string& event) {'),
        'PROJECTOR': extract(source, 'static json projectFanLifecycleEvidence(const json& details) {'),
        'WRITER': extract(source, 'static void appendFanLifecycleLog(const char* event, json detail) {')}
    for name, code in parts.items():
        marker = '// ACTUAL_' + name
        assert template.count(marker) == 1
        template = template.replace(marker, code, 1)
    return template


def run_fan_lifecycle_envelope_case(args):
    import shutil, time
    source_file = (args.source or ROOT / 'native/main.cpp').resolve()
    raw = source_file.read_bytes()
    source = raw.decode('utf-8-sig').replace('\r\n', '\n')
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    run = out / ('fan-envelope-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + str(os.getpid()))
    run.mkdir(exist_ok=False)
    def put(name, data):
        with (run / name).open('xb') as stream: stream.write(data)
    def save(name, value): put(name, (json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode())
    cpp = run / 'fixture.cpp'; exe = run / 'fixture.exe'
    put('fixture.cpp', (coordinator_rewrite_fixture(source) if args.fan_lifecycle_case=='coordinator-rewrite' else fan_detail_failure_fixture(source) if args.fan_lifecycle_case=='detail-failures' else fan_lifecycle_failure_fixture(source) if args.fan_lifecycle_case=='failures' else fan_lifecycle_envelope_fixture(source)).encode())
    compiler = shutil.which('cl.exe')
    assert compiler, 'Reviewed compiler must be supplied in PATH'
    arguments = [compiler, '/nologo', '/std:c++20', '/utf-8', '/EHsc', '/MT', '/O2', '/W4', '/WX',
        '/showIncludes', str(cpp), '/I' + str(ROOT / 'native'), '/I' + str(ROOT / 'deps/json'),
        '/Fo' + str(run / 'fixture.obj'), '/Fe' + str(exe), '/link', '/verbose:lib']
    tick = time.monotonic()
    built = subprocess.run(arguments, cwd=run, capture_output=True, timeout=90)
    put('compile.stdout.raw', built.stdout); put('compile.stderr.raw', built.stderr)
    save('COMPILE.json', {'exitCode': built.returncode, 'arguments': arguments, 'elapsedMs': (time.monotonic()-tick)*1000})
    if built.returncode: return built.returncode
    selection = {'text': 'text-only', 'boundary': 'boundary-only'}.get(args.fan_lifecycle_case, 'all')
    modes = ('known',) if args.fan_lifecycle_case else ('known', 'unknown')
    for mode in modes:
        arguments = [str(exe), str(run / mode)] if args.fan_lifecycle_case in ('failures','detail-failures','coordinator-rewrite') else [str(exe), str(run / mode), mode, selection]
        result = subprocess.run(arguments, cwd=run, capture_output=True, timeout=30)
        put(mode + '.stdout.raw', result.stdout); put(mode + '.stderr.raw', result.stderr)
        report = json.loads(result.stdout)
        save(mode + '.json', {'exitCode': result.returncode, 'report': report})
        if result.returncode or report['failures']: return result.returncode or 3
    assert source_file.read_bytes() == raw, 'Bound Fan source changed during selected execution'
    print(json.dumps({'selectedCase': 'fan-lifecycle-envelope', 'runDirectory': str(run), 'failures': 0}))
    return 0


def run_primary_log_retry_case(args):
    # Logging-only selection in this existing consumer. The resource matrix and
    # its strict source contract keep their own entry and historical receipts.
    import importlib.util
    output=args.output_dir.resolve();output.mkdir(parents=True,exist_ok=True)
    run=output/('primary-log-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'-'+str(os.getpid()));run.mkdir()
    def put(p,b):
        with p.open('xb') as f:f.write(b.encode('utf8') if isinstance(b,str) else b)
    def save(p,v):put(p,json.dumps(v,ensure_ascii=False,indent=2)+'\n')
    def pin(p):
        b=p.read_bytes();return {'path':str(p.resolve()),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
    sourcePath=args.source.resolve() if args.source else ROOT/'native/main.cpp'
    source=sourcePath.read_bytes();text=source.decode('utf-8-sig').replace('\r\n','\n')
    fixturePath=ROOT/'tools/fixtures/input_host_primary_thread/suite.cpp.in';fixture=fixturePath.read_bytes();ft=fixture.decode('utf-8-sig').replace('\r\n','\n')
    helper=extract(text,'static bool inputHostPrimaryThreadRetirementProvenLocked(const char* phase) noexcept {')
    base=extract(text,'static bool nativeLifecycleEventIsBase(const char* event) {')
    currentCatch='''        catch (...) {
            lastFailed = nullptr; lastError = ERROR_SUCCESS; // Retry a missing failure witness on the next original call.
            /* evidence failure cannot discard or rebind this owner */
        }'''
    previousCatch='        catch (...) { /* evidence failure cannot discard or rebind this owner */ }'
    if helper.count(currentCatch)!=1:raise ValueError('primary log retry source guard missing or ambiguous')
    oldHelper=helper.replace(currentCatch,previousCatch,1)
    classStart=ft.index('struct PrivateThread {');classEnd=ft.index('static void threadFacts(',classStart)
    originalClass=ft[classStart:classEnd]
    prelude=r'''
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
#include "input_host_process_retirement.h"
#include "input_pipe_test_allocation_fault.h"
using json=nlohmann::json;
static json checks=json::array();
static void check(bool value,const char* name){checks.push_back({{"name",name},{"pass",value}});}
static void require(bool value,const char* name){if(!value)throw std::runtime_error(name);}
static HANDLE g_inputHostPrimaryThread=nullptr,g_inputHostProcess=nullptr;
static std::string g_inputHostRunId="old-run";
static unsigned long long g_inputHostEpoch=17;
static struct{json logs=json::array();}ports;
static bool primaryLoggerThrows=false;
static unsigned sinkExceptions=0,primaryNegativeSalvage=0,primaryThreadJoins=0;
static void appendNativeLifecycleLog(const char* event,const json& data){
 if(primaryLoggerThrows){++sinkExceptions;throw std::runtime_error("injected-log-failure");}
 ports.logs.push_back({{"event",event},{"data",data}});
}
'''
    selected=r'''
int main(){try{
 DWORD before=0,after=0;require(GetProcessHandleCount(GetCurrentProcess(),&before)!=FALSE,"baseline handles");
 check(nativeLifecycleEventIsBase("input-host-primary-thread-retirement"),"primary event is already resident");
 check(!nativeLifecycleEventIsBase("input-host-primary-thread-retirement-extra"),"primary event registry remains exact");
 PrivateThread owner;owner.protect();g_inputHostPrimaryThread=owner.thread;
 primaryLoggerThrows=true;const bool first=inputHostPrimaryThreadRetirementProvenLocked("throwing-log");primaryLoggerThrows=false;
 check(!first&&g_inputHostPrimaryThread==owner.thread&&sinkExceptions==1&&ports.logs.empty(),"sink failure keeps actual protected primary owner without row");
 const bool retry=inputHostPrimaryThreadRetirementProvenLocked("throwing-log");
 check(!retry&&g_inputHostPrimaryThread==owner.thread&&ports.logs.size()==1,"same failed Close retries missing primary witness after sink exception");
#ifndef YMCC_PRIMARY_LOG_PREFIX_ONLY
 check(ports.logs.size()==1&&ports.logs[0]["data"]["handleClosed"]==false&&ports.logs[0]["data"]["ownerRetained"]==true&&ports.logs[0]["data"]["runId"]=="old-run"&&ports.logs[0]["data"]["epoch"]==17,"retried row preserves owner tuple and failure facts");
#endif
 const auto rows=ports.logs.size();ymcc::testing::allocationFault::failAfter(0);bool failed=true;
 for(unsigned i=0;i<1000;++i)failed=(!inputHostPrimaryThreadRetirementProvenLocked("repeat"))&&failed;
 const auto armed=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
 check(failed&&armed==0&&ports.logs.size()==rows,"1000 unchanged failures have no new JSON allocation or log rows");
 owner.unprotect();check(inputHostPrimaryThreadRetirementProvenLocked("returned")&&g_inputHostPrimaryThread==nullptr,"same real protected handle returns after restriction removed");
 check(owner.alive(),"closing handle is not thread exit");owner.finish();
#ifndef YMCC_PRIMARY_LOG_PREFIX_ONLY
 g_inputHostRunId="allocation-run";g_inputHostEpoch=18;PrivateThread allocationOwner;allocationOwner.protect();g_inputHostPrimaryThread=allocationOwner.thread;
 const auto beforeAllocation=ports.logs.size();const auto priorSinkThrows=sinkExceptions;
 ymcc::testing::allocationFault::failAfter(0);const bool allocationFailed=inputHostPrimaryThreadRetirementProvenLocked("allocation");
 const auto consumed=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
 check(!allocationFailed&&consumed==-1&&g_inputHostPrimaryThread==allocationOwner.thread&&ports.logs.size()==beforeAllocation&&sinkExceptions==priorSinkThrows,"JSON allocation failure keeps actual owner and does not enter sink");
 check(!inputHostPrimaryThreadRetirementProvenLocked("allocation")&&ports.logs.size()==beforeAllocation+1,"same failed Close retries missing primary witness after JSON allocation exception");
 allocationOwner.unprotect();check(inputHostPrimaryThreadRetirementProvenLocked("allocation-return")&&g_inputHostPrimaryThread==nullptr,"allocation-arm same actual handle returns");
 check(allocationOwner.alive(),"allocation-arm handle close does not end live thread");allocationOwner.finish();
 const auto finalRows=ports.logs.size();ymcc::testing::allocationFault::failAfter(0);bool empty=true;
 for(unsigned i=0;i<1000;++i)empty=inputHostPrimaryThreadRetirementProvenLocked("empty")&&empty;
 const auto emptyArmed=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
 check(empty&&emptyArmed==0&&ports.logs.size()==finalRows,"1000 empty owner calls have no new JSON allocation or logs");
#endif
 require(GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE,"closing handles");
 check(before==after&&g_inputHostPrimaryThread==nullptr&&primaryNegativeSalvage==0,"all actual private owner handles returned before exit without salvage");
 unsigned failures=0;for(const auto& row:checks)failures+=!row["pass"].get<bool>();
 std::cout<<json{{"selectedCase","primary-log-retry"},{"pass",failures==0},{"checks",checks},{"failures",failures},{"operationLogs",ports.logs},
 {"handlesBefore",before},{"handlesAfter",after},{"privateThreadsJoinedBeforeExit",primaryThreadJoins},{"negativeOnlySalvage",primaryNegativeSalvage},
 {"fullPrimaryResourceMatrix",false},{"productSdkDeviceExecuted",false}}.dump(2)<<'\n';return failures?2:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 3;}}
'''
    spec=importlib.util.spec_from_file_location('logging_existing_native_tools',ROOT/'tools/input_host_clear_retirement_selftest.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
    tc=m.CAPTURE;capture=json.loads((tc/'build-environment.json').read_text(encoding='utf-8-sig'));manifest=json.loads((tc/'toolchain-manifest.json').read_text(encoding='utf-8-sig'))
    env=dict(capture['environment']);env['INCLUDE']=';'.join(r['frozen'] for r in manifest['roots'] if r['category']=='INCLUDE');env['LIB']=';'.join(r['frozen'] for r in manifest['roots'] if r['category']=='LIB');env['LIBPATH']=env['LIB'];env['VSLANG']='1033'
    for key in list(env):
        if key.upper() in ('CL','_CL_','LINK','_LINK_'):del env[key]
    temp=run/'temp';temp.mkdir();env['TEMP']=str(temp);env['TMP']=str(temp)
    cl=pathlib.Path(capture['tools']['cl.exe']['paths'][0]);approved={str(pathlib.Path(r['frozen']).resolve()).lower():r['sha256'] for r in manifest['files']}
    tools=[]
    # Capture binds historical include/lib roots; executable identity is the
    # current observed tool epoch, pinned before/after this new execution.
    env['PATH']=str(cl.parent)+';'+str(pathlib.Path(sys.executable).parent)+';C:/Windows/System32;C:/Windows'
    for name in ('cl.exe','link.exe'):
        file=pathlib.Path(capture['tools'][name]['paths'][0]);tools.append(pin(file))
    inputs=[sourcePath,fixturePath,ROOT/'native/input_host_process_retirement.h',ROOT/'deps/json/json.hpp',ROOT/'tools/input_pipe_test_allocation_fault.h',ROOT/'tools/input_host_clear_retirement_selftest.py']
    sourcePins=[pin(p) for p in inputs];allowed={str(p.resolve()).lower():pin(p)['sha256'] for p in inputs}
    results=[]
    for variant,body in [('current',helper),('old-catch',oldHelper)]:
        cpp=run/(variant+'.cpp');exe=run/(variant+'.exe');code=prelude+'\n'+body+'\n'+base+'\n'+originalClass+'\n'+selected
        if variant=='old-catch':code='#define YMCC_PRIMARY_LOG_PREFIX_ONLY 1\n'+code
        put(cpp,code);dep=run/(variant+'.dependencies.json')
        command=[str(cl),'/nologo','/std:c++20','/utf-8','/EHsc','/W4','/WX','/wd4505','/MT','/O2','/I'+str(ROOT/'native'),'/I'+str(ROOT/'deps/json'),'/I'+str(ROOT/'tools'),'/sourceDependencies',str(dep),str(cpp),'/Fo'+str(run/(variant+'.obj')),'/Fe'+str(exe),'/link','/VERBOSE:LIB']
        save(run/(variant+'.command.json'),command);p=subprocess.run(command,cwd=run,env=env,capture_output=True,timeout=75)
        put(run/(variant+'.compile.stdout.raw'),p.stdout);put(run/(variant+'.compile.stderr.raw'),p.stderr)
        save(run/(variant+'.COMPILE.json'),{'exitCode':p.returncode})
        if p.returncode:raise RuntimeError('primary log compile failed '+variant)
        data=json.loads(dep.read_text(encoding='utf-8-sig'))['Data'];includes=[]
        for file in data['Includes']:
            q=pin(pathlib.Path(file));key=str(pathlib.Path(file).resolve()).lower()
            if q['sha256']!=allowed.get(key,approved.get(key)):raise ValueError('undeclared primary log include '+file)
            includes.append(q)
        libs=[]
        for file in re.findall(r'^\s*Searching (.+\.lib):?\s*$',(p.stdout+p.stderr).decode('utf8',errors='replace'),re.M|re.I):
            q=pin(pathlib.Path(file));key=str(pathlib.Path(file).resolve()).lower()
            if q['sha256']!=approved.get(key):raise ValueError('undeclared primary log library '+file)
            libs.append(q)
        if not libs:raise ValueError('primary log link graph missing')
        save(run/(variant+'.GRAPH.json'),{'source':pin(cpp),'includes':includes,'libraries':libs,'executable':pin(exe)})
        p=subprocess.run([str(exe)],cwd=run,env=env,capture_output=True,timeout=25);put(run/(variant+'.stdout.raw'),p.stdout);put(run/(variant+'.stderr.raw'),p.stderr)
        result=json.loads(p.stdout);save(run/(variant+'.RESULT.json'),result)
        failed=[q['name'] for q in result['checks'] if not q['pass']]
        if variant=='current':
            if p.returncode!=0 or failed or result['privateThreadsJoinedBeforeExit']!=2:raise ValueError('current primary log checks failed')
        elif p.returncode!=2 or failed!=['same failed Close retries missing primary witness after sink exception'] or result['privateThreadsJoinedBeforeExit']!=1:raise ValueError('necessary old primary log assertion not rejected')
        if result['handlesBefore']!=result['handlesAfter'] or result['negativeOnlySalvage']:raise ValueError('primary log private handles not returned')
        results.append({'variant':variant,'nativeExitCode':p.returncode,'result':pin(run/(variant+'.RESULT.json')),'graph':pin(run/(variant+'.GRAPH.json'))})
    for q in sourcePins+tools:
        if pin(pathlib.Path(q['path']))!=q:raise ValueError('primary log source/tool drift')
    report={'selectedCase':'primary-log-retry','pass':True,'runs':results,'sourcePins':sourcePins,'tools':tools,
            'toolchainManifest':pin(tc/'toolchain-manifest.json'),'toolchainCapture':pin(tc/'build-environment.json'),
            'compilerIdentityAuthority':'current observed before/after tool pins;capture used only for frozen include/lib layout',
            'primaryDefaultMatrixExecuted':False,'fullProductSdkDeviceExecuted':False,'out':str(run)}
    save(run/'RESULT.json',report);print(json.dumps(report,ensure_ascii=False));return 0


def run_deferred_post_witness_case(args):
    """Original native-sink test selection;complete diagnostic queue headers only."""
    import time
    if any((args.source,args.native_sink_before,args.review_existing_run,args.sensor_retirement_phase,args.ipc_reoffer_phase)):
        raise ValueError('deferred-post-witness requires its original current headers and exclusive directed selection')
    output=args.output_dir.resolve();output.mkdir(parents=True,exist_ok=True)
    run=output/('deferred-post-witness-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'-'+str(os.getpid()));run.mkdir(exist_ok=False)
    def put(p,b):
        with p.open('xb') as stream:stream.write(b.encode('utf-8') if isinstance(b,str) else b)
    def pin(p):
        b=p.read_bytes();return dict(path=str(p.resolve()),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
    template=ROOT/'tools/fixtures/log_frequency_deferred_post_witness.cpp'
    toolRecipe=ROOT/'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'
    inputs=[pathlib.Path(__file__),template,toolRecipe,ROOT/'deps/json/json.hpp',ROOT/'native/input_host_diagnostic_runtime.h',ROOT/'native/input_host_diagnostic_dispatch.h']
    frozen={p:p.read_bytes() for p in inputs};cpp=run/'Actual.cpp';put(cpp,frozen[template])
    recipe=json.loads(frozen[toolRecipe])['native'];exe=run/'Fixture.exe'
    command=[recipe['compiler'],'/nologo','/std:c++20','/utf-8','/EHsc','/O2','/MT','/W4','/WX','/wd4505','/showIncludes',
        *['/I'+p for p in recipe['include']],'/I'+str(ROOT/'native'),'/I'+str(ROOT/'deps/json'),str(cpp),'/Fo'+str(run/'Fixture.obj'),'/Fe'+str(exe),
        '/link','/VERBOSE:LIB','/INCREMENTAL:NO',*['/LIBPATH:'+p for p in recipe['lib']],'kernel32.lib']
    started=time.monotonic();built=subprocess.run(command,cwd=run,capture_output=True,timeout=180)
    put(run/'compile.stdout.raw',built.stdout);put(run/'compile.stderr.raw',built.stderr)
    metadata={'arguments':command,'exitCode':built.returncode,'timeoutSeconds':180,'elapsedMs':int((time.monotonic()-started)*1000)}
    put(run/'BUILD.json',json.dumps(metadata,indent=2)+'\n')
    if built.returncode:raise RuntimeError('deferred post witness compile failed: '+str(run))
    ran=subprocess.run([str(exe)]+(['--submission-kinds'] if args.deferred_post_phase=='submission-kinds' else []),cwd=run,capture_output=True,timeout=45)
    put(run/'native.stdout.raw',ran.stdout);put(run/'native.stderr.raw',ran.stderr);result=json.loads(ran.stdout)
    result.update(nativeExitCode=ran.returncode,sourceStable=all(p.read_bytes()==b for p,b in frozen.items()),sourceInputs=[pin(p) for p in inputs],
        generatedCpp=pin(cpp),executable=pin(exe),runDirectory=str(run),elapsedMs=int((time.monotonic()-started)*1000))
    put(output/'report.json',json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(dict(report=str(output/'report.json'),checks=result['checks'],nativeExitCode=ran.returncode)))
    return ran.returncode if result['sourceStable'] else 4


def run_intent_observation_preflight_case(args):
    """Original target-intent fixture plus exact Native producers;value ports only."""
    import time
    if any((args.source,args.native_sink_before,args.review_existing_run,args.sensor_retirement_phase,args.ipc_reoffer_phase,args.deferred_post_phase,args.finite_terminal_phase)):
        raise ValueError('intent-preflight requires only its current original Native sources')
    output=args.output_dir.resolve();output.mkdir(parents=True,exist_ok=True)
    run=output/('intent-preflight-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'-'+str(os.getpid()));run.mkdir(exist_ok=False)
    def put(p,b):
        with p.open('xb') as stream:stream.write(b.encode('utf-8') if isinstance(b,str) else b)
    def pin(p):
        b=p.read_bytes();return dict(path=str(p.resolve()),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
    main=ROOT/'native/main.cpp';fixture=ROOT/'tools/msi_target_intent_evidence_selftest.cpp';extra=ROOT/'tools/fixtures/msi_target_intent_observation.inc';header=ROOT/'native/msi_target_intent_evidence.h';recipePath=ROOT/'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'
    inputs=[pathlib.Path(__file__),main,fixture,extra,header,ROOT/'deps/json/json.hpp',recipePath];bound={p:p.read_bytes() for p in inputs};source=bound[main].decode('utf-8-sig').replace('\r\n','\n')
    signatures=('static void msiTargetIntentObserveInput(const json& saved,const json& effective,bool shortcut,bool overrideActive) noexcept {','static void msiTargetIntentDocumentWrite(const json& args,const json& committed,bool ok,bool conflict,long long stale,long long expected) noexcept {')
    functions='\n\n'.join(extract(source,signature) for signature in signatures)
    body=bound[extra].decode('utf-8-sig').replace('\r\n','\n');assert body.count('// ACTUAL_NATIVE_INTENT_FUNCTIONS')==1;body=body.replace('// ACTUAL_NATIVE_INTENT_FUNCTIONS',functions,1)
    code=bound[fixture].decode('utf-8-sig').replace('\r\n','\n');needle='#include "fixtures/msi_target_intent_observation.inc"';assert code.count(needle)==1
    code='#define YMCC_INTENT_PREFLIGHT_ONLY 1\n'+code.replace('#include "../native/msi_target_intent_evidence.h"','#include "msi_target_intent_evidence.h"',1).replace(needle,body,1)
    cpp=run/'Actual.cpp';put(cpp,code);recipe=json.loads(bound[recipePath])['native'];exe=run/'Fixture.exe'
    command=[recipe['compiler'],'/nologo','/std:c++20','/utf-8','/EHsc','/O2','/MT','/W4','/WX','/wd4505','/showIncludes',*['/I'+p for p in recipe['include']],'/I'+str(ROOT/'native'),'/I'+str(ROOT/'deps/json'),'/I'+str(ROOT/'tools'),str(cpp),'/Fo'+str(run/'Fixture.obj'),'/Fe'+str(exe),'/link','/VERBOSE:LIB','/INCREMENTAL:NO',*['/LIBPATH:'+p for p in recipe['lib']],'kernel32.lib']
    started=time.monotonic();built=subprocess.run(command,cwd=run,capture_output=True,timeout=180);put(run/'compile.stdout.raw',built.stdout);put(run/'compile.stderr.raw',built.stderr)
    put(run/'BUILD.json',json.dumps(dict(arguments=command,exitCode=built.returncode,elapsedMs=int((time.monotonic()-started)*1000),timeoutSeconds=180),indent=2)+'\n')
    if built.returncode:raise RuntimeError('intent preflight compile failed:'+str(run))
    ran=subprocess.run([str(exe),'--observation-preflight'],cwd=run,capture_output=True,timeout=60);put(run/'native.stdout.raw',ran.stdout);put(run/'native.stderr.raw',ran.stderr);result=json.loads(ran.stdout)
    result.update(nativeExitCode=ran.returncode,sourceStable=all(p.read_bytes()==b for p,b in bound.items()),sourceInputs=[pin(p) for p in inputs],generatedCpp=pin(cpp),executable=pin(exe),runDirectory=str(run))
    put(output/'report.json',json.dumps(result,ensure_ascii=False,indent=2)+'\n');print(json.dumps(dict(report=str(output/'report.json'),nativeExitCode=ran.returncode,checks=result['checks'])))
    return ran.returncode if result['sourceStable'] else 4


def domain_truth_observation_region(source):
    marker='appendNativeLifecycleLog("domain-pad-truth", {'
    assert source.count(marker)==1
    at=source.index(marker);start=source.rfind('    const bool triggerCrossed =',0,at)
    end=source.index('    if (!publishUi || !g_view) return;',at)
    region=source[start:end]
    assert start>=0 and region.count(marker)==1 and region.count('appendNativeLifecycleLog("gamepad-xinput-input", {')==1
    return region


def domain_truth_block(source):
    marker='appendNativeLifecycleLog("domain-pad-truth", {'
    assert source.count(marker)==1
    at=source.index(marker);start=source.rfind('        {',0,at)
    block=extract(source[start:],'        {')
    assert block.count(marker)==1 and 'static bool lastInit = false;' in block
    return block


def run_native_sink_failure_case(args):
    if args.native_sink_case=='intent-preflight':return run_intent_observation_preflight_case(args)
    if args.native_sink_case=='deferred-post-witness':return run_deferred_post_witness_case(args)
    output=args.output_dir.resolve();output.mkdir(parents=True,exist_ok=True)
    run=output/('native-sink-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'-'+str(os.getpid()));run.mkdir()
    def put(p,b):
        with p.open('xb') as stream:stream.write(b.encode('utf8') if isinstance(b,str) else b)
    def save(p,value):put(p,json.dumps(value,ensure_ascii=False,indent=2)+'\n')
    def pin(p):
        b=p.read_bytes();return dict(path=str(p.resolve()),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
    sourcePath=args.source.resolve() if args.source else ROOT/'native/main.cpp'
    templatePath=ROOT/'tools/fixtures/log_frequency_native_failure.cpp.in'
    source=sourcePath.read_text(encoding='utf-8-sig').replace('\r\n','\n');template=templatePath.read_text(encoding='utf-8-sig').replace('\r\n','\n')
    signatures=['static void flushNativeLifecycleLogs(bool closeDetail) {','static bool nativeLifecycleEventIsBase(const char* event) {',
        'static bool nativeLifecycleRecordIsStreaming(const char* event, const json& detail) {',
        'static void appendNativeLifecyclePrepared(const char* event,const json& detail,bool baseEvent,const std::string* encoded) {',
        'static void appendNativeLifecycleLog(const char* event, const json& observed) {',
        'static void logScreenTouchpadObservation(const char* site, const std::string& persona, const json& state) noexcept {']
    if args.native_sink_case=='sensor-retirement':signatures=['static bool domainLogEnabled(const char* domain) {','static void mirrorLifecycleToDomains(const char* event, const json& detail) {']+signatures
    functions='\n\n'.join(extract(source,signature) for signature in signatures)
    exportStart='        availability["nativeLogFailureEvidence"]=[]()->json {'
    start=source.index(exportStart);end=source.index('        }();',start)+len('        }();')
    export=source[start:end]
    if template.count('// ACTUAL_FUNCTIONS')!=1 or template.count('// ACTUAL_EXPORT')!=1:raise ValueError('native sink fixture token changed')
    code=template.replace('// ACTUAL_FUNCTIONS',functions,1).replace('// ACTUAL_EXPORT',export,1)
    if args.native_sink_case=='msi-feedback' and args.msi_feedback_phase=='classification':
        msiTemplate=ROOT/'tools/fixtures/log_frequency_msi_classifier.inc'
        code=code[:code.index('int wmain(')]+msiTemplate.read_text(encoding='utf-8-sig').replace('\r\n','\n')
    elif args.native_sink_case=='msi-feedback':
        msiTemplate=ROOT/'tools/fixtures/log_frequency_msi_failure.inc'
        sourceMsiParts=[extract(source,s) for s in ('struct MsiClawHidDiag {','struct MsiDinputFeedbackResult {','static MsiDinputFeedbackResult msiDinputFeedbackWrite(BYTE smallMotor, BYTE largeMotor,')]
        msiBody=sourceMsiParts[2];needle='    if(logDetail)try{result.interfacePath=record.interfacePath;'
        assert msiBody.count(needle)==1
        msiBody=msiBody.replace(needle,'    if(logDetail)try{msiFailurePreparationFaultPoint();result.interfacePath=record.interfacePath;',1)
        templateMsi=msiTemplate.read_text(encoding='utf-8-sig').replace('\r\n','\n')
        for token,part in zip(('@@MSI_DIAG@@','@@MSI_RESULT@@','@@MSI_WRITE@@'),sourceMsiParts[:2]+[msiBody]):
            assert templateMsi.count(token)==1;templateMsi=templateMsi.replace(token,part,1)
        code=code[:code.index('int wmain(')]+templateMsi
    if args.native_sink_case=='touch-haptic-preparation':
        name='log_frequency_touch_haptic_queue_clear.inc' if args.haptic_preparation_phase=='queue-clear' else 'log_frequency_touch_haptic_preparation.inc'
        selected=(ROOT/'tools/fixtures'/name).read_text(encoding='utf-8-sig').replace('\r\n','\n')
        original=extract(source,'static void nativeTouchHapticSummaryEnd() {')
        probes=[('if(g_touchHapticSummaryTestRows)','sourceBeforeOfferProbe();if(g_touchHapticSummaryTestRows)'),
            ('else appendNativeLifecycleLog("gamepad-touch-haptics-summary",std::move(row));','else appendNativeLifecycleLog("gamepad-touch-haptics-summary",std::move(row));sourceAfterOfferProbe();')]
        measured=original
        for before,after in probes:
            if measured.count(before)!=1:raise ValueError('haptic preparation source probe changed')
            measured=measured.replace(before,after,1)
        inverse=measured
        for before,after in reversed(probes):inverse=inverse.replace(after,before,1)
        if inverse!=original:raise ValueError('haptic source probes do not reverse')
        exportStart=source.index('        try {\n            availability["touchHapticSummaryPreparationEvidence"]')
        exportEnd=source.index('        availability["nativeIpcFailureEvidence"]',exportStart)
        limit=next(line for line in source.splitlines() if line.startswith('static constexpr size_t GAMEPAD_SERIAL_QUEUE_LIMIT ='))
        values={'@@ACTUAL_KIND@@':extract(source,'enum class SerialJobKind {')+';',
            '@@ACTUAL_LIMIT@@':limit,'@@ACTUAL_SUBMIT@@':extract(source,'static bool gamepadSerialSubmit(std::function<void()> job,'),
            '@@ACTUAL_SOURCE@@':measured,'@@ACTUAL_SOURCE_EXPORT@@':source[exportStart:exportEnd]}
        if args.haptic_preparation_phase=='queue-clear':
            worker=extract(source,'static bool gamepadSerialStartLocked() {')
            closing='if(closing){g_gamepadSerialQ.clear();nativeTouchHapticSummaryQueueCleared();}'
            if worker.count(closing)!=1:raise ValueError('original worker closing clear changed')
            claim=extract(worker,'if(!g_exitRequested.compare_exchange_strong(exiting,false,std::memory_order_acq_rel,std::memory_order_acquire))')
            values.update({'@@ACTUAL_QUEUE_CLEAR_HELPER@@':extract(source,'static void nativeTouchHapticSummaryQueueCleared() noexcept {'),
                '@@ACTUAL_STOP@@':extract(source,'static bool gamepadSerialStop(DWORD timeoutMs = INFINITE) {'),
                '@@ACTUAL_CLOSING_BLOCK@@':closing,'@@ACTUAL_CLAIM_REFUSAL@@':'bool exiting=false;\n'+claim})
        for token,value in values.items():
            if selected.count(token)!=1:raise ValueError('haptic source fixture token changed')
            selected=selected.replace(token,value,1)
        code=code[:code.index('int wmain(')]+selected
    if args.native_sink_case=='touch-haptic-lanes':
        selected=(ROOT/'tools/fixtures/log_frequency_touch_haptic_lanes.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
        original=extract(source,'static void nativeTouchHapticSummaryEnd() {')
        if selected.count('@@ACTUAL_SUMMARY@@')!=1:raise ValueError('original haptic summary token changed')
        code=code[:code.index('int wmain(')]+selected.replace('@@ACTUAL_SUMMARY@@',original,1)
    if args.native_sink_case=='classifier-text':
        if args.classifier_phase=='command-result':
            selected=(ROOT/'tools/fixtures/log_frequency_command_boolean.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
            if selected.count('@@ACTUAL_COMMAND_WRAPPER@@')!=1:raise ValueError('command logger fixture token changed')
            code=code[:code.index('int wmain(')]+selected.replace('@@ACTUAL_COMMAND_WRAPPER@@',extract(source,'static void inputHostCommandResultLog(const json& row) noexcept {'),1)
        else:code=code[:code.index('int wmain(')]+(ROOT/'tools/fixtures/log_frequency_classifier_text.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
    if args.native_sink_case=='common-frame-recovery':
        selected=(ROOT/'tools/fixtures/log_frequency_common_frame_recovery.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
        producer=extract(source,'static void inputCaptureCommonFrameEvidence(const json& hostFrame,bool semanticNeutral) noexcept {')
        if selected.count('@@PRODUCER@@')!=1:raise ValueError('common frame source fixture token changed')
        code=code[:code.index('int wmain(')]+selected.replace('@@PRODUCER@@',producer,1)
    if args.native_sink_case=='quarantine-source':
        selected=(ROOT/'tools/fixtures/log_frequency_quarantine_source_recovery.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
        declarations=extract(source,'struct InputHostQuarantineLogState : ymcc::logging::FrameEvidenceLogState {')+';\n'
        for name in ('g_inputHostQuarantineBusLogState','g_inputHostQuarantineLifecycleLogState','g_inputHostQuarantineHeldLogState'):
            line='static InputHostQuarantineLogState '+name+';'
            if source.count(line)!=1:raise ValueError('quarantine source declaration changed')
            declarations+=line+'\n'
        producer='template<class Build>\n'+extract(source,'static void inputHostLogQuarantineObservationLocked(')
        if selected.count('@@ACTUAL_SOURCE@@')!=1:raise ValueError('quarantine source fixture token changed')
        code=code[:code.index('int wmain(')]+selected.replace('@@ACTUAL_SOURCE@@',declarations+producer,1)
    if args.native_sink_case=='retained-session':
        selected=(ROOT/'tools/fixtures/log_frequency_retained_session_recovery.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
        leaves=(ROOT/'tools/input_pipe_retirement_memory_selftest.cpp').read_text(encoding='utf-8-sig').replace('\r\n','\n')
        if leaves.count('using json=nlohmann::json;')!=1:raise ValueError('original memory leaf prefix changed')
        leaves=leaves[:leaves.index('using json=nlohmann::json;')]
        suite=(ROOT/'tools/fixtures/input_host_clear_retirement/suite.cpp.in').read_text(encoding='utf-8-sig').replace('\r\n','\n')
        ports=suite[suite.index('struct Ports {'):suite.index('@@PIPE_CLOSE@@')]
        for signature in ('static void appendNativeLifecycleLog(','static bool domainLogEnabled('):
            original=extract(ports,signature)
            if ports.count(original)!=1:raise ValueError('original Clear port function changed')
            ports=ports.replace(original,'',1)
        power='static std::atomic<unsigned long long> g_powerGeneration{0};'
        if ports.count(power)!=1:raise ValueError('original Clear power port changed')
        ports='namespace tr=ymcc::inputHost::pipeTransport;\nusing InputHostMutex=std::mutex;static InputHostMutex g_inputHostMx;\n'+ports.replace(power,'',1)
        declarations=extract(source,'struct InputHostQuarantineLogState : ymcc::logging::FrameEvidenceLogState {')+';\n'
        for name in ('g_inputHostQuarantineBusLogState','g_inputHostQuarantineLifecycleLogState','g_inputHostQuarantineHeldLogState'):
            line='static InputHostQuarantineLogState '+name+';'
            if source.count(line)!=1:raise ValueError('original quarantine source declaration changed')
            declarations+=line+'\n'
        values={'@@ACTUAL_MEMORY_LEAVES@@':leaves,'@@ORIGINAL_CLEAR_PORTS@@':ports,'@@ACTUAL_QUARANTINE_DECLARATIONS@@':declarations,
            '@@ACTUAL_PIPE_CLOSE@@':extract(source,'static void inputHostPipeCloseOwned(HANDLE pipe) noexcept {'),
            '@@ACTUAL_PIPE_WRAPPER@@':'static HANDLE g_inputHostUnadoptedPipe = nullptr;\n'+extract(source,'static bool inputHostPipeRetirementProvenLocked() noexcept {'),
            '@@ACTUAL_PRIMARY_HELPER@@':extract(source,'static bool inputHostPrimaryThreadRetirementProvenLocked(const char* phase) noexcept {'),
            '@@ACTUAL_CLEAR@@':extract(source,'static void inputHostClearLocked() {')}
        for token,value in values.items():
            if selected.count(token)!=1:raise ValueError('retained-session source fixture token changed')
            selected=selected.replace(token,value,1)
        code=code[:code.index('int wmain(')]+selected
    if args.native_sink_case=='ui-output-recovery':
        if getattr(args,'ui_output_recovery_phase',None)=='connection-truth':
            selected=(ROOT/'tools/fixtures/log_frequency_connection_truth_recovery.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
            if selected.count('@@DOMAIN_TRUTH_REGION@@')!=1:raise ValueError('connection truth producer token changed')
            code=code[:code.index('int wmain(')]+selected.replace('@@DOMAIN_TRUTH_REGION@@',domain_truth_observation_region(source),1)
        else:
            runtime=(ROOT/'native/virtual_controller_ui_output_runtime.h').read_text(encoding='utf-8-sig').replace('\r\n','\n')
            selected=(ROOT/'tools/fixtures/log_frequency_ui_output_recovery.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
            producerSignatures=('static void dsuUiOutputNote(','static void virtualUiOutputNote(','static void steamDeckUiOutputNote(','static void msiUiOutputNote(','static void ps5UiOutputNote(')
            producer='\n\n'.join(extract(runtime,sig) for sig in producerSignatures)
            helper=extract(runtime,'struct UiOutputLogRecoveryMemo {')+';'
            if selected.count('@@PRODUCERS@@')!=1:raise ValueError('UI output producer token changed')
            code=code[:code.index('int wmain(')]+selected.replace('@@PRODUCERS@@',helper+'\n'+producer,1)
    if args.native_sink_case=='foreground-wait':
        selected=(ROOT/'tools/fixtures/log_frequency_foreground_wait.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
        replayBytes=args.foreground_wait_replay.read_bytes()
        if len(replayBytes)>2*1024*1024:raise ValueError('foreground replay exceeds bounded input')
        replay=json.loads(replayBytes)
        if not isinstance(replay,dict) or not isinstance(replay.get('rows'),list) or len(replay['rows'])>512:raise ValueError('foreground replay row contract')
        data=json.dumps(replay,ensure_ascii=False)
        if ')YMCC_WAIT_FIELDS"' in data or selected.count('@@FIELD_DECLARATION@@')!=1:raise ValueError('foreground replay literal boundary')
        chunks=[data[i:i+3000] for i in range(0,len(data),3000)]
        declaration='static std::string foregroundReplay(){std::string value;value.reserve('+str(len(data.encode('utf8')))+');\n'+''.join('value.append(R"YMCC_WAIT_FIELDS('+chunk+')YMCC_WAIT_FIELDS");\n' for chunk in chunks)+'return value;}\n'
        code=code[:code.index('int wmain(')]+selected.replace('@@FIELD_DECLARATION@@',declaration,1)
    if args.native_sink_case=='power-leaf-failures':
        selected=(ROOT/'tools/fixtures/log_frequency_power_leaf_failures.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
        resume=extract(source,'static void handlePowerResumeNotification(SgWork work, const char* reason) {')
        broadcast=source[source.index('    case kSleepPowerForwardMessage:'):source.index('    case kSleepPowerForwardMessage:')+24000]
        values={'@@ACTUAL_SGWORK@@':extract(source,'enum class SgWork : uint8_t {')+';',
            '@@ACTUAL_POWER_GETTER@@':extract(source,'static unsigned long long currentPowerGeneration() {'),
            '@@ACTUAL_RESUME_GUARD@@':extract(resume,'const auto consume=[&](const char* name,auto&& call) noexcept {')+';',
            '@@ACTUAL_BROADCAST_GUARD@@':extract(broadcast,'const auto powerConsumer=[&](const char* name,auto&& consume) noexcept {')+';'}
        for token,value in values.items():
            if selected.count(token)!=1:raise ValueError('original power consumer guard fixture token changed')
            selected=selected.replace(token,value,1)
        code=code[:code.index('int wmain(')]+selected
    if args.native_sink_case=='startup-finite' and getattr(args,'startup_finite_phase',None)=='calibration-restore':
        selected=(ROOT/'tools/fixtures/log_frequency_calibration_restore.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
        for token,signature in (('@@ACTUAL_FRAME_CURRENT@@','static bool inputCaptureWindowsCalibrationFrameCurrent(const ymcc::windows_motion::Stamp& sample) {'),
            ('@@ACTUAL_NOTE_APPLIED@@','static void inputCaptureReviewedCalibrationNoteApplied(const std::string& deviceId,int weight) {'),
            ('@@ACTUAL_INVALIDATE@@','static void inputCaptureExtendedCalibrationInvalidateOnce(const InputCaptureSensorMeta& meta) {'),
            ('@@ACTUAL_RESTORE@@','static void inputCaptureReviewedCalibrationRestoreOnce(const ymcc::hc_model_motion::Recipe& recipe) {')):
            if selected.count(token)!=1:raise ValueError('original calibration fixture token changed:'+token)
            selected=selected.replace(token,extract(source,signature),1)
        code=code[:code.index('int wmain(')]+selected
    elif args.native_sink_case=='startup-finite' and getattr(args,'startup_finite_phase',None)=='lifecycle-outcomes':
        selected=(ROOT/'tools/fixtures/log_frequency_lifecycle_outcomes.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
        calls=[]
        for index,event in enumerate(('gyro-power-early-pause-completed','gyro-power-start-current','fxsound-toggle','settings-upgrade-repaired')):
            needle='appendNativeLifecycleLog("'+event+'",'
            if source.count(needle)!=1:raise ValueError('original lifecycle receipt ambiguous:'+event)
            if event=='settings-upgrade-repaired':
                call=needle+' recovery);'
                if source.count(call)!=1:raise ValueError('original settings receipt statement changed')
            else:
                call=extract(source,needle);end=source.index(call)+len(call)
                if not re.match(r'\s*\);',source[end:]):raise ValueError('original lifecycle receipt boundary changed:'+event)
                call+=');'
            calls.append('    if(index=='+str(index)+'){'+call+'return;}')
        if selected.count('@@PRODUCERS@@')!=1:raise ValueError('lifecycle receipt fixture token changed')
        code=code[:code.index('int wmain(')]+selected.replace('@@PRODUCERS@@','\n'.join(calls),1)
    elif args.native_sink_case=='startup-finite' and getattr(args,'startup_finite_phase',None)=='feedback-stop':
        selected=(ROOT/'tools/fixtures/log_frequency_feedback_stop.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
        if selected.count('@@ACTUAL_FINISH@@')!=1:raise ValueError('original feedback finish token changed')
        selected=selected.replace('@@ACTUAL_FINISH@@',extract(source,'static void physicalFeedbackFinish(unsigned long long generation) {'),1)
        code=code[:code.index('int wmain(')]+selected
    elif args.native_sink_case=='startup-finite':
        selected=(ROOT/'tools/fixtures/log_frequency_startup_finite.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
        overlay=(ROOT/'native/steam_overlay_startup.h').read_text(encoding='utf-8-sig').replace('\r\n','\n')
        redaction='    auto diagnostic=result;diagnostic.erase("targetPath");diagnostic.erase("targetAccount");\n    appendNativeLifecycleLog("steam-overlay-startup",diagnostic);'
        if overlay.count(redaction)!=1:raise ValueError('original Steam startup redaction changed')
        calls=['    if(index==0){'+redaction+'return;}']
        for index,event in enumerate(('process-efficiency-startup','input-module-replay-unverified','hwinfo-recovery-result-discarded','controller-motion-raw-registration'),1):
            needle='appendNativeLifecycleLog("'+event+'",'
            if source.count(needle)!=1:raise ValueError('original startup producer ambiguous:'+event)
            call=extract(source,needle);end=source.index(call)+len(call)
            if not re.match(r'\s*\);',source[end:]):raise ValueError('original startup initializer boundary changed:'+event)
            calls.append('    if(index=='+str(index)+'){'+call+');return;}')
        if selected.count('@@PRODUCERS@@')!=1:raise ValueError('startup finite fixture token changed')
        code=code[:code.index('int wmain(')]+selected.replace('@@PRODUCERS@@','\n'.join(calls),1)
    if args.native_sink_case=='msi-return-debt':
        selected=(ROOT/'tools/fixtures/log_frequency_msi_return_debt.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
        beginning=source.index('static bool msiDinputTryDischargeReturnDebt(')
        opening=source.index(') {',beginning)+2
        producer=extract(source[opening:],'{')
        needle='appendNativeLifecycleLog("msi-claw-return-debt",'
        if producer.count(needle)!=2:raise ValueError('MSI return debt original producer count changed')
        first=extract(producer,needle);remaining=producer[producer.index(first)+len(first):];second=extract(remaining,needle)
        parts=(extract(source,'enum class MsiReturnDebtBasis {'),extract(source,'static const char* msiReturnDebtBasisName('),first+');',second+');')
        for token,part in zip(('@@BASIS_ENUM@@','@@BASIS_NAME@@','@@RETAINED@@','@@DISCHARGED@@'),parts):
            if selected.count(token)!=1:raise ValueError('MSI return debt fixture token changed:'+token)
            selected=selected.replace(token,part,1)
        code=code[:code.index('int wmain(')]+selected
    if args.native_sink_case=='shortcut-outcomes':
        runtime=(ROOT/'native/shortcut_action_runtime.h').read_text(encoding='utf-8-sig').replace('\r\n','\n')
        selected=(ROOT/'tools/fixtures/log_frequency_shortcut_outcomes.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
        needles=('appendNativeLifecycleLog("shortcut-action-request",{{"actionId",action}',
            'appendNativeLifecycleLog("shortcut-action-request",{{"id",request["id"]}',
            'appendNativeLifecycleLog("shortcut-action-result",feedback);')
        for token,needle in zip(('@@REQUEST_REFUSED@@','@@REQUEST_ISSUED@@','@@RESULT@@'),needles):
            if runtime.count(needle)!=1 or selected.count(token)!=1:raise ValueError('shortcut original producer changed:'+token)
            if token=='@@RESULT@@':call=needle
            else:
                call=extract(runtime,needle);end=runtime.index(call)+len(call)
                if not re.match(r'\s*\);',runtime[end:]):raise ValueError('shortcut initializer boundary changed:'+token)
                call+=');'
            selected=selected.replace(token,call,1)
        code=code[:code.index('int wmain(')]+selected
    if args.native_sink_case=='finite-terminals':
        events=('onex-back-write-late','onex-back-write-stuck','onex-back-read-stuck','onex-back-read-late','onex-back-read-fault','settings-reset-refresh-warning','settings-write-refresh-warning')
        producers=[]
        for index,event in enumerate(events):
            needle='appendNativeLifecycleLog("'+event+'",'
            if source.count(needle)!=1:raise ValueError('finite terminal producer missing or ambiguous:'+event)
            call=extract(source,needle);end=source.index(call)+len(call)
            if not re.match(r'\s*\);',source[end:]):raise ValueError('finite terminal initializer boundary changed:'+event)
            producers.append('    if(index=='+str(index)+'){'+call+');return;}')
        selected=(ROOT/'tools/fixtures/log_frequency_finite_terminals.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
        if selected.count('@@PRODUCERS@@')!=1:raise ValueError('finite terminal fixture token changed')
        dynamicCalls=[]
        for selector in ('stopped','recovered'):
            needle='appendNativeLifecycleLog('+selector
            if source.count(needle)!=1:raise ValueError('startup dynamic producer missing or ambiguous:'+selector)
            call=extract(source,needle);end=source.index(call)+len(call)
            if not re.match(r'\s*\);',source[end:]):raise ValueError('startup dynamic producer boundary changed:'+selector)
            dynamicCalls.append(call+');')
        for token,call in zip(('@@STARTUP_STOPPED@@','@@STARTUP_RECOVERED@@'),dynamicCalls):
            if selected.count(token)!=1:raise ValueError('startup dynamic fixture token changed:'+token)
            selected=selected.replace(token,call,1)
        recoveryWrapper=extract(source,'static void rogRemapLog(const char* event, json detail) {')
        if selected.count('@@ROG_LOG_WRAPPER@@')!=1:raise ValueError('ROG recovery wrapper fixture token changed')
        selected=selected.replace('@@ROG_LOG_WRAPPER@@',recoveryWrapper,1)
        recoveryCalls=[]
        for index,event in enumerate(('rog.recover-bind-deferred','rog.recover-bind-executed','rog.recover-bind-dropped','rog.recover-bind-consumed')):
            needle='rogRemapLog("'+event+'",'
            if source.count(needle)!=1:raise ValueError('ROG recovery outcome missing or ambiguous:'+event)
            call=extract(source,needle);end=source.index(call)+len(call)
            if not re.match(r'\s*\);',source[end:]):raise ValueError('ROG recovery outcome initializer changed:'+event)
            recoveryCalls.append('    if(index=='+str(index)+'){'+call+');return;}')
        if selected.count('@@ROG_RECOVERY_CALLS@@')!=1:raise ValueError('ROG recovery initializer fixture token changed')
        selected=selected.replace('@@ROG_RECOVERY_CALLS@@','\n'.join(recoveryCalls),1)
        intentGate=extract(source,'static bool msiTargetIntentEvidenceEnabled() noexcept {')
        intentFunctions='\n\n'.join(extract(source,signature) for signature in (
            'static void msiTargetIntentObserveInput(const json& saved,const json& effective,bool shortcut,bool overrideActive) noexcept {',
            'static void msiTargetIntentDocumentWrite(const json& args,const json& committed,bool ok,bool conflict,long long stale,long long expected) noexcept {'))
        for token,value in (('@@ACTUAL_INTENT_GATE@@',intentGate),('@@ACTUAL_INTENT_PRODUCERS@@',intentFunctions)):
            if selected.count(token)!=1:raise ValueError('original intent file-delivery fixture token changed:'+token)
            selected=selected.replace(token,value,1)
        code=code[:code.index('int wmain(')]+selected.replace('@@PRODUCERS@@','\n'.join(producers),1)
    if args.native_sink_case=='boundary-failures':
        code=code[:code.index('int wmain(')]+(ROOT/'tools/fixtures/log_frequency_boundary_failures.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
    if args.native_sink_case=='preselection-stages':
        projection=extract(source,'static json nativeSteamGameLinkLogDetail(const json& detail) {')
        inner=extract(projection,'            for (const auto& fields : *candidates) {')
        renamed=re.sub(r'\bfields\b','candidateFields',inner)
        if renamed==inner or projection.count(inner)!=1:raise ValueError('preselection projection shadow adapter anchor changed')
        projection=projection.replace(inner,renamed,1)
        stub='static json nativeSteamGameLinkLogDetail(const json& value){return value;}'
        if code.count(stub)!=1:raise ValueError('preselection projection fixture anchor changed')
        code=code.replace(stub,projection,1)
        code=code[:code.index('int wmain(')]+(ROOT/'tools/fixtures/log_frequency_preselection_stages.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
    if args.native_sink_case=='path-preparation':
        extra=(ROOT/'tools/fixtures/log_frequency_path_preparation.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
        ipcExport='        availability["nativeIpcFailureEvidence"]=ymcc::logging::ipcFailureLogStats();'
        if source.count(ipcExport)!=1 or extra.count('@@ACTUAL_IPC_EXPORT@@')!=1:raise ValueError('path preparation IPC stats anchor changed')
        code=code[:code.index('int wmain(')]+extra.replace('@@ACTUAL_IPC_EXPORT@@',ipcExport,1)
    if args.native_sink_case=='record-preparation':
        extra=(ROOT/'tools/fixtures/log_frequency_record_preparation.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
        ipcExport='        availability["nativeIpcFailureEvidence"]=ymcc::logging::ipcFailureLogStats();'
        if source.count(ipcExport)!=1 or extra.count('@@ACTUAL_IPC_EXPORT@@')!=1:raise ValueError('record preparation IPC stats anchor changed')
        code=code[:code.index('int wmain(')]+extra.replace('@@ACTUAL_IPC_EXPORT@@',ipcExport,1)
    if args.native_sink_case=='ipc-reoffer':
        extra=(ROOT/'tools/fixtures/log_frequency_ipc_reoffer.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
        ipcExport='        availability["nativeIpcFailureEvidence"]=ymcc::logging::ipcFailureLogStats();'
        if source.count(ipcExport)!=1 or extra.count('@@ACTUAL_IPC_EXPORT@@')!=1:raise ValueError('IPC stats export anchor changed')
        code=code[:code.index('int wmain(')]+extra.replace('@@ACTUAL_IPC_EXPORT@@',ipcExport,1)
        if getattr(args,'ipc_reoffer_phase',None)=='event-gates':code='#define YMCC_IPC_REOFFER_EVENT_GATES_ONLY 1\n'+code
    if args.native_sink_case=='diagnostic-memos':
        code=code[:code.index('int wmain(')]+(ROOT/'tools/fixtures/log_frequency_diagnostic_memo_recovery.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
    if args.native_sink_case=='snapshots':
        code=code[:code.index('int wmain(')]+(ROOT/'tools/fixtures/log_frequency_snapshot_recovery.inc').read_text(encoding='utf-8-sig').replace('\r\n','\n')
    if args.native_sink_case=='mirror':code='#define YMCC_NATIVE_FAILURE_MIRROR_ONLY 1\n'+code
    if args.native_sink_case=='flush':code='#define YMCC_NATIVE_FAILURE_FLUSH_ONLY 1\n'+code
    if args.native_sink_case=='recovery':code='#define YMCC_SCREEN_RECOVERY_ONLY 1\n'+code
    if args.native_sink_case=='preparation':code='#define YMCC_SCREEN_PREPARATION_ONLY 1\n'+code
    if args.native_sink_case=='sensor-retirement':
        code='#define YMCC_SENSOR_RETIREMENT_ONLY 1\n'+code
        guards=[]
        for relative,signature,counter in [('native/model_adaptation_log_native.inc','static void modelAdaptationObserveRecord(const char* event,const json& detail) noexcept {','modelObserverEntered'),
                ('native/virtual_adaptation_log_native.inc','static void virtualAdaptationObserveRecord(const char* event,const json& detail) noexcept {','virtualObserverEntered')]:
            observed=(ROOT/relative).read_text(encoding='utf-8-sig').replace('\r\n','\n')
            body=extract(observed,signature);guard=body[:body.index('    try {')]
            guards.append(guard+'    ++'+counter+';\n}')
        if code.count('// ACTUAL_SENSOR_OBSERVER_GUARDS')!=1:raise ValueError('sensor observer guard token changed')
        code=code.replace('// ACTUAL_SENSOR_OBSERVER_GUARDS','\n'.join(guards),1)
        retirement=extract(source,'static void inputCaptureUnsubscribeWinRtSensorsImpl() {')
        helper=extract(source,'static void logSensorRetirementFailure(const char* event,const char* sensor) noexcept {')
        if args.sensor_retirement_phase=='source-preparation':code='#define YMCC_SENSOR_SOURCE_PREPARATION_ONLY 1\n'+code
        if args.sensor_retirement_phase=='source-gate':code='#define YMCC_SENSOR_SOURCE_GATE_ONLY 1\n'+code
        if args.sensor_retirement_phase=='error-detail':code='#define YMCC_SENSOR_ERROR_DETAIL_ONLY 1\n'+code
        code=code.replace('static json exportEvidence(){',helper+'\n'+retirement+'\nstatic json exportEvidence(){',1)
    omitted='ymcc::logging::nativeLogFailureEvidence().record(baseEvent?Failure::eventOpen:Failure::detailOpen,event);'
    if code.count(omitted)!=2:raise ValueError('native sink open evidence control anchors changed')
    toolsPath=ROOT/'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'
    recipe=json.loads(toolsPath.read_text(encoding='utf-8-sig'))['native']
    env=dict(os.environ);env['PATH']=str(pathlib.Path(recipe['compiler']).parent)+';C:/Windows/System32;C:/Windows';env['INCLUDE']=';'.join(recipe['include']);env['LIB']=';'.join(recipe['lib']);env['VSLANG']='1033'
    for key in list(env):
        if key.upper() in ('CL','_CL_','LINK','_LINK_'):del env[key]
    sourcePaths=[sourcePath,templatePath,ROOT/'native/log_snapshot_state.h',ROOT/'native/log_frequency_policy.h',ROOT/'native/log_jsonl_boundary.h',ROOT/'native/sleep_fact_log_record.h',ROOT/'deps/json/json.hpp',ROOT/'tools/input_pipe_test_allocation_fault.h',toolsPath]
    if args.native_sink_case=='shortcut-outcomes':sourcePaths.extend([ROOT/'tools/fixtures/log_frequency_shortcut_outcomes.inc',ROOT/'native/shortcut_action_runtime.h',ROOT/'native/shortcut_action_protocol.h'])
    if args.native_sink_case=='msi-return-debt':sourcePaths.append(ROOT/'tools/fixtures/log_frequency_msi_return_debt.inc')
    if args.native_sink_case=='power-leaf-failures':sourcePaths.extend([ROOT/'tools/fixtures/log_frequency_power_leaf_failures.inc',ROOT/'native/log_ai_index.h',ROOT/'native/log_export_evidence.h',ROOT/'native/buffered_domain_log.h'])
    if args.native_sink_case=='startup-finite' and getattr(args,'startup_finite_phase',None)=='calibration-restore':sourcePaths.extend([ROOT/'tools/fixtures/log_frequency_calibration_restore.inc',ROOT/'native/hc_model_motion_catalog.h',ROOT/'native/reviewed_gyro_calibration.h',ROOT/'native/windows_motion_submission.h'])
    elif args.native_sink_case=='startup-finite' and getattr(args,'startup_finite_phase',None)=='lifecycle-outcomes':sourcePaths.append(ROOT/'tools/fixtures/log_frequency_lifecycle_outcomes.inc')
    elif args.native_sink_case=='startup-finite':sourcePaths.extend([ROOT/'tools/fixtures/log_frequency_feedback_stop.inc',ROOT/'native/xinput_identity_refresh_policy.h'] if getattr(args,'startup_finite_phase',None)=='feedback-stop' else [ROOT/'tools/fixtures/log_frequency_startup_finite.inc',ROOT/'native/steam_overlay_startup.h'])
    if args.native_sink_case=='common-frame-recovery':sourcePaths.extend([ROOT/'tools/fixtures/log_frequency_common_frame_recovery.inc',ROOT/'native/log_ai_index.h',ROOT/'native/log_export_evidence.h',ROOT/'native/common_frame_log_scope.h'])
    if args.native_sink_case=='quarantine-source':sourcePaths.extend([ROOT/'tools/fixtures/log_frequency_quarantine_source_recovery.inc',ROOT/'native/log_ai_index.h',ROOT/'native/log_export_evidence.h'])
    if args.native_sink_case=='retained-session':sourcePaths.extend([ROOT/'tools/fixtures/log_frequency_retained_session_recovery.inc',ROOT/'tools/input_pipe_retirement_memory_selftest.cpp',ROOT/'tools/fixtures/input_host_clear_retirement/suite.cpp.in',ROOT/'native/input_host_shared_power.h',ROOT/'native/sleep_power_observation.h',ROOT/'native/sleep_power_revocation.h',ROOT/'native/sleep_power_state.h',ROOT/'native/input_host_pipe_transport.h',ROOT/'native/input_host_process_retirement.h',ROOT/'native/log_ai_index.h',ROOT/'native/log_export_evidence.h'])
    if args.native_sink_case=='ui-output-recovery':sourcePaths.extend([ROOT/'tools/fixtures/log_frequency_connection_truth_recovery.inc'] if getattr(args,'ui_output_recovery_phase',None)=='connection-truth' else [ROOT/'native/virtual_controller_ui_output_runtime.h',ROOT/'tools/fixtures/log_frequency_ui_output_recovery.inc'])
    if args.native_sink_case=='foreground-wait':sourcePaths.extend([ROOT/'tools/fixtures/log_frequency_foreground_wait.inc',ROOT/'native/msi_native_foreground_isolation_runtime.h',args.foreground_wait_replay])
    if args.native_sink_case=='touch-haptic-preparation':sourcePaths.extend([ROOT/'tools/fixtures'/('log_frequency_touch_haptic_queue_clear.inc' if args.haptic_preparation_phase=='queue-clear' else 'log_frequency_touch_haptic_preparation.inc'),ROOT/'native/touch_haptic_dispatch.h'])
    if args.native_sink_case=='touch-haptic-lanes':sourcePaths.extend([ROOT/'tools/fixtures/log_frequency_touch_haptic_lanes.inc',ROOT/'native/touch_haptic_dispatch.h',ROOT/'native/log_ai_index.h',ROOT/'native/log_export_evidence.h',ROOT/'native/buffered_domain_log.h'])
    if args.native_sink_case=='classifier-text':sourcePaths.append(ROOT/'tools/fixtures'/('log_frequency_command_boolean.inc' if args.classifier_phase=='command-result' else 'log_frequency_classifier_text.inc'))
    if args.native_sink_case=='finite-terminals':sourcePaths.extend([ROOT/'tools/fixtures/log_frequency_finite_terminals.inc',ROOT/'native/msi_target_intent_evidence.h'])
    if args.native_sink_case=='boundary-failures':sourcePaths.append(ROOT/'tools/fixtures/log_frequency_boundary_failures.inc')
    if args.native_sink_case=='preselection-stages':sourcePaths.append(ROOT/'tools/fixtures/log_frequency_preselection_stages.inc')
    if args.native_sink_case=='path-preparation':sourcePaths.append(ROOT/'tools/fixtures/log_frequency_path_preparation.inc')
    if args.native_sink_case=='record-preparation':sourcePaths.append(ROOT/'tools/fixtures/log_frequency_record_preparation.inc')
    if args.native_sink_case=='ipc-reoffer':sourcePaths.append(ROOT/'tools/fixtures/log_frequency_ipc_reoffer.inc')
    if args.native_sink_case=='diagnostic-memos':sourcePaths.append(ROOT/'tools/fixtures/log_frequency_diagnostic_memo_recovery.inc')
    if args.native_sink_case=='snapshots':sourcePaths.append(ROOT/'tools/fixtures/log_frequency_snapshot_recovery.inc')
    if args.native_sink_case=='sensor-retirement':sourcePaths.extend([ROOT/'native/model_adaptation_log_native.inc',ROOT/'native/virtual_adaptation_log_native.inc'])
    if args.native_sink_case=='msi-feedback':sourcePaths.extend([msiTemplate]+([] if args.msi_feedback_phase=='classification' else [ROOT/'native/touch_feedback_lease.h',ROOT/'native/touch_haptic_dispatch.h']))
    pins=[pin(p) for p in sourcePaths];tools=[pin(pathlib.Path(recipe[k])) for k in ('compiler','linker')];results=[]
    variants=[('current',code),('missing-open-evidence','#define YMCC_NATIVE_FAILURE_PREFIX_ONLY 1\n'+code.replace(omitted,'/* necessary missing-open-evidence control */',1))]
    if args.native_sink_before:sourcePaths.append(args.native_sink_before.resolve());pins=[pin(p) for p in sourcePaths]
    if args.native_sink_case=='sensor-retirement':
        variants=[('current-sensor-retirement',code)]
        if args.native_sink_before:
            before=args.native_sink_before.read_text(encoding='utf-8-sig').replace('\r\n','\n')
            old=code
            for signature in signatures:
                old=old.replace(extract(source,signature),extract(before,signature),1)
            old=old.replace(retirement,extract(before,'static void inputCaptureUnsubscribeWinRtSensorsImpl() {'),1)
            variants.insert(0,('old-sensor-source','#define YMCC_SENSOR_OLD_PREFIX_ONLY 1\n'+old))
        missing=re.sub(r'ymcc::logging::sensorRetirementLogState\(\)\.succeeded\([^;]+;', '', retirement)
        if missing==retirement:raise ValueError('sensor success control hook missing')
        variants.append(('missing-sensor-success','#define YMCC_SENSOR_SUCCESS_PREFIX_ONLY 1\n'+code.replace(retirement,missing,1)))
    if args.sensor_retirement_phase=='source-preparation':
        marker='ymcc::logging::nativeLogFailureEvidence().record(ymcc::logging::NativeLogFailureEvidence::preparationException,event);'
        if helper.count(marker)!=1:raise ValueError('source preparation helper arm anchor missing')
        variants=[('current-sensor-source-preparation',code),('missing-sensor-source-preparation','#define YMCC_SENSOR_SOURCE_PREP_PREFIX_ONLY 1\n'+code.replace(helper,helper.replace(marker,''),1))]
    if args.sensor_retirement_phase=='error-detail':
        marker='return !state.retain(slot,g_powerGeneration.load(std::memory_order_acquire),error.kind,error.code);'
        if code.count(marker)!=1:raise ValueError('sensor exception key control anchor missing')
        variants=[('current-sensor-error-detail',code),
            ('missing-sensor-error-key','#define YMCC_SENSOR_ERROR_KEY_PREFIX_ONLY 1\n'+code.replace(marker,'return !state.retain(slot,g_powerGeneration.load(std::memory_order_acquire));',1))]
        if args.native_sink_before:
            before=args.native_sink_before.read_text(encoding='utf-8-sig').replace('\r\n','\n')
            variants.insert(0,('old-sensor-error-detail','#define YMCC_SENSOR_ERROR_OLD_PREFIX_ONLY 1\n'+code.replace(retirement,extract(before,'static void inputCaptureUnsubscribeWinRtSensorsImpl() {'),1)))
    if args.sensor_retirement_phase=='source-gate':
        variants=[('current-sensor-source-gate',code)]
        if args.native_sink_before:
            before=args.native_sink_before.read_text(encoding='utf-8-sig').replace('\r\n','\n')
            oldHelper=extract(before,'static void logSensorRetirementFailure(const char* event,const char* sensor) noexcept {')
            variants.insert(0,('old-sensor-source-gate','#define YMCC_SENSOR_SOURCE_GATE_OLD_PREFIX_ONLY 1\n'+code.replace(helper,oldHelper,1)))
    if args.native_sink_case=='mirror':
        marker='if(!fileExitCompleted)ymcc::logging::nativeLogFailureEvidence().record('
        if code.count(marker)!=1:raise ValueError('mirror exception scope guard missing')
        variants=[('current-mirror',code),('missing-mirror-scope',code.replace(marker,'ymcc::logging::nativeLogFailureEvidence().record(',1))]
    if args.native_sink_case=='flush':
        flushMarker='ymcc::logging::nativeLogFailureEvidence().record(Failure::eventFlush,nullptr);'
        closeMarker='ymcc::logging::nativeLogFailureEvidence().recordBoundary(Failure::closeFailure);'
        if code.count(flushMarker)!=1 or code.count(closeMarker)!=1:raise ValueError('flush/close control anchor changed')
        variants=[('current-flush',code),
            ('missing-explicit-flush-evidence','#define YMCC_NATIVE_FLUSH_PREFIX_ONLY 1\n'+code.replace(flushMarker,'(void)0; /* missing explicit flush evidence control */',1)),
            ('missing-close-evidence','#define YMCC_NATIVE_CLOSE_CONTROL 1\n'+code.replace(closeMarker,'(void)0; /* missing close evidence control */',1))]
    if args.native_sink_case=='recovery':
        producer=extract(source,'static void logScreenTouchpadObservation(const char* site, const std::string& persona, const json& state) noexcept {')
        oldProducer=producer
        producerEdits=[('unsigned long long generation=0;bool known=false;', 'unsigned long long generation=0,deliveryEpoch=0;bool known=false;'), ('            auto& last=previous[ymcc::logging::screenObservationSlot(persona)];', '            const auto deliveryEpoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();\n            auto& last=previous[ymcc::logging::screenObservationSlot(persona)];'), ('if(last.known && generation==last.generation && persona==last.persona &&', 'if(last.known && generation==last.generation && deliveryEpoch==last.deliveryEpoch && persona==last.persona &&'), ('last.generation=generation;last.known=true;', 'last.generation=generation;last.deliveryEpoch=deliveryEpoch;last.known=true;')]
        for before,after in reversed(producerEdits):
            if oldProducer.count(after)!=1:raise ValueError('source recovery control anchor changed')
            oldProducer=oldProducer.replace(after,before,1)
        classifier=extract(source,'static bool nativeLifecycleRecordIsStreaming(const char* event, const json& detail) {')
        screenReset='screenState.known='
        personaReset='        for(auto& memo:screenPersonaStates)memo.known=false;\n'
        if classifier.count(screenReset)!=1 or classifier.count(personaReset)!=1:raise ValueError('classifier recovery control anchor changed')
        oldClassifier=classifier.replace(screenReset,'',1).replace(personaReset,'',1)
        variants=[('current-recovery',code),
            ('old-screen-source-memo','#define YMCC_SCREEN_RECOVERY_PREFIX_ONLY 1\n'+code.replace(producer,oldProducer,1)),
            ('old-screen-classifier-memo','#define YMCC_SCREEN_RECOVERY_PREFIX_ONLY 1\n'+code.replace(classifier,oldClassifier,1))]
    if args.native_sink_case=='preparation':
        producer=extract(source,'static void logScreenTouchpadObservation(const char* site, const std::string& persona, const json& state) noexcept {')
        currentCatch='    } catch (...) {ymcc::logging::nativeLogFailureEvidence().record(ymcc::logging::NativeLogFailureEvidence::preparationException,"screen-touchpads-state");}'
        oldCatch='    } catch (...) { /* A diagnostic allocation/sink failure cannot change the IPC result. */ }'
        guard='            if(ymcc::logging::nativeLogFailureEvidence().screenPreparationFailurePending.load(std::memory_order_acquire))return;\n'
        oldProducer=producer
        for before,after in [(currentCatch,oldCatch),(guard,'')]:
            if oldProducer.count(before)!=1:raise ValueError('screen preparation source control anchor changed')
            oldProducer=oldProducer.replace(before,after,1)
        disarm='            if(kind==preparationException&&event&&std::string_view(event)=="screen-touchpads-state")\n                screenPreparationFailurePending.store(true,std::memory_order_release);\n'
        header=(ROOT/'native/log_snapshot_state.h').read_text(encoding='utf-8-sig').replace('\r\n','\n')
        if header.count(disarm)!=1:raise ValueError('preparation arm header control changed')
        oldHeader=header.replace(disarm,'',1).replace('#pragma once\n','',1)
        # Inline one exact private header variant into the control translation
        # unit. The product header has no test conditional or private include.
        oldArmCode=code.replace('#include "log_snapshot_state.h"',oldHeader,1)
        variants=[('current-preparation',code),
            ('old-screen-preparation-source','#define YMCC_SCREEN_PREP_SOURCE_PREFIX_ONLY 1\n'+code.replace(producer,oldProducer,1)),
            ('old-screen-preparation-arm','#define YMCC_SCREEN_PREP_NATIVE_PREFIX_ONLY 1\n'+oldArmCode)]
    if args.native_sink_case=='ipc-reoffer':variants=[('current-ipc-reoffer-event-gates' if getattr(args,'ipc_reoffer_phase',None)=='event-gates' else 'current-ipc-reoffer',code)]
    if args.native_sink_case=='shortcut-outcomes':variants=[('current-shortcut-outcomes',code)]
    if args.native_sink_case=='msi-return-debt':variants=[('current-msi-return-debt',code)]
    if args.native_sink_case=='power-leaf-failures':variants=[('current-power-leaf-failures',code)]
    if args.native_sink_case=='startup-finite':variants=[('current-startup-finite',code)]
    if args.native_sink_case=='common-frame-recovery':variants=[('current-common-frame-recovery',code)]
    if args.native_sink_case=='quarantine-source':variants=[('current-quarantine-source',code)]
    if args.native_sink_case=='retained-session':variants=[('current-retained-session',code)]
    if args.native_sink_case=='ui-output-recovery':variants=[('current-ui-output-recovery',code)]
    if args.native_sink_case=='foreground-wait':variants=[('current-foreground-wait',code)]
    if args.native_sink_case=='touch-haptic-preparation':variants=[('current-touch-haptic-queue-clear' if args.haptic_preparation_phase=='queue-clear' else 'current-touch-haptic-preparation',code)]
    if args.native_sink_case=='touch-haptic-lanes':variants=[('current-touch-haptic-lanes',code)]
    if args.native_sink_case=='classifier-text':variants=[('current-classifier-text',code)]
    if args.native_sink_case=='finite-terminals':variants=[('current-finite-terminals',code)]
    if args.native_sink_case=='boundary-failures':variants=[('current-boundary-failures',code)]
    if args.native_sink_case=='preselection-stages':variants=[('current-preselection-stages',code)]
    if args.native_sink_case=='path-preparation':variants=[('current-path-preparation',code)]
    if args.native_sink_case=='record-preparation':variants=[('current-record-preparation',code)]
    if args.native_sink_case=='diagnostic-memos':variants=[('current-diagnostic-memos',code)]
    if args.native_sink_case=='snapshots':variants=[('current-snapshots',code)]
    if args.native_sink_case=='msi-feedback':
        variants=[('current-msi-feedback',code)]
        if args.native_sink_before:
            before=args.native_sink_before.read_text(encoding='utf-8-sig').replace('\r\n','\n');oldCode=code
            for signature in signatures:
                oldCode=oldCode.replace(extract(source,signature),extract(before,signature),1)
            oldMsi=extract(before,'static MsiDinputFeedbackResult msiDinputFeedbackWrite(BYTE smallMotor, BYTE largeMotor,')
            oldMsi=oldMsi.replace(needle,'    if(logDetail)try{msiFailurePreparationFaultPoint();result.interfacePath=record.interfacePath;',1)
            oldCode=oldCode.replace(msiBody,oldMsi,1)
            variants.insert(0,('old-msi-feedback','#define YMCC_MSI_OLD_PREFIX_ONLY 1\n'+oldCode))
    if args.review_existing_run:
        if args.native_sink_case!='flush':raise ValueError('native existing source reference only supports flush controls')
        original=args.review_existing_run.resolve()/'current-flush.cpp'
        if original.read_bytes()!=code.encode('utf8'):raise ValueError('positive flush fixture changed;cannot reference existing execution')
        variants=variants[1:]
    for variant,text in variants:
        cpp=run/(variant+'.cpp');exe=run/(variant+'.exe');put(cpp,text)
        command=[recipe['compiler'],'/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/W4','/WX','/wd4505','/showIncludes',
            '/I'+str(ROOT/'native'),'/I'+str(ROOT/'deps/json'),'/I'+str(ROOT/'tools'),str(cpp),'/Fo'+str(run/(variant+'.obj')),'/Fe'+str(exe),'/link','/VERBOSE:LIB','kernel32.lib','advapi32.lib','user32.lib']
        if args.native_sink_case=='sensor-retirement':command+=['runtimeobject.lib','oleaut32.lib']
        save(run/(variant+'.command.json'),command);built=subprocess.run(command,cwd=run,env=env,capture_output=True,timeout=90)
        put(run/(variant+'.compile.stdout.raw'),built.stdout);put(run/(variant+'.compile.stderr.raw'),built.stderr);save(run/(variant+'.COMPILE.json'),dict(exitCode=built.returncode))
        if built.returncode:raise RuntimeError('native sink compile failed '+variant)
        output=(built.stdout+built.stderr).decode('utf8',errors='replace')
        includes=[pathlib.Path(m[1].strip()) for line in output.splitlines() if (m:=re.match(r'^Note: including file:\s*(.*)$',line))]
        libs=[pathlib.Path(m[1]) for line in output.splitlines() if (m:=re.match(r'^\s*Searching (.+\.lib):?\s*$',line,re.I))]
        unique=lambda values:list({str(p.resolve()).lower():p for p in values}.values())
        graph=dict(source=pin(cpp),includes=[pin(p) for p in unique(includes)],libraries=[pin(p) for p in unique(libs)],executable=pin(exe));save(run/(variant+'.GRAPH.json'),graph)
        if not includes or not libs:raise ValueError('native sink dependency graph missing')
        ran=subprocess.run([str(exe),str(run/(variant+'-files'))]+(['--'+args.finite_terminal_phase] if args.native_sink_case=='finite-terminals' and args.finite_terminal_phase else []),cwd=run,env=env,capture_output=True,timeout=45 if args.sensor_retirement_phase=='source-gate' else 25)
        put(run/(variant+'.stdout.raw'),ran.stdout);put(run/(variant+'.stderr.raw'),ran.stderr);save(run/(variant+'.RUN.json'),dict(exitCode=ran.returncode))
        result=json.loads(ran.stdout);save(run/(variant+'.RESULT.json'),result);failed=[q['name'] for q in result['checks'] if not q['pass']]
        if variant in ('current','current-mirror','current-flush','current-recovery','current-preparation','current-sensor-retirement','current-sensor-source-preparation','current-sensor-error-detail','current-sensor-source-gate','current-msi-feedback','current-snapshots','current-diagnostic-memos','current-ipc-reoffer','current-ipc-reoffer-event-gates','current-record-preparation','current-classifier-text','current-path-preparation','current-preselection-stages','current-boundary-failures','current-finite-terminals','current-shortcut-outcomes','current-msi-return-debt','current-startup-finite','current-foreground-wait','current-ui-output-recovery','current-common-frame-recovery','current-quarantine-source','current-retained-session','current-touch-haptic-lanes','current-touch-haptic-preparation','current-touch-haptic-queue-clear','current-power-leaf-failures'):
            if ran.returncode!=0 or failed:raise ValueError('actual native sink failure checks failed')
        elif variant=='old-msi-feedback':
            if ran.returncode!=2 or failed!=['managed first MSI failure is resident with detail OFF']:raise ValueError('old MSI missing resident failure was not rejected')
        elif variant=='old-sensor-source-gate':
            if ran.returncode!=2 or failed!=['50000 actual original four-API retirement calls skip repeated JSON without consuming a C++ allocation fault']:raise ValueError('old source JSON construction was not rejected')
        elif variant=='old-sensor-error-detail':
            if ran.returncode!=2 or failed!=['four original catches retain actual WinRT exception HRESULT bits and failed subscription flags']:raise ValueError('old sensor exception detail was not rejected')
        elif variant=='missing-sensor-error-key':
            if ran.returncode!=2 or failed!=['changed HRESULT retains one new witness for its own operation and sensor']:raise ValueError('missing error key was not rejected')
        elif variant=='missing-sensor-source-preparation':
            if ran.returncode!=2 or failed!=['actual failed receipt construction arms named preparation evidence before Native append']:raise ValueError('missing source preparation evidence was not rejected')
        elif variant=='old-sensor-source':
            if ran.returncode!=2 or failed!=['first four original sensor API failures remain resident with detail OFF']:raise ValueError('old sensor source was not rejected')
        elif variant=='missing-sensor-success':
            if ran.returncode!=2 or failed!=['same-power errors after corresponding actual success retain four fresh witnesses']:raise ValueError('missing sensor success was not rejected')
        elif variant=='old-screen-preparation-source':
            if ran.returncode!=2 or failed!=['actual source allocation failure is recorded separately from file failures']:raise ValueError('old preparation source not rejected')
        elif variant=='old-screen-preparation-arm':
            if ran.returncode!=2 or failed!=['actual Native preparation allocation failure rearms current screen evidence without pretending file failure']:raise ValueError('old preparation arm not rejected')
        elif variant in ('old-screen-source-memo','old-screen-classifier-memo'):
            if ran.returncode!=2 or failed!=['same current screen state is retained after original file exit recovers']:raise ValueError('old screen memo loss was not rejected')
        elif variant=='missing-explicit-flush-evidence':
            if ran.returncode!=2 or failed!=['original explicit event flush failure is retained']:raise ValueError('missing flush evidence not rejected')
        elif variant=='missing-close-evidence':
            if ran.returncode!=2 or failed!=['controlled close refusal is retained before original clear']:raise ValueError('missing close evidence not rejected')
        elif variant=='missing-mirror-scope':
            if ran.returncode!=2 or failed!=['postwrite mirror exception is not misclassified as Native file sink exception']:raise ValueError('missing mirror scope was not rejected')
        elif ran.returncode!=2 or failed!=['actual open failure is counted in event lane']:raise ValueError('missing open evidence was not rejected by original assertion')
        if result['handlesBefore']!=result['handlesAfter']:raise ValueError('native sink private handles not returned')
        results.append(dict(variant=variant,nativeExitCode=ran.returncode,result=pin(run/(variant+'.RESULT.json')),graph=pin(run/(variant+'.GRAPH.json'))))
    for item in pins+tools:
        if pin(pathlib.Path(item['path']))!=item:raise ValueError('native sink input changed')
    report=dict(selectedCase='native-sink-failure',pass_=True,runs=results,sourcePins=pins,tools=tools,out=str(run),
        defaultSuiteExecuted=False,productSdkDeviceSleepExecuted=False,originalFunctions=10 if args.native_sink_case=='sensor-retirement' else 6,originalAdaptationGuardPrefixes=2 if args.native_sink_case=='sensor-retirement' else 0,exportBlockExecuted=True,
        limits='controlled streambuf faults;real ofstream open and successful writes;rotation/domain/deferred queues are ports;no complete runtime delivery proof')
    save(run/'RESULT.json',report);print(json.dumps(report,ensure_ascii=False));return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=pathlib.Path,
                        default=ROOT.parents[1] / "Build/Validation/LogFrequency")
    parser.add_argument("--gpu-windows-result", type=pathlib.Path, help="Production Windows offline transport responses for capability-log checks")
    parser.add_argument("--sdk-first-response",type=pathlib.Path,help="Actual-source fake-SDK reply for Native journal bridge replay")
    parser.add_argument("--review-existing-run",type=pathlib.Path,help="Read an immutable completed producer after a postcheck repair;never rebuild or rerun it")
    parser.add_argument("--case", choices=["aya-keyboard-source", "exit-event-owner", "adaptation-facts", "decky-mode-health", "fan-lifecycle-envelope", "primary-log-retry", "native-sink-failure", "http-failure-reason"], help="Directed existing logging consumer; default suite remains separate")
    parser.add_argument("--deferred-post-phase",choices=["submission-kinds"],help="Only the original deferred diagnostic submission exception boundaries")
    parser.add_argument("--decky-log-phase",choices=["memo-exception","memo-last-commit","jsonl-boundary","failure-counters"],help="Only original Decky memo, commit, JSONL boundary or failure observations")
    parser.add_argument("--startup-finite-phase",choices=["feedback-stop","lifecycle-outcomes","calibration-restore"],help="Only selected finite lifecycle receipts;default startup selection retained")
    parser.add_argument("--finite-terminal-phase",choices=["startup-outcomes","rog-recovery-outcomes","intent-delivery"],help="Only selected original finite startup, ROG recovery or bounded intent delivery outcomes")
    parser.add_argument("--native-sink-case",choices=["mirror","flush","recovery","preparation","sensor-retirement","msi-feedback","snapshots","diagnostic-memos","ipc-reoffer","record-preparation","classifier-text","path-preparation","preselection-stages","boundary-failures","deferred-post-witness","finite-terminals","intent-preflight","shortcut-outcomes","msi-return-debt","startup-finite","foreground-wait","ui-output-recovery","common-frame-recovery","quarantine-source","retained-session","touch-haptic-lanes","touch-haptic-preparation","power-leaf-failures"],help="Only selected mirror,flush/close,screen or sensor-retirement boundary;original Native append checks remain separate")
    parser.add_argument("--classifier-phase",choices=["command-result"],help="Only original command-result boolean classification and logger;no Host requests")
    parser.add_argument("--msi-feedback-phase",choices=["classification"],help="Only original MSI feedback classifier and Native lanes;no output lease or device calls")
    parser.add_argument("--ui-output-recovery-phase",choices=["connection-truth"],help="Only original connection truth producer and Native file-recovery boundary")
    parser.add_argument("--haptic-preparation-phase",choices=["queue-clear"],help="Only original accepted-summary queue-clear and pending-latch observations")
    parser.add_argument("--foreground-wait-replay",type=pathlib.Path,help="Explicit bounded extracted field replay for foreground-wait only")
    parser.add_argument("--aya-keyboard-case",choices=["legacy-air"],help="Only the selected legacy AIR function-key witness; existing AYA cases stay separate")
    parser.add_argument("--adaptation-export-boundary",choices=["index","model","facts-rejected","facts-rejected-resume","signals-rejected"],help="Only selected original export/index or rejected-facts boundary")
    parser.add_argument("--adaptation-record-boundary",choices=["open-visibility"],help="Only original Start open-result diagnostic initializer;no Start or device work")
    parser.add_argument("--adaptation-selector-boundary",choices=["guide"],help="Only final AI guide boundary;selector15 stays separate")
    parser.add_argument("--adaptation-selector-only",action="store_true",help="Only original selector plus bounded rejected-context evidence;other adaptation groups stay separate")
    parser.add_argument("--adaptation-journal-only",action="store_true",help="Only original Journal failure/defer branch evidence")
    parser.add_argument("--store-status-before",type=pathlib.Path,help="Explicit old header for full normal status parity in stores-only phase")
    parser.add_argument("--journal-failure-phase",choices=["budget","budget-tail","io","read","meta","meta-fields","stores","stores-control","prefix","prefix-control","prefix-faults","prefix-utf8"],help="Only selected Journal IO,read or metadata failure/publication boundary")
    parser.add_argument("--journal-failure-control",action="store_true",help="Only necessary missing-stage evidence assertion against an explicit old source")
    parser.add_argument("--adaptation-export-only",action="store_true",help="Only current-process first-witness export boundaries of adaptation-facts;existing selector/system groups stay separate")
    parser.add_argument("--source",type=pathlib.Path,help="Fixed main source for a directed consumer run")
    parser.add_argument("--native-sink-before",type=pathlib.Path,help="Optional exact pre-fix source for sensor-retirement control")
    parser.add_argument("--sensor-retirement-phase",choices=["source-preparation","error-detail","source-gate"],help="Only original sensor receipt preparation,caught-exception detail or source repeat preflight")
    parser.add_argument("--ipc-reoffer-phase",choices=["event-gates"],help="Only original IPC overflow and unclassified event gate file-recovery paths")
    parser.add_argument("--fan-lifecycle-case", choices=["text", "boundary", "failures", "detail-failures", "coordinator-rewrite"], help="Only selected Fan text,boundary or failure evidence;original envelope checks remain separate")
    parser.add_argument("--exit-event-case",choices=["wait-failure","session-marker"],help="Only original exit failure registration or session marker scope")
    parser.add_argument("--exit-event-phase",choices=["recovery"],help="Only original failed exit log recovery suffix")
    args = parser.parse_args()
    if args.classifier_phase and (args.case!="native-sink-failure" or args.native_sink_case!="classifier-text" or any(value for key,value in vars(args).items() if key not in ("case","native_sink_case","classifier_phase","source","output_dir"))):parser.error("--classifier-phase requires exclusive native-sink-failure/classifier-text")
    if args.msi_feedback_phase and (args.case!="native-sink-failure" or args.native_sink_case!="msi-feedback" or any(value for key,value in vars(args).items() if key not in ("case","native_sink_case","msi_feedback_phase","source","output_dir"))):parser.error("--msi-feedback-phase requires exclusive native-sink-failure/msi-feedback")
    if args.ui_output_recovery_phase and (args.case!="native-sink-failure" or args.native_sink_case!="ui-output-recovery" or any(value for key,value in vars(args).items() if key not in ("case","native_sink_case","ui_output_recovery_phase","source","output_dir"))):parser.error("--ui-output-recovery-phase requires exclusive native-sink-failure/ui-output-recovery")
    if args.adaptation_record_boundary and (args.case!="adaptation-facts" or any(value for key,value in vars(args).items() if key not in ("case","adaptation_record_boundary","output_dir"))):parser.error("--adaptation-record-boundary requires exclusive adaptation-facts")
    if args.decky_log_phase and (args.case!="decky-mode-health" or any((args.native_sink_case,args.finite_terminal_phase,args.deferred_post_phase,args.source,args.native_sink_before,args.review_existing_run))):parser.error("--decky-log-phase requires exclusive decky-mode-health")
    if args.haptic_preparation_phase and (args.case!="native-sink-failure" or args.native_sink_case!="touch-haptic-preparation"):parser.error("--haptic-preparation-phase requires native-sink-failure/touch-haptic-preparation")
    if args.ipc_reoffer_phase and (args.case!="native-sink-failure" or args.native_sink_case!="ipc-reoffer"):parser.error("--ipc-reoffer-phase requires native-sink-failure/ipc-reoffer")
    if args.exit_event_phase and (args.case != "exit-event-owner" or args.exit_event_case != "wait-failure"):parser.error("--exit-event-phase requires wait-failure")
    if args.exit_event_case and args.case != "exit-event-owner":parser.error("--exit-event-case requires exit-event-owner")
    if args.fan_lifecycle_case in ("failures","detail-failures","coordinator-rewrite") and any(value for key,value in vars(args).items() if key not in ("case","fan_lifecycle_case","source","output_dir")):parser.error("Fan failure selection requires exclusive fan-lifecycle-envelope")
    if args.fan_lifecycle_case and args.case != "fan-lifecycle-envelope":parser.error("--fan-lifecycle-case requires --case fan-lifecycle-envelope")
    if args.store_status_before and args.journal_failure_phase not in ("stores","stores-control","prefix","prefix-control","prefix-faults","prefix-utf8"):parser.error("old store status header requires stores phase")
    if args.journal_failure_phase and (not args.adaptation_journal_only or args.journal_failure_control):parser.error("journal phase requires exclusive journal-only without core control")
    if args.journal_failure_control and not args.adaptation_journal_only:parser.error("journal failure control requires journal-only")
    if args.adaptation_journal_only and (args.case!="adaptation-facts" or args.adaptation_export_only or args.adaptation_selector_only or args.native_sink_case or args.aya_keyboard_case or args.fan_lifecycle_case or args.exit_event_case or args.source or args.review_existing_run):parser.error("journal-only requires exclusive adaptation-facts")
    if args.adaptation_selector_boundary and not args.adaptation_selector_only:parser.error("--adaptation-selector-boundary requires selector-only")
    if args.adaptation_selector_only and (args.case != "adaptation-facts" or args.adaptation_export_only):parser.error("--adaptation-selector-only requires adaptation-facts and cannot combine with export-only")
    if args.adaptation_export_boundary and not args.adaptation_export_only:parser.error("--adaptation-export-boundary requires --adaptation-export-only")
    if args.adaptation_export_only and args.case != "adaptation-facts":parser.error("--adaptation-export-only requires --case adaptation-facts")
    if args.aya_keyboard_case and args.case != "aya-keyboard-source":parser.error("--aya-keyboard-case requires --case aya-keyboard-source")
    if args.sensor_retirement_phase and (args.case != "native-sink-failure" or args.native_sink_case != "sensor-retirement"):parser.error("--sensor-retirement-phase requires sensor-retirement")
    if args.native_sink_before and (args.case != "native-sink-failure" or args.native_sink_case != "sensor-retirement"):parser.error("--native-sink-before requires sensor-retirement")
    if args.startup_finite_phase and (args.case!="native-sink-failure" or args.native_sink_case!="startup-finite" or any(value for key,value in vars(args).items() if key not in ("case","native_sink_case","startup_finite_phase","source","output_dir"))):parser.error("--startup-finite-phase requires exclusive native-sink-failure/startup-finite")
    if args.finite_terminal_phase and (args.case!="native-sink-failure" or args.native_sink_case!="finite-terminals"):parser.error("--finite-terminal-phase requires native-sink-failure/finite-terminals")
    if args.deferred_post_phase and (args.case!="native-sink-failure" or args.native_sink_case!="deferred-post-witness"):parser.error("--deferred-post-phase requires native-sink-failure/deferred-post-witness")
    if args.foreground_wait_replay and (args.case!="native-sink-failure" or args.native_sink_case!="foreground-wait"):parser.error("--foreground-wait-replay requires foreground-wait")
    if args.native_sink_case=="foreground-wait" and not args.foreground_wait_replay:parser.error("foreground-wait requires explicit --foreground-wait-replay")
    if args.native_sink_case and args.case != "native-sink-failure":parser.error("--native-sink-case requires native-sink-failure")
    if args.case == "http-failure-reason":
        return run_http_failure_reason_case(args)
    if args.case == "native-sink-failure":
        return run_native_sink_failure_case(args)
    if args.case == "primary-log-retry":
        return run_primary_log_retry_case(args)
    if args.case == "fan-lifecycle-envelope":
        return run_fan_lifecycle_envelope_case(args)
    if args.case == "decky-mode-health":
        return run_decky_mode_health_case(args)
    if args.case == "exit-event-owner":
        return run_exit_event_owner_case(args)
    if args.case == "adaptation-facts":
        if args.adaptation_journal_only:return run_journal_failure_case(args)
        return run_adaptation_facts_case(args)
    review_run=args.review_existing_run.resolve() if args.review_existing_run else None

    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    run = output / ("run-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + str(os.getpid()))
    run.mkdir(exist_ok=False)
    sdk_response_path=args.sdk_first_response.resolve() if args.sdk_first_response else None
    sdk_response_bytes=sdk_response_path.read_bytes() if sdk_response_path else None
    if sdk_response_bytes is not None:(run/"sdk-first-response.json").write_bytes(sdk_response_bytes)
    gpu_result_path = args.gpu_windows_result.resolve() if args.gpu_windows_result else None
    if gpu_result_path:
        gpu_result_bytes = gpu_result_path.read_bytes()
        gpu_result = json.loads(gpu_result_bytes)
        if not (gpu_result.get("ok") and gpu_result.get("productionReader") and gpu_result.get("productionApply")):
            raise ValueError("GPU Windows result is not a passing production transport fixture")
    else:
        gpu_result_bytes = None
    extra_headers = {name:(ROOT/"native"/name).read_bytes() for name in ("input_capture_log_evidence.h", "log_snapshot_state.h", "virtual_start_protection.h", "rog_fse_experiment_policy.h", "sleep_fact_log_record.h", "log_jsonl_boundary.h", "sleep_steam_recovery.h", "decky_sidebar_diagnostics.h", "model_adaptation_log.h", "virtual_adaptation_evidence.h", "virtual_adaptation_log_native.inc", "model_adaptation_log_native.inc", "../capabilities/HcOneXVendorInputContract.h", "../capabilities/HcOemInputCapabilityBinding.h", "../capabilities/hc-device-catalog.generated.h")}
    source_path = args.source.resolve() if args.source else ROOT / "native/main.cpp"
    source_bytes = source_path.read_bytes()
    source = source_bytes.decode("utf-8-sig").replace("\r\n", "\n")
    identity_observer = extract(source, "static void hidHideObserveRawInputDevice(HANDLE rawDevice) {")
    header = ROOT / "native/log_frequency_policy.h"
    header_bytes = header.read_bytes()
    (run / "bound-main.cpp").write_bytes(source_bytes)
    (run / "bound-log-frequency-policy.h").write_bytes(header_bytes)
    functions = [extract(source, x) for x in (
        "static bool domainLogEnabled(const char* domain) {",
        "static bool sensorDetailLoggingGate() {",
        "static void rotateOneLogFile(const std::wstring& path, unsigned long long maxBytes)",
        "static void mirrorLifecycleToDomains(const char* event, const json& detail) {",
        "static bool nativeLifecycleEventIsBase(const char* event) {",
        "static void flushNativeLifecycleLogs(bool closeDetail) {",
        "static bool nativeLifecycleRecordIsStreaming(const char* event, const json& detail) {",
        "static void appendNativeLifecycleLog(const char* event, const json& observed) {",
        "static void domainLogGateRecord(const char* source, bool enabled) {",
        "static void inputHostCostFlagSync() {",
        "template<class Record>\nstatic void inputCaptureAppendRecord(Record&& rec) {",
        "static void inputCaptureAppend(const json& rec) {",
        "static void inputCaptureAppendOwned(json&& rec) {",
        "static void inputCaptureQueueSample(json rec) {",
        "static void inputCaptureDrainQueuedSamples() {",
        "static void sgRecordFact(const char* event, const json& details) {",
        "static void record_gpu_dedicated_memory_capability(const json& result) {",
        "static void hidHideLogPhysicalIdentityObservation(const HidHidePhysicalIdentity& resolved, std::uint64_t sourceGeneration, bool selectedChanged = true) {",
    )]
    # rotateOneLogFile has a forward declaration with a default argument. Select
    # its definition explicitly to avoid extracting the next unrelated function.
    functions[2] = extract(source, "static void rotateOneLogFile(const std::wstring& path, unsigned long long maxBytes) {")
    aya_prepared_writer = args.case == "aya-keyboard-source" and "static void appendNativeLifecyclePrepared(" in source
    if aya_prepared_writer:
        functions[7] = extract(source,"static void appendNativeLifecyclePrepared(const char* event,const json& detail,bool baseEvent,const std::string* encoded) {") + "\n" + functions[7]
        extra_headers["input_host_diagnostic_dispatch.h"]=(ROOT/"native/input_host_diagnostic_dispatch.h").read_bytes()
    functions.append(extract(source,"static json inputHostRecoveryEpisodeLogRecord(const json& requestId,unsigned long long hostEpoch,"))
    fan = extract(source, 'ipc_on("fanLog.setEnabled", [](const json& a) -> json {')
    fan_body = fan[fan.index("{"):]
    family = extract(source, "        if (controllerMotion) {")
    family_log = extract(family, "if (g_sensorDetailLogging.load(std::memory_order_acquire)) {")
    inspection_source = (ROOT / "native/sleep_steam_recovery.h").read_text(encoding="utf-8-sig").replace("\r\n", "\n")
    inspection_log = extract(inspection_source, "static void recordInspection(unsigned long long generation, const char* purpose, const json& observed) noexcept {")
    decky_log = extract(source, 'static std::wstring deckySidebarLogPath() {') + "\n" + extract(source, 'static void deckySidebarDiagnostic(const char* event,const json& detail,bool force,const ymcc::steamfailure::FailureContext* failure) {')
    screen_log = extract(source, 'static void logScreenTouchpadObservation(const char* site, const std::string& persona, const json& state) noexcept {')
    steam_log_projection = extract(source, "static json nativeSteamGameLinkLogDetail(const json& detail) {")
    prelude = r"""
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <map>
#include "decky_sidebar_diagnostics.h"
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "json.hpp"
#include "log_frequency_policy.h"
#include "log_snapshot_state.h"
#include "rog_fse_experiment_policy.h"
#include "sleep_fact_log_record.h"
#include "log_jsonl_boundary.h"
#include "input_capture_log_evidence.h"
#include "virtual_adaptation_evidence.h"
#include "virtual_start_protection.h"
#include <Xinput.h>
using json = nlohmann::json;
// Sleep activity sampling is outside this isolated journal/writer fixture.
static void sgSleepActivityOnFact(const char*,const json&) {}
static void flushTdpObservationLogs(bool = false) {}
// Detailed monitor sink is tested by monitor_detail_bounds_selftest; this fixture owns only legacy writer lanes.
static void monitorDetailGateChanged(bool) {}
namespace fspath = std::filesystem;
static std::wstring g_testRoot, SG_FACT_LOG;
struct MachineIdentityFixture {
    std::string deviceClass="HandheldCompanion.Devices.ClawCG3EM",manufacturer="MSI",
        systemProduct="G3E fixture",processor="Intel fixture",product="fixture",systemVersion="fixture";
    int family=0;
    bool resolved=true,attempted=true;
};
struct VirtualEvidenceSourceFixture {
    std::string inputSource="xinput";XINPUT_GAMEPAD pad{};
    uint64_t readAttemptSequence=17,readStartedTick=100,readCompletedTick=101,readPowerGeneration=23,readTopologyGeneration=11,readContinuityGeneration=7;
    unsigned primarySlot=0;bool live=true,readContextStable=true;
};
static const VirtualEvidenceSourceFixture* t_commonFrameSource=nullptr;
static MachineIdentityFixture g_machineIdentity;
static const char* ymccFamilyName(int) { return "fixture-family"; }
static const unsigned SG_FACT_HISTORY_LIMIT = 512;
static std::mutex g_sgFactMx;
static std::deque<std::string> g_sgFacts;
static std::string sgFactTimestamp() { return "2000-01-01T00:00:00.000"; }
static std::wstring app_data_dir() { return g_testRoot; }
static std::wstring yemancc_data_dir() { return g_testRoot; }
static std::string W2U(const std::wstring& s) { return std::string(s.begin(), s.end()); }
static unsigned long long kLogMaxBytes = 256; // fixture threshold only; production stays 5 MiB
static std::atomic<bool> g_domainLogEnabled[2] = {false, false};
static std::atomic<bool> g_sensorDetailLogging{false};
static std::atomic<bool> g_fanLogEnabled{false};
static std::wstring g_fanLogFile;
static std::atomic<int> g_powerLifecycle{0};
static std::atomic<unsigned long long> g_powerGeneration{0};
static HWND g_hwnd = nullptr;
static const char* powerLifecycleName(int) { return "offline-fixture"; }
static std::string nativeLifecycleTimestamp() { return "2000-01-01T00:00:00.000"; }
struct HidHidePhysicalIdentity {
    bool resolved=true;
    std::string sourcePath,deviceInstanceId,baseContainerDeviceInstanceId;
    unsigned short vendorId=0x17ef,productId=0x61ed,usagePage=1,usage=5;
};
static std::mutex g_nativeLifecycleLogMx;
static std::ofstream g_nativeLifecycleLog;
static uint32_t g_nativeLifecycleFrameWrites = 0;
static std::ofstream g_nativeDetailLog;
static uint32_t g_nativeDetailWrites = 0;
static ULONGLONG g_nativeDetailLastFlushTick = 0;
static std::mutex g_domainLogGateTimelineMx;
static std::vector<std::string> g_domainLogGateTimeline;
static bool g_domainLogGateKnown = false;
static bool g_domainLogGateLast = false;
static unsigned configWrites = 0;
static unsigned domainRows = 0;
static std::mutex g_inputCaptureMx;
static std::wstring g_inputCapturePath;
static std::ofstream g_inputCaptureLog;
static ymcc::logging::CaptureLogFailureEvidence g_inputCaptureLogFailureEvidence;
static uint64_t g_inputCaptureBytes = 0;
static uint32_t g_inputCaptureSampleWrites = 0;
static std::mutex g_inputCaptureSampleQueueMx;
static std::deque<json> g_inputCaptureSampleQueue;
static uint64_t g_inputCaptureSampleQueueDrops = 0;
static constexpr size_t kInputCaptureSampleQueueLimit = 256;
static std::wstring fan_host_state_dir() { return g_testRoot; }
static const std::wstring POWER_CONTROL_DIR=L"isolated-fixture-root";
static unsigned long long recoveryStateOwnProcessStartKey(){FILETIME born{},end{},kernel{},user{};if(!GetProcessTimes(GetCurrentProcess(),&born,&end,&kernel,&user))return 0;return (static_cast<unsigned long long>(born.dwHighDateTime)<<32)|born.dwLowDateTime;}
static void deckySidebarDiagnostic(const char*,const json&,bool,const ymcc::steamfailure::FailureContext* = nullptr);
static std::mutex g_domainLogGateWriteMx;
static void domainLogsSaveConfig() { ++configWrites; }
static void fanDiagnosticsApplyEnabled(bool on) { g_fanLogEnabled.store(on); }
static void appendDomainLog(const char*, const char*, const json&) { ++domainRows; }
static void flushDomainLogs(bool) {} // Real domain buffering has a separate R20 offline gate.
static void flushNativeLifecycleLogs(bool closeDetail = false);
static void rotateOneLogFile(const std::wstring&, unsigned long long maxBytes = kLogMaxBytes);
"""
    if aya_prepared_writer:
        prelude += '\n#include "input_host_diagnostic_dispatch.h"\n'
        prelude += 'static void inputHostDeferNativeDiagnostic(const char*,json,bool,bool=false) noexcept { std::terminate(); }\n'
    prelude += "\n" + extract(source,"static unsigned long long currentPowerGeneration() {")
    oem_constants = "\n".join(line for line in source.splitlines() if line.startswith("static constexpr uint64_t kOemBit"))
    catalog_fixture = '\n#include "../capabilities/HcOemInputCapabilityBinding.h"\n'
    for signature in ("enum class OemKeyForm : int {", "struct OemKeyDef {", "struct OemCatalogRef {", "static const OemKeyDef kOneXApexKeys[] = {"):
        catalog_fixture += extract(source,signature) + ";\n"
    catalog_fixture += r"""
static const ymcc::capabilities::oem::InputBinding<OemCatalogRef>& oemUnifiedInputBindingFor(int family,const std::string& cls){
    static ymcc::capabilities::oem::InputBindingCache<int,OemCatalogRef> cache;
    return cache.bind(family,cls,[](int,const std::string&){return 4;},[](int,const std::string& canonical){
        return canonical=="HandheldCompanion.Devices.OneXPlayerApex"?OemCatalogRef{kOneXApexKeys,static_cast<int>(std::size(kOneXApexKeys))}:OemCatalogRef{nullptr,0};});
}
"""
    prelude += "\n" + oem_constants + catalog_fixture + '\n#include "model_adaptation_log_native.inc"\n'
    rog_parser=extract(source,"static void rogOemKeyParse(const BYTE* data, size_t len, uint64_t actionGeneration) {")
    rog_constants="\n".join(line for line in source.splitlines() if line.startswith("static constexpr BYTE kRogKeyEvent") or line.startswith("static constexpr ULONGLONG kRogKeyPulseMs"))
    onex_constants="\n".join(line for line in source.splitlines() if line.startswith("static constexpr BYTE kOnexBack"))
    onex_real_parser=extract(source,"static void onexBackParse(const BYTE* data, size_t len, uint64_t actionGeneration) {")
    rog_fixture=r"""
enum class OemProducerId { RogClick, OneX };
static bool oemProducerCurrent(OemProducerId,unsigned long long generation){return generation==71;}
static struct {uint64_t pulseMask=0;ULONGLONG pulseUntil=0;} g_rogOemKey;
static std::atomic<uint64_t> g_oemKeyMaskRog{0};
static unsigned rogPublications=0;
static bool oemProducerPublishRogClick(unsigned long long, BYTE, uint64_t){++rogPublications;return true;}
""" + rog_constants + "\n" + rog_parser + r"""
static struct {ymcc::capabilities::oem::onex::Descriptor desc{true,true,ymcc::capabilities::oem::onex::InitProfile::Apex,true};
    bool feLatched=false;int state=0;uint64_t auxKeys=0,actionGeneration=71;} g_onexBack;
static std::atomic<uint64_t> g_oemKeyMaskHid{0};static std::atomic<int> g_oemBackState{0};
static unsigned onexReinit=0,onexPublications=0;static uint64_t onexObserved=0;
static void onexBackRequestReinit(){++onexReinit;}
static bool oemProducerPublish(OemProducerId,uint64_t,uint64_t,uint64_t observed){++onexPublications;onexObserved=observed;return true;}
""" + onex_constants + "\n" + onex_real_parser + "\n"
    tests = r"""
static json checks = json::array();
static void check(const char* name, bool pass) { checks.push_back({{"name", name}, {"pass", pass}}); }
static std::vector<json> readRows(const wchar_t* name) {
    flushNativeLifecycleLogs();
    std::ifstream f(fspath::path(g_testRoot) / name);
    std::vector<json> rows;
    for (std::string line; std::getline(f, line);) if (!line.empty()) rows.push_back(json::parse(line));
    return rows;
}
static void reset(const fspath::path& root, const wchar_t* name) {
    g_nativeLifecycleLog.close(); g_nativeLifecycleLog.clear();
    g_nativeDetailLog.close(); g_nativeDetailLog.clear();
    g_inputCaptureLog.close(); g_inputCaptureLog.clear();
    g_inputCaptureBytes = 0; g_inputCaptureSampleWrites = 0;
    g_inputCaptureSampleQueue.clear(); g_inputCaptureSampleQueueDrops = 0;
    kLogMaxBytes = 256;
    g_nativeLifecycleFrameWrites = 0; g_nativeDetailWrites = 0;
    g_nativeDetailLastFlushTick = GetTickCount64();
    g_domainLogEnabled[0] = false; g_domainLogEnabled[1] = false;
    g_domainLogGateKnown = false; g_domainLogGateTimeline.clear();
    g_sensorDetailLogging = false; g_fanLogEnabled = false; configWrites = 0; domainRows = 0;
    g_testRoot = (root / name).wstring(); fspath::create_directories(g_testRoot);
    SG_FACT_LOG = (fspath::path(g_testRoot) / L"sleep-facts.log").wstring(); g_sgFacts.clear();
    g_inputCapturePath = (fspath::path(g_testRoot) / L"input-capture.jsonl").wstring();
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2 && argc != 3) return 2;
    const fspath::path root(argv[1]);
    {
    reset(root,L"virtual-adaptation");kLogMaxBytes=1024*1024;g_powerGeneration=23;
    appendNativeLifecycleLog("input-host-switch-recovery",{{"powerGeneration",22},{"phase","stale"},{"requestId",37},{"hostEpoch",91},{"runId","old-run"},{"stopInvoked",false}});
    auto rows=readRows(L"native-lifecycle.log");
    check("virtual-old-captured-power-survives-current-writer-wrapper",rows.size()==1&&rows[0]["powerGeneration"]==23&&rows[0]["producerPowerGeneration"]==22&&rows[0]["observedPowerGeneration"]==23&&rows[0]["stopInvoked"]==false);
    VirtualEvidenceSourceFixture source;source.pad.wButtons=4096;source.pad.bLeftTrigger=128;t_commonFrameSource=&source;
    ymcc::virtualAdaptation::FrameStamp stamp{91,23,7,11,300,20,1234,false,true,true,true};stamp.captureBinding("old-run","Host-before-call");
    const std::string wire=R"({"command":"SUBMIT_FRAME","buttons":4096,"raw":{"bLeftTrigger":128},"runId":"old-run","epoch":91,"sequence":300,"startupNonce":"must-not-retain-this"})";
    for(unsigned i=0;i<50000;++i){stamp.commandSequence=300+i;stamp.wireSample=20+i;
        virtualAdaptationFrameReceipt(stamp,json::object(),wire,"first-request",{{"ok",true},{"startupNonce","must-not-retain-this"},{"frameReplyBinding","must-not-retain-this"}});}
    stamp.neutral=true;virtualAdaptationFrameReceipt(stamp,json::object(),wire,"neutral",json::object());
    stamp.accepted=false;virtualAdaptationFrameReceipt(stamp,json::object(),wire,"rejected",json::object());
    rows=readRows(L"virtual-adaptation.log");
    check("virtual-50000-frames-three-first-outcome-samples",rows.size()==4&&virtualAdaptationSelector().selected()==3);
    const auto& sample=rows[1]["firstSample"];
    check("virtual-frame-sample-preserves-exact-source-and-request",sample["source"]["pad"]["buttons"]==4096&&sample["source"]["pad"]["ltByte"]==128&&sample["replayRequest"]["buttons"]==4096&&sample["commandSequence"]==300&&rows[1]["uniqueKey"]["identity"]["hostInstanceId"]=="Host-before-call");
    check("virtual-transport-auth-not-logged-and-external-proof-false",sample.dump().find("must-not-retain-this")==std::string::npos&&!sample["replayRequest"].contains("startupNonce")&&!sample["nativeReceiptJson"].contains("frameReplyBinding")&&sample["hostSdkDeliveryProven"]==false&&sample["steamConsumptionProven"]==false);
    check("virtual-stage-original-power-retained",rows[0]["firstSample"]["producer"]["powerGeneration"]==22);
    check("virtual-independent-budget-and-no-per-write-rescan",virtualAdaptationStatus()["maxBytes"]==8ull*1024*1024&&virtualAdaptationJournal().status()["journalReadScans"]<=1);
    ymcc::virtualAdaptation::FrameSelector bounded;unsigned selected=0;
    for(unsigned context=1;context<=64;++context)for(unsigned flags=0;flags<8;++flags){auto value=stamp;value.epoch=context;value.neutral=flags&1;value.accepted=flags&2;value.sourceAdmitted=flags&4;selected+=bounded.select(value);}
    stamp.epoch=65;bool overflow=!bounded.select(stamp);stamp.epoch=0;bool unknown=!bounded.select(stamp);
    check("virtual-context-and-outcome-cap-no-eviction",selected==512&&overflow&&unknown&&bounded.capacityDrops()==1&&bounded.unknownDrops()==1);
    if(fspath::exists(root.parent_path()/L"sdk-first-response.json")){
        std::ifstream actualReply(root.parent_path()/L"sdk-first-response.json");json reply;actualReply>>reply;
        auto sdkStamp=stamp;sdkStamp.neutral=false;sdkStamp.accepted=true;
        virtualAdaptationFrameReceipt(sdkStamp,json::object(),wire,"bridge-request",reply);
        auto bridged=readRows(L"virtual-adaptation.log");
        check("virtual-actual-Host-SDK-first-witness-survives-native-sampler",bridged.size()==5&&bridged.back()["category"]=="virtual-sdk-first-witness"&&bridged.back()["firstSample"]["nativeReceiptJson"]["sdkFirstWitnesses"]==reply["sdkFirstWitnesses"]&&bridged.back()["firstSample"]["hostSdkDeliveryProven"]==false);
        reply["sdkFirstWitnesses"]=json::array();reply["sdkFirstWitnessCoverage"]={{"dropped",1},{"emissionFailures",0}};
        virtualAdaptationFrameReceipt(sdkStamp,json::object(),wire,"gap-1",reply);
        reply["sdkFirstWitnessCoverage"]["dropped"]=2;
        virtualAdaptationFrameReceipt(sdkStamp,json::object(),wire,"gap-2",reply);
        auto gapRows=readRows(L"virtual-adaptation.log");
        check("virtual-SDK-gap-only-updates-retain-changing-loss-counts",gapRows.size()==7&&gapRows.back()["firstSample"]["nativeReceiptJson"]["sdkFirstWitnessCoverage"]["dropped"]==2);

    }
    t_commonFrameSource=nullptr;
    appendNativeLifecycleLog("input-host-wake-execution",{{"stage","duplicate"},{"wakeSerial",1},{"originalPlayerDeadline",4000}});
    appendNativeLifecycleLog("input-host-wake-execution",{{"stage","retired"},{"wakeSerial",1},{"powerGeneration","unknown"},{"reason","original-wake-budget-expired"}});
    rows=readRows(L"native-lifecycle.log");
    check("virtual-missing-generation-stays-missing-and-unknown-stays-unknown",!rows[1].contains("producerPowerGeneration")&&rows[2]["producerPowerGeneration"]=="unknown"&&rows[2]["powerGeneration"]==23);
    auto evidence=readRows(L"virtual-adaptation.log");
    check("virtual-wake-duplicate-retains-original-budget",evidence[evidence.size()-2]["firstSample"]["producer"]["originalPlayerDeadline"]==4000&&evidence.back()["firstSample"]["producer"]["reason"]=="original-wake-budget-expired");
    bool protectionUnchanged=true,unknownRequest=true,badViewExpires=true;
    for(unsigned verifying=0;verifying<2;++verifying){
        ymcc::VirtualStartProtection currentProtection,badProtection;
        const auto currentAdmission=currentProtection.admit("steamdeck",1000);
        const auto badAdmission=badProtection.admit("steamdeck",1000);
        if(verifying){
            currentProtection.beginVerification(currentAdmission.requestId,"success",1002);
            badProtection.beginVerification(badAdmission.requestId,"success",1002);
            currentProtection.reserveFallback(currentAdmission.requestId,11002);
            badProtection.reserveFallback(badAdmission.requestId,11002);
        }
        const auto authorizedNow=verifying?12002ULL:1001ULL;
        const auto before=currentProtection.view(authorizedNow);json recorded;
        episodeLogProbe(currentProtection,500000,false,recorded);
        const auto after=currentProtection.view(authorizedNow);
        protectionUnchanged&=(before==after)&&after.fallbackAttempts==(verifying?1u:0u)&&after.phase==(verifying?"verifying":"pending");
        unknownRequest&=recorded["requestId"].is_null()&&recorded["requestIdScope"]=="unobserved;logger-does-not-read-start-protection";
        episodeLogProbe(badProtection,500000,true,recorded);
        badViewExpires&=badProtection.view(authorizedNow).phase=="expired";
    }
    check("episode-actual-log-block-preserves-real-class-pending-verifying-candidate-and-budget",protectionUnchanged);
    check("episode-actual-log-block-retains-unknown-request-without-state-read",unknownRequest);
    check("episode-negative-view-with-boot-clock-reproduces-premature-expiry",badViewExpires);
    const auto priorEpisodeRows=readRows(L"virtual-adaptation.log").size();
    const auto episodeA=inputHostRecoveryEpisodeLogRecord(7,91,"run-a","host-a",23,4,8,100);
    const auto episodeB=inputHostRecoveryEpisodeLogRecord(8,92,"run-b","host-b",23,6,9,110);
    const auto episodeA2=inputHostRecoveryEpisodeLogRecord(7,91,"run-a","host-a",23,4,8,100);
    const auto episodeA3=inputHostRecoveryEpisodeLogRecord(7,91,"run-a","host-a",23,4,8,100);
    for(unsigned i=0;i<50000;++i)virtualAdaptationObserveRecord("input-host-recovery-episode-new",episodeA);
    for(const auto& row:{episodeB,episodeA2,episodeA3})virtualAdaptationObserveRecord("input-host-recovery-episode-new",row);
    virtualAdaptationFlush();auto episodeRows=readRows(L"virtual-adaptation.log");
    check("virtual-episode-four-real-resets-not-collapsed-even-reused-context",episodeRows.size()==priorEpisodeRows+4&&episodeA["episodeId"]!=episodeA2["episodeId"]&&episodeA2["episodeId"]!=episodeA3["episodeId"]);
    check("virtual-episode-50000-replays-of-same-observation-retain-one",virtualAdaptationJournal().status()["budgetOmissions"]==0&&episodeRows.size()==priorEpisodeRows+4);
    const auto& episodeKey=episodeRows[priorEpisodeRows]["uniqueKey"]["identity"];
    check("virtual-episode-existing-context-and-writer-scope-retained",episodeKey["requestId"]==7&&episodeKey["hostEpoch"]==91&&episodeKey["runId"]=="run-a"&&episodeKey["hostInstanceId"]=="host-a"&&episodeKey["powerGeneration"]==23&&episodeKey["intentRevision"]==4&&episodeKey["presentationGeneration"]==8&&episodeKey["writerInstanceId"]==episodeA["writerInstanceId"]);
    const auto& metadata=episodeRows.back();const auto& cachedWriter=ymcc::sleepfactlog::writerIdentity();
    check("adaptation-journal-writer-birth-and-instance-come-from-existing-cache",metadata["writerPid"]==cachedWriter.pid&&metadata["writerProcessCreation100ns"]==std::to_string(cachedWriter.creation100ns)&&metadata["writerInstanceId"]==episodeA["writerInstanceId"]);
    bool sequenceIncreasing=true;for(size_t i=1;i<episodeRows.size();++i)sequenceIncreasing&=episodeRows[i]["writerSequence"].get<unsigned long long>()>episodeRows[i-1]["writerSequence"].get<unsigned long long>();
    check("adaptation-journal-sequence-is-process-scoped-and-outside-hardware-key",sequenceIncreasing&&metadata["journalScopeSchema"]==1&&!metadata.contains("writerSequenceScope")&&!metadata["uniqueKey"].contains("writerSequence")&&!metadata["uniqueKey"].contains("writerProcessCreation100ns"));

    const auto beforeStartup=readRows(L"virtual-adaptation.log").size();
    json slot={{"slot",0},{"stage","target-mismatch"},{"stateResult",0},{"confirmResult",1167},{"mappedSlot",255},
        {"path","cached-a"},{"stateAttempted",true},{"pathAttempted",true},{"targetInterfaceAttempted",true},
        {"targetInterfaceMatched",false},{"indexAttempted",false},{"confirmAttempted",false}};
    json startup={{"phase","xinput-missing"},{"attempt",1},{"retry",false},{"elapsedMs",500},{"polls",10},
        {"targetHid","target-a"},{"targetRoot","root-a"},{"nodePresent",true},{"statusRead",true},{"nodeStatus",10},{"nodeProblem",0},
        {"slotReadback",json::array({slot})}};
    g_domainLogEnabled[1]=false;
    appendNativeLifecycleLog("input-host-xbox-startup",startup);
    auto nativeStartup=readRows(L"native-lifecycle.log").back();
    check("virtual-startup-six-observation-flags-preserved-in-detail-off-native",nativeStartup["slotReadback"]==startup["slotReadback"]&&nativeStartup["phase"]=="xinput-missing"&&!g_domainLogEnabled[1]);
    json indexed=startup;indexed["slotReadback"][0]["targetInterfaceMatched"]=true;
    indexed["slotReadback"][0]["indexAttempted"]=true;indexed["slotReadback"][0]["stage"]="index-mismatch";
    json confirmed=indexed;confirmed["slotReadback"][0]["mappedSlot"]=0;
    confirmed["slotReadback"][0]["confirmAttempted"]=true;confirmed["slotReadback"][0]["stage"]="readback-changed";
    const json shapes[]={startup,indexed,confirmed};
    for(unsigned i=0;i<50000;++i){auto observed=shapes[i%3];observed["elapsedMs"]=500+i;observed["polls"]=10+i;
        observed["slotReadback"][0]["path"]="cached-"+std::to_string(i);
        virtualAdaptationObserveRecord("input-host-xbox-startup",observed);}
    virtualAdaptationFlush();auto startupRows=readRows(L"virtual-adaptation.log");
    check("virtual-startup-50000-observations-three-check-shapes-only",startupRows.size()==beforeStartup+3);
    const auto& firstStartup=startupRows[beforeStartup];const auto& startupIdentity=firstStartup["uniqueKey"]["identity"];
    check("virtual-startup-unknown-operation-and-original-fields-retained",firstStartup["firstSample"]["producer"]==startup&&firstStartup["firstSample"]["operationContextKnown"]==false);
    const auto& startupSlot=startupIdentity["startupReadbackShape"]["slotReadback"][0];
    check("virtual-startup-unattempted-default-and-attempted-failure-distinct",startupSlot["confirmAttempted"]==false&&startupSlot["confirmResult"]==1167&&startupRows[beforeStartup+2]["uniqueKey"]["identity"]["startupReadbackShape"]["slotReadback"][0]["confirmAttempted"]==true&&startupRows[beforeStartup+2]["uniqueKey"]["identity"]["startupReadbackShape"]["slotReadback"][0]["confirmResult"]==1167);
    check("virtual-startup-key-excludes-clock-poll-and-path",!startupIdentity.contains("elapsedMs")&&!startupIdentity.contains("polls")&&!startupSlot.contains("path"));
    json legacy=startup;for(const auto* flag:{"stateAttempted","pathAttempted","targetInterfaceAttempted","targetInterfaceMatched","indexAttempted","confirmAttempted"})legacy["slotReadback"][0].erase(flag);
    virtualAdaptationObserveRecord("input-host-xbox-startup",legacy);virtualAdaptationFlush();startupRows=readRows(L"virtual-adaptation.log");
    check("virtual-startup-missing-attempt-flags-remain-unknown",startupRows.size()==beforeStartup+4&&startupRows.back()["uniqueKey"]["identity"]["startupReadbackShape"]["slotReadback"][0]["confirmAttempted"].is_null()&&!startupRows.back()["firstSample"]["producer"]["slotReadback"][0].contains("confirmAttempted"));
    for(unsigned request:{32,33}){auto observed=startup;observed["requestId"]=request;virtualAdaptationObserveRecord("input-host-xbox-startup",observed);}
    virtualAdaptationFlush();startupRows=readRows(L"virtual-adaptation.log");
    check("virtual-startup-explicit-request-context-preserved-when-provided",startupRows.size()==beforeStartup+6&&startupRows.back()["uniqueKey"]["identity"]["requestId"]==33&&startupRows.back()["firstSample"]["operationContextKnown"]==true);
    auto nodeChanged=startup;nodeChanged["nodeProblem"]=28;
    virtualAdaptationObserveRecord("input-host-xbox-startup",nodeChanged);virtualAdaptationFlush();startupRows=readRows(L"virtual-adaptation.log");
    check("virtual-startup-node-problem-changes-not-collapsed",startupRows.size()==beforeStartup+7&&startupRows.back()["uniqueKey"]["identity"]["startupReadbackShape"]["nodeProblem"]==28);


    }
    using ymcc::logging::NativeLane;
    using ymcc::logging::nativeLane;
    check("lane-policy-base-stays-event-on-and-off",
        nativeLane(true, false, false) == NativeLane::event && nativeLane(true, false, true) == NativeLane::event);
    check("lane-policy-details-off-suppressed-on-detailed",
        nativeLane(false, false, false) == NativeLane::suppressed && nativeLane(false, false, true) == NativeLane::detail);
    check("lane-policy-successful-frame-never-base",
        nativeLane(true, true, false) == NativeLane::suppressed && nativeLane(true, true, true) == NativeLane::detail);
    check("normal-build-compiles-ROG-FSE-adaptation-diagnostics-out",YMCC_ROG_FSE_DIAGNOSTICS==0);
    check("ROG-raw-input-window-cannot-be-resident",!nativeLifecycleEventIsBase("rog-raw-input-window"));
    for(bool detailed : {false,true}) {
        reset(root,detailed?L"r114-evidence-detail-on":L"r114-evidence-detail-off");
        g_domainLogEnabled[1]=detailed;
        check("R114 bounded diagnostics classified as base",nativeLifecycleEventIsBase("input-host.native-helper-diagnostic")&&nativeLifecycleEventIsBase("hidhide.owned-game-reconnect-plan"));
        appendNativeLifecycleLog("input-host.native-helper-diagnostic",{{"helperExitCode",8},{"helperDiagnostic",{{"state","record-accepted"}}}});
        appendNativeLifecycleLog("hidhide.owned-game-reconnect-plan",{{"discoveryAttempts",1},{"driverWrite",false}});
        flushNativeLifecycleLogs(true);
        const auto rows=readRows(L"native-lifecycle.log");
        check("R114 helper and owned-plan facts reach ordinary lifecycle file",rows.size()==2&&rows[0].value("event",std::string())=="input-host.native-helper-diagnostic"&&rows[1].value("event",std::string())=="hidhide.owned-game-reconnect-plan");
        check("R114 bounded events do not become a detailed stream",readRows(L"native-detail.log").empty());
    }
    for(bool detailed : {false,true}) {
        reset(root,detailed?L"onex-start-detail-on":L"onex-start-detail-off");
        g_domainLogEnabled[1]=detailed;
        check("OneX Start exact stage event is ordinary while forged names stay unknown",
            nativeLifecycleEventIsBase("input-host-onex-start-refresh") &&
            !nativeLifecycleEventIsBase("input-host-onex-start-refresh-forged"));
        const json proof={{"stage","completed"},{"requestId",17},{"persona","steamdeck"},{"runId","fixture-onex"},
            {"freshSameRootSource",true},{"driverOperationMayHaveIssued",true},
            {"externalSuppressionVerified",false},{"filterAttachmentProven",false},
            {"newTargetCreatedHere",false},{"sourceInterfacePathKnown",true},{"reverseBindingApiResolved",true},
            {"repairRevision","VC-ONEX-093203-R1"},{"locationPathCount",1},
            {"usbTopology",{{"desiredAccess",0},{"portKind",-1},{"reason","hop-unproven"},
                {"hops",json::array({{{"port",4},{"openError",5},{"reason","hub-open-failed"}}})}}}};
        appendNativeLifecycleLog("input-host-onex-start-refresh",proof);flushNativeLifecycleLogs(true);
        const auto rows=readRows(L"native-lifecycle.log");
        check("OneX Start actual writer retains the bound identity and limited evidence fields",
            rows.size()==1 && rows[0].value("event",std::string{})=="input-host-onex-start-refresh" &&
            rows[0].value("requestId",0)==17 && rows[0].value("runId",std::string{})=="fixture-onex" &&
            rows[0].value("persona",std::string{})=="steamdeck" && rows[0].value("freshSameRootSource",false) &&
            rows[0].value("driverOperationMayHaveIssued",false) &&
            !rows[0].value("externalSuppressionVerified",true) && !rows[0].value("filterAttachmentProven",true) &&
            !rows[0].value("newTargetCreatedHere",true) && rows[0].value("sourceInterfacePathKnown",false) &&
            rows[0].value("reverseBindingApiResolved",false));
        check("OneX093203 actual ordinary writer retains nested query failure evidence without asserting internal hardware",
            rows.size()==1 && rows[0].value("repairRevision",std::string{})=="VC-ONEX-093203-R1" &&
            rows[0].value("locationPathCount",0)==1 && rows[0]["usbTopology"].value("desiredAccess",999)==0 &&
            rows[0]["usbTopology"].value("portKind",0)==-1 && rows[0]["usbTopology"]["hops"][0].value("openError",0)==5 &&
            rows[0]["usbTopology"]["hops"][0].value("reason",std::string{})=="hub-open-failed");
        check("OneX bounded Start stages do not create a detailed stream",readRows(L"native-detail.log").empty());
    }
    for(bool detailed : {false,true}) {
        reset(root,detailed?L"r117-slot-detail-on":L"r117-slot-detail-off");
        g_domainLogEnabled[1]=detailed;++g_powerGeneration;
        const json assignment={{"path","fixture-original-source"},{"targetSlot",0},{"result",0},{"actualSlot",0},{"accepted",true}};
        const json guarded={{"primarySlot",0},{"probeSlot",1},{"assigned",true},{"accepted",true},{"intentRevisionAtQuery",4}};
        for(int n=0;n<8;++n){appendNativeLifecycleLog("gamepad-openxinput-assign-slot",assignment);appendNativeLifecycleLog("gamepad-openxinput-slot-guard",guarded);}
        flushNativeLifecycleLogs(true);
        check("R117 actual assignment and guard changes reach ordinary log",readRows(L"native-lifecycle.log").size()==2);
        check("R117 repeated results stay detail-only",readRows(L"native-detail.log").size()==(detailed?14u:0u));
        auto failure=assignment;failure["accepted"]=false;failure["result"]=1167;appendNativeLifecycleLog("gamepad-openxinput-assign-slot",failure);
        auto newIntent=guarded;newIntent["intentRevisionAtQuery"]=6;appendNativeLifecycleLog("gamepad-openxinput-slot-guard",newIntent);
        flushNativeLifecycleLogs(true);check("R117 outcome and intent transitions retained",readRows(L"native-lifecycle.log").size()==4);
    }
    reset(root, L"snapshot-routing"); kLogMaxBytes = 5 * 1024 * 1024;
    g_domainLogEnabled[1] = true;
    for (int i=0;i<296;++i) appendNativeLifecycleLog("rog.hid-bound", {{"matched", false}, {"bindingPathHash", 1}});
    check("repeated-rog-binding-first-event-rest-detailed", readRows(L"native-lifecycle.log").size()==1 && readRows(L"native-detail.log").size()==295);
    for (int i=0;i<64;++i) appendNativeLifecycleLog("rog.hid-discovery", {{"path", i}, {"report5A", true}});
    check("per-interface-rog-discovery-never-enters-low-frequency", readRows(L"native-lifecycle.log").size()==1 && readRows(L"native-detail.log").size()==359);
    appendNativeLifecycleLog("rog.hid-discovery-summary", {{"matches", 3}, {"enumerationComplete", true}});
    check("rog-discovery-summary-first-observation-is-low-frequency", readRows(L"native-lifecycle.log").size()==2);
    appendNativeLifecycleLog("rog.hid-bound", {{"matched", true}, {"bindingPathHash", 1}});
    appendNativeLifecycleLog("rog.hid-bound", {{"matched", true}, {"bindingPathHash", 2}});
    ++g_powerGeneration;
    appendNativeLifecycleLog("rog.hid-bound", {{"matched", true}, {"bindingPathHash", 2}});
    check("binding-result-path-and-power-generation-edges-stay-event", readRows(L"native-lifecycle.log").size()==5);
    g_domainLogEnabled[1] = false;
    appendNativeLifecycleLog("rog.hid-bound", {{"matched", true}, {"bindingPathHash", 2}});
    appendNativeLifecycleLog("rog.hid-bound", {{"matched", false}, {"bindingPathHash", 0}});
    check("snapshot-detail-off-does-not-hide-transitions", readRows(L"native-lifecycle.log").size()==6 && readRows(L"native-detail.log").size()==359);
    appendNativeLifecycleLog("gamepad.feedbackCaps", {{"ledAvailable", true}});
    appendNativeLifecycleLog("gamepad.feedbackCaps", {{"ledAvailable", true}});
    appendNativeLifecycleLog("gamepad.feedbackCaps", {{"ledAvailable", false}});
    check("feedback-capability-first-change-not-duplicate-stays-event", readRows(L"native-lifecycle.log").size()==8);
    appendNativeLifecycleLog("http-request-failed", {{"deadlineExpired", true}});
    appendNativeLifecycleLog("screen-standalone-special", {{"status", "completed"}});
    appendNativeLifecycleLog("desktop-hud-event", {{"hudEvent", "desktopHud.closed"}});
    appendNativeLifecycleLog("desktop-hud-event", {{"hudEvent", "desktopHud.painted"}});
    check("new-errors-actions-and-hud-boundaries-survive-default-off", readRows(L"native-lifecycle.log").size()==11);
    g_domainLogEnabled[1] = true;
    appendNativeLifecycleLog("desktop-hud-event", {{"hudEvent", "desktopHud.painted"}, {"seq", 42}});
    check("hud-paint-is-detailed-with-original-sequence", readRows(L"native-detail.log").size()==360 && readRows(L"native-detail.log").back()["seq"]==42);
    appendNativeLifecycleLog("desktop-hud-event", {{"hudEvent", "desktopHud.unavailable"}, {"hud", {{"reason", "content-too-large"}, {"seq", 1}}}});
    appendNativeLifecycleLog("desktop-hud-event", {{"hudEvent", "desktopHud.unavailable"}, {"hud", {{"reason", "content-too-large"}, {"seq", 2}}}});
    appendNativeLifecycleLog("desktop-hud-event", {{"hudEvent", "desktopHud.unavailable"}, {"hud", {{"reason", "target-exited"}, {"seq", 3}}}});
    check("hud-repeated-unavailable-sequence-is-detailed-reason-edge-resident",readRows(L"native-lifecycle.log").size()==13 && readRows(L"native-detail.log").size()==361 && readRows(L"native-detail.log").back()["hud"]["seq"]==2);
    reset(root, L"sleep-device-routing"); kLogMaxBytes = 5 * 1024 * 1024;
    g_domainLogEnabled[1] = true;
    for (int i=0;i<373;++i) sgRecordFact("device-change", {{"code",7},{"userStandby",false},{"standbyAgeEligible",false},{"standbyAgeMs",i}});
    sgRecordFact("device-change", {{"code",32768},{"userStandby",false}});
    sgRecordFact("device-change", {{"code",32772},{"userStandby",false}});
    sgRecordFact("device-change", {{"code",7},{"userStandby",true}});
    sgRecordFact("device-change", {{"code",7},{"standbyAgeEligible",true}});
    sgRecordFact("power-broadcast", {{"code",7}});
    const auto treeRows=readRows(L"native-detail.log");
    check("generic-tree-notifications-detailed-no-row-or-field-loss",treeRows.size()==373 && treeRows.back()["details"]["standbyAgeMs"]==372 && treeRows.front()["factEvent"]=="device-change");
    check("arrival-removal-standby-and-power-boundaries-stay-sleep-facts",g_sgFacts.size()==5 && readRows(L"sleep-facts.log").size()==5);
    const auto sleepIdentityRows=readRows(L"sleep-facts.log");
    const auto& firstSleep=sleepIdentityRows.front();const auto& lastSleep=sleepIdentityRows.back();
    check("sleep-facts-explicit-writer-process-creation-not-subject-pid",firstSleep["writerPid"]==GetCurrentProcessId()&&firstSleep["writerInstanceId"].is_string()&&firstSleep["writerProcessCreation100ns"].is_string()&&firstSleep["writerIdentityStatus"]=="pid-and-process-creation-known");
    check("sleep-facts-local-legacy-clock-preserved-new-UTC-labeled",firstSleep["time"]==sgFactTimestamp()&&firstSleep["timestampUtc"].get<std::string>().back()=='Z'&&firstSleep["timestampLocal"].is_string()&&firstSleep["utcEpochMs"].is_number_unsigned()&&firstSleep["observedTickMs"].is_number_unsigned());
    check("sleep-fact-sequence-follows-actual-journal-order",lastSleep["writerSequence"].get<unsigned long long>()==firstSleep["writerSequence"].get<unsigned long long>()+4);
    check("sleep-fact-generation-observer-scope-is-explicit",firstSleep["writerPowerGeneration"]==currentPowerGeneration()&&firstSleep["factSchemaVersion"]==2&&firstSleep["observationScope"]=="journal-record-creation;not-kernel-event-time-or-action-completion");
    auto originalFacts=json{{"generation",999},{"writerPid",987654},{"time","original-kernel-time"},{"result",false}};
    sgRecordFact("sleep-action-failed",originalFacts);
    const auto failedFact=readRows(L"sleep-facts.log").back();
    check("sleep-fact-details-preserved-and-subject-identity-not-promoted",failedFact["details"]==originalFacts&&failedFact["writerPid"]==GetCurrentProcessId()&&failedFact["writerPowerGeneration"]==currentPowerGeneration());
    // Restore only this fixture's extra row; original production assertions remain unchanged.
    g_sgFacts.pop_back();{std::ofstream f(fspath::path(SG_FACT_LOG),std::ios::binary|std::ios::trunc);for(const auto& row:sleepIdentityRows)f<<row.dump()<<'\n';}

    g_domainLogEnabled[1]=false;
    sgRecordFact("device-change", {{"code",7},{"userStandby",true}});
    check("standby-relevant-device-event-survives-detail-off",g_sgFacts.size()==6);
    // REQUIRED default-log contract for sleep repair; no frequent log opt-in.
    reset(root, L"sleep-required-evidence-off");
    unsigned requiredSleepFacts=0;
    for(const char* event:{"user-initiated-sleep","abnormal-sleep-live-source-start","abnormal-sleep-live-source-gap",
        "abnormal-sleep-armed","abnormal-sleep-evidence-state","abnormal-sleep-deadline","abnormal-sleep-request",
        "abnormal-sleep-request-result","abnormal-sleep-owned-retry-result","abnormal-sleep-finished",
        "s0-low-power-residency","static-wake-delay-armed","static-wake-delay-canceled","static-wake-scan-start",
        "static-wake-scan-result","static-wake-restore-result"}) {
        sgRecordFact(event,{{"mandatoryEvidence",true},{"detailLogsRequired",false}});++requiredSleepFacts;
    }
    const auto mandatorySleepRows=readRows(L"sleep-facts.log");
    check("required-sleep-source-decision-action-cancel-facts-survive-default-detail-off",mandatorySleepRows.size()==requiredSleepFacts&&g_sgFacts.size()==requiredSleepFacts);
    check("required-sleep-evidence-preserves-event-and-fields",mandatorySleepRows.front()["event"]=="user-initiated-sleep"&&mandatorySleepRows.back()["event"]=="static-wake-restore-result"&&mandatorySleepRows.back()["details"]["detailLogsRequired"]==false);
    check("required-sleep-evidence-does-not-enable-high-frequency-stream",!fspath::exists(fspath::path(g_testRoot)/L"native-detail.log")&&!g_domainLogEnabled[1]);
    check("static-summary-retained-errors-are-not-labeled-fresh-failures",mandatorySleepRows.back()["payloadSemantics"]["currentItemReadbackVerified"].is_null()&&mandatorySleepRows.back()["payloadSemantics"]["actualWritesThisOperation"].is_null()&&mandatorySleepRows.back()["details"]["mandatoryEvidence"]==true);


    reset(root, L"off");
    appendNativeLifecycleLog("app-exit-ipc", {{"fixture", true}});
    for (int i=0; i<1000; ++i) appendNativeLifecycleLog("real-stick-frame", {{"index", i}});
    check("off-base-immediately-readable", readRows(L"native-lifecycle.log").size() == 1);
    check("off-details-no-file-no-domain-mirror", !fspath::exists(fspath::path(g_testRoot)/L"native-detail.log") && domainRows == 0);
    reset(root, L"on"); g_domainLogEnabled[0] = true; g_domainLogEnabled[1] = true;
    appendNativeLifecycleLog("gyro-calibration-request", {{"fixture", true}});
    for (int i=0; i<40; ++i) appendNativeLifecycleLog("real-stick-frame", {{"index", i}});
    check("on-low-file-only-base-events", readRows(L"native-lifecycle.log").size() == 1);
    const auto detailed = readRows(L"native-detail.log");
    check("on-detail-preserves-all-rows-and-order", detailed.size() == 40 && detailed.front()["index"] == 0 && detailed.back()["index"] == 39);
    check("on-domain-mirror-preserved", domainRows == 41);
    reset(root, L"frames-off");
    for(int i=0;i<1000;++i) appendNativeLifecycleLog("input-host-command-result", {{"command", "SUBMIT_FRAME"}, {"ok", true}, {"sequence", i}});
    check("successful-frame-off-neither-lane-created", !fspath::exists(fspath::path(g_testRoot)/L"native-lifecycle.log") && !fspath::exists(fspath::path(g_testRoot)/L"native-detail.log"));
    appendNativeLifecycleLog("input-host-command-result", {{"command", "SUBMIT_FRAME"}, {"ok", false}, {"phase", "transport"}});
    check("frame-error-remains-visible-with-detail-off", readRows(L"native-lifecycle.log").size() == 1);
    reset(root, L"frames-on"); g_domainLogEnabled[1] = true;
    appendNativeLifecycleLog("input-host-command-result", {{"command", "SUBMIT_FRAME"}, {"ok", true}});
    check("successful-frame-on-detailed-not-base", readRows(L"native-detail.log").size() == 1 && !fspath::exists(fspath::path(g_testRoot)/L"native-lifecycle.log"));
    reset(root, L"rate");
    ymcc::logging::BusRateLogState rate;
    unsigned edges = 0;
    for(int i=0;i<500;++i) edges += rate.observe(0);
    const bool normalSilent = edges == 0;
    for(int i=0;i<500;++i) {
        if(rate.observe(1)) { ++edges; appendNativeLifecycleLog("input-capture-bus-rate", {{"rateFlags", 1}}); }
    }
    check("rate-normal-silent-persistent-anomaly-one-event", normalSilent && edges == 1 && readRows(L"native-lifecycle.log").size() == 1);
    edges += rate.observe(3); edges += rate.observe(2); edges += rate.observe(0);
    for(int i=0;i<500;++i) edges += rate.observe(0);
    check("rate-category-change-and-recovery-once", edges == 4);
    reset(root, L"catalog-off");
    appendNativeLifecycleLog("oem-key-catalog-query", {{"count", 0}});
    check("read-only-catalog-off-not-base", !fspath::exists(fspath::path(g_testRoot)/L"native-lifecycle.log") && !fspath::exists(fspath::path(g_testRoot)/L"native-detail.log"));
    reset(root, L"rotation");
    {std::ofstream f(fspath::path(g_testRoot)/L"native-lifecycle.log"); f << std::string(300, 'x') << '\n';}
    g_nativeLifecycleFrameWrites = 255;
    appendNativeLifecycleLog("app-exit-ipc", {{"rotation", true}});
    check("base-rotation-retains-backup-and-current-row", fspath::exists(fspath::path(g_testRoot)/L"native-lifecycle.log.1") && readRows(L"native-lifecycle.log").size() == 1);
    reset(root, L"blocked-backup");
    {std::ofstream f(fspath::path(g_testRoot)/L"native-lifecycle.log"); f << "retained-before\n" << std::string(300, 'x') << '\n';}
    const auto backup = fspath::path(g_testRoot)/L"native-lifecycle.log.1";
    HANDLE held = CreateFileW(backup.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    g_nativeLifecycleFrameWrites = 255;
    appendNativeLifecycleLog("app-exit-ipc", {{"rotation", true}});
    std::ifstream active(fspath::path(g_testRoot)/L"native-lifecycle.log"); std::stringstream contents; contents << active.rdbuf();
    check("blocked-backup-preserves-active-and-new-event", held != INVALID_HANDLE_VALUE && contents.str().find("retained-before") == 0 && contents.str().find("app-exit-ipc") != std::string::npos);
    active.close(); if(held != INVALID_HANDLE_VALUE) CloseHandle(held);
    reset(root, L"fan-toggle");
    const json enabled = fanToggle({{"enabled", true}});
    const auto costFlag = fspath::path(g_testRoot)/L"input-host-cost-enabled.flag";
    check("fan-entry-on-syncs-authoritative-and-derived-gates", enabled["enabled"] == true && g_domainLogEnabled[0] && g_domainLogEnabled[1] && g_sensorDetailLogging && fspath::exists(costFlag));
    appendNativeLifecycleLog("real-stick-frame", {{"partialBatch", true}});
    fanToggle({{"enabled", false}});
    const auto rowsAfterOff = readRows(L"native-detail.log");
    check("fan-entry-off-flushes-and-closes-detail-stream", rowsAfterOff.size() == 1 && !g_nativeDetailLog.is_open());
    check("fan-entry-off-clears-sensor-detail-and-cost-flag", !g_domainLogEnabled[0] && !g_domainLogEnabled[1] && !g_sensorDetailLogging && !fspath::exists(costFlag));
    appendNativeLifecycleLog("real-stick-frame", {{"afterOff", true}});
    check("off-does-not-reopen-detail-stream", !g_nativeDetailLog.is_open() && readRows(L"native-detail.log").size() == 1);
    const unsigned writesBeforeRead = configWrites; fanToggle(json::object());
    check("fan-entry-read-only-does-not-save-or-enable", configWrites == writesBeforeRead && !g_sensorDetailLogging && !fspath::exists(costFlag));
    reset(root, L"family-imu"); kLogMaxBytes = 5 * 1024 * 1024;
    for(int i=0;i<1000;++i) familyImuLog(1,2,3,4,5,6);
    check("family-imu-off-no-json-queue-or-file", g_inputCaptureSampleQueue.empty() && !fspath::exists(g_inputCapturePath));
    g_sensorDetailLogging = true;
    for(int i=0;i<16;++i) familyImuLog(i,2,3,4,5,6);
    check("family-imu-on-queues-without-bus-disk-io", g_inputCaptureSampleQueue.size() == 16 && !fspath::exists(g_inputCapturePath));
    inputCaptureDrainQueuedSamples();
    const auto imuRows = readRows(L"input-capture.jsonl");
    check("family-imu-writer-preserves-values-and-sample-batching", imuRows.size() == 16 && g_inputCaptureSampleWrites == 16 && imuRows.front()["dps"]["x"] == 0 && imuRows.back()["dps"]["x"] == 15);
    check("sample-writer-pid-preserves-sample-values", imuRows.front()["writerPid"] == GetCurrentProcessId());
    inputCaptureAppend({{"kind","identity-fixture"},{"pid",12345},{"writerPid",0},{"t",98765}});
    const auto identitySample = readRows(L"input-capture.jsonl").back();
    check("sample-writer-pid-does-not-relabel-business-pid-or-producer-time",
        identitySample["writerPid"] == GetCurrentProcessId() && identitySample["pid"] == 12345 && identitySample["t"] == 98765);
    check("sample-kind-policy-does-not-batch-lifecycle-events", ymcc::logging::inputSampleKind("sample") && ymcc::logging::inputSampleKind("controller-family-imu") && !ymcc::logging::inputSampleKind("stop") && !ymcc::logging::inputSampleKind("worker-exception"));
    reset(root,L"capture-interrupted-tail");kLogMaxBytes=1024*1024;
    const std::string captureBroken="{\"kind\":\"old-fragment\",\"value\":";
    {std::ofstream seed(g_inputCapturePath,std::ios::binary);seed<<captureBroken;}
    inputCaptureAppend({{"kind","boundary-next"},{"value",37}});
    {std::ifstream input(g_inputCapturePath,std::ios::binary);std::string retained,next;std::getline(input,retained);std::getline(input,next);
    check("capture-open-preserves-old-fragment-and-next-json-row",retained==captureBroken&&json::parse(next)["value"]==37);}
    g_inputCaptureLog.close();g_inputCaptureLog.clear();inputCaptureAppendOwned(json{{"kind","boundary-next"},{"value",38}});
    {std::ifstream input(g_inputCapturePath,std::ios::binary);unsigned lines=0;for(std::string line;std::getline(input,line);)++lines;
    check("capture-clean-reopen-does-not-add-empty-boundary",lines==3);}
    reset(root,L"capture-valid-missing-lf");kLogMaxBytes=1024*1024;
    {std::ofstream seed(g_inputCapturePath,std::ios::binary);seed<<"{\"kind\":\"previous-valid\",\"value\":1}";}
    inputCaptureAppend({{"kind","next-valid"},{"value",2}});
    const auto captureTwo=readRows(L"input-capture.jsonl");
    check("capture-valid-old-row-missing-lf-and-new-row-both-retained",captureTwo.size()==2&&captureTwo[0]["value"]==1&&captureTwo[1]["value"]==2);
    reset(root, L"queue-bound"); g_sensorDetailLogging = true;
    for(int i=0;i<300;++i) familyImuLog(i,2,3,4,5,6);
    check("family-imu-queue-bound-and-drop-counter-retained", g_inputCaptureSampleQueue.size() == 256 && g_inputCaptureSampleQueueDrops == 44 && g_inputCaptureSampleQueue.front()["dps"]["x"] == 44 && !fspath::exists(g_inputCapturePath));
    reset(root, L"discovery-summary-routing"); g_domainLogEnabled[1] = true;
    ++g_powerGeneration;
    const json absent = {{"schema",2},{"matches",0},{"enumerationComplete",true},{"enumerationError",0},
        {"interfaceInspectionCoverage","not-measured"},{"physicalAbsenceProven",false}};
    // Replay the supplied archive's repeated-absence groups (43/22/42/21).
    for (int count : {43,22,42,21}) {
        ++g_powerGeneration;
        for (int i=0;i<count;++i) appendNativeLifecycleLog("rog.hid-discovery-summary", absent);
    }
    flushNativeLifecycleLogs(false);
    check("archive-128-summary-observations-keep-four-edges-and-124-detail-rows",
        readRows(L"native-lifecycle.log").size()==4 && readRows(L"native-detail.log").size()==124);
    g_domainLogEnabled[1] = false;
    for(int i=0;i<64;++i) {
        appendNativeLifecycleLog("rog.hid-discovery-summary", absent);
        appendNativeLifecycleLog("rog.hid-discovery", {{"path","fake-hid-path"}});
    }
    check("gate-off-no-repeated-summary-or-interface-detail-written",
        readRows(L"native-lifecycle.log").size()==4 && readRows(L"native-detail.log").size()==124);
    json enumerationFailure = absent; enumerationFailure["enumerationComplete"]=false; enumerationFailure["enumerationError"]=5;
    appendNativeLifecycleLog("rog.hid-discovery-summary", enumerationFailure);
    appendNativeLifecycleLog("rog.hid-discovery-summary", enumerationFailure);
    enumerationFailure["enumerationError"]=6; appendNativeLifecycleLog("rog.hid-discovery-summary", enumerationFailure);
    appendNativeLifecycleLog("rog.hid-discovery-summary", absent);
    ++g_powerGeneration; appendNativeLifecycleLog("rog.hid-discovery-summary", absent);
    check("gate-off-keeps-failure-change-recovery-and-new-power-generation",
        readRows(L"native-lifecycle.log").size()==8 && readRows(L"native-detail.log").size()==124);
    reset(root, L"rear-observation-routing"); g_domainLogEnabled[1] = false;
    appendNativeLifecycleLog("msi-rear-source-observation", {{"snapshotReason","context-change"},{"rearDemand",true}});
    for(int i=0;i<48;++i) appendNativeLifecycleLog("msi-rear-source-observation", {{"snapshotReason","raw-input-change"},{"dinputSamples",i}});
    check("rear-context-stays-low-but-activity-counters-off-stay-out",
        readRows(L"native-lifecycle.log").size()==1 && readRows(L"native-detail.log").empty());
    g_domainLogEnabled[1] = true;
    for(int i=0;i<48;++i) appendNativeLifecycleLog("msi-rear-source-observation", {{"snapshotReason","raw-input-change"},{"dinputSamples",i}});
    appendNativeLifecycleLog("msi-rear-source-observation", {{"snapshotReason","context-change"},{"rearDemand",false}});
    flushNativeLifecycleLogs(false);
    const auto activityRows = readRows(L"native-detail.log");
    check("rear-activity-on-retains-all-48-counter-receipts-in-detail",
        readRows(L"native-lifecycle.log").size()==2 && activityRows.size()==48 &&
        activityRows.front()["dinputSamples"]==0 && activityRows.back()["dinputSamples"]==47);
    reset(root, L"dedicated-memory-capability");
    json memoryFailed{{"schema",1},{"ok",false},{"available",false},{"reason","[adapter.binding] fixture unread"}};
    for(int i=0;i<500;++i)record_gpu_dedicated_memory_capability(memoryFailed);
    auto capabilityRows=readRows(L"native-lifecycle.log");
    check("gpu-capability-default-logging-retains-one-first-failed-read",
        !g_domainLogEnabled[1] && capabilityRows.size()==1 && readRows(L"native-detail.log").empty() &&
        capabilityRows[0]["event"]=="machine-capability" && capabilityRows[0]["capability"]=="gpu.dedicatedMemory");
    check("gpu-capability-failed-read-is-unconfirmed-with-model-and-reason-not-invented-zero",
        capabilityRows[0]["status"]=="unconfirmed" && capabilityRows[0]["available"]==false &&
        capabilityRows[0]["deviceClass"]=="HandheldCompanion.Devices.ClawCG3EM" &&
        capabilityRows[0]["reason"]==memoryFailed["reason"] && !capabilityRows[0].contains("dedicatedBytes"));
    json memoryRead{{"schema",1},{"ok",true},{"available",true},{"reason",""},{"provider","intel-gmm"},
        {"capacityRead",true},{"dedicatedBytes",4096ull*1024*1024},{"adapterName","Intel fixture"},
        {"driverVersion","fixture"},{"bindingSource","kernel-pnp"},{"selected","auto"},
        {"options",json::array({json{{"id","auto"}}})}};
    for(int i=0;i<500;++i)record_gpu_dedicated_memory_capability(memoryRead);
    capabilityRows=readRows(L"native-lifecycle.log");
    check("gpu-capability-recovery-retains-real-capacity-binding-and-options-once",
        capabilityRows.size()==2 && capabilityRows[1]["available"]==true &&
        capabilityRows[1]["dedicatedBytes"]==memoryRead["dedicatedBytes"] &&
        capabilityRows[1]["capacityRead"]==true && capabilityRows[1]["bindingSource"]=="kernel-pnp" &&
        capabilityRows[1]["options"]==memoryRead["options"]);
    memoryRead["dedicatedBytes"]=0;record_gpu_dedicated_memory_capability(memoryRead);
    capabilityRows=readRows(L"native-lifecycle.log");
    check("gpu-capability-confirmed-zero-remains-an-actual-zero",
        capabilityRows.size()==3 && capabilityRows[2]["available"]==true && capabilityRows[2]["dedicatedBytes"]==0);
    g_domainLogEnabled[1]=true;record_gpu_dedicated_memory_capability(memoryRead);g_domainLogEnabled[1]=false;
    check("gpu-capability-detail-toggle-does-not-duplicate-unchanged-capability",readRows(L"native-lifecycle.log").size()==3);
    record_gpu_dedicated_memory_capability(memoryFailed);capabilityRows=readRows(L"native-lifecycle.log");
    check("gpu-capability-new-read-failure-does-not-retain-stale-capacity",
        capabilityRows.size()==4 && capabilityRows[3]["available"]==false && !capabilityRows[3].contains("dedicatedBytes"));
    if(argc==3){
        reset(root,L"dedicated-memory-production-responses");kLogMaxBytes=5*1024*1024;
        std::ifstream input(fspath::path(argv[2]),std::ios::binary);const auto windows=json::parse(input);
        bool preserved=true,deduplicated=true;
        for(const auto& response:windows["responses"]){
            record_gpu_dedicated_memory_capability(response);const auto rows=readRows(L"native-lifecycle.log");
            if(rows.empty()){preserved=false;continue;}
            const auto& row=rows.back();const bool available=response.value("ok",false)&&response.value("available",false);
            preserved=preserved&&row["event"]=="machine-capability"&&row["available"]==available&&
                row["status"]==(available?"available":"unconfirmed")&&row["reason"]==response["reason"]&&
                row["deviceClass"]=="HandheldCompanion.Devices.ClawCG3EM";
            for(const auto* key:{"readerRevision","dedicatedBytes","capacityRead","bindingSource","classificationSource","selected","verification","bootId","savedBootId","bootComparison","pendingOption","options","readDiagnostics","writeDiagnostics"})
                preserved=preserved&&(row.contains(key)==response.contains(key))&&(!response.contains(key)||row[key]==response[key]);
            record_gpu_dedicated_memory_capability(response);
            deduplicated=deduplicated&&readRows(L"native-lifecycle.log").size()==rows.size();
        }
        check("gpu-capability-production-Windows-results-preserve-actual-readback-and-failure-with-detail-off",preserved&&readRows(L"native-detail.log").empty());
        check("gpu-capability-production-Windows-results-repeat-does-not-duplicate",deduplicated);
    }
    reset(root, L"oem-wake-pass-routing"); g_domainLogEnabled[1] = true;
    ++g_powerGeneration;
    appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-started"},{"generation",77}});
    for(int i=0;i<10;++i) appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-pass"},
        {"ok",true},{"requestedEpoch",i+3},{"durationMs",i*2},{"intervalMs",2000}});
    appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-ended"},{"generation",77}});
    flushNativeLifecycleLogs(false);
    auto wakeDetail = readRows(L"native-detail.log");
    check("wake-guard-ten-successes-keep-start-first-end-and-nine-detail",
        readRows(L"native-lifecycle.log").size()==3 && wakeDetail.size()==9 &&
        wakeDetail.front()["requestedEpoch"]==4 && wakeDetail.back()["durationMs"]==18);
    g_domainLogEnabled[1] = false;
    appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-pass"},{"ok",true},{"requestedEpoch",90},{"durationMs",70},{"intervalMs",2000}});
    check("wake-guard-unchanged-success-off-no-extra-row",
        readRows(L"native-lifecycle.log").size()==3 && readRows(L"native-detail.log").size()==9);
    for(int i=0;i<2;++i) appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-pass"},{"ok",false},{"requestedEpoch",91+i},{"intervalMs",2000}});
    appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-pass"},{"ok",true},{"intervalMs",2000}});
    check("wake-guard-failures-and-recovery-resident-with-detail-off",readRows(L"native-lifecycle.log").size()==6);
    appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-pass"},{"intervalMs",2000}});
    appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-pass"},{"ok","unknown"},{"intervalMs",2000}});
    appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-pass"},{"ok",true},{"intervalMs",2000}});
    ++g_powerGeneration;
    appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-pass"},{"ok",true},{"intervalMs",2000}});
    check("wake-guard-unknown-and-new-generation-cannot-disappear",readRows(L"native-lifecycle.log").size()==10);
    for(const auto* state:{"task-disabled","process-stopped","process-stop-failed","retry-exhausted","applied"})
        for(int i=0;i<2;++i) appendNativeLifecycleLog("oem-vendor-stack",{{"state",state},{"requestedEpoch",i}});
    for(int i=0;i<4;++i) appendNativeLifecycleLog("late-hide-complete",{{"queueToken",i},{"outcome","skipped"},{"decision","refused-window"}});
    check("guard-action-and-late-hide-transaction-receipts-remain-resident",readRows(L"native-lifecycle.log").size()==24);
    appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-started"},{"generation",77}});
    appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-pass"},{"ok",true},{"intervalMs",2000}});
    check("new-guard-start-retains-first-pass-even-in-same-power-generation",readRows(L"native-lifecycle.log").size()==26);

    reset(root, L"frame-observation-lanes"); g_domainLogEnabled[1] = true;
    appendNativeLifecycleLog("input-host-frame-evidence", {{"snapshotReason","state-change"},{"sequence",1}});
    appendNativeLifecycleLog("input-host-frame-evidence", {{"snapshotReason","periodic"},{"sequence",2}});
    appendNativeLifecycleLog("input-host-frame-evidence", {{"snapshotReason","unknown"},{"sequence",3}});
    flushNativeLifecycleLogs();
    check("frame-periodic-detail-state-change-and-unknown-retained", readRows(L"native-lifecycle.log").size()==2 &&
        readRows(L"native-detail.log").size()==1 && readRows(L"native-detail.log")[0]["sequence"]==2);
    g_domainLogEnabled[1] = false;
    appendNativeLifecycleLog("input-host-frame-evidence", {{"snapshotReason","periodic"},{"sequence",4}});
    flushNativeLifecycleLogs();
    check("frame-periodic-off-adds-no-row", readRows(L"native-lifecycle.log").size()==2 && readRows(L"native-detail.log").size()==1);

    reset(root, L"interrupted-tail");
    const auto tailPath=fspath::path(g_testRoot)/L"native-lifecycle.log";
    const std::string broken="{\"oldPid\":7,\"unfinished\":";
    {std::ofstream seed(tailPath,std::ios::binary);seed<<broken;}
    appendNativeLifecycleLog("app-exit-ipc", {{"nextPid",8}});
    std::ifstream tailInput(tailPath,std::ios::binary);std::string retained,valid;
    std::getline(tailInput,retained);std::getline(tailInput,valid);
    check("restart-retains-tail-bytes-and-keeps-next-row-parseable",retained==broken && json::parse(valid)["nextPid"]==8);
    tailInput.close();g_nativeLifecycleLog.close();g_nativeLifecycleLog.clear();
    appendNativeLifecycleLog("app-exit-ipc", {{"nextPid",9}});
    std::ifstream again(tailPath);unsigned tailLines=0;for(std::string line;std::getline(again,line);)++tailLines;
    check("clean-reopen-does-not-add-extra-boundary",tailLines==3);
    again.close();
    reset(root, L"valid-row-without-newline");
    {std::ofstream seed(fspath::path(g_testRoot)/L"native-lifecycle.log",std::ios::binary);seed<<"{\"oldPid\":7}";}
    appendNativeLifecycleLog("app-exit-ipc", {{"nextPid",8}});
    check("valid-old-row-without-newline-and-new-row-both-retained",readRows(L"native-lifecycle.log").size()==2);

    reset(root, L"invalid-destination"); g_testRoot += L"\\missing\\nested";
    bool survived = true;
    try { appendNativeLifecycleLog("app-exit-ipc", {{"fixture", true}}); } catch(...) { survived = false; }
    check("writer-failure-never-changes-caller-control-flow", survived && !g_nativeLifecycleLog.is_open());
    reset(root,L"screen-off-fast-producer");kLogMaxBytes=5*1024*1024;++g_powerGeneration;
    json borrowed={{"available",true},{"cursorFallbackQueries",1}};
    logScreenTouchpadObservation("screenTouchpads.get","steamdeck",borrowed);
    for(int i=0;i<100;++i){borrowed["cursorFallbackQueries"]=i;logScreenTouchpadObservation("screenTouchpads.get","steamdeck",borrowed);}
    check("screen-off-fast-producer-emits-only-first",readRows(L"native-lifecycle.log").size()==1);
    logScreenTouchpadObservation("screenTouchpads.set","steamdeck",borrowed);logScreenTouchpadObservation("screenTouchpads.set","steamdeck",borrowed);
    check("screen-fast-path-preserves-both-settings-actions",readRows(L"native-lifecycle.log").size()==3);

    reset(root,L"screen-persona-memos");kLogMaxBytes=5*1024*1024;
    for(bool detailed:{false,true}){
        reset(root,detailed?L"screen-personas-on":L"screen-personas-off");kLogMaxBytes=5*1024*1024;
        g_domainLogEnabled[1]=detailed;++g_powerGeneration;
        const std::array<std::string,4> personas={"disabled","steamdeck","dualsense-edge","elite"};
        for(const auto& persona:personas)logScreenTouchpadObservation("screenTouchpads.get",persona,borrowed);
        for(int i=0;i<100;++i){borrowed["cursorFallbackQueries"]=i;logScreenTouchpadObservation("screenTouchpads.get",personas[i%4],borrowed);}
        check(detailed?"screen-alternating-on-keeps-four-resident":"screen-alternating-off-keeps-four-resident",readRows(L"native-lifecycle.log").size()==4);
        check(detailed?"screen-alternating-on-repeats-in-detail":"screen-alternating-off-no-detail",readRows(L"native-detail.log").size()==(detailed?100:0));
        logScreenTouchpadObservation("screenTouchpads.saveProfiles","batch",borrowed);logScreenTouchpadObservation("screenTouchpads.saveProfiles","batch",borrowed);
        check(detailed?"screen-identical-batch-actions-retained-on":"screen-identical-batch-actions-retained-off",readRows(L"native-lifecycle.log").size()==6);
    }

    for(bool detailed:{false,true}){
        reset(root,detailed?L"raw-identity-on":L"raw-identity-off");kLogMaxBytes=5*1024*1024;
        g_domainLogEnabled[1]=detailed;++g_powerGeneration;
        HidHidePhysicalIdentity first,second;
        first.sourcePath="collection-one";first.deviceInstanceId="instance-one";first.baseContainerDeviceInstanceId="shared-container";
        second=first;second.sourcePath="collection-two";second.deviceInstanceId="instance-two";
        for(int i=0;i<100;++i){hidHideLogPhysicalIdentityObservation(first,1);hidHideLogPhysicalIdentityObservation(second,1);}
        check("alternating-collections-are-two-identity-observations-not-200-resident-edges",readRows(L"native-lifecycle.log").size()==2);
        check("alternating-collection-trajectory-follows-detail-gate",readRows(L"native-detail.log").size()==(detailed?198u:0u));
        auto count=readRows(L"native-lifecycle.log").size();
        first.baseContainerDeviceInstanceId="new-container";hidHideLogPhysicalIdentityObservation(first,1);
        hidHideLogPhysicalIdentityObservation(first,2);++g_powerGeneration;hidHideLogPhysicalIdentityObservation(first,2);
        first.resolved=false;hidHideLogPhysicalIdentityObservation(first,2);hidHideLogPhysicalIdentityObservation(first,2);
        first.resolved=true;hidHideLogPhysicalIdentityObservation(first,2);
        appendNativeLifecycleLog("physical-controller-identity",{{"resolved",true},{"sourcePath","legacy-without-label"}});
        check("identity-fields-topology-power-failures-and-legacy-records-stay-resident",readRows(L"native-lifecycle.log").size()==count+7);
        const auto beforeStable=readRows(L"native-lifecycle.log").size(),beforeDetail=readRows(L"native-detail.log").size();
        for(int i=0;i<64;++i)hidHideLogPhysicalIdentityObservation(first,2,false);
        check("unchanged-selected-source-does-not-add-packet-level-diagnostics",readRows(L"native-lifecycle.log").size()==beforeStable&&readRows(L"native-detail.log").size()==beforeDetail);
        hidHideLogPhysicalIdentityObservation(first,3,false);
        check("new-topology-is-logged-even-without-selected-source-change",readRows(L"native-lifecycle.log").size()==beforeStable+1);

    }

        // Field traces interleave consuming/dequeue/countdown/coalesced receipts.
        // A dequeue is an observation of the same queued request, not a new one.
        for(bool detailed : {false,true}) {
            reset(root,detailed?L"queued-wait-detail-on":L"queued-wait-detail-off");kLogMaxBytes=5*1024*1024;
            g_domainLogEnabled[1]=detailed;++g_powerGeneration;
            json consuming={{"action","consuming"},{"requestId",3},{"originalDeadline",10000},{"persona","steamdeck"}};
            json dequeue={{"beat","dequeue"},{"reason","initial-target-admission"},{"requestedAtDequeue","steamdeck"},{"waitedMs",4000},{"deferCount",1}};
            json settle={{"reason","initial-target-admission"},{"policy","queue-not-reject-coalesce-on-next-round"},{"deferMs",1000},{"settleMs",1000}};
            json merged={{"outcome","coalesced"},{"coalesced",true},{"queueToken",4},{"runId","field-run"},{"hostGen",2},{"sourceGen",390},{"recordGen",7}};
            for(int i=0;i<32;++i){
                dequeue["waitedMs"]=4000+i*8;dequeue["deferCount"]=i+1;settle["deferMs"]=1000-i*8;
                appendNativeLifecycleLog("input-host-player-start-work",consuming);
                appendNativeLifecycleLog("input-host-switch-audit",dequeue);
                appendNativeLifecycleLog("input-host-switch-settle",settle);
                appendNativeLifecycleLog("late-hide-enqueue",merged);
            }
            flushNativeLifecycleLogs(true);
            check("interleaved-queue-wait-only-four-first-observations-resident",readRows(L"native-lifecycle.log").size()==4);
            check("interleaved-queue-wait-full-repeats-follow-detail-gate",readRows(L"native-detail.log").size()==(detailed?124u:0u));
            auto count=readRows(L"native-lifecycle.log").size();
            consuming["requestId"]=4;appendNativeLifecycleLog("input-host-player-start-work",consuming);
            merged["queueToken"]=5;appendNativeLifecycleLog("late-hide-enqueue",merged);
            dequeue["requestedAtDequeue"]="elite";appendNativeLifecycleLog("input-host-switch-audit",dequeue);
            settle["deferMs"]=999;appendNativeLifecycleLog("input-host-switch-settle",settle);
            check("request-token-target-and-new-countdown-window-remain-resident",readRows(L"native-lifecycle.log").size()==count+4);
            appendNativeLifecycleLog("input-host-switch-audit",{{"beat","request"},{"requestId",5}});
            count=readRows(L"native-lifecycle.log").size();
            appendNativeLifecycleLog("input-host-player-start-work",consuming);
            appendNativeLifecycleLog("input-host-switch-audit",dequeue);
            appendNativeLifecycleLog("input-host-switch-settle",settle);
            check("real-request-resets-wait-evidence",readRows(L"native-lifecycle.log").size()==count+3);
            ++g_powerGeneration;count=readRows(L"native-lifecycle.log").size();
            appendNativeLifecycleLog("input-host-player-start-work",consuming);
            appendNativeLifecycleLog("input-host-switch-audit",dequeue);
            appendNativeLifecycleLog("input-host-switch-settle",settle);
            appendNativeLifecycleLog("late-hide-enqueue",merged);
            check("power-generation-resets-all-wait-evidence",readRows(L"native-lifecycle.log").size()==count+4);
            count=readRows(L"native-lifecycle.log").size();
            auto rejected=consuming;rejected["action"]="rejected";
            auto accepted=merged;accepted["outcome"]="accepted";accepted["coalesced"]=false;
            auto refused=merged;refused["outcome"]="refused";refused["coalesced"]=false;
            for(int i=0;i<2;++i){
                appendNativeLifecycleLog("input-host-player-start-work",rejected);
                appendNativeLifecycleLog("late-hide-enqueue",accepted);
                appendNativeLifecycleLog("late-hide-enqueue",refused);
                appendNativeLifecycleLog("input-host-switch-audit",{{"beat","settled"},{"result","ok"}});
                appendNativeLifecycleLog("late-hide-complete",{{"queueToken",5},{"outcome","completed"}});
            }
            check("rejection-acceptance-refusal-and-terminal-actions-always-resident",readRows(L"native-lifecycle.log").size()==count+10);
            settle["deferMs"]="unavailable";count=readRows(L"native-lifecycle.log").size();
            appendNativeLifecycleLog("input-host-switch-settle",settle);
            appendNativeLifecycleLog("input-host-switch-settle",settle);
            check("invalid-countdown-never-suppressed-as-telemetry",readRows(L"native-lifecycle.log").size()==count+2);
            dequeue["waitedMs"]=-1;count=readRows(L"native-lifecycle.log").size();
            appendNativeLifecycleLog("input-host-switch-audit",dequeue);
            appendNativeLifecycleLog("input-host-switch-audit",dequeue);
            check("invalid-dequeue-counter-never-suppressed-as-telemetry",readRows(L"native-lifecycle.log").size()==count+2);
            settle["deferMs"]=500;appendNativeLifecycleLog("input-host-switch-settle",settle);
            settle["futureUnknownField"]=true;count=readRows(L"native-lifecycle.log").size();
            appendNativeLifecycleLog("input-host-switch-settle",settle);
            check("unknown-wait-fields-remain-semantic",readRows(L"native-lifecycle.log").size()==count+1);
        }

    for (bool detailed : {false,true}) {
        reset(root,detailed?L"new-observation-on":L"new-observation-off");
        kLogMaxBytes=5*1024*1024; ++g_powerGeneration;
        g_domainLogEnabled[1]=detailed;
        const json usage={{"supported",true},{"runtime",{{"keyboardActive",true}}}};
        for(int i=0;i<64;++i) appendNativeLifecycleLog("oem-key-usage-query",usage);
        auto base=readRows(L"native-lifecycle.log"), detail=readRows(L"native-detail.log");
        check(detailed?"oem-repeats-retained-only-in-detail":"oem-off-only-first-resident",base.size()==1&&detail.size()==(detailed?63:0));
        check("writer-borrows-business-state-without-mutation",!usage.contains("time")&&!usage.contains("pid"));
        auto changed=usage;changed["runtime"]["keyboardActive"]=false;
        appendNativeLifecycleLog("oem-key-usage-query",changed);
        ++g_powerGeneration;appendNativeLifecycleLog("oem-key-usage-query",changed);
        check("oem-source-change-and-power-edge-survive-off",readRows(L"native-lifecycle.log").size()==3);
        json screen={{"callSite","screenTouchpads.get"},{"state",{{"available",true},{"cursorFallbackQueries",1},
            {"cursorDeckReportPulseTick",1},{"cursorTransitionTrace",{{"entries",json::array({{{"samples",1},{"lastTick",1}}})},{"placements",{{"unchanged",1}}}}}}}};
        appendNativeLifecycleLog("screen-touchpads-state",screen);
        screen["state"]["cursorFallbackQueries"]=2;screen["state"]["cursorDeckReportPulseTick"]=2;
        screen["state"]["cursorTransitionTrace"]["entries"][0]["samples"]=2;
        appendNativeLifecycleLog("screen-touchpads-state",screen);
        auto rowsBefore=readRows(L"native-lifecycle.log").size();
        check("screen-counter-and-trajectory-samples-not-resident",rowsBefore==4);
        screen["state"]["available"]=false;appendNativeLifecycleLog("screen-touchpads-state",screen);
        screen["state"]["futureUnknownState"]=1;appendNativeLifecycleLog("screen-touchpads-state",screen);
        check("screen-availability-and-unknown-fields-preserved",readRows(L"native-lifecycle.log").size()==rowsBefore+2);
        screen["callSite"]="screenTouchpads.set";
        appendNativeLifecycleLog("screen-touchpads-state",screen);appendNativeLifecycleLog("screen-touchpads-state",screen);
        check("identical-settings-actions-both-remain-resident",readRows(L"native-lifecycle.log").size()==rowsBefore+4);
        screen["callSite"]="screenTouchpads.get";
        screen["state"]["cursorFallbackQueries"]="unavailable";
        const auto beforeSchema=readRows(L"native-lifecycle.log").size();
        appendNativeLifecycleLog("screen-touchpads-state",screen);
        screen["state"].erase("cursorFallbackQueries");appendNativeLifecycleLog("screen-touchpads-state",screen);
        check("wrong-type-and-missing-counter-are-not-hidden",readRows(L"native-lifecycle.log").size()==beforeSchema+2);
        const json work={{"requestId",7},{"originalDeadline",1000},{"action","consuming"},{"persona","elite"}};
        const auto beforeWork=readRows(L"native-lifecycle.log").size();
        for(int i=0;i<59;++i)appendNativeLifecycleLog("input-host-player-start-work",work);
        check("player-consuming-burst-only-first-resident",readRows(L"native-lifecycle.log").size()==beforeWork+1);
        auto failure=work;failure["action"]="rejected";
        appendNativeLifecycleLog("input-host-player-start-work",failure);
        check("player-failure-is-not-consumption-noise",readRows(L"native-lifecycle.log").size()==beforeWork+2);
        json throttle={{"beat","throttled"},{"reason","initial-target-admission"},{"policy","one-admission-attempt-per-2s"},{"waitMs",1}};
        const auto beforeThrottle=readRows(L"native-lifecycle.log").size();
        for(int i=0;i<10;++i){throttle["waitMs"]=i;appendNativeLifecycleLog("input-host-switch-audit",throttle);}
        check("wait-ms-alone-does-not-create-resident-edge",readRows(L"native-lifecycle.log").size()==beforeThrottle+1);
        appendNativeLifecycleLog("input-host-switch-audit",{{"beat","request"},{"requestId",8}});
        appendNativeLifecycleLog("input-host-switch-audit",throttle);
        check("new-request-restores-first-throttle-evidence",readRows(L"native-lifecycle.log").size()==beforeThrottle+3);

        json facts={{"generation",static_cast<unsigned long long>(g_powerGeneration.load())},{"pid",27},{"status","current"},
            {"foregroundRole","original-game"},{"nativePrimary",{{"status","matched"},{"log",{{"copiedBytes",1},{"originalBytes",1}}}}}};
        recordInspection(g_powerGeneration.load(),"observation",facts);
        facts["nativePrimary"]["log"]["copiedBytes"]=2;facts["nativePrimary"]["log"]["originalBytes"]=2;
        for(int i=0;i<22;++i)recordInspection(g_powerGeneration.load(),"observation",facts);
        check("sleep-log-growth-not-a-resident-state-change",readRows(L"sleep-facts.log").size()==1);
        recordInspection(g_powerGeneration.load(),"input-preflight",facts);
        recordInspection(g_powerGeneration.load(),"input-postflight",facts);
        check("sleep-identical-action-before-after-always-retained",readRows(L"sleep-facts.log").size()==3);
        facts["nativePrimary"]["log"]["truncated"]=true;
        recordInspection(g_powerGeneration.load(),"observation",facts);
        check("sleep-read-coverage-loss-is-real-evidence",readRows(L"sleep-facts.log").size()==4);
        facts["foregroundRole"]="steam-ui";recordInspection(g_powerGeneration.load(),"observation",facts);
        check("sleep-focus-change-is-real-evidence",readRows(L"sleep-facts.log").size()==5);
        const auto beforeDecky=readRows(L"native-lifecycle.log").size();
        for(int i=0;i<20;++i)deckySidebarDiagnostic("mirror-snapshot-queued",{{"generation",static_cast<unsigned long long>(g_powerGeneration.load())},{"ready",true},{"revision",i}},false);
        check("decky-snapshot-revisions-are-detailed-not-state-edges",readRows(L"decky-sidebar.log").size()==1&&readRows(L"native-lifecycle.log").size()==beforeDecky);
        deckySidebarDiagnostic("mirror-snapshot-queued",{{"generation",static_cast<unsigned long long>(g_powerGeneration.load())},{"ready",false},{"revision",20}},false);
        deckySidebarDiagnostic("runtime",{{"phase","failed"},{"reason",detailed?"fixture-error-on":"fixture-error-off"}},false);
        check("decky-ready-transition-and-runtime-error-survive",readRows(L"decky-sidebar.log").size()==3);
        ++g_powerGeneration;
        deckySidebarDiagnostic("mirror-snapshot-queued",{{"generation",static_cast<unsigned long long>(g_powerGeneration.load())-1},{"ready",false},{"revision",20}},false);
        check("decky-new-native-power-edge-survives-identical-payload",readRows(L"decky-sidebar.log").size()==4);
        bool survived=true;try{recordInspection(g_powerGeneration.load(),"observation",json(5));}catch(...){survived=false;}
        check("bad-diagnostic-projection-does-not-escape",survived);
    }
    reset(root,L"decky-sink-recovery");g_testRoot+=L"\\missing";++g_powerGeneration;
    deckySidebarDiagnostic("mirror-snapshot-queued",{{"generation",999},{"ready",true},{"revision",1}},false);
    fspath::create_directories(g_testRoot);
    deckySidebarDiagnostic("mirror-snapshot-queued",{{"generation",999},{"ready",true},{"revision",1}},false);
    check("decky-failed-write-does-not-consume-first-evidence",readRows(L"decky-sidebar.log").size()==1);

    {
    reset(root,L"model-adaptation");kLogMaxBytes=1024*1024;
    g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayerApex";
    g_machineIdentity.manufacturer="ONE-NETBOOK";
    g_machineIdentity.systemProduct="ONEXPLAYER APEX";
    const auto beforeSignals=modelAdaptationSignals().status()["knownTypes"].get<unsigned>();
    std::array<BYTE,64> raw{};raw[0]=0xB2;raw[1]=raw[62]=0x3F;
    LARGE_INTEGER perfFreq{},perfBegin{},perfEnd{};QueryPerformanceFrequency(&perfFreq);QueryPerformanceCounter(&perfBegin);
    for(unsigned i=0;i<50000;++i){raw[6]=static_cast<BYTE>(0x21+i%3);raw[12]=1;raw[17]=static_cast<BYTE>(i);
        modelAdaptationOnexReport(raw.data(),raw.size(),true);raw[12]=0;
        modelAdaptationOnexReport(raw.data(),raw.size(),true);}
    QueryPerformanceCounter(&perfEnd);
    const auto producerUs=(perfEnd.QuadPart-perfBegin.QuadPart)*1000000/perfFreq.QuadPart;
    check("adaptation-50000-press-release-three-types-no-count-key",modelAdaptationSignals().status()["knownTypes"]==beforeSignals+3);
    check("adaptation-producer-does-not-write-files",!fspath::exists(fspath::path(g_testRoot)/L"model-adaptation.log"));
    modelAdaptationFlush();auto adaptation=readRows(L"model-adaptation.log");
    check("adaptation-detail-off-exactly-three-persistent-types",adaptation.size()==3);
    check("adaptation-sample-and-save-clocks-have-explicit-scope",adaptation[0].contains("timestampUtc")&&adaptation[0]["timestampUtc"].get<std::string>().back()=='Z'&&adaptation[0]["firstSample"].contains("capturedTickMs")&&!adaptation[0].contains("firstObservedAt"));
    bool rawReplay=true;for(const auto& row:adaptation){const auto& sample=row["firstSample"];
        const std::string hex=sample["firstRawReportHex"];
        std::array<BYTE,64> recovered{};for(unsigned i=0;i<64;++i)recovered[i]=static_cast<BYTE>(std::stoul(hex.substr(i*2,2),nullptr,16));
        const auto* frame=ymcc::capabilities::oem::onex::inputFrame(recovered.data(),recovered.size());
        rawReplay=rawReplay&&frame&&frame[6]==row["uniqueKey"]["identity"]["code"]&&sample["consumerActionProven"]==false;
        onexObserved=0;onexBackParse(recovered.data(),recovered.size(),71);
        rawReplay=rawReplay&&onexObserved==row["uniqueKey"]["identity"]["canonicalMask"].get<uint64_t>();
    }
    check("adaptation-first-raw-sample-replays-through-pinned-decoder",rawReplay);
    raw[0]=0xB8;raw[6]=0x99;modelAdaptationOnexReport(raw.data(),raw.size(),true);
    raw[0]=0xB2;raw[62]=0;modelAdaptationOnexReport(raw.data(),raw.size(),true);modelAdaptationFlush();
    check("adaptation-status-and-malformed-not-button-types",readRows(L"model-adaptation.log").size()==3);
    raw[62]=0x3F;modelAdaptationOnexReport(raw.data(),raw.size(),true);modelAdaptationFlush();
    adaptation=readRows(L"model-adaptation.log");
    check("adaptation-unknown-code-retained-before-mapping",adaptation.size()==4&&adaptation.back()["firstSample"]["mappingFound"]==false&&adaptation.back()["uniqueKey"]["identity"]["code"]==0x99);
    {std::array<BYTE,256> wide{};wide[0]=7;wide[1]=0xB2;wide[2]=wide[254]=0x3F;wide[7]=0x77;
    modelAdaptationOnexReport(wide.data(),wide.size(),true);modelAdaptationFlush();auto wideRows=readRows(L"model-adaptation.log");
    check("adaptation-complete-non64-report-retains-report-id",wideRows.size()==5&&wideRows.back()["firstSample"]["rawBytes"]==256&&wideRows.back()["firstSample"]["firstRawReportHex"].get<std::string>().size()==512&&wideRows.back()["uniqueKey"]["identity"]["reportId"]==7);}
    ++g_powerGeneration;raw[6]=0x22;modelAdaptationOnexReport(raw.data(),raw.size(),true);modelAdaptationFlush();
    check("adaptation-power-generation-not-new-type",readRows(L"model-adaptation.log").size()==5);
    const auto path=fspath::path(g_testRoot)/L"model-adaptation.log";
    ymcc::adaptlog::Journal reopened;
    const auto& first=adaptation.front();
    check("adaptation-reopen-dedup-ignores-first-sample-and-time",reopened.record(path,first["uniqueKey"]["model"],"oem-signal-type",first["uniqueKey"]["identity"],{{"pressedAtFirstWitness",false}},"different-time")&&readRows(L"model-adaptation.log").size()==5);
    auto changedModel=first["uniqueKey"]["model"];changedModel["deviceClass"]="different-model";
    auto changedRule=first["uniqueKey"]["identity"];changedRule["canonicalMask"]=1;
    check("adaptation-new-model-and-rule-not-deduped",reopened.record(path,changedModel,"oem-signal-type",first["uniqueKey"]["identity"],json::object(),"time")&&reopened.record(path,first["uniqueKey"]["model"],"oem-signal-type",changedRule,json::object(),"time")&&readRows(L"model-adaptation.log").size()==7);
    json catalog={{"schemaVersion",1},{"familyId","onex"},{"deviceClass","APEX"},{"captureClass","actual-consumer"},{"bindingSource","current-total-catalog"},{"keys",json::array({{{"keyId","l4"},{"physicalKeyIds",json::array({"l4"})}}})},{"runtime",{{"keyboardActive",true}}}};
    const auto originalCatalog=catalog;
    for(unsigned i=0;i<5000;++i){catalog["generation"]=i;modelAdaptationObserveRecord("oem-key-usage-query",catalog);}
    modelAdaptationFlush();adaptation=readRows(L"model-adaptation.log");
    check("adaptation-catalog-5000-queries-one-stable-fact",adaptation.size()==8&&adaptation.back()["uniqueKey"]["identity"]["captureClass"]=="actual-consumer");
    catalog.erase("generation");check("adaptation-observer-does-not-mutate-business-payload",catalog==originalCatalog);
    auto binding=json{{"ok",true},{"initProfile",3},{"path","fixture-hid"},{"inputReportBytes",65},{"outputReportBytes",64}};
    modelAdaptationObserveRecord("onex-back-bind",binding);
    modelAdaptationObserveRecord("msi-wmi-source-capability",{{"availability","known-absent"},{"eventClass","ROOT\\WMI:MSI_Event"},{"beginHresult",-1},{"authorityGranted",false}});
    modelAdaptationFlush();adaptation=readRows(L"model-adaptation.log");
    check("adaptation-actual-bind-and-wmi-field-projections",adaptation.size()==10&&adaptation[8]["uniqueKey"]["identity"]["inputReportBytes"]==65&&adaptation[9]["uniqueKey"]["identity"]["availability"]=="known-absent");
    for(const bool detail:{false,true}){
        g_domainLogEnabled[1]=detail;
        appendNativeLifecycleLog("onex-back-buttons",{{"id",0x22},{"down",true}});
        appendNativeLifecycleLog("onex-oem-input-edge",{{"mask",kOemBitR4}});
        appendNativeLifecycleLog("rog-oem-key",{{"code",1}});
        appendNativeLifecycleLog("oem-producer-edge",{{"status","queued"},{"reason","state-transition"}});
        appendNativeLifecycleLog("msi-wmi-key",{{"reason","queued"},{"accepted",true},{"posted",true}});
        appendNativeLifecycleLog("oem-producer-edge",{{"status","refused"},{"reason","fixture-no-binding"}});
        appendNativeLifecycleLog("msi-wmi-key",{{"reason","post-failed"},{"accepted",true},{"posted",false}});
    }
    auto resident=readRows(L"native-lifecycle.log"),detailed=readRows(L"native-detail.log");
    check("adaptation-key-trajectories-never-resident",resident.size()==4&&detailed.size()==5);
    check("adaptation-rejection-and-post-failure-resident-on-and-off",resident[0]["status"]=="refused"&&resident[1]["posted"]==false);
    ymcc::adaptlog::Signals bounded;
    for(unsigned i=0;i<ymcc::adaptlog::signalCapacity+1;++i){ymcc::adaptlog::Signal value;value.type.code=i;bounded.offer(value);}
    check("adaptation-queue-cap-no-eviction-explicit-omission",bounded.status()["knownTypes"]==ymcc::adaptlog::signalCapacity&&bounded.status()["capacityDrops"]==1&&bounded.status()["eviction"]==false);
    ymcc::adaptlog::Journal ledger;
    const auto boundedPath=fspath::path(g_testRoot)/L"bounded.log";
    bool committed=true;for(unsigned i=0;i<ymcc::adaptlog::journalCapacity;++i)committed&=ledger.record(boundedPath,json::object(),"test",{{"code",i}},json::object(),"time");
    const auto boundedBytes=fspath::file_size(boundedPath);
    check("adaptation-ledger-cap-preserves-original-types",committed&&!ledger.record(boundedPath,json::object(),"test",{{"code",999}},json::object(),"time")&&ledger.status()["budgetFull"]==true&&fspath::file_size(boundedPath)==boundedBytes);
    ymcc::adaptlog::Journal limited(10000,4,1024);const auto poison=fspath::path(g_testRoot)/L"poison.log";
    check("adaptation-permanent-oversize-does-not-block-later-types",limited.recordResult(poison,json::object(),"test",{{"code",1}},{{"tooLarge",std::string(2048,'x')}},"time")==ymcc::adaptlog::RecordResult::omitted&&limited.record(poison,json::object(),"test",{{"code",2}},json::object(),"time")&&limited.status()["budgetOmissions"]==1);
    const auto broken=fspath::path(g_testRoot)/L"missing"/L"repaired.log";
    ymcc::adaptlog::Journal failedWriter;
    bool failedWrite=!failedWriter.record(broken,json::object(),"test",{{"code",1}},json::object(),"time");
    failedWriter.record(broken,json::object(),"test",{{"code",1}},json::object(),"time");
    check("adaptation-failed-sink-backoff-bounds-retry",failedWrite&&failedWriter.status()["writeFailures"]==1);
    fspath::create_directories(broken.parent_path());ymcc::adaptlog::Journal repaired;
    check("adaptation-write-failure-does-not-consume-type",repaired.record(broken,json::object(),"test",{{"code",1}},json::object(),"time"));
    check("adaptation-no-pending-idle-path-is-explicit",!modelAdaptationSignals().pending()&&!modelAdaptationFacts().pending());
    {ymcc::adaptlog::Facts facts;for(unsigned i=0;i<2000;++i)facts.offer({json::object(),{{"a",i%2}},json::object(),"two-types"});
    check("adaptation-alternating-facts-do-not-grow",facts.status()["knownFacts"]==2);}
    std::ofstream cost(fspath::path(g_testRoot)/L"producer-cost.json");cost<<json({{"calls",100000},{"uniqueTypes",3},{"wallMicroseconds",producerUs},{"scope","isolated-real-callback-POD-path;not-product-CPU-or-device-latency"}}).dump(2);cost.close();
    }
    {
    reset(root,L"rog-adaptation");kLogMaxBytes=1024*1024;
    g_machineIdentity.manufacturer="ASUSTeK";g_machineIdentity.deviceClass="HandheldCompanion.Devices.ROGAlly";
    const BYTE unknown[]={0x5A,0x77};const auto previousTypes=modelAdaptationSignals().status()["knownTypes"];
    rogOemKeyParse(unknown,sizeof(unknown),70);modelAdaptationFlush();
    check("adaptation-ROG-stale-source-not-promoted",modelAdaptationSignals().status()["knownTypes"]==previousTypes&&!fspath::exists(fspath::path(g_testRoot)/L"model-adaptation.log"));
    for(unsigned i=0;i<5000;++i)rogOemKeyParse(unknown,sizeof(unknown),71);
    modelAdaptationFlush();auto rows=readRows(L"model-adaptation.log");
    check("adaptation-ROG-unknown-one-raw-type-no-business-publication",rows.size()==1&&rows[0]["firstSample"]["mappingFound"]==false&&rogPublications==0&&rows[0]["firstSample"]["firstRawReportHex"]=="5a77");
    check("adaptation-ROG-unknown-no-resident-input-flood",readRows(L"native-lifecycle.log").empty());
    const BYTE known[]={0x5A,kRogKeyEventAc};rogOemKeyParse(known,sizeof(known),71);modelAdaptationFlush();
    rows=readRows(L"model-adaptation.log");
    check("adaptation-ROG-mapped-parser-retains-publication",rows.size()==2&&rogPublications==1&&g_oemKeyMaskRog.load()==kOemBitAc&&rows[1]["firstSample"]["mappingFound"]==true);
    }
    {
    reset(root,L"model-catalog-baseline");
    g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayerApex";g_machineIdentity.manufacturer="ONE-NETBOOK";
    modelAdaptationCatalog();modelAdaptationCatalog();modelAdaptationFlush();auto rows=readRows(L"model-adaptation.log");
    check("adaptation-baseline-without-diagnostics-query",rows.size()==1&&rows[0]["category"]=="model-catalog-baseline"&&rows[0]["uniqueKey"]["identity"]["deviceId"]=="hc.device.OneXPlayerApex"&&rows[0]["uniqueKey"]["identity"]["keys"].size()==std::size(kOneXApexKeys)&&rows[0]["firstSample"]["consumerActionProven"]==false);
    g_machineIdentity.deviceClass="unknown-machine";g_machineIdentity.resolved=false;modelAdaptationCatalog();modelAdaptationFlush();rows=readRows(L"model-adaptation.log");
    check("adaptation-unknown-model-is-evidence-not-silenced",rows.size()==2&&rows[1]["uniqueKey"]["identity"]["catalogBound"]==false&&rows[1]["uniqueKey"]["model"]["resolved"]==false);
    }
    flushNativeLifecycleLogs(true);
    unsigned failed = 0; for(const auto& row:checks) if(!row["pass"].get<bool>()) ++failed;
    std::cout << json({{"checks", checks}, {"failures", failed}, {"scope", "EXTRACTED_REAL_WRITER_AND_GATE_HANDLER_ISOLATED_FILES_NO_PRODUCT_OR_HARDWARE"}}).dump(2) << std::endl;
    return failed ? 3 : 0;
}
"""
    maintenance=extract(source,"static void inputHostRecoveryMaintenanceTick() {")
    block_call=maintenance.index('appendNativeLifecycleLog("input-host-recovery-episode-new"')
    block_start=maintenance.rfind("            try {",0,block_call)
    block_end=maintenance.index("            } catch",block_call)
    block_end=maintenance.index(chr(10),block_end)
    episode_log_block=maintenance[block_start:block_end]
    assert block_start>=0 and "g_virtualStartProtection" not in episode_log_block and "GetTickCount64" not in episode_log_block
    unsafe_episode_block=episode_log_block.replace("json(nullptr),g_inputHostEpoch","g_virtualStartProtection.view(now).requestId,g_inputHostEpoch",1)
    assert unsafe_episode_block!=episode_log_block
    episode_log_probe=r"""
static void episodeLogProbe(ymcc::VirtualStartProtection& protection,unsigned long long observationTick,bool unsafe,json& recorded){
    auto& g_virtualStartProtection=protection;
    const auto now=observationTick;const unsigned long long g_inputHostEpoch=91;
    const std::string g_inputHostRunId="probe-run",g_inputHostHostInstanceId="probe-host";
    const std::atomic<unsigned long long> g_inputHostVirtualIntentRevision{4},g_inputHostPresentationWorkGeneration{8};
    auto appendNativeLifecycleLog=[&](const char*,const json& row){recorded=row;};
    if(unsafe){
"""+unsafe_episode_block+r"""
    }else{
"""+episode_log_block+r"""
    }
}
"""
    code = prelude + steam_log_projection + "\n\n" + "\n\n".join(functions) + "\nstatic json fanToggle(const json& a) " + fan_body + "\nstatic void familyImuLog(double gx, double gy, double gz, double ax, double ay, double az) {\n" + "struct Context { unsigned long long power=1, rawGeneration=1; }; struct Sample { Context context; unsigned long long capturedAt=0; } value; const auto* controllerMotion=&value; const auto now=GetTickCount64();\n" + family_log + "\n}\n" + inspection_log + "\n" + decky_log + "\n" + screen_log + "\n" + rog_fixture + "\n" + tests
    code = code.replace("static json fanToggle",episode_log_probe+"static json fanToggle",1)

    if args.case == "aya-keyboard-source":
        chord = "\n" + extract(source, "enum OemVkBit : uint32_t {") + ";\n"
        for signature in ("struct OemChordRule {", "struct OemChordProfile {"):
            chord += extract(source, signature) + ";\n"
        for match in re.finditer(r"^static const OemChordRule (\w+)\[\] = \{", source, re.M):
            chord += extract(source, match.group(0)) + ";\n"
        chord += extract(source, "enum class OemChordProfileId : int {") + ";\n"
        chord += extract(source, "static const OemChordProfile kOemProfiles[] = {") + ";\n"
        chord += extract(source, "static const OemChordProfile& oemChordProfileFor(int id) {") + "\n"
        chord += extract(source, "static uint32_t oemBackHookVkBit(DWORD vk) {") + "\n"
        chord += "static std::atomic<uint64_t> g_ayaOemKeyboardEvidenceBinding{0};\n"
        chord += extract(source, "static uint64_t ayaOemKeyboardEvidenceBindingFor(bool aya,int profileId) noexcept {") + "\n"
        chord += extract(source, "static void ayaOemKeyboardSourceObserve(DWORD vk,bool injected,bool rawInput,bool down) noexcept {") + "\n"
        selected = r"""
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;const fspath::path root(argv[1]);reset(root,L"aya-keyboard-source");kLogMaxBytes=1024*1024;
 g_machineIdentity.deviceClass="HandheldCompanion.Devices.AYANEOAIR1S";g_machineIdentity.resolved=true;
 const auto before=modelAdaptationSignals().status()["knownTypes"].get<unsigned>();
 ayaOemKeyboardSourceObserve(VK_F17,false,false,true);
 check("aya-unconfigured-evidence-is-off",modelAdaptationSignals().status()["knownTypes"]==before);
 const int cec=static_cast<int>(OemChordProfileId::AyaneoCec),next=static_cast<int>(OemChordProfileId::AyaneoNext2);
 check("aya-family-false-binding-is-zero",ayaOemKeyboardEvidenceBindingFor(false,cec)==0);
 check("aya-empty-or-invalid-profile-is-zero",ayaOemKeyboardEvidenceBindingFor(true,-1)==0&&ayaOemKeyboardEvidenceBindingFor(true,999)==0);
 const auto cecBinding=ayaOemKeyboardEvidenceBindingFor(true,cec);
 check("aya-descriptor-preserves-profile-and-real-source-mask",(cecBinding>>32)==uint64_t(cec+1)&&(uint32_t(cecBinding)&kVkF17));
 g_ayaOemKeyboardEvidenceBinding=cecBinding;
 for(DWORD vk:{DWORD('D'),DWORD('K'),DWORD(VK_LWIN),DWORD(VK_RCONTROL),DWORD(VK_F18)})ayaOemKeyboardSourceObserve(vk,false,false,true);
 check("aya-no-printable-modifier-or-outside-profile-witness",modelAdaptationSignals().status()["knownTypes"]==before);
 for(unsigned cycle=0;cycle<50000;++cycle)for(DWORD vk:{DWORD(VK_F15),DWORD(VK_F16),DWORD(VK_F17)})for(unsigned path=0;path<3;++path){
  ayaOemKeyboardSourceObserve(vk,path==1,path==2,true);ayaOemKeyboardSourceObserve(vk,path==1,path==2,false);
 }
 check("aya-50000-press-release-cycles-keep-nine-types",modelAdaptationSignals().status()["knownTypes"]==before+9);
 check("aya-callback-producers-never-write-files",!fspath::exists(fspath::path(g_testRoot)/L"model-adaptation.log"));
 modelAdaptationFlush();modelAdaptationFlush();auto rows=readRows(L"model-adaptation.log");
 check("aya-real-existing-journal-saves-nine-source-witnesses",rows.size()==9);
 unsigned ordinary=0,injected=0,raw=0;bool truthful=true,profile=true,noRaw=true;
 for(const auto& row:rows){const auto& sample=row["firstSample"];const auto& identity=row["uniqueKey"]["identity"];
  const auto path=sample.value("keyboardSourcePath","");ordinary+=path=="low-level-non-injected";injected+=path=="low-level-injected";raw+=path=="raw-input";
  truthful=truthful&&sample["consumerActionProven"]==false&&sample["deviceOriginVerified"]==false&&sample["completeChordProven"]==false&&sample["inputAdmissionChanged"]==false&&sample["injectedWriterIdentified"]==false&&sample["emptyIsAbsenceProof"]==false&&sample["mappingFound"]==false;
  profile=profile&&identity["sourceId"]==4&&identity["profileId"]==cec;noRaw=noRaw&&!sample.contains("firstRawReportHex");
  if(path=="raw-input")truthful=truthful&&sample["injectedFlagSeen"].is_null();
  else truthful=truthful&&sample["injectedFlagSeen"]==(path=="low-level-injected");
 }
 check("aya-raw-and-injected-paths-stay-distinct",ordinary==3&&injected==3&&raw==3);
 check("aya-key-atoms-never-claim-mapping-origin-or-action",truthful);
 check("aya-journal-identity-keeps-real-profile",profile);
 check("aya-no-raw-keyboard-reports-or-printable-content",noRaw);
 const auto acceptedBefore=modelAdaptationSignals().status()["knownTypes"].get<unsigned>();
 g_ayaOemKeyboardEvidenceBinding=ayaOemKeyboardEvidenceBindingFor(false,cec);
 ayaOemKeyboardSourceObserve(VK_F17,true,false,true);
 check("aya-other-family-gate-does-not-add-evidence",modelAdaptationSignals().status()["knownTypes"]==acceptedBefore);
 g_ayaOemKeyboardEvidenceBinding=ayaOemKeyboardEvidenceBindingFor(true,next);
 for(DWORD vk:{DWORD(VK_F19),DWORD(VK_F17),DWORD(VK_F20),DWORD(VK_F18)})for(unsigned path=0;path<3;++path)ayaOemKeyboardSourceObserve(vk,path==1,path==2,true);
 modelAdaptationFlush();modelAdaptationFlush();rows=readRows(L"model-adaptation.log");
 check("aya-next2-real-four-rear-function-keys-retain-twelve-paths",rows.size()==21);
 ++g_powerGeneration;
 for(DWORD vk:{DWORD(VK_F19),DWORD(VK_F17),DWORD(VK_F20),DWORD(VK_F18)})for(unsigned path=0;path<3;++path)ayaOemKeyboardSourceObserve(vk,path==1,path==2,false);
 modelAdaptationFlush();check("aya-power-time-counts-and-release-never-create-type-keys",readRows(L"model-adaptation.log").size()==21);
 g_ayaOemKeyboardEvidenceBinding=0;ayaOemKeyboardSourceObserve(VK_F17,false,false,true);modelAdaptationFlush();
 check("aya-clear-descriptor-stops-observation",readRows(L"model-adaptation.log").size()==21);
 flushNativeLifecycleLogs(true);unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();
 std::cout<<json{{"selectedCase","aya-keyboard-source"},{"checks",checks},{"failures",failed},{"scope","EXTRACTED_REAL_AYA_PROFILE_FILTER_PRODUCER_AND_EXISTING_JOURNAL;NO_INPUT_DEVICE_OR_OEM_ACTION"}}.dump(2)<<std::endl;return failed?3:0;
}
"""
        if args.aya_keyboard_case == "legacy-air":
            selected = r"""
int wmain(int argc,wchar_t**argv){
 if(argc!=2)return 2;const fspath::path root(argv[1]);reset(root,L"aya-legacy-air-source");kLogMaxBytes=1024*1024;
 g_machineIdentity.deviceClass="HandheldCompanion.Devices.AYANEOAIRPro";g_machineIdentity.resolved=true;
 const auto count=[](){return modelAdaptationSignals().status()["knownTypes"].get<unsigned>();};
 const auto before=count();const int air=int(OemChordProfileId::AyaneoAir);
 check("aya-air-profile-scope-uses-existing-mask",(uint32_t(ayaOemKeyboardEvidenceBindingFor(true,air))&(kVkF10|kVkF11|kVkF12))==(kVkF10|kVkF11|kVkF12));
 for(DWORD vk:{DWORD(VK_F10),DWORD(VK_F11),DWORD(VK_F12)})ayaOemKeyboardSourceObserve(vk,false,false,true);
 check("aya-air-no-binding-no-observation",count()==before);
 g_ayaOemKeyboardEvidenceBinding=ayaOemKeyboardEvidenceBindingFor(true,air);
 for(DWORD vk:{DWORD('D'),DWORD('K'),DWORD(VK_LWIN),DWORD(VK_RCONTROL),DWORD(VK_F3),DWORD(VK_F13),DWORD(VK_F24)})ayaOemKeyboardSourceObserve(vk,false,false,true);
 check("aya-air-printable-modifier-outside-profile-stay-unobserved",count()==before);
 for(unsigned repeat=0;repeat<2;++repeat)for(DWORD vk:{DWORD(VK_F10),DWORD(VK_F11),DWORD(VK_F12)})for(unsigned path=0;path<3;++path){
  ayaOemKeyboardSourceObserve(vk,path==1,path==2,true);ayaOemKeyboardSourceObserve(vk,path==1,path==2,false);
 }
 check("aya-air-three-function-keys-retain-nine-source-types",count()==before+9);
 check("aya-air-hook-producer-does-not-write-file",!fspath::exists(fspath::path(g_testRoot)/L"model-adaptation.log"));
 modelAdaptationFlush();modelAdaptationFlush();const auto rows=readRows(L"model-adaptation.log");
 check("aya-air-existing-journal-saves-nine-witnesses",rows.size()==9);
 unsigned ordinary=0,injected=0,raw=0;bool truthful=true;
 for(const auto& row:rows){const auto& sample=row["firstSample"];const auto& identity=row["uniqueKey"]["identity"];
  const auto path=sample.value("keyboardSourcePath","");ordinary+=path=="low-level-non-injected";injected+=path=="low-level-injected";raw+=path=="raw-input";
  const auto vk=identity["code"].get<unsigned>();
  truthful=truthful&&identity["sourceId"]==4&&identity["profileId"]==air&&vk>=VK_F10&&vk<=VK_F12&&
    sample["sourceScope"]=="AYA-current-profile-F10-F24;not-physical-OEM-device-proof"&&sample["consumerActionProven"]==false&&
    sample["deviceOriginVerified"]==false&&sample["completeChordProven"]==false&&sample["inputAdmissionChanged"]==false&&
    sample["injectedWriterIdentified"]==false&&sample["emptyIsAbsenceProof"]==false&&sample["mappingFound"]==false&& !sample.contains("firstRawReportHex");
  truthful=truthful&&(path=="raw-input"?sample["injectedFlagSeen"].is_null():sample["injectedFlagSeen"]==(path=="low-level-injected"));
 }
 check("aya-air-non-injected-injected-raw-remain-distinct",ordinary==3&&injected==3&&raw==3);
 check("aya-air-witness-is-not-chord-action-or-device-origin-proof",truthful);
 ++g_powerGeneration;
 for(DWORD vk:{DWORD(VK_F10),DWORD(VK_F11),DWORD(VK_F12)})for(unsigned path=0;path<3;++path)ayaOemKeyboardSourceObserve(vk,path==1,path==2,false);
 modelAdaptationFlush();check("aya-air-release-power-and-repeat-do-not-add-types",readRows(L"model-adaptation.log").size()==9);
 g_ayaOemKeyboardEvidenceBinding=ayaOemKeyboardEvidenceBindingFor(false,air);
 ayaOemKeyboardSourceObserve(VK_F10,false,false,true);modelAdaptationFlush();
 check("aya-air-other-family-stops-observation",count()==before+9&&readRows(L"model-adaptation.log").size()==9);
 flushNativeLifecycleLogs(true);unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();
 std::cout<<json{{"selectedCase","aya-keyboard-source"},{"directedCase","legacy-air"},{"checks",checks},{"failures",failed},
  {"scope","EXTRACTED_EXISTING_AYA_PROFILE_FILTER_AND_JOURNAL;NO_INPUT_ADMISSION_CHANGE_OR_DEVICE"}}.dump(2)<<std::endl;return failed?3:0;
}
"""
        start = code.index("int wmain(")
        code = code[:start] + chord + selected

    cpp = run / "log-frequency-harness.cpp"
    if review_run is not None:
        if (review_run/"log-frequency-harness.cpp").read_text(encoding="utf-8-sig") != code:
            raise ValueError("Referenced generated producer differs;refusing implicit rebuild/re-execution")
        if (review_run/"bound-log-frequency-policy.h").read_bytes()!=header_bytes:
            raise ValueError("Referenced producer policy differs")
        if sdk_response_bytes is not None and (review_run/"sdk-first-response.json").read_bytes()!=sdk_response_bytes:
            raise ValueError("Referenced SDK bridge response differs")
    cpp.write_text(code, encoding="utf-8")
    exe = run / "log-frequency-harness.exe"
    vcvars = pathlib.Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat")
    if not vcvars.is_file(): raise RuntimeError("Existing MSVC toolchain unavailable; no installation attempted")
    directed_dependencies = ' /sourceDependencies "' + str(run / 'dependencies.json') + '"' if args.aya_keyboard_case else ''
    command = 'call "' + str(vcvars) + '" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2' + directed_dependencies + ' "' + str(cpp) + '" /I"' + str(ROOT / "deps/json") + '" /I"' + str(ROOT / "native") + '" /Fo"' + str(run / "harness.obj") + '" /Fe"' + str(exe) + '"'
    # A saved batch avoids Python's list-to-command-line quoting of /c payloads.
    # It only initializes the existing compiler environment and compiles this fixture.
    # Registry discovery can omit an SDK whose headers/libraries are installed.
    # This only supplies existing paths to the fixture compiler, never host setup.
    sdk_root = pathlib.Path(r"C:\Program Files (x86)\Windows Kits\10")
    installed = sorted((sdk_root / "Include").glob("10.*"), reverse=True)
    sdk = next((p for p in installed if (p/"um/Windows.h").is_file() and (p/"ucrt/stdio.h").is_file()), None)
    if sdk is not None:
        command += "".join(' /I"' + str(sdk / name) + '"' for name in ("um", "shared", "ucrt", "winrt"))
        command += ' /link' + "".join(' /LIBPATH:"' + str(sdk_root/"Lib"/sdk.name/name/"x64") + '"' for name in ("um", "ucrt"))
    build_cmd = run / "build-harness.cmd"
    build_cmd.write_text("@echo off\n" + command + "\n", encoding="utf-8")
    if review_run is None:
        build = subprocess.run(["cmd.exe", "/d", "/c", str(build_cmd)], cwd=run, capture_output=True, text=True, errors="replace")
        (run / "compile.log").write_text(build.stdout + build.stderr, encoding="utf-8")
        if build.returncode: print(build.stdout + build.stderr); return build.returncode
        run_args = [str(exe), str(run / "fixtures")]
        if gpu_result_path: run_args.append(str(gpu_result_path))
        result = subprocess.run(run_args, cwd=run, capture_output=True, text=True, errors="replace")
        (run / "result.json").write_text(result.stdout, encoding="utf-8")
        if result.stderr: (run / "stderr.log").write_text(result.stderr, encoding="utf-8")
        report = json.loads(result.stdout)
        producer_exit_code=result.returncode
        report["executedThisRun"]=True
    else:
        raw_result=(review_run/"result.json").read_bytes()
        report=json.loads(raw_result)
        if not isinstance(report.get("checks"),list):raise ValueError("Referenced raw checks missing")
        failures=sum(not item.get("pass",False) for item in report["checks"])
        if report.get("failures")!=failures:raise ValueError("Referenced failure summary disagrees with actual checks")
        producer_exit_code=3 if failures else 0
        references=[]
        for name in ("result.json","log-frequency-harness.cpp","log-frequency-harness.exe","bound-main.cpp","bound-log-frequency-policy.h","compile.log"):
            file=review_run/name;data=file.read_bytes()
            references.append({"path":str(file),"bytes":len(data),"sha256":hashlib.sha256(data).hexdigest()})
        (run/"REFERENCED-PRODUCER.json").write_text(json.dumps({"executedThisRun":False,"generatedHarnessIdentical":True,"references":references},indent=2)+"\n",encoding="utf-8")
        report["executedThisRun"]=False
        report["producerScope"]="CACHED_REFERENCE;original actual producer;only postchecks executed in this review"
        report["referencedProducer"]=references
    wake = extract(source, "static void inputHostEmitWakeTruthSnapshot() {")
    mirror = functions[3]
    current_bytes = source_path.read_bytes()
    current_source = current_bytes.decode("utf-8-sig").replace("\r\n", "\n")
    bound_functions = functions
    if aya_prepared_writer:
        bound_functions = functions[:7] + [extract(source,"static void appendNativeLifecyclePrepared(const char* event,const json& detail,bool baseEvent,const std::string* encoded) {"),
            extract(source,"static void appendNativeLifecycleLog(const char* event, const json& observed) {")] + functions[8:]
    stable_functions = all(extract(current_source, f[:f.index("{")].strip() + " {") == f for f in bound_functions)
    stable_fan = extract(current_source, 'ipc_on("fanLog.setEnabled", [](const json& a) -> json {') == fan
    current_family = extract(current_source, "        if (controllerMotion) {")
    stable_family = extract(current_family, "if (g_sensorDetailLogging.load(std::memory_order_acquire)) {") == family_log
    static_checks = {
        "identity-observation-helper-called-after-selected-update": identity_observer.index("g_hidHidePhysicalIdentity = resolved;") < identity_observer.index("hidHideLogPhysicalIdentityObservation(resolved, resolvedGeneration, changed);"),
        "identity-observer-stable-during-selftest": extract(current_source, "static void hidHideObserveRawInputDevice(HANDLE rawDevice) {") == identity_observer,
        "identity-detail-off-gates-before-json": extract(source,"static void hidHideLogPhysicalIdentityObservation(").index('if (!changed && (!selectedChanged || !domainLogEnabled("virtual"))) return;') < extract(source,"static void hidHideLogPhysicalIdentityObservation(").index('appendNativeLifecycleLog('),
        "wake-truth-gated-before-json-construction": wake.index('if (!domainLogEnabled("virtual")) return;') < wake.index('appendDomainLog('),
        "mirror-gated-before-string-allocation": mirror.index('if (!domainLogEnabled("gyro") && !domainLogEnabled("virtual")) return;') < mirror.index('std::string name'),
        "detail-export-includes-active-and-rotated": 'addRotated(appDir + L"\\\\native-detail.log");' in source,
        "export-flushes-pending-detail-batch": 'flushNativeLifecycleLogs(); // export includes the latest partial detail batch' in source,
        "bus-transition-helper-used-in-production": 'if (busRateLogState.observe(evidenceRateFlags)) {' in source,
        "periodic-bus-samples-have-separate-name": 'appendNativeLifecycleLog("input-capture-bus-rate-sample", rateEvidence(true));' in source,
        "family-imu-source-gates-before-building-json": family_log.index("g_sensorDetailLogging.load") < family_log.index("inputCaptureQueueSample"),
        "family-imu-source-has-no-synchronous-write": "inputCaptureAppend(" not in family,
        "detail-writer-rechecks-gate-under-lock": 'if (!baseEvent && !g_domainLogEnabled[1].load(std::memory_order_acquire)) return;' in functions[7],
        "bound-logging-functions-stable-during-selftest": stable_functions and stable_fan and stable_family,
        "policy-header-stable-during-selftest": header.read_bytes() == header_bytes,
        "screen-off-fast-path-before-wrapper": screen_log.index('ymcc::logging::observationEqual(') < screen_log.index('appendNativeLifecycleLog('),
        "suppressed-writer-skips-deep-copy": functions[7].index("NativeLane::suppressed") < functions[7].index("json detail = *evidence"),
        "sleep-post-action-read-labels-preserved": all(x in (ROOT/"native/sleep_recovery_auto.h").read_text(encoding="utf-8-sig") for x in ('inspect(gen,"reconcile-postflight")','inspect(gen,"input-postflight")')),
        "sleep-pre-action-labels-preserved": all(x in inspection_source for x in ('"input-preflight"','"reconcile-preflight"','"native-return-preflight"','"suspend-baseline"')),
    }
    onex_parser=extract(source,"static void onexBackParse(const BYTE* data, size_t len, uint64_t actionGeneration) {")
    static_checks.update({
        "virtual-sample-bound-before-actual-request-and-observed-after": source.index("virtualEvidenceStamp.captureBinding") < source.index('accepted = frameTextReady') < source.index("virtualAdaptationFrameReceipt(virtualEvidenceStamp"),
        "virtual-no-extra-submit-in-observer": "inputHostRequest" not in (ROOT/"native/virtual_adaptation_log_native.inc").read_text(encoding="utf-8"),
        "adaptation-baseline-called-by-existing-machine-resolver": "modelAdaptationCatalog();" in extract(source,"static void ymccResolveMachineIdentityOnce() {"),
        "episode-log-block-no-protection-view-or-new-clock": "g_virtualStartProtection" not in episode_log_block and "GetTickCount64" not in episode_log_block and "inputHostStartProtectionNow" not in episode_log_block,
        "episode-log-builder-has-no-new-os-query": "GetProcessTimes" not in extract(source,"static json inputHostRecoveryEpisodeLogRecord(") and "writerIdentity()" in extract(source,"static json inputHostRecoveryEpisodeLogRecord("),
        "episode-log-builder-only-called-for-accepted-reset": source.count("inputHostRecoveryEpisodeLogRecord(")==2 and extract(source,"static void inputHostRecoveryMaintenanceTick() {").index("g_inputHostRecoveryIntentReset.exchange") < extract(source,"static void inputHostRecoveryMaintenanceTick() {").index("inputHostRecoveryEpisodeLogRecord("),
        "adaptation-OneX-parser-bound-to-source": extract(current_source,"static void onexBackParse(const BYTE* data, size_t len, uint64_t actionGeneration) {")==onex_real_parser,
        "adaptation-ROG-parser-bound-to-source": extract(current_source,"static void rogOemKeyParse(const BYTE* data, size_t len, uint64_t actionGeneration) {")==rog_parser,
        "adaptation-tap-after-current-source-guard": onex_parser.index("!oemProducerCurrent") < onex_parser.index("modelAdaptationOnexReport") < onex_parser.index("inputFrame"),
        "adaptation-export-file-and-status": 'addFile(dataDir + L"\\\\model-adaptation.log");' in source and 'availability["modelAdaptation"]=modelAdaptationStatus();' in source,
        "adaptation-observer-no-disk": "Journal().record" not in extract((ROOT/"native/model_adaptation_log_native.inc").read_text(encoding="utf-8"),"static void modelAdaptationObserveRecord(const char* event,const json& detail) noexcept {"),
    })
    static_checks.update({
        "R117-export-index-locates-physical-slot-results": 'physicalSlotGuardEvidenceFile=native-lifecycle.log(changes),native-detail.log(repeats)' in source,
        "R114-export-includes-rotated-native-lifecycle": 'addRotated(appDir + L"\\\\native-lifecycle.log");' in source,
        "R114-export-index-locates-helper-facts": 'inputHelperAuthorityEvidenceFile=native-lifecycle.log event=input-host.native-helper-diagnostic detailLoggingRequired=0' in source,
        "R114-export-index-locates-owned-game-plan": 'ownedGameReconnectEvidenceFile=native-lifecycle.log event=hidhide.owned-game-reconnect-plan detailLoggingRequired=0' in source,
    })
    if gpu_result_path:
        static_checks["gpu-Windows-result-stable-during-selftest"] = gpu_result_path.read_bytes() == gpu_result_bytes
        report["gpuWindowsResultPath"] = str(gpu_result_path)
        report["gpuWindowsResultSha256"] = hashlib.sha256(gpu_result_bytes).hexdigest().upper()
        report["gpuProductionResponseCount"] = len(gpu_result["responses"])
    static_checks["snapshot-and-boundary-headers-stable"] = all((ROOT/"native"/n).read_bytes()==b for n,b in extra_headers.items())
    report["extraHeaderSha256"] = {n:hashlib.sha256(b).hexdigest().upper() for n,b in extra_headers.items()}
    if args.case == "aya-keyboard-source":
        hook=extract(source,"static LRESULT CALLBACK oemBackHookProc(int nCode, WPARAM wParam, LPARAM lParam) {")
        raw_observer=extract(source,"static void keyboardModifierObserveRaw(const RAWINPUT& raw) noexcept {")
        gate=extract(source,"static void oemBackMappingRefreshGate(bool targetEnabled, const std::string& configuredPersona,\n                                      bool backMappingRequested) {")
        static_checks.update({
            "aya-low-level-witness-before-injected-rejection":hook.index("ayaOemKeyboardSourceObserve")<hook.index("if (kb != nullptr && kb->vkCode < 256"),
            "aya-raw-witness-before-detailed-log-gate":raw_observer.index("ayaOemKeyboardSourceObserve")<raw_observer.index("g_keyboardModifierEventGate"),
            "aya-evidence-only-exact-family-using-existing-bound-profile":"g_machineIdentity.family==YmccFamily::Ayanero,profileId" in gate and "oemBoundChordProfileIdFor" in gate,
        })
        report["selectedCase"]=args.case
    report["sourceChecks"] = static_checks
    report["wholeSourceStableDuringSelftest"] = current_bytes == source_bytes
    report["currentWholeSourceSha256"] = hashlib.sha256(current_bytes).hexdigest().upper()
    report["sourceDriftPolicy"] = "Unrelated shared-file edits are disclosed; any bound logging-function or policy-header drift fails the test."
    report["sourceSha256"] = hashlib.sha256(source_bytes).hexdigest().upper()
    report["policyHeaderSha256"] = hashlib.sha256(header.read_bytes()).hexdigest().upper()
    report["runDirectory"] = str(run)
    report["fixtureRotationThresholdBytes"] = 256
    report["productionOrHardwareExecuted"] = False
    report["sdkFirstBridgeExecuted"]=sdk_response_path is not None
    if sdk_response_path:report["sdkFirstResponse"]={"path":str(sdk_response_path),"sha256":hashlib.sha256(sdk_response_bytes).hexdigest(),"stable":sdk_response_path.read_bytes()==sdk_response_bytes}
    report["frequencyPolicyHeaderChanged"] = header.read_bytes() != header_bytes
    report["boundedR114EvidenceBaseEvents"] = ["input-host.native-helper-diagnostic", "hidhide.owned-game-reconnect-plan"]
    report["failures"] += sum(not v for v in static_checks.values())
    (output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 3 if report["failures"] else producer_exit_code

if __name__ == "__main__":
    raise SystemExit(main())
