// music.ts — 共享音乐播放逻辑（单例 Audio，跨路由常驻）
//
// 设计要点：
// - 用模块级单例 `Audio` 承载播放，切换快捷应用页面不中断（KeepAlive 保活，且本模块不被销毁）。
// - 顶部 CD（TopMonitorBar）与快捷应用播放器（QuickAppView）共用同一份响应式状态。
// - 本地音乐通过 native 专用 HTTPS 虚拟主机 `music-assets.invalid/` 加载，不直接使用 file://。
import { ref, computed } from 'vue';
import { dialog, fs, music as musicApi, type MusicState } from './api';

const AUDIO_EXT = new Set(['mp3', 'm4a', 'aac', 'wav', 'ogg', 'flac']);
const MUSIC_HOST = 'https://music-assets.invalid/';

export type MusicMode = 'sequential' | 'random';

// 单例音频元素：不放入任何组件模板，避免路由切换销毁。
const audio = new Audio();
audio.preload = 'metadata';

// ── 音量（0–1）：模块级单例，持久化到 native 配置（music_player.json），切换路由/页面不丢；部署清 EBWebView 缓存也不丢 ──
export const volume = ref<number>(0.8);
export const muted = ref<boolean>(false);
audio.volume = volume.value; // 应用初始音量（muted 默认 false）

export const folder = ref<string>('');
export const baseUrl = ref<string>('');
export const tracks = ref<string[]>([]);
export const index = ref<number>(-1);
export const playing = ref<boolean>(false);
export const mode = ref<MusicMode>('sequential');
export const error = ref<string | null>(null);
export const currentName = computed(() =>
  index.value >= 0 && index.value < tracks.value.length ? tracks.value[index.value] : ''
);
export const hasFolder = computed(() => folder.value !== '' && baseUrl.value !== '');

let brokenStreak = 0;
let initialized = false;

function extOf(name: string): string {
  const i = name.lastIndexOf('.');
  return i >= 0 ? name.slice(i + 1).toLowerCase() : '';
}

function sortNames(names: string[]): string[] {
  return names
    .slice()
    .sort((a, b) => a.localeCompare(b, undefined, { numeric: true, sensitivity: 'base' }));
}

function currentUrl(): string {
  if (index.value < 0 || index.value >= tracks.value.length) return '';
  return baseUrl.value + encodeURIComponent(tracks.value[index.value]);
}

// ── 音频事件：以媒体自身状态为唯一真相 ──
audio.addEventListener('play', () => {
  playing.value = true;
});
audio.addEventListener('pause', () => {
  playing.value = false;
});
audio.addEventListener('ended', () => {
  playNext();
});
audio.addEventListener('error', () => {
  skipBroken();
});

export async function initMusic(): Promise<void> {
  if (initialized) return;
  initialized = true;
  try {
    const st = await musicApi.get();
    if (st.enabled && st.folder && st.baseUrl) {
      folder.value = st.folder;
      baseUrl.value = st.baseUrl;
      await scanFolder();
    }
    // 恢复音量和播放顺序（与文件夹一样持久化在 native 配置）
    if (typeof st.volume === 'number' && st.volume >= 0 && st.volume <= 1) {
      volume.value = st.volume;
      audio.volume = st.volume;
    }
    if (st.mode === 'random' || st.mode === 'sequential') mode.value = st.mode;
  } catch {
    /* 启动期读取失败不阻塞界面 */
  }
}

export async function chooseFolder(): Promise<void> {
  const picked = await dialog.openFolder();
  if (!picked) return;
  let st: MusicState;
  try {
    st = await musicApi.setFolder(picked);
  } catch (e) {
    error.value = '无法设置音乐目录：' + (e as Error).message;
    return;
  }
  if (!st.enabled || !st.folder || !st.baseUrl) {
    error.value = '无法设置音乐目录';
    return;
  }
  folder.value = st.folder;
  baseUrl.value = st.baseUrl;
  // 动态映射可能需要刷新页面才能对当前页生效；刷新后由 initMusic 重新恢复列表。
  if (st.reloadRecommended) {
    window.location.reload();
    return;
  }
  await scanFolder();
}

export async function scanFolder(): Promise<void> {
  if (!folder.value) return;
  const entries = await fs.readDir(folder.value);
  const names = entries
    .filter((e: { isFile?: boolean; name?: string }) => e.isFile && AUDIO_EXT.has(extOf(e.name ?? '')))
    .map((e: { name?: string }) => e.name ?? '');
  tracks.value = sortNames(names);
  index.value = tracks.value.length > 0 ? 0 : -1;
  error.value = tracks.value.length === 0 ? '所选目录没有可播放的音乐文件' : null;
  stop();
}

export function playCurrent(): void {
  const url = currentUrl();
  if (!url) {
    error.value = '没有可播放的曲目';
    return;
  }
  if (audio.src !== url) audio.src = url;
  audio.play().catch(() => {
    playing.value = false;
  });
}

export function togglePlay(): void {
  if (!hasFolder.value) {
    void chooseFolder();
    return;
  }
  if (playing.value) {
    pause();
    return;
  }
  if (index.value < 0 && tracks.value.length > 0) index.value = 0;
  playCurrent();
}

export function pause(): void {
  audio.pause();
}

export function stop(): void {
  audio.pause();
  audio.removeAttribute('src');
  audio.load();
  playing.value = false;
}

export function playNext(): void {
  if (tracks.value.length === 0) return;
  if (mode.value === 'random') {
    if (tracks.value.length === 1) {
      playCurrent();
      return;
    }
    let n = index.value;
    while (n === index.value) n = Math.floor(Math.random() * tracks.value.length);
    index.value = n;
  } else {
    index.value = (index.value + 1) % tracks.value.length;
  }
  brokenStreak = 0;
  playCurrent();
}

export function playPrev(): void {
  if (tracks.value.length === 0) return;
  if (mode.value === 'random') {
    if (tracks.value.length === 1) {
      playCurrent();
      return;
    }
    let n = index.value;
    while (n === index.value) n = Math.floor(Math.random() * tracks.value.length);
    index.value = n;
  } else {
    index.value = (index.value - 1 + tracks.value.length) % tracks.value.length;
  }
  brokenStreak = 0;
  playCurrent();
}

export async function setMode(m: MusicMode): Promise<void> {
  const previous = mode.value;
  mode.value = m;
  try {
    await musicApi.setMode(m);
  } catch {
    mode.value = previous;
    error.value = '播放顺序保存失败';
  }
}

function skipBroken(): void {
  if (tracks.value.length === 0) {
    stop();
    return;
  }
  brokenStreak++;
  if (brokenStreak >= tracks.value.length) {
    stop();
    error.value = '所有曲目都无法播放';
    return;
  }
  playNext();
}

// ── 音量控制 ──
// setVolume：实时调音量（拖动 input 事件高频调用，不写盘）。
export function setVolume(v: number): void {
  const c = Math.max(0, Math.min(1, v));
  volume.value = c;
  audio.volume = c;
  // 调大音量时若处于静音态，自动解除（避免“拉了音量却没声”的困惑）。
  if (c > 0 && audio.muted) {
    audio.muted = false;
    muted.value = false;
  }
}

// persistVolume：拖动结束（change 事件）落盘到 native 配置，避免高频同步写盘。
export async function persistVolume(): Promise<void> {
  try {
    await musicApi.setVolume(volume.value);
  } catch {
    /* native 写失败忽略，下次 change 再试 */
  }
}

// toggleMute：静音/恢复（与音量值相互独立；恢复时不改音量值）。
export function toggleMute(): void {
  muted.value = !muted.value;
  audio.muted = muted.value;
}
