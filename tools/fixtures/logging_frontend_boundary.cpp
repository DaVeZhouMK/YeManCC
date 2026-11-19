#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
#include "log_frequency_policy.h"
#include "log_jsonl_boundary.h"
#include "monitor_notice_log.h"
#include "tdp_static_observation.h"
#include "log_ai_index.h"
using json=nlohmann::json;namespace fspath=std::filesystem;
static bool fixtureUnknownBirth=false;
static unsigned fixtureProcessTimeQueries=0,fixtureWriterCacheReads=0;
static BOOL fixtureGetProcessTimes(HANDLE process,LPFILETIME creation,LPFILETIME exit,LPFILETIME kernel,LPFILETIME user){
    ++fixtureProcessTimeQueries;if(fixtureUnknownBirth){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    return ::GetProcessTimes(process,creation,exit,kernel,user);
}
#define GetProcessTimes fixtureGetProcessTimes
#include "sleep_fact_log_record.h"
#undef GetProcessTimes
namespace ymcc::sleepfactlog {
[[maybe_unused]] static const WriterIdentity& fixtureWriterIdentity(){++fixtureWriterCacheReads;return writerIdentity();}
}
#define writerIdentity fixtureWriterIdentity
// ACTUAL_IDENTITY
#undef writerIdentity

static std::wstring fixtureRoot;
static std::wstring webview_data_dir(){return fixtureRoot;}
static std::atomic<unsigned long long> g_frontendDiagnosticsFailures{0},g_frontendTraceDetailSuppressed{0},g_frontendPreviewTraceSuppressed{0},g_frontendCalibrationTraceSuppressed{0};
static std::atomic<unsigned long long> g_powerGeneration{1},g_webviewGeneration{1};
static std::atomic<bool> g_domainLogEnabled[2]={false,false};
static std::mutex g_webviewFailureLogMx;
static int g_webviewGpuMode=0;
static std::string g_webviewRouteStatus="fixture-ready";
static std::string webViewLocalTimestamp(bool){return "2000-01-01T00:00:00.000";}
static const char* webViewGpuModeName(int){return "fixture-no-WebView";}
static ULONGLONG fixtureTick=1000;
namespace ymcc::logging {
static unsigned frontendBoundaryOpens=0;
[[maybe_unused]] static bool countedFrontendBoundaryOpen(std::ofstream& out,const fspath::path& path){++frontendBoundaryOpens;return openJsonLineAppend(out,path);}
}
// ACTUAL_ROTATE
// ACTUAL_BOUNDED
#define GetTickCount64() fixtureTick
#define openJsonLineAppend countedFrontendBoundaryOpen
// ACTUAL_TRACE
// ACTUAL_ERROR
// ACTUAL_WEBVIEW
#undef openJsonLineAppend
#undef GetTickCount64
static json checks=json::array();
static void check(const char* name,bool pass){checks.push_back({{"name",name},{"pass",pass}});}
static void seed(const wchar_t* name,const std::string& data){
    fspath::create_directories(fixtureRoot);std::ofstream out(fspath::path(fixtureRoot)/name,std::ios::binary|std::ios::trunc);
    out.write(data.data(),data.size());out.close();if(!out)throw std::runtime_error("private seed failed");
}
static std::string bytes(const wchar_t* name){std::ifstream in(fspath::path(fixtureRoot)/name,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
static std::vector<json> rows(const wchar_t* name){std::vector<json> result;std::ifstream in(fspath::path(fixtureRoot)/name,std::ios::binary);
    for(std::string line;std::getline(in,line);){auto value=json::parse(line,nullptr,false);if(value.is_object())result.push_back(value);}return result;}
static HANDLE denyTail(const wchar_t* name){const auto path=fspath::path(fixtureRoot)/name;HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("private write-only handle failed");return h;}
static bool giveBack(HANDLE h){return CloseHandle(h)!=FALSE;}
static json trace(unsigned epoch,unsigned seq=1){return {{"where","gamepad-admission"},{"message",json{{"stage","owner-start"},{"epoch",epoch},{"qSeq",seq},{"t",seq}}.dump()},{"build","fixture"},{"route","#/fixture"}};}
static json error(const std::string& message="actual-original-error"){return {{"where","vue-runtime"},{"message",message},{"stack","original-stack"},{"build","fixture"}};}
static constexpr const char* fragment="{\"event\":\"old-fragment\",\"x\":";
static void traceCases(bool control){
    seed(L"frontend-trace.log",fragment);const auto input=trace(1);const auto original=input;
    const bool first=appendFrontendTrace(input);auto current=rows(L"frontend-trace.log");
    check("frontend-state-fragment-is-preserved-before-parseable-current-row",first&&current.size()==1&&bytes(L"frontend-trace.log").rfind(std::string(fragment)+"\n",0)==0&&current[0]["trace"]["epoch"]==1);
    if(control)return;
    check("frontend-state-original-message-route-and-input-preserved",input==original&&current[0]["trace"]["qSeq"]==1&&current[0]["route"]=="#/fixture"&&current[0]["lane"]=="event");
    const auto base=bytes(L"frontend-trace.log");const auto opens=ymcc::logging::frontendBoundaryOpens;
    check("frontend-repeated-state-detail-off-does-not-open-or-append",appendFrontendTrace(trace(1,2))&&g_frontendTraceDetailSuppressed==1&&bytes(L"frontend-trace.log")==base&&ymcc::logging::frontendBoundaryOpens==opens);
    seed(L"frontend-trace-detail.log",fragment);g_domainLogEnabled[1]=true;
    check("frontend-repeat-detail-on-repairs-only-detail-stream",appendFrontendTrace(trace(1,2))&&rows(L"frontend-trace-detail.log").size()==1&&rows(L"frontend-trace-detail.log")[0]["trace"]["qSeq"]==2&&bytes(L"frontend-trace.log")==base);
    g_domainLogEnabled[1]=false;
    check("frontend-new-owner-state-remains-resident",appendFrontendTrace(trace(2))&&rows(L"frontend-trace.log").size()==2);
    auto h=denyTail(L"frontend-trace.log");const auto failures=g_frontendDiagnosticsFailures.load();const auto size=base.size();(void)size;
    const bool failed=!appendFrontendTrace(trace(3));const bool returned=giveBack(h);
    check("frontend-base-tail-denial-is-false-and-counted-once",failed&&returned&&g_frontendDiagnosticsFailures==failures+1&&rows(L"frontend-trace.log").size()==2);
    const auto suppressed=g_frontendTraceDetailSuppressed.load();
    check("frontend-failed-base-write-does-not-consume-first-state-witness",appendFrontendTrace(trace(3))&&rows(L"frontend-trace.log").size()==3&&g_frontendTraceDetailSuppressed==suppressed);
    g_domainLogEnabled[1]=true;h=denyTail(L"frontend-trace-detail.log");const auto detailFailures=g_frontendDiagnosticsFailures.load();
    const bool detailFailed=!appendFrontendTrace(trace(3,4));const bool detailReturned=giveBack(h);
    check("frontend-detail-denial-and-next-valid-current-call-are-honest",detailFailed&&detailReturned&&g_frontendDiagnosticsFailures==detailFailures+1&&appendFrontendTrace(trace(3,4))&&rows(L"frontend-trace-detail.log").size()==2);
    g_domainLogEnabled[1]=false;const auto parent=fixtureRoot;fixtureRoot=(fspath::path(parent)/L"valid-no-lf").wstring();seed(L"frontend-trace.log","{\"event\":\"old-valid\"}");
    check("frontend-valid-old-state-without-lf-keeps-two-rows",appendFrontendTrace(trace(90))&&rows(L"frontend-trace.log").size()==2);
    fixtureRoot=(fspath::path(parent)/L"rotate").wstring();const std::string old(1024*1024,'x');seed(L"frontend-trace.log",old);
    check("frontend-original-real-rotation-precedes-new-stream-boundary",appendFrontendTrace(trace(91))&&bytes(L"frontend-trace.log.1")==old&&rows(L"frontend-trace.log").size()==1&&bytes(L"frontend-trace.log").front()=='{');
}
static void errorCases(bool control){
    seed(L"frontend-errors.log",fragment);const auto input=error();const auto original=input;
    check("frontend-error-fragment-is-preserved-before-current-error",appendFrontendDiagnostic(input)&&rows(L"frontend-errors.log").size()==1&&bytes(L"frontend-errors.log").rfind(std::string(fragment)+"\n",0)==0);
    if(control)return;
    check("frontend-real-errors-stay-resident-and-input-unmodified",appendFrontendDiagnostic(input)&&rows(L"frontend-errors.log").size()==2&&rows(L"frontend-errors.log").back()["severity"]=="error"&&input==original);
    auto both=trace(7);both["stack"]="real-stack";
    check("frontend-structured-message-with-stack-is-not-demoted",appendFrontendDiagnostic(both)&&rows(L"frontend-errors.log").size()==3&&rows(L"frontend-trace.log").empty());
    check("frontend-original-legacy-state-error-endpoint-still-routes-to-trace",appendFrontendDiagnostic(trace(8))&&rows(L"frontend-trace.log").size()==1&&rows(L"frontend-errors.log").size()==3);
    check("frontend-unstructured-message-without-stack-stays-error",appendFrontendDiagnostic({{"where","gamepad-admission"},{"message","real-unstructured-error"}})&&rows(L"frontend-errors.log").size()==4);
    auto h=denyTail(L"frontend-errors.log");const auto before=g_frontendDiagnosticsFailures.load();const bool failed=!appendFrontendDiagnostic(error("refused-error"));const bool returned=giveBack(h);
    check("frontend-error-unreadable-tail-fails-with-one-counter-increment",failed&&returned&&g_frontendDiagnosticsFailures==before+1&&rows(L"frontend-errors.log").size()==4);
    check("frontend-error-recovers-on-next-call-without-replaying-refused-row",appendFrontendDiagnostic(error("new-after-failure"))&&rows(L"frontend-errors.log").size()==5&&bytes(L"frontend-errors.log").find("refused-error")==std::string::npos);
    const auto parent=fixtureRoot;fixtureRoot=(fspath::path(parent)/L"valid-no-lf").wstring();seed(L"frontend-errors.log","{\"event\":\"old-valid\"}");
    auto privacy=error(std::string(999,'x')+"中文");privacy["stack"]="Authorization: Bearer private-token C:\\Users\\PrivateUser\\file";
    const bool written=appendFrontendDiagnostic(privacy);const auto all=rows(L"frontend-errors.log");
    check("frontend-error-no-lf-unicode-and-existing-privacy-rules-preserved",written&&all.size()==2&&all.back()["messageTruncated"]==true&&all.back().dump().find("private-token")==std::string::npos&&all.back().dump().find("PrivateUser")==std::string::npos);
}
static void webviewCases(bool control){
    seed(L"webview-failures.log",fragment);json original={{"event","process-failed"},{"reason","crashed"},{"exitCode",7}};appendWebViewDiagnostic(original);
    check("webview-fragment-is-preserved-before-current-diagnostic",rows(L"webview-failures.log").size()==1&&bytes(L"webview-failures.log").rfind(std::string(fragment)+"\n",0)==0);
    if(control)return;
    check("webview-original-fields-and-metadata-kept-without-input-mutation",original==json({{"event","process-failed"},{"reason","crashed"},{"exitCode",7}})&&rows(L"webview-failures.log").back()["gpuMode"]=="fixture-no-WebView"&&rows(L"webview-failures.log").back()["exitCode"]==7);
    auto h=denyTail(L"webview-failures.log");appendWebViewDiagnostic({{"event","not-replayed"}});const bool returned=giveBack(h);
    check("webview-read-denial-keeps-void-path-and-original-counter-separate",returned&&rows(L"webview-failures.log").size()==1&&g_frontendDiagnosticsFailures==0);
    appendWebViewDiagnostic({{"event","new-after-denial"}});
    check("webview-next-valid-current-event-recovers-without-replay",rows(L"webview-failures.log").size()==2&&rows(L"webview-failures.log").back()["event"]=="new-after-denial"&&bytes(L"webview-failures.log").find("not-replayed")==std::string::npos);
    seed(L"webview-failures.log","{\"event\":\"old-valid\"}");appendWebViewDiagnostic({{"event","after-valid"}});
    check("webview-complete-old-json-without-lf-has-two-valid-rows",rows(L"webview-failures.log").size()==2);
}
static void budgetCases(){
    const auto root=fixtureRoot;
    const auto input=[](const char* where,const json& value){return json{{"where",where},{"message",value.dump()}};};
    fixtureRoot=(fspath::path(root)/L"preview").wstring();seed(L"frontend-trace.log",fragment);auto h=denyTail(L"frontend-trace.log");
    const auto preview=[&](unsigned n){return input("controller-preview",{{"schemaVersion",1},{"stage","window"},{"policy","VC-PREVIEW-TRACE-20261024-R1"},{"n",n}});};
    const bool previewFailed=!appendFrontendTrace(preview(1));const bool previewReturned=giveBack(h);bool previewOk=true;
    for(unsigned n=2;n<=12;++n)previewOk&=appendFrontendTrace(preview(n));const auto opens=ymcc::logging::frontendBoundaryOpens;
    check("frontend-preview-failed-write-spends-original12-budget",previewFailed&&previewReturned&&previewOk&&rows(L"frontend-trace.log").size()==11);
    check("frontend-preview-cap-refuses-extra-write-without-extra-tail-read",appendFrontendTrace(preview(13))&&g_frontendPreviewTraceSuppressed==1&&rows(L"frontend-trace.log").size()==11&&ymcc::logging::frontendBoundaryOpens==opens);
    fixtureRoot=(fspath::path(root)/L"calibration").wstring();seed(L"frontend-trace.log",fragment);h=denyTail(L"frontend-trace.log");
    const auto calibration=[&](unsigned n){return input("controller-calibration",{{"schemaVersion",1},{"stage","observe"},{"phase","probe"},{"policy","YMCC-AXIS-CALIBRATION-TRACE-20261025-R1"},{"n",n}});};
    const bool calibrationFailed=!appendFrontendTrace(calibration(1));const bool calibrationReturned=giveBack(h);bool calibrationOk=true;
    for(unsigned n=2;n<=120;++n)calibrationOk&=appendFrontendTrace(calibration(n));
    check("frontend-calibration-failed-write-spends-original120-budget",calibrationFailed&&calibrationReturned&&calibrationOk&&rows(L"frontend-trace.log").size()==119);
    check("frontend-calibration-original-cap-returns-false-and-separate-suppression",!appendFrontendTrace(calibration(121))&&g_frontendCalibrationTraceSuppressed==1&&rows(L"frontend-trace.log").size()==119);
    fixtureRoot=(fspath::path(root)/L"notice").wstring();seed(L"frontend-trace.log",fragment);h=denyTail(L"frontend-trace.log");
    const auto notice=[&](unsigned n){return input("monitor-notice-lifecycle",{{"stage","healthy"},{"epoch",n}});};
    const bool noticeFailed=!appendFrontendTrace(notice(1));const bool noticeReturned=giveBack(h);bool noticeOk=true;
    for(unsigned n=2;n<=256;++n)noticeOk&=appendFrontendTrace(notice(n));
    check("frontend-notice-failed-write-spends-original256-budget",noticeFailed&&noticeReturned&&noticeOk&&rows(L"frontend-trace.log").size()==255);
    const auto failures=g_frontendDiagnosticsFailures.load();
    check("frontend-notice-original-cap-remains-false-and-counted",!appendFrontendTrace(notice(257))&&g_frontendDiagnosticsFailures==failures+1&&rows(L"frontend-trace.log").size()==255);
}
static void identityCases(const std::wstring& selection){
    const bool unknown=selection==L"identity-unknown";
    fixtureUnknownBirth=unknown;
    const bool first=appendFrontendTrace(trace(31));auto saved=rows(L"frontend-trace.log");
    check("frontend-current-writer-envelope-is-present",first&&saved.size()==1&&saved[0].value("writerEnvelopeSchema",0)==1&&saved[0].contains("writerPid")&&saved[0]["writerPid"]==GetCurrentProcessId());
    if(selection==L"identity-control")return;
    FILETIME created{},exit{},kernel{},user{};const bool processObserved=::GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user)!=FALSE;
    const auto expected=unknown?json(nullptr):json(std::to_string(ymcc::sleepfactlog::fileTimeValue(created)));
    check("frontend-birth-is-current-process-exact-decimal-or-null",processObserved&&saved[0]["writerProcessCreation100ns"]==expected&&fixtureProcessTimeQueries==1);
    const auto cacheReads=fixtureWriterCacheReads;const auto openCalls=ymcc::logging::frontendBoundaryOpens;
    bool quiet=true;for(unsigned i=0;i<50000;++i)quiet&=appendFrontendTrace(trace(31,i+2));
    check("frontend50000-quiet-repeats-do-not-enter-writer-identity-or-file",quiet&&fixtureWriterCacheReads==cacheReads&&ymcc::logging::frontendBoundaryOpens==openCalls&&rows(L"frontend-trace.log").size()==1&&g_frontendTraceDetailSuppressed==50000);
    g_domainLogEnabled[1]=true;const bool detailed=appendFrontendTrace(trace(31,50002));auto detail=rows(L"frontend-trace-detail.log");
    check("frontend-detail-row-keeps-original-values-and-same-writer",detailed&&detail.size()==1&&detail[0]["writerPid"]==GetCurrentProcessId()&&detail[0]["writerProcessCreation100ns"]==expected&&detail[0]["trace"]["qSeq"]==50002);
    g_domainLogEnabled[1]=false;const auto input=error("original-real-error");const auto original=input;
    const bool errorWritten=appendFrontendDiagnostic(input);auto errors=rows(L"frontend-errors.log");
    check("frontend-error-writer-is-added-without-changing-error-or-input",errorWritten&&input==original&&errors.size()==1&&errors[0]["severity"]=="error"&&errors[0]["message"]=="original-real-error"&&errors[0]["writerPid"]==GetCurrentProcessId()&&errors[0]["writerProcessCreation100ns"]==expected);
    const json payload={{"event","process-failed"},{"pid",777},{"browserProcessId",888},{"writerPid",999},{"writerProcessCreation100ns","old-birth"},{"writerEnvelopeSchema",false},{"nested",{{"unknown",17}}}};
    appendWebViewDiagnostic(payload);const auto web=rows(L"webview-failures.log");
    check("webview-subject-pids-and-unknown-payload-are-not-native-writer",web.size()==1&&web[0]["writerPid"]==GetCurrentProcessId()&&web[0]["writerProcessCreation100ns"]==expected&&web[0]["pid"]==777&&web[0]["browserProcessId"]==888&&web[0]["nested"]["unknown"]==17);
    check("webview-conflicting-writer-claims-are-retained-separately",web[0]["producerWriterPid"]==999&&web[0]["producerWriterProcessCreation100ns"]=="old-birth"&&web[0]["producerWriterEnvelopeSchema"]==false&&web[0]["writerEnvelopeSchema"]==1);
    const auto beforeFailure=g_frontendDiagnosticsFailures.load();auto h=denyTail(L"frontend-trace.log");const bool refused=!appendFrontendTrace(trace(32));const bool returned=giveBack(h);
    check("frontend-identity-does-not-turn-tail-refusal-into-success",refused&&returned&&g_frontendDiagnosticsFailures==beforeFailure+1&&rows(L"frontend-trace.log").size()==1);
    check("frontend-after-refusal-current-call-retains-new-state-and-writer",appendFrontendTrace(trace(32))&&rows(L"frontend-trace.log").size()==2&&rows(L"frontend-trace.log").back()["writerProcessCreation100ns"]==expected);
    check("frontend-all-kept-lanes-share-one-cached-process-time-query",fixtureProcessTimeQueries==1);
}

static json ownerPhaseTrace(bool after,unsigned seq=1){
    return {{"where","gamepad-admission"},{"route","#/fixture"},{"message",json{
        {"ownerTransitionSchema",1},{"stage",after?"owner-result":"owner-start"},{"phase",after?"after-call":"before-call"},
        {"transition","start"},{"trigger","window-focus"},{"owner","none"},{"epoch",1},{"rearm",true},
        {"sourceAdmission",false},{"sourcePresent",true},{"qSeq",seq},{"t",seq}}.dump()}};
}
static void ownerPhaseCases(bool control){
    const auto before=ownerPhaseTrace(false),after=ownerPhaseTrace(true);
    check("owner-phase-first-attempt-and-returned-state-are-two-resident-witnesses",appendFrontendTrace(before)&&appendFrontendTrace(after)&&rows(L"frontend-trace.log").size()==2);
    const auto firstBytes=bytes(L"frontend-trace.log");const auto firstOpens=ymcc::logging::frontendBoundaryOpens;
    const bool repeated=appendFrontendTrace(ownerPhaseTrace(false,2))&&appendFrontendTrace(ownerPhaseTrace(true,2));
    check("owner-phase-repeat-pair-does-not-evict-each-other-into-resident-file",repeated&&bytes(L"frontend-trace.log")==firstBytes&&ymcc::logging::frontendBoundaryOpens==firstOpens&&g_frontendTraceDetailSuppressed==2);
    if(control)return;
    const auto guide=ymcc::logexport::aiReadingGuide();
    check("owner-phase-guide-is-built-from-actual-export-header",guide.contains("frontendOwnerTransitions")&&guide["frontendOwnerTransitions"].contains("unknown")&&guide["frontendOwnerTransitions"].contains("frequency"));
    bool quiet=true;for(unsigned i=0;i<50000;++i){quiet&=appendFrontendTrace(ownerPhaseTrace(false,i+3));quiet&=appendFrontendTrace(ownerPhaseTrace(true,i+3));}
    check("owner-phase50000-pairs-stay-detail-off-without-file-or-identity-entry",quiet&&bytes(L"frontend-trace.log")==firstBytes&&ymcc::logging::frontendBoundaryOpens==firstOpens&&g_frontendTraceDetailSuppressed==100002&&fixtureWriterCacheReads==firstOpens);
    g_domainLogEnabled[1]=true;const bool detail=appendFrontendTrace(ownerPhaseTrace(false,50003))&&appendFrontendTrace(ownerPhaseTrace(true,50004));const auto detailRows=rows(L"frontend-trace-detail.log");g_domainLogEnabled[1]=false;
    check("owner-phase-repeat-detail-on-keeps-both-actual-phases-and-sequences",detail&&detailRows.size()==2&&detailRows[0]["trace"]["phase"]=="before-call"&&detailRows[0]["trace"]["qSeq"]==50003&&detailRows[1]["trace"]["phase"]=="after-call"&&detailRows[1]["trace"]["qSeq"]==50004&&bytes(L"frontend-trace.log")==firstBytes);
    auto changed=after;auto value=json::parse(changed["message"].get<std::string>());value["owner"]="game-consumer";value["epoch"]=2;value["sourceAdmission"]=true;changed["message"]=value.dump();
    check("owner-phase-real-returned-state-change-remains-resident",appendFrontendTrace(changed)&&rows(L"frontend-trace.log").size()==3&&rows(L"frontend-trace.log").back()["trace"]["owner"]=="game-consumer");
    const auto count=rows(L"frontend-trace.log").size();const bool legacy=appendFrontendTrace(trace(99));const auto suppressed=g_frontendTraceDetailSuppressed.load();
    check("owner-phase-new-slots-preserve-original-legacy-gamepad-slot",legacy&&appendFrontendTrace(ownerPhaseTrace(false,50005))&&appendFrontendTrace(trace(99,2))&&rows(L"frontend-trace.log").size()==count+1&&g_frontendTraceDetailSuppressed==suppressed+2);
    auto denied=changed;value["epoch"]=3;denied["message"]=value.dump();auto h=denyTail(L"frontend-trace.log");const auto failures=g_frontendDiagnosticsFailures.load();
    const bool refused=!appendFrontendTrace(denied),returned=giveBack(h);const auto afterDenied=rows(L"frontend-trace.log").size();
    check("owner-phase-failed-sink-does-not-consume-returned-state",refused&&returned&&g_frontendDiagnosticsFailures==failures+1&&appendFrontendTrace(denied)&&rows(L"frontend-trace.log").size()==afterDenied+1);
    const auto beforePower=rows(L"frontend-trace.log").size();++g_powerGeneration;
    check("owner-phase-original-power-generation-invalidates-both-fixed-slots",appendFrontendTrace(before)&&appendFrontendTrace(denied)&&rows(L"frontend-trace.log").size()==beforePower+2);
    auto unrecognized=before;auto raw=json::parse(before["message"].get<std::string>());raw["ownerTransitionSchema"]=true;unrecognized["message"]=raw.dump();const auto priorFailure=g_frontendDiagnosticsFailures.load();
    check("owner-phase-invalid-schema-keeps-original-generic-trace-path",appendFrontendTrace(unrecognized)&&g_frontendDiagnosticsFailures==priorFailure&&rows(L"frontend-trace.log").back()["trace"]["ownerTransitionSchema"]==true);
    check("owner-phase-real-error-stays-in-original-error-lane",appendFrontendDiagnostic(error("owner-phase-original-real-error"))&&rows(L"frontend-errors.log").size()==1&&rows(L"frontend-errors.log")[0]["severity"]=="error");
}

static void execGuardCases(const wchar_t* inputPath){
    std::ifstream in(inputPath,std::ios::binary);json report;in>>report;
    std::vector<json> inputRows;for(const auto& value:report.at("traces"))if(value.value("stage",std::string{})=="exec-drop")inputRows.push_back(value);
    if(inputRows.empty())throw std::runtime_error("selected frontend report has no exec-drop rows");
    bool accepted=true;for(const auto& value:inputRows){const json input={{"where","gamepad-admission"},{"message",value.dump()},{"route","#/fixture"},{"build","fixture"}};accepted=appendFrontendTrace(input)&&accepted;}
    const auto saved=rows(L"frontend-trace.log");bool same=accepted&&saved.size()==inputRows.size();unsigned guards=0,legacy=0;
    for(std::size_t i=0;i<saved.size()&&i<inputRows.size();++i){same=same&&saved[i].at("trace")==inputRows[i];if(saved[i]["trace"].contains("execGuard"))++guards;else ++legacy;}
    check("actual complete Native writer preserves every frontend exec-drop scalar and skipped null",same&&guards==5&&legacy==1);
    bool identity=true;for(const auto& row:saved)identity=identity&&row.value("writerEnvelopeSchema",0)==1&&row.contains("writerProcessCreation100ns")&&row.value("lane",std::string{})=="event";
    check("existing writer identity and low-frequency changed-state lane are retained",identity);
    json repeat=inputRows.front();repeat["qSeq"]=600;repeat["t"]=600;const auto wrap=[&](const json& v){return json{{"where","gamepad-admission"},{"message",v.dump()},{"route","#/fixture"},{"build","fixture"}};};
    check("existing semantic fingerprint can be selected without a new trace channel",appendFrontendTrace(wrap(repeat)));
    const auto beforeBase=bytes(L"frontend-trace.log"),beforeDetail=bytes(L"frontend-trace-detail.log");const auto opens=ymcc::logging::frontendBoundaryOpens,cacheReads=fixtureWriterCacheReads;const auto suppressed=g_frontendTraceDetailSuppressed.load();
    bool quiet=true;for(unsigned i=0;i<20000;++i){repeat["qSeq"]=601+i;repeat["t"]=601+i;quiet=appendFrontendTrace(wrap(repeat))&&quiet;}
    check("20000 repeated rejection traces detail OFF keep original no-open no-identity and suppression routing",quiet&&bytes(L"frontend-trace.log")==beforeBase&&bytes(L"frontend-trace-detail.log")==beforeDetail&&ymcc::logging::frontendBoundaryOpens==opens&&fixtureWriterCacheReads==cacheReads&&g_frontendTraceDetailSuppressed==suppressed+20000);
    g_domainLogEnabled[1]=true;check("detail ON writes original phase booleans and skipped null unchanged",appendFrontendTrace(wrap(repeat))&&rows(L"frontend-trace-detail.log").size()==1&&rows(L"frontend-trace-detail.log").back()["trace"]==repeat);g_domainLogEnabled[1]=false;
    const auto baseCount=rows(L"frontend-trace.log").size();auto changed=inputRows[1];check("changed first predicate outcome remains an original low-frequency state edge",appendFrontendTrace(wrap(changed))&&rows(L"frontend-trace.log").size()==baseCount+1&&rows(L"frontend-trace.log").back()["trace"]==changed);
    check("other exec-drop branch without guard stays legacy rather than fabricated",legacy==1&&std::any_of(saved.begin(),saved.end(),[](const json& row){return row["trace"].contains("entryGate")&&!row["trace"].contains("execGuard");}));
    check("actual compiled export guide explains original short-circuit versus later rereads",ymcc::logexport::aiReadingGuide().contains("frontendExecutionGuard"));
}

int wmain(int argc,wchar_t** argv){
    if(argc!=3&&argc!=4)return 2;const std::wstring selection=argv[2];
    if((argc==4&&selection!=L"exec-guard")||(selection==L"exec-guard"&&argc!=4))return 2;
    if(selection!=L"trace"&&selection!=L"error"&&selection!=L"webview"&&selection!=L"budgets"&&selection!=L"trace-control"&&selection!=L"error-control"&&selection!=L"webview-control"&&selection!=L"identity-known"&&selection!=L"identity-unknown"&&selection!=L"identity-control"&&selection!=L"owner-phase"&&selection!=L"owner-phase-control"&&selection!=L"exec-guard")return 2;
    fixtureRoot=argv[1];if(fspath::exists(fixtureRoot))return 2;fspath::create_directories(fixtureRoot);
    if(selection==L"trace"||selection==L"trace-control")traceCases(selection==L"trace-control");
    if(selection==L"error"||selection==L"error-control")errorCases(selection==L"error-control");
    if(selection==L"webview"||selection==L"webview-control")webviewCases(selection==L"webview-control");
    if(selection==L"budgets")budgetCases();
    if(selection==L"exec-guard")execGuardCases(argv[3]);
    if(selection==L"owner-phase"||selection==L"owner-phase-control")ownerPhaseCases(selection==L"owner-phase-control");
    if(selection==L"identity-known"||selection==L"identity-unknown"||selection==L"identity-control")identityCases(selection);
    unsigned failures=0;for(const auto& value:checks)failures+=!value["pass"].get<bool>();
    std::cout<<json{{"checks",checks},{"failures",failures},{"frontendFailures",g_frontendDiagnosticsFailures.load()},
        {"detailSuppressed",g_frontendTraceDetailSuppressed.load()},{"helperOpenCalls",ymcc::logging::frontendBoundaryOpens},
        {"processTimeQueries",fixtureProcessTimeQueries},{"writerCacheReads",fixtureWriterCacheReads},
        {"scope","actual complete frontend state/error/WebView writers and rotation with real private files;clock/WebView labels controlled;no renderer/product/device"}}.dump(2)<<std::endl;
    return failures?3:0;
}
