// Executes the actual native IPC selector and actual ROG/MSI packet consumers at memory IO terminals.
import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';import {spawnSync} from 'node:child_process';import {createHash} from 'node:crypto';import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),args=process.argv.slice(2);if(args.length!==2||args[0]!=='--out')throw Error('Usage --out isolated-evidence-directory');
const out=path.resolve(args[1]);fs.mkdirSync(out,{recursive:true});const main=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8').replace(/\r\n/g,'\n');
const sha=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex'),sourceHash=sha(path.join(repo,'native/main.cpp'));
function block(signature){const a=main.indexOf(signature),b=main.indexOf('\n}',a);if(a<0||b<0)throw Error('Missing function '+signature);return main.slice(a,b+2);}
const start=main.indexOf('    ipc_on("gamepad.setLed"'),end=main.indexOf('\n    });',start)+8;if(start<0||end<=start)throw Error('Missing actual setLed IPC');const handler=main.slice(start,end);
const enums=main.slice(main.indexOf('enum class RogAuraMode : BYTE'),main.indexOf('// （g_rogAuraMx'));
const msiStart=main.indexOf('static constexpr int kMsiLedZoneCount'),msiEnd=main.indexOf('// MSI Claw 灯光应用：',msiStart);if(msiStart<0||msiEnd<0)throw Error('Missing MSI packet builders');
const include=p=>'#include '+JSON.stringify(path.join(repo,p).replaceAll('\\','/'))+'\n';
const cpp=String.raw`#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;using BYTE=unsigned char;using DWORD=std::uint32_t;using ULONG=std::uint32_t;using HANDLE=std::intptr_t;
constexpr HANDLE INVALID_HANDLE_VALUE=-1;constexpr int FALSE=0;
enum class YmccFamily {Unknown,MsiClaw,AsusRogAllyX,AsusRogAlly,AsusRogAllyClassic,AsusRogAllyXClassic};
struct {std::string deviceClass;YmccFamily family=YmccFamily::Unknown;}g_machineIdentity;
struct {HANDLE auraControl=-1;ULONG auraFeatureLen=64;}g_rog;
static std::mutex g_rogAuraMx;static constexpr BYTE kRogAuraReportId=0x5d;
static std::vector<std::vector<BYTE>> rogPackets,rogFeatures,msiPackets;static std::vector<json> logs;
static bool powerAllowed=true,writeOk=true,shortWrite=false,openOk=true;static int helperCalls=0,msiOpens=0,msiCloses=0;
static bool WriteFile(HANDLE,const void*data,DWORD size,DWORD*written,void*){const auto*p=static_cast<const BYTE*>(data);rogPackets.emplace_back(p,p+size);*written=writeOk?(shortWrite?size/2:size):0;return writeOk;}
static bool HidD_SetFeature(HANDLE,void*data,ULONG size){const auto*p=static_cast<BYTE*>(data);rogFeatures.emplace_back(p,p+size);return writeOk&&!shortWrite;}
struct MsiClawHidDiag {bool deviceFound=false;int devicesScanned=0,vendorMatches=0,openFailures=0;DWORD lastError=0;ULONG firmware=0x230;};
struct MsiClawWriteResult {bool ok=false;DWORD written=0,lastError=0;};
static HANDLE msiClawOpenCommandHid(MsiClawHidDiag& d){++msiOpens;d.deviceFound=true;return openOk?55:INVALID_HANDLE_VALUE;}
static MsiClawWriteResult msiClawWriteFrame(HANDLE,const BYTE*data,DWORD size){msiPackets.emplace_back(data,data+size);return {writeOk&&!shortWrite,writeOk?(shortWrite?size/2:size):0,writeOk?0u:5u};}
static void CloseHandle(HANDLE handle){if(handle!=55)throw std::runtime_error("unexpected close");++msiCloses;}
static void appendNativeLifecycleLog(const char*name,const json& row){logs.push_back({{"event",name},{"body",row}});}
static const char* ymccFamilyName(YmccFamily){return "memory-identity";}
static bool hardwareWriteAllowed(){return powerAllowed;}
static json ymccLightSetterApply(bool,const std::string&,BYTE,BYTE,BYTE,BYTE,BYTE,BYTE,int,int,const std::string&){++helperCalls;return {{"ok",true},{"backend","hc-light-setter"},{"effectApplied",true},{"brightnessApplied",true}};}
`+include('native/oem_lighting_operations.h')+enums+[
 'static bool machineIsRogAllyFamily()',
 'static const std::vector<std::string>& ymccDeviceClassLightingSet()',
 'static bool ymccDeviceClassSupportsLighting(',
 'static std::vector<BYTE> rogAuraMessage(',
 'static bool rogAuraWriteLocked(',
 'static RogAuraSpeed rogAuraSpeedFromPercent(',
 'static bool rogAuraApplyColor(',
 'static bool rogAuraApplyAmbilight(',
 'static bool rogAuraSetBrightness(',
 'static bool parseRgbHex('
].map(block).join('\n')+'\n'+main.slice(msiStart,msiEnd)+'\n'+block('static json msiClawApplyLed(')+String.raw`
static json execute(const json& a){auto ipc_on=[](const char*,auto handler){return handler;};auto callback=
`+handler+String.raw`
;return callback(a);}
static void reset(){rogPackets.clear();rogFeatures.clear();msiPackets.clear();logs.clear();helperCalls=0;msiOpens=0;msiCloses=0;powerAllowed=writeOk=openOk=true;shortWrite=false;}
int main(int argc,char**argv){std::ifstream f(argv[1]);json c;f>>c;int checks=0,models=0;std::vector<std::string>failures;
auto check=[&](bool ok,const std::string&id){++checks;if(!ok)failures.push_back(id);};
auto quiet=[&]{return rogPackets.empty()&&rogFeatures.empty()&&msiPackets.empty()&&helperCalls==0&&msiOpens==0;};
try{
for(const auto& row:c["domains"]["lighting"]["rows"]){++models;reset();const std::string cls=row["deviceClass"],id=row["deviceId"];g_machineIdentity.deviceClass=cls;
const bool rog=ymcc::lighting::usesRogAura(cls),msi=ymcc::lighting::usesMsiNativeHid(cls);g_machineIdentity.family=rog?YmccFamily::AsusRogAlly:msi?YmccFamily::MsiClaw:YmccFamily::Unknown;g_rog.auraControl=rog?17:-1;
auto result=execute({{"mode","solid"},{"color","#123456"},{"brightness",65}});
check(result.at("ok")==true&&result.at("capabilityId")==id&&result.at("physicalEffectVerified")==false,id+":unified-result");
if(rog){check(rogPackets.size()==3&&rogFeatures.size()==1&&msiPackets.empty()&&helperCalls==0,id+":actual-ROG-writer");check(rogPackets[0].size()==64&&rogPackets[0][0]==0x5d&&rogPackets[0][4]==0x12&&rogPackets[1][1]==0xb4&&rogPackets[2][1]==0xb5,id+":real-ROG-packets");check(rogFeatures[0].size()==64&&rogFeatures[0][1]==0xba,id+":real-brightness-feature");}
else if(msi){check(msiPackets.size()==4&&msiOpens==1&&msiCloses==1&&rogPackets.empty()&&helperCalls==0,id+":actual-MSI-writer");check(msiPackets[0].size()==64&&msiPackets[0][0]==15&&msiPackets[0][6]==2&&msiPackets[0][7]==0x4a&&msiPackets[0][13]==65&&msiPackets[0][14]==0x12,id+":real-MSI-packets");}
else check(helperCalls==1&&rogPackets.empty()&&msiPackets.empty(),id+":actual-helper-call");
reset();powerAllowed=false;check(execute({{"mode","solid"}}).at("ok")==false&&quiet(),id+":power-refusal-zero-io");
reset();check(execute({{"mode","unreviewed-effect"}}).at("ok")==false&&quiet(),id+":mode-refusal-zero-io");
reset();if(rog||msi){g_machineIdentity.family=YmccFamily::Unknown;check(execute({{"mode","solid"}}).at("reason")=="lighting-identity-mismatch"&&quiet(),id+":family-mismatch-zero-io");}
else {g_rog.auraControl=17;check(execute({{"mode","solid"}}).at("ok")==true&&rogPackets.empty()&&helperCalls==1,id+":cached-Aura-handle-does-not-steal-route");}
}
for(const std::string cls:{"HandheldCompanion.Devices.ROGAlly","HandheldCompanion.Devices.ClawCG3EM"}){g_machineIdentity.deviceClass=cls;g_machineIdentity.family=ymcc::lighting::usesRogAura(cls)?YmccFamily::AsusRogAlly:YmccFamily::MsiClaw;g_rog.auraControl=17;
reset();writeOk=false;auto failed=execute({{"mode","solid"}});check(failed.at("ok")==false&&failed.at("physicalEffectVerified")==false,cls+":write-failure-preserved");
reset();shortWrite=true;check(execute({{"mode","solid"}}).at("ok")==false,cls+":short-write-not-success");
reset();auto off=execute({{"enabled",false}});check(off.at("ok")==true,cls+":off-consumed");if(ymcc::lighting::usesRogAura(cls))check(rogFeatures[0][4]==0&&rogPackets[0][4]==0,cls+":off-black-and-zero-brightness");else check(msiPackets[0][13]==0&&msiPackets[0][14]==0,cls+":off-black-and-zero-brightness");
reset();auto ambient=execute({{"mode","ambilight"},{"color","#123456"},{"color2","#abcdef"}});check(ambient.at("ok")==true,cls+":ambilight-consumed");if(ymcc::lighting::usesRogAura(cls))check(rogPackets.size()==4&&rogPackets[0][2]==1&&rogPackets[3][2]==4,cls+":actual-four-zone-packets");else check(msiPackets[0][14]==0xab&&msiPackets[0][26]==0x12,cls+":actual-dual-color-zones");
}
reset();g_machineIdentity.deviceClass="Unreviewed.Vendor.ROGAlly";g_machineIdentity.family=YmccFamily::AsusRogAlly;g_rog.auraControl=17;check(execute({{"mode","solid"}}).at("ok")==false&&quiet(),"unregistered-model-with-cached-Aura-zero-io");
}catch(const std::exception&e){failures.push_back(e.what());}
json result={{"status",failures.empty()?"PASS":"FAIL"},{"checks",checks},{"models",models},{"failures",failures},{"actualRogAndMsiPacketConsumers",true},{"hardwareIo",false}};std::cout<<result.dump();return failures.empty()?0:1;}
`;
fs.writeFileSync(path.join(out,'actual.cpp'),cpp);const preamble=calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat');
fs.writeFileSync(path.join(out,'build.cmd'),preamble+`cl /nologo /std:c++20 /utf-8 /EHsc /MT "${out}/actual.cpp" /I"${repo}/deps/json" /Fo"${out}/actual.obj" /Fe"${out}/actual.exe"\r\nexit /b %errorlevel%\r\n`);
function run(exe,args,log){const r=spawnSync(exe,args,{cwd:out,encoding:'utf8',windowsHide:true,timeout:120000,maxBuffer:2*1024*1024});fs.writeFileSync(path.join(out,log),(r.stdout??'')+(r.stderr??''));if(r.status!==0)throw Error('Native lighting consumption failed: '+log);return r.stdout;}
run('cmd.exe',['/d','/c',path.join(out,'build.cmd')],'compile.log');const result=JSON.parse(run(path.join(out,'actual.exe'),[path.join(repo,'capabilities/hc-device-catalog.json')],'execute.log'));result.sourceHash=sourceHash;
if(sha(path.join(repo,'native/main.cpp'))!==sourceHash)throw Error('Native source changed during execution');fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result));
