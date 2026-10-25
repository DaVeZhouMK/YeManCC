import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';

const root=process.cwd(), req=createRequire(path.join(root,'package.json'));
const {build}=req('esbuild');
const evidence=path.resolve(process.env.YMCC_FAN_FEATURE_EVIDENCE_DIR||path.join(root,'../../../_scratch/fan-feature-snapshot-'+Date.now()));
fs.mkdirSync(evidence,{recursive:true});
const featurePath=path.join(root,'src/bridge/fanFeature.ts');
const override=process.env.YMCC_FAN_FEATURE_SOURCE_OVERRIDE,curveOverride=process.env.YMCC_FAN_CURVE_SOURCE_OVERRIDE;let curveOverrideLoads=0,featureOverrideLoads=0;
const mocks={
 './topmon':"import {ref} from 'vue';export const topMonitorData=ref({tempC:55});",
 './settingsRepository':`export async function readSettingsSection(){const f=globalThis.__fanFeatureFixture;f.reads++;if(f.readError)throw Error('SETTINGS_READ_FAILED');return f.saved;}
 export async function saveSettingsSection(section,value){const f=globalThis.__fanFeatureFixture;f.saves.push({section,value});if(f.saveError)throw Error('SETTINGS_WRITE_FAILED');}
  export async function updateSettings(){throw Error('Unexpected settings transaction outside this fixture scope');}`,
 './fanApi':"export function createFanApiAdapter(){throw Error('This gate must not open a real Fan API');}",
 './fanDiagnostics':`export async function readFanDiagnosticLogging(){globalThis.__fanFeatureFixture.logReads++;return false;}
 export function adoptFanDiagnosticLoggingState(){}export async function configureFanDiagnosticLogging(){}`,
};
const result=await build({stdin:{resolveDir:root,contents:"export * from './src/bridge/fanFeature';export {isReactive,reactive} from 'vue';export {validateFanNodes} from './src/bridge/fanCurve';",loader:'ts'},bundle:true,write:false,format:'esm',platform:'node',plugins:[{name:'actual-feature-with-inert-platform',setup(b){
 b.onResolve({filter:/^\.\//},a=>path.dirname(a.importer)===path.join(root,'src/bridge')&&a.path in mocks?{path:a.path,namespace:'feature-fixture'}:undefined);
 b.onLoad({filter:/.*/,namespace:'feature-fixture'},a=>({contents:mocks[a.path],loader:'ts',resolveDir:root}));
 if(override)b.onLoad({filter:/[\\/]src[\\/]bridge[\\/]fanFeature\.ts$/},()=>{featureOverrideLoads++;return{contents:fs.readFileSync(override,'utf8'),loader:'ts',resolveDir:path.dirname(featurePath)};});
  if(curveOverride)b.onLoad({filter:/[\\/]src[\\/]bridge[\\/]fanCurve\.ts$/},a=>{curveOverrideLoads++;return{contents:fs.readFileSync(curveOverride,'utf8'),loader:'ts',resolveDir:path.dirname(a.path)};});
}}]});
if(override&&featureOverrideLoads!==1)throw Error('feature override load '+featureOverrideLoads);
if(curveOverride&&curveOverrideLoads!==1)throw Error('curve override load '+curveOverrideLoads);
const bundle=path.join(evidence,'feature-fixture.mjs');fs.writeFileSync(bundle,result.outputFiles[0].text);
const rows=[],hostCurveCases=[],curveReadWork=[];let generation=0;
const nodes=d=>[{tempC:0,dutyPercent:d},{tempC:40,dutyPercent:Math.max(d,35)},{tempC:70,dutyPercent:Math.max(d,70)},{tempC:100,dutyPercent:100}];
const saved=()=>({configured:true,preset:'balanced',motionEnabled:true,deviceIdentity:{model:'fixture-device',nested:{name:'fixture'}},nodes:nodes(10),presetCurves:{soft:nodes(5),balanced:nodes(10),aggressive:nodes(25)}});
async function fresh(values={}){const f=globalThis.__fanFeatureFixture={saved:saved(),reads:0,logReads:0,saves:[],...values};const module=await import(pathToFileURL(bundle).href+'?generation='+generation++);return {module,f};}
async function check(name,work){try{const detail=await work();rows.push({name,ok:true,detail});console.log('PASS '+name);}catch(e){rows.push({name,ok:false,error:String(e)});console.log('FAIL '+name+': '+String(e));}}

await check('initialization returns deep plain and structuredClone-safe data',async()=>{const {module:m}=await fresh();const s=await m.initializeFanFeature();assert.equal(m.isReactive(s.nodes),false);assert.equal(m.isReactive(s.presetCurves),false);assert.equal(m.isReactive(s.deviceIdentity),false);assert.deepEqual(structuredClone(s),s);return {proxy:false};});
await check('coalesced initialization gives each caller an independent result',async()=>{const {module:m,f}=await fresh();const [a,b]=await Promise.all([m.initializeFanFeature(),m.initializeFanFeature()]);assert.equal(f.reads,1);assert.equal(f.logReads,1);assert.notEqual(a,b);assert.notEqual(a.nodes,b.nodes);assert.notEqual(a.presetCurves.soft,b.presetCurves.soft);a.presetCurves.soft[0].dutyPercent=99;a.deviceIdentity.nested.name='changed';assert.equal(b.presetCurves.soft[0].dutyPercent,5);assert.equal(b.deviceIdentity.nested.name,'fixture');assert.equal(m.getFanPresetCurve('soft')[0].dutyPercent,5);return {reads:f.reads,logReads:f.logReads};});
await check('mutating an initialization result cannot edit live settings or factory defaults',async()=>{const {module:m}=await fresh();const s=await m.initializeFanFeature();s.presetCurves.aggressive[0].dutyPercent=99;s.nodes[1].dutyPercent=99;s.deviceIdentity.nested.name='caller';assert.equal(m.getFanPresetCurve('aggressive')[0].dutyPercent,25);assert.equal(m.getFanFeatureSettings().deviceIdentity.nested.name,'fixture');assert.equal(m.DEFAULT_FAN_PRESET_CURVES.aggressive[0].dutyPercent,0);});
await check('subsequent initialization returns the current detached settings without another read',async()=>{const {module:m,f}=await fresh();await m.initializeFanFeature();await m.saveFanCurve(nodes(40),'aggressive');const s=await m.initializeFanFeature();assert.equal(s.preset,'aggressive');assert.deepEqual(s.nodes,nodes(40));assert.equal(f.reads,1);assert.equal(f.saves.length,1);});
await check('reading one preset does not clone unrelated device identity',async()=>{const {module:m}=await fresh();await m.initializeFanFeature();let reads=0;const identity={model:'fixture',get observation(){reads++;return 'identity';}};await m.recordFanHandshake(true,identity);reads=0;assert.deepEqual(m.getFanPresetCurve('soft'),nodes(5));assert.equal(reads,0,'single-curve query must not traverse identity');const all=m.getFanFeatureSettings();assert.equal(all.deviceIdentity.observation,'identity');assert.equal(reads,1,'full snapshot must still detach identity');return {identityReadsForPreset:0,identityReadsForFullSnapshot:reads};});
await check('reading one valid preset does not clone the whole three-preset factory table',async()=>{const {module:m}=await fresh();await m.initializeFanFeature();const original=globalThis.structuredClone;let wholeTableClones=0;globalThis.structuredClone=(value,...args)=>{if(value&&Array.isArray(value.soft)&&Array.isArray(value.balanced)&&Array.isArray(value.aggressive))wholeTableClones++;return original(value,...args);};try{assert.deepEqual(m.getFanPresetCurve('balanced'),nodes(10));assert.equal(wholeTableClones,0);}finally{globalThis.structuredClone=original;}return {wholeTableClones};});
await check('returned preset arrays and nodes are independent across calls',async()=>{const {module:m}=await fresh();await m.initializeFanFeature();const a=m.getFanPresetCurve('balanced'),b=m.getFanPresetCurve('balanced');assert.notEqual(a,b);assert.notEqual(a[0],b[0]);a[0].dutyPercent=99;assert.deepEqual(b,nodes(10));assert.deepEqual(m.getFanPresetCurve('balanced'),nodes(10));});
await check('initialization and read queries neither save settings nor grant remembered-device handshake',async()=>{const {module:m,f}=await fresh();await m.initializeFanFeature();for(const p of ['soft','balanced','aggressive'])m.getFanPresetCurve(p);m.getFanFeatureSettings();assert.equal(f.saves.length,0);assert.equal(m.fanFeatureEnabled.value,false);await m.recordFanHandshake(true);assert.equal(m.fanFeatureEnabled.value,true);});
await check('read failure returns detached defaults and does not persist defaults',async()=>{const {module:m,f}=await fresh({readError:true});const a=await m.initializeFanFeature(),b=await m.initializeFanFeature();assert.equal(m.isReactive(a.nodes),false);assert.notEqual(a,b);assert.deepEqual(a.nodes,m.DEFAULT_FAN_PRESET_CURVES.balanced);assert.equal(f.reads,1);assert.equal(f.saves.length,0);assert.equal(m.fanFeatureEnabled.value,false);});
await check('storage rejection stays a rejection and leaves other presets and manual handshake reachable',async()=>{const {module:m,f}=await fresh();await m.initializeFanFeature();f.saveError=true;await assert.rejects(m.saveFanCurve(nodes(45),'aggressive'),/SETTINGS_WRITE_FAILED/);assert.deepEqual(m.getFanPresetCurve('soft'),nodes(5));assert.deepEqual(m.getFanPresetCurve('balanced'),nodes(10));f.saveError=false;await m.recordFanHandshake(true);assert.equal(m.fanFeatureEnabled.value,true);});
await check('valid, invalid and legacy curves retain the original validation and per-preset fallback',async()=>{const {module:m}=await fresh();const defaults=m.DEFAULT_FAN_PRESET_CURVES;let checks=0;const bad=[null,[],nodes(20).slice(0,3),[{tempC:1,dutyPercent:5},...nodes(20).slice(1)],[...nodes(20).slice(0,2),{tempC:86,dutyPercent:70},nodes(20)[3]],[...nodes(20).slice(0,3),{tempC:100,dutyPercent:49}],nodes(20).map((n,i)=>i===1?{...n,tempC:NaN}:n),nodes(20).map((n,i)=>i===2?{...n,dutyPercent:1}:n)];
 for(const active of ['soft','balanced','aggressive']){const legacy=nodes(15);const migrated=m.normalizeFanPresetCurves(undefined,legacy,active);for(const p of ['soft','balanced','aggressive']){assert.deepEqual(migrated[p],p===active?legacy:defaults[p]);checks++;}
 for(const corrupt of bad){const input={soft:nodes(5),balanced:nodes(10),aggressive:nodes(25)};input[active]=corrupt;const out=m.normalizeFanPresetCurves(input,undefined,active);assert.deepEqual(out[active],defaults[active]);for(const p of ['soft','balanced','aggressive'])if(p!==active)assert.deepEqual(out[p],input[p]);checks+=3;}}
 return {normalizationChecks:checks};});
await check('single-preset query matches full snapshot after an invalid edited curve',async()=>{let checks=0;for(const p of ['soft','balanced','aggressive']){const {module:m}=await fresh();await m.initializeFanFeature();await m.saveFanCurve(nodes(20).slice(0,3),p);const full=m.getFanFeatureSettings();for(const q of ['soft','balanced','aggressive']){assert.deepEqual(m.getFanPresetCurve(q),full.presetCurves[q]);checks++;}}return {checks};});

const rangeCorruptions=()=>{
 const base=nodes(0),change=(at,field,value)=>base.map((n,i)=>i===at?{...n,[field]:value}:{...n});
 return [change(0,'dutyPercent',-1),base.map((n,i)=>({...n,dutyPercent:[-3,-2,-1,50][i]})),change(3,'dutyPercent',101),base.map((n,i)=>({...n,dutyPercent:101+i})),base.map((n,i)=>({...n,dutyPercent:[0,101,102,103][i]})),change(3,'tempC',101),change(3,'tempC',1e10),change(3,'dutyPercent',1e10)];
};
await check('Host-rejected persisted ranges fall back only within the affected preset',async()=>{
 const {module:m}=await fresh();let subchecks=0;
 for(const active of ['soft','balanced','aggressive'])for(const input of rangeCorruptions()){
  const table={soft:nodes(5),balanced:nodes(10),aggressive:nodes(25)};table[active]=input;
  const actual=m.normalizeFanPresetCurves(table,undefined,active);
  hostCurveCases.push({name:'table-'+active+'-'+hostCurveCases.length,input,output:actual[active],inputAccepted:false});
  assert.deepEqual(actual[active],m.DEFAULT_FAN_PRESET_CURVES[active],'out-of-range persisted curve survived');
  assert(m.validateFanNodes(actual[active]));for(const other of ['soft','balanced','aggressive'])if(other!==active)assert.deepEqual(actual[other],table[other]);subchecks++;
 }
 const valid=[nodes(0),nodes(100),[{tempC:0,dutyPercent:0},{tempC:0,dutyPercent:0},{tempC:85,dutyPercent:50},{tempC:85,dutyPercent:50}],[{tempC:0,dutyPercent:0.25},{tempC:40.5,dutyPercent:35.5},{tempC:70.5,dutyPercent:70.25},{tempC:99.5,dutyPercent:99.5}],nodes(10).map(n=>({tempC:String(n.tempC),dutyPercent:String(n.dutyPercent)}))];
 for(const input of valid){const actual=m.normalizeFanPresetCurves({soft:input},undefined,'soft').soft,expected=input.map(n=>({tempC:Number(n.tempC),dutyPercent:Number(n.dutyPercent)}));assert.deepEqual(actual,expected);hostCurveCases.push({name:'valid-'+hostCurveCases.length,input:expected,output:actual,inputAccepted:true});subchecks++;}
 return {subchecks};
});
await check('invalid stored ranges keep read-only defaults and current handshake reachable',async()=>{
 let subchecks=0;
 for(const active of ['soft','balanced','aggressive'])for(const source of ['table','legacy'])for(const input of [rangeCorruptions()[0],rangeCorruptions()[2],rangeCorruptions()[5]]){
  const raw=saved();raw.preset=active;raw.nodes=input;if(source==='legacy')delete raw.presetCurves;else raw.presetCurves[active]=input;const original=structuredClone(raw);
  const {module:m,f}=await fresh({saved:raw}),settings=await m.initializeFanFeature();
  hostCurveCases.push({name:source+'-init-'+active+'-'+hostCurveCases.length,input,output:settings.nodes,inputAccepted:false});
  assert.equal(settings.preset,active);assert.deepEqual(settings.nodes,m.DEFAULT_FAN_PRESET_CURVES[active]);assert.deepEqual(m.getFanPresetCurve(active),settings.nodes);assert(m.validateFanNodes(settings.nodes));
  assert.equal(f.reads,1);assert.equal(f.saves.length,0,'read fallback wrote user settings');assert.deepEqual(f.saved,original);
  assert.equal(m.fanFeatureEnabled.value,false);await m.recordFanHandshake(true);assert.equal(m.fanFeatureEnabled.value,true,'invalid old curve blocked a new successful handshake');assert.equal(f.saves.length,0);subchecks++;
 }
 return {subchecks};
});

await check('invalid edited curve lookup clones only the required preset fallback',async()=>{
 const {module:m}=await fresh();await m.initializeFanFeature();const invalid=nodes(20).map((node,i)=>i===1?{...node,dutyPercent:1}:node);await m.saveFanCurve(invalid,'soft');
 const original=globalThis.structuredClone;let clones=0;globalThis.structuredClone=(value,...args)=>{if(Array.isArray(value)&&value.length===4&&value.every(n=>n&&typeof n.tempC==='number'&&typeof n.dutyPercent==='number'))clones++;return original(value,...args);};
 try{for(let i=0;i<100;i++)assert.deepEqual(m.getFanPresetCurve('soft'),m.DEFAULT_FAN_PRESET_CURVES.soft);}finally{globalThis.structuredClone=original;}
 const evidence={curveReads:100,defaultCurveClones:clones};curveReadWork.push(evidence);assert.equal(clones,100,'invalid lookup still clones defaults only to discard them');return evidence;
});


await check('full valid snapshots do not construct discarded factory curves', async () => {
 const {module:m}=await fresh();await m.initializeFanFeature();
 const original=globalThis.structuredClone;let tableClones=0,curveClones=0;
 globalThis.structuredClone=(value,...args)=>{
  if(value===m.DEFAULT_FAN_PRESET_CURVES)tableClones++;
  if(['soft','balanced','aggressive'].some(p=>value===m.DEFAULT_FAN_PRESET_CURVES[p]))curveClones++;
  return original(value,...args);
 };
 try{for(let i=0;i<100;i++){
  const snapshot=m.getFanFeatureSettings();
  assert.deepEqual(snapshot.presetCurves,{soft:nodes(5),balanced:nodes(10),aggressive:nodes(25)});
  assert.deepEqual(snapshot.nodes,nodes(10));snapshot.presetCurves.soft[0].dutyPercent=99;
 }}finally{globalThis.structuredClone=original;}
 assert.equal(tableClones,0,'full snapshot constructed a discarded default table');
 assert.equal(curveClones,0,'full snapshot constructed discarded default curves');
 return {fullSnapshots:100,factoryTableClones:tableClones,factoryCurveClones:curveClones};
});

await check('all partial tables preserve per-preset migration and numeric output', async () => {
 const {module:m}=await fresh();const presets=['soft','balanced','aggressive'];let subchecks=0;
 const source={soft:nodes(5),balanced:nodes(10),aggressive:nodes(25)};
 for(let mask=0;mask<8;mask++)for(const active of presets)for(const legacyKind of ['valid','numeric','invalid','missing']){
  const table=Object.fromEntries(presets.map((key,i)=>[key,(mask&(1<<i))?source[key]:[{tempC:0,dutyPercent:-1}]]));
  const legacy=legacyKind==='valid'?nodes(15):legacyKind==='numeric'?nodes(15).map(n=>({tempC:String(n.tempC),dutyPercent:String(n.dutyPercent)})):legacyKind==='invalid'?[]:undefined;
  const actual=m.normalizeFanPresetCurves(table,legacy,active);
  for(const [i,key] of presets.entries()){
   const expected=(mask&(1<<i))?source[key]:key===active&&['valid','numeric'].includes(legacyKind)?nodes(15):m.DEFAULT_FAN_PRESET_CURVES[key];
   assert.deepEqual(actual[key],expected,`${mask}/${active}/${legacyKind}/${key}`);
   assert.notEqual(actual[key],expected);assert.notEqual(actual[key][0],expected[0]);subchecks++;
  }
 }
 return {subchecks,scope:'all eight presence masks, all active presets and four legacy forms'};
});

await check('only genuinely missing defaults are cloned and remain isolated', async () => {
 const {module:m}=await fresh();const presets=['soft','balanced','aggressive'];
 const original=globalThis.structuredClone;let tableClones=0,curveClones=0,expectedClones=0;
 globalThis.structuredClone=(value,...args)=>{
  if(value===m.DEFAULT_FAN_PRESET_CURVES)tableClones++;
  if(presets.some(p=>value===m.DEFAULT_FAN_PRESET_CURVES[p]))curveClones++;
  return original(value,...args);
 };
 const outputs=[];
 try{for(let mask=0;mask<8;mask++)for(const active of presets)for(const legacy of [undefined,nodes(15)]){
  const input=Object.fromEntries(presets.map((key,i)=>[key,(mask&(1<<i))?nodes(10):null]));
  const actual=m.normalizeFanPresetCurves(input,legacy,active);outputs.push(actual);
  for(const [i,key] of presets.entries())if(!(mask&(1<<i))&&!(key===active&&legacy))expectedClones++;
 }}finally{globalThis.structuredClone=original;}
 assert.equal(tableClones,0,'still clones the full factory table before resolving fallbacks');
 assert.equal(curveClones,expectedClones,'default clone count does not match actual fallback need');
 const baseline=structuredClone(m.DEFAULT_FAN_PRESET_CURVES);
 for(const output of outputs)for(const key of presets)output[key][0].dutyPercent=99;
 assert.deepEqual(m.DEFAULT_FAN_PRESET_CURVES,baseline);
 return {normalizations:outputs.length,factoryTableClones:tableClones,factoryCurveClones:curveClones,expectedClones};
});

await check('valid active table avoids touching unused legacy values and fallback shapes remain compatible', async () => {
 const {module:m}=await fresh();const legacy=nodes(15);Object.defineProperty(legacy[0],'tempC',{get(){throw Error('UNUSED_LEGACY_READ');}});
 assert.deepEqual(m.normalizeFanPresetCurves({soft:nodes(5)},legacy,'soft').soft,nodes(5));
 assert.throws(()=>m.normalizeFanPresetCurves({},legacy,'soft'),/UNUSED_LEGACY_READ/);
 for(const table of [undefined,null,[],0,'invalid']){
  const actual=m.normalizeFanPresetCurves(table,nodes(15),'aggressive');
  assert.deepEqual(actual.soft,m.DEFAULT_FAN_PRESET_CURVES.soft);
  assert.deepEqual(actual.balanced,m.DEFAULT_FAN_PRESET_CURVES.balanced);
  assert.deepEqual(actual.aggressive,nodes(15));
 }
 return {legacyReadSkipped:true,invalidTableForms:5};
});

const report={ok:rows.length===19&&rows.every(r=>r.ok),checks:rows.length,rows,hostCurveCases,curveReadWork,featureOverrideLoads,curveOverrideLoads,curveSha256:createHash('sha256').update(fs.readFileSync(curveOverride||path.join(root,'src/bridge/fanCurve.ts'))).digest('hex'),sourceSha256:createHash('sha256').update(fs.readFileSync(override||featurePath)).digest('hex'),bundleSha256:createHash('sha256').update(result.outputFiles[0].contents).digest('hex'),hardwareCalls:0,scope:'actual fanFeature and Vue; inert settings/diagnostics/topmon/API; snapshot isolation and operation counts, no CPU performance comparison or device I/O'};
fs.writeFileSync(path.join(evidence,'result.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({ok:report.ok,checks:report.checks,failed:rows.filter(r=>!r.ok).map(r=>r.name),hardwareCalls:0,evidence}));if(!report.ok)process.exitCode=1;
