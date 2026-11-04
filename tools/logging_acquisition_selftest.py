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

# Explicit config-storage selection in this original consumer. The legacy sample
# exporter and TEST assertions below retain their existing entry/coverage.
IDENTITY_PRELUDE=r"""
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include "json.hpp"
#include "tdp_static_observation.h"
using json=nlohmann::json;namespace fspath=std::filesystem;
static std::wstring fixtureData,kTdpDaemonExe,POWER_CONTROL_DIR;
static std::atomic<unsigned long long> g_tdpObservationFailures{0};
static std::vector<json> logAttempts;
static unsigned hashCalls=0,writeCalls=0;
static int writeFault=0;static bool flushFault=false,lostPublication=false;
static unsigned long long fixtureTick=100;
static ULONGLONG identityTick(){return fixtureTick;}
#define GetTickCount64 identityTick
static std::wstring yemancc_data_dir(){return fixtureData;}
static std::string sha256File(const std::wstring&){++hashCalls;return std::string(64,'a');}
static void appendTdpObservation(const char* event,json value,bool=false) noexcept{try{value["event"]=event;logAttempts.push_back(std::move(value));}catch(...){++g_tdpObservationFailures;}}
static BOOL identityWriteFile(HANDLE h,LPCVOID p,DWORD n,LPDWORD done,LPOVERLAPPED o){++writeCalls;if(writeFault==1){SetLastError(ERROR_WRITE_FAULT);return FALSE;}return ::WriteFile(h,p,writeFault==2&&n?n-1:n,done,o);}
static BOOL identityFlushFileBuffers(HANDLE h){if(flushFault){SetLastError(ERROR_WRITE_FAULT);return FALSE;}return ::FlushFileBuffers(h);}
static BOOL identityMoveFileExW(LPCWSTR from,LPCWSTR to,DWORD flags){if(lostPublication){::DeleteFileW(from);return TRUE;}return ::MoveFileExW(from,to,flags);}
#define WriteFile identityWriteFile
#define FlushFileBuffers identityFlushFileBuffers
#define MoveFileExW identityMoveFileExW
"""
IDENTITY_TEST=r"""
static void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static void put(const fspath::path& path,const std::string& value){fspath::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);file.write(value.data(),static_cast<std::streamsize>(value.size()));file.flush();need(bool(file),"fixture write failed");}
static std::string bytes(const fspath::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
int wmain(int argc,wchar_t** argv){try{
 if(argc<2||argc>3)return 2;const fspath::path root=argv[1];fspath::create_directories(root);const std::string selected=argc==3?W2U(argv[2]):"";json cases=json::array();
 auto path=[&]{return fspath::path(fixtureData)/L"tdp-runtime-identity.json";};
 auto live=[&]{return json::parse(bytes(path()));};
 auto valid=[&]{const auto state=live();need(state.is_object()&&state["schemaVersion"]==1&&state["observedOnly"]==true&&state["files"].is_array()&&state["revision"].is_number_integer(),"identity publication missing or invalid");return state;};
 auto check=[&](const std::string& name,const std::function<void(const fspath::path&)>& run){if(!selected.empty()&&selected!=name)return;const auto dir=root/std::to_wstring(cases.size());fspath::create_directories(dir/L"data");fspath::create_directories(dir/L"helper"/L"_internal");fixtureData=(dir/L"data").wstring();POWER_CONTROL_DIR=(dir/L"owned-PowerControl").wstring();kTdpDaemonExe=(dir/L"helper"/L"YeManTdpCtl.exe").wstring();put(kTdpDaemonExe,"owned-data-not-an-executable");fixtureTick+=30001;g_tdpObservationFailures=0;hashCalls=writeCalls=0;writeFault=0;flushFault=lostPublication=false;logAttempts.clear();try{run(dir);cases.push_back({{"name",name},{"ok",true},{"hashCalls",hashCalls},{"writeCalls",writeCalls},{"logAttempts",logAttempts.size()}});}catch(const std::exception& error){cases.push_back({{"name",name},{"ok",false},{"error",error.what()},{"hashCalls",hashCalls},{"writeCalls",writeCalls},{"logAttempts",logAttempts.size()}});}};
 check("identity healthy publication uses actual cache and owned file",[&](const auto&){observeTdpRuntimeIdentity();const auto state=valid();need(state["helperPath"]==W2U(kTdpDaemonExe)&&state["failureTriggered"]==false&&logAttempts.size()==1,"healthy producer data wrong");});
 check("identity locked first publication preserves bytes and explicit retry heals",[&](const auto&){const std::string old="{old";put(path(),old);HANDLE held=CreateFileW(path().c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);need(held!=INVALID_HANDLE_VALUE,"fixture lock failed");observeTdpRuntimeIdentity();CloseHandle(held);need(bytes(path())==old&&g_tdpObservationFailures>0,"lock failure was destructive or unreported");const auto hashes=hashCalls;observeTdpRuntimeIdentity();valid();need(hashCalls==hashes,"retry unnecessarily rescanned helper");});
 check("identity missing parent failure retries same metadata revision",[&](const auto&){fspath::remove(fixtureData);observeTdpRuntimeIdentity();need(!fspath::exists(path())&&g_tdpObservationFailures>0,"missing parent failure not observed");fspath::create_directories(fixtureData);observeTdpRuntimeIdentity();valid();});
 check("identity external corruption after publish heals on next request",[&](const auto&){observeTdpRuntimeIdentity();valid();const std::string bad="{\"files\":";put(path(),bad);observeTdpRuntimeIdentity();valid();bool archived=false;for(const auto& entry:fspath::directory_iterator(fixtureData))if(entry.is_regular_file()&&entry.path().filename().wstring().find(L"tdp-runtime-identity.json.corrupt-")==0&&bytes(entry.path())==bad)archived=true;need(archived,"external damaged preimage lost");});
 check("identity future file remains intact and reports unknown publication",[&](const auto&){observeTdpRuntimeIdentity();auto state=valid();state["schemaVersion"]=2;put(path(),state.dump());const auto before=static_cast<unsigned long long>(g_tdpObservationFailures);observeTdpRuntimeIdentity();need(bytes(path())==state.dump()&&g_tdpObservationFailures>before,"future file overwritten or treated as published current schema");fspath::remove(path());observeTdpRuntimeIdentity();valid();});
 check("identity locked healthy file is not silently accepted as current",[&](const auto&){observeTdpRuntimeIdentity();valid();HANDLE held=CreateFileW(path().c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);need(held!=INVALID_HANDLE_VALUE,"fixture lock failed");const auto before=static_cast<unsigned long long>(g_tdpObservationFailures);observeTdpRuntimeIdentity();CloseHandle(held);need(g_tdpObservationFailures>before,"unreadable persisted evidence silently treated as healthy");observeTdpRuntimeIdentity();valid();});
 check("identity repeated healthy requests avoid duplicate writes hashes and logs",[&](const auto&){observeTdpRuntimeIdentity();const auto original=bytes(path());const auto hashes=hashCalls,writes=writeCalls;for(int i=0;i<100;++i)observeTdpRuntimeIdentity();need(bytes(path())==original&&hashCalls==hashes&&writeCalls==writes&&logAttempts.size()==1,"healthy repeat churned publication or observations");});
 check("identity damaged initial snapshot preserved before regeneration",[&](const auto&){const std::string bad="{broken";put(path(),bad);observeTdpRuntimeIdentity();valid();bool archived=false;for(const auto& entry:fspath::directory_iterator(fixtureData))if(entry.is_regular_file()&&entry.path().filename().wstring().find(L"tdp-runtime-identity.json.corrupt-")==0&&bytes(entry.path())==bad)archived=true;need(archived,"old corrupt evidence overwritten without preservation");});
 check("identity healthy extensions survive explicit refresh",[&](const auto&){observeTdpRuntimeIdentity();auto state=valid();state["extension"]={{"note","keep"}};put(path(),state.dump());observeTdpRuntimeIdentity();need(live()["extension"]==state["extension"],"healthy extension erased");});
 check("identity failure evidence is not downgraded by later normal request",[&](const auto&){observeTdpRuntimeIdentity();observeTdpRuntimeIdentity(true);need(valid()["failureTriggered"]==true,"failure evidence missing");const auto original=bytes(path());observeTdpRuntimeIdentity();need(bytes(path())==original&&live()["failureTriggered"]==true,"normal request downgraded failure evidence");});
 check("identity concurrent explicit requests keep complete snapshots",[&](const auto&){auto normal=[&]{for(int i=0;i<8;++i)observeTdpRuntimeIdentity();};auto failure=[&]{for(int i=0;i<8;++i)observeTdpRuntimeIdentity(true);};std::thread a(normal),b(failure);a.join();b.join();need(valid()["failureTriggered"]==true&&logAttempts.size()<=2,"concurrent metadata publication torn or duplicated");});

 check("identity future record before first observation cannot be overwritten",[&](const auto&){const std::string future="{\"schemaVersion\":2,\"extension\":\"keep\"}";put(path(),future);observeTdpRuntimeIdentity();need(bytes(path())==future&&g_tdpObservationFailures>0,"first publication overwrote future evidence");fspath::remove(path());observeTdpRuntimeIdentity();valid();});
 check("identity regular file parent failure heals after directory repair",[&](const auto&){fspath::remove(fixtureData);put(fixtureData,"parent-is-a-file");observeTdpRuntimeIdentity();need(bytes(fixtureData)=="parent-is-a-file"&&g_tdpObservationFailures>0,"bad parent not preserved");fspath::remove(fixtureData);fspath::create_directories(fixtureData);observeTdpRuntimeIdentity();valid();});
 check("identity repeated malformed bytes and wrong types recover with preimages",[&](const auto&){observeTdpRuntimeIdentity();for(const auto& bad:std::vector<std::string>{"", "[]", "true", "{bad", std::string("{\"bad\":\"")+char(0xff)+"\"}", "{\"schemaVersion\":1,\"observedOnly\":false}"}){put(path(),bad);observeTdpRuntimeIdentity();valid();bool archived=false;for(const auto& entry:fspath::directory_iterator(fixtureData))if(entry.is_regular_file()&&entry.path().filename().wstring().find(L"tdp-runtime-identity.json.corrupt-")==0&&bytes(entry.path())==bad)archived=true;need(archived,"storm preimage lost");}need(logAttempts.size()==1,"corrupt document caused duplicate identity log storm");});
 check("identity decoded duplicate keys cannot count as valid evidence",[&](const auto&){observeTdpRuntimeIdentity();auto raw=bytes(path());raw.pop_back();raw+=",\"observedO\\u006ely\":true}";put(path(),raw);observeTdpRuntimeIdentity();valid();bool archived=false;for(const auto& entry:fspath::directory_iterator(fixtureData))if(entry.is_regular_file()&&entry.path().filename().wstring().find(L"tdp-runtime-identity.json.corrupt-")==0&&bytes(entry.path())==raw)archived=true;need(archived,"duplicate state silently accepted");});
 check("identity oversized evidence preserved then explicit repair recovers",[&](const auto&){const std::string huge((1u<<20)+1,'x');put(path(),huge);observeTdpRuntimeIdentity();need(bytes(path())==huge&&g_tdpObservationFailures>0,"oversized original consumed or overwritten");fspath::remove(path());observeTdpRuntimeIdentity();valid();});
 check("identity short write and flush failure retain old complete snapshot",[&](const auto&){observeTdpRuntimeIdentity();const auto first=valid();const auto original=bytes(path());put(kTdpDaemonExe,"changed helper bytes with another metadata signature");fixtureTick+=30001;for(int fault:{1,2}){writeFault=fault;observeTdpRuntimeIdentity();need(bytes(path())==original,"failed or short write replaced old evidence");}writeFault=0;flushFault=true;observeTdpRuntimeIdentity();need(bytes(path())==original,"unflushed publication replaced old evidence");flushFault=false;observeTdpRuntimeIdentity();need(valid()["revision"]>first["revision"]&&logAttempts.size()==2,"write recovery failed or churned logs");});
 check("identity silently lost publication not marked successful and retries",[&](const auto&){observeTdpRuntimeIdentity();const auto first=valid();const auto original=bytes(path());put(kTdpDaemonExe,"changed again for lost publication");fixtureTick+=30001;const auto failures=static_cast<unsigned long long>(g_tdpObservationFailures);lostPublication=true;observeTdpRuntimeIdentity();lostPublication=false;need(bytes(path())==original&&g_tdpObservationFailures>failures,"writer acknowledgment incorrectly accepted");observeTdpRuntimeIdentity();need(valid()["revision"]>first["revision"]&&logAttempts.size()==2,"lost publication retry failed");});
 check("identity real replacement sharing lock retains old snapshot and heals",[&](const auto&){observeTdpRuntimeIdentity();const auto first=valid();const auto original=bytes(path());put(kTdpDaemonExe,"changed for replacement sharing lock");fixtureTick+=30001;HANDLE held=CreateFileW(path().c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);need(held!=INVALID_HANDLE_VALUE,"fixture lock failed");observeTdpRuntimeIdentity();CloseHandle(held);need(bytes(path())==original&&g_tdpObservationFailures>0,"locked replacement changed original or lied");observeTdpRuntimeIdentity();need(valid()["revision"]>first["revision"],"replacement lock recovery failed");});
 check("identity damaged original archive failure cannot authorize overwrite",[&](const auto&){const std::string bad="{corrupt";put(path(),bad);writeFault=1;observeTdpRuntimeIdentity();need(bytes(path())==bad&&g_tdpObservationFailures>0,"failed archive lost preimage");writeFault=0;observeTdpRuntimeIdentity();valid();});
 check("identity BOM extension remains unchanged while healthy",[&](const auto&){observeTdpRuntimeIdentity();auto value=valid();value["extension"]={{"note","中文"}};const auto raw=std::string("\xef\xbb\xbf")+value.dump();put(path(),raw);const auto writes=writeCalls;observeTdpRuntimeIdentity();need(bytes(path())==raw&&writeCalls==writes&&logAttempts.size()==1,"healthy BOM/extension churned");});
 check("identity valid byte-boundary extension permits next compact update",[&](const auto&){observeTdpRuntimeIdentity();auto value=valid();value["extension"]="";const auto size=value.dump().size();value["extension"]=std::string((1u<<20)-size-64,'x');put(path(),value.dump());put(kTdpDaemonExe,"changed boundary helper");fixtureTick+=30001;observeTdpRuntimeIdentity();const auto final=valid();need(final["revision"]>value["revision"]&&final["extension"]==value["extension"]&&fspath::file_size(path())<=(1u<<20),"valid bounded extension blocked fresh evidence");});

 check("identity supported runtime manifest hashes retain read-only match",[&](const auto& dir){const json manifest={{"schemaVersion",1},{"helperSha256",std::string(64,'a')},{"pythonVersion","fixture"},{"files",json::array({json{{"name","YeManTdpCtl.exe"},{"sha256",std::string(64,'a')}}})},{"extension",{{"note","keep"}}}};const auto manifestPath=dir/L"helper"/L"tdp-runtime-manifest.json";put(manifestPath,std::string("\xef\xbb\xbf")+manifest.dump());observeTdpRuntimeIdentity();need(valid()["manifestCheck"]["status"]=="matched"&&bytes(manifestPath)==std::string("\xef\xbb\xbf")+manifest.dump(),"healthy bounded manifest not consumed or changed");});
 check("identity malformed or future manifest never attests matched",[&](const auto& dir){const auto manifestPath=dir/L"helper"/L"tdp-runtime-manifest.json";const json good={{"schemaVersion",1},{"helperSha256",std::string(64,'a')},{"files",json::array({json{{"name","YeManTdpCtl.exe"},{"sha256",std::string(64,'a')}}})}};std::vector<std::string> bad;auto value=good;value["schemaVersion"]=2;bad.push_back(value.dump());value=good;value["schemaVersion"]="1";bad.push_back(value.dump());value=good;value["files"]=json::array({nullptr,12,"bad"});bad.push_back(value.dump());auto duplicate=good.dump();duplicate.pop_back();duplicate+=",\"helperSha25\\u0036\":\""+std::string(64,'a')+"\"}";bad.push_back(duplicate);json nested=json::object();for(int i=0;i<70;++i)nested={{"n",nested}};value=good;value["extension"]=nested;bad.push_back(value.dump());for(const auto& raw:bad){put(manifestPath,raw);fixtureTick+=30001;observeTdpRuntimeIdentity();const auto snapshot=valid();need(snapshot["manifestCheck"].is_object()&&snapshot["manifestCheck"].value("status",std::string{})!="matched"&&bytes(manifestPath)==raw,"invalid runtime manifest falsely attested matched");}put(manifestPath,good.dump());fixtureTick+=30001;observeTdpRuntimeIdentity();need(valid()["manifestCheck"]["status"]=="matched","manifest correction did not recover");});

 check("identity runtime manifest sharing denial heals without byte change",[&](const auto& dir){const auto manifestPath=dir/L"helper"/L"tdp-runtime-manifest.json";const json good={{"schemaVersion",1},{"helperSha256",std::string(64,'a')},{"files",json::array({json{{"name","YeManTdpCtl.exe"},{"sha256",std::string(64,'a')}}})}};put(manifestPath,good.dump());HANDLE held=CreateFileW(manifestPath.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);need(held!=INVALID_HANDLE_VALUE,"manifest lock failed");observeTdpRuntimeIdentity();CloseHandle(held);const auto blocked=valid()["manifestCheck"];need(!blocked.is_object()||blocked.value("status",std::string{})!="matched","locked manifest falsely matched");fixtureTick+=30001;observeTdpRuntimeIdentity();const auto healed=valid()["manifestCheck"];need(healed.is_object()&&healed.value("status",std::string{})=="matched"&&bytes(manifestPath)==good.dump(),"unchanged manifest remained stuck after sharing denial");});
 if(cases.empty()||(!selected.empty()&&cases.size()!=1))throw std::runtime_error("No unique original identity case selected");unsigned failed=0;for(const auto& c:cases)if(!c["ok"].get<bool>())++failed;std::cout<<json{{"cases",cases},{"passed",cases.size()-failed},{"failed",failed}}.dump(2);return failed?1:0;
}catch(const std::exception& error){std::cerr<<error.what();return 2;}}
"""


def run_identity_owned_child(label,args,budget_ms,run,*,cwd=None):
 """Reuse the original gated Win32 Job runner; no name-based process cleanup."""
 import os,time,shutil
 host=os.environ.get('YMCC_IDENTITY_PROCESS_HOST') or shutil.which('pwsh.exe') or shutil.which('pwsh')
 if not host or not pathlib.Path(host).is_file():raise ValueError('Existing PowerShell7 owned-process host required')
 helper=ROOT/'tools/ymcc_sim/global/host-process.ps1'
 argumentFile=run/(label+'-arguments.json');argumentFile.write_text(json.dumps(args[1:],ensure_ascii=False),encoding='utf-8')
 jobReceipt=run/(label+'-JOB.json');started=datetime.datetime.now(datetime.timezone.utc).isoformat();tick=time.perf_counter();deadline=int(time.time()*1000)+budget_ms
 command=[host,'-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',str(helper),'-ProgramPath',str(args[0]),'-ArgumentsPath',str(argumentFile),'-DeadlineEpochMs',str(deadline),'-CleanupReserveMs',str(min(1000,max(100,budget_ms//5))),'-DeadlineReceiptPath',str(jobReceipt)]
 record={'cwd':str(ROOT if cwd is None else cwd),'programCommand':list(map(str,args)),'ownedWrapperCommand':command,'startedAtUtc':started,'budgetMs':budget_ms,'status':'STARTED','scope':'Only gated owned process and descendants. Existing Job kill-on-close; original program budget includes host startup/cleanup reserve.','identity':[]}
 for file in[helper,pathlib.Path(host),pathlib.Path(args[0])]:
  if file.is_file():record['identity'].append({'path':str(file),'bytes':file.stat().st_size,'sha256':hashlib.sha256(file.read_bytes()).hexdigest()})
 process=None
 try:
  with(run/(label+'.stdout')).open('wb')as stdout,(run/(label+'.stderr')).open('wb')as stderr:
   process=subprocess.Popen(command,cwd=ROOT if cwd is None else cwd,stdout=stdout,stderr=stderr,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0));record['pid']=process.pid
   try:record['exitCode']=process.wait(timeout=budget_ms/1000)
   except subprocess.TimeoutExpired:
    record['status']='TIMEOUT';record['outerDeadlineExpired']=True;process.kill();record['forcedOwnedWrapperTermination']=True
    try:record['exitCode']=process.wait(timeout=2)
    except subprocess.TimeoutExpired:record['cleanupUnconfirmed']=True
  if record['status']!='TIMEOUT':record['status']='EXITED'if record.get('exitCode')==0 else'FAILED'
 finally:
  if process is not None and process.poll()is None:
   record['forcedOwnedWrapperTermination']=True;process.kill()
   try:record['exitCode']=process.wait(timeout=2)
   except subprocess.TimeoutExpired:record['cleanupUnconfirmed']=True
  record['endedAtUtc']=datetime.datetime.now(datetime.timezone.utc).isoformat();record['elapsedMs']=(time.perf_counter()-tick)*1000
  record['jobReceipt']=str(jobReceipt)
  if jobReceipt.is_file():record['job']=json.loads(jobReceipt.read_text(encoding='utf-8-sig'))
  else:record['jobReceiptMissing']=True
  record['naturalOrOwnedWrapperTerminated']=process is not None and process.poll()is not None
  record['programTreeCleanupProvenByExistingJob']=bool(record.get('job',{}).get('jobDisposed'))
  (run/(label+'-PROCESS.json')).write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
 return record


# Independently selected diagnostic ceilings, not reconstructed historical limits.
SAMPLE_DIAGNOSTIC_LIMITS_MS={'total':60000,'compile':30000,'native':15000,'finalCleanupReserve':2000}
def run_sample_owned_child(label,args,run,deadline):
 """Original sample argv/cwd under the existing owned Job; no successful-log fallback."""
 import time,re
 if label not in ('compile','native'):raise ValueError('Known original sample process stage required')
 remaining_ms=int((deadline-time.monotonic())*1000)
 effective_ms=min(SAMPLE_DIAGNOSTIC_LIMITS_MS[label],remaining_ms-SAMPLE_DIAGNOSTIC_LIMITS_MS['finalCleanupReserve'])
 if effective_ms<=0:
  record={'status':'NOT_RUN_BUDGET_EXHAUSTED','exitCode':124,'programCommand':list(map(str,args)),'cwd':str(run),'originalHistoricalLimitMs':None,'independentTotalLimitMs':SAMPLE_DIAGNOSTIC_LIMITS_MS['total'],'selectedStageLimitMs':SAMPLE_DIAGNOSTIC_LIMITS_MS[label],'remainingBeforeMs':remaining_ms,'effectiveStageLimitMs':max(0,effective_ms),'cleanupReserveMs':SAMPLE_DIAGNOSTIC_LIMITS_MS['finalCleanupReserve'],'accepted':False}
  (run/('sample-'+label+'-PROCESS.json')).write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
  return {'process':record,'stdout':'','stderr':'Sample diagnostic budget exhausted before stage','accepted':False}
 record=run_identity_owned_child('sample-'+label,args,effective_ms,run,cwd=run)
 stdout=(run/('sample-'+label+'.stdout')).read_text(encoding='utf-8',errors='replace')
 stderr=(run/('sample-'+label+'.stderr')).read_text(encoding='utf-8',errors='replace')
 stdout=re.sub(r'^YMCC_SIM_OWNED_JOB childPid=[0-9]+\r?\n','',stdout,count=1)
 job=record.get('job')
 accepted=bool(record.get('status')=='EXITED'and record.get('exitCode')==0 and isinstance(job,dict)and job.get('schemaVersion')==1 and job.get('exitCode')==0 and job.get('status')=='EXITED'and job.get('jobDisposed')is True and job.get('deadlineExpired')is False and not record.get('cleanupUnconfirmed'))
 record.update(originalHistoricalLimitMs=None,independentTotalLimitMs=SAMPLE_DIAGNOSTIC_LIMITS_MS['total'],selectedStageLimitMs=SAMPLE_DIAGNOSTIC_LIMITS_MS[label],remainingBeforeMs=remaining_ms,effectiveStageLimitMs=effective_ms,cleanupReserveMs=SAMPLE_DIAGNOSTIC_LIMITS_MS['finalCleanupReserve'],accepted=accepted)
 (run/('sample-'+label+'-PROCESS.json')).write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
 return {'process':record,'stdout':stdout,'stderr':stderr,'accepted':accepted}

def runtime_identity_state_main(a):
 import os,re,time
 run=a.output_dir
 run.mkdir(parents=True,exist_ok=False)
 sourcePath=a.identity_source or ROOT/'native/main.cpp';source=sourcePath.read_text(encoding='utf-8-sig')
 def take(name):
  hits=[]
  for m in re.finditer(r'^static [^\n]+ '+re.escape(name)+r'\(',source,re.M):
   brace=source.find('{',m.start());semi=source.find(';',m.start())
   if brace<0 or (semi>=0 and semi<brace):continue
   if name=='W2U' and not source.startswith('static std::string W2U(const wchar_t*',m.start()):continue
   hits.append(m)
  if len(hits)!=1:raise ValueError('Missing/ambiguous actual function '+name)
  start=hits[0].start();return extract(source,source[start:source.index('{',start)])
 names=['startupParseJson','startupReadJsonFile','tdpRuntimeIdentityDocumentValid','publishTdpRuntimeIdentitySnapshot','observeTdpRuntimeIdentity'];names=[n for n in names if re.search(r'^static [^\n]+ '+n+r'\(',source,re.M)]
 prefix=take('W2U')+'\n'+next(line for line in source.splitlines() if line.startswith('static std::string W2U(const std::wstring&'))
 atomic=extract(source,'bool sgWriteFileAtomicFlags(')+'\n'+take('sgWriteFileAtomic')
 generated=IDENTITY_PRELUDE+'\n'+prefix+'\n'+atomic+'\n'+'\n\n'.join(take(n)for n in names)+'\n'+IDENTITY_TEST
 headerPath=a.identity_header or ROOT/'native/tdp_static_observation.h'
 if a.identity_header:
  headerName=str(a.identity_header.resolve()).replace('\\','/')
  if any(c in headerName for c in ['"','\n','\r']):raise ValueError('Invalid private header path')
  generated=generated.replace('#include "tdp_static_observation.h"','#include "'+headerName+'"',1)
 cpp=run/'identity-consumer.cpp';cpp.write_text(generated,encoding='utf-8');exe=run/'identity-consumer.exe';environment=a.compiler_environment
 if not environment or not environment.is_file():raise ValueError('Explicit existing compiler environment required for this isolated selection')
 batch=run/'build.cmd';batch.write_text('@echo off\r\ncall "'+str(environment)+'" >nul\r\nif errorlevel 1 exit /b 1\r\nwhere cl.exe > "'+str(run/'cl-path.txt')+'"\r\nwhere link.exe > "'+str(run/'link-path.txt')+'"\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /sourceDependencies "'+str(run/'dependencies.json')+'" "'+str(cpp)+'" /I"'+str(ROOT/'deps/json')+'" /I"'+str(ROOT/'native')+'" /Fo"'+str(run/'identity-consumer.obj')+'" /Fe"'+str(exe)+'"\r\nexit /b %errorlevel%\r\n',encoding='utf-8')
 inputs=[sourcePath,ROOT/'tools/logging_acquisition_selftest.py',ROOT/'tools/log_frequency_selftest.py',ROOT/'tools/steam_log_export_selftest.py',headerPath,ROOT/'deps/json/json.hpp',environment,ROOT/'tools/ymcc_sim/global/host-process.ps1',cpp]
 def identities():return [{'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'bytes':p.stat().st_size}for p in inputs]
 report={'status':'STARTED','mode':'runtime-identity-state','sourcePath':str(sourcePath),'generatedCpp':{'path':str(cpp),'sha256':hashlib.sha256(cpp.read_bytes()).hexdigest()},'functions':names,'before':identities(),'budgetsMs':{'compile':120000,'run':60000},'scope':'Real actual producer/cache/files plus clock/hash/logger and Win32 write fault ports; dummy helper never executed. No installed files, TDP commands or real logs.'}
 (run/'START.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
 def child(label,args,timeout):return run_identity_owned_child(label,args,timeout*1000,run)
 compiled=child('compile',['cmd.exe','/d','/c',str(batch)],120);report['compile']=compiled
 if compiled['exitCode']!=0:report['status']='SETUP_FAIL'
 else:
  command=[str(exe),str(run/'fixtures')]+([a.identity_case]if a.identity_case else[]);tested=child('native',command,60);report['native']=tested
  try:
   nativeOutput=(run/'native.stdout').read_text(encoding='utf-8');nativeOutput=re.sub(r'^YMCC_SIM_OWNED_JOB childPid=[0-9]+\r?\n','',nativeOutput,count=1);result=json.loads(nativeOutput);(run/'raw-result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');report.update(passed=result['passed'],failed=result['failed'],failedCases=[r for r in result['cases']if not r['ok']]);report['status']='PASS'if not result['failed']and tested['exitCode']==0 else'FAIL'
  except Exception as error:report.update(status='SETUP_FAIL',reportFailure=str(error))
 report['after']=identities();report['sourceOrToolDrift']=[b['path']for b,c in zip(report['before'],report['after'])if b['sha256']!=c['sha256']]
 dependencies=run/'dependencies.json'
 if dependencies.is_file():report['actualIncludes']=[{'path':p,'sha256':hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()}for p in json.loads(dependencies.read_text(encoding='utf-8-sig')).get('Data',{}).get('Includes',[])]
 report['compileTools']=[]
 for label in ['cl','link']:
  file=run/(label+'-path.txt')
  if file.is_file():
   for row in file.read_text(encoding='utf-8').splitlines():
    tool=pathlib.Path(row.strip())
    if tool.is_file():report['compileTools'].append({'path':str(tool),'sha256':hashlib.sha256(tool.read_bytes()).hexdigest(),'role':label})
 (run/'RESULT.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps({k:report.get(k)for k in ['status','passed','failed','failedCases','sourceOrToolDrift']},ensure_ascii=False,indent=2));return 0 if report['status']=='PASS'else 1


def main():
 p=argparse.ArgumentParser();p.add_argument('--sample-zip',type=pathlib.Path);p.add_argument('--output-dir',type=pathlib.Path,required=True);p.add_argument('--runtime-identity-state',action='store_true');p.add_argument('--identity-case');p.add_argument('--identity-source',type=pathlib.Path);p.add_argument('--identity-header',type=pathlib.Path);p.add_argument('--compiler-environment',type=pathlib.Path);a=p.parse_args()
 if a.runtime_identity_state:return runtime_identity_state_main(a)
 if a.sample_zip is None:p.error('--sample-zip is required for the original export consumer')
 import time
 sampleStarted=time.monotonic();sampleDeadline=sampleStarted+SAMPLE_DIAGNOSTIC_LIMITS_MS['total']/1000
 run=a.output_dir/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'));run.mkdir(parents=True);fixture=run/'fixtures';sample=fixture/'sample';sample.mkdir(parents=True)
 with zipfile.ZipFile(a.sample_zip)as z:
  for name in ['input-capture.jsonl','native-lifecycle.log','frontend-errors.log']:(sample/name).write_bytes(z.read(name))
 source=(ROOT/'native/main.cpp').read_text(encoding='utf-8-sig');signatures=['static void rotateOneLogFile(const std::wstring& path, unsigned long long maxBytes) {','static std::string boundedJsonString(const json& value, const char* key, size_t limit) {','static bool appendFrontendTrace(const json& input) noexcept {','static bool appendFrontendDiagnostic(const json& input) noexcept {','static bool nativeLifecycleRecordIsStreaming(const char* event, const json& detail) {']
 funcs='\n'.join(extract(source,s)for s in signatures);start=source.index('static constexpr unsigned int LOG_ARCHIVE_MAX_ATTEMPTS');helper=source[start:source.index('// 进程内实时检测 SMT',start)]
 cpp=run/'fixture.cpp';cpp.write_text(PRELUDE+'\n'+EXTRA+'\n'+funcs+'\n'+helper+'\n'+TEST,encoding='utf-8');exe=run/'fixture.exe';vc=r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat';batch=run/'build.cmd';batch.write_text(f'@echo off\ncall "{vc}" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 "{cpp}" /I"{ROOT/"deps/json"}" /I"{ROOT/"native"}" /Fo"{run/"fixture.obj"}" /Fe"{exe}" /link advapi32.lib user32.lib\n')
 build=run_sample_owned_child('compile',['cmd.exe','/d','/c',str(batch)],run,sampleDeadline);(run/'compile.log').write_text(build['stdout']+build['stderr'],encoding='utf-8')
 if not build['accepted']:print(build['stdout']+build['stderr']);return build['process'].get('exitCode') or 1
 tested=run_sample_owned_child('native',[str(exe),str(fixture)],run,sampleDeadline);(run/'stderr.txt').write_text(tested['stderr'],encoding='utf-8')
 if not tested['accepted']:print(tested['stderr']);return tested['process'].get('exitCode') or 1
 if not tested['stdout']:print(tested['stderr']);return tested['process'].get('exitCode') or 1
 report=json.loads(tested['stdout'])
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
 sampleExpired=time.monotonic()>sampleDeadline
 report['independentDiagnosticBudget']={'limitsMs':SAMPLE_DIAGNOSTIC_LIMITS_MS,'elapsedMs':(time.monotonic()-sampleStarted)*1000,'historicalLimitsReconstructed':False,'expired':sampleExpired}
 report['executionStatus']='TIMEOUT'if sampleExpired else'EXITED'
 (a.output_dir/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False,indent=2));return 124 if sampleExpired else 1 if report['failures']else 0

def control_engine_diff():
 baseline=pathlib.Path(r'G:\YeManCC-Work\Build\Validation\LoggingAcquisition-20261011-194855\pre-edit\src\gamepad\engine.ts').read_text(encoding='utf-8-sig');current=(ROOT/'src/gamepad/engine.ts').read_text(encoding='utf-8-sig');expected=baseline.replace("import { reportFrontendError } from '@/robust/frontendDiagnostics';","import { reportFrontendTrace } from '@/robust/frontendDiagnostics';",1).replace('// reportFrontendError → native diagnostics.frontendError → frontend-errors.log','// reportFrontendTrace → native diagnostics.frontendTrace → frontend-trace(.detail).log',1).replace("reportFrontendError('gamepad-admission', JSON.stringify({ stage, action, ...gamepadGateSnapshot(), ...detail }));","reportFrontendTrace('gamepad-admission', { stage, action, ...gamepadGateSnapshot(), ...detail });",1);return expected==current
if __name__=='__main__':raise SystemExit(main())
