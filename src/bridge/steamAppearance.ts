import { fs, shell, settingsStore, type RunResult } from './api';
import { invoke } from './ipc';
import { loadSettings, updateSettings } from './settingsRepository';
import { assertBootPreferenceOwner, captureBootPreferenceWrite, rememberOrdinaryAppearancePreferences, type BootPreferenceWrite } from './bootModeGlobalPreferences';

export const STEAM_APPEARANCE_THEMES = [
  { name: 'Art Hero', label: '首页布局', description: '保留宽幅背景与轮播比例，取消附加特效', group: '首页' },
  { name: 'Centered Game Text', label: '文字居中', description: '让游戏标题保持居中', group: '首页' },
  { name: 'Darken Unfocused Games', label: '焦点突出', description: '保留静态焦点提示，关闭封面滤镜与装饰动画', group: '首页' },
  { name: 'Mini Carousel', label: '紧凑轮播', description: '使用你设定的迷你轮播尺寸', group: '首页' },
  { name: 'NEX-85 Keyboard', label: '定制键盘', description: '平面键盘配色，保留按键比例与焦点提示', group: '界面' },
  { name: 'Round', label: '界面圆角', description: '保留圆角与静态描边，取消阴影和呼吸动画', group: '界面' },
  { name: 'YMCC Game Details', label: '放大详情背景', description: '放大至开始游戏操作行，底部仅留少量空隙', group: '游戏详情' },
] as const;
// Bundled presets enable every managed effect; a missing snapshot is not an off selection.
export const STEAM_APPEARANCE_DEFAULT_ENABLED = true;
export type AppearanceThemeName = (typeof STEAM_APPEARANCE_THEMES)[number]['name'];
export interface AppearanceRenderPlan { protocol: 4; files: {path: string; property: string; contentProperty: string; enabled: boolean}[]; variables: {property: string; enabled: boolean; value: string}[]; }
export interface AppearanceThemeState {
  name: AppearanceThemeName; installed: boolean; configured: boolean;
  backendEnabled: boolean | null; parameterCount: number; error: string; renderPlan?: AppearanceRenderPlan | null;
}
export interface AppearanceSnapshot {
  controlProtocol?: number;
  presetScopeProtocol?: number;
  refreshScopeProtocol?: number;
  applyScopeProtocol?: number;
  packAvailable?: boolean; packOwned?: boolean; packUpdateAvailable?: boolean; packPrepared?: boolean; packRepairRequired?: boolean; backendReloadRequired?: boolean; canRestore?: boolean; foreignActiveCount?: number; packError?: string;
  targetId?: string; backendIdle?: boolean;
  revision: string; linked: boolean; backendReady: boolean; backendProcessCount: number;
  steamRunning: boolean; configuredMatchesBackend: boolean; note: string; themes: AppearanceThemeState[];
  // Optional on older helpers; only used to correlate a successful Status with a Steam UI observation.
  steamPid?: number; steamBirth?: string;
}
interface RefreshReloadScope { targetId: string; revision: string; steamPid: number; steamBirth: string; }
export interface AppearanceResult { renderConfirmed?: boolean; renderContextsComplete?: boolean; reloaded?: boolean; previousRevision?: string; editorProtected?: boolean; ok: boolean; saved?: boolean; preferenceSaved?: boolean; preferenceReason?: string; message?: string; snapshot?: AppearanceSnapshot; statusLockTimedOut?: true; refreshPreparationRequired?: true; refreshReload?: RefreshReloadScope; applyPreflightChanged?: true; }
type Changes = Partial<Record<AppearanceThemeName, boolean>>;
interface IdlePolicy { processQuery?: string; sharedSteamReadProtocol?: number; supported: boolean; version: number; pid: number; birth: string; manualObservationProtocol?: number; automaticRepairActive?: boolean; automaticAppliedRevision?: number; }
export type AppearanceReconcileResult = AppearanceResult | 'superseded' | undefined;
interface UseRelease { ok: boolean; minimumHoldPending?: boolean; editorProtected?: boolean; editorStateUnknown?: boolean; error?: number; }
interface UseTicket { ok: boolean; token: string; pid: number; birth: string; backendPath: string; }
type EnsureWake = 'completed' | 'canceled' | 'timeout';
interface EnsureObservation { wake: Promise<EnsureWake>; close(): void; }
interface Dependencies {
  loadBootPreferences?(): Promise<Record<string, any>>;
  rememberBootPreferences?(states: Partial<Record<AppearanceThemeName, boolean>>): Promise<void>;
  idlePolicy?(): Promise<IdlePolicy>;
  beginUse?(options?: BootPreferenceWrite): Promise<UseTicket>;
  ensureUse?(token: string, options?: BootPreferenceWrite): Promise<{ ok: boolean; completionEvent?: number }>;
  observeEnsure?(token: string): EnsureObservation;
  endUse?(token: string): Promise<UseRelease>;
  location(): Promise<{ directory: string }>;
  exists(path: string): Promise<boolean>;
  run(program: string, args: string[], timeoutMs: number, options?: BootPreferenceWrite): Promise<RunResult>;
  hidden(program: string, args: string[]): Promise<{ ok: boolean }>;
  delay(ms: number): Promise<void>;
}
export function validAppearanceSnapshot(value: any): value is AppearanceSnapshot {
  return !!value && (value.controlProtocol === undefined || Number.isSafeInteger(value.controlProtocol) && value.controlProtocol >= 1) && typeof value.revision === 'string' && /^[a-f\d]{64}$/i.test(value.revision)
    && (value.targetId === undefined || typeof value.targetId === 'string' && /^[a-f\d]{16}$/i.test(value.targetId))
    && (value.presetScopeProtocol === undefined || Number.isSafeInteger(value.presetScopeProtocol) && value.presetScopeProtocol >= 1)
    && (value.refreshScopeProtocol === undefined || Number.isSafeInteger(value.refreshScopeProtocol) && value.refreshScopeProtocol >= 1)
    && (value.applyScopeProtocol === undefined || Number.isSafeInteger(value.applyScopeProtocol) && value.applyScopeProtocol >= 1)
    && ['linked', 'backendReady', 'steamRunning', 'configuredMatchesBackend'].every(key => typeof value[key] === 'boolean')
    && Number.isSafeInteger(value.backendProcessCount) && value.backendProcessCount >= 0
    && ['packAvailable','packOwned','packPrepared','packRepairRequired','backendReloadRequired','canRestore','backendIdle'].every(key => value[key] === undefined || typeof value[key] === 'boolean')
    && (value.foreignActiveCount === undefined || Number.isSafeInteger(value.foreignActiveCount) && value.foreignActiveCount >= 0)
    && (value.packError === undefined || typeof value.packError === 'string')
    && typeof value.note === 'string' && Array.isArray(value.themes) && value.themes.length === STEAM_APPEARANCE_THEMES.length
    && STEAM_APPEARANCE_THEMES.every(theme => value.themes.filter((row: any) => row?.name === theme.name).length === 1)
    && value.themes.every((row: any) => typeof row.installed === 'boolean' && typeof row.configured === 'boolean'
      && (row.backendEnabled === null || typeof row.backendEnabled === 'boolean') && typeof row.error === 'string'
      && Number.isSafeInteger(row.parameterCount) && row.parameterCount >= 0
      && (row.renderPlan == null || row.renderPlan.protocol === 4 && Array.isArray(row.renderPlan.files)
        && row.renderPlan.files.length > 0 && row.renderPlan.files.length <= 128
        && new Set(row.renderPlan.files.map((file: any) => file?.contentProperty)).size === row.renderPlan.files.length
        && new Set(row.renderPlan.files.map((file: any) => file?.property)).size === row.renderPlan.files.length
        && row.renderPlan.files.every((file: any) => typeof file?.path === 'string' && file.path.length <= 128
          && /^[A-Za-z0-9][A-Za-z0-9_./ -]*\.css$/.test(file.path) && !file.path.split('/').some((part: string) => ['', '.', '..'].includes(part))
          && typeof file.property === 'string' && /^--ymcc-file-[a-f0-9]{16}$/.test(file.property)
          && typeof file.contentProperty === 'string' && /^--ymcc-content-[0-9a-f]{16}$/.test(file.contentProperty)
          && typeof file.enabled === 'boolean' && (row.configured || !file.enabled))
        && Array.isArray(row.renderPlan.variables) && row.renderPlan.variables.length <= 64
        && new Set(row.renderPlan.variables.map((variable: any) => variable?.property)).size === row.renderPlan.variables.length
        && row.renderPlan.variables.every((variable: any) => typeof variable?.property === 'string' && /^--[a-z][a-z0-9-]{0,79}$/.test(variable.property)
          && !variable.property.startsWith('--ymcc-') && typeof variable.enabled === 'boolean' && variable.enabled === row.configured
          && typeof variable.value === 'string' && variable.value.length <= 96 && /^[A-Za-z0-9#.% +_-]*$/.test(variable.value)
          && (variable.enabled || variable.value === ''))));
}
export function createSteamAppearance(deps: Dependencies) {
  let activeOperation = false;
  let useTicket: UseTicket | null = null;
  let scriptLocation: Promise<string> | undefined;
  const reconciled = new Set<string>();
  type Operation = { write: BootPreferenceWrite; expired: boolean };
  const captureOperation = (options: BootPreferenceWrite): Operation => ({ write: captureBootPreferenceWrite(options), expired: false });
  async function requireCurrent(operation?: Operation): Promise<void> {
    if (!operation?.write.bootContext) return;
    if (!operation.expired) {
      try {
        if (!deps.loadBootPreferences) throw Error('Appearance boot preference reader unavailable');
        assertBootPreferenceOwner(await deps.loadBootPreferences(), operation.write);
        return;
      } catch { operation.expired = true; }
    }
    throw Error('BOOT_APPEARANCE_REQUEST_EXPIRED');
  }
  const owned = <T>(operation: Operation | undefined, work: () => Promise<T>, beforeWork?: () => void): Promise<T> => {
    if (!operation?.write.bootContext) {
      beforeWork?.();
      return work();
    }
    return (async () => {
      await requireCurrent(operation);
      beforeWork?.();
      const result = await work();
      await requireCurrent(operation);
      return result;
    })();
  };
  function writeOrigin(options: BootPreferenceWrite): 'ordinary' | 'boot-mode' | 'runtime' {
    const origin = options.origin === undefined ? 'ordinary' : options.origin;
    if (!['ordinary', 'boot-mode', 'runtime'].includes(origin)) throw new Error('外观写入来源无效');
    return origin;
  }
  async function rememberPreference(result: AppearanceResult, states: Partial<Record<AppearanceThemeName, boolean>>, origin: string, targetId?: string): Promise<AppearanceResult> {
    if (origin !== 'ordinary' || !result.ok && result.saved !== true) return result;
    if (!result.snapshot || targetId && result.snapshot.targetId !== targetId ||
        Object.entries(states).some(([name, enabled]) => result.snapshot!.themes.find(row => row.name === name)?.configured !== enabled))
      return { ...result, ok: false, preferenceSaved: false, preferenceReason: '开机全局外观偏好未保存：外观保存回执与本次选择不符' };
    try {
      if (!deps.rememberBootPreferences) throw new Error('全局偏好保存入口不可用');
      await deps.rememberBootPreferences(states);
      return { ...result, preferenceSaved: true };
    } catch (error) {
      return { ...result, preferenceSaved: false, preferenceReason: '开机全局外观偏好未保存：' + (error instanceof Error ? error.message : String(error)) };
    }
  }
  function preferenceMessage(result: AppearanceResult): AppearanceResult {
    return result.preferenceSaved === false ? { ...result, message: [result.message, result.preferenceReason].filter(Boolean).join('；') } : result;
  }
  function validPolicy(policy?: IdlePolicy): policy is IdlePolicy {
    return !!policy?.supported && policy.version === 3 && policy.manualObservationProtocol === 1 && Number.isSafeInteger(policy.pid) && policy.pid > 0 && policy.pid <= 0xffffffff && /^[1-9]\d{0,19}$/.test(policy.birth);
  }
  async function releaseUse() {
    const ticket = useTicket; useTicket = null;
    const released = ticket ? await deps.endUse?.(ticket.token) : undefined;
    const editorProtected = released?.editorProtected === true && !released.error;
    if (ticket && !released?.ok && !editorProtected) throw new Error('外观请求释放未确认，请查看 Steam 外观记录');
    return { holding: released?.minimumHoldPending === true, editorProtected, editorStateUnknown: released?.editorStateUnknown === true };
  }
  function assertControlProtocol(snapshot: AppearanceSnapshot) {
    if (snapshot.controlProtocol !== 2) throw new Error('外观脚本与当前 YMCC 不配套，请同步 PowerControl 中的外观脚本和主题资源后重试');
  }
  async function useBackend(snapshot: Readonly<AppearanceSnapshot>, operation?: Operation, matchObservedSteam = false) {
    assertControlProtocol(snapshot);
    if (!snapshot.steamRunning) return; // Save only: never start Steam or CSS to replay settings.
    const policy = await owned(operation, async () => deps.idlePolicy?.());
    if (!validPolicy(policy) || !deps.beginUse || !deps.ensureUse || !deps.endUse)
      throw new Error('当前原生组件不支持常驻外观后台，请同步更新 YMCC');
    if (matchObservedSteam && (policy.pid !== snapshot.steamPid || policy.birth !== snapshot.steamBirth))
      throw new Error('Steam 会话已变化，请重新读取外观状态');
    // Keep one ticket for this serialized operation. Revalidate the Steam
    // identity before reuse; a second Begin would supersede the first token.
    if (useTicket) {
      if (useTicket.pid !== policy.pid || useTicket.birth !== policy.birth)
        throw new Error('Steam 会话已变化，请重新读取外观状态');
      return;
    }
    await requireCurrent(operation);
    const ticket = await deps.beginUse(operation?.write);
    // Retain the exact ticket before the post-await check, so stale requests
    // still release their acquired lease through the ordinary finally path.
    useTicket = ticket;
    await requireCurrent(operation);
    if (!ticket.ok || !/^[1-9]\d{0,19}$/.test(ticket.token) || typeof ticket.backendPath !== 'string' || !/^[a-z]:\\.*\\CssLoader-Standalone-Headless\.exe$/i.test(ticket.backendPath) || /[\r\n"\0]/.test(ticket.backendPath) || ticket.pid !== policy.pid || ticket.birth !== policy.birth)
      throw new Error('Steam 会话已变化，请重新读取外观状态');
  }
  function resolveScriptLocation() {
    // The native PowerControl directory is fixed for the process; the file is not.
    return scriptLocation ??= (async () => {
      const location = await deps.location();
      const dir = typeof location?.directory === 'string' ? location.directory.replace(/\//g, '\\').replace(/\\+$/, '') : '';
      if (!/^[a-z]:\\/i.test(dir)) throw new Error('无法确认 YMCC 配置目录');
      return dir + '\\steam-appearance.ps1';
    })().catch(error => { scriptLocation = undefined; throw error; });
  }
  type Observation = { requireCurrent?: () => void; beforeDispatch?: (policy: IdlePolicy) => void };
  const observedApplyRevisions = new WeakMap<AppearanceSnapshot, number>();
  function applyRevision(policy: IdlePolicy | undefined, snapshot: AppearanceSnapshot) {
    const revision = policy?.automaticAppliedRevision;
    return validPolicy(policy) && policy.pid === snapshot.steamPid && policy.birth === snapshot.steamBirth &&
      typeof revision === 'number' && Number.isSafeInteger(revision) && revision >= 0 ? revision : undefined;
  }
  async function scriptPath(operation?: Operation, observation?: Observation) {
    const path = await owned(operation, resolveScriptLocation, observation?.requireCurrent);
    observation?.requireCurrent?.();
    if (!(await owned(operation, () => deps.exists(path), observation?.requireCurrent))) throw new Error('当前 YMCC 配置目录缺少外观控制组件：' + path + '。请修复组件部署；这不代表 CSSLoader 或当前美化未运行。');
    return path;
  }
  async function request(action: 'Status' | 'Apply' | 'ScopedApply' | 'Refresh' | 'ScopedRefresh' | 'Reload' | 'ScopedReload' | 'Prepare' | 'Reset' | 'Restore' | 'ScopedPreset' | 'Idle' | 'Reconcile', payload?: object, timeoutMs?: number, operation?: Operation, expectedUse?: UseTicket, observation?: Observation): Promise<AppearanceResult> {
    const path = await scriptPath(operation, observation);
    observation?.requireCurrent?.();
    const args = ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', path, '-Action', action];
    if (useTicket) args.push('-BackendPath', useTicket.backendPath);
    const policy = await owned(operation, async () => deps.idlePolicy?.(), observation?.requireCurrent);
    observation?.requireCurrent?.();
    if (!policy?.supported || policy.sharedSteamReadProtocol !== 1 || !policy.processQuery || !/^Local\\YMCC\.CssInventory\.[a-f0-9]{32}$/.test(policy.processQuery))
      throw new Error('Steam 公共读取组件与外观脚本不配套，请完整更新 YMCC 和 PowerControl');
    if (expectedUse && (useTicket !== expectedUse || policy.pid !== expectedUse.pid || policy.birth !== expectedUse.birth ||
        ((action === 'Refresh' || action === 'ScopedRefresh' || action === 'ScopedApply' || action === 'ScopedReload') && !validPolicy(policy))))
      throw new Error('Steam 会话已变化，未继续确认美化后台');
    args.push('-ProcessQuery', policy.processQuery, '-SessionPid', String(policy.pid), '-SessionBirth', policy.birth);

    // Older helpers ignore the profile and return the full snapshot.
    const body = action === 'Idle' ? payload : { profile: 'ui-summary-v1', ...payload };
    if (body) {
      const encoded = btoa(Array.from(new TextEncoder().encode(JSON.stringify(body)), byte => String.fromCharCode(byte)).join(''));
      args.push('-Payload', encoded);
    }
    const defaultTimeout = action === 'Status' ? 22000 : 45000;
    const output = await owned(operation, () => {
      observation?.beforeDispatch?.(policy);
      return operation?.write.bootContext
        ? deps.run('powershell.exe', args, timeoutMs ? Math.min(defaultTimeout, timeoutMs) : defaultTimeout, operation.write)
        : deps.run('powershell.exe', args, timeoutMs ? Math.min(defaultTimeout, timeoutMs) : defaultTimeout);
    }, observation?.requireCurrent);
    observation?.requireCurrent?.();
    let result: AppearanceResult;
    try { result = JSON.parse(output.stdout.trim().replace(/^\uFEFF/, '')); }
    catch {
      const detail = output.stderr || output.stdout;
      if (/NamedParameterNotFound|ParameterBinding|BackendPath/i.test(detail))
        throw new Error('外观脚本与当前 YMCC 不配套：' + path + '。请同步外观组件后重试');
      throw new Error('外观控制结果无法解析：' + detail.slice(0, 240));
    }
    const reload = result?.refreshReload;
    if (!result || typeof result !== 'object' || typeof result.ok !== 'boolean'
      || (result.saved !== undefined && typeof result.saved !== 'boolean')
      || (result.renderConfirmed !== undefined && typeof result.renderConfirmed !== 'boolean')
      || (result.renderContextsComplete !== undefined && typeof result.renderContextsComplete !== 'boolean')
      || (result.message !== undefined && typeof result.message !== 'string')
      || (result.statusLockTimedOut !== undefined && (result.statusLockTimedOut !== true || action !== 'Status' || result.ok || result.snapshot !== undefined))
      || (result.refreshPreparationRequired !== undefined && (result.refreshPreparationRequired !== true || action !== 'ScopedRefresh' || result.ok || result.saved !== undefined || result.renderConfirmed !== undefined || result.renderContextsComplete !== undefined || result.snapshot !== undefined))
      || (reload !== undefined && (result.refreshPreparationRequired !== true || !reload || Object.keys(reload).length !== 4 ||
        typeof reload.targetId !== 'string' || !/^[a-f\d]{16}$/i.test(reload.targetId) || typeof reload.revision !== 'string' || !/^[a-f\d]{64}$/i.test(reload.revision) ||
        !Number.isSafeInteger(reload.steamPid) || reload.steamPid < 1 || reload.steamPid > 0xffffffff ||
        typeof reload.steamBirth !== 'string' || !/^[1-9]\d{0,19}$/.test(reload.steamBirth)))
      || (result.applyPreflightChanged !== undefined && (result.applyPreflightChanged !== true || action !== 'ScopedApply' || result.ok || result.saved !== undefined || result.renderConfirmed !== undefined || result.renderContextsComplete !== undefined || result.snapshot !== undefined))
      || (result.snapshot !== undefined && !validAppearanceSnapshot(result.snapshot))) throw new Error('外观控制返回了无效状态');
    if (result.ok && result.renderConfirmed === false) throw new Error('外观控制回执与页面样式确认矛盾');
    if (output.exitCode !== 0 && result.ok) throw new Error('外观控制执行失败');
    return result;
  }
  async function read(operation?: Operation, singleWait = false, current?: () => boolean, beforeStatusDispatch?: () => void) {
    const requireObservation = current && (() => { if (!current()) throw new Error('Appearance observation retired'); });
    let statusPolicy: IdlePolicy | undefined;
    const observation: Observation | undefined = singleWait
      ? { requireCurrent: requireObservation, beforeDispatch: policy => { statusPolicy = policy; beforeStatusDispatch?.(); } }
      : requireObservation && { requireCurrent: requireObservation };
    // Only a legacy busy response needs the short shell retry on a UI read.
    for (let attempt = 0; attempt < 3; attempt++) {
      requireObservation?.();
      const result = await request('Status', singleWait && attempt === 0 ? { lockWait: 'single-v1' } : undefined, undefined, operation, undefined, observation);
      requireObservation?.();
      if (result.ok && result.snapshot) {
        const snapshot = result.snapshot;
        if (snapshot.steamRunning && snapshot.backendReady && snapshot.packOwned && !snapshot.packError &&
            (!snapshot.configuredMatchesBackend || snapshot.packRepairRequired || snapshot.backendReloadRequired)) {
          const revision = applyRevision(statusPolicy, snapshot);
          if (revision !== undefined) observedApplyRevisions.set(snapshot, revision);
        }
        return snapshot;
      }
      if (result.statusLockTimedOut) throw new Error('外观配置正被其他操作占用，请稍后重试');
      if (!/Theme operation busy/i.test(result.message || '') || attempt === 2) throw new Error(result.message || '无法读取外观状态');
      await owned(operation, () => deps.delay(250), requireObservation);
    }
    throw new Error('外观状态暂未确认，请重试');
  }
  async function serialized<T>(work: () => Promise<T>, operation?: Operation): Promise<T> {
    if (activeOperation) throw new Error('外观操作进行中，请稍后重试');
    activeOperation = true;
    try {
      await requireCurrent(operation);
      const result = await work();
      // CSS is resident. Completing this explicit operation must end its
      // temporary native lease; the native observer confirms/rebinds read-only.
      await releaseUse();
      await requireCurrent(operation);
      return result;
    } catch (error) { await releaseUse().catch(() => {}); throw error; }
    finally { activeOperation = false; }
  }
  async function ensureReady(initial?: AppearanceSnapshot, operation?: Operation) {
    let snapshot = initial || await read(operation);
    if (!snapshot.steamRunning) return snapshot;
    await useBackend(snapshot, operation);
    if (snapshot.backendReady) {
      if (!snapshot.backendReloadRequired) return snapshot;
      // The verified loader can reread a new/updated theme without starting a
      // native helper or taking ownership from an open Desktop editor.
      const loaded = await request('Reload', { expectedRevision: snapshot.revision, targetId: snapshot.targetId }, undefined, operation);
      if (loaded.snapshot?.targetId && loaded.snapshot.targetId.toLowerCase() !== snapshot.targetId?.toLowerCase())
        throw new Error('加载器主题目录已改变，请重新读取当前目录后再套用预设');
      if (!loaded.ok || !loaded.snapshot || !loaded.snapshot.backendReady || loaded.snapshot.backendReloadRequired
        || loaded.snapshot.targetId?.toLowerCase() !== snapshot.targetId?.toLowerCase()
        || loaded.reloaded !== true || loaded.previousRevision !== snapshot.revision || !loaded.snapshot.configuredMatchesBackend)
        throw new Error(loaded.message || '现有 CSSLoader 重读主题未确认，选择已保留');
      return loaded.snapshot;
    }
    if (!snapshot.linked && snapshot.backendProcessCount === 0) throw new Error('自定义外观后台配置缺失，请修复 YMCC 外观组件');
    // The fixed native helper is created suspended and placed in the use Job.
    // No detached shell.hidden launch, including on a partial/old native update.
    if (!useTicket) throw new Error('无法确认常驻美化后台');
    const expectedUse = useTicket;
    // Subscribe before invoking Ensure: the helper can finish before the IPC reply.
    const observation = operation?.write.bootContext ? undefined : deps.observeEnsure?.(expectedUse.token);
    let started: number, wake: EnsureWake | undefined;
    try {
      const ensured = await owned(operation, () => deps.ensureUse!(expectedUse.token, operation?.write));
      if (!ensured.ok) throw new Error('无法确认常驻美化后台');
      started = performance.now();
      if (observation && ensured.completionEvent === 1) wake = await owned(operation, () => observation.wake);
    } finally { observation?.close(); }
    if (wake === 'canceled') throw new Error('美化后台启动请求已取消，请重新读取外观状态');
    const deadline = started + 30000;
    // Completion is only a wakeup. A fresh Status must still prove readiness.
    // Skip elapsed (or immediately adjacent) slots after a call, not the next
    // genuinely spaced chance to recover from a busy or not-ready response.
    const offsets = wake === 'timeout' ? [0, 15000, 24000] : wake === 'completed' ? [0, 2000, 7000, 15000, 24000] : [2000, 7000, 15000, 24000];
    let lastReadAt = -Infinity, reads = 0;
    for (const offset of offsets) {
      if (reads >= 4) break;
      if (performance.now() >= deadline) break;
      if (wake && offset > 0 && started + offset < Math.max(lastReadAt + 1000, performance.now())) continue;
      const delay = started + offset - performance.now();
      if (delay > 0) await owned(operation, () => deps.delay(delay));
      const remaining = deadline - performance.now();
      if (remaining <= 0) break;
      lastReadAt = performance.now();
      reads++;
      try {
        // Startup already has its own bounded retry window; do not multiply
        // it by read()'s mutex retries or leave the UI waiting indefinitely.
        const result = await request('Status', undefined, Math.max(1, Math.min(7000, Math.floor(remaining))), operation, wake ? expectedUse : undefined);
        if (!result.ok || !result.snapshot) throw new Error(result.message || '无法读取外观状态');
        snapshot = result.snapshot;
      }
      catch (error) { if (/Theme operation busy/i.test((error as Error).message)) continue; throw error; }
      if (initial?.targetId && snapshot.targetId?.toLowerCase() !== initial.targetId.toLowerCase()) throw new Error('加载器主题目录已改变，请重新读取当前目录后再套用预设');
      if (snapshot.backendReady && !snapshot.backendReloadRequired) return snapshot;
    }
    throw new Error(snapshot.note || '美化后端尚未就绪；请稍后读取状态并重新应用');
  }
  async function settlePreset(result: AppearanceResult, operation?: Operation): Promise<AppearanceResult> {
    if (!result.snapshot || !result.ok && result.saved !== true) throw new Error(result.message || '无法套用 YMCC 预设');
    if (!result.ok) return result;
    let next = result.snapshot;
    try {
      if (next.backendProcessCount > 0 || (next.steamRunning && next.linked)) next = await ensureReady(next, operation);
      // Readiness after a reload is not a render receipt for the new snapshot.
      if (next !== result.snapshot) return { ...result, snapshot: next, renderConfirmed: undefined, renderContextsComplete: undefined };
      return { ...result, snapshot: next };
    } catch (error) {
      await requireCurrent(operation);
      try { next = await read(operation); } catch {}
      await requireCurrent(operation);
      return { ...result, ok: false, saved: true, snapshot: next, renderConfirmed: undefined, renderContextsComplete: undefined, message: '外观配置已保存，但后端重新加载未确认：' + (error as Error).message };
    }
  }
  async function prepare(snapshot: AppearanceSnapshot, settle = true, operation?: Operation, requireScope?: (snapshot: AppearanceSnapshot) => void) {
    if (snapshot.packPrepared !== false && !snapshot.packRepairRequired && !snapshot.foreignActiveCount) return snapshot;
    if (!snapshot.packAvailable) throw new Error(snapshot.packError || 'YMCC 自带主题资源缺失，请修复安装');
    const prepared = await request('Prepare', { expectedRevision: snapshot.revision }, undefined, operation);
    if (prepared.ok && prepared.snapshot) requireScope?.(prepared.snapshot);
    const result = settle ? await settlePreset(prepared, operation) : prepared;
    if (!result.ok || !result.snapshot) throw new Error(result.message || '预设已保存，但后端未确认');
    return result.snapshot;
  }
  function validateIntent(intent: Changes) {
    const entries = Object.entries(intent);
    if (!entries.length || entries.some(([name, enabled]) => !STEAM_APPEARANCE_THEMES.some(theme => theme.name === name) || typeof enabled !== 'boolean'))
      throw new Error('未知外观开关');
  }
  const changedStates = (current: Readonly<AppearanceSnapshot>, intent: Changes): Changes => Object.fromEntries(
    Object.entries(intent).filter(([name, enabled]) => current.themes.find(row => row.name === name)?.configured !== enabled));
  async function rememberCurrent(current: AppearanceSnapshot, intent: Changes, origin = 'ordinary') {
    return preferenceMessage(await rememberPreference({ ok: true, saved: true, snapshot: current, message: '选择已与配置一致，无需重复写入' }, intent, origin, current.targetId));
  }
  async function applyCurrent(current: AppearanceSnapshot, intent: Changes, patch: Changes, origin: string, operation?: Operation) {
    await useBackend(current, operation);
    const firstTakeover = current.packPrepared === false && current.packOwned !== true && !current.packRepairRequired;
    // Persist the user's switches before a missing backend launch. A launch
    // failure must not make an otherwise valid preset impossible to turn on.
    let baseline = await prepare(current, false, operation);
    if (baseline.targetId?.toLowerCase() !== current.targetId?.toLowerCase()) throw new Error('加载器主题目录已改变，请重新读取');
    if (baseline.backendReady && baseline.backendReloadRequired) baseline = await ensureReady(baseline, operation);
    const requested = firstTakeover ? { ...Object.fromEntries(current.themes.map(theme => [theme.name, theme.configured])), ...patch } : patch;
    const result = await rememberPreference(await request('Apply', { expectedRevision: baseline.revision, states: requested }, undefined, operation), intent, origin, current.targetId);
    return preferenceMessage(result.ok ? await settlePreset(result, operation) : result);
  }
  function applySelection(current: AppearanceSnapshot, intent: Changes, origin: string, operation: Operation) {
    const changed = changedStates(current, intent), hasChanges = Object.keys(changed).length > 0;
    if (!hasChanges && current.packPrepared !== false && !current.packRepairRequired && !current.foreignActiveCount)
      return rememberCurrent(current, intent, origin);
    return applyCurrent(current, intent, hasChanges ? changed : intent, origin, operation);
  }
  return {
    read: (current?: () => boolean, beforeStatusDispatch?: () => void) => read(undefined, true, current, beforeStatusDispatch),
    reconcileExisting: (snapshot: AppearanceSnapshot, current?: () => boolean, canDispatch?: () => boolean) => serialized<AppearanceReconcileResult>(async () => {
      let dispatched = false;
      // A click can veto an unstarted repair, but its dispatched receipt still matters.
      const isCurrent = () => current?.() !== false && (dispatched || canDispatch?.() !== false);
      if (!isCurrent()) return;
      const key = [snapshot.steamPid, snapshot.steamBirth, snapshot.targetId, snapshot.revision, snapshot.backendReloadRequired, snapshot.packRepairRequired,
        snapshot.themes.map(row => String(row.backendEnabled)).join(',')].join(':');
      if (reconciled.has(key)) return {ok:false,snapshot,message:'本次配置异常已检查；保留状态，可手动重新应用'};
      if (!snapshot.steamRunning || !snapshot.backendReady || !snapshot.packOwned || snapshot.packError)
        return {ok:false,snapshot,message:'现有外观后台未满足自动核对条件'};
      const retired = Symbol(), superseded = Symbol();
      const observedApply = observedApplyRevisions.get(snapshot);
      const markAttempt = () => {
        if (reconciled.size >= 32) reconciled.clear();
        reconciled.add(key); // Do not replay a failed/ambiguous write within the same Steam lifetime.
      };
      try {
        return await request('Reconcile', {expectedRevision:snapshot.revision,targetId:snapshot.targetId}, undefined, undefined, undefined, {
          requireCurrent: () => { if (!isCurrent()) throw retired; },
          beforeDispatch: policy => {
            // No new query or retry. Skipping does not spend the anomaly key.
            if (policy.pid === 0 && policy.birth === '0' ||
                snapshot.steamPid && snapshot.steamBirth && validPolicy(policy) &&
                (policy.pid !== snapshot.steamPid || policy.birth !== snapshot.steamBirth || policy.automaticRepairActive === true)) throw retired;
            const applied = applyRevision(policy, snapshot);
            if (observedApply !== undefined && applied !== undefined && applied > observedApply) throw superseded;
            markAttempt(); dispatched = true;
          },
        });
      } catch (error) {
        if (error === retired || (!dispatched && !isCurrent())) return;
        if (error === superseded) return 'superseded';
        if (!dispatched) markAttempt();
        throw error;
      }
    }),
    idle: (snapshot: AppearanceSnapshot) => serialized(async () => {
      // Compatibility API: release only the temporary request, never CSS.
      try { await releaseUse(); }
      catch (error) { return {ok:false,snapshot,message:'外观请求释放未确认：' + (error as Error).message}; }
      let current = snapshot;
      try {
        const final = await request('Status', undefined, 7000);
        if (!final.ok || !final.snapshot) return {ok:false,snapshot:current,message:final.message || '外观请求已结束，后台状态暂未确认'};
        current = final.snapshot;
      } catch (error) {
        return {ok:false,snapshot:current,message:'外观请求已结束，后台状态暂未确认：' + (error as Error).message};
      }
      return {ok:current.backendReady && current.configuredMatchesBackend,snapshot:current,message:'已释放外观请求，CSS 后台保持常驻'};
    }),
    start: () => serialized(() => ensureReady()),
    submitDraft: async (targetId: string, states: Changes, mode: 'selection' | 'preferences', onCurrent: (current: AppearanceSnapshot) => void, observed?: Readonly<AppearanceSnapshot> | null) => {
      const intent = { ...states };
      validateIntent(intent);
      return serialized(async () => {
        // Only a leased, ready ordinary draft can elide the separate Status.
        // Its revision must still match the freshly read files under the helper lock.
        let scopedUse: UseTicket | undefined;
        if (mode === 'selection' && observed?.controlProtocol === 2 && observed.applyScopeProtocol === 1 &&
            observed.targetId?.toLowerCase() === targetId && observed.steamRunning && observed.steamPid && observed.steamBirth &&
            observed.backendReady && observed.packOwned === true && observed.packPrepared === true && !observed.packError &&
            !observed.backendReloadRequired && !observed.packRepairRequired && !observed.foreignActiveCount && observed.themes.every(row => row.installed && !row.error) &&
            Object.entries(intent).some(([name, enabled]) => observed.themes.find(row => row.name === name)?.configured !== enabled)) {
          const patch = changedStates(observed, intent);
          await useBackend(observed, undefined, true);
          if (!useTicket) throw new Error('无法确认常驻美化后台');
          scopedUse = useTicket;
          const result = await request('ScopedApply', { targetId, expectedRevision: observed.revision, states: patch }, undefined, undefined, scopedUse);
          if (!result.applyPreflightChanged) {
            if (result.snapshot && (result.snapshot.targetId?.toLowerCase() !== targetId || !result.snapshot.steamRunning ||
                result.snapshot.steamPid !== scopedUse.pid || result.snapshot.steamBirth !== scopedUse.birth))
              throw new Error('外观应用的回执目录或 Steam 会话不符，请重新读取');
            if (!result.snapshot) throw new Error(result.message || '外观应用未确认，请重新读取');
            const saved = await rememberPreference(result, patch, 'ordinary', result.snapshot.targetId);
            return preferenceMessage(saved.ok ? await settlePreset(saved) : saved);
          }
        }
        // Ordinary drafts are bound to a directory, not to a stale UI revision.
        // The script independently checks this fresh revision before writing.
        const current = await read();
        const matches = !!targetId && current.targetId?.toLowerCase() === targetId;
        onCurrent(current);
        if (!matches) throw new Error(mode === 'preferences' ? '主题目录已变化，未补写旧全局偏好' : '加载器主题目录已改变，未跨目录提交旧选择');
        if (scopedUse && (!current.steamRunning || current.steamPid !== scopedUse.pid || current.steamBirth !== scopedUse.birth))
          throw new Error('Steam 会话已变化，请重新读取外观状态');
        if (mode === 'preferences') return rememberCurrent(current, intent);
        const patch = changedStates(current, intent);
        return Object.keys(patch).length ? applyCurrent(current, patch, patch, 'ordinary') : rememberCurrent(current, intent);
      });
    },
    reset: async (mode: 'preset' | 'previous' = 'preset', options: BootPreferenceWrite = {}, observed?: Readonly<AppearanceSnapshot> | null) => {
      const operation = captureOperation(options), origin = writeOrigin(operation.write);
      if (mode !== 'preset' && mode !== 'previous') throw new Error('未知外观恢复模式');
      const targetId = observed?.targetId;
      const shownTarget = typeof targetId === 'string' && targetId.length === 16 && /^[a-f\d]{16}$/i.test(targetId) ? targetId.toUpperCase() : undefined;
      const scopedTarget = observed?.controlProtocol === 2 && observed.presetScopeProtocol === 1 ? shownTarget : undefined;
      return serialized(async () => {
        // An old script rejects the new action instead of ignoring its target.
        const current = scopedTarget ? undefined : await read(operation);
        if (current) {
          assertControlProtocol(current);
          if (shownTarget && current.targetId?.toUpperCase() !== shownTarget) throw new Error('主题目录已变化，请重新读取后再恢复外观');
        }
        const acknowledged = scopedTarget
          ? await request('ScopedPreset', { mode, targetId: scopedTarget }, undefined, operation)
          : await request(mode === 'previous' ? 'Restore' : 'Reset', undefined, undefined, operation);
        if (scopedTarget) {
          if (!acknowledged.snapshot || acknowledged.snapshot.targetId?.toUpperCase() !== scopedTarget) {
            if (acknowledged.saved === true) return { ok: false, saved: true, message: acknowledged.ok || acknowledged.snapshot
              ? '外观配置已保存，但恢复回执的目录未确认，请重新读取' : acknowledged.message };
            throw new Error(acknowledged.message || '恢复外观的回执目录不符，请重新读取');
          }
        }
        const states = Object.fromEntries(acknowledged.snapshot?.themes.map(row => [row.name, row.configured]) || []);
        const saved = await rememberPreference(acknowledged, states, origin, scopedTarget ? acknowledged.snapshot?.targetId : current?.targetId);
        // Reload only supports a prepared YMCC pack. Restoring the original
        // files relinquishes ownership and must not take them over again.
        if (mode === 'previous' && saved.ok && saved.snapshot?.packOwned === false && saved.snapshot.backendReloadRequired && saved.snapshot.steamRunning)
          return preferenceMessage({ ...saved, ok: false, message: '已保存接管前的外观，画面尚未确认，等待 CSSLoader 下次读取原配置' });
        const result = await settlePreset(saved, operation);
        if (!result.ok) return preferenceMessage(result);
        const label = mode === 'previous' ? '接管前的外观' : 'YMCC 默认预设';
        return preferenceMessage({ ...result, message: result.snapshot?.backendReloadRequired ? '已保存' + label + '，等待美化后端启动后生效' : '已恢复' + label });
      }, operation);
    },
    apply: async (snapshot: AppearanceSnapshot, states: Partial<Record<AppearanceThemeName, boolean>>, options: BootPreferenceWrite = {}) => {
      const operation = captureOperation(options), origin = writeOrigin(operation.write);
      if (!validAppearanceSnapshot(snapshot)) throw new Error('请先读取外观状态');
      const intent = { ...states }, expectedRevision = snapshot.revision, expectedTarget = snapshot.targetId;
      validateIntent(intent);
      return serialized(async () => {
        const current = await read(operation);
        if (expectedTarget && current.targetId !== expectedTarget) throw new Error('加载器主题目录已改变，请重新读取');
        if (current.revision !== expectedRevision) throw new Error('外观配置已变化，请确认最新状态后重试');
        return applySelection(current, intent, origin, operation);
      }, operation);
    },
    applyScene: async (states: Changes, options: BootPreferenceWrite, observe: (read: () => Promise<AppearanceSnapshot>) => Promise<AppearanceSnapshot>) => {
      const operation = captureOperation(options);
      if (writeOrigin(operation.write) !== 'boot-mode') throw new Error('开机预设外观来源无效');
      const intent = { ...states };
      validateIntent(intent);
      return serialized(async () => {
        // Scene preferences have no UI revision to recheck. Read once under the
        // scene owner; the Apply helper independently validates that revision.
        let observing = true;
        const current = await observe(() => read(operation, false, () => observing)).finally(() => { observing = false; });
        await requireCurrent(operation);
        return applySelection(current, intent, 'boot-mode', operation);
      }, operation);
    },
    refresh: (beforeDispatch?: () => void, observed?: Readonly<AppearanceSnapshot> | null) => serialized(async () => {
      const shownTarget = observed?.targetId && /^[a-f\d]{16}$/i.test(observed.targetId) ? observed.targetId.toLowerCase() : undefined;
      const shownSteam = observed?.steamRunning && observed.steamPid && observed.steamBirth && /^[1-9]\d{0,19}$/.test(observed.steamBirth)
        ? { pid: observed.steamPid, birth: observed.steamBirth } : undefined;
      const shownScopeChanged = (snapshot: AppearanceSnapshot) =>
        !!(shownTarget && snapshot.targetId?.toLowerCase() !== shownTarget ||
          shownSteam && (!snapshot.steamRunning || snapshot.steamPid !== shownSteam.pid || snapshot.steamBirth !== shownSteam.birth));
      const requireShownScope = (snapshot: AppearanceSnapshot) => {
        if (shownScopeChanged(snapshot)) throw new Error('Steam 会话或主题目录已变化，请重新读取外观状态');
      };
      const scopedTarget = observed?.controlProtocol === 2 && observed.refreshScopeProtocol === 1 && shownSteam &&
        observed.backendReady && observed.packPrepared === true && observed.packOwned === true && !observed.packError &&
        !observed.packRepairRequired && !observed.foreignActiveCount && !observed.backendReloadRequired ? shownTarget : undefined;
      const dispatch = async () => {
        if (!useTicket) throw new Error('无法确认常驻美化后台');
        const result = await request(scopedTarget ? 'ScopedRefresh' : 'Refresh', scopedTarget ? { targetId: scopedTarget } : undefined,
          undefined, undefined, useTicket, { beforeDispatch });
        if (scopedTarget && (result.snapshot || result.ok || result.saved) && result.snapshot?.targetId?.toLowerCase() !== scopedTarget)
          throw new Error('外观重新应用的回执目录不符，请重新读取');
        return result;
      };
      if (scopedTarget && observed) {
        await useBackend(observed, undefined, true);
        const result = await dispatch();
        // Only an explicit no-write preflight result can enter the slow path.
        if (!result.refreshPreparationRequired) return result;
        if (result.refreshReload) {
          const scope = result.refreshReload, ticket = useTicket;
          if (!ticket || scope.targetId.toLowerCase() !== scopedTarget || scope.steamPid !== ticket.pid || scope.steamBirth !== ticket.birth)
            throw new Error('Steam 会话或主题目录已变化，请重新读取外观状态');
          const loaded = await request('ScopedReload', { targetId: scopedTarget, expectedRevision: scope.revision },
            undefined, undefined, ticket);
          const state = loaded.snapshot;
          if (!loaded.ok || typeof loaded.reloaded !== 'boolean' || loaded.previousRevision !== scope.revision || !state ||
              !state.steamRunning || state.steamPid !== ticket.pid || state.steamBirth !== ticket.birth || state.targetId?.toLowerCase() !== scopedTarget ||
              !state.backendReady || state.backendReloadRequired !== false || state.packPrepared !== true || state.packOwned !== true ||
              state.packError || state.packRepairRequired || state.foreignActiveCount)
            throw new Error(loaded.message || '现有 CSSLoader 重读主题未确认');
          const final = await dispatch();
          if (final.refreshPreparationRequired) throw new Error(final.message || '外观环境尚未就绪，请重新读取外观状态');
          return final;
        }
      }
      const current = await read();
      if (!scopedTarget && shownScopeChanged(current))
        return { ok: false, snapshot: current, message: 'Steam 会话或主题目录已变化，未重新应用外观' };
      requireShownScope(current);
      if (scopedTarget && (!useTicket || current.steamPid !== useTicket.pid || current.steamBirth !== useTicket.birth))
        throw new Error('Steam 会话或主题目录已变化，请重新读取外观状态');
      await useBackend(current, undefined, !!shownSteam);
      const snapshot = await prepare(current, true, undefined, requireShownScope);
      requireShownScope(snapshot);
      if (!snapshot.steamRunning) return { ok: true, saved: true, snapshot, message: 'YMCC 预设已保存，启动 Steam 后生效' };
      // Prepare can already have settled the backend. Dispatch still checks
      // the existing ticket against a fresh native policy before applying.
      assertControlProtocol(snapshot);
      if (!useTicket || !snapshot.backendReady || snapshot.backendReloadRequired) requireShownScope(await ensureReady(snapshot));
      const result = await dispatch();
      if (result.refreshPreparationRequired) throw new Error(result.message || '外观环境尚未就绪，请重新读取外观状态');
      return result;
    }),
  };
}
export const steamAppearance = createSteamAppearance({
  loadBootPreferences: loadSettings,
  rememberBootPreferences: async states => {
    const unchanged = new Error('No ordinary appearance preference delta');
    try {
      await updateSettings(settings => {
        if (!rememberOrdinaryAppearancePreferences(settings, states as Record<string, boolean>)) throw unchanged;
      }, { origin: 'runtime' });
    } catch (error) { if (error !== unchanged) throw error; }
  },
  idlePolicy: () => invoke<IdlePolicy>('steamAppearance.idlePolicy', {}, { timeoutMs: 3000 }),
  beginUse: (options = {}) => { const write = captureBootPreferenceWrite(options);return invoke<UseTicket>('steamAppearance.beginUse', write.bootContext ? { origin: write.origin, bootContext: write.bootContext } : {}, { timeoutMs: 4000 }); },
  ensureUse: (token, options = {}) => { const write = captureBootPreferenceWrite(options);return invoke<{ ok: boolean; completionEvent?: number }>('steamAppearance.ensureUse', { token, ...(write.bootContext ? { origin: write.origin, bootContext: write.bootContext } : {}) }, { timeoutMs: 4000 }); },
  observeEnsure: token => {
    let close: () => void = () => {};
    const eventName = 'ipc:steamAppearance.ensureFinished';
    const wake = new Promise<EnsureWake>(resolve => {
      const finish = (state: EnsureWake) => { close(); resolve(state); };
      const notice = (event: Event) => {
        const detail = (event as CustomEvent).detail;
        if (detail?.token === token && (detail.state === 'completed' || detail.state === 'canceled')) finish(detail.state);
      };
      const timer = setTimeout(() => finish('timeout'), 15000);
      close = () => { clearTimeout(timer); window.removeEventListener(eventName, notice); };
      window.addEventListener(eventName, notice);
    });
    return { wake, close };
  },
  endUse: token => invoke<UseRelease>('steamAppearance.endUse', { token }, { timeoutMs: 3000 }),
  location: settingsStore.location, exists: fs.exists,
  run: (program, args, timeoutMs, options) => shell.run(program, args, timeoutMs, undefined, undefined, options?.bootContext), hidden: shell.hidden,
  delay: ms => new Promise(resolve => setTimeout(resolve, ms)),
});
