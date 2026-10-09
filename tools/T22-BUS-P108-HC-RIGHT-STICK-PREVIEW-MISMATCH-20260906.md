# T22 BUS-P108 — HC 右摇杆预览未对上修正（2026-09-06）

## 1. 触发问题

用户指出页面曾显示：

```text
本地 HC 公式模拟预览（fixture；不写入设备）：
未准入：unclosed-config（零输出）
```

该文字没有明确告诉操作者“HC 右摇杆没有对上”。更严重的是，页面在 canonical right-stick assembly 失败时曾退回显示 `preview.frame.contribution`，会把未完成 HC 右摇杆合成误画成一个可用的右摇杆结果。

## 2. HC authority（source fact）

锁定 HC 源码中的右摇杆路线为：

1. `deps/handheldcompanion-runtime/source/Actions/GyroActions.cs`
   - `DefaultAxisLayoutFlags = AxisLayoutFlags.RightStick`
   - `DefaultGyroWeight = 1.2f`
2. `deps/handheldcompanion-runtime/source/Managers/LayoutManager.cs`
   - `ProcessGyroActions()` 取得当前 gyro action 的 `AxisActions.Axis`；
   - 对 `ActionType.Joystick` 执行 `AxisActions.Execute()`；
   - 通过 `_axisXY[aA.Axis]` 取得目标轴对；
   - 读取当前 target stick，并按 `current + aA.GetValue() * (gyroWeight - stickNorm)` 合成。

因此：只有 canonical assembly 成功并明确 target=`RightStick`，预览才可称为“HC 右摇杆已对上”。raw gyro contribution 不是右摇杆消费结果。

## 3. 本轮 source fix

修改：`src/views/GyroMotionView.vue`

- `unclosed-config`、缺少 GamepadMotion plane、触发状态未解析、目标不是右摇杆等情况，统一显示带有明确前缀的错误：

  ```text
  错误：HC 右摇杆未对上（…；rightStick=0，零输出）
  ```

- `outputStick !== 'right'` 明确显示目标不是 HC 右摇杆。
- canonical `assembleCanonicalFrame()` 失败时不再 fallback 到 `preview.frame.contribution`，而是保持 `rightStick=0` 并报告合成失败。
- canonical assembly 成功时才显示：

  ```text
  HC 右摇杆已对上（fixture；不写入设备，仍非 Host/HID/Steam/游戏回读）
  ```

- 增加错误/成功颜色，仅改善可见诊断，不改变 mapper、pair gate、GamepadMotion、InputHost、HID、Steam 或游戏路线。

## 4. 验证

| 检查 | 结果 |
|---|---|
| `pnpm exec vue-tsc --noEmit` | PASS |
| `tools/gyro_motion_mapper_mock_selftest.ts` | PASS |
| `tools/assisted_stick_assembler_selftest.ts` | PASS |
| 真实 `pairProven` / GamepadMotion / Host / HID / Steam / 游戏 consumer | 未运行，仍 RUNTIME-BLOCKED |

## 5. 证据与缺口分离

### 已证明

- 当前页面能明确区分“HC 右摇杆未对上”和“fixture 右摇杆已对上”。
- fixture 不再把 raw contribution 冒充 canonical right-stick result。
- HC 右摇杆合成公式和目标轴选择仍与锁定 HC source 对齐。

### 未证明 / 未关闭

- ROG 真实 provider/physical identity、same-provider gyro/accel pair、HC selector/matrix/calibration、first paired sample。
- native → GamepadMotion → Coordinator → InputHost → HIDMaestro → Steam/game 的真实右摇杆 consumer receipt。
- 页面 fixture 的成功状态不代表 Host/HID/Steam/game active。

## 6. 主线影响

本页是 P108 source/fixture 诊断修正。由于修改了 current source，`T22-RF00 → T20 → T21 → T14` 的 current-source refresh 仍需重新执行；本修正不解除 `pairProven=false`、`outX/outY=0`、direct-IMU=`no-report` 或 `runtimeUpgrade=false`。

