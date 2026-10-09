<script setup lang="ts">
import { nextTick, computed, onActivated, onDeactivated, onBeforeUnmount, onMounted, ref, toRaw, watch } from 'vue';
import SegButton from '@/components/SegButton.vue';
import Slider from '@/components/Slider.vue';
import Toggle from '@/components/Toggle.vue';
import Dropdown from '@/components/Dropdown.vue';
import InlineIcon from '@/components/InlineIcon.vue';
import { compareAndSwapInputSettings, loadSettings, type InputSettingsSnapshot } from '@/bridge/settingsRepository';
import { normalizeGyroTelemetry, type GyroMotionConfigV1 } from '@/bridge/inputContracts';
import { mapGyroSample } from '@/bridge/gyroMotionMapperMock';
import { assembleCanonicalFrame } from '@/bridge/virtualReportAssembler';
import { invoke, on } from '@/bridge/ipc';
import { subscribeGameInputOverrideState, getGameInputOverrideState } from '@/bridge/gameInputOverride';
import { shell } from '@/bridge/api';
import { isUiVisible, onUiVisibilityChange } from '@/bridge/uiLifecycle';
import { createGyroPresentationGate } from '@/bridge/gyroPresentation';

const snapshot = ref<InputSettingsSnapshot | null>(null);
const busy = ref(false);
const gameLocked = ref(getGameInputOverrideState().locked);
const controlsLocked = computed(() => busy.value || gameLocked.value);
let stopOverride: (() => void) | null = null;
const status = ref('读取配置中');
const enabled = ref(false);
const provider = ref('unselected');
const calibrationId = ref('');
const motionInput = ref('local-space');
const steeringAxis = ref('roll');
// 用户裁决（2026-09-14）：抑制并入触发方式——'suppress' 为 UI 层值，落盘映射
// native mode='on'+非空触发键（native 语义 mode on = !pressed，按住键即抑制输出，
// HC MotionMode.On 同源）；'on'+空键 = 常开。UI 回读时反向派生。
const motionMode = ref<'off' | 'suppress' | 'on' | 'toggle'>('off');
const motionTrigger = ref('');
const outputMode = ref('virtual-stick');
const gyroMultiplier = ref(1);
// 用户裁决（2026-09-29）：灵敏度默认 1 → 1.5（高级区敏感度滑块下限 0.5/上限 2）。
const motionSensitivityX = ref(1.5);
const motionSensitivityY = ref(1.5);
// 6（用户 UI 设计 2026-09-12）：敏感度 + 曲线预设（新增 Dropdown）。
// 敏感度档位同时写 sensitivityX=Y；曲线预设写 49 节点 motionSensitivityArray。
// 批96（2026-09-12 用户裁决）：曲线预设「默认」档改为线性直通（1:1 恒等），
// FPS/赛车/自定义 各预设未微调时默认就是线性；高/低两档保留低段压缩。
// responseCurvePoints 仍 null 保持线性恒等。native main.cpp:5629-5638 已读数组。
const sensitivityPreset = ref<'high' | 'default' | 'low'>('default');
const curvePreset = ref<'high' | 'default' | 'low'>('default');
const accelerometerMultiplier = ref(1);
// 用户裁决（2026-09-29）：陀螺仪权重默认 1.2 → 1.8，上限 2 → 3。
const gyroWeight = ref(1.8);
const velocityMode = ref('default');
const velocityScale = ref(1);
const innerDeadzone = ref(1);
const outerDeadzone = ref(0);
const antiDeadzone = ref(20);
const outputShape = ref<'default' | 'circle' | 'cross' | 'square'>('circle');
const invertHorizontal = ref(false);
const invertVertical = ref(false);
type GyroPresetKind = 'fps' | 'racing' | 'custom' | 'steam';
type VirtualPadPersona = 'disabled' | 'dualshock4' | 'xbox360' | 'steamdeck' | 'dualsense' | 'elite' | 'dualsense-edge';
const preset = ref<GyroPresetKind>('fps');
// 用户裁决（2026-09-15）：开启/关闭与预设拆开。preset 只记当前选中的四档，
// 不再用 'off' 当关闭。enabled 才是总开关。
// 用户裁决（2026-09-16）：虚拟手柄人格只认控制器页当前选择，不再按预设记忆，
// 开启陀螺仪只给当前虚拟手柄打开 gyroEnabled。
const activePreset = ref<GyroPresetKind>('fps');
const virtualPersona = ref<VirtualPadPersona>('disabled');
const virtualPadEnabled = ref(false);
const virtualPadOn = computed(() => virtualPadEnabled.value && virtualPersona.value !== 'disabled');
// 用户裁决（2026-09-29）：启动陀螺仪改「2 个并排按钮」，去掉「陀螺仪已链接」小字。
// 左按钮 = 陀螺仪电源三态（已开启=蓝底 / 已关闭=红 / 未识别=灰且不可到达）；
// 未识别以 gyroConnected（真实传感器证据，1s 无 telemetry 回退）为准。
// 右按钮 = 虚拟手柄联动（开启虚拟手柄即启动陀螺仪，关闭一起关闭）。
type GyroPowerState = 'on' | 'off' | 'unknown';
// 用户裁决（2026-09-29）：默认开启（3 个虚拟手柄任开一个即默认联动）。
const virtualPadLink = ref(true);
// Temporary shortcut feedback is read-only: never feed it to the autosave watchers.
const shortcutGyroEnabled = ref<boolean | null>(null);
let offShortcutRuntime: (() => void) | null = null;
function applyShortcutRuntime(state: { active?: boolean; gyroEnabled?: boolean } | null | undefined): void {
  if (typeof state?.active !== 'boolean') return;
  shortcutGyroEnabled.value = state.active && typeof state.gyroEnabled === 'boolean' ? state.gyroEnabled : null;
}
const gyroPowerState = computed<GyroPowerState>(() => {
  if (shortcutGyroEnabled.value !== null) return shortcutGyroEnabled.value ? 'on' : 'off';
  if (enabled.value) return 'on';
  return gyroConnected.value ? 'off' : 'unknown';
});
const gyroPowerText = computed(() => (gyroPowerState.value === 'on'
  ? '陀螺仪已开启'
  : gyroPowerState.value === 'off' ? '陀螺仪已关闭' : '陀螺仪未识别'));
const gyroPowerClass = computed(() => gyroPowerState.value);
// 联动落点：仅当联动开启时把陀螺仪电源同步到虚拟手柄状态（顶部专属配置优先于本开关）。
function applyVirtualPadLink(): void {
  // Loading a page is not a user power action: preserve native boot gyro off.
  if (!loaded.value || gameLocked.value) return;
  if (!virtualPadLink.value) return;
  const next = virtualPadOn.value;
  if (enabled.value === next) return;
  enabled.value = next;
  if (next) outputMode.value = 'virtual-stick';
}
function applyVirtualPad(target: { persona?: unknown; buttonMappingEnabled?: unknown } | null | undefined): void {
  const persona = String(target?.persona || 'disabled');
  virtualPersona.value = persona === 'dualshock4' || persona === 'xbox360' || persona === 'steamdeck' || persona === 'dualsense' ||
    persona === 'elite' || persona === 'dualsense-edge'   // 批112
    ? (persona as VirtualPadPersona)
    : 'disabled';
  virtualPadEnabled.value = target?.buttonMappingEnabled === true && virtualPersona.value !== 'disabled';
}
const presetStore = ref<Record<string, Record<string, unknown> | null>>({
  fps: null, racing: null, custom: null, steam: null,
});
const outputStick = ref<'left' | 'right'>('right');
const outputAxis = ref<'xy' | 'x'>('xy');
const steeringMaxAngle = ref(30);
const steeringPower = ref(1);
const steeringDeadzone = ref(0.5);
// 用户裁决（2026-09-29）：变速触发键倍率默认 1.0 → 0.3（0.3 = 30% 变慢）。
const aimingSightsMultiplier = ref(0.3);
const aimingSightsTrigger = ref('');
const autoCalibrate = ref(true);
const advancedOpen = ref(false);
const loaded = ref(false);
const USBIP_RELEASES_URL = 'https://github.com/vadimgrn/usbip-win2/releases';
const usbipPrompt = ref<null | 'missing' | 'failed'>(null);
const usbipPromptDismissed = ref(0);
const usbipPromptText = computed(() => usbipPrompt.value === 'missing'
  ? '启用 Steam Deck 需要 USBip 后端，但本地未找到 USBip 安装器。是否前往官网下载？下载后请放入 C:\\SOFT\\YeMan\\PowerControl\\redist 目录再重试。'
  : 'USBip 后端自动安装没有成功。是否前往官网下载最新版手动安装？');
function dismissUsbipPrompt(): void {
  usbipPromptDismissed.value = usbipPrompt.value === 'missing' ? 1 : 2;
  usbipPrompt.value = null;
}
async function openUsbipSite(): Promise<void> {
  usbipPrompt.value = null;
  try { await shell.open(USBIP_RELEASES_URL); } catch { /* 打开失败不打断页面 */ }
}
const sampleSequence = ref<number | null>(null);
const sampleTimestamp = ref<string | null>(null);
const gyroAxes = ref({ x: 0, y: 0, z: 0 });
// Y-2 v3（2026-09-12，用户第三次裁决）：陀螺仪点 = 2D 坐标系点位移（与摇杆点
// 同域），不做角度/3D/四元数——直接显示陀螺仪在摇杆 2D 空间的贡献。
const gyroDot2D = ref<{ x: number; y: number }>({ x: 0, y: 0 });
const hostOutputAxes = ref({ x: 0, y: 0 });
const hostFrameState = ref<'not-running' | 'not-admitted-or-failed' | 'publication-suppressed' | 'frame-accepted' | 'unavailable'>('unavailable');
const hostLifecycle = ref('none');
const hostActive = ref(false);
const outputAxes = ref({ x: 0, y: 0 });
// 传感器连接状态：收到 gyro.telemetry 即为已连接；连续 ~1s 无事件回退为未连接。
const gyroConnected = ref(false);
let gyroConnectedTimer: ReturnType<typeof window.setTimeout> | null = null;
let lastSensorEvidenceAt = Number.NEGATIVE_INFINITY;
let gyroConnectedDeadline = Number.NEGATIVE_INFINITY;
function expireGyroConnection(): void {
  gyroConnectedTimer = null;
  const remaining = gyroConnectedDeadline - performance.now();
  if (remaining > 0) {
    gyroConnectedTimer = window.setTimeout(expireGyroConnection, remaining);
  } else {
    gyroConnected.value = false;
  }
}
function noteGyroConnected(receivedAt: number): void {
  gyroConnectedDeadline = receivedAt + 1000;
  if (gyroConnectedDeadline <= performance.now()) {
    if (gyroConnectedTimer) window.clearTimeout(gyroConnectedTimer);
    gyroConnectedTimer = null;
    gyroConnected.value = false;
    return;
  }
  gyroConnected.value = true;
  // Extend the deadline, not one new timeout per telemetry frame. The live
  // timer rechecks that deadline, preserving the original one-second expiry.
  if (gyroConnectedTimer === null) {
    gyroConnectedTimer = window.setTimeout(expireGyroConnection,
      Math.max(1, gyroConnectedDeadline - performance.now()));
  }
}

// HC 静态校准：native IPC gyro.calibrate 触发 capture tick 内非阻塞状态机，
// 完成回发 gyro.calibrate.result（confidence/weight/offset，HC
// SensorsManager.Calibrate 语义：静止 5s 采样、置信度 >=1 才可用于锁定）。
// 用户指令 UX 合同（2026-09-12，C1-reinstate）：invoke 成功后启动 10s 计时器，
// 到点未收到 gyro.calibrate.result → calibrating=false + "校准超时，请重试"；
// 收到 result 即清计时器。纯前端 UX 兜底，不改 native、不带 HC 语义。
const calibrating = ref(false);
const calibResult = ref<{ confidence: number; weight: number; offsetX: number; offsetY: number; offsetZ: number } | null>(null);
const calibError = ref('');
const calibUiStatus = ref<'uncalibrated' | 'waiting-still' | 'calibrating' | 'already-calibrated'>('uncalibrated');
const calibStatusText = computed(() => {
  if (calibUiStatus.value === 'already-calibrated') return '已经校准';
  if (calibUiStatus.value === 'calibrating') return '校准中';
  if (calibUiStatus.value === 'waiting-still') return '等待放稳';
  return '未校准';
});
function applyCalibStatus(status: string | undefined): void {
  if (status === 'already-calibrated' || status === 'calibrating' || status === 'waiting-still' || status === 'uncalibrated')
    calibUiStatus.value = status;
  if (status === 'calibrating') calibrating.value = true;
  else if (status === 'already-calibrated' || status === 'waiting-still' || status === 'uncalibrated')
    calibrating.value = false;
}
let offCalibResult: (() => void) | null = null;
let offCalibStatus: (() => void) | null = null;
let offGamepadState: (() => void) | null = null;
let calibTimeout: ReturnType<typeof window.setTimeout> | null = null;
function clearCalibTimeout(): void {
  if (calibTimeout) { window.clearTimeout(calibTimeout); calibTimeout = null; }
}
async function startCalibration(): Promise<void> {
  if (gameLocked.value) return;
  calibError.value = '';
  calibResult.value = null;
  try {
    const resp = await invoke<{ ok: boolean; reason?: string }>('gyro.calibrate', {});
    // Y-1 诊断埋点（LegB 取证）：native ack/拒绝、UI 超时、result 到达三处
    // 都写 console + diagnostics.frontendError（进 native 导出日志），下次
    // 录制一次即可定位 result 事件断点位置。
    console.log('[gyro.calibrate] native-ack', resp, new Date().toISOString());
    void invoke('diagnostics.frontendError', { where: 'gyro-motion', message: resp?.ok ? 'calibration-request-ack-ok' : 'calibration-request-ack-nack:' + (resp?.reason ?? '') }).catch(() => {});
    if (!resp?.ok) { calibError.value = resp?.reason ?? '校准请求被拒绝'; return; }
    calibrating.value = true;
    // 用户指令 UX 合同（2026-09-12）：10s 超时兜底，收到 result 即清。
    clearCalibTimeout();
    calibTimeout = window.setTimeout(() => {
      calibrating.value = false;
      calibTimeout = null;
      calibError.value = '校准超时，请重试';
      console.warn('[gyro.calibrate] ui-timeout', new Date().toISOString());
      void invoke('diagnostics.frontendError', { where: 'gyro-motion', message: 'calibration-timeout-observed' }).catch(() => {});
    }, 10_000);
  } catch (e) {
    calibError.value = String(e);
    console.error('[gyro.calibrate] invoke-failed', e, new Date().toISOString());
    void invoke('diagnostics.frontendError', { where: 'gyro-motion', message: 'calibration-invoke-failed:' + String(e) }).catch(() => {});
  }
}

const stickOptions = [
  { value: 'left', label: '左摇杆' },
  { value: 'right', label: '右摇杆' },
];
const presetOptions = [
  { value: 'fps', label: 'FPS射击' },
  { value: 'racing', label: '赛车' },
  { value: 'custom', label: '自定义' },
  { value: 'steam', label: 'Steam' },
];
function asGyroPreset(value: unknown): GyroPresetKind | null {
  return value === 'fps' || value === 'racing' || value === 'custom' || value === 'steam' ? value : null;
}
const TRIGGER_VALUES = ['RT', 'LT', 'RB', 'LB'] as const;
const triggerOptions = [
  { value: 'RT', label: 'RT' },
  { value: 'LT', label: 'LT' },
  { value: 'RB', label: 'RB' },
  { value: 'LB', label: 'LB' },
];
// 批97（2026-09-12 用户裁决）：瞄准触发键改下拉菜单（含「无」），不再自由文本。
const aimingTriggerOptions = [
  { value: '', label: '无' },
  ...triggerOptions,
];
const motionModeOptions = [
  { value: 'off', label: '按住触发' },
  { value: 'suppress', label: '按住抑制' },
  { value: 'on', label: '常开' },
  { value: 'toggle', label: '切换' },
];
// 6：敏感度/曲线预设档位选项与数值映射（同 motionMode Dropdown 组件款）。
const SENSITIVITY_VALUES: Record<'high' | 'default' | 'low', number> = { high: 2, default: 1, low: 0.5 };
// 批96：曲线预设高/低档保留低段压缩档值；「默认」档由 buildCurveNodes 走线性
// 直通（1:1 恒等），此处占位 1.0 仅用于历史配置反推容错。
const CURVE_LOW_BAND_VALUES: Record<'high' | 'default' | 'low', number> = { high: 0.75, default: 1.0, low: 0.25 };
const sensitivityPresetOptions = [
  { value: 'high', label: '最敏感 ×2.0' },
  { value: 'default', label: '默认 ×1.0' },
  { value: 'low', label: '最不敏感 ×0.5' },
];
const curvePresetOptions = [
  { value: 'high', label: '高灵敏曲线 0.75' },
  { value: 'default', label: '默认' },
  { value: 'low', label: '低灵敏曲线 0.25' },
];
function snapSensitivityPreset(value: number): 'high' | 'default' | 'low' {
  if (Math.abs(value - 2) < 1e-6) return 'high';
  if (Math.abs(value - 1) < 1e-6) return 'default';
  if (Math.abs(value - 0.5) < 1e-6) return 'low';
  return 'default';
}
function snapCurvePreset(lowBand: number): 'high' | 'default' | 'low' {
  if (Math.abs(lowBand - 0.75) < 1e-6) return 'high';
  if (Math.abs(lowBand - 0.25) < 1e-6) return 'low';
  if (Math.abs(lowBand - 1.0) < 1e-6) return 'default';   // 线性直通（新默认）
  if (Math.abs(lowBand - 0.5) < 1e-6) return 'default';   // 老「默认 0.5」配置容错归入默认档
  return 'default';
}
// 49 节点：key = i/48（i=0..48）。「默认」档节点 y=x 线性恒等；
// 高/低档 0–50% 段用档值、50–100% 恒 0.5（低段压缩，批96 保留）。
function buildCurveNodes(preset: 'high' | 'default' | 'low'): Array<[number, number]> {
  if (preset === 'default') {
    return Array.from({ length: 49 }, (_, i): [number, number] => [i / 48, i / 48]);
  }
  const lowBand = CURVE_LOW_BAND_VALUES[preset];
  return Array.from({ length: 49 }, (_, i): [number, number] => {
    const key = i / 48;
    return [key, key <= 0.5 ? lowBand : 0.5];
  });
}
function onSensitivityPreset(value: string | number): void {
  if (gameLocked.value) return;
  if (value === 'high' || value === 'default' || value === 'low') {
    const level: 'high' | 'default' | 'low' = value;
    sensitivityPreset.value = level;
    // 同时写 sensitivityX = sensitivityY（用户 UI 设计）。
    motionSensitivityX.value = SENSITIVITY_VALUES[level];
    motionSensitivityY.value = SENSITIVITY_VALUES[level];
  }
  markCustom();
}
function onSensitivityLevel(value: number): void {
  if (gameLocked.value) return;
  // 用户裁决（2026-09-29）：敏感度改滑块——与旧下拉同口径，一次同时写 X=Y，
  // 水平/垂直两个细分滑块保留可各自微调。
  motionSensitivityX.value = value;
  motionSensitivityY.value = value;
  markCustom();
}
function onCurvePreset(value: string | number): void {
  if (gameLocked.value) return;
  if (value === 'high' || value === 'default' || value === 'low') curvePreset.value = value;
  markCustom();
}
const motionInputOptions = [
  { value: 'local-space', label: '本地空间', sub: '常用瞄准' },
  { value: 'joystick-steering', label: '方向盘倾斜', sub: '使用加速度' },
  { value: 'player-space', label: '玩家空间', sub: '使用 GamepadMotion 玩家空间' },
  { value: 'world-space', label: '世界空间', sub: '使用 GamepadMotion 世界空间' },
];
const steeringAxisOptions = [
  { value: 'roll', label: '默认', sub: '本地空间 (Z,X)' },
  { value: 'yaw', label: '横轴', sub: '本地空间 (-Y,X)' },
  { value: 'auto', label: '竖轴', sub: '按传感器族解析' },
];
const velocityModeOptions = [
  { value: 'default', label: '线性' },
  { value: 'velocity', label: '加速度' },
];
const outputShapeOptions = [
  { value: 'default', label: '默认' },
  { value: 'circle', label: '圆形' },
  { value: 'cross', label: '十字' },
  { value: 'square', label: '方形' },
];

function number(value: unknown, fallback: number): number {
  const n = Number(value);
  return Number.isFinite(n) ? n : fallback;
}
// 用户裁决（2026-09-14）：预设独立记忆。capturePresetParams 把当前页面全部
// 可调参数抽成一份（UI 形态：motionMode 含 'suppress'），存进 presetStore 并
// 落盘 gyroMotion.presets[预设]；applyPresetParams 反向写回 refs（含敏感度/
// 曲线档位反推）。enabled/provider/calibrationId/outputMode 为全局项，不入预设。
function capturePresetParams(): Record<string, unknown> {
  return {
    motionMode: motionMode.value,
    motionTrigger: motionTrigger.value,
    outputStick: outputStick.value,
    outputAxis: outputAxis.value,
    motionInput: motionInput.value,
    steeringAxis: steeringAxis.value,
    gyroMultiplier: gyroMultiplier.value,
    motionSensitivityX: motionSensitivityX.value,
    motionSensitivityY: motionSensitivityY.value,
    accelerometerMultiplier: accelerometerMultiplier.value,
    velocityMode: velocityMode.value,
    velocityScale: velocityScale.value,
    innerDeadzone: innerDeadzone.value,
    outerDeadzone: outerDeadzone.value,
    antiDeadzone: antiDeadzone.value,
    gyroWeight: gyroWeight.value,
    outputShape: outputShape.value,
    invertHorizontal: invertHorizontal.value,
    invertVertical: invertVertical.value,
    steeringMaxAngle: steeringMaxAngle.value,
    steeringPower: steeringPower.value,
    steeringDeadzone: steeringDeadzone.value,
    aimingSightsMultiplier: aimingSightsMultiplier.value,
    aimingSightsTrigger: aimingSightsTrigger.value,
    motionSensitivityArray: buildCurveNodes(curvePreset.value),
    responseCurvePoints: null,
    autoCalibrate: autoCalibrate.value,
  };
}
function applyPresetParams(p: Record<string, unknown> | null | undefined): void {
  if (!p) return;
  if (p.motionMode === 'off' || p.motionMode === 'suppress' || p.motionMode === 'on' || p.motionMode === 'toggle')
    motionMode.value = p.motionMode;
  if (typeof p.motionTrigger === 'string') motionTrigger.value = p.motionTrigger;
  if (p.outputStick === 'left' || p.outputStick === 'right') outputStick.value = p.outputStick;
  if (p.outputAxis === 'x' || p.outputAxis === 'xy') outputAxis.value = p.outputAxis;
  if (p.motionInput === 'local-space' || p.motionInput === 'joystick-steering' || p.motionInput === 'player-space' || p.motionInput === 'world-space')
    motionInput.value = p.motionInput;
  if (p.steeringAxis === 'roll' || p.steeringAxis === 'yaw' || p.steeringAxis === 'auto') steeringAxis.value = p.steeringAxis;
  if (typeof p.gyroMultiplier === 'number') gyroMultiplier.value = p.gyroMultiplier;
  if (typeof p.motionSensitivityX === 'number') motionSensitivityX.value = p.motionSensitivityX;
  if (typeof p.motionSensitivityY === 'number') motionSensitivityY.value = p.motionSensitivityY;
  if (typeof p.accelerometerMultiplier === 'number') accelerometerMultiplier.value = p.accelerometerMultiplier;
  if (p.velocityMode === 'default' || p.velocityMode === 'velocity') velocityMode.value = p.velocityMode;
  if (typeof p.velocityScale === 'number') velocityScale.value = p.velocityScale;
  innerDeadzone.value = typeof p.innerDeadzone === 'number' ? p.innerDeadzone : 1;
  if (typeof p.outerDeadzone === 'number') outerDeadzone.value = p.outerDeadzone;
  antiDeadzone.value = typeof p.antiDeadzone === 'number' ? p.antiDeadzone : 20;
  if (typeof p.gyroWeight === 'number') gyroWeight.value = p.gyroWeight;
  if (p.outputShape === 'default' || p.outputShape === 'circle' || p.outputShape === 'cross' || p.outputShape === 'square')
    outputShape.value = p.outputShape;
  if (typeof p.invertHorizontal === 'boolean') invertHorizontal.value = p.invertHorizontal;
  if (typeof p.invertVertical === 'boolean') invertVertical.value = p.invertVertical;
  if (typeof p.steeringMaxAngle === 'number') steeringMaxAngle.value = p.steeringMaxAngle;
  if (typeof p.steeringPower === 'number') steeringPower.value = p.steeringPower;
  if (typeof p.steeringDeadzone === 'number') steeringDeadzone.value = p.steeringDeadzone;
  if (typeof p.aimingSightsMultiplier === 'number') aimingSightsMultiplier.value = p.aimingSightsMultiplier;
  if (typeof p.aimingSightsTrigger === 'string') aimingSightsTrigger.value = p.aimingSightsTrigger;
  autoCalibrate.value = p.autoCalibrate !== false;
  // 敏感度/曲线档位反推（与 load 同口径）
  if (typeof p.motionSensitivityX === 'number' && typeof p.motionSensitivityY === 'number' &&
      p.motionSensitivityX === p.motionSensitivityY)
    sensitivityPreset.value = snapSensitivityPreset(p.motionSensitivityX);
  const storedCurve = Array.isArray(p.motionSensitivityArray) ? p.motionSensitivityArray : null;
  const firstNode = Array.isArray(storedCurve) && storedCurve.length > 0 ? storedCurve[0] : null;
  if (Array.isArray(firstNode)) curvePreset.value = snapCurvePreset(Number(firstNode[1]));
  // 2026-10-04 用户裁决：按住触发/按住抑制/切换 必须有触发键（默认 LT 防呆）。
  ensureMotionTrigger();
}
function applyPreset(raw: string | number): void {
  if (gameLocked.value) return;
  const next = asGyroPreset(raw);
  if (!next) return;

  // 用户裁决（2026-09-15）：预设切换不再顺带开启/关闭。先保存当前预设，
  // 再加载目标预设；有独立记忆用记忆，没有则用该预设默认。
  if (next !== preset.value) {
    presetStore.value[activePreset.value] = capturePresetParams();
  }
  preset.value = next;
  activePreset.value = next;
  // 选任何预设时，若陀螺仪未开则自动开启（总开关仍可单独关掉）。
  enabled.value = true;
  outputMode.value = 'virtual-stick';
  const stored = presetStore.value[next];
  if (stored) {
    applyPresetParams(stored);
    queueSave(true);
    return;
  }
  autoCalibrate.value = true;
  innerDeadzone.value = 1;
  antiDeadzone.value = 20;
  if (next === 'steam') {
    // 用户裁决（2026-09-29）：Steam 预设输入方式默认「本地」(local-space)。
    motionMode.value = 'on';
    motionTrigger.value = '';
    motionInput.value = 'local-space';
  } else if (next === 'fps') {
    // 用户裁决（2026-09-29）：FPS 射击预设触发模式默认「常开」。
    motionMode.value = 'on';
    motionTrigger.value = '';
    outputStick.value = 'right';
    outputAxis.value = 'xy';
    motionInput.value = 'local-space';
    // 用户裁决（2026-09-29）：FPS 射击预设的变速触发键默认 LT。
    aimingSightsTrigger.value = 'LT';
  } else if (next === 'racing') {
    motionMode.value = 'on';
    motionTrigger.value = '';
    outputStick.value = 'left';
    outputAxis.value = 'x';
    motionInput.value = 'joystick-steering';
  } else if (next === 'custom') {
    // 用户裁决（2026-09-29）：自定义预设触发模式为「常开」。
    motionMode.value = 'on';
    motionTrigger.value = '';
  }
  queueSave(true);
}
function onGyroEnabled(value: boolean): void {
  if (gameLocked.value) return;
  enabled.value = value;
  if (value) outputMode.value = 'virtual-stick';
  queueSave(true);
}
// 用户裁决（2026-09-29）：左按钮=陀螺仪电源，可点击切换开/关；「未识别」不可到达。
function toggleGyroPower(): void {
  if (gameLocked.value) return;
  if (gyroPowerState.value === 'unknown') return;
  onGyroEnabled(gyroPowerState.value !== 'on');
}
// 用户裁决（2026-09-29）：右按钮=虚拟手柄联动，可点击切换（开启=联动，关闭=不联动）。
function toggleVirtualPadLink(): void {
  if (gameLocked.value) return;
  virtualPadLink.value = !virtualPadLink.value;
  applyVirtualPadLink();
  queueSave(true);
}
function onAutoCalibrate(value: boolean): void {
  if (gameLocked.value) return;
  autoCalibrate.value = value;
  queueSave(true);
}
// 用户裁决（2026-09-29）：变速触发键倍率说明文字——统一「百分比(注)」格式，
// 去掉小数（1.0=100% 不变速 → 100%(不变速)）；步进仍 0.1，仅改显示文案。
const aimingSightsMultiplierText = computed(() => {
  const v = aimingSightsMultiplier.value;
  const pct = Math.round(v * 100);
  const note = v === 1 ? '不变速' : (v < 1 ? '变慢' : '加速');
  return `${pct}%(${note})`;
});
function markCustom(): void {
  // 用户裁决（2026-09-14）：预设独立记忆——调参留在当前预设，不再跳回自定义。
  // 保存链会把当前参数 capture 进 presetStore[activePreset] 并写顶层热生效。
  queueSave();
}
// 2026-10-04 用户裁决：触发方式为「按住触发/按住抑制/切换」时必须有触发键，
// 默认 LT 兜底（禁止留空防呆）；「常开」无需触发键。
function ensureMotionTrigger(): void {
  if (motionMode.value !== 'on' && motionTrigger.value.trim() === '') {
    motionTrigger.value = 'LT';
  }
}
function onMotionModeChange(): void {
  ensureMotionTrigger();
  markCustom();
}
// 抖动根因 A（2026-09-14）：telemetry 12.5Hz 直写 6 个响应式变量造成页面抖动。
// 渲染门：最新一帧暂存，requestAnimationFrame 每帧最多应用一次；
// 拖拽滑块期间冻结页面级 XYZ 芯片刷新（气泡 .dot 照常刷新）。
const gyroPresentation = createGyroPresentationGate({ present: applyTelemetry });
let stopGyroVisibility: (() => void) | null = null;
let previewPointerDown = false;
function onGyroPreviewPointerDown(e: PointerEvent): void {
  const target = e.target as HTMLElement | null;
  if (target && (target.closest('input[type="range"]') || target.closest('.slider, .ym-slider, [data-slider]'))) {
    previewPointerDown = true;
  }
}
function onGyroPreviewPointerUp(): void {
  previewPointerDown = false;
}
function telemetry(event: Event): void {
  const detail = normalizeGyroTelemetry((event as CustomEvent<unknown>).detail);
  if (!detail) return;
  const receivedAt = performance.now();
  if (hasGyroSensorEvidence(detail)) lastSensorEvidenceAt = receivedAt;
  gyroPresentation.push(detail, receivedAt);
}
function hasGyroSensorEvidence(detail: NonNullable<ReturnType<typeof normalizeGyroTelemetry>>): boolean {
  const g = detail.gyro;
  const a = detail.accel ?? { x: 0, y: 0, z: 0 };
  return detail.sensorPresent === true || detail.gamepadMotionPlane !== undefined
    || Math.abs(g.x) + Math.abs(g.y) + Math.abs(g.z) > 0.5
    || Math.abs(a.x) + Math.abs(a.y) + Math.abs(a.z) > 0.5;
}
function applyTelemetry(detail: NonNullable<ReturnType<typeof normalizeGyroTelemetry>>, _receivedAt: number): void {
  // Connection badge must be real sensor evidence, not "any frame arrived": the
  // native layer posts a gyro.telemetry frame even when no default gyrometer is
  // present (all-zero sample). A Windows sensor binding receipt is the primary
  // signal; a proven GamepadMotion plane and non-zero raw samples cover
  // external/SMBios IMU lanes that bypass the WinRT default sensor.
  // The last proven receipt can have arrived while cached/hidden. Its original
  // time, not the time of this replay, controls connection expiry.
  if (Number.isFinite(lastSensorEvidenceAt)) noteGyroConnected(lastSensorEvidenceAt);
  if (sampleSequence.value === detail.sequence && sampleTimestamp.value === detail.timestampUtc) return;
  if (!previewPointerDown) gyroAxes.value = detail.gyro; // 拖拽中冻结页面级 XYZ 刷新
  // Y-2 v3（2026-09-12，用户第三次裁决）：陀螺仪点数据 = telemetry 的
  // motionContribution（MotionManager 陀螺仪贡献，摇杆 2D 域、归一化 [-1,1]）。
  // 无贡献或字段缺失时归零（同 HC：motion 未映射/未触发即无贡献）。
  const contrib = detail.motionContribution;
  if (contrib && [contrib.x, contrib.y].every((v) => Number.isFinite(v))) {
    gyroDot2D.value = {
      x: Math.max(-1, Math.min(1, contrib.x)),
      y: Math.max(-1, Math.min(1, contrib.y)),
    };
  } else {
    gyroDot2D.value = { x: 0, y: 0 };
  }
  const actualTarget = detail.targetStick ?? detail.hostFrame?.targetStick ?? outputStick.value;
  const actualHostStick = actualTarget === 'left'
    ? (detail.leftStick ?? detail.hostFrame?.leftStick)
    : (detail.rightStick ?? detail.hostFrame?.rightStick);
  hostOutputAxes.value = actualHostStick ?? { x: 0, y: 0 };
  hostFrameState.value = detail.hostFrame?.hostSubmission ?? 'unavailable';
  hostLifecycle.value = detail.hostFrame?.lifecycle ?? 'none';
  hostActive.value = detail.hostFrame?.hostActive === true;
  // A standalone real-stick lane has a canonical Host-local receipt. Show it
  // directly so the operator can see the actual target axis before looking at
  // the fixture preview below; this is still not HID/Steam/game readback.
  const actualLane = detail.motionAdmission === 'hc-motion-admitted' &&
    detail.hostFrame?.firstFrame === true && detail.hostFrame.hostActive === true && !!actualHostStick;
  const persistedMotion = snapshot.value?.gyroMotion as GyroMotionConfigV1 | undefined;
  if (actualLane) {
    outputAxes.value = actualHostStick;
  } else {
    // Preview must consume the real durable gyro section, not a page-local
    // CLOSED/windows-imu/ui-preview substitute. A saved-pending or unbound
    // config is intentionally still unclosed here, so the fixture stays zero.
    let stick = { x: 0, y: 0 };
    const gateOpen = persistedMotion?.enabled === true &&
      persistedMotion.outputStick === 'right' &&
      persistedMotion.motionMode !== 'toggle' && !persistedMotion.motionTrigger &&
      !(!!persistedMotion.aimingSightsTrigger && detail.aimingSightsTriggerResolved === false && (persistedMotion.aimingSightsMultiplier ?? 1) !== 1);
    if (gateOpen) {
      const preview = mapGyroSample(
        { runId: 'ui-preview', epoch: 0, powerGeneration: 0, targetId: 'preview', configRevision: persistedMotion.revision },
        persistedMotion,
        { gamepadMotionPlane: detail.gamepadMotionPlane, sequence: detail.sequence, timestamp: Date.parse(detail.timestampUtc) },
      );
      if (preview.ok) {
        const assembled = assembleCanonicalFrame({
          runId: 'ui-preview', epoch: 0, powerGeneration: 0, targetId: 'preview', persona: 'dualshock4', configRevision: persistedMotion.revision,
          inputSequence: detail.sequence, timestamp: 0, buttons: 0, axes: {}, triggers: {}, rightStick: { x: 0, y: 0 },
        }, preview.frame, {
          gyroWeight: persistedMotion.gyroWeight, innerDeadzone: persistedMotion.innerDeadzone, outerDeadzone: persistedMotion.outerDeadzone,
          antiDeadzone: persistedMotion.antiDeadzone, outputShape: persistedMotion.outputShape,
          responseCurvePoints: persistedMotion.responseCurvePoints ?? null,
        });
        if (assembled.ok) stick = assembled.frame.rightStick;
      }
    }
    outputAxes.value = persistedMotion?.outputAxis === 'x' ? { x: stick.x, y: 0 } : stick;
  }
  sampleSequence.value = detail.sequence;
  sampleTimestamp.value = detail.timestampUtc;
}
const hostLiveDot = computed(() => ({
  left: `${50 + hostOutputAxes.value.x * 38}%`,
  top: `${50 - hostOutputAxes.value.y * 38}%`,
}));
const previewLiveDot = computed(() => ({
  // Y-2 v3（用户第三次裁决）：陀螺仪点 = 摇杆同域 2D 位移（motionContribution），
  // 不做角度映射。±1 → ±38% 舞台半径，与摇杆点公式一致。
  left: `${50 + gyroDot2D.value.x * 38}%`,
  top: `${50 - gyroDot2D.value.y * 38}%`,
}));
let loadGeneration = 0;
// Keep preset tuning, but never use this session's power/preset as next boot intent.
// Native initializes power and selected preset from startupDesired once per process.
async function load(): Promise<void> {
  const generation = ++loadGeneration;
  loaded.value = false;
  const settings = await loadSettings();
  if (generation !== loadGeneration) return;
  const motion = settings.input.gyroMotion || {};
  snapshot.value = settings.input;
  gameLocked.value = getGameInputOverrideState().locked || !!settings.input.gameOverride;
  enabled.value = motion.enabled === true;
  provider.value = String(motion.provider || 'unselected');
  calibrationId.value = String(motion.calibrationId || '');
  motionInput.value = String(motion.motionInput || 'local-space');
  // 批99：转向轴默认回 roll（HC 默认）；Auto 为 YMCC 原创轴位（yaw 映射），
  // 用户实机出现「只剩上下轴」后裁决：无 UI 数据支撑前一律默认 roll。
  steeringAxis.value = String(motion.steeringAxis || 'roll');
  const storedTrigger = String(motion.motionTrigger || '').toUpperCase();
  motionTrigger.value = (TRIGGER_VALUES as readonly string[]).includes(storedTrigger) ? storedTrigger : '';
  const persistedMode = String(motion.motionMode || 'off');
  // 用户裁决（2026-09-14）：按住抑制并入触发方式——回读时 mode='on'+非空
  // 触发键 派生为 suppress（native mode on = !pressed 即按住抑制，
  // HC MotionMode.On 同源）；mode='on'+空键 = 常开。老配置无迁移。
  let uiMode: 'off' | 'on' | 'toggle' = 'off';
  if (persistedMode === 'off' || persistedMode === 'on' || persistedMode === 'toggle')
    uiMode = persistedMode;
  motionMode.value = uiMode === 'on' && motionTrigger.value !== '' ? 'suppress' : uiMode;
  outputMode.value = String(motion.outputMode || 'virtual-stick');
  gyroMultiplier.value = number(motion.gyroMultiplier, 1);
  motionSensitivityX.value = number(motion.motionSensitivityX, 1.5);
  motionSensitivityY.value = number(motion.motionSensitivityY, 1.5);
  // 6：反推预设档位——X==Y 且命中三档才锁定敏感度档；曲线按数组首节点
  // （低频段）档值反推；不命中回退 default（不破坏高级区 slider 微调）。
  sensitivityPreset.value = motionSensitivityX.value === motionSensitivityY.value
    ? snapSensitivityPreset(motionSensitivityX.value)
    : 'default';
  const storedCurve = Array.isArray(motion.motionSensitivityArray) ? motion.motionSensitivityArray : null;
  const firstNode = Array.isArray(storedCurve) && storedCurve.length > 0 ? storedCurve[0] : null;
  curvePreset.value = Array.isArray(firstNode) ? snapCurvePreset(Number(firstNode[1])) : 'default';
  accelerometerMultiplier.value = number(motion.accelerometerMultiplier, 1);
  gyroWeight.value = number(motion.gyroWeight, 1.8);
  velocityMode.value = String(motion.velocityMode || 'default');
  velocityScale.value = number(motion.velocityScale, 1);
  innerDeadzone.value = number(motion.innerDeadzone ?? motion.deadzone, 1);
  outerDeadzone.value = number(motion.outerDeadzone, 0);
  antiDeadzone.value = number(motion.antiDeadzone, 20);
  outputShape.value = motion.outputShape === 'circle' || motion.outputShape === 'cross' || motion.outputShape === 'square' ? motion.outputShape : 'circle';
  invertHorizontal.value = motion.invertHorizontal === true;
  invertVertical.value = motion.invertVertical === true;
  autoCalibrate.value = motion.autoCalibrate !== false;
  // 用户裁决（2026-09-29）：虚拟手柄联动默认 true（旧配置缺省即视为开启）。
  virtualPadLink.value = motion.virtualPadLink !== false;
  // 用户裁决（2026-09-15）：关闭不再写成 preset='off'。老配置 preset=off
  // 时回退到 activePreset / fps，enabled 单独决定开关。
  // 用户裁决（2026-09-30）：无数据时默认预设为 FPS（fps 是缺省档）。
  const presetRaw = asGyroPreset(motion.preset);
  const activeRaw = asGyroPreset(motion.activePreset);
  preset.value = presetRaw ?? activeRaw ?? 'fps';
  activePreset.value = activeRaw ?? preset.value;
  outputStick.value = motion.outputStick === 'left' ? 'left' : 'right';
  outputAxis.value = motion.outputAxis === 'x' ? 'x' : 'xy';
  steeringMaxAngle.value = number(motion.steeringMaxAngle, 30);
  steeringPower.value = number(motion.steeringPower, 1);
  steeringDeadzone.value = number(motion.steeringDeadzone, 0.5);
  aimingSightsMultiplier.value = number(motion.aimingSightsMultiplier, 0.3);
  // 批97：瞄准触发键改下拉后，老配置自由文本/非法值归「无」。
  aimingSightsTrigger.value = (TRIGGER_VALUES as readonly string[]).includes(
    String(motion.aimingSightsTrigger || '').toUpperCase()) ? String(motion.aimingSightsTrigger).toUpperCase() : '';
  // 用户裁决（2026-09-14）：预设独立记忆——presets 载入放在全部顶层 refs
  // 赋值之后：磁盘有独立记忆则采用（覆盖顶层），缺失则用当前顶层初始化当前
  // 预设（老配置零迁移：顶层参数归当前预设，其余预设待首次切换时建默认）。
  const rawPresets = (motion.presets && typeof motion.presets === 'object' && !Array.isArray(motion.presets))
    ? motion.presets as Record<string, unknown>
    : {};
  presetStore.value = { fps: null, racing: null, custom: null, steam: null };
  for (const key of ['fps', 'racing', 'custom', 'steam'] as const) {
    const p = rawPresets[key];
    if (p && typeof p === 'object' && !Array.isArray(p)) {
      const stored = p as Record<string, unknown>;
      if (typeof stored.autoCalibrate !== 'boolean') stored.autoCalibrate = true;
      delete stored.gyroMode;
      presetStore.value[key] = stored;
    } else {
      presetStore.value[key] = null;
    }
  }
  const currentStored = presetStore.value[activePreset.value];
  applyVirtualPad(settings.input.outputTarget);
  if (currentStored) {
    applyPresetParams(currentStored);
  } else {
    // 无该预设记忆：老配置零迁移——顶层参数归当前预设（其余预设待首次切换建默认）。
    // 2026-10-04 用户裁决：但若顶层完全是「未配置」默认（陀螺仪未开 + 按住触发 +
    // 无触发键 + 无变速触发键），视为全新/无数据配置，改用该预设自身默认，
    // 保证 FPS 射击预设默认=触发方式「常开」+ 变速触发键「LT」。
    const unconfigured = !enabled.value
      && motionMode.value === 'off'
      && motionTrigger.value === ''
      && aimingSightsTrigger.value === '';
    if (unconfigured) applyDefaultParamsForPreset(activePreset.value);
    presetStore.value[activePreset.value] = capturePresetParams();
  }
  // 2026-10-04 用户裁决：按住触发/按住抑制/切换 必须有触发键（默认 LT 防呆）。
  ensureMotionTrigger();
  // E7 回退（第七十七批 E-①，2026-09-12 用户批准）：删除 enabled+outputMode=disabled
  // 的 virtual-stick 强制迁移（E7 配套逻辑，随默认值回退一并撤销，不再自动改写老配置）。
  status.value = '配置已载入。';
  // Programmatic base reload must not enqueue an autosave.
  await nextTick();
  if (generation === loadGeneration) loaded.value = true;
  try {
    const runtime = await invoke<{ active?: boolean; gyroEnabled?: boolean }>('input.shortcutRuntime.get', {});
    if (generation === loadGeneration) applyShortcutRuntime(runtime);
  } catch { /* Older hosts have no temporary switches. */ }
}
let saveTimer: ReturnType<typeof window.setTimeout> | null = null;
let saveRequested = false;
// 用户裁决（2026-09-14）：重置此预设——把当前激活预设重置为该预设自身默认
// （fps/赛车/steam/custom 各不同，与 applyPreset 无记忆分支同口径），随后走
// 正常保存链。off 时 activePreset 仍是上次激活预设，同样重置它。
function applyDefaultParamsForPreset(kind: 'fps' | 'racing' | 'steam' | 'custom'): void {
  // 通用默认（与各 ref 初始默认一致）
  motionMode.value = 'on';
  motionTrigger.value = '';
  outputStick.value = 'right';
  outputAxis.value = 'xy';
  motionInput.value = 'local-space';
  steeringAxis.value = 'roll';
  gyroMultiplier.value = 1;
  accelerometerMultiplier.value = 1;
  motionSensitivityX.value = 1.5;
  motionSensitivityY.value = 1.5;
  velocityMode.value = 'default';
  velocityScale.value = 1;
  innerDeadzone.value = 1;
  outerDeadzone.value = 0;
  antiDeadzone.value = 20;
  gyroWeight.value = 1.8;
  outputShape.value = 'circle';
  invertHorizontal.value = false;
  invertVertical.value = false;
  steeringMaxAngle.value = 30;
  steeringPower.value = 1;
  steeringDeadzone.value = 0.5;
  aimingSightsMultiplier.value = 0.3;
  aimingSightsTrigger.value = '';
  sensitivityPreset.value = 'default';
  curvePreset.value = 'default';
  autoCalibrate.value = true;
  // 预设特定默认（对齐 applyPreset 无记忆分支）
  if (kind === 'fps') {
    // 用户裁决（2026-09-29）：FPS 射击预设触发模式默认「常开」。
    motionMode.value = 'on';
    motionTrigger.value = '';
    outputStick.value = 'right';
    outputAxis.value = 'xy';
    motionInput.value = 'local-space';
    // 用户裁决（2026-09-29）：FPS 射击预设的变速触发键默认 LT。
    aimingSightsTrigger.value = 'LT';
  } else if (kind === 'racing') {
    motionMode.value = 'on';
    motionTrigger.value = '';
    outputStick.value = 'left';
    outputAxis.value = 'x';
    motionInput.value = 'joystick-steering';
  } else if (kind === 'steam') {
    // 用户裁决（2026-09-29）：Steam 预设输入方式默认「本地」(local-space)。
    motionMode.value = 'on';
    motionTrigger.value = '';
    motionInput.value = 'local-space';
  }
  // custom 即通用默认
}
const PRESET_LABELS: Record<'fps' | 'racing' | 'steam' | 'custom', string> = {
  fps: 'FPS射击', racing: '赛车', steam: 'Steam', custom: '自定义',
};
function resetGyroDefaults(): void {
  if (gameLocked.value) return;
  const kind = activePreset.value;
  applyDefaultParamsForPreset(kind);
  // 立即把默认收进该预设快照（无需等保存链，避免 3s 防抖窗口内切走丢失）
  presetStore.value[kind] = capturePresetParams();
  status.value = `已将「${PRESET_LABELS[kind]}」重置为默认，即将保存。`;
  queueSave(true);
}
function queueSave(immediate = false): void {
  if (gameLocked.value || !loaded.value) return;
  saveRequested = true;
  if (saveTimer) window.clearTimeout(saveTimer);
  // 用户裁决（2026-09-14）：滑块输入 3 秒防抖（期间有新输入自动顺延），
  // 避免拖动过程中反复写盘触发原生刷新造成页面抖动。
  // 开启/预设立即落盘，否则用户开了还要等 3 秒才真正请求手柄。
  saveTimer = window.setTimeout(() => {
    saveTimer = null;
    void saveNow();
  }, immediate ? 0 : 3000);
}
async function saveNow(): Promise<void> {
  if (gameLocked.value || !snapshot.value || busy.value) return;
  saveRequested = false;
  busy.value = true;
  try {
    // 用户裁决（2026-09-14）：预设独立记忆——保存前把当前 refs 收进当前预设，
    // 未定义过的预设不落盘（null 跳过），落盘 presets 对象 + activePreset。
    presetStore.value[activePreset.value] = capturePresetParams();
    const presetsOut: Record<string, Record<string, unknown>> = {};
    for (const key of ['fps', 'racing', 'custom', 'steam'] as const) {
      const p = presetStore.value[key];
      if (!p) continue;
      // 【A（2026-09-15 用户裁决）】presetStore 是 ref 深层代理，读回 p 是
      // reactive Proxy；直接入 patch 会让 settingsRepository 的
      // structuredClone(next) 抛 DataCloneError（"#<Object> could not be
      // cloned"，fanFeature.ts:79-86 同款前科，本机 EBWebView 152 对 Proxy
      // 拒克隆）。toRaw 剥代理拿底层普通字面量（capturePresetParams 产出），
      // structuredClone 深拷贝纯数据——与 fanFeature 修法同构，确认盘数据
      // 仍为纯 JSON，不影响任何既有字段。
      const cloned = structuredClone(toRaw(p));
      delete cloned.gyroMode;
      presetsOut[key] = cloned;
    }
    const outputTarget = { gyroEnabled: enabled.value };
    const result = await compareAndSwapInputSettings(snapshot.value.revision, {
      outputTarget,
      gyroMotion: {
        enabled: enabled.value,
        provider: provider.value === 'unselected' ? null : provider.value,
        unit: 'deg/s',
        calibrationId: calibrationId.value.trim() || null,
        outputMode: outputMode.value,
        motionInput: motionInput.value,
        steeringAxis: steeringAxis.value,
        // 用户裁决（2026-09-14）：UI「按住抑制」落盘为 native mode='on'+触发键
        // （HC MotionMode.On=!pressed 即按住抑制）；「常开」落盘 mode='on'+空键。
        motionMode: motionMode.value === 'suppress' ? 'on' : motionMode.value,
        motionTrigger: motionMode.value === 'on'
          ? null
          : (motionTrigger.value.trim() || null),
        gyroMultiplier: gyroMultiplier.value,
        motionSensitivityX: motionSensitivityX.value,
        motionSensitivityY: motionSensitivityY.value,
        accelerometerMultiplier: accelerometerMultiplier.value,
        velocityMode: velocityMode.value,
        velocityScale: velocityScale.value,
        innerDeadzone: innerDeadzone.value,
        outerDeadzone: outerDeadzone.value,
        deadzone: innerDeadzone.value,
        antiDeadzone: antiDeadzone.value,
        gyroWeight: gyroWeight.value,
        outputShape: outputShape.value,
        invertHorizontal: invertHorizontal.value,
        invertVertical: invertVertical.value,
        autoCalibrate: autoCalibrate.value,
        virtualPadLink: virtualPadLink.value,
        preset: preset.value,
        outputStick: outputStick.value,
        outputAxis: outputAxis.value,
        steeringMaxAngle: steeringMaxAngle.value,
        steeringPower: steeringPower.value,
        steeringDeadzone: steeringDeadzone.value,
        aimingSightsMultiplier: aimingSightsMultiplier.value,
        aimingSightsTrigger: aimingSightsTrigger.value.trim() || null,
        // 曲线响应点已从 UI 移除：responseCurvePoints 始终 null（线性恒等），
        // 敏感曲线由下方曲线预设 Dropdown 写入（6）。
        responseCurvePoints: null,
        // 6：按所选曲线预设写入 49 节点数组；批96 起「默认」档=线性直通
        // （y=x 恒等），高/低档保留低段压缩。native main.cpp:5629-5638 已读数组。
        motionSensitivityArray: buildCurveNodes(curvePreset.value),
        // 用户裁决（2026-09-14）：预设独立记忆。activePreset 持久化（off 时
        // 也保留，用于记住上次激活预设）；presets 为四预设各自独立全套参数。
        activePreset: activePreset.value,
        presets: presetsOut,
      },
    });
    // 抖动根因 B（2026-09-14）：保存失败不再整页 load() 回填（拖动中的
    // 滑块会被快照回跳造成"页面刷新"观感）；仅提示，保留当前编辑值。
    if (!result.ok) { status.value = '配置版本已变化，请稍后重试保存。'; return; }
    // Only an explicit page save ends the temporary shortcut; hydration never saves.
    await invoke('input.shortcutRuntime.clear', {}).catch(() => undefined);
    shortcutGyroEnabled.value = null;
    snapshot.value = result.value;
    applyVirtualPad(result.value.outputTarget);
    status.value = `已记录（修订 ${result.revision}），等待 InputHost 确认。`;
  } catch (error) {
    status.value = `保存失败：${(error as Error).message}`;
  } finally {
    busy.value = false;
    if (saveRequested) queueSave();
  }
}
watch([
  enabled, outputMode, motionInput, steeringAxis, motionMode, motionTrigger, outputStick, outputAxis,
  gyroMultiplier, motionSensitivityX, motionSensitivityY, gyroWeight, accelerometerMultiplier,
  velocityMode, velocityScale, innerDeadzone, outerDeadzone, antiDeadzone, outputShape,
  invertHorizontal, invertVertical, steeringMaxAngle, steeringPower, steeringDeadzone,
  aimingSightsMultiplier, aimingSightsTrigger, autoCalibrate,
], () => queueSave());
// 用户裁决（2026-09-29）：虚拟手柄联动开启时，虚拟手柄开关状态变化需同步陀螺仪电源。
watch(virtualPadOn, () => { applyVirtualPadLink(); });
onMounted(() => {
  offShortcutRuntime = on('input.shortcutRuntime', applyShortcutRuntime);
  gyroPresentation.setVisible(isUiVisible());
  gyroPresentation.setActive(true);
  stopGyroVisibility = onUiVisibilityChange(({ visible }) => {
    if (!visible) previewPointerDown = false;
    gyroPresentation.setVisible(visible);
  });
  stopOverride = subscribeGameInputOverrideState((state) => {
    gameLocked.value = state.locked;
    if (saveTimer) window.clearTimeout(saveTimer);
    saveTimer = null; saveRequested = false;
    void load().catch(() => undefined);
  });
  window.addEventListener('input:motion-telemetry', telemetry);
  // 抖动根因 A 配套：拖拽检测（冻结页面级 XYZ 刷新；气泡照常）
  window.addEventListener('pointerdown', onGyroPreviewPointerDown, { capture: true });
  window.addEventListener('pointerup', onGyroPreviewPointerUp, { capture: true });
  offCalibResult = on('gyro.calibrate.result', (detail: { ok?: boolean; saved?: boolean; source?: string; confidence?: number; weight?: number; offset?: { x?: number; y?: number; z?: number } }) => {
    // Y-1 诊断埋点（LegB）：result 事件到达时刻。
    console.log('[gyro.calibrate] result', detail, new Date().toISOString());
    void invoke('diagnostics.frontendError', { where: 'gyro-motion', message: 'calibration-result-received-ok:' + String(detail?.ok === true) }).catch(() => {});
    clearCalibTimeout();
    calibrating.value = false;
    const saved = detail?.saved === true || (detail?.ok === true && detail?.saved !== false && (detail.confidence ?? 0) > 0.99);
    if (saved) {
      calibError.value = '';
      calibUiStatus.value = 'already-calibrated';
      calibResult.value = {
        confidence: detail.confidence ?? 0,
        weight: detail.weight ?? 0,
        offsetX: detail.offset?.x ?? 0,
        offsetY: detail.offset?.y ?? 0,
        offsetZ: detail.offset?.z ?? 0,
      };
    } else if (detail?.source === 'auto') {
      calibError.value = '';
    } else {
      calibResult.value = null;
      calibError.value = '未达到确认可信，未保存';
    }
  });
  offGamepadState = on<{ backendInstallPrompt?: number }>('gamepad.state', (state) => {
    const prompt = state.backendInstallPrompt;
    if (prompt && usbipPromptDismissed.value !== prompt && !usbipPrompt.value) {
      usbipPrompt.value = prompt === 1 ? 'missing' : 'failed';
    }
  });
  offCalibStatus = on('gyro.calibrate.status', (detail: { status?: string }) => {
    applyCalibStatus(detail?.status);
  });
  void invoke<{ status?: string; trusted?: boolean }>('gyro.calibrate.status', {}).then((s) => {
    applyCalibStatus(s?.status);
    if (s?.trusted) calibUiStatus.value = 'already-calibrated';
  }).catch(() => {});
  void load().catch((error) => { status.value = `读取失败：${(error as Error).message}`; });
});
onActivated(() => {
  gyroPresentation.setActive(true);
  if (busy.value || saveRequested || saveTimer) return;
  void load().catch(() => undefined);
});
onDeactivated(() => {
  previewPointerDown = false;
  gyroPresentation.setActive(false);
});
onBeforeUnmount(() => {
  gyroPresentation.dispose();
  stopGyroVisibility?.();
  stopGyroVisibility = null;
  stopOverride?.();
  if (saveTimer) window.clearTimeout(saveTimer);
  if (gyroConnectedTimer) window.clearTimeout(gyroConnectedTimer);
  clearCalibTimeout();
  if (offCalibResult) offCalibResult();
  if (offCalibStatus) offCalibStatus();
  if (offGamepadState) offGamepadState();
  offShortcutRuntime?.();
  window.removeEventListener('input:motion-telemetry', telemetry);
  window.removeEventListener('pointerdown', onGyroPreviewPointerDown, { capture: true } as EventListenerOptions);
  window.removeEventListener('pointerup', onGyroPreviewPointerUp, { capture: true } as EventListenerOptions);
});
</script>
<template>
  <div class="page">
    <p v-if="gameLocked" class="game-override-notice" role="status">游戏专属配置生效中</p>
    <section class="card">
      <h3 class="card-title"><InlineIcon name="rotate" /> 开启陀螺仪</h3>
      <div class="field-stack">
        <div class="gyro-power-row">
          <button
            type="button"
            class="gyro-power-btn"
            :class="gyroPowerClass"
            :disabled="controlsLocked || gyroPowerState === 'unknown'"
            data-gp-row="0"
            data-gp-col="0"
            @click="toggleGyroPower"
          >
            {{ gyroPowerText }}
          </button>
          <button
            type="button"
            class="gyro-power-btn link"
            :disabled="controlsLocked"
            :class="{ on: virtualPadLink }"
            data-gp-row="0"
            data-gp-col="1"
            @click="toggleVirtualPadLink"
          >
            {{ virtualPadLink ? '虚拟手柄联动已开启' : '虚拟手柄联动已关闭' }}
          </button>
        </div>
        <div class="field">
          <span>预设</span>
          <SegButton :model-value="preset" :options="presetOptions" full :disabled="controlsLocked" :gp-row="1" @update:model-value="applyPreset" />
        </div>
      </div>
    </section>

    <section v-if="preset !== 'steam'" class="card">
      <h3 class="card-title"><InlineIcon name="target" /> 触发模式</h3>
      <div class="field-stack">
        <div
          class="field-grid"
          :class="{ 'three-col': motionMode !== 'on' }"
        >
          <label class="field">
            <span>触发方式</span>
            <Dropdown v-model="motionMode" :options="motionModeOptions" :disabled="controlsLocked" :gp-row="3" :gp-col="0" @change="onMotionModeChange" />
          </label>
          <label v-if="motionMode !== 'on'" class="field">
            <span>触发键</span>
            <Dropdown v-model="motionTrigger" :options="triggerOptions" :disabled="controlsLocked" :gp-row="3" :gp-col="1" @change="markCustom" />
          </label>
          <label class="field">
            <span>变速触发键</span>
            <Dropdown v-model="aimingSightsTrigger" :options="aimingTriggerOptions" :disabled="controlsLocked" :gp-row="3" :gp-col="motionMode !== 'on' ? 2 : 1" @change="markCustom" />
          </label>
        </div>
        <Slider v-model="aimingSightsMultiplier" label="变速触发键倍率" :min="0" :max="2" :step="0.1" :value-text="aimingSightsMultiplierText" :disabled="controlsLocked || !aimingSightsTrigger" :gp-row="4" :gp-col="0" @update:model-value="markCustom" />
      </div>
    </section>

    <!-- 2026-09-30 用户裁决：Steam 预设的说明小字，位置在「输入模式」上方。 -->
    <p v-if="preset === 'steam'" class="steam-preset-hint">请在 Steam 内设置，支持 SteamDeck 和 PS5 虚拟手柄</p>

    <section class="card">
      <h3 class="card-title"><InlineIcon name="speed" /> 输入模式</h3>
      <div class="field-stack">
        <div v-if="preset !== 'steam'" class="field">
          <span>输出到</span>
          <SegButton v-model="outputStick" :options="stickOptions" full :disabled="controlsLocked" :gp-row="5" @update:model-value="markCustom" />
        </div>
        <div class="field-grid" :class="{ 'one-col': preset === 'steam' }">
          <label class="field">
            <span>输入方式</span>
            <Dropdown v-model="motionInput" :options="motionInputOptions" :disabled="controlsLocked" show-selected-sub :gp-row="6" :gp-col="0" @change="markCustom" />
          </label>
          <div v-if="preset !== 'steam'" class="field">
            <span>速度模式</span>
            <SegButton v-model="velocityMode" :options="velocityModeOptions" full :disabled="controlsLocked" :gp-row="6" :gp-col-start="1" @update:model-value="markCustom" />
          </div>
        </div>
        <Slider v-if="preset !== 'steam' && velocityMode === 'velocity'" v-model="velocityScale" label="变速倍率" :min="0.1" :max="5" :step="0.1" :value-text="velocityScale.toFixed(1)" :disabled="controlsLocked" :gp-row="7" :gp-col="0" @update:model-value="markCustom" />
        <Slider v-model="gyroWeight" label="陀螺仪权重【倍率】" :min="1" :max="3" :step="0.1" :value-text="gyroWeight.toFixed(1)" :disabled="controlsLocked" :gp-row="8" :gp-col="0" @update:model-value="markCustom" />
        <template v-if="motionInput === 'joystick-steering'">
          <div class="modifier-title">赛车倾斜</div>
          <Slider v-model="steeringMaxAngle" label="最大倾角" unit="°" :min="10" :max="80" :step="1" :disabled="controlsLocked" :gp-row="9" :gp-col="0" @update:model-value="markCustom" />
          <Slider v-model="steeringPower" label="转向曲线" :min="0.2" :max="5" :step="0.1" :value-text="steeringPower.toFixed(1)" :disabled="controlsLocked" :gp-row="10" :gp-col="0" @update:model-value="markCustom" />
          <Slider v-model="steeringDeadzone" label="倾斜死区" unit="°" :min="0" :max="5" :step="0.1" :disabled="controlsLocked" :gp-row="11" :gp-col="0" @update:model-value="markCustom" />
        </template>
      </div>
    </section>

    <section class="card">
      <div class="inner preview">
        <div class="stage" aria-label="陀螺仪数据面">
          <i class="mid-x"></i>
          <i class="mid-y"></i>
          <span class="axis-tag tag-x">X</span>
          <span class="axis-tag tag-y">Y</span>
          <span class="axis-tag tag-z">Z</span>
          <b class="dot host-dot" :class="{ inactive: hostFrameState !== 'frame-accepted' || !hostActive }" :style="hostLiveDot" aria-label="native InputHost canonical frame receipt"></b>
          <b class="dot preview-dot" :style="previewLiveDot" aria-label="本地模拟预览"></b>
        </div>
        <div class="axis-chips">
          <div class="chip"><span>X</span><b>{{ gyroAxes.x.toFixed(1) }}</b></div>
          <div class="chip"><span>Y</span><b>{{ gyroAxes.y.toFixed(1) }}</b></div>
          <div class="chip"><span>Z</span><b>{{ gyroAxes.z.toFixed(1) }}</b></div>
        </div>
      </div>
      <p v-if="busy || status.includes('失败')" class="hint live-status">{{ busy ? '正在记录…' : status }}</p>
    </section>

    <section class="card">
      <div class="advanced-head">
        <h3 class="card-title"><InlineIcon name="settings" /> 高级</h3>
        <button type="button" class="expand" :aria-expanded="advancedOpen" data-gp-row="15" data-gp-col="0" @click="advancedOpen = !advancedOpen">
          {{ advancedOpen ? '收起' : '展开' }}
        </button>
      </div>

      <div v-if="advancedOpen" class="advanced-content">
        <div class="inner calib-row">
          <div class="calib-static">
            <span class="row-label">静态校准</span>
            <button type="button" class="calib-btn" :disabled="calibrating || busy" data-gp-row="16" data-gp-col="0" @click="startCalibration">
              {{ calibrating ? '校准中' : '开始校准' }}
            </button>
          </div>
          <div class="calib-auto">
            <Toggle :model-value="autoCalibrate" label="自动校准" :disabled="controlsLocked" :gp-row="16" :gp-col="1" @update:model-value="onAutoCalibrate" />
            <p class="calib-result" :class="'calib-' + calibUiStatus">{{ calibStatusText }}</p>
            <span v-if="calibResult && calibUiStatus === 'already-calibrated'" class="calib-result">
              可信度 {{ (calibResult.confidence * 100).toFixed(0) }}%
            </span>
            <span v-else-if="calibError" class="calib-error">{{ calibError }}</span>
          </div>
        </div>
        <div class="inner">
          <Slider :model-value="motionSensitivityX" label="灵敏度" :min="0.5" :max="2" :step="0.1" :value-text="motionSensitivityX.toFixed(1)" :disabled="controlsLocked" :gp-row="18" :gp-col="0" @update:model-value="onSensitivityLevel" />
        </div>
        <div class="inner">
          <Slider v-model="gyroMultiplier" label="速度" :min="0.1" :max="3" :step="0.1" :value-text="gyroMultiplier.toFixed(1)" :disabled="controlsLocked" :gp-row="19" :gp-col="0" @update:model-value="markCustom" />
        </div>
        <div class="inner">
          <Slider v-model="accelerometerMultiplier" label="加速度倍率" :min="0.1" :max="3" :step="0.1" :value-text="accelerometerMultiplier.toFixed(1)" :disabled="controlsLocked" :gp-row="20" :gp-col="0" @update:model-value="markCustom" />
        </div>
        <div class="inner">
          <Slider v-model="motionSensitivityX" label="水平" :min="0.1" :max="3" :step="0.1" :value-text="motionSensitivityX.toFixed(1)" :disabled="controlsLocked" :gp-row="21" :gp-col="0" @update:model-value="markCustom" />
        </div>
        <div class="inner">
          <Slider v-model="motionSensitivityY" label="垂直" :min="0.1" :max="3" :step="0.1" :value-text="motionSensitivityY.toFixed(1)" :disabled="controlsLocked" :gp-row="22" :gp-col="0" @update:model-value="markCustom" />
        </div>
        <div class="inner">
          <div class="field-grid" :class="{ 'three-col': motionInput === 'local-space' }">
            <label v-if="motionInput === 'local-space'" class="field">
              <span>转向轴</span>
              <Dropdown v-model="steeringAxis" :options="steeringAxisOptions" :disabled="controlsLocked" :gp-row="23" :gp-col="0" @change="markCustom" />
            </label>
            <div class="field">
              <span>水平反转</span>
              <Toggle v-model="invertHorizontal" compact :disabled="controlsLocked" :gp-row="23" :gp-col="motionInput === 'local-space' ? 1 : 0" @update:model-value="markCustom" />
            </div>
            <div class="field">
              <span>垂直反转</span>
              <Toggle v-model="invertVertical" compact :disabled="controlsLocked" :gp-row="23" :gp-col="motionInput === 'local-space' ? 2 : 1" @update:model-value="markCustom" />
            </div>
          </div>
        </div>
        <div class="modifier-title">陀螺输出修饰</div>
        <div class="inner">
          <Slider v-model="innerDeadzone" label="内死区" unit="%" :min="0" :max="25" :step="0.5" :disabled="controlsLocked" :gp-row="24" :gp-col="0" @update:model-value="markCustom" />
        </div>
        <div class="inner">
          <Slider v-model="outerDeadzone" label="外死区" unit="%" :min="0" :max="25" :step="5" :disabled="controlsLocked" :gp-row="25" :gp-col="0" @update:model-value="markCustom" />
        </div>
        <div class="inner">
          <Slider v-model="antiDeadzone" label="反死区" unit="%" :min="0" :max="40" :step="5" :disabled="controlsLocked" :gp-row="26" :gp-col="0" @update:model-value="markCustom" />
        </div>
        <div class="inner row-inner">
          <span class="row-label">输出形状</span>
          <Dropdown v-model="outputShape" :options="outputShapeOptions" width="104px" :disabled="controlsLocked" :gp-row="27" :gp-col="0" @change="markCustom" />
        </div>
        <button type="button" class="reset-preset-btn" :disabled="controlsLocked" data-gp-row="28" data-gp-col="0" @click="resetGyroDefaults">重置此预设</button>
      </div>
    </section>

    <div v-if="usbipPrompt" class="usbip-prompt-backdrop" @click.self="dismissUsbipPrompt">
      <div class="usbip-prompt-card">
        <h3 class="card-title"><InlineIcon name="download" /> 需要 USBip 后端</h3>
        <p>{{ usbipPromptText }}</p>
        <div class="usbip-prompt-actions">
          <button type="button" class="usbip-btn" data-gp-row="31" data-gp-col="0" @click="dismissUsbipPrompt">稍后再说</button>
          <button type="button" class="usbip-btn primary" data-gp-row="31" data-gp-col="1" @click="openUsbipSite">是，去官网下载</button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.game-override-notice{margin:0 0 12px;color:var(--accent);font-size:12px;font-weight:700}
.gyro-power-btn:disabled{opacity:.5;cursor:default}
.page { padding-bottom: 20px; }
/* 2026-09-30 用户裁决：Steam 预设说明小字（位于「输入模式」上方，跟随卡片 10px 节奏）。
   2026-10-04 用户裁决：改为居中显示并放大字体。 */
.steam-preset-hint { margin: 0 0 10px; color: var(--text-dim); font-size: 13px; line-height: 1.5; text-align: center; }
.calib-btn { min-height: var(--btn-min-h); padding: 0 12px; border: 1px solid #2a3342; border-radius: var(--radius-ctrl); background: var(--bg-input); color: var(--text); cursor: pointer; font-weight: 700; }
.calib-btn:disabled { opacity: .5; cursor: default; }
.calib-result { color: #8b96a8; font-size: 12px; line-height: 1.5; }
.calib-result.calib-waiting-still, .calib-result.calib-calibrating { color: #2ea6ff; }
.calib-result.calib-already-calibrated { color: #7fe08a; }
.calib-result small { display: block; color: #9fd0a9; }
.calib-error { color: #ff8080; font-size: 12px; }
.inner {
  padding: 8px 10px;
  margin-top: 8px;
  border: 1px solid rgba(255, 255, 255, 0.06);
  border-radius: 10px;
  background: color-mix(in srgb, var(--bg-input) 88%, transparent);
}
.inner:first-of-type { margin-top: 0; }
.preset-output {
  margin-top: 8px;
  padding-top: 8px;
  border-top: 1px solid rgba(255, 255, 255, 0.06);
}
.row-inner {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
}
.calib-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
}
.calib-static {
  display: flex;
  align-items: center;
  gap: 10px;
  flex: 0 0 auto;
}
.calib-auto {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
  justify-content: flex-end;
  min-width: 0;
}
.calib-auto :deep(.toggle-row) {
  padding: 0;
}
.gyro-power-row {
  display: flex;
  align-items: stretch;
  gap: 8px;
}
.gyro-power-btn {
  flex: 1 1 0;
  min-width: 0;
  min-height: 38px;
  padding: 0 8px;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: var(--radius-ctrl);
  background: var(--bg-input);
  color: var(--text);
  font-size: 11px;
  font-weight: 700;
  cursor: pointer;
  transition: background 0.12s, color 0.12s, border-color 0.12s;
}
.gyro-power-btn.on {
  border-color: var(--accent);
  background: var(--accent);
  color: #06121d;
}
.gyro-power-btn.off {
  border-color: var(--danger);
  background: var(--danger);
  color: #fff;
}
.gyro-power-btn.unknown {
  border-color: rgba(255, 255, 255, 0.08);
  background: var(--bg-input);
  color: var(--text-dim);
}
.gyro-power-btn.unknown:disabled {
  opacity: 0.6;
  cursor: default;
}
/* YMCC 规则：实心强调按钮手柄聚焦时保留蓝色外发光并加白色内描边。 */
.gyro-power-btn.on.focused {
  box-shadow: inset 0 0 0 2px #fff, var(--focus-ring);
}
.gyro-power-btn.link.on {
  border-color: color-mix(in srgb, var(--accent) 45%, transparent);
  background: color-mix(in srgb, var(--accent) 12%, var(--bg-input));
  color: var(--accent);
}
.gyro-power-btn.link.on.focused {
  box-shadow: var(--focus-ring);
}
.field-stack { display: grid; gap: 10px; }
.field { display: grid; gap: 6px; min-width: 0; }
.field > span {
  color: var(--text-dim);
  font-size: 12px;
  white-space: nowrap;
}
.field-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 10px;
}
.field-grid.one-col { grid-template-columns: minmax(0, 1fr); }
.field-grid.three-col { grid-template-columns: repeat(3, minmax(0, 1fr)); }
.preview {
  display: grid;
  /* 用户裁决（2026-09-29）：永远左右结构——首列随视口收紧而等比缩小，
     不再在窄宽度下折成上下两行。 */
  grid-template-columns: clamp(84px, 26vw, 132px) minmax(0, 1fr);
  gap: 10px;
  align-items: center;
}
.stage {
  position: relative;
  width: 100%;
  aspect-ratio: 1 / 1;
  border-radius: 50%;
  background: color-mix(in srgb, var(--bg-panel) 80%, #000);
}
.mid-x, .mid-y { position: absolute; background: color-mix(in srgb, var(--text) 28%, transparent); }
.mid-x { left: 10%; right: 10%; top: 50%; height: 1px; }
.mid-y { top: 10%; bottom: 10%; left: 50%; width: 1px; }
.axis-tag {
  position: absolute;
  font-size: 10px;
  font-weight: 700;
  color: var(--accent);
}
.tag-x { right: 8px; top: calc(50% + 4px); }
.tag-y { top: 8px; left: calc(50% + 6px); }
.tag-z { left: 8px; bottom: 8px; }
.dot {
  position: absolute;
  width: 10px;
  height: 10px;
  margin: -5px;
  border-radius: 50%;
  background: var(--accent);
}
.host-dot {
  z-index: 2;
  background: #65d6ff;
  box-shadow: 0 0 0 2px color-mix(in srgb, #65d6ff 24%, transparent);
}
.host-dot.inactive { opacity: 0.32; }
.preview-dot {
  z-index: 3;
  box-sizing: border-box;
  background: transparent;
  border: 2px solid var(--accent);
  box-shadow: 0 0 0 1px color-mix(in srgb, #000 55%, transparent);
}
.axis-chips { display: grid; gap: 6px; }
.chip {
  display: flex;
  justify-content: space-between;
  align-items: center;
  min-height: 34px;
  padding: 0 10px;
  border-radius: 8px;
  background: color-mix(in srgb, var(--bg-panel) 78%, #000);
}
.chip span { font-size: 12px; font-weight: 700; color: var(--accent); }
.chip b { font-size: 13px; font-variant-numeric: tabular-nums; }
.live-status { padding: 2px 2px 0; }
.advanced-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
}
.advanced-head .card-title { margin-bottom: 0; }
.expand {
  min-height: 28px;
  padding: 5px 10px;
  border: 1px solid color-mix(in srgb, var(--accent) 45%, transparent);
  border-radius: var(--radius-ctrl);
  background: color-mix(in srgb, var(--accent) 10%, transparent);
  color: var(--accent);
  font-size: 11px;
  font-weight: 700;
  cursor: pointer;
}
.expand:focus-visible { box-shadow: var(--focus-ring); }
.advanced-content { margin-top: 10px; }
.modifier-title {
  margin: 14px 2px 4px;
  color: var(--text-dim);
  font-size: 11px;
  font-weight: 700;
}
.value {
  min-height: 32px;
  width: min(160px, 45vw);
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: var(--radius-ctrl);
  background: color-mix(in srgb, var(--bg-panel) 80%, #000);
  color: var(--text);
  padding: 0 9px;
}
.hint { margin: 8px 0 0; font-size: 12px; color: var(--text-dim); }
/* 用户裁决（2026-09-29）：重置此预设——整行红色按钮，置于高级展开区底部。 */
.reset-preset-btn {
  display: block;
  width: 100%;
  margin-top: 14px;
  min-height: var(--btn-min-h);
  border: 1px solid color-mix(in srgb, var(--danger, #ef4444) 62%, transparent);
  border-radius: var(--radius-ctrl);
  background: color-mix(in srgb, var(--danger, #ef4444) 22%, var(--bg-input));
  color: #ffb4b4;
  font-weight: 700;
  cursor: pointer;
}
.reset-preset-btn:disabled { opacity: .5; cursor: default; }
.reset-preset-btn:focus-visible { box-shadow: var(--focus-ring); }

.usbip-prompt-backdrop {
  position: fixed;
  inset: 0;
  z-index: 900;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(0, 0, 0, 0.55);
}
.usbip-prompt-card {
  width: min(420px, calc(100vw - 32px));
  padding: 16px;
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: var(--radius-ctrl);
  background: var(--bg-panel);
}
.usbip-prompt-card h3 {
  display: flex;
  align-items: center;
  gap: 6px;
  margin: 0 0 8px;
  font-size: 13px;
}
.usbip-prompt-card p {
  margin: 0 0 14px;
  color: var(--text-dim);
  font-size: 11px;
  line-height: 1.5;
}
.usbip-prompt-actions { display: flex; justify-content: flex-end; gap: 8px; }
.usbip-btn {
  min-height: 30px;
  padding: 0 12px;
  border: 1px solid rgba(255, 255, 255, 0.14);
  border-radius: var(--radius-ctrl);
  background: color-mix(in srgb, var(--bg-input) 88%, transparent);
  color: var(--text);
  font-size: 11px;
  font-weight: 700;
  cursor: pointer;
}
.usbip-btn.primary {
  border-color: color-mix(in srgb, var(--accent) 55%, transparent);
  background: color-mix(in srgb, var(--accent) 14%, transparent);
  color: var(--accent);
}
</style>
