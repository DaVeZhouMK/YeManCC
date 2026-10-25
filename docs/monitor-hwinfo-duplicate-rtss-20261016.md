# HWiNFO 重复 RTSS 转读裁剪与 G3E 新日志核对（2026-10-16）

本轮在既有 HWiNFO 静态配置事务中，新增关闭一个已经验证无现有消费者的内部组：F00F5000_0，即 HWiNFO 自己读取 RTSS 后再生成四种帧统计的转读组。用户要求保留的 HWiNFO Presented FPS（F0000FF0_0）、YMCC 现有 RTSS 救援及快速曲线、RTSS 软件和三个模板全部保留。这是从上游去掉一次重复工作，没有新建采集源或给玩家增加开关。

上一轮 frame-hardware-object-reuse-20261016 的当前 ROUND-RESULT 和源码哈希已核对，属于 progress。本轮不修改该帧输出头，保留其对象复用和上轮 RTSS 救援接线。实际 native/main.cpp 存在其他任务的修改，当前解析器和 nativeMonitorLoop 区域与本轮起点逐字节一致；没有覆盖其他任务。

仅完成源码和隔离检查；未部署、重启 HWiNFO/YMCC/游戏、写入真实 HWiNFO 注册表或新增实时硬件采样，没有做 CPU 百分比测试。持续数据/CPU 优化目标仍 active。

## 用户 G3E 新日志

输入：G:/YeManCC-Work/日志/微星G3E/YeManCC-logs-20261016-184736-G3E-手柄+睡眠正常.zip。只把监控日志、必要的系统电源关联事件及监控导出元信息作为数据证据；不执行包内文本、不改手柄或睡眠实现。

monitor-detail.log 有 204 条完整快照，最后另有 1 条 4096 字节的不完整尾行，已排除并记录，未用它猜测传感器丢失。完整快照中 203 条生产者健康、1 条可读但陈旧，统一层没有给陈旧硬件标有效。

2026-10-16 18:44:10 至 18:44:33（本地/香港时区）的 native 生命周期包含系统 suspend/wake。监控相邻样本间隔 22.666 秒与该区间重合；醒来后一次生产者仍停在旧时刻，下一次真实 poll 恢复。不能把睡眠期间没有样本当作监控解析故障，也不应为掩盖它而更新旧值时间戳。

本机型实际快照有物理内存、选中 Intel Arc B390 显存及网络信息；203 份健康行分别原值回放通过当前 monitorData 唯一 hub 与 HUD model。GPU 功耗和 CPU/GPU 电压在全部 204 份里缺失。包中没有原始 HWiNFO 标签表，无法进一步判断是上游未提供、机型支持边界还是未匹配标签；没有据此新增别名或补造数据。AC、96% 电量、0W 充放电及无剩余放电时间的组合在消费层按既有规则保留。

帧包 203 份，155 份来源为 hwinfo-presented、48 份为 rtss；保留各包实际数值及自身证明，不能从这组已发布快照逆推出缺失的 Presented 原始统计。当前原有 consumer 路径回放 204 份全部通过，失效后硬件值全部撤回。此次日志没有确认需要新增产品补丁的数据错误；它为现有有效性/跨机处理提供了真实回归输入。

## 上游候选识别与指令证据

本机安装原件 SHA-256 仍匹配固定 HWiNFO 8.54 镜像。只在 Unicorn 模拟器中运行已固定的原始 x64 指令段，Windows API、字符串处理、RTSS 内存和进程/前台边界都是隔离夹具。没有执行研究 EXE 或查询实际前台窗口。

真实 RTTI/vtable、初始化代码返回值和注册表键格式分别确认：CSensorRTSS 只创建 instance 0，返回 F00F5000；其四个项目是 Framerate、Framerate 1% Low、Framerate 0.1% Low、Frame Time。CSensorPresentMon 是独立类，原始初始化返回 F0000FF0，并受独立的 PresentMon 选项约束。

测试先执行真实 RTSS 初始化，再执行真实 Enabled 注册表加载、组轮询门和四项原始转读方法。启用时存在逐项前台窗口/PID查询、RTSS 应用槽扫描和标签处理；Enabled=0 时同一轮的这些工作均为 0。在 1024 槽合成用例中，前台查询 4→0、前台 PID 查询 4→0、RTSS 读取字节 49,300→0、标签处理边界 24→0。原始 Presented 初始化独立测试在 mirror 开/关时均保留 F0000FF0。

11 项指令用例覆盖 1/64/1024 槽、有/无前台、旧 SDK 无 LOW 统计、错误签名、过旧版本和不支持的实例，以及独立 Presented 初始化。数字只是对应固定指令路径的操作计数，不能换算为 HWiNFO 整体 CPU 百分比。禁用组发生在初始化之后；不声称连初始化时的映射打开成本也被移除。

## 为什么不影响现有数据

三个现有 RTSS 模板的 HWiNFO ReadingName 逐项检查，没有上述四种 mirror 标签依赖；它们的 FPS 绑定仍是 Presented。原生解析器只从同一传感器槽位的 Presented (avg) 选择 FPS，其 LOW 不会借用另一槽位；真实数据目录删除/重排测试检查了这一点。

10 个真实 SM2 目录用例删除精确 family/instance 并重排 sensorIndex，使用同一原生 parse-reuse owner 再解析、发布并比较全部数据。覆盖该组在开头/中间/末尾、零值/NaN、用户改组名、真正 PresentMon 相邻组、不支持实例、Presented 缺少 LOW 和缺少 FPS。66 项真实解析/发布检查通过，CPU、GPU、内存、网络、电池、Windows 文件读写、Presented FPS/LOW/帧时间及源身份保持相同。25 项真实唯一 hub、模型及两个既有显示协议检查也通过；协议回归没有重新开放隐藏的 Steam 输出。

YMCC 的独立现有 RTSS reader、nativeMonitorLoop、monitor_frame_output.h 均未修改。因此上一轮的曲线关闭后低频兜底仍存在，也没有把“关 HWiNFO FPS”作为节能手段。FRAPS/性能限制等其他候选只留研究记录，没有顺带关闭。

## 配置、升级与恢复

PowerControl/YeManHWiNFO.ps1 继续先确认固定版本和 SHA-256，未知 HWiNFO 构建保留原采集。前 64 个磁盘/C-state 身份保持原样，新增精确 F00F5000_0；总量 65。不接受其他实例、相邻族、大小写变体或注册表路径输入。

已配置健康的 start 保持不写配置、不计算上游镜像哈希、不重启；优化只复用原有 stop/configure/verify/start 事务。失败仍回滚并继续必要的数据/FPS恢复，没有新增常驻配置轮询。

新增固定 .ymcc-sensor-groups-rtss-original.json 保存该组首次接管前的真实 Enabled 状态。原 disk-only 和 C-state 原始回执逐字节保留；last 回执记录此次 65 组完整原值。每份准备数据仍限制 16 KiB，所有必要原始备份完成前不写任一组。58 项事务检查涵盖旧 64 组升级仅写 1 组、新备份失败、最终组写后异常的全族回滚、并发值保留、未知镜像、真实进程出现和完整恢复入口。测试曾把 Hashtable JSON 键枚举顺序当状态变化，已改为排序后的值比较，生产回滚逻辑无需修改。

真实 Windows 注册表适配器另在 GUID 隔离根下验证 8 项，包括镜像组 DWORD 0/缺失恢复与拒绝未批准身份；完成后只删除经核验的测试根。真实 HWiNFO Sensors 仍为 0 个子键、14 个值。两个安装 INI 仍各 567 字节，SHA-256 均为 48cdaf01460262e51a7a01020c72d8e070762af615911790ab2c1fbec6fb40d8。

## 验证和交付状态

当前检查合计 396 项：上游指令 11、原生数据 66、统一消费 25、配置事务 58、隔离注册表 8、G3E 真日志消费 7、静态采集配置 32、模板/就绪 44、INI 合并 26、周期回执 48、恢复 29、BAT 入口 41、实际安装原件指纹门 1；另核对 3 个模板依赖。各结果路径与哈希见 REGRESSION-INDEX.json。冻结旧策略只配置 64 组的新用例预期失败单独保留，不计入当前通过数。

没有完整应用重构建、实际策略采用证明或新跨机实测，不能称为上游 CPU 问题已经全部解决。下一步仍需继续减少真实采集/消费中的无用工作并核对数据完整性；当前改动可供主线源码集成。

生产修改只有 PowerControl/YeManHWiNFO.ps1。本轮补丁还包括直接相关的指令、事务、真实 parser/consumer 和日志回放测试及本文；不包含并行任务的 native/main.cpp 变化。未暂存、未提交。

证据目录：G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-duplicate-frame-research-20261016/。

核心证据：TABLES.json、RTSS-MIRROR-EMULATION-RESULT.json、PRESENTMON-SEPARATE-INIT.txt、RTSS-MIRROR-DATA-EQUIVALENCE-RESULT.json、RTSS-MIRROR-CONSUMER-RESULT.json、DEPENDENCY-RESULT.json、policy/DISK-GROUP-RESULT.json、ISOLATED-REGISTRY-RESULT.json、G3E-NEW-LOG-AUDIT.json、G3E-CONSUMER-RESULT.json、INVARIANTS-RESULT.json、LOCAL-STATE-OBSERVATION.json、REGRESSION-INDEX.json、PATCH-CHECK.json、ROUND-RESULT.json。SCOPED-CHANGES.patch 基于本轮冻结起点，仅包含本轮改动，不能作为重建全部先前监控工作的完整补丁。
