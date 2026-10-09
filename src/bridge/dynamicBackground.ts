import { fs, registry, type HttpResponse } from './api';
import { requestSteamHttp, requestSteamJson } from './steamHttp';
import {
  dynamicBackgroundGet,
  dynamicBackgroundInstallOnline,
  dynamicBackgroundClear,
  type DynamicBackgroundTarget,
  type DynamicBackgroundState,
} from './background';
import type { DetectedGame } from './gamedetect';
import { getUiSetting, setUiSettings } from './uiSettings';

// V2 is keyed by the normalized performance-schedule title, so stale exe-name matches are ignored.
const CACHE_KEY = 'yeman.ui.dynamic-background-title-cache-v3';
const MAX_ONLINE_MEDIA_CANDIDATES = 16;
let dynamicRefreshInFlight: { key: string; promise: Promise<DynamicBackgroundResult | null> } | null = null;

function reportDynamicProgress(message: string): void {
  window.dispatchEvent(new CustomEvent('dynamic-background:progress', { detail: message }));
}

const TITLE_APP_IDS: Record<string, number> = {
  wargamereddragon: 251060,
  warno: 1611600,
};

// Executable mappings are deliberately last-resort fallbacks.
const EXE_APP_IDS: Record<string, number> = {
  wargame3: 251060,
};

const SEARCH_NOISE = new Set([
  'the', 'a', 'an', 'of', 'and', 'for', 'with', 'game', 'edition',
]);

export interface DynamicBackgroundConfig {
  enabled: boolean;
}

export interface DynamicBackgroundResult {
  state: DynamicBackgroundState;
  appId: number;
  gameName: string;
  source: 'video' | 'background' | 'screenshot';
  detectedTitle: string;
  matchedBy: 'title-map' | 'steam-library' | 'cache' | 'steam-search' | 'exe-fallback';
}

interface StoreDetails {
  name?: string;
  steam_appid?: number;
  background?: string;
  background_raw?: string;
  screenshots?: Array<{ path_full?: string }>;
  movies?: Array<{
    id?: number;
    name?: string;
    highlight?: boolean;
    webm?: Record<string, string>;
    dash_h264?: string;
    hls_h264?: string;
    dash_manifests?: string[];
    hlsManifest?: string;
  }>;
}

interface StorePageMedia {
  webm?: string;
  mp4?: string;
  hls?: string;
  webms?: string[];
  mp4s?: string[];
  hlss?: string[];
  image?: string;
}

interface SteamSearchItem {
  id?: number;
  name?: string;
  type?: string;
}

interface ResolvedApp {
  appId: number;
  matchedBy: DynamicBackgroundResult['matchedBy'];
  detectedTitle: string;
}

export function stripLaunchModeSuffix(value: string): string {
  let title = value.trim().replace(/\.exe$/i, '');
  const suffixes = [
    /\s*[-|:–—]\s*(?:direct\s*x|directx|dx)\s*(?:9|10|11|12)(?:\s*x64)?\s*$/i,
    /\s*\((?:direct\s*x|directx|dx)\s*(?:9|10|11|12)(?:\s*x64)?\)\s*$/i,
    /\s*[-|:–—]\s*(?:vulkan|open\s*gl)(?:\s*x64)?\s*$/i,
    /\s*\((?:vulkan|open\s*gl)(?:\s*x64)?\)\s*$/i,
  ];
  let changed = true;
  while (changed) {
    changed = false;
    for (const suffix of suffixes) {
      const next = title.replace(suffix, '').trim();
      if (next !== title) {
        title = next;
        changed = true;
      }
    }
  }
  return title;
}

export function normalizeGameTitle(value: string): string {
  return cleanGameTitle(value)
    .normalize('NFKD')
    .replace(/[’']/g, '')
    .replace(/&/g, ' and ')
    .replace(/[^a-z0-9]+/gi, '')
    .toLowerCase();
}

function titleTokens(value: string): string[] {
  return cleanGameTitle(value)
    .normalize('NFKD')
    .replace(/&/g, ' and ')
    .toLowerCase()
    .split(/[^a-z0-9]+/)
    .filter((token) => token.length > 0 && !SEARCH_NOISE.has(token));
}

// Process titles sometimes append launcher/render-mode and copyright metadata.
// Keep the real game title so Steam can match it as a normal store search term.
export function cleanGameTitle(value: string): string {
  let title = stripLaunchModeSuffix(value).replace(/\s+/g, ' ').trim();
  title = title.replace(/\s+\bby\b[\s\S]*$/i, '').trim();
  title = title.replace(/\s*(?:\(\s*c\s*\)|\[\s*c\s*\]|\bcopyright\b)\s*\d{4}\s*$/i, '').trim();
  return title.replace(/[|:,-]+\s*$/, '').trim();
}

export function scoreSteamTitle(query: string, candidate: string): number {
  const queryKey = normalizeGameTitle(query);
  const candidateKey = normalizeGameTitle(candidate);
  if (!queryKey || !candidateKey) return 0;
  if (queryKey === candidateKey) return 1;

  const queryTokens = new Set(titleTokens(query));
  const candidateTokens = new Set(titleTokens(candidate));
  if (!queryTokens.size || !candidateTokens.size) return 0;
  let intersection = 0;
  for (const token of queryTokens) if (candidateTokens.has(token)) intersection++;
  const coverage = intersection / queryTokens.size;
  const precision = intersection / candidateTokens.size;
  let score = coverage * 0.68 + precision * 0.32;
  if (candidateKey.startsWith(queryKey) || queryKey.startsWith(candidateKey)) score += 0.08;

  // Search often returns DLC directly after the base game. Penalize extra commercial suffixes.
  const candidateLower = candidate.toLowerCase();
  if (/\b(dlc|soundtrack|season pass|nation pack|expansion|demo)\b/.test(candidateLower) &&
      !/\b(dlc|soundtrack|season pass|nation pack|expansion|demo)\b/.test(query.toLowerCase())) {
    score -= 0.3;
  }
  return Math.max(0, Math.min(0.99, score));
}

function detectedGameTitle(game: DetectedGame): string {
  // This is the same source displayed by PerformanceScheduleView.
  const title = cleanGameTitle(game.title || '');
  return title || cleanGameTitle(game.name || '');
}

function readConfig(): DynamicBackgroundConfig {
  return {
    enabled: getUiSetting('dynamicBackgroundEnabled'),
  };
}

export function getDynamicBackgroundConfig(): DynamicBackgroundConfig {
  return readConfig();
}

export function setDynamicBackgroundEnabled(enabled: boolean): Promise<void> {
  return setUiSettings({ dynamicBackgroundEnabled: enabled });
}

function cachedAppId(titleKey: string): number | null {
  try {
    const cache = JSON.parse(localStorage.getItem(CACHE_KEY) || '{}') as Record<string, number>;
    const id = Number(cache[titleKey]);
    return Number.isInteger(id) && id > 0 ? id : null;
  } catch {
    return null;
  }
}

function cacheAppId(titleKey: string, appId: number): void {
  try {
    const cache = JSON.parse(localStorage.getItem(CACHE_KEY) || '{}') as Record<string, number>;
    cache[titleKey] = appId;
    localStorage.setItem(CACHE_KEY, JSON.stringify(cache));
  } catch { /* use the live result only */ }
}

function decodeHtml(value: string): string {
  return value
    .replace(/&amp;/gi, '&')
    .replace(/&quot;/gi, '"')
    .replace(/&#39;/gi, "'")
    .replace(/&#x2F;/gi, '/')
    .replace(/\\\//g, '/');
}

async function requestJson<T>(url: string): Promise<T> {
  return requestSteamJson<T>(url);
}

/* function steamHttpCandidates(
  url: string,
  options: { method?: string; headers?: Record<string, string>; body?: string },
): Array<{ url: string; options: typeof options }> {
  let parsed: URL;
  try { parsed = new URL(url); } catch { return [{ url, options }]; }
  if (!isSteamPrimaryHost(parsed.hostname)) return [{ url, options }];

  const path = `${parsed.pathname}${parsed.search}`;
  const accelerated = [
    { url: `http://127.0.0.1:80${path}`, port: 80 },
    { url: `https://127.0.0.1:443${path}`, port: 443 },
  ];
  const local = accelerated.map(({ url: candidateUrl }) => ({
    url: candidateUrl,
    options: { ...options, headers: { ...options.headers, Host: parsed.hostname } },
  }));
  return [...local, { url, options }];
}
*/

/* function isSteamPrimaryHost(host: string): boolean {
  const value = host.toLowerCase();
  return value === 'steampowered.com' || value.endsWith('.steampowered.com') ||
    value === 'steamstatic.com' || value.endsWith('.steamstatic.com') ||
    value === 'akamaihd.net' || value.endsWith('.akamaihd.net') ||
    value === 'eccdnx.com' || value.endsWith('.eccdnx.com');
}
*/

/* function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => window.setTimeout(resolve, Math.max(0, ms)));
}
*/

/* function isSteamNetworkError(error: unknown): boolean {
  const message = String(error instanceof Error ? error.message : error).toLowerCase();
  if (/\b408\b|\b425\b|\b429\b|\b5\d\d\b/.test(message)) return true;
  if (/\b4\d\d\b/.test(message)) return false;
  return /network|timeout|timed out|connection|winhttp|http request failed|failed to download|temporarily unavailable|缃戠粶|璇锋眰澶辫触/.test(message);
}
*/

async function installSteamMediaOnline(
  source: string,
  kind: 'video' | 'image',
  appId: number,
  gameName: string,
  sourceType: string,
  fallbackUrls: string[] = [source],
  target: DynamicBackgroundTarget,
): Promise<DynamicBackgroundState> {
  // Online mode deliberately does not probe or download the media. WebView2
  // owns the request and can move through fallback URLs after playback errors.
  return dynamicBackgroundInstallOnline(source, fallbackUrls, appId, gameName, sourceType, kind, target);
}

function steamVideoPath(url: string): string {
  try {
    return decodeURIComponent(new URL(url).pathname).toLowerCase();
  } catch {
    return url.toLowerCase();
  }
}

function isSteamPrimaryVideo(url: string): boolean {
  const path = steamVideoPath(url);
  // Steam uses extras/*.webm for short animated store-description elements.
  // Playnite's library media comes from the formal movie/trailer media instead.
  return !/(^|\/)extras\//i.test(path);
}

function scoreSteamVideo(url: string): number {
  const path = steamVideoPath(url);
  if (!isSteamPrimaryVideo(url)) return -1000;
  let score = 0;
  if (/(^|\/)movies\//i.test(path)) score += 100;
  if (/highlight|trailer|movie/i.test(path)) score += 20;
  if (/store_item_assets\/steam\/apps\//i.test(path)) score += 5;
  return score;
}

function sortSteamVideos(urls: string[]): string[] {
  return [...new Set(urls)]
    .filter(isSteamPrimaryVideo)
    .sort((a, b) => scoreSteamVideo(b) - scoreSteamVideo(a));
}

function steamMovieUrls(movieId: number): string[] {
  const base = `https://cdn.akamai.steamstatic.com/steam/apps/${movieId}`;
  return [
    `${base}/movie480.webm`,
    `${base}/movie480.mp4`,
    `${base}/movie_max.webm`,
    `${base}/movie_max.mp4`,
  ];
}

function findStorePageMedia(html: string, appId: number): StorePageMedia {
  const text = decodeHtml(html);
  const urls = Array.from(text.matchAll(/https?:\/\/[^"'<>\s]+?\.(?:m3u8|mpd|webm|mp4|jpg|jpeg|png)(?:\?[^"'<>\s]*)?/gi))
    .map((match) => match[0].replace(/[),.;]+$/, ''));
  const hlss = sortSteamVideos(urls.filter((url) => /\.m3u8(?:\?|$)/i.test(url)));
  const webms = sortSteamVideos(urls.filter((url) => /\.webm(?:\?|$)/i.test(url)));
  const mp4s = sortSteamVideos(urls.filter((url) => /\.mp4(?:\?|$)/i.test(url)));
  const hls = hlss[0];
  const webm = webms[0];
  const mp4 = mp4s[0];
  const image = urls.find((url) => /\.(?:jpg|jpeg|png)(?:\?|$)/i.test(url));

  // Some Steam page JSON stores only urlPart (for example extras/foo.webm).
  // Reconstruct that Playnite-style media URL when the absolute URL is omitted.
  if (!hls && !webm && !mp4) {
    const parts = Array.from(text.matchAll(/"urlPart"\s*:\s*"((?:extras\\|movies\\)[^"\s]+\.(?:webm|mp4))"/gi))
      .map((match) => match[1].replace(/\\\\/g, '/'));
    const part = parts.find((candidate) => !/(^|\/)extras\//i.test(candidate));
    if (part) {
      const base = `https://shared.cdn.queniuqe.com/store_item_assets/steam/apps/${appId}/`;
      return /\.webm$/i.test(part) ? { webm: base + part } : { mp4: base + part };
    }
  }
  return { hls, webm, mp4, hlss, webms, mp4s, image };
}

async function requestStorePageMedia(appId: number): Promise<StorePageMedia> {
  const url = `https://store.steampowered.com/app/${appId}/?l=english&cc=us`;
  let response: HttpResponse;
  try {
    response = await requestSteamHttp(url, { headers: { Accept: 'text/html,application/xhtml+xml' } });
  } catch (error) {
    throw new Error(`Steam 商店页访问失败：${(error as Error).message}`);
  }
  if (response.status < 200 || response.status >= 300) throw new Error(`Steam 商店页请求失败（HTTP ${response.status}）`);
  return findStorePageMedia(response.body, appId);
}

function parseVdfValue(text: string, key: string): string {
  const escaped = key.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  return text.match(new RegExp(`"${escaped}"\\s+"([^"]*)"`, 'i'))?.[1]?.replace(/\\\\/g, '\\') || '';
}

async function steamLibraryRoots(): Promise<string[]> {
  const roots: string[] = [];
  for (const root of ['HKCU', 'HKLM']) {
    try {
      const steamPath = await registry.read(root, 'Software\\Valve\\Steam', 'SteamPath');
      if (typeof steamPath === 'string' && steamPath.trim()) roots.push(steamPath.trim().replace(/\//g, '\\'));
    } catch { /* try the next registry root */ }
  }
  const unique = [...new Set(roots)];
  for (const steamPath of [...unique]) {
    try {
      const text = await fs.readTextFile(`${steamPath}\\steamapps\\libraryfolders.vdf`, 2 * 1024 * 1024);
      const matches = text.matchAll(/"path"\s+"([^"]+)"/gi);
      for (const match of matches) {
        const path = match[1].replace(/\\\\/g, '\\').replace(/\//g, '\\');
        if (path && !unique.includes(path)) unique.push(path);
      }
    } catch { /* the default Steam library still remains */ }
  }
  return unique;
}

async function findInstalledSteamApp(title: string): Promise<number | null> {
  const titleKey = normalizeGameTitle(title);
  if (!titleKey) return null;
  let best: { id: number; score: number } | null = null;
  for (const root of await steamLibraryRoots()) {
    const steamApps = `${root}\\steamapps`;
    let entries: Array<{ name?: string; isFile?: boolean }> = [];
    try { entries = await fs.readDir(steamApps); } catch { continue; }
    for (const entry of entries) {
      if (!entry.isFile || !/^appmanifest_\d+\.acf$/i.test(entry.name || '')) continue;
      try {
        const text = await fs.readTextFile(`${steamApps}\\${entry.name}`, 256 * 1024);
        const appId = Number(parseVdfValue(text, 'appid'));
        const name = parseVdfValue(text, 'name');
        const score = scoreSteamTitle(title, name);
        if (Number.isInteger(appId) && appId > 0 && score >= 0.82 && (!best || score > best.score)) {
          best = { id: appId, score };
        }
      } catch { /* skip a changing or malformed manifest */ }
    }
  }
  return best?.id || null;
}

async function searchSteamApp(title: string): Promise<number | null> {
  const tokens = titleTokens(title);
  const queries = [...new Set([
    cleanGameTitle(title),
    tokens.join(' '),
    tokens.length > 3 ? tokens.slice(0, 4).join(' ') : '',
    ...tokens.filter((token) => token.length >= 3),
  ].filter((query) => query.length > 0))];
  const found = new Map<number, SteamSearchItem>();
  let lastError: unknown;
  for (const query of queries) {
    try {
      const result = await requestJson<{ items?: SteamSearchItem[] }>(
        `https://store.steampowered.com/api/storesearch/?term=${encodeURIComponent(query)}&l=english&cc=us`,
      );
      for (const item of Array.isArray(result.items) ? result.items : []) {
        const id = Number(item.id);
        if (Number.isInteger(id) && id > 0 && !found.has(id)) found.set(id, item);
      }
    } catch (error) {
      lastError = error;
    }
  }
  if (!found.size && lastError) throw lastError;
  const candidates = [...found.values()]
    .filter((item) => item.type === undefined || item.type === 'app')
    .map((item) => ({ item, score: scoreSteamTitle(title, item.name || '') }))
    .filter(({ item, score }) => Number(item.id) > 0 && score >= 0.72)
    .sort((a, b) => b.score - a.score);
  return Number(candidates[0]?.item.id) || null;
}

async function resolveAppId(game: DetectedGame): Promise<ResolvedApp> {
  const detectedTitle = detectedGameTitle(game);
  const titleKey = normalizeGameTitle(detectedTitle);

  const mapped = TITLE_APP_IDS[titleKey];
  if (mapped) return { appId: mapped, matchedBy: 'title-map', detectedTitle };

  const installed = await findInstalledSteamApp(detectedTitle);
  if (installed) {
    cacheAppId(titleKey, installed);
    return { appId: installed, matchedBy: 'steam-library', detectedTitle };
  }

  const cached = cachedAppId(titleKey);
  if (cached) return { appId: cached, matchedBy: 'cache', detectedTitle };

  const searched = await searchSteamApp(detectedTitle);
  if (searched) {
    cacheAppId(titleKey, searched);
    return { appId: searched, matchedBy: 'steam-search', detectedTitle };
  }

  const exeKey = normalizeGameTitle(game.name);
  const fallback = EXE_APP_IDS[exeKey];
  if (fallback) return { appId: fallback, matchedBy: 'exe-fallback', detectedTitle };
  throw new Error(`Steam 未找到“${detectedTitle}”对应的游戏本体`);
}

function chooseMedia(data: StoreDetails, pageMedia: StorePageMedia = {}): { url: string; urls: string[]; kind: 'video' | 'image'; source: DynamicBackgroundResult['source'] } {
  const movies = Array.isArray(data.movies) ? data.movies : [];
  const movieUrls = movies
    .filter((item) => item.id || (item.webm && typeof item.webm === 'object') || item.hls_h264)
    .sort((a, b) => Number(Boolean(b.highlight)) - Number(Boolean(a.highlight)))
    .flatMap((item) => {
      const manifests = [
        item.hls_h264,
        ...(Array.isArray(item.dash_manifests) ? item.dash_manifests : []),
      ].filter((url): url is string => typeof url === 'string' && url && /\.m3u8(?:\?|$)/i.test(url));
      const urls = Object.entries(item.webm || {})
        .filter(([, url]) => typeof url === 'string' && url && isSteamPrimaryVideo(url))
        .sort((a, b) => Number(b[0]) - Number(a[0]))
        .map(([, url]) => url);
      urls.unshift(...manifests);
      if (!manifests.length && !urls.length && item.id && Number.isInteger(Number(item.id)) && Number(item.id) > 0) {
        // Older Steam API entries only expose an id. Keep legacy direct files
        // as a last-resort fallback; current entries expose HLS above.
        urls.push(...steamMovieUrls(Number(item.id)));
      }
      return urls;
    });
  if (movieUrls.length) {
    const urls = sortSteamVideos(movieUrls).slice(0, MAX_ONLINE_MEDIA_CANDIDATES);
    return { url: urls[0], urls, kind: 'video', source: 'video' };
  }
  if (pageMedia.hls) return { url: pageMedia.hls, urls: pageMedia.hlss?.length ? pageMedia.hlss : [pageMedia.hls], kind: 'video', source: 'video' };
  if (pageMedia.webm) return { url: pageMedia.webm, urls: pageMedia.webms?.length ? pageMedia.webms : [pageMedia.webm], kind: 'video', source: 'video' };
  if (pageMedia.mp4) return { url: pageMedia.mp4, urls: pageMedia.mp4s?.length ? pageMedia.mp4s : [pageMedia.mp4], kind: 'video', source: 'video' };
  const image = data.background_raw || data.background || data.screenshots?.[0]?.path_full || pageMedia.image;
  if (!image) throw new Error('Steam 没有可用的背景媒体');
  return { url: image, urls: [image], kind: 'image', source: data.background_raw || data.background ? 'background' : 'screenshot' };
}

export async function refreshDynamicBackground(game: DetectedGame | null): Promise<DynamicBackgroundResult | null> {
  if (!game) return refreshDynamicBackgroundImpl(game);
  const key = `${game.pid}:${game.processCreated || 'unknown'}:${game.path || game.name}:${detectedGameTitle(game)}`;
  const existing = dynamicRefreshInFlight;
  if (existing) {
    if (existing.key === key) return existing.promise;
    await existing.promise.catch(() => null);
    // A game switch during a Steam request must queue the new game behind the
    // old request. This avoids native media/config writes racing each other.
    return refreshDynamicBackground(game);
  }
  const refresh = refreshDynamicBackgroundImpl(game);
  dynamicRefreshInFlight = { key, promise: refresh };
  try {
    return await refresh;
  } finally {
    if (dynamicRefreshInFlight?.promise === refresh) dynamicRefreshInFlight = null;
  }
}

async function refreshDynamicBackgroundImpl(game: DetectedGame | null): Promise<DynamicBackgroundResult | null> {
  const cfg = readConfig();
  if (!cfg.enabled || !game) {
    return null;
  }
  if (!game.processCreated) {
    throw new Error('游戏识别阀门未提供进程身份，拒绝加载动态壁纸');
  }
  const valveTarget: DynamicBackgroundTarget = {
    pid: game.pid,
    processCreated: game.processCreated,
  };

  reportDynamicProgress(`正在匹配 Steam：${detectedGameTitle(game)}`);
  const resolved = await resolveAppId(game);
  reportDynamicProgress(`已匹配 Steam AppID：${resolved.appId}`);
  let result: Record<string, { success?: boolean; data?: StoreDetails }>;
  reportDynamicProgress('正在读取 Steam 游戏资料');
  try {
    result = await requestJson<Record<string, { success?: boolean; data?: StoreDetails }>>(
      `https://store.steampowered.com/api/appdetails?appids=${resolved.appId}&l=english&cc=us`,
    );
  } catch (error) {
    throw new Error(`Steam 游戏资料获取失败：${(error as Error).message}`);
  }
  const app = result[String(resolved.appId)];
  if (!app?.success || !app.data) throw new Error(`Steam AppID ${resolved.appId} 的游戏资料不可用`);

  const confidence = scoreSteamTitle(resolved.detectedTitle, app.data.name || '');
  if (confidence < 0.72) {
    throw new Error(`Steam 匹配校验失败：“${resolved.detectedTitle}”不等于“${app.data.name || resolved.appId}”`);
  }

  reportDynamicProgress(`已读取 Steam 正式名称：${app.data.name || resolved.detectedTitle}`);
  let media = chooseMedia(app.data);
  let pageMedia: StorePageMedia = {};
  {
    reportDynamicProgress('正在读取 Steam 商店媒体');
    try {
      pageMedia = await requestStorePageMedia(resolved.appId);
    } catch {
      // AppDetails already contains the image fallback; a blocked store page must not remove it.
    }
    media = chooseMedia(app.data, pageMedia);
    if (media.source === 'video' && (pageMedia.webms?.length || pageMedia.hlss?.length)) {
      // Only add formal page movies as fallbacks. Never reintroduce extras/*.webm.
      media.urls = [...new Set([...media.urls, ...(pageMedia.hlss || []), ...(pageMedia.webms || [])])]
        .slice(0, MAX_ONLINE_MEDIA_CANDIDATES);
    }
  }
  reportDynamicProgress(`正在下载 Steam ${media.source === 'video' ? '视频' : media.source === 'screenshot' ? '截图' : '背景图'}`);
  let state: DynamicBackgroundState | undefined;
  let lastError: unknown;
  try {
    for (const [candidateIndex, candidate] of media.urls.entries()) {
      reportDynamicProgress(`Steam 媒体候选 ${candidateIndex + 1}/${media.urls.length}`);
      try {
        state = await installSteamMediaOnline(candidate, media.kind, resolved.appId, app.data.name || resolved.detectedTitle, media.source, media.urls, valveTarget);
        media.url = candidate;
        break;
      } catch (error) {
        lastError = error;
        reportDynamicProgress(`Steam 媒体候选失败：${(error as Error).message || '未知错误'}`);
      }
    }
    if (!state && media.kind === 'video') {
      const fallbackUrl = app.data.background_raw || app.data.background || app.data.screenshots?.[0]?.path_full;
      if (fallbackUrl) {
        reportDynamicProgress('Steam 视频均短于 30 秒，改用背景图');
        state = await installSteamMediaOnline(
          fallbackUrl,
          'image',
          resolved.appId,
          app.data.name || resolved.detectedTitle,
          app.data.background_raw || app.data.background ? 'background' : 'screenshot',
          [fallbackUrl],
          valveTarget,
        );
        media = {
          url: fallbackUrl,
          urls: [fallbackUrl],
          kind: 'image',
          source: app.data.background_raw || app.data.background ? 'background' : 'screenshot',
        };
      }
    }
    if (!state) throw lastError || new Error('没有找到大于 30 秒的完整视频');
  } catch (error) {
    throw new Error(`Steam 媒体下载失败：${(error as Error).message}`);
  }
  if (Number(state.pid) !== game.pid || String(state.processCreated || '') !== String(game.processCreated)) {
    throw new Error('游戏识别阀门目标在动态壁纸提交时已变化');
  }
  reportDynamicProgress(media.source === 'video'
    ? '已找到 Steam 在线视频，正在缓冲'
    : 'Steam 视频不可用，已回退为在线背景图');
  await fs.writeTextFileAtomic(
    'C:\\SOFT\\YeMan\\PowerControl\\ui-background\\dynamic-cache.json',
    JSON.stringify({
      appId: resolved.appId,
      detectedTitle: resolved.detectedTitle,
      game: app.data.name || resolved.detectedTitle,
      pid: state.pid,
      processCreated: state.processCreated,
      matchedBy: resolved.matchedBy,
      source: media.source,
      url: media.url,
      online: true,
      fallbackUrls: media.urls,
      ts: Date.now(),
    }),
  ).catch(() => {});
  return {
    state,
    appId: resolved.appId,
    gameName: app.data.name || resolved.detectedTitle,
    source: media.source,
    detectedTitle: resolved.detectedTitle,
    matchedBy: resolved.matchedBy,
  };
}

export async function getDynamicBackgroundState(): Promise<DynamicBackgroundState> {
  return dynamicBackgroundGet();
}
