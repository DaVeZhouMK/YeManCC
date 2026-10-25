import { shallowRef } from 'vue';
import { RADIAL_USER_AVAILABLE } from './availability';
/** Process-local admission starts closed, including while preferences load. */
export const radialTestingEnabled = shallowRef(false);
export function isRadialTestingEnabled(): boolean {
  return RADIAL_USER_AVAILABLE && radialTestingEnabled.value;
}
