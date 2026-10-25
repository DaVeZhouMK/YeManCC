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
  private recovering=false;private epoch=0;private known=false;private reading=false;private again=false;
  private timer:ReturnType<typeof setTimeout>|null=null;private retry:ReturnType<typeof setTimeout>|null=null;
  constructor(port:Omit<Ports,'schedule'|'cancel'>&Partial<Pick<Ports,'schedule'|'cancel'>>){this.port={schedule:(...args)=>globalThis.setTimeout(...args),cancel:(timer)=>globalThis.clearTimeout(timer),...port};this.apply();}
  private apply(){if(!this.stopped)this.port.apply({backgroundBlocked:this.gameRunning||this.snapshot.batterySaver===true||this.snapshot.lowBattery===true,storageBlocked:this.suspended||!this.known||!!this.snapshot.blockedReason,reason:this.suspended?'media-power-transition':!this.known?'capacity-unavailable':this.snapshot.blockedReason|| (this.snapshot.batterySaver?'battery-saver':this.snapshot.lowBattery?'low-battery':this.gameRunning?'game-running':'')});}
  game(running:boolean){if(this.stopped||this.gameRunning===running)return;this.gameRunning=running;this.apply();}
  suspend(){if(this.stopped)return;this.suspended=true;this.epoch++;if(this.timer!==null)this.port.cancel(this.timer);if(this.retry!==null)this.port.cancel(this.retry);this.timer=this.retry=null;this.apply();}
  resume(){if(this.stopped)return;this.suspended=false;this.epoch++;this.known=false;this.apply();this.request();}
  request(){if(this.stopped||this.suspended)return;if(this.reading){this.again=true;return;}if(this.timer!==null)return;this.timer=this.port.schedule(()=>{this.timer=null;void this.read();},250);}
  private async read(){if(this.stopped)return;this.reading=true;const epoch=this.epoch;try{const value=await this.port.read();if(!this.stopped&&epoch===this.epoch){this.snapshot=value;this.known=true;}}
    catch{if(!this.stopped&&epoch===this.epoch){this.known=false;}}
    finally{this.reading=false;if(!this.stopped){this.apply();this.demand();if(this.again){this.again=false;this.request();}}}}
  demand(){if(this.stopped||this.suspended)return;const retry=(this.port.enabled?.()??true)&&this.port.queued()>0;
    if(!retry&&this.retry!==null){this.port.cancel(this.retry);this.retry=null;}
    if(retry&&this.retry===null&&!this.recovering)this.retry=this.port.schedule(()=>{this.retry=null;void this.recover();},20000);
  }
  private async recover(){
    if(this.stopped||this.suspended||this.recovering||!(this.port.enabled?.()??true)||this.port.queued()<=0)return;
    this.recovering=true;const epoch=this.epoch;
    try{await this.port.recheck?.();}catch{/* Preserve current admission on an unavailable return. */}
    finally{this.recovering=false;if(!this.stopped&&!this.suspended){if(epoch===this.epoch)this.request();else this.demand();}}
  }
  stop(){this.stopped=true;if(this.timer!==null)this.port.cancel(this.timer);if(this.retry!==null)this.port.cancel(this.retry);this.timer=this.retry=null;}
}
