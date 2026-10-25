# 能力总库AI导航与初步检查说明

更新：2026-10-20，Asia/Hong_Kong。本指南整理现有入口，不新增运行时目录、分配器或验收门。机器可读索引：[AI-INDEX.json](AI-INDEX.json)；任务关系与数量口径：[OVERVIEW.md](OVERVIEW.md)。

## 1. 最短阅读顺序

1. 先读[当前启用范围](ACTIVE-INTEGRATION-SCOPE.json)和[用户决定](USER-SCOPE-DECISIONS.json)，确认是已启用、只登记、暂缓还是明确排除。
2. 查[接入状态](INTEGRATION-STATUS.json)和[执行路线](EXECUTION-CHAINS.json)，按domain读取实际消费者、file/anchor、归还义务、问题编号和来源限定证书。
3. 用AI-INDEX.domains找到策略输入、手写绑定和消费合同。先定具体问题再读大JSON；不要每次加载整个历史记录。
4. 按锚点查看实际消费者。总库负责描述与选择，原native/Host/后端负责设备操作和生命周期；绑定成功不代表允许写入。
5. 选择既有检查入口。实际运行时不能通过读取本指南／索引／OVERVIEW增加每帧中转。

## 2. 问题到入口

| 要处理的问题 | 首先查哪里 | 下一步 |
|---|---|---|
| 机型识别、别名或继承冲突 | hc-device-catalog.json、HcDeviceCapabilityCatalog.cs | 追sourceBaseline与冻结输入；通过原生成器更新派生表 |
| 已登记但功能没效果 | EXECUTION-CHAINS的domain消费者 | 跟到真实writer/decoder、实时准入、停止归还，不能只补注册 |
| 风扇或体感高频开销 | 对应手写绑定、缓存与实际消费函数 | 只缓存不可变选择；保留新鲜度、所有权、代次与同等处理量 |
| 手柄、背键、运动和反馈混淆 | CONTROLLER-CONSUMPTION及具体回执合同 | 外接实体来源与掌机DMI分开，原始观察/解码/写入/物理效果分开 |
| HC升级影响 | tools/hc_capability_maintenance.mjs、HC-UPGRADE-REGRESSION.md | 按受影响已启用消费选旧场景；保留故障原件与版本身份 |
| 只有方法声明或未启用副屏 | MODEL-METHOD-CONSUMPTION、ACTIVE-INTEGRATION-SCOPE | 只记录缺口，不将helper或元数据存在解释成执行授权 |
| 汇报数量 | OVERVIEW-COUNTS及其sourcePins | 分开92类/80factory、397声明、路线/规则/配方、24项软件验收 |

## 3. 运行绑定的引路表

| 范围 | 手写入口 | 可定位符号 | 真正执行及实时边界 |
|---|---|---|---|
| 机型/别名 | [HcDeviceCapabilityCatalog.cs](HcDeviceCapabilityCatalog.cs) | Find / Bind / CreateTypeCache | 实际消费者保留可用性、租约和写许可 |
| 风扇 | [FanCapabilityCatalog.cs](FanCapabilityCatalog.cs) | ResolveAsusCurve / AcquireAsusCurve | FanLab/real-host/Program.cs；归还见FAN-RETURN-CONSUMPTION |
| 体感 | [HcMotionCapabilityCatalog.h](HcMotionCapabilityCatalog.h) | bind / primaryKind / consume | native/main.cpp采样配对、新鲜度、坐标处理与最终发布 |
| OEM按键 | [HcOemInputCapabilityBinding.h](HcOemInputCapabilityBinding.h) | InputBindingCache::bind | native/main.cpp的hook/raw来源、key-up与释放 |
| 标准输入 | [HcControllerInputCapabilityCatalog.h](HcControllerInputCapabilityCatalog.h) | bindStandardInput | native/main.cpp -> InputHost/Program.cs；连接与最终提交仍实时验证 |
| 手柄体感 | [HcControllerMotionCapabilityCatalog.h](HcControllerMotionCapabilityCatalog.h) | bindModel / ModelBindingCache | native/main.cpp的Legion解码、lease/bank及校准 |
| 实体反馈 | [HcControllerFeedbackCapabilityCatalog.h](HcControllerFeedbackCapabilityCatalog.h) | bindPhysicalFeedback | native/main.cpp的实际writer、zero/stop、撤销与归还重试 |
| MSI背键配置 | [HcControllerConfigurationCapabilityCatalog.h](HcControllerConfigurationCapabilityCatalog.h) | bindRear / consumeRear | 保留精确型号/固件；native键盘路线只记录 |
| 灯光 | [HcLightingCapabilityCatalog.cs](HcLightingCapabilityCatalog.cs) | Require / IdentityMatches | native分流与FanLab/LightSetter实际写入、结束与清理 |
| 充电 | [HcBatteryCapabilityCatalog.cs](HcBatteryCapabilityCatalog.cs) | Bind / Resolve | src/views/SettingsView.vue 的 device 分区 → BatteryChargeSettings.vue → batteryCharge.ts → native/BatteryHost/Policy.cs、Controller.cs 的读回、冲突、恢复 |
| 未启用副屏 | [HcSecondaryDisplayCapabilityCatalog.cs](HcSecondaryDisplayCapabilityCatalog.cs) | 只读审查入口 | 先查范围；编译/登记不激活UI或设备写入 |

表中符号供检索。具体file/anchor维护在EXECUTION-CHAINS及AI-INDEX.domains，不把文档行号写成永久接口。新注释以AI-NAV标记定位；源文件已有的缓存和生命周期解释完整保留。

## 4. 文件按职责分列

物理路径保持现有编译与工具引用，按以下逻辑分组。AI-INDEX.files覆盖本目录全部文件，每个文件只属于一个主职责组；域映射可以交叉引用同一文件。数量快照是带日期的报告，不作为新增能力权威。

### 范围与用户决定

- [ACTIVE-INTEGRATION-SCOPE.json](ACTIVE-INTEGRATION-SCOPE.json)：本轮8类已启用能力与停止规则。
- [USER-SCOPE-DECISIONS.json](USER-SCOPE-DECISIONS.json)：用户排除及性能/接线边界。

### 接入状态与执行路线

- [EXECUTION-CHAINS.json](EXECUTION-CHAINS.json)：实际消费者、锚点、生命周期、测试引用和路线问题。
- [INTEGRATION-STATUS.json](INTEGRATION-STATUS.json)：按9域维护当前状态与来源限定证书。

### 生成输入策略

- [hc-battery-policy.json](hc-battery-policy.json)：Reviewed generator input for battery。
- [hc-controller-configuration-policy.json](hc-controller-configuration-policy.json)：Reviewed generator input for controller-configuration。
- [hc-controller-feedback-policy.json](hc-controller-feedback-policy.json)：Reviewed generator input for controller-feedback。
- [hc-controller-input-policy.json](hc-controller-input-policy.json)：Reviewed generator input for controller-input。
- [hc-controller-motion-policy.json](hc-controller-motion-policy.json)：Reviewed generator input for controller-motion。
- [hc-lighting-policy.json](hc-lighting-policy.json)：Reviewed generator input for lighting。
- [hc-model-method-policy.json](hc-model-method-policy.json)：Reviewed generator input for model-method。
- [hc-motion-policy.json](hc-motion-policy.json)：Reviewed generator input for motion。

### 派生数据快照

- [MODEL-METHOD-CONSUMPTION.json](MODEL-METHOD-CONSUMPTION.json)：397条Devices方法声明分类；不是逐方法执行证明；生成入口 tools/hc_model_method_consumption.mjs。
- [hc-device-catalog.json](hc-device-catalog.json)：冻结机型事实、别名、声明与各域派生登记；生成入口 tools/hc_capability_catalog_generate.mjs。
- [hc-performance-metadata.json](hc-performance-metadata.json)：CPU/GPU资料；当前运行消费保持独立；生成入口 tools/hc_capability_catalog_generate.mjs。

### 生成代码

- [HcBatteryCapabilityCatalog.generated.cs](HcBatteryCapabilityCatalog.generated.cs)：生成的编译表；经原生成器更新；生成入口 tools/hc_capability_catalog_generate.mjs。
- [HcControllerConfigurationCapabilityCatalog.generated.h](HcControllerConfigurationCapabilityCatalog.generated.h)：生成的编译表；经原生成器更新；生成入口 tools/hc_capability_catalog_generate.mjs。
- [HcControllerFeedbackCapabilityCatalog.generated.h](HcControllerFeedbackCapabilityCatalog.generated.h)：生成的编译表；经原生成器更新；生成入口 tools/hc_capability_catalog_generate.mjs。
- [HcControllerGameFeedbackCapabilityCatalog.generated.cs](HcControllerGameFeedbackCapabilityCatalog.generated.cs)：生成的编译表；经原生成器更新；生成入口 tools/hc_capability_catalog_generate.mjs。
- [HcControllerInputCapabilityCatalog.generated.h](HcControllerInputCapabilityCatalog.generated.h)：生成的编译表；经原生成器更新；生成入口 tools/hc_capability_catalog_generate.mjs。
- [HcControllerMotionCapabilityCatalog.generated.h](HcControllerMotionCapabilityCatalog.generated.h)：生成的编译表；经原生成器更新；生成入口 tools/hc_capability_catalog_generate.mjs。
- [HcDeviceCapabilityCatalog.generated.cs](HcDeviceCapabilityCatalog.generated.cs)：生成的编译表；经原生成器更新；生成入口 tools/hc_capability_catalog_generate.mjs。
- [HcLightingCapabilityCatalog.generated.cs](HcLightingCapabilityCatalog.generated.cs)：生成的编译表；经原生成器更新；生成入口 tools/hc_capability_catalog_generate.mjs。
- [HcMotionCapabilityCatalog.generated.h](HcMotionCapabilityCatalog.generated.h)：生成的编译表；经原生成器更新；生成入口 tools/hc_capability_catalog_generate.mjs。
- [hc-device-catalog.generated.h](hc-device-catalog.generated.h)：生成的编译表；经原生成器更新；生成入口 tools/hc_capability_catalog_generate.mjs。

### 手写运行绑定

- [AsusFanCapabilitySession.cs](AsusFanCapabilitySession.cs)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [FanCapabilityCatalog.cs](FanCapabilityCatalog.cs)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [HcBatteryCapabilityCatalog.cs](HcBatteryCapabilityCatalog.cs)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [HcControllerConfigurationCapabilityCatalog.h](HcControllerConfigurationCapabilityCatalog.h)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [HcControllerFeedbackCapabilityCatalog.h](HcControllerFeedbackCapabilityCatalog.h)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [HcControllerInputCapabilityCatalog.h](HcControllerInputCapabilityCatalog.h)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [HcControllerMotionCapabilityCatalog.h](HcControllerMotionCapabilityCatalog.h)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [HcDeviceCapabilityCatalog.cs](HcDeviceCapabilityCatalog.cs)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [HcLightingCapabilityCatalog.cs](HcLightingCapabilityCatalog.cs)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [HcMotionCapabilityCatalog.h](HcMotionCapabilityCatalog.h)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [HcOemInputCapabilityBinding.h](HcOemInputCapabilityBinding.h)：手写运行绑定；查文件顶部AI-NAV与对应域。
- [HcSecondaryDisplayCapabilityCatalog.cs](HcSecondaryDisplayCapabilityCatalog.cs)：手写运行绑定；查文件顶部AI-NAV与对应域。

### 消费与回执合同

- [CONTROLLER-CONSUMPTION.json](CONTROLLER-CONSUMPTION.json)：手柄输入/运动/配置/反馈/屏蔽的分阶段合同。
- [CONTROLLER-GAME-MOTOR-RECEIPT.json](CONTROLLER-GAME-MOTOR-RECEIPT.json)：游戏电机请求到物理writer的软件回执。
- [CONTROLLER-GAME-OUTPUT-OBSERVATION.json](CONTROLLER-GAME-OUTPUT-OBSERVATION.json)：Host原始游戏输出观察与归属。
- [CONTROLLER-STEAMDECK-RUMBLE-RECEIPT.json](CONTROLLER-STEAMDECK-RUMBLE-RECEIPT.json)：既有SteamDeck经典震动回执。
- [CONTROLLER-XBOX-XINPUT-RUMBLE-RECEIPT.json](CONTROLLER-XBOX-XINPUT-RUMBLE-RECEIPT.json)：既有Xbox/XInput震动回执。
- [FAN-RETURN-CONSUMPTION.json](FAN-RETURN-CONSUMPTION.json)：风扇归还、释放义务及历史/当前证据边界。
- [SECONDARY-DISPLAY-CONSUMPTION.json](SECONDARY-DISPLAY-CONSUMPTION.json)：未启用副屏的保留入口与禁止激活边界。

### 维护入口说明

- [AI-GUIDE.md](AI-GUIDE.md)：按维护问题定位文件、符号与检查方式。
- [AI-INDEX.json](AI-INDEX.json)：可检索的文件分组、域入口与工作流；不是运行时分配表。
- [OVERVIEW.md](OVERVIEW.md)：任务关系、类型数量和验收边界。
- [README.md](README.md)：总库入口和历史记录；先读顶部AI导航。

### 文档数量快照

- [OVERVIEW-COUNTS.json](OVERVIEW-COUNTS.json)：2026-10-20数量与来源哈希；数字更新需重读其sourcePins。

## 5. 生成与修改边界

- 手写绑定（runtime-binding）可以维护逻辑和注释；*.generated.cs、*.generated.h及派生catalog/performance元数据通过原生成器维护。
- MODEL-METHOD-CONSUMPTION由独立方法提取器导出registry.json；审查后再按原流程采用。它分类声明，不逐方法证明已执行。
- hc-*-policy.json是经审查的输入。改策略须同时检查实际消费者；生成成功不自动启用新功能。
- JSON保持合法JSON，不塞行内注释。AI解释放Markdown、手写C#/C++注释及文档索引，不污染数据schema。
- 总生成器可能更新native/main.cpp和灯光生成部分等目录外输出；先看tools/hc_capability_catalog_generate.mjs，不把整次生成视为“只改capabilities”。共享代码遵循原单写负责人。
- 本次整理不移动文件、不改签B40旧回执。新增注释会改变源码字节；保留原来源证据，下次适用性审计按实际输入处理。

## 6. 检查方式及其证明范围

以下命令从仓库根目录执行，OUT替换为源码树外的新证据目录。它们是现有可选工具入口，按任务选用，不要求每次说明修改都编译或全量复跑。

只读引用与已启用范围计划：

```text
node tools/hc_capability_maintenance.mjs --out OUT
node tools/hc_capability_maintenance.mjs --out OUT --candidate-catalog REVIEWED_CANDIDATE_JSON
```

这检查原消费者锚点、冻结输入、问题记录和候选差异，并生成计划；不运行实际产品功能，不加载HC。无candidate时也不证明升级后全部正常。

确需核对生成一致性时使用：

```text
node tools/hc_capability_catalog_generate.mjs --check --out OUT
node tools/hc_model_method_consumption.mjs --check --out OUT
pwsh -NoProfile -File tools/verify-unified-capability-catalog.ps1 -EvidenceDir OUT
```

这些入口会构建Roslyn提取工具／核对派生结果；不是零成本文件浏览，也不是硬件验收。最后一个调用既有生成检查和维护检查，不必把三条全都重复跑一遍。

真实消费回归使用EXECUTION-CHAINS中的原ID与tools/ymcc_sim/HC-UPGRADE-REGRESSION.md。索引中的existingTestReferences只是导航；未启用副屏、历史全局引用和未触发依赖门不因此被自动加入本轮执行计划。原runtime/ABI/profile变化门、UI导出门、Host启动与物理权限保持。

## 7. 本轮初步检查

整理前，既有hc_capability_maintenance只读检查PASS：9域、16条路线问题记录引用有效。整理后核对JSON、全文件分类、路径／锚点、README导航、注释前后原字节与生成文件不变，并重用同一维护工具做一次整理后引用检查。该检查说明导航和既有引用一致，不新增功能通过、当前整树通过、实机通过或CPU百分比。


充电入口维护（2026-10-22）：总库能匹配机型、独立组件测试通过，并不能证明父页面仍挂载消费组件。本次恢复 SettingsView 的设备分区，按活动分区挂载 BatteryChargeSettings。全页门必须包含 `settings/battery-charge`、`/settings/device`；`templates/batterySettingsCases.mjs` 执行真实父页→组件→bridge，删除父页组件在 `SETTINGS_CHARGE_ENTRY_MISSING` 失败。硬件协议仍由原 BatteryHost 管理，G3E 继续使用 Get_AP/Set_AP；不要为修复页面断线修改阈值编码。候选交付同时检查 battery-host.payload.json、Host --identity 与 native/battery_host_contract.h 的 sourceId，重建后核对全部载荷字节。UI模拟、原生传输模拟、候选文件与实机固件验收分开记录。
