// YMCC owns discovery and observation. Decky and Y consume this singleton.
import {losslessRuntime, type LosslessRuntimeReceipt} from './api';
import {on} from './ipc';
import {getSharedPowerLifecycle,isSharedPowerLifecycleAdmitted,onSharedPowerLifecycle,requestSharedPowerLifecycleReconcile} from './powerLifecycleState';
import {subscribePolicyGameStatus} from './gamePolicyTarget';
import type {LosslessProgramState} from './losslessProgramState';
type PowerScope = {powerGeneration:number;powerNotificationSequence:number;powerSuspendSerial:number};
type RuntimeEvent = LosslessRuntimeReceipt & PowerScope;
const scopeOf = (power:NonNullable<ReturnType<typeof getSharedPowerLifecycle>>):PowerScope => ({
  powerGeneration:power.generation,powerNotificationSequence:power.observedNotificationSequence??0,powerSuspendSerial:power.suspendSerial??0,
});
const invalidatedObservation:LosslessRuntimeReceipt=Object.freeze({known:false,running:false,readable:false,pid:0,processCreated:'0',path:'',button:'',status:'',reason:'observation-invalidated',configRevision:0});
function hasPowerScope(value:LosslessRuntimeReceipt|null|undefined):value is RuntimeEvent {
  return !!value&&typeof value.powerGeneration==='number'&&Number.isSafeInteger(value.powerGeneration)&&value.powerGeneration>0&&
    typeof value.powerNotificationSequence==='number'&&Number.isSafeInteger(value.powerNotificationSequence)&&value.powerNotificationSequence>=0&&
    typeof value.powerSuspendSerial==='number'&&Number.isSafeInteger(value.powerSuspendSerial)&&value.powerSuspendSerial>=0;
}
function compareScope(event:PowerScope,current:PowerScope){
  if(event.powerGeneration<current.powerGeneration||event.powerNotificationSequence<current.powerNotificationSequence||
    event.powerSuspendSerial<current.powerSuspendSerial)return -1;
  return event.powerGeneration===current.powerGeneration&&event.powerNotificationSequence===current.powerNotificationSequence&&
    event.powerSuspendSerial===current.powerSuspendSerial?0:1;
}
export class LosslessStateOwner {
  private document:LosslessRuntimeReceipt|null=null;
  private flight:{work:Promise<LosslessRuntimeReceipt>;retire:()=>void;retired:()=>boolean}|null=null;
  private epoch=0;private lastObservedAt=0;
  private listeners=new Set<()=>void>();
  constructor(private detect:()=>Promise<LosslessRuntimeReceipt>,private canRead=()=>true){}
  get revision(){return this.epoch;}
  subscribe(cb:()=>void){this.listeners.add(cb);return ()=>{this.listeners.delete(cb);};}
  private changed(){for(const cb of [...this.listeners]){try{cb();}catch{}}}
  accept(value:LosslessRuntimeReceipt){if(value.observedAt&&value.observedAt<this.lastObservedAt)return;this.lastObservedAt=value.observedAt??this.lastObservedAt;this.epoch++;this.document=value;this.changed();}
  invalidate(retire=false){
    this.epoch++;this.document=null;
    if(retire&&this.flight){const old=this.flight;this.flight=null;old.retire();}
  }
  read():Promise<LosslessRuntimeReceipt>{
    if(!this.canRead())return Promise.resolve(invalidatedObservation);
    if(this.document)return Promise.resolve(this.document);
    const prior=this.flight;
    if(prior)return prior.work.then(value=>!this.canRead()?invalidatedObservation:prior.retired()?value:this.document??this.read());
    const epoch=this.epoch;
    let retire!:()=>void,retiredLocally=false;
    const retired=new Promise<LosslessRuntimeReceipt>(resolve=>{retire=()=>{retiredLocally=true;resolve(invalidatedObservation);};});
    const work=Promise.race([retired,Promise.resolve().then(async()=>{
      if(retiredLocally||!this.canRead())return invalidatedObservation;
      const value=await this.detect();
      if(retiredLocally||!this.canRead())return invalidatedObservation;
      if(epoch===this.epoch&&value!==invalidatedObservation)this.document=value;
      return this.document??invalidatedObservation;
    })]);
    const flight={work,retire,retired:()=>retiredLocally};
    this.flight=flight;void work.finally(()=>{if(this.flight===flight)this.flight=null;}).catch(()=>{});return work;
  }
}
let consumers=0,gamePresent=false,suspended=true,resident=0;
let observedScope:PowerScope|null=null;
const owner=new LosslessStateOwner(async()=>{
  const value=await losslessRuntime.detect();
  if(!resident)return value.reason==='power-not-ready'?invalidatedObservation:value;
  const power=getSharedPowerLifecycle();
  if(!isSharedPowerLifecycleAdmitted(power)||!power)return invalidatedObservation;
  if(!hasPowerScope(value)){
    if(value.reason==='power-not-ready')requestSharedPowerLifecycleReconcile('lossless-detect-denied');
    return invalidatedObservation;
  }
  const order=compareScope(value,scopeOf(power));
  if(order>0)requestSharedPowerLifecycleReconcile('lossless-detect-new-scope');
  return order===0?value:invalidatedObservation;
},()=>{
  // Before the resident owner mounts, direct discovery is guarded by Native.
  if(!resident)return true;
  const power=getSharedPowerLifecycle();
  return !suspended&&!!observedScope&&!!power&&isSharedPowerLifecycleAdmitted(power)&&compareScope(scopeOf(power),observedScope)===0;
});
let configured='',configurationRevision=0,watchPowerRevision=0,configuring=Promise.resolve();
let watchRetry:ReturnType<typeof setTimeout>|null=null;
let residentStops:Array<()=>void>=[];
function cancelWatchRetry(){
  if(watchRetry!==null)clearTimeout(watchRetry);
  watchRetry=null;
}
function configure(retrying=false){
  const active=!suspended&&(gamePresent||consumers>0),detailed=active&&consumers>0;
  const key=String(active)+String(detailed);if(key===configured)return;
  cancelWatchRetry();configured=key;
  // Serialize interest changes; a late older request cannot re-enable a watch.
  const ticket=++configurationRevision;
  configuring=configuring.catch(()=>{}).then(async()=>{
    if(ticket!==configurationRevision)return;
    const powerAtSend=watchPowerRevision;
    try { if(await losslessRuntime.watch(active,detailed)===true)return; } catch { /* Reconcile below. */ }
    if(ticket!==configurationRevision)return;
    // A merged Ready may have kept the same interest while this request was pending.
    if(watchPowerRevision!==powerAtSend){configured='';configure();return;}
    if(retrying){configured='';return;}
    // Native applies desired observation interest idempotently. Keep the request
    // in the same serial queue if it or its reply did not arrive.
    const timer=setTimeout(()=>{
      if(watchRetry!==timer||ticket!==configurationRevision||powerAtSend!==watchPowerRevision)return;
      watchRetry=null;configured='';configure(true);
    },500);
    watchRetry=timer;
  });
}
export function startLosslessRuntimeOwner():()=>void {
  resident++;
  if(resident===1){const power=getSharedPowerLifecycle();suspended=!isSharedPowerLifecycleAdmitted(power);
  observedScope=power?scopeOf(power):null;owner.invalidate(true);
  let pending:RuntimeEvent|null=null;
  residentStops=[on<RuntimeEvent>('lossless.runtime',value=>{
      if(!hasPowerScope(value))return;
      const current=getSharedPowerLifecycle(),order=current?compareScope(value,scopeOf(current)):1;
      if(order===0&&isSharedPowerLifecycleAdmitted(current)&&!suspended&&observedScope&&compareScope(value,observedScope)===0)owner.accept(value);
      else if(order>=0&&(!pending||compareScope(value,pending)>0||
        (compareScope(value,pending)===0&&(value.observedAt??0)>=(pending.observedAt??0)))){
        // Native can emit before this owner sees Ready, even within the same scope.
        pending=value;
        if(order>0)requestSharedPowerLifecycleReconcile('lossless-runtime-observation');
      }
    }),
    subscribePolicyGameStatus(game=>{gamePresent=!!game;owner.invalidate();configure();}),
    onSharedPowerLifecycle(snapshot=>{
      const held=!isSharedPowerLifecycleAdmitted(snapshot),next=scopeOf(snapshot);
      if(held!==suspended||!observedScope||compareScope(next,observedScope)!==0){
        ++watchPowerRevision;suspended=held;observedScope=next;
        if(watchRetry!==null){cancelWatchRetry();configured='';}
        owner.invalidate(true);configure();
      }
      if(pending){
        const order=compareScope(pending,next);
        if(order<0)pending=null;
        else if(order===0&&!held){const value=pending;pending=null;owner.accept(value);}
      }
    },{observeSuspendSerial:true,observeNotificationSequence:true})];
  requestSharedPowerLifecycleReconcile('lossless-runtime-owner');
  configure();}
  let stopped=false;
  return ()=>{if(stopped)return;stopped=true;if(--resident)return;for(const stop of residentStops)stop();residentStops=[];gamePresent=false;suspended=true;observedScope=null;owner.invalidate(true);configure();};
}
export function retainLosslessObservation():()=>void {
  consumers++;owner.invalidate();configure();let ended=false;
  return ()=>{if(ended)return;ended=true;consumers=Math.max(0,consumers-1);configure();};
}
export const readLosslessRuntime=()=>owner.read();
// All consumers reuse the same YMCC observation; never interpret UIA/config as liveness.
export async function readLosslessProgram():Promise<LosslessProgramState> {
  const value=await readLosslessRuntime();
  return {known:value.known,running:value.known&&value.running};
}
export const losslessRuntimeRevision=()=>owner.revision;
export const invalidateLosslessRuntime=()=>owner.invalidate();
export const subscribeLosslessRuntime=(listener:()=>void)=>owner.subscribe(listener);
