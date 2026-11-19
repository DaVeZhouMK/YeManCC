import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {isDeepStrictEqual} from 'node:util';
const q=JSON.stringify,sha=x=>createHash('sha256').update(x).digest('hex');
const BATTERY_POLICY_PATH='capabilities/hc-battery-policy.json';
const BATTERY_CATALOG_MODULE='tools/lib/hc_battery_catalog.mjs';
export const BATTERY_INPUT_PATHS=Object.freeze({
 runtimeRules:'native/BatteryHost/battery-routes.json',
 reviewedResearch:'docs/battery-charge-capabilities.json',
 reviewedProtocolOverride:'native/BatteryHost/msi-ap-route-review.json',
});
const SUPERX_ROUTE='oxp-superx-dual-ec',SUPERX_CLASS='OneXPlayerSuperX';
const SUPERX_FACTS='capabilities/upstream/xmax-6d728795/SuperXCharge.source-facts.json';
const X2_MINI_PRO_FACTS='capabilities/upstream/onexconsole-0.10.4-fix3/X2MiniProCharge.source-facts.json';
const SUPERX_FACTS_SHA256='e564eb505a90f8dd5e04070492ef80c3d8143342668bc6284d9e94660b42f3fb';
const X2_MINI_PRO_FACTS_SHA256='9e67e2fae0d48ac5966d30d9d24707c1c37697888223d24f8d6a050d24894c5f';
const OXP3_OVERLAY='native/BatteryHost/oxp3-battery-route.json';
const OXP3_SOURCE='capabilities/upstream/f2e58468/OneXPlayer3.cs';
const OXP3_DEVICE='capabilities/upstream/f2e58468/OneXPlayer3.json';
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
export function validateOxp3ChargeOverlay({repo}){
 const {schema,source,protocol,limits,bypassModes,...row}=JSON.parse(fs.readFileSync(path.join(repo,OXP3_OVERLAY),'utf8'));
 requireValue(schema===1,'UNREVIEWED_OXP3_BATTERY_SCHEMA');
 // Keep the existing system field until the HC238 field discrepancy is resolved.
 exact(row,{route:'oxp3-wmi-ec',family:'OneXPlayer 3',kind:'firmware-percent',min:40,
  identity:{manufacturers:['ONE-NETBOOK','ONE-NETBOOK TECHNOLOGY CO., LTD.'],field:'systemProduct',values:['ONEXPLAYER 3','ONEXPLAYER-3'],deviceClass:'OneXPlayer3'},
  firmwareGate:{kind:'provider-probe-required',majorMin:0,minorMin:0}},'UNREVIEWED_OXP3_BATTERY_RULE');
 keys(source,['pinnedSource','sha256','deviceJson','deviceJsonSha256','commit','upstreamSourcePin','parentX2','wmiEc','identityEvidence'],'UNREVIEWED_OXP3_BATTERY_SOURCE');
 requireValue(source.commit==='f2e58468d9917189d4be7ccfc242385ce2c3035b'&&source.pinnedSource===OXP3_SOURCE&&source.deviceJson===OXP3_DEVICE&&
  text(source.upstreamSourcePin)&&path.win32.basename(source.upstreamSourcePin)==='SOURCE-PIN.json'&&
  source.identityEvidence==='IDevice.cs switch ProductName ONEXPLAYER 3 / ONEXPLAYER-3 with ManufacturerName ONE-NETBOOK','UNREVIEWED_OXP3_BATTERY_SOURCE');
 for(const [file,declared,expected] of [
  [OXP3_SOURCE,source.sha256,'e3a6275e34db52fa60c7b5a421655af24d8580611fe9ebb875986817891809dc'],
  [OXP3_DEVICE,source.deviceJsonSha256,'5c0f9ae26f1b01233d0ffee78dc7831df2bf2447dacbbea7145f8a321617371a']
 ]){
  requireValue(declared===expected,'OXP3_BATTERY_DECLARED_SOURCE_HASH: '+file);
  requireValue(sha(fs.readFileSync(path.join(repo,file)))===expected,'OXP3_BATTERY_SOURCE_BYTES: '+file);
 }
 // Historical evidence paths are not portable build dependencies.
 for(const [ref,name,expected] of [
  [source.parentX2,'OneXPlayerX2.cs','41cf648a9d9247c2e1a5da29bed748f0aed0a8407202a690b0b866ae2d5c369b'],
  [source.wmiEc,'OneXPlayerWmiEc.cs','f84150a6e4714568e4cb14c019bd8e3cda394a32ef0cebe620a86f82c16a3021']
 ]){
  keys(ref,['path','sha256'],'OXP3_BATTERY_PARENT_SOURCE');
  requireValue(text(ref.path)&&path.win32.basename(ref.path)===name&&ref.sha256===expected,'OXP3_BATTERY_PARENT_SOURCE: '+name);
 }
 exact(protocol,{namespace:'root\\WMI',class:'SuRwECRegInterface',readMethod:'ReadECReg',writeMethod:'WriteECReg',limitRegister:'0x04A3',bypassRegister:'0x04A4',addressPacking:'bank | offset << 8 (0x04A3 -> 0xA304)',writePacking:'packedAddress | value << 16',response:'hex status,value; status must be 0'},'OXP3_BATTERY_PROTOCOL');
 exact(limits,{min:0,max:100,step:1,origin:'upstream source range; product choices remain 50..100 by 5'},'OXP3_BATTERY_LIMITS');
 exact(bypassModes,{0:'0x00',1:'0x01',2:'0x0B'},'OXP3_BATTERY_BYPASS');
 return {route:row.route,family:row.family,kind:row.kind,identity:row.identity,firmwareGate:row.firmwareGate,min:row.min};
}

export function validateSuperXChargeOverlay({repo}){
 const bytes=fs.readFileSync(path.join(repo,SUPERX_FACTS));
 // The overlay and fact sheet must not be able to attest to a new hash together.
 requireValue(sha(bytes)===SUPERX_FACTS_SHA256,'UNREVIEWED_SUPERX_BATTERY_SOURCE_FACTS');
 const facts=JSON.parse(bytes);
 const rule=JSON.parse(fs.readFileSync(path.join(repo,'native/BatteryHost/superx-charge-route.json'),'utf8'));
 const commit='6d728795511c2f9f683d1757e72481236de9b23a';
 exact(rule,{schema:1,route:SUPERX_ROUTE,family:'OneXPlayer Super X',kind:'firmware-percent',min:75,
  identity:SUPERX_IDENTITY,firmwareGate:SUPERX_GATE,
  source:{facts:SUPERX_FACTS,sha256:SUPERX_FACTS_SHA256,repository:'the-reengineers/XmaX',commit},
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

export function validateX2MiniProChargeOverlay({repo}){
 const bytes=fs.readFileSync(path.join(repo,X2_MINI_PRO_FACTS));
 requireValue(sha(bytes)===X2_MINI_PRO_FACTS_SHA256,'UNREVIEWED_X2_MINI_PRO_BATTERY_SOURCE_FACTS');
 const facts=JSON.parse(bytes);
 const name='ONEXPLAYER X2Mini PRO';
 const firmwareGate={kind:'oxp-x2-mini-pro-field-firmware',bios:'0.22',ecMajor:0,ecMinor:12};
 requireValue(facts.schema===1&&facts.source?.product===name&&facts.source.feature==='enableBatteryProtection'&&
   facts.source.accessType==='WinRing0/direct EC16'&&facts.source.limitAddress===1253&&facts.source.limitRegister==='0x04E5'&&
   facts.source.sha256==='2eb89e3244f44baeb508ccfaa6d98c73cc1849e4b7bdb08eba71b165aae52726','UNVERIFIED_X2_MINI_PRO_BATTERY_SOURCE');
 requireValue(facts.fieldEvidence?.boardProduct===name&&facts.fieldEvidence.systemProduct===name&&
   facts.fieldEvidence.bios===firmwareGate.bios&&facts.fieldEvidence.ecMajor===firmwareGate.ecMajor&&
   facts.fieldEvidence.ecMinor===firmwareGate.ecMinor,'UNVERIFIED_X2_MINI_PRO_BATTERY_FIRMWARE');
 exact(facts.runtime,{route:'oxp-x2-mini-pro-direct-ec',family:'OneXPlayer X2 Mini Pro',kind:'firmware-percent',
  identity:{manufacturers:['ONE-NETBOOK','ONE-NETBOOK TECHNOLOGY CO., LTD.'],field:'boardProduct',values:[name],deviceClass:'OneXPlayerX2MiniPro'},
  firmwareGate,min:50},'UNREVIEWED_X2_MINI_PRO_BATTERY_RULE');
 return facts.runtime;
}

export function readBatteryPolicy({repo}){
 const policy=JSON.parse(fs.readFileSync(path.join(repo,BATTERY_POLICY_PATH),'utf8'));
 requireValue(object(policy)&&policy.schemaVersion===1,'UNSUPPORTED_BATTERY_POLICY');
 for(const [key,expected] of Object.entries(BATTERY_INPUT_PATHS))
  requireValue(policy[key]===expected,'UNREVIEWED_BATTERY_POLICY_INPUT_PATH: '+key);
 return policy;
}

const probe={kind:'provider-probe-required'};
const overlayProbe={...probe,majorMin:0,minorMin:0};
const ec=(majorMin,minorMin)=>({kind:'hc-ec-components',majorMin,minorMin,unknownValue:255});
const pair=(boardId,firmware)=>({boardId,firmware});
const percent='firmware-percent',fixed='firmware-fixed',software='software-bypass-threshold';
// Reviewed consumer contracts, independent of the research projection.
const reviewedRouteSemantics=new Map([
 ['asus-ally-acpi',[percent,40,probe]],
 ['lenovo-go-conservation',[fixed,40,probe]],
 ['msi-claw-wmi',[percent,20,probe]],
 ['msi-claw-cg3em-ap',[percent,20,probe]],
 ['ayaneo-cec-bypass',[software,40,probe]],
 ['ayaneo-ceii-bypass',[software,40,probe]],
 ['oxp-x1-amd-ec',[percent,40,ec(1,3)]],
 ['oxp-x1-intel-ec',[percent,40,ec(0,67)]],
 ['oxp-x2-mini-pro-direct-ec',[percent,50,{kind:'oxp-x2-mini-pro-field-firmware',bios:'0.22',ecMajor:0,ecMinor:12}]],
 ['oxp3-wmi-ec',[percent,40,overlayProbe]],
 ['valve-deck-firmware',[percent,40,{kind:'exact-board-firmware',pairs:[pair(10,0xb030),...[0x1010,0x1050,0x1090,0x1100].map(fw=>pair(5,fw))]}]],
 ['gpd-win-mini-2025',[percent,60,{kind:'exact-reviewed-firmware',bios:'2.10',ecMajor:2,ecMinor:7}]],
 ['oxp-superx-dual-ec',[percent,75,overlayProbe]],
]);

export function validateBatteryRouteSemantics(rules,admittedProviderRoutes){
 requireValue(Array.isArray(rules),'BATTERY_ROUTE_SEMANTICS_INVALID: expected rule array');
 const seen=new Set();
 for(const [index,row] of rules.entries()){
  const spec=reviewedRouteSemantics.get(row?.route);
  if(!spec)throw Error(`BATTERY_ROUTE_SEMANTICS_INVALID: row ${index}: unexpected route ${q(row?.route)}`);
  seen.add(row.route);
  for(const [i,key] of ['kind','min','firmwareGate'].entries()){
   if(!isDeepStrictEqual(row[key],spec[i]))throw Error(`BATTERY_ROUTE_SEMANTICS_INVALID: ${row.route} row ${index}: ${key}; expected ${q(spec[i])}, got ${q(row[key])}`);
  }
 }
 for(const route of reviewedRouteSemantics.keys())if(!seen.has(route))throw Error(`BATTERY_ROUTE_SEMANTICS_INVALID: missing reviewed route ${route}`);
 requireValue(Array.isArray(admittedProviderRoutes),'BATTERY_PROVIDER_SET_INVALID: expected admittedProviderRoutes array');
 const admitted=new Set();
 for(const route of admittedProviderRoutes){
  if(!seen.has(route))throw Error(`BATTERY_PROVIDER_SET_INVALID: policy route has no reviewed runtime rule: ${q(route)}`);
  if(admitted.has(route))throw Error(`BATTERY_PROVIDER_SET_INVALID: duplicate policy route ${route}`);
  admitted.add(route);
 }
 for(const route of seen)if(!admitted.has(route))throw Error(`BATTERY_PROVIDER_SET_INVALID: missing policy route ${route}`);
 return seen.size;
}

function readReviewedJson(repo,file,digest,name){
 const bytes=fs.readFileSync(path.join(repo,file));
 assert.equal(sha(bytes),digest,'UNREVIEWED_BATTERY_'+name+': '+file);
 return JSON.parse(bytes);
}

export function buildReviewedBatteryRoutes({repo,policy=readBatteryPolicy({repo})}){
 const research=readReviewedJson(repo,BATTERY_INPUT_PATHS.reviewedResearch,'f73acd9e67758bc971857e1b3d47cfd38bbf2bdd341dcfeb0255960f441e2e40','BASE_RESEARCH');
 const excluded=new Set(['oxp-linux-platform','suiplay-cec-inheritance']);
 const rules=research.routes.filter(r=>!excluded.has(r.id)).flatMap(r=>r.exactIdentities.map(identity=>({route:r.id,family:r.family,kind:r.kind,identity,firmwareGate:r.firmwareGate,min:r.id==='msi-claw-wmi'?20:r.id==='gpd-win-mini-2025'?60:40})));
 // The reviewed CG3EM protocol applies only to the dedicated MS-1T91 group.
 const msiAp=readReviewedJson(repo,BATTERY_INPUT_PATHS.reviewedProtocolOverride,'3ef95add17b0214d263f0a7c49ebad7ab3be6fbcf6e20e9229ed591456e7f400','MSI_AP_REVIEW');
 assert.equal(msiAp.schema,1);assert.deepEqual(msiAp.boardProducts,['MS-1T91']);
 assert.equal(msiAp.selector,0);assert.equal(msiAp.valueByte,5);assert.equal(msiAp.packetBytes,32);assert.equal(msiAp.min,20);
 let replaced=0;
 for(const rule of rules)if(rule.route===msiAp.baseRoute&&rule.identity.field==='boardProduct'&&rule.identity.values.some(v=>msiAp.boardProducts.includes(v))){
  assert.ok(rule.identity.values.every(v=>msiAp.boardProducts.includes(v)),'Mixed MSI identity group must be split before overriding');
  rule.route=msiAp.route;rule.min=msiAp.min;replaced++;
 }
 assert.equal(replaced,1,'CG3EM override must match exactly one identity group');
 // Order here is stable serialization only; multiple matches never select a route.
 const x2=validateX2MiniProChargeOverlay({repo});
 assert.ok(!rules.some(r=>r.route===x2.route||r.identity.deviceClass===x2.identity.deviceClass));
 rules.push(x2);
 const oxp3=validateOxp3ChargeOverlay({repo});
 assert.ok(!rules.some(r=>r.route===oxp3.route),'OXP3 overlay route must not duplicate research routes');
 rules.push(oxp3);
 for(const item of rules){
  assert.ok(item.identity.manufacturerMatch===undefined||item.identity.manufacturerMatch==='normalized-exact','Unreviewed battery manufacturer predicate');
  assert.ok(item.identity.valueMatch===undefined||item.identity.valueMatch==='normalized-exact','Unreviewed battery value predicate');
  assert.notEqual(item.identity.deviceClass,SUPERX_CLASS,'Super X must be a reviewed extension, never a research/frozen alias');
 }
 const superx=validateSuperXChargeOverlay({repo});
 assert.ok(!rules.some(r=>r.route===superx.route),'Super X overlay route must not duplicate another route');
 rules.push({route:superx.route,family:superx.family,kind:superx.kind,identity:superx.identity,firmwareGate:superx.firmwareGate,min:superx.min});
 const routeCount=validateBatteryRouteSemantics(rules,policy.admittedProviderRoutes);
 return {rules,routeCount,research,x2,superx,bytes:Buffer.from(JSON.stringify(rules,null,2)+'\n','utf8')};
}

function reviewedDisabledModels({records,research,devices,rows}){
 requireValue(Array.isArray(records),'INVALID_BATTERY_DISABLED_POLICY');
 const byDevice=new Map();
 for(const record of records){
  keys(record,['device','researchRoute','reason'],'INVALID_BATTERY_DISABLED_POLICY');
  requireValue(text(record.device)&&text(record.researchRoute)&&text(record.reason),'INVALID_BATTERY_DISABLED_POLICY');
  requireValue(!byDevice.has(record.device),'DUPLICATE_BATTERY_DISABLED_MODEL: '+record.device);
  const source=research.routes.find(route=>route.id===record.researchRoute);
  requireValue(devices.has(record.device)&&source?.exactIdentities?.some(identity=>identity.deviceClass===record.device)&&
   !rows.some(row=>row.deviceClass===record.device||row.provider===record.researchRoute),'STALE_BATTERY_DISABLED_POLICY: '+record.device);
  byDevice.set(record.device,record);
 }
 return byDevice;
}

export function buildBatteryArtifacts({repo,catalog}){
 const policy=readBatteryPolicy({repo});
 strings(policy.reviewedExtensionModels,'INVALID_BATTERY_EXTENSION_POLICY');
 strings(policy.sourceProvenance,'INVALID_BATTERY_PROVENANCE_POLICY');
 for(const file of [BATTERY_POLICY_PATH,BATTERY_CATALOG_MODULE])
  requireValue(policy.sourceProvenance.includes(file),'MISSING_BATTERY_CATALOG_PROVENANCE: '+file);
 exact(policy.reviewedRouteOverlays,['native/BatteryHost/oxp3-battery-route.json','native/BatteryHost/superx-charge-route.json',X2_MINI_PRO_FACTS],'UNREVIEWED_BATTERY_OVERLAYS');
 const {rules,research,x2,superx,bytes}=buildReviewedBatteryRoutes({repo,policy});
 requireValue(policy.reviewedExtensionModels.includes(SUPERX_CLASS),'UNREGISTERED_SUPERX_BATTERY_ROUTE');
 for(const file of [SUPERX_FACTS,...Object.values(SUPERX_CONSUMERS)])
  requireValue(policy.sourceProvenance.includes(file)||['native/BatteryHost/Policy.cs','native/BatteryHost/Controller.cs'].includes(file),'MISSING_SUPERX_BATTERY_PROVENANCE: '+file);
 // Compare the resource once to the same reviewed bytes used for the binder.
 requireValue(fs.readFileSync(path.join(repo,policy.runtimeRules)).equals(bytes),
  'BATTERY_REVIEWED_RULES_DIFFER: BATTERY_RUNTIME_RULE_BYTES_DIFFER');
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
 requireValue(rows.filter(r=>r.provider===x2.route).length===1&&rows.filter(r=>r.deviceClass===x2.identity.deviceClass).length===1&&extensions.has(x2.identity.deviceClass),'X2_MINI_PRO_BATTERY_RULE_CARDINALITY');
 if([...extensions].some(name=>by.has(name)||!rows.some(r=>r.deviceClass===name)))throw Error('STALE_BATTERY_EXTENSION_POLICY');
 const disabledByDevice=reviewedDisabledModels({records:policy.disabledSourceModels,research,devices:by,rows});
 const coverage=catalog.devices.map(d=>{const declared=d.declared.Capabilities.possible.filter(x=>x.startsWith('Battery'));const registered=rows.filter(r=>r.deviceId===d.deviceId).map(r=>r.ruleId),disabled=disabledByDevice.get(d.name);if(d.factoryInstantiable&&declared.length&&!registered.length&&!disabled)throw Error('UNCLASSIFIED_BATTERY_SOURCE_GAP: '+d.name);return {deviceId:d.deviceId,deviceClass:d.name,factory:d.factoryInstantiable,declared,registeredRules:registered,status:registered.length?'registered-exact-identity':disabled?'reviewed-not-admitted':declared.length?'non-factory-source-base':'no-source-declaration',reason:disabled?.reason??null};});
 const provenance=[...new Set([policy.reviewedResearch,policy.reviewedProtocolOverride,...policy.reviewedRouteOverlays,'tools/battery-charge-generate-routes.mjs','native/BatteryHost/Policy.cs','native/BatteryHost/Controller.cs','native/BatteryHost/Providers.cs','native/BatteryHost/MsiWmi.cs','native/BatteryHost/MsiApProvider.cs',...policy.sourceProvenance,OXP3_SOURCE,OXP3_DEVICE])].map(file=>({file:relative(file),sha256:sha(fs.readFileSync(path.join(repo,file)))}));
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
