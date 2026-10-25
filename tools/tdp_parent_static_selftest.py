"""Compile the real parent shell handler/logger against isolated helper fixtures.
The only real helper invocation is NO ARGUMENTS (usage rc=2) or a broken
PyInstaller runtime which cannot enter Python. No TDP/driver command is issued.
"""
from __future__ import annotations
import argparse,datetime,hashlib,json,pathlib,shutil,subprocess,zipfile
from log_frequency_selftest import ROOT,extract
from steam_log_export_selftest import PRELUDE as ARCHIVE_PRELUDE
PRELUDE=r"""
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <string>
#include <memory>
#include <unordered_set>
#include "json.hpp"
#include "tdp_static_observation.h"
#include "steam_log_export.h"
#include "log_export_evidence.h"
#include "log_ai_index.h"
#include <functional>
#include "buffered_domain_log.h"
using json=nlohmann::json;namespace fspath=std::filesystem;
static std::wstring g_testRoot,kTdpDaemonExe,POWER_CONTROL_DIR;
static std::wstring yemancc_data_dir(){const auto dir=fspath::path(g_testRoot)/L"data";fspath::create_directories(dir);return dir.wstring();}
static std::atomic<unsigned long long> g_powerGeneration{1};
static std::atomic<int> g_powerLifecycle{0};
static const char* powerLifecycleName(int){return "fixture-ready";}
static std::string nativeLifecycleTimestamp(){return "2026-10-11T16:00:00.000";}
static std::atomic<bool> g_domainLogEnabled[2]={false,false},g_poolCancel{false};
static unsigned long long kLogMaxBytes=5ull*1024*1024;
static std::mutex g_tdpObservationMx;
static ymcc::logging::BufferedDomainLog g_tdpObservationStreams[2];
static std::atomic<unsigned long long> g_tdpObservationFailures{0};
static unsigned hardwareLeaseCalls=0;
struct MachineIdentity{std::string deviceClass,product,processor;}; static MachineIdentity g_machineIdentity;
struct HardwareWriteLease{explicit HardwareWriteLease(const char*){++hardwareLeaseCalls;throw std::runtime_error("fixture forbids ALL hardware writes");}};
static bool hardwareWriteAllowed(){return false;}
static bool sameFinalPath(const std::wstring& a,const std::wstring& b){return _wcsicmp(a.c_str(),b.c_str())==0;}
static std::string nextTdpRequestId(){return "fixture-request";}
"""
TESTS=r"""
__declspec(noinline) static void overwriteFixtureStack(){volatile unsigned long long bytes[512];for(size_t i=0;i<512;++i)bytes[i]=0xababababababababULL+i;}
static json checks=json::array();
static void check(const char* name,bool pass){checks.push_back({{"name",name},{"pass",pass}});}
static std::vector<json> readRows(){for(auto& stream:g_tdpObservationStreams)stream.flush();std::ifstream in(fspath::path(yemancc_data_dir())/L"tdp.log");std::vector<json> rows;for(std::string line;std::getline(in,line);)if(!line.empty())rows.push_back(json::parse(line));return rows;}
int wmain(int argc,wchar_t** argv){
 if(argc==2&&wcscmp(argv[1],L"--fixture-timeout")==0){Sleep(60000);return 0;}
 if(argc==2&&wcscmp(argv[1],L"--fixture-stale")==0){Sleep(300);return 0;}
 if(argc==2&&wcscmp(argv[1],L"--fixture-error")==0){const std::string text="fixture-first-error Authorization: Bearer private-token\nC:\\Users\\PrivateUser\\file\n"+std::string(5000,'x')+std::string(1,char(0xff));DWORD wrote=0;WriteFile(GetStdHandle(STD_ERROR_HANDLE),text.data(),static_cast<DWORD>(text.size()),&wrote,nullptr);return 7;}
 if(argc!=2)return 2;g_testRoot=argv[1];POWER_CONTROL_DIR=g_testRoot;
 const auto root=fspath::path(g_testRoot);
 kTdpDaemonExe=(root/L"complete"/L"YeManTdpCtl.exe").wstring();
 auto full=shellHandler({{"program",W2U(kTdpDaemonExe)},{"args",json::array()},{"timeoutMs",10000},{"tdpObservation",{{"operationId","fixture-complete"},{"source","ui"}}}});
 auto rows=readRows();bool normal=false;for(const auto& r:rows)if(r.value("operationId","")=="fixture-complete"&&r.value("event","")=="tdp.helper-result")normal=r["exitCode"]==2&&r["processStarted"]==true&&r["bootstrapFailureKind"]=="not-observed";
 check("complete-helper-no-args-is-usage-2-not-python-bootstrap-failure",full["exitCode"]==2&&normal);
 kTdpDaemonExe=(root/L"missing-base"/L"YeManTdpCtl.exe").wstring();
 auto bad=shellHandler({{"program",W2U(kTdpDaemonExe)},{"args",json::array()},{"timeoutMs",10000},{"tdpObservation",{{"operationId","fixture-bootstrap"},{"source","ui"}}}});
 rows=readRows();bool bootstrap=false,terminal=false,runtimeMissing=false;
 for(const auto& r:rows){
  if(r.value("operationId","")=="fixture-bootstrap"&&r.value("event","")=="tdp.failed"){
   terminal=true;bootstrap=r["bootstrapFailureKind"]=="encodings-import-failed"&&r.value("stderr","").find("encodings")!=std::string::npos;
  }
  if(r.value("event","")=="tdp.runtime-identity")for(const auto& file:r["files"])if(file["name"]=="base_library.zip"&&file["status"]=="missing")runtimeMissing=true;
 }
 check("python-not-started-parent-captures-real-encodings-stderr-and-terminal",bad["exitCode"]!=0&&bootstrap&&terminal);
 check("missing-base-library-parent-static-identity",runtimeMissing);
 check("detail-off-failure-is-still-durable-base-evidence",!g_domainLogEnabled[1]&&!rows.empty());
 check("no-hardware-write-or-driver-lease-entered",hardwareLeaseCalls==0);
 check("parent-output-bounded-and-user-directory-redacted",ymcc::tdpdiag::boundedText(std::string(10000,'x')).size()==4096&&ymcc::tdpdiag::boundedText("C:\\Users\\PrivateUser\\secret-file").find("PrivateUser")==std::string::npos);
 check("usage-2-is-not-classified-from-rc-alone",ymcc::tdpdiag::bootstrapKind("usage: set W",true)=="not-observed");
 const auto zipGood=ymcc::tdpdiag::encodingsZip(root/L"zip-good.zip");const auto zipBad=ymcc::tdpdiag::encodingsZip(root/L"zip-bad.zip");
 check("zip-limited-three-encoding-entry-check",zipGood["status"]=="ok"&&zipBad["status"]=="encodings-entry-missing");
 ymcc::tdpdiag::RuntimeIdentityCache cache;unsigned hashes=0;
 auto sha=[&](const std::wstring&){++hashes;return std::string(64,'a');};const auto exe=root/L"complete"/L"YeManTdpCtl.exe";
 cache.snapshot(exe,1,false,sha);const auto first=hashes;
 for(int i=0;i<1000;++i)cache.snapshot(exe,2+i,false,sha);
 check("autofloat-actual-requests-never-trigger-hash-storm",first>0&&hashes==first);
 cache.snapshot(exe,1002,true,sha);const auto retry=hashes;for(int i=0;i<1000;++i)cache.snapshot(exe,1003+i,true,sha);
 check("bootstrap-failure-file-recheck-once-per-identity",retry>first&&hashes==retry);

 // Our executable is a pure fixture child, copied under the production helper name.
 kTdpDaemonExe=(root/L"fake"/L"YeManTdpCtl.exe").wstring();
 auto nonzero=shellHandler({{"program",W2U(kTdpDaemonExe)},{"args",json::array({"--fixture-error"})},{"timeoutMs",1000},{"tdpObservation",{{"operationId","fixture-nonzero"},{"source","ui"}}}});
 rows=readRows();bool nonzeroSaved=false;for(const auto& r:rows)if(r.value("operationId","")=="fixture-nonzero"&&r.value("event","")=="tdp.failed")nonzeroSaved=r["exitCode"]==7&&r.value("stderr","").find("fixture-first-error")!=std::string::npos&&r.value("stderrTruncated",false)&&r.value("stderr","").find("PrivateUser")==std::string::npos&&r.value("stderr","").find("private-token")==std::string::npos;
 check("nonzero-error-bounded-redacted-base-terminal",nonzero["exitCode"]==7&&nonzeroSaved);
 bool timeoutThrown=false;try{auto ignoredTimeout=shellHandler({{"program",W2U(kTdpDaemonExe)},{"args",json::array({"--fixture-timeout"})},{"timeoutMs",100},{"tdpObservation",{{"operationId","fixture-timeout"},{"source","ui"}}}});}catch(const std::exception&){timeoutThrown=true;}
 rows=readRows();bool timeoutSaved=false;for(const auto& r:rows)if(r.value("operationId","")=="fixture-timeout"&&r.value("event","")=="tdp.failed")timeoutSaved=timeoutSaved||(r.value("timeout",false)&&r.value("processStarted",false));
 check("own-fixture-timeout-has-linked-parent-terminal",timeoutThrown&&timeoutSaved);
 std::thread lifecycle([]{Sleep(100);++g_powerGeneration;});
 auto stale=shellHandler({{"program",W2U(kTdpDaemonExe)},{"args",json::array({"--fixture-stale"})},{"timeoutMs",1000},{"tdpObservation",{{"operationId","fixture-stale"},{"source","ui"}}}});lifecycle.join();
 rows=readRows();bool staleSaved=false;for(const auto& r:rows)if(r.value("operationId","")=="fixture-stale"&&r.value("event","")=="tdp.helper-result")staleSaved=r.value("stale",false)&&r.value("ignored",false);
 check("old-generation-result-marked-stale-ignored",staleSaved);
 check("invalid-utf8-evidence-never-loses-whole-json-row",json({{"text",ymcc::tdpdiag::boundedText(std::string("encodings ")+char(0xff))}}).dump().find("encodings")!=std::string::npos);
 // Capture the EXISTING daemon launch options using only a broken pre-Python fixture.
 const auto captureDir=root/L"capture";fspath::create_directories(captureDir);
 auto capture=std::make_unique<ymcc::tdpdiag::DaemonStartupCapture>();const bool prepared=capture->prepare(captureDir);
 std::wstring command=quote_windows_arg((root/L"missing-base"/L"YeManTdpCtl.exe").wstring())+L" daemon --fixture-pre-python-only";
 std::vector<wchar_t> cmd(command.begin(),command.end());cmd.push_back(0);PROCESS_INFORMATION pi{};
 bool created=prepared&&CreateProcessW(nullptr,cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,capture->startup(),&pi);
 const bool firstCreated=created;const DWORD firstLaunchError=created?0:GetLastError();DWORD firstWait=0,firstExit=0;
 json captured=json::object();bool settled=false;
 if(created){capture->started(pi.hProcess,pi.dwProcessId);firstWait=WaitForSingleObject(pi.hProcess,10000);GetExitCodeProcess(pi.hProcess,&firstExit);settled=capture->settle(false,captured);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}
 const bool firstSettled=settled;
 check("daemon-pre-python-encodings-stderr-captured-without-real-daemon",created&&settled&&captured["bootstrapFailureKind"]=="encodings-import-failed"&&captured["processStarted"]==true);
 capture.reset();check("startup-only-capture-cleans-its-own-temp-files",fspath::is_empty(captureDir));
 bool stableCapture=true;
 for(unsigned i=0;i<10;++i){
  auto probe=std::make_unique<ymcc::tdpdiag::DaemonStartupCapture>();const bool preparedProbe=probe->prepare(captureDir);
  overwriteFixtureStack();std::wstring probeCommand=quote_windows_arg((root/L"missing-base"/L"YeManTdpCtl.exe").wstring())+L" daemon --fixture-pre-python-only";
  std::vector<wchar_t> probeArgs(probeCommand.begin(),probeCommand.end());probeArgs.push_back(0);PROCESS_INFORMATION child{};
  const bool launchedProbe=preparedProbe&&CreateProcessW(nullptr,probeArgs.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,probe->startup(),&child);
  json proof;bool capturedProbe=false;if(launchedProbe){probe->started(child.hProcess,child.dwProcessId);WaitForSingleObject(child.hProcess,10000);capturedProbe=probe->settle(false,proof);CloseHandle(child.hThread);CloseHandle(child.hProcess);}
  stableCapture=stableCapture&&launchedProbe&&capturedProbe&&proof.value("bootstrapFailureKind","")=="encodings-import-failed";
 }
 check("startup-capture-HANDLE-LIST-survives-stack-reuse-ten-isolated-launches",stableCapture&&fspath::is_empty(captureDir));

 auto ready=std::make_unique<ymcc::tdpdiag::DaemonStartupCapture>();bool readyPrepared=ready->prepare(captureDir);command=quote_windows_arg(kTdpDaemonExe)+L" --fixture-error";cmd.assign(command.begin(),command.end());cmd.push_back(0);pi={};
 created=readyPrepared&&CreateProcessW(nullptr,cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,ready->startup(),&pi);json readyRow;
 if(created){ready->started(pi.hProcess,pi.dwProcessId);WaitForSingleObject(pi.hProcess,10000);settled=ready->settle(true,readyRow);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}
 check("startup-capture-ready-boundary-is-bounded-and-redacted",created&&settled&&readyRow["stderrTruncated"]==true&&readyRow.value("stderr","").find("private-token")==std::string::npos&&readyRow.value("stderr","").find("PrivateUser")==std::string::npos);ready.reset();
 // Version-relative manifests: compare only when the actual helper identity matches.
 auto putManifest=[&](const json& manifest){std::ofstream out(root/L"complete"/L"tdp-runtime-manifest.json");out<<manifest.dump();};
 putManifest({{"helperSha256",std::string(64,'a')},{"files",json::array({{{"name","_internal/base_library.zip"},{"sha256",std::string(64,'a')}}})}});
 ymcc::tdpdiag::RuntimeIdentityCache matched;check("matching-runtime-manifest-uses-actual-helper-hash",matched.snapshot(exe,1,false,sha)["manifestCheck"]["status"]=="matched");
 putManifest({{"helperSha256",std::string(64,'b')},{"files",json::array()}});
 ymcc::tdpdiag::RuntimeIdentityCache old;check("other-version-manifest-not-used-to-condemn-old-runtime",old.snapshot(exe,1,false,sha)["manifestCheck"]["status"]=="not-checked-manifest-for-other-helper-hash");
 putManifest({{"helperSha256",std::string(64,'a')},{"files",json::array({{{"name","_internal/base_library.zip"},{"sha256",std::string(64,'b')}}})}});
 ymcc::tdpdiag::RuntimeIdentityCache differs;check("matching-helper-manifest-reports-library-hash-difference",differs.snapshot(exe,1,false,sha)["manifestCheck"]["status"]=="identity-differs");
 // Fixed slots retain the first original autofloat error in base and summarize repeats.
 const auto beforeRepeat=readRows().size();for(int i=0;i<64;++i)appendTdpObservation("tdp.failed",{{"source","autofloat"},{"operationId","fixture-float-"+std::to_string(i)},{"error","fixture-original-encodings-error"},{"rc",7}});
 rows=readRows();unsigned firstError=0,summaries=0;for(size_t i=beforeRepeat;i<rows.size();++i){if(rows[i].value("event","")=="tdp.failed")++firstError;if(rows[i].value("event","")=="tdp.failure-summary"&&rows[i].value("repeatCount",0)>=32)++summaries;}
 check("autofloat-detail-off-first-original-error-plus-bounded-repeat-summary",firstError==1&&summaries==2);
 const auto beforeSuccess=rows.size();for(int i=0;i<64;++i)appendTdpObservation("tdp.completed",{{"source","autofloat"},{"operationId","fixture-ok-"+std::to_string(i)}});
 rows=readRows();check("autofloat-detail-off-successes-aggregated-not-every-tick",rows.size()-beforeSuccess==2);
 // Export actual retained base/detail/previous and identity; no live product started.
 const auto data=fspath::path(yemancc_data_dir());std::ofstream(data/L"tdp.log.prev")<<"{\"time\":\"2026-10-11T13:00:00+08:00\",\"event\":\"tdp.request\",\"operationId\":\"fixture-previous\"}\n";
 const auto coverage=tdpExportCoverage(data.wstring());std::ofstream(data/L"tdp-export-coverage.json")<<coverage.dump(2);
 std::ofstream(data/L"manifest.txt")<<"schemaVersion=1\nfixtureOnly=true\ncoverageReceipt=tdp-export-coverage.json\nleafHcEvidence=not-observed-pinned-HC\n";
 bool prevWindow=false,detailGap=false;for(const auto& f:coverage["files"]){if(f["name"]=="tdp.log.prev")prevWindow=f["present"]==true&&f["firstTime"]=="2026-10-11T13:00:00+08:00";if(f["name"]=="tdp-detail.log")detailGap=f["present"]==false;}
 check("coverage-describes-real-retained-previous-time-window-and-detail-gap",prevWindow&&detailGap&&!coverage["detailEnabled"].get<bool>());
 const auto packed=createTarArchiveWithRetry((root/L"tdp-export.zip").wstring(),{(data/L"tdp.log").wstring(),(data/L"tdp.log.prev").wstring(),(data/L"tdp-runtime-identity.json").wstring(),(data/L"tdp-export-coverage.json").wstring()},data.wstring(),L"manifest.txt");
 check("ordinary-export-helper-packs-static-tdp-files-with-manifest",packed.ok);
 json report={{"daemonStartupFixture",{{"prepared",prepared},{"created",firstCreated},{"launchError",firstLaunchError},{"wait",firstWait},{"exitCode",firstExit},{"settled",firstSettled},{"captured",captured}}},{"checks",checks},{"failures",0},{"helperFixturePurpose","no-arguments-only-usage-or-bootstrap-no-Python-body"},{"realHardwareExecuted",false}};for(const auto& c:checks)if(!c["pass"].get<bool>())report["failures"]=report["failures"].get<int>()+1;
 std::cout<<report.dump(2)<<std::endl;return report["failures"].get<int>()?1:0;
}
"""
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--helper-root',type=pathlib.Path,required=True);parser.add_argument('--output-dir',type=pathlib.Path,required=True);args=parser.parse_args()
 run=args.output_dir/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'));run.mkdir(parents=True);fixture=run/'fixtures';fixture.mkdir()
 shutil.copytree(args.helper_root,fixture/'complete')
 shutil.copytree(args.helper_root,fixture/'missing-base',ignore=shutil.ignore_patterns('base_library.zip'))
 for name,entries in [('zip-good.zip',['encodings/__init__.pyc','encodings/aliases.pyc','encodings/utf_8.pyc']),('zip-bad.zip',['encodings/aliases.pyc'])]:
  with zipfile.ZipFile(fixture/name,'w') as z:
   for entry in entries:z.writestr(entry,b'fixture-no-execution')
 text=(ROOT/'native/main.cpp').read_text(encoding='utf-8-sig')
 signatures=['static std::string W2U(const wchar_t* w, int len = -1) {','static std::string W2U(const std::wstring& w) {','static std::wstring U2W(const std::string& s) {','static std::string oemToUtf8(const std::string& bytes) {','static std::string ascii_lower(std::string s) {','static std::wstring quote_windows_arg(const std::wstring& arg) {','static std::string sha256FileCancellable(const std::wstring& path, const std::function<bool()>& cancelled) {','static std::string sha256File(const std::wstring& path) {','static void rotateOneLogFile(const std::wstring& path, unsigned long long maxBytes) {','static std::string tdpObservationRunId() {','static void appendTdpObservation(const char* event, json detail, bool raw = false) noexcept {','static void observeTdpRuntimeIdentity(bool failure=false) noexcept {','static json tdpParentContext(const json& args,const char* route) {','static json tdpCapturedObservation(const std::string& output) noexcept {','static std::string exportReadTail(const std::wstring& path, unsigned long long maxBytes) {','static json tdpExportCoverage(const std::wstring& directory) noexcept {']
 funcs=[extract(text,s) for s in signatures];handler=extract(text,'ipc_on("shell.run", [](const json& a) -> json {');body=handler[handler.index('{'):];archive=text[text.index('static constexpr unsigned int LOG_ARCHIVE_MAX_ATTEMPTS'):text.index('// 进程内实时检测 SMT',text.index('static constexpr unsigned int LOG_ARCHIVE_MAX_ATTEMPTS'))]; archive_prelude=ARCHIVE_PRELUDE[ARCHIVE_PRELUDE.index('struct RunOut'):ARCHIVE_PRELUDE.index('static json checks=')]; code=PRELUDE+'\n'+archive_prelude+'\n'+'\n'.join(funcs)+'\n'+archive+'\nstatic json shellHandler(const json& a) '+body+'\n'+TESTS
 cpp=run/'harness.cpp';cpp.write_text(code,encoding='utf-8');exe=run/'harness.exe';vcvars=pathlib.Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
 command=f'call "{vcvars}" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 "{cpp}" /I"{ROOT/"deps/json"}" /I"{ROOT/"native"}" /Fo"{run/"harness.obj"}" /Fe"{exe}" /link advapi32.lib user32.lib'
 batch=run/'build.cmd';batch.write_text('@echo off\n'+command+'\n',encoding='utf-8');compiled=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,text=True,errors='replace');(run/'compile.log').write_text(compiled.stdout+compiled.stderr,encoding='utf-8')
 if compiled.returncode:print(compiled.stdout+compiled.stderr);return compiled.returncode
 (fixture/'fake').mkdir();shutil.copy2(exe,fixture/'fake'/'YeManTdpCtl.exe')
 import os
 env=dict(os.environ);temp=fixture/'isolated-temp';temp.mkdir();env.update(TEMP=str(temp),TMP=str(temp))
 tested=subprocess.run([str(exe),str(fixture)],cwd=run,env=env,capture_output=True,encoding='utf-8',errors='replace');(run/'result.json').write_text(tested.stdout,encoding='utf-8');(run/'stderr.txt').write_text(tested.stderr,encoding='utf-8')
 if not tested.stdout:print(tested.stderr);return tested.returncode or 1
 report=json.loads(tested.stdout);report['runDirectory']=str(run);report['sourceSha256']=hashlib.sha256(text.encode()).hexdigest();report['helperSha256']=hashlib.sha256((args.helper_root/'YeManTdpCtl.exe').read_bytes()).hexdigest();report['scope']='REAL_NATIVE_PARENT_HANDLER_AND_WRITER_REAL_PYINSTALLER_BOOTSTRAP_FIXTURES_NO_HARDWARE'
 with zipfile.ZipFile(fixture/'tdp-export.zip') as packed:
  names=set(packed.namelist()); expected={'tdp.log','tdp.log.prev','tdp-runtime-identity.json','tdp-export-coverage.json','manifest.txt'}
  valid=expected.issubset(names) and b'fixture-previous' in packed.read('tdp.log.prev') and json.loads(packed.read('tdp-export-coverage.json'))['schemaVersion']==1
  report['checks'].append({'name':'actual-export-zip-content-identity-previous-coverage','pass':valid});report['failures']+=not valid
 report['exportSample']=str(fixture/'tdp-export.zip')
 (args.output_dir/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,indent=2));return tested.returncode
if __name__=='__main__':raise SystemExit(main())
