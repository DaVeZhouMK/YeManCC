<script setup lang="ts">
import { ref, onMounted } from 'vue';
import SegButton from '@/components/SegButton.vue';
import Toggle from '@/components/Toggle.vue';
import { probeUndervolt, setUndervolt, UV_PRESETS, uvPresetValue } from '@/bridge/cpuctl';
import { readCpuAutostart, writeUvAutostart } from '@/bridge/autostart';
import InlineIcon from '@/components/InlineIcon.vue';

// 打开自动验证：本机支持降压才显示，否则整块隐藏
const supported = ref(false);
const vendor = ref<'amd' | 'intel' | ''>('amd');
const uvKey = ref('off');
const uvCurrent = ref(0);
const uvBusy = ref(false);
const autostart = ref(false);
const errMsg = ref('');

const UV_OPTS = UV_PRESETS.map((p) => ({ value: p.key, label: p.label }));

onMounted(async () => {
  const p = await probeUndervolt();
  supported.value = p.supported;
  vendor.value = p.vendor;
  const cfg = await readCpuAutostart();
  autostart.value = cfg.uv.enabled;
  if (cfg.uv.vendor) vendor.value = cfg.uv.vendor;
  // 优先用记录的档位展示当前值：AMD 无真读回（probe 的 current 恒 0），以配置为准
  const valid = UV_PRESETS.some((x) => x.key === cfg.uv.preset);
  if (valid) {
    uvKey.value = cfg.uv.preset;
    uvCurrent.value = uvPresetValue(vendor.value, cfg.uv.preset);
  } else {
    uvKey.value = p.current === 0 ? 'off' : 'balance';
    uvCurrent.value = p.current;
  }
});

async function onUv(key: string) {
  if (uvBusy.value) return;
  uvBusy.value = true;
  errMsg.value = '';
  const v = uvPresetValue(vendor.value, key);
  try {
    const r = await setUndervolt(v);
    if (r.ok) {
      uvKey.value = key;
      uvCurrent.value = v;
      // 无论自动应用开关是否开启都记录档位（AMD 无真读回, 靠配置恢复当前值）
      try {
        await writeUvAutostart(autostart.value, key, vendor.value);
      } catch {
        /* 持久化失败不影响应用结果 */
      }
    } else {
      errMsg.value = '降压失败：' + r.msg;
    }
  } catch (e) {
    errMsg.value = '降压失败：' + (e as Error).message;
  } finally {
    uvBusy.value = false;
  }
}

// 「30秒自行启用」开关：开启时按当前选择的降压档记录，开机 30 秒后自动套用
async function onAutostart(v: boolean) {
  autostart.value = v;
  try {
    await writeUvAutostart(v, uvKey.value, vendor.value);
  } catch {
    /* 忽略写入失败 */
  }
}
</script>

<template>
  <section v-if="supported" class="card">
    <div class="card-head">
      <h3 class="card-title"><InlineIcon name="bolt" /> CPU 降压</h3>
      <Toggle
        :model-value="autostart"
        :label="'打开软件自动应用'"
        color="accent"
        :disabled="uvBusy"
        @update:model-value="onAutostart"
        compact
      />
    </div>
    <div v-if="errMsg" class="err-bar">{{ errMsg }}</div>
    <div class="uv-row">
      <SegButton
        :model-value="uvKey"
        :options="UV_OPTS"
        color="accent"
        full
        :disabled="uvBusy"
        @update:model-value="(v: string) => onUv(v)"
      />
    </div>
    <div class="uv-hint">
      当前：{{ uvCurrent === 0 ? '未降压' : uvCurrent + (vendor === 'intel' ? ' mV' : ' CO') }}
      ｜安全档可长期开，风险档建议压测确认
    </div>
    <div class="hint">勾选后，打开程序 30 秒自动套用所选降压档；未勾选即使有选项也不操作。</div>
  </section>
</template>

<style scoped>
.card {
  background: color-mix(in srgb, var(--bg-panel) 72%, transparent);
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
.uv-row {
  margin-top: 4px;
}
.uv-hint {
  font-size: 11px;
  color: var(--text-dim, #8a97a8);
  margin-top: 10px;
  line-height: 1.4;
}
</style>
