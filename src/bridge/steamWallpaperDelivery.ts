import type {MirrorPeer} from './deckyMirrorRelay';
type Channel='ready'|'frame';
type Message=Record<string,unknown>;
interface Slot {message:Message;signature:string;version:number;acknowledged:number;pending:boolean;failed:boolean;}
interface Ports {send:(peer:MirrorPeer,message:Message)=>Promise<boolean>;schedule:typeof setTimeout;cancel:typeof clearTimeout;}
interface RetryTimer {handle:ReturnType<typeof setTimeout>;}
// Only replacement media for the same live selection inherits a refused send.
// Handshakes, context receipts and revocations must not wait behind old media.
function sameVisibleFrame(a:Message,b:Message):boolean {
  return a.type==='wallpaper'&&b.type==='wallpaper'&&a.enabled===true&&b.enabled===true&&a.foreground===true&&b.foreground===true&&
    !!a.entry&&!!b.entry&&a.contextSeq===b.contextSeq&&a.appId===b.appId&&a.stateOwner===b.stateOwner&&a.stateRevision===b.stateRevision;
}
// Two latest-value lanes only. Retry an explicit refusal/lost receipt without
// repeating media lookup, inventory work, or an obsolete selection.
export class SteamWallpaperDelivery {
  private peer:MirrorPeer|null=null;
  private epoch=0;
  private ended=false;
  private recoveryAttempt=0;
  private slots=new Map<Channel,Slot>();
  private timer:RetryTimer|null=null;
  constructor(private port:Ports){}
  reset(peer:MirrorPeer|null){if(this.ended)return;++this.epoch;this.peer=peer?{...peer}:null;this.slots.clear();this.cancelTimer();this.recoveryAttempt=0;}
  private cancelTimer(){const timer=this.timer;this.timer=null;if(timer!==null)this.port.cancel(timer.handle);}
  // A precomputed signature must describe the entire message, including its owner and revision.
  put(channel:Channel,message:Message,force=false,signature?:string){
    if(this.ended||!this.peer)return;
    signature??=JSON.stringify(message);let slot=this.slots.get(channel);
    if(!force&&slot?.signature===signature)return;
    if(!slot){slot={message,signature,version:1,acknowledged:0,pending:false,failed:false};this.slots.set(channel,slot);}
    else{slot.failed=slot.failed&&sameVisibleFrame(slot.message,message);slot.message=message;slot.signature=signature;++slot.version;}
    if(!slot.failed)this.pump(slot);
    this.reconcileTimer();
  }
  private pump(slot:Slot){
    if(this.ended||!this.peer||slot.pending||slot.version===slot.acknowledged)return;
    slot.pending=true;slot.failed=false;
    const epoch=this.epoch,peer=this.peer,version=slot.version,message=slot.message;
    let attempted=false,accepted=false;
    void Promise.resolve().then(()=>{
      if(this.ended||epoch!==this.epoch||slot.version!==version)return false;
      attempted=true;return this.port.send(peer,message);
    }).then(value=>{accepted=value===true;}).catch(()=>{}).finally(()=>{
      if(this.ended||epoch!==this.epoch)return;
      slot.pending=false;
      if(accepted){
        slot.acknowledged=version;
        if([...this.slots.values()].every(value=>value.version===value.acknowledged))this.recoveryAttempt=0;
      }
      slot.failed=attempted&&!accepted&&(slot.version===version||sameVisibleFrame(message,slot.message));
      if(slot.version!==version&&!slot.failed)this.pump(slot);
      this.reconcileTimer();
    });
  }
  private reconcileTimer(){
    const failed=!this.ended&&this.peer&&[...this.slots.values()].some(slot=>slot.failed);
    if(!failed){this.cancelTimer();return;}
    if(this.timer!==null)return;
    // Only unacknowledged latest state owns retries. Recover a brief wake/IPC
    // refusal quickly, then back off; no healthy heartbeat or extra media IO.
    const delays=[250,1000,3000,10000,20000];
    const delay=delays[Math.min(this.recoveryAttempt,delays.length-1)];
    this.recoveryAttempt=Math.min(this.recoveryAttempt+1,delays.length);
    const retry:RetryTimer={handle:this.port.schedule(()=>{
      // A queued callback can outlive cancellation, even within the same peer.
      if(this.timer!==retry)return;
      this.timer=null;
      for(const slot of this.slots.values())if(slot.failed)this.pump(slot);
      this.reconcileTimer();
    },delay)};
    this.timer=retry;
  }
  stop(){if(this.ended)return;this.reset(null);this.ended=true;}
}
