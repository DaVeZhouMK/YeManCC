// Same sleep.residency-evidence-consumer execution. Pure helper plus the actual
// export adapter, with inert Windows/command ports and owned local test files.
#include "sleep_export_diagnostics.h"
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
inline QueryResult report(const std::string& xml="<?xml version=\"1.0\"?><SleepStudy/>") {
    QueryResult q;q.ran=true;q.completion="child-exit";q.elapsedMs=7;q.xml=xml;return q;
}
inline std::string utf16(const std::string& ascii,bool little) {
    std::string out=little?std::string("\xff\xfe",2):std::string("\xfe\xff",2);
    for(const auto c:ascii){out.push_back(little?c:'\0');out.push_back(little?'\0':c);}return out;
}
inline unsigned helperTests() {
    using namespace ymcc::sleepexport;unsigned checks=0;
    const auto expect=[&](bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);};
    const auto selected=anomaly();const std::vector<std::string> valid={selected.dump()};
    expect(selectAnomaly(valid)==selected,"export selects original completed507 identity at exact120s silent-zero boundary");
    auto later=anomaly(2);auto positive=later;positive["details"]["assessment"]="hardware-residency-recorded";
    expect(selectAnomaly({selected.dump(),later.dump(),positive.dump()})==later,"export chooses latest qualifying historical anomaly despite newer positive record");
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
        expect(below["status"]=="UNKNOWN"&&below["reason"]=="session-below-documented-sleepstudy-minimum"&&
            below["reportAvailable"]==false&&below["queryAttempts"]==0&&contextCalls==0&&queryCalls==0,
            "short anomaly retains UNKNOWN without calling current or query");
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
    expect(receipt["status"]=="NOT_REQUIRED"&&queries==0&&currentChecks==0&&!receipt["reportAvailable"].get<bool>(),"no anomaly performs neither current check nor Windows query");
    receipt=collect(queryFacts,[](){return false;},query);
    expect(receipt["status"]=="UNKNOWN"&&queries==0&&receipt["queryAttempts"]==0,"noncurrent export performs zero queries");
    receipt=collect(queryFacts,current,query);
    expect(receipt["status"]=="AVAILABLE"&&receipt["reportAvailable"]==true&&queries==1&&requestedTimeout==8000&&currentChecks==2,"valid export performs exactly one8000ms query with pre and post context checks");
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
static unsigned guidSerial=0,queryCalls=0,cleanupFailures=0;
static std::uint32_t lastTimeout=0;
static bool writeReport=true,changeGeneration=false;
static std::wstring lastCommand;
static std::filesystem::path lastReport;
static std::uint64_t currentPowerGeneration(){return fixtureGeneration;}
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
struct RunOut {bool ran=true;DWORD exitCode=0,win32Error=0;const char* completion="child-exit";std::string out;};
static RunOut runCapture(const std::wstring& command,DWORD timeout,const std::function<bool()>& canceled) {
    ++queryCalls;lastCommand=command;lastTimeout=timeout;tick+=9;
    const auto marker=command.find(L" /output \"");
    if(marker==std::wstring::npos||command.back()!=L'"')throw std::runtime_error("export command missing quoted output");
    lastReport=command.substr(marker+10,command.size()-(marker+10)-1);
    if(writeReport){std::ofstream stream(lastReport,std::ios::binary);stream<<"<?xml version=\"1.0\"?><SleepStudy/>";}
    if(changeGeneration){++fixtureGeneration;if(!canceled())throw std::runtime_error("native cancellation lost generation check");}
    return {};
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
static unsigned adapterTests() {
    unsigned checks=0;const auto expect=[&](bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);};
    static_assert(!std::is_copy_constructible_v<SgWindowsSleepExport>);
    static_assert(std::is_move_constructible_v<SgWindowsSleepExport>);
    fixtureRoot=std::filesystem::current_path()/L"sleep-export-owned-fixture";
    if(!std::filesystem::create_directory(fixtureRoot))throw std::runtime_error("export fixture requires a fresh owned directory");
    // Files use scoped exact-path cleanup; retain nonempty failure evidence.
    struct Cleanup {~Cleanup(){std::error_code ec;std::filesystem::remove(fixtureRoot,ec);}} cleanup;
    SG_FACT_LOG=(fixtureRoot/L"ordinary-sleep-facts.log").wstring();
    const auto reset=[&](){g_sgFacts.clear();g_exitRequested=false;g_powerLifecycle=PowerLifecycle::Ready;queryCalls=0;lastTimeout=0;lastCommand.clear();lastReport.clear();pretendSymlink.clear();writeReport=true;changeGeneration=false;};
    reset();
    {auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());expect(result.receipt["status"]=="NOT_REQUIRED"&&queryCalls==0&&result.files.empty()&&result.owned.empty(),"actual export adapter with no anomaly creates no query or files");}
    for(const std::uint64_t duration:std::vector<std::uint64_t>{120000000ULL,153208591ULL,352000000ULL,599999999ULL}) {
        reset();auto historical=sleep_export_fixture::anomaly(92);
        historical["details"]["values"]["DurationInUs"]=duration;
        historical["details"]["values"]["ActiveResidencyInUs"]=duration;
        g_sgFacts.push_back(historical.dump());const auto serialBefore=guidSerial;
        auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());
        expect(result.receipt["status"]=="UNKNOWN"&&result.receipt["reason"]=="session-below-documented-sleepstudy-minimum"&&
            result.receipt["queryAttempts"]==0&&result.receipt["reportAvailable"]==false&&queryCalls==0&&
            result.files.empty()&&result.owned.empty()&&guidSerial==serialBefore&&std::filesystem::is_empty(fixtureRoot),
            "actual short-session adapter performs no command GUID allocation or file creation");
        expect(result.receipt["selectedEvidence"]==historical&&result.receipt["selectedEvidenceIdentity"]["writerSequence"]==92&&
            result.receipt["automaticRepairAuthorized"]==false&&result.receipt["powerActionAuthorized"]==false&&
            result.receipt["selectedSessionInReportVerified"]==false,
            "actual short-session export preserves original evidence identity without claiming target-session coverage");
    }
    reset();g_sgFacts.push_back(sleep_export_fixture::queryAnomaly().dump());std::vector<std::filesystem::path> owned;
    {
        auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());owned=result.owned;
        expect(result.receipt["reportAvailable"]==true&&queryCalls==1&&lastTimeout==8000&&result.files.size()==2&&owned.size()==2,("actual adapter runs one inert8000ms SleepStudy query and adds XML plus ordinary receipt: "+result.receipt.dump()+"; outputPathChars="+std::to_string(lastReport.wstring().size())).c_str());
        expect(lastCommand.find(L"\"C:\\Windows\\System32\\powercfg.exe\" /sleepstudy /xml /duration 1 /output \"")==0&&lastReport.parent_path()==fixtureRoot,"actual command uses quoted absolute System32 and unique direct owned output only");
        expect(result.receipt["selectedEvidence"]["details"]["values"]["DurationInUs"]==600000000u&&
            result.receipt["diagnostic"]=="WindowsSleepStudy"&&result.receipt["selectedEvidence"]==sleep_export_fixture::queryAnomaly()&&
            result.receipt["selectedSessionInReportVerified"]==false&&result.receipt["automaticRepairAuthorized"]==false,
            "exact600s boundary enables one original SleepStudy command while target-session coverage stays unverified");
        expect(std::all_of(owned.begin(),owned.end(),[](const auto& p){return std::filesystem::is_regular_file(p);}),"native report files remain available through archive ownership scope");
        auto moved=std::move(result);expect(result.owned.empty()&&moved.owned==owned,"moving archive scope transfers cleanup ownership exactly once");
    }
    expect(std::none_of(owned.begin(),owned.end(),[](const auto& p){return std::filesystem::exists(p);})&&cleanupFailures==0,"actual scoped destructor removes only unique owned files after archive scope");
    for(const bool symlink:{false,true}) {
        reset();g_sgFacts.push_back(sleep_export_fixture::queryAnomaly().dump());
        const auto existing=fixtureRoot/(L"windows-sleepstudy-{fixture-"+std::to_wstring(guidSerial+1)+L"}.xml");
        {std::ofstream stream(existing);stream<<"untouched sentinel";}
        if(symlink)pretendSymlink=existing; // Inert reparse status, no link privileges or external target.
        {auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());expect(result.receipt["status"]=="UNKNOWN"&&queryCalls==0&&result.owned.empty()&&result.files.empty(),"actual native adapter never owns overwrites or deletes an existing or reparse target");}
        std::ifstream stream(existing);std::string value;std::getline(stream,value);stream.close();
        expect(value=="untouched sentinel","rejected preexisting target survives returned-scope destruction unchanged");
        pretendSymlink.clear();std::filesystem::remove(existing);
    }
    reset();g_sgFacts.push_back(sleep_export_fixture::queryAnomaly().dump());g_powerLifecycle=PowerLifecycle::Suspended;
    {auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());expect(result.receipt["status"]=="UNKNOWN"&&queryCalls==0&&result.files.empty()&&result.owned.empty(),"nonready native generation performs zero query and creates no files");}
    reset();g_sgFacts.push_back(sleep_export_fixture::queryAnomaly().dump());writeReport=false;
    {auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());expect(result.receipt["status"]=="UNKNOWN"&&result.receipt["reportAvailable"]==false&&queryCalls==1&&result.files.size()==1&&std::filesystem::path(result.files[0]).extension()==L".json","native missing-report read failure exports only ordinary UNKNOWN receipt without retry");}
    reset();g_sgFacts.push_back(sleep_export_fixture::queryAnomaly().dump());changeGeneration=true;
    {auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());expect(result.receipt["status"]=="UNKNOWN"&&result.receipt["reportAvailable"]==false&&result.receipt["automaticRepairAuthorized"]==false&&queryCalls==1&&result.files.size()==1,"manual wake or native generation change suppresses XML and never authorizes power action");}
    reset();
    {std::ofstream stream{std::filesystem::path(SG_FACT_LOG)};stream<<sleep_export_fixture::queryAnomaly(8).dump()<<'\n'<<"{partial";}
    {auto result=sgExportWindowsSleepEvidence(fixtureRoot.wstring());expect(result.receipt["reportAvailable"]==true&&queryCalls==1&&result.receipt["selectedEvidenceIdentity"]["writerSequence"]==8,"actual adapter finds completed historical anomaly in bounded ordinary log and ignores partial tail");}
    std::filesystem::remove(std::filesystem::path(SG_FACT_LOG));
    return checks;
}
} // namespace sleep_export_native_fixture
static unsigned sleepExportDiagnosticTests(){return sleep_export_fixture::helperTests()+sleep_export_native_fixture::adapterTests();}
