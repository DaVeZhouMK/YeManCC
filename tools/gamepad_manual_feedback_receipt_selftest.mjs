// Actual manual vibration IPC and LED receipt closures; all writers are memory.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');if(!process.argv[2])throw Error('Output directory required');const out=path.resolve(process.argv[2]);fs.mkdirSync(out,{recursive:true});const main=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8');
const start=main.indexOf('    ipc_on("gamepad.vibrate",');const end=main.indexOf('\n    });',start)+8;if(start<0||end<start)throw Error('Missing manual vibration handler');const handler=main.slice(start,end);
const ledStart=main.indexOf('    ipc_on("gamepad.setLed",'),ls=main.indexOf('        const auto receipt =',ledStart),le=main.indexOf('\n        };',ls)+11;if(ls<0||le<ls)throw Error('Missing LED receipt');const led=main.slice(ls,le);
const cpp='#include '+JSON.stringify(path.join(repo,'native/oem_lighting_operations.h').replaceAll('\\','/'))+'\n'+String.raw`#include <string>
#include <vector>
#include <iostream>
#include <stdexcept>
#include "json.hpp"
using json=nlohmann::json;
struct {std::string deviceClass;int family=0;}g_machineIdentity;
struct {int auraControl=-1;unsigned auraFeatureLen=64;}g_rog;
constexpr int INVALID_HANDLE_VALUE=-1;
static std::string ymccFamilyName(int family){return family==1?"msi-claw":family==2?"asus-rog-ally-x":"gpd";}
static std::vector<json> logs;static json pulseResponse;static int pulseCalls=0,seenStrength=0,seenDuration=0;
static void appendNativeLifecycleLog(const char* name,const json& body){logs.push_back(json{{"event",name},{"body",body}});}
static json nativeGamepadFeedbackPulse(int strength,int duration){++pulseCalls;seenStrength=strength;seenDuration=duration;return pulseResponse;}
static json invokeVibrate(const json& a){auto ipc_on=[](const char*,auto callback){return callback;};auto callback=
`+handler+String.raw`
;return callback(a);}
static json invokeLedReceipt(const json& a,const json& body){
 const std::string capabilityId="hc.device.fixture";
 const bool enabled=a.value("enabled",true);const std::string mode=a.value("mode",std::string{"solid"}),presetId=a.value("presetId",std::string{});int speed=65,brightness=100;
`+led+String.raw`
 return receipt(body);}
int main(){unsigned checks=0;auto check=[&](bool ok,const char* n){++checks;if(!ok)throw std::runtime_error(n);};try{
 const std::vector<json> responses={
  {{"ok",true},{"backend","msi-dinput-hid"},{"attempted",true},{"written",2},{"strength",70},{"durationMs",125},{"fieldRumbleVerified",false}},
  {{"ok",false},{"backend","msi-dinput-hid"},{"attempted",false},{"written",0},{"reason","source-changed"}},
  {{"ok",false},{"backend","msi-dinput-hid"},{"attempted",true},{"written",1},{"reason","write-failed"}},
  {{"ok",true},{"backend","xinput-physical"},{"attempted",true},{"rc",0},{"slot",2}},
  {{"ok",false},{"backend","xinput-physical"},{"attempted",false},{"reason","no-xinput-physical-source"}},
  {{"ok",false},{"backend","none"},{"attempted",false},{"reason","power-transition"}},
  {{"ok",false},{"backend","xinput-physical"},{"attempted",true},{"reason","stop-unavailable"}}
 };
 for(int family:{1,2,3})for(const auto& response:responses)for(bool defaults:{false,true}){
  g_machineIdentity={family==1?"HandheldCompanion.Devices.ClawCG3EM":family==2?"HandheldCompanion.Devices.XboxROGAllyX":"HandheldCompanion.Devices.GPDWin5",family};pulseResponse=response;const auto count=logs.size();const int called=pulseCalls;
  const json args=defaults?json::object():json{{"strength",137},{"durationMs",2}};const auto result=invokeVibrate(args);
  check(result==response,"IPC response unchanged for every backend/failure");check(pulseCalls==called+1,"only one backend invocation");check(seenStrength==(defaults?70:137)&&seenDuration==(defaults?125:2),"original pulse argument/default contract retained");check(logs.size()==count+1,"exactly one request receipt on all returns");
  const auto& log=logs.back();check(log.at("event")=="gamepad-vibrate-request","request event independent from low-level XInput pulse event");const auto& body=log.at("body");
  for(auto it=response.begin();it!=response.end();++it)check(body.at(it.key())==it.value(),"actual backend result preserved in log");
  check(body.at("deviceClass")==g_machineIdentity.deviceClass&&body.at("family")==ymccFamilyName(family),"manual receipt bound to actual model identity");check(body.at("physicalEffectVerified")==false&&body.at("proofScope")=="request-and-backend-receipt","write receipt cannot claim physical effect");check(body.at("requestedStrength")==seenStrength&&body.at("requestedDurationMs")==seenDuration,"requested vs returned clamped values distinguishable");
 }
 for(bool enabled:{false,true})for(const auto& result:std::vector<json>{{{"ok",false},{"reason","power-transition"}},{{"ok",false},{"reason","invalid-color"}},{{"ok",true},{"effectApplied",true},{"brightnessApplied",true},{"backend","msi-hid"}}}){
  const json a={{"enabled",enabled},{"mode","ambilight"},{"color","#ff0000"},{"color2","#0000ff"}};const auto n=logs.size();const auto returned=invokeLedReceipt(a,result);auto expected=result;expected["capabilityId"]=result.value("capabilityId",std::string{"hc.device.fixture"});expected["requestedCapabilityId"]="hc.device.fixture";expected["capabilityCatalogSha256"]=ymcc::lighting::kCatalogSha256;expected["physicalEffectVerified"]=false;check(returned==expected,"LED response retains outcome and adds unified identity/proof metadata");check(logs.size()==n+1,"one LED receipt including early failure");const auto& b=logs.back().at("body");check(b.at("requestedColor")=="#ff0000"&&b.at("requestedColor2")=="#0000ff","both requested colors are available for physical side evidence");check(b.at("physicalEffectVerified")==false,"LED software write does not claim physical effect");check(b.at("schema")==2&&b.at("deviceClass")==g_machineIdentity.deviceClass,"LED receipt version and family identity");
 }
 const json helper={{"queuedWriteManaged",true},{"queuedWritesCompleted",true},{"queuedWriteTasks",3},{"queuedWriteFailures",1}};
 auto actual=invokeLedReceipt(json{{"mode","solid"}},json{{"ok",true},{"capabilityId","hc.device.LokiMax6800U"},{"requestedCapabilityId","hc.device.AynLoki"}});check(actual.at("capabilityId")=="hc.device.LokiMax6800U"&&logs.back().at("body").at("capabilityId")=="hc.device.LokiMax6800U","validated concrete helper identity survives outer receipt and log");
 invokeLedReceipt(json{{"mode","preset"},{"presetId","LEDPreset_OneXPlayerX1_Preset09"}},json{{"ok",false},{"reason","light-setter-exit-1"},{"note","onex-write-queue-failed"},{"helperReceipt",helper}});
 check(logs.back().at("body").at("helperReceipt")==helper&&logs.back().at("body").at("backendNote")=="onex-write-queue-failed","backend queue failure evidence survives ordinary exported LED receipt");
 check(logs.back().at("body").at("physicalEffectVerified")==false&&logs.back().at("body").at("ok")==false,"queue receipt cannot promote write failure to physical success");
 std::cout<<"MANUAL_FEEDBACK_RECEIPT_OK checks="<<checks<<" actual_ipc_closures=true backend_writes=fake hardware_io=none\n";return 0;
 }catch(const std::exception& e){std::cerr<<"MANUAL_FEEDBACK_RECEIPT_FAIL "<<e.what()<<'\n';return 1;}}
`;
fs.writeFileSync(path.join(out,'actual-manual-feedback-receipt.cpp'),cpp);const sha=x=>crypto.createHash('sha256').update(x).digest('hex');fs.writeFileSync(path.join(out,'source-binding.json'),JSON.stringify({mainSha256:sha(main),vibrationHandlerSha256:sha(handler),ledClosureSha256:sha(led),fixtureSha256:sha(cpp),hardwareIo:false,backendWrites:'memory fake'},null,2));console.log('Generated actual manual IPC/LED receipt closure harness.');
