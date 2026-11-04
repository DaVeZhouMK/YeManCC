export interface MediaResumeEvent {generation?:number;source?:string;}
// The S0 sleep-intent Ready path does not commit power.resumed. Both native
// Ready paths feed media only; generation fencing keeps an old wake from
// clearing a newer suspend. No timer or hardware/input action belongs here.
export function subscribeGameMediaResume(handler:(event:MediaResumeEvent)=>void,subscribe:(name:string,handler:(event:MediaResumeEvent)=>void)=>()=>void){
  let stopped=false,latest=0,resumed=0;
  const generation=(event:MediaResumeEvent)=>{const value=event?.generation;return Number.isSafeInteger(value)&&Number(value)>0?Number(value):0;};
  const offSuspend=subscribe('power.suspending',event=>{if(stopped)return;const value=generation(event);if(value>latest)latest=value;});
  const ready=(event:MediaResumeEvent)=>{if(stopped)return;const value=generation(event);if(!value||value<latest||value<=resumed)return;latest=resumed=value;handler(event);};
  const offResume=subscribe('power.resumed',ready),offIntent=subscribe('gameMedia.resume-ready',ready);
  return()=>{if(stopped)return;stopped=true;offSuspend();offResume();offIntent();};
}
export interface MediaPreparationSnapshot {blockedReason?:string;batterySaver?:boolean;lowBattery?:boolean;}
interface Ports {
  read:()=>Promise<MediaPreparationSnapshot>;
  apply:(state:{backgroundBlocked:boolean;storageBlocked:boolean;reason:string})=>void;
  queued:()=>number;
  enabled?:()=>boolean;
  recheck?:()=>Promise<void>;
  schedule:typeof setTimeout;
  cancel:typeof clearTimeout;
}
// Events remain the fast path. One slow check exists only for retained work;
// it repairs lost game/power/progress notifications without polling an idle owner.
export class GameMediaPreparationPolicy {
  private port:Ports;
  private stopped=false;private suspended=false;private gameRunning=false;private snapshot:MediaPreparationSnapshot={};
  private epoch=0;private known=false;private reading:number|null=null;private recovering:number|null=null;private again=false;
  private timer:ReturnType<typeof setTimeout>|null=null;private retry:ReturnType<typeof setTimeout>|null=null;
  constructor(port:Omit<Ports,'schedule'|'cancel'>&Partial<Pick<Ports,'schedule'|'cancel'>>){this.port={schedule:(...args)=>globalThis.setTimeout(...args),cancel:(timer)=>globalThis.clearTimeout(timer),...port};this.apply();}
  private apply(){if(!this.stopped)this.port.apply({backgroundBlocked:this.gameRunning||this.snapshot.batterySaver===true||this.snapshot.lowBattery===true,storageBlocked:this.suspended||!this.known||!!this.snapshot.blockedReason,reason:this.suspended?'media-power-transition':!this.known?'capacity-unavailable':this.snapshot.blockedReason|| (this.snapshot.batterySaver?'battery-saver':this.snapshot.lowBattery?'low-battery':this.gameRunning?'game-running':'')});}
  game(running:boolean){if(this.stopped||this.gameRunning===running)return;this.gameRunning=running;this.apply();}
  private current(epoch:number){return !this.stopped&&!this.suspended&&epoch===this.epoch;}
  private retirePending(){
    ++this.epoch;this.reading=this.recovering=null;this.again=false;
    if(this.timer!==null)this.port.cancel(this.timer);if(this.retry!==null)this.port.cancel(this.retry);
    this.timer=this.retry=null;
  }
  suspend(){if(this.stopped)return;this.suspended=true;this.retirePending();this.apply();}
  resume(){if(this.stopped)return;this.suspended=false;this.retirePending();this.known=false;this.apply();this.request();}
  request(){
    if(this.stopped||this.suspended)return;
    if(this.reading!==null){this.again=true;return;}if(this.timer!==null)return;
    const epoch=this.epoch;
    const timer=this.port.schedule(()=>{
      if(this.timer!==timer||!this.current(epoch))return;
      this.timer=null;void this.read(epoch);
    },250);this.timer=timer;
  }
  private async read(epoch:number){
    if(!this.current(epoch))return;this.reading=epoch;
    try{const value=await this.port.read();if(this.current(epoch)){this.snapshot=value;this.known=true;}}
    catch{if(this.current(epoch))this.known=false;}
    finally{if(this.current(epoch)&&this.reading===epoch){
      this.reading=null;this.apply();this.demand();if(this.again){this.again=false;this.request();}
    }}
  }
  demand(){if(this.stopped||this.suspended)return;const retry=(this.port.enabled?.()??true)&&this.port.queued()>0;
    if(!retry&&this.retry!==null){this.port.cancel(this.retry);this.retry=null;}
    if(retry&&this.retry===null&&this.recovering===null){
      const epoch=this.epoch;
      const timer=this.port.schedule(()=>{
        if(this.retry!==timer||!this.current(epoch))return;
        this.retry=null;void this.recover(epoch);
      },20000);this.retry=timer;
    }
  }
  private async recover(epoch:number){
    if(!this.current(epoch)||this.recovering!==null||!(this.port.enabled?.()??true)||this.port.queued()<=0)return;
    this.recovering=epoch;
    try{await this.port.recheck?.();}catch{/* Preserve current admission on an unavailable return. */}
    finally{if(this.current(epoch)&&this.recovering===epoch){this.recovering=null;this.request();}}
  }
  stop(){if(this.stopped)return;this.stopped=true;this.retirePending();}
}
