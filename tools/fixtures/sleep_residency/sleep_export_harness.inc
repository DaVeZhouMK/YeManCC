// Same sleep.residency-evidence-consumer execution. Pure helper plus the actual
// export adapter, with inert Windows/command ports and owned local test files.
#include "sleep_export_diagnostics.h"
#include "log_ai_index.h"
#include "input_pipe_test_allocation_fault.h"
#include <filesystem>
#include <mutex>
#include <sstream>
#include <cwchar>
#include <type_traits>
namespace sleep_export_fixture {
using Json=ymcc::sleepexport::Json;
using QueryResult=ymcc::sleepexport::QueryResult;
inline Json anomaly(std::uint64_t sequence=1) {
    return {{"event","s0-low-power-residency"},{"timestampUtc","2026-10-25T15:24:52.072Z"},
        {"writerInstanceId","native:fixture:birth"},{"writerPid",73},{"writerProcessCreation100ns","134000000000000000"},
        {"writerSequence",sequence},{"details",{{"source","Microsoft-Windows-Kernel-Power/507"},
            {"timing","after-standby-exit"},{"assessment","no-software-drips-recorded"},
            {"fileTime","2026-10-25 23:24:52.072"},{"automaticRepairAuthorized",false},{"invalidFields",Json::array()},
            {"values",{{"SleepEntered",true},{"DurationInUs",120000000u},{"DripsResidencyInUs",0u},
                {"HwDripsResidencyInUs",0u},{"DripsTransitions",0u},{"ActiveResidencyInUs",120000000u}}}}}};
}
inline Json queryAnomaly(std::uint64_t sequence=1) {
    auto fact=anomaly(sequence);
    fact["details"]["values"]["DurationInUs"]=600000000u;
    fact["details"]["values"]["ActiveResidencyInUs"]=600000000u;
    return fact;
}
inline Json longScreenOff(std::uint64_t sequence=9) {
    auto fact=anomaly(sequence);
    fact["event"]="s0-long-screen-off-not-entered";
    fact["details"]={{"source","Microsoft-Windows-Kernel-Power/506+507"},
        {"entryFileTime100ns",134400511910830000ULL},{"exitFileTime100ns",134400598550180000ULL},
        {"entryReason",12},{"exitReason",1},{"generation",9971},{"screenOffUs",8661077336ULL},
        {"sessionUs",8661077406ULL},{"windowsSleepEntered",false},{"powerActionAuthorized",false},
        {"detailLogsRequired",false},{"mandatoryEvidence",true}};
    return fact;
}
inline Json g3eAcScreenOff() {
    auto fact=longScreenOff(10);
    fact["event"]="s0-long-screen-off-ac-segment-not-entered";
    auto& d=fact["details"];
    d["entryFileTime100ns"]=134399013948900000ULL;
    d["exitFileTime100ns"]=134399678408347094ULL;
    d["exitReason"]=28;
    d["sessionUs"]=66445945042ULL;
    d["screenOffUs"]=66445944305ULL;
    d["sessionContinues"]=true;
    d["windowsPowerStateAc"]=true;
    return fact;
}
inline Json win5ReasonFiveScreenOff(bool continuing) {
    auto fact=continuing?g3eAcScreenOff():longScreenOff();
    auto& d=fact["details"];
    d["entryReason"]=5;
    d["exitReason"]=continuing?28:8;
    d["generation"]=6596392;
    d["entryFileTime100ns"]=continuing?134392400845360000ULL:134392554217850000ULL;
    d["exitFileTime100ns"]=continuing?134392554155450000ULL:134392584947300000ULL;
    d["sessionUs"]=continuing?15331011509ULL:3072946969ULL;
    d["screenOffUs"]=continuing?15331010340ULL:3072946045ULL;
    return fact;
}
inline QueryResult report(const std::string& xml="<?xml version=\"1.0\"?><SleepStudy/>") {
    QueryResult q;q.ran=true;q.completion="child-exit";q.elapsedMs=7;q.xml=xml;return q;
}
inline std::string utf16(const std::string& ascii,bool little) {
    std::string out=little?std::string("\xff\xfe",2):std::string("\xfe\xff",2);
    for(const auto c:ascii){out.push_back(little?c:'\0');out.push_back(little?'\0':c);}return out;
}
inline unsigned deadlinePolicyTests() {
    using namespace ymcc::sleepexport;unsigned checks=0;
    const auto expect=[&](bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);};
    DeadlineCooldown cache;QueryResult deadline;deadline.ran=true;deadline.exitCode=1460;
    deadline.completion="deadline";deadline.elapsedMs=60015;
    expect(!cache.active(9,100),"no deadline observation cannot suppress a query");
    cache.observe(9,100,deadline,"original");
    expect(cache.active(9,100)&&cache.active(9,60099),"deadline reuse covers age zero through59999ms");
    expect(!cache.active(9,60100),"exact60000ms deadline expiry does not reuse");
    expect(!cache.active(10,100),"another generation never reuses a timeout");
    expect(!cache.active(9,99),"clock rollback never extends reuse");
    cache.observe(9,100,report(),"successful");expect(!cache.active(9,100),"a success clears prior timeout suppression");
    for(unsigned mode=0;mode<6;++mode) {
        auto invalid=deadline;
        if(mode==0)invalid.ran=false;
        if(mode==1)invalid.exitCode=0;
        if(mode==2)invalid.win32Error=5;
        if(mode==3)invalid.completion="canceled";
        if(mode==4)invalid.readError="read-failed";
        if(mode==5)invalid.xml="<unexpected/>";
        cache.observe(9,100,deadline,"original");cache.observe(9,101,invalid,"rejected");
        expect(!cache.active(9,101),"only the original completed diagnostic deadline is reusable");
    }
    cache.observe(9,100,deadline,"original");unsigned queries=0;
    const auto query=[&](std::uint32_t){++queries;return report();};
    auto stopped=collect({},[](){return false;},query,&cache,9,101);
    expect(stopped["reason"]=="export-context-not-current"&&queries==0&&!stopped.contains("recentDeadline"),
        "noncurrent context cannot expose the previous timeout as a current diagnostic");
    unsigned contexts=0;auto changed=collect({},[&](){return ++contexts==1;},query,&cache,9,101);
    expect(changed["reason"]=="export-context-changed"&&queries==0&&contexts==2&&cache.completedTick==100,
        "context change vetoes reuse without query or renewing its original deadline");
    return checks;
}
inline unsigned helperTests() {
    using namespace ymcc::sleepexport;unsigned checks=0;
    const auto expect=[&](bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);};
    // Field145759 exhausted the original eight-second diagnostic allowance.
    const auto delayed=collect({},[](){return true;},[](std::uint32_t limit){
        auto q=report();q.elapsedMs=9000;
        if(limit<9000){q.completion="deadline";q.exitCode=1460;q.xml.clear();}
        return q;
    });
    expect(delayed["reportAvailable"]==true&&delayed["queryAttempts"]==1&&
        delayed["targetPowerState"]=="UNKNOWN"&&!delayed["sleepSuccessWitness"].get<bool>(),
        "diagnostic allowance admits a nine-second report beyond the old eight-second cutoff");
    const auto selected=anomaly();const std::vector<std::string> valid={selected.dump()};
    expect(selectAnomaly(valid)==selected,"export selects original completed507 identity at exact120s silent-zero boundary");
    auto later=anomaly(2);auto positive=later;positive["details"]["assessment"]="hardware-residency-recorded";
    expect(selectAnomaly({selected.dump(),later.dump(),positive.dump()})==later,"export chooses latest qualifying historical anomaly despite newer positive record");
    const auto off=longScreenOff();
    expect(selectAnomaly({later.dump(),off.dump(),positive.dump()})==off,
        "export selects the newer confirmed long screen-off failure without any DRIPS counters");
    auto afterPowerChange=off;
    auto& segment=afterPowerChange["details"];
    segment["entryReason"]=28;
    segment["entryFileTime100ns"]=134399678408350175ULL;
    segment["exitFileTime100ns"]=134399990201484595ULL;
    segment["sessionUs"]=31179314305ULL;
    segment["screenOffUs"]=31179313981ULL;
    expect(selectAnomaly({later.dump(),afterPowerChange.dump(),positive.dump()})==afterPowerChange,
        "export keeps the G3E battery segment after the power-source change");
    const auto ac=g3eAcScreenOff();
    expect(selectAnomaly({later.dump(),ac.dump(),positive.dump()})==ac,
        "export associates the G3E continuing AC segment without relight or DRIPS");
    expect(reportHistoryDays(afterPowerChange,134401466270000000ULL)==3&&
        reportHistoryDays(later,134401466270000000ULL)==1,
        "delayed G3E battery export looks back to original segment entry; auxiliary DRIPS keeps default history");
    for(const auto& change:std::vector<std::pair<std::string,Json>>{{"entryReason",28},{"exitReason",1},
        {"sessionContinues",false},{"sessionContinues",nullptr},{"windowsPowerStateAc",false},{"windowsPowerStateAc","true"},
        {"windowsSleepEntered",true},{"powerActionAuthorized",true}}) {
        auto invalid=ac;invalid["details"][change.first]=change.second;
        expect(selectAnomaly({later.dump(),invalid.dump()})==later,"export rejects an ambiguous continuing AC segment");
    }
    for(const auto& change:std::vector<std::pair<std::string,Json>>{{"source","other"},{"entryReason",1},
        {"entryReason",4294967324ULL},
        {"exitReason",20},{"windowsSleepEntered",true},{"powerActionAuthorized",true},
        {"screenOffUs",1u},{"sessionUs",1u},{"generation",0u},{"entryFileTime100ns",0u},
        {"exitFileTime100ns",134400511910830000ULL}}) {
        auto invalid=off;invalid["details"][change.first]=change.second;
        expect(selectAnomaly({later.dump(),invalid.dump()})==later,"export rejects an incomplete or inconsistent screen-off failure");
    }
    for(const char* missing:{"HwDripsResidencyInUs","DripsTransitions"}) {
        auto optional=selected;optional["details"]["values"].erase(missing);
        expect(selectAnomaly({optional.dump()})==optional,"missing optional hardware or transitions permits diagnostic without fabricating zero");
    }
    auto optional=selected;optional["details"]["values"].erase("HwDripsResidencyInUs");optional["details"]["values"].erase("DripsTransitions");
    expect(selectAnomaly({optional.dump()})==optional,"proven software zero alone permits historical diagnostic with both optional counters absent");
    std::vector<Json> rejected;
    for(const char* key:{"SleepEntered","DurationInUs","DripsResidencyInUs"}) {
        auto bad=selected;bad["details"]["values"].erase(key);rejected.push_back(std::move(bad));
    }
    for(const auto& change:std::vector<std::pair<std::string,Json>>{{"SleepEntered",false},{"SleepEntered","true"},{"DurationInUs",119999999u},
        {"DurationInUs",-1},{"DurationInUs",120000000.0},{"DurationInUs","120000000"},{"DurationInUs",UINT64_MAX},
        {"DripsResidencyInUs",1u},{"HwDripsResidencyInUs",1u},{"DripsTransitions",1u},
        {"HwDripsResidencyInUs",nullptr},{"DripsTransitions","0"},{"ActiveResidencyInUs",120000001u},
        {"FirstDripsEntryInUs",1u},{"SleepSwDripsResidencyInUs",1u},{"SleepHwDripsResidencyInUs",1u}}) {
        auto bad=selected;bad["details"]["values"][change.first]=change.second;rejected.push_back(std::move(bad));
    }
    for(const auto& change:std::vector<std::pair<std::string,Json>>{{"source","other/507"},{"timing","during-standby"},
        {"assessment","short-session"},{"values",Json::array()},{"invalidFields",Json::array({"DurationInUs"})},
        {"invalidFields",false},{"automaticRepairAuthorized",true}}) {
        auto bad=selected;bad["details"][change.first]=change.second;rejected.push_back(std::move(bad));
    }
    auto wrongEvent=selected;wrongEvent["event"]="kernel-power";rejected.push_back(wrongEvent);
    for(const auto& bad:rejected)expect(selectAnomaly({bad.dump()}).is_null(),"export rejects missing malformed inconsistent or positive completed-session evidence");
    for(const std::string bad:{"","{","null","[]","{\"details\":null}"})expect(selectAnomaly({bad}).is_null(),"export skips malformed or non-fact JSON");
    std::vector<std::string> tail(maxFacts,"null");tail[0]=selected.dump();
    expect(selectAnomaly(tail)==selected,"export includes exact256-row tail boundary");tail.push_back("null");
    expect(selectAnomaly(tail).is_null(),"export never searches beyond last256 rows");
    auto padded=selected.dump();padded.resize(maxFactBytes,' ');
    expect(selectAnomaly({padded})==selected,"export accepts a valid fact at128KiB byte boundary");padded.push_back(' ');
    expect(selectAnomaly({padded}).is_null(),"export ignores oversize facts without treating them as anomalies");
    for(const std::uint64_t duration:std::vector<std::uint64_t>{120000000ULL,153208591ULL,352000000ULL,599999999ULL}) {
        auto historical=anomaly(91);historical["details"]["values"]["DurationInUs"]=duration;
        historical["details"]["values"]["ActiveResidencyInUs"]=duration;
        unsigned contextCalls=0,queryCalls=0;
        const auto below=collect({historical.dump()},[&](){++contextCalls;return true;},
            [&](std::uint32_t){++queryCalls;return report();});
        expect(selectAnomaly({historical.dump()})==historical,"120s through599999999us remains qualifying historical software-zero evidence");
        expect(below["status"]=="AVAILABLE"&&below["reportAvailable"]==true&&
            below["queryAttempts"]==1&&contextCalls==2&&queryCalls==1&&below["targetPowerState"]=="UNKNOWN",
            "short session still collects transitions without claiming target power state");
        expect(below["selectedEvidence"]==historical&&below["selectedEvidenceIdentity"]["writerSequence"]==91&&
            below["selectedEvidenceIdentity"]["originalEventFileTime"]==historical["details"]["fileTime"]&&
            below["automaticRepairAuthorized"]==false&&below["powerActionAuthorized"]==false&&
            below["sleepSuccessWitness"]==false&&below["selectedSessionInReportVerified"]==false,
            "short-session receipt keeps exact original evidence and identity without coverage or power claims");
    }
    const auto querySelected=queryAnomaly();const std::vector<std::string> queryFacts={querySelected.dump()};
    unsigned queries=0,currentChecks=0;std::uint32_t requestedTimeout=0;
    const auto current=[&](){++currentChecks;return true;};
    const auto query=[&](std::uint32_t timeout){++queries;requestedTimeout=timeout;return report();};
    auto receipt=collect({},current,query);
    expect(receipt["status"]=="AVAILABLE"&&queries==1&&currentChecks==2&&receipt["reportAvailable"]==true&&
        receipt["selectedEvidence"].is_null()&&receipt["selectedEvidenceIdentity"].is_null()&&
        receipt["targetPowerState"]=="UNKNOWN"&&receipt["dripsRole"]=="auxiliary-only",
        "DRIPS absence still collects Windows transitions without fabricating a sleep verdict");
    queries=0;currentChecks=0;
    for(const auto& context:std::vector<std::vector<std::string>>{{"{"},{positive.dump()},{optional.dump()},{wrongEvent.dump()}}) {
        const auto independent=collect(context,[](){return true;},query);
        expect(independent["reportAvailable"]==true&&independent["queryAttempts"]==1&&
            independent["targetPowerState"]=="UNKNOWN"&&independent["sleepSuccessWitness"]==false,
            "missing malformed positive or unsupported auxiliary facts never gate Windows transitions");
    }
    queries=0;currentChecks=0;
    receipt=collect(queryFacts,[](){return false;},query);
    expect(receipt["status"]=="UNKNOWN"&&queries==0&&receipt["queryAttempts"]==0,"noncurrent export performs zero queries");
    receipt=collect(queryFacts,current,query);
    expect(receipt["status"]=="AVAILABLE"&&receipt["reportAvailable"]==true&&queries==1&&requestedTimeout==10000&&currentChecks==2,"valid export performs exactly one10000ms query with pre and post context checks");
    expect(receipt["selectedEvidence"]==querySelected&&receipt["selectedEvidenceIdentity"]["writerSequence"]==1&&receipt["selectedEvidenceIdentity"]["originalEventFileTime"]==selected["details"]["fileTime"],"ordinary receipt preserves original evidence and writer identity");
    expect(receipt["automaticRepairAuthorized"]==false&&receipt["powerActionAuthorized"]==false&&receipt["sleepSuccessWitness"]==false&&receipt["selectedSessionInReportVerified"]==false&&receipt["mandatoryEvidence"]==true&&receipt["detailLogsRequired"]==false,"even available XML is only mandatory ordinary diagnostics with no repair or sleep witness");
    queries=0;currentChecks=0;
    receipt=collect(queryFacts,[&](){return ++currentChecks==1;},query);
    expect(receipt["status"]=="UNKNOWN"&&receipt["reportAvailable"]==false&&queries==1&&currentChecks==2,"late manual wake or generation change suppresses a seemingly valid report without retry");
    queries=0;currentChecks=0;
    receipt=collect(queryFacts,current,[&](std::uint32_t)->QueryResult{++queries;throw std::runtime_error("query-fault");});
    expect(receipt["status"]=="UNKNOWN"&&queries==1&&currentChecks==2&&receipt["queryAttempts"]==1,"throwing query remains unknown and still rechecks context exactly once");
    queries=0;receipt=collect(queryFacts,[]()->bool{throw std::runtime_error("current-fault");},query);
    expect(receipt["status"]=="UNKNOWN"&&queries==0&&receipt["reportAvailable"]==false,"throwing pre-query context grants no query or report");
    receipt=collect(queryFacts,[](){return true;},[](std::uint32_t){auto completed=report();completed.elapsedMs=8100;return completed;});
    expect(receipt["reportAvailable"]==true,"successful child exit remains exportable when total elapsed includes setup cleanup or report read overhead");
    std::vector<QueryResult> failures;
    auto q=report();q.ran=false;failures.push_back(q);q=report();q.exitCode=5;failures.push_back(q);
    q=report();q.win32Error=5;failures.push_back(q);q=report();q.exitCode=1460;q.completion="deadline";failures.push_back(q);
    q=report();q.readError="unreadable-report";failures.push_back(q);
    for(const std::string completion:{"deadline","canceled","wait-failed","overflow",""}){q=report();q.completion=completion;failures.push_back(q);}
    for(const auto& failure:failures) {
        queries=0;receipt=collect(queryFacts,[](){return true;},[&](std::uint32_t){++queries;return failure;});
        expect(receipt["status"]=="UNKNOWN"&&receipt["reportAvailable"]==false&&receipt["automaticRepairAuthorized"]==false&&queries==1,"failed timed-out canceled or unreadable query is UNKNOWN with no retry");
    }
    for(const auto& xml:std::vector<std::string>{"<SleepStudy/>"," \r\n<?xml version=\"1.0\"?><x/>",std::string("\xef\xbb\xbf",3)+"<x/>",utf16("<?xml version=\"1.0\"?><x/>",true),utf16("<SleepStudy/>",false)}) {
        receipt=collect(queryFacts,[](){return true;},[&](std::uint32_t){return report(xml);});
        expect(receipt["reportAvailable"]==true&&receipt["reportBytes"]==xml.size(),"export accepts ASCII UTF8-BOM and either UTF16-BOM XML prefixes only");
    }
    for(const auto& xml:std::vector<std::string>{"","not XML","<","<?xmlbad?>",std::string("\xef\xbb\xbf",3),std::string("\xff\xfe<",3),std::string("\0<x/>",6)}) {
        receipt=collect(queryFacts,[](){return true;},[&](std::uint32_t){return report(xml);});
        expect(receipt["status"]=="UNKNOWN"&&receipt["reportAvailable"]==false,"empty nonXML BOM-only or broken UTF16 prefix remains UNKNOWN");
    }
    std::string maxXml="<SleepStudy/>";maxXml.resize(maxReportBytes,' ');
    receipt=collect(queryFacts,[](){return true;},[&](std::uint32_t){return report(maxXml);});
    expect(receipt["reportAvailable"]==true,"report exact8MiB boundary remains eligible for export");maxXml.push_back(' ');
    receipt=collect(queryFacts,[](){return true;},[&](std::uint32_t){return report(maxXml);});
    expect(receipt["status"]=="UNKNOWN"&&receipt["reportAvailable"]==false,"report above8MiB never becomes available");
    return checks;
}
} // namespace sleep_export_fixture

namespace sleep_export_native_fixture {
using json=nlohmann::json;
static std::filesystem::path fixtureRoot,pretendSymlink;
namespace fspath {
using std::filesystem::path;using std::filesystem::file_type;using std::filesystem::file_status;
using std::filesystem::remove;using std::filesystem::is_directory;using std::filesystem::file_size;
inline file_status symlink_status(const path& p,std::error_code& ec) {
    if(!pretendSymlink.empty()&&p==pretendSymlink){ec.clear();return file_status(file_type::symlink);}
    return std::filesystem::symlink_status(p,ec);
}
} // namespace fspath
static std::mutex g_sgFactMx;
static std::deque<std::string> g_sgFacts;
static std::wstring SG_FACT_LOG;
enum class PowerLifecycle {Ready,Suspended};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<bool> g_exitRequested{false};
static std::uint64_t fixtureGeneration=1,tick=100;
static std::uint64_t fixtureExportFileTime=0;
static unsigned guidSerial=0,queryCalls=0,cleanupFailures=0;
static DWORD simulatedQueryMs=0;static bool exitDuringQuery=false;
static std::uint32_t lastTimeout=0;
static bool writeReport=true,changeGeneration=false;
static std::string commandOutput;static bool forceCommandResult=false;
static DWORD commandExit=0,commandError=0;static const char* commandCompletion="child-exit";
static std::wstring lastCommand;
static std::filesystem::path lastReport;
static std::uint64_t currentPowerGeneration(){return fixtureGeneration;}
[[maybe_unused]] static std::uint64_t sgNowFileTime(){return fixtureExportFileTime;}
static std::string W2U(const std::wstring& s){std::string out;for(const auto c:s)out.push_back(static_cast<char>(c));return out;}
static void appendNativeLifecycleLog(const char*,const json&){++cleanupFailures;}
static std::wstring quote_windows_arg(const std::wstring& s){return L"\""+s+L"\"";}
static ULONGLONG exportTick(){return tick;}
static HRESULT exportGuid(GUID* value){*value=GUID{};value->Data1=++guidSerial;return S_OK;}
static int exportGuidString(REFGUID value,LPOLESTR out,int capacity) {
    const auto text=L"{fixture-"+std::to_wstring(value.Data1)+L"}";
    if(static_cast<std::size_t>(capacity)<=text.size())return 0;
    std::wmemcpy(out,text.c_str(),text.size()+1);return static_cast<int>(text.size()+1);
}
static UINT exportSystem(LPWSTR out,UINT capacity) {
    constexpr wchar_t system[]=L"C:\\Windows\\System32";
    if(capacity<sizeof(system)/sizeof(wchar_t))return 0;
    std::wmemcpy(out,system,sizeof(system)/sizeof(wchar_t));return static_cast<UINT>(std::size(system)-1);
}
static ymcc::sleepexport::CaptureFinalization fixtureFinalization;
static const char* simulatedDeadlineFile=nullptr;
struct RunOut {bool ran=true;DWORD exitCode=0,win32Error=0;const char* completion="child-exit";std::string out;ymcc::sleepexport::CaptureFinalization captureFinalization=fixtureFinalization;};
static RunOut runCapture(const std::wstring& command,DWORD timeout,const std::function<bool()>& canceled,
                         HANDLE,HANDLE,const wchar_t*,ymcc::sleepexport::CaptureLoopTiming* timing) {
    ++queryCalls;lastCommand=command;lastTimeout=timeout;tick+=9;
    if(timing){*timing={true,4,5,{},{},{}};timing->pipe.emptyPeekObserved=true;}
    if(forceCommandResult){
        if(timing&&commandExit==ERROR_TIMEOUT&&std::string_view(commandCompletion)=="deadline") {
            timing->deadlineJob={true,true,0,2,1,0,101,202};
            timing->deadlineRoot={true,true,0,0,0,73};
        }
        return {true,commandExit,commandError,commandCompletion,commandOutput};
    }
    const auto marker=command.find(L" /output \"");
    if(marker==std::wstring::npos||command.back()!=L'"')throw std::runtime_error("export command missing quoted output");
    lastReport=command.substr(marker+10,command.size()-(marker+10)-1);
    if(simulatedQueryMs){
        tick+=(std::min)(simulatedQueryMs,timeout);
        if(timing)timing->loopElapsedMs+=(std::min)(simulatedQueryMs,timeout);
        if(changeGeneration)++fixtureGeneration;
        if(exitDuringQuery)g_exitRequested=true;
        if(canceled())return {true,ERROR_CANCELLED,0,"canceled",commandOutput};
        if(simulatedQueryMs>=timeout){
            if(timing){timing->deadlineJob={true,false,ERROR_ACCESS_DENIED,0,0,0,0,0};timing->deadlineRoot={true,false,ERROR_ACCESS_DENIED,0,0,0};}
            if(simulatedDeadlineFile) {
                std::ofstream report(lastReport,std::ios::binary);report<<simulatedDeadlineFile;report.close();
                if(!report)throw std::runtime_error("deadline fixture report write failed");
            }
            return {true,ERROR_TIMEOUT,0,"deadline",commandOutput};
        }
    }
    if(writeReport){std::ofstream stream(lastReport,std::ios::binary);stream<<"<?xml version=\"1.0\"?><SleepStudy/>";}
    if(changeGeneration&&!simulatedQueryMs){++fixtureGeneration;if(!canceled())throw std::runtime_error("native cancellation lost generation check");}
    return {true,0,0,"child-exit",commandOutput};
}
#define GetTickCount64 exportTick
#define CoCreateGuid exportGuid
#define StringFromGUID2 exportGuidString
#define GetSystemDirectoryW exportSystem
#include "sleep_export_diagnostics_native.inc"
#undef GetSystemDirectoryW
#undef StringFromGUID2
#undef CoCreateGuid
#undef GetTickCount64
static unsigned queryOutputTests() {
    using namespace ymcc::sleepexport;
    json checks=json::array();
    const auto expect=[&](bool ok,const char* name){checks.push_back({{"name",name},{"pass",ok}});};
    const auto finish=[&](){
        unsigned failures=0;for(const auto& row:checks)failures+=!row.at("pass").get<bool>();
        std::cout<<"QUERY_OUTPUT_RESULT "<<json{{"checks",checks},{"failures",failures},{"actualPowercfgCalls",0}}.dump()<<'\n';
        if(failures)throw std::runtime_error("query output witness failed");
        return static_cast<unsigned>(checks.size());
    };
    fixtureRoot=std::filesystem::current_path()/L"query-output-owned";
    if(!std::filesystem::create_directory(fixtureRoot))throw std::runtime_error("query output directory must be fresh");
    struct Cleanup {~Cleanup(){std::error_code ec;std::filesystem::remove(fixtureRoot,ec);}} cleanup;
    SG_FACT_LOG=(fixtureRoot/L"absent-facts.log").wstring();
    const auto reset=[&](){++fixtureGeneration;g_sgFacts.clear();g_exitRequested=false;g_powerLifecycle=PowerLifecycle::Ready;queryCalls=0;cleanupFailures=0;lastTimeout=0;lastCommand.clear();lastReport.clear();pretendSymlink.clear();writeReport=true;changeGeneration=false;simulatedQueryMs=0;exitDuringQuery=false;commandOutput.clear();forceCommandResult=false;commandExit=0;commandError=0;commandCompletion="child-exit";};
    const auto checkQuery=[&](const json& receipt,const std::string& expected,const char* name){
        bool exact=receipt.contains("commandFailureOutputBytes")&&receipt["commandFailureOutputBytes"].is_object();
        if(exact){const auto& b=receipt["commandFailureOutputBytes"];std::string hex;static constexpr char digits[]="0123456789abcdef";
            for(std::size_t i=0;i<(std::min)(expected.size(),std::size_t{2048});++i){const auto c=static_cast<unsigned char>(expected[i]);hex+=digits[c>>4];hex+=digits[c&15];}
            exact=b.at("bufferObserved")==true&&b.at("bufferBytes")==expected.size()&&b.at("retainedBytes")==((std::min)(expected.size(),std::size_t{2048}))&&b.at("data")==hex&&b.at("truncated")==(expected.size()>2048)&&b.at("codePageKnown")==false;
        }expect(exact,name);
    };
    reset();forceCommandResult=true;commandExit=5;commandOutput=std::string("\xff\xfe\0\x81",4);
    {
        auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring(),"binary-failure");const auto& receipt=result.receipt;const auto expected=commandOutput;
        expect(receipt["reportAvailable"]==false&&receipt["query"]["exitCode"]==5&&receipt["queryAttempts"]==1&&queryCalls==1&&lastTimeout==10000,"original failed query result and single allowance are preserved");
        checkQuery(receipt,expected,"failure output preserves original returned bytes");
#ifdef YMCC_SLEEP_EXPORT_OUTPUT_OLD_ONLY
        return finish();
#endif
        expect(receipt["commandOutputBytes"]==4&&receipt["commandFailureOutputEvidenceBuildFailures"]==0&&result.files.size()==1&&result.owned.size()==2,"failure byte count and one original receipt file remain independent of XML availability");
        std::ifstream in(std::filesystem::path(result.files.front()),std::ios::binary);json disk;in>>disk;
        expect(disk==receipt,"ordinary owned receipt file contains the exact bounded byte evidence");
    }
    expect(std::filesystem::is_empty(fixtureRoot)&&cleanupFailures==0,"binary failure returns all original owned paths");
    for(std::size_t count:{std::size_t{0},std::size_t{2048},std::size_t{2049}}){
        reset();forceCommandResult=true;commandExit=50;commandOutput.assign(count,'x');
        auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring(),"boundary-failure");
        checkQuery(result.receipt,commandOutput,count==0?"observed empty output is zero bytes with empty hex":count==2048?"exact 2048 byte prefix stays untruncated":"2049 byte buffer retains only 2048 bytes and marks truncation");
    }
    reset();forceCommandResult=true;commandExit=87;
    for(unsigned c=0;c<256;++c)commandOutput.push_back(static_cast<char>(c));
    {auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring(),"all-byte-values");checkQuery(result.receipt,commandOutput,"all 256 byte values including NUL survive without codepage conversion");}
    reset();commandOutput="successful-output-not-retained";
    {auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring(),"successful");const auto& b=result.receipt["commandFailureOutputBytes"];
        expect(result.receipt["reportAvailable"]==true&&result.files.size()==2&&b["bufferObserved"]==false&&b["data"].is_null()&&b["bufferBytes"].is_null()&&result.receipt["commandOutputBytes"]==commandOutput.size(),"success keeps original report while no failure output bytes are retained");}
    reset();simulatedQueryMs=10001;commandOutput="timeout-prefix";
    {auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring(),"deadline-with-output");checkQuery(result.receipt,commandOutput,"deadline preserves available returned failure prefix");
        expect(result.receipt["query"]["elapsedMs"]==10009&&result.receipt["query"]["completion"]=="deadline"&&result.receipt["query"]["exitCode"]==1460&&queryCalls==1,"output observation does not change original deadline result or query count");}
    const auto completed=tick;
    {auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring(),"cooldown");const auto& b=result.receipt["commandFailureOutputBytes"];
        expect(queryCalls==1&&tick==completed&&result.receipt["queryAttempts"]==0&&result.receipt["recentDeadline"]["originalExportId"]=="deadline-with-output"&&result.files.empty()&&b["bufferObserved"]==false&&b["data"].is_null()&&!result.receipt.contains("query"),"cooldown neither queries nor borrows the previous failure output");}
    reset();g_powerLifecycle=PowerLifecycle::Suspended;
    {auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());expect(queryCalls==0&&result.owned.empty()&&result.receipt["queryAttempts"]==0&&result.receipt["commandFailureOutputBytes"]["bufferObserved"]==false,"nonready context has no observed failure output or new files");}
    reset();simulatedQueryMs=10001;exitDuringQuery=true;commandOutput="canceled-old-context";
    {auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());expect(result.receipt["reason"]=="export-context-changed"&&!result.receipt.contains("query")&&result.receipt["commandFailureOutputBytes"]["bufferObserved"]==false,"rejected post-query context does not advertise current failure bytes");}
    reset();
    {auto result=sgExportWindowsSleepEvidence((fixtureRoot/L"missing-directory").wstring());expect(queryCalls==0&&result.owned.empty()&&result.receipt["commandFailureOutputBytes"]["bufferObserved"]==false,"setup refusal before runCapture remains unobserved rather than empty");}
    QueryFailureOutputBytes leaf;leaf.observe(std::string(2049,'q'));const auto good=queryFailureOutputBytesEvidence(leaf);unsigned allocationFailures=0;bool optional=true;
    for(int at=0;at<128;++at){ymcc::testing::allocationFault::failAfter(at);auto value=queryFailureOutputBytesEvidence(leaf);ymcc::testing::allocationFault::failAfter(-1);if(value.is_null())++allocationFailures;else optional=optional&&value==good;}
    expect(allocationFailures>0&&optional&&leaf.observed&&leaf.bufferBytes==2049&&leaf.retainedBytes==2048,"optional JSON allocation failures return null and preserve the bounded stack observation");
    leaf.observe("");const auto empty=queryFailureOutputBytesEvidence(leaf);expect(empty["data"]==""&&empty["bufferBytes"]==0&&empty["retainedBytes"]==0&&empty["truncated"]==false,"reused local observation captures empty output without stale bytes");
    const auto guide=ymcc::logexport::aiReadingGuide();expect(guide.contains("windowsQueryFailureOutput"),"actual AI reading guide advertises bounded current failure evidence");
    expect(std::filesystem::is_empty(fixtureRoot)&&cleanupFailures==0,"all original receipt and report scopes return their owned paths");
    reset();return finish();
}

static unsigned captureFinalizationAdapterTests() {
    using namespace ymcc::sleepexport;unsigned checks=0;
    const auto expect=[&](bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);};
    fixtureRoot=std::filesystem::current_path()/L"capture-finalization-adapter-owned";
    if(!std::filesystem::create_directory(fixtureRoot))throw std::runtime_error("adapter finalization directory must be fresh");
    struct Cleanup{~Cleanup(){std::error_code ec;std::filesystem::remove(fixtureRoot,ec);}} cleanup;
    SG_FACT_LOG=(fixtureRoot/L"absent-facts.log").wstring();
    const auto reset=[&](){++fixtureGeneration;g_sgFacts.clear();g_exitRequested=false;g_powerLifecycle=PowerLifecycle::Ready;queryCalls=0;cleanupFailures=0;writeReport=true;changeGeneration=false;simulatedQueryMs=0;exitDuringQuery=false;commandOutput.clear();forceCommandResult=false;commandExit=0;commandError=0;commandCompletion="child-exit";fixtureFinalization={};simulatedDeadlineFile=nullptr;};
    reset();fixtureFinalization={true,true,0,0,0};
    {auto r=sgExportWindowsSleepEvidence(fixtureRoot.wstring(),"normal-finalization");expect(r.receipt["reportAvailable"]==true&&r.receipt["queryAttempts"]==1&&queryCalls==1&&r.receipt["commandCaptureFinalization"]==captureFinalizationEvidence(fixtureFinalization),"actual adapter publishes returned success finalization independently of report");
        expect(r.receipt.at("queryPhaseElapsedMs")==json{{"preparation",0},{"captureCall",9},{"setupThroughResume",4},{"captureLoop",5},{"reportFileRead",0}},"completed report file check preserves observed zero separately from no read");
        expect(!r.receipt.contains("commandCapturePipe")&&!r.receipt.contains("deadlineReportPath")&&!r.receipt.contains("deadlineJobAccounting")&&!r.receipt.contains("deadlineRootAccounting"),"successful query does not publish failure-only evidence");
        std::ifstream in(std::filesystem::path(r.files.back()),std::ios::binary);json saved;in>>saved;expect(saved==r.receipt,"original owned JSON receipt preserves exact finalization values");}
    reset();forceCommandResult=true;commandExit=1460;commandCompletion="deadline";fixtureFinalization={true,false,5,258,0};
    {auto r=sgExportWindowsSleepEvidence(fixtureRoot.wstring(),"failed-finalization");expect(!r.receipt["reportAvailable"].get<bool>()&&r.receipt["query"]["exitCode"]==1460&&r.receipt["commandCaptureFinalization"]["jobTerminationError"]==5&&r.receipt["commandCaptureFinalization"]["rootWaitResult"]==258,"actual adapter keeps wrapper deadline termination error and root wait separate");
        expect(r.receipt.at("commandCapturePipe")==json{{"schema",1},{"scope","current-failed-query-capture-loop-before-finalization"},
            {"emptyPeekObserved",true},{"nonemptyPeekObserved",false},{"emptyReadObserved",false},
            {"bytesReadObserved",false},{"firstPeekError",nullptr},{"firstReadError",nullptr}},"failed query preserves observed empty pipe without a read or pipe error");
        expect(r.receipt.at("deadlineJobAccounting")==json{{"schema",1},{"scope","owned-command-job-before-deadline-termination"},
            {"available",true},{"elapsedMs",0},{"win32Error",nullptr},{"totalProcesses",2},{"activeProcesses",1},
            {"userTime100ns",101},{"kernelTime100ns",202}},"deadline receipt preserves exact accounting of its own job");
        expect(r.receipt.at("deadlineRootAccounting")==json{{"schema",1},{"scope","owned-command-root-before-deadline-termination"},
            {"available",true},{"elapsedMs",0},{"win32Error",nullptr},{"userTime100ns",0},{"kernelTime100ns",73}},"root timing is independent of job timing");
        const auto& path=r.receipt.at("deadlineReportPath");
        expect(path["state"]=="path-not-found"&&path["bytes"].is_null()&&path["win32Error"]==ERROR_FILE_NOT_FOUND,
            "deadline with no output retains the actual missing-path result");}
    {auto r=sgExportWindowsSleepEvidence(fixtureRoot.wstring(),"cooldown");const auto& v=r.receipt["commandCaptureFinalization"];expect(queryCalls==1&&r.receipt["queryAttempts"]==0&&!v["entered"].get<bool>()&&v["jobTerminationAccepted"].is_null()&&v["rootWaitResult"].is_null(),"original cooldown does not borrow the previous finalization");
        expect(!r.receipt.contains("queryPhaseElapsedMs")&&!r.receipt.contains("commandCapturePipe")&&!r.receipt.contains("deadlineReportPath")&&!r.receipt.contains("deadlineJobAccounting")&&!r.receipt.contains("deadlineRootAccounting"),"cooldown has no current command timing or output observation");}
    for(const char* residue:{"","<?xml version=\"1.0\"?><SleepStudy/>"}) {
        reset();simulatedQueryMs=timeoutMs;simulatedDeadlineFile=residue;
        auto r=sgExportWindowsSleepEvidence(fixtureRoot.wstring(),"deadline-residue");
        const auto& root=r.receipt.at("deadlineRootAccounting");
        expect(root["available"]==false&&root["win32Error"]==ERROR_ACCESS_DENIED&&root["userTime100ns"].is_null()&&root["kernelTime100ns"].is_null(),"root refusal preserves original error and forbids fabricated zeroes");
        const auto& job=r.receipt.at("deadlineJobAccounting");
        expect(job["available"]==false&&job["win32Error"]==ERROR_ACCESS_DENIED&&job["activeProcesses"].is_null()&&job["userTime100ns"].is_null(),"query refusal keeps raw error and no counter values");
        const auto& path=r.receipt.at("deadlineReportPath");
        expect(path["state"]=="regular-file-metadata"&&path["bytes"]==std::strlen(residue)&&
            path["win32Error"].is_null()&&path["elapsedMs"]==0&&r.receipt["reportAvailable"]==false&&
            !r.receipt.contains("reportFile")&&r.files.size()==1&&r.owned.size()==2&&queryCalls==1,
            "actual deadline output metadata distinguishes zero and nonzero bytes without exporting the report");
        std::ifstream in(std::filesystem::path(r.files.back()),std::ios::binary);json saved;in>>saved;
        expect(saved==r.receipt,"original diagnostic sidecar retains the current metadata without XML");
    }
    reset();g_powerLifecycle=PowerLifecycle::Suspended;fixtureFinalization={true,true,0,0,0};
    {auto r=sgExportWindowsSleepEvidence(fixtureRoot.wstring());expect(queryCalls==0&&r.owned.empty()&&r.receipt["commandCaptureFinalization"]["entered"]==false,"unentered query has no current finalization even with fixture prior values");}
    reset();simulatedQueryMs=9000;changeGeneration=true;fixtureFinalization={true,true,0,0,0};
    {auto r=sgExportWindowsSleepEvidence(fixtureRoot.wstring());expect(r.receipt["reason"]=="export-context-changed"&&!r.receipt.contains("query")&&r.receipt["commandCaptureFinalization"]["entered"]==false&&!r.receipt.contains("commandCapturePipe")&&!r.receipt.contains("deadlineReportPath"),"post-query context rejection suppresses current capture and output observations");}
    reset();expect(std::filesystem::is_empty(fixtureRoot)&&cleanupFailures==0,"all finalization adapter original owned report and receipt paths return");
    return checks;
}

static unsigned firstExportBudgetTests() {
    unsigned checks=0;const auto expect=[&](bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);};
    fixtureRoot=std::filesystem::current_path()/L"first-export-budget-owned";
    if(!std::filesystem::create_directory(fixtureRoot))throw std::runtime_error("budget fixture directory must be fresh");
    SG_FACT_LOG=(fixtureRoot/L"absent-facts.log").wstring();++fixtureGeneration;g_exitRequested=false;g_powerLifecycle=PowerLifecycle::Ready;
    g_sgFacts.clear();queryCalls=0;writeReport=true;changeGeneration=false;exitDuringQuery=false;simulatedQueryMs=9000;
    {auto fast=sgExportWindowsSleepEvidence(fixtureRoot.wstring(),"nine-second");
        expect(lastTimeout==10000&&fast.receipt["timeoutMs"]==10000&&fast.receipt["reportAvailable"]==true&&queryCalls==1,"first export forwards10000ms and retains a nine-second report");}
    simulatedQueryMs=10001;
    {auto first=sgExportWindowsSleepEvidence(fixtureRoot.wstring(),"short-deadline");
        expect(queryCalls==2&&first.receipt["query"]["elapsedMs"]==10009&&first.receipt["reportAvailable"]==false&&first.receipt["status"]=="UNKNOWN","first10000ms deadline retains UNKNOWN and does not retry or include XML");}
    const auto completed=tick;
    tick=completed+10001;
    {auto repeat=sgExportWindowsSleepEvidence(fixtureRoot.wstring(),"after-ten-seconds");
        expect(queryCalls==2&&repeat.receipt["queryAttempts"]==0&&repeat.files.empty()&&repeat.owned.empty(),"query budget does not shorten the original60000ms cooldown");
        expect(repeat.receipt["recentDeadline"]["retryAfterMs"]==49999&&repeat.receipt["recentDeadline"]["cooldownMs"]==60000&&repeat.receipt["recentDeadline"]["originalExportId"]=="short-deadline","suppressed query preserves the exact prior operation and remaining cooldown");}
    tick=completed+60000;simulatedQueryMs=9000;
    {auto expired=sgExportWindowsSleepEvidence(fixtureRoot.wstring(),"expired");expect(queryCalls==3&&lastTimeout==10000&&expired.receipt["reportAvailable"]==true,"exact60000ms expiry admits one new10000ms query");}
    ++fixtureGeneration;g_powerLifecycle=PowerLifecycle::Suspended;
    {auto off=sgExportWindowsSleepEvidence(fixtureRoot.wstring());expect(queryCalls==3&&off.receipt["queryAttempts"]==0&&off.files.empty(),"nonready generation still performs no query or file creation");}
    g_powerLifecycle=PowerLifecycle::Ready;g_exitRequested=true;
    {auto off=sgExportWindowsSleepEvidence(fixtureRoot.wstring());expect(queryCalls==3&&off.files.empty(),"exit still performs no query or file creation");}
    g_exitRequested=false;simulatedQueryMs=10001;
    const auto policy=[&](const char* id,unsigned streak,unsigned cooldown){
        auto r=sgExportWindowsSleepEvidence(fixtureRoot.wstring(),id);const auto& p=r.receipt["deadlineRetryPolicy"];
        expect(r.receipt["queryAttempts"]==1&&lastTimeout==10000&&p["deadlineStreak"]==streak&&p["cooldownMs"]==cooldown&&
            p["completedTickMs"]==std::to_string(tick)&&p["powerGeneration"]==std::to_string(fixtureGeneration)&&p["streakWindowMs"]==300000,
            "actual deadline retains the original timeout and publishes the observed retry policy");
    };
    policy("streak-one",1,60000);
    tick+=60000;policy("streak-two",2,120000);
    const auto second=tick;const auto afterSecond=queryCalls;
    for(const auto age:{61000u,119999u}){
        tick=second+age;auto r=sgExportWindowsSleepEvidence(fixtureRoot.wstring(),"retry-within-adapted-window");
        expect(queryCalls==afterSecond&&r.receipt["queryAttempts"]==0&&!r.receipt.contains("deadlineRetryPolicy")&&
            r.receipt["recentDeadline"]["originalExportId"]=="streak-two"&&r.receipt["recentDeadline"]["deadlineStreak"]==2&&
            r.receipt["recentDeadline"]["retryAfterMs"]==120000-age&&r.files.empty()&&r.owned.empty(),
            "second deadline suppresses a retry after the old cooldown without sliding the completion anchor");
    }
    tick=second+120000;policy("streak-three",3,240000);
    tick+=240000;policy("streak-capped",3,240000);
    tick+=300001;policy("outside-streak-window",1,60000);
    ++fixtureGeneration;policy("new-generation-starts-at-one",1,60000);
    tick+=60000;
    {auto early=sgExportWindowsSleepEvidence((fixtureRoot/L"missing").wstring(),"setup-failed");
        expect(early.receipt["query"]["ran"]==false&&!early.receipt.contains("deadlineRetryPolicy"),"a setup failure breaks the deadline streak");}
    policy("after-setup-failure",1,60000);
    tick+=60000;simulatedQueryMs=9000;
    {auto recovered=sgExportWindowsSleepEvidence(fixtureRoot.wstring(),"recovered");
        expect(recovered.receipt["reportAvailable"]==true&&!recovered.receipt.contains("deadlineRetryPolicy"),"success clears deadline state and still exports the report");}
    simulatedQueryMs=10001;policy("after-recovery",1,60000);
    expect(std::filesystem::is_empty(fixtureRoot)&&cleanupFailures==0,"all report and receipt files return after their original scopes");
    std::filesystem::remove(fixtureRoot);return checks;
}

static unsigned adapterTests() {
    unsigned checks=0;const auto expect=[&](bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);};
    static_assert(!std::is_copy_constructible_v<SgWindowsSleepExport>);
    static_assert(std::is_move_constructible_v<SgWindowsSleepExport>);
    fixtureRoot=std::filesystem::current_path()/L"sleep-export-owned-fixture";
    if(!std::filesystem::create_directory(fixtureRoot))throw std::runtime_error("export fixture requires a fresh owned directory");
    // Files use scoped exact-path cleanup; retain nonempty failure evidence.
    struct Cleanup {~Cleanup(){std::error_code ec;std::filesystem::remove(fixtureRoot,ec);}} cleanup;
    SG_FACT_LOG=(fixtureRoot/L"ordinary-sleep-facts.log").wstring();
    const auto reset=[&](){++fixtureGeneration;g_sgFacts.clear();g_exitRequested=false;g_powerLifecycle=PowerLifecycle::Ready;queryCalls=0;lastTimeout=0;lastCommand.clear();lastReport.clear();pretendSymlink.clear();writeReport=true;changeGeneration=false;simulatedQueryMs=0;exitDuringQuery=false;};
    reset();
    {auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());expect(result.receipt["status"]=="AVAILABLE"&&queryCalls==1&&result.files.size()==2&&result.owned.size()==2&&
        result.receipt["selectedEvidence"].is_null()&&result.receipt["targetPowerState"]=="UNKNOWN",
        "actual adapter collects Windows transitions when DRIPS never appeared");}
    for(const std::uint64_t duration:std::vector<std::uint64_t>{120000000ULL,153208591ULL,352000000ULL,599999999ULL}) {
        reset();auto historical=sleep_export_fixture::anomaly(92);
        historical["details"]["values"]["DurationInUs"]=duration;
        historical["details"]["values"]["ActiveResidencyInUs"]=duration;
        g_sgFacts.push_back(historical.dump());const auto serialBefore=guidSerial;
        auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());
        expect(result.receipt["status"]=="AVAILABLE"&&result.receipt["queryAttempts"]==1&&
            result.receipt["reportAvailable"]==true&&queryCalls==1&&result.files.size()==2&&
            result.owned.size()==2&&guidSerial==serialBefore+1&&result.receipt["targetPowerState"]=="UNKNOWN",
            "actual adapter collects one transition report for short sessions without a SleepStudy minimum gate");
        expect(result.receipt["selectedEvidence"]==historical&&result.receipt["selectedEvidenceIdentity"]["writerSequence"]==92&&
            result.receipt["automaticRepairAuthorized"]==false&&result.receipt["powerActionAuthorized"]==false&&
            result.receipt["selectedSessionInReportVerified"]==false,
            "actual short-session export preserves original evidence identity without claiming target-session coverage");
    }
    reset();g_sgFacts.push_back(sleep_export_fixture::queryAnomaly().dump());std::vector<std::filesystem::path> owned;
    {
        auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());owned=result.owned;
        expect(result.receipt["reportAvailable"]==true&&queryCalls==1&&lastTimeout==10000&&result.files.size()==2&&owned.size()==2,("actual adapter runs one inert10000ms System Power Report query and adds XML plus ordinary receipt: "+result.receipt.dump()+"; outputPathChars="+std::to_string(lastReport.wstring().size())).c_str());
        expect(lastCommand.find(L"\"C:\\Windows\\System32\\powercfg.exe\" /systempowerreport /xml /duration 1 /output \"")==0&&lastReport.parent_path()==fixtureRoot,"actual command uses quoted absolute System32 and unique direct owned output only");
        expect(result.receipt["selectedEvidence"]["details"]["values"]["DurationInUs"]==600000000u&&
            result.receipt["diagnostic"]=="WindowsSystemPowerReport"&&result.receipt["selectedEvidence"]==sleep_export_fixture::queryAnomaly()&&
            result.receipt["selectedSessionInReportVerified"]==false&&result.receipt["automaticRepairAuthorized"]==false,
            "explicit export enables one System Power Report command while target-session coverage stays unverified");
        expect(std::all_of(owned.begin(),owned.end(),[](const auto& p){return std::filesystem::is_regular_file(p);}),"native report files remain available through archive ownership scope");
        auto moved=std::move(result);expect(result.owned.empty()&&moved.owned==owned,"moving archive scope transfers cleanup ownership exactly once");
    }
    expect(std::none_of(owned.begin(),owned.end(),[](const auto& p){return std::filesystem::exists(p);})&&cleanupFailures==0,"actual scoped destructor removes only unique owned files after archive scope");
    for(const bool symlink:{false,true}) {
        reset();g_sgFacts.push_back(sleep_export_fixture::queryAnomaly().dump());
        const auto existing=fixtureRoot/(L"windows-systempowerreport-{fixture-"+std::to_wstring(guidSerial+1)+L"}.xml");
        {std::ofstream stream(existing);stream<<"untouched sentinel";}
        if(symlink)pretendSymlink=existing; // Inert reparse status, no link privileges or external target.
        {auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());expect(result.receipt["status"]=="UNKNOWN"&&queryCalls==0&&result.owned.empty()&&result.files.empty(),"actual native adapter never owns overwrites or deletes an existing or reparse target");}
        std::ifstream stream(existing);std::string value;std::getline(stream,value);stream.close();
        expect(value=="untouched sentinel","rejected preexisting target survives returned-scope destruction unchanged");
        pretendSymlink.clear();std::filesystem::remove(existing);
    }
    reset();g_sgFacts.push_back(sleep_export_fixture::queryAnomaly().dump());g_powerLifecycle=PowerLifecycle::Suspended;
    {auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());expect(result.receipt["status"]=="UNKNOWN"&&queryCalls==0&&result.files.empty()&&result.owned.empty(),"nonready native generation performs zero query and creates no files");}
    {const auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());
        expect(result.receipt.value("queryContextGate",json::object())==json{{"schema",1},{"blocker","power-not-ready"}}&&queryCalls==0,
               "nonready export reports its context blocker without querying Windows");}
    reset();g_sgFacts.push_back(sleep_export_fixture::queryAnomaly().dump());writeReport=false;
    {auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());expect(result.receipt["status"]=="UNKNOWN"&&result.receipt["reportAvailable"]==false&&queryCalls==1&&result.files.size()==1&&std::filesystem::path(result.files[0]).extension()==L".json","native missing-report read failure exports only ordinary UNKNOWN receipt without retry");}
    reset();g_sgFacts.push_back(sleep_export_fixture::queryAnomaly().dump());changeGeneration=true;
    {auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());expect(result.receipt["status"]=="UNKNOWN"&&result.receipt["reportAvailable"]==false&&result.receipt["automaticRepairAuthorized"]==false&&queryCalls==1&&result.files.size()==1,"manual wake or native generation change suppresses XML and never authorizes power action");}
    for(const std::string mode:{"nine-second-success","deadline","generation","exit"}) {
        reset();simulatedQueryMs=mode=="deadline"?10001:9000;
        changeGeneration=mode=="generation";exitDuringQuery=mode=="exit";
        const auto start=tick;std::vector<std::filesystem::path> own;
        {
            auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());own=result.owned;
            expect(queryCalls==1&&lastTimeout==10000&&own.size()==2&&result.receipt["queryAttempts"]==1,
                   "late diagnostic query uses one bounded attempt with original file ownership");
            expect(result.receipt["targetPowerState"]=="UNKNOWN"&&result.receipt["automaticRepairAuthorized"]==false,
                   "late report never grants physical success or power authority");
            if(mode=="nine-second-success")
                expect(result.receipt["reportAvailable"]==true&&result.files.size()==2&&tick-start==9009,
                       "actual adapter preserves a nine-second report after the old cutoff");
            else if(mode=="deadline")
                expect(result.receipt["status"]=="UNKNOWN"&&!result.receipt["reportAvailable"].get<bool>()&&
                    result.receipt["query"]["completion"]=="deadline"&&result.receipt["query"]["exitCode"]==1460&&
                    tick-start==10009&&result.files.size()==1,
                       "ten-second diagnostic deadline stays UNKNOWN with no retry or XML");
            else expect(result.receipt["reason"]=="export-context-changed"&&!result.receipt["reportAvailable"].get<bool>()&&
                        result.files.size()==1&&tick-start==9009,
                       "generation or exit during the extended query cancels and rejects the report");
            if(mode=="generation"||mode=="exit")
                expect(result.receipt.value("queryContextGate",json::object())==json{{"schema",1},{"blocker",mode=="exit"?"native-exit-requested":"power-generation-changed"}},
                       "rejected report distinguishes generation change from native exit");
            else expect(!result.receipt.contains("queryContextGate"),"success and deadline do not attribute a context cancellation");
        }
        expect(std::none_of(own.begin(),own.end(),[](const auto& p){return std::filesystem::exists(p);}),
               "late success timeout and cancellation return only their original owned files");
    }
    reset();simulatedQueryMs=10001;
    {auto first=sgExportWindowsSleepEvidence(fixtureRoot.wstring(),"first-timeout");
        expect(first.receipt["queryAttempts"]==1&&queryCalls==1&&first.receipt["query"]["elapsedMs"]==10009,
            "first report uses the ten-second diagnostic allowance");}
    const auto completed=tick;const auto serialAfterDeadline=guidSerial;
    for(const std::uint64_t age:{0ULL,1ULL,59999ULL}) {
        tick=completed+age;
        auto repeat=sgExportWindowsSleepEvidence(fixtureRoot.wstring(),"repeat");
        expect(queryCalls==1&&guidSerial==serialAfterDeadline&&repeat.files.empty()&&repeat.owned.empty()&&tick==completed+age,
            "same-generation repeated export launches no report child or temporary file during cooldown");
        expect(repeat.receipt["queryAttempts"]==0&&repeat.receipt["reason"]=="recent-query-deadline"&&
            repeat.receipt["reportAvailable"]==false&&repeat.receipt["status"]=="UNKNOWN"&&
            repeat.receipt["recentDeadline"]["originalExportId"]=="first-timeout"&&
            repeat.receipt["recentDeadline"]["query"]["elapsedMs"]==10009&&
            repeat.receipt["recentDeadline"]["ageMs"]==age&&repeat.receipt["recentDeadline"]["retryAfterMs"]==60000-age&&
            !repeat.receipt.contains("query")&&repeat.receipt["automaticRepairAuthorized"]==false,
            "repeat receipt preserves the previous timeout identity and never fabricates a new query or sleep verdict");
    }
    tick=completed+60000;simulatedQueryMs=9000;
    {auto expired=sgExportWindowsSleepEvidence(fixtureRoot.wstring(),"after-expiry");
        expect(queryCalls==2&&lastTimeout==10000&&expired.receipt["reportAvailable"]==true&&
            expired.receipt["queryAttempts"]==1&&!expired.receipt.contains("recentDeadline"),
            "exact cooldown expiry permits the original full query and nine-second successful report");}
    {auto afterSuccess=sgExportWindowsSleepEvidence(fixtureRoot.wstring());
        expect(queryCalls==3&&afterSuccess.receipt["queryAttempts"]==1&&afterSuccess.receipt["reportAvailable"]==true,
            "successful reports are never reused as current evidence");}
    reset();simulatedQueryMs=10001;
    {auto timedOut=sgExportWindowsSleepEvidence(fixtureRoot.wstring());expect(timedOut.receipt["queryAttempts"]==1,"generation control starts with a fresh deadline");}
    ++fixtureGeneration;simulatedQueryMs=9000;
    {auto changed=sgExportWindowsSleepEvidence(fixtureRoot.wstring());
        expect(queryCalls==2&&changed.receipt["reportAvailable"]==true,"new power generation cannot reuse the previous generation timeout");}
    reset();simulatedQueryMs=10001;
    {auto timedOut=sgExportWindowsSleepEvidence(fixtureRoot.wstring());expect(timedOut.receipt["queryAttempts"]==1,"clock control starts with a fresh deadline");}
    --tick;simulatedQueryMs=9000;
    {auto rollback=sgExportWindowsSleepEvidence(fixtureRoot.wstring());
        expect(queryCalls==2&&rollback.receipt["reportAvailable"]==true,"clock rollback cannot extend the deadline reuse window");}
    reset();
    {std::ofstream stream{std::filesystem::path(SG_FACT_LOG)};stream<<sleep_export_fixture::queryAnomaly(8).dump()<<'\n'<<"{partial";}
    {auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());expect(result.receipt["reportAvailable"]==true&&queryCalls==1&&result.receipt["selectedEvidenceIdentity"]["writerSequence"]==8,"actual adapter finds completed historical anomaly in bounded ordinary log and ignores partial tail");}
    reset();const auto off=sleep_export_fixture::longScreenOff();
    {std::ofstream stream{std::filesystem::path(SG_FACT_LOG)};stream<<off.dump()<<'\n'<<"{partial";}
    {auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());
        expect(result.receipt["reportAvailable"]==true&&queryCalls==1&&result.receipt["selectedEvidence"]==off&&
            result.receipt["selectedEvidenceIdentity"]["originalEventFileTime100ns"]==off["details"]["exitFileTime100ns"]&&
            result.receipt["selectedEvidenceIdentity"]["writerSequence"]==9&&
            result.receipt["targetPowerState"]=="UNKNOWN"&&result.receipt["selectedSessionInReportVerified"]==false&&
            result.receipt["automaticRepairAuthorized"]==false&&result.receipt["powerActionAuthorized"]==false,
            "actual export includes the original long screen-off evidence from disk without claiming report coverage or power authority");}
    std::filesystem::remove(std::filesystem::path(SG_FACT_LOG));
    for(const bool continuing:{true,false}){
        reset();const auto reasonFive=sleep_export_fixture::win5ReasonFiveScreenOff(continuing);
        {std::ofstream stream{std::filesystem::path(SG_FACT_LOG)};stream<<reasonFive.dump()<<'\n';}
        const auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());
        expect(result.receipt["selectedEvidence"]==reasonFive&&
            result.receipt["selectedEvidenceIdentity"]["originalEventFileTime100ns"]==reasonFive["details"]["exitFileTime100ns"]&&
            result.receipt["selectedSessionInReportVerified"]==false&&result.receipt["powerActionAuthorized"]==false,
            "actual ordinary export carries the correct Win5 Reason 5 segment without power authority");
        std::filesystem::remove(std::filesystem::path(SG_FACT_LOG));
    }
    const auto ac=sleep_export_fixture::g3eAcScreenOff();
    const auto brief=[&] {
        auto fact=ac;
        fact["event"]="s0-long-screen-off-brief-sleep-continuing";
        auto& d=fact["details"];
        d["entryReason"]=d["exitReason"]=16777220;
        d["entryFileTime100ns"]=134401607489550000ULL;
        d["exitFileTime100ns"]=134401609716680000ULL;
        d["sessionUs"]=222714334;
        d["screenOffUs"]=221710008;
        d["sleepUs"]=1004539;
        d["windowsSleepEntered"]=true;
        return fact;
    }();
    for(const int entryReason:{1,16777220}){
        reset();auto expected=brief;
        auto& d=expected["details"];
        d["entryReason"]=entryReason;
        auto wrong=expected;
        wrong["details"]["powerActionAuthorized"]=true;
        auto realSleep=expected;
        realSleep["details"]["sleepUs"]=57913729;
        auto unrelated=expected;
        unrelated["details"]["entryReason"]=12;
        {std::ofstream stream{std::filesystem::path(SG_FACT_LOG)};
         stream<<expected.dump()<<'\n'<<wrong.dump()<<'\n'<<realSleep.dump()<<'\n'<<unrelated.dump()<<'\n';}
        const auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());
        expect(result.receipt["selectedEvidence"]==expected&&
            result.receipt["selectedEvidenceIdentity"]["originalEventFileTime100ns"]==d["exitFileTime100ns"]&&
            result.receipt["sleepSuccessWitness"]==false&&result.receipt["powerActionAuthorized"]==false,
            "ordinary export keeps the paired brief-Sleep segment and rejects claims with action authority or sustained Sleep");
        std::filesystem::remove(std::filesystem::path(SG_FACT_LOG));
    }
    const auto entry=ac["details"]["entryFileTime100ns"].get<std::uint64_t>();
    const auto exit=ac["details"]["exitFileTime100ns"].get<std::uint64_t>();
    constexpr std::uint64_t day=864000000000ULL;
    {
        reset();
        fixtureExportFileTime=entry+day*2;
        auto previous=std::filesystem::path(SG_FACT_LOG+L".previous");
        {std::ofstream stream{previous};stream<<ac.dump()<<'\n';}
        {std::ofstream stream{std::filesystem::path(SG_FACT_LOG)};stream<<"{\"event\":\"ordinary-writer-noise\"}\n";}
        const auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());
        std::filesystem::remove(previous);
        std::filesystem::remove(std::filesystem::path(SG_FACT_LOG));
        expect(queryCalls==1&&result.receipt["selectedEvidence"]==ac&&result.receipt["historyDays"]==3&&
            result.receipt["powerActionAuthorized"]==false,
            "rotated-only G3E paired evidence did not reach the single bounded report selector");
    }
    const auto current=std::filesystem::path(SG_FACT_LOG);
    const auto previous=std::filesystem::path(SG_FACT_LOG+L".previous");
    for(const auto* paired:{&ac,&brief}) {
        reset();
        fixtureExportFileTime=(*paired)["details"]["entryFileTime100ns"].get<std::uint64_t>()+day*2;
        const auto noise=json{{"event","ordinary-writer-noise"},{"details",{{"text",std::string(3300,'x')}}}}.dump();
        {std::ofstream stream{previous};stream<<paired->dump()<<'\n';
         for(unsigned i=0;i<330;++i)stream<<noise<<'\n';
         stream<<"{partial";}
        const auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());
        expect(queryCalls==1&&lastTimeout==10000&&result.receipt["historyDays"]==3&&
            lastCommand.find(L" /duration 3 /output ")!=std::wstring::npos&&result.receipt["selectedEvidence"]==*paired&&
            result.receipt["selectedEvidenceIdentity"]["originalEventFileTime100ns"]==(*paired)["details"]["exitFileTime100ns"]&&
            result.receipt["targetPowerState"]=="UNKNOWN"&&result.receipt["selectedSessionInReportVerified"]==false&&
            result.receipt["sleepSuccessWitness"]==false&&result.receipt["powerActionAuthorized"]==false,
            "both paired diagnoses survive at the head of a full rotation with one bounded report query");
    }
    reset();fixtureExportFileTime=brief["details"]["exitFileTime100ns"].get<std::uint64_t>();
    {std::ofstream stream{current};stream<<off.dump()<<'\n';}
    {const auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());
     expect(queryCalls==1&&result.receipt["selectedEvidence"]==off,"current paired evidence takes precedence over rotation");}
    reset();fixtureExportFileTime=exit;g_sgFacts.push_back(ac.dump());
    {const auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());
     expect(queryCalls==1&&result.receipt["selectedEvidence"]==ac,"in-memory evidence keeps precedence over both files");}
    std::filesystem::remove(current);
    const auto unknownRotation=[&](std::uint64_t now,const std::string& bytes,const char* failure) {
        reset();fixtureExportFileTime=now;
        {std::ofstream stream{previous};stream<<bytes;}
        const auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());
        expect(queryCalls==1&&lastTimeout==10000&&result.receipt["historyDays"]==1&&
            result.receipt["selectedEvidence"].is_null()&&result.receipt["targetPowerState"]=="UNKNOWN"&&
            result.receipt["powerActionAuthorized"]==false,failure);
    };
    auto unbound=ac;unbound["details"].erase("generation");
    auto rawCounts=brief;rawCounts["event"]="s0-low-power-residency";
    unknownRotation(exit,unbound.dump()+'\n'+rawCounts.dump()+"\n{broken}\n",
        "rotation does not promote unbound, raw or malformed rows into paired evidence");
    unknownRotation(exit+day*3,ac.dump()+'\n',"rotation rejects evidence at the age limit");
    unknownRotation(exit-1,ac.dump()+'\n',"rotation rejects a future exit");
    unknownRotation(exit,ac.dump(),"rotation rejects an unterminated row");
    unknownRotation(exit,std::string((1024+128)*1024+1,'x')+'\n'+ac.dump()+'\n',
        "rotation does not select records beyond its bounded read");
    reset();fixtureExportFileTime=exit;pretendSymlink=previous;
    {std::ofstream stream{previous};stream<<ac.dump()<<'\n';}
    {const auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());
     expect(queryCalls==1&&result.receipt["selectedEvidence"].is_null()&&result.receipt["historyDays"]==1,
        "rotation does not follow a symbolic link");}
    std::filesystem::remove(previous);
    for(const auto& [now,days]:std::vector<std::pair<std::uint64_t,unsigned>>{{exit-1,1},{exit,1},
        {entry+day-1,1},{entry+day,2},{entry+day*2,3},{134401466270000000ULL,3},
        {entry+day*3,3},{exit+day*3,1}}){
        reset();fixtureExportFileTime=now;g_sgFacts.push_back(ac.dump());
        auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());
        expect(queryCalls==1&&lastTimeout==10000&&result.receipt["historyDays"]==days&&
            lastCommand.find(L" /duration "+std::to_wstring(days)+L" /output ")!=std::wstring::npos&&
            result.receipt["selectedEvidenceIdentity"]["originalEventFileTime100ns"]==exit&&
            result.receipt["selectedSessionInReportVerified"]==false&&result.receipt["powerActionAuthorized"]==false,
            "actual export uses the smallest bounded Windows report duration for the G3E interval");
    }
    fixtureExportFileTime=0;
    return checks;
}
} // namespace sleep_export_native_fixture
static unsigned sleepExportDiagnosticTests(){return sleep_export_fixture::helperTests()+sleep_export_fixture::deadlinePolicyTests()+sleep_export_native_fixture::adapterTests()+sleep_export_native_fixture::queryOutputTests()+sleep_export_native_fixture::captureFinalizationAdapterTests()+sleep_export_native_fixture::firstExportBudgetTests();}
