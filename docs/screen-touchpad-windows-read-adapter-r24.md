# R24：追到 Windows 读取实现，检查后处理直接写入

2026-10-16。目标仍是虚拟Deck由Steam合成鼠标时闪烁，直接Mouse正常。本轮接续R23的短读取/调用方边，完成具体本地HID读取分支与固定官方SDL机器码对照，并检查三个post-normalize目标。**没有确认根因，没有生产修复，也没有实机消闪验收。**

## 读取链的新增正证据

Steam抽象对象把protocol对象存在+0x10。VID28DE/PID1205的protocol工厂分支安装CSteamControllerProtocolHandlerV1；其read slot+0x18转交底层HID对象slot+0x20。正常local provider创建CHIDDeviceLocal，read方法尾跳函数槽1727d88。

该槽不是仅凭相邻字符串命名：对应全局函数对象的resolver明确读取SDL_hid_read_timeout名称，经已核GetProcAddress封装解析并写入实际read槽；模块初始化代码明确加载SDL3.dll并以该模块遍历解析函数记录。固定本机SDL3.dll与同一份保留官方Steam ZIP成员重新逐字节相等（SHA66db88c6...，3093144B）。此为磁盘/静态链，不是MSI实际loaded模块或provider选择证明。

SDL默认动态表把该read导出接到d9700，再按backend分派。第一open成功分支的Windows实现d7b90完整506B，直接IAT核到ReadFile、WaitForSingleObject、GetOverlappedResult：

- 无完成/等待未就绪返回0，不拷贝一份合法Deck松触报告。
- 失败返回错误（部分取消路径返回0），不把失败伪装成64B成功。
- 完成后按实际count处理；必要时剥掉首字节0 report-ID，copy=min(capacity,actualCount)，返回同一长度。

因此，已核默认本地分支没有“空读取自动补成有头部的touch0报告”，也没有适配器把缺body的短包虚报为完整64；capacity73本身不会把64B Deck报告截短。R23短读候选只能继续落在**实际底层短完成/其它provider/backend/动态API覆盖**等条件，不能当普通无新包已造成故障。实际返回长度仍缺现场证据。

其他provider分派与两个SDL备用backend均保留在证据中，没有省略后宣称只有一条路。完整构造/pdata、RTTI、动态记录/正式IAT、官方包双模块核验和接受/拒绝反汇编范围见ReadAdapterReview/REPORT.md与CHAIN-EVIDENCE.json。

## 调用方与后处理的新增边界

候选caller用manager对象内的持久B缓冲区。normalize返回false时，已核guard跳过true消费者块，没有在该guard直接清buttons/XY。主线程完整factory证据补出了返回RAX与已构造对象RSI的别名；factory父manager与候选caller manager的来源仍未合并，不把相同成员偏移当作完整类型/实例证明。

三个表目标本身完整展开共1269B：

| 目标 | 直接行为 | 对触摸字段的有限结论 |
|---|---|---|
| 67d830/slot70 | 条件取负word[B+0x60]，另向对象写状态；把B内部指针传四个callee | 没有直接写touch19/20、pad XY或pressure；callee范围未知 |
| 67dad0/slot78 | 仅对buttons OR bit30/31 | 两条OR不清touch19/20，无直接XY/pressure store；其它callee未全展开 |
| 678ea0/slot80 | 叶函数将B复制入对象缓存，不把缓存恢复到B | 在对象缓存与B不重叠前提下不制造touch0；实际参数/别名未采集 |

不能把normalizer方法中保持字段，扩大成三个调用后全链都不变；也不能从缓存中出现touch0倒推缓存函数制造了它。PostNormalizeReview对MOVUPS内存目标的Capstone access标志进行了原机器码方向核对，没有用错误READ标记证明无store。

## 剩余具体边与状态

最具体待核的写入范围是63d430、63c5d0、63d990、63d750，它们接收B+0x24或B+0x58；0x34 stride不是已证写入宽度。另缺manager来源join、故障时provider/backend/loaded模块与实际read长度，以及同一实例Steam最终XY/range guard是否清touch。R20的rangeguard候选和R22父机全局门值均保留原限制。

本轮只改研究文档/索引，生产输入、频率、滤波、压力、UP、显示与设置均未调整，没有新增运行时CPU工作。没有运行目标应用、设备、测试、模拟、编译或部署；保留共享工作树和其他任务修改。R23本身为进展，本轮也产生改变下一步的正证据，整体目标继续。

证据根：G:/YeManCC-Work/Build/Analysis/Touchpad-Data-R24-20261016。worker两个封存清单分别44/44、23/23完整；主线Steam audit仍13区域396候选0缺失0未归类，只证明维护索引。verification.json记录范围、哈希与未解决项。
