// Execute the exact owner-thread helpers from native/main.cpp. Only settings/log/runtime IO
// are inert; the production decision and ordering stay byte-for-byte source-extracted.
import fs from 'node:fs';import path from 'node:path';import {execFileSync} from 'node:child_process';
const root=path.resolve(import.meta.dirname,'..'),out=path.resolve(process.argv[2]||process.env.YMCC_DECKY_ARTIFACT_ROOT||'work/decky-health-owner');fs.mkdirSync(out,{recursive:true});
const native=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8'),begin=native.indexOf('static json deckySidebarSetEnabled('),end=native.indexOf('\n// One shared production consumer',begin);if(begin<0||end<0)throw Error('Review owner helper source boundary');
const source=String.raw`
#include "decky_sidebar_health.h"
#include <iostream>
#include <vector>
#include <stdexcept>
using json=nlohmann::json;
static std::vector<std::string> trace;
static bool saveOk=true,readThrows=false,saveThrows=false,logThrows=false,environmentReady=true;
static json written;
struct RuntimeFixture {
 json state={{"enabled",true},{"phase","loader-started"},{"reason",""},{"revision",7},{"pid",123}};
 json snapshot(){return state;}
 bool acceptsSafetyStop(const json& event,const std::string& run){return ymcc::deckyhealth::matches(event,state,run);}
 void observeSteam(int){trace.push_back("observe");}
 void setEnabled(bool enabled,const std::string& reason={}){trace.push_back(enabled?"enable":"close");state["enabled"]=enabled;state["phase"]=enabled?"loader-started":"disabled";state["reason"]=reason;state["pid"]=enabled?123:0;state["revision"]=state["revision"].get<int>()+1;}
}g_deckySidebarRuntime;
struct SteamFixture{int steamPresence(){return 1;} int sessionSnapshot(){return 1;}}g_steamSettingsObserver;
struct UpdateRecoveryFixture{void veto(){} void userEnable(int){}}g_deckyUpdateRecovery;
struct BrokerFixture{json bootstrap(){return {{"runId",std::string(32,'a')}};}}g_deckyMirrorBroker;
static json deckySidebarStateJson(const json& value){return value;}
static json deckySidebarEnvironment(bool){trace.push_back("environment");return {{"ready",environmentReady},{"reason","environment-check-failed"}};}
static json ymSettingsSection(const char*){trace.push_back("read");if(readThrows)throw std::runtime_error("damaged settings");return {{"unrelatedExtension",42}};}
static bool ymSettingsWriteSection(const char*,const json& value){trace.push_back("save");if(saveThrows)throw std::runtime_error("disk exception");written=value;return saveOk;}
static void deckySidebarDiagnostic(const char*,const json&,bool){trace.push_back("diagnostic");if(logThrows)throw std::runtime_error("logging exception");}
`+native.slice(begin,end)+String.raw`
int main(){unsigned checks=0;const auto check=[&](bool yes,const char* label){if(!yes)throw std::runtime_error(label);++checks;};const auto reset=[](){trace.clear();g_deckySidebarRuntime=RuntimeFixture{};saveOk=true;readThrows=false;saveThrows=false;logThrows=false;environmentReady=true;written=json();};
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
 std::cout<<"DECKY_SAFETY_OWNER_OK checks="<<checks<<" sourceExtracted=true realSettingsWrites=0 realSteamActions=0\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
`;
const cpp=path.join(out,'owner-safety-test.cpp'),exe=path.join(out,'owner-safety-test.exe');fs.writeFileSync(cpp,source);
execFileSync('cl.exe',['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/DNOMINMAX','/I'+path.join(root,'deps/json'),'/I'+path.join(root,'native'),cpp,'/Fo'+path.join(out,'owner-safety-test.obj'),'/Fe'+exe],{stdio:'pipe',windowsHide:true});
const result=execFileSync(exe,[],{encoding:'utf8',windowsHide:true});process.stdout.write(result);fs.writeFileSync(path.join(out,'SAFETY-OWNER.json'),JSON.stringify({passed:true,checks:Number(/checks=(\d+)/.exec(result)[1]),productionSourceExtracted:true,settingsWrites:0,realSteamActions:0},null,2));
