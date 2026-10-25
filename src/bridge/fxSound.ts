// One YMCC owner. Reads never open FxSound UI or write configuration. The
// sidebar retains observation only while visible; its close does not undo ON.
import {invoke} from './ipc';
export interface FxSoundState {known:boolean;installed:boolean;running:boolean;enabled:boolean;selected:boolean;preset:string;output:string;reason:string;}
export interface FxSoundResult {ok:boolean;copied:boolean;reused:boolean;repaired:boolean;reason:string;state:FxSoundState;}
export const unknownFxSound=():FxSoundState=>({known:false,installed:false,running:false,enabled:false,selected:false,preset:'',output:'',reason:''});
export class FxSoundOwner {
  private value=unknownFxSound();private flight:Promise<FxSoundState>|null=null;private busy=false;private epoch=0;
  private timer:ReturnType<typeof setTimeout>|null=null;private listeners=new Set<()=>void>();
  constructor(private api:{read:()=>Promise<FxSoundState>;set:(enabled:boolean)=>Promise<FxSoundResult>},private schedule= setTimeout,private cancel=clearTimeout){}
  private accept(value:FxSoundState){if(JSON.stringify(value)===JSON.stringify(this.value))return;this.value=value;for(const listener of this.listeners)listener();}
  read(){if(this.flight)return this.flight;if(this.busy)return Promise.resolve(this.value);const epoch=this.epoch;
    const task=this.api.read().then(value=>{if(epoch===this.epoch)this.accept(value);return this.value;},()=>{if(epoch===this.epoch)this.accept(unknownFxSound());return this.value;});
    this.flight=task;void task.finally(()=>{if(this.flight===task)this.flight=null;});return task;
  }
  async setEnabled(enabled:boolean){if(this.busy)throw Error('音质开关正在处理');this.busy=true;this.epoch++;
    try{const result=await this.api.set(enabled);this.accept(result.state);return result;}finally{this.busy=false;}}
  retain(listener:()=>void){this.listeners.add(listener);if(this.listeners.size===1)this.tick();let ended=false;
    return ()=>{if(ended)return;ended=true;this.listeners.delete(listener);if(!this.listeners.size){this.epoch++;if(this.timer!==null)this.cancel(this.timer);this.timer=null;}};}
  private tick(){if(!this.listeners.size)return;void this.read().finally(()=>{if(this.listeners.size&&this.timer===null)this.timer=this.schedule(()=>{this.timer=null;this.tick();},1500);});}
}
export const fxSound=new FxSoundOwner({read:()=>invoke<FxSoundState>('fxsound.getState',{}, {timeoutMs:4000}),set:enabled=>invoke<FxSoundResult>('fxsound.setEnabled',{enabled},{timeoutMs:40000})});
