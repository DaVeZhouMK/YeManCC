# R30：旧包覆盖与低开销生产/pump进度诊断

2026-10-16。继续虚拟SteamDeck触摸板闪烁。直接Mouse正常的对照保留，整体active，根因未定位。上一轮R29完成实际后端/参考deadline/driver长度源码绑定，属于progress；本轮不重做显示策略。

## 先查现有归档，确认缺什么

独立审查先列12份2026-10-15/16 G3E ZIP目录，在7个优先包定向读取45条目及17:05历史大日志的有限首尾，共约26.19MB主扫描；未读中段、轮转日志或其它故障域的内容仍逐项留空。当前扫描没有生产shared sequence、pump lastShared sequence、生产idle线程状态或实际endpoint completion计数。已有Host command/sample sequence、Host raw成功返回统计、SDK缓存64B、pending和pump线程IsAlive均不能代替它们。

现有InputThreadIsAlive可通过固定SDK源码/字段契约绑定为USBIP输入pump线程元数据，它表示线程存活观察，不表示pump在前进，也不是生产idle线程。不能把已有身份再次写成未绑定。17:05的86,822,313B成本日志只读取有限首尾；不把本轮缺项结论扩大到整条历史或未扫描域。完整archive/entry/hash/行号在研究目录ArchiveCoverageReview/REPORT.md、coverage.json。

驱动包清单补到具体版本：七包所保留drivers.xml中有oem4.inf/usbip2_ude.inf、04/19/2026 21.14.27.907；oem3.inf/usbip2_filter.inf为21.14.27.661。其证据是导出保留的包清单，非故障时设备实际选用/内核加载SYS字节，不能从版本号直接等同R29固定源码的完整二进制。

旧日志中多个Host、DeviceIdentity=1或SDK ctrl=0也没有证明同一mapping并发写。18:47旧Host2676在18:43:53.4829684+08已dispose，新Host11272从18:44:01.7751528+08出现，是这组观察的有序换代，不能拿两个PID/局部ordinal=1当碰撞证据。

## 已写入主线的诊断范围

只改InputHost/UsbipTransportObservation.cs，在已启用的约1Hz成本采样/原稀疏生命周期捕获中附加ProducerProgress对象。原asset SHA、assembly/MVID、精确controller/device类型门先执行；未核asset或未开日志不读新增字段。没有改Program.cs、SDK、native发送链、Mouse、触摸UX、压力/counter/UP/刷新率。

| 字段 | 实际读取 | 可用于检查 | 必须保留的解释边界 |
| --- | --- | --- | --- |
| ProducerSharedSequenceBefore/After | HMController._inputSeqNo前后各1次 | SDK writer对象字段是否推进 | 在最终mapped偶数commit/SetEvent前已赋值；不等成功完成发布 |
| PumpAcceptedSharedSequenceBefore/After | UsbipEmulatedDevice._lastSharedSeqNo前后各1次 | pump稳定读后的内部记录是否推进 | 在新report/cache/queue构造前赋值；不等构造/队列/发送/endpoint完成 |
| SequenceValuesUnchangedAcrossCapture | 比较上述各自前后可见值 | 标记本次顺序读取两端值是否相同 | 普通nonvolatile读取；相同不排除未观察更新、回绕/ABA；不等原子快照 |
| IdleThreadIsAlive/Observation | _idleThread引用及至多1次BCL IsAlive | 生产idle线程存活元数据 | true不证明正在写/抢写；false不独立证明绝无第二写者；null或异常明确未知 |
| InputWaitHandlePresent | _inputWaitEvent仅比较非零 | 等待句柄字段是否存在 | 不解引用、不验证handle有效性/信号；0仍可能走SDK Sleep4回退 |
| BeforeTick/AfterTick | 本捕获开始/结束uptime | 界定本次顺序读时段 | 不是USB报文或touch事件时间 |

缓存四个optional FieldInfo，四项完整shape核对成功后才读第一项。可选元数据绑定或GetValue失败仅令ProducerProgress未知/unsupported/error，原queue/cache采样继续保留；失败不输出猜测0/false。Idle IsAlive单项异常仅该值null/metadata-error，不强行丢其它已读原值。输出不带SDK对象、Thread或原始handle/address。

所有新增采样都在既有hidLock锁区外；不增加持锁时间、等待锁、重试或SDK方法调用。Progress成功不把原lock-busy状态升级为observed，失败也不降格原来已成功的64B缓存样本。DTO显式AtomicWithCachedReport=false、TransferCompletedProven=false、SteamConsumptionProven=false；不计算“已追平/丢帧数/完成率”等缺同步依据的结论。

## CPU与验证

每次已有capture正常最多新增6次GetValue（producer2、pump2、idle1、event1）、至多1次IsAlive和2次取时，以及一个子DTO/少量装箱；metadata绑定缓存，shape不符也缓存结果。没有每FRAME额外工作、新线程、timer、polling、handle系统查询、共享映射读取、输入/控制/重发。不能说零分配/零CPU；这是既有调度下的固定有界开销，尚未量测CPU。

候选形成后做独立窄静态复核，再冻结整个InputHost及三DLL引用共40输入进行compile-only：0错误，7条CS8602均在未修改Program.cs。最终候选SHA9cf03e76123fce73982ef428b0c3078fa0da9c93a6181215df8041d4f0dc6c65与编译输入一致，使用原源SHA前置检查精确写主线。未运行功能测试、fixture、仿真、Host/SDK、设备、真实输入或socket服务器，未导出/部署/覆盖共享Release。

此前R28单写者修补保持SHA88b5cfde91b47df2acb1667a6dea3b567f4b7eeb1ae8f0931ce5e324dde19c4a。R27 Steam消费端扩展保持撤回，未继续Steam客户端逆向。共享audit13区域/397候选/0未归类，既有task_view_helper.cpp缺锚点仍属于其它任务。

## 目标状态与下一边

本轮为有证据缺口的只读诊断补强，不是闪烁修复或实机验收。后续可把同Host/epoch的Host raw统计、ProducerProgress与缓存报告并列，区分生产对象不推进、pump记录不推进和已到缓存但端点仍未知；即便两端都推进，也不能单独宣布数据正常。实际滑动期间逐transfer长度/完成/时序和触摸边沿仍缺权威证据，不因此提高频率、补帧、滤波或改显示。

研究根：G:/YeManCC-Work/Build/Analysis/Touchpad-Producer-R30-20261016。ArchiveCoverageReview保存旧包覆盖；ProgressMetadataReview保存独立静态审查；ProgressMetadataPatch/PATCH-RECEIPT.json绑定主线精确变化；CompileOnly/SOURCE-MANIFEST.json和BUILD-EXIT.json绑定编译。
