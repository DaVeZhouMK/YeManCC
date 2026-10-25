// One global fan choice. Debounce only user input; readiness remains with YMCC.
import type { ClientState, MirrorSnapshot } from './mirrorClient';
export const FAN_CHOICE_DELAY_MS = 3000;
export const FAN_CHOICES = [{data:'off',label:'关闭'},{data:'soft',label:'轻柔转速'},{data:'balanced',label:'均衡转速'},{data:'aggressive',label:'暴力转速'}];
export interface PendingFanChoice { value:string; snapshot:MirrorSnapshot; }
export class FanChoiceDebounce {
  private pending:PendingFanChoice|null=null;
  private editing=false;
  private timer:ReturnType<typeof setTimeout>|null=null;
  private listeners=new Set<()=>void>();
  constructor(private deps:{state:()=>ClientState;send:(choice:PendingFanChoice)=>Promise<unknown>;schedule:typeof setTimeout;cancel:typeof clearTimeout}){}
  snapshot(){return this.pending?structuredClone(this.pending):null;}
  subscribe(listener:()=>void){this.listeners.add(listener);return()=>{this.listeners.delete(listener);};}
  private notify(){for(const listener of this.listeners)listener();}
  private valid(choice:PendingFanChoice,state:ClientState){
    const current=state.snapshot,expected=choice.snapshot;
    return state.connected&&!state.busy&&current?.actions?.fan===true&&current.fan.canToggle===true&&
      current.runId===expected.runId&&current.generation===expected.generation&&Number.isSafeInteger(current.revision)&&current.revision>=expected.revision&&
      current.fan.enabled===expected.fan.enabled&&current.fan.preset===expected.fan.preset&&
      FAN_CHOICES.some(o=>o.data===choice.value)&&(choice.value==='off'||current.fan.choices.some(o=>o.data===choice.value&&!o.disabled));
  }
  observe(state:ClientState){if(!this.pending)return;if(!this.valid(this.pending,state)){this.cancel();return;}if(state.snapshot!.revision>this.pending.snapshot.revision)this.pending.snapshot=structuredClone(state.snapshot!);}
  begin(){this.editing=true;if(this.timer!==null)this.deps.cancel(this.timer);this.timer=null;}
  end(){if(!this.editing)return;this.editing=false;if(this.pending)this.arm(this.pending);}
  private arm(choice:PendingFanChoice){this.timer=this.deps.schedule(()=>{if(this.pending!==choice||this.editing)return;this.timer=null;this.pending=null;this.notify();const state=this.deps.state();if(this.valid(choice,state)){choice.snapshot=structuredClone(state.snapshot!);void this.deps.send(choice).catch(()=>{});}},FAN_CHOICE_DELAY_MS);}
  choose(value:string,snapshot:MirrorSnapshot){
    const choice={value,snapshot:structuredClone(snapshot)};if(!this.valid(choice,this.deps.state()))return;
    const editing=this.editing;this.cancel();this.editing=editing;if(value===(snapshot.fan.enabled?snapshot.fan.preset:'off'))return;
    this.pending=choice;if(!this.editing)this.arm(choice);this.notify();
  }
  cancel(){this.editing=false;if(this.timer!==null)this.deps.cancel(this.timer);this.timer=null;if(this.pending){this.pending=null;this.notify();}}
  dispose(){this.cancel();this.listeners.clear();}
}
