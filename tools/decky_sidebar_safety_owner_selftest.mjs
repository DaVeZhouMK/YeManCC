// Execute the exact owner-thread helpers from native/main.cpp. Only settings/log/runtime IO
// are inert; the production decision and ordering stay byte-for-byte source-extracted.
import fs from 'node:fs';import path from 'node:path';import {execFileSync,spawnSync} from 'node:child_process';
const deadlineRaw=process.env.YMCC_DECKY_DEADLINE;
const boundedTimeout=limit=>{
 if(deadlineRaw===undefined)return limit;
 const deadline=Number(deadlineRaw),remaining=deadline-Date.now();
 if(!deadlineRaw.trim()||!Number.isSafeInteger(deadline)||remaining<=0)throw Error('Invalid or expired YMCC_DECKY_DEADLINE');
 return Math.min(limit,remaining);
};
boundedTimeout(60000);
const root=path.resolve(import.meta.dirname,'..'),out=path.resolve(process.argv[2]||process.env.YMCC_DECKY_ARTIFACT_ROOT||'work/decky-health-owner');fs.mkdirSync(out,{recursive:true});
const nativeSourceAt=process.argv.indexOf('--native-source'),nativeFile=nativeSourceAt<0?path.join(root,'native/main.cpp'):path.resolve(process.argv[nativeSourceAt+1]);
const native=fs.readFileSync(nativeFile,'utf8'),begin=native.indexOf('static json deckySidebarSetEnabled('),end=native.indexOf('\n// One shared production consumer',begin);if(begin<0||end<0)throw Error('Review owner helper source boundary');
const productionFunction=name=>{const start=native.indexOf('static '+(name==='mouseModeBootOwnerCheck'?'std::function<bool()> ':'bool ')+name+'(');if(start<0)throw Error('missing actual owner '+name);const end=native.indexOf('\n}',start);if(end<0)throw Error('missing owner end '+name);return native.slice(start,end+2);};
const ipcStart=native.indexOf('    ipc_on("deckySidebar.setEnabled"'),ipcEnd=native.indexOf('\n    });',ipcStart);if(ipcStart<0||ipcEnd<0)throw Error('missing actual Decky IPC');
const ipc=native.slice(ipcStart,ipcEnd+'\n    });'.length);
const defaults=fs.readFileSync(path.join(root,'PowerControl/settings-defaults-v1.json'),'utf8');
const source=String.raw`
#include "decky_sidebar_health.h"
#include "boot_mode_preferences.h"
#include "settings_baseline.h"
#include <atomic>
#include <memory>
#include <functional>
#include <iostream>
#include <vector>
#include <stdexcept>
using json=nlohmann::json;
static std::vector<std::string> trace;
static bool saveOk=true,readThrows=false,saveThrows=false,logThrows=false,environmentReady=true;
static json written;
static json settings={{"extensions",{{"unrelatedExtension",42}}}};
static std::function<void()> beforeWrite,beforeEnvironment,beforeRuntimeIntent;
static int settingsLockDepth=0,resourceWrites=0,runtimePublications=0,ordinaryEnableRefunds=0,recoveryVetoes=0;
struct SettingsFileGuard {SettingsFileGuard(){if(settingsLockDepth)throw std::runtime_error("recursive settings lock");++settingsLockDepth;}~SettingsFileGuard(){--settingsLockDepth;}};
static json ymSettingsReadUnlocked(){return settings;}
static const wchar_t* YM_SETTINGS_FILE=L"owned-memory-settings";
static std::string ymSettingsReadRaw(const wchar_t*){return settings.dump();}
struct {bool isolated=false;}g_aiFanMockSession;
static std::function<json(const json&)> deckyHandler;
static void ipc_on(const char*,const std::function<json(const json&)>& handler){deckyHandler=handler;}
`+productionFunction('ymSettingsBootWriteMatches')+"\n"+productionFunction('mouseModeBootOwnerCheck')+String.raw`
struct RuntimeFixture {
 json state={{"enabled",true},{"phase","loader-started"},{"reason",""},{"revision",7},{"pid",123}};
 json snapshot(){return state;}
 bool acceptsSafetyStop(const json& event,const std::string& run){return ymcc::deckyhealth::matches(event,state,run);}
 void observeSteam(int){trace.push_back("observe");}
 void setEnabled(bool enabled,const std::string& reason={},const std::function<void(const std::function<void()>&)>& withOwner={}){if(beforeRuntimeIntent){auto action=beforeRuntimeIntent;beforeRuntimeIntent={};action();}const auto publish=[&]{++runtimePublications;trace.push_back(enabled?"enable":"close");state["enabled"]=enabled;state["phase"]=enabled?"loader-started":"disabled";state["reason"]=reason;state["pid"]=enabled?123:0;state["revision"]=state["revision"].get<int>()+1;};if(withOwner)withOwner(publish);else publish();if(settingsLockDepth)throw std::runtime_error("settings lock held across Runtime stop/join");}
}g_deckySidebarRuntime;
struct SteamFixture{int steamPresence(){return 1;} int sessionSnapshot(){return 1;}}g_steamSettingsObserver;
struct UpdateRecoveryFixture{void veto(){++recoveryVetoes;} void userEnable(int){++ordinaryEnableRefunds;}}g_deckyUpdateRecovery;
struct AutomaticRepairFixture{void endEpisode(){}}g_deckyAutomaticRepair;
struct ManualBaselineFixture{void clear(){}}g_deckyManualBaseline;
static void deckySidebarEndManagedModeWait(){}
struct BrokerFixture{json bootstrap(){return {{"runId",std::string(32,'a')}};}}g_deckyMirrorBroker;
static json deckySidebarStateJson(const json& value){return value;}
static json deckySidebarEnvironment(bool,const std::function<bool()>& mayPrepare={}){trace.push_back("environment");if(beforeEnvironment){auto action=beforeEnvironment;beforeEnvironment={};action();}if(mayPrepare&&!mayPrepare())throw std::runtime_error("environment owner expired");if(environmentReady)++resourceWrites;return {{"ready",environmentReady},{"reason","environment-check-failed"}};}
static json ymSettingsSection(const char*){trace.push_back("read");if(readThrows)throw std::runtime_error("damaged settings");return {{"unrelatedExtension",42}};}
static bool ymSettingsWriteSection(const char* section,const json& value,const std::function<bool(json&,json&)>& prepare=nullptr){
 if(beforeWrite){auto action=beforeWrite;beforeWrite={};action();}SettingsFileGuard locked;trace.push_back("read");if(readThrows)throw std::runtime_error("damaged settings");
 auto all=settings;auto draft=value;if(prepare&&!prepare(draft,all))return false;
 trace.push_back("save");if(saveThrows)throw std::runtime_error("disk exception");if(!saveOk)return false;
 written=draft;all[section]=draft;settings=std::move(all);return true;
}
static void deckySidebarDiagnostic(const char*,const json&,bool){trace.push_back("diagnostic");if(logThrows)throw std::runtime_error("logging exception");}
`+native.slice(begin,end)+"\nstatic const json bootFixtureDefaults=json::parse("+JSON.stringify(defaults)+");\n"+String.raw`
int main(){unsigned checks=0;const auto check=[&](bool yes,const char* label){if(!yes)throw std::runtime_error(label);++checks;};const auto reset=[](){trace.clear();g_deckySidebarRuntime=RuntimeFixture{};saveOk=true;readThrows=false;saveThrows=false;logThrows=false;environmentReady=true;written=json();settings={{"extensions",{{"unrelatedExtension",42}}}};beforeWrite={};beforeEnvironment={};beforeRuntimeIntent={};resourceWrites=runtimePublications=ordinaryEnableRefunds=recoveryVetoes=0;};
json ownerRows=json::array();
`+ipc+String.raw`
try{
 for(int fault=0;fault<4;++fault){reset();if(fault==1)saveOk=false;if(fault==2)readThrows=true;if(fault==3)saveThrows=true;logThrows=fault!=0;
  const auto s=deckySidebarSetEnabled(false,"decky-render-failed");check(!s.value("enabled",true)&&s.value("pid",1)==0,"settings/log failure cannot keep runtime running");check(trace.front()=="close","safety closes before any possibly failing settings/log IO");check(s.value("reason",std::string{})=="decky-render-failed","typed reason remains in state despite settings failure");
 }
 reset();deckySidebarSetEnabled(false,"decky-render-failed");check(written["unrelatedExtension"]==42,"preserve other extension members");check(written["deckySidebar"]["disabledReason"]=="decky-render-failed","successful save persists disabled reason");
 reset();saveOk=false;bool threw=false;try{deckySidebarSetEnabled(false);}catch(...){threw=true;}check(threw&&g_deckySidebarRuntime.state.value("enabled",false),"manual switch retains old state if save rejected");
 reset();deckySidebarSetEnabled(false);check(trace.front()=="read"&&written["deckySidebar"].size()==1,"normal user off follows existing save-first behavior without false safety reason");
 reset();environmentReady=false;threw=false;try{deckySidebarSetEnabled(true);}catch(...){threw=true;}check(threw&&trace.size()==1&&trace.front()=="environment","bad environment cannot save or start");
 reset();logThrows=true;try{deckySidebarAcceptSafetyStop({{"reason","decky-render-failed"},{"runId",std::string(32,'a')}});}catch(...){}check(!g_deckySidebarRuntime.state.value("enabled",true),"post-close logging exception cannot defeat accepted fault close");
 reset();deckySidebarAcceptSafetyStop({{"reason","decky-render-failed"},{"runId",std::string(32,'b')}});check(trace.empty()&&g_deckySidebarRuntime.state.value("enabled",false),"stale queued run never writes or closes");
 reset();deckySidebarSetEnabled(false,"arbitrary-secret-text");check(!written["deckySidebar"].contains("disabledReason"),"unknown reasons cannot be persisted as faults");
 for(const char* mode:{"global","xbox","steam","xbox-steam"}){
  reset();auto& boot=settings["extensions"]["bootModes"];boot={{"schema",1},{"revision",2},{"current",mode},{"global",{{"steamDecky",{{"enabled",true}}}}},{"scenes",{{"steam",{{"enabled",true}}}}}};
  const auto scenes=boot["scenes"];
  beforeWrite=[](){settings["extensions"]["peerCommittedUnderLock"]="keep";};
  deckySidebarSetEnabled(false,{},ymcc::bootPreferences::WriteOrigin::Ordinary);
  check(written["peerCommittedUnderLock"]=="keep","ordinary switch overwrote concurrent extension member");
  check(written["bootModes"]["global"]["steamDecky"]["enabled"]==false&&written["bootModes"]["revision"]==3,"ordinary switch lost same-transaction global preference");
  check(written["bootModes"]["scenes"]==scenes,"ordinary switch changed independent scenes");
  deckySidebarSetEnabled(true,{},ymcc::bootPreferences::WriteOrigin::BootMode);
  check(written["deckySidebar"]["enabled"]==true&&written["bootModes"]["global"]["steamDecky"]["enabled"]==false&&written["bootModes"]["revision"]==3,"boot restoration contaminated ordinary global preference");
  settings["extensions"]["bootModes"]["global"]["steamDecky"]["enabled"]=true;
  deckySidebarSetEnabled(false,"decky-render-failed");
  check(written["bootModes"]["global"]["steamDecky"]["enabled"]==true&&written["bootModes"]["revision"]==3,"safety stop changed ordinary global preference");
  deckySidebarSetEnabled(false,{},ymcc::bootPreferences::WriteOrigin::Ordinary);
  check(written["bootModes"]["global"]["steamDecky"]["enabled"]==false&&written["bootModes"]["revision"]==4,"explicit same-state ordinary OFF was not remembered");
 }
 reset();bool invalidOrigin=false;try{ymcc::bootPreferences::readOrigin({{"origin","unknown"}});}catch(const std::invalid_argument&){invalidOrigin=true;}
 check(invalidOrigin&&trace.empty(),"invalid origin accepted or performed an owner action");
 for(const char* mode:{"global","xbox","steam","xbox-steam"})for(const bool desired:{false,true}){
  for(const char* stage:{"dispatch","environment","settings","intent"}){
   if(!desired&&std::string(stage)=="environment")continue;
   for(const char* change:{"none","ack","unrelated","input","mode","observed","result","bad-baseline"}){
    reset();settings=bootFixtureDefaults;
    settings["input"]["revision"]=8;settings["input"]["startupObservedBoot"]="boot-A";
    settings["extensions"]["bootModes"]={{"schema",1},{"revision",3},{"current",mode},{"boot",mode},{"bootEnabled",true},{"global",{{"steamDecky",{{"enabled",false}}}}}};
    settings["extensions"]["bootModesAppliedBoot"]="boot-A";
    const json expected={{"input",settings["input"]},{"extensions",{{"bootModes",settings["extensions"]["bootModes"]},{"bootModesAppliedBoot","boot-A"}}}};
    const json originalMode=settings["extensions"]["bootModes"];
    const bool current=std::string(change)=="none"||std::string(change)=="ack"||std::string(change)=="unrelated";
    const auto mutate=[&]{const std::string kind=change;
     if(kind=="ack")settings["input"]["appliedRevision"]=8;
     if(kind=="unrelated")settings["ui"]["theme"]="preserve-peer";
     if(kind=="input")settings["input"]["revision"]=9;
     if(kind=="mode")settings["extensions"]["bootModes"]["revision"]=4;
     if(kind=="observed")settings["input"]["startupObservedBoot"]="boot-B";
     if(kind=="result")settings["extensions"]["bootModesStartupResult"]={{"peer",true}};
     if(kind=="bad-baseline")settings["schemaVersion"]=999;
    };
    if(std::string(stage)=="dispatch")mutate();
    if(std::string(stage)=="environment")beforeEnvironment=mutate;
    if(std::string(stage)=="settings")beforeWrite=mutate;
    if(std::string(stage)=="intent")beforeRuntimeIntent=mutate;
    bool threw=false;json result;
    try{result=deckyHandler({{"enabled",desired},{"origin","boot-mode"},{"bootContext",expected}});}catch(const std::exception&){threw=true;}
    check(current?!threw:threw,"boot context must be accepted only while its real native owner is current");
    check(settingsLockDepth==0,"native owner settings lock leaked");
    if(current){
     check(result.value("enabled",!desired)==desired,"current boot runtime intent not applied");
     check(settings["extensions"]["deckySidebar"]["enabled"]==desired,"current boot preference not saved");
     check(settings["extensions"]["bootModes"]==originalMode,"boot write overwrote global or independent mode bank");
     check(runtimePublications==1,"current boot intent published more than once");
    }else{
     check(runtimePublications==0,"expired boot intent reached native Runtime");
     check(ordinaryEnableRefunds==0&&recoveryVetoes==0,"expired boot intent changed recovery ownership");
     if(std::string(stage)!="intent")check(written.is_null(),"expired boot intent saved a preference");
     if(std::string(stage)=="environment")check(resourceWrites==0,"cancelled environment request performed a resource write");
    }
    ownerRows.push_back({{"mode",mode},{"enabled",desired},{"stage",stage},{"change",change},{"accepted",current}});
   }
  }
 }
 reset();bool missingContext=false;try{deckyHandler({{"enabled",true},{"origin","boot-mode"}});}catch(...){missingContext=true;}
 check(missingContext&&trace.empty(),"boot-origin command without context escaped admission");
 for(const char* origin:{"ordinary","runtime"}){reset();bool badOrigin=false;try{deckyHandler({{"enabled",false},{"origin",origin},{"bootContext",json::object()}});}catch(...){badOrigin=true;}check(badOrigin&&trace.empty(),"foreign origin carried a boot context");}
 std::cout<<"DECKY_BOOT_OWNER_ROWS "<<ownerRows.dump()<<'\n';

 std::cout<<"DECKY_SAFETY_OWNER_OK checks="<<checks<<" sourceExtracted=true realSettingsWrites=0 realSteamActions=0\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
`;
const cpp=path.join(out,'owner-safety-test.cpp'),exe=path.join(out,'owner-safety-test.exe');fs.writeFileSync(cpp,source);
const compileArgs=['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/DNOMINMAX','/sourceDependencies',path.join(out,'source-dependencies.json'),'/I'+path.join(root,'deps/json'),'/I'+path.join(root,'native'),cpp,'/Fo'+path.join(out,'owner-safety-test.obj'),'/Fe'+exe],compileBegan=Date.now(),compileLimit=boundedTimeout(60000);
const compiled=spawnSync('cl.exe',compileArgs,{encoding:'utf8',windowsHide:true,timeout:compileLimit,maxBuffer:16777216});
for(const stream of ['stdout','stderr'])fs.writeFileSync(path.join(out,'compile.'+stream+'.raw'),compiled[stream]??'',{flag:'wx'});
fs.writeFileSync(path.join(out,'compile.process.json'),JSON.stringify({program:'cl.exe',args:compileArgs,elapsedMs:Date.now()-compileBegan,timeoutMs:compileLimit,exitCode:compiled.status,signal:compiled.signal,error:compiled.error?.message??null,etimedout:compiled.error?.code==='ETIMEDOUT'},null,2)+'\n',{flag:'wx'});
if(compiled.error||compiled.signal||compiled.status!==0)throw Error('Decky owner compiler incomplete: '+(compiled.stderr??'')+(compiled.stdout??''));
const result=execFileSync(exe,[],{encoding:'utf8',windowsHide:true,timeout:boundedTimeout(15000)});fs.writeFileSync(path.join(out,'native.stdout.raw'),result,{flag:'wx'});process.stdout.write(result);const ownerRows=JSON.parse(result.split('DECKY_BOOT_OWNER_ROWS ')[1].split('\n')[0]);if(ownerRows.length!==224||new Set(ownerRows.map(r=>[r.mode,r.enabled,r.stage,r.change].join('|'))).size!==224)throw Error('actual native Decky selection differs');fs.writeFileSync(path.join(out,'BOOT-OWNER-ROWS.json'),JSON.stringify(ownerRows,null,2),{flag:'wx'});fs.writeFileSync(path.join(out,'SAFETY-OWNER.json'),JSON.stringify({passed:true,checks:Number(/checks=(\d+)/.exec(result)[1]),productionSourceExtracted:true,settingsWrites:0,realSteamActions:0},null,2));
