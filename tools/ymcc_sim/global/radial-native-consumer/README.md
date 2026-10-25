本目录的公共入口已升级为 V2：在既有唯一 controller.radial-native-consumer 中，保留 post-create 原12+2，并追加 mandatory main-return 12+2。本次只完成公共port和有限静态检查；尚未编译/运行新的V2 native，也未运行global producer/audit。父端负责下一次唯一正式执行。

当前工作仅收尾已启用手柄输入的轮盘所有权、排队动作重查和中性归还。没有新增总库/机型/界面/窗口渲染/host动作/TDP调度测试，也没有修改产品源码、shared registry/runner或D15等并行工作。先前host checkpoint疑点只留来源/锚点和未来条件，不继续候选。

adapter.mjs仍导出 getRadialNativeConsumerAdapters(repo)，ID与kind:powershell保持。Run-Radial.ps1继续由共享host-process.ps1的gated child、kill-on-close Job托管整个PowerShell→Node→cl/link/native树，不自行另建Job。工具清单保持1191文件、4完整目录（核心533/3目录加当前选定PowerShell完整658/1目录），未删成员。包身份声明不冒称全OS或全部已加载模块闭合。

每次新case使用源码库之外全新EvidenceDir，复制当前实际来源为owned input-sources。V2 actual product sources为native/main.cpp、native/radial_overlay.h、native/radial_input_region.h、deps/json/json.hpp。main-return/contract.json锁定这4份完整文件、30完整定义、54精确声明、组装结果及内存模板；当前approved main为8aa08fb62c24652744dadeb6314ff1d1c4ffb149465a0078c719dfe2de17a757，即父端已最小合入Submit/Emit修复的来源。任何整文件/定义/声明/端口变化须显式维护，不能凭同一函数文本豁免完整来源身份。整文件预检在任何C++阶段之前执行，拒绝旧main后再消耗测试时间。

两阶段必须顺序全部完成：

| 必需阶段 | 消费者 | 本轮必需执行 |
| --- | --- | --- |
| post-create | 保留原13完整定义及实际State，receive/owner/ready/region/intent/queued proc-close | 原12全部通过，再做原owner-or-to-and / generation-gate-removed两项定向故障 |
| main-return | 当前main的完整gamepadEval、实际ownership callback、held判定、父发射准入、native Emit→Submit排队闭包，加actual Overlay.pad/close/retired receive | 当前已修复产品12全部通过，再做neutral-and-to-or / queued-radial-gate-removed两项定向故障 |

post-create的plan、extraction-contract、prepare、helper、MemoryPorts.hpp、ConsumerCases.hpp及完整toolchain-inputs原字节保持。新增main-return/MainCases.hpp / MainPlatform.hpp / MainEndpoints.hpp也与先前D-only已验证夹具原字节一致，公共运行时从当前G快照重新抽取，不读取D selected或旧执行回执。

main-return完整提取包含30定义（main5类型、10完整函数、完整radialInitialize声明；radial14定义含pad），另54精确声明。带默认参数{}的函数签名由专门scanner完整处理，不能把参数初始化器当成函数体。每个定义和声明都记录来源整文件哈希、UTF8字节区间、行号、原文和独立哈希。full main translation unit与真实create均不因这个抽取而被宣称完成。

每阶段故障只能使用同runId、同阶段根、同来源fingerprint、同轮原12通过的不可覆盖回执。main故障不能借post-create原12，不能借旧D或旧R25。每次编译必须自然0并完成实际sourceDependencies/链接库闭合；每个故障必须真正启动原生进程，自然23，命中精确断言，stdout/stderr完整，内存端点和动作队列归零。编译失败、信号、超时、launchError、kill或不完整输出都不能充当逆证。

| main故障 | 定向场景 | 精确断言 |
| --- | --- | --- |
| neutral-and-to-or | held-button-blocks-return | held-button-must-keep-main-return-gate |
| queued-radial-gate-removed | queued-parent-action-after-claim | queued-parent-action-must-not-execute-after-radial-claim |

main正例的original指本轮当前已修复产品来源。旧bug原始11通过/1失败、D候选12通过和两负例原件仍冻结在D-only main-return02，公共case不重用其PASS或重跑其exe。新增阶段检查原bug修复能否持续挡住“先排队、后轮盘claim”的迟到父动作，同时保留radial.summon的原有例外。

全叶通过条件采用schemaVersion=2与scopeVersion=2，两阶段及wrapper全部mandatory。适配器要求四份回执：radial-native/result.json、radial-native/post-create-result.json、radial-native/main-return/result.json、radial-native/wrapper-terminal.json。唯一完成行改为：

```
RADIAL_NATIVE_CONSUMER_V2_COMPLETED postCreate=12+2 mainReturn=12+2
```

旧的三组通过、旧完成行、缺main结果或main失败都不能放行V2。root的publicStagesPassed是实际用于最终结果的判定函数，有限契约直接测试这些拒绝行为。Node、编译器和原生阶段继续记录精确参数/PID/输入身份/提交与close后的自然终止/完整输出。root最后统一核对全部22份源码输入及所有1191工具/4目录的前后身份。父端注册层原来额外声明runner和execution-completion，外层因此预计24份来源；main现在有实际被编译消费者，不再只是关联。host.ts或旧host selftest不混入本次来源。

范围说明：实际State入场仍是post-create seed；采样、OEM先决条件、队列运输、焦点和副作用是明确内存端点。关闭的实际旧State迟到receive/close、main两个atomic、完整gamepadEval的中性归还、native shortcut admission和真实延迟闭包在本阶段范围内。完整父导航gamepadProcessUiInput内部语义、真实物理采样/OEM规则/队列并发、跨完整claim→close→neutral后才消费的超迟动作、未选择thumb按钮、真实COM/WinRT/GDI/Unicode/create/host动作/TDP/硬件均保持开放。notify只证明发出消息。

旧V1 INDEX.json与READY.json未改签，属于历史交付；它们的84个索引成员及INDEX原件在修改代码前已完整复制到D:/Codex/2026-10-18/kai-s/work/radial-main-public03/public-preimage，并逐字节核对。旧R25及其同原件审计、D-only main-return02所有原件不修改。新增交付使用独立READY-main-return.json / INDEX-main-return.json，不能用旧INDEX验收当前V2代码。

本轮检查仅运行Node契约/语法及PowerShell Parser；契约里的合成JSON/terminal只测试验收器拒绝策略，不是新的native行为回执。记录保存在maintenance-evidence/main-return-checks-*，最新检查身份由新READY绑定。当前25项契约包含原13及新增12；另对9份Node脚本作语法检查，PowerShell Parser只解析入口，不执行入口。正式native/global只由父端下一次统一运行。

父端审查时需同步shared extra-adapters.test.mjs中旧resultFiles两项期待，改成当前四项；本次遵守边界未修改该共享测试/registry/runner。唯一登记ID无需新增，来源由当前adapter自动扩展。父端还需完成当前全main编译和正式V2回执审计后，才能声称新公共范围通过。

host checkpoint疑点仅记录为src/radial/host.ts中checkpoint(s:Session)、await loadRadialSettings后的会话复查边界（观察来源cf71c1a9aa3f512c9538ac93d2a89abbc1e8cf670f68930ece704b8c5fb7f3fc，约第38–40行）。没有执行host新测试或形成候选。仅在以后用户重新授权host动作范围、父端确定独立有界验证条件后再评估；本轮停止探索。
