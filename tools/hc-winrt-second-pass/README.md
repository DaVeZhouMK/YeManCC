# SDK / WinRT second pass (offline)

本工具只写新 `tools/hc-winrt-second-pass` 与指定 `D:/Codex/2026-10-18/kai-s/work/hc-third-trim/winrt` scratch。最终 SDK 单文件候选减少 493,568 B，WinRT.Runtime 原样保留；本子任务没有直接执行生产提升；父代理现已按授权提升唯一 SDK 候选。中文交接见 HANDOFF.zh-CN.md。PROMOTION-LIST.json 是唯一交接名单，不要复制整棵 overlay 或其中过时的 manifest/JSON。

离线构建使用现有 .NET 10.0.400 / runtime 10.0.11、缓存 ILLink 10.0.11 / Mono.Cecil；NuGet.Config 禁用所有包源，不安装 SDK/驱动。所有 dotnet build 必须带 scratch `--artifacts-path`，不得在其它项目生成 obj/bin。

复现顺序：snapshot.ps1（仅对相同 before 输入）；构建 WinrtSecondPass.csproj、Probe/Probe.csproj、CallbackProbe/CallbackProbe.csproj；build-candidate.ps1 -Mode sdk-data-only -AttemptName sdk-data-only-recursive；运行工具的 contracts-full、exact-il、abi、data-audit、dynamic-audit、signatures 以及两份独立进程的软件和 callback 对照；最后 verify-receipts.ps1 检验完整已存回执。命令的真实参数与对应报告留在 scratch，各次 linker 参数在 attempt-*/arguments.json。Snapshot 已冻结，不应在父任务更新 JSON 后重做并覆盖 before。

最终保守策略 root 每个不可变消费者及 SDK 的全部原类型/成员；只不强制保留无方法、非公开编译器容器的 RVA 字段，随后要求删除面严格符合 5,088 个 16 B/hash 命名数据字段、保留所有原方法/类型/属性/事件、IL/ABI 和动态消费验证。WinRT 不重写；不执行 HC 设备构造、Host、网络或 SDL。纯 WinRT 软件探针仅允许目标库从 System32 载入 COM/HSTRING 系统库，其他应用 native import 一律拒绝。

代码复用（只读链接）：../hc-application-dependency-trim/Program.cs 和 RuntimeProbe/{Program,PinnedHcAdapter}.cs。Signing.cs 由既有 DevelopmentSigning.cs 派生，仅调用验证，不读私钥、不签名。SOURCE-DEPENDENCIES.json 锁定实际复用源与工具哈希。


父代理已完成组合验证并提升（profile 前缀 c3c35e，总量11,849,647B）。verify-receipts.ps1 中的 live before guard 仅供历史提升前使用，当前不要重跑以覆盖已确认的提升结论；生产变化属于授权整合。冻结 baseline 只用于来源审计。见 PARENT-PROMOTION-RECEIPT.json。

