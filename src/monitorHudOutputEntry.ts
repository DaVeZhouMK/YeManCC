// Dedicated multi-page entry: no App, router, store, startup/hardware/gamepad
// imports. Native accepts ONLY ready/layout/close from this renderer.
import { admitHudDragPlacement, type HudDragPlacement } from './bridge/monitorHudDrag';
import { nextTick } from 'vue';
import { invoke, on, isNativeRuntime } from './bridge/ipc';
import { mountMonitorHud } from './bridge/monitorHudRenderer';
import { createHudLayoutLane } from './bridge/monitorHudLayoutLane';
import { validHudViewport, type HudViewport } from './bridge/monitorHudViewport';
import { admitDesktopHudFrame } from './bridge/desktopHudProtocol';
import { createHudTransportLease } from './bridge/monitorHudTransportLease';
import { sameHudLayoutAppearance, type MonitorOutputPreferences } from './bridge/monitorHudModel';
import './styles/monitorHudOutput.css';
const params=new URLSearchParams(location.search),nonce=params.get('nonce')??'',hudId=Number(params.get('hudId'));
let seq=0,position=1,disposed=false;let dragPlacement:HudDragPlacement|null=null;
let layoutAppearance: MonitorOutputPreferences['ymcc'] | null = null;
const container=document.getElementById('hud')!;
const trusted=isNativeRuntime&&Number.isSafeInteger(hudId)&&hudId>0&&/^[a-zA-Z0-9-]{16,80}$/.test(nonce);
let renderer:ReturnType<typeof mountMonitorHud>|null=null;
let screenViewport:HudViewport|undefined,crosshairEnabled=false;
function syncContainerWidth(){if(screenViewport)container.style.width=(crosshairEnabled?screenViewport.width:screenViewport.workArea?.width??screenViewport.width)+'px';}
function acceptViewport(value:unknown){if(!validHudViewport(value))return;screenViewport=value;syncContainerWidth();renderer?.setViewport(value);layoutLane.invalidate();void nextTick(()=>renderer?.measure());}
const offViewport=on<HudViewport & {id:number}>('desktopHud.viewport',value=>{if(trusted&&!disposed&&value.id===hudId)acceptViewport(value);});
const identity={hudId,nonce};
function clearInteractionFeedback(){delete document.documentElement.dataset.hudDragging;container.classList.remove('radial-holding','radial-hold-ready','radial-monitor-return');}
const close=()=>{clearInteractionFeedback();if(trusted)void invoke('desktopHud.close',identity,{timeoutMs:2000}).catch(()=>{});};
const expiryLease=createHudTransportLease({now:Date.now,expire:()=>{container.style.visibility='hidden';close();},
  setTimer:(fn,ms)=>setTimeout(fn,ms),clearTimer:id=>clearTimeout(id as ReturnType<typeof setTimeout>)});
const layoutLane=createHudLayoutLane({sequence:()=>seq,
  send:layout=>invoke<boolean>('desktopHud.layout',{...identity,...layout},{timeoutMs:2000}),
  failure:()=>{container.style.visibility='hidden';close();},
});
const offInteraction=on<{id:number;nonce:string;seq:number;dragging:boolean;toast:boolean;placement:unknown}>('desktopHud.interaction',event=>{
 if(!trusted||disposed||event.id!==hudId||event.nonce!==nonce||!Number.isSafeInteger(event.seq)||event.seq<1||event.seq>seq||typeof event.dragging!=='boolean')return;
 // Identity/sequence validates the end independently of persisted placement.
 // A malformed native version must never keep a released gesture glowing.
 if(!event.dragging)clearInteractionFeedback();
 const p=admitHudDragPlacement(event.placement);if(!p)return;
 if(p.dragLocked)clearInteractionFeedback();
 if(dragPlacement&&p.dragRevision<dragPlacement.dragRevision)return;
 dragPlacement=p;if(crosshairEnabled)renderer?.setPlacement(p);if(event.dragging&&!p.dragLocked){container.classList.remove('radial-holding','radial-hold-ready','radial-monitor-return');document.documentElement.dataset.hudDragging='true';}
});
function dispose(){if(disposed)return;disposed=true;offInteraction();clearInteractionFeedback();window.removeEventListener('blur',clearInteractionFeedback);document.removeEventListener('visibilitychange',onVisibility);container.removeEventListener('animationend',onFeedbackAnimationEnd);layoutLane.dispose();off();offViewport();expiryLease.dispose();layoutAppearance=null;renderer?.dispose();renderer=null;}
const off=on<unknown>('desktopHud.frame',raw=>{
  if(!trusted||disposed)return;
  const frame=admitDesktopHudFrame(raw,hudId,nonce,Date.now(),seq);if(!frame)return;
  if(dragPlacement&&(frame.preferences.ymcc.dragRevision??0)<=dragPlacement.dragRevision)frame.preferences.ymcc={...frame.preferences.ymcc,...dragPlacement};
  document.documentElement.style.setProperty('--hud-drag-opacity',String(frame.preferences.ymcc.opacity));
  seq=frame.seq;position=frame.preferences.ymcc.position;crosshairEnabled=!!frame.preferences.ymcc.crosshair&&frame.preferences.ymcc.crosshair!=='none';syncContainerWidth();
  // Identity/sequence/data admission already succeeded; renew transport only.
  if(!expiryLease.accept(frame.expiresAt)){container.style.visibility='hidden';close();return;}
  if(!frame.show){clearInteractionFeedback();container.style.visibility='hidden';return;}
  if(frame.preferences.ymcc.dragLocked)clearInteractionFeedback();
  container.style.visibility='visible';
  if(!renderer)renderer=mountMonitorHud(container,{preferences:frame.preferences,data:frame.data,mouseMode:frame.mouseMode,viewport:screenViewport,onSize:(width,height)=>{
    if(!seq||disposed)return;
    layoutLane.request({width,height,position});
  },onError:()=>{container.style.visibility='hidden';close();}});
  else {renderer.setMouseMode(frame.mouseMode??null);renderer.update(frame.preferences,frame.data);}
  const appearance=frame.preferences.ymcc;
  // Position changes can leave dimensions unchanged. Compare owned normalized
  // placement/field values directly; avoid JSON/DOM signatures on every frame.
  if(!layoutAppearance||!sameHudLayoutAppearance(layoutAppearance,appearance)){
    layoutAppearance=appearance;layoutLane.invalidate();void nextTick(()=>renderer?.measure());
  }
});
function onVisibility(){if(document.hidden)clearInteractionFeedback();}
function onFeedbackAnimationEnd(event:AnimationEvent){if(event.target!==container)return;if(event.animationName==='monitor-hold')container.classList.remove('radial-holding');if(event.animationName==='monitor-return')container.classList.remove('radial-monitor-return');}
window.addEventListener('blur',clearInteractionFeedback);document.addEventListener('visibilitychange',onVisibility);container.addEventListener('animationend',onFeedbackAnimationEnd);
window.addEventListener('pagehide',dispose,{once:true});
if(trusted)void invoke<HudViewport & {id:number}>('desktopHud.ready',identity,{timeoutMs:5000}).then(viewport=>{
  if(disposed)return;
  if(viewport.id!==hudId||!validHudViewport(viewport))throw Error('Invalid HUD viewport receipt');
  acceptViewport(viewport);
}).catch(close);
else {container.textContent='请从 YMCC 打开监控输出。';container.style.visibility='hidden';}

const offRadialHold=on<{active:boolean;ready?:boolean;duration?:number}>('desktopHud.radialHold',event=>{
 if(!trusted||disposed)return;const active=event.active===true&&container.style.visibility!=='hidden';
 container.classList.toggle('radial-holding',active&&!event.ready);container.classList.toggle('radial-hold-ready',active&&event.ready===true);
 container.style.setProperty('--radial-hold-ms',`${Math.max(400,Math.min(1500,event.duration??650))}ms`);
});
const offRadialTransition=on<{open:boolean;durationMs?:number}>('desktopHud.radialTransition',event=>{
 if(!trusted||disposed)return;
 const css=getComputedStyle(container),prefix=event.open?'--monitor-away-':'--monitor-return-';
 const interrupted=event.open||container.classList.contains('radial-monitor-away');
 container.style.setProperty(prefix+'opacity',interrupted?css.opacity:'0');
 clearInteractionFeedback();container.style.setProperty('--radial-transition-ms',`${Math.max(1,Math.min(500,event.durationMs??240))}ms`);container.classList.toggle('radial-monitor-away',event.open);container.classList.toggle('radial-monitor-return',!event.open);
});
const radialStyles=document.createElement('style');radialStyles.textContent=`
#hud.radial-holding{animation:monitor-hold var(--radial-hold-ms,650ms) linear both}
#hud.radial-hold-ready [data-monitor-hud]{box-shadow:inset 0 0 0 2px var(--hud-accent,#8bd5e8)!important}
#hud.radial-monitor-away{animation:monitor-away var(--radial-transition-ms,240ms) ease-in both}
#hud.radial-monitor-return{animation:monitor-return var(--radial-transition-ms,240ms) ease-out both}
@keyframes monitor-away{from{opacity:var(--monitor-away-opacity,1);transform:none}to{opacity:0;transform:none}}
@keyframes monitor-hold{to{opacity:.82}}
@keyframes monitor-return{from{opacity:var(--monitor-return-opacity,0);transform:none}to{opacity:1;transform:none}}
@media(prefers-reduced-motion:reduce){#hud.radial-holding,#hud.radial-monitor-away,#hud.radial-monitor-return{animation-duration:.001s}}`;
document.head.append(radialStyles);
window.addEventListener('pagehide',()=>{offRadialHold();offRadialTransition();radialStyles.remove();},{once:true});
