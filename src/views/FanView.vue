<script setup lang="ts">
import { computed, nextTick, onActivated, onDeactivated, onMounted, onUnmounted, ref, watch } from 'vue';
import Dropdown from '@/components/Dropdown.vue';
import Toggle from '@/components/Toggle.vue';
import { topMonitorData } from '@/bridge/topmon';
import { onFanMirrorDisplay, registerFanMirrorUiBlocker, notifyFanMirrorUiGate } from '@/bridge/deckyFanDisplay';
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
import { fanHostLifecycle, resolveFanEntryAction, type FanHostLifecycleState } from '@/bridge/fanHost';
import type { FanNode } from '@/bridge/fanApi';
import { normalizeFanNodes } from '@/bridge/fanCurve';
import { fanDiagnosticLog } from '@/bridge/fanDiagnostics';
import { readSettingsSection, saveSettingsSection } from '@/bridge/settingsRepository';
import { emitNativeEvent } from '@/bridge/ipc';

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
// FAN-926R U3: `connecting` is the immediate, non-blocking "Starting" state of
// the inner-page switch. A real start attempt is only ever made by this page,
// so `startAttempted` distinguishes "never asked" from "asked and refused".
const connecting = ref(false);
const startAttempted = ref(false);
const hostState = ref<FanHostLifecycleState>(FAN_FORCE_PREVIEW ? 'awaiting-control' : fanHostLifecycle.state);
const statusMessage = ref(FAN_FORCE_PREVIEW ? '模拟预览：握手成功' : '风扇控制未开启');
/**
 * FAN-926R UX-1（2026-09-27，用户反馈"按阶段显示文字"）：关闭同样是一段要等的过程
 * （交回 OEM → 释放 HC 会话），旧版在整段里仍显示"控制已开启"，用户会以为开关坏了。
 * `closing` 只驱动**文案**，不改变任何关闭顺序或判定。
 */
const closing = ref(false);
/**
 * FAN-938 R5 §P4：恢复 owner 仍在推进（bridge `recoveryActive` 为真）时，界面显示
 * “控制意图已开启，正在恢复”，**不**把它当作实际受控成功。该标记只驱动文案；真正的
 * 成功仍以当次控制结果（Host Ready + hardwareWritesEnabled）判定。
 */
const recovering = ref(false);
/** 启用/关闭在途的已用时长：仅用于把"还在等"讲清楚（首次启用真的需要十几秒）。 */
const busyElapsedMs = ref(0);
let busyTicker: ReturnType<typeof window.setInterval> | null = null;

/** 启动阶段 → 用户可读进度（阶段名来自 bridge 的只读 `startStage`，与诊断行同源）。 */
const START_STAGE_TEXT: Record<string, string> = {
  'launcher-enter': '正在启动风扇服务…',
  'host-executable-found': '正在启动风扇服务…',
  'runtime-manifest-resolved': '正在准备运行环境…',
  'fan-state-dir-resolved': '正在准备运行环境…',
  'resident-host-recovered': '正在准备运行环境…',
  'resident-host-adopted': '正在接管常驻风扇服务…',
  // FAN-927 §2：逐文件校验与重复安装验证已归部署边界；正常开启只核对一份小部署记录，
  // 因此不再有"正在校验运行依赖…"这个阶段（它曾占首次开启 8 秒以上）。
  'deployment-record-verified': '正在校验风扇服务文件…',
  'host-process-launched': '风扇服务已启动，正在连接…',
};

function startBusyTicker(): void {
  if (busyTicker !== null) return;
  busyElapsedMs.value = 0;
  const startedAt = Date.now();
  busyTicker = window.setInterval(() => {
    busyElapsedMs.value = Date.now() - startedAt;
    // 只读镜像：把 bridge 的权威状态同步给界面，不发起任何请求。
    hostState.value = fanHostLifecycle.state;
    syncRecoveryFlag();
  }, 250);
}

function stopBusyTicker(): void {
  if (busyTicker !== null) window.clearInterval(busyTicker);
  busyTicker = null;
  busyElapsedMs.value = 0;
}

/** FAN-938 R5 §P4：只读镜像恢复状态（不发请求、不改任何控制意图）。
 * FAN-938 R6 §P4：判据与 refreshTelemetry/onFanGuardState 一致——本页仍有在途控制意图
 * （有界提交在跑，或 bridge 恢复 owner 在推进）时必须保持"恢复中"，250ms 只读镜像不得
 * 在设备写入尚未返回时把界面提前打回"已开启"（不冒充成功）。 */
function syncRecoveryFlag(): void {
  recovering.value = controlIntentPending();
}

/**
 * FAN-932 §3.2：内页顶部的镜像开关，与 PowerView 的「开机启动项」共用同一个
 * `startupDesired.fanControl` 偏好。它只是一个偏好镜像：只读写设置，不调用
 * start/apply/close，不影响当前运行中的风扇会话；写在途用独立 busy 标记防重入
 * （不参与 `busy`，也不给开关加 :disabled——否则手柄焦点会被引擎回退到页首）。
 */
const fanBootAutoStart = ref(false);
let fanBootSettingBusy = false;

async function refreshFanBootAutoStart(): Promise<void> {
  try {
    const startup = await readSettingsSection<{ fanControl?: boolean }>('startupDesired');
    fanBootAutoStart.value = startup.fanControl === true;
  } catch {
    // 读取失败按关闭处理；只影响镜像显示，不改动任何控制状态。
  }
}

async function onFanBootAutoStartToggle(v: boolean): Promise<void> {
  if (fanBootSettingBusy) return; // 双击/重复事件只产生一次写入
  fanBootSettingBusy = true;
  const prev = fanBootAutoStart.value;
  fanBootAutoStart.value = v;
  try {
    await saveSettingsSection('startupDesired', { fanControl: v });
  } catch {
    fanBootAutoStart.value = prev;
    statusMessage.value = '开机/休眠唤醒启动风扇设置保存失败';
  } finally {
    fanBootSettingBusy = false;
  }
}

/** 启用在途时的阶段文字（按 bridge 报出的真实阶段，逐段推进）。 */
const enableStageText = computed(() => {
  const stage = fanHostLifecycle.startStage;
  if (stage && START_STAGE_TEXT[stage]) return START_STAGE_TEXT[stage];
  if (hostState.value === 'handshaking') return '正在连接风扇服务…';
  if (hostState.value === 'awaiting-control') return '正在接管风扇，马上就好…';
  // 首次启用要准备运行环境，实测约 10–20 秒：超过 6 秒就把这件事说出来，
  // 否则用户会以为卡死。
  if (busyElapsedMs.value >= 6000) return '正在准备运行环境（首次启用需要十几秒）…';
  return '正在启动风扇服务…';
});

const chartWrap = ref<HTMLElement | null>(null);
const draggingNode = ref<number | null>(null);
const graphDragArmed = ref<number | null>(null);
let telemetryTimer: ReturnType<typeof window.setInterval> | null = null;
/**
 * FAN-931 P1.4：5 s 展示更新只在「本页当前激活**且**可见」时运行。
 * KeepAlive 缓存下切走页面只触发 onDeactivated（不触发 onUnmounted），旧实现会让隐藏页
 * 继续每 5 s 读一次 Host 状态；异步 enable/adopt 返回也会把已停的隐藏页定时器复活。
 * 这里用 viewActive/viewVisible 两个前置条件把三种情况（失活 / document 隐藏 / 卸载）
 * 一并挡住，且不改变任何全局控制意图或续租（那是 fanHost.ts heartbeat 的职责）。
 */
let viewActive = false;
let viewVisible = !document.hidden;
/** 视图代：失活/卸载/停轮询时递增；迟到的 in-flight 结果按代丢弃，不写回已停页面。 */
let telemetryGeneration = 0;
/** 本代是否有 GET 在途（实现"最多一个在途"）：键是代，换代后旧请求不再阻塞新请求。 */
let telemetryInFlightGeneration = -1;
let curveApplyPromise: Promise<void> | null = null;
let curveApplyPending = false;
let fanSuspendBoundary = false;
let fanPowerGeneration = 0;
/**
 * FAN-938 R6 §P4：控制在途提交的**有界受理窗口**（目标 ≤2s）。开启/重试只在这个窗口内
 * 占住 `busy`/`connecting`，窗口结束即释放；后台设备提交继续推进，页面保持可操作，
 * 关闭/重试不再被长期 busy/connecting 无声丢弃。
 */
const CONTROL_ADMISSION_BUDGET_MS = 2000;
/** FAN-938 R6 §P4：控制意图修订号。关闭/新睡眠/新意图递增，使在途提交的最终写入失效。 */
let controlCommitEpoch = 0;
/** FAN-938 R6 §P4：唯一的后台设备提交任务；不复用 busy/connecting，故不阻塞页面。 */
let controlCommitTask: Promise<void> | null = null;
/** FAN-938 R6 §P4：在途提交任务所持的修订号；与 `controlCommitEpoch` 不等即视为已失效。 */
let controlCommitEpochInFlight = -1;

/** Keep the graph point and its lower editor card as one spatial selection.
 * The graph and the four cards are two renderings of the same node; allowing
 * them to drift made the controller highlight a different node than the one
 * being edited. */
function selectNode(index: number): void {
  graphSelectedNode.value = index;
  configSelectedNode.value = index;
}

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
  // UX-1：启用/关闭在途时先说清"正在做什么"，不要沿用上一次的结论文字。
  if (closing.value) return '关闭中';
  if (connecting.value) return enableStageText.value;
  // FAN-938 R5 §P4：恢复推进中不冒充"控制已开启"。
  if (recovering.value) return '正在恢复';
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
  // FAN-933 §2.2/§4.8：唤醒后的会话重开（rearm）还在进行/被新睡眠取代时的**可重试**文案。
  // 清除 busy/connecting 后按钮保持可用，不把页面锁在失败态，也不自动连续重试。
  if (/FAN_RESUME_ADMISSION_RECOVERING|FAN_RESUME_WAIT_|FAN_RESUME_ADMISSION_/i.test(message)) {
    return '系统正在恢复，风扇服务将在恢复后重新接管，请稍后重试';
  }
  if (/FAN_UNSUPPORTED|风扇不支持/i.test(message)) return '风扇不支持';
  if (/FAN_ROUTE_CONFLICT|HC_OPENLIB_CONFLICT|EXTERNAL_FAN_OWNER/i.test(message)) {
    return '检测到其他风扇控制程序，已停止控制并交回 OEM';
  }
  if (/LEASE_INVALID|LEASE_REQUIRED|lease/i.test(message)) return '风扇控制租约已失效，请重试';
  return userMessage || '风扇控制请求失败';
}
// The button reflects the requested on/off state; lifecycle progress stays above it.
const controlText = computed(() => controlActive.value ? '风扇已开启' : '风扇未操控');
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
  const max = field === 'tempC' && index === 2 ? 85 : 100;
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
  selectNode(index);
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

function syncGraphNodeEditMarker(index: number, active: boolean, target?: HTMLElement | null): void {
  const button = target ?? document.querySelector<HTMLElement>(
    `[data-gp-inline-edit-scope="fan-curve-node"][data-gp-col="${index}"]`,
  );
  if (!button) return;
  if (active) {
    // Keep the native direction event routable before Vue's next render turn.
    button.classList.add('edit-armed');
    button.dataset.gpInlineEditActive = 'true';
    button.setAttribute('aria-pressed', 'true');
  } else {
    button.classList.remove('edit-armed');
    delete button.dataset.gpInlineEditActive;
    button.setAttribute('aria-pressed', 'false');
  }
}

function onGraphNodeClick(index: number, event: MouseEvent): void {
  selectNode(index);
  // Native gamepad A and keyboard Enter both invoke HTMLElement.click() with
  // detail=0. A first press arms the shared spatial editor; a second press
  // commits it. A physical mouse click remains a selection only, preserving
  // direct pointer dragging.
  if (event.detail !== 0) {
    const previous = graphDragArmed.value;
    if (previous !== null) syncGraphNodeEditMarker(previous, false);
    graphDragArmed.value = null;
    syncGraphNodeEditMarker(index, false, event.currentTarget as HTMLElement);
    emitNativeEvent('gamepad.fan-node-edit', { active: false });
    return;
  }
  if (graphDragArmed.value === index) {
    graphDragArmed.value = null;
    syncGraphNodeEditMarker(index, false, event.currentTarget as HTMLElement);
    emitNativeEvent('gamepad.fan-node-edit', { active: false });
    commitCurve(nodes.value.map((node) => ({ ...node })));
    statusMessage.value = controlActive.value
      ? `节点 ${index + 1} 已应用`
      : `节点 ${index + 1} 已保存（未写入硬件：风扇控制未开启）`;
  } else {
    const previous = graphDragArmed.value;
    if (previous !== null) syncGraphNodeEditMarker(previous, false);
    graphDragArmed.value = index;
    syncGraphNodeEditMarker(index, true, event.currentTarget as HTMLElement);
    emitNativeEvent('gamepad.fan-node-edit', { active: true });
    statusMessage.value = `节点 ${index + 1} 编辑中：方向键调整，A 确认`;
  }
}

function onGraphSpatialEdit(index: number, event: Event): void {
  if (graphDragArmed.value !== index) return;
  selectNode(index);
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
  syncGraphNodeEditMarker(graphDragArmed.value, false);
  graphDragArmed.value = null;
  emitNativeEvent('gamepad.fan-node-edit', { active: false });
  statusMessage.value = '已取消节点编辑';
  event.preventDefault();
}

async function refreshTelemetry(): Promise<void> {
  if (!controlActive.value) return;
  if (FAN_FORCE_PREVIEW) return;
  // FAN-931 P1.4：最多一个 GET 在途——重叠 tick 直接跳过（不排队、不叠加）。
  if (telemetryInFlightGeneration === telemetryGeneration) return;
  const generation = telemetryGeneration;
  telemetryInFlightGeneration = generation;
  try {
    const state = await fanHostLifecycle.getState();
    // 迟到的 in-flight 结果按视图代丢弃：页面失活/隐藏/卸载后不得再写回状态。
    if (generation !== telemetryGeneration) return;
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
    const remoteNotReady = state.unknownState === true || !['Ready', 'Open'].includes(state.state)
      || remoteWritesLost || remoteRestoreAlreadyConfirmed || state.hcCloseCleanupPending === true || remoteLeaseMismatch;
    if (remoteNotReady) {
      // FAN-938 R5 §P4：恢复 owner 仍在推进时，一次只读快照**不得**把界面打回“服务已停止，
      // 请重新打开开关”（现场失败形态）。保持“控制意图已开启，正在恢复”，等 owner 真正接管
      // 后由下一次快照转成“控制已开启”。OEM 已交回（restoreConfirmed）或租约失效是真实终止，
      // 不在此保留恢复态。
      // FAN-938 R6 §P4：判据是"本页仍有在途控制意图"（有界提交任务在跑，或 bridge 恢复
      // owner 在推进），而不仅是 bridge 的恢复标记。冷启动早期 controlActive 已置位、
      // bridge 尚未登记 owner 时，5 s 只读快照不得把界面打回"服务已停止"。
      // R7: telemetry is also a recovery request source. If the renderer missed
      // the one-shot wake event, hand the existing curve to the same bridge owner;
      // it reads native power state and stays write-closed until the cycle is safe.
      if (!remoteLeaseMismatch && fanHostLifecycle.hasControlIntent) {
        await fanHostLifecycle.requestRecoveryAfterObservedHostState('telemetry-host-not-ready').catch(() => false);
      }
      if (controlIntentPending() && !remoteLeaseMismatch) {
        recovering.value = true;
        controlActive.value = true;
        hostState.value = fanHostLifecycle.state;
        statusMessage.value = '控制意图已开启，正在恢复…';
        return;
      }
      recovering.value = false;
      controlActive.value = false;
      setFanControlActive(false);
      setFanNavigationDuty(0);
      // FAN-926R-WAKEUI: a lost session does not come back on its own (the Host
      // recovery ends exhausted and only re-arms on the next power event), so the
      // copy must not promise a 10 second guard recovery.
      statusMessage.value = remoteLeaseMismatch ? '风扇租约已失效，请重新打开开关' : '风扇服务已停止，请重新打开开关';
    } else if (recovering.value) {
      // FAN-938 R5 §P4：恢复达成——清除恢复文案，按**当次真实控制结果**转为“控制已开启”。
      recovering.value = false;
      controlReady.value = fanHostLifecycle.controlReady;
      controlActive.value = true;
      setFanControlActive(true);
      setFanNavigationDuty(expectedDuty.value);
      statusMessage.value = '控制已开启';
      fanDiagnosticLog('ui.recovery-completed', { state: state.state });
    }
  } catch {
    // 迟到失败同样按代丢弃：不得让已失活/隐藏的页面把状态改成“服务未响应”。
    if (generation !== telemetryGeneration) return;
    hostState.value = fanHostLifecycle.state;
    if (fanHostLifecycle.hasControlIntent) {
      // A Host GET timeout is not proof that the user's control intent ended.
      // Keep the page retryable and let the same owner adopt the wake once the
      // Fan-only power snapshot becomes readable; its write gate stays closed
      // throughout a real suspend.
      const recoveryRequested = await fanHostLifecycle
        .requestRecoveryAfterObservedHostState('telemetry-host-state-unavailable')
        .catch(() => false);
      recovering.value = true;
      controlActive.value = true;
      setFanControlActive(true);
      setFanNavigationDuty(expectedDuty.value);
      statusMessage.value = recoveryRequested || fanHostLifecycle.recoveryActive
        ? '控制意图已保留，正在恢复…'
        : '风扇状态暂不可读，控制意图已保留；正在继续检查…';
      startTelemetry();
      return;
    }
    controlActive.value = false;
    setFanControlActive(false);
    setFanNavigationDuty(0);
    statusMessage.value = '风扇服务未响应，请重新打开开关';
  } finally {
    if (telemetryInFlightGeneration === generation) telemetryInFlightGeneration = -1;
  }
}

function startTelemetry(): void {
  // FAN-931 P1.4：失去「激活且可见」后，异步 enable/adopt/guard-state 的返回**不得复活**
  // 已停的页面定时器——这是旧实现"隐藏页仍每 5 s 取数"的来源之一。
  if (!viewActive || !viewVisible) return;
  if (telemetryTimer !== null) return;
  void refreshTelemetry();
  // FAN-929（2026-09-27 用户裁决）：运行期自证回读从 1 Hz 降到 0.2 Hz。
  // 1 Hz 是"侧边栏常驻 + 控制已开启"时唯一恒定的渲染侧周期成本；降频后
  // 稳态请求降为 1/5，代价是"Host 已交回 OEM/租约失效"的 UI 提示最多晚 5 s。
  telemetryTimer = window.setInterval(() => { void refreshTelemetry(); }, 5000);
}

function stopTelemetry(): void {
  if (telemetryTimer !== null) window.clearInterval(telemetryTimer);
  telemetryTimer = null;
  // 换代：在途结果按代丢弃（并释放"最多一个在途"的占用，使重现后能立即取数）。
  telemetryGeneration += 1;
}

/** FAN-931 P1.4：document 隐藏同样停止展示轮询（切走靠 onDeactivated 拦截）。 */
function onDocumentVisibilityChange(): void {
  viewVisible = !document.hidden;
  if (!viewActive || !viewVisible) {
    stopTelemetry();
    return;
  }
  if (controlActive.value) startTelemetry();
}

function onFanPowerSuspending(event: Event): void {
  const generation = Number((event as CustomEvent<{ generation?: number }>).detail?.generation) || 0;
  if (generation > fanPowerGeneration) fanPowerGeneration = generation;
  // App.vue is the sole lifecycle owner. FanView only consumes the boundary
  // to close its UI/write indicator immediately; it never calls Host suspend.
  if (!fanSuspendBoundary) {
    fanSuspendBoundary = true;
  }
  // FAN-938 R6 §P4：新睡眠使在途控制提交整体失效，避免旧曲线在唤醒前后被错误复活写入。
  controlCommitEpoch += 1;
  controlActive.value = false;
  recovering.value = false;
  // The old admission task is now stale. Release only its UI admission locks so
  // the first post-wake manual click cannot remain disabled behind a request
  // that was superseded by this sleep generation. The lifecycle queue still
  // serializes any already-entered Host operation and rejects stale writes.
  if (!closing.value) {
    busy.value = false;
    connecting.value = false;
  }
  setFanControlActive(false);
  setFanNavigationDuty(0);
  stopTelemetry();
  hostState.value = 'suspended';
  // Evidence FAN-926R-WAKEUI: the suspend boundary closed HC but could NOT prove the
  // OEM release (restore.close-hc-release-unproven), so the honest statement is
  // "handed back, ownership unproven" - not a claimed successful OEM restore.
  // UX-1：并说明恢复由**电源事件**驱动（本次真机 11/11 次唤醒都完成了曲线恢复）。
  statusMessage.value = '睡眠中：已交回 OEM 默认转速（物理归属未证实）；唤醒后随电源事件自动恢复控制。';
}

function onFanGuardState(event: Event): void {
  const detail = (event as CustomEvent<{ active?: boolean; state?: FanHostLifecycleState; error?: string }>).detail ?? {};
  fanSuspendBoundary = false;
  controlReady.value = fanHostLifecycle.controlReady;
  hostState.value = fanHostLifecycle.state;
  if (detail.active === true && fanHostLifecycle.state === 'ready') {
    recovering.value = false;
    controlActive.value = true;
    setFanControlActive(true);
    setFanNavigationDuty(expectedDuty.value);
    startTelemetry();
    statusMessage.value = '风扇开启守护已恢复控制';
    return;
  }
  // FAN-938 R5 §P4：守护事件到达时若恢复 owner 仍在推进，不得把它显示成"服务已停止"。
  // FAN-938 R6 §P4：判据同为"本页仍有在途控制意图"（后台提交在跑或 bridge owner 在推进）。
  if (controlIntentPending()) {
    recovering.value = true;
    controlActive.value = true;
    setFanControlActive(true);
    setFanNavigationDuty(expectedDuty.value);
    startTelemetry();
    statusMessage.value = '控制意图已开启，正在恢复…';
    return;
  }
  recovering.value = false;
  controlActive.value = false;
  setFanControlActive(false);
  setFanNavigationDuty(0);
  stopTelemetry();
  statusMessage.value = supported.value ? '风扇服务已停止，请重新打开开关' : '风扇不支持';
}

async function ensureSupported(manualRecovery = false): Promise<boolean> {
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
  if (fanHostLifecycle.state === 'awaiting-control' || fanHostLifecycle.state === 'ready' || fanHostLifecycle.state === 'suspended') {
    supported.value = true;
    controlReady.value = fanHostLifecycle.controlReady;
    hostState.value = fanHostLifecycle.state;
    statusMessage.value = fanHostLifecycle.controlReady
      ? (fanHostLifecycle.state === 'ready' ? '控制已开启' : '握手成功')
      : '握手成功，真实写入能力待确认';
    return true;
  }
  try {
    // FAN-926R U2: this is the first and only place that spawns the Fan Host.
    // Nothing above probed it, so the handshake result below is this page's own
    // on-demand admission rather than a boot-time resident check.
    startAttempted.value = true;
    const gate = await fanHostLifecycle.start(manualRecovery
      ? { manualRecovery: true, reason: 'user-action-while-suspended' }
      : undefined);
    supported.value = gate.allowed;
    controlReady.value = gate.writeReady;
    hostState.value = fanHostLifecycle.state;
    statusMessage.value = gate.allowed
      ? (gate.writeReady ? '握手成功' : '已识别风扇路线，真实写入尚未验证')
      : '风扇不支持';
    await recordFanHandshake(gate.allowed);
    return gate.allowed;
  } catch (error) {
    // A failed start/handshake is not allowed to leave the remembered device
    // visible as if this session were ready. The saved identity remains in
    // settings for the next retry, while the current route is fail-closed.
    await recordFanHandshake(false);
    supported.value = false;
    controlReady.value = false;
    hostState.value = fanHostLifecycle.state;
    statusMessage.value = readableFanError(error);
    return false;
  }
}

/**
 * FanView is allowed to unmount when the user changes the main page. The
 * resident Host and its lease deliberately stay alive, so a fresh view must
 * adopt the Host's authoritative Ready/write state instead of defaulting its
 * local button back to “未开启”. This is read-only and never sends a second
 * Open/Enable request.
 */
async function adoptResidentControlState(): Promise<void> {
  if (FAN_FORCE_PREVIEW) return;
  try {
    const remote = await fanHostLifecycle.getState();
    hostState.value = fanHostLifecycle.state;
    const active = ['Ready', 'Open'].includes(String(remote.state)) &&
      remote.hardwareWritesEnabled === true &&
      remote.unknownState !== true &&
      remote.oemRestoreConfirmed !== true &&
      remote.hcCloseCleanupPending !== true;
    controlActive.value = active;
    setFanControlActive(active);
    if (active) {
      recovering.value = false;
      controlReady.value = true;
      statusMessage.value = '控制已开启';
      setFanNavigationDuty(expectedDuty.value);
      startTelemetry();
      fanDiagnosticLog('ui.resident-control-adopted', { state: remote.state });
    } else if (fanHostLifecycle.recoveryActive) {
      // FAN-938 R5 §P4：驻留会话正在恢复时重新进页，如实显示"正在恢复"，不冒充成功。
      recovering.value = true;
      controlActive.value = true;
      setFanControlActive(true);
      statusMessage.value = '控制意图已开启，正在恢复…';
      setFanNavigationDuty(expectedDuty.value);
      startTelemetry();
    } else {
      recovering.value = false;
      setFanNavigationDuty(0);
    }
  } catch {
    // The normal telemetry loop performs safety recovery if the resident Host
    // cannot be read; do not issue a second close from a remounted page.
  }
}


/** 官方 R-B（统一文档 §35.5 ②）：点启用/切预设必须先 start() 做可达性续证。
 * 不改 apply/resume/心跳。进页 ensureSupported 仍可短路，避免每次进风扇页重建。 */
async function confirmFanHostBeforeWrite(): Promise<void> {
  const gate = await fanHostLifecycle.start({
    manualRecovery: true,
    reason: 'user-action-before-control-write',
  });
  if (!gate.allowed) throw new Error(gate.reason || '风扇服务不可用');
  controlReady.value = gate.writeReady;
  hostState.value = fanHostLifecycle.state;
}

async function applyCurveOnce(): Promise<void> {
  if (busy.value) return;
  busy.value = true;
  try {
    fanDiagnosticLog('ui.control-enable-begin', { preset: selectedPreset.value, nodes: nodes.value });
    if (!controlActive.value && !FAN_FORCE_PREVIEW) {
      // FAN-926R U3: publish the Starting/Connecting state and let Vue paint it
      // before the spawn/handshake round trip. The switch never waits on the HC
      // readiness path, which can legitimately take ten seconds.
      connecting.value = true;
      hostState.value = 'starting';
      statusMessage.value = '启用中：正在启动风扇服务…';
      startBusyTicker();
      await nextTick();
    }
    // FAN-938 R6 §P4：已有**仍有效**的后台提交在途时，本次点击只刷新最新意图（曲线/预设），
    // 由在途任务在取得许可后写入最新值；不排队、不叠加第二个 owner，立即有界返回。
    if (controlCommitTask !== null && controlCommitEpochInFlight === controlCommitEpoch) {
      publishControlIntent();
      return;
    }
    publishControlIntent();
    const epoch = ++controlCommitEpoch;
    controlCommitEpochInFlight = epoch;
    const task = commitControlCurve(epoch);
    controlCommitTask = task;
    void task.then(
      () => { if (controlCommitTask === task) controlCommitTask = null; },
      () => { if (controlCommitTask === task) controlCommitTask = null; },
    );
    // FAN-938 R6 §P4 有界受理：开启/重试在 ≤2s 内返回，页面恢复可操作；后台设备提交
    // 继续推进（不占用 busy/connecting），故"长期 busy/connecting"不再无声丢弃点击。
    await Promise.race([
      task,
      new Promise<void>((resolve) => { window.setTimeout(resolve, CONTROL_ADMISSION_BUDGET_MS); }),
    ]);
  } finally {
    busy.value = false;
    connecting.value = false;
    stopBusyTicker();
  }
}

/** FAN-938 R6 §P4：登记/刷新"控制意图已开启"。只更新本页意图与文案，不写设备。 */
function publishControlIntent(): void {
  recovering.value = true;
  controlActive.value = true;
  hostState.value = fanHostLifecycle.state;
  statusMessage.value = '控制意图已开启，正在恢复…';
  startTelemetry();
}

/** FAN-938 R6 §P4：本页是否仍有在途控制意图（有界提交在跑，或 bridge 恢复 owner 在推进）。 */
function controlIntentPending(): boolean {
  return controlCommitTask !== null || fanHostLifecycle.recoveryActive || fanHostLifecycle.hasControlIntent;
}

/**
 * FAN-938 R6 §P4：唯一的后台设备提交任务（自动恢复与手动点击共用同一条恢复链）。
 * 只发布"最新意图"（`nodes.value`/预设），每个 await 后复核 `epoch` 令旧提交失效；本任务
 * 永不 reject（失败在内部收敛），因此既不阻塞页面，也不产生未处理拒绝。
 */
async function commitControlCurve(epoch: number): Promise<void> {
  try {
    if (!(await ensureSupported(true))) {
      if (epoch !== controlCommitEpoch) return;
      controlActive.value = false;
      recovering.value = false;
      setFanControlActive(false);
      setFanNavigationDuty(0);
      stopTelemetry();
      hostState.value = fanHostLifecycle.state;
      return;
    }
    // 每个 await 后复核任务身份：关闭/新睡眠/新曲线已令本提交失效，不得复活它。
    if (epoch !== controlCommitEpoch) return;
    if (!controlReady.value) {
      // FAN-938 R6.2 T0：唤醒后的遥测可能先把页面镜像降为“未就绪”，
      // 但 FanHost 仍持有本代握手/控制意图，或正处于陈旧 Suspended 救援窗。
      // 这种情况下显式点击必须继续进入 bridge 的手动 rearm；只有完全没有
      // 路线/意图证据的新设备才在 UI 层拒绝。
      const hostCanRescue = fanHostLifecycle.controlReady
        || fanHostLifecycle.hasControlIntent
        || fanHostLifecycle.recoveryActive
        || fanHostLifecycle.processId !== null
        || fanHostLifecycle.state === 'suspended'
        || fanHostLifecycle.state === 'ready'
        || fanHostLifecycle.state === 'awaiting-control'
        || fanHostLifecycle.state === 'starting'
        || fanHostLifecycle.state === 'handshaking';
      if (!hostCanRescue) {
        statusMessage.value = '已识别风扇路线，但真实写入/恢复验证尚未完成';
        return;
      }
      if (fanHostLifecycle.recoveryActive) {
        fanHostLifecycle.stageRecoveryCurve(nodes.value);
        publishControlIntent();
        return;
      }
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
    await confirmFanHostBeforeWrite();
    if (epoch !== controlCommitEpoch) return;
    await fanHostLifecycle.apply(nodes.value);
    if (epoch !== controlCommitEpoch) return;
    fanDiagnosticLog('ui.control-enable-success', { preset: selectedPreset.value });
    controlActive.value = true;
    setFanControlActive(true);
    hostState.value = fanHostLifecycle.state;
    statusMessage.value = '控制已开启';
    setFanNavigationDuty(expectedDuty.value);
    startTelemetry();
  } catch (error) {
    if (epoch !== controlCommitEpoch) return;
    fanDiagnosticLog('ui.control-enable-failure', { state: fanHostLifecycle.state, error: error instanceof Error ? error.message : String(error) });
    hostState.value = fanHostLifecycle.state;
    // FAN-938 R6.2 T0：a stale Suspended wake edge is a recoverable manual
    // request. The bridge keeps the staged curve and a single recovery owner;
    // do not turn that explicit click into a dead switch just because the
    // first bounded HTTP attempt returned POWER_RESUMING/Suspended.
    if (fanHostLifecycle.recoveryActive || fanHostLifecycle.hasControlIntent) {
      publishControlIntent();
      recovering.value = true;
      controlActive.value = true;
      setFanControlActive(true);
      setFanNavigationDuty(expectedDuty.value);
      statusMessage.value = '控制意图已登记，正在恢复…';
      startTelemetry();
      return;
    }
    statusMessage.value = `风扇控制失败：${readableFanError(error)}，请重试`;
    controlActive.value = false;
    setFanControlActive(false);
    setFanNavigationDuty(0);
    stopTelemetry();
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

async function closeFanControl(): Promise<void> {
  // Switching off also cancels an explicit control request while recovery is pending.
  // disable() invalidates the recovery owner before entering its serialized queue.
  if (closing.value) return;
  fanDiagnosticLog('ui.control-disable-begin');
  const epoch = ++controlCommitEpoch;
  if (FAN_FORCE_PREVIEW) {
    controlActive.value = false;
    recovering.value = false;
    setFanControlActive(false);
    setFanNavigationDuty(0);
    stopTelemetry();
    hostState.value = 'awaiting-control';
    statusMessage.value = '模拟控制已关闭';
    return;
  }
  busy.value = true;
  closing.value = true;
  statusMessage.value = '关闭中：正在交回 OEM 默认转速…';
  startBusyTicker();
  await nextTick();
  // FAN-941: relinquishing the user's intent must not disable the only
  // switch for the full lifetime of a queued/native cleanup. The background
  // disable still owns the same serialized queue; its deadline is never proof
  // of OEM release or permission to launch a second hardware writer.
  let settled = false;
  const task = (async () => {
    try {
      await fanHostLifecycle.disable();
      if (epoch !== controlCommitEpoch) return;
      controlActive.value = false;
      recovering.value = false;
      setFanControlActive(false);
      setFanNavigationDuty(0);
      stopTelemetry();
      hostState.value = fanHostLifecycle.state;
      statusMessage.value = '控制已关闭';
      fanDiagnosticLog('ui.control-disable-success');
    } catch (error) {
      if (epoch !== controlCommitEpoch) return;
      fanDiagnosticLog('ui.control-disable-failure', { state: fanHostLifecycle.state, error: error instanceof Error ? error.message : String(error) });
      controlActive.value = false;
      recovering.value = false;
      setFanControlActive(false);
      setFanNavigationDuty(0);
      stopTelemetry();
      hostState.value = fanHostLifecycle.state;
      statusMessage.value = readableFanError(error);
    } finally {
      settled = true;
    }
  })();
  let timer: ReturnType<typeof window.setTimeout> | undefined;
  try {
    await Promise.race([
      task,
      new Promise<void>(resolve => { timer = window.setTimeout(resolve, CONTROL_ADMISSION_BUDGET_MS); }),
    ]);
    if (!settled && epoch === controlCommitEpoch) {
      controlActive.value = false;
      recovering.value = false;
      setFanControlActive(false);
      setFanNavigationDuty(0);
      stopTelemetry();
      hostState.value = fanHostLifecycle.state;
      statusMessage.value = '已取消控制，风扇交还仍在进行';
      fanDiagnosticLog('ui.control-disable-pending', { state: fanHostLifecycle.state });
    }
  } finally {
    if (timer !== undefined) window.clearTimeout(timer);
    busy.value = false;
    closing.value = false;
    stopBusyTicker();
  }
}

async function toggleControl(): Promise<void> {
  if (closing.value) return;
  // One switch owns both directions, including an in-flight recovery. Turning
  // it off invalidates the pending intent before disable enters the queue.
  if (controlActive.value) {
    await closeFanControl();
    return;
  }
  if (busy.value || connecting.value) return;
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
      await confirmFanHostBeforeWrite();
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
  // FAN-938 R6 §P4：退出清理边界同样使在途控制提交失效（安全交还优先于旧意图）。
  controlCommitEpoch += 1;
  controlActive.value = false;
  setFanControlActive(false);
  setFanNavigationDuty(0);
  stopTelemetry();
  statusMessage.value = typeof detail?.message === 'string'
    ? detail.message
    : '风扇 OEM 恢复未确认，已取消退出';
}

onMounted(() => {
  // FAN-931 P1.4：本页在 KeepAlive 内；mounted 后紧跟 activated，故此处先置位，
  // 使挂载期间完成的 adopt 能正常开启展示更新（失活时由 onDeactivated 收回）。
  viewActive = true;
  viewVisible = !document.hidden;
  document.addEventListener('visibilitychange', onDocumentVisibilityChange);
  window.addEventListener('ipc:power.suspending', onFanPowerSuspending);
  window.addEventListener('fan:guard-state', onFanGuardState);
  window.addEventListener('ipc:gamepad-back', onFanGamepadBack);
  window.addEventListener('ipc:fan.exit.cleanup-failed', onFanExitCleanupFailed);
  if (FAN_FORCE_PREVIEW) {
    supported.value = true;
    return;
  }
  // FAN-926R U2/U3: mounting the fan page never spawns the Fan Host and never
  // performs a handshake. Only a session that is already resident (the user
  // enabled control earlier in this app session) is adopted read-only.
  void adoptOnDemandEntryState();
});

/**
 * FAN-931 P1.4：KeepAlive 重现时先读一次最新状态（Host 权威），控制仍在时再恢复
 * 5 s 展示更新；失活即停（隐藏页不再每 5 s 取数）。此前/此后都不改"进页不拉宿主"规则。
 */
onActivated(() => {
  viewActive = true;
  viewVisible = !document.hidden;
  // FAN-932 §3.2：从 PowerView 改回本页时同步镜像开关（settingsRepository 无变更事件，
  // 且不允许常驻 watcher 或周期读取）。仅读设置，不触发任何启动。
  void refreshFanBootAutoStart().catch(() => {});
  if (FAN_FORCE_PREVIEW || !viewVisible) return;
  if (controlActive.value) startTelemetry();
});

onDeactivated(() => {
  viewActive = false;
  stopTelemetry();
});

/**
 * FAN-926R §2.1: entering the fan page never starts the Fan Host. A session that
 * is already resident (the user enabled control earlier in this app run) is
 * adopted read-only so page switches neither drop nor re-handshake it; otherwise
 * the page stays idle until the user turns the switch on. The persisted
 * `startupDesired.fanControl` preference is intentionally NOT read here - this
 * version does not execute boot auto-control, and page entry must not become a
 * second start path.
 */
async function adoptOnDemandEntryState(): Promise<void> {
  if (resolveFanEntryAction(fanHostLifecycle.phase) === 'adopt-resident-session') {
    supported.value = true;
    await adoptResidentControlState();
    return;
  }
  hostState.value = fanHostLifecycle.state;
}

onUnmounted(() => {
  viewActive = false;
  emitNativeEvent('gamepad.fan-node-edit', { active: false });
  document.removeEventListener('visibilitychange', onDocumentVisibilityChange);
  window.removeEventListener('ipc:power.suspending', onFanPowerSuspending);
  window.removeEventListener('fan:guard-state', onFanGuardState);
  window.removeEventListener('ipc:gamepad-back', onFanGamepadBack);
  window.removeEventListener('ipc:fan.exit.cleanup-failed', onFanExitCleanupFailed);
  stopTelemetry();
  // UX-1：离页时同时停掉"启用/关闭进行中"的只读计时器（它只读状态，不发请求）。
  stopBusyTicker();
  fanSuspendBoundary = false;
});
// YMCC Decky sidebar: observation-only additions; original business functions remain unchanged.
const fanMirrorBlocked = () => busy.value || connecting.value || closing.value ||
  controlCommitTask !== null || curveApplyPromise !== null || graphDragArmed.value !== null || draggingNode.value !== null;
const stopFanMirrorBlocker = registerFanMirrorUiBlocker(fanMirrorBlocked);
watch(() => [fanMirrorBlocked(), statusMessage.value, controlActive.value, hostState.value], notifyFanMirrorUiGate);
const stopFanMirrorDisplay = onFanMirrorDisplay((value) => {
  selectedPreset.value = value.preset;
  nodes.value = getFanPresetCurve(value.preset);
  controlActive.value = value.active;
  recovering.value = value.pending;
  controlReady.value = fanHostLifecycle.controlReady;
  supported.value = fanHostLifecycle.controlReady || supported.value;
  hostState.value = fanHostLifecycle.state;
  statusMessage.value = value.notice;
  if (value.active) { setFanNavigationDuty(expectedDuty.value); startTelemetry(); }
  else { setFanNavigationDuty(0); stopTelemetry(); }
});
onUnmounted(() => { stopFanMirrorBlocker(); stopFanMirrorDisplay(); });
</script>

<template>
  <section class="fan-page">
    <header class="fan-header"><div><div class="fan-kicker">Fan API</div><h1>风扇控制</h1></div></header>
    <div class="fan-status-banner" :class="{ supported }"><strong>{{ statusText }}</strong></div>
    <section class="fan-card controls-card" aria-label="风扇控制状态">
      <div class="control-line" data-gp-row="0"><button class="control-state control-button fan-toggle" :class="{ 'fan-toggle-active': controlActive }" data-gp-row="0" data-gp-col="0" type="button" :aria-pressed="controlActive" :disabled="closing || (!controlActive && (busy || connecting))" @click="toggleControl">{{ controlText }}</button><button class="control-state control-button" data-gp-row="0" data-gp-col="1" type="button" :disabled="!supported || !controlReady" @click="toggleMotion">风扇图标 {{ motionEnabled ? '转动' : '静止' }}</button><span class="control-state temperature-state">温度 {{ temperatureText }}</span></div>
      <div class="control-line" data-gp-row="1"><Dropdown class="control-dropdown" popup-class="fan-control-popup" :model-value="selectedPreset" :options="PRESET_OPTIONS" :disabled="!supported || !controlReady || busy" aria-label="转速预设" gp-row="1" gp-col="0" @change="choosePreset" /><button class="control-state control-button" data-gp-row="1" data-gp-col="1" type="button" :disabled="!supported || !controlReady || busy" @click="resetCurve">重置转速</button><span class="control-state">期望转速 {{ expectedDuty }}%</span></div>
      <Toggle v-model="fanBootAutoStart" label="开机/休眠唤醒启动风扇" description="根据适配机型自动启动风扇" color="accent" :gp-row="2" :gp-col="0" @update:model-value="onFanBootAutoStartToggle" />
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
          <button v-for="(node, index) in nodes" :key="`hit-${index}`" class="chart-node-hit" :class="{ active: graphSelectedNode === index, 'edit-armed': graphDragArmed === index }" :style="nodePointStyle(node)" type="button" :data-gp-row="3" :data-gp-col="index" data-gp-inline-edit data-gp-inline-edit-scope="fan-curve-node" data-gp-inline-edit-repeat-ms="55" :data-gp-inline-edit-active="graphDragArmed === index ? 'true' : undefined" :aria-pressed="graphDragArmed === index" :aria-label="`节点 ${index + 1} ${node.tempC}°C ${node.dutyPercent}%`" @focus="selectNode(index)" @click="onGraphNodeClick(index, $event)" @gp:spatial-edit="onGraphSpatialEdit(index, $event)" @pointerdown="onGraphNodePointerDown(index, $event)" @pointermove="onGraphNodePointerMove(index, $event)" @pointerup="onGraphNodePointerUp(index, $event)" @pointercancel="onGraphNodePointerUp(index, $event)"><span>节点 {{ index + 1 }}</span></button>
        </div>
      </div>
      <div class="node-grid" aria-label="节点配置"><article v-for="(node, index) in nodes" :key="index" class="node-card" :class="{ active: configSelectedNode === index }" @click="selectNode(index)" @focusin="selectNode(index)"><strong>节点 {{ index + 1 }}</strong><label>温度<Dropdown :model-value="node.tempC" :options="nodeDropdownOptions(index, 'tempC')" :disabled="!supported || busy" :aria-label="`节点 ${index + 1} 温度`" :gp-row="4" :gp-col="index" @change="updateNodeValue(index, 'tempC', $event)" /></label><label>转速<Dropdown :model-value="node.dutyPercent" :options="nodeDropdownOptions(index, 'dutyPercent')" :disabled="!supported || busy" :aria-label="`节点 ${index + 1} 转速`" :gp-row="5" :gp-col="index" @change="updateNodeValue(index, 'dutyPercent', $event)" /></label></article></div>
    </section>
  </section>
</template>

<style scoped>
.fan-page { height: 100%; width: 100%; min-width: 0; box-sizing: border-box; overflow: auto; padding: 14px 14px 28px; }.fan-header { display:flex;align-items:center;justify-content:space-between;margin-bottom:10px }.fan-kicker{color:var(--accent);font-size:10px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}h1{margin:3px 0 0;font-size:21px}.fan-status-banner,.fan-card{border-radius:var(--radius);background:var(--bg-panel)}.fan-status-banner{display:grid;gap:4px;padding:10px 12px;margin-bottom:9px;color:var(--text-dim);font-size:11px}.fan-status-banner strong{color:var(--text);font-size:13px;line-height:18px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.fan-status-banner.supported strong{color:var(--accent)}.fan-card{box-sizing:border-box;padding:11px 12px;margin-bottom:9px}.controls-card{display:grid;gap:8px}.control-line{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));grid-auto-rows:35px;gap:8px;width:100%}.control-state{min-width:0;min-height:35px;height:35px;box-sizing:border-box;border:1px solid #29384a;border-radius:var(--radius-ctrl);background:var(--bg-input);color:var(--text-dim);font:inherit;font-size:11px;font-weight:700;text-align:center}.control-state{display:flex;align-items:center;justify-content:center;padding:0 6px}.control-dropdown{min-width:0;height:35px}.control-dropdown :deep(.dd-trigger){height:35px;justify-content:center;text-align:center;position:relative}.control-dropdown :deep(.dd-value){justify-content:center;text-align:center;padding:0 18px 0 4px}.control-dropdown :deep(.dd-caret){position:absolute;right:10px}.control-dropdown :deep(.dd-menu){min-width:100%}.control-dropdown :deep(.dd-option){position:relative;justify-content:center;text-align:center}.control-dropdown :deep(.dd-opt-label){text-align:center}.control-dropdown :deep(.dd-check){position:absolute;right:10px}.control-button:not(:disabled){cursor:pointer;color:var(--text)}.control-button:not(:disabled):hover{border-color:var(--accent);background:#162434}.fan-toggle-active:not(:disabled){background:#1269a3;border-color:#2ea6ff;color:#fff;box-shadow:0 0 0 1px rgba(46,166,255,.32),0 0 12px rgba(46,166,255,.2)}.fan-toggle-active:not(:disabled):hover{background:#197dbb;border-color:#65c1ff}.fan-toggle-active.focused:not(:disabled),.fan-toggle-active:focus-visible:not(:disabled){box-shadow:inset 0 0 0 2px #fff,var(--focus-ring)}.chart-title{display:flex;justify-content:space-between;align-items:center;margin-bottom:5px;color:var(--text-dim);font-size:10px}.chart-title strong{color:var(--text);font-size:13px}.chart-wrap{position:relative;width:100%;aspect-ratio:650 / 370;overflow:visible}.fan-chart{display:block;width:100%;height:100%;overflow:visible}.chart-node-hit-layer{position:absolute;inset:0;width:100%;height:100%;pointer-events:none}.chart-node-hit{position:absolute;transform:translate(-50%,-50%);width:32px;height:32px;padding:0;border:2px solid transparent;border-radius:50%;background:transparent;color:transparent;font-size:0;line-height:1;pointer-events:auto;cursor:pointer}.chart-node-hit::after{content:'';position:absolute;inset:8px;border-radius:50%;background:var(--accent);box-shadow:0 0 0 2px var(--bg-panel)}.chart-node-hit:hover,.chart-node-hit:focus-visible,.chart-node-hit.focused{border-color:#fff;outline:none;box-shadow:0 0 0 2px color-mix(in srgb,var(--accent) 45%,transparent)}.chart-node-hit.active::after{box-shadow:0 0 0 2px #fff,0 0 10px color-mix(in srgb,var(--accent) 55%,transparent)}.grid-lines line{stroke:color-mix(in srgb,var(--text-dim) 18%,transparent);stroke-width:1}.curve{fill:none;stroke:var(--accent);stroke-width:3;stroke-linejoin:round;stroke-linecap:round}.node-halo{fill:color-mix(in srgb,var(--accent) 14%,transparent)}.node-point{fill:var(--accent);stroke:var(--bg-panel);stroke-width:2}.node-point.active{stroke:#fff;stroke-width:2.5}.chart-node text{fill:var(--text);font-size:10px;pointer-events:none}.chart-node text+text{fill:var(--text-dim);font-size:9px}.axis-label{fill:var(--text-dim);font-size:10px}.node-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));grid-auto-rows:auto;gap:7px;margin-top:8px;width:100%}.node-card{display:grid;gap:5px;padding:8px;border:1px solid #29384a;border-radius:var(--radius-ctrl);background:var(--bg-input);cursor:pointer;min-width:0}.node-card.active{border-color:color-mix(in srgb,var(--accent) 55%,transparent)}.node-card strong{color:var(--text);font-size:11px;text-align:center}.node-card label{display:grid;gap:3px;color:var(--text-dim);font-size:10px;text-align:center}.node-card :deep(.dd-trigger){min-height:27px;padding:4px 7px;font-size:10px}.node-card :deep(.dd-value){justify-content:center}.node-card :deep(.dd-caret){width:12px;height:12px}.node-card :deep(.dd-menu){--dd-popup-font-size:12px;--dd-popup-option-py:7px;--dd-popup-option-px:8px}
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
