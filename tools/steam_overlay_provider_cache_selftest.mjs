import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import crypto from 'node:crypto';
const source=fs.readFileSync('native/steam_live.js','utf8');
const anchorNames=['RegisterForBrowserCreated','OverlayBrowserCreated'];
const frozen=anchorNames.map(name=>fs.readFileSync('G:/YeManCC-Work/Docs/Research/Sleep/SteamNonSteamRecovery-20261011/NonSleep-AMD9950/CodexReview/ControlledFault/NormalLaunchJob/OverlayCacheLifecycle/methods/OverlayStore.'+name+'.js.txt','utf8'));
const fixtureSource=fs.readFileSync('tools/steam_sleep_dispatch_guard_selftest.mjs','utf8');
const begin=fixtureSource.indexOf('function fixture('),end=fixtureSource.indexOf('\nconst results=[];',begin);
assert(begin>=0&&end>begin);
const anchor='return {fn,request,stats,state,apply};',body=fixtureSource.slice(begin,end);
assert.equal(body.split(anchor).length,2);
const appid=2399932135,gameid='10307630032478011392',pid=11960;
const plain=value=>JSON.parse(JSON.stringify(value));
const fieldNames=['m_unPID','m_nBrowserID','m_eBrowserType','m_unAppID','m_gameID','m_eUIMode'];
const descriptor=(changes={})=>({m_unPID:pid,m_nBrowserID:7,m_eBrowserType:1,m_unAppID:appid,m_gameID:gameid,m_eUIMode:4,...changes});
function fixture({rows=[[`${gameid}:${pid}`,descriptor()]],clock=performance}={}){
 const build=vm.runInNewContext('('+body.replace(anchor,'return {fn,request,stats,state,apply,window,SteamClient,ui,active,editor,row};')+')',
  {source,appid,gameid,assert,vm,performance:clock,setTimeout,clearTimeout,queueMicrotask,console});
 const f=build(),provider={m_mapBrowserInfo:new Map(rows)};
 for(let i=0;i<anchorNames.length;i++)provider[anchorNames[i]]=vm.runInNewContext('({'+frozen[i]+'}).'+anchorNames[i]);
 f.window.overlayStore=provider;
 f.ui.m_WindowStore={m_mapOverlayPopupByPID:new Map(),GetAppWindowIDs:()=>[],BHasStandaloneKeyboard:()=>false};
 f.request={operation:'sleep.inspect',account:42,appids:[appid],runningLinkRead:true,overlayProviderCacheTarget:{pid,gameid64:gameid}};
 return {...f,provider,map:provider.m_mapBrowserInfo};
}
async function read(f){
 const r=plain(await f.fn(f.request));assert.equal(r.ok,true);assert.equal(r.mutated,false);
 const d=r.overlayProviderCacheDiagnostic;assert(d);
 for(const flag of ['snapshotAtomic','originalProviderObjectVerified','steamContactVerified','actualInputVerified','actualContinueVerified','recoveryAdmission'])assert.equal(d[flag],false);
 assert.equal(f.stats.raises,0);assert.equal(f.stats.configReads,0);
 return d;
}
function unknown(d,reason){assert.equal(d.available,false);assert.equal(d.rows,null);assert.equal(d.mapEntryCount,null);assert.equal(d.passesComplete,null);if(reason)assert.equal(d.reason,reason);}
const results=[];
async function test(name,action){await action();results.push({name,passed:true});}
await test('same-original descriptor is read without executing provider or native methods',async()=>{
 const f=fixture(),d=await read(f);assert(d.sourceMatched&&d.sourcePathQualified&&d.available);assert.equal(d.mapEntryCount,1);assert.deepEqual(d.passesComplete,[true,true]);
 assert.equal(d.rows[0].entryKey,`${gameid}:${pid}`);assert.deepEqual(d.rows[0].fields.m_gameID,{rawType:'string',value:gameid});assert.equal(d.rows[0].fields.m_unPID.value,pid);
});
await test('two complete empty reads are real zero, not source-unavailable fallback',async()=>{const d=await read(fixture({rows:[]}));assert(d.available);assert.equal(d.mapEntryCount,0);assert.deepEqual(d.rows,[]);});
await test('full GameID BigInt is tagged losslessly without JSON BigInt execution',async()=>{const d=await read(fixture({rows:[[`${gameid}:${pid}`,descriptor({m_gameID:BigInt(gameid)})]]}));assert.deepEqual(d.rows[0].fields.m_gameID,{rawType:'bigint',value:gameid});});
await test('unsafe Number GameID remains number with null representation',async()=>{const d=await read(fixture({rows:[[`${gameid}:${pid}`,descriptor({m_gameID:Number(gameid)})]]}));assert.deepEqual(d.rows[0].fields.m_gameID,{rawType:'number',value:null});});
await test('undefined and null remain different primitive tags',async()=>{const d=await read(fixture({rows:[[`${gameid}:${pid}`,descriptor({m_eBrowserType:undefined,m_eUIMode:null})]]}));assert.deepEqual(d.rows[0].fields.m_eBrowserType,{rawType:'undefined',value:null});assert.deepEqual(d.rows[0].fields.m_eUIMode,{rawType:'null',value:null});});
await test('object and symbol identity never invoke conversion or serialization hooks',async()=>{let called=0;const value={toString(){called++;throw Error('no');},toJSON(){called++;throw Error('no');}};const d=await read(fixture({rows:[[`${gameid}:${pid}`,descriptor({m_gameID:value,m_eUIMode:Symbol('private')})]]}));assert.deepEqual(d.rows[0].fields.m_gameID,{rawType:'object',value:null});assert.deepEqual(d.rows[0].fields.m_eUIMode,{rawType:'symbol',value:null});assert.equal(called,0);});
await test('negative zero and nonfinite numbers do not acquire integer representations',async()=>{const d=await read(fixture({rows:[[`${gameid}:${pid}`,descriptor({m_unPID:-0,m_eBrowserType:Infinity,m_eUIMode:NaN})]]}));for(const name of ['m_unPID','m_eBrowserType','m_eUIMode'])assert.deepEqual(d.rows[0].fields[name],{rawType:'number',value:null});});
await test('oversize BigInt invalidates the entire summary',async()=>{unknown(await read(fixture({rows:[[`${gameid}:${pid}`,descriptor({m_gameID:1n<<1024n})]]})),'summary-budget');});
await test('unrelated raw fields never cross the six-field transport boundary',async()=>{const d=await read(fixture({rows:[[`${gameid}:${pid}`,descriptor({password:'PRIVATE_TOKEN',nested:{gameid:'PRIVATE'}})]]}));assert.deepEqual(Object.keys(d.rows[0].fields),fieldNames);assert(!JSON.stringify(d).includes('PRIVATE'));});
await test('custom map entries and iterator hooks are ignored',async()=>{const f=fixture();let called=0;f.map.entries=()=>{called++;throw Error('no');};f.map[Symbol.iterator]=()=>{called++;throw Error('no');};const d=await read(f);assert(d.available);assert.equal(called,0);});
await test('missing provider is unknown rather than empty',async()=>{const f=fixture();delete f.window.overlayStore;unknown(await read(f),'data-path-unqualified');});
await test('provider global accessor is not evaluated',async()=>{const f=fixture();let calls=0;Object.defineProperty(f.window,'overlayStore',{get(){calls++;throw Error('no');}});unknown(await read(f),'data-path-unqualified');assert.equal(calls,0);});
await test('provider map accessor is not evaluated',async()=>{const f=fixture();let calls=0;Object.defineProperty(f.provider,'m_mapBrowserInfo',{get(){calls++;throw Error('no');}});unknown(await read(f),'data-path-unqualified');assert.equal(calls,0);});
await test('method accessor is not evaluated',async()=>{const f=fixture();let calls=0;Object.defineProperty(f.provider,'RegisterForBrowserCreated',{get(){calls++;throw Error('no');}});unknown(await read(f),'provider-source-unverified');assert.equal(calls,0);});
await test('spoofed method own toString does not qualify changed source',async()=>{const f=fixture();let calls=0;const bad=()=>{calls++;return [];};bad.toString=()=>frozen[0];f.provider.RegisterForBrowserCreated=bad;unknown(await read(f),'provider-source-unverified');assert.equal(calls,0);});
await test('both frozen source anchors must match',async()=>{const f=fixture();f.provider.OverlayBrowserCreated=()=>{};unknown(await read(f),'provider-source-unverified');});
await test('Map-like wrappers do not bypass genuine Map branding',async()=>{const f=fixture();let calls=0;f.provider.m_mapBrowserInfo={data_:f.map,entries(){calls++;return [][Symbol.iterator]();}};unknown(await read(f),'map-brand-unqualified');assert.equal(calls,0);});
await test('own descriptor field accessors stay unknown without invocation',async()=>{let calls=0;const row=descriptor();Object.defineProperty(row,'m_gameID',{get(){calls++;return gameid;}});unknown(await read(fixture({rows:[[`${gameid}:${pid}`,row]]})),'field-unavailable');assert.equal(calls,0);});
await test('missing data field is not fabricated as null',async()=>{const row=descriptor();delete row.m_nBrowserID;unknown(await read(fixture({rows:[[`${gameid}:${pid}`,row]]})),'field-unavailable');});
await test('overbudget map fails before enumerating entries',async()=>{const rows=Array.from({length:129},(_,i)=>[`${gameid}:${pid+i}`,descriptor({m_unPID:pid+i})]);unknown(await read(fixture({rows})),'map-budget');});
await test('summary byte budget clears all rows instead of emitting partial absence',async()=>{const rows=Array.from({length:60},(_,i)=>[`${gameid}:${pid+i}`,descriptor({m_unPID:pid+i})]);unknown(await read(fixture({rows})),'summary-budget');});
await test('UTF16 field string limit does not truncate an identity',async()=>{unknown(await read(fixture({rows:[[`${gameid}:${pid}`,descriptor({m_gameID:'1'.repeat(65)})]]})),'summary-budget');});
await test('entry key types are not coerced',async()=>{unknown(await read(fixture({rows:[[123,descriptor()]]})),'field-unavailable');});
await test('primitive row values are not followed or reinterpreted',async()=>{unknown(await read(fixture({rows:[[`${gameid}:${pid}`,7]]})),'field-unavailable');});
await test('field changes between passes invalidate membership evidence',async()=>{let calls=0;const target=descriptor(),row=new Proxy(target,{getOwnPropertyDescriptor(o,k){if(++calls===7)o.m_unPID=22000;return Reflect.getOwnPropertyDescriptor(o,k);}});unknown(await read(fixture({rows:[[`${gameid}:${pid}`,row]]})),'cache-changed');});
await test('Map replacement during inspection invalidates the whole snapshot',async()=>{let calls=0;const f=fixture();const target=descriptor(),row=new Proxy(target,{getOwnPropertyDescriptor(o,k){if(++calls===7)f.provider.m_mapBrowserInfo=new Map();return Reflect.getOwnPropertyDescriptor(o,k);}});f.map.set(`${gameid}:${pid}`,row);unknown(await read(f),'cache-changed');});
await test('arbitrary thrown object message getter is never inspected',async()=>{let calls=0;const error={get message(){calls++;throw Error('no');}};const row=new Proxy(descriptor(),{getOwnPropertyDescriptor(){throw error;}});unknown(await read(fixture({rows:[[`${gameid}:${pid}`,row]]})),'cache-read-unavailable');assert.equal(calls,0);});
await test('expired read window has no retry or partial result',async()=>{let tick=0;const clock={now:()=>{tick+=300;return tick;}};unknown(await read(fixture({clock})),'read-window-expired');});
await test('later row descriptor mutation cannot hide an earlier target change',async()=>{
 const original=descriptor();let calls=0;const other=new Proxy(descriptor({m_unPID:pid+1,m_unAppID:42,m_gameID:'42'}),{getOwnPropertyDescriptor(o,k){if(++calls===7)original.m_nBrowserID=99;return Reflect.getOwnPropertyDescriptor(o,k);}});
 unknown(await read(fixture({rows:[[`${gameid}:${pid}`,original],[`42:${pid+1}`,other]]})),'cache-changed');
});
await test('target tail is checked even in a single pass before the next enumeration',async()=>{
 const original=descriptor();let calls=0;const other=new Proxy(descriptor({m_unPID:pid+1,m_unAppID:42,m_gameID:'42'}),{getOwnPropertyDescriptor(o,k){if(++calls===1)original.m_eUIMode=11;return Reflect.getOwnPropertyDescriptor(o,k);}});
 unknown(await read(fixture({rows:[[`${gameid}:${pid}`,original],[`42:${pid+1}`,other]]})),'cache-changed');
});
await test('missing original native witness never selects a provider row',async()=>{const f=fixture();delete f.request.overlayProviderCacheTarget;unknown(await read(f),'original-identity-bracket-unqualified');});
await test('invalid witness PID/full GameID is never repaired using AppID or focus',async()=>{for(const target of [{pid:0,gameid64:gameid},{pid,gameid64:Number(gameid)},{pid,gameid64:'0'+gameid},{pid,gameid64:'42'}]){const f=fixture();f.request.overlayProviderCacheTarget=target;unknown(await read(f),'original-identity-bracket-unqualified');}});
await test('no target does not select one from focus or provider entries',async()=>{const f=fixture();f.request.appids=[];unknown(await read(f),'no-target');});
await test('non-running-link inspection does not touch provider data',async()=>{const f=fixture();let calls=0;Object.defineProperty(f.window,'overlayStore',{get(){calls++;throw Error('no');}});f.request.runningLinkRead=false;const r=plain(await f.fn(f.request));assert.equal(r.overlayProviderCacheDiagnostic,undefined);assert.equal(calls,0);});
const report={schema:'steam-overlay-provider-cache-production-script-tests.v1',cases:results.length,passed:results.filter(x=>x.passed).length,sourceSha256:crypto.createHash('sha256').update(source).digest('hex'),productionScriptExecuted:true,steamLiveConnection:false,runtimeActions:0,repairAccepted:false,results};
const n=process.argv.indexOf('--output');if(n>=0&&process.argv[n+1])fs.writeFileSync(process.argv[n+1],JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify({cases:report.cases,passed:report.passed,productionScriptExecuted:true,steamLiveConnection:false,repairAccepted:false}));
