"""Extract actual fan observation statements and Host identity initializers; fake log/clock only."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'));from log_frequency_selftest import extract

def main():
 p=argparse.ArgumentParser();p.add_argument('--out',required=True,type=Path);p.add_argument('--baseline-native',required=True,type=Path);args=p.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
 text=(ROOT/'native/main.cpp').read_text(encoding='utf-8-sig').replace('\r\n','\n')
 worker=extract(text,'static void fanHostEmergencyResume(unsigned long long generation) {')
 compare=worker[worker.index('                // Compare only existing observations;'):worker.index('                const auto verdict = fanResumeClassify',worker.index('// Compare only existing observations;'))]
 begin=worker.index('                // First/changed observations stay resident;')
 end=worker.index('\n            }\n            Sleep(intervalMs);',begin);observe=worker[begin:end]
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
static int loggedProbes=0,probeCount=0,constructed=0;static constexpr int kFanResumeObserveLogLimit=16;
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
static void reset(bool gate=true){fixtureLast=Probe{};lastStatus=0;generation=1;clockTick=0;lastObserveLogTick=0;loggedProbes=probeCount=constructed=0;detailGate=gate;throwEvent=throwDetail=false;eventRows.clear();detailRows.clear();}
int main(){json checks=json::array();int failed=0;auto check=[&](const char* name,bool ok){checks.push_back({{"name",name},{"pass",ok}});if(!ok)++failed;};Probe p;
 reset();note(p);check("first-observation-resident",eventRows.size()==1&&detailRows.empty()&&eventRows[0]["snapshotReason"]=="state-change");
 clockTick=500;note(p);check("unchanged-before-one-second-skips-construction",constructed==1&&loggedProbes==1);
 clockTick=1000;note(p);check("unchanged-one-second-routes-detail",eventRows.size()==1&&detailRows.size()==1&&detailRows[0]["snapshotReason"]=="periodic");
 check("original-observation-fields-preserved",detailRows[0]["generation"]==1&&detailRows[0]["probes"]==3&&detailRows[0]["phase"]=="Resuming"&&detailRows[0]["status"]==200&&detailRows[0]["unknownState"]==false&&detailRows[0]["closeCleanupPending"]==false);
 reset(false);note(p);clockTick=1000;note(p);check("detail-off-skips-json-and-sink",constructed==1&&eventRows.size()==1&&detailRows.empty());
 detailGate=true;clockTick=2000;note(p);check("detail-on-resumes-without-replaying-old-rows",detailRows.size()==1&&loggedProbes==3);
 p.opStatus="queued";clockTick=2001;note(p);check("operation-status-edge-resident-immediately",eventRows.size()==2&&eventRows.back()["hostOpStatus"]=="queued");
 p.cleanupPending=true;clockTick=2002;note(p);check("cleanup-edge-resident-immediately",eventRows.size()==3&&eventRows.back()["closeCleanupPending"]==true);
 p.unknown=true;clockTick=2003;note(p);check("unknown-edge-resident-immediately",eventRows.size()==4&&eventRows.back()["unknownState"]==true);
 clockTick=2004;note(p,503);check("http-status-edge-resident-immediately",eventRows.size()==5&&eventRows.back()["status"]==503);
 p.phase="Closing";clockTick=2005;note(p,503);check("phase-edge-resident-immediately",eventRows.size()==6);
 p.stateName="Closing";clockTick=2006;note(p,503);check("host-state-edge-resident-immediately",eventRows.size()==7);
 reset();p=Probe{};for(int i=0;i<30;++i){clockTick=static_cast<ULONGLONG>(i)*1000;note(p);}check("original-sixteen-row-cap-preserved",loggedProbes==16&&eventRows.size()==1&&detailRows.size()==15);
 reset();throwEvent=true;bool survived=true;try{note(p);}catch(...){survived=false;}check("event-diagnostic-throw-isolated",survived&&loggedProbes==1);
 throwEvent=false;throwDetail=true;clockTick=1000;try{note(p);}catch(...){survived=false;}check("detail-diagnostic-throw-isolated",survived&&loggedProbes==2);
 reset(false);for(int i=0;i<25;++i){clockTick=static_cast<ULONGLONG>(i)*1000;note(p);}check("off-path-remains-bounded-with-one-row-construction",loggedProbes==16&&constructed==1&&detailRows.empty());
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
 # Strip just the two named logging regions to compare the recovery algorithm with the saved pre-edit worker.
 before=args.baseline_native.resolve();old=extract(before.read_text(encoding='utf-8-sig').replace('\r\n','\n'),'static void fanHostEmergencyResume(unsigned long long generation) {')
 def strip_log(s,new):
  start=s.index('                // Compare only existing observations;' if new else '                lastStatus = status;\n                const std::string prevPhase')
  end=s.index('                const auto verdict = fanResumeClassify',start)
  s=s[:start]+'                lastStatus = status;\n                last = probe;\n'+s[end:]
  start=s.index('                // First/changed observations stay resident;' if new else '                // 观察日志：状态变化')
  end=s.index('\n            }\n            Sleep(intervalMs);',start)
  return s[:start]+'                /* LOGGING_ONLY */'+s[end:]
 controlStable=strip_log(worker,True)==strip_log(old,False)
 assert controlStable,'non-logging recovery body drifted; review before attributing it to this patch'
 current=extract((ROOT/'native/main.cpp').read_text(encoding='utf-8-sig').replace('\r\n','\n'),'static void fanHostEmergencyResume(unsigned long long generation) {')
 assert worker==current,'bound worker drifted during test'
 receipt={'fanChecks':len(json.loads(run.stdout)['checks']),'fanFailures':0,'workerControlBodyUnchangedOutsideLogRegions':controlStable,
  'workerSha256':hashlib.sha256(worker.encode()).hexdigest(),'sourceBindings':{'compareSha256':hashlib.sha256(compare.encode()).hexdigest(),'observeSha256':hashlib.sha256(observe.encode()).hexdigest()},'scope':'offline extracted production logging with fake boundaries'}
 (out/'result.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8');print(json.dumps(receipt));return 0
if __name__=='__main__':raise SystemExit(main())
