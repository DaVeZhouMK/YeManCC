// Build-time provider and original-protocol provenance. No hardware probing or HC loading.
import fs from 'node:fs';import path from 'node:path';import {createHash} from 'node:crypto';
const sha=bytes=>createHash('sha256').update(bytes).digest('hex'),q=JSON.stringify;
export function buildControllerMotionArtifacts({repo,catalog}){
 const policy=JSON.parse(fs.readFileSync(path.join(repo,'capabilities/hc-controller-motion-policy.json'),'utf8'));
 const expected=[['LegionGoTablet','Tablet',0x17EF,2],['LegionGoTablet2','Tablet',0x17EF,2],['LegionGoSZ1','GoS',0x1A86,1],['LegionGoSZ2','GoS',0x1A86,1]];
 if(policy.schemaVersion!==1||policy.providers?.length!==expected.length||policy.allControllerMotionConnected!==false||policy.physicalAcceptance!==false||policy.protocolSource!=='native/legion_input_protocol.h'||JSON.stringify(policy.pidSources)!==JSON.stringify(['legionBackIsPid','legionSBackIsPid']))throw Error('CONTROLLER_MOTION_POLICY_SCOPE');
 const contract=JSON.parse(fs.readFileSync(path.join(repo,'capabilities/CONTROLLER-CONSUMPTION.json'),'utf8'));
 const frozen=new Map(contract.sourceInventory.files.map(row=>[row.file,row]));
 const rows=policy.providers.map((p,index)=>{
  const[name,protocol,vendorId,bankCount]=expected[index],d=catalog.devices.find(d=>d.name===name);
  if(!d||p.model!==name||p.protocol!==protocol||p.vendorId!==vendorId||p.bankCount!==bankCount||p.id!==(protocol==='Tablet'?'controller.motion.legion-tablet':'controller.motion.legion-go-s'))throw Error('UNREVIEWED_CONTROLLER_MOTION_PROVIDER');
  if(JSON.stringify(p.references)!==JSON.stringify([protocol==='Tablet'?'Controllers/Lenovo/LegionController.cs':'Controllers/Lenovo/LegionControllerS.cs','Controllers/Lenovo/LegionControllerBase.cs']))throw Error('CONTROLLER_MOTION_SOURCE_REFERENCES');
  const references=p.references.map(file=>{const record=frozen.get(file);if(!record||sha(fs.readFileSync(path.join(repo,'deps/handheldcompanion-runtime/source',file)))!==record.sha256)throw Error('CONTROLLER_MOTION_FROZEN_SOURCE:'+file);return{file,sha256:record.sha256};});
  return{token:index+1,deviceId:d.deviceId,deviceClass:d.consumerClass,deviceSource:{file:d.sourcePath,sha256:d.sourceSha256},protocol,vendorId,bankCount,capabilityId:p.id,references};
 });
 const activation=policy.activation;
 if(activation?.physicalAcceptance!==false||activation.recipes?.length!==2)throw Error('CONTROLLER_MOTION_ACTIVATION_SCOPE');
 const sourceFor=model=>{
  const d=catalog.devices.find(d=>d.name===model);
  if(!d)throw Error('CONTROLLER_MOTION_ACTIVATION_MODEL:'+model);
  const bytes=fs.readFileSync(path.join(repo,d.sourcePath));
  if(sha(bytes)!==d.sourceSha256)throw Error('CONTROLLER_MOTION_ACTIVATION_SOURCE:'+model);
  return {model,file:d.sourcePath,sha256:d.sourceSha256,text:bytes.toString('utf8')};
 };
 const withoutText=({text,...row})=>row;
 const tablet=sourceFor('LegionGoTablet'),tablet2=sourceFor('LegionGoTablet2'),gos=sourceFor('LegionGoSZ1'),gos2=sourceFor('LegionGoSZ2'),framing=sourceFor('IDevice');
 if(!/class LegionGoTablet2\s*:\s*LegionGoTablet\b/.test(tablet2.text)||!/class LegionGoSZ2\s*:\s*LegionGoSZ1\b/.test(gos2.text))throw Error('CONTROLLER_MOTION_ACTIVATION_INHERITANCE');
 const deriveTablet=()=>{
  const a=tablet.text.indexOf('private IEnumerable<byte[]> EnableControllerGyro(int idx)'),b=tablet.text.indexOf('private IEnumerable<byte[]> DisableControllerGyro',a);
  if(a<0||b<a||!tablet.text.includes('LeftJoyconIndex = 3')||!tablet.text.includes('RightJoyconIndex = 4'))throw Error('CONTROLLER_MOTION_ACTIVATION_METHOD');
  const rows=[...tablet.text.slice(a,b).matchAll(/yield return new byte\[\]\s*\{([^}]+)\}/g)];
  if(rows.length!==2)throw Error('CONTROLLER_MOTION_ACTIVATION_COMMANDS');
  return [3,4].flatMap(index=>rows.map(row=>row[1].split(',').map(x=>x.trim()==='(byte)idx'?index:Number(x.trim()))));
 };
 const deriveGoS=()=>{
  if(!gos.text.includes('device.Write(WithReportID(cmd))')||!gos.text.includes('InputReportByteLength != 65 || device.Capabilities.OutputReportByteLength != 65'))throw Error('CONTROLLER_MOTION_GOS_TRANSPORT');
  const a=gos.text.indexOf('private IEnumerable<byte[]> ControllerFactoryReset()'),b=gos.text.indexOf('public override void SetPassthrough',a);
  const rows=[...gos.text.slice(a,b).matchAll(/yield return ConvertHex\("([0-9a-f]+)"\);\s*\/\/ (enable gyro|enable HID IMU)\s*$/gmi)];
  if(a<0||b<a||rows.length!==2)throw Error('CONTROLLER_MOTION_GOS_IMU_SUBSET');
  if(!framing.text.includes('WithReportID(byte[] payload, byte reportID = 0x00, int reportLen = 64)')||!framing.text.includes('new byte[1 + reportLen]')||!framing.text.includes('buffer[0] = reportID;')||!framing.text.includes('Buffer.BlockCopy(payload, 0, buffer, 1, len);'))throw Error('CONTROLLER_MOTION_GOS_REPORT_ID');
  return rows.map(row=>[...Buffer.from(row[1],'hex')]);
 };
 const tabletCommands=deriveTablet(),goSLogical=deriveGoS(),goSCommands=goSLogical.map(bytes=>[0,...bytes]);
 const expectedRecipes=[{id:'tablet',source:'Devices/Lenovo/LegionGoTablet.cs',method:'EnableControllerGyro',models:['LegionGoTablet','LegionGoTablet2'],minReportBytes:7,maxReportBytes:64,inputReportBytes:0,commands:tabletCommands,sources:[tablet,tablet2]},
  {id:'go-s',source:'Devices/Lenovo/LegionGoSZ1.cs',method:'ControllerFactoryReset:IMU-only-subset',models:['LegionGoSZ1','LegionGoSZ2'],minReportBytes:65,maxReportBytes:65,inputReportBytes:65,commands:goSCommands,sources:[gos,gos2,framing]}];
 const recipes=expectedRecipes.map((expected,i)=>{
  const actual=activation.recipes[i];
  for(const key of ['id','source','method','models','minReportBytes','maxReportBytes','inputReportBytes','commands'])if(JSON.stringify(actual?.[key])!==JSON.stringify(expected[key]))throw Error('CONTROLLER_MOTION_ACTIVATION_COMMAND_MISMATCH:'+expected.id+':'+key);
  if(actual.physicalAcceptance!==false||expected.commands.some(row=>!row.length||row.length>7||row.some(x=>!Number.isInteger(x)||x<0||x>255)))throw Error('CONTROLLER_MOTION_ACTIVATION_RECIPE');
  if(i===1&&(actual.framingSource!=='Devices/IDevice.cs'||actual.framingMethod!=='WithReportID'||JSON.stringify(actual.logicalCommands)!==JSON.stringify(goSLogical)))throw Error('CONTROLLER_MOTION_GOS_FRAMING');
  return {...actual,sourceSha256:expected.sources[0].sha256,sources:expected.sources.map(withoutText)};
 });
 const activationMetadata={...activation,recipes};
 const renderRecipe=(name,recipe)=>{
  const commands=recipe.commands.map(bytes=>'ActivationCommand{{'+[...bytes,...Array(7-bytes.length).fill(0)].join(', ')+'}, '+bytes.length+'}');
  while(commands.length<4)commands.push('ActivationCommand{}');
  return 'inline constexpr ActivationRecipe '+name+' = {std::array<ActivationCommand, 4>{{'+commands.join(', ')+'}}, '+[recipe.commands.length,recipe.minReportBytes,recipe.maxReportBytes,recipe.inputReportBytes].join(', ')+'};';
 };
 const protocolBytes=fs.readFileSync(path.join(repo,policy.protocolSource));if(sha(protocolBytes)!==policy.protocolSourceSha256)throw Error('CONTROLLER_MOTION_ORIGINAL_PROTOCOL_CHANGED');
 const metadata={activation:activationMetadata,schemaVersion:1,registeredProtocols:2,registeredModelBindings:rows.length,scope:policy.scope,rows,protocolSource:{file:policy.protocolSource,sha256:sha(protocolBytes),meaning:'original production decodeLogical function reused, no protocol copy'},pidSources:policy.pidSources,consumer:policy.consumer,chassisGuardRetained:true,liveOwnershipCached:false,allControllerMotionConnected:false,physicalAcceptance:false};metadata.catalogSha256=sha(JSON.stringify(metadata));
 const header=['// Generated by tools/hc_capability_catalog_generate.mjs. Exact original decoder remains authoritative.','#pragma once','#include <array>','#include <cstdint>','#include <string_view>','namespace ymcc::capabilities::controller_motion {','enum class ProtocolKind { Tablet, GoS };','struct Descriptor { std::uint16_t token; std::string_view deviceId, deviceClass, capabilityId; ProtocolKind protocol; std::uint16_t vendorId; unsigned bankCount; };','struct ActivationCommand { std::array<std::uint8_t, 7> bytes{}; unsigned length = 0; };','struct ActivationRecipe { std::array<ActivationCommand, 4> commands{}; unsigned count = 0, minReportBytes = 0, maxReportBytes = 0, inputReportBytes = 0; };','inline constexpr ActivationRecipe noActivationRecipe{};',renderRecipe('tabletActivationRecipe',recipes[0]),renderRecipe('goSActivationRecipe',recipes[1]),'inline constexpr std::string_view catalogSha256 = '+q(metadata.catalogSha256)+';','inline constexpr std::array<Descriptor, '+rows.length+'> descriptors = {{',...rows.map(r=>'    {'+r.token+', '+[r.deviceId,r.deviceClass,r.capabilityId].map(q).join(', ')+', ProtocolKind::'+r.protocol+', '+r.vendorId+', '+r.bankCount+'},'),' }};','} // namespace ymcc::capabilities::controller_motion',''].join('\n');
 return{metadata,artifacts:{'capabilities/HcControllerMotionCapabilityCatalog.generated.h':header}};
}
