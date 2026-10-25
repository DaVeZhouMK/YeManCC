/** Real public control/heartbeat paths; virtual clock, lease-enforcing Host, no device I/O. */
import assert from 'node:assert/strict';
import {FanHostLifecycle} from '../src/bridge/fanHost';
import {FanApiError,type FanNode,type FanState,type FanLease} from '../src/bridge/fanApi';
import {NoIoHost} from './fixtures/fan_no_io_host';
const curve=(duty:number)=>[0,40,70,100].map(tempC=>({tempC,dutyPercent:duty}));
const gate=<T>()=>{let resolve!:(v:T)=>void,reject!:(e:Error)=>void,entered!:()=>void;return{promise:new Promise<T>((r,j)=>{resolve=r;reject=j;}),ready:new Promise<void>(r=>entered=r),resolve:(v:T)=>resolve(v),reject:(e:Error)=>reject(e),entered:()=>entered()};};
const outcome=<T>(p:Promise<T>)=>p.then(value=>({value,error:null as unknown}),error=>({value:null,error}));
class Clock{now=0;seq=0;timers=new Map<number,{due:number,period:number,fn:()=>void}>();add(fn:()=>void,ms:number,period=0){const id=++this.seq;this.timers.set(id,{due:this.now+ms,period,fn});return{id};}clear(h:any){this.timers.delete(h?.id);}async flush(){for(let i=0;i<180;i++)await Promise.resolve();}async advance(ms:number){const target=this.now+ms;for(let i=0;i<1000;i++){const next=[...this.timers.entries()].filter(([,t])=>t.due<=target).sort((a,b)=>a[1].due-b[1].due)[0];if(!next)break;this.now=next[1].due;if(next[1].period)next[1].due+=next[1].period;else this.timers.delete(next[0]);next[1].fn();await this.flush();}this.now=target;await this.flush();}}
type Op='apply'|'preset'|'heartbeat';
class Host extends NoIoHost{
 nodes:FanNode[]|null=null;controlled=false;refusals=0;readDenial:Error|null=null;readDenialOnRefusal:Error|null=null;acquireDenial:Error|null=null;denied:Partial<Record<Op,Error>>={};holdState:ReturnType<typeof gate<FanState>>|null=null;holdWrite:ReturnType<typeof gate<void>>|null=null;expiry:any=null;writes:number[]=[];
 constructor(readonly clock:Clock){super();}
 snapshot():FanState{return{...super.snapshot(),hardwareWritesEnabled:this.controlled&&!!this.lease,activeCurve:this.nodes,lastControlWriteEvidence:this.controlled?'compat-route-callback-returned-no-scope':'not-written',oemPhysicalOwnershipConfirmed:false,oemOwnershipStatus:'hc-callback-only-physical-unknown'};}
 async getState(){this.calls.push(this.readDenial?'state-denied':'state');if(this.holdState){const g=this.holdState;this.holdState=null;g.entered();return g.promise;}if(this.readDenial)throw this.readDenial;return this.snapshot();}
 arm(){this.clock.clear(this.expiry);const token=this.lease!.leaseId;this.expiry=this.clock.add(()=>{if(this.lease?.leaseId!==token)return;this.lease=null;this.controlled=false;this.stateName='AwaitingControl';},15000);}
 require(id?:string){if(!this.lease||id!==this.lease.leaseId||this.lease.expiresAtMonoMs!<=this.clock.now)throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');}
 async acquireControl(){this.calls.push(this.acquireDenial?'acquire-denied':'acquire');if(this.acquireDenial)throw this.acquireDenial;if(this.lease&&this.lease.expiresAtMonoMs!>this.clock.now)throw new FanApiError('LEASE_HELD',409,'LEASE_HELD');this.lease={leaseId:'inert-'+ ++this.sequence,generation:this.sequence,expiresAtMonoMs:this.clock.now+15000};this.arm();return{...this.lease};}
 async heartbeat(id?:string){this.calls.push(this.denied.heartbeat?'heartbeat-denied':'heartbeat');if(this.denied.heartbeat)throw this.denied.heartbeat;this.require(id);this.lease={...this.lease!,expiresAtMonoMs:this.clock.now+15000};this.arm();return{...this.lease};}
 async enable(nodes:readonly FanNode[]=[],id?:string){const error=this.denied.apply;if(error){this.calls.push('apply-denied');if(this.holdWrite){const g=this.holdWrite;this.holdWrite=null;g.entered();await g.promise;}throw error;}if(this.refusals-->0){this.calls.push('known-refusal');if(this.readDenialOnRefusal){this.readDenial=this.readDenialOnRefusal;this.readDenialOnRefusal=null;this.lease=null;this.controlled=false;this.stateName='AwaitingControl';this.clock.clear(this.expiry);}throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');}this.require(id);this.calls.push('enable');this.nodes=nodes.map(n=>({...n}));this.writes.push(nodes[0]?.dutyPercent);this.controlled=true;this.stateName='Ready';return this.snapshot();}
 async applyPreset(_name?:string,id?:string,nodes?:readonly FanNode[]){if(this.denied.preset){this.calls.push('preset-denied');throw this.denied.preset;}return this.enable(nodes,id);}
 async restoreOem(){this.calls.push('restore');this.controlled=false;return{...this.snapshot(),oemRestoreConfirmed:true};}
 async releaseControl(){this.calls.push('release');this.clock.clear(this.expiry);this.lease=null;this.stateName='AwaitingControl';return{...this.snapshot(),oemRestoreConfirmed:true};}
 async disable(){this.controlled=false;return super.disable();}
 async close(){this.clock.clear(this.expiry);this.controlled=false;return super.close();}
}
const families=[['ROG','HandheldCompanion.Devices.XboxROGAllyX','ProfileCurve'],['MSI','HandheldCompanion.Devices.MSI.ClawCG3EM','ProfileCurve'],['GPD','HandheldCompanion.Devices.GPDWin5','GenericDuty'],['OneX','HandheldCompanion.Devices.OneXPlayerX1Mini','GenericDuty'],['Lenovo','HandheldCompanion.Devices.Lenovo.LegionGo','ProfileCurve']] as const;
async function fixture(family:typeof families[number]=families[1]){
 const clock=new Clock(),saved={setTimeout,clearTimeout,setInterval,clearInterval},perf=Object.getOwnPropertyDescriptor(globalThis,'performance')!;const holds:{resolve:(v:any)=>void}[]=[];
 Object.assign(globalThis,{setTimeout:(f:()=>void,ms:number)=>clock.add(f,ms),clearTimeout:(h:any)=>clock.clear(h),setInterval:(f:()=>void,ms:number)=>clock.add(f,ms,ms),clearInterval:(h:any)=>clock.clear(h)});Object.defineProperty(globalThis,'performance',{configurable:true,value:{now:()=>clock.now}});
 let native:any={generation:6,phase:'ready',hardwareWritesAllowed:true,resumeReady:true,inputReady:true,hibernateAvailable:true};
  const host=new Host(clock);host.factoryType=family[1];const handshake=host.handshake.bind(host);host.handshake=async()=>({...await handshake(),fanRoute:family[2]});let starts=0;
 const life=new FanHostLifecycle({enabled:true,adapter:host,heartbeatIntervalMs:5000,readNativeActivity:async()=>null,readNativePowerState:async()=>native,launcher:{async start(){starts++;return{pid:116701,executable:'inert.exe'};},async stop(){}}});
 life.setPowerGeneration(6);life.observePowerBoundary('resume-ready',6);await life.start({manualRecovery:true});await life.apply(curve(100));host.generation=6;host.attempt=1;host.powerState='On';
 return{host,life,clock,starts:()=>starts,setNative:(value:any)=>{native=value;},observations:[] as unknown[],holdState(){const g=gate<FanState>();holds.push(g);host.holdState=g;return g;},holdWrite(){const g=gate<void>();holds.push(g);host.holdWrite=g;return g;},async heal(){host.refusals=2;await assert.rejects(life.apply(curve(65)));assert(life.recoveryActive);},async cleanup(){for(const g of holds)g.resolve(host.snapshot());await clock.flush();host.denied={};host.refusals=0;host.readDenial=host.readDenialOnRefusal=host.acquireDenial=null;await life.close().catch(()=>{});Object.assign(globalThis,saved);Object.defineProperty(globalThis,'performance',perf);}};
}
const result:any[]=[];
async function check(name:string,work:(f:Awaited<ReturnType<typeof fixture>>)=>Promise<unknown>,family:typeof families[number]=families[1]){if(process.env.YMCC_TERMINAL_CASE&&!name.includes(process.env.YMCC_TERMINAL_CASE))return;const f=await fixture(family);try{result.push({name,family:family[0],ok:true,evidence:await work(f)});}catch(e){result.push({name,family:family[0],ok:false,error:String(e),observations:f.observations,calls:f.host.calls.slice(),writes:f.host.writes});}finally{await f.cleanup();}}
const denials=[['session-code',()=>new FanApiError('session required',409,'API_SESSION_REQUIRED')],['401',()=>new FanApiError('session rejected',401,'AUTH_REJECTED')],['403',()=>new FanApiError('permission rejected',403,'API_PERMISSION_DENIED')],['unsupported',()=>new FanApiError('device unavailable',409,'FAN_UNSUPPORTED')],['gate-denied',()=>new FanApiError('device unavailable',409,'FAN_DEVICE_GATE_DENIED')],['invalid-handshake',()=>new FanApiError('invalid handshake',200,'FAN_API_RESPONSE_INVALID','/api/handshake')],['invalid-unknown-route',()=>new FanApiError('invalid observation',200,'FAN_API_RESPONSE_INVALID','/api/future-observation')],['protocol-incompatible',()=>new FanApiError('incompatible protocol',409,'FAN_PROTOCOL_INCOMPATIBLE','/api/handshake')],['route-code',()=>new FanApiError('another controller',409,'FAN_ROUTE_CONFLICT')],['openlib-code',()=>new FanApiError('another controller',409,'HC_OPENLIB_CONFLICT')],['owner-code',()=>new FanApiError('another controller',409,'EXTERNAL_FAN_OWNER')],['legacy-owner',()=>new Error('EXTERNAL_FAN_OWNER: another controller')]] as const;
async function main(){
 for(const family of families)for(const op of ['apply','preset','heartbeat'] as const)for(const [kind,make] of denials)await check(`${op} ${kind}: park, no automatic retry, fresh manual remains usable`,async f=>{
  const error=make();f.host.denied[op]=error;
  const reply=await outcome(op==='apply'?f.life.apply(curve(70)):op==='preset'?f.life.applyPreset('soft',curve(70)):f.life.heartbeat());
  const index=f.host.calls.lastIndexOf(op+'-denied'),after=f.host.calls.slice(index+1);f.observations.push({afterDenied:after,replyRejected:reply.error===error,needsAttention:f.life.recoveryNeedsAttention});
  assert.equal(reply.error,error,'explicit refusal became a successful automatic repair');assert.deepEqual(after,[],'explicit refusal submitted follow-up control/state work');
  assert(f.life.recoveryNeedsAttention);assert(!f.life.recoveryActive);assert(f.life.hasControlIntent);const count=f.host.calls.length;
  await f.clock.advance(1000);await f.life.requestRecoveryAfterObservedHostState('page-after-refusal');await f.clock.advance(5000);assert.equal(f.host.calls.length,count,'observation or maintenance revived a denied episode');
  f.host.denied={};assert((await f.life.start({manualRecovery:true})).allowed);await f.life.apply(curve(75));assert.deepEqual(f.host.nodes,curve(75));assert(!f.life.recoveryNeedsAttention);assert.equal(f.starts(),1);
  return{afterDenied:after,manualAfterRefusal:true,writes:f.host.writes,starts:f.starts()};
 },family);
 for(const op of ['apply','preset','heartbeat'] as const)for(const held of [false,true])await check(`${op}: terminal rejection retires ${held?'in-flight':'queued'} self-heal and manual does not await old read`,async f=>{
  await f.heal();let read:ReturnType<typeof gate<FanState>>|null=null;if(held){read=f.holdState();await f.clock.advance(250);await read.ready;}
  f.host.denied[op]=new FanApiError('permission rejected',403,'API_PERMISSION_DENIED');await outcome(op==='apply'?f.life.apply(curve(70)):op==='preset'?f.life.applyPreset('soft',curve(70)):f.life.heartbeat());
  assert(f.life.recoveryNeedsAttention);const before=f.host.calls.length,writes=f.host.writes.length;
  await f.clock.advance(1000);assert.equal(f.host.calls.length,before,'denial left a queued recovery active');
  f.host.denied={};assert((await f.life.start({manualRecovery:true})).allowed);await f.life.apply(curve(75));assert.deepEqual(f.host.nodes,curve(75));assert.equal(f.host.writes.length,writes+1);
  const after=f.host.calls.length;read?.resolve({...f.host.snapshot(),activeCurve:curve(65)});await f.clock.advance(1000);assert.equal(f.host.writes.length,writes+1,'old read wrote after terminal rejection');assert.deepEqual(f.host.nodes,curve(75));return{oldReadHeld:held,manualBeforeOldRead:true,extraWritesAfterOldRead:f.host.writes.length-writes-1,afterManualCalls:f.host.calls.slice(after)};
 });
 for(const edge of ['curve','off','generation','resuming'] as const)await check(`late denied write after ${edge} cannot park a newer request`,async f=>{
  const g=f.holdWrite();f.host.denied.apply=new FanApiError('old rejection',401,'AUTH_REJECTED');const work=outcome(f.life.apply(curve(65)));await g.ready;let off:Promise<unknown>|null=null;
  if(edge==='curve')f.life.stagePendingControlCurve(curve(75));else if(edge==='off')off=f.life.disable();else if(edge==='generation')f.life.setPowerGeneration(7);else f.life.observePowerBoundary('resuming',6);
  f.host.denied={};g.resolve();await work;if(off)await off;assert(!f.life.recoveryNeedsAttention,'retired refusal parked the new intent');
  if(edge==='curve'){await f.life.apply(curve(75));assert.deepEqual(f.host.nodes,curve(75));}return{retired:true,needsAttention:f.life.recoveryNeedsAttention};
 });
 for(const op of ['apply','preset','heartbeat'] as const)await check(`${op}: old read returning after refusal cannot revive control before any manual request`,async f=>{
  await f.heal();const read=f.holdState();await f.clock.advance(250);await read.ready;
  f.host.denied[op]=new FanApiError('refused',403,'API_PERMISSION_DENIED');await outcome(op==='apply'?f.life.apply(curve(70)):op==='preset'?f.life.applyPreset('soft',curve(70)):f.life.heartbeat());
  assert(f.life.recoveryNeedsAttention);const calls=f.host.calls.length,writes=f.host.writes.length;f.host.denied={};read.resolve(f.host.snapshot());await f.clock.advance(6000);
  assert.equal(f.host.calls.length,calls,'old read resumed queries/writes after terminal receipt');assert.equal(f.host.writes.length,writes);assert(f.life.recoveryNeedsAttention);
  assert((await f.life.start({manualRecovery:true})).allowed);await f.life.apply(curve(75));assert.deepEqual(f.host.nodes,curve(75));return{noOldContinuation:true,manual:true};
 });
 await check('heartbeat queued behind a denied write never renews after the terminal receipt',async f=>{
  const g=f.holdWrite();f.host.denied.apply=new FanApiError('refused',401,'AUTH_REJECTED');const write=outcome(f.life.apply(curve(70)));await g.ready;const heartbeat=outcome(f.life.heartbeat());g.resolve();await write;const renewal=await heartbeat;
  assert(renewal.error);assert.equal(f.host.calls.filter(x=>x==='heartbeat').length,0,'queued old heartbeat renewed a denied episode');assert(f.life.recoveryNeedsAttention);return{queuedHeartbeatCalls:0};
 });
 await check('late refusal from a retired same-PID process cannot park the replacement',async f=>{
  const g=f.holdWrite();f.host.denied.apply=new FanApiError('old refused',401,'AUTH_REJECTED');const work=outcome(f.life.apply(curve(65)));await g.ready;const owner=f.life as any;owner.process={...owner.process};const epoch=owner.heartbeatEpoch;
  f.host.denied={};g.resolve();await work;assert(!f.life.recoveryNeedsAttention);assert.equal(owner.heartbeatEpoch,epoch);assert(owner.heartbeatTimer!==null);return{identityOnlyCounterexample:true};
 });
 for(const kind of ['cleanup','resuming','unknown'] as const)await check(`${kind}: nonterminal refusal keeps its original recovery semantics`,async f=>{
  f.host.denied.apply=kind==='cleanup'?new FanApiError('pending',409,'HC_CLOSE_PENDING'):kind==='resuming'?new FanApiError('resuming',409,'POWER_RESUMING'):new FanApiError('reply lost',0,'FAN_HTTP_RESULT_UNKNOWN');
  await outcome(f.life.apply(curve(65)));assert(f.life.recoveryActive,'terminal classifier swallowed a permitted recovery');assert(!f.life.recoveryNeedsAttention);f.host.denied={};
  await f.clock.advance(kind==='unknown'?8000:1500);
  if(kind==='unknown'){assert(f.life.recoveryActive,'slow feedback stage ended before its original 30s budget');assert(!f.life.recoveryNeedsAttention);await f.clock.advance(27000);assert(f.life.recoveryNeedsAttention);assert(f.host.calls.includes('heartbeat'),'uncertain write stopped valid prior control maintenance');assert.deepEqual(f.host.writes,[100],'uncertain write was blindly retried');await f.life.apply(curve(75));assert.deepEqual(f.host.nodes,curve(75));}
  else{assert(!f.life.recoveryActive);assert.deepEqual(f.host.nodes,curve(65));}
  return{kind,retained:true,writes:f.host.writes};
 });
 await check('ordinary curve write failure retains its existing hardware-recovery policy',async f=>{
  f.host.denied.apply=new FanApiError('writer failed',409,'FAN_CURVE_WRITE_FAILED');await outcome(f.life.apply(curve(65)));assert(!f.life.recoveryNeedsAttention,'writer failure was reclassified as a permission refusal');
  f.host.denied={};await f.clock.advance(6000);assert(f.host.calls.includes('heartbeat'));assert.deepEqual(f.host.writes,[100]);await f.life.apply(curve(75));assert.deepEqual(f.host.nodes,curve(75));return{permissionPolicyNotAppliedToWriterFailure:true};
 });
 for(const op of ['apply','preset','heartbeat'] as const)await check(`${op}: declared owner refusal takes precedence over absent HTTP status`,async f=>{
  const error=new FanApiError('another controller',0,'EXTERNAL_FAN_OWNER');f.host.denied[op]=error;const reply=await outcome(op==='apply'?f.life.apply(curve(65)):op==='preset'?f.life.applyPreset('soft',curve(65)):f.life.heartbeat());assert.equal(reply.error,error);const index=f.host.calls.lastIndexOf(op+'-denied');assert.deepEqual(f.host.calls.slice(index+1),[]);assert(f.life.recoveryNeedsAttention);return{declaredOwnerNotTransportLoss:true};
 });
 for(const role of ['preflight-apply','preflight-preset','startup','page','resync','self-heal-read','reattach'] as const)await check(`${role}: declared state refusal cannot turn into a control attempt`,async f=>{
  const error=new FanApiError('read session rejected',403,'API_PERMISSION_DENIED');let reply:{value:unknown,error:unknown}|null=null;
  if(role==='self-heal-read'){
   f.host.denied.apply=new FanApiError('write reply lost',0,'FAN_HTTP_RESULT_UNKNOWN');await outcome(f.life.apply(curve(65)));f.host.denied={};assert(f.life.recoveryActive);f.host.readDenial=error;await f.clock.advance(250);
  }else if(role==='resync'){
   f.host.refusals=1;f.host.readDenialOnRefusal=error;reply=await outcome(f.life.apply(curve(65)));
  }else{
   f.host.readDenial=error;reply=await outcome(role==='preflight-apply'?f.life.apply(curve(65)):role==='preflight-preset'?f.life.applyPreset('soft',curve(65)):role==='startup'?f.life.start({manualRecovery:true}):role==='reattach'?f.life.reattachExistingControlSession():f.life.getState({discardSuperseded:true}));
  }
  const index=f.host.calls.lastIndexOf('state-denied'),after=f.host.calls.slice(index+1);f.observations.push({role,replyRejected:reply?.error===error,afterDenied:after,needsAttention:f.life.recoveryNeedsAttention});
  if(reply)assert.equal(reply.error,error,'read refusal was swallowed or replaced by a later operation');assert.deepEqual(after,[],'declared read refusal still submitted more work');assert(f.life.recoveryNeedsAttention);
  const count=f.host.calls.length;await f.clock.advance(6000);assert.equal(f.host.calls.length,count,'read refusal left automatic maintenance/recovery active');
  f.host.readDenial=null;assert((await f.life.start({manualRecovery:true})).allowed);await f.life.apply(curve(75));assert.deepEqual(f.host.nodes,curve(75));return{role,afterDenied:after,manualAfterReadRefusal:true};
 });
 for(const optional of [false,true])await check(`stale ${optional?'optional':'mandatory'} page refusal cannot pause a newer observation`,async f=>{
  const read=f.holdState(),error=new FanApiError('old read refused',401,'AUTH_REJECTED');const old=outcome(optional?f.life.getState({discardSuperseded:true}):f.life.getState());await read.ready;await f.life.getState();const epoch=(f.life as any).heartbeatEpoch;read.reject(error);const reply=await old;
  if(optional){assert.equal(reply.error,null);assert.equal(reply.value,null);}else assert.equal(reply.error,error);assert(!f.life.recoveryNeedsAttention);assert.equal((f.life as any).heartbeatEpoch,epoch);return{staleRefusalIgnored:true};
 });
 for(const role of ['preflight','page','reattach'] as const)await check(`${role}: an ordinary read timeout is still not a permission denial`,async f=>{
  const error=new Error('HTTP request timed out');f.host.readDenial=error;const reply=await outcome(role==='preflight'?f.life.apply(curve(65)):role==='page'?f.life.getState():f.life.reattachExistingControlSession());assert(!f.life.recoveryNeedsAttention);
  if(role==='preflight'){assert.equal(reply.error,null);assert.deepEqual(f.host.nodes,curve(65));}else if(role==='page')assert.equal(reply.error,error);else{assert.equal(reply.error,null);assert.equal(reply.value,false);}f.host.readDenial=null;return{softReadContractPreserved:true};
 });
 await check('self-heal read refusal retired by a same-generation wake does not strand its owner',async f=>{
  await f.heal();const read=f.holdState();await f.clock.advance(250);await read.ready;f.life.observePowerBoundary('resuming',6);read.reject(new FanApiError('old epoch rejected',401,'AUTH_REJECTED'));await f.clock.flush();
  await f.clock.advance(3000);assert(!f.life.recoveryNeedsAttention,'old epoch denied the new wake');assert(!f.life.recoveryActive,'old reply left a recovering owner without future work');assert.deepEqual(f.host.nodes,curve(65));return{newWakeContinued:true};
 });
 await check('reattach acquisition refusal is not an ordinary missing session',async f=>{
  const error=new FanApiError('acquisition refused',403,'API_PERMISSION_DENIED'),snapshot=f.host.snapshot.bind(f.host);f.host.lease=null;f.host.controlled=true;f.host.snapshot=()=>({...snapshot(),hardwareWritesEnabled:true});f.host.acquireDenial=error;
  const reply=await outcome(f.life.reattachExistingControlSession());assert.equal(reply.error,error);assert(f.life.recoveryNeedsAttention);const calls=f.host.calls.length;await f.clock.advance(1000);assert.equal(f.host.calls.length,calls);
  f.host.acquireDenial=null;f.host.snapshot=snapshot;assert((await f.life.start({manualRecovery:true})).allowed);await f.life.apply(curve(75));assert.deepEqual(f.host.nodes,curve(75));return{acquireRefusalPropagated:true,manual:true};
 });
  for (const [kind, make] of denials) await check(`resume replay ${kind}: a declared read refusal cannot become a curve write`, async f => {
    const error = make();
    f.host.readDenial = error;
    const reply = await outcome(f.life.resume());
    const index = f.host.calls.lastIndexOf('state-denied');
    const after = f.host.calls.slice(index + 1);
    f.observations.push({replyRejected: reply.error === error, afterDenied: after, needsAttention: f.life.recoveryNeedsAttention});
    assert.equal(reply.error, error, 'resume swallowed the declared state refusal');
    assert.deepEqual(after, [], 'resume submitted work after a declared state refusal');
    assert(f.life.recoveryNeedsAttention);
    const calls = f.host.calls.length;
    await f.clock.advance(6000);
    assert.equal(f.host.calls.length, calls, 'resume refusal left automatic work running');
    f.host.readDenial = null;
    assert((await f.life.start({manualRecovery:true})).allowed);
    await f.life.apply(curve(75));
    assert.deepEqual(f.host.nodes, curve(75));
    return {afterDenied: after, manualAfterRefusal: true};
  });
  for (const error of [new Error('HTTP request timed out'), new FanApiError('unknown state receipt', 0, 'FAN_HTTP_RESULT_UNKNOWN')]) await check(`resume replay soft read ${error.message}: original write admission remains usable`, async f => {
    const count = f.host.writes.length;
    f.host.readDenial = error;
    const reply = await outcome(f.life.resume());
    assert.equal(reply.error, null);
    assert.equal(f.host.writes.length, count + 1);
    assert(!f.life.recoveryNeedsAttention);
    f.host.readDenial = null;
    return {originalSoftReadContract: true, writes: f.host.writes.slice(count)};
  });
  for (const newer of ['curve', 'off', 'wake'] as const) await check(`resume replay stale refusal after ${newer}: newer intent remains in charge`, async f => {
    const read = f.holdState(), error = new FanApiError('old resume read refused', 403, 'API_PERMISSION_DENIED');
    const old = outcome(f.life.resume());
    await read.ready;
    let next: ReturnType<typeof outcome> | null = null;
    if (newer === 'curve') next = outcome(f.life.apply(curve(75)));
    else if (newer === 'off') next = outcome(f.life.disable());
    else f.life.observePowerBoundary('resuming', 6);
    const epoch = (f.life as any).heartbeatEpoch;
    const index = f.host.calls.length;
    read.reject(error);
    await old;
    if (next) assert.equal((await next).error, null);
    assert(!f.life.recoveryNeedsAttention, 'old read refusal paused the newer owner');
    if (newer === 'curve') assert.deepEqual(f.host.nodes, curve(75));
    else if (newer === 'off') assert(!f.life.hasControlIntent);
    else {
      assert.equal((f.life as any).heartbeatEpoch, epoch, 'old refusal retired the new wake epoch');
      assert.deepEqual(f.host.calls.slice(index), [], 'old read refusal submitted another curve');
      assert((await f.life.start({manualRecovery:true})).allowed);
      await f.life.apply(curve(75));
      assert.deepEqual(f.host.nodes, curve(75));
    }
    return {newer, staleRefusalDidNotParkCurrentOwner: true};
  });
 for (const family of families) for (const action of ['apply','preset'] as const) for (const edge of ['resuming','resuming-ready'] as const) await check(`late successful ${action} after ${edge}: old write receipt cannot finish current recovery`,async f=>{
  const held=gate<FanState>(),enable=f.host.enable.bind(f.host);let oldState:FanState|null=null;
  f.host.enable=async(nodes=curve(65),id?:string)=>{const applied=await enable(nodes,id);if(!oldState){oldState=applied;held.entered();return held.promise;}return applied;};
  const receipt=(f.life as any).activeCurveReceipt;
  const pending=outcome(action==='apply'?f.life.apply(curve(65)):f.life.applyPreset('soft',curve(65)));
  try {
   await held.ready;f.setNative({generation:6,phase:'resuming',hardwareWritesAllowed:false,resumeReady:false,inputReady:false,hibernateAvailable:true});
   f.life.observePowerBoundary('resuming',6);f.host.lease=null;f.host.controlled=false;f.host.stateName='Suspended';f.host.opened=f.host.events=false;
   if(edge==='resuming-ready'){
    f.setNative({generation:6,phase:'ready',hardwareWritesAllowed:true,resumeReady:true,inputReady:true,hibernateAvailable:true});f.host.completeResume=true;await f.host.resume({generation:6});f.life.observePowerBoundary('resume-ready',6);
   }
   const count=f.host.writes.length;held.resolve(oldState!);const reply=await pending;
   f.observations.push({point:'old-write-returned',receiptReplaced:(f.life as any).activeCurveReceipt!==receipt,recoveryActive:f.life.recoveryActive,replyCode:reply.error instanceof FanApiError?reply.error.errorCode:null,writes:f.host.writes.slice()});
   assert(reply.error instanceof FanApiError && reply.error.errorCode==='FAN_HTTP_RESULT_UNKNOWN','old successful receipt was reported as current control success');
   assert.equal((f.life as any).activeCurveReceipt,receipt,'old write reply replaced the current receipt');
   assert(f.life.recoveryActive,'old acknowledgement ended the retained recovery intent');assert.equal(f.host.writes.length,count,'unknown current result was immediately replayed');
   if(edge==='resuming'){
    await f.clock.advance(500);assert.equal(f.host.writes.length,count,'curve replayed while native still resuming');
    f.setNative({generation:6,phase:'ready',hardwareWritesAllowed:true,resumeReady:true,inputReady:true,hibernateAvailable:true});f.host.completeResume=true;await f.host.resume({generation:6});f.life.observePowerBoundary('resume-ready',6);
   }
   await f.clock.advance(3000);assert(!f.life.recoveryActive,'current wake did not continue the same intent');assert.deepEqual(f.host.nodes,curve(65));assert.equal(f.host.writes.length,count+1,'current wake must restore its curve once');
   await f.life.apply(curve(75));assert.deepEqual(f.host.nodes,curve(75));return{edge,oldReceiptIgnored:true,currentWakeRecovered:true,manualAfterRecovery:true};
  } finally {held.resolve(oldState??f.host.snapshot());await pending;f.host.enable=enable;}
 },family);
 for(const action of ['apply','preset'] as const)for(const replacement of ['process','session'] as const)await check(`late successful ${action} after ${replacement} replacement cannot stamp a new owner`,async f=>{
  const held=gate<FanState>(),enable=f.host.enable.bind(f.host);let oldState:FanState|null=null;
  f.host.enable=async(nodes=curve(65),id?:string)=>{const applied=await enable(nodes,id);oldState=applied;held.entered();return held.promise;};
  const receipt=(f.life as any).activeCurveReceipt,pending=outcome(action==='apply'?f.life.apply(curve(65)):f.life.applyPreset('soft',curve(65)));
  try{await held.ready;if(replacement==='process')(f.life as any).process={...(f.life as any).process};else(f.life as any).sessionGeneration++;
   held.resolve(oldState!);const reply=await pending;assert(reply.error instanceof Error&&reply.error.message.startsWith('FAN_CONTROL_INTENT_SUPERSEDED:'));
   assert.equal((f.life as any).activeCurveReceipt,receipt);return{replacement,retiredReceiptIgnored:true};
  }finally{held.resolve(oldState??f.host.snapshot());await pending;f.host.enable=enable;}
 });
 for(const action of ['apply','preset'] as const)for(const end of ['off','close','new-curve'] as const)await check(`late successful ${action} preserves existing ${end} priority`,async f=>{
  const held=gate<FanState>(),enable=f.host.enable.bind(f.host);let oldState:FanState|null=null;
  f.host.enable=async(nodes=curve(65),id?:string)=>{const applied=await enable(nodes,id);if(!oldState){oldState=applied;held.entered();return held.promise;}return applied;};
  const pending=outcome(action==='apply'?f.life.apply(curve(65)):f.life.applyPreset('soft',curve(65)));let next:ReturnType<typeof outcome>|null=null;
  try{await held.ready;next=outcome(end==='off'?f.life.disable():end==='close'?f.life.close():f.life.apply(curve(75)));
   held.resolve(oldState!);assert((await pending).error);assert.equal((await next).error,null);
   if(end==='new-curve')assert.deepEqual(f.host.nodes,curve(75));else assert(!f.life.hasControlIntent);return{end,existingPriorityPreserved:true};
  }finally{held.resolve(oldState??f.host.snapshot());await pending;if(next)await next;f.host.enable=enable;}
 });
 for(const action of ['apply','preset'] as const)await check(`current successful ${action} receipt still completes immediately`,async f=>{
  const reply=await outcome(action==='apply'?f.life.apply(curve(65)):f.life.applyPreset('soft',curve(65)));assert.equal(reply.error,null);assert(!f.life.recoveryActive);assert.deepEqual(f.host.nodes,curve(65));return{currentSuccessUnchanged:true};
 });
 for(const ownerKind of ['self-heal','lifecycle'] as const)for(const edge of ['resuming','resuming-ready'] as const)await check(`late automatic ${ownerKind} success after ${edge} cannot settle the current wake`,async f=>{
  const held=gate<FanState>(),enable=f.host.enable.bind(f.host);let oldState:FanState|null=null;
  f.host.enable=async(nodes=curve(65),id?:string)=>{const applied=await enable(nodes,id);if(!oldState){oldState=applied;held.entered();return held.promise;}return applied;};
  const receipt=(f.life as any).activeCurveReceipt,completed=(f.life as any).completedRecoveryWrite;
  try{
   if(ownerKind==='self-heal')await f.heal();else{f.life.rememberDesiredCurve(curve(65));assert(await f.life.requestRecoveryAfterObservedHostState('automatic-late-write'));}
   await f.clock.advance(250);await held.ready;
   f.setNative({generation:6,phase:'resuming',hardwareWritesAllowed:false,resumeReady:false,inputReady:false,hibernateAvailable:true});f.life.observePowerBoundary('resuming',6);f.host.lease=null;f.host.controlled=false;f.host.stateName='Suspended';f.host.opened=f.host.events=false;
   if(edge==='resuming-ready'){f.setNative({generation:6,phase:'ready',hardwareWritesAllowed:true,resumeReady:true,inputReady:true,hibernateAvailable:true});f.host.completeResume=true;await f.host.resume({generation:6});f.life.observePowerBoundary('resume-ready',6);}
   const count=f.host.writes.length;held.resolve(oldState!);await f.clock.flush();
   f.observations.push({point:'old-auto-write-returned',ownerKind,edge,recoveryActive:f.life.recoveryActive,receiptReplaced:(f.life as any).activeCurveReceipt!==receipt,completedReplaced:(f.life as any).completedRecoveryWrite!==completed,writes:f.host.writes.slice()});
   assert(f.life.recoveryActive,'old automatic success settled the new wake');assert.equal((f.life as any).activeCurveReceipt,receipt);assert.equal((f.life as any).completedRecoveryWrite,completed);assert.equal(f.host.writes.length,count,'old automatic result immediately triggered another write');
   if(edge==='resuming'){await f.clock.advance(500);assert.equal(f.host.writes.length,count);f.setNative({generation:6,phase:'ready',hardwareWritesAllowed:true,resumeReady:true,inputReady:true,hibernateAvailable:true});f.host.completeResume=true;await f.host.resume({generation:6});f.life.observePowerBoundary('resume-ready',6);}
   await f.clock.advance(3000);assert(!f.life.recoveryActive);assert.deepEqual(f.host.nodes,curve(65));assert.equal(f.host.writes.length,count+1);
   await f.life.apply(curve(75));assert.deepEqual(f.host.nodes,curve(75));return{ownerKind,edge,retainedUntilCurrentWake:true,manualAfterRecovery:true};
  }finally{held.resolve(oldState??f.host.snapshot());await f.clock.flush();f.host.enable=enable;}
 });
 for(const action of ['apply','preset'] as const)await check(`held current ${action} success survives the committed resumed notice`,async f=>{
  const held=gate<FanState>(),enable=f.host.enable.bind(f.host);let state:FanState|null=null;f.host.enable=async(nodes=curve(65),id?:string)=>{state=await enable(nodes,id);held.entered();return held.promise;};
  const pending=outcome(action==='apply'?f.life.apply(curve(65)):f.life.applyPreset('soft',curve(65)));
  try{await held.ready;f.life.observePowerBoundary('resumed',6);held.resolve(state!);assert.equal((await pending).error,null);assert(!f.life.recoveryActive);assert.deepEqual(f.host.nodes,curve(65));return{committedResumedNotRetiringCurrentWrite:true};}
  finally{held.resolve(state??f.host.snapshot());await pending;f.host.enable=enable;}
 });
 console.log(JSON.stringify({ok:result.every(x=>x.ok),checks:result.length,results:result,scope:'real public bridge and recovery owner; virtual clock, lease-enforcing inert Host/native/launcher; no hardware/CPU benchmark'}));if(result.some(x=>!x.ok))process.exitCode=1;
}
void main().catch(e=>{console.error(e);process.exitCode=1;});
