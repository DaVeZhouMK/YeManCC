// Actual production singleton, with only IPC, file IO and clocks made inert.
// The frozen pre-change fixture is expected to reproduce shared nested metadata.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {pathToFileURL} from 'node:url';
const root=process.cwd();
const task=path.resolve(process.env.YMCC_METADATA_READ_OUT || '../../../Build/Tasks/YMCC-Steam-HUD-Probe/monitor-read-metadata-isolation-20261017/regression');
fs.mkdirSync(task,{recursive:true});
const beforePath=path.resolve(process.env.YMCC_METADATA_BEFORE || 'tools/fixtures/monitor_read_metadata_before_20261017.ts');
const currentPath=path.resolve(process.env.YMCC_METADATA_CANDIDATE || 'src/bridge/monitorData.ts');
const {build}=createRequire(path.join(root,'package.json'))('esbuild');
const keys=['fpsValidUntil','fpsPollPeriodMs','fps1Source','fps1ObservedAt','fps1ValidUntil','fps1PollPeriodMs'];
const slots=globalThis.__monitorMetadataStates={};
const pins={},results={};let importId=0;
const settle=async()=>{for(let i=0;i<35;++i)await Promise.resolve();};
function state(){return {
 now:200020, snapshotReads:0,fileReads:0,topParses:0,fpsParses:0,clones:0,timers:new Map(),nextTimer:0,commands:[],
 reply:null, files:new Map(), unknown:false,
 async invoke(command,args){this.commands.push({command,args});if(command==='monitor.snapshot'){++this.snapshotReads;if(this.unknown)throw Error('unknown:monitor.snapshot');return this.reply;}
 return {ok:true,cleaned:true,top:args?.top===true,fps:args?.fps===true};},
 async readTextFile(name){++this.fileReads;const suffix=name.split('\\').at(-1);if(!this.files.has(suffix))throw Error('Unexpected file '+name);return this.files.get(suffix);},
 clone(value){++this.clones;return structuredClone(value);},
 setTimer(fn,ms){const id=++this.nextTimer;this.timers.set(id,{fn,ms});return id;},clearTimer(id){this.timers.delete(id);}
};}
async function bundle(side,sourcePath){
 const ref=`globalThis.__monitorMetadataStates[${JSON.stringify(side)}]`;
 const text=fs.readFileSync(sourcePath,'utf8');pins[side]={path:sourcePath,sha256:crypto.createHash('sha256').update(text).digest('hex')};
 function instrument(source){for(const [needle,counter] of [['function parseTopPayload(raw: string): TopMonData | null {','topParses'],['function parseFpsPayload(raw: string): MonitorFpsPayload | null {','fpsParses']]){assert.equal(source.split(needle).length,2);source=source.replace(needle,needle+` ++${ref}.${counter};`);}
 return source.replaceAll('Date.now()',ref+'.now').replaceAll('performance.now()',ref+'.now').replaceAll('structuredClone(',ref+'.clone(').replaceAll('setTimeout(',ref+'.setTimer(').replaceAll('clearTimeout(',ref+'.clearTimer(');}
 const built=await build({stdin:{contents:`export * from 'monitor-metadata-entry';`,resolveDir:root,loader:'ts'},bundle:true,write:false,platform:'node',format:'esm',plugins:[{name:'production-metadata',setup(api){
 api.onResolve({filter:/^monitor-metadata-entry$/},()=>({path:sourcePath,namespace:'metadata-source'}));
 api.onLoad({filter:/.*/,namespace:'metadata-source'},()=>({contents:instrument(text),loader:'ts',resolveDir:path.join(root,'src/bridge')}));
 api.onResolve({filter:/^\.\/(ipc|api|monitorAppliedTdp)$/},a=>({path:a.path,namespace:'metadata-mock'}));
 api.onLoad({filter:/.*/,namespace:'metadata-mock'},a=>({loader:'ts',contents:a.path==='./ipc'?`export async function invoke(...args){return ${ref}.invoke(...args)}export function on(){return()=>{}}`:a.path==='./api'?`export const fs={readTextFile:(...args)=>${ref}.readTextFile(...args)};`:`export function advanceMonitorTdpDisplay(){}export function subscribeMonitorTdpState(){return()=>{}}`}));
 }}]});
 const file=path.join(task,`${side}-production.mjs`);fs.writeFileSync(file,built.outputFiles[0].text);
 return async()=>{const h=slots[side]=state();const m=await import(pathToFileURL(file).href+`?case=${++importId}`);return {h,m};};
}
function fixture(h,metadata={},seq=1){const ts=h.now-20;
 const top=JSON.stringify({ts,hwDown:false,cpuUsage:25,tdpW:30,tempC:65,freqMhz:3000,hasBattery:false,coreUsage:Array.from({length:128},(_,i)=>i%100),coreLabels:Array.from({length:128},(_,i)=>'CPU '+i),coreUsageTs:ts});
 const fps=JSON.stringify({ts,fps:120,fps1:90,pid:2077,processCreated:'123456789',fpsSource:'rtss',fpsObservedAt:ts,fpsValidUntil:ts+5000,fpsPollPeriodMs:0,game:'fixture',gpu:70,packagePower:30,frametime:10,frameTimeSource:'rtss-current-frame',frameTimeTs:ts,frameTimeValidUntil:ts+5000,frameTimePollPeriodMs:0,frameTimes:Array.from({length:120},(_,i)=>8+i%3),...metadata});
 return {schema:1,available:true,session:1,relayEpoch:1,generation:1,seq,collectedAt:ts,heartbeatAt:ts,hwinfoOkAt:ts,topActive:true,fpsActive:true,hwDown:false,top,fps};}
async function group(ctx,packet,mode='snapshot',beforeResolve){const {h,m}=ctx;let resolve;const pending=new Promise(r=>resolve=r);let display=null;
 const off=m.subscribeMonitorSnapshot(v=>{if(v)display=v;});const release=m.acquireMonitorDemand('metadata-fixture',{top:true,fps:true});
 if(mode==='snapshot')h.reply=pending;else{h.unknown=true;h.files=new Map([['topmon.json',packet.top],['fps-status.json',pending],['fps-monitor.hb',String(h.now-20)],['hwinfo-ok',String(h.now-20)]]);}
 try{const reads=Array.from({length:3},()=>m.readMonitorSnapshot());await settle();if(beforeResolve)await beforeResolve({release,reads});resolve(mode==='snapshot'?packet:packet.fps);
 const values=await Promise.all(reads);return {values,display,counts:{snapshotReads:h.snapshotReads,fileReads:h.fileReads,topParses:h.topParses,fpsParses:h.fpsParses,clones:h.clones}};
 }finally{release();off();await settle();assert.equal(h.timers.size,0,'all test demand/display timers released');}}
function leaf(v){return Array.isArray(v)?v[0].nested:v.nested;}
async function exercise(load){const rows=[];
 for(const mode of ['snapshot','files'])for(const key of keys)for(const array of [false,true]){
 const ctx=await load(),value=array?[{nested:{value:1}}]:{nested:{value:1}};const packet=fixture(ctx.h,{[key]:value});const r=await group(ctx,packet,mode);
 const [a,b,c]=r.values;assert(a?.fps&&b?.fps&&c?.fps&&r.display?.fps,`${mode}/${key} admitted`);
 assert.deepEqual(a.fps[key],value);assert.notEqual(a,b);assert.notEqual(a.fps,b.fps);assert.equal(r.counts.snapshotReads,1);assert.equal(r.counts.fileReads,mode==='files'?4:0);
 const independent=a.fps[key]!==b.fps[key]&&a.fps[key]!==c.fps[key]&&a.fps[key]!==r.display.fps[key];
 leaf(a.fps[key]).value=99;
 rows.push({mode,key,array,independent,peerValue:leaf(b.fps[key]).value,thirdValue:leaf(c.fps[key]).value,displayValue:leaf(r.display.fps[key]).value,counts:r.counts});
 }
 const ordinaryCtx=await load(),ordinary=await group(ordinaryCtx,fixture(ordinaryCtx.h));const [a,b,c]=ordinary.values;
 assert(a?.top&&a?.fps&&b?.top&&b?.fps&&c?.top&&ordinary.display?.top);
 const ordinaryChecks={separateOuter:a!==b,separateTop:a.top!==b.top,separateFps:a.fps!==b.fps,coreShared:a.top.coreUsage===b.top.coreUsage&&b.top.coreUsage===ordinary.display.top.coreUsage,labelsShared:a.top.coreLabels===b.top.coreLabels,framesShared:a.fps.frameTimes===b.fps.frameTimes,coreFrozen:Object.isFrozen(a.top.coreUsage),framesFrozen:Object.isFrozen(a.fps.frameTimes),fullBounds:a.top.coreUsage.length===128&&a.fps.frameTimes.length===120,noStructuredClone:ordinary.counts.clones===0,oneParse:ordinary.counts.fpsParses===1&&ordinary.counts.topParses===1};
 a.fps.fps=999;a.top.cpuUsage=99;ordinaryChecks.scalarIsolation=b.fps.fps===120&&c.top.cpuUsage===25&&ordinary.display.fps.fps===120;
 assert(Object.values(ordinaryChecks).every(Boolean),JSON.stringify(ordinaryChecks));
 const cache=[];
 for(const nested of [false,true]){const ctx=await load();const p=fixture(ctx.h,nested?{fps1ValidUntil:{nested:{value:1}}}:{});const release=ctx.m.acquireMonitorDemand('cache',{top:true,fps:true});let old;
 for(let i=0;i<4;++i){ctx.h.reply={...p,seq:i+1};const current=await ctx.m.readMonitorSnapshot();assert(current?.fps);if(old&&nested)assert.notEqual(old.fps.fps1ValidUntil,current.fps.fps1ValidUntil);old=current;}
 await settle();cache.push({nested,topParses:ctx.h.topParses,fpsParses:ctx.h.fpsParses,clones:ctx.h.clones});assert.equal(ctx.h.topParses,1);assert.equal(ctx.h.fpsParses,nested?4:1);assert.equal(ctx.h.clones,0);release();await settle();assert.equal(ctx.h.timers.size,0);
 }
 const scalar=[];for(const value of [undefined,null,0,205000,'legacy',false]){const ctx=await load();const meta=Object.fromEntries(keys.filter(()=>value!==undefined).map(k=>[k,value]));const r=await group(ctx,fixture(ctx.h,meta));assert(r.values[0]?.fps);scalar.push({value,keys:Object.keys(r.values[0].fps),fields:keys.map(k=>r.values[0].fps[k]),clones:r.counts.clones});}
 const stopCtx=await load(),stop=await group(stopCtx,fixture(stopCtx.h,{fps1ValidUntil:{nested:{value:1}}}),'snapshot',async({release})=>{release();await settle();});
 assert(stop.values.every(v=>v===null||(v.top===null&&v.fps===null)),'stop fences all three readers');
 const sessionCtx=await load(),superseded=await group(sessionCtx,fixture(sessionCtx.h,{fps1ValidUntil:{nested:{value:1}}}),'snapshot',async()=>{sessionCtx.m.acceptPublishedMonitorSnapshot({...fixture(sessionCtx.h,{},1),session:2});});
 assert(superseded.values.every(v=>v===null),'new session fences old pending readers');
 // This valid JSON is below the real 16KiB per-document wire bound. A recursive
 // structuredClone can throw even though native JSON admission succeeds.
 const deepCtx=await load(),deepPacket=fixture(deepCtx.h,{fps1ValidUntil:'__NESTED__'}),depth=2500;
 deepPacket.fps=deepPacket.fps.replace('"__NESTED__"','{"n":'.repeat(depth)+'{"value":1}'+'}'.repeat(depth));assert(Buffer.byteLength(deepPacket.fps)<16384);
 let deep;try{const r=await group(deepCtx,deepPacket);const [one,two]=r.values;assert(one?.fps&&two?.fps);let x=one.fps.fps1ValidUntil,y=two.fps.fps1ValidUntil;for(let i=0;i<depth;++i){x=x.n;y=y.n;}x.value=99;deep={admitted:true,independent:x!==y,peerValue:y.value,depth,bytes:Buffer.byteLength(deepPacket.fps),clones:r.counts.clones};}catch(error){deep={admitted:false,error:error.message,depth,bytes:Buffer.byteLength(deepPacket.fps)};}
 return {rows,ordinary:{checks:ordinaryChecks,counts:ordinary.counts},cache,scalar,stopRace:true,sessionRace:true,deep};
}
const before=await exercise(await bundle('before',beforePath));
const after=await exercise(await bundle('after',currentPath));
assert(before.rows.every(r=>!r.independent&&r.peerValue===99&&r.displayValue===99),'frozen production baseline must reproduce all 24 shared-object cases');
assert(after.rows.every(r=>r.independent&&r.peerValue===1&&r.thirdValue===1&&r.displayValue===1),'current production must isolate all readers and display');
assert.deepEqual(after.ordinary,before.ordinary,'normal production work and ownership stay unchanged');
assert.deepEqual(after.cache,before.cache,'decoder caching and normal clone counts stay unchanged');
assert.deepEqual(after.scalar,before.scalar,'legacy scalar/null/absent field admission stays unchanged');
assert.equal(after.deep.admitted,true);assert.equal(after.deep.independent,true);assert.equal(after.deep.peerValue,1);
const result={ok:true,pins,before,after,scope:'Actual production singleton and decoder; mocked native IPC, file IO, clocks and timer callbacks. No installed app, hardware, CPU timing, deployment or game changes.'};
fs.writeFileSync(path.join(task,'READ-METADATA-RESULT.json'),JSON.stringify(result,null,2));
console.log(JSON.stringify({ok:true,beforeSharedCases:before.rows.length,afterIsolatedCases:after.rows.length,ordinary:after.ordinary,cache:after.cache,scalarCases:after.scalar.length,deep:after.deep,stopRace:after.stopRace,sessionRace:after.sessionRace,pins},null,2));