# 灯光 DeviceCore 离线回放

生产代码在 `FanLab/LightSetter/`。组件项目为
`FanLab/LightSetter/DeviceCore/YeManLightDeviceCore.csproj`，程序集独立命名为
`YeManLightDeviceCore`，目标 net10.0，无 NuGet、HC、WPF、WindowsSDK.NET 依赖。
它从锁定 HC 文件原样提取 7 个函数；`extract_pinned.py --check` 同时核验原文件
SHA256、函数生成物和锁文件。不会把一个改名的完整 HC 当作精简组件。

当前只支持精确机型 `HandheldCompanion.Devices.OneXPlayerX1Mini` 的
`color solid`、`color rainbow`、`brightness`。HID 筛选为
`1A86:FE00 / FF00:0001`，要求输出长度为 report ID 0 加 64 字节。传输完成后
释放自己持有的句柄；写超时为 2 秒，不重试。状态、off、预设、其他机型留在
现有 HC adapter，未伪造等效能力，也没有停用原 CLI。

## 生产消费和信任

参数：

```text
YeManLightSetter --device-core-assembly <绝对 YeManLightDeviceCore.dll 路径>
  --expected-device-class HandheldCompanion.Devices.OneXPlayerX1Mini
  --action color --level solid --r 10 --g 20 --b 30 --brightness 50
  [--device-core-sha256 <必须与内置 pin 一致>]
```

路径显式指定。SHA256 必须等于生产源码 `LightingAssemblyPins.cs` 的固定 pin；
调用参数、sidecar 或任意 manifest 均不能增加允许值。还核对程序集名、API 类型
和 `ApiVersion=1`。ABI 只有 string/bool/JSON，能力判断发生在 HC 加载和配置前。
覆盖能力一旦开始 Open/Write，任何失败都直接返回，不通过 HC 重做请求。

可以同时提供 `--hc-assembly`；只有组件明确不覆盖的能力才会进入原适配。
旧默认 `--hc-profile legacy` 及旧 HC SHA256 保持。新增
`--hc-profile ymcc-device-core-v1` 尚未启用，必须由主控填入
`Program.ExpectedYmccHcCoreSha256` 和 `ExpectedYmccHcCoreAssemblyName`。
这两个空值不接受运行时配置或自报 manifest 代替。

默认 setter 保留 WPF shared framework，以维持原 HC adapter 启动依赖。
`-p:LightSetterLegacyDesktop=false` 可构建仅需 Microsoft.NETCore.App 的 consumer；
它已通过独立组件回放，但主控在保留 legacy 时应继续采用默认构建。
不修改正式路径、payload 或导出选择。

## 验证

从任意目录执行以下脚本，所有输出写到给定 scratch；不会更新生产 pin：

```powershell
& G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/tools/fixtures/lightsetter_devicecore/verify.ps1 `
  -Scratch D:/Codex/2026-10-18/kai-s/work/hc-large-strip/lighting
```

- 构建独立组件，并验证内置 pin 与不同输出目录的可重复构建。
- 构建保留旧适配及无 WPF shared framework 的 setter。
- `ReplayCore.csproj` 直接链接生产 API、调度和锁定函数，只替换 HID 末端。
- `ReplayConsumer.csproj` 直接链接生产 setter 源码；其测试 pin 只在 scratch
  编译，生产 setter 会拒绝这份回放组件，即使传入它的真实 SHA256。
- `replay.py` 启动这些真实 consumer 代码，逐字节比较完整黄金报文，覆盖正常、
  默认值、数值边界、false、异常、断连、释放错误、pin 和 profile 拒绝及 legacy 边界。
- `LegacyActionReplay.csproj` 直接链接既有 `tools/LightSetterSelfTest` 的测试源码，
  补齐 ProcessEfficiency 和新 consumer/pin 源文件依赖，复用锁定 HC SerialQueue。
  旧测试项目在本任务写域外，未修改。

边界：回放替换的是末端，未调用实际 HID/EC/Host、WMI 或 UI；生产原生 HID 适配器
已构建，设备描述符/写入/灯效实机验证仍待允许的硬件验收。回放成功不表示物理灯效已验证。

## 原生接线与程序集引用证据

`native/lighting_device_core_binding.h` 只包含 `<string>`、内联纯函数与常量。
`ymcc::lighting::useIndependentDeviceCore(deviceClass, action, mode)` 精确限制
X1Mini 的 color solid/rainbow 或 brightness。action/mode 使用原生规范小写；
brightness 不选择灯效，因此 mode 对它无影响。off/status/preset action 拒绝。
DLL 路径后缀相对于 POWER_CONTROL_DIR 为 `\light-setter\YeManLightDeviceCore.dll`。
主控应在独立路由分支前避免强制检查 HC DLL 是否存在；main.cpp 由主控接线。

执行实际生产 header 的 30 项路由组合和 3 个常量检查：

```powershell
python G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/tools/fixtures/lightsetter_devicecore/verify_native_binding.py --scratch D:/Codex/2026-10-18/kai-s/work/hc-large-strip/lighting
```

`AssemblyReferenceAudit.csproj` 使用 PEReader 只读元数据，不加载被查程序集，
输出 assemblyReferences、TypeRef/TypeDef、原生模块和 HC 字符串。
本轮报告为 scratch 下 `assembly-references-core.json`、
`assembly-references-setter.json`、`assembly-references-setter-default.json`。
组件没有 HC/HidLibrary/WPF/WinRT/WindowsSDK 类型或程序集引用；HC 命名字符串
仅精确机型标识。默认兼容 setter 也无这些静态引用，其 WPF shared framework
启动依赖仍由默认构建配置保留。
