<script setup lang="ts">
import { computed, onActivated, onBeforeUnmount, onDeactivated, onMounted, shallowRef } from 'vue';
import { invoke, on } from '@/bridge/ipc';
import { isUiVisible, onUiVisibilityChange } from '@/bridge/uiLifecycle';
import { createUsbipUpgradeStatusController, unverifiedUsbipUpgradeStatus, usbipUpgradePresentation, usbipUpgradeHasError,
  USBIP_UPGRADE_READ_TIMEOUT_MS } from '@/bridge/usbipUpgradeStatus';

const props = defineProps<{ errorsOnly?: boolean }>();
const status = shallowRef(unverifiedUsbipUpgradeStatus());
const presentation = computed(() => usbipUpgradePresentation(status.value));
const controller = createUsbipUpgradeStatusController({
  read: signal => invoke('gamepad.driverSetup.get', { refreshUsbip: true }, { signal, timeoutMs: USBIP_UPGRADE_READ_TIMEOUT_MS }),
  subscribe: handler => on('gamepad.driverSetup', handler),
  isVisible: isUiVisible,
  watchVisibility: handler => onUiVisibilityChange(state => handler(state.visible)),
  watchRefresh: handler => {
    const stops = ['window.restored', 'window.maximized', 'window.summoned', 'window.focus'].map(event => on(event, handler));
    window.addEventListener('focus', handler);
    return () => { for (const stop of stops) stop(); window.removeEventListener('focus', handler); };
  },
  changed: next => { status.value = next; },
});
onMounted(controller.start);
onActivated(controller.start);
onDeactivated(controller.stop);
onBeforeUnmount(controller.stop);
</script>

<template>
  <div v-if="!props.errorsOnly || usbipUpgradeHasError(status)" class="usbip-upgrade-status" :data-usbip-phase="status.phase" :data-usbip-tone="presentation.tone"
    role="status" aria-live="polite" aria-atomic="true">
    <strong>{{ presentation.title }}</strong>
    <p>{{ props.errorsOnly ? status.detail || presentation.description : presentation.description }}</p>
    <p v-if="!props.errorsOnly && (status.installedVersion || status.targetVersion)" class="usbip-upgrade-versions">
      <span v-if="status.installedVersion">已安装：{{ status.installedVersion }}</span>
      <span v-if="status.targetVersion">配套目标：{{ status.targetVersion }}</span>
    </p>
    <p v-if="!props.errorsOnly && status.detail" class="usbip-upgrade-detail">{{ status.detail }}</p>
  </div>
</template>

<style scoped>
.usbip-upgrade-status{display:grid;gap:4px;min-width:0;margin-top:10px;padding:8px 10px;border:1px solid transparent;border-radius:var(--radius-ctrl,8px);color:var(--text-dim,#aaa);font-size:11px;line-height:1.5;overflow-wrap:anywhere}
.usbip-upgrade-status strong{font-size:12px;font-weight:650;color:var(--text,#eee)}
.usbip-upgrade-status p{margin:0}
.usbip-upgrade-versions{display:flex;flex-wrap:wrap;gap:4px 16px}
.usbip-upgrade-detail{white-space:pre-wrap}
.usbip-upgrade-status[data-usbip-tone="progress"] strong,.usbip-upgrade-status[data-usbip-tone="success"] strong{color:var(--accent,#79baff)}
.usbip-upgrade-status[data-usbip-tone="warning"]{border-color:color-mix(in srgb,#f4a33d 35%,transparent);background:color-mix(in srgb,#f4a33d 7%,transparent)}
.usbip-upgrade-status[data-usbip-tone="warning"] strong{color:#ffc46b}
</style>
