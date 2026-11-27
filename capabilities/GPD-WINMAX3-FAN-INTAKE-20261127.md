# GPD WIN Max 3 风扇接入核查（2026-11-27）

归属：**YMCC 自行适配**。风扇和专用按键共用 `ymcc.device.GPDWinMax3` / `YeManCC.SourceDevices.GPDWinMax3`，由 `GPD / G1638-01` 精确识别。已接主线源码，物理转速、OEM 交还和按键帧待真机确认。

- 风扇消费：[固定来源目录](PinnedFanSourceCatalog.cs)，参照 GPDTool v1.67，自有风扇会话，复用现有传输与唯一睡眠协调层。关控制时清零已经发出调速命令的通道；独立记录写入回执和终端关闭，不用未经证明的 duty 回读挡住恢复，也不称已确认物理归还。
- 专用键消费：[GPD V2 输入绑定](HcOemInputCapabilityBinding.h)，首批只做 L4/R4；和风扇共用机型身份，输入与风扇分开验收。Win5 的其他能力不继承；不按 CPU、营销名称或通用外接 HID 放行。

## 现场与当前能力库

- 日志 ZIP SHA256 `3fea938459777d69ab45e9b6004314e90a50db3f55ca443ea6fb642c092cf2a5`；导出清单 6 行为 `0.0.43.0`，94/103/112 行为 Native `1a10befa…`、FanHost DLL `3122fb3d…`、HC `f79edd9dc…`（磁盘身份）。`native-lifecycle.log:5901` 为 `GPD / G1638-01 / STANDARD / Ryzen AI Max+ 388`、`resolved:false`；`model-adaptation-session.jsonl:2-3` 同样为未知机型、未绑定目录。
- `yeman-fan-host20261127.log:1-2` 在 01:12:51 报 `G1638-01 from GPD`，继而报告 `Device not yet supported`；11 月 26 日 14:26 的 Host 日志也有相同结果。Host 已启动；本次导出没有接管或写入证据，不能用它判断调速、关闭或唤醒的实际效果。
- 日志对应的旧包没有登记 `G1638-01`。固定 HC 机型表维持原样；这次新增的是 YMCC 自行适配的独立条目。[上游 HC ed6bc515](https://github.com/Valkirie/HandheldCompanion/blob/ed6bc5153f6f316f9545ec176e739e61e359244b/HandheldCompanion/Devices/IDevice.cs) 和 [Linux gpd-fan 25faec9c](https://github.com/torvalds/linux/blob/25faec9ce61c01fb45abf812a222c5b43c3cd5c5/drivers/hwmon/gpd-fan.c) 均可核对 Win5 `G1618-05`。共同厂商或 CPU 不建立风扇写入授权。

## 原厂对拍

来源：[GPDTool 官方 v1.67](https://www.gpd.hk/gpdtool/) 和 [原厂中文下载页](https://softwincn.com/gpdtool/)。离线解包后用 MSVC `dumpbin /disasm` 读取 x64 PE。ZIP SHA256 `7d87ada4e97b9f7ad8739d1f24d00b7d48061195e9bd15ab28e2a9b612a973b1`；服务映像 `GPDToolService.exe` SHA256 `6461ba3e3f3f23564cbbf92376e11f376808792afd1ee38806761f4108cf5bb3`。以下位置均为服务映像 RVA，ImageBase `0x140000000`。

| 环节 | 原厂 Max 3 | YMCC 当前 Win5 |
|---|---|---|
| 板号 | `0x80280` 为 `G1638-01`，`0x1b699` 查板号，`0x1b6b9`–`0x1b6c4` 返回对应记录；`0x1acd0`–`0x1ad22` 构造该记录 | 工厂只认 `G1618-05`；原厂将 Win5 放在另一记录（`0x801f0`、`0x1a946`） |
| 端口和转速 | `4E/4F`，`0x1b820`–`0x1b85b` 读取并合并 `0478/0479`；`0x1c380` 为选址写入叶 | HC `GPDWin5.cs:22-25,220` 使用相同的一组 RPM 寄存器；不能由此推定每路有独立 RPM |
| 手动调速 | `0x1c2b9`–`0x1c2eb` 对整数百分比按 `trunc(244 * percent / 100)` 量化，量化为 0 时改用 1；`0x1c311`–`0x1c33c` 依次写 `047B`、`047A` | 地址相同；HC `GPDWin5.cs:204-218` 的顺序、量化和 0% 行为不同，`Open` 还包含 Win5 专属 HID |
| 自动交还 | `0x1c1ef`–`0x1c219` 两路写 0；`0x1aed0` 析构分支也执行同语义 | `GPDWin5.cs:192-201` 的显式关闭同样两路清零。软件证据不等于物理交还 |

该型号记录的编号为 10，原厂还在 `0x1b0ad`–`0x1b0df` 选择该型号的预设映射（`0x7ff38`、`0x80058`），不是 YMCC 曲线默认值。[GPD 产品页](https://www.gpd.hk/gpdwinmax3/) 说明内置双风扇，可选双风扇扩展模块，同时列出 388 和 395 两款。当前只收到 388 的板号证据；上述两路命令不能推导扩展模块接入时的物理通道映射及交还效果。

## 接入边界

1. 通过能力总库登记精确 `GPD + G1638-01`。复用现有 SuperIO/PawnIO 传输、HW 温度、FanHost 会话和唯一睡眠协调层。`PinnedSourceFanSession` 当前按独立模式寄存器工作，不能把第二路 duty 当作模式位；双路零值自动协议需要自己的描述与交还逻辑。风扇就绪不依赖 Win5 HID。
2. 用现有 `fan.unified-capability-roundtrip` 和生命周期夹具覆盖两路写入顺序、0% 最小非零、100% 上限、OFF 双路清零、唤醒后重建，以及异机型、第二路拒绝和写入途中睡眠/退出。未完成的双路交还动作必须留在旧会话，不能被新会话覆盖；回读单组 RPM 不替代交还证明。
3. 首轮真机用同一 `G1638-01`，先不装扩展模块，记录 BIOS/EC 版本，观察启用、变速、关闭后回原厂自动、睡醒继续。扩展模块与 395 板号另行验收；旧安装包不含本次接线。
