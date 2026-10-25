<script setup lang="ts">
import { computed, ref } from 'vue';
import { controllerFeatureStatuses, type OemInputUsage } from '@/bridge/controllerFeatureStatus';
const props = defineProps<{
  status: { text: string; detail: string; tone: 'on' | 'off' | 'warning' | 'pending' };
  gyroSupported: boolean; gyroEnabled: boolean; backButtons: number;
  physicalGyroSupported?: boolean | null;
  physicalBackButtons?: number | null;
  physicalBackButtonsCaptured?: number | null;
  usage?: OemInputUsage | null; virtualEnabled?: boolean; persona?: string;
  observedKeys?: readonly string[];
}>();
const selected = ref<string | null>(null);
const virtualLabel = computed(() => props.status.text.replace(/^虚拟手柄[-－·\s]*/, '').replace('状态待确认', '待确认').replace('成功开启', '已开启').replace('Steam未开启实际无效', 'Steam未运行'));
const lights = computed(() => controllerFeatureStatuses({usage: props.usage, virtualEnabled: props.virtualEnabled === true,
  persona: props.persona || 'disabled', gyroSupported: props.gyroSupported, gyroEnabled: props.gyroEnabled,
  physicalGyroSupported: props.physicalGyroSupported, observedKeys: props.observedKeys}));
const detail = computed(() => lights.value.find(light => light.id === selected.value));
</script>
<template>
  <aside class="controller-feature-status" aria-label="手柄功能状态">
    <div class="controller-output-status" :data-tone="status.tone" role="status" :title="status.detail"><span>虚拟输出</span><strong>{{ virtualLabel }}</strong></div>
    <div class="controller-channel-list">
      <button v-for="light in lights" :key="light.id" type="button" class="controller-channel" :data-feature="light.id" :data-tone="light.tone"
        :title="light.detail" :aria-label="light.name + '：' + light.value + '，' + light.hint" :aria-expanded="selected === light.id"
        @click="selected = selected === light.id ? null : light.id">
        <span class="channel-name"><i class="channel-led"/>{{ light.name }}</span>
        <strong>{{ light.value }}</strong><small>{{ light.hint }}</small>
      </button>
    </div>
    <p v-if="detail" class="controller-channel-detail" role="status">{{ detail.detail }}</p>
  </aside>
</template>
<style scoped>
.controller-feature-status{min-width:0;border-top:1px solid rgba(139,159,186,.15);padding-top:7px}
.controller-output-status{display:flex;align-items:center;gap:7px;margin:0 0 7px;font-size:11px;line-height:1.4;color:var(--text-dim)}
.controller-output-status strong{font-weight:600}.controller-output-status[data-tone=on] strong{color:var(--accent)}.controller-output-status[data-tone=warning] strong{color:#e5b66e}
.controller-channel-list{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:6px;min-width:0;margin:0}
.controller-channel{display:grid;grid-template-rows:auto auto auto;gap:3px;min-width:0;text-align:left;padding:8px;border:1px solid rgba(139,159,186,.16);border-radius:7px;background:var(--bg-input,rgba(139,159,186,.04));font:inherit;cursor:pointer;color:var(--text-dim)}
.controller-channel:focus-visible{outline:2px solid var(--accent);outline-offset:2px}.controller-channel[aria-expanded=true]{border-color:var(--accent)}
.channel-name{display:inline-flex;align-items:center;gap:4px;font-size:11px;line-height:1.4}.channel-led{width:5px;height:5px;flex:0 0 auto;border-radius:50%;background:#667282}
.controller-channel strong{display:block;min-width:0;font-size:12px;line-height:1.5;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.controller-channel small{display:block;min-width:0;font-size:10px;line-height:1.5;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.controller-channel[data-tone=on] strong{color:var(--accent)}.controller-channel[data-tone=on] .channel-led{background:var(--accent)}
.controller-channel[data-tone=warning] strong{color:#e5b66e}.controller-channel[data-tone=warning] .channel-led{background:#e5b66e}
.controller-channel-detail{margin:7px 0 0;padding:8px;border:1px solid rgba(139,159,186,.16);border-radius:7px;color:var(--text-dim);font-size:11px;line-height:1.65;overflow-wrap:anywhere}
</style>
