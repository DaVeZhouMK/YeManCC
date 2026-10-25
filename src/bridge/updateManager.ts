import { reactive, ref } from 'vue';
import {
  checkUpdate,
  compareVersions,
  downloadUpdate,
  isValidSha256,
  installUpdate,
  updateState as readNativeUpdateState,
  updatePackageUrl,
  type UpdateInfo,
  type UpdateProgressState,
  UPDATE_MANIFEST_URL,
} from './yeman';
import { on, invoke, IpcRemoteError } from './ipc';
import { APP_VERSION } from '@/version';

export type UpdatePhase =
  | 'idle'
  | 'checking'
  | 'available'
  | 'latest'
  | 'downloading'
  | 'validating'
  | 'downloaded'
  | 'installing'
  | 'completed'
  | 'failed'
  | 'interrupted';

export interface UpdateSnapshot extends UpdateProgressState {
  phase: UpdatePhase;
  operationId: string;
}

const initial: UpdateSnapshot = {
  phase: 'idle',
  operationId: '',
  repairAuthorized: false, installerActive: false,
  background: false, backgroundPaused: false, downloadActive: false, activeDownloadOperation: '',
  downloadedBytes: 0,
  totalBytes: 0,
  percent: 0,
  speedBps: 0,
  etaSeconds: 0,
  resumedBytes: 0,
  message: '',
  error: '',
};

export const updateSnapshot = reactive<UpdateSnapshot>({ ...initial });
export const updateInfo = ref<UpdateInfo | null>(null);
export const updateCacheState = reactive({ busy: false, message: '', error: '', bytesFreed: 0, revision: 0 });
let maintenanceGeneration = 0;
let retireDownload: (() => Promise<boolean>) | null = null;
let initialized = false;
let initialization: Promise<void> | null = null;
let activeOperation = '';
let downloadTask: Promise<boolean> | null = null;
let installRequested = false;
let backgroundAllowed = false, backgroundGamePaused = false, backgroundGeneration = 0;
let pendingTarget: UpdateInfo | null = null;
let successorProbe: Promise<void> | null = null;
const maintenanceWaiters = new Set<() => void>();
async function waitForSuccessor(): Promise<void> {
  if (!successorProbe) return;
  let release!: () => void;
  const interrupted = new Promise<void>(resolve => { release = resolve; maintenanceWaiters.add(release); });
  try { await Promise.race([successorProbe.catch(() => {}), interrupted]); }
  finally { maintenanceWaiters.delete(release); }
}
let offProgress: (() => void) | null = null;

function makeOperationId(): string {
  try {
    return crypto.randomUUID();
  } catch {
    return `update-${Date.now()}-${Math.random().toString(16).slice(2)}`;
  }
}

function merge(next: UpdateProgressState): void {
  const incomingId = String(next.operationId || '');
  if (incomingId && activeOperation && incomingId !== activeOperation) return;
  if (incomingId) updateSnapshot.operationId = incomingId;
  if (next.phase) updateSnapshot.phase = next.phase as UpdatePhase;
  for (const key of [
    'repairAuthorized', 'installerActive', 'background', 'downloadActive', 'activeDownloadOperation', 'version', 'attempt', 'nextAttempt', 'retryInSeconds', 'remainingRetrySeconds',
    'downloadedBytes', 'totalBytes', 'percent', 'speedBps',
    'etaSeconds', 'resumedBytes', 'stage', 'sha256',
    'lastError', 'message', 'error', 'updatedAt',
  ] as const) {
    if (next[key] !== undefined) updateSnapshot[key] = next[key] as never;
  }
}

function restoreSavedUpdateInfo(saved: UpdateProgressState): void {
  if (!saved.version || !isValidSha256(saved.sha256)) return;
  try {
    const order = compareVersions(saved.version, APP_VERSION);
    if (order < 0 || (order === 0 && !(saved.repairAuthorized === true && saved.stage === 'install'))) return;
  } catch {
    return;
  }
  updateInfo.value = {
    version: saved.version,
    sha256: saved.sha256.toUpperCase(),
  };
}

export async function ensureUpdateManager(): Promise<void> {
  if (!offProgress) {
    offProgress = on<UpdateProgressState>('update.progress', merge);
  }
  if (initialized) return;
  if (initialization) return initialization;
  const generation = maintenanceGeneration;
  const task = (async () => {
    try {
      const saved = await readNativeUpdateState();
      if (generation !== maintenanceGeneration || updateCacheState.busy) return;
      restoreSavedUpdateInfo(saved);
      if (saved.downloadActive && saved.activeDownloadOperation === saved.operationId) {
        activeOperation = saved.operationId || '';
        backgroundAllowed = saved.background === true;
        merge(saved);
      } else if (
        (saved.phase === 'installing' || saved.phase === 'interrupted') &&
        saved.stage === 'install'
      ) {
        if (saved.installerActive === true) {
          activeOperation = saved.operationId || '';
          merge(saved);
        } else merge({ ...saved, phase: 'interrupted', message: '上次安装未完成，可重试安装' });
      } else if (saved.phase === 'checking' || saved.phase === 'downloading' || saved.phase === 'validating' || saved.phase === 'installing') {
        merge({ ...saved, phase: 'interrupted', message: '上次下载未完成，可继续下载' });
      } else {
        merge(saved);
      }
      if (!updateInfo.value && !saved.downloadActive) Object.assign(updateSnapshot, initial);
      initialized = true;
    } catch {
      /* Native state is optional in preview, but a failed load stays retryable. */
    }
  })();
  initialization = task;
  try { await task; }
  finally { if (initialization === task) initialization = null; }
}

export async function checkForUpdate(appVersion: string, background = false): Promise<void> {
  await ensureUpdateManager();
  if (downloadTask || installRequested || updateCacheState.busy) return;
  if (['checking', 'downloading', 'validating', 'installing'].includes(updateSnapshot.phase)) return;
  const priorInfo = updateInfo.value;
  const priorPhase = updateSnapshot.phase;
  const generation = maintenanceGeneration;
  activeOperation = makeOperationId();
  updateSnapshot.operationId = activeOperation;
  updateSnapshot.phase = 'checking';
  updateSnapshot.error = '';
  updateSnapshot.message = '正在连接更新服务器';
  try {
    const info = await checkUpdate(UPDATE_MANIFEST_URL, background);
    if (generation !== maintenanceGeneration || updateCacheState.busy) return;
    if (compareVersions(info.version, appVersion) > 0) {
      updateInfo.value = info;
      updateSnapshot.phase = priorPhase === 'downloaded' && priorInfo?.version === info.version && priorInfo.sha256.toUpperCase() === info.sha256.toUpperCase() ? 'downloaded' : 'available';
      updateSnapshot.stage = updateSnapshot.phase === 'downloaded' ? 'install' : 'download';
      updateSnapshot.sha256 = info.sha256;
      updateSnapshot.version = info.version;
      updateSnapshot.message = '发现新版本';
    } else if (updateSnapshot.repairAuthorized === true && priorInfo &&
        compareVersions(priorInfo.version, appVersion) === 0) {
      updateInfo.value = priorInfo;
      updateSnapshot.phase = 'interrupted'; updateSnapshot.stage = 'install';
      updateSnapshot.message = '上次安装未完成，请重新安装以修复';
    } else {
      updateInfo.value = null;
      updateSnapshot.phase = 'latest';
      updateSnapshot.message = '当前已是最新版本';
    }
  } catch (error) {
    if (generation !== maintenanceGeneration || updateCacheState.busy) return;
    updateSnapshot.phase = 'failed';
    updateSnapshot.error = `检查失败：${error instanceof Error ? error.message : String(error)}`;
    updateSnapshot.message = updateSnapshot.error;
  }
}

/** Download ownership is shared by the automatic scheduler and explicit install.
 * A verified package is an end state here; only downloadAndInstall may install. */
export function prepareUpdate(options: { background?: boolean; cacheOnly?: boolean } = {}): Promise<boolean> {
  if (updateCacheState.busy) return Promise.resolve(false);
  if (downloadTask) return downloadTask;
  let info = updateInfo.value;
  if ((options.background && backgroundGamePaused) || !info || ['checking', 'downloading', 'validating', 'installing'].includes(updateSnapshot.phase)) return Promise.resolve(false);
  const generation = maintenanceGeneration;
  backgroundAllowed = options.background === true;
  const task = (async () => {
   for (;;) {
    if (options.background && backgroundGamePaused) return false;
    const operationId = makeOperationId();
  activeOperation = operationId;
  Object.assign(updateSnapshot, { operationId, phase: 'downloading', stage: 'download', version: info.version,
    sha256: info.sha256, background: options.background === true, backgroundPaused: false, downloadedBytes: 0, totalBytes: 0, percent: 0, resumedBytes: 0, speedBps: 0, etaSeconds: 0, error: '', message: options.cacheOnly ? '正在核验已下载的更新包' : '正在下载更新包' });
    try {
      // The native worker has idle timeouts and bounded background retry rounds.
      // Read its actual ownership to recover a lost IPC completion without ever
      // releasing the JS cache lock merely because a wall-clock timer expired.
      await new Promise<void>((resolve, reject) => {
        let settled = false, timer: ReturnType<typeof setTimeout>;
        let lastChange = Date.now(), signature = '', observed = false;
        const controller = new AbortController();
        const finish = (error?: unknown) => {
          if (settled) return;
          settled = true; clearTimeout(timer);
          if (retireDownload === retire) retireDownload = null;
          // Discard a lost completion only after native inactivity/retirement
          // was proved, or after the native request itself returned.
          controller.abort();
          error ? reject(error) : resolve();
        };
        const retire = async () => {
          if (!options.background || settled) return settled;
          const receipt = await invoke<{ retired: boolean; active: boolean; activeOperation: string }>(
            'app.retireUpdateDownload', { operationId }, { timeoutMs: 5000 });
          if (!receipt || receipt.retired !== true) return false;
          if (receipt.active === false && typeof receipt.activeOperation === 'string' && receipt.activeOperation !== operationId) {
            activeOperation = makeOperationId(); updateSnapshot.operationId = activeOperation;
            finish(new Error('后台下载已安全停止，将稍后重试')); return true;
          }
          updateSnapshot.message = '正在停止无进展的后台下载，已下载部分会保留';
          return false;
        };
        retireDownload = retire;
        const poll = async () => {
          try {
            const saved = await readNativeUpdateState();
            if (!settled && saved.operationId === operationId) {
              observed = true;
              const nextSignature = [saved.phase, saved.downloadedBytes, saved.attempt, saved.resumedBytes].join(':');
              if (signature !== nextSignature) { signature = nextSignature; lastChange = Date.now(); }
              merge(saved);
              if (saved.downloadActive === false) {
                if (saved.phase === 'downloaded') finish();
                else if (saved.phase === 'failed' || saved.phase === 'interrupted') finish(new Error(saved.error || saved.message || '下载已中断'));
              }
            }
          } catch { /* Failed state reads never prove a native owner stopped. */ }
          if (!settled && options.background && (updateCacheState.busy || updateSnapshot.backgroundPaused ||
              Date.now() - lastChange >= (observed ? (updateSnapshot.phase === 'validating' ? 600000 : 180000) : 45000))) {
            try { await retire(); } catch { /* No receipt: retain ownership and retry the probe. */ }
          }
          if (!settled) timer = setTimeout(poll, 15000);
        };
        timer = setTimeout(poll, 15000);
        void downloadUpdate(updatePackageUrl(info.version), info.sha256, operationId, info.version,
          { ...options, signal: controller.signal }).then(() => finish(), error => {
            if (settled) return;
            if (!options.background || error instanceof IpcRemoteError) { finish(error); return; }
            // A WebView reset can reject IPC while its native worker survives.
            // Keep ownership until the same inactivity/fence proof as timeout.
            updateSnapshot.message = '更新连接中断，正在确认后台任务状态';
            void retire().catch(() => {});
          });
      });
      if (generation !== maintenanceGeneration) return false;
      await waitForSuccessor();
      if (generation !== maintenanceGeneration) return false;
      if (options.background && backgroundAllowed && !backgroundGamePaused && !installRequested && pendingTarget && isSuccessor(pendingTarget, info!)) {
        info = pendingTarget; pendingTarget = null; updateInfo.value = info; continue;
      }
      Object.assign(updateSnapshot, { phase: 'downloaded', stage: 'install', downloadActive:false, activeDownloadOperation:'', percent: 100, speedBps: 0, etaSeconds: 0, error: '', message: '更新包已校验，可安装并重启' });
      return true;
    } catch (error) {
      if (generation !== maintenanceGeneration) return false;
      await waitForSuccessor();
      if (generation !== maintenanceGeneration) return false;
      if (options.background && backgroundAllowed && !backgroundGamePaused && !installRequested && pendingTarget && isSuccessor(pendingTarget, info!)) {
        info = pendingTarget; pendingTarget = null; updateInfo.value = info; continue;
      }
      if (options.background && (updateSnapshot.backgroundPaused || /Background (?:download|update) paused/.test(String(error)))) {
        updateSnapshot.backgroundPaused=true;
        Object.assign(updateSnapshot,{phase:'interrupted',stage:'download',error:'',message:'后台下载已暂停，下载进度已保留'});return false;
      }
      updateSnapshot.phase = 'failed';
      updateSnapshot.error = '下载失败：' + (error instanceof Error ? error.message : String(error));
      updateSnapshot.message = updateSnapshot.error;
      return false;
    }
   }
  })();
  downloadTask = task;
  void task.finally(() => { if (downloadTask === task) { downloadTask = null; backgroundAllowed = false; } });
  return task;
}

async function cancelNativeBackground(): Promise<void> {
  if (!activeOperation) return;
  await invoke('app.cancelUpdateDownload', { operationId: activeOperation }, { timeoutMs: 5000 });
}
export async function cancelBackgroundUpdate(): Promise<void> {
  if (installRequested) return;
  if (updateSnapshot.background && ['downloading','validating'].includes(updateSnapshot.phase)) updateSnapshot.backgroundPaused = true;
  backgroundAllowed = false; backgroundGeneration++;
  await cancelNativeBackground();
}
function isSuccessor(next: UpdateInfo, current: UpdateInfo): boolean {
  try {
    return compareVersions(next.version, APP_VERSION) > 0 &&
      (compareVersions(next.version, current.version) > 0 ||
       (next.version === current.version && next.sha256.toUpperCase() !== current.sha256.toUpperCase()));
  } catch { return false; }
}
function selectPendingTarget(): void {
  if (!pendingTarget || installRequested || backgroundGamePaused || updateCacheState.busy) return;
  if (updateInfo.value && !isSuccessor(pendingTarget, updateInfo.value)) { pendingTarget = null; return; }
  updateInfo.value = pendingTarget; pendingTarget = null;
  activeOperation = makeOperationId();
  Object.assign(updateSnapshot, initial, { phase: 'available', operationId: activeOperation, stage: 'download',
    version: updateInfo.value.version, sha256: updateInfo.value.sha256, message: '已切换至最新版本，等待后台下载' });
}
export function setBackgroundGamePaused(paused: boolean): void {
  backgroundGamePaused = paused;
  if (paused) { void cancelBackgroundUpdate().catch(() => {}); }
  else if (!downloadTask && !['downloading', 'validating', 'installing'].includes(updateSnapshot.phase)) selectPendingTarget();
}
/** A persisted successor hint prevents a killed renderer from resuming or
 * announcing an old package that it already knows has been superseded. */
export async function restoreBackgroundTarget(next: UpdateInfo): Promise<void> {
  if (updateCacheState.busy || !isValidSha256(next.sha256) || compareVersions(next.version, APP_VERSION) <= 0 || installRequested) return;
  if (updateInfo.value && !isSuccessor(next, updateInfo.value)) return;
  if (pendingTarget && !isSuccessor(next, pendingTarget)) return;
  pendingTarget = next;
  if (downloadTask || ['downloading', 'validating'].includes(updateSnapshot.phase)) await cancelNativeBackground();
  else selectPendingTarget();
}
/** Read-only manifest probing is allowed during a background transfer. It never
 * changes a user-owned install or bypasses the download/cache ownership lease. */
export function probeBackgroundSuccessor(): Promise<void> {
  if (successorProbe) return successorProbe;
  if (updateCacheState.busy || backgroundGamePaused || installRequested || !backgroundAllowed || !updateInfo.value ||
      !['downloading', 'validating'].includes(updateSnapshot.phase)) return Promise.resolve();
  const generation = backgroundGeneration;
  const task = (async () => {
    const next = await checkUpdate(UPDATE_MANIFEST_URL, true);
    if (backgroundGamePaused || installRequested || generation !== backgroundGeneration || !backgroundAllowed ||
        !updateInfo.value || !isSuccessor(next, updateInfo.value) || (pendingTarget && !isSuccessor(next, pendingTarget))) return;
    await invoke('app.updatePredownloadTarget', { version: next.version, sha256: next.sha256 }, { timeoutMs: 5000 });
    // Saving the hint must precede cancellation: a crash in the handoff may not
    // bring an obsolete package back as a fresh ready notification.
    if (backgroundGamePaused || installRequested || generation !== backgroundGeneration || !backgroundAllowed) return;
    await restoreBackgroundTarget(next);
  })();
  successorProbe = task;
  void task.finally(() => { if (successorProbe === task) successorProbe = null; }).catch(() => {});
  return task;
}

/** Refresh only an adopted native download; do not overwrite a manifest check. */
export async function refreshAdoptedUpdate(): Promise<void> {
  // Reuse the service's bounded refresh opportunity, including while automatic
  // downloads are disabled or paused for a game. This never launches an install.
  if (!updateCacheState.busy && !installRequested && updateSnapshot.installerActive === true) {
    const saved = await readNativeUpdateState();
    activeOperation = saved.operationId || '';
    restoreSavedUpdateInfo(saved);
    merge(saved);
    return;
  }
  if (updateCacheState.busy || downloadTask || !['downloading', 'validating'].includes(updateSnapshot.phase)) return;
  const saved = await readNativeUpdateState();
  if (saved.operationId !== activeOperation) return;
  merge(saved);
  if (saved.downloadActive === false && saved.phase !== 'downloaded' && updateSnapshot.backgroundPaused) { updateSnapshot.phase='interrupted';updateSnapshot.stage='download'; }
  if (saved.downloadActive === false && pendingTarget && !backgroundGamePaused) { selectPendingTarget(); return; }
  if (saved.downloadActive === false && ['downloading', 'validating'].includes(updateSnapshot.phase)) {
    updateSnapshot.phase = 'interrupted';
    updateSnapshot.stage = 'download';
  }
}

export async function downloadAndInstall(): Promise<void> {
  if (updateSnapshot.installerActive === true) {
    await refreshAdoptedUpdate();
    if (updateSnapshot.installerActive === true) return;
  }
  const info = updateInfo.value;
  const retryable = ['available', 'failed', 'interrupted'].includes(updateSnapshot.phase);
  if (updateCacheState.busy || !info || installRequested || (!retryable && updateSnapshot.phase !== 'downloaded' && !downloadTask)) return;
  installRequested = true;
  try {
    const retryInstall = !downloadTask && updateSnapshot.stage === 'install';
    if (retryInstall) {
      const operationId = makeOperationId(); activeOperation = operationId; updateSnapshot.operationId = operationId;
      updateSnapshot.phase = 'installing'; updateSnapshot.message = '正在安装并重启程序';
      try { await installUpdate(operationId, info.version, info.sha256); return; }
      catch (error) {
        const detail = error instanceof Error ? error.message : String(error);
        // A module owner is not a missing/corrupt cache. Preserve the verified
        // package and return to the user instead of posting a second install.
        if (/MODULE_MAINTENANCE|Module maintenance|模块正在|UPDATE_INSTALLER_ACTIVE|UPDATE_INSTALLER_START_FAILED/.test(detail)) throw error;
        updateSnapshot.phase = 'interrupted'; updateSnapshot.stage = 'download';
      }
    }
    if (!await prepareUpdate()) return;
    const readyInfo = updateInfo.value;
    if (!readyInfo || updateSnapshot.phase !== 'downloaded') return;
    const operationId = makeOperationId(); activeOperation = operationId; updateSnapshot.operationId = operationId;
    updateSnapshot.phase = 'installing'; updateSnapshot.stage = 'install';
    updateSnapshot.message = '正在安装并重启程序';
    await installUpdate(operationId, readyInfo.version, readyInfo.sha256);
  } catch (error) {
    updateSnapshot.phase = 'failed';
    updateSnapshot.error = '更新失败：' + (error instanceof Error ? error.message : String(error));
    updateSnapshot.message = updateSnapshot.error;
  } finally { installRequested = false; }
}

/** Delete re-downloadable packages only after a queued/live background owner
 * has been fenced. The native mutex remains the authority for all cache IO. */
export async function clearUpdateCache(): Promise<boolean> {
  if (updateCacheState.busy) return false;
  if (installRequested || updateSnapshot.repairAuthorized === true || updateSnapshot.phase === 'installing' ||
      (!updateSnapshot.background && ['downloading', 'validating'].includes(updateSnapshot.phase))) {
    updateCacheState.error = '请等待手动下载或安装结束后再清理';
    updateCacheState.message = updateCacheState.error; return false;
  }
  Object.assign(updateCacheState, { busy: true, error: '', bytesFreed: 0, message: '正在停止后台下载并清理更新缓存' });
  maintenanceGeneration++;
  for (const release of maintenanceWaiters) release();
  maintenanceWaiters.clear();
  updateCacheState.revision++;
  backgroundGeneration++; backgroundAllowed = false; pendingTarget = null;
  try {
    // The retirement endpoint also fences POSTs still waiting in the IPC queue.
    const deadline = Date.now() + 45000;
    if (updateSnapshot.background && activeOperation) {
      for (;;) {
        let stopped = false;
        if (retireDownload) stopped = await retireDownload();
        else {
          const proof = await invoke<{retired:boolean;active:boolean;activeOperation:string}>(
            'app.retireUpdateDownload', {operationId:activeOperation}, {timeoutMs:5000});
          stopped = proof?.retired === true && proof.active === false &&
            typeof proof.activeOperation === 'string' && proof.activeOperation !== activeOperation;
        }
        if (stopped) break;
        if (Date.now() >= deadline) throw Error('下载任务仍在停止，请稍后重试清理');
        await new Promise(resolve => setTimeout(resolve, 1000));
      }
    }
    if (downloadTask) await downloadTask;
    if (updateSnapshot.background && ['downloading', 'validating'].includes(updateSnapshot.phase))
      Object.assign(updateSnapshot, {phase:'interrupted',stage:'download',downloadActive:false,activeDownloadOperation:''});
    activeOperation = makeOperationId();
    const receipt = await invoke<{ok:boolean;busy:boolean;bytesFreed:number;message:string}>(
      'app.clearUpdateCache', {operationId:activeOperation}, {timeoutMs:45000});
    if (!receipt || typeof receipt.ok !== 'boolean' || !Number.isFinite(receipt.bytesFreed) || receipt.bytesFreed < 0)
      throw Error('清理结果未获确认，请稍后重试');
    updateCacheState.bytesFreed = receipt.bytesFreed;
    if (!receipt.busy) {
      updateInfo.value = null;
      Object.assign(updateSnapshot, initial, {operationId:activeOperation});
    }
    updateCacheState.message = receipt.message || (receipt.ok ? '更新缓存已清理' : '清理未完成，请稍后重试');
    if (!receipt.ok) { updateCacheState.error = updateCacheState.message; return false; }
    return true;
  } catch (error) {
    updateCacheState.error = error instanceof Error ? error.message : String(error);
    updateCacheState.message = '清理未完成：' + updateCacheState.error;
    return false;
  } finally {
    updateCacheState.busy = false; updateCacheState.revision++;
    // A failed retirement must keep the original owner visible for a retry.
  }
}
