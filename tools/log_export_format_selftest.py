"""Offline export-format and event-meaning regression. No product/hardware execution."""
from __future__ import annotations
import argparse,datetime,hashlib,json,pathlib,re,subprocess,zipfile
from log_frequency_selftest import ROOT,extract

PRELUDE=r'''
#define NOMINMAX
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include "log_export_evidence.h"
#include "log_ai_index.h"
#include <wincrypt.h>
#include <functional>
#pragma comment(lib,"advapi32.lib")
#pragma comment(lib,"user32.lib")
using json=nlohmann::json;
namespace fs=std::filesystem;
namespace fspath=std::filesystem;
static json checks=json::array(),rows=json::array();
static void check(const std::string& name,bool ok){checks.push_back({{"name",name},{"pass",ok}});}
static std::atomic<bool> g_fanLeaseKeepaliveArmed{true},g_exitRequested{false};
static std::atomic<unsigned long long> g_fanMsiModePowerGeneration{0},g_fanMsiModeRevision{0};
static bool allowed=true;
static unsigned long long currentPowerGeneration(){return 1;}
static unsigned long long currentFanPowerGeneration(){return 3;}
static std::string W2U(const std::wstring& value){if(value.empty())return {};const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);if(n<=0)throw std::runtime_error("UTF8 encode failed");std::string out(n,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),n,nullptr,nullptr);return out;}
static std::wstring U2W(const std::string& value){if(value.empty())return {};const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);if(n<=0)throw std::runtime_error("UTF8 decode failed");std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),n);return out;}
static bool fanHostPowerMayWrite(){return allowed;}
static void appendFanLifecycleLog(const char* event,const json& detail){auto row=detail;row["event"]=event;rows.push_back(row);}
'''
TESTS=r'''
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;const fs::path root=argv[1];fs::create_directories(root);
 using namespace ymcc::logexport;
 for(const char* name:{"model-adaptation.log","virtual-adaptation.log","native-lifecycle.log.12","native-detail.log.previous","input-capture.jsonl.prev",
     "input-host-cost.log.pending","input-host-signal.log.prev","input-host-report-chain.log",
     "steam-original-recovery.jsonl","steam-original-recovery.jsonl.1","steam-original-recovery.jsonl.previous",
     "yeman-fan-host-runtime.1.log","yeman-fan-host-runtime.12.log","yeman-fan-host-runtime.previous.log",
     "msi-controller-visibility.log","msi-controller-visibility.log.2","msi-controller-visibility-wake-2.log.12",
     "msi-controller-visibility-wake-123.log.previous","battery-charge-client.log.7","battery-charge-host.log.1"}){
     check(std::string("known-json-format:")+name,jsonLinesEvidenceName(name));
 }
 for(const char* name:{"yeman-fan-host20261015.log","yeman-fan-host20261015.1.log","app.log.12",
     "steam/logs/controller.txt","steam-controller.log","unknown.log","msi-controller-visibility-wake-x.log",
     "msi-controller-visibility-wake-.log","msi-controller-visibility-wake-2junk.log"})
     check(std::string("unrecognized-or-text-format:")+name,!jsonLinesEvidenceName(name));
 check("historical-json-document-format",jsonDocumentEvidenceName("gamepad-driver-setup.json.previous")&&jsonDocumentEvidenceName("wake-recovery-last.json.12")&&!jsonDocumentEvidenceName("steam/logs/file.txt"));
 check("recovery-jsonl-is-resident-evidence",std::string(aiEvidenceLane("steam-original-recovery.jsonl.1"))=="resident");
 const auto seed=root/L"fixture.data";const std::string content="{\"timestampUtc\":\"2026-10-15T09:00:00Z\",\"processId\":8}\n{\"unfinished\":";
 {std::ofstream f(seed,std::ios::binary);f<<content;}
 for(const char* name:{"yeman-fan-host-runtime.1.log","msi-controller-visibility-wake-2.log.12","battery-charge-host.log.1"}){
     const auto row=stagedFileEvidence(seed,name);
     check(std::string("staged-content-inspected:")+name,row["format"]=="json-lines"&&row["validJsonRows"]==1&&row["invalidJsonRows"]==1&&row["partialOrInvalidTrailingRecord"]==true);
 }
 const auto text=stagedFileEvidence(seed,"yeman-fan-host20261015.log");
 check("HC-text-is-not-bad-JSON",text["format"]=="opaque-file"&&!text.contains("invalidJsonRows"));
 std::ifstream in(seed,std::ios::binary);const std::string after((std::istreambuf_iterator<char>(in)),{});
 check("inspection-preserves-all-source-bytes",after==content);
 fanHostNoteMsiModeWrite(0);g_fanLeaseKeepaliveArmed=false;fanHostNoteMsiModeWrite(1);
 g_fanLeaseKeepaliveArmed=true;allowed=false;fanHostNoteMsiModeWrite(1);
 allowed=true;g_exitRequested=true;fanHostNoteMsiModeWrite(1);g_exitRequested=false;
 check("rejected-notification-paths-retain-original-no-revision-no-log-behavior",g_fanMsiModeRevision==0&&rows.empty());
 fanHostNoteMsiModeWrite(1);fanHostNoteMsiModeWrite(1);
 check("distinct-write-notifications-retain-both-revisions-and-evidence",g_fanMsiModeRevision==2&&g_fanMsiModePowerGeneration==3&&rows.size()==2&&rows[0]["revision"]==1&&rows[1]["revision"]==2&&rows[1]["powerGeneration"]==3);
 check("notification-receipt-does-not-claim-firmware-change-or-delivery",rows[1]["modeChangeVerified"]==false&&rows[1]["notificationDelivery"]=="not-observed-at-this-boundary"&&rows[1]["evidenceScope"]=="mode-write-may-have-occurred; notification-revision-recorded");

 const auto costName=[](char digit){return std::string("input-host-cost.log.archive.063928391234567890")+digit+".0123456789abcdef01234567.zip";};
 const std::vector<HostCostArchiveExportCandidate> costFiles={{costName('1'),3},{costName('3'),3},{costName('2'),3},{"unowned.zip",999}};
 const auto bounded=selectHostCostArchivesForExport(costFiles,6);
 check("cost-export-default-has-no-nested-volumes",HostCostArchiveExportMaxBytes==0);
 check("cost-export-newest-complete-volumes-fit-exact-budget",bounded["selectedSourceBytes"]==6&&bounded["selectedVolumes"]==2&&bounded["omittedVolumes"]==1&&bounded["archives"][0]["name"]==costName('3')&&bounded["archives"][1]["name"]==costName('2')&&bounded["archives"][2]["includedByBudget"]==false);
 check("cost-export-inventory-explicitly-retains-gap",bounded["allDiscoveredHistorySelected"]==false&&bounded["sourceHistoryModified"]==false&&bounded["archives"][2]["reason"]=="ordinary-export-cost-history-byte-budget");
 const auto zero=selectHostCostArchivesForExport(costFiles,0);
 const auto zeroByteLegacy=selectHostCostArchivesForExport({{costName('3'),0}});
 check("disabled-volume-export-also-excludes-empty-legacy-volume",zeroByteLegacy["selectedVolumes"]==0&&zeroByteLegacy["omittedVolumes"]==1);
 check("cost-export-zero-budget-retains-omission-inventory",zero["selectedVolumes"]==0&&zero["omittedVolumes"]==3);
 const auto oversize=selectHostCostArchivesForExport({{costName('3'),7},{costName('2'),1}},6);
 check("cost-export-never-truncates-oversized-newest-volume-or-fills-with-older-gap",oversize["selectedVolumes"]==0&&oversize["omittedVolumes"]==2);
 const auto unknown=selectHostCostArchivesForExport({{costName('3'),0,false,5},{costName('2'),3}},6);
 check("cost-export-size-error-is-explicit-gap-not-absence",unknown["sourceSizeErrors"]==1&&unknown["archives"][0]["sourceBytes"].is_null()&&unknown["archives"][0]["errorCode"]==5&&unknown["selectedVolumes"]==1&&unknown["allDiscoveredHistorySelected"]==false);
 const auto full=selectHostCostArchivesForExport(costFiles,9);
 check("cost-export-small-history-complete",full["allDiscoveredHistorySelected"]==true&&full["selectedVolumes"]==3&&full["knownDiscoveredBytes"]==9);
 const auto overflow=selectHostCostArchivesForExport({{costName('3'),(std::numeric_limits<unsigned long long>::max)()},{costName('2'),1}},6);
 check("cost-export-discovery-size-overflow-is-explicit-and-never-wraps-budget",overflow["knownDiscoveredBytesSaturated"]==true&&overflow["selectedVolumes"]==0);
 const auto costDir=root/L"cost-source";fs::create_directories(costDir);
 {std::ofstream f(costDir/fs::path(costName('3')),std::ios::binary);f<<"newest";}
 {std::ofstream f(costDir/fs::path(costName('2')),std::ios::binary);f<<"older";}
 fs::resize_file(costDir/fs::path(costName('2')),8ULL*1024*1024);
 {std::ofstream f(costDir/L"unowned.zip",std::ios::binary);f<<"ignored";}
 {std::ofstream f(costDir/fs::path(costName('1')+".tmp"),std::ios::binary);f<<"uncommitted";}
 // Execute the actual ordinary-export discovery/selection block on owned dummy files.
 std::wstring dataDir=costDir.wstring();json availability;std::vector<std::wstring> selectedPaths;
 auto addFile=[&](const std::wstring& value){selectedPaths.push_back(value);};
 PRODUCTION_COST_EXPORT_BLOCK
 const auto& discovery=availability["costArchiveDiscovery"];
 check("actual-export-block-discovers-only-committed-owned-volumes",discovery["archivesFound"]==2&&selectedPaths.empty()&&discovery["status"]=="complete-at-discovery");
 check("actual-export-block-uses-bounded-selection",discovery["exportSelection"]["budgetBytes"]==HostCostArchiveExportMaxBytes&&discovery["exportSelection"]["selectedSourceBytes"]==0&&discovery["exportSelection"]["omittedVolumes"]==2);
 check("actual-export-block-preserves-originals-and-unrelated-files",fs::exists(costDir/fs::path(costName('3')))&&fs::exists(costDir/fs::path(costName('2')))&&fs::exists(costDir/L"unowned.zip")&&fs::exists(costDir/fs::path(costName('1')+".tmp")));
 const auto fieldInventory=root/L"field-cost-archive-candidates.json";
 if(fs::exists(fieldInventory)) {
     std::ifstream input(fieldInventory);const auto inventory=json::parse(input);
     std::vector<HostCostArchiveExportCandidate> candidates;
     for(const auto& item:inventory["archives"])candidates.push_back({item["name"].get<std::string>(),item["bytes"].get<unsigned long long>()});
     const auto fieldSelection=selectHostCostArchivesForExport(std::move(candidates));
     std::ofstream output(root/L"field-cost-export-selection.json");output<<fieldSelection.dump(2)<<'\n';output.close();
     check("field-bundle-whole-volume-selection-within-production-budget",fieldSelection["selectedSourceBytes"].get<unsigned long long>()<=HostCostArchiveExportMaxBytes);
     check("field-bundle-all-archive-candidates-in-selection-inventory",fieldSelection["archives"].size()==inventory["archives"].size());
     check("field-bundle-retained-and-omitted-counts-balanced",fieldSelection["selectedVolumes"].get<unsigned long long>()+fieldSelection["omittedVolumes"].get<unsigned long long>()==inventory["archives"].size());
 }
 const std::string archive="input-host-cost.log.archive.0639283912345678901.0123456789abcdef01234567.zip";
 check("cost-archive-exact-owned-name",hostCostArchiveEvidenceName(archive));
 check("cost-archive-in-progress-excluded",!hostCostArchiveEvidenceName(archive+".tmp"));
 check("cost-archive-unrelated-name-excluded",!hostCostArchiveEvidenceName("something.zip"));
 fs::create_directories(root/L"staged"/L"steam"/L"logs");
 {std::ofstream f(root/L"staged"/L"steam"/L"logs"/L"controller.txt");f<<"third party text\n";}
 std::vector<std::pair<std::wstring,std::wstring>> entries={{root.wstring(),L"fixture.data"},{(root/L"staged").wstring(),L"steam"},{root.wstring(),L"fixture.data"}};
 check("real-ai-index-written",writeAiLogIndex(root/L"ai",entries,sha256File));
 std::ifstream ai(root/L"ai"/L"ai-log-index.jsonl");std::vector<json> frames;std::string line;
 while(std::getline(ai,line))frames.push_back(json::parse(line));
 check("bundle-and-file-markers-balanced",frames.size()==8&&frames.front()["record"]=="bundle_begin"&&frames.back()["record"]=="bundle_end"&&frames.back()["indexedFiles"]==3);
 check("steam-included-as-third-party-bytes",frames[3]["entry"]=="steam/logs/controller.txt"&&frames[3]["sourceScope"]=="third-party-steam"&&frames[3]["format"]=="text-or-opaque");
 check("duplicate-entry-occurrences-kept",frames[1]["archiveEntryOccurrence"]==0&&frames[5]["archiveEntryOccurrence"]==1&&frames[1]["fileId"]!=frames[5]["fileId"]);
 check("full-staged-sha256-begin-end-agree",frames[1]["sha256"]==sha256File(seed.wstring())&&frames[2]["sha256"]==frames[1]["sha256"]);
 check("file-end-does-not-claim-session-exit",frames[2]["processSessionComplete"].is_null()&&frames.back()["processSessionComplete"].is_null());
 check("failed-hash-refuses-successful-index",!writeAiLogIndex(root/L"bad-ai",entries,[](const std::wstring&){return std::string{};}));
 const auto clockSeed=root/L"sleep-clock.log";
 {std::ofstream f(clockSeed);f<<json({{"time","2026-10-25 23:20:11.820"},{"timestampUtc","2026-10-25T15:20:11.820Z"},{"timestampLocal","2026-10-25T23:20:11.820+08:00"},{"writerPid",77}}).dump()<<'\n';}
 const auto clocks=stagedFileEvidence(clockSeed,"sleep-facts.log");
 check("staged-sleep-prefers-explicit-UTC-and-reports-field",clocks["firstTime"]=="2026-10-25T15:20:11.820Z"&&clocks["firstTimeField"]=="timestampUtc"&&clocks["lastTimeField"]=="timestampUtc");
 const auto guide=aiReadingGuide();
 check("virtual-evidence-budget-and-proof-guide",std::string(aiEvidenceLane("virtual-adaptation.log"))=="resident"&&guide["virtualAdaptation"].contains("proofBoundary")&&guide["virtualAdaptation"].contains("generation"));
 check("adaptation-log-resident-and-replay-guide",std::string(aiEvidenceLane("model-adaptation.log"))=="resident"&&guide["modelAdaptation"]["file"]=="model-adaptation.log"&&guide["modelAdaptation"].contains("replay"));
 check("cost-export-inventory-guide-points-to-actual-export-receipt",guide["retention"]["costArchiveExportInventory"].get<std::string>().find("log-export-availability.json costArchiveDiscovery.exportSelection")==0);
 check("ai-guide-warns-about-untrusted-input-and-inspection-scope",guide.contains("instructionTrust")&&guide.contains("inspection")&&guide.contains("clockRules")&&guide.contains("identityRules")&&guide.contains("sleepFacts"));
 check("ai-guide-common-steam-power-evidence-stages-and-coverage",guide.contains("commonSteamAndPower")&&guide["commonSteamAndPower"].contains("consumerGate")&&guide["commonSteamAndPower"].contains("cancellation")&&guide["commonSteamAndPower"].contains("steamFailure")&&guide["commonSteamAndPower"].contains("coverage"));
 unsigned failures=0;for(const auto& item:checks)if(!item["pass"].get<bool>())++failures;
 std::cout<<json({{"checks",checks},{"failures",failures}}).dump(2)<<std::endl;return failures?3:0;
}
'''

OPAQUE_TAIL_TESTS = r'''static bool allocationMeasure=false;static size_t largestAllocation=0;
void* operator new(std::size_t n){if(allocationMeasure)largestAllocation=(std::max)(largestAllocation,n);if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
struct InputTrace {unsigned long long readCalls=0,getCalls=0,requestedBytes=0,returnedBytes=0;};
static InputTrace inputTrace;
static bool failRead=false,growOnSeek=false,shrinkOnSeek=false;
struct CountedInput:std::ifstream {
    fs::path path;
    CountedInput(const fs::path& value,std::ios::openmode mode):std::ifstream(value,mode),path(value){}
    void mutate(){
        if(growOnSeek){growOnSeek=false;std::ofstream growthWriter(path,std::ios::binary|std::ios::app);growthWriter<<'\n';}
        if(shrinkOnSeek){shrinkOnSeek=false;fs::resize_file(path,0);}
    }
    CountedInput& seekg(pos_type position){mutate();std::ifstream::seekg(position);return *this;}
    CountedInput& seekg(off_type offset,std::ios_base::seekdir direction){std::ifstream::seekg(offset,direction);return *this;}
    CountedInput& read(char* data,std::streamsize count){++inputTrace.readCalls;inputTrace.requestedBytes+=static_cast<unsigned long long>(count);if(failRead)setstate(std::ios::badbit);else std::ifstream::read(data,count);inputTrace.returnedBytes+=static_cast<unsigned long long>(gcount());return *this;}
    CountedInput& get(char& value){++inputTrace.getCalls;++inputTrace.requestedBytes;if(failRead)setstate(std::ios::badbit);else std::ifstream::get(value);inputTrace.returnedBytes+=static_cast<unsigned long long>(gcount());return *this;}
};
// ACTUAL_BEFORE_INSPECTOR
// ACTUAL_CURRENT_INSPECTOR
static void seedOpaque(const fs::path& path,unsigned long long size,char last){
    fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary|std::ios::trunc);const std::string block(8192,'X');
    unsigned long long left=size;while(left){const auto n=(std::min)(left,static_cast<unsigned long long>(block.size()));out.write(block.data(),static_cast<std::streamsize>(n));left-=n;}
    if(size){out.seekp(static_cast<std::streamoff>(size-1));out.put(last);}out.close();if(!out)throw std::runtime_error("own seed failed");
}
static json measuredInspect(bool before,const fs::path& path,const std::string& entry,InputTrace& trace,size_t& peak){
    inputTrace={};largestAllocation=0;allocationMeasure=true;
    auto value=before?beforeStagedFileEvidence(path,entry):currentStagedFileEvidence(path,entry);
    allocationMeasure=false;trace=inputTrace;peak=largestAllocation;return value;
}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 2;const fs::path root(argv[1]);if(fs::exists(root))return 3;fs::create_directories(root);
    const auto warm=root/L"hash-warmup.txt";seedOpaque(warm,1,'X');DWORD cold=0,warm1=0,warm2=0,handlesAfter=0;
    GetProcessHandleCount(GetCurrentProcess(),&cold);const auto hash1=sha256File(warm.wstring());GetProcessHandleCount(GetCurrentProcess(),&warm1);const auto hash2=sha256File(warm.wstring());GetProcessHandleCount(GetCurrentProcess(),&warm2);
    if(hash1.size()!=64||hash1!=hash2||warm1!=warm2)throw std::runtime_error("own hash warmup unstable");
    json measurements=json::array();bool opaque=true,allocationBound=true,metadata=true;
    const unsigned long long sizes[]={0,1,8191,8ULL*1024*1024-1,8ULL*1024*1024,8ULL*1024*1024+1,64ULL*1024*1024};
    for(const auto size:sizes){for(const char last:{'X','\n'}){
        const auto path=root/(L"opaque-"+std::to_wstring(size)+(last=='\n'?L"-lf.bin":L"-x.bin"));seedOpaque(path,size,last);
        InputTrace oldTrace,newTrace;size_t oldPeak=0,newPeak=0;
        const auto old=measuredInspect(true,path,"snapshot.json",oldTrace,oldPeak);const auto now=measuredInspect(false,path,"snapshot.json",newTrace,newPeak);
        opaque=opaque&&oldTrace.requestedBytes==(std::min)(size,8ULL*1024*1024)&&newTrace.requestedBytes==(size?1ULL:0ULL)&&newTrace.returnedBytes==newTrace.requestedBytes;
        metadata=metadata&&now["bytes"]==size&&now["endsWithNewline"]==(size==0||last=='\n')&&now["format"]=="opaque-file"&&!now.contains("invalidJsonRows")&&now["earlierBytesNotInspected"]==(size?size-1:0)&&now["inspectionScope"]==(size?"final-byte-only":"empty-file");
        allocationBound=allocationBound&&newPeak<65536&&(size<8ULL*1024*1024||oldPeak>=8ULL*1024*1024);
        measurements.push_back({{"bytes",size},{"lastLf",last=='\n'},{"oldLogicalReadBytes",oldTrace.requestedBytes},{"newLogicalReadBytes",newTrace.requestedBytes},{"oldLargestCppAllocation",oldPeak},{"newLargestCppAllocation",newPeak}});
    }}
    check("opaque0-through64MiB-inspection-requests-only-final-byte",opaque);
    check("opaque-largest-observed-Cpp-allocation-no-longer-scales-to8MiB",allocationBound);
    check("opaque-scope-and-uninspected-byte-count-match-smaller-inspection",metadata);
    const auto normal=root/L"native-lifecycle.log";const std::vector<std::string> shapes={"","{}\n","{\"time\":\"a\"}\n{\"time\":\"b\"}","{\"x\":",std::string(70000,'X'),"{}\r\n",std::string("\xff\0",2)};
    bool jsonl=true;for(const auto& shape:shapes){std::ofstream out(normal,std::ios::binary|std::ios::trunc);out.write(shape.data(),static_cast<std::streamsize>(shape.size()));out.close();InputTrace a,b;size_t x=0,y=0;const auto old=measuredInspect(true,normal,"native-lifecycle.log",a,x);const auto now=measuredInspect(false,normal,"native-lifecycle.log",b,y);jsonl=jsonl&&old==now&&a.requestedBytes==b.requestedBytes;}
    seedOpaque(normal,8ULL*1024*1024+100,'\n');{std::ofstream out(normal,std::ios::binary|std::ios::app);out<<"{\"time\":\"retained\"}\n{";}
    InputTrace a,b;size_t x=0,y=0;const auto oldLarge=measuredInspect(true,normal,"native-lifecycle.log",a,x);const auto newLarge=measuredInspect(false,normal,"native-lifecycle.log",b,y);
    check("all-original-JSONL-small-and-tail-boundary-results-remain-exact",jsonl&&oldLarge==newLarge&&a.requestedBytes==b.requestedBytes&&newLarge["partialOrInvalidTrailingRecord"]==true);
    const auto changing=root/L"changing.bin";bool growth=true;for(const bool before:{true,false}){seedOpaque(changing,1,'X');growOnSeek=true;InputTrace t;size_t peak=0;const auto value=measuredInspect(before,changing,"snapshot.json",t,peak);growth=growth&&fs::file_size(changing)==2&&value["bytes"]==1&&value["endsWithNewline"]==false;}
    check("growth-after-size-query-still-reads-original-last-byte-position",growth);
    bool shrink=true;for(const bool before:{true,false}){seedOpaque(changing,1,'X');shrinkOnSeek=true;InputTrace t;size_t peak=0;const auto value=measuredInspect(before,changing,"snapshot.json",t,peak);shrink=shrink&&value["status"]=="staged-read-error"&&!value.contains("endsWithNewline");}
    check("shrink-before-read-is-error-and-never-fabricates-empty-or-LF",shrink);
    seedOpaque(changing,9,'\n');failRead=true;const auto failed=currentStagedFileEvidence(changing,"unknown.bin");failRead=false;
    check("returned-read-failure-cannot-become-healthy-opaque-metadata",failed["status"]=="staged-read-error"&&!failed.contains("endsWithNewline"));
    HANDLE blocker=CreateFileW(changing.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    const auto refused=ymcc::logexport::stagedFileEvidence(changing,"unknown.bin");if(blocker!=INVALID_HANDLE_VALUE)CloseHandle(blocker);
    check("actual-sharing-refusal-remains-original-staged-read-error",blocker!=INVALID_HANDLE_VALUE&&refused["status"]=="staged-read-error");
    const auto doc=root/L"package"/L"payload.bin",steam=root/L"package"/L"steam"/L"logs"/L"text.txt";seedOpaque(doc,8ULL*1024*1024+1,'\n');seedOpaque(steam,7,'X');
    std::vector<std::pair<std::wstring,std::wstring>> entries={{doc.parent_path().wstring(),L"payload.bin"},{(root/L"package").wstring(),L"steam"}};
    const auto beforeHash=sha256File(doc.wstring());unsigned digests=0;const bool indexed=writeAiLogIndex(root/L"ai",entries,[&](const std::wstring& path){++digests;return sha256File(path);});
    std::ifstream index(root/L"ai"/L"ai-log-index.jsonl");std::vector<json> frames;for(std::string line;std::getline(index,line);)frames.push_back(json::parse(line));index.close();
    check("actual-index-keeps-full-file-hash-size-order-and-third-party-role",indexed&&digests==2&&frames.size()==6&&frames[1]["sha256"]==beforeHash&&frames[1]["bytes"]==8ULL*1024*1024+1&&frames[3]["lane"]=="external"&&frames[3]["format"]=="text-or-opaque"&&frames[2]["inspectionScope"]=="final-byte-only");
    check("failed-full-digest-still-refuses-index",!writeAiLogIndex(root/L"bad-ai",entries,[](const std::wstring&){return std::string{};}));
    const auto guide=aiReadingGuide();check("guide-matches10s-query-archive-timing-and-sidecar-failure-scopes",guide["ordinaryExportStages"]["windowsReport"].get<std::string>().find("first query retains10000ms")==0&&guide.contains("ordinaryArchiveAttemptTiming")&&guide.contains("deckySidecarLogFailures")&&guide["inspection"].get<std::string>().find("final-byte-only")!=std::string::npos);
    check("inspection-and-index-keep-every-original-file-byte",sha256File(doc.wstring())==beforeHash&&fs::file_size(doc)==8ULL*1024*1024+1);
    GetProcessHandleCount(GetCurrentProcess(),&handlesAfter);check("all-owned-inspection-and-hash-handles-return-to-observed-warm-count",warm2==handlesAfter);
    unsigned failures=0;for(const auto& value:checks)failures+=!value["pass"].get<bool>();
    std::cout<<json{{"selectedCase","opaque-tail"},{"checks",checks},{"failures",failures},{"measurements",measurements},{"coldHandlesBeforeHash",cold},{"firstHashHandles",warm1},{"handlesBefore",warm2},{"handlesAfter",handlesAfter},{"actualIfstreamBase",true},{"readCounterMeaning","logical bytes requested by actual read/get calls;not OS-buffer or disk-sector bytes"},{"hardwareActions",0},{"productOrSteamActions",0}}.dump(2)<<std::endl;return failures?3:0;
}catch(const std::exception& error){allocationMeasure=false;std::cerr<<error.what()<<std::endl;return 4;}}
'''


def opaque_tail_source(before_header):
    """Full real inspector/index/hash;single declared ifstream counter adapter."""
    source=(ROOT/'native/main.cpp').read_text(encoding='utf-8-sig')
    header=(ROOT/'native/log_export_evidence.h').read_text(encoding='utf-8-sig')
    before=pathlib.Path(before_header).read_text(encoding='utf-8-sig')
    signature='inline EvidenceJson stagedFileEvidence(const std::filesystem::path& staged,const std::string& entry) noexcept {'
    old=extract(before,signature);current=extract(header,signature)
    def counted(body,name):
        if body.count('std::ifstream in(')!=1:raise ValueError('inspector stream port changed')
        return body.replace('inline EvidenceJson stagedFileEvidence(', 'static EvidenceJson '+name+'(',1).replace('std::ifstream in(','CountedInput in(',1)
    tests=OPAQUE_TAIL_TESTS.replace('// ACTUAL_BEFORE_INSPECTOR',counted(old,'beforeStagedFileEvidence'),1).replace('// ACTUAL_CURRENT_INSPECTOR',counted(current,'currentStagedFileEvidence'),1)
    hashes=extract(source,'static std::string sha256FileCancellable(const std::wstring& path, const std::function<bool()>& cancelled) {')+'\n'+extract(source,'static std::string sha256File(const std::wstring& path) {')
    return PRELUDE+'\n#include <cstdlib>\n#include <new>\n#include <stdexcept>\n#include <iterator>\nusing namespace ymcc::logexport;\n'+hashes+'\n'+tests


def run_opaque_tail(args,source=None,selected_case="opaque-tail"):
    import shutil,time
    source=opaque_tail_source(args.opaque_before_header) if source is None else source
    out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
    run=out/(selected_case+'-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f'));run.mkdir()
    def put(name,value):
        with (run/name).open('xb') as stream:stream.write(value.encode('utf8') if isinstance(value,str) else value)
    cpp=run/'fixture.cpp';exe=run/'fixture.exe';put('fixture.cpp',source)
    compiler=shutil.which('cl.exe')
    if not compiler:raise RuntimeError('Reviewed compiler environment required')
    arguments=[compiler,'/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/W4','/WX','/wd4505','/showIncludes',str(cpp),'/I'+str(ROOT/'native'),'/I'+str(ROOT/'deps/json'),'/Fo'+str(run/'fixture.obj'),'/Fe'+str(exe),'/link','/VERBOSE:LIB']
    start=time.monotonic();built=subprocess.run(arguments,cwd=run,capture_output=True,timeout=180)
    put('compile.stdout.raw',built.stdout);put('compile.stderr.raw',built.stderr)
    put('COMPILE.json',json.dumps({'arguments':arguments,'exitCode':built.returncode,'elapsedMs':int((time.monotonic()-start)*1000)}))
    if built.returncode:return built.returncode
    result=subprocess.run([str(exe),str(run/'files')],cwd=run,capture_output=True,timeout=90)
    put('native.stdout.raw',result.stdout);put('native.stderr.raw',result.stderr)
    value=json.loads(result.stdout);put('RESULT.json',json.dumps({'exitCode':result.returncode,'result':value},ensure_ascii=False,indent=2))
    print(json.dumps({'case':selected_case,'checks':len(value['checks']),'failures':value['failures'],'result':str(run/'RESULT.json')}))
    return result.returncode


JSONL_RANGE_TESTS = r'''struct AllocationTrace {unsigned long long calls=0,bytes=0,watched=0;size_t lastSize=0,largest=0;};
static AllocationTrace allocationTrace;static bool allocationMeasure=false;static size_t watchedSize=0;
void* operator new(std::size_t n){if(allocationMeasure){++allocationTrace.calls;allocationTrace.bytes+=n;allocationTrace.lastSize=n;allocationTrace.largest=(std::max)(allocationTrace.largest,n);if(n==watchedSize)++allocationTrace.watched;}if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
// ACTUAL_BEFORE_INSPECTOR
// ACTUAL_CURRENT_INSPECTOR
static void rangePut(const fs::path& path,const std::string& value){fs::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(value.data(),static_cast<std::streamsize>(value.size()));file.close();if(!file)throw std::runtime_error("private seed failed");}
static std::string rangeBytes(const fs::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
static json measuredRange(bool before,const fs::path& path,AllocationTrace& measured){allocationTrace={};allocationMeasure=true;auto value=before?beforeStagedFileEvidence(path,"native-lifecycle.log"):currentStagedFileEvidence(path,"native-lifecycle.log");allocationMeasure=false;measured=allocationTrace;return value;}
static json traceJson(const AllocationTrace& value){return {{"calls",value.calls},{"bytes",value.bytes},{"watched",value.watched},{"largest",value.largest}};}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 2;const fs::path root(argv[1]);if(fs::exists(root))return 3;fs::create_directories(root);
    const auto file=root/L"native-lifecycle.log";rangePut(file,"{}\n");DWORD cold=0,warm1=0,beforeHandles=0,afterHandles=0;
    GetProcessHandleCount(GetCurrentProcess(),&cold);const auto firstHash=sha256File(file.wstring());GetProcessHandleCount(GetCurrentProcess(),&warm1);const auto secondHash=sha256File(file.wstring());GetProcessHandleCount(GetCurrentProcess(),&beforeHandles);if(firstHash.size()!=64||firstHash!=secondHash||warm1!=beforeHandles)throw std::runtime_error("own hash warmup unstable");
    const std::string calibration(4096,'x');allocationTrace={};allocationMeasure=true;const auto calibrated=calibration.substr(0,4096);allocationMeasure=false;
    if(allocationTrace.calls!=1||calibrated.size()!=4096)throw std::runtime_error("copy-size calibration changed");watchedSize=allocationTrace.lastSize;
    const std::string line="{\"x\":1}"+std::string(4089,' ');std::string data;for(unsigned i=0;i<128;++i)data+=line+'\n';rangePut(file,data);
    AllocationTrace oldTrace,newTrace;const auto old=measuredRange(true,file,oldTrace);const auto now=measuredRange(false,file,newTrace);
    check("128-long-valid-row-inspection-metadata-remains-exact",old==now&&now["validJsonRows"]==128&&now["invalidJsonRows"]==0&&rangeBytes(file)==data);
    check("one-calibrated-owned-substring-allocation-is-removed-per-long-row",oldTrace.watched==128&&newTrace.watched==0&&oldTrace.calls>=newTrace.calls+128);
#ifdef YMCC_JSONL_RANGE_OLD_ONLY
    GetProcessHandleCount(GetCurrentProcess(),&afterHandles);unsigned failed=0;for(const auto& value:checks)failed+=!value["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failed},{"oldAllocation",traceJson(oldTrace)},{"newAllocation",traceJson(newTrace)},{"watchedCopyBytes",watchedSize},{"handlesBefore",beforeHandles},{"handlesAfter",afterHandles},{"actualOldInspectorBody",true},{"hardwareActions",0},{"productOrSteamActions",0}}.dump(2)<<std::endl;return failed?3:0;
#else
    check("gross-requested-allocation-bytes-drop-by-exact128-copy-buffers",oldTrace.bytes>=newTrace.bytes+128*watchedSize&&oldTrace.largest==newTrace.largest);
    const auto longAllocation=json{{"old",traceJson(oldTrace)},{"current",traceJson(newTrace)},{"calibratedCopyBytes",watchedSize},{"rows",128}};
    const std::string richLine=json{{"timestampUtc","2026-11-17T00:00:00Z"},{"message",std::string(4096,'x')},{"state",{{"ok",true},{"count",17}}}}.dump();std::string rich;for(unsigned i=0;i<32;++i)rich+=richLine+'\n';rangePut(file,rich);
    const auto richOld=measuredRange(true,file,oldTrace);const auto richNow=measuredRange(false,file,newTrace);
    check("32-long-payload-and-time-rows-keep-fields-with32-fewer-allocations",richOld==richNow&&richNow["firstTime"]=="2026-11-17T00:00:00Z"&&oldTrace.calls>=newTrace.calls+32&&oldTrace.bytes>newTrace.bytes);
    const auto richAllocation=json{{"old",traceJson(oldTrace)},{"current",traceJson(newTrace)},{"rows",32},{"lineBytes",richLine.size()}};
    std::vector<std::pair<std::string,std::string>> shapes={
        {"empty",""},{"blank-lines","\n\r\n \n"},{"adjacent","{}\n[]\nnull\n17\ntrue"},{"no-newline","{\"time\":\"last\"}"},
        {"bad-tail","{}\n{broken"},{"bad-middle","{broken\n{}\n"},{"bad-utf8",std::string("{\"x\":\"\xff\"}")},
        {"nul",std::string("{}\0{}",5)},{"bom",std::string("\xef\xbb\xbf{}\n\xef\xbb\xbf[]")},{"unicode","{\"x\":\"中文🎮\u2028\"}\n"},
        {"numeric-overflow","{\"n\":1e999}"},{"duplicate-key","{\"x\":1,\"x\":2}"},{"trailing-token","{}junk\n{}"},
        {"time-priority","{\"timestampUtc\":\"zulu\",\"timeWithOffset\":\"offset\",\"time\":\"legacy\"}\n{\"timestampLocal\":\"local\",\"atUtc\":\"utc\"}"},
        {"time-types","{\"timestampUtc\":false,\"timeWithOffset\":1,\"timestampLocal\":null,\"atUtc\":\"utc\",\"time\":\"fallback\"}"},
        {"line65536","{\"s\":\""+std::string(65528,'x')+"\"}"},{"line65537","{\"s\":\""+std::string(65529,'x')+"\"}"},
        {"tail-window",std::string(8*1024*1024,'X')+"\n{\"time\":\"retained\"}\n{"}};
    bool exact=true,bytesKept=true;json evidence=json::array();unsigned number=0;
    for(const auto& shape:shapes){const auto path=root/L"cases"/(std::to_wstring(number++)+L".jsonl");rangePut(path,shape.second);const auto before=beforeStagedFileEvidence(path,"native-lifecycle.log"),actual=currentStagedFileEvidence(path,"native-lifecycle.log");exact=exact&&before==actual;bytesKept=bytesKept&&rangeBytes(path)==shape.second;evidence.push_back({{"case",shape.first},{"bytes",shape.second.size()},{"result",actual}});}
    check("18-format-boundary-UTF8-BOM-NUL-number-and-time-shapes-match-old",exact&&shapes.size()==18);
    check("each-format-input-is-kept-byte-exact",bytesKept);
    const auto& last=evidence.back()["result"];check("8MiB-leading-boundary-and-bad-final-tail-keep-original-scope",last["inspectionScope"]=="retained-tail-only"&&last["partialOrInvalidTrailingRecord"]==true&&last["validJsonRows"]==1&&last["invalidJsonRows"]==1&&last["lastTime"]=="retained");
    rangePut(file,"{}\n[]\nnull\n");const auto smallOld=measuredRange(true,file,oldTrace),smallNow=measuredRange(false,file,newTrace);check("small-SSO-row-path-adds-no-allocation-and-keeps-results",smallOld==smallNow&&newTrace.calls<=oldTrace.calls&&newTrace.bytes<=oldTrace.bytes);
    const auto missing=root/L"missing.jsonl";check("unreadable-file-keeps-original-typed-metadata",beforeStagedFileEvidence(missing,"native-lifecycle.log")==currentStagedFileEvidence(missing,"native-lifecycle.log"));
    const auto opaque=root/L"opaque.bin";rangePut(opaque,"opaque\n");check("opaque-final-byte-path-remains-exact",beforeStagedFileEvidence(opaque,"snapshot.json")==currentStagedFileEvidence(opaque,"snapshot.json"));
    rangePut(file,"{\"time\":\"first\",\"x\":1}\n{\"time\":\"last\",\"x\":2}\n");const auto beforeHash=sha256File(file.wstring());std::vector<std::pair<std::wstring,std::wstring>> entries={{root.wstring(),L"native-lifecycle.log"}};
    const auto actual=ymcc::logexport::stagedFileEvidence(file,"native-lifecycle.log");const auto expected=beforeStagedFileEvidence(file,"native-lifecycle.log");bool indexed=writeAiLogIndex(root/L"ai",entries,sha256File);
    std::ifstream index(root/L"ai"/L"ai-log-index.jsonl");std::vector<json> frames;for(std::string value;std::getline(index,value);)frames.push_back(json::parse(value));index.close();
    check("actual-index-full-hash-size-and-inspection-fields-remain-correct",indexed&&frames.size()==4&&frames[1]["sha256"]==beforeHash&&frames[1]["format"]=="json-lines"&&frames[2]["validJsonRows"]==2&&frames[2]["firstTime"]=="first"&&frames[2]["lastTime"]=="last"&&actual==expected&&sha256File(file.wstring())==beforeHash);
    GetProcessHandleCount(GetCurrentProcess(),&afterHandles);check("all-owned-inspector-and-hash-handles-return-to-warm-count",beforeHandles==afterHandles);
    unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();std::cout<<json{{"selectedCase","jsonl-range"},{"checks",checks},{"failures",failed},{"longAllocation",longAllocation},{"richAllocation",richAllocation},{"formatEvidence",evidence},{"coldHandles",cold},{"firstHashHandles",warm1},{"handlesBefore",beforeHandles},{"handlesAfter",afterHandles},{"actualBeforeAndCurrentBodies",true},{"hardwareActions",0},{"productOrSteamActions",0}}.dump(2)<<std::endl;return failed?3:0;
#endif
}catch(const std::exception& error){allocationMeasure=false;std::cerr<<error.what()<<std::endl;return 4;}}
'''


def jsonl_range_source(before_header):
    """Actual full old/current inspectors;no stream or parser type adapter."""
    source=(ROOT/'native/main.cpp').read_text(encoding='utf-8-sig')
    header=(ROOT/'native/log_export_evidence.h').read_text(encoding='utf-8-sig')
    before=pathlib.Path(before_header).read_text(encoding='utf-8-sig')
    signature='inline EvidenceJson stagedFileEvidence(const std::filesystem::path& staged,const std::string& entry) noexcept {'
    old=extract(before,signature).replace('inline EvidenceJson stagedFileEvidence(','static EvidenceJson beforeStagedFileEvidence(',1)
    current=extract(header,signature).replace('inline EvidenceJson stagedFileEvidence(','static EvidenceJson currentStagedFileEvidence(',1)
    tests=JSONL_RANGE_TESTS.replace('// ACTUAL_BEFORE_INSPECTOR',old,1).replace('// ACTUAL_CURRENT_INSPECTOR',current,1)
    hashes=extract(source,'static std::string sha256FileCancellable(const std::wstring& path, const std::function<bool()>& cancelled) {')+'\n'+extract(source,'static std::string sha256File(const std::wstring& path) {')
    return PRELUDE+'\n#include <cstdlib>\n#include <new>\n#include <stdexcept>\n#include <iterator>\nusing namespace ymcc::logexport;\n'+hashes+'\n'+tests


FINITE_OUTCOME_GUIDE_TESTS = r'''
int wmain(int argc,wchar_t**){
 if(argc!=2)return 2;DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
 const auto guide=ymcc::logexport::aiReadingGuide();
 check("finite-outcome-guide-lists-thirteen-exact-existing-events",guide.contains("finiteNativeOutcomeReceipts")&&guide["finiteNativeOutcomeReceipts"]["schemaVersion"]==1&&guide["finiteNativeOutcomeReceipts"]["events"]==json::array({"shortcut-action-request","shortcut-action-result","msi-claw-return-debt","steam-overlay-startup","process-efficiency-startup","input-module-replay-unverified","hwinfo-recovery-result-discarded","controller-motion-raw-registration","gyro-power-early-pause-completed","gyro-power-start-current","fxsound-toggle","settings-upgrade-repaired","gamepad-feedback-stop-pending"}));
 const auto& fan=guide["fanLifecycleLogFailures"];
 check("fan-failure-guide-current-export-field",fan["field"]=="availability fanLifecycleLogFailureEvidence");
 check("fan-failure-guide-independent-bounded-counters",fan["scope"].get<std::string>().find("7 independent Fan-owned")!=std::string::npos&&fan["scope"].get<std::string>().find("95B")!=std::string::npos);
 check("fan-failure-guide-excludes-unobserved-destructor-and-argument-preparation",fan["limits"].get<std::string>().find("by-value arguments before entry,destructor close")!=std::string::npos);
 const auto& calibration=guide["calibrationRestoreEvidence"];
 check("calibration-guide-three-exact-events",calibration["events"]==json::array({"gyro-calibration-awaiting-current-source","gyro-calibration-awaiting-current-recipe","gyro-calibration-restored"}));
 check("calibration-guide-keeps-two-waits-legacy-and-recipe-distinct",calibration["states"].get<std::string>().find("source wait differs")!=std::string::npos&&calibration["states"].get<std::string>().find("legacy offset and recipe")!=std::string::npos);
 check("calibration-guide-bounded-and-not-input-proof",calibration["offlineView"].get<std::string>().find("max48 observations")!=std::string::npos&&calibration["currentSensorOrInputHealthProven"]==false&&calibration["operationsPaired"]==false);
 const auto& value=guide["finiteNativeOutcomeReceipts"];
 check("finite-outcome-guide-keeps-sensor-command-and-power-independent",value["sensorPower"].get<std::string>().find("independent")!=std::string::npos);
 check("finite-outcome-guide-does-not-upgrade-feedback-or-config-to-hardware-proof",value["feedbackStop"].get<std::string>().find("does not prove hardware stop")!=std::string::npos&&value["settingsAndFx"].get<std::string>().find("do not prove physical hardware")!=std::string::npos);
 check("finite-outcome-guide-bounds-witnesses-and-rejects-health-proof",value["offlineView"].get<std::string>().find("max80 observations")!=std::string::npos&&value["currentBusinessSuccessInferred"]==false&&value["physicalOrResourceReturnProven"]==false);
 check("finite-outcome-guide-keeps-pending-debt-and-discarded-zero-separate",value["shortcut"].get<std::string>().find("pending is unresolved")!=std::string::npos&&value["returnDebt"].get<std::string>().find("retained is unresolved")!=std::string::npos&&value["finiteStartup"].get<std::string>().find("discarded recovery code0 remains discarded")!=std::string::npos);
 check("original-guide-trust-clocks-identity-and-export-query-remain",guide.contains("instructionTrust")&&guide.contains("clockRules")&&guide.contains("identityRules")&&guide["ordinaryExportStages"]["windowsReport"].get<std::string>().find("first query retains10000ms")==0);
 GetProcessHandleCount(GetCurrentProcess(),&after);check("guide-construction-returns-owned-handles",before==after);
 unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failed},{"guide",guide},{"handlesBefore",before},{"handlesAfter",after},{"hardwareActions",0},{"productOrSteamActions",0}}.dump(2)<<'\n';return failed?3:0;
}
'''


def finite_outcomes_guide_source():
    source=(ROOT/'native/main.cpp').read_text(encoding='utf-8-sig')
    hashes=extract(source,'static std::string sha256FileCancellable(const std::wstring& path, const std::function<bool()>& cancelled) {')+'\n'+extract(source,'static std::string sha256File(const std::wstring& path) {')
    return PRELUDE+'\n'+hashes+'\n'+FINITE_OUTCOME_GUIDE_TESTS


TOUCHPAD_RECOVERY_GUIDE_TESTS=r'''
int wmain(){
 DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
 const auto guide=ymcc::logexport::aiReadingGuide();
 const auto& value=guide["touchpadRecoveryEvidence"];
 check("touchpad-guide-uses-existing-offline-summary",value["offlineView"]=="commonEvidence.touchpadRecoveryEvidence");
 check("touchpad-guide-keeps-top-level-and-recovery-layers-separate",value["layers"].get<std::string>().find("do not override")!=std::string::npos);
 check("touchpad-guide-does-not-certify-current-input-or-cause",value["currentInputHealthProven"]==false&&value["rootCauseProven"]==false);
 check("touchpad-guide-keeps-bounded-observations-and-raw",value["budget"].get<std::string>().find("max48")!=std::string::npos);
 GetProcessHandleCount(GetCurrentProcess(),&after);check("guide-owned-handles-returned",before==after);
 bool pass=true;for(const auto& row:checks)pass=pass&&row["pass"].get<bool>();std::cout<<json{{"checks",checks},{"guide",guide},{"handlesBefore",before},{"handlesAfter",after},{"hardwareActions",0},{"productOrSteamActions",0}}.dump(2)<<'\n';return pass?0:1;
}
'''

def touchpad_recovery_guide_source():
    source=(ROOT/'native/main.cpp').read_text(encoding='utf-8-sig')
    hashes=extract(source,'static std::string sha256FileCancellable(const std::wstring& path, const std::function<bool()>& cancelled) {')+'\n'+extract(source,'static std::string sha256File(const std::wstring& path) {')
    return PRELUDE+'\n'+hashes+'\n'+TOUCHPAD_RECOVERY_GUIDE_TESTS


def jsonl_sax_source(before_header,fixture_name='log_export_jsonl_sax.inc'):
    source=(ROOT/'native/main.cpp').read_text(encoding='utf-8-sig')
    header=(ROOT/'native/log_export_evidence.h').read_text(encoding='utf-8-sig')
    before=pathlib.Path(before_header).read_text(encoding='utf-8-sig')
    signature='inline EvidenceJson stagedFileEvidence(const std::filesystem::path& staged,const std::string& entry) noexcept {'
    old=extract(before,signature).replace('inline EvidenceJson stagedFileEvidence(','static EvidenceJson beforeStagedFileEvidence(',1)
    current=extract(header,signature).replace('inline EvidenceJson stagedFileEvidence(','static EvidenceJson currentStagedFileEvidence(',1)
    tests=(ROOT/'tools/fixtures'/fixture_name).read_text(encoding='utf-8-sig').replace('// ACTUAL_BEFORE_INSPECTOR',old,1).replace('// ACTUAL_CURRENT_INSPECTOR',current,1)
    hashes=extract(source,'static std::string sha256FileCancellable(const std::wstring& path, const std::function<bool()>& cancelled) {')+'\n'+extract(source,'static std::string sha256File(const std::wstring& path) {')
    return PRELUDE+'\n#include <cstdlib>\n#include <new>\n#include <stdexcept>\n#include <iterator>\nusing namespace ymcc::logexport;\n'+hashes+'\n'+tests


def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output-dir',required=True,type=pathlib.Path);parser.add_argument('--baseline',type=pathlib.Path);parser.add_argument('--cost-bundle',type=pathlib.Path,help='Optional offline ZIP central-directory replay; never runs the product')
 parser.add_argument('--case',choices=['opaque-tail','jsonl-range','jsonl-sax','finite-outcomes','touchpad-recovery']);parser.add_argument('--opaque-before-header',type=pathlib.Path);parser.add_argument('--jsonl-before-header',type=pathlib.Path)
 args=parser.parse_args()
 if args.case=='touchpad-recovery':
  if args.opaque_before_header or args.jsonl_before_header or args.baseline or args.cost_bundle:parser.error('touchpad-recovery accepts only output directory')
  return run_opaque_tail(args,touchpad_recovery_guide_source(),'touchpad-recovery')
 if args.case=='finite-outcomes':
  if args.opaque_before_header or args.jsonl_before_header or args.baseline or args.cost_bundle:parser.error('finite-outcomes accepts only output directory')
  return run_opaque_tail(args,finite_outcomes_guide_source(),'finite-outcomes')
 if args.case=='opaque-tail':
  if not args.opaque_before_header or args.jsonl_before_header or args.baseline or args.cost_bundle:parser.error('opaque-tail requires only explicit previous header and output directory')
  return run_opaque_tail(args)
 if args.case=='jsonl-sax':
  if not args.jsonl_before_header or args.opaque_before_header or args.baseline or args.cost_bundle:parser.error('jsonl-sax requires only explicit previous header and output directory')
  return run_opaque_tail(args,jsonl_sax_source(args.jsonl_before_header),'jsonl-sax')
 if args.case=='jsonl-range':
  if not args.jsonl_before_header or args.opaque_before_header or args.baseline or args.cost_bundle:parser.error('jsonl-range requires only explicit previous header and output directory')
  return run_opaque_tail(args,jsonl_range_source(args.jsonl_before_header),'jsonl-range')
 if args.opaque_before_header or args.jsonl_before_header:parser.error('previous inspector header requires its matching directed case')
 if not args.baseline:parser.error('original default format suite requires baseline')
 out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True);run=out/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));run.mkdir()
 source=(ROOT/'native/main.cpp').read_bytes();text=source.decode('utf-8-sig').replace('\r\n','\n');sig='static void fanHostNoteMsiModeWrite(unsigned long long generation) noexcept {';function=extract(text,sig)
 headers={name:(ROOT/'native'/name).read_bytes() for name in ('log_export_evidence.h','log_ai_index.h')}
 for name,b in headers.items():(run/name).write_bytes(b)
 cost_export_block=extract(text,'        {\n            std::error_code archiveEc;\n            std::vector<ymcc::logexport::HostCostArchiveExportCandidate> archiveCandidates;')
 (run/'bound-main.cpp').write_bytes(source);hash_functions=extract(text,'static std::string sha256FileCancellable(const std::wstring& path, const std::function<bool()>& cancelled) {')+'\n'+extract(text,'static std::string sha256File(const std::wstring& path) {');(run/'harness.cpp').write_text(PRELUDE+'\n'+function+'\n'+hash_functions+'\n'+TESTS.replace(' PRODUCTION_COST_EXPORT_BLOCK',cost_export_block),encoding='utf-8')
 vc=pathlib.Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat');exe=run/'harness.exe';batch=run/'build.cmd'
 sdk_root=pathlib.Path('C:/Program Files (x86)/Windows Kits/10');installed=sorted((sdk_root/'Include').glob('*'),key=lambda p:tuple(int(v) for v in p.name.split('.') if v.isdigit()),reverse=True)
 sdk=next((p for p in installed if (p/'um/Windows.h').is_file() and (p/'ucrt/crtdbg.h').is_file()),None)
 if sdk is None:raise RuntimeError('No complete Windows SDK with UCRT headers')
 sdk_args=''.join(' /I"'+str(sdk/n)+'"' for n in ('um','shared','ucrt','winrt'))
 sdk_link=' /link'+''.join(' /LIBPATH:"'+str(sdk_root/'Lib'/sdk.name/n/'x64')+'"' for n in ('um','ucrt'))
 batch.write_text('@echo off\ncall "'+str(vc)+'" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 "'+str(run/'harness.cpp')+'" /I"'+str(run)+'" /I"'+str(ROOT/'deps/json')+'"'+sdk_args+' /Fo"'+str(run/'harness.obj')+'" /Fe"'+str(exe)+'"'+sdk_link+'\n',encoding='utf-8')
 build=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,text=True,errors='replace');(run/'compile.log').write_text(build.stdout+build.stderr,encoding='utf-8')
 if build.returncode:print(build.stdout+build.stderr);return build.returncode
 if args.cost_bundle:
  fixtures=run/'fixtures';fixtures.mkdir(exist_ok=True)
  with zipfile.ZipFile(args.cost_bundle) as bundle:
   inventory={'source':str(args.cost_bundle),'sourceSha256':hashlib.sha256(args.cost_bundle.read_bytes()).hexdigest(),'archives':[{'name':i.filename,'bytes':i.file_size} for i in bundle.infolist() if re.fullmatch(r'input-host-cost\.log\.archive\.[0-9]{19}\.[0-9a-f]{24}\.zip',i.filename)]}
  (fixtures/'field-cost-archive-candidates.json').write_text(json.dumps(inventory,indent=2)+'\n',encoding='utf-8')
 result=subprocess.run([str(exe),str(run/'fixtures')],cwd=run,capture_output=True,text=True,errors='replace');(run/'result.json').write_text(result.stdout,encoding='utf-8');report=json.loads(result.stdout)
 baseline=args.baseline.read_text(encoding='utf-8-sig');before=extract(baseline,sig)
 def control_only(value):return re.sub(r'try \{ appendFanLifecycleLog\(.*?catch \(\.\.\.\) \{ \}', '<logging-statement>',value,flags=re.S)
 latest=(ROOT/'native/main.cpp').read_bytes();newtext=latest.decode('utf-8-sig').replace('\r\n','\n')
 checks={'ai-index-is-export-only':text.count('writeAiLogIndex(')==1 and 'snapshotReady && steamLogs' in text,
     'cost-volumes-and-pending-discovered':'hostCostArchiveEvidenceName(W2U(it->path().filename().wstring()))' in text and 'input-host-cost.log.pending' in text,
     'ordinary-cost-export-single-active-no-rotation':'addFile(dataDir + L"\\\\input-host-cost.log");' in text and 'addRotated(dataDir + L"\\\\input-host-cost.log");' not in text and 'addFile(dataDir + L"\\\\input-host-cost.log.pending");' not in text,
     'ordinary-cost-history-bounded-before-staging':'selectHostCostArchivesForExport(std::move(archiveCandidates))' in cost_export_block and 'includedByBudget' in cost_export_block,
     'key-signal-and-fault-files-kept-outside-cost-budget':all(v in text for v in ('addRotated(dataDir + L"\\\\focus-trace.log");','addRotated(dataDir + L"\\\\input-host-signal.log");','addRotated(dataDir + L"\\\\input-host-fatal.log");')),
     'notification-control-body-identical-to-baseline':control_only(before)==control_only(function),
     'manifest-frame-version-and-row-schema-agree':'inputFrameEvidenceVersion=2\\n' in text and 'inputFrameEvidenceFile=native-lifecycle.log(state-change),native-detail.log(periodic) event=input-host-frame-evidence formatVersion=2' in text,
     'bound-logging-function-stable':extract(newtext,sig)==function,
     'bound-export-header-stable':all((ROOT/'native'/n).read_bytes()==b for n,b in headers.items())}
 report.update(sourceChecks=checks,sourceSha256=hashlib.sha256(source).hexdigest(),wholeSourceStable=source==latest,headers={n:hashlib.sha256(b).hexdigest() for n,b in headers.items()},productionOrHardwareExecuted=False,runDirectory=str(run));report['failures']+=sum(not ok for ok in checks.values())
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps({'checks':len(report['checks']),'sourceChecks':checks,'failures':report['failures']}));return result.returncode or (3 if report['failures'] else 0)

if __name__=='__main__':raise SystemExit(main())
