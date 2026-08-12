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
}

export const STEAM_CHARTS_URL = 'https://store.steampowered.com/charts/steamdecktopplayed';
export const STEAMDECK_CHARTS_API = 'https://api.steampowered.com/ISteamChartsService/GetMostPlayedSteamDeckGames/v1/';
const STEAM_FEATURED_URL = 'https://store.steampowered.com/api/featuredcategories/?l=english&cc=us';
const STEAM_APP_DETAILS_URL = 'https://store.steampowered.com/api/appdetails';
const CACHE_FOLDER = 'SteamCharts';
const CACHE_MAX_AGE_MS = 6 * 60 * 60 * 1000;
const FALLBACK_POWER_CONTROL_DIR = 'C:\\SOFT\\YeMan\\PowerControl';

let cachePathPromise: Promise<{ dir: string; steamFile: string; steamDeckFile: string }> | null = null;
const activeLoads: Partial<Record<SteamChartMode, Promise<SteamChartsResult>>> = {};

function normalizePath(value: string): string {
  return value.replace(/[\\/]+$/, '').replace(/\//g, '\\');
}

async function getCachePath(): Promise<{ dir: string; steamFile: string; steamDeckFile: string }> {
  if (!cachePathPromise) {
    cachePathPromise = (async () => {
      let root = FALLBACK_POWER_CONTROL_DIR;
      try {
        const configured = await app.powerControlDir();
        if (configured.trim()) root = configured;
      } catch {
        // Dev mode and old shells may not expose this command yet.
      }
      const dir = `${normalizePath(root)}\\${CACHE_FOLDER}`;
      return { dir, steamFile: `${dir}\\steam.json`, steamDeckFile: `${dir}\\steamdeck.json` };
    })();
  }
  return cachePathPromise;
}

function cacheFileFor(mode: SteamChartMode, paths: Awaited<ReturnType<typeof getCachePath>>): string {
  return mode === 'steamdeck' ? paths.steamDeckFile : paths.steamFile;
}

function sourceFor(mode: SteamChartMode): string {
  return mode === 'steamdeck' ? STEAM_CHARTS_URL : STEAM_FEATURED_URL;
}

function isValidItem(value: unknown): value is SteamChartItem {
  if (!value || typeof value !== 'object') return false;
  const item = value as Partial<SteamChartItem>;
  return Number.isInteger(item.rank) && item.rank > 0 && item.rank <= 20 &&
    Number.isInteger(item.appId) && item.appId > 0 &&
    typeof item.name === 'string' && item.name.trim().length > 0 &&
    typeof item.url === 'string' && item.url.startsWith('https://store.steampowered.com/app/') &&
    typeof item.imageUrl === 'string' && item.imageUrl.startsWith('https://') &&
    typeof item.fallbackImageUrl === 'string' && item.fallbackImageUrl.startsWith('https://');
}

function normalizeCache(value: unknown, mode: SteamChartMode): SteamChartsCache | null {
  if (!value || typeof value !== 'object') return null;
  const raw = value as Partial<SteamChartsCache>;
  const date = typeof raw.refreshedAt === 'string' ? raw.refreshedAt : '';
  if (raw.schemaVersion !== 2 || raw.mode !== mode || raw.source !== sourceFor(mode) ||
      !Number.isFinite(Date.parse(date)) || !Array.isArray(raw.items)) return null;
  const items = raw.items.filter(isValidItem).sort((a, b) => a.rank - b.rank).slice(0, 20);
  if (items.length < (mode === 'steamdeck' ? 20 : 5)) return null;
  return { schemaVersion: 2, mode, source: sourceFor(mode), refreshedAt: date,
    period: typeof raw.period === 'string' && raw.period.trim() ? raw.period.trim() : '过去一周', items };
}

async function readCache(mode: SteamChartMode): Promise<SteamChartsCache | null> {
  try {
    const paths = await getCachePath();
    const raw = await fs.readTextFile(cacheFileFor(mode, paths), 2 * 1024 * 1024);
    return normalizeCache(JSON.parse(raw), mode);
  } catch { return null; }
}

async function writeCache(value: SteamChartsCache): Promise<void> {
  const paths = await getCachePath();
  await fs.mkdir(paths.dir);
  await fs.writeTextFileAtomic(cacheFileFor(value.mode, paths), JSON.stringify(value, null, 2));
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
    const appId = Number(item.id); const name = typeof item.name === 'string' ? item.name.trim() : '';
    if (item.type !== undefined && item.type !== 0 || !Number.isInteger(appId) || appId <= 0 || !name || seen.has(appId)) continue;
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

async function hydrateSteamDeckRanks(ranks: Array<{ rank: number; appId: number }>): Promise<SteamChartsCache> {
  const ids = ranks.map((item) => item.appId).join(',');
  const response = await requestSteamHttp(`${STEAM_APP_DETAILS_URL}?appids=${ids}&l=schinese&cc=cn`, {
    headers: { Accept: 'application/json', 'Accept-Encoding': 'identity', Connection: 'close' },
  });
  let payload: AppDetailsResponse;
  try { payload = JSON.parse(response.body) as AppDetailsResponse; } catch { throw new Error('SteamDeck 游戏资料格式无法读取'); }
  const items = ranks.map((item) => {
    const data = payload[String(item.appId)]?.data;
    const name = typeof data?.name === 'string' ? data.name.trim() : '';
    if (!name) throw new Error(`SteamDeck AppID ${item.appId} 缺少游戏名称`);
    return makeItem(item.rank, item.appId, name, data?.header_image || data?.small_capsule_image || '');
  });
  return makeCache('steamdeck', '过去一周', items);
}

async function loadOnline(mode: SteamChartMode): Promise<SteamChartsCache> {
  if (mode === 'steam') {
    const featured = await requestSteamHttp(STEAM_FEATURED_URL, { headers: { Accept: 'application/json', 'Accept-Encoding': 'identity', Connection: 'close' } });
    try { return parseFeaturedCategories(featured.body); } catch {
      const response = await requestSteamHttp(STEAM_CHARTS_URL, { headers: { Accept: 'text/html,application/xhtml+xml', 'Accept-Language': 'zh-CN,zh;q=0.9,en;q=0.7', 'Accept-Encoding': 'identity', Connection: 'close' } });
      return parseChartHtml(response.body);
    }
  }
  const requestBytes = new Uint8Array([0x18, 0x01, 0x20, 0x64]);
  const encoded = btoa(String.fromCharCode(...requestBytes));
  const response = await requestSteamHttp(`${STEAMDECK_CHARTS_API}?input_protobuf_encoded=${encodeURIComponent(encoded)}&format=json`, { headers: { Accept: 'application/json', 'Accept-Encoding': 'identity', Connection: 'close' } });
  try {
    const payload = JSON.parse(response.body) as { response?: { ranks?: Array<{ rank?: number; appid?: number }> } };
    const ranks = (payload.response?.ranks || []).map((item) => ({ rank: Number(item.rank), appId: Number(item.appid) }))
      .filter((item) => Number.isInteger(item.rank) && item.rank > 0 && Number.isInteger(item.appId) && item.appId > 0)
      .sort((a, b) => a.rank - b.rank).slice(0, 20);
    if (ranks.length < 20) throw new Error('SteamDeck JSON 榜单不完整');
    return hydrateSteamDeckRanks(ranks);
  } catch (error) { throw error instanceof Error ? error : new Error('SteamDeck 榜单返回格式无法读取'); }
}

function resultFromCache(cache: SteamChartsCache, now: number): SteamChartsResult {
  return { ...cache, fromCache: true, stale: now - Date.parse(cache.refreshedAt) >= CACHE_MAX_AGE_MS };
}

export async function loadSteamCharts(mode: SteamChartMode = 'steamdeck', force = false): Promise<SteamChartsResult> {
  if (activeLoads[mode]) return activeLoads[mode]!;
  activeLoads[mode] = (async () => {
    const cached = await readCache(mode); const now = Date.now();
    if (!force && cached && now - Date.parse(cached.refreshedAt) < CACHE_MAX_AGE_MS) return resultFromCache(cached, now);
    try { const next = await loadOnline(mode); await writeCache(next); return { ...next, fromCache: false, stale: false }; }
    catch (error) { if (cached) return resultFromCache(cached, now); throw error instanceof Error ? error : new Error(String(error)); }
  })();
  try { return await activeLoads[mode]!; } finally { delete activeLoads[mode]; }
}
