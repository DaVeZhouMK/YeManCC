import { app, fs } from './api';
import { requestSteamHttp } from './steamHttp';

export type SteamChartMode = 'steam' | 'steamdeck';

export interface SteamChartItem {
  rank: number;
  appId: number;
  name: string;
  url: string;
  imageUrl: string;
  fallbackImageUrl: string;
}

export interface SteamChartsCache {
  schemaVersion: 2;
  mode: SteamChartMode;
  source: string;
  refreshedAt: string;
  period: string;
  items: SteamChartItem[];
}

export interface SteamChartsResult extends SteamChartsCache {
  fromCache: boolean;
  stale: boolean;
  cachePersisted?: boolean;
  error?: string;
}

export const STEAM_CHARTS_URL = 'https://store.steampowered.com/charts/steamdecktopplayed';
export const STEAMDECK_CHARTS_API = 'https://api.steampowered.com/ISteamChartsService/GetMostPlayedSteamDeckGames/v1/';
const STEAM_FEATURED_URL = 'https://store.steampowered.com/api/featuredcategories/?l=english&cc=us';
const STEAM_APP_DETAILS_URL = 'https://store.steampowered.com/api/appdetails';
const CACHE_FOLDER = 'SteamCharts';
const CACHE_MAX_AGE_MS = 6 * 60 * 60 * 1000;
const CACHE_MAX_BYTES = 2 * 1024 * 1024;
const HTTP_MAX_BYTES = 8 * 1024 * 1024;
const IO_WAIT_MS = 1500;
const HTTP_WAIT_MS = 5000;
const FUTURE_CLOCK_SKEW_MS = 5 * 60 * 1000;
interface CachePaths { root: string; dir: string; steamFile: string; steamDeckFile: string; }
interface FileStamp { size: number; modified: number; isFile: boolean; isDir: boolean; }
interface CacheRead { kind: 'valid' | 'missing' | 'corrupt' | 'protected' | 'unknown'; paths?: CachePaths; stamp?: FileStamp; raw?: string; cache?: SteamChartsCache; error?: string; }
interface LoadJob { alive: boolean; }
let cachePathPromise: Promise<CachePaths> | null = null;
let serial = 0;
const activeLoads: Partial<Record<SteamChartMode, { force: boolean; promise: Promise<SteamChartsResult> }>> = {};
// A timed-out rename cannot be cancelled by this IPC API. Fence this renderer's
// later commits until the dispatched mutation settles; callers still finish.
const pendingMutations = new Map<string, Promise<unknown>>();

function bounded<T>(action: () => Promise<T>, ms: number, reason: string): Promise<T> {
  return new Promise((resolve, reject) => {
    let finished = false;
    const timer = setTimeout(() => { finished = true; reject(new Error(reason)); }, ms);
    Promise.resolve().then(action).then(value => {
      if (!finished) { finished = true; clearTimeout(timer); resolve(value); }
    }, error => { if (!finished) { finished = true; clearTimeout(timer); reject(error); } });
  });
}
function io<T>(action: () => Promise<T>): Promise<T> { return bounded(action, IO_WAIT_MS, 'charts-cache-io-timeout'); }
function current(job: LoadJob): void { if (!job.alive) throw new Error('charts-request-obsolete'); }
function normalizePath(value: unknown): string {
  if (typeof value !== 'string' || !/^[a-z]:[\\/]/i.test(value) || /[\x00-\x1f<>"|?*]/.test(value)) throw new Error('charts-cache-location-invalid');
  const root = value.replace(/\//g, '\\').replace(/\\+$/, '');
  if (root.length <= 3 || root.slice(2).includes(':') || root.slice(3).split('\\').some(part => !part || part === '.' || part === '..' || /[. ]$/.test(part))) throw new Error('charts-cache-location-invalid');
  return root;
}
function stamp(value: unknown, directory = false): FileStamp {
  const v = value as FileStamp;
  if (!v || typeof v !== 'object' || !Number.isSafeInteger(v.size) || v.size < 0 || !Number.isFinite(v.modified) || v.isDir !== directory || v.isFile !== !directory) throw new Error('charts-cache-stat-invalid');
  return v;
}
async function exists(file: string): Promise<boolean> {
  const value = await io(() => fs.exists(file, IO_WAIT_MS));
  if (typeof value !== 'boolean') throw new Error('charts-cache-exists-invalid');
  return value;
}
function sameStamp(a: FileStamp, b: FileStamp): boolean { return a.size === b.size && a.modified === b.modified && a.isFile === b.isFile; }
async function getCachePath(): Promise<CachePaths> {
  if (cachePathPromise) return cachePathPromise;
  const pending = (async () => {
    const root = normalizePath(await io(() => app.powerControlDir()));
    stamp(await io(() => fs.stat(root)), true);
    const dir = root + '\\' + CACHE_FOLDER;
    return { root, dir, steamFile: dir + '\\steam.json', steamDeckFile: dir + '\\steamdeck.json' };
  })();
  cachePathPromise = pending;
  const clear = () => { if (cachePathPromise === pending) cachePathPromise = null; };
  void pending.then(clear, clear); // Cache only an in-flight lookup, including no rejected/default root.
  return pending;
}
function cacheFileFor(mode: SteamChartMode, paths: CachePaths): string { return mode === 'steamdeck' ? paths.steamDeckFile : paths.steamFile; }
function sourceFor(mode: SteamChartMode): string { return mode === 'steamdeck' ? STEAM_CHARTS_URL : STEAM_FEATURED_URL; }
function secureUrl(value: unknown): value is string {
  if (typeof value !== 'string' || value.length > 4096) return false;
  try { const url = new URL(value); return url.protocol === 'https:' && !!url.hostname && !url.username && !url.password; } catch { return false; }
}
function isValidItem(value: unknown): value is SteamChartItem {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return false;
  const item = value as SteamChartItem;
  return Number.isInteger(item.rank) && item.rank > 0 && item.rank <= 20 &&
    Number.isSafeInteger(item.appId) && item.appId > 0 && item.appId <= 0xffffffff &&
    typeof item.name === 'string' && item.name.trim().length > 0 && item.name.length <= 512 &&
    typeof item.url === 'string' && new RegExp('^https://store\\.steampowered\\.com/app/' + item.appId + '/?$').test(item.url) &&
    secureUrl(item.imageUrl) && secureUrl(item.fallbackImageUrl);
}
function normalizeCache(value: unknown, mode: SteamChartMode): SteamChartsCache | null {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return null;
  const raw = value as SteamChartsCache, date = typeof raw.refreshedAt === 'string' ? raw.refreshedAt : '';
  const time = Date.parse(date);
  if (raw.schemaVersion !== 2 || raw.mode !== mode || raw.source !== sourceFor(mode) || date.length > 64 ||
      !Number.isFinite(time) || time < 0 || time > Date.now() + FUTURE_CLOCK_SKEW_MS || !Array.isArray(raw.items) ||
      raw.items.length < (mode === 'steamdeck' ? 20 : 5) || raw.items.length > 20 || !raw.items.every(isValidItem)) return null;
  const items = [...raw.items].sort((a, b) => a.rank - b.rank);
  if (new Set(items.map(item => item.appId)).size !== items.length || items.some((item, i) => item.rank !== i + 1)) return null;
  return { schemaVersion: 2, mode, source: sourceFor(mode), refreshedAt: date,
    period: typeof raw.period === 'string' && raw.period.trim() ? raw.period.trim() : '过去一周', items };
}
async function text(file: string): Promise<string> {
  const value = await io(() => fs.readTextFile(file, CACHE_MAX_BYTES, { bounded: true, timeoutMs: IO_WAIT_MS }));
  if (typeof value !== 'string' || new TextEncoder().encode(value).byteLength > CACHE_MAX_BYTES) throw new Error('charts-cache-read-invalid');
  return value;
}
async function readCache(mode: SteamChartMode): Promise<CacheRead> {
  let paths: CachePaths | undefined;
  try {
    paths = await getCachePath(); const file = cacheFileFor(mode, paths);
    if (!await exists(file)) return { kind: 'missing', paths };
    const before = stamp(await io(() => fs.stat(file)));
    if (before.size > CACHE_MAX_BYTES) return { kind: 'corrupt', paths, stamp: before, error: 'charts-cache-size-limit' };
    const raw = await text(file), after = stamp(await io(() => fs.stat(file)));
    if (!sameStamp(before, after) || new TextEncoder().encode(raw).byteLength !== before.size) throw new Error('charts-cache-changed-during-read');
    let parsed: unknown; try { parsed = JSON.parse(raw); } catch { return { kind: 'corrupt', paths, stamp: before, raw }; }
    if (parsed && typeof parsed === 'object' && 'schemaVersion' in parsed && (parsed as SteamChartsCache).schemaVersion !== 2)
      return { kind: 'protected', paths, stamp: before, error: 'charts-cache-schema-unsupported' };
    const cache = normalizeCache(parsed, mode);
    return { kind: cache ? 'valid' : 'corrupt', paths, stamp: before, raw, ...(cache ? { cache } : {}) };
  } catch (error) { return { kind: 'unknown', paths, error: error instanceof Error ? error.message : 'charts-cache-unavailable' }; }
}
async function unchanged(file: string, before: CacheRead): Promise<void> {
  const present = await exists(file);
  if (before.kind === 'missing') { if (present) throw new Error('charts-cache-writer-conflict'); return; }
  if (!present || !before.stamp || !sameStamp(before.stamp, stamp(await io(() => fs.stat(file))))) throw new Error('charts-cache-writer-conflict');
  // Native stat has coarse timestamps. Compare the bounded original bytes too;
  // these checks are optimistic guards, not a cross-process atomic CAS.
  if (before.raw !== undefined && await text(file) !== before.raw) throw new Error('charts-cache-writer-conflict');
}
async function rename(file: string, from: string, to: string): Promise<void> {
  if (pendingMutations.has(file)) throw new Error('charts-cache-commit-pending');
  const operation = Promise.resolve().then(() => fs.rename(from, to));
  pendingMutations.set(file, operation);
  const clear = () => { if (pendingMutations.get(file) === operation) pendingMutations.delete(file); };
  void operation.then(clear, clear);
  if (await io(() => operation) !== true) throw new Error('charts-cache-rename-invalid');
}
async function writeCache(value: SteamChartsCache, before: CacheRead, job: LoadJob): Promise<void> {
  if (!before.paths || before.kind === 'unknown' || before.kind === 'protected') throw new Error(before.error || 'charts-cache-write-protected');
  const paths = before.paths, file = cacheFileFor(value.mode, paths);
  if (pendingMutations.has(file)) throw new Error('charts-cache-commit-pending');
  current(job);
  if (normalizePath(await io(() => app.powerControlDir())).toLowerCase() !== paths.root.toLowerCase()) throw new Error('charts-cache-location-changed');
  stamp(await io(() => fs.stat(paths.root)), true);
  if (!await exists(paths.dir) && await io(() => fs.mkdir(paths.dir, IO_WAIT_MS)) !== true) throw new Error('charts-cache-mkdir-invalid');
  stamp(await io(() => fs.stat(paths.dir)), true);
  const nonce = Date.now().toString(36) + '-' + (++serial).toString(36) + '-' + Math.random().toString(36).slice(2);
  const staged = file + '.next-' + nonce, bytes = JSON.stringify(value, null, 2);
  current(job); await io(() => fs.writeTextFileAtomic(staged, bytes, IO_WAIT_MS));
  if (await text(staged) !== bytes || stamp(await io(() => fs.stat(staged))).size !== new TextEncoder().encode(bytes).byteLength) throw new Error('charts-cache-write-unverified');
  current(job); await unchanged(file, before); current(job);
  if (before.kind === 'corrupt') {
    const backup = file + '.corrupt-' + nonce;
    if (await exists(backup)) throw new Error('charts-cache-backup-collision');
    await rename(file, file, backup); current(job);
    if (!before.stamp || !sameStamp(before.stamp, stamp(await io(() => fs.stat(backup)))) || await exists(file)) throw new Error('charts-cache-backup-unverified');
    // rename retains the native original bytes, including oversized/undecodable files.
  }
  if (before.kind === 'corrupt' && await exists(file)) throw new Error('charts-cache-writer-conflict');
  current(job); await rename(file, staged, file); current(job);
  if (await text(file) !== bytes) throw new Error('charts-cache-commit-unverified');
}

function canonicalAppUrl(appId: number): string { return `https://store.steampowered.com/app/${appId}/`; }
function fallbackCapsuleUrl(appId: number): string { return `https://cdn.cloudflare.steamstatic.com/steam/apps/${appId}/capsule_231x87.jpg`; }
function makeItem(rank: number, appId: number, name: string, imageUrl = ''): SteamChartItem {
  return { rank, appId, name: name.trim(), url: canonicalAppUrl(appId),
    imageUrl: imageUrl.startsWith('https://') ? imageUrl : fallbackCapsuleUrl(appId),
    fallbackImageUrl: fallbackCapsuleUrl(appId) };
}
function makeCache(mode: SteamChartMode, period: string, items: SteamChartItem[]): SteamChartsCache {
  return { schemaVersion: 2, mode, source: sourceFor(mode), refreshedAt: new Date().toISOString(), period,
    items: items.sort((a, b) => a.rank - b.rank).slice(0, 20) };
}

function parseChartHtml(body: string): SteamChartsCache {
  if (!body || body.length > 8 * 1024 * 1024) throw new Error('Steam 榜单响应无效');
  const document = new DOMParser().parseFromString(body, 'text/html');
  const items: SteamChartItem[] = [];
  const seen = new Set<number>();
  for (const row of Array.from(document.querySelectorAll('table tbody tr'))) {
    const cells = Array.from(row.querySelectorAll('td'));
    const rank = Number.parseInt((cells[1]?.textContent || '').trim(), 10);
    const nameLink = cells[2]?.querySelector<HTMLAnchorElement>('a[href*="/app/"]') || row.querySelector<HTMLAnchorElement>('a[href*="/app/"]');
    const href = nameLink?.getAttribute('href') || '';
    const appId = Number.parseInt(href.match(/\/app\/(\d+)/i)?.[1] || '', 10);
    const name = (nameLink?.textContent || '').replace(/\s+/g, ' ').trim();
    const image = row.querySelector<HTMLImageElement>('img');
    const imageUrl = image?.getAttribute('src') || image?.getAttribute('data-src') || '';
    if (!Number.isInteger(rank) || rank < 1 || rank > 20 || !Number.isInteger(appId) || !name || seen.has(appId)) continue;
    seen.add(appId); items.push(makeItem(rank, appId, name, imageUrl));
  }
  if (items.length < 20) throw new Error(`Steam 榜单数据不完整（仅读取到 ${items.length} 项）`);
  return makeCache('steam', '过去一周', items);
}

interface FeaturedCategoryItem { id?: number; type?: number; name?: string; header_image?: string; large_capsule_image?: string; small_capsule_image?: string; }
interface FeaturedCategoriesResponse { top_sellers?: FeaturedCategoryItem[] | { items?: FeaturedCategoryItem[] }; }
function parseFeaturedCategories(body: string): SteamChartsCache {
  let payload: FeaturedCategoriesResponse;
  try { payload = JSON.parse(body) as FeaturedCategoriesResponse; } catch { throw new Error('Steam 热门接口返回的资料格式无法读取'); }
  const source = Array.isArray(payload.top_sellers) ? payload.top_sellers : Array.isArray(payload.top_sellers?.items) ? payload.top_sellers.items : [];
  const items: SteamChartItem[] = []; const seen = new Set<number>();
  for (const item of source) {
    const appId = item.id; const name = typeof item.name === 'string' ? item.name.trim() : '';
    if (item.type !== undefined && item.type !== 0 || !Number.isSafeInteger(appId) || !appId || appId <= 0 || !name || seen.has(appId)) continue;
    const imageUrl = [item.header_image, item.large_capsule_image, item.small_capsule_image].find((v): v is string => typeof v === 'string' && v.startsWith('https://')) || '';
    seen.add(appId); items.push(makeItem(items.length + 1, appId, name, imageUrl));
    if (items.length >= 20) break;
  }
  // 普通 Steam 沿用原有兼容阈值：接口返回至少 5 项即可显示；SteamDeck
  // 则必须有完整前 20，避免把不完整的设备榜单显示成正式排名。
  if (items.length < 5) throw new Error(`Steam 热门数据不完整（仅读取到 ${items.length} 项）`);
  return makeCache('steam', '当前热门', items);
}

interface AppDetailsResponse { [appId: string]: { success?: boolean; data?: { name?: string; header_image?: string; small_capsule_image?: string } }; }

async function hydrateSteamDeckRanks(ranks: Array<{ rank: number; appId: number }>, request: typeof requestSteamHttp): Promise<SteamChartsCache> {
  const ids = ranks.map((item) => item.appId).join(',');
  const response = await request(`${STEAM_APP_DETAILS_URL}?appids=${ids}&l=schinese&cc=cn`, {
    headers: { Accept: 'application/json', 'Accept-Encoding': 'identity', Connection: 'close' },
  });
  let payload: AppDetailsResponse;
  try { payload = JSON.parse(response.body) as AppDetailsResponse; } catch { throw new Error('SteamDeck 游戏资料格式无法读取'); }
  const items = ranks.map((item) => {
    if (payload[String(item.appId)]?.success !== true) throw new Error('SteamDeck 游戏资料未确认成功');
    const data = payload[String(item.appId)]?.data;
    const name = typeof data?.name === 'string' ? data.name.trim() : '';
    if (!name) throw new Error(`SteamDeck AppID ${item.appId} 缺少游戏名称`);
    return makeItem(item.rank, item.appId, name, data?.header_image || data?.small_capsule_image || '');
  });
  return makeCache('steamdeck', '过去一周', items);
}

async function loadOnline(mode: SteamChartMode, request: typeof requestSteamHttp): Promise<SteamChartsCache> {
  if (mode === 'steam') {
    const featured = await request(STEAM_FEATURED_URL, { headers: { Accept: 'application/json', 'Accept-Encoding': 'identity', Connection: 'close' } });
    try { return parseFeaturedCategories(featured.body); } catch {
      const response = await request(STEAM_CHARTS_URL, { headers: { Accept: 'text/html,application/xhtml+xml', 'Accept-Language': 'zh-CN,zh;q=0.9,en;q=0.7', 'Accept-Encoding': 'identity', Connection: 'close' } });
      return parseChartHtml(response.body);
    }
  }
  const requestBytes = new Uint8Array([0x18, 0x01, 0x20, 0x64]);
  const encoded = btoa(String.fromCharCode(...requestBytes));
  const response = await request(`${STEAMDECK_CHARTS_API}?input_protobuf_encoded=${encodeURIComponent(encoded)}&format=json`, { headers: { Accept: 'application/json', 'Accept-Encoding': 'identity', Connection: 'close' } });
  try {
    const payload = JSON.parse(response.body) as { response?: { ranks?: Array<{ rank?: number; appid?: number }> } };
    if (!Array.isArray(payload.response?.ranks)) throw new Error('SteamDeck JSON 榜单无效');
    const ranks = payload.response.ranks.map((item) => ({ rank: item.rank, appId: item.appid }))
      .filter((item): item is { rank: number; appId: number } => Number.isInteger(item.rank) && !!item.rank && item.rank > 0 && item.rank <= 20 && Number.isSafeInteger(item.appId) && !!item.appId && item.appId > 0 && item.appId <= 0xffffffff)
      .sort((a, b) => a.rank - b.rank);
    if (ranks.length !== 20 || new Set(ranks.map(item => item.appId)).size !== 20 || ranks.some((item, i) => item.rank !== i + 1)) throw new Error('SteamDeck JSON 榜单不完整');
    return hydrateSteamDeckRanks(ranks, request);
  } catch (error) { throw error instanceof Error ? error : new Error('SteamDeck 榜单返回格式无法读取'); }
}

function resultFromCache(cache: SteamChartsCache, now: number, error?: string): SteamChartsResult {
  return { ...cache, fromCache: true, stale: !!error || now - Date.parse(cache.refreshedAt) >= CACHE_MAX_AGE_MS, cachePersisted: true, ...(error ? { error } : {}) };
}
function emptyResult(mode: SteamChartMode, error: string): SteamChartsResult {
  return { schemaVersion: 2, mode, source: sourceFor(mode), refreshedAt: '', period: '', items: [], fromCache: false, stale: true, cachePersisted: false, error };
}
export async function loadSteamCharts(mode: SteamChartMode = 'steamdeck', force = false): Promise<SteamChartsResult> {
  const active = activeLoads[mode];
  if (active) {
    if (!force || active.force) return structuredClone(await active.promise);
    await active.promise.catch(() => {});
    return loadSteamCharts(mode, true);
  }
  const job: LoadJob = { alive: true };
  const promise = (async (): Promise<SteamChartsResult> => {
    const before = await readCache(mode), cached = before.cache, now = Date.now();
    if (!force && cached && now - Date.parse(cached.refreshedAt) < CACHE_MAX_AGE_MS) return resultFromCache(cached, now);
    let next: SteamChartsCache;
    try {
      const request: typeof requestSteamHttp = async (url, options) => {
        current(job);
        const response = await bounded(() => requestSteamHttp(url, options), HTTP_WAIT_MS, 'charts-http-timeout');
        current(job);
        if (!response || typeof response !== 'object' || Array.isArray(response) || !Number.isInteger(response.status) || response.status < 200 || response.status >= 300 || typeof response.body !== 'string' || new TextEncoder().encode(response.body).byteLength > HTTP_MAX_BYTES) throw new Error('charts-http-response-invalid');
        return response;
      };
      const parsed = await loadOnline(mode, request), normalized = normalizeCache(parsed, mode);
      if (!normalized) throw new Error('charts-online-records-invalid');
      next = normalized;
    } catch (error) {
      const reason = error instanceof Error ? error.message : 'charts-online-unavailable';
      return cached ? resultFromCache(cached, now, reason) : emptyResult(mode, reason);
    }
    try { await writeCache(next, before, job); return { ...next, fromCache: false, stale: false, cachePersisted: true }; }
    catch (error) { return { ...next, fromCache: false, stale: false, cachePersisted: false, error: error instanceof Error ? error.message : 'charts-cache-write-failed' }; }
  })();
  activeLoads[mode] = { force, promise };
  try { return structuredClone(await promise); }
  finally { job.alive = false; if (activeLoads[mode]?.promise === promise) delete activeLoads[mode]; }
}
