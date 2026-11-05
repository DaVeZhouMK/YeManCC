import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';import {spawnSync} from 'node:child_process';import {createHash} from 'node:crypto';import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),args=process.argv.slice(2);
if(args.length!==2||args[0]!=='--out')throw Error('Usage --out evidence-directory');const out=path.resolve(args[1]);fs.mkdirSync(out,{recursive:true});
const main=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8').replace(/\r\n/g,'\n'),sourceHash=createHash('sha256').update(fs.readFileSync(path.join(repo,'native/main.cpp'))).digest('hex');
const signatures=['static const std::vector<std::string>& ymccDeviceClassLightingSet()','static const std::vector<std::string>& ymccDeviceClassSecondColorSet()','static bool ymccDeviceClassSupportsLighting(','static bool ymccDeviceClassSupportsSecondColor(','static std::vector<std::string> ymccDeviceClassLedEffects('];
function block(sig){const start=main.indexOf(sig),end=main.indexOf('\n}',start);if(start<0||end<0)throw Error('Missing actual function '+sig);return main.slice(start,end+2);}
const fixture=path.join(repo,'tools/fixtures/hc_lighting_catalog');
const include=file=>'#include '+JSON.stringify(file.replaceAll('\\','/'))+'\n';
const code=String.raw`#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
`+include(path.join(repo,'native/oem_lighting_operations.h'))+include(path.join(repo,'native/lighting_device_core_binding.h'))+include(path.join(fixture,'operations-before.h'))+include(path.join(fixture,'binding-before.h'))+'namespace before {\n'+fs.readFileSync(path.join(fixture,'native-before.inc'),'utf8')+'\n}\n'+signatures.map(block).join('\n')+String.raw`
int main(int argc,char**argv){std::ifstream f(argv[1]);json catalog;f>>catalog;int checks=0,models=0;std::vector<std::string>failures;
auto check=[&](bool ok,const std::string&id){++checks;if(!ok)failures.push_back(id);};
for(const auto& d:catalog["devices"]){++models;const std::string cls=d["consumerClass"];
const bool exactApex=cls=="HandheldCompanion.Devices.OneXPlayerApex";
check(ymccDeviceClassSupportsLighting(cls)==before::ymccDeviceClassSupportsLighting(cls),cls+":lighting");
if(cls=="HandheldCompanion.Devices.OneXPlayerX1Mini"||exactApex){
 check(before::ymccDeviceClassSupportsSecondColor(cls),cls+":legacy-source-declaration-reference");
 check(!ymccDeviceClassSupportsSecondColor(cls),cls+":exact-consumer-secondary-exclusion");
 bool sourceDeclared=false;for(const auto& flag:d["declared"]["Capabilities"]["minimum"])if(flag=="DynamicLightingSecondLEDColor")sourceDeclared=true;
 check(sourceDeclared,cls+":frozen-source-declaration-retained");
 bool found=false;for(const auto& row:catalog["domains"]["lighting"]["rows"])if(row["deviceClass"]==cls){found=true;check(row.value("sourceDeclaredSecondColor",false)&&!row.value("consumerSecondColor",true)&&!row.at("secondColor").get<bool>(),cls+":source-and-effective-metadata");}
 check(found,cls+":existing-lighting-row-retained");
}else check(ymccDeviceClassSupportsSecondColor(cls)==before::ymccDeviceClassSupportsSecondColor(cls),cls+":second-color");
check(ymccDeviceClassLedEffects(cls)==(exactApex?std::vector<std::string>{"solid","rainbow"}:before::ymccDeviceClassLedEffects(cls)),cls+":effect-order");
check(ymcc::lighting::presetImplemented(cls)==(exactApex?false:before::lighting::presetImplemented(cls)),cls+":preset-implementation");
check(ymcc::lighting::presetDeclaredButUnavailable(cls)==before::lighting::presetDeclaredButUnavailable(cls),cls+":preset-source-disabled");
if(ymccDeviceClassSupportsLighting(cls))check(ymcc::lighting::brightnessImplemented(cls)==before::lighting::brightnessImplemented(cls),cls+":existing-brightness");
for(const char* action:{"color","brightness","status","off","preset","unknown"})for(const char*mode:{"solid","rainbow","preset","wave","","SOLID"})
check(ymcc::lighting::useIndependentDeviceCore(cls,action,mode)==before::lighting::useIndependentDeviceCore(cls,action,mode),cls+":backend:"+action+":"+mode);
}
const std::string apex="HandheldCompanion.Devices.OneXPlayerApex",apexId="hc.device.OneXPlayerApex",x1="HandheldCompanion.Devices.OneXPlayerX1";
const auto* apexRoute=ymcc::lighting::resolve(apex);
check(apexRoute&&apexRoute->registered&&apexRoute->brightness&&!apexRoute->preset&&!apexRoute->declaredPresetWithoutTransport&&
 apexRoute->provider==ymcc::lighting::NativeProvider::Helper&&apexRoute->colorEffects==5&&apexRoute->fixedSpeed&&!apexRoute->presetSecondColor&&
 apexRoute->sourceAdapter==ymcc::lighting::SourceAdapter::ApexHidV2,"APEX-original-native-row-exact-effective-route");
check(ymcc::lighting::sourceAdapter(apex)==ymcc::lighting::SourceAdapter::ApexHidV2&&!ymcc::lighting::usesOneXVendorOwner(apex),"APEX-reviewed-source-adapter-uses-shared-leased-HID-owner");
check(ymcc::lighting::supportsColorEffect(apex,"solid")&&ymcc::lighting::supportsColorEffect(apex,"rainbow"),"APEX-only-reviewed-color-effects");
for(const char* mode:{"breathing","wave","wheel","gradient","ambilight","preset"})check(!ymcc::lighting::supportsColorEffect(apex,mode),std::string("APEX-unreviewed-effect-denied:")+mode);
check(!ymcc::lighting::validPreset(apex,"LEDPreset_OneXPlayerX1_Preset09")&&!ymcc::lighting::presetDeclaredButUnavailable(apex),"APEX-no-preset-declaration-or-serial-consumption");
check(before::ymccDeviceClassLedEffects(apex)==std::vector<std::string>{"solid","preset"}&&before::lighting::presetImplemented(apex),"APEX-old-X1-consumer-reference-remains-frozen");
check(catalog["devices"].size()==92&&catalog["domains"]["lighting"]["rows"].size()==47&&catalog["domains"]["lighting"]["extensionRows"].size()==4,"APEX-no-registration-count-growth");
bool foundApexSource=false,foundApexRow=false;
for(const auto& d:catalog["devices"])if(d["deviceId"]==apexId){
 foundApexSource=true;
 check(d["sourcePath"]=="deps/handheldcompanion-runtime/source/Devices/OneXPlayer/OneXPlayerApex.cs"&&d["sourceSha256"]=="c64af412f599b43d337be93f9860fac30cc85a578c5bed727d84ddfc02237e60"&&
  d["declared"]["DynamicLightingCapabilities"]["minimum"]==json::array({"LEDPreset","SolidColor"}),"APEX-frozen-source-and-declared-effects-retained");
 for(const auto& alias:d["aliases"]){const std::string actual=alias;
  check(ymcc::lighting::identityMatches(actual,apex)&&ymcc::lighting::identityMatches(apex,actual),actual+":APEX-canonical-same-device-id-accepted");
  check(!ymcc::lighting::identityMatches(actual,x1)&&!ymcc::lighting::identityMatches(actual,"hc.device.OneXPlayerX1"),actual+":actualAPEX-expectedX1-rejected");
  check(ymcc::lighting::sourceAdapter(actual)==ymcc::lighting::SourceAdapter::ApexHidV2,actual+":canonical-adapter-preserved");
 }
}
for(const auto& row:catalog["domains"]["lighting"]["rows"]){
 if(row["deviceId"]==apexId){foundApexRow=true;const auto& frozen=row.at("frozenConsumer");
  check(row["sourceAdapter"]=="onex-apex-hid-v2"&&row["brightness"]==true&&row["secondColor"]==false&&row["declaredEffects"]==json::array({"solid","rainbow"})&&
   row["presetDeclared"]==false&&row["presetAvailable"]==false&&row["statusAvailable"]==false&&row["fixedSpeed"]==true&&row["offAvailable"]==true,"APEX-exact-reviewed-effective-metadata");
  check(frozen["brightness"]==true&&frozen["secondColor"]==true&&frozen["declaredEffects"]==json::array({"solid","preset"})&&
   frozen["presetDeclared"]==true&&frozen["presetAvailable"]==true&&frozen["statusAvailable"]==true&&frozen["statusTransportDisabled"]==false,"APEX-frozen-consumer-facts-retained");
  for(const char* name:{"SetLedColor","SetLedBrightness","SetLedStatus","SetLEDPreset"})
   check(row["methodSources"][name]==frozen["methodSources"][name]&&row["methodSources"][name]["owner"]=="OneXPlayerX1",std::string("APEX-frozen-method-provenance:")+name);
 }else for(const auto& target:row["nativeAliasTargets"])check(target!=apexId,"APEX-no-incoming-alias:"+row["name"].get<std::string>());
}
check(foundApexSource&&foundApexRow,"APEX-existing-source-and-effective-row-both-retained");
for(const char* child:{"OneXPlayerX1AMD","OneXPlayerX1Intel","OneXPlayerX1Mini","OneXPlayerX1Pro"})
 check(ymcc::lighting::identityMatches(std::string("HandheldCompanion.Devices.")+child,x1),std::string("X1-other-child-alias-retained:")+child);
check(ymcc::lighting::kOneXPresets.size()==before::lighting::kOneXPresets.size(),"preset-count");
for(size_t i=0;i<ymcc::lighting::kOneXPresets.size();i++){const auto&a=ymcc::lighting::kOneXPresets[i];const auto&b=before::lighting::kOneXPresets[i];check(std::string(a.id)==b.id&&a.hcValue==b.hcValue&&std::string(a.label)==b.label,"preset-value-and-order");}
check(!ymcc::lighting::brightnessImplemented("Unreviewed.Vendor.ROGAlly"),"unknown-brightness-denied");
const std::string oxp3="HandheldCompanion.Devices.OneXPlayer3";
check(ymccDeviceClassSupportsLighting(oxp3)&&ymccDeviceClassSupportsSecondColor(oxp3),"OXP3-lighting-and-second-color-set");
check(ymcc::lighting::resolve(oxp3)!=nullptr&&std::string(ymcc::lighting::deviceId(oxp3))=="ymcc.device.OneXPlayer3","OXP3-route-and-capability-id");
const auto* oxp3Route=ymcc::lighting::resolve(oxp3);
check(oxp3Route&&oxp3Route->brightness&&oxp3Route->preset&&!oxp3Route->declaredPresetWithoutTransport&&oxp3Route->provider==ymcc::lighting::NativeProvider::Helper&&oxp3Route->colorEffects==3,"OXP3-route-flags");
check(ymcc::lighting::validPreset(oxp3,"LEDPreset_OneXPlayerX1_Preset09"),"OXP3-preset09-valid");
check(!ymcc::lighting::validPreset(oxp3,"LEDPreset_OneXPlayerX1_Preset08"),"OXP3-preset08-denied");
check(ymcc::lighting::validPreset("HandheldCompanion.Devices.OneXPlayerX1","LEDPreset_OneXPlayerX1_Preset08"),"frozen-preset08-retained");
check(ymcc::lighting::supportsColorEffect(oxp3,"solid")&&ymcc::lighting::supportsColorEffect(oxp3,"breathing")&&!ymcc::lighting::supportsColorEffect(oxp3,"rainbow"),"OXP3-color-effects");
check(!ymcc::lighting::usesRogAura(oxp3)&&!ymcc::lighting::usesMsiNativeHid(oxp3),"OXP3-not-native-vendor-route");
const std::string x2mini="HandheldCompanion.Devices.OneXPlayerX2MiniPro";
check(ymccDeviceClassSupportsLighting(x2mini)&&ymccDeviceClassSupportsSecondColor(x2mini),"X2MiniPro-source-bound-lighting-and-real-secondary-consumer");
const auto* x2route=ymcc::lighting::resolve(x2mini);
check(x2route&&x2route->brightness&&!x2route->preset&&x2route->declaredPresetWithoutTransport&&x2route->colorEffects==3&&x2route->fixedSpeed,"X2MiniPro-exact-route-effective-actions");
check(ymcc::lighting::sourceAdapter(x2mini)==ymcc::lighting::SourceAdapter::OneXX2MiniHid,"X2MiniPro-independent-source-adapter");
check(ymcc::lighting::identityMatches(x2mini,x2mini)&&!ymcc::lighting::identityMatches(x2mini,"HandheldCompanion.Devices.OneXPlayerX2"),"X2MiniPro-no-family-identity-fallback");
for(const char* mode:{"solid","breathing"})check(ymcc::lighting::supportsColorEffect(x2mini,mode)&&ymcc::lighting::useIndependentDeviceCore(x2mini,"color",mode),std::string("X2MiniPro-actual-independent-color:")+mode);
check(ymcc::lighting::useIndependentDeviceCore(x2mini,"brightness","")&&ymcc::lighting::useIndependentDeviceCore(x2mini,"off",""),"X2MiniPro-explicit-brightness-and-black-off");
for(const char* action:{"status","preset","unknown"})check(!ymcc::lighting::useIndependentDeviceCore(x2mini,action,"solid"),std::string("X2MiniPro-unimplemented-actions-denied:")+action);
for(const char* mode:{"rainbow","wave","","SOLID"})check(!ymcc::lighting::useIndependentDeviceCore(x2mini,"color",mode),std::string("X2MiniPro-unsupported-native-mode:")+mode);
check(!ymccDeviceClassSupportsLighting("HandheldCompanion.Devices.OneXPlayerX2")&&!ymccDeviceClassSupportsLighting(x2mini+" Air"),"unconsumed-X2-and-marketing-alias-not-admitted");
json result={{"status",failures.empty()?"PASS":"FAIL"},{"checks",checks},{"models",models},{"failures",failures},{"hardwareIo",false}};std::cout<<result.dump();return failures.empty()?0:1;}
`;
fs.writeFileSync(path.join(out,'actual.cpp'),code);const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
fs.writeFileSync(path.join(out,'build.cmd'),calibrationToolchainPreamble(vcvars)+`cl /nologo /std:c++20 /utf-8 /EHsc /MT "${out}/actual.cpp" /I"${repo}/deps/json" /Fo"${out}/actual.obj" /Fe"${out}/actual.exe"\r\nexit /b %errorlevel%\r\n`);
function run(exe,args,log){const r=spawnSync(exe,args,{cwd:out,encoding:'utf8',windowsHide:true,timeout:120000,maxBuffer:2*1024*1024});fs.writeFileSync(path.join(out,log),(r.stdout??'')+(r.stderr??''));if(r.status!==0)throw Error('Native parity failed: '+log);return r.stdout;}
run('cmd.exe',['/d','/c',path.join(out,'build.cmd')],'compile.log');const result=JSON.parse(run(path.join(out,'actual.exe'),[path.join(repo,'capabilities/hc-device-catalog.json')],'execute.log'));result.sourceHash=sourceHash;
if(createHash('sha256').update(fs.readFileSync(path.join(repo,'native/main.cpp'))).digest('hex')!==sourceHash)throw Error('Native source changed during parity run');
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result));
