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
export interface ControllerCalibrationSnapshot extends CalibrationSnapshot { axes?: CalibrationAxes; inputDocument?: Record<string, unknown>; observerId?: string; }
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
  private centerRequest: { source: NonNullable<CalibrationSnapshot['source']>; side: StickSide; revision: number } | null = null;
  private earlyCenter = new Map<string, CenterReceipt>();
  private editorId = 0;
  constructor(private port: ControllerCalibrationPort, private changed: (state: CalibrationEditorState) => void) {}
  get value(): CalibrationEditorState { return this.state; }
  private publish(): void { if (this.mounted) this.changed({ ...this.state }); }
  private feedback(text: string): void { this.state.feedback = text; this.publish(); }
  async mount(): Promise<void> {
    if (this.mounted) return;
    this.mounted = true; ++this.life;
    if (this.observationVisible) await this.startObservation();
    else this.publish();
  }
  /** Pause presentation work without discarding a draft or changing saved/output state. */
  setObservationVisible(visible: boolean): void {
    if (this.observationVisible === visible) return;
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
        } catch { this.state.available = false; this.cancelCenter(); this.feedback('校对状态暂不可用，未更改已保存参数'); }
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
    this.state.snapshot = snapshot; this.state.available = true;
    if (sourceChanged) this.cancelCenter();
    if (!this.state.dirty || sourceChanged) {
      this.baseline = copy(snapshot.config); this.state.draft = copy(snapshot.config);
      this.baseRevision = snapshot.inputRevision; this.state.dirty = false;
    }
    if (sourceChanged) { ++this.draftVersion; this.state.feedback = '手柄来源已变化，未应用的调整已丢弃'; }
    else if (!this.state.dirty && !this.state.applying && !this.state.sampling) this.state.feedback = '调整后点击应用';
    if (this.centerRequest && this.centerRequest.revision !== snapshot.inputRevision) this.cancelCenter();
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
      if (!this.mounted || life !== this.life || version !== this.draftVersion || !sameNullableSource(binding, this.state.snapshot?.source || null)) return;
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
      this.feedback('参数已保存，生效状态以当前通路回执为准');
    } catch (e) { if (this.mounted && life === this.life && version === this.draftVersion) this.feedback(e instanceof Error ? e.message : '应用失败，草稿已保留'); }
    finally { if (this.mounted && life === this.life) { this.state.applying = false; this.publish(); } }
  }
  async startCenter(side: StickSide): Promise<void> {
    const snapshot = this.state.snapshot;
    if (!this.mounted || this.state.applying || !this.state.available || !snapshot?.source || snapshot.capabilities.sticks !== 'standard-independent') return;
    if (!this.state.draft.enabled || !this.state.draft.sticksEnabled || !this.state.draft.sticks[side].enabled) return;
    if (this.state.sampling) { this.cancelCenter(); return; }
    const token = ++this.centerToken, life = this.life;
    this.centerRequest = { source: copy(snapshot.source), side, revision: snapshot.inputRevision };
    this.state.sampling = true; this.state.centerSide = side; this.feedback('请松开摇杆，正在采样');
    try {
      const ack = await this.port.startCenter({ protocol: AXIS_CALIBRATION_PROTOCOL, source: copy(snapshot.source), side, expectedInputRevision: snapshot.inputRevision });
      if (!ack || typeof ack.sessionId !== 'string' || !ack.sessionId) throw new Error('未收到有效校准会话');
      if (!this.mounted || life !== this.life || token !== this.centerToken) { void this.port.cancelCenter(ack.sessionId).catch(() => {}); return; }
      this.centerSession = ack.sessionId;
      const early = this.earlyCenter.get(ack.sessionId); this.earlyCenter.clear(); if (early) this.receiveCenter(early);
    } catch (e) { if (this.mounted && life === this.life && token === this.centerToken) { this.clearCenter(); this.feedback(e instanceof Error ? e.message : '校准未完成'); } }
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
    if (!receipt.ok) { this.clearCenter(); this.feedback('校准未完成，请松开摇杆后重试'); return; }
    if (!sameSource(request.source, receipt.source) || receipt.side !== request.side || receipt.expectedInputRevision !== request.revision ||
        !Number.isInteger(receipt.sampleCount) || receipt.sampleCount < 30 || !Number.isFinite(receipt.noiseP99) || receipt.noiseP99 < 0 || receipt.noiseP99 > .02 ||
        !Number.isFinite(receipt.centerX) || !Number.isFinite(receipt.centerY) || Math.hypot(receipt.centerX, receipt.centerY) > .15) {
      this.clearCenter(); this.feedback('校准结果不适用于当前手柄'); return;
    }
    this.clearCenter();
    this.update(config => { config.sticks[request.side].centerX = receipt.centerX; config.sticks[request.side].centerY = receipt.centerY; });
    this.feedback('轴心采样完成，点击应用后保存');
  }
  private clearCenter(): void { ++this.centerToken; this.centerSession = null; this.centerRequest = null; this.earlyCenter.clear(); this.state.sampling = false; this.state.centerSide = null; }
  cancelCenter(): void {
    const id = this.centerSession; this.clearCenter();
    if (id) void this.port.cancelCenter(id).catch(() => {});
    this.publish();
  }
  dispose(): void {
    if (!this.mounted) return;
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
  return {
    read: async () => parseCalibrationSnapshot(await invoke(CONTROLLER_CALIBRATION_COMMANDS.read, {}, { timeoutMs: 3000 })),
    apply: async request => {
      const receipt = await invoke<ApplyReceipt>(CONTROLLER_CALIBRATION_COMMANDS.apply, request, { timeoutMs: 10000 });
      if (!object(receipt) || typeof receipt.ok !== 'boolean' || typeof receipt.saved !== 'boolean' || typeof receipt.requestId !== 'string') throw new Error('应用回执无效');
      return receipt;
    },
    startCenter: request => invoke(CONTROLLER_CALIBRATION_COMMANDS.startCenter, request, { timeoutMs: 3000 }),
    cancelCenter: async sessionId => { await invoke(CONTROLLER_CALIBRATION_COMMANDS.cancelCenter, { sessionId }, { timeoutMs: 3000 }); },
    subscribe: listener => {
      // Known before the ACK: timeout/dispose can retire this exact watch even if the reply is lost.
      const requestedWatchId = `axis-watch-client:${crypto.randomUUID()}`;
      let disposed = false, failed = false, watchId: string | null = null;
      let earlySnapshot: ControllerCalibrationSnapshot | null = null;
      const stopped = new Set<string>();
      const stopWatch = (id: string) => {
        if (stopped.has(id)) return;
        stopped.add(id);
        void invoke(CONTROLLER_CALIBRATION_COMMANDS.observe, { active: false, watchId: id }, { timeoutMs: 3000 }).catch(() => {});
      };
      const unsubscribe = on<ControllerCalibrationSnapshot | CenterReceipt>(CONTROLLER_CALIBRATION_COMMANDS.event, event => {
        if (disposed || failed || !object(event)) return;
        if ('sessionId' in event) { listener(event); return; }
        // Watched snapshots belong to this exact page token; a slot/source match is insufficient.
        if (typeof event.observerId !== 'string') return;
        if (!watchId) { earlySnapshot = event; return; }
        if (event.observerId === watchId) listener(event);
      });
      void invoke<{ watchId: string }>(CONTROLLER_CALIBRATION_COMMANDS.observe, { active: true, watchId: requestedWatchId }, { timeoutMs: 3000 }).then(ack => {
        if (!ack || typeof ack.watchId !== 'string' || !ack.watchId) throw new Error('校对观测未建立');
        if (disposed) stopWatch(ack.watchId);
        else {
          watchId = ack.watchId;
          if (earlySnapshot && (earlySnapshot as unknown as { observerId: string }).observerId === watchId) listener(earlySnapshot);
          earlySnapshot = null;
        }
      }).catch(() => {
        failed = true; earlySnapshot = null; unsubscribe(); stopWatch(watchId || requestedWatchId);
        if (!disposed) listener({ observationUnavailable: true } as unknown as ControllerCalibrationSnapshot);
      });
      return () => { if (disposed) return; disposed = true; earlySnapshot = null; unsubscribe(); stopWatch(watchId || requestedWatchId); };
    },
  };
}
