# T22-BUS-P69：当前 source 与历史 IMU 工件的 stale narrative 对账

Status: `CURRENT-SOURCE-CORRECTED / HISTORICAL-ARTIFACTS-PRESERVED / DOCS-ONLY / NO-PRODUCT-MATH-FIX / RUNTIME-BLOCKED`

本页处理的是文档与证据工件的 current-source 归属错误，不是产品运行时修复。没有修改历史 JSON、锁定 HC、HIDMaestro、正式 Release、升级器、系统或真实设备。

## 1. current RF00 source 事实

当前 RF00 manifest 绑定的关键文件为：

```text
manifestId   = T22-RF00-20260905-T10-I-HC-ROUTE-STATIC-SNAPSHOT
sourceDigest = CF09B233709F493F5A7EE836CC47F6C2D02D898B8007F8B70486C818309B9E31
InputHost/Program.cs sha256 = F017F534A75424227297C8D0B618E7EC72E9A40985D79C42AFC2EE8CD1DAB59C
native/main.cpp sha256       = 0246970E4EFFFA2F1DD0C8583987E25A7E758A3538ABE96A9015DD6E672828D3
```

`InputHost/Program.cs:471-479` 的 `TrySubmitFrame()` 仅构造 `HMGamepadState` 的 `Buttons`、`Hat`、`Axes`；`Program.cs:500-506` 的 neutral 路径同样只构造按钮、帽键和标准轴。当前文件中没有 `GyroDpsX/Y/Z`、`GyroPitch/Yaw/Roll` 或 `AccelX/Y/Z` 的赋值。

Native `inputHostSubmitPad()` 仍在 `native/main.cpp:6421-6428` 将受 `pairProven && g_gmLocked` 控制的 `cgx/cgy/cgz` 作为诊断/Host 参数传递，但当前 Host 不把这些参数写入 `HMGamepadState`。因此这是 direct DS4 IMU 的 fail-closed/no-report 状态，不是已实现的 GyroDps sink。

## 2. 被识别的 stale current-source narrative

以下工件保留其原始内容和原始 hash，不回写历史证据；它们只能按各自 capture identity 阅读，不能覆盖当前 RF00 source：

| 工件 | stale 叙述 | 当前归属 |
| --- | --- | --- |
| `T22-BUS-P12-E02-HIDMAESTRO-EMBEDDED-PROFILE-STATIC-EVIDENCE-20260905.json` | `Program.cs sets HMGamepadState.GyroDpsX/Y/Z only` | 历史 source scope；当前不成立 |
| `T22-BUS-P12-E03-HIDMAESTRO-DS4-ENCODER-STATIC-EVIDENCE-20260905.json` | `Program.cs:127-136 assigns ... GyroDpsX/Y/Z` | 历史 source scope；行号与当前文件不对应 |
| `T22-BUS-P07-current-source-line-map-20260904.md` | `Program.cs:132-136` 无条件写 GyroDps | 旧 snapshot；不作为 current claim |
| `T22-RF00-current-source-line-map-20260904.md` | `Program.cs:122-136` 为 GyroDps sink | 旧 RF00；已被后续 current RF00 supersede |
| `T22-RF00-SIDEBAR-P15-C10-C11-C12-C13-current-source-line-map-20260905.md` | `Program.cs:122-149` 写 GyroDps | 旧 sidebar scope；不覆盖当前 source |
| `T22-BUS-P64-HC-UNKNOWN-GAP-RECONCILIATION-20260905.md` | Host 当前只写 GyroDps | 需按本页 current-source 修正阅读 |
| `PersonaDescriptorParity.v1.json` / `MainlineClaimReconciliation.v1.json` | 仍引用旧 GyroDps assignment | provenance/claim wording stale；不等于当前 runtime |

P20、P24 和 TASK 的错误登记段已经指出这类 stale 工件；本页将 current fact 固定为“当前 Host 不赋任何 IMU 字段”。

## 3. 三方裁决

```text
锁定 HC：DualShock4Target 使用 paired raw gyro + raw accel，并按 HC target 规则转换
HIDMaestro：DS4 extended encoder 消费六个 raw int16 字段；GyroDps float 不是该 DS4 codec sink
当前 YMCC：native 保留诊断 gx/gy/gz，但 InputHost 不赋 GyroDps 或六个 int16
当前 direct DS4 IMU：SAFE-STOP / NO-REPORT / transport-source-and-readback-unverified
```

这项修正不改变 `PS4 battery 5% origin = UNKNOWN`。stale GyroDps 叙述不能解释当前 Steam 电量提示，也不授权补写 battery 或 motion 字段。

## 4. 未知与介入门

```text
当前 HMGamepadState 默认值如何被 HIDMaestro 最终编码       = UNPROVEN
当前虚拟 DS4 report byte 14 的真实值                       = RUNTIME-BLOCKED
当前 Host/driver/OS/Steam/game consumer 回读                 = RUNTIME-BLOCKED
ROG DMI→matrix receipt、same-provider pair、calibration     = UNENCLOSED
```

需要用户提供实际 Host 生命周期的 DS4 descriptor、input/feature raw report、instance/container identity、独立 decoder/readback 与 Steam 对应显示，才能继续处理 5% 问题。

## 5. 结论

```text
docs-only stale narrative correction = RECORDED
current product source math fix      = NONE
new HC axis/sign/unit/gain drift      = NOT FOUND
runtimeUpgrade                       = false
GYRO VIRTUAL MAINLINE STATUS         = RUNTIME_BLOCKED
```

