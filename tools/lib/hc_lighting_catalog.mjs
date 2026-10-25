import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
const q=JSON.stringify;
const effectNames={SolidColor:'solid',Breathing:'breathing',Rainbow:'rainbow',Wave:'wave',Wheel:'wheel',Gradient:'gradient',Ambilight:'ambilight',LEDPreset:'preset'};
function functionBlock(text,signature){const start=text.indexOf(signature),end=start<0?-1:text.indexOf('\n}',start);if(start<0||end<0||text.indexOf(signature,start+signature.length)>=0)throw Error('LIGHTING_FUNCTION_BOUNDARY: '+signature);return {start,end:end+2,text:text.slice(start,end+2)};}
export function buildLightingArtifacts({repo,catalog,policy,main}){
 if(policy.schemaVersion!==1)throw Error('UNSUPPORTED_LIGHTING_POLICY');
 const by=new Map(catalog.devices.map(d=>[d.name,d])),baseAliases=new Set(policy.nativeBaseAliases.map(d=>d.name));
 for(const n of baseAliases)if(!by.has(n)||by.get(n).factoryInstantiable)throw Error('INVALID_NATIVE_LIGHTING_ALIAS: '+n);
 if(policy.effectOrder.length!==8||new Set(policy.effectOrder).size!==8||policy.effectOrder.some(n=>!effectNames[n]))throw Error('UNREVIEWED_LIGHTING_EFFECT_ORDER');
 const available=catalog.devices.filter(d=>(d.factoryInstantiable||baseAliases.has(d.name))&&d.declared.Capabilities.minimum.includes('DynamicLighting'));
 function method(d,name){for(const node of [...d.inheritance].reverse()){const row=by.get(node)?.methods.find(m=>m.name===name);if(row)return {owner:node,...row};}throw Error('MISSING_SOURCE_LIGHTING_METHOD: '+d.name+'.'+name);}
 for(const rule of policy.disabledActions){const d=by.get(rule.device);if(!d||!['preset','status'].includes(rule.action))throw Error('UNREVIEWED_LIGHTING_ACTION_EXCLUSION');const source=fs.readFileSync(path.join(repo,'deps/handheldcompanion-runtime/source/Devices',rule.sourceFile),'utf8');if(!source.includes(rule.requiredSourceText))throw Error('STALE_LIGHTING_ACTION_EXCLUSION: '+rule.device);}
 const rows=available.map(d=>{
  const methods=Object.fromEntries(['SetLedColor','SetLedBrightness','SetLedStatus','SetLEDPreset'].map(n=>[n,method(d,n)]));
  const implemented=n=>methods[n].owner!=='IDevice'&&methods[n].constantBooleanReturn===null;
  const brightnessCompat=(policy.brightnessCompatibility??[]).some(rule=>rule.classes.includes(d.name)&&methods.SetLedBrightness.owner===rule.methodOwner);
  if((policy.brightnessCompatibility??[]).some(rule=>rule.classes.includes(d.name))&&!brightnessCompat)throw Error('STALE_BRIGHTNESS_COMPATIBILITY: '+d.name);
  const declaredEffects=policy.effectOrder.filter(e=>d.declared.DynamicLightingCapabilities.minimum.includes(e)).map(e=>effectNames[e]);
  const brightness=(d.declared.Capabilities.minimum.includes('DynamicLightingBrightness')||brightnessCompat)&&implemented('SetLedBrightness');
  const presetDeclared=declaredEffects.includes('preset'),presetAvailable=presetDeclared&&implemented('SetLEDPreset')&&!policy.disabledActions.some(r=>r.device===d.name&&r.action==='preset');
  if(!implemented('SetLedColor')||!declaredEffects.includes('solid'))throw Error('LIGHTING_COLOR_OPERATION_UNCLASSIFIED: '+d.name);
  const nativeAliasTargets=baseAliases.has(d.name)?availableFactoryDescendants(d.name):[];
  function availableFactoryDescendants(n){return catalog.devices.filter(c=>c.factoryInstantiable&&c.inheritance.includes(n)).map(c=>c.deviceId);}
  return {deviceId:d.deviceId,deviceClass:d.consumerClass,name:d.name,brightness,secondColor:d.declared.Capabilities.minimum.includes('DynamicLightingSecondLEDColor'),declaredEffects,presetDeclared,presetAvailable,statusAvailable:implemented('SetLedStatus')&&!policy.disabledActions.some(r=>r.device===d.name&&r.action==='status'),statusTransportDisabled:implemented('SetLedStatus')&&policy.disabledActions.some(r=>r.device===d.name&&r.action==='status'),nativeAliasTargets,methodSources:methods,independent:policy.independentCore.device===d.name?policy.independentCore.actions:null};
 });
 const source=fs.readFileSync(path.join(repo,'deps/handheldcompanion-runtime/source/Devices/OneXPlayer/OneXPlayerX1.cs'),'utf8'),list=source.match(/LEDPresets\s*=\s*\[([\s\S]*?)\];/);
 if(!list)throw Error('MISSING_NAMED_LED_PRESETS');
 const presets=[...list[1].matchAll(/new\s*\(\s*"([^"]+)"\s*,\s*"[^"]+"\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*\)/g)].map((m,i)=>({id:m[1],label:'预设 '+(i+1),value:Number(m[2])}));
 if(presets.length!==11||new Set(presets.map(p=>p.id)).size!==11)throw Error('UNREVIEWED_NAMED_LED_PRESETS');
 for(const route of policy.nativeAdapters??[])if(!["rog-aura","msi-claw-native-hid"].includes(route.id)||route.devices.some(n=>!rows.some(r=>r.name===n)))throw Error("UNREVIEWED_NATIVE_LIGHTING_ADAPTER");
 const metadata={schemaVersion:1,registeredClasses:rows.length,rows,presets,independentCore:policy.independentCore,nativeBaseAliases:policy.nativeBaseAliases,disabledActions:policy.disabledActions,brightnessCompatibility:policy.brightnessCompatibility??[],nativeAdapters:policy.nativeAdapters??[]};
 metadata.catalogSha256=createHash('sha256').update(JSON.stringify({sourceBaseline:catalog.sourceBaseline,runtimeAliasSource:catalog.runtimeAliasSource,metadata})).digest('hex');
 const nativeIdentity=['inline bool identityMatches(std::string_view actual, std::string_view expected) noexcept {','    const auto* a=ymcc::capabilities::find(actual); const auto* e=ymcc::capabilities::find(expected);','    if (!a || !e) return false;','    if (a->deviceId == e->deviceId) return true;',...rows.filter(r=>r.nativeAliasTargets.length).map(r=>'    if (e->deviceId == '+q(r.deviceId)+') return '+r.nativeAliasTargets.map(id=>'a->deviceId == '+q(id)).join(' || ')+';'),'    return false;','}'].join('\n');
 const enumBits=names=>names.length?names.map(n=>'HcSourceLedEffect.'+Object.keys(effectNames).find(k=>effectNames[k]===n)).join(' | '):'HcSourceLedEffect.None';
 const cs=['// Generated by tools/hc_capability_catalog_generate.mjs. Do not edit.','using System;','namespace YeManFanBackend.Capabilities;','public static partial class HcLightingCapabilityCatalog','{','    public const string CatalogSha256 = '+q(metadata.catalogSha256)+';','    private static readonly LightingCapabilityDescriptor[] GeneratedRoutes = {',...rows.map(r=>'        new('+[q(r.deviceId),String(r.brightness),String(r.secondColor),enumBits(r.declaredEffects),String(r.statusAvailable),String(r.presetDeclared),String(r.presetAvailable),r.independent?'true':'false',String(r.statusTransportDisabled)].join(', ')+'),'),'    };','    private static readonly string[] GeneratedPresetIds = { '+presets.map(p=>q(p.id)).join(', ')+' };','    private static readonly (string Expected, string Actual)[] GeneratedIdentityAliases = {',...rows.flatMap(r=>r.nativeAliasTargets.map(actual=>'        ('+q(r.deviceId)+', '+q(actual)+'),')),'    };','    public const string IndependentProvider = '+q(policy.independentCore.provider)+';','}',''].join('\n');
 function setFunction(name,entries){return 'static const std::vector<std::string>& '+name+'() {\n    // Generated from the unified HC catalog; runtime IO remains consumer-owned.\n    static const std::vector<std::string> s_set = {\n'+entries.map(r=>'        '+q(r.deviceClass)+',').join('\n')+'\n    };\n    return s_set;\n}';}
 const groups=new Map();for(const row of rows){const key=row.declaredEffects.join(',');if(!groups.has(key))groups.set(key,[]);groups.get(key).push(row);}
 const effectFunction='static std::vector<std::string> ymccDeviceClassLedEffects(const std::string& deviceClass) {\n    // Generated declared effects; preset transport availability is a separate gate.\n'+[...groups].map(([key,group])=>'    if ('+group.map(r=>'deviceClass == '+q(r.deviceClass)).join(' ||\n        ')+')\n        return {'+key.split(',').map(q).join(', ')+'};').join('\n')+'\n    return {};\n}';
 const functions=[setFunction('ymccDeviceClassLightingSet',rows),setFunction('ymccDeviceClassSecondColorSet',rows.filter(r=>r.secondColor)),effectFunction];
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
  return '    {'+[true,r.brightness,r.presetAvailable,r.presetDeclared&&!r.presetAvailable,'NativeProvider::'+(provider==='rog-aura'?'RogAura':provider==='msi-claw-native-hid'?'MsiHid':'Helper'),r.declaredEffects.reduce((bits,n)=>n==='preset'?bits:bits|1<<colorNames.indexOf(n),0)].join(', ')+'}, // '+r.name;
 });
 const nativeRoutes=[
 'enum class NativeProvider : unsigned char { Helper, RogAura, MsiHid };',
 'struct Route {bool registered, brightness, preset, declaredPresetWithoutTransport; NativeProvider provider; unsigned colorEffects;};',
 '// Aligned to the shared identity table: one binary identity lookup, then constant-time operation/provider selection.',
 'inline constexpr std::array<Route, '+catalog.devices.length+'> routes = {{',...nativeRouteRows,' }};',
 'inline const Route* resolve(const ymcc::capabilities::Device* device) noexcept {',
 '    if (!device) return nullptr;',
 '    const auto index = static_cast<std::size_t>(device - ymcc::capabilities::devices.data());',
 '    return index < routes.size() && routes[index].registered ? &routes[index] : nullptr;','}',
 'inline const Route* resolve(std::string_view cls) noexcept {return resolve(ymcc::capabilities::find(cls));}',
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
 'inline bool validPreset(std::string_view cls, std::string_view id) noexcept {const auto* r=resolve(cls);return r && validPreset(*r,id);}'
 ];
 const header=['// Generated from capabilities/hc-device-catalog.json and hc-lighting-policy.json.','// Declared capabilities and implemented operations are separate. No device access.','#pragma once','#include <array>','#include <string_view>','#include "../capabilities/hc-device-catalog.generated.h"','namespace ymcc::lighting {','inline constexpr char kCatalogSha256[] = '+q(metadata.catalogSha256)+';',nativeIdentity,'struct Preset { const char* id; const char* label; int hcValue; };','inline constexpr std::array<Preset, '+presets.length+'> kOneXPresets = {{',...presets.map(p=>'    {'+q(p.id)+', '+q(p.label)+', 0x'+p.value.toString(16)+'},'),' }};',...nativeRoutes,'} // namespace ymcc::lighting',''].join('\n');
 const core=policy.independentCore;if(!/^[a-f0-9]{64}$/.test(core.sha256))throw Error('INVALID_LIGHTING_CORE_PIN');
 const independentRows=rows.filter(r=>r.independent);if(independentRows.length!==1||!core.actions.brightness||JSON.stringify(core.actions.color)!=='["solid","rainbow"]')throw Error('UNREVIEWED_INDEPENDENT_LIGHTING_ABI');
 const coreHeader=['// Generated from the unified lighting policy; the caller owns absolute paths.','#pragma once','#include <string>','namespace ymcc { namespace lighting {','inline constexpr char kIndependentDeviceCoreFileName[] = '+q(core.assemblyFile)+';','inline constexpr wchar_t kIndependentDeviceCorePathSuffix[] = L"\\\\light-setter\\\\'+core.assemblyFile+'";','inline constexpr char kIndependentDeviceCoreSha256[] = '+q(core.sha256)+';','inline bool useIndependentDeviceCore(const std::string& deviceClass, const std::string& action, const std::string& mode) {','    if (deviceClass != '+q(independentRows[0].deviceClass)+') return false;','    return action == "brightness" || (action == "color" && (mode == "solid" || mode == "rainbow"));','}','} } // namespace ymcc::lighting',''].join('\n');
 const pins='// Generated from capabilities/hc-lighting-policy.json.\ninternal static class LightingAssemblyPins\n{\n    internal const string StandaloneLightingSha256 = '+q(core.sha256)+';\n}\n';
 return {metadata,artifacts:{'capabilities/HcLightingCapabilityCatalog.generated.cs':cs,'native/main.cpp':generatedMain,'native/oem_lighting_operations.h':header,'native/lighting_device_core_binding.h':coreHeader,'FanLab/LightSetter/LightingAssemblyPins.cs':pins}};
}
