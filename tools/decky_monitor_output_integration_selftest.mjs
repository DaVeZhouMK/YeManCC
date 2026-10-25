import {task,validation,root} from './fixtures/decky_task_paths.mjs';
// Production monitor action/output coordinator/HUD host and current data model.
// Only native/window/settings/sensor IO boundaries are synthetic; no installed state is changed.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {pathToFileURL} from 'node:url';
const {build}=createRequire(path.join(root,'package.json'))('esbuild');
const f={settings:{rtss:{}},events:new Map(),calls:[],native:null,nextId:1,extension:null,rtss:true,layout:'Empty.ovl',demand:0,reads:0,steamShown:false,steamPresence:true,steamPosition:0,saves:[],failCreate:false,
  emit(name,args){for(const fn of this.events.get(name)??[])fn(args);}};
const now=Date.now(),runId='c'.repeat(32),peer={connection:'1',runId,connected:true};
const game={appId:480,gameId:'480',browserGameId:'480',overlayTitle:'SP Overlay: 2/3/4',overlayPid:2,overlayBrowserId:3,ownerBrowserId:5,overlayBrowserType:4,overlayNativeAppId:480};
f.snapshot={top:{ts:now,cpuUsage:32,tempC:54,coreUsage:[10,20,30,40],coreClocks:[4.1,4.2,4.3,4.4],cpuFreqMhz:4100,cpuVoltageV:1.15,hwDown:false,fileReadMBps:38.5,fileWriteMBps:1.7,fileSystemTs:now,fileSystemSource:'windows-filesystem-disk-activity',ramUsedGB:12,ramTotalGB:32},fps:{ts:now,fps:120,fps1:95,gpu:64},hwDown:false};
globalThis.__deckyMonitorOutputTest=f;globalThis.window={setTimeout,clearTimeout};
const mocks={
 'src/bridge/ipc.ts':`export const isNativeRuntime=true;export const invoke=(command,args)=>globalThis.__deckyMonitorOutputTest.invoke(command,args);export function on(name,fn){const f=globalThis.__deckyMonitorOutputTest;const set=f.events.get(name)||new Set();f.events.set(name,set);set.add(fn);return()=>set.delete(fn)}`,
 'src/bridge/settingsRepository.ts':`export async function readSettingsSection(name){return structuredClone(globalThis.__deckyMonitorOutputTest.settings[name]||{})}export async function saveSettingsSection(name,patch){const f=globalThis.__deckyMonitorOutputTest;f.saves.push({name,patch:structuredClone(patch)});const current=f.settings[name]||{};f.settings[name]={...current,...structuredClone(patch),...(patch.monitorOutput?{monitorOutput:{...current.monitorOutput,...structuredClone(patch.monitorOutput)}}:{})}}`,
 'src/bridge/yeman.ts':`const f=()=>globalThis.__deckyMonitorOutputTest;export async function rtssRunning(){return f().rtss}export async function toggleRtss(show){f().calls.push(['rtss-background',show]);if(!show)throw Error('Unexpected RTSS stop');f().rtss=show}export async function readOverlayLayout(){return f().layout}export async function setOverlayLayout(v){f().calls.push(['rtss-layout',v]);f().layout=v==='off'?'Empty.ovl':({W:'YeManOBS-W-1.ovl',L:'YeManOBS-L-1.ovl',J:'YeManOBS-JJ-1.ovl'})[v];return {mode:'live'}}export async function readRtssZoom(){return 5}`,
 'src/bridge/monitorData.ts':`const f=()=>globalThis.__deckyMonitorOutputTest;export function acquireMonitorDemand(){f().demand++;let closed=false;return()=>{if(!closed){closed=true;f().demand--}}}export async function readMonitorSnapshot(){f().reads++;return f().snapshot}export function subscribeMonitorSnapshot(fn){fn(f().snapshot);return()=>{}}export function subscribeMonitorTdpState(fn){fn(null);return()=>{}}export function acceptPublishedMonitorSnapshot(){}`,
 'src/bridge/deckyMirrorExtensions.ts':`export function registerDeckyMirrorExtension(ext){globalThis.__deckyMonitorOutputTest.extension=ext;return()=>{globalThis.__deckyMonitorOutputTest.extension=null}}`,
};
const entry=`export {startMonitorHudHost} from './src/bridge/monitorHudHost';export {createMonitorMirrorActions} from './src/bridge/deckyMonitorActions';export {normalizeMonitorOutput,HUD_LAYOUTS,HUD_METRICS} from './src/bridge/monitorHudModel';export {readMonitorOutputPreferences,saveMonitorOutputPreferences,monitorOutputCoordinator} from './src/bridge/monitorOutput';export {saveSettingsSection} from './src/bridge/settingsRepository';export {admitHudFrame} from './src/bridge/monitorHudProtocol';export {admitDesktopHudFrame} from './src/bridge/desktopHudProtocol';`;
const b=await build({stdin:{contents:entry,loader:'ts',resolveDir:root},bundle:true,platform:'node',format:'esm',write:false,plugins:[{name:'inert-native-boundaries',setup(build){build.onLoad({filter:/\.ts$/},args=>{const name=path.relative(root,args.path).replaceAll('\\','/');return mocks[name]?{contents:mocks[name],loader:'ts',resolveDir:path.dirname(args.path)}:undefined;});}}]});
const file=path.join(task,'Build/decky-monitor-output-under-test.mjs');fs.writeFileSync(file,b.outputFiles[0].contents);
const m=await import(pathToFileURL(file).href),cases=[];
const flush=async()=>{for(let i=0;i<24;i++)await new Promise(resolve=>setImmediate(resolve));};
const steamFrames=[],desktopFrames=[];
f.invoke=async(command,args={})=>{
 f.calls.push([command,args]);
 if(command==='desktopHud.status')return {available:true,id:f.native?.id??0,nonce:f.native?.nonce};
 if(command==='desktopHud.create'){if(f.failCreate)throw Error('Synthetic native create unavailable');assert.equal(args.scope,'global');assert.equal(args.pid,undefined);const value={id:f.nextId++,nonce:args.nonce};f.native=value;queueMicrotask(()=>f.emit('desktopHud.ready',value));return value;}
 if(command==='desktopHud.frame'){const admitted=m.admitDesktopHudFrame(args.frame,args.hudId,args.nonce,Date.now(),-1);assert.ok(admitted,'Current desktop monitor schema must be admitted');desktopFrames.push(admitted);queueMicrotask(()=>f.emit('desktopHud.painted',{id:args.hudId,nonce:args.nonce,seq:args.frame.seq}));return true;}
 if(command==='desktopHud.close'){const own=f.native;if(own){assert.equal(args.hudId,own.id);assert.equal(args.nonce,own.nonce);f.native=null;queueMicrotask(()=>f.emit('desktopHud.closed',own));}return true;}
 if(command==='monitor.hudPulse.set'||command==='monitor.hudPulse.ack')return true;
 if(command==='deckySidebar.get')return {steamKnown:true,steamRunning:f.steamPresence};
 if(command==='steam.settings.get')return {ok:true,available:true,pending:false,via:'live',liveConfirmed:true,applicationConfirmed:true,position:f.steamPosition,detail:4,scale:1,saturation:1,opacity:.86};
 if(command==='steam.settings.set'){f.steamPosition=args.monitor.position;return {ok:true,available:true,pending:false,via:'live',liveConfirmed:true,applicationConfirmed:true,...args.monitor};}
 if(command==='deckySidebar.mirrorReply'){
  const frame=args.message;if(frame.type==='hud'){assert.equal(args.connection,peer.connection);const admitted=m.admitHudFrame(frame,runId,Date.now(),-1);assert.ok(admitted,'Current Steam monitor schema must be admitted');steamFrames.push(admitted);f.steamShown=frame.show;
   queueMicrotask(()=>f.extension?.request({...peer,request:{runId,command:'hud.ack',ok:true,status:{capable:true,game,shown:frame.show,hiddenConfirmed:!frame.show,session:frame.session,operation:frame.operation,reason:''}}}));}
  return true;
 }
 throw Error('Unexpected test IO '+command);
};
const originalOther={frameLimitAc:60,frameLimitDc:30,rtssStartup:true};
f.settings.rtss={...originalOther,monitorOutput:m.normalizeMonitorOutput({enabled:true,output:'ymcc',ymccCarrier:'desktop',steamPosition:4,rtssTemplate:'L',ymcc:{layout:'tactical',scale:1.4,position:6,fields:Object.fromEntries(m.HUD_METRICS.map(metric=>[metric.id,true])),opacity:.67,hideUnavailable:false}})};
const actions=m.createMonitorMirrorActions({allowed:()=>true,power:async()=>({generation:1,phase:'ready',hardwareWritesAllowed:true}),read:m.readMonitorOutputPreferences,save:enabled=>m.saveSettingsSection('rtss',{monitorOutput:{enabled}}),apply:m.monitorOutputCoordinator.apply});
let host;
async function toggle(enabled){const current=await m.readMonitorOutputPreferences();return actions.execute({enabled},{generation:1,checkpoint(){},controlAdmission:{monitor:JSON.stringify(current)}});}
async function check(name,fn){await fn();cases.push(name);}
async function saveOff(extra){const current=await m.readMonitorOutputPreferences();const prefs=m.normalizeMonitorOutput({...current,...extra,enabled:false});await m.saveMonitorOutputPreferences(prefs);assert((await m.monitorOutputCoordinator.apply(prefs)).ok);await flush();return prefs;}
try{
 host=m.startMonitorHudHost();await host.ready();await flush();
 await check('Current host restores original enabled desktop monitor with one shared demand and admits the new frame schema',()=>{assert(f.native);assert.equal(f.demand,1);assert.equal(host.inspect().carrier,'desktop');assert.equal(desktopFrames.at(-1).preferences.ymcc.layout,'tactical');assert.equal(desktopFrames.at(-1).data.values.fps,120);assert.equal(desktopFrames.at(-1).data.values.diskRead,38.5);});
 const original=await m.readMonitorOutputPreferences();
 await check('Decky OFF reaches the real coordinator, closes owned output and releases sample/clock users',async()=>{const result=await toggle(false);await flush();assert.equal(result.saved,true);assert.equal(result.applied,true);assert.equal(f.native,null);assert.equal(f.demand,0);assert.equal(host.inspect().clock.users,0);assert.deepEqual(await m.readMonitorOutputPreferences(),{...original,enabled:false});});
 await check('Decky ON reaches the real coordinator and restores chosen new template without resetting monitor appearance or FPS limits',async()=>{const result=await toggle(true);await flush();assert.equal(result.saved,true);assert.equal(result.applied,true);assert(f.native);assert.equal(f.demand,1);assert.deepEqual(await m.readMonitorOutputPreferences(),original);assert.deepEqual(f.saves.filter(row=>Object.keys(row.patch.monitorOutput??{}).length===1).map(row=>row.patch),[{monitorOutput:{enabled:false}},{monitorOutput:{enabled:true}}]);for(const [key,value]of Object.entries(originalOther))assert.equal(f.settings.rtss[key],value);});
 await saveOff({ymccCarrier:'steam-preferred'});
 f.extension.connected(peer);f.extension.request({...peer,request:{runId,command:'hud.status',status:{capable:true,game,shown:false,hiddenConfirmed:true,session:null,operation:0,reason:''}}});await flush();
 await check('Decky ON with Steam-preferred monitor produces an admitted HUD frame and waits for formal first-paint acknowledgement',async()=>{const result=await toggle(true);await flush();assert.equal(result.applied,true);assert.equal(host.inspect().carrier,'steam');assert.equal(f.steamShown,true);assert.equal(f.native,null);assert.equal(f.demand,1);const frame=steamFrames.findLast(frame=>frame.show);assert.equal(frame.preferences.ymcc.layout,'tactical');assert.equal(frame.data.values.fps,120);assert.equal(frame.data.values.diskRead,38.5);assert(Buffer.byteLength(JSON.stringify(frame))<=65536);});
 await check('Decky OFF closes the formal Steam HUD and leaves RTSS background running',async()=>{const result=await toggle(false);await flush();assert.equal(result.applied,true);assert.equal(f.steamShown,false);assert.equal(f.demand,0);assert.equal(f.rtss,true);});
 await check('Every current HUD template and extended metric selection remains accepted by the Steam protocol',async()=>{const sample=steamFrames.findLast(frame=>frame.show);assert(sample);for(const layout of m.HUD_LAYOUTS){const frame={...sample,expiresAt:Date.now()+6000,preferences:m.normalizeMonitorOutput({...sample.preferences,ymcc:{...sample.preferences.ymcc,layout:layout.value,fields:Object.fromEntries(m.HUD_METRICS.map(metric=>[metric.id,true]))}})};assert.ok(m.admitHudFrame(frame,runId,Date.now(),-1),layout.value);assert(Buffer.byteLength(JSON.stringify(frame))<65536);}});
 await check('Largest accepted custom background plus all new metrics stays within the native/Decky message limit',()=>{const sample=steamFrames.findLast(frame=>frame.show),prefix='data:image/webp;base64,';const image=prefix+'A'.repeat(Math.floor((45000-prefix.length)/4)*4);const preferences=m.normalizeMonitorOutput({...sample.preferences,ymcc:{...sample.preferences.ymcc,fields:Object.fromEntries(m.HUD_METRICS.map(metric=>[metric.id,true])),underlayPreset:'custom',underlayImage:image}});assert.equal(preferences.ymcc.underlayImage,image);const frame={...sample,expiresAt:Date.now()+6000,preferences};assert(m.admitHudFrame(frame,runId,Date.now(),-1));assert(Buffer.byteLength(JSON.stringify(frame))<=65536);});
 await saveOff({output:'rtss'});
 await check('Decky master switch preserves chosen RTSS output and template while controlling visibility',async()=>{assert((await toggle(true)).applied);assert.equal(f.layout,'YeManOBS-L-1.ovl');assert.equal(f.native,null);assert.equal(f.steamShown,false);assert((await toggle(false)).applied);assert.equal(f.layout,'Empty.ovl');assert.equal(f.rtss,true);});
 await saveOff({output:'steam'});
 await check('Legacy Steam native selection is migrated to the current YMCC Steam carrier before Decky toggles it',async()=>{const migrated=await m.readMonitorOutputPreferences();assert.equal(migrated.output,'ymcc');assert.equal(migrated.ymccCarrier,'steam-preferred');assert.equal(migrated.steamPosition,4);assert((await toggle(true)).applied);await flush();assert.equal(host.inspect().carrier,'steam');assert.equal(f.steamShown,true);assert.equal(f.steamPosition,0);assert((await toggle(false)).applied);await flush();assert.equal(f.steamShown,false);assert.equal(f.steamPosition,0);assert.equal(f.demand,0);});
 await saveOff({output:'ymcc',ymccCarrier:'desktop'});f.failCreate=true;
 await check('A display failure keeps accepted ON saved, reports pending and never invents an applied state',async()=>{const result=await toggle(true);await flush();assert.equal(result.saved,true);assert.equal(result.applied,false);assert.equal(result.pending,true);assert.equal((await m.readMonitorOutputPreferences()).enabled,true);assert.equal(f.native,null);});
 f.failCreate=false;await toggle(false);await flush();
 await check('Decky integration never writes unrelated frame limits or shuts RTSS down',()=>{for(const [key,value]of Object.entries(originalOther))assert.equal(f.settings.rtss[key],value);assert.equal(f.calls.some(([cmd,value])=>cmd==='rtss-background'&&value===false),false);});
 host.stop();await flush();host=null;
 await check('Host stop removes extension and every native event listener and releases all data users',()=>{assert.equal(f.extension,null);assert.equal(f.demand,0);assert([...f.events.values()].every(set=>set.size===0));});
 const report={passed:cases.length,cases,currentLayouts:m.HUD_LAYOUTS.map(layout=>layout.value),currentMetrics:m.HUD_METRICS.map(metric=>metric.id),actualProductionAction:true,actualOutputCoordinator:true,actualHudHostAndProtocols:true,nativeWindowReceiptsSynthetic:true,actualSteam:false,realConfigWrites:0,hardwareWrites:0};
 fs.writeFileSync(path.join(validation,'DECKY-MONITOR-OUTPUT-INTEGRATION.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
}finally{host?.stop();await flush();delete globalThis.__deckyMonitorOutputTest;delete globalThis.window;}
