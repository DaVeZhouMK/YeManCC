/** Candidate shared axis-calibration contract. Preparation only; no installed command is claimed. */
export const AXIS_CALIBRATION_PROTOCOL = 'ymcc.axis-calibration.v1' as const;
export type StickSide = 'left' | 'right';
export type TriggerSide = 'lt' | 'rt';
export type StickOutputShape = 'original' | 'circle-to-square' | 'square-to-circle';
export interface StickCalibration {
  enabled: boolean;
  centerX: number; centerY: number;
  /** Missing in older profiles means original; conversion direction is explicit. */
  outputShape?: StickOutputShape;
  innerDeadzonePct: number; outerDeadzonePct: number; antiDeadzonePct: number;
}
export interface TriggerCalibration { startDeadzonePct: number; fullScalePct: number; }
export interface AxisCalibrationConfig {
  schemaVersion: 1;
  /** Applied master OFF is an unconditional identity path. Legacy missing master migrates OFF. */
  enabled: boolean;
  sticksEnabled: boolean;
  triggersEnabled: boolean;
  applyToVirtual: boolean; applyToLocal: boolean;
  sticks: Record<StickSide, StickCalibration>;
  triggers: Record<TriggerSide, TriggerCalibration>;
}
export type NativeCounter = string; // Lossless uint64 decimal, never a truncated JS number.
export interface SourceBinding {
  /** Native-resolved source identity; neither an XInput slot nor a browser id is sufficient. */
  deviceKey: string;
  connectionEpoch: NativeCounter;
  ownershipEpoch: NativeCounter;
  powerGeneration: NativeCounter;
  /** Exact native source tuple; these fields are resolved by the existing source owner. */
  route?: string;
  identityGeneration?: NativeCounter;
  topologyGeneration?: NativeCounter;
}
export interface VirtualContext {
  requestId: NativeCounter; runId: string; hostEpoch: NativeCounter; // runId is opaque, not a numeric counter.
  requestedPersona: string; boundPersona: string;
}
export interface SampleWitness {
  source: SourceBinding;
  readSequence: NativeCounter;
  readStartedTick: NativeCounter;
  readCompletedTick: NativeCounter;
}
export interface NormalizedAxes {
  left: { x: number; y: number }; right: { x: number; y: number }; lt: number; rt: number;
}
export interface SignalCapabilities {
  sticks: 'standard-independent' | 'unmapped' | 'unavailable';
  lt: 'independent-analog' | 'digital' | 'merged' | 'unknown' | 'unavailable';
  rt: 'independent-analog' | 'digital' | 'merged' | 'unknown' | 'unavailable';
  virtualWrite: 'available' | 'unsupported' | 'unknown';
  localWrite: 'available' | 'unsupported' | 'unknown';
}
export type RouteState = 'bypass' | 'pending' | 'active' | 'unsupported' | 'rejected' | 'stopped';
export interface RouteReceipt {
  state: RouteState;
  appliedInputRevision: number | null;
  appliedConfigHash: string | null;
  source: SourceBinding | null;
  reason: string;
}
export interface CalibrationSnapshot {
  protocol: typeof AXIS_CALIBRATION_PROTOCOL;
  /** Must be the shared input snapshot revision; not a private calibration counter. */
  inputRevision: number;
  inputConfigHash: string;
  config: AxisCalibrationConfig;
  source: SourceBinding | null;
  /** Absent for a local software route with no Host. */
  virtualContext?: VirtualContext;
  sample?: SampleWitness;
  capabilities: SignalCapabilities;
  routes: { virtual: RouteReceipt; local: RouteReceipt };
}
export interface ApplyRequest {
  protocol: typeof AXIS_CALIBRATION_PROTOCOL;
  expectedInputRevision: number;
  /** Null is allowed only for a master-OFF commit, so disconnect cannot prevent disabling. */
  source: SourceBinding | null;
  config: AxisCalibrationConfig;
  /** Correlates one explicit Apply action; never used to grant runtime ownership. */
  requestId: string;
}
export type ApplyReceipt =
  | { ok: true; saved: true; requestId: string; snapshot: CalibrationSnapshot }
  | { ok: false; saved: false; requestId: string;
      reason: 'revision-conflict' | 'source-changed' | 'unsupported-route' | 'invalid-config' |
        'write-failed' | 'ownership-denied' | 'cancelled-before-commit';
      snapshot: CalibrationSnapshot | null };
export interface CenterRequest {
  protocol: typeof AXIS_CALIBRATION_PROTOCOL;
  source: SourceBinding;
  side: StickSide;
  expectedInputRevision: number;
}
export type CenterReceipt =
  | { ok: true; sessionId: string; source: SourceBinding; expectedInputRevision: number;
      side: StickSide; sampleCount: number; centerX: number; centerY: number; noiseP99: number }
  | { ok: false; sessionId: string; reason: string };
/** Backend negotiation surface. Do not map to speculative IPC names in production. */
export interface AxisCalibrationPort {
  read(): Promise<CalibrationSnapshot>;
  /** Sole durable write. No slider/toggle/center handler may call this implicitly. */
  apply(request: ApplyRequest): Promise<ApplyReceipt>;
  /** Samples pre-calibration source values; candidate only, never persists settings. */
  startCenter(request: CenterRequest): Promise<{ sessionId: string }>;
  cancelCenter(sessionId: string): Promise<void>;
  /** Reuse the existing native status/event owner, rather than adding a polling loop. */
  subscribe(listener: (event: CalibrationSnapshot | CenterReceipt) => void): () => void;
}
export function defaultAxisCalibration(): AxisCalibrationConfig {
  const stick = (): StickCalibration => ({ enabled: true, centerX: 0, centerY: 0, outputShape: 'original', innerDeadzonePct: 0, outerDeadzonePct: 0, antiDeadzonePct: 0 });
  return { schemaVersion: 1, enabled: false, sticksEnabled: true, triggersEnabled: true, applyToVirtual: true, applyToLocal: false,
    sticks: { left: stick(), right: stick() },
    triggers: { lt: { startDeadzonePct: 0, fullScalePct: 100 }, rt: { startDeadzonePct: 0, fullScalePct: 100 } } };
}
function numberIn(value: unknown, min: number, max: number): boolean {
  return typeof value === 'number' && Number.isFinite(value) && value >= min && value <= max;
}
export function validateAxisCalibration(value: unknown): value is AxisCalibrationConfig {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return false;
  const v = value as AxisCalibrationConfig;
  if (v.schemaVersion !== 1 || typeof v.enabled !== 'boolean' || typeof v.sticksEnabled !== 'boolean' || typeof v.triggersEnabled !== 'boolean' || typeof v.applyToVirtual !== 'boolean' || typeof v.applyToLocal !== 'boolean') return false;
  for (const side of ['left', 'right'] as const) {
    const s = v.sticks?.[side];
    if (!s || typeof s.enabled !== 'boolean' || !numberIn(s.centerX, -.15, .15) || !numberIn(s.centerY, -.15, .15) ||
      !numberIn(s.innerDeadzonePct, 0, 25) || !numberIn(s.outerDeadzonePct, 0, 25) ||
      !numberIn(s.antiDeadzonePct, 0, 25)) return false;
    if (s.outputShape !== undefined && !['original', 'circle-to-square', 'square-to-circle'].includes(s.outputShape)) return false;
    if (![s.innerDeadzonePct, s.outerDeadzonePct, s.antiDeadzonePct].every(Number.isInteger)) return false;
  }
  for (const side of ['lt', 'rt'] as const) {
    const t = v.triggers?.[side];
    if (!t || !numberIn(t.startDeadzonePct, 0, 99) || !numberIn(t.fullScalePct, 1, 100) ||
      !Number.isInteger(t.startDeadzonePct) || !Number.isInteger(t.fullScalePct) ||
      t.fullScalePct <= t.startDeadzonePct) return false;
  }
  return true;
}
export function validNativeCounter(value: unknown): value is NativeCounter {
  return typeof value === 'string' && /^(0|[1-9]\d{0,19})$/.test(value) && BigInt(value) <= 18446744073709551615n;
}
export function validSource(source: SourceBinding | null): source is SourceBinding {
  return !!source && typeof source.deviceKey === 'string' && source.deviceKey.trim().length > 0 &&
    validNativeCounter(source.connectionEpoch) && source.connectionEpoch !== '0' &&
    validNativeCounter(source.ownershipEpoch) && source.ownershipEpoch !== '0' &&
    validNativeCounter(source.powerGeneration) &&
    (source.identityGeneration === undefined || validNativeCounter(source.identityGeneration)) &&
    (source.topologyGeneration === undefined || validNativeCounter(source.topologyGeneration));
}
export function sameSource(a: SourceBinding | null, b: SourceBinding | null): boolean {
  return validSource(a) && validSource(b) && a.deviceKey === b.deviceKey && a.connectionEpoch === b.connectionEpoch &&
    a.ownershipEpoch === b.ownershipEpoch && a.powerGeneration === b.powerGeneration &&
    a.route === b.route && a.identityGeneration === b.identityGeneration && a.topologyGeneration === b.topologyGeneration;
}
function clamp(value: number, min = 0, max = 1): number { return Math.min(max, Math.max(min, value)); }
/** Shape behavior follows HC InputUtils.ImproveCircularity / ImproveSquare.
 * Applied after radial inner/outer deadzones and anti-deadzone (HC AxisActions).
 * This is output reshaping, not a reversible recovery of missing hardware data.
 * https://github.com/Valkirie/HandheldCompanion/blob/main/HandheldCompanion/Utils/InputUtils.cs
 */
export function squareToCircle(value: { x: number; y: number }) {
  const radius = Math.hypot(value.x, value.y);
  if (radius <= 1) return { ...value };
  return { x: value.x / radius, y: value.y / radius };
}
export function circleToSquare(value: { x: number; y: number }) {
  const radius = Math.hypot(value.x, value.y);
  if (radius < 1e-5) return { x: 0, y: 0 };
  const edge = Math.max(Math.abs(value.x), Math.abs(value.y));
  if (edge <= 1e-5) return { ...value };
  const scale = Math.min(1, radius) / edge;
  return { x: value.x * scale, y: value.y * scale };
}
export function mapCalibrationStick(value: { x: number; y: number }, s: StickCalibration) {
  if (!s.enabled) return { ...value };
  let output = {
    x: clamp((value.x - s.centerX) / (value.x >= s.centerX ? 1 - s.centerX : 1 + s.centerX), -1, 1),
    y: clamp((value.y - s.centerY) / (value.y >= s.centerY ? 1 - s.centerY : 1 + s.centerY), -1, 1),
  };
  if (s.innerDeadzonePct !== 0 || s.outerDeadzonePct !== 0) {
    const radius = Math.hypot(output.x, output.y), inner = s.innerDeadzonePct / 100;
    if (radius <= inner || radius === 0) return { x: 0, y: 0 };
    const mapped = clamp((radius - inner) / (1 - s.outerDeadzonePct / 100 - inner));
    output = { x: output.x / radius * mapped, y: output.y / radius * mapped };
  }
  if (s.antiDeadzonePct !== 0) {
    const radius = Math.hypot(output.x, output.y);
    if (radius === 0) return { x: 0, y: 0 };
    const anti = s.antiDeadzonePct / 100, scale = ((1 - anti) * radius + anti) / radius;
    output = { x: output.x * scale, y: output.y * scale };
  }
  if (s.outputShape === 'circle-to-square') return circleToSquare(output);
  if (s.outputShape === 'square-to-circle') return squareToCircle(output);
  return output;
}
export function mapCalibrationTrigger(value: number, settings: TriggerCalibration): number {
  return clamp((value * 100 - settings.startDeadzonePct) / (settings.fullScalePct - settings.startDeadzonePct));
}
export type CalibrationDecision =
  | { ok: true; virtual: NormalizedAxes; local: NormalizedAxes; calibrated: NormalizedAxes }
  | { ok: false; reason: 'invalid-config' | 'invalid-sample' | 'unmapped-sticks' | 'unsupported-trigger' | 'unsupported-route' };
/** Numerical oracle only. Production owner must retain all focus/power/source/neutral admission gates. */
export function evaluateAxisCalibration(raw: NormalizedAxes, config: AxisCalibrationConfig, caps: SignalCapabilities): CalibrationDecision {
  if (!validateAxisCalibration(config)) return { ok: false, reason: 'invalid-config' };
  if (!numberIn(raw?.left?.x, -1, 1) || !numberIn(raw?.left?.y, -1, 1) ||
    !numberIn(raw?.right?.x, -1, 1) || !numberIn(raw?.right?.y, -1, 1) ||
    !numberIn(raw?.lt, 0, 1) || !numberIn(raw?.rt, 0, 1)) return { ok: false, reason: 'invalid-sample' };
  const copy = (v: NormalizedAxes): NormalizedAxes => ({ left: { ...v.left }, right: { ...v.right }, lt: v.lt, rt: v.rt });
  if (!config.enabled || (!config.sticksEnabled && !config.triggersEnabled) || (!config.applyToVirtual && !config.applyToLocal)) return { ok: true, calibrated: copy(raw), virtual: copy(raw), local: copy(raw) };
  if ((config.applyToVirtual && caps.virtualWrite !== 'available') || (config.applyToLocal && caps.localWrite !== 'available'))
    return { ok: false, reason: 'unsupported-route' };
  const hasStickSettings = (s: StickCalibration) => s.enabled && (s.centerX !== 0 || s.centerY !== 0 || s.innerDeadzonePct !== 0 || s.outerDeadzonePct !== 0 || s.antiDeadzonePct !== 0 || (s.outputShape !== undefined && s.outputShape !== 'original'));
  if (config.sticksEnabled && caps.sticks !== 'standard-independent' && (hasStickSettings(config.sticks.left) || hasStickSettings(config.sticks.right)))
    return { ok: false, reason: 'unmapped-sticks' };
  for (const side of ['lt', 'rt'] as const) {
    const t = config.triggers[side];
    if (config.triggersEnabled && (t.startDeadzonePct !== 0 || t.fullScalePct !== 100) && caps[side] !== 'independent-analog')
      return { ok: false, reason: 'unsupported-trigger' };
  }
  const calibrated = {
    left: config.sticksEnabled && config.sticks.left.enabled && caps.sticks === 'standard-independent' ? mapCalibrationStick(raw.left, config.sticks.left) : { ...raw.left },
    right: config.sticksEnabled && config.sticks.right.enabled && caps.sticks === 'standard-independent' ? mapCalibrationStick(raw.right, config.sticks.right) : { ...raw.right },
    lt: config.triggersEnabled && caps.lt === 'independent-analog' ? mapCalibrationTrigger(raw.lt, config.triggers.lt) : raw.lt,
    rt: config.triggersEnabled && caps.rt === 'independent-analog' ? mapCalibrationTrigger(raw.rt, config.triggers.rt) : raw.rt,
  };
  return { ok: true, calibrated, virtual: config.applyToVirtual ? copy(calibrated) : copy(raw), local: config.applyToLocal ? copy(calibrated) : copy(raw) };
}
