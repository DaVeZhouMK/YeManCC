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


<!-- IDLE-IMU-DIAGNOSTIC-BUILD-20261108 -->
## 无序列IMU诊断树的前置去重（2026-11-08）

主线 inputCaptureOnGamepad 的详细sample路径原先无条件构建完整imuObj，再在两个sequence都为零时用imuActive:false覆盖。现在仅在原谓词gyroSequence != 0 || accelSequence != 0成立时建树；最终移动、原inactive覆盖、50ms日志门、队列/落盘和全部输入/Host/DSU链保持原字节。没有新增缓存、状态或计时器，也不根据sensorPresent或启用开关偷换谓词。

复用原 tools/virtual_sample_tree_perf_selftest.py 的显式 --current-idle-imu --before <固定前源> --out <新Evidence目录>，不增加默认ID/minimum/发布门。源提取的Release/Checked各1593例、9558断言通过；或误写成且的同消费者负例在第2例准确拒绝。覆盖序列四组合、独立标志、重置转换、宽整数、特殊浮点位型及最终dump字节；其他字段在测试中为opaque夹具输入，完整生产器其余部分仅作源码不变证明。原分配器探针另实际执行通过。

一次预声明固定成本活动（两组、每组11对、每臂1024次）中，无序列final-record局部cycles配对中位改善58.79%–62.33%，含dump改善48.68%–49.71%，均11/11更快；每次少41次请求/3232请求字节，活动路径分配不增，全部控制在5%保护内。这不是完整sample、全进程CPU、设备或游戏延迟降幅。完整主原生构建、重新导出/部署与安装版外部复测尚未执行；旧安装版0.339%峰值继续保留。

证据 G:/YeManCC-Work/Testing/YMCC/Evidence/RogCpu-20261104/IdleImuDiagnosticBuild-20261108/RESULT.json；续作同目录CONTINUATION.json。前置源码提取/CMD路径/编译器解析失败原件保留，正式成本活动没有失败后重计时或改门槛。
<!-- /IDLE-IMU-DIAGNOSTIC-BUILD-20261108 -->


<!-- TYPED-TELEMETRY-LOCAL-DELTA-20261110 -->
## gyro.telemetry 独占载荷与惰性编码（2026-11-10）

主线将原80ms遥测的标量/向量保存到 native/gyro_telemetry_record.h 的 Record；3个字符串及10份动态JSON子树继续独占拥有，最后完整Host仍深复制。原ResultMailbox token、actionPower/configuration围栏、失败post/terminal释放和旧JSON入口保留。普通路径到首个有效renderer才编码，不再先构造完整顶层/轴向JSON树；4次独立诊断门、原字段求值顺序和UTF16失败重试边界保留。

固定来源实际测试：Release/Checked各758核心场景/12629断言；每配置原JSON和typed门各2829场景，formatter280、惰性广播62，parity各271864断言；18次指定语义反例拒绝。四组固定成本共44行/484对，cycles及wall每行均11/11更快，原10/11和5%门不变，分配请求/请求字节均严格下降。Release捕获paired-median wall下降54.58%–83.17%，消息53.86%–79.19%，派发/广播22.64%–67.24%。这些只属于对应隔离代码链，不是整进程CPU降幅。

完整Native翻译单元固定快照的/Zs编译通过；1364条实际include拼写/1362个规范唯一路径已核对。主线精确7段补丁保留并行修改；正式header与测试来源仅一条独立注释不同，捕获片段另有一条注释不同。实际编译/成本原件和这些来源等价证明分开，当前整主线链接/ZIP/安装/实机尚未验收，sourceReady=false。

现有 tools/virtual_telemetry_hostframe_current_perf_selftest.py 增加显式 --typed-telemetry-build：prepare生成原核心/双布局成本输入，plan绑定显式工具、累计ledger与实际剩余额度，correctness/fixed-cost通过原owned RawProgram执行。现有 tools/virtual_gyro_parity_gate_selftest.py 的 --typed --prepare-only 为同一流程生成parity输入。公共准备不读取历史Evidence绝对路径，不增加默认ID/minimum/父门；旧模式和原断言/负例保持。

本轮公共执行桥仅已接线和计划检查，尚无其本体的原生实跑。样例correctness账剩113051ms，而相同完整core+parity曾用119133ms；不要启动此样例、重置原账或从其它阶段借额度求绿。新桥的fixed-cost还要求同输入的公共owned correctness回执。V2已有原始正确性/成本证据作为限定引用，不能倒填为新公共运行PASS。

证据：G:/YeManCC-Work/Testing/YMCC/Evidence/RogCpu-20261104/TelemetryDirectWireReview-20261110/FullPipeline/REPORT.md；公共桥见同目录CorePublicPort/HANDOFF-RUNTIME.json。原失败和预算保持；完整历史vcvarsall引导闭包未补，实际最终编译环境/argv/头库/工具/EXE及44行成本的限定核收不受该缺口否定。外部CPU观察器仍仅开发使用，不打包、不自启。
<!-- /TYPED-TELEMETRY-LOCAL-DELTA-20261110 -->
