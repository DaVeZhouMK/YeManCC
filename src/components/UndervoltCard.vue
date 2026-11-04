<script setup lang="ts">
import { computed, onActivated, onBeforeUnmount, onDeactivated, onMounted, ref } from 'vue';
import Slider from './Slider.vue';
import { probeUndervolt, setUndervolt, type UvProbe, type UvSetReceipt, type UvUnit } from '@/bridge/cpuctl';

const root = ref<HTMLElement | null>(null);
const capability = ref<UvProbe | null>(null);
const draft = ref(0), baseline = ref(0), observed = ref<number | null>(null);
const observedPlanes = ref<Partial<Record<string, number>>>({});
const receipt = ref<UvSetReceipt | null>(null);
const submitted = ref<{ value: number; unit: UvUnit } | null>(null);
const busy = ref(false), error = ref('');
let active = false, generation = 0;
const visible = computed(() => {
  const p = capability.value;
  return p?.supported === true && p.available === true &&
    (p.unit === 'co_step' || p.unit === 'mv') && p.offset.step > 0 &&
    (p.vendor === 'amd' || p.scope.planes.length > 0);
});
const scope = computed(() => capability.value?.vendor === 'amd' ? '全核 Curve Optimizer' : 'Intel FIVR 电压域');
const planeNames: Record<string, string> = { core: '核心', cache: '缓存' };
const planes = computed(() => (capability.value?.scope.planes ?? []).map(p => planeNames[p] ?? p).join('、'));
const dirty = computed(() => draft.value !== baseline.value);
const canApply = computed(() => {
  const p = capability.value;
  return visible.value && !busy.value && p !== null && Number.isSafeInteger(draft.value) &&
    draft.value >= p.offset.min && draft.value <= p.offset.max &&
    Math.abs((draft.value - p.offset.min) / p.offset.step - Math.round((draft.value - p.offset.min) / p.offset.step)) < 1e-7;
});
function format(value: number, valueUnit: UvUnit = capability.value?.unit ?? '') {
  const label = valueUnit === 'co_step' ? 'CO 步进' : valueUnit === 'mv' ? 'mV' : '';
  return `${value > 0 ? '+' : ''}${value} ${label}`.trim();
}
const readbackText = computed(() => {
  if (observed.value !== null) return format(observed.value);
  return capability.value?.readbackCapability === false
    ? '无法回读当前值（此通道不提供当前偏移读回）' : '无法回读当前值';
});
const receiptText = computed(() => {
  const request = submitted.value, r = receipt.value;
  if (!request) return '尚未提交本次请求';
  const value = format(request.value, request.unit);
  if (busy.value) return `请求 ${value} 正在等待回执`;
  if (!r) return `请求 ${value} 未取得与本次请求匹配的有效回执 · 实际应用未知`;
  if (!r.accepted && r.vendor === 'intel' && Object.keys(r.planes ?? {}).length > 0)
    return `请求 ${value} 失败 · 已收到域写请求回执 · 实际偏移未知`;
  if (!r.accepted) return r.applicationState === 'failed'
    ? `请求 ${value} 失败 · 已收到本次失败回执 · 当前偏移未知`
    : `请求 ${value} 未确认 · 已收到本次未确认回执 · 实际应用未知`;
  return r.vendor === 'intel'
    ? `请求 ${value}：驱动写请求已提交 · 固件是否接受及实际偏移未知（无写后读回）`
    : `请求 ${value} 已接受 · 实际应用尚未确认（无写后读回）`;
});
function routeLabel(route: { transport: string; command: number }) {
  return `${route.transport.toUpperCase()} 0x${route.command.toString(16).toUpperCase().padStart(2, '0')}`;
}
const attemptDetails = computed(() => (receipt.value?.attempts ?? []).map((attempt, index) => {
  const status = attempt.origin === 'firmware_response' && attempt.mailboxStatus !== null
    ? `固件响应 0x${attempt.mailboxStatus.toString(16).toUpperCase().padStart(2, '0')}`
    : '未取得固件响应';
  return `尝试 ${index + 1}：${routeLabel(attempt)} · ${status} · ${attempt.accepted ? '已接受' : '未确认接受'}`;
}));
const routeText = computed(() => receipt.value?.route ? `最终尝试通道：${routeLabel(receipt.value.route)}` : '');
const planeWriteDetails = computed(() => (['core', 'cache'] as const).flatMap(name => {
  const plane = receipt.value?.planes?.[name];
  return plane ? [`${planeNames[name]}${plane.ioctl_written ? '请求已写入（仅驱动写请求）' : '写入失败'} · 实际偏移未知`] : [];
}));
const planeReadbacks = computed(() => Object.entries(observedPlanes.value)
  .map(([name, value]) => `${planeNames[name] ?? name} ${format(value!)}`).join(' · '));
async function activate() {
  if (active) return;
  active = true;
  const epoch = ++generation;
  capability.value = null; receipt.value = null; submitted.value = null; observed.value = null;
  observedPlanes.value = {}; error.value = ''; busy.value = false;
  try {
    const next = await probeUndervolt();
    if (!active || epoch !== generation) return;
    capability.value = next;
    observed.value = next.currentVerified;
    observedPlanes.value = next.readback ? { ...next.planes } : {};
    const raw = next.currentVerified ?? 0, step = next.offset.step;
    if (step > 0) draft.value = baseline.value = Math.max(next.offset.min, Math.min(next.offset.max,
      next.offset.min + Math.round((raw - next.offset.min) / step) * step));
  } catch { /* Unknown or unavailable voltage capability remains hidden. */ }
}
function suspend() {
  if (root.value) root.value.dispatchEvent(new CustomEvent('gp:slider-edit-invalidated', { bubbles: true }));
  active = false; ++generation; capability.value = null;
  receipt.value = null; submitted.value = null; observed.value = null;
  observedPlanes.value = {}; error.value = ''; busy.value = false;
}
function cancel() { if (!busy.value) draft.value = baseline.value; }
function back(event: Event) {
  if (!active || busy.value || event.defaultPrevented || !dirty.value || !root.value?.contains(document.activeElement) ||
      document.querySelector('.dd-open,[data-gp-modal]')) return;
  if (event instanceof KeyboardEvent && event.key !== 'Escape') return;
  event.preventDefault(); cancel();
}
async function apply() {
  const target = capability.value;
  if (!active || !canApply.value || !target) return;
  const epoch = generation, value = draft.value;
  busy.value = true; error.value = ''; receipt.value = null;
  submitted.value = { value, unit: target.unit };
  // A pre-write observation cannot establish the new offset, even after a failed/partial request.
  observed.value = null; observedPlanes.value = {};
  try {
    const result = await setUndervolt(value, target);
    if (!active || epoch !== generation) return;
    receipt.value = result.receipt ?? null;
    if (!result.ok || !result.accepted || !result.receipt) {
      error.value = result.msg || '电压请求未取得接受回执；当前值未知'; return;
    }
    baseline.value = value;
  } catch {
    if (active && epoch === generation) error.value = 'CPU 电压偏移请求未取得有效回执；实际应用未知';
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
  <section v-if="visible && capability" ref="root" class="card cpu-setting" aria-label="电压偏移" data-cpu-setting="voltage" data-gp-slider-draft :aria-busy="busy" @gp:slider-edit-cancel="cancel">
    <div class="cpu-setting-heading"><h3 class="card-title">电压偏移</h3><span>{{ scope }}</span></div>
    <p v-if="planes" class="cpu-setting-hint">作用域：{{ planes }}，使用相同偏移。</p>
    <p v-if="capability.vendor === 'amd'" class="cpu-setting-hint">SMU 通道可读；是否接受全核 CO 由本次写入回执确认。TDP 可用不代表支持电压偏移。</p>
    <Slider v-model="draft" :min="capability.offset.min" :max="capability.offset.max" :step="capability.offset.step" :disabled="busy" :accelerate="false" :label="receipt?.accepted && !dirty ? '上次接受偏移' : '待提交偏移'" aria-label="电压偏移待提交值" :value-text="format(draft)" />
    <div class="cpu-setting-controls">
      <button type="button" :disabled="!canApply" data-cpu-apply="voltage" @click="apply">{{ busy ? '提交中' : '应用' }}</button>
      <button type="button" :disabled="busy || !dirty" @click="cancel">取消</button>
    </div>
    <div class="cpu-setting-status" role="status" aria-live="polite">
      <span>当前回读：{{ readbackText }}</span>
      <span>{{ receiptText }}</span>
      <span v-for="(detail, index) in attemptDetails" :key="index">{{ detail }}</span>
      <span v-if="routeText">{{ routeText }}</span>
      <span v-for="(detail, index) in planeWriteDetails" :key="`plane-${index}`">{{ detail }}</span>
      <span v-if="planeReadbacks">{{ planeReadbacks }}</span>
    </div>
    <p v-if="error" class="cpu-setting-error" role="alert">{{ error }}</p>
    <p class="cpu-setting-hint">仅点击应用时提交静态偏移；取消不提交。命令回执与当前值读回分别显示；无读回时不能确认实际偏移或稳定性。打开设置不会自动套用。</p>
  </section>
</template>

<style scoped>
.cpu-setting{display:flex;flex-direction:column;gap:10px;padding:12px;border:1px solid var(--console-line,color-mix(in srgb,var(--text-dim) 22%,transparent));border-radius:10px;background:var(--console-surface-alt,var(--bg-panel));min-width:0}
.cpu-setting-heading{display:flex;align-items:center;justify-content:space-between;gap:8px}.card-title{margin:0;font-size:14px;font-weight:650;color:var(--text)}.cpu-setting-heading>span{font-size:12px;color:var(--text-dim)}
.cpu-setting-hint,.cpu-setting-status{margin:0;font-size:12px;line-height:1.55;color:var(--text-dim)}.cpu-setting-status{display:flex;flex-wrap:wrap;gap:4px 18px}.cpu-setting-status>span{min-width:0;overflow-wrap:anywhere}.cpu-setting-controls{display:flex;flex-wrap:wrap;align-items:center;gap:8px}
button{min-height:36px;padding:6px 14px;border:1px solid var(--console-line,color-mix(in srgb,var(--text-dim) 28%,transparent));border-radius:7px;background:var(--bg-input);color:var(--text);font:inherit;font-size:13px;cursor:pointer}button:disabled{opacity:.45;cursor:default}button:focus-visible,button.focused{outline:2px solid var(--accent);outline-offset:2px}
.cpu-setting-error{margin:0;color:var(--danger);font-size:12px;line-height:1.5}
</style>
