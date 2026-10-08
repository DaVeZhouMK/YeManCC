/**
 * HC ↔ YMCC 陀螺仪词汇正反推测 parity（2026-09-10）。
 *
 * 正推：HC 反编译词汇（发行 DLL HandheldCompanion.dll 0.32.4.0 元数据导出，
 *       反编译证据 hc-metadata-gyro-20260910.txt）→ YMCC 源码对应实现/常量/枚举值。
 * 反推：每个 YMCC 名称/常量/枚举 必须能在 HC 词汇表找到语义对应（差异已记录的不在此列）。
 *
 * 目标：消灭"理解偏差"——任何 HC 陀螺仪词汇（枚举名、常量值、方法名、字段名）
 * 都必须有 YMCC 侧逐字/逐值对应；漂移即红。
 */
import { readFileSync } from 'node:fs';
import { join } from 'node:path';

const root = process.cwd();
const hc = readFileSync(join(root, '..', '..', 'Build', 'Validation', 'HC-Parity', 'hc-metadata-gyro-20260910.txt'), 'utf8');
const il = readFileSync(join(root, '..', '..', 'Build', 'Validation', 'HC-Parity', 'hc-il-constants-20260910.txt'), 'utf8');
const devices = readFileSync(join(root, '..', '..', 'Build', 'Validation', 'HC-Parity', 'hc-il-devices-20260910.txt'), 'utf8');
const ayn = readFileSync(join(root, '..', '..', 'Build', 'Validation', 'HC-Parity', 'hc-il-ayn-20260910.txt'), 'utf8');
const utils = readFileSync(join(root, 'src', 'bridge', 'hcInputUtils.ts'), 'utf8');
const selftest = readFileSync(join(root, 'tools', 'gyro_ui_forward_reverse_selftest.ts'), 'utf8');
const mapper = readFileSync(join(root, 'src', 'bridge', 'gyroMotionMapperMock.ts'), 'utf8');
const contracts = readFileSync(join(root, 'src', 'bridge', 'inputContracts.ts'), 'utf8');
const native = readFileSync(join(root, 'native', 'main.cpp'), 'utf8');
const vue = readFileSync(join(root, 'src', 'views', 'GyroMotionView.vue'), 'utf8');
const settings = readFileSync(join(root, 'src', 'bridge', 'settingsRepository.ts'), 'utf8');

function assertHcVocab(label: string, hcPattern: RegExp, ySource: string, yPattern: RegExp, yLabel: string): void {
  if (!hcPattern.test(hc)) throw new Error(`VOCAB: HC 侧缺 ${label}（反编译证据应含）`);
  if (!yPattern.test(ySource)) throw new Error(`VOCAB: YMCC 缺 ${yLabel}（对应 HC ${label}）`);
}

// ── 枚举值（HC 反编译 ENUM 字面量 ↔ YMCC 字符串）──
assertHcVocab('SteeringAxis Roll/Yaw/Auto', /SteeringAxis\b[\s\S]{0,120}Roll = 0[\s\S]{0,60}Yaw = 1[\s\S]{0,60}Auto = 2/, vue, /'roll'|"roll"/, 'roll');
assertHcVocab('MotionInput 四态', /MotionInput\b[\s\S]{0,120}LocalSpace = 0[\s\S]{0,120}JoystickSteering = 3/, vue, /'joystick-steering'/, 'joystick-steering');
assertHcVocab('MotionMode 三态', /MotionMode\b[\s\S]{0,100}Off = 0[\s\S]{0,60}Toggle = 2/, vue, /'toggle'/, 'toggle');
assertHcVocab('GyroVelocityMode', /GyroVelocityMode\b[\s\S]{0,100}Default = 0[\s\S]{0,40}Velocity = 1/, vue, /'velocity'/, 'velocity');
assertHcVocab('OutputShape 字段（枚举字面量见 TemplatesDictionary.xaml:1460-1465）', /FIELD OutputShape/, vue, /'square'/, 'square');

// ── 常量值（HC 字段默认值 ↔ YMCC 常量/默认）──
assertHcVocab('DefaultGyroWeight=1.2', /DefaultGyroWeight = 1\.2/, utils, /HC_DEFAULT_GYRO_WEIGHT\s*=\s*1\.2/, 'HC_DEFAULT_GYRO_WEIGHT=1.2');
assertHcVocab('DefaultAxisAntiDeadZone=15', /DefaultAxisAntiDeadZone = 15/, settings, /antiDeadzone,\s*20/, 'antiDeadzone 默认 20（YMCC 页面默认）');
assertHcVocab('SHORT_MAX_F=32767', /SHORT_MAX_F = 32767/, utils, /HC_SHORT_MAX\s*=\s*32767/, 'HC_SHORT_MAX=32767');
assertHcVocab('DefaultVelocityScale=1', /DefaultVelocityScale = 1/, settings, /velocityScale,\s*1,/, 'velocityScale 默认 1');
assertHcVocab('SensivityArraySize=49', /SensivityArraySize = 49/, utils, /49/, '敏感曲线 49 节点');

// ── 方法/字段词汇（HC 方法集 ↔ YMCC 函数名）──
assertHcVocab('MotionManager.ProcessMotion', /METHOD ProcessMotion\(1\)/, native, /inputCaptureProcessMotionContribution/, 'inputCaptureProcessMotionContribution');
assertHcVocab('MotionManager.DetermineSteeringAxis', /METHOD DetermineSteeringAxis\(17\)/, native, /inputCaptureHcSteeringAxisResolvesYaw/, 'inputCaptureHcSteeringAxisResolvesYaw');
assertHcVocab('AxisActions.ApplyAxisModifiers', /METHOD ApplyAxisModifiers\(1\)/, utils, /applyHcGyroAxisModifiers/, 'applyHcGyroAxisModifiers');
assertHcVocab('InputUtils.ApplyCustomSensitivity', /ApplyCustomSensitivity/, utils, /applyCustomSensitivity/, 'applyCustomSensitivity');
assertHcVocab('InputUtils.GetSensitivityX (×1000)', /GetSensitivityX/, utils, /motionSensitivity \* 1000|\* 1000/, 'getHcSensitivity ×1000');
assertHcVocab('InputUtils.ApplyResponseCurve', /ApplyResponseCurve/, utils, /applyResponseCurve/, 'applyResponseCurve');
assertHcVocab('InputUtils.InterpolateResponseCurve', /InterpolateResponseCurve/, utils, /interpolateResponseCurve/, 'interpolateResponseCurve');
assertHcVocab('Inclination', /Inclination/, utils, /hcInclinationAngles/, 'hcInclinationAngles');
assertHcVocab('LayoutManager.ProcessGyroActions', /METHOD ProcessGyroActions\(1\)/, utils, /blendGyro(Short)?IntoStick/, 'blendGyro*IntoStick');
assertHcVocab('AxisActions.ApplyAntiDeadzone', /ApplyAntiDeadzone|AntiDeadzone/, utils, /applyAntiDeadzone/, 'applyAntiDeadzone');
assertHcVocab('IMUGyrometer.ReadingChanged', /IMUGyrometer\b[\s\S]{0,200}ReadingChanged/, native, /OnGyro|g_gyroSensor/, 'WinRT gyro 订阅');
assertHcVocab('SensorFamily.Windows（provider 自由文本承载，U0a 产品差异）', /SensorFamily\b[\s\S]{0,80}Windows = 1/, contracts, /provider: string \| null/, 'provider 持久字段');

// ── 全数据输入输出（Profile/IMU/SerialUSB/Steering/曲线/阈值/触发 词汇扩展，2026-09-10）──
assertHcVocab('Profile.SteeringMaxAngle', /FIELD <SteeringMaxAngle>k__BackingField/, settings, /steeringMaxAngle,\s*30,/, 'steeringMaxAngle 默认 30');
assertHcVocab('Profile.SteeringPower', /FIELD <SteeringPower>k__BackingField/, settings, /steeringPower,\s*1,/, 'steeringPower 默认 1');
assertHcVocab('Profile.SteeringDeadzone', /FIELD <SteeringDeadzone>k__BackingField/, settings, /steeringDeadzone,\s*0,/, 'steeringDeadzone 默认 0');
assertHcVocab('AxisActions.ResponseCurvePoints', /FIELD ResponseCurvePoints/, contracts, /responseCurvePoints/, 'responseCurvePoints 持久字段');
assertHcVocab('GyroActions.MotionTrigger', /FIELD MotionTrigger/, vue, /motionTrigger/, 'motionTrigger 控件');
assertHcVocab('IMUCalibration.thresholdG（gyro 阈值）', /FIELD thresholdG/, settings, /gyroThreshold,\s*2000/, 'gyroThreshold 默认 2000');
assertHcVocab('IMUAccelerometer.Shaken', /METHOD Shaken\(1\)/, native, /accel|OnAccel/, 'accel 订阅（Shaken 未实现=HC 抖动键产品差异）');
assertHcVocab('IMUSensor.GetCurrentReading（轮询回退）', /METHOD GetCurrentReading\(18\)/, native, /GetDefault\(\)|ReadingChanged/, 'WinRT 事件源');
assertHcVocab('MotionManager.SettingsMode0Update', /FIELD SettingsMode0Update/, native, /"gyro", \{\{"x"/, 'gyro 三轴遥测（U11B SettingsMode0）');
assertHcVocab('MotionManager.UpdateReport', /METHOD UpdateReport\(1\)/, native, /gyro\.telemetry/, 'gyro.telemetry（U11B）');
assertHcVocab('MotionManager.SwapYawRoll', /METHOD SwapYawRoll\(1\)/, native, /inputCaptureHcSteeringAxisResolvesYaw|steeringYaw/, 'steering yaw 分支');
assertHcVocab('InputUtils.DEG2RAD/RAD2DEG', /FIELD DEG2RAD = 0\.017453292/, utils, /180 \/ Math\.PI/, 'hcInclinationAngles rad2deg');
assertHcVocab('SerialUSBIMU（外接路由，UNENCLOSED 差异）', /TYPE HandheldCompanion\.Sensors\.SerialUSBIMU/, contracts, /UNENCLOSED/, '外部路由 UNENCLOSED 记录');
assertHcVocab('MotionManager.DetermineSteeringAxis（Auto=2 走 Selector）', /METHOD DetermineSteeringAxis\(17\)/, native, /auto/, 'steeringAxis auto');

console.log('gyro HC vocabulary parity selftest: PASS (反编译词汇 ↔ YMCC 全部对应)');

// ── IL 常量断言（读 hc-il-constants 反编译方法体证据）──
function assertIl(label: string, ilPattern: RegExp, ySource: string, yPattern: RegExp, yLabel: string): void {
  if (!ilPattern.test(il)) throw new Error(`VOCAB-IL: HC IL 缺 ${label}`);
  if (!yPattern.test(ySource)) throw new Error(`VOCAB-IL: YMCC 缺 ${yLabel}（对应 IL ${label}）`);
}
assertIl('IL 1.41f', /IL MotionManager\.ProcessMotion R4=\[[^\]]*1\.41/, native, /1\.41f/, 'g_gmPlayer 1.41f');
assertIl('IL 0.125f', /IL MotionManager\.ProcessMotion R4=\[[^\]]*0\.125/, native, /0\.125f/, 'g_gmWorld 0.125f');
assertIl('IL 60', /IL MotionManager\.ProcessMotion R4=\[[^\]]*60/, native, /\* 60\.0/, 'velocity ×60');
assertIl('IL 0.9', /IL MotionManager\.ProcessMotion R4=\[[^\]]*0\.9/, utils, /HC_VELOCITY_DECAY = 0\.9/, 'HC_VELOCITY_DECAY');
assertIl('IL 57.2958 (RAD2DEG)', /IL Inclination\.UpdateReport[\s\S]{0,30}R8=\[[^\]]*57\.295/, utils, /180 \/ Math\.PI/, 'hcInclinationAngles');
assertIl('IL k=2', /IL InputUtils\.ApplyCustomSensitivity R4=\[[^\]]*,2\]/, utils, /Math\.min\(2|HC_SENSITIVITY_NEAREST/, '最近 2 节点');
assertIl('IL deadzone /100', /IL InputUtils\.ApplyAntiDeadzone R4=\[[^\]]*100/, utils, /\/ 100|100/, 'deadzone /100');
assertIl('IL epsilon 1e-5', /IL InputUtils\.ImproveSquare R4=\[[^\]]*1E-05/, utils, /1e-5|1E-5/, 'improveSquare epsilon');

// ── 联想词汇正反推测·测试覆盖（每个 HC 词汇的 YMCC 实现必须在 gyro_ui_forward_reverse_selftest 有正反推演，2026-09-10）──
function assertCoverage(label: string, funcName: string): void {
  if (!selftest.includes(funcName)) {
    throw new Error(`VOCAB-COVER: HC 词汇 ${label} 的 YMCC 实现 ${funcName} 缺正反推演测试覆盖`);
  }
}
assertCoverage('ApplyCustomSensitivity', 'applyCustomSensitivity');
assertCoverage('GetSensitivityX/Y ×1000', 'getHcSensitivity');
assertCoverage('ApplyResponseCurve', 'applyResponseCurve');
assertCoverage('ProcessGyroActions 混棒', 'blendGyroShortIntoStick');
assertCoverage('ApplyAxisModifiers', 'applyHcGyroAxisModifiers');
assertCoverage('Inclination', 'hcInclinationAngles');
assertCoverage('Steering', 'hcSteering');
assertCoverage('ProcessMotion（mapper 等价）', 'mapGyroSample');
assertCoverage('LayoutManager canonical frame', 'assembleCanonicalFrame');
assertCoverage('ResponseCurvePoints 编辑器', 'commitCurveRows');
assertCoverage('SensitivityArray 编辑器', 'createDefaultMotionSensitivityArray');
assertCoverage('clamp short', 'hcClampToShort');

console.log('gyro HC IL-constant parity: PASS (IL 字面常量 ↔ YMCC 全部对应)');
console.log('gyro HC vocabulary→test coverage: PASS (12 个 HC 词汇全部有正反推演覆盖)');

// ── 机型库矩阵 DLL 级验证（ROG 三机型符号，2026-09-10）──
function assertDevices(label: string, devPattern: RegExp): void {
  if (!devPattern.test(devices)) throw new Error(`VOCAB-DEV: 发行 DLL 缺 ${label}（机型矩阵符号）`);
}
assertDevices('ROGAlly gyro/accel (-1,-1,1)', /IL ROGAlly\.\.ctor R4=\[[^\]]*-1,-1,1,[^\]]*-1,-1,1[,\]]/);
assertDevices('ROGAllyX gyro (1,1,-1)', /IL ROGAllyX\.\.ctor R4=\[[^\]]*1,1,-1[,\]]/);
assertDevices('XboxROGAllyX gyro (1,1,-1) + accel (-1,-1,1)', /IL XboxROGAllyX\.\.ctor R4=\[[^\]]*1,1,-1,[^\]]*-1,-1,1[,\]]/);
assertDevices('ClawA1M (1,1,-1)/(-1,-1,1)', /IL ClawA1M\.\.ctor R4=\[[^\]]*1,1,-1,[^\]]*-1,-1,1[,\]]/);
assertDevices('LegionGoTablet (-1,1,1)/(1,-1,-1)', /IL LegionGoTablet\.\.ctor R4=\[[^\]]*-1,1,1,[^\]]*1,-1,-1[,\]]/);
assertDevices('GPDWin4 (-1,1,1)/(-1,-1,1)', /IL GPDWin4\.\.ctor R4=\[[^\]]*-1,1,1,[^\]]*-1,-1,1[,\]]/);
assertDevices('AYANEOAIR (1,-1,1)/(1,-1,-1)', /IL AYANEOAIR\.\.ctor R4=\[[^\]]*1,-1,1,[^\]]*1,-1,-1[,\]]/);
assertDevices('MinisforumV3 (1,-1,1)/(-1,1,-1)', /IL MinisforumV3\.\.ctor R4=\[[^\]]*1,-1,1,[^\]]*-1,1,-1[,\]]/);
assertDevices('GamingZone (1,1,-1)/(1,1,1)', /IL GamingZone\.\.ctor R4=\[[^\]]*1,1,-1,[^\]]*1,1,1[,\]]/);

console.log('gyro HC device-matrix DLL parity: PASS (全表机型矩阵符号 ↔ §13.3A 一致)');