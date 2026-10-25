# Steam 启动必须手动：原生后台修正（2026-10-11）

## 当前交付状态

**本轮源码已修正，冻结源码的原生候选已编译通过；没有部署到正在运行的安装版。**

上一轮的 Steam 页面热补丁已经部署，但它只守住了前端启动按钮，不能约束旧原生后台的自动联动/第三方注入。因此不能再让用户仅重启 YMCC，就宣称这一轮问题已经修好。

本轮完整原生候选来自共享工作树；该树中同时还有风扇、触控板等任务的原生变更，而且构建期间这些文件确实继续变化。本任务保留并未修改那些实现，没有拿完整候选覆盖安装，以免带入其它任务尚未验收的部分。正式更新要把本轮原生修改与最终前端一起整合，不是单独换 SteamView JS。

候选：G:\YeManCC-Work\Deliverables\Steam-Manual-Only-20261011\frozen-native-candidate\Build\App\Native\YeManCC.exe。

当前安装 native SHA-256：e6cacc4b3437a54e3851d1d9e8888aec602c3e966a679a868f44e4d9e551112a。
候选 SHA-256：55ee8c2c5de0fbbf907d5d3f4a43147df40cd3ffe023b90ec92ab039e7c06719。

## 用户意图

- 打开/记住自定义外观不是启动 Steam 或切换大屏的授权。
- 重开 YMCC、页面加载、睡眠恢复、读取状态不能拉起 Steam、最大化 Steam、重新注入并恢复大屏。
- 只有明确点击 Steam 启动/模式按钮，才能执行该动作。
- 保持美化配置、7 个主题参数、已运行的 CSSLoader，不修改用户 BAT/VBS。

## 本轮发现的遗漏

原生 steamSettingsObserverStart 会在启动时观察已有 Steam，并把已保存的 CSS/侧栏开关交给后台工作器。旧工作器能在这时启动原加载器，而不是仅观察。

当前本机 19:38:22 的日志表明：YMCC PID 36024 开始后原生已启动原侧栏加载器、注入 bootstrap；这发生在页面输入引擎就绪之前。

只读检查实际 PluginLoader_noconsole.exe 的打包代码发现：decky_loader.helpers.create_inject_script 在 window.deckyHasLoaded 已为真时会调 SteamClient.Browser.RestartJSContext；Windows inject_javascript 路径确实会生成并执行这段脚本。因此只检查 YMCC 是否调用 steam.exe/BAT 不够，也必须限制重开 YMCC 时的自动后台初始化和再注入。

这是已确认的代码路径和风险，不能冒充已唯一复现所有“拉前台/切大屏”的原因。本轮没有实际启动/切换 Steam 验证该分支。

证据：G:\YeManCC-Work\Deliverables\Steam-Manual-Only-20261011\validation\runtime-evidence.json。

## 修正后的原生规则

### 会话启动资格仅在内存，不写入配置

新文件：G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\steam_link_admission.h。

资格绑定 Steam PID、进程创建时间和安装目录。保存的 enabled/active 标志、外观草稿、账户切换均不是资格来源。

- Steam 在 YMCC 启动前就存在：默认仅观察。重开 YMCC 清空上次手动资格。
- Steam 真正是在 YMCC 启动之后新创建的：保留既有缺失 CSS/侧栏恢复能力（并不是 YMCC 自动创建 Steam）。
- 成功的手动 bigpicture 请求，且当前会话仍匹配请求 PID/birth，才能给既有 Steam 会话打开一次内存联动资格。
- 手动切回 desktop 记录该会话的禁止再注入状态，即使它晚于 YMCC 创建，也不能因此又恢复大屏。
- Steam 退出/身份改变不继承旧资格；无法确认进程创建时间则失败关闭。

### CSS 启动保护

启动时对于未取得资格的既有 Steam，只 probe/adopt 已运行的 CSS 进程，不调用创建/重启逻辑。CSS 缺失时进入 waiting-manual-steam，并暂停重试预算、等待事件；不会把记住美化开关等同于自动启动授权。

手动 Steam 大屏动作可唤醒一次原有有界恢复预算。已有 CSS 复用，不强杀或重载，不重置主题。

### 侧栏初始化保护

侧栏工作器在 beginPending、资源准备、bootstrap 注入、原加载器创建之前检查资格。被阻止时仍保留 enabled=true、真实 steamRunning=true；只是等待手动，不伪装成 Steam 退出。

除了资格，还要求只读检测确认实际 bigpicture 模式。桌面模式不能启动原加载器并借其再注入改变主界面。

手动模式请求返回可能早于 UI 真正切换；共享窗口事件只能重查 waiting-manual-steam 的已获资格会话。已经健康的加载器不因此再次注入/重启。没有新增空闲轮询或定时扫描。

## 本轮自有源码范围

- G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\steam_link_admission.h
- G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\decky_sidebar_runtime.h
- G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\steam_cssloader_guard.h
- G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\main.cpp
- G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\src\bridge\deckySidebar.ts
- G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\steam_manual_startup_selftest.cpp
- G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\steam_manual_startup_source_selftest.mjs

native/main.cpp 的共享改动未整体覆盖/回滚；只在 Steam 原生初始化与 Steam 模式 IPC 处增加资格判断。新增测试在隔离目录，不实际运行 Steam/外部加载器；原侧栏生命周期回归使用自己的源构建惰性子进程。

## 验证

- Manual startup admission / real C++ runtimes, inert fixtures：30 项通过。
- Existing CSS guard regression：63 项通过。
- Existing CSS source safety regression：23 项通过。
- Existing sidebar process lifecycle, source-built inert child only：32 项通过。
- Frozen exact startup admission wiring：11 项通过。

合计 159 项。vue-tsc --noEmit 通过，冻结源码的完整 native 构建 BUILD_OK。候选未运行在真实设备上，没有启动、关闭、重新加载 CSS/Steam 或模式切换。

源码、安装预设资源、实际用户 themes 三处 61 个资源/配置均与原 manifest SHA-256 一致，BAT/VBS 保持原字节。未改全局环境变量，未修改持久化开关。

## 后续必须完成的整合

这份修复需要原生 YeManCC.exe 更新，单换前端或仅重启仍加载旧原生程序不会生效。当前旧安装仍是上述旧 SHA-256。本轮暂不覆盖正在进行其它任务更新的安装；整合任务应合入新 admission 头文件、native 运行时检查、main 的 Steam 初始化/IPC 修改以及前端 waiting-manual-steam 状态文案后，正常构建部署。

记录：G:\YeManCC-Work\Deliverables\Steam-Manual-Only-20261011\validation\summary.json。
