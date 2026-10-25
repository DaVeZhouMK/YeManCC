// YMCC owns discovery and observation. Decky and Y consume this singleton.
import {losslessRuntime, type LosslessRuntimeReceipt} from './api';
import {on} from './ipc';
import {subscribePolicyGameStatus} from './gamePolicyTarget';
import type {LosslessProgramState} from './losslessProgramState';
export class LosslessStateOwner {
  private document:LosslessRuntimeReceipt|null=null;
  private flight:Promise<LosslessRuntimeReceipt>|null=null;
  private epoch=0;private lastObservedAt=0;
  private listeners=new Set<()=>void>();
  constructor(private detect:()=>Promise<LosslessRuntimeReceipt>){}
  get revision(){return this.epoch;}
  subscribe(cb:()=>void){this.listeners.add(cb);return ()=>{this.listeners.delete(cb);};}
  private changed(){for(const cb of [...this.listeners]){try{cb();}catch{}}}
  accept(value:LosslessRuntimeReceipt){if(value.observedAt&&value.observedAt<this.lastObservedAt)return;this.lastObservedAt=value.observedAt??this.lastObservedAt;this.epoch++;this.document=value;this.changed();}
  invalidate(){this.epoch++;this.document=null;}
  read():Promise<LosslessRuntimeReceipt>{
    if(this.document)return Promise.resolve(this.document);
    if(this.flight)return this.flight.then(()=>this.document??this.read());
    const epoch=this.epoch;
    const work=this.detect().then(value=>{
      if(epoch===this.epoch)this.document=value;
      return this.document??{known:false,running:false,readable:false,pid:0,processCreated:'0',path:'',button:'',status:'',reason:'observation-invalidated',configRevision:0};
    });
    this.flight=work;void work.finally(()=>{if(this.flight===work)this.flight=null;}).catch(()=>{});return work;
  }
}
const owner=new LosslessStateOwner(()=>losslessRuntime.detect());
let consumers=0,gamePresent=false,suspended=false,resident=0;
let configured='',configuring=Promise.resolve();
let residentStops:Array<()=>void>=[];
function configure(){
  const active=!suspended&&(gamePresent||consumers>0),detailed=active&&consumers>0;
  const key=String(active)+String(detailed);if(key===configured)return;configured=key;
  // Serialize interest changes; a late older request cannot re-enable a watch.
  configuring=configuring.catch(()=>{}).then(()=>losslessRuntime.watch(active,detailed)).then(()=>{},()=>{if(configured===key)configured='';});
}
export function startLosslessRuntimeOwner():()=>void {
  resident++;
  if(resident===1){suspended=false;
  residentStops=[on('lossless.runtime',value=>owner.accept(value as LosslessRuntimeReceipt)),
    subscribePolicyGameStatus(game=>{gamePresent=!!game;owner.invalidate();configure();}),
    on('power.suspending',()=>{suspended=true;owner.invalidate();configure();}),
    on('power.resumed',()=>{suspended=false;owner.invalidate();configure();})];
  configure();}
  let stopped=false;
  return ()=>{if(stopped)return;stopped=true;if(--resident)return;for(const stop of residentStops)stop();residentStops=[];gamePresent=false;suspended=true;owner.invalidate();configure();};
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
