<script setup lang="ts">
import { computed, onActivated, onBeforeUnmount, onDeactivated, onMounted, ref } from 'vue';
import Toggle from '@/components/Toggle.vue';
import { on } from '@/bridge/ipc';
import { getAutomaticSleepRecovery, setAutomaticSleepRecovery, type AutomaticSleepRecoveryStatus } from '@/bridge/sleepRecovery';

const props = defineProps<{ disabled?: boolean }>();
const emit = defineEmits<{ (event: 'saving', value: boolean): void }>();
const enabled = ref(true); // default on; native stored user choice remains authoritative
const saving = ref(false);
const error = ref('');
const status = ref<AutomaticSleepRecoveryStatus | null>(null);
const description = computed(() => {
  if (error.value) return error.value;
  if (enabled.value && status.value?.reason === 'log-write-failed') return '本轮 Steam 恢复未确认，且诊断保存失败；请检查日志目录权限。';
  if (enabled.value && status.value?.phase === 'blocked') return status.value.steamRecovery?.associationVerified ? 'Steam 游戏关联保留，但输入恢复未确认；诊断已留在后台日志。' : '本轮 Steam 恢复未确认，诊断已留在后台日志；不会重复启动游戏。';
  if (enabled.value && status.value?.phase === 'game-wait') return '正在等待原游戏睡眠暂停释放，再恢复 Steam 输入归属；不会解除手动暂停。';
  if (enabled.value && status.value?.phase === 'session-wait') return '正在等待同一 Steam 会话重新就绪；原游戏未被替换，不会重开游戏或绕过账户校验。';
  if (enabled.value && status.value?.phase === 'settle-wait') return '正在等待 Steam 手柄重连并复查原游戏状态；短暂焦点匹配不会提前结束检查。';
  if (enabled.value && ['waiting', 'game-wait', 'session-wait', 'settle-wait', 'inspecting', 'confirm-wait', 'verifying'].includes(status.value?.phase || '')) return '正在后台核对原游戏与 Steam 输入归属，必要时返回原游戏并刷新大屏列表。';
  if (enabled.value && status.value?.steamInputRecovery?.contextMatched) return '本轮 Steam 原游戏输入归属已核对；实际游戏内手柄操作仍待实测确认。';
  if (enabled.value && status.value?.steamRecovery?.associationVerified) return '本轮 Steam 原游戏关联已核对，但手柄输入尚未确认；原因已保存在后台日志。';
  return '唤醒后核对原游戏与 Steam 输入归属';
});
let active = false;
let revision = 0;
let unsubscribe: (() => void) | undefined;
async function refresh(): Promise<void> {
  const turn = ++revision;
  try {
    const value = await getAutomaticSleepRecovery();
    if (!active || turn !== revision || saving.value) return;
    enabled.value = value.enabled; status.value = value; error.value = '';
  } catch (e) { if (active && turn === revision) error.value = (e as Error).message; }
}
async function change(value: boolean): Promise<void> {
  if (!active || saving.value || props.disabled || value === enabled.value) return;
  const turn = ++revision; saving.value = true; emit('saving', true); error.value = '';
  try {
    const result = await setAutomaticSleepRecovery(value);
    if (!active || turn !== revision) return;
    if (!result.ok) throw new Error('自动修复开关保存失败，请重试。');
    enabled.value = result.enabled; status.value = result;
  } catch (e) { if (active && turn === revision) error.value = `${(e as Error).message}；请刷新确认开关状态。`; }
  finally { if (active && turn === revision) { saving.value = false; emit('saving', false); } }
}
function activate(): void {
  if (active) return; active = true;
  unsubscribe = on<AutomaticSleepRecoveryStatus>('sleep.recovery-auto', value => {
    if (!active || saving.value) return;
    ++revision; status.value = value; enabled.value = value.enabled; error.value = ''; saving.value = false;
  });
  void refresh(); // Memory/settings only. No diagnostic run or repair on mounting/enabling.
}
function deactivate(): void { active = false; ++revision; unsubscribe?.(); unsubscribe = undefined; if (saving.value) emit('saving', false); saving.value = false; }
onMounted(activate); onActivated(activate); onDeactivated(deactivate); onBeforeUnmount(deactivate);
</script>

<template>
  <div class="sleep-recovery-auto">
    <Toggle
      :model-value="enabled"
      label="睡眠唤醒 Steam 自动修复"
      :description="description"
      :disabled="disabled || saving"
      @update:model-value="change"
    />
  </div>
</template>
