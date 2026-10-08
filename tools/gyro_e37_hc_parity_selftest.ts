import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { mapGyroSample } from '../src/bridge/gyroMotionMapperMock';

const capturePath = resolve(process.cwd(), '../../Build/Validation/HC-Parity/R1-ROG-IMU-CAPTURE-20260904-6.jsonl');
const rows = readFileSync(capturePath, 'utf8').split(/\r?\n/).filter(Boolean).map((line) => JSON.parse(line) as Record<string, any>);
const samples = rows.filter((row) => row.kind === 'sample' && row.imu?.gyro === true && Number.isFinite(row.imu.gx) && Number.isFinite(row.imu.gy) && Number.isFinite(row.imu.gz));
if (samples.length < 20) throw new Error(`E-37 capture too small: ${samples.length}`);

const mag = (row: Record<string, any>) => Math.hypot(row.imu.gx, row.imu.gy, row.imu.gz);
const rest = samples.slice(-5);
const motion = samples.reduce((best, row) => mag(row) > mag(best) ? row : best, samples[0]);
const restMag = rest.reduce((sum, row) => sum + mag(row), 0) / rest.length;
if (!(mag(motion) > restMag * 8)) throw new Error(`E-37 motion/rest ratio too small motion=${mag(motion)} rest=${restMag}`);

const context = { runId: 'e37', epoch: 1, powerGeneration: 1, targetId: 'rog', configRevision: 1 };
const config = { schemaVersion: 1 as const, revision: 1, closure: 'CLOSED' as const, provider: 'windows-imu', unit: 'deg/s', calibrationId: 'hc-gamepadmotion-manual', outputMode: 'virtual-stick' as const, motionMode: 'on' as const, motionInput: 'local-space' as const, gyroMultiplier: 1, motionSensitivityX: 1, motionSensitivityY: 1, invertHorizontal: false, invertVertical: false };
for (const [index, row] of [...rest, motion].entries()) {
  const rawOnly = mapGyroSample(context, config, { sequence: 1000 + index, timestamp: Number(row.t) });
  if (rawOnly.ok || rawOnly.reason !== 'default-plane-unproven') {
    throw new Error(`E-37 raw capture ${index} was incorrectly admitted as HC Default plane`);
  }
}
console.log(`E-37 raw-capture admission gate: PASS restMag=${restMag.toFixed(3)} motionMag=${mag(motion).toFixed(3)}; raw capture is not HC Default-plane proof`);
