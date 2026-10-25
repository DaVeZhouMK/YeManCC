import { app, fs, shell } from './api';
import { cleanGameTitle } from './gamedetect';
import { getSettingsGeneration } from './settingsRepository';
import { reportFrontendError } from '@/robust/frontendDiagnostics';

export interface GameTrainerIdentity {
  method: string;
  steamAppId?: string;
  manifestPath?: string;
  librarySourcePath?: string;
  aliases?: string[];
  processId?: number;
  processCreated?: string;
  executablePath?: string;
  executableSha256?: string;
  windowTitle?: string;
  gameName?: string;
  snapshotPath?: string;
}
export interface InstalledGameTrainer {
  gameName: string;
  origin: string;
  gcmResourceId?: string;
  folder: string;
  path: string;
  modified: number;
  sha256?: string;
  identity?: GameTrainerIdentity;
}
export type TrainerPreparationState = 'queued' | 'searching' | 'downloading' | 'ready' | 'unavailable' | 'failed' | 'cancelled' | 'budget-limited';
export interface GcmSearchResult {
  ok: boolean;
  gameName: string;
  requestId?: string;
  downloaded?: boolean;
  downloadOutcome?: 'downloaded' | 'reused-local';
  trainerFolder?: string;
  trainerPath?: string;
  trainerGameName?: string;
  gcmResourceId?: string;
  origin?: string;
  sha256?: string;
  identity?: GameTrainerIdentity;
  state: TrainerPreparationState;
  message?: string;
  error?: string;
  retryAfter?: number;
  code?: string;
  retryable?: boolean;
  catalog?: { updated?: boolean };
  bytesDownloaded?: number;
}
export class GameTrainerCancelledError extends Error {
  constructor() { super('游戏修改器准备已取消'); this.name = 'GameTrainerCancelledError'; }
}
export interface GameTrainerProcessTarget { pid: number; processCreated: string }
export interface GameTrainerTaskOptions {
  processTarget?: GameTrainerProcessTarget;
  isCancelled?: () => boolean;
  isTargetCurrent?: () => Promise<boolean>;
  interactiveShell?: boolean;
}
interface PrepareOptions {
  automatic?: boolean;
  processTarget?: GameTrainerProcessTarget;
  processPath?: string;
  onProgress?: (progress: GcmSearchResult) => void;
}
interface PreparationJob {
  key: string;
  sourceGeneration: number;
  mode: 'Prepare' | 'RefreshData';
  title: string;
  processPath: string;
  processTarget?: GameTrainerProcessTarget;
  manual: boolean;
  cancelled: boolean;
  resultPath?: string;
  listeners: Set<(state: GcmSearchResult) => void>;
  promise: Promise<GcmSearchResult>;
}
const jobs = new Map<string, PreparationJob>();
const states = new Map<string, GcmSearchResult>();
const listeners = new Set<() => void>();
let workerQueue: Promise<unknown> = Promise.resolve();
let sourceGeneration = getSettingsGeneration();
let pathsPromise: Promise<{ cacheRoot: string; script: string }> | null = null;
const EMULATORS = new Set(['cemu.exe', 'yuzu.exe', 'ryujinx.exe', 'citra.exe', 'citra-qt.exe', 'desmume.exe', 'mgba.exe', 'duckstation.exe', 'duckstation-qt.exe', 'pcsx2.exe', 'pcsx2-qt.exe', 'ppssppwindows.exe', 'ppsspp.exe', 'rpcs3.exe']);
const terminal = new Set<TrainerPreparationState>(['ready', 'unavailable', 'failed', 'cancelled', 'budget-limited']);

export function trainerGameKey(name: string): string {
  // Equality only: Victoria 3 must never match Victoria 2 or a title prefix.
  return cleanGameTitle(name.replace(/&(?:amp|#0*38|#x0*26);/gi, '&')).normalize('NFKC').replace(/&/g, ' and ').replace(/\s+trainer\s*$/i, '').replace(/修改器\s*$/, '').toLowerCase().replace(/[^\p{L}\p{N}]/gu, '');
}
export function trainerTaskKey(name: string, processPath = ''): string {
  return trainerGameKey(name) + '|' + processPath.replace(/\//g, '\\').replace(/\\+$/, '').toLowerCase();
}
export function canAutoPrepareTrainer(name: string, processPath: string): boolean {
  const executable = processPath.split(/[\\/]/).pop()?.toLowerCase() || '';
  return !!trainerGameKey(name) && !!processPath && !EMULATORS.has(executable)
    && !/[\\/](?:GCM Trainers|trainer-cache|Game Cheats Manager)[\\/]/i.test(processPath)
    && !/trainer.*\.exe$/i.test(executable);
}
function notifyTrainerState(): void {
  for (const cb of [...listeners]) { try { cb(); } catch { /* An observer cannot interrupt a worker. */ } }
}
function syncTrainerSource(): number {
  const generation = getSettingsGeneration();
  if (generation !== sourceGeneration) {
    sourceGeneration = generation;
    pathsPromise = null;
    states.clear();
    for (const job of jobs.values()) {
      job.cancelled = true;
      void writeCancellation(job).catch(() => {});
    }
    jobs.clear();
    launches.clear();
    readyLaunches.clear();
    notifyTrainerState();
  }
  return generation;
}
function assertTrainerSource(generation: number): void {
  if (syncTrainerSource() !== generation) throw new GameTrainerCancelledError();
}
function copyResult(state: GcmSearchResult): GcmSearchResult {
  return { ...state, identity: state.identity ? { ...state.identity, aliases: state.identity.aliases?.slice() } : undefined };
}
async function trainerPaths(): Promise<{ cacheRoot: string; script: string }> {
  const generation = syncTrainerSource();
  if (!pathsPromise) {
    const pending = Promise.all([app.powerControlDir(), app.dataDir()]).then(([powerControl, data]) => {
      assertTrainerSource(generation);
      if (!powerControl || !data) throw new Error('无法确定修改器工作目录');
      return { cacheRoot: `${data.replace(/[\\/]+$/, '')}\\trainer-cache`, script: `${powerControl.replace(/[\\/]+$/, '')}\\YeManTrainerPrepare.ps1` };
    }).catch((error) => { if (pathsPromise === pending) pathsPromise = null; throw error; });
    pathsPromise = pending;
  }
  return pathsPromise;
}
function publish(state: GcmSearchResult, job: Pick<PreparationJob, 'key' | 'mode' | 'sourceGeneration' | 'listeners'>): void {
  if (job.sourceGeneration !== syncTrainerSource() || job.mode === 'RefreshData') return;
  const key = job.key;
  // Superseded workers and delayed cancellation writes cannot replace a newer instance.
  if ('promise' in job && jobs.get(key) !== job) return;
  if (state.state === 'failed' && states.get(key)?.state !== 'failed') {
    reportFrontendError('game-trainer.prepare', new Error(JSON.stringify({ gameName: state.gameName, code: state.code, retryable: state.retryable, message: state.error || state.message, requestId: state.requestId, processPath: 'processPath' in job ? job.processPath : undefined })));
  }
  states.set(key, copyResult(state));
  if (states.size > 128) {
    const oldest = [...states.keys()].find((entry) => !jobs.has(entry) && entry !== key);
    if (oldest) states.delete(oldest);
  }
  notifyTrainerState();
  for (const cb of job.listeners) { try { cb(copyResult(state)); } catch { /* Isolate UI observers. */ } }
}
export function getGameTrainerState(gameName: string, processPath?: string): GcmSearchResult | null {
  syncTrainerSource();
  const candidates = processPath === undefined ? [...states.entries()].filter(([key]) => key.startsWith(trainerGameKey(gameName) + '|')) : [];
  const state = processPath === undefined ? (candidates.length === 1 ? candidates[0][1] : undefined) : states.get(trainerTaskKey(gameName, processPath));
  return state ? copyResult(state) : null;
}
export function subscribeGameTrainerState(cb: () => void): () => void {
  listeners.add(cb);
  return () => { listeners.delete(cb); };
}
function resultTrainer(result: GcmSearchResult): InstalledGameTrainer | null {
  if (!result.ok || result.state !== 'ready' || typeof result.trainerPath !== 'string' || !result.trainerPath
    || typeof result.trainerFolder !== 'string' || !result.trainerFolder || typeof result.trainerGameName !== 'string' || !result.trainerGameName) return null;
  const path = result.trainerPath.replace(/\//g, '\\');
  const folder = result.trainerFolder.replace(/\//g, '\\').replace(/\\+$/, '');
  if (!/^(?:[a-z]:\\|\\\\)/i.test(path) || !/\.(exe|ct|cetrainer)$/i.test(path)
    || !path.toLowerCase().startsWith(`${folder.toLowerCase()}\\`) || /(?:^|\\)\.\.(?:\\|$)/.test(path)) return null;
  return { gameName: result.trainerGameName, origin: result.origin || 'local', gcmResourceId: result.gcmResourceId, path, folder, modified: Date.now(), sha256: result.sha256, identity: copyResult(result).identity };
}
function isLiveGameIdentity(identity: GameTrainerIdentity | undefined): boolean {
  return identity?.method === 'ymcc-game-target' || identity?.method === 'native-window-title';
}
function matchesRunningGame(identity: GameTrainerIdentity | undefined, processPath: string, target: GameTrainerProcessTarget): boolean {
  return !!identity && Number.isSafeInteger(identity.processId) && identity.processId! > 0
    && identity.processId === target.pid && identity.processCreated === target.processCreated
    && typeof identity.processCreated === 'string' && /^[1-9][0-9]*$/.test(identity.processCreated)
    && typeof identity.executablePath === 'string'
    && trainerTaskKey('', identity.executablePath) === trainerTaskKey('', processPath)
    && /^[a-f0-9]{64}$/i.test(identity.executableSha256 || '');
}
function hasConfirmedIdentity(identity: GameTrainerIdentity | undefined, gameName: string, processPath: string): boolean {
  if (!identity || !/^[1-9][0-9]{0,9}$/.test(identity.steamAppId || '') || Number(identity.steamAppId) > 2147483647) return false;
  const absolute = (value: string | undefined) => typeof value === 'string' && /^(?:[a-z]:[\\/]|\\\\)/i.test(value);
  if (identity.method === 'steam-appid' || identity.method === 'steam-manifest') return absolute(identity.manifestPath);
  if (identity.method === 'custom-library-appid' || identity.method === 'custom-library-install-metadata') return absolute(identity.librarySourcePath);
  if (isLiveGameIdentity(identity)) return absolute(identity.executablePath)
    && trainerTaskKey('', identity.executablePath) === trainerTaskKey('', processPath)
    && Number.isSafeInteger(identity.processId) && identity.processId! > 0 && typeof identity.processCreated === 'string' && /^[1-9][0-9]*$/.test(identity.processCreated)
    && /^[a-f0-9]{64}$/i.test(identity.executableSha256 || '')
    && (identity.method === 'ymcc-game-target' ? absolute(identity.snapshotPath) && trainerGameKey(identity.gameName || '') === trainerGameKey(gameName) : trainerGameKey(identity.windowTitle || '') === trainerGameKey(gameName));
  // exact-title can originate from a process stem. Legacy exe-binding receipts
  // do not say whether their original binding was verified or merely name-based.
  return false;
}
// Read-only, exact game+EXE lookup. No disk scan, preparation, catalog request or download.
export function getReadyGameTrainer(gameName: string, processPath: string, processTarget?: GameTrainerProcessTarget): InstalledGameTrainer | null {
  if (!trainerGameKey(gameName) || !/^(?:[a-z]:[\\/]|\\\\)/i.test(processPath)) return null;
  const state = getGameTrainerState(gameName, processPath);
  if (processTarget && !matchesRunningGame(state?.identity, processPath, processTarget)) return null;
  return state && hasConfirmedIdentity(state.identity, gameName, processPath) && /^[a-f0-9]{64}$/i.test(state.sha256 || '') ? resultTrainer(state) : null;
}
export async function findInstalledGameTrainer(gameName: string): Promise<InstalledGameTrainer | null> {
  const { cacheRoot } = await trainerPaths();
  const indexPath = `${cacheRoot}\\trainers.json`;
  if (!(await fs.exists(indexPath))) return null;
  let entry: GcmSearchResult | undefined;
  try { entry = JSON.parse(await fs.readTextFile(indexPath, 8 << 20))[trainerGameKey(gameName)]; }
  catch { return null; } // The worker can rebuild a corrupt index from local files.
  if (!entry) return null;
  const trainer = resultTrainer({ ...entry, ok: entry.state === 'ready' });
  return trainer && await fs.exists(trainer.path) ? trainer : null;
}
// Disk preparation receipts can outlive a renderer or a sleep-interrupted wait.
// Rejoin only the exact still-selected instance; never run a worker or download here.
const localRestores = new Map<string, Promise<InstalledGameTrainer | null>>();
export function restoreReadyGameTrainer(gameName: string, processPath: string, options: {
  processTarget: GameTrainerProcessTarget; isTargetCurrent: () => boolean;
}): Promise<InstalledGameTrainer | null> {
  const generation = syncTrainerSource(), title = cleanGameTitle(gameName).trim();
  const target = { ...options.processTarget }, key = trainerTaskKey(title, processPath);
  if (!trainerGameKey(title) || title.length > 240 || !/^(?:[a-z]:[\\/]|\\\\)/i.test(processPath)
    || !Number.isSafeInteger(target.pid) || target.pid <= 0 || !/^[1-9][0-9]*$/.test(target.processCreated)) return Promise.resolve(null);
  const requestKey = JSON.stringify([generation, key, target.pid, target.processCreated]);
  const pending = localRestores.get(requestKey);
  if (pending) return pending.then(value => options.isTargetCurrent() ? value : null);
  const before = states.get(key);
  const current = () => generation === getSettingsGeneration() && options.isTargetCurrent()
    && !jobs.has(key) && states.get(key) === before;
  const operation = (async (): Promise<InstalledGameTrainer | null> => {
    if (!current()) return null;
    const ready = getReadyGameTrainer(title, processPath, target);
    if (ready) return ready;
    const { cacheRoot } = await trainerPaths();
    if (!current()) return null;
    const index = JSON.parse(await fs.readTextFile(`${cacheRoot}\\trainers.json`, 8 << 20));
    if (!index || typeof index !== 'object' || Array.isArray(index) || Object.keys(index).length > 4096) return null;
    const candidates = new Map<string, GcmSearchResult>();
    for (const [entryKey, entry] of Object.entries(index)) {
      // Backend keys are a normalized title, optionally followed by an EXE hash.
      if (entryKey.split('|')[0] !== trainerGameKey(title) || !entry || typeof entry !== 'object') continue;
      try {
        const cached = entry as GcmSearchResult;
        if (cached.state !== 'ready' || !matchesRunningGame(cached.identity, processPath, target)
          || !hasConfirmedIdentity(cached.identity, title, processPath) || !/^[a-f0-9]{64}$/i.test(cached.sha256 || '')) continue;
        const trainer = resultTrainer({ ...cached, ok: true });
        if (!trainer) continue;
        candidates.set(JSON.stringify([trainer.path.toLowerCase(), trainer.folder.toLowerCase(), trainer.sha256, trainer.gcmResourceId || '']), { ...cached, ok: true });
      } catch { /* One corrupt alias cannot hide a separate valid receipt. */ }
    }
    if (candidates.size !== 1 || !current()) return null;
    const cached = [...candidates.values()][0], trainer = resultTrainer(cached)!;
    if (!(await fs.exists(trainer.path)) || !current()) return null;
    if ((await fs.sha256File(trainer.path)).toLowerCase() !== trainer.sha256!.toLowerCase() || !current()) return null;
    if ((await fs.sha256File(processPath)).toLowerCase() !== cached.identity!.executableSha256!.toLowerCase() || !current()) return null;
    publish({ ...cached, gameName: title, downloaded: false, downloadOutcome: 'reused-local', message: '已复用 GCM 中的修改器，点击即可启动' },
      { key, mode: 'Prepare', sourceGeneration: generation, listeners: new Set() });
    return getReadyGameTrainer(title, processPath, target);
  })().catch(() => null);
  localRestores.set(requestKey, operation);
  void operation.finally(() => { if (localRestores.get(requestKey) === operation) localRestores.delete(requestKey); });
  return operation;
}
async function writeCancellation(job: PreparationJob): Promise<void> {
  if (job.resultPath) await fs.writeTextFileAtomic(`${job.resultPath}.cancel`, 'cancel');
}
async function cancelJob(job: PreparationJob): Promise<void> {
  job.cancelled = true;
  await writeCancellation(job);
  publish({ ok: false, gameName: job.title, state: 'cancelled', message: '已取消修改器准备' }, job);
}
export async function cancelGameTrainerPreparation(gameName: string, processPath?: string): Promise<void> {
  const selected = [...jobs.values()].filter(job => job.mode === 'Prepare' && (processPath === undefined ? trainerGameKey(job.title) === trainerGameKey(gameName) : job.key === trainerTaskKey(gameName, processPath)));
  await Promise.all(selected.map(cancelJob));
}
export function cancelAutomaticTrainerPreparations(): void {
  for (const job of jobs.values()) {
    if (!job.manual) void cancelJob(job).catch(() => {});
  }
}
function checkCancelled(job: PreparationJob): void {
  assertTrainerSource(job.sourceGeneration);
  if (job.cancelled) throw new GameTrainerCancelledError();
}
async function runPreparation(job: PreparationJob): Promise<GcmSearchResult> {
  checkCancelled(job);
  const { cacheRoot, script } = await trainerPaths();
  if (!(await fs.exists(script))) throw new Error('缺少后台修改器组件，请更新完整安装包');
  await fs.mkdir(`${cacheRoot}\\requests`);
  checkCancelled(job);
  const requestId = `${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}-${Math.random().toString(36).slice(2)}`;
  job.resultPath = `${cacheRoot}\\requests\\${requestId}.json`;
  // A durable pending record must precede process creation; no stale request is reusable.
  const args = ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', script, '-GameName', job.title, '-CacheRoot', cacheRoot, '-ResultPath', job.resultPath, '-RequestId', requestId];
  if (job.mode === 'RefreshData') args.push('-Mode', 'RefreshData');
  if (job.processPath) args.push('-GameExecutablePath', job.processPath);
  if (job.processTarget) args.push('-GameProcessId', String(job.processTarget.pid), '-GameProcessCreated', job.processTarget.processCreated);
  if (job.manual) args.push('-Force');
  let completed = false;
  let started = false;
  try {
    await fs.writeTextFileAtomic(job.resultPath, JSON.stringify({ requestId, gameName: job.title, ok: false, state: 'queued' }));
    checkCancelled(job);
    const worker = await shell.hidden('powershell.exe', args);
    if (!worker.ok) throw new Error('无法启动后台修改器组件');
    started = true;
    if (job.cancelled) await writeCancellation(job);
    const deadline = Date.now() + (job.mode === 'RefreshData' ? 60000 : 200000);
    let lastProgress = '';
    while (Date.now() < deadline) {
      checkCancelled(job);
      let parsed: GcmSearchResult | null = null;
      try { parsed = JSON.parse(await fs.readTextFile(job.resultPath, 65536)); } catch { /* Poll until the worker has a complete result. */ }
      if (parsed?.requestId === requestId && parsed.gameName === job.title) {
        if (job.mode === 'Prepare' && parsed.state === 'ready' && (!resultTrainer(parsed) || !(await fs.exists(parsed.trainerPath!)))) throw new Error('修改器文件未通过安装核验');
        if (parsed.state === 'ready' && job.processTarget && !matchesRunningGame(parsed.identity, job.processPath, job.processTarget)) throw new Error('修改器回执对应的顶部游戏进程已改变');
        checkCancelled(job);
        const progress = JSON.stringify(parsed);
        if (progress !== lastProgress) { publish(parsed, job); lastProgress = progress; }
        if (terminal.has(parsed.state)) {
          completed = true;
          if (parsed.state === 'cancelled') throw new GameTrainerCancelledError();
          return parsed;
        }
      }
      await new Promise((resolve) => setTimeout(resolve, 400));
    }
    job.cancelled = true;
    await writeCancellation(job);
    throw new Error('后台准备修改器超时，请稍后重试');
  } finally {
    if (!started || completed) {
      await fs.remove(job.resultPath).catch(() => {});
      await fs.remove(`${job.resultPath}.cancel`).catch(() => {});
    }
  }
}
export function prepareGameTrainer(gameName: string, options: PrepareOptions = {}): Promise<GcmSearchResult> {
  const generation = syncTrainerSource();
  if (options.processTarget && (!Number.isSafeInteger(options.processTarget.pid) || options.processTarget.pid <= 0 || !/^[1-9][0-9]*$/.test(options.processTarget.processCreated))) return Promise.reject(new Error('顶部游戏进程身份无效'));
  const title = cleanGameTitle(gameName).trim();
  const key = trainerTaskKey(title, options.processPath);
  if (!trainerGameKey(title) || title.length > 240) return Promise.reject(new Error('未识别到有效游戏名'));
  const existing = jobs.get(key);
  const sameTarget = !options.processTarget || (existing?.processTarget?.pid === options.processTarget.pid && existing.processTarget.processCreated === options.processTarget.processCreated);
  if (existing && !existing.cancelled && sameTarget) {
    if (!options.automatic) existing.manual = true;
    if (options.onProgress) { existing.listeners.add(options.onProgress); const current = states.get(key); if (current) options.onProgress({ ...current }); }
    return existing.promise;
  }
  if (existing) void cancelJob(existing).catch(() => {});
  const catalog = jobs.get('@catalog');
  if (catalog && !options.automatic) void cancelJob(catalog).catch(() => {});
  const job: PreparationJob = { key, sourceGeneration: generation, mode: 'Prepare', title, processPath: options.processPath || '', processTarget: options.processTarget ? { ...options.processTarget } : undefined, manual: !options.automatic, cancelled: false, listeners: new Set(), promise: Promise.resolve(null as never) };
  if (options.onProgress) job.listeners.add(options.onProgress);
  jobs.set(key, job);
  job.promise = workerQueue.then(() => runPreparation(job)).catch((error) => {
    publish({ ok: false, gameName: title, state: error instanceof GameTrainerCancelledError ? 'cancelled' : 'failed', error: String(error.message || error), message: String(error.message || error) }, job);
    throw error;
  }).finally(() => { if (jobs.get(key) === job) jobs.delete(key); });
  workerQueue = job.promise.catch(() => {});
  publish({ ok: false, gameName: title, state: 'queued', message: '等待后台准备修改器' }, job);
  return job.promise;
}
export function refreshGameTrainerData(): Promise<GcmSearchResult> {
  const generation = syncTrainerSource();
  const existing = jobs.get('@catalog');
  if (existing) return existing.promise;
  if (jobs.size) return Promise.resolve({ ok: false, gameName: 'YMCC Trainer Catalog', state: 'unavailable', retryAfter: Date.now() + 60000 });
  const job: PreparationJob = { key: '@catalog', sourceGeneration: generation, mode: 'RefreshData', title: 'YMCC Trainer Catalog', processPath: '', manual: false, cancelled: false, listeners: new Set(), promise: Promise.resolve(null as never) };
  jobs.set(job.key, job);
  job.promise = workerQueue.then(() => runPreparation(job)).finally(() => { if (jobs.get(job.key) === job) jobs.delete(job.key); });
  workerQueue = job.promise.catch(() => {});
  return job.promise;
}
const launches = new Map<string, Promise<{ action: 'opened' | 'searched'; trainer?: InstalledGameTrainer; search?: GcmSearchResult }>>();
export function openOrSearchGameTrainer(gameName: string, onProgress?: (progress: GcmSearchResult) => void, processPath = '', options: GameTrainerTaskOptions = {}) {
  const generation = syncTrainerSource();
  const key = trainerTaskKey(gameName, processPath);
  const activeLaunch = launches.get(key);
  if (activeLaunch) return activeLaunch;
  const action = (async (): Promise<{ action: 'opened' | 'searched'; trainer?: InstalledGameTrainer; search?: GcmSearchResult }> => {
    const check = async () => {
      assertTrainerSource(generation);
      if (options.isCancelled?.()) throw new GameTrainerCancelledError();
      if (options.isTargetCurrent && !(await options.isTargetCurrent())) throw new Error('目标游戏已退出或更换，修改器已保留，下次可直接启动');
      assertTrainerSource(generation);
      if (options.isCancelled?.()) throw new GameTrainerCancelledError();
    };
    await check();
    if (EMULATORS.has(processPath.split(/[\\/]/).pop()?.toLowerCase() || '')) {
      await check();
      await launchTrainerWorker('CheatEngine', undefined, processPath, check, options.processTarget);
      return { action: 'opened' };
    }
    const search = await prepareGameTrainer(gameName, { onProgress, processPath, processTarget: options.processTarget });
    await check();
    const trainer = resultTrainer(search);
    if (!trainer) throw new Error(search.message || search.error || '暂未找到可启动的修改器');
    if (!(await fs.exists(trainer.path))) throw new Error('修改器文件已被移除，请重新准备');
    if (trainer.sha256 && (await fs.sha256File(trainer.path)).toLowerCase() !== trainer.sha256.toLowerCase()) throw new Error('修改器文件已改变，请在修改器管理器中确认后重新准备');
    await check();
    await launchTrainerWorker('Trainer', trainer, processPath, check, options.processTarget);
    return { action: 'opened', trainer, search };
  })();
  launches.set(key, action);
  void action.finally(() => { if (launches.get(key) === action) launches.delete(key); }).catch(() => {});
  return action;
}
const readyLaunches = new Map<string, Promise<{ action: 'opened'; trainer: InstalledGameTrainer; notice?: string }>>();
// Consumers must revalidate their locked PID/creation-time/path target, not just a title.
export function launchReadyGameTrainer(gameName: string, processPath: string, options: GameTrainerTaskOptions & { isTargetCurrent: () => Promise<boolean> }): Promise<{ action: 'opened'; trainer: InstalledGameTrainer; notice?: string }> {
  const generation = syncTrainerSource();
  const key = trainerTaskKey(gameName, processPath);
  const existing = readyLaunches.get(key);
  if (existing) return existing;
  if (launches.has(key)) return Promise.reject(new Error('修改器操作正在进行，请稍候'));
  const action = (async (): Promise<{ action: 'opened'; trainer: InstalledGameTrainer; notice?: string }> => {
    const trainer = getReadyGameTrainer(gameName, processPath, options.processTarget);
    if (!trainer) throw new Error('未找到已就绪的修改器');
    const check = async () => {
      assertTrainerSource(generation);
      if (options.isCancelled?.()) throw new GameTrainerCancelledError();
      if (typeof options.isTargetCurrent !== 'function' || !(await options.isTargetCurrent())) throw new Error('目标游戏已退出或更换，未启动修改器');
      if (options.isCancelled?.()) throw new GameTrainerCancelledError();
      const current = getReadyGameTrainer(gameName, processPath, options.processTarget);
      if (!current || current.path !== trainer.path || current.folder !== trainer.folder || current.sha256 !== trainer.sha256) throw new Error('修改器就绪状态已改变，请重新确认');
    };
    await check();
    const checkedState = states.get(key);
    const invalidate = (error: unknown) => {
      // A late failed check must never clear a newer source or preparation result.
      if (syncTrainerSource() === generation && states.get(key) === checkedState) {
        const message = error instanceof Error ? error.message : String(error);
        publish({ ok: false, gameName, state: 'failed', message, error: message }, { key, mode: 'Prepare', sourceGeneration: generation, listeners: new Set() });
      }
    };
    const verifyFiles = async () => {
      if (!(await fs.exists(processPath))) throw new Error('游戏 EXE 已不存在，未启动修改器');
      if (trainer.identity?.executableSha256 && (await fs.sha256File(processPath)).toLowerCase() !== trainer.identity.executableSha256.toLowerCase()) throw new Error('游戏 EXE 已改变，请重新识别当前游戏');
      // The worker resolves the current GCM entry and verifies its hash. A moved cache path is only a hint.
    };
    try { await verifyFiles(); } catch (error) { invalidate(error); throw error; }
    let notice: string | undefined;
    try {
      const resolved = await launchTrainerWorker('Trainer', trainer, processPath, check, options.processTarget, options.interactiveShell);
      notice = resolved.notice;
      if (resolved?.trainerPath && resolved.trainerFolder && syncTrainerSource() === generation && states.get(key) === checkedState) {
        const updated = { ...checkedState!, trainerPath: resolved.trainerPath, trainerFolder: resolved.trainerFolder };
        const current = resultTrainer(updated);
        if (current) { Object.assign(trainer, current); publish(updated, { key, mode: 'Prepare', sourceGeneration: generation, listeners: new Set() }); }
      }
    }
    catch (error) {
      // The backend may catch a file change occurring after the frontend check.
      // A denied launch with intact files must not discard a valid ready entry.
      if (syncTrainerSource() === generation && states.get(key) === checkedState) {
        if (error instanceof GameTrainerLaunchError && ['trainer-unavailable', 'trainer-changed', 'trainer-ambiguous'].includes(error.code)) invalidate(error);
        else { try { await verifyFiles(); } catch (fileError) { invalidate(fileError); } }
      }
      throw error;
    }
    return { action: 'opened', trainer, notice };
  })();
  readyLaunches.set(key, action);
  launches.set(key, action);
  void action.finally(() => {
    if (readyLaunches.get(key) === action) readyLaunches.delete(key);
    if (launches.get(key) === action) launches.delete(key);
  }).catch(() => {});
  return action;
}
interface TrainerLaunchReceipt { notice?: string; launchBackend?: string; window?: string; requestId?: string; pending?: boolean; ok?: boolean; error?: string; code?: string; program?: string; arguments?: string; trainerPath?: string; trainerFolder?: string }
class GameTrainerLaunchError extends Error { constructor(message: string, readonly code = '') { super(message); } }
async function launchTrainerWorker(mode: 'Trainer' | 'Manager' | 'CheatEngine', trainer?: InstalledGameTrainer, processPath = '', beforeLaunch?: () => Promise<void>, processTarget?: GameTrainerProcessTarget, interactiveShell = false): Promise<TrainerLaunchReceipt> {
  const generation = syncTrainerSource();
  const { cacheRoot, script: prepareScript } = await trainerPaths();
  const script = prepareScript.replace(/YeManTrainerPrepare\.ps1$/, 'YeManTrainerLaunch.ps1');
  if (!(await fs.exists(script))) throw new Error('缺少修改器启动组件，请更新完整安装包');
  await fs.mkdir(cacheRoot + '\\requests');
  const requestId = 'launch-' + Date.now().toString(36) + '-' + Math.random().toString(36).slice(2);
  const resultPath = cacheRoot + '\\requests\\' + requestId + '.json';
  await fs.writeTextFileAtomic(resultPath, JSON.stringify({ requestId, ok: false, pending: true }));
  const args = ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', script, '-Mode', mode, '-CacheRoot', cacheRoot, '-ResultPath', resultPath, '-RequestId', requestId];
  if (trainer) args.push('-TrainerPath', trainer.path, '-TrainerFolder', trainer.folder, '-ExpectedSha256', trainer.sha256 || '', '-TrainerGameName', trainer.gameName, '-TrainerOrigin', trainer.origin);
  if (trainer?.gcmResourceId) args.push('-GcmResourceId', trainer.gcmResourceId);
  args.push('-AppDirectory', await app.exeDir());
  if (processPath) args.push('-GameExecutablePath', processPath);
  const identity = trainer?.identity;
  const boundIdentity = identity && matchesRunningGame(identity, processPath, { pid: identity.processId || 0, processCreated: identity.processCreated || '' }) ? identity : undefined;
  const target = processTarget || (boundIdentity ? { pid: boundIdentity.processId!, processCreated: boundIdentity.processCreated! } : undefined);
  if (target) args.push('-GameProcessId', String(target.pid), '-GameProcessCreated', target.processCreated);
  if (boundIdentity?.executableSha256) args.push('-ExpectedGameSha256', boundIdentity.executableSha256);
  if (interactiveShell) args.push('-InteractiveShell');
  let complete = false;
  let started = false;
  try {
    await beforeLaunch?.();
    assertTrainerSource(generation);
    if (!(await shell.hidden('powershell.exe', args)).ok) throw new Error('无法启动修改器启动组件');
    started = true;
    const deadline = Date.now() + 90000;
    while (Date.now() < deadline) {
      let result: TrainerLaunchReceipt | null = null;
      try { result = JSON.parse(await fs.readTextFile(resultPath, 65536)); } catch { /* Wait for atomic receipt. */ }
      if (result?.requestId === requestId && !result.pending) {
        complete = true;
        if (!result.ok) {
          reportFrontendError('game-trainer.launch', new Error(JSON.stringify({ mode, requestedPath: trainer?.path, ...result })));
          throw new GameTrainerLaunchError(result.error || '修改器启动失败', result.code);
        }
        return result;
      }
      await new Promise(resolve => setTimeout(resolve, 250));
    }
    throw new Error('未收到启动确认，请检查是否有待确认的系统权限窗口');
  } finally { if (complete || !started) await fs.remove(resultPath).catch(() => {}); }
}
let cheatEngineLaunch: Promise<void> | null = null;
export function openCheatEngine(): Promise<void> {
  if (cheatEngineLaunch) return cheatEngineLaunch;
  const action = launchTrainerWorker('CheatEngine').then(() => {});
  cheatEngineLaunch = action;
  void action.finally(() => { if (cheatEngineLaunch === action) cheatEngineLaunch = null; }).catch(() => {});
  return action;
}
export async function openGameTrainerManager(): Promise<void> {
  await launchTrainerWorker('Manager');
}
