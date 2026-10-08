# T22 BUS-P116：T0 本机手柄呼出 YMCC 与默认关闭状态修正

日期：2026-09-06  
状态：`T0-SOURCE-FIXED / BUILD-VERIFIED / DEFAULTS-HC-ALIGNED / RUNTIME-HARDWARE-RECHECK-REQUIRED`

## 1. 用户问题与范围

用户报告：本机 YMCC 状态下，实体手柄不能稳定切换/呼出 YMCC；同时确认默认“脱落/断开时自动恢复可见”与虚拟手柄都应关闭。

本轮只处理：

- native XInput/Raw Input 的 YMCC 呼出入口；
- HC 默认关闭语义的 source 对账；
- 不创建虚拟手柄、不启用 HidHide、不改正式 Release/updater。

“脱落提示”按上下文解释为 HC 的 `HIDuncloakondisconnect`（物理手柄断开时自动 Unhide）。如果用户指的是另一种 UI toast，当前仍是 `UNKNOWN`，本报告不把两者混同。

## 2. 证据与判定

### 2.1 已证实

1. 锁定 HC 源码 `Properties/Settings.settings` 与 `App.config` 的 `HIDuncloakondisconnect` 默认值为 `False`；`ControllerManager` 只有在该值为 true 时才在物理设备移除路径调用 `controller.Unhide(false)`。因此默认不做断开自动恢复可见。
2. 当前本机 `C:\SOFT\YeMan\PowerControl\yeman-settings.json` 为关闭态：

```text
input.gyroMotion.enabled              = false
input.gyroMotion.outputMode           = disabled
input.outputTarget.persona            = disabled
input.outputTarget.gyroEnabled        = false
input.ownership.frontendSummonVirtualControl = false
input.outputTarget.visibilityPolicy   = hidden
```

3. 当前 YMCC native 的旧 `gamepadRegisterRawInput()` 只在 `RegisterRawInputDevices()` 成功时启动 50ms XInput 兜底计时器。若 Raw Input 注册失败，XInput 仍可用，但 `gamepadEval()` 没有任何持续入口，LB+RB 呼出就会失效。这是源码可直接证明的 T0 缺口。
4. 本机只读 XInput 探针在 2026-09-06 读取到 slot 0 已连接；本轮没有人为按键/摇杆动作，因此结果为 `OBSERVATION_REQUIRED`，不能冒充“呼出运行时已通过”。证据：`Mainline/Build/Validation/HC-Parity/R1-xinput-physical-observation-20260903.json`。

### 2.2 不能升级为事实的内容

- 旧安装实例在本轮修正前是否实际遇到了 `RegisterRawInputDevices()` 失败，没有旧二进制的 registration receipt；因此“这是用户当次失败的唯一 runtime 根因”仍是 `UNPROVEN`。
- 用户所说的“脱落提示”若不是 HC `HIDuncloakondisconnect`，其具体 UI 生产者尚未定位。
- XInput/游戏消费者隔离仍遵循 `29-ATOMIC-OWNER-AND-GAME-CONSUMER-ISOLATION-20260903.md`，本轮没有声称系统级隔离闭合。

## 3. 源码修正

文件：`native/main.cpp`，`gamepadRegisterRawInput()`。

旧逻辑：

```text
RegisterRawInputDevices()
  └─ success → arm 50 ms fallback timer
      failure → no timer, no XInput polling entry
```

新逻辑：

```text
RegisterRawInputDevices()
  ├─ success → Raw Input + 50 ms XInput fallback
  └─ failure → 50 ms XInput fallback remains armed
```

`gamepadEval()` 仍以 `g_prevW/g_curW` 做边沿与一次性 hold 判定，因此 Raw Input 与 timer 同时存在时不会重复发出语义动作。新增 native lifecycle receipt：

```text
gamepad-input-registration
  rawInputRegistered
  fallbackTimerArmed
  rawInputError
```

这项修正只保证实体 XInput 仍有 YMCC 呼出/导航入口；没有接入 virtual gamepad、HIDMaestro、HidHide 或 OEM Disable sink。

## 4. 默认关闭对齐

### HC

```text
HIDuncloakondisconnect = False
VirtualManager.defaultHIDmode = NoController
VirtualManager.HIDstatus = Disconnected
```

### YMCC

```text
virtual target persona = disabled
gyro outputMode        = disabled
gyroEnabled            = false
frontendSummonVirtualControl = false
visibilityPolicy       = hidden
```

YMCC 当前没有 active 的 `uncloak-on-disconnect` caller；现有 HidHide legacy scan/restore 函数未被正常输入呼出路径调用。本轮不新增一个无消费者的“脱落提示”开关，避免把未知 UI 语义伪装成 HC 行为。

## 5. 验证

```text
native/build_native.bat                         BUILD_OK
YeManCC.exe (Mainline/Build/App/Native)         2,256,896 bytes
SHA-256                                          2AE10CEEE341E09E0DDF2A06A95C0023E3E7DD9CB04DFDF9F98F3C44A37A90CD
vue-tsc --noEmit                                PASS
input owner runtime selftest                    PASS
input owner runtime integration selftest        PASS
physical input ownership selftest                PASS
ROG Xbox-face lifecycle static selftest          PASS
gyro virtual feature selftest                    PASS
formal Release/updater touched                  false
```

本地测试实例已部署：

```text
target     = C:\SOFT\YeMan\YeManCC\YeManCC.exe
backup     = C:\SOFT\YeMan\YeManCC\.t0-backup-20260906-192806.exe
target SHA = 2AE10CEEE341E09E0DDF2A06A95C0023E3E7DD9CB04DFDF9F98F3C44A37A90CD
```

新实例启动后已产生 native receipt（`C:\Users\DaVe\AppData\Local\YeManCC\native-lifecycle.log`）：

```json
{"event":"gamepad-input-registration","rawInputRegistered":true,"fallbackTimerArmed":true,"rawInputError":0}
```

这闭合了“当前本机 Raw Input 注册成功且 XInput fallback timer 已 arm”的启动事实；尚未闭合“人为按 LB+RB 后实际收到 summon 事件”。

同一启动实例的 `C:\Users\DaVe\AppData\Local\YeManCC\input-capture.jsonl` 还出现一条既有 capture-lane 记录：`input-capture.worker-exception / type must be string, but is null / safeStop=true`。本次没有 `YeManInputHost` 进程，也没有 DS4/PS4 PnP 节点；该异常暂记为 `CAPTURE-LANE-UNKNOWN`，不把它推断为 T0 呼出故障，也不在本轮擅自改动。

## 6. 剩余缺口与用户介入点

当前只剩一次低风险实体回归即可确认 T0 runtime：

1. 使用该 build/测试包启动 YMCC，保持虚拟手柄和陀螺仪关闭；
2. 在前台游戏或桌面按住 LB+RB 约 0.5 秒；
3. 确认 YMCC 呼出，松开后按 LB/RB 不产生组合误切页；
4. 如仍失败，回传包含 `gamepad-input-registration` 的 `native-lifecycle.log`，即可区分 Raw Input 注册失败、XInput slot 变化或窗口焦点问题。

在用户回传前，T0 的 source fix 已完成，但不能把未按键的本机只读探针升级成 runtime PASS。虚拟手柄与断开自动恢复仍保持默认关闭；正式 Release/updater 未改动。
