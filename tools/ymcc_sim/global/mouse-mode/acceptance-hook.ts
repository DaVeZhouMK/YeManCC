// Acceptance-only hooks expose actual production state and HUD renderer.
import {acceptMouseModeState as acceptMouse,mouseModeState as mouseState} from '@/bridge/mouseModeState';
import {mountMonitorHud as mountMouseHud} from '@/bridge/monitorHudRenderer';
import {normalizeMonitorOutput as mouseHudPreferences} from '@/bridge/monitorHudModel';
let acceptanceHud:ReturnType<typeof mountMouseHud>|null=null;
let acceptancePreferences=mouseHudPreferences({ymccTemplate:'B'});
const acceptanceData={source:'fixture' as const,stale:false,values:{fps:60,cpuPower:18.2,cpuTemp:65},coreUsage:[],coreClocks:[],frameTimes:[]};
(window as any).__mouseProduction={
 accept(value:any){acceptMouse(value);window.dispatchEvent(new CustomEvent('gp:mouse-mode',{detail:value}));acceptanceHud?.setMouseMode(mouseState.value);},
 observeOnly(value:any){acceptMouse(value);},
 state(){return mouseState.value;},
 enabled(value:boolean){acceptancePreferences={...acceptancePreferences,mouseStatusEnabled:value};acceptanceHud?.update(acceptancePreferences,acceptanceData);},
 hud(){const host=document.createElement('div');host.id='mouse-acceptance-hud';Object.assign(host.style,{position:'fixed',inset:'0',background:'#102737',width:innerWidth+'px',height:innerHeight+'px',zIndex:'999999'});document.body.append(host);const p=acceptancePreferences;acceptanceHud=mountMouseHud(host,{preferences:p,viewport:{width:innerWidth,height:innerHeight,workArea:{left:0,top:0,width:innerWidth,height:innerHeight-42}},mouseMode:mouseState.value,data:acceptanceData});},
};
import {createApp as createMouseNotice,h as mouseNoticeNode} from 'vue';
import MouseNotice from '@/components/MonitorShortcutNotice.vue';
(window as any).__mouseProduction.notice=(feedback:any)=>{const n=document.createElement('div');Object.assign(n.style,{position:'fixed',left:'24px',top:'200px',zIndex:'1000000'});document.body.append(n);createMouseNotice({render:()=>mouseNoticeNode(MouseNotice,{feedback,opacity:.9})}).mount(n);};
window.addEventListener('gp:mouse-mode',(event:any)=>acceptMouse(event.detail));
