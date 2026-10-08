import { computed, ref } from 'vue';
import { fs } from './api';

export type GyroVirtualFeature = 'virtual-gamepad' | 'gyro-motion';
export const VIRTUAL_GAMEPAD_ASSET_FOLDER = 'virtual-gamepad';
export const GYRO_MOTION_ASSET_FOLDER = 'gyro-motion';
let featureAssetsRoot = 'C:\\SOFT\\YeMan\\PowerControl\\feature-assets';
const initialized = ref(false);
const virtualGamepadAssetsPresent = ref(false);
const gyroMotionAssetsPresent = ref(false);

export function setGyroVirtualFeatureAssetsRoot(powerControlDir: string): void {
  const root = powerControlDir.replace(/\//g, '\\').replace(/[\\]+$/, '');
  featureAssetsRoot = `${root}\\feature-assets`;
}
export const virtualGamepadFeatureEnabled = computed(() => initialized.value && virtualGamepadAssetsPresent.value);
export const gyroMotionFeatureEnabled = computed(() => initialized.value && gyroMotionAssetsPresent.value);
export interface GyroVirtualFeatureDirectoryState {
  virtualGamepad: boolean;
  gyroMotion: boolean;
}
/** Pure mapping shared by native-backed probing and fixture tests. */
export function evaluateGyroVirtualFeatureDirectories(
  directories: Partial<GyroVirtualFeatureDirectoryState>,
): GyroVirtualFeatureDirectoryState {
  return {
    virtualGamepad: directories.virtualGamepad === true,
    gyroMotion: directories.gyroMotion === true,
  };
}
export function gyroVirtualFeatureEnabled(feature: GyroVirtualFeature): boolean {
  return feature === 'virtual-gamepad' ? virtualGamepadFeatureEnabled.value : gyroMotionFeatureEnabled.value;
}
export async function refreshGyroVirtualFeatureAvailability(): Promise<{ virtualGamepad: boolean; gyroMotion: boolean }> {
  const isDev = Boolean((import.meta as { env?: { DEV?: boolean } }).env?.DEV);
  if (isDev) {
    const detail = evaluateGyroVirtualFeatureDirectories({ virtualGamepad: true, gyroMotion: true });
    virtualGamepadAssetsPresent.value = true;
    gyroMotionAssetsPresent.value = true;
    initialized.value = true;
    window.dispatchEvent(new CustomEvent('gyro-virtual:visibility', { detail }));
    return detail;
  }
  const [virtualGamepad, gyroMotion] = await Promise.all([
    fs.exists(`${featureAssetsRoot}\\${VIRTUAL_GAMEPAD_ASSET_FOLDER}`).catch(() => false),
    fs.exists(`${featureAssetsRoot}\\${GYRO_MOTION_ASSET_FOLDER}`).catch(() => false),
  ]);
  const detail = evaluateGyroVirtualFeatureDirectories({ virtualGamepad, gyroMotion });
  virtualGamepadAssetsPresent.value = detail.virtualGamepad;
  gyroMotionAssetsPresent.value = detail.gyroMotion;
  initialized.value = true;
  window.dispatchEvent(new CustomEvent('gyro-virtual:visibility', { detail }));
  return detail;
}
export function getGyroVirtualFeatureAssetsRoot(): string { return featureAssetsRoot; }
export async function gyroVirtualCaptureEnabled(): Promise<boolean> {
  const detail = await refreshGyroVirtualFeatureAvailability();
  // Native input capture/InputHost use the two feature directories as one
  // atomic test surface. Keep diagnostics/logging readiness on the same gate
  // so a partial package cannot advertise capture that native will refuse.
  return detail.virtualGamepad && detail.gyroMotion;
}
