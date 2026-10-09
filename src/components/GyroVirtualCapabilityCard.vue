<script setup lang="ts">
type CapabilityVisibility = 'hidden' | 'disabled' | 'available';

const props = withDefaults(defineProps<{
  visibility: CapabilityVisibility;
  status?: string;
  reasons?: readonly string[];
  loggingEnabled?: boolean;
  logCount?: number;
}>(), {
  status: 'UNENCLOSED',
  reasons: () => [],
  loggingEnabled: false,
  logCount: 0,
});

const emit = defineEmits<{
  (event: 'toggle-logging', enabled: boolean): void;
  (event: 'clear-logs'): void;
  (event: 'export-logs'): void;
}>();

const canInteract = () => props.visibility === 'available';
</script>

<template>
  <section
    v-if="visibility !== 'hidden'"
    class="gyro-virtual-capability-card"
    aria-label="GyroVirtual 能力状态"
    :data-visibility="visibility"
  >
    <header class="gyro-virtual-header">
      <div>
        <h3>GyroVirtual</h3>
        <p class="gyro-virtual-status">{{ status }}</p>
      </div>
      <span class="gyro-virtual-badge">{{ visibility === 'available' ? '可用' : '未开放' }}</span>
    </header>
    <p v-if="visibility !== 'available'" class="gyro-virtual-disabled">
      当前能力尚未完成运行时验收，输入后端保持关闭。
    </p>
    <ul v-if="reasons.length" class="gyro-virtual-reasons">
      <li v-for="reason in reasons" :key="reason">{{ reason }}</li>
    </ul>
    <div class="gyro-virtual-log-actions" aria-label="输入日志操作">
      <button type="button" :disabled="!canInteract()" :aria-pressed="loggingEnabled" @click="emit('toggle-logging', !loggingEnabled)">
        {{ loggingEnabled ? '日志已开启' : '开启输入日志' }}
      </button>
      <button type="button" :disabled="!canInteract() || logCount === 0" @click="emit('clear-logs')">清空日志</button>
      <button type="button" :disabled="!canInteract() || logCount === 0" @click="emit('export-logs')">导出日志</button>
    </div>
  </section>
</template>

<style scoped>
.gyro-virtual-capability-card { padding: 16px; border: 1px solid var(--border-color, #3b4351); border-radius: 12px; background: var(--bg-card, #171b24); }
.gyro-virtual-header { display: flex; align-items: center; justify-content: space-between; gap: 12px; }
.gyro-virtual-header h3 { margin: 0; }
.gyro-virtual-status { margin: 4px 0 0; color: var(--text-secondary, #9da7b8); font: 11px ui-monospace, SFMono-Regular, Consolas, monospace; }
.gyro-virtual-badge { padding: 3px 8px; border-radius: 999px; background: color-mix(in srgb, var(--warning, #d6a84f) 18%, transparent); color: var(--warning, #d6a84f); font-size: 12px; }
.gyro-virtual-disabled, .gyro-virtual-reasons { color: var(--text-secondary, #9da7b8); font-size: 12px; }
.gyro-virtual-reasons { margin: 8px 0; padding-left: 18px; }
.gyro-virtual-log-actions { display: flex; flex-wrap: wrap; gap: 8px; }
.gyro-virtual-log-actions button { padding: 6px 10px; border: 1px solid var(--border-color, #3b4351); border-radius: 7px; background: transparent; color: inherit; }
.gyro-virtual-log-actions button:disabled { cursor: not-allowed; opacity: .45; }
</style>
