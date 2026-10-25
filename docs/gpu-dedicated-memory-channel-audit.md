# 专用显存读写通道反查

日期：2026-10-13。范围为设置页专用显存，从显示的实际容量反查适配器、配置读取与设置输出，再检查输出后的独立回读。

## 通道对应表

| 环节 | 读取通道与返回数据 | 写入通道与确认条件 |
| --- | --- | --- |
| 当前专用容量 | DXGI 的 DedicatedVideoMemory；DXCore 确认同 LUID 为硬件核显 | 不把用户选择或注册表配置当作当前容量。不同启动会话中，实际读取容量与目标精确相同才确认生效 |
| 适配器绑定 | LUID → D3DKMT 当前软件 PnP 驱动键 → 当前 SetupAPI 显示设备 → SPDRP_DRIVER 节点；查询不可用才保留 PCI 位置路径 | 保存前重新解析绑定，配置回读后再次比较设备、驱动、启动、LUID、节点、提供者、内存总量与可选档位 |
| Intel 配置 | 打开匹配适配器父节点并确认 DriverVersion，再通过同一父句柄读取 GMM/DedicatedSegmentSize | 数值写入为该值的 REG_DWORD；自动模式只删除同一个值。两种操作均复核父节点；不创建缺失的适配器父节点 |
| ROG 新协议 | ATKACPI / IOCTL 0x0022240c；DSTS 0x53545344，设备 0x000600c4；解析实际返回长度内的当前配置和档位 | DEVS 0x53564544 写入同一设备 0x000600c4、使用读取时给出的 raw 编码，再用 DSTS 独立读取 |
| ROG 旧协议 | 只在经典 Ally/Ally X 且新端点返回零时，读取 DSTS 的 0x000600c1；未知返回不能切换通道 | DEVS 写入同一 0x000600c1，使用匹配档位编码；Xbox ROG 机型不采用这一回退 |

前端入口是 gpu.dedicatedMemory.get / gpu.dedicatedMemory.set。两者都在工作线程执行，并共享该功能的互斥锁。写入前确认与实际读取快照对应；保存配置不代表新容量已经生效。所有通道仅面向专用显存。

## 本次补强

1. 读取快照中的可选档位、编码、容量、自动模式语义和已安装内存总量加入绑定。能力变化后，旧的保存请求失效；写入后的回读也必须保留同一绑定。
2. Intel 数值写入与自动模式恢复共用父节点验证。父节点缺失或驱动版本变化时拒绝读取/写入，避免把父节点消失解释为成功恢复自动。通过父句柄访问 GMM，数值分支只允许创建该子键。
3. 旧 ROG 通道仅允许明确的零返回触发。未知状态、截断数据、打不开设备、IOCTL 失败都不能触发猜测性回退。
4. 持久化记录中的目标必须与当前提供者给出的完整档位一致。记录中 ID、raw、capacityMiB 或 removeOverride 不一致时保持未确认，不能据此生成已生效状态。
5. 读取与写入辅助函数复用于生产 WindowsOps。新增测试替换这些函数底层的 Windows 注册表/设备 I/O 调用，断言实际路径、句柄、访问模式、IOCTL、DSTS/DEVS、端点、raw 与输出长度；不是重写另一份实现进行测试。

## 验证结果与边界

- 64 项契约测试通过：读数门槛、身份变化、能力变化、写入回执/配置回读分离、重启确认及记录不一致。
- 32 项通道测试通过：在离线替身节点 0003 上读写/删除，父节点消失和驱动变化时拒绝；核对两种 ASUS 协议封包与未知/短返回等失败路径。0003 是测试夹具，生产并不固定节点编号。
- 14 项浏览器测试及 Vue 类型检查通过。设置页入口与专用容量显示保持现有行为。
- 运行时头文件以 /W4 /WX 编译通过；完整原生编译链接通过。
- 完整验证回执位于工作区 Build/Validation/GpuDedicatedMemory-20261013/channel-current/verification.json；source-match.json 记录当前受检源码与这份回执的 SHA256 一致性。早一轮的编译与通道测试分别保留于 channel-audit、channel-transport，不用较早的共享 main.cpp 编译回执替代当前验证。

测试未读取或修改本机 GPU、注册表或 ASUS 固件，未运行产品、部署或重启。通道匹配与模拟返回不证明目标机固件/驱动会采用设置，Intel G3E 与 ROG 仍需真实写入后的配置回读和重启后的容量回读验收。Intel 注册表通道的界面继续说明这是驱动报告的专用容量，不承诺 BIOS 物理预留。

本次没有证据表明显存通道会造成虚拟手柄信号异常；不把这里发现的通道绑定问题视为手柄问题的已确认原因。

参考实现使用已有固定来源缓存：工作区 Docs/Research/GpuMemory/20261013/sources/GHelper-IntelVram.cs 与 GHelper-AsusACPI.cs，固定提交 bf64c632db4b9dd680b6edde11f64fdd167be3c0。源码绑定与回读入口分别位于 native/gpu_dedicated_memory_runtime.h 和 gpu_dedicated_memory_contract.h。

## G3E 后续绑定与零容量回读验证

G3E 用户日志能确认机型与包含该功能的发布包，不能确认具体拒绝阶段。新增成功读取标记与 26 项生产绑定替身测试，覆盖缺少 PCI 属性、内核路径与当前 ControlSet、PCI/内核矛盾、畸形成功返回、匹配歧义、枚举未完成和读权限失败。成功查询给出错误数据时不选择另一条路径绕过。

最新回执为 g3e-adaptation-native/verification.json：69/32/26/17 项分别对应契约/读写通道/绑定/界面，完整原生编译链接通过。Intel 的成功零容量可以显示，但未读零仍隐藏；重启后零容量不能验证非零目标生效。上述模拟通过不确定用户 G3E 的实际返回，需要该机器的只读 JSON 或包含新读取诊断的日志。


## 2026-10-14 G3E 双适配器实际证据与准入边界

02:39 导出日志已定位具体拒绝阶段：真实 B390 的 DXGI 专用容量为 128 MiB，DXCore 分类、单物理适配器与 0001 驱动绑定均成功；随后同名间接显示适配器在独立 UMA 路径上被拒绝，并导致整次读取失败。最新主线将成功内核类型证明的排除结果与未知/失败结果分开处理。

完整枚举仍必需。确认排除的间接显示等适配器不参加候选，不使用其容量或驱动项；未知类型、架构读取失败或单凭 DXGI_ERROR_UNSUPPORTED 返回不得静默跳过。保存仍使用选中物理核显的完整快照及精确 GMM 路径。machine-capability 的 readDiagnostics 保留被排除项的原始 LUID、类型、HRESULT 与明确排除标记。

修前冻结代码与修后当前生产代码均用日志拓扑离线执行。263 项功能及 60 项日志检查通过，84 份生产返回交给真实设置组件与真实日志函数；完整 Native 源码快照编译通过。新验证来源绑定见工作区 Build/Validation/GpuDedicatedMemory-20261014/g3e-indirect-render-filter/source-binding-and-result.json；其声明并行手柄改动与未验证的目标机驱动采用结果，未读取或修改本机 GPU/注册表。

## 2026-10-14 AMD/ROG回执与现有配置状态

r7在现有ROG传输中记录请求端点、DEVS/DSTS方法、原始raw、实际返回长度和Win32错误。读取诊断区分c4的固件选项与c1的机型协议编码。写入将方法返回值1限定为firmware-method-return ACK，不确认驱动采用；配置独立回读必须匹配写入前绑定，实际容量继续取DXGI。外部ROG数值配置与实际容量不同且没有历史记录时为unconfirmed，页面不借用Intel重新应用操作。

AMD官方VGM有完整的公开支持/配置/选项接口，但SetOption会主动重启；当前保存后自行重启契约不能直接接入。Zotac参考写法及其他AMD路线尚未建立完整回读确认通道，保持不放行。来源和限制见同目录gpu-dedicated-memory-settings.md的AMD路线章节与工作区Docs/Research/GpuMemory/20261014/AMD-VGM/REPORT.md。

363项功能检查、60项日志检查和完整Native源码快照编译通过。包括方法ACK但配置不变、传输成功但固件拒绝、短回复、真实访问错误、重启后容量仍不符以及外部ROG配置不符的生产代码离线检查。本轮未查询本机AMD硬件、修改GPU注册表/固件或部署。最终回执为工作区Build/Validation/GpuDedicatedMemory-20261014/amd-rog-dispatch/source-binding-and-result.json。
