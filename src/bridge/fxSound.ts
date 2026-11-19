// One YMCC owner. Reads never open FxSound UI or write configuration. The
// sidebar retains observation only while visible; its close does not undo ON.
import {invoke} from './ipc';
import type {MouseBootContext} from './gameproc';
export interface FxSoundState {known:boolean;installed:boolean;running:boolean;enabled:boolean;selected:boolean;preset:string;output:string;reason:string;}
export interface FxSoundResult {ok:boolean;copied:boolean;reused:boolean;repaired:boolean;reason:string;state:FxSoundState;}
export const unknownFxSound=():FxSoundState=>({known:false,installed:false,running:false,enabled:false,selected:false,preset:'',output:'',reason:''});
export class FxSoundOwner {
  private value=unknownFxSound();private flight:Promise<FxSoundState>|null=null;private flightEpoch=0;private busy=false;private epoch=0;private observation:object|null=null;
  private timer:ReturnType<typeof setTimeout>|null=null;private listeners=new Set<()=>void>();
  // Browser timer APIs require their Window receiver. Keep wrappers rather
  // than storing the raw functions: this.schedule/this.cancel would otherwise
  // pass FxSoundOwner as receiver and break Decky's retained observation.
  constructor(private api:{read:()=>Promise<FxSoundState>;set:(enabled:boolean,expectedGeneration?:number,bootContext?:MouseBootContext)=>Promise<FxSoundResult>},
    private schedule:(action:()=>void,delay:number)=>ReturnType<typeof setTimeout>=(action,delay)=>setTimeout(action,delay),
    private cancel:(timer:ReturnType<typeof setTimeout>)=>void=timer=>clearTimeout(timer)){}
  private accept(value:FxSoundState){if(JSON.stringify(value)===JSON.stringify(this.value))return;this.value=value;for(const listener of this.listeners){try{listener();}catch{/* Display observers cannot change the native receipt or block other observers. */}}}
  read(){const epoch=this.epoch;if(this.flight){
    // A new demand waits for the retired native read to return, then obtains
    // fresh facts once. Retired demands cannot start that replacement read.
    if(this.flightEpoch===epoch)return this.flight;
    return this.flight.then(()=>epoch===this.epoch?this.read():this.value);
  }if(this.busy)return Promise.resolve(this.value);
    const task=this.api.read().then(value=>{if(epoch===this.epoch)this.accept(value);return this.value;},()=>{if(epoch===this.epoch)this.accept(unknownFxSound());return this.value;});
    this.flight=task;this.flightEpoch=epoch;void task.finally(()=>{if(this.flight===task)this.flight=null;});return task;
  }
  async setEnabled(enabled:boolean,expectedGeneration?:number,bootContext?:MouseBootContext){if(this.busy)throw Error('音质开关正在处理');this.busy=true;this.epoch++;
    try{const result=await this.api.set(enabled,expectedGeneration,bootContext);this.accept(result.state);return result;}finally{this.busy=false;}}
  retain(listener:()=>void){this.listeners.add(listener);if(this.listeners.size===1){this.observation={};this.tick();}let ended=false;
    return ()=>{if(ended)return;ended=true;this.listeners.delete(listener);if(!this.listeners.size){this.observation=null;this.epoch++;if(this.timer!==null)this.cancel(this.timer);this.timer=null;}};}
  // Observation lifetime is distinct from the mutation epoch: a successful
  // set must not stop a still-visible owner's ordinary observation timer.
  private tick(){if(!this.listeners.size)return;const observation=this.observation;void this.read().finally(()=>{if(observation===this.observation&&this.listeners.size&&this.timer===null)this.timer=this.schedule(()=>{if(observation!==this.observation)return;this.timer=null;this.tick();},1500);});}
}
export const fxSound=new FxSoundOwner({read:()=>invoke<FxSoundState>('fxsound.getState',{}, {timeoutMs:4000}),set:(enabled,expectedGeneration,bootContext)=>invoke<FxSoundResult>('fxsound.setEnabled',{enabled,...(expectedGeneration===undefined?{}:{expectedGeneration}),...(bootContext===undefined?{}:{bootContext})},{timeoutMs:40000})});
