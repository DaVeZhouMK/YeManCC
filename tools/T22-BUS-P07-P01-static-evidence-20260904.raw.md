# T22-P01 raw static evidence

capturedUtc: 2026-09-04T15:04:02.7381973Z
cwd: G:\YeManCC-Work
runtimeOperation: false

## Schema fields
argv: Get-Content -LiteralPath G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\src\bridge\inputContracts.ts, lines 1-21
exitCode: 0
stdout:
`
1:export type ClosureState = 'CLOSED' | 'UNENCLOSED' | 'NOT-OBSERVED' | 'NOT-APPLICABLE';
2:export type InputPersona = 'disabled' | 'dualshock4' | 'xbox360';
3:
4:export interface OutputTargetConfigV1 { schemaVersion: 1; revision: number; closure: ClosureState; persona: InputPersona; descriptorHash: string | null; buttonMappingEnabled: boolean; gyroEnabled: boolean; visibilityPolicy: 'hidden' | 'project-only' | 'unavailable'; }
5:export interface ButtonMappingConfigV1 { schemaVersion: 1; revision: number; closure: ClosureState; profileId: string; rules: Record<string, unknown>; }
6:export interface GyroMotionConfigV1 {
7:  schemaVersion: 1; revision: number; closure: ClosureState;
8:  provider: string | null; unit: string | null; calibrationId: string | null;
9:  outputMode: 'virtual-stick' | 'ds4-imu' | 'disabled';
10:  motionInput?: 'local-space' | 'player-space' | 'world-space' | 'joystick-steering';
11:  motionMode?: 'off' | 'on' | 'toggle'; motionTrigger?: string | null;
12:  gyroMultiplier?: number; accelerometerMultiplier?: number;
13:  gyroThreshold?: number; motionSensitivityX?: number; motionSensitivityY?: number;
14:  motionSensitivityArray?: ReadonlyArray<readonly [number, number]>;
15:  gyroWeight?: number; outputShape?: 'default' | 'circle' | 'cross' | 'square';
16:  velocityMode?: 'default' | 'velocity'; velocityScale?: number;
17:  innerDeadzone?: number; outerDeadzone?: number; deadzone?: number; antiDeadzone?: number;
18:  steeringMaxAngle?: number; steeringPower?: number; steeringDeadzone?: number;
19:  invertHorizontal?: boolean; invertVertical?: boolean; axisOrder?: 'XYZ' | 'XZY' | 'YXZ' | 'YZX' | 'ZXY' | 'ZYX';
20:  preset?: 'fps' | 'racing' | 'custom'; outputStick?: 'left' | 'right'; outputAxis?: 'xy' | 'x';
21:}
`
stderr:

## Input config hash
argv: Get-Content -LiteralPath G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\src\bridge\inputConfigHash.ts, lines 1-18 and 114-117
exitCode: 0
stdout:
`
1:/**
2: * T10 configuration fingerprinting.
3: *
4: * The host must bind a durable, normalized input configuration rather than a
5: * UI patch.  Apply/ACK bookkeeping is deliberately excluded: it describes a
6: * Host observation of the configuration and would otherwise change the hash
7: * after an ACK without any user-config mutation.
8: */
9:export const INPUT_CONFIG_HASH_SCHEMA = 'InputConfigHash.v1';
10:
11:const APPLICATION_BOOKKEEPING_KEYS = new Set([
12:  'appliedRevision',
13:  'appliedConfigHash',
14:  'pendingConfigHash',
15:  'hostAcknowledgedRevision',
16:  'hostAcknowledgedConfigHash',
17:  'applyStatus',
18:]);
114:export function computeInputConfigHash(input: Record<string, unknown>): string {
115:  const projection = projectInputConfiguration(input);
116:  const canonical = canonicalizeInputConfiguration({ schema: INPUT_CONFIG_HASH_SCHEMA, input: projection });
117:  return `sha256:${sha256Hex(canonical)}`;
`
stderr:

## Pure activation / exact ACK candidate
argv: Get-Content -LiteralPath G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\src\bridge\gyroConfigActivation.ts, lines 16-43 and 98-148
exitCode: 0
stdout:
`
16:/** Fields shared by every T10 request, ACK, neutral barrier, and first frame. */
17:export interface GyroConfigActivationIdentityV1 {
18:  runId: string;
19:  requestId: string;
20:  epoch: number;
21:  powerGeneration: number;
22:  owner: GyroConfigActivationOwner;
23:  targetId: string | null;
24:  persona: InputPersona;
25:  configRevision: number;
26:  configHash: string;
27:  providerId: string | null;
28:  physicalIdentity: string | null;
29:  calibrationId: string | null;
30:  visibilityTransactionId: string | null;
31:  sampleSequence: number | null;
32:  timestampUtc: string;
33:}
34:
35:export interface GyroConfigActivationRequestV1 extends GyroConfigActivationIdentityV1 {
36:  schemaVersion: 1;
37:  neutralProofId: string | null;
38:}
39:
40:export interface GyroConfigHostAckV1 extends GyroConfigActivationIdentityV1 {
41:  schemaVersion: 1;
42:  hostInstanceId: string;
43:  neutralProofId: string;
98:/**
99: * Pure T10 coordinator slice. It intentionally owns no process or report
100: * handle: a future native Host adapter must feed its neutral proof, exact ACK,
101: * and same-generation first frame through this transaction before output is
102: * published. This makes stale ACK/frame handling replayable without devices.
103: */
104:export class GyroConfigActivation {
105:  private phase: GyroConfigActivationPhase = 'idle';
106:  private request: GyroConfigActivationRequestV1 | null = null;
107:  private hostInstanceId: string | null = null;
108:  private lastActiveSampleSequence: number | null = null;
109:  private readonly traces: GyroConfigActivationTraceV1[] = [];
110:
111:  requestActivation(request: GyroConfigActivationRequestV1): Outcome {
112:    if (!validIdentity(request)) return this.reject(request, 'request-invalid');
113:    this.phase = 'accepted-pending';
114:    this.request = { ...request };
115:    this.hostInstanceId = null;
116:    this.lastActiveSampleSequence = null;
117:    return this.record('accepted-pending', 'request-accepted', null, request.neutralProofId, null);
118:  }
119:
120:  confirmNeutral(requestId: string, neutralProofId: string): Outcome {
121:    const expected = this.request;
122:    if (!expected || this.phase !== 'accepted-pending' || requestId !== expected.requestId || !neutralProofId) {
123:      return this.transitionSafeStop(expected, 'neutral-proof-missing-or-stale');
124:    }
125:    this.phase = 'quiesced';
126:    this.request = { ...expected, neutralProofId };
127:    return this.record('quiesced', 'neutral-confirmed', null, neutralProofId, null);
128:  }
129:
130:  acknowledge(ack: GyroConfigHostAckV1): Outcome {
131:    const expected = this.request;
132:    if (!expected || this.phase !== 'quiesced') return this.reject(ack, 'ack-without-quiesce');
133:    if (!validIdentity(ack) || !ack.hostInstanceId || !ack.neutralProofId || !sameGeneration(expected, ack) || ack.neutralProofId !== expected.neutralProofId) {
134:      // A stale ACK must not destroy a newer saved-pending request. The UI
135:      // retains it; this trace gives the stale Host a deterministic rejection.
136:      return this.reject(ack, 'stale-ack-rejected');
137:    }
138:    this.phase = 'acknowledged';
139:    this.hostInstanceId = ack.hostInstanceId;
140:    return this.record('acknowledged', 'host-acknowledged', null, ack.neutralProofId, ack.hostInstanceId);
141:  }
142:
143:  publishFirstFrame(frame: GyroConfigFirstFrameV1): Outcome {
144:    const expected = this.request;
145:    if (!expected || this.phase !== 'acknowledged') return this.transitionSafeStop(expected, 'first-frame-before-ack');
146:    const sampleValid = frame.sampleSequence !== null && frame.sampleSequence >= 0 &&
147:      finiteVector(frame.mapped) && frame.report.length !== null && frame.report.length > 0 && !!frame.report.hash;
148:    if (!validIdentity(frame) || !sameGeneration(expected, frame) || !sampleValid) {
`
stderr:

## Product native state writer
argv: Get-Content -LiteralPath G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\main.cpp, lines 5272-5307
exitCode: 0
stdout:
`
5272:    if (v > 32767.0) return 32767;
5273:    if (v < -32768.0) return (short)-32768;
5274:    return (short)v;
5275:}
5276:static float hcApplyCustomSensitivity(float angularValue, float maxValue) {
5277:    // EXACT_HC InputUtils.ApplyCustomSensitivity + Profile default MotionSensivityArray=0.5 * 2.
5278:    const float posAbs = fabsf(angularValue / maxValue);
5279:    if (posAbs <= 0.f) return 0.f;
5280:    if (posAbs >= 1.f) return 1.f;
5281:    return 1.f;
5282:}
5283:static void inputHostSubmitPad(const XINPUT_GAMEPAD& pad, double cgx, double cgy, double cgz, float psx, float psy, float wsx, float wsy, bool steady, bool calLocked) {
5284:    if (!g_inputHostProcess) return;
5285:    (void)cgy; (void)psx; (void)psy; (void)wsx; (void)wsy;
5286:    // EXACT_HC MotionManager LocalSpace: (Z,X), curve against IMUCalibration.thresholdG=2000, then GetSensitivity*1000.
5287:    double outX = cgz;
5288:    double outY = cgx;
5289:    outX *= hcApplyCustomSensitivity((float)outX, 2000.f);
5290:    outY *= hcApplyCustomSensitivity((float)outY, 2000.f);
5291:    outX *= 1000.0;
5292:    outY *= 1000.0;
5293:    // EXACT_HC SensorsManager calibration is modal: layout/virtual output is not consumed until calibrate finishes.
5294:    if (!calLocked) { outX = 0; outY = 0; }
5295:    const double baseX = pad.sThumbRX;
5296:    const double baseY = pad.sThumbRY;
5297:    const double stickNorm = (std::min)(1.0, hypot(baseX, baseY) / 32767.0);
5298:    const double weight = 1.2 - stickNorm;
5299:    const short rx = inputHostClampShort(baseX + outX * weight);
5300:    const short ry = inputHostClampShort(baseY + outY * weight);
5301:    inputHostWrite({
5302:        {"alive", true},
5303:        {"buttons", pad.wButtons},
5304:        {"lx", pad.sThumbLX / 32767.0},
5305:        {"ly", pad.sThumbLY / 32767.0},
5306:        {"rx", rx / 32767.0},
5307:        {"ry", ry / 32767.0},
`
stderr:

## Product InputHost parser/sink
argv: Get-Content -LiteralPath G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\InputHost\Program.cs, lines 116-162
exitCode: 0
stdout:
`
116:        ctx.LoadDefaultProfiles();
117:        try { ctx.InstallDriver(); } catch { }
118:        var profile = ctx.GetProfile(persona) ?? throw new InvalidOperationException("profile-missing:" + persona);
119:        controller = ctx.CreateController(profile);
120:    }
121:
122:    static void Submit(JsonElement root)
123:    {
124:        if (controller is null) throw new InvalidOperationException("not-ready");
125:        var buttons = root.TryGetProperty("buttons", out var b) && b.ValueKind == JsonValueKind.Number ? b.GetInt32() : 0;
126:        var profile = ctx!.GetProfile(persona)!;
127:        var state = new HMGamepadState
128:        {
129:            Buttons = MapButtons(buttons),
130:            Hat = MapHat(buttons),
131:            Axes = HMGamepadStateHelpers.StandardAxes(profile, GetF(root, "lx"), GetF(root, "ly"), GetF(root, "rx"), GetF(root, "ry"), GetF(root, "lt"), GetF(root, "rt")),
132:            GyroDpsX = GetF(root, "gx"),
133:            GyroDpsY = GetF(root, "gy"),
134:            GyroDpsZ = GetF(root, "gz"),
135:        };
136:        controller.SubmitState(in state);
137:    }
138:
139:    static void SubmitNeutral()
140:    {
141:        if (controller is null) return;
142:        var profile = ctx!.GetProfile(persona)!;
143:        var state = new HMGamepadState
144:        {
145:            Buttons = HMButton.None,
146:            Hat = HMHat.None,
147:            Axes = HMGamepadStateHelpers.StandardAxes(profile, 0, 0, 0, 0, 0, 0),
148:        };
149:        controller.SubmitState(in state);
150:    }
151:
152:    static void Release()
153:    {
154:        try { SubmitNeutral(); } catch { }
155:        try { controller?.Dispose(); } catch { }
156:        controller = null;
157:        try { ctx?.Dispose(); } catch { }
158:        ctx = null;
159:    }
160:
161:    static float GetF(JsonElement root, string name) =>
162:        root.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.Number ? v.GetSingle() : 0f;
`
stderr:
