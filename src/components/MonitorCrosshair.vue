<script setup lang="ts">
import type { HudCrosshair } from '../bridge/monitorHudModel';
defineProps<{ kind: HudCrosshair }>();
</script>
<template>
  <!-- Sibling of the scaled HUD, never inside its zoom/opacity/transform chain. -->
  <svg v-if="kind !== 'none'" :data-monitor-crosshair="kind" aria-hidden="true" focusable="false" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"
    style="position:absolute;left:50%;top:50%;width:24px;height:24px;min-width:24px;min-height:24px;transform:translate(-50%,-50%);pointer-events:none;opacity:1;z-index:2;overflow:visible">
    <template v-if="kind === 'dot'">
      <circle cx="12" cy="12" r="2" fill="#fff" stroke="#10151d" stroke-width="1" />
    </template>
    <template v-else-if="kind === 'cross' || kind === 'gap'">
      <path :d="kind === 'cross' ? 'M12 4V20M4 12H20' : 'M12 3V8M12 16V21M3 12H8M16 12H21'" fill="none" stroke="#10151d" stroke-width="4" />
      <path :d="kind === 'cross' ? 'M12 4V20M4 12H20' : 'M12 3V8M12 16V21M3 12H8M16 12H21'" fill="none" stroke="#fff" stroke-width="2" />
    </template>
    <template v-else>
      <circle cx="12" cy="12" r="7" fill="none" stroke="#10151d" stroke-width="4" />
      <circle cx="12" cy="12" r="7" fill="none" stroke="#fff" stroke-width="2" />
      <circle v-if="kind === 'ring-dot'" cx="12" cy="12" r="1.5" fill="#fff" stroke="#10151d" stroke-width="1" />
    </template>
  </svg>
</template>
