import {lightingSourceAdapterTypes,lightingSourceAdapterType,validateSuperJoyExtension,validateX2MiniExtension} from './hc_lighting_source_adapters.mjs';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
const q=JSON.stringify;
const effectNames={SolidColor:'solid',Breathing:'breathing',Rainbow:'rainbow',Wave:'wave',Wheel:'wheel',Gradient:'gradient',Ambilight:'ambilight',LEDPreset:'preset'};
function functionBlock(text,signature){const start=text.indexOf(signature),end=start<0?-1:text.indexOf('\n}',start);if(start<0||end<0||text.indexOf(signature,start+signature.length)>=0)throw Error('LIGHTING_FUNCTION_BOUNDARY: '+signature);return {start,end:end+2,text:text.slice(start,end+2)};}
export function buildLightingArtifacts({repo,catalog,policy,main}){
 if(policy.schemaVersion!==1)throw Error('UNSUPPORTED_LIGHTING_POLICY');
 const coreModels=policy.independentCore?.models;
 if(!Array.isArray(coreModels)||coreModels.length<1||coreModels.length>2||policy.independentCore.device||policy.independentCore.actions)
  throw Error('UNREVIEWED_INDEPENDENT_LIGHTING_MODEL_SET');
 const independentByName=new Map();
 for(const row of coreModels){
  if(independentByName.has(row.device))throw Error('DUPLICATE_INDEPENDENT_LIGHTING_MODEL');
  const expected=row.device==='OneXPlayerX1Mini'?{brightness:true,color:['solid','rainbow']}:
   row.device==='OneXPlayerX2MiniPro'?{brightness:true,color:['solid','breathing'],off:true}:null;
  if(!expected||JSON.stringify(row.actions)!==JSON.stringify(expected))throw Error('UNREVIEWED_INDEPENDENT_LIGHTING_MODEL_ACTIONS');
  independentByName.set(row.device,row.actions);
 }
 const independentFor=name=>independentByName.get(name)??null;

 const by=new Map(catalog.devices.map(d=>[d.name,d])),baseAliases=new Set(policy.nativeBaseAliases.map(d=>d.name));
 for(const n of baseAliases)if(!by.has(n)||by.get(n).factoryInstantiable)throw Error('INVALID_NATIVE_LIGHTING_ALIAS: '+n);
 if(policy.effectOrder.length!==8||new Set(policy.effectOrder).size!==8||policy.effectOrder.some(n=>!effectNames[n]))throw Error('UNREVIEWED_LIGHTING_EFFECT_ORDER');
 const available=catalog.devices.filter(d=>(d.factoryInstantiable||baseAliases.has(d.name))&&d.declared.Capabilities.minimum.includes('DynamicLighting'));
 function method(d,name){for(const node of [...d.inheritance].reverse()){const row=by.get(node)?.methods.find(m=>m.name===name);if(row)return {owner:node,...row};}throw Error('MISSING_SOURCE_LIGHTING_METHOD: '+d.name+'.'+name);}
 for(const rule of policy.disabledActions){const d=by.get(rule.device);if(!d||!['preset','status'].includes(rule.action))throw Error('UNREVIEWED_LIGHTING_ACTION_EXCLUSION');const source=fs.readFileSync(path.join(repo,'deps/handheldcompanion-runtime/source/Devices',rule.sourceFile),'utf8');if(!source.includes(rule.requiredSourceText))throw Error('STALE_LIGHTING_ACTION_EXCLUSION: '+rule.device);}
 // This is a source-bound exclusion on one already registered row, never a route table.
 const secondColorExclusions=policy.secondColorExclusions;
 if(!Array.isArray(secondColorExclusions)||secondColorExclusions.length!==1)throw Error('LIGHTING_SECOND_COLOR_EXCLUSION_REQUIRED');
 const exclusion=secondColorExclusions[0],excluded=by.get(exclusion?.device);
 if(!excluded||exclusion.device!=='OneXPlayerX1Mini'||exclusion.methodOwner!=='OneXPlayerX1Mini'||exclusion.sourceDeclaredSecondColor!==true||exclusion.consumerSecondColor!==false||!available.includes(excluded))throw Error('LIGHTING_SECOND_COLOR_EXCLUSION_SCOPE');
 const excludedMethod=method(excluded,'SetLedColor');
 if(exclusion.method!=='SetLedColor'||excludedMethod.owner!==exclusion.methodOwner||exclusion.sourceFile!==excluded.sourcePath||exclusion.sourceSha256!==excluded.sourceSha256||exclusion.methodSha256!==excludedMethod.sourceSha256||!excluded.declared.Capabilities.minimum.includes('DynamicLightingSecondLEDColor'))throw Error('LIGHTING_SECOND_COLOR_EXCLUSION_SOURCE');
 const excludedBytes=fs.readFileSync(path.join(repo,excluded.sourcePath));
 if(createHash('sha256').update(excludedBytes).digest('hex')!==exclusion.sourceSha256)throw Error('LIGHTING_SECOND_COLOR_EXCLUSION_SOURCE_BYTES');
 const independent=exclusion.independentConsumer;
 if(!independent||independent.sourceFile!=='FanLab/LightSetter/DeviceCore/PinnedOneXHidV1.g.cs'||independent.type!=='PinnedX1MiniProtocol'||independent.method!=='SetLedColor'||!independentFor(exclusion.device))throw Error('LIGHTING_SECOND_COLOR_EXCLUSION_CONSUMER');
 const independentBytes=fs.readFileSync(path.join(repo,independent.sourceFile));
 if(createHash('sha256').update(independentBytes).digest('hex')!==independent.sourceSha256)throw Error('LIGHTING_SECOND_COLOR_EXCLUSION_CONSUMER_BYTES');
 const rows=available.map(d=>{
  const methods=Object.fromEntries(['SetLedColor','SetLedBrightness','SetLedStatus','SetLEDPreset'].map(n=>[n,method(d,n)]));
  const implemented=n=>methods[n].owner!=='IDevice'&&methods[n].constantBooleanReturn===null;
  const brightnessCompat=(policy.brightnessCompatibility??[]).some(rule=>rule.classes.includes(d.name)&&methods.SetLedBrightness.owner===rule.methodOwner);
  if((policy.brightnessCompatibility??[]).some(rule=>rule.classes.includes(d.name))&&!brightnessCompat)throw Error('STALE_BRIGHTNESS_COMPATIBILITY: '+d.name);
  const declaredEffects=policy.effectOrder.filter(e=>d.declared.DynamicLightingCapabilities.minimum.includes(e)).map(e=>effectNames[e]);
  const brightness=(d.declared.Capabilities.minimum.includes('DynamicLightingBrightness')||brightnessCompat)&&implemented('SetLedBrightness');
  const presetDeclared=declaredEffects.includes('preset'),presetAvailable=presetDeclared&&implemented('SetLEDPreset')&&!policy.disabledActions.some(r=>r.device===d.name&&r.action==='preset');
  if(!implemented('SetLedColor')||!declaredEffects.includes('solid'))throw Error('LIGHTING_COLOR_OPERATION_UNCLASSIFIED: '+d.name);
  const sourceDeclaredSecondColor=d.declared.Capabilities.minimum.includes('DynamicLightingSecondLEDColor');
  const consumerSecondColor=d.name===exclusion.device?exclusion.consumerSecondColor:sourceDeclaredSecondColor;
  const secondColorFacts=d.name===exclusion.device?{sourceDeclaredSecondColor,consumerSecondColor}:{};
  const nativeAliasTargets=baseAliases.has(d.name)?availableFactoryDescendants(d.name):[];
  function availableFactoryDescendants(n){return catalog.devices.filter(c=>c.factoryInstantiable&&c.inheritance.includes(n)).map(c=>c.deviceId);}
  return {deviceId:d.deviceId,deviceClass:d.consumerClass,name:d.name,brightness,secondColor:consumerSecondColor,...secondColorFacts,declaredEffects,presetDeclared,presetAvailable,statusAvailable:implemented('SetLedStatus')&&!policy.disabledActions.some(r=>r.device===d.name&&r.action==='status'),statusTransportDisabled:implemented('SetLedStatus')&&policy.disabledActions.some(r=>r.device===d.name&&r.action==='status'),nativeAliasTargets,methodSources:methods,independent:independentFor(d.name)};
 });
 const source=fs.readFileSync(path.join(repo,'deps/handheldcompanion-runtime/source/Devices/OneXPlayer/OneXPlayerX1.cs'),'utf8'),list=source.match(/LEDPresets\s*=\s*\[([\s\S]*?)\];/);
 if(!list)throw Error('MISSING_NAMED_LED_PRESETS');
 const presets=[...list[1].matchAll(/new\s*\(\s*"([^"]+)"\s*,\s*"[^"]+"\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*\)/g)].map((m,i)=>({id:m[1],label:'预设 '+(i+1),value:Number(m[2])}));
 if(presets.length!==11||new Set(presets.map(p=>p.id)).size!==11)throw Error('UNREVIEWED_NAMED_LED_PRESETS');
 const extensionModels=Array.isArray(policy.reviewedExtensionModels)?policy.reviewedExtensionModels:[],extensionRows=[],extensionNative=[],sha256=text=>createHash('sha256').update(text).digest('hex');
 const extensionIdentities=new Set();
 for(const ext of extensionModels){
  if(by.has(ext.name)||extensionIdentities.has(ext.deviceClass)||extensionIdentities.has(ext.deviceId))throw Error('LIGHTING_EXTENSION_IDENTITY_CONFLICT');
  extensionIdentities.add(ext.deviceClass);extensionIdentities.add(ext.deviceId);
  if(!/^HandheldCompanion\.Devices\.[A-Za-z0-9_]+$/.test(ext.deviceClass))throw Error('INVALID_LIGHTING_EXTENSION_CLASS');
  if(!/^ymcc\.device\.[A-Za-z0-9_]+$/.test(ext.deviceId))throw Error('INVALID_LIGHTING_EXTENSION_ID');
  if(!/^[a-f0-9]{64}$/.test(ext.sourceSha256))throw Error('INVALID_LIGHTING_EXTENSION_PIN');
  lightingSourceAdapterType(ext);
  const sourceText=fs.readFileSync(path.join(repo,ext.sourceFile),'utf8');
  if(sha256(sourceText)!==ext.sourceSha256)throw Error('LIGHTING_EXTENSION_SOURCE_CHANGED');
  let extPresets;
  if(ext.sourceAdapter==='superjoy-cec-v1'){validateSuperJoyExtension(repo,ext,sourceText);extPresets=[];}else if(ext.sourceAdapter==='onex-x2mini-hid-v1'){if(!independentFor(ext.name))throw Error('LIGHTING_X2MINI_INDEPENDENT_ADMISSION_REQUIRED');validateX2MiniExtension(repo,ext,sourceText);extPresets=[];}else{
  const requiredFacts=[
   'Capabilities |= DeviceCapabilities.DynamicLightingSecondLEDSet;',
   'Capabilities |= DeviceCapabilities.DynamicLightingFixedSpeed;',
   'LeftStickZone = 0x01, RightStickZone = 0x02, GKeyZone = 0x05, LogoZone = 0x06;',
   'ZoneDelay = 150;','SafeDelay = ZoneDelay * 2;','StickDelay = ZoneDelay * 4;',
   'OXPClassicPreset = 1000;','Color.FromRgb(183, 48, 0)','private bool ApplyLeds()'
  ];
  for(const fact of requiredFacts)if(!sourceText.includes(fact))throw Error('LIGHTING_EXTENSION_SOURCE_FACT: '+ext.name+': '+fact);
  const facts=ext.semanticSourceFacts??{};
  if(!facts.zones||facts.zones.leftStick!==1||facts.zones.rightStick!==2||facts.zones.gkey!==5||facts.zones.logo!==6)throw Error('LIGHTING_EXTENSION_ZONES');
  if(!facts.delaysMs||facts.delaysMs.zone!==150||facts.delaysMs.safe!==300||facts.delaysMs.stick!==600)throw Error('LIGHTING_EXTENSION_DELAYS');
  if(facts.classicPreset!==1000||JSON.stringify(facts.classicColor)!=='[183,48,0]')throw Error('LIGHTING_EXTENSION_CLASSIC');
  const presetBlock=sourceText.match(/LEDPresets\s*=\s*\[([\s\S]*?)\];/);
  if(!presetBlock)throw Error('LIGHTING_EXTENSION_PRESETS_MISSING');
  const classicValue=Number((sourceText.match(/OXPClassicPreset\s*=\s*(\d+);/)||[])[1]);
  if(classicValue!==facts.classicPreset)throw Error('LIGHTING_EXTENSION_CLASSIC_VALUE');
  extPresets=[...presetBlock[1].matchAll(/new\s*\(\s*"([^"]+)"\s*,\s*"[^"]+"\s*,\s*(0x[0-9A-Fa-f]+|\d+|OXPClassicPreset)\s*\)/g)].map(m=>({id:m[1],label:'预设 '+m[1].replace(/^LEDPreset_OneXPlayerX1_Preset/,''),value:m[2]==='OXPClassicPreset'?classicValue:Number(m[2])}));
  if(extPresets.length!==10||new Set(extPresets.map(p=>p.id)).size!==10)throw Error('LIGHTING_EXTENSION_PRESETS_UNREVIEWED');
  }
  const declaredEffects=ext.ledEffects.map(n=>{if(!effectNames[n])throw Error('LIGHTING_EXTENSION_EFFECT: '+n);return effectNames[n];});
  if(!declaredEffects.includes('solid'))throw Error('LIGHTING_EXTENSION_SOLID_REQUIRED');
  if(declaredEffects.includes('preset')&&!ext.presetAvailable)throw Error('LIGHTING_EXTENSION_PRESET_TRANSPORT');
  extensionRows.push({deviceId:ext.deviceId,deviceClass:ext.deviceClass,name:ext.name,family:ext.family,brightness:!!ext.brightness,secondColor:!!ext.secondColor,declaredEffects,presetDeclared:!!ext.presetDeclared,presetSecondColor:!!ext.presetAvailable && sourceText.includes('Capabilities |= DeviceCapabilities.DynamicLightingSecondLEDSet;'),presetAvailable:!!ext.presetAvailable,statusAvailable:!!ext.statusAvailable,statusTransportDisabled:!!ext.statusTransportDisabled,capabilities:ext.capabilities??[],nativeAliasTargets:[],methodSources:{},independent:independentFor(ext.name),origin:'reviewed-native-extension',sourceAdapter:ext.sourceAdapter,sourceDependencies:ext.sourceDependencies??[],...(ext.consumerSources?{consumerSources:ext.consumerSources}:{}),sourceFile:ext.sourceFile,sourceSha256:ext.sourceSha256,sourcePin:ext.sourcePin,presetIds:extPresets.map(p=>p.id),presets:extPresets,fixedSpeed:!!ext.fixedSpeed,requestLimits:ext.requestLimits??[]});
  extensionNative.push({row:extensionRows.at(-1),presetArray:'k'+ext.name+'PresetIds',presets:extPresets});
 }
 const allRows=[...rows,...extensionRows];
 for(const route of policy.nativeAdapters??[])if(!["rog-aura","msi-claw-native-hid"].includes(route.id)||route.devices.some(n=>!rows.some(r=>r.name===n)))throw Error("UNREVIEWED_NATIVE_LIGHTING_ADAPTER");
 const metadata={schemaVersion:1,registeredClasses:rows.length,rows,presets,frozenRegisteredClasses:rows.length,reviewedExtensionRows:extensionRows.length,allRegisteredClasses:allRows.length,extensionRows,extensionModels:extensionRows.map(r=>({name:r.name,deviceId:r.deviceId,deviceClass:r.deviceClass,sourceFile:r.sourceFile,sourceSha256:r.sourceSha256,sourcePin:r.sourcePin,sourceAdapter:r.sourceAdapter,sourceDependencies:r.sourceDependencies,...(r.consumerSources?{consumerSources:r.consumerSources}:{}),presetIds:r.presetIds,requestLimits:r.requestLimits})),independentCore:policy.independentCore,nativeBaseAliases:policy.nativeBaseAliases,disabledActions:policy.disabledActions,secondColorExclusions,brightnessCompatibility:policy.brightnessCompatibility??[],nativeAdapters:policy.nativeAdapters??[]};
 metadata.catalogSha256=createHash('sha256').update(JSON.stringify({sourceBaseline:catalog.sourceBaseline,runtimeAliasSource:catalog.runtimeAliasSource,metadata})).digest('hex');
 const extensionClasses=extensionRows.map(r=>q(r.deviceClass)).join(', ');
 const nativeIdentity=['inline constexpr const char* kExtensionLightingClasses[] = {'+extensionClasses+'};','inline bool identityMatches(std::string_view actual, std::string_view expected) noexcept {','    for (const char* cls : kExtensionLightingClasses) if (actual == cls && expected == cls) return true;','    const auto* a=ymcc::capabilities::find(actual); const auto* e=ymcc::capabilities::find(expected);','    if (!a || !e) return false;','    if (a->deviceId == e->deviceId) return true;',...rows.filter(r=>r.nativeAliasTargets.length).map(r=>'    if (e->deviceId == '+q(r.deviceId)+') return '+r.nativeAliasTargets.map(id=>'a->deviceId == '+q(id)).join(' || ')+';'),'    return false;','}'].join('\n');
 const enumBits=names=>names.length?names.map(n=>'HcSourceLedEffect.'+Object.keys(effectNames).find(k=>effectNames[k]===n)).join(' | '):'HcSourceLedEffect.None';
 const capBits=names=>names.length?names.map(n=>'HcSourceCapability.'+n).join(' | '):'HcSourceCapability.None';
 const extensionCs=extensionRows.flatMap(r=>[
  '        (new HcDeviceDescriptor('+[q(r.deviceId),q(r.deviceClass),q(r.deviceClass),q(r.family),q(r.sourceFile),q(r.sourceSha256),'false',capBits(r.capabilities),capBits(r.capabilities),enumBits(r.declaredEffects),enumBits(r.declaredEffects)].join(', ')+'),',
  '         new LightingCapabilityDescriptor('+[q(r.deviceId),String(r.brightness),String(r.secondColor),enumBits(r.declaredEffects),String(r.statusAvailable),String(r.presetDeclared),String(r.presetAvailable),r.independent?'true':'false',String(r.statusTransportDisabled)].join(', ')+') { SourceAdapter = LightingSourceAdapter.'+lightingSourceAdapterTypes[r.sourceAdapter]+' }),'
 ]).join('\n');
 const extensionPresetsCs=extensionRows.flatMap(r=>r.presets.map(p=>'        ('+q(r.deviceId)+', '+q(p.id)+', 0x'+p.value.toString(16)+'),')).join('\n');
 const extensionCsBlock=extensionRows.length?['    internal static readonly (HcDeviceDescriptor Device, LightingCapabilityDescriptor Consumer)[] GeneratedExtensionRoutes = {',extensionCs,'    };','    internal static readonly (string DeviceId, string Id, int Value)[] GeneratedExtensionPresets = {',extensionPresetsCs,'    };']:[];
 const independentGate=['    private static bool GeneratedIndependentActionAllowed(string deviceId, string action, string effect)',
  '    {',...allRows.filter(r=>r.independent).map(r=>'        if (deviceId == '+q(r.deviceId)+') return '+
  [r.independent.brightness?'action == "brightness"':null,r.independent.off?'action == "off"':null,
   '(action == "color" && ('+r.independent.color.map(effect=>'effect == '+q(effect)).join(' || ')+'))'].filter(Boolean).join(' || ')+';'),
  '        return false;','    }'];
 const cs=['// Generated by tools/hc_capability_catalog_generate.mjs. Do not edit.','using System;','namespace YeManFanBackend.Capabilities;','public static partial class HcLightingCapabilityCatalog','{','    public const string CatalogSha256 = '+q(metadata.catalogSha256)+';','    private static readonly LightingCapabilityDescriptor[] GeneratedRoutes = {',...rows.map(r=>'        new('+[q(r.deviceId),String(r.brightness),String(r.secondColor),enumBits(r.declaredEffects),String(r.statusAvailable),String(r.presetDeclared),String(r.presetAvailable),r.independent?'true':'false',String(r.statusTransportDisabled)].join(', ')+'),'),'    };','    private static readonly string[] GeneratedPresetIds = { '+presets.map(p=>q(p.id)).join(', ')+' };',...extensionCsBlock,...independentGate,'    private static readonly (string Expected, string Actual)[] GeneratedIdentityAliases = {',...rows.flatMap(r=>r.nativeAliasTargets.map(actual=>'        ('+q(r.deviceId)+', '+q(actual)+'),')),'    };','    public const string IndependentProvider = '+q(policy.independentCore.provider)+';','}',''].join('\n');
 function setFunction(name,entries){return 'static const std::vector<std::string>& '+name+'() {\n    // Generated from the unified HC catalog; runtime IO remains consumer-owned.\n    static const std::vector<std::string> s_set = {\n'+entries.map(r=>'        '+q(r.deviceClass)+',').join('\n')+'\n    };\n    return s_set;\n}';}
 const groups=new Map();for(const row of allRows){const key=row.declaredEffects.join(',');if(!groups.has(key))groups.set(key,[]);groups.get(key).push(row);}
 const effectFunction='static std::vector<std::string> ymccDeviceClassLedEffects(const std::string& deviceClass) {\n    // Generated declared effects; preset transport availability is a separate gate.\n'+[...groups].map(([key,group])=>'    if ('+group.map(r=>'deviceClass == '+q(r.deviceClass)).join(' ||\n        ')+')\n        return {'+key.split(',').map(q).join(', ')+'};').join('\n')+'\n    return {};\n}';
 const functions=[setFunction('ymccDeviceClassLightingSet',allRows),setFunction('ymccDeviceClassSecondColorSet',allRows.filter(r=>r.secondColor)),effectFunction];
 let generatedMain=main;
 for(const replacement of functions){
  const signature=replacement.slice(0,replacement.indexOf(' {')),old=functionBlock(generatedMain,signature);
  // Keep unrelated bytes and an already-current function intact, including mixed line endings.
  if(old.text.replace(/\r\n/g,'\n')===replacement)continue;
  const local=old.text.includes('\r\n')?replacement.replace(/\n/g,'\r\n'):replacement;
  generatedMain=generatedMain.slice(0,old.start)+local+generatedMain.slice(old.end);
 }
 const colorNames=Object.values(effectNames).filter(n=>n!=='preset');
 const nativeRouteRows=catalog.devices.map(d=>{
  const r=rows.find(row=>row.deviceId===d.deviceId);if(!r)return '    {false, false, false, false, NativeProvider::Helper, 0},';
  const provider=policy.nativeAdapters?.find(route=>route.devices.includes(r.name))?.id;
  return '    {'+[true,r.brightness,r.presetAvailable,r.presetDeclared&&!r.presetAvailable,'NativeProvider::'+(provider==='rog-aura'?'RogAura':provider==='msi-claw-native-hid'?'MsiHid':'Helper'),r.declaredEffects.reduce((bits,n)=>n==='preset'?bits:bits|1<<colorNames.indexOf(n),0),!!r.fixedSpeed,!!r.presetSecondColor].join(', ')+'}, // '+r.name;
 });
 const nativeRoutes=[
 'enum class NativeProvider : unsigned char { Helper, RogAura, MsiHid };',
  'enum class SourceAdapter : unsigned char { FrozenHc, Oxp3Hid, SuperJoyCec, OneXX2MiniHid };',
 'struct Route {bool registered, brightness, preset, declaredPresetWithoutTransport; NativeProvider provider; unsigned colorEffects; bool fixedSpeed = false, presetSecondColor = false;};',
 '// Aligned to the shared identity table: one binary identity lookup, then constant-time operation/provider selection.',
 'inline constexpr std::array<Route, '+catalog.devices.length+'> routes = {{',...nativeRouteRows,' }};',
 'inline const Route* resolve(const ymcc::capabilities::Device* device) noexcept {',
 '    if (!device) return nullptr;',
 '    const auto index = static_cast<std::size_t>(device - ymcc::capabilities::devices.data());',
 '    return index < routes.size() && routes[index].registered ? &routes[index] : nullptr;','}',
 ...extensionNative.map(ext=>'inline constexpr std::array<const char*, '+ext.presets.length+'> '+ext.presetArray+' = {'+ext.presets.map(p=>q(p.id)).join(', ')+'};'),
 'struct ExtensionRoute {const char* deviceClass; const char* deviceId; const char* const* presetIds; int presetCount; SourceAdapter sourceAdapter; Route route;};',
 'inline constexpr std::array<ExtensionRoute, '+extensionNative.length+'> extensionRoutes = {{',
 ...extensionNative.map(ext=>'    {'+q(ext.row.deviceClass)+', '+q(ext.row.deviceId)+', '+ext.presetArray+'.data(), '+ext.presets.length+', SourceAdapter::'+lightingSourceAdapterTypes[ext.row.sourceAdapter]+', {'+[true,ext.row.brightness,ext.row.presetAvailable,ext.row.presetDeclared&&!ext.row.presetAvailable,'NativeProvider::Helper',ext.row.declaredEffects.reduce((bits,n)=>n==='preset'?bits:bits|1<<colorNames.indexOf(n),0),!!ext.row.fixedSpeed,!!ext.row.presetSecondColor].join(', ')+'}}, // '+ext.row.name),
 '}};',
 'inline SourceAdapter sourceAdapter(std::string_view cls) noexcept {',
  '    for (const auto& row : extensionRoutes) if (cls == row.deviceClass) return row.sourceAdapter;',
  '    return SourceAdapter::FrozenHc;',
  '}',
  'inline bool usesOneXVendorOwner(std::string_view cls) noexcept {',
  '    const auto adapter = sourceAdapter(cls);',
  '    return adapter == SourceAdapter::Oxp3Hid || adapter == SourceAdapter::OneXX2MiniHid;',
  '}',
  'inline const Route* resolve(std::string_view cls) noexcept {',
 '    const auto* frozen=resolve(ymcc::capabilities::find(cls));',
 '    if (frozen) return frozen;',
 '    for (const auto& row : extensionRoutes) if (cls == row.deviceClass) return &row.route;',
 '    return nullptr;','}',
 'inline const char* deviceId(std::string_view cls) noexcept {',
 '    const auto* device=ymcc::capabilities::find(cls);',
 '    if (device) return device->deviceId.data();',
 '    for (const auto& row : extensionRoutes) if (cls == row.deviceClass) return row.deviceId;',
 '    return "";','}',
 'inline bool usesRogAura(std::string_view cls) noexcept {const auto* r=resolve(cls);return r && r->provider==NativeProvider::RogAura;}',
 'inline bool usesMsiNativeHid(std::string_view cls) noexcept {const auto* r=resolve(cls);return r && r->provider==NativeProvider::MsiHid;}',
 'inline bool supportsColorEffect(const Route& route, std::string_view mode) noexcept {',
 ...colorNames.map((name,i)=>'    if (mode == '+q(name)+') return (route.colorEffects & '+(1<<i)+'u) != 0;'),
 '    return false;','}',
 'inline bool supportsColorEffect(std::string_view cls, std::string_view mode) noexcept {const auto* r=resolve(cls);return r && supportsColorEffect(*r,mode);}',
 'inline bool presetImplemented(std::string_view cls) noexcept {const auto* r=resolve(cls);return r && r->preset;}',
 'inline bool presetDeclaredButUnavailable(std::string_view cls) noexcept {const auto* r=resolve(cls);return r && r->declaredPresetWithoutTransport;}',
 'inline bool brightnessImplemented(std::string_view cls) noexcept {const auto* r=resolve(cls);return r && r->brightness;}',
 'inline bool validPreset(const Route& route, std::string_view id) noexcept {',
 '    if (!route.preset) return false;',
 '    for (const auto& preset : kOneXPresets) if (id == preset.id) return true;',
 '    return false;','}',
 'inline bool validPreset(std::string_view cls, std::string_view id) noexcept {',
 '    const auto* r=resolve(cls); if (!r || !r->preset) return false;',
 '    for (const auto& row : extensionRoutes) if (cls == row.deviceClass) {',
 '        for (int i=0;i<row.presetCount;i++) if (id == row.presetIds[i]) return true;',
 '        return false;',
 '    }',
 '    return validPreset(*r,id);','}'
 ];
 const header=['// Generated from capabilities/hc-device-catalog.json and hc-lighting-policy.json.','// Declared capabilities and implemented operations are separate. No device access.','#pragma once','#include <array>','#include <string_view>','#include "../capabilities/hc-device-catalog.generated.h"','namespace ymcc::lighting {','inline constexpr char kCatalogSha256[] = '+q(metadata.catalogSha256)+';',nativeIdentity,'struct Preset { const char* id; const char* label; int hcValue; };','inline constexpr std::array<Preset, '+presets.length+'> kOneXPresets = {{',...presets.map(p=>'    {'+q(p.id)+', '+q(p.label)+', 0x'+p.value.toString(16)+'},'),' }};',...nativeRoutes,'} // namespace ymcc::lighting',''].join('\n');
 const core=policy.independentCore;if(!/^[a-f0-9]{64}$/.test(core.sha256))throw Error('INVALID_LIGHTING_CORE_PIN');
 const independentRows=allRows.filter(r=>r.independent);
 if(independentRows.length!==coreModels.length||independentRows.some(r=>!independentByName.has(r.name)))throw Error('UNREVIEWED_INDEPENDENT_LIGHTING_ABI');
 const coreHeader=['// Generated from the unified lighting policy; the caller owns absolute paths.','#pragma once','#include <string>','namespace ymcc { namespace lighting {',
  'inline constexpr char kIndependentDeviceCoreFileName[] = '+q(core.assemblyFile)+';',
  'inline constexpr wchar_t kIndependentDeviceCorePathSuffix[] = L"\\\\light-setter\\\\'+core.assemblyFile+'";',
  'inline constexpr char kIndependentDeviceCoreSha256[] = '+q(core.sha256)+';',
  'inline bool useIndependentDeviceCore(const std::string& deviceClass, const std::string& action, const std::string& mode) {',
  ...independentRows.map(r=>'    if (deviceClass == '+q(r.deviceClass)+') return '+
   [r.independent.brightness?'action == "brightness"':null,r.independent.off?'action == "off"':null,
    '(action == "color" && ('+r.independent.color.map(effect=>'mode == '+q(effect)).join(' || ')+'))'].filter(Boolean).join(' || ')+';'),
  '    return false;','}','} } // namespace ymcc::lighting',''].join('\n');
 const pins='// Generated from capabilities/hc-lighting-policy.json.\ninternal static class LightingAssemblyPins\n{\n    internal const string StandaloneLightingSha256 = '+q(core.sha256)+';\n}\n';
 return {metadata,artifacts:{'capabilities/HcLightingCapabilityCatalog.generated.cs':cs,'native/main.cpp':generatedMain,'native/oem_lighting_operations.h':header,'native/lighting_device_core_binding.h':coreHeader,'FanLab/LightSetter/LightingAssemblyPins.cs':pins}};
}
