# YMCC 充电上限：设置、家族能力与验证记录

更新日期：2026-10-16（Asia/Hong_Kong）。

**已接入设置页、原生 IPC、独立电池控制组件和构建/打包流程。统一使用下拉菜单，最低 50%（GPD 精确路线最低 60%，联想固定 80%/100%）。当前有 10 条 Windows 适配路线、40 组精确机型身份规则；这是代码覆盖范围，不是 40 款掌机实测通过。G3E 已有写入和独立回读证据，不代表全部机型或真实到阈值停充均已验证。**

2026-10-16 复核：[Win5 / ROG 现场证据与全部 HC 充电审计](../../../../Docs/Research/BatteryChargeLimit/20261016/README.md)。Win5（G1618-05）没有已登记协议；锁定版与本次联网固定的上游 HC 都未实现该机充电控制。ROG RC73XA 已匹配 ASUS 路线；14 次查询仅证明存在性检查成功，均无阈值回读，日志没有 SET。本轮未升级 HC、未修改产品运行代码、未新增安装包。

## 设置中的行为

入口为“设置 → 充电上限”，位于专用显存设置之前。当前以组件返回的 `batteryPresent=true` 或仍可能运行的软件策略决定显示；`false` 且无软件策略时整项隐藏。此布尔值也包含 Windows 电源查询失败/未知，不能单独证明物理无电池。显示但未匹配机型时只显示“当前不可用”，无可用档位则不显示下拉和应用。

- 联想固定养护模式：仅“电池养护（约 80%）”“充满（100%）”。
- 百分比接口：显示当前适配器允许的常用值，每 5% 一档。ASUS、OneXPlayer、Steam Deck、MSI 为 50–100%；GPD 为 60–100%。这是 YMCC 选项范围，不表示逐固件已验证所有档位。
- AYANEO 软件旁路策略：50–95% 为软件目标；选择 100% 并应用停止策略，恢复原始正常充电状态。
- 读到 77% 等非预设值时保留实际值，显示为不可重新提交的“当前设备值”，不四舍五入。
- 选择档位不写入；点击“应用”才提交。卡片只保留下拉、当前状态、应用，小字为“部分厂家理解的[旁路供电]”，没有重新读取按钮和长段解释。正在读取或应用时禁止重复提交；上次结果不明确时，显式应用先执行内部读取确认，成功后才提交新值。
- ASUS RSOC 只提供存在性与写入回执，不提供当前阈值回读。页面初始显示“当前上限未知”；本会话写入成功显示“当前设置：80%（已接受）”等回执状态，不把目标值伪装成硬件实测值。

## 运行时家族矩阵

| 协议家族 | 已登记的精确身份范围 | 当前实现与限制 |
| --- | --- | --- |
| ASUS ROG Ally | Ally、Ally X、Xbox Ally、Xbox Ally X，4 组 | ATKACPI `0x00120057`；先检查接口存在，写后要求成功回执；无上限回读 |
| Lenovo Legion Go | Go、Go 2、Go S Z1/Z2，4 组 | `LENOVO_OTHER_METHOD` 的 Get/SetFeatureValue，固定养护开关；不暴露任意百分比 |
| MSI Claw 旧协议 | A1M、A2VM、BZ2EM，3 组 | 枚举 MSI_ACPI；Get_Data/Set_Data 块 215 的成功读改写；100% 仅清最高位、保留低位；必须写后回读 |
| MSI Claw G3E | CG3EM / MS-1T91，1 组 | 独立 selector 0 Get_AP/Set_AP、32 字节、byte 5=`target OR 0x80`；100%=0xE4；保留其他字节并独立 Get_AP 回读；初始 0x80 为未知但允许明确应用，禁止未知自动恢复 |
| AYANEO CEc | 2/2S、3、AIR 多派生、FLIP 多派生、KUN、NEXT2 等，16 组；GEEK 为既有板名别名 | ACPI EC `0x1E`，暂停 `0x55`、恢复 `0xAA`；软件阈值、3% 滞回 |
| AYANEO CEii | AIR Plus AMD/Mendocino/Intel/Nxx、Slide，5 组 | 直接 EC RAM **`0xD1D1`**，暂停 `0x01`、恢复 `0x65`；独立于 CEc |
| OneXPlayer X1 AMD 家族 | X1 AMD、Mini、Pro、Apex，4 组 | 直接 EC RAM `0x04A3`；要求 EC `major >= 1 && minor >= 3` |
| OneXPlayer X1 Intel | 1 组 | 同寄存器；要求 EC `major >= 0 && minor >= 67` |
| Valve Steam Deck | 精确 Steam Deck 身份，1 组 | B030/板号 0x0A；1010、1050、1090、1100/板号 0x05；只写上限的一个字节；要求现有 InpOut 驱动，组件不安装驱动 |
| GPD WIN Mini 2025 HX370 | G1617-02 + HX370，1 组 | BIOS 2.10 + EC 2.7，ACPI EC `0x1D`；不外推至 365、8840U、WIN 4/5/Max |

精确身份、CPU 限制和固件条件均在后端再次校验。未知 EC `255.255` 不通过；不会复用主线未知 ONE-NETBOOK → X1AMD 的兜底分类。机型匹配后仍需电池存在、厂商接口/驱动可用、协议状态可识别才开放设置；ASUS 无阈值回读、G3E 已识别的 0x80 未设置状态按各自规则处理，不能捏造当前值。

以下保留研究记录，**未开放 Windows 写入**：OneXPlayer 更广 Linux 表（Fly/F1、G1、Super X 等）、AOKZOE A1X、SuiPlay 0X1。Linux `0xA3` 与 Windows 直接 RAM `0x04A3` 不能混同。X2、X2MiniPro、3 不继承 X1；X2Mini PRO 的 Linux/HC 证据冲突仍待实机核对。其余 AOKZOE、OrangePi、AYN、ZOTAC 等记为尚未建立路线，不表示硬件一定不支持。

## 生命周期与异常处理

`BatteryChargeSettings.vue` → `battery.charge.get/refresh/set/release` → 原生命名管道 → `YeManBatteryHost.exe`。硬件访问单线程串行化；请求携带会话、设备身份、修订号与原始值组成的摘要，拒绝旧页面和外部修改后的旧请求。EC 使用共享互斥量和有限超时；不调用完整 HC `Open()`，不初始化 TDP、风扇、灯光、手柄。

偏好与恢复记录保存在原生宿主选定的数据目录（通常 `%LOCALAPPDATA%\YeManCC`）：

- `battery-charge.settings.json`：明确成功的目标与是否继续管理。
- `battery-charge.recovery.json`：在写硬件前持久化的恢复/未确认事务。

固件阈值只在明确应用和启动恢复时按需写入；有回读的接口若已经是目标值则不重复写入。后台刷新和睡眠恢复不定时重推固件阈值。写入/回读/保存失败后保留未确认记录，阻止重放；内部 `refresh` 确认操作在读取成功后解除未确认状态，并停用旧目标的自动重放；页面由显式应用调用它，不再单设“重新读取”按钮。

软件限充每两秒检查电量和 AC/DC：到达目标暂停，降至目标减 3% 恢复；电量未知或离开交流电时恢复。仅接管原始为正常充电的旁路。正常退出、父进程结束和睡眠时尝试恢复；睡眠恢复后仅重新建立已保存的软件策略。离开设置页面不会停止策略。检测到其他软件修改状态时停止接管，不覆盖无法确认归属的状态。

恢复记录先于旁路写入落盘。若电池宿主本身被强杀，不能保证当时立即恢复；下一次启动会先按记录恢复，再处理保存的意图。读不到硬件或记录与机型/固件不符时不猜测写入，页面报告失败并保留停止入口。损坏或跨设备记录需要先在厂商软件核对正常充电状态，再处理对应的本地记录，不能强行套用。

## 源码、构建与打包

- UI：[设置卡片](../src/components/BatteryChargeSettings.vue)、[桥接与状态解析](../src/bridge/batteryCharge.ts)。
- 后端：[控制器](../native/BatteryHost/Controller.cs)、[厂商适配器](../native/BatteryHost/Providers.cs)、[精确身份规则](../native/BatteryHost/battery-routes.json)、[原生桥接](../native/battery_charge_client.h)。
- [生成运行时规则](../tools/battery-charge-generate-routes.mjs)从研究库生成收窄后的 Windows 清单；`--check` 检查一致性。
- [发布组件脚本](../tools/build-battery-host.ps1)已接入 `build-workspace.ps1`，发布 .NET 10 Windows x64 组件；复用固定 HC 包里的 LHM/PawnIO 底层程序集。
- `PowerControl/battery-host` 的 11 个固定文件进入正式打包/安装规则；发布身份校验绑定构建输出、源 payload 和最终文件哈希。用户偏好、恢复记录、PDB、未知 DLL 都不进入此白名单。
- 第三方许可、来源和固定二进制身份见 [THIRD-PARTY-NOTICES](../native/BatteryHost/THIRD-PARTY-NOTICES.md)。

## 已完成的验证

2026-10-13 本轮实际运行：

- 后端 **31 组**测试：精确身份/固件、未知型号不打开底层接口、固定档位、ASUS 回执与未知当前值、MSI 位保留/读失败禁写、四类 EC 地址空间、旧请求、回读不符、持久化失败、旁路滞回、退出/睡眠/崩溃恢复及外部控制冲突。使用模拟传输，无实际充电写入。
- 浏览器 **14 组**：直接加载生产 Vue 组件；固定档位、77% 保留、选择不写入、显式应用、ASUS 文案、软件停止、KeepAlive/异步读取恢复、不可用状态、错误 IPC 拒绝及窄屏布局。
- 研究能力库 **14 组**测试通过；运行时清单生成检查通过；Steam 维护只读审计为 0 缺失锚点、0 未分类。
- `vue-tsc --noEmit`、Vite 生产构建、完整 native 构建、.NET 电池组件 build/publish 均通过。Vite 保留既有大包/混合静态动态导入提示。
- 打包合同测试通过：11 个固定文件完整、构建与源 payload 哈希一致；设置/恢复记录等拒绝打包。后续已完成主线完整正式 release；未执行安装目录部署。
- 在当前 ASUS TUF 台式机执行 `--probe`，返回无电池/不支持；不打开掌机接口、不写充电硬件。
- 实际 C++ 命名管道冒烟通过：原生客户端启动已发布组件，GET 取得桌面身份；带空格和尾斜杠的路径正确传递；不生成用户设置/恢复文件，父进程结束后组件退出。此测试只允许无电池主机，仅发送 GET。

验证输出：`G:\YeManCC-Work\Build\Validation\BatteryCharge-20261013`；隔离构建在其 `isolated` 子目录。截图为浏览器模拟状态，不是掌机实机照片。

复现命令：

```powershell
pnpm run test:battery-charge
node tools/battery_charge_capability_selftest.mjs
node tools/battery-charge-audit.mjs --verify-sources
node tools/steam-integration-audit.mjs
# 构建组件并验证同一输出的打包合同（不会部署）：
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-battery-host.ps1 -WorkspaceRoot <隔离工作区>
powershell -NoProfile -ExecutionPolicy Bypass -File tools/battery_charge_packaging_selftest.ps1 -WorkspaceRoot <同一隔离工作区>
```

仍需逐家族掌机实测：读取 → 设置/回读 → 真实到阈值停充 → 电量下降恢复 → 禁用 → 正常退出/睡眠/重启 → 与 OEM 软件共存。源码证据、mock、编译、厂商回执均不能代替真实电池行为验收。

## 研究证据与历史快照

[机器可读研究库](battery-charge-capabilities.json)记录 11 条研究路线、42 个候选源码类及额外 20 个 Linux 板名；存在重叠，不能累加为支持机型数。该 JSON 的 `runtimeImplemented=false/writeEnabled=false` 是研究阶段的冻结声明，约束只读审计，不是当前产品实现状态；当前实现以本文件和运行时 allowlist 为准。

[完整研究材料](../../../../Docs/Research/BatteryChargeLimit/20261013/README.md)保留原始库、官方页面、上游源码和哈希。主要固定提交：G-Helper `bf64c632db4b9dd680b6edde11f64fdd167be3c0`；HC `a250b19a4b7e29dac3b383f2d1ac517d1af43e4e`；Linux `24a19b953e2a3d0ea5a8c4951a0ab8f32720c907`；GPD 驱动 `044e4591b2ac98b4289d2efd6b88a78dbc9ee5a6`。

实施时补充修正：ASUS RSOC 不提供当前阈值回读；CEii helper 的默认高地址字节为 `0xD1`，因此原研究表简写 `0xD1` 实际对应 `0xD1D1`；Steam Deck 本实现采用一个字节写入。冻结原始研究文件不作事后重写。

## 主线实测交付前复核（2026-10-13）

补齐原生输入校验：拒绝非整数、负数、超过 100、整数溢出、缺失字段及非 64 位十六进制校验标识；每个机型的实际档位仍由后端再次限制。停止软件限充后保存 100% 且停用管理，避免下拉框误回到旧阈值；新增回归确认重启不会恢复已停止的旧目标。

拒绝非法档位或预检失败时，保留已运行的软件策略；输入错误不能意外暂停策略并遗留旁路状态。对应回归覆盖运行中拒绝 77% 非档位、读取失败后仍按原目标恢复充电。

## 主线实测包已导出（2026-10-13）

已通过 `pnpm run release` 的完整构建、打包和产物身份校验。主线程序：
`G:\YeManCC-Work\Mainline\Release\YeManCC\YeManCC.exe`。
完整包：`G:\YeManCC-Work\Mainline\Release\Packages\YeManCC.zip`。

交付前检查包括原生输入类型/范围/溢出/校验标识、31 组后端、14 组浏览器，以及 Windows PowerShell 5.1 / PowerShell 7 的构建清单和最终暂存扫描。正式导出中已逐项对拍 11 个电池组件文件与 ZIP 内容，导出后的组件只读探测通过。

修复正式链路发现的 PowerShell 5.1 根 JSON 数组/默认路径兼容，以及复制与最终扫描对第三方说明文件的规则不一致。安装目录未覆盖，真实掌机充电行为仍待实测。

Build ID：`9fedd0343e2043a347fa4497371ae0c4d46ef95089127249c26830eff8d3010a`。
ZIP SHA256：`87f8ea1f821e92d2193b625994265d64a64b1280e710781c0cf36e74988c61c6`。

[本次交付验证记录](../../../../Build/Validation/BatteryCharge-20261013/mainline-delivery-result.json)。


## 2026-10-14 MSI 实测失败后的修正

- 用户日志：`YeManCC-logs-20261013-2323568-微星G3E手柄首次无乱码.zip`；2026-10-13 22:38:08.575 的 machine-identity 确认为 `MS-1T91 / CLAW 8 EX AI+ CG3EM LAUNCH PACK`，在现有 MSI 路由中。归档没有 battery.charge 请求或返回记录，也没有 BatteryHost 日志；无法证明此次失败的具体阶段，不能宣称已实机定位或修复成功。
- 修正已确认的代码缺口：`GetMethodParameters` 抛错、返回空或缺少 Data 时，使用无输入的 `Get_WMI` 调用取得完整参数容器，保持容器与嵌套 Data 配套，随后只调用一次目标方法。原实现可能在回退前抛错，且仅复制 Data 到原参数容器。依据固定 HC 源码 `Reference/HandheldCompanion/Frozen-HC-BINARY-BATCH12-20260831/WMI.cs` 的 `Set` 参数回退流程；不调整块 215、位定义和机型范围。
- 继续要求成功读取后才写入；Set_Data 已调用后的异常不重试，写后读不到目标值保留未确认恢复记录。
- 新增 `battery-charge-client.log` / `battery-charge-host.log`（各 256 KiB 后保留一份轮转），记录请求阶段、辅助进程 PID/退出码、辅助组件标识、机型与固件信息、WMI 方法/参数来源/HRESULT、目标与回读。前端 IPC 异常进入现有 frontend-errors.log。导出同时包含电池设置/恢复记录和发布组件 manifest，仅复制已有文件。
- 新增真实 WMI 包装流程的可替换接口，测试直接参数、抛异常、空参数、缺少 Data、Data 访问异常、坏模板、失败标志、写入回执丢失、回读未变化；非硬件测试共 41 组。设置 UI 测试 15 组。新增原生启动后立即退出的模拟进程测试，确认退出码被记录并立即返回；没有进行真实电池写入。
- 验证目录：`G:\YeManCC-Work\Build\Validation\BatteryCharge-20261014-MsiFix`。此轮最终包身份以该目录 `mainline-delivery-result.json` 为准。

最终交付：`G:\YeManCC-Work\Mainline\Release\Deliveries\YeManCC-MSI-ChargeFix-20261014-8566fee5.zip`；Build ID `8566fee5304a70bd1f67ac6fd7ff447a440a23a83bd7fdfa6ebf583ebf12a42c`，ZIP SHA256 `0f51790e6e15010c5d5da15844fc7bd3efc5673ebbec431e4be6fd40bb6fa2d4`。主线并行编辑期间使用已核验的固定源码快照独立构建，未跳过发布校验；电池实现文件与主线一致。最终包的 11 项充电组件哈希、原生日志导出标记、真实命名管道只读请求和 Steam 导出身份检查均通过。实机是否解决原故障仍待用户复测。


## 2026-10-14 CG3EM 实测 0x80 拦截：修正协议选择

用户第二份日志 `YeManCC-logs-20261014-023942-微星G3E.zip` SHA256 `49d9f092b38d5655ba439994c12a468380eb89f61ce52a6fec0359555aeb9c76`，确认 MS-1T91 / BIOS E1T91IMS.105。02:32:04.777 和 02:39:13.796 的 Get_Data(215) 都成功返回 flag=1、raw=128；之后本地阈值校验将其判为未知值而禁用设置。没有 Set_Data、Set_AP 或充电设置/恢复文件。上一轮参数回退已在实机工作，本轮现场拦截原因已定位。

重新核验同款设备的第一手开源实测记录：Stev3FrencH/ClawConfigurator，固定提交 `fdb2e3acfbafe94a584425f1185a616d0e329c42`（2026-08-15）。其 2026-08-12 测试验证 Get_AP/Set_AP、选择器 0、byte 5 = percent | 0x80；100% 编码为 E4，不能沿用旧路径清最高位。上游设备 BIOS 为 E1T91IMS.10A，与用户 .105 不同，本版仍以实际成功的 Get_AP 32 字节回执、编码与范围探测作为启用条件。完整源码/实测记录和哈希在 `Docs/Research/BatteryChargeLimit/20261014-msi`；原研究目录和能力目录保持历史快照。

新增单独审查记录 `native/BatteryHost/msi-ap-route-review.json`，生成器仅将 MS-1T91 切到 `msi-claw-cg3em-ap`。家族名称仍为 MSI Claw；其余三组 MSI 身份保留旧协议。总计仍是 40 个精确身份组，Windows 路由增加到 10 条。没有将 0x80 假报为 0%/100%，也不放宽旧协议校验。

Set_AP 使用写入前刚读到的 32 字节包，仅替换选择器和阈值，保留性能模式等其他字段；参数容器沿用 Get_WMI 兼容回退。分别处理 WMI Boolean true/false 与数值状态，检查 Set_AP 状态并独立 Get_AP 回读目标；不因裸回执宣布成功。完整请求/回执的十六进制数据纳入已有有界日志。

49 组后端回归覆盖新旧机型分流、现场旧路径 raw=128、AP 编码、100% 保留高位、非充电字段保留、过期状态、无效回执、裸确认、回读不一致和丢失回执。YMCC 尚未在用户的 .105 固件上实测新协议，结果以复测为准。

CG3EM AP 修复交付：`G:\YeManCC-Work\Mainline\Release\Deliveries\YeManCC-MSI-CG3EM-AP-20261014-39a4ec38.zip`。Build ID `39a4ec3850b1e1d4b59f552f66806edc2049dac0d6da26a256a77c3ef9618059`；ZIP SHA256 `cf9ccb78abed8f880a16df5997a7a8d7e41033197936e5ff01d2514a6cbd439a`。49/15 组后端/界面回归、完整发布和最终包只读通信已通过，修复文件与主线一致，现场 `.105` 固件仍待复测。详细记录：`G:\YeManCC-Work\Build\Validation\BatteryCharge-20261014-MsiRaw128\mainline-delivery-result.json`。
