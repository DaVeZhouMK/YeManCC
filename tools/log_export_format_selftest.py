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
 for(const char* name:{"native-lifecycle.log.12","native-detail.log.previous","input-capture.jsonl.prev",
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
 check("cost-export-default-budget-8MiB",HostCostArchiveExportMaxBytes==8ULL*1024*1024);
 check("cost-export-newest-complete-volumes-fit-exact-budget",bounded["selectedSourceBytes"]==6&&bounded["selectedVolumes"]==2&&bounded["omittedVolumes"]==1&&bounded["archives"][0]["name"]==costName('3')&&bounded["archives"][1]["name"]==costName('2')&&bounded["archives"][2]["includedByBudget"]==false);
 check("cost-export-inventory-explicitly-retains-gap",bounded["allDiscoveredHistorySelected"]==false&&bounded["sourceHistoryModified"]==false&&bounded["archives"][2]["reason"]=="ordinary-export-cost-history-byte-budget");
 const auto zero=selectHostCostArchivesForExport(costFiles,0);
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
 fs::resize_file(costDir/fs::path(costName('2')),HostCostArchiveExportMaxBytes);
 {std::ofstream f(costDir/L"unowned.zip",std::ios::binary);f<<"ignored";}
 {std::ofstream f(costDir/fs::path(costName('1')+".tmp"),std::ios::binary);f<<"uncommitted";}
 // Execute the actual ordinary-export discovery/selection block on owned dummy files.
 std::wstring dataDir=costDir.wstring();json availability;std::vector<std::wstring> selectedPaths;
 auto addFile=[&](const std::wstring& value){selectedPaths.push_back(value);};
 PRODUCTION_COST_EXPORT_BLOCK
 const auto& discovery=availability["costArchiveDiscovery"];
 check("actual-export-block-discovers-only-committed-owned-volumes",discovery["archivesFound"]==2&&selectedPaths.size()==1&&discovery["status"]=="complete-at-discovery");
 check("actual-export-block-uses-bounded-selection",discovery["exportSelection"]["budgetBytes"]==HostCostArchiveExportMaxBytes&&discovery["exportSelection"]["selectedSourceBytes"]==6&&discovery["exportSelection"]["omittedVolumes"]==1);
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
 check("cost-export-inventory-guide-points-to-actual-export-receipt",guide["retention"]["costArchiveExportInventory"].get<std::string>().find("log-export-availability.json costArchiveDiscovery.exportSelection")==0);
 check("ai-guide-warns-about-untrusted-input-and-inspection-scope",guide.contains("instructionTrust")&&guide.contains("inspection")&&guide.contains("clockRules")&&guide.contains("identityRules")&&guide.contains("sleepFacts"));
 unsigned failures=0;for(const auto& item:checks)if(!item["pass"].get<bool>())++failures;
 std::cout<<json({{"checks",checks},{"failures",failures}}).dump(2)<<std::endl;return failures?3:0;
}
'''

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output-dir',required=True,type=pathlib.Path);parser.add_argument('--baseline',required=True,type=pathlib.Path);parser.add_argument('--cost-bundle',type=pathlib.Path,help='Optional offline ZIP central-directory replay; never runs the product');args=parser.parse_args()
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
     'ordinary-cost-history-bounded-before-staging':'selectHostCostArchivesForExport(std::move(archiveCandidates))' in cost_export_block and 'includedByBudget' in cost_export_block,
     'key-signal-and-fault-files-kept-outside-cost-budget':all(v in text for v in ('addRotated(dataDir + L"\\\\focus-trace.log");','addRotated(dataDir + L"\\\\input-host-signal.log");','addRotated(dataDir + L"\\\\input-host-fatal.log");')),
     'notification-control-body-identical-to-baseline':control_only(before)==control_only(function),
     'manifest-frame-version-and-row-schema-agree':'inputFrameEvidenceVersion=2\\n' in text and 'inputFrameEvidenceFile=native-lifecycle.log(state-change),native-detail.log(periodic) event=input-host-frame-evidence formatVersion=2' in text,
     'bound-logging-function-stable':extract(newtext,sig)==function,
     'bound-export-header-stable':all((ROOT/'native'/n).read_bytes()==b for n,b in headers.items())}
 report.update(sourceChecks=checks,sourceSha256=hashlib.sha256(source).hexdigest(),wholeSourceStable=source==latest,headers={n:hashlib.sha256(b).hexdigest() for n,b in headers.items()},productionOrHardwareExecuted=False,runDirectory=str(run));report['failures']+=sum(not ok for ok in checks.values())
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps({'checks':len(report['checks']),'sourceChecks':checks,'failures':report['failures']}));return result.returncode or (3 if report['failures'] else 0)

if __name__=='__main__':raise SystemExit(main())
