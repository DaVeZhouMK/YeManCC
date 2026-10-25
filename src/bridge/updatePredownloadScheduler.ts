import type { UpdatePredownloadProvider, UpdatePredownloadSnapshot } from './updatePredownload';
import type { UpdateInfo, UpdateProgressState } from './yeman';
export interface PredownloadPolicy {
  enabled: boolean;
  bootId: string;
  lastNotifiedBoot: string;
  failureCount: number;
  nextCheckAt: number;
  cacheQuietUntil?: number;
  targetVersion?: string;
  targetSha256?: string;
}
export interface PredownloadDependencies {
  now(): number;
  timer(fn: () => void, ms: number): unknown;
  clearTimer(timer: unknown): void;
  online(): boolean;
  canRun?(): boolean;
  readPolicy(): Promise<PredownloadPolicy>;
  setEnabled(enabled: boolean): Promise<PredownloadPolicy>;
  saveSchedule(failureCount: number, nextCheckAt: number): Promise<unknown>;
  initialize(): Promise<void>;
  refresh(): Promise<void>;
  current(): { info: UpdateInfo | null; progress: UpdateProgressState };
  check(): Promise<void>;
  prepare(cacheOnly: boolean): Promise<boolean>;
  cancel(): Promise<void>;
  notify(version: string): Promise<boolean>;
  notified(policy: PredownloadPolicy, info: UpdateInfo): Promise<boolean>;
}
const CHECK_INTERVAL = 6 * 60 * 60 * 1000;
const RETRY_DELAYS = [60000, 5 * 60000, 15 * 60000, 60 * 60000];
const busyPhases = ['checking', 'downloading', 'validating', 'installing'];
const identity = (info: UpdateInfo) => info.version + ':' + info.sha256.toUpperCase();

/** One scheduler for the main renderer, with all IO delegated to the existing
 * manager. No installation API is available in this dependency contract. */
export function createUpdatePredownloadScheduler(deps: PredownloadDependencies): UpdatePredownloadProvider & {
  start(): void; stop(): void; wake(recover?: boolean): void; invalidateCache(): void; progress(): void; run(): Promise<void>;
} {
  let policy: PredownloadPolicy | null = null, loading: Promise<void> | null = null;
  let ended = false, running = false, started = false, timer: unknown = null;
  let verified = '', probed = '', verifiedAt = 0, revision = 0, lastRecovery = -Infinity;
  let quietUntil = 0;
  let noticeAccepted = ''; // An ack write failure must not replay an already accepted popup.
  let state: UpdatePredownloadSnapshot = { enabled: false, phase: 'waiting', message: '正在读取后台更新策略' };
  const listeners = new Set<(state: UpdatePredownloadSnapshot) => void>();
  function publish(patch: Partial<UpdatePredownloadSnapshot>) {
    if (ended) return;
    state = { ...state, ...patch, enabled: policy?.enabled ?? state.enabled };
    for (const listener of listeners) { try { listener({ ...state }); } catch { /* Presentation is not task ownership. */ } }
  }
  function arm(ms = 30000) {
    if (ended || !started) return;
    if (timer !== null) deps.clearTimer(timer);
    timer = deps.timer(() => { timer = null; void run(); }, ms);
  }
  async function load() {
    if (policy) return;
    const ticket = revision;
    if (!loading) loading = (async () => {
      const saved = await deps.readPolicy();
      if (!saved || typeof saved.enabled !== 'boolean' || typeof saved.bootId !== 'string' ||
          typeof saved.lastNotifiedBoot !== 'string' || !Number.isFinite(saved.failureCount) ||
          !Number.isFinite(saved.nextCheckAt)) throw Error('后台更新策略回读无效');
      if (ended || ticket !== revision) return;
      const next = { ...saved, failureCount: Math.max(0, Math.min(32, Math.floor(saved.failureCount))),
        nextCheckAt: Math.max(0, Math.min(deps.now() + CHECK_INTERVAL, Number(saved.nextCheckAt) || 0)) };
      await deps.initialize();
      if (ended || ticket !== revision) return;
      quietUntil = Math.max(quietUntil, Math.min(deps.now() + 600000, Number(saved.cacheQuietUntil) || 0));
      next.nextCheckAt = Math.max(next.nextCheckAt, quietUntil);
      policy = next;
      publish({ phase: saved.enabled ? 'waiting' : 'paused', message: saved.enabled ? '等待后台检查更新' : '后台预下载已关闭' });
    })().finally(() => { loading = null; });
    await loading;
  }
  async function schedule(failed: boolean) {
    if (!policy || ended) return;
    policy.failureCount = failed ? Math.min(32, policy.failureCount + 1) : 0;
    policy.nextCheckAt = Math.max(quietUntil, deps.now() + (failed ? RETRY_DELAYS[Math.min(policy.failureCount - 1, RETRY_DELAYS.length - 1)] : CHECK_INTERVAL));
    await deps.saveSchedule(policy.failureCount, policy.nextCheckAt);
  }
  async function ready(info: UpdateInfo) {
    if (!policy || ended || deps.canRun?.() === false) return;
    publish({ phase: 'ready', version: info.version, percent: 100, message: 'v' + info.version + ' 已下载并校验，可在更新页安装' });
    const boot = policy.bootId, ticket = revision, key = identity(info);
    const stillCurrent = () => !ended && ticket === revision && deps.canRun?.() !== false &&
      !!deps.current().info && identity(deps.current().info!) === key && deps.current().progress.phase === 'downloaded';
    if (!boot || policy.lastNotifiedBoot === boot) return;
    if (noticeAccepted !== boot) {
      if (!await deps.notify(info.version) || !stillCurrent()) return;
      noticeAccepted = boot;
    }
    if (stillCurrent() && await deps.notified(policy, info) && stillCurrent()) policy.lastNotifiedBoot = boot;
  }
  function progress() {
    if (ended) return;
    const { info, progress: current } = deps.current();
    if (current.phase === 'downloading' || current.phase === 'validating') {
      publish({ phase: policy?.enabled && deps.canRun?.() !== false ? 'downloading' : 'paused', version: info?.version || current.version,
        percent: current.percent, message: deps.canRun?.() === false ? '游戏运行或状态待确认，已暂停自动更新并保留下载进度' : policy?.enabled ? current.message || '正在后台下载更新' : '正在停止后台下载，已下载部分会保留' });
    } else if (!running) arm(1000);
  }
  async function run() {
    if (ended || running) return;
    running = true;
    const runRevision = revision;
    try {
      await load();
      if (ended || !policy) return;
      if (deps.now() < quietUntil) { publish({phase:'waiting',version:'',percent:0,message:'更新缓存已清理，稍后再自动检查；也可手动检查更新'}); return; }
      if (deps.canRun?.() === false) { publish({phase:'paused',message:'游戏运行或状态待确认，已暂停自动更新并保留下载进度'}); return; }
      const refreshTicket = revision;
      await deps.refresh();
      if (ended || refreshTicket !== revision) return;
      let current = deps.current();
      if (busyPhases.includes(current.progress.phase || '')) { progress(); return; }
      const runTicket = revision;
      // Recover a completed .part after a crash even without network. Probe an
      // interrupted identity once, so ordinary partials don't bypass backoff.
      if (current.info && probed !== identity(current.info) && ['interrupted', 'failed'].includes(current.progress.phase || '') && current.progress.stage !== 'install') {
        probed = identity(current.info);
        const prepared = await deps.prepare(true);
        if (ended || runTicket !== revision) return;
        if (prepared) { verified = probed; verifiedAt = deps.now(); }
        current = deps.current();
      }
      if (current.info && current.progress.phase === 'downloaded') {
        const key = identity(current.info);
        if (verified !== key || deps.now() - verifiedAt >= CHECK_INTERVAL) {
          const prepared = await deps.prepare(true);
          if (ended || runTicket !== revision) return;
          if (prepared) { verified = key; verifiedAt = deps.now(); }
          else { verified = ''; policy.nextCheckAt = 0; }
          if (ended) return;
        }
        // When a fresh manifest is due, confirm the actual newest version before
        // consuming this boot's one popup for a possibly superseded cached ZIP.
        if (verified === key && !(policy.enabled && deps.online() && deps.now() >= policy.nextCheckAt)) await ready(current.info);
      }
      if (!policy.enabled) {
        if (state.phase !== 'ready') publish({ phase: 'paused', message: '后台预下载已关闭，下载缓存已保留' });
        return;
      }
      if (deps.canRun?.() === false) { publish({phase:'paused',message:'游戏运行或状态待确认，已暂停自动更新并保留下载进度'}); return; }
      if (!deps.online()) {
        if (state.phase !== 'ready') publish({ phase: 'waiting', message: '等待网络恢复，已有下载进度会保留' });
        return;
      }
      if (deps.now() < policy.nextCheckAt) return;
      // An interrupted installation belongs to the explicit install action.
      if (current.info && current.progress.stage === 'install' && ['installing', 'interrupted', 'failed'].includes(current.progress.phase || '') && !verified) {
        publish({ phase: 'paused', message: '上次安装未完成，请在更新页重试安装' }); return;
      }
      const ticket = revision;
      publish({ phase: 'waiting', message: '正在后台检查新版本' });
      await deps.check();
      if (ended || !policy.enabled || ticket !== revision || deps.canRun?.() === false) return;
      current = deps.current();
      if (current.progress.phase === 'failed') throw Error(current.progress.error || '后台检查更新失败');
      if (!current.info || current.progress.phase === 'latest') {
        publish({ phase: 'idle', version: '', percent: 0, message: '当前已是最新版本，后台定期检查' });
        await schedule(false); return;
      }
      if (busyPhases.includes(current.progress.phase || '')) return;
      if (current.progress.phase !== 'downloaded' || verified !== identity(current.info)) {
        publish({ phase: 'downloading', version: current.info.version, percent: 0, message: '正在后台下载新版本' });
        const prepared = await deps.prepare(false);
        if (ended || ticket !== revision) return;
        if (!prepared) {
          if(deps.current().progress.backgroundPaused){policy.nextCheckAt=0;publish({phase:'paused',message:'后台下载已暂停，空闲后自动继续'});return;}
          throw Error(deps.current().progress.error || '后台下载未完成');
        }
        current = deps.current();
        if (!current.info || current.progress.phase !== 'downloaded') return;
        verified = identity(current.info); verifiedAt = deps.now();
      }
      if (ended || !policy.enabled || ticket !== revision || deps.canRun?.() === false) return;
      await schedule(false);
      await ready(current.info);
    } catch (error) {
      if (!ended && runRevision === revision) {
        if (deps.canRun?.() === false) { publish({phase:'paused',message:'游戏运行或状态待确认，已暂停自动更新并保留下载进度'}); return; }
        const message = error instanceof Error ? error.message : String(error);
        publish({ phase: policy?.enabled === false ? 'paused' : 'failed', message: policy?.enabled === false ? '后台预下载已关闭，下载缓存已保留' : '稍后自动重试：' + message });
        try { if (policy?.enabled) await schedule(true); } catch { /* Keep the in-memory backoff even if the disk is full. */ }
      }
    } finally { running = false; if (!ended) arm(); }
  }
  return {
    async read() { await load(); return { ...state }; },
    async setEnabled(enabled) {
      await load();
      const saved = await deps.setEnabled(enabled);
      if (saved.enabled !== enabled) throw Error('后台更新开关保存回读不一致');
      revision++;
      policy = saved;
      publish({ phase: enabled ? 'waiting' : 'paused', message: enabled ? '已启用后台预下载' : '后台预下载已关闭，下载缓存已保留' });
      if (!enabled) await deps.cancel();
      else { policy.nextCheckAt = 0; arm(1000); }
      return { ...state };
    },
    subscribe(listener) { listeners.add(listener); listener({ ...state }); return () => { listeners.delete(listener); }; },
    start() { if (started || ended) return; started = true; arm(15000); },
    stop() { if (ended) return; ended = true; revision++; if (timer !== null) deps.clearTimer(timer); timer = null; listeners.clear(); void deps.cancel().catch(() => {}); },
    wake(recover = false) {
      if (ended) return;
      if (recover && policy && deps.now() - lastRecovery >= 15000) { policy.nextCheckAt = Math.max(quietUntil, Math.min(policy.nextCheckAt, deps.now())); lastRecovery = deps.now(); }
      arm(1000);
    },
    invalidateCache() {
      revision++; quietUntil = deps.now() + 600000; verified = ''; probed = ''; verifiedAt = 0; policy = null;
      publish({ phase: 'waiting', version: '', percent: 0, message: '正在刷新更新缓存状态' });
      arm(30000);
    },
    progress, run,
  };
}

