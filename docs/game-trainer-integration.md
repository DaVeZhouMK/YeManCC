# YMCC 修改器集成与兼容维护

核验日期：2026-10-14。修改器仍由 Game Cheats Manager（GCM）统一管理，YMCC 负责游戏识别、有限额的后台准备和玩家点击后的启动。

## 已核验的上游契约

GCM v2.5.4，Git commit `d5f4e81ed89deb281f44952e11f3b995aa756006`，release published `2026-10-11T19:03:10Z`。研究副本保存在 `Mainline/Build/Validation/TrainerReliability-20261014/research`。

- `src/scripts/threads/other_threads.py` 的 FetchFlingData/FetchTrainerTranslations 通过签名资源接口获取官方 JSON；公共源码缺少 `secret_config` / CLIENT_API_KEY 配置。不能据公开仓库宣称 YMCC 可独立更新全部官方数据库，也不提取打包密钥。
- `src/scripts/threads/download_base_thread.py` 使用条件请求、有限重试和原子写入。YMCC 借鉴这些机制，但以公开风灵目录作为独立的后台数据源。
- `src/scripts/main.py` 的本地发现读取 `gcm_info.json.extension`，可为具体嵌套文件名、扩展名或 `none`。YMCC 尊重显式选择；目录型条目留给 GCM，不猜测其中的 EXE。
- `src/scripts/main.py` / `config.py` 启动 CT/CETRAINER 时使用配置的 `cePath/Cheat Engine.exe`，采用 `launchCTAsAdmin`；普通 EXE 使用 `launchAsAdmin`、正确工作目录及 `safePath`。YMCC 在单独的玩家启动入口对齐这些设置。
- GCM 是单实例应用。再次点击管理器时，YMCC 优先恢复并激活同路径的已有进程；无法激活则明确报告，不把第二次 EXE 退出当作打开成功。

参考源码：
- https://github.com/dyang886/Game-Cheats-Manager/tree/d5f4e81ed89deb281f44952e11f3b995aa756006
- CheatDeck `2921aaff9c46cc287e5d46210eaaee7dd906d932`：以 Steam AppID 关联游戏和启动配置。Windows 方案借鉴稳定身份，不改变 Steam 启动参数或移植 Proton 注入。
- PPSSPP `66dc8972e88b756c83d6b269da8054d6a08f9ee6`：Android 版本也按 DISC_ID 将 cheat.db 对应到游戏。YMCC 不把模拟器进程名当成正在模拟的具体游戏。
- GCM WinUI `42039422ca1c0676047dea47cb920c4fc2f2d453`：同样将本地库操作与目录/签名服务隔离；结构化启动错误有助于避免假成功。

## 后台数据与下载额度

`settings.json.downloadPath` 为唯一修改器库位置。库内：

| 文件 | 内容 |
| --- | --- |
| `ymcc-library.json` | 修改器库存、GCM 资源 ID、游戏 EXE 指纹、Steam AppID 依据和下载预算 |
| `ymcc-fling-public.json` | 从风灵公开 A–Z 页面解析的可用目录 |
| `ymcc-data-state.json` | 目录刷新 ETag、时间、错误、尝试次数及流量保留额 |

修改器下载：滚动 24 小时最多 3 次、总计 200 MiB、单文件最多 80 MiB；失败计次，手动重试和重启不清零。已有文件复用不消耗下载额度。

元数据刷新：启动后延迟 30 秒，在自动下载开关启用时静默执行；成功后每日检查，失败冷却 6 小时；独立的滚动 24 小时最多 3 次、总计 8 MiB、单次最大 2 MiB。任务前持久化保留额度，记录损坏时不重置配额。关闭自动下载会取消自动准备和目录更新。玩家准备任务优先于尚未开始的目录刷新。

GCM 官方 JSON 每次使用时重新读取并校验，更新后自动生效；验证快照位于 `GCM Settings/db/ymcc-validated`。字段增加可继续使用；半写入、损坏或不兼容格式则回退到上次有效数据。YMCC 不覆盖官方数据库。Windows PowerShell 5.1 的 JSON 数组包装必须在保存快照前展开，避免单条数组序列化成 `{value,Count}`。

## 游戏身份和启动边界

按游戏 EXE 所在 Steam 库清单的 `appid`、文件名和精确 `installdir` 获取身份，然后关联 GCM `steam_id`。没有 AppID 时采用清单标题、已验证 EXE 绑定或准确标题。同一 AppID 对应多个名称时，只有已核实的清单/自定义库正式名称与一条 GCM 英文名称规范化后精确相等，才采用该行别名；无法唯一消歧仍拒绝自动选择。不同版本的别名不合并，标题规范化保留续作数字。前端任务、取消、就绪状态和重试按名称加完整 EXE 路径区分。

后台准备不会启动修改器。玩家点击时重新准备/校验本地条目和 SHA-256，复查目标游戏仍存在，再通过隐藏的 PowerShell 启动助手执行。助手读取当前 GCM 设置，返回进程启动/窗口恢复回执与系统错误。回执证明 Windows 接受了启动或恢复操作，不保证某一游戏版本的修改功能兼容。

`safePath` 优先使用 NTFS 短路径；不可用时使用临时目录联接，原文件继续由 GCM 管理。不会删除或覆盖已有不匹配联接。若文件名自身非 ASCII 且磁盘无短名称，仍可能需要由 GCM/作者解决旧修改器限制。

## 回归与实测

- `tools/game_trainer_backend_selftest.ps1`：下载、ZIP、预算、迁移、库存等 27 项。
- `tools/game_trainer_reliability_selftest.ps1`：官方快照、ETag、持久预算、AppID、元数据入口、CT/EXE 启动计划和管理器单实例。
- `tools/game_trainer_background_selftest.mjs`：自动准备、同名不同路径、取消、目录刷新和启动错误。
- `tools/top_menu_power_group_browser_selftest.mjs`：四按钮边框、无管理说明副标题、生产 B 事件、点击取消及掌机尺寸。
- `tools/global_cache_config_selftest.mjs`、`tools/optiscaler_warning_ui_browser_selftest.mjs`、隔离构建的 vue-tsc 和 Vite 构建。

2026-10-14 真实联网、隔离库刷新获得 758 条公开风灵目录，同时读取本机官方 GCM 760 条主目录、465 条归档、3516 条翻译、798 条 GCM 修改器记录。此证据只支持公开目录可以纯后台刷新，不表示私有签名数据库已完成联网更新。

发布仅覆盖前端资产和修改器 PowerControl 文件，并同时部署默认目录与当前 `YEMAN_POWER_CONTROL_DIR` 覆盖目录；不覆盖原生 EXE 或修改用户设置。启动测试使用计划和进程替身，不运行真实修改器、不重启游戏或 Steam。

### 自定义游戏库适配（2026-10-14）

`TrainerCustomLibrary.ps1` 读取 `D:\YeMan\CustomSteamLibrary\data` 中的扫描、导入计划和手动覆盖记录。只有扫描中准确的主 EXE、可识别的已验证 schema/resolver、相同来源根和唯一且一致的商店 ID 才用于自动匹配。只读使用 `steamStoreAppId` / `storefrontAppId`，不使用 `shortAppId` / `longAppId`；清空的 ID、手动冲突、非主程序、扩展包身份及多个候选不自动选择。记录的 EXE 大小变化时放弃该库证据。

对于只有旧快捷方式名称的游戏，额外读取已确认游戏目录内有限的 launcher-settings.json 路径；只有其 exePath 准确指向当前主程序、官方商店 URL 中的 ID 与 EXE 相邻的 steam_appid.txt 一致时，采用 `custom-library-install-metadata` 身份。孤立 steam_appid.txt 不足以确认。缺失 EXE 直接停止准备，3DMark、Lossless Scaling 与 Steam 公共运行库不作为修改器目标。

本机 36 条真实记录模拟：资源处理结果一致性从 31/36 提升至 36/36；4 款复用本地、6 款目录匹配、22 条暂无匹配、4 条阻止准备。保持区分“一致”和“存在可用修改器”。官网 4 个页面成功解析标题与附件，测试未下载附件。验证脚本为 `tools/game_trainer_custom_library_selftest.ps1` 与 `tools/game_trainer_library_simulation.ps1`，证据目录 `Mainline/Build/Validation/TrainerLibraryConsistency-20261014`。

### 已发现状态与只启动接口（2026-10-14）

主菜单在当前游戏名称与完整 EXE 路径对应的准备结果为 ok/ready、修改器路径合法且有 SHA-256 时显示“已发现修改器”。目录命中或下载中不显示此文案。准备回执先验证本地路径存在，再通知界面为 ready，避免短暂误报。

`getReadyGameTrainer(gameName, processPath)` 是同步只读内存查询；返回 `InstalledGameTrainer | null`，不读取旧的仅按名称索引、不创建准备任务、不联网。用 `subscribeGameTrainerState` 与当前游戏订阅刷新界面。该状态是准备时已核验的快照；用户从外部删除或改变文件，由点击时的再次核验发现。

`launchReadyGameTrainer(gameName, processPath, { isTargetCurrent, isCancelled? })` 仅启动该已就绪条目，返回 `{ action: 'opened', trainer }`。调用方必须在 `isTargetCurrent` 内核验锁定游戏的 PID、创建时间与完整路径。入口在操作开始、创建隐藏启动进程前核验目标和就绪状态，并检查游戏 EXE、修改器存在及 SHA-256；相同目标重复点击共享任务。不存在就绪条目时抛错，不转入搜索、下载或 Cheat Engine 回退。GCM 设置、CT 打开方式与启动回执仍由原启动助手负责。

热门样本模拟：从 Steam most-played 前100名单中挑选39款，逐项核对官方商店 Single-player 分类、已发行、游戏类型且非 MMO。榜单汇总日期为2026-10-12，抓取/测试日期为2026-10-14；这是目的性样本，不是单机销量榜。154条模拟输入覆盖Steam正式名、通用EXE、自定义库简称和可用中文名：39/39身份及入口结果一致，25款匹配当前风灵目录，14款没有精确目录匹配，其中2款已有本地修改器。该测试未下载或运行修改器，不证明具体游戏版本的修改功能兼容。

新增 `tools/game_trainer_steam_popular_simulation.ps1`，样本、官方响应、分项结果、截图和部署回执见 `Mainline/Build/Validation/TrainerSteamPopular-20261014`。33项自定义库测试、21项可靠性测试、27项后台测试、27项前端测试和16项浏览器检查通过；原36条真实库快照结果仍一致。


### 下载后身份与已有文件重连（2026-10-14，离线）

新增 `tools/game_trainer_lifecycle_simulation.ps1` 复用保存的39款Steam热门单人样本。25款有风灵目录资源的样本模拟下载并保存游戏EXE、SHA256、已验证游戏身份和资源记录；新的前端缓存可从GCM库恢复，无二次下载。已有文件复用的回执 `downloaded=false`，仅本次实际进入下载安装分支才为true，避免把已就绪误报成新下载。

`Find-LocalTrainer` 的可选第三参数为GCM数据库路径：若本地gcm_info的gcm_url能唯一映射目录中的资源名称，优先使用该资源身份，因此改名或缺少显示名仍可重连。已知ID属于其他游戏或同ID对应多个游戏时不回退到误导名称。没有gcm_info的裸文件/同名文件夹不自动视为匹配；当前游戏也必须有自己的独立身份依据才能进入只读已确认入口。选中的资源只按解析后的名称登记一次，避免改名产生同文件重复库存。

`tools/game_trainer_lifecycle_frontend_selftest.mjs` 用生产后端生成的真实结构回执驱动生产前端模块，仅替换文件、进程与IPC：153条回执流中114条确认，39条缺少游戏身份的标题匹配拒绝只启动；覆盖目标变化、不同EXE、重复点击和文件移除。主菜单对未确认的本地回执显示“身份待确认 · 打开管理器核对”，不显示已就绪提示。

模拟结果：384项生命周期检查、140项既有及扩展回归、类型检查通过；不代表39款都真实存在修改器，也不验证修改功能兼容。25次下载为128字节不可运行的PE形状夹具，仅隔离台账计数；真实网络、真实下载、真实启动均为0。证据 `Mainline/Build/Validation/TrainerLifecycle-20261014`，本轮未部署。


### 正反向词汇与冲突联想（2026-10-15，离线）

`Get-TrainerNameKey` 与前端 `trainerGameKey` 将有语义的 `&` 规范为 `and`，不再直接删掉：保存的3516条GCM翻译快照中，“Dragon Quest I & II HD-2D Remake”和“DRAGON QUEST III HD-2D Remake”原来会合并为一个键，本轮区分它们，保留HTML实体/全半角/大小写兼容。

别名解析分两步：先验证Steam清单/自定义库精确EXE身份，再对对应行提取安全别名。多个不同Steam ID或不同正式名称共享的输入别名不自动挑选；即使从唯一英文名或已验证ID出发，也剔除会跨到其他游戏/版本的别名。相同ID有多个英文版本名时仍以可信正式名唯一消歧，不能通过共享中文别名重新引入另一版本。

反向资源匹配要求非空gcm_url在当前GCM目录中有唯一名称归属，未知或歧义ID不靠显示名回退。同优先级多个本地修改器同时存在时明确拒绝任选版本；不同优先级仍优先风灵。冲突发生在下载预算保留和网络调用之前，Force不会绕过。

`tools/game_trainer_association_selftest.ps1` 使用2026-10-14的39款保存样本：928项主矩阵、404项补充检查全部通过（两组共1332次执行，1314个不同测试名）。覆盖中英文、全半角、HTML实体、Trainer/修改器后缀、简称、续作/重制版/DLC/原声带/Demo、资源ID反查及正反向往返一致性。17个常用简称在有可信EXE时正确关联，单独简称不制造已确认身份。“荒野大镖客2”的中文名在快照里对应两个英文名称，没有EXE/正式名上下文时按歧义处理。

此前下载/本地重连的384项生命周期与153条前端回执消费流仍通过；后台32、可靠性21、自定义库33、前端39项检查以及vue-tsc通过。仅离线测试和源码修正，不部署、不联网、不下载或运行真实修改器。证据：`Mainline/Build/Validation/TrainerAssociation-20261015`。


### 顶部完整游戏名与GCM公开CT接通（2026-10-15）

修正“顶部已识别Teenage Mutant Ninja Turtles: Shredder's Revenge，但修改器未找到”的两处链路缺口：自动任务/手动菜单传递同一顶部目标的PID与processCreated；非Steam路径没有安装清单时，后台只读观察指定PID，核对进程创建FILETIME、精确镜像路径、窗口完整标题，并与GCM翻译唯一商店ID对应，计算EXE SHA256及前后进程快照。成功生成`native-window-title`证据；仅标题或进程简称仍不进入只读ready。下载完成前再次核对，点击只启动入口复核EXE与CT哈希，玩家的当前目标验证继续保留。

风灵独立资源保持第一优先；其目录无命中时，可使用GCM已启用的gcm_trainers.json中唯一、精确名称的ct_other记录，要求GCM资源ID、作者和Fearless原作者主题页明确一致。仅解析原作者首帖唯一CT附件，剔除会话sid，不选回复附件；网络绑定原主题页和该附件，跳转不能扩大地址。使用GCM原作者公开资源，不访问GCM私有签名密钥。多个版本/作者不自动任选，GCM关闭时不启用此补充。

CT只读解析XML，禁止DTD/外部实体，最大8MiB，沿用原3次/滚动24小时与累计200MiB预算；失败/取消计次，Force不绕过。安装到GCM的downloadPath，保留作者目录、ct_other来源和原gcm_url，复用仍优先风灵；启动继续交给GCM设置的Cheat Engine，自动准备不会运行CT。

真实网络测试已下载GreenHouse的TMNT.CT，45624字节，SHA256 f516263ddc4c387bf2ca8032da6d173f151696e2962d2d668d4e30eee4a8f8eb，写入真实GCM库；安装后后台重复查找未再次下载。仅准备和启动计划核验，没有运行表格。开发机未运行用户日志里的TMNT，原生标题确认与界面接线按明确标注的进程替身及日志元数据测试，不能代替微星现场验收。最近游戏任务结果保留在trainer-cache/last-game-status.json；专用修复包提供只读诊断收集器。本机修复已部署，微星现场尚待新回执。

回归：tools/game_trainer_tmnt_selftest.ps1（17）、tools/game_trainer_tmnt_browser_selftest.mjs（5）、后台32/可靠性21/自定义库33/前端43/Decky43/菜单17项通过。隔离前端构建基于已安装前端源哈希，排除其他任务未完成变更；默认与覆盖PowerControl同时更新，不替换原生EXE或用户设置。证据：Mainline/Build/Validation/TrainerTmntConnect-20261015。


## 2026-10-15 同一路径的进程重开边界

原生完整窗口标题身份的只读就绪查询支持可选的当前PID/创建时间；顶部菜单和Decky传入正在展示的同一实例，旧实例不能让新实例显示“已发现修改器”。同一路径的新实例会取消旧准备任务，晚到回执不能覆盖新任务。自动冷却按实例隔离且内存最多128条；后台仍先复用GCM文件，重新识别不会绕过下载预算。

玩家启动将同一个PID、创建时间及原生身份的游戏EXE哈希传入PowerShell助手，在OS启动前再次核验。只核对进程的启动检查允许无窗口标题；标题确认身份仍要求完整窗口标题。保留一个有界last-launch-status.json用于IPC回执清理后的诊断，它表示OS接受/拒绝启动，不代表CT功能兼容。tools/game_trainer_instance_selftest.ps1使用隔离目录的真实惰性Win32进程验证退出/重开、哈希错误及生产启动入口；不执行实际游戏或CT。


## 2026-10-15 顶部与修改器统一识别来源

修改器现在只消费native已经选定的game-target.json（与顶部相同的PID、创建时间、完整EXE），不重新选择游戏、不扫描候选、不依赖独立自定义库EXE。顶部detectedGameName与脚本名称整理共用PowerControl/GameTitlePolicy.json；PowerControl/GameTitlePolicy.ps1只是该规则表的运行时适配。窗口原始标题保留作诊断，版权后缀不再导致两边分歧。普通标题中的by（Dead by Daylight）不会删除。

实时请求必须读取有效且10秒内的共享快照，并核对指定实例；无快照/撤销/过期不再回退到窗口识别器。新回执method=ymcc-game-target，带snapshotPath、gameName、PID/birth/path和EXE哈希。GCM按既有精确名称及商店ID映射资源；稳定Steam清单/自定义库的已验证ID仍可作为元数据，不启动独立库或新增识别流程。旧native-window-title回执只保留读取兼容，本节替代其后台独立标题确认路径。

专项入口tools/game_trainer_window_title_selftest.ps1与tools/game_title_policy_selftest.mjs，真实2077 PID35788/FILETIME134365342855297510的修复前失败与共享身份回执见Build/Validation/TrainerCyberpunkTitleFix-20261015。真实GCM风灵文件已复用（downloaded=false）；游戏内功能只在实际玩家启动后验收。


## 2026-10-15 识别后的下载/复用边界

ready回执保留downloaded兼容字段，新增downloadOutcome=downloaded或reused-local。downloaded=false表示本次未新增下载；它不等于失败、冷却或版本更新确认。当前2077本地风灵文件与已保存GCM目录均记录版本2024.09.13，准备阶段优先复用，不据文件存在宣称最新或功能兼容。

本地EXE至少校验MZ/PE结构，CT至少校验有界安全XML结构；CETRAINER仅检查非空并交由GCM/CE解释，不伪造其兼容性判断。格式损坏条目不再ready；允许在预算内另存新资源，保留原文件由GCM统一管理。读取失败/占用则明确失败，避免把临时不可读当成缺失而重复下载。选定文件在标ready前再次校验。

持久预算只能降低现有限额，不能超过3次/滚动24小时、200MiB。空/null既有台账和负数计费都停止下载并保留原记录；首次不存在的台账仍按默认额度创建。有效本地文件在额度耗尽和旧网络失败冷却下仍可复用；删除文件后不会只信旧ready索引。自动网络失败重试仍至少1小时；手动重试可越过冷却，不能越过额度。

专项证据Build/Validation/TrainerDownloadReadiness-20261015；后台43项含上述反例，HTTP使用测试替身。实际2077复用另由安装后后台回执确认；没有为制造downloaded=true而删除用户文件或发起新真实下载。

### 2026-10-22 Decky 外部修改器交互启动试用

正式消费入口仍为 TrainerLaunch.ps1；同目录 TrainerForeground.ps1 仅承接一次交互式 Shell 启动。新前端只在 Decky ready-only 调用显式传 InteractiveShell；原菜单保持默认 Process.Start。已安装旧前端在已核验的同会话 Steam 前台，或精确匹配的 YMCC 父进程主窗口已隐藏/最小化时，使用原 AppDirectory 参数兼容选择该分支，无须改官方 Loader、添加 EXE、注册 URI 或重开 Steam。

独立修改器仍由原 GCM 当前库、文件 SHA、当前游戏 PID/创建时间校验。交互路径通过桌面视图的 Application 执行，保留参数、工作目录和 open/runas 设置；已运行实例仍走原恢复入口。只有获取 Shell 对象失败且尚未派发，才可使用原启动器；派发异常、PID 未确认或窗口未到前台均不触发第二次启动。前台不确定时返回 launched=true、ok=false、trainer-window-unconfirmed 及真实 PID，保留可读原因。

现有全局场景 game-tools.trainer-foreground 和 Decky 更新发布门直接执行同一消费函数，覆盖11项及吞掉不确定性后回退启动的具名失败反例。桌面模式独立 EXE 创建/原字节和无重复实例已实测；前台存在失败回执，Xbox FSE 实际拉起及用户真实修改器仍待用户测试，不能把过程回执或较早的单次前台成功写成该环境已修复。
