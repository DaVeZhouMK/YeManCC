# T22-BUS-P80：正式发布包与 ROG 完整测试包边界对账

日期：2026-09-05  
状态：`PACKAGE-BOUNDARY-FACT / NOT-HC-MOTION-DRIFT / TEST-PACKAGE-AVAILABLE / RUNTIME-BLOCKED`

本页记录 BUS 对正式 `YeManCC.zip`、ROG standalone 测试包和当前 `tools/package-release.ps1` 的只读三方对账。目的不是改变发布策略，而是防止把“正式升级包”和“可运行的陀螺仪/虚拟手柄测试包”混称。

## 1. 事实证据

### 1.1 正式 Release ZIP

```text
path = G:\YeManCC-Work\Mainline\Release\Packages\YeManCC.zip
bytes = 42,316,800
observed entry count = 359
```

条目扫描结果：

```text
YeManCC/InputHost/**                           = 0
HIDMaestro.Core.dll                            = 0
WinRT.Runtime.dll                              = 1
PowerControl/redist/HidHide_1.5.230_x64.exe    = 1
PowerControl/feature-assets/gyro-motion/**     = 6
PowerControl/feature-assets/virtual-gamepad/** = 3
```

正式包确实携带锁定 HC runtime 和 feature-assets，但不携带可创建虚拟控制器的 `YeManCC/InputHost` runtime。

### 1.2 ROG standalone 测试 ZIP

```text
path = G:\YeManCC-Work\Mainline\Build\TestPackages\GyroInput-ROG-20260905-230826\YeManCC-GyroInput-ROG-Test.zip
bytes = 85,227,247
sha256 = 572AE8841AB606D3122EC7F78D190CFE7BE917E64E4951ADFBAF05AE583239D0
observed entry count = 360
```

条目扫描结果：

```text
YeManCC/InputHost/**                           = 7
YeManCC/InputHost/HIDMaestro.Core.dll         = 1
WinRT.Runtime.dll                              = 2
PowerControl/redist/HidHide_1.5.230_x64.exe    = 1
PowerControl/feature-assets/gyro-motion/**     = 6
PowerControl/feature-assets/virtual-gamepad/** = 3
```

包内 `package-summary.json` 进一步记录：

```text
formalReleasePackageUntouched = true
systemMutation                = false
hcRuntimeId                   = HC-CANDIDATE-0.32.4.0-06c0b954-20260902
```

### 1.3 当前发布脚本的明确规则

`tools/package-release.ps1` 当前源码事实：

```text
357-359: YeManCC/InputHost is intentionally outside this release lane.
         The build may still produce it for isolated input validation,
         but the mainline updater package must not publish or install it.
570-571: formal YeManCC.zip must contain zero YeManCC/InputHost entries.
```

因此正式 ZIP 缺少 InputHost 不是打包器偶然漏导出；它是当前发布边界的显式策略。ROG standalone 包是独立测试 envelope，不代表正式升级包的内容契约。

## 2. 三方裁决

```text
formal Release InputHost presence        = INTENTIONALLY EXCLUDED / PROVEN
ROG standalone InputHost presence         = PROVEN / TEST-ONLY ENVELOPE
HIDMaestro availability in formal ZIP    = ABSENT BY RELEASE SCOPE
HIDMaestro availability in ROG test ZIP  = PRESENT
HC motion math drift                     = NOT FOUND BY THIS PACKAGE SCAN
PS4 battery 5% source                    = UNKNOWN / RUNTIME-BLOCKED
runtime virtual-device closure            = NOT PROVEN BY FORMAL ZIP
```

这不是 HC 的轴线、符号、单位、倍率、死区、校准或漂移算法偏差；也不能用正式 ZIP 的缺少 InputHost 来证明产品 runtime 已闭合或已经失败。它只定义了两种包的职责：

```text
formal Release / updater = 主程序与升级边界，不发布 InputHost
ROG standalone test      = 实机验证用完整 YMCC + InputHost + HIDMaestro/WinRT/HidHide
```

## 3. 证据、推断、缺口分离

### FACT

- 正式 ZIP 实际没有 `YeManCC/InputHost` 和 `HIDMaestro.Core.dll`。
- ROG standalone ZIP 实际包含 InputHost、HIDMaestro、锁定 HC runtime、GyroMotion/VirtualGamepad 资源和 HidHide 安装包。
- 发布脚本显式禁止正式 ZIP 包含 InputHost，并在验证阶段强制计数为零。
- standalone 包构建摘要声明未触碰正式 Release、未改变系统状态。

### INFERENCE（未提升为 runtime 结论）

- 若把正式 ZIP 直接复制到 ROG 并期待创建虚拟手柄，InputHost 缺失会使该测试链无法由正式 ZIP 自身完成；应使用 standalone 测试包。
- standalone 包“包含依赖”不等于 Host ACK、HID raw report、Steam/game consumer 或恢复闭口已经成立。

### GAP

```text
formal Release -> test runtime handoff documentation = 需要明确入口/包名，避免用户误测
standalone InputHost -> real Host ACK/first-frame  = UNENCLOSED
HIDMaestro -> DS4 descriptor/report/readback      = UNENCLOSED
PS4 5% raw producer                               = UNKNOWN
ROG provider/epoch/matrix/calibration/consumer    = RUNTIME-BLOCKED
```

## 4. 处理边界

本轮不修改 `package-release.ps1`、正式 ZIP、updater、InputHost、HIDMaestro、HidHide 或系统状态；不把正式包缺失 InputHost 改成“HC 偏差”，也不擅自把 InputHost 塞回正式升级包。若后续用户要求改变正式发布策略，应另开发布边界授权，不与 GyroVirtual HC parity 混改。

下一顺序仍为：

```text
T22-RF00 → T20 → T21 → T14 → static regression
```

本页写回后，旧 RF00 manifest 不再覆盖当前全部 key file；必须重新冻结后才能产生新的 current-source 对账。`runtimeUpgrade=false` 保持不变。

## 5. 本页写回后的 provenance 与静态回归

在本页和 `TASK.md` 写回后执行：

```text
T22-RF00 → T20 → T21 → T14
manifestId    = T22-RF00-20260905-T10-I-HC-ROUTE-STATIC-SNAPSHOT
sourceDigest  = 9BAB33529A265D3A7AED642FCD2B5CFC76992516AC193073499E318D9D73EF2D
keyFiles      = 74
porcelain     = 466
HC clean      = true
runtimeOperation = false
buildOrTest      = false
T20           = 4/13/4
T21           = 9 claims
DGF           = DGF-01…DGF-15
runtimeUpgrade = false
```

本轮只读回归还确认：

```text
pnpm run type-check                  = PASS
pnpm run test:gyro-virtual-mainline-status = RUNTIME_BLOCKED
pnpm run test:a1-gyro-release-boundary     = FAIL (HidHide release-boundary policy)
```

`RUNTIME_BLOCKED` 和 A1 `FAIL` 均不改变本页的 package-scope 事实，也不构成 HC 轴线/符号/单位/倍率/死区/漂移偏差。A1 的失败项仍是正式包的 `PowerControl/redist/HidHide_1.5.230_x64.exe`，本轮未删除、移动或重写正式包。
