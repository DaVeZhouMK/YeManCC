<!-- FAN-SHARED-SLEEP-CURRENT -->
**当前睡眠恢复入口（2026-11-26）：** 风扇消费唯一 Native 睡眠协调层发布的当轮授权；原有风扇通知只作为受约束的提示。公共投递补发、主动关闭和新一轮挂起的接测，以及当前实机复测包，统一见 [Fan 任务入口](G:/YeManCC-Work/Docs/Tasks/Fan/TASK.md:2)。前端回归用 `fan_manual_wake_ipc_selftest.mjs` 读取协调层实际报文。下方 11 月 14 日的配套信息为历史快照。

休眠专项由 `tools/fan_physical_wake_selftest.ps1 -S4WireOnly` 导出原生 `S4_RETAINED_FAN_WIRE`；把其 `gate.out` 设为 `YMCC_FAN_S4_REPLAY` 后运行原前端夹具，检查 Close 前没有写入、当轮原曲线只恢复一次，以及 OFF 或新挂起压过迟到许可。生产 Host 不订阅独立的 Windows 电源事件。
<!-- /FAN-SHARED-SLEEP-CURRENT -->

<!-- FAN-REMOTE-CURRENT-JOINT-20261114 -->
当前唯一睡眠层已完成源码、r9/r10限定软件组合和现成171232完整包配对；Host db7b7e17…、payload e6d63d4a…、TS固定native-f5-deadline-v1与原F5入口一致。父死亡后迟到返/不返与ACK Unknown边界保持，不新增恢复次数/轮询。当前剩余是用这一新包验证真实后台自动恢复与物理结果；11月13日旧Host日志不能代签。见[当前配套后继](G:/YeManCC-Work/Testing/YMCC/Evidence/SharedSleepLifecycle-20261112/all-consumers/fan-consumption/matched-package-171232-r11/REPORT.zh-CN.md)。
<!-- /FAN-REMOTE-CURRENT-JOINT-20261114 -->

<!-- FAN-SHARED-SLEEP-WORKSPACE-PAYLOAD-20261114 -->
风扇恢复仍统一走唯一 Native 睡眠层。工作树随包 Host 已刷新为 candidate-fan-shared-sleep-20261114（db7b7e17…），源码归档、九成员 payload 和 Native/verifier 原 pin 已配套；普通导出会带出该载荷，不负责再次构建 Host。完整 Native/Web 配包、最终 r11 与实际 Host 的资源组合及真机仍待验证，不将旧 Win5 成功日志当新协议验收。[实际配套和未测范围](G:/YeManCC-Work/Testing/YMCC/Evidence/SharedSleepLifecycle-20261112/all-consumers/fan-consumption/workspace-payload-r8/REPORT.zh-CN.md)。
<!-- /FAN-SHARED-SLEEP-WORKSPACE-PAYLOAD-20261114 -->

# 风扇开启与能力消费：维护索引

本目录是现行代码导航；历史原因与设备证据仍在 Docs/Tasks/Fan。后续修改同步相关 `[LIFECYCLE:FAN:节点]` 注释、本索引受影响行和原验证；不在这里再复制一份历史或总库。

## 按问题读取

| 修改事项 | 首读入口 | 必须联读的边界 | 原验证入口 |
| --- | --- | --- | --- |
| Fan Host 初始化边界 | ../fanHost.ts：start/startInternal；Host：LoadAssemblyAndFactory | 初始化工厂不显式调用 HC PullSensors（后续 HC 设备事件另计）；保留机型身份、Initialize(false,false)、Open/OpenEvents、写许可和路线 writer | FanHcInitializationDiagnosticsSelfTest、fan.lifecycle-wake-display |
| 启动位置/协议/默认配置 | startupConfiguration.ts：resolveFanHostConfig | ../fanHost.ts：resolveRuntimeFromPayloadManifest、NativeFanHostLauncher.startCore；实际运行库以载荷清单为准 | tools/fan_host_path_selftest.ps1、fan_startup_ipc_boundaries_selftest.mjs |
| 旧Host退出交接/HTTP.sys | ../fanHost.ts：findExactHostOwner、recoverPreviousHost、startInternal | System PID4须原认证PID/实例＋精确映像/出生时间绑定；资源退休≠OEM物理交还。只续接原确切退出；OFF/新代取消，健康交付前的新子实例由原launcher回收 | fan_host_owned_exit_selftest.ts 原32项；fan_host_supersede_exit_selftest.ts 原4项；[范围](G:/YeManCC-Work/Testing/YMCC/Evidence/Fan-AfterSleep-FirstEnable-20261104/launcher-handoff/REPORT.zh-CN.md) |
| 部署校验/首次初始化 | deployment.ts：resolveFanHostDeployment | startCore：先采纳/结算旧实例，再ensureFanHostDeployment；记录不匹配才运行原installer | fan_host_lifecycle_selftest.ts 的 fan927-deploy 原场景、fan_payload_selftest.ps1 |
| 机型识别/能力接入 | ../../../capabilities/README.md；capabilityAdmission.ts：evaluateFanDeviceGate | Host：RealHcBackend.CapabilityRoutes/ResolveRoute/BuildFanRoutes；桥：acceptCurrentHandshake | hc_capability_catalog_generate.mjs --check、fan_host_lifecycle_selftest.ts 原握手场景 |
| 厂商profile与基础EC协议选择 | Host：InheritedEcFanWriter.SupportsRoute/Bind/InheritedRoot、GetInheritedEcFanWriter | 必须同时GenericDuty＋GenericEcControl；两个setter和整个profile回调都由同程序集IDevice声明。会话currentness先核；Lenovo WMI、GPD Mini、source9等不借继承形状切基础writer | fan.unified-capability-roundtrip原SourceOnly协议参数化；逐项范围见[本轮记录](G:/YeManCC-Work/Testing/YMCC/Evidence/Fan-InheritedEc-Protocol-Regression-20261103/REPORT.md) |
| 手动开启/重新接管 | ../fanHost.ts：start/startInternal、apply；../../views/FanView.vue：FAN:UI | 本代原生许可、Open/OpenEvents、租约、控制写入确认；旧实例退出与跨await取消检查 | fan_startup_ipc_boundaries_selftest.mjs、fan_host_owned_exit_selftest.ts、fan_host_supersede_exit_selftest.ts |
| 睡眠/唤醒 | ../fanHost.ts：readNativePowerState、resumeAfterNativeReady、observePowerBoundary／runRearmAfterWake；App：onFanResumeReady | 公共Native观察与Fan设备代次分列；UNKNOWN/pending撤授予但不伪造睡眠。迟到Ready先查当前快照，回包落后于公共代次、挂起序号或通知顺序时丢弃；公共UNKNOWN/pending时，同序回包也不授予Ready，只有更新且明确不待处理的Native观察可先进入原设备门。保留原物理唤醒/F5、Close债与同源取消，原生恢复owner持有Registration至POST/GET结算，新有效请求可重入 | fan_manual_wake_ipc_selftest.mjs；fan_physical_wake_selftest.ps1 -SharedPowerOnly／-CallersOnly；fan_power_http_transport_selftest.py --shared-owner-only／--resume-existing-only |
| 故障自愈/关闭 | ../fanHost.ts：FAN:HEAL/CANCEL/SETTLE、disable/close；Host：ScheduleRecoveryRetry | 单恢复任务；预算不被观察续期；OFF/Close撤销后续native意图接续，只有新明确ON可重新接纳；旧Timer不处置后继；HC清理与OEM物理交还分开，原已入队FIFO保持 | fan_control_self_heal_selftest.ts、fan_startup_cleanup_self_heal_selftest.ts、fan_native_intent_retirement_selftest.ts |
| 开机场景与新ON/OFF交错 | ../bootModes.ts：applyBootMode/restoreSelectedBootMode、fan消费分支 | 提交前捕获原Fan取消身份；preset逐请求归属贯穿原准入/队列/租约/重试/自愈，旧未提交请求归还新lease并保留旧已提交/新手动owner；场景Close在原队列当前归属及本地意图复核后接纳，已派发清理仍完成；start与native传输另核 | boot_modes_selftest.mjs --filter "fan scene"；[公共交错记录](G:/YeManCC-Work/Testing/YMCC/Evidence/Fan-PublicLifecycle-Storm-20261109/REPORT.zh-CN.md) |
| 状态/按钮/页面离开 | ../../views/FanView.vue：FAN:UI；../../views/fanStatusText.ts | 单按钮点击启用／确认后点击关闭；开启／恢复显示进度，保留4秒操作／每代5秒固定保护；待恢复不冒充已受控，页面卸载不新建恢复任务 | fan_control_display_evidence_selftest.ts、fan_manual_wake_ipc_selftest.mjs、ymcc_sim/global/ui-navigation |
| CPU诊断与恢复维护 | Host：WriteRuntimeDiagnostics / ShouldRecordRuntimeSnapshot；EmitStaOperationDiagnostic / WriteStaOperation，FAN:OBSERVE | 常态30秒／故障态1秒维护保留；稳定故障详细快照前30秒后限频，状态／代次／阶段变化或高CPU立即取证；日志关闭仍维护 | fan.source-current-runtime-maintenance；fan_bounded_thread_evidence_selftest.py；fan_heartbeat_work_binding_selftest.ps1 |
| CPU挤压下的STA排队 | Host：OnStaBounded / ExecuteStaWorkItem / StaWorkItem | 原15秒总期限内区分排队与实际执行；未执行过期返回409忙碌，退休精确owner，晚到回调不得执行；实际执行超时仍504、写栅栏和原返回恢复 | fan_sta_admission_selftest.ps1 -SchedulerPressure；加-ExecutingTimeout复用原执行超时；[本轮软件范围](G:/YeManCC-Work/Testing/YMCC/Evidence/CpuPressureStability-20261108/REPORT.md) |
| 温度交付被延迟 | Host：OnTemperatureMonitorSampled / OnTemperatureMonitorChanged / TemperatureWatchdogTick | 在实际接收时核原采样年龄；过期样本不能刷新健康或消耗同温抑制，下一新鲜同值样本仍可驱动 | fan_temperature_delivery_age_selftest.ps1 -DeliveryAgeOnly：原两条7250ms门延迟＋fresh边界17检查；默认及绑定组保留 |
| 原生续租传输开销 | native/main.cpp：fanLeaseKeepalivePostHeartbeat／fanLeaseKeepaliveThreadProc／fanHostNoteInstanceObserved；native/fan_keepalive_transport.h | held-process传输父句柄与凭据/port/lease检查保持；每请求绑定公共观察/Registration，UNKNOWN/pending不续旧租约，纯publication保持；失败/空闲/退出释放，5秒续租/15秒TTL不变 | fan_keepalive_transport_selftest.ps1；fan_keepalive_process_selection_selftest.ps1 |
| 页面定时续租诊断 | ../fanHost.ts：renewHeartbeat／logTimerHeartbeatSuccess；../fanApi.ts：heartbeat | `fan-api.log` 的 HTTP 回执逐次保留。`lifecycle.heartbeat-success` 定时首条及此后每 12 次确认合写一条；`confirmedTimerRenewals` 只计桥接受的本地结果，不保证写入或覆盖未满一批的尾段。手动、失败和迟到回执保持逐次。 | fan_diagnostic_isolation_selftest.mjs；fan_heartbeat_cadence_selftest.mjs |
| HTTP 请求数据所有权 | native/main.cpp：fanHttpCapturePowerAdmission／FanHttpPowerAdmission／reg_http／fanHostBoundaryHttpRequest | 唯一receiver覆盖内部凭证后进入原async/sync；旧队列/共享撤销及最终body拒绝旧成功，GET/Close等归还保持；headers借用与响应移交保持，无新硬件读取 | fan_renderer_http_transport_selftest.ps1 -ReceiptBoundaryOnly；fan_power_http_transport_selftest.py --shared-power-only／--shared-owner-only |
| 跨进程公共睡眠 scope 持有 | Host：FanRemotePowerScope／Context／ControlPower；ResumeOperationIdentity、STA与各原writer | HTTP→Channel／accepted/deferred／STA持原对象，hint只读取消事件、实际写重核Current；FanResume不升级FanWrite，child持parent；ASUS锁内复核并保留真实收据，OEM归还可达。Channel拒绝/停止归还原借用；Host认证intake／原业务和session-only、固定启动profile与Native写入／续租已主线接通；匹配包和实物仍OPEN，Dispose不合成Returned | 原FanStaAdmissionSelfTest --public-power-only 显式STA／resume／control／asus选择（PS wrapper仍仅四STA）；[队列后继与原软件范围](G:/YeManCC-Work/Testing/YMCC/Evidence/SharedSleepLifecycle-20261112/all-consumers/fan-consumption/HOST-QUEUE-PUBLIC-POWER.zh-CN.md) |

表内tools路径均相对项目根。查看实现时优先按符号定位，行号会随后续修改变化。

## 一次修改的最小阅读范围

1. 先按本表选入口，再读实际调用方与调用到的准入/提交判据；不必先通读全部历史MD。
2. 异步路径同时看成功、超时/异常、取消/代次变化、清理/退出；不能只读正常分支。
3. 追清“意图、会话就绪、写许可、写入回执、物理结果”是哪一层证明；未知不能互相代替。
4. 使用原验证并保存当前源码身份；失败先分类。旧回执、源码检查、软件夹具与实机结果分别保留。

## 能力总库接入边界

- capabilities/hc-device-catalog.json及生成物记录统一机型/alias/来源；修改源事实按既有生成器和维护合同进行，勿手改生成物。
- RealHcBackend把实际FanRoutes绑定到HcDeviceCapabilityCatalog；总库有声明，不代表有可用writer或本次写许可。
- capabilityAdmission.ts只投影Host握手。DEVICE_PROFILES仍是原GPDWin5和Xbox两类身份兼容规则（三个factory），不是全机型能力表；其它原映射分支不因此增加身份门。
- 型号归库、writer/交还归Host、当前电源/会话准入归桥、按钮显示归页面。保持原provider与手动恢复入口；不要把缺OEM物理回报当成通用永久等待条件。
- ../fanHost.ts继续提供原公共export；新叶不反向导入它，不创建另一个singleton、队列或timer。

## 固定来源风扇关闭证据

sourceFanMode 的收据由 ../fanApi.ts 校验完整布尔字段及 sourceFanGeneration 与 recoveryClaimEvidence 的同代绑定。../fanSessionEvidence.ts 分别判定终端资源关闭和自动模式归还；../fanHost.ts 的恢复、关闭和进程交接消费这两个判据。HC Close／Manager 和物理所有权仍保持各自含义。原 tools/fan_api_response_contract_selftest.ts、fan_session_evidence_selftest.ts 与 fan_exit_cleanup_selftest.ps1 覆盖缺字段、在途债务、归还失败及旧代次反例；固定九条 source 路线的实际 Host/外层 engine、公开 HTTP、正常关闭和睡眠/恢复链已完成软件联验，代码准入已接通；原授权和实时电源门保持。sourceModelClass/sourceCommit 必须与 Host 的来源身份一致，runtimeFactory 仍是 not-constructed。未部署，软件寄存器读回不代表物理效果。

## 使用中 source 故障后的重新接管（2026-11-03）

`retiredSourceSessionNeedsOpen` 只消费同一来源代次的完整自动归还/终端关闭证据。没有睡眠事务的旧 source 会话由 `completeHcOpenForAdmission` 在原生许可下重新 Open/OpenEvents；公共自动恢复与手动 apply/preset 共用此入口，不等待虚构 `/resume`。新 Open 前撤旧 Open/lease 镜像；原生等待、Open、Events 前后复核同一进程/会话/epoch，源码代次在 Open 前再核。OFF/Close/睡眠继续取消旧任务；完成的源码退休在父进程退出时由 `TrySettleCompletedSourceParentExit` 消费原完成周期，不另开预算。原45用例同ID扩展及九条source Host回归见 [本轮报告](G:/YeManCC-Work/Testing/YMCC/Evidence/Fan-X2MiniPro-InUseFailure-20261102-234039/REPORT.md)，实物初始模式漂移原因仍未证。

## 首次手动开启误报睡眠（2026-11-03）

原生 `native/fan_physical_wake.h` 保存实际Kernel506/507事实，不以Fan ON/OFF过滤wake；活动意图只控制自动publish/Host恢复，Close和实时写许可继续独立。`admitClose`锁内区分Ignored/Queued/StartWorker：仅首次新边界撤许可并通知，新代Queued也必须即时失效，同代重复不能重发suspending。桥的Ready/idle准入共用原有限native等待，队列外可取消，不延长原deadline；已成功的同意图/代次/session/曲线回执不得被前台rearm降级后重复写。原入口为 `fan_physical_wake_selftest`、`fan_native_manual_wait_selftest`、`fan_manual_wake_ipc_selftest`；[本轮源码与正反证据](G:/YeManCC-Work/Testing/YMCC/Evidence/Fan-FirstManual-SleepRefusal-20261103/REPORT.zh-CN.md)。

## 本轮范围（2026-10-20）

只移动原配置、部署记录、握手判定声明；101个原声明的实现与26个公共导出保持。Launcher、HC/native、恢复/取消、writer及按钮没有重写。当前和历史失败的具体范围见Docs/Tasks/Fan/FAN-920/FAN-1296-STARTUP-MODULE-ORGANIZATION-20261020.md；此索引不授予发布或硬件验收。


### 同题反例补查（2026-11-03）

冷Host已观察物理wake但尚欠native Close时，仍由 `requireNativeWriteReadyForAdmission` 在原有限等待内承接，不能向新Host索取旧睡眠 `/resume` 回执。`completedRecoveryWrite` 两消费点核对当前session、process和heartbeat epoch；同代新wake不得复用旧写成功。手动native ACK仅合并同意图＋代次，新明确意图可重试；本地suspended镜像即使保有wake context也须重新准入。OFF/Close、新曲线/新代次与原绝对deadline保持。原夹具扩为桥50、原生110；未知/空选择明确拒绝，未新增ID/父门/常驻轮询。原实际50输入保留；末次唯一注释差异单独重编核对。详见[正反补查交回](G:/YeManCC-Work/Testing/YMCC/Evidence/Fan-FirstManual-SleepRefusal-20261103/adversarial-continuation/REPORT.zh-CN.md)。


### 初始原生代次是身份（2026-11-04）

`native/main.cpp::initialPowerGeneration` 使用开机单调时钟，初始值不固定为1。`fanHost.ts::synchronizeObservedPowerSnapshot` 只在未见电源转换、本地未绑定、原生明确Ready／resumeReady=false／可写时建立当前初始基线；同基线重复读取保持，新代次、睡眠或只收到wake通知均撤初始标记。`fanSessionEvidence.ts` 只把完整HC会话的零power收据绑定到已确认的同一基线，不伪造Host resume，不匹配旧代次。真实显式Open证据继续独立。修改该合同先跑原 `fan_initial_awake_generation_selftest` 的实际100%派发、反向旧helper及cold入口，不以Ready/UI标签代替派发。详见[本轮原输入与正反证据](G:/YeManCC-Work/Testing/YMCC/Evidence/Fan-AfterSleep-FirstEnable-20261104/REPORT.zh-CN.md)。

<!-- FAN-CONTROL-INTENT-ADMISSION-20261104 -->
## 用户控制意图与自动开启准入（2026-11-04）
用户授权自动开启与手动点击共用原安全恢复通道；当前支持路线不以保存Version/EC附加值否决同机。首个PID-less曲线/预设保持原意图/截止并绑定确切startup进程；本地Suspended也查远端终态。Host恢复成功仍须最终原生可写，不把Host Ready或路线controlReady当写成功。原五消费者46/92/167断言/34/43软件通过，物理未验收；页面pending→侧栏明确OFF的取消门仍待分层，不改重复ON合并。详见[本轮实际正反证据](G:/YeManCC-Work/Testing/YMCC/Evidence/Fan-AfterSleep-FirstEnable-20261104/admission-forward-reverse/REPORT.zh-CN.md)。
<!-- /FAN-CONTROL-INTENT-ADMISSION-20261104 -->
