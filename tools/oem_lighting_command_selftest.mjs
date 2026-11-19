// Actual helper command builder and preset admission; no child/device process exists.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {fileURLToPath}from'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),out=path.resolve(process.argv[2]);fs.mkdirSync(out,{recursive:true});const s=fs.readFileSync(process.env.YMCC_LIGHTING_MAIN_SOURCE||path.join(repo,'native/main.cpp'),'utf8');const begin=s.indexOf('static json ymccLightSetterApply('),end=s.indexOf('    // 单一实参只取一次 str()',begin);if(begin<0||end<0)throw Error('Missing production helper builder');const functionPrefix=s.slice(begin,end);const selectedCase=process.argv[3]??null;if(selectedCase!==null&&!['--aya-oem-only','--aokzoe-sequence-only'].includes(selectedCase))throw Error('Unsupported lighting command selection');if(process.argv.length>4)throw Error('Unexpected selection arguments');
const handlerStart=s.indexOf('ipc_on("gamepad.setLed"'),at=s.indexOf('        if (enabled && mode == "preset"',handlerStart),admission=s.slice(at,s.indexOf('        BYTE r1',at));if(at<0||!admission.includes('validPreset'))throw Error('Missing live IPC admission');
const parserStart=s.indexOf('    bool hasJson = false',begin),parserEnd=s.indexOf('\n}',parserStart);const resultParser=s.slice(parserStart,parserEnd);
const ownerReceiptStart=s.indexOf('static bool oxp3OwnerReceiptAccepted('),ownerReceiptEnd=s.indexOf('\n}',ownerReceiptStart);if(ownerReceiptStart<0||ownerReceiptEnd<0)throw Error('Missing production owner receipt gate');const ownerReceiptAccepted=s.slice(ownerReceiptStart,ownerReceiptEnd+2);
let cpp='#include '+JSON.stringify(path.join(repo,'native/oxp3_lighting_owner.h').replaceAll('\\','/'))+'\n#include '+JSON.stringify(path.join(repo,'native/oem_lighting_operations.h').replaceAll('\\','/'))+'\n#include '+JSON.stringify(path.join(repo,'native/lighting_device_core_binding.h').replaceAll('\\','/'))+String.raw`
#include <atomic>
#include <optional>
#include <cstdint>
#include <string>
#include <sstream>
#include <vector>
#include <iostream>
#include <stdexcept>
#include "json.hpp"
using json=nlohmann::json;using BYTE=unsigned char;using DWORD=std::uint32_t;constexpr unsigned INVALID_FILE_ATTRIBUTES=~0u;
struct{std::string deviceClass;}g_machineIdentity;
std::wstring kLightSetterExe=L"G:\\Program Files\\YMCC\\light-setter.exe",kLightSetterHcRuntimeRoot=L"G:\\Program Files\\YMCC\\HC";
std::wstring POWER_CONTROL_DIR=L"G:\\Program Files\\YMCC\\PowerControl";
bool missingHc=false,missingCore=false;
unsigned GetFileAttributesW(const wchar_t* path){const std::wstring p(path);if(missingHc&&p.find(L"HandheldCompanion.dll")!=std::wstring::npos)return INVALID_FILE_ATTRIBUTES;if(missingCore&&p.find(L"YeManLightDeviceCore.dll")!=std::wstring::npos)return INVALID_FILE_ATTRIBUTES;return 0;}
std::wstring U2W(const std::string&s){return std::wstring(s.begin(),s.end());}
// Command/parser-only terminal: no owner reservation, clock, child or hardware.
// OXP3 command admission fails closed; parser completion flags are explicit inputs.
static ymcc::oxp3_owner::Lease g_oxp3OwnerLease;
static std::atomic<bool> g_oxp3OwnerApplyOutstanding{false},g_oxp3OwnerPendingDebt{false};
static std::uint64_t GetTickCount64(){return 0;}
static bool oxp3OwnerAdmit(std::uint64_t,std::string& reason){reason="command-parser-owner-terminal-inert";return false;}
static std::string W2U(const std::wstring& s){std::string out;for(const wchar_t c:s)out.push_back(static_cast<char>(c));return out;}
namespace ymcc::superjoy_owner {
 enum class Phase{Idle};struct Admission{std::string token,model;std::wstring path;std::uint64_t generation=0;};
 static bool sourceModel(const std::string& m){return m=="HandheldCompanion.Devices.AYANEO3"||m=="HandheldCompanion.Devices.AYANEONEXT2"||m=="HandheldCompanion.Devices.OneXPlayerApex";}
}
static bool superJoyLightingRouteAdmitted(){return false;}
static std::optional<ymcc::superjoy_owner::Admission> superJoyLightingAdmit(std::string&){return std::nullopt;}
struct InertOwner{struct O{ymcc::superjoy_owner::Phase phase=ymcc::superjoy_owner::Phase::Idle;};O observation(){return {};}};
static InertOwner& superJoyLightingOwner(){static InertOwner owner;return owner;}
static bool legacyDebt=false;static bool legacyLightSetterHasDebt(){return legacyDebt;}
static bool legacyLightSetterDebtClass(const std::string& cls){return ymcc::lighting::oemOwnershipFor(cls)!=nullptr;}
`+ownerReceiptAccepted+'\n'+functionPrefix+String.raw`
 return json{{"command",std::string(cmd.str().begin(),cmd.str().end())}};
}
[[maybe_unused]] static json actualIpcAdmission(bool enabled,const std::string& mode,const std::string& presetId){auto receipt=[](const json& result){return result;};const auto* lightingRoute=ymcc::lighting::resolve(g_machineIdentity.deviceClass);if(!lightingRoute)return json{{"ok",false}};
`+admission+String.raw`
 return json{{"ok",true},{"admitted",true}};
}
static json parseReply(const std::string& childOut,unsigned exitCode,bool brightnessSupported=true,bool timedOut=false,const std::string& action="preset",bool ownerGenerationStable=true,bool ownerChildReturned=true,bool ownerLeaseReleased=true){
 const bool outputReadTimedOut=false,outputTruncated=false,outputReadError=false,presetAction=action=="preset";const std::string presetId="LEDPreset_OneXPlayerX1_Preset09",mode="solid";
 const bool ownerAdmitted=g_machineIdentity.deviceClass=="HandheldCompanion.Devices.OneXPlayer3";
 const bool restoreOem=action=="restore-oem",superJoyOwnerRoute=false;
 const auto* oemOwnership=ymcc::lighting::oemOwnershipFor(g_machineIdentity.deviceClass);
 const std::optional<ymcc::superjoy_owner::Admission> superJoyAdmission;

`+resultParser+String.raw`
}
int main(){unsigned checks=0;auto check=[&](bool ok,const char* msg){++checks;if(!ok)throw std::runtime_error(msg);};try{
 for(const char* name:{"OneXPlayerX1","OneXPlayerX1AMD","OneXPlayerX1Intel","OneXPlayerX1Pro","OneXPlayerApex"}){
  g_machineIdentity.deviceClass=std::string("HandheldCompanion.Devices.")+name;
  for(const auto& preset:ymcc::lighting::kOneXPresets){
   auto result=ymccLightSetterApply(true,"preset",1,2,3,4,5,6,65,42,preset.id);const auto command=result.at("command").get<std::string>();
   check(command.find(" --action preset --preset-id "+std::string(preset.id))!=std::string::npos,"native forwards stable ID to separate preset action");check(command.find(" --level preset")==std::string::npos,"preset never sent as color enum128");check(command.find(" --brightness 42")!=std::string::npos,"real brightness retained");check(command.find(" --expected-device-class \""+g_machineIdentity.deviceClass+"\"")!=std::string::npos,"helper request bound to exact canonical class");check(actualIpcAdmission(true,"preset",preset.id).at("ok")==true,"same ID admitted by actual live IPC");
  }
  for(const std::string id:{"","bad-id","LEDPreset_OneXPlayerX1_Preset09 --action off"}){
   check(ymccLightSetterApply(true,"preset",1,2,3,4,5,6,65,42,id).at("ok")==false,"invalid ID cannot enter child command");check(actualIpcAdmission(true,"preset",id).at("ok")==false,"invalid ID rejected before native backend writes");
  }
 }
 for(const char* name:{"AynLoki","LokiMiniPro","LokiZero","LokiMax6600U","LokiMax6800U"}){
  g_machineIdentity.deviceClass=std::string("HandheldCompanion.Devices.")+name;auto result=ymccLightSetterApply(true,"solid",1,2,3,4,5,6,65,42);const auto cmd=result.at("command").get<std::string>();check(cmd.find(" --brightness ")==std::string::npos,"Loki no-op brightness never sent as real operation");check(cmd.find(" --action color --level solid")!=std::string::npos,"Loki color command preserved");
 }
 for(const char* name:{"OneXPlayerX1Mini","ClawCG3EM","XboxROGAllyX","GPDWin5","Unknown"}){
  g_machineIdentity.deviceClass=std::string("HandheldCompanion.Devices.")+name;check(actualIpcAdmission(true,"preset","LEDPreset_OneXPlayerX1_Preset01").at("ok")==false,"unrelated/unsupported class cannot borrow preset transport");
 }
 g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayerX1Mini";auto off=ymccLightSetterApply(false,"preset",1,2,3,4,5,6,65,42,"bad");check(off.at("command").get<std::string>().find(" --action off")!=std::string::npos,"off remains available despite obsolete unsupported preset setting");check(actualIpcAdmission(false,"preset","bad").at("ok")==true,"invalid remembered preset cannot block explicit off");
 g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayerX1Mini";
 missingHc=true;
 for(const std::string mode:{"solid","rainbow"}){
  const auto command=ymccLightSetterApply(true,mode,1,2,3,4,5,6,65,42).at("command").get<std::string>();
  check(command.find(" --device-core-assembly ")!=std::string::npos,"supported Mini action uses real native independent binding");
  check(command.find(" --hc-assembly ")==std::string::npos,"supported Mini action requires no HC file or argument");
  check(command.find(ymcc::lighting::kIndependentDeviceCoreSha256)!=std::string::npos,"native command carries fixed core consistency pin");
 }
 check(ymccLightSetterApply(true,"breathing",1,2,3,4,5,6,65,42).at("reason")=="hc-runtime-missing","unsupported Mini mode retains explicit HC requirement");
 missingCore=true;
 check(ymccLightSetterApply(true,"solid",1,2,3,4,5,6,65,42).at("reason")=="light-device-core-missing","missing independent component does not silently fall back HC");
 missingCore=false;missingHc=false;
 g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayerX1";
 const json queued={{"deviceClass","HandheldCompanion.Devices.OneXPlayerX1Pro"},{"capabilityId","hc.device.OneXPlayerX1Pro"},{"capabilityOperation","lighting.preset"},{"capabilityCatalogSha256",ymcc::lighting::kCatalogSha256},{"action","preset"},{"note","ok"},{"queuedWriteManaged",true},{"queuedWritesCompleted",true},{"queuedWriteTasks",3},{"queuedWriteFailures",0},{"presetId","LEDPreset_OneXPlayerX1_Preset09"}};
 auto accepted=parseReply(json{{"ok",true},{"result",queued}}.dump(),0);check(accepted.at("ok")==true&&accepted.at("helperReceipt").at("queuedWriteTasks")==3,"actual native child parser retains completed queued write receipt");
 auto failed=queued;failed["note"]="onex-write-queue-failed";failed["queuedWriteFailures"]=1;
 auto rejected=parseReply(json{{"ok",true},{"result",failed}}.dump(),1);check(rejected.at("ok")==false&&rejected.at("helperReceipt").at("queuedWriteFailures")==1,"failed child preserves diagnostic queue error without acceptance");
 auto timed=parseReply(json{{"ok",true},{"result",queued}}.dump(),0,true,true);check(timed.at("ok")==false&&timed.at("effectApplied")==false,"valid child JSON cannot override timeout gate");
 check(accepted.at("capabilityId")=="hc.device.OneXPlayerX1Pro"&&accepted.at("requestedCapabilityId")=="hc.device.OneXPlayerX1","bound helper receipt retains concrete factory identity alongside base request");
 for(const auto key:{"capabilityId","deviceClass","capabilityCatalogSha256","capabilityOperation","action","presetId"}){
  auto bad=queued;bad.erase(key);const auto r=parseReply(json{{"ok",true},{"result",bad}}.dump(),0);check(r.at("ok")==false&&r.at("effectApplied")==false,"missing receipt binding cannot become success");
  bad=queued;bad[key]="unreviewed";const auto f=parseReply(json{{"ok",true},{"result",bad}}.dump(),0);check(f.at("ok")==false&&f.at("helperReceipt").at("queuedWriteTasks")==3,"mismatched binding rejects success and retains possible completed writes");
 }
 auto wrong=queued;wrong["deviceClass"]="HandheldCompanion.Devices.LokiMax6800U";wrong["capabilityId"]="hc.device.LokiMax6800U";check(parseReply(json{{"ok",true},{"result",wrong}}.dump(),0).at("reason")=="light-setter-identity-mismatch","registered unrelated factory cannot borrow a base request");
 wrong=queued;wrong["capabilityCatalogSha256"]="old-helper";check(parseReply(json{{"ok",true},{"result",wrong}}.dump(),0).at("reason")=="light-setter-catalog-mismatch","stale helper receives explicit catalog mismatch");
 g_machineIdentity.deviceClass="HandheldCompanion.Devices.AynLoki";
 auto color=queued;color["deviceClass"]="HandheldCompanion.Devices.LokiMax6800U";color["capabilityId"]="hc.device.LokiMax6800U";color["action"]="color";color["capabilityOperation"]="lighting.color.solid";
 auto loki=parseReply(json{{"ok",true},{"result",color}}.dump(),0,false,false,"color");check(loki.at("ok")==true&&loki.at("brightnessApplied")==false&&loki.at("capabilityId")=="hc.device.LokiMax6800U","color-only actual factory helper result does not claim unsupported brightness");
 g_machineIdentity.deviceClass="HandheldCompanion.Devices.LokiMax6800U";color["deviceClass"]="HandheldCompanion.Devices.AynLoki";color["capabilityId"]="hc.device.AynLoki";check(parseReply(json{{"ok",true},{"result",color}}.dump(),0,false,false,"color").at("ok")==false,"base-to-factory permission cannot be reversed");
 g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayer3";
 const json oxp3={{"deviceClass",g_machineIdentity.deviceClass},{"capabilityId","ymcc.device.OneXPlayer3"},{"capabilityOperation","lighting.color.solid"},{"capabilityCatalogSha256",ymcc::lighting::kCatalogSha256},{"action","color"},{"note","ok"}};
 const auto unreturned=parseReply(json{{"ok",true},{"result",oxp3}}.dump(),0,true,false,"color",true,false,true);
 check(unreturned.at("childDeclaredOk")==true&&unreturned.at("childIdentityAccepted")==true&&unreturned.at("childCatalogAccepted")==true&&unreturned.at("childOperationAccepted")==true&&
       unreturned.at("ownerAdmitted")==true&&unreturned.at("ownerGenerationStable")==true&&unreturned.at("ownerChildReturned")==false&&unreturned.at("ownerLeaseReleased")==true&&
       unreturned.at("ok")==false&&unreturned.at("resultAccepted")==false&&unreturned.at("effectApplied")==false&&unreturned.at("brightnessApplied")==false&&unreturned.at("reason")=="light-setter-owner-child-not-returned",
       "OXP3 valid child result cannot pass production receipt gate before owner child returns");
 std::cout<<"OEM_LIGHTING_COMMAND_OK checks="<<checks<<" actual_native_command=true child_processes=0 hardware_io=none\n";return 0;
}catch(const std::exception&e){std::cerr<<"OEM_LIGHTING_COMMAND_FAIL "<<e.what()<<'\n';return 1;}}
`;
if(selectedCase==='--aya-oem-only'){
 const mainStart=cpp.indexOf('int main(){'),mainEnd=cpp.lastIndexOf('\n');if(mainStart<0)throw Error('Missing original test main');
 cpp=cpp.slice(0,mainStart)+String.raw`
int main(){unsigned checks=0;auto check=[&](bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);};try{
 for(const auto& row:ymcc::lighting::kOemOwnership){
  for(const auto& device:ymcc::capabilities::devices){if(device.deviceId!=row.deviceId)continue;g_machineIdentity.deviceClass=std::string(device.factoryType);break;}
  const auto cmd=ymccLightSetterApply(false,"unavailable-old-effect",0,0,0,0,0,0,0,0,"stale-preset",true).at("command").get<std::string>();
  check(cmd.find(" --action restore-oem")!=std::string::npos&&cmd.find(" --hc-profile ymcc-device-core-v1")!=std::string::npos,"OEM command uses approved runtime and distinct action");
  for(const char* forbidden:{" --r "," --r2 "," --level "," --brightness "," --preset-id "," --speed "," --device-core-assembly "})check(cmd.find(forbidden)==std::string::npos,"OEM command contains custom effect controls");
  const bool legacy=row.protocol==ymcc::lighting::OemOwnershipProtocol::LegacyCec;
  auto child=json{{"action","restore-oem"},{"deviceClass",g_machineIdentity.deviceClass},{"capabilityId",row.deviceId},{"capabilityOperation","lighting.restore-oem"},{"capabilityCatalogSha256",ymcc::lighting::kCatalogSha256},{"note","ok"},{"oemRestoreRequested",true},{"ownershipRequestCompleted",true},{"ownershipReadbackAvailable",false},{"physicalEffectVerified",false},{"ownershipProtocol",legacy?"legacy-cec":"ceii-ram"},{"writeCallCount",legacy?1:12}};
  const auto parse=[&](const json& value,unsigned code=0,bool timedOut=false,bool returned=true){return parseReply(json{{"ok",true},{"result",value}}.dump(),code,true,timedOut,"restore-oem",true,returned,true);};
  const auto good=parse(child);check(good.at("ok")==true&&good.at("ownershipRequestCompleted")==true&&good.at("effectApplied")==false&&good.at("brightnessApplied")==false,"OEM request completed without physical/effect claims");
  for(const char* key:{"action","deviceClass","capabilityOperation","capabilityCatalogSha256","oemRestoreRequested","ownershipRequestCompleted","ownershipReadbackAvailable","physicalEffectVerified","ownershipProtocol","writeCallCount"}){auto bad=child;bad.erase(key);check(parse(bad).at("ok")==false,"missing OEM receipt field accepted");}
  for(const char* key:{"ownershipReadbackAvailable","physicalEffectVerified"}){auto bad=child;bad[key]=true;check(parse(bad).at("ok")==false,"unproven hardware/readback claim accepted");}
  auto bad=child;bad["ownershipProtocol"]="other-protocol";check(parse(bad).at("ok")==false,"wrong protocol accepted");bad=child;bad["writeCallCount"]=legacy?12:1;check(parse(bad).at("ok")==false,"wrong write count accepted");bad=child;bad["ownershipRequestCompleted"]=false;check(parse(bad).at("ok")==false,"incomplete OEM request accepted");
  check(parse(child,1).at("ok")==false&&parse(child,0,true).at("ok")==false&&parse(child,0,false,false).at("ok")==false,"failure/timeout/outstanding child cannot pass ownership receipt");
  legacyDebt=true;check(ymccLightSetterApply(false,"solid",0,0,0,0,0,0,0,0,{},true).at("reason")=="light-setter-legacy-return-debt","OEM request bypassed outstanding child debt");legacyDebt=false;
 }
 for(const char* name:{"AYANEO3","AYANEONEXT2","OneXPlayer3","XboxROGAllyX","ClawCG3EM","Unknown"}){g_machineIdentity.deviceClass=std::string("HandheldCompanion.Devices.")+name;check(!ymccLightSetterApply(false,"solid",0,0,0,0,0,0,0,0,{},true).at("ok").get<bool>(),"unknown/non-AYA borrowed OEM command");}
 std::cout<<json{{"status","PASS"},{"checks",checks},{"scope","actual native OEM helper command and actual receipt parser at memory terminals"},{"hardwareIo",false},{"childProcesses",0}}.dump()<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
`;
}

if(selectedCase==='--aokzoe-sequence-only'){
 cpp=cpp.slice(0,cpp.indexOf('int main(){'))+String.raw`
int main(){unsigned checks=0;auto check=[&](bool ok,const char* msg){++checks;if(!ok)throw std::runtime_error(msg);};try{
 for(const char* name:{"AOKZOEA1","AOKZOEA1Pro","AOKZOEA1X","AOKZOEA2"}){
  g_machineIdentity.deviceClass=std::string("HandheldCompanion.Devices.")+name;
  const auto command=ymccLightSetterApply(true,"solid",1,2,3,4,5,6,65,65).at("command").get<std::string>();
  check(command.find(" --brightness 65")!=std::string::npos&&command.find(" --action color --level solid")!=std::string::npos,"AOKZOE explicit color/brightness reaches helper");
  const json child={{"deviceClass",g_machineIdentity.deviceClass},{"capabilityId",std::string("hc.device.")+name},{"capabilityCatalogSha256",ymcc::lighting::kCatalogSha256},{"capabilityOperation","lighting.color.solid"},{"action","color"},{"note","ok"},{"queuedWriteManaged",false},{"queuedWritesCompleted",false},{"queuedWriteTasks",0},{"queuedWriteFailures",0},{"colorSequencePolicy","brightness-settle-color"},{"colorSequenceDelayMs",300},{"physicalEffectVerified",false}};
  auto parse=[&](const json& value,unsigned exitCode=0,bool timedOut=false){return parseReply(json{{"ok",true},{"result",value}}.dump(),exitCode,true,timedOut,"color");};
  auto accepted=parse(child);check(accepted.value("ok",false),"AOKZOE actual receipt accepted");
  check(accepted.at("helperReceipt").value("colorSequencePolicy",std::string())=="brightness-settle-color","sequence policy survives native receipt filter");
  check(accepted.at("helperReceipt").value("colorSequenceDelayMs",0)==300,"sequence delay survives native receipt filter");
  check(!accepted.at("helperReceipt").value("physicalEffectVerified",true),"accepted write never claims physical lamp response");
  auto bad=child;bad["colorSequencePolicy"]=true;bad["colorSequenceDelayMs"]="300";
  const auto malformed=parse(bad);check(malformed.value("ok",false)&&!malformed.at("helperReceipt").contains("colorSequencePolicy")&&!malformed.at("helperReceipt").contains("colorSequenceDelayMs"),"wrong diagnostic types omitted without changing operation admission");
  bad=child;bad["note"]="setLedBrightness-false";auto failed=parse(bad,1);check(!failed.value("ok",true)&&!failed.value("effectApplied",true),"failed brightness remains failure");
  auto timeout=parse(child,0,true);check(!timeout.value("ok",true)&&!timeout.value("effectApplied",true),"sequence metadata never overrides timeout");
  bad=child;bad["deviceClass"]="HandheldCompanion.Devices.OneXPlayerG1";check(!parse(bad).value("ok",true),"sequence metadata never bypasses identity gate");
 }
 std::cout<<json{{"status","PASS"},{"selector","aokzoe-sequence-only"},{"models",4},{"checks",checks},{"hardwareIo",false},{"physicalEffectVerified",false},{"defaultSuiteRun",false}}.dump()<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
`;
}

// Preserve one std::wstring object when converting: iterator pairs from distinct
// cmd.str() temporaries are invalid (the product builder already keeps one).
const fixed=cpp.replace('return json{{"command",std::string(cmd.str().begin(),cmd.str().end())}};','const auto value=cmd.str();return json{{"command",W2U(value)}};');
fs.writeFileSync(path.join(out,'actual-lighting-command.cpp'),fixed);const sha=x=>crypto.createHash('sha256').update(x).digest('hex');
const sourceSlices=[['helperCommandPrefix',begin,functionPrefix],['ipcAdmission',at,admission],['resultParser',parserStart,resultParser],['ownerReceiptAccepted',ownerReceiptStart,ownerReceiptAccepted]].map(([name,start,text])=>({name,source:'native/main.cpp',startByte:Buffer.byteLength(s.slice(0,start)),bytes:Buffer.byteLength(text),sha256:sha(text),compiledInto:'actual-lighting-command.cpp'}));
const ownerHeader=fs.readFileSync(path.join(repo,'native/oxp3_lighting_owner.h'));
fs.writeFileSync(path.join(out,'source-binding.json'),JSON.stringify({mainSha256:sha(s),helperCommandPrefixSha256:sha(functionPrefix),ipcAdmissionSha256:sha(admission),resultParserSha256:sha(resultParser),ownerReceiptAcceptedSha256:sha(ownerReceiptAccepted),sourceSlices,ownerHeader:{path:'native/oxp3_lighting_owner.h',bytes:ownerHeader.length,sha256:sha(ownerHeader)},ownerTerminal:'inert-deny-admission; parser completion supplied in memory',hardwareIo:false,childProcesses:0},null,2));console.log('Generated actual native command/admission harness; no process API linked.');
