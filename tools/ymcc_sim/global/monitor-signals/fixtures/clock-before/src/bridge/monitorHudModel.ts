import { HUD_FPS_FAMILY } from './monitorHudFpsVisibility';
import { boundedHudFreshUntil, type HudFreshUntil } from './monitorHudFreshness';
import { coreUsageValues, coreUsageLabels, freezeCoreUsageValues, freezeCoreUsageLabels, MAX_VISIBLE_CORE_TILES } from './monitorCoreTiles';
import type { TdpRuntimeSnapshot } from './tdpRuntimeState';
// Display-only model shared by the YMCC settings preview and future Decky carrier.
// No hardware reads, timers, bridge initialization or RTSS sensor formulas here.
import type { MonitorSnapshotView } from './monitorData';
import { hasFrameTimeCurve, presentedFrameTime, boundedFrameTimeValues, hasExplicitFpsMetadata, fpsDisplayFresh, fps1DisplayFresh, MONITOR_MISSING_GRACE_MS } from './monitorFrameTime';
export type MonitorOutput = 'steam' | 'ymcc' | 'rtss';
export type RtssMonitorTemplate = 'W' | 'L' | 'J';
export const RTSS_MONITOR_TEMPLATES = [{ value: 'W', label: '横版监控' }, { value: 'L', label: '竖版监控' }, { value: 'J', label: '极端简单' }];
export type HudLayout = 'corner' | 'bar' | 'capsule' | 'card' | 'dashboard' | 'glass' | 'split' | 'cinema' | 'neon' | 'circuit' | 'prism' | 'holo' | 'tactical';
export const MONITOR_OUTPUTS = [
  { value: 'desktop', label: '自定义监控' }, { value: 'rtss', label: 'RTSS监控' },
];
// The public choices currently use the desktop HUD or RTSS; optional Steam routing stays internal.
// Legacy native Steam output remains an internal coordinator state for shutdown/compatibility.
export function selectableMonitorOutput(raw: unknown): MonitorOutputPreferences {
  const prefs = normalizeMonitorOutput(raw);
  return prefs.output === 'rtss' ? prefs : { ...prefs, output: 'ymcc', ymccCarrier: 'desktop' };
}
export function monitorOutputChoice(prefs: MonitorOutputPreferences): string {
  return prefs.output === 'rtss' ? 'rtss' : 'desktop';
}
export const HUD_LAYOUTS: Array<{ value: HudLayout; label: string }> = [
  { value: 'corner', label: '重点模式' },
  { value: 'bar', label: '横向信息条' },
  { value: 'capsule', label: '轻量胶囊' },
  { value: 'card', label: '信息卡片' },
  { value: 'dashboard', label: '仪表盘' },
  { value: 'glass', label: '玻璃浮层' },
  { value: 'split', label: '左右分栏' },
  { value: 'cinema', label: '电影黑条' },
  { value: 'neon', label: '霓光矩阵' },
  { value: 'circuit', label: '电路面板' },
  { value: 'prism', label: '棱镜切角' },
  { value: 'holo', label: '全息光轨' },
  { value: 'tactical', label: '战术座舱' },
];
export function isHorizontalHudLayout(layout: HudLayout): boolean {
  return layout === 'bar' || layout === 'cinema' || layout === 'holo';
}
export const HUD_POSITIONS = [
  { value: 1, label: '左上角' }, { value: 5, label: '顶部居中' }, { value: 2, label: '右上角' },
  { value: 3, label: '右下角' }, { value: 6, label: '底部居中' }, { value: 4, label: '左下角' },
];
export const HUD_METRICS = [
  { id: 'fps', group: '帧率', label: 'FPS', unit: 'FPS', digits: 0, ready: true },
  { id: 'fpsLimit', group: '帧率', label: '帧率上限', unit: 'FPS', digits: 2, ready: true },
  { id: 'fps1', group: '帧率', label: '1% Low', unit: 'FPS', digits: 0, ready: true },
  { id: 'frametime', group: '帧率', label: '帧时间', unit: 'ms', digits: 1, ready: true },
  { id: 'frametimeGraph', group: '帧率', label: '帧时间曲线', unit: 'ms', digits: 1, ready: true },
  { id: 'tdpLimit', group: 'CPU', label: 'TDP 上限', unit: 'W', digits: 0, ready: true },
  { id: 'cpuPower', group: 'CPU', label: '功耗', unit: 'W', digits: 1, ready: true },
  { id: 'cpuTemp', group: 'CPU', label: '温度', unit: '°C', digits: 0, ready: true },
  { id: 'cpuUsage', group: 'CPU', label: '总占用', unit: '%', digits: 0, ready: true },
  { id: 'cpuUsageBar', group: 'CPU', label: '总占用图形', unit: '%', digits: 0, ready: true },
  { id: 'cpuPeakBar', group: 'CPU', label: '最高核心负载图形', unit: '%', digits: 0, ready: false },
  { id: 'cpuClock', group: 'CPU', label: '频率', unit: 'GHz', digits: 2, ready: true },
  { id: 'cpuVoltage', group: 'CPU', label: '电压', unit: 'V', digits: 3, ready: true },
  { id: 'fanSpeed', group: 'CPU', label: '风扇转速', unit: '%', digits: 0, ready: true },
  { id: 'fanSpeedBar', group: 'CPU', label: '风扇转速图形', unit: '%', digits: 0, ready: true },
  { id: 'cpuCores', group: 'CPU', label: '核心负载图块（最多16项）', unit: '%', digits: 0, ready: true },
  { id: 'gpuUsage', group: 'GPU', label: '利用率', unit: '%', digits: 0, ready: true },
  { id: 'gpuBar', group: 'GPU', label: '利用率图形', unit: '%', digits: 0, ready: true },
  { id: 'gpuPower', group: 'GPU', label: '功耗', unit: 'W', digits: 1, ready: true },
  { id: 'gpuTemp', group: 'GPU', label: '温度', unit: '°C', digits: 0, ready: true },
  { id: 'gpuClock', group: 'GPU', label: '频率', unit: 'GHz', digits: 2, ready: true },
  { id: 'gpuVoltage', group: 'GPU', label: '电压', unit: 'V', digits: 3, ready: true },
  { id: 'vramUsed', group: '内存', label: '显存已用', unit: 'GB', digits: 1, ready: true },
  { id: 'vramTotal', group: '内存', label: '显存总量', unit: 'GB', digits: 1, ready: true },
  { id: 'vramAvailable', group: '内存', label: '显存可用', unit: 'GB', digits: 1, ready: true },
  { id: 'vramUsage', group: '内存', label: '显存使用率', unit: '%', digits: 0, ready: true },
  { id: 'vramUsageBar', group: '内存', label: '显存使用率图形', unit: '%', digits: 0, ready: true },
  { id: 'ramUsed', group: '内存', label: '内存已用', unit: 'GB', digits: 1, ready: true },
  { id: 'ramTotal', group: '内存', label: '内存总量', unit: 'GB', digits: 1, ready: true },
  { id: 'ramAvailable', group: '内存', label: '内存可用', unit: 'GB', digits: 1, ready: true },
  { id: 'ramUsage', group: '内存', label: '内存使用率', unit: '%', digits: 0, ready: true },
  { id: 'ramUsageBar', group: '内存', label: '内存使用率图形', unit: '%', digits: 0, ready: true },
  { id: 'netDown', group: '读写', label: '下载速率', unit: 'MB/s', digits: 1, ready: true },
  { id: 'netUp', group: '读写', label: '上传速率', unit: 'MB/s', digits: 1, ready: true },
  { id: 'diskRead', group: '读写', label: '文件读取', unit: 'MB/s', digits: 1, ready: true },
  { id: 'diskWrite', group: '读写', label: '文件写入', unit: 'MB/s', digits: 1, ready: true },
  { id: 'batteryPercent', group: '电池', label: '剩余电量', unit: '%', digits: 0, ready: true },
  { id: 'batteryBar', group: '电池', label: '电量图块', unit: '%', digits: 0, ready: true },
  { id: 'batteryPower', group: '电池', label: '功率', unit: 'W', digits: 1, ready: true },
  { id: 'batteryTime', group: '电池', label: '时长', unit: 'min', digits: 0, ready: true },
  { id: 'clock', group: '附加', label: '当前时间', unit: '', digits: 0, ready: true },
] as const;
export type HudMetricId = typeof HUD_METRICS[number]['id'];
// The removed numeric CPU-peak control remains an internal source for its
// independently saved graphic switch; it is never a visible numeric field.
export type HudScalarId = HudMetricId | 'cpuPeak';
// Graphs are presentation choices, not new sampled values. Both forms consume
// the exact same admitted scalar and its own freshness lease.
export const HUD_PERCENT_GRAPHS = [
  { id: 'cpuUsageBar', value: 'cpuUsage', label: '总占用' },
  { id: 'cpuPeakBar', value: 'cpuPeak', label: '最高核心负载' },
  { id: 'fanSpeedBar', value: 'fanSpeed', label: '风扇转速' },
  { id: 'gpuBar', value: 'gpuUsage', label: '利用率' },
  { id: 'vramUsageBar', value: 'vramUsage', label: '显存使用率' },
  { id: 'ramUsageBar', value: 'ramUsage', label: '内存使用率' },
  { id: 'batteryBar', value: 'batteryPercent', label: '剩余电量' },
] as const;
export function hudMetricControlLabel(metric: typeof HUD_METRICS[number]): string {
  return HUD_PERCENT_GRAPHS.some(graph => graph.value === metric.id) ? metric.label + '（百分比）' : metric.label;
}
export function hudPercentValue(data: HudData, id: HudScalarId): number | null {
  const value=data.values[id];
  return !data.stale && typeof value==='number' && Number.isFinite(value) && value>=0 && value<=100 ? value : null;
}
export function hudBatteryText(data: HudData): string {
  const value = hudPercentValue(data, 'batteryPercent');
  return value === null ? '—' : `${value.toFixed(0)}%`;
}
export function hudTemperatureWarning(data: HudData, id: HudMetricId): boolean {
  const value=data.values[id];
  return (id==='cpuTemp'||id==='gpuTemp') && !data.stale && typeof value==='number' && Number.isFinite(value) && value>90;
}
const metricById = Object.fromEntries(HUD_METRICS.map(metric => [metric.id, metric])) as Record<HudMetricId, typeof HUD_METRICS[number]>;
const clockFormatter = new Intl.DateTimeFormat('zh-CN', { hour: '2-digit', minute: '2-digit', hour12: false });
export type HudMetricToggles = Record<HudMetricId, boolean>;
// Only bounded, offline raster payloads are allowed; SVG and remote URLs are rejected.
// Two bounded immutable strings cover a custom underlay and a current-game LOGO.
// Repeated packets reuse the validation result; changed input still passes the
// same raster admission. Invalid/oversized inputs never enter this cache.
let admittedUnderlay = '', previousUnderlay = '';
export function normalizeHudUnderlayImage(value: unknown): string {
  if (typeof value !== 'string' || !value || value.length > 45000) return '';
  if (value === admittedUnderlay || value === previousUnderlay) return value;
  if (!/^data:image\/(?:webp|png|jpeg);base64,[A-Za-z0-9+/]+={0,2}$/.test(value)) return '';
  previousUnderlay = admittedUnderlay; admittedUnderlay = value;
  return value;
}
export const HUD_UNDERLAY_PRESETS = [
  { value: 'none', label: '不使用' },
  { value: 'tifa', label: 'Tifa Lockhart' },
  { value: 'ac8-pink', label: 'Tasha Seversky' },
  { value: 'game-logo', label: '当前游戏 LOGO' },
  { value: 'custom', label: '自定义图片' },
] as const;
export type HudUnderlayPreset = typeof HUD_UNDERLAY_PRESETS[number]['value'];
export const HUD_CROSSHAIRS = [
  { value: 'none', label: '关闭' }, { value: 'dot', label: '圆点' },
  { value: 'cross', label: '细十字' }, { value: 'gap', label: '空心十字' },
  { value: 'ring', label: '圆环' }, { value: 'ring-dot', label: '圆环中心点' },
] as const;
export type HudCrosshair = typeof HUD_CROSSHAIRS[number]['value'];
export interface HudAppearance {
  crosshair?: HudCrosshair;
  position: number; scale: number; saturation: number; opacity: number;
  layout: HudLayout; radius: number; gap: number; hideUnavailable?: boolean; fpsEmphasis?: boolean; underlay?: 'none' | HudUnderlayPreset; underlayPreset?: HudUnderlayPreset; underlayImage?: string; underlayOpacity?: number;
}
export const YMCC_HUD_CARRIERS = [
  { value: 'desktop', label: '本地全局（桌面/游戏）' },
  { value: 'steam-preferred', label: 'Steam 内部优先，可用时切换' },
] as const;
export type YmccHudCarrier = typeof YMCC_HUD_CARRIERS[number]['value'];
export interface MonitorOutputPreferences {
  version: 1; output: MonitorOutput; enabled: boolean; steamPosition: number;
  ymccCarrier: YmccHudCarrier;
  ymcc: HudAppearance & { fields: HudMetricToggles; dataPresetVersion: 1; dataPreset: HudDataPresetId; dataPresetFields: Record<HudDataPresetId, HudMetricToggles>; fieldGroup: string }; rtssScale: number; rtssTemplate: RtssMonitorTemplate;
}
export interface HudData {
  source: 'fixture' | 'ymcc-uniform'; stale: boolean; freshUntil?: HudFreshUntil; batteryPowerEstimated?: boolean; batteryTimeEstimated?: boolean;
  // Observation/target proofs are copied from the sole publisher for display entrance.
  fpsObservedAt?: number; fpsTarget?: string;
  gameLogo?: string; // Runtime-derived image, never a persisted preference.
  values: Partial<Record<HudScalarId, number | string | null>>;
  coreUsage: Array<number | null>; coreLabels?: string[]; coreClocks: Array<number | null>; frameTimes: Array<number | null>;
}
export const HUD_DATA_PRESETS = [{ value: 'simple', label: '简约' }, { value: 'minimal', label: '精简' }, { value: 'standard', label: '标准' }, { value: 'detailed', label: '详细' }] as const;
export type HudDataPresetId = typeof HUD_DATA_PRESETS[number]['value'];
// User's reference selection (2026-10-14); values remain live YMCC data.
const simple: HudMetricId[] = [
  'fps','fpsLimit','fps1','frametimeGraph',
  'tdpLimit','cpuPower','cpuTemp','cpuClock','cpuCores',
  'gpuUsage','gpuBar','gpuPower','gpuClock',
  'vramUsage','vramUsageBar','ramUsage','ramUsageBar',
  'batteryPercent','batteryBar','batteryPower','batteryTime',
];
export const HUD_DEFAULT_APPEARANCE = {layout: 'capsule', position: 1, scale: 1, saturation: 1, opacity: .8, radius: 10, gap: 8, fpsEmphasis: true, hideUnavailable: true, underlay: 'ac8-pink', underlayPreset: 'ac8-pink', underlayOpacity: .25} as const;
const minimal: HudMetricId[] = ['fps','cpuPower','cpuTemp'];
// Standard = the current YMCC reference layout: FPS curve, CPU/GPU core
// readings, memory usage graphics and battery block. All ten visual templates
// consume this same field set; the template changes arrangement only.
const standard: HudMetricId[] = [
  'fps','fpsLimit','fps1','frametimeGraph',
  'tdpLimit','cpuPower','cpuTemp','cpuUsage','cpuClock','cpuVoltage','cpuCores',
  'gpuUsage','gpuBar','gpuPower','gpuTemp','gpuClock',
  'vramUsed','ramUsed','vramUsageBar','ramUsageBar',
  'batteryPercent','batteryBar','batteryPower','batteryTime',
];
const presetIds: Record<HudDataPresetId, readonly HudMetricId[]> = {simple,minimal,standard,detailed:HUD_METRICS.filter(metric => metric.id !== 'clock').map(metric => metric.id)};
const presetFields = Object.fromEntries(HUD_DATA_PRESETS.map(p=>[p.value,Object.fromEntries(HUD_METRICS.map(m=>[m.id,presetIds[p.value].includes(m.id)]))])) as Record<HudDataPresetId,HudMetricToggles>;
export function hudDataPresetFields(id: HudDataPresetId): HudMetricToggles { return { ...presetFields[id] }; }
export function detectHudDataPreset(fields: HudMetricToggles): HudDataPresetId | 'custom' {
  return HUD_DATA_PRESETS.find(p=>HUD_METRICS.every(m=>fields[m.id]===presetFields[p.value][m.id]))?.value ?? 'custom';
}
function record(value: unknown): Record<string, unknown> {
  return value && typeof value === 'object' && !Array.isArray(value) ? value as Record<string, unknown> : {};
}
function bounded(value: unknown, fallback: number, min: number, max: number): number {
  return typeof value === 'number' && Number.isFinite(value) ? Math.min(max, Math.max(min, value)) : fallback;
}
function position(value: unknown, fallback: number): number {
  return HUD_POSITIONS.some(p => p.value === value) ? value as number : fallback;
}
export function normalizeMonitorOutput(raw?: unknown): MonitorOutputPreferences {
  const r = record(raw), y = record(r.ymcc), fields = record(y.fields);
  const toggles = {} as HudMetricToggles;
  const initialPreset = HUD_DATA_PRESETS.find(preset => preset.value === y.dataPreset)?.value ?? (Object.keys(fields).length ? 'standard' : 'simple');
  for (const metric of HUD_METRICS) toggles[metric.id] = typeof fields[metric.id] === 'boolean' ? fields[metric.id] as boolean : presetFields[initialPreset][metric.id];
  // Older saved battery-number selections gain the requested battery graphic;
  // explicit new batteryBar=false remains an independent user choice.
  if (typeof fields.batteryBar !== 'boolean' && fields.batteryPercent === true) toggles.batteryBar = true;
  const underlayImage = normalizeHudUnderlayImage(y.underlayImage);
  // Picture selection determines output; the legacy field is derived for existing renderers.
  const legacyPreset = HUD_UNDERLAY_PRESETS.find(item => item.value === y.underlay)?.value ?? (underlayImage ? 'custom' : HUD_DEFAULT_APPEARANCE.underlayPreset);
  const underlayPreset = HUD_UNDERLAY_PRESETS.find(item => item.value === y.underlayPreset)?.value ?? legacyPreset;
  const underlay: HudUnderlayPreset = underlayPreset === 'custom' && !underlayImage ? 'none' : underlayPreset;
  const inferredPreset = detectHudDataPreset(toggles);
  const oldDetailed = y.dataPresetVersion !== 1 && HUD_METRICS.every(metric => toggles[metric.id]);
  const dataPreset = HUD_DATA_PRESETS.find(preset => preset.value === y.dataPreset)?.value ?? (oldDetailed ? 'detailed' : inferredPreset === 'custom' ? 'standard' : inferredPreset);
  const dataPresetFields = {} as Record<HudDataPresetId, HudMetricToggles>;
  const savedPresets = record(y.dataPresetFields);
  for (const preset of HUD_DATA_PRESETS) {
    const saved = record(savedPresets[preset.value]);
    const selected = {} as HudMetricToggles;
    for (const metric of HUD_METRICS) selected[metric.id] = typeof saved[metric.id] === 'boolean'
      ? saved[metric.id] as boolean : presetFields[preset.value][metric.id];
    dataPresetFields[preset.value] = selected;
  }
  // Older flat fields seed the active preset; explicit false stays false.
  if (Object.keys(fields).length) dataPresetFields[dataPreset] = { ...toggles };
  else Object.assign(toggles, dataPresetFields[dataPreset]);
  // Apply the requested detailed-preset change once. Subsequent manual clock
  // choices are saved normally and remain independent of the reset defaults.
  if (y.dataPresetVersion !== 1) {
    dataPresetFields.detailed.clock = false;
    if (dataPreset === 'detailed') toggles.clock = false;
  }
  const fieldGroup = HUD_METRICS.some(metric => metric.group === y.fieldGroup) ? y.fieldGroup as string : 'CPU';
  return {
    version: 1, output: r.output === 'steam' || r.output === 'rtss' || r.output === 'ymcc' ? r.output : 'ymcc',
    ymccCarrier: r.ymccCarrier === 'steam-preferred' ? 'steam-preferred' : 'desktop',
    enabled: typeof r.enabled === 'boolean' ? r.enabled : true, steamPosition: position(r.steamPosition, 1),
    rtssTemplate: r.rtssTemplate === 'L' || r.rtssTemplate === 'J' ? r.rtssTemplate : 'W',
    rtssScale: Math.round(bounded(r.rtssScale, 100, 20, 240) / 20) * 20,
    ymcc: { position: position(y.position, 1), scale: bounded(y.scale, 1, .2, 2), saturation: bounded(y.saturation, 1, 0, 1),
      opacity: bounded(y.opacity, HUD_DEFAULT_APPEARANCE.opacity, 0, 1), layout: HUD_LAYOUTS.some(item => item.value === y.layout) ? y.layout as HudLayout : HUD_DEFAULT_APPEARANCE.layout,
      radius: bounded(y.radius, 10, 0, 18), gap: bounded(y.gap, 8, 4, 16), crosshair: HUD_CROSSHAIRS.find(item => item.value === y.crosshair)?.value ?? 'none', hideUnavailable: typeof y.hideUnavailable === 'boolean' ? y.hideUnavailable : true, fpsEmphasis: y.fpsEmphasis !== false, underlay, underlayPreset, underlayImage, underlayOpacity: bounded(y.underlayOpacity, HUD_DEFAULT_APPEARANCE.underlayOpacity, 0, 1), fields: toggles, dataPresetVersion: 1, dataPreset, dataPresetFields, fieldGroup },
  };
}
export function makeHudFixture(): HudData {
  return { source: 'fixture', stale: false, values: {
    fps: 117, fpsLimit: 120, fps1: 89, tdpLimit: 45, frametime: 8.5, cpuPower: 28.4, cpuTemp: 67, cpuUsage: 43, cpuPeak: 92, cpuClock: 4.35, cpuVoltage: 1.155, gpuVoltage: .875, fanSpeed: 76,
    gpuUsage: 86, gpuPower: 112.6, gpuTemp: 61, gpuClock: 2.48, vramUsed: 7.8, vramTotal: 12,
    ramUsed: 18.4, ramTotal: 32, ramAvailable: 13.6, ramUsage: 57.5, vramAvailable: 4.2, vramUsage: 65, netDown: 4.2, netUp: .3, diskRead: 38.5, diskWrite: 1.7,
    batteryPercent: 76, batteryPower: -32.4, batteryTime: 94, clock: '17:28',
  }, coreUsage: [31, 54, 24, 78, 35, 92, 48, 61, 27, 43, 18, 56, 32, 71, 24, 44],
    coreClocks: [4.35, 4.41, 4.21, 4.48, 4.29, 4.52, 4.36, 4.39, 3.72, 3.68, 3.51, 3.7, 3.61, 3.76, 3.55, 3.65],
    frameTimes: [8.2, 8.3, 8.7, 8.4, 8.3, 9.2, 8.5, 8.4, 8.9, 8.1, 8.5, 8.6, 9.4, 8.5, 8.2, 8.5, 10.3, 8.7, 8.4, 8.5, 8.8, 8.2, 8.4, 8.5] };
}
function validVoltage(value: unknown): number | null { return typeof value === 'number' && Number.isFinite(value) && value >= .05 && value <= 3 ? value : null; }
function validNumber(value: unknown, allowNegative = false): number | null {
  return typeof value === 'number' && Number.isFinite(value) && (allowNegative || value >= 0) ? value : null;
}
// Physical RAM / dedicated VRAM only. Virtual committed MB must not stand in.
function memoryPairGb(used: unknown, total: unknown, available?: unknown, load?: unknown) {
  let u=validNumber(used),t=validNumber(total),a=validNumber(available);let l=validNumber(load);if(l!==null&&l>100)l=null;
  if(t!==null&&((u!==null&&u>t)||(a!==null&&a>t))){u=null;a=null;l=null;}
  else if(u!==null&&a!==null){const sum=u+a;if(t!==null&&Math.abs(sum-t)>Math.max(1,t*.001)){u=a=l=null;}else if(t===null)t=sum;}
  else if(u!==null&&t!==null)a=t-u;
  else if(a!==null&&t!==null)u=t-a;
  // A direct load may differ by rounding; a gross contradiction cannot drive
  // the warning bar. Derive only from the admitted coherent capacity tuple.
  if(u!==null&&t!==null&&t>0){const derived=u/t*100;if(l===null||Math.abs(l-derived)>1)l=derived;}
  return {used:u===null?null:u/1024,total:t===null?null:t/1024,available:a===null?null:a/1024,load:l};
}
// Only maps fields that actually exist in YMCC's unified snapshot. Missing extended
// metrics remain null. In particular 1000/FPS is NOT substituted for real frametime.
export function hudFromUniformSnapshot(snapshot: MonitorSnapshotView | null, now = Date.now(), tdpState: TdpRuntimeSnapshot | null = null): HudData {
  const values: HudData['values'] = {};
  const top = snapshot?.top, fps = snapshot?.fps;
  const topFresh = !!top && !snapshot?.hwDown && !top.hwDown && top.ts > 0 && now - top.ts >= 0 && now - top.ts < 6000;
  const fpsFresh = fpsDisplayFresh(fps, now, snapshot?.hwDown === true);
  // Windows-backed per-processor data may remain fresh while HWiNFO is down.
  // Only the sole publisher can provide this timestamp; never stamp missing data now.
  const coreFresh = top?.coreUsageTs === undefined ? topFresh :
    typeof top.coreUsageTs === 'number' && Number.isFinite(top.coreUsageTs) && top.coreUsageTs > 0 && now - top.coreUsageTs >= 0 && now - top.coreUsageTs < 6000;
  const coreUsage = coreFresh ? coreUsageValues(top?.coreUsage) : [];
  const independentCpu = !!top && (top.cpuUsageValid !== undefined || top.cpuUsageTs !== undefined);
  const cpuUsageFresh = independentCpu ? top?.cpuUsageValid === true && typeof top.cpuUsage === 'number' && Number.isFinite(top.cpuUsage) && top.cpuUsage >= 0 && top.cpuUsage <= 100 &&
    typeof top.cpuUsageTs === 'number' && Number.isSafeInteger(top.cpuUsageTs) && top.cpuUsageTs > 0 && now >= top.cpuUsageTs && now - top.cpuUsageTs < 6000 : topFresh;
  if (topFresh) {
    Object.assign(values, { cpuPower: top!.cpuPowerValid === false ? null : validNumber(top!.tdpW), cpuTemp: top!.cpuTempValid === false ? null : validNumber(top!.tempC), cpuUsage: validNumber(top!.cpuUsage),
      cpuClock: top!.cpuClockValid === false || validNumber(top!.freqMhz) === null ? null : top!.freqMhz / 1000,
      gpuPower: validNumber(top!.gpuPowerW), gpuClock: validNumber(top!.gpuClockMhz) === null ? null : top!.gpuClockMhz / 1000 });
    // Memory/network fields are published by the existing HWiNFO parse;
    // missing per-device fields stay unknown, not a synthetic complete packet.
    const vram = memoryPairGb(top!.gpuMemoryUsedMb, top!.gpuMemoryTotalMb, top!.gpuMemoryAvailableMb, top!.gpuMemoryLoadPct);
    const ram = memoryPairGb(top!.physicalMemoryUsedMb, top!.physicalMemoryTotalMb, top!.physicalMemoryAvailableMb, top!.physicalMemoryLoadPct);
    Object.assign(values, { cpuVoltage: validVoltage(top!.cpuVoltageV), gpuVoltage: validVoltage(top!.gpuVoltageV) });
    Object.assign(values, { gpuTemp: validNumber(top!.gpuTempC), vramUsed: vram.used, vramTotal: vram.total, vramAvailable:vram.available, vramUsage:vram.load, ramUsed: ram.used, ramTotal: ram.total, ramAvailable:ram.available, ramUsage:ram.load, netDown:validNumber(top!.networkDownMBps), netUp:validNumber(top!.networkUpMBps) });
    if (top!.hasBattery) Object.assign(values, { batteryPercent: validNumber(top!.batteryPercent), batteryPower: top!.batteryPowerValid === false ? null : validNumber(top!.chargeW, true), batteryTime: validNumber(top!.remainMin) });
  }
  if (independentCpu) values.cpuUsage = cpuUsageFresh ? top!.cpuUsage : null;
  const batteryStatusFresh = top?.hasBattery === true && typeof top.batteryStatusTs === 'number'
    && Number.isFinite(top.batteryStatusTs) && top.batteryStatusTs > 0 && now - top.batteryStatusTs >= 0 && now - top.batteryStatusTs < 6000;
  if (top?.batteryStatusTs !== undefined) values.batteryPercent = batteryStatusFresh && top.batteryPercent >= 0 && top.batteryPercent <= 100 ? validNumber(top.batteryPercent) : null;
  const batteryFresh = top?.hasBattery === true && top.batteryPowerValid === true && typeof top.batteryPowerTs === 'number'
    && Number.isFinite(top.batteryPowerTs) && top.batteryPowerTs > 0 && now - top.batteryPowerTs >= 0 && now - top.batteryPowerTs < 6000;
  if (top?.batteryPowerValid !== undefined) values.batteryPower = batteryFresh ? validNumber(top.chargeW, true) : null;
  const batteryTimeFresh = batteryFresh && top?.batteryTimeValid === true && top.ac === 0 && top.chargeW < -.05
    && typeof top.batteryTimeTs === 'number' && Number.isFinite(top.batteryTimeTs) && top.batteryTimeTs > 0
    && now - top.batteryTimeTs >= 0 && now - top.batteryTimeTs < 6000;
  if (top?.batteryTimeValid !== undefined) values.batteryTime = batteryTimeFresh ? validNumber(top.remainMin) : null;
  const gameFpsFresh = fpsFresh && typeof fps!.fps === 'number' && Number.isFinite(fps!.fps) && fps!.fps >= 0 && fps!.fps <= 1000000;
  const selectedGpu = !!top && (Object.prototype.hasOwnProperty.call(top, 'gpuUsagePct') || Object.prototype.hasOwnProperty.call(top, 'gpuName') || Object.prototype.hasOwnProperty.call(top, 'gpuSelectionPresent'));
  if (gameFpsFresh) {
    Object.assign(values, { fps: validNumber(fps!.fps), fps1: fps1DisplayFresh(fps,now,snapshot?.hwDown===true) ? validNumber(fps!.fps1) : null });
    // A modern RTSS/Presented receipt proves frames, not the freshness or
    // selected device of the latched GPU field. Only old packets retain that
    // compatibility path; selected hardware data never needs this lookup.
    if (!selectedGpu) values.gpuUsage = hasExplicitFpsMetadata(fps) ? null : validNumber(fps!.gpu);
  }
  const frame = gameFpsFresh ? presentedFrameTime(fps) : null;
  const frameFresh = !!frame && now >= frame.frameTimeTs && now < (frame.frameTimeValidUntil ?? frame.frameTimeTs + MONITOR_MISSING_GRACE_MS) &&
    frame.frameTimeTs <= (frame.frameTimeSource === 'rtss-current-frame' ? snapshot!.collectedAt : fps!.ts);
  values.frametime = frameFresh ? frame!.frametime : null;
  // New sole-publisher packets bind utilization to the selected sensor group.
  // Never borrow a legacy/latched FPS packet's GPU when the selected card is
  // absent or expired. Older publishers retain their FPS-family behavior.
  if (selectedGpu) values.gpuUsage = topFresh && top!.gpuSelectionPresent !== false && typeof top!.gpuUsagePct === 'number' && Number.isFinite(top!.gpuUsagePct) && top!.gpuUsagePct >= 0 && top!.gpuUsagePct <= 100 ? top!.gpuUsagePct : null;
  if (selectedGpu && top!.gpuSelectionPresent === false) {
    for (const id of ['gpuPower','gpuClock','gpuTemp','gpuVoltage','vramUsed','vramAvailable','vramTotal','vramUsage'] as const) values[id] = null;
  }
  // Independent of HWiNFO freshness: an active YMCC receipt is not package power.
  // Unknown startup, power changes, pending/failed writes have no effective value.
  const w = tdpState?.watts;
  values.tdpLimit = tdpState?.status === 'applied' && tdpState.proof === 'write-ack' && (tdpState.mode === 'ac' || tdpState.mode === 'dc') && typeof w === 'number' && Number.isFinite(w) && w >= 2 && w <= 200 ? Math.round(w) : null;
  // File observation is independent of actual FPS and HWiNFO. Never infer it
  // from saved AC/DC preferences, and never turn missing data into unlimited.
  const fpsLimitFresh = top?.fpsLimitValid === true && top.fpsLimitSource === 'rtss-global'
    && typeof top.fpsLimit === 'number' && Number.isFinite(top.fpsLimit) && top.fpsLimit >= 0 && top.fpsLimit <= 1000000
    && typeof top.fpsLimitTs === 'number' && Number.isSafeInteger(top.fpsLimitTs) && top.fpsLimitTs > 0
    && now - top.fpsLimitTs >= 0 && now - top.fpsLimitTs < 6000;
  values.fpsLimit = fpsLimitFresh ? top!.fpsLimit : null;
  const fanFresh = top?.fanEnabled === true && top.fanSpeedValid === true && top.fanSpeedSource === 'ymcc-fan-target'
    && typeof top.fanSpeedPercent === 'number' && Number.isFinite(top.fanSpeedPercent) && top.fanSpeedPercent >= 0 && top.fanSpeedPercent <= 100
    && typeof top.fanSpeedTs === 'number' && Number.isSafeInteger(top.fanSpeedTs) && top.fanSpeedTs > 0 && now >= top.fanSpeedTs && now - top.fanSpeedTs < 6000;
  values.fanSpeed = fanFresh ? top!.fanSpeedPercent : null;
  const fileFresh=top?.fileSystemSource==='windows-filesystem-disk-activity' && typeof top.fileSystemTs==='number' && Number.isSafeInteger(top.fileSystemTs) && top.fileSystemTs>0 && now>=top.fileSystemTs && now-top.fileSystemTs<6000;
  values.diskRead=fileFresh?validNumber(top!.fileReadMBps):null;values.diskWrite=fileFresh?validNumber(top!.fileWriteMBps):null;
  const freshUntil: HudFreshUntil = {};
  if (selectedGpu && typeof top!.ts === 'number' && Number.isSafeInteger(top!.ts) && top!.ts > 0) freshUntil.gpu = top!.ts + 6000;
  if(fileFresh&&(values.diskRead!==null||values.diskWrite!==null))freshUntil.fileSystem=top!.fileSystemTs!+6000;
  if (fanFresh) freshUntil.fan = top!.fanSpeedTs! + 6000;
  if (fpsLimitFresh) freshUntil.fpsLimit = top!.fpsLimitTs! + 6000;
  if (topFresh) freshUntil.hardware = top!.ts + 6000;
  if (independentCpu && cpuUsageFresh) freshUntil.cpuUsage = top!.cpuUsageTs! + 6000;
  if (gameFpsFresh) freshUntil.fps = fps!.fpsValidUntil ?? (fps!.fpsObservedAt !== undefined ? fps!.fpsObservedAt + MONITOR_MISSING_GRACE_MS : fps!.ts + 6000);
  if (gameFpsFresh && values.fps1 !== null && fps!.fps1ValidUntil !== undefined) freshUntil.fps1 = fps!.fps1ValidUntil;
  if (frameFresh) freshUntil.frametime = frame!.frameTimeValidUntil ?? frame!.frameTimeTs + MONITOR_MISSING_GRACE_MS;
  if (coreFresh && coreUsage.length) freshUntil.cores = (top?.coreUsageTs ?? top!.ts) + 6000;
  if (values.batteryPercent != null) freshUntil.batteryStatus = (top?.batteryStatusTs ?? top!.ts) + 6000;
  if (values.batteryPower != null) freshUntil.batteryPower = (top?.batteryPowerTs ?? top!.ts) + 6000;
  if (values.batteryTime != null) freshUntil.batteryTime = Math.min(freshUntil.batteryPower ?? 0, (top?.batteryTimeTs ?? top!.ts) + 6000);
  // A collection-demand generation is not a new game. Strong PID/birth proof
  // survives continuous collector changes; weak legacy packets retain the
  // generation boundary. Native-session changes still require fresh entrance.
  values.clock = clockFormatter.format(now);
  return { source: 'ymcc-uniform', freshUntil,
    ...(gameFpsFresh ? { fpsObservedAt: fps!.fpsObservedAt ?? fps!.ts, fpsTarget: [snapshot!.session, fps!.pid, fps!.processCreated ?? ('legacy-generation:' + snapshot!.generation)].join('|') } : {}), stale: !topFresh && !fpsFresh && !cpuUsageFresh && !(coreFresh && coreUsage.length) && !batteryFresh && !batteryStatusFresh && !fpsLimitFresh && !fanFresh && !(fileFresh&&(values.diskRead!==null||values.diskWrite!==null)), batteryPowerEstimated: batteryFresh && top?.batteryPowerEstimated === true, batteryTimeEstimated: batteryTimeFresh && top?.batteryTimeEstimated === true, values,
    coreUsage, coreLabels: coreFresh ? coreUsageLabels(top?.coreLabels, coreUsage.length) : [], coreClocks: [], frameTimes: frameFresh ? frame!.frameTimes : [] };
}
// Same measurement limits as the sole native publisher, including strict
// positive frametime. This also protects both child-window output protocols.
const HUD_SCALAR_LIMITS: Partial<Record<HudMetricId, readonly [number, number]>> = {
  cpuPower:[0,2000],gpuPower:[0,2000],cpuTemp:[0,150],gpuTemp:[0,150],
  cpuClock:[0,10],gpuClock:[0,10],batteryPower:[-250,250],batteryTime:[0,10080],
  fps:[0,1000000],fps1:[0,1000000],frametime:[0,10000],
};
// Bounded packet admission shared by the real child and offline render benchmark.
// Caller verifies source/token/sequence; no acquisition or data fabrication here.
// Protocol admission produces a fully immutable plain packet, including owned
// values, deadlines and arrays. Only then may its identity skip repeat work.
const boundedOwned = new WeakSet<object>();
export function boundedHudData(data: HudData, stale = data.stale): HudData {
  if (boundedOwned.has(data as object) && stale === data.stale) return data;
  const values: HudData['values'] = {};
  const freshUntil = data.freshUntil === undefined ? undefined : boundedHudFreshUntil(data.freshUntil);
  for (const metric of HUD_METRICS) {
    const value = data.values?.[metric.id];
    const limits = HUD_SCALAR_LIMITS[metric.id];
    if (limits && (typeof value !== 'number' || !Number.isFinite(value) || value < limits[0] || value > limits[1]
      || ((metric.id === 'cpuPower' || metric.id === 'gpuPower') && value === limits[1]) || (metric.id === 'frametime' && value === 0))) {
      values[metric.id] = null; continue;
    }
    if (metric.id === 'cpuVoltage' || metric.id === 'gpuVoltage') { values[metric.id] = typeof value === 'number' && Number.isFinite(value) && value >= .05 && value <= 3 && (data.source === 'fixture' || freshUntil?.hardware !== undefined) ? value : null; continue; }
    if (metric.id === 'ramUsage' || metric.id === 'vramUsage') { values[metric.id]=typeof value==='number'&&Number.isFinite(value)&&value>=0&&value<=100&&(data.source==='fixture'||freshUntil?.hardware!==undefined)?value:null;continue; }
    if (['vramUsed','vramTotal','vramAvailable','ramUsed','ramTotal','ramAvailable','netDown','netUp','diskRead','diskWrite'].includes(metric.id)) {
      const disk=metric.id==='diskRead'||metric.id==='diskWrite';const net=metric.id==='netDown'||metric.id==='netUp';
      values[metric.id]=typeof value==='number'&&Number.isFinite(value)&&value>=0&&value<=(disk||net?1e6:1e9/1024)&&(data.source==='fixture'||freshUntil?.[disk?'fileSystem':'hardware']!==undefined)?value:null;continue;
    }
    if (['cpuUsage','gpuUsage','batteryPercent'].includes(metric.id)) { values[metric.id]=typeof value==='number'&&Number.isFinite(value)&&value>=0&&value<=100?value:null;continue; }
    if (metric.id === 'fanSpeed') { values[metric.id] = typeof value === 'number' && Number.isFinite(value) && value >= 0 && value <= 100 && (data.source === 'fixture' || freshUntil?.fan !== undefined) ? value : null; continue; }
    if (metric.id === 'fpsLimit') { values[metric.id] = typeof value === 'number' && Number.isFinite(value) && value >= 0 && value <= 1000000 && (data.source === 'fixture' || freshUntil?.fpsLimit !== undefined) ? value : null; continue; }
    values[metric.id] = typeof value === 'number' && Number.isFinite(value) ? value :
      metric.id === 'clock' && typeof value === 'string' ? value.slice(0, 16) : null;
  }
  if (values.batteryTime !== null && (typeof values.batteryPower !== 'number' || values.batteryPower >= -.05)) values.batteryTime = null;
  const peak = data.values?.cpuPeak;
  values.cpuPeak = typeof peak === 'number' && Number.isFinite(peak) && peak >= 0 && peak <= 100 ? peak : null;
  const numbers = (source: unknown, limit: number): Array<number | null> => {
    const result=new Array<number | null>(Array.isArray(source)?Math.min(source.length,limit):0);
    if(Array.isArray(source))for(let i=0;i<result.length;++i)if(i in source){const value=source[i];result[i]=typeof value==='number'&&Number.isFinite(value)&&value>=0?value:null;}
    return Object.freeze(result) as unknown as Array<number | null>;
  };
  const coreUsage = freezeCoreUsageValues(data.coreUsage);
  const owned: HudData = { source: data.source, stale, gameLogo: normalizeHudUnderlayImage(data.gameLogo), ...(freshUntil === undefined ? {} : { freshUntil }),
    ...(typeof data.fpsObservedAt === 'number' && Number.isSafeInteger(data.fpsObservedAt) && data.fpsObservedAt > 0 ? { fpsObservedAt: data.fpsObservedAt } : {}),
    ...(typeof data.fpsTarget === 'string' && data.fpsTarget.length > 0 && data.fpsTarget.length <= 400 ? { fpsTarget: data.fpsTarget } : {}), batteryPowerEstimated: data.batteryPowerEstimated === true, batteryTimeEstimated: data.batteryTimeEstimated === true, values, coreUsage, coreLabels: freezeCoreUsageLabels(data.coreLabels, coreUsage.length),
    coreClocks: numbers(data.coreClocks, 128), frameTimes: boundedFrameTimeValues(data.frameTimes) };
  Object.freeze(values); if(freshUntil)Object.freeze(freshUntil);
  Object.freeze(owned); boundedOwned.add(owned as object);
  return owned;
}
// Fixed-shape comparisons without JSON serialization. Identity shortcuts are
// limited to our fully immutable packets/arrays; external inputs are compared.
export function sameHudFields(a: HudMetricToggles, b: HudMetricToggles): boolean {
  return a === b || HUD_METRICS.every(metric => a[metric.id] === b[metric.id]);
}
// Only placement/composition fields previously serialized by the output client.
export function sameHudLayoutAppearance(a: MonitorOutputPreferences['ymcc'], b: MonitorOutputPreferences['ymcc']): boolean {
  return a.position === b.position && a.crosshair === b.crosshair && a.layout === b.layout &&
    a.scale === b.scale && a.gap === b.gap && sameHudFields(a.fields, b.fields);
}
export function sameHudAppearance(a: MonitorOutputPreferences['ymcc'], b: MonitorOutputPreferences['ymcc']): boolean {
  return (a.crosshair ?? 'none') === (b.crosshair ?? 'none') && a.position === b.position && a.scale === b.scale && a.saturation === b.saturation &&
    a.opacity === b.opacity && a.layout === b.layout && a.radius === b.radius && a.gap === b.gap &&
    a.hideUnavailable === b.hideUnavailable && (a.fpsEmphasis !== false) === (b.fpsEmphasis !== false) && (a.underlay ?? 'none') === (b.underlay ?? 'none') && (a.underlayPreset ?? 'none') === (b.underlayPreset ?? 'none') && (a.underlayOpacity ?? .28) === (b.underlayOpacity ?? .28) && (a.underlayImage ?? '') === (b.underlayImage ?? '') && sameHudFields(a.fields, b.fields);
}
// Renderer-only reuse: compare raw inputs against an owned normalized snapshot.
// Strict option equality avoids treating absent legacy/default fields as already
// normalized. Always compare every toggle, even if the raw object was mutated.
// Preset banks remain normalized/persisted at packet/UI boundaries as before.
const hudDisplayInputKeys = ['crosshair','position','scale','saturation','opacity','layout','radius','gap',
  'hideUnavailable','fpsEmphasis','underlay','underlayPreset','underlayOpacity','underlayImage'] as const;
export function sameHudDisplayInput(owned: MonitorOutputPreferences['ymcc'], raw: unknown): boolean {
  const input = record(raw), fields = record(input.fields);
  return input.dataPresetVersion === 1 && hudDisplayInputKeys.every(key => owned[key] === input[key]) &&
    HUD_METRICS.every(metric => owned.fields[metric.id] === fields[metric.id]);
}
export function sameHudData(a: HudData, b: HudData, fields?: HudMetricToggles): boolean {
  if(a===b && boundedOwned.has(a as object))return true;
  const immutable=boundedOwned.has(a as object)&&boundedOwned.has(b as object);
  if (a.source !== b.source || a.stale !== b.stale || (a.gameLogo ?? '') !== (b.gameLogo ?? '')) return false;
  const selected = (id: HudMetricId) => !fields || fields[id];
  if ((selected('batteryPower') && a.batteryPowerEstimated !== b.batteryPowerEstimated) ||
      (selected('batteryTime') && a.batteryTimeEstimated !== b.batteryTimeEstimated)) return false;
  if (!HUD_METRICS.every(metric => !selected(metric.id) || (a.values[metric.id] ?? null) === (b.values[metric.id] ?? null))) return false;
  // Every FPS-family field depends on genuine FPS, including when its own switch is off.
  if (HUD_FPS_FAMILY.some(selected) && ((a.values.fps ?? null) !== (b.values.fps ?? null) ||
      a.fpsObservedAt !== b.fpsObservedAt || a.fpsTarget !== b.fpsTarget ||
      a.freshUntil?.fps !== b.freshUntil?.fps)) return false;
  if (HUD_PERCENT_GRAPHS.some(graph => selected(graph.id) && (a.values[graph.value] ?? null) !== (b.values[graph.value] ?? null))) return false;
  const equal = <T>(left: readonly T[] | undefined, right: readonly T[] | undefined, max = Infinity) => {
    if(immutable && left===right)return true;
    const count = Math.min(left?.length ?? 0, max);
    if (count !== Math.min(right?.length ?? 0, max)) return false;
    for (let i=0; i<count; ++i) if (left![i] !== right![i]) return false;
    return true;
  };
  const coreLimit = fields ? MAX_VISIBLE_CORE_TILES : Infinity;
  return (!selected('cpuCores') || (equal(a.coreUsage,b.coreUsage,coreLimit) && equal(a.coreLabels,b.coreLabels,coreLimit))) &&
    (!fields ? equal(a.coreClocks,b.coreClocks) : true) &&
    (!selected('frametimeGraph') || equal(a.frameTimes,b.frameTimes));
}

// Compact tabular footprints for normal numeric ranges. Values are never
// clamped, rounded differently or hidden by this presentation-only mapping.
export function hudMetricDigitSlots(id: HudMetricId): number {
  if (['fps1','cpuTemp','gpuTemp','cpuUsage','gpuUsage','vramUsage','ramUsage','fanSpeed','batteryPercent'].includes(id)) return 3;
  if (['cpuPower','gpuPower','cpuVoltage','gpuVoltage','vramUsed','vramTotal','vramAvailable','ramUsed','ramTotal','ramAvailable','clock'].includes(id)) return 5;
  if (['netDown','netUp','diskRead','diskWrite','fpsLimit','batteryTime'].includes(id)) return 6;
  if (id==='batteryPower' || id==='frametime') return 7;
  return 4;
}
export function hudValue(data: HudData, id: HudMetricId): string {
  const value = data.values[id], spec = metricById[id];
  if (id !== 'clock' && id !== 'tdpLimit' && data.stale) return '—';
  if (value === null || value === undefined || (typeof value === 'number' && !Number.isFinite(value))) return '—';
  if (id === 'fps') return typeof value === 'number' && value >= 0 ? String(Math.min(999, Math.round(value))) : '—';
  if (id === 'fpsLimit' && typeof value === 'number') return value === 0 ? '不限' : value.toFixed(value > 0 && value < .01 ? 6 : 2).replace(/\.?0+$/, '');
  return ((id === 'batteryPower' && data.batteryPowerEstimated || id === 'batteryTime' && data.batteryTimeEstimated) ? '≈' : '') + (typeof value === 'number' ? value.toFixed(spec.digits) : String(value));
}
export function percentBar(value: unknown): number {
  return Math.max(0, Math.min(100, validNumber(value) ?? 0));
}
export function monitorTemplateDocument(preferences: MonitorOutputPreferences) {
  return { schema: 'ymcc-monitor-template/v1', dataSource: 'ymcc-uniform-only', sampleDataIncluded: false,
    preferences: normalizeMonitorOutput(preferences) };
}
// Display-only selection, never alters saved switches or acquires new values.
export function hudMetricAvailable(data: HudData, id: HudMetricId): boolean {
  if (HUD_FPS_FAMILY.some(field => field === id) && (data.stale || typeof data.values.fps !== 'number' ||
      !Number.isFinite(data.values.fps) || data.values.fps < 1 || data.values.fps > 1000000)) return false;
  if (id === 'cpuCores') {
    if (data.stale) return false;
    for (let i=0;i<Math.min(data.coreUsage.length,MAX_VISIBLE_CORE_TILES);++i) { const value=data.coreUsage[i]; if(typeof value==='number'&&Number.isFinite(value)&&value>=0&&value<=100)return true; }
    return false;
  }
  if (id === 'frametimeGraph') return !data.stale && typeof data.values.fps === 'number' && data.values.fps > 0 && hudMetricAvailable(data, 'fps') && hasFrameTimeCurve(data.frameTimes);
  const graph=HUD_PERCENT_GRAPHS.find(graph=>graph.id===id);
  if(graph)return hudPercentValue(data,graph.value)!==null;
  return hudValue(data, id) !== '—' && String(data.values[id] ?? '').trim() !== '';
}
export function hudVisibleFields(fields: HudMetricToggles, data: HudData, hideUnavailable = false, fpsReady = true): HudMetricToggles {
  const visible = {} as HudMetricToggles;
  const fpsAvailable = fpsReady && hudMetricAvailable(data, 'fps');
  for (const metric of HUD_METRICS) {
    const family = HUD_FPS_FAMILY.some(field => field === metric.id);
    visible[metric.id] = fields[metric.id] && (!family || fpsAvailable) &&
      (!hideUnavailable || hudMetricAvailable(data, metric.id));
  }
  return visible;
}
