<script setup lang="ts">
import { ref, computed, onMounted, onBeforeUnmount } from 'vue';
import Slider from '@/components/Slider.vue';
import {
  probeFan, fanGetTemp, fanSetAuto, fanSetDuty, fanReadRpm, type FanProbe,
} from '@/bridge/fanctl';
import InlineIcon from '@/components/InlineIcon.vue';
import AppIcon from '@/components/AppIcon.vue';

type Preset = 'auto' | 'quiet' | 'balanced' | 'performance' | 'manual';

// 线性 温度->占空比 曲线 (°C / %), 与 FanControl 的 Linear 曲线同构。
// 三个预设仅斜率/激进度不同, 覆盖最常用的大众化散热策略。
const CURVES: Record<'quiet' | 'balanced' | 'performance', { tMin: number; pMin: number; tMax: number; pMax: number }> = {
  quiet:       { tMin: 45, pMin: 25, tMax: 85, pMax: 70 },
  balanced:    { tMin: 40, pMin: 35, tMax: 80, pMax: 100 },
  performance: { tMin: 35, pMin: 50, tMax: 75, pMax: 100 },
};

const probe = ref<FanProbe | null>(null);
const supported = computed(() => !!probe.value?.supported);
const mode = computed(() => probe.value?.mode ?? 'generic');
const available = computed(() => !!probe.value?.available);
const hint = computed(() => probe.value?.hint ?? '');
const isGPD = computed(() => !!probe.value?.isGPDWin5);

const rpm = ref(0);
const temp = ref(0);
const activePreset = ref<Preset>('auto');
const manualPct = ref(50);
const livePct = ref(0);          // 当前跟随中的目标占空比
let lastPushed = -99;            // 上次实际下发的占空比 (迟滞用)
const errMsg = ref('');
const busy = ref(false);
let timer: number | null = null;

// 滑块展示: 手动模式显示手动值, 跟随模式显示实时目标占空比
const sliderModel = computed<number>({
  get: () => (activePreset.value === 'manual' ? manualPct.value : livePct.value),
  set: (v: number) => { manualPct.value = v; },
});

function clampPct(v: number) { return Math.max(0, Math.min(100, Math.round(v))); }

function computePct(t: number, c: { tMin: number; pMin: number; tMax: number; pMax: number }) {
  if (t <= c.tMin) return c.pMin;
  if (t >= c.tMax) return c.pMax;
  return c.pMin + (c.pMax - c.pMin) * (t - c.tMin) / (c.tMax - c.tMin);
}

function stopFollow() {
  if (timer !== null) { clearInterval(timer); timer = null; }
}

async function pushOnce() {
  if (!available.value) return;
  const p = activePreset.value;
  if (p === 'auto' || p === 'manual') return;
  const t = (await fanGetTemp()).temp;
  temp.value = t;
  const target = clampPct(computePct(t, CURVES[p]));
  livePct.value = target;
  // 迟滞: 相对上次下发变化 >=3% 才真正下发, 避免频繁抖动
  if (Math.abs(target - lastPushed) >= 3) {
    const r = await fanSetDuty(target);
    if (!r.ok) errMsg.value = '风扇设置失败：' + r.msg;
    lastPushed = target;
  }
  rpm.value = (await fanReadRpm()).rpm;
}

function startFollow() {
  stopFollow();
  if (!available.value) return;
  if (activePreset.value === 'auto' || activePreset.value === 'manual') return;
  lastPushed = -99;           // 强制首次立即下发
  pushOnce();
  timer = window.setInterval(pushOnce, 3000);
}

async function selectPreset(p: Preset) {
  if (busy.value) return;
  busy.value = true; errMsg.value = '';
  stopFollow();
  activePreset.value = p;
  try {
    if (p === 'auto') {
      const r = await fanSetAuto();
      if (!r.ok) errMsg.value = '自动模式失败：' + r.msg;
      else rpm.value = (await fanReadRpm()).rpm;
    } else if (p === 'manual') {
      const r = await fanSetDuty(manualPct.value);
      if (!r.ok) errMsg.value = '手动设置失败：' + r.msg;
      else { livePct.value = manualPct.value; lastPushed = manualPct.value; rpm.value = (await fanReadRpm()).rpm; }
    } else {
      startFollow();
    }
  } catch (e) {
    errMsg.value = '风扇操作失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function onSliderCommit(v: number) {
  manualPct.value = clampPct(v);
  if (activePreset.value !== 'manual') {
    await selectPreset('manual');
    return;
  }
  if (busy.value) return;
  busy.value = true; errMsg.value = '';
  try {
    const r = await fanSetDuty(manualPct.value);
    if (!r.ok) errMsg.value = '手动设置失败：' + r.msg;
    else { livePct.value = manualPct.value; lastPushed = manualPct.value; rpm.value = (await fanReadRpm()).rpm; }
  } catch (e) {
    errMsg.value = '手动设置失败：' + (e as Error).message;
  } finally {
    busy.value = false;
  }
}

async function refreshRpm() {
  rpm.value = (await fanReadRpm()).rpm;
}

onMounted(async () => {
  const p = await probeFan();
  probe.value = p;
  if (p.supported) {
    rpm.value = p.rpm;
    activePreset.value = 'auto';
    manualPct.value = 50;
    livePct.value = 0;
    // 默认进入自动(交还硬件/FanControl 曲线), 仅在通道可用时下发
    if (available.value) {
      const r = await fanSetAuto().catch(() => null);
      if (r && !r.ok) errMsg.value = '自动模式失败：' + r.msg;
    }
  }
});

onBeforeUnmount(() => stopFollow());
</script>

<template>
  <section v-if="supported" class="card">
    <div class="card-head">
      <h3 class="card-title"><InlineIcon name="fan" /> 风扇控制</h3>
      <span class="mode-badge" :class="mode">{{ mode === 'dedicated' ? '专用 · GPD' : '通用 · FanControl' }}</span>
    </div>

    <div v-if="!available" class="hint warn">{{ hint || '风扇控制不可用：未检测到受支持的风扇通道。' }}</div>
    <div v-if="errMsg" class="err-bar">{{ errMsg }}</div>

    <div class="fan-rpm">
      <span class="rpm-label">转速</span>
      <b class="rpm-val">{{ rpm }} RPM</b>
      <span v-if="temp" class="temp-val">· {{ temp }}°C</span>
      <button class="rpm-refresh" :disabled="busy" title="刷新转速" @click="refreshRpm"><AppIcon name="refresh" /></button>
    </div>

    <div class="preset-row">
      <button class="preset" :class="{ active: activePreset === 'auto' }" :disabled="!available || busy" @click="selectPreset('auto')">自动</button>
      <button class="preset" :class="{ active: activePreset === 'quiet' }" :disabled="!available || busy" @click="selectPreset('quiet')">静音</button>
      <button class="preset" :class="{ active: activePreset === 'balanced' }" :disabled="!available || busy" @click="selectPreset('balanced')">平衡</button>
      <button class="preset" :class="{ active: activePreset === 'performance' }" :disabled="!available || busy" @click="selectPreset('performance')">性能</button>
    </div>

    <div class="fan-row">
      <Slider
        v-model="sliderModel"
        :min="0"
        :max="100"
        :step="1"
        label="占空比 / 目标"
        unit="%"
        color="accent"
        :disabled="!available || busy || activePreset !== 'manual'"
        @commit="onSliderCommit"
      />
    </div>
    <div class="hint">{{ isGPD ? 'GPD Win5 专用：自动=主板控速；预设按温度坡度线性控双风扇。' : '通用(台式机)：经 FanControl 控制主板风扇，预设按温度坡度线性控转速。拖动滑块即切手动。' }}</div>
  </section>
</template>

<style scoped>
.card-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  margin-bottom: 10px;
}
.card-title {
  font-size: 12px;
  font-weight: 600;
  margin: 0;
  color: var(--text);
  display: flex;
  align-items: center;
  gap: 6px;
}
.mode-badge {
  font-size: 10px;
  font-weight: 600;
  padding: 2px 8px;
  border-radius: 999px;
  border: 1px solid var(--border, #1c2533);
  color: var(--text-dim);
}
.mode-badge.dedicated { color: #ffb454; border-color: #ffb45455; }
.mode-badge.generic { color: #4fd1ff; border-color: #4fd1ff55; }
.fan-rpm {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 10px;
  font-size: 12px;
  color: var(--text-dim);
}
.rpm-val {
  font-size: 15px;
  color: var(--accent);
  font-variant-numeric: tabular-nums;
}
.temp-val {
  color: var(--text);
  font-variant-numeric: tabular-nums;
}
.rpm-refresh {
  margin-left: auto;
  border: 1px solid var(--border, #1c2533);
  background: var(--bg-input, #0e1622);
  color: var(--text);
  border-radius: 8px;
  width: 30px;
  height: 30px;
  cursor: pointer;
  font-size: 14px;
}
.rpm-refresh:hover:not(:disabled) { border-color: var(--accent); }
.rpm-refresh:disabled { opacity: 0.5; cursor: not-allowed; }
.preset-row {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 8px;
  margin-bottom: 10px;
}
.preset {
  border: 1px solid var(--border, #1c2533);
  background: var(--bg-input, #0e1622);
  color: var(--text-dim);
  border-radius: 8px;
  padding: 8px 0;
  font-size: 12px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.12s ease;
}
.preset:hover:not(:disabled) { border-color: var(--accent); color: var(--text); }
.preset.active {
  background: var(--accent);
  border-color: var(--accent);
  color: #08101c;
}
.preset:disabled { opacity: 0.45; cursor: not-allowed; }
.fan-row {
  display: flex;
  align-items: center;
  gap: 10px;
}
.fan-row > .slider { flex: 1 1 auto; min-width: 0; }
.hint {
  margin-top: 8px;
  font-size: 10px;
  color: var(--text-dim);
  line-height: 1.4;
}
.hint.warn {
  color: #ffb454;
  background: #ffb45414;
  border: 1px solid #ffb45433;
  border-radius: 8px;
  padding: 6px 8px;
}
.err-bar {
  margin-bottom: 10px;
  font-size: 11px;
  color: #ff6b6b;
  background: #ff6b6b14;
  border: 1px solid #ff6b6b33;
  border-radius: 8px;
  padding: 6px 8px;
}
</style>
