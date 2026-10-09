# T22-BUS-P58：ROG 完整 YMCC 测试包回执（2026-09-05）

## 包身份

```text
OutputRoot = G:\YeManCC-Work\Mainline\Build\TestPackages\GyroInput-ROG-20260905-230826
ZIP        = G:\YeManCC-Work\Mainline\Build\TestPackages\GyroInput-ROG-20260905-230826\YeManCC-GyroInput-ROG-Test.zip
bytes      = 85,227,247
SHA-256    = 572AE8841AB606D3122EC7F78D190CFE7BE917E64E4951ADFBAF05AE583239D0
```

## 内容与边界（Fact）

- 包含当前 Build 的完整 YeManCC 前端/Native/RecoveryService、InputHost、锁定 HC `0.32.4.0 / 06c0b9544db2b1f39abf9cd3796225ccfb096103`、HIDMaestro/WinRT 依赖、`gyro-motion`、`virtual-gamepad` 和锁定 HidHide 安装包。
- 包内 ZIP 共 360 个条目，顶层包括 `YeManCC`、`PowerControl`、启动脚本、README 与 `test-package-manifest.json`；无源码、任务 MD、PDB/OBJ/ILK/日志。
- `test-package-manifest.json` 记录 HC 91 files / mismatch 0、HIDMaestro/WinRT/HidHide hash、采集器自动启动和只读边界：不调用 HidHide、不发送 OEM Disable、不创建虚拟控制器、不修改 Steam/游戏。
- 构包期间 `formalUpdaterPackageTouched=false`、`systemMutationDuringBuild=false`、`runtimeOperationDuringBuild=false`；正式 `Release\Packages\YeManCC.zip` 未被写入。

## 证据边界

该包只提供下一轮 ROG 实机采集入口，不证明同 provider/generation pairing、HC matrix/calibration、DS4 descriptor/readback、InputHost first-frame、P-HID/P-XINPUT/P-OWNER 或 Steam/game `game-input-count=0`。运行后需回传桌面的 `YeManCC-ROG-IMU-Evidence-*.zip`，不能把包内静态 manifest 当作 runtime closure。

