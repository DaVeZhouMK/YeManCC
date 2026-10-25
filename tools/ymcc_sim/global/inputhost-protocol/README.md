# InputHost 协议和成本窗隔离消费者

本目录提供 `getInputHostProtocolAdapters()`，由统一环境主线登记；本目录不改共享注册表。

- `controller.inputhost-cpu-cost-window`：直接复用支持显式输出目录的原 CPU 成本窗脚本，执行原9组断言的 Release/Debug。实际 `MaybeEmitCostWindow` 源片段及原字节 `HostCpuCostWindow` 执行，进程/线程累计值、单调时钟和诊断输出采用原有 fake。另在自有 helper 副本破坏观察时间分母，必须由同一原断言检出。
- `controller.inputhost-optional-trigger-protocol`：原 Python 工具绑定历史性能测量目录，故不直接运行。原 C#4组夹具按原字节编译，全量运行因旧6persona/profile假设真实失败，原FAIL单列保留。现行入口直接调用其中3个仍适用的原方法，并增加依据当前native发送端及消费selector的3合法组合、已移除persona/旧Elite/cross-profile拒绝、16KiB ASCII/UTF8边界矩阵；当前完整生产解析器与 metadata/screen/hash/TriggerToByte 实际函数执行。`CandidateParser` 是当前未改逻辑；`BaselineParser` 是当前源码副本注入缺省值归零故障的对照，**不是历史 before 源码**。原报告名称保持原样，外层说明该别名范围。另破坏实际 Candidate 的缺省行为，原断言必须失败。

只运行 .NET fixture，不编译或启动产品 Host 项目、SDK、驱动、pipe 或设备。原 C#中未调用的性能分支保留，新增入口仅接受一个报告路径，且 native import resolver 拒绝原性能分支的任何 native 调用；没有性能降低或整机CPU声明。MSI release fence仅消费原常量，metadata协议实际代码保持原字节。

输入来源按原字节保存；本目录成员和输入/工具集合前后校验。dotnet SDK/runtime及工具引用记录于 toolchain-inputs.json，原脚本的临时编译和所有子进程工作目录均置自有、仓库外的非系统盘证据区。生成目标记录实际 Compile/Reference/Analyzer 列表；原业务报告不改写，反例实际失败日志、源码变更与自然退出记录分别保留。最终 sourceHashes 使用仓库相对路径和 case 自有绝对路径；外部工具属于显式 artifactInputs，原字节副本也随证据保存。

```
node tools/ymcc_sim/global/inputhost-protocol/run.mjs --case controller.inputhost-cpu-cost-window --evidence-dir D:/Codex/2026-10-19/mo/work/inputhost-example/cpu
node tools/ymcc_sim/global/inputhost-protocol/run.mjs --case controller.inputhost-optional-trigger-protocol --evidence-dir D:/Codex/2026-10-19/mo/work/inputhost-example/trigger
```

证据路径必须是尚不存在的自有本地非系统盘目录，保留源码目录、系统安装盘及链接路径拒绝；不允许输出到源码目录、复用旧回执或接受新输入版本而重签原报告。成本窗只覆盖普通诊断路径，MSI延后分支由原断言验证提前返回；不声称覆盖完整MSI产生日志或硬件成本。

工具闭包为571个不同文件，82条MSBuild runtime asset映射中76条新增声明对应75个不同物理文件；不是572个不同文件。公共上限由主线有界更新为640，本目录未改共享规则。缺SDK/pack时构造返回真实缺路径，由公共fingerprintArtifacts标为缺失；实际执行仍逐文件pin，绝不放宽。CPU冻结4个运行模块不导入协议的动态case/extractor/入口/fixture，因此补协议不会改签CPU原报告。
