# T22 BUS-P98：正式 Release 的 HidHide 发布策略冲突（2026-09-06）

状态：`USER-RULING-RESOLVED / SOURCE-FIXED / RELEASE-REBUILT / A1-PASS`

## 1. 只读审计结果

执行：

```text
pnpm run test:a1-gyro-release-boundary
```

结果：`A1 GYRO VIRTUAL RELEASE BOUNDARY: FAIL`

证据：`G:\YeManCC-Work\Mainline\Build\Validation\HC-Parity\A1-gyro-virtual-release-boundary-20260903.json`

唯一 ZIP 违规条目：

```text
PowerControl/redist/HidHide_1.5.230_x64.exe
```

同一审计还记录该安装器存在于：

```text
C:\SOFT\YeMan\PowerControl\redist\HidHide_1.5.230_x64.exe
```

本次审计未执行系统修改；`systemMutation=false`。

## 2. 当前生成器的矛盾规则

`tools/package-release.ps1` 当前同时存在两组不可同时满足的规则：

```text
约第 357–359 行：YeManCC/InputHost 不进入正式 updater lane；主线 updater 不应发布测试组件。
约第 360–370 行：强制从锁定归档复制 HidHide 安装器到 Release/PowerControl/redist。
约第 568–573 行：YeManCC.zip 必须包含 gyro/virtual-gamepad 资产，并且必须包含 HidHide 安装器。
```

因此 A1 的 FAIL 是发布策略冲突，不是 HC 轴、单位、阈值、生命周期或电量语义偏差。测试包 `package-gyro-input-test.ps1` 单独携带锁定 HC/HIDMaestro/WinRT/HidHide 资产，不受本冲突影响。

## 3. 用户裁决与已执行修正

用户已明确裁决：正式包禁止出现 `PowerControl/redist/HidHide_1.5.230_x64.exe`。

已修改 `tools/package-release.ps1`：

- 移除正式 Release 复制 HidHide 安装器的逻辑；
- 移除正式 ZIP 必须包含 HidHide 的错误校验；
- 保留 standalone `package-gyro-input-test.ps1` 的锁定 HidHide 测试依赖；
- 正式包继续不发布 `YeManCC/InputHost`，测试组件与正式包分离。

随后已重建正式包并复跑 A1：

```text
pnpm run package
exitCode = 0
package result = PACKAGE_OK
YeManCC.zip SHA256 = 5C7D899B9EDC15D635C5FED1470A11904A4A87A73B505B1DC7909FCEC12CB442

pnpm run test:a1-gyro-release-boundary
exitCode = 0
A1 = PASS
```

新的正式 ZIP 不包含 HidHide 安装器；standalone ROG 测试包仍可按授权携带它。

## 4. 主线处置

```text
G-RELEASE-HIDHIDE-POLICY = RESOLVED / FORMAL-RELEASE-HIDHIDE-FORBIDDEN
A1 release boundary       = PASS
HC parity                 = no new HC source divergence
runtimeUpgrade            = false
```

该发布策略缺口已解决；它不改变 HC 运动语义，也不等于 T15–T18 runtime 已闭合。
