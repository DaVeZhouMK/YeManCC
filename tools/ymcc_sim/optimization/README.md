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

`modules.json` 登记17个模块的最小入口，每个通常一至两个原ID。它是快速定位的局部集合，不是该模块完整能力、native生命周期、真实睡眠或设备验收。完整验收保留原始要求，不能用最小集替代。

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
