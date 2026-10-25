# HC 更新时复用的双向回归合同

本文件适用于继续修复 HC 精简后的连接缺口、维护当前锁定版本，以及将来评估新的 HC 候选。既有 `tools/ymcc_sim` 是统一环境；本机运行 AI 和软件模拟，掌机提供正常版本日志。

每个确认的问题同时补正式源码和测试。正式链路已经正确的地方保留实现，让回归直接执行它。源码关联、接口名字匹配、程序集构造、模拟读写和物理读写是不同证据，不能互相代替。

## 固定的可复用链路

| 范围 | 产品链路 | 软件回归入口 |
| --- | --- | --- |
| 风扇 | `HttpFanApiAdapter` → `FanHostLifecycle` → 状态、租约、关闭/归还 | `global/consumer/fan-signal-consumer.mjs` |
| 输入/OEM | 正式 WebView IPC → `controllerShortcutRuntime` → 动作队列 → 生产 C++ mailbox → 正式反馈消费者 | `global/input-signals/input-signal-consumer.mjs` |
| 运行库 | 正式共享依赖 resolver → `RuntimeDependencyContract` → 实际 CLR 加载和初始化失败诊断 | `global/runtime-connectivity/runtime-connectivity.mjs` |
| 独立共享依赖 | 正式 InputHost/BatteryHost → 实际 Compile 链接 → 无 HC 目录发布 → 共享 resolver 与拒绝策略 | `global/shared-runtime-no-hc/run.mjs`（`lifecycle.shared-runtime-no-hc`） |
| 灯光 | 正式 LightSetter → DeviceCore/HC 适配 → 严格回执；实际 Vue 组件显示成功/失败 | `global/lighting-signals/index.mjs` |

各入口输出实际执行模块、源码哈希、输入/消费/状态/反馈证据、兼容差异及剩余缺口。系统 IO 和设备边界采用明确的内存或回放实现；物理信号和硬件恢复没有证据时保持未验收。

`global/cli.mjs --mode signal-check` 将现有 ZIP 白名单字段送入固定风扇和输入消费者。它不允许日志指定代码、命令或加载路径。原档字段标记 `archive-literal`；按源码合同补全的入口/请求单列 `source-derived-fixture`。逐成员保留顺序，不推断跨文件因果，不按历史墙钟等待若干天。

OEM immediate/settled 是只读观察。`actionDispatchAccepted=true` 不能推出原厂动作已经屏蔽；`defaultActionSuppressionVerified=false` 和缺物理写入回执必须保留。故障态曾发生硬件写入的证据也不能因握手失败被清除。

## 一轮缺口如何闭环

1. 找到最后有效发送方、实际消息结构和消费入口，记录静态推导与旧运行证据的区别。
2. 用同样输入执行真实产品逻辑，定位 input、consumer、state 或 feedback 的具体断点。
3. 保留修复前失败；在生产调用链补最小修复，同时增加能抓住原问题的负例。
4. 重新执行修复后正反例，绑定当前源码/产物哈希；缺回执、超时、接口不兼容和运行中源码变化不能通过。
5. 跑统一 global 回归。UI 主线改动还须实际正式组件及正式手柄 engine 两种尺寸的导航验收。发布前再核验当次所测源码与产物；安装和发布沿用各自验收门。

## 未启用入口的回归边界

副屏继续保持产品未启用。复用 `node tools/hc_inactive_entry_guard_selftest.mjs --out <新证据目录>`，从当次实际main抽取请求与按键两个完整消费者以及实际false常量，验证在电源查询、租约、管道、队列和日志前返回。两种配置各56种调用及两个移除门故障只检查停用边界，不执行副屏协议或设备效果，不恢复D15R2。OEM调用点仅作源码结构核对，不声称运行完整OEM循环。

该入口目前是现有tools内可复用的桌面检查，尚未登记成global独立测试。源码变化须重新核对，不能因为两个函数仍相同就把旧整main回执判为当前有效。

## 并行修改时执行固定源码版本

长回归从明确的固定源码版本运行，主线继续修复。固定副本应与原repo同父目录，保留现有workspace历史输入路径；根InputHost是真实工程，必须保留源码和项目，PowerControl混合树保留代码/脚本/项目/package.json而不读取或复制运行数据。依赖与生成目录的明确排除写进manifest，不能据目录名把生产代码排掉。

创建器的FAIL原件不得改签。若原before、实际完整副本和savedSnapshot所有文件/目录/完整main精确闭合，而primary随后变化，可由只读verifyCapturedSourceSnapshot在调用者pin的manifest SHA下另证明FROZEN_SOURCE_VERIFIED；creationStatus保留原值，isCurrent和sourceApprovalGranted单列。复制未完成、任何副本字节变化、遗漏路径或引用身份不符，都不具备固定版本执行资格。旧compareSourceSnapshot依旧只接受其原成功创建路径。

公开入口需要TypeScript、Vue编译器和esbuild。node_modules不作为源码整包复制；snapshot-node-tools在普通的副本node_modules目录中为明确的外部工具建立别名，包图、完整成员与SHA另存绑定回执，预声明工具不冒称动态加载图。默认绑定仍要求成功创建；使用历史固定版本必须显式snapshot-only并通过上述完整副本核验，不能将工具PASS解释为当前源码通过。

必须从副本里的ymcc_sim公开入口、CLI、adapter和producer整链执行，不可只给原G producer改source参数。每轮外层记录原snapshot manifest SHA、工具binding manifest SHA及核验器代码SHA。原39故障、双配置、80实际编译追踪、矩阵补充、输出完整性和预算保持；缺子例或提前退出不能放行。

运行结束后，使用副本入口对同一原件做audit，并分别复核整个副本、全部外部工具包、当前primary每个纳入路径及完整main。固定版本业务通过、工具守恒、当前源适用性和实机效果是四项不同结果；primary任一相关字节或路径变化即不可借旧结果批准当前源。已有安装包与日志流程不因该工具自动部署或变更。

## 评估 HC 候选的顺序

保留当前锁定基线和失败证据。将候选放入独立目录，传入实际 runtime、shared runtime、Host payload 根及身份，不以目录名或版本字符串替代真实加载身份。不要把当前运行库覆盖后再补证据。

运行库入口支持显式候选目录与旧回执：

```powershell
node tools/ymcc_sim/global/runtime-connectivity/runtime-connectivity.mjs `
  --runtime-root C:/YMCC-Candidates/PowerControl/handheldcompanion-runtime/CandidateRuntime `
  --shared-runtime-root C:/YMCC-Candidates/PowerControl/ymcc-shared-runtime `
  --host-payload-root C:/YMCC-Candidates/PowerControl/fan-host `
  --evidence-dir C:/YMCC-Evidence/CandidateRuntime `
  --baseline-result C:/YMCC-Evidence/BaselineRuntime/result.json
```

这是目录参数示例，需要换成真实候选输入。共享依赖 resolver 要求保留 `PowerControl/handheldcompanion-runtime/<运行库>` 与同级 `PowerControl/ymcc-shared-runtime` 的正式目录结构。版本、程序集身份、共享依赖或必需 API 的差异会阻断直接沿用旧验收；身份变化不自动证明功能回归，必须逐项审查实际差异。当前批准 profile、pin 和 ABI 合同仍有约束，候选变更需要更新对应批准输入并重新验证。不得把缺库或缺 CLR 的结果替换为 mock 成功。

再用既有真实管理器构造、ABI 正负门和 mock Host 生命周期检查候选：

```powershell
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain fan -Mode connect `
  -RuntimeRoot C:/YMCC-Candidates/PowerControl/handheldcompanion-runtime/CandidateRuntime `
  -SharedRuntimeRoot C:/YMCC-Candidates/PowerControl/ymcc-shared-runtime `
  -HostPayloadRoot C:/YMCC-Candidates/PowerControl/fan-host `
  -EvidenceDir C:/YMCC-Evidence/CandidateFan
```

复用原风扇、输入、灯光信号场景与正常版本日志，再执行完整已登记回归：

```powershell
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run `
  -Profile full -PayloadRoot C:/YMCC-Candidates/PowerControl/fan-host `
  -RuntimeRoot C:/YMCC-Candidates/PowerControl/handheldcompanion-runtime/CandidateRuntime `
  -SharedRuntimeRoot C:/YMCC-Candidates/PowerControl/ymcc-shared-runtime `
  -LightingPayloadRoot C:/YMCC-Candidates/PowerControl/light-setter `
  -ExpectedHelperSha256 $reviewedLightingSha256 `
  -EvidenceDir C:/YMCC-Evidence/GlobalCandidate
```

完整 global 回归中的运行库负例默认独立检查当前生产目录，并重建正式源码；评估外部候选时，前两个显式目录命令的候选回执也必须保留。不要把默认目录检查当成外部候选已验证。

未来 HC 真正改变 ABI、协议或能力时仍须改适配并补对应场景；保留的是统一环境、稳定信号合同、已有场景和差异回执，不是保证所有未来版本无需维护。真实 EC/HID/固件、物理转速/灯光效果及真实系统睡眠证据保持单列。

## 当前验收边界

完整 profile 有未覆盖功能入口时返回 `INCOMPLETE_COVERAGE`，即使所有已登记测试通过。已登记测试的通过不代表整个 YMCC 或每个机型已经通过。全局覆盖缺口继续由 `coverage-work-items.json` 跟踪。

本机软件模拟不启动真实 OS 睡眠，不冻结用户进程，不写真实 EC/OEM/HID/驱动，也不为掌机制作独立验证包。工具不会通过取消产品安全门或伪造设置来补足证据。

全页导航的可维护入口是 `global/ui-navigation/Run-Ui.ps1`（global适配器 `controller.ui-navigation`），复用既有真实组件与正式engine夹具；每轮证据目录独立并核对当前源码。维护过程不需要寻找历史任务目录。

并行修改后的旧回执使用global `-Mode audit -Receipts <旧轮>,<后续轮>` 只读核对，得到具体 `needsRerun` 清单。审计不运行测试，不替代候选DLL实际加载/ABI门。正式源码、测试与引用角色分别记录；改动后仅重跑真实受影响的用例，候选发布仍核对候选本身的源码和载荷。
## 普通程序运行时的睡眠软件回归

统一入口新增 `sleep.engine-memory` 和 `sleep.engine-http-all`。两者只加载 `-PayloadRoot` 明确指定的 Host DLL，运行前固定 Host 与 Backend 身份；不调用生产 Main，不订阅 OS 电源事件，不获取或取消生产 Host 机器锁。memory 执行真实 FanHostEngine 的内存方法；http-all 执行真实 LocalFanHostServer→FanHostEngine，forms/typeseries/rapidwake 各使用新进程、新令牌和端口，保留既有夹具的隔离边界。严格 mock 下 realBackend 必须为 null，HC 未加载，目标 native 尝试数为零，物理写入标志为 false。

```powershell
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run `
  -Profile full -Tests sleep.engine-memory,sleep.engine-http-all `
  -PayloadRoot C:/YMCC-Candidates/PowerControl/fan-host `
  -EvidenceDir C:/YMCC-Evidence/SleepEngineCandidate
```

原 `sleep.host-forms/host-typeseries/host-rapidwake` 仍运行 Host Main 的启动与退出集成，继续要求独占窗口；软件入口不替代这层验收。带曲线恢复可能按合同进入 FaultLocked/409，必须记录具体状态与错误，不能解释成恢复成功。真实 S0/S3/S4 硬件语义及 OS 睡眠仍未据此验收。

新全局回执除 Host EXE/DLL 外也记录 Backend、SDK、WinRT 文件身份；只读 audit 会使已记录文件的变化过期。历史仅记录部分文件的回执按原范围解释，不据此补认未记录依赖已通过。实际 HC 加载、管理器初始化和 ABI 门继续使用前述 fan connect 候选回执。
## 保留的完整运行库签名缺口

2026-10-19 当前候选的全量签名诊断仍为 `failed-existing-signatures`（exit 1）：1768 types、12516 signatures，19项既有缺口，候选新增0项。分类与当次runtime manifest绑定：18项不在当前Host/灯光受限消费者闭包；Notifications相关1项在保守闭包命中，只在没有非initializing的LayoutMode写入、不执行DesktopLayoutCommands.Execute、不启动HC UI服务的合同下保留。它不构成完整HC应用通过，也不能把19项全部归成UI无关。新增桌面布局、宏或HC服务消费者前须恢复对应依赖、重算闭包并重跑正反例。详细证据保存在本轮交付的“运行库既有签名缺口.json”；升级候选应重新生成诊断并按实际manifest比较，不能照搬这个数字作为新候选结论。

## 普通依赖与新增能力的复用

当前生成器、运行库校验器和包校验器只允许具名HC/SDK/WPF/SDL五个特殊DLL使用获批导数，其他实际77个普通DLL逐个对照锁定上游原件。2026-10-19恢复后普通原件记录34份；数字是当次候选，不可代替后来版本的真实清单。破坏反例故意让裁剪WindowsInput、profile、manifest自洽，三个入口仍应拒绝；新候选同时保留该反例与真实运行门。

全局新增电池、风扇生命周期/唤醒显示、OEM诊断/键盘、背景消费入口，均在同一tools/ymcc_sim内。电池的组件请求进入当前正式dispatch和Controller，只有IO端口是夹具；背景和键盘缺口同时修正式源码并保留失败。统一用例名和范围见GLOBAL-TESTING.md；不需要另找一次性Build目录。

更新某个helper时，程序集纯方法探针、设备Open/Close副作用边界、实际业务请求和物理验收分别记录。纯34项探针通过不证明任意机型Open没有电源/TDP副作用；具体owner、方法体和调用方式变化应补正式接线与负例。后续UI/native并行修改使旧源快照过期时，不用它阻挡已独立验证的运行库身份，也不能据后者补认新UI通过。


## 显式候选必须真正进入消费者

统一入口现在支持 `-RuntimeRoot`、`-SharedRuntimeRoot`、`-LightingPayloadRoot`、`-ExpectedHelperSha256`、`-HcProfile` 和 `-HcVersion`；对应 Node 参数使用小写连字符。`-PayloadRoot` 继续指定风扇 Host，灯光使用独立的 `-LightingPayloadRoot`，两者可以来自同一候选 PowerControl 中不同子目录。`$reviewedLightingSha256` 应来自已审查候选身份，不要在验收时把任意当前文件自动当成批准候选。HC profile 参数是受消费者批准的名称，版本标签不授予加载许可；ABI/profile/正式 pin 的改变仍须审查适配。

global 的 `candidate-options.mjs` 将参数明确传给现有风扇连接和灯光消费者。灯光 root 与审查 hash 必须成对指定；source-only 回归仍可不传候选。`candidateInputs` 记录本轮输入，灯光 `compatibility.payloadCandidate` 必须显示同一路径、预期/实际 hash 及实际候选方法回归。只有重建助手而没有 payloadCandidate 的结果，不能称作指定安装 DLL 已验证。

显式 runtime/shared 目录的 manifest 与顶层 DLL、Host 必需载荷以及灯光候选 DLL 在执行前后记录为 `candidateArtifacts`。缺文件提前 BLOCKED；运行中变化为 FAIL；`audit` 重新计算这些明确文件的 hash，替换/丢失时要求重验。这个只读重审不执行 CLR/ABI，也不取代完整运行库清单、来源原件策略或新增文件的准入门。

2026-10-19 保留的入口反例：旧统一 CLI 拒绝 lighting-payload-root；补接后实际候选方法能够执行。另用真实子进程验证参数不丢失、root/pin缺一拒绝、候选变化不能被退出码或旧PASS遮蔽。原先带 addb 名称的 source-only global 结果仍属源构建证据；安装候选与构建助手的身份分列。

设备生命周期需要按实际继承链验收 Open、OpenEvents 和 Close。MSI通用Open含功耗/模式/ROM行为，OneX通用Open含OpenLibSys与EC写；其它能力的窄写入不能证明初始化已隔离。旧候选在新增确认的合同下失败时保留原证据及新的拒绝结果，禁止用旧场景数量或父类简化模型放行。


修前风扇失败对照与待交付候选使用不同输入。`-PayloadRoot` / `--payload-root` 继续指定当前候选Host；`-LegacyHostRoot` / `--legacy-host-root` 仅覆盖风扇Open专项固定的旧坏对照，禁止把新版候选送进旧5e身份门。该专项仍从本次正式源构建新Host，在完整继承模型和惰性叶IO上检查旧坏与新修复行为。旧对照应来自仓库固定夹具及来源清单，不能依赖已经更新的C安装或一次性历史工作目录。缺失基线保持BLOCKED。

`lifecycle.runtime-connectivity` 同样接收当前runtime/shared/Host候选根；不再把默认生产目录结果当作显式外部候选结果。实际输入记录、执行模块、DLL身份和来源重审共同说明范围：构建/模拟、实际加载及ABI、安装回读、物理设备分别验收。当前68项通过是软件分项回执及精确来源通过，不代表全部产品功能、每种机型或CPU功耗优化完成。


安装绑定须单独检查，不由软件候选PASS代替。2026-10-19实际发现runtime manifest6389与安装update-manifest旧85d75不一致，并发现候选授权说明仍绑定旧Host5e。对应正式工具为 `tools/hc-device-runtime/connect-installed-components.ps1` 的 `host-only-v1` 六文件范围，`verify-installed-host-binding.ps1` 同时核对Host/Backend、授权说明、payload、HC/runtime、update和export索引；`test-host-connection.ps1` 在自有普通文件副本上执行同一个连接器，保留旧绑定拒绝与第二文件被占用时已替换Host实际回滚的反例。

此安装链检查须绑定对应计划、候选及安装回读证据，不能用重新算hash的自洽清单冒充正确更新绑定。它不启动真实后端、不查看用户session/token，不取代运行库五阶段或物理设备验收。正常连接与失败回滚分别保留回执；兼容性执行线尚在进行的安装，不提前写为通过。


第三轮已将专用显存设置、屏幕触摸、HUD输出和更新预下载纳入同一个global入口。新增场景沿正式发送、消费、状态、反馈执行，复用原断言，仍禁止真实硬件/系统睡眠及掌机独立验证包。原测试的陈旧展示元数据若与当前文档/正式消费者矛盾，须先保留完整原失败，再作有界迁移，记录唯一修改及拒绝负例；不能排除已知失败而宣称该整套通过。

源重建程序集与实际安装候选身份不能混用。最新OneX灯光安装候选e546分别有47场景、实际DLL177检查、MSI6/X1传输8/首次零值缓存3/Fly-Apex8的PE合同；productionHelper的另一次源构建0b42保持单列，旧105f/bdeb不能批准新安装源。

单项能力通过不能证明跨消费者共存。下一轮仍需对共享端口、设备所有权、工作线程/租约、睡眠/退出和失败归还做组合场景；例如OneX风扇通用Open与一次性灯光串口会话的共享端口候选，已用精确Host7f/helpere546及按HC PE核对的完整继承模型在共享排他内存端口复现（7场景/56检查/零native尝试）；实际设备未观察。下一轮须同步修正式fan-only生命周期和共存/归还回归，不能借单项PASS批准尚未修复的新组合。


每轮升级的已登记回归汇总应使用global audit的 `-AuditRegistered -Profile full`，与本轮实际执行的baseline/后续定向回执一起核对。新注册能力若没有执行证据，必须列NOT_RUN并要求补跑；不能仅凭旧轮全部PASS就批准新登记项。注册完整性与完整产品覆盖/实机验收仍分开，部分来源审计可继续不用此开关。


风扇归还不能只按无异常的void调用认定软件成功。已核对OneXAOKZOE.SetFanControl会忽略IDevice.ECRamDirectWriteByte的bool，而后者可在端口异常时catch后返回false；专项必须执行完整生产方法＋可计数叶IO，保留该原失败。新窄归还若消费同一个EC接口bool，仍只证明软件请求结果，不能据此声称固件实际接受或实际转速已恢复。写失败、配置退出异常、UseOpenLib/DeviceOpen丢失和Dispose异常分别验证债务/句柄保留；经理清理也失败时原写入错误不能被HC_CLOSE_PENDING覆盖。

## 第六轮：机型消费者、完整体感反例与接通计划分派（2026-10-19）

机型软件消费者使用现有 `controller.machine-model-capabilities`；完整原生体感消费者使用同一 global 的 `gyro.machine-motion-consumers`。后一项必须从当前正式源码提取并运行 Release/Checked 正向与固定35组实编译语义故障，每组两种配置都须成功编译后实际断言失败。编译、提取、输入缺失、源码拒绝或仅生成代码不能当作故障检出。原114560条消费者断言作为独立小计保留，评审资源与通用回退新增断言另列，不能靠重写总数掩盖删减。

通用Windows体感owner不改变Unknown机型身份。通用回退反例必须执行真正的能力分类、回执与日志消费者；不能进入跳过该网格的consumer-only模式。原失败、旧合同、原始字节哈希与CRLF仅换行比较分别保留。数据依赖与实际导入的代码、运行器和Windows Job helper都须登记到来源围栏，缺失保持BLOCKED。35组专用编译采用840秒producer/900秒所属任务预算；普通JS单项60秒规则保持。

机型能力矩阵是逐型号的需求与证据索引，不是已经实现的全机型runtime。归档参考新增9个风扇路由缺口须在对应候选中逐项补正式生命周期与模拟，不误报为当前锁定runtime的故障。旧基线应与HC候选分开保留。桌面固定输入快照可按清单找回精确历史字节；它不含运行DLL/应用，不是掌机专用验证包，也不把仍需原路径和现有工具链的环境宣称为完全可移植。

接通工具只接受大小写精确的标量 `host-only-v1`、`host-only-v2`；缺少kind、未知kind或非字符串先拒绝，不能落入另一组替换范围。v1仅保留独立证据固定的历史7f/f8及完整12成员Host/runtime元组，计划自报“旧版本”不能扩大集合。当前及以后其它候选必须走v2的精确能力证据门，不能改kind、删证据或伪造历史声明绕过。

已有接通工具的回归入口保持：

```powershell
pwsh -NoProfile -File tools/hc-device-runtime/Test-HostCapabilityEvidence.ps1 -PlanPath <精确候选计划.json> -EvidenceRoot <新的能力回归目录>
pwsh -NoProfile -File tools/hc-device-runtime/test-host-connection.ps1 -PlanPath <精确候选计划.json> -EvidenceRoot <新的接通事务回归目录>
```

两者按明确计划在普通文件副本上检查原成功、实际第二文件占用时已替换Host的回滚、临时文件清理、保留成员、缺能力、降级kind、错误类型，以及历史12成员逐项破坏。错误收据的空集合必须是真实空数组，不能用 `[null]` 代替；原失败和最终定向回执分别留档。当前固定样本为39能力检查、新v2事务28检查、历史v1事务49检查；数量只是该版样本说明，必须核对执行和来源，不能以数量代替实质断言。

正式安装按对应任务已经获得的直接用户授权、精确写集和实际窗口推进。本模拟器任务的“不写安装目录”范围不是对其它授权任务的全局禁令；无写集/资源冲突的纯内存编译回归不应成为接通的等待节点。执行者仍须在实际替换时重验进程、互斥锁、当前原件、保留成员、候选和回滚。软件回归、接通回读、完整发布及物理验收分列，原AGENTS要求的UI/手柄与Decky发布门按适用范围继续执行，不由本节豁免。


## 正常发行源也属于双向闭环

`lifecycle.fan-package-source-continuity` 复用现有 `fan_promotion_gate_selftest.ps1 -Mode PackageSource`。显式执行真实 `Assert-PublishPreGate`、主线载荷校验器和正式复制分支；不进入兼容的LegacyFull Host启动分支，不运行完整打包，也不读会话/租约。场景包含三组新载荷实际复制、旧7f自洽载荷拒绝、错DLL/后端/默认manifest、额外mock旗标、构建源缺失/变化/新增拒绝以及bin/obj生成源正控。项目以 `Compile Include` / `Link` 引入目录外源码时，同样须有逐文件缺失与变化反例，并在实际发布门和模拟器的来源记录中登记。

正式修复除了可运行安装位，还须同步normal package实际消费的canonical源载荷、当前批准校验身份和发行默认绑定。本轮已确认build-workspace刻意跳过Host构建、package-release复制PowerControl/fan-host，不能把安装C中的新DLL当作正常发行源已经更新。批准候选须绑定实际参与构建的手写 C# 源码与项目文件集合及哈希，包括 `FanLab/fan-backend/YeManFanBackend.csproj` 外链的 `capabilities/FanCapabilityCatalog.cs` 和 `capabilities/AsusFanCapabilitySession.cs`。不能仅按源码所在目录或登记数量声称集合完整；项目引用也须与固定候选的构建证据对应。新增实际编译源码或改动已批准来源而未晋级载荷必须阻断，bin/obj 生成源与 SDK/工具链身份另列。补漏来源不得改写历史计划或原回执，须保留漏检反例及新校验结果。

全局回执中的仓库来源须采用repo-relative路径，绝对路径仅用于该case自己生成的证据。不能放宽原来源边界来接受新的接线错误。注册全部软件项通过与 `INCOMPLETE_COVERAGE` 的完整产品覆盖缺口分列，不能用登记数或复制成功声称全部机型/真实设备/功耗验收。



## 总库回归的源码与实际产物闭包（2026-10-19）

`fan.unified-capability-dispatch` 与 `fan.unified-capability-roundtrip` 的全局入口在编译前记录并保存输入原字节，在运行结束核对同一来源集合、文件身份及保存副本。正式消费者的原始报告逐字归档，外层结果只补来源与产物记录，不改业务断言、原失败或实际通过数。

实际生成并消费的 DLL、PDB、原报告、构建/运行日志及其它输出均通过现有 `sourceHashes` 回执机制绑定；在普通文件副本上分别改变主 DLL、PDB、Backend DLL，原读取器必须拒绝。只记录 Host 的一个哈希字段或通过计数，不能替代完整产物文件核验。源码、项目导入与 Content、目录内可能被加载的第三方 DLL 及明确 Roslyn 引用须按实际消费登记。登记同目录 DLL 全集不表示它们全部已加载，也不表示 CPU/功耗或物理硬件已经验收。

新测试 Host/Runner 和待交付候选各有独立身份。消费测试自己的编译物通过，不可直接归到另一 DLL 候选；候选须由其冻结源码、真实构建、PDB/源文档和针对该候选的实际回执对应。后续批次可继续修改共享源码，先前通过回执按真实漂移保持过期，不回退他方新改动或给旧产物追加新来源假装通过。


## 离线模块契约与诊断开关（2026-10-19）

离线边界复用已加载的正式模块及同一份状态，不手写复制正式模块的导出接口。风扇信号环境现共享 `fanDiagnostics.ts` 的真实导出和日志门：`fanDiagnosticLogLazy` 新增后，旧的诊断替身曾让正常握手抛出 TypeError；原错误及日志开启/关闭两个修前失败用例已保留。日志关闭时控制仍须成功且不产生 API 日志投影，开启时验证正式消费者的实际回执字段。

模拟接口缺失、fixture 导入过期等错误单列为 `SIMULATOR_CONTRACT_DRIFT`，保留原异常与对应源码身份；不要反报为现场产品故障。明确分类后继续执行同一正式消费者，并保留原失败。已证明生产调用正确时以实际执行证据闭环，无须修改正确的产品算法。新版 HC 或模块更新继续复用这些场景；真实 ABI 和契约变化须在现有环境中修适配后定向重验。


## 批准组件来源与新增消费场景（2026-10-19，第九轮）

风扇批准组件默认绑定 `deps/fan-host-source/stable6b`：先核对外部固定manifest身份、DLL/PDB及其编译来源，再从归档source选择29份输入。开发中的Host/Backend源码作为差异参考，不能冒充既有DLL编译来源；原生发送端和fanHost现行消费者仍直接追踪。统一 `lifecycle.fan-package-source-continuity` 保留原17场景并加入活源漂移仍可复制、归档缺失、归档manifest变化、归档工件混搭4项，共21项；通过只适用已批准6b/211，不授予149或其他候选安装批准。源码归档属于 `E-BUILD-EVIDENCE`，不进入运行包；4个正例各只复制10份运行文件。

新增InputHost成本窗和协议场景执行当前正式函数。原六persona夹具与当前收发端三组精确persona/profile合同不符时保留原FAIL；现行接受/拒绝矩阵另列，不能改生产selector或重签旧夹具。正式C++发送端在此只做静态一致性核对，实际执行范围是C# parser/helpers。571份工具是去重后的编译/runtime允许集合，实际Compile/Reference/Analyzer另外列明；不宣称571份全部被实际加载。全局工件上限640，格式/绝对路径/前后哈希校验保留，缺工具只让对应adapter BLOCKED。

插件更新两个场景复用原业务脚本及全部原断言，实际编译结果由观察器保存。故障源码副本须触发指定原断言，编译失败不能代替业务负例；原生更新结果未知时保持未知，不自动重放安装。软件夹具不执行下载、安装、Steam UI或真实会话存储，也不据其结果宣称整进程CPU下降。


## 完整 Host 入口与子套的范围（2026-10-19，第十轮）

完整 `Main --self-test` 纳入 `fan.complete-host-selftest`。单独的总库601/62、厂商子套、只读capability门通过，都不能推断完整入口通过；原149候选曾在该入口因FAN-934身份自测异常退出，失败原件继续保留。修复后的不同DLL/来源批次必须独立执行并绑定实际四元组，不把旧通过改名。

新场景要求外部审阅合同、全部载荷成员、明确Node/现有Job helper、两次NoConsumers和原global受控Job；固定唯一参数 `--self-test`。自然退出0、完整原始输出、唯一最终成功JSON以及真实checks字符串数组共同决定成功；未观察完整标记时数量为null，拒绝伪填400/428。完整自测回执仍只证明其自身范围，不自动授权候选安装或真实硬件/系统睡眠。默认无合同保留BLOCKED，新候选合同需显式传 `-CompleteHostContract` / `--complete-host-contract`。

## fan-api 普通日志的实际信号消费（2026-10-19）

现有 `-Mode signal-check -Archive <普通日志ZIP> -Domains fan` 已接入 fan-api.log（含既有1—3号轮转）：受限ZIP读取器只投影明确标量，冻结的 schema2 归一化器检查根回执/响应投影，随后执行原 FanHostLifecycle/HttpFanApiAdapter 消费者。当前接通握手响应，以及 GET/state 的 WinHTTP12029/send 传输失败；api.request-failure和其他路由保持未接，不能据错误原文捏造传输失败或用户操作。

原ZIP的SHA/稳定性、成员行号，与过滤后数组 JSON.stringify 的SHA分别记录。重构事件属于 source-derived-fixture，不能把该数组SHA当原成员字节身份，也不证明原会话、用户意图、具体机型、跨文件因果、物理效果或精确原时间线。未知设备类标签不输出为机型证据；通用握手可作纯软件消费。

旧日志缺 observationSchemaVersion=2 或版本不支持时，该批仍由归一化器REJECTED且不生成事件；归档整体记录INCOMPLETE并保留其他producer已有证据。根/投影矛盾、坏类型、重复键、CRC错误等仍拒绝，不能用HTTP200或空字段补成功。源读取前后及消费前后校验解析器/归一化器来源；变化保持失效。

现场日志AI继续按版本与节点取证。日志里观察到错误，与模拟是否按合同正确重放错误分别记录：软件流水线PASS可同时包含一条被正确保留的传输拒绝，物理收据仍为0。

## 虚拟手柄生命周期消费与公共退出缺口（2026-10-19）

新增三个公开场景：`controller.lifecycle-completion`、`controller.host-parent-lifetime`、`controller.host-lifecycle-protocol`。直接执行当前 Native 队列/完成保护、Host pipe loop/codec/原父进程句柄生命周期、Host parser/dispatch/prepare/release。设备终端和诊断明确替换，不运行正常 Host 入口或实际驱动。

Native 异常用真实字符串分配失败、出队诊断失败、可选观测失败验证完成权与未执行请求保留；省略完成保护/保留请求的生产源码负例必须触发原语义断言。已执行的设备请求不因异常自动重放。Checked 分配故障在真实权威锁之后精确布置，不能把STL前置分配进程终止当作通过。

Host 父进程退出用真实BCL管道与自有子进程覆盖连接、读、写，原父进程退出令牌负例必须在两配置重新触发三项原失败。Host 当前14组生命周期与旧1726完整套区分：旧套三项错误文案断言仍保留FAIL，当前严格拒绝和释放门单列，不改selector或旧断言来绿化。

三场景通过仅限其源码/载荷回执；完整进程联调、SDK线程、PnP清场、物理归还、真实睡眠与游戏输入消费均未验收。Native/Host公共修复不能被当作全部MSI乱码或其他家族问题的已证根因，也不形成CPU收益结论。

## Native 与托管侧真实管道联调及健康结算（2026-10-19）

`controller.lifecycle-native-managed-pipe` 执行完整现行 `inputHostRequestPreparedLocked`、envelope、真实Win32管道factory/IO/close、实际fast/DOM回执校验，对接当前源码提取的 `RunPipeClientAsync`、codec、原父进程句柄与真实 `HostSession.Handle/ReleaseLocal`。SDK、PnP、诊断和既有假时钟边界保持显式替身；正常产品Main、完整Coordinator/物理采集ticket均未执行。

七场景包括三种当前persona、连续RECREATE、QUIESCE后帧拒绝与序号继续、一次释放屏障失败后的保留/重试、断管释放、拒绝其他Host实例。必须观察真实子进程PID、自然退出码、清理后对象状态、Native与Host最终序号以及transport无pending。私有源码省略拒绝序号同步和Host实例闸门的两个反例，须在成功编译后触发指定原断言；不能用进程崩溃/超时冒充拒绝。

托管编译与运行验收分开：显式 `managed-fixture.json` 绑定构建manifest，manifest绑定源码/快照/生成物与DLL；子进程起止核验来源并记录实际加载程序集。旧编译源变化时返回缺前置/来源失效，不能改manifest给旧DLL签新源。运行阶段绑定原生实际include与链接lib闭包、.NET运行时和夹具产物，总工件仍受640上限约束，不因联合验收放大上限。

`controller.recovery-health` 复用原47场景的实际Context/Source/Window/ACK/maintenance，诊断开关各运行一次。私有helper让旧来源不过期时，必须观察四个固定原语义失败，尤其长读取停顿不能结清恢复预算。原编译失败、无回执、错失败名和篡改断言由其维护测试拒绝。这不是实际设备自愈/睡眠或CPU验收，也不能与管道场景拼成整个产品通过。


## 多轮原始回执的有界合审（2026-10-19）

全局 audit 一次可按调用者顺序读取最多128份原始回执，单份上限32MiB、总读取预算512MiB。原有32份限制已被40份实际贡献容器超过，因此扩容；不会把原容器拼成新执行报告。每个case仍取最后一份原结果和原case根，原失败、声明终点、编译及实际产物哈希、当前登记要求均逐项校验。

读取预算耗尽时整体BLOCKED，不把已读子集作为完整验收。内部调用可用 maxTotalReceiptBytes 降低预算，不能超过512MiB固定上限。audit仍为只读，executedTests=0；数量、来源合同和reference-only证据不代表YMCC全功能或物理设备已验证。源码或产物变更须按原失效语义定向重验，不修改旧回执补签。

## 显式候选审计与组合编译工具闭包（2026-10-19）

`global -Mode audit -AuditRegistered` 与 `run` 使用同一套显式候选选项。审计完整 Host 原回执时，须同时提供本次要核对的 `-CompleteHostContract` 和 `-PayloadRoot`；不会从某个历史 PASS 猜选候选。合同格式错误、缺少配对载荷或灯光 pin 非法均明确拒绝。合法合同仍不能豁免旧源码、工具和产物的哈希差异。审计本身不启动 Host、不编译、不运行缺失测试。

单项 `artifactInputs` 的上限由 `global/runner.mjs` 导出的 `MAX_ARTIFACT_INPUTS` 统一规定，当前为 3072；`receipt-audit.mjs` 使用同一常量。非零马达私有管道同时涉及 C++、Roslyn/.NET 和运行时，其 925 个去重实际工具文件全部保留并核对；不得为适应旧 640 上限删除真实依赖或只核对清单而不核对成员。完整电池 native→Host→bridge→实际界面往返现声明2438个工具与当前源码产物，均保留；原1024边界测试仍在，另含真实3072成员、2375成员子集、尾部成员篡改及3073超限拒绝。3073 项仍拒绝；原单回执 32 MiB、总回执 512 MiB、最多 128 个原容器的读取预算保持不变。

原件池保存各次完整公开运行容器和原顺序，包括失败与来源漂移。叶内部业务 PASS、外层来源漂移 FAIL、只读审计结果与当前安装/硬件验收分别解释；不得将内部 PASS 改写为整轮通过。同样，只核对选中函数文本相同不能豁免整份来源文件身份。源码或真实载荷有后续变化时保留原截止证据，并按真实受影响消费者定向复验。

## 当前源睡眠、游戏反馈写者和电池Host往返（2026-10-20）

三个正式入口分别为 `sleep.directory-resume-consumer`、`controller.game-feedback-writer-consumer`、`battery.native-host-roundtrip-consumer`。它们复用原模拟器的global运行/选中审计；主程序源码、工具和真实载荷变化后按原身份失效语义定向复验，不能将私有原型或历史同函数文本当作新整文件验收。

睡眠原60项所有权期待保留，扩展为原生70项和实际原生请求→完整bridge→真实UI toggle函数体26项。13种原生与5种前端源码故障均须成功编译/启动并命中指定原失败；原生RPC子进程和链消费者的退出码分别核对。普通状态缺失/损坏、查询未知、恢复失败必须保留可重试状态。此链不包含真实WebView运输；完整页面挂载是另一份有独立工具清单和组件边界的证据。

游戏反馈消费者从实际Host回调、decoder/mailbox/序列化和真实私有Windows管道，经过原生fast/DOM/Inbox、同一次已准入采样的来源token，到既有串行worker及XInput/MSI写者的内存硬件端点。现行正式范围是Sony USB经典双马达；原13场景354检查在Release/Checked保留，另6场景46检查验证OFF/target-disabled准入，新门不允许被夹具的默认开关绕过。5种实际编译源码故障保留。低频gameMotorDispatch计数只由既有input-host-submit-stats消费；API成功、进程累计计数与微秒耗时都不能解锁前端或证明物理转动/整进程功耗。

电池消费者复用完整原生client header、原transport自测断言、实际Host请求语句及原Controller/Rig/MemoryStore/Fake；14项原正常/失败场景和1种身份守卫源码故障保持。Win32管道/身份/时钟与Host流是明确内存端口，实际所属子进程stdin/stdout承载请求；不能冒称真实namedpipe ACL、Host Main/Serve或固件验收。每条编译命令使用独立不可覆盖的PID/退出回执，Job还要核对nonce、case根、worker PID、gate前归属及自然退出，不能用父层汇总覆盖子层证据。

编译器不仅有cl/c1xx/c2/link本体。工具身份需覆盖实际本地PE import/delay-import依赖；为保留可能的动态支持，当前手柄和电池声明还保守纳入整个安装编译器本地目录91成员，去重后分别1020和938工具。这两个旧消费者各自的1024限制保留；共享runner/audit上限现为3072，不删实际成员凑上限；目录声明不代表每个补充文件都已加载。睡眠原575工具已经包含13个递归本地PE节点及共同5个runtime DLL，不能因为别的消费者漏项就否定它或要求重复业务运行。只有具体缺项证据才触发该项维护。

冻结机型源码的397方法/92类已进入离线方法消费分类账（文件为 `capabilities/MODEL-METHOD-CONSUMPTION.json`）。fresh源码复验使用 `tools/hc_model_method_consumption.mjs --check --out <新外部证据目录>`，原维护器只做明确标注的轻量快照核对与升级差异计划。方法数量不是可用能力数量；新增未知方法、错hash或失效消费者引用必须拒绝静默通过，生命周期和机型特有硬件未接项继续保留。执行/审查步骤和验收回执另存，不作为运行时能力或新增高频转发层。


## 继承EC结果与完整虚拟关闭（2026-10-20 独立检查）

既有 `fan.onex-lighting-coexistence` 直接消费原继承EC布尔结果，并完整提取IDevice、AYANEODevice、AYANEODeviceCEc和AYANEOFlipDS的Close，核对真实HC原件的全方法规范化指令、调用/字段、异常区及继承关系。原69项与15项继承EC场景保留，再含8项关闭消费者。原运输为空时的可空调用可能无操作仍返回true；同引用守卫不能被“回调正常返回”替代。

同一候选的两份私有编译反例分别只断开继承写入结果和现有Close归还安全判断，必须恰好检出五项写入/温度反馈失败及两项有写历史的关闭未证失败；不能拿编译、启动、超时或其他失败凑数。正例、编译反例和原始回执均受现有配对来源/产物审计保护。已结算清理、归还动作收据及物理OEM所有权仍分列。

关闭场景显式预置已拥有的生命周期线程与待解除事件订阅；RGB、管理器、事件和OLS末端为内存夹具。此证据不覆盖STA启动、混合设置OpenEvents、完整HC工厂、Flip副屏或真实RGB/风扇硬件。上述边界继续保留为独立义务，不能据92场景PASS宣布全域接线穷尽。


## 继承EC会话身份贯穿原开关与归还（2026-10-20）

同一 `fan.onex-lighting-coexistence` 现补原CEc Open及原PowerProfileManager.CloneProfile，继续完整原Close。继承EC正式路线须在原Open前拒绝已有外部运输/打开标记，保留实际进入与清理责任的区别；成功或部分失败的原Open须留下当前设备/运输观察，由同一原Close消费，停止温度监控不能重新授权替换对象。核对发生在原Open/写入/Close边界，不另造驱动、工厂、轮询或硬件释放命令。

二十二个新场景覆盖替换/丢失资源、未写入前关闭、温度队列清理、第二会话、并发/重入、原初始化失败后的同owner清理与重试、真实手动归还及公开错误反馈。配置克隆使用原方法及未改的共享Newtonsoft原件，并绑定实际HC PE。第三份私有编译反例只断开两道身份守卫及原温度清理处的身份缓存，须在相同114场景中恰好检出十二项业务失败；原五项写入与两项归还未证反例分别继续。三份反例和真实候选/回执均保留原哈希审计，不以超时或编译失败替代检出。

此轮仍为隔离软件消费者证据：原RGB/设备事件/OLS末端为内存，已有生命周期线程为显式前提，不覆盖STA创建、真实固件、跨进程物理同设备互斥或任意库外线程在单次原EC端口序列内改写静态字段。替换发生时保留原owner未结算责任，不临时改DeviceOpen/静态运输去强行收尾。全域工厂、混合设置、其他机型与安装/实机义务继续。


## OpenEvents准入、取消归属与控制端消费（2026-10-20）

原 `fan.onex-lighting-coexistence` 继续同一入口。实际公开OpenEvents把不可变的设备/电源代次/profile代次贯穿原begin/wait/finalize，等待记录也沿用该代；订阅前后复核，回滚只移除当次manager/delegate并仅失效同代队列。回滚移除失败保留引用由既有Close重试，不能把新会话一并清掉。电源已取代的初始化只保留原因，不另造一次OEM归还；非电源失效仍走原恢复。

原114项保留，新增28项实际准备/收尾、公开等待分支、排队profile回调、清理失败、原引擎恢复及公开快照，合计142项。ROG等待末端是明确内存端点，准入FanControl声明来自冻结原构造语句；不宣称执行了ROG原HID或通用混合OpenEvents设置/电池方法。第四份精确编译反例仅断开一方法，须同例恰20项业务失败；旧五项写入、两项归还和原十二项会话身份继续，后者因新事件入口增加为精确十三项。

控制端两个409拒绝码经实际FanHostLifecycle消费为有界只读观察，不立即重放初始化或曲线。既有fan-global的control-self-heal子套件原断言保留，新增十项先复现失败，再598项通过；其真实输入/输出指纹合入当前证据。选中子套件不冒充五套全域验收。三份旧源码顺序脚本只移动本次收尾签名锚点，修复前后仍存在初始化/关闭体定位失败；保持未验收，不靠静态锚点放宽绿灯。安装/物理和剩余工厂/混合生命周期继续。


## 实际工厂初始化委托与源码合同（2026-10-20）

既有fan.onex-lighting-coexistence新增9项：定位编译LoadAssemblyAndFactory的device.initialize实际ldftn，执行只捕获backend的原闭包和RunHcInitializationStage；原IDevice/FlipDS/Flip1SDS Initialize完整提取并与锁定HC PE核对，false/false不得触发首次设置，原失败链不得被诊断覆盖或重放。第五份精确私有编译副本只改FirstStart一个bool，须同151例恰好3项业务失败，native0；旧5/2/13/20反例保持。

三个既有源码审计按当前身份绑定Close、原EC返回值、缓存温度分派、带owner回执与终止性读取拒绝核对，并保留11份指定失败源码反例。完整读取依赖（含全部原设备文件成员）进入原回执哈希门；源码变化必须重验，不能改哈希使旧结果过门。源码证据与编译执行分别解释；整个工厂加载、原混合设置/电池/控制器OpenEvents、STA启动及物理/安装仍未据此证明。


## 原混合生命周期、原始事件责任与Flags状态（2026-10-20）

既有fan.onex-lighting-coexistence改为执行22个原IDevice/AYANEO/CEc/FlipDS事件/设置/设备/电源/输入方法，原ManagerStatus值/Flags属性也绑定锁定PE；原11方法缺口的软件部分已覆盖，末端和真实管理器仍明确隔离。原151项保留，新增41项共192/2038，包含实际引擎和公开快照。Host原始事件入口与Host发布分列：取消收尾不重订阅，部分抛错先由同设备原Close结算；原Close/候选清理正常返回才退记录。组合活跃/停止标志不能假报Manager清理，沿原HC_CLOSE_PENDING拒绝。

前五编译反例仍全192执行并精确5/2/13/21/3（原20事件项加新混合1）；新增两个精确单方法编译反例，以同最终harness的2/8个实际消费者分别验证重入及状态/公开反馈。修复前实际图像也同例复现2/8；所有接受执行native0，编译/启动/超时不算检出。事件/设置/传输末端、已有STA前提、完整工厂与物理验收边界保留，不按子集PASS宣称全域穷尽。

## 固定版本的静态编译数据与既有消费合同维护（2026-10-20，phase29）

源码副本策略v5只额外保留 `PowerControl/GameTitlePolicy.json`：`src/bridge/gamedetect.ts`直接导入此475字节规则表，属于编译输入。不得据此扩大到PowerControl其他JSON、设置、session、lease或安装位运行数据。原B28/v4创建FAIL、F28业务FAIL与运行后完整性回执保持原字节；策略升级后旧外部工具binding不重签，不能把旧回执改成当前主线批准。

`gyro.machine-motion-consumers`继续保留原39故障、双配置80次与原正常断言/编译追踪。`private-current-model-unchecked`在新primaryKind分派出现后，需同时移除Private helper与分派当前class的两处等价约束才构成同一故障；不得删除原故障、把存活算检出或降低正例要求。phase29定向检查已在两配置各340224检查中准确检出两条private-refusal，但它是旧固定main加新runner的定向调查，根调用缺少完整原始分流进程回执；不能冒充新固定版本的完整global通过。

D16现有primary-owner checker必须作为同一全局leaf的实际必需阶段；它的独立五个归属/查找故障不能由原39组或矩阵补充替代。阶段缺失、工具/来源不完整、非指定失败、编译失败或异常进程结束均不得放行。原39组、矩阵补充与新增归属阶段分别保留执行原件及来源关系。

充电两个既有leaf的模拟端点现消费真实接收验证、HostDomainRouter与Program.DispatchBattery，编译依赖按真实项目图收集。兼容类型只转发编译后的生产方法，不生成旧switch影子。保留原14+1及3+1场景方案、正常断言和精确原故障；暂停、恢复、Tick、Shutdown通过实际router，外部运输和设备实现仍为既有内存夹具。副屏工厂明确拒绝，编译所需类型不构成副屏启用或物理验收。

主线维护索引现保留风扇显式OFF及两条充电联动的既有case引用；D16/D17已合入状态仅说明源码接点存在，activeScopeComplete继续false。完整新固定版本回归、同原件审计和结束后的源码/工具复核仍是独立义务。普通日志节点映射见phase29/SIMULATION-LOG-NODES-P29.json，日志标签与测试断言标签不得混用，不新增掌机验证包。
## 并行修改主线时的完整历史源码捕获（2026-10-20，phase29）

B29与B30原严格复制分别在monitorHudModel.ts及monitor_fps_family_entry_selftest.mjs发生源文件变化时留下部分FAIL；两个目录与原manifest不得重签、补齐或作为完整副本运行。旧create-source-snapshot.mjs与原createSourceSnapshot函数体保持原行为。

新增显式入口 `node tools/ymcc_sim/global/capture-source-version.mjs --repo ABS_REPO --out ABS_FRESH_SIBLING`，调用createCapturedSourceSnapshot。它在读取每个源文件的同一个fd时同时hash并保存该批字节，读取前后核验源fd及路径身份，关闭后独立再hash目标；最后仍核对完整副本、完整主文件和source-before/snapshot-after/source-after引用。每文件后续被他人修改不会使复制改用另一版字节；读取中被改写、缺文件、部分输出、目标篡改或重解析路径仍拒绝。

新manifest显式写captureMode=same-descriptor-capture及atomicSourceTreeClaimed=false，不宣称跨文件原子时刻。source-after与捕获字节不一致时，原创建仍为FAIL/SNAPSHOT_SOURCE_OR_COPY_DRIFT。只有完整副本及全部原始引用通过独立verifyCapturedSourceSnapshot，才可作为固定历史版本运行；sourceApprovalGranted保持false，当前主线需另作完整比较。旧B29/B30这类部分FAIL不能借新入口升级。

两种入口采用同一policy5排除边界，静态编译JSON只有已明确导入的GameTitlePolicy；不得读取运行设置、session、lease或安装位填补来源。外部Node工具仍显式绑定完整图和verifier代码路径/哈希，业务结束后仍核验全副本与工具。旧136项源码与工具合同回归已通过；新增入口的定向合同与新固定版本完整业务执行仍分别提供原件，不以机制通过代替消费通过。

## 工厂初始化的就绪与续接（2026-10-20）

既有 fan.onex-lighting-coexistence 在原192个未改产品图像场景之外，接入24个实际编译工厂消费者：自动/诊断构造、初始化各阶段异常、同对象续接、已完成步骤不重放、写入准入、只读采样、销毁/重入/所有权更替及ALC解析回调的归属清理。两个只改一个方法的编译反例，使用同24例分别准确失败15/7项；原七项编译故障、三源码审计/11源码反例和598桥接检查保留。编译或超时不能计为检出。

新套件的复制图像只替换五个明确IO叶端口（ACL、部署代、native依赖检查、日志配置、依赖查找），逐方法比较指令、局部量与异常处理范围。正式工厂/阶段包装、就绪、握手、写入、采样及Dispose控制流不改；构造、主板及传感器仍是明示夹具末端，不能把这份通过解释成正式ACL/部署验证、原GetCurrent/传感器算法或完整硬件初始化通过。原192例继续使用未经端口改写的正式候选。新回执与所有复制图像纳入原有指纹保护，源码变化须重验。

本轮证据延续 G:/YeManCC-Work/Build/Tasks/HC-Wiring-IndependentAudit-20261020/round9-factory-initialization。总接线目标保持active，实际启用能力及必要生命周期为准，不为提高覆盖率接通未启用的副屏、SuiPlay或实验协议；安装和物理验收仍分列。

## B31原失败与新固定版本复验前提（2026-10-20，phase29）

F31原39组故障全部检出，原80配置编译/原生追踪通过；矩阵两正常配置各3940670检查与处理等价通过，但包装门错误要求newBusReviewedFindCalls大于0，原整轮仍FAIL，四个矩阵反例和owner阶段未到达。新契约只允许这项计数为合法非负整数0，仍要求旧查找严格大于新查找，其他必需正计数、全部17状态、数值/处理等价及原故障、预算不变。主线相关60项合同通过不能改写原F31状态。

C31原六项为3PASS/2FAIL/1BLOCKED。充电native往返、充电workflow、风扇roundtrip的原PASS只适用B31；风扇显式关闭停在已审源码身份，catalog停在hc-device-catalog.json生成顺序，UI充电停在323项原.pnpm布局引用。原根和同副本audit均保留FAIL。B31业务结束后的完整源码和工具图复核通过只证明输入守恒，不能升级业务或current primary。

UI充电现有leaf保留原2362工具，并显式消费已绑定26包749文件及4项authority。新固定版本公开入口与实际leaf必须同时继承下列两变量，值只用该新版本真实binding路径及完整SHA，不沿用B31的值：

- YMCC_BATTERY_UI_NODE_TOOLS_BINDING
- YMCC_BATTERY_UI_NODE_TOOLS_BINDING_SHA256

这些变量供同一已核验canonical工具图使用；不得通过删323项、临时补snapshot .pnpm链接、放宽缺失门或猜测安装目录修复。缺binding/错误SHA/不在graph内或加载入口不符必须拒绝，结束后重验完整工具图。原UI3+1、预期exit23及native14+1不变。

Catalog本次只将一个JSON对象键顺序与原生成器一致化，所有JSON值与数组严格相同；02:52的16项CURRENT只对应当时输入。generated-root目录包含照明生成器返回的完整native/main.cpp，不能把旧生成目录的historical main覆盖当前主线。每个新固定版本仍须运行严格--check及原fan消费者。

风扇显式关闭需对实际源码变更做完整差异核对和原五模式/精确负例再验证后更新reviewed identity；共享桥的后续变化也须单独复核，不删除全源/工具前后门或仅改SHA。新增src/radial/availability.ts仅闭合真实TS导入，生产停用值false保持。

## 公开握手与真实STA启动/退出（2026-10-20）

既有fan.onex-lighting-coexistence增补16个公开握手/真实STA与WPF消息循环场景。覆盖并发冷启动、Start边界失败、正常/自身/发布前销毁、循环退出后拒绝复用、已准入但未执行请求的退出结算，以及同线程初始化续接/重入和排队串行。原192个未改写候选图像场景、24工厂场景、598桥接、三源码审计/11源码反例保留。新五份单方法编译反例以同16例准确失败1/1/1/2/1项；编译错误、无效IL、启动或进程超时均不能计为检出。

测试图像替换原5个系统/部署叶端口，另把只读线程ID端口接到真实kernel32.GetCurrentThreadId；保留硬件访问拒绝。Thread.Start前加dup/owner/call三指令调度观察点，删除它后逐方法比较规范化指令、局部量和异常区间一致。插入IL须安全扩展短跳转，原始PInvoke重绑定后还须验证写出图像已成为可执行managed IL。Start故障是明确边界注入，不代表OS自然失败实测。真实Thread/Dispatcher、公开握手、准入、Dispose和排队结算均执行；正式部署/ACL和原设备/传感器算法未由此代签。所有本轮自建线程必须退出。

现有完整源码捕获工具用于保留并行源版本，原manifest状态与哈希不可改签；快照封存和实际测试执行分别记录。证据延续G:/YeManCC-Work/Build/Tasks/HC-Wiring-IndependentAudit-20261020/round10-sta-bootstrap。线程退出只结算未执行请求，不伪造硬件归还或清除实际设备债务；启用能力范围、独立权限及安装/物理验收边界保持。

## Host源码双重身份与矩阵失败原件（2026-10-20，C33之后）

既有 fan.explicit-off-host-chain-consumer 同时核对 spec.reviewedSourceHashes 和 managed-compiler-contract.json 的逐项目实际编译源码。更新 Program.cs 前先保存完整原字节与差异，说明真实消费范围；仅修改 RealHcBackend 且其余 Host/HTTP/Engine 完整正文相同时，mock(realBackend=null)范围可进入编译验证，真实后端资格仍单列。两个清单须指向同一份已审 Program；全部41个实际编译源逐条核验，保留原项目、选项、引用、analyzer、五模式412检查、原精确故障及81项其余断言、13项native准入和禁止新发控制请求的检查。不能只更新外层spec或忽略内层身份。C33已被内层旧Program身份正确拒绝，原FAIL保持；修复只在后续固定源重验。

既有gyro矩阵阶段可只移除先前改名且未调用的完整suite main，但必须证明唯一符号、删除区段和保留前后字节，并由独立expectedProbe重建核对。原39故障/80配置仍必须先实际通过；生产matrix/BUS正文、不可变比较、全部17状态、每配置3940670检查及四故障均保留。generate-only局部矩阵通过不批准整个producer或owner阶段。

矩阵checker实时记录实际compile/native子进程和阶段日志，RUNNING不能当作完成。保留60秒checker、240秒matrix、900秒adapter、840秒producer限制。超时或编译失败先保存原错误，再收集闭包；PDB锁或其他次级收尾错误单列，q/result.json须保持FAIL并保留原错误，闭包不完整不得PASS。旧F32缺失的阶段原件不补造。正式新固定源仍须实际完成39/80→矩阵正反→owner整链和同源audit，软件模拟与真实硬件结论分别记录。

## 既有风扇部署绑定与缓存准入（2026-10-20）

同一 onex-fan-coexistence 入口新增实际部署消费者：26项未改静态方法、53项完整清单/缓存/工厂拒绝、4项当前真实Host/runtime/shared字节。仅两个AppContext.BaseDirectory读取重定向到Build夹具，验证门不替换；同53项三份单方法编译反例须准确失败23/10/5。缺少必需YeManFanBackend.dll及其清单条目时，冷/缓存和实际初始化入口均须在程序集/native设置前拒绝；原用户“不每次重哈希DLL”的性能决策保留。正式AppData ACL包装层、有效native启动、原设备/传感器算法与跨进程实际所有权仍分别未验收，不以描述符谓词或真实文件校验替代。详情见该模块README与round11-deployment-binding证据。


## 安装脚本必需后端与资源身份（2026-10-20）

同一onex-fan-coexistence入口执行9项实际安装脚本required-file foreach消费者。旧5a6d68原件和只移除后端声明的当前副本均须准确漏拒绝YeManFanBackend.dll一项；其余必需文件拒绝保持。仅AST片段和惰性声明表执行，不运行完整安装器、管理员/进程探测、部署或ACL写入。正式恢复asset、ps1 BOM/跨shell字节、payload清单及native/verifier/PackageSource pins同时核对；Host98ac/Backendd8cb与原runtime逐字节保留，不能把脚本修复当新二进制晋级。历史运行回执不改，schema-only数据样例仅更新明确身份字段并保留原来源。

长回归从新捕获并独立核验的同级固定源码副本入口执行。因源码捕获有意排除PowerControl运行数据，只有清单明确声明的静态载荷可另作绑定复制，并记录全部原始/目标字节、输入稳定性及副本源码复核；不读用户设置/会话/日志，不借此批准变化后的primary。

## 风扇与虚拟手柄夹具随产品持续维护（2026-10-20）

本轮使用两个既有专用聊天维护fan当前Host和virtual-controller R3公共入口。产品与原底层coordinator夹具由原虚拟手柄作者单写，公共wrapper写者不覆盖其main/Program或tuple新场景；具体共享写集与已发生的仅观察trace改动登记在父端phase29/assigned-fan-virtual-updates-20261020。产品日志中新复现的问题，应沿既有消费者加入夹具，保留旧源失败与修后同源结果，随后由原公共入口持续执行；不另建独立正模型或掌机验证包。

实际版本变化要同步真实编译输入及依赖。fan同时更新reviewedSourceHashes和41项managed compiler合同，旧pin拒绝及原412/精确1+81/13native/六HTTP时间线保持。原STA夹具canonical设备类型输入与catalog逐字一致；已Ready且会话完整和补开两路径用实际HTTP先后证据区分，不能统一强求都发open，也不能仅凭Ready描述免除当前native写许可。auth需遵循实际launcher→handshake顺序，原16检查禁止控制写入；launcher次数仅按现有显式请求合同观察，不添加与架构矛盾的总次数零要求。

virtual R3保留原场景名称、顺序、断言、计费和原30秒截止，动态检查数量变化须逐断言核对。曾出现21组2182→2175为八项原检查未执行加一项新增检查；不得仅凭数量减少或旧13项失败消失放行，应在实际时序上恢复旧检查，并保留新的refresh失败。独立中央类81通过不代替真实managed/full-native pipe消费者；旧precompiled35身份不能换名冒充R3。公共入口既报告自然退出，也准确区分业务FAIL与构建/阶段未达。

编译和故障注入记录同样维护：实际头文件/库/Node输入声明与观察分开；源码变异锚点限定实际消费者函数范围，保留新增生产新鲜度门及原故障含义，不靠dummy或整库替换凑命中。首个compiler/timeout/anchor错误优先保存，未生成execute回执或finish缺证据是未达/次级问题，不能以ENOENT等覆盖原错，也不能将编译失败计成语义故障检出。

原writer/motor fresh-owned-Build门保留。D证据入口可桥到G工作区明确自有新鲜Build子目录执行原完整producer，再逐字节归档回D；加载位置、MVID、来源回执路径仍如实保留实际G身份。目录合同或生成测试通过不代替完整Release/Checked业务及原控制。本批范围仍以ACTIVE-INTEGRATION-SCOPE及父端固定17核心+原两R3套件为准，不扩全部注册或停用能力；源码副本完整性、当前主线资格、原场景执行、物理日志分别记录。


## 正式会话权限入口的真实ACL软件回归（2026-10-20）

同一fan.onex-lighting-coexistence新增35项/260检查，直接执行正式ValidateFormalSessionCapabilityAcl、Windows目录/文件ACL读取和实际工厂拒绝。只将该方法的一个LocalApplicationData getter操作数重定向到全新Build夹具；恢复操作数后原全部指令/局部量/异常区间须一致。原谓词、身份/路径判断、初始化/诊断均不替换；四份单方法故障同35例须准确失败24/6/8/2。私有/继承/不可信权限、文件缺失/目录、大小写/归一化及实际READ_CONTROL拒绝均执行，空/外部诊断路径边界保留。

只创建及收尾本轮新Build文件的DACL，不改任何既有用户/安装/AppData权限，不改owner或SACL。四个故意不可读的夹具用系统DACL-only接口恢复后必须真实读回，测试端收集权限失败不计业务反例。正式用户路径/launcher到令牌读取的安装验收、有效native及原设备/传感器算法仍单列，不能因该软件套件PASS宣布接线穷尽。既有部署/工厂端口套件的历史边界字段不倒填；新formalCapabilityAclProof单列实际执行范围。


## 原设备选择、主板缓存与传感器入口（2026-10-20，第十四轮）

`fan.onex-lighting-coexistence` 的 `originalBootstrapProof` 补齐原GetCurrent、MotherboardInfo、PullSensors、Between及实际Host工厂/身份消费。源码提取保留C#逐字字符串；生成骨架不得冒充原二进制。五项已记录编译差异先保留，再绑定锁定HC完整PE方法体，42方法/所用闭包、字段、locals、分支及EH精确比较；不得放宽校验以接受新的差异。53场景直接执行原逻辑，3个单方法故障副本须恰好检出2/2/4业务失败；编译异常不算检出。

原Newtonsoft二进制与真实Build缓存文件继续消费，WMI/传感器/USB/遥测及全部设备构造为明确末端；构造不提供风扇能力，SuiPlay拒绝执行，不激活次屏或实验域。实际Host以原五个部署/OS末端隔离执行工厂与身份，42方法结构核验不表示所有未用getter已执行；原型号构造硬件体、有效native启动及实际安装入口/token/全部启用域所有权仍需独立证据。范围继续以ACTIVE-INTEGRATION-SCOPE.json为准。


## 风扇、虚拟手柄更新与DLL睡眠回执共同接入（2026-10-20）

本轮复用原global入口及原ID：17项已启用核心消费者、coordinator lifecycle与managed pipe两套家族（合计5个公共入口，含原故障分支），另按旧触发条件执行sleep.engine-memory / sleep.engine-http-all两个DLL软件消费者。该24项是本轮固定源验收集合，不代表全部登记项、所有产品分支或实机都通过。B39产品源码和本轮工具覆盖在新B40固定源接续，原失败、实际编译路径与被测版本分别留存；共享主线继续修改时不能借固定副本的结果签当前整树。

R3预编译载荷的20个managed来源按relative逐成员验证当前文件、原实际编译文件、受控snapshot三方SHA；native完整来源、编译工具与加载行仍保留真实路径。外部compiler输入不能冒充repo来源。必须通过原frontdoor的global -Mode audit -AuditRegistered（或CLI --mode audit --audit-registered）核对当前注册；只审历史receipt而不加载当前注册，不足以验证本轮编译前置。原640输入限额、90/120秒外层预算、10秒检查与30秒截止均保留。原CUT47独立审查原字节保存在专用文件，B39新增边界需自己的来源绑定，不得只换main SHA。

严格DLL审计仅识别sleep.engine-memory与sleep.engine-http-all原入口：显式调用者payload根、Host/Backend实际SHA、程序集身份/MVID、原pinned/loaded、inner receipt、原执行根与当前同relative源码、输出闭包、自然退出及原安全记录都须一致。PE校验只读字节，不启动CLR或设备。其他host-mock和完整HostMain/parent/exclusive/runtime继续原入口及完整载荷规则；两个软件引擎通过不能代签完整Host启动。原S39a拒绝回执不改签。

风扇现有四个桥接日志节点与虚拟手柄本轮源绑定节点交给原日志AI，只消费普通掌机日志。缺字段保持未知，区分请求发出/消费/状态/回执和旧代次迟到；Ready、模拟ACK或归还结果不能外推固件效果。不得由日志补造未执行的软件断言，也不要求掌机安装单独验证包。


## 原型号构造与原生数学库所有权（2026-10-20，第十五轮）

现有fan.onex-lighting-coexistence增加originalConstructorProof：锁定HC原79个准入型号实例构造体、7固件/处理器分支与实际Host原生依赖准入，共89场景。仅7个明确IO/组合末端替换，所有设备实例构造及GamepadMotion托管/原生ABI不改；全方法比较不能出现额外差异。只构造原Settings/Hotkeys两个必要Manager、不Start；WMI/BIOS/内存与LayoutMode使用明确数据端口，SteamDeck构造里的Open拒绝，无原硬件Open、用户设置、未启用SuiPlay或次屏控制。

当前catalog仅作独立声明上下界，真实构造验证能力位、原命令/输入/四保留热键、矩阵、运动所有者及设置订阅释放。锁定x64 GamepadMotion为纯软件数学依赖，21托管声明/原生导出及固定CRT/kernel导入表先核对；只允许该原生模块并实际创建/释放内存句柄，其余非托管入口拒绝。4份单方法故障同89场景必须恰好4/86/86/2业务失败；Dispose故障的86个自有数学句柄须在断言失败后由锁定Delete导出逐个归还，正例不得用此收尾代替原Dispose。编译/路径/加载配置错误保留为环境失败，不能计检出。该证据不等于硬件native启动、安装发布、所有启用域生命周期或整任务完成。


## 游戏库与 Decky 原消费者接入（2026-10-20，phase30）

全局入口新增 plugins.decky-mirror-host-consumer、game-library.account-discovery-consumer、game-library.snapshot-consumer。复用原四套 Decky 自测70场景与游戏库27/40场景，保留原断言顺序与原producer字节；游戏库仍保留原1000+1000组合。原函数故障分别要求原业务断言失败，编译、工具、观察器或输入前提失败不能计检出。

Decky 只运行原内存IPC/存储/时钟消费者；App启动关联和部分native恢复仍是静态证据。游戏库使用新建的源码同字节视图和原VM内存端口，明确记录实际读取的视图与原输入路径；不作为整页浏览器或原生导入验收。源码、工具和实际输出保留身份，原global reader须拒绝产物篡改和case外绝对源码。

固定24批次已结束，以上为全局模拟器的后续扩展。能力总库声明、注册条目、实际运行和物理验收各自记录；任何旧回执仍需按实际来源重新审计。联合测试使用同一固定源码及工具版本，对真正变化的消费者重跑；保留旧失败和未启用入口边界，不启动真实Steam/安装App/Host、设备或系统睡眠。


## 原工厂与原型号构造的组合消费（2026-10-20，第十六轮）

既有fan.onex-lighting-coexistence新增joinedFactoryProof，独立probe仅引用ConstructorIoPorts，不引用旧HandheldCompanion夹具或Harness。真实Host以Default ALC加载原构造体副本，保留Configure/logger、依赖resolver、原生文件准入、GetCurrent、Initialize、身份、就绪、能力和写入拒绝。123场景覆盖全部79准入型号；27自动身份、79诊断准入含10个原嵌套命名空间拒绝，后者从原自动识别路径进入，不放宽诊断合同。X1/Apex保持原缺失FanControl位并进入既有精确EC兼容策略，不补造能力。

原7构造末端保留，HC仅再隔离传感器与始终拒绝的设置写入两个末端；所有构造/GetCurrent/Initialize不改。Host仅隔离已独立验证的ACL/部署两末端和一个GetTempPath操作数，恢复该操作数须逐指令/locals/分支/EH等于原Configure；原MVID记录且不改。16故障/生命周期场景覆盖原生依赖先于加载拒绝、不支持传感器、5阶段失败续接、重入、释放及设备/程序集owner替换。同123场景五份精确单方法故障须检出3/6/2/4/1业务失败，分别为FirstStart、旧就绪、阶段重放、跳过原生准入、旧无前缀原生加载路径；不能把编译/加载配置错误计为检出。此组合证据仍不等于硬件Open、真实Manager Start、安装发布或八域整体验收。


首次正式回归保留native-admission:valid失败：同字节数学DLL短路径可加载，259字符路径报0x800700CE，扩展绝对路径成功。产品ValidateGamepadMotion现对选定DLL进行Windows扩展绝对路径处理；新增实际工厂消费者覆盖现场259字符临界值与超过300字符有效路径、已有扩展前缀、长路径缺失/损坏拒绝。旧路径操作数单点故障只应触发joined-native:long-valid；不得靠缩短证据路径或放宽有效文件门掩盖原问题。UNC语法保留不等于真实网络共享或全部硬件native长路径已经验收。


## 公开工厂、原构造与真实线程归属（2026-10-20，第十七轮）

既有fan.onex-lighting-coexistence在原joinedFactoryProof后增加publicFactoryProof。同123场景实际调用公开LoadAndHandshake，保留OnStaBounded、Thread.Start、Dispatcher消息泵、原工厂和Dispose，覆盖79原型号。阶段轨迹须全部来自同一个非调用者的真实前台STA，并具备DispatcherSynchronizationContext；实际Host线程身份须与该线程一致。正例须自然结束125个自有STA生命周期、执行125次真实只读系统线程ID查询，夹具强制收尾和未准入设备native调用均为0。

Host仅在原3末端上增加ReadCurrentOsThreadId的精确转发，仍调用同签名kernel32.GetCurrentThreadId；ConstructorIoPorts只允许此一native声明，不向Host或设备放宽kernel32 resolver。公开调度、启动/发布/准入/执行/退出、原构造/GetCurrent/Initialize不改。六份单方法故障与同123场景须精确检出3/6/2/4/1/122业务失败，最后一份只绕过公开入口的线程调度，不能把调用者线程上的构造成功冒充实际公开链路。故障副本也须由原Dispose结束所有自有线程；编译、加载、路径或线程清理失败不算检出。此软件证据不替代硬件Open、Manager Start、安装版或八域整体验收。

主线fanHost拆分后的startupConfiguration、deployment、capabilityAdmission三个模块同时纳入源合同和adapter输入，保留原public export、原3项生命周期审计及11个源故障反例，不能只签入口文件。


公开消息泵执行字段须由真实阶段的DispatcherSynchronizationContext观察产生，绕过调度反例必须报告false及0线程/0系统线程ID查询，不能把public模式请求写成实际执行。包来源adapter同步登记拆出的3个模块；保留原21场景生产门和未登记输入拒绝，逐个删登记的反例仍须失败，不通过放宽闭包核验接受新源。


## 原OneX公开设备生命周期组合消费（2026-10-20，第十八轮）

既有fan.onex-lighting-coexistence新增originalLifecycleProof，沿用原构造与公开工厂，将五个已准入OneX型号接到公开Open/OpenEvents/Close及实际ApplyPowerProfile、WriteFanProfileDuty。发现并修复X1 Mini遗漏窄风扇家族、以及窄风扇Open前误探Mini灯光HID就绪两处问题；原失败保留。跳过通用就绪以明确skipped记录，不冒充物理ready，窄生命周期仍验证原继承和EC元数据。

29场景1075检查，实际29个STA生命周期及只读系统线程ID调用，所有自有线程经原Dispose自然退出，强制清理及设备native尝试为0。保留所有原设备构造和完整IDevice.ECRamDirectWriteByte，执行原12次命令/数据端口写；仅隔离OpenLibSys五个IO末端、四个混合资源/HID入口拒绝陷阱和既有管理器组合，共10方法。PowerProfileManager原构造/事件实际执行；DeviceManager仅原状态读取的未初始化实例，不能声称其构造或Start已执行。

覆盖五型号关闭重开、原profile事件、授权拒绝、初始化/销毁失败与公开清理重试、EC失败返回、自动归还重试且不重复写、异主资源、元数据变化、owner更替、非法占空比、取整及Busy管理器不能伪完成关闭。所有已初始化资源包括未发布和夹具异主资源均须归还。六个精确单方法故障与同29场景须检出20/19/2/1/1/11项预期业务失败；不得以编译、错误路径或强制收尾充当检出。既有原构造/内部工厂/公开工厂及bootstrap门继续执行。此证据不替代物理Open、OEM抑制确认、真实Manager Start、安装或八域整体验收。


## 原MSI公开生命周期与结构化归还组合（2026-10-20，第十九轮）

既有fan.onex-lighting-coexistence新增originalMsiLifecycleProof，四个已启用MSI型号通过实际原构造、公开工厂/STA和窄Open/OpenEvents/Close；保留原IsReady固件绑定算法、原IDevice.Open、ReleaseMsiFanControl/SetControlBit及Host当前操作收据结算。本轮为加强原消费者证据，没有修改产品行为；不能把OneX就绪跳过照搬到MSI。

23场景覆盖四型号关闭重开、断开HID重绑、未知固件、WMI传输/短数据/设备flag拒绝、代次失效、归还读取/写入/回读失败、已自动状态零写入、异主boot/token收据和外部打开标记。每次23个实际STA和只读系统线程ID调用、自然退出、零强制清理/设备native尝试。原32字节命令按152再212顺序，仅清bit7并读回；软件proven不代表物理RPM/OEM确认。

继承上一轮明确端口后，HC仅增加10个MSI方法隔离/陷阱：HID枚举、原IsReady两处连接查询操作数、结构化WMI读写和旧shift读取，以及通用MSI Open/Close、注册修复、PnP重启和旧WMI写拒绝。恢复两连接操作数须逐指令/locals/branch/EH等于原IsReady。原HidLibrary逐字节不变，夹具只构造未初始化元数据实例并抑制finalizer。Host仅再隔离系统目录操作数、只读WMI元数据末端及PnP枚举拒绝，实际资源准备器和注册协调器继续执行；恢复目录操作数须等于完整原方法。系统目录/注册表/PnP均不得写入，状态夹具不冒充真实DeviceManager构造/Start。

六个精确单方法故障与同23场景须检出23/2/5/17/5/2业务失败，分别为通用Open、把TransportOk当IsUsable、漏固件读取、漏窄归还、强制成功结算、绕过boot/token；不得将空引用、编译/加载或强制收尾算作检出。保留原构造/工厂/OneX门，完整手动曲线/占空比、其它型号、管理器/跨进程所有权及安装物理验收仍独立开放。


## 原MSI公开手动曲线、表格与控制回读（2026-10-20，第二十轮）

既有fan.onex-lighting-coexistence增加originalMsiManualProof，实际公开ApplyCurve经原STA/窄Open/events，进入ApplyCurveCore、ApplyPowerProfile、ApplyMsiFamilyCurve、反射transport和原HC私有WriteMsiFanTableBlock/SetFanControl/SetFanFullSpeed。44场景1674检查、44实际STA及只读线程ID、52次公开调用/21次成功，最终零设备native尝试与强制清理。独立CPU/GPU轴golden、31字节原载荷保留和32字节请求均由实际消费者执行，不以简单terminal fake替代原算法。

模拟IO阶段替换当前device或assembly而不改睡眠epoch时，旧程序仍继续旧transport写入并成功；同44场景原四项失败保留。正式Host现把已存在每阶段写门绑定到捕获的相同设备/程序集，并继续检查epoch、Open、关闭和超时。此为隔离替换模拟发现的缺口，不能当作已观察到物理事故。已进入的原同步HC/WMI调用允许结束并保留不确定性，不冒充瞬时取消；后续阶段与成功发布须拒绝旧owner。

仅另隔离Host原单次Timer.Change操作数：原无限期Timer构造/Dispose保留，记录arm而不执行后台回放；恢复操作数须逐指令/locals/branch/EH等于完整原方法。所有公开曲线/反射消费者和原HC表格/位写算法不改，沿用前轮Build资源目录和注册/PnP拒绝。WMI末端按独立常量核对scope/path/method/block/length/minimum；原System.Management原始解码器未执行，不声称其已验收。

覆盖四型号满速/普通曲线切换、新操作收据、0/7/8/30字节短表、两块读写与写后确认失败、原不确定性分类、必需控制位、非法公开曲线、显式legacy轴保底及32字节响应多出字节的排除。六份精确单方法故障须在同44场景检出4/9/15/4/8/11业务失败，分别为owner检查、阶段门、伪表格成功、丢原尾部、错误100%意图及错温度接口；尾部仅改一原HC夹具方法，其余只改一Host消费者，编译/加载/空引用/强制收尾不算检出。保留原工厂与各生命周期门；后台重放、原解码、Manager Start、其它厂商及跨进程归属仍独立开放。

## 原MSI定时回调与配置/固件/电源回放（2026-10-20，第二十一轮）

既有fan.onex-lighting-coexistence增加originalMsiReplayProof，从已证明的公开手动链实际进入原Timer回调、Applied/Discarded委托、pending队列和原HC写入。原BCL Timer正常构造/Dispose，额外仅隔离构造处的捕获端口、单次Change调度和物理供电读三方法；恢复构造/Change操作数须与完整原方法的指令、locals、分支/EH一致。捕获的原回调在实际ThreadPool MTA执行，再由实际Host STA消息泵准入，不以直接调用Core替代正向回调。

原程序四型号外部配置/电源边缘共8项实际MTA写失败，另2项旧Timer跨新命令/关闭重开接管被复现。正式Host统一把全部MSI回放边缘交给既有STA，准入前owner传到Core，Timer捕获创建owner；旧epoch/睡眠/队列/模式结算和有限重试门保留。26场景1062检查要求26实际STA/OS线程ID、31成功公开手动调用、63原ThreadPool MTA回调，零设备native尝试/强制清理。夹具明确用原AllowWritesAfterResume完成重开，不把关闭后睡眠门绕开。回执的原Open字段须来自实际观察的Open计数。

覆盖四型号固件/外部配置/电源，旧回调与旧事件代次、真实有界STA等候时换owner/宣告睡眠、关闭、模式重复与合并、失败模式一次尝试、外部配置3次上限及2次延迟arm、写入时新Applied保留、无变化零写及显式满速回放。6份精确单方法故障须在同26场景分别检出20/2/1/6/5/2业务失败，不能用缺方法/非法IL/空引用/编译失败或强制收尾算检出。

端口不证明真实墙钟Timer投递或STA忙预算耗尽。原System.Management解码、完整管理器组合/Start、跨进程所有权、其它厂商与实物安装/设备验收仍各自开放；不启动Host Main或真实设备，也不增加未启用功能。

## 2026-10-22 退出销毁与持续类自愈硬门槛

用户补充的现场问题为“退出后监控通知未销毁”和“Decky睡眠后不自愈”。新增统一入口为 [global/capability-lifecycle/README.md](global/capability-lifecycle/README.md)。AI根据实际长期资源与事件消费判断功能是否持续类；未判断或仅有静态关联时保留未验收。退出须观察自有资源清零、挂起关闭时的有界兜底销毁、晚到回调不复活、主动OFF不重启。持续类另须观察睡眠Ready后与暂时故障后的当前代次健康，且保留有限重试、致命OFF、Decky崩溃预算、会话/版本/资源准入和不重放未确认写入。

现有global CLI在运行与注册回执审计中执行此门；每项合同必须绑定真实产品消费者、原case内结果SHA、当前源码SHA、具名业务case与实际资源/健康测量。发出停止/恢复请求、隐藏窗口、测试进程结束、保存ON、connected或旧ready都不能代替这些证明。新增要求缺证不得补写PASS。全局full/require-all对未分类与未验收项保持非零；无合同的旧定向回归只保留原范围结果，生命周期明确未验收。原失败/UNKNOWN/预算/负例/覆盖门继续生效。

监控通知与Decky为首批新增消费者队列；公共门通过不代表这两项产品已修复。软件/原生/安装/物理证据仍分开；不运行安装版或真实睡眠，不写设备，不终止无关进程，不制作独立掌机验证包。