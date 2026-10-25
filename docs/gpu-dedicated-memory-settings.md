# 专用显存设置接入与验证

更新日期：2026-10-24。

## 当前行为（r8，2026-10-14）

专用显存组件位于“设置 → 设备”，在充电设置下方；只有设备分区激活才挂载，离开分区即卸载。TDP 页不挂载。2026-10-24 修复设置分区改版后父页面丢失引用、整个组件未挂载的问题，未改 r8 机型、固件或容量准入。菜单仅有“专用显存”、实际当前容量、“读取”、容量选择和“保存”。不展示型号、配置说明或技术介绍。选择不触发写入；一次明确点击“保存”发起设置，不再增加二次确认。保存并完成独立配置回读后提示“请重启”，不自动重启，不承诺重启一定生效。

当前实际容量只取成功的目标核显 DXGI DedicatedVideoMemory 回读，不显示共享预算或用用户目标替代。新 loader 启动序列下，数值目标与真实容量精确一致才提示“已生效”；仍不一致提示“未生效”，配置/启动证据不足提示“未确认”。默认模式仅显示真实容量。失败只显示“保存未确认，请读取”，详细错误进入原有机型能力日志。

先满足完整硬件身份、绑定、驱动、启动、当前配置、可选档位和真实正容量读取，再满足已确认的机型设置通道才显示组件。前端要求 settingRoute=known-model 且提供者为 rog-apu/rog-vram。原生 set 同样执行已知通道准入，不能通过直接 IPC 绕过页面；不支持时不会写设置记录或下发请求。保存时仍重新核对最新读取凭据与绑定，先持久化原设置，再设置并独立回读。

组件在挂载、重新激活和窗口获得焦点时读取；可用时每 15 秒刷新。30 秒以前的快照不能用于保存，即使页面刷新计时器尚未运行。离开页面或文档隐藏时清除快照和轮询，拒绝迟到结果。草稿只在同一 PnP、驱动、LUID 和提供者下保留。

## 当前开放机型

- ROG Ally、Ally X、Xbox ROG Ally、Xbox ROG Ally X：使用匹配机型的 ASUS 配置读取和设置协议。完整回读失败或返回无法解析时隐藏。c4 固件选项优先；仅经典 Ally/Ally X 的 c4 明确零返回才允许 c1 旧协议。
- Intel（包括 MSI G3E）：目前不开放设置。实际专用容量和绑定的 GMM 配置仍可读取为机型能力诊断，但已有注册表值或写入返回成功不能确立驱动接受。r8 的公开读取返回 available=false、settingRoute=read-only，公开设置在下发前拒绝。此限制不根据驱动版本号猜测支持，也不否认未来可加入已确认的型号和驱动通道。
- 其他 AMD：未建立对应机型的配置读取、写入确认与容量回读链时不开放。ADLX VGM 会触发重启，尚未接入本次手动保存菜单；不根据 AMD 品牌套用 ASUS 通道。

读取版本：ymcc-gpu-memory-r8-20261014。机型准入决策随 readDiagnostics.settingRoute 写入既有 machine-capability 日志；不另增采集开关。真实容量、配置和写入拒绝原因保留在日志。

## r8 验证

374 项功能检查（93 契约、46 通道、26 绑定、177 Windows 链路、32 实际 Vue 页面）与 60 项日志检查通过；Vue 类型检查、运行时 /W4 /WX 编译、完整 Native 源码快照编译链接和 r8 导出版本检查通过。18 份公开接口回复进入页面和日志验收，102 份底层候选回复另行保留。

工具仍保留 Intel 旧通道的底层候选读取/写入契约测试，用离线替身验证协议和历史诊断；这些候选返回存入 candidateResponses，不作为公开支持结果交给页面或日志验收。responses 只包含运行实际公开 runtimeRead/runtimeApply 的返回。新增覆盖：四个 ROG 类完整回读及设置、已知机型丢失容量、G3E 当前 128 MiB 与配置 8 GiB 保留诊断但拒绝写入、Intel 冒用 ROG 类也拒绝、单次保存、过期快照、固件拒绝及回读不一致。

本轮回执目录：Build/Validation/GpuDedicatedMemory-20261014/minimal-known-models/release-verification。完整 Native 快照只编译链接，没有运行产品、探测本机 GPU、安装或触发重启。离线检查不等于目标机已完成固件和驱动验收。

## r1–r7 历史记录

以下记录保留当时的实现和证据；其中 Intel 设置开放、重新应用与二次确认页面是旧行为。当前准入和菜单以 r8 为准。

## 源码入口

- src/views/SettingsView.vue：页面挂载。
- src/components/GpuDedicatedMemorySettings.vue：显示门槛、选择、确认保存与生命周期。
- src/bridge/gpuDedicatedMemory.ts：严格解析 IPC 数据及状态文案。
- native/gpu_dedicated_memory_contract.h：适配器校验、快照绑定、配置日志及回读状态契约。
- native/gpu_dedicated_memory_runtime.h：DXGI/DXCore、PnP 匹配、Intel/ASUS 通道和日志持久化。
- native/main.cpp：gpu.dedicatedMemory.get / gpu.dedicatedMemory.set 异步 IPC 注册。

历史研究与固定来源位于工作区 Docs/Research/GpuMemory/20261013/YMCC-显存设置可行性与回读门槛.md。

## 已完成验证

工作区 Build/Validation/GpuDedicatedMemory-20261013/verification.json 记录原生完整编译链接、51 项契约自测、14 项浏览器自测、Vue 类型检查与运行时头文件编译。该私有编译产物未运行。

入口迁移后的复验记录在 Build/Validation/GpuDedicatedMemory-20261013/settings-entry/verification.json：51 项契约自测、14 项浏览器自测、Vue 类型检查及运行时头文件编译全部通过，Steam 只读盘点没有缺失锚点或未归类候选。

同目录 page-integration.json 确认设置页仅挂载一次、位于窗口位置之后，TDP 页已无入口，并核对当前三个原生源文件的哈希与完整编译回执一致。browser/intel-pending.png 为离线模拟示例：保存目标 8 GiB，当前实际回读仍为 4 GiB。

复验命令（在仓库根目录执行）：

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/verify-gpu-dedicated-memory.ps1
# 如需重新完整编译链接，追加 -CompileNative；仍不会启动产品。
~~~

全部测试使用离线硬件/注册表替身；未探测或修改本机注册表/显存配置，未部署、启动 YMCC 或重启。最终实机验收尚需确认目标机的读取、写入后配置读取以及重启后真实容量是否一致。编译与模拟测试不替代这一步。

## 后续通道反查

2026-10-13 补强设备与档位绑定、Intel 自动模式的父节点复核、未知 ROG 返回拒绝回退及设置记录的目标一致性。最新验证为 64 项契约测试、32 项生产通道替身测试、14 项界面测试与完整原生编译。详见同目录 gpu-dedicated-memory-channel-audit.md；原验证回执保留为历史记录。

## G3E 未显示的日志核验

2026-10-13 的 17:47:46 日志确认机型识别与设置页访问，EXE 哈希匹配已含显存接口的发布包。日志缺少显存读取结果，不能判定该机型不支持或指定失败原因。已补精确读取阶段，并在现有详细日志门下将去重后的已有返回结果写入 native-detail.log（gpu-dedicated-memory-read），不新增探测或放宽显示门槛。验证回执为 Build/Validation/GpuDedicatedMemory-20261013/g3e-diagnostics/verification.json；分析见工作区 Docs/Research/GpuMemory/20261013/G3E-174746/REPORT.md。

## G3E 检测兼容性补强

2026-10-13：新增 capacityRead 成功标记，区分 Intel 真实零容量与未读取。新增同一 LUID 的 KMTQAITYPE_PHYSICALADAPTERPNPKEY 软件键绑定；确认物理适配器数量为 1，与当前显示设备的 SPDRP_DRIVER 精确匹配。显式 ControlSetNNN 必须核对 SYSTEM/Select/Current。PCI 数据也存在时，矛盾会拒绝。成功返回的畸形键路径、不完整枚举或多个匹配不能回退；只在查询不可用时保留原 PCI 路径。

本轮验证：69 项契约、32 项通道、26 项生产绑定替身、17 项界面测试，Vue 类型检查、/W4 /WX 运行时头文件编译与完整原生编译链接通过。回执：工作区 Build/Validation/GpuDedicatedMemory-20261013/g3e-adaptation-native/verification.json。

这是可复现检测误拦截的修正，不是对用户 G3E 具体拒绝原因的实机确认。独立只读工具 tools/gpu_dedicated_memory_probe.cpp 复用生产 reader 的可选诊断输出，记录已有查询的原始容量与绑定结果，不执行设置、不读取保存记录、不重启。采集器未在本机 AMD 上运行，且 PE 导入表不含注册表写入/删除/创建或重启函数。

## 主线机型能力日志

2026-10-13：专用显存读取结果改为基础事件 machine-capability，capability=gpu.dedicatedMemory，记录到 native-lifecycle.log。即使详细日志关闭也保留；只记录首次和结果变化，重复轮询不刷日志。包含机型身份、当前是否可显示、未确认原因，以及实际返回的专用容量、读取标记、驱动/节点绑定、当前配置和档位。读取失败记为 unconfirmed，缺失容量不补零，不将失败记录作为硬件不支持的结论。

复用设置页 gpu.dedicatedMemory.get 的已有结果，不启动额外硬件探测；打开设置页再导出普通日志即可检查本机能力，不要求单独采集器或详细日志开关。此项已写入主线 native/main.cpp，未部署或在本机读取/修改 GPU 注册表。原有离线日志写入回归新增 6 项实际生产函数检查，共 52 项及源码一致性检查通过。回执位于工作区 Build/Validation/GpuDedicatedMemory-20261013/mainline-capability-log。

## 自建完整 Windows 链路检测（2026-10-13）

增加 tools/gpu_dedicated_memory_windows_selftest.cpp，在 Windows/DXGI/DXCore/KMT/SetupAPI/注册表底层使用离线替身，直接运行主线 probeWindows、runtimeRead、WindowsOps::write、runtimeApply 与实际隔离文件中的保存记录。测试不调用本机 GPU、注册表或系统内存/启动身份读取，也不运行 YMCC。

自测先复现一处确定的控制流误拦截：Intel 适配器已经被 DXCore 确认为 IsHardware=false，仍继续查询 IsIntegrated；如果此查询失败，整个读取提前退出，后续有效核显无法进入候选。主线 reader 现于确认非硬件后直接跳过该适配器。未知硬件/核显属性、绑定冲突、不完整枚举仍拒绝显示，不根据 G3E 机型强制启用。这是构造场景下已复现的代码问题，不能据此认定用户 G3E 的实际未显示原因。

完整链路新增 64 项，覆盖真实零容量与未读零、缺少模块/工厂、内存与启动身份异常、离散/非硬件/软件适配器、节点绑定和 PCI 回退、注册表缺失/未知/权限/类型错误、别名冲突、配置回读、最终写前驱动/容量/配置变化、写后读取失败、两阶段保存记录失败，以及同一启动待验证、新启动未采用、精确容量验证和 Auto 当前实际容量。写入失败之后页面的后续读取也采用真正的 runtimeRead 返回值：当前容量仍为 4 GiB，准备阶段的目标保持未确认，并保留写入错误。

生产链路生成的 50 份返回值再送入实际 Vue 设置组件及实际能力日志函数。页面确认/保存使用生产 snapshot fence 与生产保存返回，日志在详细开关关闭时保留能力结果且不重复；失败返回不补造容量。tools/log_frequency_selftest.py 可选 --gpu-windows-result 参数负责此衔接，不改变产品日志行为。

最终回执：工作区 Build/Validation/GpuDedicatedMemory-20261013/g3e-selfbuilt-recorded/selfcheck-summary.json。69 项契约 + 32 项通道 + 26 项绑定 + 64 项完整 Windows 链路 + 23 项页面检查，共 214 项；54 项日志检查及其源码一致性检查通过。Vue 类型检查、/W4 /WX 运行时头文件、完整原生编译链接通过。模拟证明当前代码的读取/门槛/保存/验证逻辑，不证明 G3E 的实际驱动采用 DedicatedSegmentSize。

共享 main.cpp 在复核期间有并行任务修改。验证脚本现将原生编译输入保存到本次输出的 native-source，逐文件核对采集与编译后的 SHA256，完整编译对应这份明确源码；记录 sourceDriftAtCompletion 而不回滚其他任务。功能头文件与测试仍要求当前源一致，最终复核还核对当前显存 IPC 注册及能力日志函数与已编译副本相同。编译记录与当前共享源的任何变化记录在 selfcheck-summary.json，不把不同哈希的整文件视为同一编译对象。

复验命令（仓库根目录）：

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/verify-gpu-dedicated-memory.ps1 -OutputRoot G:/YeManCC-Work/Build/Validation/GpuDedicatedMemory-20261013/g3e-selfbuilt-recorded -CompileNative
python tools/log_frequency_selftest.py --output-dir G:/YeManCC-Work/Build/Validation/GpuDedicatedMemory-20261013/g3e-selfbuilt-recorded/logging --gpu-windows-result G:/YeManCC-Work/Build/Validation/GpuDedicatedMemory-20261013/g3e-selfbuilt-recorded/windows-result.json
~~~

## G3E 23:23:56 日志的 DXCore 阶段修正

2026-10-13 23:23:56 导出的 G3E 日志，四个进程的机型能力事件都为 `[dxcore.luid] HRESULT=0x80070057`，机型识别成功，但不能通过 DXCore 获取对应适配器，尚未进入 GMM 配置读取。此前非硬件适配器跳过修正并未覆盖这一步。完整日志分析位于工作区 Docs/Research/GpuMemory/20261013/G3E-2323568/REPORT.md。

Intel 在此特定 E_INVALIDARG 时新增同一 LUID 的内核渲染硬件 + 同一 DXGI 适配器的 D3D12 CacheCoherentUMA 独立确认，核对全LUID、单节点、未移除状态和真实驱动版本；不以机型或显卡名称代替证据。其他DXCore错误拒绝；成功后保留原有PnP/GMM绑定、配置回读、内存上限和重启容量验证。真实专用容量仍来自成功的 DXGI GetDesc1，不从架构推算。分类来源进入保存快照校验，AMD/ROG无此回退。

普通机型能力事件增加 readDiagnostics/classificationSource，包含当前读取内已有的容量、原始 LUID 与各段返回。失败不会补造零容量或沿用旧数据，部分诊断不启用界面。新自测 243 项功能、54 项日志通过，74 份生产返回经真实页面和日志函数检查；类型检查、运行时头文件及完整Native编译链接通过。源码存档及范围回执：工作区 Build/Validation/GpuDedicatedMemory-20261014/g3e-uma-with-readback/source-binding-and-result.json。全部测试离线替身，未在本机或G3E上运行产品/写GPU注册表；修正后实机驱动采用情况尚未确认。


## G3E 2026-10-14 02:39 日志：间接显示适配器筛选修正

本次日志首次提供完整实时证据：真正的 Intel B390 核显 LUID=00000000:0000f24c，DXGI 成功回读专用容量 134217728 字节（128 MiB），DXCore 确认为硬件集显，并成功定位 0001 驱动。第二个同名适配器 LUID=00000000:0001b8bc，DXCore 不提供它，但 KMT 明确返回 renderSupported=false、indirectDisplay=true。旧代码把其 intel.uma-identity/0x887a0004 当成整次读取失败。

现在仅对成功内核类型查询证明不符合物理渲染核显条件的项进行排除，继续完整枚举。未知类型/架构、D3D12 失败、设备移除和身份/配置/容量读失败仍隐藏；不按 G3E 型号或 GPU 名称强制显示。排除项继续写入既有 machine-capability 的 readDiagnostics，包含 disposition=excluded 与 exclusionReason=kernel-adapter-type。

已冻结修前生产代码复现日志故障，再以相同拓扑验证修后显示真实 128 MiB，反向顺序也不改变保存凭据。实际读写、确认页面、保存后的 128 MiB、重启未采用仍未确认、精确新容量才确认均经过离线生产链路回归。263 项功能、60 项日志检查通过；84 份真实生产函数的返回供页面/日志测试使用。完整 Native 源码副本编译通过，当前 GPU 功能和 IPC/日志函数与副本一致。并行手柄任务的后续共享文件改动另行记录并保留。

调查报告：工作区 Docs/Research/GpuMemory/20261014/G3E-023942/REPORT.md。修前/修后证据分别位于 Build/Validation/GpuDedicatedMemory-20261014/g3e-indirect-before 和 g3e-indirect-render-filter。未读取本机 AMD 硬件或注册表、运行产品或部署；目标机驱动是否接受新专用容量仍须重启后实时回读验证。


## 2026-10-14 08:01 日志：运行版本核对与独立组合审查

07:52:23 的能力事件仍走前轮修复之前的 intel.uma-identity/0x887a0004 分支，导出 Native 哈希与 02:39 档案相同。主线包含筛选修复的构建捕获在本地 08:10，晚于该失败事件；不能据此称新修复已在 G3E 运行过。前轮未核对源码修复与目标机测试程序之间的交付版本，是本轮补齐的验证边界。

自建七类适配器并穷举 5,040 种顺序，另测 18 种未知返回位置组合，完整生产读取始终保留真实核显的容量/凭据或明确拒绝。独立找到 DXCore DriverVersion 成功返回零会误准入的问题，已冻结旧代码复现并修正。读写与失败 JSON 新增 readerRevision=ymcc-gpu-memory-r3-20261014，现有普通能力日志保留它；没有增加采集开关或后台探测。

现有构建/打包 identity 链新增 GPU Native 编译版本检查及回执，旧 EXE 不可被作为当前读取版本交付。标记与哈希用于核对编译输入和产物；驱动是否采用显存设置仍依赖目标机真实容量回读。267 个命名功能检查、独立组合、60 日志检查、7 实际 EXE/回执检查和 43 导出链回归通过，85 份生产返回经页面/日志校验。完整 Native 源码副本编译通过；其电池依赖仅在副本里用已有生成器从被捕获源码生成，未改动并行任务文件。最终来源绑定见工作区 Build/Validation/GpuDedicatedMemory-20261014/g3e-proactive-final/source-binding-and-result.json；详细证据见 Docs/Research/GpuMemory/20261014/G3E-080143/REPORT.md。


## 2026-10-14 08:23 G3E 档案：已有 8 GiB 配置、实际 128 MiB

该档案的读取已经成功（available=true），真实 Intel B390 专用容量为 128 MiB，匹配驱动 GMM 的 DedicatedSegmentSize 已是 8192 MiB，8 GiB 在选项内，验证历史为 none。此前页面只比较草稿与配置而禁用保存，后端同配置也直接成功返回；遗漏了现有配置与真实容量不一致的状态。

现在独立展示当前配置和当前容量；Intel 数值配置不一致且没有匹配记录时提示尚未确认，并允许“重新应用”经原有确认、备份、最终快照验证、实际写入和独立配置回读。已保存的同启动目标不重复写；新启动仍是 128 MiB 时未确认，精确回读 8 GiB 才确认。Auto、已与真实容量一致的数值、ROG 同配置保持原有无写入行为。版本为 ymcc-gpu-memory-r4-20261014，既有机型能力日志自然记录这些状态。

直接取证的 Intel FAQ 将 128 MB 说明为兼容报告；其所链接注册表说明是 2015-06-02 的 15.36.21.4222 驱动、另一旧键及 0–512 MB 范围，不能用它承诺 B390 支持 8 GiB，也不能套用其上限或旧键。当前 driver-class GMM 配置能读写不等于驱动已采用。官方来源缓存和完整说明：工作区 Docs/Research/GpuMemory/20261014/G3E-082309/REPORT.md。

已冻结修前生产契约复现“成功返回、零写入、零记录”，修前真实页面缺少不一致提示。修后 81/32/26/127/30 项分别对应契约、通道、绑定、完整 Windows 生产链路、真实页面，共 296 项；60 日志检查、89 份生产返回、Vue 类型检查、运行时严格编译、完整 Native 编译链接和导出版本检查通过。证据：Build/Validation/GpuDedicatedMemory-20261014/g3e-configured8-actual128/source-binding-and-result.json。未探测本机 AMD、部署或执行真实 GPU 写入；目标驱动的 8 GiB 采用能力仍未确认。


## 2026-10-14 11:31 G3E：配置成功但容量未采用，核查下发机制

最新档案中的 Arc B390 / 32.0.101.9033 已读取成功，配置为 8192 MiB，实际驱动报告的专用容量仍为 128 MiB。用户报告已经重启；档案本身未包含 loader 序列和设置记录，不能用进程/LUID 变化补造重启证据。此型号/驱动的该 8 GiB 注册表路径没有通过生效验证，不能宣称已适配成功。

联网核对 Microsoft 注册表与 DXGI 文档、Intel FAQ/IGCL 公开 API 和 B370 社区直接报告。注册表 LSTATUS=0 只确认 Windows 写入；RegQueryValueExW 只回读配置；DedicatedVideoMemory 才是当前驱动的专用容量报告。Intel 官方共享内存 Override 与 IGCL 频率/电压接口不能替代专用容量设置，尚未找到可承诺 B390/9033 生效的公开专用容量 setter 或采用 ACK。B370 用户 2026-09-13 报告 8356 有效而 8993 无效，Intel 版主 2026-09-24 请求核对 9030，尚未确认修复；这一线索不充当 B390 的官方结论。

独立复现并修正仅靠环境 GUID 漏判 loader 重启的问题。新记录包含有检查的 Windows shared data BootId 序列；旧格式、未知布局或不同环境不推断重启。可信新启动下真实容量不一致为 not_effective，页面明确显示未生效，同一失败档位不再写入，显式 Auto 恢复仍按既有确认流程执行。

r6 下发回执包含阶段、API、实际 Win32 返回码、注册表 ACK 范围、独立配置回读与真实容量；driverAcceptance=not_reported，不伪造驱动 ACK。设置结果与读取结果均使用现有 machine-capability 基础日志，只在首次/变化时记录，无额外探测和采集开关。

最终 332 项功能、60 项日志及 5,040/18 种独立组合检查通过；102 份生产返回经过真实页面和日志验证，包含权限拒绝、注册表回执成功但配置不变、配置变化而容量不变、回读失败、真正新启动及精确容量采用。Vue 类型、运行时严格编译、完整 Native 快照编译链接与 r6 导出检查通过。新增权限错误页面检查单独复验并另行绑定来源。

详细证据：工作区 Docs/Research/GpuMemory/20261014/G3E-1131519/REPORT.md、machine-capability-result.json、sources/web-source-notes.json；验证：Build/Validation/GpuDedicatedMemory-20261014/g3e-driver-dispatch-audit/source-binding-and-result.json。没有部署、启动产品、真实写配置、读取本机 AMD 或要求用户反复设置和重启试错。


## 2026-10-14：Intel B390 驱动配置消费与返回路径的独立核查

已核对官方来源的8356、8993、9033、9150驱动文件，保存安装包/解包文件哈希与来源回执。8356旧路径通过IoOpenDeviceRegistryKey(PLUGPLAY_REGKEY_DRIVER)打开目标驱动节点，再读取GMM/DedicatedSegmentSize，位置与绑定节点写入相符。

已检查的新8993/9033/9150参数构造器会清零专用容量覆盖字段；消费者读到零后使用0x08000000（128 MiB）。即使预置8192 MiB，也会被初始化覆盖。这些二进制的该正常路径不能通过提高注册表权限、重复写值或向额外节点写入来恢复配置消费。

9033进一步追到实际DriverEntry注册的渲染DxgkDdiQueryAdapterInfo、内部GMM回调注册、段创建/枚举、QuerySegment/3/4及返回描述符。原始指令在完全虚构的CPU地址空间里执行，12个查询用例返回NTSTATUS=0、Size=134217728；按Microsoft的段标志规则属于DedicatedVideoMemory报告。未执行真实dxgkrnl/DXGI或硬件初始化。

另有5个8356容量用例、15个新驱动容量用例和3个内部非零输入反事实用例通过。模拟中的系统容量、UMA特性标志、间接跳转、memset和分页回调都是显式替身。旧路径8192→8 GiB只证明驱动报告计算，不证明BIOS物理保留或机型可用上限。

目标日志为B390/32.0.101.9033、配置8192 MiB、成功回读128 MiB，与机制吻合；没有目标机实际加载KMD的文件哈希，不把所有9033 OEM变体或所有Intel机型一律判为不支持。8356取样CAB为Lenovo OEM包，未证明匹配MSI子系统，不作为G3E可安装的降级方案。较新9150样本也保留该断点，不能承诺更新即可恢复。

公开IGCL及已审查CTW/HC/G-Helper资料中尚未建立MSI B390的另一条专用容量setter与采用ACK。共享容量、频率和预算不作为替代。主线生产运行时仍为r6，保持真实容量回读、配置独立回读、registry-only回执与可信新启动后的未生效判定；本次补充证据和文档，没有新增未经证明的内核入口。

研究与可复现脚本：[驱动机制报告](../../../../Docs/Research/GpuMemory/20261014/G3E-driver-mechanism/REPORT.md)、[原始Windows查询返回结果](../../../../Docs/Research/GpuMemory/20261014/G3E-driver-mechanism/raw-windows-segment-return-emulation.json)。已更新原G3E机型能力研究记录的offlineMechanismEvidence，历史事件字段原样保留；没有伪造目标机日志、启动产品、读取本机AMD、安装驱动、真实写配置、部署或重启。

## 2026-10-14 15:00 G3E日志与官方BIOS复核：当前不能认定修改能力

本轮日志确认ClawCG3EM / Arc B390 / 32.0.101.9033运行r6。14:57:10配置8192 MiB的RegSetValueExW返回0且配置回读成功，实际DedicatedVideoMemory仍为134217728（128 MiB），driverAcceptance=not_reported；15:00:44仍回读128 MiB。写后记录均为same_boot/pending_restart，本包不作为一次新的重启验证证据。

需纠正能力解释：Intel档位由intelOptions(installedBytes)在YMCC内生成，并非BIOS/驱动枚举出的支持档位。available=admitted(p)证明读取、身份、绑定和配置完整性，不证明驱动能够采用新的专用容量。G3E/9033的能力记录应为“专用容量可读取；注册表配置可写；专用容量修改未建立”，不能把Intel核显或可写注册表等同于已适配设置功能。既有8356样本的有效输入路径不能泛化到G3E或全部Intel，驱动报告变化也不能当作物理显存预留证明。

已读取微星官方CG3EM Launch Pack支持API并下载其当前列出的E1T91IMS.10D（官网发布日期2026-09-16）；ZIP SHA-256与官网公布值完全一致。包内对应MS-1T91的更新说明：.10D仅写EC更新至1T91EMS1.103；.10A（2026-08-07）写“Add new Memory support”及EC更新。这不是已建立的VRAM/DVMT容量菜单或Windows写入接口证据，也不能仅因未写出就断言BIOS不支持。INF中的FirmwareVersion/FirmwareFilename为UEFI固件更新通道，不当作显存设置注册表通道。

研究与机型能力记录：[日志与BIOS复核报告](../../../../Docs/Research/GpuMemory/20261014/G3E-150048-BIOS/REPORT.md)、[独立机型能力分析](../../../../Docs/Research/GpuMemory/20261014/G3E-150048-BIOS/machine-capability-review.json)。原始日志保持不变。本轮仅补证据与主线文档，未修改生产运行时或重新部署；固件仅下载/静态检查，未执行INF/bat、刷BIOS或查询本机AMD硬件/显卡注册表。

## 2026-10-14 AMD路线与r7回读/回执补强

AMD按厂商/驱动实际通道判定。ROG现有c4/c1提供者补齐CreateFileW/DeviceIoControl的实际Win32错误、返回长度、固件方法返回和现有读取诊断。ROG写入acknowledgementScope=firmware-method-return、driverAcceptance=not_reported；WindowsOps独立配置回读还需匹配写入前的绑定。传输成功、方法ACK或配置回读均不代替真实DedicatedVideoMemory。没有历史记录时，ROG配置容量与实际容量不符也显示未确认；同档重新应用仍只属于Intel。读取编译标记更新为ymcc-gpu-memory-r7-20261014。

已固定并检查AMD官方ADLX提交32b5a740d42295c5dfe9026b9f52683da0f3af91：VGM提供支持检测、配置/可用选项、MemoryCarved及SetOption，确为专用内存路线；SetOption会主动触发系统重启，不能套用当前“保存后自行重启、setter返回后probe”的流程。本轮没有接入或执行该setter。HC的Zotac专用CMOS/WMI写法没有建立匹配读取/设置确认链，不移植为通用AMD能力；其他AMD机型继续拒绝未证明通道。

最终363项功能检查和60项日志检查通过，112份生产返回经日志检查；Vue类型检查、运行时头文件编译、完整Native源码快照编译链接与r7导出保护通过。硬件/注册表/固件为离线替身，未进行硬件验收、部署或更新完整包。来源与能力记录见[AMD路线报告](../../../../Docs/Research/GpuMemory/20261014/AMD-VGM/REPORT.md)、[AMD通道能力记录](../../../../Docs/Research/GpuMemory/20261014/AMD-VGM/amd-capability-matrix.json)；最终来源绑定为工作区Build/Validation/GpuDedicatedMemory-20261014/amd-rog-dispatch/source-binding-and-result.json。

## 2026-10-24 ROG 设置入口恢复

日志包 YeManCC-logs-20261024-104347珍贵的Rog睡眠长时间可能.zip 的 machine-identity 明确为 HandheldCompanion.Devices.XboxROGAllyX、RC73XA；同包原生日志没有 gpu.dedicatedMemory 能力或读取回执。主线静态引用核对发现组件和get/set注册还在，但SettingsView及其它页面均未引用它，不能把这一缺失推断为固件拒绝。

恢复SettingsView的原组件import与设备分区v-if=active挂载，原native reader/apply和前端严格解析均未改。复用原gpu_dedicated_memory_browser_selftest增加真实SettingsSections及源页面挂载片段的720/800验证：进入一次读取、打开零写入、离开清理、重入不重放保存。现有单独浏览器27场景通过；可选Windows回执联动及完整GPU/全局套件本轮未运行。旧缺入口页面通过override在SETTINGS_GPU_MEMORY_ENTRY_MISSING断言失败。整体Vue类型检查通过。

新证据：Testing/YMCC/Evidence/RogDedicatedMemory-20261024。离线替身验证入口能消费有效已知机型回读，不等于这台ROG已实测读取、保存或重启生效。本轮未操作安装版、显存或驱动；真实固件读取结果仍待新版本进入该分区后的实际日志确认。