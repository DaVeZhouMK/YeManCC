<!-- SINGLE-INPUT-HANDOFF-20261024 -->
当前策略已由用户进一步收敛：虚拟手柄完成且Steam窗口已存在时，只执行一组Windows activate+focus；明确未启用虚拟输出则随Steam窗口执行。正常就绪不增加固定等待；始终未确认则启动第55秒保留同一组有效命令兜底。主路径与兜底合计最多1组，执行后不重抢。原Windows命令函数原样保留，不改输入Host/驱动/native。

focus专用就绪判断要求当前会话候选PID/出生身份、5秒内accepted frame、Host实例/run/epoch/power与已知started一致、来源live；只started+neutral不能算完成。同名辅助进程不再一律阻断，恰好1个新鲜回执拥有者才准入；多个真正拥有者仍unknown。诊断默认旧合同保留。使用现有saved-settings无虚拟分支，并非新native实时接口，临时设置覆盖和同Host目标代次边界仍需独立核实。

仍复用核心`Run.ps1 -Optimized -Mode run -Tests steam.boot-focus -Rerun`，4个原消费者共143断言和4反例本次实际通过，核心约4.2秒；无全局整轮/实机。反例检出反复交接、忽略虚拟就绪、删除有效兜底与伪造焦点成功。证据Testing/YMCC/Evidence/SteamBootFocusHold-20261024/field-202002/single-handoff。

下方每3秒重复/最多20组为已被用户替代的历史记录。
<!-- /SINGLE-INPUT-HANDOFF-20261024 -->

<!-- WINDOWS-STEAM-FOCUS-20261024 -->
2026-10-24 用户替代旧“三轮/永久让步”规则：启动60秒内唯一目标为现有Steam大屏；每秒观察、命令间隔至少3秒、最多20次。Steam已在前台仍执行 Windows `nircmd win activate handle <HWND>` 和 `win focus handle <HWND>`，不使用Steam URI重新打开界面。GetGUIThreadInfo独立读取活动窗口与键盘焦点；命令退出0/顶层前台均不能伪装成手柄消费确认。既有Xbox清理、立即启动Steam、CSS归YMCC和后续只读尾部保持。

定向入口（一个canonical ID，复用本目录原实现，不增加父门）：

```powershell
G:/YeManCC-Work/Testing/YMCC/Run.ps1 -Optimized -Mode plan -Tests steam.boot-focus
G:/YeManCC-Work/Testing/YMCC/Run.ps1 -Optimized -Mode run -Tests steam.boot-focus -Rerun
```

原4个消费者 `selftest/flow/focus/cleanup` 与4个对焦退化控制；不运行native清理、包装器或全模块验收。90秒外层预算，原子脚本45秒；实际用时写回执，当前约4秒，不将父子耗时相加。闭包为BAT、run.mjs及以上4个脚本，工具为Node与系统Windows PowerShell（consumer回执记录真实PS SHA256）；legacy动态闭包未认证，因此不缓存/不将跳过算通过。候选参数沿用原`--candidate`。来源/工具身份变化后只选此ID执行。注册在global/adapters.mjs；本轮源身份与实际回执见Testing/YMCC/Evidence/SteamBootFocusHold-20261024。

以下为历史全BAT回归说明，其旧“前台即成功/三轮/YMCC永久让步”已被本次用户要求替代；旧回执与本轮before快照保留，不作为当前成功证明。启动链测试按当前立即启动Steam/60秒观察更新，其他原生/包装器消费者未由本轮执行。
<!-- /WINDOWS-STEAM-FOCUS-20261024 -->

# Steam高级启动BAT正式消费者回归

输入是PowerControl/YeManSteam.bat内嵌PowerShell及C#，通过AST/具名方法抽取执行实际消费者，替换进程/窗口/COM/时钟等末端；不运行BAT真实入口。只有包装路径和自有超时助手使用惰性载荷，绝不启停实际YMCC/Steam/Xbox/CSS或操作输入设备/系统设置。

```powershell
node tools/ymcc_sim/global/steam-boot-bat/run.mjs --out G:/YeManCC-Work/Testing/YMCC/Evidence/XboxBootCleanup-20261023/recheck
```

可用--candidate明确选择候选。8套回归：32项原YMCC权限/让步、32项启动流及CSS归属、25项目标身份/前台、4项Steam原请求、38项新清理/等待/只读尾部、25项正式C#身份门/完整COM ABI/编码助手、13项正式原生终止消费者，以及两条惰性隐藏包装路径/实际自有超时助手退出。总169项断言与10个指定退化控制。读取receipt时注意Windows PowerShell5.1 Tee-Object文件可能是UTF16LE。

保留旧/IM效果合同与原回执在Testing/YMCC/Evidence/XboxBootCleanup-20261023/fixture-before、initial-regression；新PID/出生/包级效果转移到cleanup/native消费者，没有用删除失败掩盖不兼容。原field-record.json仍是2026-10-22召回故障，新增field-record-20261023.json指向ROGX重复重生与输入延迟诊断，不覆盖历史。

分类：清理/对焦为本次60秒有界启动操作，之后仅6次/60秒只读诊断；无常驻订阅或无限睡眠恢复。自有COM助手上限3秒、终止后500ms确认；用户应用不由BAT负责退出销毁。保留显式OFF、现有YMCC实例和系统FSE模式；包级必须核实当前用户/会话/实际GamingApp身份，父进程只用于来源记录。对超时、权限失败或持续重生保留未知/残留状态。

这些回归不构成真实手柄/FSE/整个YMCC持续生命周期验收，未注册为全局已通过能力；正式夹具和历史失败按项目Testing/YMCC统一层维护，完整主程序/Decky等已有未通过项独立保留。

2026-10-23 CSS 启动归属：BAT 不再探测、创建或管理 CSS；由 YMCC 统一处理现有实例复用、缺失启动和外观确认。新增退化控制拒绝 BAT 重新引入 CSS。CSS 权限由原生专用启动器继承 YMCC 当前令牌，与 Decky 降权策略独立；原生14项真实惰性子进程、15项权限末端及2项运行时路由检查另行记录，不计入BAT169项。
