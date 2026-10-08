import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

type Entry = { deltaId: string; runtimeClosure: string; safeStop: string; [key: string]: unknown };
const root = process.cwd();
const delta = JSON.parse(readFileSync(resolve(root, 'tools/CalibrationSafetyDelta.v1.json'), 'utf8')) as { schemaVersion: number; status: string; runtimeClosure: string; entries: Entry[] };
const native = readFileSync(resolve(root, 'native/main.cpp'), 'utf8');
const host = readFileSync(resolve(root, 'InputHost/Program.cs'), 'utf8');
const hc = readFileSync(resolve(root, '../../../Isolated/HC-Candidate-0.32.4.0-06c0b954-20260902/HandheldCompanion/Managers/SensorsManager.cs'), 'utf8');
const hcGyro = readFileSync(resolve(root, '../../../Isolated/HC-Candidate-0.32.4.0-06c0b954-20260902/HandheldCompanion/Sensors/IMUGyrometer.cs'), 'utf8');
const hcAccel = readFileSync(resolve(root, '../../../Isolated/HC-Candidate-0.32.4.0-06c0b954-20260902/HandheldCompanion/Sensors/IMUAccelerometer.cs'), 'utf8');
const hcGyroActions = readFileSync(resolve(root, '../../../Isolated/HC-Candidate-0.32.4.0-06c0b954-20260902/HandheldCompanion/Actions/GyroActions.cs'), 'utf8');
if (delta.schemaVersion !== 1 || delta.status !== 'SPEC-READY' || delta.runtimeClosure !== 'UNENCLOSED' || delta.entries.length !== 4) throw new Error('T16 delta must remain a four-row unclosed specification');
if (!hc.includes('gamepadMotion.ProcessMotion') || !hc.includes('SetCalibrationMode(CalibrationMode.Manual)')) throw new Error('T16 HC calibration reference was not located');
// The current YMCC lane does not admit HC's explicit calibration dialog/session.
// GamepadMotion remains in its HC-compatible Manual/zero-offset startup state,
// while pair/plane admission still prevents unproven motion from publishing.
// Do not make the static test depend on the retired candidate calibration block.
const captureStart = native.lastIndexOf('static void inputCaptureOnGamepad');
const captureEnd = native.indexOf('static void inputCaptureBusTick()', captureStart);
const captureBody = captureStart >= 0 && captureEnd > captureStart
  ? native.slice(captureStart, captureEnd)
  : '';
const calibrationSessionUnadmitted = native.includes('g_gmLocked = true;') &&
  native.includes('GamepadMotion starts in HC\'s default Manual mode') &&
  captureBody.includes('if (live && g_gm && g_gmProcess && g_gmGetCal && gyro && accel && pairProven)') &&
  !captureBody.includes('g_gmStartCal(') &&
  !captureBody.includes('g_gmPauseCal(') &&
  !captureBody.includes('g_gmSetOffset(') &&
  !captureBody.includes('g_gmSetMode(');
const calibrationTimeoutSafe = native.includes('const bool gamepadMotionPlaneProven = gyroSubmitAllowed') &&
  native.includes('pair-unproven-safe-zero') &&
  !captureBody.includes('calibrate-timeout');
const pairSafe = native.includes('const bool pairProven = g_realStickTest.enabled') &&
  native.includes('gyro && accel && pairProven') && native.includes('pair-unproven-safe-zero');
const powerConsumeStart = native.indexOf('static void inputHostConsumePowerRequests()');
const powerConsumeEnd = native.indexOf('static void inputHostPerformKeepAlive', powerConsumeStart);
const powerConsumeBody = powerConsumeStart >= 0 && powerConsumeEnd > powerConsumeStart
  ? native.slice(powerConsumeStart, powerConsumeEnd)
  : '';
// BUS consumes the generation and either queues the keep-alive no-write
// sleep interval (HC VirtualManager.SetSystemSleepState) or the ordered
// offline release. The blocking QUIESCE -> neutral -> RELEASE_TARGET ->
// SHUTDOWN/HidHide sequence belongs to the lifecycle owner, never to the
// 8 ms publication callback. Sleep keeps the Host/HIDMaestro controller and
// the physical hide intact (power-suspend-no-write), so "power-admission
// -revoked" is no longer a teardown reason.
const orderedLocalRelease = native.includes('inputHostRequestLocked("QUIESCE"') &&
  native.includes('"RELEASE_TARGET"') &&
  native.includes('inputHostQueueLifecycleWork("power-suspend-no-write")') &&
  native.includes('inputHostKeepAliveIfNeeded("power-suspend")') &&
  native.includes('inputHostStop(reason.c_str())') &&
  !powerConsumeBody.includes('inputHostStop(') &&
  !powerConsumeBody.includes('inputHostStart(');
// The standalone lane's physical-stick/gyro blend is not an invented YMCC
// curve: HC's GyroActions uses DefaultGyroWeight=1.2 and the same
// `gyroWeight - padNorm` shape in TouchpadActions.ApplyGyroOutput().
const hcGyroWeightBlend = !native.includes('hcApplyCustomSensitivity') &&
  hcGyroActions.includes('DefaultGyroWeight = 1.2f') &&
  native.includes('double gyroWeight = 1.2;') &&
  native.includes('g_realStickTest.gyroWeight = readNumber(motion, "gyroWeight", 1.2);') &&
  native.includes('const double weightFactor = g_realStickTest.gyroWeight - stickNorm');
const hcSelectorCoverage = native.includes('selectorId == "asus-rc73xa"') &&
  native.includes('selectorId == "asus-rc73ya"') &&
  native.includes('matrixIdentity = "hc-active-" + g_hcMotionSelector.selectorId');
const hcTriggerResolutionGate = native.includes('motionTriggerResolved') &&
  native.includes('configuredTrigger == "none"') &&
  native.includes('const bool nextMotionEnabled = nextMotionActionEnabled && targetGyroEnabled &&') &&
  native.includes('g_realStickTest.motionTriggerResolved = triggerResolved;') &&
  native.includes('if (!g_realStickTest.motionActionEnabled || !g_realStickTest.motionTriggerResolved) return false;');
const hcTimer = readFileSync(resolve(root, '../../../Isolated/HC-Candidate-0.32.4.0-06c0b954-20260902/HandheldCompanion/Managers/TimerManager.cs'), 'utf8');
const defaultSensorSelection = hcGyro.includes('Gyrometer.GetDefault()') && hcAccel.includes('Accelerometer.GetDefault()') &&
  hcGyro.includes('MinimumReportInterval') && hcAccel.includes('MinimumReportInterval') &&
  hc.includes('int UpdateInterval = TimerManager.GetPeriod()') && hcTimer.includes('DefaultMasterInterval = 8') &&
  native.includes('Gyrometer::GetDefault()') && native.includes('Accelerometer::GetDefault()') &&
  native.includes('kInputCaptureRequestedSensorIntervalMs = 8') && native.includes('MinimumReportInterval()') && native.includes('inputCaptureStopWinRtSensorsImpl()') &&
  native.includes('const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED)') && native.includes('mta-initialization-failed') &&
  !native.includes('GetSensorsByType(') && !native.includes('SetEventSink(');
const xinputEndpointNormalization = native.includes('inputHostNormalizeAxis') &&
  native.includes('static_assert(inputHostNormalizeAxis(-32768) == -1.0f)') &&
  native.includes('static_assert(inputHostNormalizeAxis(32767) == 1.0f)');
const powerRearmOrdering = native.includes('inputHostRequestPowerRelease') && native.includes('inputHostRequestPowerRearm') &&
  native.includes('inputHostConsumePowerRequests') && native.includes('inputHostRequestPowerRearm(generation, "power-resume-post-commit")') &&
  native.includes('inputHostRequestPowerRearm(generation, "sleep-intent-canceled")');
const busStart = native.indexOf('static void inputCaptureBusTick()');
const busPowerConsume = native.indexOf('inputHostConsumePowerRequests();', busStart);
const busPublicationDecision = native.indexOf('const bool publicationWanted', busStart);
const busPowerAdmissionOrdering = busStart >= 0 && busPowerConsume > busStart &&
  busPublicationDecision > busPowerConsume;
const captureWorkerStart = native.indexOf('static DWORD WINAPI inputCaptureThreadProc');
const captureLoopStart = native.indexOf('phase = "capture-loop"', captureWorkerStart);
const initialHostAdmissionOnBus = captureWorkerStart >= 0 && captureLoopStart > captureWorkerStart &&
  !native.slice(captureWorkerStart, captureLoopStart).includes('(void)inputHostStart();') &&
  native.includes('input-host-publication-deferred-to-bus') &&
  native.includes('await-first-coordinator-tick');
const hcVirtual = readFileSync(resolve(root, '../../../Isolated/HC-Candidate-0.32.4.0-06c0b954-20260902/HandheldCompanion/Managers/VirtualManager.cs'), 'utf8');
const submitPadStart = native.indexOf('static json inputHostSubmitPad');
const listeningGateStart = native.indexOf('if (g_shortcutRecordingActive.load(std::memory_order_acquire))', submitPadStart);
const submitFrameStart = native.indexOf('inputHostRequestLocked("SUBMIT_FRAME"', submitPadStart);
const listeningPublicationGate = hcVirtual.includes('if (InputsManager.IsListening)') && hcVirtual.includes('vTarget?.UpdateInputs(controllerState, gamepadMotion);') &&
  submitPadStart >= 0 && listeningGateStart >= submitPadStart &&
  native.includes('{"hostSubmission", "publication-suppressed"}') &&
  submitFrameStart > listeningGateStart;
const hcController = readFileSync(resolve(root, '../../../Isolated/HC-Candidate-0.32.4.0-06c0b954-20260902/HandheldCompanion/Managers/ControllerManager.cs'), 'utf8');
const sourceAbsentReason = native.indexOf('"publicationReason", "physical-source-absent"', submitPadStart);
const submitInvocation = native.indexOf('hostFrame = inputHostSubmitPad(', submitPadStart);
// F3 (2026-09-10): physical-source absence is a transient per-tick skip
// (suppressPublication = !physicalSourceAdmitted) matching HC's per-tick
// UpdateXInputState()==false -> no publish; the old session-level
// sourceAdmissionSuppressed marker was removed with its no-write session.
const physicalSourceNoWriteGate = hcController.includes('if (!HasTargetController || tc is null)') &&
  hcController.includes('if (isPhysical && ClearTargetIfMatch(controller.GetInstanceId()))') &&
  sourceAbsentReason > submitPadStart && submitFrameStart > sourceAbsentReason && submitInvocation >= 0 &&
  native.includes('suppressPublication = !physicalSourceAdmitted') &&
  !native.includes('sourceAdmissionSuppressed') &&
  native.includes('hostFrame = inputHostSubmitPad(') &&
  native.includes('            live,') &&
  native.includes('if (live && g_gm && g_gmProcess && g_gmGetCal && gyro && accel && pairProven)');
const coherentPhysicalSourceSnapshot = native.includes('struct InputCaptureGamepadSnapshot') &&
  native.includes('std::mutex g_inputCaptureGamepadSnapshotMx') &&
  native.includes('sourceSnapshot = g_inputCaptureGamepadSnapshot;') &&
  native.includes('g_inputCaptureGamepadSnapshot = {live, pad, g_xinputPrimarySlot, admittedMask, inputSource};') &&
  native.includes('inputCaptureOnGamepad(sourceSnapshot.live, sourceSnapshot.pad,') &&
  native.includes('g_inputCaptureGamepadSnapshot = {};');
// HC ControllerManager.XUsbDeviceArrived re-hides a reconnected selected
// controller (IsHidden -> Hide(false)). The reconcile worker re-applies the
// journaled hide from the connect path (base-container matched, current
// instance ids) instead of only logging drift; it never forms a session
// state, never blocks publication, and never touches a later-inserted source.
const reconnectedSourceRehide = hcController.includes('if (controller.IsHidden())') &&
  hcController.includes('controller.Hide(false)') &&
  native.includes('static bool hidHideReapplyReconnectedSource') &&
  native.includes('hidHideReapplyReconnectedSource(journal, currentVisibility)') &&
  native.includes('"hidhide.p-hid-reapplied"') &&
  native.includes('kInputHostReapplyHideCooldownMs') &&
  native.includes('prior.baseContainerDeviceInstanceId == physical.baseContainerDeviceInstanceId');
// A13 同步（2026-09-20，修订二；正反跑复核发现上一版断言仍引用已删除的 Host 协议命令）：
// 原断言（两条）要求 native 存在续租实现 `static void inputHostKeepAliveIfNeeded` 与两个
// per-tick 调用点；上一版只改了 native 侧，**Host 侧三条（`case "KEEPALIVE":` 等）仍为真值
// 假设**——但 **F3-B（第八十一批 B-变体）已把 KEEPALIVE 从 Host 协议整条删除**
// （`Program.cs:31/33/320/331/1919`：无 KEEPALIVE 命令、无租约超时自杀、协议白名单已移除），
// 于是 `keepAliveCaseStart = -1` ⇒ 该组合恒假。现按**当前真实且更强**的边界重写：
//   · Host 协议里不再出现该命令字面量，未知命令统一走 `unknown-command` 拒绝；
//   · native 续租实现与两个 per-tick 调用点**保持删除**（若被重新引入立即失败）；
//   · 不存在把"缺物理源/录音"误命名为 fault 的字面量。
// **已知跨文件不一致（上报，不单方面改产品）**：native 协议自测仍期待 KEEPALIVE 被拒时
// 返回 `state-rejected`（`main.cpp:18078`），而 Host 现在对未知命令回 `protocol-error`
// （detail=`unknown-command`，`Program.cs:1923` + 调用点 `:943`）⇒ 该自测在运行时判负；
// 与 192 §3.3 的 `--inputhost-protocol-selftest` fail-build-tree 崩溃同域，交裁决处理。
const noWriteLeaseTransport = !host.includes('"KEEPALIVE"') &&
  host.includes('error = "unknown-command"') &&
  !native.includes('static void inputHostKeepAliveIfNeeded') &&
  !native.includes('inputHostKeepAliveIfNeeded("shortcut-recording")') &&
  !native.includes('inputHostKeepAliveIfNeeded("backend-fault-hold")') &&
  // F3/F4 (2026-09-10): HC parity — physical-source absence is a transient
  // per-tick skip, never a keepalive/session fault. (2026-09-20: the lease-renewal
  // call sites were removed entirely by F3-B, see above; only the "must never be a
  // fault" half of this guard remains applicable.)
  !native.includes('physical-source-absent-keepalive-fault') &&
  !native.includes('shortcut-recording-keepalive-fault');
const hcSensorLifecycle = readFileSync(resolve(root, '../../../Isolated/HC-Candidate-0.32.4.0-06c0b954-20260902/HandheldCompanion/Managers/SensorsManager.cs'), 'utf8');
// 2026-09-10: WinRT sensors run on the dedicated STA sensor thread
// (SensorThreadRequest::Start/Stop/Suspended/Resume, HC Dispatcher pump
// parity); the Impl-prefixed functions are the STA-thread implementations.
const sensorPowerLifecycle = hcSensorLifecycle.includes('StopListening();') &&
  hcSensorLifecycle.includes('Gyrometer?.UpdateSensor();') &&
  native.includes('inputCaptureSuspendSensorsForPower()') &&
  native.includes('inputCaptureResumeSensorsForPower()') &&
  native.includes('inputCaptureStopWinRtSensorsImpl()') &&
  native.includes('inputCaptureStartWinRtSensorsImpl()') &&
  native.includes('SensorThreadRequest::Suspended') &&
  native.includes('SensorThreadRequest::Resume') &&
  native.includes('"kind", "sensor-suspend"') &&
  native.includes('"kind", "sensor-resume"');
const releaseNeutralResultChecked = host.includes('if (!TrySubmitNeutral(out var neutralDetail))') &&
  host.includes('errors.Add("neutral:" + (string.IsNullOrWhiteSpace(neutralDetail) ? "failed" : neutralDetail))');
const staleHostHandleGuard = native.includes('WaitForSingleObject(g_inputHostProcess, 0)') &&
  native.includes('inputHostStopLocked("host-already-exited")') &&
  native.includes('input-host.stale-process-handle');
// 具名标志表：任一守卫失败时打印**具体哪一项**漂移（原先只有笼统信息，188 ④ 的
// 归因只能人工二分）。语义与原组合断言完全一致。
const safetyFlags: Record<string, boolean> = {
  calibrationSessionUnadmitted,
  calibrationTimeoutSafe,
  pairSafe,
  orderedLocalRelease,
  hcGyroWeightBlend,
  hcSelectorCoverage,
  hcTriggerResolutionGate,
  defaultSensorSelection,
  xinputEndpointNormalization,
  powerRearmOrdering,
  busPowerAdmissionOrdering,
  initialHostAdmissionOnBus,
  listeningPublicationGate,
  physicalSourceNoWriteGate,
  coherentPhysicalSourceSnapshot,
  noWriteLeaseTransport,
  reconnectedSourceRehide,
  sensorPowerLifecycle,
  releaseNeutralResultChecked,
  staleHostHandleGuard,
};
const driftedFlags = Object.entries(safetyFlags).filter(([, ok]) => !ok).map(([name]) => name);
if (driftedFlags.length > 0) {
  throw new Error(`T10/T11 static source safety boundary changed: ${driftedFlags.join(', ')}; review before admitting any runtime path`);
}
if (delta.entries.some((entry) => entry.runtimeClosure !== 'UNENCLOSED' || !entry.safeStop)) throw new Error('T16 entry is missing safe-stop or claims runtime closure');
const evidence = {
  evidenceId: 'T16-E01-CALIBRATION-SAFETY-DELTA-STATIC-AUDIT-20260905', status: 'SPEC-READY', systemMutation: false,
  assertions: { hcProcessAndManualLocated: true, explicitCalibrationSessionUnadmitted: true, calibrationTimeoutCannotAdmit: true, unpairedMotionSafeZero: true, orderedLocalRelease: true, hcGyroWeightBlend: true, hcSelectorCoverage: true, hcTriggerResolutionGate: true, hcDefaultSensorSelection: true, xinputEndpointNormalization: true, powerReleaseAndNewEpochRearm: true, hcPowerConsumePrecedesBusPublication: true, hcInitialTargetAdmissionOnBus: true, hcListeningPublicationSuppression: true, hcPhysicalSourceNoWrite: true, coherentPhysicalSourceSnapshot: true, noWriteLeaseTransport: true, reconnectedSourceRehide: true, hcSensorSuspendResume: true, releaseNeutralResultChecked: true, staleHostHandleGuard: true, runtimeClosure: 'UNENCLOSED' },
};
const output = resolve(root, '../../Build/Validation/GyroVirtual/T16-E01-calibration-safety-delta-static-audit-20260905.json');
mkdirSync(resolve(output, '..'), { recursive: true });
writeFileSync(output, JSON.stringify(evidence, null, 2));
console.log(`calibration safety delta selftest: PASS (${output})`);
