// Dedicated inert renderer: no App, gamepad, acquisition, or settings imports.
import { createApp, h, nextTick, shallowRef } from 'vue';
import { invoke, on, isNativeRuntime } from './bridge/ipc';
import MonitorShortcutNotice from './components/MonitorShortcutNotice.vue';
import { createMonitorNoticeState, noticeAppearance } from './bridge/monitorShortcutNotice';
import { createHudLayoutLane } from './bridge/monitorHudLayoutLane';
import { validHudViewport, type HudViewport } from './bridge/monitorHudViewport';
import { placeNotice, type NoticeRect } from './bridge/monitorNoticePlacement';
import './styles/tokens.css';
import './styles/monitorHudOutput.css';
const args=new URLSearchParams(location.search),hudId=Number(args.get('hudId')),nonce=args.get('nonce')??'';
const trusted=isNativeRuntime&&Number.isSafeInteger(hudId)&&hudId>0&&/^[a-zA-Z0-9-]{16,80}$/.test(nonce);
const root=document.getElementById('notice')!,identity={hudId,nonce};
let disposed=false,seq=0,viewport:HudViewport|undefined,occupied:NoticeRect[]=[],raf=0;
const feedback=shallowRef<import('./bridge/shortcutActionFeedback').ShortcutActionFeedback|null>(null),appearance=shallowRef(noticeAppearance(null));
const state=createMonitorNoticeState({now:Date.now,setTimer:(fn,ms)=>setTimeout(fn,ms),clearTimer:id=>clearTimeout(id as ReturnType<typeof setTimeout>),changed:value=>{feedback.value=value;if(!value)root.style.visibility='hidden';}});
const close=()=>{if(trusted)void invoke('desktopHud.close',identity,{timeoutMs:2000}).catch(()=>{});};
const lane=createHudLayoutLane({sequence:()=>seq,send:layout=>invoke<boolean>('desktopHud.layout',{...identity,...layout},{timeoutMs:2000}),failure:()=>{state.clear();close();}});
const app=createApp({render:()=>feedback.value?h(MonitorShortcutNotice,{feedback:feedback.value,opacity:appearance.value.opacity}):null});app.mount(root);
function schedule(){if(disposed||raf)return;raf=requestAnimationFrame(()=>{raf=0;state.check();if(!viewport||!feedback.value)return;const node=root.firstElementChild as HTMLElement|null;if(!node)return;const width=node.offsetWidth,height=node.offsetHeight,a=viewport.workArea??{left:0,top:0,width:viewport.width,height:viewport.height};const p=placeNotice({width:a.width,height:a.height},{width,height},occupied.map(r=>({...r,x:r.x-a.left,y:r.y-a.top})),{x:(a.width-width)/2,y:a.height*.8-height/2});root.style.visibility=p?'visible':'hidden';if(p){root.style.left=a.left+p.x+'px';root.style.top=a.top+p.y+'px';}lane.request({width,height,position:1});});}
function acceptViewport(raw:unknown){if(!validHudViewport(raw))return;viewport=raw;lane.invalidate();schedule();}
const offViewport=on<HudViewport&{id:number}>('desktopHud.viewport',value=>{if(value.id===hudId)acceptViewport(value);});
const offHide=on('monitorNotice.hide',()=>state.clear());
interface NoticeFrame {type:string;protocol:number;hudId:number;nonce:string;seq:number;expiresAt:number;feedback:import('./bridge/shortcutActionFeedback').ShortcutActionFeedback;appearance:unknown;occupied?:NoticeRect[];}
const offFrame=on<NoticeFrame>('monitorNotice.frame',raw=>{
 if(!trusted||disposed||raw?.type!=='monitor-notice'||raw.protocol!==1||raw.hudId!==hudId||raw.nonce!==nonce||!Number.isSafeInteger(raw.seq)||raw.seq<=seq||raw.expiresAt!==raw.feedback?.expiresAtUnixMs)return;
 const nextAppearance=noticeAppearance(raw.appearance);appearance.value=nextAppearance;document.documentElement.dataset.theme=nextAppearance.theme;
 occupied=Array.isArray(raw.occupied)?raw.occupied.slice(0,8).filter((r:NoticeRect)=>r&&[r.x,r.y,r.width,r.height].every(Number.isFinite)&&r.width>0&&r.height>0):[];
 if(state.accept(raw.feedback)){seq=raw.seq;lane.invalidate();void nextTick(schedule);}else if(state.inspect().current?.id===raw.feedback?.id){seq=raw.seq;lane.invalidate();void nextTick(schedule);}
});
const observer=new ResizeObserver(schedule);observer.observe(root);
const visibility=()=>{state.check();schedule();};document.addEventListener('visibilitychange',visibility);
function dispose(){if(disposed)return;disposed=true;if(raf)cancelAnimationFrame(raf);raf=0;offFrame();offViewport();offHide();observer.disconnect();document.removeEventListener('visibilitychange',visibility);lane.dispose();state.dispose();app.unmount();}
window.addEventListener('pagehide',dispose,{once:true});
root.style.position='absolute';root.style.visibility='hidden';root.style.pointerEvents='none';
if(trusted)void invoke<HudViewport&{id:number}>('desktopHud.ready',identity,{timeoutMs:5000}).then(value=>{if(value.id!==hudId)throw Error('Invalid notice viewport');acceptViewport(value);}).catch(close);
