import { createDefaultQuickApps } from './quickAppDefaults';

/**
 * Monochrome quick-app icon semantics.
 *
 * The five defaults keep their user-selected line icons. Other apps use their
 * actual file/shortcut icons in grayscale; these marks remain the fallback.
 */
export type QuickAppIconKind = 'computer' | 'browser' | 'task' | 'download' | 'keyboard' | 'generic';
export type QuickAppIconVariant = 'a' | 'b';

export interface QuickAppIconPreview {
  kind: QuickAppIconKind;
  label: string;
  appName: string;
  variants: readonly [QuickAppIconVariant, QuickAppIconVariant];
}

export const QUICK_APP_ICON_PREVIEWS: readonly QuickAppIconPreview[] = [
  { kind: 'computer', label: '此电脑', appName: '此电脑', variants: ['a', 'b'] },
  { kind: 'browser', label: 'Edge 浏览器', appName: 'edge浏览器', variants: ['a', 'b'] },
  { kind: 'task', label: '任务管理器', appName: '任务管理器', variants: ['a', 'b'] },
  { kind: 'download', label: '关屏下载', appName: '关屏下载', variants: ['a', 'b'] },
  { kind: 'keyboard', label: '复古键盘', appName: '复古键盘', variants: ['a', 'b'] },
  { kind: 'generic', label: '读取失败时的备用图标', appName: '备用图标', variants: ['a', 'b'] },
];

function normalized(value: unknown): string {
  return String(value || '').trim().toLowerCase().replace(/[\\/]+/g, '/');
}

const defaultIconPaths = new Set(createDefaultQuickApps().apps.map(app => normalized(app.path)));

/** Match by path, not display name, so renames keep the default icon while
 * user-added apps named like a default still use their own file icon. */
export function isDefaultQuickAppPath(path: string): boolean {
  return defaultIconPaths.has(normalized(path));
}

export function quickAppIconKind(app: { name?: unknown; path?: unknown }): QuickAppIconKind {
  const name = normalized(app.name);
  const path = normalized(app.path);
  if (name.includes('此电脑') || path.endsWith('/windows/explorer.exe')) return 'computer';
  if (name.includes('edge') || path.endsWith('/msedge.exe')) return 'browser';
  if (name.includes('任务管理器') || path.endsWith('/taskmgr.exe')) return 'task';
  if (name.includes('关屏下载') || name.includes('关屏') || path.includes('关闭屏幕')) return 'download';
  if (name.includes('复古键盘') || path.endsWith('/osk.exe')) return 'keyboard';
  return 'generic';
}

/** The selected product variants are B for the computer/browser marks and A for everything else. */
export function quickAppIconVariant(app: { name?: unknown; path?: unknown }): QuickAppIconVariant {
  const kind = quickAppIconKind(app);
  if (kind === 'computer' || kind === 'browser') return 'b';
  return 'a';
}
