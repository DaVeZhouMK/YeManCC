<script setup lang="ts">
// TopMonitorBar.vue — 顶部监控条（电池设备 4 格；台式机 3 格等比）
//
// 数据块（从左往右）：
//   1 TDP    : CPU Package Power (W)
//   2 CPU    : 当前主频 x.x GHz
//   3 温度   : CPU (Tctl/Tdie) x°（>=85 红 / >=75 琥珀告警色）
//   4 电池   : 充电 xxW / 放电 xxW（仅电池设备显示）
// 台式机判断为无电池时，不渲染电池格，剩余三格自然等宽。
import GameRecognitionControl from '@/components/GameRecognitionControl.vue';
import { computed, onMounted, onUnmounted, ref } from 'vue';
import { registerScheduledTask } from '@/scheduler';
import { startTopMonitor, stopTopMonitor, readTopMonitor, setTopMonitorData, topMonitorData } from '@/bridge/topmon';
import InlineIcon from '@/components/InlineIcon.vue';
import { chooseFolder, currentName, hasFolder, initMusic, playing, togglePlay } from '@/bridge/music';

const POLL_MS = 1000; // 前台每 1 秒读取一次；后台由 pauseWhenHidden 暂停
let stopTask: (() => void) | null = null;

const data = topMonitorData;
const showBattery = computed(() => Boolean(data.value && (data.value.hasBattery || data.value.ac === 0)));

// ── 格式化 ──
const tdpText = computed(() => (data.value ? data.value.tdpW.toFixed(1) + 'W' : '--'));
const cpuText = computed(() => {
  const m = data.value?.freqMhz ?? 0;
  return m > 0 ? (m / 1000).toFixed(1) + 'G' : '--';
});
const tempText = computed(() => (data.value && data.value.tempC > 0 ? Math.round(data.value.tempC) + '°' : '--'));
const tempClass = computed(() => {
  const t = data.value?.tempC ?? 0;
  if (t >= 85) return 't-hot';
  if (t >= 75) return 't-warm';
  return '';
});
const batteryText = computed(() => {
  const d = data.value;
  if (!d) return '--';
  const watts = Math.round(Math.abs(d.chargeW));
  if (watts === 0) return '0W';
  return `${d.chargeW > 0 ? '+' : '-'}${watts}W`;
});
const batteryClass = computed(() => {
  const d = data.value;
  if (!d) return '';
  if (d.chargeW > 0.5) return 'p-charge';
  if (d.chargeW < -0.5) return 'p-discharge';
  return 'p-zero';
});

async function poll(): Promise<void> {
  setTopMonitorData(await readTopMonitor().catch(() => null));
}

onMounted(() => {
  void startTopMonitor(); // 拉起守护（幂等：单实例自清理），首轮数据 1-2s 内就绪
  stopTask = registerScheduledTask('topmon-read', POLL_MS, poll, {
    pauseWhenHidden: true, // 窗口隐藏时暂停轮询（顶部条不可见，省 IPC）
    runImmediately: true,
  });
});
onUnmounted(() => {
  stopTask?.();
  stopTask = null;
  void stopTopMonitor();
});

// ── 顶部旋转 CD：仅鼠标/触摸可触发，手柄完全无法点击 ──
const discTitle = computed(() => {
  if (!hasFolder.value) return '指定音乐地址';
  return playing.value ? '暂停：' + currentName.value : '播放：' + currentName.value;
});

const folderNoticeOpen = ref(false);

function closeFolderNotice(): void {
  folderNoticeOpen.value = false;
}

function openFolderNotice(): void {
  folderNoticeOpen.value = true;
}

async function chooseFolderFromNotice(): Promise<void> {
  closeFolderNotice();
  await chooseFolder();
}

function onFolderNoticeKeydown(event: KeyboardEvent): void {
  if (event.key === 'Escape' && folderNoticeOpen.value) {
    event.preventDefault();
    closeFolderNotice();
  }
}

async function onDiscPointer(event: PointerEvent): Promise<void> {
  // 仅接受鼠标/触摸；手柄产生的 click 没有绑定业务处理函数，因此不会被触发。
  if (event.pointerType !== 'mouse' && event.pointerType !== 'touch') return;
  (event.currentTarget as HTMLElement | null)?.blur();

  // 等待 native 完成默认 QQ/网易云目录探测，避免程序刚启动时误弹提示。
  await initMusic();
  if (!hasFolder.value) {
    openFolderNotice();
    return;
  }
  togglePlay();
}

onMounted(() => {
  document.addEventListener('keydown', onFolderNoticeKeydown);
});

onUnmounted(() => {
  document.removeEventListener('keydown', onFolderNoticeKeydown);
});
</script>

<template>
  <div class="topmon-bar">
    <GameRecognitionControl />
    <div class="tm-metrics">
      <!-- 1 TDP -->
      <div class="tm-cell">
        <div class="tm-label">TDP</div>
        <div class="tm-value">{{ tdpText }}</div>
      </div>
      <!-- 2 CPU 频率 -->
      <div class="tm-cell">
        <div class="tm-label">CPU</div>
        <div class="tm-value">{{ cpuText }}</div>
      </div>
      <!-- 3 温度 -->
      <div class="tm-cell">
        <div class="tm-label">温度</div>
        <div class="tm-value" :class="tempClass">{{ tempText }}</div>
      </div>
      <!-- 4 电池：仅有电池设备显示；台式机为三格等宽 -->
      <div v-if="showBattery" class="tm-cell">
        <div class="tm-label">电池</div>
        <div class="tm-value" :class="batteryClass">{{ batteryText }}</div>
      </div>
    </div>

    <!-- 旋转 CD：最右侧固定宽度入口；手柄不可聚焦/触发 -->
    <button
      class="music-disc"
      :class="{ spinning: playing, untuned: !hasFolder }"
      data-gp-ignore
      tabindex="-1"
      :title="discTitle"
      aria-label="音乐播放"
      @pointerdown.prevent
      @pointerup.stop="onDiscPointer"
    >
      <span class="cd-shape"></span>
    </button>

    <Teleport to="body">
      <Transition name="music-notice">
        <div
          v-if="folderNoticeOpen"
          class="music-notice-mask"
          @pointerdown.self="closeFolderNotice"
        >
          <div
            class="music-folder-notice"
            role="alertdialog"
            aria-modal="true"
            aria-label="音乐播放提示"
            data-gp-modal
            @pointerdown.stop
            @keydown.esc.prevent="closeFolderNotice"
          >
            <div class="music-notice-title"><InlineIcon name="warning" />需要指定音乐地址</div>
            <p class="music-notice-desc">未检测到 QQ 音乐或网易云音乐的默认音乐目录，请先指定音乐文件夹地址后才能播放音乐。</p>
            <div class="music-notice-actions">
              <button type="button" data-gp-group="music-notice" @click="chooseFolderFromNotice"><InlineIcon name="folder" />选择音乐文件夹</button>
              <button type="button" data-gp-group="music-notice" @click="closeFolderNotice">知道了</button>
            </div>
          </div>
        </div>
      </Transition>
    </Teleport>
  </div>
</template>

<style scoped>
/* 顶部条：独立于 app-content 滚动层，宽度锁定为 app-main 全宽，不受滚动条影响 */
.topmon-bar {
  display: flex;
  align-items: stretch;
  width: 100%;
  height: 34px;
  margin: 8px 0 10px; /* 顶部留出呼吸空间；底部与页面内容分隔 */
  padding: 0 12px; /* 顶死左右，内容内缩 12px；与下方页面卡片内容线对齐 */
  background: transparent;
  border-radius: 0; /* 顶部条左右贴边，不产生视觉缝隙 */
  border-bottom: none;
  flex: 0 0 auto;
  position: relative;
  z-index: 20;
}
/* 原监控格包进 .tm-metrics，继续等分；CD 固定宽度不参与等分 */
.tm-metrics {
  display: flex;
  flex: 1 1 auto;
  min-width: 0;
}
/* 旋转 CD：最右侧固定入口 */
.music-disc {
  flex: 0 0 30px;
  width: 30px;
  margin-left: 10px;
  padding: 0;
  border: none;
  background: transparent;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
}
.cd-shape {
  position: relative;
  width: 24px;
  height: 24px;
  overflow: hidden;
  border-radius: 50%;
  background: #050608;
  border: 1px solid rgba(255, 255, 255, 0.22);
  box-shadow:
    inset 0 0 0 1px rgba(0, 0, 0, 0.88),
    0 1px 3px rgba(0, 0, 0, 0.48);
  animation: music-disc-spin 2.4s linear infinite;
  animation-play-state: paused;
}
/* 参考图中心：白色轴孔 + 黑色内环 + 白色外环 */
.cd-shape::before {
  content: '';
  position: absolute;
  inset: 0;
  border-radius: 50%;
  background: radial-gradient(
    circle at 50% 50%,
    #fff 0 18%,
    #050608 19% 31%,
    #fff 32% 45%,
    transparent 46%
  );
  pointer-events: none;
}
/* 参考图左上白色扇形，仅占唱片外圈 */
.cd-shape::after {
  content: '';
  position: absolute;
  inset: 0;
  border-radius: 50%;
  background: conic-gradient(from 310deg, #fff 0 42deg, transparent 42deg 360deg);
  -webkit-mask: radial-gradient(circle, transparent 0 48%, #000 49% 94%, transparent 95%);
  mask: radial-gradient(circle, transparent 0 48%, #000 49% 94%, transparent 95%);
  pointer-events: none;
}
.music-disc.spinning .cd-shape {
  animation-play-state: running;
}
.music-disc.untuned .cd-shape {
  filter: grayscale(0.7) brightness(0.7);
}
.music-notice-mask {
  position: fixed;
  inset: 0;
  z-index: 1199;
  display: grid;
  place-items: center;
  padding: 16px;
  background: rgba(0, 0, 0, 0.24);
}
.music-folder-notice {
  position: relative;
  z-index: 1200;
  width: min(380px, calc(100vw - 32px));
  background: #161d29;
  border: 1px solid #2a3342;
  border-radius: 12px;
  padding: 18px 20px 17px;
  box-shadow: 0 16px 40px rgba(0, 0, 0, 0.55);
  display: flex;
  flex-direction: column;
  gap: 12px;
  min-height: 0;
  overflow: visible;
}
.music-notice-title {
  display: flex;
  align-items: center;
  gap: 9px;
  font-size: 16px;
  font-weight: 700;
  color: var(--text);
}
.music-notice-title :deep(svg) {
  width: 20px;
  height: 20px;
  color: var(--accent-2);
}
.music-notice-desc {
  margin: 0;
  color: var(--text-dim);
  font-size: 14px;
  line-height: 1.6;
  white-space: pre-line;
}
.music-notice-actions {
  display: grid;
  grid-template-columns: 1fr;
  gap: 10px;
  margin-top: 3px;
}
.music-notice-actions button {
  min-height: 44px;
  border: 1px solid rgba(255, 255, 255, .08);
  border-radius: 9px;
  background: var(--bg-input);
  color: var(--text);
  font-size: 14px;
  font-weight: 600;
  cursor: pointer;
}
.music-notice-actions button:hover {
  background: rgba(46, 166, 255, 0.14);
  border-color: var(--accent);
}
.music-notice-enter-active,
.music-notice-leave-active {
  transition: opacity 0.14s ease, transform 0.14s ease;
}
.music-notice-enter-from,
.music-notice-leave-to {
  opacity: 0;
  transform: scale(0.96);
}
@keyframes music-disc-spin {
  to {
    transform: rotate(360deg);
  }
}
/* 3 格或 4 格均由 flex:1 自动等分；台式机隐藏电池格后自然三等分 */
.tm-cell {
  flex: 1 1 0;
  min-width: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 1px;
  padding: 0 4px;
  border-right: none;
}
.tm-label {
  font-size: 9px;
  line-height: 1;
  color: var(--text-dim);
  letter-spacing: 0.5px;
}
.tm-value {
  font-size: 13px;
  font-weight: 600;
  line-height: 1.1;
  color: var(--text);
  font-variant-numeric: tabular-nums; /* 数字等宽，数值变化不跳动 */
  white-space: nowrap;
}
/* 温度告警 */
.tm-value.t-hot {
  color: var(--danger);
}
.tm-value.t-warm {
  color: var(--accent-2);
}
/* 电源语义色 */
.tm-value.p-charge {
  color: var(--ok); /* 充电：绿 */
}
.tm-value.p-discharge {
  color: var(--dc-accent); /* 放电：琥珀 */
}
.tm-value.p-zero {
  color: var(--text-dim); /* 无充放 */
}
</style>
