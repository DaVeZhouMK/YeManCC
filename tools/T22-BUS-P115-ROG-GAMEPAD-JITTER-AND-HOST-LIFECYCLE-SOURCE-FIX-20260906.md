# T22 BUS-P115：ROG 实体输入抖动与 Host 生命周期 source fix

日期：2026-09-06  
状态：`SOURCE-FIXED / BUILD-VERIFIED / DISABLED-LANE-EMPIRICALLY-SMOKED / STANDALONE-LANE-ONLY / RUNTIME-BLOCKED`

## 1. 触发证据与结论分层

用户回传的 `YeManCC.zip` 已归档为：

```text
Mainline/Build/Validation/UserEvidence-20260906-183040/
```

证据事实：

- `input-capture.jsonl` 中 769 个实体样本包含 `buttons=16/64/176/224/240`、`lt/rt=255`、`lx/ly/rx/ry` 在 `-32768/0/32767` 间跳变；这与用户报告的 RT/LT、Start 和双摇杆上下抖动一致。
- 同一批首帧记录 `motionEnabled=false`、`motionAdmission=safe-zero`、`pairProven=false`；因此这些跳变不是已准入的 gyro→stick contribution 证据。
- 旧 `native-lifecycle.log` 中存在 768 次 `SUBMIT_FRAME/frame-accepted`，即 motion 关闭时仍把实体 `XINPUT_GAMEPAD` 送进虚拟 DS4 Host。
- 本机只读进程核对（2026-09-06）发现 13 个无父进程的旧 `C:\SOFT\YeMan\YeManCC\InputHost\YeManInputHost.exe`，另有一个旧 YMCC 子进程；它们来自多次 HELLO/启动失败后的残留。

已执行的授权范围内系统清理：关闭旧 YMCC/自测进程及上述精确路径的孤儿 Host；随后 PnP 只剩真实 Xbox 360、HidHide/虚拟总线本身，没有 PS4 虚拟手柄。未触碰 Steam、游戏、Xbox 物理设备或正式 Release 文件。

“这些残留 Host 足以造成多虚拟目标叠加和用户所见乱动”是基于进程/PnP 事实的因果推断，已标为 `INFERENCE`；最终 ROG consumer 归因仍需新的 A/B runtime receipt。

## 2. 明确源码偏差与修正

### P115-F01：motion 关闭时仍发布实体 pad

旧路径在 `inputCaptureOnGamepad()` 中无条件调用 `inputHostSubmitPad()`；`motionEnabled=false` 只把 gyro contribution 置零，却继续提交 buttons、triggers 和 physical sticks。这样即使用户只想用 YMCC 自身的实体 XInput，测试 lane 仍会创建/维持第二个 DS4 writer。

已修正：

```text
lane marker present && motionEnabled=false
    → 不启动 Host
    → 不调用 SUBMIT_FRAME
    → Coordinator 有 Host 时执行 QUIESCE → NEUTRAL → RELEASE_TARGET → SHUTDOWN
    → YMCC UI 继续读取 canonical XInput

motionEnabled=true
    → capture/Coordinator 重新 HELLO → PREPARE → NEUTRAL
    → 只有后续 admission/first-frame 条件满足时才发布
```

这与 HC 的“未准入/监听阶段不调用 VirtualManager.UpdateInputs()”方向一致；Host stop/restart 是 YMCC 为独立 HIDMaestro 进程增加的本地生命周期实现，不能宣称为 HC 原生行为。

### P115-F02：ROG HID 未证明时覆盖有效 XInput

旧 `gamepadReadState()` 在 `g_rog.suppressed` 时可以用最近的 `lastHidPad` 覆盖 live XInput，即使两者不是同一帧、同一消费者或同一 physical identity。

已修正：

- 有效 XInput 存在时始终保留 XInput；
- 只有 XInput 不存在且 HID report 在 150 ms fallback 窗口内时，才使用 `rog-hid-fallback`；
- `gamepad.state` 和 capture sample 写入 `inputSource=xinput|rog-hid-fallback|none`；
- 不扩大 ROG PID、descriptor 或硬编码 axis 解析的准入范围。

这属于输入源隔离安全修正，不把 P-HID 推导成 P-XINPUT/P-OWNER，也不声称已经闭合 ROG 的 external consumer isolation。

### P115-F03：HELLO 失败的 Host 启动孤儿

旧 `inputHostStopLocked()` 在“未 HELLO、未准备 target”的 child 启动失败时等待超时但不终止，实际留下多个孤儿 `YeManInputHost.exe`。

已修正：仅当同时满足以下条件时，超时后执行有界 `TerminateProcess` 并记录 `input-host-startup-orphan-cleanup`：

```text
hadProcess=true && hostHello=false && preparedTarget=false
```

已建立 target/session 的故障仍保持 `release-unproven`，不走强制终止；因此该修正不改变已建立 HIDMaestro 会话的 release 证据边界。

### P115-F04：provider key 解析防止部分 GUID

`inputCaptureSensorProviderKey()` 现在明确在 `#{sensor-class-guid}` 前截断，目标形式为：

```text
ACPI#BOSC0200#1
```

不会再把 `#{09` / `#{C2` 之类的部分 GUID写入 pair 对账。当前本机台式机无 WinRT gyro/accel，故 provider 修正尚未得到 ROG 实机的新 runtime receipt。

## 3. 验证证据

源码与包：

```text
native/build_native.bat                         BUILD_OK
YeManCC.exe bytes                                2,255,360
YeManCC.exe SHA-256                             8BB8407CE6EF5BDBD46C8AC92278BEE883EEFFE08B7F9B21EDA64BC709377766
ZIP                                             Mainline/Build/TestPackages/GyroInput-ROG-RealStick-20260906-R111/YeManCC-GyroInput-ROG-Test.zip
ZIP bytes                                       88,604,212
ZIP SHA-256                                     8221CD9E079573299A86DDBB92741165ECA890DB376AC450245AC112A23C72E6
HC runtime                                      HC-CANDIDATE-0.32.4.0-06c0b954-20260902
formal Release/updater touched                  false
```

静态/脚本验证：

```text
gyro virtual feature selftest                   PASS
ROG input adapter selftest                       PASS
input lifecycle mock selftest                   PASS
input owner runtime integration                  PASS
E-37 HC parity selftest                          PASS
Capture-ROG-Input-Gap-Evidence.ps1               Windows PowerShell 5s -SkipImu PASS
```

禁用 lane 实机 smoke（本机台式机 + 外接 Xbox 360，设置 `YEMAN_POWER_CONTROL_DIR` 指向 R111 Stage）：

```text
YeManCC 运行 8 秒                                1 个
R111 Stage YeManInputHost                       0 个
退出后 Stage 进程残留                           0 个
native receipt                                   input-host-publication-suppressed
```

该 smoke 只证明“禁用时不启动/不发布 Host”；不证明 ROG gyro active、DS4 report、Steam/game consumer 或恢复事务。

## 4. 未知、缺口与必须用户介入

仍未知/未闭合：

- ROG 实机在 `motionEnabled=false` 时 YMCC 物理控制是否稳定；
- ROG RC73XA/RC73YA 的同代 gyro/accel provider key、cal-lock、matrix active 和 first-frame receipt；
- DS4 HID descriptor/report/raw readback、电量字节及 Steam/game consumer；
- HidHide/P-XINPUT/P-OWNER 三平面隔离与恢复；
- 旧孤儿 Host 是否曾留下需要重启才能消失的设备节点（当前 PnP 快照未见 PS4，但不等于历史 consumer receipt）。

下一次只需要用户在 ROG 上做同一完整程序的 A/B：

```text
A：陀螺仪关闭，YMCC 保持打开 10 秒；确认 RT/LT、Start/Pause、左右摇杆不乱动，且只存在预期设备。
B：在同一进程内手动开启陀螺仪，选择现有 outputStick；确认只有选定摇杆响应，记录 inputSource、provider、pair、calibration、firstFrame、Host/DS4/Steam 观察。
```

回传新的 `YeManCC-ROG-Input-Gap-Evidence-*.zip` 后，BUS 再做 runtime 对账。当前仍保持：

```text
T22 = P115-SOURCE-FIXED / RUNTIME-ROG-AB-REQUIRED / RUNTIME-BLOCKED
T15/T16/T17/T18 = 未因本轮 smoke 自动闭口
正式 Release/updater = 未改动
```
