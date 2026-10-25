# HidHide CLI 生命周期与 writer fence 定向回归

此入口编译显式 main 中的实际 `hidHideRun`，保留原 `g_hidHideMx`、thread_local 截止、pending/RAII 状态。新 writer 路线还逐字编译相邻的 `hidHideCliReapPendingLocked`、pending JobGuard/Session/attempt，以及真实 `VirtualStartProtection` 类与 main 的 request/power 声明。`currentPowerGeneration` 和 ID nonce 是明确受控输入，不替换 currentRequest 判定。

运行：

```powershell
node tools/hidhide_cli_lifetime_selftest.mjs --out <全新私有绝对目录> --toolchain <已捕获工具链绝对目录>
```

out/toolchain 均必填；拒绝既有目录、产品源码内、工具链内、相对路径及缺失工具链。没有安装目录/AppData 默认值。每个场景使用 runner 创建的独立私有目录和一个新夹具进程，未知后再调用与外部结束后的复核保留在同一进程，不清除生产 pending 状态。

只读选项继续保留：`--main <明确CPP>`、`--before-main <旧CPP>`，以及配对的 `--prior-function <旧函数> --prior-globals <旧声明>`（必须同时提供 before-main 取得旧 thread_local 上下文）。结果明确所选文件是否实际当前 main；固定历史快照不能冒充当前源码。

## 原有覆盖

原 28 场景、154 项检查的 `ordinary` 函数逐字保留，runner 绑定其固定摘要。覆盖截止/准备超时、非阻塞 mutex、终止请求与退出证明的区别、exact pending 的保留与后续 Wait(0)、异常后责任、命令退出码、输出上限、尾部 9000 字节竞争窗口及共享截止。五种原语义反例继续由原断言检出。

Release `/O2 /MT`、Checked `/Od /RTC1 /MT`；不称作溢出检测或动态 CRT 加载验收。可编译反例必须产生终结 JSON、正常返回 1，并包含指定的原断言文本；编译失败、崩溃或超时不算成功检出。

## writer 真实边界

main 必须实际 include `hidhide_cli_writer_runtime.h`，runner 才从该相邻 include 展开这一模块的本地头闭包；不展开整个 main 或整套虚拟 runtime。真实 record/store/Session、process 类型、JobGuard、VirtualStartProtection 均是冻结的实际编译输入。

仅替换 writer runtime 的五个环境/OS 叶：显式私有 record path、固定测试 machine binding、capture、probe、probeJob。parent 使用真实 GetCurrentProcess 的 PID/birth capture；fake CLI child 使用明确受控身份，未知任意 PID 拒绝探测。Job 查询只允许本测试已登记的私有名字，使用真实空 Job 查询。`open/clearSettled/settleOwned` 全部实际执行。

Session/store 与 JobGuard 在任何 fake CLI Win32 宏之前编译。真正的文件属性、读取、atomic replace、CloseHandle、owner 文件独占不被 fake pipe/child 替换。故障通过实际无 share-delete 的文件句柄阻止发布，或实际独占 owner 文件阻止打开。另有一个真实私有夹具进程只尝试打开同一 Session，证明 unknown 持有的 owner 会阻止独立 native；它不启动 CLI、Host 或设备。

新增覆盖：open/reserve 失败零 Create、MayRun 发布失败零 Resume、实际 commit 后原截止到期或 power 改变零 Resume、异常 unknown 保留 process/pipe/JobGuard/Session/attempt、共享 reap 在真正旧 child 结束后清理、正常完成真实 settle、settle I/O 失败保留责任及同记录重试、历史 child Alive/Unknown 拒绝、Dead 后再准入。六种新增编译反例分别绕过 reserve、MayRun、commit 后截止、currentness、lease 保留及真实 settle，须由指定断言检出。

## 不作出的验收声明

CLI 属性、pipe、child、时钟和 ResumeThread 仍为原 fake OS 模型；模型观察持盘 Reserved/MayRun 的时点。JobGuard 创建真实私有空 job，fake child 不在该 job 内，因而本入口仅验证原算法控制流，不证明真实 CreateProcess 原子 Job 归属、子树退出或驱动行为。相关真实子树测试归 118；writer record/store 独立模块测试归 119。此处不重签它们的结果。

无真实 HidHideCLI、产品 YMCC/Host/Steam、驱动、服务、PnP 或设备操作。元数据记录选定函数/相邻状态/五叶变换、原与生成源、实际编译输入、链接库、静态 DLL 与工具身份。无关 main 改动单列，不把选定消费者测试签成整包或硬件验收。
