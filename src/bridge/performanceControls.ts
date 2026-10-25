import type { FrameRatePair, FrameRateSetting } from './frameRateModel';
/** Shared continuous controls. Legacy named tiers are migration inputs only. */
export type ControlSide = 'ac' | 'dc';
export type ControlMode = 'auto' | 'manual';
export type ControlCoreMode = 'bigFirst' | 'bigOnly' | 'littleFirst' | 'littleOnly';
export type SavingLevel = 'none' | 'small' | 'medium' | 'large' | 'aggressive';
export interface ContinuousSideControls {
  mode: ControlMode;
  tdp: number;
  tdpCeiling: number;
  auto: { bias: number; saving: SavingLevel; coreSplit: number; coreLow: ControlCoreMode; coreHigh: ControlCoreMode };
  manual: { frequencyMHz: number; unlimited: boolean; aggressiveness: number; turbo: boolean; coreMode: ControlCoreMode };
}
export interface ContinuousControls { version: 1; ac: ContinuousSideControls; dc: ContinuousSideControls }
export type ContinuousSidePatch = Partial<Omit<ContinuousSideControls, 'auto' | 'manual'>> & {
  auto?: Partial<ContinuousSideControls['auto']>;
  manual?: Partial<ContinuousSideControls['manual']>;
  hyperThreadMode?: 'default' | 'on' | 'off';
  frameRate?: FrameRateSetting;
};
export interface ContinuousPerformanceIdentity {
  scope: 'global' | 'dedicated'; followEffective?: boolean; side: ControlSide | null; actualSide?: ControlSide | null; exe: string; pid: number;
  processCreated: string; generation: number; revision: string;
}
export interface ContinuousPerformanceSnapshot {
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
export function defaultContinuousBank(side: ControlSide, template: PerformanceTemplate = 'portable'): ContinuousSideControls {
  const watts = side === 'dc' ? 30 : template === 'desktop' ? 120 : 55;
  return { mode: 'auto', tdp: watts, tdpCeiling: watts,
    auto: { bias: side === 'dc' ? 50 : template === 'desktop' ? 0 : 25, saving: side === 'dc' ? 'large' : 'medium', coreSplit: 15, coreLow: 'littleOnly', coreHigh: 'bigFirst' },
    manual: { frequencyMHz: 4000, unlimited: false, aggressiveness: 60, turbo: true, coreMode: 'bigFirst' } };
}
export function normalizeContinuousBank(value: unknown, side: ControlSide, fallback = defaultContinuousBank(side)): ContinuousSideControls {
  const r = record(value), a = record(r.auto), m = record(r.manual);
  const ceiling = snap(r.tdpCeiling, fallback.tdpCeiling, 1, APPLICATION_TDP_MAX);
  return { mode: r.mode === 'auto' || r.mode === 'manual' ? r.mode : fallback.mode,
    tdp: snap(r.tdp, fallback.tdp, 1, ceiling), tdpCeiling: ceiling,
    auto: { bias: snap(a.bias, fallback.auto.bias, 0, 100, 5),
      saving: SAVING_LEVELS.includes(a.saving) ? a.saving : fallback.auto.saving,
      coreSplit: snap(a.coreSplit, fallback.auto.coreSplit, 0, ceiling),
      coreLow: cores.includes(a.coreLow) ? a.coreLow : fallback.auto.coreLow,
      coreHigh: cores.includes(a.coreHigh) ? a.coreHigh : fallback.auto.coreHigh },
    manual: { frequencyMHz: snap(m.frequencyMHz, fallback.manual.frequencyMHz, 800, 7200, 100),
      unlimited: typeof m.unlimited === 'boolean' ? m.unlimited : fallback.manual.unlimited,
      aggressiveness: snap(m.aggressiveness, fallback.manual.aggressiveness, 0, 100),
      turbo: typeof m.turbo === 'boolean' ? m.turbo : fallback.manual.turbo,
      coreMode: cores.includes(m.coreMode) ? m.coreMode : fallback.manual.coreMode } };
}
export function normalizeContinuousControls(value: unknown, fallback?: ContinuousControls): ContinuousControls {
  const r = record(value); return { version: 1, ac: normalizeContinuousBank(r.ac, 'ac', fallback?.ac), dc: normalizeContinuousBank(r.dc, 'dc', fallback?.dc) };
}
/** A patch cannot change the inactive side, game identity, or unrelated settings. */
export function patchContinuousBank(bank: ContinuousSideControls, patch: ContinuousSidePatch, side: ControlSide): ContinuousSideControls {
  const r = record(patch);
  const allowed = ['mode', 'tdp', 'tdpCeiling', 'auto', 'manual', 'hyperThreadMode', 'frameRate'];
  if (Object.keys(r).some(k => !allowed.includes(k))) throw new Error('不支持的性能参数');
  for (const [obj, keys] of [[r, ['tdp', 'tdpCeiling']], [record(r.auto), ['bias', 'coreSplit']], [record(r.manual), ['frequencyMHz', 'aggressiveness']]] as const) {
    for (const key of keys) if (key in obj && (typeof obj[key] !== 'number' || !Number.isFinite(obj[key]))) throw new Error('性能参数必须为有效数值');
  }
  for (const key of ['turbo', 'unlimited']) if (r.manual?.[key] !== undefined && typeof r.manual[key] !== 'boolean') throw new Error('无效的 CPU 开关');
  if (r.frameRate && (!Number.isInteger(r.frameRate.fps) || (r.frameRate.fps !== 0 && (r.frameRate.fps < 15 || r.frameRate.fps > 300)))) throw new Error('帧率目标应为关闭锁帧或15–300 FPS');
  if (r.mode !== undefined && !['auto', 'manual'].includes(r.mode)) throw new Error('无效的调度模式');
  if (r.auto?.saving !== undefined && !SAVING_LEVELS.includes(r.auto.saving)) throw new Error('无效的浮动策略');
  for (const v of [r.auto?.coreLow, r.auto?.coreHigh, r.manual?.coreMode]) if (v !== undefined && !cores.includes(v)) throw new Error('无效的核心方案');
  return normalizeContinuousBank({ ...bank, ...r, auto: { ...bank.auto, ...r.auto }, manual: { ...bank.manual, ...r.manual } }, side, bank);
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
export type ContinuousFloatAction = 'hold' | 'observe' | 'probe-cpu' | 'probe-tdp' | 'restore-cpu' | 'restore-tdp' | 'rollback' | 'ceiling';
interface FloatTrial {
  at: number; axis: 'cpu' | 'tdp'; watts: number; frequencyMHz: number; aggressiveness: number;
  fps: number; low: number | null; needsGain: boolean;
}
export interface ContinuousFloatState {
  watts: number; frequencyMHz: number; aggressiveness: number; stable: number; lastDown: number; recoverUntil: number;
  observedAt?: number; gpuObservedAt?: number; gpuBusy?: boolean; trial?: FloatTrial | null;
  probeMHz?: number; cpuBlockedUntil?: number; tdpBlockedUntil?: number; attempts?: number; action?: ContinuousFloatAction;
}
export interface ContinuousFloatSample {
  now: number; fps: number | null; low: number | null; target: number; minimum: number;
  cpuBusy: number | null; gpuBusy?: number | null; observedAt?: number; gpuObservedAt?: number;
  lowObservedAt?: number; cpuClockMHz?: number | null;
}
const percent = (v: unknown): v is number => typeof v === 'number' && Number.isFinite(v) && v >= 0 && v <= 100;
/** One fresh FPS observation, one axis, one receipt. Unknown sensors never become idle. */
export function stepContinuousFloat(bank: ContinuousSideControls, state: ContinuousFloatState, sample: ContinuousFloatSample): ContinuousFloatState {
  const cpu = automaticCpuControls(bank), rule = SAVING_RULES[bank.auto.saving], now = sample.now;
  // Bias affects only the internal experiment size; the user policy above stays unchanged.
  const probeLimitMHz = 100 + Math.round(bank.auto.bias / 50) * 100;
  const next: ContinuousFloatState = { ...state, action: 'hold' };
  if (bank.auto.saving === 'none') return { ...next, stable: 0 };
  if (!(sample.target > 0) || !(sample.fps! > 0) || !Number.isFinite(sample.fps)) return { ...next, stable: 0 };
  const at = sample.observedAt ?? now;
  if (!Number.isFinite(at) || at > now || now - at >= 6000 || at <= (state.observedAt ?? -1)) return next;
  next.observedAt = at;
  const gpuKnown = percent(sample.gpuBusy), cpuKnown = percent(sample.cpuBusy);
  if (!gpuKnown) { next.gpuBusy = false; next.gpuObservedAt = undefined; }
  else if ((sample.gpuObservedAt ?? at) !== state.gpuObservedAt) {
    next.gpuObservedAt = sample.gpuObservedAt ?? at;
    next.gpuBusy = sample.gpuBusy! >= 95 || (!!state.gpuBusy && sample.gpuBusy! >= 90);
  }
  const fps = sample.fps!, low = sample.low! > 0 && Number.isFinite(sample.low) ? sample.low : null;
  const bad = fps < sample.target - Math.max(2, sample.target * .05);
  const holdMs = 5000, trial = state.trial;
  const undo = (): ContinuousFloatState => ({ ...next, watts: trial!.watts, frequencyMHz: trial!.frequencyMHz,
    aggressiveness: trial!.aggressiveness, trial: null, stable: 0, probeMHz: 100,
    cpuBlockedUntil: trial!.axis === 'cpu' ? now + 30000 : state.cpuBlockedUntil,
    tdpBlockedUntil: trial!.axis === 'tdp' ? now + 30000 : state.tdpBlockedUntil,
    recoverUntil: now + holdMs, action: 'rollback' });
  if (trial) {
    if (fps < trial.fps - Math.max(2, trial.fps * .03)) return undo();
    if (now - trial.at < holdMs) return { ...next, action: 'observe' };
    // HWiNFO Low is its own statistic, not a fabricated FPS sample/window.
    if (low === null || (sample.lowObservedAt ?? at) <= trial.at) {
      return now - trial.at >= 10000 ? undo() : { ...next, action: 'observe' };
    }
    if ((!trial.needsGain && bad) || (trial.low !== null && low < trial.low * .85) ||
        (trial.needsGain && fps < trial.fps + Math.max(1, trial.fps * .02))) return undo();
    return { ...next, trial: null, stable: 0, probeMHz: Math.min(probeLimitMHz, (state.probeMHz ?? 100) + 100), recoverUntil: now + 2000 };
  }
  const probe = (axis: 'cpu' | 'tdp', values: Partial<ContinuousFloatState>, needsGain = false): ContinuousFloatState => ({ ...next, ...values,
    trial: { at: now, axis, watts: state.watts, frequencyMHz: state.frequencyMHz, aggressiveness: state.aggressiveness, fps, low, needsGain },
    stable: 0, lastDown: now, attempts: bad ? (state.attempts ?? 0) + 1 : 0, action: axis === 'cpu' ? 'probe-cpu' : 'probe-tdp' });
  const probeCpu = (needsGain = false): ContinuousFloatState => {
    const clock = sample.cpuClockMHz;
    // A measured current CPU clock anchors an experiment, never an invented hardware maximum.
    const anchor = state.frequencyMHz > 0 ? state.frequencyMHz :
      typeof clock === 'number' && Number.isFinite(clock) && clock >= 1400 && clock <= 10000 ? Math.round(clock / 100) * 100 : 0;
    if (anchor > cpu.minMHz) return probe('cpu', { frequencyMHz: Math.max(cpu.minMHz, anchor - Math.min(probeLimitMHz, state.probeMHz ?? 100)) }, needsGain);
    if (state.aggressiveness > 30) return probe('cpu', { aggressiveness: state.aggressiveness - 5 }, needsGain);
    return next;
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
  if (now >= (state.cpuBlockedUntil ?? 0) && sample.cpuBusy! < 75 && (next.gpuBusy || bank.auto.bias >= 50)) {
    const attempt = probeCpu(); if (attempt.trial) return attempt;
  }
  const floor = Math.max(sample.minimum, Math.floor(bank.tdp * (1 - rule.ratio)));
  if (state.watts > floor && now >= (state.tdpBlockedUntil ?? 0)) return probe('tdp', { watts: Math.max(floor, state.watts - Math.max(1, Math.ceil(bank.tdp * rule.stepPct))) });
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
