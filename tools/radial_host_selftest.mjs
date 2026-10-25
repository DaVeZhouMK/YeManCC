import assert from 'node:assert/strict';
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {createRequire} from 'node:module';
import {beginRadialHostTools,radialHostInputPlugin,completeRadialHostCompile,verifyRadialHostTools} from './ymcc_sim/global/radial-host-tool-scope/index.mjs';
const root=path.resolve(import.meta.dirname,'..'),require=createRequire(path.join(root,'package.json'));
let toolScope;try{toolScope=beginRadialHostTools(root,process.argv[2]);}catch(error){const report={pass:false,status:'BLOCKED',reason:'RADIAL_HOST_TOOL_AUTHORITY_UNAVAILABLE',error:error.message,checks:0,hardwareWrites:0,installedApplicationMutated:false};if(process.argv[2])fs.writeFileSync(process.argv[2],JSON.stringify(report,null,2));console.error(JSON.stringify(report));process.exit(7);}
const {build}=require(toolScope.authority.compilerEntry);
const development=process.argv.includes('--exercise-development');
const state={handlers:new Map(),calls:[],revision:0,generation:1,power:1,ready:true,capable:true,ack:false,writes:[],results:[],created:[],closed:[],windows:[],model:null,delay:null};
globalThis.__radialHostFixture=state;globalThis.window=new EventTarget();
// The real preference/normalization/testing contract uses only this process's repository and storage terminals.
const originalStorage=globalThis.localStorage,storage=new Map();globalThis.localStorage={getItem:key=>storage.get(key)??null,setItem:(key,value)=>storage.set(key,String(value)),removeItem:key=>storage.delete(key)};
state.sections={ui:{}};state.testingAck=true;state.testingRequests=[];
const sourceHashes=toolScope.sourceHashes,hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
let checks=0,phase='default-admission',finalReport;
process.on('uncaughtExceptionMonitor',error=>{const output=process.argv[2];if(output)fs.writeFileSync(output,JSON.stringify({pass:false,checks,phase,error:error.stack,sourceHashes,toolScope:toolScope.report,hardwareWrites:0,installedApplicationMutated:false},null,2));});
state.utility={revision:0,monitor:{ymccTemplate:'A',enabled:false,output:'ymcc',ymccTemplateRevision:10,ymcc:{scale:1.2},ymccTemplates:{A:{scale:1.2},B:{scale:.7}}},steam:{known:true,running:true,mode:'desktop',pid:123,birth:'456',reason:''},calls:[],launchPending:false};
state.input={revision:0,raw:{revision:1,applyStatus:'saved-pending',outputTarget:{persona:'disabled',buttonMappingEnabled:false,gyroEnabled:false},gyroMotion:{enabled:false,preset:'fps',virtualPadLink:true}},calls:[]};
state.fan={preset:'balanced',active:false,ready:false,hasIntent:false,intent:0,cancel:0,display:null,source:{featureEnabled:true,preset:'balanced',nodes:[{tempC:40,dutyPercent:30}],presetCurves:{soft:[{tempC:40,dutyPercent:20}],balanced:[{tempC:40,dutyPercent:30}],aggressive:[{tempC:40,dutyPercent:50}]}},calls:[],revision:0};
const modules={
 'radial-utilities-host':`import {createRadialUtilityActions} from '@/radial/utilityActions';import {normalizeMonitorOutput,switchMonitorHudTemplate} from '@/bridge/monitorHudModel';const s=globalThis.__radialHostFixture,f=s.utility;export const createLocalRadialUtilityActions=alive=>{const a=createRadialUtilityActions({alive,power:async()=>({generation:s.power,phase:s.ready?'ready':'suspending',inputReady:s.ready}),now:()=>1000,nextRevision:()=>String(++f.revision),monitor:{read:async()=>normalizeMonitorOutput(f.monitor),busy:()=>false,save:async(template,revision,admit)=>{if(!admit(normalizeMonitorOutput(f.monitor)))throw Error('SOURCE_CHANGED');f.monitor=switchMonitorHudTemplate(normalizeMonitorOutput(f.monitor),template,revision);f.calls.push('template:'+template)}},steam:{read:async()=>structuredClone(f.steam),launchPending:()=>f.launchPending,launch:async admit=>{if(!admit())throw Error('CANCELED');f.calls.push('launch');f.launchPending=true;},switchMode:async state=>{f.calls.push('bigpicture:'+state.pid);f.steam.mode='bigpicture'},shutdown:async state=>{f.calls.push('stop:'+state.pid)}}});return {...a,observe:()=>()=>{}}};`,
 'radial-input-host':`import {createRadialInputActions} from '@/radial/inputActions';import {globalMirrorRead} from '@/bridge/deckyGlobalActions';const s=globalThis.__radialHostFixture,f=s.input;export const createLocalRadialInputActions=alive=>{const readGlobal=async()=>globalMirrorRead({},f.raw,null);const actions=createRadialInputActions({read:async()=>({scope:'global',global:await readGlobal(),policy:null,locked:false,busy:false}),
 global:{allowed:alive,game:()=>null,locked:()=>false,targetPolicy:()=>null,side:()=>null,read:readGlobal,power:async()=>({generation:s.power,phase:s.ready?'ready':'suspending',hardwareWritesAllowed:s.ready}),acquire:()=>()=>{},cas:async(revision,patch,admit)=>{if(revision!==f.raw.revision||admit&&!admit())return {ok:false,value:f.raw};for(const [key,value]of Object.entries(patch))f.raw[key]={...f.raw[key],...value};f.raw.revision++;f.calls.push(patch);return {ok:true,value:f.raw}},closeJoyxoff:async()=>true,clearShortcut:async()=>true},
 game:{featureAllowed:alive,game:()=>null},nextRevision:()=>String(++f.revision)});return {...actions,observe:()=>()=>{}}};`,
 'radial-fan-host':`import {createRadialFanActions} from '@/radial/fanActions';const s=globalThis.__radialHostFixture,f=s.fan;export const createLocalRadialFanActions=alive=>createRadialFanActions({
  owner:{get state(){return f.ready?'ready':'idle'},get controlReady(){return f.ready},get hasControlIntent(){return f.hasIntent},get controlIntentRevision(){return f.intent},get controlCancellationRevision(){return f.cancel},recoveryActive:false,
    stagePendingControlCurve(){f.intent++},async start(){f.calls.push('start');f.hasIntent=true;return {allowed:true,writeReady:true}},async apply(){f.calls.push('apply');f.ready=f.active=true;return {}},async applyPreset(preset){f.calls.push('preset:'+preset);f.ready=f.active=true;return {}},async disable(){f.calls.push('disable');f.cancel++;f.intent++;f.hasIntent=f.ready=f.active=false;return {}}},
  enabled:()=>f.active,currentPreset:()=>f.preset,read:async()=>structuredClone(f.source),curves:(source,preset)=>source.presetCurves[preset],save:async(preset,expected)=>{if(JSON.stringify(expected)!==JSON.stringify(f.source))throw Error('SOURCE_CHANGED');f.preset=preset;f.source={...f.source,preset,nodes:structuredClone(f.source.presetCurves[preset])};f.calls.push('save:'+preset)},
  updateActive:()=>{},updateDuty:()=>{},power:async()=>({generation:s.power}),featureAllowed:alive,uiBusy:()=>false,currentDisplay:()=>f.display,display:value=>f.display=value,nextRevision:()=>String(++f.revision)});`,
 '@/bridge/deckyFanDisplay':`export const onFanMirrorDisplay=()=>()=>{};export const onFanMirrorUiGate=()=>()=>{};`,
 '@/bridge/ipc':`const s=globalThis.__radialHostFixture;export const isNativeRuntime=true;export function on(k,cb){if(!s.handlers.has(k))s.handlers.set(k,new Set());s.handlers.get(k).add(cb);return()=>s.handlers.get(k).delete(cb)}export const invoke=(cmd,a={})=>s.invoke(cmd,a);`,
 '@/bridge/api':`const s=globalThis.__radialHostFixture;export const powerLifecycle={get:async()=>({phase:s.ready?'ready':'suspending',generation:s.power,inputReady:s.ready})};export const shell={execute:async(...a)=>{s.windows.push(a);return true},open:async(...a)=>{s.windows.push(a);return true},taskView:async()=>{s.windows.push('taskView');return true}};export const windowApi={show:async()=>{s.windows.push('ymcc');return true}};`,
 'radial-preference-repository':`const s=globalThis.__radialHostFixture;export const getSettingsGeneration=()=>s.generation;export const assertSettingsGeneration=value=>{if(value!==s.generation)throw Error('settings generation changed')};export const readSettingsSection=async key=>structuredClone(s.sections[key]);export const saveSettingsSection=async(key,patch,generation=s.generation)=>{assertSettingsGeneration(generation);s.sections[key]={...s.sections[key],...structuredClone(patch)}};`,
 '@/bridge/settingsRepository':`export const getSettingsGeneration=()=>globalThis.__radialHostFixture.generation;`,
 '@/bridge/yeman':`export const getTdpExecutionCapability=async()=>({ready:globalThis.__radialHostFixture.capable,minPL1:5,maxPL1:30});`,
 '@/bridge/performanceSchedule':`const s=globalThis.__radialHostFixture;export const getContinuousPerformanceSnapshot=async()=>s.snapshot();export const updateContinuousPerformanceControls=async(identity,patch,admit)=>{if(admit&&!admit())throw Error('fixture scheduler admission refused');s.writes.push({identity,patch});s.bank=s.patch(s.bank,patch,'ac');s.bankRevision++;return {...s.snapshot(),tdpWriteReceipt:{requested:s.bank.tdp,accepted:s.ack?s.bank.tdp:null,ok:s.ack,proof:s.ack?'write-ack':'unconfirmed'}};};`,
 'radial-settings':`export const loadRadialSettings=async()=>structuredClone(globalThis.__radialHostFixture.model);`,
};
const compiled=await build({stdin:{contents:`export {startRadialHost} from './src/radial/host';export {defaultRadialSettings} from './src/radial/model';export {isRadialTestingEnabled} from './src/radial/testingState';export {RADIAL_USER_AVAILABLE} from './src/radial/availability';export {setRadialTestingPreference,syncRadialTestingPreference} from './src/radial/testing';export {loadUiSettings,getUiSetting} from './src/bridge/uiSettings';export {defaultContinuousBank,patchContinuousBank} from './src/bridge/performanceControls';`,resolveDir:root,loader:'ts'},bundle:true,write:false,metafile:true,platform:'node',format:'cjs',alias:{'@':path.join(root,'src')},plugins:[radialHostInputPlugin(toolScope),{name:'radial-memory-contract',setup(b){b.onResolve({filter:/.*/},a=>{const owner=a.importer.replaceAll('\\','/').endsWith('/radial/host.ts');const key=a.importer.replaceAll('\\','/').endsWith('/bridge/uiSettings.ts')&&a.path==='./settingsRepository'?'radial-preference-repository':owner&&a.path==='./settings'?'radial-settings':owner&&a.path==='./fanHost'?'radial-fan-host':owner&&a.path==='./inputHost'?'radial-input-host':owner&&a.path==='./utilitiesHost'?'radial-utilities-host':a.path;if(key in modules)return{path:key,namespace:'fixture'};});b.onLoad({filter:/.*/,namespace:'fixture'},a=>({contents:modules[a.path],loader:'ts'}));}}]});
completeRadialHostCompile(toolScope,compiled,Object.keys(modules));
const module={exports:{}};new Function('require','module','exports',compiled.outputFiles[0].text)(require,module,module.exports);const {startRadialHost,defaultRadialSettings,RADIAL_USER_AVAILABLE,isRadialTestingEnabled,setRadialTestingPreference,syncRadialTestingPreference,loadUiSettings,getUiSetting,defaultContinuousBank,patchContinuousBank}=module.exports;

for(const input of ['tools/radial_host_selftest.mjs','src/radial/availability.ts','src/radial/testingState.ts','src/radial/testing.ts','src/bridge/uiSettings.ts'])sourceHashes[input]=hash(path.join(root,input));
await loadUiSettings();assert.equal(RADIAL_USER_AVAILABLE,true,'Actual release availability remains true');assert.equal(getUiSetting('radialTestingEnabled'),false,'Actual UI preference default remains closed');
assert.equal(isRadialTestingEnabled(),false,"Every host starts behind closed runtime admission");
// Admission is exercised below through the real public contract, never a ref assignment.
state.bank=defaultContinuousBank('ac');state.bank.tdp=15;state.bank.tdpCeiling=25;state.patch=patchContinuousBank;state.bankRevision=0;state.snapshot=()=>({identity:{scope:'global',followEffective:true,side:'ac',actualSide:'ac',exe:'',pid:0,processCreated:'',generation:1,revision:'captured-'+state.bankRevision},side:'ac',bank:structuredClone(state.bank),controls:{version:1,ac:structuredClone(state.bank),dc:structuredClone(state.bank)},frameRates:{ac:{mode:'off'},dc:{mode:'off'}},hardwareMinimum:5,hardwareMaximum:30,hybrid:true,smtAvailable:false,hyperThreadMode:'default',configured:true});
state.model=defaultRadialSettings();
// Retain explicit legacy direct actions while checking the new fan group separately.
state.model.pages[0].items[6]={id:'tdp',label:'TDP',action:'tdp',enabled:true};state.model.pages[0].items[7]={id:'browserDirect',label:'浏览器',action:'windows.browser',enabled:true};state.model.pages[0].items[3]={id:'fanControls',label:'风扇',action:'fan',enabled:true};
state.model.pages[0].items[4]={id:'settings',label:'设置',action:'page.settings',enabled:true};
const inputPage=defaultRadialSettings().pages[0];inputPage.id='input-test';inputPage.items.forEach(item=>{if(!item.action.startsWith('input.'))item.id='input-fixture-'+item.id;item.children?.forEach(child=>child.id='input-fixture-'+child.id)});state.model.pages.push(inputPage);state.model.pages[0].items[2]={id:'monitor',label:'监控',action:'page.monitor',enabled:true};
const utilityPage=defaultRadialSettings().pages[0];utilityPage.id='utility-test';utilityPage.items.forEach(item=>{item.id='utility-'+item.id;item.children?.forEach(child=>child.id='utility-'+child.id)});state.model.pages.push(utilityPage);
state.invoke=async(cmd,args)=>{if(cmd==='radial.testing.configure'){state.testingRequests.push(structuredClone(args));return {ok:!args.enabled||state.testingAck,enabled:!!args.enabled&&state.testingAck,reason:'fixture native admission refused'};}state.calls.push({cmd,args});if(cmd==='radial.create'){const receipt={id:state.created.length+1,nonce:args.nonce};state.created.push({receipt,args});if(state.delay)await state.delay.promise;return receipt;}if(cmd==='radial.result'){state.results.push(args);return true;}if(cmd==='radial.close'){state.closed.push(args);return true;}return true;};
const emit=(event,data)=>{if(event==='radial.summon-request')data={issuedAt:Date.now(),expiresAt:Date.now()+4000,powerGeneration:state.power,...data};for(const cb of state.handlers.get(event)||[])cb(data);};
async function until(fn){for(let i=0;i<100;i++){if(fn())return;await new Promise(r=>setTimeout(r,2));}throw Error('Radial owner did not settle');}
let host=startRadialHost({push:async route=>state.windows.push(route)});
const check=(v)=>{assert.ok(v);checks++;};
{
 for (const source of ['monitor','shortcut','fullscreen','fullscreen-raw','settings']) emit('radial.summon-request',{source});
 window.dispatchEvent(new Event('ymcc:radial-settings-changed'));
 await new Promise(resolve=>setTimeout(resolve,5));
 check(state.handlers.size===0);check(state.calls.length===0);check(state.created.length===0);
 check(state.windows.length===0);check(state.writes.length===0);host.stop();host.stop();check(state.closed.length===0);
}
const defaultGateChecks=checks;
phase='public-contract-refusal';
state.testingAck=false;await assert.rejects(setRadialTestingPreference(true),/fixture native admission refused/);check(!isRadialTestingEnabled());check(state.handlers.size===0&&state.calls.length===0&&state.created.length===0);
await setRadialTestingPreference(false);check(!isRadialTestingEnabled());
phase='public-contract-admission';
state.testingAck=true;await setRadialTestingPreference(true);check(isRadialTestingEnabled()&&getUiSetting('radialTestingEnabled'));check(state.testingRequests.some(value=>value.enabled===true));
// Explicit menu data is local to the original memory settings terminal.
state.model.enabled=true;
host=startRadialHost({push:async route=>state.windows.push(route)});
phase='original-host-business-scenarios';
{
async function summon(source='shortcut'){const count=state.created.length;emit('radial.summon-request',{source,x:42,y:84});await until(()=>state.created.length>count&&state.calls.some(c=>c.cmd==='radial.frame'&&c.args.wheelId===state.created.at(-1).receipt.id));const r=state.created.at(-1).receipt;return{wheelId:r.id,nonce:r.nonce};}
emit('radial.summon-request',{source:'shortcut',issuedAt:Date.now()-7000,expiresAt:Date.now()-1000});await new Promise(r=>setTimeout(r,5));check(state.created.length===0);
emit('radial.summon-request',{source:'shortcut',powerGeneration:0});await new Promise(r=>setTimeout(r,5));check(state.created.length===0);
let owner=await summon();check(state.created.at(-1).args.source==='shortcut');
let request=0;
async function intent(itemId,extra={}){const before=state.results.length,id='request-'+(++request),now=Date.now();const body={requestId:id,itemId,revision:state.model.revision,issuedAt:now,expiresAt:now+5000,...extra};emit('radial.intent',{...owner,intent:body});await until(()=>state.results.length>before);return {outcome:state.results.at(-1).result,body};}
try{
 const activeBefore=state.closed.length,createdBefore=state.created.length;
 for(const source of ['monitor','fullscreen','fullscreen-raw'])emit('radial.summon-request',{source});
 await new Promise(r=>setTimeout(r,10));check(state.closed.length===activeBefore&&state.created.length===createdBefore);
 emit('radial.summon-request',{source:'shortcut'});await until(()=>state.closed.length>activeBefore);check(state.closed.at(-1).wheelId===owner.wheelId);owner=await summon();
 let result=await intent('tdp',{value:16});check(!result.outcome.ok&&result.outcome.proof==='unconfirmed');check(state.writes.length===1);check(state.writes[0].identity.revision==='captured-0');
 emit('radial.intent',{...owner,intent:result.body});await new Promise(r=>setTimeout(r,5));check(state.writes.length===1);
 state.ack=true;result=await intent('tdp',{value:16});check(result.outcome.ok&&result.outcome.proof==='write-ack');check(state.writes.length===2);
 result=await intent('tdp',{value:100});check(!result.outcome.ok&&state.writes.length===2);
 result=await intent('tdp',{value:16,issuedAt:Date.now()-8000,expiresAt:Date.now()-3000});check(!result.outcome.ok&&state.writes.length===2);
 state.capable=false;result=await intent('tdp',{value:16});check(!result.outcome.ok&&state.writes.length===2);state.capable=true;
 state.model.revision++;result=await intent('browserDirect');check(!result.outcome.ok&&state.windows.length===0);state.model.revision--;
 emit('radial.intent',{...owner,nonce:'stale-session',intent:{requestId:'bad'}});check(state.windows.length===0);
 const count=state.closed.length;emit('power.suspending',{});await until(()=>state.closed.length>count);check(state.closed.at(-1).wheelId===owner.wheelId);
 owner=await summon('fullscreen');check(state.created.at(-1).args.source==='fullscreen'&&state.created.at(-1).args.x===42);
 emit('power.suspending',{});await until(()=>state.closed.some(c=>c.wheelId===owner.wheelId));
 owner=await summon('fullscreen-raw');check(state.created.at(-1).args.source==='fullscreen-raw');
 result=await intent('thisPc');check(result.outcome.ok);await until(()=>state.windows.length===1);assert.deepEqual(state.windows[0],['explorer.exe',['shell:MyComputerFolder']]);checks++;
 owner=await summon();result=await intent('ymcc');check(result.outcome.ok);await until(()=>state.windows.includes('ymcc'));
 owner=await summon();result=await intent('settings');check(result.outcome.ok);await until(()=>state.windows.includes('/settings'));
 owner=await summon();
 const performanceFrame=()=>state.results.filter(r=>r.wheelId===owner.wheelId&&r.result.performance).at(-1)?.result.performance??state.calls.filter(c=>c.cmd==='radial.frame'&&c.args.wheelId===owner.wheelId).at(-1)?.args.frame.performance;
 const beforeControls=state.writes.length;
 result=await intent('pages',{control:'tdp',value:16,controlRevision:'stale'});check(!result.outcome.ok&&state.writes.length===beforeControls);
 let source=performanceFrame();check(source.enabled&&source.controls.some(c=>c.key==='bias'));
 result=await intent('pages',{control:'mode',value:'manual',controlRevision:source.sourceRevision});check(result.outcome.ok&&result.outcome.saved&&!result.outcome.applied);check(result.outcome.performance.mode==='manual'&&state.writes.at(-1).patch.mode==='manual');
 source=performanceFrame();result=await intent('pages',{control:'frequencyMHz',value:2300,controlRevision:source.sourceRevision});check(result.outcome.ok&&state.writes.at(-1).patch.manual.frequencyMHz===2300);
 const confirmedCount=state.writes.length;emit('radial.intent',{...owner,intent:result.body});await new Promise(r=>setTimeout(r,5));check(state.writes.length===confirmedCount);
 source=performanceFrame();result=await intent('pages',{control:'coreMode',value:'bigOnly',controlRevision:source.sourceRevision});check(!result.outcome.ok&&state.writes.length===confirmedCount);
 const fanFrame=()=>state.results.filter(r=>r.wheelId===owner.wheelId&&r.result.fan).at(-1)?.result.fan??state.calls.filter(c=>c.cmd==='radial.frame'&&c.args.wheelId===owner.wheelId).at(-1)?.args.frame.fan;
 result=await intent('fanControls',{fanChoice:'soft',fanRevision:'stale'});check(!result.outcome.ok&&state.fan.calls.length===0);
 let fanSource=fanFrame();check(fanSource.available&&fanSource.choice==='off');
 result=await intent('fanControls',{fanChoice:'soft',fanRevision:fanSource.sourceRevision});check(result.outcome.ok&&result.outcome.saved&&result.outcome.pending&&!result.outcome.applied);await until(()=>state.fan.active);check(state.fan.calls.includes('save:soft')&&state.fan.calls.filter(c=>c==='start').length===1);
 const fanCalls=state.fan.calls.length;emit('radial.intent',{...owner,intent:result.body});await new Promise(r=>setTimeout(r,5));check(state.fan.calls.length===fanCalls);
 result=await intent('fanControls',{fanChoice:'invalid',fanRevision:fanFrame().sourceRevision});check(!result.outcome.ok&&state.fan.calls.length===fanCalls);
 state.fan.source.presetCurves.soft[0].dutyPercent++;result=await intent('fanControls',{fanChoice:'aggressive',fanRevision:fanFrame().sourceRevision});check(!result.outcome.ok&&state.fan.calls.length===fanCalls);
 result=await intent('fanControls',{fanChoice:'off',fanRevision:fanFrame().sourceRevision});check(result.outcome.ok&&!result.outcome.applied);await until(()=>state.fan.calls.includes('disable'));check(!state.fan.active);
 const inputFrame=()=>state.results.filter(r=>r.wheelId===owner.wheelId&&r.result.input).at(-1)?.result.input??state.calls.filter(c=>c.cmd==='radial.frame'&&c.args.wheelId===owner.wheelId).at(-1)?.args.frame.input;
 result=await intent('virtualPad',{inputField:'padPersona',inputValue:'elite',inputRevision:'stale'});check(!result.outcome.ok&&state.input.calls.length===0);
 let inputSource=inputFrame();check(inputSource.available&&inputSource.scope==='global');
 result=await intent('virtualPad',{inputField:'gyroEnabled',inputValue:'on',inputRevision:inputSource.sourceRevision});check(!result.outcome.ok&&state.input.calls.length===0);
 result=await intent('virtualPad',{inputField:'padPersona',inputValue:'elite',inputRevision:inputFrame().sourceRevision});check(result.outcome.ok&&result.outcome.saved&&result.outcome.pending&&!result.outcome.applied);check(state.input.raw.outputTarget.persona==='elite'&&state.input.raw.gyroMotion.enabled);
 const inputCalls=state.input.calls.length;emit('radial.intent',{...owner,intent:result.body});await new Promise(r=>setTimeout(r,5));check(state.input.calls.length===inputCalls);
 result=await intent('gyro',{inputField:'gyroPreset',inputValue:'racing',inputRevision:inputFrame().sourceRevision});check(result.outcome.ok&&state.input.raw.gyroMotion.preset==='racing');
 result=await intent('gyro',{inputField:'gyroEnabled',inputValue:'off',inputRevision:inputFrame().sourceRevision});check(result.outcome.ok&&!state.input.raw.gyroMotion.enabled);
 const afterOff=state.input.calls.length;result=await intent('gyro',{inputField:'gyroPreset',inputValue:'fps',inputRevision:inputFrame().sourceRevision});check(!result.outcome.ok&&state.input.calls.length===afterOff);
 result=await intent('virtualPad',{inputField:'padPersona',inputValue:'disabled',inputRevision:inputFrame().sourceRevision});check(result.outcome.ok&&!state.input.raw.outputTarget.buttonMappingEnabled);
 const utilityFrame=group=>state.results.filter(r=>r.wheelId===owner.wheelId&&r.result.utilities).at(-1)?.result.utilities[group]??state.calls.filter(c=>c.cmd==='radial.frame'&&c.args.wheelId===owner.wheelId).at(-1)?.args.frame.utilities[group];
 result=await intent('utility-monitor',{utilityChoice:'B',utilityRevision:'stale'});check(!result.outcome.ok&&state.utility.calls.length===0);
 result=await intent('utility-monitor',{utilityChoice:'B',utilityRevision:utilityFrame('monitor.templates').sourceRevision});check(result.outcome.ok&&result.outcome.saved&&!result.outcome.applied&&result.outcome.proof==='settings-commit');check(state.utility.monitor.ymccTemplate==='B'&&!state.utility.monitor.enabled);
 const utilityCount=state.utility.calls.length;emit('radial.intent',{...owner,intent:result.body});await new Promise(r=>setTimeout(r,5));check(state.utility.calls.length===utilityCount);
 result=await intent('utility-monitor',{utilityChoice:'stop',utilityRevision:utilityFrame('monitor.templates').sourceRevision});check(!result.outcome.ok&&state.utility.calls.length===utilityCount);
 result=await intent('utility-steam',{utilityChoice:'bigpicture',utilityRevision:utilityFrame('steam.controls').sourceRevision});check(result.outcome.ok&&result.outcome.applied&&state.utility.calls.includes('bigpicture:123'));
 result=await intent('utility-steam',{utilityChoice:'stop',utilityRevision:utilityFrame('steam.controls').sourceRevision});check(result.outcome.ok&&result.outcome.pending&&!result.outcome.applied&&state.utility.calls.includes('stop:123'));
 const old=owner;emit('power.sourceChanged',{});await until(()=>state.closed.some(c=>c.wheelId===old.wheelId));check(true);
 let release;state.delay={promise:new Promise(r=>release=r)};const before=state.created.length;emit('radial.summon-request',{source:'shortcut'});await until(()=>state.created.length>before);const late=state.created.at(-1).receipt;host.stop();release();await until(()=>state.closed.some(c=>c.wheelId===late.id&&c.nonce===late.nonce));check(true);
 check([...state.handlers.values()].every(v=>v.size===0));
 finalReport={pass:true,checks,defaultGateChecks,sourceHashes,toolScope:toolScope.report,gateContract:'Actual uiSettings -> setRadialTestingPreference -> syncRadialTestingPreference -> inert native receipt -> actual testingState; no environment bypass or direct ref write',actualConsumer:'src/radial/host.ts',performanceProvider:'memory fixture; real schedule writer separately covered by continuous_performance_selftest',hardwareWrites:0,installedApplicationMutated:false};
}finally{host.stop();await setRadialTestingPreference(false);assert.equal(isRadialTestingEnabled(),false,'Public contract restores default-closed admission');for(const [input,digest]of Object.entries(sourceHashes))assert.equal(hash(path.join(root,input)),digest,'Source changed: '+input);verifyRadialHostTools(toolScope);if(finalReport){finalReport.restoredAdmissionClosed=true;finalReport.testingRequests=state.testingRequests;const output=process.argv[2];if(output)fs.writeFileSync(output,JSON.stringify(finalReport,null,2));console.log(JSON.stringify({pass:true,checks,defaultGateChecks,restoredAdmissionClosed:true}));}globalThis.localStorage=originalStorage;delete globalThis.__radialHostFixture;}

}
