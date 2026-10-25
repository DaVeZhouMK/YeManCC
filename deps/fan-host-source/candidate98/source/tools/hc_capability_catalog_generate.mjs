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
const artifacts={'capabilities/hc-device-catalog.json':JSON.stringify(catalog,null,2)+'\n','capabilities/HcDeviceCapabilityCatalog.generated.cs':cs};
for(const [file,text] of Object.entries(artifacts)){const p=path.join(generatedRoot,file);if(check){if(!fs.existsSync(p)||fs.readFileSync(p,'utf8')!==text)throw Error('STALE_HC_CAPABILITY_CATALOG: '+file);}else {fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,text);}}
console.log(JSON.stringify({status:check?'CURRENT':'GENERATED',...catalog.scope,runtimeOnlyTypes:extras.map(t=>t.fullName),hardwareIo:false,assemblyLoaded:false}));
