# T22 BUS-P121：控制器内页真实连接态 UI receipt

日期：2026-09-06
状态：`UI-RUNTIME-RECEIPT-PROVEN / SEMANTIC-BUTTON-RECEIPT-SEPARATE`

## 1. 本轮目标

验证用户要求的最小闭口：

```text
YMCC → 控制器内页 → 页面实际显示“已连接手柄”
```

不得用虚拟手柄、`navigator.getGamepads()` 或伪造按钮状态代替真实连接态。

## 2. 运行证据

### Native XInput

当前实例 PID `27852` 的 native lifecycle 记录：

```text
gamepad-input-registration
  rawInputRegistered=true
  fallbackTimerArmed=true

gamepad-xinput-state
  connected=true
  connectedMask=1
  primarySlot=0
  source=xinput

gamepad-state-replay
  reason=webview-render-ready
```

这证明台式机上真实 Xbox/XInput slot 0 被 native 读取，并在 WebView render-ready 后重播给 renderer。

### 控制器内页

通过窗口级 `PrintWindow` 取得真实 YMCC 控制器页图像；页面中的 `GamepadVisualizer` 条件文本显示为：

```text
手柄测试模式
```

而不是 `未检测`。源码条件锚点为 `src/components/GamepadVisualizer.vue` 的 `connected ? '手柄测试模式' : '未检测'`。

截图证据（由当前实例生成，未作为运行输入）：

```text
path=C:\Users\DaVe\Desktop\ymcc-window-pageup.png
bytes=74254
sha256=33A030A83282D322E0025BA82F9317113A02A8CEDA31B77B97D57878D78A9AD6
```

## 3. 已闭合

- native XInput slot 0 connection receipt；
- `gamepad.state` render-ready replay；
- `ipc.ts` late-subscriber snapshot replay；
- `GamepadVisualizer` 页面收到真实 native snapshot 并显示连接态；
- 未启用虚拟手柄；
- 未恢复浏览器第二物理读取器；
- 未把页面 receipt 误写成真实 A/B/LB/RB semantic runtime PASS。

## 4. 仍然分开的未知/缺口

本 receipt 只闭合“连接态页面显示”，不证明：

- 实体按钮边沿已经进入 `gamepadProcessUiInput`；
- `gamepad.ui-input` 已被 YMCC 前端消费；
- 失焦/隐藏时游戏消费者已被系统级阻断；
- ROG P-HID/P-XINPUT/虚拟 target/Steam consumer 闭口。

另见后续 P122：实体 XInput 当前存在持续非中性左摇杆值，必须先进入 neutral admission barrier，不能把它当作按钮语义通过。

