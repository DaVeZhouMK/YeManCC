import {applyGameMediaRange,DEFAULT_GAME_MEDIA_PLAYBACK,randomGameMediaRange} from '../../src/bridge/gameMediaPlayback';
import {enterWallpaper,type WallpaperEntrance} from './wallpaperEntrance';
export interface WallpaperVideoFrame {contextSeq:number;appId:string;opacity?:number;blur?:number;entry:{assetId:string;revision:string;bindingRevision:number;kind:string;url:string;playback?:typeof DEFAULT_GAME_MEDIA_PLAYBACK};}
interface PlayerOptions {entrance?:WallpaperEntrance;allowed?:()=>boolean;presented?:(frame:WallpaperVideoFrame)=>void;}
type Slot={node:HTMLVideoElement;frame:WallpaperVideoFrame;key:string;ready:boolean;dead:boolean;presented:boolean;animation:Animation|null;playRequested:boolean;playAgain:boolean;styleKey:string;playback:{start:number;end:number;initialStart:number}|null;};
const identity=(frame:WallpaperVideoFrame)=>JSON.stringify([frame.entry.assetId,frame.entry.revision,frame.entry.bindingRevision,frame.entry.url,frame.entry.playback||DEFAULT_GAME_MEDIA_PLAYBACK]);
// Only the selected video owns a source. No neighbor reads or predicted decoder.
export class WallpaperPlayers {
  private current:Slot|null=null;private container:HTMLElement|null=null;
  constructor(private doc:Document,private failed:(frame:WallpaperVideoFrame)=>void,private options:PlayerOptions={}){}
  // Recheck before every source/play/reveal boundary; a late media event or
  // promise cannot revive a background decoder while React is catching up.
  private admitted(){try{return this.doc.visibilityState!=='hidden'&&this.doc.hasFocus()&&(this.options.allowed?.()??true);}catch{return false;}}
  private release(slot:Slot|null){if(!slot||slot.dead)return;slot.dead=true;slot.animation?.cancel();slot.node.pause();slot.node.removeAttribute('src');slot.node.load();slot.node.remove();}
  clearCurrent(){this.container?.parentElement?.removeAttribute('data-ymcc-wallpaper-ready');this.container=null;this.release(this.current);this.current=null;}
  stop(){this.clearCurrent();}
  private create(frame:WallpaperVideoFrame):Slot {
    const node=this.doc.createElement('video');node.muted=true;node.loop=true;node.playsInline=true;node.preload='metadata';node.style.width='100%';node.style.height='100%';node.setAttribute('aria-hidden','true');
    const slot:Slot={node,frame,key:identity(frame),ready:false,dead:false,presented:false,animation:null,playRequested:false,playAgain:false,styleKey:'',playback:null};
    const ready=()=>{if(!this.admitted()){this.stop();return;}if(slot.dead||this.current!==slot||node.readyState<2||node.seeking)return;const range=slot.playback;if(!range)return;if(!slot.ready&&node.currentTime<range.initialStart-.08)return;slot.ready=true;node.dataset.ymccPrepared='true';this.reveal(slot);};
    node.addEventListener('loadedmetadata',()=>{if(slot.dead)return;if(!this.admitted()){this.stop();return;}if(!slot.playback)slot.playback=randomGameMediaRange(node.duration,frame.entry.playback||DEFAULT_GAME_MEDIA_PLAYBACK);node.dataset.ymccPlaybackStart=String(slot.playback.initialStart);node.dataset.ymccPlaybackEnd=String(slot.playback.end);node.currentTime=slot.playback.initialStart;});
    for(const event of ['loadeddata','seeked','canplay'])node.addEventListener(event,ready);
    node.addEventListener('timeupdate',()=>{if(!this.admitted()){this.stop();return;}if(this.current===slot&&!slot.dead)applyGameMediaRange(node,slot.playback||DEFAULT_GAME_MEDIA_PLAYBACK);});
    node.addEventListener('error',()=>{if(slot.dead)return;const selected=this.current===slot;this.release(slot);if(selected){this.current=null;this.container?.parentElement?.removeAttribute('data-ymcc-wallpaper-ready');this.failed(slot.frame);}});
    node.src=frame.entry.url;return slot;
  }
  private play(slot:Slot){
    if(!this.admitted()){this.stop();return;}
    if(slot.dead||this.current!==slot||!slot.node.paused)return;
    if(slot.playRequested){slot.playAgain ||= slot.ready;return;}
    slot.playRequested=true;slot.playAgain=false;let rejected=false;
    // A ready/seek event can arrive before an earlier interrupted play settles.
    // Retain one retry demand, never a periodic retry or a second pending play.
    try{void Promise.resolve(slot.node.play()).catch(()=>{rejected=true;}).finally(()=>{const retry=rejected&&slot.playAgain;slot.playRequested=false;slot.playAgain=false;if(retry)this.play(slot);});}catch{slot.playRequested=false;slot.playAgain=false;}
  }
  private reveal(slot:Slot){if(!this.admitted()){this.stop();return;}if(slot.dead||this.current!==slot||!this.container)return;const node=slot.node;if(node.style.visibility!=='visible')node.style.visibility='visible';if(!slot.presented){slot.presented=true;slot.animation=enterWallpaper(node,this.options.entrance);node.dataset.ymccEntrance=this.options.entrance||'fade';this.options.presented?.(slot.frame);}const target=this.container.parentElement;if(target&&target.getAttribute('data-ymcc-wallpaper-ready')!=='true')target.setAttribute('data-ymcc-wallpaper-ready','true');this.play(slot);}
  show(container:HTMLElement,frame:WallpaperVideoFrame){
    if(!this.admitted()){this.stop();return;}
    const key=identity(frame);if(this.current?.key!==key){this.clearCurrent();this.current=this.create(frame);}
    const slot=this.current!;slot.frame=frame;this.container=container;const node=slot.node;const fit=frame.entry.playback?.fit||'cover',opacity=String(frame.opacity??.64),filter=`blur(${frame.blur||0}px)`,styleKey=JSON.stringify([fit,opacity,filter]);if(slot.styleKey!==styleKey){slot.styleKey=styleKey;Object.assign(node.style,{objectFit:fit,opacity,filter});}const visibility=slot.ready?'visible':'hidden';if(node.style.visibility!==visibility)node.style.visibility=visibility;
    if(node.parentNode!==container)container.appendChild(node);
    if(slot.ready)this.reveal(slot);else{if(node.preload!=='auto')node.preload='auto';this.play(slot);}
  }
}
