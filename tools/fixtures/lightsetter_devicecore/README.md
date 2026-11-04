# 灯光 DeviceCore 离线回放

生产代码在 `FanLab/LightSetter/`。组件项目为
`FanLab/LightSetter/DeviceCore/YeManLightDeviceCore.csproj`，程序集独立命名为
`YeManLightDeviceCore`，目标 net10.0，无 NuGet、HC、WPF、WindowsSDK.NET 依赖。
它从锁定 HC 文件原样提取 7 个函数；`extract_pinned.py --check` 同时核验原文件
SHA256、函数生成物和锁文件。不会把一个改名的完整 HC 当作精简组件。

原 X1Mini v1 接口只支持精确机型 `HandheldCompanion.Devices.OneXPlayerX1Mini` 的
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


## X2MiniPro 精确扩展（HC063，尚未构建/执行）

复用原 ReplayCore / ReplayConsumer / replay.py / verify.ps1，不增加永久 ID、minimum
或父门。无 `-Case` 时只走原 X1Mini 默认流程；原 golden、原 SHA pin 和负例保持。
显式选择与默认分支互斥，未知、空、重复或多余 selector 在构建前失败。

```powershell
powershell -NoProfile -File tools/fixtures/lightsetter_devicecore/verify.ps1 -Scratch <核心证据新目录> -Case x2mini
# 同一原 producer：
python tools/fixtures/lightsetter_devicecore/replay.py --scratch <核心证据新目录> --case x2mini
```

二者择一，不能再各运行一遍。`x2mini` 的编译/执行共用原 240 秒上限，失败即停；
同一 scratch 下不覆盖已有 x2mini 尝试。耗时尚未测量，估计 90～210 秒，不能据此宣称通过。
更小范围：`x2mini-wire`（38 场景、2 次构建，约 55～110 秒）、
`x2mini-admission`（30 managed 场景 + 3 个选择器拒绝控制、2 次构建，约 20～65 秒）、
`x2mini-terminal`（26 场景、1 次构建，约 10～40 秒）、
`x2mini-negative-controls`（4 个编译后语义控制、6 次构建，约 40～100 秒）。
完整 x2mini 为 68 个 managed 场景、26 个 transport 场景、4 个语义负例和 3 个选择器拒绝控制；这些是静态声明，
实际执行集合目前为空。生产器在执行前保存精确选择，消费者逐项核对实际名称、组、数量与顺序。

`source-lock.json.exactExtensions.onex-x2mini-hid-v1` 只追加新事实，保留旧 lock 的全部字段。
固定外源为 HC 证据根 upstream/source/HandheldCompanion，绑定 X2MiniPro→X2→X1 继承、
OneXAOKZOE 和 IDevice 的精确 SHA。oracle 的 SetLedBrightness/SetLedColor、
SendV1Brightness/SendV1SolidColor/BuildV1Command、WithReportID 从这些固定文件生成，
逐方法记录 SHA，不从产品协议实现生成预期。BCL 类型别名、WriteReport 记录器和记录式
Thread.Sleep 是 oracle 的适配边界；保留源方法/字节数组和每区 200ms 事实。
64 是 BuildV1Command payload，65 来自固定 IDevice.WithReportID 的 1+64；
实体 descriptor 未验证，不能把内存 caps 的 65 冒充设备来源。

实际 Program.Main、DeviceCoreConsumer、同库准入、X2MiniProDeviceCore.ExecuteOwned
以及两份实际 core 生产文件进入原 ReplayConsumer/ReplayCore；不以新 codec 副本替代实际入口。
原 LightingDeviceCore.Execute 和 X2MiniProDeviceCore.Execute 对 owned X2 的拒绝也单列验证。
双色主区 01/02/07、副区 05/06、solid/breathing、亮度阈值与越界、参数默认/夹值、
ignored speed 和源等待逐项比较。off 固定为五区 brightness0 再五区 solidblack，保留
lighting.off/ok/off-partial/首失败；没有 SetLedStatus 或缓存猜测。
祖先 PresetDeclared 保持 true，但无五区预设消费者，PresetAvailable=false。

真实 owner lease 仅在 scratch 生成私有副本：替换相邻两个 named-object 名称常量，
每次使用唯一私有前缀，保存原 SHA、生成 SHA、精确单 hunk patch 和反向还原证明。
ABI offsets、heartbeat、token、代次、撤销、deadline、Dispose 逻辑仍是原源。
不打开、改写或按名清理 Local\YeManCC-OXP3-OwnerLease / Revoked 产品对象。
取消、阻塞写、代次更新、事件撤销、短写异常和 Dispose 异常均在离线终端及私有 owner 中驱动。

TransportReplay 编译实际 WindowsHidTransport 的完整类，保留路径、属性、caps、65 字节、
复开 PID fence、Write/Dispose 逻辑；仅 Native P/Invoke 与 FileStream/打开句柄是内存终端。
枚举调用直接拒绝。核 1A86:FE00/1305、FF00:1、报告 64/66 拒绝、错误 report ID、
断开/身份变更、FE00↔1305 复开拒绝、取消/短写异常/资源归还。FileStream.WriteAsync
是完整写或异常合同，不返回 byte count；短写控制指终端先接纳前缀后抛异常，未声称能识别静默丢字节。

四个语义负例仅在 scratch 变异实际 PinnedX2MiniProProtocol：旧 side0、丢副色、少一个 zone、RGB 错序。
每个必须先编译成功，再只执行原 `core-five-zone-secondary-solid`，且失败必须来自五区 oracle
比较断言。编译错误、启动错误、其它异常或执行集合不符均不算有效语义拒绝。

## 原 native consumer 的 owner 定向选择（HC063，尚未构建/执行）

```powershell
node tools/hc_lighting_native_consumer_selftest.mjs --out <核心证据新目录> --case x2mini-owner
```

无 `--case` 时原 ROG/MSI/OXP3/其它模型、完整 parent-wait/parent-full 和原失败控制保留。
本 selector 只编译一个原 recipe 的 C++ 翻译单元；沿原 MSVC/SDK preamble，单编译最多
120 秒，选中作用域总共最多 240 秒；估计 35～100 秒，尚无实测。
复用原 59 条 owner 断言，分别覆盖旧 OXP3 和新 X2MiniPro；各补 25 条实际 model/
初始化/身份/债务/归还检查，另 10 条实际 caller 参数和前置 gate 断言，共 178 条；加 3 个选择器拒绝控制，最终选择 181 项。
Snapshot fake 按 actual source 补 requested/bound Model 与 initialized，默认旧 OXP3 初始化完成；
不把新字段伪造成生产 owner 算法。原 owner 函数实际执行，OS named-object/句柄等待在内存边界。

native caller 执行生产 ymccLightSetterApply 从实际 admission 起至唯一 cmd.str() 的原文片段，
记录精确边界/fragment SHA 并编译执行；检查 independent core+pin、owner path/token、双色/
brightness/speed、off 和错误模式的拒绝。CreateProcess/pipe/后续 child receipt 宿主在本 selector
未执行；不能把部件组合写成完整 native/full 发布通过，原默认 full 例没有删除。

本次只修改夹具与静态证据，没有启动任何产品、硬件或 fixed named object，也未运行测试/构建。
具体逐场景计划、前像和净 patch 见 HC063/lighting-tests。


### Owner ABI v2 与默认旧场景的隔离适配

实际 ABI v2 仍为 1136 bytes，modelTag 使用原 reserved 的 offset52；其它 offsets、
`7:9:1` token 和 event 的身份规则保持。所有 fake 常量引用实际 owner 源；Mini tag2、
旧 OXP3 default tag3。同 token/path 的 2↔3 借用、tag0/未知值、v1 对 v2、写中换 tag
必须在返回成功或后续 zone 写之前拒绝。native 使用实际协议 Model/modelForClass，
没有第二份 Model enum。父交付 owner-header/managed-owner/consumer 的最终 SHA 记录在
HC063/owner-model-binding/PRODUCT-EDIT.json，fixture source freeze 已绑定这些精确新事实。

原外部 `tools/LightSetterSelfTest/Program.cs` 未修改。默认 `verify.ps1` 在 scratch 生成原测试副本，
只给 PublishHandoff fake 增加 actual ModelTagOffset/OneXPlayer3ModelTag，逐行保留原断言和负例；
配套 owner 副本仅替换两个相邻 IPC 名称。该生成由 `prepare_legacy.py` 完成，并记录两份
原/生成 SHA、精确 patch、反向还原证明。直接编译 LegacyActionReplay 时必须传这两个源参数，
缺失明确失败，不能意外回到固定产品对象。

native 默认 parent-full 也保留原全部流程/断言，但编入 actual owner header 的私有名称副本，
只改两个 namespace 字符串，记录精确单 hunk 和 SHA；原 Windows process/pipe 断言没有删除。
新 `x2mini-owner` 则始终在内存 OS 边界执行，不需要实际 named object。

`x2mini` / `x2mini-admission` 以及 native `x2mini-owner` 自身验证 unknown、empty、duplicate
selector：子入口预期非零，且不得创建 scratch，证明没有进入默认、构建或业务场景。
这三项是选择器断言；native 的 178 项 owner/caller 断言与它们分别记录。

预算由父原会话累计扣账；两个原入口的 240s 是各自安全上界，不给同一父会话新增预算。
上述未实测估计合计可能超过父剩余额度：不足时保留 NOT_RUN/TIMEOUT，不提高 cap、不自动重试，
也不为这些 case 重跑已完成的 schema/native-parity/input 场景。
