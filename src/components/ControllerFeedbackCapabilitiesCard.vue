<script setup lang="ts">
// [AI:USER:FEEDBACK] 震动测试/灯光调整→请求对应实体效果；能力与当前设备/消费者绑定，先查 capabilities/ 总库。
// 保存强度、手动测试、触摸反馈与真实游戏反馈分别判断；不支持或未确认时如实呈现，不能由一项成功推断其他项。
import { computed, onActivated, onBeforeUnmount, onDeactivated, onMounted, ref } from 'vue';
import InlineIcon from '@/components/InlineIcon.vue';
import Dropdown from '@/components/Dropdown.vue';
import Slider from '@/components/Slider.vue';
import Toggle from '@/components/Toggle.vue';
import { gamepadFeedback, type LedFeedbackCapabilities } from '@/bridge/api';
import { invoke } from '@/bridge/ipc';
import { loadSettings, saveSettingsSection } from '@/bridge/settingsRepository';
import { EDIT_REPEAT_BASE_MS } from '@/gamepad/sliderRepeat';

interface LightingSettings {
  ownership: 'ymcc' | 'oem';
  enabled: boolean;
  off: boolean;
  mode: string;
  presetId: string;
  color: string;
  color2: string;
  speed: number;
  brightness: number;
}
interface FeedbackSettings {
  vibrationStrength: number;
  lighting: LightingSettings;
}

const DEFAULT_FEEDBACK: FeedbackSettings = {
  vibrationStrength: 70,
  lighting: { ownership: 'ymcc', enabled: false, off: false, mode: 'solid', presetId: '', color: '#2ea6ff', color2: '#9d7cff', speed: 55, brightness: 65 },
};

const settings = ref<FeedbackSettings>(structuredClone(DEFAULT_FEEDBACK));
const caps = ref<LedFeedbackCapabilities>({
  ledAvailable: false,
  brightnessAvailable: false,
  ledEffects: [],
  vibrationAvailable: false,
  physicalPrimarySlot: -1,
});
let feedbackActive = false;
let feedbackEpoch = 0;
let feedbackLoaded = false;
let capabilitiesRequest = 0;
const currentFeedbackSession = (epoch: number) => feedbackActive && epoch === feedbackEpoch;
const capsError = ref('');
const status = ref('');
const feedbackSaveIssue = ref<'' | 'failed' | 'unconfirmed'>('');
const feedbackSaveMessage = computed(() => feedbackSaveIssue.value === 'failed'
  ? '设置保存失败，重启后是否保留尚未确认。'
  : feedbackSaveIssue.value === 'unconfirmed' ? '上次设置保存结果尚未确认，重新调整后会再次保存。' : '');
let feedbackSaveRequest = 0;
let feedbackSavePending = false;
const ayaFeedback = computed(() => caps.value.machineFamily === 'ayanero');
const oemLightingSelected = computed(() => ayaFeedback.value && settings.value.lighting.ownership === 'oem');
const ownershipBusy = ref(false);
const ownershipOptions = computed(() => [
  { value: 'ymcc', label: 'YMCC 灯效' },
  { value: 'oem', label: '原厂显示', disabled: caps.value.oemRestoreAvailable !== true },
]);
const vibrationTestBusy = ref(false);
const vibrationTestMessage = ref('');
const vibrationTestFailed = ref(false);
let vibrationTestRequest = 0;
function vibrationFailure(reason?: string): string {
  if (reason === 'no-xinput-physical-source' || reason === 'source-pending') return '实体手柄震动源未就绪，请待手柄恢复后再试。';
  if (reason === 'source-changed') return '实体手柄来源已变化，本次测试未完成。';
  if (reason === 'power-transition' || reason === 'cancelled') return '电源或输入状态已变化，本次测试已停止。';
  if (reason === 'write-failed' || reason === 'open-failed') return '实体手柄震动请求失败，请导出本次日志。';
  if (reason === 'pulse-stop-unavailable' || reason === 'stop-unavailable') return '震动停止通路未就绪，本次测试未完成。';
  if (reason === 'manual-pulse-active' || reason === 'queue-full' || reason === 'throttled') return '震动通路正忙，请稍后手动重试。';
  return '震动请求未被确认，请导出本次日志。';
}
async function triggerVibration(): Promise<void> {
  if (!feedbackActive || !vibrationAvailable.value || vibrationTestBusy.value) return;
  const epoch = feedbackEpoch, request = ++vibrationTestRequest;
  const strength = settings.value.vibrationStrength;
  const current = () => currentFeedbackSession(epoch) && request === vibrationTestRequest;
  vibrationTestBusy.value = true;
  vibrationTestFailed.value = false;
  vibrationTestMessage.value = '正在等待实体震动请求回执…';
  try {
    const first = await gamepadFeedback.vibrate(strength);
    if (!current()) return;
    if (first?.ok !== true) {
      vibrationTestFailed.value = true;
      vibrationTestMessage.value = vibrationFailure(first?.reason);
      return;
    }
    await new Promise(resolve => window.setTimeout(resolve, 125));
    if (!current()) return;
    if (!vibrationAvailable.value) {
      vibrationTestFailed.value = true;
      vibrationTestMessage.value = '实体手柄状态已变化，本次测试未完成。';
      return;
    }
    const second = await gamepadFeedback.vibrate(strength);
    if (!current()) return;
    vibrationTestFailed.value = second?.ok !== true;
    vibrationTestMessage.value = second?.ok !== true
      ? '后段请求未完成：' + vibrationFailure(second?.reason)
      : strength === 0 ? '强度为 0，仅提交零强度请求，无法据此判断震感。' : '请求已受理，实际震感仍需手动确认。';
  } catch {
    if (!current()) return;
    vibrationTestFailed.value = true;
    vibrationTestMessage.value = '未取得震动回执，实际结果未确认；本次不会自动重试。';
  } finally {
    if (request === vibrationTestRequest) vibrationTestBusy.value = false;
  }
}
// 能力探测进行中（首帧 capabilites 未返回）：灯光块显示"加载中"，控件禁用。
const capsLoading = ref(true);
const ledAvailable = computed(() => caps.value.ledAvailable);
const vibrationAvailable = computed(() => caps.value.vibrationAvailable);
const vibrationStateText = computed(() => {
  if (capsLoading.value) return '加载中';
  if (!vibrationAvailable.value) return '暂不可用';
  if (vibrationTestBusy.value) return '测试中';
  if (vibrationTestFailed.value) return '未完成';
  return vibrationTestMessage.value ? '请求已受理' : '可请求';
});
// 卡片与两个 bubble 始终可见；HC DevicePage 能力门（IsDynamicLightingSupported）
// 决定灯光控件是否可用，而非整卡隐藏。加载中显示"加载中"。
// 常规副色能力位与效果的分区颜色分别判断。总库授予 ambilight 时，
// ROG/MSI 实际 writer 都消费左右两色；不能因缺少常规副色位藏掉右侧设置。
const splitColorEffect = computed(() => settings.value.lighting.mode === 'ambilight' && caps.value.ledEffects.includes('ambilight'));
const supportsSecondColor = computed(() => caps.value.supportsSecondColor === true || splitColorEffect.value);
const ledReasonText = computed(() => {
  if (caps.value.ledReason === 'machine-unsupported') return '当前机型在机型库中无灯光能力（HC 无 DynamicLighting），灯光功能已隐藏。';
  if (caps.value.ledReason === 'backend-not-implemented') return '机型库支持本机灯光，但 YMCC 尚未实现该机型灯光后端，选项已禁用。';
  if (caps.value.ledReason === 'rog-hid-not-matched') return '未在本机发现 0B05 Aura 设备（PID 1ABE/1B4C）。';
  if (caps.value.ledReason === 'rog-hid-control-not-open') return '已发现 Aura 设备，但控制句柄未打开。';
  return caps.value.ledReason && caps.value.ledReason !== 'ok' ? `内部原因：${caps.value.ledReason}` : '';
});
const vibrationReasonText = computed(() => {
  if (caps.value.vibrationReason === 'no-xinput-slots') return 'XInput 无槽位连接，物理手柄未就绪。';
  return caps.value.vibrationReason && caps.value.vibrationReason !== 'ok' ? `内部原因：${caps.value.vibrationReason}` : '';
});

const effectLabels: Record<string, string> = {
  solid: '常亮',
  breathing: '呼吸',
  rainbow: '彩虹',
  wheel: 'Wheel',
  ambilight: '氛围（分区配色）',
  gradient: '渐变',
  preset: '预设',
};
const presetOptions = computed(() => (caps.value.ledPresets ?? []).map((preset) => ({ value: preset.id, label: preset.label })));
const effectOptions = computed(() =>
  caps.value.ledEffects
    .filter((effect) => effectLabels[effect] && (effect !== 'preset' || presetOptions.value.length > 0))
    .map((effect) => ({ value: effect, label: effectLabels[effect]! })));
const speedRelevant = computed(() => caps.value.lightingFixedSpeed !== true && !['solid', 'ambilight', 'preset'].includes(settings.value.lighting.mode));
const presetSecondColor = computed(() => settings.value.lighting.mode === 'preset' && caps.value.presetSupportsSecondColor === true);
// 颜色预设：HC 用开放式 ColorPicker（任意色），YMCC 以常用色下拉近似。
// 主/副颜色由 native 能力与当前效果共同判定；氛围分区可分别设置左右颜色。
const lightColors = [
  { name: '青蓝', value: '#2ea6ff' },
  { name: '天蓝', value: '#1c7ed6' },
  { name: '紫罗兰', value: '#9d7cff' },
  { name: '品红', value: '#e64980' },
  { name: '薄荷', value: '#6fe4bd' },
  { name: '翠绿', value: '#2f9e44' },
  { name: '暖白', value: '#f4d89d' },
  { name: '琥珀', value: '#fab005' },
  { name: '珊瑚', value: '#ff7a59' },
  { name: '纯白', value: '#ffffff' },
];
const colorOptions = computed(() => lightColors.map((c) => ({ value: c.value, label: c.name })));

function clampInt(value: unknown, fallback: number, min: number, max: number): number {
  const n = Number(value);
  return Number.isFinite(n) ? Math.min(max, Math.max(min, Math.round(n))) : fallback;
}
function validColor(value: unknown): string {
  const s = typeof value === 'string' ? value.trim() : '';
  return /^#[0-9a-fA-F]{6}$/.test(s) ? s : '#2ea6ff';
}
// 能力门收敛：持久化的效果可能来自另一台机器/旧版本（如 ROG 的 rainbow 带到只支持
// solid/ambilight 的 MSI Claw）。native 后端（msiClawApplyLed）对列表外模式返回
// invalid-mode，表现为「灯光无法开启」；此处把模式收敛到本机 ledEffects 内，
// 保证下拉框、设置与 native 三者一致。空列表（后端未实现族，fail-closed）不动。
function clampLedModeToCaps(): void {
  const supported = effectOptions.value.map((option) => option.value);
  if (supported.length === 0 || supported.includes(settings.value.lighting.mode)) return;
  const fallback = supported[0];
  if (fallback) settings.value.lighting.mode = fallback;
}
function clampPresetToCaps(): void {
  if (presetOptions.value.length && !presetOptions.value.some((option) => option.value === settings.value.lighting.presetId))
    settings.value.lighting.presetId = presetOptions.value[0]!.value;
}

async function loadFeedback(epoch: number): Promise<void> {
  try {
    const all = await loadSettings();
    if (!currentFeedbackSession(epoch)) return;
    const raw = all.gamepad?.feedback as Partial<FeedbackSettings> | undefined;
    const lighting = raw?.lighting as Partial<LightingSettings> | undefined;
    settings.value = {
      vibrationStrength: clampInt(raw?.vibrationStrength, DEFAULT_FEEDBACK.vibrationStrength, 0, 100),
      lighting: {
        ownership: lighting?.ownership === 'oem' ? 'oem' : 'ymcc',
        enabled: lighting?.enabled === true,
        off: lighting?.off === true,
        mode: typeof lighting?.mode === 'string' && effectLabels[lighting.mode] ? lighting.mode : DEFAULT_FEEDBACK.lighting.mode,
        presetId: typeof lighting?.presetId === 'string' ? lighting.presetId : '',
        color: validColor(lighting?.color ?? DEFAULT_FEEDBACK.lighting.color),
        color2: validColor(lighting?.color2 ?? DEFAULT_FEEDBACK.lighting.color2),
        speed: clampInt(lighting?.speed, DEFAULT_FEEDBACK.lighting.speed, 0, 100),
        brightness: clampInt(lighting?.brightness, DEFAULT_FEEDBACK.lighting.brightness, 0, 100),
      },
    };
  } catch {
    if (!currentFeedbackSession(epoch)) return;
    settings.value = structuredClone(DEFAULT_FEEDBACK);
  }
  feedbackLoaded = true;
  clampLedModeToCaps();
  clampPresetToCaps();
}

async function refreshCapabilities(showLoading = false, epoch = feedbackEpoch): Promise<void> {
  const request = ++capabilitiesRequest;
  if (!currentFeedbackSession(epoch)) return;
  if (showLoading) capsLoading.value = true;
  try {
    const result = await gamepadFeedback.capabilities();
    if (!currentFeedbackSession(epoch) || request !== capabilitiesRequest) return;
    caps.value = result;
  } catch {
    if (!currentFeedbackSession(epoch) || request !== capabilitiesRequest) return;
    caps.value = { ledAvailable: false, brightnessAvailable: false, ledEffects: [], vibrationAvailable: false, physicalPrimarySlot: -1 };
  } finally {
    if (currentFeedbackSession(epoch) && request === capabilitiesRequest) capsLoading.value = false;
  }
  clampLedModeToCaps();
  clampPresetToCaps();
}

async function saveFeedback(): Promise<boolean> {
  if (!feedbackActive) return false;
  const epoch = feedbackEpoch, request = ++feedbackSaveRequest;
  feedbackSavePending = true;
  try {
    await saveSettingsSection('gamepad', { feedback: settings.value });
    if (currentFeedbackSession(epoch) && request === feedbackSaveRequest) feedbackSaveIssue.value = '';
    return true;
  } catch {
    // Ordinary lighting still applies best-effort. A request receipt cannot
    // confirm the separately failed settings write or silence its warning.
    if (currentFeedbackSession(epoch) && request === feedbackSaveRequest) feedbackSaveIssue.value = 'failed';
    return false;
  } finally {
    if (request === feedbackSaveRequest) feedbackSavePending = false;
  }
}

let applyTimer: number | undefined;
function scheduleLedApply(): void {
  if (!feedbackActive || !ledAvailable.value || ownershipBusy.value || oemLightingSelected.value) return;
  if (applyTimer !== undefined) window.clearTimeout(applyTimer);
  const epoch = feedbackEpoch;
  applyTimer = window.setTimeout(() => {
    applyTimer = undefined;
    if (currentFeedbackSession(epoch)) void applyLed();
  }, 250);
}

async function copyDiagnostics(): Promise<void> {
  const blob = {
    capabilities: caps.value,
    queryError: capsError.value || null,
    settings: settings.value,
    at: new Date().toISOString(),
  };
  try {
    await invoke('clipboard.writeText', { text: JSON.stringify(blob, null, 2) });
    status.value = '诊断 JSON 已复制到剪贴板，直接粘贴即可。';
  } catch (error) {
    status.value = `复制失败：${(error as Error).message}`;
  }
}
let ledApplyRequest = 0;
async function applyLed(): Promise<void> {
  if (!feedbackActive || !ledAvailable.value) return;
  const epoch = feedbackEpoch;
  const request = ++ledApplyRequest;
  const light = settings.value.lighting;
  if (oemLightingSelected.value && caps.value.oemRestoreAvailable !== true) {
    status.value = '本机暂无经确认的原厂灯光归还通路，已保留原厂选择；未应用缓存灯效。';
    return;
  }
  try {
    // HC 语义分层（对比 DevicePage / DynamicLightingManager）：
    //  - enabled（启用控制 / UseDynamicLightingToggle）：YMCC 是否接管灯光（关=整灯熄灭）
    //  - off（灯光亮度关闭开关 / LEDBrightness=0）：接管中把亮度归零，效果仍保持
    const result = await gamepadFeedback.setLed(oemLightingSelected.value ? { ownership: 'oem' } : {
      enabled: light.enabled && !light.off,
      mode: light.mode,
      presetId: light.mode === 'preset' ? light.presetId : undefined,
      color: light.color,
      color2: light.color2,
      speed: light.speed,
      brightness: light.enabled ? (light.off ? 0 : light.brightness) : 0,
    });
    if (request !== ledApplyRequest) return;
    if (!currentFeedbackSession(epoch)) return;
    status.value = oemLightingSelected.value
      ? result.ok && result.ownershipRequestCompleted === true
        ? '归还请求已完成，原厂灯光和充电显示仍需手动确认。'
        : `原厂灯光归还未完成：${result.reason ?? '回执未确认'}`
      : !result.ok
      ? `灯光应用失败：${result.reason ?? '未知原因'}`
      : result.physicalEffectVerified === true
        ? '掌机灯光已应用并取得实体确认。'
        : '灯光请求已完成，实体效果仍需手动确认。';
  } catch (error) {
    if (request !== ledApplyRequest) return;
    if (!currentFeedbackSession(epoch)) return;
    status.value = `灯光应用失败：${(error as Error).message}`;
  }
}

async function onOwnership(value: string | number): Promise<void> {
  if (!feedbackActive || ownershipBusy.value || !ayaFeedback.value) return;
  if (value !== 'ymcc' && value !== 'oem') return;
  if (value === 'oem' && caps.value.oemRestoreAvailable !== true) return;
  const epoch = feedbackEpoch, previous = settings.value.lighting.ownership;
  if (applyTimer !== undefined) { window.clearTimeout(applyTimer); applyTimer = undefined; }
  ++ledApplyRequest;
  settings.value.lighting.ownership = value;
  ownershipBusy.value = true;
  try {
    // Persist intent before applying. Unlike best-effort slider writes, failure
    // here must not claim a policy that startup/recovery cannot restore.
    if (!await saveFeedback()) throw new Error('feedback-save-unconfirmed');
    if (!currentFeedbackSession(epoch)) return;
    await applyLed();
  } catch {
    settings.value.lighting.ownership = previous;
    if (currentFeedbackSession(epoch)) status.value = '灯光来源保存失败，未发送本次切换请求，请手动重试。';
  } finally { ownershipBusy.value = false; }
}
async function onLightingChanged(): Promise<void> {
  if (!feedbackActive) return;
  const epoch = feedbackEpoch;
  await saveFeedback();
  if (currentFeedbackSession(epoch)) scheduleLedApply();
}
function onEnabled(value: boolean): void { settings.value.lighting.enabled = value; void onLightingChanged(); }
function onOff(value: boolean): void { settings.value.lighting.off = !value; void onLightingChanged(); } // B1-ext: 模型正向（开=!off）
function onPreset(value: string | number): void { settings.value.lighting.presetId = String(value); void onLightingChanged(); }
function onMode(value: string | number): void { settings.value.lighting.mode = String(value); void onLightingChanged(); }
function onColor(value: string | number): void { settings.value.lighting.color = String(value); void onLightingChanged(); }
function onColor2(value: string | number): void { settings.value.lighting.color2 = String(value); void onLightingChanged(); }
function onBrightness(value: number): void {
  settings.value.lighting.brightness = value;
  if (settings.value.lighting.off) settings.value.lighting.off = false; // 手动调亮度即恢复
  void onLightingChanged();
}
function onSpeed(value: number): void { settings.value.lighting.speed = value; void onLightingChanged(); }

const strengthRow = ref<HTMLElement | null>(null);
let strengthEditStart: number | undefined;
function beginStrengthEdit(): void {
  strengthEditStart ??= settings.value.vibrationStrength;
}
function cancelStrengthEdit(): void {
  if (strengthEditStart !== undefined) settings.value.vibrationStrength = strengthEditStart;
  strengthEditStart = undefined;
}
function commitStrength(): void {
  strengthEditStart = undefined;
  void saveFeedback();
}
function onStrength(value: number): void {
  settings.value.vibrationStrength = value;
}
function activateFeedback(): void {
  // KeepAlive calls activated after the first mount as well. Keep one initial
  // capability/settings flow and revoke its continuation whenever we leave.
  if (feedbackActive) return;
  feedbackActive = true;
  const epoch = ++feedbackEpoch;
  const initialize = !feedbackLoaded;
  void refreshCapabilities(true, epoch).then(async () => {
    if (!currentFeedbackSession(epoch)) return;
    if (initialize) await loadFeedback(epoch);
    if (!currentFeedbackSession(epoch)) return;
    if (initialize && oemLightingSelected.value && caps.value.oemRestoreAvailable !== true)
      status.value = '本机暂无经确认的原厂灯光归还通路，未应用缓存灯效。';
    if (initialize && ledAvailable.value && settings.value.lighting.enabled && !oemLightingSelected.value) void applyLed();
  });
}
function deactivateFeedback(): void {
  if (strengthRow.value) window.dispatchEvent(new CustomEvent('gp:slider-edit-invalidated', { detail: { panel: strengthRow.value, input: strengthRow.value.querySelector('input[type=range]') } }));
  cancelStrengthEdit();
  if (feedbackSavePending) feedbackSaveIssue.value = 'unconfirmed';
  feedbackSavePending = false;
  ++feedbackSaveRequest;
  feedbackActive = false;
  ++vibrationTestRequest;
  vibrationTestBusy.value = false;
  vibrationTestMessage.value = '';
  vibrationTestFailed.value = false;
  ++feedbackEpoch;
  ++ledApplyRequest;
  ++capabilitiesRequest;
  if (applyTimer !== undefined) {
    window.clearTimeout(applyTimer);
    applyTimer = undefined;
  }
}
onMounted(activateFeedback);
// Refresh once per page activation, never poll hidden controls. Returning to
// an interrupted initial load starts a fresh flow; old replies cannot write.
onActivated(activateFeedback);
onDeactivated(deactivateFeedback);
onBeforeUnmount(deactivateFeedback);

</script>

<template>
  <section class="card feedback-card">
    <div class="card-head">
      <div>
        <h3 class="card-title"><InlineIcon name="gamepad" /> 灯光与震动</h3>
        <p class="card-subtitle">掌机灯光按设备能力执行；震动强度为发送力度。</p>
      </div>
      <span class="state-chip" :class="{ on: ledAvailable || vibrationAvailable }">
        {{ ledAvailable || vibrationAvailable ? '接口可用' : '设备不可用' }}
      </span>
    </div>

    <div class="feedback-grid">
      <div class="feedback-bubble rumble-bubble">
        <div class="feedback-title-row">
          <div>
            <strong>震动</strong>
          </div>
          <span class="availability-chip" :class="vibrationAvailable && !vibrationTestFailed ? 'available' : 'unavailable'">
            {{ vibrationStateText }}
          </span>
        </div>
        <p v-if="!capsLoading && !vibrationAvailable && vibrationReasonText" class="unavailable-reason">{{ vibrationReasonText }}</p>
        <div ref="strengthRow" class="rumble-row" data-gp-slider-draft @gp:slider-edit-start="beginStrengthEdit" @gp:slider-edit-cancel="cancelStrengthEdit">
          <Slider
            :model-value="settings.vibrationStrength"
            :min="0"
            :max="100"
            :step="1"
            :repeat-interval-ms="EDIT_REPEAT_BASE_MS"
            label="震动强度"
            unit="%"
            :value-text="`${settings.vibrationStrength}%`"
            :disabled="!vibrationAvailable"
            @update:model-value="onStrength"
            @commit="commitStrength"
          />
          <button type="button" class="vibrate-button" :disabled="!vibrationAvailable || vibrationTestBusy" @click="triggerVibration">
            {{ vibrationTestBusy ? '测试中…' : '立即震动' }}
          </button>
        </div>
        <p v-if="vibrationTestMessage" class="vibration-test-receipt" :class="{ failed: vibrationTestFailed }" role="status" aria-live="polite">{{ vibrationTestMessage }}</p>
      </div>

      <div
        class="feedback-bubble light-bubble"
        :class="{ dimmed: !capsLoading && !ledAvailable }"
        :inert="!ledAvailable"
      >
        <div class="feedback-title-row">
          <div>
            <strong>手柄灯光</strong>
          </div>
          <span class="availability-chip" :class="ledAvailable ? 'available' : 'unavailable'">
            {{ capsLoading ? '加载中' : (ledAvailable ? '可用' : '暂不可用') }}
          </span>
        </div>
        <p v-if="!capsLoading && !ledAvailable && ledReasonText" class="unavailable-hint">{{ ledReasonText }}</p>
        <div v-if="ayaFeedback" class="setting-field ownership-field">
          <span class="field-label">灯光来源</span>
          <Dropdown :model-value="settings.lighting.ownership" :options="ownershipOptions"
            aria-label="灯光来源" :disabled="capsLoading || ownershipBusy || !ledAvailable"
            color="accent" @update:model-value="onOwnership" />
        </div>
        <p v-if="oemLightingSelected" class="unavailable-hint">
          {{ caps.sharedChargeIndicatorDeclared ? '本机手册声明摇杆灯兼作充电显示。' : '本机充电灯是否共用灯位尚未确认。' }}
          原厂显示由固件或官方前端决定，归还请求不代表显示已恢复。
        </p>
        <template v-if="!oemLightingSelected">
        <div class="light-switch">
          <!-- 启用控制 = HC UseDynamicLightingToggle（LEDSettingsEnabled）：YMCC
               是否实际接管灯光能力。可交互；关 = 整灯熄灭。读取中标签显示"加载中"。 -->
          <Toggle
            :model-value="settings.lighting.enabled"
            :label="capsLoading ? '加载中' : '启用控制'"
            :disabled="capsLoading || !ledAvailable || ownershipBusy"
            @update:model-value="onEnabled"
          />
        </div>
        <!-- 灯光亮度 = HC LEDBrightness 归零：接管中把亮度降为 0（灯灭），效果保持。
             B1-ext（2026-09-11）正向化：开关模型取反（开 = 亮度 > 0），
             label 改「灯光亮度」，onOff 内写 !value。 -->
        <div class="light-switch">
          <Toggle
            :model-value="!settings.lighting.off"
            label="灯光亮度"
            :disabled="capsLoading || !ledAvailable || ownershipBusy || !settings.lighting.enabled"
            @update:model-value="onOff"
          />
        </div>
        <template v-if="!capsLoading && ledAvailable">
          <div class="setting-field effect-field" :class="{ muted: !settings.lighting.enabled }">
            <span class="field-label">灯光效果</span>
            <Dropdown
              :model-value="settings.lighting.mode"
              :options="effectOptions"
              :disabled="ownershipBusy || !settings.lighting.enabled"
              color="accent"
              @update:model-value="onMode"
            />
          </div>
          <div v-if="settings.lighting.mode === 'preset'" class="setting-field preset-field" :class="{ muted: !settings.lighting.enabled }">
            <span class="field-label">灯光预设</span>
            <Dropdown :model-value="settings.lighting.presetId" :options="presetOptions" :disabled="ownershipBusy || !settings.lighting.enabled" color="accent" @update:model-value="onPreset" />
          </div>
          <p v-if="caps.presetDeclared && caps.presetImplemented === false" class="unavailable-hint">此机型的灯光预设暂不可用。</p>
          <div v-if="settings.lighting.mode !== 'preset'" class="setting-field color-field" :class="{ muted: !settings.lighting.enabled }">
            <span class="field-label">{{ splitColorEffect ? '左侧颜色' : '主颜色' }}</span>
            <Dropdown
              :model-value="settings.lighting.color"
              :options="colorOptions"
              :disabled="ownershipBusy || !settings.lighting.enabled"
              color="accent"
              @update:model-value="onColor"
            />
          </div>
          <div v-if="(supportsSecondColor && settings.lighting.mode !== 'preset') || presetSecondColor" class="setting-field color-field" :class="{ muted: !settings.lighting.enabled }">
            <span class="field-label">{{ splitColorEffect ? '右侧颜色' : presetSecondColor ? '标志与功能键颜色' : '副颜色' }}</span>
            <Dropdown
              :model-value="settings.lighting.color2"
              :options="colorOptions"
              :disabled="ownershipBusy || !settings.lighting.enabled"
              color="accent"
              @update:model-value="onColor2"
            />
          </div>
          <p v-if="splitColorEffect" class="unavailable-hint lighting-effect-hint">当前氛围为左右分区固定配色，可分别调整；不会随屏幕或时间自动变色。</p>
          <p v-if="caps.lightingFixedSpeed" class="unavailable-hint">本机灯效速度由固件固定。</p>
          <div class="light-slider-grid">
            <Slider
              v-if="caps.brightnessAvailable"
              :model-value="settings.lighting.brightness"
              :min="0"
              :max="100"
              :step="1"
              label="亮度"
              unit="%"
              :value-text="`${settings.lighting.brightness}%`"
              :disabled="ownershipBusy || !settings.lighting.enabled || settings.lighting.off"
              @update:model-value="onBrightness"
            />
            <Slider
              :model-value="settings.lighting.speed"
              :min="0"
              :max="100"
              :step="1"
              label="速度"
              unit="%"
              :value-text="`${settings.lighting.speed}%`"
              :disabled="ownershipBusy || !settings.lighting.enabled || !speedRelevant"
              @update:model-value="onSpeed"
            />
          </div>
        </template>
        </template>
      </div>
    </div>
    <p v-if="feedbackSaveMessage" class="feedback-save-receipt" role="status" aria-live="polite">{{ feedbackSaveMessage }}</p>
    <p v-if="status" class="feedback-status" role="status" aria-live="polite">{{ status }}</p>
  </section>
</template>

<style scoped>
.card-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px}.card-head-actions{flex:0 0 auto;display:inline-flex;align-items:center;gap:8px}.copy-button{min-height:24px;padding:0 8px;border:1px solid rgba(255,255,255,.12);border-radius:999px;background:color-mix(in srgb,var(--bg-input) 86%,transparent);color:var(--text-dim);font-size:10px;font-weight:700;cursor:pointer}.copy-button:hover{color:var(--accent);border-color:color-mix(in srgb,var(--accent) 45%,transparent)}.card-title{margin:0}.card-subtitle{margin:4px 0 0;color:var(--text-dim);font-size:11px;line-height:1.45}.state-chip,.availability-chip{display:inline-flex;align-items:center;justify-content:center;border-radius:999px;font-weight:700}.state-chip{flex:0 0 auto;min-height:24px;padding:0 8px;border:1px solid rgba(255,255,255,.12);background:color-mix(in srgb,var(--bg-input) 86%,transparent);color:#f4c76b;font-size:10px}.state-chip.on{border-color:color-mix(in srgb,var(--accent) 58%,transparent);background:color-mix(in srgb,var(--accent) 14%,transparent);color:var(--accent)}.feedback-grid{display:grid;grid-template-columns:1fr;gap:8px;margin-top:12px}.feedback-bubble{border:1px solid rgba(255,255,255,.07);border-radius:var(--radius-ctrl);background:color-mix(in srgb,var(--bg-input) 88%,transparent);min-width:0;padding:10px}.feedback-bubble.dimmed{opacity:.45;pointer-events:none}.feedback-title-row{display:flex;align-items:flex-start;justify-content:space-between;gap:7px}.feedback-title-row strong{font-size:12px}.feedback-title-row p{margin:3px 0 0;color:var(--text-dim);font-size:10px;line-height:1.4}.availability-chip{flex:0 0 auto;min-height:19px;padding:0 6px;border:1px solid rgba(255,255,255,.1);color:var(--text-dim);font-size:9px}.availability-chip.available{border-color:color-mix(in srgb,var(--accent) 58%,transparent);color:var(--accent)}.availability-chip.unavailable{border-color:color-mix(in srgb,#f4c76b 45%,transparent);color:#f4c76b}.unavailable-reason{margin:8px 0 0;padding:6px 8px;border:1px solid rgba(244,199,107,.3);border-radius:calc(var(--radius-ctrl) - 2px);background:color-mix(in srgb,#f4c76b 8%,transparent);color:#f4c76b;font-size:10px;line-height:1.4}.unavailable-hint{margin:8px 0 0;color:var(--text-dim);font-size:10px;line-height:1.4}.sidebar-rc{border-radius:var(--radius-ctrl)}.rumble-row{display:flex;align-items:flex-end;gap:10px;margin-top:11px}.rumble-row :deep(.slider){flex:1 1 0;min-width:0;margin-top:0}.rumble-row :deep(.slider-label),.light-bubble :deep(.slider-label){font-size:10px}.rumble-row :deep(.slider-val),.light-bubble :deep(.slider-val){font-size:11px}.vibrate-button{flex:0 0 auto;min-height:32px;display:inline-flex;align-items:center;justify-content:center;gap:6px;padding:0 14px;border:1px solid color-mix(in srgb,var(--accent) 55%,transparent);border-radius:var(--radius-ctrl);background:color-mix(in srgb,var(--accent) 12%,transparent);color:var(--accent);font-size:11px;font-weight:700;cursor:pointer}.vibrate-button:disabled{opacity:.45;cursor:not-allowed}.vibrate-button:hover:not(:disabled){background:color-mix(in srgb,var(--accent) 20%,transparent)}.light-switch{margin-top:9px}.light-switch :deep(.toggle-row){align-items:flex-start}.setting-field{margin-top:8px}.field-label{display:block;margin-bottom:5px;color:var(--text-dim);font-size:10px}.setting-field.muted{opacity:.55}.effect-field :deep(.seg){min-height:29px}.effect-field :deep(.seg-btn){min-height:25px;padding:4px 3px;font-size:10px}.light-slider-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;margin-top:8px}.light-slider-grid :deep(.slider-head){margin-bottom:5px}.light-slider-grid :deep(.slider-track-wrap){height:17px}
.vibration-test-receipt{margin:8px 0 0;color:var(--text-dim);font-size:10px;line-height:1.5;overflow-wrap:anywhere}.vibration-test-receipt.failed{color:#f4c76b}
.feedback-save-receipt{margin:8px 0 0;color:#f4c76b;font-size:10px;line-height:1.5;overflow-wrap:anywhere}
</style>
