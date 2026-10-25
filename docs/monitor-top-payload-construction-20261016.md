# 硬件快照直接构造优化（2026-10-16）

本轮优化唯一原生发布函数 `nativeMonitorTopPayloadText`。硬件数据仍由 YMCC 既有唯一采集器提供，不改采样频率、不新增数据源/线程/计时器；HWiNFO 已生效的上游策略与 Presented FPS 优先关系保留。

## 产品变更

原函数以 JSON initializer-list 创建 19 个初始字段，生成临时的键值 pair 数组。GPU 功耗、GPU 频率、电池功率和剩余时间四个初值随后必定被最终有效性逻辑覆盖。

新函数创建最终对象后逐个填字段，上述四项只在原最终有效性逻辑处创建。API 保留 `remain` 参数兼容现有调用者；输出仍由原 `RemainingTime` 证明决定。最终 JSON 类型、协议字段、空值、顺序、数字精度与传感器选择保持原样。CPU 四舍五入、GPU 最终原始精度、电池与剩余时间同源规则全部保留。没有新增共享可变 JSON 缓存。

## 输出与分配证据

`tools/monitor_top_final_object_selftest.mjs` 编译优化前完整生产发布函数、当前完整函数和当前 NativeMonitorHw。历史生产函数冻结于 `tools/fixtures/monitor_top_payload_before_20261016.inc`；按统一行尾并忽略提取边界尾空白与修改前捕获函数核对相同。

4482 项断言、2539 次完整 canonical 字节对比通过，其中含 992 份由 G3E 两份、Win5、ROG 历史快照重建的发布输入。重建用于覆盖已出现过的机器数据形态，不能等同于重新采集旧机硬件或逐字复现旧版历史输出。无历史输入的新输出目录另通过 2498 项断言、1547 次完整字节对比。

覆盖健康/失效硬件、0–128 个核心、无效数组、缺失核心证明、独立 CPU 证明、无电池/AC/DC、非法电量、真实与估算功率、剩余时间、风扇期望转速、Global 锁帧上限、Windows 文件读写和非 UTF-8 显卡名称。

每组 300 次完整发布，统计仅包围被测函数，不计报告与比较操作：

| 模式 | 优化前分配次数 | 优化后分配次数 | 优化前请求分配字节 | 优化后请求分配字节 |
| --- | ---: | ---: | ---: | ---: |
| 普通硬件，32 核心槽位 | 57600 | 34800 | 5408100 | 4724100 |
| 硬件失效、Windows CPU 独立 | 56700 | 33900 | 4664400 | 3980400 |
| 电池估算与剩余时间 | 58200 | 35400 | 5427300 | 4743300 |
| AMD 部分 D3D 显存、128 槽位 | 58500 | 35700 | 7078500 | 6394500 |

每次发布少 76 次被测分配、少 2280 个请求分配字节。普通模式分配次数减少约 39.6%；不表示 YMCC 进程或整机 CPU 降幅。本轮未做 CPU 时间/百分比压测。

既有检查另通过 447 项原生 CPU 采集/发布检查、11 项统一消费/有效期检查、19 个完整解析/内存/网络/Windows 文件系统差分场景。

## 编译、本机小更新和实测

完整原生宿主基于 249 个冻结输入编译链接成功。冻结发布函数分别绑定被测函数与当前主线；HWiNFO 源/安装助手也核实一致。本轮仅更新 `C:/SOFT/YeMan/YeManCC/YeManCC.exe`，SHA-256 为 `ab1c3f81181afbff536218f7862b069a148c93cc4d02513f4d88bb5eb9ef422e`，原程序与统一设置已备份。

已运行的 Cyberpunk 2077 下检查 41 条新版本统一快照：每条都有 67 个预期字段，硬件健康，选中显卡始终为 RTX 5080。内存/显存用量、可用、总量与占比符合既有容差；无电池本机没有生成有效电池功率或剩余时间。风扇和锁帧证明规则正常。32 个逻辑处理器槽位完整，首条因无差分基线未知，后续 40 条 CPU 有效。FPS 包中 37 条来源为 `hwinfo-presented`、2 条为既有 RTSS 补位，其余不产生帧率包。

HWiNFO PID 全程保持 28552，INI 哈希未变，助手未重新部署；没有关闭游戏或重启 Steam。测试后主配置与备份配置的原详细日志门已恢复关闭。最终正常 YMCC PID 为 44040。

上游 NvmlSupport、oneAPI、AsusWMI、FlushBuffers 等候选仍缺少关闭后不损失所需数据的充分证据，维持现状。没有为减少读数而新增未验证的上游关闭项。真实 12 小时共享期限到期、其他机器当前运行情况不由本轮短时本机发布验证代替。

## 证据与回退

证据目录：`G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\top-payload-construction-20261016`。

- `FINAL-RESULT.json` / `TOP-FINAL-OBJECT-RESULT.json`：源绑定、完整输出与分配统计。
- `ARCHIVE-PINS.json` / `ARCHIVE-TOP-PUBLICATIONS.json`：历史输入与原包哈希。
- `clean-selftest/`：无历史输入依赖的独立运行。
- `consumer/` / `memory/`：现有解析、发布及消费回归。
- `LIVE-PUBLISHER-DATA-RESULT.json` / `LIVE-CPU-DATA-RESULT.json`：安装版实际数据。
- `SOURCE-MANIFEST.json` / `NATIVE-BUILD.log` / `DEPLOYMENT-MANIFEST.json`：完整构建与安装。
- `ROLLBACK-MANIFEST.json` / `rollback/`：更新前程序与设置。
