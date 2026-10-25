# Decky 持续能力生命周期候选（2026-10-22）

独立 adapter ID：`plugins.decky-persistent-lifecycle`。导出函数是 `getDeckyPersistentLifecycleAdapters(repoRoot)`；不修改 runner、catalog、registry 或原 `decky-update-recovery` 目录。公共门依赖为同级 `../capability-lifecycle/index.mjs`，由主代理维护。

本候选执行真实产品消费者，边界上的 Steam/电源/IPC/socket/设备信号采用内存回放。原生只启动本次编译的测试镜像自身；生产 Runtime 实际创建 Job、子进程和 worker event，并执行关闭。正式 Loader 仅校验字节，不执行。

## 分类与实际证据

| 子链 | 分类理由 | 实测资源和事件 |
| --- | --- | --- |
| `native/decky_sidebar_runtime.h` | 用户 ON 后跨 Steam/transport/Ready 事件保有所有权 | 真实自有 process、Job、worker event；退出调用返回后的 worker join 和句柄关闭；崩溃/关闭后请求消费 |
| `src/bridge/deckyMirrorHost.ts` / `deckyMirrorRelay.ts` | 订阅跨多次业务状态变化存活 | 原 fixture 的 IPC、game、fan、schedule、watcher 计数；`power.pending` / `power.resumed` 的实际快照推送 |
| `decky-plugin/src/mirrorClient.ts` | 显示/被动需求期间保有传输及恢复状态 | 内存 socket 边界上的实际 client，虚拟时钟恢复 timer、关闭后的保存回调投递 |
| 各次 performance 写请求 | 单次动作，属于持续父能力的写入边界 | 睡眠态由正式 client 和正式 relay 拒绝；未发生内存 owner 更新或重放 |

父能力 `plugins.decky` 因实际持续资源和跨事件行为声明为 `persistent`。这不是对全部设置按钮分别宣布持久性。

## 执行

从已包含本候选、公共门和原产品源码的固定副本根运行，证据目录须全新且在源码树之外。Windows 路径应尽量短，原 native no-IO 测试会自行派生较深的路径。

```powershell
node tools/ymcc_sim/global/decky-persistent-lifecycle/run.mjs --evidence-dir D:/owned-evidence/r1/decky-persistent-lifecycle
```

当前候选尚未登记到共享 catalog，因此不能把 global `-Tests plugins.decky-persistent-lifecycle` 宣称为已可用。主代理可决定如何引入返回的 adapter；此任务没有做登记。默认执行会保留原 `plugins.decky-update-recovery` 的整套场景及其 240000ms 原预算，然后执行原 sidebar 自测和本候选探针。原场景的行为负例仍由原生产者原断言判定，缺 terminal 回执不会算检出。原场景失败会继续运行本次具名探针，且总体保持失败。

`--baseline-result <已有原场景result.json>` 仅供复用同源码的原场景原件：必须原件 passed=true，且逐项重新核对原件全部 sourceInputs SHA；来源和原件 SHA 记录在结果中。正式交付的默认整套命令没有使用此参数。此参数不复用新增生命周期结果。

## 新证据及拒绝条件

结果包含原始 `cases`、`sourceHashes` 字典和带公共门 `contractFingerprint` 的 `lifecycleEvidence`。每项 observation 只指向一个实际执行/聚合 case，聚合 case 明列 componentCaseIds。源代码、输入故障及实际测量分别保留。

- `destroyOnExit`：真实自有子进程退出被 Win32 wait 观察；Runtime 持有的 process/Job/event 关闭且 worker 已返回；JS 实际 timer 和 observer 归零。
- `destroyWhenStopStalls`：分别注入 owned `TerminateJobObject` 失败和不响应取消的 binding 回调。前者真实 `KILL_ON_JOB_CLOSE` 兜底不得伤及同镜像的独立 sentinel；后者必须在测量期限内完成撤销并清理，否则 FAIL。夹具最终释放回调只为结束本次测试，不算产品兜底。
- `preventResurrectionAfterExit`：实际保存的 client timer、socket 事件、延迟 attach Promise 及 native recovery API 在退出后投递；新建进程/socket/资源不能增加。
- `explicitOffStaysOff`：原生用户 OFF 与正式 fatal policy + fault OFF 都投递迟到事件；OFF 不要求控制器监听销毁。其余资源清理证据是额外测量，不扩大公共门对 OFF 的要求。
- `recoverAfterSleep`：执行原 main.cpp committed-Ready 消费段；Sleeping/Resuming 不触发绑定，Ready 只接受一个 generation rebind。正式 host → relay → client 在内存电源事件后自动推送当前 runId / generation 的新 ready 快照；期间写入零、Ready 后显式读取请求零。`maxAttempts=1` 指每个提交 generation 的 rebind 上限，不替换通用连接退避预算。
- `recoverAfterTransientFailure`：一次实际 relay read 异常后，后续 read 边界已可返回同轮正式 host 产生的健康快照。只有自动重试实际执行并消费当前健康结果才能 PASS。另测 source-churn 的原两次恢复预算，不能把该路径的成功替代读取异常恢复。
- `respectRecoveryLimits`：生产 Runtime 实际启动五个快速退出的自有测试子进程；达到原上限后，recovery、unknown 状态和重复事件不能续命，用户 OFF 继续优先。原崩溃自测保留明确重新开启/已验证新会话的合法预算刷新检查。

具名负例包含 `negative.connected-without-ready-snapshot-is-not-health`、`negative.recorded-pre-sleep-ready-is-not-current-generation-health`、`negative.relay-source-churn-exhausts-original-two-retry-budget`。前两项分别实际观察 connected 但未 ready，和回放此前真实生成的旧代 ready 快照；不能计为当前健康。

## 原发布门关联及覆盖边界

既有 `tools/decky_update_release_gate.ps1` 的 Build 阶段通过 `Invoke-Required decky_update_recovery_selftest` 进入 `tools/decky_update_recovery_selftest.mjs`，后者 import 原 `global/decky-update-recovery/run.mjs`。本候选默认执行同一原 producer 字节，并额外执行原 official-loader、safety、client、host、hang、stability matrix 及 native runtime/recovery/health/health_close/crash_breaker 消费者。本候选不等于完整 release Build，也没有执行或修改该 Build 门。

新增生命周期只有在新 adapter 被实际执行、原 case 通过、相同源/候选/公共门 hash 一致且新观测满足合同后才可通过。旧 `BUILD.json` 或原 `plugins.decky-update-recovery` PASS 不能补齐本合同。当前产品缺口必须保留 FAIL；不得为放行而重标分类、伪造 ready JSON 或删除负例。

这是原生 Runtime 与正式 TS 消费者的分组件软件回放，包含真实的自建子进程销毁；不是实际 Steam/Loader/安装版 YMCC 的端到端恢复。host 的 native provenance 来自明确的内存 peer 边界，PID 不是现场进程健康证明。未执行真正电源广播、OS 睡眠、设备操作、官方 Loader/Steam 页面恢复、实物手柄、整包 Build/Package/Verify 或线上最新版本核验。未覆盖的上下文 lease/实际 CDP socket 全进程销毁不能由本探针的资源零推断。暂存目录在 owned Job 故障时可按产品策略保留；报告的资源类别是持有中的 process/handle/worker/timer/subscription，不把保留文件当活资源或当安装产物。


<!-- DECKY-BINDING-GENERATION-CANCEL-20261022 -->
2026-10-22 补入native.binding-cancel-network，直接编译tools/decky_binding_cancel_selftest.cpp：真实Runtime、正式AsyncRequest/CancelSignal、无响应HTTP/WS回环对端，以及仅本轮源码构建的惰性自有子进程，确认1500ms内退出、callback owner释放和晚到恢复不复活。测试代次替换取消由原bootstrap回归覆盖，未改无合作回调原场景或公共acceptance组合；新增具体网络PASS仍不能替代原stalled-stop失败。本轮49项46通过3相关FAIL、公开门INCOMPLETE。主程序取消实现现明确signal旧代token，不新增轮询。
<!-- /DECKY-BINDING-GENERATION-CANCEL-20261022 -->

## 当前生产独立绑定进程补充门（2026-10-23）

`plugins.decky-owned-runtime-lifecycle` 使用未修改的 `tools/decky_binding_runtime_selftest.mjs/cpp`，直接执行当前生产 Runtime、Client、Owner，以及本轮源码构建的惰性 Loader、CEF 和协议对端。原 3200ms 卡死退出界限、测量后才释放阻塞、严格进程句柄相等、无额外读取线程、子孙进程退出和当前 OFF 拒绝条件保留。它是现有持续类 Decky 功能的原生组件，不独立归类为一次性能力，也不替代原全局生命周期、TS 健康恢复或旧回调回归。

`owned-run.mjs` 校验原三项完整 case 身份、原失败、源码冻结成员与哈希、真实编译依赖图、工具声明、自然返回、生成镜像及运行前后源码/工具身份。补充 SDK/MSVC 输入只精确登记于 `owned-tools.json`；原 NoIO 工具门不变。原始 `typed-runtime-result.json` 和 `RESULT.json` 原样保留；公开聚合只可在全部原断言通过且身份完整时 PASS，不能把 owner 资源零、部分 case 或重复诊断成功提升为整体通过。

首次实际当前源码执行为三项中 1 PASS、2 FAIL：所有 owner 资源已归零，但两个场景的进程总句柄计数增加。独立重复诊断在同一进程保留原断言执行四轮共 12 项，首次仍有两个失败，后三轮九项通过；这只能定位首次初始化成本，尚未证明新增句柄归属，不能放宽计数或清除失败。原始与诊断证据保留在核心层 `Evidence/DeckyRuntimeResume-20261023`。本组件未验证安装版、实际 Steam/Loader、真实睡眠、现场恢复、功耗或整包发布。

后续生产原自测已增至五项：原三项加上正式 Ready 重绑和 retained binding-only 重绑。补充门按五项精确注册顺序和原返回/异常 ID 校验；原三项历史失败不改。全局长证据路径曾令原 LoaderTemporaryRun 的实际 Win32 临时路径达到280字符，原自测因此保留 startup FAIL。现在原自测仍原样执行于项目核心 Workspace/DR/<唯一8字符>，原始产物和回执留在该实存拥有目录，公开 result 记录绝对路径、哈希、参数与来源。此举不改变生产路径/断言/预算，不恢复旧D根目录，不把缺产物当通过。

owned Runtime组件现显式声明持续父功能 plugins.decky 的归属、实际Runtime/Client/Owner源码、驻留子进程/worker/handle/待决IO及case-local结果，并绑定公共生命周期门源码。组件通过不补齐旧完整父夹具缺少的TS恢复或其它七项观测；父合同尚未接入/执行时保留componentIntegrationGaps，不把组件改列一次性或误报为未判断的新功能。

当前原producer新增“最终启动准入拒绝时，OFF之前必须释放绑定watch”的原生回归，已评审并接入第六项。包装器以单一注册名/结果名映射表派生数量，仍拒绝缺场景、错误身份或未评审的新source注册；此前三项、五项与R4版本拒绝回执均保留。生产producer与原断言不改，预算不变。


<!-- DECKY-ASYNC-CLEANUP-CANDIDATE-20261023 -->
2026-10-23：父门增加current.typed-async-cancellation-regression，调用真实原静默HTTP/WS消费者并保留所有原预算/断言。6组取消通过仍不能越过校准或父句柄不相等。Client最多150ms协作取自原stop总预算；Job最后关闭活后代原断言仍失败，候选未放行。原25选定入口、原完整240秒树和旧失败均保留。正式生产参数冻结main优化对象通过不代表当前完整主线/安装验收。证据：G:/YeManCC-Work/Testing/YMCC/Evidence/DeckyAsyncCleanup-20261023-1300/REPORT.md。

## Typed AsyncRequest 所有权 v2（2026-10-23）

`typed-owned-async-contract.json` 明确区分当前产品的 worker 隔离生命周期与 v1 测试父进程直接调用 WinHTTP 的库级自然归零命题。正式父门每次仍运行、解析并保存 v1 的原始 verdict；v1 冷校准或父句柄差异不会被改写为 PASS，也不能被新结果反向覆盖。若 v1 的原 typed callback 场景、直接控制或原历史回归另有失败，父门仍拒绝。

v2 原生入口是 `typed-owned-async-request-regression.cpp`，驱动为同名 `.mjs`，行为重算器是 `typed-owned-async-native.mjs`，完整来源/编译/进程/原件校验器是 `typed-owned-async-parent.mjs`。它逐字生成并复用 v1 的原场景及直接消费者，只对生成副本的 main 改名；不改 v1 正式源码。原 6 行 × 12 顺序谓词、2 个直接控制、4 个负例全部保留。4 个冷 probe 与 2+4 控制共 10 个有限自有 child 使用现有 Owner/ChildChannel、精确镜像/PID/birth/session、随机 nonce、先入 Job 后 Resume、受限继承 pipe/cancel。

每个 child 在仍存活时记录真实 HANDLE_CLOSING 与 weak-owner 释放，父读取该证明后 ACK 才允许自然退出。原 off+1500ms 这一截止同时覆盖自然退出、Job active0、Owner/观察句柄释放和严格父句柄/线程相等；renew 700ms、native 20s、outer 135s、完整父门480s不变。强杀、仅 child exit、reported closed 或零 pid 都不能代替回调释放。私有实际负例分别保留父观察句柄、保留 callback 强引用，必须失败；复制真实回执的篡改控制另验证原件/身份/时钟/预算/资源计数。

测试父的 loopback 服务与公开握手加密初始化单独运行并记录两轮 before/after 句柄表，第二轮严格相等；不启动 Runtime/Client/WinHTTP 来预热，不保留 session/actor，不减任何资源偏移。当前已观察的冷初始化最多是9个 ETW、1个 Event、1个 KsecDD File；未知类型/访问权/计数或基线丢失均拒绝。

v2 仅能证明产品 typed worker 与父资源的所有权合同，不能证明一个仍存活的同进程 WinHTTP 调用会在1.5s内自然销毁所有 Windows 库状态。后一命题继续由 v1 诊断和其实际失败原件表达。独立75秒 WebIO NtClose 来源调查不进入正式预算、不作产品豁免，不允许私有回收 API、隐藏 anchor 或共享 session。实际安装/Steam/物理睡眠仍须另验。
