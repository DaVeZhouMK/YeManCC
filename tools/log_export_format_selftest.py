"""Offline export-format and event-meaning regression. No product/hardware execution."""
from __future__ import annotations
import argparse,datetime,hashlib,json,pathlib,re,subprocess
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
static json checks=json::array(),rows=json::array();
static void check(const std::string& name,bool ok){checks.push_back({{"name",name},{"pass",ok}});}
static std::atomic<bool> g_fanLeaseKeepaliveArmed{true},g_exitRequested{false};
static std::atomic<unsigned long long> g_fanMsiModePowerGeneration{0},g_fanMsiModeRevision{0};
static bool allowed=true;
static unsigned long long currentPowerGeneration(){return 1;}
static unsigned long long currentFanPowerGeneration(){return 3;}
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
 check("ai-guide-warns-about-untrusted-input-and-inspection-scope",guide.contains("instructionTrust")&&guide.contains("inspection")&&guide.contains("clockRules")&&guide.contains("identityRules")&&guide.contains("sleepFacts"));
 unsigned failures=0;for(const auto& item:checks)if(!item["pass"].get<bool>())++failures;
 std::cout<<json({{"checks",checks},{"failures",failures}}).dump(2)<<std::endl;return failures?3:0;
}
'''

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output-dir',required=True,type=pathlib.Path);parser.add_argument('--baseline',required=True,type=pathlib.Path);args=parser.parse_args()
 out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True);run=out/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));run.mkdir()
 source=(ROOT/'native/main.cpp').read_bytes();text=source.decode('utf-8-sig').replace('\r\n','\n');sig='static void fanHostNoteMsiModeWrite(unsigned long long generation) noexcept {';function=extract(text,sig)
 headers={name:(ROOT/'native'/name).read_bytes() for name in ('log_export_evidence.h','log_ai_index.h')}
 for name,b in headers.items():(run/name).write_bytes(b)
 (run/'bound-main.cpp').write_bytes(source);hash_functions=extract(text,'static std::string sha256FileCancellable(const std::wstring& path, const std::function<bool()>& cancelled) {')+'\n'+extract(text,'static std::string sha256File(const std::wstring& path) {');(run/'harness.cpp').write_text(PRELUDE+'\n'+function+'\n'+hash_functions+'\n'+TESTS,encoding='utf-8')
 vc=pathlib.Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat');exe=run/'harness.exe';batch=run/'build.cmd'
 batch.write_text('@echo off\ncall "'+str(vc)+'" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 "'+str(run/'harness.cpp')+'" /I"'+str(run)+'" /I"'+str(ROOT/'deps/json')+'" /Fo"'+str(run/'harness.obj')+'" /Fe"'+str(exe)+'"\n',encoding='utf-8')
 build=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,text=True,errors='replace');(run/'compile.log').write_text(build.stdout+build.stderr,encoding='utf-8')
 if build.returncode:print(build.stdout+build.stderr);return build.returncode
 result=subprocess.run([str(exe),str(run/'fixtures')],cwd=run,capture_output=True,text=True,errors='replace');(run/'result.json').write_text(result.stdout,encoding='utf-8');report=json.loads(result.stdout)
 baseline=args.baseline.read_text(encoding='utf-8-sig');before=extract(baseline,sig)
 def control_only(value):return re.sub(r'try \{ appendFanLifecycleLog\(.*?catch \(\.\.\.\) \{ \}', '<logging-statement>',value,flags=re.S)
 latest=(ROOT/'native/main.cpp').read_bytes();newtext=latest.decode('utf-8-sig').replace('\r\n','\n')
 checks={'ai-index-is-export-only':text.count('writeAiLogIndex(')==1 and 'snapshotReady && steamLogs' in text,
     'cost-volumes-and-pending-discovered':'hostCostArchiveEvidenceName(W2U(it->path().filename().wstring()))' in text and 'input-host-cost.log.pending' in text,
     'notification-control-body-identical-to-baseline':control_only(before)==control_only(function),
     'manifest-frame-version-and-row-schema-agree':'inputFrameEvidenceVersion=2\\n' in text and 'inputFrameEvidenceFile=native-lifecycle.log(state-change),native-detail.log(periodic) event=input-host-frame-evidence formatVersion=2' in text,
     'bound-logging-function-stable':extract(newtext,sig)==function,
     'bound-export-header-stable':all((ROOT/'native'/n).read_bytes()==b for n,b in headers.items())}
 report.update(sourceChecks=checks,sourceSha256=hashlib.sha256(source).hexdigest(),wholeSourceStable=source==latest,headers={n:hashlib.sha256(b).hexdigest() for n,b in headers.items()},productionOrHardwareExecuted=False,runDirectory=str(run));report['failures']+=sum(not ok for ok in checks.values())
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps({'checks':len(report['checks']),'sourceChecks':checks,'failures':report['failures']}));return result.returncode or (3 if report['failures'] else 0)

if __name__=='__main__':raise SystemExit(main())
