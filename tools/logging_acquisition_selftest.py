# Compile actual logging functions/archive helper. No product Main, hardware or live logs.
import argparse,datetime,hashlib,json,pathlib,subprocess,zipfile
from log_frequency_selftest import ROOT,extract
from steam_log_export_selftest import PRELUDE
EXTRA=r'''
#include <mutex>
#include <cstring>
#include "log_frequency_policy.h"
#include "tdp_static_observation.h"
static std::wstring fixtureRoot;
static std::wstring webview_data_dir(){return (fspath::path(fixtureRoot)/L"front").wstring();}
static std::atomic<unsigned long long> g_frontendDiagnosticsFailures{0},g_frontendTraceDetailSuppressed{0},g_powerGeneration{1},g_webviewGeneration{1};
static std::atomic<bool> g_domainLogEnabled[2]={false,false};
static std::mutex g_webviewFailureLogMx;
static int g_webviewGpuMode=0;static std::string g_webviewRouteStatus="fixture-ready";
static std::string webViewLocalTimestamp(bool){return "2026-10-11T19:48:55.000";}
static const char* webViewGpuModeName(int){return "fixture-no-WebView";}
'''
TEST=r'''
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;fixtureRoot=argv[1];const fspath::path root(fixtureRoot);fspath::create_directories(root);
 using namespace ymcc::logexport;
 check("missing-file-not-a-filesystem-fault",missingFileReason(std::make_error_code(std::errc::no_such_file_or_directory))=="not-created-or-not-retained");
 check("access-denied-is-still-a-real-status-error",missingFileReason(std::make_error_code(std::errc::permission_denied))=="file-status-error");
 put(root/L"partial"/L"input-capture.jsonl","{\"kind\":\"sample\",\"time\":\"2026-10-11T19:00:00+08:00\"}\n{\"kind\":\"sample\",\"imu\":");
 const auto half=stagedFileEvidence(root/L"partial"/L"input-capture.jsonl","input-capture.jsonl");
 check("partial-tail-retained-and-explicit-not-source-corruption",half["status"]=="snapshot-partial-tail-retained"&&half["validJsonRows"]==1&&half["invalidJsonRows"]==1&&half["originalBytesRetained"]==true);
 put(root/L"valid"/L"tdp.log","{\"time\":\"2026-10-11T19:30:00+08:00\"}");
 const auto finalJson=stagedFileEvidence(root/L"valid"/L"tdp.log","tdp.log");check("valid-json-without-newline-is-not-truncation",finalJson["partialOrInvalidTrailingRecord"]==false&&finalJson["recordBoundary"]=="valid-json-without-final-newline");
 put(root/L"app.log","plain text no final newline");const auto opaque=stagedFileEvidence(root/L"app.log","app.log");
 check("opaque-app-Steam-HC-text-never-judged-bad-JSONL",opaque["format"]=="opaque-file"&&!opaque.contains("invalidJsonRows")&&!jsonLinesEvidenceName("steam/logs/controller.txt")&&!jsonLinesEvidenceName("yeman-fan-host20261011.log"));
 check("rotated-JSONL-files-retain-format-classification",jsonLinesEvidenceName("frontend-trace.log.previous")&&jsonLinesEvidenceName("tdp.log.1"));
 put(root/L"empty.log","");const auto empty=stagedFileEvidence(root/L"empty.log","frontend-trace.log");check("empty-snapshot-zero-rows-no-warning",empty["bytes"]==0&&empty["validJsonRows"]==0&&!empty["partialOrInvalidTrailingRecord"].get<bool>());
 put(root/L"tdp.log",std::string(8*1024*1024+500,'x')+"\n{\"time\":\"2026-10-11T19:40:00+08:00\"}\n");const auto tail=stagedFileEvidence(root/L"tdp.log","tdp.log");
 check("bounded-8MiB-tail-does-not-claim-older-history",tail["earlierBytesNotInspected"].get<unsigned long long>()>0&&tail["validJsonRows"]==1&&tail["firstTime"]=="2026-10-11T19:40:00+08:00");
 auto readRows=[](const wchar_t* name){std::vector<json> rows;std::ifstream in(fspath::path(webview_data_dir())/name);for(std::string line;std::getline(in,line);)if(!line.empty())rows.push_back(json::parse(line));return rows;};
 json input={{"where","gamepad-admission"},{"message","{\"stage\":\"owner-start\",\"action\":\"\",\"epoch\":1,\"qSeq\":1,\"t\":1}"},{"build","fixture"}};
 check("legacy-state-error-endpoint-routes-to-trace-not-errors",appendFrontendDiagnostic(input)&&readRows(L"frontend-errors.log").empty()&&readRows(L"frontend-trace.log").size()==1);
 input["message"]="{\"stage\":\"owner-start\",\"action\":\"\",\"epoch\":1,\"qSeq\":2,\"t\":2}";
 check("same-trace-state-sequence-only-detailed-off-counted",appendFrontendTrace(input)&&g_frontendTraceDetailSuppressed==1&&readRows(L"frontend-trace.log").size()==1);
 g_domainLogEnabled[1]=true;check("same-trace-detailed-on-keeps-original-sequence-values",appendFrontendTrace(input)&&readRows(L"frontend-trace-detail.log").size()==1&&readRows(L"frontend-trace-detail.log")[0]["trace"]["qSeq"]==2);
 input["message"]="{\"stage\":\"owner-start\",\"epoch\":2,\"qSeq\":3,\"t\":3}";g_domainLogEnabled[1]=false;
 check("new-owner-epoch-remains-always-on-state-edge",appendFrontendTrace(input)&&readRows(L"frontend-trace.log").size()==2);
 input["stack"]="fixture-real-error-stack";check("real-error-with-stack-never-demoted-to-state-trace",appendFrontendDiagnostic(input)&&readRows(L"frontend-errors.log").size()==1&&readRows(L"frontend-errors.log")[0]["severity"]=="error");
 const std::string longUnicode=std::string(999,'x')+"中文";input={{"where","fixture-real-error"},{"message",longUnicode},{"stack","Authorization: Bearer private-token C:\\Users\\PrivateUser\\file"}};
 check("UTF8-cutoff-and-privacy-do-not-lose-error-row",appendFrontendDiagnostic(input)&&readRows(L"frontend-errors.log").size()==2&&readRows(L"frontend-errors.log")[1].dump().find("private-token")==std::string::npos&&readRows(L"frontend-errors.log")[1].dump().find("PrivateUser")==std::string::npos);
 check("privacy-redaction-is-not-mislabelled-as-byte-limit-truncation",readRows(L"frontend-errors.log")[1]["stackTruncated"]==false&&readRows(L"frontend-errors.log")[1]["messageTruncated"]==true);
 input={{"where","gamepad-admission"},{"message","fixture-real-error-without-stack"}};check("unstructured-real-error-without-stack-not-misclassified",appendFrontendDiagnostic(input)&&readRows(L"frontend-errors.log").size()==3);
 const auto before=g_frontendDiagnosticsFailures.load();const auto prior=fixtureRoot;fixtureRoot=(root/L"blocked").wstring();put(fspath::path(fixtureRoot)/L"front","block-directory");
 check("sink-failure-return-is-honest-and-counted",!appendFrontendDiagnostic(input)&&g_frontendDiagnosticsFailures>before);fixtureRoot=prior;
 const auto sinkBefore=g_frontendDiagnosticsFailures.load();const auto validRoot=fixtureRoot;
 fixtureRoot=(root/L"blocked-trace").wstring();put(fspath::path(fixtureRoot)/L"front","block-directory");
 json newTrace={{"where","gamepad-admission"},{"message","{\"stage\":\"unique-retry-after-sink-error\",\"epoch\":50}"}};
 const bool failedTrace=!appendFrontendTrace(newTrace);fixtureRoot=validRoot;
 const auto savedBefore=readRows(L"frontend-trace.log").size();check("failed-first-state-write-does-not-turn-retry-into-suppressed-detail",failedTrace&&g_frontendDiagnosticsFailures>sinkBefore&&appendFrontendTrace(newTrace)&&readRows(L"frontend-trace.log").size()==savedBefore+1);
 check("trace-source-cannot-choose-arbitrary-native-path",!appendFrontendTrace({{"where","arbitrary"},{"message","{}"}}));
 json screen={{"callSite","screenTouchpads.get"},{"profile",1},{"state",{{"enabled",true},{"visible",false},{"events",1},{"repaints",1}}}};
 check("first-screen-state-is-base",!nativeLifecycleRecordIsStreaming("screen-touchpads-state",screen));
 screen["state"]["events"]=2;screen["state"]["repaints"]=3;check("screen-counter-only-repeat-is-detailed",nativeLifecycleRecordIsStreaming("screen-touchpads-state",screen));
 screen["state"]["visible"]=true;check("screen-visibility-change-stays-base",!nativeLifecycleRecordIsStreaming("screen-touchpads-state",screen));
 ++g_powerGeneration;check("same-screen-state-new-power-generation-is-base",!nativeLifecycleRecordIsStreaming("screen-touchpads-state",screen));
 // Replay user ZIP bytes into isolated fixture files, then use the actual tar helper.
 const auto sample=root/L"sample";std::vector<std::wstring> sourceFiles;
 for(const auto* name:{L"input-capture.jsonl",L"native-lifecycle.log",L"frontend-errors.log"})sourceFiles.push_back((sample/name).wstring());
 const auto packed=createTarArchiveWithRetry((root/L"field-snapshot-export.zip").wstring(),sourceFiles);
 check("actual-user-sample-staged-ZIP-declares-half-tail",packed.ok&&packed.snapshotBoundaryWarnings==1&&packed.snapshotInspectionFailures==0);
 json report={{"checks",checks},{"failures",0},{"realProductOrHardwareExecuted",false},{"originalSourceLogsModified",false},{"scope","ACTUAL_NATIVE_LOG_WRITERS_AND_ARCHIVER_ISOLATED_SAMPLE_BYTES_ONLY"}};
 for(const auto& c:checks)if(!c["pass"].get<bool>())report["failures"]=report["failures"].get<int>()+1;
 std::cout<<report.dump(2)<<std::endl;return report["failures"].get<int>()?1:0;
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--sample-zip',type=pathlib.Path,required=True);p.add_argument('--output-dir',type=pathlib.Path,required=True);a=p.parse_args();run=a.output_dir/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'));run.mkdir(parents=True);fixture=run/'fixtures';sample=fixture/'sample';sample.mkdir(parents=True)
 with zipfile.ZipFile(a.sample_zip)as z:
  for name in ['input-capture.jsonl','native-lifecycle.log','frontend-errors.log']:(sample/name).write_bytes(z.read(name))
 source=(ROOT/'native/main.cpp').read_text(encoding='utf-8-sig');signatures=['static void rotateOneLogFile(const std::wstring& path, unsigned long long maxBytes) {','static std::string boundedJsonString(const json& value, const char* key, size_t limit) {','static bool appendFrontendTrace(const json& input) noexcept {','static bool appendFrontendDiagnostic(const json& input) noexcept {','static bool nativeLifecycleRecordIsStreaming(const char* event, const json& detail) {']
 funcs='\n'.join(extract(source,s)for s in signatures);start=source.index('static constexpr unsigned int LOG_ARCHIVE_MAX_ATTEMPTS');helper=source[start:source.index('// 进程内实时检测 SMT',start)]
 cpp=run/'fixture.cpp';cpp.write_text(PRELUDE+'\n'+EXTRA+'\n'+funcs+'\n'+helper+'\n'+TEST,encoding='utf-8');exe=run/'fixture.exe';vc=r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat';batch=run/'build.cmd';batch.write_text(f'@echo off\ncall "{vc}" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 "{cpp}" /I"{ROOT/"deps/json"}" /I"{ROOT/"native"}" /Fo"{run/"fixture.obj"}" /Fe"{exe}" /link advapi32.lib user32.lib\n')
 build=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,text=True,errors='replace');(run/'compile.log').write_text(build.stdout+build.stderr,encoding='utf-8')
 if build.returncode:print(build.stdout+build.stderr);return build.returncode
 tested=subprocess.run([str(exe),str(fixture)],cwd=run,capture_output=True,encoding='utf-8',errors='replace');(run/'stderr.txt').write_text(tested.stderr,encoding='utf-8')
 if not tested.stdout:print(tested.stderr);return tested.returncode or 1
 report=json.loads(tested.stdout)
 def check(name,value):report['checks'].append({'name':name,'pass':bool(value)});report['failures']+=not value
 with zipfile.ZipFile(fixture/'field-snapshot-export.zip')as z:
  receipt=json.loads(z.read('log-export-snapshot.json'));entries={r['entry']:r for r in receipt['files']}
  check('ZIP-replay-preserves-all-raw-bytes-including-truncated-tail',all(z.read(name)==(sample/name).read_bytes()for name in ['input-capture.jsonl','native-lifecycle.log','frontend-errors.log']))
  check('receipt-uses-actual-ZIP-staged-size-and-last-complete-window',entries['input-capture.jsonl']['bytes']==len(z.read('input-capture.jsonl'))and entries['input-capture.jsonl']['partialOrInvalidTrailingRecord']and entries['native-lifecycle.log']['lastTime']!='not-observed')
 check('gamepad-engine-only-diagnostic-call-and-import-changed',control_engine_diff())
 baseline=(a.output_dir.parent/'pre-edit/native/main.cpp').read_text(encoding='utf-8-sig')
 # Verify the screen controller parameters/validation/actions have not been edited by logging.
 beforeGet=extract(baseline,'ipc_on("screenTouchpads.get", [](const json& args) -> json {')
 afterGet=extract(source,'ipc_on("screenTouchpads.get", [](const json& args) -> json {')
 getCore=extract(afterGet,'        auto execute = [&]() -> json {')
 getBody=getCore[getCore.index('{')+1:-1]
 check('screen-get-identical-return-value-no-extra-screen-action',getBody==beforeGet[beforeGet.index('{')+1:-1])
 beforeSet=extract(baseline,'ipc_on("screenTouchpads.set", [](const json& args) -> json {')
 afterSet=extract(source,'ipc_on("screenTouchpads.set", [](const json& args) -> json {')
 setCore=extract(afterSet,'        auto execute = [&]() -> json {')
 check('screen-set-control-validation-and-call-order-identical',setCore[setCore.index('{')+1:-1]==beforeSet[beforeSet.index('{')+1:-1])
 check('screen-early-refusals-and-original-exceptions-have-static-observation',all('try{auto result=execute();observe(result);return result;}' in h and 'original-handler-threw' in h for h in [afterGet,afterSet]))
 report.update(runDirectory=str(run),sampleZip=str(a.sample_zip),exportSample=str(fixture/'field-snapshot-export.zip'),sourceSha256=hashlib.sha256(source.encode()).hexdigest())
 (a.output_dir/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False,indent=2));return 1 if report['failures']else 0

def control_engine_diff():
 baseline=pathlib.Path(r'G:\YeManCC-Work\Build\Validation\LoggingAcquisition-20261011-194855\pre-edit\src\gamepad\engine.ts').read_text(encoding='utf-8-sig');current=(ROOT/'src/gamepad/engine.ts').read_text(encoding='utf-8-sig');expected=baseline.replace("import { reportFrontendError } from '@/robust/frontendDiagnostics';","import { reportFrontendTrace } from '@/robust/frontendDiagnostics';",1).replace('// reportFrontendError → native diagnostics.frontendError → frontend-errors.log','// reportFrontendTrace → native diagnostics.frontendTrace → frontend-trace(.detail).log',1).replace("reportFrontendError('gamepad-admission', JSON.stringify({ stage, action, ...gamepadGateSnapshot(), ...detail }));","reportFrontendTrace('gamepad-admission', { stage, action, ...gamepadGateSnapshot(), ...detail });",1);return expected==current
if __name__=='__main__':raise SystemExit(main())
