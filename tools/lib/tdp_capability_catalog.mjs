import fs from 'node:fs';
import {createHash} from 'node:crypto';
const sha=text=>createHash('sha256').update(text).digest('hex');
const need=(ok,code)=>{if(!ok)throw Error('TDP_CATALOG_'+code);};
const quote=JSON.stringify;
function python(value,key='') {
 if(value===null)return 'None';
 if(typeof value==='boolean')return value?'True':'False';
 if(typeof value==='string'||typeof value==='number')return JSON.stringify(value);
 if(Array.isArray(value)){
  const tuples=key==='co_scopes'||key==='pbo_ops'||key==='pbo_op';
  const values=value.map(x=>python(x,key==='pbo_ops'?'pbo_op':''));
  return tuples?'('+values.join(', ')+(values.length===1?',':'')+')':'['+values.join(', ')+']';
 }
 return '{\n'+Object.entries(value).map(([k,v])=>'    '+quote(k)+': '+python(v,k)+',').join('\n')+'\n}';
}
function validateUpstreamRecipes(p){
 const bytes=fs.readFileSync(new URL('../../capabilities/upstream/uxtu-26.3.1/RyzenSmu.cs',import.meta.url));
 const identity=p.source?.upstreamFiles?.find(f=>f.path==='Scripts/AMD Backend/RyzenSmu.cs');
 need(identity?.sha256===sha(bytes),'UPSTREAM_SOURCE_IDENTITY');
 const source=bytes.toString('utf8');
 const sockets={FAM_APU_FT5_FP5:'Socket_FT5_FP5_AM4',FAM_APU_FP6:'Socket_FP6_AM4',FAM_APU_FT6_OLD:'Socket_FT6_FP7_FP8',FAM_APU_FT6_NEW:'Socket_FT6_FP7_FP8',FAM_APU_FT6_NEW_GENERIC:'Socket_FT6_FP7_FP8',FAM_APU_VANGOGH:'Socket_FF3',FAM_DESKTOP_AM4_V1:'Socket_AM4_V1',FAM_DESKTOP_AM4_V2:'Socket_AM4_V2',FAM_DESKTOP_AM5:'Socket_AM5_V1',FAM_DRAGON_FIRE:'Socket_AM5_V1',FAM_HSMP_FP10_AM5:'Socket_FP10_AM5'};
 const encode=rows=>JSON.stringify([...new Set(rows.map(r=>JSON.stringify(r)))].sort());
 for(const [name,r] of Object.entries(p.familyRecipes)){
   need(typeof r.co_supported==='boolean'&&Array.isArray(r.co_commands),'CPU_CO_CANDIDATES_SCHEMA:'+name);
   need(r.co_commands.every(c=>c&&Object.keys(c).length===2&&['mp1','rsmu','hsmp'].includes(c.transport)&&Number.isSafeInteger(c.command)&&c.command>0&&c.command<=0xffffffff),'CPU_CO_CANDIDATE_SHAPE:'+name);
   if(!r.tdp_supported){need(!Object.hasOwn(sockets,name),'DISABLED_KNOWN_TDP_RECIPE:'+name);need(!r.co_supported&&r.co_commands.length===0,'DISABLED_CPU_CO_CANDIDATES:'+name);continue;}
  const method=sockets[name];need(method,'UNREVIEWED_TDP_FAMILY:'+name);
  const start=source.indexOf('private static void '+method+'()'),end=source.indexOf('private static void ',start+1);
  need(start>=0,'UPSTREAM_SOCKET_MISSING:'+name);let body=source.slice(start,end<0?undefined:end);
  if(method==='Socket_AM5_V1'){
   const branch=body.indexOf('if (Family.FAM == Family.RyzenFamily.DragonRange'),stop=body.indexOf('return;',branch);
   need(branch>=0&&stop>branch,'AM5_SOURCE_BRANCH_BOUNDARY');
   body=body.slice(0,branch)+(name==='FAM_DRAGON_FIRE'?body.slice(branch,stop):body.slice(stop+'return;'.length));
  }
  const commands=[...body.matchAll(/\("([^"]+)"\s*,\s*(true|false)\s*,\s*(0[xX][0-9a-fA-F]+)\)/g)].map(m=>[m[1],method==='Socket_FP10_AM5'?'hsmp':m[2]==='true'?'mp1':'rsmu',Number(m[3])]);
  const fields=r.tdp_mode==='ppt'?['ppt-limit']:['stapm-limit','fast-limit','slow-limit'];
  const expected=commands.filter(c=>fields.includes(c[0]) && !(name==='FAM_DESKTOP_AM5'&&c[0]==='slow-limit'&&c[1]==='rsmu'&&c[2]===0xCB));const actual=[];
  // 9950 already works on the three MP1 operations. Import the declaration,
  // but do not add this source-only desktop operation to the existing route.
  if(r.tdp_mode==='ppt'){for(const [label,transport,cmd] of r.pbo_ops??[])if(label==='PPT')actual.push(['ppt-limit',transport,cmd]);}
  else for(const key of ['stapm','fast','slow']){actual.push([key+'-limit',r.transport,r[key]]);if(r['mirror_'+key]!==null&&r['mirror_'+key]!==undefined)actual.push([key+'-limit',r.mirror_transport,r['mirror_'+key]]);}
  need(encode(actual)===encode(expected),'UPSTREAM_TDP_COMMAND_TRANSPORT:'+name);
  if(r.pbo_supported){const expectedPbo=commands.filter(c=>['ppt-limit','tdc-limit','edc-limit'].includes(c[0]));need(encode((r.pbo_ops??[]).map(([label,transport,cmd])=>[label.toLowerCase()+'-limit',transport,cmd]))===encode(expectedPbo),'UPSTREAM_PBO_COMMAND_TRANSPORT:'+name);}
  for(const [prefix,field] of (method==='Socket_FP10_AM5'?[['HSMP','hsmp_msg'],['HSMP','hsmp_rsp'],['HSMP','hsmp_arg']]:[['MP1','msg'],['MP1','rsp'],['MP1','arg'],['PSMU','psmu_msg'],['PSMU','psmu_rsp'],['PSMU','psmu_arg']])){
   const suffix=field.endsWith('msg')?'MSG':field.endsWith('rsp')?'RSP':'ARG';
   const values=[...body.matchAll(new RegExp(prefix+'_ADDR_'+suffix+'\\s*=\\s*(0[xX][0-9a-fA-F]+)','g'))].map(m=>Number(m[1]));
   need(values.includes(r[field]),'UPSTREAM_MAILBOX_ADDRESS:'+name+':'+field);
  }
   const sourceCo=commands.filter(c=>c[0]==='set-coall').map(([,transport,command])=>({transport,command}));
   const uniqueCo=sourceCo.filter((c,i)=>sourceCo.findIndex(v=>v.transport===c.transport&&v.command===c.command)===i);
   need(r.co_supported===(uniqueCo.length>0),'UPSTREAM_CPU_CO_COVERAGE:'+name);
   if(r.co_supported){
    need(r.co_scopes?.length===1&&r.co_scopes[0]==='all_core','CPU_CO_SCOPE:'+name);
    const primary=uniqueCo.find(c=>c.transport===r.co_transport&&c.command===r.coall);
    need(primary,'UPSTREAM_CPU_CO_COMMAND_TRANSPORT:'+name);
    // Existing successful primary stays first (notably AM5 MP1 0x36).
    // Import every remaining same-operation upstream candidate in source order.
    const expectedCo=[primary,...uniqueCo.filter(c=>c!==primary)];
    need(JSON.stringify(r.co_commands.map(c=>[c.transport,c.command]))===JSON.stringify(expectedCo.map(c=>[c.transport,c.command])),'UPSTREAM_CPU_CO_CANDIDATE_COVERAGE_AND_ORDER:'+name);
   }else need(r.co_commands.length===0,'UNSUPPORTED_CPU_CO_CANDIDATES:'+name);
 }
}
function validateCpuIdentities(p){
 const bytes=fs.readFileSync(new URL('../../capabilities/upstream/uxtu-26.3.1/Family.cs',import.meta.url));
 need(p.source.upstreamFiles.find(f=>f.path==='Scripts/Family.cs')?.sha256===sha(bytes),'UPSTREAM_CPU_IDENTITY_SOURCE');
 const text=bytes.toString('utf8'),expected=[];
 for(const family of [23,25,26]){
  const start=text.indexOf('if (CPUFamily == '+family+')');need(start>=0,'UPSTREAM_CPU_FAMILY');
  const open=text.indexOf('{',start);let end=open+1,depth=1;
  for(;end<text.length&&depth;end++){if(text[end]==='{')depth++;if(text[end]==='}')depth--;}
  const body=text.slice(open+1,end-1);
  for(const m of body.matchAll(/(?:else\s+)?if\s*\(([^;{}]+)\)\s*FAM\s*=\s*RyzenFamily\.(\w+)/g)){
   let models=[...new Set([...m[1].matchAll(/CPUModel\s*==\s*(\d+)/g)].map(x=>Number(x[1])))];
   const range=m[1].match(/CPUModel\s*>=\s*(\d+)\s*&&\s*CPUModel\s*<=\s*(\d+)/);
   if(range)models=Array.from({length:Number(range[2])-Number(range[1])+1},(_,i)=>i+Number(range[1]));
   const names=[...m[1].matchAll(/CPUName\.Contains\("([^"]+)"\)/g)].map(x=>x[1]);
   expected.push({family,models,upstreamFamily:m[2],...(names.length?{nameContainsAny:names}:{})});
  }
 }
 const smu=fs.readFileSync(new URL('../../capabilities/upstream/uxtu-26.3.1/RyzenSmu.cs',import.meta.url),'utf8');
 const socketByFamily=new Map();
 for(const m of smu.matchAll(/if \(([^\n]+)\) (Socket_\w+)\(\);/g))for(const name of m[1].matchAll(/Family\.RyzenFamily\.(\w+)/g))socketByFamily.set(name[1],m[2]);
 const sockets={FAM_DESKTOP_AM5:'Socket_AM5_V1',FAM_DRAGON_FIRE:'Socket_AM5_V1',FAM_DESKTOP_AM4_V1:'Socket_AM4_V1',FAM_DESKTOP_AM4_V2:'Socket_AM4_V2',FAM_APU_FT5_FP5:'Socket_FT5_FP5_AM4',FAM_APU_FP6:'Socket_FP6_AM4',FAM_APU_VANGOGH:'Socket_FF3',FAM_APU_FT6_NEW:'Socket_FT6_FP7_FP8',FAM_APU_FT6_NEW_GENERIC:'Socket_FT6_FP7_FP8',FAM_APU_FT6_OLD:'Socket_FT6_FP7_FP8',FAM_HSMP_FP10_AM5:'Socket_FP10_AM5'};
 const actual=p.amdCpuIdentification;
 need(Array.isArray(actual)&&actual.every(x=>socketByFamily.get(x.upstreamFamily)===sockets[x.recipe]),'UPSTREAM_CPU_SOCKET_ASSIGNMENT');
 need(Array.isArray(actual)&&actual.length===expected.length,'UPSTREAM_CPU_IDENTITY_COVERAGE');
 const key=x=>JSON.stringify([x.family,x.models,x.upstreamFamily,x.nameContainsAny??[]]);
 need(actual.every((x,i)=>key(x)===key(expected[i])&&Object.hasOwn(p.familyRecipes,x.recipe)),'UPSTREAM_CPU_IDENTITY_RULES');
}
function validateCpuOffsetProducer(p){
 const read=(file,upstream)=>{const bytes=fs.readFileSync(new URL('../../capabilities/upstream/uxtu-26.3.1/'+file,import.meta.url));need(p.source.upstreamFiles.find(f=>f.path===upstream)?.sha256===sha(bytes),'UPSTREAM_CO_PRODUCER_SOURCE:'+file);return bytes.toString('utf8');};
 const main=read('MainWindow.xaml.cs','Views/Windows/MainWindow.xaml.cs');
 const presets=read('CustomPresets.xaml.cs','Views/Pages/CustomPresets.xaml.cs');
 const translator=read('RyzenAdj_To_UXTU.cs','Scripts/RyzenAdj_To_UXTU.cs');
 need(main.includes('private static uint EncodeCurveOptimiserOffset(int offset)')&&main.includes('return 0x100000u - (uint)magnitude;')&&presets.includes('--set-coall={Convert.ToUInt32(0x100000 - (uint)(-1 * (int)nudAllCO.Value))}'),'UPSTREAM_CO_NEGATIVE_SIGNED20');
 need(translator.includes('ryzenAdjCommandValue = ConvertHsmpPsmMargin(ryzenAdjCommandValue);')&&translator.includes('(value & 0x80000) != 0')&&translator.includes('(int)value - 0x100000')&&translator.includes('unchecked((ushort)(short)Math.Clamp(margin, short.MinValue, short.MaxValue))'),'UPSTREAM_HSMP_CO_SIGNED16');
 need(p.voltageOffset.amd.encodingProducer?.nonHsmp==='u20'&&p.voltageOffset.amd.encodingProducer?.hsmp==='u16'&&p.voltageOffset.amd.encodingProducer?.scope==='YMCC static all-core CPU offset only','CPU_CO_ENCODING_PRODUCER_DECLARATION');
 for(const [name,r] of Object.entries(p.familyRecipes))if(r.co_supported)need(r.co_enc===(r.co_transport==='hsmp'?'u16':'u20'),'CPU_CO_ENCODING_PRODUCER:'+name);
}
export function validateTdpPolicy(p,catalog){
 validateCpuOffsetProducer(p);
 need(p?.schemaVersion===1&&p.libraryId==='ymcc.uxtu.tdp'&&p.version==='26.3.1','POLICY_SCHEMA');
 need(p.activation?.tdp===true&&p.activation.coreSwitch===false&&p.activation.voltageScheduling===false,'ACTIVATION_BOUNDARY');
 need(p.ccdControl?.operation==='process_affinity'&&p.ccdControl.effectScope==='global_process_affinity_by_l3_domain'&&p.ccdControl.mechanism==='SetProcessAffinityMask'&&p.ccdControl.topologySource==='windows_relation_cache_l3'&&p.ccdControl.minimumDomains===2&&p.ccdControl.physicalCcdPowerOff===false&&p.ccdControl.activation===true,'CCD_DECLARATION');
 need(p.voltageOffset?.operation==='static_offset'&&p.voltageOffset.activation===true,'VOLTAGE_OFFSET_DECLARATION');
 const intelBytes=fs.readFileSync(new URL('../../capabilities/upstream/uxtu-26.3.1/Intel_Management.cs',import.meta.url));
 need(p.source.upstreamFiles.find(f=>f.path==='Scripts/Intel Backend/Intel_Management.cs')?.sha256===sha(intelBytes),'UPSTREAM_INTEL_SOURCE_IDENTITY');
 const intel=intelBytes.toString('utf8'),ic=p.intelCpuCapabilities;
 need(intel.includes('public static void changeVoltageOffset(int value, int voltagePlane)')&&intel.includes('0 => 0x80000011UL')&&intel.includes('2 => 0x80000211UL')&&intel.includes('intelPawnIO!.WriteMsr(0x150, msrValue)'),'UPSTREAM_INTEL_CPU_PLANES_AND_WRITE');
 need(ic?.admission==='cpu_identity_and_runtime_channel'&&ic.vendor==='Intel'&&ic.familyModelRequired===true&&ic.modelNames==='diagnostics_only'&&ic.tdp?.candidate===true&&ic.tdp.mechanism==='package_rapl_msr_0x610'&&ic.voltage?.candidate===true&&JSON.stringify(ic.voltage.planes)==='["core","cache"]'&&ic.voltage.mechanism==='fivr_msr_0x150'&&ic.hardwareAcceptance===false,'INTEL_CPU_CANDIDATE_ADMISSION');
 need(p.manualControls?.mode==='explicit_settings_apply'&&p.manualControls.restoreSavedOnStartup===false&&p.manualControls.automaticReapply===false&&p.manualControls.physicalCcdPowerOff===false&&JSON.stringify(p.manualControls.voltageConsumerPlanes)==='["core","cache"]','MANUAL_CONTROL_BOUNDARY');
 // Fixed upstream continues after every non-OK. YMCC admits only typed FE/FF
 // for this same-value static setter. Raw FAILED can also be transport/mutex.
 const amdSource=fs.readFileSync(new URL('../../capabilities/upstream/uxtu-26.3.1/RyzenSmu.cs',import.meta.url),'utf8');
 need(amdSource.includes('var originalArguments = (uint[])args.Clone();')&&amdSource.includes('Array.Copy(originalArguments, args, originalArguments.Length);')&&amdSource.includes('if (status == Status.OK)')&&amdSource.includes('if (status != Status.UNKNOWN_CMD)')&&/FAILED\s*=\s*0xFF/.test(amdSource)&&/UNKNOWN_CMD\s*=\s*0xFE/.test(amdSource),'UPSTREAM_CPU_CO_STATUS_CONTRACT');
 const cs=p.voltageOffset.amd?.candidateSelection;
 need(cs?.recipeField==='co_commands'&&JSON.stringify(cs.primaryFields)==='["co_transport","coall"]'&&cs.order==='existing-primary-then-upstream'&&cs.retryOnly?.origin==='firmware_response'&&JSON.stringify(cs.retryOnly.mailboxStatuses)==='[254,255]'&&!Object.hasOwn(cs.retryOnly,'mailboxStatus')&&cs.stopOnSuccess===true&&cs.retryOnTransportFailure===false&&cs.retryOnUnknownResult===false&&cs.eachCandidateAtMostOnce===true&&cs.originalArgumentsPerAttempt===true&&cs.hardwareAcceptanceFromDeclaration===false,'CPU_CO_CANDIDATE_EXECUTION_CONTRACT');
 need(p.voltageOffset.amd?.unit==='co_step'&&p.voltageOffset.amd.minimum===-60&&p.voltageOffset.amd.maximum===60&&p.voltageOffset.amd.step===1&&p.voltageOffset.amd.scope==='all_core'&&p.voltageOffset.amd.readback===false&&p.voltageOffset.amd.encodingSource==='familyRecipes.co_enc','AMD_VOLTAGE_DECLARATION');
 need(p.voltageOffset.intel?.unit==='mv'&&p.voltageOffset.intel.minimum===-150&&p.voltageOffset.intel.maximum===0&&p.voltageOffset.intel.step===1&&p.voltageOffset.intel.scope==='fivr_plane'&&JSON.stringify(p.voltageOffset.intel.planes)==='["core","cache"]'&&p.voltageOffset.intel.readback===true&&p.voltageOffset.intel.encoding==='msr_0x150_offset_bits_31_21_signed11_scale1.024'&&p.voltageOffset.intel.command==='set_vf_direct','INTEL_VOLTAGE_DECLARATION');
 // Keep only the already-consumed core/cache direct SET source values here.
 // GET/readback/completion remains the existing YMCC protocol, not UXTU data.
 const ip=p.voltageOffset.intel.protocol;
 need(ip?.msr===0x150&&JSON.stringify(ip.planeDomains)==='{"core":0,"cache":2}'&&ip.setCommand===0x11&&ip.offsetScale===1.024&&ip.offsetBits===11&&ip.offsetShift===21&&ip.settleMs===100,'INTEL_VOLTAGE_PROTOCOL');
 need(intel.includes('Math.Round(value * 1.024) << 21')&&intel.includes('Thread.Sleep(100)'),'UPSTREAM_INTEL_ENCODING_AND_SETTLE');
 need(p.dispatchContract?.replaceScheduler===false&&p.dispatchContract.hardwareAcceptanceFromCatalog===false&&p.dispatchContract.applyHcDeclaredLimits===false,'DISPATCH_BOUNDARY');
 need(p.applicationRequestDomain?.minimumW===2&&p.applicationRequestDomain.maximumW===200&&p.applicationRequestDomain.machineMaximumKnown===false,'REQUEST_DOMAIN');
 need(p.dispatchContract.intelSingleTargetPl2Offset===1,'INTEL_SINGLE_TARGET');
 need(p.dispatchContract.preserveWorkingRoutes===true&&p.dispatchContract.newModuleActivation===false,'PRESERVE_WORKING_ROUTES');
 need(p.deferredCapabilities?.length===1&&p.deferredCapabilities[0].id==='am5-desktop-slow-rsmu-cb'&&p.deferredCapabilities[0].familyRecipe==='FAM_DESKTOP_AM5'&&p.deferredCapabilities[0].operation==='slow-limit'&&p.deferredCapabilities[0].transport==='rsmu'&&p.deferredCapabilities[0].command===0xCB&&p.deferredCapabilities[0].upstreamDeclared===true&&p.deferredCapabilities[0].runtimeApplied===false,'DEFERRED_DESKTOP_DECLARATION');
 need(Array.isArray(p.oemProviders)&&p.oemProviders.length===1&&p.oemProviders[0].id==='msi-g3e-wmi'&&p.oemProviders[0].exactNormalizedBoard==='MS-1T91'&&p.oemProviders[0].minimumW===8&&p.oemProviders[0].maximumW===200,'EXISTING_OEM_PROVIDER');
 need(Array.isArray(catalog?.devices)&&catalog.devices.length>0&&new Set(catalog.devices.map(d=>d.deviceId)).size===catalog.devices.length,'MODEL_SOURCE');
 need(Array.isArray(p.familyOrder)&&p.familyOrder.length===14&&new Set(p.familyOrder).size===14&&p.familyOrder.every(n=>Object.hasOwn(p.familyRecipes,n)),'FAMILY_MEMBERSHIP');
 need(Object.keys(p.familyRecipes).length===14&&p.familyRecipes.FAM_UNKNOWN?.tdp_supported===false,'UNKNOWN_FAMILY');
 for(const [alias,name] of Object.entries(p.familyAliases))need(/^FAM_[A-Z0-9_]+$/.test(alias)&&Object.hasOwn(p.familyRecipes,name),'FAMILY_ALIAS');
 for(const name of p.familyOrder){const r=p.familyRecipes[name];need(/^FAM_[A-Z0-9_]+$/.test(name)&&typeof r.tag==='string'&&r.allow_legacy_fallback===false,'FAMILY_DECLARATION');}
 need(p.moduleCandidates.length===3&&new Set(p.moduleCandidates.map(m=>m.name)).size===3&&p.moduleCandidates.every(m=>/^[a-f0-9]{64}$/.test(m.sha256)&&Number.isSafeInteger(m.bytes)&&m.bytes>0),'MODULE_IDENTITY');
 need(p.moduleCandidates.every(m=>['RyzenSMU.bin','IntelMSR.bin','AMDFamily17.bin'].includes(m.name)),'MODULE_NAMES');
 validateUpstreamRecipes(p);
 validateCpuIdentities(p);
 return p;
}
export function tdpDomain(catalog,policy){
 validateTdpPolicy(policy,catalog);
 return {schemaVersion:1,libraryId:policy.libraryId,version:policy.version,policyFile:'capabilities/uxtu-tdp-policy.json',
  policySha256:sha(JSON.stringify(policy)),modelBinding:{source:'diagnostics-only; not capability, admission or watt limits',registeredModels:catalog.devices.length,unknownModel:'retain-unknown-model; existing CPU provider still resolves its own protocol'},
  consumers:{native:'native/main.cpp::tdpExecutionCapability',helper:'PowerControl/pawnio/YeManTdpCtl.py::_AMD_FAMILIES',frontend:'src/bridge/yeman.ts::getTdpExecutionCapability',manualCcd:'native/main.cpp::cpu.topology/cpu.setCcdMode',manualCpuOffset:'PowerControl/pawnio/YeManTdpCtl.py::amd_uv_set_with/intel_uv_set_pawn'},
  dispatch:'existing-engine-owners-generation-and-write-gates-retained',applyHcDeclaredLimits:false,
  machineMaximumKnown:false,activation:policy.activation,hardwareAcceptance:false,workingRoutesPreserved:true,newModulesActivated:false,deferredCapabilities:policy.deferredCapabilities,
  ccdControl:policy.ccdControl,voltageOffset:policy.voltageOffset,manualControls:policy.manualControls};
}
export function buildTdpArtifacts({catalog,policy,helper}){
 const metadata=tdpDomain(catalog,policy),digest=metadata.policySha256;
 const generated=['# BEGIN GENERATED UXTU CAPABILITY DATA',`# Source: capabilities/uxtu-tdp-policy.json; semantic SHA256 ${digest}`,`UXTU_CAPABILITY_LIBRARY_ID = ${quote(policy.libraryId)}`,`UXTU_CAPABILITY_LIBRARY_VERSION = ${quote(policy.version)}`,`UXTU_CAPABILITY_POLICY_SHA256 = ${quote(digest)}`,
  'CCD_POLICY = '+python(policy.ccdControl),'UV_POLICY = '+python(policy.voltageOffset),'AMD_CPU_IDENTITIES = '+python(policy.amdCpuIdentification),'INTEL_CPU_POLICY = '+python(policy.intelCpuCapabilities),
  ...Object.entries(policy.familyRecipes).map(([name,value])=>name+' = '+python(value)),
  ...Object.entries(policy.familyAliases).map(([alias,name])=>alias+' = '+name),
  '_AMD_FAMILIES = ['+policy.familyOrder.join(', ')+']','# END GENERATED UXTU CAPABILITY DATA'].join('\n');
 const normalized=helper.replace(/\r\n/g,'\n');let next;
 if(normalized.includes('# BEGIN GENERATED UXTU CAPABILITY DATA')){
  need(normalized.split('# BEGIN GENERATED UXTU CAPABILITY DATA').length===2&&normalized.split('# END GENERATED UXTU CAPABILITY DATA').length===2,'HELPER_MARKERS');
   const region=helper.includes('\r\n')?generated.replace(/\n/g,'\r\n'):generated;
   // Consumer owner may edit outside the region concurrently. Preserve that
   // exact text, including mixed line endings; generation owns only markers.
   next=helper.replace(/# BEGIN GENERATED UXTU CAPABILITY DATA[\s\S]*?# END GENERATED UXTU CAPABILITY DATA/,()=>region);
 }else{
  const start=normalized.indexOf('FAM_DESKTOP_AM5 = {'),end=normalized.indexOf('\n\ndef _read_cpu_info()',start);
  need(start>=0&&end>start&&normalized.slice(start,end).includes('_AMD_FAMILIES = ['),'HELPER_ORIGINAL_REGION');
  next=normalized.slice(0,start)+generated+'\n'+normalized.slice(end);
 }
  if(!normalized.includes('# BEGIN GENERATED UXTU CAPABILITY DATA')&&helper.includes('\r\n'))next=next.replace(/\n/g,'\r\n');
 const oem=policy.oemProviders[0],app=policy.applicationRequestDomain;
 const header=['// Generated from uxtu-tdp-policy.json and the existing unified model alias catalog.','#pragma once','#include "hc-device-catalog.generated.h"','#include <string_view>','namespace ymcc::capabilities::tdp {',
  `inline constexpr std::string_view libraryId=${quote(policy.libraryId)}, version=${quote(policy.version)}, policySha256=${quote(digest)};`,
  `inline constexpr std::string_view msiBoard=${quote(oem.exactNormalizedBoard)};`,
  `inline constexpr int intelPl2Offset=${policy.dispatchContract.intelSingleTargetPl2Offset};`,
  'struct RequestDomain {int minimumW,maximumW;};',
  `inline constexpr RequestDomain defaultDomain{${app.minimumW},${app.maximumW}}, msiDomain{${oem.minimumW},${oem.maximumW}};`,
  'inline constexpr RequestDomain requestDomain(bool oemSelected) noexcept {return oemSelected?msiDomain:defaultDomain;}',
  'inline std::string_view modelId(std::string_view modelClass) noexcept {return ymcc::capabilities::deviceId(modelClass);}',
  'inline constexpr bool coreSwitchActive=false,voltageSchedulingActive=false,applyHcDeclaredLimits=false;',
  `inline constexpr bool ccdControlActive=${policy.ccdControl.activation?'true':'false'},voltageOffsetActive=${policy.voltageOffset.activation?'true':'false'},ccdPhysicalPowerOff=false;`,
  `inline constexpr std::string_view ccdOperation=${quote(policy.ccdControl.operation)},ccdEffectScope=${quote(policy.ccdControl.effectScope)},ccdMechanism=${quote(policy.ccdControl.mechanism)},ccdTopologySource=${quote(policy.ccdControl.topologySource)};`,
  `inline constexpr int ccdMinimumDomains=${policy.ccdControl.minimumDomains};`,
  `inline constexpr std::string_view amdVoltageUnit=${quote(policy.voltageOffset.amd.unit)},intelVoltageUnit=${quote(policy.voltageOffset.intel.unit)};`,
  `inline constexpr int amdVoltageMinimum=${policy.voltageOffset.amd.minimum},amdVoltageMaximum=${policy.voltageOffset.amd.maximum},amdVoltageStep=${policy.voltageOffset.amd.step},intelVoltageMinimum=${policy.voltageOffset.intel.minimum},intelVoltageMaximum=${policy.voltageOffset.intel.maximum},intelVoltageStep=${policy.voltageOffset.intel.step};`,
  '} // namespace ymcc::capabilities::tdp',''].join('\n');
 const manual='// Generated from capabilities/uxtu-tdp-policy.json. Explicit settings actions only.\n'+'export const CCD_MANUAL_CONTROL_ACTIVE = '+policy.ccdControl.activation+';\n'+'export const CPU_VOLTAGE_OFFSET_ACTIVE = '+policy.voltageOffset.activation+';\n';
 return {metadata,artifacts:{'capabilities/TdpCapabilityCatalog.generated.h':header,'PowerControl/pawnio/YeManTdpCtl.py':next,'src/bridge/cpuCapabilityPolicy.generated.ts':manual}};
}

