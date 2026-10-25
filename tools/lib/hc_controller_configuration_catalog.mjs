// Source-locked registration of existing MSI rear configuration consumers; never loads HC.
import fs from'node:fs';import path from'node:path';import{createHash}from'node:crypto';
const sha=b=>createHash('sha256').update(b).digest('hex'),q=JSON.stringify;
export function buildControllerConfigurationArtifacts({repo,catalog}){
 const policy=JSON.parse(fs.readFileSync(path.join(repo,'capabilities/hc-controller-configuration-policy.json'),'utf8'));
 if(policy.schemaVersion!==1||policy.sharedProfileFields!==true||policy.physicalAcceptance!==false||policy.automaticRestoreEstablished!==false)throw Error('CONTROLLER_CONFIGURATION_SCOPE');
 const wantedModels=[['ClawA1M',['MS-1T41']],['ClawA2VM',['MS-1T42','MS-1T52']],['ClawBZ2EM',['MS-1T8K']],['ClawCG3EM',['MS-1T91']]];
 if(JSON.stringify(policy.models.map(m=>[m.name,m.boards]))!==JSON.stringify(wantedModels))throw Error('CONTROLLER_CONFIGURATION_MODELS');
 const wantedRoutes=[['DInputButtons','controller.configuration.msi.rear.dinput-buttons','msiClawEnsureM12Remap'],['XInputKeyboard','controller.configuration.msi.rear.xinput-keyboard','msiNativeRearPrepareKeyboard']];
 if(JSON.stringify(policy.routes.map(r=>[r.name,r.id,r.consumer]))!==JSON.stringify(wantedRoutes)||policy.routes[1].defaultAuthority!=='buildWritesEnabled=false; experiment-only; not enabled by this catalog')throw Error('CONTROLLER_CONFIGURATION_ROUTE_AUTHORITY');
 if(policy.routes[0].productEnabled!==true||policy.routes[0].currentConsumerWired!==true||policy.routes[1].productEnabled!==false||policy.routes[1].currentConsumerWired!==false)throw Error('CONTROLLER_CONFIGURATION_ENABLED_SCOPE');
 const sourceIndex=fs.readFileSync(path.join(repo,'deps/handheldcompanion-runtime/source-files.sha256'),'utf8'),locked=new Map(sourceIndex.trim().split(/\r?\n/).map(line=>{const match=line.match(/^([a-f0-9]{64})\s+(.+)$/i);if(!match)throw Error('SOURCE_INDEX_SHAPE');return [match[2],match[1].toLowerCase()];}));
 const expectedSources=['Devices/MSI/ClawA1M.cs','Devices/MSI/ClawA2VM.cs','Devices/MSI/ClawBZ2EM.cs','Devices/MSI/ClawCG3EM.cs','Devices/MSI/DeviceVersion.cs','Devices/IDevice.cs'];if(JSON.stringify(policy.sourceFiles.map(r=>r.file))!==JSON.stringify(expectedSources))throw Error('CONTROLLER_CONFIGURATION_SOURCE_SET');
 const sources=new Map();for(const row of policy.sourceFiles){const bytes=fs.readFileSync(path.join(repo,'deps/handheldcompanion-runtime/source',row.file));if(locked.get(row.file)!==row.sha256||sha(bytes)!==row.sha256)throw Error('CONTROLLER_CONFIGURATION_SOURCE:'+row.file);sources.set(row.file,bytes.toString('utf8'));}
 const original=sources.get('Devices/MSI/ClawA1M.cs');
 const firmware=[...original.matchAll(/new DeviceVersion\(\)\s*\{ Firmware = (0x[0-9A-F]+), RGB = \[[^\]]+\], M1DInput = \[([^\]]+)\], M2DInput = \[([^\]]+)\], M1XInput = \[([^\]]+)\], M2XInput = \[([^\]]+)\] \}/gi)].map((match,index)=>({index,value:Number(match[1]),d1:match[2].split(',').map(Number),d2:match[3].split(',').map(Number),x1:match[4].split(',').map(Number),x2:match[5].split(',').map(Number)}));
 if(firmware.length!==8||JSON.stringify(firmware.map(r=>r.value))!==JSON.stringify(policy.firmware)||JSON.stringify(policy.firmware)!==JSON.stringify([0x163,0x166,0x167,0x211,0x217,0x219,0x308,0x411]))throw Error('CONTROLLER_CONFIGURATION_FIRMWARE');
 for(const row of firmware)if([row.d1,row.d2,row.x1,row.x2].some(a=>a.length!==2||a.some(n=>!Number.isInteger(n)||n<0||n>255)))throw Error('CONTROLLER_CONFIGURATION_ADDRESSES');
 if(!original.includes('private byte[] GetM12(bool useM1, bool xinput)')||!original.includes('protected bool SyncToROM()'))throw Error('CONTROLLER_CONFIGURATION_SOURCE_METHODS');
 const factory=sources.get('Devices/IDevice.cs');const models=policy.models.map((model,index)=>{
  const device=catalog.devices.find(d=>d.name===model.name),file='Devices/MSI/'+model.name+'.cs',source=sources.get(file);
  if(!device||device.sourceSha256!==locked.get(file)||!device.factoryInstantiable||device.consumerClass!=='HandheldCompanion.Devices.'+model.name)throw Error('CONTROLLER_CONFIGURATION_TOTAL_IDENTITY:'+model.name);
  if(model.name!=='ClawA1M'&&!new RegExp('class '+model.name+' : ClawA1M').test(source))throw Error('CONTROLLER_CONFIGURATION_SOURCE_INHERITANCE:'+model.name);
  for(const board of model.boards)if(!factory.includes('case "'+board+'":'))throw Error('CONTROLLER_CONFIGURATION_FACTORY_BOARD:'+board);
  return {index,deviceId:device.deviceId,deviceClass:device.consumerClass,sourceSha256:device.sourceSha256,boards:model.boards};
 });
 if(policy.provider.file!=='native/msi_rear_protocol.h'||sha(fs.readFileSync(path.join(repo,policy.provider.file)))!==policy.provider.sha256)throw Error('CONTROLLER_CONFIGURATION_PROVIDER_CHANGED');
 const metadata={schemaVersion:1,registeredModels:models.length,registeredFirmware:firmware.length,registeredRoutes:policy.routes.length,enabledConsumerRoutes:1,recordOnlyRoutes:1,actualConsumers:['msiClawEnsureM12Remap'],models,firmware,routes:policy.routes,sources:policy.sourceFiles,provider:policy.provider,protocol:{vendorId:0x0db0,xinput:{productId:0x1901,usagePage:0xffa0,usage:1},dinput:{productId:0x1902,usagePage:0xfff0,usage:0x40},reportBytes:64,reportBodyOffset:0,order:['m1','m2','sync'],cadenceMs:500},sharedProfileFields:true,nativeKeyboardDefaultWritesEnabled:false,existingConsumerAdmissionRequired:true,physicalAcceptance:false,automaticRestoreEstablished:false};metadata.catalogSha256=sha(JSON.stringify(metadata));
 const generated=['// Generated from frozen HC model/method and firmware source. Registration grants no write permission.','#pragma once','#include <array>','#include <cstdint>','#include <string_view>','namespace ymcc::capabilities::configuration {',
 'enum class RearRoute { None, DInputButtons, XInputKeyboard };',
 'struct Model {std::string_view deviceId,deviceClass,sourceSha256;std::array<std::string_view,2> boards;};',
 'struct Firmware {std::uint16_t value;std::array<std::uint8_t,2> d1,d2,x1,x2;};',
 'inline constexpr std::string_view rearCatalogSha256='+q(metadata.catalogSha256)+';',
 'inline constexpr std::array<Model,4> rearModels={{',...models.map(m=>' {'+[q(m.deviceId),q(m.deviceClass),q(m.sourceSha256)].join(',')+',{'+[...m.boards,...Array(2-m.boards.length).fill('')].map(q).join(',')+'}},'),' }};',
 'inline constexpr std::array<Firmware,8> rearFirmware={{',...firmware.map(f=>' {'+[f.value,'{'+f.d1+'}','{'+f.d2+'}','{'+f.x1+'}','{'+f.x2+'}'].join(',')+'},'),' }};',
 'inline constexpr std::string_view rearCapabilityId(RearRoute route)noexcept{return route==RearRoute::DInputButtons?"controller.configuration.msi.rear.dinput-buttons":route==RearRoute::XInputKeyboard?"controller.configuration.msi.rear.xinput-keyboard":"";}',
 '} // namespace ymcc::capabilities::configuration',''].join('\n');
 return {metadata,artifacts:{'capabilities/HcControllerConfigurationCapabilityCatalog.generated.h':generated}};
}