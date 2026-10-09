import { mapGyroSample } from '../src/bridge/gyroMotionMapperMock';
import { applyCustomSensitivity, createDefaultMotionSensitivityArray, getHcSensitivity, hcClampToShort, hcInclinationAngles, hcShortToUnit, hcSteering, HC_GYRO_THRESHOLD_DPS } from '../src/bridge/hcInputUtils';

const context = { runId: 'r', epoch: 1, powerGeneration: 1, targetId: 't', configRevision: 1 };
const config = { schemaVersion: 1 as const, revision: 1, closure: 'CLOSED' as const, provider: 'fixture', unit: 'deg/s', calibrationId: 'c', outputMode: 'virtual-stick' as const, motionMode: 'on' as const, motionInput: 'local-space' as const, gyroMultiplier: 1, motionSensitivityX: 1, motionSensitivityY: 1, invertHorizontal: false, invertVertical: true };
const nodes = createDefaultMotionSensitivityArray();
const near = (a: number, b: number) => Math.abs(a - b) < 1e-9;

function expectedLocalSpace(sample: { x: number; y: number; z: number }, invertH = false, invertV = true) {
  let x = sample.z;
  let y = sample.x;
  if (invertH) x *= -1;
  if (invertV) y *= -1;
  x *= applyCustomSensitivity(x, HC_GYRO_THRESHOLD_DPS, nodes);
  y *= applyCustomSensitivity(y, HC_GYRO_THRESHOLD_DPS, nodes);
  x *= getHcSensitivity(1);
  y *= getHcSensitivity(1);
  const shortX = hcClampToShort(x);
  const shortY = hcClampToShort(y);
  return { x: hcShortToUnit(shortX), y: hcShortToUnit(shortY), shortX, shortY };
}

function plane(gyro: { x: number; y: number; z: number }, accel = { x: 0, y: 0, z: 1 }, extra: { playerSpace?: { x: number; y: number }; worldSpace?: { x: number; y: number } } = {}) {
  return { processProven: true as const, pairProven: true as const, calibrationLocked: true as const, steeringAxis: 'roll' as const, defaultGyro: gyro, defaultAccel: accel, ...extra };
}
function local(gyro: { x: number; y: number; z: number }, sequence: number, timestamp: number, deltaSeconds?: number) {
  return { sequence, timestamp, deltaSeconds, gamepadMotionPlane: plane(gyro) };
}

if (mapGyroSample(context, config, { sequence: 0, timestamp: 1 }).ok) throw new Error('T4 admitted LocalSpace without HC Default plane');
const mapped = mapGyroSample(context, config, local({ x: 10, y: 0, z: 0 }, 1, 2));
const expect10 = expectedLocalSpace({ x: 10, y: 0, z: 0 });
if (!mapped.ok || !near(mapped.frame.contribution.x, expect10.x) || !near(mapped.frame.contribution.y, expect10.y) || mapped.frame.gyroShort?.y !== expect10.shortY) {
  throw new Error(`G1 HC LocalSpace mapping failed ${JSON.stringify(mapped)}`);
}
const still = mapGyroSample(context, config, local({ x: 0, y: 0, z: 0 }, 2, 3));
if (!still.ok || still.frame.contribution.x !== 0 || still.frame.contribution.y !== 0) throw new Error('G1 rest sample was not zero');
const disabled = mapGyroSample(context, { ...config, enabled: false }, local({ x: 0, y: 0, z: 400 }, 21, 3));
if (!disabled.ok || disabled.frame.contribution.x !== 0 || disabled.frame.contribution.y !== 0) throw new Error('T11 disabled motion leaked into preview');
const saturated = mapGyroSample(context, { ...config, invertVertical: false }, local({ x: 0, y: 0, z: 2000 }, 3, 4));
if (!saturated.ok || saturated.frame.contribution.x !== 1 || saturated.frame.gyroShort?.x !== 32767) throw new Error('G1 2000 deg/s did not saturate via GetSensitivityX');
if (mapGyroSample(context, { ...config, unit: 'rad/s' }, local({ x: 0, y: 0, z: 0 }, 4, 5)).ok) throw new Error('G1 accepted unknown unit');
if (mapGyroSample(context, { ...config, closure: 'UNENCLOSED' as const }, local({ x: 0, y: 0, z: 0 }, 5, 6)).ok) throw new Error('G1 accepted unclosed config');
if (mapGyroSample(context, { ...config, motionInput: 'player-space' }, { sequence: 6, timestamp: 7 }).ok) throw new Error('G1 admitted PlayerSpace without GamepadMotion plane');
const player = mapGyroSample(context, { ...config, gyroMultiplier: 2, motionInput: 'player-space', invertVertical: false, invertHorizontal: false }, { sequence: 6, timestamp: 7, gamepadMotionPlane: plane({ x: 0, y: 0, z: 0 }, undefined, { playerSpace: { x: 10, y: -4 } }) });
if (!player.ok || Math.abs(player.frame.contribution.x - expectedLocalSpace({ x: 10, y: 0, z: 4 }, false, false).x) > 1e-6) throw new Error('T4 PlayerSpace remap mismatch');
const world = mapGyroSample(context, { ...config, gyroMultiplier: 2, motionInput: 'world-space', invertVertical: false, invertHorizontal: false }, { sequence: 61, timestamp: 8, gamepadMotionPlane: plane({ x: 0, y: 0, z: 0 }, undefined, { worldSpace: { x: 10, y: -4 } }) });
if (!world.ok || Math.abs(world.frame.contribution.x - expectedLocalSpace({ x: 10, y: 0, z: 4 }, false, false).x) > 1e-6) throw new Error('T4 WorldSpace remap mismatch');
const multipliedPlane = mapGyroSample(context, { ...config, gyroMultiplier: 2, invertVertical: false }, local({ x: 4, y: 0, z: 8 }, 62, 9));
const multipliedWant = expectedLocalSpace({ x: 4, y: 0, z: 8 }, false, false);
if (!multipliedPlane.ok || !near(multipliedPlane.frame.contribution.x, multipliedWant.x) || !near(multipliedPlane.frame.contribution.y, multipliedWant.y)) throw new Error('T4 LocalSpace reapplied GyrometerMultiplier after Default plane');
if (mapGyroSample(context, config, { sequence: 7, timestamp: Number.NaN, gamepadMotionPlane: plane({ x: 0, y: 0, z: 0 }) }).ok) throw new Error('G1 accepted NaN sample');
const stale = mapGyroSample(context, config, local({ x: 0.2, y: 0, z: 0 }, 1, 9), { lastSequence: 1 });
if (stale.ok) throw new Error('G1 accepted stale sample');

const holdConfig = { ...config, motionMode: 'off' as const, motionTrigger: 'lt', invertVertical: false };
const idle = mapGyroSample(context, holdConfig, local({ x: 40, y: 0, z: 0 }, 10, 10));
if (!idle.ok || idle.frame.contribution.x !== 0 || idle.frame.contribution.y !== 0) throw new Error('T4 off without trigger leaked');
const held = mapGyroSample(context, holdConfig, local({ x: 40, y: 0, z: 0 }, 11, 11), { pressed: new Set(['lt']) });
const expectHeld = expectedLocalSpace({ x: 40, y: 0, z: 0 }, false, false);
if (!held.ok || !near(held.frame.contribution.y, expectHeld.y) || held.frame.contribution.y === 0) throw new Error('T4 off+trigger failed');
const suppressed = mapGyroSample(context, { ...config, motionTrigger: 'lt' }, local({ x: 40, y: 0, z: 0 }, 12, 12), { pressed: new Set(['lt']) });
if (!suppressed.ok || suppressed.frame.contribution.x !== 0) throw new Error('T4 on+trigger should mute');
const toggled = mapGyroSample(context, { ...config, motionMode: 'toggle' as const, motionTrigger: 'ls', invertVertical: false }, local({ x: 20, y: 0, z: 0 }, 13, 13), { toggleOn: true });
const expectToggle = expectedLocalSpace({ x: 20, y: 0, z: 0 }, false, false);
if (!toggled.ok || !near(toggled.frame.contribution.y, expectToggle.y)) throw new Error('T4 toggle on failed');
const small = mapGyroSample(context, { ...config, invertVertical: false, innerDeadzone: 10 }, local({ x: 8, y: 0, z: 0 }, 14, 14));
if (!small.ok || small.frame.contribution.y === 0 || small.frame.gyroShort?.y === 0) throw new Error('T4 mapper applied AxisActions deadzone; MotionManager does not');
const noAccel = mapGyroSample(context, { ...config, motionInput: 'joystick-steering', invertVertical: false }, { sequence: 15, timestamp: 15 });
if (noAccel.ok) throw new Error('T4 steering without accel was admitted');
const tilt = { x: -Math.tan(15 * Math.PI / 180), y: 0, z: 1 };
const steered = mapGyroSample(context, { ...config, motionInput: 'joystick-steering', invertVertical: false, invertHorizontal: false }, { sequence: 16, timestamp: 16, gamepadMotionPlane: plane({ x: 0, y: 0, z: 0 }, tilt) });
const wantSteer = hcShortToUnit(hcClampToShort(hcSteering(hcInclinationAngles(tilt).y) * applyCustomSensitivity(hcSteering(hcInclinationAngles(tilt).y), HC_GYRO_THRESHOLD_DPS, nodes)));
if (!steered.ok || steered.frame.contribution.y !== 0 || Math.abs(steered.frame.contribution.x - wantSteer) > 1e-6) throw new Error(`T4 JoystickSteering mismatch ${JSON.stringify(steered)} want ${wantSteer}`);
const flat = mapGyroSample(context, { ...config, motionInput: 'joystick-steering', invertVertical: false }, { sequence: 17, timestamp: 17, gamepadMotionPlane: plane({ x: 0, y: 0, z: 0 }, { x: 0, y: 0, z: 1 }) });
if (!flat.ok || flat.frame.contribution.x !== 0 || flat.frame.contribution.y !== 0) throw new Error('T4 flat steering leaked');
console.log('gyro motion mapper mock selftest: PASS');

