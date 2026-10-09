export type ThemeName = 'blue-black' | 'red-black' | 'cyberpunk';

import { getUiSetting, setUiSettings } from './uiSettings';

function readTheme(): ThemeName {
  return getUiSetting('theme');
}

export function applyTheme(theme: ThemeName): void {
  document.documentElement.dataset.theme = theme;
}

export function getTheme(): ThemeName {
  return readTheme();
}

export function setTheme(theme: ThemeName): void {
  void setUiSettings({ theme });
  applyTheme(theme);
  window.dispatchEvent(new CustomEvent('theme:changed', { detail: { theme } }));
}

applyTheme(readTheme());
window.addEventListener('ui-settings:loaded', () => applyTheme(readTheme()));
window.addEventListener('ui-settings:changed', () => applyTheme(readTheme()));
