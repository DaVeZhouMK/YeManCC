# Capability / Host 只读审查结论

审查范围仅为 capabilities/FanCapabilityCatalog.cs、AsusFanCapabilitySession.cs，以及实际 Host ApplyAsusCurveViaBackend / ReleaseBackendAsusAcpiOwnedHandle 的关联调用。未修改主控文件，未运行产品、硬件或重复对照矩阵。

## 必须修：P1 非所有者 Host 能关闭其他实例的全局 ACPI 句柄

位置：FanLab/real-host/Program.cs:7266–7275。

ReleaseBackendAsusAcpiOwnedHandle 的入口只在“本实例未标记打开且全局也未打开”时退出；只要全局 AsusACPI.IsOpen 为 true，即使 backendFanCapability 为 null、本实例从未取得目录租约，也执行 AsusACPI.Close。Dispose 在 Program.cs:11236 调用该助手。

具体反例：A 取得 catalog 租约并打开全局 leaf 句柄；B 取得租约失败（或 B 根本未选择 backend）；B.Dispose 仍进入此助手并关闭 A 的句柄。A 继续持有 catalog owner，但底层句柄已被 B 撤销，后续三写可能部分/全部未发起。

建议最小修复：关闭前明确证明本 Host/session 是当前所有者，并将该证明与 Close/归还租约保持在同一目录串行边界中；仅凭全局 IsOpen 或本地旧 bool 都不足。无租约实例不得关闭他人的共享句柄。建议只补这一个双 Host 反例，不重复整套矩阵。

## P2 异常诊断：关闭证明探针会覆盖 callback 的原异常

位置：capabilities/FanCapabilityCatalog.cs:79–83。

hcHandleReleased 在 finally 直接执行。当 callback 与关闭探针都抛异常，finally 的探针异常覆盖原 callback 异常。当前 legacy flag 保持 true，所有权方面是 fail-closed，因此此项不构成第二 writer 放行；但原故障信息丢失。建议分别保存两种异常，保留原 callback 故障并记录关闭证明失败，继续占用 gate。

## 已处理，不再重复要求修复

- ownershipGeneration：新 session 使旧 session 的成功 restore 证明失效。
- BeginOperation：派发前先将 LastResult 改为 Unknown；同 session 后续操作抛异常不能继续使用旧成功结果。
- InvokeHcAfterRelease 的第三参 Func<bool>：明确关闭证明缺失时 legacy gate 保持占用到 Host 退出，这是当前明确选择的策略。
- FanCapabilityResult 已附 ScopeId；Outcome 使用枚举字符串日志。

## 外层 scope 收据结论

未发现新 session 偷取原 Host scope 收据。ApplySoftwareCurveObserved / RestoreDefaultCurvesObserved 直接返回各次调用 receipts；session Record 只读取该数组，没有创建内层 scope、调用 TakeReceipts 或清空全局队列。原 AsusAcpiObservations.Publish 仍把三条记录发布到现有 AsyncLocal scope，Host 原 CompleteAsusWriteScope 仍负责 TakeReceipts / Dispose。

## 核对快照 SHA-256

- FanLab/real-host/Program.cs: EE9F02E70DCEFFA8659C7EE19D7BBF0C5525D84FF1EC65B8E433667C9EF873E2
- capabilities/AsusFanCapabilitySession.cs: B98C6DFF5156C7088CFC056517016B2FCB2D67C542D09A54E064E7B9C74AD0FC
- capabilities/FanCapabilityCatalog.cs: 1AB08419781AD13C874C54197C3D53B24D8CB90BD5468204E309522FEB7F7DC1

以上是本次读取快照的静态审查，不声称独立复验主控报告的真实 HC binary roundtrip。

## 自有产物整理

原 .artifacts 受本 oracle .gitignore 覆盖；迁移前核对绝对路径仅位于自有 oracle 目录，未发现重解析点。51 个文件已原生移动到 D:/Codex/2026-10-18/kai-s/work/fan-pilot/oracle-prep/hc-rog-source-oracle-artifacts，迁移前后每文件 SHA-256 一致，原 .artifacts 目录不再存在。详情见 artifact-migration.json。

run-verification.ps1 默认输出已改为上述 D 盘专用目录，源码树输出路径被拒绝；只执行了语法解析及 ValidatePathsOnly 的正/反路径检查。原源码提取件与 70/70 报告留在 G 盘 fixture，相关源哈希未变，未重复跑矩阵。