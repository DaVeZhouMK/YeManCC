# YMCC ROG 掌机 CPU 实机采样

本包用于抓取真实掌机上的 YMCC 主进程、虚拟手柄 Host 和陀螺仪静止峰值。采样器是外部只读工具，不安装服务，不修改设置，不启动或关闭 YMCC、Steam、游戏、手柄、驱动或睡眠。设备和应用操作由你手动完成。

## 带走哪些文件

- `YMCC-ROG-CaptureKit-20261104.zip`：掌机上必需的采样工具。解压后运行 `Start-Capture.cmd`。计数模式只需 Windows 自带的 64 位 Windows PowerShell 5.1，不需要 Node、Python 或编译环境。
- `YMCC-ROG-Candidate-20261104.zip`：本次冻结源码的完整测试候选；保留原 `YeManCC`、`PowerControl` 等目录布局与配套文件。不要只取一个 exe。是否切换版本由你手动决定。
- `YMCC-ROG-Source-20261104.zip`：配套源码、锁定依赖、构建说明、符号和清单；采样时不需要在掌机编译。

候选为 `FastRelease` 测试导出，完整模拟夹具 `NOT_RUN`、正式发布未验收、真实设备验收待本次采样。源码 HEAD 不能代表包含大量未提交修改的构建；以包内 `CANDIDATE-IDENTITY.json`、导出 `export-identity.json` 及逐文件 SHA256 为准。包内构建说明记录了独立快照的构建修复与 helper 重建。

## 先准备一次

1. 将采样工具解压到掌机本地可写目录，例如 `D:\YMCC-CPU`。结果写入同目录 `Results`。预留约 2GB 空间供可选 ETL 与结果压缩。
2. 先用当前有峰值的安装采样也可以。若测试本次候选，手动正常退出旧 YMCC，备份当前目录，按完整包布局准备配套文件，再手动启动对应的 `YeManCC\YeManCC.exe`。沿用掌机已有驱动、WebView2、.NET 10 运行环境；本工具不会安装或切换它们。不要同时启动两份。
3. 保持同一电源模式、同一供电、同一虚拟手柄型号和同一游戏/Steam状态；不做不同型号的排列测试。优先在桌面后台复现。若只在某个游戏复现，记录游戏开启状态并保持各场景一致。
4. 手动在 YMCC 中准备下表状态。设置好后等约 30 秒，避开开启初段；放在桌面，静止场景不碰摇杆、屏幕、鼠标或键盘。开始采样后有 8 秒放置时间。
5. 双击 `Start-Capture.cmd`，选 `4` 检查工具与版本。首次输入正在运行的 **完整 YeManCC.exe 路径**，默认 `C:\SOFT\YeMan\YeManCC\YeManCC.exe`。版本不同会明确记录，仍可采现有安装；不会把它当成本包优化版本。

## 建议先跑这一组（约 8～10 分钟）

选菜单 `1`，每次按提示选择场景。脚本只记场景声明，不会替你开关功能，也不能凭声明证明 IMU 真实绑定。

|编号|你手动设置的状态|采样时长|目的|
|---|---|---:|---|
|0 / S0|虚拟手柄关、陀螺仪关；详细日志关|120 秒|主进程基础后台占用；Host 缺失允许并记录 UNKNOWN|
|1 / S1|虚拟手柄开、陀螺仪关；详细日志关；放桌面|120 秒|真实 Host 与输出线程的静止底噪|
|2 / S2|同一个虚拟手柄，陀螺仪开；详细日志关；放桌面|120 秒|优先抓“没动仍有峰值”|
|3 / S3|保持 S2 配置，缓慢转动掌机，不按键|30 秒|确认真实动作路径与静止不同|
|4 / S4|恢复放桌面，开启 YMCC 设置中的“详细日志”单开关|60 秒|补齐陀螺仪/Host 来源、状态、线程角色和 GC 旁证，单独看日志开销；JIT 证据来自 WPR|

如果时间有限，优先 S1、S2、S4。每次附一句备注：手柄型号、陀螺仪模式/触发方式、电源模式、是否连电、是否看到峰值。触发方式需要持续按键的模式，要写清按键状态；不能把“打开了功能但未触发输出”当成有效的 IMU 工作场景。

S4 后通过 YMCC 原有“导出日志”按钮导出一份正常日志包，不清理旧日志。将其复制到 `Results\YmccLogs`，也可单独发回。无需手改 flag 文件或配置。随后可手动恢复详细日志原状态。

## 再补一次线程/栈（推荐 S2，60 秒）

1. 手动回到最容易复现的 S2，详细日志关，状态稳定后放桌面。
2. 右键 `Start-Capture.cmd` → **以管理员身份运行**，选 `2`，场景选 `2`。工具不自动提权。
3. 定长录制 60 秒。WPR 收集系统调度、执行采样、DPC/ISR，以及 CLR 的 JIT/GC/方法映射及 WaitHandle 等待开始/结束事件；同时外部计数器记录主进程及初始 Host。窗口不需要保持前台。该独立诊断模式的 CLR keyword 为 `0x40000020019`；WaitHandle 位由 CPU1 在当前 TraceEvent/SDK 研究中核实，用于区分实际等待节拍，不能把配置的 8ms 直接当成每秒 125 次醒来。
4. 等待显示结束。只停止本次命名的 WPR 录制实例；录制失败不取消别人正在进行的跟踪。若 WPR 因现有会话/权限/版本而拒绝，保留错误文件，先交回计数结果，不反复重试或取消系统会话。

WPR 是有开销的诊断录制，不能将这一轮数字当作低开销的静止验收。文件已保存也不等于峰值前后完整、没有丢事件或已经解析到源码。回收后需核对 ETL 覆盖、PID/TID 生存期、PDB/模块身份，才做线程归因。

## 收回结果

菜单 `3` 生成 `ROG-CPU-RESULTS-<UTC>-<唯一号>.zip`。把这个 ZIP 和 YMCC 正常导出的日志包发回即可，不需要你先分析。不要在压缩完成前删掉原 `Results`。

每轮包含 `SCENARIO.json`、版本对比、`IDENTITY.json`、`CPU.jsonl`、`SUMMARY.json`；栈轮另有 `YMCC-CPU.etl` 和 WPR 启停记录。整数计数、FILETIME、QPC、cycles 使用十进制字符串保真。

## 怎样理解这次证据

- CPU 同时提供整机归一化百分比和单核百分比；后者可以大于 100。cycles 只保留原计数，不换算毫秒。只比较明确同场景、同版本、同日志开关的窗口。
- 主进程与初始 `YeManInputHost` 后代独立记录，以持有句柄的 PID、出生时间、完整路径、磁盘 SHA256 绑定。只在开始时发现一次进程，不每拍全机扫描；Host 晚启动、重启或更换均为未知，不会接上同名进程。出现退出/权限不足/身份不符会留 PARTIAL/ERROR，不能当成零占用。
- 根进程 + Host 不是整个应用总 CPU；WebView、其他 helper 不纳入该计数和。ETL 可用于另看系统竞争与线程。采样器自身开销独立记录，不从目标 CPU 中相减。
- 详细日志可提供 `gyro-status-second`、`input-idle-window`、`input-host-cost` 等现有记录（以实际导出为准）：raw/cal、gyro/accel序号、sourceFamily、receipt age、Host run/epoch/persona、提交/拒绝和线程角色。加速度重力、四元数、噪声和时间仍可能变化，不能把“零角速度”当成“没有数据”。
- 现有日志未完整给出绑定代次、原始传感器年龄、四元数或后端接收证明时，该项继续 UNKNOWN。采样工具不读取传感器、访问产品 IPC、调用设备 API 或伪造静止输入。
- 本机工具解析/自有测试进程验证只说明采样器能工作，不能替代 ROG 的实际设备、输出消费者、IMU 和睡眠验收。本次不要求睡眠测试。

## 可选命令行

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Collector\rog-field\Start-RogCapture.ps1 -Action Counters -ExpectedImagePath 'C:\SOFT\YeMan\YeManCC\YeManCC.exe' -Scenario S2-IMU-STILL -DurationSeconds 120 -NoPrompt

# 管理员 PowerShell；独立的定长诊断录制
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Collector\rog-field\Start-RogCapture.ps1 -Action Trace -ExpectedImagePath 'C:\SOFT\YeMan\YeManCC\YeManCC.exe' -Scenario S2-IMU-STILL -DurationSeconds 60 -NoPrompt
```

命令行 `-NoPrompt` 不提供放置倒计时；请先手动准备好场景。所有入口是外部手动工具，不随 YMCC 启动或随候选安装。


补充开发用法：当CIM隐藏完整路径或同路径存在多个YMCC实例时，已用原生句柄明确识别的root可给Observe-RogCpu.ps1同时传-ExpectedRootProcessId与-ExpectedRootBirthFileTime100ns，ExpectedImagePath仍必需；错误birth在首样本前拒绝。默认不猜选目标，后起Host不自动重绑。此参数用于当前主线开发工具，冻结交付包需按其原SHA识别。
