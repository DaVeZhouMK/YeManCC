# HcVerbatim 未消费源码退出

本工具只处理 InputHost/HcVerbatim 原8份源码。它们被 SDK 默认 Compile 项收录，但当前产品源码、反射/序列化入口、纯协议解析和普通包其他程序集均无消费者。2026-10-18 已移除这8份源码，保留 SOURCE-NOTICE.md 与 REMOVED-SOURCES.json。

InputHost 原本就没有加载 HC 主程序集。本次减少的是编入 InputHost 的未消费派生源码，不是刚去掉一个 HC DLL。当前固定快照的 DLL 从611,840变为598,528字节，减少13,312字节。HC 运行时目录的23.89 MB不会因本项改变。原普通完整包没有被修改或重新发布。

## 本次证据

证据根：D:/Codex/2026-10-18/kai-s/work/hc-followup-trim/verbatim

- HANDOFF.md、DELIVERY.json：改动范围、准确体积、限制和全部回执。
- attempt-01/frozen：冻结的原始 InputHost、链接源文件和本地第三方 DLL，不执行其中的产品入口。
- postapply-metadata-comparison.json：8,895条元数据/规范化 IL 记录；9个目标类型退出；7项差异均有明确来源，包括3块仅由退出代码使用的静态数组存储、对应3个存储类型及2项失去消费方的系统程序集引用。
- package-reference-audit.json：原普通包157个二进制，117个可读托管程序集；只有 InputHost 自身存在目标泛型成员引用，其他程序集无目标引用，也未找到完整命名空间的UTF-16反射字符串。
- parser-differential-03：Debug/Release各465例、770个断言，完整解析对象逐字对照，202例接受、263例拒绝，无 HIDMaestro SDK 加载。
- negative-consumer-01：故意添加真实消费者，审计正确拒绝，移除后编译也正确拒绝。
- reproduce-02：从已退出源码状态加原始前像重新构建，前后 DLL 分别与首次结果哈希完全相同。

## 离线复现

所有命令都只生成 scratch 文件。使用本地 .NET 10.0.400 与缓存的 Mono.Cecil（microsoft.net.illink.tasks/10.0.11）。NuGet.Config 清空网络源。新输出目录必须尚不存在。下面示例使用 attempt-03、parser-03 等新名称；若已存在请改名，保留旧回执。

```powershell
$repoRoot = 'G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC'
$taskRoot = 'D:/Codex/2026-10-18/kai-s/work/hc-followup-trim/verbatim'
$pythonExe = 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
& $pythonExe "$repoRoot/tools/hc-verbatim-cleanup/prepare.py" --out "$taskRoot/attempt-03" --verbatim-preimage "$taskRoot/attempt-01/frozen/InputHost/HcVerbatim"
dotnet build "$repoRoot/tools/hc-verbatim-cleanup/Audit.csproj" -c Release --configfile "$repoRoot/tools/hc-verbatim-cleanup/NuGet.Config" -p:UseSharedCompilation=false -nodeReuse:false "-p:BaseIntermediateOutputPath=$taskRoot/audit-obj/" -o "$taskRoot/audit-bin"
dotnet "$taskRoot/audit-bin/Audit.dll" "$taskRoot/attempt-03/baseline/InputHost/bin/Release/net10.0-windows/YeManInputHost.dll" "$taskRoot/attempt-03/candidate/InputHost/bin/Release/net10.0-windows/YeManInputHost.dll" "$taskRoot/attempt-03/metadata-comparison.json"
& $pythonExe "$repoRoot/tools/hc-verbatim-cleanup/parser-differential.py" --attempt "$taskRoot/attempt-03" --out "$taskRoot/parser-03"
& $pythonExe "$repoRoot/tools/hc-verbatim-cleanup/negative-consumer.py" --attempt "$taskRoot/attempt-03" --out "$taskRoot/negative-03" --audit "$taskRoot/audit-bin/Audit.dll"
& $pythonExe "$repoRoot/tools/hc-verbatim-cleanup/source-audit.py" --out "$taskRoot/source-audit-03.json"
```

prepare.py 在退出后需要原8份源码前像，并按 REMOVED-SOURCES.json 中原哈希逐件验证；不从网络找相似文件替代。它复制当前源码和锁定依赖，重建带原8份源码的对照基线以及移除后的候选，只编译、不启动产品。Audit 仅读取指定程序集元数据，无 Assembly.Load 或构造函数执行。scan 模式接受每行一个二进制绝对路径的 UTF-8 文件：`dotnet Audit.dll scan paths.txt output.json`。它只读引用表及原始UTF-16完整命名空间字节，不扫描所有第三方方法体或做全库反编译。

parser-differential.py 使用既有四元数工具的源码成员提取器，补入当前的两个纯解析前提；基线与候选均逐字提取相同真实方法。仅对 MSI release gate 使用原值不变的常量，未编入设备代码。其断言覆盖当前真实persona/profile映射；仍然记录接受/拒绝两条路径，不能拿宽泛“都拒绝”充当等价。

## 保留的失败回执

直接运行原 quaternion runner 时，首次被其 Build/Validation 输出目录限制拒绝；scratch 改适输出目录后，quaternion-regression-01 编译发现提取器未带入 TryGetVirtualOutputProfile 和 TryReadImuQuaternion。补全这两项纯源码后，parser-baseline-02 与 parser-candidate-02 都在旧的 valid-frame 断言失败。原测试把 profileIdentity 设成 persona、接受已退出的persona，且预期不完整四元数部分更新，均不符合当前源码。没有修改产品或原有fixture以让其通过；这些失败回执保留，新的当前协议差分回归另外记录。

## 交付边界

未修改 InputHost/YeManInputHost.csproj、Program.cs、其他输入控制器代码、已发布产品二进制、运行时清单、设备/驱动或安装目录。该 csproj 原有并行改动已保留。未运行 InputHost Program/真实HIDMaestro、枚举设备、执行真实输出/睡眠或生成掌机验证包。编译仍有原有5条 CS8602 警告，均位于未改的 Program.cs。未测 CPU/功耗。
