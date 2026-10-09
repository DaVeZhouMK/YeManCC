# Armoury Crate 常驻兼容测试包

本包允许 Armoury Crate 及其 OEM 服务保持运行，并把它们的状态写入测试证据。

HC 的 ROG 路线直接通过 AsusACPI 写入/恢复风扇曲线；Armoury Crate 不是 ACPI 调用的必需依赖，但它是 ASUS OEM 软件栈，可能在交回后重新应用固件曲线。因此本包不再把 Armoury Crate 当作竞争风扇工具阻止。

仍会阻止真正可能并发写入风扇/EC 的程序：Handheld Companion、G-Helper、FanControl、HWiNFO 控制、NBFC、RWEverything 等。

## 使用顺序

1. 保持 Armoury Crate 正常开启；不要手动停止 `ArmouryCrateSEService`、`AsusAppService`、`ArmouryCrateControlInterface`。
2. 先执行 `-Mode handshake`，确认设备为 `XboxROGAlly` 或 `XboxROGAllyX`。
3. 只有获得本轮授权后才执行 `-Mode write`。
4. 测试结束必须等待脚本输出 `oem-stack-after-close`。如果风扇仍然很大、Host 状态为 `unknownState=true` 或 OEM 恢复未确认，不要结束 Host，不要强制结束进程，把 `output\real-fan-test` 整个目录发回。

本包不会启动、关闭或修改 Armoury Crate，也不会修改 HC 源码。
