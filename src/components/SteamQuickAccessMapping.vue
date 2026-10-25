<script setup lang="ts">
import { computed, defineAsyncComponent, onActivated, onBeforeUnmount, onDeactivated, onMounted, ref } from 'vue';
import { on } from '@/bridge/ipc';
import { onInputSettingsChanged, readSettingsSection, type InputSettingsSnapshot } from '@/bridge/settingsRepository';
import { CONTROLLER_TRIGGER_OPTIONS, formatControllerShortcutInputs, readControllerShortcutRules } from '@/bridge/controllerShortcutRules';

const ShortcutEditor = defineAsyncComponent(() => import('@/views/ControllerShortcutEditorView.vue').then(module => module.default));
const props = defineProps<{ enabled: boolean }>();
const rules = ref<ReturnType<typeof readControllerShortcutRules>>([]);
const state = ref<'loading' | 'ready' | 'failed'>('loading');
const receipt = ref('');
const editorOpen = ref(false);
let active = false, epoch = 0, reading = false, dirty = false;
let unsubscribeInput: (() => void) | null = null;
let unsubscribeReceipt: (() => void) | null = null;
const mappedRules = computed(() => rules.value.filter(rule => rule.enabled && rule.inputs.length > 0));
const disabledCount = computed(() => rules.value.filter(rule => !rule.enabled).length);
const mappingText = computed(() => {
  if (state.value === 'loading') return '正在读取三点键映射…';
  if (state.value === 'failed') return '三点键映射暂未读取，点击重试';
  if (!mappedRules.value.length) return disabledCount.value ? `三点键映射已停用（${disabledCount.value} 条）` : 'Steam 三点键：尚未映射';
  return mappedRules.value.map(rule => {
    const trigger = CONTROLLER_TRIGGER_OPTIONS.find(item => item.value === rule.trigger)?.label ?? rule.trigger;
    const timing = rule.trigger === 'hold' ? ` ${rule.holdMs}ms` : '';
    return `${formatControllerShortcutInputs(rule.inputs)}（${trigger}${timing}）`;
  }).join('；');
});

function refreshMapping() {
  if (!active) return;
  dirty = true;
  if (!reading) void readMapping();
}
async function readMapping() {
  if (!active || reading || !dirty) return;
  const generation = epoch;
  reading = true;
  dirty = false;
  try {
    const input = await readSettingsSection<InputSettingsSnapshot>('input');
    if (!active || generation !== epoch) return;
    rules.value = readControllerShortcutRules(input.buttonMapping?.rules)
      .filter(rule => rule.actionId === 'input.frontendButton' && rule.params.button === 'steamQuickAccess');
    state.value = 'ready';
  } catch {
    if (active && generation === epoch) state.value = 'failed';
  } finally {
    reading = false;
    if (active && dirty) void readMapping();
  }
}
function acceptReceipt(raw: unknown) {
  if (!active || !raw || typeof raw !== 'object') return;
  const value = raw as { button?: unknown; ok?: unknown; reason?: unknown };
  if (value.button !== 5 || typeof value.ok !== 'boolean') return;
  const reasons: Record<string, string> = {
    'input-power-gate': '输入尚未就绪，请稍后重试',
    'matching-virtual-gamepad-not-ready': '对应虚拟手柄尚未就绪',
    'virtual-target-changed': '虚拟手柄已切换，请重试',
    'guide-pulse-already-pending': '上一条按键指令仍在处理',
    'action-queue-unavailable': '按键指令暂时无法提交',
    'physical-modifier-held-or-keyboard-injection-failed': '按键发送未确认，请松开修饰键后重试',
  };
  receipt.value = value.ok ? '已发送 Steam 三点键指令' : `三点键未完成：${typeof value.reason === 'string' ? reasons[value.reason] || '请检查 Steam 和输入状态' : '请检查 Steam 和输入状态'}`;
}
function closeEditor() {
  editorOpen.value = false;
  refreshMapping();
}
function start() {
  if (active) return;
  active = true;
  ++epoch;
  unsubscribeInput = onInputSettingsChanged(refreshMapping);
  unsubscribeReceipt = on<unknown>('input.frontendButton', acceptReceipt);
  window.addEventListener('ipc:controller-shortcuts.updated', refreshMapping);
  window.addEventListener('global:refresh', refreshMapping);
  refreshMapping();
}
function stop() {
  active = false;
  ++epoch;
  dirty = false;
  editorOpen.value = false;
  receipt.value = '';
  unsubscribeInput?.(); unsubscribeInput = null;
  unsubscribeReceipt?.(); unsubscribeReceipt = null;
  window.removeEventListener('ipc:controller-shortcuts.updated', refreshMapping);
  window.removeEventListener('global:refresh', refreshMapping);
}
onMounted(start);
onActivated(start);
onDeactivated(stop);
onBeforeUnmount(stop);
</script>

<template>
  <div class="steam-quick-mapping" aria-label="Steam 三点键映射">
    <div class="mapping-row">
      <span class="mapping-label">Steam 三点键</span>
      <span class="mapping-value" :class="{ mapped: state === 'ready' && mappedRules.length > 0 }" role="status" aria-live="polite">{{ mappingText }}</span>
      <button v-if="state === 'failed'" type="button" class="mapping-button" data-gp-row="0" data-gp-col="3" @click="refreshMapping">重试</button>
      <button type="button" class="mapping-button" data-gp-row="0" data-gp-col="4" @click="editorOpen = true">设置映射</button>
    </div>
    <p class="mapping-hint">在“前端呼出按钮”中选择“Steam 三点键”，用于呼出大屏快捷菜单和插件入口。{{ props.enabled ? '' : '插件商店已关闭，按键映射仍保留。' }}</p>
    <p v-if="receipt" class="mapping-receipt" role="status" aria-live="polite">{{ receipt }}</p>
    <ShortcutEditor v-if="editorOpen" @close="closeEditor" />
  </div>
</template>

<style scoped>
.steam-quick-mapping { margin-top: 10px; }
.mapping-row { display: flex; align-items: center; flex-wrap: wrap; gap: 6px 10px; }
.mapping-label { font-size: 12px; font-weight: 700; color: var(--text); }
.mapping-value { flex: 1 1 160px; min-width: 0; overflow-wrap: anywhere; font-size: 12px; color: var(--text-dim); }
.mapping-value.mapped { color: var(--accent); }
.mapping-button { flex: 0 0 auto; min-height: 30px; padding: 4px 10px; border: 1px solid var(--accent); border-radius: var(--radius-ctrl); background: var(--bg-input); color: var(--accent); font-size: 12px; font-weight: 600; cursor: pointer; }
.mapping-button:hover { background: color-mix(in srgb, var(--accent) 12%, var(--bg-input)); }
.mapping-button:focus-visible { box-shadow: var(--focus-ring); }
.mapping-hint, .mapping-receipt { margin: 6px 0 0; font-size: 11px; line-height: 1.5; color: var(--text-dim); }
.mapping-receipt { color: var(--text); }
</style>
