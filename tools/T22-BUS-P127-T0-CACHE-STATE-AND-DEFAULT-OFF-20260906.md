# T22 BUS-P127：清缓存后持久化输入状态残留与默认关闭回归

日期：2026-09-06（本机日志实际记录到 `2026-09-07 00:08`，按原始日志保留）  
状态：`LOCAL-STATE-RECOVERED / DEFAULT-OFF-RECEIPT / T0-BUTTON-RECHECK-REQUIRED`

## 1. 本轮结论

本机“清除缓存后仍失败”不是当前 HC XInput 路由失效，也不是需要回退 Git
v0.0.28 的证据。当前真正发现的是：WebView2 缓存清理不会清理
`PowerControl\yeman-settings.json`，旧的 `outputTarget` 请求仍被 standalone
marker lane 读取，导致启动时自动创建 DS4 Host。

这与用户要求的“默认 HidHide/断开自动恢复/虚拟手柄关闭”不一致，但属于本机
持久化状态残留，不是已证实的 HC 源码公式偏移。本轮没有回退 v0.0.28，也没有
新增 HidHide 或 OEM writer。

## 2. 证据

### 2.1 失败前的实际状态

文件：`C:\SOFT\YeMan\PowerControl\yeman-settings.json`

```text
input.revision                         = 42
input.applyStatus                      = saved-pending
input.pendingConfigHash                = sha256:05d38f...
input.outputTarget.persona             = dualshock4
input.outputTarget.buttonMappingEnabled= true
input.outputTarget.gyroEnabled         = false
input.gyroMotion.enabled               = false
input.ownership.frontendSummonVirtualControl = false
```

同一实例 PID `32648` 的 native receipt 证明它实际启动了 Host：

```text
HELLO             = accepted
PREPARE_TARGET    = target-prepared
SUBMIT_NEUTRAL    = neutral-accepted
SUBMIT_FRAME      = frame-accepted（连续帧）
```

随后设置被刷新为关闭态，native 记录：

```text
input-host-stop-request(reason=virtual-target-disabled)
input-host-release-receipt(disposition=release-unproven)
input-host-stop-result(localReleaseAck=false)
```

`release-unproven` 不能伪装成 Host-local release 完整证明；但后续 PnP 只读观察
没有发现 `Wireless Controller`，因此当前 OS 枚举已恢复为实体 X360 单一目标。

### 2.2 本轮恢复后的状态

已先保留原文件备份：

```text
C:\SOFT\YeMan\PowerControl\yeman-settings.json.bus-backup-20260906-001000
```

当前配置已恢复为：

```text
input.applyStatus                      = unknown
input.pendingConfigHash                = null
input.outputTarget.persona             = disabled
input.outputTarget.buttonMappingEnabled= false
input.outputTarget.gyroEnabled         = false
input.gyroMotion.enabled               = false
input.gyroMotion.outputMode            = disabled
input.ownership.frontendSummonVirtualControl = false
```

恢复后自动重启的 YMCC 实例 PID `17332` 记录：

```text
gamepad-input-registration(rawInputRegistered=true,
  fallbackTimerArmed=true, rawInputError=0)
gamepad-neutral-admission(status=admitted-neutral,
  connectedMask=1, primarySlot=0, source=xinput)
input-host-publication-suppressed(
  reason=virtual-target-disabled-at-start,
  virtualTargetEnabled=false, virtualTargetPersona=disabled)
```

当前 PnP 只读结果包含：

```text
XUSB/VID_045E&PID_028E = 实体 Xbox 360
Wireless Controller    = 未发现
```

因此本机现在没有由 YMCC 创建的 PS4/DS4 虚拟目标，且 XInput 呼出入口已经
具备启动回执。

## 3. HC 与 Git v0.0.28 对照

- HC `VirtualManager.defaultHIDmode=NoController`、`HIDstatus=Disconnected`，
  默认不创建/连接虚拟目标；本轮恢复后的 YMCC 状态与此一致。
- P126 已完成的 current source 修正仍然有效：virtual target lifecycle 与
  motion gate 分离；`gyroEnabled=false` 不应阻止一个**明确请求**的 DS4 target
  进入 Host-local 流程。不能为了本机默认关闭而撤销这项 HC parity 修正。
- Git `v0.0.28` 只能作为历史参照；回退会丢失 P116/P123/P124/P125 的
  XInput fallback、ButtonState/AxisState 拆分和 HC ordinal route 修正。
- 本轮没有发现必须由源码修复的新增 HC 偏移；问题发生在 durable settings
  状态与缓存生命周期之间。

## 4. WebView2 独立问题

清缓存后 PID `13088` 曾出现两次浏览器进程退出，退出码
`-1073741819 (0xC0000005)`。现有恢复机制完成 profile isolation 并切换到
software GPU；随后新实例完成 navigation/render-ready。该问题会让用户感觉
“手柄无法切换”，但不能把它推断为 XInput 或虚拟手柄根因；继续单独登记为
WebView2 runtime/environment gap。

## 5. 未闭口项与下一次介入

已闭合：

- 默认虚拟目标关闭；
- 本机没有 DS4 PnP 残留；
- XInput registration/fallback/neutral admission 启动回执存在。

仍未闭合：

- 本轮没有代替用户按实体 `LB+RB`，所以不能把新的 PID `17332` 标记为
  `gamepad-summon-emitted` runtime PASS；
- 没有进行 Steam/game consumer、P-HID/P-XINPUT/P-OWNER 隔离证明；
- `release-unproven` 仍保留为 Host-local 事务缺口，PnP 消失只作为独立外部观察；
- ROG gyro/pair/calibration/DS4 raw readback 不受本轮影响。

最小用户回归动作：保持虚拟手柄和陀螺仪关闭，在 YMCC 已启动时按住实体
`LB+RB` 约 0.5 秒；若仍不呼出，回传同一实例的
`C:\Users\DaVe\AppData\Local\YeManCC\native-lifecycle.log` 中从
`gamepad-input-registration` 到按键时刻的记录。

