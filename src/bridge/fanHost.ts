import { app, fs, http, proc, shell } from './api';
import {
  createFanApiAdapter,
  type FanApiAdapter,
  type FanHandshake,
  type FanLease,
  type FanNode,
  type FanState,
} from './fanApi';
import { fanDiagnosticLog, setFanDiagnosticPowerGeneration } from './fanDiagnostics';

/**
 * Real-host integration boundary.
 *
 * This flag is deliberately independent from the UI import flag.  Keeping a
 * second gate makes the real Host independently reversible from the UI import.
 */
export const FAN_REAL_HOST_ENABLED = true;
export const FAN_HOST_PROTOCOL_VERSION = 2;
export const FAN_HOST_DEFAULT_PORT = 8765;
// YeManFanHost currently expires leases after 15 seconds. Renew well before
// that boundary so a visible UI pause, HC call, or WebView timer jitter cannot
// turn the next curve operation into a stale-lease failure.
const DEFAULT_LEASE_RENEWAL_INTERVAL_MS = 5000;
const RECOVERY_RETRY_DELAYS_MS = [0, 250, 750];
// Lease heartbeat is deliberately separate from the normal read-only session
// observer. The former renews HC ownership; the latter only checks state and
// must not be shortened to the lease cadence.
const NORMAL_SESSION_CHECK_INTERVAL_MS = 30_000;
const RECOVERY_OBSERVATION_INTERVAL_MS = 2_000;
const RECOVERY_OBSERVATION_MAX_ATTEMPTS = 30;
const RECOVERY_WINDOW_MS = 60_000;
const RECOVERY_MAX_ATTEMPTS = 3;
// A real Host owns the serialized HC recovery timer.  The UI must observe that
// owner instead of issuing a second restore/close request while its ACPI/HID
// call is still unwinding. Recovery observation is a bounded 2s x 30 window;
// within that window at most three route-rebuild triggers are allowed.

async function waitForRecoveryRetry(attempt: number): Promise<void> {
  const delay = RECOVERY_RETRY_DELAYS_MS[Math.min(attempt, RECOVERY_RETRY_DELAYS_MS.length - 1)];
  if (delay > 0) await new Promise<void>((resolve) => setTimeout(resolve, delay));
}

function cloneFanNodes(nodes: readonly FanNode[]): FanNode[] {
  return nodes.map(({ tempC, dutyPercent }) => ({ tempC, dutyPercent }));
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
    version: '0.32.3.2',
    sha256: '70e27fd4d73a5ca3e3e750de2736b5e1c3b126d716dd9f4f5794c84da88c6415',
  },
  { file: 'GamepadMotion.dll' },
  { file: 'hidapi.net.dll', sha256: 'adc343b824405081a1b3ec69b06b4808734fc448ee757d0ea7b723acddca3182' },
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
  hostExecutable: string;
  hostDirectory: string;
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

export interface FanHostDependencyCheck {
  ok: boolean;
  missing: string[];
  hashMismatches: string[];
}

function trimWindowsPath(value: string): string {
  return value.replace(/[\\/]+$/, '');
}

function joinWindowsPath(root: string, child: string): string {
  return `${trimWindowsPath(root)}\\${child}`;
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
  const hostDirectory = joinWindowsPath(powerControlDir, 'fan-host');
  return {
    hostDirectory,
    hostExecutable: joinWindowsPath(hostDirectory, 'YeManFanHost.exe'),
    hcAssemblyPath: joinWindowsPath(hostDirectory, 'HandheldCompanion.dll'),
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

/** Validate the pinned runtime before any HC assembly can be loaded. */
export async function validateFanHostDependencies(config: FanHostConfig): Promise<FanHostDependencyCheck> {
  const missing: string[] = [];
  const hashMismatches: string[] = [];
  for (const dependency of config.dependencies) {
    const path = joinWindowsPath(config.hostDirectory, dependency.file);
    if (!(await fs.exists(path))) {
      missing.push(dependency.file);
      continue;
    }
    if (!dependency.sha256) continue;
    const result = await shell.run('certutil.exe', ['-hashfile', path, 'SHA256'], 10000);
    const actual = `${result.stdout}\n${result.stderr}`.match(/\b[0-9a-f]{64}\b/i)?.[0]?.toLowerCase();
    if (!actual || actual !== dependency.sha256.toLowerCase()) hashMismatches.push(dependency.file);
  }
  return { ok: missing.length === 0 && hashMismatches.length === 0, missing, hashMismatches };
}

export interface FanHostProcess {
  pid: number;
  executable: string;
}

export interface FanHostLauncher {
  start(config: FanHostConfig): Promise<FanHostProcess>;
  stop(process: FanHostProcess): Promise<void>;
}

/** Native launcher is only called after the independent real-host Gate. */
export class NativeFanHostLauncher implements FanHostLauncher {
  async start(config: FanHostConfig): Promise<FanHostProcess> {
    if (!(await fs.exists(config.hostExecutable))) {
      throw new Error(`Fan Host 不存在: ${config.hostExecutable}`);
    }
    // Installed binaries are immutable release payloads. Keep the mutable
    // loopback capability in the current user's application-data directory,
    // not beside the Host executable where an updater/extractor may replace
    // files while a hardware session exists.
    // Keep the session capability in the stable native Fan Host directory.
    // app.dataDir() follows the configurable window title and caused an old
    // Host to become unreachable after a title/configuration change.
    const fanStateDirectory = await app.fanStateDir();
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
    // Recover an exact resident Host before touching the immutable payload.
    // The ACL installer quarantines stale files and rewrites payload ACLs;
    // doing that while an older Host still owns HC/ACPI/HID can race its
    // loaded assembly or leave an update half-applied while hardware is live.
    // The recovery path is authenticated when a session sidecar exists and
    // falls back to the exact-image legacy path only for pre-token Hosts.
    await this.recoverPreviousHostBeforePayloadMutation(config);
    await this.installAndVerifyPayload(config, fanStateDirectory);
    if (!config.sessionToken) {
      config.sessionToken = createSessionToken();
      await fs.writeTextFileAtomic(config.sessionTokenPath, config.sessionToken);
    }
    // Atomic-write success is not enough for a recovery capability: verify the
    // exact bytes that the next process and native exit fallback will read.
    const persistedToken = (await fs.readTextFile(config.sessionTokenPath, 4096)).trim();
    if (!isSessionToken(persistedToken) || persistedToken.toLowerCase() !== config.sessionToken.toLowerCase()) {
      throw new Error(`Fan Host 会话令牌未可靠落盘：${config.sessionTokenPath}`);
    }
    const dependencies = await validateFanHostDependencies(config);
    if (!dependencies.ok) {
      const details = [...dependencies.missing.map((name) => `缺失:${name}`), ...dependencies.hashMismatches.map((name) => `哈希不匹配:${name}`)];
      throw new Error(`Fan Host 依赖校验失败: ${details.join(', ')}`);
    }
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
    try {
      await this.waitForReady(config);
    } catch (error) {
      // The lifecycle has not received a process handle yet. Reclaim the
      // exact child here or a failed readiness probe would leak a listener
      // that blocks the next start and may retain HC ownership.
      await this.stop({ pid: launched.pid, executable: config.hostExecutable }).catch(() => {});
      throw error;
    }
    return { pid: launched.pid, executable: config.hostExecutable };
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
   * A ZIP extraction and an application update inherit ACLs differently.
   * Run one authoritative installer for both cases before a token is read or
   * HC can be loaded. The script validates the manifest before it changes
   * ACLs, quarantines stale Host files, then verifies the final boundary.
   */
  private async installAndVerifyPayload(config: FanHostConfig, fanStateDirectory: string): Promise<void> {
    const installer = joinWindowsPath(config.hostDirectory, 'install-fan-host-payload.ps1');
    const manifest = joinWindowsPath(config.hostDirectory, 'YeManFanHost.payload.json');
    if (!(await fs.exists(installer)) || !(await fs.exists(manifest))) {
      throw new Error('Fan Host 部署不完整：缺少受保护载荷清单或安装器');
    }
    const result = await shell.run('powershell.exe', [
      '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
      '-File', installer,
      '-PayloadDirectory', config.hostDirectory,
      '-StateDirectory', fanStateDirectory,
    ], 60000);
    if (result.exitCode !== 0 || !/FAN_HOST_ACL_OK:/i.test(`${result.stdout}\n${result.stderr}`)) {
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
    if (verifiedOwner <= 0) {
      throw new Error(verifiedOwner < 0
        ? '旧 Fan Host 健康响应来自非当前 YeManFanHost，已阻止生命周期请求'
        : '旧 Fan Host 健康响应但未找到可验证的监听进程，已阻止生命周期请求');
    }
    let closeResult: any;
    try {
      // Match HC Window_Closed ownership: send exactly one Close request to
      // the resident Host. A lost/409/5xx response is ambiguous, so recovery
      // observes the original Host operation instead of posting a second
      // CurrentDevice.Close into the same ACPI/HID lifecycle.
      let closeResponse: { status: number; body: string } | undefined;
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
        throw new Error('旧 Host 未完成恢复或 HC Close 清理');
      }
      const shutdownResponse = await http.request(`${config.baseUrl}/api/shutdown`, {
        method: 'POST', headers: { 'Content-Type': 'application/json', 'X-YeMan-Fan-Session': config.sessionToken }, body: '{}',
      });
      if (shutdownResponse.status >= 300 && shutdownResponse.status !== 404) {
        throw new Error(`旧 Host shutdown 返回 ${shutdownResponse.status}`);
      }
    } catch (error) {
      throw new Error(`旧 Fan Host 无法安全退出：${error instanceof Error ? error.message : String(error)}`);
    }
    const deadline = Date.now() + 4000;
    while (Date.now() < deadline) {
      try {
        const response = await http.request(`${config.baseUrl}/health`, { method: 'GET', headers: { 'X-YeMan-Fan-Session': config.sessionToken } });
        if (response.status < 200 || response.status >= 300) return;
      } catch {
        return;
      }
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    // Older Hosts do not have /api/shutdown and used a name-only parent
    // watchdog. After their OEM restore is confirmed, terminate only the
    // exact YeManFanHost executable that owns this loopback port.
    const port = Number(new URL(config.baseUrl).port || 80);
    const netstat = await shell.run('netstat.exe', ['-ano', '-p', 'tcp'], 10000);
    const escapedPort = String(port).replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    const ownerMatch = `${netstat.stdout}\n${netstat.stderr}`.match(
      new RegExp(`127\\.0\\.0\\.1:${escapedPort}\\s+[^\\r\\n]*LISTENING\\s+(\\d+)`, 'i'),
    );
    const ownerPid = ownerMatch ? Number(ownerMatch[1]) : 0;
    if (ownerPid > 0) {
      const identity = await proc.identity(ownerPid).catch(() => null);
      const exactPath = identity?.path && identity.path.toLowerCase() === config.hostExecutable.toLowerCase();
      if (identity?.valid === true && exactPath) {
        await proc.terminateTree(ownerPid);
        await new Promise((resolve) => setTimeout(resolve, 250));
        try { await http.request(`${config.baseUrl}/health`, { method: 'GET', headers: { 'X-YeMan-Fan-Session': config.sessionToken } }); }
        catch { return; }
      }
    }
    throw new Error('旧 Fan Host 未释放本机端口，已阻止新的硬件接管');
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
    if (identity?.valid !== true) return -1;
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
    await proc.terminateTree(process.pid);
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
  if (!profile) return { allowed: false, writeReady: false, reason: '设备类型不在已映射风扇设备矩阵内' };

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
function assertHcSessionSuspended(state: FanState, context: string): void {
  if (!hasCompletedHcCloseBoundary(state, false)) assertOemRestoreConfirmed(state, context);
  if (state.openCalled === true || state.openEventsCalled === true) {
    throw new Error(`${context}：睡眠前 HC Open/OpenEvents 会话仍未释放`);
  }
  if ('hcVirtualCloseReturned' in state && state.hcVirtualCloseReturned !== true) {
    throw new Error(`${context}：睡眠前 HC 虚拟 Close 尚未确认返回`);
  }
}

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
 * A route-loss response is different from a normal lease expiry or write
 * rejection. The native Host has already stopped its temperature source and
 * owns the one HC Close/unbind boundary; the bridge must observe that boundary
 * and then rebuild the same curve on a newly stable HC route. This classifier
 * is used only by the heartbeat path, never by a user mutation or HTTP layer.
 */
function isHidRouteLoss(error: unknown): boolean {
  const message = error instanceof Error ? error.message : String(error);
  return /\b(?:HC_SESSION_ROUTE_LOST|HC_SESSION_UNAVAILABLE|hc-device-is-open-false|route-marker-lost)\b/i.test(message);
}

function isHcCloseCleanupPending(error: unknown): boolean {
  const message = error instanceof Error ? error.message : String(error);
  return /\bHC_CLOSE_PENDING\b|HC Close 资源清理/i.test(message);
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

export interface FanHostLifecycleOptions {
  enabled?: boolean;
  config?: FanHostConfig;
  adapter?: FanApiAdapter;
  launcher?: FanHostLauncher;
  savedIdentity?: Record<string, unknown> | null;
  heartbeatIntervalMs?: number;
  onDeviceIdentity?: (identity: Record<string, unknown>) => void;
  onState?: (state: FanHostLifecycleState) => void;
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
  private onDeviceIdentity?: (identity: Record<string, unknown>) => void;
  private readonly heartbeatIntervalMs: number;
  private savedIdentity: Record<string, unknown> | null;
  private stateValue: FanHostLifecycleState;
  private process: FanHostProcess | null = null;
  private lease: FanLease | null = null;
  private opened = false;
  private eventsOpened = false;
  private operation: Promise<unknown> = Promise.resolve();
  private heartbeatTimer: ReturnType<typeof setTimeout> | null = null;
  private sessionCheckTimer: ReturnType<typeof setTimeout> | null = null;
  private sessionCheckInFlight = false;
  // A normal session check is intentionally sparse. Once a read-only check
  // observes that an external controller/OEM has taken the route, keep the
  // same lifecycle owner but poll at the Phase-2 2s cadence for one bounded
  // 60s recovery window. This is YeMan observation/reacquisition only; HC
  // Open/OpenEvents/lease/curve ordering remains the single write path.
  private sessionRecoveryBurstUntil = 0;
  private sessionRecoveryAttempts = 0;
  // Once a burst exhausts its three triggers, do not immediately start a new
  // burst for the same unresolved route loss. Normal 30s checks continue and
  // re-arm only after a healthy route is observed.
  private sessionRecoveryExhausted = false;
  private lastRecoveryPowerGeneration = 0;
  private sessionRecoveryInFlight = false;
  private sessionGeneration = 0;
  private powerGeneration = 0;
  private writeReady = false;
  // The resident Host owns Close/unbind. This bridge marker only prevents a
  // second route-rebuild waiter from being created if a timer and an explicit
  // heartbeat observe the same HID removal in one lifecycle turn.
  private recoveryOwner: 'none' | 'hid-route' = 'none';
  // The globally-owned lifecycle, not a mounted FanView, remembers the last
  // acknowledged curve. This keeps a full OEM -> fresh HC session -> replay
  // transaction possible after KeepAlive evicts the fan page during sleep.
  private activeCurve: FanNode[] | null = null;
  private resumeCurve: FanNode[] | null = null;

  constructor(options: FanHostLifecycleOptions = {}) {
    this.enabled = options.enabled === true;
    this.config = options.config ?? resolveFanHostConfig();
    this.adapter = options.adapter ?? createFanApiAdapter({ enabled: this.enabled, baseUrl: this.config.baseUrl, sessionToken: this.config.sessionToken });
    this.launcher = options.launcher ?? new NativeFanHostLauncher();
    this.savedIdentity = options.savedIdentity ?? null;
    this.heartbeatIntervalMs = Math.max(0, options.heartbeatIntervalMs ?? DEFAULT_LEASE_RENEWAL_INTERVAL_MS);
    this.onDeviceIdentity = options.onDeviceIdentity;
    this.onState = options.onState;
    this.stateValue = this.enabled ? 'stopped' : 'disabled';
  }

  get state(): FanHostLifecycleState { return this.stateValue; }
  get controlReady(): boolean { return this.writeReady; }
  get currentLease(): FanLease | null { return this.lease ? { ...this.lease } : null; }
  get processId(): number | null { return this.process?.pid ?? null; }

  /** Bind lifecycle diagnostics to the native power transaction currently
   * being consumed.  Older WebView events may omit generation; those events
   * must not erase a newer value. */
  setPowerGeneration(generation: number): void {
    if (Number.isFinite(generation) && generation > this.powerGeneration) {
      this.powerGeneration = generation;
      setFanDiagnosticPowerGeneration(generation);
      fanDiagnosticLog('lifecycle.power-generation', { generation });
    }
  }

  /**
   * AC/DC is a YeMan observation signal, not a replacement for HC's profile
   * callback. If a route-loss recovery burst is already active, the physical
   * power transition refreshes that same bounded window and gives it a new
   * three-attempt budget. It never starts hardware control by itself.
   */
  notifyPowerSourceChanged(source = 'acdc', generation?: number): void {
    const now = Date.now();
    const eventGeneration = Number.isFinite(generation) && Number(generation) > 0 ? Number(generation) : 0;
    if (eventGeneration > 0 && eventGeneration === this.lastRecoveryPowerGeneration) return;
    if (eventGeneration > 0) this.lastRecoveryPowerGeneration = eventGeneration;
    if (!this.process || !this.activeCurve || this.sessionRecoveryExhausted ||
        now >= this.sessionRecoveryBurstUntil || this.sessionRecoveryAttempts >= RECOVERY_MAX_ATTEMPTS) return;
    this.sessionRecoveryBurstUntil = now + RECOVERY_WINDOW_MS;
    this.sessionRecoveryAttempts = 0;
    fanDiagnosticLog('lifecycle.recovery-window-refreshed', {
      source,
      burstUntil: this.sessionRecoveryBurstUntil,
      maxAttempts: RECOVERY_MAX_ATTEMPTS,
      generation: this.powerGeneration,
    });
    this.scheduleSessionCheck();
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

  private setState(next: FanHostLifecycleState): void {
    this.stateValue = next;
    fanDiagnosticLog('lifecycle.state', { state: next, generation: this.powerGeneration });
    this.onState?.(next);
  }

  private advanceSessionGeneration(): number {
    this.sessionGeneration += 1;
    this.stopSessionCheck();
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
    if (remote.state === 'Stopped' || remote.state === 'Suspended') {
      this.opened = false;
      this.eventsOpened = false;
    }
    if (typeof remote.openCalled === 'boolean') this.opened = remote.openCalled;
    if (typeof remote.openEventsCalled === 'boolean') this.eventsOpened = remote.openEventsCalled;
  }

  /**
   * Wait for the resident Host's own serialized recovery timer.  This method
   * deliberately performs read-only state polls; it never sends another
   * restore, close, release, or shutdown request while HC may still be inside
   * an ACPI/HID call.  That single-owner rule is the important difference from
   * the previous UI retry loops which could keep a failed Host busy forever.
   */
  private async waitForHostRecovery(
    requireStopped = false,
  ): Promise<FanState> {
    let last: FanState | null = null;
    for (let attempt = 0; attempt < RECOVERY_OBSERVATION_MAX_ATTEMPTS; attempt += 1) {
      const remote = await this.adapter.getState().catch(() => null);
      if (remote) {
        last = remote;
        this.syncRemoteSessionState(remote);
        if (isSafeHostRecoveryState(remote, requireStopped)) return remote;
      }
      if (attempt + 1 < RECOVERY_OBSERVATION_MAX_ATTEMPTS) {
        await new Promise<void>((resolve) => setTimeout(resolve, RECOVERY_OBSERVATION_INTERVAL_MS));
      }
    }
    const suffix = last ? `（最后状态：${String(last.state ?? 'unknown')}）` : '';
    throw new Error(`Fan Host 安全恢复在等待窗口内未确认${suffix}`);
  }

  private async adoptRecoveredHost(remote: FanState): Promise<void> {
    this.syncRemoteSessionState(remote);
    this.lease = null;
    this.activeCurve = null;
    this.resumeCurve = null;
    const state = String(remote.state ?? '').toLowerCase();
    if (state === 'stopped') {
      this.writeReady = false;
      // The remote Host has already completed the HC close boundary.  It is
      // now safe (and necessary) to release the resident process handle before
      // a queued mutation starts a new Host instance.
      try { await this.adapter.shutdown(); } catch { /* already stopped */ }
      await this.stopProcessOnly();
      this.setState('stopped');
    } else if (state === 'suspended') {
      this.setState('suspended');
    } else {
      this.setState('awaiting-control');
    }
  }

  private async finalizeConfirmedClose(remote: FanState): Promise<void> {
    assertHcSessionClosed(remote, 'Fan Host close recovery');
    try { await this.adapter.shutdown(); } catch { /* already stopped/legacy Host */ }
    await this.stopProcessOnly();
    this.lease = null;
    this.opened = false;
    this.eventsOpened = false;
    this.activeCurve = null;
    this.resumeCurve = null;
    this.writeReady = false;
    this.setState('stopped');
  }

  private enqueue<T>(work: () => Promise<T>): Promise<T> {
    const next = this.operation.then(work, work);
    this.operation = next.then(() => undefined, () => undefined);
    return next;
  }

  async start(): Promise<FanDeviceGateResult> {
    return this.enqueue(async () => {
      fanDiagnosticLog('lifecycle.start-begin', { state: this.state });
      let lastError: unknown;
      for (let attempt = 0; attempt < RECOVERY_RETRY_DELAYS_MS.length; attempt += 1) {
        try { return await this.startInternal(); }
        catch (error) {
          fanDiagnosticLog('lifecycle.start-failure', { state: this.state, error: error instanceof Error ? error.message : String(error) });
          lastError = error;
          if (isExternalFanControlConflict(error)) throw error;
          if (attempt + 1 < RECOVERY_RETRY_DELAYS_MS.length) await waitForRecoveryRetry(attempt + 1);
        }
      }
      throw lastError instanceof Error ? lastError : new Error('Fan Host 启动失败');
    });
  }

  private async startInternal(): Promise<FanDeviceGateResult> {
    if (!this.enabled) {
      this.setState('disabled');
      this.writeReady = false;
      return { allowed: false, writeReady: false, reason: '真实 Fan Host Gate 关闭' };
    }
    // A prior transport/restore failure must be recovered through the old
    // Host before a new process can claim the loopback endpoint. Starting a
    // second Host while the first one may still own HC is a P0 unsafe race.
    if (this.state === 'fault-locked' || this.state === 'conflict-locked' || this.state === 'unknown') {
      await this.recoverLockedHostBeforeStart();
    }
    if (this.state === 'ready' || this.state === 'awaiting-control') {
      return { allowed: true, writeReady: this.writeReady, reason: this.writeReady ? 'Fan Host 已运行' : 'Fan Host 已握手，但真实写入尚未验证' };
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
      const gate = evaluateFanDeviceGate(handshake, this.savedIdentity);
      if (!gate.allowed) {
        this.writeReady = false;
        await this.stopProcessOnly();
        this.setState('conflict-locked');
        return gate;
      }
      this.savedIdentity = handshake.deviceIdentity ?? this.savedIdentity;
      this.writeReady = gate.writeReady;
      if (handshake.deviceIdentity) this.onDeviceIdentity?.(handshake.deviceIdentity);
      // Startup performs handshake only.  HC Open/OpenEvents and lease
      // acquisition begin on the first explicit curve/preset enable.
      this.setState('awaiting-control');
      return gate;
    } catch (error) {
      await this.safeAbortAfterStart();
      this.setState(isExternalFanControlConflict(error) ? 'conflict-locked' : 'fault-locked');
      throw error;
    }
  }

  async apply(nodes: readonly FanNode[]): Promise<FanState> {
    return this.enqueue(async () => {
      fanDiagnosticLog('lifecycle.apply-begin', { state: this.state, nodeCount: nodes.length });
      let lastError: unknown;
      for (let attempt = 0; attempt < 2; attempt += 1) {
        try { return await this.applyMutation(nodes); }
        catch (error) {
          fanDiagnosticLog('lifecycle.apply-failure', { attempt, state: this.state, error: error instanceof Error ? error.message : String(error) });
          lastError = error;
          if (isExternalFanControlConflict(error)) {
            if (this.state !== 'fault-locked' && this.state !== 'unknown') this.setState('conflict-locked');
            throw error;
          }
          if (attempt === 1) throw error;
        }
      }
      throw lastError instanceof Error ? lastError : new Error('Fan 曲线应用失败');
    });
  }

  async applyPreset(name: 'soft' | 'balanced' | 'aggressive', nodes?: readonly FanNode[]): Promise<FanState> {
    return this.enqueue(async () => {
      fanDiagnosticLog('lifecycle.preset-begin', { state: this.state, preset: name });
      let lastError: unknown;
      for (let attempt = 0; attempt < 2; attempt += 1) {
        try { return await this.applyPresetMutation(name, nodes); }
        catch (error) {
          fanDiagnosticLog('lifecycle.preset-failure', { attempt, state: this.state, preset: name, error: error instanceof Error ? error.message : String(error) });
          lastError = error;
          if (isExternalFanControlConflict(error)) {
            if (this.state !== 'fault-locked' && this.state !== 'unknown') this.setState('conflict-locked');
            throw error;
          }
          if (attempt === 1) throw error;
        }
      }
      throw lastError instanceof Error ? lastError : new Error('Fan 预设应用失败');
    });
  }

  private async applyMutation(nodes: readonly FanNode[]): Promise<FanState> {
    if (this.state === 'stopped') {
      const gate = await this.startInternal();
      if (!gate.allowed) throw new Error(gate.reason);
    }
    if (this.state !== 'ready' && this.state !== 'awaiting-control') {
      throw new Error(`Fan Host 当前不可控制: ${this.state}`);
    }
    if (!this.writeReady) throw new Error('当前风扇数据路线已识别，但真实写入/恢复尚未验证');
    try {
      if (!this.opened) {
        this.advanceSessionGeneration();
        // Mark the session before the call. HC/Open can touch EC and then
        // throw; recovery must therefore attempt OEM restore even when the
        // promise rejects before the normal success assignment.
        this.opened = true;
        const opened = await this.adapter.open();
        this.syncRemoteSessionState(opened);
      }
      if (!this.eventsOpened) {
        const eventsOpened = await this.adapter.openEvents();
        this.eventsOpened = true;
        this.syncRemoteSessionState(eventsOpened);
      }
      await this.ensureLeaseForMutation();
      this.setState('ready');
      const applied = await this.adapter.enable(nodes, this.lease?.leaseId);
      // The initial acquisition begins in awaiting-control. Scheduling before
      // the first successful write silently skips the timer, allowing the
      // Host lease to expire and correctly restore OEM after its timeout.
      // Start renewal only once this control session is actually active.
      this.activeCurve = cloneFanNodes(nodes);
      this.sessionRecoveryExhausted = false;
      this.scheduleHeartbeat();
      this.scheduleSessionCheck();
      return applied;
    } catch (error) {
      try { await this.recoverAfterMutationFailure(); }
      catch (recoveryError) {
        const original = error instanceof Error ? error.message : String(error);
        const recovery = recoveryError instanceof Error ? recoveryError.message : String(recoveryError);
        throw new Error(`Fan 控制写入失败：${original}；恢复失败：${recovery}`);
      }
      throw error;
    }
  }

  private async applyPresetMutation(name: 'soft' | 'balanced' | 'aggressive', nodes?: readonly FanNode[]): Promise<FanState> {
    if (this.state === 'stopped') {
      const gate = await this.startInternal();
      if (!gate.allowed) throw new Error(gate.reason);
    }
    if (this.state !== 'ready' && this.state !== 'awaiting-control') {
      throw new Error(`Fan Host 当前不可控制: ${this.state}`);
    }
    if (!this.writeReady) throw new Error('当前风扇数据路线已识别，但真实写入/恢复尚未验证');
    try {
      if (!this.opened) {
        this.advanceSessionGeneration();
        this.opened = true;
        const opened = await this.adapter.open();
        this.syncRemoteSessionState(opened);
      }
      if (!this.eventsOpened) {
        const eventsOpened = await this.adapter.openEvents();
        this.eventsOpened = true;
        this.syncRemoteSessionState(eventsOpened);
      }
      await this.ensureLeaseForMutation();
      this.setState('ready');
      const applied = await this.adapter.applyPreset(name, this.lease?.leaseId, nodes);
      // Keep the first preset path identical to a manual curve apply: the
      // lease watchdog starts only after the initial write has succeeded.
      if (nodes) this.activeCurve = cloneFanNodes(nodes);
      this.sessionRecoveryExhausted = false;
      this.scheduleHeartbeat();
      this.scheduleSessionCheck();
      return applied;
    } catch (error) {
      try { await this.recoverAfterMutationFailure(); }
      catch (recoveryError) {
        const original = error instanceof Error ? error.message : String(error);
        const recovery = recoveryError instanceof Error ? recoveryError.message : String(recoveryError);
        throw new Error(`Fan 预设写入失败：${original}；恢复失败：${recovery}`);
      }
      throw error;
    }
  }

  /** Disable the curve, restore OEM control and release the lease. */
  async disable(): Promise<FanState> {
    return this.enqueue(async () => {
      fanDiagnosticLog('lifecycle.disable-begin', { state: this.state });
      if (this.state === 'disabled' || this.state === 'stopped' || this.state === 'suspended') {
        // A disable click is also an explicit cancellation of any deferred
        // post-sleep replay. Do not retain a curve that the user has already
        // asked to return to OEM control.
        this.activeCurve = null;
        this.resumeCurve = null;
        return { state: this.state, powerState: 'Unknown', hardwareWrites: false, hardwareWritesObserved: false };
      }
      if (this.state === 'fault-locked' || this.state === 'conflict-locked' || this.state === 'unknown') {
        // A fault/unknown state is never cleared merely because the local UI
        // has no lease flag. Close the resident Host and require its restore
        // confirmation first; otherwise a partially-open HC session could be
        // hidden by a cosmetic Disable click.
        await this.recoverLockedHostBeforeStart();
        return { state: 'Stopped', powerState: 'Unknown', hardwareWrites: false, hardwareWritesObserved: false };
      }
      this.stopHeartbeat();
      this.stopSessionCheck();
      const hadLease = this.lease !== null;
      try {
        if (this.lease || this.opened) await this.restoreAndRelease();
      } catch (error) {
        fanDiagnosticLog('lifecycle.disable-failure', { state: this.state, error: error instanceof Error ? error.message : String(error) });
        // The resident Host owns the retry/backoff.  Poll its read-only state
        // until OEM ownership is confirmed instead of sending a second
        // restore/close request from the UI thread.
        try {
          // The failed restore above already belongs to this disable request;
          // do not call restoreAndRelease a second time here.  The Host's
          // serialized timer is the only writer allowed to retry it.
          const recovered = await this.waitForHostRecovery(false);
          await this.adoptRecoveredHost(recovered);
          return recovered;
        } catch (recoveryError) {
          const original = error instanceof Error ? error.message : String(error);
          const recovery = recoveryError instanceof Error ? recoveryError.message : String(recoveryError);
          this.setState('fault-locked');
          throw new Error(`Fan 控制关闭失败：${original}；恢复失败：${recovery}`);
        }
      }
      if (hadLease) {
        // /api/release-control mirrors HC's profile handoff to Hardware. The
        // resident HC device remains open until sleep or application exit;
        // keeping the remote Open/OpenEvents flags avoids re-running
        // subscriptions on the next enable. If the Host had already closed
        // the failed session, restoreAndRelease has synchronized both flags
        // to false and the next enable will recreate the HC session.
      }
      this.activeCurve = null;
      this.resumeCurve = null;
      this.sessionRecoveryExhausted = false;
      this.setState('awaiting-control');
      return this.adapter.getState();
    });
  }

  async heartbeat(): Promise<FanLease> {
    return this.enqueue(async () => {
      if (this.state !== 'ready' || !this.lease) throw new Error('Fan lease 不可用');
      const curveBeforeRouteLoss = this.activeCurve ? cloneFanNodes(this.activeCurve) : null;
      try {
        this.lease = await this.adapter.heartbeat(this.lease.leaseId);
        fanDiagnosticLog('lifecycle.heartbeat-success', { state: this.state });
        return { ...this.lease };
      } catch (error) {
        fanDiagnosticLog('lifecycle.heartbeat-failure', { state: this.state, error: error instanceof Error ? error.message : String(error) });
        this.stopHeartbeat();
        this.stopSessionCheck();
        this.advanceSessionGeneration();
        if (curveBeforeRouteLoss && isHidRouteLoss(error)) {
          try {
            // The C# Host has already claimed and serialized the HC
            // Close/unbind boundary. Do not send restore/close from this
            // layer; wait for its terminal AwaitingControl evidence, probe a
            // stable route, and then issue exactly Open -> OpenEvents -> lease
            // -> curve through the existing mutation transaction.
            return await this.recoverAfterHidRemoval(curveBeforeRouteLoss);
          } catch (recoveryError) {
            // Keep the desired curve available for an explicit later retry,
            // but never keep a lease or a heartbeat alive after a failed
            // rebuild.
            this.lease = null;
            this.activeCurve = cloneFanNodes(curveBeforeRouteLoss);
            const recoveryMessage = recoveryError instanceof Error ? recoveryError.message : String(recoveryError);
            this.setState('fault-locked');
            throw new Error(`HC 路线丢失后自动重建失败：${recoveryMessage}`);
          }
        }
        await this.recoverAfterMutationFailure();
        if (isExternalFanControlConflict(error) && this.stateValue !== 'fault-locked' && this.stateValue !== 'unknown') {
          this.setState('conflict-locked');
        }
        throw error;
      }
    });
  }

  async suspend(): Promise<void> {
    return this.enqueue(async () => {
      fanDiagnosticLog('lifecycle.suspend-begin', { state: this.state });
      if (!this.enabled || this.state === 'disabled' || this.state === 'stopped' || this.state === 'suspended') return;
      // Sleep is a safety boundary, not a UI-only transition. A previous
      // write/transport failure can leave the local lifecycle fault-locked
      // while the resident Host still owns an HC session. Do not return early
      // for fault/conflict/unknown states; send the Host suspend request so its
      // own serialized HC recovery path gets the last OEM handoff before the
      // OS transition. The Host remains fail-closed if that request cannot be
      // confirmed.
      // A sleep notification may arrive while the HC capability probe or
      // launcher is still completing. Do not drop that boundary merely
      // because the frontend has not reached awaiting-control yet; the
      // serialized queue will let start/handshake finish first and then send
      // the same Host suspend cleanup. HC's SystemPending path is independent
      // of whether its SystemReady Open task has returned.
      if (this.state !== 'starting' && this.state !== 'handshaking' &&
          this.state !== 'ready' && this.state !== 'awaiting-control' &&
          this.state !== 'fault-locked' && this.state !== 'conflict-locked' &&
          this.state !== 'unknown') return;
      const curveToResume = this.state === 'ready' && this.activeCurve
        ? cloneFanNodes(this.activeCurve)
        : null;
      try {
        // Match HC SystemPending: the power boundary owns the one virtual
        // CurrentDevice.Close() call. Profile Hardware handoff belongs only
        // to an explicit control disable, not to suspend.
        this.stopHeartbeat();
        this.stopSessionCheck();
        this.advanceSessionGeneration();
        const suspended = await this.adapter.suspend();
        assertHcSessionSuspended(suspended, 'Fan Host suspend');
        // The Host has completed the HC Close boundary. A lease is scoped to
        // that now-closed device session and must never be reused after wake.
        this.lease = null;
        this.opened = false;
        this.eventsOpened = false;
        this.activeCurve = null;
        this.resumeCurve = curveToResume;
        this.setState('suspended');
      } catch (error) {
        fanDiagnosticLog('lifecycle.suspend-failure', { state: this.state, error: error instanceof Error ? error.message : String(error) });
        // The resident Host also listens to Windows power notifications. It
        // may have already restored OEM and cleared the lease before this UI
        // event reaches the WebView; retry the readback before fault-locking.
        for (let attempt = 0; attempt < RECOVERY_RETRY_DELAYS_MS.length; attempt += 1) {
          await waitForRecoveryRetry(attempt);
          const remote = await this.adapter.getState().catch(() => null);
          if (remote?.state === 'Suspended' || remote?.powerState === 'Suspended') {
            this.lease = null;
            this.opened = false;
            this.eventsOpened = false;
            this.setState('suspended');
            return;
          }
        }
        // The readback may itself be stale while the resident Host is still
        // recovering. Complete the normal restore/close recovery once more;
        // if that succeeds, retry the suspend endpoint before fault-locking.
        try {
          await this.recoverAfterMutationFailure();
          const suspended = await this.adapter.suspend();
          assertHcSessionSuspended(suspended, 'Fan Host suspend retry');
          this.lease = null;
          this.opened = false;
          this.eventsOpened = false;
          this.setState('suspended');
          return;
        } catch { /* retain the original suspend error and lock safely */ }
        this.setState('fault-locked');
        throw error;
      }
    });
  }

  async resume(): Promise<void> {
    return this.enqueue(async () => {
      fanDiagnosticLog('lifecycle.resume-begin', { state: this.state });
      if (!this.enabled || this.state === 'disabled' || this.state === 'stopped') return;
      if (this.state !== 'suspended') return;
      let lastError: unknown;
      let conflictLocked = false;
      for (let attempt = 0; attempt < RECOVERY_RETRY_DELAYS_MS.length; attempt += 1) {
        try {
          if (attempt > 0) await waitForRecoveryRetry(attempt);
          const resumedState = await this.adapter.resume();
          const handshake = await this.adapter.handshake();
          const gate = evaluateFanDeviceGate(handshake, this.savedIdentity);
          if (!gate.allowed) {
            conflictLocked = true;
            this.setState('conflict-locked');
            throw new Error(gate.reason);
          }
          this.savedIdentity = handshake.deviceIdentity ?? this.savedIdentity;
          this.writeReady = gate.writeReady;
          if (handshake.deviceIdentity) this.onDeviceIdentity?.(handshake.deviceIdentity);
          // The resident Host now rebuilds HC Open/OpenEvents/lease/curve on
          // its own power notification. Do not race that task by immediately
          // issuing a second UI-side enable; wait for its authoritative state
          // and adopt the curve it acknowledged when available.
          // The Host may finish its automatic HC rebuild between the native
          // resume event and this UI request. In that case /api/resume returns
          // Ready rather than Resuming. Always perform one read-only state
          // adoption check for both responses; otherwise the renderer would
          // issue a second Enable/curve write against an already-live HC
          // session, which is not an HC lifecycle transition.
          let remoteAfterResume: FanState | null = null;
          if (resumedState.state === 'Resuming') {
            for (let poll = 0; poll < RECOVERY_OBSERVATION_MAX_ATTEMPTS; poll += 1) {
              const remote = await this.adapter.getState();
              if (remote.state !== 'Resuming') {
                remoteAfterResume = remote;
                break;
              }
              if (poll + 1 < RECOVERY_OBSERVATION_MAX_ATTEMPTS) {
                await new Promise<void>((resolve) => setTimeout(resolve, RECOVERY_OBSERVATION_INTERVAL_MS));
              }
            }
            if (!remoteAfterResume) throw new Error('FAN_RESUME_OBSERVATION_TIMEOUT');
          } else if (resumedState.state === 'Ready') {
            remoteAfterResume = await this.adapter.getState().catch(() => null);
          }
          if (remoteAfterResume) {
            if (remoteAfterResume.state === 'FaultLocked' || remoteAfterResume.state === 'Unknown') {
              throw new Error('唤醒后 Fan Host 自动重建失败');
            }
            if (remoteAfterResume.state === 'Ready' && remoteAfterResume.hardwareWritesEnabled === true) {
              this.syncRemoteSessionState(remoteAfterResume);
              const remoteCurve = Array.isArray(remoteAfterResume.activeCurve)
                ? remoteAfterResume.activeCurve as FanNode[]
                : null;
              if (remoteCurve) this.activeCurve = cloneFanNodes(remoteCurve);
              // If the Host omits activeCurve from a compatible older snapshot,
              // the remembered curve still identifies the exact replay that
              // was being rebuilt; adopting it is safer than writing twice.
              if (!this.activeCurve && this.resumeCurve) this.activeCurve = cloneFanNodes(this.resumeCurve);
              if (this.activeCurve) this.resumeCurve = null;
              this.lease = null;
              this.setState('ready');
              fanDiagnosticLog('lifecycle.resume-host-auto-rebuild-adopted', {
                state: remoteAfterResume.state,
                hasCurve: Boolean(remoteCurve || this.activeCurve),
                responseState: resumedState.state,
              });
              return;
            }
          }
          // HC never retains a pre-sleep device session. Recreate the normal
          // enable boundary on a fresh handshake: Open -> OpenEvents -> lease
          // -> the same curve that was acknowledged before suspend.
          this.setState('awaiting-control');
          const curveToResume = this.resumeCurve;
          if (!curveToResume) return;
          if (!this.writeReady) throw new Error('唤醒后风扇数据路线未通过真实写入/恢复验证');
          await this.applyMutation(curveToResume);
          this.resumeCurve = null;
          return;
        } catch (error) {
          lastError = error;
          fanDiagnosticLog('lifecycle.resume-failure', {
            attempt,
            state: this.state,
            error: error instanceof Error ? error.message : String(error),
          });
          if (isExternalFanControlConflict(error)) {
            conflictLocked = true;
            this.setState('conflict-locked');
            break;
          }
        }
      }
      this.activeCurve = null;
      this.resumeCurve = null;
      if (!conflictLocked) this.setState('fault-locked');
      throw lastError instanceof Error ? lastError : new Error('唤醒后 Fan Host 恢复失败');
    });
  }

  async close(): Promise<void> {
    return this.enqueue(async () => {
      fanDiagnosticLog('lifecycle.close-begin', { state: this.state });
      if (!this.enabled || this.state === 'disabled' || this.state === 'stopped') return;
      try {
        this.stopHeartbeat();
        // Match HC Window_Closed: process exit owns one virtual Close. Do
        // not inject a separate Hardware-profile handoff before it; that
        // would add ACPI work and a second lifecycle owner to the same close.
        await this.closeHostAfterRestore();
        this.lease = null;
        this.opened = false;
        this.eventsOpened = false;
        this.activeCurve = null;
        this.resumeCurve = null;
        this.setState('stopped');
        this.writeReady = false;
        } catch (error) {
          fanDiagnosticLog('lifecycle.close-failure', { state: this.state, error: error instanceof Error ? error.message : String(error) });
          // HC Window_Closed has one CurrentDevice.Close() owner. The first
          // HTTP request may have reached the resident Host even if its
          // response was lost, so failure recovery is observer-only. It must
          // never post another Close into the same HC device session.
          try {
            const recovered = await this.waitForHostRecovery(true);
            await this.finalizeConfirmedClose(recovered);
            return;
          } catch (recoveryError) {
            // Never terminate a Host while OEM/HC cleanup is unconfirmed; it
            // remains resident so the next explicit close can retry safely.
            this.setState('unknown');
            throw recoveryError instanceof Error ? recoveryError : error;
          }
      }
    });
  }

  /** Close/confirm first, request shutdown second, force-stop only as a
   * compatibility fallback after restore has already been confirmed. */
  private async closeHostAfterRestore(): Promise<void> {
    if (!this.process) {
      if (this.opened || this.lease) throw new Error('Fan Host 进程已丢失，硬件状态无法确认');
      return;
    }
    const closed = await this.adapter.close();
    this.syncRemoteSessionState(closed);
    assertHcSessionClosed(closed, 'Fan Host close');
    if (!isSafeHostRecoveryState(closed, true)) {
      throw new Error(`Fan Host close：远程状态未进入安全停止边界（${String(closed.state ?? 'unknown')}）`);
    }
    try { await this.adapter.shutdown(); } catch { /* legacy Host: safe fallback below */ }
    await this.stopProcessOnly();
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

  private async recoverLockedHostBeforeStart(): Promise<void> {
    if (!this.process) {
      if (this.opened || this.lease) throw new Error('Fan Host 进程已丢失，硬件状态无法确认');
      // No process and no local ownership means the lock is only a stale UI
      // state. It is safe to clear it and perform a fresh handshake.
      this.setState('stopped');
      return;
    }
    // HC has one Window_Closed/CurrentDevice.Close owner.  A transport
    // timeout or a 409 is ambiguous: the resident Host may already be inside
    // its serialized ACPI/HID close gate.  Retrying /api/close here used to
    // create a second virtual Close and was the source of the ROG stuck/OEM
    // handoff loop.  Send one request, then observe the Host's own recovery
    // worker only.
    try {
      await this.closeHostAfterRestore();
      this.lease = null;
      this.opened = false;
      this.eventsOpened = false;
      this.setState('stopped');
      return;
    } catch (error) {
      fanDiagnosticLog('lifecycle.locked-close-failure-observe-only', {
        state: this.state,
        error: error instanceof Error ? error.message : String(error),
      });
      try {
        const recovered = await this.waitForHostRecovery(true);
        await this.finalizeConfirmedClose(recovered);
        return;
      } catch (recoveryError) {
        throw recoveryError instanceof Error ? recoveryError : error;
      }
    }
  }

  /** Mutation/heartbeat failure path: restore and close automatically. */
  private async recoverAfterMutationFailure(): Promise<void> {
    this.stopHeartbeat();
    // Preferred path: release the current lease after OEM restore but keep
    // the resident Host alive in AwaitingControl so the next user action can
    // reopen/reacquire without a second process race.
    try {
      await this.restoreAndRelease();
      // Restore/release returns to HC Hardware mode while retaining the
      // resident device session, just like HC's profile switch. If the Host
      // closed that session while handling the original failure,
      // syncRemoteSessionState keeps both flags false so the next mutation
      // performs a fresh Open/OpenEvents pair.
      this.setState('awaiting-control');
      return;
    } catch {
      // The real Host has already scheduled its serialized 1/2/4/8/16 sec
      // close recovery. Do not send a second restore or an eager close here.
    }
    try {
      const recovered = await this.waitForHostRecovery(false);
      await this.adoptRecoveredHost(recovered);
    } catch (error) {
      // Leave the process/lease resident so the Host's own watchdog and a
      // later explicit close attempt still have a live recovery endpoint.
      this.setState('fault-locked');
      throw error instanceof Error ? error : new Error('Fan Host 自动恢复未确认，已保持故障锁定');
    }
  }

  /**
   * Rebuild one control session after the resident Host has closed a lost HID
   * route. All calls remain inside the lifecycle queue; no second Close or
   * profile restore is sent while HC's original owner is unwinding.
   */
  private async recoverAfterHidRemoval(curve: readonly FanNode[]): Promise<FanLease> {
    if (this.recoveryOwner !== 'none') throw new Error('HC_RECOVERY_OWNER_BUSY');
    this.recoveryOwner = 'hid-route';
    try {
      const remote = await this.waitForHostRecovery(false);
      this.syncRemoteSessionState(remote);
      if (String(remote.state ?? '').toLowerCase() !== 'awaitingcontrol') {
        throw new Error(`HC_ROUTE_RECOVERY_NOT_CLOSED: ${String(remote.state ?? 'unknown')}`);
      }

      // Handshake is read-only route admission. Do not call Open until the
      // route identity and explicit write/recovery gate are stable again.
      let handshake: FanHandshake | null = null;
      let gate: FanDeviceGateResult | null = null;
      let lastError: unknown;
      for (let attempt = 0; attempt < RECOVERY_RETRY_DELAYS_MS.length; attempt += 1) {
        try {
          if (attempt > 0) await waitForRecoveryRetry(attempt);
          handshake = await this.adapter.handshake();
          gate = evaluateFanDeviceGate(handshake, this.savedIdentity);
          if (gate.allowed && gate.writeReady) break;
          throw new Error(gate.reason);
        } catch (error) {
          lastError = error;
          handshake = null;
          gate = null;
        }
      }
      if (!handshake || !gate?.allowed || !gate.writeReady) {
        throw lastError instanceof Error ? lastError : new Error('HC_ROUTE_NOT_STABLE');
      }
      this.savedIdentity = handshake.deviceIdentity ?? this.savedIdentity;
      this.writeReady = gate.writeReady;

      // A completed HC Close invalidates the old Open/OpenEvents and lease;
      // never trust a stale local flag when a compatible adapter omitted the
      // optional telemetry fields.
      this.opened = remote.openCalled === true;
      this.eventsOpened = remote.openEventsCalled === true;
      this.lease = null;
      this.activeCurve = cloneFanNodes(curve);
      this.setState('awaiting-control');
      await this.applyMutation(curve);
      if (!this.lease) throw new Error('HC_ROUTE_REBUILD_LEASE_MISSING');
      return { ...this.lease };
    } finally {
      this.recoveryOwner = 'none';
    }
  }

  private isObservedRouteLoss(remote: FanState): boolean {
    const remoteState = String(remote.state ?? '').toLowerCase();
    // A confirmed sleep/process boundary is not an external takeover. The
    // existing SystemPending/SystemReady owner handles those states and must
    // not be raced by the route observer.
    if (remoteState === 'suspended' || remoteState === 'stopped') return false;
    if (remoteState === 'awaitingcontrol' && remote.hardwareWritesEnabled !== true) return true;
    if (remote.openCalled === false || remote.openEventsCalled === false) return true;
    return remoteState === 'faultlocked' || remoteState === 'unknown';
  }

  private async restoreAndRelease(): Promise<void> {
    if (this.lease) {
      try {
        const restored = await this.adapter.restoreOem(this.lease.leaseId);
        assertOemRestoreConfirmed(restored, 'Fan Host restore');
        this.syncRemoteSessionState(restored);
        const released = await this.adapter.releaseControl(this.lease.leaseId);
        assertOemRestoreConfirmed(released, 'Fan Host release');
        this.syncRemoteSessionState(released);
        this.lease = null;
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

  private async safeAbortAfterStart(): Promise<void> {
    this.stopHeartbeat();
    let closeConfirmed = !this.opened && !this.eventsOpened && !this.lease;
    let finalizedByObserver = false;
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
      // lost or incomplete.  Observe only; never issue a second Close.
      try {
        const recovered = await this.waitForHostRecovery(true);
        await this.finalizeConfirmedClose(recovered);
        closeConfirmed = true;
        finalizedByObserver = true;
      } catch {
        closeConfirmed = false;
      }
    }
    if (closeConfirmed && !finalizedByObserver) {
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
  }

  private scheduleHeartbeat(): void {
    this.stopHeartbeat();
    if (this.heartbeatIntervalMs <= 0 || !this.lease || this.state !== 'ready') return;
    this.heartbeatTimer = setTimeout(() => {
      this.heartbeatTimer = null;
      if (this.state !== 'ready' || !this.lease) return;
      void this.heartbeat()
        .catch(() => {})
        .finally(() => this.scheduleHeartbeat());
    }, this.heartbeatIntervalMs);
  }

  /**
   * A mutation is a lease boundary. Refresh an existing lease before sending
   * the curve/preset so a stale timer cannot make the write request fail. If
   * the Host already expired it, acquire a new lease and let the Host's own
   * ownership checks decide whether reacquisition is safe.
   */
  private async ensureLeaseForMutation(): Promise<void> {
    if (!this.lease) {
      this.lease = await this.adapter.acquireControl();
      return;
    }
    try {
      this.lease = await this.adapter.heartbeat(this.lease.leaseId);
    } catch (error) {
      await this.recoverAfterMutationFailure();
      if (isExternalFanControlConflict(error) && this.state !== 'fault-locked' && this.state !== 'unknown') {
        this.setState('conflict-locked');
        throw error;
      }
      if (this.state === 'stopped') {
        const gate = await this.startInternal();
        if (!gate.allowed) throw new Error(gate.reason);
      }
      this.lease = await this.adapter.acquireControl();
    }
  }

  private stopHeartbeat(): void {
    if (this.heartbeatTimer !== null) {
      clearTimeout(this.heartbeatTimer);
      this.heartbeatTimer = null;
    }
  }

  /** Schedule the normal read-only session observer, never the lease renewer. */
  private scheduleSessionCheck(): void {
    this.stopSessionCheck();
    const burstActive = Date.now() < this.sessionRecoveryBurstUntil;
    const recoveryAwaitingControl = this.state === 'awaiting-control';
    if (!this.process || (this.state !== 'ready' && !recoveryAwaitingControl) ||
        (this.state === 'ready' && !this.lease)) return;
    const generation = this.sessionGeneration;
    const intervalMs = burstActive ? RECOVERY_OBSERVATION_INTERVAL_MS : NORMAL_SESSION_CHECK_INTERVAL_MS;
    this.sessionCheckTimer = setTimeout(() => {
      this.sessionCheckTimer = null;
      if (this.sessionCheckInFlight || generation !== this.sessionGeneration ||
          (this.state !== 'ready' && !recoveryAwaitingControl) ||
          (this.state === 'ready' && !this.lease)) return;
      this.sessionCheckInFlight = true;
      void this.enqueue(async () => {
        const retryAwaitingControl = this.state === 'awaiting-control';
        if (generation !== this.sessionGeneration ||
            (this.state !== 'ready' && !retryAwaitingControl) ||
            (this.state === 'ready' && !this.lease)) return;
        try {
          const remote = await this.adapter.getState();
          if (generation !== this.sessionGeneration) return;
          this.syncRemoteSessionState(remote);
          const remoteState = String(remote.state ?? '').toLowerCase();
          const sessionClosed = remote.openCalled === false || remote.openEventsCalled === false;
          const curveBeforeRouteLoss = this.activeCurve ? cloneFanNodes(this.activeCurve) : null;
          const routeLossObserved = Boolean(curveBeforeRouteLoss && this.isObservedRouteLoss(remote));
          const burstStillActive = Date.now() < this.sessionRecoveryBurstUntil;
          if (!routeLossObserved && this.sessionRecoveryExhausted) {
            this.sessionRecoveryExhausted = false;
            this.sessionRecoveryAttempts = 0;
            fanDiagnosticLog('lifecycle.recovery-cycle-rearmed', { generation, remoteState });
          }
          if (routeLossObserved && !this.sessionRecoveryExhausted && !this.sessionRecoveryInFlight && !this.recoveryOwnerIsBusy() &&
              (!burstStillActive || this.sessionRecoveryAttempts < RECOVERY_MAX_ATTEMPTS)) {
            if (!burstStillActive) {
              this.sessionRecoveryBurstUntil = Date.now() + RECOVERY_WINDOW_MS;
              this.sessionRecoveryAttempts = 0;
            }
            this.sessionRecoveryAttempts += 1;
            const recoveryAttempt = this.sessionRecoveryAttempts;
            this.sessionRecoveryInFlight = true;
            this.stopHeartbeat();
            fanDiagnosticLog('lifecycle.external-route-loss-observed', {
              generation,
              remoteState,
              openCalled: remote.openCalled,
              openEventsCalled: remote.openEventsCalled,
              hardwareWritesEnabled: remote.hardwareWritesEnabled,
              burstUntil: this.sessionRecoveryBurstUntil,
              attempt: recoveryAttempt,
              maxAttempts: RECOVERY_MAX_ATTEMPTS,
            });
            try {
              await this.recoverAfterHidRemoval(curveBeforeRouteLoss);
              this.sessionRecoveryBurstUntil = 0;
              this.sessionRecoveryAttempts = 0;
              this.sessionRecoveryExhausted = false;
              this.scheduleSessionCheck();
            } catch (error) {
              fanDiagnosticLog('lifecycle.external-route-recovery-failure', {
                generation,
                error: error instanceof Error ? error.message : String(error),
                burstRemainingMs: Math.max(0, this.sessionRecoveryBurstUntil - Date.now()),
                attempt: recoveryAttempt,
                maxAttempts: RECOVERY_MAX_ATTEMPTS,
              });
              if (recoveryAttempt < RECOVERY_MAX_ATTEMPTS && Date.now() < this.sessionRecoveryBurstUntil) {
                this.setState('awaiting-control');
                this.scheduleSessionCheck();
              } else {
                // Three recovery triggers complete this burst. Return to the
                // sparse observer; a later, distinct route loss may start a
                // new burst, but this cycle must not keep writing at 2s.
                this.sessionRecoveryBurstUntil = 0;
                this.sessionRecoveryAttempts = 0;
                this.sessionRecoveryExhausted = true;
                this.setState('awaiting-control');
                this.scheduleSessionCheck();
              }
            } finally {
              this.sessionRecoveryInFlight = false;
            }
            return;
          }
          if (routeLossObserved && burstStillActive && this.sessionRecoveryAttempts >= RECOVERY_MAX_ATTEMPTS) {
            this.sessionRecoveryBurstUntil = 0;
            this.sessionRecoveryAttempts = 0;
            this.sessionRecoveryExhausted = true;
            this.setState('awaiting-control');
            this.scheduleSessionCheck();
            return;
          }
          if (remoteState === 'faultlocked' || remoteState === 'unknown' ||
              remote.hcCloseCleanupPending === true || sessionClosed) {
            this.stopHeartbeat();
            this.stopSessionCheck();
            this.setState('fault-locked');
            return;
          }
          this.scheduleSessionCheck();
        } catch (error) {
          if (generation !== this.sessionGeneration) return;
          this.stopHeartbeat();
          this.stopSessionCheck();
          this.setState('fault-locked');
          fanDiagnosticLog('lifecycle.session-check-failure', {
            generation,
            error: error instanceof Error ? error.message : String(error),
          });
        } finally {
          this.sessionCheckInFlight = false;
        }
      }).catch(() => {
        this.sessionCheckInFlight = false;
      });
    }, intervalMs);
  }

  private recoveryOwnerIsBusy(): boolean {
    return this.recoveryOwner !== 'none';
  }

  private stopSessionCheck(): void {
    if (this.sessionCheckTimer !== null) {
      clearTimeout(this.sessionCheckTimer);
      this.sessionCheckTimer = null;
    }
  }
}

/** Product singleton. Importing this module has no native side effects. */
export const fanHostLifecycle = new FanHostLifecycle({ enabled: FAN_REAL_HOST_ENABLED });

export function getFanHostLifecycle(): FanHostLifecycle {
  return fanHostLifecycle;
}
