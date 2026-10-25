# Steam 联合事件监控与 CSSLoader 缺失补启动（2026-10-11）

## 用户目标与边界

Steam 大屏“三点键呼出”的 YMCC 插件与 CSSLoader 使用同一套 Steam 生命周期事件。Steam 启动一段时间后，如果美化后台仍未运行，再专门补启动 CSSLoader；不新增高频进程轮询，不修改原 BAT，不重置已确认的 7 项主题参数。

## 已有代码与接入

源码已有一个共享的 Steam Observer，使用注册表变更通知、经过路径核对的窗口创建/显示通知，以及 Steam 进程退出等待句柄。三点键插件已经消费这套事件。

本次给该 Observer 增加线程安全的 sessionSnapshot，包含 Steam 安装路径、PID 和进程创建时间。CSS guard 从同一个回调消费快照；没有另起一个 Steam 进程扫描器。账号变更不会给 CSS 重新补满启动预算，PID 复用但创建时间不同则视作新进程。

CSS 的稍后工作在独立等待线程执行，避免堵住共享 Steam Observer、插件绑定或 UI 线程。

## 行为

1. 仅当已有“Steam 美化联动”链接文件启用，并有匹配当前用户/Windows 会话的真实 Steam 进程时允许补启动。
2. 初次 Steam 会话进入后先等 10 秒，留给原 BAT 联动。已有后台就复用其只读进程等待句柄，不创建另一个。
3. 未运行且条件仍允许时，仅启动链接文件指定的 CssLoader-Standalone-Headless.exe。程序隐藏启动，使用现有已验证的非提权启动通道。
4. 失败情况下，每个 Steam 会话最多检查 3 次，正常短耗时情况下约为启动后的 10 / 25 / 60 秒；之后停在事件等待，不永久重试。休眠或慢 IO 不会补发一串积压检查。
5. 已运行后台退出通过句柄事件发现，延迟使用该会话剩余的检查预算；不会每次崩溃补满预算造成无限拉起。
6. 删除美化联动链接文件立即通过目录通知取消待办；再次开启可开启一个新的有限窗口。其它插件链接文件变化不重置 CSS 预算。
7. Steam 退出、YMCC 开始退出或进入隔离模式时，不允许晚到的检查启动后台或发布过期的健康状态。
8. Steam 退出或用户关闭联动只关闭 guard 的等待句柄，不强杀已存在的 CSSLoader，也不改变三点键插件的独立开关。

## 单实例与安全

- 与现有 PowerShell helper 复用 Local\YMCC.CssLoader.Launch 互斥，不另造冲突的启动锁。
- 同时核查 IPv4 / IPv6 的 35821 监听者；其它程序/其它用户的占用会阻止启动，不会终止占用者。
- 表枚举或所有者核对失败不会被当作“后台不存在/端口空闲”。
- 当前用户已安装在其它路径的同名真实 CSSLoader 后台可以复用，不覆盖其配置。
- PyInstaller 父/子进程不被误判为需要再启动一个实例。
- 原 BAT 不用改，也不要求它配合互斥；在本次创建的子进程尚未恢复执行时再检查一次其它启动者，降低竞态窗口。原 BAT 或其它第三方启动者不合作时，双重检查不是跨进程绝对原子保证。
- 唯一可终止的对象是此次操作自行创建、仍未恢复执行的子进程；已有 CSSLoader、Steam、其它用户的进程不是终止目标。
- 链接文件只接受有界 UTF-8 本地绝对路径，程序文件名必须为 CssLoader-Standalone-Headless.exe，不把额外参数、换行、嵌入 NUL 或任意命令传给 shell。

## CPU 与可观测性

- 空闲用 WaitForMultipleObjects 等待停止、会话变化、链接目录通知和后台退出，没有 setInterval、WMI 周期扫描或 Sleep 常驻循环。
- Steam 状态仍共用原有事件链；进程/端口枚举只在有限的延迟检查内进行。
- 健康/预算耗尽后的隔离短时采样均记录 0 ms CPU 时间；这只是采样结果，不代表任何机器任何时间绝对不占 CPU。
- 新增按需 IPC steamAppearance.guardStatus，返回 linked、steamRunning、phase、reason、pid、attempts、started。没有新增前端轮询。
- 已存在进程但 API/界面尚未就绪不会被强制重启；这种情况保持手动“重新应用”路线，而不是自动清理或重注入样式。

## 验证

- CSS guard：62 项隔离测试（预算、CPU、联动/Steam 取消、进程退出、目录晚创建/删除重建、坏链接、在途完成/退出边界、句柄回收）。
- 共享 Steam Observer：45 项 Win32 隔离测试，包括 PID 注册表为 0 时靠窗口事件发现启动，及创建时间快照共享/清理。
- 源码接线/安全边界/原 BAT/主题资源：23 项检查。
- 合计 130 项通过；真实后台仅做进程/端口只读探测，未为了测试启动/停止真实 CSSLoader，未重启 Steam。
- 用户原 7 个主题及 61 项主题资源/配置摘要仍完全一致，原源码/本机 YeManSteam2.bat 摘要仍为 D788CE2906E67A4E4CCFE3538A1FF51A1BEDECF568C4D979F51CC2D2954E2479。
- 原生壳在独立工作目录编译、链接并嵌入清单；不覆盖其它任务的 Build 输出。

## 文件与当前生效状态

实现：
G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\steam_cssloader_guard.h
G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\steam_session_observer.h
G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\main.cpp

测试：
G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\steam_cssloader_guard_selftest.cpp
G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\steam_cssloader_guard_selftest.ps1
G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\steam_cssloader_guard_source_selftest.mjs
G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\steam_session_observer_selftest.cpp

验证输出：
G:\YeManCC-Work\_scratch\steam-css-guard-20261011

原生候选主程序：
G:\YeManCC-Work\_scratch\steam-css-guard-20261011\native-build\YeManCC.exe

这轮未替换 C:\SOFT\YeMan\YeManCC\YeManCC.exe，也没有让候选程序接管正在使用的 Steam。因此“源码/候选构建完成”不等于当前旧进程已经具备 guard；随新版 YMCC 主程序更新并启动后才生效。候选构建来自当前共享源码树，不能当作已完成所有其它功能验收的完整发布包。
## 共享工作区构建快照说明

最终候选编译完成后，共享 native\main.cpp 又新增了其它任务的桌面 HUD cache-pulse 代码。该后续改动已原样保留，未覆盖或回滚。候选二进制对应本交付 source\native\main.cpp 的构建快照，SHA-256 为 f3861bf07044aaed44f18edb2d243c33a958f515ac56a1cebe74f90bf3fff614；CSS guard 自身及其共享 Observer 接入仍在最新源码里。候选不是包含该后续 HUD 改动的最终整包。
