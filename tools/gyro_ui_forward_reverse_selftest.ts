/**
 * UI 陀螺仪功能正反推演闭环测试（todo 31 UI↔HC 生命周期闭环 U1–U21）。
 *
 * 对 GyroMotionView.vue 的每个持久控件做：
 *   正向推演：UI 配置 → HC 公式（mapGyroSample / hcInputUtils）→ 期望输出；
 *   反向推演：从输出反推输入/公式参数，验证与原始配置或输入一致（1e-6 容差）。
 *
 * 行为判断依据 31-UI-GYRO-HC-LIFECYCLE-CLOSURE-20260909.md §3/§4。
 * flipping 边界（sensor 单调、trigger 状态机、velocity 消耗-衰减）逐项包含反推验证。
 */
import { mapGyroSample, type GyroGateContext } from '../src/bridge/gyroMotionMapperMock';
import {
  applyCustomSensitivity, applyHcGyroAxisModifiers, applyResponseCurve, blendGyroShortIntoStick, createDefaultMotionSensitivityArray, getHcSensitivity, hcClampToShort,
  hcInclinationAngles, hcShortToUnit, hcSteering, hcUnitToShort,
  HC_GYRO_THRESHOLD_DPS, HC_DEFAULT_RESPONSE_CURVE, HC_VELOCITY_DECAY, HC_VELOCITY_FPS,
  type ResponseCurvePoint,
} from '../src/bridge/hcInputUtils';
import { assembleCanonicalFrame } from '../src/bridge/virtualReportAssembler';
import { normalizeSettings } from '../src/bridge/settingsRepository';
import type { GyroMotionConfigV1 } from '../src/bridge/inputContracts';
import { normalizeGyroTelemetry } from '../src/bridge/inputContracts';
import {
  commitCurveRows,
  curveRowsIssue,
  defaultResponseCurvePairs,
  defaultSensitivityCurvePairs,
  draftCurveRows,
  type CurveRow,
  type EditableCurve,
} from '../src/bridge/gyroCurveEdit';

const context = { runId: 'ui-fwd-rev', epoch: 1, powerGeneration: 1, targetId: 't', configRevision: 1 };
const base: GyroMotionConfigV1 = {
  schemaVersion: 1 as const, revision: 1, closure: 'CLOSED' as const, provider: 'fixture', unit: 'deg/s',
  calibrationId: 'c', outputMode: 'virtual-stick' as const, motionMode: 'on' as const, motionInput: 'local-space' as const,
  gyroMultiplier: 1, accelerometerMultiplier: 1, motionSensitivityX: 1, motionSensitivityY: 1,
  invertHorizontal: false, invertVertical: false,
};
const nodes = createDefaultMotionSensitivityArray();
const near = (a: number, b: number, eps = 1e-6) => Math.abs(a - b) < eps;

function plane(gyro: { x: number; y: number; z: number }, accel = { x: 0, y: 0, z: 1 }, extra: { playerSpace?: { x: number; y: number }; worldSpace?: { x: number; y: number } } = {}) {
  return { processProven: true as const, pairProven: true as const, calibrationLocked: true as const, steeringAxis: 'roll' as const, defaultGyro: gyro, defaultAccel: accel, ...extra };
}
function sample(gyro: { x: number; y: number; z: number }, seq: number, ts: number, deltaSeconds?: number) {
  return { sequence: seq, timestamp: ts, deltaSeconds, gamepadMotionPlane: plane(gyro) };
}
function forwardLocal(cfg: GyroMotionConfigV1, g: { x: number; y: number; z: number }) {
  // HC MotionManager.ProcessMotion LocalSpace = (Z, X) with CustomSensitivity then ×Sensitivity.
  let x = g.z, y = g.x;
  if (cfg.invertHorizontal) x *= -1;
  if (cfg.invertVertical) y *= -1;
  x *= applyCustomSensitivity(x, HC_GYRO_THRESHOLD_DPS, cfg.motionSensitivityArray ?? nodes);
  y *= applyCustomSensitivity(y, HC_GYRO_THRESHOLD_DPS, cfg.motionSensitivityArray ?? nodes);
  x *= (cfg.gyroMultiplier ?? 1) * getHcSensitivity(cfg.motionSensitivityX);
  y *= (cfg.gyroMultiplier ?? 1) * getHcSensitivity(cfg.motionSensitivityY);
  return { x: hcClampToShort(x), y: hcClampToShort(y) };
}

// ---------------------------------------------------------------- U1 preset（fps/racing 展开语义）
{
  // UI applyPreset 把 fps/racing 展开为一组持久字段（GyroMotionView.vue applyPreset）：
  //   fps    -> enabled, local-space, motionMode=on(常开), right stick（2026-09-29 用户裁决：默认常开）
  //   racing -> enabled, joystick-steering, motionMode=on, 无触发, left stick, outputAxis=x
  // 本用例验证展开后的字段组合在 mapper 层单向一致（正向推演），并反向确认
  // 触发门控差异（反向推演：显式带 RT 触发 key 的同配置缺触发=零）。
  // 注：fpsBase 是 mapper 层触发门控 fixture，故意保留 motionMode=off + RT 以验证
  //    「同配置缺触发=零」，与 UI 新默认（常开）不冲突。
  const fpsBase = { ...base, preset: 'fps' as const, motionInput: 'local-space' as const, motionMode: 'off' as const, motionTrigger: 'RT', outputStick: 'right' as const };
  const fpsIdle = mapGyroSample(context, fpsBase, sample({ x: 40, y: 0, z: 20 }, 60, 60));
  if (!fpsIdle.ok || fpsIdle.frame.contribution.x !== 0 || fpsIdle.frame.contribution.y !== 0) {
    throw new Error('U1 fwd: fps preset idle (RT not pressed) must be zero');
  }
  const fpsHeld = mapGyroSample(context, fpsBase, sample({ x: 40, y: 0, z: 20 }, 61, 61), { pressed: new Set(['RT']) });
  const fpsWant = forwardLocal({ ...fpsBase, invertVertical: false }, { x: 40, y: 0, z: 20 });
  if (!fpsHeld.ok || fpsHeld.frame.gyroShort!.y !== fpsWant.y) {
    throw new Error(`U1 fwd: fps preset RT-held output mismatch got=${JSON.stringify(fpsHeld.frame.gyroShort)} want=${fpsWant.y}`);
  }
  const racingBase = { ...base, preset: 'racing' as const, motionInput: 'joystick-steering' as const, motionMode: 'on' as const, motionTrigger: '' as const, outputStick: 'left' as const, outputAxis: 'x' as const, invertVertical: false, invertHorizontal: false };
  const tilt = { x: -Math.tan(20 * Math.PI / 180), y: 0, z: 1 };
  const racing = mapGyroSample(context, racingBase, { sequence: 62, timestamp: 62, gamepadMotionPlane: plane({ x: 0, y: 0, z: 0 }, tilt) });
  const wantRacing = hcClampToShort(hcSteering(hcInclinationAngles(tilt).y, 30, 1, 0) * applyCustomSensitivity(hcSteering(hcInclinationAngles(tilt).y, 30, 1, 0), HC_GYRO_THRESHOLD_DPS, nodes));
  if (!racing.ok || racing.frame.gyroShort!.x !== wantRacing || racing.frame.gyroShort!.y !== 0) {
    throw new Error(`U1 fwd: racing preset steering mismatch got=${JSON.stringify(racing.frame.gyroShort)} want=${wantRacing}`);
  }
  // 反向推演: preset 本身只是 UI 组合（非持久独立语义），持久化后仅保留展开字段。
  const persisted = normalizeSettings({ input: { gyroMotion: { preset: 'racing', motorInput: 'x' } } });
  if (persisted.input.gyroMotion.preset !== 'racing') throw new Error('U1 reverse: preset persists as UI combo marker');
  // 'off' 是主开关预设（UI 关闭时持久化 enabled=false），归一化白名单必须保留该值。
  const offPersisted = normalizeSettings({ input: { gyroMotion: { preset: 'off' } } });
  if (offPersisted.input.gyroMotion.preset !== 'off') throw new Error('U1 reverse: off preset must persist');
  const steamPersisted = normalizeSettings({ input: { gyroMotion: { preset: 'steam' } } });
  if (steamPersisted.input.gyroMotion.preset !== 'steam') throw new Error('U1 reverse: steam preset must persist');
  // per-preset gyroMode 断言已删除（2026-09-29 用户裁决）：该字段是 legacy 死字段，
  // 原生与 mapper 均不消费，normalizeSettings / capturePresetParams / presetStore
  // 三处主动 delete 是既定产品行为，故不再要求其持久化。
}

// ---------------------------------------------------------------- U2 enabled
{
  const off = mapGyroSample(context, { ...base, enabled: false }, sample({ x: 50, y: 0, z: 100 }, 1, 1));
  if (!off.ok || off.frame.contribution.x !== 0 || off.frame.contribution.y !== 0) {
    throw new Error('U2 fwd: disabled must be zero');
  }
  const on = mapGyroSample(context, { ...base, enabled: true }, sample({ x: 50, y: 0, z: 100 }, 2, 2));
  if (!on.ok || on.frame.contribution.x === 0) throw new Error('U2 fwd: enabled must contribute');
}

// ---------------------------------------------------------------- U4 gyroMultiplier
{
  // HC 将 GyrometerMultiplier 应用于 SetupMotion（MotionManager.cs:110）阶段，
  // 产生 Default 平面后 ProcessMotion 才消费。mapGyroSample 是 post-SetupMotion fixture
  // （Default/Player/World 平面已含 multiplier），因此这里用预乘的 Default 平面做正向与
  // 反向验证：同一 raw 输入、multiplier 2× → 平面值 2× → 最终输出线性 2×（未钳位区域）。
  const raw = { x: 0, y: 0, z: 5 };
  const one = mapGyroSample(context, base, sample({ ...raw, z: raw.z * 1 }, 3, 3));
  const two = mapGyroSample(context, base, sample({ ...raw, z: raw.z * 2 }, 4, 4));
  if (!one.ok || !two.ok) throw new Error('U4 base map failed');
  const ratio = two.frame.gyroShort!.x / one.frame.gyroShort!.x;
  if (!near(ratio, 2, 5e-2)) throw new Error(`U4 reverse: multiplier ratio=${ratio} want 2`);
  // 正向：与"HC Formula 直接在各层组合"等价（Sensitivity 线性段 1:1）。
  const wantOne = hcClampToShort(raw.z * applyCustomSensitivity(raw.z, HC_GYRO_THRESHOLD_DPS, nodes) * getHcSensitivity(1));
  const wantTwo = hcClampToShort(raw.z * 2 * applyCustomSensitivity(raw.z * 2, HC_GYRO_THRESHOLD_DPS, nodes) * getHcSensitivity(1));
  if (one.frame.gyroShort!.x !== wantOne || two.frame.gyroShort!.x !== wantTwo) {
    throw new Error(`U4 fwd mismatch one=${one.frame.gyroShort!.x} want=${wantOne} two=${two.frame.gyroShort!.x} want=${wantTwo}`);
  }
}

// ---------------------------------------------------------------- U4b accelerometerMultiplier（defaultAccel 平面 + steering 角度不变）
{
  // HC SetupMotion 将 AccelerometerMultiplier 应用于 Default accel 平面
  // （MotionManager.cs:113），再供 Inclination/JoystickSteering 消费
  // （MotionManager.cs:237-238）。atan(ax/len) 各轴同乘 multiplier 后角度不变——
  // 因此同一倾角在乘数不同时 steering 输出必须一致（HC 语义），而 defaultAccel
  // 平面数值线性缩放（native main.cpp:9638-9641 构造，9677-9684 defaultAccel）。
  const tilt = { x: -Math.tan(15 * Math.PI / 180), y: 0, z: 1 };
  const cfg = { ...base, motionInput: 'joystick-steering' as const, invertVertical: false, invertHorizontal: false };
  const m1 = mapGyroSample(context, cfg, { sequence: 50, timestamp: 50, gamepadMotionPlane: plane({ x: 0, y: 0, z: 0 }, tilt) });
  const m2 = mapGyroSample(context, cfg, { sequence: 51, timestamp: 51, gamepadMotionPlane: plane({ x: 0, y: 0, z: 0 }, { x: tilt.x * 2, y: 0, z: 2 }) });
  if (!m1.ok || !m2.ok) throw new Error('U4b base map failed');
  // 反向推演：乘数不影响 steering 输出（角度不变）。
  if (m1.frame.gyroShort!.x !== m2.frame.gyroShort!.x || m1.frame.gyroShort!.y !== m2.frame.gyroShort!.y) {
    throw new Error(`U4b reverse: steering angle must be multiplier-invariant got1=${m1.frame.gyroShort!.x} got2=${m2.frame.gyroShort!.x}`);
  }
  // HC 平面构造（native）已线性缩放 defaultAccel：验证合法乘数范围归一化（0.1..3）。
  const lo = normalizeSettings({ input: { gyroMotion: { accelerometerMultiplier: 0.05 } } });
  const hi = normalizeSettings({ input: { gyroMotion: { accelerometerMultiplier: 9 } } });
  if (lo.input.gyroMotion.accelerometerMultiplier !== 0.1 || hi.input.gyroMotion.accelerometerMultiplier !== 3) {
    throw new Error('U4b fwd: accelerometerMultiplier bounds 0.1..3 normalization failed');
  }
}

// ---------------------------------------------------------------- U5/U6 sensitivity X/Y
{
  // LocalSpace 输出为 (Z, X)：水平敏感度作用于 Z（outX），垂直敏感度作用于 X（outY）。
  const g = { x: 8, y: 0, z: 10 };
  const cfg = { ...base, invertHorizontal: false, invertVertical: false };
  const m1 = mapGyroSample(context, cfg, sample(g, 5, 5));
  const m2 = mapGyroSample(context, { ...cfg, motionSensitivityX: 2, motionSensitivityY: 3 }, sample(g, 6, 6));
  if (!m1.ok || !m2.ok) throw new Error('U5/U6 base map failed');
  // 反向推演：outX 放大 2x（sensitivityX）；outY 放大 3x（sensitivityY）。
  if (!near(m2.frame.gyroShort!.x / m1.frame.gyroShort!.x, 2, 5e-2) ||
      !near(m2.frame.gyroShort!.y / m1.frame.gyroShort!.y, 3, 5e-2)) {
    throw new Error(`U5/U6 reverse scaling failed x=${(m2.frame.gyroShort!.x / m1.frame.gyroShort!.x)} y=${(m2.frame.gyroShort!.y / m1.frame.gyroShort!.y)}`);
  }
  const e = { x: hcClampToShort(g.z * applyCustomSensitivity(g.z, HC_GYRO_THRESHOLD_DPS, nodes) * getHcSensitivity(2)), y: hcClampToShort(g.x * applyCustomSensitivity(g.x, HC_GYRO_THRESHOLD_DPS, nodes) * getHcSensitivity(3)) };
  if (m2.frame.gyroShort!.x !== e.x || m2.frame.gyroShort!.y !== e.y) throw new Error('U5/U6 fwd mismatch vs HC formula');
}

// ---------------------------------------------------------------- U5B 自定义敏感曲线（motionSensitivityArray）
{
  // HC Profile.MotionSensivityArray 是 SortedDictionary，editor 生成 49 个 1/48 节点，
  // ProcessMotion 用 InputUtils.ApplyCustomSensitivity（InputUtils.cs:491-520）。
  // 同一输入、不同曲线必须产生不同短轴；曲线节点 key 升序、value∈[0,1]。
  const g = { x: 0, y: 0, z: 4000 }; // 归一化后 |z|/2000 = 2.0（posAbs 饱和段）
  const flatCurve: [number, number][] = [[0, 0.5], [1, 0.5]]; // 恒 0.5
  const steepCurve: [number, number][] = [[0, 0.2], [1, 1.0]]; // 末端 1.0
  const cfg = { ...base, motionSensitivityArray: flatCurve as unknown as ReadonlyArray<readonly [number, number]>, invertVertical: false, invertHorizontal: false };
  const flatTest = mapGyroSample(context, cfg, sample(g, 70, 70));
  const steepTest = mapGyroSample(context, { ...cfg, motionSensitivityArray: steepCurve as unknown as ReadonlyArray<readonly [number, number]> }, sample(g, 71, 71));
  if (!flatTest.ok || !steepTest.ok) throw new Error('U5B base map failed');
  // 反向推演：posAbs=2.0 >= 1 时 ApplyCustomSensitivity 恒返回 1.0，两条曲线输出必须相同。
  if (flatTest.frame.gyroShort!.x !== steepTest.frame.gyroShort!.x) {
    throw new Error('U5B reverse: saturating input must be curve-invariant');
  }
  // 正向推演（未饱和段）：输入 z=5（posAbs=0.0025，short=5×1000×cs≈3747/5833 未饱和）。
  // bump 曲线在中段 value 高于 flat，输出短轴必须更大。
  const midFlat = mapGyroSample(context, cfg, sample({ x: 0, y: 0, z: 5 }, 72, 72));
  const bumpCurve: [number, number][] = [[0, 0.5], [0.5, 1.0], [1, 0.5]]; // 中段峰顶 1.0
  const midBump = mapGyroSample(context, { ...cfg, motionSensitivityArray: bumpCurve as unknown as ReadonlyArray<readonly [number, number]> }, sample({ x: 0, y: 0, z: 5 }, 73, 73));
  if (!midFlat.ok || !midBump.ok) throw new Error('U5B mid map failed');
  const wantMidFlat = hcClampToShort(5 * getHcSensitivity(1) * applyCustomSensitivity(5, HC_GYRO_THRESHOLD_DPS, flatCurve));
  const wantMidBump = hcClampToShort(5 * getHcSensitivity(1) * applyCustomSensitivity(5, HC_GYRO_THRESHOLD_DPS, bumpCurve));
  if (midFlat.frame.gyroShort!.x !== wantMidFlat || midBump.frame.gyroShort!.x !== wantMidBump) {
    throw new Error(`U5B fwd: custom curve mismatch flat=${midFlat.frame.gyroShort!.x} want=${wantMidFlat} bump=${midBump.frame.gyroShort!.x} want=${wantMidBump}`);
  }
  if (Math.abs(midBump.frame.gyroShort!.x) <= Math.abs(midFlat.frame.gyroShort!.x)) {
    throw new Error('U5B fwd: bump curve must exceed flat curve at mid-saturated input');
  }
}

// ---------------------------------------------------------------- U5B-DEF 默认曲线结构（与 native 缺失时的 49×0.5 逐点一致）
{
  // native inputCaptureApplyCustomSensitivity（main.cpp:5083-5091）：数组缺失时
  // 用默认曲线 nodeKey=i/48、nodeValue=0.5（49 点）。TS createDefaultMotionSensitivityArray
  // 必须与之逐点一致，否则「UI 未配置曲线」时预览与实际输出两端定义不同（§1.30 同类）。
  const def = createDefaultMotionSensitivityArray();
  if (def.length !== 49) throw new Error(`U5B-DEF fwd: default curve must be 49 nodes, got=${def.length}`);
  for (let i = 0; i < 49; i++) {
    if (Math.abs(def[i][0] - i / 48) > 1e-9 || def[i][1] !== 0.5) {
      throw new Error(`U5B-DEF fwd: node ${i} mismatch key=${def[i][0]} want=${i / 48} val=${def[i][1]} want=0.5`);
    }
  }
  // 反向：默认曲线输出必须与 HC 最近 2 点反距离加权公式逐点一致（native
  // main.cpp:5104-5110：k=2、w=1/(1+d)、sum/k×2）。midIn=800 → posAbs=0.4，
  // 最近节点 i=19(0.395833,d1=0.004167)、i=20(0.416667,d2=0.016667)；两节点值均 0.5。
  const midInput = 800;
  const posAbs = midInput / HC_GYRO_THRESHOLD_DPS;
  const w1 = 1 / (1 + Math.abs(19 / 48 - posAbs));
  const w2 = 1 / (1 + Math.abs(20 / 48 - posAbs));
  const wantCS = ((0.5 * w1 + 0.5 * w2) / 2) * 2.0;
  const cs = applyCustomSensitivity(midInput, HC_GYRO_THRESHOLD_DPS, def);
  if (Math.abs(cs - wantCS) > 1e-9) throw new Error(`U5B-DEF reverse: default curve gain mismatch got=${cs} want=${wantCS}`);
}

// ---------------------------------------------------------------- U7/U8 invert
{
  const g = { x: 10, y: 8, z: 20 };
  const plain = mapGyroSample(context, base, sample(g, 7, 7));
  const inv = mapGyroSample(context, { ...base, invertHorizontal: true, invertVertical: true }, sample(g, 8, 8));
  if (!plain.ok || !inv.ok) throw new Error('U7/U8 base map failed');
  if (inv.frame.gyroShort!.x !== -plain.frame.gyroShort!.x || inv.frame.gyroShort!.y !== -plain.frame.gyroShort!.y) {
    throw new Error('U7/U8 reverse: inversion must be exact negation');
  }
}

// ---------------------------------------------------------------- U9/U10 motionMode + trigger
{
  const holdCfg = { ...base, motionMode: 'off' as const, motionTrigger: 'lt', invertVertical: false };
  const idle = mapGyroSample(context, holdCfg, sample({ x: 40, y: 0, z: 0 }, 9, 9));
  if (!idle.ok || idle.frame.contribution.x !== 0) throw new Error('U9 fwd: off w/o trigger must be zero');
  const held = mapGyroSample(context, holdCfg, sample({ x: 40, y: 0, z: 0 }, 10, 10), { pressed: new Set(['lt']) });
  if (!held.ok || held.frame.contribution.y === 0) throw new Error('U9 fwd: off+trigger must contribute');
  const onHold = mapGyroSample(context, { ...base, motionMode: 'on' as const, motionTrigger: 'lt', invertVertical: false }, sample({ x: 40, y: 0, z: 0 }, 11, 11), { pressed: new Set(['lt']) });
  if (!onHold.ok || onHold.frame.contribution.y !== 0) throw new Error('U9 fwd: on+trigger must mute');
  // U10 invalid-trigger fail-closed: native triggerResolved (main.cpp:5154-5167)
  // gates motion OFF for unknown names in EVERY mode (nextMotionEnabled &&=
  // triggerResolved). Mapper must match: 'on' + bogus name = zero, not active.
  const bogus = mapGyroSample(context, { ...base, motionMode: 'on' as const, motionTrigger: 'left-pad-touch', invertVertical: false }, sample({ x: 40, y: 0, z: 0 }, 11, 11));
  if (!bogus.ok || bogus.frame.contribution.y !== 0 || bogus.frame.contribution.x !== 0) {
    throw new Error(`U10 reverse: unresolved trigger name must fail closed in on-mode, got=(${bogus.frame.contribution.x},${bogus.frame.contribution.y})`);
  }
  // 反向：大小写不敏感解析（native ascii_lower），大写持久名仍能命中小写 pressed 集。
  const upperTrigger = mapGyroSample(context, { ...base, motionMode: 'off' as const, motionTrigger: 'RT', invertVertical: false }, sample({ x: 40, y: 0, z: 0 }, 11, 11), { pressed: new Set(['rt']) });
  if (!upperTrigger.ok || upperTrigger.frame.contribution.y === 0) throw new Error('U10 reverse: uppercase trigger must resolve case-insensitively');

  // toggle 状态机：press 边沿翻转由 native inputCaptureMotionTriggered (main.cpp:5248–5252)
  // 维护并下发 toggleOn；mapper 只消费 toggleOn 布尔（gyroMotionMapperMock motionActive）。
  // 正向：toggleOn=true → 输出 == HC 公式；反向：toggleOn=false → 零输出。
  const togCfg = { ...base, motionMode: 'toggle' as const, motionTrigger: 'ls', invertVertical: false };
  const togOff = mapGyroSample(context, togCfg, sample({ x: 20, y: 0, z: 0 }, 12, 12), { toggleOn: false });
  if (!togOff.ok || togOff.frame.contribution.y !== 0) throw new Error('U9 fwd: toggleOn=false must be zero');
  const togOn = mapGyroSample(context, togCfg, sample({ x: 20, y: 0, z: 0 }, 13, 13), { toggleOn: true });
  const want = forwardLocal(togCfg, { x: 20, y: 0, z: 0 });
  if (!togOn.ok || togOn.frame.gyroShort!.y !== want.y) throw new Error('U9 reverse: toggle-on output must match HC formula');
  // 反向：toggle 状态机身份——两次 press 边沿（pressed 集变化）应回到原状态；此处验证
  // mapper 在重复 toggleOn=true 的样本下保持稳定（native 负责去抖）。
  const togRepeat = mapGyroSample(context, togCfg, sample({ x: 20, y: 0, z: 0 }, 14, 14), { toggleOn: true });
  if (!togRepeat.ok || togRepeat.frame.gyroShort!.y !== want.y) throw new Error('U9 reverse: toggle repeat must stay stable');
}

// ---------------------------------------------------------------- U11 motionInput: steering + planes
{
  const noPlane = mapGyroSample(context, { ...base, motionInput: 'joystick-steering' as const }, { sequence: 16, timestamp: 16 });
  if (noPlane.ok) throw new Error('U11 fwd: steering w/o accel plane must be rejected');
  const tilt = { x: -Math.tan(15 * Math.PI / 180), y: 0, z: 1 };
  const s = mapGyroSample(context, { ...base, motionInput: 'joystick-steering' as const, invertVertical: false, invertHorizontal: false }, { sequence: 17, timestamp: 17, gamepadMotionPlane: plane({ x: 0, y: 0, z: 0 }, tilt) });
  if (!s.ok) throw new Error('U11 fwd steering failed');
  const steerShort = hcSteering(hcInclinationAngles(tilt).y, 30, 1, 0);
  const wantSteer = hcClampToShort(steerShort * applyCustomSensitivity(steerShort, HC_GYRO_THRESHOLD_DPS, nodes));
  if (s.frame.gyroShort!.x !== wantSteer || s.frame.gyroShort!.y !== 0) throw new Error('U11 reverse: steering short mismatch');
}

// ---------------------------------------------------------------- U11b SettingsMode1 inclination 角度（HC MotionManager.cs:237-247）
{
  // HC MotionManager 在 UI 页面为 SettingsMode1 时发射 SettingsMode1Update，
  // 参数为 Inclination.UpdateReport 的 Angles（X/Y 度，atan 公式：
  //   x = -atan(ay / sqrt(ax^2+az^2)); y = -atan(ax / sqrt(ay^2+az^2))）。
  // 该角度由 JoystickSteering 消费（MotionManager.cs:237-238），也供校准/预览 UI。
  // YMCC 以 hcInclinationAngles(defaultAccel) 提供等价数据面；此处验证与
  // 手工 atan 公式一致，且 defaultAccel 平面（native 构造）是唯一输入。
  const accel = { x: -Math.tan(20 * Math.PI / 180), y: 0, z: 1 };
  const got = hcInclinationAngles(accel);
  const rad2deg = 180 / Math.PI;
  const wantX = -(Math.atan(accel.y / Math.sqrt(accel.x * accel.x + accel.z * accel.z)) * rad2deg);
  const wantY = -(Math.atan(accel.x / Math.sqrt(accel.y * accel.y + accel.z * accel.z)) * rad2deg);
  if (!near(got.x, wantX, 1e-9) || !near(got.y, wantY, 1e-9)) {
    throw new Error(`U11b inclination mismatch got=(${got.x},${got.y}) want=(${wantX},${wantY})`);
  }
  // 反向推演：角度与输入 accel 平面一一对应（20° 倾角 => y 轴角度 ±20°）。
  if (Math.abs(Math.abs(got.y) - 20) > 1e-6) throw new Error(`U11b reverse: expected |y|=20 deg got ${got.y}`);
  // SettingsMode0 等价：原始 Default gyro 三轴经 telemetry detail.gyro 提供（UI 只读遥测）。
  const s0 = normalizeGyroTelemetry?.({ gyro: { x: 12.5, y: -3, z: 40 }, output: { x: 0, y: 0 } });
  if (!s0 || s0.gyro.x !== 12.5 || s0.gyro.z !== 40) throw new Error('U11b SettingsMode0 gyro telemetry normalization failed');
}

// ---------------------------------------------------------------- U11a steeringAxis (HC Profile.SteeringAxis)
{
  // HC SetupMotion emits the Default plane already resolved by steering axis:
  //   roll: defaultGyro = (cgx, cgy, cgz)  -> LocalSpace (Z,X) = (cgz, cgx)
  //   yaw:  defaultGyro = (cgx,-cgz,-cgy)  -> LocalSpace (Z,X) = (-cgy,cgx)
  // native/main.cpp now selects this plane via inputCaptureHcSteeringAxisResolvesYaw()
  // (main.cpp:8640-8644) which mirrors HC DetermineSteeringAxis (MotionManager.cs:139-156).
  // mapper consumes the plane verbatim, so roll and yaw must map to different
  // contributions for the same calibrated raw (cgx,cgy,cgz).
  const cal = { x: 30, y: -20, z: 15 };
  const cfg = { ...base, motionInput: 'local-space' as const, invertVertical: false, invertHorizontal: false };
  const rollMapped = mapGyroSample(context, cfg, { sequence: 29, timestamp: 29, gamepadMotionPlane: plane(cal) });
  const yawMapped = mapGyroSample(context, cfg, { sequence: 30, timestamp: 30, gamepadMotionPlane: plane({ x: cal.x, y: -cal.z, z: -cal.y }) });
  if (!rollMapped.ok || !yawMapped.ok) throw new Error('U11a base plane map failed');
  // roll: (Z,X) = (cgz, cgx); yaw: (Z,X) = (-cgy, cgx)
  // forwardLocal(g): outX = g.z, outY = g.x（即 LocalSpace (Z,X) 消费）。
  //   roll 需要 g = (cgx,?,cgz) → 即 cal 本身；
  //   yaw 需要 g = (cgx, ?,-cgy) → 期望 outX = -cgy、outY = cgx。
  const rollWant = forwardLocal({ ...cfg, gyroMultiplier: 1 }, cal);
  const yawWant = forwardLocal({ ...cfg, gyroMultiplier: 1 }, { x: cal.x, y: 0, z: -cal.y });
  if (rollMapped.frame.gyroShort!.x !== rollWant.x || rollMapped.frame.gyroShort!.y !== rollWant.y) {
    throw new Error(`U11a roll plane mismatch ${JSON.stringify(rollMapped.frame.gyroShort)} want (${rollWant.x},${rollWant.y})`);
  }
  if (yawMapped.frame.gyroShort!.x !== yawWant.x || yawMapped.frame.gyroShort!.y !== yawWant.y) {
    throw new Error(`U11a yaw plane mismatch ${JSON.stringify(yawMapped.frame.gyroShort)} want (${yawWant.x},${yawWant.y})`);
  }
  // 反向推演: roll 的 outX 直接还原 cgz，yaw 的 outX 还原 -cgy；两个模式必须可区分。
  const rollBack = rollMapped.frame.gyroShort!.x / (getHcSensitivity(1) * applyCustomSensitivity(rollMapped.frame.gyroShort!.x / getHcSensitivity(1), HC_GYRO_THRESHOLD_DPS, nodes));
  const yawBack = yawMapped.frame.gyroShort!.x / (getHcSensitivity(1) * applyCustomSensitivity(yawMapped.frame.gyroShort!.x / getHcSensitivity(1), HC_GYRO_THRESHOLD_DPS, nodes));
  if (!near(rollBack, cal.z, 1e-3) || !near(yawBack, -cal.y, 1e-3)) {
    throw new Error(`U11a reverse steering-axis origination rollBack=${rollBack} yawBack=${yawBack}`);
  }
  // config 合法值校验（settingsRepository normalize 亦以此集合为准）。
  for (const axis of ['roll', 'yaw', 'auto'] as const) {
    const ok = mapGyroSample(context, { ...cfg, steeringAxis: axis, motionInput: 'local-space' as const }, { sequence: 31, timestamp: 31, gamepadMotionPlane: plane(cal) });
    if (!ok.ok) throw new Error(`U11a config steeringAxis=${axis} rejected`);
  }
}

// ---------------------------------------------------------------- U12/U13 velocity mode
{
  const cfg = { ...base, velocityMode: 'velocity' as const, velocityScale: 2, invertVertical: false, motionSensitivityX: 1, motionSensitivityY: 1 };
  const dt = 1 / HC_VELOCITY_FPS;
  let gate: GyroGateContext = {};
  // z=2 deg/s：cs(2/2000)≈0.99；intended = 2×0.99×dt×60×2 ≈ 3.96（short=sens×3.96=3960，未饱和）。
  const r1 = mapGyroSample(context, cfg, sample({ x: 0, y: 0, z: 2 }, 18, 18, dt), gate);
  if (!r1.ok || r1.frame.gyroShort!.x === 0) throw new Error('U12 fwd: velocity first frame must contribute');
  const sens = getHcSensitivity(1);
  const intended = 2 * applyCustomSensitivity(2, HC_GYRO_THRESHOLD_DPS, nodes) * dt * HC_VELOCITY_FPS * (cfg.velocityScale ?? 1);
  const wantShort = hcClampToShort(intended * sens);
  if (r1.frame.gyroShort!.x !== wantShort) throw new Error(`U12 fwd: velocity short mismatch got=${r1.frame.gyroShort!.x} want=${wantShort}`);
  // 反向推演：consumed = short/sens 恰好把本帧加入的 intended 位移消耗掉；余量 ×0.90 decay → 接近 0。
  const accAfter = gate.accumulatedDisplacement!;
  if (!(Math.abs(accAfter.x) < 1e-2)) {
    throw new Error(`U13 reverse: accumulator after consume+decay not zero acc=${accAfter.x}`);
  }
  // 非 velocity 模式重置累积器（反向断言）。
  const gate2: GyroGateContext = { accumulatedDisplacement: { x: 123, y: 45 } };
  const plain = mapGyroSample(context, { ...base, velocityMode: 'default' as const }, sample({ x: 0, y: 0, z: 100 }, 19, 19, dt), gate2);
  if (!plain.ok || plain.frame.gyroShort!.x === 0) throw new Error('U12 fwd default mode failed');
  if (gate2.accumulatedDisplacement!.x !== 0 || gate2.accumulatedDisplacement!.y !== 0) throw new Error('U13 reverse: default mode must reset accumulator');
}

// ---------------------------------------------------------------- U14 gyroWeight (LayoutManager 混棒，native 8880–8903)
{
  // 该功能在 native 合帧层，mapper 不消费；此处验证 hcInputUtils.blendGyroShortIntoStick 与 HC 公式一致。
  // 反推：weightFactor = gyroWeight - stickNorm，输出 = base + gyroShort×factor。
  // 使用与 native main.cpp 8888–8901 相同的公式做独立推导比对。
  const baseShort = { x: 5000, y: 0 };
  const gyroShort = { x: 4000, y: 0 };
  const weight = 1.2;
  const norm = Math.min(1, Math.hypot(baseShort.x, baseShort.y) / 32767);
  const factor = weight - norm;
  const want = { x: Math.max(-32768, Math.min(32767, Math.trunc(baseShort.x + gyroShort.x * factor))), y: 0 };
  const got = blendGyroShortIntoStick(baseShort, gyroShort, weight);
  if (got.x !== want.x || got.y !== want.y) throw new Error(`U14 reverse: blend mismatch got=${JSON.stringify(got)} want=${JSON.stringify(want)}`);
}

// ---------------------------------------------------------------- U3 outputStick 目标隔离（native main.cpp:9432-9450 镜像）
{
  // native inputHostSubmitPad：targetLeft 决定混棒写入 sThumbLX/LY 或 sThumbRX/RY，
  // 另一摇杆原样透传（9444-9450）。UI 消费侧（GyroMotionView.vue telemetry 142-145）
  // 用 actualTarget 选择 leftStick/rightStick——两端必须同语义。
  // 正推演：right 目标 → 右摇杆 = base + gyro×factor，左摇杆保持 base。
  const padBase = { left: { x: 3000, y: -4000 }, right: { x: 5000, y: 0 } };
  const gyro = { x: 4000, y: 1500 };
  const weight = 1.2;
  const applyTarget = (target: 'left' | 'right') => {
    const base = target === 'left' ? padBase.left : padBase.right;
    const stickNorm = Math.min(1, Math.hypot(base.x, base.y) / 32767);
    const factor = weight - stickNorm;
    const mixed = { x: Math.max(-32768, Math.min(32767, Math.trunc(base.x + gyro.x * factor))), y: Math.max(-32768, Math.min(32767, Math.trunc(base.y + gyro.y * factor))) };
    return { left: target === 'left' ? mixed : { ...padBase.left }, right: target === 'right' ? mixed : { ...padBase.right } };
  };
  const rightOut = applyTarget('right');
  if (rightOut.right.x === padBase.right.x || rightOut.left.x !== padBase.left.x || rightOut.left.y !== padBase.left.y) {
    throw new Error(`U3 fwd: right target must blend right stick, keep left intact got=${JSON.stringify(rightOut)}`);
  }
  const leftOut = applyTarget('left');
  if (leftOut.left.x === padBase.left.x || leftOut.right.x !== padBase.right.x || leftOut.right.y !== padBase.right.y) {
    throw new Error(`U3 fwd: left target must blend left stick, keep right intact got=${JSON.stringify(leftOut)}`);
  }
  // 反向推演：targetLeft=false 时 right 必须等于独立 blend 公式、left 恒等。
  const independent = blendGyroShortIntoStick(padBase.right, gyro, weight);
  if (rightOut.right.x !== independent.x || rightOut.right.y !== independent.y) throw new Error('U3 reverse: right-target blend must equal single-stick formula');
  // UI 选取（142-145 同款）：actualTarget=right → 取 rightStick。
  const pickedRight = rightOut.right;
  if (Math.hypot(pickedRight.x, pickedRight.y) <= Math.hypot(rightOut.left.x, rightOut.left.y)) throw new Error('U3 reverse: picked right must be the blended (longer) stick');
}

// ---------------------------------------------------------------- U18–U20 steering params
{
  const d = (deg: number) => ({ x: -Math.tan(deg * Math.PI / 180), y: 0, z: 1 });
  const a = (deg: number) => (hcInclinationAngles(d(deg)) as { x: number; y: number }).y;
  // U18 maxAngle：同样 30°，max=30 输出 short 更大
  const r30 = mapGyroSample(context, { ...base, motionInput: 'joystick-steering' as const, steeringMaxAngle: 30, steeringPower: 1, steeringDeadzone: 0, invertVertical: false, invertHorizontal: false }, { sequence: 20, timestamp: 20, gamepadMotionPlane: plane({ x: 0, y: 0, z: 0 }, d(30)) });
  const r60 = mapGyroSample(context, { ...base, motionInput: 'joystick-steering' as const, steeringMaxAngle: 60, steeringPower: 1, steeringDeadzone: 0, invertVertical: false, invertHorizontal: false }, { sequence: 21, timestamp: 21, gamepadMotionPlane: plane({ x: 0, y: 0, z: 0 }, d(30)) });
  if (!r30.ok || !r60.ok) throw new Error('U18 base steering failed');
  const want30 = hcClampToShort(hcSteering(a(30), 30, 1, 0) * applyCustomSensitivity(hcSteering(a(30), 30, 1, 0), HC_GYRO_THRESHOLD_DPS, nodes));
  const want60 = hcClampToShort(hcSteering(a(30), 60, 1, 0) * applyCustomSensitivity(hcSteering(a(30), 60, 1, 0), HC_GYRO_THRESHOLD_DPS, nodes));
  if (r30.frame.gyroShort!.x !== want30 || r60.frame.gyroShort!.x !== want60) throw new Error('U18 reverse: steering maxAngle mismatch');
  if (!(Math.abs(want60) < Math.abs(want30))) throw new Error('U18 fwd: larger maxAngle must saturate less (smaller short)');
  // U19 power：power>1 压缩
  const p1 = mapGyroSample(context, { ...base, motionInput: 'joystick-steering' as const, steeringMaxAngle: 30, steeringPower: 1, invertVertical: false, invertHorizontal: false }, { sequence: 22, timestamp: 22, gamepadMotionPlane: plane({ x: 0, y: 0, z: 0 }, d(15)) });
  const p2 = mapGyroSample(context, { ...base, motionInput: 'joystick-steering' as const, steeringMaxAngle: 30, steeringPower: 2, invertVertical: false, invertHorizontal: false }, { sequence: 23, timestamp: 23, gamepadMotionPlane: plane({ x: 0, y: 0, z: 0 }, d(15)) });
  if (!p1.ok || !p2.ok) throw new Error('U19 base steering failed');
  const w1 = hcClampToShort(hcSteering(a(15), 30, 1, 0) * applyCustomSensitivity(hcSteering(a(15), 30, 1, 0), HC_GYRO_THRESHOLD_DPS, nodes));
  const w2 = hcClampToShort(hcSteering(a(15), 30, 2, 0) * applyCustomSensitivity(hcSteering(a(15), 30, 2, 0), HC_GYRO_THRESHOLD_DPS, nodes));
  if (p1.frame.gyroShort!.x !== w1 || p2.frame.gyroShort!.x !== w2) throw new Error('U19 reverse: steering power mismatch');
  if (Math.abs(w2) > Math.abs(w1)) throw new Error('U19 fwd: power>1 must compress output');
  // U20 deadzone：2° 输入，deadzone=5 → 0；deadzone=0 → 非零
  const dz0 = mapGyroSample(context, { ...base, motionInput: 'joystick-steering' as const, steeringMaxAngle: 30, steeringPower: 1, steeringDeadzone: 0, invertVertical: false, invertHorizontal: false }, { sequence: 24, timestamp: 24, gamepadMotionPlane: plane({ x: 0, y: 0, z: 0 }, d(2)) });
  const dz5 = mapGyroSample(context, { ...base, motionInput: 'joystick-steering' as const, steeringMaxAngle: 30, steeringPower: 1, steeringDeadzone: 5, invertVertical: false, invertHorizontal: false }, { sequence: 25, timestamp: 25, gamepadMotionPlane: plane({ x: 0, y: 0, z: 0 }, d(2)) });
  if (!dz0.ok || !dz5.ok) throw new Error('U20 base steering failed');
  if (dz0.frame.gyroShort!.x === 0 || dz5.frame.gyroShort!.x !== 0) throw new Error('U20 reverse: deadzone boundary mismatch');
}

// ---------------------------------------------------------------- U21 outputAxis (native 8687–8690)
{
  // mapper 不消费 outputAxis；该逻辑在 native，此处验证 U21 语义为"Y 置零"不会出现在 mapper（保持 HC 公式不混入 YMCC 专属门）。
  const m = mapGyroSample(context, { ...base, outputAxis: 'x' }, sample({ x: 10, y: 8, z: 20 }, 26, 26));
  if (!m.ok) throw new Error('U21 map failed');
  if (m.frame.gyroShort!.x === 0 || m.frame.gyroShort!.y === 0) throw new Error('U21: mapper must not apply outputAxis (native-only gate)');
}

// ---------------------------------------------------------------- U21b AimingSights（HC MotionManager.cs:296-298）
{
  // HC: while aiming trigger held -> output *= AimingSightsMultiplier (after
  // custom-sensitivity curve, before velocity). mapper 与 native 均实现该分支。
  const g = { x: 10, y: 0, z: 20 };
  const cfg = { ...base, aimingSightsTrigger: 'LT', aimingSightsMultiplier: 0.5 };
  // 无 gate（未按住 LT）=> 不乘。
  const notHeld = mapGyroSample(context, cfg, sample(g, 80, 80));
  // 按住 LT => 乘 0.5。
  const held = mapGyroSample(context, cfg, sample(g, 81, 81), { pressed: new Set(['LT']) });
  if (!notHeld.ok || !held.ok) throw new Error('U21b base map failed');
  // 反向推演：held = notHeld × 0.5（未饱和区域）。
  const ratio = held.frame.gyroShort!.x / notHeld.frame.gyroShort!.x;
  if (!near(ratio, 0.5, 2e-2)) throw new Error(`U21b reverse: aiming ratio=${ratio} want 0.5`);
  // 正向推演：与 HC 公式一致（notHeld=forwardLocal；held=forwardLocal×0.5）。
  const wantBase = forwardLocal({ ...cfg }, g);
  const wantHeld = { x: hcClampToShort(wantBase.x * 0.5), y: hcClampToShort(wantBase.y * 0.5) };
  if (notHeld.frame.gyroShort!.y !== wantBase.y) throw new Error('U21b fwd: not-held must equal base');
  if (held.frame.gyroShort!.y !== wantHeld.y) throw new Error('U21b fwd: held must equal base * 0.5');
  // multiplier=1 或 trigger 为空 => no-op（反向不变性）。
  const noOp = mapGyroSample(context, { ...base, aimingSightsTrigger: 'LT', aimingSightsMultiplier: 1 }, sample(g, 82, 82), { pressed: new Set(['LT']) });
  if (!noOp.ok || noOp.frame.gyroShort!.x !== notHeld.frame.gyroShort!.x) {
    throw new Error('U21b reverse: multiplier=1 must be no-op');
  }
  // settings 归一化：0.5 保留、越界 3 clamp 到 2。
  const aimNorm = normalizeSettings({ input: { gyroMotion: { aimingSightsMultiplier: 3, aimingSightsTrigger: 'LT' } } });
  if (aimNorm.input.gyroMotion.aimingSightsMultiplier !== 2 || aimNorm.input.gyroMotion.aimingSightsTrigger !== 'LT') {
    throw new Error(`U21b fwd: aiming normalization failed mult=${aimNorm.input.gyroMotion.aimingSightsMultiplier}`);
  }
  // U21b-R 无效 aiming 触发名：native 解析器（main.cpp:5262 ascii_lower 后按
  // XInput 名集匹配）对未知名返回 false -> multiplier 不生效；mapper 同语义。
  const bogusAim = mapGyroSample(context, { ...base, aimingSightsTrigger: 'left-pad-touch', aimingSightsMultiplier: 0.5 }, sample({ x: 10, y: 0, z: 20 }, 83, 83), { pressed: new Set(['left-pad-touch']) });
  if (!bogusAim.ok || bogusAim.frame.gyroShort!.x !== notHeld.frame.gyroShort!.x) {
    throw new Error('U21b-R fwd: unresolvable aiming trigger must not apply multiplier');
  }
  // 大小写：native ascii_lower；持久大写名遇小写 pressed 也应命中。
  const aimCase = mapGyroSample(context, { ...base, aimingSightsTrigger: 'LT', aimingSightsMultiplier: 0.5 }, sample({ x: 10, y: 0, z: 20 }, 84, 84), { pressed: new Set(['lt']) });
  if (!aimCase.ok || !near(aimCase.frame.gyroShort!.x / notHeld.frame.gyroShort!.x, 0.5, 2e-2)) {
    throw new Error(`U21b-R reverse: aiming trigger must resolve case-insensitively got=${aimCase.frame.gyroShort!.x}`);
  }
}

// ---------------------------------------------------------------- U2C outputMode 输出门（virtual-stick/ds4-imu/disabled）
{
  // HC 无直接枚举等价（陀螺仪动作恒经虚拟手柄→LayoutManager），YMCC 的
  // outputMode 是持久化输出目标门：
  //   virtual-stick -> 陀螺仪成为混棒贡献（唯一放行模式）；
  //   disabled      -> 零贡献（保留物理帧）；
  //   ds4-imu       -> persona 冲突拒绝（inputContracts.ts:277：Xbox360 无 IMU 传输）。
  // native main.cpp:5164-5165: nextMotionActionEnabled = enabled && outputMode=="virtual-stick"。
  // mapper gyroMotionMapperMock.ts:89: outputMode!="virtual-stick" -> zeroFrame。
  // 正向推演：enabled=true + disabled/ds4-imu -> 零贡献；virtual-stick -> 正常贡献。
  const gActive = { x: 40, y: 0, z: 20 };
  const active = mapGyroSample(context, { ...base, outputMode: 'virtual-stick', motionMode: 'on' }, sample(gActive, 90, 90));
  if (!active.ok || active.frame.contribution.x === 0 || active.frame.contribution.y === 0) {
    throw new Error('U2C fwd: virtual-stick outputMode must produce motion contribution');
  }
  const disabled = mapGyroSample(context, { ...base, outputMode: 'disabled', motionMode: 'on' }, sample(gActive, 91, 91));
  if (!disabled.ok) throw new Error('U2C fwd: disabled outputMode map failed');
  const disabledActive = disabled.frame.gyroContributionActive === true;
  if (disabledActive || disabled.frame.contribution.x !== 0 || disabled.frame.contribution.y !== 0) {
    throw new Error('U2C fwd: disabled outputMode must zero contribution');
  }
  const ds4Imu = mapGyroSample(context, { ...base, outputMode: 'ds4-imu', motionMode: 'on' }, sample(gActive, 92, 92));
  if (!ds4Imu.ok) throw new Error('U2C fwd: ds4-imu outputMode map failed');
  if (ds4Imu.frame.contribution.x !== 0 || ds4Imu.frame.contribution.y !== 0) {
    throw new Error('U2C fwd: ds4-imu (non-virtual-stick) must zero contribution on mapper');
  }
  // 反向推演：零贡献源可还原为 outputMode != virtual-stick；还原后重开 virtual-stick 恢复贡献。
  if (active.frame.contribution.x === disabled.frame.contribution.x) throw new Error('U2C reverse: disabled must differ from active');
  const reEnabled = mapGyroSample(context, { ...base, motionMode: 'on' }, sample(gActive, 93, 93));
  if (!reEnabled.ok || reEnabled.frame.contribution.x !== active.frame.contribution.x || reEnabled.frame.contribution.y !== active.frame.contribution.y) {
    throw new Error('U2C reverse: reopening virtual-stick must restore identical contribution');
  }
  // settings 归一化：非法输出门回落 disabled；virtual-stick/ds4-imu/disabled 三值保留。
  const nonVirtualNorm = normalizeSettings({ input: { gyroMotion: { ...base, outputMode: 'bogus' as never } } });
  if (nonVirtualNorm.input.gyroMotion.outputMode !== 'disabled') {
    throw new Error(`U2C fwd: invalid outputMode must normalize to disabled, got=${nonVirtualNorm.input.gyroMotion.outputMode}`);
  }
  const ds4Norm = normalizeSettings({ input: { gyroMotion: { ...base, outputMode: 'ds4-imu' as never } } });
  if (ds4Norm.input.gyroMotion.outputMode !== 'ds4-imu') throw new Error('U2C fwd: ds4-imu outputMode must survive normalization');
  // 契约级排序（inputContracts.ts:277）：xbox360 persona + ds4-imu outputMode -> persona-conflict。
  const { validateInputSnapshot } = require('../src/bridge/inputContracts') as typeof import('../src/bridge/inputContracts');
  const conflict = validateInputSnapshot({
    schemaVersion: 1, revision: 1,
    outputTarget: { schemaVersion: 1, persona: 'xbox360', gyroEnabled: true, closure: 'CLOSED', descriptorHash: 'd', buttonMappingEnabled: false },
    gyroMotion: { ...base, outputMode: 'ds4-imu', enabled: true, closure: 'CLOSED', provider: 'fixture', unit: 'deg/s', calibrationId: 'c', revision: 1, schemaVersion: 1 },
    capability: { realRuntimeAuthorized: false, hcAssetId: 'x' },
  });
  if (conflict.ok || conflict.reason !== 'persona-conflict') {
    throw new Error(`U2C reverse: xbox360 + ds4-imu must be rejected, got=${conflict.reason}`);
  }
}

// ---------------------------------------------------------------- 综合反向：LocalSpace 全图 1:1 还原
{
  const g = { x: 30, y: -20, z: 15 };
  const m = mapGyroSample(context, { ...base, invertVertical: false }, sample(g, 27, 27));
  if (!m.ok) throw new Error('reverse combo map failed');
  const back = {
    x: (m.frame.gyroShort!.y / getHcSensitivity(1)) / applyCustomSensitivity((m.frame.gyroShort!.y / getHcSensitivity(1)), HC_GYRO_THRESHOLD_DPS, nodes),
    z: (m.frame.gyroShort!.x / getHcSensitivity(1)) / applyCustomSensitivity((m.frame.gyroShort!.x / getHcSensitivity(1)), HC_GYRO_THRESHOLD_DPS, nodes),
  };
  if (!near(back.x, g.x, 1e-3) || !near(back.z, g.z, 1e-3)) {
    throw new Error(`reverse combo restore failed back=(${back.x},${back.z}) want=(${g.x},${g.z})`);
  }
}

// ---------------------------------------------------------------- U15–U17 deadzone 分层
{
  // 反死区/死区属 AxisActions 层；mapper 的 gyroShort 必须保留死区前原始值（native 在 CanonicalFrame 前只做 HC MotionManager 层）。
  const dz = mapGyroSample(context, { ...base, innerDeadzone: 10, outerDeadzone: 10, antiDeadzone: 15 }, sample({ x: 8, y: 0, z: 0 }, 28, 28));
  if (!dz.ok || dz.frame.gyroShort!.y === 0) throw new Error('U15–U17: MotionManager layer must not eat AxisActions deadzone input');
}

// ---------------------------------------------------------------- U15/U22a AxisActions modifiers（native 混棒前，与 HC 公式单向等价）
{
  // native inputHostApplyAxisModifiers()（main.cpp:8643-8731）镜像
  // HC AxisActions.ApplyAxisModifiers（Actions/AxisActions.cs:81-107）：
  // radial inner/outer deadzone -> anti-deadzone -> output shape。
  // TS 侧 applyHcGyroAxisModifiers 必须与这些语义一致。
  const raw = { x: 18000, y: 12000 }; // short 域输入（gyro contribution）
  // U15 fwd: inner deadzone=20% 把小输入归零。
  const small = { x: Math.round(raw.x * 0.4), y: Math.round(raw.y * 0.4) }; // len≈0.26 高于 20%
  const smallLen = Math.hypot(small.x, small.y) / 32767;
  if (!(smallLen > 0.2)) throw new Error('U15 fwd small vector not inside inner deadzone');
  const innerSmall20 = applyHcGyroAxisModifiers(small, { innerDeadzone: 20, outerDeadzone: 0, antiDeadzone: 0, outputShape: 'default' });
  if (innerSmall20.x === 0 && innerSmall20.y === 0) throw new Error('U15 fwd: 20% inner deadzone must NOT zero a >20% vector');
  const tiny = { x: Math.round(raw.x * 0.05), y: Math.round(raw.y * 0.05) }; // len≈0.033 < 20%
  const innerTiny = applyHcGyroAxisModifiers(tiny, { innerDeadzone: 20, outerDeadzone: 0, antiDeadzone: 0, outputShape: 'default' });
  if (innerTiny.x !== 0 || innerTiny.y !== 0) throw new Error('U15 fwd: inner deadzone must zero a <20% vector');
  // 反向推演: 同一输入在 0% 与 20% 内死区下长度之比 = (len-inner)/(1-inner)/len。
  const smallNone = applyHcGyroAxisModifiers(small, { innerDeadzone: 0, outerDeadzone: 0, antiDeadzone: 0, outputShape: 'default' });
  const small20 = applyHcGyroAxisModifiers(small, { innerDeadzone: 20, outerDeadzone: 0, antiDeadzone: 0, outputShape: 'default' });
  const lenNone = Math.hypot(smallNone.x, smallNone.y);
  const len20 = Math.hypot(small20.x, small20.y);
  const wantRatio = (smallLen - 0.2) / (1 - 0.2);
  if (Math.abs((len20 / lenNone) - (wantRatio / smallLen)) > 1e-6) {
    throw new Error(`U15 reverse: inner-deadzone length ratio mismatch got=${len20 / lenNone} want=${wantRatio / smallLen}`);
  }
  // 0 修饰 = 恒等（反向推演必然成立）。
  const none = applyHcGyroAxisModifiers(raw, { innerDeadzone: 0, outerDeadzone: 0, antiDeadzone: 0, outputShape: 'default' });
  if (none.x !== raw.x || none.y !== raw.y) throw new Error('U15 reverse: zero modifiers must be identity');
  // U16 fwd: outer deadzone=10% 时 len>=0.9 的向量被钳到单位圆（方向不变、长度=1）。
  const sat = { x: 30000, y: 15000 }; // len≈1.05 > 1-0.1
  const outer10 = applyHcGyroAxisModifiers(sat, { innerDeadzone: 0, outerDeadzone: 10, antiDeadzone: 0, outputShape: 'default' });
  const satLen = Math.hypot(outer10.x, outer10.y) / 32767;
  if (Math.abs(satLen - 1) > 1e-6) throw new Error('U16 fwd: outer deadzone must clamp magnitude to 1');
  const satAngle = Math.atan2(outer10.y, outer10.x);
  const wantAngle = Math.atan2(sat.y, sat.x);
  if (Math.abs(satAngle - wantAngle) > 1e-9) throw new Error('U16 reverse: outer clamp must preserve direction');
  // U17 fwd: anti-deadzone=15% 增加零附近输出。
  const antiSmall = { x: 800, y: 0 }; // len≈0.024
  const anti15 = applyHcGyroAxisModifiers(antiSmall, { innerDeadzone: 0, outerDeadzone: 0, antiDeadzone: 15, outputShape: 'default' });
  const antiLen = Math.hypot(anti15.x, anti15.y) / 32767;
  if (antiLen <= 0.024) throw new Error('U17 fwd: anti-deadzone must boost small vectors');
  // 反向: anti 公式 mul = ((1-dz)*len+dz)/len -> 长度 = (1-dz)*len+dz。
  const wantAntiLen = (1 - 0.15) * 0.0244 + 0.15;
  if (Math.abs(antiLen - wantAntiLen) > 1e-3) throw new Error(`U17 reverse: anti magnitude mismatch got=${antiLen} want=${wantAntiLen}`);
  // U22a fwd: outputShape=circle 只放宽（ImproveCircularity 仅在 len>1 时 clamp）；square 使向量沿 L∞ 归一化。
  const circleOut = applyHcGyroAxisModifiers(sat, { innerDeadzone: 0, outerDeadzone: 0, antiDeadzone: 0, outputShape: 'circle' });
  const circleLen = Math.hypot(circleOut.x, circleOut.y) / 32767;
  if (Math.abs(circleLen - 1) > 1e-6) throw new Error('U22a fwd: circle shape must clamp len>1 to unit');
  const squareOut = applyHcGyroAxisModifiers(raw, { innerDeadzone: 0, outerDeadzone: 0, antiDeadzone: 0, outputShape: 'square' });
  // HC ImproveSquare 保持 L2 长度 len，并把 L∞ (max|axis|) 提升到 len：
  //   scale = len/denom；max|out| = len。因此 maxAxis == 原始 len。
  const rawNorm = Math.hypot(raw.x / 32767, raw.y / 32767);
  const maxAxis = Math.max(Math.abs(squareOut.x / 32767), Math.abs(squareOut.y / 32767));
  if (Math.abs(maxAxis - rawNorm) > 1e-6) {
    throw new Error(`U22a reverse: square shape must keep L2 len and lift L-infinity to len, got=${maxAxis} want=${rawNorm}`);
  }
  // U22a cross: CrossDeadzoneMapping(x, inner%, outer%) 分别对 x/y 用轴死区削，
  // 再做 ImproveCircularity；验证 x 轴上 inner=50% 使小 x 归零、y 方向保留。
  const crossIn = { x: 12000, y: 20000 };
  const crossOut = applyHcGyroAxisModifiers(crossIn, { innerDeadzone: 50, outerDeadzone: 0, antiDeadzone: 0, outputShape: 'cross' });
  // x 轴死区 50%：x/32767=0.366 < 0.5 => outX=0；y 轴死区 0% => 保留。
  if (crossOut.x !== 0) throw new Error(`U22a fwd: cross x-axis inner deadzone must zero small x, got=${crossOut.x}`);
  if (crossOut.y === 0) throw new Error('U22a fwd: cross y-axis must survive y inner=0%');
  // default 形状 = 恒等（反向推演基准）。
  const shapeNone = applyHcGyroAxisModifiers(crossIn, { innerDeadzone: 0, outerDeadzone: 0, antiDeadzone: 0, outputShape: 'default' });
  if (shapeNone.x !== crossIn.x || shapeNone.y !== crossIn.y) throw new Error('U22a reverse: default shape must be identity');
}

// ---------------------------------------------------------------- U17B response curve（HC AxisActions.ResponseCurvePoints）
{
  // HC ApplyAxisModifiers 顺序：radial -> anti -> ApplyResponseCurve -> shape
  // （AxisActions.cs:86-99；InputUtils.cs:469-489 幅度乘数）。默认 6 点线性恒等
  // （AxisActions.cs:36-44）=> 无曲线 = no-op。native main.cpp 已实现同序（新版）。
  // 正向推演：自定义二次曲线使低输入放大、高输入收敛。
  const rcRaw = { x: 10000, y: 0 }; // len≈0.305
  // 无曲线/线性恒等 => 长度不变。
  const rcNone = applyHcGyroAxisModifiers(rcRaw, { innerDeadzone: 0, outerDeadzone: 0, antiDeadzone: 0, outputShape: 'default' });
  if (rcNone.x !== rcRaw.x || rcNone.y !== rcRaw.y) throw new Error('U17B fwd: absent response curve must be identity');
  const linearCurve = HC_DEFAULT_RESPONSE_CURVE as unknown as ResponseCurvePoint[];
  const rcLinear = applyHcGyroAxisModifiers(rcRaw, { innerDeadzone: 0, outerDeadzone: 0, antiDeadzone: 0, outputShape: 'default', responseCurvePoints: linearCurve });
  if (Math.abs(rcLinear.x - rcRaw.x) > 1e-6) throw new Error('U17B fwd: HC default linear curve must be identity');
  // 自定义 3 点曲线：(0,0),(0.5,1),(1,1) —— 在 len<0.5 时放大、>0.5 收敛。
  const boostCurve: ResponseCurvePoint[] = [[0, 0], [0.5, 1], [1, 1]];
  const boosted = applyHcGyroAxisModifiers({ x: 4000, y: 0 }, { innerDeadzone: 0, outerDeadzone: 0, antiDeadzone: 0, outputShape: 'default', responseCurvePoints: boostCurve });
  // 正向公式：norm=0.122，曲线插值=0.244（0..0.5 段 t=0.244），mult=0.244/0.122=2。
  const boostLen = Math.hypot(boosted.x, boosted.y) / 32767;
  const wantBoostNorm = 4000 / 32767; // ≈0.122
  const wantCurved = (wantBoostNorm / 0.5) * 1; // 插值
  if (Math.abs(boostLen - wantCurved) > 2e-3) throw new Error(`U17B fwd: boost curve magnitude mismatch got=${boostLen} want=${wantCurved}`);
  // 方向保持（反向推演：输出/输入必须同向）。
  const rcRaw2 = { x: 3000, y: -4000 }; // 非轴对齐
  const rcBoost2 = applyHcGyroAxisModifiers(rcRaw2, { innerDeadzone: 0, outerDeadzone: 0, antiDeadzone: 0, outputShape: 'default', responseCurvePoints: boostCurve });
  const angleIn = Math.atan2(rcRaw2.y, rcRaw2.x);
  const angleOut = Math.atan2(rcBoost2.y, rcBoost2.x);
  if (Math.abs(angleOut - angleIn) > 1e-6) throw new Error('U17B reverse: response curve must preserve direction');
  // 唯一点数 <2 时 HC 返回原值（InputUtils.cs:471-472）；归一化拒绝非法节点。
  const degenerated = applyResponseCurve(rcRaw, [[0.2, 0.2]] as unknown as ResponseCurvePoint[]);
  if (degenerated.x !== rcRaw.x) throw new Error('U17B fwd: <2 control points must be identity');
  const badCurve = applyResponseCurve(rcRaw, [[1, 1], [0, 0]] as unknown as ResponseCurvePoint[]);
  if (badCurve.x !== rcRaw.x) throw new Error('U17B fwd: inverted control points must be identity');
  // settings 归一化：合法曲线保留（排序）、非法回退删除（identity）。
  const rcNorm = normalizeSettings({ input: { gyroMotion: { ...base, responseCurvePoints: boostCurve } } });
  const rcNormCurve = rcNorm.input.gyroMotion.responseCurvePoints;
  if (!rcNormCurve || rcNormCurve.length !== 3 || rcNormCurve[0][0] !== 0 || rcNormCurve[1][0] !== 0.5) {
    throw new Error('U17B fwd: valid response curve must survive normalization');
  }
  const rcBadNorm = normalizeSettings({ input: { gyroMotion: { ...base, responseCurvePoints: [[2, 2], [1, 0]] as unknown as ResponseCurvePoint[] } } });
  if (rcBadNorm.input.gyroMotion.responseCurvePoints !== undefined) {
    throw new Error('U17B fwd: invalid response curve must be dropped by normalization');
  }
}

// ---------------------------------------------------------------- U14a 混棒含 modifiers（assembleCanonicalFrame 与 native 一致）
{
  // native inputHostSubmitPad 现在先 ApplyAxisModifiers 再 blend：
  //   contribution -> modified -> out = base + modified*(weight-stickNorm)。
  // UI preview（assembleCanonicalFrame）同样 applyHcGyroAxisModifiers + blendGyroShortIntoStick。
  // 验证组装含 modifiers 时 rightStick 与手工公式一致。
  const baseStick = { x: 0.2, y: 0 };
  const rawShort = { x: hcUnitToShort(0.4), y: 0 };
  const modified = applyHcGyroAxisModifiers(rawShort, { innerDeadzone: 10, outerDeadzone: 0, antiDeadzone: 15, outputShape: 'default' });
  const wantBlend = blendGyroShortIntoStick({ x: hcUnitToShort(baseStick.x), y: hcUnitToShort(baseStick.y) }, modified, 1.2);
  const assembled = assembleCanonicalFrame(
    { runId: 't', epoch: 1, powerGeneration: 1, targetId: 't', persona: 'xbox360' as const, configRevision: 1, inputSequence: 40, timestamp: 40, buttons: 0, axes: {}, triggers: {}, rightStick: baseStick },
    { runId: 't', epoch: 1, powerGeneration: 1, targetId: 't', persona: 'xbox360' as const, configRevision: 1, sampleSequence: 41, timestamp: 41, valid: true, contribution: { x: 0.4, y: 0 }, gyroShort: rawShort },
    { gyroWeight: 1.2, innerDeadzone: 10, outerDeadzone: 0, antiDeadzone: 15, outputShape: 'default' },
  );
  if (!assembled.ok) throw new Error(`U14a assemble failed ${'reason' in assembled ? assembled.reason : ''}`);
  const gotStick = assembled.frame.rightStick;
  const wantStick = { x: hcShortToUnit(wantBlend.x), y: hcShortToUnit(wantBlend.y) };
  if (!near(gotStick.x, wantStick.x, 1e-6) || !near(gotStick.y, wantStick.y, 1e-6)) {
    throw new Error(`U14a blend+modifiers mismatch got=(${gotStick.x},${gotStick.y}) want=(${wantStick.x},${wantStick.y})`);
  }
  // 反向推演: 相同 base 下开启 vs 关闭内死区应产生不同 rightStick（可区分）。
  const noMag = applyHcGyroAxisModifiers(rawShort, { innerDeadzone: 0, outerDeadzone: 0, antiDeadzone: 0, outputShape: 'default' });
  const wantNoMag = blendGyroShortIntoStick({ x: hcUnitToShort(baseStick.x), y: hcUnitToShort(baseStick.y) }, noMag, 1.2);
  if (Math.abs(Math.hypot(wantNoMag.x, wantNoMag.y) - Math.hypot(wantBlend.x, wantBlend.y)) < 1e-6) {
    throw new Error('U14a reverse: modifiers must change the blended magnitude');
  }
  // U14a-RC: responseCurvePoints 必须经 assembleCanonicalFrame 应用（UI preview
  // 链路 = native inputHostApplyAxisModifiers 第 3 步）。boost 曲线改变大小。
  const rcBlend: { gyroWeight?: number; innerDeadzone?: number; outerDeadzone?: number; antiDeadzone?: number; outputShape?: 'default' | 'circle' | 'cross' | 'square'; responseCurvePoints?: readonly ResponseCurvePoint[] | null } = {
    gyroWeight: 1.2, innerDeadzone: 10, outerDeadzone: 0, antiDeadzone: 15, outputShape: 'default',
    responseCurvePoints: [[0, 0], [0.5, 0.25], [1, 1]] as ResponseCurvePoint[],
  };
  const assembledRC = assembleCanonicalFrame(
    { runId: 't', epoch: 1, powerGeneration: 1, targetId: 't', persona: 'xbox360' as const, configRevision: 1, inputSequence: 42, timestamp: 42, buttons: 0, axes: {}, triggers: {}, rightStick: baseStick },
    { runId: 't', epoch: 1, powerGeneration: 1, targetId: 't', persona: 'xbox360' as const, configRevision: 1, sampleSequence: 43, timestamp: 43, valid: true, contribution: { x: 0.4, y: 0 }, gyroShort: rawShort },
    rcBlend,
  );
  if (!assembledRC.ok) throw new Error(`U14a-RC assemble failed ${'reason' in assembledRC ? assembledRC.reason : ''}`);
  const modifiedRC = applyHcGyroAxisModifiers(rawShort, { innerDeadzone: 10, outerDeadzone: 0, antiDeadzone: 15, outputShape: 'default', responseCurvePoints: rcBlend.responseCurvePoints });
  const wantBlendRC = blendGyroShortIntoStick({ x: hcUnitToShort(baseStick.x), y: hcUnitToShort(baseStick.y) }, modifiedRC, 1.2);
  const gotStickRC = assembledRC.frame.rightStick;
  const wantStickRC = { x: hcShortToUnit(wantBlendRC.x), y: hcShortToUnit(wantBlendRC.y) };
  if (!near(gotStickRC.x, wantStickRC.x, 1e-6) || !near(gotStickRC.y, wantStickRC.y, 1e-6)) {
    throw new Error(`U14a-RC assemble+curve mismatch got=(${gotStickRC.x},${gotStickRC.y}) want=(${wantStickRC.x},${wantStickRC.y})`);
  }
  // 反向: 同一输入在 boost 曲线下输出必须弱于恒等（0.5→0.25 压平中段）。
  const assembledRCId = assembleCanonicalFrame(
    { runId: 't', epoch: 1, powerGeneration: 1, targetId: 't', persona: 'xbox360' as const, configRevision: 1, inputSequence: 44, timestamp: 44, buttons: 0, axes: {}, triggers: {}, rightStick: baseStick },
    { runId: 't', epoch: 1, powerGeneration: 1, targetId: 't', persona: 'xbox360' as const, configRevision: 1, sampleSequence: 45, timestamp: 45, valid: true, contribution: { x: 0.4, y: 0 }, gyroShort: rawShort },
    { gyroWeight: 1.2, innerDeadzone: 10, outerDeadzone: 0, antiDeadzone: 15, outputShape: 'default' },
  );
  if (!assembledRCId.ok || !assembledRC.ok) throw new Error('U14a-RC identity assemble failed');
  if (Math.hypot(assembledRC.frame.rightStick.x - baseStick.x, assembledRC.frame.rightStick.y) >= Math.hypot(assembledRCId.frame.rightStick.x - baseStick.x, assembledRCId.frame.rightStick.y)) {
    throw new Error('U14a-RC reverse: boost curve must attenuate mid-range contribution vs identity');
  }
}

// ---------------------------------------------------------------- U17B-ED 响应曲线 UI 编辑器正反推演
{
  const defaults = defaultResponseCurvePairs();
  // 正向: HC 默认 6 点线性恒等（AxisActions.cs:36-44）。
  if (defaults.length !== 6 || defaults[0][0] !== 0 || defaults[1][1] !== 0.2 || defaults[5][0] !== 1) {
    throw new Error('U17B-ED fwd: HC default response curve must be 6-point identity');
  }
  // 正向: 自定义持久曲线 → 编辑器草稿 → commit 归一排序（严格递增）且保序留值。
  const customED: EditableCurve = [[0.1, 0.05], [0.5, 0.6], [0.9, 0.95]];
  const draftED = draftCurveRows(customED, defaults);
  const committedED = commitCurveRows(draftED);
  if (!committedED || committedED.length !== 3 || committedED[0][0] !== 0.1 || committedED[2][1] !== 0.95 ||
      !committedED.every((p, i) => i === 0 || p[0] > committedED[i - 1][0])) {
    throw new Error(`U17B-ED fwd: custom curve commit mismatch ${JSON.stringify(committedED)}`);
  }
  // 反向: draft→commit 幂等；且与应用端 applyResponseCurve 一致（0.5 幅度命中曲线节点 0.6）。
  const againED = commitCurveRows(draftCurveRows(committedED, defaults));
  if (JSON.stringify(againED) !== JSON.stringify(committedED)) throw new Error('U17B-ED reverse: commit must be idempotent');
  const boostED = applyResponseCurve({ x: 16384, y: 0 }, committedED);
  // HC_SHORT_MAX=32767 量化: 16384/32767 ≈ 0.5000153, 落在 (0.5,0.6)-(0.9,0.95) 插值段 → ≈0.6（1e-3 容差）。
  if (!near(Math.abs(boostED.x) / 32767, 0.6, 1e-3)) {
    throw new Error(`U17B-ED reverse: 0.5 magnitude must hit curve value ~0.6 got=${Math.abs(boostED.x) / 32767}`);
  }
  // 反向（fail-closed）: 空/单行/重复键/越界草稿一律 null——与 schema/native 落空一致。
  if (commitCurveRows([]) !== null || commitCurveRows([{ key: 0, value: 0 }]) !== null) throw new Error('U17B-ED reverse: <2 rows must fail closed to null');
  const dupED: CurveRow[] = [{ key: 0.5, value: 0.5 }, { key: 0.5, value: 0.6 }];
  if (commitCurveRows(dupED) !== null || curveRowsIssue(dupED, 'hint') === null) throw new Error('U17B-ED reverse: duplicate key must fail closed');
  const oobED: CurveRow[] = [{ key: 0, value: 0 }, { key: 1, value: 1.5 }];
  if (commitCurveRows(oobED) !== null || curveRowsIssue(oobED, 'hint') === null) throw new Error('U17B-ED reverse: out-of-range value must fail closed');
  // 默认回退: modelValue=null → 草稿 = HC 默认曲线。
  const defaultDraftED = draftCurveRows(null, defaults);
  if (defaultDraftED.length !== 6 || JSON.stringify(commitCurveRows(defaultDraftED)) !== JSON.stringify(Array.from(defaults))) {
    throw new Error('U17B-ED default: null draft must be HC default curve');
  }
}
// ---------------------------------------------------------------- U5B-ED 敏感曲线 UI 编辑器正反推演
{
  const defaults = defaultSensitivityCurvePairs();
  const hcDefault = createDefaultMotionSensitivityArray();
  // 正向: 编辑器默认 = HC 49 节点 i/(N-1) 全 0.5，逐点一致。
  if (defaults.length !== 49 || defaults.length !== hcDefault.length || defaults[48][0] !== 1 ||
      defaults.some((p, i) => p[0] !== hcDefault[i][0] || p[1] !== hcDefault[i][1])) {
    throw new Error('U5B-ED fwd: sensitivity defaults must equal HC 49-node array pointwise');
  }
  // 正向: 乱序自定义输入 → commit 排序保留；反向: 幂等。
  const bumpED: EditableCurve = [[0.2, 0.7], [0.8, 0.9], [0, 0.3], [1, 1]];
  const c1ED = commitCurveRows(draftCurveRows(bumpED, defaults));
  if (!c1ED || c1ED.length !== 4 || c1ED[0][0] !== 0 || c1ED[3][0] !== 1 ||
      !c1ED.every((p, i) => i === 0 || p[0] > c1ED[i - 1][0])) {
    throw new Error(`U5B-ED fwd: sensitivity custom commit mismatch ${JSON.stringify(c1ED)}`);
  }
  if (JSON.stringify(commitCurveRows(draftCurveRows(c1ED, defaults))) !== JSON.stringify(c1ED)) {
    throw new Error('U5B-ED reverse: sensitivity commit must be idempotent');
  }
  // 反向: 手动复算 HC 公式 (Σ wᵢ·vᵢ / k) × 2, k=2, w=1/(1+d)。
  // 默认 49 节点全 0.5: posAbs=0.5 最近节点 0.5(d=0,v=0.5) 与 ±1/48 等距 0.520833(d=1/48,v=0.5)
  //   → 0.5·(1 + 48/49) = 0.9897959183673469（恰为 U5B-DEF 不等权教训值）
  // 自定义 bump [0,0.3],[0.2,0.7],[0.8,0.9],[1,1]: posAbs=0.5 最近 0.2 与 0.8(d 均 0.3)
  //   → (0.7 + 0.9)/(1+0.3) = 16/13 ≈ 1.2307692307692307（可 >1, 后续级再钳制, HC 语义）
  const gBump = applyCustomSensitivity(0.5 * HC_GYRO_THRESHOLD_DPS, HC_GYRO_THRESHOLD_DPS, c1ED);
  const gFlat = applyCustomSensitivity(0.5 * HC_GYRO_THRESHOLD_DPS, HC_GYRO_THRESHOLD_DPS, hcDefault);
  const expectFlat = 0.5 * (1 + 48 / 49);
  const expectBump = 16 / 13;
  if (!near(gFlat, expectFlat, 1e-9) || !near(gBump, expectBump, 1e-9)) {
    throw new Error(`U5B-ED reverse: formula mismatch flat=${gFlat}(want ${expectFlat}) bump=${gBump}(want ${expectBump})`);
  }
}

console.log('gyro UI forward/reverse derivation selftest: PASS');