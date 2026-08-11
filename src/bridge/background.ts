import { invoke } from './ipc';
import { getUiSetting, setUiSettings } from './uiSettings';

export type BackgroundKind = 'image' | 'video';

export interface BackgroundState {
  enabled: boolean;
  kind?: BackgroundKind;
  file?: string;
  url: string;
  fallbackUrls?: string[];
}

export interface DynamicBackgroundState {
  enabled: boolean;
  kind?: BackgroundKind;
  url: string;
  fallbackUrls?: string[];
  appId?: number;
  gameName?: string;
  source?: string;
}

export const BACKGROUND_OPACITY_MIN = 0.2;
export const BACKGROUND_OPACITY_MAX = 0.8;
export const BACKGROUND_OPACITY_DEFAULT = 0.64;
export const BACKGROUND_BLUR_MIN = 0;
export const BACKGROUND_BLUR_MAX = 24;
export const BACKGROUND_BLUR_DEFAULT = 0;

function normalizeBackgroundOpacity(value: number): number {
  if (!Number.isFinite(value)) return BACKGROUND_OPACITY_DEFAULT;
  return Math.min(BACKGROUND_OPACITY_MAX, Math.max(BACKGROUND_OPACITY_MIN, Math.round(value * 100) / 100));
}

function normalizeBackgroundBlur(value: number): number {
  if (!Number.isFinite(value)) return BACKGROUND_BLUR_DEFAULT;
  return Math.min(BACKGROUND_BLUR_MAX, Math.max(BACKGROUND_BLUR_MIN, Math.round(value)));
}

export const backgroundGet = () => invoke<BackgroundState>('background.get');

export const backgroundInstall = (source: string) =>
  invoke<BackgroundState>('background.install', { source }, { timeoutMs: 30000 });

export const backgroundClear = () => invoke<BackgroundState>('background.clear');

export const dynamicBackgroundGet = () => invoke<DynamicBackgroundState>('dynamicBackground.get');

export const dynamicBackgroundInstallUrl = (
  source: string,
  kind: BackgroundKind,
  appId: number,
  gameName: string,
  sourceType: string,
) => invoke<DynamicBackgroundState>('dynamicBackground.installUrl', { source, kind, appId, gameName, sourceType }, { timeoutMs: 120000 });

export const dynamicBackgroundInstallOnline = (
  source: string,
  fallbackUrls: string[],
  appId: number,
  gameName: string,
  sourceType: string,
  kind: BackgroundKind = 'video',
) => invoke<DynamicBackgroundState>('dynamicBackground.installOnline', { source, fallbackUrls, appId, gameName, sourceType, kind });

export const dynamicBackgroundClear = () => invoke<DynamicBackgroundState>('dynamicBackground.clear');

export function getBackgroundOpacity(): number {
  return normalizeBackgroundOpacity(Number(getUiSetting('backgroundOpacity')));
}

export function previewBackgroundOpacity(value: number): number {
  const opacity = normalizeBackgroundOpacity(value);
  window.dispatchEvent(new CustomEvent('background:opacity-changed', { detail: { opacity } }));
  return opacity;
}

export function setBackgroundOpacity(value: number): number {
  const opacity = normalizeBackgroundOpacity(value);
  void setUiSettings({ backgroundOpacity: opacity });
  return previewBackgroundOpacity(opacity);
}

export function getBackgroundBlur(): number {
  return normalizeBackgroundBlur(Number(getUiSetting('backgroundBlur')));
}

export function previewBackgroundBlur(value: number): number {
  const blur = normalizeBackgroundBlur(value);
  window.dispatchEvent(new CustomEvent('background:blur-changed', { detail: { blur } }));
  return blur;
}

export function setBackgroundBlur(value: number): number {
  const blur = normalizeBackgroundBlur(value);
  void setUiSettings({ backgroundBlur: blur });
  return previewBackgroundBlur(blur);
}

export function notifyBackgroundChanged(state: BackgroundState): void {
  const detail = state.enabled && state.url
    ? { ...state, url: `${state.url}${state.url.includes('?') ? '&' : '?'}ui=${Date.now()}` }
    : state;
  window.dispatchEvent(new CustomEvent('background:changed', { detail }));
}
