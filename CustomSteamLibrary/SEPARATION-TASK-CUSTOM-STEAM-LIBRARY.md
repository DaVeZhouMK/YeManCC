# Steam 自定义游戏库（Custom Steam Library）分离任务

状态（2026-08-25）：阶段 0「检查与冻结」、阶段 2A「数据目录规范化与旧路径迁移」、阶段 2B「配置双重备份、恢复与真实抓取」已完成；阶段 3 的 YeManCC 一级菜单、protocol 1 启动/关闭桥、主程序唯一手柄转发、三目录 `green-child` 升级适配和 CustomSteamLibrary 独立隐藏启动健康握手已完成源码与临时包回归。默认安装目录真实升级、实体手柄窗口验收仍未完成。

> 当前切换点（2026-08-25）：运行时接入和 green-child 自动更新均已进入源码启用、分步验收。独立程序仍可单独启动；主程序固定使用 `C:\SOFT\YeMan\CustomSteamLibrary\CustomSteamLibrary.exe`，数据规则不改变。升级完成必须同时通过 Custom 子程序健康握手和 YeManCC 主程序握手；正式目录升级另行执行。

### 当前状态矩阵

| 项目 | 当前状态 |
| --- | --- |
| 三目录升级包与清单驱动覆盖 | 已完成 |
| Custom 程序文件回滚、未知文件和用户数据保留 | 已完成 |
| CustomSteamLibrary 独立启动健康握手 | 已完成，已做真实临时包成功/失败探测 |
| YeManCC 主程序启动握手 | 已完成 |
| 正式 `C:\SOFT\YeMan` 目录升级 | 尚未执行 |
| 实体手柄窗口级验收 | 尚未完成 |

本文档是后续拆分的主任务单。每次只推进一个阶段，先编译和验证，再继续下一阶段；不在同一轮里同时移动数据、改启动链、改主程序和打包。

## 1. 产品定义

- 中文名：`Steam自定义游戏库`
- 英文名：`Custom Steam Library`
- canonical 程序根目录：`C:\SOFT\YeMan\CustomSteamLibrary`
- 有 D 盘时的数据根目录：`D:\YeMan\CustomSteamLibrary\data`
- 无 D 盘时的数据根目录：程序根目录下的 `data`；canonical 安装时即为 `C:\SOFT\YeMan\CustomSteamLibrary\data`
- 绿色软件规则：安装到任意目录都能运行；程序通过自身 EXE 所在目录识别资源、Worker、缓存和默认数据位置。`C:\SOFT\YeMan\CustomSteamLibrary` 是默认安装位置，不作为不可移动的运行时硬编码。
- 本阶段已统一原生窗口标题、原生自定义标题栏和网页标题；独立发行入口已确定为 `CustomSteamLibrary.exe`，已嵌入多尺寸图标和 PE 产品信息。

## 2. 当前检查结论

### 2.1 当前是否已经是独立 EXE

是，但目前是「独立宿主 + Worker」的组合，不是可直接复制到任意位置的完整绿色发行包：

```text
CustomSteamLibrary.exe      原生窗口宿主、WebView2、键盘/手柄输入、目录和任务协调（开发构建名为 SteamLibraryWorkspace.exe）
SteamArtworkLab.exe        刮削/扫描/识别 Worker
workspace-ui\              WebView2 加载的界面资源
```

`SteamArtworkLab.exe` 是后台命令行 Worker，不是用户入口；直接打开没有窗口或只显示命令帮助属于正常行为。独立发行包的用户入口是 `CustomSteamLibrary.exe`，启动脚本 `run-workspace.bat` 也只负责启动宿主。

当前构建输出仍位于 `SteamArtworkLab\build\`。开发布局下宿主通过 `build` 的父目录识别项目根；发行布局可把 EXE 直接放在 `CustomSteamLibrary` 根目录。两种布局都能查找 Worker 和 `workspace-ui\`。

### 2.2 当前数据与运行时依赖

当前数据根目录优先级为：

1. 环境变量 `YEMAN_STEAM_BIG_PICTURE_DATA_ROOT`；
2. 如果存在 `D:\` 且 `D:\YeMan\CustomSteamLibrary\data` 存在，或旧迁移源存在，使用 `D:\YeMan\CustomSteamLibrary\data`；
3. 如果整个 D 盘数据包丢失但程序根目录已有 `data`，回退到程序根目录下的 `data`；
4. 否则在有 D 盘时使用 D 盘目标目录，在无 D 盘时使用程序根目录下的 `data`。

显式设置 `YEMAN_STEAM_BIG_PICTURE_DATA_ROOT` 时，该目录是隔离边界，不会自动混入开发目录或旧迁移源；默认安装路径才执行旧数据的复制迁移。

旧路径 `D:\YeMan\Steam大屏` 不再作为新默认目录，只作为迁移源。新的持久化树为：

```text
CustomSteamLibrary\
  CustomSteamLibrary.exe
  SteamArtworkLab.exe
  workspace-ui\
  data\
    config\              library-config、manual-overrides 等设置
    state\               library-scan、Steam 计划、迁移记录
    cache\
      jobs\              识别任务、候选和临时输出
      identity\          名称/AppID 缓存
      artwork-search\    图片搜索缓存
      webview\           WebView2 用户数据
    artwork\
      overrides\         手动指定的封面/壁纸
      <game-data-id>\     自动下载并持久化的素材
    games\                单独 EXE/游戏元数据
    backups\              设置、Steam 文件和迁移备份
```

本轮已经把新写入的任务输出从程序根目录 `output` 移到 `data\cache\jobs`，把顶层 `artwork-overrides` 规范为 `data\artwork\overrides`。旧源码 `output` 仍只作为兼容读取源，不再作为新输出位置。

### 2.3 主程序联动检查

已检查 `YeManCC3\src` 和 `YeManCC3\native`：没有发现对 `SteamLibraryWorkspace.exe` 或 `SteamArtworkLab.exe` 的专用入口、参数协议或返回协议。主程序已有通用 `shell.execute` 能力，但“能启动 EXE”不等于已完成导入联动。

后续需要明确：

- 一级菜单如何打开 Custom Steam Library；
- 已运行时是激活已有窗口，还是拒绝重复启动；
- 主程序关闭/返回时是否保持刮削程序独立运行；
- 是否需要传入主程序数据根、Steam 用户或启动来源；
- 二级菜单返回一级菜单时的窗口激活和状态刷新方式。

当前主程序构建资源已经存在统一的游戏手柄事件/路由和窗口错误记录机制；Custom Steam Library 独立模式仍由宿主负责 XInput，YeManCC 父模式则由主程序统一导航并通过语义动作转发。这里不能简单把两个轮询器同时打开，否则会出现一次按键移动两格、焦点跳页或 B 键重复返回。接口冻结稿见 [CUSTOM-STEAM-LIBRARY-INTEGRATION-CONTRACT.md](C:/SOFT/YeManCC-Work/SteamArtworkLab/CUSTOM-STEAM-LIBRARY-INTEGRATION-CONTRACT.md)。

### 2.4 编辑页闪退审计（本轮）

现状与证据：

- 目前未在最近的 Windows Application 事件日志中发现 `SteamLibraryWorkspace.exe` 或 `SteamArtworkLab.exe` 的崩溃记录；发现的 `rundll32.exe` / `RTSSHooks64.dll` 访问冲突属于外部 RTSS 组件，不能归因给本程序。
- 桌面复现过程中用户已主动终止控制，随后反馈“不闪退了”，因此本轮没有把“未复现”误写成“已经完全修复”。
- 编辑按钮路径为网页 `openEdit()`，会复制编辑草稿、重建封面/壁纸预览、填充候选 EXE 并聚焦模态框；当前已加入空节点保护、异常兜底和 `data\state\ui-errors.jsonl` 诊断记录。UI 诊断消息走宿主的安全命令入口，WebView 消息解析失败也改为安全回传。发生下一次编辑页异常时，优先查看该文件、Windows Application 日志和宿主退出码。
- 宿主启动前现在会检查 Worker、`workspace-ui` 和 WebView2 缓存目录；WebView2 环境/控制器创建失败会弹出明确错误并关闭窗口，不再让初始化异常直接表现为闪退。
- 目前没有足够证据把闪退归因到单一原因；源码审计确认了两个高风险边界：编辑页 DOM/草稿异常可能从点击事件冒泡，WebView 正在关闭时原生消息回传也可能抛异常。前者已在 `openEdit()` 做局部兜底，后者已把消息错误回传改为 `postWebMessageNoThrow`，并保留诊断记录，下一次复现即可区分 JS 异常、宿主异常还是外部注入组件。

下一轮需要专门验证：打开库后等待后台扫描、连续打开/关闭编辑页、编辑页内打开图片搜索/名称搜索、取消任务、快速返回，以及窗口关闭期间 Worker 完成回调。这些动作必须在 1280×720 和 1280×800 各跑一次。

### 2.5 自动维护与显式扫描边界

用户要求“自动维护游戏库先取消”。当前宿主编译开关 `kAutomaticLibraryMaintenanceEnabled = false`，并且默认配置 `enabled = false`；目录监听、目录变更自动扫描和网络恢复自动唤醒均不应启动。显式打开“自定义游戏库”、手动刷新、添加/移除目录和单独添加游戏仍可执行必要扫描。

因此回归脚本必须分别验证两件事：

1. 新建目录不会被后台监听自动加入；
2. 用户明确执行扫描后，新目录会加入并保留生命周期记录。

旧的“event-driven refresh”通过字样已经从维护关闭验收中移除，避免把显式扫描误判为自动维护通过。

### 2.6 分离前文件结构冻结与确认闸门

历史分离记录：当时已生成 `C:\SOFT\YeMan\CustomSteamLibrary` 独立发行目录，但尚未接入 YeManCC。当前主程序启动链和三目录升级适配已写入源码，正式目录验收仍单独执行。

确认前必须冻结以下约定：

- 发行根目录包含宿主、Worker、`workspace-ui`、版本/图标资源和启动检查；
- 用户数据只在 `D:\YeMan\CustomSteamLibrary\data`，无 D 盘时使用程序根目录 `data`；
- 升级只替换程序文件，不覆盖 `data`；
- 主程序联动使用版本化启动参数/IPC，不读取对方 WebView DOM；
- 手柄只能有一个输入所有者：独立运行由宿主负责，接入 YeManCC 时由主程序统一空间导航引擎负责，或者由主程序明确把语义事件交给宿主，二者不能同时轮询。
- 分离前检查脚本 `validate-preseparation-structure.ps1` 保留用于以后重新打包前的空目录门禁；当前发行包使用 `publish-custom-steam-library.ps1` 生成，完成后由 `validate-custom-steam-library-package.ps1` 验证。

没有用户明确确认，不进入“最终重命名、生成图标发行包、接入一级菜单、删除旧入口”阶段。

## 3. 分阶段实施计划

### 阶段 0：检查与冻结（已完成）

- 确认当前工程结构、宿主/Worker 边界和构建输出。
- 记录外部数据根、WebView2、单实例锁和主程序依赖。
- 审计期间保留现有 `SteamArtworkLab` 工程和旧数据源；完成真实迁移/抓取验证后，旧 `D:\YeMan\Steam大屏` 已按用户授权清理，恢复依据改为新数据树和迁移备份。
- 统一当前窗口标题显示为 `Steam自定义游戏库 · Custom Steam Library`。

验收：旧版本仍可编译；迁移阶段只能复制，只有用户明确授权且真实验证完成后才允许清理旧源。

### 阶段 0.5：稳定性、错误证据与输入边界（静态门槛已完成，窗口级回归待完成）

- 保持自动维护关闭，只保留用户明确触发的扫描和识别。
- 对编辑页增加 DOM 空节点保护、异常兜底和 UI 错误记录；宿主启动增加 Worker/UI/WebView2 前置检查。
- 验证宿主退出时后台 Worker 回调、目录监视器停止、WebView2 关闭和任务取消不会触发 `std::terminate` 或访问已释放的 WebView。
- 已冻结独立运行的输入所有者为 `host`；宿主把手柄按键转换为单一语义动作，父模式由 YeManCC 转发语义动作，二者不会同时轮询。
- 将窗口级回归拆成“独立运行”和“作为 YeManCC 子窗口运行”两组，未完成前不宣称接入完成。

验收：编辑页异常能留下 `data\state\ui-errors.jsonl` 或 Windows 事件证据；独立运行不崩溃；自动维护关闭时新目录不会自行加入；统一手柄接入仍保持待办。

### 阶段 1：产品身份、文件名与图标（已完成）

- 统一 PE 产品名、文件描述、版权、版本号和窗口类相关显示。
- 独立发行入口已采用 `CustomSteamLibrary.exe`；开发目录保留 `SteamLibraryWorkspace.exe` 作为兼容构建名，YeManCC 源码已引用正式入口。
- 已制作并嵌入 `.ico` 多尺寸图标，同时更新任务栏和发行包图标；快捷方式需待最终发行包时创建/验收。
- 保留兼容启动器或旧文件名过渡策略，避免旧快捷方式失效。

验收：入口文件名、窗口标题、图标和 PE 产品信息已统一；快捷方式图标仍待最终发行包验收，旧数据已能通过 D 盘数据根打开。

### 阶段 2：绿色路径与数据迁移

- 以宿主 EXE 所在目录为 `appRoot`，资源统一使用相对路径：`workspace-ui`、Worker、WebView2 缓存和默认数据。
- 默认数据为 `D:\YeMan\CustomSteamLibrary\data`；没有 D 盘时回退到 `appRoot\data`。
- 允许显式环境变量或配置覆盖数据根，但优先级、显示位置和错误提示必须固定。
- 从现有 `D:\YeMan\Steam大屏`、旧 `appRoot\data\Steam大屏` 或旧 `appRoot\Steam大屏` 迁移时，先备份目标设置，再逐文件复制；目标已有文件优先。旧源只在迁移期间使用，完成真实验证后可删除。
- 迁移记录写入 `data\state\data-migration.json`；备份写入 `data\backups\migration\<timestamp>\`。
- 将临时任务计划、输出文件和 WebView 缓存放入明确的运行数据目录，避免与源码/构建目录混淆。
- 手动素材和自动素材共存于 `data\artwork`；单独 EXE 信息随 `config`、`state`、`games` 一起保留，重装程序只需保留数据根。
- 配置和关键状态写入采用“双重备份 + 原子替换”：目标文件旁保留 `.bak`，同时按时间戳写入 `data\backups\config` 或 `data\backups\state`；写入前不删除原文件，临时文件通过 `MoveFileExW` 替换。目标损坏时依次尝试目标、旁路 `.bak`、集中备份，损坏原件另存到 `data\backups\corrupt`。
- 自动维护暂时保持关闭；不要把 `scanMode=event-driven-no-polling` 误解为当前正在监听目录。显式打开库和用户点击刷新仍然可以扫描。

验收：把程序重新部署后保留 `D:\YeMan\CustomSteamLibrary` 即可恢复设置和素材；D 盘目录丢失时，程序在 C 盘 `appRoot\data` 使用本地设置并重新抓取缺失缓存；无 D 盘时也不丢数据。

### 阶段 3：导入 YeManCC 一级菜单并联动

- 已在 YeManCC 源码增加隐藏的 `Steam自定义游戏库` 一级菜单路由，默认不显示且直接深链会被拦截。
- 已加入明确的启动协议调用：`--integration=YeManCC --protocol=1 --parent-pid=<pid> --input-owner=parent`；独立启动仍使用 `--input-owner=host`。
- 已接入统一空间导航的语义动作桥，只转发 `navigate-left/right/up/down`、`accept`、`back`、`tab-previous/next`、`edit`，不同时传原始方向键和 XInput 状态。
- 已把父子输入隔离收归 YeManCC 原生手柄循环：原生识别子窗口类、EXE 名、`parent` 所有者和当前父 PID，负责 `launching → child-active → returning → disabled`；打开键不重放、子窗口活动时屏蔽父程序全局快捷键、返回键不泄漏。网页桥只负责启动/关闭，不维护第二套输入闸门。
- 子进程启动后通过单实例激活窗口；父进程退出、子进程异常退出和重复启动都要有明确的恢复行为。
- 设计单实例激活、启动失败提示、返回主程序和重复打开行为。
- 若需要联动状态，只通过版本化 IPC/文件协议同步；不直接依赖对方内部 JSON 或 WebView DOM。
- 第一阶段先实现“打开/激活/返回”闭环，再增加识别完成、库刷新等状态联动。

验收：从 YeManCC 一级菜单进入，二级菜单可正常打开；关闭或返回后 YeManCC 可恢复焦点；重复点击不会生成多个工作窗口。

### 阶段 4：发行包与安装位置

- 已形成 `CustomSteamLibrary` 目录清单：入口 EXE、Worker、界面、图标、清单和运行时说明；数据目录不进入程序包。
- 补充便携版启动脚本、缺失文件检查和 WebView2 Runtime 检查。
- 已生成默认目录 `C:\SOFT\YeMan\CustomSteamLibrary`，并验证 Worker 从该程序目录解析到 `D:\YeMan\CustomSteamLibrary\data`；任意路径运行仍待回归。
- 在不覆盖用户数据的前提下提供升级和回滚。

### 阶段 5：回归与发布门槛

- 1280×720、1280×800、普通窗口、全屏/最大化、键盘和 XInput 手柄回归。
- 目录选择、单独 EXE 添加、未加入/未分类/不加入的选择与删除回归。
- 有封面/壁纸、无封面/壁纸、Steam AppID、无 AppID、旧数据迁移回归。
- 验证主程序与独立程序同时运行、重复启动、异常退出和 WebView2 缺失提示。

## 4. 当前已知风险与处理原则

| 风险 | 现状 | 处理阶段 |
|---|---|---|
| 外部 `D:\YeMan\Steam大屏` | 已完成复制迁移、真实验证并清理 | 阶段 2A/2B 已完成；新数据仍可恢复 |
| 新数据根 | `D:\YeMan\CustomSteamLibrary\data`；无 D 盘为 `appRoot\data` | 阶段 2A 已实现 |
| Worker 路径 | 开发布局为 `appRoot\build\SteamArtworkLab.exe`，发行布局可在根目录 | 阶段 2A 已实现解析 |
| UI 路径 | 固定加载 `workspace-ui` | 阶段 2，纳入发行包清单 |
| WebView2 缓存 | 已移到 `data\cache\webview` | 阶段 2A 已实现 |
| 单实例锁 | 当前为 `Local\\YeManSteamLibraryWorkspace` | 阶段 1/3，改名需保留升级兼容 |
| 主程序入口 | 独立入口已为 `C:\SOFT\YeMan\CustomSteamLibrary\CustomSteamLibrary.exe`；YeManCC 源码已具备启动协议和语义动作桥 | 阶段 3，正式目录验收待完成 |
| 数据结构 | 识别、素材和导入计划相互关联 | 阶段 2/5，迁移前做备份和校验 |
| Steam 写入 | 导入会影响 Steam 大屏文件 | 阶段 5，保留预览、确认和回滚提示 |
| 图标/PE 信息 | 已嵌入 7 尺寸 ICO 和 PE 产品信息；快捷方式仍待最终发行包验收 | 阶段 1 已完成；发行阶段待验收 |
| 配置丢失/损坏 | 已有 `.bak` + 集中时间戳备份 + 损坏件隔离；生命周期脚本已验证恢复 | 阶段 2B 已实现 |
| 显式临时数据目录被旧数据污染 | 已修复：显式环境变量目录不执行迁移 | 阶段 2B 已实现 |
| Steam 官方验证 | 已用 6 个本地项目完成 Steam 官方候选验证；真实 `shortcuts.vdf` 前后哈希不变，未启动游戏、未真实写入 | 仍需最终发布环境复测 |
| 编辑页闪退 | 最近 Application 日志没有本程序崩溃；已增加 `openEdit` 兜底、UI 错误日志和宿主启动前置检查，但用户终止桌面复现，尚未完成完整窗口级复现 | 阶段 0.5/5 |
| 自动维护边界 | 产品开关和宿主目录监听均关闭；显式打开库仍会扫描，维护关闭脚本需与显式扫描脚本分开验收 | 阶段 0.5/5 |
| UI 视觉、720P/800P、键盘/手柄 | 已通过静态布局门槛和一轮独立窗口回归；目标分辨率视觉复核与主程序联动仍待完成 | 阶段 0.5/3/5 |
| YeManCC 统一空间导航 | 已把输入所有权收归原生手柄循环；原生识别 Custom Steam Library 子进程并直接发送语义动作；尚未做真实子窗口手柄验收 | 阶段 3/5 |

## 5. 本轮检查与修理结果

- 已把启动和打开游戏库拆成“本地快照 → 后台计划刷新”两阶段：首屏只读取本地配置/扫描状态，Steam 账户发现和计划生成通过 `library-plan-refreshed` 事件回传，不再让弱网或损坏的 Steam 安装延迟首屏和手柄输入。
- 已把自动识别、名称候选搜索和素材任务改为按 EXE/素材类型管理的后台 Job；新任务只取消同一目标的旧 Job，使用 `jobId + generation` 丢弃过期事件，前台编辑和保存不再被网络任务占用。
- 已保留短事务锁只保护本地 JSON 读改写；网络 worker、重试、素材搜索和网络恢复不持有该锁。显式 Steam 计划/提交仍保持用户主动触发和有界超时。
- 已修正事件验收脚本：启动完成以本地扫描状态为准，不再错误等待 Steam 计划文件；事件测试确认自动维护关闭、显式扫描可用、未启动游戏且真实 `shortcuts.vdf` 不变。
- 本轮回归通过：`validate-workspace-startup.ps1`、`validate-workspace-input.ps1`、`validate-workspace-gamepad-parity.ps1`、`validate-workspace-events.ps1`、`validate-lifecycle.ps1`、`validate-steam-fuzzy-recognition.ps1`；发行部署数据保留与回滚验证通过。
- 已修正任务栏/Alt-Tab 使用的原生窗口标题。
- 已修正原生自定义标题栏和 WebView 页面标题。
- 已保留当前 EXE 文件名，避免主程序/脚本入口提前断裂。
- 已统一宿主和 Worker 的程序根、数据根、Worker、UI、缓存和任务输出解析。
- 已把旧手动素材目录映射到 `data\artwork\overrides`。
- 已对真实 `D:\YeMan\Steam大屏` 执行一次复制迁移：复制 451 个文件、约 49.2 MB；真实抓取验证后已按用户授权清理旧目录。
- 已将配置/状态写入改为旁路 `.bak` 与 `data\backups\config|state` 时间戳备份，损坏文件进入 `data\backups\corrupt`；生命周期脚本验证了配置损坏恢复、便携快照校验和事务恢复。
- 已用真实 `D:\Game` 扫描：26 个项目、24 个可用主程序、2 个非游戏、1 个无 EXE 跳过；`validate-library-scan.ps1` 全部通过。
- 已对 `D:\Game\CK3\binaries\ck3.exe` 做真实素材抓取：本地发现 Steam AppID `1158310`，官方接口因 WinHTTP `12152` 失败后安全回退 Playnite IGDB `124954`，实际持久化 Tall `810x1080` 与 Hero `1920x1080` 到 `data\artwork`，任务和 manifest 位于 `data\cache\jobs`。
- 已有完整 `validate-lifecycle.ps1` 通过记录：弱网重试入队/事件唤醒、素材阶段断点、安装/丢失/恢复、快照校验、配置损坏恢复、删除撤销均通过；本轮实时网络唤醒再次受 WinHTTP `12152` 阻断，没有启动游戏 EXE、没有真实写入 Steam。
- 本轮已重新编译宿主与 Worker，并将带图标/PE 信息的正式构建发布到独立发行目录。
- 已修正 `auto-fill-missing.ps1`、`prepare-library-for-steam.ps1` 的旧 `output` 路径假设：审计、批量任务、摘要和计划现在跟随当前数据根的 `cache\jobs`。

### 5.1 本轮新增审计证据与未完成项

- 已加入配置/状态损坏件隔离和恢复诊断；独立恢复探针通过：状态游戏数 `26`，状态损坏件 `1`，状态时间戳备份 `3`，网络队列损坏件 `1`，网络队列备份 `1`。
- `validate-library-scan.ps1`、批量识别选择、素材选择器和 Steam 模糊识别均有通过记录；本轮库扫描、批量选择、素材选择器和 JS 语法检查通过，模糊识别重跑因实时进程长时间无输出而按超时停止，不能计为本轮通过。Node 脚本检查与 PowerShell 解析检查应作为每次源码改动后的最低门槛。
- `validate-lifecycle.ps1` 过去已有完整通过记录；本轮在网络唤醒阶段再次遇到 WinHTTP `12152`，因此本轮不能把实时网络恢复写成通过。弱网入队、持久化、损坏恢复探针已通过，官方 Steam 网络仍待代理/网络环境复测。
- 维护关闭脚本的验收已改成“新目录在观察窗口内不自动加入，再由显式扫描加入”。桌面宿主已有用户进程运行时脚本会拒绝接管，不能为了测试强杀用户窗口；应在用户关闭后再运行窗口级脚本。
- 当前未发现本程序的 Windows 崩溃事件；RTSS 的 `rundll32.exe` 崩溃为外部组件证据。由于编辑页复现被用户主动终止，本项状态是“已加固、待独立窗口复验”，不是“100% 已修复”。
- 主程序一级菜单和协议桥已写入 YeManCC 源码但保持隐藏；输入所有权已改为 YeManCC 原生手柄循环唯一裁决，尚未做真实子窗口手柄验收，目标分辨率完整窗口级 UI 回归也仍未完成。
- `custom_steam_library_integration_selftest.ts` 已改为原生仲裁契约自测：确认子 EXE 识别、父 PID/所有者校验、原生直接语义转发存在，并确认网页层没有第二套输入闸门。
- 本轮新增窗口所有者冲突验收：开发宿主实测 `host=1`，父模式实测 `parent=2`；两种模式互相重复启动均返回 `7`，不抢前台；`validate-yemancc-custom-steam-library-integration.ps1` 已固化该门槛。
- 本轮重新修复了旧 PowerShell 验收脚本的 Windows PowerShell 引号/编码兼容问题；输入、启动、发行包和隐藏接入静态验收脚本均可实际解析并输出报告。
- 已新增分离前契约冻结稿和结构门禁脚本；它们只写开发工作区 `output\preseparation-structure-*` 证据，不启动真实分离。
- 本轮已重新编译当前开发版宿主与 Worker；`validate-preseparation-structure.ps1` 通过，`validate-workspace-events.ps1` 通过：自动维护关闭、新目录观察期未自动加入、显式扫描后加入、轮询为 0、真实 Steam 未变化。
- 结构门禁在实际分离前确认过 `C:\SOFT\YeMan\CustomSteamLibrary` 和 `D:\YeMan\CustomSteamLibrary` 没有程序 EXE；随后才生成独立发行包，`D:\YeMan\CustomSteamLibrary\data` 数据树保持不变。
- 历史记录（分离阶段）：`publish-custom-steam-library.ps1` 生成 `C:\SOFT\YeMan\CustomSteamLibrary\CustomSteamLibrary.exe`、`SteamArtworkLab.exe`、`workspace-ui\` 和启动脚本；当时验证的是 `mainProgramIntegration=false`，当前包已更新为三目录适配并标记为 `true`。

## 6. 本次明确不做

- 本轮只修改 YeManCC 源码中的隐藏接入准备，不把入口显示、不替换已安装主程序、不执行真实主程序分离。
- 本轮已生成并嵌入最终图标及 PE 信息；入口改名只作用于独立发行目录，开发构建仍保留旧文件名。
- 旧 `D:\YeMan\Steam大屏` 已完成迁移和真实抓取验证并清理；恢复来源为 `D:\YeMan\CustomSteamLibrary\data`、`data\backups\migration\1787289899849`、配置时间戳备份和便携快照。
- 不把开发工作区整体复制到发行目录；发行目录只包含独立运行所需的程序、Worker、UI 和说明文件。
- 不在本轮修改 YeManCC 一级菜单或伪造“已完成联动”。
- 不在本轮让 Custom Steam Library 的 XInput 轮询和 YeManCC 的统一空间导航同时工作；父模式已设置为不启动宿主 XInput 轮询。
- 未获主程序联动确认前不打开隐藏功能闸门、不更新已安装 YeManCC 和独立发行包的默认联动状态。

### 5.2 本轮 1–3 执行结果

- 已完成独立/父模式输入语义边界：独立版本使用 `inputOwner=host`；父模式仅接受完整 YeManCC 协议参数，宿主不启动 XInput 轮询，只接收一次 `WM_COPYDATA` 语义动作；YeManCC 不再把原始 XInput 状态传给子窗口。
- 已通过 `validate-workspace-input.ps1`：方向、确认、返回、编辑、LB/RB 标签动作均有映射；卡牌左右同一行、上下按可见网格列移动；WebView 未处理异常和消息发送失败都有诊断路径。
- 已通过 `validate-workspace-layout.ps1`：1280×720、1280×800 的宽高规则、工具条单行禁止换行、三位数字预留宽度和编辑页双栏布局门槛通过。
- 已通过 `validate-product-identity.ps1`：宿主与 Worker 的 ProductName、FileDescription、InternalName、OriginalFilename 和 7 尺寸 ICO 资源通过；入口嵌入 `CustomSteamLibrary.exe` 产品身份。
- 已通过 `validate-artwork-picker.ps1`：素材搜索/下载、候选焦点和语义手柄映射通过；未启动游戏，未修改真实 Steam。
- 已修复键盘 Enter/空格重复处理：卡牌和图片候选在自身处理后停止冒泡；全局路由忽略已消费事件，并对 input、textarea、select 和 contenteditable 保留原生输入行为。
- 已修复后台任务边界：扫描、自动/手动识别和素材任务均在独立线程运行；弱网 worker 不再长期持有任务边界。编辑保存、分类、目录操作和 Steam 提交可继续发起，只在本地 JSON 读改写瞬间使用短互斥，避免读旧快照覆盖新修改。
- 已修复取消范围：名称候选搜索使用独立 `cancelIdentitySearch`，全局取消不会再误杀后台识别或素材任务；手动刷新遇到已有识别队列时改为合并/排队，不再返回阻塞错误，也不再把“已启动”误报成“已完成”。
- 本轮补强后台任务编排：手动识别、名称候选搜索、封面/壁纸任务按游戏或素材槽隔离；同一键的新请求取消旧代次并携带 `jobId`/`generation`，不同游戏可以并行，旧结果不能覆盖当前编辑页。扫描队列中的手动刷新只合并一次后续批次，避免重复点击产生无限任务。
- 本轮补强无阻塞边界：`prepare` 立即返回本地快照和 `scrapeRequest` 状态；Steam 导入计划生成改用有限本地 worker 超时，失败时继续展示最新匹配缓存；搜索页和刷新页保持可操作，取消只终止对应后台 worker。
- 本轮新增回归通过：`validate-workspace-input.ps1`、`validate-workspace-startup.ps1`、`validate-workspace-events.ps1`、`validate-workspace-layout.ps1`、`validate-artwork-picker.ps1`、Node JS 语法检查、独立发行包和便携升级/回滚验证。
- 已对发行包 `C:\SOFT\YeMan\CustomSteamLibrary\CustomSteamLibrary.exe` 做实际窗口回归：入口打开、进入游戏库、右/下/左卡牌导航、X 打开编辑页、封面/壁纸实际显示、Esc 返回均通过；本次桌面实际截图为 1808×926，目标 1280×720/800 目前完成静态门槛，仍需最终目标分辨率视觉复核。
- 历史记录（分离阶段）：发行包已清洁重打，`workspace-ui` 顶层只保留 `app.js`、`index.html`、`styles.css`，不再携带嵌套 UI 目录或 `patch_.js`；当时 `mainProgramIntegration=false`，当前包已进入主程序适配阶段。

## 7. 发布前完成定义

只有以下条件全部满足，才可以把项目称为已分离完成：

1. 独立目录中包含完整运行文件，且不依赖源码或 build 目录。
2. 任意安装位置启动后能找到自身 UI、Worker、缓存和数据。
3. 旧库完成可验证迁移，失败可恢复。
4. YeManCC 一级菜单能启动/激活/返回 Custom Steam Library。
5. 任务栏、文件属性、快捷方式和图标统一使用新产品身份。
6. 720P、800P、键盘、手柄和核心刮削流程通过回归。
7. 独立运行与 YeManCC 子进程运行均只有一个手柄输入所有者，方向导航、焦点、确认和返回通过统一语义事件验收。
8. 用户明确确认后，才执行最终文件复制、重命名、图标嵌入和 YeManCC 一级菜单联动。

当前尚未满足发布完成定义的项目：YeManCC 一级菜单尚未联动，1280×720/800 目标分辨率的完整窗口级视觉复核尚未完成，完整窗口级键盘/手柄正反向回归仍需在不被用户中止的情况下完成。因此本任务单不能标记为 100%。

## 8. 历史资源与升级器对齐审计（2026-08-22）

> 本节记录当日基线。后续三目录适配和独立健康握手已替代其中“尚未接入”的结论；保留原文用于追溯，不作为当前发布状态。

- 已新增 `CUSTOM-STEAM-LIBRARY-UPGRADE-CONTRACT.md`，冻结子包身份、C/D 盘职责、受管文件、永久保留路径、旧路径处理、暂存和回滚边界。
- 历史记录：当时已确认 YeManCC `app.installUpdate` 只接受 `YeManCC` 和 `PowerControl` 顶层目录；当前实现已改为清单驱动三目录安装。
- 已将发行脚本的子包清单升级为 `schemaVersion=2`，增加独立 `packageVersion`、`packageId`、`packageType`、数据根优先级、升级适配器状态、受管路径、保留路径和每个发行文件的 SHA-256 索引。
- 历史记录：当时保持 `mainProgramIntegration=false` 和 `inputOwner=host`；当前包已完成主程序适配，正式目录升级仍未执行。
- 已确认当前真实资源边界：程序包在 `C:\SOFT\YeMan\CustomSteamLibrary`，持久数据在 `D:\YeMan\CustomSteamLibrary\data`，程序包不存在本地 `data`，因此本轮没有发生 C/D 双数据冲突。
- 旧 `D:\YeMan\Steam大屏` 只保留为历史路径，不在升级过程中自动导入或合并。

历史结论：下一阶段只剩主程序侧的 `green-child` 升级适配。该适配已在 2026-08-25 完成；当前剩余的是正式目录升级和实体手柄验收。

## 9. 手柄与弱网链路复核（2026-08-22，继续执行）

- 已确认父模式的唯一手柄所有者仍是 YeManCC 原生循环：主程序负责 XInput、前台判断、LB+RB 组合抑制和父程序输入闸门；子程序父模式只接收经过父 PID/窗口所有者校验的 `WM_COPYDATA` 语义动作，不启动自己的 XInput 定时器。
- 已补齐独立运行模式的空间导航：D-pad/左摇杆阈值、同一方向 220ms 重复、对角线优先水平、LB+RB 组合 50ms 裁决及释放后的短抑制；A/B/X/Y 仍可到达确认/返回/编辑路由。独立模式是测试/绿色运行路径，不能与父模式同时轮询。
- 已把网络恢复从前台命令 lane 拆出：`wakeNetwork` 使用独立 detached worker，重复恢复事件合并为一次 pending，不再以“后台识别正在运行”阻塞编辑、保存、分类和刷新。弱网恢复、素材搜索、目录扫描分别有 8 分钟、3 分钟、2 分钟进程上限；JSON 只在短读改写事务内加锁。
- 本轮新增编排回归：输入验证确认按游戏代次、素材槽隔离和手动刷新合并规则；事件验证确认自动维护仍关闭、显式扫描仍可用、状态事件不轮询且不启动测试游戏。规划刷新也受到本地 worker 上限保护，网络/Steam 请求不会无限持有宿主请求线程。
- 已补齐窗口关闭竞态：后台任务向已关闭窗口投递结果失败时释放结果对象；目录/网络提示在 WebView 已关闭时安全丢弃，避免退出阶段泄漏或异常传播。
- 已完成新宿主回归：自动维护关闭时新增目录在观察窗口内不会自动加入，显式扫描仍可加入；轮询为 0，未启动测试游戏，真实 Steam 文件未改变。`validate-lifecycle.ps1`、`validate-workspace-events.ps1`、`validate-workspace-gamepad-parity.ps1`、`validate-workspace-input.ps1`、`validate-workspace-startup.ps1`、`validate-workspace-layout.ps1`、`validate-artwork-picker.ps1` 均通过。
- 自动化没有注入真实 XInput 手柄，因此仍不把“实体手柄窗口级验收”标成完成；主程序一级菜单、启动协议和统一手柄桥继续保持隐藏，未启用真实接入。

## 10. SteamID 与元数据验证分离（2026-08-22，本轮执行）

本轮已按“先验证、再准入”的建议落地，仍未接入 YeManCC 一级菜单、未打开隐藏联动、未执行真实 Steam 写入。

- Worker resolver 从 `steam-official-fuzzy-global-v3` 升级为 `steam-official-fuzzy-global-v4`。
- `match` 新增并固定三类证据：
  - `steamVerificationStatus`：`steam-verified`、`network-pending`、`needs-confirmation`、`not-available`。
  - `metadataVerificationStatus`：`metadata-verified`、`needs-confirmation`、`not-available`。
  - `steamIdCandidate`：本地配置或安装信息发现的候选 AppID；它不是已确认 SteamID。
- `identityStatus=metadata-verified` 只表示名称/素材元数据通过，不再代表 SteamID 已确认。
- 本地 AppID 遇到可恢复网络失败时，候选会保留到 `steamIdCandidate` 和 `localSteamIdentity`，并标记 `network-pending`；如果 IGDB 可用，可先完成素材，不阻塞前台编辑。
- Steam 导入计划现在只接受当前 resolver、`primaryProvider=steam`、`steamVerificationStatus=steam-verified`、canonical `appId>0` 且素材完整的项目。IGDB-only、网络待验证和人工待确认项目统一进入 `needs-steam-verification` 或 `needs-identity-confirmation`，不会进入 `ready-to-add`。
- UI 不再把本地候选、IGDB ID 或待验证人工 ID 当作列表中的 canonical SteamID；编辑页仍保留用户输入草稿。

### 10.1 本轮回归结果与上一轮对比

上一轮成功基线：`output/steam-fuzzy-recognition-1787398592736/summary.json`，6 个基础案例 + 1 个弱网案例共 7/7 通过，resolver v3。

本轮结果：`output/steam-fuzzy-recognition-1787403866866/summary.json`。

- 正常/弱网识别：7/7 `steam-verified`，7/7 同时完成 Tall/Hero 等素材，resolver v4。
- 名称与 AppID 结果保持一致：WARNO 1611600、SHOGUN 2 34330、Cyberpunk 2077 1091500、Wargame 251060、War Thunder 236390、ROME II 214950；WARNO/ROME II 的内容到基础游戏关系没有退化。
- 弱网重试案例仍通过，Steam 网络重试和素材链路没有因为状态拆分而回退。
- 元数据降级隔离案例：`metadataFallbackResult` 为 IGDB-only，`metadata-verified`，Steam 状态为 `needs-confirmation`，保留本地候选 1611600；该项目没有 canonical `appId`，不会进入导入计划。
- 计划准入正向回归：`output/plan-ready-regression-178740c/plan.json` 中 SHOGUN 2 为 `ready-to-add`，且字段为 `steam-verified`。
- 计划准入反向回归：`output/plan-gate-regression-troy2-178740/plan-v4.json` 中元数据-only 项为 `needs-steam-verification`，`readyToAdd=0`，没有绕过 SteamID 门槛。
- 真实 Steam 文件前后 SHA-256 均为 `475D7F5964246723B5FBE70B718BDC1BBD06A2403734ECDB6A9B68B20ACDB053`，本轮没有真实写入。

### 10.2 可行性结论

方案可行，且比上一轮更安全：联网正常时识别命中率和素材结果保持；Steam 暂不可用时仍能先显示可用元数据和素材，但不会错误地把项目标成可加入 Steam。剩余限制是：完全没有任何元数据 provider 时，系统仍只能生成可恢复失败任务，不能凭本地候选直接生成完整素材 manifest；这属于有意保留的安全边界，不应通过降级准入解决。

## 11. 弱网、阻塞与高命中方案增强（2026-08-22，本轮执行）

本轮目标是让“Steam 官方验证暂时失败”成为可恢复状态，而不是前台阻塞或错误准入。仍未接入 YeManCC 一级菜单、未打开隐藏联动、未执行真实 Steam 写入。

### 11.1 已修复的链路问题

- 高命中链路保留三段证据：本地 AppID 先做官方验证；官方验证失败但 Playnite/IGDB 命中时先保留元数据和素材；Steam ID 只在官方验证成功后进入 canonical AppID 和导入计划。
- Steam 官方请求全部重试失败、但元数据 provider 成功时，`match.steamVerificationStatus` 标记为 `network-pending`，并生成 `stage=steam-identity` 的事件唤醒任务；候选 AppID 保留，不丢失、不冒充已确认 ID。
- 手动刷新队列现在会重新处理 `needs-steam-verification`，因此网络恢复后不会把“元数据已显示但 Steam 尚未确认”的项目永久留在中间态。
- 网络唤醒重复事件不再丢失：当前批次运行期间到达的事件会合并为一次 follow-up batch；前台命令 lane 不被网络唤醒占用。
- 唤醒子进程从无限等待改为 7 分 30 秒有界等待；宿主 worker 仍有 8 分钟上限。超时/取消后会等待清理，并把遗留的 `running-event-wakeup` 修回 `waiting-network`，带回退时间和失败原因，不会卡死在 running 状态。
- 宿主 worker 超时/取消后的管道清理增加异步终止保护，避免最终读取继承管道时再次阻塞。
- 手动识别不再把 `metadata-verified` 或 `network-pending` 误写成 `identityStatus=ready`；只有 `steamVerificationStatus=steam-verified` 才能保存 canonical SteamID，网络待验证项目保留 `steamIdCandidate` 并继续可重试。
- 网络唤醒子进程继承父进程当前目录，修复相对输出目录被错误写到 `build\output` 的缓存分叉问题。

### 11.2 本轮验证证据

- 独立 audit 编译通过：`build\SteamArtworkLab.audit.exe`、`build\SteamLibraryWorkspace.audit.exe`；后续路径修复后的 Worker 版本为 `build\SteamArtworkLab.audit2.exe`。
- 完整 Steam 识别回归：`output\steam-fuzzy-recognition-1787408277600\summary.json`，7/7 官方 Steam 验证，7/7 素材完整，弱网案例通过，真实 `shortcuts.vdf` 前后哈希一致。
- 弱网入队实测：`output\weak-network-queue-audit\queue-before.json`，任务状态为 `waiting-network`，唤醒参数移除了故障注入参数。
- 弱网唤醒实测：同目录 `queue-after.json`，1 个任务完成、进入 history、活动 jobs 为 0，`POLLING_USED=false`。
- 相对路径修复复测：`output\weak-network-queue-audit-2\failed-run\shogun2-34330\manifest.json` 位于原始输出目录，不再落到 `build\output`。
- 静态并发/输入门槛：`output\input-validation-1787408259102\input-validation.json`、`output\gamepad-parity-1787407739833\gamepad-parity.json` 通过；Node UI 语法检查通过。

### 11.3 当前剩余边界

- 当前弱网故障注入可以验证重试与队列，但受本机实时网络、代理和 WinHTTP 状态影响，不能把外网恢复速度当作产品 SLA；正式发布前仍需在目标网络环境做一次窗口级恢复验证。
- 本轮没有强杀用户正在运行的正式宿主实例，因此正式输出 EXE 未覆盖；audit 输出已完成源码编译验证。关闭占用正式 EXE 的旧测试进程后，应再执行 `build.bat` 生成正式文件。
- 本轮仍不执行 YeManCC 一级菜单的真实启用和主程序侧升级器适配；这些属于用户确认后的分离阶段。

## 12. 编辑页自动识别交互（2026-08-23）

- 编辑页入口名称统一为 `自动识别`，结果窗口改为 `自动识别结果 / 选择名称和 ID`。
- 点击自动识别只调用后台 `searchIdentity`；宿主立即返回，搜索由独立 generation/job worker 执行，90 秒有界超时，不占用打开游戏库和前台编辑命令。
- 即使用户已经填写 Steam AppID，也不再直接跳过结果页；当前输入会作为“当前输入”候选与网络候选一起展示，最终由玩家确认名称和 ID。
- 候选确认只修改编辑草稿，不写入配置；只有点击 `保存内容` 才调用 `identifyOne` 并启动后续 Steam/IGDB 验证与素材补齐。
- IGDB 候选的暂存/保存链路已补齐；界面明确显示 `IGDB ID（待 Steam 验证）`，不会把它误显示为已确认 SteamID。
- 打开游戏库仍保持“本地快照先显示，扫描/自动识别后台运行”的边界，未增加启动阻塞。
- 本轮通过：Node UI 语法检查、`validate-workspace-input.ps1`、`validate-workspace-layout.ps1`、Worker/Host audit 编译及 Steam 身份自测。

## 13. 自动搜索统一协调与互不阻塞（2026-08-23，本轮执行）

本轮重点复核了所有会触发联网搜索、识别或素材请求的入口：打开游戏库的自动首轮刮削、编辑页 `自动识别`、保存后手动识别/补齐、封面/壁纸搜索与下载、Steam 网络唤醒，以及手动刷新合并队列。

- 发现并修复一个真实门控缺口：编辑页 `searchIdentity` 原先虽然最终使用独立 `identity-search` 后台任务，但请求入口仍先经过通用 `g_taskRunning`，因此在其他短命令运行时会误报“已有任务正在运行”；请求入口还会同步递归扫描历史缓存，弱网或大缓存下会延迟编辑页响应。
- 现在 `searchIdentity` 使用独立的非阻塞请求入口：只做 EXE 存在性和查询文本校验，立即返回 `pending=true` 与空候选，网络结果通过 `identity-search-complete/failed` 事件返回；90 秒 worker 上限和取消机制保持不变。
- 统一协调采用“按目标隔离、按类型分代、全局不串行”的模型，而不是再增加一个全局搜索锁：
  - 自动库刮削由 `g_scrapeQueueRunning` 管理，同一队列内顺序访问 worker，手动刷新只合并一个后续批次。
  - 编辑页自动识别由 `g_identitySearchJobs` 和 EXE 代次管理；同一 EXE 的重复请求取消旧代次，不同 EXE 可并行，且不受刮削队列或前台 `g_taskRunning` 影响。
  - 保存后的手动识别、素材搜索/下载、网络唤醒分别拥有自己的 job 控制和有界进程等待，不会因为另一个网络请求而互相取消。
  - 本地 JSON 读改写仍只在短事务内使用 `g_taskBoundaryMutex`/`g_jsonWriteMutex`；网络等待不持有该锁，用户可以继续编辑、保存、刷新和返回。
- 结果安全边界保持：每个后台任务使用唯一输出目录和 `jobId/generation`；旧代次只能结束自身进程，不能把候选、缓存或素材覆盖到当前编辑会话。自动识别结果仍只是候选，必须由玩家确认后保存。
- 取消边界也已补齐：编辑页识别进行中时，统一取消按钮只调用当前 EXE 的 `cancelIdentitySearch`；只有不存在编辑识别任务时才调用全局 `cancelTask`，避免取消自动识别时误杀库扫描、手动识别或素材任务。
- 结论：需要“统一协调”，但应是统一的后台任务协议/状态模型，而不是把所有搜索塞进一个总队列。当前项目已按这个模型完成修复，两个自动搜索可以同时启动；同一 EXE 的重复自动识别会做代次替换，避免重复请求和旧结果回写。

### 13.1 本轮验收门槛

- 静态验证新增：`searchIdentity` 必须绕过通用前台任务门控、请求必须立即返回 `pending`、必须走独立 generation/job、并与 `g_scrapeQueueRunning` 保持不同状态域。
- 未启用 YeManCC 一级菜单联动、未修改主程序统一手柄开关、未执行真实 Steam 写入；本轮只改变独立 Custom Steam Library 的搜索协调边界。

## 14. P0/P1 发布阻断审计（2026-08-23，当前轮）

本轮按“发布阻断/数据丢失/不可恢复卡死/重复输入所有权”重新审计，目标是当前源码和待发布包中没有可复现或证据确认的 P0/P1。YeManCC 一级菜单接入仍保持隐藏，不把未完成的主程序联动算作已交付。

### 14.1 本轮发现并修复

- P1：两个兼容宿主同时启动时，旧逻辑在激活/冲突处理后没有立即结束，可能留下第二个宿主和第二套 XInput/窗口消息路径。现在兼容重复启动返回 `0`、冲突模式返回 `7`，进程数保持 1。
- P1：窗口关闭和 detached Worker 结果回投存在退出竞态，关闭后仍可能向已销毁 WebView 投递结果。现在关闭先设置 shutdown 闸门、取消任务，再恢复父窗口并销毁；投递失败会释放结果对象。
- P1：两个 Worker 同时将弱网失败写入重试队列时，后一个读旧队列会覆盖前一个任务。现在队列读—改—写、唤醒状态更新和完成/失败修复都使用跨进程短互斥，网络请求本身不持锁。
- P1：两个 Worker 同时保存手动名称时，旧逻辑即使写入成功也可能发生“后写覆盖先写”。现在手动名称、清除名称、清除 ID、保存 ID 都在完整读—改—写事务内合并最新 JSON。
- P1：并发 Worker 的稳定 `*.tmp` 文件和备份激活会竞争。现在所有 JSON 原子写入及备份激活使用同一跨进程事务，生命周期事件追加也使用同一事务，避免临时文件冲突和审计事件丢失。

### 14.2 当前回归证据

- `build.bat`：宿主和 Worker 编译通过。
- `validate-workspace-input.ps1`：通过；键盘/手柄语义映射、关闭取消、重复启动、弱网有界等待、后台不阻塞和跨进程事务门槛均通过。
- 并发真实 Worker 压测：`output/concurrency-p0p1-final-1787494882429`；两个进程均按注入弱网失败退出，最终 `manualItems=2`、`retryJobs=2`、`lifecycleEvents=2`，无临时文件错误。
- `validate-steam-fuzzy-recognition.ps1 -IncludeWeakNetwork`：`output/steam-fuzzy-recognition-1787494131692/summary.json`，7/7 Steam 命中、7/7 素材完整、弱网案例通过、真实 `shortcuts.vdf` 前后哈希一致。
- 历史记录：产品身份、输入、布局、启动、手柄奇偶、素材选择器、便携升级/回滚和发行包验证均通过；当时发行包验证为 `mainProgramIntegration=false`，当前包已进入主程序适配阶段。

### 14.3 当前 P0/P1 结论与边界

当前源码和待发布包：P0 = 0；P1 = 0（没有可复现或证据确认的发布阻断级问题）。实时生命周期测试若遇 WinHTTP `12152`，只记录为当前网络/代理环境的外部阻断；本轮独立真实识别已在稳定网络下 7/7 通过，不能把外部网络波动写成产品通过或产品缺陷。

仍未宣称完成的项目是用户明确保留的范围：YeManCC 一级菜单真实启用、主程序侧升级器适配、实体手柄窗口级验收和 1280×720/800 的最终视觉复核。这些不是本轮 P0/P1 结论，也不会在未确认前自动启用。

## 15. 过度修复反向审计（2026-08-23，当前轮）

本轮专门检查上一轮健壮性修复是否把数据安全变成新的全局阻塞或死锁。

- 发现并修复 P1：宿主 `executeWorkspaceCommand` 入口原先把 `g_taskBoundaryMutex` 持有到整个 Worker 生命周期，Steam 提交、素材刷新、删除和本地扫描会阻塞其他本地状态动作。现已移除入口大锁；手动名称/清除 ID/素材覆盖和后台状态提交只在本地 JSON 读改写期间加锁，Steam 提交单独使用 `g_steamOperationMutex`。
- 发现并修复 P1：拆开入口大锁后，计划、提交和快照线程对 `g_steamTarget` 存在并发读写风险。现已增加短时 `g_steamTargetMutex`，所有目标读取使用快照，避免 C++ JSON 数据竞争。
- 反向并发实测：`output/overcorrection-concurrency-clean-1787496676191`；弱网两个 Worker 同时运行时，本地配置命令 `0.296s` 返回，手动名称 `2` 条、重试任务 `2` 个、生命周期事件 `2` 条、临时文件 `0`。
- 本轮输入、启动、布局、手柄奇偶、身份自测、素材选择器、便携升级/回滚、前向/反向验证均通过；主程序联动仍为 `false`，未写真实 Steam，未启动游戏 EXE。
- 本轮完整实时 Steam 识别受外部 WinHTTP/代理等待影响而停止观察；Worker 未遗留。Worker 源码未在本轮变更，最近一次稳定网络证据仍为 `output/steam-fuzzy-recognition-1787494131692/summary.json`，7/7 Steam 验证和素材完整通过。

当前结论：P0 = 0；P1 = 0。上一轮的全局阻塞过度修复和拆锁后的目标状态竞争均已消除；剩余联网等待属于环境限制，不构成源码 P0/P1。

## 16. 年份显示保守策略（2026-08-23）

- 自动识别候选列表不再显示“年份未知”占位；候选没有可信 `year`、`releaseYear` 或刮削返回的 `releaseDate` 时，只显示名称、ID 和来源。
- 只有刮削结果中实际带有可解析年份时，才在候选名称前显示年份；排序仍可按内部年份值进行，不影响候选命中和玩家选择。
- 已更新 `app.js` 缓存版本，避免旧 WebView 缓存继续显示旧年份占位。

## 17. Steam 年份链路修复（2026-08-24）

本轮确认“一个年份都刮不出”不是 Steam 数据缺失，而是详情已经请求成功后没有继续传递：Steam `appdetails` 的标准字段是 `data.release_date.date`，旧逻辑只把它用于 metadata 展示，没有写入候选审核记录、match 或 Steam 计划项。

- Worker 新增统一年份解析：支持 `release_date.date`、规范化 `releaseDate.display`、显式 `year/releaseYear`；无法解析时保持空值，不猜年份。
- 已验证的 Steam 候选现在把 `year`、`releaseYear`、`releaseDate` 写入全局候选审核记录；编辑页“自动识别”候选可以直接显示真实年份，不再需要二次联网。
- 最终 manifest 的 `match` 和 `metadata` 同时保存年份；Steam 计划项也携带 `year/releaseYear/releaseDate`，因此库快照和编辑页不会在计划映射时再次丢失。
- 宿主读取历史 rounds、失败候选和 metadata 时也兼容 `release_date/releaseDate`，旧缓存无需全部作废即可重新显示已有年份。
- 新增 `--release-year-self-test`，覆盖 Steam 日期、旧键、规范化 metadata、IGDB Unix 秒、缺失日期和无关数字；自动识别回归脚本会在日期文本带年份时校验 match 与 metadata 一致。

验证证据：

- `SteamArtworkLab.exe --release-year-self-test`：6/6 通过。
- 真实 Steam AppID `1158310`（Crusader Kings III）联网 metadata-only：`match.year=2020`、`metadata.releaseYear=2020`、`metadata.releaseDate.display=Sep 1, 2020`。
- 无 AppID、仅名称 `Crusader Kings III` 自动识别：候选审核记录带 `2020`，最终 manifest 同样为 `2020`；同一轮其他 Steam 候选也成功带出 `2016/2026`，说明不是只给当前选中项打补丁。
- IGDB 备用链路 `first_release_date=1716422400` 实测转换为 `2024`；Steam 暂时不可达时，元数据候选仍可显示年份，但仍保持 `network-pending`，不会冒充 Steam 已确认。
- Steam DLC/版本记录如果自身只有“Coming Soon”而已验证的 base game 有正式日期，会回退使用 base game 年份，避免内容页覆盖掉可用年份。
- `validate-workspace-input.ps1`：通过；`build.bat`：Worker/Host 编译通过。

## 18. 一级菜单接入执行记录（2026-08-24，当前阶段）

- 已从“隐藏准备”切换为主程序源码启用：YeManCC 新增一级菜单路由 `/custom-steam-library` 和页面 `CustomSteamLibraryView.vue`，入口显示为“自定义游戏库”。
- 已新增原生异步 IPC：`customSteamLibrary.launch`、`customSteamLibrary.close`、`customSteamLibrary.isActive`。启动固定使用 `C:\SOFT\YeMan\CustomSteamLibrary\CustomSteamLibrary.exe`，不等待 WebView/网络，不阻塞 YeManCC 消息泵。
- 已接入 protocol 1：父模式参数为 `--integration=YeManCC --protocol=1 --parent-pid=<pid> --input-owner=parent`；独立模式仍为 `--input-owner=host`。发现独立 host 实例时拒绝静默复用，避免错误报告运行中和双输入所有者。
- 已将统一手柄逻辑落到 YeManCC 原生 XInput 线程：方向、A/B、X/Y、LB/RB 转为语义动作；子程序活动期间屏蔽主程序 B 双击、LB+RB 呼出、Start+方向、选择键组合和前端页面导航；启动按键必须完全释放后才放行。
- 已加入入口页 B 返回主程序：父模式子页面收到 `back` 后调用 `closeWorkspace`，宿主优雅关闭并恢复 YeManCC 焦点；独立运行不启用该关闭行为。
- 已重新编译：`C:\SOFT\YeManXX\YeManCC4\YeManCC3\native\YeManCC.exe`、`C:\SOFT\YeManCC-Work\SteamArtworkLab\build\SteamLibraryWorkspace.exe` 和 Worker 均通过；主程序 `vue-tsc --noEmit` 与生产前端构建通过。
- 已生成集成测试包：`SteamArtworkLab\output\CustomSteamLibrary-integrated-test-20260824-214832`，清单已标记 `mainProgramIntegration=true`，同时保留独立 host 模式。
- 默认目录暂未覆盖：当前 `C:\SOFT\YeMan\CustomSteamLibrary\CustomSteamLibrary.exe` 与 `C:\SOFT\YeMan\YeManCC\YeManCC.exe` 正在运行。为避免中断用户进程，本轮未强制替换；停止两个进程后再执行默认目录部署和实体手柄窗口验收。

当前阶段结论：源码接入、三目录升级器适配和 Custom 独立健康握手已完成编译及临时包回归；默认包部署、正式目录升级、真实主程序窗口链路和实体手柄仍是剩余验收项。
