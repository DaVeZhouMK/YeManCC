import { ref } from 'vue';
import { app, fs, paths } from './api';
import { InputDiagnosticsBus } from './inputDiagnostics';
import type { InputDiagnosticRecordV1 } from './inputContracts';
import { compareAndSwapInputSettings, loadSettings } from './settingsRepository';
import { gyroVirtualCaptureEnabled } from './gyroVirtualFeature';

export const inputDiagnosticsBus = new InputDiagnosticsBus();
export const inputDiagnosticsLoggingEnabled = ref(false);
export const inputDiagnosticsLogCount = ref(0);
// native 侧真实日志文件（input-capture.jsonl + 旋转旧档）是否存在（驱动「清空/导出」按钮可用性）
export const inputDiagnosticsLogFiles = ref(false);
function syncSnapshot(): void { inputDiagnosticsLogCount.value = inputDiagnosticsBus.snapshot().length; }
async function refreshNativeLogFiles(): Promise<void> {
  inputDiagnosticsLogFiles.value = await checkInputCaptureFileExists();
}
/** 检查 native 输入捕获日志（当前 + 旋转旧档）是否存在。 */
export async function checkInputCaptureFileExists(): Promise<boolean> {
  try {
    const dir = await app.dataDir();
    for (const f of ['input-capture.jsonl', 'input-capture.jsonl.prev']) {
      if (await fs.exists(`${dir}\\${f}`).catch(() => false)) return true;
    }
  } catch { /* ignore */ }
  return false;
}

export async function initializeInputDiagnosticsUi(): Promise<void> {
  // 输入日志默认关闭：native 输入采集是否写 input-capture.jsonl 由 feature-assets
  // sidecar 独立门控（诊断管道），UI 开关只反映并控制用户可见的输入诊断总线。
  // 仅在用户显式开启（持久化 inputLoggingEnabled=true）时打开，首次启动不强制开启。
  inputDiagnosticsBus.setEnabled(false);
  inputDiagnosticsLoggingEnabled.value = false;
  syncSnapshot();
  const capture = await gyroVirtualCaptureEnabled().catch(() => false);
  if (!capture) return;
  try {
    const settings = await loadSettings();
    const userEnabled = settings.input.diagnostics?.inputLoggingEnabled === true;
    inputDiagnosticsBus.setEnabled(userEnabled);
    inputDiagnosticsLoggingEnabled.value = userEnabled;
    syncSnapshot();
  } catch { /* 读取失败保持默认关闭 */ }
}
async function saveLoggingEnabled(enabled: boolean): Promise<void> {
  for (let attempt = 0; attempt < 2; attempt += 1) {
    const settings = await loadSettings();
    const result = await compareAndSwapInputSettings(settings.input.revision, { diagnostics: { inputLoggingEnabled: enabled } });
    if (result.ok) return;
  }
  throw new Error('输入日志设置被并发修改，请重试');
}
export async function setInputDiagnosticsLoggingEnabled(enabled: boolean): Promise<void> {
  await saveLoggingEnabled(enabled);
  inputDiagnosticsBus.setEnabled(enabled);
  inputDiagnosticsLoggingEnabled.value = enabled;
  syncSnapshot();
}
export async function clearInputDiagnosticLogs(): Promise<void> {
  inputDiagnosticsBus.clear();
  syncSnapshot();
  try {
    const dir = await app.dataDir();
    for (const f of ['input-capture.jsonl', 'input-capture.jsonl.prev']) {
      const p = `${dir}\\${f}`;
      if (await fs.exists(p).catch(() => false)) await fs.remove(p).catch(() => false);
    }
  } catch { /* 磁盘清理失败不阻塞 */ }
  await refreshNativeLogFiles();
}
/** 导出 native 真实捕获文件（存在时）；否则退回内存总线快照。 */
export async function exportInputDiagnosticLogs(): Promise<string> {
  const root = await paths.desktop();
  const stamp = new Date().toISOString().replace(/[-:.TZ]/g, '').slice(0, 14);
  try {
    const result = await app.exportInputCaptureLog();
    if (result.ok && result.path) return result.path;
  } catch { /* fallback below */ }
  const path = `${root}\\input-diagnostics-${stamp}.json`;
  await fs.writeTextFileAtomic(path, JSON.stringify(inputDiagnosticsBus.exportSnapshot(), null, 2));
  return path;
}
export function writeInputDiagnostic(record: InputDiagnosticRecordV1): boolean {
  const accepted = inputDiagnosticsBus.append(record);
  syncSnapshot();
  return accepted;
}
export function makeUiInputDiagnostic(
  eventName: string,
  operation: string,
  result: 'ok' | 'rejected' | 'unknown',
  error: string | null = null,
): InputDiagnosticRecordV1 {
  const now = new Date();
  return {
    schemaVersion: 1, revision: 1, closure: 'UNENCLOSED',
    timestampUtc: now.toISOString(), timestampLocal: now.toString(),
    source: 'settings-ui', eventName, operation,
    runId: null, hostInstanceId: null, processId: null,
    nativeGeneration: null, inputEpoch: null, configRevision: null,
    configHash: null, targetId: null, persona: null, lifecyclePhase: 'disabled',
    transportResult: result, callbackResult: result, readback: 'not-observed',
    physicalObservation: 'not-applicable', error, releaseProofId: null,
  };
}
