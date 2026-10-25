import { fs, shell, settingsStore, type RunResult } from './api';
import { invoke } from './ipc';

export const STEAM_APPEARANCE_THEMES = [
  { name: 'Art Hero', label: '首页布局', description: '宽幅背景与游戏轮播布局', group: '首页' },
  { name: 'Centered Game Text', label: '文字居中', description: '让游戏标题保持居中', group: '首页' },
  { name: 'Darken Unfocused Games', label: '焦点突出', description: '暗化未选中的游戏封面', group: '首页' },
  { name: 'Game Cover Reflections', label: '封面倒影', description: '在游戏封面下显示柔和倒影', group: '首页' },
  { name: 'Mini Carousel', label: '紧凑轮播', description: '使用你设定的迷你轮播尺寸', group: '首页' },
  { name: 'NEX-85 Keyboard', label: '定制键盘', description: '使用 NEX-85 键盘外观', group: '界面' },
  { name: 'Round', label: '界面圆角', description: '沿用你的圆角与描边设置', group: '界面' },
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
  packAvailable?: boolean; packOwned?: boolean; packUpdateAvailable?: boolean; packPrepared?: boolean; packRepairRequired?: boolean; backendReloadRequired?: boolean; canRestore?: boolean; foreignActiveCount?: number; packError?: string;
  targetId?: string; backendIdle?: boolean;
  revision: string; linked: boolean; backendReady: boolean; backendProcessCount: number;
  steamRunning: boolean; configuredMatchesBackend: boolean; note: string; themes: AppearanceThemeState[];
}
export interface AppearanceResult { renderConfirmed?: boolean; reloaded?: boolean; previousRevision?: string; editorProtected?: boolean; ok: boolean; saved?: boolean; message?: string; snapshot?: AppearanceSnapshot; }
interface IdlePolicy { supported: boolean; version: number; pid: number; birth: string; manualObservationProtocol?: number; }
interface UseRelease { ok: boolean; minimumHoldPending?: boolean; editorProtected?: boolean; editorStateUnknown?: boolean; error?: number; }
interface UseTicket { ok: boolean; token: string; pid: number; birth: string; backendPath: string; }
interface Dependencies {
  idlePolicy?(): Promise<IdlePolicy>;
  beginUse?(): Promise<UseTicket>;
  ensureUse?(token: string): Promise<{ ok: boolean }>;
  endUse?(token: string): Promise<UseRelease>;
  location(): Promise<{ directory: string }>;
  exists(path: string): Promise<boolean>;
  run(program: string, args: string[], timeoutMs: number): Promise<RunResult>;
  hidden(program: string, args: string[]): Promise<{ ok: boolean }>;
  delay(ms: number): Promise<void>;
}
export function validAppearanceSnapshot(value: any): value is AppearanceSnapshot {
  return !!value && (value.controlProtocol === undefined || Number.isSafeInteger(value.controlProtocol) && value.controlProtocol >= 1) && typeof value.revision === 'string' && /^[a-f\d]{64}$/i.test(value.revision)
    && (value.targetId === undefined || typeof value.targetId === 'string' && /^[a-f\d]{16}$/i.test(value.targetId))
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
  const reconciled = new Set<string>();
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
  async function useBackend(snapshot: AppearanceSnapshot) {
    assertControlProtocol(snapshot);
    if (!snapshot.steamRunning) return; // Save only: never start Steam or CSS to replay settings.
    const policy = await deps.idlePolicy?.();
    if (!validPolicy(policy) || !deps.beginUse || !deps.ensureUse || !deps.endUse)
      throw new Error('当前原生组件不支持常驻外观后台，请同步更新 YMCC');
    // Keep one ticket for this serialized operation. Revalidate the Steam
    // identity before reuse; a second Begin would supersede the first token.
    if (useTicket) {
      if (useTicket.pid !== policy.pid || useTicket.birth !== policy.birth)
        throw new Error('Steam 会话已变化，请重新读取外观状态');
      return;
    }
    const ticket = await deps.beginUse();
    useTicket = ticket;
    if (!ticket.ok || !/^[1-9]\d{0,19}$/.test(ticket.token) || typeof ticket.backendPath !== 'string' || !/^[a-z]:\\.*\\CssLoader-Standalone-Headless\.exe$/i.test(ticket.backendPath) || /[\r\n"\0]/.test(ticket.backendPath) || ticket.pid !== policy.pid || ticket.birth !== policy.birth)
      throw new Error('Steam 会话已变化，请重新读取外观状态');
  }
  async function scriptPath() {
    const dir = (await deps.location()).directory.replace(/\//g, '\\').replace(/\\+$/, '');
    if (!/^[a-z]:\\/i.test(dir)) throw new Error('无法确认 YMCC 配置目录');
    const path = dir + '\\steam-appearance.ps1';
    if (!(await deps.exists(path))) throw new Error('当前 YMCC 配置目录缺少外观控制组件：' + path + '。请修复组件部署；这不代表 CSSLoader 或当前美化未运行。');
    return path;
  }
  async function request(action: 'Status' | 'Apply' | 'Refresh' | 'Reload' | 'Prepare' | 'Reset' | 'Restore' | 'Idle' | 'Reconcile', payload?: object, timeoutMs?: number): Promise<AppearanceResult> {
    const args = ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', await scriptPath(), '-Action', action];
    if (useTicket) args.push('-BackendPath', useTicket.backendPath);
    if (payload) {
      const encoded = btoa(Array.from(new TextEncoder().encode(JSON.stringify(payload)), byte => String.fromCharCode(byte)).join(''));
      args.push('-Payload', encoded);
    }
    const defaultTimeout = action === 'Status' ? 22000 : 45000;
    const output = await deps.run('powershell.exe', args, timeoutMs ? Math.min(defaultTimeout, timeoutMs) : defaultTimeout);
    let result: AppearanceResult;
    try { result = JSON.parse(output.stdout.trim().replace(/^\uFEFF/, '')); }
    catch {
      const detail = output.stderr || output.stdout;
      if (/NamedParameterNotFound|ParameterBinding|BackendPath/i.test(detail))
        throw new Error('外观脚本与当前 YMCC 不配套：' + await scriptPath() + '。请同步外观组件后重试');
      throw new Error('外观控制结果无法解析：' + detail.slice(0, 240));
    }
    if (!result || typeof result !== 'object' || typeof result.ok !== 'boolean'
      || (result.saved !== undefined && typeof result.saved !== 'boolean')
      || (result.renderConfirmed !== undefined && typeof result.renderConfirmed !== 'boolean')
      || (result.message !== undefined && typeof result.message !== 'string')
      || (result.snapshot !== undefined && !validAppearanceSnapshot(result.snapshot))) throw new Error('外观控制返回了无效状态');
    if (result.ok && result.renderConfirmed === false) throw new Error('外观控制回执与页面样式确认矛盾');
    if (output.exitCode !== 0 && result.ok) throw new Error('外观控制执行失败');
    return result;
  }
  async function read() {
    // Bounded recovery only for the shared operation mutex; no recurring UI poll.
    for (let attempt = 0; attempt < 3; attempt++) {
      const result = await request('Status');
      if (result.ok && result.snapshot) return result.snapshot;
      if (!/Theme operation busy/i.test(result.message || '') || attempt === 2) throw new Error(result.message || '无法读取外观状态');
      await deps.delay(250);
    }
    throw new Error('外观状态暂未确认，请重试');
  }
  async function serialized<T>(work: () => Promise<T>): Promise<T> {
    if (activeOperation) throw new Error('外观操作进行中，请稍后重试');
    activeOperation = true;
    try {
      const result = await work();
      // CSS is resident. Completing this explicit operation must end its
      // temporary native lease; the native observer confirms/rebinds read-only.
      await releaseUse();
      return result;
    } catch (error) { await releaseUse().catch(() => {}); throw error; }
    finally { activeOperation = false; }
  }
  async function ensureReady(initial?: AppearanceSnapshot) {
    let snapshot = initial || await read();
    if (!snapshot.steamRunning) return snapshot;
    await useBackend(snapshot);
    if (snapshot.backendReady) {
      if (!snapshot.backendReloadRequired) return snapshot;
      // The verified loader can reread a new/updated theme without starting a
      // native helper or taking ownership from an open Desktop editor.
      const loaded = await request('Reload', { expectedRevision: snapshot.revision, targetId: snapshot.targetId });
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
    if (!useTicket || !(await deps.ensureUse!(useTicket.token)).ok) throw new Error('无法确认常驻美化后台');
    const started = performance.now(), deadline = started + 30000;
    // Full Status starts PowerShell and validates the entire theme pack. Keep
    // the 30s startup window with four spaced reads, never ten complete audits.
    for (const offset of [2000, 7000, 15000, 24000]) {
      if (performance.now() >= deadline) break;
      await deps.delay(Math.max(0, started + offset - performance.now()));
      const remaining = deadline - performance.now();
      if (remaining <= 0) break;
      try {
        // Startup already has its own bounded retry window; do not multiply
        // it by read()'s mutex retries or leave the UI waiting indefinitely.
        const result = await request('Status', undefined, Math.max(1, Math.min(7000, Math.floor(remaining))));
        if (!result.ok || !result.snapshot) throw new Error(result.message || '无法读取外观状态');
        snapshot = result.snapshot;
      }
      catch (error) { if (/Theme operation busy/i.test((error as Error).message)) continue; throw error; }
      if (initial?.targetId && snapshot.targetId?.toLowerCase() !== initial.targetId.toLowerCase()) throw new Error('加载器主题目录已改变，请重新读取当前目录后再套用预设');
      if (snapshot.backendReady && !snapshot.backendReloadRequired) return snapshot;
    }
    throw new Error(snapshot.note || '美化后端尚未就绪；请稍后读取状态并重新应用');
  }
  async function settlePreset(result: AppearanceResult): Promise<AppearanceResult> {
    if (!result.ok || !result.snapshot) throw new Error(result.message || '无法套用 YMCC 预设');
    let next = result.snapshot;
    try {
      if (next.backendProcessCount > 0 || (next.steamRunning && next.linked)) next = await ensureReady(next);
      return { ...result, snapshot: next };
    } catch (error) {
      try { next = await read(); } catch {}
      return { ...result, ok: false, saved: true, snapshot: next, message: '外观配置已保存，但后端重新加载未确认：' + (error as Error).message };
    }
  }
  async function prepare(snapshot: AppearanceSnapshot, settle = true) {
    if (snapshot.packPrepared !== false && !snapshot.packRepairRequired && !snapshot.foreignActiveCount) return snapshot;
    if (!snapshot.packAvailable) throw new Error(snapshot.packError || 'YMCC 自带主题资源缺失，请修复安装');
    const prepared = await request('Prepare', { expectedRevision: snapshot.revision });
    const result = settle ? await settlePreset(prepared) : prepared;
    if (!result.ok || !result.snapshot) throw new Error(result.message || '预设已保存，但后端未确认');
    return result.snapshot;
  }
  return {
    read,
    reconcileExisting: (snapshot: AppearanceSnapshot) => serialized(async () => {
      const key = [snapshot.targetId, snapshot.revision, snapshot.backendReloadRequired, snapshot.packRepairRequired,
        snapshot.themes.map(row => String(row.backendEnabled)).join(',')].join(':');
      if (reconciled.has(key)) return {ok:false,snapshot,message:'本次配置异常已检查；保留状态，可手动重新应用'};
      if (!snapshot.steamRunning || !snapshot.backendReady || !snapshot.packOwned || snapshot.packError)
        return {ok:false,snapshot,message:'现有外观后台未满足自动核对条件'};
      if (reconciled.size >= 32) reconciled.clear();
      reconciled.add(key); // A failed/ambiguous write is never replayed automatically.
      return request('Reconcile', {expectedRevision:snapshot.revision,targetId:snapshot.targetId});
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
    reset: (mode: 'preset' | 'previous' = 'preset') => serialized(async () => {
      const current = await read();
      assertControlProtocol(current);
      // Restoring defaults is a file operation first. A missing/old native CSS
      // interface must not prevent copying the verified bundled preset files.
      // settlePreset reports saved-but-unconfirmed if live application fails.
      const result = await settlePreset(await request(mode === 'previous' ? 'Restore' : 'Reset'));
      if (!result.ok) return result;
      const label = mode === 'previous' ? '接管前的外观' : 'YMCC 默认预设';
      return { ...result, message: result.snapshot?.backendReloadRequired ? '已保存' + label + '，等待美化后端启动后生效' : '已恢复' + label };
    }),
    apply: (snapshot: AppearanceSnapshot, states: Partial<Record<AppearanceThemeName, boolean>>) => serialized(async () => {
      if (!validAppearanceSnapshot(snapshot)) throw new Error('请先读取外观状态');
      const names = Object.keys(states);
      if (!names.length || names.some(name => !STEAM_APPEARANCE_THEMES.some(theme => theme.name === name) || typeof states[name as AppearanceThemeName] !== 'boolean')) throw new Error('未知外观开关');
      const current = await read();
      if (snapshot.targetId && current.targetId !== snapshot.targetId) throw new Error('加载器主题目录已改变，请重新读取');
      if (current.revision !== snapshot.revision) throw new Error('外观配置已变化，请确认最新状态后重试');
      await useBackend(current);
      const firstTakeover = current.packPrepared === false && current.packOwned !== true && !current.packRepairRequired;
      // Persist the user's switches before a missing backend launch. A launch
      // failure must not make an otherwise valid preset impossible to turn on.
      let baseline = await prepare(current, false);
      if (baseline.backendReady && baseline.backendReloadRequired) baseline = await ensureReady(baseline);
      const requested = firstTakeover ? { ...Object.fromEntries(current.themes.map(theme => [theme.name, theme.configured])), ...states } : states;
      const result = await request('Apply', { expectedRevision: baseline.revision, states: requested });
      return result.ok ? settlePreset(result) : result;
    }),
    refresh: () => serialized(async () => {
      const current = await read();
      await useBackend(current);
      const snapshot = await prepare(current);
      if (!snapshot.steamRunning) return { ok: true, saved: true, snapshot, message: 'YMCC 预设已保存，启动 Steam 后生效' };
      await ensureReady(snapshot);
      return request('Refresh');
    }),
  };
}
export const steamAppearance = createSteamAppearance({
  idlePolicy: () => invoke<IdlePolicy>('steamAppearance.idlePolicy', {}, { timeoutMs: 3000 }),
  beginUse: () => invoke<UseTicket>('steamAppearance.beginUse', {}, { timeoutMs: 4000 }),
  ensureUse: token => invoke<{ ok: boolean }>('steamAppearance.ensureUse', { token }, { timeoutMs: 4000 }),
  endUse: token => invoke<UseRelease>('steamAppearance.endUse', { token }, { timeoutMs: 3000 }),
  location: settingsStore.location, exists: fs.exists, run: shell.run, hidden: shell.hidden,
  delay: ms => new Promise(resolve => setTimeout(resolve, ms)),
});