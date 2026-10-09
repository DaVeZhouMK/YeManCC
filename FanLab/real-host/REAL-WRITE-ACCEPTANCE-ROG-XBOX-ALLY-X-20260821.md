# YeManFanHost ROG Xbox Ally X 真机写入验收（2026-08-21）

## 结论

技术写入链路通过。目标设备为 ROG Xbox Ally X，HC 返回 `XboxROGAllyX`，使用 `ProfileCurve` 路由；本次真实 Host 完成了授权、打开、曲线写入、OEM 恢复、lease 释放和关闭。

## 机器与包

- Manufacturer：`ASUSTEK COMPUTER INC.`
- SystemModel：`ROG Xbox Ally X RC73XA_RC73XA`
- Product：`RC73XA`
- HC factory：`HandheldCompanion.Devices.XboxROGAllyX`
- Fan route：`ProfileCurve`
- HC mapped route count：`70`
- 测试包：`YeManFanHost-real-framework-dependent-20260821-final-v3.zip`
- 测试包 SHA-256：`8E5E7E0D457518985E5D05CFC706A1F81F0CCA62417E958F4D677FE2C46C1A4C`
- 用户反馈文件 SHA-256：`0EAF86E5265A4777B9AFF56A8A2A9EA698C08831C735C5E0A673E7A21EE53F20`

## 实际步骤

| 步骤 | 结果 |
|---|---|
| Handshake / device Gate | 通过；`supported=true`、`fanRouteWriteReady=true` |
| HC Open | 通过 |
| HC OpenEvents | 通过 |
| Fan lease | 通过 |
| 四节点曲线写入 | 通过；`hardwareWritesEnabled=true`、`hardwareWritesObserved=true` |
| 5 秒读回状态 | 通过；Host 状态保持 `Ready` |
| OEM restore | 只有 `oemRestoreConfirmed=true` 且 `oemPhysicalOwnershipConfirmed=true`、`unknownState=false` 才通过；仅 `hcRestoreCallbackReturned=true` 仍为未确认 |
| Release lease | 通过 |
| Close | 通过；最终 `Stopped`、`unknownState=false` |

本次脚本使用曲线：`0°C → 0%`、`40°C → 20%`、`60°C → 45%`、`100°C → 90%`。

## 边界

- `hardwareWritesObserved=true` 证明 HC 写入调用已发生；反馈 JSON 没有独立的真实 RPM/噪声传感器读数，因此实际风扇转速变化仍需操作者确认。
- 本次未执行睡眠/唤醒、长时间第三方冲突和主程序正式启用。
- 主程序 `FAN_REAL_HOST_ENABLED` 仍保持关闭；没有安装或修改 `C:\SOFT\YeMan\PowerControl`。
