import { reportFrontendTrace } from '@/robust/frontendDiagnostics';
import { invoke, on } from './ipc';
import { acceptNativeInputSettingsCommit } from './settingsRepository';
import { canonicalizeInputConfiguration } from './inputConfigHash';
import {
  AXIS_CALIBRATION_PROTOCOL, defaultAxisCalibration, sameSource, validNativeCounter, validSource,
  validateAxisCalibration, type AxisCalibrationConfig, type AxisCalibrationPort, type ApplyReceipt,
  type CalibrationSnapshot, type CenterReceipt, type NormalizedAxes, type StickSide,
} from './controllerCalibrationContract';

export interface CalibrationAxes {
  raw: NormalizedAxes;
  virtual: NormalizedAxes | null;
  local: NormalizedAxes | null;
}
export interface ControllerCalibrationSnapshot extends CalibrationSnapshot { axes?: CalibrationAxes; inputDocument?: Record<string, unknown>; observerId?: string; centerSession?: { sessionId: string; active: boolean }; }
export interface ControllerCalibrationPort extends Omit<AxisCalibrationPort, 'read' | 'subscribe'> {
  read(): Promise<ControllerCalibrationSnapshot>;
  subscribe(listener: (event: ControllerCalibrationSnapshot | CenterReceipt) => void): () => void;
}
export interface CalibrationEditorState {
  snapshot: ControllerCalibrationSnapshot | null;
  draft: AxisCalibrationConfig;
  dirty: boolean;
  loading: boolean;
  available: boolean;
  applying: boolean;
  sampling: boolean;
  centerSide: StickSide | null;
  feedback: string;
}
const copy = <T>(value: T): T => structuredClone(value);
const equalConfig = (a: AxisCalibrationConfig, b: AxisCalibrationConfig) => {
  const normalized = (value: AxisCalibrationConfig) => ({ ...value, sticks: {
    left: { ...value.sticks.left, outputShape: value.sticks.left.outputShape || 'original' },
    right: { ...value.sticks.right, outputShape: value.sticks.right.outputShape || 'original' },
  } });
  return canonicalizeInputConfiguration(normalized(a)) === canonicalizeInputConfiguration(normalized(b));
};
const object = (v: unknown): v is Record<string, any> => !!v && typeof v === 'object' && !Array.isArray(v);
export function validCalibrationAxes(v: unknown): v is NormalizedAxes {
  if (!object(v)) return false;
  const inRange = (n: unknown, min: number, max: number) => typeof n === 'number' && Number.isFinite(n) && n >= min && n <= max;
  return ['left', 'right'].every(side => object(v[side]) && inRange(v[side].x, -1, 1) && inRange(v[side].y, -1, 1)) &&
    inRange(v.lt, 0, 1) && inRange(v.rt, 0, 1);
}
/** Reject malformed transport replies; never manufacture an active receipt or source. */
export function parseCalibrationSnapshot(v: unknown): ControllerCalibrationSnapshot {
  if (!object(v) || v.protocol !== AXIS_CALIBRATION_PROTOCOL || !Number.isSafeInteger(v.inputRevision) || v.inputRevision < 0 ||
      typeof v.inputConfigHash !== 'string' || !validateAxisCalibration(v.config) ||
      (v.source !== null && !validSource(v.source)) || !object(v.capabilities) || !object(v.routes)) throw new Error('校对状态无效');
  if (v.centerSession !== undefined && (!object(v.centerSession) || typeof v.centerSession.sessionId !== 'string' ||
      v.centerSession.sessionId.length > 256 || (v.centerSession.sessionId !== '' && !/^[\x21-\x7e]+$/.test(v.centerSession.sessionId)) ||
      typeof v.centerSession.active !== 'boolean' || (v.centerSession.active && !v.centerSession.sessionId))) throw new Error('校准会话状态无效');
  const caps = v.capabilities;
  if (!['standard-independent','unmapped','unavailable'].includes(caps.sticks) ||
      !['lt','rt'].every(s => ['independent-analog','digital','merged','unknown','unavailable'].includes(caps[s])) ||
      !['virtualWrite','localWrite'].every(s => ['available','unsupported','unknown'].includes(caps[s]))) throw new Error('校对能力无效');
  for (const target of ['virtual','local']) {
    const r = v.routes[target];
    if (!object(r) || !['bypass','pending','active','unsupported','rejected','stopped'].includes(r.state) || typeof r.reason !== 'string' ||
        !(r.appliedInputRevision === null || Number.isSafeInteger(r.appliedInputRevision) && r.appliedInputRevision >= 0) ||
        !(r.appliedConfigHash === null || typeof r.appliedConfigHash === 'string') ||
        !(r.source === null || validSource(r.source))) throw new Error('校对通路回执无效');
    if (r.state === 'active' && (!sameSource(r.source, v.source) || r.appliedInputRevision !== v.inputRevision ||
        !v.inputConfigHash || r.appliedConfigHash !== v.inputConfigHash)) throw new Error('校对回执不属于当前来源或配置');
  }
  if (v.virtualContext && (!object(v.virtualContext) || typeof v.virtualContext.runId !== 'string' || !v.virtualContext.runId ||
      !validNativeCounter(v.virtualContext.requestId) || !validNativeCounter(v.virtualContext.hostEpoch))) throw new Error('虚拟手柄身份无效');
  if (v.sample && (!object(v.sample) || !sameSource(v.sample.source, v.source) ||
      !['readSequence','readStartedTick','readCompletedTick'].every(k => validNativeCounter(v.sample[k])) ||
      BigInt(v.sample.readCompletedTick) < BigInt(v.sample.readStartedTick))) throw new Error('校对采样回执无效');
  if (v.axes && (!object(v.axes) || !v.sample || !validCalibrationAxes(v.axes.raw) ||
      !['virtual','local'].every(k => v.axes[k] === null || validCalibrationAxes(v.axes[k])) ||
      (caps.localWrite !== 'available' && v.axes.local !== null) || (v.axes.virtual !== null && !v.virtualContext))) throw new Error('校对输入快照无效');
  return copy(v as ControllerCalibrationSnapshot);
}

/** Turning correction OFF remains available with no source or optional output capability. */
export function calibrationApplyAvailable(snapshot: ControllerCalibrationSnapshot | null, config: AxisCalibrationConfig): boolean {
  if (!snapshot) return false;
  if (!config.enabled) return true;
  return validSource(snapshot.source) && (!config.applyToVirtual || snapshot.capabilities.virtualWrite === 'available') &&
    (!config.applyToLocal || snapshot.capabilities.localWrite === 'available');
}


let calibrationTraceSerial = 0;
type CalibrationTraceStage = 'page' | 'observe' | 'read' | 'apply' | 'snapshot' | 'center';
const CALIBRATION_TRACE_POLICY = 'YMCC-AXIS-CALIBRATION-TRACE-20261025-R1';
const traceText = (value: unknown, max = 240): string | undefined => typeof value === 'string' ? value.slice(0, max) : undefined;
function traceSource(source: CalibrationSnapshot['source']) {
  if (!source) return null;
  const deviceKey = traceText(source.deviceKey) || '';
  const route = traceText(source.route, 80);
  return { deviceKey, deviceKeyTruncated: deviceKey.length !== source.deviceKey.length,
    deviceKeyOriginalLength: source.deviceKey.length, identityComplete: deviceKey.length === source.deviceKey.length && route === source.route,
    connectionEpoch: source.connectionEpoch, ownershipEpoch: source.ownershipEpoch, powerGeneration: source.powerGeneration,
    identityGeneration: source.identityGeneration, topologyGeneration: source.topologyGeneration,
    route, routeTruncated: route !== source.route };
}
function traceSnapshot(snapshot: ControllerCalibrationSnapshot, witness = false) {
  const route = (value: CalibrationSnapshot['routes']['virtual']) => ({ state: value.state, reason: traceText(value.reason),
    reasonTruncated: value.reason.length > 240, appliedInputRevision: value.appliedInputRevision,
    appliedConfigHash: traceText(value.appliedConfigHash, 128), source: traceSource(value.source) });
  const context = snapshot.virtualContext;
  return { source: traceSource(snapshot.source), inputRevision: snapshot.inputRevision,
    inputConfigHash: traceText(snapshot.inputConfigHash, 128), inputConfigHashTruncated: snapshot.inputConfigHash.length > 128,
    observerId: traceText(snapshot.observerId, 256), enabled: snapshot.config.enabled,
    routes: { virtual: route(snapshot.routes.virtual), local: route(snapshot.routes.local) },
    virtualContext: context ? { requestId: context.requestId, runId: traceText(context.runId, 160),
      runIdTruncated: context.runId.length > 160, hostEpoch: context.hostEpoch,
      requestedPersona: traceText(context.requestedPersona, 80), boundPersona: traceText(context.boundPersona, 80) } : null,
    centerSession: snapshot.centerSession ? { sessionId: traceText(snapshot.centerSession.sessionId, 256), active: snapshot.centerSession.active } : null,
    acceptanceMeaning: 'local-submit-accepted-only',
    ...(witness && snapshot.sample ? { sample: { source: traceSource(snapshot.sample.source),
      readSequence: snapshot.sample.readSequence, readStartedTick: snapshot.sample.readStartedTick,
      readCompletedTick: snapshot.sample.readCompletedTick }, axes: snapshot.axes ? { raw: snapshot.axes.raw,
      finalAccepted: snapshot.axes.virtual, local: snapshot.axes.local,
      finalAcceptedMeaning: 'may-include-existing-composition-or-neutralization' } : undefined } : {}) };
}
function traceConfig(config: AxisCalibrationConfig) {
  const stick = (s: AxisCalibrationConfig['sticks']['left']) => ({ enabled: s.enabled, centerX: s.centerX, centerY: s.centerY,
    outputShape: s.outputShape || 'original', innerDeadzonePct: s.innerDeadzonePct, outerDeadzonePct: s.outerDeadzonePct, antiDeadzonePct: s.antiDeadzonePct });
  const trigger = (s: AxisCalibrationConfig['triggers']['lt']) => ({ startDeadzonePct: s.startDeadzonePct, fullScalePct: s.fullScalePct });
  return { schemaVersion: config.schemaVersion, enabled: config.enabled, sticksEnabled: config.sticksEnabled,
    triggersEnabled: config.triggersEnabled, applyToVirtual: config.applyToVirtual, applyToLocal: config.applyToLocal,
    sticks: { left: stick(config.sticks.left), right: stick(config.sticks.right) },
    triggers: { lt: trigger(config.triggers.lt), rt: trigger(config.triggers.rt) } };
}
type CalibrationTraceSnapshotKey = Pick<ControllerCalibrationSnapshot, 'inputRevision' | 'inputConfigHash' | 'source' | 'observerId' | 'virtualContext' | 'centerSession' | 'routes'> & { enabled: boolean };
const CALIBRATION_TRACE_ROUTE_KEYS = ['virtual', 'local'] as const;
const traceCacheTextBounded = (value: string | undefined | null, limit: number) => value == null || value.length <= limit;
const traceCacheSourceBounded = (value: CalibrationSnapshot['source']) => !value || traceCacheTextBounded(value.deviceKey, 1024) && traceCacheTextBounded(value.route, 128);
function traceSnapshotCacheable(s: ControllerCalibrationSnapshot): boolean {
  if (!traceCacheSourceBounded(s.source) || !traceCacheTextBounded(s.inputConfigHash, 256) || !traceCacheTextBounded(s.observerId, 256) ||
      !traceCacheTextBounded(s.centerSession?.sessionId, 256) || !traceCacheTextBounded(s.virtualContext?.runId, 512) ||
      !traceCacheTextBounded(s.virtualContext?.requestedPersona, 80) || !traceCacheTextBounded(s.virtualContext?.boundPersona, 80)) return false;
  for (const target of CALIBRATION_TRACE_ROUTE_KEYS) {
    const route = s.routes[target];
    if (!traceCacheSourceBounded(route.source) || !traceCacheTextBounded(route.reason, 512) || !traceCacheTextBounded(route.appliedConfigHash, 256)) return false;
  }
  return true;
}
function traceSnapshotKey(s: ControllerCalibrationSnapshot): CalibrationTraceSnapshotKey {
  const source = (value: CalibrationSnapshot['source']) => value ? { deviceKey: value.deviceKey, connectionEpoch: value.connectionEpoch,
    ownershipEpoch: value.ownershipEpoch, powerGeneration: value.powerGeneration, route: value.route,
    identityGeneration: value.identityGeneration, topologyGeneration: value.topologyGeneration } : null;
  const route = (value: CalibrationSnapshot['routes']['virtual']) => ({ state: value.state, reason: value.reason,
    appliedInputRevision: value.appliedInputRevision, appliedConfigHash: value.appliedConfigHash, source: source(value.source) });
  const context = s.virtualContext;
  return { inputRevision: s.inputRevision, inputConfigHash: s.inputConfigHash, source: source(s.source), observerId: s.observerId,
    enabled: s.config.enabled, virtualContext: context ? { requestId: context.requestId, runId: context.runId, hostEpoch: context.hostEpoch,
      requestedPersona: context.requestedPersona, boundPersona: context.boundPersona } : undefined,
    centerSession: s.centerSession ? { sessionId: s.centerSession.sessionId, active: s.centerSession.active } : undefined,
    routes: { virtual: route(s.routes.virtual), local: route(s.routes.local) } };
}
function calibrationTraceSnapshotChanged(previous: CalibrationTraceSnapshotKey | null, next: ControllerCalibrationSnapshot): boolean {
  if (!previous || previous.inputRevision !== next.inputRevision || previous.inputConfigHash !== next.inputConfigHash ||
      !sameNullableSource(previous.source, next.source) || previous.observerId !== next.observerId ||
      previous.enabled !== next.config.enabled || previous.centerSession?.sessionId !== next.centerSession?.sessionId ||
      previous.centerSession?.active !== next.centerSession?.active) return true;
  for (const key of CALIBRATION_TRACE_ROUTE_KEYS) {
    const a = previous.routes[key], b = next.routes[key];
    if (a.state !== b.state || a.reason !== b.reason || a.appliedInputRevision !== b.appliedInputRevision ||
        a.appliedConfigHash !== b.appliedConfigHash || !sameNullableSource(a.source, b.source)) return true;
  }
  const a = previous.virtualContext, b = next.virtualContext;
  return a?.requestId !== b?.requestId || a?.runId !== b?.runId || a?.hostEpoch !== b?.hostEpoch ||
    a?.requestedPersona !== b?.requestedPersona || a?.boundPersona !== b?.boundPersona;
}
/** One bounded evidence owner shared by the port and its editor. No control state or work scheduling. */
class CalibrationTrace {
  readonly pageId: string;
  private windowAt = 0;
  private emitted = 0;
  private suppressed = 0;
  private lastSnapshotKey: CalibrationTraceSnapshotKey | null = null;
  private cacheUnavailable = false;
  constructor() {
    let id: string; try { id = crypto.randomUUID(); } catch { id = `fallback-${++calibrationTraceSerial}`; }
    this.pageId = `axis-page:${id}`;
  }
  emit(stage: CalibrationTraceStage, phase: string, detail: () => Record<string, unknown> = () => ({})): void {
    try {
      const now = Date.now();
      if (!this.windowAt || now < this.windowAt || now - this.windowAt >= 60000) { this.windowAt = now; this.emitted = 0; }
      if (this.emitted >= 64) { ++this.suppressed; return; }
      const suppressed = this.suppressed;
      let record: Record<string, unknown>;
      if (this.emitted === 63) {
        ++this.suppressed;
        record = { stage: 'page', phase: 'trace-budget-exhausted', suppressedAtLeast: this.suppressed,
          omittedStage: stage, omittedPhase: traceText(phase, 48) };
      } else { record = { ...detail(), stage, phase: traceText(phase, 48), suppressedSincePriorLine: suppressed }; this.suppressed = 0; }
      record = { ...record, schemaVersion: 1, policy: CALIBRATION_TRACE_POLICY, pageId: this.pageId };
      if (new TextEncoder().encode(JSON.stringify(record)).length > 7600)
        record = { schemaVersion: 1, policy: CALIBRATION_TRACE_POLICY, pageId: this.pageId, stage,
          phase: 'payload-omitted', originalPhase: traceText(phase, 48), detailTruncated: true, suppressedSincePriorLine: suppressed };
      ++this.emitted;
      reportFrontendTrace('controller-calibration', record);
    } catch { /* Evidence failure must not change the operation or its receipt. */ }
  }
  snapshot(snapshot: ControllerCalibrationSnapshot): void {
    try {
      if (!traceSnapshotCacheable(snapshot)) {
        if (this.cacheUnavailable) return;
        this.cacheUnavailable = true; this.lastSnapshotKey = null;
        this.emit('snapshot', 'comparison-unavailable', () => ({ ...traceSnapshot(snapshot),
          semanticComparisonUnavailable: true, reason: 'identity-or-state-exceeds-bounded-cache' }));
        return;
      }
      if (!this.cacheUnavailable && !calibrationTraceSnapshotChanged(this.lastSnapshotKey, snapshot)) return;
      this.cacheUnavailable = false;
      this.lastSnapshotKey = traceSnapshotKey(snapshot);
      const accepted = snapshot.routes.virtual.state === 'active' || snapshot.routes.local.state === 'active';
      this.emit('snapshot', 'state-edge', () => traceSnapshot(snapshot, accepted));
    } catch { /* Invalid evidence is never a reason to alter input. */ }
  }
}
const calibrationPortTraces = new WeakMap<ControllerCalibrationPort, CalibrationTrace>();
function traceForPort(port: ControllerCalibrationPort): CalibrationTrace {
  let trace = calibrationPortTraces.get(port);
  if (!trace) { trace = new CalibrationTrace(); calibrationPortTraces.set(port, trace); }
  return trace;
}
const traceError = (error: unknown) => traceText(error instanceof Error ? error.message : String(error));

/** UI session only: one explicit Apply, no storage, output writer, polling or automatic retry. */
export class CalibrationEditor {
  private state: CalibrationEditorState = { snapshot: null, draft: defaultAxisCalibration(), dirty: false,
    loading: false, available: false, applying: false, sampling: false, centerSide: null, feedback: '正在读取校对状态' };
  private mounted = false;
  private life = 0;
  private observed = 0;
  private observationVisible = true;
  private observationLife = 0;
  private draftVersion = 0;
  private baseRevision = 0;
  private baseline = defaultAxisCalibration();
  private unsubscribe: (() => void) | null = null;
  private centerToken = 0;
  private centerSession: string | null = null;
  private centerRequestedSession: string | null = null;
  private stopCenter: (() => void) | null = null;
  private centerRequest: { source: NonNullable<CalibrationSnapshot['source']>; side: StickSide; revision: number } | null = null;
  private earlyCenter = new Map<string, CenterReceipt>();
  private editorId = 0;
  private readonly evidence: CalibrationTrace;
  constructor(private port: ControllerCalibrationPort, private changed: (state: CalibrationEditorState) => void) { this.evidence = traceForPort(port); }
  get value(): CalibrationEditorState { return this.state; }
  private publish(): void { if (this.mounted) this.changed({ ...this.state }); }
  private feedback(text: string): void { this.state.feedback = text; this.publish(); }
  tracePage(phase: string): void {
    this.evidence.emit('page', phase, () => ({ editorLife: this.life, observerId: traceText(this.state.snapshot?.observerId, 256),
      sessionId: this.centerSession || this.centerRequestedSession, sampling: this.state.sampling, available: this.state.available,
      source: traceSource(this.state.snapshot?.source || null), inputRevision: this.state.snapshot?.inputRevision,
      inputConfigHash: traceText(this.state.snapshot?.inputConfigHash, 128) }));
  }
  async mount(reason = 'mount'): Promise<void> {
    if (this.mounted) return;
    this.mounted = true; ++this.life; this.tracePage(reason);
    if (this.observationVisible) await this.startObservation();
    else this.publish();
  }
  /** Pause presentation work without discarding a draft or changing saved/output state. */
  setObservationVisible(visible: boolean): void {
    if (this.observationVisible === visible) return;
    this.tracePage(visible ? 'visibility-show' : 'visibility-hide');
    this.observationVisible = visible;
    if (!this.mounted) return;
    if (visible) { void this.startObservation(); return; }
    ++this.observationLife;
    this.unsubscribe?.(); this.unsubscribe = null;
    this.cancelCenter();
    this.state.loading = false; this.state.available = false; this.publish();
  }
  private async startObservation(): Promise<void> {
    if (!this.mounted || !this.observationVisible || this.unsubscribe) return;
    const life = this.life, observationLife = ++this.observationLife, observed = this.observed;
    const current = () => this.mounted && this.observationVisible && life === this.life && observationLife === this.observationLife;
    this.state.loading = true; this.state.available = false; this.publish();
    try {
      this.unsubscribe = this.port.subscribe(event => {
        if (!current()) return;
        try {
          if ('sessionId' in event) this.receiveCenter(event);
          else { ++this.observed; this.receiveSnapshot(parseCalibrationSnapshot(event)); }
        } catch (error) { this.evidence.emit('snapshot', 'event-invalid', () => ({ error: traceError(error), editorLife: life })); this.state.available = false; this.cancelCenter(); this.feedback('校对状态暂不可用，未更改已保存参数'); }
      });
      const snapshot = parseCalibrationSnapshot(await this.port.read());
      if (current() && observed === this.observed) this.receiveSnapshot(snapshot);
    } catch (e) { if (current() && observed === this.observed) { this.state.available = false; this.feedback(e instanceof Error ? e.message : '无法读取校对状态'); } }
    finally { if (current()) { this.state.loading = false; this.publish(); } }
  }
  private receiveSnapshot(snapshot: ControllerCalibrationSnapshot): void {
    const previous = this.state.snapshot;
    // Within the same source, a delayed lower revision cannot replace a newer snapshot.
    if (previous && sameSource(previous.source, snapshot.source) && snapshot.inputRevision < previous.inputRevision) return;
    const sourceChanged = !!previous && !sameNullableSource(previous.source, snapshot.source);
    this.evidence.snapshot(snapshot);
    this.state.snapshot = snapshot; this.state.available = true;
    if (sourceChanged) this.cancelCenter();
    if (!this.state.dirty || sourceChanged) {
      this.baseline = copy(snapshot.config); this.state.draft = copy(snapshot.config);
      this.baseRevision = snapshot.inputRevision; this.state.dirty = false;
    }
    if (sourceChanged) { ++this.draftVersion; this.state.feedback = '手柄来源已变化，未应用的调整已丢弃'; }
    else if (!this.state.dirty && !this.state.applying && !this.state.sampling) this.state.feedback = '调整后点击应用';
    if (this.centerRequest && this.centerRequest.revision !== snapshot.inputRevision) this.cancelCenter();
    const terminal = snapshot.centerSession;
    if (this.state.sampling && terminal && !terminal.active && terminal.sessionId === (this.centerSession || this.centerRequestedSession)) {
      const early = this.earlyCenter.get(terminal.sessionId);
      if (early) { this.centerSession = terminal.sessionId; this.receiveCenter(early); }
      else { this.evidence.emit('center', 'inactive-without-result', () => ({ sessionId: terminal.sessionId, source: traceSource(snapshot.source), inputRevision: snapshot.inputRevision, measurementAvailable: false })); this.clearCenter(); this.state.feedback = '校准会话已结束，未取得有效结果，请重试'; }
    }
    this.publish();
  }
  update(edit: (config: AxisCalibrationConfig) => void): void {
    if (!this.mounted || this.state.applying || this.state.sampling || !this.state.snapshot || !this.state.available || !this.state.draft.enabled) return;
    const draft = copy(this.state.draft); edit(draft);
    if (!validateAxisCalibration(draft)) return;
    ++this.draftVersion; this.state.draft = draft; this.state.dirty = !equalConfig(draft, this.baseline);
    this.state.feedback = this.state.dirty ? '有未应用的调整' : '参数未修改'; this.publish();
  }
  /** Master is the only editable control while OFF. A toggle never commits settings. */
  setEnabled(enabled: boolean): void {
    if (typeof enabled !== 'boolean' || !this.mounted || this.state.applying || !this.state.snapshot || !this.state.available) return;
    this.cancelCenter();
    ++this.draftVersion; this.state.draft = { ...this.state.draft, enabled };
    this.state.dirty = !equalConfig(this.state.draft, this.baseline);
    this.state.feedback = this.state.dirty ? '有未应用的调整' : '参数未修改'; this.publish();
  }
  restoreDraft(draft: AxisCalibrationConfig): void { this.update(config => Object.assign(config, copy(draft))); }
  reset(): void { this.restoreDraft(defaultAxisCalibration()); }
  async apply(): Promise<void> {
    const source = this.state.snapshot?.source || null;
    if (!this.mounted || this.state.applying || this.state.sampling || !this.state.dirty || !this.state.available ||
        (!this.state.draft.enabled && !this.state.snapshot?.config.enabled) ||
        !calibrationApplyAvailable(this.state.snapshot, this.state.draft)) return;
    const life = this.life, version = this.draftVersion, config = copy(this.state.draft), binding = copy(source);
    // The native transaction owns the durable write; there is no second settingsRepository write.
    const requestId = `calibration-${Date.now()}-${++this.editorId}-${this.life}`;
    this.state.applying = true; this.feedback('正在应用');
    try {
      const result: ApplyReceipt = await this.port.apply({ protocol: AXIS_CALIBRATION_PROTOCOL, expectedInputRevision: this.baseRevision,
        source: binding, config, requestId });
      if (!this.mounted || life !== this.life || version !== this.draftVersion || !sameNullableSource(binding, this.state.snapshot?.source || null)) { this.evidence.emit('apply', 'retired-reply', () => ({ requestId, editorLife: life, source: traceSource(binding) })); return; }
      if (result.requestId !== requestId) throw new Error('应用回执不属于本次操作');
      if (!result.ok || !result.saved) {
        if (result.snapshot) this.state.snapshot = parseCalibrationSnapshot(result.snapshot);
        this.feedback('应用未完成，草稿已保留；请重新进入读取最新状态'); return;
      }
      const snapshot = parseCalibrationSnapshot(result.snapshot);
      if (!sameNullableSource(binding, snapshot.source) || snapshot.inputRevision !== this.baseRevision + 1 || !equalConfig(snapshot.config, config)) throw new Error('应用回执与提交内容不一致');
      if (this.state.snapshot && snapshot.inputRevision < this.state.snapshot.inputRevision) throw new Error('应用回执已过期，请重新进入核对');
      this.baseline = copy(snapshot.config); this.state.draft = copy(snapshot.config); this.state.dirty = false;
      this.baseRevision = snapshot.inputRevision; this.state.snapshot = snapshot;
      if (snapshot.inputDocument) acceptNativeInputSettingsCommit(snapshot.inputDocument, snapshot.inputRevision, snapshot.inputConfigHash);
      this.evidence.emit('apply', 'saved-validated', () => ({ requestId, editorLife: life, saved: true, ...traceSnapshot(snapshot) }));
      this.feedback('参数已保存，生效状态以当前通路回执为准');
    } catch (e) { this.evidence.emit('apply', 'validation-or-transport-failed', () => ({ requestId, editorLife: life, source: traceSource(binding), error: traceError(e) })); if (this.mounted && life === this.life && version === this.draftVersion) this.feedback(e instanceof Error ? e.message : '应用失败，草稿已保留'); }
    finally { if (this.mounted && life === this.life) { this.state.applying = false; this.publish(); } }
  }
  async startCenter(side: StickSide): Promise<void> {
    const snapshot = this.state.snapshot;
    if (!this.mounted || this.state.applying || !this.state.available || !snapshot?.source || snapshot.capabilities.sticks !== 'standard-independent') return;
    if (!this.state.draft.enabled || !this.state.draft.sticksEnabled || !this.state.draft.sticks[side].enabled) return;
    if (this.state.sampling) { this.cancelCenter(); return; }
    const token = ++this.centerToken, life = this.life;
    const requestedSession = `axis-center-client:${crypto.randomUUID()}`;
    let activeSession = requestedSession;
    const cancelled = new Set<string>();
    const stop = (id: string) => {
      if (cancelled.has(id)) return;
      cancelled.add(id); void this.port.cancelCenter(id).catch(() => {});
    };
    this.centerRequestedSession = requestedSession; this.stopCenter = () => stop(activeSession);
    this.centerRequest = { source: copy(snapshot.source), side, revision: snapshot.inputRevision };
    this.state.sampling = true; this.state.centerSide = side; this.feedback('请松开摇杆，正在采样');
    try {
      // Optional native extension; older ports may ACK their own exact server session ID.
      const request = { protocol: AXIS_CALIBRATION_PROTOCOL, source: copy(snapshot.source), side, expectedInputRevision: snapshot.inputRevision, sessionId: requestedSession };
      const ack = await this.port.startCenter(request);
      if (!ack || typeof ack.sessionId !== 'string' || !ack.sessionId) throw new Error('未收到有效校准会话');
      activeSession = ack.sessionId;
      if (!this.mounted || life !== this.life || token !== this.centerToken) { stop(activeSession); return; }
      this.evidence.emit('center', 'ack-validated', () => ({ requestedSessionId: requestedSession, sessionId: activeSession, editorLife: life, source: traceSource(request.source), expectedInputRevision: request.expectedInputRevision, side }));
      this.centerSession = activeSession;
      const early = this.earlyCenter.get(ack.sessionId); this.earlyCenter.clear(); if (early) this.receiveCenter(early);
    } catch (e) { this.evidence.emit('center', 'start-or-ack-failed', () => ({ sessionId: activeSession, requestedSessionId: requestedSession, editorLife: life, source: traceSource(snapshot.source), expectedInputRevision: snapshot.inputRevision, error: traceError(e) })); stop(activeSession); if (this.mounted && life === this.life && token === this.centerToken) { this.clearCenter(); this.feedback(e instanceof Error ? e.message : '校准未完成'); } }
  }
  private receiveCenter(receipt: CenterReceipt): void {
    if (!this.state.sampling || !this.centerRequest || !receipt || typeof receipt.sessionId !== 'string') return;
    if (!this.centerSession) {
      if (receipt.ok && (!sameSource(receipt.source, this.centerRequest.source) || receipt.side !== this.centerRequest.side || receipt.expectedInputRevision !== this.centerRequest.revision)) return;
      if (this.earlyCenter.size >= 4) this.earlyCenter.delete(this.earlyCenter.keys().next().value!);
      this.earlyCenter.set(receipt.sessionId, receipt); return;
    }
    if (receipt.sessionId !== this.centerSession) return;
    const request = this.centerRequest;
    if (!sameSource(request.source, this.state.snapshot?.source || null) || request.revision !== this.state.snapshot?.inputRevision) { this.cancelCenter(); return; }
    if (!receipt.ok) { this.evidence.emit('center', 'terminal-rejected', () => ({ sessionId: receipt.sessionId, reason: traceText('reason' in receipt ? receipt.reason : undefined), source: traceSource(request.source), expectedInputRevision: request.revision })); this.clearCenter(); this.feedback('校准未完成，请松开摇杆后重试'); return; }
    if (!sameSource(request.source, receipt.source) || receipt.side !== request.side || receipt.expectedInputRevision !== request.revision ||
        !Number.isInteger(receipt.sampleCount) || receipt.sampleCount < 30 || !Number.isFinite(receipt.noiseP99) || receipt.noiseP99 < 0 || receipt.noiseP99 > .02 ||
        !Number.isFinite(receipt.centerX) || !Number.isFinite(receipt.centerY) || Math.hypot(receipt.centerX, receipt.centerY) > .15) {
      this.evidence.emit('center', 'terminal-invalid', () => ({ sessionId: receipt.sessionId, source: traceSource(request.source), expectedInputRevision: request.revision })); this.clearCenter(); this.feedback('校准结果不适用于当前手柄'); return;
    }
    this.evidence.emit('center', 'draft-only', () => ({ sessionId: receipt.sessionId, source: traceSource(request.source), expectedInputRevision: request.revision, side: request.side, sampleCount: receipt.sampleCount, noiseP99: receipt.noiseP99, centerX: receipt.centerX, centerY: receipt.centerY, requiresExplicitApply: true }));
    this.clearCenter();
    this.update(config => { config.sticks[request.side].centerX = receipt.centerX; config.sticks[request.side].centerY = receipt.centerY; });
    this.feedback('轴心采样完成，点击应用后保存');
  }
  private clearCenter(): void { ++this.centerToken; this.centerSession = null; this.centerRequestedSession = null; this.stopCenter = null; this.centerRequest = null; this.earlyCenter.clear(); this.state.sampling = false; this.state.centerSide = null; }
  cancelCenter(): void {
    if (this.centerRequest) this.evidence.emit('center', 'cancel-requested', () => ({ sessionId: this.centerSession || this.centerRequestedSession, source: traceSource(this.centerRequest!.source), expectedInputRevision: this.centerRequest!.revision, nativeTerminalVerified: false }));
    const stop = this.stopCenter; this.clearCenter();
    stop?.();
    this.publish();
  }
  dispose(reason = 'dispose'): void {
    if (!this.mounted) return;
    this.tracePage(reason);
    this.cancelCenter(); this.mounted = false; ++this.life;
    this.unsubscribe?.(); this.unsubscribe = null;
    this.state = { snapshot: null, draft: defaultAxisCalibration(), dirty: false, loading: false, available: false, applying: false, sampling: false, centerSide: null, feedback: '正在读取校对状态' };
    this.baseline = defaultAxisCalibration(); ++this.draftVersion;
  }
}
function sameNullableSource(a: CalibrationSnapshot['source'], b: CalibrationSnapshot['source']) { return a === null && b === null || sameSource(a, b); }

// Commands confirmed by the native owner in handoff/backend-port.json (2026-10-25).
export const CONTROLLER_CALIBRATION_COMMANDS = {
  read: 'controller.axisCalibration.get', apply: 'controller.axisCalibration.apply',
  startCenter: 'controller.axisCalibration.center.start', cancelCenter: 'controller.axisCalibration.center.cancel',
  event: 'controller.axisCalibration.changed', observe: 'controller.axisCalibration.observe',
} as const;
export function createControllerCalibrationPort(): ControllerCalibrationPort {
  const evidence = new CalibrationTrace();
  const port: ControllerCalibrationPort = {
    read: async () => {
      evidence.emit('read', 'requested');
      try {
        const snapshot = parseCalibrationSnapshot(await invoke(CONTROLLER_CALIBRATION_COMMANDS.read, {}, { timeoutMs: 3000 }));
        evidence.emit('read', 'parsed-validated', () => traceSnapshot(snapshot));
        return snapshot;
      } catch (error) { evidence.emit('read', 'failed', () => ({ error: traceError(error) })); throw error; }
    },
    apply: async request => {
      evidence.emit('apply', 'requested', () => ({ requestId: traceText(request.requestId, 256), source: traceSource(request.source),
        expectedInputRevision: request.expectedInputRevision, config: traceConfig(request.config) }));
      try {
        const receipt = await invoke<ApplyReceipt>(CONTROLLER_CALIBRATION_COMMANDS.apply, request, { timeoutMs: 10000 });
        if (!object(receipt) || typeof receipt.ok !== 'boolean' || typeof receipt.saved !== 'boolean' || typeof receipt.requestId !== 'string') throw new Error('应用回执无效');
        evidence.emit('apply', 'transport-settled', () => ({ requestId: traceText(request.requestId, 256),
          returnedRequestId: traceText(receipt.requestId, 256), ok: receipt.ok, saved: receipt.saved,
          reason: !receipt.ok ? traceText('reason' in receipt ? receipt.reason : undefined) : undefined, source: traceSource(request.source),
          expectedInputRevision: request.expectedInputRevision, frontendValidated: false }));
        return receipt;
      } catch (error) { evidence.emit('apply', 'transport-or-shape-failed', () => ({ requestId: traceText(request.requestId, 256), error: traceError(error) })); throw error; }
    },
    startCenter: async request => {
      const sessionId = traceText((request as typeof request & { sessionId?: string }).sessionId, 256);
      evidence.emit('center', 'start-requested', () => ({ sessionId, source: traceSource(request.source),
        expectedInputRevision: request.expectedInputRevision, side: request.side }));
      try {
        const ack = await invoke<{ sessionId: string }>(CONTROLLER_CALIBRATION_COMMANDS.startCenter, request, { timeoutMs: 3000 });
        evidence.emit('center', 'start-transport-settled', () => ({ requestedSessionId: sessionId,
          sessionId: traceText(ack?.sessionId, 256), measurementAvailable: false }));
        return ack;
      } catch (error) { evidence.emit('center', 'start-transport-failed', () => ({ sessionId, error: traceError(error) })); throw error; }
    },
    cancelCenter: async sessionId => {
      evidence.emit('center', 'cancel-transport-requested', () => ({ sessionId: traceText(sessionId, 256), nativeTerminalVerified: false }));
      try {
        const result = await invoke<unknown>(CONTROLLER_CALIBRATION_COMMANDS.cancelCenter, { sessionId }, { timeoutMs: 3000 });
        evidence.emit('center', 'cancel-transport-settled', () => ({ sessionId: traceText(sessionId, 256),
          returnedOk: object(result) ? result.ok === true : undefined, nativeTerminalVerified: false }));
      } catch (error) { evidence.emit('center', 'cancel-transport-failed', () => ({ sessionId: traceText(sessionId, 256), error: traceError(error), nativeTerminalVerified: false })); throw error; }
    },
    subscribe: listener => {
      // Known before the ACK: timeout/dispose can retire this exact watch even if the reply is lost.
      const requestedWatchId = `axis-watch-client:${crypto.randomUUID()}`;
      let disposed = false, failed = false, watchId: string | null = null;
      let earlySnapshot: ControllerCalibrationSnapshot | null = null;
      const stopped = new Set<string>();
      const stopWatch = (id: string) => {
        if (stopped.has(id)) return;
        stopped.add(id);
        evidence.emit('observe', 'stop-requested', () => ({ watchId: traceText(id, 256), requestedWatchId, nativeTerminalVerified: false }));
        void invoke<unknown>(CONTROLLER_CALIBRATION_COMMANDS.observe, { active: false, watchId: id }, { timeoutMs: 3000 }).then(result => {
          evidence.emit('observe', 'stop-transport-settled', () => ({ watchId: traceText(id, 256), requestedWatchId,
            returnedOk: object(result) ? result.ok === true : undefined, nativeTerminalVerified: false }));
        }).catch(error => evidence.emit('observe', 'stop-transport-failed', () => ({ watchId: traceText(id, 256),
          requestedWatchId, error: traceError(error), nativeTerminalVerified: false })));
      };
      const unsubscribe = on<ControllerCalibrationSnapshot | CenterReceipt>(CONTROLLER_CALIBRATION_COMMANDS.event, event => {
        if (disposed || failed || !object(event)) return;
        if ('sessionId' in event) { listener(event); return; }
        // Watched snapshots belong to this exact page token; a slot/source match is insufficient.
        if (typeof event.observerId !== 'string') return;
        if (!watchId) { earlySnapshot = event; return; }
        if (event.observerId === watchId) listener(event);
      });
      evidence.emit('observe', 'start-requested', () => ({ requestedWatchId }));
      void invoke<{ watchId: string }>(CONTROLLER_CALIBRATION_COMMANDS.observe, { active: true, watchId: requestedWatchId }, { timeoutMs: 3000 }).then(ack => {
        if (!ack || typeof ack.watchId !== 'string' || !ack.watchId) throw new Error('校对观测未建立');
        evidence.emit('observe', 'start-ack-validated', () => ({ requestedWatchId, watchId: traceText(ack.watchId, 256), retired: disposed }));
        if (disposed) stopWatch(ack.watchId);
        else {
          watchId = ack.watchId;
          if (earlySnapshot && (earlySnapshot as unknown as { observerId: string }).observerId === watchId) listener(earlySnapshot);
          earlySnapshot = null;
        }
      }).catch(error => {
        evidence.emit('observe', 'start-failed', () => ({ requestedWatchId, error: traceError(error), retired: disposed }));
        failed = true; earlySnapshot = null; unsubscribe(); stopWatch(watchId || requestedWatchId);
        if (!disposed) listener({ observationUnavailable: true } as unknown as ControllerCalibrationSnapshot);
      });
      return () => { if (disposed) return; disposed = true; earlySnapshot = null; unsubscribe(); stopWatch(watchId || requestedWatchId); };
    },
  };
  calibrationPortTraces.set(port, evidence);
  return port;
}
