import {applyGameMediaRange,DEFAULT_GAME_MEDIA_PLAYBACK,randomGameMediaRange} from '../../src/bridge/gameMediaPlayback';
import {enterWallpaper,type WallpaperEntrance} from './wallpaperEntrance';
export interface WallpaperVideoFrame {contextSeq:number;appId:string;opacity?:number;blur?:number;entry:{assetId:string;revision:string;bindingRevision:number;kind:string;url:string;playback?:typeof DEFAULT_GAME_MEDIA_PLAYBACK};}
interface PlayerOptions {entrance?:WallpaperEntrance;allowed?:()=>boolean;presented?:(frame:WallpaperVideoFrame)=>void;now?:()=>number;schedule?:(run:()=>void,delay:number)=>ReturnType<typeof setTimeout>;cancel?:(timer:ReturnType<typeof setTimeout>)=>void;}
type Slot={node:HTMLVideoElement;frame:WallpaperVideoFrame;key:string;ready:boolean;dead:boolean;presented:boolean;animation:Animation|null;playRequested:boolean;styleKey:string;playback:{start:number;end:number;initialStart:number}|null;position:number;progressAt:number;stableAt:number|null;};
const identity=(frame:WallpaperVideoFrame)=>JSON.stringify([frame.entry.assetId,frame.entry.revision,frame.entry.bindingRevision,frame.entry.url,frame.entry.playback||DEFAULT_GAME_MEDIA_PLAYBACK]);
const progressDeadlineMs=15000,stablePlaybackMs=30000,foregroundRetryMs=60000;
// Only the selected video owns a source. No neighbor reads or predicted decoder.
export class WallpaperPlayers {
  private current:Slot|null=null;private container:HTMLElement|null=null;
  private pending:{key:string;frame:WallpaperVideoFrame;container:HTMLElement}|null=null;
  private selectionTimer:ReturnType<typeof setTimeout>|null=null;private selectedAt:number|null=null;
  private recovery:{key:string;frame:WallpaperVideoFrame;container:HTMLElement;position?:number}|null=null;
  private mediaTimer:ReturnType<typeof setTimeout>|null=null;private mediaVersion=0;
  private selectedKey:string|null=null;private failedKey:string|null=null;private retries=0;
  constructor(private doc:Document,private failed:(frame:WallpaperVideoFrame)=>void,private options:PlayerOptions={}){}
  private now(){return this.options.now?.()??this.doc.defaultView?.performance.now()??Date.now();}
  private schedule(run:()=>void,delay:number){return this.options.schedule?this.options.schedule(run,delay):globalThis.setTimeout(run,delay);}
  private cancel(timer:ReturnType<typeof setTimeout>|null){if(timer!==null){if(this.options.cancel)this.options.cancel(timer);else globalThis.clearTimeout(timer);}}
  // Recheck before every source/play/reveal boundary; a late media event or
  // promise cannot revive a background decoder while React is catching up.
  private admitted(){try{return this.doc.visibilityState!=='hidden'&&this.doc.hasFocus()&&(this.options.allowed?.()??false);}catch{return false;}}
  private active(slot:Slot){return !slot.dead&&this.current===slot;}
  private release(slot:Slot|null){if(!slot||slot.dead)return;slot.dead=true;slot.animation?.cancel();slot.node.pause();slot.node.removeAttribute('src');slot.node.load();slot.node.remove();}
  private cancelSelection(){this.cancel(this.selectionTimer);this.selectionTimer=null;this.pending=null;}
  private cancelMedia(){++this.mediaVersion;this.cancel(this.mediaTimer);this.mediaTimer=null;this.recovery=null;}
  private releaseCurrent(){this.cancelMedia();this.container?.parentElement?.removeAttribute('data-ymcc-wallpaper-ready');this.release(this.current);this.current=null;}
  clearCurrent(){this.cancelSelection();this.releaseCurrent();this.container=null;this.selectedKey=this.failedKey=null;this.retries=0;}
  stop(){this.clearCurrent();this.selectedAt=null;}
  // One deadline belongs to the active decoder or its pending local reopen.
  // timeupdate only records progress; it never allocates a timer per frame.
  // Healthy foreground video wakes at most once per15s. Hidden/OFF has none.
  private watch(slot:Slot,delay=progressDeadlineMs){
    if(!this.active(slot)||this.mediaTimer!==null)return;
    const version=++this.mediaVersion;
    this.mediaTimer=this.schedule(()=>{
      if(version!==this.mediaVersion||!this.active(slot))return;++this.mediaVersion;this.mediaTimer=null;
      if(!this.admitted()){this.stop();return;}
      this.progress(slot);
      const remaining=progressDeadlineMs-(this.now()-slot.progressAt);
      if(remaining>0)this.watch(slot,remaining);else this.recover(slot);
    },delay);
  }
  private progress(slot:Slot){
    if(!this.active(slot)||!slot.ready||slot.node.paused||slot.node.seeking)return;
    const position=slot.node.currentTime;
    if(!Number.isFinite(position)||Math.abs(position-slot.position)<.02)return;
    slot.position=position;slot.progressAt=this.now();
    if(slot.stableAt===null)slot.stableAt=slot.progressAt;
    if(slot.progressAt-slot.stableAt>=stablePlaybackMs)this.retries=0;
  }
  private recover(slot:Slot){
    if(!this.active(slot))return;
    if(!this.admitted()){this.stop();return;}
    // Read only the current node error before releasing its src/load state.
    // A timeout, abort, network failure or play() rejection does not prove bad bytes.
    const errorCode=slot.node.error?.code;
    const frame=slot.frame,container=this.container,key=slot.key;
    const position=slot.ready&&Number.isFinite(slot.node.currentTime)?slot.node.currentTime:undefined;
    this.releaseCurrent();
    if(!container||this.selectedKey!==key)return;
    if(this.retries>=2&&(errorCode===3||errorCode===4)){this.failedKey=key;this.failed(frame);return;}
    // Exhausted transient failures retain one selected-foreground retry. Only
    // sustained playback progress restores the quick budget; rerenders do not.
    const delay=this.retries>=2?foregroundRetryMs:[500,2000][this.retries++];
    const recovery={key,frame,container,position};this.recovery=recovery;
    this.mediaTimer=this.schedule(()=>{
      if(this.recovery!==recovery)return;this.mediaTimer=null;this.recovery=null;
      if(!this.admitted()){this.stop();return;}
      if(this.selectedKey===key)this.display(recovery.container,recovery.frame,recovery.position);
    },delay);
  }
  private create(frame:WallpaperVideoFrame,position?:number):Slot {
    const node=this.doc.createElement('video');node.muted=true;node.loop=true;node.playsInline=true;node.preload='metadata';node.style.width='100%';node.style.height='100%';node.setAttribute('aria-hidden','true');
    const slot:Slot={node,frame,key:identity(frame),ready:false,dead:false,presented:false,animation:null,playRequested:false,styleKey:'',playback:null,position:0,progressAt:this.now(),stableAt:null};
    const ready=()=>{if(!this.active(slot))return;if(!this.admitted()){this.stop();return;}if(node.readyState<2||node.seeking)return;const range=slot.playback;if(!range)return;if(!slot.ready&&node.currentTime<range.initialStart-.08)return;slot.ready=true;node.dataset.ymccPrepared='true';this.reveal(slot);};
    node.addEventListener('loadedmetadata',()=>{if(!this.active(slot))return;if(!this.admitted()){this.stop();return;}if(slot.playback)return;slot.playback=randomGameMediaRange(node.duration,frame.entry.playback||DEFAULT_GAME_MEDIA_PLAYBACK);if(position!==undefined&&position>=slot.playback.start&&position<slot.playback.end-.08)slot.playback.initialStart=position;node.dataset.ymccPlaybackStart=String(slot.playback.initialStart);node.dataset.ymccPlaybackEnd=String(slot.playback.end);slot.position=slot.playback.initialStart;node.currentTime=slot.position;});
    for(const event of ['loadeddata','seeked','canplay'])node.addEventListener(event,ready);
    node.addEventListener('timeupdate',()=>{if(!this.active(slot))return;if(!this.admitted()){this.stop();return;}applyGameMediaRange(node,slot.playback||DEFAULT_GAME_MEDIA_PLAYBACK);this.progress(slot);});
    node.addEventListener('error',()=>this.recover(slot));
    node.src=frame.entry.url;return slot;
  }
  private play(slot:Slot){
    if(!this.active(slot))return;
    if(!this.admitted()){this.stop();return;}
    if(!slot.node.paused||slot.playRequested)return;
    slot.playRequested=true;
    // A rejection and a never-settling play use the same bounded recovery as a
    // failed load. Ready-event storms cannot form a microtask retry loop.
    try{void Promise.resolve(slot.node.play()).catch(()=>this.recover(slot)).finally(()=>{slot.playRequested=false;});}catch{slot.playRequested=false;this.recover(slot);}
  }
  private reveal(slot:Slot){if(!this.active(slot)||!this.container)return;if(!this.admitted()){this.stop();return;}const node=slot.node;if(node.style.visibility!=='visible')node.style.visibility='visible';if(!slot.presented){slot.presented=true;slot.animation=enterWallpaper(node,this.options.entrance);node.dataset.ymccEntrance=this.options.entrance||'fade';this.options.presented?.(slot.frame);}const target=this.container.parentElement;if(target&&target.getAttribute('data-ymcc-wallpaper-ready')!=='true')target.setAttribute('data-ymcc-wallpaper-ready','true');this.play(slot);}
  show(container:HTMLElement,frame:WallpaperVideoFrame){
    if(!this.admitted()){this.stop();return;}
    const key=identity(frame);
    if(this.pending?.key===key){this.pending.frame=frame;this.pending.container=container;return;}
    if(this.recovery?.key===key){this.recovery.frame=frame;this.recovery.container=container;return;}
    if(this.failedKey===key)return;
    if(this.selectedKey!==key){
      const now=this.now(),rapid=this.selectedAt!==null&&now-this.selectedAt<120;this.selectedAt=now;
      this.clearCurrent();this.selectedKey=key;
      if(rapid){
        // A quick pass over intermediate cards must not start a decoder or
        // fetch a full clip. One short latest-selection timer, never polling.
        const next={key,frame,container};this.pending=next;
        const start=()=>{if(this.pending!==next)return;this.selectionTimer=null;this.pending=null;if(this.admitted())this.display(next.container,next.frame);};
        this.selectionTimer=this.schedule(start,90);return;
      }
    }
    this.display(container,frame);
  }
  private display(container:HTMLElement,frame:WallpaperVideoFrame,position?:number){
    if(!this.admitted()){this.stop();return;}
    const key=identity(frame);if(this.selectedKey!==key||this.failedKey===key)return;
    if(this.current?.key!==key){this.releaseCurrent();this.current=this.create(frame,position);this.watch(this.current);}
    const slot=this.current!;slot.frame=frame;this.container=container;const node=slot.node;const fit=frame.entry.playback?.fit||'cover',opacity=String(frame.opacity??.64),filter=(frame.blur??0)>0?`blur(${frame.blur}px)`:'none',styleKey=JSON.stringify([fit,opacity,filter]);if(slot.styleKey!==styleKey){slot.styleKey=styleKey;Object.assign(node.style,{objectFit:fit,opacity,filter});}const visibility=slot.ready?'visible':'hidden';if(node.style.visibility!==visibility)node.style.visibility=visibility;
    if(node.parentNode!==container)container.appendChild(node);
    // Metadata and the selected random seek must be ready before play().
    if(slot.ready)this.reveal(slot);
  }
}
