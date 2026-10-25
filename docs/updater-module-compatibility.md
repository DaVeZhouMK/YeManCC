# YMCC 更新器与模块维护兼容

整包更新、模块安装与 SDK 准备共用 `Global\YeManCC.ModuleMaintenance`。锁顺序为模块维护 → GamepadPrerequisites 安装 → USB/IP → 原 SDK Transport/Deploy。后台下载只准备和校验更新包，不取得安装锁，也不执行驱动安装。

`native/module_package_maintenance.ps1` 是输入资源文件事务的源，`tools/module_package_maintenance_embed.mjs` 生成现有原生更新器内嵌头。`native/decky_package_maintenance.ps1` 提供共用互斥与逐文件事务原语。替换前后核对哈希；计划后被其他安装器改写的文件拒绝覆盖；回滚也要重验当前内容。未知文件、用户设置、其他 Decky 插件保持原有所有权。

`tools/package-release.ps1` 在完整 InputHost/HIDM 与共享运行库装配后调用 `Write-YmccInputCompatibilityPayload`，生成 `YeManCC/module-package.json`。发布构建的原有 InputHost、HIDM 锁与运行库校验仍须通过。

公开 0.0.35 的旧更新器会跳过 `feature-assets/virtual-gamepad` 和 `gyro-motion`。为处理首次升级，包内额外携带已绑定 SHA256 的输入资源 ZIP 与文件补齐脚本。HIDMaestroSetup 在后台等待旧更新事务提交，再补齐文件；随后检查实际驱动状态。此步骤本身只写模块文件，不执行驱动卸载或重启。等待、解压、文件处理都有预算，失败保留诊断和恢复数据。输入目标在文件身份匹配前不启动；延后意图由既有生命周期队列接续，界面握手不等待这个任务。

安装忙会直接反馈“等待完成后重试”，保留已校验的更新包。前端不会把模块忙误作缓存损坏，立即重发一次安装请求。正式安装助手持有模块锁直到提交或回滚完成；新进程的启动准备对提交与释放作有限等待。

新安装助手给旧 YMCC 20 秒正常退出时间，再对路径和创建时间都匹配、已绑定句柄的旧进程作强制退出，额外等待最多 15 秒。恢复服务提前收到主动退出意图。常驻子进程仍须符合其既有安装路径边界；强制关闭用已验证 Process 对象，避免仅按 PID 重用误杀。系统驱动/MSI 安装器不走这个强退通道。

BUS 使用原 USB/IP 版本维护状态机。原厂 Inno 卸载的非零返回不能套用 MSI 的成功码；返回失败会停止后续安装。成功卸载但原厂要求重启时先持久化“等待重启”，当次启动不安装替代驱动。下一次启动确认 Windows 启动身份、SDK 与目标，再读取实际安装情况接续；同版/新版保留，未知状态不盲目卸载。HidHide 自己的 MSI 返回码仍沿用其既有处理。

新 UI/默认字段与旧配置通过 `settingsRepository.normalizeSettings` 合并。结构类型损坏的已知对象/数组恢复可用结构，用户值和未知字段保持；未来 schema、坏文件与备份恢复继续走原有显式恢复流程，不将工厂配置覆盖到用户文件。

隔离回归入口：

- `tools/updater_git035_cycle_selftest.ps1`：用正式 035 文件及其原始助手发射代码测试 035→测试036→测试037、退出、回滚、重试；程序外壳为无硬件 Win32 夹具。
- `tools/updater_helper_extract.mjs`：逐条提取真正 C++ 脚本输出，未知插值必须报错。
- `tools/updater_package_validation_extract.mjs` + `tools/updater_package_validation_fixture.cpp`：提取公开 035 的原始 ZIP/版本/布局校验；OS 适配器只执行有界 tar 与真实文件哈希。
- `tools/updater_module_compatibility_selftest.ps1`：循环文件事务、文件占用、外部更新、跨 C#/PowerShell 互斥与真实 setup 的提前延期。
- `tools/updater_running_process_fixture.cpp`：无硬件消息循环、无响应进程与新进程握手。
- `tools/updater_install_interlock_selftest.ts`：真实 updateManager，内存 IPC，覆盖自动准备、手动确认、重复点击、模块忙与清缓存互斥。
- `tools/updater_config_migration_selftest.ts`：实际配置归一化的旧结构/坏类型/未知字段测试。

测试版本 036/037 只写在夹具目录；不修改产品正式版本号。真实驱动安装、实际重启、硬件归还与安装版 WebView2 现场验收须独立记录。

## 2026-10-23 第二轮恢复与交叉启动加固

兼容补齐增加 `native/module_bootstrap_recovery.ps1`。每个临时目录先写 schema 2 所有权标记，绑定安装目标、包身份和创建时的精确文件路径清单。解压、文件写入、回执提交、提交后清理均可在中断后重新核验并接续。校验尚未触及安装文件的失败会清理本次目录；安装文件与管理回执都核验后，才回收其他已知遗留目录。一次最多回收 8 个；未知目录、未知嵌套文件、错误目标、目录跳转和超限内容保持原样。逐文件重新核对路径后删除，只删除空目录，所有权标记最后删除，避免清理中断后丢失身份。

文件已复制但 `.ymcc-input-managed.json` 尚未写入时，后台准备不能只凭文件哈希提前返回。直接手动解压的完整包也会补齐或修正自身的管理回执；文件已完整时不要求兼容 ZIP 仍存在。用户配置仍不参与这些文件事务。

`ModulePayloadBootstrap` 的标准输出/错误读取各最多保留 16 KiB，诊断收尾最多等待 2 秒。继承了错误管道的后代进程不能使准备程序在主辅助进程已退出后一直等待。

BUS 新写入的 x64 重启证据同时绑定启动 GUID 和加载器启动序号；延续安装、重试预算及 Pending 栅栏使用同一规则。非已审定 x64 布局保留原有 GUID 证据路径，不将未知序号写成可误读的空栅栏。

启动准备与输出请求共享原有串行生命周期队列。已知准备仍在运行，或外部维护锁忙时，保留延后意图，不将其记为永久启动故障。锁释放后只排队一次；退出、用户关闭输出、无法核验的模块文件分别保留其终止/拒绝语义。必须重启的 USB/IP 栅栏使用 `usbip-reboot-required`，不进入短暂锁竞争的自动重试路径。

新增验证入口：`tools/updater_bootstrap_recovery_selftest.ps1`、`tools/updater_module_startup_extract.mjs` / `updater_module_startup_fixture.cpp`、`tools/updater_input_prepare_prefix_selftest.mjs`，以及 `tools/fixtures/updater-bootstrap-diagnostics/DiagnosticsTests.csproj`。它们分别使用真实脚本中断点、真实 Win32 互斥、原始准备入口前缀和真实继承管道；驱动及实际安装版验收仍独立。

同版本恢复的旧缺口保留在历史复现中：`tools/updater_same_version_recovery_probe.ts` 只提供没有受保护授权记录时的只读输入，不能将其当作完整安装验收。当前恢复准入、真实安装助手中断与修复、后台下载仍分别验证。

## 2026-10-23 整包中断恢复与外部插帧工具

`native/update_install_recovery.h` 和 `update_install_recovery.ps1` 提供受保护的整包事务记录。目录来自 Windows ProgramData；目录和记录须由 Administrators 或 SYSTEM 所有，关闭 ACL 继承，访问规则仅允许这两者。路径跳转、超限文件和损坏字段均不能授予恢复资格。记录绑定真实 EXE、实际 PowerControl 路径、操作身份、目标版本和整包 SHA256。

EXE 已替换但整包未提交时，只有匹配的未完成记录允许手动同版本修复。普通同版本安装、降级以及后台自动执行修复仍被拒绝。重试通过当前 native 重新生成助手、校验整包与结构，不重放旧的 LocalAppData 脚本。同版本修复再次失败后保留 repair-required；提交后退休授权。缓存已经缺失时，用户可手动重新下载同一份哈希对应的包。

安装助手的存活由 PID、创建时间和镜像路径共同验证。新程序观察到存活助手时不发出第二次安装；既有后台服务的状态观察机会继续更新安装状态，包括游戏暂停下载或关闭自动预下载时。自动下载不关闭 YMCC、外部工具或安装驱动。

公开 035 的旧助手没有这个受保护记录，不能追溯地把普通可写进度文件当成它的授权证明。旧助手正常完成后的新版及未来更新使用新恢复链；首次跨越旧助手的异常中断、受保护目录的真实管理员写入以及物理安装验收分别记录，不能以软件夹具的 ACL 边界代替。

用户明确确认安装时，助手先处理支持的外部插帧工具，再关闭旧 YMCC：固定 OptiscalerClient 路径、固定 LosslessScaling 路径，以及现有快捷应用解析器同样识别的 Steam 库路径。只处理当前会话、精确镜像路径和创建时间匹配的实例。正常退出失败后使用已绑定句柄的进程结束原语；外部工具总处理窗口最多 60 秒，再次自动拉起的实例会使安装明确失败，避免无休止关闭循环。同名但位于其他目录的程序保持运行，不修改外部工具配置。

## HC 必须跟随新版精确交付树

用户于 2026-10-23 明确：升级采用新版文件，不把旧 HC 库补回新版目录，清理旧文件。`native/updater_hc_runtime.ps1` 在已经校验 incoming FanHost/runtime 绑定后，同步实际包内 HC 清单和文件集。它不下载上游原件，不按旧精简 profile 恢复 DLL，也不修改现有 HC 二进制。

HC 从普通 PowerControl 增量复制与普通文件回滚中排除。独立同步对专属不可变 runtime 目录作精确复制和哈希核对，清除新包已删除的库，覆盖大小/修改时间相同而内容不同的文件。其它模块失败不能将普通回滚中的老 HC 文件混回新目录；整包保留待修复事务。已淘汰的已识别 HC runtime ID 目录按路径和非跳转检查清理；无关目录及用户配置不参与。

`tools/updater_hc_runtime_selftest.ps1` 从实际生成助手抽取生产函数，使用当前真实 85 项 HC runtime 文件、真实 robocopy、同大小同时间破坏、文件占用和目录跳转测试。该测试不证明 profile 中 34 项历史 ABI 修复是否应重新精简；它只证明交付文件选择遵循当前包，旧安装文件不能覆盖包内权威。

隔离验证新增 `tools/updater_install_recovery_extract.mjs` / `updater_install_recovery_fixture.cpp`、`updater_install_recovery_selftest.ts` 和 `updater_hook_tools_selftest.ps1`。管理员 ACL 实际写入、真实外部工具与安装版控制、完整发布候选 Build/Package/Verify、设备及 Steam 现场验收仍须独立证据。测试版本 036/037 不更改正式 0.0.35。

安装启动交接另增加 native 回执：主程序最多等待 90 秒，为外部工具的 60 秒总预算留出启动余量；只有相同事务的助手已取得模块维护锁、完成外部工具退出、写入 applying 并核对实际存活身份后，才发布自身主动退出。助手启动失败或在锁竞争中提前失败时，YMCC 保持可用；前端保留已校验缓存，不立刻重发安装。`tools/updater_ready_ack_extract.mjs` / `updater_ready_ack_fixture.cpp` 抽取这段实际 native 逻辑，日志 IO 与单调时钟为显式夹具边界，进程身份核对使用真实自有进程。

2026-10-23 的进程身份读取已经改为 QueryFullProcessImageName + GetProcessTimes，避免把对方 DLL 模块枚举当成路径读取。安装助手的 Process 对象先绑定句柄，再核对路径和出生时间；外部同名进程先用有限权限只读查询筛选，再绑定和重核，不按 PID 或名称单独强退。Windows PS5 源 PS1 使用 UTF-8 BOM；生成的 C++ 脚本内容与编码修正前一致。新助手优先使用自身 PSHOME 的模块目录，恢复从 PowerShell 7 环境继承后 Get-FileHash 等命令不可用的问题；只修改助手自己的环境。

运行中夹具必须收到自身消息循环的 ready 回执后才开始升级。ready 绑定 PID、创建时间及实际 HWND；新进程启动握手也从消息循环发出。正常模式须得到 graceful，挂起模式须得到 forced-owned-parent，不能只凭进程消失宣称正常退出。测试仍使用隐藏启动、屏幕外 1px、不抢前台的自有窗口；不操作安装版。
