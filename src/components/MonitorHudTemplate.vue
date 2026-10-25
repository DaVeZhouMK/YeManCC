<script setup lang="ts">
import { computed, ref, shallowRef, onMounted, onActivated, onDeactivated, onUnmounted, watch, nextTick } from 'vue';
import MonitorHoneycombFrame from './MonitorHoneycombFrame.vue';
import MonitorHoneycombDigits from './MonitorHoneycombDigits.vue';
import { honeycombOutline } from '@/bridge/monitorHoneycombStyle';
import { createHudFpsVisibilityGate } from '@/bridge/monitorHudFpsVisibility';
import { packedBarWidth } from '@/bridge/monitorBarPacking';
import { coreTileLayout, coreTileUsage } from '@/bridge/monitorCoreTiles';
import { HUD_METRICS, HUD_PERCENT_GRAPHS, hudBaseLayout, isHorizontalHudLayout, isVerticalHudLayout, hudFontFamily, hudPercentValue, hudTemperatureWarning, hudMetricDigitSlots, hudTextScale, HUD_VALUE_SCALE_MAX, hudValue, hudMetricAvailable, hudVisibleFields, sameHudFields, percentBar, type HudMetricId, type HudAppearance, type HudMetricToggles, type HudData } from '@/bridge/monitorHudModel';
import { createFrameTimeCurveCache } from '@/bridge/monitorFrameTime';
import { HUD_TIFA_UNDERLAY } from '@/bridge/monitorHudUnderlayDemo';
import { HUD_AC8_UNDERLAY } from '@/bridge/monitorHudUnderlayAc8';
const props = defineProps<{ appearance: HudAppearance; fields: HudMetricToggles; data: HudData }>();
const emit = defineEmits<{ (event: 'layout'): void }>();
const fpsReady = ref(false);
const fpsGate = createHudFpsVisibilityGate({ now: () => Date.now(), emit: ready => { fpsReady.value = ready; },
  setTimer: (fn, ms) => window.setTimeout(fn, ms), clearTimer: id => window.clearTimeout(id as number) });
watch(() => props.data, data => fpsGate.accept(data), { immediate: true, flush: 'sync' });
onActivated(() => fpsGate.accept(props.data));
onDeactivated(() => fpsGate.reset());
onUnmounted(() => fpsGate.dispose());
const visibleFields = computed<HudMetricToggles>((previous) => {
  // Keep the selected FPS shell; dependent statistics still require genuine FPS.
  const next = hudVisibleFields(props.fields, props.data, props.appearance.hideUnavailable === true, fpsReady.value);
  // Fully evaluate availability before reusing the previous value so Vue keeps
  // complete dependencies while stable availability avoids group/layout work.
  return previous && sameHudFields(previous, next) ? previous : next;
});
const fpsValid = computed(() => fpsReady.value && hudMetricAvailable(props.data, 'fps'));
const cachedFrameCurve = createFrameTimeCurveCache();
const framePaths = computed(() => !visibleFields.value.frametimeGraph || !fpsValid.value || !(Number(props.data.values.fps) > 0) ? [] : cachedFrameCurve(props.data.frameTimes));
const graphVisible = computed(() => framePaths.value.length > 0);
const emptyHidden = computed(() => props.appearance.hideUnavailable === true && !groups.value.length);
const groupNames = ['帧率', 'CPU', 'GPU', '内存', '读写', '电池', '附加'];
const special = new Set<string>(['fps', 'cpuCores', 'frametimeGraph', ...HUD_PERCENT_GRAPHS.map(graph => graph.id)]);
// Fixed metadata belongs to component setup, not repeated field projection.
// A graph-only source such as cpuPeak must stay graph-only even with old fields.
const vertical = computed(() => isVerticalHudLayout(props.appearance.layout));
const baseLayout = computed(() => hudBaseLayout(props.appearance.layout));
const honeycomb = computed(() => props.appearance.layout === 'honeycomb');
const honeycombSegments = computed(() => honeycomb.value && (!props.appearance.fpsFont || props.appearance.fpsFont === 'inherit')); 
const groupDefinitions = groupNames.map(name => ({ name, key: name, label: name === '内存' ? 'MEM' : name === '读写' ? 'I/O' : name === '电池' ? 'BATT' : name === '附加' ? 'TIME' : name,
  metrics: HUD_METRICS.filter(metric => metric.group === name && !special.has(metric.id)),
  graphs: HUD_PERCENT_GRAPHS.filter(graph => HUD_METRICS.find(metric => metric.id === graph.id)?.group === name)
    .map(graph => ({ graph, scalar: HUD_METRICS.find(metric => metric.id === (graph.value as string)) })),
}));
// Static projections share the existing metric/graph metadata and data leases.
// Vertical compositions split RAM/VRAM into neighboring sections.
const verticalGroupDefinitions = groupDefinitions.flatMap(group => group.name !== '内存' ? [group] :
  ['ram', 'vram'].map(prefix => ({ ...group, key: prefix, label: prefix === 'ram' ? '内存' : '显存',
    metrics: group.metrics.filter(metric => metric.id.startsWith(prefix)),
    graphs: group.graphs.filter(({ graph }) => graph.id.startsWith(prefix)),
  })));
const groups = computed(() => {
  const fields = visibleFields.value;
  return (vertical.value ? verticalGroupDefinitions : groupDefinitions).map(group => ({ name: group.name, key: group.key, label: group.label,
    metrics: group.metrics.filter(metric => fields[metric.id]),
    graphs: group.graphs.filter(({ graph, scalar }) => fields[graph.id] && !(scalar && fields[scalar.id])).map(({ graph }) => graph),
    extra: (group.name === '帧率' && (fields.fps || graphVisible.value)) ||
      (group.name === 'CPU' && fields.cpuCores) || group.graphs.some(({ graph }) => fields[graph.id]),
  })).filter(group => group.metrics.length || group.extra);
});
// All inline meter lookups share this field-owned index. A data-only snapshot
// keeps the same visibleFields object, so it does not allocate/rebuild the map.
const inlineGraphs = computed(() => Object.fromEntries(
  HUD_PERCENT_GRAPHS.filter(graph => visibleFields.value[graph.id]).map(graph => [graph.value, graph]),
) as Partial<Record<string, typeof HUD_PERCENT_GRAPHS[number]>>);
// Layout depends on selected fields, never on each 1 Hz numeric update.
const selectedCount = computed(() => HUD_METRICS.filter(metric => props.fields[metric.id]).length);
const selectedGroups = computed(() => new Set(HUD_METRICS.filter(metric=>props.fields[metric.id]).map(metric=>metric.group)).size);
const compact = computed(() => selectedGroups.value <= 2 && selectedCount.value <= 5 &&
  !props.fields.cpuCores && !props.fields.frametimeGraph && !HUD_PERCENT_GRAPHS.some(graph=>props.fields[graph.id]));
// Three-item presentation (and its partial/auto-hidden subsets) is display-only.
const summary = computed(() => compact.value && selectedCount.value > 0 &&
  HUD_METRICS.every(metric => !props.fields[metric.id] || ['fps', 'cpuPower', 'cpuTemp'].includes(metric.id)));
const horizontal = computed(() => isHorizontalHudLayout(props.appearance.layout));
const full = computed(() => selectedCount.value > 30);
const dense = computed(() => groups.value.length > 4 && !horizontal.value && !vertical.value && (full.value || baseLayout.value === 'capsule'));
const underlaySource = computed(() => props.appearance.underlay === 'tifa' ? HUD_TIFA_UNDERLAY : props.appearance.underlay === 'ac8-pink' ? HUD_AC8_UNDERLAY : props.appearance.underlay === 'custom' ? props.appearance.underlayImage || '' : props.appearance.underlay === 'game-logo' ? props.data.gameLogo || '' : '');
const fpsEmphasis = computed(() => props.appearance.fpsEmphasis !== false);
const duetSummary = computed(() => summary.value && baseLayout.value === 'capsule' && !honeycomb.value);
// Reserve three tabular digit slots. Numeric updates never change the font
// size or rearm layout observers; only composition/appearance changes do.
const fpsEmphasisSize = computed(() => {
  const base = summary.value ? 5.2 : vertical.value ? 4.8 : horizontal.value ? 4.8 : full.value ? 7.4 : dense.value ? 8.2 : 6.8;
  return base.toFixed(3) + 'em';
});
const columns = computed(() => [
  groups.value.filter(group => ['帧率', '附加'].includes(group.name)),
  groups.value.filter(group => ['CPU', '内存'].includes(group.name)),
  groups.value.filter(group => ['GPU', '读写', '电池'].includes(group.name)),
].filter(column => column.length));
const groupColumns = computed(() => honeycomb.value || compact.value ? [groups.value] : dense.value ? columns.value : [groups.value]);
function groupStyle(group: typeof groups.value[number]) {
  const count = group.metrics.length;
  // Battery level occupies the left cell; power/time share the right column.
  // Field switches determine the grid, never each numeric sensor update.
  if (group.name === '电池') return { '--hud-value-cols': visibleFields.value.batteryPercent && count > 1 ? 2 : 1 };
  if (vertical.value) return { '--hud-value-cols': group.name === '内存' ? 2 : group.name === '读写' ? Math.min(4, count) : group.name === '帧率' ? 1 : Math.min(3, count) };
  const expandedBar = horizontal.value && selectedCount.value > 20;
  const valueColumns = group.name === '内存' && count > 4 ? 4 : expandedBar && group.name === 'CPU' ? Math.min(4, count) :
    (groups.value.length > 4 || expandedBar) && ['帧率', '电池'].includes(group.name) ? Math.min(3, count) : Math.min(count <= 4 ? 2 : 3, count);
  return { '--hud-value-cols': Math.max(1, valueColumns) };
}
function inlineGraph(id: string) {
  return inlineGraphs.value[id];
}
// A visible percentage is validated once per reactive data/field update.
// Meter state, width, alerts and battery text share that same zero/null result.
// Keep this lazy: a minimal FPS/power/temperature HUD computes no percentages.
const percentValues = computed<Record<string, number | null>>(() => {
  const values: Record<string, number | null> = {};
  for (const graph of HUD_PERCENT_GRAPHS) {
    if (visibleFields.value[graph.id] || (graph.value === 'batteryPercent' && visibleFields.value.batteryPercent)) {
      values[graph.value] = hudPercentValue(props.data, graph.value);
    }
  }
  return values;
});
const batteryText = computed(() => {
  const value = percentValues.value.batteryPercent ?? null;
  return value === null ? '—' : value.toFixed(0) + '%';
});
function percentGraphAlert(id: string) {
  if (id !== 'ramUsage' && id !== 'vramUsage' && id !== 'batteryPercent') return undefined;
  const value = percentValues.value[id] ?? null;
  if (value === null) return undefined;
  if (id === 'batteryPercent') {
    if (value <= 10) return 'warning';
    return value >= 20 && value <= 30 ? 'caution' : undefined;
  }
  if (value >= 95) return 'warning';
  return value >= 80 && value <= 85 ? 'caution' : undefined;
}
function hasContent(group: typeof groups.value[number]) {
  return group.metrics.length > 0 || group.name !== '帧率';
}
const board = ref<HTMLElement | null>(null), barWidth = ref<number | null>(null);
const underlayBounds = ref<{width:number;height:number} | null>(null);
let underlayObserver: ResizeObserver | null = null;
// Explicit logical pixel bounds avoid a stale percentage-sized image in a
// transparent native surface after data auto-hide, packing or FPS resizing.
function refreshUnderlayBounds() {
  const el=board.value, win=el?.ownerDocument.defaultView;
  if (!el || !win || (!underlaySource.value && !honeycomb.value) || emptyHidden.value) return;
  const style=win.getComputedStyle(el), width=parseFloat(style.width), height=parseFloat(style.height);
  if (!Number.isFinite(width) || !Number.isFinite(height) || width<=0 || height<=0) return;
  const old=underlayBounds.value;
  if (!old || Math.abs(old.width-width)>.01 || Math.abs(old.height-height)>.01) underlayBounds.value={width,height};
}
const underlayStyle = computed(() => underlayBounds.value ? {width:underlayBounds.value.width+'px',height:underlayBounds.value.height+'px',...(honeycomb.value ? {clipPath:`path('${honeycombOutline(underlayBounds.value.width,underlayBounds.value.height)}')`} : {})} : {});
function reconnectUnderlay() {
  underlayObserver?.disconnect();underlayObserver=null;underlayBounds.value=null;
  const el=board.value,win=el?.ownerDocument.defaultView;
  if (!el || !win || (!underlaySource.value && !honeycomb.value) || emptyHidden.value) return;
  if (win.ResizeObserver) { underlayObserver=new win.ResizeObserver(refreshUnderlayBounds);underlayObserver.observe(el); }
  refreshUnderlayBounds();
}
onMounted(reconnectUnderlay);
watch(() => [underlaySource.value,honeycomb.value,emptyHidden.value], () => {void nextTick(reconnectUnderlay);});
onUnmounted(() => {underlayObserver?.disconnect();underlayObserver=null;});

let observer: ResizeObserver | null = null, frame: number | null = null, frameWindow: Window | null = null, parent: HTMLElement | null = null, disposed = false;
function scheduleBarFit() {
  if (disposed || !horizontal.value || summary.value || frame !== null) return;
  const win = board.value?.ownerDocument.defaultView; if (!win) return;
  frameWindow = win; frame = win.requestAnimationFrame(() => { frame = null; frameWindow = null; fitBar(); });
}
function fitBar() {
  const el = board.value;
  if (!el || !parent || !horizontal.value || summary.value) return;
  const win = el.ownerDocument.defaultView; if (!win) return;
  const style = win.getComputedStyle(el), outer = win.getComputedStyle(parent);
  // client/offset widths are CSS-logical units, unlike zoomed DOM rectangles.
  // The parent is outside the HUD zoom; pack in the board's base-size units.
  const scale = props.appearance.scale;
  const available = (parent.clientWidth - parseFloat(outer.paddingLeft) - parseFloat(outer.paddingRight)) / scale;
  const padding = parseFloat(style.paddingLeft) + parseFloat(style.paddingRight);
  const children = Array.from(el.querySelectorAll<HTMLElement>(':scope > .hud-column > .hud-group,:scope > .hud-empty'));
  // offsetWidth rounds each group before summation; at 200% that can push
  // an otherwise fitting final group onto a new row. CSS width keeps fractions
  // in base units, including when the settings preview has its own 80% zoom.
  const widths = children.map(child => parseFloat(win.getComputedStyle(child).width) || child.offsetWidth);
  const width = packedBarWidth(widths, available, parseFloat(style.columnGap) || 0, padding);
  if (width > 0 && (barWidth.value === null || Math.abs(barWidth.value - width) > 1)) {
    barWidth.value = Math.ceil(width);
    // Report only after the final packed width is applied, not an interim natural width.
    void nextTick(() => { if (!disposed) emit('layout'); });
  }
}
function reconnectBarFit() {
  observer?.disconnect(); observer = null;
  if (frame !== null) frameWindow?.cancelAnimationFrame(frame); frame = null; frameWindow = null;
  barWidth.value = null; parent = board.value?.parentElement ?? null;
  if (disposed || !horizontal.value || summary.value || !board.value || !parent) return;
  const win = board.value.ownerDocument.defaultView;
  if (win?.ResizeObserver) {
    // Event-driven only: no resident loop and no watcher on 1 Hz sensor values.
    let parentWidth = parent.clientWidth;
    observer = new win.ResizeObserver(entries => {
      // A native game/monitor resize needs one natural-width baseline, otherwise
      // a previously constrained group could stay narrow after the viewport grows.
      if (entries.some(entry => entry.target === parent) && parent && parent.clientWidth !== parentWidth) {
        parentWidth = parent.clientWidth; barWidth.value = null; void nextTick(scheduleBarFit);
      } else scheduleBarFit();
    });
    observer.observe(parent);
    for (const child of board.value.querySelectorAll(':scope > .hud-column > .hud-group')) observer.observe(child);
  }
  scheduleBarFit();
}
onMounted(reconnectBarFit);
// A primitive layout signature: data-only snapshots must not clear width/reconnect
// observers every second merely because computed group arrays got a new identity.
watch(() => JSON.stringify([props.appearance.layout, props.appearance.font, props.appearance.fpsFont, props.appearance.scale, props.appearance.textScale, props.appearance.valueScales, props.appearance.showCpuName, props.appearance.showGpuName, props.data.cpuName, props.data.gpuName, props.appearance.gap, fpsEmphasis.value,
  groups.value.map(group => group.name + ':' + group.metrics.map(metric => metric.id).join(',') + ':' + group.extra + ':' + group.graphs.map(graph=>graph.id).join(',')).join('|'),
  visibleFields.value.cpuCores, visibleFields.value.frametimeGraph, HUD_PERCENT_GRAPHS.map(graph => visibleFields.value[graph.id]), visibleFields.value.fps]),
  () => { barWidth.value = null; void nextTick(reconnectBarFit); });
onUnmounted(() => { disposed = true; observer?.disconnect(); if (frame !== null) frameWindow?.cancelAnimationFrame(frame); });
const textScale = computed(()=>hudTextScale(props.appearance.textScale,1.2));
function metricScale(id: string) { return hudTextScale(props.appearance.valueScales?.[id as HudMetricId],1,HUD_VALUE_SCALE_MAX); }
function groupLabel(group: {name:string;label:string}) {
  if(group.name==='CPU' && props.appearance.showCpuName && props.data.cpuName)return props.data.cpuName;
  if(group.name==='GPU' && props.appearance.showGpuName && props.data.gpuName)return props.data.gpuName;
  return group.label;
}
const boardStyle = computed(() => ({
  ...(emptyHidden.value ? { width: '1px', height: '1px', padding: '0', visibility: 'hidden' as const, backgroundColor: 'transparent' } : {}),
  width: emptyHidden.value ? '1px' : horizontal.value && summary.value ? 'max-content' :
    horizontal.value && barWidth.value !== null ? barWidth.value + 'px' : undefined,
  // Scale the complete board, not just text: spacing, tiles, borders and radius.
  fontSize: '13px', '--hud-text-scale':textScale.value, '--hud-fps-value-scale':metricScale('fps'), fontFamily: hudFontFamily(props.appearance.font), '--hud-fps-font': props.appearance.fpsFont === 'inherit' || !props.appearance.fpsFont ? 'inherit' : hudFontFamily(props.appearance.fpsFont), zoom: props.appearance.scale,
  '--hud-fps-size': fpsEmphasis.value ? fpsEmphasisSize.value : '1.65em',
  backgroundColor: emptyHidden.value || honeycomb.value ? 'transparent' : `rgba(16,22,31,${props.appearance.opacity})`, borderRadius: props.appearance.radius + 'px',
  '--hud-opacity': props.appearance.opacity, '--hud-image-opacity': props.appearance.underlayOpacity ?? .28,
  '--hud-gap': props.appearance.gap + 'px', '--hud-honeycomb-text':textScale.value/1.2, '--hud-honeycomb-fps-height':114*(textScale.value/1.2)*metricScale('fps')*(fpsEmphasis.value?1:.55)+'px', '--hud-accent': honeycomb.value ? `hsl(7 ${Math.round(100 * props.appearance.saturation)}% 60%)` : `hsl(195 ${Math.round(62 * props.appearance.saturation)}% 72%)`,
  '--hud-secondary': honeycomb.value ? `hsl(10 ${Math.round(40 * props.appearance.saturation)}% 65%)` : `rgba(225,233,242,${.55 + .35 * props.appearance.saturation})`,
}));
const coreValues = computed(() => !visibleFields.value.cpuCores ? [] : coreTileUsage(props.data.coreUsage, props.data.stale));
const coreLayout = computed(() => coreTileLayout(coreValues.value.length));
// # means array position only, not an invented physical-core identity.
const coreLabel = (i: number) => props.data.coreLabels?.[i] || '#' + (i + 1);

// Approved minimal B composition. Geometry changes are coalesced, event driven
// and bounded; this consumes the same admitted YMCC snapshot as the full HUD.
const duetHero=ref<HTMLElement|null>(null),duetReadouts=ref<HTMLElement|null>(null),duetLine=ref<HTMLElement|null>(null),duetNumber=ref<HTMLElement|null>(null);
const duetMetrics=computed(()=>HUD_METRICS.filter(metric=>(metric.id==='cpuPower'||metric.id==='cpuTemp')&&visibleFields.value[metric.id]));
function duetValue(id: HudMetricId): string {
  const value=props.data.values[id];
  if(hudValue(props.data,id)==='—'||typeof value!=='number'||!Number.isFinite(value))return '—';
  // Presentation bounds only. The sole YMCC snapshot retains the real value.
  return id==='cpuPower'?Math.min(999.9,Math.max(0,value)).toFixed(1):String(Math.min(999,Math.max(0,Math.round(value))));
}
const duetAlignment=shallowRef({fontSize:0,left:0,top:0,width:0,cell:0,captionLeft:0,height:0});
const duetStyle=computed(()=>({
  gridTemplateColumns:visibleFields.value.fps?'var(--duet-width) max-content':'max-content',
  '--duet-width':duetAlignment.value.width?duetAlignment.value.width+'px':240*textScale.value/1.2+'px',
  '--duet-font-size':duetAlignment.value.fontSize?duetAlignment.value.fontSize+'px':116*textScale.value+'px',
  '--duet-left':duetAlignment.value.left+'px','--duet-top':duetAlignment.value.top+'px',
  '--duet-cell':duetAlignment.value.cell?duetAlignment.value.cell+'px':'1ch',
  '--duet-caption-left':duetAlignment.value.captionLeft+'px','--duet-height':(duetAlignment.value.height||134*textScale.value/1.2)+'px',
}));
let duetObserver:ResizeObserver|null=null,duetFrame:number|null=null,duetWin:Window|null=null,duetEnded=false,duetMeasuring=false;
let duetContext:CanvasRenderingContext2D|null=null;
async function alignDuet(){
  duetFrame=null;if(duetEnded||duetMeasuring||!duetSummary.value||!visibleFields.value.fps)return;duetMeasuring=true;
  try{
    const right=duetReadouts.value,hero=duetHero.value,numeric=duetNumber.value,line=duetLine.value;
    const win=right?.ownerDocument.defaultView;if(!right||!hero||!numeric||!line||!win)return;
    duetContext??=right.ownerDocument.createElement('canvas').getContext('2d');const ctx=duetContext;if(!ctx)return;
    const metrics=(el:HTMLElement,value:string,size?:number)=>{const style=win.getComputedStyle(el);ctx.font=style.fontWeight+' '+(size??parseFloat(style.fontSize))+'px '+style.fontFamily;return ctx.measureText(value);};
    const readings=Array.from(right.querySelectorAll<HTMLElement>('.hud-duet-value'));
    const first=readings[0],last=readings[readings.length-1],a=first?.querySelector<HTMLElement>('b'),b=last?.querySelector<HTMLElement>('b');
    const ab=first?.querySelector<HTMLElement>('.hud-duet-baseline'),bb=last?.querySelector<HTMLElement>('.hud-duet-baseline');
    if(!a||!b||!ab||!bb)return;
    const am=metrics(a,'0123456789'),bm=metrics(b,'0123456789');
    // Work in unzoomed CSS pixels, including the settings preview transform.
    const rect=right.getBoundingClientRect(),cssHeight=parseFloat(win.getComputedStyle(right).height);
    const factor=cssHeight>0&&rect.height>0?rect.height/cssHeight:1;
    const top=(ab.getBoundingClientRect().top-am.actualBoundingBoxAscent*factor-hero.getBoundingClientRect().top)/factor;
    const bottom=(bb.getBoundingClientRect().top+bm.actualBoundingBoxDescent*factor-hero.getBoundingClientRect().top)/factor;
    const unit=metrics(numeric,'0123456789',100),canonicalSize=Math.max(12,(bottom-top)*100/(unit.actualBoundingBoxAscent+unit.actualBoundingBoxDescent));
    let fontSize=canonicalSize*metricScale('fps')*(fpsEmphasis.value?1:.55);
    // Canonical digit metrics keep the shell independent of the current reading
    // and of a literal placeholder, including during missing/recovery cycles.
    const reserved=metrics(numeric,'000',fontSize),width=Math.ceil(reserved.width+3),cell=reserved.width/3+1;
    const numericValue=fpsValid.value?hudValue(props.data,'fps'):'FPS';
    const actual=metrics(numeric,numericValue,numericValue==='FPS'?fontSize*.42:fontSize);
    const leftInk=-actual.actualBoundingBoxLeft;
    const rightInk=numericValue==='FPS'?actual.actualBoundingBoxRight:(numericValue.length-1)*cell+metrics(numeric,numericValue.slice(-1),fontSize).actualBoundingBoxRight;
    const offset=(width-(rightInk-leftInk))/2-leftInk;
    const caption=hero.querySelector<HTMLElement>('.hud-duet-caption'),captionHeight=caption?parseFloat(win.getComputedStyle(caption).lineHeight):14*textScale.value;
    const digitBox=metrics(numeric,'0123456789',fontSize);
    const height=Math.max(cssHeight,top+digitBox.actualBoundingBoxAscent+digitBox.actualBoundingBoxDescent+captionHeight+5);
    if(Math.abs(duetAlignment.value.fontSize-fontSize)>.01)duetAlignment.value={...duetAlignment.value,fontSize};
    await nextTick();if(duetEnded)return;
    const baseline=line.querySelector<HTMLElement>('.hud-duet-baseline');if(!baseline)return;
    const baselineOffset=(baseline.getBoundingClientRect().top-line.getBoundingClientRect().top)/factor;
    // Center the smaller placeholder inside the exact numeric ink height.
    const inkTop=numericValue==='FPS'?top+(digitBox.actualBoundingBoxAscent+digitBox.actualBoundingBoxDescent-actual.actualBoundingBoxAscent-actual.actualBoundingBoxDescent)/2:top;
    const next={fontSize,left:offset,top:inkTop-baselineOffset+actual.actualBoundingBoxAscent,width,cell,captionLeft:offset+leftInk+3*textScale.value,height};
    if(Object.keys(next).some(key=>Math.abs(next[key as keyof typeof next]-duetAlignment.value[key as keyof typeof next])>.01))duetAlignment.value=next;
  }finally{duetMeasuring=false;}
}
function scheduleDuet(){if(duetEnded||!duetSummary.value||duetFrame!==null)return;duetWin=board.value?.ownerDocument.defaultView??null;if(duetWin)duetFrame=duetWin.requestAnimationFrame(()=>{void alignDuet();});}
function connectDuet(){duetObserver?.disconnect();duetObserver=null;if(duetEnded||!duetSummary.value)return;const win=board.value?.ownerDocument.defaultView;if(win?.ResizeObserver&&duetReadouts.value){duetObserver=new win.ResizeObserver(scheduleDuet);duetObserver.observe(duetReadouts.value);}scheduleDuet();}
onMounted(()=>{connectDuet();board.value?.ownerDocument.fonts?.ready.then(scheduleDuet);});
watch(()=>duetSummary.value,()=>{void nextTick(connectDuet);});
watch(()=>[props.appearance.font,props.appearance.fpsFont,props.appearance.scale,props.appearance.textScale,JSON.stringify(props.appearance.valueScales),props.appearance.fpsEmphasis,fpsValid.value,hudValue(props.data,'fps'),hudValue(props.data,'cpuPower'),hudValue(props.data,'cpuTemp'),props.appearance.showCpuName,props.data.cpuName],()=>{void nextTick(scheduleDuet);});
onUnmounted(()=>{duetEnded=true;duetObserver?.disconnect();if(duetFrame!==null)duetWin?.cancelAnimationFrame(duetFrame);duetContext=null;});

// Reserve the last genuine frame section footprint while its dependent values
// disappear. A lost producer cannot collapse A's frame section or repack the HUD.
const fpsShell=shallowRef({width:0,height:0});
const fpsShellStyle=computed(()=>props.fields.fps&&fpsShell.value.width>0?{minWidth:fpsShell.value.width+'px',minHeight:fpsShell.value.height+'px'}:undefined);
let fpsShellObserver:ResizeObserver|null=null;
function connectFpsShell(){
  fpsShellObserver?.disconnect();fpsShellObserver=null;
  const el=board.value?.querySelector<HTMLElement>('[data-hud-group="帧率"]'),win=el?.ownerDocument.defaultView;
  if(!el||!win||summary.value||!props.fields.fps)return;
  const capture=()=>{if(!fpsValid.value)return;const style=win.getComputedStyle(el);const width=parseFloat(style.width),height=parseFloat(style.height);if(!Number.isFinite(width)||!Number.isFinite(height))return;const before=fpsShell.value;if(width>before.width+.1||height>before.height+.1)fpsShell.value={width:Math.max(before.width,width),height:Math.max(before.height,height)};};
  if(win.ResizeObserver){fpsShellObserver=new win.ResizeObserver(capture);fpsShellObserver.observe(el);}capture();
}
onMounted(connectFpsShell);
watch(()=>JSON.stringify([props.appearance.layout,props.appearance.font,props.appearance.fpsFont,props.appearance.scale,props.appearance.textScale,props.appearance.valueScales,props.appearance.gap,props.appearance.fpsEmphasis,props.fields]),()=>{fpsShell.value={width:0,height:0};void nextTick(connectFpsShell);});
watch(()=>fpsValid.value,()=>{void nextTick(connectFpsShell);});
onUnmounted(()=>{fpsShellObserver?.disconnect();fpsShellObserver=null;});

</script>
<template>
  <div ref="board" class="ymcc-hud" :class="['ymcc-hud--' + baseLayout, { 'ymcc-hud--honeycomb':honeycomb, 'ymcc-hud--honeycomb-summary':honeycomb && summary, 'ymcc-hud--image-underlay': !!underlaySource && !emptyHidden, 'ymcc-hud--fps-emphasis': fpsEmphasis, 'ymcc-hud--fps-regular': !fpsEmphasis, 'ymcc-hud--dense': dense, 'ymcc-hud--horizontal': horizontal, 'ymcc-hud--vertical': vertical, 'ymcc-hud--full': full, 'ymcc-hud--compact': compact, 'ymcc-hud--summary': summary, 'ymcc-hud--duet-summary': duetSummary, 'ymcc-hud--summary-split': summary && visibleFields.fps && (visibleFields.cpuPower || visibleFields.cpuTemp) }]" :style="boardStyle" data-monitor-hud="ymcc" :data-hud-layout="honeycomb ? 'honeycomb' : baseLayout">
    <MonitorHoneycombFrame v-if="honeycomb && !emptyHidden" :width="underlayBounds?.width ?? 560" :height="underlayBounds?.height ?? 267" :opacity="appearance.opacity" />
    <header v-if="honeycomb && !emptyHidden" class="hud-honeycomb-topline"><span class="hud-honeycomb-identity"><i aria-hidden="true"></i><b>运行监控</b><span>HEXA / 01</span></span><span class="hud-honeycomb-status">▪ YMCC</span></header>
    <img v-if="underlaySource && !emptyHidden" class="hud-image-underlay" :src="underlaySource" :style="underlayStyle" @load="refreshUnderlayBounds" alt="" aria-hidden="true" draggable="false" decoding="async" />
    <div v-if="!groups.length && !emptyHidden" class="hud-empty">所有数据项已关闭</div>
    <div v-if="honeycomb && summary" class="hud-honeycomb-primary">
      <section v-if="visibleFields.fps" class="hud-honeycomb-fps" data-hud-group="帧率" data-hud-metric="fps" :data-fps-state="fpsValid ? 'live' : 'unknown'">
        <strong class="hud-honeycomb-fps-value" :class="{'hud-honeycomb-fps-value--missing':!fpsValid}"><template v-if="fpsValid"><span :class="{'hud-honeycomb-accessible-value':honeycombSegments}">{{hudValue(data,'fps')}}</span><MonitorHoneycombDigits v-if="honeycombSegments" :value="hudValue(data,'fps')" /></template><span v-else class="hud-honeycomb-placeholder">FPS</span></strong>
        <span class="hud-honeycomb-fps-caption">FPS帧数</span>
      </section>
      <section v-if="duetMetrics.length" class="hud-honeycomb-readouts" data-hud-group="CPU">
        <div v-for="metric in duetMetrics" :key="metric.id" class="hud-honeycomb-reading" :data-hud-metric="metric.id">
          <p class="hud-duet-value" :style="{'--duet-reading-size':29 * textScale / 1.2 * metricScale(metric.id)+'px'}"><span class="hud-duet-reserve" aria-hidden="true"><b>{{metric.id==='cpuPower'?'999.9':'999'}}</b><small>{{metric.unit}}</small><span v-if="metric.id==='cpuTemp'" class="hud-duet-warning">!</span></span><span class="hud-duet-live"><b class="hud-duet-number">{{duetValue(metric.id)}}</b><small>{{metric.unit}}</small><span v-if="metric.id==='cpuTemp'" class="hud-duet-warning" :class="{'hud-duet-warning--quiet':!hudTemperatureWarning(data,metric.id)}" :aria-hidden="!hudTemperatureWarning(data,metric.id)" :aria-label="hudTemperatureWarning(data,metric.id)?'温度超过90摄氏度':undefined">!</span></span></p>
          <span class="hud-honeycomb-reading-caption">{{groupLabel({name:'CPU',label:'CPU'})}} {{metric.label}}</span>
        </div>
      </section>
    </div>
    <div v-else-if="duetSummary" class="hud-duet" :class="{'hud-duet--unmeasured':!duetAlignment.fontSize}" :style="duetStyle">
      <section v-if="visibleFields.fps" ref="duetHero" class="hud-duet-hero" data-hud-group="帧率">
        <div class="hud-duet-fps" data-hud-metric="fps" :data-fps-state="fpsValid ? 'live' : 'unknown'">
          <span class="hud-fps-label hud-duet-caption">FPS帧数</span>
          <div ref="duetLine" class="hud-duet-line"><strong ref="duetNumber" :class="{'hud-duet-number--missing': !fpsValid}"><template v-if="fpsValid"><span v-for="(digit,index) in hudValue(data,'fps')" :key="index" class="hud-duet-digit">{{digit}}</span></template><span v-else class="hud-duet-placeholder">FPS</span></strong><i class="hud-duet-baseline" aria-hidden="true"></i></div>
        </div>
      </section>
      <section ref="duetReadouts" class="hud-duet-readouts" data-hud-group="CPU">
        <div v-for="metric in duetMetrics" :key="metric.id" class="hud-duet-reading" :data-hud-metric="metric.id">
          <span class="hud-metric-label">{{ groupLabel({name:'CPU',label:'CPU'}) }} {{metric.label}}</span>
          <p class="hud-duet-value" :style="{'--duet-reading-size':27 * textScale * metricScale(metric.id)+'px'}">
            <span class="hud-duet-reserve" aria-hidden="true"><b>{{metric.id==='cpuPower'?'999.9':'999'}}</b><small>{{metric.unit}}</small><span v-if="metric.id==='cpuTemp'" class="hud-duet-warning">!</span></span>
            <span class="hud-duet-live"><b class="hud-duet-number">{{duetValue(metric.id)}}</b><small>{{metric.unit}}</small><span v-if="metric.id==='cpuTemp'" class="hud-duet-warning" :class="{'hud-duet-warning--quiet':!hudTemperatureWarning(data,metric.id)}" :aria-label="hudTemperatureWarning(data,metric.id)?'温度超过90摄氏度':undefined" :aria-hidden="!hudTemperatureWarning(data,metric.id)">!</span></span>
            <i class="hud-duet-baseline" aria-hidden="true"></i>
          </p>
        </div>
      </section>
    </div>
    <template v-else>
    <div v-for="(column, index) in groupColumns" :key="index" class="hud-column">
    <section v-for="group in column" :key="group.key" class="hud-group" :data-hud-group="group.name" :data-hud-section="group.key" :style="[groupStyle(group), group.name === '帧率' ? fpsShellStyle : undefined]">
      <div v-if="group.name === '帧率' && (visibleFields.fps || graphVisible)" class="hud-fps-stack" :class="{ 'hud-fps-stack--graph-only': !visibleFields.fps }">
      <div v-if="visibleFields.fps" class="hud-fps" :class="{ 'hud-fps--missing': !fpsValid }" data-hud-metric="fps" :data-fps-state="fpsValid ? 'live' : 'unknown'"><span class="hud-fps-label">FPS帧数</span><strong :class="{'hud-honeycomb-fps-value':honeycombSegments}"><template v-if="fpsValid"><span :class="{'hud-honeycomb-accessible-value':honeycombSegments}">{{ hudValue(data, 'fps') }}</span><MonitorHoneycombDigits v-if="honeycombSegments" :value="hudValue(data,'fps')" /></template><span v-else class="hud-fps-placeholder">FPS</span></strong></div>
        <div v-if="graphVisible" class="hud-chart hud-chart--fps" data-hud-metric="frametimeGraph">
          <svg viewBox="0 0 240 40" preserveAspectRatio="none" role="img" aria-label="帧时间曲线，单位毫秒" data-curve-state="live"><polyline v-for="(path, i) in framePaths" :key="i" :points="path" fill="none" stroke="var(--hud-accent)" stroke-width="1.5" /></svg>
        </div>
      </div>
      <div v-if="hasContent(group)" class="hud-group-content">
        <div v-if="group.name !== '帧率' && !summary" class="hud-group-label">{{ groupLabel(group) }}</div>
        <div v-if="group.metrics.length" class="hud-values" :class="{ 'hud-values--battery': group.name === '电池', 'hud-values--battery-level': group.name === '电池' && visibleFields.batteryPercent, 'hud-values--battery-pair': group.name === '电池' && visibleFields.batteryPower && visibleFields.batteryTime }">
          <div v-for="metric in group.metrics" :key="metric.id" class="hud-metric" :data-hud-metric="metric.id" :style="{ '--hud-value-scale':metricScale(metric.id), '--hud-battery-factor':textScale * metricScale(metric.id) }" :data-percent-alert="metric.id === 'batteryPercent' ? percentGraphAlert(metric.id) : undefined">
            <span class="hud-metric-label">{{ summary && group.name === 'CPU' ? groupLabel(group) + ' ' + metric.label : metric.label }}</span>
            <template v-if="metric.id === 'batteryPercent' && inlineGraph(metric.id)">
              <div class="hud-percent-graph hud-percent-graph--inline hud-percent-graph--battery" :data-percent-alert="percentGraphAlert(metric.id)" :data-hud-metric="inlineGraph(metric.id)!.id" :data-percent-state="percentValues[metric.id] === null ? 'unknown' : 'live'">
                <div class="hud-battery-track" role="meter" aria-label="剩余电量" aria-valuemin="0" aria-valuemax="100" :aria-valuenow="percentValues[metric.id] ?? undefined" :aria-valuetext="percentValues[metric.id] === null ? '未知' : undefined">
                  <i v-if="percentValues[metric.id] !== null" :style="{ width: percentValues[metric.id] + '%' }"></i><span class="hud-battery-value" aria-hidden="true"><span class="hud-battery-digits" :style="{fontSize:metricScale('batteryPercent')+'em'}">{{ percentValues.batteryPercent === null ? '—' : Math.round(percentValues.batteryPercent ?? 0) }}</span><small v-if="percentValues.batteryPercent !== null">%</small></span>
                </div>
              </div>
            </template>
            <template v-else>
              <span class="hud-number"><span class="hud-value-digits" :class="{ 'hud-fps-low-value': metric.id === 'fps1' }" :style="{ minWidth: hudMetricDigitSlots(metric.id) + 'ch', fontSize:metricScale(metric.id)+'em' }">{{ hudValue(data, metric.id) }}</span><small v-if="metric.unit" :class="{ 'hud-unit--hidden': metric.id === 'fpsLimit' && data.values.fpsLimit === 0 }">{{ metric.unit }}</small><span v-if="metric.id === 'cpuTemp' || metric.id === 'gpuTemp'" class="hud-temp-warning" :class="{ 'hud-temp-warning--inactive': !hudTemperatureWarning(data,metric.id) }" role="img" :aria-hidden="!hudTemperatureWarning(data,metric.id)" :aria-label="hudTemperatureWarning(data,metric.id) ? metric.group + ' 温度超过 90°C' : undefined" :title="hudTemperatureWarning(data,metric.id) ? '温度超过 90°C' : undefined">!</span></span>
              <div v-if="inlineGraph(metric.id)" class="hud-percent-graph hud-percent-graph--inline" :class="{ 'hud-percent-graph--battery': inlineGraph(metric.id)!.id === 'batteryBar' }" :data-percent-alert="percentGraphAlert(metric.id)" :data-hud-metric="inlineGraph(metric.id)!.id" :data-percent-state="percentValues[metric.id] === null ? 'unknown' : 'live'">
                <div :class="inlineGraph(metric.id)!.id === 'batteryBar' ? 'hud-battery-track' : 'hud-percent-track'" role="meter" :aria-label="metric.id === 'fanSpeed' ? 'YMCC 风扇期望转速' : metric.label" aria-valuemin="0" aria-valuemax="100" :aria-valuenow="percentValues[metric.id] ?? undefined" :aria-valuetext="percentValues[metric.id] === null ? '未知' : undefined">
                  <i v-if="percentValues[metric.id] !== null" :style="{ width: percentValues[metric.id] + '%' }"></i>
                </div>
              </div>
            </template>
          </div>
        </div>
        <div v-if="group.name === 'CPU' && visibleFields.cpuCores" class="hud-chart" data-hud-metric="cpuCores">
          <div class="hud-core-bars" :style="{ '--hud-core-cols': coreLayout.columns }" role="img" :aria-label="coreValues.map((v,i) => coreLabel(i)+':'+(v===null?'未知':v+'%')).join('，')">
            <div v-for="(usage, i) in coreValues" :key="i" class="hud-core-track hud-core-tile" :class="{ 'hud-core-tile--unknown': usage === null }" :data-core-index="i" :data-core-state="usage === null ? 'unknown' : 'live'">
              <i v-if="usage !== null" :style="{ height: percentBar(usage) + '%' }"></i>
            </div>
          </div>
        </div>
        <div v-if="group.graphs.length" class="hud-percent-graphs">
          <div v-for="graph in group.graphs" :key="graph.id" class="hud-percent-graph" :class="{ 'hud-percent-graph--battery': graph.id === 'batteryBar' }" :data-percent-alert="percentGraphAlert(graph.value)" :data-hud-metric="graph.id" :data-percent-state="percentValues[graph.value] === null ? 'unknown' : 'live'">
            <span class="hud-percent-label">{{ graph.label }}</span>
            <div :class="graph.id === 'batteryBar' ? 'hud-battery-track' : 'hud-percent-track'" role="meter" :aria-label="graph.id === 'batteryBar' ? '剩余电量' : graph.id === 'fanSpeedBar' ? 'YMCC 风扇期望转速' : graph.label" aria-valuemin="0" aria-valuemax="100" :aria-valuenow="percentValues[graph.value] ?? undefined" :aria-valuetext="percentValues[graph.value] === null ? '未知' : undefined">
              <i v-if="percentValues[graph.value] !== null" :style="{ width: percentValues[graph.value] + '%' }"></i>
            </div>
          </div>
        </div>
      </div>
    </section>
    </div>
    </template>
    <footer v-if="honeycomb && !emptyHidden" class="hud-honeycomb-bottomline"><span class="hud-honeycomb-stripes" aria-hidden="true"><i v-for="i in 7" :key="i"></i></span><span>PERFORMANCE MONITOR</span><span aria-hidden="true">⬡</span></footer>
  </div>
</template>
<style scoped>
.ymcc-hud { position:relative;isolation:isolate;contain:layout paint;color:#f1f5fa;padding:.8em 1em;width:max-content;max-width:none;box-sizing:border-box;font-family:'Segoe UI','Microsoft YaHei UI',sans-serif;line-height:1.35;font-variant-numeric:tabular-nums;pointer-events:none;opacity:1; }
.hud-column { display:contents; }
.hud-group { display:flex;gap:var(--hud-gap);padding:.5em 0;min-width:0;align-self:start; }
.hud-group-content { min-width:0; }.hud-group-label { color:var(--hud-accent);font-size:calc(.78em * var(--hud-text-scale,1));font-weight:600;letter-spacing:.06em;margin-bottom:.4em; }
/* Intrinsic tracks replace the old fixed 22em board and stretching FPS cell. */
.hud-values { display:grid;grid-template-columns:repeat(var(--hud-value-cols),minmax(4em,max-content));gap:.5em .8em; }
.hud-metric { display:flex;flex-direction:column;min-width:0; }.hud-metric-label { font-size:calc(.76em * var(--hud-text-scale,1));color:var(--hud-secondary);white-space:nowrap; }.hud-number { font-size:calc(1em * var(--hud-text-scale,1));white-space:nowrap; }.hud-number small { font-size:.72em;margin-left:.25em;color:var(--hud-secondary); }
.hud-value-digits { display:inline-block;text-align:left;font-variant-numeric:tabular-nums; }
.hud-unit--hidden,.hud-temp-warning--inactive { visibility:hidden; }
.hud-fps-low-value { display:inline-block;min-width:3ch;text-align:left;font-variant-numeric:tabular-nums; }
.hud-fps { display:flex;flex-direction:column;min-width:0;flex:none; }.hud-fps-label { font-size:calc(.8em * var(--hud-text-scale,1));color:var(--hud-accent); }.hud-fps strong { font-size:calc(3.8em * var(--hud-text-scale,1) * var(--hud-fps-value-scale,1));line-height:1.04;font-weight:600;letter-spacing:-.03em;min-width:2ch; }
.hud-fps-stack { display:flex;flex-direction:column;align-items:stretch;align-self:start;min-width:0;flex:none; }
.hud-fps-stack--graph-only { width:10em; }
.hud-fps-stack > .hud-chart--fps { width:0;min-width:100%;margin-top:.28em; }
.hud-fps-stack > .hud-chart--fps svg { height:1.8em; }
.hud-chart { contain:layout paint;margin-top:.55em; }.hud-chart-label { display:flex;justify-content:space-between;gap:.6em;color:var(--hud-secondary);font-size:calc(.72em * var(--hud-text-scale,1));margin-bottom:.35em;white-space:nowrap; }.hud-core-bars { display:grid;grid-template-columns:repeat(var(--hud-core-cols),minmax(0,1fr));gap:.16em;min-width:8em;--hud-core-row-height:1.2em;grid-auto-rows:var(--hud-core-row-height); }
.hud-core-track { position:relative;min-width:0;background:rgba(232,242,252,.065);border:1px solid rgba(232,242,252,.08);border-radius:3px;overflow:hidden;contain:layout paint; }
.hud-core-track i { position:absolute;inset:auto 0 0;display:block;width:100%;background:var(--hud-accent);opacity:.78; }.hud-core-tile--unknown { border-style:dashed;opacity:.5; }
.hud-chart svg { display:block;width:100%;height:2.6em; }.hud-unavailable,.hud-empty { font-size:calc(.8em * var(--hud-text-scale,1));color:var(--hud-secondary); }
/* Focus mode: hero FPS, CPU/GPU peers, then secondary groups. */
.ymcc-hud--compact { display:flex;gap:1.2em;align-items:center; }
.ymcc-hud--compact .hud-group { padding:0;border:0;align-items:center; }
.ymcc-hud--compact .hud-group+.hud-group { padding-left:1.2em;border-left:1px solid rgba(232,242,252,.1); }
.ymcc-hud--compact .hud-values { grid-template-columns:repeat(var(--hud-value-cols),max-content); }
/* Detailed mode: three independent stacks, with memory below CPU and battery below GPU/I/O. */
.ymcc-hud--dense { display:flex;gap:1.2em;align-items:start; }
.ymcc-hud--dense .hud-column { display:flex;flex-direction:column;gap:.35em; }
.ymcc-hud--dense [data-hud-group="帧率"] { flex-direction:column;align-items:stretch; }
.ymcc-hud--dense .hud-group { padding:.35em 0; }
.ymcc-hud--horizontal { width:max-content;max-width:100%;display:flex;flex-wrap:wrap;gap:.5em 1.2em;align-items:center; }
.ymcc-hud--horizontal .hud-group { flex:0 1 auto;max-width:100%;box-sizing:border-box;border-top:0;padding:0; }
.ymcc-hud--horizontal .hud-group+.hud-group { border-left:1px solid rgba(232,242,252,.1);padding-left:1em; }
.ymcc-hud--capsule { padding:.6em .85em;display:grid;grid-template-columns:repeat(2,max-content);gap:.35em 1em; }
.ymcc-hud--capsule [data-hud-group="帧率"] { grid-column:1/-1; }
.ymcc-hud--capsule .hud-metric { flex-direction:row;align-items:baseline;gap:.3em; }
.ymcc-hud--capsule .hud-values { gap:.4em .7em; }
.ymcc-hud--capsule .hud-fps strong { font-size:calc(2.5em * var(--hud-text-scale,1) * var(--hud-fps-value-scale,1)); }
.ymcc-hud--capsule.ymcc-hud--compact { display:flex; }
.ymcc-hud--capsule.ymcc-hud--dense { display:flex; }
.ymcc-hud--capsule.ymcc-hud--dense .hud-metric { flex-direction:column;align-items:stretch;gap:0; }
/* Compact preset: shared title and value rows. Flatten only this three-field
   presentation, so CSS baseline alignment is typographic, not font-box bottom
   alignment. The intrinsic summary bar needs no packing/layout observer. */
.ymcc-hud--summary.ymcc-hud--compact { display:grid;grid-template-columns:none;grid-auto-columns:max-content;grid-template-rows:auto auto;gap:.5em .8em;align-items:baseline;max-width:none; }
.ymcc-hud--summary .hud-group,.ymcc-hud--summary .hud-fps,.ymcc-hud--summary .hud-group-content,.ymcc-hud--summary .hud-values,.ymcc-hud--summary .hud-metric { display:contents; }
.ymcc-hud--summary .hud-fps-label,.ymcc-hud--summary .hud-metric-label { grid-row:1;font-size:calc(.8em * var(--hud-text-scale,1));line-height:1.35;font-weight:500;letter-spacing:.04em;color:var(--hud-accent); }
.ymcc-hud--summary .hud-fps strong,.ymcc-hud--summary .hud-number { grid-row:2;align-self:baseline; }
.ymcc-hud--summary .hud-number { font-size:calc(1.2em * var(--hud-text-scale,1));line-height:1.04; }
.ymcc-hud--summary-split [data-hud-group="CPU"]::before { content:'';grid-column:2;grid-row:1/span 2;align-self:stretch;width:1px;background:rgba(232,242,252,.1); }
/* Eight memory switches: two compact rows, preserving the overall board zoom. */
.ymcc-hud [data-hud-group="内存"] .hud-values { grid-template-columns:repeat(var(--hud-value-cols),minmax(0,max-content)); }
.ymcc-hud--dense [data-hud-group="内存"] .hud-values { font-size:.88em; }
/* Battery scalars use short inline captions. The level spans their two rows;
   without a level, power/time become a single stack with no reserved blank cell.
   These selectors apply to all base layouts and their line styles, including inline capsules. */
.ymcc-hud .hud-values--battery { grid-template-columns:repeat(var(--hud-value-cols),max-content);gap:.3em .8em;align-items:center; }
.ymcc-hud .hud-values--battery [data-hud-metric="batteryPower"],.ymcc-hud .hud-values--battery [data-hud-metric="batteryTime"] { flex-direction:row;align-items:baseline;gap:.35em; }
.ymcc-hud .hud-values--battery-level.hud-values--battery-pair [data-hud-metric="batteryPercent"] { grid-column:1;grid-row:1/span 2;align-self:center; }
.ymcc-hud .hud-values--battery-level.hud-values--battery-pair [data-hud-metric="batteryPower"] { grid-column:2;grid-row:1; }
.ymcc-hud .hud-values--battery-level.hud-values--battery-pair [data-hud-metric="batteryTime"] { grid-column:2;grid-row:2; }
/* All scalar percentages have independent, static graphic counterparts. */
.hud-percent-graphs { display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:.45em .7em;margin-top:.55em; }
.hud-percent-graph { min-width:0; }
.hud-percent-label { display:block;font-size:calc(.7em * var(--hud-text-scale,1));line-height:1.35;color:var(--hud-secondary);white-space:nowrap;margin-bottom:.2em; }
.hud-percent-track { height:.36em;min-width:5em;background:rgba(232,242,252,.09);border-radius:2px;overflow:hidden; }
.hud-percent-track i { display:block;height:100%;background:var(--hud-percent-color,var(--hud-accent)); }
[data-percent-alert="caution"] { --hud-percent-color:#f2c35a; }
[data-percent-alert="warning"] { --hud-percent-color:#ff5b63; }
.hud-metric[data-percent-alert] > .hud-number { color:var(--hud-percent-color); }
.hud-percent-graph[data-percent-state="unknown"] .hud-percent-track { outline:1px dashed rgba(232,242,252,.2);outline-offset:0; }
.hud-battery-track { position:relative;display:flex;align-items:center;justify-content:center;width:3.5em;height:1.35em;min-height:1.35em;border:1px solid var(--hud-percent-color,var(--hud-accent));border-radius:.18em;box-sizing:border-box;overflow:visible; }
.hud-battery-track::after { content:'';position:absolute;left:100%;top:25%;width:.18em;height:50%;border-radius:0 .1em .1em 0;background:var(--hud-percent-color,var(--hud-accent)); }
.hud-battery-track i { position:absolute;left:0;top:0;display:block;height:100%;background:var(--hud-percent-color,var(--hud-accent));opacity:.24;border-radius:.1em;z-index:0; }
.hud-battery-value { position:relative;z-index:1;display:block;color:#f1f5fa;font-size:calc(1em * var(--hud-text-scale,1));font-weight:700;line-height:1;text-shadow:0 1px 2px rgba(0,0,0,.72);pointer-events:none; }
.hud-percent-graph[data-percent-state="unknown"] .hud-battery-track { border-style:dashed;opacity:.5; }
.hud-percent-graph--battery { grid-column:1/-1; }
.hud-temp-warning { display:inline-flex;align-items:center;justify-content:center;width:.85em;height:.85em;margin-left:.3em;border:1px solid #ff5b63;border-radius:50%;color:#ff5b63;font-size:.8em;line-height:1;font-weight:800;vertical-align:baseline; }
.hud-percent-graph--inline { margin-top:.25em; }
.hud-percent-graph--battery.hud-percent-graph--inline { margin-top:0; }
.hud-percent-graph--inline .hud-percent-track { min-width:0; }
.ymcc-hud--capsule .hud-metric:has(> .hud-percent-graph--inline) { flex-direction:column;align-items:stretch;gap:0; }

/* Display-only compositions. No effects that sample/repaint the game
   backdrop: glass is a static tint, not a per-frame backdrop-filter. All panels
   derive alpha from the user's opacity, so 0% has no leftover opaque tiles. */

.ymcc-hud--cinema { padding:.45em .9em;outline:1px solid rgb(225 233 242 / calc(var(--hud-opacity) * .16)); }
.ymcc-hud--cinema .hud-fps strong { font-size:calc(2.7em * var(--hud-text-scale,1) * var(--hud-fps-value-scale,1)); }
.ymcc-hud--cinema .hud-metric-label { font-size:calc(.7em * var(--hud-text-scale,1)); }
.ymcc-hud--cinema .hud-group-label { font-size:calc(.68em * var(--hud-text-scale,1));letter-spacing:.14em; }
.ymcc-hud--cinema .hud-chart svg { height:1.6em; }
.ymcc-hud--cinema .hud-core-bars { --hud-core-row-height:.85em; }

/* Static technology treatments. Inner light stays within the measured board,
   follows opacity and never requires timers, animation or backdrop sampling. */
/* Three luminous columns: hero, processor/memory, graphics/battery. */
/* Side-mounted section titles and narrow circuit traces form two modules. */
/* Slanted corner markers, full-width hero, three independent lower modules. */
/* Horizontal twin light rails, compact FPS and aligned instrument groups. */
/* Paired instrument stacks with corner brackets, no crosshair over the game. */
/* Full-data rectangular stacks keep their existing column arrangement.
   Only gutters change; selected data and the 13px base type remain intact. */
/* Sparse new templates retain intrinsic compact geometry. */
/* Full data does not grow into a tall 720p-clipped grid: use three independent
   stacks without shrinking font/zoom or dropping any selected metric. */
/* The minimal preset retains the same typographic title/value baselines in all
   available templates, even those that flatten/reorder full-data groups differently. */
.ymcc-hud--summary .hud-column { display:contents; }
/* Raster is preprocessed once at import. No filters, blend modes or animation. */
.hud-image-underlay { position:absolute;inset:0;z-index:0;width:100%;height:100%;object-fit:contain;object-position:right bottom;opacity:var(--hud-image-opacity);pointer-events:none;user-select:none;border-radius:inherit; }
.ymcc-hud--image-underlay > :not(.hud-image-underlay) { position:relative;z-index:1; }
/* Emphasized digits are one weight step lighter; neon keeps its softer face. */
/* Shared FPS emphasis overrides template-specific headline sizes. A tight
   line box fills the existing cell rather than creating a padded hero card. */
.ymcc-hud--fps-emphasis .hud-fps { gap:.05em; }
.ymcc-hud--fps-emphasis .hud-fps-label { font-weight:700;line-height:1.1; }
.ymcc-hud--fps-emphasis .hud-fps strong { font-size:calc(var(--hud-fps-size) * var(--hud-text-scale,1) * var(--hud-fps-value-scale,1));font-weight:700;line-height:.94;letter-spacing:-.055em;min-width:0; }
.ymcc-hud--fps-regular .hud-fps strong { font-size:calc(var(--hud-fps-size) * var(--hud-text-scale,1) * var(--hud-fps-value-scale,1));font-weight:600;line-height:1.05;letter-spacing:-.02em;min-width:0; }
/* Same three-digit footprint for 0 / 1 / 99 / 999, with no leading zeroes. */
.ymcc-hud .hud-fps strong { display:block;width:3ch;min-width:3ch;max-width:3ch;flex:none;font-variant-numeric:tabular-nums;text-align:left; }
/* Missing FPS occupies a normal value line at every board scale/template. */
.ymcc-hud .hud-fps strong { height:1.05em; }
.hud-fps-placeholder { display:inline-block;font-size:.42em;letter-spacing:.03em;line-height:1;vertical-align:middle; }

/* Selected vertical layouts use the same admitted fields, charts and meters.
   Width is in em so the existing whole-board zoom remains proportional. */
.ymcc-hud--vertical:not(.ymcc-hud--compact) { width:28em;display:block;padding:1em;outline:1px solid rgb(225 233 242 / calc(var(--hud-opacity) * .18)); }
.ymcc-hud--vertical:not(.ymcc-hud--compact) .hud-column { display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:.65em; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) .hud-group { display:block;grid-column:1/-1;width:100%;padding:.65em .75em;border-radius:.5em;background:rgb(225 233 242 / calc(var(--hud-opacity) * .045));box-sizing:border-box; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) .hud-group-content { width:100%; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) .hud-group-label { margin-bottom:.6em;letter-spacing:.08em; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) .hud-values { grid-template-columns:repeat(var(--hud-value-cols),minmax(0,1fr));gap:.65em .7em; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) .hud-metric { flex-direction:column;align-items:stretch;gap:.1em; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) .hud-metric-label { font-size:calc(.75em * var(--hud-text-scale,1)); }
.ymcc-hud--vertical:not(.ymcc-hud--compact) .hud-number { font-size:calc(1.13em * var(--hud-text-scale,1));line-height:1.2; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) .hud-number small { font-size:.65em; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="帧率"] { display:grid;grid-template-columns:9em minmax(0,1fr);gap:.4em 1em;padding:.25em .25em .55em;background:none;border-radius:0; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="帧率"] .hud-group-content { display:contents; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="帧率"] .hud-values { grid-column:2;display:flex;flex-direction:column;gap:.5em;justify-content:center; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="帧率"] .hud-metric { flex-direction:row;align-items:baseline;justify-content:space-between;gap:.4em; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="帧率"] .hud-chart { grid-column:1/-1;margin-top:.2em; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="帧率"] .hud-chart svg { height:2em; }
.ymcc-hud--vertical.ymcc-hud--fps-emphasis .hud-fps strong { font-weight:550;letter-spacing:-.04em; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) :is([data-hud-metric="cpuUsage"],[data-hud-metric="gpuUsage"]) { grid-column:1/-1;order:2;display:grid;grid-template-columns:1fr auto;align-items:baseline;gap:.2em; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) :is([data-hud-metric="cpuUsage"],[data-hud-metric="gpuUsage"]) .hud-percent-graph { grid-column:1/-1;margin-top:.1em; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) .hud-percent-graph--inline .hud-percent-track { height:.3em;min-width:0; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) .hud-core-bars { --hud-core-row-height:1.25em;gap:.2em;min-width:0; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) :is([data-hud-section="ram"],[data-hud-section="vram"]) { grid-column:auto; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="内存"] .hud-values { font-size:.92em;grid-template-columns:repeat(2,minmax(0,1fr));gap:.65em .35em; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="内存"] .hud-metric-label { font-size:calc(.72em * var(--hud-text-scale,1)); }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="内存"] .hud-number { font-size:calc(1.1em * var(--hud-text-scale,1)); }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="内存"] :is([data-hud-metric="ramUsage"],[data-hud-metric="vramUsage"]) { grid-column:1/-1;display:grid;grid-template-columns:1fr auto;gap:.1em;align-items:baseline; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="内存"] .hud-percent-graph--inline { grid-column:1/-1; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="读写"] .hud-number { font-size:calc(1.05em * var(--hud-text-scale,1)); }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="读写"] .hud-number small { display:block;margin:.15em 0 0; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) .hud-values--battery { grid-template-columns:4.8em minmax(0,1fr); }
.ymcc-hud--vertical:not(.ymcc-hud--compact) .hud-values--battery :is([data-hud-metric="batteryPower"],[data-hud-metric="batteryTime"]) { flex-direction:row;align-items:baseline;justify-content:space-between;gap:.4em; }

/* Full-detail verticals compact gutters, never font size or selected fields. */
.ymcc-hud--vertical.ymcc-hud--full .hud-column { gap:.5em; }
.ymcc-hud--vertical.ymcc-hud--full .hud-group { padding:.45em .6em; }
.ymcc-hud--vertical.ymcc-hud--full .hud-group-label { margin-bottom:.4em; }
.ymcc-hud--vertical.ymcc-hud--full .hud-values { gap:.45em .6em; }
.ymcc-hud--vertical.ymcc-hud--full [data-hud-group="GPU"] .hud-values { grid-template-columns:repeat(4,minmax(0,1fr)); }
.ymcc-hud--vertical.ymcc-hud--full [data-hud-group="内存"] :is([data-hud-metric="ramUsage"],[data-hud-metric="vramUsage"]) { grid-column:auto;display:flex;flex-direction:column;align-items:stretch;gap:.1em; }
.ymcc-hud--vertical.ymcc-hud--full .hud-core-bars { --hud-core-row-height:1.1em; }
.ymcc-hud--vertical.ymcc-hud--full .hud-chart { margin-top:.4em; }

/* Fonts are local stacks. The large FPS numeral has an independent family;
   all other text, digits and units inherit the selected body family. */
.ymcc-hud .hud-fps strong { font-family:var(--hud-fps-font,inherit); }
/* Vertical 02: a borderless numerical ledger, readings on a common baseline. */
.ymcc-hud--verticalLedger:not(.ymcc-hud--compact) { padding:.9em 1em;border-radius:0 !important;outline:0;border-top:2px solid rgb(225 233 242 / calc(var(--hud-opacity) * .55)); }
.ymcc-hud--verticalLedger:not(.ymcc-hud--compact) .hud-group { background:none;border-radius:0;padding:.35em 0 .6em;border-bottom:1px solid rgb(225 233 242 / calc(var(--hud-opacity) * .18)); }
.ymcc-hud--verticalLedger:not(.ymcc-hud--compact) .hud-column { gap:.3em .9em; }
.ymcc-hud--verticalLedger:not(.ymcc-hud--compact) .hud-group-label { font-size:calc(.72em * var(--hud-text-scale,1));letter-spacing:.18em;margin-bottom:.6em; }
.ymcc-hud--verticalLedger:not(.ymcc-hud--compact) :is([data-hud-group="CPU"],[data-hud-group="GPU"],[data-hud-group="读写"]) .hud-values { grid-template-columns:repeat(2,minmax(0,1fr));gap:.5em .8em; }
.ymcc-hud--verticalLedger:not(.ymcc-hud--compact) .hud-metric { flex-direction:row;align-items:baseline;justify-content:space-between;gap:.3em; }
.ymcc-hud--verticalLedger:not(.ymcc-hud--compact) .hud-number { font-size:calc(.96em * var(--hud-text-scale,1)); }
.ymcc-hud--verticalLedger:not(.ymcc-hud--compact) .hud-number small { display:inline;margin-left:.2em; }
.ymcc-hud--verticalLedger:not(.ymcc-hud--compact) .hud-metric:has(> .hud-percent-graph) { flex-wrap:wrap; }
.ymcc-hud--verticalLedger:not(.ymcc-hud--compact) .hud-percent-graph--inline { width:100%; }
.ymcc-hud--verticalLedger:not(.ymcc-hud--compact) .hud-core-track { border-radius:0;border:0; }

.ymcc-hud--verticalLedger:not(.ymcc-hud--compact) [data-hud-group="内存"] .hud-metric { flex-direction:column;align-items:stretch;gap:.15em; }
.ymcc-hud--verticalLedger:not(.ymcc-hud--compact) [data-hud-group="内存"] .hud-metric-label { font-size:calc(.72em * var(--hud-text-scale,1)); }

/* The FPS-family table has one numerical edge and one unit start in every
   vertical layout. Per-metric reservation slots must not left-align readings. */
.ymcc-hud--vertical:not(.ymcc-hud--compact) { width:28em;padding:1em; }
.ymcc-hud--verticalLedger:not(.ymcc-hud--compact) { border-top:0; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="帧率"] { display:grid;grid-template-columns:9em minmax(0,1fr);gap:.4em 1em;padding:0 0 .55em;border:0;background:none;align-items:center; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="帧率"] .hud-values { display:flex;flex-direction:column;gap:.5em; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="帧率"] .hud-metric { display:grid;grid-template-columns:minmax(0,1fr) max-content;align-items:baseline;gap:.4em; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="帧率"] .hud-number { display:grid;grid-template-columns:7ch 2.6em;column-gap:.5em;align-items:baseline;font-size:calc(1.13em * var(--hud-text-scale,1)); }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="帧率"] .hud-value-digits { width:auto;text-align:right;justify-self:end; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="帧率"] .hud-number small { display:block;margin:0;text-align:left;font-size:.65em; }
/* Keep the common FPS grid stronger than each vertical's body layout. */
.ymcc-hud.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="帧率"] { display:grid;grid-template-columns:9em minmax(0,1fr);padding:0 0 .55em;gap:.4em 1em;align-items:center;border:0; }

/* Glyph layout, not a board transform: values enlarge without enlarging their labels/units. */
.hud-group-label { max-width:30em;white-space:normal;overflow-wrap:anywhere;letter-spacing:.03em; }
.hud-battery-value { display:flex;align-items:baseline;justify-content:center;gap:.08em; }
.hud-battery-value small { font-size:.72em; }
.hud-battery-track:has(> .hud-battery-value) { width:max(3.5em,calc(3.5em * var(--hud-battery-factor,1)));height:max(1.35em,calc(1.35em * var(--hud-battery-factor,1))); }
.ymcc-hud--summary .hud-metric-label { white-space:normal;max-width:24em; }

/* Content determines the column width, including independently enlarged numbers. */
.ymcc-hud--vertical:not(.ymcc-hud--compact) { width:max-content;min-width:28em; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) .hud-column { grid-template-columns:repeat(2,minmax(max-content,1fr)); }
.ymcc-hud--vertical:not(.ymcc-hud--compact) .hud-values { grid-template-columns:repeat(var(--hud-value-cols),minmax(max-content,1fr)); }
.ymcc-hud--vertical:not(.ymcc-hud--compact) .hud-metric { min-width:max-content; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="内存"] .hud-values { grid-template-columns:repeat(2,minmax(max-content,1fr)); }
.ymcc-hud--vertical.ymcc-hud--full [data-hud-group="GPU"] .hud-values { grid-template-columns:repeat(4,minmax(max-content,1fr)); }
.ymcc-hud--vertical:not(.ymcc-hud--compact) .hud-values--battery { grid-template-columns:max-content minmax(max-content,1fr); }
.ymcc-hud.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="帧率"] { grid-template-columns:max-content minmax(max-content,1fr); }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="帧率"] .hud-metric { grid-template-columns:max-content max-content; }
.ymcc-hud--vertical:not(.ymcc-hud--compact) [data-hud-group="帧率"] .hud-number { grid-template-columns:max-content 2.6em; }
/* Approved template B: centered FPS and two stacked CPU readings. */
.ymcc-hud.ymcc-hud--duet-summary { display:block;padding:25px 20px;max-width:none; }
.hud-duet { display:grid;column-gap:10px;align-items:stretch; }
.ymcc-hud--duet-summary .hud-duet-hero { grid-column:1;grid-row:1;position:relative;min-width:0;min-height:var(--duet-height); }
.hud-duet-fps { display:block; }
.hud-duet-caption { position:absolute;left:50%;bottom:0;transform:translateX(-50%);white-space:nowrap; }
.hud-duet .hud-duet-caption,.hud-duet-reading .hud-metric-label { color:rgb(227 237 249 / .62);font-size:calc(10px * var(--hud-text-scale));font-weight:450;line-height:1.4;letter-spacing:0; }
.hud-duet-line { position:absolute;display:flex;align-items:baseline;left:var(--duet-left);top:var(--duet-top);width:max-content; }
.hud-duet-line strong { display:flex;align-items:baseline;flex:none;font-family:var(--hud-fps-font);font-size:var(--duet-font-size);font-weight:500;line-height:1.05;letter-spacing:0;color:#f0f4fa;font-variant-numeric:normal; }
.hud-duet-digit { display:inline-block;flex:none;width:var(--duet-cell);text-align:left;letter-spacing:0; }
.hud-duet-placeholder { font-size:.42em;line-height:1.05;letter-spacing:0; }
.hud-duet-baseline { display:inline-block;width:0;height:0;flex:none;align-self:baseline; }
.hud-duet .hud-duet-readouts { grid-column:2;grid-row:1;display:grid;align-content:start;row-gap:21px;border-left:1px solid rgb(225 239 253 / .16);padding-left:18px; }
.hud-duet:not(:has(.hud-duet-hero)) .hud-duet-readouts { grid-column:1;border-left:0;padding-left:0; }
.ymcc-hud--duet-summary .hud-duet-readouts::before { display:none;content:none; }
.hud-duet-reading { display:flex;flex-direction:column;min-width:max-content; }
.hud-duet-reading .hud-metric-label { order:2;margin-top:5px;text-align:center; }
.hud-duet-value { position:relative;display:flex;justify-content:center;align-items:baseline;white-space:nowrap;margin:0; }
.hud-duet-value b { display:inline-block;flex:none;font-size:var(--duet-reading-size);font-weight:550;line-height:1.1;letter-spacing:0;font-variant-numeric:tabular-nums;color:#f0f4fa; }
.hud-duet-reserve { display:flex;align-items:baseline;gap:5px;visibility:hidden;pointer-events:none; }
.hud-duet-live { position:absolute;left:50%;top:0;transform:translateX(-50%);display:flex;align-items:baseline;gap:5px;white-space:nowrap; }
.hud-duet-live>.hud-duet-warning { position:absolute;left:calc(100% + 5px);bottom:0; }
.hud-duet-number { white-space:nowrap; }
.hud-duet-warning--quiet { visibility:hidden; }
.hud-duet-value small { font-size:calc(11px * var(--hud-text-scale));font-weight:450;color:rgb(232 241 253 / .72);line-height:1.3; }
.hud-duet--unmeasured .hud-duet-line { left:50%;top:4px;transform:translateX(-50%); }
.hud-duet--unmeasured .hud-duet-caption { left:50%; }
.hud-duet-warning { color:#ff6575;font-size:calc(12px * var(--hud-text-scale));font-weight:750; }


/* 蜂窝监控: static decoration over the existing data/visibility pipeline. */
.ymcc-hud--honeycomb { contain:layout;display:grid;grid-template-columns:repeat(2,minmax(max-content,1fr));gap:16px 25px;padding:calc(25px * var(--hud-honeycomb-text)) calc(33px * var(--hud-honeycomb-text)) calc(19px * var(--hud-honeycomb-text)) calc(37px * var(--hud-honeycomb-text));border-radius:0!important;outline:0;color:var(--hud-accent);--hud-secondary:color-mix(in srgb,var(--hud-accent) 48%,#b09c92); }
.ymcc-hud--honeycomb:has(.hud-honeycomb-topline) { min-width:calc(560px * var(--hud-honeycomb-text)); }
.ymcc-hud--honeycomb .hud-column { display:contents; }
.ymcc-hud--honeycomb .hud-group { padding:0 0 10px;border:0;border-bottom:1px solid color-mix(in srgb,var(--hud-accent) 20%,transparent);background:none;align-items:start; }
.ymcc-hud--honeycomb [data-hud-group="帧率"].hud-group { grid-column:1/-1;display:flex;flex-direction:row;align-items:center;gap:24px; }
.ymcc-hud--honeycomb .hud-fps { display:flex;flex-direction:column;align-items:center;gap:9px; }
.ymcc-hud--honeycomb .hud-fps-label { order:2;font-weight:450;line-height:16px;color:var(--hud-secondary); }
.ymcc-hud--honeycomb .hud-values { gap:9px 16px; }
.ymcc-hud--honeycomb .hud-number,.ymcc-hud--honeycomb .hud-duet-value b { color:var(--hud-accent); }
.hud-honeycomb-topline { grid-column:1/-1;display:flex;justify-content:space-between;align-items:center;gap:12px;padding-bottom:calc(13px * var(--hud-honeycomb-text));border-bottom:1px solid color-mix(in srgb,var(--hud-accent) 23%,transparent); }
.hud-honeycomb-identity { display:flex;align-items:center;gap:calc(10px * var(--hud-honeycomb-text)); }
.hud-honeycomb-identity i { width:12px;height:14px;background:var(--hud-accent);clip-path:polygon(25% 0,75% 0,100% 50%,75% 100%,25% 100%,0 50%); }
.hud-honeycomb-identity b { font-size:calc(17px * var(--hud-honeycomb-text));font-weight:650;line-height:1.2;letter-spacing:.06em; }
.hud-honeycomb-identity>span,.hud-honeycomb-status { color:var(--hud-secondary);font-size:calc(9px * var(--hud-honeycomb-text));letter-spacing:.11em;white-space:nowrap; }
.hud-honeycomb-status { color:var(--hud-accent); }
.ymcc-hud--honeycomb.ymcc-hud--honeycomb-summary { display:block; }
.hud-honeycomb-primary { display:grid;grid-template-columns:calc(280px * var(--hud-honeycomb-text) * var(--hud-fps-value-scale)) max-content;column-gap:calc(22px * var(--hud-honeycomb-text));align-items:stretch;margin-top:calc(18px * var(--hud-honeycomb-text)); }
.ymcc-hud--honeycomb .hud-honeycomb-fps { grid-column:1;grid-row:1;display:flex;flex-direction:column;align-items:center;justify-content:center;min-height:calc(143px * var(--hud-honeycomb-text));min-width:0;padding-right:calc(12px * var(--hud-honeycomb-text)); }
.hud-honeycomb-fps-value { position:relative;display:flex;align-items:center;justify-content:center;flex:none;width:calc(210px * var(--hud-honeycomb-text) * var(--hud-fps-value-scale));min-width:calc(210px * var(--hud-honeycomb-text) * var(--hud-fps-value-scale));height:var(--hud-honeycomb-fps-height);font-family:var(--hud-fps-font);font-size:calc(var(--hud-honeycomb-fps-height) * .9);line-height:1;font-weight:500;letter-spacing:0;color:var(--hud-accent); }
.ymcc-hud--honeycomb .hud-fps strong.hud-honeycomb-fps-value { font-size:calc(var(--hud-honeycomb-fps-height) * .9);line-height:1;letter-spacing:0; }
.hud-honeycomb-accessible-value { position:absolute;width:1px;height:1px;clip-path:inset(50%);overflow:hidden;white-space:nowrap; }
.hud-honeycomb-placeholder,.ymcc-hud--honeycomb .hud-fps-placeholder { font-size:.48em;line-height:1;letter-spacing:.055em; }
.hud-honeycomb-fps-caption { font-size:calc(11px * var(--hud-honeycomb-text));line-height:16px;letter-spacing:.055em;color:var(--hud-secondary);margin-top:calc(9px * var(--hud-honeycomb-text)); }
.hud-honeycomb-readouts { grid-column:2;grid-row:1;display:grid;gap:calc(14px * var(--hud-honeycomb-text));border-left:1px solid color-mix(in srgb,var(--hud-accent) 26%,transparent);padding-left:calc(21px * var(--hud-honeycomb-text));align-content:center;min-width:calc(188px * var(--hud-honeycomb-text)); }
.hud-honeycomb-primary:not(:has(.hud-honeycomb-fps)) { grid-template-columns:max-content; }
.hud-honeycomb-primary:not(:has(.hud-honeycomb-fps)) .hud-honeycomb-readouts { grid-column:1;border-left:0;padding-left:0; }
.ymcc-hud--honeycomb .hud-honeycomb-readouts::before { display:none;content:none; }
.hud-honeycomb-reading { text-align:center;min-width:0; }
.hud-honeycomb-reading .hud-duet-value b { font-weight:500; }
.hud-honeycomb-reading .hud-duet-value small { font-size:calc(11px * var(--hud-honeycomb-text));color:var(--hud-secondary); }
.hud-honeycomb-reading-caption { display:block;margin-top:calc(4px * var(--hud-honeycomb-text));font-size:calc(10px * var(--hud-honeycomb-text));line-height:14px;color:var(--hud-secondary); }
.hud-honeycomb-bottomline { grid-column:1/-1;display:flex;align-items:center;gap:12px;margin-top:calc(18px * var(--hud-honeycomb-text));font:500 calc(8px * var(--hud-honeycomb-text))/1 Consolas,monospace;letter-spacing:.15em;color:var(--hud-secondary); }
.hud-honeycomb-bottomline>span:nth-child(2) { margin-left:auto; }
.hud-honeycomb-bottomline>span:last-child { font-size:18px;line-height:10px; }
.hud-honeycomb-stripes { display:flex;gap:3px; }
.hud-honeycomb-stripes i { display:block;width:14px;height:4px;background:var(--hud-accent);transform:skewX(-28deg); }
.hud-honeycomb-stripes i:nth-last-child(-n+3) { opacity:.35; }
.ymcc-hud--honeycomb .hud-image-underlay { border-radius:0; }

</style>
