<script setup lang="ts">
import { nextTick, onMounted, onBeforeUnmount, ref } from 'vue';
import { createPerformanceSimulationSession, simulationPanelCommands } from '@/bridge/performanceSimulation';
import ContinuousPerformancePanel from './ContinuousPerformancePanel.vue';
import { automaticCpuControls, defaultContinuousBank, stepContinuousFloat } from '@/bridge/performanceControls';
import type { PerformancePanelSource } from '@/bridge/performancePanelSource';
import { spatialNavigationTarget, nearestSpatialTarget } from '@/gamepad/spatial';
import { performancePanelTarget } from '@/gamepad/performanceNavigation';
import { stopPerformanceScheduleAndUseWindowsBalanced, onGameCustomConfigChanged, onPerformanceScheduleChanged } from '@/bridge/performanceSchedule';
import { onFrameRatesChanged } from '@/bridge/frameRateLimits';
import { powerSourceMode } from '@/bridge/powerSource';
import { watch } from 'vue';
import { windowApi } from '@/bridge/api';
import { isNativeRuntime } from '@/bridge/ipc';
import { subscribePolicyGameStatus } from '@/bridge/gamePolicyTarget';
const emit = defineEmits<{ (e:'close'):void }>();
const frame = ref<HTMLIFrameElement | null>(null), error = ref('');
const editor = ref<HTMLElement | null>(null), editorReady = ref(false), modalOpen = ref(false), tdpAdvisory = ref('');
const wide = ref(window.innerWidth >= 980);
// Preserve the right editor proportions and derive its width from the available full height.
const editorScale = ref(Math.max(.1,(window.innerHeight-92)/628));
let editorRange: HTMLInputElement | null = null;
const session = createPerformanceSimulationSession(), stops: (()=>void)[] = [];
const opener = document.activeElement as HTMLElement | null;
let ready = false, alive = true, saving = false, publishTicket = 0;
const origin = location.origin;
const widthOwner = `performance-simulation-${Date.now()}-${Math.random().toString(36).slice(2)}`;
let widthOwned = false;
// Reuse the production component against the existing isolated draft, never the live writer.
function demo() { return frame.value?.contentWindow as (Window & { YmccScheduleLink: any; ymccEnergyDemo: any; YmccSimulationNavigation: any }) | null; }
const panelSource: PerformancePanelSource = {
  sideLocked: true,
  async read() {
    const link = demo()?.YmccScheduleLink;
    if (!link?.snapshot()) throw new Error('正在载入临时参数');
    return session.panel(link.baseSnapshot(), link.snapshot());
  },
  async commit(identity, patch) {
    const link = demo()?.YmccScheduleLink;
    const current = await this.read();
    if (!link || current.side !== identity.side) throw new Error('供电配置已变化，请恢复当前设置');
    for (const command of simulationPanelCommands(patch,current)) {
      if (command.key === 'manualSmt') {
        const game = link.snapshot().gameThreadPolicy;
        if (game?.target) Object.assign(command,{gameTarget:{exe:game.exe,...game.target}});
      }
      await link.edit(command);
    }
    await link.flush();
    if (patch.mode) demo()?.ymccEnergyDemo.selectPreview(patch.mode);
    return this.read();
  },
  subscribe(refresh) {
    const link=demo()?.YmccScheduleLink;
    if(!link)return () => undefined;
    const stop=link.subscribe(refresh),status=link.subscribeStatus((value:any) => { if(!value.loading&&link.baseSnapshot())refresh(); });
    return () => { stop();status(); };
  },
};
function clearEditorRange() { if (editorRange) delete editorRange.dataset.gpEditing; editorRange = null; }
function visible(el: HTMLElement) { const r=el.getBoundingClientRect();return !el.matches(':disabled') && r.width>0 && r.height>0; }
function editorNodes() { return [...(editor.value?.querySelectorAll<HTMLElement>('button,input[type=range]')??[])].filter(visible); }
function focusEditor(y: number, x = editor.value?.getBoundingClientRect().x ?? 0) {
  const score=(el:HTMLElement)=>{const r=el.getBoundingClientRect();return Math.abs(r.y+r.height/2-y)*10000+Math.abs(r.x+r.width/2-x);};
  const nodes=editorNodes(), target=nodes.sort((a,b)=>score(a)-score(b))[0];
  target?.focus({preventScroll:true});
}
function focusPreview(dx: -1 | 0 | 1, dy: -1 | 0 | 1) {
  const child=demo(), current=document.activeElement as HTMLElement;
  if (!child || !current) return;
  const candidates=[...child.document.querySelectorAll<HTMLElement>('.app [data-nav]')].filter(visible);
  // Crossing documents uses viewport geometry rather than either editor's local scope.
  const anchor={getBoundingClientRect:()=>current.getBoundingClientRect(),closest:()=>null} as unknown as HTMLElement;
  const target=nearestSpatialTarget(candidates,anchor,{dx,dy});
  if (target) { clearEditorRange();frame.value?.focus({ preventScroll: true });target.focus({preventScroll:true}); }
}
function editorCommand(action: string) {
  const current=document.activeElement as HTMLElement | null;
  if (!current) return;
  const menu=current.closest('.dd-menu');
  if (menu) {
    const key=({a:'Enter',b:'Escape',up:'ArrowUp',down:'ArrowDown'} as Record<string,string>)[action];
    if(key) current.dispatchEvent(new KeyboardEvent('keydown',{key,bubbles:true,cancelable:true}));
    return;
  }
  if (action==='b') { if(editorRange)clearEditorRange();else forward('b');return; }
  if (action==='a') {
    if(current.matches('input[type=range]')) { if(editorRange===current)clearEditorRange();else{clearEditorRange();editorRange=current as HTMLInputElement;editorRange.dataset.gpEditing='true';} }
    else if(current.matches('[data-gp-dropdown]'))current.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true,cancelable:true}));
    else current.click();
    return;
  }
  const dx=['right','slider-right'].includes(action)?1:['left','slider-left'].includes(action)?-1:0,dy=action==='up'?-1:action==='down'?1:0;
  if (editorRange && dx) {
    const el=editorRange,step=Number(el.step)||1;
    el.value=String(Math.max(Number(el.min),Math.min(Number(el.max),Number(el.value)+dx*step)));
    el.dispatchEvent(new Event('input',{bubbles:true}));el.dispatchEvent(new Event('change',{bubbles:true}));return;
  }
  if(action.startsWith('slider-'))return;
  if (dy)clearEditorRange();
  const nodes=editorNodes(),panelTarget=performancePanelTarget(nodes,current,{dx,dy});
  const target=panelTarget===undefined?spatialNavigationTarget(nodes,current,{dx,dy}):panelTarget;
  if(target)target.focus({preventScroll:true});else focusPreview(dx,dy);
}
// The production editor is the layout authority. The renderer fills only the remaining left region.
let editorLayoutObserver: ResizeObserver | null = null;
function publishEditorLayout() {
  const box=editor.value?.getBoundingClientRect(), viewport=frame.value?.getBoundingClientRect();
  if(!box||!viewport||box.width<=0||box.height<=0)return;
  post({type:'ymcc-simulation-editor-layout',layout:{width:box.width,height:box.height,left:box.left-viewport.left,top:box.top-viewport.top,right:viewport.right-box.right,gap:12}});
}
watch(editor,element=>{editorLayoutObserver?.disconnect();if(element){editorLayoutObserver=new ResizeObserver(publishEditorLayout);editorLayoutObserver.observe(element);publishEditorLayout();}},{flush:'post'});
function resize() { wide.value=window.innerWidth>=980;editorScale.value=Math.max(.1,(window.innerHeight-92)/628);void nextTick(publishEditorLayout); }
function editorKey(event: KeyboardEvent) {
  if(!event.isTrusted||event.ctrlKey||event.metaKey||event.altKey)return;
  if(!(editor.value?.contains(document.activeElement)||document.activeElement?.closest('.dd-menu')))return;
  const action=({Enter:'a',' ':'a',Escape:'b',ArrowLeft:'left',ArrowRight:'right',ArrowUp:'up',ArrowDown:'down'} as Record<string,string>)[event.key];
  if(action){event.preventDefault();event.stopImmediatePropagation();if(!event.repeat||!['a','b'].includes(action))editorCommand(action);}
}
async function widenWindow() {
  if (!isNativeRuntime || /[?&]__ymccChild=\d+/.test(location.hash)) return;
  try {
    const result = await windowApi.controllerEditorWidth(widthOwner, true, 2);
    if (!alive) { if (result.ok) await windowApi.controllerEditorWidth(widthOwner, false); return; }
    widthOwned = result.ok;
  } catch { /* A width lease failure does not disable the local simulator. */ }
}
function post(value: unknown) { frame.value?.contentWindow?.postMessage(value, origin); }
async function publish(type = 'ymcc-simulation-source', requestId?: string) {
  const ticket = ++publishTicket;
  try {
    const snapshot = await session.read();
    if (!alive || ticket !== publishTicket) return;
    error.value = ''; post({type,snapshot,requestId});
    if (type === 'ymcc-simulation-session-start') { post({type:'ymcc-simulation-host',action:'resume'}); frame.value?.focus({ preventScroll: true }); }
  } catch (reason) { if (alive) error.value = (reason as Error).message; }
}
function forward(command: string) { if (ready) post({type:'ymcc-simulation-host',action:'command',command}); else if(command==='b') emit('close'); }
async function message(event: MessageEvent) {
  if (!alive || event.origin !== origin || event.source !== frame.value?.contentWindow) return;
  const data = event.data;
  if (data?.type === 'ymcc-simulation-ready') { ready = true;
    const child=demo();
    if (child) Object.assign(child,{YmccProductionControls:Object.freeze({automaticCpuControls,defaultContinuousBank,stepContinuousFloat})});
    if(child?.YmccScheduleLink) {
      stops.push(child.YmccScheduleLink.subscribeStatus((value:any) => { if(!value.loading&&child.YmccScheduleLink.baseSnapshot())editorReady.value=true; }));
      const observer=new MutationObserver(() => { modalOpen.value=!!child.document.querySelector('dialog[open]');if(modalOpen.value){clearEditorRange();frame.value?.focus({ preventScroll: true });} });
      observer.observe(child.document.body,{subtree:true,attributes:true,attributeFilter:['open']});stops.push(() => observer.disconnect());
    }
    await publish('ymcc-simulation-session-start'); }
  else if (data?.type === 'ymcc-simulation-source-request') await publish('ymcc-simulation-source', data.requestId);
  else if (data?.type === 'ymcc-simulation-focus-editor' && wide.value) focusEditor(Number(data.y)||90, Number.isFinite(data.x)?data.x:undefined);
  else if (data?.type === 'ymcc-simulation-tdp-advisory') tdpAdvisory.value=data.show?String(data.message||''):'';
  else if (data?.type === 'ymcc-simulation-return') emit('close');
  else if (data?.type === 'ymcc-simulation-stop-schedule') {
    if (saving) { post({type:'ymcc-simulation-stop-result',requestId:data.requestId,ok:false,reason:'已有设置操作正在进行，请稍后重试'}); return; }
    saving = true;
    try {
      if (!isNativeRuntime) throw new Error('此操作仅在 YMCC 内可用');
      await stopPerformanceScheduleAndUseWindowsBalanced();
      if (alive) post({type:'ymcc-simulation-stop-result',requestId:data.requestId,ok:true});
    } catch (reason) {
      if (alive) post({type:'ymcc-simulation-stop-result',requestId:data.requestId,ok:false,reason:(reason as Error).message});
    } finally { saving = false; }
  }
  else if (data?.type === 'ymcc-simulation-source-save' && !saving) {
    saving = true;
    try { const result = await session.save(data.base, data.draft); if (alive) post({type:'ymcc-simulation-save-result',requestId:data.requestId,...result}); }
    catch (reason) { if (alive) post({type:'ymcc-simulation-save-result',requestId:data.requestId,ok:false,reason:(reason as Error).message}); }
    finally { saving = false; }
  }
}
function command(event: Event) {
  const action = (event as CustomEvent<{action:string}>).detail?.action;
  const mapped: Record<string,string> = {confirm:'a',back:'b','nav-left':'left','nav-right':'right','nav-up':'up','nav-down':'down','slider-decrease':'slider-left','slider-increase':'slider-right'};
  if (mapped[action]) { event.preventDefault();
    if(editor.value?.contains(document.activeElement)||document.activeElement?.closest('.dd-menu')) editorCommand(mapped[action]);
    else forward(mapped[action]);
  }
  else if (action === 'page-prev' || action === 'page-next') event.preventDefault();
}
function back(event: Event) { if(event.defaultPrevented)return;event.preventDefault();if(editor.value?.contains(document.activeElement)||document.activeElement?.closest('.dd-menu'))editorCommand('b');else forward('b'); }
function key(event: KeyboardEvent) { if (!event.defaultPrevented && event.key === 'Escape') { event.preventDefault(); forward('b'); } }
onMounted(async () => {
  void widenWindow();
  window.addEventListener('resize',resize);window.addEventListener('keydown',editorKey,true);
  window.addEventListener('message',message); window.addEventListener('gp:page-command',command);
  window.addEventListener('ipc:gamepad-back',back); window.addEventListener('keydown',key);
  const refresh = () => { if (ready && !saving) void publish(); };
  stops.push(watch(powerSourceMode,refresh),subscribePolicyGameStatus(refresh),onFrameRatesChanged(refresh),
    onGameCustomConfigChanged(refresh),onPerformanceScheduleChanged(refresh));
  await nextTick(); frame.value?.focus({ preventScroll: true });
});
onBeforeUnmount(() => {
  editorLayoutObserver?.disconnect();clearEditorRange();window.removeEventListener('resize',resize);window.removeEventListener('keydown',editorKey,true);
  post({type:'ymcc-simulation-host',action:'suspend'}); alive=false;
  if (widthOwned) void windowApi.controllerEditorWidth(widthOwner, false).catch(() => undefined); publishTicket++;
  for(const stop of stops)stop(); window.removeEventListener('message',message); window.removeEventListener('gp:page-command',command);
  window.removeEventListener('ipc:gamepad-back',back); window.removeEventListener('keydown',key); opener?.focus({preventScroll:true});
});
</script>
<template>
  <Teleport to="body"><div class="performance-simulation-window" data-gp-modal="performance-simulation" data-gp-page-lock role="dialog" aria-modal="true" aria-label="模拟测试/重置">
    <iframe ref="frame" title="模拟测试" src="/performance-simulation/index.html?embedded=1&amp;nativeHost=1&amp;nativeEditor=1" allow="gamepad" tabindex="0" />
    <aside v-if="editorReady && wide" ref="editor" v-show="!modalOpen" :inert="modalOpen" :style="{width: `${432*editorScale}px`, '--simulation-editor-scale': String(editorScale)}" :data-performance-editor-scale="editorScale" class="simulation-editor" aria-label="性能调节 · 模拟临时配置" @focusout="event => { if(event.target===editorRange)clearEditorRange(); }">
      <div class="simulation-editor-content"><ContinuousPerformancePanel scope="effective" :panel-source="panelSource" />
      <p class="simulation-tdp-advisory" role="status" :title="tdpAdvisory">{{tdpAdvisory}}</p></div>
    </aside>
    <div v-if="error" class="simulation-error" role="alert"><p>{{error}}</p><button @click="publish('ymcc-simulation-session-start')">重新连接</button><button @click="emit('close')"><strong>B</strong>关闭页面</button></div>
  </div></Teleport>
</template>
<style scoped>
.performance-simulation-window{position:fixed;inset:0;z-index:700;background:var(--bg,#101722)}iframe{display:block;width:100%;height:100%;border:0}.simulation-error{position:absolute;inset:0;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:14px;background:var(--bg,#101722);font-size:15px}.simulation-error button{min-height:48px;min-width:180px;font:inherit}.simulation-error strong{color:#ef7979;margin-right:8px}

.simulation-editor{position:absolute;top:58px;right:12px;bottom:34px;min-width:0;background:var(--bg-solid,#101722);border-radius:12px}
.simulation-editor-content{display:flex;flex-direction:column;gap:4px;width:432px;height:628px;zoom:var(--simulation-editor-scale,1)}
.simulation-editor :deep(.continuous-performance){flex:1;min-height:0;grid-template-rows:repeat(4,auto) minmax(0,1fr);gap:6px;--performance-control-h:35px;--performance-track-h:30px}
.simulation-editor :deep(.supply-status){zoom:calc(1 / var(--simulation-editor-scale,1));position:fixed;top:17px;left:302px;min-height:24px;font-size:13px}
.simulation-editor :deep(.control-modes),.simulation-editor :deep(.power-modes){padding:4px;gap:6px}
.simulation-editor :deep(.parameter-summary){padding:6px 10px;gap:8px}
.simulation-editor :deep(.parameter-summary span){font-size:12px}
.simulation-editor :deep(.tdp-summary b){font-size:23px}
.simulation-editor :deep(.limits-bubble){padding:4px 10px}
.simulation-editor :deep(.slider-dropdown-row){grid-template-columns:56px minmax(0,1fr) 100px;gap:8px;padding:2px 0 6px}
.simulation-editor :deep(.slider-dropdown-row+.slider-dropdown-row){padding-top:8px;padding-bottom:1px}
.simulation-editor :deep(.metric-label){gap:3px}
.simulation-editor :deep(.metric-label strong){font-size:22px;line-height:25px}
.simulation-editor :deep(.auto-bubble){flex:1;display:flex;flex-direction:column;padding:7px 10px}
.simulation-editor :deep(.saving-section){padding-bottom:6px}
.simulation-editor :deep(.bias-section){flex:1;display:flex;flex-direction:column;justify-content:center;padding:6px 0 9px}
.simulation-editor :deep(.bias-labels){margin-bottom:2px}
.simulation-editor :deep(.bias-ticks){height:16px;margin-top:0}
.simulation-editor :deep(.bias-ticks small){top:9px}
.simulation-editor :deep(.core-section){padding-top:6px}
.simulation-editor :deep(.core-heading b){font-size:19px}
.simulation-editor :deep(.core-topology){margin:4px 0 0;font-size:11px}
.simulation-editor :deep(.auto-core-row){gap:6px;margin-top:3px}
.simulation-editor :deep(.core-choice){gap:3px}
.simulation-editor :deep(.core-choice>small){font-size:10px;line-height:12px}
.simulation-editor :deep(.slider-head){margin-bottom:4px}
.simulation-editor :deep(.tdp-summary>div){gap:3px}
.simulation-editor :deep(.manual-bubble){flex:1;display:flex;flex-direction:column;justify-content:space-between;padding:10px}
.simulation-editor :deep(input[data-gp-editing=true]){outline:2px solid var(--accent);outline-offset:3px;border-radius:6px}
.simulation-tdp-advisory:empty{visibility:hidden}
.simulation-tdp-advisory{flex:0 0 auto;height:16px;line-height:16px;margin:0;padding:0 8px;font-size:11px;color:#f5c77a;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
</style>
