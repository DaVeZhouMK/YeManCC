<script setup lang="ts">
import { computed } from 'vue';
import { ICONS } from '@/icons';
import classicMouse from '../assets/mouse-classic.png';
import controller_xbox from '../assets/input-status/controller-xbox.svg';
import controller_native from '../assets/input-status/controller-native.svg';
import controller_sd from '../assets/input-status/controller-sd.svg';
import controller_ps5 from '../assets/input-status/controller-ps5.png';
import mouse_solid from '../assets/input-status/mouse-solid.svg';
import gyro_on from '../assets/input-status/gyro-on.svg';
import gyro_off from '../assets/input-status/gyro-off.svg';
import touch_steam from '../assets/input-status/touch-steam-574.svg';
import calibration_steam from '../assets/input-status/calibration-steam-517.svg';
import return_desktop from '../assets/input-status/return-desktop-steam-69.svg';
const statusMasks:Record<string,string>={'controller-xbox':controller_xbox,'controller-native':controller_native,'controller-sd':controller_sd,'controller-ps5':controller_ps5,'mouse-solid':mouse_solid,'gyro-on':gyro_on,'gyro-off':gyro_off,'touch-steam':touch_steam,'calibration-steam':calibration_steam,'return-desktop':return_desktop};

const props = defineProps<{ name: string; strokeWidth?: number }>();
const lightIconNames = new Set(['gauge', 'gamepad', 'rotate']);
const resolvedStrokeWidth = computed(() => props.strokeWidth ?? (lightIconNames.has(props.name) ? 1.2 : 1.6));
</script>

<template>
  <span v-if="statusMasks[name]" class="app-icon app-icon-mouse-classic" :class="{'app-icon-page-symbol':name==='touch-steam'||name==='calibration-steam'}" :style="{maskImage: 'url('+JSON.stringify(statusMasks[name])+')', WebkitMaskImage: 'url('+JSON.stringify(statusMasks[name])+')'}" aria-hidden="true"/>
  <span v-else-if="name === 'mouse'" class="app-icon app-icon-mouse-classic" :style="{maskImage: 'url('+classicMouse+')', WebkitMaskImage: 'url('+classicMouse+')'}" aria-hidden="true"/>
  <svg v-else
    class="app-icon"
    viewBox="0 0 24 24"
    fill="none"
    stroke="currentColor"
    :stroke-width="resolvedStrokeWidth"
    stroke-linecap="round"
    stroke-linejoin="round"
    aria-hidden="true"
    v-html="ICONS[name] || ''"
  />
</template>

<style scoped>
.app-icon {
  width: 1em;
  height: 1em;
  display: block;
}
.app-icon-page-symbol{opacity:.85}
.app-icon-mouse-classic{background:currentColor;mask-size:contain;-webkit-mask-size:contain;mask-position:center;-webkit-mask-position:center;mask-repeat:no-repeat;-webkit-mask-repeat:no-repeat;}
</style>
