# 现有夹具的按需入口

本目录只负责选择、身份及证据引用，执行复用 `global/runner.mjs` 的 `executeAdapter`，不复制测试实现，不改原断言、负例或时限。

统一入口：`G:\YeManCC-Work\Testing\YMCC\Run.ps1`。

```powershell
# 默认只是计划；模块最小集合与覆盖范围写入 plan.json。
& G:\YeManCC-Work\Testing\YMCC\Run.ps1 -Optimized -Modules fan
# 首次/身份变化才执行；同身份的有效旧回执单独引用。
& G:\YeManCC-Work\Testing\YMCC\Run.ps1 -Optimized -Modules fan -Mode run
& G:\YeManCC-Work\Testing\YMCC\Run.ps1 -Optimized -Modules gamepad -Mode run
# 同一孩子被多个模块引用时，在这一计划中只保留一次实际执行。
& G:\YeManCC-Work\Testing\YMCC\Run.ps1 -Optimized -Modules sleep,lifecycle
# 直接选择既有ID；动态工具闭包未审清时默认NOT_RUN，不自动无限重跑。
& G:\YeManCC-Work\Testing\YMCC\Run.ps1 -Optimized -Tests steam.shared-mode-source -Mode run
# 已明确要求重现某项时，只强制这一项；不伪造缓存或验收通过。
& G:\YeManCC-Work\Testing\YMCC\Run.ps1 -Optimized -Tests steam.shared-mode-source -Mode run -Rerun
# 只有明确完整验收时调用；交回原全局生命周期/覆盖门，全部真实执行。
& G:\YeManCC-Work\Testing\YMCC\Run.ps1 -Optimized -Mode full -FullAcceptance
```

`-Tier minimal` 为默认；`-Tier module` 选择这个模块原登记的全部用例，应先生成计划。`-Tests` 和模块最小集合可以共同选择，重复ID按原定义合并，定义冲突会拒绝。未知ID、模块、空profile交集或证据目录越出G核心层不能悄悄变成通过。

`modules.json` 登记18个模块的最小入口，每个通常一至两个原ID。它是快速定位的局部集合，不是该模块完整能力、native生命周期、真实睡眠或设备验收。完整验收保留原始要求，不能用最小集替代。

身份包括真实文件及工具SHA256、执行参数、配置、实际bundle依赖、工具图和解析目录成员。TypeScript复用原登记审过的依赖图，并消费原执行器采集的完整工具/实际导入闭包；纯Node仅三个已审查的源码合同允许缓存。其余动态Node/PowerShell/外部工具链保持 `NOT_RUN_UNPROVEN_CLOSURE`，需补齐闭包或明确 `-Rerun` 才执行该项，不自动触发大门。可缓存范围是明确限制，不冒称所有legacy夹具已经完成增量化。

索引在核心层 `State/ExecutionCache/<原ID>.json`，同ID每次更新同一索引；日志/实际回执继续写核心证据层。索引只引用原result、stdout/stderr、结构化回执和产物的路径/SHA。丢失、坏证据或身份漂移使引用失效；并发同ID持有锁时写NOT_RUN_BUSY，不重复开进程。遗留锁不自动按PID猜测删除。

结果分清以下口径：

| 状态/字段 | 含义 |
| --- | --- |
| `PASS` + `executedThisRun=true` | 原进程本轮实际执行通过 |
| `CACHED_REFERENCE` | 同身份旧PASS回执引用；本轮执行/通过均为0 |
| `UNCHANGED_FAILURE_REFERENCE` | 原FAIL/UNKNOWN等原样引用，整体仍失败/未完成 |
| `NOT_RUN_*` | 没有执行，原因保留 |
| `INCOMPLETE_IDENTITY_DRIFT` | 运行中身份变了；旧原件保留，不能计通过 |

`selectionMs` 是选择成本，`identityMs` 是初始身份/引用检查；每行 `durationMs` 是原adapter总成本（可能包含编译、执行和原证据校验），`wallMs` 是整轮总时间。父耗时包括子耗时，不能重复加总；当前原工具没有拆开的编译段继续记未知，不编造分段收益。

后续新增规则见同层上级 `FIXTURE-POLICY.md` 和 `AGENTS.md`。本轮没有运行完整验收或修改产品代码。

<!-- PORTABLE-BUDGET-CONTROLS -->
## 预算执行器的定向维护入口

源身份pre/post按64KiB原字节读取，在每文件／块前后检查同一截止。pre耗尽不启动worker；post耗尽保留内部真实结果，外层TIMEOUT且不报本轮通过。release共享额度在结束阶段超额时，外层返回124，内部Job成功与原receipt保持其真实值。单次同步文件操作、JSON解析／最终写入及锁释放仍不可抢占，不能把下列小组通过写成完整30分钟已经证明。

在真实主线运行以下原入口即可，不依赖私有轮次清单或新增环境。两个延迟边界明确选择才生成180个257B样本，默认不加入原套件或minimum；测试结束恢复文件API。无效选择在注册测试体前拒绝。示例保留调用方筛选变量：

```powershell
$fixtureNode='G:/YeManCC-Work/Environment/Toolchains/nodejs/node-v22.14.0-win-x64/node.exe'
$fixtureRepo='G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC'
$fixturePriorFilter=$env:YMCC_BUDGET_CASE_FILTER
try {
  $env:YMCC_BUDGET_CASE_FILTER='["source identity pre scan stops at the original deadline before worker admission","source identity post scan cannot authorize PASS after the original deadline"]'
  & $fixtureNode --require "$fixtureRepo/tools/fixtures/execution_budget_identity_delay.cjs" --test --test-reporter=tap --test-concurrency=1 "$fixtureRepo/tools/ymcc_sim/optimization/execution-budget.test.mjs"
} finally { $env:YMCC_BUDGET_CASE_FILTER=$fixturePriorFilter }
```

共享closing边界复用`release-full-budget.test.mjs`，选择环境变量`YMCC_FULL_BUDGET_CASE_FILTER`的精确原名`shared release quota rejects closing overrun while preserving the inner successful receipt`；原累计额度正控名为`Build and Package consume one cumulative quota; a successful stage cannot silently run twice`。该入口不自动执行产品构建或完整发布。证据分别在核心层`Evidence/ReleaseClosingQuota-20261103`及`Evidence/BudgetPortableControls-20261103`；前者15.772s/20s，后者4.704s/10s，范围不能相加冒充整体验收。
<!-- /PORTABLE-BUDGET-CONTROLS -->

<!-- REQUIRED-PARENT-FAILURE-CONTROLS -->
### 原persistent父链的失败停止控制

原`current-parent-global.mjs --mode run`默认在必需动作异常或false断言后停止，后项NOT_RUN；手动诊断才显式`--collect-after-failure`。原预算、负例及通过时的完整覆盖保持。停止时原来源/历史/已执行记录继续封存，不能将部分通过当整父通过。

执行控制复用原`current-parent-global.test.mjs`，仅明确筛选才运行新5项，不加载原测试体的HANDOFF/r5历史初始化，也不执行native/SDK/原baseline。示例保留调用者变量：

```powershell
$parentControlNode='G:/YeManCC-Work/Environment/Toolchains/nodejs/node-v22.14.0-win-x64/node.exe'
$parentControlRepo='G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC'
$parentPriorFilter=$env:YMCC_PARENT_GLOBAL_CASE_FILTER
try {
  $env:YMCC_PARENT_GLOBAL_CASE_FILTER=@(
    'required-stage guard preserves successful stage order and return values'
    'required-stage guard rethrows the original required exception before later stages'
    'required-stage guard stops on a newly recorded false semantic case without an action throw'
    'required-stage guard explicit collection retains failures and continues later stages'
    'required-stage guard continues after an expected negative handled inside the original action'
  ) | ConvertTo-Json -Compress
  & $parentControlNode --test --test-reporter=tap --test-concurrency=1 "$parentControlRepo/tools/ymcc_sim/global/decky-persistent-lifecycle/current-parent-global.test.mjs"
} finally { $env:YMCC_PARENT_GLOBAL_CASE_FILTER=$parentPriorFilter }
```

未知/空/null/重复筛选在控制体前拒绝。原无筛选入口保留历史依赖和旧控制体；实际5项与4类拒绝证据见核心层`Evidence/RequiredStageFailStop-20261103`，累计1.611s/10s，仅为执行控制，不是完整父链验收。
<!-- /REQUIRED-PARENT-FAILURE-CONTROLS -->

## 2026-10-30 缺陷优先与整轮截止

`Run.ps1 -Optimized` 默认使用共享 1800 秒墙钟预算，目标覆盖同步选择、依赖／工具检查、编译、执行、结果及清理；可用 `-BudgetSeconds 20` 缩短，不能大于1800。直接 CLI 对应 `--budget-ms`，范围1000～1800000。原用例时限继续有效，实际进程时限取原值与本轮剩余值的较小值。

预算使用原 `global/host-process.ps1` 的自有 Windows job，实际优化worker及后代归入同一归属；外层启动前／退出后的源身份扫描按原截止逐块检查。默认预留最多两分钟收尾，短预算按比例预留；超时回执为 `TIMEOUT`，部分证据保留，未完成不能计PASS。`execution-budget.json` 记录父墙钟和原结果状态；普通源码身份变化会撤回本轮成功。此机制没有添加第二套测试或业务断言。

当前共享截止覆盖核心 `Run.ps1` 的优化入口和原global/fan/gamepad/sleep/msi/full/doctor域。`-Route`仍只显示路由，不启动测试。旧域保持原参数及原退出码，预算回执只记ORIGINAL_DOMAIN_EXITED/PLANNED等进程范围，不把退出0冒充全部用例PASS。直接绕过核心入口调用底层脚本不在这项统一预算路径内。

计划读取原登记元数据，不准备无关signal/music/residency工具；执行只物化选中的原工厂。索引stale时也保留选择与实际group拓扑校验，不再物化无关组。`decky` 已成为独立的最小模块，选择原 `plugins.decky-owned-runtime-lifecycle`，不再别名到壁纸；动态闭包仍需显式定向重跑，不声称其已通过。当前18模块含23个唯一最小ID。

普通发布继续FastRelease，HC升级和CPU／功耗保留专项，不自动加入发布。修bug用原失败ID与直接受影响消费者；发布必要bug列表须有具体缺陷与场景依据，不能把整个performance/HC域绑回发布。

## 必要缺陷计划与发布接入

显式计划只引用原ID，不建立第二套用例库。格式：`schemaVersion:1`、`kind:"ymcc-release-bug-plan"`、`issues:[{id,reason,purpose,testIds}]`。只有 `purpose:"bug-regression"` 入选；`hc-upgrade`、`cpu-optimization`单列为未选择专项。CPU/HC名称不能排除真实功能bug。重复原ID只执行一次，保留多issue引用；未知入选ID拒绝，不用整个域或通配补齐。

先计划：`Run.ps1 -Optimized -Mode plan -BugPlan <计划文件>`。定向实跑：`Run.ps1 -Optimized -Mode run -BugPlan <计划文件> -Rerun -BudgetSeconds 1800`。不允许同时传Modules/Tests/full扩大精确缺陷计划。空计划或纯专项写NOT_RUN_NO_RELEASE_BUGS，执行与本轮通过均为0，不自动补默认最小集。

发布器新增显式 `tools/publish.ps1 -FastRelease -ReleaseBugPlan <计划文件> -FixtureBudgetSeconds 1800`。只在编译后、打包前执行一次原ID定向回归；失败、超时或缺证停止这次明确要求的缺陷验收。普通FastRelease不传计划时行为不变，完整夹具仍NOT_RUN。发布必要缺陷回执在核心Evidence/Build/release-bugs，不能冒充完整验收；调用前应先用计划入口核对实际范围。CI显式运行需已有合法的核心输出/工具上下文，不自动下载或生成全部测试环境。

`release-bugs.mjs`只做纯选择；执行仍复用原CLI、executeAdapter及同一预算。当前控制计划只在Evidence下，用来验证编排，不是虚构的下一次发布缺陷清单。具体产品bug须由当前任务或日志证据交接，再填准确原ID及参数。

进程包装层已固定UTF-8，中文、符号和原参数在新/旧程序入口中均有定向回归。原全局规划缺TypeScript的FAIL原件保留；当前同因明确报BLOCKED/10与缺少的AST包，不执行测试。睡眠日志回放已不再导入不用的AST目录，原S0-joined回放仍如实INCOMPLETE/10且所有实机claims为false。离线睡眠分类缺历史S3证据的CHECK_FAIL保留，不造证据或删断言变绿。

## 旧睡眠分类的范围

原 `-Domain sleep -Mode sleeptypes` 默认仍为all，保留本机能力、历史S3输入与全部原golden断言。历史证据缺失继续FAIL，不能以离线测试通过替代。

只做分类软件回归时明确调用：`Run.ps1 -Domain sleep -Mode sleeptypes -ExtraArgs '-TypingScope','software'`。这条路径复用原7个golden样本及类型/样本集断言，共9项，不查询powercfg、不读取历史S3；回执明确typingScope=software、fieldTyping=NOT_RUN，不代表现场或真实睡眠通过。没有新增业务ID或默认扩大全局最小集。

迁移后的真实历史输入可在all范围提供成对 `-S3EvidencePath` 与 `-S3EvidenceSha256`。缺一、坏hash或software范围提供现场输入会拒绝；哈希只绑定所选字节，不把历史输入升格为当前物理验收。原路径/原失败保留；归档检索无结果时不重建证明。

预算结果现在校验本轮worker格式和owned-job对应；FAIL/TIMEOUT/INCOMPLETE即使子进程exit0也不会返回成功。结果缺失或损坏不能借运行前计划冒充完成；两种合法旧计划、空缺陷计划和full委派保留兼容。外层sourceClosureComplete=false只表示部分来源闭包，行为断言仍由原adapter负责。

分类范围的持久回归位于 `domains/sleep/lib_sleep_typing.test.ps1`，复用原函数与原golden，不新增业务适配器，也不进入默认发布。它验证software不查询平台、坏golden可检出、路径/哈希成对、software拒绝现场输入、坏历史哈希阻止分类；声明输入及实际PowerShell文件身份写入G核心证据。可通过原host-process护栏单独运行，原预算20秒。旧临时控制脚本仅保留为历史证据。


## 缺陷计划的运行范围与候选输入

`ymcc-release-bug-plan`可选顶层`profile`（desktop/full/sleep）和`candidateInputs`。有入选功能bug时才应用；空／纯HC升级／CPU优化计划不验证或加载其中的专项候选输入。所有必测原ID必须在所选profile中且可解析，不能筛掉一部分后按剩余项放行；profile=full只影响所列ID的可用性，不展开全套。

候选白名单沿用原消费者：payloadRoot、runtimeRoot、sharedRuntimeRoot、lightingPayloadRoot、legacyHostRoot、fanToolLoadPlan、completeHostContract，以及expectedHelperSha256、fanToolLoadPlanSha256、hcProfile、hcVersion。路径使用绝对路径；成对输入和哈希继续使用原校验。计划不能指定程序、任意args、输出目录或预算。显式命令参数与计划冲突会拒绝；相同值可复用。核心Run.ps1未明确指定Profile时让计划选择，避免自动desktop覆盖计划full。原ID的适配器与执行器保持唯一。

例如修复当前Host问题时，计划可声明`profile: "full"`及`candidateInputs: {"payloadRoot": "G:/YeManCC-Work/Build/实际本轮Host目录"}`，并只列该缺陷的原testIds。路径示例不是可执行交付；必须填写确切当前产物。计划和软件测试不能代表该Host或所有机型实机通过。


## 固定准入与无关目录变更

原入口计划／运行均调用selection-policy.mjs：全登记ID唯一，minimum每模块固定1～2项，配置不能提高上限；跨模块引用同原ID只实际执行一次。模块目录修改仍记录配置SHA并校验本轮来源，但单纯修改未选择模块说明不会成为原用例的行为依赖，不会导致无关用例重复执行。实际adapter定义、源码、动态依赖、工具、参数及运行配置的变化仍失效对应缓存。

本轮最终实跑：`Run.ps1 -Optimized -Mode run -Modules fan,controller,display -Tests steam.contact-source -Rerun -BudgetSeconds 300`，5项实际通过，约22秒；这是明确所选软件行为／源合同，不是完整模块或实机验收。另用无关audio说明变更确认4项缓存引用，执行与实际通过均0。原失败、元数据控制、真实回执和成本比较位于核心层Evidence/FixtureIntake-20261030。

当前所需TypeScript/Vue/esbuild测试闭包从主线pnpm-lock精确版本及SRI准备于核心Workspace/NodeTools，源树node_modules通过Junction正常解析；原736文件工具身份门保持，没有用旧Backup或关闭原检查。产品package.json/pnpm-lock未修改；这些工具不表示完整产品构建依赖已准备。


### 完整发布的累计夹具预算

`tools/publish.ps1 -FullAcceptance -FixtureBudgetSeconds 1800`为灯光原lighting.signal-consumer、Decky Build和Package分配同一份累计墙钟。下一阶段使用剩余量，不重置；原灯光240秒更小上限继续保留。直接完整Build也初始化会话，独立Package／Verify消费BUILD.json的fixtureBudgetSession。会话、每阶段execution-budget、拥有Job回收与原step计时均写原回执或核心Evidence/Build/full-release-budget。重复阶段、缺Build会话、FAILED／EXHAUSTED／中断锁拒绝自动继续。

release-full-budget只接原发布门和固定原灯光请求，不新增业务ID／minimum，不自动调用完整验收。普通导出／FastRelease不启用；正常编译打包另计。退出码与TIMEOUT、失败、来源漂移不被计为PASS；模拟控制验证范围、真实全链结果及现场分别报告。


### 原发布baseline同批复用

Decky Build原recovery required step由persistent父门保留完整原240秒入口并执行一次，随后原发布门用release-baseline-reference验证当轮父／global／process／raw／源／工具／声明环境和metadata。成功记录SHARED_REFERENCE、originalProducerExecutionsByThisReference=0，验证成本单独计；失败或跨轮不能复用旧PASS。alias未来新增逻辑会因reviewed SHA变化被拒，必须重新核验新增覆盖。不会删除owned组件或Package的Build证明／真实ZIP安装回滚。

实际两ID的parent57／owned40覆盖存在共享，不相加成97个独立全局测试。原完整发布尚未实跑，局部成功及1800秒上限不能宣称完整全量30分钟完成。原R1 FAIL与库侧source mismatch按原件保留。


2026-10-31 包维护的同批共享：原 `decky-package-maintenance` 保留为发布要求，由 persistent 父门中原 baseline 执行同一完整生产者一次。普通32项与三个原故障注入负例保留，不以缩小范围替代去重。父门及全局最终通过后，原 `release-baseline-reference.mjs` 一次验证同时供 recovery 与 package-maintenance 两项引用；验证墙钟只记一次，包维护引用不启动第二个生产者、不计新增实际PASS。父门失败时两项保持 `NOT_RUN_DEFERRED_TO_PARENT`。

包维护生产者另记录原 process/result/stdout/stderr、真实cwd、有限声明环境、编译authority文件列表和三份原负例FAIL的路径/大小/SHA256。共享核对精确原参数与30000/25000/5000ms预算、当前时间范围、源码/工具无漂移、完整普通项、三次语义拒绝及子Job回收；不能只凭外层PASS、同名脚本或工具数量共享。WinPS无选择参数的空串与null按同义处理，真正的筛选参数仍拒绝。原嵌套命令的NonInteractive及完整参数必须匹配；与旧外层不同的工作目录/ArtifactRoot只作已审自有输出与无关环境处理，不把旧记录缺失的身份补造成新证据。

旧回执没有新增包绑定，不回填历史；父门INCOMPLETE也不能转PASS。此次去重仅减少一份重复的原包维护调用，不声称完成整链提速或全量30分钟通过。普通导出默认、HC/CPU专项、模块minimum及业务ID保持原规则。

<!-- FULL_RELEASE_RAW_LAUNCHER_SCOPE -->
显式 FullAcceptance 的原阶段 wrapper 会把当前已安装 PowerShell 7 Application 的准确路径传给既有 RawProgram 消费者，使当前父首生产器及资源 cache/worker 的已实现并发路线能在正式入口生效；调用者已指定的路径不覆盖，阶段退出（含失败/超时）恢复原环境。声明/SHA、原 Job 回收和1800秒累计预算保持，普通导出/Fast不进入此阶段。该接线通过隔离控制，不能冒充完整父或发布验收成功。正式发布的指定灯光/Decky及原必要回归图，与全局全ID或HC/CPU专项图分开；不得把全局历史244计数误报为每次发布必跑244项。
<!-- /FULL_RELEASE_RAW_LAUNCHER_SCOPE -->
