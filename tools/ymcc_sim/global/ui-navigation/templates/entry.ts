import {getUiVisibilityState,startUiPowerReconciliation} from '@/bridge/uiLifecycle';
import {powerLifecycle} from '@/bridge/api';
import {isNativeRuntime} from '@/bridge/ipc';
(window as any).__uiPowerState=getUiVisibilityState;
import {setRadialTestingPreference} from '@/radial/testing';
import {radialTestingEnabled} from '@/radial/testingState';
(window as any).__radialTesting={set:setRadialTestingPreference,enabled:()=>radialTestingEnabled.value};
import {newControllerShortcutRule} from '@/bridge/controllerShortcutRules';
import '@/gamepad/engine';
import * as logMaintenance from '@/bridge/diagnosticLogMaintenance';
(window as any).__logMaintenance=logMaintenance;
import * as updater from '@/bridge/updateManager';
(window as any).__updater=updater;
import * as predownload from '@/bridge/updatePredownload';
(window as any).__predownload=predownload;
import {emitNativeEvent} from '@/bridge/ipc';
import {loadSettings,compareAndSwapInputSettings,saveSettingsSection} from '@/bridge/settingsRepository';
(window as any).__virtualStartConsumer={loadSettings,compareAndSwapInputSettings,saveSettingsSection};
(window as any).__emitNativeEvent=emitNativeEvent;
import {createApp,h,ref} from 'vue';import {createPinia} from 'pinia';import Shell from './Shell.vue';import {defaultContinuousBank,normalizeContinuousControls} from '@/bridge/performanceControls';import {frameRatePair} from '@/bridge/frameRateModel';import {focusGamepadElement} from '@/gamepad/focus';import {nearestSpatialTarget,spatialNavigationTarget} from '@/gamepad/spatial';import {previewRoute,navigate} from './previewState';import {loadUiSettings,setUiSettings} from '@/bridge/uiSettings';import {applyTheme} from '@/bridge/theme';
const params=new URLSearchParams(location.search),ac=defaultContinuousBank('ac','portable'),dc=defaultContinuousBank('dc');
(window as any).__fixture={navigationShortcuts:params.get('shortcuts')==='paged'?Array.from({length:9},(_,i)=>({...newControllerShortcutRule('navigation-'+i,'keyboard.numeric'),params:{key:'Digit'+(i+1)}})):[],game:{name:'Cyberpunk2077.exe',title:'Cyberpunk 2077',path:'C:/Games/Cyberpunk2077.exe',pid:77,processCreated:'100077'},dedicatedControls:normalizeContinuousControls({ac:structuredClone(ac),dc:structuredClone(dc)}),dedicatedFrames:frameRatePair({ac:120,dc:60}),profileInput:{padPersona:'follow',gyroOverride:'follow',enabled:true},side:'ac',hybrid:params.get('device')!=='uniform',controls:normalizeContinuousControls({ac,dc}),frames:frameRatePair({ac:120,dc:60}),smt:'on',writes:[],listeners:new Set()};
if(params.get('controllerScope')==='global')(window as any).__fixture.profileDeleted=true;
if(params.get('controllerScope')==='dedicated')(window as any).__fixture.profileInput.displayName='崩坏：星穹铁道 · 超长中文游戏专属名称与控制器设置预览';
if(params.has('virtualStartSeed')){(window as any).__fixture.game=null;(window as any).__fixture.profileDeleted=true;}
(window as any).__fixture.currentLogFiles=[...logMaintenance.CURRENT_DIAGNOSTIC_LOG_FILES];
(window as any).__previewErrors=[];async function mountPreview(){if(isNativeRuntime){const initial=powerLifecycle.get().catch(()=>null);const power=startUiPowerReconciliation(()=>powerLifecycle.get(1500),initial);window.addEventListener('pagehide',()=>power.stop(),{once:true});}await loadUiSettings();if(params.get('route')==='/radial')await setRadialTestingPreference(true);const theme=params.get('theme')==='red-black'?'red-black':params.get('theme')==='cyberpunk'?'cyberpunk':'blue-black';await setUiSettings({theme});applyTheme(theme);const app=createApp(Shell);app.use(createPinia());app.config.errorHandler=(err:any,instance,info)=>{(window as any).__previewErrors.push({message:String(err),stack:err?.stack,info});console.error(err);};app.mount('#app');(window as any).__unmount=()=>app.unmount();(window as any).__previewReady=true;}mountPreview().catch(err=>{(window as any).__previewErrors.push({message:String(err)});console.error(err);});function command(action:string){
 const mapped=({left:'nav-left',right:'nav-right',up:'nav-up',down:'nav-down',previous:'page-prev',next:'page-next',dedicated:'edit-game'} as Record<string,string>)[action]||action;
 (window as any).__gamepadAudit.command(mapped);
}
(window as any).__navigationTargets=()=> (window as any).__gamepadAudit.focusables();
(window as any).__navigationGraph=()=>{
 const audit=(window as any).__gamepadAudit,els=audit.focusables(),original=document.activeElement as HTMLElement;
 const nodes=els.map((el:HTMLElement,index:number)=>({index,label:el.getAttribute('aria-label')||el.textContent?.trim()||el.tagName,edges:['nav-left','nav-right','nav-up','nav-down'].map(action=>{el.focus({preventScroll:true});audit.command(action);return els.indexOf(document.activeElement);})}));
 original?.focus({preventScroll:true});return nodes;
};
(window as any).__command=command;
document.addEventListener('keydown',e=>{if(e.key==='Tab')return;if((window as any).__gyroGameAudit?.playing&&!['Escape','F8'].includes(e.key))return;if(e.key.toLowerCase()==='y'){e.preventDefault();command('dedicated');return;}if((document.activeElement as HTMLElement)?.matches('input[type=text],textarea')&&!['Escape','Enter'].includes(e.key))return;const a=({ArrowLeft:'left',ArrowRight:'right',ArrowUp:'up',ArrowDown:'down',Enter:'confirm',' ':'confirm',Escape:'back',F8:'debug',PageUp:'previous',PageDown:'next'} as any)[e.key];if(!a)return;e.preventDefault();e.stopPropagation();command(a);},true);
let last:boolean[]=[],repeat=0,live=true;function poll(now:number){if(!live)return;const pad=navigator.getGamepads?.()[0];if(pad){const b=pad.buttons.map(v=>v.pressed);if(!b[9]&&last[9])command('debug');if(b[4]&&b[5]){last=b;requestAnimationFrame(poll);return;}for(const [key,action] of Object.entries({0:'confirm',1:'back',4:'previous',5:'next',12:'up',13:'down',14:'left',15:'right',3:'dedicated',6:'slider-decrease',7:'slider-increase'})){if(b[Number(key)]&&!last[Number(key)])command(action);}if(now-repeat>190){if(b[6]||b[7]){command(b[7]?'slider-increase':'slider-decrease');repeat=now;}const dx=pad.axes[0]||0,dy=pad.axes[1]||0;if(Math.max(Math.abs(dx),Math.abs(dy))>.55){command(Math.abs(dx)>Math.abs(dy)?dx>0?'right':'left':dy>0?'down':'up');repeat=now;}}last=b;}requestAnimationFrame(poll);}requestAnimationFrame(poll);addEventListener('pagehide',()=>live=false);

let editorWide=false;const workbenchObserver=new MutationObserver(()=>{const next=!!document.querySelector('.editor-overlay,.gyro-simulation-window,.performance-simulation-window,.monitor-editor-window,.radial-editor-window');if(next!==editorWide){editorWide=next;parent.postMessage({type:'preview-workbench',wide:next},location.origin);}});workbenchObserver.observe(document.body,{childList:true,subtree:true});addEventListener('pagehide',()=>workbenchObserver.disconnect());

addEventListener('theme:changed',(e:any)=>parent.postMessage({type:'preview-theme',theme:e.detail.theme},location.origin));



(window as any).__currentAuditRoute=()=>previewRoute.path;

if(params.get('simulation')==='1'){
 const opener=setInterval(()=>{const el=document.querySelector<HTMLButtonElement>('.gyro-simulation-open');if(el){clearInterval(opener);el.click();}},80);
 setTimeout(()=>clearInterval(opener),10000);
}

import RadialWheel from '@/components/RadialWheel.vue';
import {defaultRadialSettings} from '@/radial/model';
(window as any).__mountRadialWheel=()=>{
 const node=document.createElement('div');node.style.cssText='position:fixed;inset:0;background:#0b111a;z-index:2000';document.body.append(node);
 const events:any[]=[];const items=defaultRadialSettings().pages[0].items;
 // Explicit legacy menu fixture, independent of current runtime utility groups.
 items[2]={id:'monitor',label:'监控',action:'page.monitor',enabled:true};items[7]={id:'taskViewDirect',label:'切换程序',action:'windows.taskView',enabled:true};
 items[3]={id:'fan',label:'风扇',action:'page.fan',enabled:true};items[4]={id:'settings',label:'设置',action:'page.settings',enabled:true};
 items[6]={id:'tdp',label:'TDP',action:'tdp',enabled:true};
 const app=createApp(RadialWheel,{items,tdp:{available:true,min:5,max:30,value:15,label:'模拟 · 接电'},onExecute:(id:string,value?:number)=>events.push({id,value}),onClose:()=>events.push({close:true})});
 const api=app.mount(node);(window as any).__radialUsage={api,events,items,unmount:()=>{app.unmount();node.remove();}};
};


// Development-only component fixture. It never changes the product entry policy.
import { radialPerformanceFrame, applyRadialPerformance, type RadialPerformanceIntent } from '@/radial/performance';
import { patchContinuousBank } from '@/bridge/performanceControls';
(window as any).__mountRadialPerformanceWheel=()=>{
 const node=document.createElement('div');node.style.cssText='position:fixed;inset:0;background:#0b111a;z-index:2400;display:grid;place-items:center';document.body.append(node);
 const initial=defaultContinuousBank('ac');initial.tdp=15;initial.tdpCeiling=25;initial.manual.unlimited=false;
 let alive=true,epoch=0,revision=0;
 const capability={ready:true,minPL1:5,maxPL1:30},copy=(value:any)=>JSON.parse(JSON.stringify(value));
 let snapshot:any={identity:{scope:'global',followEffective:true,side:'ac',actualSide:'ac',exe:'',pid:0,processCreated:'',generation:1,revision:'fixture-0'},side:'ac',actualSide:'ac',bank:copy(initial),controls:{version:1,ac:copy(initial),dc:copy(initial)},frameRates:{ac:{mode:'off'},dc:{mode:'off'}},configured:true,hardwareMinimum:3,hardwareMaximum:40,hybrid:true,smtAvailable:true,hyperThreadMode:'default'};
 const items=defaultRadialSettings().pages[0].items;
 const frame=ref(radialPerformanceFrame(snapshot,capability,'fixture-frame-0')),pending=ref(false),status=ref(''),wheel=ref<any>(null),events:any[]=[],writes:any[]=[];
 async function execute(parentId:string,intent:RadialPerformanceIntent){if(pending.value)return;pending.value=true;const ticket=epoch,captured=copy(snapshot),capturedFrame=copy(frame.value);events.push({stage:'submitted',parentId,intent:copy(intent)});
  try{const result=await applyRadialPerformance(captured,capturedFrame,intent,{
   read:async()=>copy(snapshot),capability:async()=>copy(capability),checkpoint:async()=>{if(!alive||ticket!==epoch)throw Error('fixture session changed')},admit:()=>alive&&ticket===epoch,
   update:async(identity,patch,admit)=>{if(!admit())throw Error('fixture scheduler admission refused');writes.push({identity:copy(identity),patch:copy(patch)});const next=copy(snapshot);next.bank=patchContinuousBank(next.bank,patch,'ac');next.controls.ac=copy(next.bank);next.identity.revision='fixture-'+(++revision);next.tdpWriteReceipt={requested:next.bank.tdp,accepted:next.bank.tdp,ok:true,proof:'write-ack'};return next;},
   nextRevision:()=> 'fixture-frame-'+revision,
  });snapshot=copy(result.snapshot);frame.value=result.frame;status.value=result.message;events.push({stage:'result',...copy(result)});
  }catch(error){status.value=(error as Error).message;events.push({stage:'error',message:status.value})}finally{pending.value=false;}
 }
 const app=createApp({setup:()=>()=>h(RadialWheel,{ref:wheel,items,performance:frame.value,tdp:{available:true,min:5,max:25,value:snapshot.bank.tdp,label:'模拟 · 接电'},pending:pending.value,status:status.value,onControl:execute,onExecute:(id:string,value?:number)=>events.push({stage:'legacy',id,value}),onClose:()=>events.push({stage:'closed'})})});
 app.mount(node);
 (window as any).__radialPerformance={get api(){return wheel.value},events,writes,read:()=>({frame:copy(frame.value),snapshot:copy(snapshot),pending:pending.value,status:status.value}),
  changeContext:()=>{epoch++;snapshot.identity.pid++;snapshot.identity.processCreated='changed';snapshot.identity.revision='external';frame.value=radialPerformanceFrame(snapshot,capability,'external-'+epoch)},
  unmount:()=>{alive=false;epoch++;app.unmount();node.remove();}};
};

// Actual fan adapter and component, isolated memory settings/lifecycle only.
import { createRadialFanActions } from '@/radial/fanActions';
import { emptyRadialFan, type RadialFanIntent } from '@/radial/fan';
(window as any).__mountRadialFanWheel=()=>{
 const node=document.createElement('div');node.style.cssText='position:fixed;inset:0;background:#0b111a;z-index:2400;display:grid;place-items:center';document.body.append(node);
 const copy=(value:any)=>JSON.parse(JSON.stringify(value)),items=defaultRadialSettings().pages[0].items;
 // Isolate fan geometry from the other unavailable runtime owners.
 items[2]={id:'monitor',label:'监控',action:'page.monitor',enabled:true};items[7]={id:'taskViewDirect',label:'切换程序',action:'windows.taskView',enabled:true};
 items[3]={id:'fanPage',label:'风扇设置',action:'page.fan',enabled:true};items[4]={id:'settings',label:'设置',action:'page.settings',enabled:true};
 const curves={soft:[{tempC:40,dutyPercent:20},{tempC:90,dutyPercent:65}],balanced:[{tempC:40,dutyPercent:35},{tempC:90,dutyPercent:85}],aggressive:[{tempC:40,dutyPercent:55},{tempC:90,dutyPercent:100}]};
 const s={source:{featureEnabled:true,preset:'balanced',nodes:copy(curves.balanced),presetCurves:copy(curves)},active:false,ready:false,intent:0,cancel:0,hasIntent:false,busy:false,display:null as any,hold:false,release:null as null|(()=>void)};
 let alive=true,epoch=0,revision=0;
 const frame=ref(emptyRadialFan()),pending=ref(false),status=ref(''),wheel=ref<any>(null),events:any[]=[],calls:any[]=[],regions=ref<number[][][]>([]);
 const refresh=async()=>{const value=await adapter.read(()=>{if(!alive)throw Error('fixture ended')});if(alive)frame.value=value;};
 const adapter=createRadialFanActions({owner:{get state(){return s.ready?'ready':'idle'},get controlReady(){return s.ready},get hasControlIntent(){return s.hasIntent},get controlIntentRevision(){return s.intent},get controlCancellationRevision(){return s.cancel},recoveryActive:false,
  stagePendingControlCurve(nodes){s.intent++;calls.push({kind:'stage',nodes:copy(nodes)})},async start(){s.hasIntent=true;calls.push({kind:'start'});if(s.hold)await new Promise<void>(resolve=>s.release=resolve);return {allowed:true,writeReady:true}},
  async apply(nodes){calls.push({kind:'apply',nodes:copy(nodes)});s.active=s.ready=true;return {}},async applyPreset(preset,nodes){calls.push({kind:'preset',preset,nodes:copy(nodes)});s.active=s.ready=true;return {}},async disable(){s.cancel++;s.intent++;s.active=s.ready=s.hasIntent=false;calls.push({kind:'disable'});return {}}},
  enabled:()=>s.active,currentPreset:()=>s.source.preset as any,read:async()=>copy(s.source),curves:(source,preset)=>(source.presetCurves as any)[preset],save:async(preset,source)=>{if(JSON.stringify(s.source)!==JSON.stringify(source))throw Error('source changed');s.source={...s.source,preset,nodes:copy(s.source.presetCurves[preset])};calls.push({kind:'save',preset})},
  updateActive:()=>{},updateDuty:()=>{},power:async()=>({generation:1} as any),featureAllowed:()=>alive,uiBusy:()=>s.busy,currentDisplay:()=>s.display,
  display:value=>{s.display=copy(value);queueMicrotask(()=>{if(alive&&!pending.value)void refresh()})},nextRevision:()=>String(++revision)});
 async function execute(parentId:string,intent:RadialFanIntent){if(pending.value)return;pending.value=true;const ticket=epoch;events.push({parentId,intent:copy(intent)});
  try{const result=await adapter.apply(intent,{generation:1,checkpoint:()=>{if(!alive||ticket!==epoch)throw Error('fixture canceled')}});frame.value=result.frame;status.value=result.notice;}
  catch(error){status.value=(error as Error).message;}finally{pending.value=false;if(alive)await refresh();}}
 const app=createApp({setup:()=>()=>h(RadialWheel,{ref:wheel,items,fan:frame.value,pending:pending.value,status:status.value,onFan:execute,onRegions:(value:number[][][])=>regions.value=value,onClose:()=>events.push({close:true})})});app.mount(node);void refresh();
 (window as any).__radialFan={get api(){return wheel.value},events,calls,read:()=>({frame:copy(frame.value),pending:pending.value,status:status.value,regions:copy(regions.value),items:copy(items)}),holdStart:()=>s.hold=true,releaseStart:()=>{s.hold=false;s.release?.()},
  changeSource:async()=>{s.source.presetCurves.soft[0].dutyPercent++;await refresh()},setBusy:async(value:boolean)=>{s.busy=value;await refresh()},
  unmount:()=>{alive=false;epoch++;adapter.stop();s.release?.();app.unmount();node.remove()}};
};

import RadialOutput from '@/components/RadialOutput.vue';
(window as any).__mountRadialOutput=()=>{
 (window as any).__radialTransition?.unmount();
 const node=document.createElement('div');node.id='radial';node.style.cssText='position:fixed;inset:0;z-index:20000;';document.body.append(node);
 const app=createApp(RadialOutput);app.mount(node);
 (window as any).__radialTransition={frame:()=>emitNativeEvent('radial.frame',{settings:defaultRadialSettings(),tdp:{available:true,min:5,max:30,value:15,label:'Transition fixture'},transitionOrigin:{x:64,y:-36},transition:{enterMs:240,exitMs:200}}),unmount:()=>{app.unmount();node.remove();}};
};

import {mountRadialInputWheel} from './radialInputFixture';
(window as any).__mountRadialInputWheel=mountRadialInputWheel;

import {mountRadialUtilitiesWheel} from './radialUtilitiesFixture';
(window as any).__mountRadialUtilitiesWheel=mountRadialUtilitiesWheel;
