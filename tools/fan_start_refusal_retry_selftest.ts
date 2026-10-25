/** Production lifecycle with inert Host, native power and owned-process boundaries. */
import fs from 'node:fs';
import {FanHostLifecycle,type FanHostProcess} from '../src/bridge/fanHost';
import {FanApiError,type FanNode} from '../src/bridge/fanApi';
import {NoIoHost} from './fixtures/fan_no_io_host';
const curve:FanNode[]=[{tempC:0,dutyPercent:100},{tempC:40,dutyPercent:100},{tempC:70,dutyPercent:100},{tempC:100,dutyPercent:100}];
type Mode='healthy'|'auth'|'unsupported'|'unmapped'|'transient-once';
const delay=(ms:number)=>new Promise<void>(resolve=>setTimeout(resolve,ms));
function fixture(initial:Mode,generation=0,adopted=false){
 const host=new NoIoHost();let mode=initial,starts=0,stops=0,alive:FanHostProcess|null=null,exitDenied=false,reuseExisting=false;let handshakeHold:Promise<void>|null=null;const trace:string[]=[];
 const original=host.handshake.bind(host);
 host.handshake=async()=>{trace.push('handshake:'+mode);if(handshakeHold)await handshakeHold;
  if(mode==='auth')throw new FanApiError('fixture session refused',401,'API_SESSION_REQUIRED');
  if(mode==='transient-once'){mode='healthy';throw new Error('fixture transport temporarily unavailable');}
  const value=await original();
  if(mode==='unsupported')return {...value,supported:false,reason:'fixture current capability admission refused'};
  if(mode==='unmapped')return {...value,deviceClass:'fixture.Unmapped',fanRoute:'',fanRouteWriteReady:false};
  return value;
 };
 const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:0,resumeWaitDeadlineMs:400,
  readNativeActivity:async()=>null,
  readNativePowerState:async()=>({generation,phase:'ready',hardwareWritesAllowed:true,inputReady:true,resumeReady:generation>0,hibernateAvailable:true}),
  launcher:{async start(){if(alive){if(adopted||reuseExisting){trace.push('adopt:'+alive.pid);return {...alive,adopted:true};}throw new Error('fixture second live Host forbidden');}starts++;trace.push('launch:'+starts);
   host.stateName='AwaitingControl';host.powerState='Unknown';host.opened=host.events=host.closed=false;host.lease=null;host.generation=host.attempt=0;
   alive={pid:104300+starts,executable:'inert-fan.exe',processCreated:'fixture-birth-'+starts,adopted};return alive;
  },async stop(process){if(!alive||process.pid!==alive.pid||process.processCreated!==alive.processCreated||process.executable!==alive.executable)throw new Error('fixture wrong process identity');if(exitDenied){trace.push('exit-unconfirmed');throw new Error('fixture exact exit not confirmed');}stops++;trace.push('exit:'+process.pid);alive=null;}}});
 (life as unknown as {config:{sessionToken:string}}).config.sessionToken='a'.repeat(64);
 if(generation>0){life.setPowerGeneration(generation);life.observePowerBoundary('resume-ready',generation);}
 return {life,host,trace,reuseExisting:(value:boolean)=>{reuseExisting=value;},denyExit:(value:boolean)=>{exitDenied=value;},holdHandshake:(wait:Promise<void>|null)=>{handshakeHold=wait;},mode:(value:Mode)=>{mode=value;},starts:()=>starts,stops:()=>stops,alive:()=>alive,
  status:()=>{const view=life as unknown as {recoveryState:string;recoveryRetryTimer:unknown};return {state:view.recoveryState,timer:view.recoveryRetryTimer!==null};}};
}
const results:Array<{name:string;passed:boolean;error?:string;trace:string[]}>=[];
function assert(value:unknown,message:string):asserts value{if(!value)throw new Error(message);}
async function until(test:()=>boolean,ms=1800){const end=Date.now()+ms;while(!test()&&Date.now()<end)await delay(5);assert(test(),'fixture progress deadline');}
async function run(name:string,work:(f:ReturnType<typeof fixture>)=>Promise<void>,mode:Mode,generation=0,adopted=false){
 const f=fixture(mode,generation,adopted);try{await work(f);results.push({name,passed:true,trace:[...f.trace]});}
 catch(error){results.push({name,passed:false,error:String(error),trace:[...f.trace]});}
 finally{f.mode('healthy');f.denyExit(false);f.holdHandshake(null);await f.life.close().catch(()=>undefined);}
}
for(const mode of ['auth','unsupported','unmapped'] as const)await run(mode+' startup refusal retires its child and the next direct manual curve works',async f=>{
 const first=await f.life.apply(curve).then(()=>null,error=>error);const firstState=f.life.state;
 assert(first!==null,'current rejected startup must not write');assert(f.host.calls.filter(c=>c==='enable').length===0,'denied startup cannot submit curve');
 assert(f.alive()===null&&f.stops()===1,'only the exact owned child may retire');
 f.mode('healthy');const retry=await f.life.apply(curve).then(()=> 'accepted',error=>String(error));
 assert(retry==='accepted','next direct manual request blocked by stale '+firstState+': '+retry);
 assert(firstState==='stopped','confirmed child exit must clear startup phase');assert(f.starts()===2,'one fresh child per explicit attempt');
 assert(f.host.calls.filter(c=>c==='enable').length===1,'only corrected admission may write');
},mode);
await run('one transient startup failure uses the existing bounded retry without a second click',async f=>{
 const result=await f.life.apply(curve).then(()=> 'accepted',error=>String(error));
 assert(result==='accepted','same manual request failed: '+result);assert(f.starts()===2&&f.stops()===1,'exactly one bounded replacement');
 assert(f.host.calls.filter(c=>c==='enable').length===1,'write once after retry');
},'transient-once');
await run('repeated authentication refusal stays write-closed and a later corrected manual request remains reachable',async f=>{
 for(let n=0;n<2;n++){const error=await f.life.apply(curve).then(()=>null,error=>error);assert(error instanceof FanApiError&&error.status===401,'preserve the current auth error');}
 assert(f.starts()===2&&f.stops()===2,'each explicit auth attempt cleans its own child');assert(!f.host.calls.includes('enable'),'auth never writes');
 f.mode('healthy');await f.life.apply(curve);assert(f.starts()===3,'corrected manual request starts anew');
},'auth');
for(const mode of ['unsupported','unmapped'] as const)await run(mode+' recovery refusal stops automatic relaunch, retains intent and permits a later manual retry',async f=>{
 f.life.rememberDesiredCurve(curve);assert(await f.life.requestRecoveryAfterObservedHostState('fixture-old-host-lost'),'register real recovery owner');
 await until(()=>f.starts()>=1);await delay(800);
 const observed=f.status();assert(f.starts()===1,'stable admission denial repeatedly launched '+f.starts()+' children');
 assert(observed.state==='needs-attention'&&!observed.timer,'denied owner must stop its automatic timer');assert(f.life.hasControlIntent,'keep user intent for explicit retry');
 await f.life.requestRecoveryAfterObservedHostState('fixture-repeat-telemetry');await delay(300);assert(f.starts()===1,'telemetry cannot silently rearm denied owner');
 f.mode('healthy');await f.life.start({manualRecovery:true});await f.life.apply(curve);
 assert(f.starts()===2&&f.host.calls.filter(c=>c==='enable').length===1,'new manual request rechecks and writes once');
},mode,6);
await run('transient recovery transport failure still retries and writes the retained curve',async f=>{
 f.life.rememberDesiredCurve(curve);assert(await f.life.requestRecoveryAfterObservedHostState('fixture-transient-rebuild'),'register recovery');
 await until(()=>f.host.calls.includes('enable')&&!f.life.recoveryActive,2500);
 assert(f.starts()===2,'transient rebuild must retry once');assert(f.host.calls.filter(c=>c==='enable').length===1,'retained curve written once');
 assert(f.status().state==='controlled','do not misclassify transient failure as terminal');
},'transient-once',6);
await run('unconfirmed owned-child exit never becomes Stopped or a second Host',async f=>{
 f.denyExit(true);await f.life.apply(curve).catch(()=>undefined);
 assert(f.alive()!==null&&f.stops()===0,'unconfirmed owner must be retained');assert(f.life.state!=='stopped','no false stopped acknowledgement');
 assert(f.starts()===1&&!f.host.calls.includes('enable'),'no second writer');
},'auth');
await run('an adopted owner rejected during startup is retained rather than marked stopped',async f=>{
 await f.life.start({manualRecovery:true}).catch(()=>undefined);
 assert(f.alive()!==null&&f.stops()===0,'do not retire an unconfirmed adopted session');assert(f.life.state!=='stopped','adopted refusal is not confirmed process exit');
 f.mode('healthy');await f.life.start({manualRecovery:true});await f.life.apply(curve);
 assert(f.starts()===1&&f.host.calls.filter(c=>c==='enable').length===1,'same adopted owner can be explicitly rechecked');
},'auth',0,true);
for(const cancel of ['off','sleep'])await run(cancel+' during a failing handshake cannot revive a write or overwrite the newer sleep phase',async f=>{
 let release!:()=>void;f.holdHandshake(new Promise<void>(resolve=>{release=resolve;}));const pending=f.life.apply(curve).then(()=>null,error=>error);
 let cancelled:Promise<unknown>=Promise.resolve();
 try{await until(()=>f.trace.some(t=>t==='handshake:auth'));
  if(cancel==='off')cancelled=f.life.disable();else{f.life.setPowerGeneration(1);f.life.observePowerBoundary('suspending',1);}
 }finally{release();}
 await pending;await cancelled;assert(!f.host.calls.includes('enable')&&f.starts()===1,'cancelled handshake must not restart/write');
 if(cancel==='sleep')assert(f.life.state==='suspended','handshake failure must not overwrite newer sleep state');
},'auth');

// A native launcher may re-adopt the exact resident process. The fixture counts
// this separately from a fresh child, preserving the no-second-writer assertion.
for(const adopted of [true,false])for(const action of ['apply','preset'] as const)await run((adopted?'adopted':'retained-owned')+' rejected startup: next direct '+action+' rechecks one exact owner',async f=>{
 if(!adopted)f.denyExit(true);await f.life.start({manualRecovery:true}).catch(()=>undefined);
 assert(f.alive()!==null&&f.life.state==='handshaking','fixture must retain the unfinished admission');const owner=f.alive();
 f.denyExit(false);f.mode('healthy');f.reuseExisting(true);
 const result=await (action==='apply'?f.life.apply(curve):f.life.applyPreset('balanced',curve)).then(()=> 'accepted',error=>String(error));
 assert(result==='accepted','next direct request blocked: '+result);assert(f.alive()===owner&&f.starts()===1&&f.stops()===0,'re-adopt original exact process without a second child');
 assert(f.host.calls.filter(x=>x===(action==='apply'?'enable':'preset')).length===1,'one admitted write');assert(f.trace.filter(x=>x.startsWith('handshake:healthy')).length===1,'manual retry must handshake again');
},'auth',0,adopted);
for(const action of ['apply','preset'] as const)await run('retained adopted auth denial stays closed on direct '+action+' retries',async f=>{
 await f.life.start({manualRecovery:true}).catch(()=>undefined);for(let i=0;i<2;i++){const error=await (action==='apply'?f.life.apply(curve):f.life.applyPreset('balanced',curve)).then(()=>null,error=>error);assert(error instanceof FanApiError&&error.status===401,'each explicit retry must preserve current auth failure');}
 assert(f.starts()===1&&f.stops()===0,'recheck original resident, no new child');assert(!f.host.calls.some(x=>['open','events','acquire','enable','preset'].includes(x)),'auth refusal may not touch an HC session');
},'auth',0,true);


for(const action of ['apply','preset'] as const)await run('native re-adoption refusal retains one owner and a later '+action+' retries unfinished Starting',async f=>{
 f.denyExit(true);await f.life.start({manualRecovery:true}).catch(()=>undefined);f.mode('healthy');
 const refused=await (action==='apply'?f.life.apply(curve):f.life.applyPreset('balanced',curve)).then(()=>null,error=>error);
 assert(refused!==null,'current native ownership admission remains refused');assert(f.starts()===1&&f.stops()===0&&!f.host.calls.some(x=>['open','events','acquire','enable','preset'].includes(x)),'no duplicate child or HC operation after rejected re-adoption');
 f.reuseExisting(true);f.denyExit(false);const retried=await (action==='apply'?f.life.apply(curve):f.life.applyPreset('balanced',curve)).then(()=> 'accepted',error=>String(error));assert(retried==='accepted','next explicit operation was trapped in '+f.life.state+': '+retried);assert(f.starts()===1,'one exact resident owner');
},'auth');
for(const cancel of ['off','sleep'] as const)for(const action of ['apply','preset'] as const)await run(cancel+' cancels retained-owner '+action+' while its fresh handshake is pending',async f=>{
 await f.life.start({manualRecovery:true}).catch(()=>undefined);f.mode('healthy');let release!:()=>void;const held=new Promise<void>(resolve=>{release=resolve;});f.holdHandshake(held);
 const pending=(action==='apply'?f.life.apply(curve):f.life.applyPreset('balanced',curve)).then(()=> 'accepted',error=>String(error));await until(()=>f.trace.includes('handshake:healthy'));
 let off:Promise<unknown>|null=null;if(cancel==='off')off=f.life.disable().catch(()=>undefined);else{f.life.setPowerGeneration(1);f.life.observePowerBoundary('suspending',1);}
 release();f.holdHandshake(null);const result=await pending;if(off)await off;
 assert(result!=='accepted','cancelled request must not complete a control write');assert(!f.host.calls.some(x=>['open','events','acquire','enable','preset'].includes(x)),'late handshake cannot revive an HC write');assert(f.starts()===1&&f.stops()===0,'cancellation cannot create another Host');
},'auth',0,true);

const report={ok:results.every(r=>r.passed),checks:results.length,hardwareCalls:0,scope:'real public apply/start and recovery-owner scheduling; inert adapter/native power/exact child process fixtures; no hardware or CPU benchmark',results};
if(process.argv[2])fs.writeFileSync(process.argv[2],JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));process.exitCode=report.ok?0:1;
