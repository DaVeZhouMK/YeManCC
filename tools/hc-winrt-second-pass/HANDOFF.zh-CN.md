# SDK / WinRT 第二轮收尾交接（2026-10-18）

结论：交付 **SDK 单文件、编译器 RVA 数据裁剪候选**，最终状态 `PROMOTED_BY_PARENT`；父代理已审阅唯一 SDK 候选并按授权提升，本子任务没有直接写生产。WinRT.Runtime 保留原文件。请主代理只取 `PROMOTION-LIST.json` 中这一项，不复制任何整个 overlay，不还原冻结快照中的 JSON/manifest。

| 文件 | 当前基线 | 候选 | 净减少 |
|---|---:|---:|---:|
| Microsoft.Windows.SDK.NET.dll | 1,568,256 B | 1,074,688 B | 493,568 B |
| WinRT.Runtime.dll | 528,944 B | 原样 528,944 B | 0 B |
| 合计 | 2,097,200 B | 1,603,632 B | **493,568 B** |

SDK before SHA256：`0da19d45d98c20b46b08ec282bd8a818e5600d482912e2289ba0da591e19b7e0`。
SDK after SHA256：`c37d2e6b66889551d7de218aa726e90de6e6dd5b794069485f3b23524028e102`。
候选：`D:/Codex/2026-10-18/kai-s/work/hc-third-trim/winrt/promotion/Microsoft.Windows.SDK.NET.dll`。
WinRT 保留 SHA256：`bcf3a14e8712a90837fc5c0d8c8a24696af2bd7f74e9767597ec81ededfb23db`。

裁剪内容和正面隔离证据：

- 唯一删除面是 SDK `<PrivateImplementationDetails>` 中的 5,088 个字段。每个字段均是非公开、Static、InitOnly、HasFieldRVA 的 16 字节编译器常量，字段名严格等于数据的 SHA256 十六进制大写值。容器是非公开、sealed、CompilerGenerated，既无构造/方法，也无接口或 WinRT 投影元数据。
- 所有原有 SDK 类型、方法、属性、事件保留；对保留字段的属性、常量、RVA 初始数据、封送信息均作了比较。候选保留 1,892 个 SDK 元数据类型（CLR GetTypes 1,891），10,396 个 SDK 方法。WinRT 603 个元数据类型、3,594 个方法对应整文件原样保留。
- 保留的全部方法 IL（含所有 ldtoken/字段访问、分支、局部变量、异常处理、PInvoke）以及 ABI 零差异。每个保留的生成 IID/vtable 初始化器仍引用同名字段、同一初始字节；不是只看“某字段没有静态引用”就批准。
- 83 个不可变托管消费者中，没有指向该编译器容器或任何删除字段名的字面量。WinRT.Runtime 的动态字段反射入口实际使用 `PIID`、`AbiToProjectionVftablePtr` 等投影成员；这些成员及 helper/激活/泛型/marshaller 类型与属性完整保留。所有当前消费者跨程序集引用解析保持与原样基线一致。
- 任意外部插件主动反射编译器内部 RVA 存储不属于本次兼容性承诺；没有宣称对一切可能的反射程序完成全量证明。本轮只交付固定不可变消费者及现有投影动态边界内的保守变更。

实际验证回执：

- 从当前 86 个 runtime 文件与 `deps/ymcc-shared-runtime/bin` 8 个不可变共享原件重新冻结输入、校验两个原 manifest。所有可链接的不可变程序集均完整 root；二进制根共 83 个托管程序集。原生文件同样按哈希锁定，未加载执行。
- 原样 baseline 与最终候选均通过 26/26 软件检查，结果值逐项完全一致。覆盖几何值类型、HSTRING 空串/中文/嵌入 NUL、12 组 GUID/泛型签名、反射闭合的 blittable/non-blittable/string 数组、真正的本地 ValueSet factory 激活/泛型字典/装箱、ApiInformation、本地完成 Task 的双向 async action/operation 桥、47 个 SDK enum 的 228 项反射表，以及 7 个既有 detached 纯构造。
- 全部 83 库 GetTypes 均与原样 baseline 完全相同；HC CLR 类型数 1,768。真实 HC callback 对照 22 项通过。HC DLL SHA/所有字节不变，因此原 77 机型集合完整保留；没有把继承树数量、工厂分支数量或纯软件测试冒称 77 台实机验收。
- 完整相关 TypeRef 495、MemberRef 5,910、ExportedType 0；全部解析成功，完整记录与 baseline 相同。包含不可变库指入两目标，以及两目标所有对外引用；0 exports 是实际表计数。
- 所有保留 IL、方法/字段/类型/属性/事件 ABI、泛型约束、PInvoke、结构布局和嵌入资源内容比较通过。数字 metadata token 不作为跨文件固定 ABI；IL 操作数按类型/成员语义及作用域比较，仅规范化框架转发与函数指针无意义 Scope。
- 86 文件 overlay 中仅 SDK 一个文件改变；生产和共享全部二进制仍匹配冻结 before SHA。父任务期间压缩了 HandheldCompanion.deps.json、HandheldCompanion.json 并刷新 manifest，回执已单列，未覆盖这些变更。观测到 manifest `cf1319fd8024a9c781b5585e8acf65327317af0927ee55424d138f7924543713`。

签名事实必须随候选保留：

- 当前 SDK 基线是上轮已修改的 YMCC 派生物，虽然保留 Microsoft 公钥/token 与 StrongNameSigned 标志，强名称密码学校验原本就失败，且没有内嵌 Authenticode 证书。本候选保持相同身份和相同签名事实。不能称为“微软原厂有效签名文件”，也没有通过清标志把原厂签名要求悄悄降级。
- WinRT 原文件强名称密码学校验成功，保留 10,288 B 原证书表及证书哈希；最终候选完整沿用该原文件。没有使用私钥，也没有施加 development 签名，更没有签署其他人的文件。没有联网做 Authenticode 链/撤销检查。

已实际执行并保留的失败/无收益结果（四种策略，guarded 含一次根语法修正）：

| 尝试 | 双库合计 | 实际结果 |
|---|---:|---|
| guarded 初版 | 1,554,944 B | 24/26；异步反射桥失败。嵌套 root 曾误用 `+`，ILLink 对应 IL2008 日志完整保存；绝不提升。 |
| guarded-v2 | 1,587,200 B | 修正嵌套 root 为 `/` 后仍 24/26；AsAsyncAction / AsAsyncOperation 实际缺失；拒绝。 |
| full-dynamic | 2,083,840 B | 26/26、22 callbacks、保留 IL 通过；WinRT 原有效强名称/证书丢失并有 1 项编译器容器类型 ABI 差异；拒绝。 |
| SDK 全表重排 | 2,097,200 B | 26/26、IL/ABI 通过，但大小完全不减；不提升。 |
| SDK RVA 单文件 | 1,603,632 B | 最终可供父任务复核的候选；所有交付门通过。 |

原始旧 IL 比较器曾把函数指针 Scope 从 System.Runtime 到 CoreLib 的重写记成 98 项差异；完整方法文本保存，修正比较器后操作码和完整函数指针签名零差异。初期软件夹具的 API-set 绝对路径和反射重载选取问题也留存原失败日志，修正后重跑原样 baseline，才得到最终 26/26。首份汇总回执曾将两个空数组计成非空删除集合，原报告已另存；修正汇总逻辑后全部必要门通过，未改候选来消除该报告错误。

父代理整合已完成：用户告知已审阅 PROMOTION-LIST，并完成 UI+SDK 组合 7 构造、SDL 正反路径、22 callback、完整跨库 contract、13 runtime 检查后提升。当前 profile 前缀 `c3c35e`，组合总量 `11,849,647 B`（父代理回执，不是本子任务重测）。不再以生产不同于冻结 before 当作输入漂移错误，也不重跑 before live guard。493,568 B 仍仅表示本次 SDK DLL 的未压缩净减少，不能独立归因整个 UI+SDK 组合或压缩包的所有差值。

边界与下一轮具体切口：本轮没有真实硬件、Host、SDL、网络、睡眠/生命周期或 UI 启动验证。若以后继续裁 WinRT.Runtime，应先解决“保持原厂有效签名”与重写冲突，再设计明确的派生签名及消费者绑定方案；独立保留 async 桥/投影注册/泛型 marshaller 根，使用本轮 ValueSet + HSTRING + 数组 + async 夹具扩展闭包。不能把 WinRT 13,360 B 的签名损失收益计入本轮交付，也不再在本轮扩展策略。

主要证据：`PROMOTION-LIST.json`、`VALIDATION-RECEIPT.json`、`FAILURES.json`、`ATTEMPTS.json`、`BASELINE-LOCK.json`、`attempt-sdk-data-only-recursive/{contracts,exact-IL,retained-ABI,data-audit,dynamic-data-audit,software-26,baseline-comparison,callback,signatures}.json`。完整生成文件列表见 `CHANGED-FILES.scratch.txt`。

阶段消息曾尝试用会话工具投递，但工具拒绝给 native ancestor 发消息；当前子会话未暴露 send_input。阶段简报另存 STAGE-BRIEF.zh-CN.txt，最终结果由本子任务 final 交还。

最终递归范围修正：初版脚本仅枚举顶层 DLL，收尾时发现 `Resources/RTSS/RTSSSharedMemoryNET.dll` 是第 83 个托管消费者，另两个嵌套 DLL 是原生文件（只锁定不加载）。已对同一 SDK RVA 策略重新完整 root、生成候选，并在原样 baseline / 最终候选各执行 83 库 GetTypes 和全部 26 项软件检查；RTSS 400 个 CLR 类型正常加载，native 入口仍被拒绝。递归前后 SDK 候选 SHA 完全相同。最终权威回执位于 `attempt-sdk-data-only-recursive`，baseline 对应 `software-baseline-26-recursive.json` / `contracts-baseline-recursive.json`。前面 82 库结果作为历史记录保留，不作为最终“全部消费者”的依据。链接参数追加后触发一次 PowerShell 原生重定向启动错误，已改为 ILLink 支持的 response file 并成功运行，原 arguments 和 response file 留存。没有增加第五种探索策略。


父代理最终提升记录：见 PARENT-PROMOTION-RECEIPT.json。本次通知后仅修改交接文档和索引，没有新增试验、执行生产检查或覆盖父代理已经提升的任何文件。冻结快照及此前失败结果继续保存。

