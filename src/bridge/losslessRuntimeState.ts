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
function compareScope(event:PowerScope,current:PowerScope){
  if(event.powerGeneration<current.powerGeneration||event.powerNotificationSequence<current.powerNotificationSequence||
    event.powerSuspendSerial<current.powerSuspendSerial)return -1;
  return event.powerGeneration===current.powerGeneration&&event.powerNotificationSequence===current.powerNotificationSequence&&
    event.powerSuspendSerial===current.powerSuspendSerial?0:1;
}
export class LosslessStateOwner {
  private document:LosslessRuntimeReceipt|null=null;
  private flight:{work:Promise<LosslessRuntimeReceipt>;retire:()=>void;retired:boolean}|null=null;
  private epoch=0;private lastObservedAt=0;
  private listeners=new Set<()=>void>();
  constructor(private detect:()=>Promise<LosslessRuntimeReceipt>){}
  get revision(){return this.epoch;}
  subscribe(cb:()=>void){this.listeners.add(cb);return ()=>{this.listeners.delete(cb);};}
  private changed(){for(const cb of [...this.listeners]){try{cb();}catch{}}}
  accept(value:LosslessRuntimeReceipt){if(value.observedAt&&value.observedAt<this.lastObservedAt)return;this.lastObservedAt=value.observedAt??this.lastObservedAt;this.epoch++;this.document=value;this.changed();}
  invalidate(retire=false){
    this.epoch++;this.document=null;
    if(retire&&this.flight){const old=this.flight;this.flight=null;old.retired=true;old.retire();}
  }
  read():Promise<LosslessRuntimeReceipt>{
    if(this.document)return Promise.resolve(this.document);
    const prior=this.flight;
    if(prior)return prior.work.then(value=>prior.retired?value:this.document??this.read());
    const epoch=this.epoch;
    const invalidated:LosslessRuntimeReceipt={known:false,running:false,readable:false,pid:0,processCreated:'0',path:'',button:'',status:'',reason:'observation-invalidated',configRevision:0};
    let retire!:()=>void;
    const retired=new Promise<LosslessRuntimeReceipt>(resolve=>{retire=()=>resolve(invalidated);});
    const work=Promise.race([retired,Promise.resolve().then(()=>this.detect()).then(value=>{
      if(epoch===this.epoch)this.document=value;
      return this.document??invalidated;
    })]);
    const flight={work,retire,retired:false};
    this.flight=flight;void work.finally(()=>{if(this.flight===flight)this.flight=null;}).catch(()=>{});return work;
  }
}
const owner=new LosslessStateOwner(()=>losslessRuntime.detect());
let consumers=0,gamePresent=false,suspended=true,resident=0;
let configured='',configurationRevision=0,configuring=Promise.resolve();
let residentStops:Array<()=>void>=[];
function configure(){
  const active=!suspended&&(gamePresent||consumers>0),detailed=active&&consumers>0;
  const key=String(active)+String(detailed);if(key===configured)return;configured=key;
  // Serialize interest changes; a late older request cannot re-enable a watch.
  const ticket=++configurationRevision;
  configuring=configuring.catch(()=>{}).then(()=>{if(ticket!==configurationRevision)return;return losslessRuntime.watch(active,detailed);}).then(()=>{},()=>{if(ticket===configurationRevision)configured='';});
}
export function startLosslessRuntimeOwner():()=>void {
  resident++;
  if(resident===1){const power=getSharedPowerLifecycle();suspended=!isSharedPowerLifecycleAdmitted(power);
  let scope=power?scopeOf(power):null,pending:RuntimeEvent|null=null;
  residentStops=[on<RuntimeEvent>('lossless.runtime',value=>{
      if(!value||!Number.isSafeInteger(value.powerGeneration)||value.powerGeneration<=0||
        !Number.isSafeInteger(value.powerNotificationSequence)||value.powerNotificationSequence<0||
        !Number.isSafeInteger(value.powerSuspendSerial)||value.powerSuspendSerial<0)return;
      const current=getSharedPowerLifecycle(),order=current?compareScope(value,scopeOf(current)):1;
      if(order===0&&isSharedPowerLifecycleAdmitted(current))owner.accept(value);
      else if(order>0&&(!pending||compareScope(value,pending)>0||
        (compareScope(value,pending)===0&&(value.observedAt??0)>=(pending.observedAt??0)))){
        // The observation can arrive ahead of its public fact. Keep only the newest one.
        pending=value;requestSharedPowerLifecycleReconcile('lossless-runtime-observation');
      }
    }),
    subscribePolicyGameStatus(game=>{gamePresent=!!game;owner.invalidate();configure();}),
    onSharedPowerLifecycle(snapshot=>{
      const held=!isSharedPowerLifecycleAdmitted(snapshot),next=scopeOf(snapshot);
      if(held!==suspended||!scope||compareScope(next,scope)!==0){
        suspended=held;scope=next;owner.invalidate(true);configure();
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
  return ()=>{if(stopped)return;stopped=true;if(--resident)return;for(const stop of residentStops)stop();residentStops=[];gamePresent=false;suspended=true;owner.invalidate(true);configure();};
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
