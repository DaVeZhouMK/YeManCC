# T22-BUS-P88：HC parity 静态回归与 PS4 电量缺口再审计

日期：2026-09-06
状态：`STATIC-RECHECK / NO-NEW-DIRECT-FIX / RUNTIME-BLOCKED`

本轮继续以锁定 HC source、当前 YMCC source、HIDMaestro 静态 profile、TASK.md 和现有 ROG/PS4 证据为 authority。只执行静态读取、源码搜索、mock/selftest 和只读审计；没有启动 InputHost、HIDMaestro、HidHide、Steam、游戏、虚拟设备或真实设备，也没有修改系统状态。

## 1. 静态 parity 自检事实

以下检查均 exit 0 / PASS：

```text
gyro-motion-mapper-mock
gyro-config-activation
gyro-calibration-session (mock)
gyro-e37-hc-parity
gyro-virtual-feature
motion-sample
provider-capability-matrix
rog-gyro-protocol
rog-input-adapter
rog-visibility-topology
rog-hidhide-contract
input-contracts
input-lifecycle-mock
input-owner-runtime
input-runtime-admission
input-host-supervisor
input-route-evidence
input-capability-gate
persona-descriptor-parity
persona-report-mock
hc-parity-drift-ledger
```

这些结果证明 mock、静态合同、拒绝路径和 source topology 没有新增回归；E-37 明确声明 raw capture 不是 HC Default-plane proof，不能提升为运行时 IMU/DS4 证据。

## 2. HC 依赖与隔离只读审计

```text
HC parity ledger                 = PASS / 15 entries
HC manager graph                = BLOCKED / 13 managers / 4 direct-token managers
HC input dependency             = PASS / missing=0
HC input order                 = PASS / 4 traces
HC dependency location         = LOCKED_RUNTIME_PRESENT
HC source baseline             = OK / HC-BINARY-BATCH12-20260831 / 1048 files
HC candidate runtime integrity = PASS / 96/96
gyro virtual sidecar           = PASS
HidHide readonly               = LOCATED
third-party isolation          = LOCATED_UNVERIFIED
```

`HC manager graph = BLOCKED` 只表示锁定 HC 的 ManagerFactory 是宽管理器图，不能把 metadata/source inspection 当作 input-only runtime composition；它不是依赖缺失、不是 HC 源码错误，也不是新的 DGF。`third-party isolation = LOCATED_UNVERIFIED` 同样不能证明 reWASD/HC ViGEm/XInputPlus 的消费者隔离或恢复。

## 3. 三方 DS4/电量裁决

```text
HC DualShock4Target battery assignment      = NOT FOUND
current YMCC InputHost battery writer        = NOT FOUND
HIDMaestro 0→5 implicit conversion          = NOT FOUND
HC 054C:05C4 vs YMCC v2 054C:09CC parity     = UNENCLOSED
user PS4 low-battery 5%                      = UNKNOWN / RUNTIME-BLOCKED
new HC axis/sign/unit/gain/deadzone drift    = NOT FOUND
new original drift algorithm                 = NOT FOUND
```

不写 `BatteryLevel=5`、`0x05`、`0x0A`、`100` 或 `Full`；不把 HC 的 31-byte VIIPER `054C:05C4` 报告契约直接复制到 HIDMaestro 64-byte profile，也不把 v2 猜切为 v1。

## 4. 仍然未闭合的最小运行时门

必须由同一 Host epoch 的 ROG/测试机证据关闭：

1. 虚拟 DS4 的 VID/PID、container/instance identity；
2. descriptor 原始字节；
3. input/feature report 的 report ID、长度、raw hex；
4. battery byte 独立解码，并与 Windows/Steam 同 device identity 的电量读数对时；
5. 若继续验证陀螺仪，还需 provider identity、gyro/accel pair、epoch、calibration 与 matrix receipt。

在这些资料回来前，T11/T12/T13/T15-T18、P-HID/P-XINPUT/P-OWNER、Steam/game consumer、sleep/PnP/crash restore 和 PS4 5% 均保持 `RUNTIME-BLOCKED`；`runtimeUpgrade=false`。本轮无可按锁定 HC 无歧义直接修改的源码项。
