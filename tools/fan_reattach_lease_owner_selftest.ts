/** Real FanHostLifecycle ownership queue; Host/native/launcher are inert.
 * The adapter grants unique leases and rejects LEASE_HELD/LEASE_INVALID as the
 * production Host does. An acquire can commit at the server before its reply.
 */
import assert from 'node:assert/strict';
import {FanHostLifecycle} from '../src/bridge/fanHost';
import {FanApiError,type FanState,type FanNode} from '../src/bridge/fanApi';
import {NoIoHost} from './fixtures/fan_no_io_host';
const curve:FanNode[]=[{tempC:0,dutyPercent:20},{tempC:50,dutyPercent:50},{tempC:100,dutyPercent:100}];
const newer=curve.map(x=>({...x,dutyPercent:80}));
const delay=(ms:number)=>new Promise<void>(r=>setTimeout(r,ms));
async function bounded<T>(p:Promise<T>):Promise<T>{let t:ReturnType<typeof setTimeout>|undefined;try{return await Promise.race([p,new Promise<never>((_,r)=>{t=setTimeout(()=>r(new Error('inert chain did not settle')),2000);})]);}finally{if(t)clearTimeout(t);}}
class StrictLeaseHost extends NoIoHost {
 writes=true;restored=false;holdAcquire=false;acquireEntered=false;releaseAcquire:(()=>void)|null=null;releaseFailure=false;deny=false;written:FanNode[][]=[];
 holdState=false;stateEntered=false;releaseState:(()=>void)|null=null;
 override async getState(){this.calls.push('state');const remote=this.snapshot();if(this.holdState){this.holdState=false;this.stateEntered=true;await new Promise<void>(r=>this.releaseState=r);}return remote;}
 constructor(family:string){super();this.factoryType='HandheldCompanion.Devices.'+family;this.generation=3;this.attempt=1;this.stateName='Ready';this.powerState='On';this.opened=this.events=true;}
 override snapshot():FanState{return{...super.snapshot(),hardwareWritesEnabled:this.writes,hardwareWrites:this.writes,hardwareWritesObserved:true,oemRestoreConfirmed:this.restored,oemPhysicalOwnershipConfirmed:false,controlAccepting:!this.deny&&['Ready','AwaitingControl'].includes(this.stateName)};}
 override async acquireControl(){this.calls.push('acquire');if(this.lease)throw new FanApiError('LEASE_HELD',409,'LEASE_HELD');const lease={leaseId:'strict-'+ ++this.sequence,generation:this.sequence};this.lease=lease;if(this.holdAcquire){this.holdAcquire=false;this.acquireEntered=true;await new Promise<void>(r=>this.releaseAcquire=r);}return {...lease};}
 override async enable(nodes?:readonly FanNode[],id?:string){this.calls.push('enable');if(this.lease?.leaseId!==id)throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');this.writes=true;this.restored=false;this.stateName='Ready';this.written.push(nodes?.map(n=>({...n}))??[]);return this.snapshot();}
 override async applyPreset(_name?:string,id?:string,nodes?:readonly FanNode[]){return this.enable(nodes,id);}
 override async releaseControl(id?:string){this.calls.push('release');if(this.releaseFailure)throw new Error('fixture release result unavailable');if(this.lease?.leaseId!==id)throw new FanApiError('LEASE_INVALID',409,'LEASE_INVALID');this.lease=null;this.writes=false;this.restored=true;this.stateName='AwaitingControl';return this.snapshot();}
 override async restoreOem(){this.calls.push('restore');this.writes=false;this.restored=true;return this.snapshot();}
 override async close(){this.writes=false;this.restored=true;return super.close();}
}
async function setup(family='ClawCG3EM',heartbeatIntervalMs=0){
 const h=new StrictLeaseHost(family);let starts=0,stops=0;const displays:boolean[]=[];let displayHook:((active:boolean)=>void)|null=null;
 const life=new FanHostLifecycle({enabled:true,adapter:h,heartbeatIntervalMs,onControlDisplay:a=>{displays.push(a);displayHook?.(a);},launcher:{start:async()=>{starts++;return{pid:1062,executable:'inert.exe',adopted:true};},stop:async()=>{stops++;}},readNativePowerState:async()=>({generation:3,phase:'ready',hardwareWritesAllowed:true,inputReady:true,resumeReady:true,hibernateAvailable:true})});
 life.setPowerGeneration(3);life.observePowerBoundary('resume-ready',3);life.rememberDesiredCurve(curve);await life.start();return{h,life,displays,counts:()=>({starts,stops}),setDisplayHook:(hook:((active:boolean)=>void)|null)=>{displayHook=hook;}};
}
const cases:{name:string,ok:boolean,error?:string,evidence?:unknown}[]=[];
async function check(name:string,run:()=>Promise<unknown>){try{const evidence=await run();cases.push({name,ok:true,evidence});}catch(e){cases.push({name,ok:false,error:String(e)});}}
async function main(){
 for(const family of ['ROGAlly','ClawCG3EM','GPDWin5'])for(const action of ['apply','preset','off','close'])await check(family+': held reattach acquire -> newer '+action,async()=>{
  const {h,life,displays,counts}=await setup(family);h.holdAcquire=true;const old=life.reattachExistingControlSession();
  for(let i=0;i<80&&!h.acquireEntered;i++)await Promise.resolve();assert(h.acquireEntered,'must hold an actually committed acquisition');
  let next:Promise<unknown>;if(action==='apply')next=life.apply(newer);else if(action==='preset')next=life.applyPreset('aggressive',newer);else if(action==='off')next=life.disable();else next=life.close();
  const owned=next.then(x=>({ok:true,value:x}),e=>({ok:false,error:String(e)}));await Promise.race([owned,delay(40)]);
  const before={writes:h.writes,calls:[...h.calls]};h.releaseAcquire!();const [oldResult,result]=await bounded(Promise.all([old,owned]));
  try{
   assert.equal(oldResult,false,'new request retires reattach');assert.equal(result.ok,true,'one newer request must finish without another user click');
   if(action==='apply'||action==='preset'){assert.equal(h.writes,true,'stale reattach released current manual control');assert.deepEqual(h.written.at(-1),newer);assert.equal(h.calls.filter(c=>c==='enable').length,1);assert.equal(h.lease?.leaseId,life.currentLease?.leaseId);assert.equal(displays.at(-1),true);const released=h.calls.indexOf('release'),enabled=h.calls.indexOf('enable');assert(released>=0&&released<enabled,'retire old lease before newer hardware write');}
   else{assert.equal(h.writes,false);assert.equal(life.hasControlIntent,false);assert.equal(h.calls.filter(c=>c==='enable').length,0);assert.equal(displays.at(-1)??false,false);}
   assert.equal(counts().starts,1,'reattach/manual control may not launch a second process');
   return{before,after:{writes:h.writes,calls:[...h.calls],counts:counts()}};
  }finally{await life.close().catch(()=>{});}
 });
 await check('current reattach acquires once without any curve rewrite',async()=>{const {h,life}=await setup();try{assert.equal(await life.reattachExistingControlSession(),true);assert.equal(h.calls.filter(c=>c==='acquire').length,1);assert.equal(h.written.length,0);assert.equal(h.writes,true);assert.equal(life.currentLease?.leaseId,h.lease?.leaseId);}finally{await life.close();}});
 await check('two current reattach calls share live token without another acquisition',async()=>{const {h,life}=await setup();try{assert.deepEqual(await Promise.all([life.reattachExistingControlSession(),life.reattachExistingControlSession()]),[true,true]);assert.equal(h.calls.filter(c=>c==='acquire').length,1);assert.equal(h.written.length,0);}finally{await life.close();}});
 await check('explicit Host admission denial remains read-only and does not acquire',async()=>{const {h,life}=await setup();h.deny=true;try{assert.equal(await life.reattachExistingControlSession(),false);assert.equal(h.calls.filter(c=>c==='acquire').length,0);assert.equal(h.written.length,0);}finally{await life.close();}});
 await check('failed stale release does not repeat or cancel the later manual success',async()=>{const {h,life}=await setup();h.holdAcquire=true;h.releaseFailure=true;const old=life.reattachExistingControlSession();for(let i=0;i<80&&!h.acquireEntered;i++)await Promise.resolve();assert(h.acquireEntered);const next=life.apply(newer);await delay(40);h.releaseAcquire!();try{assert.equal(await bounded(old),false);await bounded(next);assert.equal(h.writes,true);assert.deepEqual(h.written.at(-1),newer);assert.equal(h.calls.filter(c=>c==='release').length,1);}finally{h.releaseFailure=false;await life.close();}});

 // A new resuming edge may keep the same generation. The old packet then
 // loses authority even though PID/session/intent revision are unchanged.
 const families=['ROGAlly','ClawCG3EM','GPDWin5','OneXPlayerX1Mini','LenovoLegionGo'];
 const edges=['resuming','resuming-then-ready','resume-ready','resumed','none'] as const;
 const changePower=(life:FanHostLifecycle,h:StrictLeaseHost,edge:typeof edges[number])=>{
  if(edge==='none')return;
  if(edge==='resuming'||edge==='resuming-then-ready'){
   life.observePowerBoundary('resuming',3);h.lease=null;h.stateName='AwaitingControl';
   if(edge==='resuming-then-ready')life.observePowerBoundary('resume-ready',3);
  }else life.observePowerBoundary(edge,3);
 };
 const manualAfterBoundary=async(life:FanHostLifecycle,h:StrictLeaseHost)=>{
  life.observePowerBoundary('resume-ready',3);h.completeResume=true;
  await bounded(life.apply(newer));
  assert.deepEqual(h.written.at(-1),newer,'one new manual action must apply the latest curve');
  assert.equal(h.calls.filter(x=>x==='enable').length,1,'manual recovery required an extra click or duplicate write');
  assert.equal(life.currentLease?.leaseId,h.lease?.leaseId);
 };
 for(const family of families)for(const stage of ['state','acquire'] as const)for(const edge of edges)await check(`${family}: held reattach ${stage} crosses ${edge}`,async()=>{
  const {h,life,counts}=await setup(family,60_000);const owner=life as any;
  if(stage==='state')h.holdState=true;else h.holdAcquire=true;
  const old=life.reattachExistingControlSession();
  for(let i=0;i<100&&!(stage==='state'?h.stateEntered:h.acquireEntered);i++)await Promise.resolve();
  assert(stage==='state'?h.stateEntered:h.acquireEntered,'the actual reattach request must be in flight');
  const retired=edge==='resuming'||edge==='resuming-then-ready';changePower(life,h,edge);
  (stage==='state'?h.releaseState:h.releaseAcquire)!();
  try{
   const result=await bounded(old);
   assert.equal(result,!retired,'old same-generation wake packet was reported as a current reattach');
   assert.equal(h.calls.filter(x=>x==='release').length,0,'retired packet released into another power boundary');
   assert.equal(h.written.length,0,'reattach must not replay a hardware curve');
   if(retired){
    assert.equal(life.currentLease,null,'retired packet adopted its stale lease');
    assert.equal(owner.heartbeatTimer,null,'retired packet restarted lease renewal');
    assert.equal(h.calls.filter(x=>x==='acquire').length,stage==='state'?0:1,'retired state packet initiated a new acquisition');
    await manualAfterBoundary(life,h);
   }else{
    assert.equal(h.calls.filter(x=>x==='acquire').length,1);
    assert.equal(life.currentLease?.leaseId,h.lease?.leaseId);assert.notEqual(owner.heartbeatTimer,null);
   }
   assert.equal(counts().starts,1);return{stage,edge,current:result,calls:[...h.calls],counts:counts()};
  }finally{h.releaseState?.();h.releaseAcquire?.();await life.close().catch(()=>{});}
 });
 for(const family of families)for(const publication of ['read','acquire'] as const)for(const edge of edges.filter(x=>x!=='none'))await check(`${family}: reattach ${publication} display callback crosses ${edge}`,async()=>{
  const {h,life,counts,setDisplayHook}=await setup(family,60_000);const owner=life as any;let fired=0;
  // Establish real bridge Ready via a public control, then model native-owned
  // session continuation. Clear only fixture accounting, not lifecycle state.
  await life.apply(curve);life.observePowerBoundary('resuming',3);life.observePowerBoundary('resume-ready',3);
  h.calls.length=0;h.written.length=0;h.lease=null;
  if(publication==='read')h.lease={leaseId:'read-existing',generation:41};
  const retired=edge==='resuming'||edge==='resuming-then-ready';
  setDisplayHook(active=>{if(!active||fired)return;fired++;setDisplayHook(null);changePower(life,h,edge);});
  try{
   const result=await bounded(life.reattachExistingControlSession());assert.equal(fired,1,'callback boundary was not exercised');
   assert.equal(result,!retired,'display callback retired the operation before its success publication');
   assert.equal(h.calls.filter(x=>x==='release').length,0);assert.equal(h.written.length,0);
   if(retired){
    assert.equal(life.currentLease,null);assert.equal(owner.heartbeatTimer,null,'display callback was followed by a stale renewal timer');
    assert.equal(h.calls.filter(x=>x==='acquire').length,publication==='read'?0:1);
    await manualAfterBoundary(life,h);
   }else{assert.equal(life.currentLease?.leaseId,h.lease?.leaseId);assert.notEqual(owner.heartbeatTimer,null);}
   assert.equal(counts().starts,1);return{publication,edge,current:result,calls:[...h.calls],counts:counts()};
  }finally{setDisplayHook(null);await life.close().catch(()=>{});}
 });

 const result={ok:cases.every(c=>c.ok),checks:cases.length,cases,hardwareCalls:0,scope:'actual bridge; server-committed delayed unique lease, LEASE_HELD/INVALID semantics; inert Host/native/launcher; no device or CPU benchmark'};console.log(JSON.stringify(result));if(!result.ok)process.exitCode=1;
}
main().catch(e=>{console.error(e);process.exitCode=1;});
