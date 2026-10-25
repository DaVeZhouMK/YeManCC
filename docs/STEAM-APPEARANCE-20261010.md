> 历史 v1 记录；以 README-v2.md / STEAM-APPEARANCE-V2-20261010.md 及 final-audit-v2.json 为准。本文中的 BAT 修改已撤回，最终 BAT 恢复任务开始前原样；本文的旧类型检查结果不代表当前全项目状态。

# YMCC Steam 外观开关：实现与预览

日期：2026-10-10（Asia/Hong_Kong）。本轮范围：修复现有 BAT 的 CSSLoader 重复启动；实现外观开关、重新应用及手柄全屏编辑；先不提供参数调节，但完整保留参数与后续扩展入口；提供可交互 UI 预览。

## 已实现

### 1. 修复重复启动

真实复现：tasklist 默认 TABLE 把 CssLoader-Standalone-Headless.exe 的长映像名称截断，再 find 完整名称永远匹配不到，导致不断 start。

- 现有 BAT 的 CSSLoader 分支改为调用 steam-appearance.ps1 -Action Ensure。
- Ensure 用完整进程名、路径核对及跨进程 Mutex，处理串行与并发启动。
- 其他联动程序改用 tasklist /FO CSV /NH，不改 Steam 参数、系统启动任务、Xbox 行为、加速器亲和性等原有逻辑。
- 手动“启动 Steam 美化”入口也复用单实例服务。
- YMCC launchSteam 的常驻启动改用 shell.hidden，避免 shell.run 退出时的同步 Job 连后代一起回收。
- 实测本机 Headless 是一个 PyInstaller 父进程 + 一个工作进程，共 2 个；这是一组后端，不是两个独立重复实例。四次同时启动后的两项 PID 保持不变。

已同步至本机的文件：
- C:\SOFT\YeMan\PowerControl\YeManSteam2.bat
- C:\SOFT\YeMan\PowerControl\steam-appearance.ps1

两个文件均与源码摘要一致。原 BAT 备份在 G:\YeManCC-Work\Backup\SteamAppearance-20261010\installed。

### 2. 生效方式

主题开关 → 校验配置 revision → 只改 active（保留所有 patches/components）→ 调用已有 CSSLoader 的 set_theme_state → 回读实际后端状态。

- Art Hero 关闭会使加载器级联关闭可选依赖；实现显式恢复用户独立选择的文字居中/迷你轮播状态，set_deps/set_deps_value 均为 false，避免参数默认值覆盖。
- “重新应用”从磁盘读取这组开关，逐主题重新提交样式，不重启 Steam，不杀掉加载器，不重新运行完整启动 BAT。
- 本机实测全局 reset 会访问坏的/空白 CEF 目标并超时，因此不使用它作为正常开关或刷新实现；没有用超时作为重启进程的理由。
- UI 区分“配置已保存”“后端未就绪”“配置与加载器一致”。失败不显示乐观成功，不自动重发不确定的写操作。
- 后端未启动时，只在 Steam 已运行且美化联动已开启的情况下复用 Ensure；Steam 未运行时保存配置供下次启动读取。
- RPC 只允许固定方法和这 7 个主题；拒绝任意路径/方法透传，核对 35821 的进程归属与实际主题路径。
- 所有 RPC 有超时/总预算，JSON 输出用 ASCII Unicode 转义，兼容 native shell.run 的 OEM 解码与 Windows PowerShell 5.1。

### 3. UI 与手柄

- Steam 大屏主页新增一个紧凑外观卡片：全部开关、开启数量、编辑外观、重新应用与后端状态。
- 7 个分项放在独立全屏编辑器，不在主页堆叠。小屏单列，大屏双列，内容区可滚动，头尾固定。
- D-pad / 左摇杆移动，A 切换，B 返回并恢复入口焦点，Y 重新应用。键盘支持 Tab / Escape；预览额外支持方向键、Enter、R。
- 全屏使用既有 data-gp-modal 和原始空间导航代码，屏蔽背景交互，LB/RB 不切换背后的页面。
- 本轮没有新增滑块或参数下拉；theme.json、原 config_USER.json 的参数结构、参数数量元信息和独立编辑组件仍保留，后续可在全屏页增加 Slider/Dropdown，不需要拉长主页。

## 验证证据

- Windows PowerShell：21 项通过，含原参数保留、独立依赖状态、过期 revision 拒绝、失败状态、6 个并发启动进程的惯性 EXE 单实例验证。
- 浏览器/服务/手柄：22 项通过，使用真实 SteamView/Vue 组件、真实 typed service、生产空间导航代码；1280/720/420 宽及 512 高布局与焦点收敛、A/B/Y、预览 Gamepad API 都通过。没有页面运行错误。
- 实际 Steam/CSSLoader：9 项通过，直接检查真实 Steam 大屏 DOM；Round 关闭样式数量 32→29，开启回到 32，圆角恢复 10px；全部关闭 32→0，全部开启 0→32；同一次验收 Steam 大屏目标 ID 不变。
- 实际 BAT 联动循环：4 个并发循环复用同一后端组，不新增进程；用隔离 TXT 目录，仅执行原 BAT 的联动循环，不执行其系统任务/Steam/亲和性段。
- vue-tsc --noEmit：通过。
- Vite 生产构建：通过；只有项目已有的混合导入和大包警告。
- 验收后 7 个主题全部恢复原启用状态，61 个原主题文件 SHA-256 与清理前一致。

证据目录：G:\YeManCC-Work\Deliverables\SteamAppearance-20261010\validation

边界：浏览器导航/按键测试使用软件输入与内存原生适配器，不把它称为物理手柄真机验收。真实 Steam 测试单独使用已安装 helper 和真实 CSSLoader 接口，不是模拟后端。

## 可预览 UI

目录：G:\YeManCC-Work\Deliverables\SteamAppearance-20261010\preview

- index.html + fixture.js 是实际 Vue 组件编译出来的可交互预览。
- serve-preview.mjs 只绑定 127.0.0.1，提供两个静态文件；node serve-preview.mjs 会输出本机地址。
- 当前预览地址/进程在 preview-server.json 中。
- 预览对接内存状态，不会更改真实 Steam、启动或关闭程序，可放心切换开关。
- 预览可接浏览器 Gamepad API 的标准映射手柄；没有声称对所有型号都做了物理连接测试。
- 截图包括主页、720 宽全屏编辑器、420 宽滚动编辑器。

## 源码改动

在 G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC 下：
- PowerControl\YeManSteam2.bat
- PowerControl\steam-appearance.ps1（新）
- src\bridge\yeman.ts（只改 Steam 常驻启动方式）
- src\bridge\steamAppearance.ts（新）
- src\components\SteamAppearanceCard.vue（新）
- src\components\SteamAppearanceEditor.vue（新）
- src\views\SteamView.vue（新增入口与关联刷新，顺延导航行）
- src\gamepad\engine.ts（只增加全屏编辑器页面锁，不重写既有导航）
- tools\steam_appearance_selftest.ps1（新）
- tools\steam_appearance_browser_selftest.mjs（新）
- tools\steam_appearance_live_selftest.mjs（新）

保留了源码中本来存在的未提交改动。本次未覆盖正在运行的 YMCC 可执行文件/正式发布目录；新 UI 已接入源码、通过构建并在独立预览中提供。原生壳没有新增 IPC 命令，沿用 fs / settings.location / shell.run / shell.hidden。

本轮产出不是完整新版 YMCC 安装包；PowerControl 文件已在本机修复，前端用于预览和后续正常主线构建集成。