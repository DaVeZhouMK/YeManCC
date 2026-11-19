// Actual native feedbackCapabilities lambda, with all external state in memory.
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
if(!process.argv[2])throw Error('Output directory required');const out=path.resolve(process.argv[2]);fs.mkdirSync(out,{recursive:true});
const sourceFile=path.resolve(process.env.YMCC_FEEDBACK_CAPABILITIES_MAIN_SOURCE||path.join(root,'native/main.cpp'));
const s=fs.readFileSync(sourceFile,'utf8'),st=s.indexOf('ipc_on("gamepad.feedbackCapabilities"'),en=s.indexOf('\n    });',st)+8;
if(st<0||en<=st)throw Error('Missing capabilities production lambda');const body=s.slice(st,en);
let cpp='#include '+JSON.stringify(path.join(root,'native/oem_lighting_operations.h').replaceAll('\\','/'))+'\n'+String.raw`
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
enum class YmccFamily{Unknown,MsiClaw,AsusRogAllyX,Gpd,Ayanero};
struct{YmccFamily family=YmccFamily::Unknown;std::string deviceClass="fake";}g_machineIdentity;
struct{HANDLE control=-1,auraControl=-1;int auraFeatureLen=64,featureLen=64,productId=0;bool identityMatched=false,suppressed=false;}g_rog;
struct HidHideMsiMainSourceRecord{uint64_t generation=0;std::string interfacePath;};
std::wstring kLightSetterExe=L"fixture.exe";std::mutex g_gamepadReadMx;
unsigned g_xinputPrimarySlot=0,g_xinputConnectedMask=0,g_xinputAdmittedMask=0,g_xinputPhysicalSourceMask=0xf;
bool g_xinputPhysicalSourceLocked=false,familyLighting=false,helperPresent=false,rogFamily=false,msiOwner=false;
json lastLog;
unsigned motorWriteAttempts=0;
json nativeGamepadFeedbackPulse(int,int){++motorWriteAttempts;throw std::runtime_error("capability-query-issued-feedback-write");}
bool ymccDeviceClassSupportsLighting(const std::string&){return familyLighting;}
bool ymccDeviceClassSupportsSecondColor(const std::string&){return false;}
std::vector<std::string> ymccDeviceClassLedEffects(const std::string&){return {"solid"};}
DWORD GetFileAttributesW(const wchar_t*){return helperPresent?0:INVALID_FILE_ATTRIBUTES;}
bool machineIsRogAllyFamily(){return rogFamily;}
DWORD xinputPhysicalConnectedMask(){return g_xinputConnectedMask;}
bool msiDinputManagedMainSourceActive(HidHideMsiMainSourceRecord& r){if(msiOwner){r.generation=7;r.interfacePath="fake-source";}return msiOwner;}
const char* ymccFamilyName(YmccFamily f){return f==YmccFamily::Ayanero?"ayanero":f==YmccFamily::MsiClaw?"msi-claw":f==YmccFamily::Gpd?"gpd":"rog";}
void appendNativeLifecycleLog(const char*,const json& row){lastLog=row;}
`+'\nstatic json caps(const json& args=json::object()) { auto query='+body.replace('ipc_on("gamepad.feedbackCapabilities", ','').replace(/\);\s*$/,'')+';return query(args);}\n'+String.raw`
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
 check(motorWriteAttempts==0,"actual capability consumer cannot invoke the motor writer");
 check(r.at("vibrationDetectionMode")=="static"&&lastLog.at("vibrationDetectionMode")=="static","all queried states report static detection");
 check(r.at("vibrationTestRequiresUserAction")==true&&lastLog.at("vibrationTestRequiresUserAction")==true,"capability discovery cannot authorize manual tests");
 check(r.at("verificationScope")=="capability-query-only;physical-response-not-observed","explicit proof boundary");
 check(r.at("vibrationAvailable")==bool(msiOwner||physical),"existing vibration gate unchanged");
 check(r.at("ledAvailable")==expectedLight,"existing lighting gate unchanged");
 check(r.at("vibrationBackend")== (msiOwner?"msi-dinput-hid":physical?"xinput":"none"),"existing backend selection unchanged");
 check(r.at("physicalPrimarySlot")== (physical?0:-1),"existing source slot unchanged");
 check(lastLog.at("vibrationAvailable")==r.at("vibrationAvailable")&&lastLog.at("ledAvailable")==r.at("ledAvailable"),"native receipts agree with response");
 check(lastLog.at("schema")==3&&lastLog.at("deviceClass")==g_machineIdentity.deviceClass,"capability log binds exact queried model");
 check(lastLog.at("capabilityId")==std::string(ymcc::lighting::deviceId(g_machineIdentity.deviceClass))&&lastLog.at("capabilityCatalogSha256")==ymcc::lighting::kCatalogSha256,"capability log preserves current catalog identity");
 for(const char* key:{"oemRestoreAvailable","oemOwnershipProtocol","sharedChargeIndicatorDeclared","ledReason","brightnessAvailable","presetImplemented"})check(lastLog.at(key)==r.at(key),"ordinary receipt retains exact lighting and charging declaration without granting control");
 }
 familyLighting=true;helperPresent=true;rogFamily=false;msiOwner=false;g_machineIdentity.family=YmccFamily::Gpd;g_rog.auraControl=-1;
 for(const char* name:{"AynLoki","LokiMiniPro","LokiZero","LokiMax6600U","LokiMax6800U"}){
  g_machineIdentity.deviceClass=std::string("HandheldCompanion.Devices.")+name;auto r=caps();++cases;
  check(r.at("ledAvailable")==true&&r.at("brightnessAvailable")==false,"Loki keeps color capability without fake brightness");check(r.at("ledPresets").empty(),"Loki cannot borrow OneX presets");
 }
 for(const char* name:{"OneXPlayerX1","OneXPlayerX1AMD","OneXPlayerX1Intel","OneXPlayerX1Pro"}){
  g_machineIdentity.deviceClass=std::string("HandheldCompanion.Devices.")+name;auto r=caps();++cases;
  check(r.at("brightnessAvailable")==true&&r.at("presetImplemented")==true,"serial OneX has actual operation metadata");check(r.at("ledPresets").size()==11,"eleven named HC presets");check(r.at("ledPresets")[8].at("id")=="LEDPreset_OneXPlayerX1_Preset09","stable ID for high-value preset");
 }
 g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayerApex";auto apex=caps();++cases;
 check(ymcc::lighting::sourceAdapter(g_machineIdentity.deviceClass)==ymcc::lighting::SourceAdapter::ApexHidV2,"exact APEX keeps its reviewed HID V2 route");
 check(apex.at("ledAvailable")==true&&apex.at("brightnessAvailable")==true,"APEX HID retains light and brightness");
 check(apex.at("presetImplemented")==false&&apex.at("presetDeclared")==false&&apex.at("ledPresets").empty(),"APEX must not borrow X1 serial presets");
 check(apex.at("lightingFixedSpeed")==true&&apex.at("supportsSecondColor")==false,"APEX cannot expose ignored speed or secondary color");
 g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayer3";auto oxp3=caps();++cases;
 check(oxp3.at("ledPresets").size()==10,"OXP3 advertises only ten reviewed presets, not the legacy eleven");
 for(const auto& preset:oxp3.at("ledPresets")) check(ymcc::lighting::validPreset(g_machineIdentity.deviceClass,preset.at("id").get<std::string>()),"Every advertised preset must be admitted by this exact model");
 check(oxp3.at("lightingFixedSpeed")==true,"OXP3 fixed firmware speed must reach the UI");
 check(oxp3.at("presetSupportsSecondColor")==true,"OXP3 preset secondary zones must remain controllable");
 g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayerX1Mini";auto mini=caps();++cases;
 check(mini.at("ledAvailable")==true&&mini.at("brightnessAvailable")==true,"Mini retains HID light and brightness");check(mini.at("presetDeclared")==true&&mini.at("presetImplemented")==false&&mini.at("ledPresets").empty(),"Mini inherited serial preset remains explicit missing operation");check(mini.at("presetReason")=="preset-transport-not-implemented","Mini failure reason not hidden");
 g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.deviceClass="HandheldCompanion.Devices.ClawCG3EM";auto msi=caps();++cases;
 check(msi.at("brightnessAvailable")==true&&msi.at("presetImplemented")==false,"native MSI brightness retained without OneX controls");
 g_machineIdentity.family=YmccFamily::Ayanero;helperPresent=true;familyLighting=true;rogFamily=false;msiOwner=false;
 for(const auto& row:ymcc::lighting::kOemOwnership){for(const auto& device:ymcc::capabilities::devices)if(device.deviceId==row.deviceId){g_machineIdentity.deviceClass=std::string(device.factoryType);break;}auto r=caps();++cases;check(r.at("oemRestoreAvailable")==true,"registered AYA OEM release reaches capability consumer");for(const char* key:{"oemRestoreAvailable","oemOwnershipProtocol","sharedChargeIndicatorDeclared"})check(lastLog.at(key)==r.at(key),"exact per-model OEM/charge capability reaches exported receipt");check(r.at("sharedChargeIndicatorDeclared")==row.sharedChargeIndicatorDeclared,"charging topology declaration kept per model");check(r.at("oemOwnershipProtocol")== (row.protocol==ymcc::lighting::OemOwnershipProtocol::LegacyCec?"legacy-cec":"ceii-ram"),"exact OEM release protocol exposed");check(r.at("hardwareWriteIssued")==false&&r.at("ledVerified")==false,"read-only probe claims no physical success");helperPresent=false;check(caps().at("oemRestoreAvailable")==false,"missing helper cannot expose live OEM operation");helperPresent=true;}
 g_machineIdentity.deviceClass="HandheldCompanion.Devices.AYANEONEXT2";check(caps().at("oemRestoreAvailable")==false,"SuperJoy cannot borrow legacy release");g_machineIdentity.family=YmccFamily::Gpd;g_machineIdentity.deviceClass="HandheldCompanion.Devices.AYANEOAIR1S";check(caps().at("oemRestoreAvailable")==false,"wrong family cannot expose AYA operation");
 const auto requestedProbe=caps(json{{"probe",true},{"active",true},{"strength",100}});++cases;
 check(motorWriteAttempts==0&&requestedProbe.at("hardwareWriteIssued")==false,"query arguments cannot request an active probe");
 check(requestedProbe.at("vibrationDetectionMode")=="static"&&requestedProbe.at("vibrationTestRequiresUserAction")==true,"query remains static with active-looking arguments");
 std::cout<<"FEEDBACK_CAPABILITY_EVIDENCE_OK cases="<<cases<<" checks="<<checks<<" actual_production_lambda=true hardware_io=none motor_write_attempts="<<motorWriteAttempts<<"\n";return 0;
}catch(const std::exception&e){std::cerr<<"FEEDBACK_CAPABILITY_EVIDENCE_FAIL "<<e.what()<<'\n';return 1;}}
`;
const selectedCase=process.argv[3]??null;if(selectedCase!==null&&selectedCase!=='--aya-oem-only'||process.argv.length>4)throw Error('Unsupported feedback query selection');
if(selectedCase){const mainStart=cpp.indexOf('int main(){'),testsStart=cpp.indexOf(' g_machineIdentity.family=YmccFamily::Ayanero;helperPresent=true;',mainStart),testsEnd=cpp.indexOf(' std::cout<<"FEEDBACK_CAPABILITY_EVIDENCE_OK',testsStart);if(mainStart<0||testsStart<0||testsEnd<0)throw Error('AYA-only actual query cases missing');cpp=cpp.slice(0,mainStart)+'int main(){unsigned checks=0,cases=0;auto check=[&](bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);};try{\n'+cpp.slice(testsStart,testsEnd)+'std::cout<<"FEEDBACK_CAPABILITY_EVIDENCE_OK cases="<<cases<<" checks="<<checks<<" selected=aya-oem-only hardware_io=none\\n";return 0;}catch(const std::exception&e){std::cerr<<e.what();return 1;}}\n';}

fs.writeFileSync(path.join(out,'actual-feedback-capabilities.cpp'),cpp);fs.writeFileSync(path.join(out,'source-binding.json'),JSON.stringify({schema:1,sourceFile,sourceSha256:crypto.createHash('sha256').update(s).digest('hex'),lambdaSha256:crypto.createHash('sha256').update(body).digest('hex'),hardwareIo:false},null,2));console.log('Actual production feedback lambda extracted; all external APIs/state are fakes.');
