# 源数据复用命中前去掉重复读数复制（2026-10-16）

本轮把既有 HWiNFO 原始内容复用检查移到 scratch 复制之前。完整原始读数和传感器身份没有变化、原有最终头部/周期/身份表核验也通过时，直接恢复既有解析结果；内容变化或不能证明可复用时，继续有界复制后解析。仍由原生唯一采集线程处理，没有新增数据层、定时器或显示侧读取。

上一轮 hwinfo-duplicate-frame-research-20261016 的 ROUND-RESULT 和实际 PowerShell/帧输出哈希已核对，属于 progress。本轮仍以减少上游与下游的实际无用工作为目标，不以测试通过替代整机验收。持续优化任务 active；本轮只源码和隔离验证，未部署/重启、未新增真实硬件样本，未测 CPU 百分比。

## 重复工作的来源

此前复用机制已经避免相同数据重复分类，但 monitorParseHWiNFOView 仍先把整个 reading 区复制到 parseScratch，再拿副本与已接受字节比较。因此 HWiNFO 每 3 秒发布、唯一线程每秒读取的普通路径中，三轮会复制三次，尽管两轮最终只是命中原有结果。

现在先校验布局、区间、时间可表示性与原复用资格，再用已有 Reuse::matches 对映射中的全部读数字节及完整传感器表比较。相等后保留原始末尾校验：完整布局印记、真实周期、完整身份表；全部通过才恢复独立的 NativeMonitorHw 包。没有仅凭相同时间戳或 hash 判断命中，没有缓存借用映射内指针。

失配时才创建/复用原 scratch 并复制。重要的是 cache.stage 同步移到复制之后，仍保存实际送入解析器的那份副本；不能直接缓存另一次实时映射内容再把它与解析结果关联。命中失败的清理、有界三次重试、源映射名称切换、代际/空闲释放均沿用原有实现。

原 1 MiB 复用字节上限、32 MiB scratch 上限、支持周期范围及 Packet 非分配复制约束没有变化。没有为优化扩大快照、日志或缓存体积。

## 复制次数和数据对照

编译冻结/当前真实 parser、真实映射 reader 及发布器，Windows 映射 API 仅替换为夹具内存。只在实际 scratch memcpy 和分类循环旁插入计数，未替换生产判断。原有 48 项综合复用用例继续编译执行；扩展的周期/副本检查后，两侧各 67 项通过，16 个完整输出用例保持相同。

每组输入 2048 行，每秒消费一次，共 300 次。数据结构、值和元数据都要通过实际全字节比较：

| 上游发布周期 | 旧复制次数 | 新复制次数 | 旧累计复制字节 | 新累计复制字节 |
| --- | ---: | ---: | ---: | ---: |
| 1 秒 | 300 | 300 | 194,150,400 | 194,150,400 |
| 1.5 秒 | 300 | 200 | 194,150,400 | 129,433,600 |
| 2 秒 | 300 | 150 | 194,150,400 | 97,075,200 |
| 3 秒 | 300 | 100 | 194,150,400 | 64,716,800 |
| 6 秒 | 300 | 50 | 194,150,400 | 32,358,400 |
| 60 秒（既有资格上界夹具） | 300 | 5 | 194,150,400 | 3,235,840 |

后两行只覆盖原缓存资格边界，不代表把实际上游周期改到 6 秒或 60 秒；本轮没有改任何频率。数字是合成工作量，不是 CPU 百分比、真实时间、峰值/常驻内存或省电比例。每次仍支付完整内容比较、头部读取和映射包装成本，新 poll 仍支付原始复制及解析成本。

无 owner、未知周期、1 秒重复输入以及大于 1 MiB 的表均保留原普通复制路径。相同 poll 内的 CPU 值修正会重新复制和解析，之后才再次可复用。既有测试还覆盖标签/单位/全部传感器名/ID/instance变化、映射失败、诊断开关、缓存分配失败、跨 GPU 插拔及源冻结失效；没有用重复观测刷新原来源时间。

共享内存仍只提供“读取时未观察到变化”的一致性，原始代码也没有生产者原子快照协议。本轮没有宣称解决该固有限制；完整字节比较前后的印记与身份复核未削弱。

## 跨机、时间和消费回归

实际唯一 hub/显示协议回归 18 项；静态单 worker/锁域/代际/源映射/重试等接线 14 项，后者明确不是实机线程验收。对应编译区域与当前源码逐段核对，避免其他任务改变 main.cpp 的全文件哈希后误认旧测试仍代表当前 parser。

原始 HWiNFO 重复 RTSS 转读组目录删除/重排回归重新编译当前 parser，66 项原生、25 项真实 hub/model/协议通过；完整普通 CPU/GPU、内存、网络、帧统计、源身份和独立 Windows 数据没有丢失。该回归不重新开放隐藏的 Steam 输出。

生产者时间边界 22 项通过：不可表示/溢出时间仍在比较或复制行数据前拒绝，停更/零时间不变健康，真实更新恢复。Win5 真实 CSV 的当前原生专用/共享显存和稀疏槽位 47 项、实际消费链 40 项通过，并回放 Win5 58 与 ROG Xbox 2025 276 条旧快照，保持兼容。原历史优化的初始化计数不算本轮新增收益。

本轮合计 299 项检查，其中 14 项为静态接线断言，其余为实际相关代码的隔离执行；334 条归档回放属于消费回归，不额外充作 334 项独立测试。无完整应用重构建、安装版实测或新的跨机硬件采集结论。

## 本轮上游筛查决定

仍先筛查了 HWiNFO 上游选项，在固定原始镜像中确认引用/分支，并保存原始反汇编和决定：

- NvmlSupport/oneAPI 与 GPU 后端初始化关联，未证明关闭后保留全部现有读数，保留。
- AsusWMI 涉及机型/主板分支，没有完成所有需要温度等项目的依赖证明，保留。
- FlushBuffers 的跟踪辅助会枚举卷并调用 GetDriveTypeW/CreateFileW/FlushFileBuffers，未证明是当前稳定采集热点，亦未证明修改不影响探测行为，保留。
- PollSleepingGPUs 固定镜像默认已经 0，没有可核验的新增收益，保留现状。
- CSensorPerfLim 的表含 IA/PROCHOT/CPU package/platform 条目，不能误当成通用 GPU 专属开关；本轮未完成底层采集门及依赖核验，不增加裁剪。

筛查目录：G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-upstream-candidate-screen-20261016/，OPTION-REFERENCES.json、PERFLIM-LABEL-TABLES.json、FOLLOWUP-0x140216464.txt 和 DECISIONS.json。研究副本未执行，原始硬件 API 未调用。

HWiNFO 静态策略逐字节保持上轮哈希 f838f6f8eab72eef35a54f141fbc17a4d4577c24c6d6b81e45742d44cecc1292；PresentMon=1、既有 65 组固定裁剪和兼容兜底不变，不增加 AMD 开关或自动 SnapshotPolling。

## 交付

生产修改仅 native/main.cpp 中的解析器复用/副本排序及相邻说明；整个 nativeMonitorLoop、reader、结构、发布器、帧输出头、复用头、新鲜度规则及 PowerShell 策略均保留。新增 tools/monitor_parse_copy_reuse_selftest.mjs 复用现有综合夹具，冻结/当前源码与同一组输入对照。

证据目录：G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/source-copy-reuse-20261016/。

关键文件：BASELINE-PINS.json、native/before/RESULT.json、native/after/RESULT.json、NATIVE-RESULT.json、CONSUMER-RESULT.json、WIRING-RESULT.json、COMPILED-REGIONS-RESULT.json、INVARIANTS-RESULT.json、producer-time-regression/、amd-csv-regression/、rtss-mirror-regression/、REGRESSION-INDEX.json、SCOPED-CHANGES.patch、PATCH-CHECK.json、ROUND-RESULT.json。

补丁以本轮起点为解析区域基准，保留外部并行修改，只打包本轮所有权范围。未暂存或提交。持续目标尚未完成：实际安装版数据完整性、上游策略采用和总体 CPU 成本不能由本轮局部复制检查代替。
