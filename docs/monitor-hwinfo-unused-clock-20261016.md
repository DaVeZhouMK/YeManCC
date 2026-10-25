# HWiNFO 未消费的 NVIDIA 实测时钟分支：源头优化与数据等价检查

日期：2026-10-16。承接 `docs/monitor-hwinfo-static-acquisition-20261016.md` 的最终状态；该报告描述的是上一轮，当前配置以本报告和源码为准。

## 最终行为

公开界面不提供 AMD SnapshotPolling 开关，也不默认开启该采集模式。保留现有数据是约束；技术选项不交由玩家判断。原有可选内部静态策略仍只在配置准备/复核时读取，全 YMCC 继续共用唯一数据层，没有新增采集器、刷新定时器或配置轮询。

`PowerControl/YeManHWiNFO.ps1` 在内嵌稳定基础模板上增加 `NvClockMeasuring=0`，并继续强制 `PresentMon=1`。已存在且健康的 HWiNFO 启动路径仍不改配置、不重启；只有原有配置准备/恢复事务才合并该设置。原有备份、复制后校验、回滚、提权恢复和共享内存时间戳推进校验保持原流程。

关闭目标为 NVIDIA 的三个额外项目：`GPU Clock (measured)`、`GPU Memory Clock (measured)`、`GPU Crossbar Clock (measured)`。YMCC 普通 GPU 频率选择及三个 RTSS 模板读取的是常规时钟名称。没有以关闭普通 GPU 功耗、温度、频率或 HWiNFO FPS 获得节能。RTSS FPS 仍仅在 HWiNFO FPS 缺失、无效或过期时救援；上一轮的来源优先级文件未修改。

## 上游分支证据与边界

本机安装 EXE 与前一轮下载的官方版本字节相同，当前研究副本及其解压来源均有哈希固定。仅在隔离指令模拟器中执行额外时钟分支的原始指令；GPU API、等待和内存初始化边界均由桩替代，没有执行研究 EXE、调用真实显卡接口或改写安装程序。

静态选项表将 `NvClockMeasuring` 映射到该开关变量，默认值为 1。实际比较指令在零值时直接跳至公共继续位置。开启且 API 正常的用例包含 3 次计数器初始化、3 次计数器读取和 1 次等待辅助调用；零值在该分支内均为 0。5 组指令用例覆盖正常、API 缺失、初始化失败、部分初始化失败、读取失败。模拟器都到达同一公共继续位置，普通时钟字段未在所测分支内改变。

这些是局部分支操作计数，不是整个 HWiNFO 的 CPU 百分比。等待辅助参数为 30；不能据此声称等待 30 毫秒。未实测安装版本采用新 INI 后是否执行同一路径，未证明其他 HWiNFO 版本支持相同选项，也未证明所有机器的数据完整性。

## 数据消费验证

从当前 `native/main.cpp` 提取实际结构、解析器和发布器，并使用真实的解析复用缓存及粘性 GPU 选择逻辑。9 组用例只删除明确属于 NVIDIA 分组的三个额外实测时钟行；对比所有发布字段、FPS/LOW/帧时间的原生值及来源标识。

覆盖 RTX 5080、NVIDIA 与 AMD 同时存在、普通频率有效零值、普通频率缺失、NaN、普通频率别名、仅有额外时钟、AMD 不变、归档原始 32 行数据。归档中确有 NVIDIA `GPU Memory Clock (measured)` 读数，但它是部分传感器记录，不代表完整运行中的机器。合成用例另断言普通频率 2475 MHz、功率 82 W、温度 65、负载 95%、电压 1.05 V、FPS 180、LOW 101 及真实帧时间样本等已知输入，防止用“前后都为空”冒充数据保留。

63 项原生检查通过。三个 RTSS 模板分别解析到 45、99、28 个 `ReadingName`，均包含普通 `GPU Clock`，无上述额外实测时钟依赖。21 项消费检查使用实际唯一 hub、HUD model 及两个现有输出协议，对比全部输出数据，验证原始数值、缺失读数和原有过期边界。两个协议的隔离检查不代表重新开放用户已经隐藏的 Steam 输出选项。

## 配置回归与交付

当前源码的隔离检查：指令分支 5、实际解析/发布 63、实际消费链 21、配置策略 31、稳定模板/就绪 44、合并 26、周期确认 48、恢复 29、BAT 入口 41，共 308 项；另完成 3 个 RTSS 模板依赖检查。旧缺陷基线来自各轮留存的原始源码，不以当前源码伪造对照。此轮没有 UI 类型修改，因此未重复前一轮的 TypeScript 全量检查。

本机两个安装 INI 只读哈希仍为 `48cdaf01460262e51a7a01020c72d8e070762af615911790ab2c1fbec6fb40d8`，每个 567 字节。没有部署、重启 HWiNFO/YMCC/游戏、新增真实传感器采样或测 CPU 百分比。当前变更可供源码集成；实际上游是否采用设置仍需后续原有安装验证。

未写入 `native/main.cpp`，未覆盖其他任务修改，未暂存或提交主线。交付补丁仅包含本轮 PowerShell 改动、相应测试和本报告，并在隔离目录检查补丁应用及应用后的逐字节一致性。

证据目录：`G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-unused-clock-20261016/`。主要记录：`EMULATED-BRANCH-RESULT.json`、`DATA-EQUIVALENCE-RESULT.json`、`CONSUMER-RESULT.json`、`TEMPLATE-DEPENDENCIES.json`、`regressions/`、`LOCAL-INI-UNCHANGED.json`、`PATCH-CHECK.json`、`ROUND-RESULT.json`。

实际生产者接受配置、跨机器完整性以及完整的传感器组裁剪还未闭合，持续优化目标保持 active。
