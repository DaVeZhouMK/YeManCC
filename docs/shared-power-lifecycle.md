# 统一睡眠生命周期

原生 `native/main.cpp` 的 `PowerLifecycle`、generation 和恢复事务是唯一电源权威。`sleep_power_dispatch.h` 只负责进程内通知顺序及补送，`sleep_power_state.h` 是同进程只读 ABI。前端 `src/bridge/powerLifecycleState.ts` 共享读取原生当前事实，由首个／最后一个订阅者管理生命周期，不提交唤醒、不重放业务写入。

`publicationRevision` 和 `observedNotificationSequence` 必须分别防倒退。callback 接收通知后、UI 消费并发布前，接收序号已经变化，而发布版本与 generation 可以不变；此时诊断 phase 仍可为 Ready，但 pending 门已经关闭硬件准入。旧 Ready 不能覆盖这个观察，也不能满足新的 Ready 确认。拒绝旧事实必须先于修改两个水位；最后一个订阅者退出时清除水位，晚回包不得进入新生命周期。

订阅者可能同步触发新的原生事实。一次状态通知被新状态取代后，必须终止旧状态的剩余分发，避免其他订阅者先收到新 Suspended、再收到旧 Ready。仅元数据变化不重新派发业务通知，也不截断仍有效的当前分发。共享 Ready 与设备、游戏暂停租约、Host 退出和物理归还回执各自保持职责。

读失败、未知状态或未准入状态沿原 1／2／5／15／30 秒封顶退避继续只读观察。show、focus 和重复通知不刷新等待期限；确认恢复后停表。Ready 确认只由请求后启动的有效读取满足。页面销毁须归还订阅、timer、在途读取的消费资格和确认请求。

唯一回归 ID 为 `lifecycle.ui-power-recovery`。现有默认入口保留原 27 个 UI 场景、14 个共享源场景，追加 6 个完整周期／乱序／截断边界；连续 20 周期使用逻辑时钟。旧场景的有效断言保持，只在需要观察读取时显式推进原退避时钟。定向运行：

```powershell
G:/YeManCC-Work/Testing/YMCC/Run.ps1 -Optimized -Mode run -Rerun -Tests lifecycle.ui-power-recovery -BudgetSeconds 300 -EvidenceDir <新的证据目录>
```

2026-11-12 当前源码、旧源反例、实际原生 reader／Dispatch 到前端的快照回放，以及九份普通导出日志的核对见 `G:/YeManCC-Work/Testing/YMCC/Evidence/SharedSleepLifecycle-20261112/SOURCE_READY.json` 和 `REPORT.zh-CN.md`。这次没有重跑完整原生父组或实际系统睡眠，产品进入下一次自然 Web 构建。既有 2026-11-10 公共睡眠记录及失败保留；本入口维护公共生命周期，不接管 Steam 游戏睡眠修复或模块私有设备职责。


2026-11-12 原生恢复广播分类后继：较早PBT18/7不再先清仍待原506/507分类的事务。原生唯一owner保留原任务/精确PID租约，首次2秒观察不随重复通知续期；实际OFF→ON只接原5秒窗口剩余时间。缺失败证据时走原资源恢复，不根据缺证下发睡眠。迟到原始506可以加入已准入的重试，保持原代次/种类/截止和独立240秒身份。

`sharedSleepWakeClassified(generation)` 是纯原子软件观察门，消费端与原phase/最终Kernel见证/owner/预算相与；不得借此加入硬件或InputHost Ready条件。分类完成沿原common路径并以`steamOriginalRecoveryPowerReady(generation)`接续同代次。Dispatch新增接收FILETIME只属进程内证据，SharedPowerV1 ABI保持48字节。

后继原件在`Testing/YMCC/Evidence/SharedSleepLifecycle-20261112/early-resume/SOURCE_READY.json`、`CLOSING.json`和`REPORT-CORRECTION.zh-CN.md`。原29项只按旧epoch引用，最终两个迟到506-after507场景由同一canonical ID的精确续测入口实跑通过；没有一次完整31项父PASS。最终自有33片段的整TU语法通过，其他并行后继另列，不追赶重编。Steam/非Steam原消费者接线、配套Native链接/包及现场仍OPEN，目标ACTIVE；未部署或操作真实睡眠/设备。


同日原生生命周期补验已沿原`sleep.non-user-wake-consumer`增加互斥定向参数`--resume-classification-lifetime-only`，只执行4项：pending期间critical恢复、原506回调/GUI与507全缺时的有/无PID正常归还、实际setConfig取消分支在guard仍ON时的一项保留及两项关闭。原ID和minimum保持，默认整组声明35项但未实跑整父。正式4项与2个单项语义反例、精确fixture写回及成本见`early-resume/LIFETIME-SOURCE_READY.json`；旧29与后继2仍分别引用各自epoch。此设置用例只证明原取消分支，不代签完整RPC验证/锁；PID恢复仍为运输内存端，不代签设备或进程实际归还。

现场负责人已独立只读核收后继2项/4项的230个归档成员、精确raw序列与当前37函数/common/begin/三worker/classifier/timeout及原506/507、双PBT和critical消费片段。对旧候选误报的纠正在原Review-R2/CORRECTION保留，我侧核收索引为`early-resume/INDEPENDENT-REVIEW-INTAKE.json`。当前整文件并行后继与实际消费切片等值分列；这些引用不授完整当前Native链接/包或实机通过。

共享runtime消费guard已由Steam层明确转交非Steam层：只在原wake/PowerReady/deferred、操作和最终归属守卫消费纯分类门，纯账户候选getter不加门。交付和实际回执核收前继续OPEN；归属以后继`early-resume/CONSUMER-OWNERSHIP-TRANSFER.json`为准，不以旧HANDOFF的Steam写入归属覆盖它。协调层、Steam层均不双写该组。


后继消费软件接线已闭合（仍非整体验收）：非Steam owner两次SSE终态后由协调层明确接续同一候选/原120秒账，完成原dual_route消费者134项、1527检查（保留128+新6）及2个单项分类语义反例；7处AND守卫、2产品/2原夹具精确CAS已入主线。原16negative仅全17变体准备通过，未声称本轮实跑；准备/编译FAIL和工具收尾原件全保留。固定602输入整TU仅替2个新头后语法通过，未链接或追赶并行main。原实际编译所需condition_variable/deque精确补入既有native-tools，旧编译器/环境不变，后置核收复用同份raw。

本段取代上一段“consumer继续OPEN”的当前状态及旧写入归属：由`early-resume/CONSUMER-SOURCE_READY.json`、`SOFTWARE-CLOSURE-AUDIT.json`和`CONSUMER-INDEPENDENT-INTAKE.json`接续。非Steam层已独立只读核原134/2/raw/产品七hunk/工具与Job范围；其初始INTAKE早于正式封存，最终pin追加确认另列。纯分类门与原phase/最终Kernel证据相与、同代未消费唤醒续接、已消费动作不退款、无硬件/InputHost或账户getter新门保持。下一自然匹配Native/Web/链接包及后续普通现场/实机仍OPEN，整体ACTIVE；未部署或真实睡眠/设备操作。


2026-11-12 全消费端稳定性后继仍 ACTIVE：前端旧代未返回任务不再串住新周期，共享 Ready/首次有效 Ready 沿原队列补偿；原生延后提交消息携带并核对原 generation。原19项按旧epoch引用，后继实际App调度体4项及首Ready反例/最终类型通过；当前默认23整父未跑。原提交体+真实Runtime选定场景16断言通过，旧源与保留phase负例准确失败，原13变体仅准备2项核对。首次缺50个实际工具输入的FAIL保留，定向声明补齐；10个自有Job关前0。新200615日志作为完整周期和同代认证断连/重连控制，不作视频健康或当前包证明。当前源码、费用、日志时钟/身份及剩余消费者清单见 `Testing/YMCC/Evidence/SharedSleepLifecycle-20261112/all-consumers/SOURCE_READY.json`、`REPORT.zh-CN.md` 和 `CONSUMER-INVENTORY.json`。本后继不接管Steam睡眠修复项目；其他native/managed/frontend消费者终审、匹配自然Native/Web/包及实机仍OPEN。


2026-11-12 消费补送后继：已提交Ready后的提交/输入消息投递失败，现在保留原generation的欠账并复用既有50ms补送计时器；新睡眠/换代/退出拒绝旧消费，输入补送不重跑已完成提交广播。原提交场景25断言及2单项负例有实际编译/图/原件，完整Native与物理Input恢复仍未验。电池实际共享InProcess原用例448断言/20周期和1负例已核，产品未改，只补原夹具、AllowUnsafeBlocks配方及5个既有测试源码闭包。两侧各3Job关前0。七项准备控制stdout未封存只记有限观察，未重跑取元数据。后继原件/费用/失败见 `all-consumers/managed-consumers/SOURCE_READY.json`、`all-consumers/native-delivery/SOURCE_READY.json` 与 `all-consumers/REPORT-SUCCESSOR.zh-CN.md`（均在Testing/YMCC/Evidence/SharedSleepLifecycle-20261112下）。其他消费者终审、性能实际队列、独立watchdog漏Resume、自然配套包及现场继续OPEN，目标ACTIVE。


2026-11-12 性能常驻后继：gamePolicyRuntime直接订阅共享准入，pending/未知/非Ready撤销旧结果并停原重试；同代Ready继续欠账、新代只接原TDP核对。原四模式实际仓库/服务/共享源各20周期（80周期）、旧源及接续负例、原inflight-stop4项已核，8Job关前0。完整Vue类型生产器退出0，但生成声明/工具路径及VueRouter类型闭包后置仍拒绝，SOURCE_READY=false，不倒填全类型/包通过；旧准备FAIL和费用保留，无已过业务重跑。原件见 `Testing/YMCC/Evidence/SharedSleepLifecycle-20261112/all-consumers/performance-power/SOURCE_READY.json` 及同目录HANDOFF.json。其他消费者和匹配自然包/现场仍OPEN，目标ACTIVE。

2026-11-12 性能类型闭包后继：同一次完整Vue类型exit0的497个实际输入已完成核收，原生成体独立重建、原lock/缓存声明与包别名全图严格校验。只续后置核收，无编译/业务重跑；性能消费者SOURCE_READY-R2已成立，旧拒绝/FAIL保持。详见 `Testing/YMCC/Evidence/SharedSleepLifecycle-20261112/all-consumers/performance-power/SOURCE_READY-R2.json`；整体ACTIVE，其他消费者/自然包/现场仍OPEN。

2026-11-12 更新入口睡眠后继：Decky updater host直接读取共享准入；安装核验期间的拒绝/换代永久作废本次请求，finally释放临时共享订阅，恢复后新确认可用。原3宿主用例及8睡眠边界、两个语义反例、497输入完整类型检查已核；旧准备FAIL保留，已过业务未重跑。1产品/1原夹具精确写回，4Job关前0，无新ID/minimum。详见 `Testing/YMCC/Evidence/SharedSleepLifecycle-20261112/all-consumers/updater-power/SOURCE_READY.json`。该消费者SOURCE_READY，整体ACTIVE，其他消费者/恢复服务/自然包/现场仍OPEN。

2026-11-12 变速清理后继：共享未准入的诊断Ready不再消耗本代唯一自动清理机会；仅两处产品源码改动。原不确定目标owner/API/adapter+实际共享源5睡眠边界、旧源反例、497输入类型已核，3Job关前0，原8其他归属用例NOT_RUN及准备FAIL保持。原件 `Testing/YMCC/Evidence/SharedSleepLifecycle-20261112/all-consumers/speed-power/SOURCE_READY.json`。整体ACTIVE；其他消费者/恢复服务/自然包/现场继续OPEN。

轮盘睡眠后继：实际公共测试偏好可启用轮盘（默认关闭），原“发布完全禁用”分类已纠正。共享pending/未准入退出会话、停止心跳；旧读取/创建/动作settings等待后不可跨同代恢复。原正常行为及8睡眠边界、两有效反例、497输入完整类型已核，1产品/1原夹具及原两ID共享源依赖已精确写回。首反例IPC端口接线不完整故拒收，修后有效旧源单列；全部失败/中断/费用和一次Job强制归还保留。见 `Testing/YMCC/Evidence/SharedSleepLifecycle-20261112/all-consumers/radial-pending/SOURCE_READY.json` 与 `REPORT-FINAL.zh-CN.md`。其他消费者/恢复服务/自然包/现场仍OPEN，整体ACTIVE。


原生输入恢复截断后继：gamepadRecoverAfterResume在原Raw Input注册、ROG有界会话等待、面键恢复、状态清理/读取之间复核原generation、Ready及退出；原WM_POWER_INPUT_RECOVER的完整恢复分支在返回、Host重挂、rescan和stats之后复核，旧结果不覆盖新owner的输入回执。原input_power_gate_r122入口新增互斥--resume-only（可--case精确选择），仍属controller.unified-standard-input，无新ID/minimum；原四profile默认正文保持。实际提取函数及原消息准入/后半段29项135断言、旧源单项及删除返回后复核的单项反例已核，390实际编译输入/7库和选择拒绝有原件。Modern Standby keep-writer、screenpad/MSI前置观察和物理Host仍未验。一次准备timeout参数FAIL、独立反例缺导入FAIL及两次编译owned Job各有1个身份未捕获的后代由本方Job强制归还，全部原件保留，所有本方Job最终0。证据为Testing/YMCC/Evidence/SharedSleepLifecycle-20261112/all-consumers/native-input-interruption/SOURCE_READY.json；仅本源码切片可交下一自然Native/包，整体ACTIVE，未部署或真实睡眠/设备操作。


2026-11-13 原生提交广播接续：WM_POWER_RESUME_COMMIT按原native generation在UI线程记录已分派步骤；每步前核Ready/当前代次/退出及Dispatch pending。pending截断保留原欠账，原50ms补送仅接未分派后半段；同代重入不嵌套执行，新代欠账等待旧栈退出。CSS/Decky/Ready事件绑定捕获generation，重复提交不重跑已分派调用。单个回调异常如实记失败，其他独立消费者继续；不自动重放可能已接受的业务，不冒称其内部自愈完成。原plugins.decky-update-recovery新增互斥--committed-delivery-only/--case定向入口，56项171断言、原pending单项及重复分派mutation单项反例已核；398实际编译输入/7库。另旧gen诊断先触传输身份拒绝而无指定断言，仅作诊断；原首编译C4505、父后置FAIL与费用保留，无业务重跑。原13变体只准备，typed/current生成体与核收器配套且语法通过，未跑整父；三本方Job自然退出且关前0。原件Testing/YMCC/Evidence/SharedSleepLifecycle-20261112/all-consumers/native-commit-continuation/SOURCE_READY.json。本次不改媒体/壁纸或设备owner；输入已排队后的pending截断、Modern Standby/物理及recovery_service新鲜唤醒证明、自然配套包和实机仍OPEN，整体ACTIVE。


2026-11-13 输入pending后继：原WM_POWER_INPUT_RECOVER完整/keep-writer分支每步核native generation、Ready、退出及Dispatch pending；原50ms补送接续未分派后半段，gamepad注册/锁等待/面键/状态进度不跨代，也不重放已调用步骤。原ROG边界在800ms等待后复核原设备/电源归属，延期请求以受保护device/power对保存，pending同代保留、换代/退出拒绝；锁内仅短内存读写，不在pending对锁内等待设备边界或做设备IO。软件Ready在同代重新提交时可按已完成进度恢复，设备/Host预算不重开。原resume-only保留29项并扩174项1007断言，后继只续14项异常/双真实waiter112断言；两份产品片段相同，默认188整父未跑。原pending旧源和误消费取消waiter两单项反例、398实际输入/7库有原件。首语法/编译FAIL及两次旧Job未知残留强制归还保持，不能冒称工具或自然退出；后继严格RawProgram核已声明vctip路径/SHA才回收，Job最终0。见Testing/YMCC/Evidence/SharedSleepLifecycle-20261112/all-consumers/native-input-pending/SOURCE_READY.json。壁纸同伴39/10及93类型输入只读限域核收见同目录MEDIA-DELEGATED-INTAKE，native foreground coherent stamp仍由其owner接续。本后继未部署或操作真实睡眠/设备；MSI真实消费/物理/完整Native与自然包/recovery_service唤醒证明仍OPEN，整体ACTIVE。


2026-11-13 Steam SourceLease对接：正式现有合同仍为48字节SharedPowerV1/只读Reader；没有已安装的取消注册器或具名public current helper。相同generation/Ready须核suspendSerial及observedNotificationSequence；持续租约必须另有不会被Ready复活的取消/寿命及注册最终复查。Dispatch receiveLocked水位存入后、closeGate推进后、生产NonReady写入及成功退出CAS为最小同步SetEvent建议点，锁内不回Reader/owner。仅明确现有事实和建议，未写hook/第二owner；共享记录Testing/YMCC/Evidence/SharedSleepLifecycle-20261112/all-consumers/POWER-SOURCE-LEASE-CONTRACT-20261113.md及native-input-pending/POWER-CONTRACT-IDENTITY.json。
