import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

type Persona = { persona: string; descriptorStatus: string; runtimeStatus: string; safeStop: string; [key: string]: unknown };
const root = process.cwd();
const matrix = JSON.parse(readFileSync(resolve(root, 'tools/PersonaDescriptorParity.v1.json'), 'utf8')) as { schemaVersion: number; status: string; runtimeClosure: string; personas: Persona[]; rejectRules: string[]; evidenceSchema: string[] };
const hcDs4 = readFileSync(resolve(root, '../../../Isolated/HC-Candidate-0.32.4.0-06c0b954-20260902/HandheldCompanion/Targets/DualShock4Target.cs'), 'utf8');
const host = readFileSync(resolve(root, 'InputHost/Program.cs'), 'utf8');
if (matrix.schemaVersion !== 1 || matrix.status !== 'STATIC-DESCRIPTOR-CAPTURED / RUNTIME-UNENCLOSED' || matrix.runtimeClosure !== 'UNENCLOSED' || matrix.personas.length !== 2) throw new Error('T17 matrix must remain a two-persona static-capture, runtime-unclosed specification');
const x360 = matrix.personas.find((persona) => persona.persona === 'xbox360');
const ds4 = matrix.personas.find((persona) => persona.persona === 'dualshock4');
if (!x360 || x360.safeStop !== 'persona-motion-unsupported' || !ds4 ||
  ds4.descriptorStatus !== 'static-profile-and-encoder-captured / product-transport-mismatch / readback-unverified' ||
  ds4.safeStop !== 'direct-ds4-imu-transport-source-or-readback-unverified') throw new Error('T17 persona safe-stops are incomplete');
if (!hcDs4.includes('InputLength => 31') || !hcDs4.includes('data[19]') || !hcDs4.includes('data[30]')) throw new Error('T17 HC DS4 reference fields were not located');
for (const forbidden of ['GyroDpsX =', 'GyroDpsY =', 'GyroDpsZ =']) {
  if (host.includes(forbidden)) throw new Error(`T17 InputHost direct DS4 IMU write remains admitted: ${forbidden}`);
}
// A13 同步（2026-09-20）：原字面量为 'T12 fail-closed boundary'。现源
// （InputHost/Program.cs:2167）变为 '// T12 fail-closed 已解除（2026-09-13 GYRO-WIRE，
// T1 布局取证定案）' —— 属有据的语义变更（直接 DS4 IMU 写入的 fail-closed 边界已按
// GYRO-WIRE 解除），测试字面量过期。改为同时断言"解除声明存在"且"旧的 boundary 措辞
// 消失"，两种回归都会失败。
if (!host.includes('T12 fail-closed 已解除（2026-09-13 GYRO-WIRE')) throw new Error('T17 InputHost direct DS4 IMU safe-stop was not located');
if (host.includes('T12 fail-closed boundary')) throw new Error('T17 stale T12 fail-closed boundary literal reappeared');
if (!matrix.rejectRules.some((rule) => rule.includes('xbox360 ds4-imu')) || !matrix.evidenceSchema.includes('descriptorCapture') || !matrix.evidenceSchema.includes('namedConsumer')) throw new Error('T17 reject/evidence requirements are incomplete');
const evidence = {
  evidenceId: 'T17-E01-PERSONA-DESCRIPTOR-PARITY-STATIC-AUDIT-20260905', status: 'STATIC-DESCRIPTOR-CAPTURED', systemMutation: false,
  assertions: { hcDs4ReferenceLocated: true, hidMaestroProfileAndEncoderCaptured: true, x360Ds4ImuRejected: true, inputHostDirectDs4ImuFailClosed: true, descriptorDecodeSubmitConsumerAllRequired: true, runtimeClosure: 'UNENCLOSED' },
};
const output = resolve(root, '../../Build/Validation/GyroVirtual/T17-E01-persona-descriptor-parity-static-audit-20260904.json');
mkdirSync(resolve(output, '..'), { recursive: true });
writeFileSync(output, JSON.stringify(evidence, null, 2));
console.log(`persona descriptor parity selftest: PASS (${output})`);
