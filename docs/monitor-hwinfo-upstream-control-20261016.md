# HWiNFO 可控上游接入：配置事务与采样周期（2026-10-16）

用户追加要求：HWiNFO也是可控上游，CPU优化需要包含它，不能只做YMCC下游解析优化。本轮在现有PowerControl/YeManHWiNFO.ps1恢复入口内增加可回退的配置控制；仍使用原HWiNFO进程、原共享内存及YMCC唯一采集层。

此前cpu-unit-gates-20261016已经交付CPU单位/有效性先判定，并有ROUND-RESULT.json。本轮启动核对实际源码、HWiNFO安装配置和官方资料，上一目标轮分类为progress。

## 本机实际发现

只读文件版本为8.54-6090。C:/Program Files/HWiNFO64/HWiNFO64.INI及YeMan/HWiNFO64.INI均567字节，相关配置一致：

- SensorsOnly=1、SensorsSM=1；SensorInterval=3000、SensorAveragingInterval=1000。
- EC=0、SMART=0、ATAStats=0、SMBus=0、GPUI2C=0、WakeGPUs=0。

这些昂贵/无关采集项此前已关闭，不能把重复设置为0算成新增CPU收益。3000是配置文件值，本轮没有读取真实硬件映射来确认运行中的生产者周期。

现有恢复脚本只会整份复制YeMan模板，启动时还会设置固定0xA0亲和性。CPU亲和性本身不证明减少传感器工作量，本轮保留原行为，没有将它计为CPU优化。

HKCU HWiNFO64/Sensors本机只看到窗口、列宽等14个值，没有可直接验证的传感器禁用配置。先前把INI覆盖与所有单传感器状态丢失联系起来的说法过早：官方说明单传感器配置绑定传感器类型/实例并存于注册表。INI和注册表应分别处理，本轮没有写注册表。

## 已接入的上游控制

原脚本新增可选-SensorIntervalMs参数，接受0、1000、1500、2000、3000。0表示保留1000–3000内已有有效周期，否则使用受管模板的周期。既有bat和原生恢复调用不变，未新增应用UI入口。

显式restart路径：验证配置→确认HWiNFO退出→重新生成最终配置→保存原始/上次配置备份→写同目录stage→复制并SHA256验证→原方式启动→检查原共享内存健康。显式参数会沿现有UAC完整重试传递。

健康运行时使用start并带周期请求，返回12表示未应用；不修改活动INI、不重启，也不把请求写成自动排队任务。需要经同一脚本的restart路径才能应用。未带参数的健康start保留既有行为。

配置合并保留模板之外的用户节/键/值和目的文件编码，模板中已存在的管理键继续由模板控制；有效已有采样周期是明确例外。这里不是承诺保留全部用户覆盖：模板已有的其他键仍按旧受管规则覆盖。没有写任何license/单传感器注册表字段。

未知设置行被保留，统一使用目的文件原换行风格；混合换行可能被规范化，因此不声称合并结果逐字节保持原文。原始/上次备份保留精确原始字节。

缺少的键插入现有节内，缺少的节只追加一次；拒绝重复节、大小写重复键、节外值和零字节，避免不同INI读取器的歧义。按节建立一次索引，合并工作与有界行/键数线性相关。

## 资源、错误与恢复

输入/输出分别最多256KiB、最多8192行。文件使用限长stream读取，在分配前检查长度，读取中验证未缩短/增长。支持本机ANSI编码、带BOM的UTF8及UTF16编解码；编码错误在退出应用前的预检中失败。

配置备份仅两个固定名称：HWiNFO64.INI.ymcc-original.bak（已有则保留）与.ymcc-last.bak（本次之前的版本）。本轮创建的备份受输入预算约束；不是每次生成一个新历史文件。备份临时文件完成后再移动/替换，使用Windows PowerShell 5.1需要的[NullString]::Value调用File.Replace。

stage在finally清理。普通复制/内容校验失败最多重试5次，失败且没有新HWiNFO实例时恢复本次之前的精确字节；冷启动本来没有目的文件时，移除本次失败的部分文件，不误用旧事务的备份。原有进程退出确认、互斥和提权重试保持。

这部分处理只发生在已有启动/恢复事务，没有增加每秒配置读取、轮询、线程或硬件采集。两个原有映射健康检查逻辑没有被换成新采集入口。

## 为什么目前限制1–3秒

检查当前monitorHudModel和monitorHudFreshness发现硬件展示仍采用6秒有效期。直接将上游改为10秒会在两次源更新间形成周期性隐藏，而原生新鲜度有周期感知不能替代消费端这一限制。

因此本轮拒绝5000/10000等请求，默认保持现有3秒。更慢档位必须和消费者需求、字段来源有效期、自动浮动及FPS需求一起设计；不能单纯把全局周期调慢后宣称完成低功耗。1秒请求也会增加上游轮询频率，当前没有自动将它设为默认。

## 验证与本机边界

- 新tools/hwinfo_upstream_config_selftest.mjs：26项，执行实际生产INI/恢复函数，只替换进程/UAC边界，在独立临时目录实际读写配置。
- 覆盖周期设置/默认保持/越界拒绝、健康运行未应用回执、未知键与多节、冷启动无目的文件、插入缺少的周期、原始/上次备份、连续事务、永久损坏后回滚、冷启动部分文件清理、UAC参数传递、重复键/节、字节/行/合并预算、零字节、UTF16及UTF8 BOM/LF，以及500个节/1000个键。
- 原有tools/hwinfo_recovery_selftest.ps1保持未改：29项通过，仍验证原恢复时序、失败和互斥、提权，以及合成命名映射。没有启动/结束真实HWiNFO。
- 新旧本机只读快照记录两个安装INI的SHA256、长度、允许输出的相关键，以及进程ID集合；验证窗口前后一致，进程为9120。
- 没有部署脚本，没有调用真实恢复事务，没有更新正在运行的HWiNFO，没有读取真实传感器共享内存或测量CPU百分比。配置和进程集合一致不等于HWiNFO已采用新参数。

中途修正：生成器中PowerShell正则的$及换行反引号被JavaScript模板处理，导致语法失败，已改为保留字面量后重建；测试包装器的try/finally括号也已修正。File.Replace的PowerShell空字符串绑定问题由原恢复回归发现并修正。首次只读观察因继承的PSModulePath不适合Windows PowerShell而漏读文件，已改用该宿主模块路径、Stop错误策略，并强制两个INI都必须被观察。失败观察保留为LOCAL-OBSERVATION-FIRST.json，不能作为未修改文件的证明；最终LOCAL-OBSERVATION.json才包含有效哈希对比。

## 仍需继续的上游优化

本轮提供可控、可回退的全局配置基础，并没有完成自动按消费者需求裁剪HWiNFO传感器，也没有证明本机上游CPU下降。

单传感器禁用需要验证当前版本实际的注册表键格式、传感器ID/实例和设备插拔行为；不能凭显示名称去写未证实的注册表值。CPU/GPU/显存、温度、PresentMon帧率、电池及自动浮动所需数据必须保留。

官方把PresentMon进程排除命令-pm_proc_ex列在Pro功能中，已找到依据，但本机版本号不能证明具有对应许可能力。本轮没有向原启动命令加入该参数。隐藏传感器也不等同于停止监控；真正裁剪应使用已验证的监控禁用能力。

启动入口目前尚未把YMCC信息量预设/所有消费者的细粒度需求自动映射到周期参数。运行后还需要从既有统一快照验证生产者实际周期，不能只把INI写成功当作HWiNFO设置起效。持续目标保持active。

## 官方资料

资料用于核实能力边界，不授权额外系统修改。2026-10-16检索到HWiNFO官方论坛作者/管理员说明：

- “Hwinfo64 settings stored location”，HWiNFO作者Martin，2013-02-15：`https://www.hwinfo.com/forum/threads/hwinfo64-settings-stored-location.694/`。说明INI与注册表的职责，以及传感器类型/实例绑定。该历史说明不证明8.54的具体注册表键编码。
- “Supported Command-line parameters in HWiNFO64 Pro”，HWiNFO作者Martin：`https://www.hwinfo.com/forum/threads/supported-command-line-parameters-in-hwinfo64-pro.8549/`。列出Pro命令接口和-pm_proc_ex，并在2024-08-06说明INI只有固定选项集合，应通过UI变化确认键值。
- “PresentMon”第2页，作者2026-03-19至2026-03-23回复：`https://www.hwinfo.com/forum/threads/presentmon.9170/page-2`。说明进程过滤启动时传入；已有实例不会因为另一次命令自动采用参数。
- “Polling Period - Global vs Logging Cycle”：`https://www.hwinfo.com/forum/threads/polling-period-global-vs-logging-cycle.6610/`。作者说明Profiling Time可用于定位传感器读取耗时，实际间隔还含读取开销。本轮没有打开该UI或执行整机CPU压测。

此前对话的两条引用编号无法解析，已重新检索官方实际页面并以以上地址修正；报告不使用虚构的论坛帖号作为依据。
不依赖未经核实的第三方“更低CPU”配置配方。本机已关闭的底层扫描项以LOCAL-OBSERVATION.json为依据。

证据目录：G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-upstream-control-20261016/。UPSTREAM-RESULT.json、LOCAL-OBSERVATION.json及ROUND-RESULT.json记录实际范围；SCOPED-CHANGES.patch仅包含本轮脚本、新测试与本报告。
