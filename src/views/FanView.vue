<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from 'vue';
import Dropdown from '@/components/Dropdown.vue';
import { topMonitorData } from '@/bridge/topmon';
import {
  getFanFeatureSettings,
  getFanPresetCurve,
  FAN_FORCE_PREVIEW,
  recordFanHandshake,
  saveFanCurve,
  setFanDiagnosticLoggingEnabled,
  setFanControlActive,
  setFanNavigationDuty,
  setFanMotionEnabled,
  type FanFeatureSettings,
} from '@/bridge/fanFeature';
import { fanHostLifecycle, type FanHostLifecycleState } from '@/bridge/fanHost';
import type { FanNode } from '@/bridge/fanApi';
import { normalizeFanNodes } from '@/bridge/fanCurve';
import { fanDiagnosticLog } from '@/bridge/fanDiagnostics';

type FanPreset = FanFeatureSettings['preset'];

const PRESETS: Record<FanPreset, FanNode[]> = {
  soft: [
    { tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 0 },
    { tempC: 70, dutyPercent: 20 }, { tempC: 100, dutyPercent: 80 },
  ],
  balanced: [
    { tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 20 },
    { tempC: 70, dutyPercent: 45 }, { tempC: 100, dutyPercent: 90 },
  ],
  aggressive: [
    { tempC: 0, dutyPercent: 0 }, { tempC: 40, dutyPercent: 30 },
    { tempC: 70, dutyPercent: 75 }, { tempC: 100, dutyPercent: 100 },
  ],
};
const PRESET_OPTIONS = [
  { value: 'soft', label: '轻柔转速' },
  { value: 'balanced', label: '均衡转速' },
  { value: 'aggressive', label: '暴力转速' },
] as const;
const AXIS_TICKS = [0, 20, 40, 60, 80, 100] as const;
const GRAPH_LEFT = 54;
const GRAPH_TOP = 70;
const GRAPH_BOTTOM = 320;
const GRAPH_X_SCALE = 5.82;
const GRAPH_Y_SCALE = 2.5;

const settings = getFanFeatureSettings();
const graphSelectedNode = ref(0);
const configSelectedNode = ref(0);
const nodes = ref<FanNode[]>(structuredClone(settings.presetCurves?.[settings.preset] ?? settings.nodes));
const selectedPreset = ref<FanPreset>(settings.preset);
const motionEnabled = ref(settings.motionEnabled);
const supported = ref(false);
const controlReady = ref(FAN_FORCE_PREVIEW);
const controlActive = ref(false);
const busy = ref(false);
const chartWrap = ref<HTMLElement | null>(null);
const draggingNode = ref<number | null>(null);
const graphDragArmed = ref<number | null>(null);
let telemetryTimer: ReturnType<typeof window.setInterval> | null = null;
let curveApplyPromise: Promise<void> | null = null;
let curveApplyPending = false;
let fanWasActiveBeforeSuspend = false;
let fanSuspendBoundary = false;
let fanResumeGeneration = 0;
let fanResumeTimer: ReturnType<typeof window.setTimeout> | null = null;
const hostState = ref<FanHostLifecycleState>(FAN_FORCE_PREVIEW ? 'awaiting-control' : fanHostLifecycle.state);
const statusMessage = ref(FAN_FORCE_PREVIEW ? '模拟预览：握手成功' : '正在握手识别…');

function graphX(tempC: number): number {
  return GRAPH_LEFT + tempC * GRAPH_X_SCALE;
}

function graphY(dutyPercent: number): number {
  return GRAPH_BOTTOM - dutyPercent * GRAPH_Y_SCALE;
}

const temperature = computed(() => {
  const value = Number(topMonitorData.value?.tempC);
  return Number.isFinite(value) && value >= 0 ? value : null;
});
const temperatureText = computed(() => temperature.value === null ? '无数据' : `${Math.round(temperature.value)}°C`);
const expectedDuty = computed(() => {
  if (!controlActive.value || temperature.value === null) return 0;
  return Math.round(interpolate(nodes.value, temperature.value));
});
const statusText = computed(() => {
  if (controlActive.value) {
    return '控制已开启';
  }
  return statusMessage.value;
});

function readableFanError(error: unknown): string {
  const message = error instanceof Error ? error.message : String(error);
  const userMessage = message
    .replace(/HandheldCompanion/gi, '设备数据组件')
    .replace(/\bHC\b/gi, '设备数据')
    .replace(/\bfactory\b/gi, '设备类型');
  if (/依赖校验失败|Fan Host 不存在|runtimeconfig|Windows Desktop Runtime|程序集|FileNotFoundException|DllNotFoundException/i.test(message)) {
    return `风扇 Host 环境不可用：${userMessage}`;
  }
  if (/REAL_WRITE_AUTH_REQUIRED|写入\/恢复尚未验证/i.test(message)) {
    return '已识别风扇路线，但真实写入/恢复验证尚未完成';
  }
  if (/FAN_UNSUPPORTED|风扇不支持/i.test(message)) return '风扇不支持';
  if (/FAN_ROUTE_CONFLICT|HC_OPENLIB_CONFLICT|EXTERNAL_FAN_OWNER/i.test(message)) {
    return '检测到其他风扇控制程序，已停止控制并交回 OEM';
  }
  if (/LEASE_INVALID|LEASE_REQUIRED|lease/i.test(message)) return '风扇控制租约已失效，请重试';
  return userMessage || '风扇控制请求失败';
}
const controlText = computed(() => {
  if (!supported.value) return '风扇控制 不支持';
  return controlActive.value ? '风扇控制 已开启' : '风扇控制 未开启';
});
const chartPoints = computed(() => nodes.value
  .map((node) => `${graphX(node.tempC)},${graphY(node.dutyPercent)}`)
  .join(' '));

function interpolate(points: readonly FanNode[], temp: number): number {
  if (temp <= points[0].tempC) return points[0].dutyPercent;
  for (let i = 1; i < points.length; i += 1) {
    if (temp <= points[i].tempC) {
      const span = points[i].tempC - points[i - 1].tempC;
      if (span <= 0) return points[i].dutyPercent;
      const ratio = (temp - points[i - 1].tempC) / span;
      return points[i - 1].dutyPercent + (points[i].dutyPercent - points[i - 1].dutyPercent) * ratio;
    }
  }
  return points[points.length - 1].dutyPercent;
}

function nodeOptions(index: number, field: 'tempC' | 'dutyPercent'): number[] {
  const current = nodes.value[index][field];
  // Node 1 temperature stays anchored at 0°C, but its duty is editable.
  // Every later option starts at its predecessor so the menu cannot create a
  // curve that HC would reject; editing an earlier duty still propagates up.
  if (index === 0 && field === 'tempC') return [0];
  const predecessor = index > 0 ? Number(nodes.value[index - 1][field]) : 0;
  const min = field === 'dutyPercent'
    ? Math.max(index === 3 ? 50 : 0, predecessor)
    : Math.max(0, predecessor);
  const max = 100;
  const values: number[] = [];
  for (let value = min; value <= max; value += 5) values.push(value);
  if (current >= min && current <= max && !values.includes(current)) values.push(current);
  return values.sort((a, b) => a - b);
}

function nodeDropdownOptions(index: number, field: 'tempC' | 'dutyPercent') {
  const suffix = field === 'tempC' ? '°C' : '%';
  return nodeOptions(index, field).map((value) => ({ value, label: `${value} ${suffix}` }));
}

function normalizeEditedNodes(index: number, field: 'tempC' | 'dutyPercent', value: number, source: readonly FanNode[] = nodes.value): FanNode[] {
  return normalizeFanNodes(source, index, field, value);
}

function commitCurve(next: FanNode[]): void {
  nodes.value = next;
  fanDiagnosticLog('ui.curve-edited', { preset: selectedPreset.value, nodes: next });
  void saveFanCurve(next, selectedPreset.value).catch(() => {
    statusMessage.value = '曲线配置保存失败，当前控制未改变';
  });
  if (controlActive.value) void requestCurveApply();
}

function updateNodeValue(index: number, field: 'tempC' | 'dutyPercent', rawValue: string | number): void {
  const value = Number(rawValue);
  if (!Number.isFinite(value)) return;
  configSelectedNode.value = index;
  commitCurve(normalizeEditedNodes(index, field, value));
}

function nodePointStyle(node: FanNode): Record<string, string> {
  return {
    left: `${(graphX(node.tempC) / 650) * 100}%`,
    top: `${(graphY(node.dutyPercent) / 370) * 100}%`,
  };
}

function updateGraphNode(index: number, event: PointerEvent, commit = false): void {
  const chart = chartWrap.value;
  if (!chart) return;
  const rect = chart.getBoundingClientRect();
  if (rect.width <= 0 || rect.height <= 0) return;
  const x = Math.max(0, Math.min(650, ((event.clientX - rect.left) / rect.width) * 650));
  const y = Math.max(0, Math.min(370, ((event.clientY - rect.top) / rect.height) * 370));
  const temp = index === 0 ? 0 : Math.round(Math.max(0, Math.min(100, (x - GRAPH_LEFT) / GRAPH_X_SCALE)));
  const duty = Math.round(Math.max(0, Math.min(100, (GRAPH_BOTTOM - y) / GRAPH_Y_SCALE)));
  const next = normalizeEditedNodes(index, 'tempC', temp);
  const dutyNext = next.map((node) => ({ ...node }));
  dutyNext[index].dutyPercent = duty;
  const normalized = normalizeEditedNodes(index, 'dutyPercent', dutyNext[index].dutyPercent, next);
  nodes.value = normalized;
  if (commit) {
    void saveFanCurve(normalized, selectedPreset.value);
    if (controlActive.value) void requestCurveApply();
  }
}

function onGraphNodePointerDown(index: number, event: PointerEvent): void {
  event.preventDefault();
  graphSelectedNode.value = index;
  draggingNode.value = index;
  (event.currentTarget as HTMLElement).setPointerCapture?.(event.pointerId);
  updateGraphNode(index, event);
}

function onGraphNodePointerMove(index: number, event: PointerEvent): void {
  if (draggingNode.value !== index) return;
  event.preventDefault();
  updateGraphNode(index, event);
}

function onGraphNodePointerUp(index: number, event: PointerEvent): void {
  if (draggingNode.value !== index) return;
  event.preventDefault();
  updateGraphNode(index, event, true);
  draggingNode.value = null;
  (event.currentTarget as HTMLElement).releasePointerCapture?.(event.pointerId);
}

function onGraphNodeClick(index: number, event: MouseEvent): void {
  graphSelectedNode.value = index;
  // Native gamepad A and keyboard Enter both invoke HTMLElement.click() with
  // detail=0. A first press arms the shared spatial editor; a second press
  // commits it. A physical mouse click remains a selection only, preserving
  // direct pointer dragging.
  if (event.detail !== 0) {
    graphDragArmed.value = null;
    return;
  }
  if (graphDragArmed.value === index) {
    graphDragArmed.value = null;
    commitCurve(nodes.value.map((node) => ({ ...node })));
    statusMessage.value = `节点 ${index + 1} 已应用`;
  } else {
    graphDragArmed.value = index;
    statusMessage.value = `节点 ${index + 1} 编辑中：方向键调整，A 确认`;
  }
}

function onGraphSpatialEdit(index: number, event: Event): void {
  if (graphDragArmed.value !== index) return;
  const detail = (event as CustomEvent<{ dx?: number; dy?: number }>).detail ?? {};
  const dx = Number(detail.dx) || 0;
  const dy = Number(detail.dy) || 0;
  if (dx === 0 && dy === 0) return;
  const current = nodes.value[index];
  // Gamepad editing intentionally uses a 1°C / 1% step. The shared engine
  // repeats only while this node is armed, so ordinary page navigation keeps
  // its existing cadence.
  const withTemp = normalizeEditedNodes(index, 'tempC', current.tempC + (index === 0 ? 0 : dx));
  const next = normalizeEditedNodes(index, 'dutyPercent', current.dutyPercent - dy, withTemp);
  nodes.value = next;
  event.preventDefault();
}

function onFanGamepadBack(event: Event): void {
  if (graphDragArmed.value === null) return;
  graphDragArmed.value = null;
  statusMessage.value = '已取消节点编辑';
  event.preventDefault();
}

async function recoverFanSession(reason: string): Promise<void> {
  // Telemetry is a safety signal, not merely a cosmetic refresh. If the Host
  // disappears or reports a fault while the UI still shows control enabled,
  // explicitly enter the same restore/release path as the Disable action.
  try {
    await fanHostLifecycle.disable();
    hostState.value = fanHostLifecycle.state;
    statusMessage.value = reason;
  } catch {
    hostState.value = fanHostLifecycle.state;
    statusMessage.value = '风扇控制恢复中，请重试';
  }
}

async function refreshTelemetry(): Promise<void> {
  if (!controlActive.value) return;
  if (FAN_FORCE_PREVIEW) return;
  try {
    const state = await fanHostLifecycle.getState();
    hostState.value = fanHostLifecycle.state;
    // The Host is authoritative. If it restored OEM or entered a fault state
    // while the UI was waiting, never leave the page showing “control active”.
    const localLease = fanHostLifecycle.currentLease;
    const remoteLeaseMismatch = localLease !== null && typeof state.leaseGeneration === 'number'
      && state.leaseGeneration !== localLease.generation;
    // A UI “active” flag is valid only while the remote Host explicitly
    // reports live software writes. State=Ready/Open alone is not enough:
    // after a failed restore or a lease/transport race the state label can
    // briefly remain readable while hardware control has already been
    // revoked. Treat missing/false telemetry and pending HC cleanup as a
    // recovery boundary, never as an active curve.
    const remoteWritesLost = state.hardwareWritesEnabled !== true;
    const remoteRestoreAlreadyConfirmed = state.oemRestoreConfirmed === true;
    if (state.unknownState === true || !['Ready', 'Open'].includes(state.state)
      || remoteWritesLost || remoteRestoreAlreadyConfirmed || state.hcCloseCleanupPending === true || remoteLeaseMismatch) {
      controlActive.value = false;
      setFanControlActive(false);
      setFanNavigationDuty(0);
      stopTelemetry();
      statusMessage.value = remoteLeaseMismatch ? '风扇租约已变化，正在恢复' : '风扇控制已停止，正在恢复';
      void recoverFanSession(remoteLeaseMismatch ? '检测到风扇租约变化，OEM 正在恢复' : '风扇控制已停止，OEM 正在恢复');
    }
  } catch {
    controlActive.value = false;
    setFanControlActive(false);
    setFanNavigationDuty(0);
    stopTelemetry();
    hostState.value = fanHostLifecycle.state;
    statusMessage.value = '风扇状态读取失败，正在恢复';
    void recoverFanSession('风扇状态读取失败，OEM 正在恢复');
  }
}

function startTelemetry(): void {
  if (telemetryTimer !== null) return;
  void refreshTelemetry();
  telemetryTimer = window.setInterval(() => { void refreshTelemetry(); }, 1000);
}

function stopTelemetry(): void {
  if (telemetryTimer !== null) window.clearInterval(telemetryTimer);
  telemetryTimer = null;
}

function cancelFanResume(): void {
  fanResumeGeneration += 1;
  if (fanResumeTimer !== null) window.clearTimeout(fanResumeTimer);
  fanResumeTimer = null;
}

async function resumeFanControlAfterWake(generation: number): Promise<void> {
  // Native/App power handlers and WebView recreation can deliver resume more
  // than once. The generation check makes this replay single-owner and keeps
  // a stale wake from writing after a newer suspend/exit boundary.
  const delays = [250, 750, 1500] as const;
  for (let attempt = 0; attempt < delays.length; attempt += 1) {
    if (generation !== fanResumeGeneration || !fanWasActiveBeforeSuspend) return;
    if (attempt > 0) await new Promise<void>((resolve) => setTimeout(resolve, delays[attempt]));
    if (generation !== fanResumeGeneration || !fanWasActiveBeforeSuspend) return;
    try {
      statusMessage.value = attempt === 0 ? '唤醒后正在恢复风扇控制…' : '唤醒后正在重试风扇控制…';
      // App.vue owns the durable resume path. This component may have been
      // recreated after KeepAlive eviction, so it reflects lifecycle state
      // only and must never issue a second curve write.
      await fanHostLifecycle.resume();
      controlReady.value = fanHostLifecycle.controlReady;
      hostState.value = fanHostLifecycle.state;
      if (fanHostLifecycle.state === 'ready') {
        controlActive.value = true;
        setFanControlActive(true);
        setFanNavigationDuty(expectedDuty.value);
        startTelemetry();
        fanWasActiveBeforeSuspend = false;
        statusMessage.value = '唤醒后风扇控制已自动恢复';
        return;
      }
    } catch (error) {
      fanDiagnosticLog('lifecycle.resume-auto-apply-failure', {
        attempt,
        state: fanHostLifecycle.state,
        error: error instanceof Error ? error.message : String(error),
      });
    }
  }
  if (generation === fanResumeGeneration && fanWasActiveBeforeSuspend) {
    fanWasActiveBeforeSuspend = false;
    controlActive.value = false;
    setFanControlActive(false);
    setFanNavigationDuty(0);
    stopTelemetry();
    statusMessage.value = '唤醒后风扇控制恢复失败，已保持 OEM，请手动重试';
  }
}

function onFanPowerSuspending(): void {
  // The native power boundary may be reported twice (query/broadcast). Keep
  // the first active snapshot instead of overwriting it after the UI is reset.
  if (!fanSuspendBoundary) {
    fanSuspendBoundary = true;
    fanWasActiveBeforeSuspend = controlActive.value;
    cancelFanResume();
  }
  controlActive.value = false;
  setFanControlActive(false);
  setFanNavigationDuty(0);
  stopTelemetry();
  hostState.value = 'suspended';
  statusMessage.value = '睡眠前已恢复 OEM 控制';
}

function onFanPowerResumed(): void {
  if (!fanSuspendBoundary) return;
  fanSuspendBoundary = false;
  const shouldResume = fanWasActiveBeforeSuspend;
  controlActive.value = false;
  setFanControlActive(false);
  setFanNavigationDuty(0);
  stopTelemetry();
  hostState.value = 'awaiting-control';
  if (!shouldResume) {
    statusMessage.value = supported.value ? '唤醒后请重新开启风扇控制' : '风扇不支持';
    return;
  }
  const generation = ++fanResumeGeneration;
  fanResumeTimer = window.setTimeout(() => {
    fanResumeTimer = null;
    void resumeFanControlAfterWake(generation);
  }, 250);
}

async function ensureSupported(): Promise<boolean> {
  if (FAN_FORCE_PREVIEW) {
    supported.value = true;
    controlReady.value = true;
    hostState.value = controlActive.value ? 'ready' : 'awaiting-control';
    statusMessage.value = controlActive.value ? '模拟控制已开启' : '模拟预览：握手成功';
    return true;
  }
  // A remembered device remains supported, but a fault/stopped lifecycle
  // must re-run the handshake/recovery path before another write attempt.
  if (supported.value && !['fault-locked', 'unknown', 'stopped'].includes(fanHostLifecycle.state)) {
    controlReady.value = fanHostLifecycle.controlReady;
    return true;
  }
  if (fanHostLifecycle.state === 'awaiting-control' || fanHostLifecycle.state === 'ready') {
    supported.value = true;
    controlReady.value = fanHostLifecycle.controlReady;
    hostState.value = fanHostLifecycle.state;
    statusMessage.value = '握手成功，真实写入能力待确认';
    return true;
  }
  try {
    const gate = await fanHostLifecycle.start();
    supported.value = gate.allowed;
    controlReady.value = gate.writeReady;
    hostState.value = fanHostLifecycle.state;
    statusMessage.value = gate.allowed
      ? (gate.writeReady ? '握手成功' : '已识别风扇路线，真实写入尚未验证')
      : '风扇不支持';
    await recordFanHandshake(gate.allowed);
    return gate.allowed;
  } catch (error) {
    // A transport/lease/restore failure is not an unsupported-device result.
    // Keep a remembered HC device visible so the user can retry recovery; only
    // a failed handshake gate should hide the feature.
    const recoverable = settings.configured === true ||
      fanHostLifecycle.state === 'fault-locked' || fanHostLifecycle.state === 'unknown';
    supported.value = recoverable;
    controlReady.value = false;
    hostState.value = fanHostLifecycle.state;
    statusMessage.value = recoverable
      ? '风扇控制恢复中，请重试'
      : readableFanError(error);
    return false;
  }
}

async function applyCurveOnce(): Promise<void> {
  if (busy.value) return;
  busy.value = true;
  try {
    fanDiagnosticLog('ui.control-enable-begin', { preset: selectedPreset.value, nodes: nodes.value });
    if (!(await ensureSupported())) return;
    if (!controlReady.value) {
      statusMessage.value = '已识别风扇路线，但真实写入/恢复验证尚未完成';
      return;
    }
  if (FAN_FORCE_PREVIEW) {
    controlActive.value = true;
    setFanControlActive(true);
    setFanNavigationDuty(expectedDuty.value);
    hostState.value = 'ready';
    statusMessage.value = '模拟控制已开启（未连接硬件）';
    startTelemetry();
    return;
  }
    await fanHostLifecycle.apply(nodes.value);
    fanDiagnosticLog('ui.control-enable-success', { preset: selectedPreset.value });
    controlActive.value = true;
    setFanControlActive(true);
    hostState.value = fanHostLifecycle.state;
    statusMessage.value = '控制已开启';
    setFanNavigationDuty(expectedDuty.value);
    startTelemetry();
  } catch (error) {
    fanDiagnosticLog('ui.control-enable-failure', { state: fanHostLifecycle.state, error: error instanceof Error ? error.message : String(error) });
    hostState.value = fanHostLifecycle.state;
    statusMessage.value = `风扇控制失败：${readableFanError(error)}，已进入恢复等待`;
    controlActive.value = false;
    setFanControlActive(false);
    setFanNavigationDuty(0);
    stopTelemetry();
    if (fanHostLifecycle.state === 'fault-locked' || fanHostLifecycle.state === 'unknown') {
      await recoverFanSession('风扇控制失败，已确认恢复 OEM');
    }
  } finally {
    busy.value = false;
  }
}

/** Coalesce rapid graph/dropdown edits into one serialized hardware apply. */
function requestCurveApply(): Promise<void> {
  curveApplyPending = true;
  if (curveApplyPromise) return curveApplyPromise;
  curveApplyPromise = (async () => {
    while (curveApplyPending) {
      curveApplyPending = false;
      await applyCurveOnce();
    }
  })().finally(() => {
    curveApplyPromise = null;
  });
  return curveApplyPromise;
}

async function toggleControl(): Promise<void> {
  if (busy.value) return;
  if (!controlActive.value && fanWasActiveBeforeSuspend) {
    // A deliberate user action supersedes an automatic wake replay.
    fanWasActiveBeforeSuspend = false;
    cancelFanResume();
  }
  if (controlActive.value) {
    fanDiagnosticLog('ui.control-disable-begin');
    if (FAN_FORCE_PREVIEW) {
      controlActive.value = false;
      setFanControlActive(false);
      setFanNavigationDuty(0);
      stopTelemetry();
      hostState.value = 'awaiting-control';
      statusMessage.value = '模拟控制已关闭';
      return;
    }
    busy.value = true;
    try {
      await fanHostLifecycle.disable();
      controlActive.value = false;
      setFanControlActive(false);
      setFanNavigationDuty(0);
      stopTelemetry();
      hostState.value = fanHostLifecycle.state;
      statusMessage.value = '控制已关闭';
      fanDiagnosticLog('ui.control-disable-success');
    } catch (error) {
      fanDiagnosticLog('ui.control-disable-failure', { state: fanHostLifecycle.state, error: error instanceof Error ? error.message : String(error) });
      // A failed restore is never treated as an active control session in the
      // UI. Stop animation/telemetry immediately; the Host remains resident or
      // fault-locked and the next click retries its recovery path.
      controlActive.value = false;
      setFanControlActive(false);
      setFanNavigationDuty(0);
      stopTelemetry();
      hostState.value = fanHostLifecycle.state;
      statusMessage.value = readableFanError(error);
    } finally {
      busy.value = false;
    }
    return;
  }
  await requestCurveApply();
}

async function choosePreset(rawValue: string | number): Promise<void> {
  const name = String(rawValue) as FanPreset;
  if (!Object.prototype.hasOwnProperty.call(PRESETS, name)) return;
  selectedPreset.value = name;
  fanDiagnosticLog('ui.preset-selected', { preset: name });
  nodes.value = getFanPresetCurve(name);
  try {
    await saveFanCurve(nodes.value, name);
  } catch {
    statusMessage.value = '预设保存失败，硬件控制未改变';
    return;
  }
  if (controlActive.value) {
    if (FAN_FORCE_PREVIEW) {
      statusMessage.value = '模拟预设已应用';
      return;
    }
    // A graph drag may have queued an apply just before the dropdown opened.
    // Let that serialized request settle before changing the active preset;
    // otherwise two valid writes can interleave and the second one observes a
    // lease that the first operation is already restoring.
    if (curveApplyPromise) await curveApplyPromise.catch(() => {});
    busy.value = true;
    try {
      await fanHostLifecycle.applyPreset(name, nodes.value);
      statusMessage.value = '控制已开启';
    } catch (error) {
      fanDiagnosticLog('ui.preset-apply-failure', { preset: name, state: fanHostLifecycle.state, error: error instanceof Error ? error.message : String(error) });
      hostState.value = fanHostLifecycle.state;
      controlActive.value = false;
      setFanControlActive(false);
      setFanNavigationDuty(0);
      stopTelemetry();
      statusMessage.value = `预设应用失败：${readableFanError(error)}，正在确认恢复`;
      if (fanHostLifecycle.state === 'fault-locked' || fanHostLifecycle.state === 'unknown') {
        await recoverFanSession('预设应用失败，已确认恢复 OEM');
      }
    } finally {
      busy.value = false;
    }
  }
}

async function resetCurve(): Promise<void> {
  // Reset only the currently selected preset. Other presets retain their
  // independently edited curves and will be restored when selected again.
  nodes.value = structuredClone(PRESETS[selectedPreset.value]);
  try {
    await saveFanCurve(nodes.value, selectedPreset.value);
  } catch {
    statusMessage.value = '重置保存失败，硬件控制未改变';
    return;
  }
  if (controlActive.value) await requestCurveApply();
}

async function toggleMotion(): Promise<void> {
  motionEnabled.value = !motionEnabled.value;
  try {
    await setFanMotionEnabled(motionEnabled.value);
  } catch {
    motionEnabled.value = !motionEnabled.value;
    statusMessage.value = '风扇图标设置保存失败';
  }
}

watch(expectedDuty, (duty) => {
  setFanNavigationDuty(controlActive.value ? duty : 0);
});

function onFanExitCleanupFailed(event: Event): void {
  const detail = (event as CustomEvent<{ message?: unknown }>).detail;
  controlActive.value = false;
  setFanControlActive(false);
  setFanNavigationDuty(0);
  stopTelemetry();
  statusMessage.value = typeof detail?.message === 'string'
    ? detail.message
    : '风扇 OEM 恢复未确认，已取消退出';
}

onMounted(() => {
  window.addEventListener('ipc:power.suspending', onFanPowerSuspending);
  window.addEventListener('ipc:power.resumed', onFanPowerResumed);
  window.addEventListener('ipc:gamepad-back', onFanGamepadBack);
  window.addEventListener('ipc:fan.exit.cleanup-failed', onFanExitCleanupFailed);
  if (FAN_FORCE_PREVIEW) {
    supported.value = true;
    return;
  }
  void ensureSupported();
});

onUnmounted(() => {
  window.removeEventListener('ipc:power.suspending', onFanPowerSuspending);
  window.removeEventListener('ipc:power.resumed', onFanPowerResumed);
  window.removeEventListener('ipc:gamepad-back', onFanGamepadBack);
  window.removeEventListener('ipc:fan.exit.cleanup-failed', onFanExitCleanupFailed);
  stopTelemetry();
  cancelFanResume();
  fanWasActiveBeforeSuspend = false;
  fanSuspendBoundary = false;
});
</script>

<template>
  <section class="fan-page">
    <header class="fan-header"><div><div class="fan-kicker">Fan API</div><h1>风扇控制</h1></div></header>
    <div class="fan-status-banner" :class="{ supported }"><strong>{{ statusText }}</strong><span v-if="!supported">未检测到可用的风扇控制路线。</span></div>
    <section class="fan-card controls-card" aria-label="风扇控制状态">
      <div class="control-line" data-gp-row="0"><button class="control-state control-button fan-toggle" :class="{ 'fan-toggle-active': controlActive }" data-gp-row="0" data-gp-col="0" type="button" :disabled="!supported || !controlReady || busy" @click="toggleControl">{{ controlText }}</button><span class="control-state">期望转速 {{ expectedDuty }}%</span><span class="control-state temperature-state">温度 {{ temperatureText }}</span></div>
      <div class="control-line" data-gp-row="1"><Dropdown class="control-dropdown" popup-class="fan-control-popup" :model-value="selectedPreset" :options="PRESET_OPTIONS" :disabled="!supported || !controlReady || busy" aria-label="转速预设" gp-row="1" gp-col="0" @change="choosePreset" /><button class="control-state control-button" data-gp-row="1" data-gp-col="1" type="button" :disabled="!supported || !controlReady || busy" @click="resetCurve">重置转速</button><button class="control-state control-button" data-gp-row="1" data-gp-col="2" type="button" :disabled="!supported || !controlReady" @click="toggleMotion">风扇图标 {{ motionEnabled ? '转动' : '静止' }}</button></div>
    </section>
    <section class="fan-card chart-card" aria-label="四节点风扇曲线">
      <div class="chart-title"><strong>风扇曲线</strong></div>
      <div ref="chartWrap" class="chart-wrap">
        <svg class="fan-chart" viewBox="0 0 650 370" role="img" aria-label="四节点风扇曲线">
          <g class="grid-lines">
            <line v-for="i in 6" :key="`h${i}`" :x1="GRAPH_LEFT" :y1="GRAPH_TOP + (i - 1) * 50" x2="636" :y2="GRAPH_TOP + (i - 1) * 50" />
            <line v-for="i in 11" :key="`v${i}`" :x1="graphX((i - 1) * 10)" :y1="GRAPH_TOP" :x2="graphX((i - 1) * 10)" :y2="GRAPH_BOTTOM" />
          </g>
          <g class="axis-ticks" aria-hidden="true">
            <text v-for="value in AXIS_TICKS" :key="`x-tick-${value}`" :x="graphX(value)" y="346" text-anchor="middle">{{ value }}</text>
            <text v-for="value in AXIS_TICKS" :key="`y-tick-${value}`" x="46" :y="graphY(value) + 5" text-anchor="end">{{ value }}</text>
          </g>
          <polyline class="curve" :points="chartPoints" />
          <g v-for="(node, index) in nodes" :key="index" class="chart-node">
            <circle :cx="graphX(node.tempC)" :cy="graphY(node.dutyPercent)" r="14" class="node-halo" />
            <circle :cx="graphX(node.tempC)" :cy="graphY(node.dutyPercent)" r="7" :class="['node-point', { active: graphSelectedNode === index }]" />
            <text class="node-title" :x="graphX(node.tempC)" :y="Math.max(28, graphY(node.dutyPercent) - 50)" :text-anchor="index === 0 ? 'start' : index === nodes.length - 1 ? 'end' : 'middle'">节点 {{ index + 1 }}</text>
            <text class="node-value" :x="graphX(node.tempC)" :y="Math.max(46, graphY(node.dutyPercent) - 24)" :text-anchor="index === 0 ? 'start' : index === nodes.length - 1 ? 'end' : 'middle'">{{ node.tempC }}°C · {{ node.dutyPercent }}%</text>
          </g>
          <text x="325" y="366" text-anchor="middle" class="axis-label">温度 (°C)</text>
          <text x="14" y="195" text-anchor="middle" class="axis-label" transform="rotate(-90 14 195)">风扇转速 (%)</text>
        </svg>
        <div class="chart-node-hit-layer" aria-label="曲线节点">
          <button v-for="(node, index) in nodes" :key="`hit-${index}`" class="chart-node-hit" :class="{ active: graphSelectedNode === index, 'edit-armed': graphDragArmed === index }" :style="nodePointStyle(node)" type="button" :data-gp-row="2" :data-gp-col="index" data-gp-inline-edit :data-gp-inline-edit-active="graphDragArmed === index ? 'true' : undefined" :aria-pressed="graphDragArmed === index" :aria-label="`节点 ${index + 1} ${node.tempC}°C ${node.dutyPercent}%`" @focus="graphSelectedNode = index" @click="onGraphNodeClick(index, $event)" @gp:spatial-edit="onGraphSpatialEdit(index, $event)" @pointerdown="onGraphNodePointerDown(index, $event)" @pointermove="onGraphNodePointerMove(index, $event)" @pointerup="onGraphNodePointerUp(index, $event)" @pointercancel="onGraphNodePointerUp(index, $event)"><span>节点 {{ index + 1 }}</span></button>
        </div>
      </div>
      <div class="node-grid" aria-label="节点配置"><article v-for="(node, index) in nodes" :key="index" class="node-card" :class="{ active: configSelectedNode === index }" @click="configSelectedNode = index"><strong>节点 {{ index + 1 }}</strong><label>温度<Dropdown :model-value="node.tempC" :options="nodeDropdownOptions(index, 'tempC')" :disabled="!supported || busy" :aria-label="`节点 ${index + 1} 温度`" :gp-row="3" :gp-col="index" @change="updateNodeValue(index, 'tempC', $event)" /></label><label>转速<Dropdown :model-value="node.dutyPercent" :options="nodeDropdownOptions(index, 'dutyPercent')" :disabled="!supported || busy" :aria-label="`节点 ${index + 1} 转速`" :gp-row="4" :gp-col="index" @change="updateNodeValue(index, 'dutyPercent', $event)" /></label></article></div>
    </section>
  </section>
</template>

<style scoped>
.fan-page { height: 100%; overflow: auto; padding: 14px 14px 28px; }.fan-header { display:flex;align-items:center;justify-content:space-between;margin-bottom:10px }.fan-kicker{color:var(--accent);font-size:10px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}h1{margin:3px 0 0;font-size:21px}.fan-status-banner,.fan-card{border-radius:var(--radius);background:var(--bg-panel)}.fan-status-banner{display:grid;gap:4px;padding:10px 12px;margin-bottom:9px;color:var(--text-dim);font-size:11px}.fan-status-banner strong{color:var(--text);font-size:13px}.fan-status-banner.supported strong{color:var(--accent)}.fan-card{padding:11px 12px;margin-bottom:9px}.controls-card{display:grid;gap:8px}.control-line{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px}.control-state{min-width:0;min-height:35px;border:1px solid #29384a;border-radius:var(--radius-ctrl);background:var(--bg-input);color:var(--text-dim);font:inherit;font-size:11px;font-weight:700;text-align:center}.control-state{display:flex;align-items:center;justify-content:center;padding:0 6px}.control-dropdown{min-width:0}.control-dropdown :deep(.dd-trigger){justify-content:center;text-align:center;position:relative}.control-dropdown :deep(.dd-value){justify-content:center;text-align:center;padding:0 18px 0 4px}.control-dropdown :deep(.dd-caret){position:absolute;right:10px}.control-dropdown :deep(.dd-menu){min-width:100%}.control-dropdown :deep(.dd-option){position:relative;justify-content:center;text-align:center}.control-dropdown :deep(.dd-opt-label){text-align:center}.control-dropdown :deep(.dd-check){position:absolute;right:10px}.control-button:not(:disabled){cursor:pointer;color:var(--text)}.control-button:not(:disabled):hover{border-color:var(--accent);background:#162434}.fan-toggle-active:not(:disabled){background:#1269a3;border-color:#2ea6ff;color:#fff;box-shadow:0 0 0 1px rgba(46,166,255,.32),0 0 12px rgba(46,166,255,.2)}.fan-toggle-active:not(:disabled):hover{background:#197dbb;border-color:#65c1ff}.chart-title{display:flex;justify-content:space-between;align-items:center;margin-bottom:5px;color:var(--text-dim);font-size:10px}.chart-title strong{color:var(--text);font-size:13px}.chart-wrap{position:relative;width:100%;}.fan-chart{display:block;width:100%;height:auto;overflow:visible}.chart-node-hit-layer{position:absolute;inset:0;pointer-events:none}.chart-node-hit{position:absolute;transform:translate(-50%,-50%);width:32px;height:32px;padding:0;border:2px solid transparent;border-radius:50%;background:transparent;color:transparent;font-size:0;line-height:1;pointer-events:auto;cursor:pointer}.chart-node-hit::after{content:'';position:absolute;inset:8px;border-radius:50%;background:var(--accent);box-shadow:0 0 0 2px var(--bg-panel)}.chart-node-hit:hover,.chart-node-hit:focus-visible,.chart-node-hit.focused{border-color:#fff;outline:none;box-shadow:0 0 0 2px color-mix(in srgb,var(--accent) 45%,transparent)}.chart-node-hit.active::after{box-shadow:0 0 0 2px #fff,0 0 10px color-mix(in srgb,var(--accent) 55%,transparent)}.grid-lines line{stroke:color-mix(in srgb,var(--text-dim) 18%,transparent);stroke-width:1}.curve{fill:none;stroke:var(--accent);stroke-width:3;stroke-linejoin:round;stroke-linecap:round}.node-halo{fill:color-mix(in srgb,var(--accent) 14%,transparent)}.node-point{fill:var(--accent);stroke:var(--bg-panel);stroke-width:2}.node-point.active{stroke:#fff;stroke-width:2.5}.chart-node text{fill:var(--text);font-size:10px;pointer-events:none}.chart-node text+text{fill:var(--text-dim);font-size:9px}.axis-label{fill:var(--text-dim);font-size:10px}.node-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:7px;margin-top:8px}.node-card{display:grid;gap:5px;padding:8px;border:1px solid #29384a;border-radius:var(--radius-ctrl);background:var(--bg-input);cursor:pointer}.node-card.active{border-color:color-mix(in srgb,var(--accent) 55%,transparent)}.node-card strong{color:var(--text);font-size:11px;text-align:center}.node-card label{display:grid;gap:3px;color:var(--text-dim);font-size:10px;text-align:center}.node-card :deep(.dd-trigger){min-height:27px;padding:4px 7px;font-size:10px;text-align:center}.node-card :deep(.dd-value){justify-content:center}.node-card :deep(.dd-caret){width:12px;height:12px}.node-card :deep(.dd-menu){--dd-popup-font-size:12px;--dd-popup-option-py:7px;--dd-popup-option-px:8px}@media(max-width:560px){.control-line{grid-template-columns:1fr}.node-grid{grid-template-columns:repeat(2,minmax(0,1fr))}}
.chart-node-hit { touch-action: none; }
.chart-node-hit.edit-armed { border-color: var(--accent); box-shadow: 0 0 0 3px color-mix(in srgb,var(--accent) 40%,transparent); }
:global(.fan-control-popup) .dd-option { justify-content: center; text-align: center; position: relative; }
:global(.fan-control-popup) .dd-opt-label { text-align: center; flex: 1 1 auto; }
:global(.fan-control-popup) .dd-check { position: absolute; right: 10px; }
.axis-label { font-size: 20px; }
.chart-node text { font-size: 20px; font-weight: 700; paint-order: stroke; stroke: var(--bg-panel); stroke-width: 3px; stroke-linejoin: round; }
.chart-node .node-value { fill: var(--text-dim); font-size: 18px; }
.axis-ticks text { fill: var(--text-dim); font-size: 16px; font-weight: 600; pointer-events: none; }
</style>
