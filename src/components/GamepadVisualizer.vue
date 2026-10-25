<script setup lang="ts">
import { ref, shallowRef, onMounted, onBeforeUnmount, onActivated, onDeactivated, onBeforeUpdate, onUpdated, computed, watch } from 'vue';
import { on as onIpc } from '@/bridge/ipc';
import { isUiVisible, onUiVisibilityChange } from '@/bridge/uiLifecycle';
import { createGamepadPresenter, EMPTY_GAMEPAD_PRESENTATION } from '@/bridge/gamepadPresentation';
import { createPressFeedback } from '@/bridge/controllerPressFeedback';
import { createControllerPreviewTrace } from '@/robust/controllerPreviewTrace';
import ControllerDeviceDiagram from '@/components/ControllerDeviceDiagram.vue';
import { CONTROLLER_DIAGRAMS, controllerDiagramPersona } from '@/bridge/controllerDeviceDiagram';
import { controllerOemDisplayCatalog } from '@/bridge/controllerOemPresentation';

import Toggle from '@/components/Toggle.vue';
import type { GamepadSettings } from '@/bridge/yeman';
import { OEM_EMPTY_SNAPSHOT, type OemKeysSnapshot, type OemSpecialKey } from '@/bridge/controllerShortcutRules';

const props = withDefaults(
  defineProps<{
    settings: GamepadSettings;
    persona?: string;
    highlightButtons?: number[];
    oemKeys?: OemKeysSnapshot;
    highlightOemKeys?: string[];
    /** Hide the optional live test-mode switch in compact/editor mirrors. */
    showTestMode?: boolean;
    minimumPressFeedback?: boolean;
    /** Use the Steam Deck height in the controller settings card only. */
    matchSteamDeckHeight?: boolean;
  }>(),
  { persona: 'xbox', highlightButtons: () => [], oemKeys: () => OEM_EMPTY_SNAPSHOT, highlightOemKeys: () => [], showTestMode: true, minimumPressFeedback: false, matchSteamDeckHeight: false }
);

// Bounded observation only: no input gate or render pacing lives here.
const previewRoot = ref<HTMLElement | null>(null);
let previewTrace: ReturnType<typeof createControllerPreviewTrace> | null = null;
let previewObserver: IntersectionObserver | null = null;
let previewIntersects = false;
function stopPreviewTrace(): void {
  const trace = previewTrace;
  previewTrace = null;
  try { previewObserver?.disconnect(); } catch { /* Observation cannot affect controls. */ }
  previewObserver = null;
  previewIntersects = false;
  try { trace?.dispose(); } catch { /* Original input/presenter lifecycle continues. */ }
}
function observePreview(phase: 'receipt' | 'presented' | 'willPatch' | 'didPatch', payload?: unknown): void {
  try {
    if (!previewTrace) return;
    if (phase === 'receipt') previewTrace.receipt(payload);
    else previewTrace[phase]();
  } catch { stopPreviewTrace(); }
}
function syncPreviewTrace(): void {
  try { previewTrace?.setVisible(viewActive && isUiVisible() && previewIntersects); }
  catch { stopPreviewTrace(); }
}

// Independent shallow refs keep axis-only changes out of button/name consumers.
// The presenter preserves normalized array identity and avoids deep proxy churn.
const live = shallowRef(EMPTY_GAMEPAD_PRESENTATION.buttons);
const axes = shallowRef(EMPTY_GAMEPAD_PRESENTATION.axes);
const oemPressedKeys = shallowRef(EMPTY_GAMEPAD_PRESENTATION.oemPressedKeys);
const connected = ref(false);
const padName = ref('');
const testMode = ref(false);
const presenter = createGamepadPresenter((state) => {
  connected.value = state.connected;
  padName.value = state.name;
  live.value = state.buttons;
  axes.value = state.axes;
  oemPressedKeys.value = state.oemPressedKeys;
  observePreview('presented');
});
const pulseKeys = shallowRef<readonly string[]>([]);
const pressFeedback = createPressFeedback(keys => { pulseKeys.value = keys; });
const diagramButtons = computed(() => props.minimumPressFeedback ? Array.from({length:Math.max(live.value.length,18)},(_,i)=>Boolean(live.value[i])||pulseKeys.value.includes('button:'+i)) : live.value);
const diagramOemKeys = computed(() => props.minimumPressFeedback ? [...new Set([...oemPressedKeys.value,...pulseKeys.value.filter(key=>key.startsWith('oem:')).map(key=>key.slice(4))])] : oemPressedKeys.value);
let viewActive = true;
let stopState: (() => void) | null = null;
let stopVisibility: (() => void) | null = null;
let testBTimer: number | null = null;
let testBHeld = false;
// The native stream remains authoritative. Only display work pauses when hidden.
const baseImageRevision = ref(0);
let baseImageRetryTimer: number | null = null;
let stopResumeReady: (() => void) | null = null;
let stopResumed: (() => void) | null = null;
let imageRefreshPending = false;
let imageFailures = 0;
const IMAGE_RETRY_LIMIT = 3;

const baseImageUrl = computed(() => `/gamepad-base.png?wake=${baseImageRevision.value}`);

function cancelImageRetry(): void {
  if (baseImageRetryTimer !== null) window.clearTimeout(baseImageRetryTimer);
  baseImageRetryTimer = null;
}

function refreshBaseImage(): void {
  cancelImageRetry();
  imageFailures = 0;
  if (!viewActive || !isUiVisible()) { imageRefreshPending = true; return; }
  imageRefreshPending = false;
  baseImageRevision.value += 1;
  // Preserve the existing bounded second load after the compositor resumes.
  baseImageRetryTimer = window.setTimeout(() => {
    baseImageRetryTimer = null;
    if (!viewActive || !isUiVisible()) { imageRefreshPending = true; return; }
    baseImageRevision.value += 1;
  }, 250);
}

function onBaseImageError(): void {
  cancelImageRetry();
  if (!viewActive || !isUiVisible()) { imageRefreshPending = true; return; }
  // A missing/failed image must not create a permanent 120ms render/network loop.
  if (imageFailures >= IMAGE_RETRY_LIMIT) return;
  const delay = 120 * 2 ** imageFailures++;
  baseImageRetryTimer = window.setTimeout(() => {
    baseImageRetryTimer = null;
    if (!viewActive || !isUiVisible()) { imageRefreshPending = true; return; }
    baseImageRevision.value += 1;
  }, delay);
}

function syncPresentationVisibility(): void {
  const visible = viewActive && isUiVisible();
  presenter.setVisible(visible);
  syncPreviewTrace();
  if (!visible) {
    if (baseImageRetryTimer !== null) imageRefreshPending = true;
    cancelImageRetry();
  } else if (imageRefreshPending) refreshBaseImage();
}

function syncTestBHold(): void {
  if (!testMode.value || !testBHeld) {
    if (testBTimer !== null) window.clearTimeout(testBTimer);
    testBTimer = null;
    return;
  }
  if (testBTimer !== null) return;
  testBTimer = window.setTimeout(() => {
    testBTimer = null;
    if (testMode.value && testBHeld) setTestMode(false);
  }, 3000);
}

function setTestMode(v: boolean): void {
  testMode.value = v;
  syncTestBHold();
  window.dispatchEvent(new CustomEvent('ipc:gamepad.testmode', { detail: v }));
}

function onTestModeEvent(e: Event) {
  testMode.value = Boolean((e as CustomEvent<boolean>).detail);
  syncTestBHold();
}

onBeforeUpdate(() => observePreview('willPatch'));
onUpdated(() => observePreview('didPatch'));
watch(() => props.persona, () => {
  try { previewTrace?.personaChanged(); } catch { stopPreviewTrace(); }
});
onMounted(() => {
  try {
    previewTrace = createControllerPreviewTrace({
      displayPersona: () => controllerDiagramPersona(props.persona),
      visible: () => viewActive && isUiVisible() && previewIntersects,
    });
    if (typeof IntersectionObserver === 'function' && previewRoot.value) {
      previewObserver = new IntersectionObserver(entries => {
        if (!previewTrace) return;
        for (const entry of entries) if (entry.target === previewRoot.value) previewIntersects = entry.isIntersecting;
        syncPreviewTrace();
      });
      previewObserver.observe(previewRoot.value);
    }
  } catch { stopPreviewTrace(); }
  syncPresentationVisibility();
  window.addEventListener('ipc:gamepad.testmode', onTestModeEvent);
  stopVisibility = onUiVisibilityChange(syncPresentationVisibility);
  stopState = onIpc('gamepad.state', (payload) => {
    observePreview('receipt', payload);
    const raw = payload as { connected?: unknown; buttons?: unknown } | null;
    // The safety/long-press path still observes EVERY snapshot while hidden.
    // Axis-only changes must not restart a held-B deadline.
    testBHeld = Boolean(Array.isArray(raw?.buttons) && raw.buttons[1]);
    if (!raw?.connected && testMode.value) setTestMode(false);
    syncTestBHold();
    if(props.minimumPressFeedback){const state=payload as {connected?:boolean;buttons?:boolean[];oemPressedKeys?:string[]};if(state.connected){pressFeedback.accept([...(state.buttons||[]).flatMap((down,i)=>down?['button:'+i]:[]),...(state.oemPressedKeys||[]).filter(key=>props.oemKeys.keys.some(k=>k.keyId===key&&k.captureImplemented!==false)).map(key=>'oem:'+key)])}else pressFeedback.reset()}
    presenter.accept(payload);
  });
  stopResumeReady = onIpc('power.resume-ready', refreshBaseImage);
  stopResumed = onIpc('power.resumed', refreshBaseImage);
});
onActivated(() => { viewActive = true; syncPresentationVisibility(); });
onDeactivated(() => { viewActive = false; syncPresentationVisibility(); });
onBeforeUnmount(() => {
  stopPreviewTrace();
  pressFeedback.reset();
  presenter.dispose();
  stopVisibility?.();
  stopVisibility = null;
  cancelImageRetry();
  window.removeEventListener('ipc:gamepad.testmode', onTestModeEvent);
  stopState?.();
  stopState = null;
  stopResumeReady?.();
  stopResumeReady = null;
  stopResumed?.();
  stopResumed = null;
  if (testBTimer !== null) window.clearTimeout(testBTimer);
  testBTimer = null;
  setTestMode(false);
});
// Native/XInput Y is positive upward; SVG Y is positive downward.
// Invert only the preview coordinate, retaining input precision and real output.
const STICK_L = { x: 116, y: 138 };
const STICK_R = { x: 255, y: 191 };
const STICK_TRAVEL = 10;
const stickL = computed(() => ({
  x: STICK_L.x + (axes.value[0] || 0) * STICK_TRAVEL,
  y: STICK_L.y - (axes.value[1] || 0) * STICK_TRAVEL,
}));
const stickR = computed(() => ({
  x: STICK_R.x + (axes.value[2] || 0) * STICK_TRAVEL,
  y: STICK_R.y - (axes.value[3] || 0) * STICK_TRAVEL,
}));
const stickLPushed = computed(() => Math.hypot(axes.value[0] || 0, axes.value[1] || 0) > 0.45);
const stickRPushed = computed(() => Math.hypot(axes.value[2] || 0, axes.value[3] || 0) > 0.45);

function isMapped(i: number): boolean {
  // 高亮仅跟随「设置页当前选中的快捷键」：平时不亮起；点选即亮起对应按键并持续显示；切换选中项即切换显示
  return props.highlightButtons.includes(i);
}
function isLive(i: number): boolean {
  return !!live.value[i];
}
function cls(i: number): Record<string, boolean> {
  return { live: isLive(i), mapped: isMapped(i) };
}
const displayOemKeys = computed(() => controllerOemDisplayCatalog(props.persona, props.oemKeys));
const catalogKeys = computed(() => displayOemKeys.value.supported ? displayOemKeys.value.keys : []);
// Legion Go's two additional rear switches have no 1..4 slot in the existing
// catalog. Keep their canonical IDs/names in the rear group, never among front keys.
function isAdditionalBackKey(key: OemSpecialKey): boolean {
  return props.oemKeys.familyId === 'lenovo-legion-go' && key.backIndex === null &&
    (key.keyId === 'legionl' || key.keyId === 'legionm2');
}
const specialKeys = computed(() => catalogKeys.value.filter(key => key.backIndex === null && !isAdditionalBackKey(key)));
const diagramPersona = computed(() => controllerDiagramPersona(props.persona));
// Keep every model at the existing Steam Deck height, with its own aspect ratio.
const previewFrameStyle = computed(() => {
  const reference = CONTROLLER_DIAGRAMS.steamdeck;
  const model = CONTROLLER_DIAGRAMS[diagramPersona.value];
  const ratio = reference.width / reference.height;
  return { '--preview-reference-ratio': String(ratio), '--preview-device-scale': String(model.width / model.height / ratio) };
});
const exteriorSpecialKeys = specialKeys;
const additionalBackKeys = computed(() => catalogKeys.value.filter(isAdditionalBackKey));
const backPositions = [
  { x: 92, y: 229, right: false }, { x: 328, y: 229, right: true },
  { x: 81, y: 264, right: false }, { x: 339, y: 264, right: true },
];
const backKeys = computed(() => catalogKeys.value
  .filter(key => key.backIndex !== null && key.backIndex >= 1 && key.backIndex <= 4)
  .map(key => ({ ...key, ...backPositions[key.backIndex! - 1] })));
function oemClass(key: OemSpecialKey): Record<string, boolean> {
  const available = key.captureImplemented !== false;
  return { mapped: available && props.highlightOemKeys.includes(key.keyId), live: available && diagramOemKeys.value.includes(key.keyId) };
}
function oemLabel(key: OemSpecialKey): string {
  const state = oemClass(key);
  if (key.captureImplemented === false) return `${key.label}，此按键的输入暂不可用`;
  return `${key.label}${state.mapped ? '，已选中' : ''}${state.live ? '，已按下' : ''}`;
}
</script>

<template>
  <div ref="previewRoot" class="viz" :class="{ 'vector-device': Boolean(CONTROLLER_DIAGRAMS[diagramPersona].nodes) }">
    <div class="pad-wrap">
      <div v-if="exteriorSpecialKeys.length" class="oem-special-exterior" aria-label="专用按键">
        <div class="oem-caption">专用按键</div>
        <div class="oem-capsules">
          <span v-for="key in exteriorSpecialKeys" :key="key.keyId" class="oem-capsule" :class="oemClass(key)"
            :data-oem-key="key.keyId" role="img" :aria-label="oemLabel(key)" :aria-disabled="key.captureImplemented === false || undefined" :title="key.captureImplemented === false ? oemLabel(key) : key.label">
            <span class="oem-indicator" aria-hidden="true"></span><span class="oem-name">{{ key.label }}</span>
          </span>
        </div>
      </div>
      <div v-if="CONTROLLER_DIAGRAMS[diagramPersona].nodes" class="controller-preview-frame" :class="{ 'match-deck-height': matchSteamDeckHeight }" :style="previewFrameStyle">
        <ControllerDeviceDiagram :persona="diagramPersona" :buttons="diagramButtons" :axes="axes" :mapped-buttons="props.highlightButtons" :pressed-oem-keys="diagramOemKeys" :mapped-oem-keys="props.highlightOemKeys" :oem-keys="displayOemKeys" :interactive="testMode" :revision="baseImageRevision" @image-error="onBaseImageError" />
      </div>
      <svg v-else viewBox="0 48 420 282" class="pad" aria-label="手柄可视化">
        <defs>
          <filter id="glow-green" x="-50%" y="-50%" width="200%" height="200%">
            <feGaussianBlur stdDeviation="3" result="coloredBlur" />
            <feMerge>
              <feMergeNode in="coloredBlur" />
              <feMergeNode in="SourceGraphic" />
            </feMerge>
          </filter>
          <!-- 底图为近黑剪影：按 alpha 强行映射为纯白，形状/位置不变 -->
        </defs>

        <!-- PNG 在上一版基础上向下 5%（约 10.5）并放大 10%，以中心 x=210 放大。 -->
        <image
          :key="baseImageRevision"
          :href="baseImageUrl"
          x="-37.5"
          y="68.5"
          width="495"
          height="232.1"
          preserveAspectRatio="none"
          @error="onBaseImageError"
        />

        <!-- 交互层与 PNG 保持同一 1.1 倍几何映射；图标按中心缩放到 64%，LB/RB 放大到 85%。 -->
        <g transform="translate(-21 4.7) scale(1.1)">
          <!-- LT / RT：以 x=210 镜像，左侧补齐 cls(6)，左右严格对称。 -->
          <g class="pad-trigger" :class="cls(6)" transform="translate(128 74) scale(.64) translate(-124 -68)">
            <path d="M93 93 99 77Q102 67 116 61L136 53Q148 49 151 59L160 78L140 78Q118 80 106 89Z" />
            <text x="124" y="68">LT</text>
          </g>
          <g class="pad-trigger" :class="cls(7)" transform="translate(292 74) scale(.64) translate(-296 -68)">
            <path d="M327 93 321 77Q318 67 304 61L284 53Q272 49 269 59L260 78L280 78Q302 80 314 89Z" />
            <text x="296" y="68">RT</text>
          </g>

          <!-- LB / RB：同一轮廓关于 x=210 镜像。 -->
          <g class="pad-shoulder" :class="cls(4)" transform="translate(124 89) scale(.85) translate(-124 -86)">
            <path d="M140 78Q124 79 114 88L96 94L91 107Q102 99 118 97L154 87Q162 84 160 78Z" />
            <text x="124" y="86">LB</text>
          </g>
          <g class="pad-shoulder" :class="cls(5)" transform="translate(296 89) scale(.85) translate(-296 -86)">
            <path d="M280 78Q296 79 306 88L324 94L329 107Q318 99 302 97L266 87Q258 84 260 78Z" />
            <text x="296" y="86">RB</text>
          </g>

          <!-- 左摇杆 -->
          <g transform="translate(128 147) scale(.64) translate(-116 -138)">
            <g class="stick-range" :class="{ live: stickLPushed || isLive(10) }">
              <circle cx="116" cy="138" r="31" />
              <circle cx="116" cy="138" r="27" />
            </g>
            <g class="stick-dot" :class="{ live: stickLPushed || isLive(10), mapped: isMapped(10) }">
              <circle :cx="stickL.x" :cy="stickL.y" r="21" />
              <circle class="stick-cap-ring" :cx="stickL.x" :cy="stickL.y" r="16" />
            </g>
          </g>

          <!-- 方向键 -->
          <g transform="translate(169.5 195.5) scale(.66528 .7392) translate(-152.5 -200.5)">
            <g class="dpad" :class="{ live: isLive(12), mapped: isMapped(12) }">
              <path d="M139 172Q139 166 145 166H160Q166 166 166 172V194H139Z" />
            </g>
            <g class="dpad" :class="{ live: isLive(13), mapped: isMapped(13) }" transform="rotate(180 152.5 201.5)">
              <path d="M139 172Q139 166 145 166H160Q166 166 166 172V194H139Z" />
            </g>
            <g class="dpad" :class="{ live: isLive(14), mapped: isMapped(14) }" transform="rotate(-90 152.5 201.5) translate(152.5 201.5) scale(1 1.2) translate(-152.5 -201.5)">
              <path d="M139 172Q139 166 145 166H160Q166 166 166 172V194H139Z" />
            </g>
            <g class="dpad" :class="{ live: isLive(15), mapped: isMapped(15) }" transform="rotate(90 152.5 201.5) translate(152.5 201.5) scale(1 1.2) translate(-152.5 -201.5)">
              <path d="M139 172Q139 166 145 166H160Q166 166 166 172V194H139Z" />
            </g>
            <rect class="dpad-center" x="139" y="188" width="27" height="27" rx="2" />
          </g>

          <!-- 右摇杆 -->
          <g transform="translate(249 195) scale(.704) translate(-255 -191)">
            <g class="stick-range" :class="{ live: stickRPushed || isLive(11) }">
              <circle cx="255" cy="191" r="31" />
              <circle cx="255" cy="191" r="27" />
            </g>
            <g class="stick-dot" :class="{ live: stickRPushed || isLive(11), mapped: isMapped(11) }">
              <circle :cx="stickR.x" :cy="stickR.y" r="21" />
              <circle class="stick-cap-ring" :cx="stickR.x" :cy="stickR.y" r="16" />
            </g>
          </g>

          <!-- ABXY -->
          <g class="pad-btn face y" :class="cls(3)" transform="translate(293 130) scale(.6336) translate(-288 -102)">
            <circle cx="288" cy="102" r="17" /><text x="288" y="102">Y</text>
          </g>
          <g class="pad-btn face b" :class="cls(1)" transform="translate(313 148) scale(.6336) translate(-318 -132)">
            <circle cx="318" cy="132" r="17" /><text x="318" y="132">B</text>
          </g>
          <g class="pad-btn face a" :class="cls(0)" transform="translate(293 168) scale(.6336) translate(-288 -162)">
            <circle cx="288" cy="162" r="17" /><text x="288" y="162">A</text>
          </g>
          <g class="pad-btn face x" :class="cls(2)" transform="translate(273 148) scale(.6336) translate(-258 -132)">
            <circle cx="258" cy="132" r="17" /><text x="258" y="132">X</text>
          </g>

          <!-- View / Menu -->
          <g class="pad-btn small" :class="cls(8)" transform="translate(185 147) scale(.768) translate(-185 -132)">
            <circle cx="185" cy="132" r="10" />
            <g class="pad-glyph outline">
              <rect x="180.5" y="128.5" width="7" height="5.5" rx="0.8" />
              <rect x="183" y="130" width="7" height="5.5" rx="0.8" />
            </g>
          </g>
          <g class="pad-btn small" :class="cls(9)" transform="translate(231 147) scale(.768) translate(-235 -132)">
            <circle cx="235" cy="132" r="10" />
            <g class="pad-glyph">
              <line x1="231.5" y1="128.5" x2="238.5" y2="128.5" />
              <line x1="231.5" y1="132" x2="238.5" y2="132" />
              <line x1="231.5" y1="135.5" x2="238.5" y2="135.5" />
            </g>
          </g>
        </g>
        <g v-for="key in backKeys" :key="key.keyId" class="oem-back-key" :class="oemClass(key)"
          :data-oem-key="key.keyId" role="img" :aria-label="oemLabel(key)" :aria-disabled="key.captureImplemented === false || undefined">
          <title>{{ key.captureImplemented === false ? oemLabel(key) : key.label }}</title>
          <path class="oem-back-leader" :d="key.right ? `M${key.x+21} ${key.y+4}h11` : `M${key.x-21} ${key.y+4}h-11`" />
          <text class="oem-back-name" :x="key.right ? 400 : 20" :y="key.y+7" text-anchor="middle">背部 {{ key.backIndex }}</text>
          <g :transform="`translate(${key.x} ${key.y}) rotate(${key.right ? 12 : -12})`">
            <path class="oem-back-paddle" d="M-19-9 Q-20-13-14-13 H13 Q20-13 21-7 L17 9 Q16 13 10 13 H-12 Q-19 13-19 7 Z" />
            <text class="oem-back-code" x="0" y="4">{{ key.label }}</text>
          </g>
        </g>
        <g v-if="backKeys.length" class="oem-back-caption">
          <path d="M140 306h22m96 0h22" /><text x="210" y="310">背部按键 · 透视</text>
        </g>
      </svg>
      <div v-if="additionalBackKeys.length && !CONTROLLER_DIAGRAMS[diagramPersona].nodes" class="oem-additional-back" aria-label="其他背部按键">
        <div class="oem-caption">其他背部按键</div>
        <div class="oem-capsules">
          <span v-for="key in additionalBackKeys" :key="key.keyId" class="oem-capsule" :class="oemClass(key)"
            :data-oem-key="key.keyId" role="img" :aria-label="oemLabel(key)" :aria-disabled="key.captureImplemented === false || undefined" :title="key.captureImplemented === false ? oemLabel(key) : key.label">
            <span class="oem-indicator" aria-hidden="true"></span><span class="oem-name">{{ key.label }}</span>
          </span>
        </div>
      </div>
    </div>

    <div v-if="props.showTestMode" class="test-toggle">
      <Toggle
        v-model="testMode"
        :label="connected ? '手柄测试模式' : '未检测'"
        :description="connected ? (testMode ? '【B】按住以退出（实际按住 3 秒）' : '开启后可视化显示摇杆方向，但手柄不再控制程序界面') : '未检测到手柄，无法进入测试'"
        :disabled="!connected"
        color="accent"
        @update:model-value="setTestMode"
      />
    </div>
  </div>
</template>

<style scoped>
.viz {
  margin-top: 6px;
}
.pad-wrap {
  background: rgba(0, 0, 0, 0.22);
  border: 1px solid rgba(255, 255, 255, 0.06);
  border-radius: 14px;
  padding: 0;
  overflow: hidden;
}
.pad {
  width: 100%;
  max-width: 360px;
  display: block;
  margin: 0 auto;
}

.vector-device .pad-wrap{background:transparent;border:0;border-radius:0;overflow:visible;box-shadow:none}

/* 纯 SVG 手柄：深色机身 + 浅色轮廓，所有输入区均为独立矢量节点 */
.controller-shell path,
.controller-shell circle {
  fill: #0b0f16;
  stroke: #aabbd0;
  stroke-width: 1.8;
  stroke-linejoin: round;
}
.controller-shell .shell-body {
  fill: #101621;
  stroke-width: 2.2;
  filter: drop-shadow(0 8px 10px rgba(0, 0, 0, 0.35));
}
.controller-shell .shell-detail {
  fill: none;
  stroke: #68778d;
  stroke-width: 1.25;
}
.controller-shell .xbox-mark {
  fill: none;
  stroke: #c9d5e5;
  stroke-width: 1.4;
  stroke-linecap: round;
}
.pad-trigger path,
.pad-shoulder path {
  fill: #202a38;
  stroke: #aabbd0;
  stroke-width: 1.5;
  stroke-linejoin: round;
  transition: fill 0.08s, stroke 0.08s;
}
.pad-trigger text,
.pad-shoulder text {
  fill: #ffffff;
  font-size: 9px;
  font-weight: 700;
  text-anchor: middle;
  dominant-baseline: central;
  pointer-events: none;
  transition: fill 0.08s;
}
.pad-shoulder path {
  fill: #253142;
}

.dpad path,
.dpad-center {
  fill: #222c3a;
  stroke: #aabbd0;
  stroke-width: 1.5;
  stroke-linejoin: round;
  transition: fill 0.08s, stroke 0.08s;
}
.dpad-center {
  pointer-events: none;
}

.stick-range circle {
  fill: #121925;
  stroke: #7f90a8;
  stroke-width: 1.5;
  transition: fill 0.08s, stroke 0.08s;
}
.stick-range circle + circle {
  fill: none;
  stroke: #c2cfdf;
  stroke-width: 1.15;
}
.stick-dot > circle:not(.stick-cap-ring) {
  fill: #263244;
  stroke: #cdd8e8;
  stroke-width: 1.7;
  transition: fill 0.05s, stroke 0.05s;
}
.stick-cap-ring {
  fill: none;
  stroke: #718198;
  stroke-width: 1.15;
  pointer-events: none;
}

.pad-btn.face circle,
.pad-btn.small circle,
.pad-btn.guide rect {
  fill: #17202c;
  stroke: #aabbd0;
  stroke-width: 1.5;
  transition: fill 0.08s, stroke 0.08s;
}
.pad-btn.face.y circle { stroke: #d9c86a; }
.pad-btn.face.b circle { stroke: #df7d7d; }
.pad-btn.face.a circle { stroke: #70cf91; }
.pad-btn.face.x circle { stroke: #69aeea; }
.pad-btn text {
  fill: #d8e2ef;
  font-size: 14px;
  font-weight: 700;
  text-anchor: middle;
  dominant-baseline: central;
  pointer-events: none;
  transition: fill 0.08s;
}
.pad-btn.small text {
  font-size: 7px;
}
.pad-btn .pad-glyph {
  fill: none;
  stroke: #aabbd0;
  stroke-width: 1.3;
  stroke-linecap: round;
  pointer-events: none;
  transition: fill 0.08s, stroke 0.08s;
}
.pad-btn.mapped .pad-glyph { fill: #06203a; stroke: #06203a; }
.pad-btn.live .pad-glyph { fill: #06210f; stroke: #06210f; }

/* 描边型 glyph（小房子 / 电源环）：灰色空心，不实心 */
.pad-btn .pad-glyph.outline { fill: none; stroke: #aabbd0; }
.pad-btn.mapped .pad-glyph.outline { fill: none; stroke: #06203a; }
.pad-btn.live .pad-glyph.outline { fill: none; stroke: #06210f; }

/* 快捷键关联高亮：蓝色 + 闪烁动画（选中态从琥珀改为蓝，与开关/选中一致） */
@keyframes gpBlink {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.35; }
}
.pad-shoulder.mapped path,
.pad-trigger.mapped path,
.pad-btn.mapped circle,
.dpad.mapped path,
.dpad.mapped rect {
  fill: var(--accent);
  stroke: #7fb4ff;
  filter: drop-shadow(0 0 5px rgba(80, 150, 255, 0.75));
  animation: gpBlink 0.85s ease-in-out infinite;
}
.stick-dot.mapped circle {
  fill: var(--accent);
  stroke: #7fb4ff;
  animation: gpBlink 0.85s ease-in-out infinite;
}
.pad-shoulder.mapped text,
.pad-trigger.mapped text,
.pad-btn.mapped text {
  fill: #06203a;
}

.pad-shoulder.live path,
.pad-trigger.live path,
.pad-btn.live circle,
.dpad.live path,
.dpad.live rect,
.stick-range.live circle,
.stick-dot.live circle {
  fill: var(--ok);
  stroke: #5fe08a;
  filter: url(#glow-green);
}
.pad-shoulder.live text,
.pad-trigger.live text,
.pad-btn.live text {
  fill: #ffffff;
  filter: drop-shadow(0 0 2px #5fe08a);
}
.pad-shoulder.mapped text,
.pad-trigger.mapped text {
  fill: #ffffff;
}

/* 当某键既是「关联高亮」又是「实时按下」时，仍以蓝色闪烁为主（override 放在最后确保优先级） */
.pad-shoulder.mapped.live path,
.pad-trigger.mapped.live path,
.pad-btn.mapped.live circle,
.dpad.mapped.live path,
.stick-dot.mapped.live circle {
  stroke: #7fb4ff;
}
.pad-shoulder.mapped path,
.pad-trigger.mapped path,
.pad-btn.mapped circle,
.dpad.mapped path,
.dpad.mapped rect,
.stick-dot.mapped circle {
  fill: var(--accent) !important;
  stroke: #7fb4ff !important;
  filter: drop-shadow(0 0 5px rgba(80, 150, 255, 0.75)) !important;
  animation: gpBlink 0.85s ease-in-out infinite !important;
}

.test-banner {
  text-align: center;
  font-size: 11px;
  color: var(--accent-2);
  padding: 6px 0 2px;
}
.test-toggle {
  margin-top: 10px;
  padding-top: 4px;
  border-top: 1px solid rgba(255, 255, 255, 0.06);
}
/* Physical OEM preview: read-only, with no additional focus targets or polling. */
.oem-special-exterior, .oem-additional-back { display: grid; justify-items: center; gap: 8px; padding: 12px 12px 4px; }
.oem-additional-back { padding-bottom: 12px; }
.oem-caption { font-size: 10px; color: var(--text-dim); letter-spacing: 1px; }
.oem-capsules { display: flex; align-items: center; justify-content: center; flex-wrap: wrap; gap: 8px; width: 100%; min-width: 0; }
.oem-capsule { display: inline-flex; align-items: center; justify-content: center; gap: 8px; box-sizing: border-box; min-height: 32px; max-width: 100%; padding: 6px 15px; border: 1px solid #526579; border-radius: 999px; background: var(--bg-input); color: var(--text); font-size: 11px; font-weight: 600; line-height: 1.35; }
.oem-name { overflow-wrap: anywhere; min-width: 0; }
.oem-indicator { width: 5px; height: 5px; flex: 0 0 auto; border: 1px solid #92a6ba; border-radius: 50%; }
.oem-back-paddle { fill: #192634; fill-opacity: .92; stroke: #8d9fb2; stroke-width: 1.5; stroke-dasharray: 3 2; }
.oem-back-code { fill: #d7e1ed; font-size: 11px; font-weight: 750; text-anchor: middle; }
.oem-back-leader { fill: none; stroke: #617186; stroke-width: 1; }
.oem-back-name { fill: #8b9caf; font-size: 8.5px; }
.oem-back-caption path { fill: none; stroke: #344355; stroke-width: 1; }
.oem-back-caption text { fill: var(--text-dim); font-size: 9px; text-anchor: middle; letter-spacing: .5px; }
.oem-back-key.live .oem-back-paddle { fill: #18452c; stroke: #59d889; stroke-dasharray: none; }
.oem-back-key.live .oem-back-name, .oem-back-key.live .oem-back-code { fill: #a7eac1; }
.oem-back-key.mapped .oem-back-paddle { fill: color-mix(in srgb, var(--accent) 30%, var(--bg-input)); stroke: var(--accent); stroke-dasharray: none; }
.oem-back-key.mapped .oem-back-name, .oem-back-key.mapped .oem-back-code { fill: var(--accent); }
.oem-back-key.mapped .oem-back-leader { stroke: var(--accent); }
.oem-capsule.live { border-color: #59d889; color: #a7eac1; background: #163424; }
.oem-capsule.live .oem-indicator { border-color: #59d889; background: #59d889; }
.oem-capsule.mapped { border-color: var(--accent); color: var(--accent); background: color-mix(in srgb, var(--accent) 14%, var(--bg-input)); }
.oem-capsule.mapped .oem-indicator { border-color: var(--accent); background: var(--accent); }
.vector-device .oem-special-exterior{max-height:108px;box-sizing:border-box;overflow-y:auto;padding:0 8px 8px;gap:5px}
.vector-device .oem-caption{font-size:11px;letter-spacing:.5px}
.vector-device .oem-capsule.live{border-color:#8bc4ed;color:#fff;background:#4897d2}
.vector-device .oem-capsule.live .oem-indicator{border-color:#fff;background:#fff}
.controller-preview-frame{width:100%}
.controller-preview-frame.match-deck-height{display:grid;place-items:center;aspect-ratio:var(--preview-reference-ratio)}
.controller-preview-frame.match-deck-height :deep(.device-diagram){width:calc(100% * var(--preview-device-scale));margin:0 auto}
.oem-capsule.mapped:not(.live){animation:oem-mapped-fill .85s ease-in-out infinite}
@keyframes oem-mapped-fill{0%,100%{background-color:color-mix(in srgb,var(--accent) 35%,var(--bg-input))}50%{background-color:color-mix(in srgb,var(--accent) 14%,var(--bg-input))}}
@media(prefers-reduced-motion:reduce){.oem-capsule.mapped:not(.live){animation:none}}
</style>
