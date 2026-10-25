import {buildArtifactManifest,MANIFEST_FILE} from './hc_capability_catalog_integrity.mjs';
import {buildControllerConfigurationArtifacts} from './lib/hc_controller_configuration_catalog.mjs';
import {buildControllerFeedbackArtifacts} from './lib/hc_controller_feedback_catalog.mjs';
import {buildControllerMotionArtifacts} from './lib/hc_controller_motion_catalog.mjs';
import {buildPerformanceMetadata} from './lib/hc_performance_metadata.mjs';
import {buildControllerArtifacts} from './lib/hc_controller_catalog.mjs';
import {controllerInventoryErrors} from './lib/hc_controller_inventory.mjs';
import {buildMotionArtifacts} from './lib/hc_motion_catalog.mjs';
import {buildBatteryArtifacts} from './lib/hc_battery_catalog.mjs';
import {buildLightingArtifacts} from './lib/hc_lighting_catalog.mjs';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const sha=b=>createHash('sha256').update(b).digest('hex');
const json=f=>JSON.parse(fs.readFileSync(f,'utf8').replace(/^\uFEFF/,''));
const args=process.argv.slice(2);let check=false,out=null,generatedRoot=repo;
for(let i=0;i<args.length;i++){if(args[i]==='--check')check=true;else if(args[i]==='--out'&&args[i+1])out=path.resolve(args[++i]);else if(args[i]==='--generated-root'&&args[i+1])generatedRoot=path.resolve(args[++i]);else throw Error('Usage: [--check] [--out scratch-directory] [--generated-root output-root]');}
const scratch=out??fs.mkdtempSync(path.join(os.tmpdir(),'ymcc-hc-catalog-'));fs.mkdirSync(scratch,{recursive:true});
const baseDir=path.join(repo,'deps/handheldcompanion-runtime'),base=json(path.join(baseDir,'HC-BASELINE.json'));
const sourceIndex=fs.readFileSync(path.join(baseDir,base.sourceFilesIndex));
if(sha(sourceIndex)!==base.sourceFilesIndexSha256.toLowerCase())throw Error('FROZEN_SOURCE_INDEX_CHANGED');
const expected=new Map(sourceIndex.toString('utf8').trim().split(/\r?\n/).map(line=>{const m=line.match(/^([A-Fa-f0-9]{64})  (.+)$/);if(!m||m[2].split('/').includes('..'))throw Error('INVALID_FROZEN_SOURCE_INDEX');return [m[2],m[1].toLowerCase()];}));
if(expected.size!==base.sourceFileCount)throw Error('FROZEN_SOURCE_COUNT_CHANGED');
const profile=json(path.join(repo,'deps/ymcc-device-core-runtime/profile.json'));
const runtimeRoot=path.resolve(repo,profile.sourceDirectory),manifestPath=path.join(runtimeRoot,'HandheldCompanion.runtime.json'),manifestBytes=fs.readFileSync(manifestPath);
if(sha(manifestBytes)!==profile.sourceManifestSha256.toLowerCase())throw Error('UPSTREAM_RUNTIME_MANIFEST_CHANGED');
const manifest=JSON.parse(manifestBytes.toString('utf8').replace(/^\uFEFF/,''));
if(manifest.runtimeId!==profile.sourceRuntimeId)throw Error('UPSTREAM_RUNTIME_ID_CHANGED');
const hcPath=path.join(runtimeRoot,'HandheldCompanion.dll'),hcHash=sha(fs.readFileSync(hcPath));
if(hcHash!==manifest.files.find(f=>f.path==='HandheldCompanion.dll')?.sha256.toLowerCase())throw Error('UPSTREAM_HC_BINARY_CHANGED');
const targets=path.join(scratch,'exclude-original-build.targets');
fs.writeFileSync(targets,'<Project><Target Name="ExcludeOriginalWorkspaceOutputs" BeforeTargets="CoreCompile"><ItemGroup><Compile Remove="$(MSBuildProjectDirectory)/bin/**/*.cs"/><Compile Remove="$(MSBuildProjectDirectory)/obj/**/*.cs"/></ItemGroup></Target></Project>');
function run(argv){const r=spawnSync('dotnet',argv,{cwd:repo,encoding:'utf8',windowsHide:true,timeout:120000,maxBuffer:4*1024*1024});if(r.status!==0)throw Error('CATALOG_EXTRACTOR_FAILED: '+(r.stdout??'')+(r.stderr??'')+(r.error??''));return r.stdout;}
run(['build','tools/PcCapabilityAudit/PcCapabilityAudit.csproj','--artifacts-path',path.join(scratch,'build'),'-p:CustomAfterMicrosoftCommonTargets='+targets,'--nologo','-v:q']);
const dll=path.join(scratch,'build/bin/PcCapabilityAudit/debug/PcCapabilityAudit.dll'),inventoryPath=path.join(scratch,'inventory.json');
run([dll,path.join(baseDir,'source/Devices'),inventoryPath,hcPath]);
const inventory=json(inventoryPath);if(inventory.issues.length||!inventory.devices.length||!inventory.devices.some(d=>d.instantiable))throw Error('INVALID_SOURCE_INVENTORY');
const controllerInventoryPath=path.join(scratch,'controllers-inventory.json');
run([dll,'--controllers',path.join(baseDir,'source/Controllers'),controllerInventoryPath]);
const controllerInventory=json(controllerInventoryPath);
const controllerErrors=controllerInventoryErrors(json(path.join(repo,'capabilities/CONTROLLER-CONSUMPTION.json')),controllerInventory);
if(controllerErrors.length)throw Error('INVALID_CONTROLLER_SOURCE_INVENTORY: '+controllerErrors.join(';'));
// The current source and its contract can change together; neither replaces the authenticated frozen index.
const frozenControllers=new Map([...expected].filter(([file])=>file.startsWith('Controllers/')&&file.endsWith('.cs')));
const seenControllers=new Set();
for(const f of controllerInventory.files){
 if(!frozenControllers.has(f.file))throw Error('FROZEN_CONTROLLER_SOURCE_UNEXPECTED: '+f.file);
 if(seenControllers.has(f.file))throw Error('FROZEN_CONTROLLER_SOURCE_DUPLICATE: '+f.file);
 seenControllers.add(f.file);
 if(frozenControllers.get(f.file)!==f.sha256)throw Error('FROZEN_CONTROLLER_SOURCE_CHANGED: '+f.file);
}
for(const file of frozenControllers.keys())if(!seenControllers.has(file))throw Error('FROZEN_CONTROLLER_SOURCE_MISSING: '+file);
if(controllerInventory.fileCount!==frozenControllers.size||controllerInventory.files.length!==frozenControllers.size)throw Error('FROZEN_CONTROLLER_SOURCE_COUNT_CHANGED');
for(const f of inventory.files){if(expected.get('Devices/'+f.file)!==f.sha256)throw Error('FROZEN_DEVICE_SOURCE_CHANGED: '+f.file);}
if(inventory.runtime.sha256!==hcHash||inventory.runtime.assemblyLoaded!==false)throw Error('RUNTIME_METADATA_IDENTITY_MISMATCH');
const runtimeTypes=new Map(inventory.runtime.types.map(t=>[t.name,t.fullName]));
const devices=inventory.devices.map(d=>{const runtimeType=runtimeTypes.get(d.name);return {
 deviceId:'hc.device.'+d.name, name:d.name, family:d.family, factoryInstantiable:d.instantiable,
 sourceType:d.fullName, runtimeType:runtimeType??null, consumerClass:'HandheldCompanion.Devices.'+d.name,
 aliases:[...new Set(['hc.device.'+d.name,d.fullName,runtimeType,'HandheldCompanion.Devices.'+d.name].filter(Boolean))].sort(),
 sourcePath:'deps/handheldcompanion-runtime/source/Devices/'+d.file, sourceSha256:inventory.files.find(f=>f.file===d.file).sha256,
 declarationSha256:d.sourceSha256, inheritance:d.chain, declared:d.declared,
 operations:d.operations, parameters:d.properties, methods:d.methods, oemChords:d.oemChords,
 evidenceLevel:'source-declarations-and-runtime-type-metadata; availability-is-consumer-owned'
};});
if(devices.some(d=>d.factoryInstantiable&&!d.runtimeType))throw Error("FROZEN_FACTORY_TYPE_MISSING_FROM_PINNED_RUNTIME");
const extras=inventory.runtime.types.filter(t=>!devices.some(d=>d.name===t.name));
const catalog={schemaVersion:1,sourceBaseline:base.baselineId,sourceProjectVersion:base.upstreamProjectVersion,sourceFilesIndexSha256:sha(sourceIndex),sourceFiles:inventory.files,
 runtimeAliasSource:{runtimeId:manifest.runtimeId,sha256:hcHash,assemblyVersion:inventory.runtime.assemblyVersion,assemblyLoaded:false},
 scope:{sourceDeviceClasses:devices.length,factoryInstantiableClasses:devices.filter(d=>d.factoryInstantiable).length,runtimeDeviceClasses:runtimeTypes.size},
 runtimeOnlyTypes:extras,devices};
const capabilities=['FanControl','FanOverride','DynamicLighting','DynamicLightingBrightness','DynamicLightingSecondLEDColor','BatteryChargeLimit','BatteryChargeLimitPercent','BatteryBypassCharging','OEMCPU','OEMGPU'];
const effects=['SolidColor','Breathing','Rainbow','Wave','Wheel','Gradient','Ambilight','LEDPreset'];
function bits(values,names,type){for(const v of values)if(!names.includes(v))throw Error('UNREVIEWED_SOURCE_FLAG: '+v);return values.length?values.map(v=>type+'.'+v).join(' | '):type+'.None';}
const q=JSON.stringify;
const cs=['// Generated by tools/hc_capability_catalog_generate.mjs. Do not edit.','// Source flags are declarations; registered consumers own runtime admission.','using System;','namespace YeManFanBackend.Capabilities;','[Flags] public enum HcSourceCapability : ulong { None = 0, '+capabilities.map((n,i)=>n+' = '+(2**i)+'UL').join(', ')+' }','[Flags] public enum HcSourceLedEffect : ulong { None = 0, '+effects.map((n,i)=>n+' = '+(2**i)+'UL').join(', ')+' }','public static partial class HcDeviceCapabilityCatalog','{','    public const string SourceBaseline = '+q(base.baselineId)+';','    public const string RuntimeAliasSha256 = '+q(hcHash)+';','    private static readonly HcDeviceDescriptor[] GeneratedDevices = {',
 ...devices.map(d=>'        new('+[q(d.deviceId),q(d.runtimeType??d.sourceType),q(d.consumerClass),q(d.family),q(d.sourcePath),q(d.sourceSha256),String(d.factoryInstantiable),bits(d.declared.Capabilities.minimum,capabilities,'HcSourceCapability'),bits(d.declared.Capabilities.possible,capabilities,'HcSourceCapability'),bits(d.declared.DynamicLightingCapabilities.minimum,effects,'HcSourceLedEffect'),bits(d.declared.DynamicLightingCapabilities.possible,effects,'HcSourceLedEffect')].join(', ')+'),'),
 '    };','    private static readonly string[][] GeneratedAliases = {',...devices.map(d=>'        new[] { '+d.aliases.map(q).join(', ')+' },'),'    };','}',''].join('\n');
const mask=(values,names)=>values.reduce((bits,name)=>bits+2**names.indexOf(name),0);
const nativeAliases=devices.flatMap((d,i)=>d.aliases.map(alias=>({alias,index:i}))).sort((a,b)=>a.alias<b.alias?-1:a.alias>b.alias?1:0);
const nativeCatalog=['// Generated from the same frozen HC identity and declaration catalog as C#.','#pragma once','#include <array>','#include <cstddef>','#include <cstdint>','#include <string_view>','namespace ymcc::capabilities {','struct Device {std::string_view deviceId, factoryType, consumerClass; bool factoryInstantiable; std::uint64_t declaredMinimum, declaredPossible;};','struct Alias {std::string_view name; std::size_t device;};','inline constexpr std::array<Device, '+devices.length+'> devices = {{',...devices.map(d=>'    {'+[q(d.deviceId),q(d.runtimeType??d.sourceType),q(d.consumerClass),String(d.factoryInstantiable),mask(d.declared.Capabilities.minimum,capabilities)+'ULL',mask(d.declared.Capabilities.possible,capabilities)+'ULL'].join(', ')+'},'),' }};','inline constexpr std::array<Alias, '+nativeAliases.length+'> aliases = {{',...nativeAliases.map(a=>'    {'+q(a.alias)+', '+a.index+'},'),' }};','inline const Device* find(std::string_view name) noexcept {','    std::size_t lo=0,hi=aliases.size();','    while(lo<hi){const auto mid=lo+(hi-lo)/2;if(aliases[mid].name<name)lo=mid+1;else hi=mid;}','    return lo<aliases.size()&&aliases[lo].name==name?&devices[aliases[lo].device]:nullptr;','}','inline std::string_view deviceId(std::string_view name) noexcept {const auto* d=find(name);return d?d->deviceId:std::string_view{};}','} // namespace ymcc::capabilities',''].join('\n');
const mainPath=path.join(repo,'native/main.cpp'),mainBefore=fs.readFileSync(mainPath,'utf8');
const lighting=buildLightingArtifacts({repo,catalog,policy:json(path.join(repo,'capabilities/hc-lighting-policy.json')),main:mainBefore});
const battery=buildBatteryArtifacts({repo,catalog});
const motion=buildMotionArtifacts({repo,catalog});
const controller=buildControllerArtifacts({repo,catalog});
const controllerConfiguration=buildControllerConfigurationArtifacts({repo,catalog});
const controllerMotion=buildControllerMotionArtifacts({repo,catalog});
const controllerFeedback=buildControllerFeedbackArtifacts({repo,catalog});
const performanceMetadata=buildPerformanceMetadata({catalog,inventory});
catalog.domains={lighting:lighting.metadata,battery:battery.metadata,motion:motion.metadata,controller:controller.metadata,controllerConfiguration:controllerConfiguration.metadata,controllerMotion:controllerMotion.metadata,controllerFeedback:controllerFeedback.metadata};
const artifacts={'capabilities/hc-performance-metadata.json':JSON.stringify(performanceMetadata,null,2)+'\n','capabilities/hc-device-catalog.json':JSON.stringify(catalog,null,2)+'\n','capabilities/HcDeviceCapabilityCatalog.generated.cs':cs,'capabilities/hc-device-catalog.generated.h':nativeCatalog,...lighting.artifacts,...battery.artifacts,...motion.artifacts,...controller.artifacts,...controllerConfiguration.artifacts,...controllerMotion.artifacts,...controllerFeedback.artifacts};
if(!check&&generatedRoot===repo&&fs.readFileSync(mainPath,'utf8')!==mainBefore)throw Error('CONCURRENT_NATIVE_SOURCE_CHANGE');
for(const [file,text] of Object.entries(artifacts)){const p=path.join(generatedRoot,file);if(check){if(!fs.existsSync(p)||fs.readFileSync(p,'utf8')!==text)throw Error('STALE_HC_CAPABILITY_CATALOG: '+file);}else {fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,text);}}
fs.writeFileSync(path.join(scratch,MANIFEST_FILE),JSON.stringify(buildArtifactManifest(artifacts),null,2)+'\n');
console.log(JSON.stringify({status:check?'CURRENT':'GENERATED',...catalog.scope,runtimeOnlyTypes:extras.map(t=>t.fullName),hardwareIo:false,assemblyLoaded:false}));
