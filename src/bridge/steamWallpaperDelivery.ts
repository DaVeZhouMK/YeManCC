import type {MirrorPeer} from './deckyMirrorRelay';
type Channel='ready'|'frame';
type Message=Record<string,unknown>;
interface Slot {message:Message;signature:string;version:number;acknowledged:number;pending:boolean;failed:boolean;}
interface Ports {send:(peer:MirrorPeer,message:Message)=>Promise<boolean>;schedule:typeof setTimeout;cancel:typeof clearTimeout;}
// Two latest-value lanes only. Retry an explicit refusal/lost receipt without
// repeating media lookup, inventory work, or an obsolete selection.
export class SteamWallpaperDelivery {
  private peer:MirrorPeer|null=null;
  private epoch=0;
  private ended=false;
  private slots=new Map<Channel,Slot>();
  private timer:ReturnType<typeof setTimeout>|null=null;
  constructor(private port:Ports){}
  reset(peer:MirrorPeer|null){if(this.ended)return;++this.epoch;this.peer=peer?{...peer}:null;this.slots.clear();this.cancelTimer();}
  private cancelTimer(){if(this.timer!==null)this.port.cancel(this.timer);this.timer=null;}
  put(channel:Channel,message:Message,force=false){
    if(this.ended||!this.peer)return;
    const signature=JSON.stringify(message);let slot=this.slots.get(channel);
    if(!force&&slot?.signature===signature)return;
    if(!slot){slot={message,signature,version:1,acknowledged:0,pending:false,failed:false};this.slots.set(channel,slot);}
    else{slot.message=message;slot.signature=signature;++slot.version;slot.failed=false;}
    this.pump(slot);this.reconcileTimer();
  }
  private pump(slot:Slot){
    if(this.ended||!this.peer||slot.pending||slot.version===slot.acknowledged)return;
    slot.pending=true;slot.failed=false;
    const epoch=this.epoch,peer=this.peer,version=slot.version,message=slot.message;
    let accepted=false;
    void Promise.resolve().then(()=>this.ended||epoch!==this.epoch||slot.version!==version?false:this.port.send(peer,message)).then(value=>{accepted=value===true;}).catch(()=>{}).finally(()=>{
      if(this.ended||epoch!==this.epoch)return;
      slot.pending=false;
      if(accepted)slot.acknowledged=version;
      slot.failed=!accepted&&slot.version===version;
      if(slot.version!==version)this.pump(slot);
      this.reconcileTimer();
    });
  }
  private reconcileTimer(){
    const failed=!this.ended&&this.peer&&[...this.slots.values()].some(slot=>slot.failed);
    if(!failed){this.cancelTimer();return;}
    if(this.timer!==null)return;
    this.timer=this.port.schedule(()=>{this.timer=null;for(const slot of this.slots.values())if(slot.failed)this.pump(slot);this.reconcileTimer();},20000);
  }
  stop(){if(this.ended)return;this.reset(null);this.ended=true;}
}
