# YeManFanHost 正式包依赖裁剪说明（2026-08-21）

## 结论

当前 `YeManFanHost-real-gated` 是完整 HC 运行时验收包，不是最终发布包。它故意携带了 HC 的全量 satellite resource、测试脚本和证据目录，便于在不同语言/设备环境下定位依赖。

下列目录是语言资源或测试产物，不应进入正式 Fan Host 发布包：

- `cs`, `de`, `de-DE`, `es`, `es-ES`, `fr`, `fr-FR`, `it`, `it-IT`；
- `ja`, `ja-JP`, `ko`, `ko-KR`, `pl`, `pt-BR`, `ru`, `ru-RU`, `tr`；
- `zh-Hans`, `zh-Hant`；
- `cache`, `logs`, `output`。

语言目录中的 `*.resources.dll` 是 .NET/WPF 与 HC 的语言 satellite assembly，不是风扇控制协议。Fan Host 没有 HC UI，正常情况下使用 neutral resource 即可；如未来需要本地化日志，只按目标语言单独加入对应 satellite，不打包全部语言。

## 裁剪验证

已制作 `YeManFanHost-real-min-no-l10n`，移除上述语言和生成目录后，台式机真实只读握手仍通过：

- HC `IDevice.GetCurrent()` 正常返回；
- `fanRouteCount=70`；
- ASUS 未映射 Gate 正确阻断；
- `Open/OpenEvents` 与硬件写入均未调用。

裁剪后仍保留 HC 真实依赖闭包；不能直接按文件名猜测删除其他 DLL。正式版应在真机写入验收后，再基于实际加载闭包做第二轮最小化。

## 框架依赖发布候选

为避免把 .NET 运行时重复塞进 `YeManFanHost.exe`，已另行制作框架依赖候选：

- Host：`YeManFanHost.exe`，24.47 MiB；SHA-256：`D8CBB8E4C31160635647B6F64BDDC2AE86E5A02B6E2FF2B7D5F68BE42EA3B3B3`；
- 真机候选 ZIP：`C:\SOFT\YeManCC-Work\Temp\YeManFanHost-real-framework-dependent-20260821-final-v3.zip`，31.39 MiB；SHA-256：`8E5E7E0D457518985E5D05CFC706A1F81F0CCA62417E958F4D677FE2C46C1A4C`；脚本已采用 UTF-8 BOM，兼容 Windows PowerShell 5.1；
- 需要单独安装 `Microsoft Windows Desktop Runtime 10 x64`，不随 Host 包分发；现有安装器：
  `D:\Win11\TEST\野蛮整合优化系统\2系统环境包\2..NET+VC\windowsdesktop-runtime-10.0.10-win-x64.exe`；
- 运行时安装器 SHA-256：`E82FC901C8F52D716293B2BC0830CE0DD254A06268C457A19E8FC503560A84D1`。

框架依赖候选已通过桌面机安全自测（`mapped-routes-70`、全生命周期 no-write）和真实 HC 只读握手；桌面机因未映射而返回不支持，未执行硬件写入。测试脚本现在会检查 `Microsoft.WindowsDesktop.App 10.x`，缺失时打开官方安装地址并停止，不会静默失败。当前 `v5` 自包含包保持不变，作为无运行时前提的回退验收包。

对 HC 依赖做了元数据闭包复核；仅移除 Serilog sink 会导致 `Serilog.Sinks.File.dll` 缺失并阻断握手，因此不再继续盲删 DLL。v3 修复了真实写入流程中 `Open()` 误要求“已 Open”而返回 409 的状态机错误；现在授权 Gate 通过后才调用 HC `Open()`。
