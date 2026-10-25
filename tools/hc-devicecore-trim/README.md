# HC devicecore binary resource trim — handoff 2026-10-18

**最新交付：SDL passive timer + 零 touchpad guards 已完成。** 当前 HC `08a38660…`（2,774,016 B），实际两个 Dummy receiver 的完整 InitializeInputOutput 零路径、无 SDL3.dll 新进程与 native deny、非零哨兵拒绝、原 SDL NULL oracle、Path/GetTypes/22 callback/7 纯构造均通过。见 [SDL-TOUCHPAD.md](SDL-TOUCHPAD.md) / [SDL-TOUCHPAD-RESULTS.json](SDL-TOUCHPAD-RESULTS.json)。完整 Dummy 构造仍未执行，消费边界见报告。

**前阶段：SDL passive timer 已完成。** HC `c0e720…` → `110156b1…`（2,774,016 B），仅改默认构造两处计时 call 并添加内部 BCL helper。物理无 SDL3.dll 的新进程计时前缀、全部 GetTypes、22 callback 与 7 纯构造通过；完整 dummy 与其他 SDL 消费路径需保持报告所述边界。见 [SDL-TIMER.md](SDL-TIMER.md) / [SDL-TIMER-RESULTS.json](SDL-TIMER-RESULTS.json)。以下 Modern 三文件闭包是本次输入基线。

**前阶段交付：Modern headless 签名/引用闭包已完成并通过实际 Path 加载与纯构造验证。** 正式交接见 [MODERN-CLOSURE.md](MODERN-CLOSURE.md)，机器清单见 [MODERN-RESULTS.json](MODERN-RESULTS.json)。主控应同时提升 HC `c0e720…`、Modern `5570…`、Controls `953b…` 三文件。以下 HC pathfix-v1 是前一阶段基线，保留其复现与 P1 失败证据。

- 原 HC：12,748,800 B，SHA256 `0c5132a9d13aebfc5add2aa7c9ac54e8daaa8ebc816a0c68d099bb9685dc2e49`。
- 定稿候选：2,772,992 B，SHA256 `94c575e53c66b8fba54b5afbc4d136400c555366c31cd9d25cca86bac6a441ee`。
- 实际减少 **9,975,808 B（78.25%）**。虚拟地址空间未压缩；文件资源字节已实际移除。
- 候选：`D:/Codex/2026-10-18/kai-s/work/hc-large-strip/binary-trim/candidates/YMCC-DEVICECORE-HC-RESOURCE-TRIM-pathfix-v1/HandheldCompanion.dll`。
- 原程序集绑定名称/版本、公钥状态、全部定义/引用 token 保留。新 MVID、单独 `YMCC-DEVICECORE.candidate.json`、新 SHA256 和 runtimeId 表明 YMCC derivative。上游 runtime manifest 原封保留，只用于验证输入，不能授权修改后的文件。

## 离线复现

使用 .NET SDK 10.0.400；已验证本机 Microsoft.NET.ILLink.Tasks/10.0.11 的 Mono.Cecil（Authenticode Valid、NuGet 签名验证通过）。无需下载包。解析器优先 Desktop WindowsBase/PresentationCore/PresentationFramework/System.Xaml，避免 Core WindowsBase facade。

```powershell
$tool = 'G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/tools/hc-devicecore-trim'
$scratch = 'D:/Codex/2026-10-18/kai-s/work/hc-large-strip/binary-trim'
& "$tool/build-offline.ps1" -ScratchRoot $scratch
# 输出目录必须为空；用新目录保存重现结果，保留已交接和失败样本。
dotnet "$scratch/bin/HcDeviceCoreTrim.dll" trim `
  "$scratch/input-snapshot/HandheldCompanion.dll" `
  "$tool/profiles/hc-devicecore.policy.json" `
  "$scratch/input-snapshot/HandheldCompanion.runtime.json" `
  "$scratch/reproduce-HC-pathfix"
dotnet "$scratch/bin/HcDeviceCoreTrim.dll" resource-check `
  "$scratch/reproduce-HC-pathfix/HandheldCompanion.dll" `
  "$scratch/reproduce-HC-pathfix/resource-reader-verification.json"
```

可用原 runtime 中相同 hash 的 DLL/manifest 代替 input-snapshot，工具会校验 DLL、上游 manifest 和每项资源 hash。`inspect <dll> <json>` 支持任意程序集盘点；`audit` 输出静态消费闭包。未知资源默认保留；签名输入默认拒绝。Modern 实验所用显式新开发签名选项不是交付 profile。

## 删除和保留

移除 111 项：37 张 HC 设备展示 PNG（6,860,748 B 原始数据）、8 张 library/controller 占位图（914,958 B）、HC 自身完整 62 项 BAML 组（469,819 B），以及四项 XInput Hook/virtual-output 载荷（1,725,460 B）。这是资源内容大小；DLL 净减少值另计资源索引/对齐开销。

保留：全部 1,382 个字符串；`Properties.Resources` 原 reader/header/type table；`XInputPlus` INI 字符串；11 个灯光预设；MSI `msiapcfg.dll`、PawnIO `RyzenSMU.bin`；两个小图标；所有原 IL/字段初值与 Win32 资源。字体不在此 HC 的 g.resources 内。外置载荷重复不是删除依据。

## 消费边界（必须保留）

`consumption-evidence.json` 是实际 DLL 的逐项消费者/静态闭包证据，设备根含所有 `HandheldCompanion.Devices.*` 方法、构造器和嵌套回调，再纳入内部方法、类型初始化器、委托目标和 async 状态机。Device 闭包 2,579 方法，ManagerFactory 构造闭包 805 方法，内部引用未解析数 0。闭包内没有本次删除项的直接读路径。ProductIllustration 型号/校准字符串保持；图片流路径消费者为 About/Welcome UI。

ManagerFactory 会构造 ProcessManager/ProfileManager；构造本身不读取四项 XInput 二进制。四项 getter 的实际消费者只在 XInputPlus.EnsureInjectorFiles/RegisterApplication，未落入上述闭包。

**这不是逐型号全构造、完整生命周期或实机验证。** 静态闭包不完备覆盖任意反射、外部回调、虚调用所有可能的动态目标、WPF 延迟主题/字体加载。Host 必须维持 headless devicecore 消费边界：不进入 HC App/窗口 InitializeComponent，不调用 ProfileManager.Start/UpdateProfileWrapper，不进入 XInputPlus 注入/重定向工作流。若主控改变边界，需重审或使用保留资源 profile。

## 已执行验证

- 完整元数据除新 MVID/ManifestResource offset 外逐字节相同，14,035 方法、12,793 方法体逐方法 IL 语义摘要及原始方法头/IL/局部签名/EH 比较通过；1,768 TypeDef（含 `<Module>`）、8,731 FieldDef 身份未变。
- **117 个 FieldRVA 地址和初始数据逐项相同**；原尾部被映射至 `.ymcct`，保持 RVA，不改初值。
- 真正 `AssemblyLoadContext.LoadFromAssemblyPath` 后 `hc.GetTypes()` 返回 **1,767** 个运行时类型。另有 SDK 1,891 类型 GetTypes；两种计数包括不同范围，不能当作实机验收。
- 原 HC 也用 Path API 重采黄金封包；新候选经 Path API 的 **22 项真实 ROG fan callback** 对照通过：三次 28 B 写封包、默认 CPU/GPU 曲线、句柄释放。内存 ACPI 代理，Native import 在 ALC/每程序集末端抛错；不构造真实设备、不启动窗口。
- .NET 实际 ResourceReader/DeserializingResourceReader 成功读取 1,382 字符串和 13 stream；MSI stream 的 MZ 头有效；未知序列化 bitmap 不反序列化。
- 小型 C# fixture 实际编译，原件/裁剪件均 Path 加载执行 branch/exception/finally；en-US/zh-HK/fr-FR ResourceManager 中性回退一致。
- 篡改方法体/错误输入 hash 负例被拒绝；旧 `2a55...` Path BadImageFormatException 已独立重现并保留。

探针可复现（先离线编译 probes 两个项目，输出全部指向 scratch，UseWPF 通过本机 Desktop framework）：

```powershell
dotnet build "$tool/probes/PathLoadProbe/PathLoadProbe.csproj" "-p:BaseIntermediateOutputPath=$scratch/probe-obj/" "-p:OutputPath=$scratch/probe-bin/" --source "$scratch/packages"
dotnet "$scratch/probe-bin/PathLoadProbe.dll" "$scratch/path-load-runtime" "$scratch/path-load-runtime/Microsoft.Windows.SDK.NET.dll" "$scratch/path-load-recheck.json"
dotnet build "$tool/probes/CallbackPathProbe/CallbackPathProbe.csproj" "-p:BaseIntermediateOutputPath=$scratch/callback-obj/" "-p:OutputPath=$scratch/callback-bin/" --source "$scratch/packages"
dotnet "$scratch/callback-bin/CallbackPathProbe.dll" "$scratch/path-load-runtime/HandheldCompanion.dll" '94c575e53c66b8fba54b5afbc4d136400c555366c31cd9d25cca86bac6a441ee' "$scratch/callback-original-path-golden.json" "$scratch/callback-path-recheck.json"
```

`path-load-runtime` 是本轮独立 scratch dependency shadow，HC 必须为上述 hash；它不是正式 runtime 包，不得直接沿用其上游 runtime manifest 部署。回调原件可用原 runtime + 固定原 hash、golden 参数 `-` 重采。

## P1 修复与退役证据

旧 `2a55c11c...46bfc10e` 仅 Stream 加载成功，磁盘 Path API 失败，已撤回。原因是 `.text` VirtualSize 被缩短、尾部仍留原 RVA，形成 Windows 映像加载不接受的间隙。pathfix 保留 `.text` 连续虚拟范围，通过小 RawData + 零填充映射，尾部/原生资源 RVA 均不变。文件大小不增加。失败 DLL、原静态报告与原始失败日志位于 `binary-trim/failure-evidence/HC-2a55-path-load/`。同算法旧 display-only/Modern 实验不得默认消费。

## 历史 Modern 单文件实验（已由闭包交付替代）

Modern 实验曾将 7,405,056 B 减至 2,657,280 B（仅两套 Fluent 字体和两张宣传图），但新开发公钥 token 为 `538ea1f68f629a04`，原 token 为 `cd19e634b9706635`。未完成 HC/Controls/其余 runtime AssemblyRef、BAML 和 packURI 的签名身份闭包 retarget，也未通过真实 resolver/GetTypes/FontIcon ctor 的完整验证。RSA 签名校验不等价于身份兼容；Windows mscoree 检查对原件也返回 false，未宣称其通过。该早期单文件实验仍为 **BLOCKED / NOT DEPLOYABLE**，不得混入新闭包。后续完整三文件交付已完成引用、BAML/friend 身份与运行验证，使用入口见本页顶部。

## 写域

仅 `tools/hc-devicecore-trim/**` 和 `D:/Codex/2026-10-18/kai-s/work/hc-large-strip/binary-trim/**`。未修改生产 payload、Host、SDK、共同构建/导出脚本；未 reset/stash/启动 HC 普通入口或真实设备操作。最终 hash/policy 交接后停止写入，由主控接正式 pin/profile。
