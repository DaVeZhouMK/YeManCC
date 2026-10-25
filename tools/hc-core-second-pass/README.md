# HC 主体第二次精简：本轮不提升

本轮已完成有界试验与软件验证。最终候选保持原 HC 无强名身份，实际生成更小 DLL；由于动态反射和序列化的完整语义闭包仍未证明，**本轮不提升、不提交到生产、不接入普通构建**。`PROMOTION-LIST.json` 的 replace/remove/deploy 均为空。

| 项目 | 原样 baseline | 最终 unsigned 试验候选 |
| --- | ---: | ---: |
| HC 字节 | 2,774,016 | 2,238,976 |
| 原 JSON 不变时 runtime 字节 | 12,372,746 | 11,837,706 |
| runtime 文件 | 86 | 86 |
| HC GetTypes | 1,768 | 1,557 |
| 主 Devices 具体类型 | 77 | 77 |
| 全部具体 IDevice 派生类型 | 91 | 91 |

HC 净减少 **535,040 B**。上一轮未提升 devices-v2 产物为 2,241,536 B，本轮最终只比它再小 2,560 B；主要新增价值是执行和拒绝证据，不能把本轮体积结果包装成新的已批准生产版本。

最终 HC SHA256：`4f9d2f534289c4e6cc7183deddd719cfd3dfa613fde1e99946ff5327697149ab`。
原 HC SHA256：`08a386607a0e5e72c5fcaa98d3a6c0b3b256eb0fca7d6547f6f124c1f1f33583`。
两者身份均为 `HandheldCompanion, Version=0.32.4.0, Culture=neutral, PublicKeyToken=null`。

候选：`D:/Codex/2026-10-18/kai-s/work/hc-third-trim/core/candidate/HandheldCompanion.dll`。
证据根：`D:/Codex/2026-10-18/kai-s/work/hc-third-trim/core/`。

## 已执行的最终验证

- 原样 baseline 先复制，逐文件 SHA256 核对，86 文件及 12,372,746 B 与授权输入一致。原始 baseline 始终保留，不受父任务 JSON 紧凑影响。
- 全部 260 个 Devices 元数据类型（含嵌套）保留，1,826 个方法与 1,094 个字段零差异。独立现有 verifier 与生成器分开构建。
- 最终候选全部 9,732 个保留方法 IL、方法标志、locals、异常区和 native imports 对比通过，80 个 native 布局通过；补充布局审计覆盖 327 个类型。
- Factory 的 13 个字段管理器类型、Status、PowerProfileManager 的 Applied/Discarded/CloneProfile、App 四路径字段、所有显式消费根反射加载通过。Factory/App/管理器构造及隐式 cctor 前向闭包 771 方法全保留，代码由独立 IL 比较锁定。未执行真实 Factory 初始化。
- `hc.GetTypes()` 全量执行，任何 ReflectionTypeLoadException 都不按可加载子集放行。原 77 个主 Devices 及额外 14 个具体派生类型全部可解析。211 个删除类型逐名记录在 `DELETED-TYPES.json`。
- 实际 HC 的 22 项内存 ACPI callback 比较通过，无设备构造、无真实 ACPI transport。
- 真实同形 `PowerControl/handheldcompanion-runtime/<id>` 与 `PowerControl/ymcc-shared-runtime` 中放入精确 8 个共享原件和 manifest，使用生产 `DeviceCoreSharedDependencies`。baseline 原有生命周期脚本完整通过；候选 7 纯构造、14 个零句柄操作、4 个非零哨兵 native 拒绝、被动计时全部通过。
- 全部 3 个嵌入资源容器和条目字节不变；11 张 OneX LED 缩略图也未删除。未伪造实现、未吞异常、未开启 GPU/SDL/JSL 等 manager。

## 实际失败与归因

原有零触摸 fixture 固定了三个旧 MethodDef token。ILLink 移动元数据行，原断言先失败，尚未执行任何操作。最终只在三个方法签名、原 token、候选 SHA 和独立“全部保留 IL 相等”回执都固定后重定位三项 token 常量。接收类型、行为、零句柄、哨兵和 native 拒绝断言全部保留。变换见 `verify-lifecycle.py` 与 `zero-touch-token-rebase.json`；原失败回执仍在。

原始单目录纯构造探针缺共享 Newtonsoft.Json 的失败已保留。最终验证不塞入依赖掩盖，而是使用生产同形目录和原有精确 shared resolver。早期 flat 合并测试视图只作试验过程证据，最终以 `lifecycle-unsigned-final/verification.json` 为准。

对 baseline 和候选运行同一份完整方法签名探针，baseline 有 26 个失败成员，候选有 23 个，**候选独有失败为 0**。包括当前批准 runtime 本来就未提供的 WindowsDisplayAPI 等依赖；它们不影响已执行的 HC 全 GetTypes 和实际消费反射入口。完整签名诊断仍记为失败，不宣称全部任意 UI 签名可解析。此前扩展字段/成员 walk 遇 HelixToolkit 也保留原日志，未补库。

开发签名试验生成了 2,239,488 B 的派生 DLL，RSA 验证成功，但把无强名 HC 变为 development token `538ea1f68f629a04`，独立 identity 比较失败。父任务明确要求保留 HC 原身份后，该版本移入 `rejected-signed/`，不在最终候选或提升列表中；未复制私钥。两 UI 库的现有 development 签名不受本任务影响。

首次 linker/签名输出的 DispatcherPriority 解析失败、资源 reader 不支持序列化资源头、探针最初将 91 全派生类型误当 77 主命名空间的计数错误，以及过宽布局/构造审计的失败均保留。最终只修正工具解析与验证定义，没有通过删除行为断言或放宽全 GetTypes 得到通过。

## 未证明的闭包与后续切口

`REFLECT-ROOT-SOURCES.json` 完整列出 XML 描述符来源、649 个 Host/LightSetter 反射词法站点、消费源码 SHA 和 348 个保留 HC 反射/序列化站点的上下文入口。枚举完整不等于语义证明：尚未把所有动态名称、接收对象和序列化模型划入可达消费闭包。因此本轮拒绝提升，即使上述软件验证已通过。

后续限定切口是逐一分类这 348 个站点，首先处理实际 Profile 克隆/JSON 模型、Factory Status/事件与 LightSetter LEDPreset 的 NameKey/Value。再按删除名单和保留 IL 零悬空引用，缩小 UI 区域；不能仅凭 Devices IL 相等证明管理器初始化安全。

11 张 OneX 缩略图共 107,169 个原始资源字节。已查明 LEDPreset 构造仅保存 Icon 字符串，LightSetter 使用 NameKey/Value；下一轮若能证明无实际 headless resource stream/render 消费者，再尝试资源删除。本轮保留全部资源。候选只消失了若干 framework AssemblyRef，没有消失任何应用叶库 AssemblyRef，本轮不据此批准删除任何叶 DLL。

## 工具与复核入口

所有新源码只在 `tools/hc-core-second-pass/`；构建和证据只在上述 scratch core。`NuGet.Config` 清空源；构建均离线，使用本机 SDK 10.0.400 / ILLink 10.0.11。独立 verifier 和既有 fixtures 以只读链接源码方式编译，未编辑原有工具。

`CoreTrim.csproj` 提供 inventory、roots、tokens 命令；sign 命令只用于已拒绝身份变化的历史试验。`IndependentVerifier.csproj` 提供原有 Devices 与全部保留 IL/native 比较；`ClosureAudit.csproj` 给出初始化闭包及保留 IL 反向悬空引用；`LoadProbe.csproj` 检查完整 GetTypes 与实际根；`SignatureProbe.csproj` 在两输入上执行相同完整方法签名诊断；`CallbackFixture.csproj` 复用原 ACPI fixture。

最终生命周期可用新的 scratch 输出目录复核：

```powershell
python G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/tools/hc-core-second-pass/verify-lifecycle.py --runtime D:/Codex/2026-10-18/kai-s/work/hc-third-trim/core/production-shaped/PowerControl/handheldcompanion-runtime/candidate-core --scratch D:/Codex/2026-10-18/kai-s/work/hc-third-trim/core/recheck-lifecycle --token-map D:/Codex/2026-10-18/kai-s/work/hc-third-trim/core/zero-touch-token-rebase.json
```

`finalize-reports.py` 仅汇总已有回执、重新核对原始 DLL 与源码站点，输出 STAGE-REPORT/PROMOTION-LIST 等；不会生成批准或改变生产内容。scratch 中 `candidate/` 的 manifest 仍是冻结旧 manifest，故该目录仅是试验覆盖视图，不是部署包。生产同形 fixture 视图中更新 HC 哈希的 manifest 也只用于软件检查。

没有启动产品 Host、LightSetter、设备、驱动、SDL native 或 Steam，没有连接硬件，没有制作掌机验证包，没有联网，没有提交、部署或覆盖生产 profile/runtime/native。父任务自行紧凑的 JSON 在原样 baseline 对比中单独列出，所有生产 DLL 均未被本任务改写。
