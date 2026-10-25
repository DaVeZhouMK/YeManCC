# 旧 HC 体感声明配方目录

`native/hc_legacy_motion_catalog.h` 提供 `ymcc::hc_legacy_motion::Recipe`、`find(deviceClass, gyroMatrixId, accelMatrixId)` 和 `containsClass(deviceClass)`。键是当前 native 返回的完整类名与精确 gyro/accel ID 三元组；所有参数均逐字匹配。selectorId 由完整三元组的 SHA256 前 16 位及 native 类名组成，生成器会拒绝碰撞。调用方必须传入已完成身份解析的当前 matrix pair，不能仅凭 `containsClass` 放行。

默认输入是 `identity-inputs.json` 的 215 个 DMI 身份，来自仓内 pc80 身份输入，来源与原文件 SHA256 见 `identity-inputs.provenance.json`。历史 `expected` / `hcClass` 标签已移除。每次运行都会从当前固定 C# 源码提取 factory switch，以无硬件实现的空类执行，重新取得 HC oracle class；再提取并编译当前生产 `ymccResolveFamily` 和实际命名矩阵变换。历史 comparison 不参与计算。`followup-matrix-classes.json` 的 51 类只用于交付覆盖复核，不筛选候选。

当前结果为 **57 个 native tuple、53 个 native 类、69 个 HC 原始 factory 类**。215 个身份覆盖当前固定 HC 的 80 个 oracle 类；其中 161 个身份进入本目录的双矩阵比对，54 个因范围或无 native matrix pair 排除。每个身份用零向量、三个 basis 和四个不同符号样本分别比较 gyro 与 accel，共 2,576 次向量比对。当前未证实或不一致配方为 0，历史 51 类全部有目录来源。

GPDWin4 保留 2 套、GPDWinMax2 保留 2 套、GPDWinMini 保留 3 套三元组。AYANEO2021Pro、LokiMax6600U/6800U 及 GPD 年份/CPU 类均以原 HC 来源类保留在 aliases 中。同一个 tuple 可以有多个 HC 来源类，其 bundle 包含这些类及完整父链的所有实际源文件。这里没有精确物理 SKU 的承诺。OneX X1Mini、G1AMD、G1Intel 仍是不同的 native 类；各自只登记当前 factory 对应的完整 matrix pair，不交叉组合 gyro 与 accel。

固定参考为 `deps/handheldcompanion-runtime/source` 的 HC **0.32.3.2**，baseline ID `HC-BINARY-BATCH12-20260831`。生成器先核对 `HC-BASELINE.json` 所记录的 `source-files.sha256` 本身，再核对每个读取的源文件与涉及的 coreFiles 记录。当前共核验 94 个具体 HC 源文件。`catalog.json` 中每条配方列出原 HC 类、从基类到子类的 inheritance、矩阵来源类、源路径、行号、表达式和全部覆盖步骤；bundles 列出每个具体 source 及实际文件 SHA256。native 输出仅用于当前身份关系与变换对照，不作为 HC 独立证明。

bundle 摘要为确定排序的 UTF-8 manifest 的 SHA256：按仓内 POSIX 路径排序，每行是 `小写文件SHA256`、两个空格、路径、LF。manifest 原文也保存在 bundles 中；`sourcePath` 指向 `catalog.json#bundles/<digest>`，`sourceSha256` 就是该 bundle 的实际摘要。每个 alias 的全部父链与 IMUMatrix 定义、gyro/accel 回调、SensorsManager 均纳入来源。selectorId 不依赖文件 hash 或 aliases 数量，已有 tuple 在补充来源说明时保持稳定。

矩阵解释器使用 SDK 自带 Roslyn，仅解析语法，不编译 HC 源码，不加载 HC 程序集，也不运行 HC 构造器。先解析固定 IMUMatrix 默认值；依继承顺序、构造器语句顺序处理赋值。每次 `new IMUMatrix` 都重置 Axis 与 AxisSwap，子类仅写 Axis 时不会继承父类的 swap。只接受单位正负号和完整 XYZ 置换；不认识的表达式、条件赋值、构造器重载/this 链、可能提前 return、成员/字典修改、回调或方法内修改都拒绝，该类及受影响 tuple 列入未证实项。

这里比对的是**声明的 AxisSwap / Axis 配方语义**：`output[swap[input]] = input`，再按输出轴乘 Axis。固定 IDevice.cs 的 IMUMatrix 构造器同时把 `_axisRemapIndices` 缓存初始化为 identity；AxisSwap 的对象初始化赋值没有清空缓存，而固定 Windows gyro/accel callback 使用 AxisRemapIndices。此处没有执行或修补这段 HC callback，不能将本工具的声明比对称为 HC callback 执行一致性；相关原始源码在证据快照中保留。

目录声明配方，不证明设备确有陀螺仪或加速度计。固定 SensorsManager 先检查 `controller.HasMotionSensor()`，再选择运行时 `InternalSensor` 对应的 Windows provider。`InternalSensor` 由 `IDevice.PullSensors()` 内 `Gyrometer.GetDefault()` / `Accelerometer.GetDefault()` 运行时更新，静态 flags 不能证明存在，更不能证明六轴都可用。所有目录项仍需主线程实际身份、provider、设备和 generation 准入。

Lenovo 的 LegionGoSZ1、LegionGoSZ2、LegionGoTablet、LegionGoTablet2 四条登记 `controllerPreferred=true`；当前 engine resolver 按主线程安排暂拒这组，避免 controllerRaw 再套 Windows 矩阵。其他条目的 false 不覆盖 HC 的全局控制器优先规则。旧 ROGAlly/ROGAllyX 可登记。MSI、XboxROGAlly 两型、SteamDeck、ASUS 兜底、Unknown/DefaultDevice、无 native matrix pair 的 Win2/Win3 以及新九表均不准由本目录扩展准入。

在仓根目录运行（本工具仅支持本机已安装的 Node、.NET 10 SDK/Roslyn、MSVC Build Tools；离线恢复配置清空 NuGet 源）：

```text
node tools/hc_legacy_motion_catalog.mjs
node tools/hc_legacy_motion_catalog.mjs --check
node tools/hc_legacy_motion_catalog.mjs --check --out G:/YeManCC-Work/Build/Validation/HC-ModelCapabilities-20261017/LegacyMotion/catalog/<fresh>
```

`--inputs <file>` 可以指定其他完整身份集，但仍要求覆盖锁定 factory 的 80 个 oracle 类。`--vcvars <file>` 指定本机 vcvars64.bat。每次都使用空的新证据目录，默认位于 `Build/Validation/HC-ModelCapabilities-20261017/LegacyMotion/catalog/<UTC timestamp>`。生成模式完成全部校验后只更新生成 header 与 `catalog.json`；`--check` 逐字比较这两个生成物。元数据没有时间戳、随机值或证据目录绝对路径。

每次生成/检查都执行解析正反例、构造器范围故障、实际 header 的 Release 和 Checked 查询，以及五个必须编译成功并由运行期断言拒绝的故障版本：忽略 accel ID、忽略 gyro ID、仅按 class 查询、抹掉 Lenovo 控制器优先、破坏 source SHA256。查询覆盖全部 tuple、未知/空/短类名、错误 ID 与跨配方混合 pair。编译失败不计为故障检出。

证据包括 raw-before 源码、baseline 与逐源 hash、当前 215 个身份的 native 元数据、Roslyn inventory、独立样本结果、generated 生成物、源码提取片段位置/hash、编译命令/日志、故障结果及 summary。`source-bindings.json` 的 start/end 为 UTF-16 字符偏移；整个 main.cpp hash 只定位冻结快照。运行结束只复核实际提取片段的 hash，其他任务修改 consumer/Lease 不会触发整文件漂移误报。

本写集不修改 main、其他头文件、其他 tests、公共 Pad、OEM 控制，也不改动 HC DLL 或固定源。完整 native 集成编译与实际 provider 准入由主线程验证。
