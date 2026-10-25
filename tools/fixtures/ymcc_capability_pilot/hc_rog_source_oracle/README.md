# HC ROG 源码参考 oracle

这是一个独立的 `net10.0` 测试参考库。生成器从锁定的 HC 源码逐字提取声明与方法体，包含原缩进、注释和换行；不以自主实现或再次手写的曲线算法充当参考。它是 **HC source reference oracle**，不是真实 HC 二进制。

## 主控可引用的入口

只向主控的新测试项目添加 `HcRogSourceOracle.csproj` 的 `ProjectReference`。不要引用本目录的 `Verification` 项目，也不要把记录端接入产品。

命名空间：`Ymcc.CapabilityPilot.HcReference`。类型：`HcRogSourceOracle`。

| 入口 | 返回值 / 用途 |
| --- | --- |
| `ConvertCurve(double[]? fanSpeeds)` | `byte[]`，直接调用逐字提取的私有 `ToAsusCurve` |
| `ApplySoftware(double[]? fanSpeeds, bool isOpen = true)` | `RecordedFanCurveRun`，执行逐字提取的 Software 分支 |
| `RestoreDefaultCurves(bool isOpen = true)` | `RecordedFanCurveRun`，执行逐字提取的 `SetFanControl(false)` |
| `DefaultCpuCurve` / `DefaultGpuCurve` | 默认表的防御性副本 |
| `ApplyProfile(FanMode fanMode, double[]? fanSpeeds, bool isOpen = true, UpdateSource source = UpdateSource.Background)` | `RecordedFanCurveRun`，执行完整的已提取配置方法 |
| `SetFanControlCaptured(bool enable, bool isOpen = true, int mode = 0)` | `RecordedFanCurveRun`，保留原方法的启用/关闭分支 |

`RecordedFanCurveRun.Writes` 是有序的 `IReadOnlyList<CapturedFanCurveWrite>`。每条记录含 `Ordinal`、字符串 `Fan`、调用时复制的 `byte[] Curve`、会话内数组身份 `BufferIdentity` 和 `SyntheticReturnValue`。`LegacyReturnValue` 在原 HC void 方法的参考调用中为 `null`；验证端可用它检查自主 writer 是否返回最后一次调用的合成值。

```csharp
using Ymcc.CapabilityPilot.HcReference;

var sourceApply = HcRogSourceOracle.ApplySoftware(new double[]
    { 0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 });
// sourceApply.Writes: CPU / GPU / Mid，同一输入数组、三个调用时的曲线副本。

Action releasedHcSourceReferenceCallback = () =>
{
    var sourceRestore = HcRogSourceOracle.RestoreDefaultCurves();
    // 默认 CPU / 默认 GPU / 默认 CPU；只记录在内存中。
};
```

该回调可供主控在自己的能力会话释放测试后调用；本 fixture 不实现能力目录、提供者选择或会话消费链。命名应保留“HC 源码参考”，不要将回调结果称为真实 HC 二进制或设备验收。

原方法 `PowerProfileManager_Applied(PowerProfile, UpdateSource)` 和 `SetFanControl(bool, int)` 也按原签名公开保留。推荐使用上表捕获入口；直接调用原方法时须先进入 `CurveWriteCapture.Capture(...)`，未进入会话会抛异常。

## 锁定来源与逐字范围

基线：`HC-BINARY-BATCH12-20260831`；上游项目版本：`0.32.3.2`。

ROG 源文件：`deps/handheldcompanion-runtime/source/Devices/ASUS/ROGAlly.cs`。

SHA-256：`04097428F3D62DE75F8160EB47398E1C61606F9D6681EB605D100B384814AA00`。

| 真实源声明 | 包含首尾的源行范围 |
| --- | --- |
| `defaultCPUFan` | ROGAlly.cs 218–218 |
| `defaultGPUFan` | ROGAlly.cs 219–219 |
| `ToAsusCurve(double[])` | ROGAlly.cs 221–244 |
| `PowerProfileManager_Applied(PowerProfile, UpdateSource)` | ROGAlly.cs 246–260 |
| `SetFanControl(bool, int)` | ROGAlly.cs 392–406 |
| `AsusFan` | AsusACPI.cs 5–11 |
| `FanMode` | Misc/FanProfile.cs 8–12 |
| `[Flags] UpdateSource` | Misc/Profile.cs 34–46 |

范围取自本次锁定文件，不依赖自主 writer 内可能滞后的来源行号注释。`HcRogSourceOracle.provenance.json` 同时保存源文件哈希、摘录哈希、生成文件哈希、生成器哈希及源/生成范围。生成文件用 `#line` 回指源范围；`editsToExtractedDeclarations` 为空。

生成器还核验 `source-files.sha256` 和 `HC-BASELINE.json` 的基线标识、版本及索引哈希。锁定值不符就停止，不自动更新依赖或接受新源。

## 纯参考边界

保留原方法声明和方法体，仅改外围命名空间及类容器；空基类提供 override 签名和内存 `IsOpen` 布尔状态，`PowerProfile` / `FanProfile` 为只承载所读字段的数据壳。不会运行 HC 的实际构造器、设备基类或工厂。

唯一有副作用的末端 `AsusACPI.SetFanCurve` 解析到内存记录端，按调用时复制曲线并记录数组身份。验证侧实际 `AsusFanCurveWriter.cs` 的 `SetFanCurveObserved` 也解析到同一个记录器。新增 observed overload 所需的 `AsusAcpiCallReceipt` 是空的编译占位类型，不产生硬件收据，也不验证收据内容。

这里捕获的是 AsusACPI 方法**入口参数**，包括尚未做下游钳制的 100%。不包含 AsusACPI 内部 100→99 钳制、ACPI 封包、IOCTL、驱动回执或硬件写入。1001 / 1002 / 1003 仅是用于验证 CPU/GPU/Mid 调用顺序及末次返回值的合成整数，没有固件成功含义。

原源的两个行为差异被保留并测试：Software 分支本身不检查 `IsOpen`；默认曲线交还检查 `IsOpen`。`SetFanControl(true)` 保留原始空操作。`ConvertCurve` 对非法长度/null 返回原静态 CPU 默认数组，保持别名语义，请不要修改该返回数组；需要默认表副本时用 `DefaultCpuCurve` / `DefaultGpuCurve`。

## 生成与验证

现有本机 .NET 10 SDK 和 Python 标准库即可。没有 `PackageReference`；本地 `NuGet.Config` 清空包源，构建不安装或更新依赖。

```powershell
# 只核验，不修改生成物：
python .\generate_hc_rog_source_oracle.py --check

# 需要明确重建提取件时：
python .\generate_hc_rog_source_oracle.py

# 构建独立库，编译实际 writer，并执行内存对照：
.\run-verification.ps1
```

请在本目录运行这些相对命令。脚本使用自身绝对路径定位 fixture，默认把临时文件、SDK home、NuGet 缓存及构建产物写到 `D:/Codex/2026-10-18/kai-s/work/fan-pilot/oracle-prep/hc-rog-source-oracle-artifacts/`；源码、提取件与结果仍留在 fixture 的原位置。`-ArtifactsRoot` 只接受该专用目录或其子目录；`-ValidatePathsOnly` 只检查输出路径，不执行构建或测试。独立生成器默认从目录位置推导仓库根，也支持显式 `--repo-root`。验证前后均核验提取件，编译程序集内嵌本次生成源和实际 writer 源的副本，运行时检查哈希以排除陈旧编译物。

最新本地验证：**70 / 70 通过，0 警告，0 错误**。类别包括正常 11 点、null 与 6 种非法长度、0/100、越界值、银行家舍入、非递减处理、无穷值/NaN 的本运行时对照、32 组固定种子样本、CPU/GPU/Mid 次序及数组别名、默认表交还、源 `IsOpen` 行为、UpdateSource 组合和无本机导入/HC 依赖检查。

对照编译的是 `FanLab/fan-backend/Asus/AsusFanCurveWriter.cs` 实际文件，不是复制件。准确源哈希和逐条输出见 `results/oracle-parity-results.json`。仅改变说明文件不要求重跑；实际 writer、oracle、记录端或测试代码变化后需重跑。主控新增能力目录与会话的消费链测试由主控自己的项目负责。

## 本目录文件

- `generate_hc_rog_source_oracle.py`：验证锁定源并逐字提取。
- `Generated/HcRogSourceOracle.g.cs`：可编译的 8 个原始声明。
- `HcRogSourceOracle.provenance.json`：源/摘录/生成哈希与范围。
- `HcRogSourceOracle.cs` / `HcRogSourceOracle.csproj`：独立参考库、数据壳和记录器。
- `Verification/OracleParityChecks.cs` / `Verification/HcRogSourceOracle.Verification.csproj`：单独的实际 writer 对照程序。
- `Verification/AsusWriterRecordingBoundary.cs`：只供对照程序使用的记录末端与编译占位类型。
- `NuGet.Config` / `.gitignore` / `run-verification.ps1`：离线构建约束、产物忽略与运行入口。
- `results/oracle-parity-results.json` / `results/verification-summary.md`：机器结果与人工摘要。
- `results/*.log`：实际构建与核验输出，受仓库既有日志忽略规则影响。
- `results/changed-paths.txt`：全部交付文件和迁移后构建缓存的绝对路径；`results/artifact-migration.json` 保存迁移前后逐文件哈希核对。

没有改动现有 Program.cs、项目文件、manifest、打包文件，或主控的能力目录与会话代码。按追加授权，原 `.artifacts/` 已迁往 D 盘专用 prep 子目录，51 个文件逐一比对 SHA-256 一致；原源码树产物目录已移走。原 `.artifacts/` 已受忽略规则覆盖，迁移前确认路径仅在本 oracle 目录，且无重解析点。此次只调整自有 runner 和说明，原 70 项结果继续保留；ROG 源、生成源及实际 writer 哈希均未变，未重跑已通过的对照。