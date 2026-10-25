# Modern headless 三文件闭包交付 — 2026-10-18

状态：**READY-FOR-HEADLESS-INTEGRATION**。主控可据此重新生成正式 profile / runtime manifest / source pins，再执行自身完整构建。仅提交以下三文件整体替换；原 HC `94c575…` 必须同时更新为 `c0e720…`。

交付目录：`D:/Codex/2026-10-18/kai-s/work/hc-large-strip/binary-trim/modern-closure/final/replacements/`。
权威机器清单：`D:/Codex/2026-10-18/kai-s/work/hc-large-strip/binary-trim/modern-closure/final/DELIVERY.json`，同内容副本为本目录 `MODERN-RESULTS.json`。

| 文件 | 字节 | SHA256 |
|---|---:|---|
| HandheldCompanion.dll | 2,774,016 | `c0e7201b099b66e447ddae67dc5e6e6c04e1594fbec4689cbdb2baf320ebfa15` |
| iNKORE.UI.WPF.Modern.dll | 2,658,304 | `5570bbb9d89f92816cd9416a2ba8cf8119fc9b6d4e6bb9648fd17f61bda87fc1` |
| iNKORE.UI.WPF.Modern.Controls.dll | 1,203,200 | `953b57e143a2ba9c376105bef174db1d2e82003c9a6537990f4edfa1f44e65c6` |

Modern 原件 7,405,056 B；新件减少 4,746,752 B。HC 为引用重定位增加 1,024 B、Controls 为签名/身份闭包增加 2,560 B；三文件净减少 **4,743,168 B**。SDK 未改变：1,568,256 B，SHA256 `0da19d45d98c20b46b08ec282bd8a818e5600d482912e2289ba0da591e19b7e0`。

## 修改与签名身份

Modern 和 Controls 使用同一 YMCC 开发公钥，token 从 `cd19e634b9706635` 改为 `538ea1f68f629a04`；HC 仍为 unsigned，绑定名称和版本保留。93 个 runtime 托管程序集全部扫描，恰有三条选定 AssemblyRef：HC→Modern、HC→Controls、Controls→Modern，均闭合到新身份。未更改其他 iNKORE 库的旧身份，未覆盖 HC 与其他引用共享的旧 token blob。

Modern 删除资源严格限定为 `profiles/modern-headless.policy.json` 中带输入哈希的四个 exact entries：两套 FluentSystemIcons 字体、两张宣传 PNG。全部 BAML、主题、noise、字符串保留；Controls 不删任何资源；本轮 HC 不再删资源。104 个资源条目的身份文本按完整身份改写，其中选定 BAML assembly identity 记录共 139 条，其他旧 WPF identity 记录 34 条保持原样。Modern 的 InternalsVisibleTo 完整公钥也更新到实际 Controls 公钥并独立解析验证。文本 sidecar 全扫描，未发现需要改写的完整 token pack URI；simple-name pack URI 的名称/版本保留。

两库的 RSA-1024 / PKCS#1 v1.5 / SHA-1 标准 strong-name 签名经密码学验证。此签名仅为**开发绑定身份**，不是发布签名或发布者认证；Authenticode 状态为 `NotSigned`。公钥见 `profiles/modern-development-public-key.json`；私钥仅在 scratch 的 `keys/YMCC-DeviceCore-Development.private.pem`，不进入共享源码或交付替换目录。输出以新 YMCC runtimeId、derivative 记录和新 SHA 标明派生物，原上游 manifest 只用于输入验证。

## 已通过的实际验证

证据根目录为 `final/closure/evidence/` 与 `final/validation/`，以下路径相对此次交付根目录 `.../modern-closure/final/`。

- `validation/path-load-gettypes.json`：实际 `AssemblyLoadContext.LoadFromAssemblyPath`，HC 全部 **1,767** 个运行时类型成功加载。
- `validation/headless-construction.json`：请求与实际解析 identity/token 严格一致，Controls 全部 **445** 个类型加载；7 个已审阅纯构造全部成功，native 尝试为 0。构造对象为 Modern FontIcon、FontIconSource，HC GlyphIconInfo、OnScreenKeyboardCommands（不 Execute）、DelegateCommand、RelayCommand，以及 Controls 内部 CommandBarFlyoutCommandBarTemplateSettings。friend 公钥与实际加载 Controls 公钥一致。
- `validation/actual-callback-path-comparison.json`：Path API 下 **22 项**真实 HC memory-ACPI 回调与原件黄金封包一致，native import 在末端阻断，未构造真实设备。
- `closure/evidence/full-runtime-reference-closure.json`：93 托管程序集引用闭包通过；选定旧身份没有留在有效 metadata/资源文本中，其他引用身份保留。
- 各程序集 `verification.json` 与 `methods.json`：**21,498 方法 / 20,071 方法体**逐方法比较通过，方法头、原始 IL、局部签名、EH 字节一致；定义/引用 metadata token 保持原值，118 个 FieldRVA 地址和初值保持原样。批准的变化仅身份元数据、MVID、资源内容/偏移与物理 PE 布局；原方法 RVA、native relocation/resource 保留。
- `validation/negative-mixed-identity.json`：manifest 哈希正确但混入原 token Modern 的 runtime 被 identity mismatch 明确拒绝，构造 0、native 尝试 0。保留原始失败日志。
- 两份 `validation/*.resource-reader.json`：真实 .NET ResourceReader 校验通过。
- `validation/independent-baml-friend-audit.json`：独立 BAML/friend 解析结果；`independent-audit-provenance.json` 证明 attempt-03 与 final 的三文件哈希一致。

**已知基线限制与消费边界：** Modern 原件与候选均只能加载 324 个运行时类型中的 323 个；剩余一个类型依赖原 runtime 已缺失的 `iNKORE.UI.WPF, Version=1.2.8.0, PublicKeyToken=cd19e634b9706635`。HC 全类型、Controls 全类型及上述具体构造均通过。未加入 shim，不声明完整 Modern UI 支持。未构造设备、Application 或窗口，未应用模板、创建视觉树、渲染、度量/枚举字体、访问 glyph，也未执行系统命令。DesktopLayoutCommands/MainWindowCommands/QuickToolsCommands 存在设置或窗口/overlay 副作用，审阅后未执行；完整 ModelFactory/GetCurrent 设备构造及硬件生命周期仍不属于本验证。消费方必须维持 headless 边界；改变边界需恢复字体/图资源并另行验收。

## 离线复现

使用本机 .NET SDK、Desktop 10.0.11 与已经核验的本机 Mono.Cecil。无包下载；Desktop-first resolver 避免 WindowsBase stub。脚本在自身进程补齐 dotnet PATH，所有构建/探针输出进入 scratch。

```powershell
$tool = 'G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/tools/hc-devicecore-trim'
& "$tool/build-modern-closure.ps1" `
  -OutputDirectory 'D:/Codex/2026-10-18/kai-s/work/hc-large-strip/binary-trim/modern-closure/reproduce-final-01'
```

输出目录必须新建且为空。默认冻结输入为 `binary-trim/modern-closure/input-snapshot`，源 manifest SHA256 `276e47b3e477e2a05c67a468fb4a33878a6d7240e2d2178099bad439189c82e4`；Modern 输入 SHA256 由 policy 固定，全部 manifest 文件逐个核验。使用同一 scratch 私钥和本工具 public-key policy。底层工具入口是 `modern-closure <input-runtime> <input-manifest> <policy> <new-output-directory>`；封装脚本继续执行 Path、回调、纯构造和资源读取验证。

脚本只读读取主控现有 profile，将其已批准的 8 个 native XInputPlus loose exclusions 应用于新 scratch runtime；正式 profile 未修改。此次只读 profile 快照为 `final/consumer-profile.readonly-snapshot.json`，SHA256 `bff6907b35795ddea57f82beb6fa0f79299f0af43d650781076cda07ba95729b`。如主控 profile 后续改动，应传 `-ExistingProfilePath` 指向此快照以复现本次相同边界；新 profile 的最终闭包由主控重验。

本次 scratch runtime 为 111 个 manifest 文件、32,486,755 B；相同 exclusions 的输入为 37,229,923 B。没有 private PresentationCore，也没有已排除的 8 个 XInputPlus loose 文件。runtimeId 为 `YMCC-DEVICECORE-MODERN-HEADLESS-20261018-e1e547088b009a75-profile-aligned`，manifest SHA256 `77afd0479f8d9d936dcb8358efa7ee65d4e5e630dc7f643b321ba4f697977513`。该 manifest 仅为 scratch 交付证据，主控须合并三文件并重建自己的正式 manifest/pins；不要用早期 119-file attempt 替换整个 runtime。

## 变更文件与保留失败证据

新增 `MetadataIdentityEditor.cs`、`MetadataPeRepack.cs`、`ModernClosure.cs`、`ClosureVerification.cs`、`build-modern-closure.ps1`、Modern policy/public-key、`probes/ModernHeadlessProbe/*`；更新 `Program.cs`、`ResourceBlob.cs`、`Verification.cs` 及交付文档。完整当前源文件清单见 `CHANGED-FILES.txt`，哈希回执见 scratch `modern-closure/final/source-receipt.json`。构建日志 `modern-closure/final.build.log` 保留。

attempt-01 的 PE 重定位失败与 attempt-02 的 friend/BAML 身份遗漏原样保留并标为 NOT-READY。attempt-03 的修正同时由独立解析、严格 resolver、内部 friend 构造及旧身份负例验证。更早 HC `2a55…` 的 Path BadImageFormatException 证据继续保留。

本轮仅写授权工具目录与 binary-trim scratch，未修改主控 profile、消费者、生产 payload、sourcepin 或共用构建/导出文件。三文件和 hash 交接后停止写入，由主控统一提升。