# R23：继续追虚拟 Deck 数据，核对旧包而非只看现源码

2026-10-16。上一轮R22新增消费端日志导出并隔离编译，属于进展；整体闪烁目标仍未完成。本轮沿“直接Mouse正常，虚拟Deck由Steam合成鼠标闪烁”继续数据链，未修改输入/显示/频率，也未做测试或部署。

## 本轮改变了哪些判断

1. **保留头部01 00 09 40。** Linux注释的56一度形成候选，但官方SDL release-3.2.0实际要求ucLength==64，当前值有直接公开源码支持。不能把56直接改进去。当前SDK和已固定HHD也写64；后者不是Windows现场验收。
2. **固定managed USBIP后端没有缺帧超时自动发松触的分支。** 同共享序号不交付新包；无帧时IN请求留pending；unlink只有取消回复。主队列可延迟旧帧、满8丢整帧，但正常单pump/串行连接分支没发现旧新倒序或自动补零。没有推广为驱动全链正确。
3. **Windows normalize局部也区分无数据/重复数据与清零。** 重复type9 counter保留touch字段并返回true；无有效输入返回false，此方法不清buttons/XY。调用者仍可能另有行为，不能据局部函数宣布全链保持。
4. **两个现场包的Host都已包含SDK idle交接。** 从原ZIP的inputHostBusiness磁盘SHA+loadedMvid找到精确保留DLL，解析CLI MVID，离线反编译C#和IL，确认首次screen pad先Cancel/Join(100)，成功后才raw-submit；所选两方法IL指令相同。不再将“旧程序没有此交接”作为默认解释。它不证明当时线程状态或全loaded内存字节。

## 一条新收窄的数据候选

Windows read方法接收门只要求header合法且至少4B，没有在此要求完整64B；输入buffer在读取前被清零。如果下层adapter实际返回新counter但缺body的短数据，归一化复制阶段会从缺失位置读到0。现在仍缺具体adapter的长度/ReportID/错误处理及现场长度证据，不宣布短读发生，更不据此改counter、强持touch、增加频率或滤波。下一可核边是read的虚调用+0x18和normalize(false)的上层使用。

已知另一个候选仍是Steam后段坐标预测/校正后range guard清touch，尚无同实例故障XY证明。R22父机repeat/slowq/fastq=0只约束该次采样，不能替代微星历史状态。

## 精确证据

- DecoderReview/REPORT.md、EVIDENCE.json：官方SDL/Linux来源、完整Windows read/normalize根和关键分支。
- InterruptReview/REPORT.md、SOURCE-RECEIPT.json：固定SDK重新离线反编译、端点/队列/取消/长度规则，22文件封存清单。
- FIELD-ACTUAL-INPUTHOST.json、FieldHostReview/REPORT.md、RECEIPT.json、METHOD-EVIDENCE.json：现场真正业务身份、精确DLL和Cancel/Join/raw-submit指令。
- DecoderReview/PRODUCER-SOURCE-EVIDENCE.json：当前源与14:34历史导出计数投影。

13:46业务SHA7f5c285c.../MVID6e00678c...；14:34业务SHAdf28d324.../MVID62cbaf23...。通用导出manifest里的hostDllSha256实际属于fan-host，未用于本轮InputHost匹配。两个旧包均无新R18逐成功提交字段，不把其缺失视作输入正常、未实现idle修复或新诊断已经失败。

本轮只写研究文档和证据；没有新增运行时开销，没有运行应用/设备/功能测试或数值仿真，没有编译、部署、改Steam设置或覆盖共享Release。结论用于缩小根因范围，**不是消闪修复或实机验收**。
