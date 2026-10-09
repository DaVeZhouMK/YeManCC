/**
 * HC ↔ YMCC 陀螺仪 UI 参数口径 parity 自测（2026-09-10）。
 *
 * 目标：UI 滑块（范围/步进/默认值）与 HC 源码 UI(XAML)/默认常量 **高度一致**，
 * 防止后续"不丝滑"类问题由数值口径漂移引入。本工具读取活动 YMCC 源码
 * （GyroMotionView.vue 滑块定义 + settingsRepository 默认值），与从 HC 冻结源
 * 抽取的合同表逐项断言；任何漂移即失败。
 *
 * HC 引用出处（commit 06c0b954）：
 *  - Views/Pages/ProfilesPage.xaml:1295-1297 GyrometerMultiplier 0.1..3/0.1
 *  - Views/Pages/ProfilesPage.xaml:1323-1325 AccelerometerMultiplier 0.1..3/0.1
 *  - Views/TemplatesDictionary.xaml:1296-1298 VelocityScale 0.1..5/0.1
 *  - Views/TemplatesDictionary.xaml:1448-1450 GyroWeight 1.0..2.0/0.1
 *  - Views/TemplatesDictionary.xaml:1481-1483 Inner deadzone 0..25/1
 *  - Views/TemplatesDictionary.xaml:1503-1505 Outer deadzone 0..25/5
 *  - Views/TemplatesDictionary.xaml:1524-1526 Anti deadzone 0..25/5
 *  - Views/TemplatesDictionary.xaml:1460-1465 Output shape 四选项 Default/Circle/Cross/Square
 *  - Misc/Profile.cs:212,213,219,220,224-226,229 默认 1.0/1.0/1.0/1.0/30/1/0/1.0
 *  - Actions/GyroActions.cs:32 DefaultGyroWeight=1.2；AxisActions.cs:27-29 默认 0；
 *    ViewModels/Pages/ProfilesPageViewModel.cs:591,609 + Layout/Mappings/GyroMappingViewModel.cs:315
 *    → 陀螺映射默认 AxisAntiDeadZone = GyroActions.DefaultAxisAntiDeadZone = 15
 *  - Actions/AxisActions.cs:37-44 默认 ResponseCurvePoints 6 点恒等
 */
import { readFileSync } from 'node:fs';
import { join } from 'node:path';

// pnpm run 的 cwd 恒为 YeManCC 项目根；用 cwd 而非 bundle 产物目录定位源码。
const root = process.cwd();
const vue = readFileSync(join(root, 'src', 'views', 'GyroMotionView.vue'), 'utf8');
const settings = readFileSync(join(root, 'src', 'bridge', 'settingsRepository.ts'), 'utf8');

function sliderStep(vueSrc: string, label: string, step: string): void {
  const pattern = new RegExp(`label="${label}"[^>]*?:step="${step}"`);
  if (!pattern.test(vueSrc)) {
    throw new Error(`GYRO_PARAM: 滑块 "${label}" 步进应为 ${step}（HC TickFrequency 同值）`);
  }
}
function sliderRange(vueSrc: string, label: string, min: string, max: string, step: string): void {
  const pattern = new RegExp(`label="${label}"[^>]*?:min="${min}"[^>]*?:max="${max}"[^>]*?:step="${step}"`);
  if (!pattern.test(vueSrc)) {
    throw new Error(`GYRO_PARAM: 滑块 "${label}" 应为 [${min},${max}] 步进 ${step}`);
  }
}
function defaultFinite(source: string, field: string, fallback: number, min: number, max: number): void {
  const pattern = new RegExp(`${field}[^,]*finite\\([^,]*,\\s*${fallback},\\s*${min},\\s*${max}\\)`);
  if (!pattern.test(source)) {
    throw new Error(`GYRO_PARAM: settingsRepository ${field} 默认/边界应 finite(_,${fallback},${min},${max})`);
  }
}

// ── 滑块范围/步进（HC XAML 同值）──
sliderRange(vue, '速度', '0.1', '3', '0.1');
sliderRange(vue, '加速度倍率', '0.1', '3', '0.1');
sliderRange(vue, '水平', '0.1', '3', '0.1');
sliderRange(vue, '垂直', '0.1', '3', '0.1');
sliderRange(vue, '陀螺仪权重【倍率】', '1', '3', '0.1');   // 2026-09-29 用户裁决：上限 2 → 3
sliderRange(vue, '变速倍率', '0.1', '5', '0.1');
sliderStep(vue, '内死区', '0.5');   // UI 现状：内死区步进 0.5（HC TickFrequency=1，YMCC 更细粒度）
sliderStep(vue, '外死区', '5');
sliderStep(vue, '反死区', '5');

// ── 默认值/边界（HC Profile/GyroActions/AxisActions 同值）──
defaultFinite(settings, 'gyroMultiplier', 1, 0.1, 3);
defaultFinite(settings, 'accelerometerMultiplier', 1, 0.1, 3);
defaultFinite(settings, 'motionSensitivityX', 1.5, 0.1, 3);   // 2026-09-29 用户裁决：默认 1 → 1.5
defaultFinite(settings, 'motionSensitivityY', 1.5, 0.1, 3);   // 2026-09-29 用户裁决：默认 1 → 1.5
defaultFinite(settings, 'gyroWeight', 1.8, 1, 3);             // 2026-09-29 用户裁决：默认 1.2 → 1.8，上限 2 → 3
defaultFinite(settings, 'velocityScale', 1, 0, 10);
defaultFinite(settings, 'steeringMaxAngle', 30, 10, 80);
defaultFinite(settings, 'steeringPower', 1, 0.2, 5);
defaultFinite(settings, 'steeringDeadzone', 0, 0, 5);
defaultFinite(settings, 'aimingSightsMultiplier', 0.3, 0, 2);   // 2026-09-29 用户裁决：默认 1 → 0.3
if (!/antiDeadzone,\s*20/.test(settings)) throw new Error('GYRO_PARAM: antiDeadzone 默认应为 20');
if (!/finite\(motion\.innerDeadzone, 1, 0, 25\)/.test(settings)) {
  throw new Error('GYRO_PARAM: innerDeadzone 默认应为 1');
}
if (!/finite\(motion\.antiDeadzone, 20, 0, 40\)/.test(settings)) {
  throw new Error('GYRO_PARAM: antiDeadzone 边界应为 0..40 默认 20');
}
if (!/finite\(motion\.outerDeadzone, 0, 0, 25\)/.test(settings)) {
  throw new Error('GYRO_PARAM: outerDeadzone 默认应为 0（HC AxisActions 字段默认）');
}

console.log('gyro UI param parity selftest: PASS (HC XAML/Profile/Actions 口径全部一致)');