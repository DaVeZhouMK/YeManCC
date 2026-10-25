# R18：SD SDK 后端实际取帧与逐提交边沿

日期：2026-10-16，Asia/Hong_Kong。继续用户“直接Mouse正常、SD/Steam闪烁，应查数据”方向。本轮没有新增显示策略或输入协议修改，目标仍未通过实机验收。

证据根：`G:/YeManCC-Work/Build/Analysis/Touchpad-Data-Backend-R18`；前轮现场仍为2026-10-16 13:46:59的policy16失败归档，本轮没有新实机归档、没有部署或启动真实虚拟设备。

## 从静态复制分析推进到实际 SDK 方法

前轮10000帧覆盖native状态/严格parser/encoder，但不含SDK共享区读回。本轮逐字节核验锁定HIDMaestro.Core 1.7.0 SHA256 `bd42a99bcb260435ce25796c54a4b792f8a2ced6ab78659c0cf926011663938e` 和MVID `c61e2715-47ae-4cf3-87f7-244c330ef80e`，直接执行真实 `HMController.SubmitRawReport` 与 `UsbipEmulatedDevice.TryReadInputFrame`。

两者只绑定测试进程私有362B非托管内存。用GetUninitializedObject绕过controller/backend构造器，补齐profile/builder/私有buffer和指针；事件句柄、connection、后端线程为空。HMContext只加载profile并解析描述符；没有调用CreateController、EnsureInputMapping、InputPumpLoop、driver/socket或真实输入。最终释放私有内存。

21项通过：

- 实际composite builder为64B、reportId0；空共享区不生帧。
- caller在Submit返回后清空，后端仍读到完整帧；后续提交不修改上次返回数组。
- 同一共享序号不重读；未消费的两个提交合并为最新接触帧，不凭空插入中性。
- odd序号拒绝读取而非变成中性；稳定后可读；raw提交清除旧extended-length。
- 共享序号与packet counter回绕均保持正确。
- 20000串行写读、50000并发写入，8973次实际成功读取，无坏帧，末帧79999可取得；显式release才出现预期中性。

这些结果比单独编码器模拟更靠后，仍不能证明InputPumpLoop、USB完成回包、现场时序或Steam消费。并发试验是有限调度样本，不是所有硬件内存序的形式证明。原始数据：`SDK-Memory/result.json`，来源/回执：`SDK-Memory/receipt.json`。复现入口：`tools/steamdeck_sdk_memory_selftest.ps1`。

## Endpoint与GET_REPORT边界

固定composite描述符的mouse0x81、keyboard0x82、vendor0x83是不同接口。本后端input pump只从主0x83取pending，0x81/82没有额外鼠标/键盘包生产分支，不能把它们的长期pending当作第二路鼠标输入或触摸被清空。Feature ACK/echo也不能等同实体固件lizard模拟。没有据此给附加接口发送全零包。

单连接、单input pump、正常同步发送的稳态0x83 FIFO/pending组合中，尚未找到能证明的逆序缺陷。但`GET_REPORT(Input)`与interrupt之间存在条件交错：control先读N/off并停住，input pump发送N+1/on，control随后回复旧N/off；这不要求FIFO积压。**当前没有故障窗GET_REPORT请求记录，更没有证据证明Steam把两路结果按该顺序合并**，所以它是下一条需验证线索，不作为生产修改理由。

独立准确源码、描述符和IL复核：`ENDPOINT-QUEUE-REVIEW.md`、`EndpointReview/`。真实队列状态试验另行保存为Queue-*；它不进入socket或InputPumpLoop，不能冒充线完成验收。

## 纠正前轮1Hz采样盲点

R17的SDK_lastInputReport每秒采样可能错过短促松触；字段正常不能证明报告没有抖动。本轮新增 `InputHost/SteamDeckPadSubmissionEvidence.cs`，在**原有串行SubmitRawReport成功返回后**，对每个已开启诊断的原报告做固定数值比较：

- packet counter连续性（含回绕）；wire touch/click、XY/pressure与本次Host envelope是否相符。
- 累计全部报告/触摸边沿/点击边沿/各种不匹配与观察异常。
- 固定32项状态边沿环，包含command/sample sequence、native source tick、counter、pad数值、expected touch/click、错误位及neutral-command标志。普通坐标移动不新建环项；另外保留最后一个成功报告值。
- 溢出记录明确OmittedRows；日志关闭时不比较跨缺口counter。EnabledSpans包括首次开启，不能解释为丢失帧数。新controller reset另起TargetGeneration，不把前后目标串联。

开关复用既有 `_costFlagEnabled`，没有新UI设置。每个报告不建JSON、不写文件、不查时钟/反射、不新增线程、定时器或发送。原有每秒成本窗口创建固定大小不可变快照：MSI继续由现有诊断worker序列化写出；普通现有成本分支同样携带快照。窗口队列失败由既有诊断统计处理，提交成功逻辑不随之改变。

下份包含新Host的日志：`input-host-cost.log → steamDeckPadSubmissions`。结合前轮 `usbipTransport.snapshot.LastSteamDeckPadReport` 可分别观察“Host原始API成功返回”与“后端最后缓存”两个位置；两者顺序读取、可能跨帧，不做原子对应或Steam已接收的承诺。若发现短促off/on、非预期click、counter异常，即使1Hz缓存恰好采到正常值，逐提交累计仍会保留；若真正问题只在更后端/消费者，则此统计仍不能单独排除它。

## 生产边界与验证

`ScreenTouchpadReport.cs`只把原单行编码→SubmitRawReport拆为同序局部变量，API返回后旁观；编码方法、parser及idle handoff与本轮Before字节一致。`Program.cs`只传已有日志门、在原新controller重置处重置统计并加入成本快照。`MsiCostWindowDiagnostics.cs`仅携带已生成的不可变对象，worker不反向读取HostSession。没有更改native、触点事件、点击分类、鼠标路径、报告值、刷新/提交频率、焦点、Steam配置或设备设置。

纯提交统计24项通过，覆盖快速off/on、unexpected release/click/XY/pressure/counter、ring溢出、关开日志、controller reset和快照独立。三轮100000报告记录的分配为128B、0B、0B；首轮保留为一次性运行时开销，不声称冷态零分配。稳态100000次约11.5–11.8ms，仅是此机器隔离方法计时，不换算实机YMCC CPU占用或功耗。

完整当前Host冻结40项来源编译成功，0错误、原有7条nullable警告，生产安全512codec/idle自测通过。再在冻结副本中增加fixture-only early dispatch，直接运行**生产SubmitSteamDeckWithScreenPads +真实SDK写入私有内存**9项通过：首次接管取消idle token、packet衔接、1000帧同一touch只存一个边沿、短促neutral/recontact均被记录、日志关闭不改变实际写入字节。该fixture未创建controller/设备/pipe/socket；原始生产输出与带fixture输出分别保存，最初fixture缺using导致的编译失败日志保留。

诊断producer保留原10组143项，并增加真实worker不可变pad快照一组，Release/Debug最终各11组157项通过；现有成本连续性Release/Debug各19组226项通过。新字段是诊断输出，不进入pipe协议或ACK。为保持原有精确方法抽取测试，fixture补充同一统计类依赖，未删除或降低原断言。

回执入口：`validation-summary.json`、`source-boundary.json`、`Submission-Evidence-Measured/receipt.json`、`Host-Integration/receipt.json`、`Cost-Producer-Pad-Snapshot/result.json`、`Cost-Continuity/result.json`。当前未打完整发行包、未部署；实机闪烁目标仍未完成。


### 真实队列方法补充验证

`tools/steamdeck_sdk_queue_selftest.ps1`冻结锁定SDK并对私有未构造对象执行实际HandleSubmit共7次，10/10通过。验证空主FIFO按顺序停放请求，0x81/82 pending不会消耗已有主FIFO，0x83按FIFO取出最旧帧，再转为pending。`_connection=null`、worker/audio/事件句柄均为空；reply函数不会进socket。因此这里只证明真实方法的私有内存状态变化，**没有观测reply状态、字节或完成顺序**。未执行Audio依赖的HandleUnlink、InputPumpLoop、capacity8填充或GET_REPORT。fixture清理集合并核验无私有worker/句柄残留。准确源码/锁定IL/回执见`Queue-20261016-150453/receipt.json`和`QUEUE-STATE-REPORT.md`。
