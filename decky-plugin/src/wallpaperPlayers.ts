import {applyGameMediaRange,DEFAULT_GAME_MEDIA_PLAYBACK,randomGameMediaRange} from '../../src/bridge/gameMediaPlayback';
import {enterWallpaper,type WallpaperEntrance} from './wallpaperEntrance';
export interface WallpaperVideoFrame {contextSeq:number;appId:string;opacity?:number;blur?:number;entry:{assetId:string;revision:string;bindingRevision:number;kind:string;url:string;reducedPlayback?:true;recoverOriginal?:true;playback?:typeof DEFAULT_GAME_MEDIA_PLAYBACK};}
type StartupTiming={reducedPlayback:boolean;prepareMs:number;metadataMs:number|null;seekedMs:number|null;seekTargetS:number|null;};
interface PlayerOptions {entrance?:WallpaperEntrance;allowed?:()=>boolean;presented?:(frame:WallpaperVideoFrame,detail:StartupTiming)=>void;observeComposition?:()=>boolean;composited?:(frame:WallpaperVideoFrame,detail:{sinceRevealMs:number;presentedFrames:number})=>void;recovering?:(frame:WallpaperVideoFrame,detail:{reason:string;inactiveMs:number;retryDelayMs:number;readyState:number;errorCode?:number;position?:number}&Partial<StartupTiming>)=>void;now?:()=>number;schedule?:(run:()=>void,delay:number)=>ReturnType<typeof setTimeout>;cancel?:(timer:ReturnType<typeof setTimeout>)=>void;}
type Slot={node:HTMLVideoElement;frame:WallpaperVideoFrame;key:string;ready:boolean;dead:boolean;presented:boolean;compositionRequest:number|null;animation:Animation|null;playRequested:boolean;styleKey:string;playback:{start:number;end:number;initialStart:number}|null;position:number;startedAt:number;metadataAt:number|null;seekedAt:number|null;progressAt:number;stableAt:number|null;interruption:{at:number;reason:string}|null;};
const identity=(frame:WallpaperVideoFrame)=>JSON.stringify([frame.appId,frame.entry.assetId,frame.entry.revision,frame.entry.bindingRevision,frame.entry.url,frame.entry.playback||DEFAULT_GAME_MEDIA_PLAYBACK]);
const progressDeadlineMs=15000,interruptionDeadlineMs=2500,stablePlaybackMs=30000,foregroundRetryMs=60000;
const derivativeStartupMs=900;
const canRecoverOriginal=(frame:WallpaperVideoFrame)=>frame.entry.reducedPlayback===true&&frame.entry.recoverOriginal===true;
// Only the selected video owns a source. No neighbor reads or predicted decoder.
export class WallpaperPlayers {
  private current:Slot|null=null;private container:HTMLElement|null=null;
  private readyTarget:HTMLElement|null=null;
  private pending:{key:string;frame:WallpaperVideoFrame;container:HTMLElement}|null=null;
  private selectionTimer:ReturnType<typeof setTimeout>|null=null;private selectedAt:number|null=null;
  private recovery:{key:string;frame:WallpaperVideoFrame;container:HTMLElement;position?:number}|null=null;
  private mediaTimer:ReturnType<typeof setTimeout>|null=null;private mediaVersion=0;private mediaWatchDue=Infinity;
  private selectedKey:string|null=null;private failure:{key:string;seq:number;deferred:boolean;temporary:boolean}|null=null;private retries=0;private selectedPresented=false;
  constructor(private doc:Document,private failed:(frame:WallpaperVideoFrame,temporary?:boolean)=>boolean|void,private options:PlayerOptions={}){}
  private now(){return this.options.now?.()??this.doc.defaultView?.performance.now()??Date.now();}
  private schedule(run:()=>void,delay:number){return this.options.schedule?this.options.schedule(run,delay):globalThis.setTimeout(run,delay);}
  private cancel(timer:ReturnType<typeof setTimeout>|null){if(timer!==null){if(this.options.cancel)this.options.cancel(timer);else globalThis.clearTimeout(timer);}}
  // Recheck before every source/play/reveal boundary; a late media event or
  // promise cannot revive a background decoder while React is catching up.
  private admitted(){try{return this.doc.visibilityState!=='hidden'&&this.doc.hasFocus()&&(this.options.allowed?.()??false);}catch{return false;}}
  private active(slot:Slot){return !slot.dead&&this.current===slot;}
  private release(slot:Slot|null){
    if(!slot||slot.dead)return;slot.dead=true;
    const compositionRequest=slot.compositionRequest;slot.compositionRequest=null;
    if(compositionRequest!==null){try{slot.node.cancelVideoFrameCallback(compositionRequest);}catch{}}
    slot.animation?.cancel();slot.node.pause();slot.node.removeAttribute('src');slot.node.load();slot.node.remove();
  }
  // One optional compositor submission receipt per decoder, not a display or
  // sustained-playback guarantee. Neither registration nor delivery owns recovery.
  private observeComposition(slot:Slot){
    const node=slot.node,{appId,contextSeq}=slot.frame;
    try{
      if(!this.options.composited||typeof node.requestVideoFrameCallback!=='function'||typeof node.cancelVideoFrameCallback!=='function'||!this.options.observeComposition?.())return;
      const started=this.now();
      slot.compositionRequest=node.requestVideoFrameCallback((_now,metadata)=>{
        if(slot.compositionRequest===null)return;slot.compositionRequest=null;
        if(!this.active(slot)||!node.isConnected||node.parentNode!==this.container||!this.admitted()||slot.frame.appId!==appId||slot.frame.contextSeq!==contextSeq)return;
        const sinceRevealMs=metadata.presentationTime-started,presentedFrames=metadata.presentedFrames;
        if(!Number.isFinite(sinceRevealMs)||sinceRevealMs<0||!Number.isSafeInteger(presentedFrames)||presentedFrames<1)return;
        try{this.options.composited?.(slot.frame,{sinceRevealMs:Math.round(sinceRevealMs),presentedFrames});}catch{}
      });
    }catch{}
  }
  private cancelSelection(){this.cancel(this.selectionTimer);this.selectionTimer=null;this.pending=null;}
  private cancelMedia(){++this.mediaVersion;this.cancel(this.mediaTimer);this.mediaTimer=null;this.recovery=null;}
  private clearReadyTarget(){
    // Steam can move or detach the output before the decoder is released.
    const target=this.readyTarget;this.readyTarget=null;
    target?.removeAttribute('data-ymcc-wallpaper-ready');
  }
  private releaseCurrent(){this.cancelMedia();this.clearReadyTarget();this.release(this.current);this.current=null;}
  clearCurrent(){this.cancelSelection();this.releaseCurrent();this.container=null;this.selectedKey=null;this.failure=null;this.retries=0;this.selectedPresented=false;}
  stop(){this.clearCurrent();this.selectedAt=null;}
  // One deadline belongs to the active decoder or its pending local reopen.
  // timeupdate only records progress; it never allocates a timer per frame.
  // Stall signals may shorten the current watch but never replace an earlier
  // check. The callback rechecks real progress before taking recovery action.
  private confirmationDue(slot:Slot){
    if(!slot.ready&&!this.selectedPresented&&canRecoverOriginal(slot.frame))return slot.startedAt+derivativeStartupMs;
    return Math.min(slot.progressAt+progressDeadlineMs,slot.interruption?slot.interruption.at+(slot.interruption.reason==='seek-timeout'?5000:interruptionDeadlineMs):Infinity);
  }
  private watch(slot:Slot,due=this.confirmationDue(slot)){
    if(!this.active(slot))return;
    if(this.mediaTimer!==null){
      if(this.mediaWatchDue<=due)return;
      this.cancelMedia();
    }
    const version=++this.mediaVersion;
    this.mediaWatchDue=due;
    this.mediaTimer=this.schedule(()=>{
      if(version!==this.mediaVersion||!this.active(slot))return;++this.mediaVersion;this.mediaTimer=null;
      if(!this.admitted()){this.stop();return;}
      this.progress(slot);
      const nextDue=this.confirmationDue(slot);
      if(nextDue>this.now()){this.watch(slot,nextDue);return;}
      if(!slot.ready&&!this.selectedPresented&&canRecoverOriginal(slot.frame)){
        // The media task may already have finished while its ready event is queued.
        this.updateReady(slot);
        if(this.active(slot)&&!slot.ready)this.recover(slot,slot.node.error?'media-error':'derivative-startup-timeout');
        return;
      }
      this.recover(slot,slot.interruption?.reason??'progress-timeout');
    },Math.max(0,due-this.now()));
  }
  private progress(slot:Slot){
    if(!this.active(slot)||!slot.ready||slot.node.paused||slot.node.seeking)return;
    const position=slot.node.currentTime;
    if(!Number.isFinite(position)||Math.abs(position-slot.position)<.02)return;
    slot.position=position;slot.progressAt=this.now();
    // Real progress after an interruption starts a new stability window.
    if(slot.interruption||slot.stableAt===null)slot.stableAt=slot.progressAt;
    slot.interruption=null;
    if(slot.progressAt-slot.stableAt>=stablePlaybackMs)this.retries=0;
  }
  private interrupted(slot:Slot,reason:string){
    if(!this.active(slot)||!slot.ready||slot.node.seeking)return;
    if(!this.admitted()){this.stop();return;}
    this.progress(slot);if(slot.interruption)return;
    slot.interruption={at:this.now(),reason};
    this.watch(slot,this.confirmationDue(slot));
  }
  private seeking(slot:Slot){
    if(!this.active(slot)||!slot.presented||slot.frame.entry.recoverOriginal!==true||slot.interruption)return;
    if(!this.admitted()){this.stop();return;}
    slot.interruption={at:this.now(),reason:'seek-timeout'};
    this.watch(slot,this.confirmationDue(slot));
  }
  private recover(slot:Slot,reason='media-error'){
    if(!this.active(slot))return;
    if(!this.admitted()){this.stop();return;}
    // Read only the current node error before releasing its src/load state.
    // A timeout, abort, network failure or play() rejection does not prove bad bytes.
    const errorCode=slot.node.error?.code,readyState=slot.node.readyState,inactiveMs=Math.max(0,Math.round(this.now()-slot.progressAt));
    const frame=slot.frame,container=this.container,key=slot.key;
    const position=slot.ready&&Number.isFinite(slot.node.currentTime)?slot.node.currentTime:undefined;
    this.releaseCurrent();
    if(!container||this.selectedKey!==key)return;
    // A failed read or rejected source before the first frame does not prove
    // the derivative is corrupt. Use the original for this selection now.
    const coldStartupTimeout=reason==='derivative-startup-timeout';
    const coldReadFailure=!this.selectedPresented&&reason==='media-error'&&(errorCode===2||errorCode===4);
    const warmRetryFailed=this.selectedPresented&&this.retries>=1&&errorCode!==3;
    const temporary=canRecoverOriginal(frame)&&(coldStartupTimeout||coldReadFailure||warmRetryFailed);
    const terminal=temporary||this.retries>=2&&(errorCode===3||errorCode===4&&frame.entry.reducedPlayback===true);
    // Exhausted transient failures retain one selected-foreground retry. Only
    // sustained playback progress restores the quick budget; rerenders do not.
    const delay=terminal?0:this.retries>=2?foregroundRetryMs:[500,2000][this.retries++];
    try{this.options.recovering?.(frame,{reason,inactiveMs,retryDelayMs:delay,readyState,errorCode,position,...(coldStartupTimeout?this.startupTiming(slot):{})});}catch{}
    if(terminal){this.reportFailure(key,frame,temporary);return;}
    const recovery={key,frame,container,position};this.recovery=recovery;
    this.mediaTimer=this.schedule(()=>{
      if(this.recovery!==recovery)return;this.mediaTimer=null;this.recovery=null;
      if(!this.admitted()){this.stop();return;}
      if(this.selectedKey===key)this.display(recovery.container,recovery.frame,recovery.position);
    },delay);
  }
  private reportFailure(key:string,frame:WallpaperVideoFrame,temporary:boolean){
    // A held frame can fail while the host is confirming a newer context.
    // Retry a refused report only on a fresh frame for this exact selection.
    const failure={key,seq:frame.contextSeq,deferred:false,temporary};this.failure=failure;
    failure.deferred=this.failed(frame,temporary)===false;
  }
  private startupTiming(slot:Slot):StartupTiming {
    const elapsed=(at:number|null)=>at===null?null:Math.max(0,Math.round(at-slot.startedAt));
    const target=slot.playback?.initialStart;
    return {reducedPlayback:slot.frame.entry.reducedPlayback===true,prepareMs:elapsed(this.now())!,metadataMs:elapsed(slot.metadataAt),seekedMs:elapsed(slot.seekedAt),seekTargetS:target===undefined?null:Math.round(target*1000)/1000};
  }
  private updateReady(slot:Slot){
    if(!this.active(slot))return;if(!this.admitted()){this.stop();return;}
    const {node,playback:range}=slot;
    if(node.error||node.readyState<2||node.seeking||!range)return;
    if(!slot.ready){
      if(node.currentTime<range.initialStart-.08)return;
      slot.progressAt=this.now();slot.ready=true;node.dataset.ymccPrepared='true';
      // Retire the short cold watch; healthy derivatives use the normal watch.
      if(!this.selectedPresented&&canRecoverOriginal(slot.frame)){this.cancelMedia();this.watch(slot);}
    }
    this.reveal(slot);
  }
  private create(frame:WallpaperVideoFrame,position?:number):Slot {
    const startedAt=this.now();
    const node=this.doc.createElement('video');node.muted=true;node.loop=true;node.playsInline=true;node.preload='metadata';node.style.width='100%';node.style.height='100%';node.setAttribute('aria-hidden','true');
    const slot:Slot={node,frame,key:identity(frame),ready:false,dead:false,presented:false,compositionRequest:null,animation:null,playRequested:false,styleKey:'',playback:null,position:0,startedAt,metadataAt:null,seekedAt:null,progressAt:startedAt,stableAt:null,interruption:null};
    node.addEventListener('loadedmetadata',()=>{if(!this.active(slot))return;if(!this.admitted()){this.stop();return;}if(slot.playback)return;slot.metadataAt=this.now();slot.playback=randomGameMediaRange(node.duration,frame.entry.playback||DEFAULT_GAME_MEDIA_PLAYBACK);if(position!==undefined&&position>=slot.playback.start&&position<slot.playback.end-.08)slot.playback.initialStart=position;node.dataset.ymccPlaybackStart=String(slot.playback.initialStart);node.dataset.ymccPlaybackEnd=String(slot.playback.end);slot.position=slot.playback.initialStart;node.currentTime=slot.position;});
    for(const event of ['loadeddata','seeked','canplay'])node.addEventListener(event,()=>{
      if(event==='seeked'&&!slot.ready&&this.active(slot)&&slot.playback&&slot.seekedAt===null)slot.seekedAt=this.now();
      this.updateReady(slot);
    });
    node.addEventListener('timeupdate',()=>{if(!this.active(slot))return;if(!this.admitted()){this.stop();return;}applyGameMediaRange(node,slot.playback||DEFAULT_GAME_MEDIA_PLAYBACK);this.progress(slot);});
    node.addEventListener('error',()=>this.recover(slot));
    node.addEventListener('seeking',()=>this.seeking(slot));
    for(const event of ['waiting','stalled','pause'])node.addEventListener(event,()=>this.interrupted(slot,event));
    node.src=frame.entry.url;return slot;
  }
  private play(slot:Slot){
    if(!this.active(slot))return;
    if(!this.admitted()){this.stop();return;}
    if(!slot.node.paused||slot.playRequested)return;
    slot.playRequested=true;
    // A rejection and a never-settling play use the same bounded recovery as a
    // failed load. Ready-event storms cannot form a microtask retry loop.
    try{void Promise.resolve(slot.node.play()).catch(()=>this.recover(slot,'play-rejected')).finally(()=>{slot.playRequested=false;});}catch{slot.playRequested=false;this.recover(slot,'play-threw');}
  }
  private reveal(slot:Slot){
    if(!this.active(slot)||!this.container)return;
    if(!this.admitted()){this.stop();return;}
    const node=slot.node;if(node.style.visibility!=='visible')node.style.visibility='visible';
    if(!slot.presented){slot.presented=true;this.selectedPresented=true;slot.animation=enterWallpaper(node,this.options.entrance);node.dataset.ymccEntrance=this.options.entrance||'fade';this.options.presented?.(slot.frame,this.startupTiming(slot));this.observeComposition(slot);}
    const target=this.container.parentElement;
    if(target!==this.readyTarget){this.clearReadyTarget();this.readyTarget=target;}
    if(target&&target.getAttribute('data-ymcc-wallpaper-ready')!=='true')target.setAttribute('data-ymcc-wallpaper-ready','true');
    this.play(slot);
  }
  show(container:HTMLElement,frame:WallpaperVideoFrame){
    if(!this.admitted()){this.stop();return;}
    const key=identity(frame);
    if(this.pending?.key===key){this.pending.frame=frame;this.pending.container=container;return;}
    if(this.recovery?.key===key){this.recovery.frame=frame;this.recovery.container=container;return;}
    const failure=this.failure;
    if(failure?.key===key){if(failure.deferred&&frame.contextSeq>failure.seq)this.reportFailure(key,frame,failure.temporary);return;}
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
    const key=identity(frame);if(this.selectedKey!==key||this.failure?.key===key)return;
    if(this.current?.key!==key){this.releaseCurrent();this.current=this.create(frame,position);this.watch(this.current);}
    const slot=this.current!;slot.frame=frame;this.container=container;const node=slot.node;const fit=frame.entry.playback?.fit||'cover',opacity=String(frame.opacity??.64),filter=(frame.blur??0)>0?`blur(${frame.blur}px)`:'none',styleKey=JSON.stringify([fit,opacity,filter]);if(slot.styleKey!==styleKey){slot.styleKey=styleKey;Object.assign(node.style,{objectFit:fit,opacity,filter});}const visibility=slot.ready?'visible':'hidden';if(node.style.visibility!==visibility)node.style.visibility=visibility;
    if(node.parentNode!==container)container.appendChild(node);
    // Metadata and the selected random seek must be ready before play().
    if(slot.ready)this.reveal(slot);
  }
}
