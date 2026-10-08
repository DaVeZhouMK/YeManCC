# T22-BUS-P70：静态回归、发布边界与 PS4 电量问题复核

日期：2026-09-05  
范围：锁定 HC `0.32.4.0 / 06c0b9544db2b1f39abf9cd3796225ccfb096103`、当前 YMCC source、`TASK.md` 三方静态对账。  
运行边界：本轮未启动真实 Host、HIDMaestro、HidHide、Steam、游戏、虚拟设备或真实硬件。

## 1. Fact：RF00 / T20 / T21 / T14

在本页写回前，`PersonaDescriptorParity.v1.json` 修正后执行了一次：

```text
T22-RF00 -> T20 -> T21 -> T14
manifestId    = T22-RF00-20260905-T10-I-HC-ROUTE-STATIC-SNAPSHOT
sourceDigest  = 3EE1B13F666D30F87AC169E048146549E564D8AFBA2CDA0F414982EAEEA539DA
keyFiles      = 74
porcelain raw = 456 entries
HC clean      = true
runtimeOperation = false
buildOrTest      = false
```

RF00 manifest 的 74 个 key file 均以当前绝对路径重新计算 SHA-256 和 bytes，逐项匹配数为 `74/74`；manifest 与 re-audit report 是 provenance 的唯一事实来源，`TASK.md` 不复制 digest。

T20 保持 `4/13/4` disposition，T21 保持 9 条 REC，T14 仍严格为 `DGF-01…DGF-15`，不创建 DGF-16。`runtimeUpgrade=false`。

## 2. Fact：静态回归结果

以下命令均以 exit code `0` 完成：

- `type-check`
- motion sample、gyro E-37/HC parity、provider capability matrix、special-controller protocols
- input lifecycle、owner runtime/integration/resident、physical ownership、input contracts/runtime admission/route evidence、Host supervisor
- gyro mapper/config activation/calibration session/virtual feature/sidecar
- persona descriptor/report、ROG gyro/input adapter/protocol/visibility topology/Xbox face lifecycle、ROG/HidHide contract
- calibration safety delta、evidence quality gate、HC parity ledger、gyro virtual mainline status

这些是 static/mock/self-test 证据，不提升为 HID/OS/Steam/game consumer runtime 闭口。

## 3. Fact：发布边界审计

`test:a1-gyro-release-boundary` 的脚本调用 exit code 为 `0`，但审计结果状态为 `FAIL`：

```text
releaseForbiddenEntries = PowerControl/redist/HidHide_1.5.230_x64.exe
installedExternal      = C:\SOFT\YeMan\PowerControl\redist\HidHide_1.5.230_x64.exe
```

这表示正式 Release 仍含位于锁定 HC runtime 目录之外的 HidHide 安装包；它是当前发布边界策略冲突，不是 HC 轴/符号/单位/倍率或漂移算法偏差。本轮只修正 `gyro_virtual_release_boundary_audit.ps1` 的结论文字，使 `status=FAIL` 不再错误落到“没有 forbidden assets”；未删除或移动正式包内容。

## 4. Fact / Gap：PS4 低电量 5%

锁定 HC `DualShock4Target.BuildReport()` 仍未找到 battery assignment；`DS4OutDevice.bBatteryLvl` 没有已证明的生产写入链；DSU `DsBattery` 是网络 metadata，不是 HIDMaestro DS4 report；当前 YMCC InputHost 只提交 Buttons/Hat/标准 Axes，不写 Battery 字段。

因此：

```text
PS4 battery 5% origin = UNKNOWN / RUNTIME-BLOCKED
```

当前禁止猜写 `0x05`、`0x0A`、`100%` 或 `Full`。要关闭缺口，必须在同一 Host 生命周期取得 DS4 descriptor、input/feature raw report、report ID/length、raw bytes、instance/container identity、独立 decoder/readback，并与 Steam 显示关联。

## 5. Fact / Gap：HC 偏差与原创逻辑

```text
new HC axis/sign/unit/gain/deadzone/velocity-decay drift = NOT FOUND
original drift algorithm                                  = NOT FOUND
ROG DMI -> native matrix receipt                         = UNENCLOSED
same-provider pair / calibration / age-skew               = UNENCLOSED
GamepadMotion / Host / HID / Steam / game consumer        = RUNTIME-BLOCKED
```

`pairProven=false`、safe-zero、未绑定身份时保持 raw、direct IMU no-report 均是 fail-closed 安全门，不归类为原创漂移逻辑，也不证明 runtime 已闭合。

## 6. 需要介入的唯一剩余门

本机静态证据已到边界。后续只有两类外部证据能推进：

1. ROG：同一次 Host 生命周期的 provider/epoch/pair、HC matrix receipt、descriptor/encoded report、independent readback、Steam/game consumer 观察；
2. PS4：同一次 Host 生命周期的 DS4 descriptor、raw input/feature report 与独立 battery decoder/readback。

在这些证据回来前保持 `runtimeUpgrade=false`、`RUNTIME-BLOCKED`，不打开 `pairProven`，不补套 ROG matrix，不修改 battery 字段。

## 7. Final provenance after TASK write-back

Because `TASK.md` is an RF00 key file, the final write-back was followed by another freeze and re-audit:

```text
manifestId    = T22-RF00-20260905-T10-I-HC-ROUTE-STATIC-SNAPSHOT
sourceDigest  = 3E57F2D11CE6B18EAC7F8EE0922084C063983D0EC965958DC5EC7A5EF5EAAD04
keyFiles      = 74
porcelain raw = 457 entries
HC clean      = true
runtimeOperation = false
buildOrTest      = false
T20 = 4/13/4
T21 = 9 claims
DGF = DGF-01…DGF-15
runtimeUpgrade = false
```

The 74-key manifest was checked against current files with `74/74` SHA-256/byte matches. This block records the P70 write-back checkpoint; the later P71 TASK write-back supersedes its dynamic digest/porcelain values. It does not change any runtime or PS4 battery conclusion.

## 8. Later final RF00 after P71 write-back

```text
manifestId    = T22-RF00-20260905-T10-I-HC-ROUTE-STATIC-SNAPSHOT
sourceDigest  = 174ED4EAF5076BB8493B208F28B419E6DC7B0F461674F665CB157C5C03C501D4
keyFiles      = 74
porcelain raw = 458 entries
HC clean      = true
runtimeOperation = false
buildOrTest      = false
T20 = 4/13/4
T21 = 9 claims
DGF = DGF-01…DGF-15
runtimeUpgrade = false
```

This is the current provenance checkpoint after the P71 entry was added to `TASK.md`.
