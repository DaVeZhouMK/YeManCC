// 野蛮系统控制台: one passive mirror. No Python/main.py or own settings.
import { MirrorClient, type Bootstrap, type ClientState, type Choice, type MirrorSnapshot, type MirrorCommand } from './mirrorClient';
import { DECKY_GYRO_CHOICES } from '../../src/bridge/deckyInputOptions';
import {losslessProgramLabel} from '../../src/bridge/losslessProgramState';
import { FRAME_RATE_CEILINGS } from '../../src/bridge/frameRateModel';
import { AnchoredDropdown } from './AnchoredDropdown';
import { FanChoiceDebounce, FAN_CHOICES } from './fanChoiceDebounce';
import {PerformanceIntentQueue} from './performanceIntentQueue';
import {performancePatchAllowed,normalizeSliderValue} from './performanceModel';
import {PerformanceControls} from './PerformanceControls';
import {GameProfileControls} from './GameProfileControls';
import { observeSteamRunning } from './steamRunningContext';
import { installConsoleTab } from './qamPlacement';
import { useQuickAccessVisible, routerHook } from '@decky/api';
import { installSteamHudProbe } from './steamHudProbe';
import { installSteamMonitorHud } from './steamMonitorHud';
import {installSteamWallpaper} from './steamWallpaper';
// Official Decky owns its store, plugin install and catalog/update API.
// YMCC consumes the public plugin API and adds only its own product/update bridge.
import {installLoaderUpdateRow} from './deckyLoaderUpdate';
declare const __YMCC_HUD_PROBE_BUILD__: boolean;
declare const __YMCC_DECKY_BUILD_ID__: string;
declare const __YMCC_DECKY_PANEL_STYLE__: string;
declare const SP_REACT: any;
declare const SP_REACTDOM: any;
declare const DFL: any;
declare global { interface Window { __YMCC_DECKY_MIRROR__?: Bootstrap; __YMCC_DECKY_FRONTEND__?: {buildId:string;active:boolean}; } }
const React = SP_REACT;
const panelStyle=typeof __YMCC_DECKY_PANEL_STYLE__==='string'?__YMCC_DECKY_PANEL_STYLE__:'grouped';
const disposeHudProbe = typeof __YMCC_HUD_PROBE_BUILD__ !== 'undefined' && __YMCC_HUD_PROBE_BUILD__
  ? installSteamHudProbe({host:window,react:React,reactDom:typeof SP_REACTDOM==='undefined'?undefined:SP_REACTDOM,ui:DFL,routerHook}) : null;
const { PanelSection, PanelSectionRow, SliderField, DropdownItem, ToggleField, ButtonItem } = DFL;
if (typeof useQuickAccessVisible !== 'function') throw new Error('YMCC QAM 可见性需要 Decky API 2；未启动镜像连接');
if (!SliderField || !DropdownItem || !ToggleField) throw new Error('YMCC 需要 Steam 原生调节控件；未启动镜像连接');
const client = new MirrorClient({ bootstrap: () => window.__YMCC_DECKY_MIRROR__, socket: url => new WebSocket(url),
  schedule: (action, delay) => setTimeout(action, delay), cancel: handle => clearTimeout(handle), clientId: crypto.randomUUID(),buildId:typeof __YMCC_DECKY_BUILD_ID__==='string'?__YMCC_DECKY_BUILD_ID__:undefined,steam:() => observeSteamRunning(DFL.Router) });
const disposeMonitorHud = typeof __YMCC_HUD_PROBE_BUILD__ !== 'undefined' && __YMCC_HUD_PROBE_BUILD__ ? null
  : installSteamMonitorHud({host:window,react:React,reactDom:typeof SP_REACTDOM==='undefined'?undefined:SP_REACTDOM,ui:DFL,routerHook,client});
const disposeWallpaper=typeof __YMCC_HUD_PROBE_BUILD__ !== 'undefined' && __YMCC_HUD_PROBE_BUILD__?null:installSteamWallpaper({host:window,react:React,reactDom:typeof SP_REACTDOM==='undefined'?undefined:SP_REACTDOM,ui:DFL,routerHook,client});
const disposeLoaderUpdate=typeof __YMCC_HUD_PROBE_BUILD__ !== 'undefined' && __YMCC_HUD_PROBE_BUILD__?null:installLoaderUpdateRow({host:window,ui:DFL,reactDom:typeof SP_REACTDOM==='undefined'?undefined:SP_REACTDOM,routerHook,client});
// Each QAM Content owns its drafts. One page hiding must not cancel another.
type ContentIntents={performance:PerformanceIntentQueue<MirrorSnapshot>;fan:FanChoiceDebounce};
const activeContentIntents=new Set<ContentIntents>();
function createContentIntents():ContentIntents {
const fanChoice = new FanChoiceDebounce({state:()=>client.snapshot(),send:choice=>client.mutate('fan.setChoice',{choice:choice.value},choice.snapshot),schedule:(action,delay)=>setTimeout(action,delay),cancel:handle=>clearTimeout(handle)});
const performanceChoice=new PerformanceIntentQueue<MirrorSnapshot>({confirmOnSnapshot:true,current:()=>{const s=client.snapshot();return s.connected&&!s.busy&&s.snapshot?.ready?s.snapshot:null;},
  sameSource:(a,b)=>a.runId===b.runId&&a.generation===b.generation&&a.game?.identity===b.game?.identity&&a.performance?.sourceRevision===b.performance?.sourceRevision&&a.performance?.side===b.performance?.side&&a.performance?.scope===b.performance?.scope&&a.performance?.mode===b.performance?.mode,
  allowed:(patch,s)=>!!s.performance&&s.actions?.performance===true&&performancePatchAllowed(patch,s.performance),value:(key,s)=>s.performance?.controls.find(c=>c.key===key)?.value,
  send:intent=>client.mutate('performance.setControls',{patch:intent.patch},intent.view),schedule:(action,delay)=>setTimeout(action,delay),cancel:handle=>clearTimeout(handle)});
  return {performance:performanceChoice,fan:fanChoice};
}
const bootstrapListener = () => client.bootstrapChanged();
window.addEventListener('ymcc-decky-bootstrap',bootstrapListener);
const directContent = <Content />;
// Steam centers QAM SVG icons with native dimensions/margins; a text span skips that rule.
// Keep the Y outline centered in its viewBox and leave the tab height to Steam.
const consoleIcon = <svg aria-label="野蛮系统控制台" role="img" viewBox="0 0 24 24" width="1em" height="1em" fill="currentColor" xmlns="http://www.w3.org/2000/svg">
  <path d="M4.5 4H8L12 10.5L16 4H19.5L13.5 13.25V20H10.5V13.25Z" />
</svg>;
const placement = installConsoleTab(DFL, window, directContent, consoleIcon);
const fieldLabels: Record<string, string> = { audioOptimization:'掌机专用低音强化', monitor:'监控总开关', padPersona: '虚拟手柄', gyroEnabled: '陀螺仪', gyroPreset: '陀螺仪预设', speedFactor: '游戏变速', losslessScaling: 'LosslessScaling 插帧' };
const labelStyle = { display: 'flex', justifyContent: 'space-between', alignItems: 'baseline', gap: '8px', width: '100%', fontSize: '12px', lineHeight: '16px' };
const smallLabel = (label: string) => <span className="ymcc-field-label">{label}</span>;
const touchpadLabels:Record<string,string>={layout:'触摸板',singleMode:'触摸板映射',standaloneSpecialMode:'专用按键',specialMask:'专用按键',rearMask:'背部按键'};
function menuField(snapshot:MirrorSnapshot|undefined|null,key:string) {
  if(key.startsWith('touchpad.'))return snapshot?.touchpads?.fields[key.slice(9)];
  if(key==='acCeiling'||key==='dcCeiling'){
    const side=key==='acCeiling'?'ac':'dc',setting=snapshot?.frames?.pair[side];
    return setting?{value:String(setting.ceiling),choices:FRAME_RATE_CEILINGS.map(value=>({data:String(value),label:value===0?'不锁帧':`${value} FPS`})),supported:!!snapshot?.actions?.frames,inherited:!!snapshot?.game&&!snapshot.frames?.dedicated}:undefined;
  }
  return (snapshot?.game?.fields??snapshot?.global?.fields)?.[key];
}
function menuCommand(key:string,snapshot?:MirrorSnapshot|null):MirrorCommand {return key.startsWith('touchpad.')?'touchpad.setField':key==='acCeiling'||key==='dcCeiling'?'frame.setField':snapshot?.game?'game.setField':'global.setField';}
function menuAllowed(snapshot:MirrorSnapshot|null|undefined,command:MirrorCommand) {return command==='touchpad.setField'?snapshot?.actions?.touchpads:command==='frame.setField'?snapshot?.actions?.frames:command==='global.setField'?snapshot?.actions?.global:snapshot?.actions?.game;}
function sameFrameSource(a:MirrorSnapshot|null|undefined,b:MirrorSnapshot|null|undefined){
  return !!a&&!!b&&a.runId===b.runId&&a.generation===b.generation&&a.game?.identity===b.game?.identity&&a.powerSource===b.powerSource&&JSON.stringify(a.frames)===JSON.stringify(b.frames);
}
function sameInputMenuSource(a:MirrorSnapshot,b:MirrorSnapshot,key:string){
  return a.runId===b.runId && a.generation===b.generation && a.game?.identity===b.game?.identity && a.powerSource===b.powerSource
    && menuCommand(key,a)===menuCommand(key,b) && menuAllowed(b,menuCommand(key,b))===true
    && b.virtualController?.locked!==true
    && a.game?.fields.profile?.sourceRevision===b.game?.fields.profile?.sourceRevision
    && ['padPersona','gyroEnabled','gyroPreset'].every(field=>JSON.stringify(menuField(a,field))===JSON.stringify(menuField(b,field)))
    && menuField(b,key)?.supported===true;
}
function Content() {
  const intentsRef=React.useRef(null) as {current:ContentIntents|null};
  if(!intentsRef.current)intentsRef.current=createContentIntents();
  const intents=intentsRef.current;
  const performanceChoice=intents.performance,fanChoice=intents.fan;
  const [state, setState] = React.useState(client.snapshot());
  const [performancePending, setPerformancePending] = React.useState(performanceChoice.snapshot());
  const [frameDraft, setFrameDraft] = React.useState<number|null>(null);
  const frameStart=React.useRef(null) as {current:MirrorSnapshot|null};
  const [fanPending, setFanPending] = React.useState(fanChoice.snapshot());
  const visible = useQuickAccessVisible();
  // Steam can recreate a QAM page without restarting this loaded module.
  // A hidden/unmounted page must lose its callback authority, including a late
  // native slider release that would otherwise end the replacement page's drag.
  const performancePage = React.useRef({visible});
  if(performancePage.current.visible!==visible)performancePage.current={visible};
  const performancePageToken=performancePage.current;
  const ownsPerformanceInteraction=()=>performancePage.current===performancePageToken&&performancePageToken.visible;
  const [toggleEpoch,setToggleEpoch]=React.useState({}) as [Record<string,number>,(update:(value:Record<string,number>)=>Record<string,number>)=>void];
  const rollbackFocus=React.useRef(null) as {current:string|null};
  React.useEffect(()=>{const key=rollbackFocus.current;rollbackFocus.current=null;if(!key||!visible||typeof document==='undefined')return;const label=key==='fan'?'风扇控制':fieldLabels[key];const node=Array.from(document.querySelectorAll('.ymcc-decky-sidebar [role=checkbox]')).find(e=>document.getElementById(e.getAttribute('aria-labelledby')??'')?.innerText===label) as HTMLElement|undefined;node?.focus();},[toggleEpoch,visible]);
  const toggleKey=(field:string)=>`${field}:${state.snapshot?.runId??''}:${field==='fan'?'':state.snapshot?.game?.identity??''}:${toggleEpoch[field]??0}`;
  const [menuActive, setMenuActive] = React.useState(false);
  const menu = React.useRef(null) as { current: null | { snapshot: MirrorSnapshot; field: string; selecting: boolean; timer: ReturnType<typeof setTimeout> | null } };
  const sessionVisible = visible || menuActive;
  const visibilityOwner=React.useRef({});
  React.useEffect(() => client.subscribe((next: ClientState) => {
    performanceChoice.observe(next.connected&&next.snapshot?.ready?next.snapshot:null); fanChoice.observe(next);
    const owned=menu.current, current=next.snapshot;
    if(owned && !owned.selecting){
      const frameMenu=owned.field==='acCeiling'||owned.field==='dcCeiling';
      const inputMenu=owned.field==='padPersona'||owned.field==='gyroPreset';
      // The shared revision also advances for monitor/performance updates. Keep
      // an open frame menu only while its frame bank and source stay unchanged.
      const sameFrames=frameMenu && current?.actions?.frames===true && sameFrameSource(owned.snapshot,current);
      const stableMenu=sameFrames || inputMenu && !!current && sameInputMenuSource(owned.snapshot,current,owned.field);
      if(!next.connected || !current?.ready || current.runId!==owned.snapshot.runId || current.generation!==owned.snapshot.generation || current.game?.identity!==owned.snapshot.game?.identity ||
         (frameMenu||inputMenu ? !stableMenu : current.revision!==owned.snapshot.revision)){
        if(owned.timer)clearTimeout(owned.timer); menu.current=null; setMenuActive(false);
      }else if(stableMenu && current.revision!==owned.snapshot.revision){
        // Selection still passes MirrorClient's strict current-revision check;
        // never submit a stale receipt or extend the original menu timeout.
        owned.snapshot=structuredClone(current);
      }
    }
    setState(next);
  }), []);
  React.useEffect(() => performanceChoice.subscribe(() => setPerformancePending(performanceChoice.snapshot())), []);
  React.useEffect(() => fanChoice.subscribe(() => setFanPending(fanChoice.snapshot())), []);
  React.useEffect(() => { if (!visible) { performanceChoice.cancel(); fanChoice.cancel(); const owned=menu.current;
    if(owned && !owned.selecting){if(owned.timer)clearTimeout(owned.timer);menu.current=null;setMenuActive(false);}
  } }, [visible]);
  React.useEffect(() => { client.setVisible(sessionVisible,visibilityOwner.current); return () => client.setVisible(false,visibilityOwner.current); }, [sessionVisible]);
  React.useEffect(() => { activeContentIntents.add(intents); return () => { performancePage.current={visible:false}; if (menu.current?.timer) clearTimeout(menu.current.timer); menu.current = null; performanceChoice.cancel(); fanChoice.cancel(); activeContentIntents.delete(intents); }; }, []);
  const snapshot = (state as ClientState).snapshot;
  React.useEffect(()=>{if(!visible){frameStart.current=null;setFrameDraft(null);}else if(!sameFrameSource(frameStart.current,snapshot))setFrameDraft(null);},[snapshot?.revision,snapshot?.powerSource,snapshot?.game?.identity,visible]);
  const disabled = !visible || menuActive || !state.connected || !snapshot?.ready || state.busy;
  // Fan intent goes to its own power-aware owner even while shared recovery waits.
  const fanDisabled = !visible || menuActive || !state.connected || state.busy || snapshot?.actions?.fan !== true || snapshot.fan.canToggle !== true;
  const apply = (command: MirrorCommand, args: Record<string, unknown>, control?:string) => {
    if (disabled || !snapshot) return;
    performanceChoice.cancel(); fanChoice.cancel();
    void client.mutate(command, args, snapshot).catch(() => {
      if(!control || client.snapshot().snapshot?.runId!==snapshot.runId)return;
      if(typeof document!=='undefined'){const e=document.activeElement,label=control==='fan'?'风扇控制':fieldLabels[control];if(e?.getAttribute('role')==='checkbox'&&document.getElementById(e.getAttribute('aria-labelledby')??'')?.innerText===label)rollbackFocus.current=control;}
      // Steam ToggleField owns an optimistic internal value. A rejected action may leave the checked prop unchanged; recreate ONLY this control from confirmed state, never resend.
      setToggleEpoch(value=>({...value,[control]:(value[control]??0)+1}));
    });
  };
  const cancelMenu = () => {
    if (menu.current?.selecting) return;
    if (menu.current?.timer) clearTimeout(menu.current.timer);
    menu.current = null; setMenuActive(false);
  };
  const openMenu = (fieldName: string, show: () => void) => {
    const current = client.snapshot(), field = menuField(current.snapshot,fieldName);
    if (!visible || menu.current || current.busy || !current.connected || !current.snapshot?.ready || !menuAllowed(current.snapshot,menuCommand(fieldName,current.snapshot)) || !field?.supported) return false;
    if (['padPersona','gyroPreset'].includes(fieldName)&&current.snapshot.virtualController?.locked)return false;
    if (fieldName === 'gyroPreset' && menuField(current.snapshot,'gyroEnabled')?.value !== 'on') return false;
    performanceChoice.cancel(); fanChoice.cancel();
    menu.current = { snapshot: structuredClone(current.snapshot), field: fieldName, selecting: false, timer: null };
    const owned = menu.current;
    owned.timer = setTimeout(() => { if (menu.current === owned && !owned.selecting) cancelMenu(); }, 45000);
    setMenuActive(true);
    try { show(); } catch (error) { cancelMenu(); throw error; }
    // Steam ToggleMenu calls ShowMenu itself unless this hook explicitly returns false.
    return false;
  };
  const selectMenu = (fieldName: string, option: { data: string }) => {
    const owned = menu.current, field = menuField(owned?.snapshot,fieldName);
    if (!owned || owned.field !== fieldName || owned.selecting || !field || !field.choices.some(choice => choice.data === option?.data && !choice.disabled)) return;
    if (fieldName!=='padPersona' && option.data === field.value && !field.inherited) { cancelMenu(); return; }
    performanceChoice.cancel(); fanChoice.cancel();
    owned.selecting = true;
    if (owned.timer) { clearTimeout(owned.timer); owned.timer = null; }
    void client.mutate(menuCommand(fieldName,owned.snapshot), { field: fieldName.startsWith('touchpad.')?fieldName.slice(9):fieldName, value: option.data }, owned.snapshot).catch(() => {}).finally(() => {
      if (menu.current === owned) { menu.current = null; setMenuActive(false); }
    });
  };
  const losslessData=menuField(snapshot,'losslessScaling');
  const losslessProgram=state.connected&&snapshot?.ready?snapshot.losslessProgram:undefined;
  const side=snapshot?.powerSource;
  const frameSetting=side?snapshot?.frames?.pair[side]:undefined;
  const frameLabel=side==='ac'?'锁帧率 AC':'锁帧率 DC';
  const frameBlocked=disabled||!snapshot?.actions?.frames||!frameSetting||!side;
  const unavailable=(text:string)=><PanelSectionRow><div className="ymcc-decky-unavailable">{text}</div></PanelSectionRow>;
  const heading=(title:string)=><PanelSectionRow><div className="ymcc-section-heading">{title}</div></PanelSectionRow>;
  // Label presentation is keyed by stable enum data, including older hosts whose
  // strings were corrupted during an export. Values and support gates stay live.
  const choiceLabels=(key:string,choices:Choice[])=>choices.map(o=>key==='gyroPreset'?{...o,label:DECKY_GYRO_CHOICES.find(c=>c.data===o.data)?.label??o.label}:key==='speedFactor'&&/^(0\.5|0\.8|1|1\.2|1\.5|2|4|8)$/.test(o.data)?{...o,label:o.data+'×'}:o);
  const renderDropdown=(key:string,label:string)=>{
    const data=menuField(snapshot,key)??(key==='speedFactor'?{value:'1',choices:[{data:'1',label:'1×'}],supported:false}:undefined);if(!data)return <PanelSectionRow key={key}><AnchoredDropdown label={smallLabel(label)} selectedOption="unavailable" rgOptions={[{data:'unavailable',label:'暂无游戏'}]} opened={false} disabled={true} onMenuWillOpen={()=>false} onCancel={()=>{}} onChange={()=>{}}/></PanelSectionRow>;
    const owned=menu.current?.field===key;
    const speedWaiting=key==='speedFactor' && owned && menu.current?.selecting===true;
    return <PanelSectionRow key={key}><AnchoredDropdown label={smallLabel(speedWaiting ? '游戏变速 · 等待确认' : label)} selectedOption={data.value} rgOptions={choiceLabels(key,data.choices)}
      opened={menuActive && owned && !speedWaiting} disabled={speedWaiting || key==='padPersona'&&snapshot?.virtualController?.locked===true || (disabled && !owned)||!data.supported || !menuAllowed(snapshot,menuCommand(key,snapshot)) || key==='gyroPreset'&&menuField(snapshot,'gyroEnabled')?.value!=='on'}
      onMenuWillOpen={(show:()=>void)=>openMenu(key,show)} onCancel={cancelMenu} onChange={(option:{data:string})=>selectMenu(key,option)}/>
      {key==='padPersona'&&snapshot?.virtualController&&<div className="ymcc-controller-status" role="status" title={snapshot.virtualController.detail}>{snapshot.virtualController.text}</div>}
    </PanelSectionRow>;
  };
  return <div className={"ymcc-decky-sidebar ymcc-panel-"+panelStyle}>
    <style>{`.ymcc-decky-sidebar{--ymcc-edge:rgba(171,194,215,.14);--ymcc-muted:#8e9dad;color:#dce5ec;}
.ymcc-field-label{font-size:12px;line-height:1.35;}
.ymcc-controller-status{margin:3px 0 5px;color:var(--ymcc-muted);font-size:11px;line-height:1.4;}
.ymcc-section-heading{display:flex;align-items:center;gap:10px;width:100%;margin:12px 0 5px;color:#91a8bb;font-size:11px;font-weight:600;letter-spacing:.6px;line-height:18px;}
.ymcc-section-heading:after{content:'';height:1px;flex:1;background:var(--ymcc-edge);}
.ymcc-decky-sidebar .ymcc-decky-slider{padding-top:5px!important;padding-bottom:5px!important;}
.ymcc-decky-slider [role=slider] > .SliderControl + div > div > div{width:2px!important;height:6px!important;}
.ymcc-anchored-dropdown{width:100%;min-width:0;}
.ymcc-anchored-dropdown>div:first-child{display:grid!important;grid-template-columns:88px minmax(0,1fr);column-gap:10px;width:100%;min-width:0;box-sizing:border-box;padding-top:5px!important;padding-bottom:5px!important;}
.ymcc-anchored-dropdown [id$="_Label"]{font-size:12px!important;line-height:18px!important;}
.ymcc-anchored-dropdown>div:first-child>div{min-width:0;width:auto!important;padding-left:0!important;padding-right:0!important;}
.ymcc-anchored-dropdown>div:first-child>div:last-child>div,.ymcc-anchored-dropdown>div:first-child>div:last-child>div>div{width:100%!important;min-width:0;box-sizing:border-box;}
.ymcc-decky-sidebar [role=combobox]{width:100%!important;min-width:0;min-height:32px!important;height:auto!important;box-sizing:border-box;margin:0!important;padding:7px 10px!important;border-radius:7px!important;font-size:12px!important;line-height:18px!important;box-shadow:none;}
.ymcc-decky-sidebar [role=combobox]:focus,.ymcc-decky-sidebar [role=combobox].gpfocus{outline:2px solid #93cafa;outline-offset:1px;}
.ymcc-decky-sidebar [role=combobox] span{font-size:12px!important;}
.ymcc-decky-sidebar [role=combobox] svg{width:13px;height:13px;flex-shrink:0;opacity:.7;}
.ymcc-anchored-options{margin:3px 0 7px auto;width:calc(100% - 98px);box-sizing:border-box;max-height:238px;overflow-y:auto;background:#1a2530;border:1px solid #42586b;border-radius:7px;}
.ymcc-anchored-option{box-sizing:border-box;overflow-wrap:anywhere;padding:9px 11px;min-height:32px;font-size:12px;line-height:16px;cursor:pointer;}
.ymcc-anchored-option[aria-selected=true]{color:#86c8ff;background:rgba(45,136,210,.12);}
.ymcc-anchored-option[aria-disabled=true]{opacity:.45;cursor:default;}
.ymcc-anchored-option:focus,.ymcc-anchored-option.gpfocus{background:#34546c;color:#fff;outline:2px solid #93cafa;outline-offset:-2px;}
.ymcc-anchored-cancel{border-top:1px solid var(--ymcc-edge);color:var(--ymcc-muted);}
.ymcc-performance-context{display:flex;align-items:center;justify-content:space-between;gap:8px;font-size:11px;color:var(--ymcc-muted);padding:0 0 6px;line-height:16px;}
.ymcc-performance-status{min-height:16px;white-space:nowrap;text-align:right;color:#9ecbef;font-size:10px;}
.ymcc-performance-label{display:flex;align-items:center;justify-content:space-between;gap:8px;width:100%;font-size:12px;line-height:18px;}
.ymcc-performance-label>span:last-child{font-variant-numeric:tabular-nums;color:#8dcaff;font-weight:600;white-space:nowrap;}
.ymcc-performance-mode{display:flex;gap:4px;width:100%;margin:3px 0 7px;padding:3px;box-sizing:border-box;border-radius:7px;background:rgba(255,255,255,.045);}
.ymcc-performance-mode>[role=button]{flex:1;text-align:center;padding:7px 4px;border-radius:5px;color:#a0aebb;font-size:12px;line-height:18px;}
.ymcc-performance-mode>[aria-pressed=true]{background:#2b4d68;color:#d9efff;}
.ymcc-performance-mode>[role=button]:focus,.ymcc-performance-mode>.gpfocus{outline:2px solid #93cafa;outline-offset:-2px;}
.ymcc-performance-mode>[aria-disabled=true]{opacity:.45;}
.ymcc-bias-directions{display:flex;justify-content:space-between;margin:-2px 10px 4px;font-size:11px;line-height:18px;color:#8e9dad;}
.ymcc-profile-header{margin:2px 0 10px;padding:9px 10px;border:1px solid rgba(141,202,255,.22);border-radius:7px;background:rgba(43,77,104,.16);}
.ymcc-profile-title{display:flex;align-items:center;gap:7px;min-width:0;}.ymcc-profile-title>strong{font-size:12px;color:#8dcaff;white-space:nowrap;}
.ymcc-profile-game{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:12px;}
.ymcc-profile-action{flex:none;padding:5px 7px;border-radius:5px;background:#2b4d68;color:#d9efff;font-size:11px;line-height:17px;white-space:nowrap;cursor:pointer;}
.ymcc-profile-action:focus,.ymcc-profile-action.gpfocus{outline:2px solid #93cafa;outline-offset:1px;}
.ymcc-profile-action[aria-disabled=true]{opacity:.4;cursor:default;}
.ymcc-profile-hint{margin-top:6px;font-size:10px;line-height:16px;color:#8e9dad;}
.ymcc-profile-confirm{margin-top:8px;padding-top:8px;border-top:1px solid rgba(171,194,215,.14);font-size:11px;line-height:18px;}
.ymcc-profile-confirm-actions{display:flex;justify-content:flex-end;gap:8px;margin-top:7px;}
.ymcc-performance-pair{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:10px;align-items:start;}
.ymcc-performance-pair>div{min-width:0;}
.ymcc-performance-pair .ymcc-anchored-dropdown>div:first-child{display:flex!important;flex-direction:column;gap:5px;align-items:stretch;}
.ymcc-performance-pair .ymcc-anchored-options{width:100%;}
.ymcc-performance-pair label{font-size:11px!important;}
.ymcc-fan-labels{display:flex;justify-content:space-between;width:100%;font-size:10px;line-height:16px;gap:4px;color:var(--ymcc-muted);}
.ymcc-fan-labels [data-selected=true]{color:#8dcaff;}
.ymcc-control-gap{height:5px;}
.ymcc-trainer-field{display:grid!important;grid-template-columns:88px minmax(0,1fr);gap:10px;padding-top:5px!important;padding-bottom:5px!important;width:100%;min-width:0;max-width:100%;box-sizing:border-box;}
.ymcc-trainer-field>div{min-width:0;width:auto!important;padding-left:0!important;padding-right:0!important;}
.ymcc-trainer-field>div:last-child>div,.ymcc-trainer-field>div:last-child>div>div{width:100%;min-width:0;max-width:100%;box-sizing:border-box;}
.ymcc-decky-sidebar .ymcc-trainer-button{width:100%!important;min-width:0!important;max-width:100%;min-height:32px;height:auto;box-sizing:border-box!important;margin:0!important;padding:7px 10px!important;border-radius:7px;font-size:12px!important;line-height:18px!important;white-space:normal;overflow-wrap:anywhere;}
.ymcc-decky-unavailable{font-size:11px;color:var(--ymcc-muted);padding:3px 0;line-height:16px;}
.ymcc-decky-sidebar .ymcc-decky-notice,.ymcc-pending-notice{font-size:11px;line-height:16px;overflow-wrap:anywhere;padding:4px 0;color:#9ecbef;}
.ymcc-panel-minimal [role=combobox]{padding:6px 9px!important;background:rgba(255,255,255,.025)!important;border:1px solid #354250!important;border-radius:4px!important;}
.ymcc-panel-minimal [role=combobox]:focus,.ymcc-panel-minimal [role=combobox].gpfocus{background:#24435b!important;color:#eff8ff!important;}
.ymcc-panel-minimal .ymcc-section-heading{font-weight:500;letter-spacing:0;margin-top:9px;color:#8399aa;}
.ymcc-panel-minimal .ymcc-section-heading:after{opacity:.5;}
.ymcc-panel-minimal .ymcc-performance-mode{background:transparent;border:1px solid #354250;padding:2px;border-radius:5px;}
.ymcc-panel-minimal .ymcc-performance-mode>[aria-pressed=true]{background:#263f53;}
.ymcc-panel-minimal .ymcc-performance-label>span:last-child{color:#dce6ee;}
.ymcc-panel-minimal .ymcc-anchored-dropdown>div:first-child{padding-top:3px!important;padding-bottom:3px!important;}
.ymcc-panel-minimal .ymcc-fan-labels [data-selected=true]{color:#dce6ee;}
`}</style>
    <PanelSection>
      <GameProfileControls snapshot={snapshot} disabled={disabled} pending={!!performancePending||!!fanPending||frameDraft!==null} change={(value,owned)=>{if(!disabled&&!performancePending&&!fanPending&&frameDraft===null)void client.mutate('game.setField',{field:'profile',value},owned).catch(()=>{});}}/>
      {heading('性能调度')}
      {snapshot?.performance ? <PerformanceControls begin={()=>{if(ownsPerformanceInteraction())performanceChoice.begin();}} end={()=>{if(ownsPerformanceInteraction())performanceChoice.end();}} view={snapshot.performance} disabled={disabled||!snapshot.actions?.performance} draft={performancePending?.patch??{}} status={performancePending?.phase==='confirming'?'等待状态确认…':performancePending?.phase==='applying'?'正在应用…':performancePending?'松开后 3 秒应用':''} change={(field,value)=>{if(ownsPerformanceInteraction()&&!disabled&&snapshot){fanChoice.cancel();performanceChoice.choose(field,value,snapshot);}}}/> : unavailable('性能调度待读取')}
      
      {heading('画面与散热')}
      {frameSetting && side && <PanelSectionRow><SliderField className="ymcc-decky-slider" label={<span className="ymcc-performance-label"><span>{frameLabel}</span><span>{frameSetting.fps===0?'不锁帧':`${frameDraft??frameSetting.fps} FPS`}</span></span>}
        layout="below" bottomSeparator="none" value={frameDraft??frameSetting.fps} min={frameSetting.fps===0?0:15} max={frameSetting.ceiling||15} step={5}
        validValues="steps" minimumDpadGranularity={5} notchCount={Math.min(5,Math.floor(((frameSetting.ceiling||15)-15)/5)+1)} notchTicksVisible={true} showValue={false} editableValue={false} disabled={frameBlocked||frameSetting.fps===0}
        onChangeStart={()=>{if(!frameBlocked&&snapshot)frameStart.current=structuredClone(snapshot);}}
        onChange={(value:number)=>{const next=normalizeSliderValue(value,15,frameSetting.ceiling,5);if(!frameBlocked&&frameSetting.fps!==0&&next!==null){frameStart.current??=structuredClone(snapshot!);setFrameDraft(next);}}}
        onChangeComplete={(value:number)=>{
          const current=client.snapshot(),expected=frameStart.current;frameStart.current=null;setFrameDraft(null);
          const next=normalizeSliderValue(value,15,frameSetting.ceiling,5),latest=current.snapshot;
          if(frameBlocked||current.busy||!current.connected||!latest?.ready||!latest.actions?.frames||!sameFrameSource(expected,latest)||frameSetting.fps===0||next===null)return;
          if(next===frameSetting.fps&&(!latest.game||latest.frames?.dedicated))return;
          performanceChoice.cancel();fanChoice.cancel();
          void client.mutate('frame.setField',{field:side+'Fps',value:String(next)},latest).catch(()=>{});
        }}/></PanelSectionRow>}
      {frameSetting && side && renderDropdown(side+'Ceiling','锁帧上限')}
      {(!frameSetting||!side)&&unavailable('锁帧状态待读取')}
      <PanelSectionRow><div className="ymcc-control-gap" /></PanelSectionRow>
      <PanelSectionRow><div className="ymcc-fan-control">
        <SliderField className="ymcc-decky-slider" label={smallLabel('风扇控制')} layout="below" bottomSeparator="none"
          value={Math.max(0,FAN_CHOICES.findIndex(o=>o.data===(fanPending?.value??(snapshot?.fan.enabled?snapshot.fan.preset:'off'))))}
          min={0} max={3} step={1} validValues="steps" minimumDpadGranularity={1} notchCount={4} notchTicksVisible={true} showValue={false} editableValue={false}
          disabled={fanDisabled}
          onChangeStart={()=>{if(!fanDisabled)fanChoice.begin();}} onChangeComplete={()=>fanChoice.end()}
          onChange={(raw:number)=>{const value=normalizeSliderValue(raw,0,3,1);if(value!==null&&!fanDisabled&&snapshot&&Number.isSafeInteger(value)&&value>=0&&value<4){performanceChoice.cancel();fanChoice.choose(FAN_CHOICES[value].data,snapshot);}}}/>
        <div className="ymcc-fan-labels">{FAN_CHOICES.map(o=><span key={o.data} data-selected={o.data===(fanPending?.value??(snapshot?.fan.enabled?snapshot.fan.preset:'off'))}>{o.label}</span>)}</div>
        {!fanPending&&snapshot?.fan.pending&&<div role="status" className="ymcc-decky-notice">等待 YMCC 风扇真实信号</div>}
      </div></PanelSectionRow>
      {heading('手柄与陀螺仪')}
      {renderDropdown('padPersona','虚拟手柄')}
      {(() => {const data=menuField(snapshot,'gyroEnabled'),command=menuCommand('gyroEnabled',snapshot),allowed=menuAllowed(snapshot,command);return <PanelSectionRow><ToggleField key={toggleKey('gyroEnabled')} label={smallLabel('陀螺仪')} checked={data?.value==='on'} bottomSeparator="none" disabled={disabled||!allowed||!data?.supported||!['on','off'].includes(data?.value??'')} onChange={(enabled:boolean)=>{if(allowed&&data?.supported&&typeof enabled==='boolean'&&enabled!==(data.value==='on'))apply(command,{field:'gyroEnabled',value:enabled?'on':'off'},'gyroEnabled');}} /></PanelSectionRow>;})()}
      {renderDropdown('gyroPreset','陀螺仪预设')}
      {heading('屏幕触控')}
      {snapshot?.touchpads && Object.entries(touchpadLabels).map(([field,label])=>{
        const layout=snapshot.touchpads!.fields.layout?.value;
        const standalone=snapshot.touchpads!.fields.standaloneSpecialMode;
        if(field==='specialMask'&&standalone||field==='standaloneSpecialMode'&&!standalone)return null;
        if(field==='singleMode'&&layout!=='single')return null;
        return renderDropdown('touchpad.'+field,label);
      })}

      {heading('监控/软件')}
      <PanelSectionRow><ToggleField key={toggleKey('monitor')} label={smallLabel('监控总开关')} checked={snapshot?.monitor?.enabled??false}
        bottomSeparator="none" disabled={disabled||!snapshot?.actions?.monitor||!snapshot.monitor}
        onChange={(enabled:boolean)=>{if(snapshot?.actions?.monitor&&snapshot.monitor&&typeof enabled==='boolean'&&enabled!==snapshot.monitor.enabled)apply('monitor.setEnabled',{enabled},'monitor');}}/></PanelSectionRow>
      <PanelSectionRow><ToggleField key={toggleKey('audioOptimization')} label={smallLabel('掌机专用低音强化')} checked={snapshot?.audioOptimization?.enabled??false}
        bottomSeparator="none" disabled={disabled||!snapshot?.actions?.audioOptimization||!snapshot?.audioOptimization?.known}
        onChange={(enabled:boolean)=>{if(snapshot?.actions?.audioOptimization&&typeof enabled==='boolean')apply('audioOptimization.setEnabled',{enabled},'audioOptimization');}}/></PanelSectionRow>
      <PanelSectionRow><div className="ymcc-control-gap" /></PanelSectionRow>
      {renderDropdown('speedFactor','游戏变速')}
      <PanelSectionRow>{ButtonItem ? <ButtonItem label={<span className="ymcc-field-label" role="status" title="仅表示程序运行状态"><span aria-hidden="true" style={{display:'inline-block',width:7,height:7,borderRadius:'50%',marginRight:6,background:!losslessProgram?.known?'#d6a34c':losslessProgram.running?'#4cc38a':'#c4c4c4'}} />{losslessProgramLabel(losslessProgram)}</span>}
        layout="inline" bottomSeparator="none" childrenContainerWidth="max" highlightOnFocus={false}
        fieldClassName="ymcc-trainer-field" className="ymcc-trainer-button"
        disabled={disabled||!snapshot?.game||!snapshot?.actions?.game||!losslessData?.supported}
        onClick={()=>{if(snapshot?.game&&snapshot?.actions?.game&&losslessData?.supported)apply('game.setField',{field:'losslessScaling',value:'on'},'losslessScaling');}}>
        {state.busy?'处理中…':'一键缩放'}
      </ButtonItem> : <div className="ymcc-decky-unavailable">{losslessProgramLabel(losslessProgram)} · 一键缩放不可用</div>}</PanelSectionRow>
      <PanelSectionRow>{ButtonItem ? <ButtonItem label={smallLabel('修改器')} layout="inline" bottomSeparator="none"
        fieldClassName="ymcc-trainer-field" className="ymcc-trainer-button" childrenContainerWidth="max" highlightOnFocus={false}
        disabled={disabled||!snapshot?.game||!snapshot?.actions?.game||!snapshot?.trainer?.available||snapshot.trainer.pending===true}
        onClick={()=>{if(snapshot?.game&&snapshot?.actions?.game&&snapshot?.trainer?.available&&!snapshot.trainer.pending)apply('game.setField',{field:'trainer',value:'open'});}}>
        {snapshot?.trainer?.pending?'启动中…':'打开修改器'}
      </ButtonItem> : <div className="ymcc-decky-unavailable">修改器</div>}</PanelSectionRow>
      <PanelSectionRow><div role="status" className="ymcc-decky-notice" style={{minHeight:24,boxSizing:'border-box'}}>{!snapshot?.ready&&snapshot?.notice?snapshot.notice:state.notice&&state.notice!==snapshot?.notice?state.notice:'\u00a0'}</div></PanelSectionRow>
      {!!snapshot?.trainer?.notice&&<PanelSectionRow><div role="status" className="ymcc-decky-notice">{snapshot.trainer.notice}</div></PanelSectionRow>}
    </PanelSection>
  </div>;
}
// Steam can reload its shared page before Loader calls onDismount. Remove only our
// own tab/array receipts and connection while the old context is still alive.
let dismounted = false;
function dismount() {
  if (dismounted) return;
  dismounted = true;
  window.removeEventListener('pagehide',dismount);
  if(window.__YMCC_DECKY_FRONTEND__===frontendReceipt)delete window.__YMCC_DECKY_FRONTEND__;
  window.removeEventListener('ymcc-decky-bootstrap',bootstrapListener);
  disposeLoaderUpdate?.(); disposeWallpaper?.(); disposeMonitorHud?.(); disposeHudProbe?.(); for(const intents of activeContentIntents){intents.performance.dispose();intents.fan.dispose();} activeContentIntents.clear(); client.dispose(); placement.dispose();
}
const frontendReceipt=Object.freeze({buildId:__YMCC_DECKY_BUILD_ID__,active:true});
Object.defineProperty(window,'__YMCC_DECKY_FRONTEND__',{value:frontendReceipt,configurable:true});
window.dispatchEvent(new Event('ymcc-decky-frontend-ready'));
window.addEventListener('pagehide',dismount);
export default () => ({ alwaysRender: true, name: '野蛮系统控制台', titleView: <></>, content: placement.active ? null : directContent,
  icon: consoleIcon, onDismount: dismount });
