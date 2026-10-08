# T22 BUS-P117：T0 全部 YMCC 手柄语义链路复核

日期：2026-09-06  
状态：`SOURCE-FIXED / BUILD-VERIFIED / XINPUT-READBACK-PROVEN / SEMANTIC-RUNTIME-REQUIRES-PRESS`

## 1. 用户问题修正

本轮问题不是单一 LB+RB 呼出失败，而是 YMCC 内全部手柄语义均无效：

- LB+RB 呼出 YMCC；
- 双击 B 关闭/返回；
- LB/RB 内页切换；
- A/B/Y、方向键、摇杆导航与焦点控件操作。

因此本轮把检查范围提升为：

```text
XInput/Raw Input
  → native gamepadEval
  → native eligibility/owner gate
  → gamepad.ui-input / gamepad.summon
  → renderer semantic dispatch
```

## 2. BUS 只读证据

### 2.1 本机清缓存后旧实例

PID `44708`（2026-09-06 19:34:04–19:34:16）启动 receipt：

- `window-created`；
- `gamepad-input-registration(rawInputRegistered=true, fallbackTimerArmed=true, rawInputError=0)`；
- WebView `navigation-complete`、`render-ready-complete`；
- 没有 `gamepad.ui-input`、`gamepad.summon` 或按键语义 receipt。

这只能说明启动/注册闭合，不能证明用户按键已经进入语义层。

### 2.2 XInput 物理只读探针

本机 slot 0 可读，当前中性：

```text
xinput9_1_0: rc=0
xinput1_4:   rc=0
buttons=0x0000
```

该结果证明物理 XInput 来源存在；没有人为按键，不能升级为语义 runtime PASS。

### 2.3 当前实例隐藏状态

BUS 读取 PID `18532` 的时间窗发现，实例随后进入 `window-hidden`。在该状态下，现有 HC/owner 安全闸门要求：

```cpp
IsWindowVisible(g_hwnd) && !IsIconic(g_hwnd) && focusMainWindowIsForeground()
```

因此 A/B/LB/RB/方向等 YMCC semantic 会被 native 丢弃；隐藏状态下只有全局 LB+RB summon 理论上不依赖该 page gate。该事实不能被解释为 XInput 不存在。

## 3. Git v0.0.28 对账

基线：tag `v0.0.28` / commit `0274412`。

- `src/gamepad/engine.ts`、`src/bridge/yeman.ts`、`src/bridge/ipc.ts` 与该基线的公共手柄链路一致；
- native 原有 `gamepadUiInputEligible()` 也要求可见、非最小化、YMCC 前台；不把该 HC/owner gate 改成失焦可用；
- 当前工作树相对 v0.0.28 的手柄相关差异主要是 50 ms XInput fallback、multi-slot snapshot、ROG fallback、shortcut recording 与 Multi-axis Raw Input 订阅；没有证据表明这些差异本身改变了普通 XInput 语义映射。

因此“窗口在测试时已隐藏”是已证实的阻断条件；“按钮没有进入 gamepadEval”在旧日志上仍是未观测，不得猜测。

## 4. 本轮源码修正

### 4.1 补齐 native → renderer 的连接状态镜像

文件：`src/gamepad/engine.ts`

原缺口：

```ts
function hasConnectedPad(): boolean {
  return false;
}
```

当前 native 已发布 `gamepad.state`，但 renderer 没有把该连接状态用于启动/恢复 semantic RAF loop。已改为：

- 维护 `nativePadConnected`；
- 监听 `ipc:gamepad.state` 的 `connected`；
- 连接时按当前可见/呼出状态启动 loop；
- 断开时停止 loop；
- 不读取 `navigator.getGamepads()` 作为第二物理采集源。

这修正的是 renderer readiness/recovery 链路，不创建虚拟手柄、不改变 HidHide、不放宽失焦 gate。

### 4.2 补齐 native 证据 receipt

文件：`native/main.cpp`

已增加低频诊断事件：

```text
gamepad-xinput-state
gamepad-ui-input-emitted
gamepad-ui-input-dropped
gamepad-summon-emitted
```

`gamepad-ui-input-dropped` 会记录 `windowVisible/windowIconic/foreground/customOwner`，用于区分窗口 gate、child owner 和真实语义发出问题。该日志不改变准入、owner 或输出行为。

## 5. 构建、部署与新证据

通过：

- `pnpm run type-check`；
- `native/build_native.bat`；
- `pnpm run build`；
- `deploy-installed.ps1` → `C:\SOFT\YeMan\YeManCC`；
- 前端资源与 native/recovery service hash 对账。

新实例 PID `57372`（2026-09-06 19:56:24）已产生：

```json
{"event":"gamepad-input-registration","rawInputRegistered":true,"fallbackTimerArmed":true,"rawInputError":0}
{"event":"gamepad-xinput-state","connected":false,"connectedMask":0,"source":"none"}
{"event":"gamepad-xinput-state","connected":true,"connectedMask":1,"primarySlot":0,"source":"xinput","buttonMask":0}
```

这闭合了：

```text
native fallback timer → gamepadEval → XInput slot 0 readback
```

尚未闭合：

```text
真实按键 → gamepad-ui-input-emitted 或 gamepad-summon-emitted
→ renderer action receipt
```

## 6. 当前未知与用户介入点

仍需一次实体回归，才能区分最后一层：

1. 保持本机新实例窗口可见并置前；虚拟手柄、陀螺仪保持关闭。
2. 按一次 A，再按一次 B；观察是否有 `gamepad-ui-input-emitted`。
3. 按一次 LB 或 RB；观察是否有 `gamepad-ui-input-emitted(action=page-prev/page-next)`。
4. 从桌面按住 LB+RB 约 0.5 秒；观察是否有 `gamepad-summon-emitted`。
5. 回传新的 `C:\Users\DaVe\AppData\Local\YeManCC\native-lifecycle.log` 尾部即可继续闭口。

如果出现 `gamepad-ui-input-dropped`，按 receipt 的 reason 修复；如果只有 `gamepad-xinput-state` 而无任何 semantic receipt，则继续查 Raw Input/timer 到 `gamepadEval` 的按键边沿，而不改 owner gate。

## 7. 安全边界

- 默认虚拟手柄仍关闭：`persona=disabled`、`gyroEnabled=false`、`frontendSummonVirtualControl=false`。
- HC `HIDuncloakondisconnect=false` 仍保持；没有新增断开自动恢复。
- 未使用 HidHide 作为 XInput 阻断。
- 正式 Release/updater 未修改。
- 本轮未将 `input-capture.worker-exception(type must be string, but is null)` 猜测为 T0 根因；该问题继续标记为独立 `CAPTURE-LANE-UNKNOWN`。
