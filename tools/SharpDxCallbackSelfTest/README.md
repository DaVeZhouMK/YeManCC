# SharpDX callback regression for the existing fan simulator

离线、使用现有 .NET 10 SDK 与本机 ILLink 自带 Mono.Cecil。没有 NuGet 网络源，不安装环境。所有输出、编译产物和破坏副本写入调用者指定的证据目录。三个产品 DLL、profile、manifest、安装包及其他工具均只读。

## 最快接入

恢复三库并更新 manifest 后，在主线根目录执行：

```powershell
pwsh -NoProfile -NonInteractive -File tools/SharpDxCallbackSelfTest/Run-SelfTest.ps1 `
  -OutputDirectory D:/Codex/2026-10-18/kai-s/work/sharpdx-callback-regression-20261019/restored-full
```

默认 `-Scope full`，要求当前 Include shadow 的元数据、初始化、缓存和两个委托槽绑定全部通过。默认 runtime/shared/upstream 使用本仓库现有位置，KnownTrimmedRoot 默认 `deps/ymcc-device-core-runtime/bin`（只读）。可显式传入真实待交付目录及新 manifest SHA256：

```powershell
pwsh -NoProfile -NonInteractive -File tools/SharpDxCallbackSelfTest/Run-SelfTest.ps1 `
  -RuntimeRoot $RuntimeRoot -SharedRuntimeRoot $SharedRuntimeRoot `
  -UpstreamRuntimeRoot $upstreamRuntimeRoot `
  -KnownTrimmedRoot (Join-Path $repo 'deps\ymcc-device-core-runtime\bin') `
  -ExpectedManifestSha256 $newManifestSha256 `
  -OutputDirectory (Join-Path $EvidenceDir 'sharpdx') -Scope full
```

省略 `ExpectedManifestSha256` 时，在准入时捕获当前 manifest 的 SHA256，记录为 `captured-at-admission`；仍逐一核验 manifest 所有文件并比对运行前后 runtime/shared 全树哈希。传入预期 SHA256 则要求严格相等。不要沿用恢复前的 `559d2f90...` 作为新输入的 pin。

脚本成功明确 `exit 0`，失败 `exit 1`；预期失败的子进程不泄漏退出码。运行前先将旧 receipt 改为 running；编译、输入准入或执行中断不会保留旧 passed。独立执行单个 probe 还会以 `2` 表示未处理的工具/参数错误。

## 原 fan 门的接入项

把以下项加入 `tools/ymcc_sim/domains/fan/fan_domain.ps1` 的 `$phases`，建议在 interop 阶段之后、manager 构造之前。由主线集成者修改该文件；本工具没有修改它。

```powershell
[ordered]@{
  id='sharpdx-callback-positive-negative'
  script=(Join-Path $repo 'tools\SharpDxCallbackSelfTest\Run-SelfTest.ps1')
  arguments=@(
    '-RuntimeRoot',$RuntimeRoot,
    '-SharedRuntimeRoot',$SharedRuntimeRoot,
    '-UpstreamRuntimeRoot',$upstreamRuntimeRoot,
    '-KnownTrimmedRoot',(Join-Path $repo 'deps\ymcc-device-core-runtime\bin'),
    '-Scope','full',
    '-OutputDirectory',(Join-Path $EvidenceDir 'sharpdx')
  )
  receipt=(Join-Path $EvidenceDir 'sharpdx\result.json')
}
```

沿用原门的 `exit == 0 && receipt fresh && receipt.status == 'passed'`。本脚本另外检查 scope、manifest pin、负例数量和 native 禁令；这些检查失败就返回非零。

## result.json 字段契约（schemaVersion 2）

| 字段 | 类型 / 成功条件 | 含义 |
| --- | --- | --- |
| `schemaVersion` | integer，2 | 本脚本的结果协议 |
| `status` / `passed` | `passed` / true | 整个请求 scope 的正反例通过 |
| `scope` | `full` 或 `compat` | full 为交付默认；compat 仅供旧裁剪输入对照 |
| `runtimeRoot`, `sharedRuntimeRoot`, `upstreamRuntimeRoot` | absolute path | 本次实际输入 |
| `runtimeManifestSha256` | 64 hex characters | 本次 manifest 精确 pin |
| `checks` | array `{Id, Passed, Detail}` | 每项断言及原因；失败不吞掉 |
| `positiveChecksPassed` | boolean | 除人为破坏副本之外的要求均通过 |
| `knownBadCount` / `knownBadRejected` | integer，9 / 9 | 固定九类破坏副本均命中指定失败原因 |
| `currentCallbacks.enumPassed` / `currentCallbacks.formatPassed` | true / true | 当前原件或候选的真实托管 enum / format 路径 |
| `knownTrimmedRoot` / `knownTrimmedShadowRejected` | path / true | 旧三库作为独立反例，须因缺少指定 Include shadow 根被拒绝，且逐文件保持只读 |
| `currentHcConsumerClosurePassed` | true | 实际 HC 元数据闭包和当前 enum/format 通过 |
| `currentShadowSurface.metadataPassed` | boolean | Include shadow 所需类型/方法/属性/布局契约与原件一致 |
| `currentShadowSurface.bindingPassed` | boolean | 当前库真实执行 shadow 初始化和两个委托绑定通过 |
| `currentShadowSurface.passed` | full 要求 true | 上述两项都通过；不表示调用 shader/native |
| `targetCodeExecuted` | true | 原件/当前库的允许托管路径确实执行；不是只有源码 mock |
| `targetNativePolicy` | `deny` | 目标 DllImport/ALC native 解析一律拒绝 |
| `targetNativeAttempts` | 0 | 主要正例的 native 解析尝试；所有子例另外执行强制零尝试检查 |
| `hardwareAccess` | false | 不枚举/打开真实设备，不启动 Host/driver |
| `sourceInputsUnchanged` | true | runtime/shared 运行前后路径、字节数、SHA256 全等 |
| `restorationAssessment` | object | 当前消费者结论、恢复理由和 Include 所有者原件 pin |
| `limits` | string array | 执行和验收边界 |

编译、准入等提前失败时 receipt 为精简 `{schemaVersion,status:'failed',passed:false,scope,targetNativePolicy,error,...}`；调用方不得要求失败回执具有成功回执的全部字段。构建及参数记录分别在 `build.log`、`run.log`、`invocation.json`，后者包含工具源码哈希和 pin 来源。

## 实际执行范围

1. Cecil 读取当前 runtime/shared 所有托管模块，验证指向三库的 TypeRef/MemberRef，提取真实 `HandheldCompanion.dll` IL 调用，并遍历三库的托管调用闭包；P/Invoke/calli 作为边界记录，不执行。与上游同名 shadow/callback 的删除差异另列，不凭空当成 HC 调用根。
2. 原件与当前库分别在短命独立 dotnet 子进程中执行真实 `EnumDevicesCallback` 构造和委托绑定。合成两个 DeviceInstance 内存记录，经原委托的托管 `Invoke` 解码和添加；检查 GUID、名称、size、type、HID usage、返回值以及 GC 后绑定稳定性。不调用 DirectInput.GetDevices，不通过 native 函数指针调用枚举器。
3. 在零句柄、跳过构造且抑制 finalizer 的 Joystick 对象上，单独执行真实 `GetDataFormat`，检查 RawJoystickState 272 B / 164 项及缓存；逐项比较原件/当前的名称、GUID、offset、flags 和 instance。不调用设备构造、SetDataFormat 或设备 disposal。
4. 使用仅在内存中生成的 Include 接口适配器，执行真实 `ShadowAttribute.Get`、`Activator`、`ShadowContainer.Initialize`、`CppObjectShadow.Initialize`、pointer cache 和 Open/Close 两个委托的 CLR thunk 绑定。Open/Close 方法体不调用；释放测试 shadow 的内存与 GCHandle。
5. 先检查上述根、相关类型初始化器和模块初始化器的 IL，拒绝触达 P/Invoke、calli、NativeLibrary loader 及未审查 Marshal 操作。目标程序集置于专用 ALC，并安装拒绝型 DllImport resolver；任何子例出现目标 native 尝试，包括预期失败的负例，也会使整轮失败。

CLR 托管委托 thunk、GCHandle 和框架堆内存不等于启动外部 DirectInput/D3D native 库。证据同时记录目标 native 尝试和新增目标 native 模块；已加载的托管 SharpDX DLL 按精确路径排除。DeviceInstance 合成布局用 CLR `sizeof` / `ldflda`，与其 unsafe 回调一致；不能用默认 CharSet 的 `Marshal.SizeOf` 替代这一布局。

不证明原生枚举、物理输入、D3D 实例/设备/着色器、COM/Inspectable/ComStream 全行为或 HC Host 启动成功。full 的含义限定为本工具覆盖的完整 Include shadow 元数据与安全绑定面。

主线已于 2026-10-19 恢复三库，runtime manifest pin 更新为 `85d75de442300296de056b487d68d7db9ab9123efa5d51d5245e0c32b2ddf032`。本工具未执行这一新输入；主线应使用 full 命令生成新回执。旧报告保持原样。

## 已知坏副本

| 副本 | 预期拒绝路径 | 是否执行其目标代码 |
| --- | --- | --- |
| enum callback 返回 0 | 指定 `enum-return` 断言失败 | 是，安全托管回调 |
| enum delegate Invoke 改 int64 | 元数据方法签名不符 | 否 |
| enum delegate StdCall 改 Cdecl | 委托属性不符 | 否，避免 x64 约定被忽略造成假阳性 |
| DeviceInstance 原生字段删除 | 元数据布局不符 | 否，避免执行破坏的指针布局 |
| RawJoystickState 删除 DataFormatAttribute | `format-content` 失败 | 是，反射路径 |
| X 字段删除 DataObjectFormatAttribute | `format-content` 失败 | 是，反射少一项 |
| Include 删除 ShadowAttribute | `shadow-pointer` 失败 | 是，初始化查找返回空 |
| IncludeShadow 删除构造器 | `safety` 拒绝 | 否 |
| core 缺少 ToCallbackPtr 名称 | `safety` 拒绝 | 否 |

九类副本只在证据目录 `fixtures/`。此外每轮读取 KnownTrimmedRoot 的真实旧裁剪三库，要求 shadow 根缺失这一明确原因被拒绝，记录 `known-trimmed-shadow.json` / `known-trimmed-input.json`；这不是第十种人为破坏副本。元数据负例会核对具体不匹配原因，动态负例会核对指定断言，不能以任意异常充当检出。

## 2026-10-19 旧裁剪输入结论与保留方式

已执行证据：`D:/Codex/2026-10-18/kai-s/work/sharpdx-callback-regression-20261019/result.json`（收尾前协议 v1，23/23）。该输入 manifest 为 `559d2f90fe7f1c4585d00e0d3d6896141013018bade203eb8ec46b87fead81df`。当前 HC enum/format 纯托管通过，82 个保留托管文件、116 个 HC IL 引用、170 个可达目标方法无未解析引用。三库不能据此被写成“已实测发现当前 HC 动态缺陷”。

上游 Include 初始化/绑定通过，旧裁剪输入缺少此能力。旧输入对照矩阵表明只恢复 core 或只恢复 D3D 不够；core+D3D 原件可恢复被测 Include 面。恢复后已经完整的 current 不再套用“只恢复一个仍必须失败”的旧矩阵断言，而由 current full 正例直接判定。

主线选择恢复三库是为了更完整的动态 ABI 和兼容余量。DirectInput 虽本轮测试通过，其原件额外 95,232 B 也可保留未来消费者能力；三库合计增量 625,152 B。这是能力保守恢复选择，非本次证明了三库当前消费者缺陷。

旧三库已从既有副本按运行前 SHA256 复核，保存到：

`D:/Codex/2026-10-18/kai-s/work/sharpdx-callback-regression-20261019/compat-trimmed-three/`

其中 `snapshot.json` 记录精确原 runtime 路径/hash/bytes、复制来源与旧失败回执；只包含三 DLL，不冒充完整 runtime。需要旧对照时可用工具的 `probe shadow <该目录> <新失败回执>`，预期非零且缺少 shadow 根。旧完整 runtime 的历史分析可用 `-Scope compat`；该模式仍要求 enum/format、输入一致性和所有破坏副本通过，允许 current Include 面被报告缺失。交付 gate 使用 full。

停止新增探测后，最终脚本和 v2 scope 逻辑仅做编译与语法检查，未再次执行目标 DLL。新恢复的运行库须由主线按上面的 full 命令产生新的回执；不能把旧 23/23 当成恢复后新 manifest 的验证。

## 精确原件

根目录：`G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/deps/hc-runtime-upstream/HC-CANDIDATE-0.32.4.0-06c0b954-20260902/`。

| 文件 | 原件 B | 相对旧裁剪增量 B | SHA256 |
| --- | ---: | ---: | --- |
| SharpDX.dll | 275968 | 230912 | `518d45a5aaec84cb37e83ee2cf58c503ab6a25febb8c48b53316340c967e84bd` |
| SharpDX.DirectInput.dll | 151040 | 95232 | `35d9ae6b98c5b68fdc1fcaf6e03c95c82f9305c7355dd911f8841880b42e945f` |
| SharpDX.Direct3D9.dll | 338944 | 299008 | `69701eda7433ac0010aba416b9d9c245cd78694770d4bb6b7541b83bace41d55` |

本工具不复制这些文件到产品目录。`originals.json` 和每轮 `restorationAssessment` 包含绝对路径、hash、bytes。

