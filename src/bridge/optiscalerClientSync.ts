import { fs, proc, registry } from './api';

// Catalog compatibility with the installed 1.0.7.2 client. This is not an
// anti-cheat override and does not transfer ownership of YMCC's file backups.
interface ClientPaths { games: string; pending: string; clientDirectory: string; queueDirectory: string; }
export function optiClientPaths(appData: string): ClientPaths {
  const root = appData.trim().replace(/\//g, '\\').replace(/\\+$/, '');
  if (!/^(?:[a-z]:\\|\\\\[^\\]+\\[^\\]+)/i.test(root) || /[%\x00-\x1f]/.test(root)
      || root.split('\\').some(part => part === '.' || part === '..')) throw new Error('AppData 路径无效');
  return { games: `${root}\\OptiscalerClient\\games.json`, pending: `${root}\\YeManCC\\opti_client_pending.json`,
    clientDirectory: `${root}\\OptiscalerClient`, queueDirectory: `${root}\\YeManCC` };
}
async function resolvePaths(): Promise<ClientPaths> {
  // fs IPC does not expand %APPDATA%. Shell Folders supplies the actual roaming
  // path; never infer it from YMCC's configurable window-title data directory.
  const root = await registry.read('HKCU', 'Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders', 'AppData');
  if (typeof root !== 'string') throw new Error('无法定位 Windows 漫游配置目录');
  return optiClientPaths(root);
}
const MAX_BYTES = 16 << 20;

type ClientGame = Record<string, unknown>;
export interface OptiClientUpdate {
  executablePath: string;
  name: string;
  installed: boolean;
  version?: string;
}
export interface OptiClientSyncResult {
  status: 'synced' | 'pending' | 'error' | 'idle';
  message: string;
}

export function optiExeKey(value: unknown): string {
  return typeof value === 'string' ? value.trim().replace(/\//g, '\\').replace(/\\+$/, '').toLowerCase() : '';
}
function validUpdate(value: unknown): value is OptiClientUpdate {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return false;
  const item = value as OptiClientUpdate;
  return typeof item.executablePath === 'string' && /^(?:[a-z]:\\|\\\\[^\\]+\\[^\\]+\\)/i.test(item.executablePath)
    && /\.exe$/i.test(item.executablePath) && !/[\x00-\x1f]/.test(item.executablePath)
    && !item.executablePath.split(/[\\/]/).some(part => part === '.' || part === '..')
    && typeof item.name === 'string' && typeof item.installed === 'boolean'
    && (item.version === undefined || typeof item.version === 'string');
}
function directoryOf(executable: string): string {
  return executable.slice(0, executable.lastIndexOf('\\'));
}

// Merge only the selected game's identity/install state. Preserve client fields,
// user preferences, settings and other games, including fields added by upgrades.
export function mergeOptiClientGames(games: ClientGame[], updates: OptiClientUpdate[], newId: () => string): ClientGame[] {
  const merged = games.map(game => ({ ...game }));
  for (const update of updates) {
    if (!validUpdate(update)) throw new Error('OPT 登记目标无效');
    const key = optiExeKey(update.executablePath);
    const directory = directoryOf(update.executablePath);
    let matches = merged.map((game, index) => ({ game, index }))
      .filter(({ game }) => optiExeKey(game.ExecutablePath) === key);
    if (!matches.length) {
      matches = merged.map((game, index) => ({ game, index }))
        .filter(({ game }) => {
          if (game.ExecutablePath) return false;
          const root = optiExeKey(game.InstallPath);
          return root === optiExeKey(directory) || (root && game.Platform === 0
            && typeof game.AppId === 'string' && /^\d+$/.test(game.AppId) && key.startsWith(`${root}\\`));
        });
      if (matches.length > 1) {
        const longest = Math.max(...matches.map(({ game }) => optiExeKey(game.InstallPath).length));
        matches = matches.filter(({ game }) => optiExeKey(game.InstallPath).length === longest);
      }
    }
    // Do not pick between duplicate identities and overwrite somebody's settings.
    if (matches.length > 1) throw new Error('OPT 列表已有重复目标，请在客户端整理后重试');
    let game: ClientGame;
    if (matches.length) {
      game = matches[0].game;
      if (!game.Name) game.Name = update.name;
      if (!game.InstallPath) game.InstallPath = directory;
      game.ExecutablePath = update.executablePath;
    } else {
      game = { Name: update.name, InstallPath: directory, ExecutablePath: update.executablePath,
        Platform: 8, AppId: `Manual_${newId()}` };
      merged.push(game);
    }
    game.IsOptiscalerInstalled = update.installed;
    if (update.installed && update.version) {
      const [version, extra] = update.version.split('/');
      game.OptiscalerVersion = version;
      if (extra) game.Fsr4ExtraVersion = extra;
    } else {
      // An original installation may survive a restore. If its version is not
      // known, clear measured version caches instead of retaining YMCC's version.
      game.OptiscalerVersion = null;
      game.Fsr4ExtraVersion = null;
    }
  }
  return merged;
}

// The client format is an unversioned array. A future envelope is not an empty
// catalog, and an unrecognized queue extension is not permission to consume it.
function boundedJson(text: string): unknown {
  if (new TextEncoder().encode(text).byteLength > MAX_BYTES) throw new Error('OPT 配置超出大小上限');
  const content = text.replace(/^\uFEFF/, '');
  const value: unknown = JSON.parse(content, (_key, item) => {
    if (typeof item === 'number' && (!Number.isFinite(item) || (Number.isInteger(item) && !Number.isSafeInteger(item))))
      throw new Error('OPT 配置数值无法无损保留，已保留原文件');
    return item;
  });
  const stack: {keys: Set<string> | null; key: boolean}[] = [];
  for (const match of content.matchAll(/"(?:\\[\s\S]|[^"\\])*"|[{}\[\],:]/g)) {
    const token = match[0], top = stack[stack.length - 1];
    if (token === '{' || token === '[') {
      stack.push({keys:token === '{' ? new Set<string>() : null,key:token === '{'});
      if (stack.length > 64) throw new Error('OPT 配置嵌套超限，已保留原文件');
    } else if (token === '}' || token === ']') stack.pop();
    else if (token === ',' && top?.keys) top.key = true;
    else if (token === ':') { if (top) top.key = false; }
    else if (token.startsWith('"') && top?.keys && top.key) {
      const key: string = JSON.parse(token);
      if (top.keys.has(key)) throw new Error('OPT 配置存在重复字段，已保留原文件');
      top.keys.add(key); top.key = false;
    }
  }
  return value;
}
async function configText(file: string): Promise<string | null> {
  if (!(await fs.exists(file))) return null;
  return await fs.readTextFile(file, MAX_BYTES, {bounded:true});
}
function configBytes(value: unknown): string {
  const text = JSON.stringify(value, null, 2);
  boundedJson(text); return text;
}
function validGame(game: unknown): game is ClientGame {
  if (!game || typeof game !== 'object' || Array.isArray(game)) return false;
  const row = game as ClientGame;
  for (const key of ['Name','InstallPath','ExecutablePath','AppId','OptiscalerVersion','Fsr4ExtraVersion']) {
    if (row[key] !== undefined && row[key] !== null && typeof row[key] !== 'string') return false;
  }
  if (row.Platform !== undefined && row.Platform !== null &&
      (!Number.isSafeInteger(row.Platform) || (row.Platform as number) < 0)) return false;
  return row.IsOptiscalerInstalled === undefined || row.IsOptiscalerInstalled === null || typeof row.IsOptiscalerInstalled === 'boolean';
}
interface PendingSnapshot { text: string | null; updates: OptiClientUpdate[]; }
const pendingFields = new Set(['executablePath','name','installed','version']);

let syncQueue: Promise<unknown> = Promise.resolve();
function serialized<T>(operation: () => Promise<T>): Promise<T> {
  const next = syncQueue.then(operation, operation);
  syncQueue = next.catch(() => undefined);
  return next;
}
async function readPending(paths: ClientPaths): Promise<PendingSnapshot> {
  const text = await configText(paths.pending);
  if (text === null) return {text:null,updates:[]};
  const value = boundedJson(text);
  if (!Array.isArray(value) || value.length > 1000 || !value.every(validUpdate))
    throw new Error('待登记记录损坏，已保留原文件');
  const keys = new Set<string>();
  for (const item of value) {
    const key = optiExeKey(item.executablePath);
    if (keys.has(key)) throw new Error('待登记记录身份重复，已保留原文件');
    keys.add(key);
  }
  return {text,updates:value};
}
async function writePending(paths: ClientPaths, updates: OptiClientUpdate[], expectedText: string | null): Promise<void> {
  const content = configBytes(updates);
  if (updates.length > 1000) throw new Error('待登记队列已满');
  if (!(await fs.mkdir(paths.queueDirectory))) throw new Error('无法创建待登记目录');
  if ((await readPending(paths)).text !== expectedText) throw new Error('待登记记录已变化，已保留待重试');
  await fs.writeTextFileAtomic(paths.pending, content);
  if ((await readPending(paths)).text !== content) throw new Error('待登记写入后回读不一致，未确认保存或清空');
}
async function clientRunning(): Promise<boolean> {
  // Native proc.running expects image names WITHOUT .exe.
  const state = await proc.running(['OptiscalerClient']);
  if (typeof state?.OptiscalerClient !== 'boolean') throw new Error('无法确认 OPT 客户端是否已退出');
  return state.OptiscalerClient;
}
async function readCatalog(paths: ClientPaths, file = paths.games): Promise<{ text: string | null; games: ClientGame[] }> {
  const text = await configText(file);
  if (text === null) return {text:null,games:[]};
  const games = boundedJson(text);
  if (!Array.isArray(games) || games.length > 20000 || !games.every(validGame))
    throw new Error('OPT 游戏列表格式异常，已保留原文件');
  return {text,games};
}
async function flushPendingLocked(paths: ClientPaths): Promise<OptiClientSyncResult> {
  const pending = await readPending(paths), updates = pending.updates;
  if (!updates.length) return { status: 'idle', message: '' };
  if (await clientRunning()) return { status: 'pending', message: 'OPT 客户端正在运行：登记已暂存，请关闭后从 YMCC 再次打开以同步。' };
  if (updates.some(item => Object.keys(item).some(key => !pendingFields.has(key))))
    return {status:'pending',message:'待登记记录含未确认的扩展或归属，已保留；未更新列表或清除记录。'};
  const original = await readCatalog(paths);
  const merged = mergeOptiClientGames(original.games, updates, () => crypto.randomUUID().slice(0, 8));
  if (merged.length > 20000) throw new Error('OPT 游戏列表已满');
  const content = configBytes(merged);
  if (!(await fs.mkdir(paths.clientDirectory))) throw new Error('无法创建 OPT 配置目录');
  // Compare exact observed bytes; this is not a cross-process atomic CAS.
  const baseline = await readCatalog(paths);
  if (baseline.text !== original.text) throw new Error('OPT 列表已被其他程序修改，登记已保留待重试');
  if ((await readPending(paths)).text !== pending.text) throw new Error('待登记记录已变化，已保留待重试');
  if (await clientRunning()) return { status: 'pending', message: 'OPT 客户端已启动：登记已暂存，请关闭后重试。' };
  if (original.text !== null) {
    const backup = `${paths.games}.ymcc.bak`;
    await readCatalog(paths, backup); // Never replace a damaged or future backup.
    await fs.writeTextFileAtomic(backup, original.text);
    if ((await readCatalog(paths, backup)).text !== original.text) throw new Error('OPT 备份回读不一致，未覆盖列表');
  }
  if ((await readCatalog(paths)).text !== original.text) throw new Error('OPT 列表已被其他程序修改，登记已保留待重试');
  if ((await readPending(paths)).text !== pending.text) throw new Error('待登记记录已变化，已保留待重试');
  if (await clientRunning()) return {status:'pending',message:'OPT 客户端已启动，未覆盖列表；登记已保留。'};
  await fs.writeTextFileAtomic(paths.games, content);
  const verified = await readCatalog(paths);
  if (verified.text !== content) throw new Error('OPT 登记写入后发生冲突，待登记记录已保留');
  await writePending(paths, [], pending.text);
  return { status: 'synced', message: `已同步 ${updates.length} 个游戏到 OPT 客户端列表；不代表插帧已生效。` };
}
function failed(error: unknown): OptiClientSyncResult {
  return { status: 'error', message: `OPT 客户端登记未完成：${error instanceof Error ? error.message : String(error)}。不影响已完成的 OPT 文件操作。` };
}
export async function syncOptiClientGame(gamePath: string, installed: boolean, version?: string): Promise<OptiClientSyncResult> {
  return serialized(async () => {
    try {
      const paths = await resolvePaths();
      const executablePath = gamePath.trim().replace(/\//g, '\\');
      const update: OptiClientUpdate = { executablePath,
        name: executablePath.split('\\').pop()!.replace(/\.exe$/i, ''), installed, ...(version ? { version } : {}) };
      if (!validUpdate(update)) throw new Error('目标游戏 EXE 路径无效');
      const snapshot = await readPending(paths), pending = snapshot.updates;
      const index = pending.findIndex(item => optiExeKey(item.executablePath) === optiExeKey(executablePath));
      if (index >= 0) {
        pending[index] = {...pending[index],...update};
        if (update.version === undefined) delete pending[index].version;
      } else pending.push(update);
      if (pending.length > 1000) throw new Error('待登记队列已满');
      await writePending(paths, pending, snapshot.text);
      return await flushPendingLocked(paths);
    } catch (error) { return failed(error); }
  });
}
export async function flushOptiClientGames(): Promise<OptiClientSyncResult> {
  return serialized(async () => {
    try { return await flushPendingLocked(await resolvePaths()); }
    catch (error) { return failed(error); }
  });
}
