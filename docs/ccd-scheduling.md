# CCD 调度接入性能页

2026-11-05。性能调度页新增 CCD 范围下拉框，自动和手动页面均可操作。位置为 CCD、超线程、降压，等宽排列。设置页保留原来的选择、应用和取消，仅统一范围文案。

性能页显示条件由原生和桥层共同校验：AMD、Windows 已检测到同一效率等级、没有混合大小核、至少两个真实上报的 L3 缓存域，且亲和性掩码能在单处理器组中完整表示。同一封装下，优先使用与 L3 域逐一匹配且完整覆盖封装的 Windows processor die 记录。

2026-11-10 补充：Windows 缺少 die 记录或把它们合并时，允许通过 AMD CPUID `80000026` 的 CCD/Socket 身份补充证明。临时观测线程逐逻辑处理器读取，核对各子项 APIC ID 一致、各处理器 APIC ID 唯一、同一 Socket，再按硬件 CCD ID 汇集 Windows 掩码，与真实 L3 掩码逐一完全相等。`8000001D` 的 L3 sharing 地址位宽须与 CCD 边界相同；逻辑处理器计数只作观察数量上界，不要求等于当前 OS 活动数量，不从核心数均分掩码。允许裁核、APIC 编号空洞及不同 CCD 的活动核心数不同。

这条补充路径复用 `cpu.topology → probeCcd → ccdRuntime → ContinuousPerformancePanel`，不增加型号或 DMI 白名单。诊断字段 `multiCcdSource` 区分 `windows_processor_die` 与 `amd_cpuid_80000026_and_l3`。观测线程恢复自身亲和性后退出；读取、恢复或身份核对失败均不能确认能力。开页不会改变任何进程的亲和性、CCD 租约或业务配置，不增加周期检测，也不接入高频 TDP 浮动路径。未知、单域、混合核心、跨组、单 CCD 内多 CCX/L3 或无效掩码仍隐藏；设置页保留原 L3 域控制能力及操作。

性能页选择后立即复用 `cpu.setCcdMode`；0 为全部域，1..N 对应上报的域。操作是可管理进程的 L3 域亲和性限制，不是物理关闭 CCD。当前游戏 CPU/超线程策略保持原有优先级，不争抢其进程所有权。打开页面只读探测，不自动应用、不增加开机配置；沿用原 CCD 的本次运行语义，不新增自动持久化。

`src/bridge/ccdRuntime.ts` 保留性能页的请求状态，切换自动/手动或页面时不取消已提交请求。进入性能页重新读取原生租约，因此设置页的修改可以被读到；设置页也通过原有探测读到性能页的修改。未知、部分应用和失败回执不会显示为全部成功。提示复用页底区域，4 秒后消失，无新增周期探测。

验证复用原 CCD bridge/native 消费者和性能页浏览器导航入口：`tools/retired_power_pages_selftest.mjs --evidence-dir <新路径> --scope ccd`、`tools/performance_panel_navigation_browser_selftest.mjs <新路径>`。浏览器用真实组件、桥层和执行状态，原生 IO 替换为内存端口；另外验证原生完整 main 的目标文件编译和原降压入口回归。没有对本机进程改亲和性、没有实机多 CCD 效果验收、没有打包或安装。

本轮证据：`G:/YeManCC-Work/Testing/YMCC/Evidence/CCD-Scheduling-20261105`。原失败和每次修正后的独立结果均保留。

2026-11-10 识别修复证据：`G:/YeManCC-Work/Testing/YMCC/Evidence/CCD-Visibility-20261110`。原 CCD 消费者及原生提取消费者、720P/800P 真实组件与正式手柄处理逻辑均定向运行。395 有公开原始 CPUID 样本；388 的裁核输入为模拟边界，不能代签具体 388 机器的 BIOS、拓扑或安装版效果。完整主线构建、部署及 395/388 实机效果另行记录，不沿用历史 PASS 代签。

2026-11-10 Win5 17:33:49 反馈续修：其导出时磁盘 EXE SHA 与 16:56:51 导出包一致，包内已包含首轮 CPUID 修复；不能解释成未更新。对比 Git `v0.0.37` 发现旧版在 mask 为空时按核数均分，新版删掉该猜测后未兼容旧 `CACHE_RELATIONSHIP` 单 mask 格式。现在 `GroupCount=0` 时在记录长度核验后读取真实 `GroupMasks[0]`（与旧 `GroupMask` 联合体同址）；不恢复均分，不放宽多 CCD 身份证明。新增截断、超长计数、错 mask、跨组负例。

同次页面探测的原生结果以 `native-lifecycle.log` 的 `machine-capability / cpu.ccd` 记录，含 `readerRevision`、`performanceReason`、效率等级、L3 原始 GroupCount、实际 mask、封装/die 数及既有 CPUID 采样摘要。性能页的最终决策以 `frontend-trace.log / cpu-ccd-visibility` 记录，含显示/隐藏原因；原生接收端校验字段、长度并去重。只随既有探测及状态变化记录，无定时器、无额外硬件读取，也不触发调度写。原日志包没有这些返回值，不能倒填其实际拒绝阶段。

续修证据位于上述目录的 `win5-173349/`。原生 87 项（含真实 IPC 回包与日志接收端文件写入）、生命周期 8 项及原页面导航通过；Win5 安装版效果仍待包含本次续修的包实测，不能以模拟证明现场已恢复。
