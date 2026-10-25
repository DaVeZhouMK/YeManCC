# R28：回到虚拟 Steam Deck 生产端，修复首次提交前的双写者

2026-10-16。用户纠偏后暂停 Steam 客户端逆向，核当前触摸事件 → 共享状态 → BUS/管道 → Host → raw/SDK 输出。直接 Mouse 模式正常的对照保留。持续闪烁未定位、未验收；本轮修复不能称为闪烁已经改善。

## 已确认并写入主线的缺口

原 SubmitSteamDeckWithScreenPads 只有首次 touchpads 或 screenButtons 才取消并等待 SDK idle 线程。此前普通 FRAME/NEUTRAL 调用 SubmitState，而固定 SDK 的 4ms idle 线程也会读取 _lastSubmitted 并调用同一 SubmitState。它们共享编码状态、报告缓冲及滚动计数器，Host 的生命周期锁不能约束 SDK 内部线程。可达交错为 idle 读取旧 S0 → Host 写 S1 → idle 后写 S0；这属于明确的旧状态覆盖/并发写入缺口，尚无该交错的现场捕获。

只修改 InputHost/ScreenTouchpadReport.cs：第一次合法 Deck FRAME/NEUTRAL 就执行已有 Cancel/Join 接管，此后沿用已有 raw builder，删除 SubmitState 旁路。等待失败仍抛错，不能带着两个写者继续发。Program.cs、native/main.cpp 和其他任务代码本轮未写。构造至首个 Host 提交前仍可由 SDK 单独 idle；该阶段没有 Host SubmitState 与它并发。

提前接管前逐字段核对固定 SDK/profile：普通按钮、方向、背键、扳机的 >0 阈值、轴、加速度与角速度、零保留字段保持原有已接线语义。counter 接续 Join 后的最后已编码值。字段等价不代表节奏等价：此前首次触摸前的 SDK idle 重复帧被去掉，counter 此后按 Host 实际提交推进。触摸之后原本就已沿用该 raw 节奏。详见研究目录 HostProducerReview/COMPATIBILITY.md。

## 生产链逐段结论

| 链路 | 当前源码结论 | 证据边界 |
| --- | --- | --- |
| Touch/Pen → Core | 独立输入与传统路由汇入同一状态机，拒绝重复/陌生触点，MOVE 不生成点击 | 未证明输入事件与物理手势的全部时序 |
| Core → BUS | 当前目标锁内取最终快照；发送左右 pad，未触摸侧明确归零；全部松开时省略字段 | 准入/epoch 变化会撤销手势，最新包 discardedGestures=0，不能当该次原因 |
| 快速序列化 → pipe | 从当帧递归编码 touchpads，pipe 有自己的异步存储，没有发现旧 JSON/调用者缓冲后改覆盖 | 未运行传输测试 |
| Host 解析/提交 | 省略 pads 解析为默认释放；普通输入 hash 仅校验/缓存计算，不按相同输入跳过合法新 FRAME | Host 成功返回仍不等于每帧到达消费端 |
| SDK 写者 | 修复首个业务提交前仍存在的 Host/idle 并发；接管后未发现重新启动 idle 或 SubmitState 的 Deck 旁路 | 此缺口不能解释已接管后的持续闪烁 |

Deck 另有真实精度差异：float 坐标先取整到屏幕像素，再归一化为 signed16 pad；Mouse 使用 float 差值和余数。它会增加坐标量化，但尚无证据证明其造成显隐闪烁，因此未以滤波/人为位移修改。

## 固定上游生产协议对照

- SDT 固定提交 deee7fa887c2864ee3b54eba2c32b2f58b547349 读取物理 Deck，虚拟输出为 X360/DS4；没有在该范围找到虚拟 Deck 生产路径，不能把它当本案发包正常样本。
- HC 固定提交 a250b19a4b7e29dac3b383f2d1ac517d1af43e4e 有 SteamDeckTarget → libviiper 输出链。其 C# builder 的 byte3=56、counter=0 并非最终线上格式的可靠依据：独立固定 VIIPER 参考源码会重建 byte3=64 并覆盖递增 counter；参考源码尚未绑定到 HC 内置 DLL。固定 SDL 入口明确要求完整64字节且 byte3=64，Linux不检查byte3。因此保留当前64，不能照搬 HC builder 的56/0。
- HC 普通触摸也写 pad pressure=0，点击才写65535。固定 SDL/Linux 以独立 touch bit 判接触；SDL 对相同 down/XY/pressure 去重，但不因此松触。当前滑动 pressure=0 和重复XY并非据此可改的生产错误，也不把 HC 点击65535强搬到本机的32767。
- byte4 公开命名为包/状态序号；已核消费者没有按此值差×4ms计算触控事件时间/速度的实现。不能把现场约8ms一次、counter每次+1直接判成错误时钟，更不能据此改counter步长或刷新率。
- 一处待继续核的结构差异：R28最初只核到参考VIIPER设备方法的gate deadline重复构建能力；R29补齐同commit服务器后，已闭合有pending读、auto/keepalive、未取消条件下的6ms attempt期限→完整状态→RET_SUBMIT→socket Write源码链，重复时不增加状态frame。当前固定SDK没有新共享序号时继续等待。实际HC DLL与此参考源码未绑定，wire执行及闪烁因果未证；照搬6ms会增加当前约8ms Host提交下的报告数量，因此不据此改SDK、补帧或增加线程。详见后续screen-touchpad-endpoint-hold-and-driver-length-r29.md。

上游38份固定文件、commit、SHA与逐行证据在研究目录 ProtocolReferenceReview/SOURCE-MANIFEST.json、REPORT.md、PROTOCOL-64B.md。仅静态读取，不等于当前运行二进制绑定或实机验收。

## CPU 与验证

单写者接管只在每个 target 的首次提交发生一次，不增线程、轮询、timer或刷新率，去掉接管前 idle 的重复编码/发布。没有测量 CPU 净收益，也没有运行功能测试、仿真、应用输入或设备操作。既有 Mouse、触摸 UX、震动、显示/焦点规则未改。

冻结当前 InputHost 全部源码及三个 DLL 引用，独立输出目录 compile-only 成功，0 errors；7 条 CS8602 警告位于未修改的 Program.cs。没有覆盖 Release、导出测试包或部署。源码 SHA 与编译输入绑定见 SingleWriterPatch/PATCH-RECEIPT.json、CompileOnly/SOURCE-MANIFEST.json。当前补丁 SHA256 88b5cfde91b47df2acb1667a6dea3b567f4b7eeb1ae8f0931ce5e324dde19c4a。

R27 新增 Steam 消费端取证扩展保持撤回；原 R22 读取器保留。当前输出端协议参考对照与完整路径矩阵在同一 R28 研究目录独立保存。

研究目录：G:/YeManCC-Work/Build/Analysis/Touchpad-Producer-R28-20261016。目标继续 active，后续仍从实际生产报告/交付边界推进，不以重复显示修补代替数据核验。
