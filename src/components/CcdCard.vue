<script setup lang="ts">
import { ref, onMounted } from 'vue';
import SegButton from '@/components/SegButton.vue';
import Toggle from '@/components/Toggle.vue';
import { probeCcd, setCcdMode, type CcdMode } from '@/bridge/cpuctl';
import { readCpuAutostart, writeCcdAutostart } from '@/bridge/autostart';
import InlineIcon from '@/components/InlineIcon.vue';

// 打开自动验证：多 CCD（l3Domains>=2）才显示，否则整块隐藏
const supported = ref(false);
const ccdCount = ref(0);
const physicalCores = ref(0);
const logicalThreads = ref(0);
const ccdMode = ref<CcdMode>(0);
const ccdBusy = ref(false);
const autostart = ref(false);
const errMsg = ref('');

const ccdOptions = ref<{ value: number; label: string }[]>([]);

onMounted(async () => {
  const p = await probeCcd();
  ccdCount.value = p.l3Domains;
  physicalCores.value = p.physicalCores;
  logicalThreads.value = p.logical;
  supported.value = p.supported;
  ccdOptions.value = [
    { value: 0, label: '全核' },
    ...Array.from({ length: p.l3Domains }, (_, i) => ({ value: i + 1, label: `仅 CCD${i}` })),
  ];
  if (supported.value) {
    const cfg = await readCpuAutostart();
    autostart.value = cfg.ccd.enabled;
    const savedMode = Number(cfg.ccd.mode ?? 0);
    ccdMode.value = Number.isInteger(savedMode) && savedMode >= 0 && savedMode <= p.l3Domains ? savedMode : 0;
  }
});

async function onCcd(mode: CcdMode) {
  if (ccdBusy.value) return;
  ccdBusy.value = true;
  errMsg.value = '';
  try {
    const r = await setCcdMode(mode);
    if (r.ok) {
      ccdMode.value = mode;
      // 无论自动应用开关是否开启都记录当前档位, 避免重进页面时 UI 与实际脱节
      try {
        await writeCcdAutostart(autostart.value, mode);
      } catch {
        /* 持久化失败不影响应用结果 */
      }
    } else {
      errMsg.value = 'CCD 切换失败：' + r.msg;
    }
  } catch (e) {
    errMsg.value = 'CCD 切换失败：' + (e as Error).message;
  } finally {
    ccdBusy.value = false;
  }
}

// 「30秒自行启用」开关：开启时按当前选择的 CCD 模式记录，开机 30 秒后自动套用
async function onAutostart(v: boolean) {
  autostart.value = v;
  try {
    await writeCcdAutostart(v, ccdMode.value);
  } catch {
    /* 忽略写入失败 */
  }
}

</script>

<template>
  <section v-if="supported" class="card">
    <div class="card-head">
      <h3 class="card-title"><InlineIcon name="core" /> 核心CCD调度</h3>
      <Toggle
        :model-value="autostart"
        :label="'打开软件自动应用'"
        color="accent"
        :disabled="ccdBusy"
        @update:model-value="onAutostart"
        compact
      />
    </div>
    <div v-if="errMsg" class="err-bar">{{ errMsg }}</div>
    <div class="ccd-row">
      <SegButton
        :model-value="ccdMode"
        :options="ccdOptions"
        color="accent"
        full
        :disabled="ccdBusy"
        @update:model-value="(v: number) => onCcd(v as CcdMode)"
      />
    </div>
    <div class="hint">检测到 {{ ccdCount }} 个 L3/CCD 域<span v-if="physicalCores">，{{ physicalCores }} 物理核 / {{ logicalThreads }} 线程</span>。勾选后，打开程序 30 秒自动套用所选 CCD 模式；未勾选即使有选项也不操作。</div>
  </section>
</template>

<style scoped>
.card {
  background: var(--bg-panel);
  border-radius: var(--radius);
  padding: 12px 14px;
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
.card-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  margin-bottom: 10px;
}
.hint {
  font-size: 11px;
  color: var(--text-dim, #8a97a8);
  margin-top: 10px;
  line-height: 1.4;
}
.err-bar {
  background: rgba(229, 72, 77, 0.12);
  border: 1px solid rgba(229, 72, 77, 0.4);
  color: #ff9ea1;
  border-radius: var(--radius-ctrl);
  padding: 8px 10px;
  font-size: 11px;
  margin-bottom: 10px;
  line-height: 1.4;
}
.ok-bar {
  background: rgba(46, 166, 255, 0.12);
  border: 1px solid rgba(46, 166, 255, 0.4);
  color: #7dd3fc;
  border-radius: var(--radius-ctrl);
  padding: 8px 10px;
  font-size: 11px;
  margin-bottom: 10px;
  line-height: 1.4;
}
.ccd-row {
  margin-top: 4px;
}
</style>
