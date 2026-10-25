# 风扇开启与能力消费：维护索引

本目录是现行代码导航；历史原因与设备证据仍在 Docs/Tasks/Fan。后续修改同步相关 `[LIFECYCLE:FAN:节点]` 注释、本索引受影响行和原验证；不在这里再复制一份历史或总库。

## 按问题读取

| 修改事项 | 首读入口 | 必须联读的边界 | 原验证入口 |
| --- | --- | --- | --- |
| Fan Host 初始化边界 | ../fanHost.ts：start/startInternal；Host：LoadAssemblyAndFactory | 初始化工厂不显式调用 HC PullSensors（后续 HC 设备事件另计）；保留机型身份、Initialize(false,false)、Open/OpenEvents、写许可和路线 writer | FanHcInitializationDiagnosticsSelfTest、fan.lifecycle-wake-display |
| 启动位置/协议/默认配置 | startupConfiguration.ts：resolveFanHostConfig | ../fanHost.ts：resolveRuntimeFromPayloadManifest、NativeFanHostLauncher.startCore；实际运行库以载荷清单为准 | tools/fan_host_path_selftest.ps1、fan_startup_ipc_boundaries_selftest.mjs |
| 部署校验/首次初始化 | deployment.ts：resolveFanHostDeployment | startCore：先采纳/结算旧实例，再ensureFanHostDeployment；记录不匹配才运行原installer | fan_host_lifecycle_selftest.ts 的 fan927-deploy 原场景、fan_payload_selftest.ps1 |
| 机型识别/能力接入 | ../../../capabilities/README.md；capabilityAdmission.ts：evaluateFanDeviceGate | Host：RealHcBackend.CapabilityRoutes/ResolveRoute/BuildFanRoutes；桥：acceptCurrentHandshake | hc_capability_catalog_generate.mjs --check、fan_host_lifecycle_selftest.ts 原握手场景 |
| 手动开启/重新接管 | ../fanHost.ts：start/startInternal、apply；../../views/FanView.vue：FAN:UI | 本代原生许可、Open/OpenEvents、租约、控制写入确认；旧实例退出与跨await取消检查 | fan_startup_ipc_boundaries_selftest.mjs、fan_host_owned_exit_selftest.ts、fan_host_supersede_exit_selftest.ts |
| 睡眠/唤醒 | ../fanHost.ts：observePowerBoundary、suspend/resume、runRearmAfterWake | 新代次撤旧写许可；补开和已Ready分支都走requireNativeWriteReadyForAdmission；真实手动救援仍沿原许可 | fan_wake_open_progress_selftest.ts、fan-explicit-off-host-chain-consumer |
| 故障自愈/关闭 | ../fanHost.ts：FAN:HEAL/CANCEL/SETTLE、disable/close | 单恢复任务；预算不被观察续期；OFF使旧写入失效；HC清理与OEM物理交还分开 | fan_control_self_heal_selftest.ts、fan_startup_cleanup_self_heal_selftest.ts |
| 状态/按钮/页面离开 | ../../views/FanView.vue：FAN:UI；../../views/fanStatusText.ts | 4秒操作/每代5秒唤醒固定保护；待恢复不是已受控；页面卸载不新建恢复任务 | fan_control_display_evidence_selftest.ts、ymcc_sim/global/ui-navigation |

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

## 本轮范围（2026-10-20）

只移动原配置、部署记录、握手判定声明；101个原声明的实现与26个公共导出保持。Launcher、HC/native、恢复/取消、writer及按钮没有重写。当前和历史失败的具体范围见Docs/Tasks/Fan/FAN-920/FAN-1296-STARTUP-MODULE-ORGANIZATION-20261020.md；此索引不授予发布或硬件验收。
