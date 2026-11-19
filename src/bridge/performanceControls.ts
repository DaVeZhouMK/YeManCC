import type { FrameRatePair, FrameRateSetting } from './frameRateModel';
/** Shared continuous controls. Legacy named tiers are migration inputs only. */
export type ControlSide = 'ac' | 'dc';
export type ControlMode = 'auto' | 'manual';
export type ManualPowerScheme = 'yeman' | 'besteff' | 'bal' | 'bestperf';
export const CONTROL_POWER_SCHEMES = [{ value: 'yeman', label: '野蛮' }, { value: 'besteff', label: '节能' }, { value: 'bal', label: '平滑' }, { value: 'bestperf', label: '高性能' }] as const;
export type ControlCoreMode = 'bigFirst' | 'bigOnly' | 'littleFirst' | 'littleOnly';
/** ON releases YMCC's per-game restriction; it never enables SMT in firmware. */
export function normalizeHyperThreadMode(value: unknown): 'default' | 'off' {
  return value === 'off' ? 'off' : 'default';
}
export type SavingLevel = 'none' | 'small' | 'medium' | 'large' | 'aggressive';
export interface ContinuousSideControls {
  mode: ControlMode;
  tdp: number;
  /** Repaired display data is not a valid hardware target until explicitly corrected. */
  tdpValid?: false;
  cpuValid?: false;
  tdpCeiling: number;
  auto: { bias: number; saving: SavingLevel; coreSplit: number; coreLow: ControlCoreMode; coreHigh: ControlCoreMode };
  manual: { frequencyMHz: number; unlimited: boolean; aggressiveness: number; turbo: boolean; coreMode: ControlCoreMode };
}
export type ControlPowerSchemes = Record<ControlSide, ManualPowerScheme>;
export function normalizeControlPowerSchemes(value: unknown): ControlPowerSchemes {
  const raw = value && typeof value === 'object' ? value as Partial<ControlPowerSchemes> : {};
  const pick = (side: ControlSide): ManualPowerScheme => CONTROL_POWER_SCHEMES.some(option => option.value === raw[side]) ? raw[side]! : 'yeman';
  return { ac: pick('ac'), dc: pick('dc') };
}
/** Same intent key for the sole writer's completion and its resident observer. */
export function performanceConsumptionSignature(owner: unknown[], identity: string, side: ControlSide,
  frameDemand: number | boolean, bank: unknown, powerScheme: ManualPowerScheme): string {
  return JSON.stringify([owner, identity, side, frameDemand, bank, powerScheme], (key, value) => key === 'fpsTarget' ? undefined : value);
}
export interface ContinuousControls { version: 1; ac: ContinuousSideControls; dc: ContinuousSideControls }
export type ContinuousSidePatch = Partial<Omit<ContinuousSideControls, 'auto' | 'manual'>> & {
  auto?: Partial<ContinuousSideControls['auto']>;
  manual?: Partial<ContinuousSideControls['manual']>;
  powerScheme?: ManualPowerScheme;
  hyperThreadMode?: 'default' | 'on' | 'off';
  frameRate?: FrameRateSetting;
};
export interface ContinuousPerformanceIdentity {
  scope: 'global' | 'dedicated'; followEffective?: boolean; side: ControlSide | null; actualSide?: ControlSide | null; exe: string; pid: number;
  processCreated: string; generation: number; revision: string;
}
export interface ContinuousPerformanceSnapshot {
  /** Global remembered choice for the edited power side; never EXE-owned. */
  powerScheme: ManualPowerScheme;
  /** Present only for a frame-rate edit; actual original RTSS apply receipt. */
  frameRateApplied?: boolean;
  /** Receipt from this invocation of the original TDP writer; never inferred from saved watts. */
  tdpWriteReceipt?: { requested: number; accepted: number | null; ok: boolean; proof: 'write-ack' | 'unconfirmed' };
  gameName?: string;
  frameRates: FrameRatePair;
  identity: ContinuousPerformanceIdentity; actualSide?: ControlSide | null; coreGroups?: { label: string; physical: number; logical: number }[]; controls: ContinuousControls; side: ControlSide | null;
  bank: ContinuousSideControls | null; hardwareMinimum: number; hardwareMaximum: number;
  hybrid: boolean; smtAvailable: boolean; hyperThreadMode: 'default' | 'on' | 'off'; configured: boolean;
}
/** Display only existing intent/receipts; never turn saved numbers into hardware proof. */
export function continuousPerformanceSaveNotice(snapshot: Pick<ContinuousPerformanceSnapshot, 'tdpWriteReceipt'>): string {
  const receipt = snapshot.tdpWriteReceipt;
  if (!receipt) return '设置已保存';
  const accepted = receipt.accepted;
  return receipt.ok && receipt.proof === 'write-ack' && typeof accepted === 'number' && Number.isFinite(accepted) && accepted >= 2 && accepted <= receipt.requested
    ? `设置已保存；本次 TDP 写入回执 ${accepted} W`
    : '设置已保存；本次 TDP 写入未确认';
}
export function continuousPerformanceDataNotice(snapshot: Pick<ContinuousPerformanceSnapshot, 'configured' | 'side' | 'actualSide' | 'bank'>,
  runtime: { mode: string | null; watts: number | null } | null): string {
  if (!snapshot.configured) return 'TDP 尚未配置，当前显示参考参数';
  const bank = snapshot.bank;
  if (!bank) return '正在确认性能配置';
  const parts: string[] = [];
  if (bank.tdpValid === false) parts.push(`TDP 源数据异常，按界面目标 ${bank.tdp} W 慢速校对；自动浮动暂停`);
  else {
    parts.push(`TDP 目标 ${bank.tdp} W`);
    if (snapshot.actualSide && snapshot.side !== snapshot.actualSide) parts.push(`当前供电为 ${snapshot.actualSide.toUpperCase()}，此侧参数仅保存`);
    else if (snapshot.side && runtime?.mode === snapshot.side && typeof runtime.watts === 'number' && Number.isFinite(runtime.watts) && runtime.watts >= 2)
      parts.push(`当前写入回执 ${runtime.watts} W`);
    else parts.push('当前写入未确认');
  }
  if (bank.cpuValid === false) parts.push(bank.mode === 'auto' ? 'CPU 参数未确认，保留原设置；自动浮动暂停' : 'CPU 参数未确认，保留原设置');
  return parts.join('；');
}
export const CONTROL_CORE_OPTIONS = [
  { value: 'bigFirst', label: '大核为主' }, { value: 'bigOnly', label: '仅大核' },
  { value: 'littleFirst', label: '小核为主' }, { value: 'littleOnly', label: '仅小核' },
] as const;
export const SAVING_LEVELS = ['none', 'small', 'medium', 'large', 'aggressive'] as const;
export const SAVING_RULES = {
  none: { label: '无浮动', ratio: 0, intervalMs: 0, stepPct: 0, depth: 0, minState: 100 },
  small: { label: '小幅', ratio: .2, intervalMs: 8000, stepPct: .03, depth: 1, minState: 30 },
  medium: { label: '中幅', ratio: .3, intervalMs: 5000, stepPct: .05, depth: 2, minState: 20 },
  large: { label: '大幅', ratio: .4, intervalMs: 3000, stepPct: .08, depth: 3, minState: 10 },
  aggressive: { label: '激进', ratio: .5, intervalMs: 2000, stepPct: .12, depth: 4, minState: 0 },
} as const;
export const APPLICATION_TDP_MAX = 150;
const cores = CONTROL_CORE_OPTIONS.map(o => o.value) as readonly string[];
const finite = (v: unknown, fallback: number) => typeof v === 'number' && Number.isFinite(v) ? v : fallback;
const snap = (v: unknown, fallback: number, lo: number, hi: number, step = 1) => Math.max(lo, Math.min(hi, Math.round(finite(v, fallback) / step) * step));
const record = (v: unknown): Record<string, any> => v !== null && typeof v === 'object' && !Array.isArray(v) ? v as Record<string, any> : {};
export type PerformanceTemplate = 'portable' | 'desktop';
/** Exists only during invalid data; consumers reuse their original scheduler. */
export interface InvalidTdpRecovery { nextCheck: number; nextWrite: number; failures: number }
export function beginInvalidTdpRecovery(now: number): InvalidTdpRecovery {
  return { nextCheck: now + 6000, nextWrite: now + 6000, failures: 0 };
}
export function finishInvalidTdpRecovery(state: InvalidTdpRecovery, now: number, accepted: boolean): void {
  if (accepted) {
    state.failures = 0; state.nextCheck = now + 60000; state.nextWrite = now + 120000;
  } else {
    const delay = state.failures === 0 ? 30000 : state.failures === 1 ? 60000 : 120000;
    state.failures = Math.min(2, state.failures + 1); state.nextCheck = state.nextWrite = now + delay;
  }
}
export function defaultContinuousBank(side: ControlSide, template: PerformanceTemplate = 'portable'): ContinuousSideControls {
  const watts = side === 'dc' ? 30 : template === 'desktop' ? 120 : 55;
  return { mode: 'auto', tdp: watts, tdpCeiling: watts,
    auto: { bias: side === 'dc' ? 50 : template === 'desktop' ? 0 : 25, saving: side === 'dc' ? 'large' : 'medium', coreSplit: 15, coreLow: 'littleOnly', coreHigh: 'bigFirst' },
    manual: { frequencyMHz: 4000, unlimited: false, aggressiveness: 60, turbo: true, coreMode: 'bigFirst' } };
}
export function normalizeContinuousBank(value: unknown, side: ControlSide, fallback = defaultContinuousBank(side)): ContinuousSideControls {
  const r = record(value), a = record(r.auto), m = record(r.manual);
  const ceilingValid = r.tdpCeiling === undefined || typeof r.tdpCeiling === 'number' && Number.isFinite(r.tdpCeiling) && r.tdpCeiling >= 2;
  const targetValid = value === undefined || typeof r.tdp === 'number' && Number.isFinite(r.tdp) && r.tdp >= 2;
  const invalid = r.tdpValid === false || !targetValid || !ceilingValid;
  const mode = r.mode === 'auto' || r.mode === 'manual' ? r.mode : fallback.mode;
  const validCpu = value === undefined || (mode === 'auto'
    ? typeof a.bias === 'number' && Number.isFinite(a.bias) && a.bias >= 0 && a.bias <= 100
    : (m.unlimited === true || typeof m.frequencyMHz === 'number' && Number.isFinite(m.frequencyMHz) && m.frequencyMHz >= 800 && m.frequencyMHz <= 7200));
  const ceiling = snap(ceilingValid ? r.tdpCeiling : undefined, fallback.tdpCeiling, 2, APPLICATION_TDP_MAX);
  return { ...(invalid ? { tdpValid: false as const } : {}), ...(!validCpu || r.cpuValid === false ? { cpuValid: false as const } : {}), mode,
    tdp: snap(targetValid ? r.tdp : undefined, fallback.tdp, 2, ceiling), tdpCeiling: ceiling,
    auto: { bias: Math.max(0, Math.min(100, Math.ceil(finite(a.bias, fallback.auto.bias) / 5) * 5)),
      saving: SAVING_LEVELS.includes(a.saving) ? a.saving : fallback.auto.saving,
      coreSplit: snap(a.coreSplit, fallback.auto.coreSplit, 0, ceiling),
      coreLow: cores.includes(a.coreLow) ? a.coreLow : fallback.auto.coreLow,
      coreHigh: cores.includes(a.coreHigh) ? a.coreHigh : fallback.auto.coreHigh },
    manual: { frequencyMHz: Math.max(800, Math.min(7200, Math.floor(finite(m.frequencyMHz, fallback.manual.frequencyMHz) / 100) * 100)),
      unlimited: typeof m.unlimited === 'boolean' ? m.unlimited : fallback.manual.unlimited,
      aggressiveness: snap(m.aggressiveness, fallback.manual.aggressiveness, 0, 100),
      turbo: typeof m.turbo === 'boolean' ? m.turbo : fallback.manual.turbo,
      coreMode: cores.includes(m.coreMode) ? m.coreMode : fallback.manual.coreMode } };
}
export function normalizeContinuousControls(value: unknown, fallback?: ContinuousControls): ContinuousControls {
  const r = record(value), supplied = Object.keys(r).length > 0;
  return { version: 1, ac: normalizeContinuousBank(r.ac ?? (supplied ? {} : undefined), 'ac', fallback?.ac), dc: normalizeContinuousBank(r.dc ?? (supplied ? {} : undefined), 'dc', fallback?.dc) };
}
/** A patch cannot change the inactive side, game identity, or unrelated settings. */
export function patchContinuousBank(bank: ContinuousSideControls, patch: ContinuousSidePatch, side: ControlSide, powerScheme: ManualPowerScheme = 'yeman'): ContinuousSideControls {
  const r = record(patch);
  const allowed = ['mode', 'tdp', 'tdpCeiling', 'auto', 'manual', 'hyperThreadMode', 'frameRate', 'powerScheme'];
  if (Object.keys(r).some(k => !allowed.includes(k))) throw new Error('不支持的性能参数');
  for (const [obj, keys] of [[r, ['tdp', 'tdpCeiling']], [record(r.auto), ['bias', 'coreSplit']], [record(r.manual), ['frequencyMHz', 'aggressiveness']]] as const) {
    for (const key of keys) if (key in obj && (typeof obj[key] !== 'number' || !Number.isFinite(obj[key]))) throw new Error('性能参数必须为有效数值');
  }
  for (const key of ['tdp', 'tdpCeiling']) if (key in r && r[key] < 2) throw new Error('TDP 参数不能低于 2W；错误值不会钳成最低瓦数');
  if (r.manual?.frequencyMHz !== undefined && (!Number.isInteger(r.manual.frequencyMHz) || r.manual.frequencyMHz < 800 || r.manual.frequencyMHz > 7200 || r.manual.frequencyMHz % 100 !== 0))
    throw new Error('CPU 频率必须为 800–7200 MHz 的 100 MHz 档位');
  if (r.auto?.bias !== undefined && (r.auto.bias < 0 || r.auto.bias > 100 || r.auto.bias % 5 !== 0)) throw new Error('CPU 倾向必须为 0–100 的 5% 档位');
  for (const key of ['turbo', 'unlimited']) if (r.manual?.[key] !== undefined && typeof r.manual[key] !== 'boolean') throw new Error('无效的 CPU 开关');
  if (r.frameRate && (!Number.isInteger(r.frameRate.fps) || (r.frameRate.fps !== 0 && (r.frameRate.fps < 15 || r.frameRate.fps > 300)))) throw new Error('帧率目标应为关闭锁帧或15–300 FPS');
  if (r.mode !== undefined && !['auto', 'manual'].includes(r.mode)) throw new Error('无效的调度模式');
  if (r.powerScheme !== undefined && !CONTROL_POWER_SCHEMES.some(option => option.value === r.powerScheme)) throw new Error('无效的电源方案');
  const mode = r.mode ?? bank.mode, scheme = r.powerScheme ?? powerScheme;
  if (mode === 'manual' && scheme !== 'yeman' && (r.auto !== undefined || Object.keys(record(r.manual)).length > 0))
    throw new Error('此电源方案由 Windows 管理 CPU，仅可调节 TDP 和帧率');
  if (r.auto?.saving !== undefined && !SAVING_LEVELS.includes(r.auto.saving)) throw new Error('无效的浮动策略');
  for (const v of [r.auto?.coreLow, r.auto?.coreHigh, r.manual?.coreMode]) if (v !== undefined && !cores.includes(v)) throw new Error('无效的核心方案');
  // The ceiling dropdown is an explicit wattage choice, just like the slider.
  // An invalid bank only contains a display fallback: use the selected ceiling
  // as its new target rather than keeping that fallback or its invalid latch.
  // For a valid bank, changing the ceiling still preserves a lower target.
  const repairTarget = !('tdp' in r) && 'tdpCeiling' in r && bank.tdpValid === false;
  return normalizeContinuousBank({ ...bank, ...r, ...(repairTarget ? { tdp: r.tdpCeiling } : {}),
    ...('tdp' in r || 'tdpCeiling' in r ? { tdpValid: undefined } : {}),
    ...(r.mode !== undefined || r.auto?.bias !== undefined || r.manual?.frequencyMHz !== undefined || r.manual?.unlimited !== undefined ? { cpuValid: undefined } : {}),
    auto: { ...bank.auto, ...r.auto }, manual: { ...bank.manual, ...r.manual } }, side, bank);
}
/** The user's bias directly sets the CPU policy, including with no FPS/GPU data or floating disabled. */
export function automaticCpuControls(bank: ContinuousSideControls) {
  const { bias, saving } = bank.auto, release = (100 - bias) / 100, rule = SAVING_RULES[saving];
  // Preserve the established slider mapping; 0 explicitly means no frequency cap.
  const max = Math.max(1200, Math.round((.8 + 4.2 * release) * 10) * 100);
  const base = Math.max(1200, Math.round((.8 + 2.7 * release) * 10) * 100);
  return { minMHz: Math.max(1200, Math.round((max - (max - base) * rule.depth / 3) / 100) * 100),
    maxMHz: bias === 0 ? 0 : max, aggressiveness: Math.round(10 + 90 * release),
    minState: rule.minState, turbo: bias < 70 && (bias === 0 || max > 2000),
    targetTdp: Math.max(1, Math.floor(bank.tdp * (1 - rule.ratio))) };
}
export function manualCpuMinimum(bank: ContinuousSideControls): number {
  return bank.manual.unlimited ? 100 : Math.min(100, Math.max(0, Math.round(bank.manual.frequencyMHz / 50)));
}
export function continuousCoreAt(bank: ContinuousSideControls, watts: number): ControlCoreMode {
  return bank.mode === 'manual' ? bank.manual.coreMode : watts <= bank.auto.coreSplit ? bank.auto.coreLow : bank.auto.coreHigh;
}
export const toPowerCoreMode = (core: ControlCoreMode): 'big' | 'only-big' | 'small' | 'only-small' => ({ bigFirst: 'big', bigOnly: 'only-big', littleFirst: 'small', littleOnly: 'only-small' } as const)[core];
export type ContinuousFloatAction = 'hold' | 'observe' | 'probe-cpu' | 'probe-tdp' | 'probe-both' | 'restore-cpu' | 'restore-tdp' | 'rollback' | 'ceiling';
interface FloatTrial {
  at: number; axis: 'cpu' | 'tdp' | 'both'; watts: number; frequencyMHz: number; aggressiveness: number;
  fps: number; low: number | null; needsGain: boolean;
}
export interface ContinuousFloatState {
  watts: number; frequencyMHz: number; aggressiveness: number; stable: number; lastDown: number; recoverUntil: number;
  observedAt?: number; gpuObservedAt?: number; gpuBusy?: boolean; trial?: FloatTrial | null;
  savingBackoff?: boolean;
  probeMHz?: number; cpuBlockedUntil?: number; tdpBlockedUntil?: number; attempts?: number; action?: ContinuousFloatAction;
}
export interface ContinuousFloatSample {
  now: number; fps: number | null; low: number | null; target: number; minimum: number;
  cpuBusy: number | null; gpuBusy?: number | null; observedAt?: number; gpuObservedAt?: number;
  lowObservedAt?: number; cpuClockMHz?: number | null;
}
const percent = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v) && v >= 0 && v <= 100;
/** Fresh observations and acknowledged trials. Unknown sensors never become idle. */
export function stepContinuousFloat(bank: ContinuousSideControls, state: ContinuousFloatState, sample: ContinuousFloatSample): ContinuousFloatState {
  const cpu = automaticCpuControls(bank), rule = SAVING_RULES[bank.auto.saving], now = sample.now;
  // A previous trial/receipt never grants a higher limit than today's selection.
  // Zero is unlimited only when the selected policy explicitly allows it.
  const boundedFrequency = (value: number) => !Number.isFinite(value) || value < 0 ? cpu.maxMHz :
    cpu.maxMHz > 0 && (value === 0 || value > cpu.maxMHz) ? cpu.maxMHz : value;
  const boundedAggression = (value: number) => !Number.isFinite(value) || value < 0 ? cpu.aggressiveness : Math.min(value, cpu.aggressiveness);
  if (boundedFrequency(state.frequencyMHz) !== state.frequencyMHz || boundedAggression(state.aggressiveness) !== state.aggressiveness ||
      state.trial && (boundedFrequency(state.trial.frequencyMHz) !== state.trial.frequencyMHz || boundedAggression(state.trial.aggressiveness) !== state.trial.aggressiveness))
    return { ...state, frequencyMHz: boundedFrequency(state.frequencyMHz), aggressiveness: boundedAggression(state.aggressiveness),
      trial: null, stable: 0, recoverUntil: now + 5000, action: 'restore-cpu' };
  // Bias affects only the internal experiment size; the user policy above stays unchanged.
  const probeLimitMHz = 100 + Math.round(bank.auto.bias / 50) * 100;
  const next: ContinuousFloatState = { ...state, action: 'hold' };
  if (bank.auto.saving === 'none') return { ...next, stable: 0 };
  const trial = state.trial, fastSavings = bank.auto.saving === 'aggressive' && !state.savingBackoff;
  const holdMs = fastSavings && !trial?.needsGain ? 2000 : 5000;
  const savingFailure = trial && bank.auto.saving === 'aggressive' && !trial.needsGain;
  const blockedMs = savingFailure ? 5000 : 30000;
  const undo = (): ContinuousFloatState => ({ ...next, watts: trial!.watts, frequencyMHz: trial!.frequencyMHz,
    aggressiveness: trial!.aggressiveness, trial: null, stable: 0, probeMHz: 100,
    savingBackoff: state.savingBackoff || !!savingFailure,
    cpuBlockedUntil: trial!.axis !== 'tdp' ? now + blockedMs : state.cpuBlockedUntil,
    tdpBlockedUntil: trial!.axis !== 'cpu' ? now + blockedMs : state.tdpBlockedUntil,
    recoverUntil: now + 5000, action: 'rollback' });
  const floor = Math.max(sample.minimum, Math.floor(bank.tdp * (1 - rule.ratio)));
  // A transient accepted value cannot redefine the saving floor. Roll back
  // that experiment even if the FPS source disappeared after the write.
  if (trial && trial.axis !== 'cpu' && state.watts < floor) return undo();
  const validFps = sample.target > 0 && sample.fps! > 0 && Number.isFinite(sample.fps);
  const at = sample.observedAt ?? now;
  const fresh = Number.isFinite(at) && at <= now && now - at < 6000 && at > (state.observedAt ?? -1);
  if (!validFps || !fresh) {
    if (trial && now - trial.at >= 10000) return undo();
    return validFps ? next : { ...next, stable: 0 };
  }
  next.observedAt = at;
  const gpuKnown = percent(sample.gpuBusy), cpuKnown = percent(sample.cpuBusy);
  if (!gpuKnown) { next.gpuBusy = false; next.gpuObservedAt = undefined; }
  else if ((sample.gpuObservedAt ?? at) !== state.gpuObservedAt) {
    next.gpuObservedAt = sample.gpuObservedAt ?? at;
    next.gpuBusy = sample.gpuBusy! >= 95 || (!!state.gpuBusy && sample.gpuBusy! >= 90);
  }
  const fps = sample.fps!, low = sample.low! > 0 && Number.isFinite(sample.low) ? sample.low : null;
  const bad = fps < sample.target - Math.max(2, sample.target * .05);
  if (trial) {
    if (fps < trial.fps - Math.max(2, trial.fps * .03)) return undo();
    if (now - trial.at < holdMs) return { ...next, action: 'observe' };
    // HWiNFO Low is its own statistic, not a fabricated FPS sample/window.
    if (low === null || (sample.lowObservedAt ?? at) <= trial.at) {
      return now - trial.at >= 10000 ? undo() : { ...next, action: 'observe' };
    }
    if ((!trial.needsGain && bad) || (trial.low !== null && low < trial.low * .85) ||
        (trial.needsGain && fps < trial.fps + Math.max(1, trial.fps * .02))) return undo();
    return { ...next, trial: null, stable: fastSavings && !trial.needsGain ? 1 : 0,
      probeMHz: trial.axis !== 'tdp' ? Math.min(probeLimitMHz, (state.probeMHz ?? 100) + 100) : state.probeMHz,
      recoverUntil: fastSavings && !trial.needsGain ? now : now + 2000 };
  }
  const probe = (axis: 'cpu' | 'tdp' | 'both', values: Partial<ContinuousFloatState>, needsGain = false): ContinuousFloatState => ({ ...next, ...values,
    trial: { at: now, axis, watts: state.watts, frequencyMHz: state.frequencyMHz, aggressiveness: state.aggressiveness, fps, low, needsGain },
    stable: 0, lastDown: now, attempts: bad ? (state.attempts ?? 0) + 1 : 0,
    action: axis === 'both' ? 'probe-both' : axis === 'cpu' ? 'probe-cpu' : 'probe-tdp' });
  const probeCpu = (needsGain = false, watts?: number): ContinuousFloatState => {
    const clock = sample.cpuClockMHz;
    // A measured current CPU clock anchors an experiment, never an invented hardware maximum.
    const anchor = state.frequencyMHz > 0 ? state.frequencyMHz :
      typeof clock === 'number' && Number.isFinite(clock) && clock >= 1400 && clock <= 10000 ? Math.round(clock / 100) * 100 : 0;
    const axis = watts === undefined ? 'cpu' : 'both', values = watts === undefined ? {} : { watts };
    if (anchor > cpu.minMHz) return probe(axis, { ...values,
      frequencyMHz: Math.max(cpu.minMHz, anchor - Math.min(probeLimitMHz, state.savingBackoff ? 100 : state.probeMHz ?? 100)) }, needsGain);
    if (state.aggressiveness > 30) return probe(axis, { ...values, aggressiveness: state.aggressiveness - 5 }, needsGain);
    return watts === undefined ? next : probe('tdp', { watts });
  };
  if (bad) {
    next.stable = 0;
    // Recover reduced total budget before testing a different CPU/GPU allocation.
    if (state.watts < bank.tdp) return { ...next, watts: bank.tdp, recoverUntil: now + holdMs, action: 'restore-tdp' };
    // Low FPS may recover an automatic reduction only up to the user's chosen policy.
    const constrained = (cpu.maxMHz === 0 ? state.frequencyMHz !== 0 : state.frequencyMHz > 0 && state.frequencyMHz < cpu.maxMHz) ||
      state.aggressiveness < cpu.aggressiveness;
    if (constrained && (!next.gpuBusy && (cpuKnown && sample.cpuBusy! >= 80 || !gpuKnown || !cpuKnown)))
      return { ...next, frequencyMHz: cpu.maxMHz, aggressiveness: cpu.aggressiveness, recoverUntil: now + holdMs, action: 'restore-cpu' };
    if (now < state.recoverUntil) return next;
    if (gpuKnown && next.gpuBusy && cpuKnown && sample.cpuBusy! < 75 &&
        now >= (state.cpuBlockedUntil ?? 0) && (state.attempts ?? 0) < 2) return probeCpu(true);
    if (constrained && cpuKnown && sample.cpuBusy! >= 80 && now >= (state.cpuBlockedUntil ?? 0) && (state.attempts ?? 0) < 2)
      return probe('cpu', state.frequencyMHz > 0 && (cpu.maxMHz === 0 || state.frequencyMHz < cpu.maxMHz) ?
        { frequencyMHz: cpu.maxMHz === 0 ? state.frequencyMHz + 100 : Math.min(cpu.maxMHz, state.frequencyMHz + 100) } :
        { aggressiveness: Math.min(cpu.aggressiveness, state.aggressiveness + 5) }, true);
    return { ...next, action: 'ceiling' };
  }
  next.attempts = 0;
  // Missing CPU/Low and a trailing bad Low never authorize more savings.
  if (!cpuKnown || low === null || low < sample.target * .8 || sample.cpuBusy! >= 80)
    return { ...next, stable: 0 };
  next.stable = state.stable + 1;
  if (next.stable < 2 || now < state.recoverUntil || now - state.lastDown < Math.max(holdMs, rule.intervalMs)) return next;
  // Aggressive savings lower both eligible axes in one acknowledged trial.
  // A failed trial restores both and keeps this owner on smaller, slower steps.
  const tdpAvailable = state.watts > floor && now >= (state.tdpBlockedUntil ?? 0);
  const lowerTdp = Math.max(floor, state.watts - Math.max(1, Math.ceil(bank.tdp * rule.stepPct /
    (bank.auto.saving === 'aggressive' && state.savingBackoff ? 2 : 1))));
  if (bank.auto.saving === 'aggressive' && tdpAvailable) {
    if (now >= (state.cpuBlockedUntil ?? 0) && sample.cpuBusy! < 75) return probeCpu(false, lowerTdp);
    return probe('tdp', { watts: lowerTdp });
  }
  if (now >= (state.cpuBlockedUntil ?? 0) && sample.cpuBusy! < 75 && (next.gpuBusy || bank.auto.bias >= 50)) {
    const attempt = probeCpu(); if (attempt.trial) return attempt;
  }
  if (tdpAvailable) return probe('tdp', { watts: lowerTdp });
  if (now >= (state.cpuBlockedUntil ?? 0) && sample.cpuBusy! < 75) return probeCpu();
  return next;
}

export interface ContinuousCoreState { mode: ControlCoreMode; changedAt: number; pending?: ControlCoreMode; pendingAt?: number }
/** +/-2W hysteresis, 3s confirmation and 10s dwell; only called from the existing float tick. */
export function stepContinuousCore(bank: ContinuousSideControls, state: ContinuousCoreState, watts: number, now: number): ContinuousCoreState {
  if (!Number.isFinite(watts) || watts <= 0) return { ...state, pending: undefined, pendingAt: undefined };
  const low = state.mode === bank.auto.coreLow;
  const mode = low ? watts >= bank.auto.coreSplit + 2 ? bank.auto.coreHigh : state.mode :
    watts <= bank.auto.coreSplit - 2 ? bank.auto.coreLow : state.mode;
  if (mode === state.mode) return { ...state, pending: undefined, pendingAt: undefined };
  if (state.pending !== mode) return { ...state, pending: mode, pendingAt: now };
  return now - state.changedAt >= 10000 && now - (state.pendingAt ?? now) >= 3000 ? { mode, changedAt: now } : state;
}
