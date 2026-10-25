import { createApp } from 'vue';
import { RADIAL_USER_AVAILABLE } from './radial/availability';
if (RADIAL_USER_AVAILABLE) {
  void import('./components/RadialOutput.vue').then(({ default: RadialOutput }) => {
    createApp(RadialOutput).mount('#radial');
  });
}
