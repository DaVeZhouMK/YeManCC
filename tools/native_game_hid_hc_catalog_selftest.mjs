import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
import {extract} from './fixtures/msi_owned_return_retired_io/source-binding.mjs';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const args=process.argv.slice(2);
const option=(name,fallback)=>{const index=args.indexOf(name);return index<0?fallback:args[index+1];};
const data=path.resolve(option('--data','D:/Codex/2026-10-16/ni/work/hc-all-family-shield-simulation-20261016'));
const ledgerPath=option('--ledger','G:/YeManCC-Work/Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/LegionMatchedClosure-20261018/FULL-CATALOG-LEDGER.json');
const runtimePath=path.resolve(option('--runtime','D:/Codex/2026-10-16/ni/outputs/shield-hc-fix-r23-20261018/hc-runtime-types.json'));
const output=path.resolve(option('--output','G:/YeManCC-Work/Build/Validation/NativeGameHidCaps-20261018/HC-Catalog'));
fs.mkdirSync(output,{recursive:true});
const hash=value=>crypto.createHash('sha256').update(value).digest('hex');
const bindings=[];
function read(file){const bytes=fs.readFileSync(file);bindings.push({file,bytes:bytes.length,sha256:hash(bytes)});return bytes.toString('utf8').replace(/^\uFEFF/,'');}
const json=file=>JSON.parse(read(file));
const frozen=json(path.join(data,'data-manifest.json'));
for(const row of frozen.files)assert.equal(hash(fs.readFileSync(row.destination)),row.sha256,'HC frozen source '+row.file);
const upstream=json(path.join(data,'extraction/upstream-devices.json'));
const pinned=json(path.join(data,'extraction/pinned-devices.json'));
const catalog=json(path.join(data,'all-family-catalog.json'));
const ledger=json(ledgerPath);
assert.equal(upstream.devices.length,102);assert.equal(upstream.devices.filter(row=>row.instantiable).length,90);assert.equal(pinned.devices.length,92);
for(const row of ledger.sourceFiles)assert.equal(hash(fs.readFileSync(row.file)),row.expectedSha256,'current reference source '+row.file);
const referenceRoot=path.dirname(path.dirname(ledger.sourceFiles[0].file));
for(const row of upstream.files){const actual=ledger.sourceFiles.find(entry=>entry.file.replaceAll('\\','/').endsWith('/Devices/'+row.file.replaceAll('\\','/')));assert(actual,'HC source binding '+row.file);assert.equal(actual.expectedSha256,row.sha256);}
const runtime=json(runtimePath);assert(runtime.metadataOnly && !runtime.constructorsExecuted);
assert.equal(hash(fs.readFileSync(runtime.path)),runtime.sha256,'current shipped HC runtime');
const runtimeNames=new Set(runtime.types.map(row=>row.name));
for(const row of pinned.devices)assert(runtimeNames.has(row.name),'frozen runtime lost device type '+row.name);
const identityFiles=['G:/YeManCC-Work/Build/Validation/HC-ModelCapabilities-20261017/pc80-identity/inputs.json','G:/YeManCC-Work/Build/Validation/HC-ModelCapabilities-20261017/new-model-inputs.json'];
const identities=identityFiles.flatMap(file=>json(file));
const modelRows=upstream.devices.map(model=>{
 const selected=catalog.models.find(row=>row.model===model.name);
 const reference=ledger.rows.find(row=>row.name===model.name);assert(reference,'reference type '+model.name);
 const identityCases=identities.filter(row=>row.hcClass===model.fullName || row.hcClass===reference.sourceClass || row.hcClass?.split('.').at(-1)===model.name);
 const factoryRoutes=upstream.identities.filter(row=>row.deviceClass===model.name);
 if(!identityCases.length)for(const route of factoryRoutes){
  const manufacturers=route.switches.find(row=>row.field==='ManufacturerName')?.values??[''];
  const products=route.switches.find(row=>row.field==='ProductName')?.values??[''];
  for(const manufacturer of manufacturers)for(const product of products)identityCases.push({id:model.name+'-source-factory-'+identityCases.length,manufacturer,product,systemProduct:'',processor:'',version:'',hcClass:model.fullName,sourceFactoryLine:route.line,sourceConditions:route.conditions});
 }
 return {name:model.name,sourceClass:model.fullName,canonicalSourceClass:reference.sourceClass,family:model.family,instantiable:model.instantiable,runtimePresent:runtimeNames.has(model.name),sourceChain:model.chain,sourceFile:model.file,sourceSha256:upstream.files.find(row=>row.file===model.file).sha256,identities:identityCases,gameCandidates:selected?.gameCandidates??[],filters:selected?.filterDeclarations??[],vendorControlIsGameAuthority:false,physicalAcceptance:false};
});
assert.equal(modelRows.filter(row=>row.runtimePresent).length,92);
fs.writeFileSync(path.join(output,'MODELS.json'),JSON.stringify(modelRows,null,2)+'\n');
const main=read(path.join(repo,'native/main.cpp')),pads=read(path.join(repo,'native/screen_touchpads.h'));
const extracted=[];
const blocks=[];
for(const name of ['hidHideUpper','hidHideIsVirtualIdentity','hidHideClassifyInterfaceRole','inputHostNormalizeAxis','focusIsolationNeutralContent','focusIsolationAssembleFrame'])blocks.push(extract(main,'function',name,'native/main.cpp',extracted));
const roleStart=main.indexOf('enum class HidHideIfaceRole : int {'),roleEnd=main.indexOf('\n};',roleStart)+3;
assert(roleStart>=0 && roleEnd>roleStart);
const role=main.slice(roleStart,roleEnd);blocks.unshift(role);extracted.push({symbol:'HidHideIfaceRole',sha256:hash(role)});
const contact=extract(pads,'struct','Contact','native/screen_touchpads.h',extracted),snapshot=extract(pads,'struct','Snapshot','native/screen_touchpads.h',extracted);
const padJsonStart=pads.indexOf('inline Json snapshotJson('),padJsonEnd=pads.indexOf('\n}',padJsonStart)+2;
assert(padJsonStart>=0 && padJsonEnd>padJsonStart);
const padJson=pads.slice(padJsonStart,padJsonEnd);extracted.push({symbol:'snapshotJson',sha256:hash(padJson)});
blocks.unshift('namespace ymcc::screenpads { using Json=nlohmann::json;\n'+contact+'\n'+snapshot+'\n'+padJson+'\n}',extract(main,'struct','FocusFrameContent','native/main.cpp',extracted));
const template=read(path.join(repo,'tools/fixtures/native_game_hid_caps/Catalog.cpp'));
const code=template.replace('@@PRODUCTION_BLOCKS@@',blocks.join('\n'));
fs.writeFileSync(path.join(output,'ActualCatalog.cpp'),code);
const headers=['native_foreground_source_policy.h','msi_native_foreground_isolation.h','controller_persona.h'];
for(const header of headers){const original=path.join(repo,'native',header);read(original);fs.copyFileSync(original,path.join(output,header));}
const results=[];
for(const [configuration,flags] of [['Release',['/O2','/MD']],['Checked',['/Od','/RTC1','/MDd']]]){
 const directory=path.join(output,configuration);fs.mkdirSync(directory,{recursive:true});
 const vcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
 const build=calibrationToolchainPreamble(vcvars)+`cl /nologo /std:c++20 /EHsc /utf-8 /W4 ${flags.join(' ')} "${path.join(output,'ActualCatalog.cpp')}" /I"${path.join(repo,'deps/json')}" /I"${output}" /Fo"${path.join(directory,'Fixture.obj')}" /Fe"${path.join(directory,'Fixture.exe')}"\r\nexit /b %errorlevel%\r\n`;
 fs.writeFileSync(path.join(directory,'build.cmd'),build);
 const compiled=spawnSync('cmd.exe',['/d','/c',path.join(directory,'build.cmd')],{cwd:directory,encoding:'utf8',timeout:120000});fs.writeFileSync(path.join(directory,'build.log'),(compiled.stdout??'')+(compiled.stderr??''));assert.equal(compiled.status,0,compiled.stdout+compiled.stderr);
 const run=spawnSync(path.join(directory,'Fixture.exe'),[path.join(output,'MODELS.json'),path.join(directory,'RESULT.json')],{cwd:directory,encoding:'utf8',timeout:60000});fs.writeFileSync(path.join(directory,'run.log'),(run.stdout??'')+(run.stderr??''));assert.equal(run.status,0,run.stdout+run.stderr);
 const report=JSON.parse(fs.readFileSync(path.join(directory,'RESULT.json'),'utf8'));assert(report.passed);results.push({configuration,...report,rows:undefined});console.log(configuration+': '+report.types+' types / '+report.checks+' checks PASS');
}
for(const row of bindings)assert.equal(hash(fs.readFileSync(row.file)),row.sha256,'source stable '+row.file);
const report={passed:true,sourceTypes:102,concreteTypes:90,parentTypes:12,currentRuntimeDeviceTypes:92,currentRuntimeConcreteTypes:pinned.devices.filter(row=>row.instantiable).length,referenceOnlyTypes:modelRows.filter(row=>!row.runtimePresent).map(row=>row.name),frozenSourceFiles:frozen.files.length,currentReferenceSourceFiles:ledger.sourceFiles.length,families:[...new Set(modelRows.map(row=>row.family))],configurations:results,bindings,extracted,modelRows,hardwareAccess:false,productMainExecuted:false,hcDllExecuted:false,limitations:['native/OFF authority remains limited to current admitted MSI/ROG/GPD roots; other families are not newly granted device writes','all models use common virtual semantic neutral tests; this does not establish physical source presence or external game consumption','current runtime and reference-only model types are recorded separately']};
fs.writeFileSync(path.join(output,'RESULT.json'),JSON.stringify(report,null,2)+'\n');
