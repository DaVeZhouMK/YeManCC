import { reactive, ref } from 'vue';
import {
  checkUpdate,
  compareVersions,
  downloadUpdate,
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
    'version', 'downloadedBytes', 'totalBytes', 'percent', 'speedBps',
    'etaSeconds', 'message', 'error', 'updatedAt',
  ] as const) {
    if (next[key] !== undefined) updateSnapshot[key] = next[key] as never;
  }
}

export async function ensureUpdateManager(): Promise<void> {
  if (!offProgress) {
    offProgress = on<UpdateProgressState>('update.progress', merge);
  }
  if (initialized) return;
  initialized = true;
  try {
    const saved = await readNativeUpdateState();
    if (saved.phase === 'installing' && saved.version && compareVersions(APP_VERSION, saved.version) >= 0) {
      merge({ ...saved, phase: 'completed', percent: 100, message: '更新已完成' });
    } else if (saved.phase === 'downloading' || saved.phase === 'validating' || saved.phase === 'installing') {
      merge({ ...saved, phase: 'interrupted', message: '上次更新未完成，可重新检查更新' });
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
  if (!info || updateSnapshot.phase !== 'available') return;
  const operationId = activeOperation || makeOperationId();
  activeOperation = operationId;
  updateSnapshot.operationId = operationId;
  updateSnapshot.phase = 'downloading';
  updateSnapshot.downloadedBytes = 0;
  updateSnapshot.totalBytes = 0;
  updateSnapshot.percent = 0;
  updateSnapshot.speedBps = 0;
  updateSnapshot.etaSeconds = 0;
  updateSnapshot.error = '';
  updateSnapshot.version = info.version;
  updateSnapshot.message = `正在下载 ${info.version}`;
  try {
    await downloadUpdate(updatePackageUrl(info.version), info.sha256, operationId, info.version);
    updateSnapshot.phase = 'downloaded';
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
