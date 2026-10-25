<script setup lang="ts">
import { getCurrentInstance, nextTick, onBeforeUnmount, ref } from 'vue';
import AppIcon from './AppIcon.vue';
import { focusGamepadElement } from '@/gamepad/focus';
import {
  deleteCurrentDiagnosticLogs, isDiagnosticLogMaintenanceBusy,
  DIAGNOSTIC_LOG_DELETE_SCOPE, DIAGNOSTIC_LOG_PRESERVED_SCOPE,
} from '@/bridge/diagnosticLogMaintenance';

const props = withDefaults(defineProps<{ disabled?: boolean }>(), { disabled: false });
const emit = defineEmits<{
  (event: 'busy-change', busy: boolean): void;
  (event: 'changed'): void;
  (event: 'status', status: string): void;
  (event: 'error', error: string): void;
}>();
const showConfirmation = ref(false);
const busy = ref(false);
const trigger = ref<HTMLButtonElement | null>(null);
const cancelButton = ref<HTMLButtonElement | null>(null);
const descriptionId = 'diagnostic-log-delete-description-' + getCurrentInstance()!.uid;
let mounted = true;

function unlisten(): void {
  window.removeEventListener('ipc:gamepad-back', back);
  window.removeEventListener('keydown', keydown, true);
}
async function open(): Promise<void> {
  if (props.disabled || busy.value || isDiagnosticLogMaintenanceBusy()) return;
  showConfirmation.value = true;
  window.addEventListener('ipc:gamepad-back', back);
  window.addEventListener('keydown', keydown, true);
  await nextTick();
  if (mounted && showConfirmation.value) focusGamepadElement(cancelButton.value);
}
async function close(): Promise<void> {
  if (busy.value) return;
  showConfirmation.value = false;
  unlisten();
  await nextTick();
  if (mounted) focusGamepadElement(trigger.value);
}
function back(event: Event): void {
  if (!showConfirmation.value) return;
  event.preventDefault();
  void close();
}
function keydown(event: KeyboardEvent): void {
  if (event.key !== 'Escape' || !showConfirmation.value) return;
  event.preventDefault();
  event.stopPropagation();
  void close();
}
async function confirm(): Promise<void> {
  if (props.disabled || busy.value || isDiagnosticLogMaintenanceBusy()) return;
  busy.value = true;
  emit('busy-change', true);
  emit('error', '');
  emit('status', '正在临时停写、删除当前日志并恢复原日志开关…');
  try {
    const result = await deleteCurrentDiagnosticLogs();
    emit('status', result.status);
    emit('error', result.error);
    if (result.changed) emit('changed');
  } finally {
    busy.value = false;
    emit('busy-change', false);
    if (mounted) await close();
  }
}
onBeforeUnmount(() => { mounted = false; unlisten(); });
</script>

<template>
  <button ref="trigger" type="button" class="diagnostic-log-delete-button" :disabled="disabled || busy"
    :aria-busy="busy" aria-haspopup="dialog" :aria-expanded="showConfirmation" @click="open">删除日志</button>
  <Teleport to="body">
    <div v-if="showConfirmation" class="diagnostic-log-delete-shade" @click.self="close">
      <section class="diagnostic-log-delete-dialog" role="dialog" aria-modal="true" aria-label="删除当前诊断日志"
        :aria-describedby="descriptionId" :aria-busy="busy" data-gp-modal data-gp-page-lock>
        <h2><AppIcon name="warning" />删除当前诊断日志</h2>
        <div :id="descriptionId" class="diagnostic-log-delete-description">
          <p>临时停止写入并确认后，删除当前文件，再恢复原来的日志开关。原本关闭的开关会保持关闭。</p>
          <p><strong>{{ DIAGNOSTIC_LOG_DELETE_SCOPE }}</strong>。</p>
          <p>{{ DIAGNOSTIC_LOG_PRESERVED_SCOPE }}。</p>
          <p>删除不可撤销；暂停确认失败时不会删除，删除失败也会尝试恢复原开关。</p>
        </div>
        <p v-if="busy" role="status" aria-live="polite">正在处理，请稍候…</p>
        <div class="diagnostic-log-delete-actions">
          <button ref="cancelButton" type="button" :disabled="busy" data-gp-row="0" data-gp-col="0" @click="close"><b>B</b> 取消</button>
          <button type="button" class="diagnostic-log-delete-confirm" :disabled="disabled || busy"
            data-gp-row="0" data-gp-col="1" @click="confirm">确认删除</button>
        </div>
      </section>
    </div>
  </Teleport>
</template>

<style scoped>
.diagnostic-log-delete-button{width:100%;height:34px;min-width:0;box-sizing:border-box;padding:0 12px;border:1px solid var(--border);border-radius:8px;background:var(--bg-input);color:var(--danger);font:inherit;font-size:14px;font-weight:600;white-space:nowrap;cursor:pointer}
.diagnostic-log-delete-button:hover:not(:disabled),.diagnostic-log-delete-confirm:hover:not(:disabled){border-color:var(--danger);background:color-mix(in srgb,var(--danger) 10%,var(--bg-input))}
.diagnostic-log-delete-button:disabled,.diagnostic-log-delete-actions button:disabled{opacity:.55;cursor:default}
.diagnostic-log-delete-shade{position:fixed;inset:0;z-index:1500;display:grid;place-items:center;padding:20px;box-sizing:border-box;background:rgba(0,0,0,.45)}
.diagnostic-log-delete-dialog{width:min(480px,100%);max-height:calc(100dvh - 40px);overflow:hidden;box-sizing:border-box;border:1px solid var(--border);border-radius:12px;padding:18px 20px;background:var(--bg-panel);box-shadow:0 16px 40px #0008;display:flex;flex-direction:column;gap:12px;color:var(--text)}
.diagnostic-log-delete-dialog h2{flex-shrink:0;margin:0;display:flex;align-items:center;gap:9px;font-size:17px}.diagnostic-log-delete-dialog h2 :deep(svg){width:20px;height:20px;color:var(--danger)}
.diagnostic-log-delete-description{display:grid;gap:8px;min-height:0;overflow-y:auto}.diagnostic-log-delete-dialog p{margin:0;font-size:14px;line-height:1.65;color:var(--text-dim)}.diagnostic-log-delete-dialog strong{color:var(--text)}
.diagnostic-log-delete-actions{flex-shrink:0;display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:3px}.diagnostic-log-delete-actions button{min-height:44px;border:1px solid var(--border);border-radius:9px;background:var(--bg-input);color:var(--text);font:inherit;font-size:14px;font-weight:600;cursor:pointer}.diagnostic-log-delete-actions .diagnostic-log-delete-confirm{color:var(--danger);border-color:color-mix(in srgb,var(--danger) 45%,transparent);background:color-mix(in srgb,var(--danger) 12%,var(--bg-input))}.diagnostic-log-delete-actions b{margin-right:5px}
</style>
