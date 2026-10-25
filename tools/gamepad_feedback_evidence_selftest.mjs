// Actual native feedbackCapabilities lambda, with all external state in memory.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
if(!process.argv[2])throw Error('Output directory required');const out=path.resolve(process.argv[2]);fs.mkdirSync(out,{recursive:true});
const s=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8'),st=s.indexOf('ipc_on("gamepad.feedbackCapabilities"'),en=s.indexOf('\n    });',st)+8;
if(st<0||en<=st)throw Error('Missing capabilities production lambda');const body=s.slice(st,en);
const cpp='#include '+JSON.stringify(path.join(root,'native/oem_lighting_operations.h').replaceAll('\\','/'))+'\n'+String.raw`
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <iostream>
#include <stdexcept>
#include "json.hpp"
using json=nlohmann::json;using DWORD=uint32_t;using HANDLE=intptr_t;
constexpr HANDLE INVALID_HANDLE_VALUE=-1;constexpr DWORD INVALID_FILE_ATTRIBUTES=0xffffffffu;
enum class YmccFamily{Unknown,MsiClaw,AsusRogAllyX,Gpd};
struct{YmccFamily family=YmccFamily::Unknown;std::string deviceClass="fake";}g_machineIdentity;
struct{HANDLE control=-1,auraControl=-1;int auraFeatureLen=64,featureLen=64,productId=0;bool identityMatched=false,suppressed=false;}g_rog;
struct HidHideMsiMainSourceRecord{uint64_t generation=0;std::string interfacePath;};
std::wstring kLightSetterExe=L"fixture.exe";std::mutex g_gamepadReadMx;
unsigned g_xinputPrimarySlot=0,g_xinputConnectedMask=0,g_xinputAdmittedMask=0,g_xinputPhysicalSourceMask=0xf;
bool g_xinputPhysicalSourceLocked=false,familyLighting=false,helperPresent=false,rogFamily=false,msiOwner=false;
json lastLog;
bool ymccDeviceClassSupportsLighting(const std::string&){return familyLighting;}
bool ymccDeviceClassSupportsSecondColor(const std::string&){return false;}
std::vector<std::string> ymccDeviceClassLedEffects(const std::string&){return {"solid"};}
DWORD GetFileAttributesW(const wchar_t*){return helperPresent?0:INVALID_FILE_ATTRIBUTES;}
bool machineIsRogAllyFamily(){return rogFamily;}
DWORD xinputPhysicalConnectedMask(){return g_xinputConnectedMask;}
bool msiDinputManagedMainSourceActive(HidHideMsiMainSourceRecord& r){if(msiOwner){r.generation=7;r.interfacePath="fake-source";}return msiOwner;}
const char* ymccFamilyName(YmccFamily f){return f==YmccFamily::MsiClaw?"msi-claw":f==YmccFamily::Gpd?"gpd":"rog";}
void appendNativeLifecycleLog(const char*,const json& row){lastLog=row;}
`+'\nstatic json caps() { auto query='+body.replace('ipc_on("gamepad.feedbackCapabilities", ','').replace(/\);\s*$/,'')+';return query(json::object());}\n'+String.raw`
int main(){unsigned checks=0,cases=0;const auto check=[&](bool p,const char* why){++checks;if(!p)throw std::runtime_error(why);};
try{for(unsigned iteration=0;iteration<512;++iteration){const unsigned bits=(iteration+192)%512;++cases;
 familyLighting=bits&1;helperPresent=bits&2;rogFamily=bits&4;msiOwner=bits&8;
 g_machineIdentity.family=(bits&16)?YmccFamily::MsiClaw:rogFamily?YmccFamily::AsusRogAllyX:YmccFamily::Gpd;
 g_machineIdentity.deviceClass=g_machineIdentity.family==YmccFamily::MsiClaw?"HandheldCompanion.Devices.ClawA1M":rogFamily?"HandheldCompanion.Devices.ROGAlly":"HandheldCompanion.Devices.AYANEOSlide";
 g_rog.auraControl=(bits&32)?17:INVALID_HANDLE_VALUE;g_xinputPrimarySlot=(bits&64)?0:4;
 g_xinputConnectedMask=(bits&128)?1:0;g_xinputPhysicalSourceLocked=bits&256;
 g_xinputPhysicalSourceMask=0;lastLog={};auto r=caps();
 const bool physical=g_xinputPrimarySlot<4 && (g_xinputConnectedMask&(g_xinputPhysicalSourceLocked?0:0xf))!=0;
 const bool registeredRog=ymcc::lighting::usesRogAura(g_machineIdentity.deviceClass),registeredMsi=ymcc::lighting::usesMsiNativeHid(g_machineIdentity.deviceClass);
 const bool rogLight=registeredRog&&rogFamily;
 const bool msiLight=registeredMsi&&g_machineIdentity.family==YmccFamily::MsiClaw&&familyLighting;
 const bool expectedLight=familyLighting&&(registeredRog?rogLight:registeredMsi?msiLight:helperPresent)&&(registeredRog?g_rog.auraControl!=-1:(msiLight||helperPresent));
 check(r.at("vibrationVerified")==false,"connection/owner is not physical vibration verification");
 check(lastLog.at("vibrationVerified")==false,"query log must not promote connection to verified");
 check(r.at("ledVerified")==false && lastLog.at("ledVerified")==false,"no visual proof from capability query");
 check(r.at("hardwareWriteIssued")==false && lastLog.at("hardwareWriteIssued")==false,"capability query is read only");
 check(r.at("verificationScope")=="capability-query-only;physical-response-not-observed","explicit proof boundary");
 check(r.at("vibrationAvailable")==bool(msiOwner||physical),"existing vibration gate unchanged");
 check(r.at("ledAvailable")==expectedLight,"existing lighting gate unchanged");
 check(r.at("vibrationBackend")== (msiOwner?"msi-dinput-hid":physical?"xinput":"none"),"existing backend selection unchanged");
 check(r.at("physicalPrimarySlot")== (physical?0:-1),"existing source slot unchanged");
 check(lastLog.at("vibrationAvailable")==r.at("vibrationAvailable")&&lastLog.at("ledAvailable")==r.at("ledAvailable"),"native receipts agree with response");
 }
 familyLighting=true;helperPresent=true;rogFamily=false;msiOwner=false;g_machineIdentity.family=YmccFamily::Gpd;g_rog.auraControl=-1;
 for(const char* name:{"AynLoki","LokiMiniPro","LokiZero","LokiMax6600U","LokiMax6800U"}){
  g_machineIdentity.deviceClass=std::string("HandheldCompanion.Devices.")+name;auto r=caps();++cases;
  check(r.at("ledAvailable")==true&&r.at("brightnessAvailable")==false,"Loki keeps color capability without fake brightness");check(r.at("ledPresets").empty(),"Loki cannot borrow OneX presets");
 }
 for(const char* name:{"OneXPlayerX1","OneXPlayerX1AMD","OneXPlayerX1Intel","OneXPlayerX1Pro","OneXPlayerApex"}){
  g_machineIdentity.deviceClass=std::string("HandheldCompanion.Devices.")+name;auto r=caps();++cases;
  check(r.at("brightnessAvailable")==true&&r.at("presetImplemented")==true,"serial OneX has actual operation metadata");check(r.at("ledPresets").size()==11,"eleven named HC presets");check(r.at("ledPresets")[8].at("id")=="LEDPreset_OneXPlayerX1_Preset09","stable ID for high-value preset");
 }
 g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayerX1Mini";auto mini=caps();++cases;
 check(mini.at("ledAvailable")==true&&mini.at("brightnessAvailable")==true,"Mini retains HID light and brightness");check(mini.at("presetDeclared")==true&&mini.at("presetImplemented")==false&&mini.at("ledPresets").empty(),"Mini inherited serial preset remains explicit missing operation");check(mini.at("presetReason")=="preset-transport-not-implemented","Mini failure reason not hidden");
 g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.deviceClass="HandheldCompanion.Devices.ClawCG3EM";auto msi=caps();++cases;
 check(msi.at("brightnessAvailable")==true&&msi.at("presetImplemented")==false,"native MSI brightness retained without OneX controls");
 std::cout<<"FEEDBACK_CAPABILITY_EVIDENCE_OK cases="<<cases<<" checks="<<checks<<" actual_production_lambda=true hardware_io=none\n";return 0;
}catch(const std::exception&e){std::cerr<<"FEEDBACK_CAPABILITY_EVIDENCE_FAIL "<<e.what()<<'\n';return 1;}}
`;
fs.writeFileSync(path.join(out,'actual-feedback-capabilities.cpp'),cpp);fs.writeFileSync(path.join(out,'source-binding.json'),JSON.stringify({schema:1,sourceSha256:crypto.createHash('sha256').update(s).digest('hex'),lambdaSha256:crypto.createHash('sha256').update(body).digest('hex'),hardwareIo:false},null,2));console.log('Actual production feedback lambda extracted; all external APIs/state are fakes.');