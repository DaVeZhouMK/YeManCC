/** [LIFECYCLE:FAN:STARTUP-CONFIG] 启动路径、协议和兼容依赖元数据的唯一前端定义。
 * 实际运行库由载荷清单解析；此表不替代能力总库，也不在每次开启时逐库校验。
 * 修改路径/协议时同步原public export、部署合同与既有启动验证；维护导航见本目录README.md。
 */
import type { AiFanMockSession } from '../aiFanMock';

export const FAN_HOST_PROTOCOL_VERSION = 2;

export const FAN_HOST_DEFAULT_PORT = 8765;


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


export function joinWindowsPath(root: string, child: string): string {
  return `${trimWindowsPath(root)}\\${child}`;
}


export function parseJson<T>(raw: string): T {
  // PowerShell UTF-8 output may include a BOM; JSON.parse rejects it even
  // though Windows/.NET JSON readers accept the same file.
  return JSON.parse(raw.replace(/^\uFEFF/, '')) as T;
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


export type FanHostPayloadManifest = {
  schemaVersion?: unknown;
  runtimeManifest?: unknown;
  /** FAN-927 §2：部署代描述的一部分（与 runtime manifest 的 runtimeId 必须一致）。 */
  runtimeId?: unknown;
  files?: unknown;
};
