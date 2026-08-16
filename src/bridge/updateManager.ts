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
import { on } from './ipc';
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
let initialized = false;
let activeOperation = '';
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
    'version', 'attempt', 'nextAttempt', 'retryInSeconds', 'remainingRetrySeconds',
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
    if (compareVersions(saved.version, APP_VERSION) <= 0) return;
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
  initialized = true;
  try {
    const saved = await readNativeUpdateState();
    restoreSavedUpdateInfo(saved);
    if (saved.phase === 'installing' && saved.version && compareVersions(APP_VERSION, saved.version) >= 0) {
      merge({ ...saved, phase: 'completed', percent: 100, message: '更新已完成' });
    } else if (
      (saved.phase === 'downloaded' || saved.phase === 'installing' || saved.phase === 'interrupted') &&
      saved.stage === 'install'
    ) {
      merge({ ...saved, phase: 'interrupted', message: '上次安装未完成，可重试安装' });
    } else if (saved.phase === 'downloading' || saved.phase === 'validating' || saved.phase === 'installing') {
      merge({ ...saved, phase: 'interrupted', message: '上次下载未完成，可继续下载' });
    } else {
      merge(saved);
    }
  } catch {
    /* Native state is optional during browser preview. */
  }
}

export async function checkForUpdate(appVersion: string): Promise<void> {
  await ensureUpdateManager();
  if (updateSnapshot.phase === 'downloading' || updateSnapshot.phase === 'installing') return;
  updateInfo.value = null;
  activeOperation = makeOperationId();
  updateSnapshot.operationId = activeOperation;
  updateSnapshot.phase = 'checking';
  updateSnapshot.error = '';
  updateSnapshot.message = '正在连接更新服务器';
  try {
    const info = await checkUpdate(UPDATE_MANIFEST_URL);
    if (compareVersions(info.version, appVersion) > 0) {
      updateInfo.value = info;
      updateSnapshot.phase = 'available';
      updateSnapshot.sha256 = info.sha256;
      updateSnapshot.version = info.version;
      updateSnapshot.message = '发现新版本';
    } else {
      updateSnapshot.phase = 'latest';
      updateSnapshot.message = '当前已是最新版本';
    }
  } catch (error) {
    updateSnapshot.phase = 'failed';
    updateSnapshot.error = `检查失败：${error instanceof Error ? error.message : String(error)}`;
    updateSnapshot.message = updateSnapshot.error;
  }
}

export async function downloadAndInstall(): Promise<void> {
  const info = updateInfo.value;
  if (!info || !['available', 'failed', 'interrupted'].includes(updateSnapshot.phase)) return;
  const operationId = activeOperation || makeOperationId();
  const isResume = updateSnapshot.phase === 'failed' || updateSnapshot.phase === 'interrupted';
  const retryInstall = isResume && updateSnapshot.stage === 'install';
  activeOperation = operationId;
  updateSnapshot.operationId = operationId;
  updateSnapshot.speedBps = 0;
  updateSnapshot.etaSeconds = 0;
  updateSnapshot.error = '';
  updateSnapshot.version = info.version;
  try {
    if (retryInstall) {
      updateSnapshot.stage = 'install';
      updateSnapshot.phase = 'installing';
      updateSnapshot.message = `正在重试安装 ${info.version}`;
      try {
        await installUpdate(operationId, info.version, info.sha256);
        return;
      } catch {
        // Preflight failures (for example a missing package) can be repaired
        // by downloading a fresh, verified package before trying installation
        // again. If the helper already launched, this process will exit and
        // this fallback is never reached.
        updateSnapshot.stage = 'download';
        updateSnapshot.phase = 'downloading';
        updateSnapshot.message = `安装前置检查失败，正在重新下载 ${info.version}`;
      }
    } else {
      updateSnapshot.stage = 'download';
      updateSnapshot.phase = 'downloading';
      updateSnapshot.message = isResume ? `正在继续下载 ${info.version}` : `正在下载 ${info.version}`;
    }
    await downloadUpdate(updatePackageUrl(info.version), info.sha256, operationId, info.version);
    updateSnapshot.phase = 'downloaded';
    updateSnapshot.stage = 'install';
    updateSnapshot.percent = 100;
    updateSnapshot.message = '下载完成，准备安装';
    updateSnapshot.phase = 'installing';
    updateSnapshot.message = '正在安装并重启程序';
    await installUpdate(operationId, info.version, info.sha256);
  } catch (error) {
    updateSnapshot.phase = 'failed';
    updateSnapshot.error = `更新失败：${error instanceof Error ? error.message : String(error)}`;
    updateSnapshot.message = updateSnapshot.error;
  }
}
