<script setup lang="ts">
import { computed, onActivated, onBeforeUnmount, onDeactivated, onMounted, ref } from 'vue';
import Dropdown from './Dropdown.vue';
import { probeCcd, setCcdMode, type CcdProbe, type CcdSetReceipt } from '@/bridge/cpuctl';

const root = ref<HTMLElement | null>(null);
const capability = ref<CcdProbe | null>(null);
const draft = ref<number | string>(''), baseline = ref<number | string>('');
const receipt = ref<CcdSetReceipt | null>(null);
const busy = ref(false), error = ref('');
let active = false, generation = 0;
const visible = computed(() => capability.value?.supported === true && capability.value.available === true &&
  capability.value.operation === 'process_affinity' && !capability.value.physicalCcdPowerOff);
const options = computed(() => [
  { value: 0, label: 'CCD · 全部' },
  ...(capability.value?.domains ?? []).map(domain => ({
    value: domain.index, label: `CCD · ${domain.index}`, sub: `${domain.logical} 个逻辑处理器`,
  })),
]);
const dirty = computed(() => draft.value !== baseline.value);
const canApply = computed(() => visible.value && !busy.value && typeof draft.value === 'number' &&
  options.value.some(option => option.value === draft.value));
const requested = computed(() => options.value.find(option => option.value === baseline.value)?.label ?? '尚未提交');
const unconfirmedText = computed(() => {
  const a = receipt.value?.affected;
  if (!a) return '';
  return [
    ['无法打开', a.openFailed], ['设置失败', a.setFailed], ['读回不匹配', a.verifyFailed],
    ['原状态未取得', a.captureFailed ?? 0], ['代次已变化', a.staleAborted ?? 0],
  ].filter(([, count]) => Number(count) > 0).map(([label, count]) => `${label} ${count}`).join(' · ');
});
const resultText = computed(() => {
  const r = receipt.value;
  if (!r) return '尚无本次应用回执';
  if (r.applicationState === 'applied') return '首轮目标已回读';
  if (r.applicationState === 'failed') return '已接受请求，目标应用失败';
  if (r.applicationState === 'pending') return '已接受请求，首轮目标尚未全部确认';
  return '已接受请求，应用状态未知';
});
async function activate() {
  if (active) return;
  active = true;
  const epoch = ++generation;
  capability.value = null; receipt.value = null; error.value = ''; busy.value = false;
  draft.value = baseline.value = '';
  try {
    const next = await probeCcd();
    if (!active || epoch !== generation) return;
    capability.value = next;
    const mode = next.lease?.active ? next.lease.mode : undefined;
    if (typeof mode === 'number' && options.value.some(option => option.value === mode))
      draft.value = baseline.value = mode;
  } catch { /* An unknown capability has no Settings control. */ }
}
function suspend() { active = false; ++generation; capability.value = null; }
function cancel() { if (!busy.value) draft.value = baseline.value; }
function back(event: Event) {
  if (!active || busy.value || event.defaultPrevented || !dirty.value || !root.value?.contains(document.activeElement) ||
      document.querySelector('.dd-open,[data-gp-modal]')) return;
  if (event instanceof KeyboardEvent && event.key !== 'Escape') return;
  event.preventDefault(); cancel();
}
async function apply() {
  if (!canApply.value) return;
  const epoch = generation, mode = Number(draft.value);
  busy.value = true; error.value = ''; receipt.value = null;
  try {
    const result = await setCcdMode(mode);
    if (!active || epoch !== generation) return;
    if (!result.ok || !result.receipt) { error.value = result.msg || 'CCD 请求未取得接受回执'; return; }
    receipt.value = result.receipt; baseline.value = mode;
  } catch (e) {
    if (active && epoch === generation) error.value = (e as Error).message || 'CCD 请求失败';
  } finally { if (active && epoch === generation) busy.value = false; }
}
onMounted(() => {
  window.addEventListener('ipc:gamepad-back', back, true);
  window.addEventListener('keydown', back, true);
  void activate();
});
onActivated(() => { void activate(); });
onDeactivated(suspend);
onBeforeUnmount(() => {
  suspend();
  window.removeEventListener('ipc:gamepad-back', back, true);
  window.removeEventListener('keydown', back, true);
});
</script>

<template>
  <section v-if="visible" ref="root" class="card cpu-setting" aria-label="CCD 调度" data-cpu-setting="ccd" :aria-busy="busy">
    <div class="cpu-setting-heading"><h3 class="card-title">CCD 调度</h3><span>进程范围</span></div>
    <p class="cpu-setting-hint">按 L3 缓存域限制可管理进程的调度范围，不等同固件关闭或物理断电。当前游戏的专属 CPU 策略优先。</p>
    <div class="cpu-setting-controls">
      <Dropdown :model-value="draft" :options="options" :disabled="busy" placeholder="请选择范围" aria-label="CCD 调度范围" @update:model-value="value => draft = value" />
      <button type="button" :disabled="!canApply" data-cpu-apply="ccd" @click="apply">{{ busy ? '提交中' : '应用' }}</button>
      <button type="button" :disabled="busy || !dirty" @click="cancel">取消</button>
    </div>
    <div class="cpu-setting-status" role="status" aria-live="polite">
      <span>上次请求：{{ requested }}</span><span>{{ resultText }}</span>
      <span v-if="receipt">首轮亲和性读回匹配 {{ receipt.affected.verified }} / {{ receipt.affected.attempted }} 个目标</span>
      <span v-if="unconfirmedText">首轮未确认：{{ unconfirmedText }}</span>
      <span v-if="receipt">{{ receipt.backgroundRefresh ? '后台继续跟进后续进程；此处显示首轮回执' : '后台跟进未启动' }}</span>
    </div>
    <p v-if="error" class="cpu-setting-error" role="alert">{{ error }}</p>
    <p class="cpu-setting-hint">选择后点击应用才提交；取消不提交，打开设置不会自动套用。</p>
  </section>
</template>

<style scoped>
.cpu-setting{display:flex;flex-direction:column;gap:10px;padding:12px;border:1px solid var(--console-line,color-mix(in srgb,var(--text-dim) 22%,transparent));border-radius:10px;background:var(--console-surface-alt,var(--bg-panel));min-width:0}
.cpu-setting-heading{display:flex;align-items:center;justify-content:space-between;gap:8px}.card-title{margin:0;font-size:14px;font-weight:650;color:var(--text)}.cpu-setting-heading>span{font-size:12px;color:var(--text-dim)}
.cpu-setting-hint,.cpu-setting-status{margin:0;font-size:12px;line-height:1.55;color:var(--text-dim)}.cpu-setting-status{display:flex;flex-wrap:wrap;gap:4px 18px}.cpu-setting-controls{display:flex;align-items:center;gap:8px;min-width:0}.cpu-setting-controls>:first-child{flex:1;min-width:0}
button{flex-shrink:0;min-height:36px;padding:6px 14px;border:1px solid var(--console-line,color-mix(in srgb,var(--text-dim) 28%,transparent));border-radius:7px;background:var(--bg-input);color:var(--text);font:inherit;font-size:13px;cursor:pointer}button:disabled{opacity:.45;cursor:default}button:focus-visible,button.focused{outline:2px solid var(--accent);outline-offset:2px}
.cpu-setting-error{margin:0;color:var(--danger);font-size:12px;line-height:1.5}
</style>
