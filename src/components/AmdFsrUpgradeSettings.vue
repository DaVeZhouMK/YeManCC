<script setup lang="ts">
import { computed, onMounted, onActivated, onDeactivated, onBeforeUnmount, ref } from 'vue';
import Toggle from './Toggle.vue';
import { shell } from '@/bridge/api';
import { onUiVisibilityChange } from '@/bridge/uiLifecycle';
import { amdFsrState, createAmdFsrPage, amdFsrStatus, amdFsrError, AMD_FSR_DRIVER_URL, type AmdFsrAdapter } from '@/bridge/amdFsrUpgrade';
const busy = ref(false), error = ref('');
const adapters = computed(() => amdFsrState.value?.adapters ?? []);
const page = createAmdFsrPage();
let inPage = false, visible = false, active = false, generation = 0;
let stopVisibility: (() => void) | undefined;
async function execute(operation: () => ReturnType<typeof page.read>) {
  if (!active || busy.value) return;
  const epoch = generation; busy.value = true; error.value = '';
  try {
    const result = await operation();
    if (active && generation === epoch && result && !result.ok) error.value = amdFsrError(result.reason);
  } catch {
    if (active && generation === epoch) error.value = '驱动状态未确认，请重新检测。';
  } finally { if (generation === epoch) busy.value = false; }
}
function refresh() { return execute(() => page.read()); }
function setEnabled(a: AmdFsrAdapter, enabled: boolean) {
  if (!a.supported || typeof a.enabled !== 'boolean') return;
  return execute(() => page.set(a, enabled));
}
async function openDriverPage() {
  if (!active || busy.value) return;
  try { await shell.open(AMD_FSR_DRIVER_URL); }
  catch { if (active) error.value = '无法打开官方驱动下载页。'; }
}
function activate() {
  if (!inPage || !visible || active) return;
  active = true; ++generation;
  void execute(() => page.enter());
}
function suspend() {
  active = false; ++generation; busy.value = false; error.value = ''; page.close();
}
onMounted(() => {
  inPage = true;
  stopVisibility = onUiVisibilityChange(state => {
    visible = state.visible;
    if (visible) activate(); else suspend();
  });
});
onActivated(() => { inPage = true; activate(); });
onDeactivated(() => { inPage = false; suspend(); });
onBeforeUnmount(() => { inPage = false; stopVisibility?.(); stopVisibility = undefined; suspend(); });
</script>
<template>
  <section v-if="adapters.length" class="card amd-fsr-settings" aria-label="AMD FSR 全局升级">
    <div class="fsr-heading"><h3 class="card-title">AMD 核显 · FSR 全局升级</h3>
      <button type="button" :disabled="busy" data-fsr-refresh @click="refresh">{{ busy ? '检测中' : '重新检测' }}</button>
    </div>
    <div v-for="a in adapters" :key="a.id" class="fsr-adapter">
      <Toggle :model-value="a.enabled === true" :label="a.name" color="accent" data-fsr-toggle
        :aria-label="`${a.name} FSR 全局升级`" :disabled="busy || !a.supported || typeof a.enabled !== 'boolean'"
        @update:model-value="value => setEnabled(a, value)" />
      <p class="muted body" role="status">{{ amdFsrStatus(a) }}</p>
      <p v-if="a.driverVersion" class="muted body">驱动版本：{{ a.driverVersion }}</p>
      <button v-if="a.upgradeRecommended" type="button" :disabled="busy" data-fsr-driver @click="openDriverPage">检查 AMD 驱动</button>
    </div>
    <p class="muted body">仅进入设备页时检测；支持后默认开启一次，手动关闭后保留选择。FSR 4.1 等实际升级版本由 AMD 驱动决定，游戏内仍需启用兼容的 FSR 超分辨率。</p>
    <p v-if="error" class="fsr-error body" role="alert">{{ error }}</p>
  </section>
</template>
<style scoped>
.amd-fsr-settings { display: flex; flex-direction: column; gap: 12px; }
.fsr-heading { display: flex; align-items: center; justify-content: space-between; gap: 12px; }
.card-title { margin: 0; font-size: 14px; font-weight: 600; }
.fsr-adapter { display: flex; flex-direction: column; gap: 6px; }
p { margin: 0; line-height: 1.6; }
.fsr-error { color: var(--danger); }
button { align-self: flex-start; padding: 6px 12px; border: 1px solid color-mix(in srgb, var(--text-dim) 28%, transparent); border-radius: 6px; background: var(--bg-input); color: var(--text); cursor: pointer; }
button:disabled { opacity: .45; cursor: default; }
</style>
