import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
const q=JSON.stringify,sha=x=>createHash('sha256').update(x).digest('hex');
const SUPERX_ROUTE='oxp-superx-dual-ec',SUPERX_CLASS='OneXPlayerSuperX';
const SUPERX_FACTS='capabilities/upstream/xmax-6d728795/SuperXCharge.source-facts.json';
const SUPERX_IDENTITY={manufacturers:['ONE-NETBOOK'],field:'boardProduct',values:['ONEXPLAYER SUPER X'],deviceClass:SUPERX_CLASS,manufacturerMatch:'ordinal-contains',valueMatch:'ordinal-exact'};
const SUPERX_GATE={kind:'provider-probe-required',majorMin:0,minorMin:0};
const SUPERX_CONSUMERS={reader:'native/BatteryHost/SuperXUmaChargeReader.cs',writer:'native/BatteryHost/SuperXCheckedEcWriter.cs',provider:'native/BatteryHost/SuperXChargeProvider.cs',dispatcher:'capabilities/HcBatteryCapabilityCatalog.cs',policy:'native/BatteryHost/Policy.cs',controller:'native/BatteryHost/Controller.cs',inProcessOwner:'native/BatteryHost/InProcessEntry.cs',domainOwner:'native/BatteryHost/HostDomainRouter.cs'};
function requireValue(ok,code){if(!ok)throw Error(code);}
function exact(actual,expected,code){try{assert.deepEqual(actual,expected);}catch(cause){throw Error(code,{cause});}}
const object=x=>x!==null&&typeof x==='object'&&!Array.isArray(x);
const text=x=>typeof x==='string'&&x.trim().length>0;
function strings(values,code){requireValue(Array.isArray(values)&&values.length>0&&values.every(text)&&new Set(values).size===values.length,code);}
function relative(file){requireValue(text(file)&&/^[A-Za-z0-9_./-]+$/.test(file)&&!path.isAbsolute(file)&&file.split('/').every(p=>p!==''&&p!=='.'&&p!=='..'),'INVALID_BATTERY_PROVENANCE_PATH');return file;}
function keys(value,allowed,code){requireValue(object(value)&&Object.keys(value).every(key=>allowed.includes(key)),code);}

// Shared by the original route generator and catalogue builder. Importing this
// module performs no generation, subprocess work, WMI, driver or hardware IO.
export function validateSuperXChargeOverlay({repo}){
 const bytes=fs.readFileSync(path.join(repo,SUPERX_FACTS)),facts=JSON.parse(bytes);
 const rule=JSON.parse(fs.readFileSync(path.join(repo,'native/BatteryHost/superx-charge-route.json'),'utf8'));
 const commit='6d728795511c2f9f683d1757e72481236de9b23a';
 exact(rule,{schema:1,route:SUPERX_ROUTE,family:'OneXPlayer Super X',kind:'firmware-percent',min:75,
  identity:SUPERX_IDENTITY,firmwareGate:SUPERX_GATE,
  source:{facts:SUPERX_FACTS,sha256:sha(bytes),repository:'the-reengineers/XmaX',commit},
  protocol:{limitRegister:'0x04A3',readTransport:'wmi-uma-ec16',writeTransport:'checked-it87-superio-ec16',reader:SUPERX_CONSUMERS.reader,writer:SUPERX_CONSUMERS.writer,provider:SUPERX_CONSUMERS.provider},
  limits:{min:75,max:100,step:5}},'UNREVIEWED_SUPERX_BATTERY_OVERLAY');
 requireValue(facts.schemaVersion===1&&facts.source?.repository==='the-reengineers/XmaX'&&facts.source?.commit===commit,'SUPERX_BATTERY_FIXED_SOURCE_AUTHORITY');
 requireValue(Array.isArray(facts.source.files),'SUPERX_BATTERY_SOURCE_FILES');
 exact(facts.source.files.map(({path,bytes,sha256})=>({path,bytes,sha256})),[
  {path:'backend/src/platform/platform_win32.cpp',bytes:87825,sha256:'bd7bd2feaad1b4eb90ecf98c78a2a915d1b1235c6b0b30268b1bb66b2232a59c'},
  {path:'backend/src/power.h',bytes:3187,sha256:'ecbfeb63b634fc1727a671b814fb415b49ca562fadab5fe1a5c0ef045adf6ece'},
  {path:'backend/src/power.cpp',bytes:2821,sha256:'06489cbb478542f2b0e7ec71584ac8b5f72f48c492c69ceaec3e094c1b4044d8'}
 ],'SUPERX_BATTERY_SOURCE_FILES');
 const identity=facts.identity;
 requireValue(object(identity)&&identity.repository==='torvalds/linux'&&identity.commit==='0b6573e23acc7bca808e539e3edea49683f106de'&&identity.file==='drivers/platform/x86/oxpec.c'&&identity.sha256==='161fa21f5bd4e332914880dff780224a17a0ee4a0c94c467069b6555885974e3'&&identity.boardVendorContains==='ONE-NETBOOK'&&identity.boardNameExact==='ONEXPLAYER SUPER X'&&identity.predicateCase==='Ordinal'&&identity.systemProductAlias===false&&identity.processorPredicate===false&&identity.existingFanFacts==='capabilities/upstream/xmax-6d728795/SuperXFan.source-facts.json','SUPERX_BATTERY_BOARD_IDENTITY');
 const fan=JSON.parse(fs.readFileSync(path.join(repo,identity.existingFanFacts),'utf8'));
 requireValue(fan.identity?.commit===identity.commit&&fan.identity?.sha256===identity.sha256&&fan.identity?.boardVendorContains===identity.boardVendorContains&&fan.identity?.boardNameExact===identity.boardNameExact&&fan.identity?.predicateCase==='Ordinal','SUPERX_BATTERY_FAN_IDENTITY_DRIFT');
 exact(facts.protocol,{
  limitRegister:'0x04A3',
  read:{namespace:'root\\WMI',class:'UMAInterface',instanceNameContains:'SETUMAWMI',method:'GetEcValue',indexCallerVariant:'VT_I4',address:'raw EC16',actualData:['byte','Int32 0..255','UInt32 0..255','UInt16 0..255 only for CIM UInt16 managed projection'],endpoint:'Unique matching instance, retained exact nonempty __PATH rechecked before every invocation',queryTimeoutSeconds:3,invokeOptionsTimeoutSeconds:3,invokeDeadlineEnforced:false},
  write:{transport:'checked-it87-superio-ec16',slot:1,registerPort:'0x4E',dataPort:'0x4F',unlock:['0x87','0x01','0x55','0xAA'],indexedSelectors:['0x2E','0x2F'],addressHighIndex:'0x11',addressLowIndex:'0x10',dataIndex:'0x12',exitWrite:'none in pinned source',selectOperation:'ioctl_select_slot',writeOperation:'ioctl_pio_outb'},
  range:{minimum:75,maximum:100,source:'backend/src/power.h CHARGE_LIMIT_MIN / CHARGE_LIMIT_MAX',productStep:5,productChoices:[75,80,85,90,95,100]}
 },'SUPERX_BATTERY_PROTOCOL_SCOPE');
 exact(facts.fixedModule,{library:'deps/ymcc-shared-runtime/bin/LibreHardwareMonitorLib.dll',librarySha256:'f7ed30f07ea636c0ddcc5764c15c0a25ae8a0aca02ded77e4af24439955487cf',resource:'LibreHardwareMonitor.Resources.PawnIo.LpcIO.bin',resourceBytes:17172,resourceSha256:'3dcf8b2bc80ff642d97c4608511a818642b5bf315ff53df3df393d043e71d101',newDllOrModule:false},'SUPERX_BATTERY_FIXED_MODULE');
 exact(facts.consumers,SUPERX_CONSUMERS,'SUPERX_BATTERY_CONSUMER_SOURCE');
 requireValue(facts.implementation==='Original YMCC checked native ABI adaptation from audited protocol facts; no upstream implementation copied'&&object(facts.ymccAdaptation)&&text(facts.ymccAdaptation.loaderReason),'SUPERX_BATTERY_CHECKED_ADAPTATION');
 return rule;
}

export function buildBatteryArtifacts({repo,catalog}){
 const policy=JSON.parse(fs.readFileSync(path.join(repo,'capabilities/hc-battery-policy.json'),'utf8'));
 requireValue(policy.schemaVersion===1,'UNSUPPORTED_BATTERY_POLICY');
 strings(policy.admittedProviderRoutes,'INVALID_BATTERY_PROVIDER_POLICY');
 strings(policy.reviewedExtensionModels,'INVALID_BATTERY_EXTENSION_POLICY');
 strings(policy.sourceProvenance,'INVALID_BATTERY_PROVENANCE_POLICY');
 exact(policy.reviewedRouteOverlays,['native/BatteryHost/oxp3-battery-route.json','native/BatteryHost/superx-charge-route.json'],'UNREVIEWED_BATTERY_OVERLAYS');
 const superx=validateSuperXChargeOverlay({repo});
 requireValue(policy.admittedProviderRoutes.includes(SUPERX_ROUTE)&&policy.reviewedExtensionModels.includes(SUPERX_CLASS),'UNREGISTERED_SUPERX_BATTERY_ROUTE');
 for(const file of [SUPERX_FACTS,...Object.values(SUPERX_CONSUMERS)])
  requireValue(policy.sourceProvenance.includes(file)||['native/BatteryHost/Policy.cs','native/BatteryHost/Controller.cs'].includes(file),'MISSING_SUPERX_BATTERY_PROVENANCE: '+file);
 const checked=spawnSync(process.execPath,[path.join(repo,'tools/battery-charge-generate-routes.mjs'),'--check'],{cwd:repo,encoding:'utf8',windowsHide:true,timeout:30000});
 if(checked.error||checked.status!==0)throw Error('BATTERY_REVIEWED_RULES_DIFFER: '+(checked.error?.message??checked.stderr));
 const bytes=fs.readFileSync(path.join(repo,policy.runtimeRules)),rules=JSON.parse(bytes),research=JSON.parse(fs.readFileSync(path.join(repo,policy.reviewedResearch),'utf8'));
 requireValue(Array.isArray(rules),'INVALID_BATTERY_RULE_DOCUMENT');
 const by=new Map(catalog.devices.map(d=>[d.name,d])),extensions=new Set(policy.reviewedExtensionModels),ruleKeys=new Set(),identityKeys=new Set();
 requireValue(!by.has(SUPERX_CLASS),'SUPERX_BATTERY_MUST_BE_REVIEWED_EXTENSION');
 const rows=rules.map((rule,index)=>{
  keys(rule,['route','family','kind','identity','firmwareGate','min'],'INVALID_BATTERY_RULE_SHAPE');
  requireValue(text(rule.route)&&text(rule.family)&&['firmware-percent','firmware-fixed','software-bypass-threshold'].includes(rule.kind)&&Number.isInteger(rule.min)&&rule.min>=0&&rule.min<=100&&object(rule.firmwareGate)&&text(rule.firmwareGate.kind),'INVALID_BATTERY_RULE_SHAPE');
  const identity=rule.identity;
  keys(identity,['manufacturers','field','values','deviceClass','processorPattern','manufacturerMatch','valueMatch'],'INVALID_BATTERY_IDENTITY_RULE');
  requireValue(text(identity.deviceClass)&&['boardProduct','systemProduct'].includes(identity.field)&&(!Object.hasOwn(identity,'processorPattern')||identity.processorPattern===null||text(identity.processorPattern)),'INVALID_BATTERY_IDENTITY_RULE');
  strings(identity.manufacturers,'INVALID_BATTERY_IDENTITY_MANUFACTURERS');strings(identity.values,'INVALID_BATTERY_IDENTITY_VALUES');
  if(identity.processorPattern!==undefined&&identity.processorPattern!==null)new RegExp(identity.processorPattern,'i');
  const manufacturerMatch=identity.manufacturerMatch===undefined?'normalized-exact':identity.manufacturerMatch;
  const valueMatch=identity.valueMatch===undefined?'normalized-exact':identity.valueMatch;
  const special=rule.route===SUPERX_ROUTE||identity.deviceClass===SUPERX_CLASS||manufacturerMatch!=='normalized-exact'||valueMatch!=='normalized-exact';
  if(special){
   exact({route:rule.route,family:rule.family,kind:rule.kind,identity,firmwareGate:rule.firmwareGate,min:rule.min},
    {route:superx.route,family:superx.family,kind:superx.kind,identity:superx.identity,firmwareGate:superx.firmwareGate,min:superx.min},'UNREVIEWED_BATTERY_IDENTITY_PREDICATE');
  }
  const name=identity.deviceClass,device=by.get(name);
  if(!device&&!extensions.has(name))throw Error('UNREVIEWED_BATTERY_MODEL_EXTENSION: '+name);
  if(!policy.admittedProviderRoutes.includes(rule.route))throw Error('UNREGISTERED_BATTERY_PROVIDER: '+rule.route);
  const ruleId='battery.rule.'+rule.route+'.'+name;
  requireValue(!ruleKeys.has(ruleId),'DUPLICATE_BATTERY_RULE');ruleKeys.add(ruleId);
  for(const manufacturer of identity.manufacturers)for(const value of identity.values){
   const normalize=x=>x.trim().toUpperCase();
   const key=q([identity.field,manufacturerMatch,valueMatch,manufacturerMatch==='normalized-exact'?normalize(manufacturer):manufacturer,valueMatch==='normalized-exact'?normalize(value):value,identity.processorPattern??null]);
   requireValue(!identityKeys.has(key),'DUPLICATE_BATTERY_IDENTITY');identityKeys.add(key);
  }
  return {index,ruleId,deviceId:device?.deviceId??'ymcc.device.'+name,deviceClass:name,origin:device?'frozen-HC-identity':'reviewed-native-extension',sourceDeclaredBattery:!!device?.declared.Capabilities.possible.some(c=>c.startsWith('Battery')),provider:rule.route,identity,firmwareGate:rule.firmwareGate,min:rule.min,kind:rule.kind};
 });
 requireValue(rows.filter(r=>r.provider===SUPERX_ROUTE).length===1&&rows.filter(r=>r.deviceClass===SUPERX_CLASS).length===1,'SUPERX_BATTERY_RULE_CARDINALITY');
 if([...extensions].some(name=>by.has(name)||!rows.some(r=>r.deviceClass===name)))throw Error('STALE_BATTERY_EXTENSION_POLICY');
 for(const disabled of policy.disabledSourceModels){if(!by.has(disabled.device)||rows.some(r=>r.deviceClass===disabled.device)||!research.routes.some(r=>r.id===disabled.researchRoute))throw Error('STALE_BATTERY_DISABLED_POLICY');}
 const coverage=catalog.devices.map(d=>{const declared=d.declared.Capabilities.possible.filter(x=>x.startsWith('Battery'));const registered=rows.filter(r=>r.deviceId===d.deviceId).map(r=>r.ruleId),disabled=policy.disabledSourceModels.find(r=>r.device===d.name);if(d.factoryInstantiable&&declared.length&&!registered.length&&!disabled)throw Error('UNCLASSIFIED_BATTERY_SOURCE_GAP: '+d.name);return {deviceId:d.deviceId,deviceClass:d.name,factory:d.factoryInstantiable,declared,registeredRules:registered,status:registered.length?'registered-exact-identity':disabled?'reviewed-not-admitted':declared.length?'non-factory-source-base':'no-source-declaration',reason:disabled?.reason??null};});
 const provenance=[...new Set([policy.reviewedResearch,policy.reviewedProtocolOverride,...policy.reviewedRouteOverlays,'tools/battery-charge-generate-routes.mjs','native/BatteryHost/Policy.cs','native/BatteryHost/Controller.cs','native/BatteryHost/Providers.cs','native/BatteryHost/MsiWmi.cs','native/BatteryHost/MsiApProvider.cs',...policy.sourceProvenance])].map(file=>({file:relative(file),sha256:sha(fs.readFileSync(path.join(repo,file)))}));
 const metadata={schemaVersion:1,ruleDocument:policy.runtimeRules,ruleDocumentSha256:sha(bytes),registeredRules:rows.length,frozenModelRules:rows.filter(r=>r.origin==='frozen-HC-identity').length,reviewedExtensionRules:rows.filter(r=>r.origin!=='frozen-HC-identity').length,providerRoutes:policy.admittedProviderRoutes,disabledSourceModels:policy.disabledSourceModels,rows,sourceCoverage:coverage,provenance};
 metadata.catalogSha256=sha(JSON.stringify(metadata));
 const cs=['// Generated by tools/hc_capability_catalog_generate.mjs. No runtime IO.','namespace YeManFanBackend.Capabilities;','public static partial class HcBatteryCapabilityCatalog','{','    public const string RuleDocumentSha256 = '+q(metadata.ruleDocumentSha256)+';','    public const string CatalogSha256 = '+q(metadata.catalogSha256)+';','    internal static readonly BatteryCapabilityDescriptor[] GeneratedRules = {',...rows.map(r=>{
  const args=[q(r.ruleId),q(r.deviceId),q(r.deviceClass),q(r.origin),q(r.provider),q(r.identity.field),'new[] { '+r.identity.manufacturers.map(q).join(', ')+' }','new[] { '+r.identity.values.map(q).join(', ')+' }',r.identity.processorPattern?q(r.identity.processorPattern):'null',String(r.sourceDeclaredBattery)];
  // Existing generated rows retain their original constructor text. Optional
  // descriptor defaults preserve normalized exact matching for those 41 rows.
  if(r.identity.manufacturerMatch!==undefined||r.identity.valueMatch!==undefined)args.push(q(r.identity.manufacturerMatch??'normalized-exact'),q(r.identity.valueMatch??'normalized-exact'));
  return '        new('+args.join(', ')+'),';
 }),'    };','}',''].join('\n');
 return {metadata,artifacts:{'capabilities/HcBatteryCapabilityCatalog.generated.cs':cs}};
}
