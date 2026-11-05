// Build-time provenance/type validation for the sole lighting catalog generator.
// This module never runs in YMCC, discovers devices or authorizes hardware I/O.
import fs from 'node:fs';import path from 'node:path';import {createHash} from 'node:crypto';
export const lightingSourceAdapterTypes=Object.freeze({'oxp3-hid-v1':'Oxp3Hid','superjoy-cec-v1':'SuperJoyCec','onex-x2mini-hid-v1':'OneXX2MiniHid','onex-apex-hid-v2':'ApexHidV2'});
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
 if(ext.sourceAdapter==='onex-apex-hid-v2')throw Error('LIGHTING_APEX_OVERRIDE_NOT_EXTENSION');
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

// This reviewed adapter replaces one frozen consumer; it cannot admit extensions.
const apexSourcePins=Object.freeze({
  "huesync": "7176803ce147d8bffef126720c21e4f42e0f3612",
  "hcWindows": "f2e58468d9917189d4be7ccfc242385ce2c3035b"
});
const apexSourceFiles=Object.freeze({
  "identity-mapping": {
    "path": "capabilities/upstream/huesync-7176803c/onexplayer_configs.py",
    "sha256": "8a697b193db3a4f5c7801fb572eba4cadf0855e0ce50387b973c2a70a4e50d24"
  },
  "endpoint-and-brightness": {
    "path": "capabilities/upstream/huesync-7176803c/onex_led_device_hid.py",
    "sha256": "6ca7e1ab26165d548b8467905530cef3eca1a94243142ee2aadd267cb812ace3"
  },
  "hid-v2-command": {
    "path": "capabilities/upstream/huesync-7176803c/oxp_hid_v2.py",
    "sha256": "27c2f2b449271ac275e083daa39ae75fe7806f15bb42f0933f672c805d3feb08"
  },
  "windows-wire": {
    "path": "capabilities/upstream/f2e58468/OneXAOKZOE.cs",
    "sha256": "30661f7bfba6b67ba9f025be97158c274fe28cabdcd0faac08ca3fb94c34d718"
  },
  "windows-report": {
    "path": "capabilities/upstream/f2e58468/IDevice.cs",
    "sha256": "f608b6f5a165e8b74a48d09d81fa5316c72273c0efd721327b1d2deccf1d5a47"
  },
  "frozen-device": {
    "path": "deps/handheldcompanion-runtime/source/Devices/OneXPlayer/OneXPlayerApex.cs",
    "sha256": "c64af412f599b43d337be93f9860fac30cc85a578c5bed727d84ddfc02237e60"
  },
  "frozen-serial": {
    "path": "deps/handheldcompanion-runtime/source/Devices/OneXPlayer/OneXPlayerX1.cs",
    "sha256": "9948d5f99ec6b50f93a1c85ee557a309c3b0b6225a1ba50231c9c1e12ee49478"
  },
  "source-facts": {
    "path": "capabilities/upstream/huesync-7176803c/apex-lighting.source-facts.json",
    "sha256": "dea80706072e504feb51dd22867f154c663fd0719bba31859748c6a3134b4465"
  }
});
const apexFrozenMethods=Object.freeze({
  "SetLedColor": "ec78229ee1e5193756ba48e1b6d2eeb46a2083f5423d07b0cd98180a60a77f92",
  "SetLedBrightness": "4c11baf43f1b72b4237e612ad32a5ad650363da372826dd18b419502c26135d5",
  "SetLedStatus": "64ff6b0f634a08596bf01b04bedb5edcece8172b84b2ab082d7a33797b8ca306",
  "SetLEDPreset": "41c28ac07661ffcaf003a45f8b5e1101c3ed193c23abe986a9db830de8a94ca5"
});
export function validateApexConsumerOverride(repo,rule,device,row){
 if(!rule||rule.name!=='OneXPlayerApex'||rule.deviceId!=='hc.device.OneXPlayerApex'||
    rule.deviceClass!=='HandheldCompanion.Devices.OneXPlayerApex'||rule.family!=='OneXPlayer'||
    rule.sourceAdapter!=='onex-apex-hid-v2'||!device||!row||device.name!==rule.name||device.deviceId!==rule.deviceId||
    device.consumerClass!==rule.deviceClass||!device.factoryInstantiable||row.deviceId!==rule.deviceId||row.deviceClass!==rule.deviceClass||row.independent)
  throw Error('LIGHTING_APEX_OVERRIDE_SCOPE');
 if(device.sourcePath!==apexSourceFiles['frozen-device'].path||device.sourceSha256!==apexSourceFiles['frozen-device'].sha256||
    JSON.stringify(device.inheritance)!=='["IDevice","OneXAOKZOE","OneXPlayerX1","OneXPlayerX1AMD","OneXPlayerApex"]')
  throw Error('LIGHTING_APEX_FROZEN_IDENTITY_SOURCE');
 for(const [name,hash] of Object.entries(apexFrozenMethods))
  if(row.methodSources[name]?.owner!=='OneXPlayerX1'||row.methodSources[name]?.sourceSha256!==hash)
   throw Error('LIGHTING_APEX_FROZEN_METHOD_SOURCE: '+name);
 if(!rule.sourcePins||Object.keys(rule.sourcePins).length!==2||Object.entries(apexSourcePins).some(([name,pin])=>rule.sourcePins[name]!==pin))
  throw Error('LIGHTING_APEX_SOURCE_PINS');
 const dependencies=rule.sourceDependencies,texts={};
 if(!Array.isArray(dependencies)||dependencies.length!==Object.keys(apexSourceFiles).length||
    new Set(dependencies.map(d=>d.role)).size!==dependencies.length)throw Error('LIGHTING_APEX_DEPENDENCY_SET');
 for(const [role,pin] of Object.entries(apexSourceFiles)){
  const d=dependencies.find(d=>d.role===role);
  if(!d||d.path!==pin.path||d.sha256!==pin.sha256)throw Error('LIGHTING_APEX_DEPENDENCY_PIN: '+role);
  const bytes=fs.readFileSync(path.join(repo,pin.path));
  if(createHash('sha256').update(bytes).digest('hex')!==pin.sha256)throw Error('LIGHTING_APEX_SOURCE_CHANGED: '+role);
  texts[role]=bytes.toString('utf8');
 }
 const required={
  'identity-mapping':['"ONEXPLAYER APEX": OXP_F1_CONF','OXP_F1_CONF = OXPConfig(','protocol=OXPProtocol.HID_V2,','rgb_secondary: bool = False'],
  'endpoint-and-brightness':['XFLY_VID = 0x1A2C','XFLY_PID = 0xB001','XFLY_PAGE = 0xFF01','XFLY_USAGE = 0x0001',
   'brightness_val = round(brightness / 20)','if brightness_val <= 1:','elif brightness_val <= 3:',
   'if primary_state_changed:', 'self._queue_delay(0.3)'],
  'hid-v2-command':['size: int = 64','base = bytes([cid, 0xFF, *c])','"flowing": 0x03','"low": 0x01','"medium": 0x03','"high": 0x04',
   'return gen_cmd(0x07, [0xFD, int(enabled), 0x05, bc])','return gen_cmd(0x07, [0xFE] + 20 * [r, g, b] + [0x00])'],
  'windows-wire':['protected bool SendV2Brightness(','protected bool SendV2SolidColor(','protected bool SendV2Rainbow(',
   'byte[] msg = { 0x00, 0x07, 0xFF, 0xFD, 0x01, 0x05, (byte)level };','byte[] prefix = { 0x00, 0x07, 0xFF };',
   'byte[] ledOption = { 0xFE };','.Repeat(new[] { color.R, color.G, color.B }, 20)','byte[] ledOption = { 0x03 };',
   'Enumerable.Repeat((byte)0x00, 60)','return WriteReport(device, msg);'],
  'windows-report':['byte[] WithReportID(byte[] payload, byte reportID = 0x00, int reportLen = 64)',
   'var buffer = new byte[1 + reportLen];','buffer[0] = reportID;','Buffer.BlockCopy(payload, 0, buffer, 1, len);']
 };
 for(const [role,requiredTexts] of Object.entries(required))for(const fact of requiredTexts)
  if(!texts[role].includes(fact))throw Error('LIGHTING_APEX_SOURCE_FACT: '+role+': '+fact);
 const facts=JSON.parse(texts['source-facts']);
 if(facts.schemaVersion!==1||facts.deviceId!==rule.deviceId||facts.deviceClass!==rule.deviceClass||
    facts.identity.product!=='ONEXPLAYER APEX'||facts.identity.configuration!=='OXP_F1_CONF'||facts.identity.protocol!=='HID_V2'||
    facts.endpoint.vendorId!==0x1a2c||facts.endpoint.productId!==0xb001||facts.endpoint.usagePage!==0xff01||facts.endpoint.usage!==1||
    facts.wire.linuxCommandBytes!==64||facts.wire.windowsReportId!==0||facts.wire.windowsReportBytes!==65||
    facts.brightness.enabled!=='percent > 0'||JSON.stringify(facts.brightness.offWindowsPrefix)!=='[0,7,255,253,0,5,1]'||
    facts.contract.provider!==rule.sourceAdapter||facts.contract.sourceAdapter!=='ApexHidV2'||facts.contract.commandDelayMs!==300)
  throw Error('LIGHTING_APEX_SEMANTIC_SOURCE_FACTS');
 for(const [name,value] of Object.entries(facts.consumer))
  if(JSON.stringify(rule[name])!==JSON.stringify(value))throw Error('LIGHTING_APEX_CONSUMER_CAPABILITIES: '+name);
 const requiredConsumers={protocol:'FanLab/LightSetter/ApexLightingConsumer.cs',transport:'FanLab/LightSetter/ApexWindowsHidTransport.cs',
  entry:'FanLab/LightSetter/Program.cs',owner:'FanLab/LightSetter/SuperJoyOwnerLease.cs'},consumers=rule.consumerSources;
 if(!Array.isArray(consumers)||consumers.length!==4||new Set(consumers.map(d=>d.role)).size!==4)throw Error('LIGHTING_APEX_CONSUMER_SET');
 const consumerTexts={};
 for(const [role,file] of Object.entries(requiredConsumers)){
  const d=consumers.find(d=>d.role===role);
  if(!d||d.path!==file||!/^[a-f0-9]{64}$/.test(d.sha256))throw Error('LIGHTING_APEX_CONSUMER_PIN: '+role);
  const bytes=fs.readFileSync(path.join(repo,file));
  if(createHash('sha256').update(bytes).digest('hex')!==d.sha256)throw Error('LIGHTING_APEX_CONSUMER_CHANGED: '+role);
  consumerTexts[role]=bytes.toString('utf8');
 }
 const consumerFacts={
  protocol:['ReportLength = 65, CommandDelayMs = 300','MidpointRounding.ToEven','value <= 1 ? 1 : value <= 3 ? 3 : 4',
   'brightness > 0 ? (byte)1 : (byte)0','report[3] = 0xFE','i < 20','report[3] = 0x03'],
  transport:['MatchesDescriptor(','caps.OutputReportByteLength','APEX_HID_DESCRIPTOR_MISMATCH','APEX_FULL_WINDOWS_REPORT_REQUIRED'],
  entry:['LightingSourceAdapter.ApexHidV2','operation.PreferredProvider != "onex-apex-hid-v2"',
   'SuperJoyOwnerLease.Acquire(identity, ownerToken','ApexWindowsHidTransport.Open(identity'],
  owner:['!identity.MatchesProtocol && !ApexLightingConsumer.MatchesOwnerIdentity(identity)']
 };
 for(const [role,requiredTexts] of Object.entries(consumerFacts))for(const fact of requiredTexts)
  if(!consumerTexts[role].includes(fact))throw Error('LIGHTING_APEX_CONSUMER_FACT: '+role+': '+fact);
 return facts;
}
