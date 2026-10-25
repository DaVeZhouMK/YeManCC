<script setup lang="ts">
// [AI:USER:MONITOR] “显示监控”控制显示，“弹出提示”独立；切自定义/RTSS→使用选中的输出，切页不等于关闭监控。
// 编辑→修改当前模板；返回→回到监控页。缺数据如实显示未知/无数据，不能把预览、期望转速或TDP上限当实测。
// 输出恢复与失败状态按实际结果呈现；具体协调读 bridge/monitorOutputCoordinator.ts，UI/手柄共性见 [AI:UI:*]。
import { readMonitorCpuName } from '@/bridge/monitorHardwareMetadata';
import SettingsSections from '@/components/SettingsSections.vue';
import { computed, nextTick, onMounted, onActivated, onDeactivated, onUnmounted, reactive, ref, shallowRef, watch } from 'vue';
import Dropdown from './Dropdown.vue';
import Slider from './Slider.vue';
import Toggle from './MonitorToggle.vue';
import SegButton from './SegButton.vue';
import InlineIcon from './InlineIcon.vue';
import MonitorHudPreview from './MonitorHudPreview.vue';
import { subscribeMonitorGameLogo } from '@/bridge/monitorGameLogo';
import MonitorCrosshair from './MonitorCrosshair.vue';
import { prepareHudUnderlayFile } from '@/bridge/monitorHudUnderlayImport';
import { pickNativeHudUnderlayFile } from '@/bridge/monitorHudUnderlayPicker';
import { isNativeRuntime } from '@/bridge/ipc';
import { focusGamepadElement } from '@/gamepad/focus';
import { HUD_DEFAULT_TEMPLATE_SCALES, HUD_TEMPLATES, switchMonitorHudTemplate, resetMonitorHudTemplate, type HudTemplateId, HUD_CROSSHAIRS, HUD_VISIBILITIES, HUD_UNDERLAY_PRESETS, type HudUnderlayPreset, HUD_DATA_PRESETS, hudDataPresetFields, type HudDataPresetId, hudMetricControlLabel, HUD_METRICS, HUD_LAYOUTS, HUD_FONTS, HUD_FPS_FONTS, isHorizontalHudLayout, isVerticalHudLayout, selectableMonitorOutput, monitorOutputChoice, RTSS_MONITOR_TEMPLATES, MONITOR_OUTPUTS, makeHudFixture, hudFromUniformSnapshot, normalizeMonitorOutput, type HudMetricId, type HudAppearance, type HudLayout, type HudData } from '@/bridge/monitorHudModel';
import { readMonitorOutputPreferences, saveMonitorOutputPreferences, onMonitorOutputPreferencesChanged, readMonitorOutputRuntime, monitorOutputCoordinator, onMonitorHudAvailability } from '@/bridge/monitorOutput';
import { setRtssZoom } from '@/bridge/yeman';
import { subscribeMonitorSnapshot, subscribeMonitorTdpState, type MonitorSnapshotView, type TdpRuntimeSnapshot } from '@/bridge/monitorData';
import type { AppliedMonitorOutput } from '@/bridge/monitorOutputCoordinator';
import { createMonitorLiveEdits } from '@/bridge/monitorLiveEdits';
import { createHudAgeLease } from '@/bridge/monitorHudFreshness';
const emit=defineEmits<{(event:'close'):void;(event:'edit'):void;(event:'ready'):void}>();
const props = withDefaults(defineProps<{ previewOnly?: boolean; overviewOnly?: boolean; editorVariant?:'a'|'b'|'c' }>(), { previewOnly: false, editorVariant:'a' });
const prefs = reactive(normalizeMonitorOutput());
const loading = ref(true), busy = ref(false), status = ref('读取监控配置…'), failed = ref(false);
const applied = ref<AppliedMonitorOutput>('unknown'), backend = ref(false), ymccAvailable = ref(false);
const editorElement=ref<HTMLElement|null>(null),editorHeight=ref(0);
let editorResize:ResizeObserver|null=null;
function fitEditorViewport(){
 const root=editorElement.value,content=root?.closest<HTMLElement>('[data-monitor-editor-viewport],.app-content');if(!root||!content||!root.offsetWidth)return;
 const box=root.getBoundingClientRect(),viewport=content.getBoundingClientRect(),scale=box.width/root.offsetWidth;
 if(scale<=0||box.height===0)return;
 const padding=Number.parseFloat(getComputedStyle(content).paddingBottom)||0;
 editorHeight.value=Math.max(280,Math.floor((Math.min(innerHeight,viewport.bottom)-box.top)/scale-padding));
}
function observeEditorViewport(){
 const content=editorElement.value?.closest<HTMLElement>('[data-monitor-editor-viewport],.app-content');
 if(content){editorResize=new ResizeObserver(fitEditorViewport);editorResize.observe(content);}
 window.addEventListener('resize',fitEditorViewport);void nextTick(fitEditorViewport);
}
const sample = makeHudFixture();
const cpuName = ref('');
const previewOriginal=ref(false);
const liveTdpState = shallowRef<TdpRuntimeSnapshot | null>(null);
const liveSnapshot = shallowRef<MonitorSnapshotView | null>(null);
const previewReadings = shallowRef<HudData>(hudFromUniformSnapshot(null));
const previewGameLogo = ref('');
const previewData = computed(() => ({ ...(props.previewOnly ? sample : previewReadings.value), gameLogo: previewGameLogo.value }));
let offGameLogo: (() => void) | null = null;
function syncGameLogo() {
  const wanted = active && !props.previewOnly && (prefs.enabled || props.overviewOnly) && prefs.output === 'ymcc' && prefs.ymcc.underlay === 'game-logo';
  if (wanted && !offGameLogo) offGameLogo = subscribeMonitorGameLogo(image => { previewGameLogo.value = image; });
  if (!wanted && offGameLogo) { offGameLogo(); offGameLogo = null; previewGameLogo.value = ''; }
}
watch(() => [prefs.enabled, prefs.output, prefs.ymcc.underlay] as const, (next, previous) => {
  if ((next[0] !== previous[0] || next[1] !== previous[1]) && active) startSnapshotView();
  else syncGameLogo();
});
let offTdpState: (() => void) | null = null, offSnapshot: (() => void) | null = null;
let previewAge: ReturnType<typeof createHudAgeLease> | null = null;
let projectSnapshot: (() => void) | null = null;
watch(() => prefs.ymcc.fields.clock, () => { projectSnapshot?.(); });
function stopSnapshotView() {
  projectSnapshot = null;
  previewAge?.dispose(); previewAge = null;
  offGameLogo?.(); offGameLogo = null; previewGameLogo.value = '';
  offTdpState?.(); offTdpState = null; liveTdpState.value = null;
  offSnapshot?.(); offSnapshot = null; liveSnapshot.value = null;
}
function startSnapshotView() {
  stopSnapshotView(); syncGameLogo(); if (!active || props.previewOnly || (!prefs.enabled && !props.overviewOnly) || prefs.output !== 'ymcc') return;
  // Use the same per-family measurement leases as the output renderer. A 5s
  // FPS/Low/curve observation must not inherit a renewed packet's ts + 6s.
  const age = previewAge = createHudAgeLease({now: () => Date.now(), emit: value => { previewReadings.value = value; },
    setTimer: (fn, ms) => window.setTimeout(fn, ms), clearTimer: id => window.clearTimeout(id as number)});
  const project = projectSnapshot = () => age.accept(hudFromUniformSnapshot(liveSnapshot.value, Date.now(), liveTdpState.value, prefs.ymcc.fields.clock === true,cpuName.value));
  offTdpState = subscribeMonitorTdpState(value => { liveTdpState.value = value; project(); });
  offSnapshot = subscribeMonitorSnapshot(snapshot => { liveSnapshot.value = snapshot; project(); });
}
const metricSettingsGroup=(metric:typeof HUD_METRICS[number])=>metric.id==='tdpLimit'||metric.id==='cpuPower'?'TDP':metric.group==='附加'?'电池':metric.group;
const fieldColumns=(_group:string)=>props.editorVariant==='a'?3:2;
const metricGroups = [...new Set(HUD_METRICS.map(metricSettingsGroup))].map(name => ({ name, metrics: HUD_METRICS.filter(m => metricSettingsGroup(m) === name) }));
const selectedOutput = computed(() => monitorOutputChoice(prefs));
const workbenchPanels = computed(() => prefs.output === 'ymcc' ? isHorizontalHudLayout(prefs.ymcc.layout) ? ['preview', 'appearance'] : ['appearance', 'preview'] : ['rtss']);
const activeFieldGroup = computed({ get: () => prefs.ymcc.fieldGroup, set: value => { prefs.ymcc.fieldGroup = value; persist(false, true); } });
const FIELD_CATEGORY_COLUMNS = 2;
const selectedMetrics = computed(() => HUD_METRICS.filter(metric => prefs.ymcc.fields[metric.id]));
const groupLabel = (name: string) => ({ '帧率': '帧率', '内存': '内存', '读写': '读写', '电池': '电池', '附加': '附加' })[name] || name;

let active = true, saveQueue: Promise<unknown> = Promise.resolve(), readEpoch = 0;
async function refresh() {
  if (!active || busy.value) return;
  const epoch = ++readEpoch;
  try {
    await liveEdits.flush(); await saveQueue;
    const [saved, runtime] = await Promise.all([readMonitorOutputPreferences(), readMonitorOutputRuntime()]);
    if (!active || epoch !== readEpoch) return;
    Object.assign(prefs, selectableMonitorOutput(saved)); prefs.rtssScale = runtime.rtssScale; if (runtime.rtssTemplate) prefs.rtssTemplate = runtime.rtssTemplate;
    applied.value = runtime.applied; backend.value = runtime.backend; ymccAvailable.value = runtime.ymccAvailable;
    status.value = props.previewOnly ? '浏览器示例预览 · 不接硬件、不修改输出' : '自定义监控按所选条件显示；数据由 YMCC 统一数据层提供。';
    failed.value = false;
  } catch (error) { if (active && epoch === readEpoch) { status.value = '读取失败：' + String(error); failed.value = true; } }
  finally { if (active && epoch === readEpoch) { loading.value = false; if(props.overviewOnly)emit('ready'); } }
}
function save(snapshot = normalizeMonitorOutput(prefs)) {
  ++readEpoch;
  const result = saveQueue.then(() => saveMonitorOutputPreferences(snapshot)); saveQueue = result.catch(() => {});
  return result.catch(error => { const message = '保存失败：' + String(error); if (active) { status.value = message; failed.value = true; } throw Error(message); });
}
const liveEdits = createMonitorLiveEdits({
  snapshot: () => normalizeMonitorOutput(prefs), save,
  async apply(snapshot) {
    const result = await monitorOutputCoordinator.apply(snapshot);
    if (active && (result.ok || result.phase !== 'preflight')) applied.value = result.applied;
    if (!result.ok) throw Error(result.reason);
    if (active) { backend.value = true; failed.value = false; status.value = ''; }
  },
  async steam() { /* Native Steam appearance has no user entry. */ },
  async rtss(snapshot) { if (snapshot.output === 'rtss') await setRtssZoom(Math.round(snapshot.rtssScale / 20)); },
  busy: value => { if (active) busy.value = value; },
  failure: error => { if (active) { status.value = String(error instanceof Error ? error.message : error); failed.value = true; } },
  timer: (callback, delay) => window.setTimeout(callback, delay), clearTimer: handle => window.clearTimeout(handle as number),
});
function persist(output = false, immediate = false) { ++readEpoch; failed.value = false; liveEdits.request({ preferences: true, output, immediate }); }
function choose(value: string) {
  if (value === 'rtss') prefs.output = 'rtss';
  else if (value === 'desktop') { prefs.output = 'ymcc'; prefs.ymccCarrier = 'desktop'; }
  else return;
  persist(true, true);
}
function chooseVisibility(value: string | number) {
  const mode = HUD_VISIBILITIES.find(item => item.value === value)?.value;
  if (!mode) return;
  prefs.ymcc.visibility = mode; persist(false, true);
}
function chooseHudLayout(value: string | number) {
  const layout = HUD_LAYOUTS.find(item => item.value === value)?.value;
  if (!layout) return;
  prefs.ymcc.layout = layout as HudLayout; persist(false, true);
}
const templateSwitching=ref(false);
const editingTemplate=computed(()=>prefs.ymccTemplate ?? 'A');
const templateControlsDisabled=computed(()=>loading.value || busy.value || templateSwitching.value);
async function chooseTemplate(value:string|number) {
  if(value!=='A'&&value!=='B'||templateControlsDisabled.value||value===editingTemplate.value)return;
  templateSwitching.value=true;
  try {
    await liveEdits.flush();await saveQueue;
    if(!active)return;
    Object.assign(prefs,switchMonitorHudTemplate(normalizeMonitorOutput(prefs),value as HudTemplateId));persist(false,true);
  } finally {templateSwitching.value=false;}
}
function toggleDragLock() {
  if(loading.value||busy.value)return;
  prefs.ymcc.dragLocked=!prefs.ymcc.dragLocked;
  prefs.ymcc.dragRevision=Math.max((prefs.ymcc.dragRevision??0)+1,Date.now());persist(false,true);
}
function enabled(value: boolean) { prefs.enabled = value; persist(true, true); }
function popupEnabled(value: boolean) { prefs.popupEnabled = value; persist(false, true); }
function mouseStatusEnabled(value: boolean) { prefs.mouseStatusEnabled = value; persist(false, true); }
function chooseFont(kind: 'font' | 'fpsFont', value: string | number) {
  const choice = (kind === 'font' ? HUD_FONTS : HUD_FPS_FONTS).find(item => item.value === value);
  if (!choice) return;
  if (kind === 'font') prefs.ymcc.font = HUD_FONTS.find(item => item.value === choice.value)?.value ?? 'system';
  else prefs.ymcc.fpsFont = choice.value;
  persist(false, true);
}
function toggleHardwareName(key:'showCpuName'|'showGpuName',value:boolean){prefs.ymcc[key]=value;persist(false,true);}
function valueScale(id:HudMetricId,value:number,immediate=false){prefs.ymcc.valueScales={...prefs.ymcc.valueScales,[id]:value/100};persist(false,immediate);}
function hasNumericText(metric:typeof HUD_METRICS[number]){return metric.ready && metric.id!=='cpuCores' && metric.id!=='frametimeGraph' && !metric.id.endsWith('Bar') && metric.id!=='gpuBar';}
function fpsEmphasis(value: boolean) { prefs.ymcc.fpsEmphasis = value; persist(false, true); }
const underlayInput = ref<HTMLInputElement | null>(null), underlayImporting = ref(false), underlayPicking = ref(false), underlayError = ref('');
const underlayBusy = computed(() => underlayPicking.value || underlayImporting.value);
async function openUnderlayPicker() {
  if (loading.value || busy.value || underlayBusy.value) return;
  // Native A uses a programmatic click, without browser user activation.
  // Reuse YMCC's Windows dialog; offline browser previews retain the file input.
  if (!isNativeRuntime) { underlayInput.value?.click(); return; }
  underlayPicking.value = true; underlayError.value = '';
  try {
    const file = await pickNativeHudUnderlayFile();
    if (file && active) await importUnderlayFile(file);
  } catch (error) { if (active) underlayError.value = error instanceof Error ? error.message : '图片选择失败'; }
  finally { underlayPicking.value = false; }
}
function chooseUnderlay(value: string | number) {
  const preset = HUD_UNDERLAY_PRESETS.find(item => item.value === value)?.value; if (!preset) return;
  prefs.ymcc.underlayPreset = preset as HudUnderlayPreset; underlayError.value = '';
  prefs.ymcc.underlay = preset === 'custom' && !prefs.ymcc.underlayImage ? 'none' : preset;
  persist(false, true);
  if (preset === 'custom') openUnderlayPicker();
}
async function importUnderlay(event: Event) {
  const input = event.target as HTMLInputElement, file = input.files?.[0]; input.value = '';
  if (file) await importUnderlayFile(file);
}
async function importUnderlayFile(file: File) {
  if (underlayImporting.value || !active) return;
  underlayImporting.value = true; underlayError.value = '';
  try { const image = await prepareHudUnderlayFile(file); if (!active) return; prefs.ymcc.underlayImage = image; prefs.ymcc.underlayPreset = 'custom'; prefs.ymcc.underlay = 'custom'; persist(false, true); }
  catch (error) { underlayError.value = error instanceof Error ? error.message : '图片导入失败'; }
  finally { underlayImporting.value = false; }
}
function hideUnavailable(value: boolean) { prefs.ymcc.hideUnavailable = value; persist(false, true); }
const dataPresetOptions = HUD_DATA_PRESETS.map(item => ({ ...item }));
const dataPreset = computed(() => prefs.ymcc.dataPreset);
const dataPresetStep = computed(() => Math.max(0, dataPresetOptions.findIndex(option => option.value === dataPreset.value)));
const dataPresetLabel = computed(() => dataPresetOptions[dataPresetStep.value].label);
function chooseDataPresetStep(value: number) {
  const option = dataPresetOptions[Math.max(0, Math.min(dataPresetOptions.length - 1, Math.round(value)))];
  chooseDataPreset(option.value);
}
function chooseDataPreset(value: string | number) {
  const choice = HUD_DATA_PRESETS.find(item => item.value === value); if (!choice) return;
  prefs.ymcc.dataPresetFields[prefs.ymcc.dataPreset] = { ...prefs.ymcc.fields };
  prefs.ymcc.dataPreset = choice.value as HudDataPresetId;
  prefs.ymcc.fields = { ...prefs.ymcc.dataPresetFields[prefs.ymcc.dataPreset] };
  persist(false, true);
}
const resetPresetTitle = computed(() => '恢复'+editingTemplate.value+'模板默认配置：'+(editingTemplate.value==='A'?'简约':'精简')+'、轻量胶囊、整体大小'+Math.round(HUD_DEFAULT_TEMPLATE_SCALES[editingTemplate.value]*100)+'%');
async function resetTemplate() {
  if(templateControlsDisabled.value)return;
  const selected=editingTemplate.value;
  failed.value=false;
  templateSwitching.value=true;
  try {
    await flushEdits();
    if(!active || editingTemplate.value!==selected)return;
    Object.assign(prefs,resetMonitorHudTemplate(normalizeMonitorOutput(prefs)));
    persist(false,true);
    await flushEdits();
  } catch(error) {
    if(active){status.value='重置失败：'+String(error instanceof Error?error.message:error);failed.value=true;}
  } finally {templateSwitching.value=false;}
}
function field(id: HudMetricId, value: boolean) {
  prefs.ymcc.fields[id] = value;
  prefs.ymcc.dataPresetFields[prefs.ymcc.dataPreset] = { ...prefs.ymcc.fields };
  persist(false, true);
}
function appearance(key: keyof Omit<HudAppearance, 'layout'>, value: number, immediate = false) { (prefs.ymcc[key] as number) = value; persist(false, immediate); }
function rtssCommit(value: number, immediate = true) {
  prefs.rtssScale = value; ++readEpoch; failed.value = false; liveEdits.request({ preferences: true, rtss: true, immediate });
}
const offPlacementPreferences=onMonitorOutputPreferencesChanged(value=>{
 if((value.ymccTemplateRevision??0)>(prefs.ymccTemplateRevision??0))Object.assign(prefs,normalizeMonitorOutput(value));
 if((value.ymcc.dragRevision??0)>(prefs.ymcc.dragRevision??0)){
   const {dragX,dragY,dragLocked,dragRevision}=value.ymcc;Object.assign(prefs.ymcc,{dragX,dragY,dragLocked,dragRevision});
 }
});
// The header is in the fixed preview pane. Use the existing page-command
// handoff to enter the picture editor, without changing shared spatial rules.
function templatePictureNavigation(event: Event) {
 const command=event as CustomEvent<{action:string}>;
 if(command.defaultPrevented||!active||props.overviewOnly||prefs.output!=='ymcc')return;
 const root=editorElement.value,focused=document.activeElement as HTMLElement|null;
 if(!root||!focused||!root.contains(focused)||focused.getAttribute('aria-expanded')==='true')return;
 if(command.detail?.action==='nav-down'&&focused.matches('[data-monitor-template]')){
  const picture=root.querySelector<HTMLButtonElement>('.monitor-image-underlay--top .dd-trigger');
  // Follow the currently visible editor. Do not switch sections or leave a
  // delayed focus task behind while the shared engine explores neighbours.
  if(picture&&!picture.disabled&&picture.getClientRects().length){command.preventDefault();focusGamepadElement(picture);}
 }else if(command.detail?.action==='nav-up'&&focused.matches('.monitor-image-underlay--top .dd-trigger')){
  const template=root.querySelector<HTMLButtonElement>('[data-monitor-template][aria-pressed="true"]');
  if(template&&!template.disabled){command.preventDefault();focusGamepadElement(template);}
 }
}
let offHudAvailability: (() => void) | null = null;
onMounted(() => { window.addEventListener('gp:page-command',templatePictureNavigation); observeEditorViewport(); if(!props.previewOnly)void readMonitorCpuName().then(name=>{if(active){cpuName.value=name;projectSnapshot?.();}}); if (!props.previewOnly) offHudAvailability = onMonitorHudAvailability(value => { ymccAvailable.value = value; }); startSnapshotView(); void refresh(); });
onActivated(() => { void nextTick(fitEditorViewport); if (!active) { active = true; startSnapshotView(); void refresh(); } });
onDeactivated(() => { void liveEdits.flush(); active = false; ++readEpoch; stopSnapshotView(); });
onUnmounted(() => { window.removeEventListener('gp:page-command',templatePictureNavigation);editorResize?.disconnect();window.removeEventListener('resize',fitEditorViewport);offPlacementPreferences(); offHudAvailability?.(); offHudAvailability = null; active = false; ++readEpoch; stopSnapshotView(); void liveEdits.dispose(); });
async function flushEdits(){ await liveEdits.flush(); await saveQueue; if(failed.value)throw Error(status.value); }
defineExpose({ flushEdits });
</script>
<template>
<div v-if="overviewOnly" ref="editorElement" class="monitor-overview" :style="editorHeight?{height:editorHeight+'px'}:undefined" data-gp-scope="monitor-output" data-gp-spatial="nearest">
 <section class="monitor-overview-output" aria-label="监控输出">
  <h3 class="monitor-overview-heading"><InlineIcon name="monitor"/>监控输出</h3>
  <SegButton class="monitor-overview-output-modes" :model-value="monitorOutputChoice(prefs)" :options="MONITOR_OUTPUTS" color="accent" full :disabled="loading||busy" :gp-row="0" @update:model-value="v=>choose(String(v))" />
  <div class="monitor-overview-controls"><Toggle :model-value="prefs.enabled" label="显示监控" :disabled="loading||busy" :gp-row="1" :gp-col="0" @update:model-value="enabled"/><Toggle :model-value="prefs.popupEnabled" label="弹出提示" :disabled="loading||busy" :gp-row="1" :gp-col="1" @update:model-value="popupEnabled"/><Toggle :model-value="prefs.mouseStatusEnabled" label="鼠标状态" :disabled="loading||busy" :gp-row="1" :gp-col="2" @update:model-value="mouseStatusEnabled"/></div>
  <label v-if="prefs.output==='ymcc'" class="monitor-overview-visibility"><span>监控出现</span><Dropdown :model-value="prefs.ymcc.visibility??'always'" :options="HUD_VISIBILITIES" aria-label="监控出现" color="accent" :disabled="loading||busy" :gp-row="2" :gp-col="0" @update:model-value="chooseVisibility"/></label>
  <section v-if="prefs.output==='rtss'" class="monitor-overview-rtss"><h3>RTSS 模板与大小</h3><SegButton :model-value="prefs.rtssTemplate" :options="RTSS_MONITOR_TEMPLATES" color="accent" full :disabled="loading||busy" @update:model-value="v=>{prefs.rtssTemplate=v as any;persist(true,true)}"/><Slider :model-value="prefs.rtssScale" :min="20" :max="240" :step="20" :accelerate="false" label="监控大小" unit="%" :disabled="loading||busy" @update:model-value="v=>rtssCommit(v,false)" @commit="rtssCommit"/></section>
  <p v-if="failed" class="monitor-overview-error" role="alert">{{status}}</p>
 </section>
 <template v-if="prefs.output==='ymcc'">
  <div class="monitor-overview-editor-bar">
   <button type="button" class="monitor-overview-entry" data-monitor-edit data-gp-row="3" data-gp-col="0" :disabled="loading||busy" @click="emit('edit')"><InlineIcon name="edit"/><span><strong>编辑自定义监控</strong><small>外观与显示数据</small></span></button>
   <div class="monitor-overview-template-switch" role="group" aria-label="选择监控模板">
    <button v-for="(option,index) in HUD_TEMPLATES" :key="option.value" type="button" class="monitor-overview-template" :class="{active:editingTemplate===option.value}" :data-monitor-overview-template="option.value" :aria-pressed="editingTemplate===option.value" :disabled="templateControlsDisabled" data-gp-row="3" :data-gp-col="index+1" @click="chooseTemplate(option.value)">{{editingTemplate===option.value?'当前模板':'模板'}}{{option.value}}</button>
   </div>
  </div>
  <section class="monitor-overview-preview" aria-label="当前监控预览">
   <header class="monitor-overview-preview-heading"><strong>当前监控预览</strong></header>
   <button type="button" class="monitor-overview-lock" :aria-pressed="prefs.ymcc.dragLocked===true" :disabled="loading||busy" data-gp-row="4" data-gp-col="0" @click="toggleDragLock"><InlineIcon :name="prefs.ymcc.dragLocked?'lock':'unlock'"/><span>{{prefs.ymcc.dragLocked?'拖动已锁定 · 点击解锁':'可拖动 · 点击锁定'}}</span></button>
   <div class="monitor-overview-preview-canvas"><MonitorHudPreview :appearance="prefs.ymcc" :fields="prefs.ymcc.fields" :data="previewData"/></div>
  </section>
 </template>
</div>
<div v-else ref="editorElement" class="monitor-output-settings sectioned-monitor monitor-header-layout monitor-template-workspace" :style="editorHeight?{height:editorHeight+'px'}:undefined"  :data-editor-variant="editorVariant" data-gp-scope="monitor" data-gp-spatial="nearest" :data-preview-only="previewOnly"><input ref="underlayInput" type="file" accept="image/png,image/jpeg,image/webp" hidden @change="importUnderlay"/><SettingsSections :layout-variant="editorVariant" :sections="prefs.output === 'ymcc' ? [{id:'output',title:'基础设置',description:'监控显隐与提示',icon:'monitor'},{id:'appearance',title:(prefs.ymccTemplate??'A')+'模板 外观',description:'位置、字体与背景',icon:'layers'},{id:'fields',title:(prefs.ymccTemplate??'A')+'模板 显示数据',description:'选择需要关注的指标',icon:'grid'}] : [{id:'output',title:'输出设置',description:'选择监控输出方式',icon:'monitor'}]" label="监控"><template #top><header class="monitor-compact-heading">
 <div class="monitor-compact-title"><h1>自定义监控</h1><span aria-live="polite">{{loading?'正在读取':templateSwitching?'正在切换':'正在编辑'}}<strong>{{editingTemplate}}模板</strong></span></div>
 <div class="monitor-compact-templates" role="group" aria-label="选择监控模板"><button v-for="option in HUD_TEMPLATES" :key="option.value" type="button" class="monitor-template-choice" :class="{active:editingTemplate===option.value}" :data-monitor-template="option.value" :aria-label="option.label" :aria-pressed="editingTemplate===option.value" :disabled="templateControlsDisabled" @click="chooseTemplate(option.value)">{{option.label}}<InlineIcon v-if="editingTemplate===option.value" name="check" /></button></div>
 <button type="button" class="monitor-editor-close" aria-label="返回" @click="emit('close')"><span class="monitor-back-key">B</span>返回</button>
</header>
 </template><template v-if="prefs.output==='ymcc'" #preview>
  <section class="monitor-live-preview" aria-label="当前模板实时预览">
   <div class="monitor-live-title"><span>监控预览</span><small>{{editorVariant==='a'?'实时显示':editorVariant==='b'?'横向预览':'实时显示'}}</small></div>
   <div class="monitor-preview-stage" :class="{'monitor-preview-original':previewOriginal}">
    <MonitorHudPreview v-if="prefs.enabled" :appearance="prefs.ymcc" :fields="prefs.ymcc.fields" :data="previewData" :original-size="previewOriginal" />
    <span v-else class="preview-disabled">监控已关闭</span>
   </div>
   <div class="monitor-preview-mode" role="group" aria-label="预览显示比例">
    <button type="button" :aria-pressed="!previewOriginal" data-preview-fit="fit" @click="previewOriginal=false">完整可见</button>
    <button type="button" :aria-pressed="previewOriginal" data-preview-fit="original" @click="previewOriginal=true">原始尺寸</button>
   </div>
   <div class="monitor-preview-controls"><button type="button" class="action-btn monitor-drag-lock" :class="{'monitor-drag-lock--locked':prefs.ymcc.dragLocked}" :aria-pressed="prefs.ymcc.dragLocked===true" :disabled="loading||busy" @click="toggleDragLock"><InlineIcon :name="prefs.ymcc.dragLocked?'lock':'unlock'"/>{{prefs.ymcc.dragLocked?'拖动已锁定 · 点击解锁':'可拖动 · 点击锁定'}}</button></div>
  </section>
 </template><template #output><section class="card output-card">
      <div class="monitor-output-head">
        <h3 class="card-title"><InlineIcon name="monitor" /> 监控输出</h3>
        <div class="monitor-output-toggles">
          <Toggle :model-value="prefs.enabled" label="显示监控" compact :disabled="loading || busy" :gp-row="0" :gp-col="0" @update:model-value="enabled" />

          <Toggle :model-value="prefs.popupEnabled" label="弹出提示" compact :disabled="loading || busy" :gp-row="0" :gp-col="1" @update:model-value="popupEnabled" />
          <Toggle :model-value="prefs.mouseStatusEnabled" label="鼠标状态" compact :disabled="loading || busy" :gp-row="0" :gp-col="2" @update:model-value="mouseStatusEnabled" />
        </div>
      </div>
      


      <label v-if="prefs.output === 'ymcc'" class="monitor-visibility-choice"><span>监控出现</span><Dropdown :model-value="prefs.ymcc.visibility ?? 'always'" :options="HUD_VISIBILITIES" aria-label="监控出现" width="164px" color="accent" :disabled="loading || busy" :gp-row="1" :gp-col="0" @update:model-value="chooseVisibility" /></label>
      <p v-if="failed" class="monitor-feedback monitor-feedback--failed" role="alert">{{ status }}</p>
    </section><div v-if="prefs.output==='rtss'"><section class="card rtss-output-card">
      <h3 class="card-title">RTSS 模板与大小</h3>
      <SegButton :model-value="prefs.rtssTemplate" :options="RTSS_MONITOR_TEMPLATES" color="accent" full :disabled="loading || busy" gp-row="2" @update:model-value="v => { prefs.rtssTemplate = v as any; persist(true, true); }" />
      <div class="monitor-slider-stack"><Slider :model-value="prefs.rtssScale" :min="20" :max="240" :step="20" :accelerate="false" label="监控大小" unit="%" :disabled="loading || busy" :gp-row="4" @update:model-value="v => rtssCommit(v, false)" @commit="rtssCommit" /></div>
    </section></div></template><template #appearance><section class="card appearance-card">
          <h3 class="card-title">{{ editingTemplate }}模板 外观</h3>
          <div class="monitor-select-grid">
            <div class="monitor-image-underlay monitor-image-underlay--top"><span class="monitor-underlay-label">图片背景</span><div class="monitor-underlay-actions"><Dropdown :model-value="prefs.ymcc.underlayPreset ?? 'none'" :options="HUD_UNDERLAY_PRESETS" aria-label="图片背景" width="100%" color="accent" :disabled="loading || busy || underlayBusy" :gp-row="2" :gp-col="0" @update:model-value="chooseUnderlay" /><button v-if="prefs.ymcc.underlayPreset === 'custom'" type="button" class="action-btn" :disabled="loading || busy || underlayBusy" data-gp-row="2" data-gp-col="1" @click="openUnderlayPicker">{{ underlayPicking ? '选择中…' : underlayImporting ? '导入中…' : '选择图片' }}</button></div><span v-if="underlayError" class="monitor-underlay-error" role="alert">{{ underlayError }}</span></div>
            <label><span>监控样式</span><Dropdown :model-value="prefs.ymcc.layout" :options="HUD_LAYOUTS" aria-label="监控样式" width="100%" color="accent" :disabled="loading || busy" :gp-row="2" :gp-col="2" @update:model-value="chooseHudLayout" /></label>
          </div>
          <label v-if="prefs.output === 'ymcc'" class="monitor-crosshair-choice"><span class="monitor-crosshair-label">屏幕准星<span class="monitor-crosshair-swatch"><MonitorCrosshair :kind="prefs.ymcc.crosshair ?? 'none'" /></span></span><Dropdown :model-value="prefs.ymcc.crosshair ?? 'none'" :options="HUD_CROSSHAIRS" aria-label="屏幕准星" width="160px" color="accent" :disabled="loading || busy" :gp-row="3" :gp-col="0" @update:model-value="v => { prefs.ymcc.crosshair = HUD_CROSSHAIRS.find(item => item.value === v)?.value ?? 'none'; persist(false, true); }" /></label>
          <div class="monitor-fps-emphasis" v-if="prefs.output === 'ymcc'"><Toggle :model-value="prefs.ymcc.fpsEmphasis !== false" label="FPS 突出显示" :disabled="loading || busy" :gp-row="4" :gp-col="0" @update:model-value="fpsEmphasis" /></div>
          <div v-if="prefs.output === 'ymcc'" class="monitor-font-grid">
            <label><span>正文字体</span><Dropdown :model-value="prefs.ymcc.font ?? 'system'" :options="HUD_FONTS" aria-label="正文字体" width="100%" :disabled="loading || busy" :gp-row="5" :gp-col="0" @update:model-value="v => chooseFont('font', v)" /></label>
            <label><span>FPS 大字字体</span><Dropdown :model-value="prefs.ymcc.fpsFont ?? 'inherit'" :options="HUD_FPS_FONTS" aria-label="FPS 大字字体" width="100%" :disabled="loading || busy" :gp-row="6" :gp-col="0" @update:model-value="v => chooseFont('fpsFont', v)" /></label>
          </div>
          <div class="monitor-hardware-names"><Toggle :model-value="prefs.ymcc.showCpuName===true" label="CPU 显示设备名称" :disabled="loading || busy" :gp-row="7" :gp-col="0" @update:model-value="v=>toggleHardwareName('showCpuName',v)" /><Toggle :model-value="prefs.ymcc.showGpuName===true" label="GPU 显示设备名称" :disabled="loading || busy" :gp-row="7" :gp-col="1" @update:model-value="v=>toggleHardwareName('showGpuName',v)" /></div>
          <div class="monitor-slider-stack" v-if="prefs.output === 'ymcc'">
            <Slider :model-value="Math.round((prefs.ymcc.textScale ?? 1.2)*100)" :min="50" :max="200" :step="5" :accelerate="false" label="监控文字倍率" unit="%" :gp-row="7.5" :disabled="loading || busy" @update:model-value="v=>appearance('textScale',v/100)" @commit="v=>appearance('textScale',v/100,true)" />
            <Slider :model-value="Math.round((1 - (prefs.ymcc.underlayOpacity ?? .28)) * 100)" :min="0" :max="100" :step="1" :accelerate="false" label="图片透明度" unit="%" :gp-row="8" :disabled="loading || busy" @update:model-value="v => appearance('underlayOpacity', 1 - v / 100)" @commit="v => appearance('underlayOpacity', 1 - v / 100, true)" />
            <Slider :model-value="Math.round(prefs.ymcc.scale * 100)" :min="20" :max="200" :step="5" :accelerate="false" label="监控整体大小" unit="%" :gp-row="9" :disabled="loading || busy" @update:model-value="v => appearance('scale', v / 100)" @commit="v => appearance('scale', v / 100, true)" />
            <Slider :model-value="Math.round(prefs.ymcc.saturation * 100)" :min="0" :max="100" :step="5" :accelerate="false" label="文字对比度/饱和度" unit="%" :gp-row="10" :disabled="loading || busy" @update:model-value="v => appearance('saturation', v / 100)" @commit="v => appearance('saturation', v / 100, true)" />
            <Slider :model-value="Math.round(prefs.ymcc.opacity * 100)" :min="0" :max="100" :step="1" :accelerate="false" label="背景不透明度" unit="%" :gp-row="11" :disabled="loading || busy" @update:model-value="v => appearance('opacity', v / 100)" @commit="v => appearance('opacity', v / 100, true)" />
            <component :is="editorVariant==='a'?'section':'details'" class="monitor-refinements" aria-label="细节外观"><h4 v-if="editorVariant==='a'" class="monitor-refinements-title">细节外观</h4><summary v-else tabindex="0" aria-label="细化外观">细化外观</summary><Slider :model-value="prefs.ymcc.radius" :min="0" :max="18" :step="1" label="圆角" unit="px" :gp-row="12" @update:model-value="v => appearance('radius', v)" @commit="v => appearance('radius', v, true)" /><Slider :model-value="prefs.ymcc.gap" :min="4" :max="16" :step="1" label="内容间距" unit="px" :gp-row="13" @update:model-value="v => appearance('gap', v)" @commit="v => appearance('gap', v, true)" /></component>
          </div>
        </section></template><template #fields-actions><div class="monitor-field-header-actions">
      <button type="button" class="monitor-fields-reset" :disabled="templateControlsDisabled" data-gp-row="13" data-gp-col="0" :aria-label="'重置模板'+editingTemplate" :title="resetPresetTitle" @click="resetTemplate">{{editorVariant==='a'?'重置模板'+editingTemplate:'重置'}}</button>
      <button type="button" class="monitor-fields-hide" :class="{active:prefs.ymcc.hideUnavailable === true}" :aria-pressed="prefs.ymcc.hideUnavailable === true" :disabled="loading || busy" data-gp-row="13" data-gp-col="1" aria-label="无数据自动隐藏" @click="hideUnavailable(prefs.ymcc.hideUnavailable !== true)">无数据自动隐藏</button>
      <span class="monitor-field-total" :title="`${selectedMetrics.length} / ${HUD_METRICS.length} 已选`" :aria-label="`${selectedMetrics.length} / ${HUD_METRICS.length} 已选`">{{selectedMetrics.length}} / {{HUD_METRICS.length}}</span>
    </div></template><template #fields><section v-if="prefs.output === 'ymcc'" class="card fields-card">
      <div class="monitor-field-tabs" :style="{'--field-category-count':metricGroups.length}" role="tablist" aria-label="数据分区" aria-orientation="horizontal" :data-gp-pane="editorVariant==='a'?'field-categories':undefined">
        <button v-for="(group, gi) in metricGroups" :key="group.name" type="button" class="monitor-field-tab" role="tab" :id="'monitor-field-tab-' + gi" :aria-controls="'monitor-field-panel-' + gi" :aria-selected="activeFieldGroup === group.name" tabindex="0" :data-gp-row="editorVariant==='a'?14:15+Math.floor(gi/FIELD_CATEGORY_COLUMNS)" :data-gp-col="editorVariant==='a'?gi:gi%FIELD_CATEGORY_COLUMNS" @click="activeFieldGroup = group.name">
          <span>{{ groupLabel(group.name) }}</span><small>{{ group.metrics.filter(m => prefs.ymcc.fields[m.id]).length }} / {{ group.metrics.length }}</small>
        </button>
      </div>
      <div class="monitor-fields-head"><h3 class="card-title">自定义数据</h3><div class="monitor-fields-actions"><div class="monitor-data-preset-slider">
        <Slider :model-value="dataPresetStep" :min="0" :max="dataPresetOptions.length - 1" :step="1" :accelerate="false" label="数据预设" :value-text="dataPresetLabel" :disabled="loading || busy" :gp-row="15" :gp-col="0" @update:model-value="chooseDataPresetStep" />
        <div class="monitor-preset-ticks" aria-hidden="true"><span v-for="(option,index) in dataPresetOptions" :key="option.value" :class="{active:index===dataPresetStep}" :style="{left:(index / (dataPresetOptions.length - 1) * 100)+'%'}">{{option.label}}</span></div>
      </div></div></div>
      <div class="monitor-fields-workbench">

      <div class="monitor-field-details" :data-gp-pane="editorVariant==='a'?'field-metrics':undefined">
      <div class="monitor-field-panels">
        <section v-for="(group, gi) in metricGroups" :key="group.name" v-show="activeFieldGroup === group.name" class="monitor-field-panel" role="tabpanel" :id="'monitor-field-panel-' + gi" :aria-labelledby="'monitor-field-tab-' + gi" :data-field-group="group.name">
          <div class="monitor-field-grid" :data-grid-category="group.name" :style="{'--metric-columns':fieldColumns(group.name),'--metric-rows':Math.ceil(group.metrics.length/fieldColumns(group.name))}">
            <div v-for="(metric, mi) in group.metrics" :key="metric.id" :data-field="metric.id" :data-interface-ready="metric.ready" :title="metric.id === 'tdpLimit' ? '当前 AC/DC 下 YMCC 后端确认下发的 TDP 上限（包括浮动临时值）；非保存最大值、非 HWiNFO 功耗，也不是持续硬件回读。切换/失败/未确认显示 —' : (metric.id === 'fanSpeed' || metric.id === 'fanSpeedBar') ? 'YMCC 当前已开启控制下的期望转速百分比，非 RPM；设备不支持或回执未知时无读数。图形和数字共用统一来源。' : metric.ready ? '可映射现有 YMCC 统一接口' : '扩展字段：真实统一接口待扩展'">
              <Toggle :aria-label="group.name + ' · ' + hudMetricControlLabel(metric)" :model-value="prefs.ymcc.fields[metric.id]" :label="hudMetricControlLabel(metric)" compact :disabled="loading || busy" :gp-row="editorVariant==='a'?16+Math.floor(mi/fieldColumns(group.name))*2:15+Math.floor(mi/2)" :gp-col="editorVariant==='a'?mi%fieldColumns(group.name):FIELD_CATEGORY_COLUMNS+mi%2" @update:model-value="v => field(metric.id, v)" />
              <div v-if="hasNumericText(metric) && prefs.ymcc.fields[metric.id]" class="monitor-value-scaling"><Slider :model-value="Math.round((prefs.ymcc.valueScales?.[metric.id]??1)*100)" :min="50" :max="300" :step="5" :accelerate="false" label="数值大小" :aria-label="metric.label+' 数值倍率'" unit="%" :disabled="loading || busy" :gp-row="editorVariant==='a'?17+Math.floor(mi/fieldColumns(group.name))*2:15.5+Math.floor(mi/2)" :gp-col="editorVariant==='a'?mi%fieldColumns(group.name):FIELD_CATEGORY_COLUMNS+mi%2" @update:model-value="v=>valueScale(metric.id,v)" @commit="v=>valueScale(metric.id,v,true)" /></div>
            </div>
          </div>
        </section>
      </div>
      </div>
      </div>
    </section></template></SettingsSections></div>
</template><style scoped>
.monitor-preview-controls{display:grid;gap:8px;margin-top:10px}.monitor-preview-controls .monitor-note{margin:0}.monitor-drag-lock{display:flex;align-items:center;justify-content:center;gap:6px;min-height:32px;width:100%}.monitor-drag-lock--locked{border-color:var(--accent)}

.monitor-output-toggles { display:flex;align-items:center;justify-content:flex-end;gap:16px;flex-wrap:wrap;min-width:0; }

.monitor-output-settings { container:monitor-output / inline-size;display:flex;flex-direction:column;gap:10px;padding-bottom:10px; }.monitor-output-settings>.card,.monitor-workbench>.card { margin-bottom:0; }.monitor-workbench { min-width:0; }.monitor-workbench--ymcc { display:grid;grid-template-columns:minmax(0,3fr) minmax(0,7fr);gap:10px;align-items:start; }.card-title { display:flex;align-items:center;gap:6px;font-size:13px;font-weight:600;margin:0 0 10px; }.monitor-output-head,.monitor-preview-head,.monitor-fields-head,.output-apply-row { display:flex;align-items:center;justify-content:space-between;gap:10px; }.monitor-fields-actions { display:flex;align-items:center;gap:10px;flex-wrap:wrap;min-width:0; } .preset-reset-btn { white-space:nowrap;padding:6px 8px;font-size:11px; }.monitor-fields-actions :deep(.seg) { min-width:180px; }.monitor-output-head { margin-bottom:10px; }.monitor-output-head .card-title,.monitor-preview-head .card-title,.monitor-fields-head .card-title { margin:0; }.output-apply-row { margin-top:8px; }.monitor-note,.monitor-feedback { color:var(--text-dim);font-size:11px;line-height:1.55;margin:8px 0 0; }.monitor-feedback--failed { color:var(--accent-2); }.output-apply-row .monitor-note { margin:0; }.action-btn { color:var(--text);background:var(--bg-input);border:1px solid var(--border);border-radius:var(--radius-ctrl);font-size:12px;padding:7px 10px;cursor:pointer; }.action-btn { color:var(--accent); }.action-btn:disabled { opacity:.45;cursor:default; }
.monitor-select-grid { display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:10px; }.monitor-workbench--ymcc .monitor-select-grid { grid-template-columns:minmax(0,1fr); }.monitor-select-grid label { display:flex;flex-direction:column;gap:7px;min-width:0;font-size:12px; }.monitor-slider-stack { display:grid;gap:10px;margin-top:14px; }.monitor-refinements summary { color:var(--text-dim);font-size:12px;cursor:pointer; }.monitor-refinements .slider-wrap { margin-top:10px; }.appearance-card { min-width:0; }.preview-card { min-width:0; }.monitor-preview-stage { height:clamp(160px,24vh,280px);min-height:128px;box-sizing:border-box;padding:10px;margin-top:10px;overflow:hidden;position:relative;border-radius:8px;background-color:#18222e;background-image:linear-gradient(45deg,rgba(225,233,242,.035) 25%,transparent 25%,transparent 75%,rgba(225,233,242,.035) 75%),linear-gradient(45deg,rgba(225,233,242,.035) 25%,transparent 25%,transparent 75%,rgba(225,233,242,.035) 75%);background-size:24px 24px;background-position:0 0,12px 12px; }
/* Fit is confined to the preview; saved output zoom stays independent. */
.preview-disabled { display:block;padding:56px 10px;text-align:center;font-size:12px;color:#e9eef5; }
.monitor-fields-workbench { display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:12px;align-items:start;margin-top:10px; }.monitor-field-tabs { display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:6px;min-width:0;padding:0; }.monitor-field-details { min-width:0; }.monitor-field-tab { display:flex;align-items:center;justify-content:space-between;gap:6px;min-height:32px;padding:6px 8px;border:1px solid rgba(139,156,178,.18);border-radius:7px;background:var(--bg-input);color:var(--text);font-size:12px;cursor:pointer; }.monitor-field-tab small { color:var(--text-dim);font-size:10px;white-space:nowrap; }.monitor-field-tab[aria-selected=true] { border-color:var(--accent);background:color-mix(in srgb,var(--accent) 13%,var(--bg-input)); }.monitor-field-tab[aria-selected=true] small { color:var(--accent); }.monitor-field-tab:focus-visible { outline:2px solid var(--accent);outline-offset:2px; }.monitor-field-panel { min-height:108px;padding:0 0 4px; }.monitor-field-grid { display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px 16px; }.monitor-field-grid>div { min-width:0;padding:5px 0; }.monitor-field-total { font-size:11px;color:var(--text-dim);white-space:nowrap; }
.monitor-image-underlay { display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:8px;margin:8px 0; }
.monitor-crosshair-label { display:flex;align-items:center;gap:8px; }.monitor-crosshair-swatch { position:relative;display:inline-block;width:24px;height:24px;pointer-events:none; }.monitor-crosshair-choice { display:flex;align-items:center;justify-content:space-between;gap:8px;margin:10px 0;font-size:12px; }.monitor-underlay-label { font-size:12px; }.monitor-underlay-actions { display:flex;gap:6px;flex-wrap:wrap;justify-content:flex-end;min-width:0; }
.monitor-underlay-error { width:100%;font-size:12px;color:var(--danger,#f18d8d); }
.monitor-fps-emphasis { margin:8px 0 4px; }
/* Bar only: preview first; preserve the saved 100% output scale and apply the preview-only 80% wrapper. */
.monitor-workbench--ymcc.monitor-workbench--bar { grid-template-columns:minmax(0,1fr); }
.monitor-workbench--bar .monitor-select-grid { grid-template-columns:repeat(2,minmax(0,1fr)); }
.monitor-workbench--bar .monitor-slider-stack { grid-template-columns:repeat(3,minmax(0,1fr));align-items:start; }
.monitor-workbench--bar .monitor-refinements { grid-column:1/-1; }
.monitor-workbench--bar .preview-anchor { width:100%; }
.monitor-workbench--bar .preview-anchor :deep(.ymcc-hud--horizontal) { max-width:100%; }
@container monitor-output (max-width:600px) { .monitor-workbench--ymcc { grid-template-columns:minmax(0,1fr); }.monitor-field-grid { grid-template-columns:repeat(2,minmax(0,1fr));gap:10px 16px; }.monitor-fields-workbench { grid-template-columns:minmax(0,1fr) minmax(0,1fr);gap:8px; } }
/* A narrow host reflows this workbench, never the shared YMCC shell zoom. */
.monitor-output-settings, .output-card, .fields-card { min-width:0;max-width:100%; }
.output-apply-row .monitor-note { min-width:0;flex:1;overflow-wrap:anywhere; }
.output-apply-row .action-btn { flex-shrink:0; }
.monitor-preview-head, .monitor-fields-head { flex-wrap:wrap; }
.monitor-field-tab > span { min-width:0;overflow-wrap:anywhere; }
@container monitor-output (max-width:600px) {
  .monitor-workbench--ymcc .monitor-select-grid { grid-template-columns:repeat(2,minmax(0,1fr)); }
  .monitor-workbench--ymcc .monitor-slider-stack { grid-template-columns:repeat(2,minmax(0,1fr));gap:10px; }
  .monitor-workbench--ymcc .monitor-refinements { grid-column:1/-1; }
}
@container monitor-output (max-width:520px) {
  .monitor-field-grid { grid-template-columns:minmax(0,1fr);gap:4px; }
  .monitor-field-tab { gap:4px;padding:6px; }
}
/* Vertical HUDs need the height beside the appearance controls. The two
   cards share their top/bottom edges; only the preview content is fitted. */
.monitor-workbench--vertical { align-items:stretch; }
.monitor-workbench--vertical .preview-card { display:flex;flex-direction:column; }
.monitor-workbench--vertical .monitor-preview-stage { height:auto;min-height:520px;flex:1; }
@container monitor-output (max-width:600px) {
  .monitor-workbench--vertical .monitor-preview-stage { min-height:560px; }
}
.monitor-font-grid { display:grid;gap:10px;margin:12px 0; }
.monitor-font-grid label { display:flex;flex-direction:column;gap:6px;font-size:12px;min-width:0; }
.monitor-output-toggles :deep(.monitor-toggle),.monitor-fields-actions :deep(.monitor-toggle) { width:auto; }
.monitor-visibility-choice { display:flex;align-items:center;gap:8px;margin:0;font-size:12px;white-space:nowrap; }
.monitor-visibility-choice :deep(.dd-trigger) { min-height:30px;padding-top:5px;padding-bottom:5px; }
@container monitor-output (max-width:700px) {
  .monitor-output-head { flex-wrap:wrap; }
  .monitor-output-toggles { width:100%;justify-content:space-between;gap:10px; }
}
@container monitor-output (max-width:440px) {
  .monitor-output-toggles { justify-content:flex-start; }
}
.monitor-image-underlay--top { display: flex; flex-direction: column; align-items: stretch; justify-content: flex-start; gap: 7px; margin: 0; min-width: 0; font-size: 13px; }
.monitor-image-underlay--top .monitor-underlay-actions { min-width: 0; flex: 0; flex-wrap: nowrap; }
.monitor-image-underlay--top .monitor-underlay-actions > :first-child { flex: 1; min-width: 0; }
.monitor-output-settings.sectioned-monitor .monitor-image-underlay--top .monitor-underlay-label { font-size: 13px !important; }
</style>
<style>
/* Existing monitor controls retain their real data and handlers. */
.monitor-output-settings.sectioned-monitor{container-type:inline-size}.monitor-output-settings.sectioned-monitor .settings-section-panel>.card,.monitor-output-settings.sectioned-monitor .settings-section-panel>div>.card{border:0!important;box-shadow:none!important;background:transparent!important;border-radius:0!important;padding:12px 0!important;margin:0!important;max-width:100%;min-width:0}.monitor-output-settings.sectioned-monitor .settings-section-panel .card-title{font-size:14px!important;margin:0 0 10px!important}.monitor-output-settings.sectioned-monitor .settings-section-panel .appearance-card>.card-title,.monitor-output-settings.sectioned-monitor .settings-section-panel .fields-card .card-title,.monitor-output-settings.sectioned-monitor .output-card .card-title{display:none!important}
.monitor-output-settings.sectioned-monitor .monitor-output-head{display:block}.monitor-output-settings.sectioned-monitor .monitor-output-toggles{display:flex!important;flex-direction:column!important;align-items:stretch!important;gap:0!important;width:100%}.monitor-output-settings.sectioned-monitor .monitor-output-toggles>.toggle-row{width:100%!important;min-height:46px;padding:8px 0!important;border-bottom:1px solid rgba(146,169,198,.12)}.monitor-output-settings.sectioned-monitor .monitor-visibility-choice{display:flex;justify-content:space-between;min-height:49px;white-space:normal;font-size:14px;gap:8px}.monitor-output-settings.sectioned-monitor .monitor-visibility-choice .dd{max-width:58%}.monitor-output-settings.sectioned-monitor .monitor-output-toggles .toggle-row .toggle-label{font-size:14px!important}.monitor-output-settings.sectioned-monitor .output-card>.seg{margin-top:10px}.monitor-output-settings.sectioned-monitor .monitor-preview-stage{height:210px!important;min-height:0!important;background:radial-gradient(ellipse at 60% 40%,color-mix(in srgb,var(--accent) 8%,#101c2b),#0b111a)!important;overflow:auto!important;border-radius:10px!important}.monitor-output-settings.sectioned-monitor .preview-anchor{min-width:0!important;max-width:100%}.monitor-output-settings.sectioned-monitor .preview-card{border-top:1px solid rgba(146,169,198,.15)!important}.monitor-output-settings.sectioned-monitor .monitor-select-grid{grid-template-columns:repeat(2,minmax(0,1fr))!important;gap:9px}.monitor-output-settings.sectioned-monitor .monitor-select-grid label,.monitor-output-settings.sectioned-monitor .monitor-font-grid label{font-size:13px!important}.monitor-output-settings.sectioned-monitor .monitor-crosshair-choice,.monitor-output-settings.sectioned-monitor .monitor-underlay-label{font-size:14px!important}.monitor-output-settings.sectioned-monitor .monitor-slider-stack{display:grid!important;grid-template-columns:minmax(0,1fr)!important;gap:9px!important;margin-top:14px}.monitor-output-settings.sectioned-monitor .monitor-refinements{grid-column:1/-1}.monitor-output-settings.sectioned-monitor .monitor-fields-head{display:block}.monitor-output-settings.sectioned-monitor .monitor-fields-actions{display:flex;flex-direction:column;align-items:stretch;gap:8px}.monitor-output-settings.sectioned-monitor .monitor-fields-presets{display:flex;flex-wrap:wrap;gap:5px}.monitor-output-settings.sectioned-monitor .monitor-data-preset{font-size:13px!important;min-height:35px!important}.monitor-output-settings.sectioned-monitor .monitor-fields-workbench{display:block!important}.monitor-output-settings.sectioned-monitor .monitor-field-tabs{grid-template-columns:repeat(3,minmax(0,1fr))!important;gap:5px;margin-bottom:12px}.monitor-output-settings.sectioned-monitor .monitor-field-tab{font-size:13px!important;min-height:37px!important;padding:7px 6px!important}.monitor-output-settings.sectioned-monitor .monitor-field-tab small{display:none}.monitor-output-settings.sectioned-monitor .monitor-field-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr))!important;gap:8px 10px!important;align-items:stretch}
.monitor-output-settings.sectioned-monitor .monitor-field-grid>div{min-width:0;display:flex;align-items:stretch;padding:6px 8px!important;border:1px solid var(--console-line);border-radius:8px;background:var(--console-surface-alt)}
.monitor-output-settings.sectioned-monitor .monitor-field-grid .toggle-row{width:100%!important;min-height:48px;gap:8px!important;padding:4px 0!important;align-items:center}
.monitor-output-settings.sectioned-monitor .monitor-field-grid .toggle-label{font-size:14px!important;line-height:1.4;overflow-wrap:anywhere}
.monitor-output-settings.sectioned-monitor .monitor-field-grid .ymcc-switch{flex-shrink:0}
.monitor-output-settings.sectioned-monitor .monitor-field-tabs{grid-template-columns:repeat(4,minmax(0,1fr))!important}
.monitor-output-settings.sectioned-monitor .monitor-field-tab{justify-content:center}
.monitor-output-settings.sectioned-monitor .monitor-field-tab>span{white-space:nowrap}
.monitor-output-settings.sectioned-monitor .monitor-slider-stack{grid-template-columns:repeat(2,minmax(0,1fr))!important;column-gap:14px!important;row-gap:16px!important}
.monitor-output-settings.sectioned-monitor .monitor-font-grid{grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}
.monitor-output-settings.sectioned-monitor .monitor-refinements .slider{margin-top:12px}



.monitor-output-settings.sectioned-monitor .settings-section-heading p{display:none}
.monitor-output-settings.sectioned-monitor .monitor-fields-actions{display:grid;grid-template-columns:auto minmax(0,1fr) auto;column-gap:10px;row-gap:10px;align-items:center}
.monitor-output-settings.sectioned-monitor .monitor-fields-presets{grid-column:1/-1;width:100%;min-width:0;display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:7px}
.monitor-output-settings.sectioned-monitor .monitor-fields-presets .seg-btn{min-width:0;padding:6px;white-space:nowrap}
.monitor-output-settings.sectioned-monitor .monitor-fields-actions>.toggle-row{min-height:38px;padding:4px 0!important}
.monitor-output-settings.sectioned-monitor .monitor-field-total{justify-self:end}
</style>

<style scoped>
.monitor-header-layout .monitor-page-heading{display:grid;grid-template-columns:auto minmax(0,1fr);align-items:center;gap:16px;padding:3px 1px 0;margin:0;min-width:0}
.monitor-header-layout .monitor-page-heading h1{font-size:20px;line-height:1.2;margin:0;font-weight:650;white-space:nowrap}
.monitor-header-layout .monitor-page-heading :deep(.seg){width:100%;min-width:0;display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}
.monitor-header-layout .monitor-page-heading :deep(.seg-btn){min-width:0;min-height:38px;font-size:14px;padding:7px 8px;white-space:nowrap}
.monitor-header-layout .output-card>.monitor-visibility-choice{min-height:49px;padding-top:8px;box-sizing:border-box}
</style>

<style scoped>
.monitor-header-layout .monitor-field-header-actions{display:flex;align-items:center;gap:7px;min-width:0;white-space:nowrap}
.monitor-header-layout .monitor-field-header-actions>button{min-height:34px;padding:6px 9px;border:1px solid var(--console-line);border-radius:7px;background:var(--console-surface-alt);color:var(--text);font:inherit;font-size:13px;font-weight:650;line-height:1.4;white-space:nowrap;cursor:pointer}
.monitor-header-layout .monitor-field-header-actions>.monitor-fields-reset{color:var(--danger);border-color:color-mix(in srgb,var(--danger) 50%,transparent);background:color-mix(in srgb,var(--danger) 12%,var(--bg-input))}
.monitor-header-layout .monitor-field-header-actions>.monitor-fields-hide.active{color:var(--accent);border-color:var(--accent);background:var(--accent-surface)}
.monitor-header-layout .monitor-field-header-actions>button:disabled{opacity:.45;cursor:default}
.monitor-header-layout .monitor-field-header-actions>button:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
.monitor-header-layout .monitor-field-header-actions .monitor-field-total{font-size:12px;font-variant-numeric:tabular-nums;color:var(--text-dim)}
</style>

<style scoped>
.monitor-header-layout .monitor-data-preset-slider{grid-column:1 / -1;min-width:0;padding:0 2px 5px}
.monitor-header-layout .monitor-data-preset-slider :deep(.slider){padding:0}
.monitor-header-layout .monitor-data-preset-slider :deep(.slider-head){margin-bottom:9px}
.monitor-header-layout .monitor-data-preset-slider :deep(.slider-label){font-size:14px;color:var(--text)}
.monitor-header-layout .monitor-data-preset-slider :deep(.slider-val){font-size:14px;font-weight:650}
.monitor-header-layout .monitor-preset-ticks{position:relative;height:25px;margin-top:5px;font-size:12px;line-height:1.4;color:var(--text-dim)}
.monitor-header-layout .monitor-preset-ticks>span{position:absolute;top:0;transform:translateX(-50%);white-space:nowrap}
.monitor-header-layout .monitor-preset-ticks>span:first-child{transform:none}
.monitor-header-layout .monitor-preset-ticks>span:last-child{transform:translateX(-100%)}
.monitor-header-layout .monitor-preset-ticks>span.active{color:var(--accent);font-weight:650}
</style>

<style scoped>
/* Template identity belongs above all monitor editing sections and stays visible while scrolling. */
.monitor-template-workspace{min-height:0;box-sizing:border-box}
.monitor-template-workspace :deep(.settings-sections){height:100%;min-height:0;display:flex;flex-direction:column;gap:10px}
.monitor-template-workspace :deep(.settings-sections-top){flex:0 0 auto;margin:0}
.monitor-template-workspace :deep(.settings-section-tabs){flex:0 0 auto;margin:0}
.monitor-template-workspace :deep(.settings-section-panel:not([hidden])){flex:1 1 0;min-height:0;overflow-y:auto;scrollbar-gutter:stable;overscroll-behavior:contain;scroll-padding:14px 0 28px}

.monitor-header-layout .monitor-template-context{display:grid;grid-template-columns:minmax(92px,.8fr) minmax(0,2fr);gap:12px;align-items:center;padding:10px 12px;border:1px solid var(--accent-border);border-radius:10px;background:var(--console-surface);margin:0;min-width:0}
.monitor-header-layout .monitor-template-current{display:flex;flex-direction:column;gap:3px;min-width:0}
.monitor-header-layout .monitor-template-current>span{font-size:12px;line-height:1.3;color:var(--text-dim)}
.monitor-header-layout .monitor-template-current>strong{font-size:17px;line-height:1.3;font-weight:700;color:var(--accent);white-space:nowrap}
.monitor-header-layout .monitor-template-choices{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;min-width:0}
.monitor-header-layout .monitor-template-choice{display:flex;align-items:center;justify-content:center;gap:6px;min-height:42px;padding:7px 8px;border:1px solid var(--console-line);border-radius:8px;background:var(--console-surface-alt);color:var(--text-dim);font:inherit;font-size:14px;font-weight:650;white-space:nowrap;cursor:pointer}
.monitor-header-layout .monitor-template-choice.active{border-color:var(--accent);background:var(--accent-surface);color:var(--accent)}
.monitor-header-layout .monitor-template-choice svg{width:16px;height:16px;flex:0 0 auto}
.monitor-header-layout .monitor-template-choice:disabled{opacity:.45;cursor:default}
.monitor-header-layout .monitor-template-choice:focus-visible,.monitor-header-layout .monitor-template-choice.focused{outline:2px solid var(--accent);outline-offset:2px}
.monitor-header-layout .monitor-template-context[aria-busy=true] .monitor-template-current{opacity:.7}
</style>

<style scoped>

/* Monitor workbench: retain the shared shell scale, widen the page, and scroll only the editor. */
.monitor-template-workspace :deep(.settings-section-workspace){display:grid!important;grid-template-columns:minmax(230px,.9fr) minmax(0,1.1fr);gap:14px;flex:1;min-height:0;overflow:hidden}
.monitor-template-workspace :deep(.settings-section-preview){display:flex;min-height:0;min-width:0}
.monitor-template-workspace :deep(.settings-section-editor){display:flex;flex-direction:column;min-height:0;min-width:0}
.monitor-template-workspace :deep(.settings-section-tabs){flex-shrink:0;margin:0 0 8px!important}
.monitor-template-workspace :deep(.settings-section-tabs button){font-size:14px!important;min-height:38px;padding:7px 4px}
.monitor-template-workspace :deep(.settings-section-panel:not([hidden])){flex:1;min-height:0;overflow-y:auto;overflow-x:hidden;scrollbar-gutter:stable;padding:12px!important;scroll-padding:10px}
.monitor-template-workspace .monitor-template-context{grid-template-columns:1fr 1fr;gap:14px;padding:10px 12px;margin-bottom:0}
.monitor-template-workspace .monitor-template-current{flex-direction:row;align-items:center;gap:12px}
.monitor-template-workspace .monitor-template-current strong{font-size:18px}
.monitor-live-preview{display:flex;flex-direction:column;flex:1;min-height:0;min-width:0;padding:12px;border:1px solid var(--border);border-radius:12px;background:var(--console-surface)}
.monitor-live-title{display:flex;align-items:center;justify-content:space-between;font-size:14px;color:var(--text-dim);gap:8px;margin-bottom:10px}
.monitor-live-title strong{color:var(--accent);font-size:15px}
.monitor-template-workspace .monitor-live-preview .monitor-preview-stage{display:flex;flex:1;min-height:100px!important;height:auto!important;max-height:none!important;padding:0!important;margin:0 0 10px!important;overflow:hidden!important;background:radial-gradient(ellipse at 30% 30%,color-mix(in srgb,var(--accent) 15%,#172738),#0a111b)!important}
.monitor-template-workspace .monitor-live-preview .monitor-preview-stage.monitor-preview-original{overflow:auto!important}
.monitor-preview-mode{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:7px}
.monitor-preview-mode button{font:inherit;font-size:14px;color:var(--text-dim);background:var(--bg-input);border:1px solid var(--border);border-radius:8px;min-height:36px;cursor:pointer}
.monitor-preview-mode button[aria-pressed=true]{color:var(--accent);border-color:var(--accent);background:var(--accent-surface)}
.monitor-live-preview .monitor-preview-controls{margin-top:8px}
.monitor-template-workspace .monitor-output-toggles{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px;width:100%}
.monitor-template-workspace .monitor-hardware-names{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;margin-top:10px}
.monitor-template-workspace .monitor-hardware-names :deep(.toggle-label){font-size:13px}
.monitor-template-workspace .monitor-font-grid{display:grid!important;grid-template-columns:repeat(2,minmax(0,1fr))!important;gap:8px!important}
.monitor-template-workspace .monitor-slider-stack{display:grid!important;grid-template-columns:repeat(2,minmax(0,1fr))!important;gap:10px!important}
.monitor-template-workspace .monitor-select-grid{grid-template-columns:repeat(2,minmax(0,1fr))!important}
.monitor-template-workspace .monitor-select-grid>label:first-child{grid-column:1/-1}
.monitor-template-workspace .monitor-fields-workbench{display:block!important}
.monitor-template-workspace .monitor-field-grid{grid-template-columns:repeat(2,minmax(0,1fr))!important;gap:10px!important}
.monitor-template-workspace .monitor-field-grid>[data-field]{min-width:0;padding:7px;border:1px solid color-mix(in srgb,var(--accent) 15%,var(--border));border-radius:8px;background:var(--console-surface-alt)}
.monitor-value-scaling{margin-top:8px}
.monitor-template-workspace .monitor-refinements{grid-column:1/-1}
.monitor-template-workspace :deep(.slider-header){font-size:13px}
.monitor-template-workspace :deep(.settings-section-heading p){display:none}
.monitor-template-workspace .monitor-fields-hide{white-space:normal}
@container monitor-output (max-width:620px){
 .monitor-template-workspace :deep(.settings-section-workspace){grid-template-columns:1fr}
 .monitor-template-workspace :deep(.settings-section-preview){display:none}
}

</style>

<style scoped>
.monitor-output-settings.monitor-template-workspace .monitor-output-toggles{display:grid!important;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px!important}
.monitor-output-settings.monitor-template-workspace .monitor-field-grid>[data-field]{display:flex!important;flex-direction:column!important;align-items:stretch!important;min-width:0}
.monitor-output-settings.monitor-template-workspace .monitor-value-scaling{width:100%;min-width:0}
.monitor-output-settings.monitor-template-workspace .monitor-value-scaling :deep(.slider-label){font-size:13px;white-space:nowrap}
.monitor-output-settings.monitor-template-workspace .monitor-value-scaling :deep(.slider-head){gap:4px}
</style>

<style scoped>
.monitor-template-workspace.monitor-output-settings{padding:0;gap:10px;font-size:16px}
.monitor-template-workspace.monitor-output-settings .monitor-page-heading{grid-template-columns:1fr minmax(300px,430px) auto;gap:14px;margin-bottom:10px}
.monitor-template-workspace.monitor-output-settings .monitor-page-heading h1{font-size:22px}
.monitor-template-workspace.monitor-output-settings .monitor-page-heading :deep(.seg-btn){font-size:16px;min-height:40px}
.monitor-editor-close{display:flex;align-items:center;justify-content:center;gap:8px;padding:0 16px;min-height:40px;border:1px solid color-mix(in srgb,var(--accent) 52%,transparent);border-radius:var(--radius-ctrl);background:transparent;color:var(--accent);font:inherit;cursor:pointer}
.monitor-back-key{display:grid;place-items:center;width:21px;height:21px;border:1px solid currentColor;border-radius:50%;font-size:13px;font-weight:700}
.monitor-template-workspace.monitor-output-settings :deep(.settings-section-workspace){grid-template-columns:minmax(300px,.8fr) minmax(0,1.2fr);gap:14px}
.monitor-template-workspace.monitor-output-settings :deep(.settings-section-tabs button){font-size:16px!important;min-height:40px}
.monitor-template-workspace.monitor-output-settings .monitor-template-choice{font-size:16px;min-height:40px}
.monitor-template-workspace.monitor-output-settings .monitor-template-current span{font-size:14px}
.monitor-template-workspace.monitor-output-settings .monitor-template-current strong{font-size:20px}
.monitor-template-workspace.monitor-output-settings .monitor-select-grid label,.monitor-template-workspace.monitor-output-settings .monitor-font-grid label{font-size:15px!important}
.monitor-template-workspace.monitor-output-settings :deep(.dd-trigger){font-size:15px;min-height:40px}
.monitor-template-workspace.monitor-output-settings :deep(.slider-label){font-size:14px}
.monitor-template-workspace.monitor-output-settings :deep(.slider-value){font-size:15px}
.monitor-template-workspace.monitor-output-settings .monitor-field-tab{font-size:15px!important}
.monitor-template-workspace.monitor-output-settings .monitor-field-grid :deep(.toggle-label){font-size:15px!important}
.monitor-template-workspace.monitor-output-settings .monitor-preview-mode button,.monitor-template-workspace.monitor-output-settings .monitor-drag-lock{font-size:15px;min-height:38px}
</style>

<style scoped>
.monitor-compact-heading{display:grid;grid-template-columns:minmax(240px,1fr) minmax(240px,360px) auto;gap:16px;align-items:center;min-height:42px;margin:0}
.monitor-compact-title{display:flex;align-items:center;gap:20px;min-width:0}
.monitor-compact-title h1{font-size:22px;line-height:1.2;white-space:nowrap;margin:0;font-weight:700}
.monitor-compact-title>span{font-size:13px;color:var(--text-dim);white-space:nowrap;display:flex;gap:7px;align-items:center}
.monitor-compact-title strong{font-size:15px;color:var(--accent)}
.monitor-compact-templates{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}
.monitor-compact-templates .monitor-template-choice{justify-content:center;gap:8px;min-height:38px;font-size:15px}
.monitor-template-workspace :deep(.settings-sections-top){margin-bottom:12px!important}
.monitor-live-title small{font-size:12px;color:var(--text-dim)}
.monitor-template-workspace :deep(.settings-section-heading>span){display:none}
.monitor-template-workspace[data-editor-variant=a] :deep(.settings-section-heading){display:none}
.monitor-template-workspace[data-editor-variant=a] :deep(.settings-section-workspace){grid-template-columns:minmax(310px,.72fr) minmax(0,1.28fr)!important}
.monitor-template-workspace[data-editor-variant=b] :deep(.settings-section-workspace){grid-template-columns:minmax(0,1fr)!important;grid-template-rows:180px minmax(0,1fr);gap:12px}
.monitor-template-workspace[data-editor-variant=b] .monitor-live-preview{display:grid;grid-template-columns:minmax(0,1fr) 220px;grid-template-rows:20px 1fr 40px;gap:8px 12px;padding:10px}
.monitor-template-workspace[data-editor-variant=b] .monitor-live-title{grid-column:1/-1;grid-row:1;margin:0}
.monitor-template-workspace[data-editor-variant=b] .monitor-live-preview .monitor-preview-stage{grid-column:1;grid-row:2/4;margin:0!important;min-height:0!important}
.monitor-template-workspace[data-editor-variant=b] .monitor-preview-mode{grid-column:2;grid-row:2;align-self:center}
.monitor-template-workspace[data-editor-variant=b] .monitor-preview-controls{grid-column:2;grid-row:3;margin:0}
.monitor-template-workspace[data-editor-variant=b] :deep(.settings-section-heading){display:none}
.monitor-template-workspace[data-editor-variant=b] .monitor-select-grid,.monitor-template-workspace[data-editor-variant=b] .monitor-font-grid,.monitor-template-workspace[data-editor-variant=b] .monitor-slider-stack{grid-template-columns:repeat(4,minmax(0,1fr))!important}
.monitor-template-workspace[data-editor-variant=b] .monitor-field-grid{grid-template-columns:repeat(4,minmax(0,1fr))!important}
.monitor-template-workspace[data-editor-variant=c] :deep(.settings-section-workspace){grid-template-columns:minmax(0,1fr) minmax(280px,.82fr) minmax(0,1.05fr)!important;grid-template-rows:124px minmax(0,1fr);gap:12px}
.monitor-template-workspace[data-editor-variant=c] :deep(.settings-section-preview){grid-column:2;grid-row:1/3}
.monitor-template-workspace[data-editor-variant=c] :deep(.settings-section-editor){display:contents!important}
.monitor-template-workspace[data-editor-variant=c] :deep(.settings-section-tabs){display:none!important}
.monitor-template-workspace[data-editor-variant=c] :deep(.settings-section-basics){grid-column:1;grid-row:1;min-height:0;overflow:auto;padding:6px 12px;border:1px solid var(--border);border-radius:12px;background:var(--console-surface)}
.monitor-template-workspace[data-editor-variant=c] :deep(.settings-section-panel[aria-label$="外观"]){grid-column:1;grid-row:2}
.monitor-template-workspace[data-editor-variant=c] :deep(.settings-section-panel[aria-label$="显示数据"]){grid-column:3;grid-row:1/3}
.monitor-template-workspace[data-editor-variant=c] .monitor-output-toggles :deep(.toggle-label){font-size:14px!important}
.monitor-template-workspace[data-editor-variant=c] .output-card{padding:0!important}
.monitor-template-workspace[data-editor-variant=c] .monitor-visibility-choice{margin:0;min-height:40px}
.monitor-template-workspace[data-editor-variant=c] .monitor-field-header-actions{display:grid;grid-template-columns:auto minmax(0,1fr);gap:6px}
.monitor-template-workspace[data-editor-variant=c] .monitor-fields-hide{font-size:12px!important;padding:5px!important}
.monitor-template-workspace[data-editor-variant=c] .monitor-field-tabs{grid-template-columns:repeat(3,minmax(0,1fr))!important}
.monitor-template-workspace[data-editor-variant=c] .monitor-field-tab{font-size:14px!important}
.monitor-template-workspace[data-editor-variant=c] .monitor-field-grid{grid-template-columns:repeat(2,minmax(0,1fr))!important;gap:8px!important}
.monitor-template-workspace[data-editor-variant=c] .monitor-field-grid :deep(.toggle-label){font-size:14px!important}
.monitor-template-workspace[data-editor-variant=c] .monitor-field-grid :deep(.ymcc-switch){transform:scale(.9);transform-origin:right center}
</style>

<style scoped>
.monitor-template-workspace[data-editor-variant=b] .appearance-card{display:grid!important;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px 16px;align-items:start}
.monitor-template-workspace[data-editor-variant=b] .monitor-select-grid{grid-column:1;grid-row:1;grid-template-columns:repeat(2,minmax(0,1fr))!important;margin:0!important}
.monitor-template-workspace[data-editor-variant=b] .monitor-font-grid{grid-column:2;grid-row:1;grid-template-columns:repeat(2,minmax(0,1fr))!important;margin:0!important}
.monitor-template-workspace[data-editor-variant=b] .monitor-crosshair-choice{grid-column:1;grid-row:2;min-height:40px;margin:0!important}
.monitor-template-workspace[data-editor-variant=b] .monitor-fps-emphasis{grid-column:2;grid-row:2;margin:0!important;min-height:40px;display:flex;align-items:center}
.monitor-template-workspace[data-editor-variant=b] .monitor-hardware-names{grid-column:1/-1;grid-row:3;margin:0!important}
.monitor-template-workspace[data-editor-variant=b] .monitor-slider-stack{grid-column:1/-1;grid-row:4;margin:0!important;grid-template-columns:repeat(4,minmax(0,1fr))!important}
</style>

<style scoped>
.monitor-template-workspace:not([data-editor-variant=b]) .monitor-live-title{order:0}
.monitor-template-workspace:not([data-editor-variant=b]) .monitor-preview-mode{order:1;margin-bottom:8px}
.monitor-template-workspace:not([data-editor-variant=b]) .monitor-preview-controls{order:2;margin:0 0 10px}
.monitor-template-workspace:not([data-editor-variant=b]) .monitor-live-preview .monitor-preview-stage{order:3;margin:0!important}
</style>

<style scoped>
/* A: all navigation and basic controls belong to the preview side. */
.monitor-template-workspace[data-editor-variant=a] :deep(.settings-sections-top){margin-bottom:0!important;flex-shrink:0}
.monitor-template-workspace[data-editor-variant=a] :deep(.settings-section-workspace){grid-template-columns:minmax(340px,.66fr) minmax(0,1.34fr)!important;gap:12px!important}
.monitor-template-workspace[data-editor-variant=a] :deep(.settings-section-preview){display:flex;flex-direction:column;gap:10px;min-height:0;min-width:0}
.monitor-template-workspace[data-editor-variant=a] .monitor-compact-heading{grid-template-columns:minmax(0,1fr) auto;grid-template-rows:auto auto;gap:8px 12px;min-height:0;padding:0 1px}
.monitor-template-workspace[data-editor-variant=a] .monitor-compact-title{grid-column:1;grid-row:1;display:flex;flex-direction:column;align-items:flex-start;gap:4px}
.monitor-template-workspace[data-editor-variant=a] .monitor-compact-title h1{font-size:20px}
.monitor-template-workspace[data-editor-variant=a] .monitor-compact-title>span{font-size:13px}
.monitor-template-workspace[data-editor-variant=a] .monitor-compact-title strong{font-size:14px}
.monitor-template-workspace[data-editor-variant=a] .monitor-editor-close{grid-column:2;grid-row:1;min-height:36px;font-size:14px;padding:0 12px}
.monitor-template-workspace[data-editor-variant=a] .monitor-compact-templates{grid-column:1/-1;grid-row:2;gap:8px}
.monitor-template-workspace[data-editor-variant=a] .monitor-compact-templates .monitor-template-choice{min-height:36px;font-size:15px;padding:6px 10px}
.monitor-template-workspace[data-editor-variant=a] :deep(.settings-section-basics){flex-shrink:0;min-width:0;padding:8px 10px;border:1px solid var(--border);border-radius:10px;background:var(--console-surface)}
.monitor-template-workspace[data-editor-variant=a] .output-card{padding:0!important;margin:0!important;border:0!important;box-shadow:none!important;background:transparent!important}
.monitor-template-workspace[data-editor-variant=a] .output-card>.monitor-output-head>.card-title{display:none!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-output-head{margin:0 0 5px!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-output-toggles>.toggle-row{min-height:36px!important;padding:4px 0!important;border:0!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-output-toggles :deep(.toggle-label){font-size:14px!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-visibility-choice{min-height:36px!important;margin:0!important;font-size:14px}
.monitor-template-workspace[data-editor-variant=a] .monitor-visibility-choice :deep(.dd-trigger){min-height:36px;font-size:14px}
.monitor-template-workspace[data-editor-variant=a] .monitor-live-preview{padding:10px;gap:0;min-height:160px}
.monitor-template-workspace[data-editor-variant=a] .monitor-live-title{margin-bottom:8px}
.monitor-template-workspace[data-editor-variant=a] .monitor-preview-mode button,.monitor-template-workspace[data-editor-variant=a] .monitor-drag-lock{min-height:34px;font-size:14px}
.monitor-template-workspace[data-editor-variant=a] .monitor-preview-mode{margin-bottom:7px}
.monitor-template-workspace[data-editor-variant=a] .monitor-preview-controls{margin-bottom:8px}
.monitor-template-workspace[data-editor-variant=a] :deep(.settings-section-tabs){margin-bottom:8px!important;gap:7px}
.monitor-template-workspace[data-editor-variant=a] :deep(.settings-section-tabs button){min-height:38px;font-size:15px!important}
.monitor-template-workspace[data-editor-variant=a] :deep(.settings-section-panel:not([hidden])){padding:10px!important}
.monitor-template-workspace[data-editor-variant=a] :deep(.settings-section-heading:has(.settings-section-actions)){display:flex!important;min-height:34px;align-items:center;padding-bottom:8px;margin-bottom:8px}
.monitor-template-workspace[data-editor-variant=a] :deep(.settings-section-heading:has(.settings-section-actions)>div:first-child){display:none}
.monitor-template-workspace[data-editor-variant=a] :deep(.settings-section-actions){width:100%}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-header-actions{width:100%;justify-content:flex-end}
.monitor-template-workspace[data-editor-variant=a] .appearance-card{padding:6px 0!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-slider-stack{gap:8px!important;margin-top:10px!important}
.monitor-template-workspace[data-editor-variant=a] :deep(.dd-trigger){min-height:36px}
</style>

<style scoped>
/* Always expose fine appearance controls; keep data categories within one viewport. */
.monitor-template-workspace[data-editor-variant=a] .monitor-refinements{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:6px 8px;align-items:start;padding-top:3px}
.monitor-template-workspace[data-editor-variant=a] .monitor-refinements-title{grid-column:1/-1;margin:0;color:var(--text-dim);font-size:14px;font-weight:500}
.monitor-template-workspace[data-editor-variant=a] .monitor-refinements :deep(.slider){margin-top:0!important;padding:2px 0}
.monitor-template-workspace[data-editor-variant=a] .monitor-refinements :deep(.slider-head){margin-bottom:5px}
.monitor-template-workspace[data-editor-variant=a] .fields-card{padding:6px 0!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-fields-head{margin:0 0 10px!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-fields-workbench{margin-top:8px!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-tabs{grid-template-columns:repeat(7,minmax(0,1fr))!important;gap:5px;margin-bottom:10px!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-tab{min-height:34px!important;padding:6px 4px!important;font-size:14px!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-grid{grid-template-columns:repeat(3,minmax(0,1fr))!important;gap:8px!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-grid>[data-field]{padding:6px 7px!important;min-height:0}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-grid :deep(.toggle-row){min-height:32px!important;gap:6px!important;padding:0!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-grid :deep(.toggle-label){font-size:14px!important;line-height:1.3;overflow-wrap:normal}
.monitor-template-workspace[data-editor-variant=a] .monitor-value-scaling{margin-top:4px}
.monitor-template-workspace[data-editor-variant=a] .monitor-value-scaling :deep(.slider){padding:0}
.monitor-template-workspace[data-editor-variant=a] .monitor-value-scaling :deep(.slider-head){margin-bottom:4px}
.monitor-template-workspace[data-editor-variant=a] .monitor-value-scaling :deep(.slider-label){font-size:14px!important}
</style>

<style scoped>
/* Compact data editor: four cards across, one category row, retained numeric controls. */
.monitor-template-workspace[data-editor-variant=a] .monitor-field-grid{grid-template-columns:repeat(4,minmax(0,1fr))!important;gap:7px!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-grid>[data-field]{padding:5px 6px!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-grid :deep(.toggle-row){min-height:30px!important;gap:5px!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-grid :deep(.ymcc-switch){flex-basis:42px;width:42px;min-width:42px;height:30px;min-height:30px;max-height:30px}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-grid :deep(.ymcc-switch-track){flex-basis:38px;width:38px;height:22px}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-grid :deep(.ymcc-switch-knob){width:16px;height:16px}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-grid :deep(.ymcc-switch.on .ymcc-switch-knob){transform:translateX(16px)}
.monitor-template-workspace[data-editor-variant=a] .monitor-value-scaling{margin-top:3px}
.monitor-template-workspace[data-editor-variant=a] .monitor-value-scaling :deep(.slider-head){margin-bottom:2px}
.monitor-template-workspace[data-editor-variant=a] .monitor-value-scaling :deep(.slider-track-wrap){height:20px}
.monitor-template-workspace[data-editor-variant=a] .monitor-value-scaling :deep(.slider-label){font-size:14px!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-fields-head{margin-bottom:4px!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-fields-workbench{margin-top:4px!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-data-preset-slider{padding:0 2px}
.monitor-template-workspace[data-editor-variant=a] .monitor-data-preset-slider :deep(.slider-head){margin-bottom:4px}
.monitor-template-workspace[data-editor-variant=a] .monitor-preset-ticks{height:18px;margin-top:2px}
</style>

<style scoped>
.monitor-template-workspace[data-editor-variant=a] :deep(.settings-section-bank-actions){flex-shrink:0;min-width:0;border:1px solid var(--border);border-radius:10px;padding:6px 8px;background:var(--console-surface)}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-header-actions{display:grid;grid-template-columns:auto minmax(0,1fr) auto;gap:7px;align-items:center}
.monitor-template-workspace[data-editor-variant=a] .monitor-fields-reset,.monitor-template-workspace[data-editor-variant=a] .monitor-fields-hide{font-size:14px!important;min-height:32px!important;padding:5px 8px!important;white-space:nowrap}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-total{font-size:13px}
.monitor-template-workspace[data-editor-variant=a] .monitor-data-preset-slider :deep(.slider){display:grid;grid-template-columns:80px minmax(0,1fr) 60px;gap:12px;align-items:center}
.monitor-template-workspace[data-editor-variant=a] .monitor-data-preset-slider :deep(.slider-head){display:contents}
.monitor-template-workspace[data-editor-variant=a] .monitor-data-preset-slider :deep(.slider-label){grid-column:1;grid-row:1;font-size:14px}
.monitor-template-workspace[data-editor-variant=a] .monitor-data-preset-slider :deep(.slider-val){grid-column:3;grid-row:1;text-align:right;font-size:14px}
.monitor-template-workspace[data-editor-variant=a] .monitor-data-preset-slider :deep(.slider-track-wrap){grid-column:2;grid-row:1;height:22px}
.monitor-template-workspace[data-editor-variant=a] .monitor-preset-ticks{margin:0 72px 0 92px;height:18px}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-tab{min-height:32px!important;padding:5px 4px!important}
</style>

<style scoped>
/* Balanced density: vertical category rail and readable three-column controls. */
.monitor-template-workspace[data-editor-variant=a] .monitor-fields-workbench{display:grid!important;grid-template-columns:92px minmax(0,1fr);gap:12px;align-items:start;margin-top:12px!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-tabs{display:grid!important;grid-template-columns:minmax(0,1fr)!important;grid-auto-rows:40px;gap:6px!important;align-content:start;margin:0!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-tab{min-height:40px!important;padding:8px!important;font-size:15px!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-details{min-width:0}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-grid{grid-template-columns:repeat(3,minmax(0,1fr))!important;gap:10px!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-grid>[data-field]{padding:10px!important;min-height:58px}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-grid :deep(.toggle-row){min-height:36px!important;gap:8px!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-grid :deep(.toggle-label){font-size:15px!important;line-height:1.4}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-grid :deep(.ymcc-switch){flex-basis:52px;width:52px;min-width:52px;height:36px;min-height:36px;max-height:36px}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-grid :deep(.ymcc-switch-track){flex-basis:48px;width:48px;height:26px}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-grid :deep(.ymcc-switch-knob){width:20px;height:20px}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-grid :deep(.ymcc-switch.on .ymcc-switch-knob){transform:translateX(22px)}
.monitor-template-workspace[data-editor-variant=a] .monitor-value-scaling{margin-top:7px}
.monitor-template-workspace[data-editor-variant=a] .monitor-value-scaling :deep(.slider-head){margin-bottom:6px}
.monitor-template-workspace[data-editor-variant=a] .monitor-value-scaling :deep(.slider-track-wrap){height:24px}
.monitor-template-workspace[data-editor-variant=a] .monitor-value-scaling :deep(.slider-label){font-size:14px!important}
</style>

<style scoped>
/* One category row precedes the preset slider; metric cards retain natural heights. */
.monitor-template-workspace[data-editor-variant=a] :deep(.settings-section-panel:not([hidden])[aria-label$="显示数据"]){display:flex;flex-direction:column;min-height:0}
.monitor-template-workspace[data-editor-variant=a] .fields-card{display:flex;flex-direction:column;flex:1;min-height:0}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-tabs{display:grid!important;grid-template-columns:repeat(7,minmax(0,1fr))!important;grid-template-rows:38px;grid-auto-rows:auto;height:38px;min-height:38px;gap:6px!important;margin:0 0 10px!important;flex-shrink:0;align-content:stretch}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-tab{height:38px;min-height:38px!important;padding:6px 4px!important;font-size:15px!important;white-space:nowrap}
.monitor-template-workspace[data-editor-variant=a] .monitor-fields-head{flex-shrink:0}
.monitor-template-workspace[data-editor-variant=a] .monitor-fields-workbench{flex:1;min-height:0;display:block!important;margin-top:2px!important}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-details,.monitor-template-workspace[data-editor-variant=a] .monitor-field-panels,.monitor-template-workspace[data-editor-variant=a] .monitor-field-panel{height:100%;min-height:0}
.monitor-template-workspace[data-editor-variant=a] .monitor-field-panel{padding:0}
</style>

<style scoped>
.monitor-overview{display:flex;flex-direction:column;gap:10px;min-height:0;min-width:0;box-sizing:border-box}
.monitor-overview-output,.monitor-overview-preview{min-width:0;background:var(--console-surface);border:1px solid var(--console-line);border-radius:12px;padding:12px;backdrop-filter:blur(12px)}
.monitor-overview-output{flex:0 0 auto}
.monitor-overview .monitor-overview-heading{display:flex;align-items:center;gap:7px;margin:0 0 10px;padding-bottom:10px;border-bottom:1px solid var(--console-line);font-size:16px}
.monitor-overview-heading .inline-icon{color:var(--accent)}
.monitor-overview-controls{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:14px;margin:8px 0}
.monitor-overview-controls :deep(.toggle-row){border:0!important;padding:6px 0!important}
.monitor-overview-visibility{display:grid;grid-template-columns:max-content minmax(0,1fr);align-items:center;gap:12px;min-width:0;margin-top:4px;font-size:14px}
.monitor-overview-visibility>span{white-space:nowrap}
.monitor-overview-visibility :deep(.dd){width:100%;min-width:0}
.monitor-overview-editor-bar{flex:0 0 auto;display:grid;grid-template-columns:minmax(0,1fr) 204px;align-items:stretch;gap:10px;min-width:0}
.monitor-overview-entry{display:flex;align-items:center;gap:8px;min-width:0;width:100%;min-height:62px;text-align:left;border:1px solid color-mix(in srgb,var(--accent) 65%,transparent);border-radius:10px;padding:8px 10px;background:var(--accent-surface);color:var(--text);font:inherit;cursor:pointer}
.monitor-overview-template-switch{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:3px;align-items:stretch;min-width:0;padding:3px;border-radius:14px;background:color-mix(in srgb,var(--bg-panel) 86%,var(--console-surface-alt));box-shadow:inset 0 0 0 1px color-mix(in srgb,var(--text) 5%,transparent)}
.monitor-overview-template{min-width:0;min-height:54px;border:1px solid transparent;border-radius:11px!important;padding:6px;color:var(--text-dim);background:transparent;font:inherit;font-size:14px;font-weight:650;white-space:nowrap;cursor:pointer}
.monitor-overview-template.active{border-color:color-mix(in srgb,var(--accent) 70%,transparent);color:var(--accent);background:color-mix(in srgb,var(--accent) 17%,var(--bg-panel))}
.monitor-overview-template:focus-visible{outline:2px solid var(--accent);outline-offset:1px}
.monitor-overview-template:disabled{opacity:.5;cursor:default}
.monitor-overview-entry>span{display:grid;gap:3px;min-width:0}
.monitor-overview-entry strong{font-size:16px;line-height:1.35}
.monitor-overview-entry small{font-size:14px;color:var(--text-dim);line-height:1.4}
.monitor-overview-entry>.inline-icon{flex:none;color:var(--accent)}
.monitor-overview-entry:focus-visible,.monitor-overview-lock:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
.monitor-overview-entry:disabled,.monitor-overview-lock:disabled{opacity:.5;cursor:default}
.monitor-overview-preview{flex:1 1 0;min-height:130px;display:flex;flex-direction:column;gap:8px;overflow:hidden}
.monitor-overview-preview-heading{display:flex;align-items:center;justify-content:space-between;gap:8px;flex:0 0 auto;min-width:0}
.monitor-overview-preview-heading>strong{font-size:14px;white-space:nowrap}
.monitor-overview-lock{flex:0 0 auto;display:flex;align-items:center;justify-content:center;gap:7px;width:100%;border:1px solid var(--console-line);border-radius:8px;min-height:32px;padding:5px 8px;background:var(--console-surface-alt);color:var(--text-dim);font:inherit;font-size:14px;cursor:pointer;white-space:nowrap}
.monitor-overview-lock .inline-icon{color:var(--accent)}
.monitor-overview-lock[aria-pressed=true]{color:var(--accent);border-color:color-mix(in srgb,var(--accent) 50%,transparent)}
.monitor-overview-preview-canvas{flex:1;min-height:0;min-width:0;overflow:hidden;border-radius:8px;background:color-mix(in srgb,var(--bg-panel) 56%,transparent)}
.monitor-overview-rtss{margin-top:12px}
.monitor-overview-rtss h3{font-size:16px}
.monitor-overview-rtss :deep(.slider){margin-top:14px}
.monitor-overview-error{margin:8px 0 0;color:var(--danger);font-size:13px;overflow-wrap:anywhere}
</style>

