export type ThemeName = 'blue-black' | 'red-black' | 'cyberpunk';

const THEME_KEY = 'yeman.ui.theme';

function readTheme(): ThemeName {
  const value = localStorage.getItem(THEME_KEY);
  if (value === 'red-black' || value === 'cyberpunk') return value;
  if (value === 'cyber-red') return 'red-black';
  return 'blue-black';
}

export function applyTheme(theme: ThemeName): void {
  document.documentElement.dataset.theme = theme;
}

export function getTheme(): ThemeName {
  return readTheme();
}

export function setTheme(theme: ThemeName): void {
  localStorage.setItem(THEME_KEY, theme);
  applyTheme(theme);
  window.dispatchEvent(new CustomEvent('theme:changed', { detail: { theme } }));
}

applyTheme(readTheme());
