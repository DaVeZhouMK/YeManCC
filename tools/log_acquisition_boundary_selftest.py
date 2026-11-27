"""Extract actual fan observation statements and Host identity initializers; fake log/clock only."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'));from log_frequency_selftest import extract

def main():
 p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=Path);p.add_argument('--baseline-native',required=True,type=Path);p.add_argument('--native',type=Path,default=ROOT/'native/main.cpp');args=p.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
 text=args.native.read_text(encoding='utf-8-sig')
 signature='static void fanHostEmergencyResume(unsigned long long generation, ymcc::fan_resume::RequestTicket ticket) {'
 worker=extract(text,signature)
 def observe_end(s,begin):
  return s.index('\n            }\n            const auto remaining=',begin)
 compare=worker[worker.index('                // Compare only existing observations;'):worker.index('                const auto verdict = fanResumeClassify',worker.index('// Compare only existing observations;'))]
 begin=worker.index('                // First/changed observations stay resident;')
 observe=worker[begin:observe_end(worker,begin)]
 limits='\n'.join(re.findall(r'^static constexpr (?:int|unsigned) kFanResume(?:Detail)?ObserveLogLimit = \d+;$',text,re.M))
 assert 'kFanResumeObserveLogLimit' in limits
 if 'kFanResumeDetailObserveLogLimit' not in limits: limits+='\nstatic constexpr unsigned kFanResumeDetailObserveLogLimit = 15;'
 (out/'limits.inc').write_text(limits,encoding='ascii')
 (out/'compare.inc').write_text(compare,encoding='utf-8');(out/'observe.inc').write_text(observe,encoding='utf-8')
 cpp=r'''
#include <string>
#include <vector>
#include <iostream>
#include <stdexcept>
#include "json.hpp"
using json=nlohmann::json;
using ULONGLONG=unsigned long long;using DWORD=unsigned long;
struct Probe {std::string phase="Resuming",stateName="Resuming",opStatus="executing";bool unknown=false,cleanupPending=false;};
static Probe fixtureLast;static DWORD lastStatus=0;static unsigned long long generation=1,clockTick=0,lastObserveLogTick=0;
static int loggedProbes=0,probeCount=0,constructed=0;static unsigned detailedProbeLogs=0;
#include "limits.inc"
static bool detailGate=true,throwEvent=false,throwDetail=false;static std::vector<json> eventRows,detailRows;
static ULONGLONG GetTickCount64(){return clockTick;}
static bool domainLogEnabled(const char*){return detailGate;}
static long long elapsedMs(){++constructed;return static_cast<long long>(clockTick);}
static void appendFanLifecycleLog(const char* e,json r){if(throwEvent)throw std::runtime_error("event sink");r["event"]=e;eventRows.push_back(std::move(r));}
static void appendDomainLog(const char*,const char* e,json r){if(throwDetail)throw std::runtime_error("detail sink");r["event"]=e;detailRows.push_back(std::move(r));}
static void note(const Probe& probe,DWORD status=200){++probeCount;Probe& last=fixtureLast;
#include "compare.inc"
#include "observe.inc"
}
static void reset(bool gate=true){fixtureLast=Probe{};lastStatus=0;generation=1;clockTick=0;lastObserveLogTick=0;loggedProbes=probeCount=constructed=0;detailedProbeLogs=0;detailGate=gate;throwEvent=throwDetail=false;eventRows.clear();detailRows.clear();}
int main(){json checks=json::array();int failed=0;auto check=[&](const char* name,bool ok){checks.push_back({{"name",name},{"pass",ok}});if(!ok)++failed;};Probe p;
 reset();note(p);check("first-observation-resident",eventRows.size()==1&&detailRows.empty()&&eventRows[0]["snapshotReason"]=="state-change");
 clockTick=500;note(p);check("unchanged-before-one-second-skips-construction",constructed==1&&loggedProbes==1);
 clockTick=1000;note(p);check("unchanged-one-second-routes-detail",eventRows.size()==1&&detailRows.size()==1&&detailRows[0]["snapshotReason"]=="periodic");
 check("original-observation-fields-preserved",detailRows[0]["generation"]==1&&detailRows[0]["probes"]==3&&detailRows[0]["phase"]=="Resuming"&&detailRows[0]["status"]==200&&detailRows[0]["unknownState"]==false&&detailRows[0]["closeCleanupPending"]==false);
 reset(false);note(p);clockTick=1000;note(p);check("detail-off-skips-json-and-sink",constructed==1&&eventRows.size()==1&&detailRows.empty());
 detailGate=true;clockTick=2000;note(p);check("detail-on-resumes-without-replaying-old-rows",detailRows.size()==1&&loggedProbes==1&&detailedProbeLogs==1);
 p.opStatus="queued";clockTick=2001;note(p);check("operation-status-edge-resident-immediately",eventRows.size()==2&&eventRows.back()["hostOpStatus"]=="queued");
 p.cleanupPending=true;clockTick=2002;note(p);check("cleanup-edge-resident-immediately",eventRows.size()==3&&eventRows.back()["closeCleanupPending"]==true);
 p.unknown=true;clockTick=2003;note(p);check("unknown-edge-resident-immediately",eventRows.size()==4&&eventRows.back()["unknownState"]==true);
 clockTick=2004;note(p,503);check("http-status-edge-resident-immediately",eventRows.size()==5&&eventRows.back()["status"]==503);
 p.phase="Closing";clockTick=2005;note(p,503);check("phase-edge-resident-immediately",eventRows.size()==6);
 p.stateName="Closing";clockTick=2006;note(p,503);check("host-state-edge-resident-immediately",eventRows.size()==7);
 reset();p=Probe{};for(int i=0;i<30;++i){clockTick=static_cast<ULONGLONG>(i)*1000;note(p);}check("stable-path-keeps-original-detailed-cap",loggedProbes==1&&eventRows.size()==1&&detailRows.size()==kFanResumeDetailObserveLogLimit&&detailedProbeLogs==kFanResumeDetailObserveLogLimit);
 p.phase="Closing";note(p);check("full-detailed-quota-cannot-hide-next-edge",eventRows.size()==2&&loggedProbes==2&&eventRows.back()["phase"]=="Closing");
 reset();throwEvent=true;bool survived=true;try{note(p);}catch(...){survived=false;}check("event-diagnostic-throw-isolated",survived&&loggedProbes==1);
 throwEvent=false;throwDetail=true;clockTick=1000;try{note(p);}catch(...){survived=false;}check("detail-diagnostic-throw-isolated",survived&&loggedProbes==1&&detailedProbeLogs==1);
 reset(false);p=Probe{};for(int i=0;i<14;++i){clockTick=static_cast<ULONGLONG>(i)*1000;note(p);}
 check("off-path-does-not-spend-row-budget-or-construct-detail",loggedProbes==1&&detailedProbeLogs==0&&constructed==1&&detailRows.empty());
 for(int i=0;i<20;++i){clockTick=13001+static_cast<ULONGLONG>(i);p.cleanupPending=!p.cleanupPending;note(p);}
 check("off-path-keeps-sixteen-state-events-within-original-request-deadline",eventRows.size()==static_cast<unsigned>(kFanResumeObserveLogLimit)&&eventRows.back()["elapsedMs"]==13015&&detailRows.empty());
 std::cout<<json({{"checks",checks},{"failures",failed},{"scope","source-extracted logging only; fake probe values/clock/sinks; no device or product"}}).dump(2)<<'\n';return failed?1:0;
}
'''
 (out/'fan-observation.cpp').write_text(cpp,encoding='utf-8')
 cmd='@echo off\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\nif errorlevel 1 exit /b 1\ncl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /MT /I"'+str(ROOT/'deps/json')+'" /I"'+str(out)+'" "'+str(out/'fan-observation.cpp')+'" /Fo"'+str(out/'fan-observation.obj')+'" /Fe"'+str(out/'fan-observation.exe')+'"\n'
 (out/'compile.cmd').write_text(cmd,encoding='ascii');compiled=subprocess.run(['cmd.exe','/d','/c',str(out/'compile.cmd')],capture_output=True)
 (out/'compile.log').write_bytes(compiled.stdout+compiled.stderr)
 if compiled.returncode:print((compiled.stdout+compiled.stderr).decode('utf-8',errors='replace'));return compiled.returncode
 run=subprocess.run([str(out/'fan-observation.exe')],capture_output=True,text=True,encoding='utf-8');(out/'fan-result.json').write_text(run.stdout,encoding='utf-8')
 if run.returncode:print(run.stdout,run.stderr);return run.returncode
 # The compare block and all worker control are exact after removing only the new log budget state.
 old=extract(args.baseline_native.read_text(encoding='utf-8-sig'),signature)
 def strip_log(s):
  start=s.index('                // First/changed observations stay resident;')
  s=s[:start]+'                /* LOGGING_ONLY */'+s[observe_end(s,start):]
  return (s.replace('    unsigned detailedProbeLogs = retained->observations.detailedProbeLogs;\n','')
           .replace('        unsigned& detailLogs;\n','').replace('lastLog,detailLogs}', 'lastLog}')
           .replace('reachableProof,detailedProbeLogs};','reachableProof};'))
 controlStable=strip_log(worker)==strip_log(old)
 assert controlStable,'non-logging recovery body drifted; review before attributing it to this patch'
 current=extract(args.native.read_text(encoding='utf-8-sig'),signature)
 assert worker==current,'bound worker drifted during test'
 receipt={'fanChecks':len(json.loads(run.stdout)['checks']),'fanFailures':0,'workerControlBodyUnchangedOutsideLogRegions':controlStable,
  'workerSha256':hashlib.sha256(worker.encode()).hexdigest(),'sourceBindings':{'compareSha256':hashlib.sha256(compare.encode()).hexdigest(),'observeSha256':hashlib.sha256(observe.encode()).hexdigest()},'scope':'offline extracted production logging with fake boundaries'}
 (out/'result.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8');print(json.dumps(receipt));return 0
if __name__=='__main__':raise SystemExit(main())
