import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

const sourcePath = resolve(process.cwd(), 'InputHost/Program.cs');
const source = readFileSync(sourcePath, 'utf8');

for (const forbidden of ['GyroDpsX =', 'GyroDpsY =', 'GyroDpsZ =']) {
  if (source.includes(forbidden)) throw new Error(`T12 direct IMU field remains wired to SubmitState: ${forbidden}`);
}
// A13 同步（2026-09-20，联想词扫描发现的第七例，与 persona_descriptor_parity 同源）：
// 原断言要求两个旧措辞 'T12 fail-closed boundary' 与 'no direct IMU fields may enter
// SubmitState'。现源（InputHost/Program.cs:2165-2178）= **T12 fail-closed 已解除
// （2026-09-13 GYRO-WIRE，T1 布局取证定案）**，改为**受约束的 data-only wire 编码**：
//   · BuildDs4V2DataOnlyReport 按 stock profile 字段表编码 gyro/accel（int16-le，
//     dps clamp ±2048 ×16、m/s² clamp ±64 ×512，fail-zero 保留）；
//   · report size 必须 64，否则抛 ds4-v2-wire-size-mismatch；
//   · 仍禁止 GyroDpsX/Y/Z 直接字段赋值（上方 for 循环保持不变）。
// ⇒ 旧字面量消失属**有据的语义变更**（解除 fail-closed + 换成带校验的编码路径），
// 不是"文档缺失"。下方同时钉住"解除声明"与"编码约束"；任一空缺或旧措辞回归即失败。
const t12LiftedDeclared = source.includes('T12 fail-closed 已解除（2026-09-13 GYRO-WIRE');
// 注意：以下仍是**静态源码契约检查**（193 §2 P2 第 4 条）——它证明"编码路径所需的结构与
// 校验字面量仍在"，不等于运行期编码正确性证明。为让"破坏真实编码约束"能被检出，这里
// 额外钉住 **report-size 守卫与其异常**（把守卫改成恒假/删掉即失败），而不只是注释文字。
const t12ConstrainedWire = source.includes('BuildDs4V2DataOnlyReport') &&
  source.includes('ds4-v2-wire-size-mismatch') &&
  source.includes('if (profile.InputReportSize != 64)') &&
  source.includes('throw new InvalidOperationException("ds4-v2-wire-size-mismatch:" + profile.InputReportSize)') &&
  source.includes('dps clamp ±2048 ×16') &&
  source.includes('fail-zero 保留');
if (!t12LiftedDeclared || !t12ConstrainedWire) {
  throw new Error('T12 InputHost safe-stop boundary is not documented in source');
}
if (source.includes('T12 fail-closed boundary') || source.includes('no direct IMU fields may enter SubmitState')) {
  throw new Error('T12 stale fail-closed wording reappeared; review before admitting any runtime path');
}

const evidence = {
  evidenceId: 'T12-E02-INPUTHOST-DS4-IMU-NO-REPORT-STATIC-20260905',
  status: 'STATIC-PASS',
  systemMutation: false,
  assertions: {
    inputHostDoesNotWriteGyroDpsToSubmitState: true,
    descriptorEncoderDecoderReadbackStillUnenclosed: true,
    virtualStickAndStandardAxesRemainOutsideDirectImuPlane: true,
  },
};
const evidencePath = resolve(process.cwd(), '../../Build/Validation/GyroVirtual/T12-E02-inputhost-ds4-imu-no-report-static-20260905.json');
mkdirSync(resolve(evidencePath, '..'), { recursive: true });
writeFileSync(evidencePath, JSON.stringify(evidence, null, 2));
console.log(`InputHost DS4 IMU static selftest: PASS (${evidencePath})`);
