import { ref } from 'vue';
import { invoke, isNativeRuntime } from './ipc';

/** Fan-only diagnostics. Disabled by default and isolated from app.log. */
export const fanDiagnosticLoggingEnabled = ref(false);
export const fanDiagnosticLogPath = ref<string | null>(null);
let activePowerGeneration = 0;

/** Current native power generation used to correlate fan API/lifecycle logs. */
export function setFanDiagnosticPowerGeneration(generation: number): void {
  if (Number.isFinite(generation) && generation > activePowerGeneration) activePowerGeneration = generation;
}

export function getFanDiagnosticPowerGeneration(): number { return activePowerGeneration; }

/** Read the current native log path without changing the logging switch. */
export async function getFanDiagnosticLogPath(): Promise<string | null> {
  if (!isNativeRuntime) return fanDiagnosticLogPath.value;
  try {
    const path = await invoke<string | null>('fanLog.getPath');
    fanDiagnosticLogPath.value = typeof path === 'string' && path.trim() ? path : null;
  } catch {
    // Older shells may not expose the optional path query. Logging itself
    // remains independent and the UI can show its documented fallback path.
  }
  return fanDiagnosticLogPath.value;
}

export async function clearFanDiagnosticLogs(): Promise<boolean> {
  if (!isNativeRuntime) return false;
  const result = await invoke<{ ok?: boolean }>('fanLog.clear');
  return result.ok === true;
}

export interface FanDiagnosticExportResult {
  ok: boolean;
  path?: string;
  files?: string[];
  reason?: string;
}

export async function exportFanDiagnosticLogs(): Promise<FanDiagnosticExportResult> {
  if (!isNativeRuntime) return { ok: false, reason: '当前为预览环境' };
  return invoke<FanDiagnosticExportResult>('fanLog.export');
}

/**
 * 允许保留的**非秘密**键（裁决 §7：只允许令牌哈希前缀与路径身份；绝不输出令牌 / 令牌前缀 /
 * 密钥材料）。每个键都按**形状**二次校验，形状不符一律 `[redacted]`。
 *
 * 为什么需要它（b142 实机取证）：`sanitize()` 原来的键名正则含 `token|session`，于是把 CP-05
 * 特意设计的**非秘密**字段也一起抹掉了 —— 2026-09-25 19:27 实机行是
 * `credential-source{tokenFingerprint:[redacted],sessionTokenPath:[redacted]}`，
 * 等于把该事件的证据价值清零（无法与 native/Host 侧的版本标识对账）。
 */
// Reuse fixed non-global grammar, never a previous field value or decision.
const FAN_DIAGNOSTIC_FINGERPRINT_PATTERN = /^([0-9a-f]{8})?$/;
const FAN_DIAGNOSTIC_VERSION_PATTERN = /^([0-9a-f]{8}|absent)$/;
const FAN_DIAGNOSTIC_PATH_HEX_PATTERN = /[0-9a-f]{32}/i;
const FAN_DIAGNOSTIC_SECRET_KEY_PATTERN = /(token|lease.?id|authorization|session|password|secret)/i;

function sanitizeNonSecretValue(key: string, value: unknown): unknown | undefined {
  // 布尔型非秘密字段（"这场会话里到底有没有令牌"）——被正则吃掉会与旁边的真指纹并列成
  // `tokenPresent:"[redacted]"`，看起来像把秘密值藏了，实际只是丢了一个可判读的事实位。
  if (key === 'tokenPresent') return typeof value === 'boolean' ? value : '[redacted]';
  if (typeof value !== 'string') return undefined;
  if (key === 'tokenFingerprint') return FAN_DIAGNOSTIC_FINGERPRINT_PATTERN.test(value) ? value : '[redacted]';
  if (key === 'credentialVersionId') return FAN_DIAGNOSTIC_VERSION_PATTERN.test(value) ? value : '[redacted]';
  if (key === 'sessionTokenPath') return FAN_DIAGNOSTIC_PATH_HEX_PATTERN.test(value) ? '[redacted]' : value;
  return undefined;   // 其它键不在此白名单内
}

function sanitize(value: unknown, key = ''): unknown {
  if (key) {
    const allowed = sanitizeNonSecretValue(key, value);
    if (allowed !== undefined) return allowed;
  }
  if (FAN_DIAGNOSTIC_SECRET_KEY_PATTERN.test(key)) return '[redacted]';
  if (value === null || value === undefined) return value;
  if (typeof value === 'string') {
    return value.replace(/(X-YeMan-Fan-Session|sessionToken|leaseId)\s*[:=]\s*[^,\s}]+/gi, '$1=[redacted]');
  }
  if (Array.isArray(value)) return value.map((item) => sanitize(item, key));
  if (typeof value === 'object') {
    // Capture getters before recursion; keep safe data-property construction.
    const entries = Object.entries(value as Record<string, unknown>);
    for (const entry of entries) entry[1] = sanitize(entry[1], entry[0]);
    return Object.fromEntries(entries);
  }
  return value;
}

export async function configureFanDiagnosticLogging(enabled: boolean): Promise<string | null> {
  if (isNativeRuntime) {
    try {
      const result = await invoke<{ enabled: boolean; path?: string | null }>('fanLog.setEnabled', { enabled });
      if (result.enabled !== enabled) throw new Error('风扇诊断日志开关未能应用');
      fanDiagnosticLogPath.value = result.path ?? null;
    } catch (error) {
      // An older native shell may not know the optional diagnostics command.
      // Keeping logging disabled remains safe and must not hide the Fan page.
      if (enabled) throw error;
      fanDiagnosticLogPath.value = null;
    }
  }
  fanDiagnosticLoggingEnabled.value = enabled;
  return fanDiagnosticLogPath.value;
}

/** 只读统一详细日志门（P1a / CP-09）：**不发** `enabled` 键 ⇒ native 侧零副作用读回权威值。
 *  旧壳可能不认识该命令 ⇒ 返回 null（调用方必须保留原值，不得回落写默认）。 */
export async function readFanDiagnosticLogging(): Promise<boolean | null> {
  if (!isNativeRuntime) return null;
  try {
    // Optional logging metadata must not hold Fan initialization indefinitely.
    // This is a read only: unknown preserves saved settings and never writes a default.
    const result = await invoke<{ enabled: boolean }>('fanLog.setEnabled', {}, { timeoutMs: 1000 });
    return result.enabled === true;
  } catch {
    return null;
  }
}

/**
 * 把**已经读回**的权威值采纳为桥内日志门（b142）。
 *
 * 为什么必须单独有这一步：`fanDiagnosticLog()` 的闸门是本模块的 `fanDiagnosticLoggingEnabled`，
 * 而只有 `configureFanDiagnosticLogging()`（= 用户动作 / 写路径）会设置它。b141 的 CP-10 把初始化
 * 改成"读回并采纳"之后，初始化只更新了 `settings.value`、**没有**同步这个 ref ⇒ 即使用户已经把
 * 统一详细门打开，整场会话里所有 `lifecycle.*` 行（凭据来源 / 采纳被拒 / 拒绝轮换 / 中止路径，
 * 以及既有的 state / power-generation / handshake-retry 行）都会被**静默丢弃**。
 * 2026-09-25 19:03 实机取证：门已打开（native 写了 `logging-enabled`），而 `fan-api.log` 里
 * **零** `lifecycle.*` 行 —— 正是此形态；同一会话里宿主确实被真实启动（宿主 runtime 日志
 * `host.starting{realBackend:true}` @19:03:31），桥的对应诊断却全部丢失。
 *
 * 语义：**只采纳、不写**（不调用 native、不落设置）——与"初始化只读实际权威、只有用户明确
 * 操作才写"的纪律一致。
 */
export function adoptFanDiagnosticLoggingState(authoritative: boolean): void {
  fanDiagnosticLoggingEnabled.value = authoritative === true;
}

// Retire only the diagnostic IPC observer when native omits an acknowledgement.
// The write may still finish: never replay it or change any fan control deadline.
const FAN_DIAGNOSTIC_IPC_OPTIONS = { timeoutMs: 5000 } as const;

export function fanDiagnosticLog(event: string, details?: unknown): void {
  if (!fanDiagnosticLoggingEnabled.value || !event || !isNativeRuntime) return;
  try {
    void invoke('fanLog.write', { event, details: sanitize(details ?? {}) }, FAN_DIAGNOSTIC_IPC_OPTIONS).catch(() => {});
  } catch { /* Diagnostic projection failures cannot change control results. */ }
}

/**
 * Hot-path diagnostic entry: evaluate the detail object only when the local
 * diagnostic gate is enabled. This preserves the ordinary fire-and-forget
 * semantics while avoiding a short-lived allocation on steady heartbeat ticks.
 */
export function fanDiagnosticLogLazy(event: string, detailsFactory: () => unknown): void {
  if (!fanDiagnosticLoggingEnabled.value || !event || !isNativeRuntime) return;
  try {
    void invoke('fanLog.write', { event, details: sanitize(detailsFactory() ?? {}) }, FAN_DIAGNOSTIC_IPC_OPTIONS).catch(() => {});
  } catch { /* Keep lazy factories and sanitization outside the control contract. */ }
}

/**
 * G7（执行单 §3.4）：有限责任事件的**固定 caller 类别**（非秘密常量）。
 * 现场缺少足够关联字段时，这一位至少能回答"这条责任事件是 Fan 桥投递的"。
 */
const FAN_LIFECYCLE_EVIDENCE_CALLER = 'fanHost-lifecycle';

/**
 * R2（FAN-926R 唤醒租约与有界等待裁决 §4.4）：**有限责任证据**入口——**不**受详细日志
 * 开关影响（普通 `fanDiagnosticLog` 仍受开关，关闭时整条责任链会消失）。
 *
 * 纪律：
 * - 只允许白名单事件（native 侧同样按事件名严格白名单校验，非法名一律丢弃）；
 * - 每个恢复等待任务只发 `started` + 唯一 `completed/cancelled/terminal-failed`，
 *   含非秘密的 operation/instance/generation、耗时/探测次数/错误码，**不逐次轮询落盘**；
 * - 只含非秘密字段（复用 `sanitize`，lease token / session / 凭据一律 `[redacted]`）；
 * - fire-and-forget：投递失败绝不影响调用方（睡眠/退出路径不得被日志阻塞）。
 */
export function fanLifecycleEvidence(event: string, details?: unknown): void {
  if (!event || !isNativeRuntime) return;
  // G7（FAN-926R 执行单 §3.4）：有限责任事件必须带**固定 caller 类别**（非秘密），
  // 使"谁投递了这条责任事件"在缺失关联字段的现场也能判读，不再只靠事件名猜来路。
  try {
    const base = details && typeof details === 'object' && !Array.isArray(details)
      ? { caller: FAN_LIFECYCLE_EVIDENCE_CALLER, ...(details as Record<string, unknown>) }
      : { caller: FAN_LIFECYCLE_EVIDENCE_CALLER };
    void invoke('fanLog.write', { event, details: sanitize(base) }, FAN_DIAGNOSTIC_IPC_OPTIONS).catch(() => {});
  } catch { /* Failed evidence projection must not block sleep, exit or manual ON. */ }
}
