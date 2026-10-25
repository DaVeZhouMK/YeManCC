# R27：原始坐标清touch、鼠标消费门与连接间隔

**范围纠偏：用户要求先核虚拟手柄输出源码。本轮新增Steam只读取证已从主线撤回，主线header恢复R22三global/两段代码；以下新增第4global的实现与compile-only仅为保留研究候选，不随当前主线导出。**

2026-10-16。上一轮18:47日志覆盖的是后段左板短触摸，不替代约18:42的移动数据；用户说明包来自其它任务，继续按真实版本解释诊断范围。本轮继续数据端，整体闪烁目标仍未修复、未实机验收。

## 新找到一条比滤波更早的清touch路径

固定官方缓存对应的steamclient64（SHA e9045710ec7ee47d7b7488d1c5dda2d933dec7e251e965967a5c48f4df68eb4e，不称当前最新或微星loaded image）在type9新counter归一化、A8c>=3的路径，67a37d..67a3cc读取controller_edge_cutoff。n非零时，某侧原始有符号XY任轴超出[-n,n]，就直接清该侧按钮touch bit19/20；n=0跳过。它发生在CTrackpadSource与坐标滤波之前，与之前的最终float越界清touch是两条路径。

名称不是字符串猜测：b7a50把全局wrapper1810010、名称12e8928和默认文本0传给cf74d0/cf7d00；后者明确写wrapper+30=self，对应67a37d使用的pointer slot1810040，又写object+18=name。默认文本0只能说明条件默认，不说明注册后的canonical parent、现场值或历史触发。现有3793份Host报告对比正常，仍不能排除Steam消费时在此清touch。

## 曾实现、现已撤回主线的研究候选

native/steam_touchpad_consumer_evidence.h现有只读导出增加第4个有名global：controller_edge_cutoff；并核验对应79B loaded原始坐标谓词。schema1保留、profileRevision改2，输出globals的intAt7c/floatAt78和rawCoordinateCutoffIntNonzero。只有最终原实例/module/file/binding检查全部通过才接受数值，未知版本或失败清空数值，未调用或修改Steam设置。

仍由既有logs.exportAll及virtual详细门触发。关闭门不构造Observer、不取时、不做进程/module/hash/内存工作。正常滑动、报表提交与显示路径没有新增调用、timer或线程。规范成功读取从16RPM/386B到21RPM/512B，+5次/+126B；原32RPM/1024B、单次128B、64次内存API组合和3次binding上限均不变。导出是非原子全局快照，不能自动证明所选controller或历史清touch。

ProviderBuild冻结header并以/std:c++20 /W4 /WX /c通过，只生成.obj；没有链接/执行、功能测试、仿真、设备操作、整包导出或部署。没有修改触摸位、坐标范围、压力、释放、实际频率、Mouse模式、焦点或Steam配置。该变更帮助辨别具体数据门，不是消闪修复。

## source更新门的实际下游已接上

CAbsoluteMouseMode的RTTI/COL与slot8=8b0190已按固定字节复核，接收V=T+8。它会读V60，也就是T68更新门。前置完整32B base helper可在H180置位时将V60一次性提升1，之后才复制到H248；因此不能把source刚算的gate原封不动套到所有consumer读点。

V1f即T27的touch直接写H834；V64即T6c写H270。8aca00和部分坐标处理在特定状态下根据V60决定是否继续，但false gate没有在已核直接写中清V1f/T27。跳过鼠标处理不是一份收到的neutral报告。所选source实际类型/生命周期仍需现场或更完整typed连接，stored absolute_mouse不代替runtime选择。

## 新的时间元信息候选

固定SDK profile的83属性回复tag11=4000，官方SDL/Valve协议名称为ATTRIB_CONNECTION_INTERVAL_IN_US。Windows678dd0解析tag11->A+d4；normalizer复制到B+a0；source slot58返回它到T6c；鼠标对象再取得H270。8b0830的一条touch-active路径用nominal/1000000作为时间步长，备用路径才按保存状态差计算。

另一个5000/nominal系数位于8b0200；此前父任务口头推测它为1e6/nominal，在读取原始float常量后已纠正。此区别不会改变侧任务“未直接读这些时间字段”的结论。

18:47 raw观察段3793份报告、3792个连续counter差的SourceTick跨度31375ms，平均8.274ms。Host源tick/成功提交、声明4ms、实际USB/Steam消费间隔是不同量，不能只因两数不一致改元信息或增加频率。调整整个设备间隔还可能影响其它消费者，现阶段没有安全生产修复依据。

独立审阅已完整恢复8b1330的switch（旧data曾被当作代码），其只对H290/H294施加比例，既不是SendInput也不直接读H254/H1e8/H270。后续实际输出链另由ActualMouseEmitReview继续。

## 证据与当前状态

证据根G:/YeManCC-Work/Build/Analysis/Touchpad-Data-R27-20261016。RawTouchClearReview保存名称/默认/canonical pointer绑定、79B谓词和主线patch；BindingConsumptionReview保存sourceview与条件门；DeclaredIntervalReview保存83属性、官方协议、常量和Host计算；AbsoluteOutputReview为独立封存侧任务。初稿8b0950/8acc40线性decode含跳表或失败，未计为完整已核根，不用于行为结论。

当前继续追Steam实际消费与鼠标输出，保留直接Mouse正常的对照。本轮的诊断/静态进展不能当作闪烁改善。
