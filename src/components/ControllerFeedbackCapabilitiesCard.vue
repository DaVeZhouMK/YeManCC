<script setup lang="ts">
import { computed, onActivated, onMounted, ref } from 'vue';
import InlineIcon from '@/components/InlineIcon.vue';
import Dropdown from '@/components/Dropdown.vue';
import Slider from '@/components/Slider.vue';
import Toggle from '@/components/Toggle.vue';
import { gamepadFeedback, type LedFeedbackCapabilities } from '@/bridge/api';
import { invoke } from '@/bridge/ipc';
import { loadSettings, saveSettingsSection } from '@/bridge/settingsRepository';

interface LightingSettings {
  enabled: boolean;
  off: boolean;
  mode: string;
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
  lighting: { enabled: false, off: false, mode: 'solid', color: '#2ea6ff', color2: '#9d7cff', speed: 55, brightness: 65 },
};

const settings = ref<FeedbackSettings>(structuredClone(DEFAULT_FEEDBACK));
const caps = ref<LedFeedbackCapabilities>({
  ledAvailable: false,
  brightnessAvailable: false,
  ledEffects: [],
  vibrationAvailable: false,
  physicalPrimarySlot: -1,
});
const capsError = ref('');
const status = ref('');
// 能力探测进行中（首帧 capabilites 未返回）：灯光块显示"加载中"，控件禁用。
const capsLoading = ref(true);
const ledAvailable = computed(() => caps.value.ledAvailable);
const vibrationAvailable = computed(() => caps.value.vibrationAvailable);
// 卡片与两个 bubble 始终可见；HC DevicePage 能力门（IsDynamicLightingSupported）
// 决定灯光控件是否可用，而非整卡隐藏。加载中显示"加载中"。
// HC DeviceCapabilities.DynamicLightingSecondLEDColor：ROG 无此位 → 隐藏副颜色下拉。
const supportsSecondColor = computed(() => caps.value.supportsSecondColor === true);
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
  ambilight: '氛围',
  gradient: '渐变',
  preset: '预设',
};
const effectOptions = computed(() =>
  caps.value.ledEffects
    .filter((effect) => effectLabels[effect])
    .map((effect) => ({ value: effect, label: effectLabels[effect]! })));
const speedRelevant = computed(() => !['solid', 'ambilight'].includes(settings.value.lighting.mode));
// 颜色预设：HC 用开放式 ColorPicker（任意色），YMCC 以常用色下拉近似。
// 主/副颜色均由 native 能力过滤（supportsSecondColor 才显示副颜色）。
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
  const supported = caps.value.ledEffects;
  if (supported.length === 0 || supported.includes(settings.value.lighting.mode)) return;
  const fallback = supported[0];
  if (fallback) settings.value.lighting.mode = fallback;
}

async function loadFeedback(): Promise<void> {
  try {
    const all = await loadSettings();
    const raw = all.gamepad?.feedback as Partial<FeedbackSettings> | undefined;
    const lighting = raw?.lighting as Partial<LightingSettings> | undefined;
    settings.value = {
      vibrationStrength: clampInt(raw?.vibrationStrength, DEFAULT_FEEDBACK.vibrationStrength, 0, 100),
      lighting: {
        enabled: lighting?.enabled === true,
        off: lighting?.off === true,
        mode: typeof lighting?.mode === 'string' && effectLabels[lighting.mode] ? lighting.mode : DEFAULT_FEEDBACK.lighting.mode,
        color: validColor(lighting?.color ?? DEFAULT_FEEDBACK.lighting.color),
        color2: validColor(lighting?.color2 ?? DEFAULT_FEEDBACK.lighting.color2),
        speed: clampInt(lighting?.speed, DEFAULT_FEEDBACK.lighting.speed, 0, 100),
        brightness: clampInt(lighting?.brightness, DEFAULT_FEEDBACK.lighting.brightness, 0, 100),
      },
    };
  } catch {
    settings.value = structuredClone(DEFAULT_FEEDBACK);
  }
  clampLedModeToCaps();
}

async function refreshCapabilities(showLoading = false): Promise<void> {
  if (showLoading) capsLoading.value = true;
  try {
    caps.value = await gamepadFeedback.capabilities();
  } catch {
    caps.value = { ledAvailable: false, brightnessAvailable: false, ledEffects: [], vibrationAvailable: false, physicalPrimarySlot: -1 };
  } finally {
    capsLoading.value = false;
  }
  clampLedModeToCaps();
}

async function saveFeedback(): Promise<void> {
  try {
    await saveSettingsSection('gamepad', { feedback: settings.value });
  } catch {
    // Durable write is best-effort; an apply failure is surfaced separately.
  }
}

let applyTimer: number | undefined;
function scheduleLedApply(): void {
  if (!ledAvailable.value) return;
  if (applyTimer !== undefined) window.clearTimeout(applyTimer);
  applyTimer = window.setTimeout(() => { void applyLed(); }, 250);
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
async function applyLed(): Promise<void> {
  const light = settings.value.lighting;
  try {
    // HC 语义分层（对比 DevicePage / DynamicLightingManager）：
    //  - enabled（启用控制 / UseDynamicLightingToggle）：YMCC 是否接管灯光（关=整灯熄灭）
    //  - off（灯光亮度关闭开关 / LEDBrightness=0）：接管中把亮度归零，效果仍保持
    const result = await gamepadFeedback.setLed({
      enabled: light.enabled && !light.off,
      mode: light.mode,
      color: light.color,
      color2: light.color2,
      speed: light.speed,
      brightness: light.enabled ? (light.off ? 0 : light.brightness) : 0,
    });
    status.value = result.ok ? '掌机灯光已应用。' : `灯光应用失败：${result.reason ?? '未知原因'}`;
  } catch (error) {
    status.value = `灯光应用失败：${(error as Error).message}`;
  }
}

async function onLightingChanged(): Promise<void> {
  await saveFeedback();
  scheduleLedApply();
}
function onEnabled(value: boolean): void { settings.value.lighting.enabled = value; void onLightingChanged(); }
function onOff(value: boolean): void { settings.value.lighting.off = !value; void onLightingChanged(); } // B1-ext: 模型正向（开=!off）
function onMode(value: string | number): void { settings.value.lighting.mode = String(value); void onLightingChanged(); }
function onColor(value: string | number): void { settings.value.lighting.color = String(value); void onLightingChanged(); }
function onColor2(value: string | number): void { settings.value.lighting.color2 = String(value); void onLightingChanged(); }
function onBrightness(value: number): void {
  settings.value.lighting.brightness = value;
  if (settings.value.lighting.off) settings.value.lighting.off = false; // 手动调亮度即恢复
  void onLightingChanged();
}
function onSpeed(value: number): void { settings.value.lighting.speed = value; void onLightingChanged(); }

async function onStrength(value: number): Promise<void> {
  settings.value.vibrationStrength = value;
  await saveFeedback();
}
async function triggerVibration(): Promise<void> {
  if (!vibrationAvailable.value) return;
  try {
    // 双脉冲：HC Rumble 一次按下也会以默认延迟连续两次脉冲；
    // 这里显式连发两次，中间隔 125ms（HC Rumble 默认脉冲时长）。
    await gamepadFeedback.vibrate(settings.value.vibrationStrength);
    await new Promise((resolve) => window.setTimeout(resolve, 125));
    await gamepadFeedback.vibrate(settings.value.vibrationStrength);
  } catch {
    // 诊断静默；UI 不再显示状态小字。
  }
}

onMounted(() => {
  void refreshCapabilities(true).then(() => {
    void loadFeedback().then(() => {
      if (ledAvailable.value && settings.value.lighting.enabled) void applyLed();
    });
  });
});
// 能力在打开 YMCC 时识别一次即可（挂载/重新激活），后台不持续轮询刷新，
// 避免灯光/震动切片每 2 秒重绘导致"还在刷新"的观感。
onActivated(() => {
  void refreshCapabilities(true);
});
</script>

<template>
  <section class="card feedback-card">
    <div class="card-head">
      <div>
        <h3 class="card-title"><InlineIcon name="gamepad" /> 灯光与震动</h3>
        <p class="card-subtitle">掌机灯光按设备能力执行；震动强度为发送力度。</p>
      </div>
      <span class="state-chip" :class="{ on: ledAvailable || vibrationAvailable }">
        {{ ledAvailable || vibrationAvailable ? '已就绪' : '设备不可用' }}
      </span>
    </div>

    <div class="feedback-grid">
      <div class="feedback-bubble rumble-bubble">
        <div class="feedback-title-row">
          <div>
            <strong>震动</strong>
          </div>
          <span class="availability-chip" :class="vibrationAvailable ? 'available' : 'unavailable'">
            {{ capsLoading ? '加载中' : (vibrationAvailable ? '可用' : '暂不可用') }}
          </span>
        </div>
        <p v-if="!capsLoading && !vibrationAvailable && vibrationReasonText" class="unavailable-reason">{{ vibrationReasonText }}</p>
        <div class="rumble-row">
          <Slider
            :model-value="settings.vibrationStrength"
            :min="0"
            :max="100"
            :step="1"
            label="震动强度"
            unit="%"
            :value-text="`${settings.vibrationStrength}%`"
            :disabled="!vibrationAvailable"
            @update:model-value="onStrength"
          />
          <button type="button" class="vibrate-button" :disabled="!vibrationAvailable" @click="triggerVibration">
            立即震动
          </button>
        </div>
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
        <div class="light-switch">
          <!-- 启用控制 = HC UseDynamicLightingToggle（LEDSettingsEnabled）：YMCC
               是否实际接管灯光能力。可交互；关 = 整灯熄灭。读取中标签显示"加载中"。 -->
          <Toggle
            :model-value="settings.lighting.enabled"
            :label="capsLoading ? '加载中' : '启用控制'"
            :disabled="capsLoading || !ledAvailable"
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
            :disabled="capsLoading || !ledAvailable || !settings.lighting.enabled"
            @update:model-value="onOff"
          />
        </div>
        <template v-if="!capsLoading && ledAvailable">
          <div class="setting-field effect-field" :class="{ muted: !settings.lighting.enabled }">
            <span class="field-label">灯光效果</span>
            <Dropdown
              :model-value="settings.lighting.mode"
              :options="effectOptions"
              :disabled="!settings.lighting.enabled"
              color="accent"
              @update:model-value="onMode"
            />
          </div>
          <div class="setting-field color-field" :class="{ muted: !settings.lighting.enabled }">
            <span class="field-label">主颜色</span>
            <Dropdown
              :model-value="settings.lighting.color"
              :options="colorOptions"
              :disabled="!settings.lighting.enabled"
              color="accent"
              @update:model-value="onColor"
            />
          </div>
          <div v-if="supportsSecondColor" class="setting-field color-field" :class="{ muted: !settings.lighting.enabled }">
            <span class="field-label">副颜色</span>
            <Dropdown
              :model-value="settings.lighting.color2"
              :options="colorOptions"
              :disabled="!settings.lighting.enabled"
              color="accent"
              @update:model-value="onColor2"
            />
          </div>
          <div class="light-slider-grid">
            <Slider
              :model-value="settings.lighting.brightness"
              :min="0"
              :max="100"
              :step="1"
              label="亮度"
              unit="%"
              :value-text="`${settings.lighting.brightness}%`"
              :disabled="!settings.lighting.enabled || settings.lighting.off"
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
              :disabled="!settings.lighting.enabled || !speedRelevant"
              @update:model-value="onSpeed"
            />
          </div>
        </template>
      </div>
    </div>
  </section>
</template>

<style scoped>
.card-head{display:flex;align-items:flex-start;justify-content:space-between;gap:12px}.card-head-actions{flex:0 0 auto;display:inline-flex;align-items:center;gap:8px}.copy-button{min-height:24px;padding:0 8px;border:1px solid rgba(255,255,255,.12);border-radius:999px;background:color-mix(in srgb,var(--bg-input) 86%,transparent);color:var(--text-dim);font-size:10px;font-weight:700;cursor:pointer}.copy-button:hover{color:var(--accent);border-color:color-mix(in srgb,var(--accent) 45%,transparent)}.card-title{margin:0}.card-subtitle{margin:4px 0 0;color:var(--text-dim);font-size:11px;line-height:1.45}.state-chip,.availability-chip{display:inline-flex;align-items:center;justify-content:center;border-radius:999px;font-weight:700}.state-chip{flex:0 0 auto;min-height:24px;padding:0 8px;border:1px solid rgba(255,255,255,.12);background:color-mix(in srgb,var(--bg-input) 86%,transparent);color:#f4c76b;font-size:10px}.state-chip.on{border-color:color-mix(in srgb,var(--accent) 58%,transparent);background:color-mix(in srgb,var(--accent) 14%,transparent);color:var(--accent)}.feedback-grid{display:grid;grid-template-columns:1fr;gap:8px;margin-top:12px}.feedback-bubble{border:1px solid rgba(255,255,255,.07);border-radius:var(--radius-ctrl);background:color-mix(in srgb,var(--bg-input) 88%,transparent);min-width:0;padding:10px}.feedback-bubble.dimmed{opacity:.45;pointer-events:none}.feedback-title-row{display:flex;align-items:flex-start;justify-content:space-between;gap:7px}.feedback-title-row strong{font-size:12px}.feedback-title-row p{margin:3px 0 0;color:var(--text-dim);font-size:10px;line-height:1.4}.availability-chip{flex:0 0 auto;min-height:19px;padding:0 6px;border:1px solid rgba(255,255,255,.1);color:var(--text-dim);font-size:9px}.availability-chip.available{border-color:color-mix(in srgb,var(--accent) 58%,transparent);color:var(--accent)}.availability-chip.unavailable{border-color:color-mix(in srgb,#f4c76b 45%,transparent);color:#f4c76b}.unavailable-reason{margin:8px 0 0;padding:6px 8px;border:1px solid rgba(244,199,107,.3);border-radius:calc(var(--radius-ctrl) - 2px);background:color-mix(in srgb,#f4c76b 8%,transparent);color:#f4c76b;font-size:10px;line-height:1.4}.unavailable-hint{margin:8px 0 0;color:var(--text-dim);font-size:10px;line-height:1.4}.sidebar-rc{border-radius:var(--radius-ctrl)}.rumble-row{display:flex;align-items:flex-end;gap:10px;margin-top:11px}.rumble-row :deep(.slider){flex:1 1 0;min-width:0;margin-top:0}.rumble-row :deep(.slider-label),.light-bubble :deep(.slider-label){font-size:10px}.rumble-row :deep(.slider-val),.light-bubble :deep(.slider-val){font-size:11px}.vibrate-button{flex:0 0 auto;min-height:32px;display:inline-flex;align-items:center;justify-content:center;gap:6px;padding:0 14px;border:1px solid color-mix(in srgb,var(--accent) 55%,transparent);border-radius:var(--radius-ctrl);background:color-mix(in srgb,var(--accent) 12%,transparent);color:var(--accent);font-size:11px;font-weight:700;cursor:pointer}.vibrate-button:disabled{opacity:.45;cursor:not-allowed}.vibrate-button:hover:not(:disabled){background:color-mix(in srgb,var(--accent) 20%,transparent)}.light-switch{margin-top:9px}.light-switch :deep(.toggle-row){align-items:flex-start}.setting-field{margin-top:8px}.field-label{display:block;margin-bottom:5px;color:var(--text-dim);font-size:10px}.setting-field.muted{opacity:.55}.effect-field :deep(.seg){min-height:29px}.effect-field :deep(.seg-btn){min-height:25px;padding:4px 3px;font-size:10px}.light-slider-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;margin-top:8px}.light-slider-grid :deep(.slider-head){margin-bottom:5px}.light-slider-grid :deep(.slider-track-wrap){height:17px}
</style>