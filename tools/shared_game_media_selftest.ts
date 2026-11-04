import {readFileSync} from 'node:fs';
import {resolve as resolveSourcePath} from 'node:path';
import {startSteamWallpaperHost} from '../src/bridge/steamWallpaperHost';
import {strict as assert} from 'node:assert';

import {SharedGameMedia as ProductionSharedGameMedia,wallpaperTargetKey,gameMediaPairComplete,MediaInventoryScan,reconcileMediaAliases,type GameMediaEntry} from '../src/bridge/sharedGameMedia';

import {effectiveGameMediaRange,DEFAULT_GAME_MEDIA_PLAYBACK} from '../src/bridge/gameMediaPlayback';

import {parseHlsAsset} from '../src/bridge/steamMediaPlaylist';

import {GameMediaRevisionFence} from '../src/bridge/gameMediaRevisionFence';

import {GameMediaPreparationPolicy} from '../src/bridge/gameMediaPreparationPolicy';

// Original preparation scenarios explicitly supply a current native admission
// at their IO boundary. New ticket negatives below use the unwrapped class.
const steamTicket=(current=()=>true)=>({demandConsumer:'steam' as const,demandEpoch:'native-focus-1',demandRevision:1,current});
class SharedGameMedia extends ProductionSharedGameMedia {
  override ensure(target:Parameters<ProductionSharedGameMedia['ensure']>[0],consumer:Parameters<ProductionSharedGameMedia['ensure']>[1],priority=false,completePair=false,demand?:Parameters<ProductionSharedGameMedia['ensure']>[4]){return super.ensure(target,consumer,priority,completePair,consumer==='steam'?(demand||steamTicket()):demand);}
}
const stabilityRecoveryOnly=process.argv.includes('--stability-recovery');
let cases=0,checks=0;

function equal(actual:unknown,expected:unknown,message:string){checks++;assert.deepEqual(actual,expected,message);}

async function foregroundDemandCases(){
 for(const invalid of [undefined,steamTicket(()=>false),{...steamTicket(),demandEpoch:''},{...steamTicket(),demandRevision:-1}]){
  const calls:string[]=[];const owner=new ProductionSharedGameMedia({enabled:()=>true,invoke:(async command=>{calls.push(command);return {key:'steam-570',bindingRevision:1,status:'missing'};}) as any});
  await assert.rejects(()=>owner.ensure({appId:570,name:'Fixture',verified:true},'steam',true,true,invalid),/steam-foreground-required/);checks++;equal(calls.length,0,'missing/withdrawn native ticket causes zero IPC');owner.stop();
 }
 await run('Steam demand revoked during lookup cannot enter background queue',async()=>{let active=true,release!:(value:any)=>void;const calls:any[]=[];const owner=new ProductionSharedGameMedia({enabled:()=>true,backgroundEnabled:()=>true,invoke:(async(command,args)=>{calls.push({command,args});if(command==='gameMedia.resolve')return new Promise(r=>release=r);throw Error('revoked lookup entered '+command);}) as any});const pending=owner.ensure({appId:570,name:'Fixture',verified:true},'steam',true,true,steamTicket(()=>active));await Promise.resolve();active=false;release({key:'steam-570',bindingRevision:1,status:'missing'});await assert.rejects(()=>pending,/steam-demand-withdrawn/);checks++;equal(calls.length,1,'only already-issued local lookup remains');owner.stop();});
 await run('Steam current ticket reaches lookup and native background without altering other consumers',async()=>{let active=true;const calls:any[]=[];const owner=new ProductionSharedGameMedia({enabled:()=>true,backgroundEnabled:()=>true,invoke:(async(command,args)=>{calls.push({command,args});return command==='gameMedia.resolve'?{key:'steam-570',bindingRevision:1,status:'missing'}:{};}) as any});await owner.ensure({appId:570,name:'Fixture',verified:true},'steam',true,true,steamTicket(()=>active));equal(calls.map(x=>[x.args.demandConsumer,x.args.demandEpoch,x.args.demandRevision]),[['steam','native-focus-1',1],['steam','native-focus-1',1]],'both Steam boundaries retain native ticket');active=false;calls.length=0;await owner.ensure({appId:570,name:'Fixture',verified:true},'ymcc',true,true);equal(calls.some(x=>x.args.demandConsumer==='steam'),false,'YMCC demand has independent ownership');equal(calls.map(x=>x.command),['gameMedia.resolve','gameMedia.background'],'YMCC lookup and supplementation remain functional');calls.length=0;await owner.ensure({appId:570,name:'Fixture',verified:true},'library',true,true);equal(calls.some(x=>x.args.demandConsumer==='steam'),false,'library demand has independent ownership');owner.stop();});
 await run('late Steam failure receipt cannot schedule retries or announce after demand withdrawal',async()=>{let active=true,release!:(value:any)=>void;let notices=0;const timers:any[]=[];const owner=new ProductionSharedGameMedia({schedule:((fn:any)=>{timers.push(fn);return timers.length;}) as any,cancel:(()=>{}) as any,invoke:(async()=>new Promise(r=>release=r)) as any});owner.subscribe(()=>notices++);const pending=owner.playbackFailed({key:'steam-570',assetId:'good',bindingRevision:1},steamTicket(()=>active));active=false;release({key:'steam-570',bindingRevision:2,status:'error',retryAt:Date.now()+60000});await pending;equal(timers.length,0,'old demand cannot arm a retry');equal(notices,0,'old demand cannot trigger fresh consumer reads');owner.stop();});
 await run('Steam late playback failure cannot quarantine a current shared asset',async()=>{const calls:any[]=[];const owner=new ProductionSharedGameMedia({invoke:(async(command,args)=>{calls.push({command,args});return {};}) as any});await assert.rejects(()=>owner.playbackFailed({key:'steam-570',assetId:'good',bindingRevision:1},steamTicket(()=>false)),/steam-demand-withdrawn/);checks++;equal(calls.length,0,'withdrawn failure never enters Store.edit');owner.stop();});
}
function fixture(options:{enabled?:boolean;candidateFailure?:boolean;crossLease?:boolean;pair?:boolean;legacy?:boolean;install?:(args:any)=>Promise<void>}={}){

 const entries=new Map<number,GameMediaEntry>();const counts={resolve:0,begin:0,metadata:0,install:0,fail:0,legacy:0};const order:number[]=[];const timers:Array<()=>void>=[];let enabled=options.enabled??true;

 const owner=new SharedGameMedia({nativeBackground:false,enabled:()=>enabled,now:()=>100000,candidates:async target=>{counts.metadata++;if(options.candidateFailure)throw Error('offline');return {name:target.name,urls:['https://video.steamstatic.com/'+target.appId+'.mp4'],poster:options.pair?'https://shared.steamstatic.com/'+target.appId+'.jpg':undefined};},installed:async()=>[],schedule:((fn:()=>void)=>{timers.push(fn);return timers.length;}) as any,cancel:(()=>{}) as any,invoke:(async(command:string,args:any={})=>{

  const id=Number(args.appId)||Number(String(args.key).split('-')[1]);let entry=entries.get(id);

  if(command==='gameMedia.resolve'){counts.resolve++;return entry?{...entry}:{key:'steam-'+id,status:'missing',bindingRevision:0,ready:false};}

  if(command==='gameMedia.begin'){counts.begin++;if(options.crossLease)return {key:'steam-'+id,status:'pending',bindingRevision:1,taskId:'other-process',acquired:false,leaseUntil:200000};entry={...(entry||{key:'steam-'+id,appId:id,bindingRevision:1,status:'missing'}),taskId:'job-'+id,taskSlots:options.pair?['static','dynamic']:['dynamic'],acquired:true,status:'pending'};entries.set(id,entry);return {...entry};}

  if(command==='gameMedia.legacy'){counts.legacy++;if(!options.legacy)return {};const current=entries.get(id)!;entry={...current,ready:true,status:'ready',bindingRevision:current.bindingRevision+1,dynamicReady:true,url:'http://fixture/legacy-'+id,taskSlots:(current.taskSlots||[]).filter(slot=>slot!=='dynamic')};entries.set(id,entry);return {...entry};}

  if(command==='gameMedia.install'){counts.install++;order.push(id);await options.install?.(args);const current=entries.get(id)!;if(current.bindingRevision!==args.bindingRevision)throw Error('media-stale-publication');const kind=/\.jpg$/.test(args.source)?'image':'video';const slot=kind==='image'?'static':'dynamic';const asset={id:'asset-'+id+'-'+slot,revision:'1',kind,extension:kind==='image'?'jpg':'mp4',mime:kind==='image'?'image/jpeg':'video/mp4',bytes:100} as const;entry={...current,ready:true,status:'ready',bindingRevision:current.bindingRevision+1,url:'http://fixture/'+id,asset,assets:{...current.assets,[slot]:asset},[slot+'Ready']:true};entries.set(id,entry);return {...entry};}

  if(command==='gameMedia.finish'){entry={...entries.get(id)!,videoUnavailableUntil:args.videoUnavailable?999999999:0,staticUnavailableUntil:args.staticUnavailable?999999999:0};entries.set(id,entry);return {...entry};}

  if(command==='gameMedia.fail'){counts.fail++;const current=entries.get(id)!;if(current.bindingRevision===args.bindingRevision)entries.set(id,{...current,status:'error',retryAt:200000,error:args.reason});return {...entries.get(id)!};}

  if(command==='gameMedia.snapshot')return {entries:Object.fromEntries([...entries.values()].map(e=>[e.key,e]))};

  if(command==='gameMedia.inventory')return [];

  throw Error('unhandled '+command);

 }) as any});return {owner,entries,counts,order,timers,setEnabled:(value:boolean)=>{enabled=value;}};

}

const target=(id:number)=>({appId:id,name:'Fixture '+id,verified:true});

const cached=(id:number):GameMediaEntry=>({key:'steam-'+id,appId:id,bindingRevision:4,status:'ready',ready:true,url:'http://fixture/'+id});

const reports:any[]=[];
async function run(name:string,fn:()=>Promise<void>|void){if(stabilityRecoveryOnly&&!name.startsWith('R18 recovery:'))return;try{await fn();reports.push({name,ok:true});console.log('PASS '+name);}catch(error){reports.push({name,ok:false,error:String(error instanceof Error?error.message:error)});console.log('FAIL '+name);}cases++;}


async function lookupCoalescingCases(){
 const fixtures:any[]=[],callCounts:Record<string,number>={};
 const value=(revision=1)=>({key:'steam-570',bindingRevision:revision,status:'ready',ready:true,asset:{id:'asset-'+revision,revision:'1',kind:'image',mime:'image/png',extension:'png',bytes:12},assets:{static:{id:'asset-'+revision,revision:'1',kind:'image',mime:'image/png',extension:'png',bytes:12}},playback:{start:8,end:38,fit:'cover'}} as GameMediaEntry);
 const flush=async()=>{for(let i=0;i<8;i++)await Promise.resolve();};
 const fixture=()=>{
  const calls:any[]=[],pending:any[]=[],tasks:Promise<any>[]=[];
  const owner=new ProductionSharedGameMedia({enabled:()=>true,backgroundEnabled:()=>true,invoke:((command:string,args:any)=>{calls.push({command,args});return new Promise((resolve,reject)=>pending.push({command,resolve,reject}));}) as any});
  const f:any={owner,calls,pending,tasks};fixtures.push(f);return f;
 };
 const one=(f:any,target:any={appId:570},demand?:any)=>{let result:Promise<GameMediaEntry>;try{result=Promise.resolve(f.owner.lookup(target,demand));}catch(error){result=Promise.reject(error);}void result.catch(()=>{});f.tasks.push(result);return result;};
 const release=(f:any,revision=1)=>{for(const pending of f.pending.splice(0))pending.resolve(JSON.parse(JSON.stringify(value(revision))));};
 const scenario=async(name:string,fn:(f:any)=>Promise<void>)=>{const at=process.argv.indexOf('--lookup-case');if(at>=0&&process.argv[at+1]!==name)return;await run(name,async()=>{const f=fixture();try{await fn(f);}finally{f.owner.stop();release(f);await Promise.allSettled(f.tasks);callCounts[name]=f.calls.length;}});};
 await scenario('lookup: 200 simultaneous identical reads share one original native request',async f=>{
  const tasks=Array.from({length:200},()=>one(f));release(f);const entries=await Promise.all(tasks);equal(entries.length,200,'all consumers receive their own response');equal(f.calls.length,1,'one native resolve for two hundred overlapping readers');equal(f.calls[0].command,'gameMedia.resolve','the original RPC is used');equal((f.owner as any).lookupFlights?.size,0,'settled flight is not retained');
 });
 await scenario('lookup: settled result is never a response cache',async f=>{const first=one(f);release(f,1);equal((await first).bindingRevision,1,'first observation');const second=one(f);release(f,2);equal((await second).bindingRevision,2,'later lookup gets a new observation');equal(f.calls.length,2,'settled reads always dispatch again');});
 await scenario('lookup: each consumer owns nested response values',async f=>{const a=one(f),b=one(f);release(f);const [x,y]=await Promise.all([a,b]);x.asset!.id='changed';x.assets!.static!.id='changed-nested';x.playback!.start=99;equal(y.asset!.id,'asset-1','top asset isolated');equal(y.assets!.static!.id,'asset-1','nested asset isolated');equal(y.playback!.start,8,'nested playback isolated');equal(f.calls.length,1,'isolation does not require duplicate native work');});
 await scenario('lookup: top-level request key order does not duplicate work',async f=>{const a=one(f,{appId:570,name:'A'}),b=one(f,{name:'A',appId:570});release(f);await Promise.all([a,b]);equal(f.calls.length,1,'same serialized argument values share one request');});
 await scenario('lookup: different target and binding metadata remain separate',async f=>{const tasks=[one(f,{appId:570,key:'steam-570',bindingRevision:1}),one(f,{appId:570,key:'steam-570',bindingRevision:2}),one(f,{appId:730,key:'steam-730',bindingRevision:1})];release(f);await Promise.all(tasks);equal(f.calls.length,3,'identity and caller metadata do not merge');});
 await scenario('lookup: nested request metadata differences remain separate',async f=>{const tasks=[one(f,{appId:570,selection:{url:'https://example.invalid/a'}}),one(f,{appId:570,selection:{url:'https://example.invalid/b'}})];release(f);await Promise.all(tasks);equal(f.calls.length,2,'all nested native arguments participate');});
 await scenario('lookup: Steam epochs revisions and ordinary readers never share authority',async f=>{const tasks=[one(f,{appId:570},steamTicket()),one(f,{appId:570},{...steamTicket(),demandEpoch:'new-focus'}),one(f,{appId:570},{...steamTicket(),demandRevision:2}),one(f,{appId:570})];release(f);await Promise.all(tasks);equal(f.calls.length,4,'different native authority creates independent reads');});
 await scenario('lookup: equal ticket bytes still validate each caller after response',async f=>{let active=true;const a=one(f,{appId:570},steamTicket(()=>active)),b=one(f,{appId:570},steamTicket());active=false;release(f);await assert.rejects(()=>a,/steam-demand-withdrawn/);checks++;equal((await b).ready,true,'other current ticket remains usable');equal(f.calls.length,1,'identical native ticket values share only transport work');});
 await scenario('lookup: already withdrawn demand sends zero native requests',async f=>{const a=one(f,{appId:570},steamTicket(()=>false));await assert.rejects(()=>a,/steam-demand-withdrawn/);checks++;equal(f.calls.length,0,'denied before IPC');});
 await scenario('lookup: event invalidation and old completion preserve a replacement flight',async f=>{
  const a=one(f);f.owner.changed('asset','steam-570');const b=one(f);equal(f.calls.length,2,'post-change read does not share pre-change query');f.pending.shift().resolve(value(1));await a;const c=one(f);equal(f.calls.length,2,'old cleanup cannot remove the replacement flight');release(f,2);equal((await b).bindingRevision,2,'new query owns current result');equal((await c).bindingRevision,2,'later overlapping read shares replacement');
 });
 await scenario('lookup: raw native event can invalidate without listener fanout',async f=>{let notifications=0;f.owner.subscribe(()=>notifications++);const a=one(f);f.owner.invalidateLookups();const b=one(f);release(f);await Promise.all([a,b]);equal(f.calls.length,2,'raw invalidation opens new read');equal(notifications,0,'invalidation itself creates no demand event');});
 await scenario('lookup: rejected transport is removed and a later call retries',async f=>{const a=one(f);f.pending.shift().reject(Error('fixture-read-failed'));await assert.rejects(()=>a,/fixture-read-failed/);checks++;const b=one(f);release(f,2);equal((await b).bindingRevision,2,'next query can recover');equal(f.calls.length,2,'failure is not cached');});
 await scenario('lookup: stop rejects both a late response and future queries',async f=>{const a=one(f);f.owner.stop();release(f);await assert.rejects(()=>a,/media-owner-stopped/);checks++;const b=one(f);release(f);await assert.rejects(()=>b,/media-owner-stopped/);checks++;equal(f.calls.length,1,'no new native query after stop');equal((f.owner as any).lookupFlights?.size,0,'stop releases all sharing records');const inactive=await f.owner.ensure({appId:570,name:'Fixture',verified:true},'library');equal(inactive.status,'consumer-inactive','stopped ensure completes inertly under the original contract');equal(f.calls.length,1,'inert ensure creates no query or admission');});
 await scenario('lookup: bounded map dispatches every overflow request',async f=>{const tasks=Array.from({length:80},(_,i)=>one(f,{appId:1000+i}));const retained=(f.owner as any).lookupFlights?.size;tasks.push(one(f,{appId:1079}));release(f);const responses=await Promise.all(tasks);equal(responses.length,81,'overflow is not dropped');equal(f.calls.length,81,'untracked overflow dispatches independently');equal(retained,64,'retained in-flight keys have a fixed bound');equal((f.owner as any).lookupFlights?.size,0,'settling overflow leaves no records');});
 await scenario('lookup: synchronous transport exceptions do not poison later reads',async f=>{let count=0;const owner=new ProductionSharedGameMedia({invoke:((command:string)=>{equal(command,'gameMedia.resolve','original command');if(++count===1)throw Error('fixture-sync-failure');return Promise.resolve(value(2));}) as any});try{let first:Promise<any>;try{first=Promise.resolve(owner.lookup({appId:570}));}catch(error){first=Promise.reject(error);}await assert.rejects(()=>first,/fixture-sync-failure/);checks++;equal((await owner.lookup({appId:570})).bindingRevision,2,'second read recovers');equal(count,2,'two actual boundary calls');}finally{owner.stop();}});
 await scenario('lookup: two ordinary ensure callers retain one native admission',async f=>{const target={appId:570,name:'Fixture',verified:true};const a=f.owner.ensure(target,'ymcc',true,true),b=f.owner.ensure(target,'library',true,true);void a.catch(()=>{});void b.catch(()=>{});f.tasks.push(a,b);for(const pending of f.pending.splice(0))pending.resolve({key:'steam-570',bindingRevision:1,status:'missing'});await flush();const native=f.calls.filter((x:any)=>x.command==='gameMedia.background').length;release(f);await Promise.all([a,b]);equal(f.calls.filter((x:any)=>x.command==='gameMedia.resolve').length,1,'one common lookup');equal(native,1,'original background admission sharing preserved');});
  await scenario('lookup: replaced flight survives old transport rejection',async f=>{
   const old=one(f),rejected=assert.rejects(()=>old,/fixture-old-transport-failed/);void rejected.catch(()=>{});f.tasks.push(rejected);
   f.owner.invalidateLookups();const current=one(f);
   f.pending.shift().reject(Error('fixture-old-transport-failed'));await rejected;checks++;
   const joined=one(f);equal(f.calls.length,2,'old rejection cannot remove replacement flight');
   release(f,2);equal((await Promise.all([current,joined])).map(entry=>entry.bindingRevision),[2,2],'replacement readers receive the new result');
  });
  await scenario('lookup: tracked flight survives earlier overflow completion',async f=>{
   const occupied=Array.from({length:64},(_,i)=>one(f,{appId:1000+i}));
   const oldA=one(f,{appId:1079}),oldB=one(f,{appId:1079});
   equal(f.calls.length,66,'full map dispatches both overflow reads independently');
   f.pending.shift().resolve(value(1));await occupied[0];await flush();
   const current=one(f,{appId:1079});
   for(const pending of f.pending.splice(occupied.length-1,2))pending.resolve(value(1));
   await Promise.all([oldA,oldB]);await flush();
   const joined=one(f,{appId:1079});equal(f.calls.length,67,'old overflow cannot remove replacement flight');
   release(f,2);equal((await Promise.all([current,joined])).map(entry=>entry.bindingRevision),[2,2],'tracked replacement readers receive the new result');
   await Promise.all(occupied);
  });
 return callCounts;
}
if(process.argv.includes('--lookup-coalescing-only')){
 const invocationCounts=await lookupCoalescingCases();
 const selectedOk=reports.length>0&&reports.every(row=>row.ok);
 console.log(JSON.stringify({ok:selectedOk,cases,checks,reports,invocationCounts,selection:'lookup-coalescing-only',defaultGroups:'NOT_RUN',realNetworkOrApplication:false}));
 process.exit(selectedOk?0:1);
}

await run('cache hit has no network or task',async()=>{const f=fixture({candidateFailure:true});f.entries.set(1,cached(1));const e=await f.owner.ensure(target(1),'ymcc');equal(e.url,'http://fixture/1','cached source returned');equal(f.counts.metadata,0,'no external lookup');equal(f.counts.begin,0,'no prepare request');f.owner.stop();});

await run('two consumers share one preparation',async()=>{const f=fixture();const [a,b]=await Promise.all([f.owner.ensure(target(1),'steam'),f.owner.ensure(target(1),'ymcc')]);equal(a.url,b.url,'same completed source');equal(f.counts.metadata,1,'one metadata lookup');equal(f.counts.install,1,'one download');f.owner.stop();});

await run('another process lease never downloads twice',async()=>{const f=fixture({crossLease:true});await f.owner.ensure(target(1),'steam');equal(f.counts.install,0,'not lease owner');equal(f.counts.metadata,0,'no competing lookup');equal(f.timers.length,1,'bounded lease retry');f.owner.stop();});

await run('manual protection and deletion remain protected',async()=>{const f=fixture();f.entries.set(1,{...cached(1),manual:true});f.entries.set(2,{key:'steam-2',bindingRevision:5,status:'deleted',deleted:true,manual:true});await f.owner.ensure(target(1),'steam');await f.owner.ensure(target(2),'ymcc');equal(f.counts.begin,0,'protected records not mutated');equal(f.counts.install,0,'deleted video not resurrected');f.owner.stop();});

await run('retry backoff blocks repeated card refreshes',async()=>{const f=fixture();f.entries.set(1,{key:'steam-1',bindingRevision:1,status:'error',retryAt:150000});await Promise.all([f.owner.ensure(target(1),'steam'),f.owner.ensure(target(1),'steam')]);equal(f.counts.metadata,0,'cooldown no network');equal(f.timers.length,1,'one retry wake');f.owner.stop();});

await run('failed metadata persists retry and drains queue',async()=>{const f=fixture({candidateFailure:true});await f.owner.ensure(target(1),'steam');equal(f.counts.install,0,'no bad download');equal(f.counts.fail,1,'single failure record');equal(f.owner.inspect().tasks,0,'job released');equal(f.owner.inspect().queued,0,'queue drained');f.owner.stop();});

await run('disabled Steam queue does not suppress YMCC demand',async()=>{let release!:()=>void;const gate=new Promise<void>(yes=>{release=yes;});const f=fixture({install:async args=>{if(args.appId===1)await gate;}});const first=f.owner.ensure(target(1),'ymcc');await new Promise(yes=>setTimeout(yes,0));const queued=f.owner.ensure(target(2),'steam');const shared=f.owner.ensure(target(2),'ymcc');f.setEnabled(false);release();await Promise.all([first,queued,shared]);equal(f.order,[1,2],'YMCC owns the shared queued task');f.owner.stop();});

await run('focused game promotes an existing background task',async()=>{let release!:()=>void;const gate=new Promise<void>(yes=>{release=yes;});const f=fixture({install:async args=>{if(args.appId===1)await gate;}});const first=f.owner.ensure(target(1),'ymcc');await new Promise(yes=>setTimeout(yes,0));const a=f.owner.ensure(target(2),'steam');const b=f.owner.ensure(target(3),'steam');await new Promise(yes=>setTimeout(yes,0));const priority=f.owner.ensure(target(3),'steam',true);await new Promise(yes=>setTimeout(yes,0));release();await Promise.all([first,a,b,priority]);equal(f.order,[1,3,2],'current selection precedes background');equal(f.counts.install,3,'no duplicate priority task');f.owner.stop();});

await run('finished fMP4 resolves relative media segments',()=>{const plan=parseHlsAsset('#EXTM3U\n#EXT-X-MAP:URI="init.mp4"\n#EXTINF:5,\nseg1.m4s\n#EXT-X-ENDLIST','https://video.steamstatic.com/movie/main.m3u8');equal(plan.segments,['https://video.steamstatic.com/movie/init.mp4','https://video.steamstatic.com/movie/seg1.m4s'],'local assembly plan');});

await run('HLS defaults to 480p when no 720p variant exists',()=>{const plan=parseHlsAsset('#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=3000000,RESOLUTION=1920x1080\n1080.m3u8\n#EXT-X-STREAM-INF:BANDWIDTH=1000000,RESOLUTION=854x480\n480.m3u8','https://video.steamstatic.com/master.m3u8');equal(plan.variant,'https://video.steamstatic.com/480.m3u8','uses the next preferred automatic quality');});

await run('unfinished and encrypted playlists are rejected',()=>{for(const text of ['#EXTM3U\nseg.ts','#EXTM3U\n#EXT-X-KEY:METHOD=AES-128\n#EXT-X-ENDLIST','#EXTM3U\n#EXT-X-BYTERANGE:10@0\n#EXT-X-ENDLIST']){checks++;assert.throws(()=>parseHlsAsset(text,'https://video.steamstatic.com/a.m3u8'));}});

await run('dual collection publishes both slots under one metadata lease',async()=>{const f=fixture({pair:true});const entry=await f.owner.ensure(target(1),'steam',true,true);equal(f.counts.metadata,1,'one metadata lookup');equal(f.counts.install,2,'two different variants');equal(Object.keys(entry.assets||{}).sort(),['dynamic','static'],'pair retained');await f.owner.ensure(target(1),'ymcc',true,true);equal(f.counts.install,2,'complete pair is offline cache hit');f.owner.stop();});

await run('failed static still prepares dynamic and records bounded retry',async()=>{const f=fixture({pair:true,install:async args=>{if(/\.jpg$/.test(args.source))throw Error('static offline');}});const entry=await f.owner.ensure(target(1),'ymcc',true,true);equal(entry.dynamicReady,true,'video preserved despite failed image');equal(f.counts.install,2,'both attempts happened');equal(f.counts.fail,1,'partial failure recorded');equal(entry.status,'error','partial cache not misreported complete');f.owner.stop();});

await run('failed dynamic preserves the prepared static fallback',async()=>{const f=fixture({pair:true,install:async args=>{if(/\.mp4$/.test(args.source))throw Error('video offline');}});const entry=await f.owner.ensure(target(1),'steam',true,true);equal(entry.staticReady,true,'static retained');equal(entry.ready,true,'usable fallback remains');equal(entry.status,'error','backoff persisted');f.owner.stop();});

await run('Steam switch off cancels queued dual collection',async()=>{const f=fixture({enabled:false,pair:true});await f.owner.ensure(target(1),'steam',false,true);equal(f.counts.begin,0,'no acquisition while off');equal(f.counts.metadata,0,'no external requests');f.owner.stop();});

await run('intro skip and requested length respect actual clip duration',()=>{equal(effectiveGameMediaRange(42.917875,DEFAULT_GAME_MEDIA_PLAYBACK),{start:8,end:38},'recommended8s plus30s');equal(effectiveGameMediaRange(42.917875,{start:8,end:68}),{start:8,end:42.917875},'60s limited by actual remaining clip');equal(effectiveGameMediaRange(2.1,DEFAULT_GAME_MEDIA_PLAYBACK),{start:0,end:2.1},'short clip plays from beginning');equal(effectiveGameMediaRange(43,{start:8,end:0}),{start:8,end:43},'full selection means remaining video');equal(effectiveGameMediaRange(Infinity,DEFAULT_GAME_MEDIA_PLAYBACK),{start:0,end:0},'metadata not loaded does not seek');});

await run('different unresolved EXEs never collapse onto AppID zero',()=>{const a={appId:0,executable:'G:/Games/A.exe'},b={appId:0,executable:'G:/Games/B.exe'};equal(wallpaperTargetKey(a)!==wallpaperTargetKey(b),true,'two local files have separate queue identities');equal(wallpaperTargetKey(a),wallpaperTargetKey({appId:0,executable:'g:\\games\\a.EXE'}),'same normalized file coalesces');});

await run('unresolved and unverified numeric hints use file identity before paired preparation',async()=>{for(const hint of [0,999]){

 const records=new Map<string,GameMediaEntry>(),aliases=new Map<string,string>();let metadata=0,identified=0,downloaded=0;

 const resolveKey=(args:any)=>{const key=args.key||wallpaperTargetKey(args);return aliases.get(key)||key;};

 const owner=new SharedGameMedia({nativeBackground:false,enabled:()=>true,installed:async()=>[],candidates:async target=>{equal(target.appId,570,'only verified file result reaches media metadata');metadata++;return {name:'Dota 2',urls:['https://video.steamstatic.com/570.mp4'],poster:'https://shared.steamstatic.com/570.jpg'};},invoke:(async(command:string,args:any={})=>{

  const key=resolveKey(args),entry=records.get(key)||{key,bindingRevision:1,status:'missing',ready:false};

  if(command==='gameMedia.resolve')return {...entry};

  if(command==='gameMedia.begin'){const next={...entry,key,executable:args.executable,appId:args.appId,taskId:'file-job',taskSlots:['static','dynamic'],acquired:true};records.set(key,next);return {...next};}

  if(command==='gameMedia.identify'){identified++;return {appId:570,name:'Dota 2',verified:true};}

  if(command==='gameMedia.bind'){const before=wallpaperTargetKey({appId:0,executable:args.executable}),existing=records.get(before)!;aliases.set(before,key);records.delete(before);const next={...existing,key,appId:args.appId,bindingRevision:existing.bindingRevision+1};records.set(key,next);return {...next};}

  if(command==='gameMedia.install'){const slot=/jpg$/.test(args.source)?'static':'dynamic';equal(args.bindingRevision,entry.bindingRevision,'lease follows identity merge and both publications');downloaded++;const next={...entry,ready:true,status:'ready',url:'http://fixture/570',bindingRevision:entry.bindingRevision+1,[slot+'Ready']:true};records.set(key,next);return {...next};}

  if(command==='gameMedia.finish')return {...entry};

  if(command==='gameMedia.fail')throw Error(args.reason);

  throw Error('unhandled '+command);

 }) as any});

 const target={appId:hint,verified:false,name:'file only',executable:'G:/Games/Dota.exe'};await Promise.all([owner.ensure(target,'steam',true,true),owner.ensure(target,'library',true,true)]);equal(identified,1,'one file identification');equal(metadata,1,'one official metadata fetch');equal(downloaded,2,'both variants prepared');equal(records.size,1,'one canonical record');owner.stop();

}});

await run('manual invalid video remains protected while static cache is usable',async()=>{const f=fixture({pair:true});const saved={...cached(1),staticReady:true,dynamicReady:false,manualSlots:{dynamic:true}};f.entries.set(1,saved);equal(gameMediaPairComplete(saved,100000),true,'manual slot is satisfied without automatic replacement');await f.owner.ensure(target(1),'steam',true,true);equal(f.counts.metadata,0,'no network retry against protected video');equal(f.counts.begin,0,'no pointless prepare lease');f.owner.stop();});

await run('manual edit interrupting preparation schedules one shared reconciliation',async()=>{let f:ReturnType<typeof fixture>;let edited=false;f=fixture({pair:true,install:async()=>{if(!edited){edited=true;const current=f.entries.get(1)!;f.entries.set(1,{...current,bindingRevision:current.bindingRevision+1,taskId:'',leaseUntil:0,ready:true,status:'ready',staticReady:true,manualSlots:{static:true}});throw Error('media-stale-publication');}}});const reasons:string[]=[];f.owner.subscribe(reason=>reasons.push(reason));const result=await f.owner.ensure(target(1),'steam',true,true);equal(result.staticReady,true,'latest user static edit retained');equal(result.status,'ready','stale task never turns manual edit into error');equal(f.timers.length,1,'one deferred reconciliation');equal(f.owner.inspect().tasks,0,'interrupted job released');f.timers[0]();equal(reasons.filter(reason=>reason==='retry').length,1,'single shared retry event');f.owner.stop();});

await run('pair completeness honors unavailable TTL and independent protected slots',()=>{equal(gameMediaPairComplete({...cached(1),staticReady:true,videoUnavailableUntil:200000},100000),true,'bounded unavailable marker avoids repeated queries');equal(gameMediaPairComplete({...cached(1),staticReady:true,videoUnavailableUntil:90000},100000),false,'expired marker permits requested refresh');equal(gameMediaPairComplete({...cached(1),manualSlots:{static:true},deletedSlots:{dynamic:true}},100000),true,'independent protection avoids automatic resurrection');});

await run('verified legacy bytes reuse shared queue without metadata or download',async()=>{const f=fixture({legacy:true});const result=await f.owner.ensure({...target(1),legacyTarget:{pid:123,processCreated:'original'}},'ymcc',true,true);equal(result.url,'http://fixture/legacy-1','legacy source installed');equal(f.counts.legacy,1,'single shared migration');equal(f.counts.metadata,0,'no metadata for completed cached variant');equal(f.counts.install,0,'no download for known local bytes');f.owner.stop();});

await run('legacy video only collects missing static counterpart',async()=>{const f=fixture({legacy:true,pair:true});const result=await f.owner.ensure({...target(1),legacyTarget:{pid:123,processCreated:'original'}},'ymcc',true,true);equal(f.counts.legacy,1,'one video migration');equal(f.counts.install,1,'only static download');equal(result.staticReady&&result.dynamicReady,true,'both variants usable');f.owner.stop();});

await run('automatic current-game demand delegates to native worker rather than bypassing resource gates',async()=>{const submitted:any[]=[];const owner=new SharedGameMedia({backgroundEnabled:()=>true,invoke:(async(command:string,args:any={})=>{if(command==='gameMedia.resolve')return {key:'steam-570',bindingRevision:0,status:'missing'};if(command==='gameMedia.background'){submitted.push(args);return {};}throw Error('automatic request leaked into foreground '+command);}) as any,candidates:async()=>{throw Error('renderer must not access network');}});const result=await owner.ensure({appId:570,name:'Fixture',verified:true},'steam',true,true);equal(result.status,'missing','missing cache returns immediately for event-driven publication');equal(submitted.length,1,'single native queue submission');equal(submitted[0].targets[0].appId,570,'original verified target retained');equal(owner.inspect().tasks,0,'renderer has no duplicate automatic queue');owner.stop();});

await run('inventory-only update delegates to the library worker without a Steam rescan or renderer download',async()=>{let installed=0;const queued:any[]=[];const owner=new SharedGameMedia({nativeBackground:false,enabled:()=>true,backgroundEnabled:()=>true,installed:async()=>{installed++;return [{appId:570,name:'Known Steam',lastPlayed:1234,gameDirectory:'G:/Steam/Dota',verified:true}];},invoke:(async(command:string,args:any={})=>{if(command==='gameMedia.background'){queued.push(...args.targets);equal('enabled' in args,false,'inventory cannot overwrite the settings owner');return {};}return command==='gameMedia.inventory'?[{appId:0,name:'Steam-only shortcut',executable:'G:/Games/New.exe',verified:false}]:{entries:{'steam-570':{key:'steam-570',appId:570,name:'stored',identityVerified:true,bindingRevision:1,status:'missing'}}};}) as any});owner.ensure=(async()=>{throw Error('renderer must not own background downloads');}) as any;await owner.supplement(false);equal(installed,0,'library signal does not enumerate appmanifests again');equal(queued.some(value=>value.appId===0&&value.executable==='G:/Games/New.exe'),true,'unknown identity stays with native local identity admission');queued.length=0;await owner.supplement();equal(installed,1,'full scan includes installed Steam');equal(queued.find(value=>value.appId===570).gameDirectory,'G:/Steam/Dota','stored record cannot erase installed directory');equal(queued.find(value=>value.appId===570).lastPlayed,1234,'recent-play evidence survives inventory merging');equal(queued.find(value=>value.appId===570).sourceTier,1,'native Steam tier survives stored/library merge');equal(queued.find(value=>value.appId===0).sourceTier,2,'YMCC confirmation tier retained for native admission');owner.stop();});

await run('inventory bursts coalesce and retain events arriving during a scan',async()=>{let id=0,enabled=true;const timers=new Map<number,{fn:()=>void;delay:number}>(),reads:boolean[]=[];let release!:()=>void;let first=true;const wait=new Promise<void>(yes=>release=yes);const scan=new MediaInventoryScan(async full=>{reads.push(full);if(first){first=false;await wait;}},()=>enabled,{schedule:((fn:()=>void,delay:number)=>{timers.set(++id,{fn,delay});return id;}) as any,cancel:((key:number)=>timers.delete(key)) as any});const tick=async()=>{await Promise.resolve();await Promise.resolve();};const fire=async()=>{const [key,value]=timers.entries().next().value!;timers.delete(key);value.fn();await tick();};for(let n=0;n<30;n++)scan.request();equal(timers.size,1,'one burst timer');scan.request(true);equal([...timers.values()][0].delay,0,'full scan takes precedence');await fire();equal(reads,[true],'first scan full');for(let n=0;n<20;n++)scan.request();scan.request(true);equal(timers.size,0,'no parallel native scan');release();await tick();equal(timers.size,1,'one follow-up retained');await fire();equal(reads,[true,true],'change during first scan not lost');equal(timers.size,0,'no idle timer');enabled=false;scan.request();equal(timers.size,0,'disabled never wakes inventory');enabled=true;scan.request();scan.stop();equal(timers.size,0,'shutdown cancels pending inventory');});

await run('late cached response cannot roll back a newer edit or identity merge',()=>{const fence=new GameMediaRevisionFence();fence.observe('steam-570',5);equal(fence.outdated({mediaKey:'steam-570',mediaBindingRevision:4}),true,'new edit observed before cached response rejects stale output');equal(fence.outdated({mediaKey:'steam-570',mediaBindingRevision:5}),false,'current version allowed');fence.observe('steam-570',3);equal(fence.outdated({mediaKey:'steam-570',mediaBindingRevision:4}),true,'late lower event cannot erase known revision');fence.observe('exe-old',undefined,'steam-570');equal(fence.outdated({mediaKey:'exe-old',mediaBindingRevision:999}),true,'merged identity re-resolves even if old revision was larger');equal(fence.outdated({mediaKey:'unknown',mediaBindingRevision:1}),false,'unknown independent cache is not blocked');});

await run('revision history overflow fences in-flight reads without retaining an unbounded map',()=>{const fence=new GameMediaRevisionFence(2),read=fence.capture();fence.observe('steam-1',5);fence.observe('steam-2',1);fence.observe('steam-3',1);equal(fence.outdated({mediaKey:'steam-1',mediaBindingRevision:4,mediaReadEpoch:read}),true,'evicted known edit cannot allow an older pending read');equal(fence.outdated({mediaKey:'steam-1',mediaBindingRevision:5,mediaReadEpoch:fence.capture()}),false,'new local read after burst is allowed without polling');});



await run('background backlog yields to a running game while cached playback and current target remain available',async()=>{

 const f=fixture();f.owner.setPreparationState({backgroundBlocked:true});const queued=f.owner.ensure(target(1),'steam',false);await new Promise(resolve=>setImmediate(resolve));

 equal(f.counts.begin,0,'background does no identity or network work while paused');equal(f.owner.inspect().queued,1,'pending work is retained');

 f.entries.set(2,cached(2));equal((await f.owner.ensure(target(2),'steam',true)).ready,true,'cached target never waits for the paused queue');

 const current=await f.owner.ensure(target(3),'steam',true);equal(current.ready,true,'visible uncached target can prepare');equal(f.order,[3],'only current demand ran');

 f.owner.setPreparationState({backgroundBlocked:false});await queued;equal(f.order,[3,1],'background resumes once game/power allows');f.owner.stop();

});

await run('promoting an already queued game starts it without waiting for a power event',async()=>{

 const f=fixture();f.owner.setPreparationState({backgroundBlocked:true});const first=f.owner.ensure(target(1),'steam');await new Promise(resolve=>setImmediate(resolve));

 const promoted=await f.owner.ensure(target(1),'steam',true);await first;equal(promoted.ready,true,'same deferred job becomes interactive');equal(f.counts.begin,1,'no duplicate preparation lease');f.owner.stop();

});

await run('storage pressure pauses new work without blocking existing media or discarding requests',async()=>{

 const f=fixture();f.owner.setPreparationState({storageBlocked:true});const queued=f.owner.ensure(target(1),'library',true);await new Promise(resolve=>setImmediate(resolve));

 equal(f.counts.metadata,0,'even explicit missing media waits for disk space');f.entries.set(2,cached(2));equal((await f.owner.ensure(target(2),'ymcc',true)).ready,true,'disk check never gates local playback');

 f.owner.setPreparationState({storageBlocked:false});await queued;equal(f.counts.install,1,'queued request resumes exactly once');f.owner.stop();

});

await run('disabling Steam settles a deferred backlog without starting downloads',async()=>{

 const f=fixture();f.owner.setPreparationState({backgroundBlocked:true,storageBlocked:true});const pending=f.owner.ensure(target(1),'steam');await new Promise(resolve=>setImmediate(resolve));

 f.setEnabled(false);f.owner.reconcilePreparation();await pending;equal(f.counts.begin,0,'disabled pending request never acquires a lease');equal(f.owner.inspect().queued,0,'disabled queue is retired');f.owner.stop();

});

await run('a running bounded download completes but no next background job starts after game activation',async()=>{

 let release!:()=>void;const pending=new Promise<void>(yes=>release=yes);const f=fixture({install:async()=>pending});const first=f.owner.ensure(target(1),'steam');await new Promise(resolve=>setImmediate(resolve));

 const next=f.owner.ensure(target(2),'steam');await new Promise(resolve=>setImmediate(resolve));f.owner.setPreparationState({backgroundBlocked:true});release();await first;

 equal(f.counts.begin,1,'next game remains deferred');f.owner.setPreparationState({backgroundBlocked:false});await next;equal(f.order,[1,2],'original request resumes once');f.owner.stop();

});

function policyFixture(read:()=>Promise<any>=async()=>({})){

 let id=0,queued=0;const timers=new Map<number,{fn:()=>void;delay:number}>(),states:any[]=[];

 const policy=new GameMediaPreparationPolicy({read,apply:state=>states.push(state),queued:()=>queued,schedule:((fn:()=>void,delay:number)=>{timers.set(++id,{fn,delay});return id;}) as any,cancel:((key:number)=>timers.delete(key)) as any});

 const fire=async(delay:number)=>{const row=[...timers].find(([,v])=>v.delay===delay);if(!row)throw Error('missing expected policy timer '+delay);timers.delete(row[0]);row[1].fn();await new Promise(resolve=>setImmediate(resolve));};

 return {policy,timers,states,fire,setQueued:(n:number)=>queued=n};

}

await run('preparation policy coalesces event bursts and has no idle timer',async()=>{

 let reads=0;const f=policyFixture(async()=>{reads++;return {};});for(let n=0;n<100;n++)f.policy.request();equal(f.timers.size,1,'one coalesced native read');await f.fire(250);equal(reads,1,'one filesystem snapshot');equal(f.timers.size,0,'no resident policy polling');

 f.policy.game(true);equal(f.states.at(-1),{backgroundBlocked:true,storageBlocked:false,reason:'game-running'},'existing game signal applies synchronously');f.policy.game(false);equal(f.states.at(-1).backgroundBlocked,false,'existing exit signal releases backlog');equal(reads,1,'game changes do not scan disks');f.policy.stop();

});

await run('disk retry exists only for retained blocked demand and disappears after recovery',async()=>{

 let blocked=true;const f=policyFixture(async()=>({blockedReason:blocked?'media-disk-space-low':''}));f.setQueued(2);f.policy.request();await f.fire(250);equal([...f.timers.values()].map(v=>v.delay),[20000],'twenty second retry while retained disk-blocked requests exist');

 for(let n=0;n<50;n++)f.policy.demand();equal(f.timers.size,1,'no duplicate retry timers');blocked=false;await f.fire(20000);await f.fire(250);equal(f.states.at(-1).storageBlocked,false,'space recovery releases queue');equal(f.timers.size,1,'retained work keeps one slow health check');f.setQueued(0);f.policy.demand();equal(f.timers.size,0,'completed queue removes health timer');f.policy.stop();

});

await run('power saver retains one slow health check only while demand exists and idle capacity failure stays dormant',async()=>{

 const f=policyFixture(async()=>({batterySaver:true}));f.policy.request();await f.fire(250);equal(f.states.at(-1),{backgroundBlocked:true,storageBlocked:false,reason:'battery-saver'},'saver does not falsely claim full disk');f.setQueued(4);f.policy.demand();equal(f.timers.size,1,'retained saver pause gets one slow fallback for lost power events');f.policy.stop();

 const bad=policyFixture(async()=>{throw Error('transport disconnected');});bad.policy.request();await bad.fire(250);equal(bad.states.at(-1).storageBlocked,true,'unknown capacity never starts uncapped work');equal(bad.timers.size,0,'idle IPC failure does not spin');bad.setQueued(1);bad.policy.demand();equal(bad.timers.size,1,'pending failed admission gets one retry');bad.setQueued(0);bad.policy.demand();equal(bad.timers.size,0,'last demand removal cancels retry');bad.policy.stop();

});

await run('policy shutdown ignores late native responses and cancels pending timers',async()=>{

 let release!:(value:any)=>void;const waiting=new Promise(yes=>release=yes);const f=policyFixture(async()=>waiting);f.setQueued(1);f.policy.request();await f.fire(250);const before=f.states.length;f.policy.request();f.policy.stop();release({});await new Promise(resolve=>setImmediate(resolve));equal(f.states.length,before,'late policy response cannot restart a stopped owner');equal(f.timers.size,0,'shutdown has no pending timers');

});



await run('sleep suppresses pending preparation reads and resumes with fresh capacity evidence',async()=>{

 const f=policyFixture();f.setQueued(2);f.policy.request();f.policy.suspend();equal(f.timers.size,0,'sleep cancels planned reads');equal(f.states.at(-1).reason,'media-power-transition','sleep does not present full disk');f.policy.demand();f.policy.request();equal(f.timers.size,0,'sleep has no queue retry timer');

 f.policy.resume();equal(f.states.at(-1).storageBlocked,true,'resume cannot use stale pre-sleep admission');await f.fire(250);equal(f.states.at(-1).storageBlocked,false,'fresh resume response releases pending media');equal(f.timers.size,1,'healthy resume retains a single slow check for the backlog');f.setQueued(0);f.policy.demand();equal(f.timers.size,0,'finished backlog removes the slow check');f.policy.stop();

});



await run('a pre-sleep capacity response cannot authorize post-resume work',async()=>{

 let release!:(value:any)=>void,reads=0;const pending=new Promise(yes=>release=yes);const f=policyFixture(async()=>++reads===1?pending:{});f.setQueued(1);f.policy.request();await f.fire(250);f.policy.suspend();f.policy.resume();release({});await new Promise(resolve=>setImmediate(resolve));equal(f.states.at(-1).storageBlocked,true,'old response is fenced by the power generation');await f.fire(250);equal(f.states.at(-1).storageBlocked,false,'only the fresh response releases post-wake queue');f.policy.stop();

});



await run('leaving a visible card downgrades its queued task while another consumer keeps its demand',async()=>{

 const f=fixture();f.owner.setPreparationState({backgroundBlocked:true,storageBlocked:true});const old=f.owner.ensure(target(1),'steam',true),next=f.owner.ensure(target(2),'steam',true);await new Promise(resolve=>setImmediate(resolve));

 f.owner.selectSteamTarget(target(2));f.owner.setPreparationState({storageBlocked:false});await next;equal(f.order,[2],'the prior card cannot retain interactive quota');equal(f.owner.inspect().queued,1,'prior card remains as deferred background work');

 f.owner.selectSteamTarget(null);f.owner.setPreparationState({backgroundBlocked:false});await old;equal(f.order,[2,1],'leaving a card does not lose normal background demand');f.owner.stop();

 const shared=fixture();shared.owner.setPreparationState({backgroundBlocked:true,storageBlocked:true});const steam=shared.owner.ensure(target(3),'steam',true);await new Promise(resolve=>setImmediate(resolve));const own=shared.owner.ensure(target(3),'ymcc',true);await new Promise(resolve=>setImmediate(resolve));shared.owner.selectSteamTarget(null);shared.owner.setPreparationState({storageBlocked:false});await Promise.all([steam,own]);equal(shared.counts.install,1,'YMCC ownership survives Steam card departure without duplication');shared.owner.stop();

});



await run('default media clocks keep the browser receiver required by WebView2',async()=>{

 const nativeSet=globalThis.setTimeout,nativeClear=globalThis.clearTimeout;let serial=0;const scheduled:number[]=[],cancelled:number[]=[];

 try{

  globalThis.setTimeout=function(this:unknown,...args:any[]){if(this!==globalThis)throw Error('Illegal invocation');scheduled.push(Number(args[1]));return ++serial;} as any;

  globalThis.clearTimeout=function(this:unknown,timer:any){if(this!==globalThis)throw Error('Illegal invocation');cancelled.push(timer);} as any;

  const policy=new GameMediaPreparationPolicy({read:async()=>({}),apply:()=>{},queued:()=>0});policy.request();policy.stop();

  const inventory=new MediaInventoryScan(async()=>{},()=>true);inventory.request();inventory.stop();

  const owner=new SharedGameMedia({nativeBackground:false,now:()=>1,invoke:(async()=>({key:'steam-570',bindingRevision:1,status:'error',retryAt:60001})) as any});await owner.ensure(target(570),'steam');owner.stop();

  equal(scheduled,[250,250,60000],'policy, inventory and retry all use receiver-safe defaults');equal(cancelled.length,3,'all three default cancellation paths retain the browser receiver');

 }finally{globalThis.setTimeout=nativeSet;globalThis.clearTimeout=nativeClear;}

});

await run('automatic inventory never re-enrolls cache-only popular games',async()=>{let snapshots=0;const queued:any[]=[];const owner=new SharedGameMedia({nativeBackground:true,enabled:()=>true,backgroundEnabled:()=>true,installed:async()=>[{appId:570,name:'Installed',lastPlayed:123,verified:true}],invoke:(async(command:string,args:any={})=>{if(command==='gameMedia.inventory')return[{appId:620,name:'Confirmed local',lastPlayed:456,verified:true}];if(command==='gameMedia.snapshot'){snapshots++;return{entries:{'steam-730':{appId:730,name:'Old popular-only cache',identityVerified:true}}};}if(command==='gameMedia.background'){queued.push(...args.targets);return{};}throw Error(command);}) as any});await owner.supplement();equal(snapshots,0,'shared cache is not enumerated as a user game catalog');equal(queued.map(x=>x.appId).sort((a,b)=>a-b),[570,620],'only installed and local inventory targets are submitted');owner.stop();});
await run('viewing a card does not manufacture a last-played timestamp',async()=>{const submitted:any[]=[];const owner=new SharedGameMedia({nativeBackground:true,now:()=>999999,enabled:()=>true,backgroundEnabled:()=>true,invoke:(async(command:string,args:any={})=>{if(command==='gameMedia.resolve')return{key:'steam-570',status:'missing',ready:false,bindingRevision:1};if(command==='gameMedia.background'){submitted.push(...args.targets);return{};}throw Error(command);}) as any});await owner.ensure({appId:570,name:'Viewed',verified:true,lastPlayed:123},'steam',true,true);equal(submitted.length,1,'missing current game still enters native queue');equal(submitted[0].lastPlayed,123,'actual play record survives display demand');owner.stop();});

await run('playback failure has a finite native deadline and propagates unknown outcome',async()=>{
 const calls:any[]=[],timers:any[]=[];let failed=true,notifications=0;
 const owner=new SharedGameMedia({schedule:((fn:any,ms:number)=>{timers.push({fn,ms});return timers.length;}) as any,cancel:(()=>{}) as any,invoke:(async(command:string,args:any,options:any)=>{calls.push({command,args,options});if(failed)throw Error('inert media IPC timeout');return{key:'steam-570',bindingRevision:2,status:'error',ready:false,retryAt:100010};}) as any,now:()=>100000});const off=owner.subscribe(()=>notifications++);
 try{const report={key:'steam-570',assetId:'old-asset',bindingRevision:1,kind:'video'};await assert.rejects(owner.playbackFailed(report),/inert media IPC timeout/);
  equal(calls[0].options?.timeoutMs,5000,'native failure report cannot remain pending forever');equal(calls[0].args,{key:'steam-570',assetId:'old-asset',bindingRevision:1,slot:'dynamic',action:'playback-failed'},'deadline retains exact asset revision and native CAS action');equal(notifications,0,'unknown result is not announced as a committed edit');equal(timers.length,0,'media wrapper never blindly retries unknown write');
  failed=false;const result=await owner.playbackFailed(report);equal(result.bindingRevision,2,'actual receipt supplies the committed revision');equal(notifications,1,'confirmed edit notifies consumers once');equal(timers.length,1,'confirmed native retryAt retains existing preparation path');
 }finally{off();owner.stop();}
});

await run('stopped media owner cannot arm retry from a late failure receipt',async()=>{
 let finish!:(value:GameMediaEntry)=>void,calls=0,notifications=0;const timers=new Map<number,()=>void>();const owner=new SharedGameMedia({invoke:(async()=>{calls++;return new Promise(resolve=>finish=resolve);}) as any,now:()=>100000,schedule:((fn:()=>void)=>{timers.set(1,fn);return 1;}) as any,cancel:((id:number)=>timers.delete(id)) as any});owner.subscribe(()=>notifications++);
 const report={key:'steam-570',assetId:'old',bindingRevision:1,kind:'video'},pending=owner.playbackFailed(report);owner.stop();finish({key:'steam-570',bindingRevision:2,status:'error',retryAt:200000});await pending;equal(timers.size,0,'late receipt cannot schedule retry after stop');equal(notifications,0,'stopped owner does not announce a late mutation');await assert.rejects(owner.playbackFailed(report),/media-owner-stopped/);equal(calls,1,'stopped owner cannot start another cache write');
});

const admissionSamples:any[]=[];
function nativeDemandFixture(){
 let entry:GameMediaEntry={key:'steam-570',appId:570,bindingRevision:1,status:'missing',ready:false},resolveCalls=0;const pending:Array<{args:any;resolve:(x?:any)=>void;reject:(e:Error)=>void}>=[],timers:any[]=[],commands:string[]=[];
 const owner=new SharedGameMedia({nativeBackground:true,enabled:()=>true,backgroundEnabled:()=>true,now:()=>100000,installed:async()=>[],schedule:((fn:any,ms:any)=>{timers.push({fn,ms});return timers.length;}) as any,cancel:(()=>{}) as any,invoke:((command:string,args:any={})=>{commands.push(command);if(command==='gameMedia.resolve'){resolveCalls++;return Promise.resolve({...entry});}if(command==='gameMedia.background')return new Promise((resolve,reject)=>pending.push({args,resolve,reject}));throw Error('unexpected renderer preparation '+command);}) as any});
 return{owner,pending,timers,commands,setEntry:(x:Partial<GameMediaEntry>)=>entry={...entry,...x},resolveCalls:()=>resolveCalls,stop:()=>{owner.stop();for(const p of pending)p.resolve({});}};
}
const admissionTick=()=>new Promise<void>(resolve=>setImmediate(resolve));
await run('identical native demand burst has one in-flight enrollment',async()=>{
 const f=nativeDemandFixture();try{const work=Array.from({length:80},(_,n)=>f.owner.ensure(n%3===0?{verified:true,name:'Same',appId:570,executable:undefined}:n%2?{verified:true,name:'Same',appId:570}:{appId:570,name:'Same',verified:true},'steam',true,true));await admissionTick();admissionSamples.push({name:'80-same-target',resolveCalls:f.resolveCalls(),nativeSubmissions:f.pending.length});equal(f.pending.length,1,'same Steam ticket enrollment is sent once independent of target property order');for(const p of f.pending)p.resolve({});const returned=await Promise.all(work);equal(returned.length,80,'every display caller receives its own native cache result');equal(returned.every(x=>x.bindingRevision===1),true,'coalescing never invents a completed asset');equal(f.timers.length,0,'enrollment does not schedule renderer retry');}finally{f.stop();}
});
await run('different native provenance and play records never share enrollment',async()=>{
 const f=nativeDemandFixture();try{const inputs=[{...target(570),executable:'G:/games/a.exe',steamAccount:'a',shortcutAppId:'1'},{...target(570),executable:'G:/games/b.exe',steamAccount:'a',shortcutAppId:'1'},{...target(570),executable:'G:/games/a.exe',steamAccount:'b',shortcutAppId:'2'},{...target(570),lastPlayed:99},{...target(571)},{...target(570),legacyTarget:{pid:42,processCreated:'first-birth'}},{...target(570),legacyTarget:{pid:42,processCreated:'second-birth'}}];const work=inputs.map(x=>f.owner.ensure(x,'library',false,true));await admissionTick();equal(f.pending.length,inputs.length,'different EXE/account/play record/appID is preserved');equal(f.pending.map(x=>x.args.targets[0]),inputs,'native payload retains every exact target');for(const p of f.pending)p.resolve({});await Promise.all(work);}finally{f.stop();}
});
await run('new media binding does not join an older native enrollment',async()=>{
 const f=nativeDemandFixture();try{const old=f.owner.ensure(target(570),'steam',false,true);await admissionTick();f.setEntry({bindingRevision:2,refreshRequested:true});const fresh=f.owner.ensure(target(570),'library',true,true);await admissionTick();equal(f.pending.length,2,'new binding must submit independently');f.pending[0].reject(Error('old result unavailable'));await old;const joined=f.owner.ensure(target(570),'ymcc',true,true);await admissionTick();equal(f.pending.length,2,'settling old revision must not release the current in-flight admission');f.pending[1].resolve({});const values=await Promise.all([fresh,joined]);equal(values.map(x=>x.bindingRevision),[2,2],'new revision callers retain their own lookup receipt');}finally{f.stop();}
});
await run('failed native enrollment retires without retry timers or a poisoned cache',async()=>{
 const f=nativeDemandFixture();try{const work=[f.owner.ensure(target(570),'steam'),f.owner.ensure(target(570),'steam'),f.owner.ensure(target(570),'steam')];await admissionTick();equal(f.pending.length,1,'concurrent same-ticket failure shares a single native attempt');f.pending[0].reject(Error('native return lost'));const values=await Promise.all(work);equal(values.every(x=>x.ready===false),true,'failed enrollment retains missing-cache truth');equal(f.timers.length,0,'optional enqueue failure must not create a retry timer');const retry=f.owner.ensure(target(570),'steam');await admissionTick();equal(f.pending.length,2,'later explicit demand is allowed after failure');f.pending[1].resolve({});await retry;}finally{f.stop();}
});
await run('completed native enrollment is not cached across later demand',async()=>{
 const f=nativeDemandFixture();try{const first=f.owner.ensure(target(570),'steam');await admissionTick();f.pending[0].resolve({});await first;const next=f.owner.ensure(target(570),'steam');await admissionTick();equal(f.pending.length,2,'only in-flight work is shared, not a later still-missing-cache request');f.pending[1].resolve({});await next;f.setEntry({ready:true,dynamicReady:true,staticReady:true,status:'ready'});const ready=await f.owner.ensure(target(570),'steam',true,true);equal(ready.ready,true,'complete cache is usable');equal(f.pending.length,2,'already complete wallpaper creates no new enrollment');}finally{f.stop();}
});
await run('stopping native demand preserves accepted work without followup',async()=>{
 const f=nativeDemandFixture();try{const work=[f.owner.ensure(target(570),'steam'),f.owner.ensure(target(570),'steam')];await admissionTick();equal(f.pending.length,1,'accepted same-ticket native work has one submission');let finished=false;void Promise.all(work).then(()=>finished=true);f.owner.stop();await admissionTick();equal(finished,false,'stop cannot invent completion for an accepted native promise');f.pending[0].resolve({});await Promise.all(work);await f.owner.ensure(target(570),'library');equal(f.pending.length,1,'stopped owner cannot enqueue followup');equal(f.timers.length,0,'stopped native owner leaves no retry timer');}finally{f.stop();}
});
await run('independent consumers never join a revocable Steam admission',async()=>{const f=nativeDemandFixture();try{const work=[f.owner.ensure(target(570),'steam'),f.owner.ensure(target(570),'ymcc'),f.owner.ensure(target(570),'library'),f.owner.ensure(target(570),'steam')];await admissionTick();equal(f.pending.length,2,'one captured Steam ticket and one independent admission remain separate');equal(f.pending.filter(x=>x.args.demandConsumer==='steam').length,1,'Steam same-ticket callers still coalesce');equal(f.pending.filter(x=>x.args.demandConsumer!=='steam').length,1,'YMCC and library retain their independent shared owner');for(const p of f.pending)p.resolve({});await Promise.all(work);equal(f.timers.length,0,'separate ownership creates no polling');}finally{f.stop();}});
if(!stabilityRecoveryOnly)await foregroundDemandCases();
await stabilityRecoveryCases();
// Reuse the shipping refresh body and its retry subscription. Remove only its
// one erased TypeScript call annotation; do not model the dispatch algorithm.
async function stabilityRecoveryCases(){
 const sourcePath=process.env.YMCC_SHARED_RETRY_SOURCE||resolveSourcePath('src/bridge/sharedGameMedia.ts');
 const source=readFileSync(sourcePath,'utf8'),begin=source.indexOf('  async function refresh(){'),end=source.indexOf("  const off=on('gameMedia.changed'",begin);
 assert.ok(begin>=0&&end>begin,'exact production refresh function boundaries');
 let body=source.slice(begin,end);const annotation='invoke<{entries:Record<string,GameMediaEntry>;aliases?:Record<string,string>}>';
 assert.equal(body.split(annotation).length,2,'one TS call annotation in actual refresh');body=body.replace(annotation,'invoke');
 const retryAt=source.indexOf("  const retry=sharedGameMedia.subscribe(reason=>",end),retryEnd=source.indexOf('\n',retryAt);assert.ok(retryAt>=end&&retryEnd>retryAt,'actual retry subscription boundary');
 const makeRefresh=new Function('env','with(env){'+body+source.slice(retryAt,retryEnd)+';return {refresh,stop:retry};}');
 const flush=async()=>{for(let n=0;n<4;n++)await new Promise<void>(r=>setImmediate(r));};
 function harness(consumer:string|undefined,options:{steam?:boolean;ymcc?:boolean;background?:boolean;requested?:boolean;attachHost?:boolean;deadline?:boolean}={}){
  let clock=Date.now(),id=0;const settings={steam:options.steam??true,ymcc:options.ymcc??true,background:options.background??true};const calls:any[]=[],ensures:any[]=[],timers=new Map<number,{fn:()=>void;ms:number}>();
  let record:GameMediaEntry={key:'steam-570',appId:570,name:'Current fixture',bindingRevision:4,status:'error',ready:false,retryAt:options.deadline===false?0:clock+1000,refreshRequested:options.requested===true,...(consumer===undefined?{}:{demandConsumer:consumer})};
  const schedule=((fn:()=>void,ms:number)=>{timers.set(++id,{fn,ms});return id;}) as any,cancel=((timer:number)=>timers.delete(timer)) as any;
  const owner=new ProductionSharedGameMedia({enabled:()=>settings.steam,backgroundEnabled:()=>settings.background,nativeBackground:true,now:()=>clock,schedule,cancel,invoke:(async(command:string,args:any)=>{calls.push({command,args});if(command==='gameMedia.resolve')return {...record};if(command==='gameMedia.background')return {};throw Error('unexpected owner command '+command);}) as any});
  const actualEnsure=owner.ensure.bind(owner);owner.ensure=((...args:any[])=>{ensures.push(args);return (actualEnsure as any)(...args);}) as any;
  const env:any={disposed:false,refreshing:false,refreshAgain:false,revisions:new Map([['steam-570',4]]),aliases:new Map(),reconcileMediaAliases,sharedGameMedia:owner,steamWallpaperEnabled:()=>settings.steam,backgroundDownloadEnabled:()=>settings.background,manualAllowed:()=>false,getUiSetting:()=>settings.ymcc,gameMediaPairComplete,window:{dispatchEvent(){}},CustomEvent:globalThis.CustomEvent,Date:{now:()=>clock},scan(){},invoke:async(command:string)=>{calls.push({command});if(command!=='gameMedia.snapshot')throw Error(command);return {entries:{'steam-570':{...record}},aliases:{}};}};
  const reader=makeRefresh(env);let host:ReturnType<typeof startSteamWallpaperHost>|null=null,extension:any,foreground:(value:any)=>void=()=>{};
  if(options.attachHost){host=startSteamWallpaperHost({enabled:()=>settings.steam,readForeground:async()=>({source:'native-steam-bigpicture-foreground',known:true,foreground:true,current:true,mode:'bigpicture',pid:77,birth:'12345',window:'789',ownerEpoch:'live-retry',revision:1}),onForeground:handler=>{foreground=handler;return()=>{};},onResume:()=>()=>{},inventory:()=>{},appearance:()=>({opacity:.64,blur:0}),media:owner,now:()=>clock,schedule,cancel,register:value=>{extension=value;return()=>{};},invoke:(async(command:string)=>{if(command==='deckySidebar.mirrorReply')return true;throw Error(command);}) as any});host.ready();extension.connected({connection:'1',runId:'a'.repeat(32)});extension.request({connection:'1',runId:'a'.repeat(32),request:{command:'wallpaper.context',contextSeq:1,appId:'570',visible:true}});}
  return {owner,reader,calls,ensures,timers,settings,env,foreground,async fire(){const next=[...timers][0];assert.ok(next,'retained failure needs a deadline timer');timers.delete(next[0]);clock+=next[1].ms;next[1].fn();await flush();},setRecord(value:Partial<GameMediaEntry>){record={...record,...value};},async settle(){await flush();},stop(){host?.stop();reader.stop();owner.stop();}};
 }
 for(const consumer of ['library','ymcc',undefined])await run('R18 recovery: '+(consumer||'legacy library')+' keeps independent identity through failure deadline',async()=>{const f=harness(consumer,{steam:false});try{await f.reader.refresh();equal(f.timers.size,1,'independent owner retains one deadline with Steam OFF');equal(f.ensures.length,0,'no preparation before its deadline');await f.fire();equal(f.ensures.length,1,'deadline submits one current independent retry');equal(f.ensures[0][1],consumer==='ymcc'?'ymcc':'library','library/legacy never become ticketless Steam');equal(f.calls.filter(x=>x.command==='gameMedia.background').length,1,'one native background admission');equal(f.calls.some(x=>x.args?.demandConsumer==='steam'),false,'independent request never borrows a Steam ticket');equal(f.timers.size,0,'no duplicate deadline timer');}finally{f.stop();}});
 await run('R18 recovery: global refresh never manufactures a Steam retry ticket',async()=>{const f=harness('steam');try{await f.reader.refresh();equal(f.timers.size,1,'retained Steam failure announces one shared deadline');await f.fire();equal(f.ensures.length,0,'global refresh must leave Steam retry to selected Host');equal(f.calls.filter(x=>x.command==='gameMedia.background').length,0,'global refresh cannot submit a ticketless Steam request');}finally{f.stop();}});
 await run('R18 recovery: real Shared owner and Host share one deadline and one current-ticket retry',async()=>{const f=harness('steam',{attachHost:true});try{await f.reader.refresh();await f.settle();equal(f.timers.size,1,'Host and global refresh share one existing retry owner');equal(f.ensures.length,0,'failed resource waits for actual deadline');await f.fire();equal(f.ensures.length,1,'only current Host owns ensure at deadline');equal(f.ensures[0][1],'steam','retry keeps Steam consumer');equal([f.ensures[0][4].demandEpoch,f.ensures[0][4].demandRevision],['live-retry',1],'retry captures current native proof');equal(f.calls.filter(x=>x.command==='gameMedia.background').length,1,'one target admission, not global plus Host');equal(f.timers.size,0,'completed attempt leaves no duplicate retry timer');}finally{f.stop();}});
 await run('R18 recovery: expired deadline cannot recreate Steam demand after native blur',async()=>{const f=harness('steam',{attachHost:true});try{await f.reader.refresh();await f.settle();f.foreground({source:'native-steam-bigpicture-foreground',known:true,foreground:false,current:false,mode:'bigpicture',pid:77,birth:'12345',window:'789',ownerEpoch:'blur-retry',revision:2});await f.settle();await f.fire();equal(f.ensures.length,0,'blurred Host never retries from global deadline');equal(f.calls.filter(x=>x.command==='gameMedia.background').length,0,'blurred deadline cannot submit background work');}finally{f.stop();}});
 await run('R18 recovery: explicit library refresh and disabled independent owners retain their own gates',async()=>{const manual=harness('steam',{steam:false,requested:true,deadline:false});try{await manual.reader.refresh();equal(manual.ensures.length,1,'explicit library request is preserved');equal(manual.ensures[0][1],'library','explicit edit uses independent library consumer');}finally{manual.stop();}for(const [consumer,settings] of [['ymcc',{steam:false,ymcc:false}],['library',{steam:false,background:false}],['background',{steam:true}],['unrecognized',{steam:true}]] as const){const f=harness(consumer,{...settings,deadline:false});try{await f.reader.refresh();equal(f.ensures.length,0,'disabled/unknown owner has no implicit fallback authorization');equal(f.timers.size,0,'disabled/unknown owner has no retry clock');}finally{f.stop();}}});
}

const ok=reports.every(row=>row.ok);if(!ok)process.exitCode=1;
console.log(JSON.stringify({ok,selection:stabilityRecoveryOnly?'stability-recovery':'default',cases,checks,reports,admissionSamples,scope:'production preparation queue and HLS planner; transport fixture only'}));

