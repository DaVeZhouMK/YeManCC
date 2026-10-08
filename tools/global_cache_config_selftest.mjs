// Global cache/config invariants. No fan/input feature operations; all I/O is memory-only.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createRequire} from 'node:module';
const root=process.cwd(),require=createRequire(path.join(root,'package.json'));
const {build}=require('esbuild');
const apiSource=fs.readFileSync(path.join(root,'src/bridge/api.ts'),'utf8');
const apiNames=[...apiSource.matchAll(/export (?:const|function) (\w+)/g)].map(m=>m[1]);
const fakeApi=apiNames.map(name=>['fs','settingsStore'].includes(name)?'':`export const ${name}=new Proxy({}, {get:(_target,key)=>(...args)=>{const fn=globalThis.__fixture.services[${JSON.stringify(name)}]?.[key];if(fn)return fn(...args);throw Error('Forbidden native operation: ${name}.'+String(key))}});`).join('\n')+String.raw`
const fx=globalThis.__fixture;
export const fs={
 exists:async p=>fx.files.has(p),
 readTextFile:async (p,n)=>{await Promise.resolve();fx.reads.push(p);if(fx.readHook)return fx.readHook(p,n);if(!fx.files.has(p))throw Error('missing fixture');return fx.files.get(p)},
 mkdir:async()=>true,
 remove:async p=>fx.files.delete(p),
 readDir:async p=>fx.services.fs?.readDir?fx.services.fs.readDir(p):[],
 writeTextFile:async(p,s)=>{fx.files.set(p,s);fx.writes.push({path:p,content:s});return true},
 writeTextFileAtomic:async(p,s)=>{if(fx.atomicHook)await fx.atomicHook(p,s);fx.files.set(p,s);fx.writes.push({path:p,content:s});},
 rename:async(a,b)=>{if(!fx.files.has(a))return false;fx.files.set(b,fx.files.get(a));fx.files.delete(a);fx.renames.push({from:a,to:b});return true;},
};
function object(v){return v!==null&&typeof v==='object'&&!Array.isArray(v)}
function delta(current,base,next){
 if(JSON.stringify(base)===JSON.stringify(next))return current;
 if(!object(next)||(!object(base)&&base!==null))return next;
 const result=object(current)?current:{};
 if(object(base))for(const k of Object.keys(base))if(!(k in next))delete result[k];
 for(const k of Object.keys(next))if(!object(base)||!(k in base)||JSON.stringify(base[k])!==JSON.stringify(next[k]))result[k]=delta(result[k],object(base)&&k in base?base[k]:null,next[k]);
 return result;
}
function fill(current,next){for(const k of Object.keys(next)){if(!(k in current))current[k]=next[k];else if(object(current[k])&&object(next[k]))fill(current[k],next[k])}return current}
export const settingsStore={read:async(p,known)=>{
 await Promise.resolve();fx.stats++;const current=fx.files.get(p);const stamp=current===undefined?'missing':current;
 if(known!==null&&known===stamp&&stamp!=='missing')return {stamp,unchanged:true};
 try{return {stamp,unchanged:false,content:await fs.readTextFile(p)}}catch(e){if(fx.files.has(p))throw Error('读取配置文件失败：'+e.message);return {stamp:'missing',unchanged:false,content:''}}
},write:async(p,s,metadata)=>{
 fx.writes.push({path:p,content:s,metadata});if(fx.failWrite)return false;
 if(fx.commitHook)await fx.commitHook(p);
 const incoming=JSON.parse(s);let old;try{old=JSON.parse(fx.files.get(p)||'{}')}catch{old={}};
 const next=metadata ? (metadata.initialize?fill(structuredClone(old),incoming):delta(structuredClone(old),metadata.baseline,incoming)) : incoming;
 // Mirror the currently inspected native document writer's owner protections.
 for(const k of ['domainLogs','steamDeckMouse'])if(k in old)next[k]=old[k];
 if(old.input&&(old.input.revision??-1)>(next.input?.revision??-1))next.input=old.input;
 fx.files.set(p,JSON.stringify(next));return metadata?{ok:true,content:JSON.stringify(next)}:true;
}};
`;
async function fixture(imports){
 const result=await build({stdin:{contents:imports,resolveDir:root,loader:'ts'},bundle:true,external:['vue'],format:'cjs',platform:'node',write:false,alias:{'@':path.join(root,'src')},plugins:[{name:'strict-memory-api',setup(b){b.onLoad({filter:/api\.ts$/},()=>({contents:fakeApi,loader:'ts'}));b.onLoad({filter:/ipc\.ts$/},()=>({contents:`export const invoke=async(c,a)=>{const f=globalThis.__fixture.ipc[c];if(!f)throw Error('Forbidden fixture IPC '+c);return f(a)};export const on=()=>()=>{};export const notify=()=>{};`,loader:'ts'}));}}]});
 const data={stats:0,commitHook:null,atomicHook:null,ipc:{},files:new Map(),reads:[],writes:[],renames:[],failWrite:false,readHook:null,services:{}};
 const local=new Map(),events=[];const module={exports:{}};
 const context={Audio:class{addEventListener(){}removeAttribute(){}load(){}pause(){}play(){return Promise.resolve()}},module,exports:module.exports,require,console,process:{env:{NODE_ENV:'production'},platform:'win32',version:process.version},structuredClone,TextEncoder,TextDecoder,URL,performance,setTimeout,clearTimeout,setInterval,clearInterval,__fixture:data,
 document:{visibilityState:'visible',addEventListener(){},removeEventListener(){},querySelector(){return null}},
 window:{addEventListener(){},removeEventListener(){},dispatchEvent(e){events.push(e);return true},setTimeout,clearTimeout},
 CustomEvent:class{constructor(type,init){this.type=type;this.detail=init?.detail}},
 localStorage:{getItem:k=>local.get(k)??null,setItem:(k,v)=>local.set(k,String(v)),removeItem:k=>local.delete(k)},
 };
 vm.runInNewContext(result.outputFiles[0].text,context,{filename:'memory-only-global-config.cjs'});
 const repo=module.exports.repo;
 const initial=repo.normalizeSettings({tdp:{tdpMax:37,fpsLimit:75,float:{target:120,profile:'none',tdpStrategy:'none'}}});
 data.files.set(repo.SETTINGS_FILE,JSON.stringify(initial));
 return {...module.exports,data,initial,events};
}
const base=`export * as repo from './src/bridge/settingsRepository';`;
const cases=[];
async function check(name,run){try{await run();cases.push({name,ok:true});console.log('PASS '+name)}catch(e){cases.push({name,ok:false,error:e.message});console.log('FAIL '+name+': '+e.message)}}
const ticks=async()=>{for(let i=0;i<12;i++)await Promise.resolve()};
await check('coalesced initial settings readers own different snapshots',async()=>{
 const f=await fixture(base);const [a,b]=await Promise.all([f.repo.loadSettings(),f.repo.loadSettings()]);
 a.tdp.tdpMax=199;assert.equal(b.tdp.tdpMax,37,'another reader received the same mutable initial-load object');
});
await check('native background changes survive a later unrelated UI save',async()=>{
 const f=await fixture(base);await f.repo.loadSettings();const disk=JSON.parse(f.data.files.get(f.repo.SETTINGS_FILE));
 disk.background.asset={file:'native-new.png',enabled:true};f.data.files.set(f.repo.SETTINGS_FILE,JSON.stringify(disk));
 await f.repo.saveSettingsSection('ui',{theme:'red-black'});
 assert.equal(JSON.parse(f.data.files.get(f.repo.SETTINGS_FILE)).background.asset.file,'native-new.png','stale full-document write erased a native change');
});
await check('settings reads observe external persisted updates',async()=>{
 const f=await fixture(base);await f.repo.loadSettings();const disk=JSON.parse(f.data.files.get(f.repo.SETTINGS_FILE));disk.sleep.externalMarker=42;
 f.data.files.set(f.repo.SETTINGS_FILE,JSON.stringify(disk));const loaded=await f.repo.readSettingsSection('sleep');
 assert.equal(loaded.externalMarker,42,'the shared cache never invalidated');
});
await check('changing settings directories isolates an in-flight old load',async()=>{
 const f=await fixture(base);const oldPath=f.repo.SETTINGS_FILE;let release;
 f.data.readHook=async p=>{if(p===oldPath)return new Promise(r=>{release=r});if(!f.data.files.has(p))throw Error('missing fixture');return f.data.files.get(p)};
 const oldLoad=f.repo.loadSettings();await ticks();assert.equal(typeof release,'function');
 f.repo.setSettingsDirectory('Q:\\CONFIG-B');const nextPath=f.repo.SETTINGS_FILE;const next=f.repo.normalizeSettings({tdp:{tdpMax:91}});f.data.files.set(nextPath,JSON.stringify(next));
 const newLoad=f.repo.loadSettings();release(JSON.stringify(f.initial));await oldLoad.catch(()=>{});
 assert.equal((await newLoad).tdp.tdpMax,91,'the new directory reused the previous directory load/cache');
});
await check('queued old-directory writes cannot be redirected into a new directory',async()=>{
 const f=await fixture(base);await f.repo.loadSettings();const old=f.repo.SETTINGS_FILE;
 const pending=f.repo.saveSettingsSection('sleep',{oldDirectoryOnly:true});f.repo.setSettingsDirectory('Q:\\CONFIG-B');
 const next=f.repo.SETTINGS_FILE;f.data.files.set(next,JSON.stringify(f.initial));await pending.catch(()=>{});
 assert.equal(JSON.parse(f.data.files.get(next)).sleep.oldDirectoryOnly,undefined,'old request wrote into the newly selected directory');
 assert.notEqual(old,next);
});
await check('existing unreadable settings are not renamed or overwritten',async()=>{
 const f=await fixture(base);const file=f.repo.SETTINGS_FILE,before=f.data.files.get(file);
 f.data.readHook=async p=>{if(p===file)throw Error('simulated sharing violation');throw Error('missing fixture')};
 await assert.rejects(f.repo.loadSettings(),/读取配置文件失败/);assert.equal(f.data.files.get(file),before);assert.equal(f.data.writes.length,0);assert.equal(f.data.renames.length,0);
});
await check('UI setter captures the submitted patch before waiting in its own queue',async()=>{
 const f=await fixture(base+`export * as ui from './src/bridge/uiSettings';`);await f.ui.loadUiSettings();
 const patch={backgroundOpacity:0.3};const pending=f.ui.setUiSettings(patch);patch.backgroundOpacity=0.7;await pending;
 assert.equal(JSON.parse(f.data.files.get(f.repo.SETTINGS_FILE)).ui.backgroundOpacity,0.3,'UI queue captured a later mutation rather than the submitted value');
});
await check('saving a TDP maximum does not restore an obsolete float target',async()=>{
 const f=await fixture(base+`export * as yeman from './src/bridge/yeman';`);await f.yeman.readTdp('ac');
 await f.repo.saveSettingsSection('tdp',{float:{target:75,profile:'bal',tdpStrategy:'small'}});
 await f.yeman.saveTdp('ac',44);const persisted=JSON.parse(f.data.files.get(f.repo.SETTINGS_FILE));
 assert.equal(persisted.tdp.tdpMax,44);assert.equal(persisted.tdp.float.target,75,'TDP control cache wrote its old whole section over the new float target');
});
await check('saving game-custom data never replaces a valid backup with corrupt main data',async()=>{
 const f=await fixture(base+`export * as schedule from './src/bridge/performanceSchedule';`);
 const dir=path.win32.dirname(f.repo.SETTINGS_FILE),main=dir+'\\game-custom.json',backup=main+'.bak';
 const good=JSON.stringify({version:1,entries:{'fixture.exe':{enabled:true,displayName:'kept'}}});f.data.files.set(main,'{broken');f.data.files.set(backup,good);
 await f.schedule.saveGameCustomConfig({version:1,entries:{}});
 assert.equal(f.data.files.get(backup),good,'corrupt main content overwrote the recoverable standalone backup');
});
await check('a temporarily unreadable game-custom main file is not treated as an empty config',async()=>{
 const f=await fixture(base+`export * as schedule from './src/bridge/performanceSchedule';`);
 const main=path.win32.dirname(f.repo.SETTINGS_FILE)+'\\game-custom.json';f.data.files.set(main,JSON.stringify({version:1,entries:{'fixture.exe':{enabled:true}}}));
 f.data.readHook=async p=>{if(p===main)throw Error('simulated sharing violation');if(!f.data.files.has(p))throw Error('missing fixture');return f.data.files.get(p)};
 await assert.rejects(f.schedule.loadGameCustomConfig(),/read|读取|sharing/i);
});
await check('a rejected LOCALAPPDATA lookup is retried instead of permanently cached',async()=>{
 const f=await fixture(base+`export * as quick from './src/bridge/quickapp';`);let attempts=0;const localData=path.win32.normalize('Q:/LocalData');
 f.data.services.shell={run:async()=>{attempts++;if(attempts===1)throw Error('first-localdata-failure');return {exitCode:0,stdout:localData+'\r\n',stderr:''}}};
 f.data.files.set(path.win32.join(localData,'Lossless Scaling','Settings.xml'),'<Settings>\n  <GameProfiles>\n  </GameProfiles>\n</Settings>');
 await assert.rejects(f.quick.ensureLsProfile('Q:/fixture.exe'),/first-localdata-failure/);
 await f.quick.ensureLsProfile('Q:/fixture.exe');assert.equal(attempts,2);
});
function seedChart(f){
 const source=fs.readFileSync(path.join(root,'src/bridge/steamCharts.ts'),'utf8').match(/const STEAM_FEATURED_URL = '([^']+)'/)[1];
 const dir=path.win32.dirname(f.repo.SETTINGS_FILE);f.data.services.app={powerControlDir:async()=>dir};
 const items=Array.from({length:5},(_,i)=>({rank:i+1,appId:i+1,name:'cached-'+i,url:'https://store.steampowered.com/app/'+(i+1),imageUrl:'https://fixture.invalid/image.png',fallbackImageUrl:'https://fixture.invalid/fallback.png'}));
 f.data.files.set(dir+'\\SteamCharts\\steam.json',JSON.stringify({schemaVersion:2,mode:'steam',source,refreshedAt:new Date().toISOString(),period:'fixture',items}));
}
await check('concurrent Steam chart readers cannot mutate each others item records',async()=>{
 const f=await fixture(base+`export * as charts from './src/bridge/steamCharts';`);seedChart(f);
 const [a,b]=await Promise.all([f.charts.loadSteamCharts('steam'),f.charts.loadSteamCharts('steam')]);
 a.items[0].name='caller-mutation';assert.equal(b.items[0].name,'cached-0','coalesced chart callers share mutable item records');
});
await check('forced chart refresh does not silently reuse an in-flight ordinary cache hit',async()=>{
 const f=await fixture(base+`export * as charts from './src/bridge/steamCharts';`);seedChart(f);let requests=0;
 f.data.services.http={request:async()=>{requests++;return {status:200,body:JSON.stringify({top_sellers:{items:Array.from({length:5},(_,i)=>({id:i+1,name:'online-'+i,large_capsule_image:'https://fixture.invalid/image.png',small_capsule_image:'https://fixture.invalid/fallback.png'}))}})}}};
 const ordinary=f.charts.loadSteamCharts('steam');const forced=f.charts.loadSteamCharts('steam',true);await ordinary;
 const result=await forced;assert.equal(result.fromCache,false,'manual refresh reused an ordinary cache-only request');assert.equal(requests,1);
});

await check('native changes made between read and commit survive and enter cache',async()=>{
 const f=await fixture(base);await f.repo.loadSettings();f.data.commitHook=async p=>{f.data.commitHook=null;const d=JSON.parse(f.data.files.get(p));d.background.concurrent='native';d.tdp.float.target=66;d.future={opaque:'kept'};d.fan.opaqueSentinel='fan';d.input.nativeUnknown='input';f.data.files.set(p,JSON.stringify(d))};
 await f.repo.saveSettingsSection('ui',{theme:'cyberpunk'});const d=await f.repo.loadSettings();assert.equal(d.background.concurrent,'native');assert.equal(d.tdp.float.target,66);assert.equal(d.fan.opaqueSentinel,'fan');assert.equal(d.input.nativeUnknown,'input');assert.equal(d.future.opaque,'kept');
});
await check('unchanged explicit reads validate metadata without repeated JSON/file reads',async()=>{
 const f=await fixture(base);await f.repo.loadSettings();const reads=f.data.reads.length;
 for(let i=0;i<40;i++)await f.repo.loadSettings();assert.equal(f.data.reads.length,reads);assert.equal(f.data.stats,41);
});
await check('failed saves do not advance cache and the queue remains live',async()=>{
 const f=await fixture(base);await f.repo.loadSettings();f.data.failWrite=true;
 await assert.rejects(f.repo.saveSettingsSection('tdp',{tdpMax:99}),/写入失败/);assert.equal((await f.repo.loadSettings()).tdp.tdpMax,37);
 f.data.failWrite=false;await f.repo.saveSettingsSection('tdp',{tdpMax:42});assert.equal((await f.repo.loadSettings()).tdp.tdpMax,42);
});
await check('failed initial migration remains retryable and cannot publish defaults as durable',async()=>{
 const f=await fixture(base);f.data.files.delete(f.repo.SETTINGS_FILE);f.data.failWrite=true;await assert.rejects(f.repo.loadSettings(),/写入失败/);assert.equal(f.data.files.has(f.repo.SETTINGS_FILE),false);
 f.data.failWrite=false;await f.repo.loadSettings();assert.equal(f.data.files.has(f.repo.SETTINGS_FILE),true);
});
await check('unreadable backup does not archive a corrupt main or manufacture empty settings',async()=>{
 const f=await fixture(base);f.data.files.set(f.repo.SETTINGS_FILE,'{broken');f.data.files.set(f.repo.SETTINGS_BACKUP_FILE,JSON.stringify(f.initial));f.data.readHook=async p=>{if(p===f.repo.SETTINGS_BACKUP_FILE)throw Error('sharing denied');if(!f.data.files.has(p))throw Error('missing');return f.data.files.get(p)};
 await assert.rejects(f.repo.loadSettings(),/读取/);assert.equal(f.data.files.get(f.repo.SETTINGS_FILE),'{broken');assert.equal(f.data.renames.length,0);assert.equal(f.data.writes.length,0);
});
await check('UI partial save does not replay unrelated stale UI fields',async()=>{
 const f=await fixture(base+`export * as ui from './src/bridge/uiSettings';`);await f.ui.loadUiSettings();const d=JSON.parse(f.data.files.get(f.repo.SETTINGS_FILE));d.ui.backgroundBlur=17;d.ui.future={opaque:true};f.data.files.set(f.repo.SETTINGS_FILE,JSON.stringify(d));await f.ui.setUiSettings({backgroundOpacity:0.31});
 const result=JSON.parse(f.data.files.get(f.repo.SETTINGS_FILE));assert.equal(result.ui.backgroundBlur,17);assert.equal(result.ui.future.opaque,true);
});
await check('UI read errors are retryable rather than cached as default settings',async()=>{
 const f=await fixture(base+`export * as ui from './src/bridge/uiSettings';`);await f.ui.loadUiSettings();f.repo.clearSettingsCache();f.data.readHook=async()=>{throw Error('sharing denied')};await assert.rejects(f.ui.loadUiSettings(),/读取/);
 f.data.readHook=null;await f.ui.loadUiSettings();assert.equal(f.ui.getUiSettings().theme,'blue-black');
});
await check('CPU profiles observe newer persisted profiles and propagate read errors',async()=>{
 const f=await fixture(base+`export * as cpu from './src/bridge/cpuProfiles';`);await f.cpu.loadCpuProfiles();const d=JSON.parse(f.data.files.get(f.repo.SETTINGS_FILE));d.cpu.active='extreme';d.cpu.profiles={balanced:{acFreq:3333}};f.data.files.set(f.repo.SETTINGS_FILE,JSON.stringify(d));const c=await f.cpu.loadCpuProfiles();assert.equal(c.active,'extreme');assert.equal(c.profiles.balanced.acFreq,3333);
 f.repo.clearSettingsCache();f.data.readHook=async()=>{throw Error('sharing denied')};await assert.rejects(f.cpu.loadCpuProfiles(),/读取/);
});
await check('CPU autostart branch saves preserve the other branch and unknown data',async()=>{
 const f=await fixture(base+`export * as startup from './src/bridge/autostart';`);await Promise.all([f.startup.writeCcdAutostart(true,2),f.startup.writeUvAutostart(true,'safe','amd')]);const d=JSON.parse(f.data.files.get(f.repo.SETTINGS_FILE));assert.equal(d.cpu.autostart.ccd.mode,2);assert.equal(d.cpu.autostart.uv.preset,'safe');
});
await check('autofloat queued writes capture distinct submitted states',async()=>{
 const f=await fixture(base+`export * as auto from './src/bridge/autofloat';`);
 await Promise.all([f.auto.setFloatTarget(80),f.auto.setFloatTarget(90)]);
 const writes=f.data.writes.filter(w=>w.metadata&&!w.metadata.initialize).map(w=>JSON.parse(w.content).tdp.float.target);assert.deepEqual(writes,[80,90]);
});
await check('game-custom cold read and save cannot finish in reversed durable order',async()=>{
 const f=await fixture(base+`export * as schedule from './src/bridge/performanceSchedule';`);const main=path.win32.dirname(f.repo.SETTINGS_FILE)+'\\game-custom.json';f.data.files.set(main,JSON.stringify({version:1,entries:{'old.exe':{enabled:true}}}));
 const cold=f.schedule.loadGameCustomConfig(),save=f.schedule.saveGameCustomConfig({version:1,entries:{'new.exe':{enabled:true}}});await cold;await save;assert.equal(f.schedule.getGameCustomConfig().entries['old.exe'],undefined);assert.equal(f.schedule.getGameCustomConfig().entries['new.exe'].enabled,true);
});
await check('game-custom save failure does not publish cache or changed events',async()=>{
 const f=await fixture(base+`export * as schedule from './src/bridge/performanceSchedule';`);const main=path.win32.dirname(f.repo.SETTINGS_FILE)+'\\game-custom.json';const original=JSON.stringify({version:1,entries:{'old.exe':{enabled:true}}});f.data.files.set(main,original);await f.schedule.loadGameCustomConfig();let events=0;f.schedule.onGameCustomConfigChanged(()=>events++);f.data.atomicHook=async p=>{if(p===main)throw Error('atomic denied')};await assert.rejects(f.schedule.saveGameCustomConfig({version:1,entries:{}}),/atomic denied/);assert.equal(events,0);assert.equal(f.data.files.get(main),original);assert.equal(f.schedule.getGameCustomConfig().entries['old.exe'].enabled,true);
});
await check('game-custom backup failure prevents main change and a later save succeeds',async()=>{
 const f=await fixture(base+`export * as schedule from './src/bridge/performanceSchedule';`);const main=path.win32.dirname(f.repo.SETTINGS_FILE)+'\\game-custom.json';const original=JSON.stringify({version:1,entries:{'old.exe':{enabled:true}}});f.data.files.set(main,original);f.data.atomicHook=async p=>{if(p===main+'.bak')throw Error('backup denied')};await assert.rejects(f.schedule.saveGameCustomConfig({version:1,entries:{}}),/backup denied/);assert.equal(f.data.files.get(main),original);f.data.atomicHook=null;await f.schedule.saveGameCustomConfig({version:1,entries:{}});assert.equal(Object.keys(JSON.parse(f.data.files.get(main)).entries).length,0);
});
await check('retained game-custom unknown fields survive but deleted entries do not resurrect',async()=>{
 const f=await fixture(base+`export * as schedule from './src/bridge/performanceSchedule';`);const main=path.win32.dirname(f.repo.SETTINGS_FILE)+'\\game-custom.json';f.data.files.set(main,JSON.stringify({version:1,future:{opaque:true},entries:{'keep.exe':{future:'kept',ac:{future:'nested'}},'delete.exe':{enabled:true}}}));await f.schedule.saveGameCustomConfig({version:1,entries:{'keep.exe':{enabled:false}}});const d=JSON.parse(f.data.files.get(main));assert.equal(d.future.opaque,true);assert.equal(d.entries['keep.exe'].future,'kept');assert.equal(d.entries['keep.exe'].ac.future,'nested');assert.equal(d.entries['delete.exe'],undefined);
});
await check('CPU lock failed save leaves synchronous cache on durable state',async()=>{
 const f=await fixture(base+`export * as lock from './src/bridge/cpulock';`);await f.lock.loadCpuLock();const original=f.lock.getCpuLock();f.data.failWrite=true;await assert.rejects(f.lock.setCpuLock('ac',{freq:2222},false),/写入失败/);assert.deepEqual(f.lock.getCpuLock(),original);f.data.failWrite=false;await f.lock.setCpuLock('ac',{freq:2222},false);assert.equal(f.lock.getCpuLock('ac').freq,2222);
});
await check('CPU lock queued patch capture and independent AC/DC saves are isolated',async()=>{
 const f=await fixture(base+`export * as lock from './src/bridge/cpulock';`);await f.lock.loadCpuLock();const v={freq:2222};const a=f.lock.setCpuLock('ac',v,false);v.freq=4444;const b=f.lock.setCpuLock('dc',{freq:1111},false);await Promise.all([a,b]);assert.equal(f.lock.getCpuLock('ac').freq,2222);assert.equal(f.lock.getCpuLock('dc').freq,1111);
});
await check('Lossless Scaling concurrent profile saves retain both profiles',async()=>{
 const f=await fixture(base+`export * as quick from './src/bridge/quickapp';`);f.data.services.shell={run:async()=>({exitCode:0,stdout:'Q:/LocalData',stderr:''})};const file=path.win32.normalize('Q:/LocalData/Lossless Scaling/Settings.xml');f.data.files.set(file,'<Settings>\n  <GameProfiles>\n  </GameProfiles>\n</Settings>');await Promise.all([f.quick.ensureLsProfile('Q:/a.exe'),f.quick.ensureLsProfile('Q:/b.exe')]);assert.match(f.data.files.get(file),/a\.exe/);assert.match(f.data.files.get(file),/b\.exe/);
});
await check('Lossless Scaling rejects corrupt XML before touching a valid backup',async()=>{
 const f=await fixture(base+`export * as quick from './src/bridge/quickapp';`);f.data.services.shell={run:async()=>({exitCode:0,stdout:'Q:/LocalData',stderr:''})};const file=path.win32.normalize('Q:/LocalData/Lossless Scaling/Settings.xml');f.data.files.set(file,'<broken>');f.data.files.set(file+'.ymccbak','known-good');await assert.rejects(f.quick.ensureLsProfile('Q:/a.exe'),/格式/);assert.equal(f.data.files.get(file),'<broken>');assert.equal(f.data.files.get(file+'.ymccbak'),'known-good');
});
await check('Lossless Scaling backup failure prevents a main replacement',async()=>{
 const f=await fixture(base+`export * as quick from './src/bridge/quickapp';`);f.data.services.shell={run:async()=>({exitCode:0,stdout:'Q:/LocalData',stderr:''})};const file=path.win32.normalize('Q:/LocalData/Lossless Scaling/Settings.xml'),original='<Settings>\n  <GameProfiles>\n  </GameProfiles>\n</Settings>';f.data.files.set(file,original);f.data.atomicHook=async p=>{if(p.endsWith('.ymccbak'))throw Error('backup denied')};await assert.rejects(f.quick.ensureLsProfile('Q:/a.exe'),/backup denied/);assert.equal(f.data.files.get(file),original);
});
await check('forced chart online fixture actually parses fresh records',async()=>{
 const f=await fixture(base+`export * as charts from './src/bridge/steamCharts';`);seedChart(f);f.data.services.http={request:async()=>({status:200,body:JSON.stringify({top_sellers:{items:Array.from({length:5},(_,i)=>({id:100+i,name:'online-'+i,large_capsule_image:'https://fixture.invalid/image.png'}))}})})};const result=await f.charts.loadSteamCharts('steam',true);assert.equal(result.fromCache,false);assert.equal(result.items[0].appId,100);assert.equal(result.items[0].name,'online-0');
});
await check('updater initialization is coalesced and retries after IPC failure',async()=>{
 const f=await fixture(base+`export * as updater from './src/bridge/updateManager';`);let attempts=0;f.data.ipc['app.updateState']=async()=>{attempts++;if(attempts===1)throw Error('transient');return {phase:'downloaded',stage:'install',version:'99.0.0',sha256:'A'.repeat(64)}};
 await Promise.all([f.updater.ensureUpdateManager(),f.updater.ensureUpdateManager()]);assert.equal(attempts,1);await f.updater.ensureUpdateManager();assert.equal(attempts,2);assert.equal(f.updater.updateSnapshot.phase,'interrupted');
});
await check('coalesced game-detection callers own separate mutable records',async()=>{
 const f=await fixture(base+`export * as game from './src/bridge/gamedetect';`);f.data.ipc['game.detect']=async()=>({pid:42,processCreated:'12345',name:'fixture.exe',title:'fixture'});const [a,b]=await Promise.all([f.game.detectGame(),f.game.detectGame()]);a.title='poison';assert.equal(b.title,'fixture');
});

await check('module-level UI queue cannot redirect an old submission after directory switch',async()=>{
 const f=await fixture(base+`export * as ui from './src/bridge/uiSettings';`);await f.ui.loadUiSettings();const pending=f.ui.setUiSettings({theme:'red-black'});f.repo.setSettingsDirectory('Q:\\NEXT-CONFIG');f.data.files.set(f.repo.SETTINGS_FILE,JSON.stringify(f.repo.normalizeSettings({ui:{theme:'cyberpunk'}})));await assert.rejects(pending,/目录已切换/);assert.equal(JSON.parse(f.data.files.get(f.repo.SETTINGS_FILE)).ui.theme,'cyberpunk');
});
await check('cache clear cancels a same-directory obsolete in-flight read',async()=>{
 const f=await fixture(base);let release;const file=f.repo.SETTINGS_FILE;f.data.readHook=async p=>{if(p===file)return new Promise(r=>release=r);throw Error('missing')};const old=f.repo.loadSettings();await ticks();f.repo.clearSettingsCache();f.data.readHook=null;const d=JSON.parse(f.data.files.get(file));d.tdp.tdpMax=71;f.data.files.set(file,JSON.stringify(d));const fresh=f.repo.loadSettings();release(JSON.stringify(f.initial));await assert.rejects(old,/目录已切换/);assert.equal((await fresh).tdp.tdpMax,71);
});
await check('tray application failure is visible and restores durable preference',async()=>{
 const f=await fixture(base+`export * as resident from './src/bridge/trayResident';`);f.data.services.tray={setResident:async()=>{throw Error('apply denied')}};await assert.rejects(f.resident.setTrayResident(true),/apply denied/);assert.equal((await f.repo.readSettingsSection('tray')).resident,false);f.data.services.tray.setResident=async()=>true;await f.resident.setTrayResident(true);assert.equal((await f.repo.readSettingsSection('tray')).resident,true);
});
await check('music initialization retries and volume persistence failure is visible',async()=>{
 const f=await fixture(base+`export * as music from './src/bridge/music';`);let attempts=0;f.data.services.music={get:async()=>{attempts++;if(attempts===1)throw Error('get denied');return {volume:0.4,mode:'random'}},setVolume:async()=>{throw Error('volume denied')}};await f.music.initMusic();await f.music.initMusic();assert.equal(attempts,2);assert.equal(f.music.volume.value,0.4);await f.music.persistVolume();assert.match(f.music.error.value,/音量保存失败/);
});
await check('unreadable suspended-state file is retained without issuing resume',async()=>{
 const f=await fixture(base+`export * as gameproc from './src/bridge/gameproc';`);const file='C:\\SOFT\\YeMan\\PowerControl\\Sleep\\quickapp_suspended.json';f.data.files.set(file,'saved-process-identity');f.data.readHook=async()=>{throw Error('sharing denied')};const result=await f.gameproc.resumeGame();assert.equal(result.ok,false);assert.equal(f.data.files.get(file),'saved-process-identity');
});
await check('trainer pending write failure prevents launch of a worker with stale results',async()=>{
 const f=await fixture(base+`export * as trainer from './src/bridge/gameTrainer';`);let workers=0;f.data.services.shell={hidden:async()=>{workers++;return {pid:42}}};f.data.atomicHook=async()=>{throw Error('pending denied')};await assert.rejects(f.trainer.openOrSearchGameTrainer('fixture'),/pending denied/);assert.equal(workers,0);
});
await check('concurrent same-title trainer searches use distinct result files',async()=>{
 const f=await fixture(base+`export * as trainer from './src/bridge/gameTrainer';`);const paths=[];f.data.services.shell={hidden:async(program,args)=>{const p=args[args.indexOf('-ResultPath')+1];paths.push(p);f.data.files.set(p,JSON.stringify({ok:true,gameName:'fixture',state:'completed',results:[]}));return {pid:42}}};await Promise.all([f.trainer.openOrSearchGameTrainer('fixture'),f.trainer.openOrSearchGameTrainer('fixture')]);assert.equal(paths.length,2);assert.notEqual(paths[0],paths[1]);assert.equal(f.data.files.has(paths[0]),false);assert.equal(f.data.files.has(paths[1]),false);
});

await check('unreadable legacy TDP number aborts migration instead of persisting defaults',async()=>{
 const f=await fixture(base);f.data.files.delete(f.repo.SETTINGS_FILE);const legacy=path.win32.dirname(f.repo.SETTINGS_FILE)+'\\tdp.txt';f.data.files.set(legacy,'44');f.data.readHook=async p=>{if(p===legacy)throw Error('legacy sharing denied');if(!f.data.files.has(p))throw Error('missing');return f.data.files.get(p)};await assert.rejects(f.repo.loadSettings(),/读取配置文件失败/);assert.equal(f.data.files.has(f.repo.SETTINGS_FILE),false);assert.equal(f.data.files.get(legacy),'44');
});
await check('unreadable remembered power configuration does not run system power commands',async()=>{
 const f=await fixture(base+`export * as yeman from './src/bridge/yeman';`);f.data.readHook=async()=>{throw Error('sharing denied')};await assert.rejects(f.yeman.reconcileRememberedPowerScheme(),/读取配置文件失败/);assert.equal(f.data.writes.length,0);
});

await check('TDP auto-apply read failure is not implicit permission to apply hardware',async()=>{
 const f=await fixture(base+`export * as autoTdp from './src/bridge/tdpAutoApply';`);f.data.readHook=async()=>{throw Error('sharing denied')};await assert.rejects(f.autoTdp.readTdpAutoApply(),/读取配置文件失败/);await assert.rejects(f.autoTdp.applyAutoTdpIfNeeded('wake','ac'),/读取配置文件失败/);assert.equal(f.data.writes.length,0);
});
await check('TDP auto-apply single-switch save preserves independently changed sibling',async()=>{
 const f=await fixture(base+`export * as autoTdp from './src/bridge/tdpAutoApply';`);await f.repo.saveSettingsSection('tdp',{autoApply:{boot:true,wake:true}});await f.autoTdp.writeTdpAutoApply({boot:false});const saved=await f.repo.readSettingsSection('tdp');assert.equal(saved.autoApply.boot,false);assert.equal(saved.autoApply.wake,true);
});
const report={scope:'global config/cache only; no fan/input feature changes; memory-only IPC',passed:cases.filter(c=>c.ok).length,failed:cases.filter(c=>!c.ok).length,cases};
console.log(JSON.stringify(report,null,2));if(process.env.YMCC_GLOBAL_CONFIG_REPORT)fs.writeFileSync(process.env.YMCC_GLOBAL_CONFIG_REPORT,JSON.stringify(report,null,2)+'\n');
if(report.failed)process.exitCode=1;







