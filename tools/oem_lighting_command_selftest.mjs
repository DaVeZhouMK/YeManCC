// Actual helper command builder and preset admission; no child/device process exists.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {fileURLToPath}from'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),out=path.resolve(process.argv[2]);fs.mkdirSync(out,{recursive:true});const s=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8');const begin=s.indexOf('static json ymccLightSetterApply('),end=s.indexOf('    // 单一实参只取一次 str()',begin);if(begin<0||end<0)throw Error('Missing production helper builder');const functionPrefix=s.slice(begin,end);
const handlerStart=s.indexOf('ipc_on("gamepad.setLed"'),at=s.indexOf('        if (enabled && mode == "preset"',handlerStart),admission=s.slice(at,s.indexOf('        BYTE r1',at));if(at<0||!admission.includes('validPreset'))throw Error('Missing live IPC admission');
const parserStart=s.indexOf('    bool hasJson = false',begin),parserEnd=s.indexOf('\n}',parserStart);const resultParser=s.slice(parserStart,parserEnd);
const cpp='#include '+JSON.stringify(path.join(repo,'native/oem_lighting_operations.h').replaceAll('\\','/'))+'\n#include '+JSON.stringify(path.join(repo,'native/lighting_device_core_binding.h').replaceAll('\\','/'))+String.raw`
#include <string>
#include <sstream>
#include <vector>
#include <iostream>
#include <stdexcept>
#include "json.hpp"
using json=nlohmann::json;using BYTE=unsigned char;constexpr unsigned INVALID_FILE_ATTRIBUTES=~0u;
struct{std::string deviceClass;}g_machineIdentity;
std::wstring kLightSetterExe=L"G:\\Program Files\\YMCC\\light-setter.exe",kLightSetterHcRuntimeRoot=L"G:\\Program Files\\YMCC\\HC";
std::wstring POWER_CONTROL_DIR=L"G:\\Program Files\\YMCC\\PowerControl";
bool missingHc=false,missingCore=false;
unsigned GetFileAttributesW(const wchar_t* path){const std::wstring p(path);if(missingHc&&p.find(L"HandheldCompanion.dll")!=std::wstring::npos)return INVALID_FILE_ATTRIBUTES;if(missingCore&&p.find(L"YeManLightDeviceCore.dll")!=std::wstring::npos)return INVALID_FILE_ATTRIBUTES;return 0;}
std::wstring U2W(const std::string&s){return std::wstring(s.begin(),s.end());}
`+functionPrefix+String.raw`
 return json{{"command",std::string(cmd.str().begin(),cmd.str().end())}};
}
static json actualIpcAdmission(bool enabled,const std::string& mode,const std::string& presetId){auto receipt=[](const json& result){return result;};const auto* lightingRoute=ymcc::lighting::resolve(g_machineIdentity.deviceClass);if(!lightingRoute)return json{{"ok",false}};
`+admission+String.raw`
 return json{{"ok",true},{"admitted",true}};
}
static json parseReply(const std::string& childOut,unsigned exitCode,bool brightnessSupported=true,bool timedOut=false,const std::string& action="preset"){
 const bool outputReadTimedOut=false,outputTruncated=false,outputReadError=false,enabled=true,presetAction=action=="preset";const std::string presetId="LEDPreset_OneXPlayerX1_Preset09",mode="solid";
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
 std::cout<<"OEM_LIGHTING_COMMAND_OK checks="<<checks<<" actual_native_command=true child_processes=0 hardware_io=none\n";return 0;
}catch(const std::exception&e){std::cerr<<"OEM_LIGHTING_COMMAND_FAIL "<<e.what()<<'\n';return 1;}}
`;
// Preserve one std::wstring object when converting: iterator pairs from distinct
// cmd.str() temporaries are invalid (the product builder already keeps one).
const fixed=cpp.replace('return json{{"command",std::string(cmd.str().begin(),cmd.str().end())}};','const auto value=cmd.str();return json{{"command",std::string(value.begin(),value.end())}};');
fs.writeFileSync(path.join(out,'actual-lighting-command.cpp'),fixed);const sha=x=>crypto.createHash('sha256').update(x).digest('hex');fs.writeFileSync(path.join(out,'source-binding.json'),JSON.stringify({mainSha256:sha(s),helperCommandPrefixSha256:sha(functionPrefix),ipcAdmissionSha256:sha(admission),hardwareIo:false,childProcesses:0},null,2));console.log('Generated actual native command/admission harness; no process API linked.');
