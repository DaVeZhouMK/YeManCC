// Build-time provenance/type validation for the sole lighting catalog generator.
// This module never runs in YMCC, discovers devices or authorizes hardware I/O.
import fs from 'node:fs';import path from 'node:path';import {createHash} from 'node:crypto';
export const lightingSourceAdapterTypes=Object.freeze({'oxp3-hid-v1':'Oxp3Hid','superjoy-cec-v1':'SuperJoyCec','onex-x2mini-hid-v1':'OneXX2MiniHid'});
const commit='f2e58468d9917189d4be7ccfc242385ce2c3035b';
const pins=Object.freeze({
 OneXAOKZOE:'30661f7bfba6b67ba9f025be97158c274fe28cabdcd0faac08ca3fb94c34d718',
 IDevice:'f608b6f5a165e8b74a48d09d81fa5316c72273c0efd721327b1d2deccf1d5a47',
 OneXPlayerX1:'3b80d932227c3beddbb6b3a4007c57fc19e0fb55df0db54d389ca8834eae3485',
 OneXPlayerX2:'41cf648a9d9247c2e1a5da29bed748f0aed0a8407202a690b0b866ae2d5c369b',
 OneXPlayerX2MiniPro:'7f06350daeb1d1c7157ae21f1dc231637394008256b3fc4c22c747109e2c7b67',
 OneXPlayer3:'e3a6275e34db52fa60c7b5a421655af24d8580611fe9ebb875986817891809dc',
 AYANEO3:'bee53cd4d730013900da5c542c751b018f006ee09b886c55926f0ac7c17ee88b',
 AYANEONEXT2:'b47a90a52912eebd2568e920da7c1d0a264a286bb06657f9e274feb7f5da91c2',
 AYANEODeviceCEc:'ff3d6bb410edbf024ae8d419093cb15f744008ccaff3b97db4c69325fd0b07da',
 AYANEOSuperJoyRgb:'73537d76a1a143f8868bf3fa15b3d25b2ecbe3ee495a7cef781bc240e4776a12'
});
export function lightingSourceAdapterType(ext){
 if(!Object.hasOwn(lightingSourceAdapterTypes,ext.sourceAdapter))throw Error('LIGHTING_EXTENSION_ADAPTER_UNREVIEWED');
 const allowed=ext.sourceAdapter==='oxp3-hid-v1'?['OneXPlayer3']:ext.sourceAdapter==='onex-x2mini-hid-v1'?['OneXPlayerX2MiniPro']:['AYANEO3','AYANEONEXT2'];
 if(!allowed.includes(ext.name)||(['oxp3-hid-v1','onex-x2mini-hid-v1'].includes(ext.sourceAdapter)&&ext.family!=='OneXPlayer')||ext.sourcePin!==commit||ext.sourceSha256!==pins[ext.name]||
    ext.deviceClass!=='HandheldCompanion.Devices.'+ext.name||ext.deviceId!=='ymcc.device.'+ext.name||
    ext.sourceFile!=='capabilities/upstream/f2e58468/'+ext.name+'.cs')throw Error('LIGHTING_EXTENSION_FIXED_SOURCE_AUTHORITY');
 return lightingSourceAdapterTypes[ext.sourceAdapter];
}
export function validateSuperJoyExtension(repo,ext,text){
 if(lightingSourceAdapterType(ext)!=='SuperJoyCec'||ext.family!=='AYANEO')throw Error('LIGHTING_SUPERJOY_MODEL_UNREVIEWED');
 for(const fact of ['public class '+ext.name+' : AYANEODeviceCEc','vendorId = AYANEOSuperJoyRgb.VendorId;','productIds = [AYANEOSuperJoyRgb.ProductId];',
  'protected override void CEcRgb_GlobalOn(LEDGroup group','protected override void CEcRgb_GlobalOff(LEDGroup group)',
  'protected override void CEcRgb_SetColorAll(LEDGroup group, Color color)','ApplySuperJoyLeds(true);','ApplySuperJoyLeds(false);',
  'group is LEDGroup.StickLeft or LEDGroup.StickRight','base.CEcRgb_SetColorAll(group, color);'])
  if(!text.includes(fact))throw Error('LIGHTING_SUPERJOY_OVERRIDE_FACT: '+fact);
 if(ext.name==='AYANEO3'?!text.includes('rgbConfirmation = false;'):text.includes('rgbConfirmation ='))throw Error('LIGHTING_SUPERJOY_CONFIRMATION_SOURCE');
 const dependencies=ext.sourceDependencies;
 if(!Array.isArray(dependencies)||dependencies.length!==2||new Set(dependencies.map(d=>d.role)).size!==2)throw Error('LIGHTING_SUPERJOY_DEPENDENCY_SET');
 const texts={};
 for(const [role,name] of [['cec','AYANEODeviceCEc'],['superjoy','AYANEOSuperJoyRgb']]){
  const row=dependencies.find(d=>d.role===role),file='capabilities/upstream/f2e58468/'+name+'.cs';
  if(!row||row.path!==file||row.sha256!==pins[name])throw Error('LIGHTING_SUPERJOY_DEPENDENCY_PIN');
  const bytes=fs.readFileSync(path.join(repo,file));
  if(createHash('sha256').update(bytes).digest('hex')!==pins[name])throw Error('LIGHTING_SUPERJOY_DEPENDENCY_CHANGED');
  texts[role]=bytes.toString('utf8');
 }
 for(const fact of ['protected bool rgbConfirmation = true;','protected virtual void CEcRgb_GlobalOn','protected virtual void CEcRgb_SetColorAll',
  'this.CEcRgb_SetColorOne(LEDGroup.AYA, 4, color.B, color.R, color.G);',
  'return (int)(((((float)color / 255) * 192) / 100) * (float)this.ledBrightness);',
  'this.EcWriteByte(0x6d, (byte)group);','this.EcWriteByte(0xb1, command);','this.EcWriteByte(0xb2, argument);',
  'this.EcWriteByte(0xbf, 0x10);','Thread.Sleep(5);','this.EcWriteByte(0xbf, 0xfe);','this.EcWriteByte(0xbf, 0x00);'])
  if(!texts.cec.includes(fact))throw Error('LIGHTING_SUPERJOY_CEC_FACT: '+fact);
 for(const fact of ['public const int VendorId = 0x1C4F;','public const int ProductId = 0x0002;','private const short Usage = 0x0001;',
  'private const int PacketLength = 64;','private const int TransactionAttempts = 3;','private const int TransactionTimeoutMilliseconds = 300;'])
  if(!texts.superjoy.includes(fact))throw Error('LIGHTING_SUPERJOY_PROTOCOL_FACT: '+fact);
 const f=ext.semanticSourceFacts??{};
 if(f.sticks!=='superjoy'||f.otherZone!=='cec-aya'||f.ayaZone!==4||JSON.stringify(f.ayaChannels)!=='["B","R","G"]'||
    f.confirmation!==(ext.name==='AYANEONEXT2')||f.ownerAbi!==2||f.explicitColorContext!==true)throw Error('LIGHTING_SUPERJOY_SEMANTIC_FACTS');
 if(JSON.stringify(ext.ledEffects)!=='["SolidColor"]'||ext.brightness!==true||ext.secondColor!==true||ext.statusAvailable!==true||ext.statusTransportDisabled!==false||
    ext.presetDeclared!==false||ext.presetAvailable!==false||ext.fixedSpeed!==false||JSON.stringify(ext.capabilities)!=='["DynamicLighting","DynamicLightingBrightness","DynamicLightingSecondLEDColor"]')
  throw Error('LIGHTING_SUPERJOY_CAPABILITIES_UNREVIEWED');
}

export function validateX2MiniExtension(repo,ext,text){
 if(lightingSourceAdapterType(ext)!=='OneXX2MiniHid')throw Error('LIGHTING_X2MINI_MODEL_UNREVIEWED');
 for(const fact of ['public class OneXPlayerX2MiniPro : OneXPlayerX2',
  'level is not (LEDLevel.SolidColor or LEDLevel.Breathing)',
  'new byte[] { 0x01, 0x02, 0x07, 0x05, 0x06 }',
  'new byte[] { 0x01, 0x02, 0x07 }','new byte[] { 0x05, 0x06 }',
  'SendV1Brightness(device, brightness, side)','SendV1SolidColor(device, mainColor, side, breathing)',
  'SendV1SolidColor(device, secondaryColor, side, breathing)','Thread.Sleep(200);'])
  if(!text.includes(fact))throw Error('LIGHTING_X2MINI_SOURCE_FACT: '+fact);
 const deps=ext.sourceDependencies;
 if(!Array.isArray(deps)||deps.length!==4||new Set(deps.map(d=>d.role)).size!==4)throw Error('LIGHTING_X2MINI_DEPENDENCY_SET');
 for(const [role,name] of [['x1','OneXPlayerX1'],['x2','OneXPlayerX2'],['lighting-base','OneXAOKZOE'],['report-base','IDevice']]){
  const d=deps.find(row=>row.role===role),file='capabilities/upstream/f2e58468/'+name+'.cs';
  if(!d||d.path!==file||d.sha256!==pins[name])throw Error('LIGHTING_X2MINI_DEPENDENCY_PIN');
  if(createHash('sha256').update(fs.readFileSync(path.join(repo,file))).digest('hex')!==d.sha256)
   throw Error('LIGHTING_X2MINI_DEPENDENCY_CHANGED');
 }
 const f=ext.semanticSourceFacts??{};
 if(JSON.stringify(f.primaryZones)!=='[1,2,7]'||JSON.stringify(f.secondaryZones)!=='[5,6]'||
    f.zoneDelayMs!==200||f.speedIgnored!==true||f.aggregateZoneAllowed!==false||f.offUsesExplicitBlackAndZeroBrightness!==true)
  throw Error('LIGHTING_X2MINI_SEMANTIC_FACTS');
 if(JSON.stringify(ext.ledEffects)!=='["SolidColor","Breathing"]'||ext.brightness!==true||ext.secondColor!==true||
    ext.statusAvailable!==false||ext.statusTransportDisabled!==true||ext.presetDeclared!==true||ext.presetAvailable!==false||ext.fixedSpeed!==true||
    JSON.stringify(ext.capabilities)!=='["DynamicLighting","DynamicLightingBrightness","DynamicLightingSecondLEDColor"]')
  throw Error('LIGHTING_X2MINI_CAPABILITIES_UNREVIEWED');
 const consumers=ext.consumerSources,required={
  protocol:'FanLab/LightSetter/DeviceCore/PinnedX2MiniProProtocol.cs',
  entry:'FanLab/LightSetter/DeviceCore/X2MiniProDeviceCore.cs',
  transport:'FanLab/LightSetter/DeviceCore/WindowsHidTransport.cs',
  abi:'FanLab/LightSetter/DeviceCore/LightingDeviceCore.cs'};
 if(!Array.isArray(consumers)||consumers.length!==Object.keys(required).length||new Set(consumers.map(d=>d.role)).size!==Object.keys(required).length)throw Error('LIGHTING_X2MINI_CONSUMER_SET');
 for(const [role,file] of Object.entries(required)){
  const row=consumers.find(d=>d.role===role);
  if(!row||row.path!==file||!/^[a-f0-9]{64}$/.test(row.sha256)||
     createHash('sha256').update(fs.readFileSync(path.join(repo,file))).digest('hex')!==row.sha256)
   throw Error('LIGHTING_X2MINI_CONSUMER_SOURCE_CHANGED: '+role);
 }
}
