// Read-only USBIP presentation. Native owns installation and virtual output.
export const USBIP_UPGRADE_READ_TIMEOUT_MS = 1500;
export const USBIP_UPGRADE_PROGRESS_TIMEOUT_MS = 180_000;
export const USBIP_UPGRADE_PHASES = [
  'checking', 'current', 'newer-preserved', 'not-installed', 'update-found',
  'removing', 'installing', 'verifying', 'complete', 'admin-required', 'busy',
  'reboot-required', 'failed', 'unverified',
] as const;
export type UsbipUpgradePhase = typeof USBIP_UPGRADE_PHASES[number];
export interface UsbipUpgradeStatus {
  phase: UsbipUpgradePhase;
  installedVersion: string;
  targetVersion: string;
  detail: string;
  sequence: number | null;
  driverSetupSession: string;
  /** A fresh inventory observation is not a repeated upgrade attempt. */
  observationOnly?: true;
}
export function unverifiedUsbipUpgradeStatus(detail = '正在读取 USBIP 配套版本状态。'): UsbipUpgradeStatus {
  return { phase: 'unverified', installedVersion: '', targetVersion: '', detail,
    sequence: null, driverSetupSession: '' };
}
const record = (value: unknown): value is Record<string, unknown> =>
  value !== null && typeof value === 'object' && !Array.isArray(value);
const text = (value: unknown, limit: number): value is string => typeof value === 'string' && value.length <= limit;
/** Invalid data never inherits a successful phase or displays untrusted details. */
export function parseUsbipUpgradeStatus(value: unknown): { valid: boolean; status: UsbipUpgradeStatus } {
  const envelope = record(value) ? value : {};
  const session = text(envelope.driverSetupSession, 256) ? envelope.driverSetupSession.trim() : '';
  const upgrade = record(envelope.usbipUpgrade) ? envelope.usbipUpgrade : {};
  const sequence = typeof upgrade.sequence === 'number' && Number.isSafeInteger(upgrade.sequence) && upgrade.sequence >= 0
    ? upgrade.sequence : null;
  const valid = !!session && upgrade.schemaVersion === 1 && sequence !== null &&
    USBIP_UPGRADE_PHASES.includes(upgrade.phase as UsbipUpgradePhase) &&
    text(upgrade.installedVersion, 128) && text(upgrade.targetVersion, 128) && text(upgrade.detail, 4096) &&
    (!['current', 'newer-preserved', 'complete'].includes(upgrade.phase as string) ||
      (!!(upgrade.installedVersion as string).trim() && !!(upgrade.targetVersion as string).trim()));
  if (!valid) {
    let detail = 'USBIP 版本状态尚未确认，请重新显示窗口确认；若仍未确认，请导出日志。';
    // Older native builds may only provide this advisory field in the driverSetup snapshot.
    if (!('usbipUpgrade' in envelope) && envelope.backendInstallPrompt === 1)
      detail = '旧版后端报告 USBIP 安装器缺失，配套版本待确认。请确认 YMCC 安装包完整后重新打开 YMCC。';
    if (!('usbipUpgrade' in envelope) && envelope.backendInstallPrompt === 2)
      detail = '旧版后端报告 USBIP 安装失败，配套版本待确认。请关闭相关 USBIP 软件，再以管理员身份重新打开 YMCC 重试。';
    return { valid: false, status: { ...unverifiedUsbipUpgradeStatus(detail), driverSetupSession: session, sequence } };
  }
  return { valid: true, status: {
    phase: upgrade.phase as UsbipUpgradePhase,
    installedVersion: (upgrade.installedVersion as string).trim(),
    targetVersion: (upgrade.targetVersion as string).trim(), detail: (upgrade.detail as string).trim(),
    sequence, driverSetupSession: session,
    ...(upgrade.observationOnly === true ? { observationOnly: true as const } : {}),
  } };
}
export interface UsbipUpgradePresentation {
  title: string;
  description: string;
  tone: 'quiet' | 'progress' | 'success' | 'warning';
}
const presentations: Record<UsbipUpgradePhase, UsbipUpgradePresentation> = {
  checking: { title: '正在检查 USBIP 配套版本', description: '检查在后台进行，可以继续使用页面。', tone: 'progress' },
  current: { title: 'USBIP 配套版本已匹配', description: '当前配套版本无需升级。', tone: 'quiet' },
  'newer-preserved': { title: '已保留较新的 USBIP 版本', description: '检测到已安装版本较新，保留现有版本，不降级。', tone: 'quiet' },
  'not-installed': { title: 'USBIP 尚未安装', description: '启用需要 USBIP 的虚拟手柄时，请留意后续配套准备状态。', tone: 'quiet' },
  'update-found': { title: '发现 USBIP 配套更新', description: '后端已发现版本差异，请留意后续升级状态。', tone: 'progress' },
  removing: { title: '正在移除旧版 USBIP', description: '配套升级在后台进行，可以继续使用页面。', tone: 'progress' },
  installing: { title: '正在安装 USBIP 配套', description: '配套安装在后台进行，可以继续使用页面。', tone: 'progress' },
  verifying: { title: '正在核验 USBIP 配套版本', description: '正在核验实际安装结果，请等待版本确认。', tone: 'progress' },
  complete: { title: 'USBIP 配套升级完成', description: '仅表示 USBIP 配套升级完成，不代表虚拟手柄已经启动。请查看虚拟手柄的实际输出状态。', tone: 'success' },
  'admin-required': { title: 'USBIP 升级需要管理员权限', description: '请关闭 YMCC，然后以管理员身份重新打开 YMCC 后重试。', tone: 'warning' },
  busy: { title: 'USBIP 配套正在被占用', description: '请关闭相关 USBIP 软件，然后重新打开 YMCC 后重试。', tone: 'warning' },
  'reboot-required': { title: 'USBIP 配套需要重启 Windows', description: '请重启 Windows 后，再打开 YMCC 重试。', tone: 'warning' },
  failed: { title: 'USBIP 配套升级失败', description: '请关闭相关 USBIP 软件，再以管理员身份重新打开 YMCC 重试；若仍失败，请导出日志。', tone: 'warning' },
  unverified: { title: 'USBIP 状态待确认', description: '尚未确认 USBIP 配套版本，请留意后续状态；仍未确认时请导出日志。', tone: 'warning' },
};
export function usbipUpgradePresentation(status: UsbipUpgradeStatus): UsbipUpgradePresentation {
  if (status.observationOnly && status.phase === 'update-found') return {
    title: 'USBIP 配套版本可更新',
    description: '已保留当前安装，未自动重试升级；版本偏低不代表手柄不可用。', tone: 'quiet',
  };
  return presentations[status.phase] ?? presentations.unverified;
}
/** Inline controller pages show confirmed failures only; version maintenance lives in Settings. */
export function usbipUpgradeHasError(status: UsbipUpgradeStatus): boolean {
  return ['failed', 'admin-required', 'reboot-required'].includes(status.phase);
}
export interface UsbipUpgradeStatusPorts {
  read(signal: AbortSignal): Promise<unknown>;
  subscribe(handler: (value: unknown) => void): () => void;
  isVisible(): boolean;
  watchVisibility(handler: (visible: boolean) => void): () => void;
  watchRefresh?(handler: () => void): () => void;
  changed(status: UsbipUpgradeStatus): void;
  /** One-shot presentation deadline, independent of native preparation. */
  progressTimeoutMs?: number;
  now?: () => number;
  setTimer?: (callback: () => void, ms: number) => ReturnType<typeof setTimeout>;
  clearTimer?: (timer: ReturnType<typeof setTimeout>) => void;
}
/** One subscription, bounded reads on visibility edges and one progress deadline. No polling. */
export function createUsbipUpgradeStatusController(ports: UsbipUpgradeStatusPorts) {
  const setTimer = ports.setTimer ?? ((callback, ms) => setTimeout(callback, ms));
  const clearTimer = ports.clearTimer ?? (timer => clearTimeout(timer));
  const now = ports.now ?? (() => performance.now());
  const progressTimeoutMs = typeof ports.progressTimeoutMs === 'number' &&
    Number.isFinite(ports.progressTimeoutMs) && ports.progressTimeoutMs > 0
    ? ports.progressTimeoutMs : USBIP_UPGRADE_PROGRESS_TIMEOUT_MS;
  let status = unverifiedUsbipUpgradeStatus();
  let active = false, epoch = 0, eventRevision = 0, reread = false, refreshQueued = false;
  let stopEvents: (() => void) | undefined, stopVisibility: (() => void) | undefined, stopRefresh: (() => void) | undefined;
  let session = '', sequence = -1, fingerprint = '', tainted = false;
  const retiredSessions = new Set<string>();
  type Read = { abort: AbortController; timer: ReturnType<typeof setTimeout>; epoch: number; revision: number };
  let pending: Read | null = null;
  let progress: UsbipUpgradeStatus | null = null, progressDeadline = 0, progressExpired = false;
  let progressTimer: ReturnType<typeof setTimeout> | undefined;
  function publish(next: UsbipUpgradeStatus): void {
    if (JSON.stringify(status) === JSON.stringify(next)) return;
    status = next;
    ports.changed(next);
  }
  function clearProgressTimer(): void {
    if (progressTimer !== undefined) clearTimer(progressTimer);
    progressTimer = undefined;
  }
  function publishExpiredProgress(): void {
    if (progress) publish({ ...progress, phase: 'unverified',
      detail: 'USBIP 准备较长时间没有新进展，状态待确认；原任务仍在后台。请重新显示窗口确认，或导出日志。' });
  }
  function armProgressTimer(): void {
    if (!active || !progress || progressTimer !== undefined) return;
    if (progressExpired || now() >= progressDeadline) {
      progressExpired = true;
      publishExpiredProgress();
      return;
    }
    const startedEpoch = epoch, observedProgress = progress;
    progressTimer = setTimer(() => {
      if (!active || epoch !== startedEpoch || progress !== observedProgress) return;
      progressTimer = undefined;
      progressExpired = true;
      publishExpiredProgress();
    }, progressDeadline - now());
  }
  function present(next: UsbipUpgradeStatus): void {
    const progressing = !next.observationOnly && ['checking', 'update-found', 'removing', 'installing', 'verifying'].includes(next.phase);
    if (!progressing) {
      clearProgressTimer();
      progress = null; progressDeadline = 0; progressExpired = false;
      publish(next);
      return;
    }
    // The identity of actual progress survives stop/start. Replays never buy more time.
    if (!progress || progress.driverSetupSession !== next.driverSetupSession || progress.sequence !== next.sequence) {
      clearProgressTimer();
      progress = next; progressDeadline = now() + progressTimeoutMs; progressExpired = false;
    }
    if (progressExpired) publishExpiredProgress();
    else publish(next);
    armProgressTimer();
  }
  function merge(value: unknown, source: 'event' | 'read', authoritative = false): boolean {
    const parsed = parseUsbipUpgradeStatus(value), next = parsed.status;
    const incomingSession = next.driverSetupSession, incomingSequence = next.sequence;
    if (incomingSession && retiredSessions.has(incomingSession)) return false;
    // Unattributed or incompatible packets cannot retire a verified session or poison its sequence.
    const sameSchema = record(value) && record(value.usbipUpgrade) && value.usbipUpgrade.schemaVersion === 1;
    if (!parsed.valid && (!session || incomingSession !== session || !sameSchema)) {
      if (!session) publish(next);
      return false;
    }
    if (incomingSession === session && incomingSequence !== null && incomingSequence < sequence) return false;
    if (parsed.valid && incomingSession !== session) {
      if (session) retiredSessions.add(session);
      session = incomingSession; sequence = -1; fingerprint = ''; tainted = false;
    }
    const key = JSON.stringify(next);
    if (incomingSequence !== null) {
      if (incomingSequence === sequence) {
        if (parsed.valid && ((!tainted && fingerprint === key) || (source === 'read' && authoritative))) {
          tainted = false; fingerprint = key;
          present(next);
          return true;
        }
        // A reply dispatched before a live event cannot rewrite that event at the same sequence.
        if (source === 'read' && !authoritative) return false;
        tainted = true;
        publish({ ...unverifiedUsbipUpgradeStatus('USBIP 同一序号的状态报告不一致，版本状态待确认。'),
          driverSetupSession: session, sequence });
        return true;
      }
      sequence = incomingSequence;
      fingerprint = key;
    }
    tainted = !parsed.valid;
    if (parsed.valid) present(next);
    else publish(next);
    return true;
  }
  function finishRead(request: Read): void {
    clearTimer(request.timer);
    pending = null;
  }
  function continueRead(): void {
    if (active && reread) { reread = false; readOnce(); }
  }
  function readOnce(): void {
    if (!active) return;
    if (pending) { reread = true; return; }
    const request: Read = { abort: new AbortController(), epoch, revision: eventRevision,
      timer: undefined as unknown as ReturnType<typeof setTimeout> };
    pending = request;
    const isCurrent = () => active && epoch === request.epoch && pending === request;
    const fail = (detail: string) => {
      if (!isCurrent()) return;
      finishRead(request);
      request.abort.abort();
      // A read failure cannot erase a newer live event.
      if (eventRevision === request.revision) publish(unverifiedUsbipUpgradeStatus(detail));
      continueRead();
    };
    request.timer = setTimer(() => fail('读取 USBIP 状态已超过 1500ms，版本状态待确认。重新显示窗口可再次确认。'), USBIP_UPGRADE_READ_TIMEOUT_MS);
    try {
      void Promise.resolve(ports.read(request.abort.signal)).then(value => {
        if (!isCurrent()) return;
        finishRead(request);
        const authoritative = eventRevision === request.revision;
        const parsed = parseUsbipUpgradeStatus(value);
        // Same-session ordering is decided by sequence, even after a duplicate event.
        // Different-session replies retain the existing live-event fence.
        if (authoritative || (parsed.valid && parsed.status.driverSetupSession === session))
          merge(value, 'read', authoritative);
        continueRead();
      }, () => fail('暂时无法读取 USBIP 配套版本状态。请重新显示窗口确认；若仍失败，请导出日志。'));
    } catch {
      fail('暂时无法读取 USBIP 配套版本状态。请重新显示窗口确认；若仍失败，请导出日志。');
    }
  }
  function refresh(): void {
    if (!active || refreshQueued) return;
    refreshQueued = true;
    const requestedEpoch = epoch;
    queueMicrotask(() => {
      if (!active || epoch !== requestedEpoch) return;
      refreshQueued = false;
      readOnce();
    });
  }
  function start(): void {
    if (active) return;
    active = true; const startedEpoch = ++epoch;
    eventRevision = 0; reread = false; refreshQueued = false;
    publish(unverifiedUsbipUpgradeStatus());
    armProgressTimer();
    let visible = ports.isVisible();
    stopEvents = ports.subscribe(value => {
      if (active && epoch === startedEpoch && merge(value, 'event')) ++eventRevision;
    });
    stopVisibility = ports.watchVisibility(next => {
      if (!active || epoch !== startedEpoch) return;
      const returned = next && !visible;
      visible = next;
      if (returned) readOnce();
    });
    stopRefresh = ports.watchRefresh?.(refresh);
    readOnce();
  }
  function stop(): void {
    if (!active) return;
    active = false; ++epoch; reread = false; refreshQueued = false;
    clearProgressTimer();
    stopEvents?.(); stopEvents = undefined;
    stopVisibility?.(); stopVisibility = undefined;
    stopRefresh?.(); stopRefresh = undefined;
    if (pending) {
      const request = pending;
      finishRead(request);
      request.abort.abort();
    }
  }
  return { start, stop, refresh, getSnapshot: () => status };
}
