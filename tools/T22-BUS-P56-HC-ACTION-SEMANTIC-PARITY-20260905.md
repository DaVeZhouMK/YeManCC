# T22-BUS-P56：HC action/parameter/lifecycle 语义对齐审计（2026-09-05）

## 范围与证据身份

本页是 BUS 对锁定 HC `0.32.4.0 / 06c0b9544db2b1f39abf9cd3796225ccfb096103`、当前 YMCC source 和既有任务书的静态三方对账。未启动产品、InputHost、HIDMaestro、HidHide、Steam/游戏、虚拟设备或真实硬件；本页不构成 runtime closure。

## 已确认事实（Fact）

1. HC `Actions/GyroActions.cs` 的触发条件是完整 `ButtonState`，`Managers/MotionManager.cs` 通过 `MotionTogglePressed` 形成 chord/release-edge/debounce 语义。
2. 当前 YMCC 合同 `src/bridge/inputContracts.ts` 与 `src/bridge/gyroMotionMapperMock.ts` 仍使用单字符串 `motionTrigger` 和外部 `toggleOn` fixture；没有 HC 等价的 ButtonState chord/release-edge 消费链。
3. HC `Managers/MotionManager.cs` 存在 ADS trigger/multiplier/sensitivity 消费链（custom sensitivity → aiming trigger/multiplier → velocity → `SensitivityX/Y` → clamp）；当前 YMCC `GyroMotionConfigV1`、mapper 和 virtual report assembler 没有对应字段或 production consumer receipt。
4. HC `SetupMotion` 在 producer plane 消费 `GyrometerMultiplier` / `AccelerometerMultiplier`，再按 `SteeringAxis` 做 yaw/roll 变换。当前 YMCC 将 `gamepadMotionPlane` 声明为已处理平面，但没有证明 native producer → Host 的真实 ownership；不能在 mapper 叠加倍率。
5. HC Windows sensor 读取在同一 controller tick 进入 `SensorsManager.UpdateReport()` 后再调用 `GamepadMotion.ProcessMotion()`；独立 `ReadingChanged` 缓存本身不证明同 provider、same hardware frame 或 same generation。YMCC 当前 `pairProven=false` 与这一安全边界相容。
6. `InputHost/Program.cs` 当前只对 `motionPairProofId` 做非空检查；未来 direct-IMU 开启时，尚未证明该 ID 已登记、与 source/epoch/config 绑定。

## HC / YMCC / MD 三方裁决

| 对账项 | HC authority | 当前 YMCC | 裁决 |
|---|---|---|---|
| 触发语义 | ButtonState chord + release edge/debounce | 单字符串 token + fixture toggle | `HC-SEMANTIC-ENCODING-GAP / UNENCLOSED` |
| ADS | trigger/multiplier/sensitivity 消费 | 字段/路径缺失 | `G-AN7 parameter-consumption-gap / UNENCLOSED` |
| multiplier / SteeringAxis | producer plane 消费 | producer ownership 未证明 | `UNKNOWN`，不得重复乘倍率 |
| gyro/accel pairing | 同 tick 更新后处理 | `pairProven=false` | fail-closed 正确，runtime 未闭合 |
| motionPairProofId | HC 无此 YMCC 外壳字段 | 仅非空验证 | `LATENT-PROTOCOL-GAP / UNKNOWN` |

## 错误、未知与原创逻辑

- 本轮没有发现新的、可按 HC 无歧义直接修正的轴交换、符号、阈值、velocity decay、gyroWeight 或生命周期顺序偏移。
- `pairProven=false`、safe-zero、direct IMU no-report 不是原创漂移公式，而是当前安全门；不应为“让图动起来”而绕过。
- 单字符串 trigger、缺少 ADS 消费、producer ownership 未绑定和非空 proof 检查均已确认，但涉及 T10/T11/T12 合同边界；本轮只记录，不自行扩展 schema 或打开 direct-IMU。

## 缺口与下一门

```text
HC chord/debounce parity              = UNENCLOSED
ADS parameter-consumption parity      = UNENCLOSED
producer-plane multiplier ownership   = UNKNOWN
motionPairProof registry/binding      = LATENT-UNKNOWN
same-provider/generation proof        = UNENCLOSED
descriptor/readback/Host first-frame  = UNENCLOSED
P-HID/P-XINPUT/P-OWNER                = UNENCLOSED
Steam/game consumer + recovery       = RUNTIME-BLOCKED
```

在 direct-IMU、实际 Host、Steam/游戏消费者或 ROG 设备路由获得独立证据前，不将上述项升级为通过，不新增 DGF，也不把 HC 的未决 `todo/unknown` 值转成 YMCC 参数。

