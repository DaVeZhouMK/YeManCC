# YMCC 全局测试入口



沿用本机既有 `tools/ymcc_sim` 环境。新增 global 域负责发现当前产品功能、统一运行已审查的测试、回放睡眠日志并比较修复前后的结果。测试工具保留在仓库内，不进入发布包，也不要求掌机运行 AI 或另装验证包。



在主线仓库中使用 PowerShell 7：



```powershell

# 看当前源码功能清单与缺口；不执行测试

pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode inventory



# 看下一轮会执行什么；退出12表示仅计划，不能作为验收

pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode plan -Profile desktop



# 台式机可执行的正式逻辑模拟与源码约定检查

pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Profile desktop



# 睡眠专用测试；真实Host用例必须指定当前载荷目录

pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Profile sleep -PayloadRoot C:/SOFT/YeMan/PowerControl/fan-host-v2



# 完整已登记测试，覆盖缺口不允许总结果为通过

pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Profile full -PayloadRoot C:/SOFT/YeMan/PowerControl/fan-host-v2



# 只重跑指定测试，保留每轮证据

pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Tests fan.curve



# 从现有掌机日志分析睡眠事件，不驱动系统

pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode replay -LogPath C:/logs/sleep-facts.log



# 修复前后差异（输入各轮result.json的绝对路径）

pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode compare -Baseline C:/evidence/before/result.json -Candidate C:/evidence/after/result.json

```



结果默认写入 `%LOCALAPPDATA%/YeManCC/sim-evidence/global/<运行标识>`。可通过 `-EvidenceDir` 指定已有工作目录。每轮包含功能清单、执行计划、逐项结果、标准输出、源码与测试哈希、总结果和中文报告。不同轮次不覆盖；失败回执继续保留。



## 结果的含义



`PASS_SELECTED_SCOPE` 表示本轮所选测试通过。它不表示整个 YMCC 通过。完整 profile 或 `-RequireAll` 遇到功能入口未关联测试时返回 `INCOMPLETE_COVERAGE`，退出10；测试失败退出2，参数错误退出3，计划退出12。未知、缺文件、超时、缺结果和执行时源码变化均不得通过。静态检查与行为测试分别记录。



功能发现来自当前页面、桥接调用、原生注册及相关产品入口。`REGISTERED_TEST` 仅表示某个入口的源码文件与已登记测试有关联，不能据此推断每条原生命令、参数边界、机型及所有分支都经过行为测试。动态调用和无法归类的入口保持可见。具体分类与本轮数字以自动生成的 `catalog.json` 为准。



新功能不会自动获得安全测试许可。AI 应先检查其现有测试代码及外部调用，再把真实可运行测试加入 `global/adapters.mjs` 或 `global/sleep.mjs`。登记必须列出准确被测文件、测试类型、超时和profile；不要仅按文件名自动执行全部 selftest。增加关联不能替代增加有效断言。



## 睡眠与恢复



本次“睡眠测试”包括模拟状态转换、既有Host场景与现有日志回放。它可以发现代次错误、乱序/重复边沿、恢复窗口拒绝、旧租约复用、超时及恢复失败。它不代表系统实际经历S0ix/S3/S4，也不证明真实游戏、驱动或掌机固件恢复成功。



当前实际系统睡眠的硬闸仍遵守 [OPERATOR-AUTHORIZATIONS.md](OPERATOR-AUTHORIZATIONS.md)。本入口不会调用真实睡眠、冻结其他进程、切换驱动或写EC/OEM。本机台式机运行模拟与分析；掌机提供正常运行日志，记录不足时明确“无法判断”，不补造成功事件。



PowerShell测试放在Windows Job中运行。只有成功分配到本轮作业的子进程才会放行执行；正常结束、测试失败或runner超时关闭作业时，其自建后代一并收尾。不会按进程名称结束其他程序。这个机制只负责自建测试进程，不接管已运行的YMCC或Steam。



## AI修复流程



1. 先盘点当前功能与缺口，读取上轮失败和掌机日志；明确是模拟行为、源码约定还是实机观测。

2. 运行相关已登记测试，保留失败结果及源码哈希。缺失能力应补入真实测试和清单，不能删去失败项。

3. 修正产品代码或测试工具；改工具时用正常、失败、超时、缺文件等反例验证判定没有被放宽。

4. 重跑相关项，比较修复前后回执。测试范围改变、被测源码改变和未执行项目在差异报告中分别保留。

5. 本机模拟通过后，后续掌机正常版本继续提供日志；把实际差异转成可回放样本。模拟通过不能抵消掌机故障日志。



已有风扇AI应用接口、旧full域和手柄专用自环仍保留。global域是统一发现与编排入口；对安装版应用的操作仍需使用专用身份校验工具，不能拿内存模拟充当安装版验收。

## 真实掌机日志语料



用户指定语料根目录为 `G:/YeManCC-Work/日志/`，目前包含微星 G3E、ROG-Xbox-2025、Win5 的历史归档。目录名与归档名仅作操作者标签；设备身份与程序版本必须以日志中存在的证据为准。索引与计划均不会修改原档或直接发送日志内的任意命令。



```powershell

# 索引现有ZIP及可读日志成员；不全量解压或导入配置/令牌

pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode corpus -LogRoot G:/YeManCC-Work/日志/



# 将单个归档中的白名单事件转成仅供自有mock使用的重放计划

pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode replay-plan -Archive <某个日志归档的绝对路径>

```



`replay-plan` 的退出12表示计划已生成、尚未执行。未知事件与缺失阶段保持未支持；不能从归档名推测失败原因，也不能在缺少预设或操作记录时伪造风扇操作。实际执行仍走已审查的模拟适配器。重放结果是模拟证据，不能抵消掌机上的真实故障。



若系统的 python.exe 仅是 Windows Store 别名，可用 `-Python` 指向本机已安装的真实 Python，或通过 `YMCC_SIM_PYTHON` 指定。Codex 已有运行时可复用，无需下载依赖或另建环境。

## 日志输入与双向补缺协作（2026-10-18）



`corpus` 盘点现有 ZIP，`replay-plan` 读取白名单信号，`log-check` 根据日志证据执行已登记回归。使用 `-LogRoot`、`-Archive` 和 `-Python` 指定来源与现有Python环境。日志计划保留每个成员的行序，不推断跨文件因果；`log-check` 仍是选取回归，不能据此宣称逐事件信号注入已经完成。



OEM逐producer白名单保留原始bool/int及已知枚举，包括动作接受、默认动作屏蔽是否已验证、硬件是否写入、1500ms后观察、配置返回的未验证状态及DirectInput实际退避时长。字段类型不合法不作强制转换。只读诊断明确为 `diagnostic-observation-only`，不会自动生成控制命令。



当前4位独立AI分别补风扇消费、运行库连接、OEM/通用输入及低优先级灯光；统一交接在 `D:/Codex/2026-10-18/kai-s/work/global-simulator-20261018/collaboration/TEAM.json`。用户要求产品源码和模拟环境两头补，具体写入分工与共同验收见同目录 `TWO-SIDED-REPAIR.md`。独立模块完成后由主聊天接入global，不能把进行中的接口或任务书当成已通过。HC版本/运行目录/载荷身份应作为兼容门输入；未来升级复用同一环境，但实际ABI或协议变化仍须适配及重跑。



HC候选升级复用方案及“正式源码+模拟环境”共同验收合同见 [HC-UPGRADE-REGRESSION.md](HC-UPGRADE-REGRESSION.md)。统一入口的 `-Mode signal-check -Archive <真实ZIP> -Domains fan,input` 执行逐消费者信号注入；与只按日志选取既有回归的 `log-check` 区分。日志不完整、缺实际输入或缺反馈时保持INCOMPLETE。



全页导航已保存为仓库内的既有模拟器适配器，可直接运行 `-Domain global -Mode run -Tests controller.ui-navigation`。它复用原真实组件/正式engine/内存IO夹具，输出逐轮证据，并执行同一个精确源码门；不依赖历史Build目录。夹具源码在 `global/ui-navigation/templates/`，产品新增接口时同步维护明确的内存返回合同，缺导出会失败。



来源引用使用 `referenceInputs` 单列，声明 `UNVERIFIED_NOT_EXECUTED_BY_THIS_TEST`，不计为被测源码。三组HTTP Host睡眠模拟实际执行Host二进制和对应脚本，native/main.cpp只提供发送端历史来源，原生主程序的静态/事务测试继续严格绑定该文件。实际执行源码、断言输入和消费者报告的源码hash仍必须一致；来源引用变化会记录但不冒充原生行为已通过。



## 当前回执是否还能沿用



`-Mode audit -Receipts <旧轮result.json>,<后续定向轮result.json>` 按给定先后顺序合并分项，再核对当前源码、消费者报告的真实源及明确Host二进制；输出 `receipt-audit.json` 与 `needsRerun`。它仅读既有回执并计算hash，不执行测试，也不替代HC候选的实际CLR/ABI连接门。后来BLOCKED的同名分项覆盖历史PASS，不能借用旧通过；未执行来源引用保持UNVERIFIED。



```powershell

pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode audit `

  -Receipts "C:/YMCC-Evidence/Full/result.json,C:/YMCC-Evidence/Repair/result.json" `

  -EvidenceDir C:/YMCC-Evidence/Audit

```



只重跑needsRerun对应项目，不要求停止其他开发。审核或发布候选时，使用该候选的明确源码/载荷及对应回执，完整覆盖与物理验收各按自身门处理。



## 2026-10-19 全局能力扩展



- `battery.charge-workflow`：复用原BatteryHost测试，正式组件和bridge通过测试stdio直接消费当前生产C#分发与同一Controller；含逻辑挂起/恢复，拒绝目标原生调用，不运行生产Main/命名管道/设备写入。细节见global/battery-signals/README.md。

- `fan.lifecycle-wake-display`：原生命周期、唤醒进度、显示证据三组直接执行，保留683个原断言；明确网络、进程、native及目录外写入禁用，不冒称FanView物理渲染。

- `controller.input-diagnostics`：原样提取并编译当前C++诊断与键位消费分支；检查UI/own/input动作身份、摘要刷新、137键位映射和默认值，不产生系统键盘事件。

- `display.background-signals`：正式背景消费者、低电量迟滞、动态媒体版本与关闭后晚到回执；既有C++默认背景函数在新鲜隔离文件/设置上执行，桌面壁纸不改动。



全页导航在复制前固定整个模板树（包括新增辅助场景），逐文件校验副本，并在结束时核对原始模板集合与包装代码。旧运行不能写入后来版本的来源hash。source-contract-selftest.ps1含辅助文件改动、缺失、增加、复制漂移和目录越界负例；场景自身的失败继续保留。



静态目录发现中，已审核的native/BatteryHostTests是测试来源，单列sourceExclusions而不当产品功能；它们仍作为真实执行adapter的必需输入和来源记录。没有因为完成了某项测试就声称整个同文件的全部操作已验收。





## 明确指定运行库与灯光候选



`-Domain global` 的 PowerShell 入口和 Node CLI 都能接收候选 runtime/shared 目录；风扇 Host 使用 `-PayloadRoot`，灯光单独使用 `-LightingPayloadRoot` 与 `-ExpectedHelperSha256`。灯光 root/pin 缺一即拒绝。参数和实际候选 bytes 的前后 hash进入回执；修改候选 DLL 后用 audit会要求定向重跑，不能凭源码没有改动沿用旧PASS。完整示例和 profile/版本适配限制见 HC-UPGRADE-REGRESSION.md。



`sensor.monitor-consumer-signals` 复用原监控hub/clock三套消费者，保留原断言，历史时钟源码夹具已按原字节固定在仓库中；不再要求一次性Build目录存在。端口注入、真实系统采样与界面物理呈现分别说明，未确认产品问题不为凑修改数改产品。



Node 工具单元测试从仓库根运行。fan-global/runner.test.mjs 和 sleep-engine/sleep-engine.test.mjs 是需要明确已执行回执和候选身份的专项检查，不得用无输入的递归 node --test 取代各自已有runner。环境/工作目录错误要保留并纠正调用，不改断言使其绕过必要输入。





## 第三轮能力扩展（2026-10-19）



四个新入口均留在现有global环境，CLI/PowerShell可按Tests选择运行；第三轮冻结时full回归共72项，后续以当前registry与-AuditRegistered结果为准。登记数和源码关联数不是完整功能或分支覆盖率。



- `performance.gpu-dedicated-memory-workflow`：四原C++套件342检查、37桥接/实际IPC场景、32原浏览器场景、12正式Vue→正式IPC→当前原生dispatch→同一runtimeRead/runtimeApply联动；Windows/COM/固件/注册表由原惰性端口承载。模拟boot身份与容量变化，不执行系统重启。原生执行前审查实际PE导入，带真实DeviceIoControl导入的坏二进制必须拒绝且不启动。说明见global/gpu-memory-signals/README.md。

- `controller.touch-surface`：32原生状态场景、8桥接场景；原event-time 10、反馈租约44、部分发送37断言及230400故障组合保留。执行真实Core/配置/所有权/重排与原头文件，排除物理输入与hook。说明见global/touch-surface-signals/README.md。

- `sensor.monitor-output-signals`：当前114检查，其中112行为和2源码合同，涵盖真实bridge/owner/source/clock→HUD entry/Vue/layout→ACK→显示/关闭。旧模板完整68项重新纳入，只对有文档与正式选择器依据的一条旧标签元数据期待作受控迁移；原67/68失败、三个错映射/顺序/默认值拒绝对照均保留。说明见global/monitor-output-signals/README.md。

- `network.update-predownload-signals`：原57断言、新23场景及11工具检查，正式scheduler/provider/service/manager→memory WebView IPC实际执行；真实网络、下载器、安装、服务及用户配置不运行。修前9失败与额外小数退避NaN失败保留，三份正式源和测试同步修复。说明见global/update-download-signals/README.md。



显存桥接修复禁止把provider/verification数组强制转换成合法枚举；全项目vue-tsc与两分辨率导航在本轮正式源重新验证。各切片已存在的正确逻辑以真实执行证明，不为凑修改数改产品。模拟IO、实际安装和物理设备证据继续单列。





## 审计当前登记项是否全部执行



回执默认只证明所提供的分项范围。对当前登记测试做完整性核对时，用 `-AuditRegistered -Profile full`（Node为 `--audit-registered --profile full`）；也可加 `-Tests` / `-Domains` 限定本次范围。此选项直接读取当前global registry并与最新分项执行回执比较，不运行测试，也不制造缺失项的PASS。



```powershell

pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode audit `

  -Profile full -AuditRegistered `

  -Receipts "C:/YMCC-Evidence/Baseline/result.json,C:/YMCC-Evidence/Repair/result.json" `

  -EvidenceDir C:/YMCC-Evidence/CurrentRegistrationAudit

```



`registeredScope.missing`列出已登记却从未提供执行回执的项目，状态NOT_RUN；相应ID加入needsRerun，返回REGISTERED_CASES_NOT_EXECUTED/exit10。已有FAIL、BLOCKED或源码漂移继续保留原原因，后来的BLOCKED不得借早先PASS覆盖；未知选择、空范围、重复注册身份均拒绝。summary.cases仍是实际已有回执数，registeredSelected/registeredMissing单列，禁止把合成缺项当已执行结果。



`-RequireAll`审计会同时核对当前登记项，但即使登记项全通过也保持INCOMPLETE_ACCEPTANCE：只读审计不证明未建测试的全部产品能力或真实设备。`-AuditRegistered`通过只说明明确登记范围的回执当前有效；普通不带此参数的audit继续用于部分回执的来源核验。PowerShell和Node实际入口均有回归，包含新增项缺回执、错误profile选择、最新BLOCKED、超限回执和完整范围不等于全产品的反例。





登记完整性还比较同名用例的当前入口路径、证据范围、必需源码输入和必需结果文件。旧文件仍存在不代表新入口已执行；source-contract不能只改登记声明就升级成behavioral-mock。新增必需输入或结果却没有原执行记录时，输出STALE_REGISTRATION及registrationMismatches，加入needsRerun。原有部分audit不改变，只有明确-AuditRegistered范围执行此合同核对。



## 显存能力日志接线



`-Mode signal-check -Domains performance -Archive <日志ZIP>` 现在复用当前白名单 ZIP 读取器，执行 `machine-capability` 中 `gpu.dedicatedMemory` 的正式桥接/IPC/状态显示。只保留枚举、严格数值和受限选项，其他机型身份/驱动/启动记录不导出。同一 native 日志同时包含 input/performance 时，各域分别限额，密集输入不会挤掉显存观察；所有字节/行/CRC/事件总预算仍生效。



能力日志缺少完整响应绑定/fence，因此固定内存补充字段会明确列出，信号回放保持 INCOMPLETE，且不调用保存。`log-check` 可根据明确的显存能力事件选择原有完整 GPU 工作流，选测试不等于原日志时序重演。新增 `performance.gpu-log-signals` 回归检查现有桥接消费者、未确认状态和拒绝负例，PASS不提升归档日志或实机范围。其他新域没有连接的日志生产者仍列后续。


## 执行回执的完整性（2026-10-19 独立反向验证）

审计以执行时记录的结构化证据 SHA-256 为准：证据 JSON 即使仍声明 PASS，只要字节改变，也须重新执行；缺少原始 hash 时不得用当前文件补签。Host 必需的 `YeManFanHost.exe` 与 `YeManFanHost.dll` 必须具备有效 hash，文件缺失和 null hash 不能互相证明通过。原执行记录明确捕获为缺失的可选库仍单独比较，后来出现文件会使旧身份失效。

同一轮出现重复用例编号会阻断该轮审计，不能让后面的 PASS 覆盖前面的失败。不同轮之间仍按调用方给定先后选择最新结果。PASS 若同时记录启动错误、终止信号、缺失输入或构建错误，按失败处理。畸形输入与证据结构生成可审查的 BLOCKED 诊断，不能直接抛错而丢失审计报告。这些约束验证回执来源和完整性，不表示重新执行了被测能力。





## 普通日志节点反馈接口（2026-10-19，日志侧）



复用普通导出ZIP，可独立执行 `python tools/log_simulation_feedback.py --zip <archive.zip> --output <feedback.json>`。

它输出LOG-SIM-01..05的有界字段反馈，不执行模拟器、ZIP命令或硬件操作，也不替代已有现场差异报告。

新Fan API日志的observationSchemaVersion=2保留supported、根receipt/state分层和失败关联；普通导出增加Backend/runtime manifest的独立磁盘hash。

日志侧伴随工具与global消费分别记录。global现已支持下文“fan-api 普通日志的实际信号消费”明确列出的schema2握手及GET/state传输失败；其他fan-api事件仍保持未支持，不能把字段检查扩称为全部事件已执行。

反馈中缺证、预算达限、版本不符、单进程CPU和物理待观察保持原义；来源样本不重签为当前主线通过。

源码/合同/回归详见 `G:/YeManCC-Work/Docs/Tasks/LogAlignment/15-SIMULATION-LOGGING-NODES-20261019.md` 及同名JSON。



## 完整 Host 自测的统一入口

`fan.complete-host-selftest` 执行正式 Host 的完整 `--self-test`，用来发现子套601/62等未覆盖的启动和自测故障。它只接受明确审阅合同和同一候选载荷；没有合同的 full 清单会保留该项为 BLOCKED，不推断安装版或自行签发候选。

```powershell
# 变量指向本轮实际审阅的载荷和独立合同；plan退出12，不执行Host。
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode plan -Profile full -Tests fan.complete-host-selftest -PayloadRoot $HostPayloadRoot -CompleteHostContract $ReviewedHostContract -EvidenceDir $FreshEvidenceDir
# 获得独占软件窗口并由原入口再次检查空闲后，使用相同参数改为-Mode run。
```

合同固定Host DLL、Backend DLL、apphost、manifest及全部平面载荷成员，还包含Node和原Job helper身份。测试沿用global受控Job，入口在准备和启动前分别检查已有消费者，仅新建自己的自测进程；实际argv固定为 `--self-test`。原始stdout/stderr、PID、自然退出、候选前后集合/哈希和真实最终JSON一并验收；没有完整结果时检查数为null，不用子套计数或旧日志充当本轮成功。

完整自测的通过范围与候选full-nine、实际HC初始化、安装准入和物理效果分别记录。已有149失败及98成功日志只作为合同回归夹具，不能代替新wrapper实际运行；普通现场日志按真正使用的版本反馈。测试工具和夹具保留在tools，不进入运行包。

## fan-api 普通日志的实际信号消费（2026-10-19）

现有 `-Mode signal-check -Archive <普通日志ZIP> -Domains fan` 已接入 fan-api.log（含既有1—3号轮转）：受限ZIP读取器只投影明确标量，冻结的 schema2 归一化器检查根回执/响应投影，随后执行原 FanHostLifecycle/HttpFanApiAdapter 消费者。当前接通握手响应，以及 GET/state 的 WinHTTP12029/send 传输失败；api.request-failure和其他路由保持未接，不能据错误原文捏造传输失败或用户操作。

原ZIP的SHA/稳定性、成员行号，与过滤后数组 JSON.stringify 的SHA分别记录。重构事件属于 source-derived-fixture，不能把该数组SHA当原成员字节身份，也不证明原会话、用户意图、具体机型、跨文件因果、物理效果或精确原时间线。未知设备类标签不输出为机型证据；通用握手可作纯软件消费。

旧日志缺 observationSchemaVersion=2 或版本不支持时，该批仍由归一化器REJECTED且不生成事件；归档整体记录INCOMPLETE并保留其他producer已有证据。根/投影矛盾、坏类型、重复键、CRC错误等仍拒绝，不能用HTTP200或空字段补成功。源读取前后及消费前后校验解析器/归一化器来源；变化保持失效。

现场日志AI继续按版本与节点取证。日志里观察到错误，与模拟是否按合同正确重放错误分别记录：软件流水线PASS可同时包含一条被正确保留的传输拒绝，物理收据仍为0。

## 2026-10-19 全局恢复消费者与有界原回执合审

日常AI回归继续使用上面的 `-Domain global` 原入口。为各工具的隔离目录要求一致，明确给出一个仓库外的新工作根，例如 `G:/YeManCC-Work/Build/Validation/<新轮次>`；InputHost协议测试已支持合法的新鲜本地非系统盘目录，系统盘、源码目录及其祖先/后代、复用旧目录和重解析路径仍拒绝。不要把中文深层交付目录直接当原生编译器的执行根。

```powershell
# 原生睡眠恢复API：真实恢复函数与受控OS端口，没有系统睡眠/真实游戏恢复。
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Tests sleep.native-recovery-api-consumer -EvidenceDir G:/YeManCC-Work/Build/Validation/sleep-new-round

# 风扇前端的等待租约与未确认握手恢复：实际正式类，Host和电源时钟为测试端口。
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Tests fan.pending-acquisition-consumer,fan.unconfirmed-resume-consumer -EvidenceDir G:/YeManCC-Work/Build/Validation/fan-wake-new-round

# 对原始运行容器做当前源复核；$OrderedRuns为按既有顺序逗号分隔的result.json绝对路径。
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode audit -Profile full -Receipts $OrderedRuns -AuditRegistered -EvidenceDir G:/YeManCC-Work/Build/Validation/audit-new-round
```

`sleep.native-recovery-api-consumer` 对经审阅的原Python夹具执行完整原生恢复链。API夹具已为当前源码维护精确完整函数提取，保留原25组API断言并增加6组上下文，连原wake43组共74组。真实线程查询在OS端口返回未知；意外进程、线程、网络或电源调用拒绝。confirmation源码负例必须先编译并启动，再命中原断言；坏编译和缺文件不算行为检出。

原生测试在nonce/marker绑定本次case的短自有目录执行，保留原必需路径段。Job后把真实产物逐字拷回case，原JSON/stdout中的执行路径不改写；单独mapping绑定原路径和归档hash。来源、工具、产物与归属核验后才清理短目录，失败保留；这只写测试临时副本，不是操作安装版。

`fan.pending-acquisition-consumer` 保留44个有序family/name结果。`fan.unconfirmed-resume-consumer` 保留30个有序场景；其源码故障产生的原断言失败与生产拒绝错误分别计数。家族标签为内存夹具身份，不能由这两项推断所有机型的OEM释放。逐路线源合同见 `capabilities/FAN-RETURN-CONSUMPTION.json`：Close、cleanup、pending和实际释放动作回执分别结算，缺少原日志OperationToken仍为字段缺口。

正式audit一次最多读取128份原容器，单份32MiB、累计512MiB。按调用顺序保留各case最后一份原结果及原case根；超预算整体BLOCKED，不接受已读子集。原结构化结果/源码/产物/完成终点和当前登记仍逐项检查，audit执行次数始终为0。文件/入口关联数量不是行为通过率；活源在审计期间变化时，外部前后围栏须明确标记该轮不构成稳定全域截止。

已有原结果经后轮替换，不删除或改写。模拟器全域尚有来源过期、未覆盖消费者及现场待证范围；单项或一批通过不能写成YMCC全部功能、物理设备或真实系统睡眠已验收。



## 原生非用户唤醒与睡眠恢复界面（2026-10-19）

这两项复用现有全局入口及原测试，分别验证原生重试决策和实际Vue设置/状态消费。使用新的绝对证据目录：

```powershell
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Profile full -Tests sleep.non-user-wake-consumer -EvidenceDir G:/YeManCC-Work/Build/Validation/non-user-wake-new-round
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Profile full -Tests sleep.recovery-ui-consumer -EvidenceDir G:/YeManCC-Work/Build/Validation/sleep-recovery-ui-new-round

# 读取刚才生成的原result.json，核对实际登记及当前源码；不重跑业务
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode audit -Profile full -Tests sleep.non-user-wake-consumer,sleep.recovery-ui-consumer -AuditRegistered -Receipts <原非用户唤醒result.json>,<原界面result.json> -EvidenceDir G:/YeManCC-Work/Build/Validation/sleep-consumer-new-audit
```

`sleep.non-user-wake-consumer` 执行原 `tools/sleep_non_user_wake_selftest.py` 的4组S3、4组能力未知、32组主流程及三个固定JSON回放（50/21/24事件）。原62处原生断言保留有序，事件回放请求数分别为2/0/0；120秒门、累计重试预算及用户唤醒取消保持原期待。当前纯内存风扇retryBoundary上下文使用真实头文件。私有预算故障须先编译、启动，再由原断言检出。系统睡眠请求是原计数fake，不能据此声称执行了SetSuspendState、USB4恢复或物理设备唤醒。短TEMP根由精确case所有权标记绑定，Job返回后按原字节归档并核验，成功才清理。

`sleep.recovery-ui-consumer` 执行真实SleepGuardView、SleepRecoveryPanel和sleepRecovery bridge的22个原有序场景。旧选择器在真实DOM中不存在，原失败和截图已保留；仅三处选择器维护为面板内精确switch角色与名称，业务期待保持。原生设置/状态端口为内存fixture；迟到回复覆盖新状态的私有源码故障须编译成功并命中原第52行断言。实际served程序、编译图、运行模块、报告和截图绑定同一执行；浏览器及loopback服务自然关闭，私有TEMP标记保留至子目录清理完成。旧清理失败保持历史FAIL；曾被自动审批拒绝清理的旧残留单列保留，不由新PASS改签。

显式 `-Profile full` 的单项或两项PASS仍保留原整域 `INCOMPLETE_COVERAGE` 结论；selected `-AuditRegistered` 仅证明所选登记项，不是全产品验收。主程序在运行之后改变时，即使原执行源码未漂移，也须把回执标为STALE_SOURCE，不追改历史PASS或放宽整文件身份。真实系统睡眠、Steam/游戏、安装版及掌机效果仍由各自现场证据确认。


## 现有原生控制器测试的短路径（2026-10-19）

`controller.startup-return` 的原ledger编译器会产生较长的sourceDependencies文件名。该适配器会在原suite执行前检查路径长度；历史282字符路径已明确拒绝，原FAIL保留。使用工作区Build下的短证据根可运行现有正式入口，不能把旧前置拒绝解释为业务断言失败：

```powershell
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Profile full -Tests controller.startup-return -EvidenceDir G:/YeManCC-Work/Build/V
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Profile full -Tests controller.recovery-health,controller.lifecycle-native-managed-pipe -EvidenceDir G:/YeManCC-Work/Build/Vh
```

证据根每轮会自动创建独立run目录。当前startup实际执行的spec为原62个seeded场景加29个cold-load场景，共91；另执行原167个owned-return场景及13个ledger场景，两种配置均保留。两个私有源码故障必须编译成功、执行后精确命中所列失败，原seeded62仍通过；静态旧README的75场景描述不替代本轮真实spec和报告。

`controller.recovery-health` 使用同样短根可完成原47场景/68断言、diagnostic off/on及实际stale-source源码故障，原四处语义失败和编译账本分别保存。`controller.lifecycle-native-managed-pipe` 需要当前源码绑定的managed fixture；InputHost源变更后会在创建pipe前返回BLOCKED。应通过既有managed fixture构建入口重建实际依赖与身份，再作定向接回；不能只更新旧二进制的pin，也不能把BLOCKED按PASS合并。该命令不授权操作真实Host、设备或系统睡眠。


## 用户明确排除的能力（2026-10-19）

以 `capabilities/USER-SCOPE-DECISIONS.json` 的直接用户裁决为准。`settings.wake-password-ui` / `sleep.wake-password-ui-consumer` 已标为USER_EXCLUDED，新增的活动import/spread与对应正向登记断言已撤下；旧产品设置、原测试和全部历史public/audit原件保留。该项不计未完成、待接线、待重跑或能力完成率分母，HC升级不自动重新导入。历史127项盘点与回执保持原字节，不能当当前活动范围。

普通全站设置UI候选不因旧脚本存在就自动接入；必须先证明它是必要机型能力的实际消费或生命周期依赖。仅设置启动恢复的草稿已停止接入，原脚本本人两处路径改动已还原，不计能力缺口。机型控制、输入反馈、睡眠过程中的设备所有权、失败归还等必要消费者继续推进。原始回执审计为保留历史仍可能列出未登记项的旧状态；活动任务与完成分母取当前registry及用户范围决策，不能把被排除项重新排进执行队列。


## 单PID睡眠暂停与恢复消费者（2026-10-19）

`sleep.game-pause-consumer` 属于必要睡眠/输入生命周期测试。它执行正式单PID暂停决策、恢复时PID创建时间复核、取消竞态和实际worker暂停分支；原底层 `sgSuspendGameByPidUnlocked` 在这份原夹具中仍是计数内存终端，不能据此说已执行真实NT暂停。原24组场景/48条断言保持，补当前纯内存上下文后共28组；新增桌面ValveSteamMachine从旧动态“所有已知机型均掌机”的假设中明确单列，16个旧family原期待继续保留。原测试自身创建并join一个取消竞态线程，未操作用户进程。

```powershell
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Profile full -Tests sleep.game-pause-consumer -EvidenceDir G:/YeManCC-Work/Build/Validation/game-pause-new-round
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode audit -Profile full -Tests sleep.game-pause-consumer -AuditRegistered -Receipts <本轮原result.json> -EvidenceDir G:/YeManCC-Work/Build/Validation/game-pause-new-audit
```

原夹具初始编译失败、补上下文后由新增桌面机型触发的真实断言失败均保留。私有源码故障只让opened-handle创建时间门恒真，必须先编译并启动，再被原PID复用断言检出；编译错误不算反向通过。原脚本按独立纯内存边界维护，Job、工具身份、完整源与实际编译输入、短TEMP所有权及原字节归档在正式入口逐一核验。真实游戏/用户进程/Host/NT/系统睡眠/实体手柄不由这项模拟验收。


## 底层单PID、失败读回与Host输出观察（2026-10-19）

`sleep.single-pid-freeze-consumer` 补上了上一层game-pause夹具仍用fake替代的真实 `sgSuspendGameByPidUnlocked`，同时执行3个marker解析函数。26个正例与5种私有源码故障分别验证PID创建时间、强制单PID、marker写入准入、同Session及NT失败状态。17个直接依赖为14个显式内存端口和3个原纯parser，parser读取端仍是内存sgReadFile，NT指针只指向自有计数函数。6个危险外部端口在独立探针拒绝。编译/链接账本须含实际库行，Job结束后167归档原字节核验与短根所有权清理；早期缺链接日志记录不当完整闭包证明。

`fan.failed-read-identity-consumer` 执行原85行脚本的45个原场景，覆盖失败apply/preset/heartbeat后的旧读回遇到新曲线、OFF、Close、sleep、代次变化或resuming的退休处理。私有源码只去掉真实resyncSessionFromHost预读身份门；负例须完整45行，精确12个原第49行断言失败、其余33通过。原格式验证失败单列，完整Node assert.equal字符串由独立生成器和pin版本固定，不放宽语义或原期待。真实未改程序的V8覆盖证明webview.postMessage执行0次；原NoIoHost与launcher不调用真实Host/HTTP。

`controller.host-game-output-observation` 执行真实Host回调、当前tuple、256字节有界raw观察及应答追加；Release/Debug各23组观察和19组当前生命周期，10种故障须编译后命中对应原断言。原57项中3个历史协议期待失败继续列明。本case只证明raw事件消费，`protocolDecoded=false`、`gameFeedbackForwardingProven=false`，不能当马达解码、物理转发、仲裁或完整停止归还。

```powershell
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Profile full -Tests sleep.single-pid-freeze-consumer -EvidenceDir G:/YeManCC-Work/Build/Validation/single-pid-new-round
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Profile full -Tests fan.failed-read-identity-consumer -EvidenceDir G:/YeManCC-Work/Build/Validation/fan-failed-read-new-round
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Profile full -Tests controller.host-game-output-observation -EvidenceDir G:/YeManCC-Work/Build/Go
```

两条Coordinator原生/托管接通入口为 `controller.coordinator-lifecycle` 与 `controller.coordinator-native-managed-pipe`。前者本轮实际8个普通成功mode与4个预期源码负例，后者实际3场景/33检查，确认自有child退出0/5/5以及两次Dispose失败后的context保留与native quarantine。更新后的源码绑定前置必须重新真实构建，不能将旧pin改签。`controller.lifecycle-native-managed-pipe` 的原7场景双配置/两个协议故障仍单独验收。三者都只使用自有SDK-free子进程与私有管道，不启动正常Host/Main。原完整full容器继续INCOMPLETE_COVERAGE；selected AuditRegistered的CURRENT只属于当时范围与源码身份，后续源/pin改变即STALE，不反复追刷。


## 性能全局／专属与 Decky 同源回归

2026-10-21新增两个正式登记项：

- `performance.ownership-chain`：执行正式性能配置与常驻调度消费者，覆盖手动静态、自动AC/DC、唤醒实际归属、相同参数的全局/专属交接、旧专属参数变更等32场景。
- `performance.entry-parity`：执行主线Vue提交函数、发布的Decky控件与镜像动作，再进入同一个performanceSchedule；对照AC/DC、全局/专属、自动/手动的保存值和CPU/TDP调用，覆盖三秒合并、配置导入/解除、过期来源及排队中会话失效。

定向执行：

```powershell
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Tests performance.ownership-chain,performance.entry-parity,performance.float-runtime
```

这些测试的文件、IPC和硬件边界都在内存中；实际浮动循环沿用已登记的`performance.float-runtime`。产品核心、桥接、Vue/Decky控件和共享夹具均进入回执哈希。模拟通过不表示真实Steam传输、掌机写入或睡眠恢复已验收。

## 总库维护的独立选择入口（2026-10-21）

沿用既有 HC 总库维护自测，可以单独检查升级计划、消费入口、待解决问题和用户已确认的启用范围：

```powershell
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Tests lifecycle.capability-maintenance
```

该项复用 `tools/hc_capability_maintenance_selftest.mjs` 的原27个正反例。
包装入口固定原场景身份、顺序和子进程结果，保留原始结果，并核对所有已声明输入的前后哈希。
原自测读取的仓库文件必须在 `global/capability-maintenance/inputs.json` 中声明；新增读取需要更新声明，缺项不会自动获得通过。

本项归类为 `source-contract`，只检查总库与升级计划的合同。它不加载 HC 程序集、启动 Host、操作设备或执行系统睡眠。
27项通过不增加实际产品消费者的覆盖数，也不等于对应机型的物理能力通过。
风扇、灯光、电池、输入、陀螺仪与睡眠仍分别运行已有真实消费者夹具。
## 性能滑块草稿与长按调速（2026-10-21）

性能面板现在复用 `performance.slider-repeat`、`performance.slider-draft` 和 `performance.slider-native-repeat` 三项回归：

```powershell
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Tests performance.slider-repeat,performance.slider-draft,performance.slider-native-repeat,performance.ownership-chain,performance.entry-parity,performance.float-runtime
```

- `slider-repeat` 执行正式间隔算法、实际浏览器风扇节奏和节点/滑块消费者。编辑每次一步，起始55ms，持续400ms后线性缩短，2000ms时达到25ms；检查方向、释放、节点变化与隐藏/失活重置。间隔算法不拥有计时器。
- `slider-draft` 编译正式 Vue 面板和 Slider，并提取实际手柄编辑消费者，在1280×720/800覆盖刷新、旧读取、迟到保存回执、游戏/供电/专属归属变动、隐藏/KeepAlive、键盘与指针提交。默认在本轮产物中仅破坏实际消费者的编辑保护来验证负例；可用 `--baseline-panel` 指定历史面板复现原问题，不依赖外部历史产物才能运行。
- `slider-native-repeat` 编译实际原生发射器、编辑通知与输入消费者，外部窗口与IPC边界在内存中；检查普通导航220ms、性能滑块/风扇节点55→25ms、root/child准入、首按/连续/反向/失焦恢复，并运行3个变异负例。需要现有MSVC/Windows SDK，缺失应明确失败。

上一轮草稿回弹修复的定向运行器回执为 `Build/Validation/Performance-Slider-Draft-20261021/sim-final/result.json`（6个适配器，160个场景通过）。全量CLI清单目前被其他writer工具闭包上限阻塞，故定向执行使用相同 `runAdapters` 和已登记适配器，未改准入或放宽清单限制；独立全导航消费者另保留顶部菜单列对齐两项失败。本轮结论只覆盖定向真实消费者的内存/浏览器执行，不表示安装版或物理掌机验收。


2026-10-21后续用户修订：加速只改变间隔，每次保持一个刻度。已移除性能滑块的步幅倍率；native沿原输入循环加密编辑事件，浏览器fallback沿原RAF，键盘仅在明确按住期间拥有一个下一步计时器，释放/失焦/隐藏/取消/卸载即停止，OS repeat不叠加。编辑通知只在开始/结束时发送，普通页面导航、风扇控制意图和性能确认后提交规则不变。

最新定向回执为 `Build/Validation/Performance-Slider-Cadence-20261021/sim-final/result.json`：13项间隔/真实风扇消费者、78项实际Vue+engine浏览器回归、66项原生消费者，以及32项归属、22项YMCC/Decky一致性、8项浮动回归，共219项通过。原生另有3个变异负例，浏览器保留旧刷新回弹负例。性能导航16种配置另通过。全局CLI清单仍被其它writer工具闭包上限阻塞，使用原runAdapters定向执行；未放宽清单或准入。验证为浏览器/内存边界，不代表实际掌机手感或硬件写入验收。


## G3E 控制器状态、XYZ 与顶部程序切换（2026-10-21）

本轮复用原全局运行器，新增以下定向入口：

```powershell
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Tests gyro.presentation,gyro.xyz-fixed-layout,controller.virtual-output-status,controller.virtual-output-observed-ui,controller.task-view-foreground,controller.task-view-native-wiring,controller.task-view-actions
```

`gyro.xyz-fixed-layout` 编译正式陀螺仪页面和子组件，在1280×720/800检查符号、一位小数、固定数位与圆圈/总高几何。`gyro.presentation` 同时回归正式格式函数与原显示门。

`controller.virtual-output-status` 执行正式TS状态映射；`controller.virtual-output-observed-ui` 默认执行真实状态组件及实际ButtonMapping页面setup/状态标签，输入与无关子视图为明确内存边界。关闭虚拟输出不能推断物理陀螺仪/背键不支持；当前source/帧缺证为等待，检查候选不是终局，显式host/transport/fallback故障与隔离异常仍告警，同persona旧requestId回执不能覆盖新请求。可用 `node tools/virtual_output_status_selftest.mjs <新产物目录> --logs <日志目录>` 回放实测原始字段；未导出的完整gamepad.state不能被自行补成成功，现场回放与无外部日志的自包含正反例分开记录。

`controller.task-view-foreground` 编译正式窗口线程授权Broker，外部User32/COM均为FakeOps且真实导入被链接阻断；覆盖指定worker、标量token、250ms界限、超时取消/迟到/重复消息及失去前台/真实拒绝。`controller.task-view-actions` 执行正式Y菜单动作、engine激活、bridge和来源标记；只在原生请求成功后收起，失败保持YMCC及原有焦点恢复边界。原dispatcher与same-exe worker契约仍沿tools/task_view_dispatch_selftest.py、tools/task_view_api_wiring_selftest.py和tools/task_view_self_worker_contract_selftest.ps1验证。

证据根为 `Build/Validation/Controller-Status-G3E-20261021/`，`sim-final/result.json` 为六项定向通过；原全局CLI仍有其他writer工具闭包上限问题，本轮直接通过原runAdapters执行已注册适配器并保留来源指纹，不表示全局通过。浏览器、内存消费者、操作系统实际前台授权与实物掌机分别验收；本轮未控制真实YMCC、Steam或设备，也未导出安装包。

### TaskView 虚拟手柄授权反馈（2026-10-21）

`controller.task-view-native-wiring` 从正式 main.cpp 提取原生 producer，使用同一 dispatcher 接口，分别回放 disabled / Xbox / DualSense / SteamDeck 及授权接受/拒绝；档位只进入缓存诊断，不改变授权判断。执行 Release/Checked，禁止真实输入、进程和 COM 导入。`controller.task-view-foreground` 补充 `[stage32|error32]` 回执：guard、真实 API 拒绝、原始错误为零、传输失败及执行后发送超时须分开。传输失败时 API 是否执行保持 unknown；畸形回复不能放行。日志只在 dispatcher 结束后写，授权至 execute 之间不加文件 I/O。

现场原日志既有 virtual 开启时拒绝，也有开启时接受，不得用 fake 内的 UI/池线程策略声称 Windows 真机必然如此。以上是正式消费者的软件回归；G3E 的虚拟开启/关闭实机对照仍须由用户测试。

这三项 TaskView 适配器使用独立的 `foreground-grant-result.json` / `native-wiring-result.json` / `task-view-action-result.json` 保存消费者证据；运行器自身的 `result.json` 不覆盖它们。回执哈希在结束后再次核对。


## 小黄鸭配置开关与Xbox全屏返回游戏（2026-10-22）

新增三个既有运行器适配器：`controller.lossless-toggle-owner` 执行正式quickapp owner的内存文件/进程边界，核对停止LS后才删除当前游戏Profile、其它Profile/未知字段/备份保留、UIA不可读与记录的精确进程所有权、配置外部变化/退出失败不假报成功；`controller.lossless-decky-state` 执行正式镜像/观察消费者，覆盖配置开关与actual enabled分离、同key保留、跨游戏/电源清空、有限未决读取、超时与迟到/销毁；`controller.lossless-return-focus` 编译正式原生consumer及main.cpp提取的登记、返回、定时器和电源入口，FakeOS验证精确原游戏与3次/10秒、已是游戏不请求、用户换应用/新呼出/停止/电源/身份改变/退出取消。

开关以Profile.AutoScale配置为准；程序未运行、实际UIA回读未知独立提示，不声称物理插帧已经生效。Decky仍由YMCC原owner进行文件及进程操作，未新增另一路配置或Steam接口。read-only观察不自动重放设置。临时焦点consumer为一次显式return episode，平时只有静态待返回身份，没有常驻计时轮询。

真实顶部GameQuickActions、SwitchControl及正式engine的720/800检查复用全页`controller.ui-navigation`：trainerMenuCases中`lossless-xbox` seed以显式memoryIPC模拟UIA不可读，实际执行owner开启、关闭、删当前配置及native目标桥接参数。原LS控制、Decky镜像与Windows焦点均分别记录软件与实机范围。证据在`Build/Validation/Lossless-Toggle-Xbox-20261022/`，首轮UI源码门因测试期间losslessScaling.ts更新保留FAIL，后续稳定源码重建通过；不得用旧源码回执放行当前导出。

定向结果仍通过原runAdapters，不放宽全局闭包上限或覆盖其它原生/Steam/生命周期门。完整产品交付须按当前源码再核对发布门；本次未部署或操作安装版、真实LS/Steam/游戏。
