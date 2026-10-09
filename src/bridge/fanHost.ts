import { app, fs, http, proc, shell, powerLifecycle, type PowerLifecycleState, type FanActivitySnapshot, type FanManualWakeResult } from './api';
import { resolveFanSnapshotGeneration, isNoPowerOperationReceipt, isColdUnopenedFanHost, isCompleteAdmissibleHcSession, hasBlockingFanTransition, hasFailedFanOperation, type FanSnapshotGenerationEvidence } from './fanSessionEvidence';
import { ensureFanHostInstaller } from './fanHostInstallerRepair';
import {
  createFanApiAdapter,
  FanApiError,
  type FanApiAdapter,
  type FanHandshake,
  type FanLease,
  type FanNode,
  type FanState,
} from './fanApi';
import { fanDiagnosticLog, fanLifecycleEvidence, setFanDiagnosticPowerGeneration } from './fanDiagnostics';
import { FanCoordinatorGate, type FanCoordinatorPowerEvent } from './hardwareCoordinator';
import { parseAiFanMockSession, aiFanMockArguments, aiFanMockHandshakeAllowed, aiFanMockStateAllowed, aiFanRuntimeEvidence, type AiFanMockSession } from './aiFanMock';

/**
 * Real-host integration boundary.
 *
 * This flag is deliberately independent from the UI import flag.  Keeping a
 * second gate makes the real Host independently reversible from the UI import.
 */
export const FAN_REAL_HOST_ENABLED = true;
export const FAN_HOST_PROTOCOL_VERSION = 2;
export const FAN_HOST_DEFAULT_PORT = 8765;
const FAN_HOST_V3_ADAPTER_FILE = 'YeManFanHcAdapter.dll';
// 租约协议完整性（第七十七批 C/E-③，2026-09-12 用户批准）：Host 端 lease
// TTL=15s（FanLab\real-host\Program.cs MonoMs()+15000），续租是协调层【架构需求】
// 协议的双侧义务，不是定时冗余。前端以 5s < TTL 周期续租；E9 曾误删心跳导致
// LEASE_INVALID（0912-15 实锤），本版恢复。guard/双锁/recover 维持 E9 删除面。
const DEFAULT_LEASE_RENEWAL_INTERVAL_MS = 5000;
// Retained only for adapter/option compatibility; YMCC no longer schedules a
// resident fan-guard loop.
const FAN_GUARD_INTERVAL_MS = 10_000;
/**
 * R2（FAN-926R 唤醒租约与有界等待裁决 §4.2）：恢复期等待的**客户端**预算。
 * 这是"客户端等多久才放弃接入"的**绝对截止**（单调时间，从该次任务开始只设一次），
 * 与宿主内部 lease TTL 是两个不同概念，也**不**重置宿主既有的 60 s 恢复总预算。
 */
const RESUME_WAIT_DEADLINE_MS = 15_000;
/** §4.2：宿主建议间隔（`retryAfterMs`）经校验后限于 250–1000 ms；无合法值用 250 ms。 */
const RESUME_WAIT_MIN_INTERVAL_MS = 250;
const RESUME_WAIT_MAX_INTERVAL_MS = 1000;
/** §4.2：单次探测的超时不得超过剩余预算；下限保证传输层不会因过小超时被误判为失败。 */
const RESUME_WAIT_MIN_PROBE_TIMEOUT_MS = 500;
const RESUME_WAIT_MAX_PROBE_TIMEOUT_MS = 5_000;
/**
 * FAN-938 R5 §3：Fan 恢复 owner 的退避节拍（毫秒，末位为上限）。
 * 单次准入超时/未就绪**不**结束恢复任务：owner 以该序列重试，直到本代设备写入
 * 许可成立（或出现认证/外部 owner 这类需要人工介入的终态）。
 */
const RECOVERY_BACKOFF_MS = [250, 500, 1000, 2000, 5000] as const;
/**
 * FAN-938 R6 §P3：旧 Fan Host 被判定"未落定"后，给优雅退出一次有界观察窗口，
 * 之后必须用绑定了 PID+processCreated+path 的精确句柄终止确认（`process.terminateExact`）。
 * 旧 HTTP/端口消失不能代替退出证明。
 */
const FAN_PREVIOUS_HOST_EXIT_OBSERVE_MS = 4000;

function cloneFanNodes(nodes: readonly FanNode[]): FanNode[] {
  return nodes.map(({ tempC, dutyPercent }) => ({ tempC, dutyPercent }));
}

/** 单调时钟（§4.2 要求"单调时间绝对截止"；无 performance 时退回 Date.now）。 */
function monotonicNowMs(): number {
  const perf = (globalThis as { performance?: { now?: () => number } }).performance;
  return typeof perf?.now === 'function' ? perf.now() : Date.now();
}

function sleepMs(ms: number): Promise<void> {
  return new Promise((resolve) => { setTimeout(resolve, Math.max(0, ms)); });
}

/**
 * FAN-927 §3/§5：生产环境的 native 活动记录读取。不可读/超时一律返回 null，
 * 使调用方 fail-closed（没有依据就**不**动作，既不启动 Host 也不写硬件）。
 */
async function defaultReadNativeActivity(): Promise<FanActivitySnapshot | null> {
  try {
    const snapshot = await app.fanActivity();
    if (!snapshot || typeof snapshot !== 'object') return null;
    return snapshot;
  } catch {
    return null;
  }
}

function describeUnknownError(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

export type FanHostLifecycleState =
  | 'disabled'
  | 'stopped'
  | 'starting'
  | 'handshaking'
  | 'ready'
  | 'awaiting-control'
  | 'suspended'
  | 'conflict-locked'
  | 'fault-locked'
  | 'unknown';

/**
 * FAN-926R item 3: the public lifecycle contract. `FanHostLifecycleState` stays
 * the internal implementation detail; this projection is the stable vocabulary
 * that the UI, the gates and the delivery report depend on.
 *
 *   disabled   - the Fan Host feature is off in this build
 *   stopped    - admitted, but no Host, no handshake, no HC session, no timer
 *   starting   - a start was requested; spawn + handshake are in flight
 *   ready      - the handshake was accepted; no HC write authority yet
 *   active     - the formal HC receipt is held and hardware writes are live
 *   recovering - the Host owns an OEM restore / post-sleep rebuild window
 *   failed     - the last operation failed; an explicit retry is required
 */
export type FanHostPhase =
  | 'disabled'
  | 'stopped'
  | 'starting'
  | 'ready'
  | 'active'
  | 'recovering'
  | 'failed';

/** FAN-926R §2.1 / FAN-932 §2.3: what entering the fan page is allowed to do.
 *   idle                   - show the page; start nothing
 *   adopt-resident-session - read back a session that is already resident
 * Cold start and page entry never start the Host. FAN-932 keeps this rule: the
 * page entry is NOT a start path. The `startupDesired.fanControl` preference is
 * consumed only by the App-level boot / wake auto-start orchestration (see
 * `ensureFanAutoStart` in App.vue), never by page entry. */
export type FanEntryAction = 'idle' | 'adopt-resident-session';
export function resolveFanEntryAction(phase: FanHostPhase): FanEntryAction {
  return phase === 'stopped' || phase === 'disabled' ? 'idle' : 'adopt-resident-session';
}

export interface FanHostDependency {
  file: string;
  sha256?: string;
  version?: string;
}

/** Runtime dependency manifest. Files are copied only in a separately
 * authorized host package; this source tree does not bundle HC assemblies. */
export const HC_FAN_DEPENDENCY_MANIFEST: readonly FanHostDependency[] = [
  { file: 'YeManFanHost.exe' },
  { file: 'YeManFanHost.dll' },
  { file: 'YeManFanHost.deps.json' },
  { file: 'YeManFanHost.runtimeconfig.json' },
  { file: 'HandheldCompanion.deps.json' },
  { file: 'Microsoft.Windows.SDK.NET.dll' },
  { file: 'WinRT.Runtime.dll' },
  { file: 'Sentry.dll' },
  { file: 'Shared.dll' },
  { file: 'Serilog.dll' },
  { file: 'Serilog.Extensions.Logging.dll' },
  { file: 'Serilog.Extensions.Logging.File.dll' },
  { file: 'Serilog.Formatting.Compact.dll' },
  { file: 'Serilog.Sinks.Async.dll' },
  { file: 'Serilog.Sinks.Console.dll' },
  { file: 'Serilog.Sinks.RollingFile.dll' },
  {
    file: 'HandheldCompanion.dll',
    version: '0.32.4.0',
    sha256: '886ac06bce514538f379fad8dfe66a25ae954da6dac44a9690d04830142d957b',
  },
  { file: 'GamepadMotion.dll' },
  // HC loads LibreHardwareMonitor through OpenLibSys on EC-based devices
  // such as GPD Win5. Preflight it so handshake cannot hide an Open failure.
  { file: 'LibreHardwareMonitorLib.dll', sha256: 'f7ed30f07ea636c0ddcc5764c15c0a25ae8a0aca02ded77e4af24439955487cf' },
  { file: 'hidapi.net.dll', sha256: '5553f2487424b325f750c0fe83bd7961943cc97f2e0d1c24285506374b298f17' },
  { file: 'hidapi.dll', sha256: 'ebeb835e2b4530ed68843f19d6a2604c51772e3c26e7f542fde194075f82d9b4' },
  // HC IDevice.GetCurrent references the full device factory. These files
  // are its small non-UI bootstrap closure and must be present before the
  // Host is allowed to load HandheldCompanion.dll.
  { file: 'WindowsInput.dll', sha256: '5567cea4661389a7fdcc51ef222e67b13c2176c9be46e61a88a100188a77c711' },
  { file: 'GregsStack.InputSimulatorStandard.dll', sha256: '453e8a4b4cf7241954e9aad060409c24f076ee0c9f742345fc36a1ea8dd8c6ee' },
  { file: 'Gma.System.MouseKeyHook.dll', sha256: 'fa9fec4dfc02c80d262e2e61abce31d9358ca84e36c9794ba5cb30f912940485' },
  { file: 'HidLibrary.dll', sha256: '00ad68889764a8bea6377a01d738a3ebc1dd286691d2ac5bcf7b1d2b16bcd9fa' },
  { file: 'Nefarius.Utilities.DeviceManagement.dll', sha256: 'b5eaf086634438f2774f6b65dd14254aaa078bf1ebfeb004f997314b61272b7c' },
  { file: 'Nefarius.Utilities.Bluetooth.dll', sha256: '010b46997f2bea44a9e95b063e106be3e662a93a7a7fab5e5d485644cc48b433' },
  { file: 'Nefarius.Vicius.Abstractions.dll', sha256: '51f380a12a82e925308e5d6255218df283b692f37b9e991c6ce5e63f3e11d8fa' },
  { file: 'PInvoke.Kernel32.dll', sha256: '3122b9c2ccd89b0ff915f4669d60f9ffa1a4d4a8608f61f5df1b29d6298c4c44' },
  { file: 'PInvoke.Windows.Core.dll', sha256: '28dc91c7027ba45b07be564a4564cf9e4606b96b01f4b431056e7d77ab25b81c' },
  { file: 'SharpDX.dll', sha256: '518d45a5aaec84cb37e83ee2cf58c503ab6a25febb8c48b53316340c967e84bd' },
  { file: 'SharpDX.Direct3D9.dll', sha256: '69701eda7433ac0010aba416b9d9c245cd78694770d4bb6b7541b83bace41d55' },
  { file: 'SharpDX.DirectInput.dll', sha256: '35d9ae6b98c5b68fdc1fcaf6e03c95c82f9305c7355dd911f8841880b42e945f' },
  { file: 'SharpDX.XInput.dll', sha256: '350195201205840b38aee094bcead4c78b1661f3570a7caa5c36b86ce6d03ff3' },
  { file: 'Serilog.Sinks.File.dll' },
  { file: 'Serilog.Settings.Configuration.dll' },
  // These must be the HC-pinned Windows runtime-target implementations, not
  // their generic lib/net10.0 reference counterparts. The Host resolves HC
  // manually and therefore cannot let the normal deps resolver choose them.
  { file: 'System.Management.dll', sha256: '01f9360d110863f810431c4d29ada0fca89f267343d030e98aa823ea4c0c0ebb' },
  { file: 'System.IO.Ports.dll', sha256: 'bf486068a47b18358313791b78aca74f4de61d1d9e2e08b58e3bfbf68bf15a2b' },
  { file: 'System.ServiceProcess.ServiceController.dll', sha256: '3274c2553c736435064e398f879404e8944f39790caee6632e6966046b3440e8' },
];

export interface FanHostConfig {
  /** Native command-line capability only; normal production config has no value. */
  aiMockSession?: AiFanMockSession;
  hostExecutable: string;
  hostDirectory: string;
  hcRuntimeDirectory: string;
  hcAssemblyPath: string;
  authorizationPath: string;
  allowHardwareWrites: boolean;
  confirmationToken: string;
  baseUrl: string;
  protocolVersion: number;
  dependencies: readonly FanHostDependency[];
  sessionToken: string;
  sessionTokenPath: string;
}

function trimWindowsPath(value: string): string {
  return value.replace(/[\\/]+$/, '');
}

function joinWindowsPath(root: string, child: string): string {
  return `${trimWindowsPath(root)}\\${child}`;
}

function parseJson<T>(raw: string): T {
  // PowerShell UTF-8 output may include a BOM; JSON.parse rejects it even
  // though Windows/.NET JSON readers accept the same file.
  return JSON.parse(raw.replace(/^\uFEFF/, '')) as T;
}

function createSessionToken(): string {
  const bytes = new Uint8Array(32);
  try {
    globalThis.crypto?.getRandomValues(bytes);
  } catch { /* fall through to a local entropy fallback */ }
  // The token is a same-user loopback capability. WebView builds normally
  // provide crypto.getRandomValues, but a missing WebCrypto surface must not
  // silently create an all-zero token that can be guessed by another local
  // process.
  if (bytes.every((value) => value === 0)) {
    const seed = `${Date.now()}-${Math.random()}-${Math.random()}`;
    for (let i = 0; i < bytes.length; i++) {
      bytes[i] = (seed.charCodeAt(i % seed.length) + Math.floor(Math.random() * 256) + i * 31) & 0xff;
    }
  }
  return Array.from(bytes, (value) => value.toString(16).padStart(2, '0')).join('');
}

function isSessionToken(value: string | undefined): value is string {
  return typeof value === 'string' && /^[0-9a-f]{64}$/i.test(value.trim());
}

/**
 * 203/Fan b141（FAN925B §4.3 / CP-05）：凭据的**非秘密**版本标识 = sha256(token) 前 8 位
 * 小写十六进制，与 native `fanCredentialVersionId` / Host `CredentialVersionId` 同算法同格式
 * （可跨端对账）。**不是**凭据、不是前缀、不可反推；只用于把三份副本对齐到同一版本。
 */
async function sessionTokenFingerprint(token: string): Promise<string> {
  if (!isSessionToken(token)) return '';
  try {
    const digest = await globalThis.crypto?.subtle?.digest('SHA-256', new TextEncoder().encode(token));
    if (!digest) return '';
    return Array.from(new Uint8Array(digest).slice(0, 4), (b) => b.toString(16).padStart(2, '0')).join('');
  } catch { return ''; }
}

function readRemoteFanStateName(body: string): string {
  try {
    const parsed = JSON.parse(body || '{}');
    if (typeof parsed?.state === 'string') return parsed.state;
    if (typeof parsed?.state?.state === 'string') return parsed.state.state;
    return '';
  } catch {
    return '';
  }
}

function isLiveResidentFanState(stateName: string): boolean {
  const state = stateName.toLowerCase();
  return state === 'suspended' || state === 'resuming' || state === 'resumed'
    || state === 'ready' || state === 'open' || state === 'awaitingcontrol'
    || state === 'starting' || state === 'handshaking';
}

function isSuspendedFanState(stateName: string | undefined, powerState?: string): boolean {
  const state = String(stateName ?? '').toLowerCase();
  const power = String(powerState ?? '').toLowerCase();
  return state === 'suspended' || state === 'resuming' || power === 'suspended';
}

/**
 * A successful unauthenticated health response is the only evidence that a
 * resident Host predates the loopback session-token boundary. A 401 response
 * with a mismatched token is deliberately not treated as legacy.
 */
export function isLegacyUnauthenticatedFanHostHealth(
  response: { status: number; body: string } | null | undefined,
  protocolVersion: number,
): boolean {
  if (!response || response.status < 200 || response.status >= 300) return false;
  try {
    const payload = JSON.parse(response.body || '{}');
    return payload?.host === 'YeManFanHost' && payload?.protocolVersion === protocolVersion;
  } catch {
    return false;
  }
}

export function resolveFanHostConfig(
  powerControlDir = 'C:\\SOFT\\YeMan\\PowerControl',
): FanHostConfig {
  const hostDirectory = joinWindowsPath(powerControlDir, 'fan-host-v2');
  // The payload manifest is the only source of truth for the HC runtime path.
  // Keep the historical directory as a pre-read fallback; start() resolves
  // the manifest before any ACL, dependency, or HC-load operation.
  const hcRuntimeDirectory = joinWindowsPath(powerControlDir, 'handheldcompanion-runtime');
  return {
    hostDirectory,
    hcRuntimeDirectory,
    hostExecutable: joinWindowsPath(hostDirectory, 'YeManFanHost.exe'),
    hcAssemblyPath: joinWindowsPath(hcRuntimeDirectory, 'HandheldCompanion.dll'),
    authorizationPath: joinWindowsPath(hostDirectory, 'YeManFanHost.authorization.md'),
    // The Host still requires the authorization record and confirmation token;
    // a missing/invalid record keeps it in read-only handshake mode.
    allowHardwareWrites: true,
    // The Host validates a per-launch random confirmation equal to its
    // protected loopback session token. Never keep a reusable write password
    // in the shipped frontend source.
    confirmationToken: '',
    baseUrl: `http://127.0.0.1:${FAN_HOST_DEFAULT_PORT}`,
    protocolVersion: FAN_HOST_PROTOCOL_VERSION,
    dependencies: HC_FAN_DEPENDENCY_MANIFEST,
    // Loaded from the protected sidecar on first start. Keeping this empty in
    // the resolved config prevents a new token from invalidating an existing
    // resident Host during every UI process restart.
    sessionToken: '',
    sessionTokenPath: joinWindowsPath(hostDirectory, 'YeManFanHost.session'),
  };
}

type FanHostPayloadManifest = {
  schemaVersion?: unknown;
  runtimeManifest?: unknown;
  /** FAN-927 §2：部署代描述的一部分（与 runtime manifest 的 runtimeId 必须一致）。 */
  runtimeId?: unknown;
  files?: unknown;
};

async function resolveRuntimeFromPayloadManifest(config: FanHostConfig): Promise<void> {
  const manifestPath = joinWindowsPath(config.hostDirectory, 'YeManFanHost.payload.json');
  if (!(await fs.exists(manifestPath))) return;
  try {
    const raw = await fs.readTextFile(manifestPath, 2 * 1024 * 1024);
    const manifest = parseJson<FanHostPayloadManifest>(raw);
    const runtimeManifest = typeof manifest.runtimeManifest === 'string' ? manifest.runtimeManifest.trim() : '';
    if (!/^\.\.[\\/][^\\/]+(?:[\\/][^\\/]+)*[\\/]HandheldCompanion\.runtime\.json$/i.test(runtimeManifest) ||
        runtimeManifest.slice(3).includes('..')) return;
    const hostParent = config.hostDirectory.replace(/[\\/]fan-host(?:-v2)?$/i, '');
    const runtimeManifestPath = joinWindowsPath(hostParent, runtimeManifest.slice(3));
    const marker = '\\HandheldCompanion.runtime.json';
    if (!runtimeManifestPath.toLowerCase().endsWith(marker.toLowerCase())) return;
    const lastSlash = runtimeManifestPath.lastIndexOf('\\');
    if (lastSlash <= 0) return;
    const runtimeDirectory = runtimeManifestPath.slice(0, lastSlash);
    config.hcRuntimeDirectory = runtimeDirectory;
    config.hcAssemblyPath = joinWindowsPath(runtimeDirectory, 'HandheldCompanion.dll');
  } catch { /* dependency validation reports the authoritative failure */ }
}

/**
 * FAN-927 §2（2026-09-27 用户批准）：**完整文件校验归部署边界**。
 *
 * 这里只有：① 两个**小清单**的摘要（允许的固定开销）；② 一份小"部署记录"的匹配。
 * 正常开启不再逐文件 `exists`、不再为每个文件起一次 `certutil`、不再运行安装脚本、
 * 不再遍历依赖目录。触发一次完整验证（安装脚本进程内 SHA-256）仅限：新装/更新/回滚部署、
 * 包代描述变更、记录缺失或不匹配、显式修复——其中也包含便携包/旧安装的**首次明确开启**一次
 * 快速批量初始化。
 *
 * 明确接受的信任调整：部署记录证明该包代在部署时完整，不保证每个 DLL 此刻仍与发布字节相同；
 * 绕过更新流程只改 DLL 并保持清单不变的离线替换不再由每次开启的全量哈希发现。完整性由
 * 构建、部署与修复环节负责。
 */
const FAN_HOST_PAYLOAD_MANIFEST_FILE = 'YeManFanHost.payload.json';
const FAN_HOST_RUNTIME_MANIFEST_FILE = 'HandheldCompanion.runtime.json';
const FAN_HOST_DEPLOYMENT_RECORD_FILE = 'YeManFanHost.deployment.json';
const FAN_HOST_DEPLOYMENT_RECORD_SCHEMA = 1;
/**
 * 部署验证规则版本：任何改变"一个包代是否算已完整验证"判据的改动都必须提升它，
 * 使既有记录自动失效并触发一次重新验证。这是"运行不遍历 DLL"的唯一替代物。
 */
const FAN_HOST_VALIDATION_RULES_VERSION = 1;

interface FanHostDeploymentIdentity {
  payloadRoot: string;
  runtimeRoot: string;
  payloadManifestSha256: string;
  runtimeManifestSha256: string;
  runtimeId: string;
}

interface FanHostDeploymentRecord extends FanHostDeploymentIdentity {
  schemaVersion: number;
  validationRulesVersion: number;
  result: string;
}

function normalizeDeploymentPath(value: string): string {
  return value.replace(/[\\/]+$/, '').replace(/\//g, '\\').toLowerCase();
}

/**
 * FAN-927 §2：部署边界判定所需的**全部** I/O。生产为真实 `fs`/安装脚本；
 * 自测注入替身，以便用**实际调用次数**证明"记录命中 ⇒ 逐文件 certutil=0、
 * 安装全验证进程=0、全文件遍历/哈希=0"。
 */
export interface FanHostDeploymentIo {
  exists(path: string): Promise<boolean>;
  readTextFile(path: string, maxBytes: number): Promise<string>;
  sha256File(path: string): Promise<string>;
  /** 完整验证（新装/更新/回滚/修复/无记录首次开启）。只允许在记录未命中时调用。 */
  runInstaller(config: FanHostConfig, fanStateDirectory: string): Promise<void>;
}

/** 只对**单个小清单**取摘要；失败/不可读一律返回 null（fail-closed）。 */
async function hashDeploymentFile(io: FanHostDeploymentIo, path: string): Promise<string | null> {
  try {
    const digest = await io.sha256File(path);
    return typeof digest === 'string' && /^[0-9a-f]{64}$/i.test(digest.trim())
      ? digest.trim().toLowerCase()
      : null;
  } catch {
    return null;
  }
}

async function readDeploymentRecord(
  io: FanHostDeploymentIo,
  recordPath: string,
): Promise<FanHostDeploymentRecord | null> {
  try {
    if (!(await io.exists(recordPath))) return null;
    const raw = await io.readTextFile(recordPath, 64 * 1024);
    const record = parseJson<FanHostDeploymentRecord>(raw);
    if (!record || typeof record !== 'object') return null;
    if (record.schemaVersion !== FAN_HOST_DEPLOYMENT_RECORD_SCHEMA) return null;
    if (typeof record.payloadRoot !== 'string' || typeof record.runtimeRoot !== 'string') return null;
    if (typeof record.payloadManifestSha256 !== 'string' || typeof record.runtimeManifestSha256 !== 'string') return null;
    if (typeof record.runtimeId !== 'string' || typeof record.result !== 'string') return null;
    return record;
  } catch {
    return null;
  }
}

function deploymentRecordMatches(record: FanHostDeploymentRecord, identity: FanHostDeploymentIdentity): boolean {
  return record.result === 'ok'
    && record.validationRulesVersion === FAN_HOST_VALIDATION_RULES_VERSION
    && record.payloadRoot === identity.payloadRoot
    && record.runtimeRoot === identity.runtimeRoot
    && record.payloadManifestSha256 === identity.payloadManifestSha256
    && record.runtimeManifestSha256 === identity.runtimeManifestSha256
    && record.runtimeId === identity.runtimeId;
}

/** 当前包代描述。只读 payload manifest（取摘要 + runtimeId）与 runtime manifest（取摘要）。 */
async function currentDeploymentIdentity(
  io: FanHostDeploymentIo,
  config: FanHostConfig,
): Promise<FanHostDeploymentIdentity | null> {
  const payloadManifestPath = joinWindowsPath(config.hostDirectory, FAN_HOST_PAYLOAD_MANIFEST_FILE);
  const runtimeManifestPath = joinWindowsPath(config.hcRuntimeDirectory, FAN_HOST_RUNTIME_MANIFEST_FILE);
  const payloadManifestSha256 = await hashDeploymentFile(io, payloadManifestPath);
  if (!payloadManifestSha256) return null;
  const runtimeManifestSha256 = await hashDeploymentFile(io, runtimeManifestPath);
  if (!runtimeManifestSha256) return null;
  let runtimeId = '';
  try {
    const manifest = parseJson<FanHostPayloadManifest>(await io.readTextFile(payloadManifestPath, 2 * 1024 * 1024));
    runtimeId = typeof manifest.runtimeId === 'string' ? manifest.runtimeId.trim() : '';
  } catch {
    return null;
  }
  if (!runtimeId) return null;
  return {
    payloadRoot: normalizeDeploymentPath(config.hostDirectory),
    runtimeRoot: normalizeDeploymentPath(config.hcRuntimeDirectory),
    payloadManifestSha256,
    runtimeManifestSha256,
    runtimeId,
  };
}

/** FAN-927 §2：部署边界判定的**唯一**实现（唯一副作用 = 必要时调用一次完整验证）。 */
export type FanHostDeploymentDecision = 'record-hit' | 'initialized';

export async function resolveFanHostDeployment(
  config: FanHostConfig,
  fanStateDirectory: string,
  io: FanHostDeploymentIo,
): Promise<FanHostDeploymentDecision> {
  const recordPath = joinWindowsPath(fanStateDirectory, FAN_HOST_DEPLOYMENT_RECORD_FILE);
  const identity = await currentDeploymentIdentity(io, config);
  if (!identity) {
    throw new Error(`Fan Host 部署身份不可读：缺少或无法解析 ${FAN_HOST_PAYLOAD_MANIFEST_FILE}/${FAN_HOST_RUNTIME_MANIFEST_FILE}`);
  }
  const record = await readDeploymentRecord(io, recordPath);
  if (record && deploymentRecordMatches(record, identity)) {
    fanDiagnosticLog('lifecycle.deployment-record-hit', {
      runtimeId: identity.runtimeId,
      payloadRoot: identity.payloadRoot,
      recordPath,
    });
    return 'record-hit';
  }
  fanDiagnosticLog('lifecycle.deployment-record-miss', {
    reason: record ? 'identity-mismatch' : 'record-absent',
    runtimeId: identity.runtimeId,
    recordPath,
  });
  // 新装/更新/回滚、包代描述变更、记录缺失或不匹配、显式修复、portable 首次明确开启：
  // 只在这里做**一次**完整验证（安装脚本进程内 SHA-256），成功即继续。
  await io.runInstaller(config, fanStateDirectory);
  const confirmed = await readDeploymentRecord(io, recordPath);
  if (!confirmed || !deploymentRecordMatches(confirmed, identity)) {
    throw new Error('Fan Host 部署记录未生成或与当前包代不一致，拒绝启动（完整性由部署/修复环节负责）');
  }
  fanDiagnosticLog('lifecycle.deployment-record-written', { runtimeId: identity.runtimeId, recordPath });
  return 'initialized';
}

export interface FanHostProcess {
  pid: number;
  executable: string;
  /** Creation time captured at launch/adoption; never rebind a reused PID at stop. */
  processCreated?: string;
  /** True when start() reattached a live Host instead of launching a new one. */
  adopted?: boolean;
}

export interface FanHostLauncher {
  start(config: FanHostConfig): Promise<FanHostProcess>;
  stop(process: FanHostProcess): Promise<void>;
}

/** Native launcher is only called after the independent real-host Gate. */
export class NativeFanHostLauncher implements FanHostLauncher {
  private readonly aiOwnedPids = new Set<number>();
  /**
   * FAN-926R UX-1（2026-09-27，用户反馈"按阶段显示文字"）：把启动**阶段**只读地报告给
   * 调用方，供风扇页把"首次启用"那段十几秒讲清楚。
   *
   * 硬约束：**只报告**。它不改变启动顺序、不改变任何准入/写入判定，也不发起任何额外
   * I/O；真正的阶段来源仍是同一个 `lifecycle.start-stage` 诊断行（行内容不变）。
   */
  constructor(private readonly onStage?: (stage: string) => void) {}

  async start(config: FanHostConfig): Promise<FanHostProcess> {
    let rawSession: unknown;
    try { rawSession = await app.aiFanMockSession(); }
    catch (error) {
      // Only an older native binary's explicit unknown-command response is a
      // compatibility fallback. Timeout/malformed evidence never selects real.
      if (!/^unknown: app\.aiFanMockSession$/.test(String(error instanceof Error ? error.message : error))) throw error;
      rawSession = { schemaVersion: 1, enabled: false };
    }
    const aiSession = parseAiFanMockSession(rawSession);
    if (aiSession) return this.startAiMock(config, aiSession);
    if (config.aiMockSession) throw new Error('AI_FAN_NATIVE_CAPABILITY_LOST');
    this.onStage?.('launcher-enter');
    fanDiagnosticLog('lifecycle.start-stage', { stage: 'launcher-enter' });
    if (!(await fs.exists(config.hostExecutable))) {
      throw new Error(`Fan Host 不存在: ${config.hostExecutable}`);
    }
    this.onStage?.('host-executable-found');
    fanDiagnosticLog('lifecycle.start-stage', { stage: 'host-executable-found' });
    await resolveRuntimeFromPayloadManifest(config);
    this.onStage?.('runtime-manifest-resolved');
    fanDiagnosticLog('lifecycle.start-stage', { stage: 'runtime-manifest-resolved', runtimeRoot: config.hcRuntimeDirectory });
    // Installed binaries are immutable release payloads. Keep the mutable
    // loopback capability in the current user's application-data directory,
    // not beside the Host executable where an updater/extractor may replace
    // files while a hardware session exists.
    // Keep the session capability in the stable native Fan Host directory.
    // app.dataDir() follows the configurable window title and caused an old
    // Host to become unreachable after a title/configuration change.
    const fanStateDirectory = await app.fanStateDir();
    this.onStage?.('fan-state-dir-resolved');
    fanDiagnosticLog('lifecycle.start-stage', { stage: 'fan-state-dir-resolved', fanStateDirectory });
    if (!(await fs.mkdir(fanStateDirectory))) {
      throw new Error(`无法创建 Fan Host 会话目录: ${fanStateDirectory}`);
    }
    config.sessionTokenPath = joinWindowsPath(fanStateDirectory, 'YeManFanHost.session');
    if (!config.sessionToken) {
      try {
        const persisted = (await fs.readTextFile(config.sessionTokenPath, 4096)).trim();
        if (isSessionToken(persisted)) config.sessionToken = persisted;
      } catch { /* first start */ }
    }
    // One-time migration from builds that put the sidecar below the mutable
    // title-derived app data directory.  Never send an unvalidated legacy
    // value to the loopback Host; a malformed/missing token remains a
    // fail-closed startup condition rather than an unauthenticated shutdown.
    if (!config.sessionToken) {
      try {
        const legacyDataDirectory = await app.dataDir();
        const legacyPath = joinWindowsPath(legacyDataDirectory, 'fan-host\\YeManFanHost.session');
        if (legacyPath.toLowerCase() !== config.sessionTokenPath.toLowerCase()) {
          const legacy = (await fs.readTextFile(legacyPath, 4096)).trim();
          if (isSessionToken(legacy)) {
            config.sessionToken = legacy;
            await fs.writeTextFileAtomic(config.sessionTokenPath, legacy);
          }
        }
      } catch { /* no legacy sidecar */ }
    }
    // 203/Fan b141（CP-05）：凭据来源证据（**非秘密**）——bridge 实际用的是哪份 sidecar、
    // 格式是否成立、版本标识是什么。绝不写凭据值/前缀。
    fanDiagnosticLog('lifecycle.credential-source', {
      sessionTokenPath: config.sessionTokenPath,
      tokenPresent: isSessionToken(config.sessionToken),
      tokenFingerprint: await sessionTokenFingerprint(config.sessionToken),
    });
    // WebView2 can recreate the renderer during S3 wake while the native
    // process and YeManFanHost are still in the HC Suspended session. Adopt
    // that authenticated Host instead of Close/shutdown/relaunch, which races
    // OEM/ACPI and previously fault-locked the Fan route out of the sidebar.
    const adopted = await this.tryAdoptAuthenticatedResidentHost(config);
    if (adopted) {
      config.confirmationToken = config.sessionToken;
      fanDiagnosticLog('lifecycle.start-stage', {
        stage: 'resident-host-adopted',
        pid: adopted.pid,
      });
      this.onStage?.('resident-host-adopted');
      return adopted;
    }
    // Recover an exact resident Host before touching the immutable payload.
    // The ACL installer quarantines stale files and rewrites payload ACLs;
    // doing that while an older Host still owns HC/ACPI/HID can race its
    // loaded assembly or leave an update half-applied while hardware is live.
    // The recovery path is authenticated when a session sidecar exists and
    // falls back to the exact-image legacy path only for pre-token Hosts.
    await this.recoverPreviousHostBeforePayloadMutation(config);
    // recoverPreviousHost must not Close a live HC session. If it returned
    // because the Suspended/Ready Host is still the SystemPending owner,
    // adopt it here instead of mutating payload ACLs or launching a second Host.
    const adoptedAfterRecover = await this.tryAdoptAuthenticatedResidentHost(config);
    if (adoptedAfterRecover) {
      config.confirmationToken = config.sessionToken;
      fanDiagnosticLog('lifecycle.start-stage', {
        stage: 'resident-host-adopted-after-recover',
        pid: adoptedAfterRecover.pid,
      });
      this.onStage?.('resident-host-adopted');
      return adoptedAfterRecover;
    }
    this.onStage?.('resident-host-recovered');
    fanDiagnosticLog('lifecycle.start-stage', { stage: 'resident-host-recovered' });
    await this.ensureFanHostDeployment(config, fanStateDirectory);
    this.onStage?.('deployment-record-verified');
    fanDiagnosticLog('lifecycle.start-stage', { stage: 'deployment-record-verified', runtimeRoot: config.hcRuntimeDirectory });
    if (!config.sessionToken) {
      // 203/Fan b141（FAN925B §4.2 / CP-04）：常驻 Host **健在**时绝不轮换活跃凭据。
      // 在这里生成新令牌会覆盖运行中的 Host 已加载的 sidecar ⇒ native 发新值而 Host 仍持
      // 旧值 ⇒ /api/parent-exit 必然 401（§2 C2）。换代必须先让旧 owner 结算；否则
      // fail-closed，把它保留为唯一恢复 owner（不杀进程、不重写 sidecar、不起第二 owner）。
      const residentBeforeRotate = await proc.findExact(config.hostExecutable)
        .catch(() => ({ found: false, pid: 0 }));
      if (residentBeforeRotate.found) {
        fanDiagnosticLog('lifecycle.credential-rotation-refused', {
          pid: residentBeforeRotate.pid,
          reason: 'resident-host-alive',
          sessionTokenPath: config.sessionTokenPath,
          policy: 'fail-closed;no-credential-rotation;no-second-owner',
        });
        throw new Error(`常驻 Fan Host 仍健在 (pid=${residentBeforeRotate.pid})，拒绝轮换会话凭据`);
      }
      config.sessionToken = createSessionToken();
      await fs.writeTextFileAtomic(config.sessionTokenPath, config.sessionToken);
    }
    // Atomic-write success is not enough for a recovery capability: verify the
    // exact bytes that the next process and native exit fallback will read.
    const persistedToken = (await fs.readTextFile(config.sessionTokenPath, 4096)).trim();
    if (!isSessionToken(persistedToken) || persistedToken.toLowerCase() !== config.sessionToken.toLowerCase()) {
      throw new Error(`Fan Host 会话令牌未可靠落盘：${config.sessionTokenPath}`);
    }
    // V3 keeps HandheldCompanion.dll as the frozen HC compatibility bridge,
    // while YeManFanHcAdapter.dll is the actual source-rebuilt fan route
    // assembly. Select it only after the authoritative V3 manifest has passed;
    // legacy packages continue using their original HC entry point.
    const v3AdapterPath = joinWindowsPath(config.hostDirectory, FAN_HOST_V3_ADAPTER_FILE);
    if (await fs.exists(v3AdapterPath)) config.hcAssemblyPath = v3AdapterPath;
    // YeManFanHost accepts real writes only when this invocation proves
    // possession of the random session token. This value never enters HTTP
    // payloads and is regenerated whenever the protected sidecar is absent.
    config.confirmationToken = config.sessionToken;
    // Keep this authenticated probe for the narrow case where a listener
    // appeared between the preflight and payload verification. It is
    // idempotent and still verifies exact executable identity first.
    await this.recoverPreviousHost(config);
    const parentPid = await app.pid().catch(() => 0);
    const launched = await shell.hidden(config.hostExecutable, [
      '--real-backend',
      '--hc-assembly',
      config.hcAssemblyPath,
      '--hc-runtime-root',
      config.hcRuntimeDirectory,
      ...(parentPid > 0 ? ['--parent-pid', String(parentPid)] : ['--parent-process', 'YeManCC']),
      '--protocol-version',
      String(config.protocolVersion),
      '--session-token-file',
      config.sessionTokenPath,
      ...(config.allowHardwareWrites ? [
        '--allow-hardware-writes',
        '--authorization',
        config.authorizationPath,
        '--confirm',
        config.confirmationToken,
      ] : []),
    ]);
    if (!launched.ok || !launched.pid) throw new Error('Fan Host 启动失败');
    const ownedProcess = await this.captureHostProcess(config, launched.pid);
    this.onStage?.('host-process-launched');
    fanDiagnosticLog('lifecycle.start-stage', { stage: 'host-process-launched', pid: launched.pid });
    try {
      await this.waitForReady(config);
    } catch (error) {
      // The lifecycle has not received a process handle yet. Reclaim the
      // exact child here or a failed readiness probe would leak a listener
      // that blocks the next start and may retain HC ownership.
      await this.stop(ownedProcess).catch(() => {});
      throw error;
    }
    return ownedProcess;
  }

  private async startAiMock(config: FanHostConfig, session: AiFanMockSession): Promise<FanHostProcess> {
    // R11 T-05 / R12: every explicit failure path in this AI-only start must leave a durable
    // receipt. `fanLifecycleEvidence` is the ungated, whitelist-projected channel, so a start
    // failure is still readable when detailed logging is off (default). Only finite own codes
    // are recorded — never a raw exception, handshake, environment or command line.
    const fail = (code: string): never => {
      fanLifecycleEvidence('ai.mock-start-failed', { reason: code });
      throw new Error(code);
    };
    config.aiMockSession = session;
    config.allowHardwareWrites = false;
    config.sessionTokenPath = session.stateDirectory + '\\YeManFanHost.session';
    // Do not migrate, restore, rotate or adopt the real production namespace.
    if (!(await fs.exists(config.hostExecutable))) fail('AI_FAN_HOST_NOT_FOUND');
    let token = '';
    try { token = (await fs.readTextFile(config.sessionTokenPath, 4096)).trim(); } catch { /* first AI start */ }
    const resident = await proc.findExact(config.hostExecutable);
    if (resident.found) {
      if (resident.pid !== session.hostPid || !isSessionToken(token)) fail('AI_FAN_RESIDENT_NOT_OWNED');
      const response = await http.request(config.baseUrl + '/api/state', {
        method: 'GET', headers: { 'X-YeMan-Fan-Session': token }, timeoutMs: 1500,
      });
      const payload = parseJson<{ ok?: boolean; state?: FanState }>(response.body);
      if (response.status !== 200 || payload.ok !== true || !payload.state || !aiFanMockStateAllowed(payload.state)) fail('AI_FAN_RESIDENT_NOT_ZERO_WRITE_MOCK');
      config.sessionToken = token;
      this.aiOwnedPids.add(resident.pid);
      return this.captureHostProcess(config, resident.pid, true).catch(() => fail('AI_FAN_HOST_IDENTITY_UNPROVEN'));
    }
    // Read-only preflight. Any HTTP response means occupied; only the precise
    // cannot-connect error is evidence of no listener. Timeout is not absence.
    //
    // R11 T-06: the invoke layer budgets `timeoutMs + 1000` wall clock. A closed
    // loopback port does not fail instantly — Windows retries the SYN once, so
    // WinHTTP only returns 12029 after ~2 s. A 1000 ms request budget therefore
    // expired first and the timeout was misread as "unknown", which blocked the
    // AI mock host from ever launching. Budget must exceed that OS connect-failure
    // latency so the real 12029 evidence can arrive.
    let occupied = false;
    try { await http.request(config.baseUrl + '/api/health', { method: 'GET', timeoutMs: 5000 }); occupied = true; }
    catch (error) { if (!/WinHTTP 12029[,)]/.test(String(error instanceof Error ? error.message : error))) fail('AI_FAN_PORT_UNKNOWN'); }
    if (occupied) fail('AI_FAN_PORT_OCCUPIED');
    if (!(await fs.mkdir(session.stateDirectory))) fail('AI_FAN_SESSION_DIRECTORY_FAILED');
    if (!isSessionToken(token)) { token = createSessionToken(); await fs.writeTextFileAtomic(config.sessionTokenPath, token); }
    const persisted = (await fs.readTextFile(config.sessionTokenPath, 4096)).trim();
    if (persisted !== token) fail('AI_FAN_SESSION_TOKEN_NOT_PERSISTED');
    config.sessionToken = token;
    const launched = await shell.hidden(config.hostExecutable, aiFanMockArguments(session, config.sessionTokenPath));
    if (!launched.ok || !launched.pid) fail('AI_FAN_MOCK_LAUNCH_FAILED');
    this.aiOwnedPids.add(launched.pid);
    const ownedProcess = await this.captureHostProcess(config, launched.pid).catch(() => fail('AI_FAN_HOST_IDENTITY_UNPROVEN'));
    try { await this.waitForReady(config); }
    catch {
      // R12 T-05: converge to a finite own code. `waitForReady`'s raw health/transport/
      // exception text must not be written to diagnostics nor re-thrown as the error text.
      fanLifecycleEvidence('ai.mock-start-failed', { reason: 'AI_FAN_MOCK_NOT_READY' });
      await this.stop(ownedProcess).catch(() => {});
      throw new Error('AI_FAN_MOCK_NOT_READY');
    }
    return ownedProcess;
  }

  /**
   * Reattach a live authenticated Host after UI/WebView recreation. Close and
   * relaunch remain the leftover-process path; a Suspended/Ready Host that
   * still answers with the current session token is the same HC owner.
   */
  private async tryAdoptAuthenticatedResidentHost(config: FanHostConfig): Promise<FanHostProcess | null> {
    if (!isSessionToken(config.sessionToken)) return null;
    let stateResponse: { status: number; body: string } | undefined;
    const requestState = async (timeoutMs: number) => http.request(`${config.baseUrl}/api/state`, {
      method: 'GET',
      headers: { 'X-YeMan-Fan-Session': config.sessionToken },
      timeoutMs,
    });
    try {
      stateResponse = await requestState(2000);
    } catch {
      const resident = await proc.findExact(config.hostExecutable).catch(() => ({ found: false, pid: 0 }));
      if (!resident.found) return null;
      try {
        stateResponse = await requestState(3000);
      } catch {
        return null;
      }
    }
    if (!stateResponse || stateResponse.status === 401) {
      // 203/Fan b141（FAN925B §4.3 / CP-05）：401 只说明"这一次尝试未获接受"，**不是**
      // "常驻 Host 是旧代"的判据，也**不是**走无认证 Close / 重写 sidecar / 杀端口 owner /
      // 起第二 owner 的许可。留痕（非秘密）后按"不可 adopt"返回。
      fanDiagnosticLog('lifecycle.adopt-auth-rejected', {
        status: stateResponse?.status ?? 0,
        sessionTokenPath: config.sessionTokenPath,
        tokenFingerprint: await sessionTokenFingerprint(config.sessionToken),
        policy: 'no-unauthenticated-close;no-sidecar-rewrite;no-second-owner;no-kill-on-401',
      });
      return null;
    }
    if (stateResponse.status < 200 || stateResponse.status >= 300) return null;
    const remoteState = readRemoteFanStateName(stateResponse.body);
    if (!isLiveResidentFanState(remoteState)) return null;
    const pid = await this.resolveAdoptableHostPid(config);
    if (pid < 0) {
      throw new Error('旧 Fan Host 健康响应来自非当前 YeManFanHost，已阻止生命周期请求');
    }
    if (pid === 0) {
      throw new Error('旧 Fan Host 健康响应但未找到可验证的监听进程，已阻止生命周期请求');
    }
    fanDiagnosticLog('lifecycle.start-stage', {
      stage: 'resident-host-adopted-state',
      pid,
      remoteState,
    });
    return this.captureHostProcess(config, pid, true);
  }

  /**
   * After sleep, QueryFullProcessImageName can fail while the exact Host image
   * is still resident. Treat an unresolved identity as unknown, not foreign.
   */
  private async resolveAdoptableHostPid(config: FanHostConfig): Promise<number> {
    const owner = await this.findExactHostOwner(config);
    if (owner > 0) return owner;
    const resident = await proc.findExact(config.hostExecutable).catch(() => ({ found: false, pid: 0 }));
    if (owner < 0) return -1;
    return resident.found ? resident.pid : 0;
  }

  /**
   * Detect a resident exact-image Host before the ACL installer can move or
   * quarantine anything beside it. A missing sidecar means this is an old
   * pre-token Host; only the exact configured executable may receive the
   * unauthenticated compatibility close.
   */
  private async recoverPreviousHostBeforePayloadMutation(config: FanHostConfig): Promise<void> {
    const resident = await proc.findExact(config.hostExecutable).catch(() => ({ found: false, pid: 0 }));
    if (!resident.found) return;
    if (config.sessionToken) {
      await this.recoverPreviousHost(config);
      return;
    }
    // A missing sidecar is not proof that the resident Host is a pre-token
    // build: the sidecar may have been deleted, moved by an updater, or made
    // unreadable while the authenticated Host still owns HC/ACPI/HID. Probe
    // the unauthenticated health contract first. Only a successful protocol-2
    // health response identifies a legacy Host; a 401 is an authenticated
    // current Host and must never receive unauthenticated close/shutdown.
    let legacyHealth: { status: number; body: string } | undefined;
    try {
      legacyHealth = await http.request(`${config.baseUrl}/health`, { method: 'GET' });
    } catch {
      throw new Error(`旧 Fan Host 会话文件缺失且健康端点不可用 (pid=${resident.pid})，已阻止无认证接管`);
    }
    if (isLegacyUnauthenticatedFanHostHealth(legacyHealth, config.protocolVersion)) {
      await this.recoverLegacyUnauthenticatedHost(config);
      return;
    }
    throw new Error(
      legacyHealth.status === 401
        ? `旧 Fan Host 会话文件缺失但端点要求会话令牌 (pid=${resident.pid})，已拒绝无认证关闭请求`
        : `旧 Fan Host 会话文件缺失且健康协议不匹配 (pid=${resident.pid})，已阻止生命周期请求`,
    );
  }

  /**
   * FAN-927 §2：**部署边界的唯一验证入口**。正常开启只做两件小事：① 对两个小清单取摘要；
   * ② 匹配一份小部署记录。任一不匹配（含记录缺失、包代描述变更）才走一次完整验证。
   *
   * 这里就是"便携包/旧安装没有记录时，首次明确开启只做一次快速批量初始化"的落点：验证由
   * 安装脚本在**同一进程内**逐条核对 manifest（防逃逸/存在性/SHA-256），成功后由该脚本写出
   * 部署记录，之后每次开启即走快路径。它**不是**每次启动都重做，也不要求用户反复手动修复。
   */
  private async ensureFanHostDeployment(config: FanHostConfig, fanStateDirectory: string): Promise<void> {
    // 生产 I/O：真实 fs + 本类的安装器入口。判定逻辑本身在 resolveFanHostDeployment
    // （可注入 I/O，自测用它按**实际调用次数**验收"记录命中即零重复验证"）。
    await resolveFanHostDeployment(config, fanStateDirectory, {
      exists: (path) => fs.exists(path),
      readTextFile: (path, maxBytes) => fs.readTextFile(path, maxBytes),
      sha256File: (path) => fs.sha256File(path),
      runInstaller: (target, stateDirectory) => this.installAndVerifyPayload(target, stateDirectory),
    });
  }

  /**
   * FAN-927 §2.1：部署边界（新装/更新/回滚/修复/无记录首次开启）的完整验证。
   *
   * 校验由 `install-fan-host-payload.ps1` 在**同一进程内**完成（manifest 每条目防逃逸 +
   * 存在性 + SHA-256，以及 HC runtime manifest），成功后该脚本写出一份小"部署记录"。
   * 2026-09-12 用户裁决起已**不再**施加不可变 ACL/quarantine/交互确认，禁止加回；
   * 逐文件 `certutil` 子进程与 Host 侧重复全扫在 FAN-927 中一并移除。
   */
  private async installAndVerifyPayload(config: FanHostConfig, fanStateDirectory: string): Promise<void> {
    const installer = await ensureFanHostInstaller(config.hostDirectory, fs);
    const result = await shell.run('powershell.exe', [
      '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
      '-File', installer,
      '-PayloadDirectory', config.hostDirectory,
      '-StateDirectory', fanStateDirectory,
    ], 60000);
    if (result.exitCode !== 0 || !/FAN_HOST_(ACL|PAYLOAD)_OK:/i.test(`${result.stdout}\n${result.stderr}`)) {
      const detail = `${result.stderr || result.stdout}`.replace(/[\r\n]+/g, ' ').trim().slice(0, 240);
      throw new Error(`Fan Host 权限部署失败${detail ? `: ${detail}` : ''}`);
    }
  }

  /**
   * Process creation is not the same as listener readiness. Probe the
   * authenticated health endpoint before the first handshake so a slow .NET
   * startup cannot turn a valid device into a hidden Fan route.
   */
  private async waitForReady(config: FanHostConfig): Promise<void> {
    const deadline = Date.now() + 5000;
    let lastError = 'Fan Host health endpoint 未就绪';
    while (Date.now() < deadline) {
      try {
        const response = await http.request(`${config.baseUrl}/health`, {
          method: 'GET',
          headers: { 'X-YeMan-Fan-Session': config.sessionToken },
          timeoutMs: 750,
        });
        if (response.status >= 200 && response.status < 300) {
          const health = JSON.parse(response.body || '{}');
          if (health?.host === 'YeManFanHost' && health?.protocolVersion === config.protocolVersion) return;
          lastError = 'Fan Host health 响应协议不匹配';
        } else {
          lastError = `Fan Host health 返回 ${response.status}`;
        }
      } catch (error) {
        lastError = error instanceof Error ? error.message : String(error);
      }
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    throw new Error(lastError);
  }

  /**
   * A previous UI process can disappear before its async Vue unmount handler
   * finishes. Reclaim the loopback port through the old Host's own restore
   * path before starting a new instance; never kill an arbitrary process.
   */
  private async recoverPreviousHost(config: FanHostConfig): Promise<void> {
    if (!config.sessionToken) {
      try { config.sessionToken = (await fs.readTextFile(config.sessionTokenPath, 4096)).trim(); } catch { return; }
    }
    let health: any;
    try {
      const response = await http.request(`${config.baseUrl}/health`, { method: 'GET', headers: { 'X-YeMan-Fan-Session': config.sessionToken } });
      if (response.status === 401) {
        // A 401 with our sidecar token has two meanings: an old pre-token Host
        // (which will answer /health without a header), or a token mismatch
        // with a current authenticated Host. Never guess the former. Probe
        // the same exact loopback endpoint without credentials first; only a
        // successful protocol-2 response proves that the legacy unauthenticated
        // migration path is applicable. A second 401 is an authentication
        // boundary, not permission to send more unauthenticated close calls.
        let legacyHealth: { status: number; body: string } | undefined;
        try {
          legacyHealth = await http.request(`${config.baseUrl}/health`, { method: 'GET' });
        } catch { /* an unreachable listener is handled by the resident check below */ }
        if (isLegacyUnauthenticatedFanHostHealth(legacyHealth, config.protocolVersion)) {
          // One-time migration path for a pre-session-token Host. It is
          // allowed only after the loopback port is proven to belong to the
          // exact configured executable; an arbitrary local service is never
          // sent a close/shutdown request.
          await this.recoverLegacyUnauthenticatedHost(config);
          return;
        }
        const exactResident = await proc.findExact(config.hostExecutable).catch(() => ({ found: false, pid: 0 }));
        if (exactResident.found) {
          throw new Error(`旧 Fan Host 会话令牌不匹配 (pid=${exactResident.pid})，已拒绝无认证关闭请求；保留原 Host 作为唯一恢复所有者`);
        }
        return;
      }
      if (response.status < 200 || response.status >= 300) return;
      health = JSON.parse(response.body || '{}');
    } catch (error) {
      if (error instanceof Error && error.message.startsWith('旧 Fan Host')) throw error;
      // The listener can disappear while the resident Host still owns an
      // HC/EC session. Detect the exact executable independently of HTTP and
      // block a second hardware Host until the resident process is reachable
      // or an operator performs the documented recovery procedure.
      const resident = await proc.findExact(config.hostExecutable).catch(() => ({ found: false, pid: 0 }));
      if (resident.found) {
        throw new Error(`旧 Fan Host 进程仍在运行但 native HTTP 不可用 (pid=${resident.pid})，已阻止重复接管`);
      }
      return;
    }
    if (health?.host !== 'YeManFanHost' || health?.protocolVersion !== config.protocolVersion) return;
    // A matching JSON health payload is not process identity. Before sending
    // any restore/close/shutdown request, prove that 127.0.0.1:8765 belongs
    // to the exact configured YeManFanHost executable. This prevents an
    // unrelated local service (or a stale test Host) from receiving lifecycle
    // commands during application startup recovery.
    const verifiedOwner = await this.findExactHostOwner(config);
    const exactResident = await proc.findExact(config.hostExecutable).catch(() => ({ found: false, pid: 0 }));
    if (verifiedOwner <= 0) {
      // Sleep can make image-name queries fail. An exact YeManFanHost image is
      // still the HC session owner; never classify it as foreign and never
      // send a second CurrentDevice.Close. The launcher adopts instead.
      if (exactResident.found) {
        fanDiagnosticLog('lifecycle.start-stage', {
          stage: 'resident-host-live-unresolved-identity',
          pid: exactResident.pid,
          owner: verifiedOwner,
        });
        return;
      }
      throw new Error(verifiedOwner < 0
        ? '旧 Fan Host 健康响应来自非当前 YeManFanHost，已阻止生命周期请求'
        : '旧 Fan Host 健康响应但未找到可验证的监听进程，已阻止生命周期请求');
    }
    try {
      const stateResponse = await http.request(`${config.baseUrl}/api/state`, {
        method: 'GET',
        headers: { 'X-YeMan-Fan-Session': config.sessionToken },
        timeoutMs: 2000,
      });
      if (stateResponse.status >= 200 && stateResponse.status < 300) {
        const remoteState = readRemoteFanStateName(stateResponse.body);
        if (isLiveResidentFanState(remoteState)) {
          fanDiagnosticLog('lifecycle.start-stage', {
            stage: 'resident-host-live-session-kept',
            pid: verifiedOwner,
            remoteState,
          });
          return;
        }
      }
    } catch { /* leftover recovery still requires a Stopped Host */ }
    // Capture the exact managed instance identity while it is still alive so
    // the exit proof below is bound to the instance we verified; a reused PID
    // cannot then redirect the termination to a different process.
    const previousInstance = await this.resolveExactHostInstance(config, verifiedOwner);
    let previousCloseUnresolved = false;
    try {
      // Match HC Window_Closed ownership: send exactly one Close request to
      // the resident Host. A lost/409/5xx response is ambiguous, so recovery
      // observes the original Host operation instead of posting a second
      // CurrentDevice.Close into the same ACPI/HID lifecycle.
      let closeResponse: { status: number; body: string } | undefined;
      let closeResult: any;
      try {
        closeResponse = await http.request(`${config.baseUrl}/api/close`, {
          method: 'POST', headers: { 'Content-Type': 'application/json', 'X-YeMan-Fan-Session': config.sessionToken }, body: '{}', timeoutMs: 45000,
        });
        closeResult = JSON.parse(closeResponse.body || '{}');
      } catch { /* recover the original Host operation by observation only */ }
      if (!closeResponse || closeResponse.status < 200 || closeResponse.status >= 300 ||
          closeResult?.state?.hcCloseCleanupPending === true) {
        closeResult = { state: await this.waitForRemoteHcCloseCleanup(config) };
      }
      const closedState = closeResult?.state;
      const liveWrites = closedState?.hardwareWritesEnabled === true || closedState?.hardwareWrites === true;
      if (closedState?.state !== 'Stopped' || closedState?.unknownState === true ||
          closedState?.hcCloseCleanupPending === true || liveWrites ||
          hasExplicitIncompleteHcCloseEvidence(closedState, true) ||
          closedState?.openCalled === true || closedState?.openEventsCalled === true ||
          (closedState?.hardwareWritesObserved === true && !hasAcceptedStoppedHcCloseEvidence(closedState))) {
        // An open/active HC session is a genuine mutual-exclusion block: never
        // terminate a live writer and never start a second one. Everything
        // else here (extra OEM evidence unknown, Close still in flight,
        // transport failure) is only "unresolved" and is settled by the
        // bounded exact-instance exit backstop below, not collapsed into this
        // terminal state.
        if (liveWrites || closedState?.openCalled === true || closedState?.openEventsCalled === true) {
          throw new Error('旧 Host 未完成恢复或 HC Close 清理');
        }
        previousCloseUnresolved = true;
      }
      if (!previousCloseUnresolved) {
        const shutdownResponse = await http.request(`${config.baseUrl}/api/shutdown`, {
          method: 'POST', headers: { 'Content-Type': 'application/json', 'X-YeMan-Fan-Session': config.sessionToken }, body: '{}',
        });
        if (shutdownResponse.status >= 300 && shutdownResponse.status !== 404) {
          throw new Error(`旧 Host shutdown 返回 ${shutdownResponse.status}`);
        }
      }
    } catch (error) {
      throw new Error(`旧 Fan Host 无法安全退出：${error instanceof Error ? error.message : String(error)}`);
    }
    if (!previousInstance) {
      throw new Error('旧 Fan Host 监听进程身份不可确证，已阻止新的硬件接管');
    }
    // Old HTTP/port disappearance is not exit proof. The previous instance must
    // be confirmed gone through its verified identity (bound PID +
    // processCreated + path) before a new writer may be created.
    await this.confirmExactPreviousHostExit(previousInstance);
  }

  private async captureHostProcess(config: FanHostConfig, pid: number, adopted = false): Promise<FanHostProcess> {
    const instance = await this.resolveExactHostInstance(config, pid);
    // Never publish an owner that can never settle its exit. An unreadable
    // identity rejects this startup; a later explicit start may authenticate
    // and capture the resident again. No unbound termination is authorized.
    if (!instance) throw new Error('Fan Host 实例身份暂不可确认，请重试；保留现有宿主，不执行猜测终止');
    return { pid, executable: config.hostExecutable, processCreated: instance.processCreated,
      ...(adopted ? { adopted: true } : {}) };
  }

  /** Resolve the exact YMCC-managed Host instance bound to `pid`, or null when
   * identity cannot be proven (invalid handle, missing creation time, or a
   * different executable). Callers must never guess on a null. */
  private async resolveExactHostInstance(
    config: FanHostConfig,
    pid: number,
  ): Promise<{ pid: number; processCreated: string; path: string } | null> {
    if (pid <= 0) return null;
    const identity = await proc.identity(pid).catch(() => null);
    const created = typeof identity?.processCreated === 'string' ? identity.processCreated.trim() : '';
    if (identity?.valid !== true || !created || created === '0') return null;
    if (!identity.path || identity.path.toLowerCase() !== config.hostExecutable.toLowerCase()) return null;
    return { pid, processCreated: created, path: identity.path };
  }

  /** Wait a bounded window for a graceful exit, then confirm the exact instance
   * is gone through `process.terminateExact` (PID + processCreated + path bound
   * to one verified native handle). Returns only on confirmed exit; otherwise
   * throws a specific block so the caller never spawns a concurrent writer. */
  private async confirmExactPreviousHostExit(
    instance: { pid: number; processCreated: string; path: string },
  ): Promise<void> {
    const deadline = Date.now() + FAN_PREVIOUS_HOST_EXIT_OBSERVE_MS;
    while (Date.now() < deadline) {
      const identity = await proc.identity(instance.pid).catch(() => null);
      const created = typeof identity?.processCreated === 'string' ? identity.processCreated.trim() : '';
      if (identity?.valid === true && created && created !== '0' && created !== instance.processCreated) {
        // The PID now belongs to a different process: our bound instance exited.
        return;
      }
      // Inconclusive here (handle gone or unreadable); settle with the
      // handle-bound termination proof below instead of guessing from PID.
      if (identity?.valid !== true) break;
      await new Promise<void>((resolve) => setTimeout(resolve, 100));
    }
    const result = await proc
      .terminateExact(instance.pid, instance.processCreated, instance.path)
      .catch(() => null);
    if (result?.exited === true) return;
    if (result?.reason === 'not-found') return;
    if (result?.reason === 'identity-mismatch') {
      // Native uses this result both for PID reuse and for an unreadable
      // creation time/image. Only a valid different creation time proves exit.
      const current = await proc.identity(instance.pid).catch(() => null);
      const created = typeof current?.processCreated === 'string' ? current.processCreated.trim() : '';
      if (current?.valid === true && created && created !== '0' && created !== instance.processCreated) return;
    }
    throw new Error(`旧 Fan Host 实例无法确认退出（${result?.reason ?? 'terminate-unavailable'}），已阻止新的硬件接管`);
  }

  /** Return the PID only when the configured loopback port is owned by the
   * exact Host image.  -1 means another process owns the port; 0 means the
   * listener could not be resolved. */
  private async findExactHostOwner(config: FanHostConfig): Promise<number> {
    const port = Number(new URL(config.baseUrl).port || 80);
    const netstat = await shell.run('netstat.exe', ['-ano', '-p', 'tcp'], 10000);
    const escapedPort = String(port).replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    const ownerMatch = `${netstat.stdout}\n${netstat.stderr}`.match(
      new RegExp(`127\\.0\\.0\\.1:${escapedPort}\\s+[^\\r\\n]*LISTENING\\s+(\\d+)`, 'i'),
    );
    const ownerPid = ownerMatch ? Number(ownerMatch[1]) : 0;
    if (ownerPid <= 0) return 0;
    const identity = await proc.identity(ownerPid).catch(() => null);
    // Sleep/resume can make OpenProcess/image-name queries fail even for the
    // exact Host. That is unresolved ownership, not proof of a foreign listener.
    if (identity?.valid !== true) return 0;
    const exactPath = identity.path && identity.path.toLowerCase() === config.hostExecutable.toLowerCase();
    return exactPath ? ownerPid : -1;
  }

  private async recoverLegacyUnauthenticatedHost(config: FanHostConfig): Promise<void> {
    const port = Number(new URL(config.baseUrl).port || 80);
    const netstat = await shell.run('netstat.exe', ['-ano', '-p', 'tcp'], 10000);
    const escapedPort = String(port).replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    const ownerMatch = `${netstat.stdout}\n${netstat.stderr}`.match(
      new RegExp(`127\\.0\\.0\\.1:${escapedPort}\\s+[^\\r\\n]*LISTENING\\s+(\\d+)`, 'i'),
    );
    const ownerPid = ownerMatch ? Number(ownerMatch[1]) : 0;
    if (ownerPid <= 0) {
      // The caller already proved that an exact legacy Host image is still
      // resident.  A missing listener is therefore ambiguous, not proof
      // that the HC/ACPI/HID session is idle: the old process may be between
      // HttpListener instances or may have lost its socket while retaining
      // the device session.  Never mutate/quarantine the immutable payload
      // in that window; keep the old process as the only recovery owner.
      throw new Error('旧 Fan Host 进程仍在运行但没有可验证监听端口，已阻止修改载荷');
    }
    const identity = await proc.identity(ownerPid).catch(() => null);
    const exactPath = identity?.path && identity.path.toLowerCase() === config.hostExecutable.toLowerCase();
    if (identity?.valid !== true || !exactPath) {
      throw new Error('旧 Fan Host 会话令牌失效且端口不属于当前 YeManFanHost，已阻止接管');
    }
    const closeResponse = await http.request(`${config.baseUrl}/api/close`, {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{}', timeoutMs: 45000,
    });
    if (closeResponse.status < 200 || closeResponse.status >= 300) {
      throw new Error(`旧 Fan Host legacy close 返回 ${closeResponse.status}`);
    }
    const closeResult = JSON.parse(closeResponse.body || '{}');
    const closedState = closeResult?.state;
    const liveWrites = closedState?.hardwareWritesEnabled === true || closedState?.hardwareWrites === true;
    if (closedState?.state !== 'Stopped' || closedState?.unknownState === true ||
        closedState?.hcCloseCleanupPending === true || liveWrites ||
        hasExplicitIncompleteHcCloseEvidence(closedState, true) ||
        closedState?.openCalled === true || closedState?.openEventsCalled === true ||
        (closedState?.hardwareWritesObserved === true && !hasAcceptedStoppedHcCloseEvidence(closedState))) {
      throw new Error('旧 Fan Host legacy 未完成恢复或 HC Close 清理');
    }
    const shutdownResponse = await http.request(`${config.baseUrl}/api/shutdown`, {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{}',
    });
    if (shutdownResponse.status >= 300 && shutdownResponse.status !== 404) {
      throw new Error(`旧 Fan Host legacy shutdown 返回 ${shutdownResponse.status}`);
    }
    const deadline = Date.now() + 4000;
    while (Date.now() < deadline) {
      try {
        const response = await http.request(`${config.baseUrl}/health`, { method: 'GET' });
        if (response.status >= 200 && response.status < 300) {
          await new Promise((resolve) => setTimeout(resolve, 100));
          continue;
        }
      } catch { return; }
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    throw new Error('旧 Fan Host legacy shutdown 后端口仍被占用，已阻止新的硬件接管');
  }

  private async waitForRemoteHcCloseCleanup(config: FanHostConfig): Promise<FanState> {
    const deadline = Date.now() + 16000;
    while (Date.now() < deadline) {
      try {
        const response = await http.request(`${config.baseUrl}/api/state`, {
          method: 'GET',
          headers: config.sessionToken ? { 'X-YeMan-Fan-Session': config.sessionToken } : {},
          timeoutMs: 750,
        });
        const remote = JSON.parse(response.body || '{}')?.state;
        // A successful HTTP response with an empty/malformed body is not a
        // lifecycle acknowledgement. Wait for a real Host state object so a
        // proxy/error page cannot make the next /api/close concurrent with
        // the original request.
        if (response.status >= 200 && response.status < 300 && remote &&
            typeof remote === 'object' && remote.hcCloseCleanupPending !== true) return remote as FanState;
      } catch { /* preserve the old Host and continue polling */ }
      await new Promise<void>((resolve) => setTimeout(resolve, 250));
    }
    throw new Error('旧 Host HC Close 资源清理在等待窗口内未完成');
  }

  async stop(process: FanHostProcess): Promise<void> {
    if (this.aiOwnedPids.has(process.pid)) {
      // Shutdown already asked the owned mock to exit. Give that exact native
      // process handle a bounded grace window, not a global image-name kill.
      const deadline = Date.now() + 7000;
      while (Date.now() < deadline) {
        const owned = parseAiFanMockSession(await app.aiFanMockSession());
        if (!owned || owned.hostPid === 0) { this.aiOwnedPids.delete(process.pid); return; }
        if (owned.hostPid !== process.pid) throw new Error('AI_FAN_STOP_IDENTITY_CHANGED');
        await new Promise<void>((resolve) => setTimeout(resolve, 50));
      }
    }
    if (!process.processCreated || process.processCreated === '0') {
      throw new Error('Fan Host 启动时的实例身份缺失，保留旧宿主以便重试，禁止按 PID 猜测终止');
    }
    await this.confirmExactPreviousHostExit({
      pid: process.pid, processCreated: process.processCreated, path: process.executable,
    });
    this.aiOwnedPids.delete(process.pid);
  }
}

export interface FanDeviceGateResult {
  allowed: boolean;
  /** The route is mapped and may be shown, but writes are blocked until the
   * Host reports a route-specific restore/readback capability. */
  writeReady: boolean;
  deviceFamily?: 'gpd-win5' | 'rog-xbox' | 'hc-mapped-fan';
  reason: string;
}

const DEVICE_PROFILES = [
  {
    family: 'gpd-win5' as const,
    factoryTypes: new Set(['HandheldCompanion.Devices.GPDWin5']),
    manufacturers: new Set(['gpd']),
  },
  {
    family: 'rog-xbox' as const,
    factoryTypes: new Set([
      'HandheldCompanion.Devices.XboxROGAlly',
      'HandheldCompanion.Devices.XboxROGAllyX',
    ]),
    // The HC factory type is the authoritative route gate. Manufacturer/model
    // metadata is checked when present but is not hard-coded because ROG Xbox
    // firmware reports different product strings across BIOS revisions.
    manufacturers: new Set<string>(),
  },
];

function normalizedIdentity(identity: Record<string, unknown> | null | undefined): Record<string, string> {
  const source = identity ?? {};
  const out: Record<string, string> = {};
  // Host readback uses HC field names (`ManufacturerName`, `SystemModel`,
  // `ProductName`, `Version`), while persisted WMI/preflight data uses the
  // short lowercase names. Normalize both forms or a restart/recovery would
  // incorrectly turn a previously accepted device into “风扇不支持”.
  const aliases: Record<string, string[]> = {
    manufacturer: ['manufacturer', 'ManufacturerName'],
    model: ['model', 'SystemModel'],
    product: ['product', 'ProductName'],
    bios: ['bios', 'Version'],
    ecRevision: ['ecRevision', 'EcRevision', 'ECRevision'],
  };
  for (const [key, names] of Object.entries(aliases)) {
    const value = names.map((name) => source[name]).find((candidate) => typeof candidate === 'string' && candidate.trim());
    if (typeof value === 'string' && value.trim()) out[key] = value.trim().toLowerCase();
  }
  return out;
}

function identityMatches(
  actual: Record<string, string>,
  saved: Record<string, string> | null,
): boolean {
  if (!saved) return true;
  const keys = Object.keys(saved);
  if (keys.length === 0) return true;
  return keys.every((key) => actual[key] === saved[key]);
}

export function evaluateFanDeviceGate(
  handshake: FanHandshake,
  savedIdentity?: Record<string, unknown> | null,
): FanDeviceGateResult {
  if (!handshake.ok || !handshake.supported) {
    const reason = handshake.reason
      ? handshake.reason
        .replace(/HandheldCompanion/gi, '设备数据组件')
        .replace(/\bHC\b/gi, '设备数据')
        .replace(/\bfactory\b/gi, '设备类型')
      : '握手未确认设备支持';
    return { allowed: false, writeReady: false, reason };
  }
  const profile = DEVICE_PROFILES.find((candidate) =>
    typeof handshake.deviceClass === 'string' && candidate.factoryTypes.has(handshake.deviceClass),
  );
  // Batch-03 mapped HC fan routes are admitted by a successful handshake even
  // when that device family has not yet had a dedicated real-machine session.
  // The runtime Host still keeps hardware control behind its explicit write
  // session; a failed/unknown handshake never reaches this branch.
  if (!profile && typeof handshake.fanRoute === 'string' && handshake.fanRoute.trim()) {
    return {
      allowed: true,
      writeReady: handshake.fanRouteWriteReady === true,
      deviceFamily: 'hc-mapped-fan',
      reason: handshake.fanRouteWriteReady === true
        ? `已识别可写风扇路线：${handshake.fanRoute}`
        : `已识别风扇路线，但尚未完成写入/恢复验证：${handshake.fanRoute}`,
    };
  }
  if (!profile) {
    return {
      allowed: false,
      writeReady: false,
      reason: '未识别风扇映射路由（源码可达/授权/实机验证三项均未建立；不作设备不支持结论）',
    };
  }

  const actual = normalizedIdentity(handshake.deviceIdentity);
  if (profile.manufacturers.size > 0 && actual.manufacturer && !profile.manufacturers.has(actual.manufacturer)) {
    return { allowed: false, writeReady: false, reason: '设备制造商与设备类型路由不一致' };
  }
  const saved = savedIdentity ? normalizedIdentity(savedIdentity) : null;
  if (!identityMatches(actual, saved)) {
    return { allowed: false, writeReady: false, reason: '当前设备身份与已保存 Fan 配置不匹配' };
  }
  const writeReady = handshake.fanRouteWriteReady === true;
  return {
    allowed: true,
    writeReady,
    deviceFamily: profile.family,
    reason: writeReady ? '设备身份与风扇数据路线通过，可写' : '设备身份已识别，但真实写入/恢复验证未完成',
  };
}

/**
 * A profile/lease restore response is safe only when it explicitly confirms
 * the HC Hardware callback. This helper is intentionally not used as a
 * universal process/sleep Close proof: HC's Window_Closed/SystemPending
 * paths provide separate virtual Close lifecycle evidence instead.
 */
function assertOemRestoreConfirmed(state: FanState, context: string): void {
  const closedWithoutHardwareCallback = hasCompletedHcCloseBoundary(state, true) &&
    state.openCalled !== true && state.openEventsCalled !== true &&
    state.hardwareWritesEnabled !== true && state.hardwareWrites !== true;
  if (state.unknownState === true || state.hcCloseCleanupPending === true ||
      (state.oemRestoreConfirmed !== true && !closedWithoutHardwareCallback)) {
    throw new Error(state.hcCloseCleanupPending === true
      ? `${context}：HC Close 资源清理未完成，拒绝结束 Host`
      : `${context}：OEM restore/HC Close 安全边界未确认，拒绝结束 Host`);
  }
}

/** Startup recovery receives the same close payload as normal shutdown, but
 * it validates the object in-line before the lifecycle object is rebuilt. A
 * protocol-2 Host may explicitly report an incomplete HC Close/Stop; that
 * must never be accepted merely because it says `Stopped`. Older Hosts which
 * omit these optional fields remain compatible and are covered by the
 * existing open/OEM checks. */
function hasExplicitIncompleteHcCloseEvidence(state: unknown, requireDeviceManagerStop: boolean): boolean {
  if (!state || typeof state !== 'object') return false;
  const value = state as Record<string, unknown>;
  const hasVirtual = 'hcVirtualCloseReturned' in value;
  const hasDeviceManagerStop = 'hcDeviceManagerStopCompleted' in value;
  if (hasVirtual && value.hcVirtualCloseReturned !== true) return true;
  // Protocol-2 process exit follows HC Window_Closed and therefore needs
  // both acknowledgements. SystemPending deliberately retains DeviceManager,
  // so its valid suspend response has only the virtual Close acknowledgement.
  return requireDeviceManagerStop && (hasVirtual || hasDeviceManagerStop) &&
    (!hasVirtual || !hasDeviceManagerStop || value.hcDeviceManagerStopCompleted !== true);
}

function hasCompletedHcCloseBoundary(state: FanState, requireDeviceManagerStop: boolean): boolean {
  if (!('hcVirtualCloseReturned' in state) || state.hcVirtualCloseReturned !== true) return false;
  return !requireDeviceManagerStop ||
    ('hcDeviceManagerStopCompleted' in state && state.hcDeviceManagerStopCompleted === true);
}

function hasAcceptedStoppedHcCloseEvidence(state: unknown): boolean {
  if (!state || typeof state !== 'object') return false;
  const value = state as FanState;
  return value.oemRestoreConfirmed === true || hasCompletedHcCloseBoundary(value, true);
}

/** A process/sleep close is complete only after the HC virtual device
 * session itself is released. OEM fan ownership and ACPI/HID resource
 * disposal are separate acknowledgements; profile/lease release intentionally
 * keeps the session open and therefore uses assertOemRestoreConfirmed only. */
function assertHcSessionClosed(state: FanState, context: string): void {
  // HC Window_Closed does not first apply a Hardware profile. A complete
  // virtual Close + DeviceManager.Stop is the process boundary; a missing
  // Hardware callback must not convert that boundary into a false failure.
  if (!hasCompletedHcCloseBoundary(state, true)) assertOemRestoreConfirmed(state, context);
  if (state.openCalled === true || state.openEventsCalled === true) {
    throw new Error(`${context}：HC Open/OpenEvents 会话仍未释放`);
  }
  // Protocol-2 real Hosts expose both lifecycle acknowledgements.  Keep
  // compatibility with older/mock adapters that omit the optional fields,
  // but never accept an explicit false: a stopped label alone is not proof
  // that ACPI/HID DeviceManager cleanup returned.
  if (hasExplicitIncompleteHcCloseEvidence(state, true)) {
    if ('hcVirtualCloseReturned' in state && state.hcVirtualCloseReturned !== true) {
    throw new Error(`${context}：HC 虚拟 Close 尚未确认返回`);
    }
    throw new Error(`${context}：HC DeviceManager 清理尚未完成`);
  }
}

/** Sleep keeps DeviceManager alive, but HC's virtual device session must have
 * returned before the OS is allowed to suspend. */
// E9（六十八批 T0 删减，HC 锚点：IDevice.cs:410-423 事件驱动 SetFanControl；生命周期零原创）
/**
 * A Host route conflict means another controller has an active hardware
 * session. It is not safe to retry a curve write automatically: recover OEM
 * once, keep the host available for a confirmed close, then require an
 * explicit user retry after the other controller has released the device.
 */
function isExternalFanControlConflict(error: unknown): boolean {
  const message = error instanceof Error ? error.message : String(error);
  return /\b(?:FAN_ROUTE_CONFLICT|HC_OPENLIB_CONFLICT|EXTERNAL_FAN_OWNER)\b/i.test(message);
}

/**
 * E5（五十五批 ① 判定①）：睡眠转场网络不可达——WinHTTP 12029/12030/超时
 * 表明宿主在本机睡眠/恢复事件窗内不可达（进程未死，只是无常驻应答）。
 * 与 route-lost/lease 不同，这不是控制权冲突也不是硬件会话丢失。
 */
function isWinHttpUnreachable(error: unknown): boolean {
  const message = error instanceof Error ? error.message : String(error);
  return /\b(?:12029|12030|timed out|timeout|winhttp|network|connection refused|request failed|ECONNRESET)\b/i.test(message);
}

function isHcCloseCleanupPending(error: unknown): boolean {
  const message = error instanceof Error ? error.message : String(error);
  return /\bHC_CLOSE_PENDING\b|HC Close 资源清理/i.test(message);
}

/**
 * R2（FAN-926R 裁决 §4.1）：**唯一**被允许"有界等待后重试"的瞬态门 = 宿主明确
 * `POWER_RESUMING`（或调用点已由可信同代快照判定 Resuming 且 controlAccepting=false）。
 */
function isPowerResumingError(error: unknown): boolean {
  if (error instanceof FanApiError) return error.errorCode === 'POWER_RESUMING';
  return /\bPOWER_RESUMING\b/.test(error instanceof Error ? error.message : String(error));
}

/**
 * R2（§4.1）：这些**不是**"恢复中"，绝不能当作可重试的瞬态门——
 * 401/403（凭据/权限）、FaultLocked/Unknown、待清理、关闭中、已挂起、路由/实例冲突。
 */
export function unsupportedFanControlError(remote: FanState): FanApiError | null {
  const legacyRejected = remote.fanCapabilitySupported == null && remote.state === 'AwaitingControl' && remote.openCalled === false && remote.openEventsCalled === false
    && typeof remote.lastError === 'string' && /HC 当前设备未声明 FanControl 能力/.test(remote.lastError);
  if (remote.fanCapabilitySupported !== false && !legacyRejected) return null;
  return new FanApiError(typeof remote.lastError === 'string' && remote.lastError
    ? remote.lastError : 'HC 当前设备未声明可用风扇能力', 409, 'FAN_UNSUPPORTED');
}

function isNonRetryableControlError(error: unknown): boolean {

  if (!(error instanceof FanApiError)) return false;
  if (error.status === 401 || error.status === 403) return true;
  return /^(?:FAN_UNSUPPORTED|FAULT_LOCKED|API_SESSION_REQUIRED|HOST_CLOSING|HC_CLOSE_PENDING|POWER_SUSPENDED|POWER_SUSPENDING|FAN_ROUTE_CONFLICT|HC_OPENLIB_CONFLICT|EXTERNAL_FAN_OWNER|HC_RESUME_RESULT_EXPIRED|HC_RESUME_SUPERSEDED|HC_RESUME_REBUILD_FAILED)$/.test(error.errorCode ?? '');
}

/**
 * FAN-927（2026-09-27）§4.4：Host 在**写入点**明确拒绝的租约失效。
 * 这不是"恢复中"也不是权限/硬件终态：本请求确认未执行，原意图/实例/代际仍有效。
 * `mutateWithBoundedRetry` 的那一次有界重试会因此走一次 fresh acquire 并重提同一曲线。
 */
function isLeaseInvalidError(error: unknown): boolean {
  if (error instanceof FanApiError && error.errorCode === 'LEASE_INVALID') return true;
  return /\bLEASE_INVALID\b/.test(error instanceof Error ? error.message : String(error));
}

/**
 * R2（§4.2）：恢复期有界等待的**终态**。每个任务恰好产出一次
 * `completed` / `cancelled` / `terminal-failed` 有限责任证据。
 * 单一形状（不做联合缩窄）——判据只看 `ok` + `reason`，不给调用方留下二义。
 */
interface FanResumeWaitOutcome {
  ok: boolean;
  reason: 'accepted' | 'cancelled' | 'superseded' | 'deadline' | 'terminal-failed';
  detail: string;
  probes: number;
  elapsedMs: number;
  intervalMs: number;
}

interface FanResumeWaitTask {
  key: string;
  cancelled: string | null;
  cancel: (reason: string) => void;
  promise: Promise<FanResumeWaitOutcome>;
}

/**
 * FAN-938 R5 §3.1：单个 Fan 恢复 owner 的**显式状态机**。
 *
 * - `queued`：手动/自动请求已登记恢复意图，但设备写入许可尚未成立；
 * - `recovering`：正在本代执行一次受控重开（探测/握手/`/api/resume`）；
 * - `controlled`：本代已重新接管，恢复任务结束（用户意图保留在既有曲线句柄）；
 * - `needs-attention`：认证/权限或外部 owner 这类**不可自动高速重试**的终态，保留意图与手动入口；
 * - `cancelled`：close/disable/new-generation 取代了该恢复任务。
 */
type FanRecoveryState = 'idle' | 'queued' | 'recovering' | 'controlled' | 'needs-attention' | 'cancelled';

/**
 * G7（FAN-926R 执行单 §3.2/§3.3）：一次控制请求的**有限任务上下文**。
 * 绑定本地电源代次、控制意图版本、Host 实例（pid）与单调绝对截止；`deadlineAt`
 * 在第一次准入探测时确定，之后探测/协调等待/队列内重入**共用**同一个截止（§3.3.4）。
 */
interface FanAdmissionContext {
  action: string;
  intentRevision: number;
  generation: number;
  hostPid: number;
  deadlineAt?: number;
  /** FAN-933：唤醒后「新控制接入」的 rearm 还要看 HC 会话是否真的重开
   * （openCalled/openEventsCalled）。既有 apply 准入不带此要求，避免改变原有语义。 */
  requireHcOpen?: boolean;
  /** Explicit user control may rescue a stale Suspended snapshot after the
   * renderer is demonstrably interactive. Automatic wake never sets this. */
  manualRecovery?: boolean;
}

/**
 * G8（执行单 §4）：一次心跳请求捕获的身份——心跳代次（睡眠边界 +1 使在途回包失效）、
 * 自己认领的 lease、当时的电源代次与 Host 实例。
 */
interface FanHeartbeatAttempt {
  intentRevision: number;
  epoch: number;
  leaseId: string;
  generation: number;
  hostPid: number;
}

function isSafeHostRecoveryState(remote: FanState, requireStopped: boolean): boolean {
  if (remote.unknownState === true || remote.hcCloseCleanupPending === true) return false;
  if (remote.hardwareWritesEnabled === true || remote.hardwareWrites === true) return false;
  const state = String(remote.state ?? '').toLowerCase();
  const completedHcClose = hasCompletedHcCloseBoundary(remote, true);
  const terminalHcBoundary = state === 'stopped'
    ? completedHcClose
    : state === 'suspended'
      ? hasCompletedHcCloseBoundary(remote, false)
      // A failed Enable/OpenEvents may already have completed HC's virtual
      // process-close boundary before the Host reports the original error.
      // HC has no generic Hardware callback on that path; the completed
      // Close + DeviceManager.Stop is still a safe resident AwaitingControl
      // state and must be observable for recovery, not fault-locked because
      // HardwareWritesObserved is historical.
      : state === 'awaitingcontrol'
        ? completedHcClose
        : false;
  // A Hardware profile acknowledgement is required while the session stays
  // open. A terminal HC lifecycle boundary is separately valid for sleep or
  // process exit because HC supplies no generic physical OEM acknowledgement.
  const releaseEvidence = remote.oemRestoreConfirmed === true || terminalHcBoundary;
  if (!releaseEvidence) return false;
  if (requireStopped) return state === 'stopped' || state === 'suspended';
  // AwaitingControl is HC's post-Hardware-profile handoff.  Stopped/Suspended
  // are the process/sleep boundaries.  Closed is retained for protocol-2
  // adapters which report the HC virtual Close before the final state label.
  return state === 'awaitingcontrol' || state === 'stopped' || state === 'suspended' || state === 'closed';
}

export interface FanHostStartOptions {
  /** Explicit control from an interactive Fan UI action may rescue a stale
   * same-generation Suspended snapshot. Automatic lifecycle calls leave this
   * false so a stale power snapshot can never become a write grant by itself. */
  manualRecovery?: boolean;
  reason?: string;
}

export interface FanHostLifecycleOptions {
  enabled?: boolean;
  config?: FanHostConfig;
  adapter?: FanApiAdapter;
  launcher?: FanHostLauncher;
  savedIdentity?: Record<string, unknown> | null;
  heartbeatIntervalMs?: number;
  /** Test override; production remains the fixed ten-second guard cadence. */
  fanGuardIntervalMs?: number;
  onDeviceIdentity?: (identity: Record<string, unknown>) => void;
  onState?: (state: FanHostLifecycleState) => void;
  /**
   * R2（§4.2）测试用受控时钟：恢复期等待的绝对截止（默认 15 s）。
   * 生产恒为默认值；自测用它把"一直 Resuming 到截止"的用例压到秒级。
   */
  resumeWaitDeadlineMs?: number;
  /**
   * FAN-927 §3/§5：读取 native 侧的当前 run 责任 / Fan 活动记录快照。
   * 生产为 `app.fanActivity()`；不可读时返回 null（fail-closed：没有依据就不动作）。
   * 自测用它注入替身，避免真机 IPC。
   */
  readNativeActivity?: () => Promise<FanActivitySnapshot | null>;
  /** R4: one bounded authoritative read on explicit control after a missed wake edge. */
  readNativePowerState?: () => Promise<PowerLifecycleState | null>;
  /** Fan-only native promotion for an interactive click after a stale Suspended phase. */
  requestManualWake?: (generation?: number) => Promise<FanManualWakeResult | null>;
}

/**
 * Serializes Host lifecycle, lease ownership and OEM restore. The class is
 * usable with a fake adapter/launcher for regression tests, while the product
 * singleton remains disabled and therefore performs zero native operations.
 */
export class FanHostLifecycle {
  private readonly enabled: boolean;
  private config: FanHostConfig;
  private readonly adapter: FanApiAdapter;
  private readonly launcher: FanHostLauncher;
  private readonly onState?: (state: FanHostLifecycleState) => void;
  // The coordinator is the fan-domain admission boundary. FanHost remains
  // the HC device-session owner; this gate only translates coordinator power,
  // epoch and write permission into the existing HC route.
  private readonly coordinator = new FanCoordinatorGate();
  private onDeviceIdentity?: (identity: Record<string, unknown>) => void;
  private readonly heartbeatIntervalMs: number;
  private readonly fanGuardIntervalMs: number;
  /** R2 §4.2：恢复期等待的绝对截止（生产 = RESUME_WAIT_DEADLINE_MS）。 */
  private readonly resumeWaitDeadlineMs: number;
  /** FAN-927 §3/§5：native 活动记录读取（见 FanHostLifecycleOptions.readNativeActivity）。 */
  private readonly readNativeActivity: () => Promise<FanActivitySnapshot | null>;
  private readonly readNativePowerState: () => Promise<PowerLifecycleState | null>;
  private readonly requestManualWake: (generation?: number) => Promise<FanManualWakeResult | null>;
  /** Only one native Fan manual-wake promotion per power generation. */
  private manualWakeRequestedGeneration = 0;
  /** FAN-927 §5.2：最近一次成功握手给出的设备类，用于与崩溃前记录比对（不同设备不自动开）。 */
  private deviceClassValue: string | null = null;
  private savedIdentity: Record<string, unknown> | null;
  private stateValue: FanHostLifecycleState;
  /**
   * FAN-926R UX-1（2026-09-27，用户反馈"按阶段显示文字"）：最近一次启动**阶段**的只读观测
   * （`launcher-enter` / `runtime-dependencies-verified` / `host-process-launched` …）。
   *
   * 只用于界面把"首次启用"那段十几秒讲清楚：它**不参与**任何准入、写入或状态判定，
   * 也**不新增**任何 I/O 或请求；离开 starting/handshaking 时清空。
   */
  private startStageValue: string | null = null;
  private process: FanHostProcess | null = null;
  // HC Close is settled, but the exact process exit must still be retried.
  private closedHostAwaitingExit = false;
  private lease: FanLease | null = null;
  private heartbeatTimer: ReturnType<typeof setInterval> | null = null;
  /** One timer renewal may be queued/in-flight; never accumulate maintenance behind user work. */
  private timerHeartbeatPending: Promise<void> | null = null;
  private opened = false;
  private eventsOpened = false;
  private operation: Promise<unknown> = Promise.resolve();
  // App startup and FanView mount can request the same start in one turn.
  // Serialize those requests and share the in-flight result; otherwise a
  // failed deployment can produce repeated shutdown/retry cycles and obscure
  // the first actionable error.
  private startPromise: Promise<FanDeviceGateResult> | null = null;
  /**
   * A manual control request arriving while ordinary startup is already in
   * flight must not inherit that request's non-manual semantics.  Keep one
   * upgrade promise so rapid clicks share the same second pass instead of
   * either being swallowed by `startPromise` or spawning parallel starts.
   */
  private startUpgradePromise: Promise<FanDeviceGateResult> | null = null;
  /**
   * `power.resume-ready` and the committed `power.resumed` compensation can
   * arrive close together.  They are one Fan recovery transaction, not two
   * HC opens.  Share the in-flight resume body across both edges.
   */
  private resumePromise: Promise<void> | null = null;
  private desiredCurve: FanNode[] | null = null;
  private sessionGeneration = 0;
  private powerGeneration = 0;
  /** Native starts awake at 1; Host power receipts start at 0 (no power operation).
   * This baseline is established only by an authoritative initial-awake read and
   * is revoked by ANY actual sleep/resume boundary. It is not a resume receipt. */
  private initialAwakePowerGeneration: number | null = null;
  private observedPowerTransition = false;
  private explicitOpenBinding: { sessionGeneration: number; powerGeneration: number } | null = null;
  private writeReady = false;
  // The globally-owned lifecycle, not a mounted FanView, remembers the last
  // acknowledged curve. This keeps a full OEM -> fresh HC session -> replay
  // transaction possible after KeepAlive evicts the fan page during sleep.
  private activeCurve: FanNode[] | null = null;
  private resumeCurve: FanNode[] | null = null;
  private coordinatorSession: string | null = null;
  private coordinatorRequestSequence = 0;
  /**
   * FAN-933：本地会话是在**哪个电源代次**被准入的。代次前进（真实睡眠→唤醒）后，
   * 常驻 Host 的 HC 会话属于**上一个电源周期**：远端 openCalled/openEventsCalled 已被
   * 关闭，此时它不是可写会话。`Suspended + process` 旧分支和「local ready 但远端其实
   * 已 Suspended」的陈旧短路口都必须先 rearm，不能再直接放行。
   */
  private admittedPowerGeneration = -1;
  /** FAN-933 §2.1(5)：同一 Host + 同一 generation 只允许一个在途 rearm。 */
  private rearmTask: { generation: number; intentRevision: number; sessionGeneration: number; promise: Promise<void> } | null = null;
  // ── R2（FAN-926R 唤醒租约与有界等待裁决 §4.2/§4.3）──────────────────────────────
  /** 同一 Host 实例/恢复代际**只共享一个**等待任务；重复通知合并到同一个 promise。 */
  private resumeWaitTask: FanResumeWaitTask | null = null;
  /**
   * 控制意图版本。新曲线/预设、disable/close、新一代 suspend 都会 +1，使**旧的**
   * 恢复重放/等待立即失效（§4.3：新意图优先于旧 resume 保存的曲线）。
   */
  private controlIntentRevision = 0;
  private aiMockCloseReceipt: Record<string, unknown> | null = null;
  /** Last confirmed AI mock Close only; no lease/token or real HC evidence. */
  getAiMockCloseReceipt(): Record<string, unknown> | null {
    return this.config.aiMockSession && this.aiMockCloseReceipt ? { ...this.aiMockCloseReceipt } : null;
  }
  /**
   * G8（执行单 §4）：心跳代次。生产睡眠/关闭边界 +1，使**在途**心跳回包（成功与失败）
   * 一律失效——旧成功不覆盖新 lease，旧失败不清新 lease、不触发重放。
   */
  private heartbeatEpoch = 0;
  /**
   * G7（执行单 §3.3.1）：最近一次真正取代当前请求的睡眠/关闭电源代次；
   * 同代重复通知幂等，不重复失效（也不会误取消刚建立的本代 waiter）。
   */
  private supersededPowerGeneration: number | null = null;
  // ── FAN-938 R5 §3：单一 Fan 恢复 owner ────────────────────────────────────────
  /**
   * 当前登记中的恢复意图。与「实际设备写入许可」**分开保存**：手动/自动请求无条件把
   * 意图登记在这里（§3「分开恢复请求与设备写入许可」），由 owner 在真实睡眠、认证、
   * 设备互斥与 HC 会话检查成立后再推动写入。
   */
  private recoveryIntent: { generation: number; reason: string; manual: boolean } | null = null;
  private recoveryState: FanRecoveryState = 'idle';
  /** 只允许一个在途退避节拍；见 scheduleRecoveryTick。 */
  private recoveryRetryTimer: ReturnType<typeof setTimeout> | null = null;
  private recoveryAttempts = 0;
  /**
   * FAN-938 R6 §P1：**真实在途 owner 身份**。唯一 timer ≠ 唯一异步任务——一次 tick
   * 可能跨多个 await 长驻，期间新的定时器/意图可能到达。这里把每个 tick 链在同一个
   * `recoveryTask` 上串行执行（不并发），并用单调 `recoveryToken` 标识 owner 世代：
   * close/disable/新睡眠/新曲线令 token 递增，所有旧 tick 在每个 await 边界复核 token
   * 后自行退出，绝不写入。
   */
  private recoveryTask: Promise<void> = Promise.resolve();
  // A pending explicit request can share the write completed by its recovery
  // owner. This receipt is only for that exact intent/generation/Host session,
  // never a cache for later manual actions or proof of physical ownership.
  private completedRecoveryWrite: {
    generation: number; intentRevision: number; hostSession: string | null;
    curve: FanNode[] | null; response: FanState;
  } | null = null;
  /** 令旧 tick 失效的单调令牌（cancelRecovery/settle 时 +1）。 */
  private recoveryToken = 0;

  constructor(options: FanHostLifecycleOptions = {}) {
    this.enabled = options.enabled === true;
    this.config = options.config ?? resolveFanHostConfig();
    this.adapter = options.adapter ?? createFanApiAdapter({ enabled: this.enabled, baseUrl: this.config.baseUrl, sessionToken: this.config.sessionToken });
    this.launcher = options.launcher ?? new NativeFanHostLauncher((stage) => { this.startStageValue = stage; });
    this.savedIdentity = options.savedIdentity ?? null;
    this.heartbeatIntervalMs = Math.max(0, options.heartbeatIntervalMs ?? DEFAULT_LEASE_RENEWAL_INTERVAL_MS);
    this.fanGuardIntervalMs = Math.max(1, options.fanGuardIntervalMs ?? FAN_GUARD_INTERVAL_MS);
    this.resumeWaitDeadlineMs = Number.isFinite(options.resumeWaitDeadlineMs) && (options.resumeWaitDeadlineMs ?? 0) > 0
      ? Math.max(RESUME_WAIT_MIN_INTERVAL_MS, Math.floor(options.resumeWaitDeadlineMs as number))
      : RESUME_WAIT_DEADLINE_MS;
    this.onDeviceIdentity = options.onDeviceIdentity;
    this.onState = options.onState;
    this.readNativeActivity = options.readNativeActivity ?? defaultReadNativeActivity;
    this.readNativePowerState = options.readNativePowerState
      ?? (() => powerLifecycle.get(1000).catch(() => null));
    this.requestManualWake = options.requestManualWake
      ?? ((generation) => powerLifecycle.manualFanWake(generation).catch(() => null));
    this.stateValue = this.enabled ? 'stopped' : 'disabled';
  }

  get state(): FanHostLifecycleState { return this.stateValue; }
  /** FAN-926R UX-1：只读的启动阶段（离开 starting/handshaking 后为 null）。 */
  get startStage(): string | null { return this.startStageValue; }
  /** 只读判定（不参与 TS 控制流缩窄）：跨 await 复核"是否已被显式禁用"时使用。 */
  private isDisabledState(): boolean { return this.stateValue === 'disabled'; }
  /** Public five-phase projection of the internal state (FAN-926R item 3).
   * A fresh application boot reports `stopped` while no Host, handshake, HC
   * session or timer exists; `active` requires live write authority. */
  get phase(): FanHostPhase {
    switch (this.stateValue) {
      case 'disabled': return 'disabled';
      case 'stopped': return 'stopped';
      case 'starting':
      case 'handshaking': return 'starting';
      case 'awaiting-control': return 'ready';
      case 'ready': return this.writeReady ? 'active' : 'ready';
      case 'suspended': return 'recovering';
      case 'conflict-locked':
      case 'fault-locked':
      case 'unknown': return 'failed';
    }
  }
  get controlReady(): boolean { return this.writeReady; }
  /** FAN-932：只读暴露「是否已有活动控制意图」，供 App 层自动启动编排做去重判定。
   * 仅转发内部 `hasActiveControlIntent()`（activeCurve/resumeCurve/desiredCurve 非空），
   * 不改变任何生命周期语义，也不构成第二条启动路径。 */
  get hasControlIntent(): boolean { return this.hasActiveControlIntent(); }
  get currentLease(): FanLease | null { return this.lease ? { ...this.lease } : null; }
  get processId(): number | null { return this.process?.pid ?? null; }

  /** FAN-938 R5 §P4：只读暴露「当前存在未完成的 Fan 恢复任务」，供 UI 显示
   * “控制意图已开启，正在恢复”。仅转发内部 owner 状态；`needs-attention` 是终态
   * （后台不再自动高速重试），不算“正在恢复”，避免界面长期停在一句无法兑现的承诺上。
   * 不改变任何生命周期语义，也不构成第二条恢复路径。 */
  get recoveryActive(): boolean {
    return this.recoveryIntent !== null && this.recoveryState !== 'needs-attention';
  }

  /**
   * FAN-938 R6.4/T0：显式 Fan 点击的第一道语义判定。
   *
   * WebView2 可能漏掉 `power.suspending`，所以 FanView 仍可能把开关镜像成
   * "已开启"，但本代 coordinator 尚未收到同代 wake-ready。此时一次用户点击
   * 必须进入恢复链，而不能被误解释成 disable。这里是只读内存判定：不发 IPC、
   * 不等待 OEM、不写硬件；真正的救援仍由 apply() → power.fanManualWake →
   * Host Open/OpenEvents/acquire/enable 完成。
   */
  needsManualRecoveryForControl(): boolean {
    if (!this.enabled || this.isDisabledState()) return false;
    if (this.recoveryActive || this.stateValue === 'suspended') return true;
    const generation = Math.max(0, Math.floor(this.powerGeneration));
    return generation > 0 && !this.coordinator.isWakeReady(generation);
  }

  /**
   * FAN-938 R7：补偿丢失的 renderer/native 电源边沿。
   *
   * `toggleControl()` 以前只能依据本地 UI 镜像和 coordinator phase 判断
   * “这是关闭还是唤醒后的第一次控制”。现代待机期间 WebView2 可能漏掉
   * `suspending`/`resumed`，此时 native 已经是 `suspended`，而本地仍是
   * `ready`；第一次点击会被误当成 disable，恢复链永远不会启动。
   *
   * 这个异步入口只读一次 Fan 专属 `power.lifecycle` 快照，并把观察到的
   * generation/phase 同步到 Fan coordinator。它不等待 OEM、不打开 HC、
   * 不写硬件；真正的写入仍必须经过 manual wake → Host resume →
   * Open/OpenEvents → acquire → enable。
   */
  async needsManualRecoveryForControlAsync(): Promise<boolean> {
    if (!this.enabled || this.isDisabledState()) return false;

    // A local Suspended/recovery state is a reason to recover, but it is not a
    // reason to skip the authoritative read: the renderer may have missed a
    // later sleep generation. Reconcile before constructing the manual request
    // so native never rejects it as stale-generation.
    const localGenerationBefore = Math.max(0, Math.floor(this.powerGeneration));
    const localStateBefore = this.stateValue;
    const localRecoveryBefore = this.needsManualRecoveryForControl();
    const snapshot = await this.readNativePowerState().catch(() => null);
    if (!snapshot) {
      // A missing power snapshot is not evidence that a live control intent is
      // safe to interpret as "turn off". Keep the user's click on the recovery
      // path; the recovery owner remains write-closed until it can synchronize
      // a same-generation safe phase.
      const retainIntent = localRecoveryBefore || this.hasActiveControlIntent()
        || this.recoveryActive || this.stateValue === 'suspended';
      if (retainIntent) {
        fanLifecycleEvidence('lifecycle.manual-wake-snapshot-unavailable', {
          generation: this.powerGeneration,
          state: this.stateValue,
          hasControlIntent: this.hasActiveControlIntent(),
          recoveryState: this.recoveryState,
        });
      }
      return retainIntent;
    }

    this.synchronizeObservedPowerSnapshot(snapshot);
    const generation = Math.max(0, Math.floor(snapshot.generation));
    if (localGenerationBefore !== generation || localStateBefore !== snapshot.phase) {
      fanLifecycleEvidence('lifecycle.manual-wake-snapshot-adopted', {
        action: 'manual-control-power-sync',
        generation: localGenerationBefore,
        currentGeneration: generation,
        state: snapshot.phase,
        reason: 'current-power-transaction-read',
        wakeContext: snapshot.resumeReady === true,
      });
    }

    const snapshotRequiresRecovery = snapshot.phase === 'suspending' || snapshot.phase === 'suspended'
      || (snapshot.phase === 'resuming' && snapshot.resumeReady !== true)
      || (generation > 0 && !this.coordinator.isWakeReady(generation));
    // Re-evaluate after synchronization. If the snapshot disproved a stale
    // local recovery state, a normal awake toggle may still perform its close.
    return this.needsManualRecoveryForControl() || snapshotRequiresRecovery;
  }
  /**
   * R7/R8: let telemetry adopt a missed wake edge, or let an explicit user
   * retry promote the same recovery owner. This only retains the latest curve
   * and registers intent; runRecoveryTick still requires a safe native phase
   * and Host Open/OpenEvents before any write.
   */
  async requestRecoveryAfterObservedHostState(reason: string): Promise<boolean> {
    if (!this.enabled || this.isDisabledState() || !this.hasActiveControlIntent()) return false;

    // An existing owner (including needs-attention) remains the sole recovery
    // authority. Telemetry must not reset its backoff or silently clear a terminal state.
    if (this.recoveryIntent !== null) return true;

    const snapshot = await this.readNativePowerState().catch(() => null);
    if (!snapshot) return false;
    this.synchronizeObservedPowerSnapshot(snapshot);

    const generation = Math.max(0, Math.floor(snapshot.generation));
    const powerCycleAdvanced = generation > this.admittedPowerGeneration;
    const wakeBoundaryObserved = snapshot.phase === 'suspending'
      || snapshot.phase === 'suspended'
      || snapshot.phase === 'resuming'
      || snapshot.resumeReady === true
      || (snapshot.phase === 'ready' && snapshot.hardwareWritesAllowed === true && powerCycleAdvanced);
    if (generation <= 0 || !wakeBoundaryObserved) {
      fanLifecycleEvidence('lifecycle.recovery-observed-host-state-deferred', {
        reason, generation, phase: snapshot.phase, resumeReady: snapshot.resumeReady,
        admittedPowerGeneration: this.admittedPowerGeneration,
        hardwareWritesAllowed: snapshot.hardwareWritesAllowed,
      });
      return false;
    }

    this.noteRecoveryIntentPending(reason, generation, false);
    fanLifecycleEvidence('lifecycle.recovery-observed-host-state-accepted', {
      reason, generation, phase: snapshot.phase, resumeReady: snapshot.resumeReady,
      admittedPowerGeneration: this.admittedPowerGeneration,
      hardwareWritesAllowed: snapshot.hardwareWritesAllowed,
    });
    return this.recoveryIntent?.generation === generation;
  }

  /**
   * Adopt a current native power snapshot when a one-shot renderer event was
   * missed. This is deliberately a read-side reconciliation: it never calls
   * HC, never sends `/api/resume`, and never grants a write.
   */
  private synchronizeObservedPowerSnapshot(snapshot: PowerLifecycleState | null): void {
    if (!snapshot || !Number.isSafeInteger(snapshot.generation) || snapshot.generation < 0) return;
    const generation = snapshot.generation;
    // FAN-943: native/main.cpp initializes g_powerGeneration to 1, whereas a
    // fresh Host has no power operation (generation=attempt=0). Adopting the
    // initial awake snapshot must not invent a sleep cycle for that open Host.
    if (snapshot.phase !== 'ready' || snapshot.resumeReady === true || generation > 1) {
      this.observedPowerTransition = true;
    }
    if (generation === 1 && snapshot.phase === 'ready' && snapshot.resumeReady === false
        && snapshot.hardwareWritesAllowed === true && !this.observedPowerTransition
        && this.powerGeneration === 0 && !this.isSleepWindow()) {
      this.initialAwakePowerGeneration = generation;
      fanDiagnosticLog('lifecycle.initial-awake-power-baseline', { generation, hostResumeReceiptCreated: false });
    } else if (generation !== 1 || snapshot.phase !== 'ready' || snapshot.resumeReady !== false
        || snapshot.hardwareWritesAllowed !== true) {
      this.initialAwakePowerGeneration = null;
    }
    if (generation > this.powerGeneration) {
      this.powerGeneration = generation;
      this.coordinator.observePowerGeneration(generation);
      this.coordinatorSession = null;
      this.controlIntentRevision += 1;
      this.cancelResumeWait('observed-native-power');
      this.invalidateHeartbeatEpoch();
      this.stopHeartbeat();
      this.lease = null;
      setFanDiagnosticPowerGeneration(generation);
      fanDiagnosticLog('lifecycle.power-snapshot-generation-adopted', {
        generation, phase: snapshot.phase, source: 'power.lifecycle-read',
      });
    }
    if (generation !== Math.max(0, Math.floor(this.powerGeneration))) return;

    if (snapshot.phase === 'suspending' || snapshot.phase === 'suspended') {
      // Treat the read snapshot as an observed boundary only. The Host may
      // already have executed its sleep Close; do not issue a second Close.
      this.coordinator.observeNativePower('suspending', generation);
      this.applyPowerBoundaryIntent('suspending', generation);
      this.coordinator.markSuspended();
      if (this.stateValue !== 'disabled' && this.stateValue !== 'stopped') this.setState('suspended');
      fanLifecycleEvidence('lifecycle.power-snapshot-synchronized', {
        generation, phase: snapshot.phase, source: 'power.lifecycle-read',
      });
      return;
    }

    if (snapshot.phase === 'resuming') {
      this.coordinator.observeNativePower('resuming', generation);
      if (this.stateValue !== 'disabled' && this.stateValue !== 'stopped') this.setState('suspended');
      fanLifecycleEvidence('lifecycle.power-snapshot-synchronized', {
        generation, phase: snapshot.phase, resumeReady: snapshot.resumeReady === true,
        source: 'power.lifecycle-read',
      });
      if (snapshot.resumeReady === true) this.coordinator.observeNativePower('resume-ready', generation);
      return;
    }

    if (snapshot.phase === 'ready' && snapshot.resumeReady === true) {
      // A durable native resume fact can outlive the renderer event. This is
      // equivalent to the existing committed Fan wake admission, not a write.
      this.coordinator.observeNativePower('resume-ready', generation);
      fanLifecycleEvidence('lifecycle.power-snapshot-synchronized', {
        generation, phase: snapshot.phase, resumeReady: true,
        source: 'power.lifecycle-read',
      });
    }
  }

  /** Bind lifecycle diagnostics to the native power transaction currently
   * being consumed.  Older WebView events may omit generation; those events
   * must not erase a newer value.  The actual power boundary is delivered
   * separately through observePowerBoundary(); do not synthesize a `resumed`
   * event here merely because a newer generation was observed. */
  setPowerGeneration(generation: number): void {
    if (Number.isFinite(generation) && generation > this.powerGeneration) {
      this.explicitOpenBinding = null;
      this.initialAwakePowerGeneration = null;
      this.powerGeneration = generation;
      // Advance the coordinator epoch without pretending that the machine has
      // resumed. The native power event that follows owns the phase change.
      this.coordinator.observePowerGeneration(generation);
      this.coordinatorSession = null;
      // G7（执行单 §3.3.1）：新的电源代次/Host 真正取代当前请求——同步失效旧等待与旧重放，
      // 并让**在途**心跳回包失效（§4）。同代的 resume-ready/resumed 不进入这个分支，
      // 因此不会误取消刚建立的本代 waiter/lease。
      this.controlIntentRevision += 1;
      this.cancelResumeWait('new-power-generation');
      this.invalidateHeartbeatEpoch();
      setFanDiagnosticPowerGeneration(generation);
      fanDiagnosticLog('lifecycle.power-generation', { generation });
    }
  }

  /** Feed the native power transaction into the single fan-domain gate. */
  observePowerBoundary(event: FanCoordinatorPowerEvent, generation: number): void {
    if (!Number.isSafeInteger(generation) || generation < 0) return;
    if (event === 'suspending' || event === 'resuming'
        || event === 'manual-resume-start' || event === 'shutdown') {
      this.observedPowerTransition = true;
      this.explicitOpenBinding = null;
      this.initialAwakePowerGeneration = null;
    }
    try {
      this.coordinator.observeNativePower(event, generation);
      fanDiagnosticLog('coordinator.power-boundary', {
        event,
        generation,
        phase: this.coordinator.snapshot().phase,
      });
    } catch (error) {
      fanDiagnosticLog('coordinator.power-boundary-rejected', {
        event,
        generation,
        error: error instanceof Error ? error.message : String(error),
      });
      throw error;
    }
    // G7（执行单 §3.3.1）：App 的**真实** suspend/shutdown 通知走这条生产入口
    // （App.vue: setPowerGeneration + observePowerBoundary('suspending')），不是直接调
    // suspend()。取消与心跳失效必须落在这里，否则"真实电源入口"仍能继续入队。
    this.applyPowerBoundaryIntent(event, generation);
  }

  /**
   * G7（执行单 §3.3.1）：只有**真正取代当前请求**的 suspend/shutdown 才同步失效旧请求。
   * 同一 epoch 的重复通知幂等；同代 resume-ready/resumed 只推进该周期，不取消仍有效的
   * 本代 waiter，也不撤销刚建立的本代 lease/心跳。
   */
  private applyPowerBoundaryIntent(event: FanCoordinatorPowerEvent, generation: number): void {
    if (event === 'resuming') {
      // FAN-927 §4.1：真正进入 resuming 说明睡眠已经发生 ⇒ 睡眠前的本地 lease 必已失效
      // （Host 的 15 s leaseTimer 已把它交回 OEM）。在**任何**唤醒 acquire 之前同步丢弃，
      // 保证"睡眠后 0 次旧 token 探测、1 次新申请"；'resumed'/'resume-ready' 不清刚拿到的新 lease。
      if (this.lease) {
        fanLifecycleEvidence('lifecycle.lease-dropped-on-resume', {
          generation,
          state: this.stateValue,
        });
      }
      this.invalidateHeartbeatEpoch();
      this.stopHeartbeat();
      this.lease = null;
      return;
    }
    if (event !== 'suspending' && event !== 'shutdown') return;
    if (this.supersededPowerGeneration === generation) return;
    this.supersededPowerGeneration = generation;
    this.controlIntentRevision += 1;
    this.cancelResumeWait(event);
    // §4：生产 suspending 边界立即停未来心跳 tick，并使旧心跳 epoch 失效。
    this.invalidateHeartbeatEpoch();
    this.stopHeartbeat();
    // FAN-927 §4.1：真正进入 suspending 就是"本地旧 lease 已不可能有效"的权威边界。
    // 同步丢弃本代旧 token（不做 release/restore、不清控制意图、不动 desiredCurve）；
    // G8 的迟到回包保护仍由 heartbeatEpoch 失效承担。唤醒后 resume 一次 fresh acquire。
    this.lease = null;
    fanLifecycleEvidence('lifecycle.power-boundary-supersede', {
      event,
      generation,
      intentRevision: this.controlIntentRevision,
      state: this.stateValue,
    });
  }

  get coordinatorSnapshot() {
    return this.coordinator.snapshot();
  }

  private beginCoordinatorSession(): void {
    const generation = Math.max(0, Math.floor(this.powerGeneration));
    const session = `fan-host-${this.sessionGeneration}-${generation}`;
    this.coordinator.beginFanHostSession(session, generation);
    this.coordinatorSession = session;
    // FAN-933：会话一旦在本代建立，就把「已准入代次」钉到当前代次；代次再前进
    // （真实睡眠→唤醒）即判定为上一电源周期的陈旧会话，必须 rearm。
    this.admittedPowerGeneration = generation;
    fanDiagnosticLog('coordinator.fan-session-begin', {
      session,
      generation,
      phase: this.coordinator.snapshot().phase,
    });
  }

  /**
   * A handshake proves only that the resident Host is reachable. HC readiness
   * is established later by either a healthy remote snapshot (the Host-owned
   * F5 rebuild) or this frontend's completed Open/OpenEvents/lease sequence.
   */
  private admitCoordinatorHcReady(): void {
    const session = this.coordinatorSession;
    if (!session) throw new Error('FAN_COORDINATOR_SESSION_REQUIRED');
    const generation = Math.max(0, Math.floor(this.powerGeneration));
    if (this.coordinator.snapshot().phase !== 'ready') {
      this.coordinator.admitHcReady(session, generation);
    }
    fanDiagnosticLog('coordinator.fan-session-ready', {
      session,
      generation,
      phase: this.coordinator.snapshot().phase,
    });
  }

  private admitCoordinatorWrite(action: string): void {
    const session = this.coordinatorSession;
    if (!session) throw new Error('FAN_COORDINATOR_SESSION_REQUIRED');
    const requestId = `fan-write-${this.sessionGeneration}-${++this.coordinatorRequestSequence}`;
    const admission = this.coordinator.admitFanWrite(requestId, session, Math.max(0, Math.floor(this.powerGeneration)));
    if (!admission.accepted) {
      fanDiagnosticLog('coordinator.write-rejected', { action, requestId, reason: admission.reason });
      throw new Error(`FAN_COORDINATOR_WRITE_REJECTED:${admission.reason ?? 'unknown'}`);
    }
    fanDiagnosticLog('coordinator.write-admitted', { action, requestId, session });
  }

  private ensureCoordinatorSession(): void {
    if (!this.coordinatorSession) this.beginCoordinatorSession();
  }

  private revokeCoordinatorWrites(reason: FanCoordinatorPowerEvent | 'disable'): void {
    if (reason === 'disable') {
      this.coordinator.releaseFanControl();
    } else {
      this.coordinator.observeNativePower(reason, Math.max(0, Math.floor(this.powerGeneration)));
    }
    fanDiagnosticLog('coordinator.write-revoked', {
      reason,
      phase: this.coordinator.snapshot().phase,
      generation: this.powerGeneration,
    });
  }

  /** Read host telemetry without changing ownership or hardware state. */
  async getState(): Promise<FanState> {
    return this.enqueue(() => this.adapter.getState());
  }

  /** Update the native-resolved sidecar directory before the first start. */
  setConfig(config: FanHostConfig): void {
    if (this.state !== 'stopped' && this.state !== 'disabled') return;
    this.config = config;
  }

  setSavedIdentity(identity: Record<string, unknown> | null): void {
    this.savedIdentity = identity;
  }

  setDeviceIdentitySink(sink: ((identity: Record<string, unknown>) => void) | undefined): void {
    this.onDeviceIdentity = sink;
  }

  /**
   * Arm the resident resume/guard curve without writing hardware. Used when
   * WebView recreation adopts a Suspended Host that was already controlling.
   */
  rememberDesiredCurve(nodes: readonly FanNode[]): void {
    if (!Array.isArray(nodes) || nodes.length === 0) return;
    // E9（六十八批 T0 删减，HC 锚点：IDevice.cs:410-423 事件驱动 SetFanControl；生命周期零原创）
    // 仅记住曲线会话句柄，不再 arm fan-guard 保活循环。
    this.desiredCurve = cloneFanNodes(nodes);
  }

  private setState(next: FanHostLifecycleState): void {
    this.stateValue = next;
    // UX-1：启动阶段只在 starting/handshaking 期间有意义；一旦进入其它状态就清空，
    // 避免界面把过期阶段当成当前进度。
    if (next !== 'starting' && next !== 'handshaking') this.startStageValue = null;
    fanDiagnosticLog('lifecycle.state', { state: next, generation: this.powerGeneration });
    this.onState?.(next);
  }

  private advanceSessionGeneration(): number {
    this.explicitOpenBinding = null;
    this.sessionGeneration += 1;
    return this.sessionGeneration;
  }

  /**
   * The real Host may close its HC device session while handling a failed
   * Open/OpenEvents/Enable call. Keep the frontend's admission flags aligned
   * with that authoritative snapshot; otherwise a retry can skip Open or
   * OpenEvents and hit HOST_EVENTS_NOT_OPEN forever.
   */
  private syncRemoteSessionState(remote: FanState | undefined): void {
    if (!remote) return;
    const evidence = this.snapshotGenerationEvidence(remote);
    if (evidence.freshness === 'stale' || evidence.freshness === 'future' || evidence.freshness === 'invalid') {
      fanDiagnosticLog('lifecycle.session-snapshot-untrusted-ignored', { freshness: evidence.freshness, remoteGeneration: evidence.generation, generation: this.powerGeneration });
      return;
    }
    if (remote.state === 'Stopped' || remote.state === 'Suspended') {
      this.opened = false;
      this.eventsOpened = false;
    }
    if (typeof remote.openCalled === 'boolean') this.opened = remote.openCalled;
    if (typeof remote.openEventsCalled === 'boolean') this.eventsOpened = remote.openEventsCalled;
    if (!this.opened || !this.eventsOpened) this.explicitOpenBinding = null;
    this.adoptAuthoritativeLeaseState(remote);
  }

  /**
   * R2（FAN-926R 裁决 §4.3）：远端权威快照与本地 lease token 的**单向**对账。
   *
   * - 快照明确给出 lease ⇒ 采纳（既有行为）；
   * - 快照**明确"无 lease"**且属于当前代次 ⇒ 本地 token 已不可能有效，清空并停心跳；
   * - **陈旧**（`resumePhaseGeneration` 落后于本地 power generation）快照不算权威 ⇒
   *   既不清旧 token，也不把新 token 抹掉（§4.3 明令）。
   *
   * 说明"另一实例"的边界：本方法只消费**本生命周期的 adapter**（同一 baseUrl/端口 + 本方
   * 会话令牌）返回的快照；别的实例/未绑定的请求在传输层就会被 401/403 拒绝并走错误路径，
   * 不会以"权威空 lease"的形式进入这里。
   */
  private adoptAuthoritativeLeaseState(remote: FanState): void {
    const evidence = this.snapshotGenerationEvidence(remote);
    const authoritativeGeneration = evidence.generation;
    if (evidence.freshness === 'stale' || evidence.freshness === 'future' || evidence.freshness === 'invalid') {
      fanDiagnosticLog('lifecycle.lease-snapshot-stale-ignored', {
        remoteGeneration: authoritativeGeneration,
        generation: this.powerGeneration,
      });
      return;
    }
    const remoteLease = remote.lease;
    if (remoteLease && typeof remoteLease.leaseId === 'string' && remoteLease.leaseId.length > 0) {
      this.lease = { ...remoteLease };
      return;
    }
    if (!this.lease) return;
    // §4.3 只认**明确**证据：快照必须真的携带 `lease: null` 字段（宿主的 lease 遥测在场），
    // 才允许据此判定"当前无 lease"。完全不携带该字段的快照是"没有这项遥测"，不是证据——
    // 用它清 token 会把新凭据误删（旧版/受控夹具/裁剪过的响应都会命中）。
    if (!Object.prototype.hasOwnProperty.call(remote, 'lease')) return;
    if (remote.leaseGeneration !== null && remote.leaseGeneration !== undefined) return;
    fanDiagnosticLog('lifecycle.lease-token-cleared-by-authoritative-snapshot', {
      generation: this.powerGeneration,
      remoteState: remote.state,
    });
    this.lease = null;
    this.stopHeartbeat();
  }

  /**
   * 920 F-3：失败路径专用回灌。Host 在 Open/OpenEvents/Enable 失败时会走
   * RecoverAfterHardwareFailure → RestoreHardware(close: true)，其
   * CloseHcSessionForLifecycle 会清掉 OpenCalled/OpenEventsCalled
   * （Program.cs L7994-7995）。前端若不同步这两个标志，重试就会跳过
   * Open/OpenEvents 直接写一个已关闭的会话——Host 侧以
   * HOST_EVENTS_NOT_OPEN 拒绝（Program.cs L6202-6203），"自动恢复"不成立。
   * 成功路径各自已有 syncRemoteSessionState，故这里只覆盖失败路径。
   */
  private async resyncSessionFromHost(): Promise<void> {
    const remote = await this.adapter.getState().catch(() => null);
    if (remote) this.syncRemoteSessionState(remote);
  }

  /**
   * G7（FAN-926R 执行单 §3.2）：**等待资格**里的"本地可信且未取消的本代唤醒依据"。
   *
   * 只有本代 native 唤醒边沿在场（`resuming` / `resume-ready`），才允许在同一个 Host
   * 尚报 Suspended/旧代未接受时只读等待；无依据的真睡眠、任意 Suspended 都不得借此
   * 放行恢复写（"获得等待资格"也**不等于**获得写权）。
   */
  private hasWakeContext(generation = Math.max(0, Math.floor(this.powerGeneration))): boolean {
    if (this.coordinator.snapshot().phase === 'resuming') return true;
    return this.coordinator.isWakeReady(generation);
  }

  /** G7（§3.2）：把一次控制请求绑定到本地电源代次/意图版本/Host 实例的**有限**上下文。 */
  private admissionContext(action: string, intentRevision: number, generation = Math.max(0, Math.floor(this.powerGeneration))): FanAdmissionContext {
    return { action, intentRevision, generation, hostPid: this.process?.pid ?? 0 };
  }

  /** G7（§3.3.2）：任务身份 = 意图版本 + 电源代次 + Host 实例。 */
  private isAdmissionContextCurrent(context: FanAdmissionContext): boolean {
    return context.intentRevision === this.controlIntentRevision
      && context.generation === Math.max(0, Math.floor(this.powerGeneration))
      && context.hostPid === (this.process?.pid ?? 0);
  }

  /**
   * §3.3.2：每次 await 返回后 / 采纳远端状态前 / 队列真正执行前都必须复核任务身份；
   * 旧回包不得清新 lease、覆盖新 phase 或输出新周期成功。
   */
  private assertAdmissionContextCurrent(context: FanAdmissionContext, stage: string): void {
    if (this.isAdmissionContextCurrent(context)) return;
    fanDiagnosticLog('lifecycle.control-intent-superseded', {
      action: context.action,
      stage,
      intentRevision: context.intentRevision,
      currentIntentRevision: this.controlIntentRevision,
      generation: context.generation,
      currentGeneration: this.powerGeneration,
      state: this.state,
    });
    throw new Error(`FAN_CONTROL_INTENT_SUPERSEDED:${context.action}`);
  }

  /**
   * R2（FAN-926R 裁决 §4.1–§4.3）+ G7（执行单 §3.2/§3.3）：**入队前**的恢复期准入。
   *
   * 为什么必须在入队前：串行队列是渲染侧唯一的生命周期互斥。若把 15 s 等待放进队列，
   * disable/close/新睡眠/新曲线都只能排在它后面（§4.3 明令"不能等待旧 waiter 占队 15 s
   * 后才取消它"）。因此等待在队列之外，取消是**同步**的（`cancelResumeWait`）。
   *
   * **返回 `null` 表示同步判定"不需要等待"**：此时调用方**不得**再 `await`，否则会给
   * `apply()` 与紧随其后的 `suspend()` 之间插入一次微任务让位，破坏"同轮入队的先后顺序"
   * （既有串行语义：先入队者先执行）。只有真的需要探测/等待时才返回 promise。
   */
  private resumeAdmissionWait(action: string, context: FanAdmissionContext): Promise<void> | null {
    if (!this.enabled || this.isDisabledState()) return null;
    // 廉价先决条件：不在睡眠/恢复窗就完全不必探测（普通 apply 零额外请求）。
    // §3.2：本规则只约束**唤醒恢复入口**；正常醒着的显式首次开启仍走既有握手/授权合同。
    if (!this.isSleepWindow()) return null;
    return this.runResumeAdmissionWait(action, context);
  }

  /**
   * G7（§3.2）：准入结果**穷尽**处理——只有 `accepting` 才进入后续控制；`waiting` 进入
   * 共用等待；`superseded`/`terminal` 与探测失败/null 都**终止本请求且不写**（后两者过去
   * 直接 return，调用方随后继续入队，这正是唤醒边沿写入的病灶）。
   */
  private async runResumeAdmissionWait(action: string, context: FanAdmissionContext): Promise<void> {
    // §3.3.4：截止从**第一次准入探测**起算；探测/协调等待/队列内重入共用同一绝对截止。
    const deadlineAt = context.deadlineAt ?? (context.deadlineAt = monotonicNowMs() + this.resumeWaitDeadlineMs);
    const remaining = deadlineAt - monotonicNowMs();
    const probeTimeout = remaining > 0
      ? Math.max(1, Math.floor(Math.min(RESUME_WAIT_MIN_PROBE_TIMEOUT_MS, remaining)))
      : 1;
    let probeFailure: unknown = null;
    const remote = await this.adapter.getState(probeTimeout).catch((error: unknown) => {
      probeFailure = error;
      fanDiagnosticLog('lifecycle.resume-wait-probe-failed', { action, error: describeUnknownError(error) });
      return null;
    });
    // §3.3.2：await 返回后、采纳远端状态（syncRemoteSessionState）**之前**先复核任务身份。
    this.assertAdmissionContextCurrent(context, 'admission-probe');
    if (!remote) {
      // §3.2：探测失败/null 不得当作恢复准入成功。401/403/故障/认证失败是**终态**证据，
      // 无论有没有唤醒依据都不得写；其余探测不可用则只有本代唤醒依据才允许只读等待。
      if (probeFailure && isNonRetryableControlError(probeFailure)) {
        this.rejectResumeAdmission(action, 'terminal-failed', describeUnknownError(probeFailure), context);
      }
      if (!this.hasWakeContext(context.generation)) {
        this.rejectResumeAdmission(action, 'probe-unavailable', 'no-credible-wake-context', context);
      }
      return this.settleResumeWait(action, await this.runBoundedResumeWait(context, deadlineAt));
    }
    const unsupported = unsupportedFanControlError(remote);
    if (unsupported) {
      fanLifecycleEvidence('lifecycle.resume-admission-rejected', { action, reason: 'terminal-failed', detail: 'FAN_UNSUPPORTED', generation: context.generation });
      throw unsupported;
    }
    this.syncRemoteSessionState(remote);
    const verdict = this.classifyControlAdmission(remote, context.requireHcOpen === true, context.manualRecovery === true);
    if (verdict === 'accepting') {
      fanLifecycleEvidence('lifecycle.resume-admission-accepting', {
        action,
        generation: context.generation,
        intentRevision: context.intentRevision,
        wakeContext: this.hasWakeContext(context.generation),
        state: this.stateValue,
      });
      return;
    }
    if (verdict === 'terminal') {
      this.rejectResumeAdmission(action, 'terminal-failed', `${remote.state ?? 'unknown'}/${remote.resumePhase ?? 'unknown'}`, context);
    }
    if (verdict === 'superseded') {
      this.rejectResumeAdmission(action, 'superseded', String(remote.state ?? remote.powerState ?? 'superseded'), context);
    }
    this.settleResumeWait(action, await this.runBoundedResumeWait(context, deadlineAt));
  }

  /** G7（§3.2）：不允许写入的准入结论——先落免门证据，再以明确非重试错误终止本请求。 */
  private rejectResumeAdmission(
    action: string,
    reason: 'superseded' | 'terminal-failed' | 'probe-unavailable',
    detail: string,
    context: FanAdmissionContext,
  ): never {
    fanLifecycleEvidence('lifecycle.resume-admission-rejected', {
      action,
      reason,
      detail,
      generation: context.generation,
      currentGeneration: this.powerGeneration,
      intentRevision: context.intentRevision,
      wakeContext: this.hasWakeContext(context.generation),
      state: this.stateValue,
    });
    throw new Error(`FAN_RESUME_ADMISSION_${reason.toUpperCase().replace(/-/g, '_')}:${action}:${detail}`);
  }

  /** §4.2/§3.2：等待未成功 ⇒ 以明确、**可重试**的错误终止本请求（不写硬件）。 */
  private settleResumeWait(action: string, outcome: FanResumeWaitOutcome): void {
    if (outcome.ok) return;
    if (outcome.detail === 'FAN_UNSUPPORTED') throw new FanApiError('HC 当前设备未声明可用风扇能力', 409, 'FAN_UNSUPPORTED');
    throw new Error(`FAN_RESUME_WAIT_${outcome.reason.toUpperCase().replace(/-/g, '_')}:${outcome.detail}`);
  }

  /**
   * §4.2：同一 Host 实例/恢复代际/**同一意图**共享一个等待任务；重复通知合并。
   * §3.3.3：意图版本进入 key——已 cancelled 但尚未退出的同 key waiter 不会被新意图复用。
   */
  private resumeWaitKey(context: FanAdmissionContext): string {
    return `fan-resume-wait-${this.sessionGeneration}-${context.generation}-${context.intentRevision}-${context.hostPid}`;
  }

  private runBoundedResumeWait(context: FanAdmissionContext, deadlineAt: number): Promise<FanResumeWaitOutcome> {
    const key = this.resumeWaitKey(context);
    const existing = this.resumeWaitTask;
    // §3.3.3：只能复用**仍然有效**的同 key waiter；已 cancelled 的旧任务只能结算旧请求。
    if (existing && existing.key === key && !existing.cancelled) return existing.promise;
    if (existing) this.cancelResumeWait('superseded-generation');
    const task: FanResumeWaitTask = {
      key,
      cancelled: null,
      cancel: () => undefined,
      promise: Promise.resolve({ ok: false, reason: 'cancelled', detail: 'not-started', probes: 0, elapsedMs: 0, intervalMs: 0 }),
    };
    task.cancel = (reason: string) => { task.cancelled = reason; };
    task.promise = this.resumeWaitLoop(task, context, deadlineAt).finally(() => {
      if (this.resumeWaitTask === task) this.resumeWaitTask = null;
    });
    this.resumeWaitTask = task;
    return task.promise;
  }

  /**
   * R2（§4.3）：**同步**失效（不等队列、不等定时器）。真正的循环退出发生在下一次
   * 循环检查点（≤ 一个校验过的间隔，250–1000 ms），并只写一条 cancelled 证据。
   */
  private cancelResumeWait(reason: string): void {
    const task = this.resumeWaitTask;
    if (!task || task.cancelled) return;
    task.cancel(reason);
    fanDiagnosticLog('lifecycle.resume-wait-cancel-requested', { reason, key: task.key, generation: this.powerGeneration });
  }

  /**
   * R2（§4.2）+ G7（§3.3.2/§3.3.4）：单一等待循环——只读 `/api/state`，最多一个在途读，
   * **共用**一个单调绝对截止；每次 await 返回后复核任务身份，被取代即刻结束且不写。
   */
  private async resumeWaitLoop(task: FanResumeWaitTask, context: FanAdmissionContext, deadlineAt: number): Promise<FanResumeWaitOutcome> {
    const startedAt = monotonicNowMs();
    let probes = 0;
    let intervalMs = RESUME_WAIT_MIN_INTERVAL_MS;
    let lastDetail = 'resuming';
    // §4.4：无论详细日志开关如何，这一个 started 必须可事后核对（白名单事件）。
    fanLifecycleEvidence('lifecycle.resume-wait-started', {
      action: context.action,
      generation: context.generation,
      session: this.sessionGeneration,
      deadlineMs: this.resumeWaitDeadlineMs,
      state: this.stateValue,
    });
    const done = (outcome: FanResumeWaitOutcome): FanResumeWaitOutcome => this.finishResumeWait(context.action, outcome);
    const failed = (reason: FanResumeWaitOutcome['reason'], detail: string): FanResumeWaitOutcome =>
      done({ ok: false, reason, detail, probes, elapsedMs: monotonicNowMs() - startedAt, intervalMs });
    for (;;) {
      if (task.cancelled) {
        return failed('cancelled', task.cancelled);
      }
      // §3.3.2：同一个绝对截止内也不接受被新意图取代的旧任务继续推进。
      if (!this.isAdmissionContextCurrent(context)) {
        return failed('superseded', 'context-changed');
      }
      const remaining = deadlineAt - monotonicNowMs();
      if (remaining <= 0) {
        return failed('deadline', lastDetail);
      }
      let remote: FanState | null = null;
      let failure: unknown = null;
      try {
        // §3.3.4：GET 超时取**剩余余额**上限，不得以 max(500, remaining) 超出最后余额。
        remote = await this.adapter.getState(Math.max(1, Math.floor(Math.min(remaining, RESUME_WAIT_MAX_PROBE_TIMEOUT_MS))));
      } catch (error) {
        failure = error;
      }
      probes += 1;
      if (task.cancelled) return failed('cancelled', task.cancelled);
      if (!this.isAdmissionContextCurrent(context)) return failed('superseded', 'context-changed');
      if (failure) {
        // §4.1：401/403、故障锁定、待清理、路由冲突……不是"恢复中"，不得继续等。
        if (isNonRetryableControlError(failure)) {
          return failed('terminal-failed', describeUnknownError(failure));
        }
        lastDetail = describeUnknownError(failure);
      } else if (remote) {
        if (unsupportedFanControlError(remote)) return failed('terminal-failed', 'FAN_UNSUPPORTED');
        this.syncRemoteSessionState(remote);
        const verdict = this.classifyControlAdmission(remote, context.requireHcOpen === true, context.manualRecovery === true);
        if (verdict === 'accepting') {
          return done({ ok: true, reason: 'accepted', detail: 'control-accepting', probes, elapsedMs: monotonicNowMs() - startedAt, intervalMs });
        }
        if (verdict === 'superseded') {
          return failed('superseded', String(remote.state ?? remote.powerState ?? 'superseded'));
        }
        if (verdict === 'terminal') {
          return failed('terminal-failed', `${remote.state ?? 'unknown'}/${remote.resumePhase ?? 'unknown'}`);
        }
        // §4.2：宿主建议间隔经校验后限于 250–1000 ms；非法/缺失一律 250 ms（不忙轮询）。
        const suggested = Number(remote.retryAfterMs);
        intervalMs = Number.isFinite(suggested) && suggested >= RESUME_WAIT_MIN_INTERVAL_MS && suggested <= RESUME_WAIT_MAX_INTERVAL_MS
          ? Math.floor(suggested)
          : RESUME_WAIT_MIN_INTERVAL_MS;
        lastDetail = `${remote.resumePhase ?? remote.state ?? 'Resuming'}`;
      }
      const sleepFor = Math.min(intervalMs, deadlineAt - monotonicNowMs());
      if (sleepFor <= 0) {
        return failed('deadline', lastDetail);
      }
      await sleepMs(sleepFor);
    }
  }

  /**
   * §4.2 达到即结束：停止计时器/重试、结束 connecting、给出**明确可重试**状态与原因；
   * 不杀 Host、不把设备标成不支持、不延长宿主恢复总预算。§4.4：唯一终态证据免门落盘。
   */
  private finishResumeWait(action: string, outcome: FanResumeWaitOutcome): FanResumeWaitOutcome {
    const evidence = outcome.ok
      ? 'lifecycle.resume-wait-completed'
      : outcome.reason === 'cancelled' || outcome.reason === 'superseded'
        ? 'lifecycle.resume-wait-cancelled'
        : 'lifecycle.resume-wait-terminal-failed';
    fanLifecycleEvidence(evidence, {
      action,
      generation: Math.max(0, Math.floor(this.powerGeneration)),
      session: this.sessionGeneration,
      probes: outcome.probes,
      elapsedMs: Math.round(outcome.elapsedMs),
      ok: outcome.ok,
      reason: outcome.ok ? 'accepted' : outcome.reason,
      detail: outcome.ok ? 'control-accepting' : outcome.detail,
      retryable: !outcome.ok && (outcome.reason === 'deadline' || outcome.reason === 'terminal-failed'),
      state: this.stateValue,
    });
    if (!outcome.ok && (outcome.reason === 'deadline' || outcome.reason === 'terminal-failed')) {
      // §4.2：结束"connecting"、给出明确**可重试**状态（不是永久不可点）。新一代睡眠/关闭由
      // 取消路径负责（outcome=cancelled/superseded，不写状态）；这里只剩 stopped/disabled 不动。
      if (this.stateValue !== 'stopped' && this.stateValue !== 'disabled') {
        this.setState('awaiting-control');
      }
    }
    return outcome;
  }

  private snapshotGenerationEvidence(remote: FanState): FanSnapshotGenerationEvidence {
    return resolveFanSnapshotGeneration(remote, {
      powerGeneration: this.powerGeneration,
      initialAwakeGeneration: this.initialAwakePowerGeneration,
      explicitOpenGeneration: this.explicitOpenBinding?.sessionGeneration === this.sessionGeneration
        ? this.explicitOpenBinding.powerGeneration : null,
    });
  }

  private hostPowerGenerationForAdmission(remote: FanState): number | null {
    return this.snapshotGenerationEvidence(remote).generation;
  }

  private bindExplicitHcOpen(remote: FanState, generation: number): void {
    if (generation !== this.powerGeneration || !isNoPowerOperationReceipt(remote)
        || !isCompleteAdmissibleHcSession(remote)) return;
    this.explicitOpenBinding = { sessionGeneration: this.sessionGeneration, powerGeneration: generation };
    fanDiagnosticLog('lifecycle.explicit-hc-open-generation-bound', { generation, sessionGeneration: this.sessionGeneration, hostResumeReceiptCreated: false });
  }

  /**
   * §4.1 判定 + G7（§3.2）：只认"恢复中"是可等待的。返回
   * `waiting`（明确 Resuming / 过渡态）· `accepting`（当前代次已可接受控制）·
   * `superseded`（无唤醒依据的睡眠已接管）· `terminal`（故障/未知/待清理/已停止）。
   *
   * G7 关键更正：同一 Host 在本代唤醒窗内**尚未接受** resume 时仍会报 Suspended——
   * 这时是"只读等待"（`waiting`），不是"新一代睡眠已接管"。只有**没有**可信本代唤醒
   * 依据（真睡眠/任意 Suspended）才算 `superseded`。
   */
  private classifyControlAdmission(remote: FanState, requireHcOpen = false, allowSuspendedWait = false): 'waiting' | 'accepting' | 'superseded' | 'terminal' {
    if (remote.unknownState === true) return 'terminal';
    if (remote.hcCloseCleanupPending === true) return 'terminal';
    const state = String(remote.state ?? '');
    const powerState = String(remote.powerState ?? '');
    if (state === 'FaultLocked' || state === 'ListenerFaultLocked' || state === 'AwaitingCloseCleanup') return 'terminal';
    const wakeContext = this.hasWakeContext();
    if (state === 'Stopped') return 'superseded';
    if (state === 'Suspended' || powerState === 'Suspended' || powerState === 'Suspending') {
      return wakeContext || allowSuspendedWait ? 'waiting' : 'superseded';
    }
    // FAN-943: positive power receipts must match exactly; neither old nor future Ready is current.
    const evidence = this.snapshotGenerationEvidence(remote);
    if (evidence.freshness === 'invalid') return 'terminal';
    const mismatchedGeneration = evidence.freshness === 'stale' || evidence.freshness === 'future';
    const phase = String(remote.resumePhase ?? state);
    if (hasFailedFanOperation(remote)) return 'terminal';
    if (hasBlockingFanTransition(remote)) return 'waiting';
    if (mismatchedGeneration) return 'waiting';
    // FAN-933 §2.1(4)：唤醒后「新控制接入」的 rearm 比既有 apply 准入更严——必须看到
    // HC 会话真的重开（openCalled && openEventsCalled），否则 AwaitingControl/Ready
    // 也可能只是一个还没 Open 的空壳快照。既有 apply 准入不传此参数，语义不变。
    if (requireHcOpen && !(remote.openCalled === true && remote.openEventsCalled === true)) return 'waiting';
    // An explicit false is an authoritative write prohibition, not missing telemetry.
    if (remote.controlAccepting === false) return 'waiting';
    if (remote.controlAccepting === true && (state === 'Ready' || state === 'AwaitingControl')) return 'accepting';
    if (remote.controlAccepting === undefined && (phase === 'Ready' || phase === 'AwaitingControl')
        && (state === 'Ready' || state === 'AwaitingControl')) return 'accepting';
    return 'waiting';
  }

  /**
   * FAN-933 §2.1(2)：远端快照是否已经是**可接管的 HC 会话**（不只是"能读状态"）。
   * 六个条件全成立才算已恢复：state ∈ {AwaitingControl, Ready}、powerState 不是
   * Suspending/Suspended/Resuming、unknownState=false、hcCloseCleanupPending=false，
   * 且 openCalled/openEventsCalled 都为 true。
   */
  private remoteControlAdmissible(remote: FanState): boolean {
    if (remote.unknownState === true || remote.hcCloseCleanupPending === true) return false;
    const state = String(remote.state ?? '');
    const powerState = String(remote.powerState ?? '');
    if (powerState === 'Suspending' || powerState === 'Suspended' || powerState === 'Resuming') return false;
    if (state !== 'AwaitingControl' && state !== 'Ready') return false;
    return this.classifyControlAdmission(remote, true) === 'accepting';
  }

  /**
   * FAN-933 §2.1：唤醒后「明确要求接管」（自动启动偏好为开 / 用户明确点击）时的会话重开。
   *
   * 为什么需要：FAN-927 规定没有活动意图时 `resume()` 立即 no-work（该规则必须保留）；
   * 而常驻 Host 在睡眠时已按 HC 顺序 `Close()`，唤醒后 `openCalled/openEventsCalled`
   * 仍为 false。旧实现让 `startInternal` 的短路口直接返回 `allowed=true`，把尚未重开
   * HC 的 Host 当成可写，随后 `/api/acquire-control` 被 Host 以 `POWER_SUSPENDING` 拒绝
   * ——表现为"卡死"。本方法在放行前先按当前 generation 做一次受控恢复。
   *
   * 只服务明确控制意图；普通页面挂载与普通 `resume()` 的零动作语义不受影响。
   */
  /**
   * R4: recover a missed Fan wake edge using native's current same-generation Ready.
   * A click is not wake evidence. This never grants HC writes or clears a Host fault;
   * the existing Host rearm must still prove Open/OpenEvents before acquire/enable.
   */
  private async observeCurrentNativeWakeForControl(context: FanAdmissionContext, allowManualRescue = false): Promise<boolean> {
    if (context.generation <= 0 || this.coordinator.isWakeReady(context.generation)) return false;
    // An explicit Fan click is the one safe way to recover when native missed
    // the one-shot S0 wake message. The native command only promotes a stale
    // same-generation Suspended phase; it rejects an active Suspending phase.
    // It does not write HC itself—the normal Host resume/Open/OpenEvents chain
    // below remains the only path to acquire/enable.
    let manualNativeWake: FanManualWakeResult | null = null;
    if (allowManualRescue && this.manualWakeRequestedGeneration !== context.generation) {
      fanLifecycleEvidence('lifecycle.manual-wake-recovery-requested', {
        action: context.action,
        generation: context.generation,
        source: 'fan.manual-control',
      });
      manualNativeWake = await this.requestManualWake(context.generation);
      this.assertAdmissionContextCurrent(context, 'native-manual-wake');
      if (manualNativeWake?.ok === true && manualNativeWake.generation === context.generation) {
        this.manualWakeRequestedGeneration = context.generation;
        fanLifecycleEvidence('lifecycle.manual-wake-snapshot-adopted', {
          action: context.action,
          generation: context.generation,
          source: 'native.power.fanManualWake',
          nativePhase: manualNativeWake.phase ?? 'ready',
          reason: manualNativeWake.reason ?? 'interactive-click-after-stale-suspended',
          hcWritesAdmitted: false,
        });
      } else if (manualNativeWake) {
        fanLifecycleEvidence('lifecycle.manual-wake-recovery-failed', {
          action: context.action,
          generation: context.generation,
          source: 'native.power.fanManualWake',
          reason: manualNativeWake.reason ?? 'native-manual-wake-rejected',
          nativePhase: manualNativeWake.phase ?? 'unknown',
        });
      }
    }
    const native = await this.readNativePowerState().catch(() => null);
    this.assertAdmissionContextCurrent(context, 'native-wake-snapshot');
    // FAN-938 R6 §P2：唤醒依据必须用 **Fan 专属、按代次持久化的系统唤醒事实**，
    // 不能用输入链状态。`inputReady` 只是输入链是否恢复（native 在真实睡眠时仍可保留
    // 它为 true，见 main.cpp:58740-58752），不能单独证明系统已唤醒。真正的系统唤醒事实
    // 是 native `power.lifecycle.resumeReady`（main.cpp 在真实唤醒分支 set、在睡眠分支
    // clear，按当前电源代次持久化）。显式 Fan IPC 的成功回执也是同代用户操作证据。
    // 另：当前实际 suspend 操作仍在进行（phase==='suspending'）时必须恒拒绝——那是
    // 「正在睡」而不是「已醒」，任何 inputReady/resumeReady 都不能在此放行写入。
    const sameGeneration = (native !== null && native.generation === context.generation)
      || (manualNativeWake?.ok === true && manualNativeWake.generation === context.generation);
    const phaseReady = native?.phase === 'ready' || manualNativeWake?.phase === 'ready';
    const suspendInProgress = native?.phase === 'suspending' || manualNativeWake?.phase === 'suspending';
    const fanWakeFact = native?.resumeReady === true;
    // Automatic recovery remains fail-closed: inputReady and a stale Suspended
    // phase are not wake evidence. An explicit native Fan promotion is the
    // manual contract; the Host resume/Open/OpenEvents chain still has to
    // complete before any write.
    const manualStaleWake = allowManualRescue && sameGeneration && !suspendInProgress
      && (manualNativeWake?.ok === true || native?.phase === 'suspended'
        || native?.phase === 'resuming' || phaseReady);
    const automaticWake = sameGeneration && !suspendInProgress
      && (phaseReady || fanWakeFact);
    if (!sameGeneration || (!automaticWake && !manualStaleWake)) {
      fanLifecycleEvidence('lifecycle.manual-wake-snapshot-rejected', {
        action: context.action, generation: context.generation,
        nativeGeneration: native?.generation ?? null, nativePhase: native?.phase ?? 'unavailable',
        nativeResumeReady: native?.resumeReady ?? null,
        nativeInputReady: native?.inputReady ?? null,
        manualRescue: allowManualRescue,
      });
      return false;
    }
    // The device was closed on sleep; retire the old lease before rearming the same Host.
    // Manual rescue intentionally does not fabricate native resume-ready. It opens
    // a local resuming window; manual-resume-ready is emitted only after the Host
    // proves Open/OpenEvents in runRearmAfterWake().
    if (manualStaleWake && !fanWakeFact && !phaseReady) {
      this.observePowerBoundary('manual-resume-start', context.generation);
    } else {
      this.observePowerBoundary('resuming', context.generation);
      this.observePowerBoundary('resume-ready', context.generation);
    }
    fanLifecycleEvidence('lifecycle.manual-wake-snapshot-adopted', {
      action: context.action, generation: context.generation,
      source: manualStaleWake ? 'explicit-control-after-stale-suspended' : 'native.power.lifecycle',
      nativePhase: native?.phase ?? 'unavailable', nativeResumeReady: native?.resumeReady ?? null,
      nativeInputReady: native?.inputReady ?? null,
      manualRescue: manualStaleWake,
      hcWritesAdmitted: false,
    });
    if (manualStaleWake) {
      fanLifecycleEvidence('lifecycle.manual-wake-recovery-accepted', {
        action: context.action,
        generation: context.generation,
        nativePhase: native?.phase ?? 'unavailable',
        reason: 'same-generation-explicit-control-after-stale-suspended',
        hcWritesAdmitted: false,
      });
    }
    return true;
  }

  private async rearmAfterWakeForControlAdmission(reason: string, allowManualRescue = false): Promise<void> {
    if (!this.enabled || this.isDisabledState()) return;
    // 无常驻 Host（冷启动 / 已退出）⇒ 走既有 spawn 路径，不需要 rearm。
    if (!this.process) return;
    const generation = Math.max(0, Math.floor(this.powerGeneration));
    const manualRecovery = allowManualRescue || reason === 'manual-control' || reason === 'manual-missed-wake-edge';
    let manualRescueStarted = false;
    // §2.1(1)：未到本代 resume-ready（或冷启动 generation=0）时只给可重试的"恢复中"，
    // 不发 HTTP、不碰 HC。
    if (generation > 0 && !this.coordinator.isWakeReady(generation)) {
      const context = this.admissionContext('start-explicit-control', this.controlIntentRevision, generation);
      context.manualRecovery = manualRecovery;
      manualRescueStarted = await this.observeCurrentNativeWakeForControl(context, manualRecovery);
    }
    if (generation <= 0) {
      fanDiagnosticLog('lifecycle.rearm-after-wake-deferred', { reason, generation, state: this.stateValue, recoveryState: this.recoveryState });
      return;
    }
    if (!this.coordinator.isWakeReady(generation) && !manualRescueStarted) {
      // FAN-938 R5 §3：手动/自动恢复请求在这里**无条件受理**——登记恢复意图并武装唯一
      // owner，由 owner 在真实睡眠、认证、设备互斥与 HC 会话检查成立后再推动写入。
      // 不再抛 `wake-not-ready`（现场失败形态：native/Host 陈旧 Suspended 时被永久拒绝）。
      this.noteRecoveryIntentPending(reason, generation, manualRecovery);
      fanDiagnosticLog('lifecycle.rearm-after-wake-deferred', {
        reason, generation, manualRecovery, state: this.stateValue, recoveryState: this.recoveryState,
      });
      return;
    }
    if (!this.coordinator.isWakeReady(generation) && manualRecovery) {
      // Explicit control opened a local resuming window. Continue to the same
      // Host resume/Open/OpenEvents admission path; the coordinator write gate
      // is opened only after the Host reaches an accepting state below.
      this.noteRecoveryIntentPending(reason, generation, true);
    }
    // §2.1(5)：同一 Host + 同一 generation 只允许一个在途 rearm；自动启动/手动点击/
    // renderer 重建同时到达时共用同一个 promise，不重复 `/api/resume`。
    const existing = this.rearmTask;
    const intentRevision = this.controlIntentRevision;
    const sessionGeneration = this.sessionGeneration;
    if (existing && existing.generation === generation) {
      if (existing.intentRevision === intentRevision && existing.sessionGeneration === sessionGeneration) return existing.promise;
      // A new curve must not inherit the cancelled old intent's waiter/result.
      // Serialize behind its settlement (outside the write queue); never run a
      // second same-generation Host resume owner concurrently.
      await existing.promise.catch(() => undefined);
      this.assertIntentStillCurrent(intentRevision, 'rearm-new-intent');
      if (this.powerGeneration !== generation || this.sessionGeneration !== sessionGeneration) {
        throw new Error('FAN_CONTROL_INTENT_SUPERSEDED:rearm-session');
      }
      if (this.rearmTask === existing) this.rearmTask = null;
      return this.rearmAfterWakeForControlAdmission(reason, manualRecovery);
    }
    const task = { generation, intentRevision, sessionGeneration, promise: this.runRearmAfterWake(generation, reason, manualRecovery) };
    this.rearmTask = task;
    try {
      await task.promise;
    } catch (error) {
      // Keep this invariant in the bridge: direct start/curve callers also
      // retain recovery after a bounded wait. A UI-specific catch is insufficient.
      if (this.isResumeReplayCurrent(generation, intentRevision)
          && this.hasActiveControlIntent() && this.hasWakeContext(generation)
          && /^FAN_RESUME_WAIT_DEADLINE:/.test(describeUnknownError(error))) {
        this.noteRecoveryIntentPending(reason, generation, manualRecovery);
      }
      throw error;
    } finally {
      if (this.rearmTask === task) this.rearmTask = null;
    }
  }

  private async runRearmAfterWake(generation: number, reason: string, manualRecovery = false): Promise<void> {
    const context = this.admissionContext('rearm-after-wake', this.controlIntentRevision, generation);
    context.requireHcOpen = true;
    context.manualRecovery = manualRecovery;
    // (2) 先只读探测：已经可接管就直接沿用现有控制门，不发 `/api/resume`。
    const probe = await this.adapter.getState(RESUME_WAIT_MIN_PROBE_TIMEOUT_MS).catch((error: unknown) => {
      if (isNonRetryableControlError(error) || isExternalFanControlConflict(error)) throw error;
      return null;
    });
    this.assertAdmissionContextCurrent(context, 'rearm-probe');
    if (probe) {
      const unsupported = unsupportedFanControlError(probe);
      if (unsupported) throw unsupported;
      this.syncRemoteSessionState(probe);
    }
    // FAN-944: native may have slept BEFORE this Host existed. A never-opened
    // Host cannot return a resume receipt for that earlier sleep. Recheck the
    // current native awake/write boundary, then enter the original Open chain.
    // Existing/closed/dirty sessions still use their real recovery receipt.
    if (probe && isColdUnopenedFanHost(probe) && this.coordinator.isWakeReady(generation)) {
      const native = await this.readNativePowerState().catch(() => null);
      this.assertAdmissionContextCurrent(context, 'cold-host-native-probe');
      if (native?.generation === generation && native.phase === 'ready' && native.hardwareWritesAllowed === true) {
        this.opened = false; this.eventsOpened = false; this.lease = null;
        this.beginCoordinatorSession();
        this.setState('awaiting-control');
        fanLifecycleEvidence('lifecycle.cold-host-hc-open-deferred', {
          generation, remoteState: probe.state, nativePhase: native.phase,
          hcOpenRequired: true, hostResumeReceiptCreated: false, controlWriteGranted: false,
        });
        return;
      }
    }
    // FAN-938 R7：手动救援已经观察到本地/系统的睡眠边界时，远端 Ready 可能只是
    // 睡眠前的陈旧快照。不能把它当作本代 HC 会话已经重开，否则会跳过 `/api/resume`
    // 和 Open/OpenEvents，正是“看起来 Ready、实际唤醒后风扇不再写入”的漏口。
    const hostGeneration = probe ? this.hostPowerGenerationForAdmission(probe) : null;
    const manualBoundaryNeedsFreshHostResume = manualRecovery
      && this.coordinator.snapshot().phase === 'resuming'
      && hostGeneration !== generation;
    if (probe && this.remoteControlAdmissible(probe) && !manualBoundaryNeedsFreshHostResume) {
      // R4: an already-open Host still needs its renderer coordinator rebound after sleep.
      if (manualRecovery && !this.coordinator.isWakeReady(generation)) {
        this.observePowerBoundary('manual-resume-ready', generation);
      }
      this.beginCoordinatorSession();
      this.admitCoordinatorHcReady();
      if (this.stateValue !== 'stopped' && this.stateValue !== 'disabled') this.setState('awaiting-control');
      // FAN-938 R6 §P1：这里只代表「会话已就绪」，**不得**清恢复 owner——曲线尚未写入。
      // 提前 settle 会让随后的写入失败再也无人接手（现场：1 次尝试 0 次成功即消失）。
      fanDiagnosticLog('lifecycle.rearm-after-wake-noop', { reason, generation, state: probe.state });
      return;
    }
    // (3) 受控 `/api/resume`：让 Host 唯一恢复 owner 按 HC 顺序执行 Open/OpenEvents。
    //     这里串行发起，绝不与 acquire/open/open-events 并发；失败不吞成成功，
    //     交给下面的有界等待给出明确结论。
    // A same-generation Host may already be rebuilding (native F5 or the
    // previous bounded attempt). Reuse that worker instead of issuing another
    // resume after each wait deadline. Missing/stale generation or Suspended
    // never proves an active worker; those still need an explicit Host resume.
    const hostAlreadyResuming = !!probe && hostGeneration === generation
      && probe.unknownState !== true && probe.hcCloseCleanupPending !== true
      && probe.state === 'Resuming' && probe.resumePhase === 'Resuming';
    if (!hostAlreadyResuming) {
      try {
        const accepted = await this.adapter.resume({ generation, source: 'renderer-control-admission', reason });
        this.assertAdmissionContextCurrent(context, 'rearm-resume-response');
        this.syncRemoteSessionState(accepted);
      } catch (error) {
        fanDiagnosticLog('lifecycle.rearm-after-wake-resume-failed', {
          reason, generation, error: describeUnknownError(error),
        });
        // A rejected/expired receipt is NOT an accepted rebuild. Preserve the
        // terminal API code instead of masking it as a 15s Ready deadline.
        if (isNonRetryableControlError(error) || isExternalFanControlConflict(error)) throw error;
      }
    }
    // (4) 有界等待同代收敛，且必须看到 HC 会话真的重开（requireHcOpen）。
    //     直接调用 runResumeAdmissionWait 而不是 resumeAdmissionWait：后者只在
    //     coordinator 仍处 suspending/suspended/resuming 时才武装，而本场景恰恰可能在
    //     `power.resumed` 之后（phase 已离开 resuming），那正是本次事故的漏口。
    await this.runResumeAdmissionWait('rearm-after-wake', context);
    this.assertAdmissionContextCurrent(context, 'rearm-completed');
    if (manualRecovery && !this.coordinator.isWakeReady(generation)) {
      // The remote Host has now proved its HC session. This is the only point
      // at which an explicit rescue may establish the local write boundary.
      this.observePowerBoundary('manual-resume-ready', generation);
    }
    // (5) 恢复完成才把会话标记为本代已准入；此后才允许 acquire-control 与曲线 apply。
    //     必须先为本代重建协调器会话：`observePowerGeneration` 在代次前进时已把
    //     coordinator 的 hostSession 清空，沿用旧会话对象会让后续 acquire 被
    //     `requireCurrentSession(旧session, 新代次)` 拒掉。
    this.beginCoordinatorSession();
    this.admitCoordinatorHcReady();
    this.admittedPowerGeneration = generation;
    if (this.stateValue !== 'stopped' && this.stateValue !== 'disabled') this.setState('awaiting-control');
    // FAN-938 R6 §P1：同上——rearm 完成只证明本代会话可接管，**曲线是否写入成功**由
    // recovery owner 在写入后单独收敛，绝不在此提前清 owner。
    fanLifecycleEvidence('lifecycle.rearm-after-wake-completed', { reason, generation });
  }

  // ── FAN-938 R5 §3：单一 Fan 恢复 owner ────────────────────────────────────────
  /**
   * R5 §3「分开恢复请求与设备写入许可」：把一次手动/自动恢复请求**无条件**登记为意图。
   * 这里只做登记与合并（同一代次不重置进度），真正的设备写入许可由 runRecoveryTick
   * 依据真实睡眠、认证、设备互斥与 HC 会话检查单独判定，绝不在此直接写设备。
   */
  private noteRecoveryIntentPending(reason: string, generation: number, manual = false): void {
    if (!this.enabled || this.isDisabledState() || generation <= 0) return;
    const existing = this.recoveryIntent;
    if (existing !== null && existing.generation === generation) {
      existing.reason = reason;
      existing.manual = existing.manual || manual;
      if (this.recoveryState !== 'recovering') this.recoveryAttempts = 0;
    } else {
      this.recoveryIntent = { generation, reason, manual };
      this.recoveryAttempts = 0;
    }
    this.recoveryState = 'queued';
    fanDiagnosticLog('lifecycle.recovery-intent-registered', {
      reason, generation, manual, recoveryState: this.recoveryState, recoveryAttempts: this.recoveryAttempts,
    });
    if (!this.recoveryRetryTimer) this.scheduleRecoveryTick(this.nextRecoveryBackoff());
  }

  private nextRecoveryBackoff(): number {
    // R6 §P1：runRecoveryTick 每轮**起始**自增一次 attempts，故退避索引取 attempts-1，
    // 使「登记后首轮 250ms → 失败后 500ms」的节奏与 RECOVERY_BACKOFF_MS 一致。
    const index = Math.min(Math.max(0, this.recoveryAttempts - 1), RECOVERY_BACKOFF_MS.length - 1);
    return RECOVERY_BACKOFF_MS[index];
  }

  private scheduleRecoveryTick(delayMs: number): void {
    if (this.recoveryRetryTimer) return;
    // R6 §P1：定时器回调捕获当前 owner token；真正执行仍走唯一 recoveryTask 串行链，
    // 到执行时 token 已变（close/disable/新曲线）则旧 tick 自动失效，绝不写入。
    const token = this.recoveryToken;
    const timer = setTimeout(() => {
      this.recoveryRetryTimer = null;
      this.launchRecoveryTick(token);
    }, Math.max(0, delayMs));
    (timer as unknown as { unref?: () => void }).unref?.();
    this.recoveryRetryTimer = timer;
  }

  private stopRecoveryLoop(reason: string): void {
    if (this.recoveryRetryTimer) {
      clearTimeout(this.recoveryRetryTimer);
      this.recoveryRetryTimer = null;
    }
    if (this.recoveryIntent) {
      fanDiagnosticLog('lifecycle.recovery-loop-stopped', {
        reason, generation: this.recoveryIntent.generation, recoveryState: this.recoveryState,
      });
    }
  }

  /** close/disable/新代次取代：清定时器 + 意图 + 进度，并令任何在途旧 tick 失效。 */
  private cancelRecovery(reason: string): void {
    const generation = this.recoveryIntent?.generation ?? null;
    this.stopRecoveryLoop(reason);
    if (generation !== null) fanDiagnosticLog('lifecycle.recovery-cancelled', { reason, generation });
    this.recoveryIntent = null;
    this.recoveryAttempts = 0;
    this.recoveryState = 'cancelled';
    // R6 §P1：取消即递增 owner 世代，跨 await 的旧 tick 在下一个复核点自行退出。
    this.recoveryToken += 1;
  }

  /** 本代恢复任务达成（曲线已按原机型写入成功，或明确无最新曲线）时收敛 owner。 */
  private settleRecoveryTaskIfAny(reason = 'settled'): void {
    if (this.recoveryRetryTimer) {
      clearTimeout(this.recoveryRetryTimer);
      this.recoveryRetryTimer = null;
    }
    // R6 §P1：先递增 owner 世代，使并发在途的旧 tick 立即失效，再清意图。
    this.recoveryToken += 1;
    if (!this.recoveryIntent) return;
    fanDiagnosticLog('lifecycle.recovery-controlled', {
      reason, generation: this.recoveryIntent.generation, attempts: this.recoveryAttempts,
    });
    this.recoveryIntent = null;
    this.recoveryAttempts = 0;
    this.recoveryState = 'controlled';
  }

  /**
   * FAN-938 R6.1 §3：Host 进程丢失时的**安全重建**。绝不直接 spawn 第二个硬件 writer：
   * 复用既有 `startInternal`（`assertReachableBeforeShortCircuit` 可达性续证 + 有界等待旧
   * 进程退出 → `launcher.start` → `recoverPreviousHost` fail-closed 核验旧实例身份/退出）。
   * 整个过程串到渲染侧操作队列，与 apply/close/disable 串行；成功后由既有 rearm/apply 走
   * 重新认证、Open/OpenEvents、申请本代控制并应用最新曲线。失败原样抛出，由本轮 owner
   * 的 catch 统一分类（瞬态退避 / 终态 needs-attention），不在此吞成成功。
   */
  private async ensureHostForRecovery(token: number, generation: number): Promise<void> {
    fanDiagnosticLog('lifecycle.recovery-host-lost-rebuild', {
      generation, state: this.stateValue, recoveryState: this.recoveryState,
    });
    try {
      await this.enqueue(async () => {
        // 入队等待期间 close/disable/新睡眠到达 → 不再重建（token 已变/意图已清）。
        if (token !== this.recoveryToken || !this.recoveryIntent) return;
        await this.startInternal();
      });
    } catch (error) {
      fanDiagnosticLog('lifecycle.recovery-host-rebuild-failed', {
        generation, state: this.stateValue, error: describeUnknownError(error),
      });
      throw error;
    }
    fanLifecycleEvidence('lifecycle.recovery-host-rebuilt', {
      generation, pid: this.process?.pid ?? 0, state: this.stateValue,
    });
  }

  /**
   * R5 §3「保持控制意图和持续恢复」：单次准入超时/未就绪**不**结束恢复任务。
   * owner 每一轮最多做一次状态探测（经 rearmAfterWakeForControlAdmission）；达成本代
   * 设备写入许可后重开并应用既有曲线，否则按 250/500/1000/2000/5000 ms 退避继续。
   * 认证/外部 owner 这类不可自动高速重试的终态转 needs-attention，保留意图与手动入口。
   */
  private async runRecoveryTick(token: number): Promise<void> {
    const intent = this.recoveryIntent;
    // R6 §P1：执行入口即复核 owner 世代——旧 tick 在入队/唤醒后 token 已变则直接退出。
    // FAN-938 R6.1 §3：`!this.process` **不再**等同 no-intent。Host 进程丢失时意图与
    // enabled 仍在，必须转入安全重建（见下方 ensureHostForRecovery），不能静默
    // stopRecoveryLoop——否则页面停在"正在恢复"却永不重建 Host/重写曲线。
    if (token !== this.recoveryToken || !intent || !this.enabled || this.isDisabledState()) {
      this.stopRecoveryLoop('no-intent');
      return;
    }
    // 新睡眠/唤醒代次：收敛到最新代次（保留用户意图），不直接取消。
    const latestGeneration = Math.max(0, Math.floor(this.powerGeneration));
    if (latestGeneration > 0 && latestGeneration !== intent.generation) {
      intent.generation = latestGeneration;
      this.recoveryAttempts = 0;
    }
    const generation = intent.generation;
    this.recoveryState = 'recovering';
    // 每轮**只**自增一次尝试计数（旧实现未就绪分支与 catch 分支各加一次，导致双计数）。
    this.recoveryAttempts += 1;
    try {
      // FAN-938 R6.1 §3：Host 进程丢失 → 转入已有安全启动/旧实例核验路径，不静默停止 owner。
      // startInternal() 内部先 assertReachableBeforeShortCircuit（可达性续证 + 有界等待旧
      // 进程退出），再经 launcher.start → recoverPreviousHost **fail-closed**：旧实例仍活
      // 则拒绝 spawn；只有精确确认旧实例退出后才启动唯一新 Host 并重新握手。
      if (!this.process) {
        await this.ensureHostForRecovery(token, generation);
        // close/disable/新睡眠在重建 await 中到达：token 已变或意图已清 → 立即退出，不写旧曲线。
        if (token !== this.recoveryToken || !this.recoveryIntent) return;
        if (!this.process) {
          // 旧实例仍活 / 新 Host 启动或认证瞬态失败：保持意图，退避后重试，不丢控制意图。
          this.recoveryState = 'queued';
          this.scheduleRecoveryTick(this.nextRecoveryBackoff());
          return;
        }
      }
      if (!this.coordinator.isWakeReady(generation)) {
        const context = this.admissionContext('recover-owner', this.controlIntentRevision, generation);
        context.manualRecovery = intent.manual;
        const adopted = await this.observeCurrentNativeWakeForControl(context, intent.manual);
        if (token !== this.recoveryToken || !this.recoveryIntent) return;
        if (!adopted && !this.coordinator.isWakeReady(generation)) {
          // 仍未到真实唤醒边界：零设备写入，保持意图，退避后重试。
          this.recoveryState = 'queued';
          this.scheduleRecoveryTick(this.nextRecoveryBackoff());
          return;
        }
      }
      await this.rearmAfterWakeForControlAdmission('recovery-owner', intent.manual);
      if (token !== this.recoveryToken || !this.recoveryIntent) return;
      if (!this.coordinator.isWakeReady(generation) || this.admittedPowerGeneration !== generation) {
        // rearm 只证明会话可接管，尚未取得本代写入许可：保持意图继续退避。
        this.recoveryState = 'queued';
        this.scheduleRecoveryTick(this.nextRecoveryBackoff());
        return;
      }
      const desired = this.desiredCurve;
      if (!desired) {
        // 无最新曲线：仅会话已就绪，按 no-work 收敛 owner（不写设备）。
        this.settleRecoveryTaskIfAny('no-curve');
        return;
      }
      // R6 §P1：**先写成功再收敛**。旧实现在写入前 settle，写入失败后再无人接手（1 次尝试 0 成功）。
      const intentRevision = this.controlIntentRevision;
      await this.enqueue(async () => {
        // An explicit writer, close or newer sleep can finish while this owner
        // waits for the queue. Recheck at execution, not only before enqueue.
        if (token !== this.recoveryToken || !this.recoveryIntent
            || !this.isResumeReplayCurrent(generation, intentRevision)) return false;
        const latestCurve = this.desiredCurve;
        if (!latestCurve) return false;
        const response = await this.applyMutation(cloneFanNodes(latestCurve));
        if (token === this.recoveryToken && this.recoveryIntent
            && this.isResumeReplayCurrent(generation, intentRevision)) {
          this.completedRecoveryWrite = {
            generation, intentRevision, hostSession: this.coordinatorSession,
            curve: this.activeCurve, response,
          };
          this.settleRecoveryTaskIfAny('curve-written');
        }
        return true;
      });
      if (token !== this.recoveryToken || !this.recoveryIntent) return;
      // A newer staged curve can arrive while the previous write awaits its
      // reply. If this owner still exists, its latest intent still needs work.
      this.recoveryState = 'queued';
      this.scheduleRecoveryTick(this.nextRecoveryBackoff());
    } catch (error) {
      if (token !== this.recoveryToken || !this.recoveryIntent) return;
      if (isNonRetryableControlError(error) || isExternalFanControlConflict(error)) {
        // 终态：保留意图（recoveryActive=false）并等待下一次手动请求实际重查。
        this.stopRecoveryLoop('needs-attention');
        this.recoveryState = 'needs-attention';
        fanDiagnosticLog('lifecycle.recovery-needs-attention', {
          generation, error: describeUnknownError(error),
        });
        return;
      }
      // 瞬态失败（含 POWER_RESUMING）保留意图继续退避。
      this.recoveryState = 'queued';
      this.scheduleRecoveryTick(this.nextRecoveryBackoff());
    }
  }

  /** R6 §P1：把每个 tick 串到唯一 recoveryTask 上执行，绝不并发。 */
  private launchRecoveryTick(token: number): void {
    const run = () => this.runRecoveryTickGuarded(token);
    this.recoveryTask = this.recoveryTask.then(run, run);
  }

  private async runRecoveryTickGuarded(token: number): Promise<void> {
    if (token !== this.recoveryToken || !this.recoveryIntent) return;
    try {
      await this.runRecoveryTick(token);
    } catch (error) {
      fanDiagnosticLog('lifecycle.recovery-tick-unexpected-error', { error: describeUnknownError(error) });
    }
  }

  private enqueue<T>(work: () => Promise<T>): Promise<T> {
    const next = this.operation.then(work, work);
    this.operation = next.then(() => undefined, () => undefined);
    return next;
  }

  async start(options: FanHostStartOptions = {}): Promise<FanDeviceGateResult> {
    if (this.startPromise) {
      if (!options.manualRecovery) return this.startPromise;
      // The ordinary startup may have been admitted before the user's first
      // click.  Waiting on it and returning the same promise would lose the
      // explicit manual-recovery context forever.  Queue exactly one upgraded
      // pass after the ordinary operation settles; repeated clicks share it.
      if (!this.startUpgradePromise) {
        const inFlight = this.startPromise;
        const upgrade = inFlight.catch(() => undefined).then(() => {
          // FAN-941: another ordinary start can already own startPromise here.
          // Re-entering start() would then join this same upgrade promise and
          // wait on itself forever. Keep the explicit pass on the existing
          // serialized queue; startInternal rechecks the live Host before any
          // launch or HC admission. Never create a second startup owner.
          return this.enqueue(() => this.startInternal({
            manualRecovery: true,
            reason: options.reason ?? 'manual-start-upgrade',
          }));
        });
        this.startUpgradePromise = upgrade;
      }
      const upgrade = this.startUpgradePromise;
      try {
        return await upgrade;
      } finally {
        if (this.startUpgradePromise === upgrade) this.startUpgradePromise = null;
      }
    }
    const pending = this.enqueue(async () => {
      fanDiagnosticLog('lifecycle.start-begin', {
        state: this.state,
        manualRecovery: options.manualRecovery === true,
        reason: options.reason ?? null,
      });
      // E9（六十八批 T0 删减，HC 锚点：IDevice.cs:410-423 事件驱动 SetFanControl；生命周期零原创）
      // 单次启动：不进入重试窗，失败即抛错。
      return this.startInternal(options);
    });
    this.startPromise = pending;
    try {
      return await pending;
    } finally {
      if (this.startPromise === pending) this.startPromise = null;
    }
  }

  private async startInternal(options: FanHostStartOptions = {}): Promise<FanDeviceGateResult> {
    if (!this.enabled) {
      this.setState('disabled');
      this.writeReady = false;
      return { allowed: false, writeReady: false, reason: '真实 Fan Host Gate 关闭' };
    }
    // FAN-938 R7：在手动控制或已有驻留 Host 的续证路径中，先读取一次
    // native 电源快照，补齐丢失的 renderer 边沿。这里只同步 Fan coordinator，
    // 不发 HC/Host 写请求；后续仍由同一条 rearm owner 负责恢复。
    let observedPower: PowerLifecycleState | null = null;
    if (options.manualRecovery || this.process) {
      observedPower = await this.readNativePowerState().catch(() => null);
      this.synchronizeObservedPowerSnapshot(observedPower);
    }
    // R-B（§32.3/§33.4/§34.4，2026-09-15）：短路前可达性续证。本地 state
    // （ready/awaiting-control/suspended）可能 stale——旧 FanHost 父死 handoff
    // 后已 StopListener 但前端 state 未失效（0915-0 链 2 根因）。续证失败 →
    // 重置 stopped → 落入下方 launcher.start（tryAdopt→spawn）自愈。
    if (this.closedHostAwaitingExit && this.process && !options.manualRecovery) {
      throw new Error('Fan Host 已关闭但进程退出尚未确认，请明确重试');
    }
    const reachable = this.closedHostAwaitingExit ? null : await this.assertReachableBeforeShortCircuit();
    // FAN-941: reachability is not recovery from a terminal Host fault. An
    // explicit retry owns one authenticated Close and only restarts after its
    // existing lifecycle/exit proof; an OEM physical measurement is secondary.
    if (options.manualRecovery && this.process && (this.closedHostAwaitingExit || (reachable
        && (reachable.state === 'FaultLocked' || reachable.unknownState === true)
        && reachable.hcCloseCleanupPending === false))) {
      if (!this.closedHostAwaitingExit && observedPower?.phase === 'suspending'
          && observedPower.generation === this.powerGeneration) {
        // Preserve the existing system sleep boundary. A terminal manual
        // retry is not permission to call HC Close while sleep is committing.
        throw new FanApiError('POWER_SUSPENDING', 409, '系统正在进入睡眠，保留手动意图，唤醒后可重试');
      }
      if (reachable) this.syncRemoteSessionState(reachable);
      this.controlIntentRevision += 1;
      const retryRevision = this.controlIntentRevision;
      this.cancelResumeWait('manual-terminal-retry');
      this.cancelRecovery('manual-terminal-retry');
      // Reuse the existing native whitelist and field projection; a new
      // event name would disappear when detailed logging is disabled.
      fanLifecycleEvidence('lifecycle.resume-admission-rejected', {
        action: 'manual-terminal-retry', generation: this.powerGeneration,
        state: reachable?.state ?? 'Stopped', reason: 'retained-host-requires-close-or-exit',
      });
      // startInternal already owns this.operation. Calling public close()
      // here would enqueue behind itself and deadlock.
      await this.closeInternal();
      this.assertIntentStillCurrent(retryRevision, 'manual-terminal-retry');
    }
    // FAN-933：常驻 Host 的会话若属于**上一个电源周期**（唤醒让代次前进、HC 已按顺序
    // Close），它就不是可写会话。旧实现让下面两个短路口直接返回 allowed=true，把尚未
    // 重开 HC 的 Host 当成可写，随后 `/api/acquire-control` 收到 POWER_SUSPENDING。
    // 只有 generation>0（真的经过一次电源事件）才判陈旧，冷启动 generation=0 不受影响。
    const currentGeneration = Math.max(0, Math.floor(this.powerGeneration));
    const residentSessionFromOlderPowerCycle = this.process !== null && currentGeneration > 0
      && this.admittedPowerGeneration < currentGeneration;
    if (residentSessionFromOlderPowerCycle) {
      await this.rearmAfterWakeForControlAdmission(
        options.reason ?? 'start-resident-session-stale', options.manualRecovery === true,
      );
      return { allowed: true, writeReady: this.writeReady, reason: '唤醒后已重新接管风扇会话' };
    }
    if (this.state === 'ready' || this.state === 'awaiting-control') {
      return { allowed: true, writeReady: this.writeReady, reason: this.writeReady ? 'Fan Host 已运行' : 'Fan Host 已握手，但真实写入尚未验证' };
    }
    if (this.state === 'suspended' && this.process) {
      // FAN-933：Suspended + 常驻 Host 不再直接续证放行——远端 HC 会话仍是关闭态。
      // 明确要求接管时先按当前 generation 做一次受控 rearm，等本代真正可接管再放行。
      await this.rearmAfterWakeForControlAdmission(
        options.reason ?? 'start-suspended-admission', options.manualRecovery === true,
      );
      return { allowed: true, writeReady: this.writeReady, reason: '唤醒后已重新接管风扇会话' };
    }
    this.setState('starting');
    try {
      this.process = await this.launcher.start(this.config);
      this.advanceSessionGeneration();
      if ('setSessionToken' in this.adapter && typeof this.adapter.setSessionToken === 'function') {
        this.adapter.setSessionToken(this.config.sessionToken);
      }
      this.setState('handshaking');
      let handshake: FanHandshake;
      try {
        handshake = await this.adapter.handshake();
      } catch (firstError) {
        // The Host may have started between two native IPC continuations. A
        // single token rebind closes that narrow startup race without ever
        // weakening the Host's session check or hardware write gate.
        fanDiagnosticLog('lifecycle.handshake-retry', {
          error: firstError instanceof Error ? firstError.message : String(firstError),
        });
        if (!('setSessionToken' in this.adapter) || typeof this.adapter.setSessionToken !== 'function') throw firstError;
        this.adapter.setSessionToken(this.config.sessionToken);
        await new Promise<void>((resolve) => setTimeout(resolve, 50));
        handshake = await this.adapter.handshake();
      }
      const gate = this.config.aiMockSession
        ? { allowed: aiFanMockHandshakeAllowed(handshake), writeReady: aiFanMockHandshakeAllowed(handshake), reason: 'AI-only mock handshake; zero real hardware writes' }
        : evaluateFanDeviceGate(handshake, this.savedIdentity);
      if (!gate.allowed) {
        this.writeReady = false;
        if (!this.process?.adopted) await this.stopProcessOnly();
        // E9（六十八批 T0 删减，HC 锚点：IDevice.cs:410-423 事件驱动 SetFanControl；
        // 生命周期零原创）：不再进入 conflict-locked/fault-locked 双锁。
        // 保持当前状态 + 抛错（设备门拒绝由调用方呈现）。
        fanDiagnosticLog('lifecycle.start-device-gate-denied', {
          reason: gate.reason ?? 'gate-denied',
        });
        throw new Error(`Fan Host 设备门拒绝：${gate.reason ?? 'gate-denied'}`);
      }
      if (!this.config.aiMockSession) this.savedIdentity = handshake.deviceIdentity ?? this.savedIdentity;
      this.writeReady = gate.writeReady;
      if (typeof handshake.deviceClass === 'string' && handshake.deviceClass) {
        this.deviceClassValue = handshake.deviceClass;
      }
      if (!this.config.aiMockSession && handshake.deviceIdentity) this.onDeviceIdentity?.(handshake.deviceIdentity);
      if (this.process?.adopted) {
        const remote = await this.adapter.getState().catch(() => null);
        this.syncRemoteSessionState(remote ?? undefined);
        if (!remote || isSuspendedFanState(remote.state, remote.powerState)) {
          // An adopted Host with no readable snapshot is treated as still in
          // the sleep window. Never open writes until coordinator resume.
          this.coordinator.markSuspended();
          this.setState('suspended');
          return gate;
        }
        if (this.remoteFanControlHealthy(remote) && this.lease) {
          this.beginCoordinatorSession();
          this.admitCoordinatorHcReady();
          this.setState('ready');
          return gate;
        }
      }
      // A freshly launched Host can still belong to a real or missed sleep
      // boundary. Handshake is route evidence, not permission to begin an HC
      // session while the coordinator is Suspended. The same explicit rearm
      // path used for resident Hosts must own this first-enable entry too.
      if (Math.max(0, Math.floor(this.powerGeneration)) > 0 && this.isSleepWindow()) {
        this.setState('suspended');
        // A confirmed native wake also admits the automatic startup preference.
        // Handshake alone still grants no HC writes: the same Host rearm owner
        // must prove Open/OpenEvents. Without a same-generation wake fact only
        // the existing explicit manual-rescue route may attempt recovery.
        if (options.manualRecovery || this.coordinator.isWakeReady(currentGeneration)) {
          await this.rearmAfterWakeForControlAdmission(
            options.reason ?? 'start-new-host-suspended-admission', options.manualRecovery === true,
          );
        }
        return gate;
      }
      this.beginCoordinatorSession();
      // Startup performs handshake only.  HC Open/OpenEvents and lease
      // acquisition begin on the first explicit curve/preset enable.
      this.setState('awaiting-control');
      return gate;
    } catch (error) {
      // FAN-941: a child rejected before any HC session operation can still
      // be reclaimed through its exact process handle. Preserve Open/lease
      // evidence for authenticated Close once hardware may have been touched.
      if (this.process && !this.process.adopted && !this.opened && !this.eventsOpened && !this.lease) {
        await this.stopProcessOnly().catch(stopError => {
          fanDiagnosticLog('lifecycle.start-child-stop-deferred', { error: describeUnknownError(stopError) });
        });
      }
      if (this.process?.adopted) {
        fanDiagnosticLog('lifecycle.start-adopt-keep-resident', {
          error: error instanceof Error ? error.message : String(error),
        });
        const remote = await this.adapter.getState().catch(() => null);
        this.syncRemoteSessionState(remote ?? undefined);
        if (isSuspendedFanState(remote?.state, remote?.powerState)) {
          this.coordinator.markSuspended();
          this.setState('suspended');
        } else if (isWinHttpUnreachable(error) && this.isSleepWindow()) {
          // E5（五十五批 ① 闭环）：WinHTTP 12029/12030/超时 且处于 suspend
          // 事件窗 → 不 fault-lock。睡眠期宿主不可达是常态（进程存活，
          // 只是无常驻应答）。HC 无锁对照注释保留：
          // HC 以 vTarget/Host 进程存活为唯一根据，无网络级 fault-lock。
          this.coordinator.markSuspended();
          this.setState('suspended');
        } else {
          // E9（六十八批 T0 删减，HC 锚点：IDevice.cs:410-423 事件驱动 SetFanControl；生命周期零原创）
          // 不再进入 fault/conflict-lock：保持当前状态 + 抛错。
        }
        throw error;
      }
      await this.safeAbortAfterStart();
      if (this.state === 'starting' && !this.process && !this.opened && !this.eventsOpened && !this.lease) {
        this.writeReady = false;
        this.stopHeartbeat();
        this.setState('stopped');
        fanDiagnosticLog('lifecycle.start-child-failed-retryable', {
          error: describeUnknownError(error),
          policy: 'no-owned-session;next-explicit-start-may-retry',
        });
      }
      if (isWinHttpUnreachable(error) && this.isSleepWindow()) {
        // E5（五十五批 ① 闭环）：同上——睡眠转场网络不可达不 fault-lock。
        this.coordinator.markSuspended();
        this.setState('suspended');
      } else {
        // E9（六十八批 T0 删减，HC 锚点：IDevice.cs:410-423 事件驱动 SetFanControl；生命周期零原创）
        // 不再进入 fault/conflict-lock：保持当前状态 + 抛错。
      }
      throw error;
    }
  }

  /**
   * R-B（§32.3/§33.4/§34.4，2026-09-15）：短路前可达性续证。
   * 本地 state（ready/awaiting-control/suspended）可能 stale——旧 FanHost 父死
   * handoff 后已 StopListener 但前端未失效（0915-0 链 2 根因）。getState 直调
   * adapter（不走 enqueue，避免队列排队卡 UI），短超时 1500ms；失败 → 有界等
   * 旧进程退出（≤5s，释放 HostInstanceLease，防 spawn 被 recoverPreviousHost
   * fail-closed 阻断）→ 重置 stopped → 上层走 launcher.start 自愈。
   */
  private async assertReachableBeforeShortCircuit(): Promise<FanState | null> {
    if (this.state !== 'ready' && this.state !== 'awaiting-control' &&
        !(this.state === 'suspended' && this.process)) {
      return null;
    }
    let remote: FanState | null = null;
    try {
      remote = await this.adapter.getState(1500);
    } catch (error) {
      // An authenticated refusal is not proof that the owned Host disappeared.
      if (isNonRetryableControlError(error) || isExternalFanControlConflict(error)) throw error;
      remote = null;
    }
    if (remote) return remote; // 可达，短路放行
    fanDiagnosticLog('lifecycle.stale-state-respawn', { from: this.state });
    // B4'（§34.4）：旧 Host 进程可能仍在父死 handoff 窗口存活（已停监听未退出），
    // 持有 HostInstanceLease 会阻断新 spawn。有界等待其退出。
    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
      const resident = await proc.findExact(this.config.hostExecutable).catch(() => ({ found: false, pid: 0 }));
      if (!resident.found) break;
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    const still = await proc.findExact(this.config.hostExecutable).catch(() => ({ found: false, pid: 0 }));
    if (still.found) {
      fanDiagnosticLog('lifecycle.stale-host-reclaim-timeout', { pid: still.pid });
    }
    // getState 失败时没有远端快照可 sync。旧会话的 opened/lease 若留下，
    // 随后 spawn 的新 Host 会在 apply 里跳过 Open/OpenEvents（本文件
    // syncRemoteSessionState 注释：skip Open → HOST_EVENTS_NOT_OPEN）。
    // 只丢本进程 HTTP 会话标记，不杀 process 句柄（launcher.start 会覆盖）。
    this.opened = false;
    this.eventsOpened = false;
    this.lease = null;
    this.stopHeartbeat();
    this.setState('stopped');
    return null;
  }

  apply(nodes: readonly FanNode[]): Promise<FanState> {
    // R2（§4.3）：新曲线是**新的用户意图**——先同步使旧恢复等待/旧重放失效（入队前），
    // 再由 mutateWithBoundedRetry 做统一准入与实际入队；不让旧 waiter 占队 15 s 才被取消。
    this.controlIntentRevision += 1;
    const intentRevision = this.controlIntentRevision;
    this.cancelResumeWait('new-curve-intent');
    const context = this.admissionContext('apply', intentRevision);
    if (this.isSleepWindow() || this.recoveryIntent !== null) this.desiredCurve = cloneFanNodes(nodes);
    return this.mutateWithBoundedRetry(
      context,
      () => {
        fanDiagnosticLog('lifecycle.apply-begin', { state: this.state, nodeCount: nodes.length });
        return this.applyMutation(nodes, intentRevision);
      },
      (attempt, error) => fanDiagnosticLog('lifecycle.apply-failure', { attempt, state: this.state, error: describeUnknownError(error) }),
      'Fan 曲线应用失败',
    );
  }

  applyPreset(name: 'soft' | 'balanced' | 'aggressive', nodes?: readonly FanNode[]): Promise<FanState> {
    this.controlIntentRevision += 1;
    const intentRevision = this.controlIntentRevision;
    this.cancelResumeWait('new-preset-intent');
    const context = this.admissionContext('apply-preset', intentRevision);
    if (nodes && (this.isSleepWindow() || this.recoveryIntent !== null)) this.desiredCurve = cloneFanNodes(nodes);
    return this.mutateWithBoundedRetry(
      context,
      () => {
        fanDiagnosticLog('lifecycle.preset-begin', { state: this.state, preset: name });
        return this.applyPresetMutation(name, nodes, intentRevision);
      },
      (attempt, error) => fanDiagnosticLog('lifecycle.preset-failure', { attempt, state: this.state, preset: name, error: describeUnknownError(error) }),
      'Fan 预设应用失败',
    );
  }

  /**
   * FAN-938 R5 §P1/P4：恢复 owner 存在期间，用户更新曲线只登记为**待应用的最新曲线**，
   * 不触发任何设备写入；由 runRecoveryTick 在取得写入许可（真实睡眠结束、认证、设备互斥
   * 与 HC 会话检查成立）后应用这个最新版本。返回 false 表示当前没有恢复任务，
   * 调用方应走正常 apply 路径（本方法绝不成为第二条写入路径）。
   */
  stageRecoveryCurve(nodes: readonly FanNode[]): boolean {
    if (this.recoveryIntent === null) return false;
    this.controlIntentRevision += 1;
    this.cancelResumeWait('new-recovery-curve-intent');
    this.desiredCurve = cloneFanNodes(nodes);
    fanDiagnosticLog('lifecycle.recovery-curve-staged', {
      generation: this.recoveryIntent.generation,
      recoveryState: this.recoveryState,
      nodeCount: nodes.length,
    });
    return true;
  }

  /**
   * R2（§4.3）：等待/探测期间到达了更新的控制意图（新曲线/预设、disable/close、新睡眠）
   * ⇒ 本次调用**不再排队**（旧意图不得在新意图之后写硬件）。这不是失败态：不置锁、
   * 不重试，交由新意图自己的调用完成。
   */
  private assertIntentStillCurrent(intentRevision: number, action: string): void {
    if (intentRevision === this.controlIntentRevision) return;
    fanDiagnosticLog('lifecycle.control-intent-superseded', {
      action,
      intentRevision,
      currentIntentRevision: this.controlIntentRevision,
      state: this.state,
    });
    throw new Error(`FAN_CONTROL_INTENT_SUPERSEDED:${action}`);
  }

  /**
   * G7（执行单 §3.3）+ R2 单次有界重试，apply()/applyPreset()/resume() 共用的**唯一**执行语义。
   *
   * 920-v1.2 §14.2 W1：`resume()` 此前直接调用 `applyMutation()`（单发），
   * 因此**唤醒时一次 enable 被拒**就会让曲线完全没写、且没有第二次尝试——
   * 而 App.vue 的 `onPowerResumeReady` 是 fire-and-forget（失败只记日志），
   * 于是只剩人工点击能恢复。同一份"瞬时失败重试一次"的语义已在 apply()/
   * applyPreset() 里存在，这里收敛为单点实现，避免再次分叉。
   *
   * G7 关键更正（为什么必须与准入一起改）：
   * - 旧实现把 500 ms 延时重试放进串行队列，队列内一旦才遇到合法 `POWER_RESUMING`，
   *   这条延时就成了慢恢复的**唯一**兜底，还继续占着队列（§3.3.6 明令禁止）。
   * - 现在：**先准入、再入队执行一次**；队列内不再做长等待。遇到合法 `POWER_RESUMING`
   *   时**释放队列占用**，回到同一个绝对截止、同一任务身份的协调只读等待后再入队一次。
   * - 401/403、故障/认证失败、外部路由冲突绝不以 generic catch 变成第二次控制写（§3.3.7）。
   */
  private async mutateWithBoundedRetry<T>(
    context: FanAdmissionContext,
    mutate: () => Promise<T>,
    onFailure: (attempt: number, error: unknown) => void,
    finalMessage: string,
  ): Promise<T> {
    let lastError: unknown;
    for (let attempt = 0; attempt < 2; attempt += 1) {
      // R4: explicit controls can recover a lost one-shot wake event, outside the queue.
      // Normal awake operations and automatic resume do not add this IPC read.
      const explicitControl = context.action === 'apply' || context.action === 'apply-preset';
      const manualRecoveryWindow = this.isSleepWindow()
        || this.state === 'suspended'
        || this.recoveryIntent?.manual === true
        || (context.generation > 0 && this.admittedPowerGeneration < context.generation);
      if (attempt === 0 && explicitControl && manualRecoveryWindow
          && !this.hasWakeContext(context.generation)) {
        // An explicit curve/preset action is the Fan T0 rescue path. It may
        // recover a stale Suspended native snapshot even when the renderer
        // missed every public resume notification. It never admits a write
        // while the native phase is still actively entering sleep.
        context.manualRecovery = true;
        fanLifecycleEvidence('lifecycle.manual-wake-recovery-requested', {
          action: context.action,
          generation: context.generation,
          state: this.state,
          reason: 'explicit-control-without-current-wake-context',
        });
        const adopted = await this.observeCurrentNativeWakeForControl(context, true);
        if (adopted && this.process) {
          await this.rearmAfterWakeForControlAdmission('manual-missed-wake-edge', true);
        } else if (!adopted) {
          this.noteRecoveryIntentPending('manual-missed-wake-edge', context.generation, true);
        }
        this.assertAdmissionContextCurrent(context, 'manual-wake-rearm');
      }
      // 每一次尝试都从**同一份**准入上下文出发（含首次与队列内重入的共用截止）。
      const admission = this.resumeAdmissionWait(context.action, context);
      if (admission) {
        await admission;
        this.assertIntentStillCurrent(context.intentRevision, context.action);
      }
      try {
        // G7（§3.3.2）：**已准入**的写入按既有队列契约执行——串行队列的既有语义是
        // "已入队的曲线请求先完成，随后到达的 suspend 才执行"（T0：不得在睡眠事务开始后
        // 再写，也不得让后到的意图穿过队列）。陈旧回包的保护落在**每个 await 边界**：
        // 准入探测后、协调等待每次回包后、以及唤醒重放最终写前（replayResumeCurve）。
        const result = await this.enqueue(async () => {
          const recovered = this.completedRecoveryWrite;
          if ((context.action === 'apply' || context.action === 'apply-preset')
              && recovered && recovered.generation === context.generation
              && recovered.intentRevision === context.intentRevision
              && recovered.hostSession === this.coordinatorSession
              && recovered.curve === this.activeCurve && this.state === 'ready'
              && this.isResumeReplayCurrent(context.generation, context.intentRevision)) {
            this.completedRecoveryWrite = null;
            return recovered.response as T;
          }
          const written = await mutate();
          // Settle inside the same queue operation as the successful write.
          // Otherwise the next queued owner can run before this await resumes
          // and write the identical staged curve a second time.
          if (this.recoveryIntent && this.isResumeReplayCurrent(context.generation, context.intentRevision)) {
            this.settleRecoveryTaskIfAny('curve-written-by-control');
          }
          return written;
        });
        if (context.manualRecovery) {
          fanLifecycleEvidence('lifecycle.manual-wake-recovery-completed', {
            action: context.action,
            generation: context.generation,
            state: this.state,
            reason: 'curve-write-succeeded-after-manual-rearm',
          });
        }
        return result;
      } catch (error) {
        // FAN-941: this cancelled request must not retry its old curve. The
        // continuous recovery owner keeps its separate newer-generation policy.
        if (error instanceof Error && error.message.startsWith('FAN_CONTROL_INTENT_SUPERSEDED:')) throw error;
        onFailure(attempt, error);
        lastError = error;
        if (context.manualRecovery && (attempt === 1 || isNonRetryableControlError(error))) {
          fanLifecycleEvidence('lifecycle.manual-wake-recovery-failed', {
            action: context.action,
            generation: context.generation,
            state: this.state,
            error: describeUnknownError(error),
            attempt: attempt + 1,
          });
        }
        // E9（六十八批 T0 删减，HC 锚点：IDevice.cs:410-423 事件驱动 SetFanControl；生命周期零原创）
        // 不再进入 conflict/fault-lock：保持当前状态 + 抛错。
        if (isExternalFanControlConflict(error) || isNonRetryableControlError(error)) throw error;
        if (attempt === 1) throw error;
        // §3.3.6：只有合法 `POWER_RESUMING` 才允许释放队列、回到协调等待后重入一次；
        // 其余瞬态失败沿用 R2 的单次重试（不再用队列内 500 ms 延时充当慢恢复兜底）。
        if (isPowerResumingError(error)) {
          fanLifecycleEvidence('lifecycle.resume-wait-queue-reentry', {
            action: context.action,
            generation: context.generation,
            intentRevision: context.intentRevision,
          });
        }
      }
    }
    throw lastError instanceof Error ? lastError : new Error(finalMessage);
  }

  private async applyMutation(nodes: readonly FanNode[], intentRevision = this.controlIntentRevision): Promise<FanState> {
    if (this.closedHostAwaitingExit) throw new Error('Fan Host 旧实例退出尚未确认，请明确重试');
    // Preserve requests already admitted in queue order; only an intent arriving
    // during this operation's await boundaries cancels its not-yet-submitted write.
    const admittedIntentRevision = this.controlIntentRevision;
    if (this.state === 'stopped') {
      const gate = await this.startInternal();
      if (!gate.allowed) throw new Error(gate.reason);
    }
    this.assertIntentStillCurrent(admittedIntentRevision, 'enable-before-session');
    if (this.state !== 'ready' && this.state !== 'awaiting-control') {
      throw new Error(`Fan Host 当前不可控制: ${this.state}`);
    }
    if (!this.writeReady) throw new Error('当前风扇数据路线已识别，但真实写入/恢复尚未验证');
    this.ensureCoordinatorSession();
    try {
      if (!this.opened) {
        this.advanceSessionGeneration();
        // Mark the session before the call. HC/Open can touch EC and then
        // throw; recovery must therefore attempt OEM restore even when the
        // promise rejects before the normal success assignment.
        this.opened = true;
        const opened = await this.adapter.open();
        this.syncRemoteSessionState(opened);
        this.assertIntentStillCurrent(admittedIntentRevision, 'enable-after-open');
      }
      if (!this.eventsOpened) {
        const openGeneration = this.powerGeneration;
        const eventsOpened = await this.adapter.openEvents();
        this.assertIntentStillCurrent(admittedIntentRevision, 'session-open-events-completed');
        this.eventsOpened = true;
        this.bindExplicitHcOpen(eventsOpened, openGeneration);
        this.syncRemoteSessionState(eventsOpened);
        this.assertIntentStillCurrent(admittedIntentRevision, 'enable-after-open-events');
      }
      await this.ensureLeaseForMutation();
      // FAN-941: disable/Close can cancel intent while HC Open or lease acquisition awaits.
      // Retain the opened session for ordered cleanup, but never submit the cancelled curve.
      this.assertIntentStillCurrent(admittedIntentRevision, 'enable-before-write');
      this.admitCoordinatorHcReady();
      this.setState('ready');
      this.admitCoordinatorWrite('enable');
      const applied = await this.adapter.enable(nodes, this.lease?.leaseId);
      // E9（六十八批 T0 删减，HC 锚点：IDevice.cs:410-423 事件驱动 SetFanControl；生命周期零原创）
      // 写曲线后仅更新曲线句柄；续租心跳由 ensureLeaseForMutation 维护（第七十七批 C）；
      // fan-guard / emit 保活不复活（E9 删除面）。
      this.activeCurve = cloneFanNodes(nodes);
      if (intentRevision === this.controlIntentRevision) this.desiredCurve = cloneFanNodes(nodes);
      return applied;
    } catch (error) {
      // FAN-927 §4.4：Host 在写入点明确拒绝 LEASE_INVALID，说明本请求曲线**未执行**。
      // 丢弃已知失效的本地 lease，让 mutateWithBoundedRetry 的那一次重试走 fresh acquire
      // 并重提同一曲线；超时/断连（结果未知）不在此列。
      if (isLeaseInvalidError(error)) {
        fanDiagnosticLog('lifecycle.lease-invalid-on-write', { state: this.state, action: 'apply' });
        this.lease = null;
        this.stopHeartbeat();
      }
      // E9（六十八批 T0 删减，HC 锚点：IDevice.cs:410-423 事件驱动 SetFanControl；生命周期零原创）
      // 保持当前状态 + 抛错；不再 recoverAfterMutationFailure。
      // 920 F-3：但 Host 可能已在本次失败中关闭了 HC 会话，必须按权威快照回灌
      // 会话标志，否则 apply()/applyPreset() 的那次重试会跳过 Open/OpenEvents。
      await this.resyncSessionFromHost();
      throw error;
    }
  }

  private async applyPresetMutation(name: 'soft' | 'balanced' | 'aggressive', nodes?: readonly FanNode[], intentRevision = this.controlIntentRevision): Promise<FanState> {
    if (this.closedHostAwaitingExit) throw new Error('Fan Host 旧实例退出尚未确认，请明确重试');
    // Preserve requests already admitted in queue order; only an intent arriving
    // during this operation's await boundaries cancels its not-yet-submitted write.
    const admittedIntentRevision = this.controlIntentRevision;
    if (this.state === 'stopped') {
      const gate = await this.startInternal();
      if (!gate.allowed) throw new Error(gate.reason);
    }
    this.assertIntentStillCurrent(admittedIntentRevision, 'preset-before-session');
    if (this.state !== 'ready' && this.state !== 'awaiting-control') {
      throw new Error(`Fan Host 当前不可控制: ${this.state}`);
    }
    if (!this.writeReady) throw new Error('当前风扇数据路线已识别，但真实写入/恢复尚未验证');
    this.ensureCoordinatorSession();
    try {
      if (!this.opened) {
        this.advanceSessionGeneration();
        this.opened = true;
        const opened = await this.adapter.open();
        this.syncRemoteSessionState(opened);
        this.assertIntentStillCurrent(admittedIntentRevision, 'preset-after-open');
      }
      if (!this.eventsOpened) {
        const openGeneration = this.powerGeneration;
        const eventsOpened = await this.adapter.openEvents();
        this.assertIntentStillCurrent(admittedIntentRevision, 'session-open-events-completed');
        this.eventsOpened = true;
        this.bindExplicitHcOpen(eventsOpened, openGeneration);
        this.syncRemoteSessionState(eventsOpened);
        this.assertIntentStillCurrent(admittedIntentRevision, 'preset-after-open-events');
      }
      await this.ensureLeaseForMutation();
      // FAN-941: disable/Close can cancel intent while HC Open or lease acquisition awaits.
      // Retain the opened session for ordered cleanup, but never submit the cancelled curve.
      this.assertIntentStillCurrent(admittedIntentRevision, 'preset-before-write');
      this.admitCoordinatorHcReady();
      this.setState('ready');
      this.admitCoordinatorWrite('preset');
      const applied = await this.adapter.applyPreset(name, this.lease?.leaseId, nodes);
      // E9（六十八批 T0 删减，HC 锚点：IDevice.cs:410-423 事件驱动 SetFanControl；生命周期零原创）
      // 写曲线后仅更新曲线句柄；续租心跳由 ensureLeaseForMutation 维护（第七十七批 C）。
      if (nodes) {
        this.activeCurve = cloneFanNodes(nodes);
        if (intentRevision === this.controlIntentRevision) this.desiredCurve = cloneFanNodes(nodes);
      }
      return applied;
    } catch (error) {
      // FAN-927 §4.4：Preset 归并到与 apply 同一路径——写入点 LEASE_INVALID 同样只允许
      // 一次重新 acquire + 重提（由 mutateWithBoundedRetry 的那一次重试完成）。
      if (isLeaseInvalidError(error)) {
        fanDiagnosticLog('lifecycle.lease-invalid-on-write', { state: this.state, action: 'apply-preset' });
        this.lease = null;
        this.stopHeartbeat();
      }
      // E9（六十八批 T0 删减，HC 锚点：IDevice.cs:410-423 事件驱动 SetFanControl；生命周期零原创）
      // 保持当前状态 + 抛错；不再 recoverAfterMutationFailure。
      // 920 F-3：但 Host 可能已在本次失败中关闭了 HC 会话，必须按权威快照回灌
      // 会话标志，否则 apply()/applyPreset() 的那次重试会跳过 Open/OpenEvents。
      await this.resyncSessionFromHost();
      throw error;
    }
  }

  /** Disable the curve, restore OEM control and release the lease. */
  async disable(): Promise<FanState> {
    // R2（§4.3）：明确关闭是**终止性用户意图**——同步使旧恢复等待/旧重放失效（入队前）。
    this.controlIntentRevision += 1;
    this.cancelResumeWait('disable');
    // FAN-938 R5 §3：disable 是终止性意图，取消 Fan 恢复 owner（不再持续重试）。
    this.cancelRecovery('disable');
    return this.enqueue(async () => {
      fanDiagnosticLog('lifecycle.disable-begin', { state: this.state });
      // E9（六十八批 T0 删减，HC 锚点：IDevice.cs:410-423 事件驱动 SetFanControl；生命周期零原创）
      // 不再 disarm fan-guard（保活循环已删）。
      if (this.state === 'disabled' || this.state === 'stopped' || this.state === 'suspended') {
        // A disable click is also an explicit cancellation of any deferred
        // post-sleep replay. Do not retain a curve that the user has already
        // asked to return to OEM control.
        this.activeCurve = null;
        this.resumeCurve = null;
        this.desiredCurve = null;
        return { state: this.state, powerState: 'Unknown', hardwareWrites: false, hardwareWritesObserved: false };
      }
      if (this.state === 'fault-locked' || this.state === 'conflict-locked' || this.state === 'unknown') {
        // E9（六十八批 T0 删减，HC 锚点：IDevice.cs:410-423 事件驱动 SetFanControl；生命周期零原创）
        // 不再 recoverLockedHostBeforeStart：保持当前状态 + 抛错。
        throw new Error(`Fan Host 当前处于 ${this.state}，保持当前状态，不做关闭恢复`);
      }
      this.revokeCoordinatorWrites('disable');
      try {
        if (this.lease || this.opened) await this.restoreAndRelease();
      } catch (error) {
        fanDiagnosticLog('lifecycle.disable-failure', { state: this.state, error: error instanceof Error ? error.message : String(error) });
        // E9（六十八批 T0 删减，HC 锚点：IDevice.cs:410-423 事件驱动 SetFanControl；生命周期零原创）
        // 不再 waitForHostRecovery/adoptRecoveredHost：保持当前状态 + 抛错。
        throw error;
      }
      // E9（六十八批 T0 删减，HC 锚点：IDevice.cs:410-423 事件驱动 SetFanControl；生命周期零原创）
      // /api/release-control mirrors HC's profile handoff to Hardware. The
      // resident HC device remains open until sleep or application exit;
      // keeping the remote Open/OpenEvents flags avoids re-running
      // subscriptions on the next enable.
      this.activeCurve = null;
      this.resumeCurve = null;
      this.desiredCurve = null;
      this.setState('awaiting-control');
      return this.adapter.getState();
    });
  }

  async heartbeat(): Promise<FanLease> {
    // Capture before enqueue: off/new intent can arrive while this renewal
    // waits behind a hardware operation, not only while its HTTP reply waits.
    const attempt: FanHeartbeatAttempt = {
      intentRevision: this.controlIntentRevision,
      epoch: this.heartbeatEpoch,
      leaseId: this.lease?.leaseId ?? '',
      generation: Math.max(0, Math.floor(this.powerGeneration)),
      hostPid: this.process?.pid ?? 0,
    };
    // 租约协议完整性（第七十七批 C-①，2026-09-12 用户批准）：真续租——调用
    // Host /api/heartbeat 延长 TTL=15s 租约并回写 expiresAt/generation。
    // 失败仅清本地 lease 惰性重建（下次 mutation 重新 acquire），不置锁
    // （E9 删除面维持；HC 无锁对照）。
    // G8（FAN-926R 执行单 §4）：每次心跳捕获 lease 身份/Host 实例/代际/心跳代次；
    // 回包（成功与失败）只有**仍匹配**时才允许更新或清理自己的 lease。
    return this.enqueue(async () => {
      if (this.state !== 'ready' || !this.lease) {
        this.stopHeartbeat();
        throw new Error('Fan lease 不可用');
      }
      if (!this.isHeartbeatResultCurrent(attempt)) throw new Error('FAN_HEARTBEAT_SUPERSEDED');
      let renewed: FanLease;
      try {
        renewed = await this.adapter.heartbeat(attempt.leaseId);
      } catch (error) {
        // G8：**迟到**失败（睡眠/关闭边界或新代次已作废本代心跳）不得清新 lease，
        // 也不得触发任何 Open/Enable 重放。只有仍属本代心跳的失败才按原合同处理。
        if (!this.isHeartbeatResultCurrent(attempt)) {
          fanDiagnosticLog('lifecycle.heartbeat-stale-failure-ignored', {
            state: this.state,
            leaseId: attempt.leaseId,
            error: describeUnknownError(error),
          });
          throw error;
        }
        fanDiagnosticLog('lifecycle.heartbeat-failure', { state: this.state, error: describeUnknownError(error) });
        this.lease = null;
        this.stopHeartbeat();
        // 租约协议完整性第四件（批85 F7-a，2026-09-12 用户授权）：心跳失败后
        // 若仍有活跃曲线（activeCurve），自动重新 acquireControl → 重写曲线
        // （applyMutation 内含 ensureLeaseForMutation 重新 acquire + 重启心跳）。
        // 失败只记 lifecycle.lease-reacquire-failed，不置锁（E9 删除面：无
        // fault/conflict 锁、无自愈 worker——单次重试，非循环守卫）。
        // 批93（2026-09-12 回滚单）：睡眠窗守卫——suspend 事件窗（本机
        // suspending/suspended/resuming，或远端 suspended）内自动重获会被
        // Host 端门拒（0912-18 实测 lifecycle.lease-reacquire-failed）；醒来
        // 后由 resume 事件驱动重建夺回，此处直接跳过（保留 F7-a 本身的动作）。
        // G8：睡眠/恢复拒绝**不得**从 catch 直接重放曲线；这一幕由上面的守卫与
        // 心跳代次失效共同保证——边界之后本代失败已被判定为 stale，走不到这里。
        if (this.stateValue === 'suspended' || this.isSleepWindow()) {
          fanDiagnosticLog('lifecycle.lease-reacquire-deferred-suspend', { state: this.state });
          throw error;
        }
        // 920 F-3：Host 可能已在本次心跳失败中关闭了 HC 会话（route-lost 的
        // RecoverAfterHardwareFailure → RestoreHardware(close: true) 会清
        // OpenCalled/OpenEventsCalled）。先按权威快照回灌，下面的自愈重写才会
        // 重新 Open/OpenEvents，而不是写进一个已关闭的会话。
        await this.resyncSessionFromHost();
        // The readback is another await boundary. An off/close/new curve
        // arriving there must not revive the previous autonomous write.
        if (!this.isHeartbeatIntentCurrent(attempt)) throw error;
        const curve = this.activeCurve ? cloneFanNodes(this.activeCurve) : null;
        if (curve) {
          try {
            await this.applyMutation(curve);
            fanDiagnosticLog('lifecycle.lease-reacquired', { state: this.state });
            if (this.lease) return { ...this.lease };
          } catch (reacquireError) {
            fanDiagnosticLog('lifecycle.lease-reacquire-failed', {
              state: this.state,
              error: reacquireError instanceof Error ? reacquireError.message : String(reacquireError),
              activeCurve: this.activeCurve !== null,
            });
          }
        }
        throw error;
      }
      // G8：旧成功不得覆盖新 lease / 新周期。
      if (!this.isHeartbeatResultCurrent(attempt)) {
        fanDiagnosticLog('lifecycle.heartbeat-stale-success-ignored', {
          state: this.state,
          leaseId: attempt.leaseId,
          renewedLeaseId: renewed.leaseId,
        });
        return this.lease ? { ...this.lease } : { ...renewed };
      }
      this.lease = renewed;
      fanDiagnosticLog('lifecycle.heartbeat-success', { state: this.state });
      return { ...this.lease };
    });
  }

  /** G8（§4）：使所有**在途**心跳回包失效（不停止未来的 tick）。 */
  private invalidateHeartbeatEpoch(): void {
    this.heartbeatEpoch += 1;
  }

  /** G8（§4）：回包是否仍属于**本代**心跳（代次/电源代次/Host 实例/自己认领的 lease）。 */
  private isHeartbeatIntentCurrent(attempt: FanHeartbeatAttempt): boolean {
    return attempt.intentRevision === this.controlIntentRevision
      && attempt.epoch === this.heartbeatEpoch
      && attempt.generation === Math.max(0, Math.floor(this.powerGeneration))
      && attempt.hostPid === (this.process?.pid ?? 0);
  }

  private isHeartbeatResultCurrent(attempt: FanHeartbeatAttempt): boolean {
    return this.isHeartbeatIntentCurrent(attempt) && this.lease?.leaseId === attempt.leaseId;
  }

  /** 协调层协议完整性：lease 存活期间 5s 周期续租（TTL=15s 的续租侧义务）。 */
  private startHeartbeat(): void {
    this.stopHeartbeat();
    this.heartbeatTimer = setInterval(() => {
      if (this.timerHeartbeatPending) return;
      const pending = this.heartbeat().then(() => undefined, () => undefined);
      this.timerHeartbeatPending = pending;
      void pending.then(() => {
        if (this.timerHeartbeatPending === pending) this.timerHeartbeatPending = null;
      });
    }, this.heartbeatIntervalMs);
  }

  private stopHeartbeat(): void {
    if (this.heartbeatTimer) {
      clearInterval(this.heartbeatTimer);
      this.heartbeatTimer = null;
    }
  }

  async suspend(): Promise<void> {
    // R2（§4.3）：新一代睡眠必须让上一代的等待/重放**立即**失效（入队前同步取消）。
    this.controlIntentRevision += 1;
    this.cancelResumeWait('suspend');
    return this.enqueue(async () => {
      fanDiagnosticLog('lifecycle.suspend-begin', { state: this.state });
      if (!this.enabled || this.state === 'disabled' || this.state === 'stopped') return;
      if (this.state === 'suspended') return;
      const curveToResume = this.desiredCurve ? cloneFanNodes(this.desiredCurve) : null;
      // suspend 零消息（E9 删除面维持：不发 adapter.suspend/协商、不做重试窗、不进锁态）。
      // 租约协议完整性第三件（第七十七批 C-③）：睡眠期 Host leaseTimer 仍会到期把
      // 风扇交回 OEM，本地 lease 已不可能有效——清空并停心跳；唤醒后 resume 经
      // ensureLeaseForMutation 重新 acquire + applyMutation 重写曲线（HC 事件驱动重写同义）。
      this.lease = null;
      this.stopHeartbeat();
      this.activeCurve = null;
      this.resumeCurve = curveToResume;
      if (curveToResume) this.desiredCurve = cloneFanNodes(curveToResume);
      this.coordinator.markSuspended();
      this.setState('suspended');
    });
  }

  /**
   * FAN-927 §3：**本 run 已确认的活动控制意图**。
   *
   * 判据只有"本实例确实成功启用过一条曲线"（active/resume/desired 三个曲线句柄之一在场）。
   * `side栏常驻`、`savedIdentity`、`configured`、`编辑/保存过曲线`、默认 `enabled` 开关
   * **都不是**活动意图——它们都不代表用户已成功建立控制。
   */
  private hasActiveControlIntent(): boolean {
    return this.activeCurve !== null || this.resumeCurve !== null || this.desiredCurve !== null;
  }

  /**
   * FAN-927 §5.1：同 run 渲染器重建 / §5.2 同系统启动内非正常退出后的重接。
   *
   * 本渲染实例本地没有任何曲线句柄时，才向 native 读取一次当前 run 责任 / 活动记录快照：
   * 只有 native 明确回答"仍有效"（本 run 已确认，或崩溃前仍有效且可匹配）才接续。
   * 无依据时**零动作**（不启动 Host、不发 HTTP、不写硬件）。
   *
   * 接续语义：已有可复用设备会话时只同步本地意图与客户端 lease，**不重开 HC、不重复下发同一曲线**。
   */
  async reattachRenderer(): Promise<boolean> {
    if (!this.enabled || this.isDisabledState()) return false;
    if (this.hasActiveControlIntent()) return false;
    return this.adoptNativeIntentSnapshot('renderer-reattach');
  }

  /**
   * FAN-927 §3/§5：向 native 读一次活动记录并把**已确认的**意图同步成本地曲线句柄。
   * 只读一次、无依据即返回 false；不启动 Host、不发 HTTP、不写硬件。
   */
  private async adoptNativeIntentSnapshot(
    reason: string,
    preloaded?: FanActivitySnapshot | null,
  ): Promise<boolean> {
    const snapshot = preloaded !== undefined
      ? preloaded
      : await this.readNativeActivity().catch(() => null);
    if (!snapshot || (snapshot.active !== true && snapshot.recoverable !== true)) {
      fanDiagnosticLog('lifecycle.intent-snapshot-unusable', {
        reason,
        state: this.state,
        active: snapshot?.active === true,
        recoverable: snapshot?.recoverable === true,
      });
      return false;
    }
    const curve = Array.isArray(snapshot.curve) ? cloneFanNodes(snapshot.curve) : [];
    if (curve.length === 0) {
      fanDiagnosticLog('lifecycle.intent-snapshot-unusable', { reason, state: this.state, detail: 'record-without-curve' });
      return false;
    }
    // 只把"native 已确认的意图"同步成本地句柄；不触发任何额外 Open/Enable。
    this.desiredCurve = cloneFanNodes(curve);
    if (this.state === 'suspended' || this.state === 'stopped') this.resumeCurve = cloneFanNodes(curve);
    fanLifecycleEvidence('lifecycle.intent-snapshot-adopted', {
      reason,
      state: this.state,
      revision: snapshot.revision,
      recoverable: snapshot.recoverable === true,
      generation: this.powerGeneration,
    });
    return true;
  }

  /** FAN-927 §3：resume 资格判定里的"§5 有效恢复意图"分支。
   *
   * 注意与 §5 曲线接续的区别：**资格**只看 native 是否确认过活动意图（`active`/`recoverable`），
   * 不要求记录里带曲线——已有意图但曲线句柄缺失是"只恢复协调状态"的既有语义。
   */
  private async adoptRecoverableControlIntent(): Promise<boolean> {
    const snapshot = await this.readNativeActivity().catch(() => null);
    const confirmed = snapshot?.active === true || snapshot?.recoverable === true;
    if (!confirmed) {
      fanDiagnosticLog('lifecycle.intent-snapshot-unusable', {
        reason: 'resume-intent-check',
        state: this.state,
        active: snapshot?.active === true,
        recoverable: snapshot?.recoverable === true,
      });
      return false;
    }
    const curve = Array.isArray(snapshot?.curve) ? cloneFanNodes(snapshot!.curve) : [];
    if (curve.length > 0 && !this.desiredCurve) {
      this.desiredCurve = cloneFanNodes(curve);
      if (this.state === 'suspended' || this.state === 'stopped') this.resumeCurve = cloneFanNodes(curve);
    }
    fanLifecycleEvidence('lifecycle.intent-snapshot-adopted', {
      reason: 'resume-intent-check',
      state: this.state,
      revision: snapshot?.revision ?? 0,
      recoverable: snapshot?.recoverable === true,
      generation: this.powerGeneration,
    });
    return true;
  }

  /**
   * FAN-929（2026-09-27 用户裁决）：AC/DC 电源档变化的**事件驱动重放**。
   *
   * 原 Host 侧 1 Hz profile watchdog 轮询负责"电源线变化后按新电源档重新套用曲线"。
   * 该轮询已按用户裁决改为事件驱动（稳态 0 tick），因此改由渲染器在收到 `power.acChanged`
   * 时用**当前曲线**重放一次；Host 仍在写入点做电源档对齐（重新捕获 HC 模板）与 route 就绪校验。
   * 无活动曲线、或无请求时**不发任何请求**。
   */
  async reapplyActiveCurve(reason: string): Promise<boolean> {
    if (!this.enabled || this.isDisabledState()) return false;
    const curve = this.desiredCurve ?? this.activeCurve;
    if (!curve || curve.length === 0) return false;
    if (this.state !== 'ready' && this.state !== 'awaiting-control') return false;
    try {
      await this.apply(cloneFanNodes(curve));
      fanDiagnosticLog('lifecycle.power-line-reapply', { reason, state: this.state, nodeCount: curve.length });
      return true;
    } catch (error) {
      fanDiagnosticLog('lifecycle.power-line-reapply-failed', { reason, error: describeUnknownError(error) });
      return false;
    }
  }

  /**
   * FAN-927 §5.1：渲染器重建后接上**已存在**的控制会话。只同步，不重开 HC、不重复下发曲线。
   * 客户端 lease 缺失（例如新渲染实例还没拿回来）时按既有 acquire 协议取一次——
   * "R1 持有曲线但没有客户端 lease"**不能**据此判 Host 不健康。
   */
  async reattachExistingControlSession(): Promise<boolean> {
    if (!this.enabled || this.isDisabledState()) return false;
    if (!this.desiredCurve) return false;
    const revision = this.controlIntentRevision;
    const generation = this.powerGeneration;
    const sessionGeneration = this.sessionGeneration;
    const stillCurrent = () => revision === this.controlIntentRevision && generation === this.powerGeneration
      && sessionGeneration === this.sessionGeneration && !this.isDisabledState();
    // A cached token proves neither current Host readiness nor current ownership.
    const remote = await this.adapter.getState().catch(() => null);
    if (!remote || !stillCurrent() || unsupportedFanControlError(remote)
        || this.classifyControlAdmission(remote, true) !== 'accepting') return false;
    this.syncRemoteSessionState(remote);
    if (this.lease) {
      if (!this.remoteFanControlHealthy(remote)) return false;
      this.startHeartbeat(); return true;
    }
    const state = String(remote.state ?? '').toLowerCase();
    const sessionReady = state === 'ready' && String(remote.protocolVersion ?? '') === '2'
      && (this.config.aiMockSession ? aiFanMockStateAllowed(remote) && remote.mockControlEnabled === true : remote.hardwareWritesEnabled === true)
      && remote.openCalled === true && remote.openEventsCalled === true;
    if (!sessionReady) return false;
    try {
      const acquired = await this.adapter.acquireControl();
      if (!stillCurrent()) {
        // Release only the token returned by THIS acquisition. The Host rejects
        // it if a newer owner has already taken over; never restore a cached token.
        await this.adapter.releaseControl(acquired.leaseId).catch(() => undefined);
        return false;
      }
      this.lease = acquired;
      this.startHeartbeat();
      fanLifecycleEvidence('lifecycle.renderer-lease-acquired', {
        state: this.state,
        generation: this.powerGeneration,
      });
      return true;
    } catch (error) {
      fanDiagnosticLog('lifecycle.renderer-lease-acquire-failed', {
        state: this.state,
        error: describeUnknownError(error),
      });
      return false;
    }
  }

  /**
   * FAN-927 §5：渲染器重建 / 主程序重开的**唯一事件入口**（App.vue 挂载时调用一次）。
   *
   * 只读 native 记录一次，然后按记录性质分流：
   * - 本 run 已确认的活动意图 ⇒ 只接上已有会话（同步意图 + 客户端 lease，不重开 HC、不重发曲线）；
   * - 崩溃前仍有效的记录 ⇒ §5.2 一次有界恢复（真实重写那条曲线）。
   * 无依据 ⇒ 零动作（不启动 Host、不发 HTTP、不写硬件）。
   */
  async reattachAfterRendererRestart(): Promise<boolean> {
    if (!this.enabled || this.isDisabledState()) return false;
    if (this.hasActiveControlIntent()) return this.reattachExistingControlSession();
    const snapshot = await this.readNativeActivity().catch(() => null);
    if (!snapshot) return false;
    if (snapshot.active === true) {
      if (!await this.adoptNativeIntentSnapshot('renderer-reattach', snapshot)) return false;
      return this.reattachExistingControlSession();
    }
    if (snapshot.recoverable === true) return this.recoverAfterUncleanExit();
    return false;
  }

  /**
   * FAN-927 §5.2：同一系统启动内**主程序非正常退出**后，用户重新打开时的一次有界恢复。
   *
   * 资格完全由 native 记录裁定（前 run 非正常结束 + 记录未被撤销 + 同一系统启动 +
   * 同一 run 令牌 + 记录中确有曲线 + 包代可匹配）。前端额外只在**设备类**与崩溃前记录
   * 不一致时拒绝（不写硬件）。正常首次打开、正常退出后再开、仅配置过设备、普通开机启动
   * 都不满足 native 的资格，因此不会自动开启。
   *
   * 只执行一次（`mutateWithBoundedRetry` 的既有 1 次重试即总上限）；失败给可重试结果，
   * 不按每次 renderer-ready 无限复活。
   */
  async recoverAfterUncleanExit(): Promise<boolean> {
    if (!this.enabled || this.isDisabledState()) return false;
    if (this.hasActiveControlIntent()) return false;
    const snapshot = await this.readNativeActivity().catch(() => null);
    if (!snapshot || snapshot.recoverable !== true) {
      fanDiagnosticLog('lifecycle.crash-recovery-nowork', {
        state: this.state,
        recoverable: snapshot?.recoverable === true,
      });
      return false;
    }
    const curve = Array.isArray(snapshot.curve) ? cloneFanNodes(snapshot.curve) : [];
    if (curve.length === 0) return false;
    // 先建立/接管会话（一次受控启动或 adopt），再按记录比对设备；不匹配就不写。
    if (this.state === 'stopped') {
      const gate = await this.startInternal();
      if (!gate.allowed) {
        fanDiagnosticLog('lifecycle.crash-recovery-nowork', { state: this.state, reason: gate.reason });
        return false;
      }
    }
    if (snapshot.deviceClass && this.deviceClassValue && snapshot.deviceClass !== this.deviceClassValue) {
      fanLifecycleEvidence('lifecycle.crash-recovery-device-mismatch', {
        recorded: snapshot.deviceClass,
        current: this.deviceClassValue,
      });
      return false;
    }
    this.desiredCurve = cloneFanNodes(curve);
    this.resumeCurve = cloneFanNodes(curve);
    const intentRevision = this.controlIntentRevision;
    const context = this.admissionContext('recover-after-unclean-exit', intentRevision);
    try {
      await this.mutateWithBoundedRetry(
        context,
        () => {
          fanDiagnosticLog('lifecycle.crash-recovery-begin', { state: this.state, nodeCount: curve.length });
          return this.applyMutation(curve);
        },
        (attempt, error) => fanDiagnosticLog('lifecycle.crash-recovery-failure', {
          attempt,
          state: this.state,
          error: describeUnknownError(error),
        }),
        'Fan 崩溃恢复失败',
      );
      this.resumeCurve = null;
      fanLifecycleEvidence('lifecycle.crash-recovery-restored', {
        state: this.state,
        revision: snapshot.revision,
        generation: this.powerGeneration,
      });
      return true;
    } catch (error) {
      fanDiagnosticLog('lifecycle.crash-recovery-unavailable', {
        state: this.state,
        error: describeUnknownError(error),
      });
      return false;
    }
  }

  /**
   * Resume shares one promise for the current power generation. The bounded
   * request observes the Host recovery and replays the retained curve; a missed
   * Host wake sender or deadline hands off to the same continuous recovery owner.
   * HC Open/OpenEvents still precede writes, without requiring OEM readback.
   */
  async resume(): Promise<void> {
    // `fan.resume-ready` and the committed `power.resumed` fallback can be
    // delivered in either order.  Both edges must consume one recovery owner
    // and one Host Open/OpenEvents chain for the same generation.
    if (this.resumePromise) return this.resumePromise;
    const pending = this.resumeInternal();
    this.resumePromise = pending;
    try {
      await pending;
    } finally {
      if (this.resumePromise === pending) this.resumePromise = null;
    }
  }

  /**
   * Native's committed `power.resumed` edge is a stronger Fan wake fact than
   * the optional one-shot `fan.resume-ready` notification.  If the latter was
   * lost while WebView2 or the renderer was being recreated, promote the
   * committed generation into the Fan-local admission gate and run the normal
   * resume owner.  This does not inspect or invent OEM evidence and never
   * opens HC when there is no active Fan control intent.
   */
  async resumeAfterCommittedPowerWake(generation: number): Promise<void> {
    if (!this.enabled || this.isDisabledState()) return;
    if (!Number.isSafeInteger(generation) || generation <= 0) return;
    const currentGeneration = Math.max(0, Math.floor(this.powerGeneration));
    if (generation !== currentGeneration) {
      fanDiagnosticLog('lifecycle.resume-committed-wake-ignored', {
        generation,
        currentGeneration,
        reason: 'stale-generation',
      });
      return;
    }
    const intentAvailable = this.hasActiveControlIntent() || await this.adoptRecoverableControlIntent();
    if (!intentAvailable) {
      fanLifecycleEvidence('lifecycle.resume-committed-wake-nowork', {
        generation,
        reason: 'no-active-control-intent',
      });
      return;
    }
    if (!this.coordinator.isWakeReady(generation)) {
      // `power.resumed` is native's committed system wake boundary.  It is
      // safe to establish the Fan-local ready edge here; unlike a click or an
      // inputReady snapshot, it is not a hardware/OEM write acknowledgement.
      this.observePowerBoundary('resume-ready', generation);
      fanLifecycleEvidence('lifecycle.resume-committed-wake-admitted', {
        generation,
        source: 'native.power.resumed',
        hcWritesAdmitted: false,
      });
    }
    // If the ordinary F5 handler won the race, do not run the compensation's
    // rearm beside it.  Await the shared resume body; after it settles the
    // normal recovery owner remains responsible for any transient failure.
    if (this.resumePromise) {
      await this.resumePromise;
      return;
    }
    // The normal F5 path is Host-owned and has already rebuilt HC before the
    // renderer calls `resume()`.  The committed-edge compensation exists
    // specifically for the case where that one-shot trigger was lost, so
    // explicitly run the same Fan rearm admission here before replaying the
    // curve.  This remains the single serialized Host resume path.
    await this.rearmAfterWakeForControlAdmission('committed-power-resume', false);
    await this.resume();
  }

  private async resumeInternal(): Promise<void> {
    if (!this.enabled || this.isDisabledState()) return;
    const generation = Math.max(0, Math.floor(this.powerGeneration));
    if (!this.coordinator.isWakeReady(generation)) {
      throw new Error(`FAN_RESUME_READY_REQUIRED:generation=${generation}`);
    }
    // FAN-927 §3：**意图前置**。自动唤醒在第一行就判资格：本 run 已确认的活动控制意图，
    // 或 §5 确认的有效恢复意图。没有就立即 NoWork/not-enabled——零 Fan HTTP、零进程枚举、
    // 零 Host 启动、零 wait-started；不输出"恢复失败"这种误导性错误。
    if (!this.hasActiveControlIntent()) {
      const adopted = await this.adoptRecoverableControlIntent();
      if (!adopted) {
        fanDiagnosticLog('lifecycle.resume-nowork', {
          generation,
          reason: 'no-active-control-intent',
          state: this.state,
        });
        fanLifecycleEvidence('lifecycle.resume-nowork', {
          generation,
          reason: 'no-active-control-intent',
          state: this.state,
        });
        return;
      }
    }
    // R2（FAN-926R 裁决 §4.2/§4.3）+ G7（执行单 §3.2/§3.3）：等待与重入场都**不在串行队列内**
    // ——真机 20:13:50 的缺陷正是"宿主重建要 ~10 s，渲染侧 40 ms 内两次尝试就被 POWER_RESUMING
    // 拒完并永久放弃"。这里由 mutateWithBoundedRetry 做一次有界、可取消、同代共享的只读等待，
    // 队列内只执行一次真实写入；disable/close/新睡眠/新曲线能在各自入队点**同步**取消它。
    const intentRevision = this.controlIntentRevision;
    const context = this.admissionContext('resume', intentRevision, generation);
    // The bounded request is only one attempt of the retained control intent.
    // A confirmed wake can arrive before the Host F5 sender: waiting then times
    // out while the Host still reports Suspended. Hand this recoverable deadline
    // to the same owner, which drives the existing resume/Open/OpenEvents chain.
    // Closing, a newer generation or a superseding curve invalidate this context;
    // credentials and route conflicts remain failures, never write admission.
    try {
      await this.mutateWithBoundedRetry(
        context,
        () => this.replayResumeCurve(generation, intentRevision),
        (attempt, error) => fanDiagnosticLog('lifecycle.resume-failure', { attempt, state: this.state, error: describeUnknownError(error) }),
        'Fan 唤醒恢复失败',
      );
    } catch (error) {
      const requestStillCurrent = this.isResumeReplayCurrent(generation, intentRevision);
      const deadlineExpired = /^FAN_RESUME_WAIT_DEADLINE:/.test(describeUnknownError(error));
      if (requestStillCurrent && this.hasActiveControlIntent() && this.hasWakeContext(generation)
          && !isNonRetryableControlError(error) && !isExternalFanControlConflict(error)
          && (isPowerResumingError(error) || deadlineExpired)) {
        this.noteRecoveryIntentPending('automatic-resume', generation);
        fanDiagnosticLog('lifecycle.resume-recovery-handoff', {
          generation, state: this.state, error: describeUnknownError(error),
        });
      }
      throw error;
    }
  }

  /**
   * §3.3.2：唤醒重放的**任务身份**复核（请求 revision + 电源代次）。队列内的每个 await
   * 边界（读快照之前**与**之后、最终写之前）都必须过这一关；被取代/换代即刻停手且不写。
   */
  private isResumeReplayCurrent(generation: number, intentRevision: number): boolean {
    return intentRevision === this.controlIntentRevision
      && generation === Math.max(0, Math.floor(this.powerGeneration));
  }

  private logResumeReplaySuperseded(generation: number, intentRevision: number): void {
    fanDiagnosticLog('lifecycle.resume-replay-superseded', {
      state: this.state,
      intentRevision,
      currentIntentRevision: this.controlIntentRevision,
      generation,
      currentGeneration: this.powerGeneration,
    });
  }

  /** 队列内的**单次**唤醒重放：事件驱动重建（无 adapter.resume/协商/观察窗）。 */
  private async replayResumeCurve(generation: number, intentRevision: number): Promise<void> {
    fanDiagnosticLog('lifecycle.resume-begin', { state: this.state });
    if (!this.enabled || this.isDisabledState()) return;
    // §4.3：最终写前核请求 revision 与电源代次——等待期间用户新曲线/预设、disable/close
    // 或新一代睡眠都必须让**旧的** resume 重放失效（新意图优先，旧代 Ready 不放行）。
    // Host/会话身份、controlAccepting 与无阻断清理由 adapter 的认证/lease 门与
    // applyMutation 的协调器写入准入各自把关，这里不重复实现、也不放行旧代回执。
    if (!this.isResumeReplayCurrent(generation, intentRevision)) {
      this.logResumeReplaySuperseded(generation, intentRevision);
      return;
    }
    if (!this.process && (this.state === 'stopped' || this.state === 'fault-locked' || this.state === 'unknown')) {
      await this.startInternal();
    }
    const remote = await this.adapter.getState().catch(() => null);
    // §3.3.2：这段只读快照的 await 期间到达的终止性意图/新电源代次必须让旧重放停手，
    // 且**不得采纳**这枚迟到快照（旧回包不能清新 lease、覆盖新 phase）。
    if (!this.isResumeReplayCurrent(generation, intentRevision)) {
      this.logResumeReplaySuperseded(generation, intentRevision);
      return;
    }
    this.syncRemoteSessionState(remote ?? undefined);
    if (this.desiredCurve) {
      // E9（六十八批 T0 删减，HC 锚点：IDevice.cs:410-423 事件驱动 SetFanControl；生命周期零原创）
      // 事件驱动重建：直接以 SetFanControl(true/曲线) 语义重写曲线，无 adapter.resume/协商/观察窗。
      // 920-v1.2 §14.2 W1：单次真实写入，重试/等待由 mutateWithBoundedRetry 在队列**外**统一负责。
      this.beginCoordinatorSession();
      this.admitCoordinatorHcReady();
      this.setState('awaiting-control');
      await this.applyMutation(this.desiredCurve as readonly FanNode[], intentRevision);
      this.resumeCurve = null;
      fanDiagnosticLog('lifecycle.resume-curve-restored', { state: this.state });
      return;
    }
    // 无曲线：仅恢复协调状态。
    this.beginCoordinatorSession();
    this.admitCoordinatorHcReady();
    this.setState('awaiting-control');
  }

  async close(): Promise<void> {
    this.aiMockCloseReceipt = null;
    // R2（§4.3）：关闭是终止性意图——同步使旧恢复等待/旧重放失效（入队前）。
    this.controlIntentRevision += 1;
    this.cancelResumeWait('close');
    // FAN-938 R5 §3：close 是终止性意图，取消 Fan 恢复 owner（不再持续重试）。
    this.cancelRecovery('close');
    return this.enqueue(() => this.closeInternal());
  }

  /** Same authenticated Close owner; called directly only while already on the Fan queue. */
  private async closeInternal(): Promise<void> {
    fanDiagnosticLog('lifecycle.close-begin', { state: this.state });
    if (!this.enabled || this.state === 'disabled' || (this.state === 'stopped' && !this.closedHostAwaitingExit)) return;
    try {
      // E9（六十八批 T0 删减，HC 锚点：IDevice.cs:410-423 事件驱动 SetFanControl；生命周期零原创）
      // 不再 disarm fan-guard（guard 保活已删）。Close→SetFanControl(false) 交回即 HC 本义；
      // lease 清空时同步停掉续租心跳（租约协议完整性，第七十七批 C）。
      // Match HC Window_Closed: process exit owns one virtual Close. Do
      // not inject a separate Hardware-profile handoff before it; that
      // would add ACPI work and a second lifecycle owner to the same close.
      // 920 F-2c：撤销写许可**放在 Close 边界确认之后**。
      // 920-v1.2 §14.2 W1 更正范围：`this.enqueue` 只串行化**渲染侧**的生命周期操作
      // （apply/applyPreset/resume/suspend/close/disable/start/heartbeat）。它**不**覆盖
      // native 的 F4 `/api/suspend`、Host 自身的 recovery 重试定时器与 HC 内部回调——
      // 那些在 Host 侧由 `lock (gate)` 串行，属**另一个**边界，不能由前端单队列外推为
      // 跨进程全局互斥。因此"Close 期间无并发写入"只在渲染侧成立；而一旦 Close 失败，
      // 先撤销会把协调器停在 stopping（wakeReadyGeneration=null），使后续显式控制请求被
      // native-resume-ready-not-observed 拒绝，只能等下一次电源事件——这正是 F-2c 的卡死。
      // 成功路径上 markStopped() 已覆盖撤销的全部效果。
      await this.closeHostAfterRestore();
      this.revokeCoordinatorWrites('shutdown');
      this.lease = null;
      this.stopHeartbeat();
      this.coordinatorSession = null;
      this.opened = false;
      this.eventsOpened = false;
      this.activeCurve = null;
      this.resumeCurve = null;
      this.desiredCurve = null;
      this.setState('stopped');
      this.coordinator.markStopped();
      this.writeReady = false;
    } catch (error) {
      fanDiagnosticLog('lifecycle.close-failure', { state: this.state, error: error instanceof Error ? error.message : String(error) });
      // E9（六十八批 T0 删减，HC 锚点：IDevice.cs:410-423 事件驱动 SetFanControl；生命周期零原创）
      // 不再 waitForHostRecovery/finalizeConfirmedClose/setState('unknown')：保持当前状态 + 抛错。
      throw error;
    }

  }

  /** Close/confirm first, request shutdown second, force-stop only as a
   * compatibility fallback after restore has already been confirmed. */
  private async closeHostAfterRestore(): Promise<void> {
    if (!this.process) {
      if (this.opened || this.lease) throw new Error('Fan Host 进程已丢失，硬件状态无法确认');
      return;
    }
    if (this.closedHostAwaitingExit) {
      // The accepted HC Close receipt remains bound to this retained owner.
      // Retrying exit never invokes virtual Close or reopens the old writer.
      await this.stopProcessOnly();
      return;
    }
    let closed: FanState;
    try {
      closed = await this.adapter.close();
    } catch (error) {
      // 920 F-2b：Close 请求失败/响应丢失时**绝不**重发第二次 Close（那会把
      // 两个 Close 叠在同一个串行门上）。HC 没有取消在途 ACPI/HID 调用的原语，
      // 常驻 Host 可能仍在自行收尾——只观察它自己确认的安全停止边界；观察不到
      // 就保持 fail-closed 并把原始错误抛出。
      // 这同时是 920 F-2c 要求的恢复路径：Host 一旦确认停止，close() 正常完成并
      // 走 markStopped()，下一次显式控制请求即可重新开始，**不需要**等到下次
      // 电源/恢复事件。
      const observed = await this.observeHostCloseAfterFailure(error);
      if (!observed) throw error;
      closed = observed;
    }
    this.syncRemoteSessionState(closed);
    if (closed.hcCloseCleanupPending === true) {
      // 920 F-2a：HC 已写出默认风扇表但虚拟 Close 仍在收尾。等 Host 的显式
      // pending 标记消失，而不是对同一个串行门重发 /api/close。
      await this.waitForHcCloseCleanup();
      closed = await this.adapter.getState();
      this.syncRemoteSessionState(closed);
    }
    if (this.config.aiMockSession) {
      // A null-backend mock must never manufacture HC/ACPI acknowledgements.
      // Its own explicit simulated close evidence is a separate test boundary.
      if (!aiFanMockStateAllowed(closed) || closed.mockCloseCompleted !== true ||
          String(closed.state).toLowerCase() !== 'stopped' || closed.openCalled === true ||
          closed.openEventsCalled === true || closed.lease) throw new Error('AI_FAN_MOCK_CLOSE_NOT_CONFIRMED');
      this.aiMockCloseReceipt = aiFanRuntimeEvidence(closed);
    } else {
      assertHcSessionClosed(closed, 'Fan Host close');
      if (!isSafeHostRecoveryState(closed, true)) {
        throw new Error(`Fan Host close：远程状态未进入安全停止边界（${String(closed.state ?? 'unknown')}）`);
      }
    }
    this.closedHostAwaitingExit = true;
    try { await this.adapter.shutdown(); } catch { /* legacy Host: safe fallback below */ }
    await this.stopProcessOnly();
  }

  /**
   * 920 F-2b/F-2c：Close 失败后**只观察、不重发**。Host 用显式 pending 标记或
   * 错误文本报告"仍在收尾"时等它结束；否则等它自己确认的安全停止边界。有界窗口
   * 内都未出现则返回 null（调用方保持 fail-closed，不伪造成功）。
   */
  private async observeHostCloseAfterFailure(error: unknown): Promise<FanState | null> {
    if (isHcCloseCleanupPending(error)) {
      try {
        await this.waitForHcCloseCleanup();
      } catch {
        return null;
      }
      return await this.adapter.getState().catch(() => null);
    }
    const deadline = Date.now() + 16000;
    while (Date.now() < deadline) {
      const remote = await this.adapter.getState().catch(() => null);
      if (remote && isSafeHostRecoveryState(remote, true)) return remote;
      await new Promise<void>((resolve) => setTimeout(resolve, 250));
    }
    return null;
  }

  /**
   * HC has no cancellation primitive for an in-flight ACPI/HID call. When the
   * Host has already written the HC default table but its virtual Close is
   * still unwinding, wait for the Host's explicit pending marker instead of
   * retrying /api/close into the same serialized gate.
   */
  private async waitForHcCloseCleanup(): Promise<void> {
    const deadline = Date.now() + 16000;
    while (Date.now() < deadline) {
      const remote = await this.adapter.getState().catch(() => null);
      // A transport proxy or a legacy adapter can return a partial object.
      // Do not treat “pending field absent” as completion while waiting for a
      // real HC Close boundary; require a state label and reject explicit
      // incomplete Close/Stop evidence before retrying /api/close.
      if (remote && typeof remote.state === 'string' &&
          remote.hcCloseCleanupPending !== true &&
          remote.unknownState !== true &&
          !hasExplicitIncompleteHcCloseEvidence(remote, true)) return;
      await new Promise<void>((resolve) => setTimeout(resolve, 250));
    }
    throw new Error('HC_CLOSE_PENDING: HC Close 资源清理在等待窗口内未完成');
  }

  // E9（六十八批 T0 删减，HC 锚点：IDevice.cs:410-423 事件驱动 SetFanControl；生命周期零原创）
  // recoverLockedHostBeforeStart / recoverAfterMutationFailure / recoverAfterHidRemoval / isObservedRouteLoss 已删。

  private async restoreAndRelease(): Promise<void> {
    if (this.lease) {
      try {
        const cleanupLeaseId = this.lease.leaseId;
        const restored = await this.adapter.restoreOem(cleanupLeaseId);
        assertOemRestoreConfirmed(restored, 'Fan Host restore');
        this.syncRemoteSessionState(restored);
        const released = await this.adapter.releaseControl(cleanupLeaseId);
        assertOemRestoreConfirmed(released, 'Fan Host release');
        this.syncRemoteSessionState(released);
        this.lease = null;
        this.stopHeartbeat();
      } catch (error) {
        // Never discard the lease on an unconfirmed restore. Keeping it makes
        // the resident Host reachable for a later automatic retry.
        throw error;
      }
    } else if (this.opened) {
      const restored = await this.adapter.restoreOem();
      assertOemRestoreConfirmed(restored, 'Fan Host restore');
      this.syncRemoteSessionState(restored);
    }
  }

  /**
   * E5（五十五批 ①）：suspend 事件窗判定——coordinator 相位处于
   * suspending/suspended/resuming（fanHost.ts 本机睡眠→恢复边界），
   * 与远端 remote.powerState === 'suspended' 互为补充。
   */
  private isSleepWindow(): boolean {
    const phase = this.coordinator.snapshot().phase;
    return phase === 'suspending' || phase === 'suspended' || phase === 'resuming';
  }

  /**
   * 203/Fan b141（FAN925B §5.1 副作用，b138 实测）：启动中止路径**绝不允许**经未绑定
   * 会话凭据的 adapter 发出生命周期请求 —— 此前会在"未 open / 未 lease"时发出**无会话头**
   * 的 `POST /api/shutdown`。这里先补绑 sidecar 令牌；仍无 ⇒ 返回 false（fail-closed）。
   */
  private async bindSessionTokenForLifecycleAbort(): Promise<boolean> {
    if (!isSessionToken(this.config.sessionToken)) {
      try {
        const persisted = (await fs.readTextFile(this.config.sessionTokenPath, 4096)).trim();
        if (isSessionToken(persisted)) this.config.sessionToken = persisted;
      } catch { /* no sidecar */ }
    }
    if (!isSessionToken(this.config.sessionToken)) return false;
    // 必须**总是**把解析到的令牌推给 adapter：adapter 是在 config 还没有令牌时构造的
    // （启动早期），而中止路径位于握手绑定之前 ⇒ 只判"config 里有令牌"会让请求仍然**不带
    // 会话头**。2026-09-25 18:40 实机 401 线实测抓到 no-header POST /api/shutdown 正是此形态。
    const bindable = this.adapter as Partial<Pick<FanApiAdapter, 'setSessionToken'>>;
    if (typeof bindable.setSessionToken === 'function') {
      bindable.setSessionToken(this.config.sessionToken);
    }
    return true;
  }

  private async safeAbortAfterStart(): Promise<void> {
    // FAN-941: a launch refusal, or a child reclaimed inside the launcher,
    // gives this lifecycle no process/session ownership. A cached token is
    // not permission to close or shut down whichever listener is at the port.
    if (!this.process) return;
    // E9（六十八批 T0 删减，HC 锚点：IDevice.cs:410-423 事件驱动 SetFanControl；生命周期零原创）
    // 不再 waitForHostRecovery / finalizeConfirmedClose（E9 删除面）；
    let closeConfirmed = !this.opened && !this.eventsOpened && !this.lease;
    if (!(await this.bindSessionTokenForLifecycleAbort())) {
      // fail-closed：没有可绑定的会话凭据 ⇒ 不发任何生命周期请求（不 shutdown、不 close、
      // 不停进程），把命名的 Host 保留为唯一恢复 owner；parent watchdog 仍是独立触发。
      fanDiagnosticLog('lifecycle.start-abort-deferred', {
        reason: 'credential-unavailable',
        closeConfirmed,
        pid: this.process?.pid ?? 0,
        sessionTokenPath: this.config.sessionTokenPath,
        policy: 'fail-closed;no-unauthenticated-lifecycle-request;no-second-owner',
      });
      return;
    }
    try {
      if (!closeConfirmed) {
        // Process-start rollback follows HC Window_Closed: one virtual Close
        // owns the device/ACPI/HID release.  Do not prepend a profile restore
        // or send another Close from the catch block; the Host's serialized
        // recovery worker is the only retry owner.
        const closed = await this.adapter.close();
        this.syncRemoteSessionState(closed);
        assertHcSessionClosed(closed, 'Fan Host 启动回滚 close');
        closeConfirmed = true;
      }
    } catch {
      // The first Close may have reached the Host even when its response was
      // lost or incomplete.  Observe only; never issue a second Close.  On an
      // unconfirmed close keep the current state (no fault-lock, no observer).
      closeConfirmed = false;
    }
    if (this.process?.adopted) {
      fanDiagnosticLog('lifecycle.start-abort-keep-resident', { pid: this.process.pid, closeConfirmed });
      return;
    }
    if (closeConfirmed) {
      try { await this.adapter.shutdown(); } catch { /* old Host: force-stop only after restore confirmation */ }
      this.lease = null;
      await this.stopProcessOnly();
      this.opened = false;
      this.eventsOpened = false;
      this.activeCurve = null;
      this.resumeCurve = null;
    }
  }

  private async stopProcessOnly(): Promise<void> {
    const process = this.process;
    if (!process) return;
    // Retain the process handle until the launcher confirms termination. If
    // stop itself fails, callers must remain able to retry close/recovery.
    await this.launcher.stop(process);
    this.process = null;
    this.closedHostAwaitingExit = false;
  }

  /**
   * FAN-927（2026-09-27）§4.3：**明确修订** 2026-09-12 第七十七批 C-②"每次 mutation
   * 前 heartbeat 预检"。当前实例/代际内已由 acquire/adopt 确认的 lease 直接携带写入，
   * 不再为一次普通 apply/preset 额外花掉一个往返（HC 风扇路径本没有本 Host 的 15 s 租约，
   * 该预检不是保持它的依据）。
   *
   * 保留的授权校验：Host 侧 `Enable → EnsureLeaseFromBody` 在**真正的曲线写之前**仍是
   * 权威；周期续租（`startHeartbeat`）继续承担 TTL=15 s 的续租侧义务。真实写返回
   * `LEASE_INVALID` 时由调用方丢弃本地 lease，`mutateWithBoundedRetry` 的一次有界重试
   * 会走到这里的 fresh acquire 并重提原请求。
   */
  private async ensureLeaseForMutation(): Promise<void> {
    if (this.lease) {
      this.startHeartbeat();
      return;
    }
    this.lease = await this.adapter.acquireControl();
    this.startHeartbeat();
  }

  // E9（六十八批 T0 删减，HC 锚点：IDevice.cs:410-423 事件驱动 SetFanControl；生命周期零原创）
  // armFanGuard / disarmFanGuard / emitFanGuardState 已删（guard 保活链不复活）；
  // stopHeartbeat 保留使用——租约续租属协调层协议完整性（第七十七批 C）。

  private remoteFanControlHealthy(remote: FanState): boolean {
    if (this.classifyControlAdmission(remote, true) !== 'accepting') return false;
    const state = String(remote.state ?? '').toLowerCase();
    // Reusing a resident Host is an HC SystemReady admission, not a mere
    // transport-health check. Protocol-2 state snapshots provide all of this
    // evidence, so absent telemetry must take the normal serialized rebuild
    // path rather than promoting the coordinator from a guessed ready state.
    if (state !== 'ready' || String(remote.protocolVersion ?? '') !== '2') return false;
    const controlEnabled = this.config.aiMockSession
      ? aiFanMockStateAllowed(remote) && remote.mockControlEnabled === true : remote.hardwareWritesEnabled === true;
    if (!controlEnabled || remote.unknownState === true ||
        remote.oemRestoreConfirmed === true || remote.hcCloseCleanupPending === true) return false;
    if (remote.openCalled !== true || remote.openEventsCalled !== true) return false;
    const remoteLease = remote.lease;
    if (!this.lease || !remoteLease || remoteLease.leaseId !== this.lease.leaseId) return false;
    const remoteLeaseGeneration = remote.leaseGeneration ?? remoteLease.generation;
    if (remoteLeaseGeneration !== undefined && this.lease.generation !== undefined &&
        remoteLeaseGeneration !== this.lease.generation) return false;
    return true;
  }
}

/** Product singleton. Importing this module has no native side effects. */
export const fanHostLifecycle = new FanHostLifecycle({ enabled: FAN_REAL_HOST_ENABLED });

export function getFanHostLifecycle(): FanHostLifecycle {
  return fanHostLifecycle;
}
