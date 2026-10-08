export type ClosureState = 'CLOSED' | 'UNENCLOSED' | 'NOT-OBSERVED' | 'NOT-APPLICABLE';
export type InputPersona = 'disabled' | 'dualshock4' | 'xbox360' | 'steamdeck' | 'dualsense' | 'elite' | 'dualsense-edge';

export interface OutputTargetConfigV1 { schemaVersion: 1; revision: number; closure: ClosureState; persona: InputPersona; descriptorHash: string | null; buttonMappingEnabled: boolean; gyroEnabled: boolean; visibilityPolicy: 'hidden' | 'project-only' | 'unavailable'; /** 背键映射 Phase A（背键映射A任务书 2026-09-14 S5）：字段保留（旧包兼容）；**2026-09-24 operator 裁决后不再参与门控**——背键映射改为「人格支持即常开」（steamdeck/elite/dualsense-edge，真 Deck 仍不挂）。 */ backMapping: boolean; /** GP-FAMILY-CAPABILITY-4 R1-2：OEM→虚拟背键映射（类型已接入输出配置）。可空、默认空；缺省 ⇒ 零映射、行为不变。键名 = canonical OEM key，值 = left/right/both/off。 */ oemRearMap?: OemRearMapV1; }
/** GP-FAMILY-CAPABILITY-4 C4-D2：通用 OEM→rear mapper（可空、默认空）。
 *  键名 = canonical OEM key（nativeKeyboardShortcutMaskBit 的 bit20+ 段，如 m1/m2/
 *  ckbig/home/guide/l4/r4…）；值 = 目标虚拟背键槽：left（bit0/L5）、right（bit1/R5）、
 *  both；off/未知 = 该键显式不映射。未声明 ⇒ 零映射（现状不变）。native 侧仅接受
 *  OEM 位作源，且输出仅在 persona ∈ {steamdeck, elite, dualsense-edge} 且门控开启时
 *  经帧侧 backButtons 生效。 */
export interface OemRearMapV1 { [canonicalOemKey: string]: 'left' | 'right' | 'both' | 'off'; }
export interface ButtonMappingConfigV1 { schemaVersion: 1; revision: number; closure: ClosureState; profileId: string; rules: Record<string, unknown>; }
export interface GyroMotionConfigV1 {
  schemaVersion: 1; revision: number; closure: ClosureState;
  /** Effective motion gate. A persisted false value must never preview/publish gyro. */
  enabled?: boolean;
  provider: string | null; unit: string | null; calibrationId: string | null;
  outputMode: 'virtual-stick' | 'ds4-imu' | 'disabled';
  motionInput?: 'local-space' | 'player-space' | 'world-space' | 'joystick-steering';
  /** HC Profile.SteeringAxis (Roll/Yaw/Auto). Roll keeps LocalSpace (Z,X);
   *  Yaw applies SwapYawRoll so LocalSpace consumes (-Y,X); Auto resolves by
   *  sensor family (Windows/SerialUSBIMU -> Yaw, Controller -> accelerometer). */
  steeringAxis?: 'roll' | 'yaw' | 'auto';
  motionMode?: 'off' | 'on' | 'toggle'; motionTrigger?: string | null;
  gyroMultiplier?: number; accelerometerMultiplier?: number;
  gyroThreshold?: number; motionSensitivityX?: number; motionSensitivityY?: number;
  motionSensitivityArray?: ReadonlyArray<readonly [number, number]>;
  gyroWeight?: number; outputShape?: 'default' | 'circle' | 'cross' | 'square';
  velocityMode?: 'default' | 'velocity'; velocityScale?: number;
  innerDeadzone?: number; outerDeadzone?: number; deadzone?: number; antiDeadzone?: number;
  /** HC AxisActions.ResponseCurvePoints (AxisActions.cs:36-44): 6-point [x,y]
   *  control curve applied in ApplyAxisModifiers after anti-deadzone, before
   *  output shape. Default is the linear identity curve (no-op). */
  responseCurvePoints?: ReadonlyArray<readonly [number, number]>;
  steeringMaxAngle?: number; steeringPower?: number; steeringDeadzone?: number;
  /** HC MotionManager.cs:296-298: when aiming trigger is held, motion output is
   *  multiplied by AimingSightsMultiplier (default 1.0 = no-op). */
  aimingSightsMultiplier?: number; aimingSightsTrigger?: string | null;
  invertHorizontal?: boolean; invertVertical?: boolean;
  autoCalibrate?: boolean;
  /** 用户裁决（2026-09-29）：虚拟手柄联动——虚拟手柄开启时联动启动陀螺仪，
   *  关闭时一并关闭。默认 true。顶部专属配置的手柄/陀螺仪开关优先于此。 */
  virtualPadLink?: boolean;
  /** 'off' remains valid for old snapshots; current UI uses enabled + fps/racing/custom/steam. */
  preset?: 'off' | 'fps' | 'racing' | 'custom' | 'steam';
  activePreset?: 'fps' | 'racing' | 'custom' | 'steam';
  presets?: Record<string, Record<string, unknown> | null>;
  outputStick?: 'left' | 'right'; outputAxis?: 'xy' | 'x';
}
export interface GamepadMotionPlaneTelemetryV1 {
  // This is an explicit producer claim for the HC MotionManager input plane.
  // It is not a HID/Steam/game-consumer observation.
  processProven: true; pairProven: true; calibrationLocked: true;
  steeringAxis: 'roll' | 'yaw';
  defaultGyro: { x: number; y: number; z: number };
  defaultAccel: { x: number; y: number; z: number };
  /** GamepadMotion's processed/linear acceleration; never the gravity vector. */
  linearAcceleration?: { x: number; y: number; z: number };
  orientation?: { w: number; x: number; y: number; z: number };
  playerSpace?: { x: number; y: number };
  worldSpace?: { x: number; y: number };
}
/** Native MotionManager-stage diagnostics. This is producer telemetry only. */
export interface MotionManagerTelemetryV1 {
  mapped: boolean; triggered: boolean; velocityMode: boolean;
  /** Pre-LayoutManager contribution in HC short-axis domain. */
  gyroShort: { x: number; y: number };
}
/** Native LayoutManager gyro-action merge diagnostics. This is producer telemetry only. */
export interface LayoutManagerTelemetryV1 {
  physicalStick: { x: number; y: number };
  stickNorm: number; gyroWeight: number; weightFactor: number;
  mergeOrder: 'physical-stick-plus-motion-contribution';
}
export interface GyroTelemetryV1 {
  schemaVersion: 1; sequence: number; timestampUtc: string;
  /** Native sensor binding receipt: a default Windows gyrometer is actually
   *  present (main.cpp inputCaptureStartWinRtSensors). Not a motion admission. */
  sensorPresent?: boolean;
  gyro: { x: number; y: number; z: number };
  playerSpace?: { x: number; y: number };
  worldSpace?: { x: number; y: number };
  accel?: { x: number; y: number; z: number };
  rawAccel?: { x: number; y: number; z: number };
  gravity?: { x: number; y: number; z: number };
  processedAcceleration?: { x: number; y: number; z: number };
  linearAcceleration?: { x: number; y: number; z: number };
  orientation?: { w: number; x: number; y: number; z: number };
  // `gyro`/`accel` above are diagnostic telemetry, not an HC Default plane.
  // A local/player/world/steering preview may use only this admitted plane.
  gamepadMotionPlane?: GamepadMotionPlaneTelemetryV1;
  output: { x: number; y: number };
  targetStick?: 'left' | 'right';
  leftStick?: { x: number; y: number };
  rightStick?: { x: number; y: number };
  motionAdmission?: 'hc-motion-admitted' | 'safe-zero' | 'unknown';
  /** Native triggerResolved (main.cpp:9779): configured trigger name must be in
   *  the XInput name set or empty; false means the name fails closed (zero
   *  contribution in every mode, §1.25). Lets the UI distinguish "valid trigger
   *  waiting for press" from "invalid trigger name needs fixing". */
  motionTriggerResolved?: boolean;
  /** Native aimingSights trigger resolution (same vocabulary, §1.25). */
  aimingSightsTriggerResolved?: boolean;
  /** MotionManager contribution before the physical-stick merge, normalized to [-1, 1]. */
  motionContribution?: { x: number; y: number };
  /** LayoutManager contribution after its gyro-weight / physical-stick merge factor. */
  layoutContribution?: { x: number; y: number };
  motionManager?: MotionManagerTelemetryV1;
  layoutManager?: LayoutManagerTelemetryV1;
  // The native canonical pipe frame is a Host-local receipt only. It is never
  // a HID, Steam, or game-consumer observation.
  outputKind: 'unknown' | 'native-canonical-pipe-frame';
  hostFrame?: {
    hostSubmission: 'not-running' | 'not-admitted-or-failed' | 'publication-suppressed' | 'frame-accepted';
    // A suppressed frame never crossed the canonical writer boundary. This
    // identifies the local admission reason; it is not an external target or
    // consumer observation.
    publicationReason?: 'physical-source-absent' | 'shortcut-recording';
    hostActive: boolean;
    firstFrame: boolean;
    lifecycle: 'none' | 'opening' | 'host-acknowledged' | 'target-prepared' | 'neutralized' | 'host-active' | 'quiescing' | 'released-local' | 'release-unproven' | 'faulted';
    persona: string;
    targetStick?: 'left' | 'right';
    leftStick: { x: number; y: number };
    rightStick: { x: number; y: number };
    gyroDps: { x: number; y: number; z: number };
    motionAdmission?: 'hc-motion-admitted' | 'safe-zero' | 'unknown';
  };
}
export interface InputRouteEvidenceV1 { schemaVersion: 1; revision: number; closure: ClosureState; routeId: string; physicalControlId: string; hcParityId: string | null; owner: string | null; releaseProofId: string | null; }
export interface OemTriggerRuleV1 { schemaVersion: 1; revision: number; closure: ClosureState; sku: string; firmware: string | null; physicalInstance: string | null; usage: string | null; rawCode: string; claimOwner: string | null; fallback: 'unhandled' | 'ymcc-semantic' | 'unavailable'; evidenceHash: string | null; }
export interface ReleaseProofV1 { schemaVersion: 1; revision: number; closure: ClosureState; frameAdmissionStopped: boolean; neutralSent: boolean; readerStopped: boolean; targetRemoved: boolean; hidhideDiffRestored: boolean; handlesDisposed: boolean; workerJoined: boolean; }
export interface InputCapabilityManifestV1 { schemaVersion: 1; revision: number; closure: ClosureState; hostIdentity: string | null; hcAssetId: string | null; externalAssets: Record<string, string | null>; capabilities: Record<string, boolean>; realRuntimeAuthorized: boolean; pageVisibility: 'hidden' | 'disabled' | 'available'; updaterExcluded: boolean; }
export interface InputDiagnosticRecordV1 {
  schemaVersion: 1; revision: number; closure: ClosureState;
  timestampUtc: string; timestampLocal: string;
  source: 'settings-ui' | 'button-mapping' | 'gyro-motion' | 'input-host' | 'coordinator';
  eventName: string; operation: string;
  runId: string | null; hostInstanceId: string | null; processId: number | null;
  nativeGeneration: number | null; inputEpoch: number | null; configRevision: number | null;
  configHash: string | null; targetId: string | null; persona: InputPersona | null;
  lifecyclePhase: string | null; transportResult: 'ok' | 'rejected' | 'unknown';
  callbackResult: 'ok' | 'rejected' | 'unknown'; readback: string | null;
  physicalObservation: 'observed' | 'not-observed' | 'not-applicable' | 'unknown';
  error: string | null; releaseProofId: string | null;
}
export interface HCParityLedgerV1 { schemaVersion: 1; revision: number; closure: ClosureState; parityId: string; domain: 'controller' | 'button' | 'motion' | 'route' | 'persona' | 'visibility' | 'lifecycle'; hcCommit: string; sourcePath: string; symbol: string; observedOrder: string[]; yemanOwner: string | null; parityClass: 'exact' | 'adapted' | 'unknown'; rationale: string; fixtureOrTrace: string | null; status: 'located' | 'verified' | 'blocked'; }

export type InputContractValidation = { ok: true } | { ok: false; reason: 'schema-invalid' | 'revision-invalid' | 'persona-conflict' | 'asset-missing' | 'parity-missing' | 'unclosed' };

export function normalizeGyroTelemetry(value: unknown): GyroTelemetryV1 | null {
  if (!value || typeof value !== 'object') return null;
  const raw = value as { gyro?: Record<string, unknown>; sensorPresent?: unknown; playerSpace?: Record<string, unknown>; worldSpace?: Record<string, unknown>; accel?: Record<string, unknown>; rawAccel?: Record<string, unknown>; gravity?: Record<string, unknown>; processedAcceleration?: Record<string, unknown>; linearAcceleration?: Record<string, unknown>; orientation?: Record<string, unknown>; gamepadMotionPlane?: Record<string, unknown>; output?: Record<string, unknown>; outputKind?: unknown; targetStick?: unknown; leftStick?: Record<string, unknown>; rightStick?: Record<string, unknown>; motionAdmission?: unknown; motionTriggerResolved?: unknown; aimingSightsTriggerResolved?: unknown; motionContribution?: Record<string, unknown>; layoutContribution?: Record<string, unknown>; motionManager?: Record<string, unknown>; layoutManager?: Record<string, unknown>; hostFrame?: Record<string, unknown>; sequence?: unknown; timestampUtc?: unknown; timestamp?: unknown };
  if (!raw.gyro && !raw.output) return null;
  const finite = (input: unknown): number => {
    const numeric = Number(input);
    return Number.isFinite(numeric) ? numeric : 0;
  };
  const clampUnit = (input: unknown): number => Math.max(-1, Math.min(1, finite(input)));
  const sequence = Number.isSafeInteger(raw.sequence) && (raw.sequence as number) >= 0 ? raw.sequence as number : 0;
  // A renderer receipt time is not sensor provenance. Preserve an unknown or
  // malformed producer timestamp as unknown rather than silently replacing it
  // with the browser's current UTC time. In particular, a supposedly admitted
  // GamepadMotion plane cannot cross this boundary without a real telemetry
  // timestamp that the receiving side can parse.
  const timestampCandidate = typeof raw.timestampUtc === 'string'
    ? raw.timestampUtc.trim()
    : typeof raw.timestamp === 'string' ? raw.timestamp.trim() : '';
  const timestamp = Number.isFinite(Date.parse(timestampCandidate)) ? timestampCandidate : '';
  const space = (input: Record<string, unknown> | undefined) => input ? { x: finite(input.x), y: finite(input.y) } : undefined;
  const strictVector3 = (input: unknown): { x: number; y: number; z: number } | undefined => {
    if (!input || typeof input !== 'object') return undefined;
    const vector = input as Record<string, unknown>;
    if (![vector.x, vector.y, vector.z].every((item) => typeof item === 'number' && Number.isFinite(item))) return undefined;
    return { x: vector.x as number, y: vector.y as number, z: vector.z as number };
  };
  const strictSpace = (input: unknown): { x: number; y: number } | undefined => {
    if (!input || typeof input !== 'object') return undefined;
    const vector = input as Record<string, unknown>;
    if (![vector.x, vector.y].every((item) => typeof item === 'number' && Number.isFinite(item))) return undefined;
    return { x: vector.x as number, y: vector.y as number };
  };
  const strictBoolean = (input: unknown): boolean | undefined => typeof input === 'boolean' ? input : undefined;
  const rawMotionManager = raw.motionManager;
  const motionManager = rawMotionManager
    && strictBoolean(rawMotionManager.mapped) !== undefined
    && strictBoolean(rawMotionManager.triggered) !== undefined
    && strictBoolean(rawMotionManager.velocityMode) !== undefined
    && strictSpace(rawMotionManager.gyroShort)
    ? {
        mapped: strictBoolean(rawMotionManager.mapped)!,
        triggered: strictBoolean(rawMotionManager.triggered)!,
        velocityMode: strictBoolean(rawMotionManager.velocityMode)!,
        gyroShort: strictSpace(rawMotionManager.gyroShort)!,
      }
    : undefined;
  const rawLayoutManager = raw.layoutManager;
  const rawPhysicalStick = strictSpace(rawLayoutManager?.physicalStick);
  const rawStickNorm = Number(rawLayoutManager?.stickNorm);
  const rawGyroWeight = Number(rawLayoutManager?.gyroWeight);
  const rawWeightFactor = Number(rawLayoutManager?.weightFactor);
  const layoutManager = rawLayoutManager
    && rawPhysicalStick
    && [rawStickNorm, rawGyroWeight, rawWeightFactor].every(Number.isFinite)
    && rawLayoutManager.mergeOrder === 'physical-stick-plus-motion-contribution'
    ? {
        physicalStick: { x: clampUnit(rawPhysicalStick.x), y: clampUnit(rawPhysicalStick.y) },
        stickNorm: Math.max(0, Math.min(1, rawStickNorm)),
        gyroWeight: Math.max(1, Math.min(2, rawGyroWeight)),
        weightFactor: Math.max(0, Math.min(2, rawWeightFactor)),
        mergeOrder: 'physical-stick-plus-motion-contribution' as const,
      }
    : undefined;
  const rawPlane = raw.gamepadMotionPlane;
  const defaultGyro = strictVector3(rawPlane?.defaultGyro);
  const defaultAccel = strictVector3(rawPlane?.defaultAccel);
  const linearAcceleration = strictVector3(rawPlane?.linearAcceleration);
  const rawOrientation = rawPlane?.orientation as Record<string, unknown> | undefined;
  const orientation = rawOrientation && typeof rawOrientation === 'object' &&
    [rawOrientation.w, rawOrientation.x, rawOrientation.y, rawOrientation.z].every((item) => typeof item === 'number' && Number.isFinite(item))
    ? { w: rawOrientation.w as number, x: rawOrientation.x as number, y: rawOrientation.y as number, z: rawOrientation.z as number }
    : undefined;
  const planeEligible = timestamp.length > 0
    && rawPlane?.processProven === true
    && rawPlane?.pairProven === true
    && rawPlane?.calibrationLocked === true
    && (rawPlane?.steeringAxis === 'roll' || rawPlane?.steeringAxis === 'yaw')
    && !!defaultGyro
    && !!defaultAccel;
  const gamepadMotionPlane = planeEligible && defaultGyro && defaultAccel
    ? {
        processProven: true as const,
        pairProven: true as const,
        calibrationLocked: true as const,
         steeringAxis: rawPlane.steeringAxis as 'roll' | 'yaw',
         defaultGyro,
         defaultAccel,
         linearAcceleration,
         orientation,
        playerSpace: strictSpace(rawPlane.playerSpace),
        worldSpace: strictSpace(rawPlane.worldSpace),
      }
    : undefined;
  const hostState = raw.hostFrame?.hostSubmission;
  const hostSubmission: 'not-running' | 'not-admitted-or-failed' | 'publication-suppressed' | 'frame-accepted' | null =
    hostState === 'not-running' || hostState === 'not-admitted-or-failed' || hostState === 'publication-suppressed' || hostState === 'frame-accepted' ? hostState : null;
  const publicationReason: NonNullable<GyroTelemetryV1['hostFrame']>['publicationReason'] =
    raw.hostFrame?.publicationReason === 'physical-source-absent'
      ? 'physical-source-absent'
      : raw.hostFrame?.publicationReason === 'shortcut-recording'
        ? 'shortcut-recording'
        : undefined;
  const lifecycle = raw.hostFrame?.lifecycle;
  const hostTargetStick: 'left' | 'right' | undefined = raw.hostFrame?.targetStick === 'left'
    ? 'left'
    : raw.hostFrame?.targetStick === 'right' ? 'right' : undefined;
  type HostLifecycle = NonNullable<GyroTelemetryV1['hostFrame']>['lifecycle'];
  const validLifecycle: HostLifecycle = lifecycle === 'none' || lifecycle === 'opening' || lifecycle === 'host-acknowledged' ||
    lifecycle === 'target-prepared' || lifecycle === 'neutralized' || lifecycle === 'host-active' ||
    lifecycle === 'quiescing' || lifecycle === 'released-local' || lifecycle === 'release-unproven' || lifecycle === 'faulted'
    ? lifecycle as HostLifecycle
    : 'none';
  const hostFrame = hostSubmission
      ? {
        hostSubmission,
        publicationReason,
        hostActive: raw.hostFrame?.hostActive === true,
        firstFrame: raw.hostFrame?.firstFrame === true,
        lifecycle: validLifecycle,
        persona: typeof raw.hostFrame?.persona === 'string' ? raw.hostFrame.persona : 'unknown',
        targetStick: hostTargetStick,
        leftStick: { x: clampUnit((raw.hostFrame?.leftStick as Record<string, unknown> | undefined)?.x), y: clampUnit((raw.hostFrame?.leftStick as Record<string, unknown> | undefined)?.y) },
        rightStick: { x: clampUnit((raw.hostFrame?.rightStick as Record<string, unknown> | undefined)?.x), y: clampUnit((raw.hostFrame?.rightStick as Record<string, unknown> | undefined)?.y) },
        gyroDps: { x: finite((raw.hostFrame?.gyroDps as Record<string, unknown> | undefined)?.x), y: finite((raw.hostFrame?.gyroDps as Record<string, unknown> | undefined)?.y), z: finite((raw.hostFrame?.gyroDps as Record<string, unknown> | undefined)?.z) },
        motionAdmission: raw.hostFrame?.motionAdmission === 'hc-motion-admitted' ? 'hc-motion-admitted' as const : raw.hostFrame?.motionAdmission === 'safe-zero' ? 'safe-zero' as const : 'unknown' as const,
      }
    : undefined;
  const targetStick = raw.targetStick === 'left' || raw.targetStick === 'right' ? raw.targetStick : undefined;
  const leftStick = raw.leftStick ? { x: clampUnit(raw.leftStick.x), y: clampUnit(raw.leftStick.y) } : undefined;
  const rightStick = raw.rightStick ? { x: clampUnit(raw.rightStick.x), y: clampUnit(raw.rightStick.y) } : undefined;
  const motionAdmission = raw.motionAdmission === 'hc-motion-admitted' ? 'hc-motion-admitted' as const : raw.motionAdmission === 'safe-zero' ? 'safe-zero' as const : 'unknown' as const;
  const motionTriggerResolved = typeof raw.motionTriggerResolved === 'boolean' ? raw.motionTriggerResolved : undefined;
  const aimingSightsTriggerResolved = typeof raw.aimingSightsTriggerResolved === 'boolean' ? raw.aimingSightsTriggerResolved : undefined;
  const rawAccel = strictVector3(raw.rawAccel);
  const gravity = strictVector3(raw.gravity);
  const processedAcceleration = strictVector3(raw.processedAcceleration);
  // 去重（2026-09-17 日志重铸第一批）：native telemetry 已删除恒等的
  // linearAcceleration 字段（与 processedAcceleration 同源），此处复用同一
  // 向量，保持下游契约字段（linearAcceleration）不变。
  const linearAccelerationTelemetry = processedAcceleration;
  const rawTelemetryOrientation = raw.orientation as Record<string, unknown> | undefined;
  const orientationTelemetry = rawTelemetryOrientation && typeof rawTelemetryOrientation === 'object' &&
    [rawTelemetryOrientation.w, rawTelemetryOrientation.x, rawTelemetryOrientation.y, rawTelemetryOrientation.z].every((item) => typeof item === 'number' && Number.isFinite(item))
    ? { w: rawTelemetryOrientation.w as number, x: rawTelemetryOrientation.x as number, y: rawTelemetryOrientation.y as number, z: rawTelemetryOrientation.z as number }
    : undefined;
  return { schemaVersion: 1, sequence, timestampUtc: timestamp, sensorPresent: raw.sensorPresent === true, gyro: { x: finite(raw.gyro?.x), y: finite(raw.gyro?.y), z: finite(raw.gyro?.z) }, playerSpace: space(raw.playerSpace as Record<string, unknown> | undefined), worldSpace: space(raw.worldSpace as Record<string, unknown> | undefined), accel: raw.accel ? { x: finite(raw.accel.x), y: finite(raw.accel.y), z: finite(raw.accel.z) } : undefined, rawAccel, gravity, processedAcceleration, linearAcceleration: linearAccelerationTelemetry, orientation: orientationTelemetry, gamepadMotionPlane, output: { x: clampUnit(raw.output?.x), y: clampUnit(raw.output?.y) }, outputKind: raw.outputKind === 'native-canonical-pipe-frame' ? 'native-canonical-pipe-frame' : 'unknown', targetStick, leftStick, rightStick, motionAdmission, motionTriggerResolved, aimingSightsTriggerResolved, motionContribution: raw.motionContribution ? { x: clampUnit(raw.motionContribution.x), y: clampUnit(raw.motionContribution.y) } : undefined, layoutContribution: raw.layoutContribution ? { x: clampUnit(raw.layoutContribution.x), y: clampUnit(raw.layoutContribution.y) } : undefined, motionManager, layoutManager, hostFrame };
}

export function validateInputSnapshot(snapshot: { schemaVersion?: unknown; revision?: unknown; outputTarget?: Partial<OutputTargetConfigV1>; gyroMotion?: Partial<GyroMotionConfigV1>; capability?: Partial<InputCapabilityManifestV1> }): InputContractValidation {
  if (snapshot.schemaVersion !== 1 || !Number.isSafeInteger(snapshot.revision) || (snapshot.revision as number) < 0) return { ok: false, reason: 'revision-invalid' };
  const target = snapshot.outputTarget;
  if (!target || target.schemaVersion !== 1 || !['disabled', 'dualshock4', 'xbox360', 'steamdeck', 'dualsense', 'elite', 'dualsense-edge'].includes(target.persona as string)) return { ok: false, reason: 'schema-invalid' };
  if (target.persona === 'disabled' && (target.buttonMappingEnabled || target.gyroEnabled)) return { ok: false, reason: 'persona-conflict' };
  if (target.persona !== 'disabled' && !target.descriptorHash && target.closure === 'CLOSED') return { ok: false, reason: 'asset-missing' };
  const motion = snapshot.gyroMotion;
  const motionEnabled = motion && typeof motion.enabled === 'boolean' ? motion.enabled : undefined;
  if (motionEnabled !== undefined && motionEnabled !== target.gyroEnabled) return { ok: false, reason: 'persona-conflict' };
  if (target.gyroEnabled && (motionEnabled !== true || motion?.closure !== 'CLOSED')) return { ok: false, reason: 'parity-missing' };
  // HC has no Xbox 360 IMU report persona. Virtual-stick gyro is a separate
  // action contribution; selecting the DS4 IMU transport for X360 is invalid.
  if (target.persona === 'xbox360' && motion?.outputMode === 'ds4-imu') return { ok: false, reason: 'persona-conflict' };
  if (motion?.closure === 'CLOSED' && (!motion.provider || motion.unit !== 'deg/s' || !motion.calibrationId)) return { ok: false, reason: 'parity-missing' };
  if (snapshot.capability?.realRuntimeAuthorized && !snapshot.capability.hcAssetId) return { ok: false, reason: 'asset-missing' };
  if (target.closure !== 'CLOSED' || snapshot.gyroMotion?.closure === 'UNENCLOSED') return { ok: false, reason: 'unclosed' };
  return { ok: true };
}
