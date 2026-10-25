// Compile the actual SFC script and exercise real Vue lifecycle/watchers using
// a headless Vue renderer. Bridge subscriptions/storage/timers are inert ports.
// The render function is intentionally empty: this checks lifecycle, not pixels.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {createRequire} from 'node:module';import {pathToFileURL} from 'node:url';
const root=process.cwd(),out=path.resolve(process.env.YMCC_MONITOR_PAGE_DEMAND_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/upstream-entry-preview-demand-20261016');fs.mkdirSync(out,{recursive:true});const req=createRequire(root+'/package.json'),{build}=req('esbuild'),{parse,compileScript}=req('vue/compiler-sfc');
const sourcePath=path.resolve('src/components/MonitorOutputSettings.vue'),checks=[],work={},io=globalThis.__monitorPageDemand={};const check=(name,fn)=>{fn();checks.push(name)};
const oldWindow=globalThis.window;
try{
 for(const side of ['before','after']){
  const f=io[side]={snapshots:new Set(),tdps:new Set(),logos:new Set(),timers:new Map(),timerId:0,projections:0,formats:0,view:null,prefs:null,reads:0,logoAcquires:0,now:Date.now()};const ref='globalThis.__monitorPageDemand.'+side;
  f.snapshot={source:'snapshot',session:1,generation:1,seq:1,collectedAt:f.now,hwDown:false,top:{ts:f.now,hwDown:false,tdpW:25,tempC:60,cpuUsage:20},fps:null};
  const mocks={
   '@/bridge/monitorData':`export function subscribeMonitorSnapshot(fn){${ref}.snapshots.add(fn);fn(${ref}.snapshot);return()=>${ref}.snapshots.delete(fn)}export function subscribeMonitorTdpState(fn){${ref}.tdps.add(fn);fn(null);return()=>${ref}.tdps.delete(fn)}`,
   '@/bridge/monitorGameLogo':`export function subscribeMonitorGameLogo(fn){${ref}.logoAcquires++;${ref}.logos.add(fn);fn('');return()=>${ref}.logos.delete(fn)}`,
   '@/bridge/monitorHudUnderlayImport':`export async function prepareHudUnderlayFile(){throw Error('No image import')}`,
   '@/bridge/monitorHudUnderlayPicker':`export async function pickNativeHudUnderlayFile(){throw Error('No image picker')}`,
   '@/bridge/ipc':`export const isNativeRuntime=false;`,
   '@/bridge/monitorOutput':`export async function readMonitorOutputPreferences(){${ref}.reads++;return structuredClone(${ref}.prefs)}export async function saveMonitorOutputPreferences(){}export async function readMonitorOutputRuntime(){return {applied:'ymcc',backend:true,ymccAvailable:true,rtssScale:100}}export const monitorOutputCoordinator={async apply(){return {ok:true,applied:'ymcc'}}};export function onMonitorHudAvailability(fn){fn(true);return()=>{}}`,
   '@/bridge/yeman':`export async function setRtssZoom(){throw Error('No RTSS write')}`,
  };
  const b=await build({stdin:{resolveDir:root,loader:'ts',contents:`export {default as Settings} from './src/components/MonitorOutputSettings.vue';export {normalizeMonitorOutput,HUD_METRICS} from './src/bridge/monitorHudModel';export {createRenderer,ref,h,KeepAlive,nextTick} from 'vue';`},bundle:true,platform:'node',format:'esm',write:false,define:{'process.env.NODE_ENV':'"production"'},plugins:[{name:'isolated-settings-lifecycle',setup(b){
   b.onResolve({filter:/.*/},a=>{if(mocks[a.path])return{path:a.path,namespace:'fake'};if(a.path.startsWith('@/'))return{path:path.resolve('src',a.path.slice(2))+(!path.extname(a.path)?'.ts':'')};if(a.path.endsWith('.vue')&&path.basename(a.path)!=='MonitorOutputSettings.vue')return{path:a.path,namespace:'component'};});
   b.onLoad({filter:/.*/,namespace:'fake'},a=>({contents:mocks[a.path],loader:'ts'}));
   b.onLoad({filter:/.*/,namespace:'component'},()=>({contents:'export default {}',loader:'ts'}));
   b.onLoad({filter:/MonitorOutputSettings\.vue$/},a=>{const text=fs.readFileSync(side==='before'?out+'/before/src/components/MonitorOutputSettings.vue':a.path,'utf8');const {descriptor}=parse(text.replace('</script>','\ndefineExpose({prefs,previewReadings});\n</script>'),{filename:a.path});const script=compileScript(descriptor,{id:'monitor-page-demand',inlineTemplate:false});return{contents:script.content.replace('export default','const component=')+'\ncomponent.render=()=>null;export default component;',loader:'ts',resolveDir:path.dirname(a.path)}});
   b.onLoad({filter:/monitorHudModel\.ts$/},a=>{let text=fs.readFileSync(a.path,'utf8');const anchor='includeClock = true): HudData {';assert.equal(text.split(anchor).length,2);assert.equal(text.split('clockFormatter.format(now)').length,2);text=text.replace(anchor,anchor+' '+ref+'.projections++;').replace('clockFormatter.format(now)','('+ref+'.formats++,clockFormatter.format(now))');return{contents:text,loader:'ts',resolveDir:path.dirname(a.path)}});
  }}]});
  const file=out+'/'+side+'-settings-lifecycle-under-test.mjs';fs.writeFileSync(file,b.outputFiles[0].text);const m=await import(pathToFileURL(file));f.prefs=m.normalizeMonitorOutput({enabled:true,output:'ymcc',ymcc:{underlay:'none',fields:Object.fromEntries(m.HUD_METRICS.map(v=>[v.id,v.id==='cpuTemp']))}});
  globalThis.window={setTimeout(fn,ms){const id=++f.timerId;f.timers.set(id,{fn,ms});return id},clearTimeout:id=>f.timers.delete(id)};
  const makeNode=(type,text='')=>({type,text,children:[],parent:null});
  const renderer=m.createRenderer({insert(node,parent,anchor){if(node.parent){const i=node.parent.children.indexOf(node);if(i>=0)node.parent.children.splice(i,1)}const i=anchor?parent.children.indexOf(anchor):-1;if(i<0)parent.children.push(node);else parent.children.splice(i,0,node);node.parent=parent},remove(node){if(node.parent){const i=node.parent.children.indexOf(node);if(i>=0)node.parent.children.splice(i,1);node.parent=null}},createElement:makeNode,createText:text=>makeNode('text',text),createComment:text=>makeNode('comment',text),setText:(n,t)=>{n.text=t},setElementText:(n,t)=>{n.text=t},parentNode:n=>n.parent,nextSibling:n=>{const c=n.parent?.children||[];return c[c.indexOf(n)+1]||null},patchProp(){}});
  const route=m.ref(true),gallery=m.ref(false),vmref=m.ref(null);const app=renderer.createApp({render:()=>m.h(m.KeepAlive,null,{default:()=>route.value?m.h(m.Settings,{key:gallery.value?'gallery':'live',previewOnly:gallery.value,ref:vmref}):m.h('div')})});
  const settle=async()=>{for(let i=0;i<20;i++){await Promise.resolve();await m.nextTick()}};
  app.mount(makeNode('root'));await settle();const view=()=>vmref.value;
  check(side+': current monitor page binds the shared snapshot and TDP once while visible',()=>{assert.equal(f.snapshots.size,1);assert.equal(f.tdps.size,1)});
  f.projections=f.formats=0;for(let i=0;i<50;i++){f.snapshot={...f.snapshot,seq:i+2,top:{...f.snapshot.top,tdpW:20+i/10}};for(const fn of f.snapshots)fn(f.snapshot)}await settle();work[side]={visible50:{projections:f.projections,formats:f.formats}};
  check(side+': unselected clock formatting behavior is measured through actual projection calls',()=>{assert.equal(f.projections,50);assert.equal(f.formats,side==='before'?50:0);assert.equal(view().previewReadings.values.cpuPower,24.9)});
  view().prefs.enabled=false;await settle();const atDisable={projections:f.projections,formats:f.formats};
  for(let i=0;i<100;i++){f.snapshot={...f.snapshot,seq:i+52};for(const fn of f.snapshots)fn(f.snapshot)}await settle();
  work[side].disabled100={projections:f.projections-atDisable.projections,formats:f.formats-atDisable.formats,snapshotSubscriptions:f.snapshots.size,tdpSubscriptions:f.tdps.size,timers:f.timers.size};
  check(side+': closing the master switch must stop hidden preview work in the current page',()=>{if(side==='before'){assert.equal(work[side].disabled100.projections,100);assert.equal(f.snapshots.size,1)}else{assert.equal(work[side].disabled100.projections,0);assert.equal(f.snapshots.size,0);assert.equal(f.tdps.size,0);assert.equal(f.timers.size,0)}});
  view().prefs.enabled=true;await settle();check(side+': reenabling maintains exactly one subscription to each unified display source',()=>{assert.equal(f.snapshots.size,1);assert.equal(f.tdps.size,1)});
  if(side==='after'){
   const formats=f.formats,projections=f.projections;view().prefs.ymcc.fields.clock=true;await settle();
   check('enabling clock projects it immediately through the existing source binding',()=>{assert.equal(f.formats,formats+1);assert.equal(f.projections,projections+1);assert.equal(typeof view().previewReadings.values.clock,'string');assert.equal(f.snapshots.size,1);assert.equal(f.tdps.size,1)});
   view().prefs.ymcc.fields.clock=false;await settle();check('disabling clock clears its cached text without adding a read, timer or format',()=>{assert.equal(f.formats,formats+1);assert.equal(view().previewReadings.values.clock,null)});
  }
  route.value=false;await settle();check(side+': KeepAlive deactivation removes passive subscriptions and age timers',()=>{assert.equal(f.snapshots.size,0);assert.equal(f.tdps.size,0);assert.equal(f.timers.size,0)});
  route.value=true;await settle();check(side+': route reactivation creates one current display binding',()=>{assert.equal(f.snapshots.size,1);assert.equal(f.tdps.size,1)});
  if(side==='after'){
   view().prefs.output='rtss';await settle();check('switching to RTSS keeps no custom preview projection binding',()=>{assert.equal(f.snapshots.size,0);assert.equal(f.tdps.size,0);assert.equal(f.timers.size,0)});
   gallery.value=true;await settle();view().prefs.enabled=true;view().prefs.output='ymcc';view().prefs.ymcc.underlay='game-logo';await settle();
   check('browser gallery mode cannot subscribe to live hardware, TDP or current-game LOGO',()=>{assert.equal(f.snapshots.size,0);assert.equal(f.tdps.size,0);assert.equal(f.logos.size,0);assert.equal(f.logoAcquires,0)});
  }
  app.unmount();await settle();check(side+': final unmount leaves no display, LOGO or age resources',()=>{assert.equal(f.snapshots.size,0);assert.equal(f.tdps.size,0);assert.equal(f.logos.size,0);assert.equal(f.timers.size,0)});
 }
 const result={ok:true,checks:checks.length,cases:checks,work,realVueLifecycle:true,headlessRenderer:true,pixelAcceptance:false,hardwareReads:false,cpuPercentageMeasured:false,sourceSha256:Object.fromEntries(['src/components/MonitorOutputSettings.vue','src/bridge/monitorHudModel.ts','src/bridge/monitorHudFreshness.ts','src/bridge/monitorLiveEdits.ts'].map(p=>[p,crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex')])),scope:'Actual SFC setup and real Vue watchers/lifecycle with projection/format operation counters. Inert subscriptions, settings and timers; no performance/CPU percentage benchmark or visual acceptance.'};fs.writeFileSync(out+'/SETTINGS-LIFECYCLE-RESULT.json',JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({ok:true,checks:checks.length,work},null,2));
}finally{if(oldWindow===undefined)delete globalThis.window;else globalThis.window=oldWindow;delete globalThis.__monitorPageDemand}
