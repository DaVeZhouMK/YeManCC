# T22 BUS-P118：XInput 按键状态与 YMCC 语义闭口复核

日期：2026-09-06  
状态：`INSTRUMENTED / BUILD-DEPLOY-VERIFIED / CONNECTION-ONLY / USER-PRESS-REQUIRED`

## 1. 本轮边界

本轮只验证：

```text
实体手柄
  → XInput 状态变化
  → native gamepadEval/gamepadProcessUiInput
  → native owner/focus gate
  → gamepad.ui-input 或 gamepad.summon
```

不放宽 `gamepadUiInputEligible()` 的可见、非最小化、前台和 child-owner gate；不创建虚拟手柄；不启用 HidHide；不把中性 readback 写成 runtime PASS。

## 2. 已执行命令与产物

工作目录：`G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC`

```text
cmd /c native\build_native.bat
→ BUILD_OK G:\YeManCC-Work\Mainline\Build\App\Native\YeManCC.exe

powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\deploy-installed.ps1 -WorkspaceRoot 'G:\YeManCC-Work\Mainline'
→ DEPLOY_OK
→ backup: C:\SOFT\YeMan\YeManCC\.deployment-backup-20260906-200438
```

构建与部署文件一致：

```text
bytes  = 2,262,528
SHA256 = B58AB5CC5AC5805F2E8738353E2A6961E223A90EF66A1245339769A84A8FADB1
```

本轮诊断字段 `gamepad-xinput-input` 只在 XInput snapshot 实际变化时记录：`buttonMask`、LT/RT、四轴、connectedMask、source。

## 3. 已证实证据

日志：`C:\Users\DaVe\AppData\Local\YeManCC\native-lifecycle.log`

最新实例 PID `10604`（20:04:54）启动 receipt：

```json
{"event":"gamepad-input-registration","rawInputRegistered":true,"fallbackTimerArmed":true,"rawInputError":0}
{"event":"gamepad-xinput-state","connected":true,"connectedMask":1,"primarySlot":0,"source":"xinput","buttonMask":0}
{"event":"gamepad-xinput-input","connected":true,"connectedMask":1,"primarySlot":0,"source":"xinput","buttonMask":0,"leftTrigger":0,"rightTrigger":0,"leftX":0,"leftY":0,"rightX":0,"rightY":0}
```

因此已闭合：

- Raw Input 注册成功；
- 50ms XInput fallback timer 已启动；
- XInput slot 0 被 native 识别为连接；
- native 持续得到中性 XInput snapshot。

当前没有任何以下 receipt：

```text
gamepad-xinput-input(buttonMask != 0 或 trigger/axis != 0)
gamepad-ui-input-emitted
gamepad-ui-input-dropped
gamepad-summon-emitted
```

## 4. 源码链路对账

BUS 复核当前链路为：

```text
RegisterRawInputDevices
→ SetTimer(GP_STATE_FALLBACK_TIMER_ID, 50ms)
→ WM_TIMER
→ gamepadEval()
→ gamepadReadState()
→ gamepadProcessUiInput()
→ gamepadEmitUiAction()
→ ipc_emit("gamepad.ui-input")
→ renderer dispatchNativeUiAction()
```

关键 gate：

- `gamepadUiInputEligible()` 仍要求主窗口存在、可见、非最小化、YMCC 前台；child owner 存在时遵循 child owner；
- `g_inputReleaseRequired` 初值为 false，只有恢复/child-returning 事务进入；中性快照会清除；
- 前端 `hasConnectedPad()` 已改为 native `ipc:gamepad.state` 连接镜像，不读取 `navigator.getGamepads()`，不产生第二个物理采集源；
- 与 v0.0.28/HC 对账没有发现可直接认定为新增偏移的 owner 或隐藏 gate。

## 5. 外部重启现象（单独记录，不归因）

PID `10604` 在 20:07:52 正常执行 `coordinator-stop → window-destroy`；随后有新 PID `68592`，再后有 PID `33788`。`recovery-service.log` 只记录 `target-exited`，没有 `unresponsive-confirmed`，所以不能把这几次退出归因于 recovery service 误杀。该外部启动/退出来源仍未知，需后续独立审计。

## 6. 证据、推断、未知与缺口

### 已证实

- native 输入注册、fallback timer、slot 0 连接读取均成立；
- 当前采集窗口内只有中性状态，尚无实体按键进入 native 的证据；
- 未出现 semantic emitted/dropped receipt。

### 不能推出的结论

- 不能从中性 snapshot 推出实体按钮一定没有进入 XInput；
- 不能从没有 semantic receipt 推出前端 owner/焦点 gate 是根因；
- 不能把独立 `input-capture.worker-exception (type must be string, but is null)` 归因到 YMCC 手柄语义链。

### 当前唯一闭口缺口

必须在 YMCC 窗口可见且置前时完成一次真实按键序列，并保留同一 PID 的以下对照：

```text
A 按下 → buttonMask=4096 → A 松开 → buttonMask=0 → gamepad-ui-input-emitted(action=confirm)
B 按下 → gamepad-ui-input-emitted(action=back)
LB/RB → page-prev/page-next
LB+RB → gamepad-summon-emitted
```

若只出现 `gamepad-xinput-input` 非中性而没有 emitted/dropped，才继续定位 `gamepadEval`/edge state；若出现 dropped，则按 receipt 的 `reason` 定位 owner/focus；若仍只有中性 snapshot，则问题停在物理/XInput 状态观察层，禁止猜测前端修复。

## 7. 介入请求

请在当前实际 YMCC 窗口中依次按一次 `A`、`B`、`LB`、`RB`，最后按一次 `LB+RB`，每次完全松开后再按下一键；不要开启陀螺仪。完成后保留程序窗口，回复“已按”。本轮之前不再修改 owner/focus/release 逻辑。

