import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
process.chdir(repo);
const args=process.argv.slice(2), option=name=>{const i=args.indexOf(name);return i<0?null:args[i+1]};
const out=path.resolve(option('--out')||path.join(repo,'../../../Build/Validation/Pc80-Software-20261014/identity'));fs.mkdirSync(out,{recursive:true});
const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const mainFile=option('--main')||'native/main.cpp',mainHash=hash(mainFile);
const run=(label,exe,args)=>{const r=spawnSync(exe,args,{cwd:repo,encoding:'utf8',windowsHide:true,timeout:120000,maxBuffer:8*1024*1024});fs.writeFileSync(path.join(out,label+'.log'),(r.stdout||'')+(r.stderr||''));assert.equal(r.status,0,label+' '+r.stderr);return r.stdout};
run('inventory-build','dotnet',['build','tools/PcCapabilityAudit/PcCapabilityAudit.csproj','--nologo','-v:q']);
run('inventory','dotnet',['tools/PcCapabilityAudit/bin/Debug/net10.0/PcCapabilityAudit.dll','deps/handheldcompanion-runtime/source/Devices',path.join(out,'inventory.json')]);
const inventory=JSON.parse(fs.readFileSync(path.join(out,'inventory.json')));
const main=fs.readFileSync(mainFile,'utf8');
const hc=fs.readFileSync('deps/handheldcompanion-runtime/source/Devices/IDevice.cs','utf8');
function block(text,marker,from=0){const start=text.indexOf(marker,from);assert(start>=0,marker);const open=text.indexOf('{',start);const tokens=/"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\/\/[^\r\n]*|\/\*[\s\S]*?\*\//g;const mask=text.replace(tokens,m=>m.replace(/[^\r\n]/g,' '));let depth=0;for(let i=open;i<mask.length;i++){if(mask[i]==='{')depth++;if(mask[i]==='}'&&!--depth)return text.slice(start,i+1)}throw Error('unclosed '+marker)}
const switchBody=block(hc,'switch (ManufacturerName)',hc.indexOf('public static IDevice GetCurrent()'));
const modelNames=[...new Set([...switchBody.matchAll(/new\s+(\w+)\(/g)].map(m=>m[1]))];
const cs=`using System.Text.Json;\nvar rows=JsonSerializer.Deserialize<List<Input>>(File.ReadAllText(args[0]))!;\nvar output=rows.Select(row=> new {row.id,deviceClass=Resolve(row)});Console.WriteLine(JsonSerializer.Serialize(output));
string Resolve(Input row){IDevice? device=null;var ManufacturerName=row.manufacturer.ToUpperInvariant();var ProductName=row.product;var SystemName=row.systemProduct;var SystemModel=row.systemProduct;var Processor=row.processor;var Version=row.version;var NumberOfCores=row.cores;${switchBody}\nreturn device is AsusDevice ? "ASUS" : device?.GetType().Name ?? "DefaultDevice";}
record Input(string id,string manufacturer,string product,string systemProduct,string processor,string version,int cores);class IDevice{}\n${modelNames.map(n=>`class ${n}:IDevice{}`).join('\n')}`;
fs.writeFileSync(path.join(out,'Program.cs'),cs);
fs.writeFileSync(path.join(out,'IdentityOracle.csproj'),'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup></Project>');
const fields={ManufacturerName:'manufacturer',ProductName:'product',SystemName:'systemProduct',SystemModel:'systemProduct',Processor:'processor',Version:'version',NumberOfCores:'cores'};
const cases=[];
for(const entry of inventory.identities){if(entry.switches.length===0)continue;let rows=[{manufacturer:'',product:'',systemProduct:'',processor:'',version:'',cores:8}];
 for(const sw of entry.switches){assert(fields[sw.field],sw.field);const vals=[...sw.values,...(sw.hasDefault?['__unknown__']:[])];assert(vals.length,'empty switch section');rows=rows.flatMap(row=>vals.map(v=>({...row,[fields[sw.field]]:sw.field==='NumberOfCores'?Number(v):v})));}
 if(entry.conditions.some(x=>x.includes('4500U')))rows=rows.flatMap(row=>['AMD Ryzen 5 4500U','AMD Ryzen 7 4800U'].map(processor=>({...row,processor})));
 for(const row of rows)cases.push({...row,id:entry.deviceClass+'-'+cases.length});
}
for(const manufacturer of [...new Set(cases.map(x=>x.manufacturer))])cases.push({id:'unknown-'+manufacturer,manufacturer,product:'YMCC-UNKNOWN-BOARD',systemProduct:'YMCC-UNKNOWN-MODEL',processor:'Unknown CPU',version:'Unknown',cores:8});

const aliases=[
 {id:'deck-jupiter-system',manufacturer:'VALVE',product:'4030',systemProduct:'Jupiter',expected:'SteamDeck'},
 {id:'deck-galileo-system',manufacturer:'VALVE',product:'4030',systemProduct:'Galileo',expected:'SteamDeck'},
 {id:'zotac-board-only',product:'G0A1W',expected:'GamingZone'},
 {id:'minisforum-board-only',product:'HPPAC',expected:'MinisforumV3'},
 {id:'minisforum-system-only',systemProduct:'HPPAC',expected:'MinisforumV3'},
 {id:'ayaneo-slide-name',manufacturer:'AYANEO',product:'SLIDE',expected:'AYANEOSlide'},
 {id:'loki-no-cpu',manufacturer:'AYN',product:'Loki Max',expected:'LokiMax6800U'},
 {id:'gpd-win4-no-cpu',manufacturer:'GPD',product:'G1618-04',expected:'GPDWin4'},
 {id:'gpd-mini-no-cpu',manufacturer:'GPD',product:'G1617-01',expected:'GPDWinMini'},
 {id:'msi-g3e-board-first',manufacturer:'MICRO-STAR INTERNATIONAL CO., LTD.',product:'MS-1T91',systemProduct:'Claw A8',expected:'ClawCG3EM'},
];
for(const c of aliases)cases.push({manufacturer:'',product:'',systemProduct:'',processor:'',version:'',cores:8,...c});
for(const manufacturer of [...new Set(cases.map(x=>x.manufacturer))])cases.push({id:'empty-'+manufacturer,manufacturer,product:'',systemProduct:'',processor:'',version:'',cores:8});
cases.push({id:'empty-dmi',manufacturer:'',product:'',systemProduct:'',processor:'',version:'',cores:0});

fs.writeFileSync(path.join(out,'inputs.json'),JSON.stringify(cases,null,2));
run('oracle-build','dotnet',['build',path.join(out,'IdentityOracle.csproj'),'--nologo','-v:q']);
const expected=JSON.parse(run('oracle','dotnet',[path.join(out,'bin/Debug/net10.0/IdentityOracle.dll'),path.join(out,'inputs.json')]));
// Only the factory switch executes in the oracle. Stubs have no constructors or HC dependencies.
const oracleExpected=structuredClone(expected);
for(let i=0;i<cases.length;i++){if(cases[i].expected)expected[i].deviceClass=cases[i].expected;cases[i].hcClass='HandheldCompanion.Devices.'+expected[i].deviceClass;}
fs.writeFileSync(path.join(out,'inputs.json'),JSON.stringify(cases,null,2));
const catalogCode=main.slice(main.indexOf('enum class OemKeyForm : int'),main.indexOf('// Table-driven unit test for ymccResolveFamily'));
assert(catalogCode.includes('oemCatalogForFamily'));
const fixture=`#include <algorithm>\n#include <cctype>\n#include <fstream>\n#include <iostream>\n#include <string>\n#include <vector>\n#include "json.hpp"\nusing json=nlohmann::json;\n${block(main,'enum class YmccFamily : int')} ;\n${block(main,'struct MachineIdentity {')};\n${block(main,'static const char* ymccFamilyName(')}\n${block(main,'static std::string ymccDmiNormalize(')}\n${block(main,'static void ymccResolveFamily(MachineIdentity& id)')}\n${block(main,'static const std::vector<std::string>& ymccDeviceClassLightingSet()')}\n${block(main,'static const std::vector<std::string>& ymccDeviceClassSecondColorSet()')}\n${block(main,'static bool ymccDeviceClassSupportsLighting(')}\n${block(main,'static bool ymccDeviceClassSupportsSecondColor(')}\n${block(main,'static std::vector<std::string> ymccDeviceClassLedEffects(')}\n${catalogCode}\n${block(main,'enum class OemChordProfileId : int')};\n${block(main,'static int oemChordProfileIdFor(')}\njson catalog(YmccFamily family,const std::string& cls){auto c=oemCatalogForFamily(family,cls);json keys=json::array();for(int i=0;i<c.count;i++)keys.push_back({c.keys[i].keyId,c.keys[i].backIndex,(int)c.keys[i].form,c.keys[i].clickOnly});return keys;}\n${block(main,'static bool ymccParseAxisToken(')}\n${block(main,'static bool ymccSplitAxisRecipe(')}\n${block(main,'static bool ymccApplyNamedImuMatrix(',main.indexOf('static bool ymccSplitAxisRecipe('))}\nint main(int argc,char**argv){std::ifstream f(argv[1]);json inputs;f>>inputs;json rows=json::array();for(auto&j:inputs){MachineIdentity id;id.resolved=true;id.deviceClass="stale";id.inputHidKey="stale";id.gyroMatrixId="stale";id.accelMatrixId="stale";id.family=YmccFamily::MsiClaw;id.manufacturer=ymccDmiNormalize(j["manufacturer"]);id.product=ymccDmiNormalize(j["product"]);id.systemProduct=ymccDmiNormalize(j["systemProduct"]);id.processor=ymccDmiNormalize(j["processor"]);id.systemVersion=ymccDmiNormalize(j["version"]);ymccResolveFamily(id);double gx=2,gy=3,gz=5,ax=2,ay=3,az=5;ymccApplyNamedImuMatrix(id.gyroMatrixId,2,3,5,gx,gy,gz);ymccApplyNamedImuMatrix(id.accelMatrixId,2,3,5,ax,ay,az);rows.push_back({{"id",j["id"]},{"resolved",id.resolved},{"keys",catalog(id.family,id.deviceClass)},{"expectedKeys",catalog(id.family,j["hcClass"])},{"profile",oemChordProfileIdFor(id.family,id.deviceClass)},{"expectedProfile",oemChordProfileIdFor(id.family,j["hcClass"])},{"deviceClass",id.deviceClass},{"family",ymccFamilyName(id.family)},{"gyro",id.gyroMatrixId},{"accel",id.accelMatrixId},{"hid",id.inputHidKey},{"gyroVector",{gx,gy,gz}},{"accelVector",{ax,ay,az}},{"lighting",ymccDeviceClassSupportsLighting(id.deviceClass)},{"secondColor",ymccDeviceClassSupportsSecondColor(id.deviceClass)},{"effects",ymccDeviceClassLedEffects(id.deviceClass)}});}std::cout<<rows.dump();}`;
fs.writeFileSync(path.join(out,'native.cpp'),fixture);
fs.writeFileSync(path.join(out,'build.cmd'),calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')+`cl /nologo /std:c++20 /utf-8 /EHsc /MT "${out}/native.cpp" /I"${repo}/deps/json" /Fo"${out}/native.obj" /Fe"${out}/native.exe"\r\nexit /b %errorlevel%\r\n`);
run('native-build','cmd.exe',['/d','/c',path.join(out,'build.cmd')]);
const actual=JSON.parse(run('native',path.join(out,'native.exe'),[path.join(out,'inputs.json')]));
const diffs=expected.map((exp,i)=>({input:cases[i],hc:exp.deviceClass,ymcc:actual[i].deviceClass.split('.').at(-1)||'DefaultDevice',actual:actual[i]})).filter(x=>x.hc!==x.ymcc);

function expectedMatrix(cls,kind){
 const d=inventory.devices.find(x=>x.name===cls);let axis=[1,1,1],swap={X:'X',Y:'Y',Z:'Z'};
 for(const n of d?.chain||[]){const e=inventory.devices.find(x=>x.name===n);for(const prop of e?.properties||[]){if(prop.field!==kind)continue;axis=[1,1,1];swap={X:'X',Y:'Y',Z:'Z'};const a=prop.value.match(/Axis\s*=\s*new Vector3\(([^)]+)\)/);if(a)axis=a[1].split(',').map(x=>Number(x.trim().replace(/f$/,'')));for(const m of prop.value.matchAll(/'([XYZ])'\s*,\s*'([XYZ])'/g))swap[m[1]]=m[2];}}
 const input={X:2,Y:3,Z:5},remap={};for(const k of ['X','Y','Z'])remap[swap[k]]=input[k];return ['X','Y','Z'].map((k,i)=>axis[i]*remap[k]);
}
const matrixDiffs=[];for(let i=0;i<expected.length;i++){if(['DefaultDevice','ASUS'].includes(expected[i].deviceClass))continue;for(const [kind,key] of [['GyroMatrix','gyroVector'],['AcceleroMatrix','accelVector']]){const value=expectedMatrix(expected[i].deviceClass,kind);if(JSON.stringify(value)!==JSON.stringify(actual[i][key]))matrixDiffs.push({id:cases[i].id,device:expected[i].deviceClass,kind,expected:value,actual:actual[i][key]});}}


// Native class names intentionally collapse CPU/year variants; HC hosts retain exact factory types.
const normalized={ASUS:'DefaultDevice',LokiMax6600U:'AynLoki',LokiMax6800U:'AynLoki',AYANEO2021Pro:'AYANEO2021',AYANEONEXTLite4500U:'AYANEONEXTLite',
 GPDWinMini_7640U:'GPDWinMini',GPDWinMini_8840U:'GPDWinMini',GPDWinMini_HX370:'GPDWinMini',
 GPDWin4_2023_7640U:'GPDWin4',GPDWin4_2023:'GPDWin4',GPDWin4_2024_8640U:'GPDWin4',GPDWin4_2024:'GPDWin4',GPDWin4_2024_HX370:'GPDWin4',
 GPDWinMax2_2022_6800U:'GPDWinMax2',GPDWinMax2_2023_7640U:'GPDWinMax2',GPDWinMax2_2023_7840U:'GPDWinMax2',GPDWinMax2_2024_8640U:'GPDWinMax2',GPDWinMax2_2024_8840U:'GPDWinMax2',GPDWinMax2_2024_HX370:'GPDWinMax2'};
const effectNames={SolidColor:'solid',Breathing:'breathing',Rainbow:'rainbow',Wave:'wave',Wheel:'wheel',Gradient:'gradient',Ambilight:'ambilight',LEDPreset:'preset'};
const failures=[],check=(ok,message)=>{if(!ok)failures.push(message)};
const covered=[...new Set(oracleExpected.map(x=>x.deviceClass))];
const factoryClasses=inventory.devices.filter(d=>d.instantiable).map(d=>d.name).sort();
check(JSON.stringify([...covered].sort())===JSON.stringify(factoryClasses),'factory coverage must match current locked HC exactly');
check(covered.length===80,'scope is current 80-class HC baseline');
check(matrixDiffs.length===0,'matrix mismatches: '+JSON.stringify(matrixDiffs));
for(let i=0;i<cases.length;i++){
 const c=cases[i],cls=expected[i].deviceClass,a=actual[i],nativeClass=a.deviceClass.split('.').at(-1)||'DefaultDevice';
 check(nativeClass===(normalized[cls]||cls),c.id+': incorrect native class '+nativeClass+' for '+cls);
 check(JSON.stringify(a.keys)===JSON.stringify(a.expectedKeys),c.id+': normalized OEM key catalog differs');
 check(a.profile===a.expectedProfile,c.id+': normalized OEM profile differs');
 if(['DefaultDevice','ASUS'].includes(cls)){
  check(!a.resolved&&!a.hid&&!a.gyro&&!a.accel&&!a.keys.length&&!a.lighting&&!a.secondColor&&!a.effects.length,c.id+': unknown identity retained capabilities');continue;
 }
 check(a.resolved&&a.hid,c.id+': known identity lost binding');
 const d=inventory.devices.find(x=>x.name===cls),flags=d.declared.Capabilities.minimum;
 const lighting=flags.includes('DynamicLighting');
 check(a.lighting===lighting,c.id+': lighting capability mismatch');
 check(a.secondColor===(lighting&&flags.includes('DynamicLightingSecondLEDColor')),c.id+': second-color gate mismatch');
 const effects=lighting?d.declared.DynamicLightingCapabilities.minimum.map(x=>{check(effectNames[x],c.id+': unmapped LED flag '+x);return effectNames[x]}).sort():[];
 check(JSON.stringify([...a.effects].sort())===JSON.stringify(effects),c.id+': LED effects mismatch '+JSON.stringify({actual:a.effects,expected:effects}));
}
check(hash(mainFile)===mainHash,'native source changed while test was running');
const result={ok:failures.length===0,cases:cases.length,hcCovered:covered.length,covered,hardwareWrites:false,HCAssemblyLoaded:false,sourceSha256:mainHash,normalized,aliases,failures,diffs,matrixDiffs,expected,actual};
fs.writeFileSync(path.join(out,'comparison.json'),JSON.stringify(result,null,2));
console.log(JSON.stringify({ok:result.ok,cases:cases.length,hcCovered:covered.length,normalizedCases:diffs.length,matrixDifferences:matrixDiffs.length,failures},null,2));
if(failures.length)process.exitCode=1;
