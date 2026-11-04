# 虚拟手柄与陀螺仪常态 CPU 维护

目标是只移除已证实的重复工作，尽量使用现有上游对象和最短接线；不靠降低报告/采集频率、减少功能或省略诊断事实求低CPU。局部线程cycles、进程CPU、真实输入到游戏延迟分别验收，历史FAIL/未知不可覆盖。

## 已合入：关闭诊断后不再逐帧重复清理

InputHost/Program.cs 的 BeginVirtualSignalEvidenceSafely 在_costFlagEnabled=false时直接返回；CompleteVirtualSignalEvidenceSafely 仅在开启时处理完成。保留两个原运行期false赋值后的立即reset以及所有原目标/绑定/释放清理，没有新状态字段、timer、SDK/驱动/OEM调用或报告节奏改变。

这是详细日志曾启用创建collector、后来关闭时的重复锁/数组清空优化。从未创建collector的默认关闭情况不能套用这次局部收益。当前flag有3个赋值点（初始化+两个运行期更新），fixture必须拒绝新增未知写点或缺少原立即清理。

当前源边界回归11组、完整Host 45个生产输入/7个引用DLL编译通过；两种破坏分别捕获重复clear和缺失false-edge reset。一次11配对局部计量中受影响OFF行raw/typed cycles中位改善77.71%/73.80%；ON控制无超限回退，分配不增。SDK终端是fake，绝对成本/原件见报告，不代表整机CPU下降该比例或游戏延迟改善。

- [本轮范围与全部边界](G:/YeManCC-Work/Testing/YMCC/Evidence/VirtualInput-SteadyCPU-20261102-01a146ff/signal-reset-direct/REPORT.zh-CN.md)
- [当前源码/编译身份](G:/YeManCC-Work/Testing/YMCC/Evidence/VirtualInput-SteadyCPU-20261102-01a146ff/signal-reset-direct/FINAL-SOURCE-CLOSURE.json)
- [保留的候选A否决](G:/YeManCC-Work/Testing/YMCC/Evidence/VirtualInput-SteadyCPU-20261102-01a146ff/signal-reset/DECISION.json)

## 验证入口

复用tools/host_virtual_signal_evidence_selftest.mjs，提供新--evidence目录、经身份绑定的--baseline Program前源与--sd-baseline参照。默认仅正确性/预算回归；--reset-baseline提供精确优化前Program时才运行显式局部CPU计量，不自动加入发布夹具或新ID。原采样额度、日志行/文件上限、报告字节、抛错对象和neutral顺序继续断言。

最大合法身份并不保证单条诊断一定落盘。现行生产摘要超8KiB时保留loss和内存pending；测试必须核对完整最大输入、精确丢弃计数、OFF留存及原SDK恰好一次，不能缩短协议输入或抹去原历史无行FAIL。enabled lifecycle tail是已观察参数摘要，不是新SDK样本；只允许精确tail种类和sdkAttempt=false/joinEligible=false，不允许旧完成回流。

## 继续核对

- Host逐帧profile读取已完成下述有界优化；今后context所有权、释放或生命周期准入变化须重新核对其安全前提。
- Native物理采集、体感处理、序列化/Host解析、SDK输出逐段取当前源证据，不把历史局部PASS作为新版完整验收。
- 同版本实机进程CPU及按键到游戏端延迟未由本轮证明；目标持续进行，不宣称优化穷尽。

## 已合入：使用当前目标的只读 profile

TrySubmitFrame/TrySubmitNeutral 直接读取已选 HMController.Profile，避免每帧 GetProfile 的 context 锁和字典查询；没有新增缓存、线程或计时器。生产代码仅两处表达式变化，原 persona/target/context 准入、生命周期锁和 SDK 提交保持。

两个 SDK API 并非一般等价：disposed context/live controller 的保留反例中，GetProfile 拒绝而 Profile getter 仍返回。本优化仅在当前生产私有context、释放先置null target、失败路径保持原准入和串行所有权的前提下成立；不能删除该反例或把它改成等价PASS。Edge 使用实际已选的轴声明调整profile，live/neutral报告字节另有对照。

完整私有Host构建45输入/7引用与合入后的生产源码逐项匹配，原消费者12组及两个破坏负例的原件见证据；新主线正确性接线另记当轮回执，不把旧原件算作新执行。一次冻结性能活动六行局部cycles改善6.97%至19.44%，终端设备提交为fake、catalog/GetProfile为真实SDK。这不是进程CPU下降或游戏延迟改善，合入不自动触发发布、部署或性能重测。

证据：G:/YeManCC-Work/Testing/YMCC/Evidence/VirtualInput-SteadyCPU-20261102-01a146ff/profile-boundary-audit/DELIVERY.json。合入前后的源码、fresh生命周期与接线结果位于同目录mainline-integration；历史失败和SDK反例不覆盖。

## 控制器IMU重复身份比对

当前Legion能力库controller-IMU路径的Session先校验handle、六轴和读取接口，之后只做本地复制/时间差运算，再进入Owner重新比对同一个Key。本优化使用仅Session可调用的private提交方法复用本次已验证handle；独立Owner::process仍保留原准入。没有新增状态/缓存/定时器，采样频率、dt、滤波、融合与可选空间读取保持。安全前提是原BUS串行锁、UI只排队和回调不重入的当前源码闭合；这不是MSI/ROG Windows-sensor路线优化。

原Owner19/Session20加FP顺序边界1，在before/candidate、Release/Checked下160cases/17448检查通过，迟延finite检查负例被MXCSR标记断言拦截。当前交付真实GamepadMotion.dll（e11d59f8…）的原9场景两配置四次消费者也通过，72个句柄平衡并全部卸载。

一次冻结成本活动使用真实DLL：短/长Key、静止/运动、四种可选读取共16行，各11对/每端50000调用，17.6m调用；所有行满足原3%cycles、9/11更快及耗时保护，局部cycles改善6.86%至16.24%。编译上下文内联差异在测前明示，不能当纯Key比对或进程CPU/游戏延迟收益。另已用R9冻结主程序TU的519项目输入/1303个唯一实际编译输入做private compile-only并核闭包，候选汇编为1次Key比对/1次axis.valid，原R9为2次/1次；没有运行或链接应用。这份原件不包含后继mouse/mutex主程序写者的变更。

合入保留原文件未改动行的原字节/混合行尾。已测private源被Python文本写入转换为CRLF，正式合入用真实MSVC预处理等价记录绑定原成本/完整TU编译原件，不回写旧证据或重跑成本。原Owner/Session消费者ID、正常组、原负例和原预算保留，新增边界仍放原Session场景；普通导出不自动增加本专项整轮。

证据：G:/YeManCC-Work/Testing/YMCC/Evidence/VirtualInput-SteadyCPU-20261102-01a146ff/motion-boundary-static-audit/real-dll-cost/；actual-main-compile/保留R9实际TU与汇编，mainline-proposal/保留变更/行尾来源和CAS回执。已合入与当前验证状态以当轮CAS/READY为准，全进程虚拟开启CPU和真实按键到游戏延迟仍未验收。
