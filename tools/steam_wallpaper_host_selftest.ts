import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import {strict as assert} from 'node:assert';
import {startSteamWallpaperHost,resolveWallpaperTarget,steamWallpaperForegroundAllowed} from '../src/bridge/steamWallpaperHost';
import type {GameMediaEntry} from '../src/bridge/sharedGameMedia';
import type {MirrorPeer} from '../src/bridge/deckyMirrorRelay';
import {subscribeGameMediaResume} from '../src/bridge/gameMediaPreparationPolicy';
import {getSharedPowerLifecycle,onSharedPowerLifecycle} from '../src/bridge/powerLifecycleState';
import {MirrorClient} from '../decky-plugin/src/mirrorClient';
const caseArgs=process.argv.filter(arg=>arg.startsWith('--case='));
assert.ok(caseArgs.length<=1&&(!caseArgs.length||caseArgs[0].length>7&&!process.argv.includes('--stability-recovery')),'one nonempty exact case without recovery mode');
const exactCase=caseArgs.length?caseArgs[0].slice(7):null;
const stabilityRecoveryOnly=process.argv.includes('--stability-recovery');const stabilityRecoveryRows:Array<{name:string;ok:boolean;error?:string}>=[];
let cases=0,checks=0;const same=(a:unknown,b:unknown,label:string)=>{checks++;assert.deepEqual(a,b,label);};
const settle=()=>new Promise<void>(resolve=>setImmediate(resolve));
const entry=(id=570,revision=1):GameMediaEntry=>({key:'steam-'+id,appId:id,bindingRevision:revision,status:'ready',ready:true,asset:{id:'asset-'+id,revision:String(revision),kind:'video',extension:'mp4',mime:'video/mp4',bytes:120},url:'http://fixture/'+id,playback:{start:8,end:38,fit:'cover'}});
const pairedEntry=(id=570,revision=1):GameMediaEntry=>({...entry(id,revision),staticReady:true,dynamicReady:true});
const nativeProof=(extra:any={})=>({source:'native-steam-bigpicture-foreground',known:true,foreground:true,current:true,mode:'bigpicture',pid:77,birth:'12345',window:'789',ownerEpoch:'focus-1',revision:1,reason:'verified',...extra});
function fixture(options:{foreground?:unknown;foregroundRead?:()=>Promise<unknown>;startReady?:boolean;lookup?:(target:any)=>Promise<GameMediaEntry>;ensure?:(target:any)=>Promise<GameMediaEntry>;shortcut?:any;shortcutRead?:()=>Promise<any>;nativePower?:boolean;playbackFailed?:(value:any)=>Promise<GameMediaEntry>;reply?:(message:any,options:any)=>Promise<boolean>}={}){
 let foregroundChanged:(value:unknown)=>void=()=>{};
 let extension:any,changed:(reason:string,key?:string)=>void=()=>{},resumed:(event:{generation?:number})=>void=()=>{},enabled=true;let opacity=.64;let current=entry();
 let timerId=0;const timers=new Map<number,{fn:()=>void;ms:number}>();const frames:any[]=[],calls:any[]=[],failures:any[]=[];const peer:MirrorPeer={connection:'1',runId:'a'.repeat(32)};
 const host=startSteamWallpaperHost({...({schedule:(fn:()=>void,ms:number)=>{timers.set(++timerId,{fn,ms});return timerId;},cancel:(id:number)=>timers.delete(id)} as any),...(!options.nativePower?{onResume:(handler:(event:{generation?:number})=>void)=>{resumed=handler;return()=>{resumed=()=>{};};}}:{}),readForeground:async()=>{calls.push({command:'foreground-read'});return options.foregroundRead?options.foregroundRead():options.foreground===undefined?nativeProof():options.foreground;},onForeground:handler=>{foregroundChanged=handler;return()=>{foregroundChanged=()=>{};};},enabled:()=>enabled,inventory:()=>{calls.push({command:'inventory'});},appearance:()=>({opacity,blur:0}),register:(value:any)=>{extension=value;return()=>extension=null;},invoke:(async(command:string,args:any,invokeOptions:any)=>{calls.push({command,args,options:invokeOptions});if(command==='deckySidebar.mirrorReply'){frames.push(args.message);return options.reply?options.reply(args.message,invokeOptions):true;}if(command==='gameMedia.shortcut')return options.shortcutRead?options.shortcutRead():options.shortcut||{executable:'G:/Games/local.exe',name:'Local Game',shortcutAppId:'4131894616',steamAccount:'17'};throw Error(command);}) as any,media:{selectSteamTarget:target=>{calls.push({command:'selectSteamTarget',target});},lookup:async (target,demand)=>{calls.push({command:'lookup',target,demand});return options.lookup?options.lookup(target):{...current};},ensure:async (target,_consumer,_priority,_complete,demand)=>{calls.push({command:'ensure',target,demand});return options.ensure?options.ensure(target):{...current};},subscribe:fn=>{changed=fn;return()=>{changed=()=>{};};},playbackFailed:async value=>{failures.push(value);return options.playbackFailed?options.playbackFailed(value):{...current};}}});
 const context=(id='570',seq=1,visible=true,other={})=>extension.request({...peer,...other,request:{command:'wallpaper.context',runId:peer.runId,contextSeq:seq,appId:id,name:'Fixture',visible,...(other as any).request}});
 if(options.startReady!==false)host.ready();extension.connected(peer);
 return {host,frames,calls,failures,context,peer,timers,foreground:(value:unknown)=>foregroundChanged(value),fire:async()=>{const next=[...timers][0];assert.ok(next,'missing wallpaper delivery recovery timer');timers.delete(next[0]);next[1].fn();await settle();},resume:(generation:number)=>resumed({generation}),status:(args:any)=>extension.request({...peer,request:{command:'wallpaper.status',runId:peer.runId,...args}}),change:(reason='asset',key?:string)=>changed(reason,key),connect:(value=peer)=>extension.connected(value),enable:(value:boolean)=>{enabled=value;},appearance:(value:number)=>{opacity=value;},setEntry:(value:GameMediaEntry)=>{current=value;},output:()=>frames.filter(f=>f.type==='wallpaper'&&f.contextSeq>0)};
}
async function run(name:string,fn:()=>Promise<void>){if(exactCase!==null&&name!==exactCase)return;if(stabilityRecoveryOnly&&!name.startsWith('R18 recovery:'))return;if(!stabilityRecoveryOnly){await fn();cases++;console.log('PASS '+name);return;}try{await fn();stabilityRecoveryRows.push({name,ok:true});console.log('PASS '+name);}catch(error){stabilityRecoveryRows.push({name,ok:false,error:String(error instanceof Error?error.message:error)});console.log('FAIL '+name);}cases++;}
// These cases exercise native foreground admission, separately from the old
// player DOM-focus checks. No native reader defaults to an affirmative proof.
await run('native foreground unknown/minimized/overlay/desktop never starts media queries',async()=>{
 for(const proof of [null,nativeProof({known:false}),nativeProof({foreground:false,reason:'minimized'}),nativeProof({foreground:false,reason:'overlay'}),nativeProof({mode:'desktop'}),nativeProof({current:false}),nativeProof({source:'process-exists'}),nativeProof({window:'0'}),nativeProof({birth:'0'}),nativeProof({ownerEpoch:''})]){const f=fixture({foreground:proof});try{f.context();await settle();await settle();same(f.calls.filter(c=>['lookup','ensure','inventory'].includes(c.command)).length,0,'unproved actual Big Picture foreground performs no media work');same(f.output().some(x=>x.entry),false,'unknown never returns a playable entry');}finally{f.host.stop();}}
});
await run('stable native foreground grants one query and repeating proof stays inert',async()=>{const f=fixture();try{f.context();await settle();await settle();same(f.calls.filter(c=>c.command==='lookup').length,1,'first current foreground owns one lookup');for(let n=0;n<100;n++)f.foreground(nativeProof());await settle();same(f.calls.filter(c=>c.command==='lookup').length,1,'same foreground proof cannot restart reads');const demand=f.calls.find(c=>c.command==='lookup').demand;same([demand.demandConsumer,demand.demandEpoch,demand.demandRevision],['steam','focus-1',1],'exact captured foreground ticket reaches native media');}finally{f.host.stop();}});
await run('native blur revokes pending media and recovery reads current selection only once',async()=>{let release!:(v:GameMediaEntry)=>void;const f=fixture({lookup:()=>new Promise(r=>release=r)});try{f.context();await settle();f.foreground(nativeProof({foreground:false,ownerEpoch:'focus-2',revision:2}));release(entry());await settle();same(f.calls.filter(c=>c.command==='ensure').length,0,'blurred lookup cannot enter preparation');same(f.output().some(x=>x.entry),false,'late old lookup cannot deliver video');f.foreground(nativeProof());await settle();same(f.calls.filter(c=>c.command==='lookup').length,1,'retired native epoch cannot restart reads');f.setEntry(entry(570,2));f.foreground(nativeProof({ownerEpoch:'focus-3',revision:3}));await settle();same(f.calls.filter(c=>c.command==='lookup').length,2,'current focus restores once');release(entry(570,2));await settle();same(f.output().at(-1).entry.bindingRevision,2,'restored output uses current asset');}finally{f.host.stop();}});
await run('hidden page and user OFF cannot be revived by native foreground',async()=>{const f=fixture();try{f.context('570',1,false);await settle();same(f.calls.filter(c=>['foreground-read','lookup','ensure'].includes(c.command)).length,0,'native foreground alone is no page demand');f.foreground(nativeProof());await settle();same(f.calls.filter(c=>c.command==='lookup').length,0,'native events cannot create page demand');f.context('570',2,true);await settle();await settle();f.enable(false);f.change('settings');const before=f.calls.filter(c=>c.command==='lookup').length;f.foreground(nativeProof({ownerEpoch:'focus-4',revision:4}));await settle();same(f.calls.filter(c=>c.command==='lookup').length,before,'OFF stays withdrawn');same(f.output().at(-1).enabled,false,'OFF remains authoritative');}finally{f.host.stop();}});
await run('late initial foreground read cannot replace a newer native veto',async()=>{let release!:(value:unknown)=>void;const f=fixture({foregroundRead:()=>new Promise(r=>release=r)});try{f.context();await settle();f.foreground(nativeProof({foreground:false,ownerEpoch:'native-veto',revision:2}));release(nativeProof());await settle();same(f.calls.filter(c=>['lookup','ensure','inventory'].includes(c.command)).length,0,'old affirmative RPC receipt cannot bypass newer native blur');for(let n=0;n<100;n++)f.change('asset');await settle();same(f.calls.filter(c=>['lookup','ensure'].includes(c.command)).length,0,'background asset storms remain metadata-idle');}finally{f.host.stop();}});
await run('native epoch history cannot revive an old low-revision proof',async()=>{const f=fixture();try{f.context();await settle();const baseline=f.calls.filter(c=>c.command==='lookup').length;for(let n=2;n<25;n++)f.foreground(nativeProof({foreground:false,ownerEpoch:'retired-'+n,revision:n}));f.foreground(nativeProof());await settle();same(f.calls.filter(c=>c.command==='lookup').length,baseline,'history eviction cannot grant a stale low revision');f.foreground(nativeProof({ownerEpoch:'fresh-current',revision:25}));await settle();same(f.calls.filter(c=>c.command==='lookup').length,baseline+1,'fresh positive revision owns one recovery');}finally{f.host.stop();}});
await run('unknown foreground transport failure stays deny without heartbeat',async()=>{const f=fixture({foregroundRead:async()=>{throw Error('native owner unavailable');}});try{f.context();await settle();await settle();same(f.calls.filter(c=>['lookup','ensure'].includes(c.command)).length,0,'unavailable backend never grants a fallback permission');same(f.timers.size,1,'transport failure owns one finite repair, never a permanent proof poll');}finally{f.host.stop();}});
await run('native media refusal withdraws proof instead of repeating preparation',async()=>{
 const inactive=(reason?:string)=>({key:'steam-570',bindingRevision:1,status:'consumer-inactive',ready:false,reason});
 const rejected=inactive('media-steam-foreground-inactive');
 const list=(f:ReturnType<typeof fixture>,command:string)=>f.calls.filter(c=>c.command===command);
 for(const mode of ['untyped','power','fresh','same','repeat','hidden','peer','blur','reader-failure','ensure'] as const){
  let reads=0,lookups=0,release!:(value:unknown)=>void;
  const f=fixture({foregroundRead:async()=>{
   if(++reads===1)return nativeProof();
   if(['hidden','peer','blur'].includes(mode))return new Promise(r=>release=r);
   if(mode==='reader-failure')throw Error('failed foreground transport');
   return mode==='same'?nativeProof():nativeProof({ownerEpoch:'focus-2',revision:2});
  },lookup:async()=>{
   ++lookups;
   if(mode==='ensure')return {key:'steam-570',bindingRevision:1,status:'missing',ready:false};
   if(mode==='untyped')return inactive();
   if(mode==='power')return inactive('wallpaper-on-battery');
   return mode==='fresh'&&lookups>1?{...entry(570,2),staticReady:true,dynamicReady:true}:rejected;
  },ensure:async()=>rejected});
  try{
   f.context();await settle();
   if(mode==='untyped'||mode==='power'){
    same([reads,lookups,list(f,'ensure').length],[1,1,0],mode+' refusal remains an authoritative deny without extra work');
   }else{
    same(reads,2,mode+' refusal takes one bounded native reading without an event');
   }
   if(mode==='fresh'){
    same(list(f,'lookup').map(call=>call.demand.demandRevision),[1,2],'only a newer native proof reopens the media read');
    same(f.output().at(-1).entry?.bindingRevision,2,'same connection resumes playback without a WinEvent');
   }else if(mode==='same'||mode==='reader-failure'||mode==='repeat'||mode==='ensure'){
    const rejectedProof=mode==='repeat'||mode==='ensure'?nativeProof({ownerEpoch:'focus-2',revision:2}):nativeProof();
    same(lookups,mode==='repeat'||mode==='ensure'?2:1,'same proof, transport failure and a second refusal cannot create a lookup loop');
    for(let n=0;n<32;n++){f.foreground(rejectedProof);f.change('asset');f.context('570',n+2);}
    await settle();
    same([reads,lookups],[2,mode==='repeat'||mode==='ensure'?2:1],'duplicate proof, asset and card events cannot refill the refusal budget');
    same(f.output().at(-1).foreground,false,'denied proof remains non-playing');
    if(mode==='same'){
     f.context('570',34,false);await settle();f.context('730',35,true);await settle();
     same([reads,lookups],[3,1],'reopening reads proof once, not media on a refused revision');
     const reply=f.output().at(-1);
     same([reply?.contextSeq,reply?.appId,reply?.foreground,reply?.entry],[35,'730',false,null],'the current visible card still gets a refusal receipt instead of timing out');
    }
   }else if(mode==='hidden'||mode==='peer'||mode==='blur'){
    if(mode==='hidden')f.context('570',2,false);
    if(mode==='peer')f.connect({connection:'2',runId:'b'.repeat(32)});
    if(mode==='blur')f.foreground(nativeProof({foreground:false,current:false,ownerEpoch:'focus-3',revision:3}));
    release(nativeProof({ownerEpoch:'focus-2',revision:2}));await settle();
    same(lookups,1,'late reread cannot resurrect the withdrawn owner: '+mode);
   }else same(f.output().at(-1).foreground,false,'native refusal withdraws player permission');
   same(f.timers.size,0,mode+' must not leave any polling or retry timer');
  }finally{f.host.stop();release?.(null);await settle();}
 }
});
// Execute the shipping App readiness statements around a deliberately held
// unrelated startup tail. Host/media are production with inert IO; this is
// source-extracted startup ordering, not a full Vue/native boot acceptance.
const appStartup=readFileSync(resolve('src/App.vue'),'utf8').replace(/\r\n/g,'\n');
const settingsAt=appStartup.indexOf('  await loadUiSettings();');
const settingsEnd=appStartup.indexOf('  backgroundOpacity.value =',settingsAt);
const tailStatement=appStartup.split('\n').find(line=>line.includes('deckyMirrorHost?.ready();')&&line.includes('monitorHudHost?.ready();'));
assert.ok(settingsAt>=0&&settingsEnd>settingsAt&&tailStatement,'shipping App readiness boundaries changed');
const runShippingReady=new Function('env','return (async()=>{const {loadUiSettings,steamWallpaperHost,deckyMirrorHost,monitorHudHost,startBackgroundUpdate,aiFanMockActive,aiCpuIsolated}=env;'+appStartup.slice(settingsAt,settingsEnd)+'await env.tail;'+tailStatement+'})()') as (env:any)=>Promise<void>;
const deferred=()=>{let resolve!:(value?:unknown)=>void,reject!:(error:Error)=>void;const promise=new Promise((yes,no)=>{resolve=yes;reject=no;});return{promise,resolve,reject};};
for(const mode of ['normal','user-off','fan-isolated','cpu-isolated','settings-failed','stopped-before-settings'])await run('App wallpaper readiness follows persisted settings without waiting for unrelated tail: '+mode,async()=>{
 const f=fixture({startReady:false}),settings=deferred(),tail=deferred();let controls=0,hud=0,updates=0;if(mode==='user-off')f.enable(false);f.context();
 const completion=runShippingReady({loadUiSettings:()=>settings.promise,tail:tail.promise,steamWallpaperHost:f.host,deckyMirrorHost:{ready:()=>controls++},monitorHudHost:{ready:()=>hud++},startBackgroundUpdate:()=>{updates++;},aiFanMockActive:mode==='fan-isolated',aiCpuIsolated:mode==='cpu-isolated'});
 const handled=completion.then(()=>({ok:true}),error=>({ok:false,error}));
 try{await settle();same(f.frames.some(frame=>frame.type==='wallpaper-ready'&&frame.enabled),false,'unloaded settings cannot authorize wallpaper');same(controls+hud+updates,0,'other hosts retain original tail gate');
  if(mode==='settings-failed'){settings.reject(Error('inert settings IO failure'));same((await handled).ok,false,'settings error remains an initialization failure');same(f.frames.some(frame=>frame.type==='wallpaper-ready'&&frame.enabled),false,'failed settings never allow default-on playback');return;}
  if(mode==='stopped-before-settings')f.host.stop();settings.resolve();await settle();await settle();
  const allowed=mode==='normal';same(f.frames.some(frame=>frame.type==='wallpaper-ready'&&frame.enabled),allowed,'persisted settings ready must release wallpaper before unrelated initialization');
  same(f.output().some(frame=>frame.entry?.assetId==='asset-570'),allowed,'cached media is delivered before unrelated tail only when authorized');same(controls+hud+updates,0,'wallpaper startup cannot authorize control/HUD/update hosts early');
  tail.resolve();same((await handled).ok,true,'original tail can finish normally');const isolated=mode==='fan-isolated'||mode==='cpu-isolated';same(controls+hud+updates,isolated?0:3,'original non-wallpaper readiness guards retained');
 }finally{tail.resolve();f.host.stop();}
});
await run('cached output is immediate and identical completion is not sent twice',async()=>{
 const f=fixture(),confirmed={...f.peer,connectionRevision:1,revision:1};
 try{
  f.connect(confirmed);f.context();await settle();
  same(f.output().length,1,'one delivery');same(f.output()[0].entry.assetId,'asset-570','cached source');same(f.calls.filter(c=>c.command==='ensure').length,1,'shared owner complements cache');
  const admitted=f.calls.find(c=>c.command==='lookup')!.demand,frames=f.frames.length,calls=f.calls.length;
  for(let revision=2;revision<=33;revision++)f.connect({...confirmed,revision});
  await settle();
  same(admitted.current(),true,'authenticated same-transport attach must not revoke the current wallpaper demand');
  same(f.frames.length,frames,'duplicate confirmation does not reannounce wallpaper');
  same(f.calls.length,calls,'duplicate confirmation does not clear selection or reread media');
  same(f.timers.size,0,'healthy reattach does not arm retry');
  f.context('570',1,true);await settle();
  same(f.calls.length,calls,'same-transport observation cannot reopen an old selection sequence');
  f.setEntry(entry(570,2));f.change('asset','steam-570');await settle();
  same(f.output().at(-1).entry.revision,'2','asset change still reaches the same selection without another context');
  f.connect({...confirmed,connected:false,connectionRevision:2});await settle();
  same(admitted.current(),false,'real disconnect still revokes wallpaper demand');
 }finally{f.host.stop();}
});
await run('unrelated asset notifications do not resolve or publish current game',async()=>{const f=fixture();try{f.setEntry(pairedEntry());f.context();await settle();same(f.output().at(-1).entry.assetId,'asset-570','complete pair is displayed');const count=f.calls.length;f.change('asset','steam-99');await settle();same(f.calls.length,count,'unrelated key does not refresh the selected pair');f.change('retry');await settle();same(f.calls.length,count,'a background retry does not refresh the complete pair');}finally{f.host.stop();}});
await run('shortcut EXE cache is displayed and missing pair uses shared identification queue',async()=>{const f=fixture();f.context('4131894616');await settle();same(f.output()[0].entry.assetId,'asset-570','same cached output reused');same(f.calls.find(c=>c.command==='ensure').target.appId,0,'unknown shortcut keeps file identity');same(f.calls.find(c=>c.command==='ensure').target.executable,'G:/Games/local.exe','exact file passed to shared queue');same(f.calls.find(c=>c.command==='ensure').target.catalogSource,'steam-shortcut','active selection retains added-Steam source');same(f.calls.find(c=>c.command==='ensure').target.steamAccount,'17','active selection retains current account');same(f.calls.find(c=>c.command==='ensure').target.shortcutAppId,'4131894616','active selection retains shortcut ID');same(f.calls.filter(c=>c.command==='gameMedia.identify').length,0,'render host performs no competing identity lookup');f.setEntry(entry(570,2));f.change('asset','steam-570');await settle();same(f.calls.filter(c=>c.command==='gameMedia.shortcut').length,1,'same context reuses active-account resolution');same(f.output().at(-1).entry.revision,'2','selected cache revision republished');f.context('4131894616',2);await settle();same(f.calls.filter(c=>c.command==='gameMedia.shortcut').length,2,'new context rechecks account mapping');f.host.stop();});
await run('confirmed file alias reuses a warm canonical asset on the selected shortcut',async()=>{let current:any={key:'exe-old',bindingRevision:0,status:'missing',ready:false};const f=fixture({lookup:async()=>current,ensure:async()=>current});f.context('4131894616');await settle();same(f.output().at(-1).entry,null,'selected unresolved file has no output');current=entry(570,3);f.change('asset','exe-old');await settle();same(f.output().at(-1).entry.assetId,'asset-570','targeted previous-key notification reuses cached canonical output');same(f.calls.filter(c=>c.command==='gameMedia.shortcut').length,1,'no extra current-account scan for the alias publication');f.host.stop();});
await run('hidden route clears media without identity or preparation work',async()=>{const f=fixture();f.context('4131894616',1,false);await settle();same(f.output().at(-1).entry,null,'paused output');same(f.calls.some(c=>['lookup','ensure','gameMedia.shortcut'].includes(c.command)),false,'no hidden media reads');f.host.stop();});
await run('old context or another connection cannot replace selected output',async()=>{const f=fixture();f.context('570',3);await settle();const count=f.calls.length;f.context('1091500',2);f.context('1091500',4,true,{connection:'999'});f.context('1091500',4,true,{runId:'b'.repeat(32)});await settle();same(f.calls.length,count,'stale and foreign requests ignored');f.host.stop();});
await run('changing game fences an earlier asynchronous lookup',async()=>{let resolve!:(entry:GameMediaEntry)=>void;const pending=new Promise<GameMediaEntry>(yes=>resolve=yes);const f=fixture({lookup:async target=>target.appId===570?pending:entry(1091500),ensure:async target=>entry(target.appId)});f.context('570',1);await settle();f.context('1091500',2);await settle();resolve(entry());await settle();same(f.output().some(frame=>frame.appId==='570'),false,'no late old-game output');same(f.output().at(-1).appId,'1091500','new game remains selected');f.host.stop();});
await run('published playback derivative is adopted on reselection without a mid-play replacement',async()=>{
 const original=pairedEntry();
 const reduced:GameMediaEntry={...original,asset:{...original.asset!,id:'derived-720p30',revision:'derived',reducedPlayback:true},url:'http://fixture/derived-720p30'};
 let available=original;
 const f=fixture({lookup:async target=>target.appId===570?available:pairedEntry(target.appId)});
 try{
  f.context('570',1);await settle();
  same(f.output().at(-1).entry.assetId,original.asset!.id,'original is ready before conversion');
  const before=f.output().length;
  available=reduced;
  f.change('wallpaper-playback',original.key);
  await settle();
  same(f.output().length,before,'derivative publication keeps current playback');
  same(f.timers.size,0,'publication does not require a playback timer');
  f.context('730',2);await settle();
  f.context('570',3);await settle();
  const frame=f.output().at(-1);
  same([frame.contextSeq,frame.entry.assetId,frame.entry.reducedPlayback,frame.entry.bindingRevision],[3,'derived-720p30',true,original.bindingRevision],'reselection delivers the new source without changing its editor binding');
  same(f.calls.filter(c=>c.command==='lookup').map(c=>c.target.appId),[570,730,570],'reselection reads the current store once');
 }finally{f.host.stop();}
});
await run('same selection adopts the first verified derivative with or without a poster',async()=>{
 for(const poster of [false,true]){
  const base=pairedEntry();
  const reduced:GameMediaEntry={...base,asset:{...base.asset!,id:'derived-720p30',revision:'derived',reducedPlayback:true},url:'http://fixture/derived-720p30'};
  let available:GameMediaEntry={...base,wallpaper30Limited:true,ready:poster,asset:poster?{...base.asset!,id:'poster',kind:'image'}:undefined,url:poster?'http://fixture/poster':undefined};
  const f=fixture({lookup:async()=>available});
  try{
   f.context();await settle();
   same(f.output().at(-1).entry?.assetId??null,poster?'poster':null,'original 60 FPS is not exposed');
   f.change('wallpaper-playback','steam-730');await settle();
   same(f.calls.filter(c=>c.command==='lookup').length,1,'unrelated video does not read the selection');
   available=reduced;
   f.change('wallpaper-playback',base.key);await settle();
   const frame=f.output().at(-1);
   same([frame.contextSeq,frame.entry?.assetId,frame.entry?.reducedPlayback],[1,'derived-720p30',true],'same context becomes playable');
   same(f.calls.filter(c=>c.command==='lookup').length,2,'one authoritative read on publication');
   same(f.calls.filter(c=>c.command==='ensure').length,0,'cached pair does not redownload');
   same(f.timers.size,0,'no polling');
  }finally{f.host.stop();}
 }
});
await run('YMCC switch off clears media and prevents preparation',async()=>{const f=fixture();f.context();await settle();f.enable(false);f.change('settings');await settle();const count=f.calls.filter(c=>c.command==='ensure').length;same(f.output().at(-1).entry,null,'off releases frame');f.context('1091500',2);await settle();same(f.calls.filter(c=>c.command==='ensure').length,count,'off never prepares');same(f.frames.filter(c=>c.type==='wallpaper-ready').at(-1).enabled,false,'switch signal delivered');f.host.stop();});
await run('playback failure requires the exact delivered revision and selection',async()=>{
 const valid={contextSeq:1,appId:'570',assetId:'asset-570',bindingRevision:1,status:'playback-failed'};
 const video=fixture();try{
  video.context();await settle();video.status({...valid,bindingRevision:0});video.status({...valid,appId:'99'});same(video.failures.length,0,'stale failure ignored');
  video.status(valid);video.status(valid);await settle();same(video.failures.length,1,'one exact failure accepted');
 }finally{video.host.stop();}
 const image=fixture({playbackFailed:async()=>{throw Error('unexpected image mutation');}});try{
  const base=entry();image.setEntry({...base,staticReady:true,dynamicReady:true,asset:{...base.asset!,kind:'image',extension:'jpg',mime:'image/jpeg'}});
  image.context();await settle();same(image.output().at(-1).entry.kind,'image','legacy report is for a delivered image');
  const calls=image.calls.length,frames=image.output().length;
  image.status(valid);image.status({...valid,kind:'video'});await settle();
  same(image.failures.length,0,'legacy image failure must not invalidate shared media');
  same(image.output().length,frames,'image failure does not clear or republish the frame');
  same(image.calls.length,calls,'image failure starts no cache or preparation work');
  same(image.timers.size,0,'image failure starts no retry');
 }finally{image.host.stop();}
});
 await run('slow derivative handoff is a single scoped lookup and does not change the next selection',async()=>{
  const original=pairedEntry(),reduced:GameMediaEntry={...original,asset:{...original.asset!,id:'derived-720p30',reducedPlayback:true},url:'http://fixture/derived-720p30'};
  const f=fixture({lookup:async target=>target.wallpaperPlaybackFallback?original:reduced});
  const recovery={contextSeq:1,appId:'570',assetId:reduced.asset!.id,bindingRevision:original.bindingRevision,status:'playback-interrupted'};
  const lookups=()=>f.calls.filter(c=>c.command==='lookup');
  try{
   f.context();await settle();
   same(f.output().at(-1).entry.recoverOriginal,true,'current host advertises original recovery only for the delivered derivative');
   f.status({...recovery,contextSeq:0});f.status({...recovery,bindingRevision:0});await settle();
   same(lookups().length,1,'stale startup timeout cannot trigger a read');
   f.status(recovery);f.status(recovery);await settle();
   same(lookups().length,2,'one immediate fallback read for the exact bound derivative');
   same(lookups().at(-1)?.target.wallpaperPlaybackFallback,{assetId:reduced.asset!.id,bindingRevision:original.bindingRevision},'bypass is scoped to the exact cached derivative');
   same([f.output().at(-1).contextSeq,f.output().at(-1).entry.assetId],[1,original.asset!.id],'original returns without changing the selection');
   same([f.failures.length,f.calls.filter(c=>c.command==='ensure').length,f.timers.size],[0,0,0],'no cache invalidation, preparation, or new host timer');
   f.context('570',2);await settle();
   same([f.output().at(-1).contextSeq,f.output().at(-1).entry.assetId],[2,reduced.asset!.id],'reselection prefers the verified derivative again');
  }finally{f.host.stop();}
 });
await run('appearance changes update same asset without duplicate ready announcement',async()=>{
 const gate=deferred();let hold=false,asset=pairedEntry(570,1);
 const f=fixture({lookup:async()=>hold?gate.promise as Promise<GameMediaEntry>:asset});
 const mediaCalls=()=>f.calls.filter(c=>['lookup','ensure','inventory','foreground-read','gameMedia.shortcut'].includes(c.command)).length;
 try{
  f.context();await settle();const ready=f.frames.filter(m=>m.type==='wallpaper-ready').length;
  const before=mediaCalls(),beforeFrames=f.output().length;
  const stringify=JSON.stringify;
  let bodyEncodes=0,wireEncodes=0;
  JSON.stringify=((value:unknown,...args:any[])=>{
   const message=value as {type?:string;entry?:{assetId?:string};stateOwner?:string}|null;
   if(message?.type==='wallpaper'&&message.entry?.assetId==='asset-570'){
    if(message.stateOwner)++wireEncodes;else ++bodyEncodes;
   }
   return Reflect.apply(stringify,JSON,[value,...args]);
  }) as typeof JSON.stringify;
  try{for(let i=0;i<24;i++){f.appearance(i===23?.4:(i+1)/100);f.change('settings');await settle();}}
  finally{JSON.stringify=stringify;}
  const frameEncodes={bodyEncodes,wireEncodes,frames:f.output().length-beforeFrames};
  console.log('WALLPAPER_FRAME_ENCODINGS '+JSON.stringify(frameEncodes));
  same(frameEncodes,{bodyEncodes:0,wireEncodes:24,frames:24},'each sent frame computes one full envelope signature');
  const latest=f.output().at(-1),styleMediaCalls=mediaCalls()-before;
  console.log('WALLPAPER_APPEARANCE_WORK '+JSON.stringify({updates:24,styleMediaCalls,revision:latest.entry.revision}));
  same([latest.opacity,latest.entry.revision],[.4,'1'],'appearance republished with the original asset');
  same(f.frames.filter(m=>m.type==='wallpaper-ready').length,ready,'ready only sent on transition');
  same(styleMediaCalls,0,'style-only changes do not access the media owner');
  hold=true;f.change('asset','steam-570');await settle();
  const started=mediaCalls();f.appearance(.7);f.change('settings');await settle();
  same([mediaCalls(),f.output().at(-1).opacity,f.output().at(-1).entry.revision],[started,.7,'1'],'current frame can repaint while a real cache read is in flight');
  asset=pairedEntry(570,2);hold=false;gate.resolve(asset);await settle();
  same([mediaCalls(),f.output().at(-1).opacity,f.output().at(-1).entry.revision],[started,.7,'2'],'real publication settles once with latest appearance');
  f.context('570',2,false);await settle();const hidden=mediaCalls();f.appearance(.6);f.change('settings');await settle();
  same([mediaCalls(),f.output().at(-1).entry],[hidden,null],'style cannot revive a hidden asset');
  f.context('570',3,true);await settle();const visible=mediaCalls();f.enable(false);f.appearance(.3);f.change('settings');await settle();
  same([mediaCalls(),f.output().at(-1).enabled,f.output().at(-1).entry],[visible,false,null],'combined OFF and appearance update revokes the frame');
  f.enable(true);f.change('settings');await settle();same([f.output().at(-1).entry.revision,f.output().at(-1).opacity],['2',.3],'ON still uses fresh foreground and media proof');
  same(f.timers.size,0,'appearance never owns a timer');
 }finally{f.host.stop();gate.resolve(asset);await settle();}
});
await run('shortcut IDs and unavailable mapping never fall back to title guesses',async()=>{const calls:any[]=[];const call=(async(command:string,args:any)=>{calls.push({command,args});return {};}) as any;same(await resolveWallpaperTarget('0','game',call),null,'zero rejected');same(await resolveWallpaperTarget('18446744073709551616','game',call),null,'overflow rejected');same(await resolveWallpaperTarget('4131894616','game',call),null,'unknown shortcut unresolved');same(calls.length,1,'only real VDF lookup');
 let mapping:any={};const f=fixture({shortcutRead:async()=>mapping});
 try{
  f.context('4131894616');await settle();await settle();
  same(f.output().at(-1).entry,null,'unavailable shortcut remains unguessed');
  same(f.calls.filter(c=>c.command==='gameMedia.shortcut').length,1,'one initial real mapping lookup');
  same(f.timers.size,0,'missing mapping adds no polling');
  mapping={executable:'G:/Games/local.exe',name:'Local Game',shortcutAppId:'4131894616',steamAccount:'17'};
  f.change('asset');await settle();await settle();
  same(f.output().at(-1)?.entry?.assetId,'asset-570','existing media notification recovers the same selected shortcut');
  same(f.calls.filter(c=>c.command==='gameMedia.shortcut').length,2,'one fresh mapping after observed source change');
  for(let n=0;n<1000;n++)f.change('asset');await settle();await settle();
  same(f.calls.filter(c=>c.command==='gameMedia.shortcut').length,2,'successful mapping stays cached through 1000 notifications');
  same(f.timers.size,0,'recovered mapping has no polling');
 }finally{f.host.stop();}
});
await run('card selection does not rescan inventory; return from hidden or reconnect does',async()=>{
 const f=fixture();const count=(command:string)=>f.calls.filter(call=>call.command===command).length;
 try{
  f.context('570',1);await settle();const initial=count('inventory');
  f.context('1091500',2);await settle();same(count('inventory'),initial,'card changes do not enumerate the library');
  f.context('1091500',3,false);f.context('1091500',4,true);await settle();same(count('inventory'),initial+1,'visible return refreshes known shortcuts');
  const foregroundReads=count('foreground-read');
  f.connect();f.context('1091500',1);await settle();
  same([count('inventory'),count('foreground-read')],[initial+1,foregroundReads],'duplicate attach keeps current context and ignores an old sequence');
  const peer={...f.peer,connection:'2',connectionRevision:2};
  f.connect(peer);await settle();same(count('inventory'),initial+1,'reconnect without a visible context does no scan');
  same(count('foreground-read'),foregroundReads,'reconnect without visibility does not request foreground');
  f.context('1091500',1,true,peer);await settle();
  same(count('inventory'),initial+2,'new visible peer context rechecks current inventory');
  same(count('foreground-read'),foregroundReads+1,'new visible peer requests foreground without a passive native event');
 }finally{f.host.stop();}
});
await run('unrelated UI settings do not query selected media',async()=>{const f=fixture();f.context();await settle();const count=f.calls.length;f.change('settings');f.change('settings');await settle();same(f.calls.length,count,'unchanged wallpaper settings do no native reads');f.host.stop();});

await run('selected shortcut reads existing launcher wallpaper without preparing it',async()=>{
 const id='4131894616',exe='G:/Games/local.exe';
 const video={...entry(106),currentGameWallpaperFallback:true as const,dynamicReady:true};
 const missing={key:'exe-fixture',bindingRevision:0,status:'missing',ready:false};
 let failures=0;
 const f=fixture({lookup:async target=>target.currentGameWallpaper===true&&target.executable===exe&&!("appId" in target)?video:
  missing,ensure:async()=>missing,playbackFailed:async()=>{if(++failures===1)throw Error('fixture failure transport');return video;}});
 try{
  f.context(id);await settle();
  same(f.calls.filter(c=>c.command==='gameMedia.shortcut').length,1,'the selected shortcut must be read through the current Steam account');
  same(f.output().at(-1)?.entry?.assetId,video.asset!.id,'selected shortcut must reuse the existing launcher video');
  same(f.output().at(-1).appId,id,'the Decky frame keeps the selected shortcut ID');
  same(f.calls.filter(c=>c.command==='ensure').length,0,'a read-only display fallback cannot prepare more media');
  same(f.calls.filter(c=>c.command==='lookup').length,1,'the fallback does not need a second resolve call');
  same(f.calls.filter(c=>c.command==='selectSteamTarget').at(-1).target.appId,0,'display reuse never promotes the unresolved shortcut');
  const demand=f.calls.find(c=>c.command==='lookup').demand;
  same([demand.demandConsumer,demand.current()],['steam',true],'the fallback still needs native foreground demand');
  f.status({contextSeq:1,appId:id,assetId:video.asset!.id,bindingRevision:video.bindingRevision,status:'playback-failed'});await settle();
  same(f.timers.size,1,'unacknowledged decoder failure schedules one recheck');await f.fire();
  same(f.failures.map(v=>[v.key,v.assetId,v.bindingRevision]),[[video.key,video.asset!.id,video.bindingRevision],[video.key,video.asset!.id,video.bindingRevision]],'retry checks and reports the same canonical asset revision');
  f.context(id,2,false);await settle();const reads=f.calls.filter(c=>c.command==='lookup').length;
  f.change('asset','steam-106');await settle();
  same(demand.current(),false,'hiding revokes the original foreground demand');
  same(f.calls.filter(c=>c.command==='lookup').length,reads,'hidden shortcut cannot use the fallback');
 }finally{f.host.stop();}
 const known=fixture({shortcut:{executable:exe,appId:106,verified:true},lookup:async()=>({...entry(106),staticReady:true,dynamicReady:true})});
 try{known.context(id);await settle();const target=known.calls.find(c=>c.command==='lookup').target;
  same([target.appId,target.currentGameWallpaper],[106,undefined],'verified shortcut keeps its explicit AppID');
  same(known.output().at(-1)?.entry?.assetId,'asset-106','verified shortcut still returns normal media');
 }finally{known.host.stop();}
 const absent=fixture({lookup:async()=>missing,ensure:async()=>missing});
 try{absent.context(id);await settle();const prepared=absent.calls.filter(c=>c.command==='ensure');
  same(prepared.map(c=>[c.target.appId,c.target.executable,c.target.currentGameWallpaper]),[[0,exe,undefined]],'missing video retains original serial admission using the real shortcut target');
 }finally{absent.host.stop();}
});

await run('new selection and hidden route withdraw the previous interactive preparation priority',async()=>{
 const f=fixture();f.context('570',1);await settle();same(f.calls.filter(c=>c.command==='selectSteamTarget').at(-1).target.appId,570,'confirmed current target owns foreground quota');f.context('1091500',2);same(f.calls.filter(c=>c.command==='selectSteamTarget').at(-1).target,null,'old target is downgraded before asynchronous identity');await settle();same(f.calls.filter(c=>c.command==='selectSteamTarget').at(-1).target.appId,1091500,'new confirmed target promoted');f.context('1091500',3,false);await settle();same(f.calls.filter(c=>c.command==='selectSteamTarget').at(-1).target,null,'hidden route relinquishes foreground quota');f.host.stop();
});
await run('reenabling resumes the selected game without another card movement',async()=>{const f=fixture();f.context();await settle();f.enable(false);f.change('settings');await settle();const before=f.calls.filter(c=>c.command==='lookup').length;f.enable(true);f.change('settings');await settle();same(f.frames.filter(c=>c.type==='wallpaper-ready').at(-1).enabled,true,'ON announced');same(f.output().at(-1).entry.assetId,'asset-570','current cached selection restored');same(f.calls.filter(c=>c.command==='lookup').length,before+1,'same selected context is refreshed once');f.host.stop();});
await run('OFF fences an in-flight download completion and remains idle',async()=>{let complete!:(entry:GameMediaEntry)=>void;const pending=new Promise<GameMediaEntry>(resolve=>{complete=resolve;});const f=fixture({ensure:()=>pending});f.context();await settle();f.enable(false);f.change('settings');await settle();const frameCount=f.output().length;complete(entry(570,2));await settle();same(f.output().length,frameCount,'late completion cannot resurrect playback');same(f.output().at(-1).enabled,false,'OFF stays authoritative');const reads=f.calls.filter(c=>['lookup','ensure'].includes(c.command)).length;f.change('asset','steam-570');f.context('1091500',2);await settle();same(f.calls.filter(c=>['lookup','ensure'].includes(c.command)).length,reads,'asset changes and new cards do not read media while OFF');f.host.stop();});


await run('committed wake waits for fresh visible context then republishes the cache',async()=>{
 const f=fixture();f.context('4131894616',1);await settle();const before=f.calls.filter(c=>c.command==='gameMedia.shortcut').length,beforeFrames=f.output().length;f.resume(2);await settle();same(f.output().length,beforeFrames+1,'wake clears old displayed selection');same(f.output().at(-1).entry,null,'wake cannot trust pre-sleep visibility');same(f.calls.filter(c=>c.command==='gameMedia.shortcut').length,before,'wake handshake alone does not read account data');f.context('4131894616',2,true);await settle();same(f.frames.filter(c=>c.type==='wallpaper-ready'&&c.resumeGeneration!==undefined).at(-1).resumeGeneration,2,'committed native wake signalled');same(f.calls.filter(c=>c.command==='gameMedia.shortcut').length,before+1,'active Steam account is revalidated after wake');same(f.output().at(-1).entry.assetId,'asset-570','cached current wallpaper returns');const count=f.calls.length;for(const generation of [2,1,0,NaN])f.resume(generation);await settle();same(f.calls.length,count,'duplicate old and invalid wake generations are inert');f.host.stop();f.resume(3);await settle();same(f.calls.length,count+1,'stop only relinquishes selected preparation; wake callback removed');
});
await run('saved OFF survives committed wake without media or enable requests',async()=>{
 const f=fixture();f.context();await settle();f.enable(false);f.change('settings');await settle();const count=f.calls.length;f.resume(2);f.resume(3);await settle();same(f.calls.length,count,'OFF wake has no IPC or media work');same(f.frames.filter(c=>c.type==='wallpaper-ready').at(-1).enabled,false,'OFF cannot be overridden by recovery');f.host.stop();
});
await run('resume fences pre-sleep asynchronous completion and new connection still requires current context',async()=>{
 let finish!:(entry:GameMediaEntry)=>void;let reads=0;const pending=new Promise<GameMediaEntry>(resolve=>finish=resolve);const f=fixture({lookup:async()=>++reads===1?pending:entry(570,2),ensure:async()=>entry(570,2)});f.context();await settle();f.resume(2);await settle();same(reads,1,'no second read before fresh post-wake visibility');f.context('570',2);await settle();finish(entry(570,1));await settle();same(f.output().at(-1).entry.revision,'2','old pre-sleep revision cannot overwrite wake output');f.connect({...f.peer,connectionRevision:2});const before=f.output().length;f.resume(3);await settle();same(f.output().length,before,'new peer cannot reuse another connection selection');f.context('1091500',1);await settle();same(f.output().at(-1).appId,'1091500','new authenticated peer selection restores playback');f.host.stop();
});

await run('actual IPC committed power event reaches production host and is detached on teardown',async()=>{
 const previous=(globalThis as any).window;(globalThis as any).window=new EventTarget();
 const fact=(generation:number,publicationRevision:number,extra:any={})=>({phase:'ready',generation,known:true,hardwareWritesAllowed:true,notificationPending:false,observedNotificationSequence:publicationRevision,publicationRevision,...extra});
 const emit=(state:any)=>{(globalThis as any).__F2_SHARED_POWER_CURRENT__=state;window.dispatchEvent(new CustomEvent('ipc:power.lifecycleChanged',{detail:state}));};
 const f=fixture({nativePower:true});
 try{
  f.context();same(f.calls.filter(c=>c.command==='lookup').length,0,'default host cannot play before a complete shared Ready');
  emit(fact(4,1));await settle();same(f.frames.filter(c=>c.type==='wallpaper-ready').at(-1).enabled,true,'actual public Ready reaches default host');
  same(f.output().at(-1).entry,null,'Ready alone requires a fresh page context');
  f.context('570',2,true);await settle();same(f.output().at(-1).entry.assetId,'asset-570','actual shared admission permits fresh page demand');
  const before=f.calls.length;
  emit(fact(4,2));emit(fact(4,2));await settle();same(f.calls.length,before,'healthy duplicate and metadata-only Ready are idle');
  f.host.stop();const count=f.calls.length;emit(fact(5,3));await settle();same(f.calls.length,count,'public listener detached on stop');
 }finally{f.host.stop();(globalThis as any).window=previous;}
});

await run('coalesced same-generation suspend scope revokes wallpaper over public IPC',async()=>{
 const previous=(globalThis as any).window;(globalThis as any).window=new EventTarget();
 let revision=0,ordinary=0;
 const publish=(suspendSerial?:number)=>window.dispatchEvent(new CustomEvent('ipc:power.lifecycleChanged',{detail:{
  phase:'ready',generation:9,known:true,hardwareWritesAllowed:true,notificationPending:false,
  observedNotificationSequence:++revision,publicationRevision:revision,...(suspendSerial===undefined?{}:{suspendSerial}),
 }}));
 const nested=onSharedPowerLifecycle(state=>{if(state.suspendSerial===2)publish(3);},{observeSuspendSerial:true});
 const offOrdinary=onSharedPowerLifecycle(()=>{++ordinary;});
 const observed:Array<number|undefined>=[];
 const offObserved=onSharedPowerLifecycle(state=>observed.push(state.suspendSerial),{observeSuspendSerial:true});
 const f=fixture({nativePower:true});
 const count=(command:string)=>f.calls.filter(c=>c.command===command).length;
 try{
  publish(1);await settle();f.context();await settle();
  const demand=f.calls.find(c=>c.command==='lookup').demand;
  same(demand.current(),true,'initial visible demand is current');
  const before=[count('foreground-read'),count('lookup'),count('ensure')];
  const normal=ordinary;
  publish(2);
  same(demand.current(),false,'coalesced Ready revokes old demand synchronously');
  same(observed,[1,3],'nested delivery skips the superseded scope and delivers the current one once');
  await settle();
  same(f.output().at(-1).entry,null,'coalesced Ready clears the decoder');
  same(f.frames.filter(x=>x.type==='wallpaper-ready'&&x.requestContext).length,2,'initial and coalesced Ready both request fresh visibility');
  same([count('foreground-read'),count('lookup'),count('ensure')],before,'Ready alone performs no read');
  f.context('570',1,true);await settle();
  same([count('foreground-read'),count('lookup'),count('ensure')],before,'old context cannot restore playback');
  f.context('570',2,true);await settle();
  same(f.output().at(-1).entry.assetId,'asset-570','fresh context restores playback');
  same(count('foreground-read'),before[0]+1,'fresh context reads foreground once');
  same(count('lookup'),before[1]+1,'fresh context reads media once');
  const current=f.calls.filter(c=>c.command==='lookup').at(-1).demand;
  const stable=f.calls.length;
  publish(3);publish();publish(2);await settle();
  same(getSharedPowerLifecycle()?.suspendSerial,3,'duplicates, old envelopes and absent metadata cannot rewind the scope');
  same(getSharedPowerLifecycle()?.publicationRevision,5,'an out-of-order scope cannot advance ordering watermarks');
  same(current.current(),true,'current demand survives repeated and old facts');
  same(f.calls.length,stable,'repeated and old facts cause no playback replay');
  same(ordinary,normal,'ordinary subscribers do not replay on suspend-scope metadata');
 }finally{f.host.stop();offObserved();offOrdinary();nested();(globalThis as any).window=previous;}
});

await run('coalesced notification retires in-flight wallpaper without replaying healthy playback',async()=>{
 for(const held of ['foreground','cache'] as const){
  const previous=(globalThis as any).window;(globalThis as any).window=new EventTarget();
  let read=0,lookup=0,releaseForeground!:(value:unknown)=>void,releaseCache!:(value:GameMediaEntry)=>void;
  const f=fixture({nativePower:true,
   foregroundRead:async()=>++read===1&&held==='foreground'?new Promise(resolve=>releaseForeground=resolve):nativeProof(),
   lookup:async()=>++lookup===1&&held==='cache'?new Promise(resolve=>releaseCache=resolve):pairedEntry(570,2)});
  const publish=(sequence:number,revision:number)=>{
   const value={phase:'ready',generation:9,known:true,hardwareWritesAllowed:true,notificationPending:false,
    suspendSerial:1,observedNotificationSequence:sequence,publicationRevision:revision};
   (globalThis as any).__F2_SHARED_POWER_CURRENT__=value;
   window.dispatchEvent(new CustomEvent('ipc:power.lifecycleChanged',{detail:value}));
  };
  try{
   publish(21,1);await settle();f.context('570',1);await settle();
   same([read,lookup],held==='foreground'?[1,0]:[1,1],'original visible context owns one held read');
   // A revision-only duplicate must not retire a read that still owns its scope.
   const before=f.calls.length;publish(21,2);await settle();same(f.calls.length,before,'revision-only publication is inert');
   publish(22,2);await settle();
   if(held==='foreground')releaseForeground(nativeProof());else releaseCache(pairedEntry(570,1));
   await settle();
   same(f.output().filter(packet=>packet.entry!==null).length,0,'retired reply cannot publish an old frame');
   same([read,lookup],held==='foreground'?[1,0]:[1,1],'scope change alone requests visibility without IO');
   same(f.frames.filter(x=>x.type==='wallpaper-ready'&&x.requestContext).length,2,'current scope asks for fresh visibility');
   f.context('570',1);await settle();same(read,1,'old context cannot restart the retired request');
   f.context('570',2);await settle();same([read,lookup],held==='foreground'?[2,1]:[2,2],'fresh context performs one current read');
   same(f.output().at(-1).entry.revision,'2','only the fresh frame returns');
   const healthy=f.calls.length;publish(23,3);await settle();
   same(f.calls.length,healthy,'a settled healthy frame is not replayed by scope metadata alone');
  }finally{f.host.stop();(globalThis as any).window=previous;}
 }
});

await run('actual S0 IPC Ready restores wallpaper only after fresh visibility and current generation',async()=>{
 const previous=(globalThis as any).window;(globalThis as any).window=new EventTarget();
 const emit=(name:string,data:any)=>window.dispatchEvent(new CustomEvent('ipc:'+name,{detail:data}));
 let revision=0,holdLookup=false,releaseLookup!:(v:GameMediaEntry)=>void,holdReply=false,releaseReply!:(v:boolean)=>void;
 const fact=(generation:number,extra:any={})=>({phase:'ready',generation,known:true,hardwareWritesAllowed:true,notificationPending:false,observedNotificationSequence:++revision,publicationRevision:revision,...extra});
 const publish=(state:any)=>{(globalThis as any).__F2_SHARED_POWER_CURRENT__=state;emit('power.lifecycleChanged',state);};
 const f=fixture({nativePower:true,lookup:async()=>holdLookup?new Promise(r=>releaseLookup=r):entry(),reply:async message=>holdReply&&message.type==='wallpaper'&&message.entry?new Promise(r=>releaseReply=r):true});
 const resumes:number[]=[];const offResume=subscribeGameMediaResume(event=>resumes.push(event.generation!));
 try{
  publish(fact(1));await settle();f.context();await settle();
  same(f.output().at(-1).entry.assetId,'asset-570','initial complete shared Ready is required');
  const firstDemand=f.calls.find(c=>c.command==='lookup').demand;
  const reads=f.calls.filter(c=>c.command==='lookup').length;
  emit('power.suspending',{generation:2});publish(fact(2,{phase:'suspending',hardwareWritesAllowed:false}));
  same(firstDemand.current(),false,'public nonadmitted fact synchronously revokes captured demand');
  await settle();same(f.output().at(-1).entry,null,'suspend delivers decoder revocation');
  same(f.frames.filter(c=>c.type==='wallpaper-ready').at(-1).enabled,false,'effective playback Ready is disabled while saved setting remains ON');
  const receipt=process.env.YMCC_MEDIA_READY_NATIVE_RECEIPT?JSON.parse(readFileSync(process.env.YMCC_MEDIA_READY_NATIVE_RECEIPT,'utf8')):null;
  const ready=receipt?.nativeReady||{event:'gameMedia.resume-ready',data:{generation:2,source:'sleep-intent-canceled'}};
  emit(ready.event,ready.data);publish(fact(2));await settle();
  same(f.output().at(-1).entry,null,'S0 Ready invalidates pre-sleep visibility');
  same(f.calls.filter(c=>c.command==='lookup').length,reads,'Ready alone performs no media/account IO');
  same(f.frames.filter(c=>c.type==='wallpaper-ready').at(-1).resumeGeneration,undefined,'public recovery uses ordinary Ready, not a synthetic generation');
  f.context('570',2,true);await settle();same(f.output().at(-1).entry.assetId,'asset-570','fresh visibility restores cached wallpaper');
  let count=f.calls.length;emit('power.resumed',{generation:2});emit(ready.event,ready.data);publish(fact(2));await settle();
  same(f.calls.length,count,'two Ready paths and healthy same-generation publication stay idle');
  const pending=fact(2,{hardwareWritesAllowed:false,notificationPending:true});
  const demand=f.calls.filter(c=>c.command==='lookup').at(-1).demand;
  publish(pending);same(demand.current(),false,'same-generation pending synchronously clears native ticket');
  const beforeRevoke=f.frames.length;await settle();
  same(f.frames.slice(beforeRevoke).some(x=>x.type==='wallpaper'&&x.entry===null&&x.foreground===false),true,'pending delivers a decoder revoke');
  count=f.calls.filter(c=>['foreground-read','lookup','ensure'].includes(c.command)).length;
  f.foreground(nativeProof());f.change('retry');f.context('570',3,true);await settle();
  same(f.calls.filter(c=>['foreground-read','lookup','ensure'].includes(c.command)).length,count,'old proof retry and page request cannot override pending');
  const resumesBefore=resumes.length;publish(fact(2));await settle();
  same(resumes.length,resumesBefore+1,'media Ready adapter recognizes same-generation admitted correction');
  same(f.calls.filter(c=>['foreground-read','lookup','ensure'].includes(c.command)).length,count,'same-generation recovery waits for new visible context');
  same(f.frames.filter(c=>c.type==='wallpaper-ready').at(-1).enabled,true,'same-generation Ready requests page visibility by restoring effective admission');
  same(f.frames.filter(c=>c.type==='wallpaper-ready').at(-1).requestContext,true,'same-generation recovery explicitly requests visibility when OFF was not delivered');
  f.context('570',4,true);await settle();same(f.output().at(-1).entry.assetId,'asset-570','same-generation recovery accepts only new page visibility');
  for(const extra of [{known:false},{hardwareWritesAllowed:undefined},{hardwareWritesAllowed:false}]){
    const captured=f.calls.filter(c=>c.command==='lookup').at(-1).demand;
    publish(fact(2,extra));same(captured.current(),false,'unknown or missing/false hardware admission never grants current');
    await settle();count=f.calls.filter(c=>c.command==='lookup').length;
    publish(fact(2));await settle();same(f.calls.filter(c=>c.command==='lookup').length,count,'each admitted correction waits for page proof');
    f.context('570',5+revision,true);await settle();
  }
  holdLookup=true;f.context('570',100,true);await settle();
  const priorEnsures=f.calls.filter(c=>c.command==='ensure').length;
  publish(fact(2,{notificationPending:true,hardwareWritesAllowed:false}));await settle();const frameCount=f.frames.length;
  releaseLookup(entry(570,99));await settle();
  same(f.frames.slice(frameCount).some(x=>x.entry),false,'pre-pending asynchronous lookup cannot revive a frame');
  same(f.calls.filter(c=>c.command==='ensure').length,priorEnsures,'pre-pending lookup cannot enter preparation');
  holdLookup=false;publish(fact(2));await settle();holdReply=true;f.context('570',101,true);await settle();
  assert.equal(typeof releaseReply,'function','a real delivery promise is held');
  publish(fact(2,{notificationPending:true,hardwareWritesAllowed:false}));await settle();
  same(f.output().at(-1).entry,null,'revoke bypasses the old hung delivery slot');
  const afterRevoke=f.frames.length;releaseReply(false);await settle();
  same(f.frames.slice(afterRevoke).some(x=>x.entry),false,'old delivery refusal cannot resurrect its frame');
  same(f.timers.size,0,'retired delivery cannot arm retry after pending');
  holdReply=false;publish(fact(2));await settle();f.context('570',102,true);await settle();
  const stableCalls=f.calls.length,stableResumes=resumes.length;
  for(let n=0;n<20;n++)publish(fact(2));await settle();
  same(f.calls.length,stableCalls,'metadata-only publication storm stays idle');
  same(resumes.length,stableResumes,'media adapter does not replay healthy Ready');
  emit('power.suspending',{generation:3});publish(fact(3,{phase:'suspending',hardwareWritesAllowed:false}));await settle();
  const suspended=f.calls.length;emit(ready.event,ready.data);publish({...fact(2),publicationRevision:1});await settle();
  same(f.calls.length,suspended,'old S0 Ready cannot reset current output');
  f.enable(false);f.change('settings');const off=f.calls.length;emit('gameMedia.resume-ready',{generation:3});publish(fact(3));await settle();
  same(f.calls.length,off,'S0 event preserves explicit wallpaper OFF');
  offResume();f.host.stop();const stopped=f.calls.length;emit('gameMedia.resume-ready',{generation:4});publish(fact(4));await settle();
  same(f.calls.length,stopped,'all Ready listeners detached after stop');
 }finally{offResume();f.host.stop();(globalThis as any).window=previous;}
});


await run('rejected native wallpaper frame retries without reading media again',async()=>{
 let rejected=false;const f=fixture({reply:async msg=>{if(msg.type==='wallpaper'&&msg.entry&&!rejected){rejected=true;return false;}return true;}});try{f.context();await settle();const reads=f.calls.filter(c=>c.command==='lookup'||c.command==='ensure').length;same(f.timers.size,1,'native false receipt retains one bounded delivery retry');same([...f.timers.values()][0].ms,250,'transient refused delivery gets one short recovery before gradual backoff');await f.fire();same(f.output().filter(x=>x.entry?.assetId==='asset-570').length,2,'unchanged frame is retried after explicit refusal');same(f.calls.filter(c=>c.command==='lookup'||c.command==='ensure').length,reads,'retry reuses the outgoing frame, no media or inventory refresh');same(f.timers.size,0,'successful recovery is dormant');same(f.calls.filter(c=>c.command==='deckySidebar.mirrorReply').every(c=>c.options?.timeoutMs===5000),true,'delivery IPC has a bounded wait');}finally{f.host.stop();}
});
await run('shared connection with no visible wallpaper performs no library scans',async()=>{
 const f=fixture();try{same(f.calls.filter(c=>c.command==='inventory').length,0,'HUD-only connection cannot scan a wallpaper library');f.context('570',1,false);await settle();same(f.calls.filter(c=>c.command==='inventory').length,0,'hidden context has no scan');f.context('570',2,true);await settle();same(f.calls.filter(c=>c.command==='inventory').length,1,'actual foreground context scans once');}finally{f.host.stop();}
});

await run('stopped wallpaper owner never sends a queued microtask',async()=>{
 const f=fixture();f.host.stop();await settle();same(f.frames.length,0,'stop before the first reply microtask revokes that send');same(f.timers.size,0,'stopped owner has no retry timer');
});
await run('slow frame delivery keeps only newest hidden intent',async()=>{
 let release!:(ok:boolean)=>void;let once=false;const f=fixture({reply:async msg=>{if(msg.type==='wallpaper'&&msg.entry&&!once){once=true;return new Promise<boolean>(resolve=>release=resolve);}return true;}});try{f.context();await settle();f.context('1091500',2);await settle();f.context('1091500',3,false);await settle();same(f.output().filter(x=>x.entry).length,1,'one frame in flight, newer game remains coalesced');release(false);await settle();same(f.output().at(-1).contextSeq,3,'only latest hidden context follows the old return');same(f.output().at(-1).entry,null,'old failure cannot resurrect a visible source');same(f.timers.size,0,'superseded failure is not retried');}finally{f.host.stop();}
});
await run('late refusal from old connection cannot arm retry for new peer',async()=>{
 let release!:(ok:boolean)=>void;let once=false;const f=fixture({reply:async msg=>{if(msg.type==='wallpaper'&&msg.entry&&!once){once=true;return new Promise<boolean>(resolve=>release=resolve);}return true;}});try{f.context();await settle();f.connect({connection:'2',runId:f.peer.runId});f.context('1091500',1,true,{connection:'2'});await settle();const before=f.frames.length;release(false);await settle();same(f.frames.length,before,'old connection return sends nothing');same(f.timers.size,0,'old refusal cannot start a timer in new connection');same(f.output().at(-1).appId,'1091500','new peer remains selected');}finally{f.host.stop();}
});
await run('ready refusal repairs handshake but hidden channel stays free of scans',async()=>{
 let once=false;const f=fixture({reply:async msg=>{if(msg.type==='wallpaper-ready'&&!once){once=true;return false;}return true;}});try{await settle();same(f.timers.size,1,'failed ready also retains one retry');await f.fire();same(f.frames.filter(m=>m.type==='wallpaper-ready').length,2,'same ON handshake is retried');same(f.timers.size,0,'healthy ready removes timer');same(f.calls.filter(c=>['lookup','ensure','inventory'].includes(c.command)).length,0,'handshake retry has no media reads or scan');}finally{f.host.stop();}
});

await run('temporary shortcut read failure retries only while foreground',async()=>{
 let attempts=0;const f=fixture({shortcutRead:async()=>{if(++attempts===1)throw Error('media-steam-account-unconfirmed');return {executable:'G:/Games/local.exe',steamAccount:'17',shortcutAppId:'4131894616'};}});try{f.context('4131894616');await settle();same(f.timers.size,1,'failed current identity read retains one slow retry');same(f.output().at(-1).entry,null,'unconfirmed identity never guesses a source');const scans=f.calls.filter(c=>c.command==='inventory').length;await f.fire();same(f.calls.filter(c=>c.command==='gameMedia.shortcut').length,2,'actual current-account read is retried');same(f.calls.filter(c=>c.command==='gameMedia.shortcut').every(c=>c.options?.timeoutMs===5000),true,'shortcut reads cannot stay pending forever');same(f.output().at(-1).entry.assetId,'asset-570','recovered file reuses the verified cache');same(f.calls.filter(c=>c.command==='inventory').length,scans,'mapping retry does not enumerate the library');same(f.timers.size,0,'mapping success leaves no resident retry');}finally{f.host.stop();}
});
await run('hiding or disabling cancels failed identity recovery',async()=>{
 const f=fixture({shortcutRead:async()=>{throw Error('unavailable');}});try{f.context('4131894616');await settle();same(f.timers.size,1,'visible failure scheduled once');f.context('4131894616',2,false);same(f.timers.size,0,'hidden context removes lookup timer');f.context('4131894616',3,true);await settle();same(f.timers.size,1,'new visible request can retry');f.enable(false);f.change('settings');same(f.timers.size,0,'saved OFF cancels identity recovery');}finally{f.host.stop();}same(f.timers.size,0,'stop leaves no timer');
});

await run('temporary cache read failure does not submit a download and recovers',async()=>{
 let reads=0;const f=fixture({lookup:async()=>{if(++reads===1)throw Error('cache-host-unavailable');return entry();}});try{f.context();await settle();same(f.calls.filter(c=>c.command==='ensure').length,0,'read outage must not submit a preparation job');same(f.timers.size,1,'one slow selected cache recovery');await f.fire();same(f.output().at(-1).entry.assetId,'asset-570','healthy cache is reused on recovery');same(f.timers.size,0,'healthy cache cancels the recovery timer');}finally{f.host.stop();}
});
await run('repeated refusals retain one timer and OFF supersedes stale video',async()=>{
 const f=fixture({reply:async msg=>{if(msg.type==='wallpaper'&&msg.entry)throw Error('lost-receipt');return true;}});try{f.context();await settle();const reads=f.calls.filter(c=>c.command==='lookup'||c.command==='ensure').length;for(let n=0;n<5;n++){same(f.timers.size,1,'one retry despite repeated throw');await f.fire();}same(f.calls.filter(c=>c.command==='lookup'||c.command==='ensure').length,reads,'five delivery retries never repeat cache work');f.enable(false);f.change('settings');await settle();same(f.timers.size,0,'OFF acknowledgement cancels obsolete source retry');same(f.output().at(-1).enabled,false,'only OFF remains as latest output');}finally{f.host.stop();}
 // Same selection, real updated binding each time. During a refused reply
 // only the value should change, not the delivery deadline or attempt budget.
 const held=deferred();let recovered=false;
 const live=fixture({reply:async msg=>{
  if(msg.type!=='wallpaper'||!msg.entry||msg.appId!=='570'||recovered)return true;
  if(msg.entry.bindingRevision===1)return await held.promise as boolean;
  return false;
 }});
 const videoReplies=()=>live.output().filter(msg=>msg.appId==='570'&&msg.entry);
 try{
  live.context();await settle();same(videoReplies().length,1,'one first frame in flight');
  live.setEntry(entry(570,2));live.change('asset','steam-570');await settle();
  held.resolve(false);await settle();const afterRefusal=videoReplies().length;
  const deadline=[...live.timers][0];
  for(let i=3;i<=10;i++){live.setEntry(entry(570,i));live.change('asset','steam-570');await settle();}
  const reads=live.calls.filter(x=>x.command==='lookup'||x.command==='ensure').length;
  const result={afterRefusal,extraAttempts:videoReplies().length-afterRefusal,sameTimer:live.timers.get(deadline?.[0])===deadline?.[1],delays:[...live.timers.values()].map(t=>t.ms)};
  console.log(JSON.stringify({wallpaperDeliveryBackoff:result}));
  same(result,{afterRefusal:1,extraAttempts:0,sameTimer:true,delays:[250]},'same-selection frames retain the original backoff');
  recovered=true;await live.fire();
  same(videoReplies().map(x=>x.entry.bindingRevision),[1,10],'recovery sends latest frame and does not replay old bindings');
  same(live.calls.filter(x=>x.command==='lookup'||x.command==='ensure').length,reads,'delivery retry does not look up media again');
  same(live.timers.size,0,'successful latest frame parks recovery');
  recovered=false;live.setEntry(entry(570,11));live.change('asset','steam-570');await settle();same(live.timers.size,1,'another real refusal arms one retry');
  live.setEntry(entry(730,12));live.context('730',2,true);await settle();
  same([live.output().at(-1).appId,live.output().at(-1).entry.bindingRevision],['730',12],'a new selection bypasses an old frame retry');
  same(live.timers.size,0,'new selection supersedes the old retry');
 }finally{held.resolve(false);live.host.stop();await settle();}
});

await run('explicit ready replay preserves saved intent without extra scans',async()=>{
 const f=fixture();try{f.context();await settle();const ready=f.frames.filter(x=>x.type==='wallpaper-ready').length,scans=f.calls.filter(x=>x.command==='inventory').length;f.context('570',2,true,{request:{command:'wallpaper.context',contextSeq:2,appId:'570',visible:true,requestReady:true}});await settle();same(f.frames.filter(x=>x.type==='wallpaper-ready').length,ready+1,'saved readiness is explicitly replayed');same(f.frames.filter(x=>x.type==='wallpaper-ready').at(-1).contextSeq,2,'readiness replay identifies the fresh request across host recreation');same(typeof f.frames.filter(x=>x.type==='wallpaper-ready').at(-1).stateOwner,'string','local state revision has an owner scope');same(f.output().at(-1).stateOwner,f.frames.filter(x=>x.type==='wallpaper-ready').at(-1).stateOwner,'frame and readiness refer to the same wallpaper owner');same(f.calls.filter(x=>x.command==='inventory').length,scans,'ready replay never enumerates inventory again');f.enable(false);f.change('settings');await settle();f.context('570',3,true,{request:{command:'wallpaper.context',contextSeq:3,appId:'570',visible:true,requestReady:true}});await settle();same(f.frames.filter(x=>x.type==='wallpaper-ready').at(-1).enabled,false,'replay preserves saved OFF');}finally{f.host.stop();}
});

await run('OFF before send microtask suppresses obsolete ON entirely',async()=>{
 const f=fixture();try{f.enable(false);f.change('settings');await settle();same(f.frames.some(x=>x.type==='wallpaper-ready'&&x.enabled===true),false,'superseded ON never reaches native send');same(f.frames.filter(x=>x.type==='wallpaper-ready').at(-1).enabled,false,'latest OFF remains authoritative');same(f.timers.size,0,'supersession does not become a failed-delivery retry');}finally{f.host.stop();}
});

await run('background publication burst leaves wallpaper owner completely dormant',async()=>{
 const f=fixture();try{f.context();await settle();f.context('570',2,false);await settle();const calls=f.calls.length;for(let n=0;n<100;n++)f.change('asset','steam-'+n);await settle();same(f.calls.length,calls,'background asset publications cause no reply, selection walk, cache read or scan');same(f.timers.size,0,'background publications create no timer');}finally{f.host.stop();}
});
await run('wake ready refusal recovers quickly and still requires fresh visibility',async()=>{
 let refused=false;const f=fixture({reply:async message=>{if(message.type==='wallpaper-ready'&&message.resumeGeneration===2&&!refused){refused=true;return false;}return true;}});
 try{f.context();await settle();f.resume(2);await settle();const reads=f.calls.filter(x=>x.command==='lookup'||x.command==='ensure').length;
 same(f.timers.size,1,'one failed wake readiness owns recovery');same([...f.timers.values()][0].ms,250,'committed wake does not wait twenty seconds after one refusal');
 await f.fire();same(f.frames.filter(x=>x.type==='wallpaper-ready'&&x.resumeGeneration===2).length,2,'same authenticated wake state is redelivered');same(f.calls.filter(x=>x.command==='lookup'||x.command==='ensure').length,reads,'wake delivery retry does not read media');same(f.timers.size,0,'successful wake delivery is idle');
 f.context('570',2);await settle();same(f.output().at(-1).entry.assetId,'asset-570','fresh visible post-wake context reaches cached video');
 }finally{f.host.stop();}
});
await run('persistent passive refusal backs off without rescan and OFF cancels it',async()=>{
 const f=fixture({reply:async message=>message.enabled!==true});
 try{f.context();await settle();const reads=f.calls.filter(x=>x.command==='lookup'||x.command==='ensure'||x.command==='inventory').length;
 for(const delay of [250,1000,3000,10000,20000,20000]){same(f.timers.size,1,'only one timer owns both latest-value lanes');same([...f.timers.values()][0].ms,delay,'persistent failure uses the capped gradual backoff');await f.fire();}
 same(f.calls.filter(x=>x.command==='lookup'||x.command==='ensure'||x.command==='inventory').length,reads,'six retries do not repeat discovery or media work');
 f.enable(false);f.change('settings');await settle();same(f.timers.size,0,'authoritative OFF acknowledgements cancel passive retry');same(f.output().at(-1).enabled,false,'stale failed ON cannot replace OFF');
 }finally{f.host.stop();}
});
await run('late passive retry callback after teardown remains silent',async()=>{
 const f=fixture({reply:async()=>false});f.context();await settle();const late=[...f.timers.values()][0].fn;f.host.stop();const sent=f.frames.length;late();await settle();same(f.frames.length,sent,'late callback cannot send after owner stop');same(f.timers.size,0,'late callback cannot recreate recovery timer');
 const outcomes=[];
 for(const replacePeer of [true,false]){
  let attempts=0;const live=fixture({reply:async message=>!(message.type==='wallpaper'&&message.entry&&++attempts<=2)});
  try{
   live.context();await settle();same(live.timers.size,1,'first refusal owns one retry');
   const [retiredId,retired]=[...live.timers][0];
   const peer=replacePeer?{connection:'2',runId:'b'.repeat(32)}:live.peer;
   if(replacePeer)live.connect(peer);
   live.setEntry(entry(730,2));live.context('730',2,true,{...peer,request:{runId:peer.runId}});await settle();
   same(attempts,2,'replacement frame was attempted and refused');
   same(live.timers.has(retiredId),false,'obsolete timer is cancelled');
   same(live.timers.size,1,'current refusal owns one fresh retry');
   const [currentId,current]=[...live.timers][0];
   live.setEntry(entry(730,3));live.change('asset','steam-730');await settle();
   same(attempts,2,'latest frame waits for the current retry');
   same(live.timers.get(currentId)===current,true,'replacement frame keeps its retry deadline');
   const reads=()=>live.calls.filter(c=>c.command==='lookup'||c.command==='ensure'||c.command==='inventory').length;
   const beforeReads=reads();
   retired.fn();await settle();
   const earlyAttempts=attempts-2;
   const sameDeadline=live.timers.get(currentId)===current;
   await live.fire();
   outcomes.push({replacePeer,earlyAttempts,sameDeadline,timedAttempts:attempts-2-earlyAttempts,
    revisions:live.output().filter(x=>x.entry).map(x=>x.entry.bindingRevision),extraReads:reads()-beforeReads,remainingTimers:live.timers.size});
  }finally{live.host.stop();}
 }
 console.log(JSON.stringify({wallpaperRetryOwnership:outcomes}));
 same(outcomes,[true,false].map(replacePeer=>({replacePeer,earlyAttempts:0,sameDeadline:true,timedAttempts:1,
  revisions:[1,2,3],extraReads:0,remainingTimers:0})),'cancelled callbacks cannot spend either replacement retry');
});
// Repeated same-shortcut account changes interleaved with committed wake,
// rejected ready delivery, pending old-account reads and replaced peers.
await run('12 account and peer replacements fence old reads wake retries and reconnect callbacks',async()=>{
 const waiting:Array<{target:any;promise:Promise<unknown>;resolve:(value?:unknown)=>void;reject:(error:Error)=>void}>=[];
 let account='17',refuseGeneration=0,refused=false;
 const f=fixture({shortcutRead:async()=>({executable:'G:/Games/local.exe',steamAccount:account,shortcutAppId:'4131894616'}),
  lookup:target=>{const wait={target,...deferred()};waiting.push(wait);return wait.promise as Promise<GameMediaEntry>;},
  reply:async message=>{if(message.type==='wallpaper-ready'&&message.resumeGeneration===refuseGeneration&&!refused){refused=true;return false;}return true;}});
 const context=(peer:any,seq:number)=>f.context('4131894616',seq,true,{connection:peer.connection,runId:peer.runId,request:{runId:peer.runId}});
 let peer={...f.peer};
 try{
  await settle();
  for(let cycle=0;cycle<12;cycle++){
   context(peer,cycle*2+2);await settle();const pending=waiting.at(-1)!;
   same(pending.target.steamAccount,account,'pending mapping belongs to the current account');
   refuseGeneration=cycle+2;refused=false;f.resume(refuseGeneration);await settle();
   same(f.timers.size,1,'one refused committed wake owns one bounded delivery retry');
   const late=[...f.timers.values()].map(t=>t.fn),old={...peer};
   account=String(18+cycle);peer={connection:String(cycle+2),runId:(cycle+2).toString(16).padStart(32,'0')};
   f.connect(peer);await settle();const frames=f.frames.length,reads=waiting.length;
   context(old,9999);f.resume(cycle+1);for(const callback of late)callback();
   pending.resolve(entry(570,cycle+100));await settle();
   same(f.frames.length,frames,'retired callback cannot publish into the replacement peer');
   same(waiting.length,reads,'old peer and old wake cannot issue another account read');
   same(f.timers.size,0,'peer replacement cancels the old delivery retry');
   context(peer,1);await settle();const current=waiting.at(-1)!;
   same(current.target.steamAccount,account,'same shortcut is resolved again for the new active account');
   f.setEntry(entry(570,cycle+2));current.resolve(entry(570,cycle+2));await settle();
   same(f.output().at(-1).entry.revision,String(cycle+2),'only current peer mapping reaches wallpaper');
   same(f.timers.size,0,'healthy replacement leaves no retry or polling');
  }
 }finally{f.host.stop();}
 same(f.timers.size,0,'all twelve lifecycles return their timers');
});

await run('failed playback report revalidates current cache before a bounded retry',async()=>{
 let count=0;const f=fixture({playbackFailed:async()=>{if(++count===1)throw Error('inert media IPC unavailable');return entry(570,2);}});
 try{f.context();await settle();const scans=f.calls.filter(c=>c.command==='inventory').length;f.status({contextSeq:1,appId:'570',assetId:'asset-570',bindingRevision:1,status:'playback-failed'});await settle();
  same(f.timers.size,1,'lost failure report must own one recovery deadline');same([...f.timers.values()][0].ms,1000,'first report recheck is bounded and prompt');
  const reads=f.calls.filter(c=>c.command==='lookup').length;await f.fire();same(f.calls.filter(c=>c.command==='lookup').length,reads+1,'retry must first read the current cache tuple');same(f.failures.length,2,'unchanged ready revision proves the old write did not commit and permits one retry');same(f.timers.size,0,'accepted retry is dormant');same(f.calls.filter(c=>c.command==='inventory').length,scans,'failure handoff never repeats library discovery');
 }finally{f.host.stop();}
});
await run('lost committed failure receipt is read back without repeating mutation',async()=>{
 const invalid={...entry(570,2),ready:false,dynamicReady:false,status:'error'};const f=fixture({playbackFailed:async()=>{f.setEntry(invalid);throw Error('inert ACK lost after commit');}});
 try{f.context();await settle();f.status({contextSeq:1,appId:'570',assetId:'asset-570',bindingRevision:1,status:'playback-failed'});await settle();same(f.timers.size,1,'unconfirmed result schedules one status read');await f.fire();same(f.failures.length,1,'new native revision must never replay the old mutation');same(f.output().at(-1).entry,null,'readback publishes invalidated cache truth');same(f.timers.size,0,'readback success stops recovery');}finally{f.host.stop();}
});
await run('failure handoff backoff is finite and keeps failed ready asset quarantined',async()=>{
 const f=fixture({playbackFailed:async()=>{throw Error('inert persistent media IPC failure');}});
 try{f.context();await settle();f.status({contextSeq:1,appId:'570',assetId:'asset-570',bindingRevision:1,status:'playback-failed'});await settle();
  for(const delay of [1000,5000,20000]){same(f.timers.size,1,'one recovery timer despite error storm');same([...f.timers.values()][0].ms,delay,'bounded handoff backoff');for(let i=0;i<100;i++)f.status({contextSeq:1,appId:'570',assetId:'asset-570',bindingRevision:1,status:'playback-failed'});await f.fire();}
  same(f.failures.length,4,'original write plus at most three revalidated attempts');same(f.timers.size,0,'exhaustion leaves no resident polling');for(let i=0;i<10;i++)f.change('asset','steam-570');await settle();same(f.timers.size,0,'unrelated asset signals cannot replenish failed report budget');same(f.output().at(-1).entry,null,'known failed tuple remains static while write handoff is unavailable');
 }finally{f.host.stop();}
});
await run('cache drift and hidden OFF peer wake revoke pending failure handoff',async()=>{
 for(const cause of ['new-cache','hidden','off','peer','wake','stop'] as const){const f=fixture({playbackFailed:async()=>{throw Error('inert report unavailable');}});
  try{f.context();await settle();f.status({contextSeq:1,appId:'570',assetId:'asset-570',bindingRevision:1,status:'playback-failed'});await settle();const late=[...f.timers.values()][0]?.fn;assert.ok(late,'expected owned recovery timer');
   if(cause==='new-cache'){f.setEntry(entry(570,3));f.change('asset','steam-570');}else if(cause==='hidden')f.context('570',2,false);else if(cause==='off'){f.enable(false);f.change('settings');}else if(cause==='peer')f.connect({connection:'2',runId:f.peer.runId});else if(cause==='wake')f.resume(2);else f.host.stop();
   await settle();const before=f.calls.length;late();await settle();same(f.failures.length,1,'revoked '+cause+' cannot repeat old failure mutation');same(f.calls.length,before,'late '+cause+' recovery callback is silent');same(f.timers.size,0,'revoked '+cause+' cancels timer');
  }finally{f.host.stop();}
 }
});

await run('queued failure callback cannot bypass a rearmed recovery deadline',async()=>{
 const f=fixture({playbackFailed:async()=>{throw Error('inert report refused');}});try{f.context();await settle();f.status({contextSeq:1,appId:'570',assetId:'asset-570',bindingRevision:1,status:'playback-failed'});await settle();const late=[...f.timers.values()][0].fn;await f.fire();same([...f.timers.values()][0].ms,5000,'second retry preserves backoff');const calls=f.calls.length;late();await settle();same(f.calls.length,calls,'same queued callback cannot reread or repeat mutation');same(f.failures.length,2,'old callback cannot consume another attempt');same(f.timers.size,1,'old callback cannot orphan the new timer');}finally{f.host.stop();}
});
await run('changed cache during pending failure verification wins before any write',async()=>{
 let waiting=false,release!:(entry:GameMediaEntry)=>void;const f=fixture({lookup:async()=>waiting?new Promise(resolve=>release=resolve):entry(),playbackFailed:async()=>{throw Error('inert report refused');}});
 try{f.context();await settle();f.status({contextSeq:1,appId:'570',assetId:'asset-570',bindingRevision:1,status:'playback-failed'});await settle();waiting=true;await f.fire();f.context('1091500',2,false);release(entry());await settle();same(f.failures.length,1,'late exact-looking old cache read cannot authorize a write for another context');same(f.timers.size,0,'late verification does not revive timers');}finally{f.host.stop();}
});
await run('unavailable cache rechecks never authorize failure writes',async()=>{
 let unavailable=false;const f=fixture({lookup:async()=>{if(unavailable)throw Error('inert cache unavailable');return entry();},playbackFailed:async()=>{throw Error('inert original ACK lost');}});
 try{f.context();await settle();f.status({contextSeq:1,appId:'570',assetId:'asset-570',bindingRevision:1,status:'playback-failed'});await settle();unavailable=true;for(let i=0;i<3;i++)await f.fire();same(f.failures.length,1,'failed read is never proof the original write did not commit');same(f.timers.size,0,'unavailable status exhausts the same finite budget');}finally{f.host.stop();}
});
// Hold real Promises across normal event-loop turns. These tests never replay
// a cancelled timer; f.fire() consumes only the currently owned one-shot timer.
await run('unexposed wallpaper clears the old frame and cannot own playback failure',async()=>{
 for(const source of ['lookup','ensure']){
  const pending=deferred();let cached=source==='lookup'?pairedEntry():entry();
  const f=fixture({lookup:async()=>cached,ensure:async()=>pending.promise as Promise<GameMediaEntry>});
  try{
   f.context();await settle();same(f.output().at(-1).entry.revision,'1',source+' initially shows valid wallpaper');
   const refused={...pairedEntry(570,2),url:''};
   if(source==='lookup'){cached=refused;f.change('asset','steam-570');}else pending.resolve(refused);
   await settle();const cleared=f.output().at(-1);
   same([cleared.enabled,cleared.foreground,cleared.contextSeq,cleared.appId],[true,true,1,'570'],'clearing frame retains current presentation owner');
   same(cleared.entry,null,source+' uses a clearing frame, not invalid media');
   same(cleared.status,'media-url-unavailable','delivery refusal is not a playback failure');
   for(const bindingRevision of [1,2])f.status({contextSeq:1,appId:'570',assetId:'asset-570',bindingRevision,status:'playback-failed'});
   await settle();same(f.failures.length,0,'neither previous nor unexposed binding admits failure');
   same(f.timers.size,0,'no retry timer for missing URL');
   const previousLookups=f.calls.filter(c=>c.command==='lookup').length;
   cached=pairedEntry(570,3);f.change('asset','steam-570');await settle();
   same(f.calls.filter(c=>c.command==='lookup').length,previousLookups+1,'real publication performs exactly one lookup');
   same(f.output().at(-1).entry.bindingRevision,3,'fresh publication restores playback');
   f.status({contextSeq:1,appId:'570',assetId:'asset-570',bindingRevision:3,status:'playback-failed'});await settle();
   same(f.failures.map(v=>v.bindingRevision),[3],'fresh exposed binding still owns playback failure');
   same(f.calls.filter(c=>c.command==='ensure').length,source==='ensure'?1:0,'no new preparation for already paired media');
  }finally{f.host.stop();pending.resolve(pairedEntry());await settle();}
 }
});
await run('complement transport refusal does not revoke a current cached frame',async()=>{
 for(const outcome of ['timeout','unexposed','off','replacement']){
  const pending=deferred();let attempts=0;
  const f=fixture({lookup:async target=>target.appId===570?entry():pairedEntry(target.appId),ensure:async()=>{++attempts;return pending.promise as Promise<GameMediaEntry>;}});
  try{
   f.context();await settle();
   same(f.output().at(-1).entry.assetId,'asset-570','the approved cached video is exposed before complement');
   same(attempts,1,'one held complement request');
   if(outcome==='off'){f.enable(false);f.change('settings');}
   if(outcome==='replacement')f.context('730',2,true);
   await settle();const published=f.output().length;
   if(outcome==='unexposed')pending.resolve({...pairedEntry(570,2),url:''});
   else pending.reject(Error('fixture complement transport timeout'));
   await settle();const selected=f.output().at(-1);
   if(outcome==='unexposed')same([selected.entry,selected.status],[null,'media-url-unavailable'],'explicit unavailable delivery still clears the old video');
   else{
    same(f.output().length,published,'rejected complement emits no replacement frame: '+outcome);
    same([selected.entry?.assetId,selected.enabled,selected.contextSeq],outcome==='off'?[undefined,false,1]:outcome==='replacement'?['asset-730',true,2]:['asset-570',true,1],'a rejected request cannot revoke or revive another selection');
   }
   same(f.timers.size,0,'no new retry scheduler for optional preparation');
   same(f.failures.length,0,'optional preparation failure is not decode failure');
  }finally{pending.resolve(pairedEntry());f.host.stop();await settle();}
 }
});
await run('32 cross-task same-key changes coalesce into one fresh trailing lookup',async()=>{
 const head=deferred(),tail=deferred();let reads=0,pending=0,peakPending=0,latest=pairedEntry();
 const f=fixture({lookup:async()=>{++reads;++pending;peakPending=Math.max(peakPending,pending);try{return await((reads===1?head.promise:tail.promise) as Promise<GameMediaEntry>);}finally{--pending;}}});
 try{
  f.context();await settle();same(reads,1,'initial selected lookup is pending');
  for(let n=0;n<32;n++){latest=pairedEntry(570,n+2);f.change('asset','steam-570');await settle();}
  same(reads,1,'32 notifications cannot submit 32 obsolete native reads');same(f.output().length,0,'dirty pending result is not a fresh cache');
  head.resolve(entry());await settle();same(reads,2,'one trailing lookup reads the current cache');
  const waiting={frames:f.output().length,ensures:f.calls.filter(c=>c.command==='ensure').length};
  console.log('WALLPAPER_OBSOLETE_LOOKUP '+JSON.stringify(waiting));
  same(waiting,{frames:0,ensures:0},'a dirty lookup must not publish or start preparation while the trailing read is pending');
  tail.resolve(latest);await settle();same(f.output().at(-1).entry.revision,'33','asset changes between initial and trailing reads survive');same(f.output().length,1,'only current cache is published');
  same(peakPending,1,'same context never owns two pending lookups');same(pending,0,'both initial and trailing lookup actually settled');
  console.log('WALLPAPER_STORM_METRICS '+JSON.stringify({notifications:32,lookupsDuringNotifications:1,totalLookups:reads,trailingLookups:reads-1,peakPendingLookups:peakPending,pendingLookupsAtEnd:pending,finalAssetRevision:f.output().at(-1).entry.revision}));
  same(f.calls.filter(c=>c.command==='ensure').length,0,'complete fresh cache needs no preparation');same(f.calls.filter(c=>c.command==='inventory').length,1,'notifications do not rescan');same(f.timers.size,0,'coalescing has no timer');
 }finally{f.host.stop();head.resolve(pairedEntry());tail.resolve(latest);await settle();}
});
await run('32 cross-task changes during ensure revalidate before any further preparation',async()=>{
 const preparing=deferred(),tail=deferred();let cached=entry(),reads=0,prepares=0;
 const f=fixture({lookup:async()=>++reads===1?cached:tail.promise as Promise<GameMediaEntry>,ensure:async()=>{prepares++;return preparing.promise as Promise<GameMediaEntry>;}});
 try{
  f.context();await settle();same(reads,1,'initial cache read completed');same(prepares,1,'one original ensure is pending');
  for(let n=0;n<32;n++){cached=pairedEntry(570,n+2);f.change('asset','steam-570');await settle();}
  same(reads,1,'ensure owns the same publication chain');same(prepares,1,'notifications do not repeat an unconfirmed preparation');
  preparing.resolve(entry(570,2));await settle();same(reads,2,'ensure settlement is followed by a real fresh lookup');
  const waiting=f.output().map(message=>message.entry?.revision);
  console.log('WALLPAPER_OBSOLETE_ENSURE '+JSON.stringify({revisions:waiting,ensures:prepares}));
  same(waiting,['1'],'keep the last verified asset while the trailing read is pending');
  tail.resolve(cached);await settle();same(prepares,1,'fresh complete cache prevents another ensure');same(f.output().at(-1).entry.revision,'33','stale ensure result cannot stand in for current cache');same(f.timers.size,0,'no polling or new deadline');
 }finally{f.host.stop();preparing.resolve(entry());tail.resolve(cached);await settle();}
});
await run('a new context bypasses an old hung lookup and its dirty tail',async()=>{
 const old=deferred();let reads=0;
 const f=fixture({lookup:async target=>{reads++;return target.appId===570?old.promise as Promise<GameMediaEntry>:pairedEntry(target.appId,7);}});
 try{
  f.context();await settle();for(let n=0;n<8;n++){f.change('asset','steam-570');await settle();}
  f.context('1091500',2);await settle();same(reads,2,'new selection reads without awaiting the old lookup');same(f.output().at(-1).appId,'1091500','new selected game is displayed while old read is hung');
  const calls=f.calls.length;old.resolve(pairedEntry(570,99));await settle();same(f.calls.length,calls,'old settlement cannot run its dirty tail');same(f.output().at(-1).entry.revision,'7','old cache cannot overwrite new context');same(f.timers.size,0,'revoked tail is dormant');
 }finally{f.host.stop();old.resolve(pairedEntry());await settle();}
 for(const stage of ['asset','foreground','settings'] as const){
 const pending=deferred(),proof=deferred();let holdForeground=false;
 const nextSeq=stage==='foreground'?3:2;
 const timers=new Map<number,{run:()=>void;ms:number}>();let seq=0,socketCount=0,closed=false,hudSent=0,ackPresented=0;
 const events=new Map<string,(event?:any)=>void>();
 const receive=(message:any)=>events.get('message')?.({data:JSON.stringify({...message,runId:live.peer.runId})});
 const live=fixture({startReady:stage!=='settings',foregroundRead:async()=>holdForeground?proof.promise:nativeProof(),lookup:async target=>target.appId===570?pairedEntry():pending.promise as Promise<GameMediaEntry>,reply:async message=>{
  if(message.type==='wallpaper-context-accepted'){
   receive({...message,appId:'999'});receive({...message,contextSeq:1});
   same([...timers.values()].map(t=>t.ms),[10000],'wrong app or stale selection cannot certify the current lease');
  }
  receive(message);return true;
 }});
 const socket={readyState:1,addEventListener:(name:string,fn:(event?:any)=>void)=>events.set(name,fn),close:()=>{closed=true;socket.readyState=3;events.get('close')?.();},send:(wire:string)=>{
  const request=JSON.parse(wire);
  if(request.command==='wallpaper.context')live.context(request.appId,request.contextSeq,request.visible,{request});
  else if(request.command==='hud.status')hudSent++;
 }};
 const mirror=new MirrorClient({clientId:'wallpaper-host-test',bootstrap:()=>({endpoint:'ws://127.0.0.1:12345/mirror',token:'TEST-ONLY',runId:live.peer.runId,wallpaperProtocol:1,hudProtocol:1}),socket:()=>{socketCount++;return socket as unknown as WebSocket;},schedule:((run:()=>void,ms:number)=>{timers.set(++seq,{run,ms});return seq;}) as any,cancel:((id:number)=>timers.delete(id)) as any});
 const off=mirror.subscribeExtra(message=>{if((message as any)?.type==='wallpaper-context-accepted')ackPresented++;});
 try{
  mirror.setPassiveDemand('wallpaper',true);mirror.setPassiveDemand('hud',true);events.get('open')?.();
  same(mirror.sendWallpaper('wallpaper.context',{contextSeq:1,appId:'570',visible:true,requestReady:true}),true,'established request accepted');await settle();
  if(stage==='settings')same(live.frames.filter(m=>m.type==='wallpaper-ready').at(-1)?.enabled,false,'unloaded settings cannot enable wallpaper');
  else same(live.output().at(-1)?.entry?.assetId,'asset-570','initial media proved on the shared connection');
  if(stage==='foreground'){
   holdForeground=true;
   same(mirror.sendWallpaper('wallpaper.context',{contextSeq:2,appId:'570',visible:false}),true,'a hidden route withdraws its foreground proof');await settle();
  }
  same(mirror.sendWallpaper('wallpaper.context',{contextSeq:nextSeq,appId:'1091500',visible:true}),true,'changed card is sent');await settle();
  const expiry=[...timers].find(([,t])=>t.ms===10000);if(expiry){timers.delete(expiry[0]);expiry[1].run();}
  const waiting={closed,socketCount,hudAvailable:mirror.sendHud('hud.status',{}),foregroundReads:live.calls.filter(c=>c.command==='foreground-read').length,pendingLookup:live.calls.filter(c=>c.command==='lookup').length,acks:live.frames.filter(f=>f.type==='wallpaper-context-accepted').map(f=>[f.contextSeq,f.appId,f.foreground]),newFrames:live.output().filter(f=>f.contextSeq===nextSeq).length};
  console.log('WALLPAPER_SLOW_CARD '+JSON.stringify({stage,...waiting}));
  same(waiting,{closed:false,socketCount:1,hudAvailable:true,foregroundReads:stage==='settings'?0:stage==='foreground'?2:1,pendingLookup:stage==='settings'?0:stage==='foreground'?1:2,acks:[[nextSeq,'1091500',stage==='asset']],newFrames:0},'a slow wallpaper dependency must not close the shared socket or authorize a frame');
  same(ackPresented,0,'transport acknowledgement never reaches a render consumer');same(hudSent,1,'HUD still works before media resolves');
  if(stage==='settings'){
   same(live.calls.filter(c=>['inventory','gameMedia.shortcut','ensure'].includes(c.command)).length,0,'waiting for settings has no media side effects');
   live.host.ready();await settle();
   same(live.calls.filter(c=>c.command==='lookup').map(c=>c.target.appId),[1091500],'ready only reads the latest retained card');
  }
  proof.resolve(nativeProof());await settle();
  pending.resolve(pairedEntry(1091500,7));await settle();same(live.output().at(-1).entry.assetId,'asset-1091500','slow current selection still publishes when ready');
  same(timers.size,0,'the recovered selection leaves no connection retry or deadline');
 }finally{off();mirror.dispose();live.host.stop();proof.resolve(null);pending.resolve(pairedEntry(1091500));await settle();}
 same(timers.size,0,'client disposal owns all deadlines');
 }
});
await run('a rejected trailing lookup retains only the original slow recovery deadline',async()=>{
 const head=deferred(),tail=deferred();let reads=0;
 const f=fixture({lookup:async()=>++reads===1?head.promise as Promise<GameMediaEntry>:reads===2?tail.promise as Promise<GameMediaEntry>:pairedEntry(570,4)});
 try{
  f.context();await settle();f.change('asset','steam-570');await settle();head.resolve(pairedEntry());await settle();same(reads,2,'one trailing lookup has actually started');
  tail.reject(Error('inert trailing cache receipt refused'));await settle();same(reads,2,'rejection does not spin another lookup');same(f.output().at(-1).entry,null,'unavailable current cache is not replaced with old pending cache');same(f.calls.filter(c=>c.command==='ensure').length,0,'read failure never authorizes preparation');same([...f.timers.values()].map(t=>t.ms),[20000],'original selected-cache retry remains bounded');
  await f.fire();same(reads,3,'existing slow retry can read fresh cache');same(f.output().at(-1).entry.revision,'4','recovered current cache is published');same(f.timers.size,0,'successful recovery is idle');
 }finally{f.host.stop();head.resolve(pairedEntry());tail.resolve(pairedEntry());await settle();}
});
await run('real notifications during a trailing read retain one further fresh tail',async()=>{
 const head=deferred(),tail=deferred();let reads=0,latest=pairedEntry(570,2);
 const f=fixture({lookup:async()=>++reads===1?head.promise as Promise<GameMediaEntry>:reads===2?tail.promise as Promise<GameMediaEntry>:latest});
 try{
  f.context();await settle();f.change('asset','steam-570');await settle();head.resolve(pairedEntry());await settle();same(reads,2,'first dirty event starts the trailing read');
  for(let n=0;n<32;n++){latest=pairedEntry(570,n+3);f.change('asset','steam-570');await settle();}
  same(reads,2,'trailing read also coalesces cross-task notifications');tail.resolve(pairedEntry(570,2));await settle();same(reads,3,'new real events own exactly one further fresh read');same(f.output().length,1,'both obsolete results remain fenced');same(f.output().at(-1).entry.revision,'34','newest changed asset wins');same(f.timers.size,0,'nothing repeats without another real event');await settle();same(reads,3,'idle completion does not invent another tail');
 }finally{f.host.stop();head.resolve(pairedEntry());tail.resolve(pairedEntry());await settle();}
});
for(const cause of ['off','hidden','peer','resume','stop'] as const)await run('dirty publication cancellation synchronously revokes pending lookup and ensure: '+cause,async()=>{
 for(const stage of ['lookup','ensure'] as const){
  const pending=deferred();const f=fixture({lookup:async()=>stage==='lookup'?pending.promise as Promise<GameMediaEntry>:entry(),ensure:async()=>pending.promise as Promise<GameMediaEntry>});
  try{
   f.context();await settle();for(let n=0;n<8;n++){f.change('asset','steam-570');await settle();}
   if(cause==='off'){f.enable(false);f.change('settings');}else if(cause==='hidden')f.context('570',2,false);else if(cause==='peer')f.connect({connection:'2',runId:'b'.repeat(32)});else if(cause==='resume')f.resume(2);else f.host.stop();
   await settle();const io=()=>f.calls.filter(c=>c.command!=='selectSteamTarget').length,before=io(),frames=f.output().length;
   for(let n=0;n<32;n++){f.change('asset','steam-570');f.change('settings');await settle();}
   same(io(),before,'revoked '+stage+' notifications perform zero native/media IO');pending.resolve(pairedEntry(570,99));await settle();same(io(),before,'revoked '+stage+' settlement cannot start a tail');same(f.output().length,frames,'revoked '+stage+' settlement cannot publish');same(f.timers.size,0,'revoked '+stage+' has no timer');
  }finally{f.host.stop();pending.resolve(pairedEntry());await settle();}
 }
});
await run('OFF then ON in the same context bypasses either kind of old pending publication',async()=>{
 for(const stage of ['lookup','ensure'] as const){
  const pending=deferred();let old=true,reads=0,prepares=0;
  const f=fixture({lookup:async()=>{reads++;return old&&stage==='lookup'?pending.promise as Promise<GameMediaEntry>:old?entry():pairedEntry(570,5);},ensure:async()=>{prepares++;return pending.promise as Promise<GameMediaEntry>;}});
  try{
   f.context();await settle();f.change('asset','steam-570');await settle();f.enable(false);f.change('settings');await settle();same(f.output().at(-1).enabled,false,'OFF is immediate while '+stage+' is hung');
   old=false;f.enable(true);f.change('settings');await settle();same(reads,2,'new ON begins a fresh cache read without awaiting old '+stage);same(f.output().at(-1).entry.revision,'5','new ON displays current cache');same(prepares,stage==='ensure'?1:0,'new ON does not replay pending preparation');
   const calls=f.calls.length;pending.resolve(pairedEntry(570,99));await settle();same(f.calls.length,calls,'old '+stage+' completion cannot create a new tail');same(f.output().at(-1).entry.revision,'5','new ON remains authoritative');same(f.timers.size,0,'new ON is idle');
  }finally{f.host.stop();pending.resolve(pairedEntry());await settle();}
 }
});

// Match Authority::observe's exact negative reply when a native revision
// changes between opening its ticket and committing its foreground proof.
const racedForeground=nativeProof({known:false,foreground:false,current:false,mode:'unknown',pid:0,birth:'0',window:'0',reason:'foreground-changed-during-read'});
await run('R18 recovery: foreground transport failure retries same live demand without a native event',async()=>{
 for(const mode of ['timeout','revision-race','revision-race-then-blur']){
  let reads=0;
  const f=fixture({foregroundRead:async()=>{
   if(++reads===1){if(mode==='timeout')throw Error('one native timeout');return racedForeground;}
   return nativeProof();
  },lookup:async target=>entry(target.appId),ensure:async target=>entry(target.appId)});
  try{
   f.context();await settle();
   same(f.calls.filter(c=>['lookup','ensure'].includes(c.command)).length,0,'failed proof never grants media: '+mode);
   same(f.timers.size,1,'one bounded proof repair belongs to visible demand: '+mode);
   same([...f.timers.values()][0].ms,1000,'first repair is delayed');
   if(mode!=='timeout'){
    const race='foreground-changed-during-read';
    same(f.output().at(-1).status,race,'a raced read is labeled separately from a transport timeout');
    f.context('730',2,true);await settle();
    same([reads,f.output().at(-1).contextSeq,f.output().at(-1).status],[1,2,race],'card change acknowledges latest selection without refilling the race budget');
   }
   if(mode==='revision-race-then-blur'){
    const late=[...f.timers.values()][0].fn;
    f.foreground(nativeProof({foreground:false,current:false,ownerEpoch:'blur',revision:2}));await settle();late();await settle();
    same([reads,f.timers.size,f.calls.filter(c=>c.command==='lookup').length],[1,0,0],'authoritative blur wins over queued repair');continue;
   }
   await f.fire();same(reads,2,'same peer retries actual reader without another WinEvent');
   same(f.calls.filter(c=>c.command==='lookup').map(c=>c.target.appId),[mode==='timeout'?570:730],'only the latest card is read after repair');
   same(f.output().at(-1).entry.assetId,mode==='timeout'?'asset-570':'asset-730','current proof recovers current selection');
   same(f.timers.size,0,'successful foreground leaves no proof poll');
  }finally{f.host.stop();}
 }
 const proof=nativeProof({ownerEpoch:'focus-2',revision:2});
 const notice={...racedForeground,ownerEpoch:proof.ownerEpoch,revision:proof.revision};
 const queryCount=(f:ReturnType<typeof fixture>)=>f.calls.filter(c=>c.command==='foreground-read').length;
 const mediaCount=(f:ReturnType<typeof fixture>)=>f.calls.filter(c=>['lookup','ensure','inventory'].includes(c.command)).length;
 {
  const initial=deferred();let reads=0;
  const f=fixture({foregroundRead:()=>++reads===1?initial.promise:Promise.resolve(proof),lookup:async target=>entry(target.appId),ensure:async target=>entry(target.appId)});
  try{
   f.context();await settle();f.foreground(notice);f.context('730',2);await settle();
   same([queryCount(f),mediaCount(f),f.output().at(-1).status],[1,0,'foreground-changed-during-read'],'raced notice keeps the current read alive and acknowledges the latest card');
   initial.resolve(nativeProof());await settle();
   same([mediaCount(f),[...f.timers.values()].map(t=>t.ms)],[0,[1000]],'a pre-notice proof cannot authorize media; original retry repairs the race');
   for(let n=0;n<32;n++)f.foreground(notice);await settle();
   same([queryCount(f),f.timers.size],[1,1],'raced notice burst cannot refill or interrupt the scheduled retry');
   await f.fire();
   same([queryCount(f),f.output().at(-1).entry.assetId,f.timers.size],[2,'asset-730',0],'same peer recovers only the latest card from confirmed proof');
  }finally{f.host.stop();initial.resolve(null);}
 }
 {
  const newer=deferred();let reads=0;
  const f=fixture({foregroundRead:()=>++reads===1?Promise.resolve(nativeProof()):newer.promise});
  try{
   f.context();await settle();const before=f.frames.length,media=mediaCount(f);
   for(let n=0;n<32;n++)f.foreground(racedForeground);await settle();
   same([queryCount(f),mediaCount(f),f.frames.length],[1,media,before],'read race at an already confirmed revision neither stops video nor reads again');
   f.foreground(notice);await settle();
   same([queryCount(f),mediaCount(f),f.output().at(-1).entry,f.output().at(-1).foreground],[2,media,null,false],'a newer unconfirmed revision retires the old frame and starts one proof read');
   for(let n=0;n<32;n++)f.foreground(notice);await settle();
   same([queryCount(f),f.timers.size],[2,0],'raced notification burst shares one in-flight read');
   f.foreground(nativeProof({foreground:false,current:false,ownerEpoch:'focus-3',revision:3}));
   newer.resolve(proof);await settle();
   same([queryCount(f),mediaCount(f),f.output().at(-1).entry,f.timers.size],[2,media,null,0],'a real newer blur retires the raced proof and any recovery');
  }finally{f.host.stop();newer.resolve(null);}
 }
});
await run('R18 recovery: repeated transport failure exhausts a finite budget without an asset-event loop',async()=>{
 let reads=0,seq=1,release!:()=>void;
 const f=fixture({foregroundRead:async()=>{if(++reads===2)await new Promise<void>(resolve=>release=resolve);throw Error('reader offline');}});
 const move=()=>{for(let n=0;n<32;n++){f.change('asset');f.context(n%2?'730':'570',++seq);}};
 try{
  f.context();await settle();
  for(const delay of [1000,5000,20000]){
   same(f.timers.size,1,'only one repair timer');const [id,queued]=[...f.timers][0];same(queued.ms,delay,'bounded original recovery delays');
   const before=reads;move();await settle();same(reads,before,'visible card changes cannot bypass backoff');same(f.timers.get(id),queued,'keeps the original timeout and owner');
   const ack=f.output().at(-1);
   same([ack?.contextSeq,ack?.appId,ack?.enabled,ack?.foreground,ack?.entry,ack?.status],
    [seq,'730',true,false,null,'foreground-transport-unavailable'],'latest card must get a non-playing reply during proof backoff');
   await f.fire();same(reads,before+1,'one actual read at one retry');
   if(delay===1000){move();await settle();same(reads,2,'card changes share the in-flight retry');release();await settle();}
  }
  same(reads,4,'initial plus three repairs only');same(f.timers.size,0,'exhausted demand stops retry timers');
  move();move();await settle();same(reads,4,'card and asset notifications cannot restart exhausted transport');
  const exhausted=f.output().at(-1);
  same([exhausted?.contextSeq,exhausted?.appId,exhausted?.foreground,exhausted?.entry],
   [seq,'730',false,null],'exhausted proof still acknowledges the latest selection');
  const sent=f.frames.length;for(let n=0;n<32;n++)f.change('asset');await settle();
  same(f.frames.length,sent,'unchanged unavailable replies are not republished');
  same(f.calls.filter(c=>['lookup','ensure'].includes(c.command)).length,0,'exhaustion never authorizes media');
  f.context('730',++seq,false);f.context('730',++seq,true);await settle();
  same(reads,5,'actual return to visible owns a fresh read');same([...f.timers.values()][0].ms,1000,'new visible interval has its own budget');
 }finally{f.host.stop();}
});
await run('R18 recovery: authoritative false or unknown never schedules transport repair',async()=>{for(const proof of [null,nativeProof({foreground:false}),nativeProof({known:false,current:false})]){const f=fixture({foreground:proof});try{f.context();await settle();for(let n=0;n<32;n++)f.change('asset');await settle();same(f.timers.size,0,'actual native veto is not a transport fault');same(f.calls.filter(c=>c.command==='foreground-read').length,1,'no recurring native veto reads');same(f.calls.filter(c=>['lookup','ensure'].includes(c.command)).length,0,'native veto stays denied');}finally{f.host.stop();}}});
for(const cause of ['off','hidden','peer','power','stop'] as const)await run('R18 recovery: queued proof repair is cancelled by '+cause,async()=>{let reads=0;const f=fixture({foregroundRead:async()=>{if(++reads===1)throw Error('one failure');return nativeProof();}});try{f.context();await settle();const late=[...f.timers.values()][0]?.fn;assert.ok(late,'failed reader needs an owned repair');if(cause==='off'){f.enable(false);f.change('settings');}else if(cause==='hidden')f.context('570',2,false);else if(cause==='peer')f.connect({connection:'2',runId:'b'.repeat(32)});else if(cause==='power')f.resume(2);else f.host.stop();await settle();const count=f.calls.length;late();await settle();same(f.calls.length,count,'late repair cannot read a withdrawn owner');same(f.timers.size,0,'withdrawal cancels the repair');}finally{f.host.stop();}});
await run('R18 recovery: a new context owns recovery and a queued old context stays inert',async()=>{
 let reads=0;const f=fixture({foregroundRead:async()=>{if(++reads===1)throw Error('first context fails');return nativeProof();}});
 try{
  f.context();await settle();const retry=[...f.timers.values()][0]?.fn;assert.ok(retry,'visible peer owns one timer');
  f.context('730',2,true);await settle();same(reads,1,'visible selection carries the existing foreground retry');
  same(f.calls.filter(c=>c.command==='lookup').length,0,'new card waits for proof');
  await f.fire();same(f.output().at(-1).appId,'730','one retry grants proof only to the latest visible selection');
  same(f.calls.filter(c=>c.command==='lookup').map(c=>c.target.appId),[730],'the old card is never read');
  const count=f.calls.length;retry();await settle();same(f.calls.length,count,'consumed callback cannot replace current output');
  same(f.timers.size,0,'new success leaves no scheduled read');
 }finally{f.host.stop();}
});
await run('R18 recovery: late repaired proof cannot grant playback after native blur',async()=>{let reads=0,release!:(v:any)=>void;const f=fixture({foregroundRead:async()=>{if(++reads===1)throw Error('first read failed');return new Promise(r=>release=r);}});try{f.context();await settle();await f.fire();f.foreground(nativeProof({foreground:false,current:false,ownerEpoch:'blur-new',revision:2}));release(nativeProof());await settle();same(f.calls.filter(c=>['lookup','ensure'].includes(c.command)).length,0,'old positive transport receipt cannot override new native blur');same(f.timers.size,0,'native veto cancels repair work');}finally{f.host.stop();}});
await run('R18 recovery: retry notifications route only current native-ticket Steam selection',async()=>{const f=fixture();try{f.context();await settle();const before=f.calls.filter(c=>c.command==='ensure').length;f.change('retry');await settle();same(f.calls.filter(c=>c.command==='ensure').length,before+1,'current owner receives one retry');const attempt=f.calls.filter(c=>c.command==='ensure').at(-1);same(attempt.target.appId,570,'retry uses selected game');same([attempt.demand.demandConsumer,attempt.demand.demandEpoch,attempt.demand.demandRevision],['steam','focus-1',1],'retry retains actual current native proof');f.foreground(nativeProof({foreground:false,current:false,ownerEpoch:'blur',revision:2}));const queries=()=>f.calls.filter(c=>['lookup','ensure','foreground-read'].includes(c.command)).length;const count=queries();f.change('retry');await settle();same(queries(),count,'blurred selection cannot retry');f.enable(false);f.change('settings');const off=queries();f.change('retry');await settle();same(queries(),off,'OFF cannot retry');}finally{f.host.stop();}});
await run('R18 recovery: pending foreground read follows only the latest visible selection',async()=>{
 for(const outcome of ['proof','failure','hidden','off','peer','power'] as const){
  const attempts:ReturnType<typeof deferred>[]=[];
  const f=fixture({foregroundRead:()=>{const work=deferred();attempts.push(work);return work.promise;}});
  try{
   f.context('570',1);await settle();
   for(let seq=2;seq<=64;seq++)f.context(String(1000+seq),seq);
   same(attempts.length,1,'visible selection storm must share exactly one pending foreground read');
   same(f.calls.filter(c=>c.command==='lookup'||c.command==='ensure'||c.command==='inventory').length,0,'no media work before proof');
   if(outcome==='proof'){
    attempts[0].resolve(nativeProof());await settle();
    same(f.calls.filter(c=>c.command==='lookup').map(c=>c.target.appId),[1064],'proof runs the latest selection only');
    same([f.output().at(-1).contextSeq,f.output().at(-1).appId],[64,'1064'],'only the latest context receives the proof result');
    same(f.timers.size,0,'success owns no repair timer');
   }else if(outcome==='failure'){
    attempts[0].reject(Error('held foreground read failed'));await settle();
    const failure=f.output().at(-1);
    same([failure.contextSeq,failure.appId,failure.status],[64,'1064','foreground-transport-unavailable'],'failure acknowledges the latest context');
    same([...f.timers.values()].map(t=>t.ms),[1000],'failure retains one original retry');
    await f.fire();same(attempts.length,2,'retry starts only after original delay');
    attempts[1].resolve(nativeProof());await settle();
    same(f.calls.filter(c=>c.command==='lookup').map(c=>c.target.appId),[1064],'repaired proof still reads latest media only');
   }else{
    if(outcome==='hidden')f.context('1064',65,false);
    if(outcome==='off'){f.enable(false);f.change('settings');}
    if(outcome==='peer')f.connect({connection:'2',runId:'b'.repeat(32)});
    if(outcome==='power')f.resume(2);
    await settle();const before=f.frames.length;
    attempts[0].resolve(nativeProof());await settle();
    same(f.frames.length,before,'withdrawal retires the reused proof: '+outcome);
    same(f.calls.filter(c=>c.command==='lookup'||c.command==='ensure').length,0,'retired proof cannot read media: '+outcome);
    same(f.timers.size,0,'retired proof cannot start a retry: '+outcome);
   }
  }finally{f.host.stop();for(const attempt of attempts)attempt.resolve(null);await settle();}
 }
});
if(exactCase!==null)assert.equal(cases,1,'exact case must select one original case');
const stabilityOk=!stabilityRecoveryOnly||stabilityRecoveryRows.length>0&&stabilityRecoveryRows.every(x=>x.ok);if(!stabilityOk)process.exitCode=1;
console.log(JSON.stringify({ok:stabilityOk,selection:exactCase??(stabilityRecoveryOnly?'stability-recovery':'default'),stabilityRecoveryRows,cases,checks,scope:'production Steam wallpaper host; injected IPC/media ports, no installed Steam mutation'}));
