<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount, computed, watch } from 'vue';
import Toggle from '@/components/Toggle.vue';
import type { GamepadSettings } from '@/bridge/yeman';

const props = withDefaults(
  defineProps<{
    settings: GamepadSettings;
    highlightButtons?: number[];
  }>(),
  { highlightButtons: () => [] }
);

const live = ref<boolean[]>([]);
const axes = ref<number[]>([]);
const connected = ref(false);
const padName = ref('');
const testMode = ref(false);
let raf = 0;

function setTestMode(v: boolean) {
  testMode.value = v;
  window.dispatchEvent(new CustomEvent('ipc:gamepad.testmode', { detail: v }));
}

function poll() {
  try {
    const pads = navigator.getGamepads ? navigator.getGamepads() : [];
    const p = Array.from(pads).find((x) => x && x.connected) || null;
    if (p) {
      connected.value = true;
      padName.value = p.id || '手柄';
      const pressedNow = p.buttons.map((b) => b.pressed);
      live.value = pressedNow;
      axes.value = Array.from(p.axes).map((a) => (Number.isFinite(a) ? a : 0));
    } else {
      connected.value = false;
      live.value = [];
      axes.value = [];
    }
  } catch {
    connected.value = false;
  }
  raf = requestAnimationFrame(poll);
}

onMounted(() => {
  raf = requestAnimationFrame(poll);
});
onBeforeUnmount(() => {
  if (raf) cancelAnimationFrame(raf);
  setTestMode(false);
});
watch(connected, (c) => {
  if (!c && testMode.value) setTestMode(false); // 断连时自动退出测试模式
});

// 摇杆方向点（按 02.png 机身缩放后校准）
const STICK_L = { x: 141, y: 154 };
const STICK_R = { x: 247, y: 204 };
const STICK_TRAVEL = 9;
const stickL = computed(() => ({
  x: STICK_L.x + (axes.value[0] || 0) * STICK_TRAVEL,
  y: STICK_L.y + (axes.value[1] || 0) * STICK_TRAVEL,
}));
const stickR = computed(() => ({
  x: STICK_R.x + (axes.value[2] || 0) * STICK_TRAVEL,
  y: STICK_R.y + (axes.value[3] || 0) * STICK_TRAVEL,
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
</script>

<template>
  <div class="viz">
    <div class="pad-wrap">
      <svg viewBox="0 61 420 249" class="pad" aria-label="手柄可视化">
        <defs>
          <filter id="glow-green" x="-50%" y="-50%" width="200%" height="200%">
            <feGaussianBlur stdDeviation="3" result="coloredBlur" />
            <feMerge>
              <feMergeNode in="coloredBlur" />
              <feMergeNode in="SourceGraphic" />
            </feMerge>
          </filter>
        </defs>

        <!-- 机身底图：原图已适配黑色 UI，直接显示 -->
        <image
          href="/gamepad-base.png"
          x="40"
          y="60"
          width="340"
          height="283"
          preserveAspectRatio="none"
        />

        <!-- LT / RT 扳机：与 LB/RB 同中心竖线（累计上移 5px） -->
        <g class="pad-trigger" :class="cls(6)">
          <rect x="103" y="71" width="34" height="16" rx="8" />
          <text x="120" y="80">LT</text>
        </g>
        <g class="pad-trigger" :class="cls(7)">
          <rect x="283" y="71" width="34" height="16" rx="8" />
          <text x="300" y="80">RT</text>
        </g>

        <!-- LB / RB 肩键：与 LT/RT 同中心竖线，底边压入机身顶边（累计上移 5px） -->
        <g class="pad-shoulder" :class="cls(4)">
          <rect x="95" y="87" width="50" height="18" rx="9" />
          <text x="120" y="97">LB</text>
        </g>
        <g class="pad-shoulder" :class="cls(5)">
          <rect x="275" y="87" width="50" height="18" rx="9" />
          <text x="300" y="97">RB</text>
        </g>

        <!-- 左摇杆（上左） -->
        <g class="stick-base" :class="{ live: stickLPushed }">
          <circle cx="141" cy="154" r="19" />
        </g>
        <g class="stick-dot" :class="{ live: stickLPushed, mapped: isMapped(10) }">
          <circle :cx="stickL.x" :cy="stickL.y" r="10" />
        </g>

        <!-- 方向键 D-pad（下左）：单中心(180,204) 对称十字，缩小 40%（居中缩放）并内移避免贴边 -->
        <g class="dpad" :class="{ live: isLive(12), mapped: isMapped(12) }">
          <rect x="177" y="182" width="6" height="22" rx="3" />
        </g>
        <g class="dpad" :class="{ live: isLive(13), mapped: isMapped(13) }">
          <rect x="177" y="204" width="6" height="22" rx="3" />
        </g>
        <g class="dpad" :class="{ live: isLive(14), mapped: isMapped(14) }">
          <rect x="158" y="201" width="22" height="6" rx="3" />
        </g>
        <g class="dpad" :class="{ live: isLive(15), mapped: isMapped(15) }">
          <rect x="180" y="201" width="22" height="6" rx="3" />
        </g>

        <!-- 右摇杆（下右） -->
        <g class="stick-base" :class="{ live: stickRPushed }">
          <circle cx="247" cy="204" r="19" />
        </g>
        <g class="stick-dot" :class="{ live: stickRPushed, mapped: isMapped(11) }">
          <circle :cx="stickR.x" :cy="stickR.y" r="10" />
        </g>

        <!-- A B X Y（上右）：严格水平/垂直钻石对齐，中心(279,154) -->
        <g class="pad-btn face" :class="cls(3)">
          <circle cx="279" cy="130" r="10" />
          <text x="279" y="130">Y</text>
        </g>
        <g class="pad-btn face" :class="cls(1)">
          <circle cx="303" cy="154" r="10" />
          <text x="303" y="154">B</text>
        </g>
        <g class="pad-btn face" :class="cls(0)">
          <circle cx="279" cy="178" r="10" />
          <text x="279" y="178">A</text>
        </g>
        <g class="pad-btn face" :class="cls(2)">
          <circle cx="255" cy="154" r="10" />
          <text x="255" y="154">X</text>
        </g>

        <!-- Back / Guide / Start（中间水平排列，累计上移 15px） -->
        <!-- 左：Select/Back — 圆形 + 两道横条 -->
        <g class="pad-btn small" :class="cls(8)">
          <circle cx="187" cy="140" r="7" />
          <g class="pad-glyph">
            <rect x="184.2" y="138.4" width="5.6" height="1.4" rx="0.65" />
            <rect x="184.2" y="140.6" width="5.6" height="1.4" rx="0.65" />
          </g>
        </g>
        <!-- 中：Home — 圆形 + 小房子（沿用上一版 outline） -->
        <g class="pad-btn guide" :class="cls(16)">
          <circle cx="211" cy="140" r="8" />
          <g class="pad-glyph outline">
            <path d="M208.5 139.5 211 136.8 213.5 139.5V142.5H208.5Z" />
          </g>
        </g>
        <!-- 右：Start/Menu — 圆形 + 三道横线 -->
        <g class="pad-btn small" :class="cls(9)">
          <circle cx="235" cy="140" r="7" />
          <g class="pad-glyph">
            <line x1="233" y1="137.6" x2="237" y2="137.6" />
            <line x1="233" y1="140" x2="237" y2="140" />
            <line x1="233" y1="142.4" x2="237" y2="142.4" />
          </g>
        </g>
      </svg>
    </div>

    <div class="legend">
      <span><i class="lg mapped"></i> 快捷键关联</span>
      <span><i class="lg live"></i> 实时按下 / 摇杆推动</span>
    </div>

    <div class="test-toggle">
      <Toggle
        v-model="testMode"
        :label="connected ? '手柄测试模式' : '未检测'"
        :description="connected ? '开启后可视化显示摇杆方向，但手柄不再控制程序界面' : '未检测到手柄，无法进入测试'"
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

/* 底图原图即深色机身 + 透明背景（用户指定直接导入、不做处理）；矢量按钮用浅色填充在深色机身上才看得清 */
.pad-trigger rect,
.pad-shoulder rect {
  fill: #7d8ba2;
  stroke: #aabbd0;
  stroke-width: 1.5;
  transition: fill 0.08s, stroke 0.08s;
}
.pad-trigger text,
.pad-shoulder text {
  fill: #0b0d11;
  font-size: 9px;
  font-weight: 700;
  text-anchor: middle;
  dominant-baseline: central;
  pointer-events: none;
  transition: fill 0.08s;
}
.pad-shoulder rect {
  fill: #8695ad;
}

.dpad path,
.dpad rect {
  fill: #7d8ba2;
  stroke: #aabbd0;
  stroke-width: 1.5;
  transition: fill 0.08s, stroke 0.08s;
}

.stick-base circle {
  fill: #7d8ba2;
  stroke: #aabbd0;
  stroke-width: 1.5;
  transition: fill 0.08s, stroke 0.08s;
}
.stick-dot circle {
  fill: #a4b3c9;
  stroke: #cdd8e8;
  stroke-width: 1.5;
  transition: fill 0.05s, stroke 0.05s;
}

.pad-btn.face circle,
.pad-btn.small circle,
.pad-btn.guide circle {
  fill: #7d8ba2;
  stroke: #aabbd0;
  stroke-width: 1.5;
  transition: fill 0.08s, stroke 0.08s;
}
.pad-btn text {
  fill: #0b0d11;
  font-size: 11px;
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
  fill: #0b0d11;
  stroke: #0b0d11;
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
.pad-shoulder.mapped rect,
.pad-trigger.mapped rect,
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

.pad-shoulder.live rect,
.pad-trigger.live rect,
.pad-btn.live circle,
.dpad.live path,
.dpad.live rect,
.stick-base.live circle,
.stick-dot.live circle {
  fill: var(--ok);
  stroke: #5fe08a;
  filter: url(#glow-green);
}
.pad-shoulder.live text,
.pad-trigger.live text,
.pad-btn.live text {
  fill: #06210f;
}

/* 当某键既是「关联高亮」又是「实时按下」时，仍以蓝色闪烁为主（override 放在最后确保优先级） */
.pad-shoulder.mapped.live rect,
.pad-trigger.mapped.live rect,
.pad-btn.mapped.live circle,
.dpad.mapped.live path,
.stick-dot.mapped.live circle {
  stroke: #7fb4ff;
}
.pad-shoulder.mapped rect,
.pad-trigger.mapped rect,
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
.legend {
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
  justify-content: center;
  margin-top: 10px;
  font-size: 10.5px;
  color: var(--text-dim);
}
.legend span {
  display: inline-flex;
  align-items: center;
  gap: 5px;
}
.lg {
  width: 11px;
  height: 11px;
  border-radius: 3px;
  display: inline-block;
}
.lg.mapped {
  background: rgba(80, 150, 255, 0.3);
  border: 1px solid var(--accent);
}
.lg.live {
  background: var(--ok);
}
.test-toggle {
  margin-top: 10px;
  padding-top: 4px;
  border-top: 1px solid rgba(255, 255, 255, 0.06);
}
</style>
