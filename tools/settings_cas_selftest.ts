import { evaluateInputCas, evaluateInputCasWrite, normalizeSettings, type InputSettingsSnapshot } from '../src/bridge/settingsRepository';

const normalized = normalizeSettings({ input: { outputTarget: { persona: 'invalid', buttonMappingEnabled: 1 }, buttonMapping: { profileId: '' }, gyroMotion: { outputMode: 'invalid' } } });
// A13 同步（2026-09-20）：旧断言写 0 与当前实现对不上。此处更新为 1 的**依据是
// 三层一致性**（属"当前实现自洽"证据，193 §2 P2 第 4 条明确：一致性不自动等于历史设计
// 批准，故本用例只声明"默认值三层一致"，不主张设计批准）：
//   · settingsRepository DEFAULTS `innerDeadzone: 1`（settingsRepository.ts:256）；
//   · normalize/clamp 缺省回退 `finite(motion.innerDeadzone, 1, 0, 25)`（:407）——
//     注意 0 仍在合法区间内，只是**不是默认值**；
//   · native 读取回退 `readNumber(motion, "innerDeadzone", 1.0)`（main.cpp:6727）。
// 若将来三层中任意一层改变默认值，下面的断言会失败。
if (normalized.input.outputTarget.persona !== 'disabled' || normalized.input.outputTarget.buttonMappingEnabled !== false || normalized.input.buttonMapping.profileId !== 'default' || normalized.input.gyroMotion.outputMode !== 'disabled' || normalized.input.gyroMotion.calibrationId !== null || normalized.input.gyroMotion.motionInput !== 'local-space' || normalized.input.gyroMotion.motionMode !== 'off' || normalized.input.gyroMotion.gyroWeight !== 1.8 || normalized.input.gyroMotion.gyroMultiplier !== 1 || normalized.input.gyroMotion.motionSensitivityX !== 1.5 || normalized.input.gyroMotion.innerDeadzone !== 1 || normalized.input.gyroMotion.outerDeadzone !== 0 || normalized.input.gyroMotion.outputShape !== 'default') throw new Error('input default normalization failed');
const bounded = normalizeSettings({ input: { gyroMotion: { motionInput: 'bad', motionMode: 'bad', gyroMultiplier: 99, accelerometerMultiplier: -1, gyroThreshold: 1, motionSensitivityX: 9, motionSensitivityY: 0, gyroWeight: 99, velocityMode: 'bad', velocityScale: -2, innerDeadzone: 99, outerDeadzone: -4, outputShape: 'bad', axisOrder: 'bad', futureField: { keep: true } } } });
if (bounded.input.gyroMotion.motionInput !== 'local-space' || bounded.input.gyroMotion.motionMode !== 'off' || bounded.input.gyroMotion.gyroMultiplier !== 3 || bounded.input.gyroMotion.accelerometerMultiplier !== 0.1 || bounded.input.gyroMotion.gyroThreshold !== 124 || bounded.input.gyroMotion.motionSensitivityX !== 3 || bounded.input.gyroMotion.motionSensitivityY !== 0.1 || bounded.input.gyroMotion.gyroWeight !== 3 || bounded.input.gyroMotion.velocityMode !== 'default' || bounded.input.gyroMotion.velocityScale !== 0 || bounded.input.gyroMotion.innerDeadzone !== 25 || bounded.input.gyroMotion.outerDeadzone !== 0 || bounded.input.gyroMotion.outputShape !== 'default' || !bounded.input.gyroMotion.futureField.keep) throw new Error('HC motion parameter bounds or unknown-field retention failed');
// axisOrder is not an HC semantic (T22-BUS-P75) and must never survive normalization.
if ('axisOrder' in bounded.input.gyroMotion) throw new Error('dead axisOrder field must be dropped by normalization');
if ('axisMatrix' in bounded.input.gyroMotion) throw new Error('dead axisMatrix field must be dropped by normalization');
// deadzone is a legacy alias of innerDeadzone (UI read fallback GyroMotionView.vue:258,
// write sync :318, normalize sync :370). Both directions must stay self-consistent.
const legacyAlias = normalizeSettings({ input: { gyroMotion: { deadzone: 12 } } });
if (legacyAlias.input.gyroMotion.innerDeadzone !== 12 || legacyAlias.input.gyroMotion.deadzone !== 12) throw new Error('legacy deadzone alias must promote to innerDeadzone');
const aliasSync = normalizeSettings({ input: { gyroMotion: { innerDeadzone: 18 } } });
if (aliasSync.input.gyroMotion.innerDeadzone !== 18 || aliasSync.input.gyroMotion.deadzone !== 18) throw new Error('normalize must keep deadzone alias synced to innerDeadzone');
const aliasClamped = normalizeSettings({ input: { gyroMotion: { deadzone: 99 } } });
if (aliasClamped.input.gyroMotion.innerDeadzone !== 25 || aliasClamped.input.gyroMotion.deadzone !== 25) throw new Error('legacy deadzone alias must be clamped with innerDeadzone range');
// Native consumes innerDeadzone only (main.cpp:5239); the alias is a UI/durable-schema compat shim, never a second semantic.
const thresholdAboveDefault = normalizeSettings({ input: { gyroMotion: { gyroThreshold: 2400 } } });
if (thresholdAboveDefault.input.gyroMotion.gyroThreshold !== 2400) throw new Error('HC gyro threshold was incorrectly capped at its default 2000 dps');
// outputMode default must match native read fallback (main.cpp:5172): an absent
// field normalizes to 'disabled' so an old snapshot cannot silently enable motion.
const absentOutputMode = normalizeSettings({ input: { gyroMotion: { enabled: true } } });
if (absentOutputMode.input.gyroMotion.outputMode !== 'disabled') throw new Error('absent outputMode must normalize to disabled (matches native fallback)');

const current: InputSettingsSnapshot = {
  schemaVersion: 1, revision: 4, appliedRevision: null, appliedConfigHash: null, applyStatus: 'unknown',
  outputTarget: { persona: 'disabled', customFutureField: { keep: true } },
  buttonMapping: { rules: {} }, gyroMotion: { enabled: false },
  ownership: { mode: 'ymcc-semantic' }, diagnostics: { inputLoggingEnabled: false },
};
const stale = evaluateInputCas(current, 3, { outputTarget: { persona: 'xbox360' } });
if (stale.ok || stale.reason !== 'revision-conflict' || stale.value.outputTarget.persona !== 'disabled') throw new Error('stale revision was accepted');
const accepted = evaluateInputCas(current, 4, { outputTarget: { persona: 'xbox360' } });
if (!accepted.ok || accepted.revision !== 5 || accepted.value.outputTarget.persona !== 'xbox360' || !accepted.value.outputTarget.customFutureField.keep || accepted.value.appliedRevision !== null || accepted.value.applyStatus !== 'saved-pending' || !accepted.value.pendingConfigHash?.startsWith('sha256:') || accepted.value.hostAcknowledgedRevision !== null || accepted.value.hostAcknowledgedConfigHash !== null) throw new Error('CAS did not enter the T10 saved-pending state');
const replay = evaluateInputCas(accepted.value, 4, { diagnostics: { inputLoggingEnabled: true } });
if (replay.ok) throw new Error('old revision replay was accepted');
const failedWrite = evaluateInputCasWrite(current, 4, { outputTarget: { persona: 'xbox360' } }, false);
if (failedWrite.ok || failedWrite.reason !== 'write-failed' || failedWrite.currentRevision !== 4 || failedWrite.value.revision !== 4 || failedWrite.value.outputTarget.persona !== 'disabled' || !failedWrite.value.outputTarget.customFutureField.keep) throw new Error('failed CAS write mutated memory or reported success');
const retryAfterFailure = evaluateInputCasWrite(current, 4, { outputTarget: { persona: 'xbox360' } }, true);
if (!retryAfterFailure.ok || retryAfterFailure.revision !== 5 || retryAfterFailure.value.outputTarget.persona !== 'xbox360') throw new Error('CAS retry after failed write was not accepted');

// GP-FAMILY-CAPABILITY-4 R1-2：OEM→虚拟背键映射（OemRearMapV1）前端保存链。
// 归一化契约（settingsRepository.ts:327-341）：仅保留 left/right/both/off（trim+小写），
// 丢弃非法字符串与非字符串；无有效项则删除字段（未配置 = 零行为变化）。
const rearKept = normalizeSettings({ input: { outputTarget: { persona: 'steamdeck', oemRearMap: { m1: 'LEFT', m2: ' right ', l4: 'both', r4: 'off' } } } });
if (rearKept.input.outputTarget.oemRearMap?.m1 !== 'left' || rearKept.input.outputTarget.oemRearMap?.m2 !== 'right' || rearKept.input.outputTarget.oemRearMap?.l4 !== 'both' || rearKept.input.outputTarget.oemRearMap?.r4 !== 'off') throw new Error('oemRearMap valid values must be trimmed and lowercased');
const rearDropped = normalizeSettings({ input: { outputTarget: { persona: 'steamdeck', oemRearMap: { m1: 'invalid', home: 5, guide: null, special: {} } } } });
if ('oemRearMap' in rearDropped.input.outputTarget) throw new Error('oemRearMap with only invalid/non-string entries must be deleted');
const rearNonObject = normalizeSettings({ input: { outputTarget: { persona: 'steamdeck', oemRearMap: 'left' as unknown as Record<string, string> } } });
if ('oemRearMap' in rearNonObject.input.outputTarget) throw new Error('non-object oemRearMap must be deleted');
const rearDefaultEmpty = normalizeSettings({ input: { outputTarget: { persona: 'steamdeck', buttonMappingEnabled: true } } });
if ('oemRearMap' in rearDefaultEmpty.input.outputTarget) throw new Error('absent oemRearMap must stay absent (default empty config = zero behaviour change)');
const rearPartial = normalizeSettings({ input: { outputTarget: { persona: 'steamdeck', oemRearMap: { m1: 'left', m2: 'bad', l4: 'BOTH' } } } });
if (rearPartial.input.outputTarget.oemRearMap?.m1 !== 'left' || rearPartial.input.outputTarget.oemRearMap?.l4 !== 'both' || 'm2' in (rearPartial.input.outputTarget.oemRearMap || {})) throw new Error('oemRearMap must drop invalid entries while keeping valid ones');

// CAS 深合并（mergeSettings, settingsRepository.ts:284-289）：保存只带 outputTarget.oemRearMap，
// 不得覆盖同段落其它字段（persona/buttonMappingEnabled/未知未来字段）。
const rearBase: InputSettingsSnapshot = {
  schemaVersion: 1, revision: 7, appliedRevision: null, appliedConfigHash: null, applyStatus: 'unknown',
  outputTarget: { persona: 'steamdeck', buttonMappingEnabled: true, oemRearMap: { m1: 'left' }, customFutureField: { keep: true } },
  buttonMapping: { rules: {} }, gyroMotion: { enabled: false },
  ownership: { mode: 'ymcc-semantic' }, diagnostics: { inputLoggingEnabled: false },
};
const rearSaved = evaluateInputCas(rearBase, 7, { outputTarget: { oemRearMap: { m1: 'off', m2: 'both', l4: 'left', r4: 'right' } } });
if (!rearSaved.ok || rearSaved.revision !== 8 || rearSaved.value.applyStatus !== 'saved-pending') throw new Error('oemRearMap CAS save must enter saved-pending at revision+1');
if (rearSaved.value.outputTarget.persona !== 'steamdeck' || rearSaved.value.outputTarget.buttonMappingEnabled !== true || !rearSaved.value.outputTarget.customFutureField.keep) throw new Error('oemRearMap CAS patch must preserve sibling outputTarget fields');
if (rearSaved.value.outputTarget.oemRearMap?.m1 !== 'off' || rearSaved.value.outputTarget.oemRearMap?.m2 !== 'both' || rearSaved.value.outputTarget.oemRearMap?.l4 !== 'left' || rearSaved.value.outputTarget.oemRearMap?.r4 !== 'right') throw new Error('oemRearMap CAS patch values were not persisted');
const rearDeep = evaluateInputCas(rearBase, 7, { outputTarget: { oemRearMap: { m1: 'off' } } });
if (rearDeep.value.outputTarget.oemRearMap?.m1 !== 'off' || rearDeep.value.outputTarget.oemRearMap?.m2 !== rearBase.outputTarget.oemRearMap?.m2) throw new Error('oemRearMap CAS patch must deep-merge keys (unpatched keys retained)');
const rearRejected = evaluateInputCas(rearBase, 6, { outputTarget: { oemRearMap: { m1: 'left' } } });
if (rearRejected.ok || rearRejected.value.outputTarget.oemRearMap?.m1 !== 'left') throw new Error('stale oemRearMap CAS write must be rejected without mutation');
console.log('settings CAS selftest: PASS');
