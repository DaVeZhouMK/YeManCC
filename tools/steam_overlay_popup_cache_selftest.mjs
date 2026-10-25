import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import crypto from 'node:crypto';
const source=fs.readFileSync('native/steam_live.js','utf8');
const fixtureSource=fs.readFileSync('tools/steam_sleep_dispatch_guard_selftest.mjs','utf8');
const begin=fixtureSource.indexOf('function fixture('),end=fixtureSource.indexOf('\nconst results=[];',begin);
assert(begin>=0&&end>begin);
const body=fixtureSource.slice(begin,end),anchor='return {fn,request,stats,state,apply};';
assert.equal(body.split(anchor).length,2);
const appid=2399932135,gameid='10307630032478011392';
const build=vm.runInNewContext('('+body.replace(anchor,'return {fn,request,stats,state,apply,window,SteamClient,ui,active,editor,row};')+')',
 {source,appid,gameid,assert,vm,performance,setTimeout,clearTimeout,queueMicrotask,console});
const GETTER='GetOverlayInstances(Zt){let Nt=Array.from(this.m_mapOverlayPopupByPID.values()).reverse();return Nt=Nt.filter(Er=>Er.params.browserInfo.m_unAppID==Zt),Nt}';
const results=[];
const plain=x=>JSON.parse(JSON.stringify(x));
function fixture({pids=[],other=[],mobx=true,status=4}={}){
 const f=build();const ws={GetAppWindowIDs:()=>[],BHasStandaloneKeyboard:()=>false};const counters={values:0,aggregate:0,unsafe:0};
 const values=[...pids.map(pid=>({params:{browserInfo:{m_unAppID:appid,m_unPID:pid}}})),...other];
 const data=new Map(values.map((v,i)=>[i,mobx?{value_:v}:v]));
 const cache=mobx?{data_:data,*values(){counters.values++;for(const v of data.values())yield v.value_;}}:data;
 ws.m_mapOverlayPopupByPID=cache;
 ws.GetOverlayInstances=vm.runInNewContext('({'+GETTER+'}).GetOverlayInstances');
 f.ui.m_WindowStore=ws;f.ui.m_runningAppIDs=status===11?[]:[appid];f.row.local_per_client_data.display_status=status;
 Object.defineProperty(f.ui,'WindowStore',{configurable:true,get(){counters.aggregate++;throw new Error('aggregate-window-getter-must-not-run');}});
 const request={operation:'sleep.inspect',account:42,appids:[appid],runningLinkRead:true};
 return {...f,ws,cache,data,counters,request};
}
async function read(f){const r=await f.fn(f.request);assert.equal(r.ok,true,r.reason);assert.equal(r.mutated,false);assert.equal(f.stats.raises,0);assert.equal(f.stats.configReads,0);assert.equal(f.counters.aggregate,0);const d=r.overlayPopupCacheDiagnostic;assert(d);assert.equal(d.readOnly,true);for(const key of ['steamContactVerified','actualInputVerified','actualContinueVerified','recoveryAdmission'])assert.equal(d[key],false,key);return plain(d);}
async function test(name,run){await run();results.push({name,passed:true});}
await test('empty source-qualified cache is a real zero while UI is already not running',async()=>{const f=fixture({status:11}),d=await read(f);assert(d.sourceMatched);assert.equal(d.rows[0].available,true);assert.equal(d.rows[0].popupCount,0);assert.deepEqual(d.rows[0].pidCandidates,[]);assert.equal(d.normalGetterCalls,2);});
await test('native Map uses the same production filter and preserves target PID',async()=>{const f=fixture({pids:[11960],mobx:false}),d=await read(f);assert.equal(d.rows[0].popupCount,1);assert.deepEqual(d.rows[0].pidCandidates,[11960]);});
await test('MobX values are read by the known Valve method, not treated as missing data',async()=>{const f=fixture({pids:[11960,22000]}),d=await read(f);assert.equal(d.rows[0].popupCount,2);assert.deepEqual(d.rows[0].pidCandidates,[22000,11960]);assert.equal(f.counters.values,2);});
await test('other app popup data and arbitrary fields never enter the diagnostic',async()=>{const other={params:{browserInfo:{m_unAppID:42,m_unPID:9911,privateAccount:'SECRET_ACCOUNT'}}};const f=fixture({pids:[11960],other:[other]}),d=await read(f);assert.equal(d.rows[0].popupCount,1);assert(!JSON.stringify(d).includes('SECRET_ACCOUNT'));assert(!JSON.stringify(d).includes('9911'));});
await test('missing data store stays unknown without calling its aggregate getter',async()=>{const f=fixture();delete f.ui.m_WindowStore;const d=await read(f);assert.equal(d.rows[0].popupCount,null);assert.equal(d.rows[0].reason,'window-store-unavailable');});
await test('unrecognized getter body is not invoked even with a spoofed toString',async()=>{const f=fixture();const unsafe=()=>{f.counters.unsafe++;return [];};unsafe.toString=()=>GETTER;f.ws.GetOverlayInstances=unsafe;const d=await read(f);assert.equal(d.sourceMatched,false);assert.equal(d.rows[0].popupCount,null);assert.equal(d.rows[0].reason,'getter-source-unverified');assert.equal(f.counters.unsafe,0);});
await test('accessor-valued API method is never evaluated',async()=>{const f=fixture();Object.defineProperty(f.ws,'GetOverlayInstances',{get(){f.counters.unsafe++;throw new Error('no');}});const d=await read(f);assert.equal(d.rows[0].popupCount,null);assert.equal(f.counters.unsafe,0);});
await test('unknown map storage does not become an empty cache',async()=>{const f=fixture();f.ws.m_mapOverlayPopupByPID={values(){f.counters.unsafe++;return [];}};const d=await read(f);assert.equal(d.rows[0].reason,'map-unavailable');assert.equal(d.rows[0].popupCount,null);assert.equal(f.counters.unsafe,0);});
await test('over-budget cache is rejected before the known getter',async()=>{const f=fixture({pids:Array.from({length:65},(_,i)=>1000+i)}),d=await read(f);assert.equal(d.rows[0].reason,'map-budget');assert.equal(d.rows[0].popupCount,null);assert.equal(f.counters.values,0);});
for(const pid of [-1,1.5,true,4294967296,'11960'])await test('invalid PID candidate '+JSON.stringify(pid)+' makes the row unknown',async()=>{const f=fixture({pids:[pid]}),d=await read(f);assert.equal(d.rows[0].available,false);assert.equal(d.rows[0].popupCount,null);assert.equal(d.rows[0].pidCandidates,null);});
await test('duplicate PID candidates are not silently deduplicated',async()=>{const f=fixture({pids:[11960,11960]}),d=await read(f);assert.equal(d.rows[0].popupCount,null);assert.equal(d.rows[0].reason,'target-entry-unknown');});
await test('PID0 remains raw and never becomes contact or Continue proof',async()=>{const f=fixture({pids:[0]}),d=await read(f);assert.equal(d.rows[0].popupCount,1);assert.deepEqual(d.rows[0].pidCandidates,[0]);assert.equal(d.actualContinueVerified,false);});
await test('cache mutation during ordinary reads invalidates all rows',async()=>{const f=fixture({pids:[11960]});const original=f.cache.values;f.cache.values=function*(){f.data.set(1,{value_:{params:{browserInfo:{m_unAppID:appid,m_unPID:22000}}}});yield* original.call(this);};const d=await read(f);assert.equal(d.rows[0].popupCount,null);assert.equal(d.rows[0].available,false);assert.equal(d.rows[0].pidCandidates,null);assert(['cache-changed','target-entry-unknown'].includes(d.rows[0].reason));});
await test('PID mutation without replacing the popup invalidates the diagnostic',async()=>{const f=fixture({pids:[11960]});const original=f.cache.values;let calls=0;f.cache.values=function*(){if(++calls===2)f.data.get(0).value_.params.browserInfo.m_unPID=22000;yield* original.call(this);};const d=await read(f);assert.equal(d.rows[0].popupCount,null);assert.equal(d.rows[0].pidCandidates,null);assert.equal(d.rows[0].reason,'cache-changed');});
await test('getter errors stay unknown and do not fail the underlying UI read',async()=>{const f=fixture({other:[{}]}),d=await read(f);assert.equal(d.rows[0].reason,'cache-read-unavailable');assert.equal(d.rows[0].popupCount,null);});
await test('no target does not guess one from focus or the running list',async()=>{const f=fixture();f.request.appids=[];const d=await read(f);assert.deepEqual(d.rows,[]);assert.equal(d.normalGetterCalls,0);});
await test('ordinary inspection outside running-link lane does not install the diagnostic',async()=>{const f=fixture();f.request.runningLinkRead=false;const r=await f.fn(f.request);assert.equal(r.overlayPopupCacheDiagnostic,undefined);assert.equal(f.counters.values,0);assert.equal(f.stats.raises,0);});
const report={schema:'steam-overlay-popup-cache-production-script-tests.v1',cases:results.length,passed:results.filter(x=>x.passed).length,sourceSha256:crypto.createHash('sha256').update(source).digest('hex'),productionScriptExecuted:true,steamLiveConnection:false,runtimeActions:0,repairAccepted:false,results};
const n=process.argv.indexOf('--output');if(n>=0&&process.argv[n+1])fs.writeFileSync(process.argv[n+1],JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify({cases:report.cases,passed:report.passed,productionScriptExecuted:true,steamLiveConnection:false,repairAccepted:false}));
