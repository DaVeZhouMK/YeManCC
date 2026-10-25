# 专用显存设置软件模拟

复用全局模拟器，入口为performance.gpu-dedicated-memory-workflow。正式组件GpuDedicatedMemorySettings.vue、gpuDedicatedMemory.ts与ipc.ts在本机浏览器执行；相同原生进程内执行当前native/main.cpp原样提取的reg_gpu_dedicated_memory注册函数、runtimeRead/runtimeApply和实际合同。只有Windows/COM/GPU/固件/注册表和机器身份是已有测试的惰性端口，记录仅写到本轮自有目录。

保留四个原C++自测文件及全部断言，342检查；原浏览器32场景继续执行，输入包含18份真实生产函数生成的公开回执。新增37个桥接/枚举/实际WebView IPC场景和12个真实Vue→同一原生消费者联动，覆盖明确保存、只选不写、旧driver fence拒绝、固件拒绝、ACK丢失不重复写、重复保存阻断、页面失活后的晚到读取、模拟boot身份/独立容量回读及Intel公开只读路线。模拟boot字段不调用系统重启。

原Windows夹具全部主体和原断言不改；被测原生注册函数的文本与当前正式源原字节一致，生成片段与整个来源文件分别记录hash。机器身份、ipc_on注册表、数据根和诊断sink是明确测试边界，未运行生产Main、OS原生IPC调度或真实设备。诊断候选Intel协议测试不会据此开放产品Intel设置路线。

正式源已修复一个能力回执解析缺口：provider/verification必须为字符串，不能把数组用String()强制转换后当作有效枚举。修前同一桥接场景6失败，修后全部通过。修前回执和源原件在父任务证据中保留，不将工具编译错误或旧测试期待错误计作产品问题。

本入口在新鲜证据目录冻结Native头和原测试再编译，所有原生测试启动前检查实际PE导入，设备/注册表/输入等调用必须已替换为惰性端口；CRT自带LoadLibraryExW来源单列。另编译一个有真实DeviceIoControl导入的坏二进制，要求检测拒绝且绝不执行。子进程有界、只回收本次创建对象；正式组件产物记录实际编译读取字节和请求/反馈。

原浏览器脚本只作明确的输出目录迁移和esbuild图记录适配，原断言保留。global正常入口使用Windows job包裹PowerShell和全部子进程。没有HC DLL加载、真实GPU/注册表设置、真实睡眠/重启或用户settings/session/token读取。

每轮输出result.json、bridge-result.json、native/windows-result.json、browser/result.json、live/result.json、原始编译日志、PE导入及dispatch-source.json。PASS仅证明所列软件流程；真实固件接受、实际专用容量、真实重启和安装版完整进程传输仍单列未验收。

## 能力日志与完整响应的区别

`machine-capability` / `gpu.dedicatedMemory` 现已接入已有 ZIP 白名单入口。`-Mode signal-check -Domains performance -Archive <ZIP>` 只保留当前原生生产者的提供者、状态、容量、选项等受限字段；设备、驱动、PnP、启动身份、错误自由文本与其他能力记录不导出。选项按现有 `auto` / `mib:<整数>` 合同验证，枚举数组、字符串数字、重复选项、缺少字段和未知选项不会强转为成功证据。

此记录没有完整原生响应的绑定/fence。日志消费者执行正式 `gpuDedicatedMemory.read`、正式 IPC、解析与展示函数；用于展示的固定内存绑定标签会逐项列为 `source-derived-inert-binding-fixture`，整体保留 `INCOMPLETE`。不调用保存，不接受设备绑定或物理效果。`log-check` 可以根据这一明确能力记录选择已有完整 GPU 工作流；这是相关回归选择，不是原日志的精确时序或实机写入重放。

独立登记 `performance.gpu-log-signals` 验证真实消费者的五种观察和拒绝负例；该回归 PASS 表示限制和行为得到验证，不能把归档日志回放从 INCOMPLETE 改成完整通过。


## 第二轮 GPU 工具与输出身份（2026-10-21）

复用原 B46 26 包/749 文件 Node 工具绑定及原 UI 完整工具权威，browser-tool-inputs.json 原字节 SHA256 为 5e9e3dffbe6533b1b70c4001f34ed8264e52339fedbb7c37cfdeabd859ea23e7，2302 文件/33 目录不变。合并 MSVC 实际 include/lib 与工具后，3061 个 artifactInputs 低于原 3072 上限。只删除未再执行的环境发现脚本/命令，不删除任何 UI 权威成员。实际 CJS require、读取、bundle 图、native include/lib、生成物和浏览器 PID/退出单列；验证哈希读取不算消费者加载。

叶 result.json 和 live/result.json 的 sourceHashes 仅包含 repo 产品源。专用 output-evidence.json 的 kind 为 gpu-memory-owned-generated-output-identities，每一行明确 kind=generated-output/role；sourceHashes 仅作为原公共兼容字节身份通道绑定 caseDir 内生成物。adapter 原 resultFiles 增加该证据，公共 runner 和事后 audit 可独立重验 exe/日志/生成头/实际 bundle/回执等。证据自身由公共 structuredEvidence SHA 绑定；不改变公共源码边界或审计实现。

浏览器仅通过 launchGpuBrowser 创建本轮 headless Edge、临时 profile、回环 websocket 和页面 server；拒绝连接现有用户实例，仅允许访问本次指定回环 origin，并记录正常退出。产品 GPU/固件/注册表/机器身份仍由原 fixture 惰性端口提供；未执行生产 Main、真实设备、睡眠、部署或安装。OS 内部模块加载与系统调用并非本软件证明的范围。

bridge-only 的 36 项与完整 37 项差异来自原 if(process.argv[3]) 的 native.actual-public-reader-responses-enter-real-bridge-parser；没有新增断言凑数。完整工作流保留四组原生 342、原 browser 32、bridge 37、live 12 场景、PE 导入反例与全部原超时。

合入及当前 G 公开 CLI 严格验收归父代理；本模块的局部软件证据不表示全局完成或允许联合整体验收。CPU/功耗收益 null。
