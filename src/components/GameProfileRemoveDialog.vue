<script setup lang="ts">
import { nextTick, onMounted, onBeforeUnmount, ref } from 'vue';
import AppIcon from './AppIcon.vue';
import { focusGamepadElement } from '@/gamepad/focus';
defineProps<{ gameName: string; busy: boolean; error?: string }>();
const emit = defineEmits<{ (e: 'cancel'): void; (e: 'confirm'): void }>();
const cancel = ref<HTMLButtonElement | null>(null);
function back(event: Event): void { event.preventDefault(); emit('cancel'); }
onMounted(() => { window.addEventListener('ipc:gamepad-back', back); void nextTick(() => focusGamepadElement(cancel.value)); });
onBeforeUnmount(() => window.removeEventListener('ipc:gamepad-back', back));
</script>
<template>
  <Teleport to="body"><div class="profile-remove-shade" @click.self="!busy && emit('cancel')">
    <section class="profile-remove-dialog" role="dialog" aria-modal="true" aria-labelledby="profile-remove-title" aria-describedby="profile-remove-description" data-gp-modal data-gp-page-lock data-gp-game-quick-dialog>
      <h2 id="profile-remove-title"><AppIcon name="warning" />解除专属游戏模式</h2>
      <p id="profile-remove-description">删除 <strong>{{ gameName }}</strong> 的专属配置？性能、虚拟手柄与陀螺仪将恢复使用公用设置。</p>
      <p v-if="error" class="remove-error" role="alert">{{ error }}</p>
      <div class="profile-remove-actions"><button ref="cancel" :disabled="busy" data-gp-row="0" data-gp-col="0" @click="emit('cancel')"><b>B</b> 取消</button><button class="confirm" :disabled="busy" data-gp-row="0" data-gp-col="1" @click="emit('confirm')">{{ busy ? '正在解除…' : '确认解除' }}</button></div>
    </section>
  </div></Teleport>
</template>
<style scoped>
.profile-remove-shade{position:fixed;inset:0;z-index:1500;display:grid;place-items:center;padding:20px;box-sizing:border-box;background:rgba(0,0,0,.45)}
.profile-remove-dialog{width:min(440px,100%);max-height:calc(100vh - 40px);overflow-y:auto;box-sizing:border-box;border:1px solid var(--border);border-radius:12px;padding:18px 20px;background:var(--bg-panel);box-shadow:0 16px 40px #0008;display:flex;flex-direction:column;gap:12px;color:var(--text)}
h2{margin:0;display:flex;align-items:center;gap:9px;font-size:17px}h2 :deep(svg){width:20px;height:20px;color:var(--danger)}p{margin:0;font-size:14px;line-height:1.65;color:var(--text-dim)}p strong{color:var(--text);overflow-wrap:anywhere}.remove-error{color:var(--danger)}
.profile-remove-actions{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:3px}.profile-remove-actions button{min-height:44px;border:1px solid var(--border);border-radius:9px;background:var(--bg-input);color:var(--text);font:inherit;font-size:14px;font-weight:600;cursor:pointer}.profile-remove-actions .confirm{color:var(--danger);border-color:color-mix(in srgb,var(--danger) 45%,transparent);background:color-mix(in srgb,var(--danger) 12%,var(--bg-input))}.profile-remove-actions button:disabled{opacity:.55}.profile-remove-actions b{margin-right:5px}
</style>
