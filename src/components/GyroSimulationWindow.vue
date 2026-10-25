<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { windowApi } from '@/bridge/api';
import { isNativeRuntime, on as onIpc, getLastGamepadState } from '@/bridge/ipc';
import { isUiVisible, onUiVisibilityChange } from '@/bridge/uiLifecycle';
import { focusGamepadElement } from '@/gamepad/focus';
import { createMotorcycle, createRange, updateMotorcycle, updateRange, triggerSnapshot, clamp, type SimulationScene } from '@/gyroSimulation/model';
import { renderRange } from '@/gyroSimulation/renderer';
import {createGameHaptics,type HapticPad} from '@/gyroSimulation/haptics';
import { loadRange3D, type Range3DRenderer } from '@/gyroSimulation/range3d';
import MotorcycleHandlebar from './MotorcycleHandlebar.vue';
import {renderMotorcycle} from '@/gyroSimulation/motorcycleRenderer';
const props=defineProps<{motion:{x:number;y:number};connected:boolean;enabled:boolean;admitted:boolean}>();
const emit=defineEmits<{(e:'close'):void;(e:'settings-host',element:HTMLElement|null):void}>();
const scene=ref<SimulationScene>('fps'),demo=ref(false),playing=ref(false),steering=ref(0),inputActive=ref(false);
const modelStatus=ref<'loading'|'ready'|'fallback'>('loading');let range3d:Range3DRenderer|null=null;
const modal=ref<HTMLElement|null>(null),canvas=ref<HTMLCanvasElement|null>(null),settings=ref<HTMLElement|null>(null);
const closeButton=ref<HTMLButtonElement|null>(null),playButton=ref<HTMLButtonElement|null>(null);
const hud=ref({speed:270,shots:0,hits:0,ammo:30,reload:false,ads:false,lt:false,rt:false,health:100,score:0,knockouts:0,haptic:'idle'});
const signal=computed(()=>demo.value?'演示动作 · 仅预览':!playing.value?'按 Start 开始测试 · RT / LT 操作':!inputActive.value?'等待测试窗口接管输入':!props.connected?'可用鼠标或键盘体验':!props.enabled?'陀螺仪已关闭':props.admitted?'使用实际陀螺仪输出':'等待触发或有效输出');
const opener=document.activeElement as HTMLElement|null;
const widthOwner=`gyro-simulation-${Date.now()}-${Math.random().toString(36).slice(2)}`;
let alive=true,widthOwned=false,frame=0,last=0,lastHud=0,range=createRange(),motorcycle=createMotorcycle();
let nativePad=triggerSnapshot(null),stopPad:(()=>void)|undefined,stopVisibility:(()=>void)|undefined,triggerArmed=true,restartHeld=false;
let pointerLT=false,pointerRT=false,dragging=false,mouseX=0,mouseY=0;
let feedbackPad:HapticPad|null=null,feedbackRunning=false;const haptics=createGameHaptics(()=>feedbackPad);
const keys=new Set<string>();
const sceneText=computed(()=>scene.value==='fps'?{eyebrow:'M4A1 · 800 RPM · LPVO 2×',title:'室内战术靶场',description:'移动吊靶 · 计分倒靶 · 自动装填',left:'按住开镜',right:'按住连射',help:'左摇杆移动 · 右摇杆视角 · 陀螺仪瞄准',keyboard:'WASD 移动 · 拖动瞄准'}:{eyebrow:'ROAD COMBAT · MOTO',title:'暴力摩托',description:'公路追逐 · 贴身挥击 · AI 对手',left:'刹车',right:'按住油门',help:'左摇杆 / 陀螺仪倾斜 · X 挥击',keyboard:'A / D 转向 · F 挥击 · R 重开'});
watch(settings,element=>emit('settings-host',element),{flush:'post'});
watch(scene,()=>{stopPlay(false);range=createRange();motorcycle=createMotorcycle();steering.value=0;demo.value=false;lastHud=0;});
function topLayer():boolean {
 const layers=[...document.querySelectorAll<HTMLElement>('[data-gp-modal]')].filter(el=>{const r=el.getBoundingClientRect();return r.width>0&&r.height>0&&getComputedStyle(el).visibility!=='hidden';});
 return layers[layers.length-1]===modal.value;
}
function ownsBack():boolean {return topLayer()&&!modal.value?.querySelector('[aria-expanded="true"][aria-haspopup="listbox"]');}
function clearInput(){pointerLT=false;pointerRT=false;dragging=false;mouseX=0;mouseY=0;keys.clear();}
function stopPlay(restore=true){playing.value=false;inputActive.value=false;haptics.stop();feedbackRunning=false;clearInput();if(restore)void nextTick(()=>focusGamepadElement(playButton.value));}
function enterPlay(){if(!ownsBack())return;if(scene.value==='motorcycle'&&motorcycle.health<=0)restart();demo.value=false;clearInput();triggerArmed=!nativePad.lt&&!nativePad.rt;playing.value=true;void nextTick(()=>focusGamepadElement(canvas.value));}
function restart(){haptics.stop();feedbackRunning=false;range=createRange();motorcycle=createMotorcycle();steering.value=0;lastHud=0;}
function toggleDemo(){stopPlay(false);demo.value=!demo.value;}
function focusPreviewEntry(){stopPlay(false);void nextTick(()=>focusGamepadElement(modal.value?.querySelector<HTMLElement>('.gyro-scene-tabs button.active')||null));}
function back(event:Event){if(event.defaultPrevented||!ownsBack())return;event.preventDefault();if(playing.value)stopPlay();else emit('close');}
function pageCommand(event:Event){
 if(event.defaultPrevented)return;
 const action=(event as CustomEvent<{action?:string}>).detail?.action;
 // Start belongs to this workbench, including while the editor has focus.
 // An open dropdown or confirmation keeps ownership until it is dismissed.
 if(action==='debug'){event.preventDefault();if(ownsBack()&&!playing.value)enterPlay();return;}
 if(!ownsBack())return;
 if(!playing.value){
  if(action==='confirm'&&(document.activeElement as HTMLElement)?.closest('.gyro-play-button,.gyro-game-pedal'))event.preventDefault();
  return;
 }
 if(action==='back'){event.preventDefault();stopPlay();}
 else if(action&&['nav-left','nav-right','nav-up','nav-down','confirm','slider-decrease','slider-increase','page-prev','page-next'].includes(action))event.preventDefault();
}
function keydown(event:KeyboardEvent){
 if(event.defaultPrevented)return;
 if(event.key==='Escape'&&ownsBack()){event.preventDefault();if(playing.value)stopPlay();else emit('close');return;}
 if(!playing.value||!ownsBack()||event.target instanceof HTMLInputElement||event.target instanceof HTMLTextAreaElement)return;
 if(['Space','ShiftLeft','ShiftRight','KeyW','KeyA','KeyS','KeyD','KeyI','KeyJ','KeyK','KeyL','KeyF','KeyR'].includes(event.code)){event.preventDefault();if(event.code==='KeyR'){if(!event.repeat)restart();}else keys.add(event.code);}
}
function keyup(event:KeyboardEvent){keys.delete(event.code);}
function neutralize(){stopPlay(false);nativePad=triggerSnapshot(null);demo.value=false;last=0;}
function visibility(){if(document.hidden)neutralize();}
// Native semantic actions already passed the window-owner gate. WebView2 can
// report a transient DOM blur while that owner remains active in full-screen
// shells; only the native blur/visibility boundary ends a native test session.
function browserBlur(){if(isNativeRuntime)clearInput();else neutralize();}
function acceptPad(raw:unknown){nativePad=triggerSnapshot(raw);if(!nativePad.connected){triggerArmed=true;if(isNativeRuntime){haptics.stop();feedbackRunning=false;}}}
function hold(event:PointerEvent,side:'lt'|'rt'){
 if(!ownsBack())return;event.preventDefault();
 if(!playing.value)enterPlay();
 if(side==='lt')pointerLT=true;else pointerRT=true;
 (event.currentTarget as HTMLElement).setPointerCapture?.(event.pointerId);
}
function togglePedal(event:MouseEvent,side:'lt'|'rt'){if(event.detail!==0)return;if(!playing.value)enterPlay();if(side==='lt')pointerLT=!pointerLT;else pointerRT=!pointerRT;}
function release(event:PointerEvent,side:'lt'|'rt'){if(side==='lt')pointerLT=false;else pointerRT=false;const el=event.currentTarget as HTMLElement;if(el.hasPointerCapture?.(event.pointerId))el.releasePointerCapture(event.pointerId);}
function startDrag(event:PointerEvent){if(event.button!==0)return;if(!playing.value)enterPlay();dragging=true;(event.currentTarget as HTMLElement).setPointerCapture(event.pointerId);}
function movePointer(event:PointerEvent){
 if(!playing.value||!dragging)return;
 if(scene.value==='fps'){range.yaw=Math.atan2(Math.sin(range.yaw+event.movementX*.0022),Math.cos(range.yaw+event.movementX*.0022));range.pitch=clamp(range.pitch+event.movementY*.0018,-.3,.3);}
 else {const box=canvas.value!.getBoundingClientRect();mouseX=clamp((event.clientX-box.left-box.width/2)/(box.width*.35),-1,1);}
}
function stopDrag(){dragging=false;mouseX=0;mouseY=0;}
function focusChanged(event:FocusEvent){if(playing.value&&event.target!==canvas.value&&!(event.target as HTMLElement)?.closest('.gyro-game-pedal,.gyro-play-button'))stopPlay(false);}
function pointerChanged(event:PointerEvent){if(playing.value&&(event.target as HTMLElement)?.closest('.gyro-simulation-editor,.gyro-scene-tabs,.gyro-simulation-heading'))stopPlay(false);}
function draw(now:number){
 if(!alive)return;frame=requestAnimationFrame(draw);if(document.hidden){last=0;return;}
 const dt=last?clamp((now-last)/1000,0,.05):0;last=now;
 const el=canvas.value,ctx=el?.getContext('2d');if(!el||!ctx||!el.clientWidth||!el.clientHeight)return;
 const w=el.clientWidth,h=el.clientHeight,dpr=Math.min(devicePixelRatio||1,2);
 if(el.width!==Math.round(w*dpr)||el.height!==Math.round(h*dpr)){el.width=Math.round(w*dpr);el.height=Math.round(h*dpr);}
 ctx.setTransform(dpr,0,0,dpr,0,0);
 let lt=nativePad.lt,rt=nativePad.rt,axes=[...nativePad.axes],restartPressed=nativePad.restart;
 // The browser fallback only exists in H5, never alongside the native input owner.
 if(!isNativeRuntime){const pad=Array.from(navigator.getGamepads?.()||[]).find(p=>p?.connected&&p.mapping==='standard');feedbackPad=(pad as unknown as HapticPad)||null;if(pad){lt=Math.max(lt,pad.buttons[6]?.value||0);rt=Math.max(rt,pad.buttons[7]?.value||0);axes=[pad.axes[0]||0,-(pad.axes[1]||0),pad.axes[2]||0,-(pad.axes[3]||0)];restartPressed=!!pad.buttons[2]?.pressed;}}
 if(!triggerArmed){if(!lt&&!rt)triggerArmed=true;else{lt=0;rt=0;}}
 const active=playing.value&&ownsBack()&&isUiVisible()&&(isNativeRuntime||document.hasFocus());
 inputActive.value=active;
 const feedbackAllowed=active&&(!isNativeRuntime||nativePad.connected);if(feedbackAllowed&&!feedbackRunning){feedbackRunning=true;void haptics.start();}else if(!feedbackAllowed&&feedbackRunning){haptics.stop();feedbackRunning=false;}
 const gyro=active&&props.connected&&props.enabled&&props.admitted;
 const stick=(value:number)=>Math.abs(value)<.12?0:Math.sign(value)*(Math.abs(value)-.12)/.88;
 if(active&&restartPressed&&!restartHeld&&scene.value==='fps')restart();restartHeld=restartPressed;
 const rightStick=scene.value==='fps';
 const input={active,demo:demo.value&&topLayer(),
  x:(gyro?clamp(props.motion.x,-1,1):0)+mouseX+stick(axes[rightStick?2:0])+(keys.has(rightStick?'KeyL':'KeyD') ? .55 : 0)-(keys.has(rightStick?'KeyJ':'KeyA') ? .55 : 0),
  y:(gyro?-clamp(props.motion.y,-1,1):0)-stick(axes[3])+mouseY+(keys.has('KeyK') ? .4 : 0)-(keys.has('KeyI') ? .4 : 0),
  moveX:stick(axes[0])+Number(keys.has('KeyD'))-Number(keys.has('KeyA')),
  moveY:stick(axes[1])+Number(keys.has('KeyW'))-Number(keys.has('KeyS')),
  throttle:active?Math.max(rt,Number(pointerRT||keys.has('Space'))):0,
  attack:active&&(restartPressed||keys.has('KeyF')),
  brake:active?Math.max(lt,Number(pointerLT||keys.has('ShiftLeft')||keys.has('ShiftRight'))):0};
 if(scene.value==='fps'){
  const shots=range.shots,hits=range.hits,reload=range.reload;updateRange(range,dt,input);renderRange(ctx,w,h,range,false,range3d??undefined);
  if(active){if(range.hits>hits)haptics.pulse('hit');else if(range.shots>shots)haptics.pulse('shot');else if(reload>0&&range.reload===0)haptics.pulse('reload');}
 }else if(scene.value==='motorcycle'){
  const collisions=motorcycle.collisions,hits=motorcycle.hits;updateMotorcycle(motorcycle,dt,input);steering.value=motorcycle.steer;renderMotorcycle(ctx,w,h,motorcycle);
  if(active){if(motorcycle.collisions>collisions)haptics.pulse('collision');else if(motorcycle.hits>hits)haptics.pulse('hit');if(motorcycle.health<=0)stopPlay();}
 }
 if(now-lastHud>85){lastHud=now;hud.value={speed:Math.round(motorcycle.speed),shots:range.shots,hits:range.hits,ammo:range.ammo,reload:range.reload>0,ads:range.ads>.55,
  lt:active&&input.brake>.2,rt:active&&input.throttle>.2,health:Math.round(motorcycle.health),score:motorcycle.score,knockouts:motorcycle.knockouts,haptic:haptics.status};}
}
async function widen(){if(!isNativeRuntime||/[?&]__ymccChild=\d+/.test(location.hash))return;try{const result=await windowApi.controllerEditorWidth(widthOwner,true,2);if(!alive){if(result.ok)await windowApi.controllerEditorWidth(widthOwner,false);return;}widthOwned=result.ok;}catch{/* Keep the local overlay usable. */}}
onMounted(async()=>{
 void loadRange3D().then(renderer=>{if(!alive){renderer.dispose();return;}range3d=renderer;modelStatus.value='ready';}).catch(()=>{if(alive)modelStatus.value='fallback';});
 void widen();acceptPad(getLastGamepadState());stopPad=onIpc('gamepad.state',acceptPad);
 stopVisibility=onUiVisibilityChange(({visible})=>{if(!visible)neutralize();});
 window.addEventListener('ipc:window.blur',neutralize);
 window.addEventListener('ipc:gamepad-back',back);window.addEventListener('gp:page-command',pageCommand);
 window.addEventListener('keydown',keydown);window.addEventListener('keyup',keyup);window.addEventListener('blur',browserBlur);document.addEventListener('visibilitychange',visibility);
 frame=requestAnimationFrame(draw);await nextTick();focusGamepadElement(closeButton.value);
});
onBeforeUnmount(()=>{
 alive=false;haptics.dispose();range3d?.dispose();range3d=null;clearInput();stopPad?.();stopVisibility?.();cancelAnimationFrame(frame);emit('settings-host',null);
 window.removeEventListener('ipc:window.blur',neutralize);window.removeEventListener('ipc:gamepad-back',back);window.removeEventListener('gp:page-command',pageCommand);window.removeEventListener('keydown',keydown);window.removeEventListener('keyup',keyup);window.removeEventListener('blur',browserBlur);document.removeEventListener('visibilitychange',visibility);
 if(widthOwned)void windowApi.controllerEditorWidth(widthOwner,false).catch(()=>undefined);if(opener?.isConnected)opener.focus({preventScroll:true});
});
</script>
<template>
 <Teleport to="body"><section ref="modal" class="gyro-simulation-window app-root" role="dialog" aria-modal="true" aria-label="模拟陀螺仪" data-gp-modal data-gp-page-lock data-gp-scope="gyro-simulation" data-gp-spatial="nearest" @focusin="focusChanged" @pointerdown.capture="pointerChanged">
  <header class="gyro-simulation-heading"><div><h1>模拟陀螺仪</h1><button class="gyro-preview-entry" data-gp-pane="gyro-preview" @click="focusPreviewEntry">左侧试玩</button><span>右侧实时调节</span></div><button ref="closeButton" data-gp-pane="gyro-settings" @click="emit('close')"><b>B</b> 返回</button></header>
  <div class="gyro-simulation-body">
   <section class="gyro-simulation-preview" data-gp-pane="gyro-preview" aria-label="陀螺仪模拟窗口">
    <div class="gyro-scene-tabs" role="group" aria-label="模拟场景"><button :class="{active:scene==='fps'}" :aria-pressed="scene==='fps'" @click="scene='fps'">FPS 射击</button><button :class="{active:scene==='motorcycle'}" :aria-pressed="scene==='motorcycle'" @click="scene='motorcycle'">暴力摩托</button><button :class="{active:demo}" :aria-pressed="demo" @click="toggleDemo">{{demo?'停止演示':'演示动作'}}</button></div>
    <div class="gyro-scene-viewport" :class="{playing}" :data-scene="scene" :data-playing="playing">
     <span v-if="scene==='fps'&&modelStatus!=='ready'" class="gyro-model-loading" role="status">{{modelStatus==='loading'?'载入 3D 武器…':'3D 不可用 · 当前为简化预览'}}</span>
     <canvas ref="canvas" :tabindex="playing?0:-1" :aria-label="sceneText.title+'，按 Start 开始测试'" @click="!playing&&enterPlay()" @pointerdown="startDrag" @pointermove="movePointer" @pointerup="stopDrag" @pointercancel="stopDrag"/>
     <div class="gyro-scene-caption"><span class="gyro-scene-eyebrow">{{sceneText.eyebrow}}</span><strong>{{sceneText.title}}</strong><span>{{sceneText.description}}</span></div>
     <div class="gyro-game-readout" aria-live="off">
      <template v-if="scene==='fps'"><div><b>{{hud.hits*100}}</b><span>{{hud.hits}} 命中 / {{hud.shots}} 发</span></div><div><b>{{hud.reload?'…':String(hud.ammo).padStart(2,'0')}}</b><span>{{hud.reload?'装填中':'/ 30 弹药'}}</span></div></template>
      <template v-else-if="scene==='motorcycle'"><div><b>{{String(hud.speed).padStart(3,'0')}}</b><span>km/h</span></div><div><b>{{hud.score}}</b><span>积分 · 击倒 {{hud.knockouts}}</span></div><div><b>{{hud.health}}</b><span>耐久</span></div></template>
     </div>
     <MotorcycleHandlebar v-if="scene==='motorcycle'" :steering="steering"/>
     <span v-if="demo" class="gyro-demo-label">演示动作 · 仅预览</span>
     <div class="gyro-game-controls">
      <div class="gyro-game-control-row">
       <button class="gyro-game-pedal" :class="{held:hud.lt}" :aria-pressed="hud.lt" :aria-label="'LT '+sceneText.left" @click="togglePedal($event,'lt')" @pointerdown="hold($event,'lt')" @pointerup="release($event,'lt')" @pointercancel="release($event,'lt')" @lostpointercapture="pointerLT=false" @contextmenu.prevent><kbd>LT</kbd><span>{{sceneText.left}}</span></button>
       <button ref="playButton" class="gyro-play-button" :class="{active:playing}" data-gyro-enter-play @click="playing?stopPlay():enterPlay()"><kbd>{{playing?'B':'Start'}}</kbd><span>{{playing?'返回调节':'进入试玩'}}</span></button>
       <button class="gyro-game-pedal" :class="{held:hud.rt}" :aria-pressed="hud.rt" :aria-label="'RT '+sceneText.right" @click="togglePedal($event,'rt')" @pointerdown="hold($event,'rt')" @pointerup="release($event,'rt')" @pointercancel="release($event,'rt')" @lostpointercapture="pointerRT=false" @contextmenu.prevent><kbd>RT</kbd><span>{{sceneText.right}}</span></button>
      </div>
      <div class="gyro-game-help"><span>{{sceneText.help}}</span><span><kbd>Start</kbd> 开始测试 · <kbd>X</kbd> {{scene==='motorcycle'?'挥击':'重开'}} · <kbd>B</kbd> 返回调节</span></div>
      <div class="gyro-keyboard-help">{{sceneText.keyboard}} · 空格 RT · Shift LT · R 重开</div>
     </div>
    </div>
    <div class="gyro-simulation-signal" role="status"><span class="gyro-haptics-status">{{hud.haptic==='ready'?'震动可用':hud.haptic==='unavailable'?'震动待连接':'事件震动'}}</span><i :class="{active:inputActive&&connected&&enabled&&admitted}"/><span>{{signal}}</span><span v-if="inputActive&&connected&&enabled&&admitted">X {{motion.x.toFixed(2)}} · Y {{motion.y.toFixed(2)}}</span></div>
   </section>
   <aside class="gyro-simulation-editor" data-gp-pane="gyro-settings" aria-label="陀螺仪实时设置"><div class="gyro-editor-label">陀螺仪设置 <span>与原页面同步</span></div><div ref="settings" class="integrated-page-body gyro-simulation-settings"/></aside>
  </div>
 </section></Teleport>
</template>
<style scoped>
.gyro-haptics-status{margin-left:auto;order:3;font-size:11px;white-space:nowrap;color:var(--text-muted)}
.gyro-simulation-window{position:fixed;inset:0;z-index:1250;display:flex;align-items:stretch;justify-content:flex-start;flex-direction:column;box-sizing:border-box;padding:12px;gap:12px;background:var(--theme-gradient,var(--bg-solid,#101722));color:var(--text);overflow:hidden;--gyro-editor-width:clamp(390px,54vh,500px)}
.gyro-simulation-heading{display:flex;align-items:center;justify-content:space-between;gap:12px;flex:0 0 auto;padding:1px 3px}.gyro-simulation-heading>div{display:flex;align-items:center;gap:14px;min-width:0}.gyro-simulation-heading h1{margin:0;font-size:19px;white-space:nowrap}.gyro-simulation-heading span{font-size:12px;color:var(--text-dim)}.gyro-simulation-heading button{min-height:36px;padding:5px 15px;border:1px solid var(--console-line);border-radius:8px;background:var(--console-surface);color:var(--text);font-size:14px;cursor:pointer}.gyro-simulation-heading b{margin-right:6px;color:var(--accent)}
.gyro-simulation-body{width:100%;min-width:0;box-sizing:border-box;display:grid;grid-template-columns:minmax(0,1fr) var(--gyro-editor-width);gap:12px;flex:1;min-height:0}.gyro-simulation-preview{display:flex;flex-direction:column;gap:10px;min-width:0;min-height:0}.gyro-scene-tabs{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px;padding:6px;border:1px solid var(--console-line);border-radius:10px;background:var(--console-surface)}.gyro-scene-tabs button{min-height:38px;border:1px solid transparent;border-radius:7px;background:var(--bg-input);font:inherit;font-size:14px;color:var(--text);cursor:pointer;white-space:nowrap}.gyro-scene-tabs button.active{background:var(--accent-surface);border-color:var(--accent-border);color:var(--accent)}
.gyro-model-loading{position:absolute;z-index:3;left:12px;bottom:126px;padding:6px 10px;border-radius:8px;background:#152026d9;color:#e4e7e9;font-size:12px;pointer-events:none}.gyro-scene-viewport{position:relative;flex:1;min-height:240px;border:1px solid var(--console-line);border-radius:12px;overflow:hidden;background:#15282d;isolation:isolate}.gyro-scene-viewport.playing{border-color:color-mix(in srgb,var(--accent) 65%,transparent)}.gyro-scene-viewport canvas{position:absolute;inset:0;width:100%;height:100%;display:block;touch-action:none;cursor:crosshair;outline-offset:-3px}.gyro-scene-viewport canvas.focused{z-index:0!important}.gyro-scene-caption{z-index:2;position:absolute;left:18px;top:17px;display:grid;gap:5px;color:#f2f3e7;text-shadow:0 2px 5px #0009;pointer-events:none}.gyro-scene-eyebrow{font-size:10px!important;letter-spacing:1.6px;font-weight:700;color:#b7d9d8}.gyro-scene-caption strong{font-size:21px;letter-spacing:.3px}.gyro-scene-caption>span:last-child{font-size:12px;color:#d3dfd7}
.gyro-game-readout{z-index:2;position:absolute;right:16px;top:18px;display:flex;gap:17px;pointer-events:none;color:#f2f3e7;text-shadow:0 2px 6px #0009}.gyro-game-readout>div{display:grid;gap:3px;text-align:right}.gyro-game-readout b{font:600 27px/1.05 ui-monospace,monospace;font-variant-numeric:tabular-nums;letter-spacing:-1px}.gyro-game-readout span{font-size:11px;color:#d0ddd6}.gyro-demo-label{z-index:2;position:absolute;left:18px;top:98px;background:#102229c9;border:1px solid #94bcb780;border-radius:5px;padding:4px 8px;color:#d3e9e5;font-size:11px;pointer-events:none}
.gyro-game-controls{z-index:2;position:absolute;inset:auto 0 0;padding:30px 14px 12px;background:linear-gradient(transparent,#08181ce8 45%);color:#e5f0eb}.gyro-game-control-row{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px}.gyro-game-control-row button{min-width:0}.gyro-game-control-row button span{min-width:0;white-space:normal;overflow-wrap:anywhere}.gyro-game-control-row button{display:flex;align-items:center;justify-content:center;gap:9px;min-height:40px;border:1px solid #a6ccc43d;border-radius:7px;background:#122b30ba;color:#e5eeea;font:600 13px/1.2 inherit;white-space:nowrap;cursor:pointer;touch-action:none;user-select:none}.gyro-game-control-row button.held{background:#c9eece;color:#122c2a;border-color:#e4f7dc}.gyro-game-control-row .gyro-play-button{background:#d3e7df;color:#153035;border-color:#dcece5}.gyro-game-control-row .gyro-play-button.active{background:#234746;color:#d9f7ed;border-color:#82b4aa}.gyro-game-control-row kbd{display:grid;place-items:center;min-width:23px;height:22px;border:1px solid currentColor;border-radius:5px;font:700 11px/1 system-ui;opacity:.85}.gyro-game-help{display:flex;justify-content:space-between;gap:8px;margin-top:9px;font-size:11px;line-height:1.4;color:#c1d7cf}.gyro-game-help span:last-child{color:#b0ccc3;white-space:nowrap}.gyro-game-help kbd{font:600 10px system-ui;border:1px solid #799b9080;border-radius:3px;padding:0 3px}.gyro-keyboard-help{font-size:10px;color:#8eaea3;margin-top:5px}
.gyro-simulation-signal{display:flex;align-items:center;gap:8px;min-height:22px;font-size:12px;color:var(--text-dim)}.gyro-simulation-signal i{width:6px;height:6px;flex-shrink:0;border-radius:50%;background:#78828f}.gyro-simulation-signal i.active{background:var(--accent)}.gyro-simulation-signal span:last-child{font-variant-numeric:tabular-nums}
.gyro-simulation-editor{min-width:0;overflow-y:auto;overscroll-behavior:contain;padding:10px;border:1px solid var(--console-line);border-radius:12px;background:var(--console-surface);scroll-padding-block:14px}.gyro-editor-label{display:flex;align-items:center;justify-content:space-between;margin-bottom:10px;font-size:14px;font-weight:650}.gyro-editor-label span{font-size:11px;font-weight:400;color:var(--text-dim)}.gyro-simulation-settings{min-width:0}
@media(max-width:1000px){.gyro-game-help{flex-wrap:wrap;gap:2px}.gyro-scene-caption strong{font-size:18px}.gyro-game-readout{gap:12px}.gyro-scene-caption>span:last-child{max-width:210px}.gyro-game-control-row button{gap:6px}}
@media(max-width:760px){.gyro-simulation-window{--gyro-editor-width:56%;padding:8px;gap:8px}.gyro-simulation-body{gap:8px}.gyro-simulation-heading span{display:none}.gyro-scene-tabs{gap:4px;padding:4px}.gyro-scene-tabs button{font-size:12px;min-height:34px}.gyro-scene-caption{left:9px;top:12px}.gyro-scene-caption strong{font-size:15px}.gyro-scene-eyebrow{font-size:9px!important;letter-spacing:.5px}.gyro-scene-caption>span:last-child{display:none}.gyro-game-readout{top:80px;right:10px}.gyro-game-controls{padding:25px 7px 10px}.gyro-game-control-row{gap:4px}.gyro-game-control-row button{flex-direction:column;font-size:11px;gap:4px;padding:6px 3px}.gyro-game-help{font-size:10px}}
</style>
