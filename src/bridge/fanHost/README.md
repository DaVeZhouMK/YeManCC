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
| 睡眠/唤醒 | ../fanHost.ts：observePowerBoundary、suspend/resume、runRearmAfterWake | 新代次撤旧写许可；补开和已Ready分支都走requireNativeWriteReadyForAdmission；真实手动救援仍沿原许可 | fan_wake_open_progress_selftest.ts、fan-explicit-off-host-chain-consumer |
| 故障自愈/关闭 | ../fanHost.ts：FAN:HEAL/CANCEL/SETTLE、disable/close | 单恢复任务；预算不被观察续期；OFF使旧写入失效；HC清理与OEM物理交还分开 | fan_control_self_heal_selftest.ts、fan_startup_cleanup_self_heal_selftest.ts |
| 状态/按钮/页面离开 | ../../views/FanView.vue：FAN:UI；../../views/fanStatusText.ts | 4秒操作/每代5秒唤醒固定保护；待恢复不是已受控；页面卸载不新建恢复任务 | fan_control_display_evidence_selftest.ts、ymcc_sim/global/ui-navigation |
| CPU诊断与恢复维护 | Host：WriteRuntimeDiagnostics / ShouldRecordRuntimeSnapshot；EmitStaOperationDiagnostic / WriteStaOperation，FAN:OBSERVE | 常态30秒／故障态1秒维护保留；稳定故障详细快照前30秒后限频，状态／代次／阶段变化或高CPU立即取证；日志关闭仍维护 | fan.source-current-runtime-maintenance；fan_bounded_thread_evidence_selftest.py；fan_heartbeat_work_binding_selftest.ps1 |
| 原生续租传输开销 | native/main.cpp：fanLeaseKeepalivePostHeartbeat / fanLeaseKeepaliveThreadProc / fanHostNoteInstanceObserved；native/fan_keepalive_transport.h | 仅复用当前held-process代次的传输父句柄；凭据/进程/port/lease/完整响应逐请求检查；失败、空闲和退出释放；实例观测借用原句柄并实时读创建时间；5秒续租/15秒TTL保持 | fan_keepalive_transport_selftest.ps1；fan_keepalive_process_selection_selftest.ps1 |
| HTTP 请求数据所有权 | native/main.cpp：reg_http | headers仅借用本次不可变参数，缺省对象按需局部构造；观察器消费完成后移入响应正文/头；连接、超时和认证规则保持 | fan_renderer_http_transport_selftest.ps1 |

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

