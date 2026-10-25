# Steam 页面故障定位与本机窄范围修复（2026-10-11）

## 本轮已实际部署，不再只是源码候选

只更新 Steam 页面 JS/CSS、主模块中的 Steam presence/linked launch 两个实现，追加只读可见状态导出，并在当前运行根补齐缺失的 64 个外观组件/资源文件。没有部署整套前端、没有改 native、BAT/VBS、环境变量、风扇/手柄实现，没有修改实际主题文件，也没有重启 YMCC/Steam/CSSLoader。

部署记录：G:\YeManCC-Work\Deliverables\Steam-Page-Repair-20261011\validation\deployment.json。

## 为什么明明安装了却提示组件未安装

本机用户环境变量 YEMAN_POWER_CONTROL_DIR 与当前进程日志均指向：

G:\YeManCC-Work\Mainline\Temp\ai-cpu-bundle-R11-rebuild\PowerControl

18:20:23.302 的 decky prepare 日志标明 directorySource=environment-override、processPid=2308，实际 PowerControl 就是该目录。

C:\SOFT\YeMan\PowerControl 中存在外观脚本和预设，但实际运行根中原来缺少 steam-appearance.ps1 及 steam-appearance 目录。因此页面按 settings.location 查找脚本时，确实找不到。不是 CSSLoader 没有运行，也不是必须更新整个 YMCC。

修复没有删除或更改这个全局变量，避免影响其它任务；仅在已核实的有效目录补齐缺失外观资源。目标如已有不同组件将失败停住，不擅自覆盖。源码错误提示现在包含具体目录/文件，并说明不代表实际美化没有运行。

## Steam 自动启动/最大化：已确认与未确认

- 当前安装版以前没有上轮源码的显式启动意图保护；本轮把该保护实际部署了。
- Steam 页挂载、恢复、只读检测均不应调用启动或模式切换。实际构建页面的内存原生适配器验证了这条路径。
- 共享启动函数无 userInitiated=true 则拒绝；已有 Steam 则不执行 BAT；依赖检查前后复核进程；无效/失败检测不是未启动。
- 本轮保留了安装模块上的运行时 guard，不能用构建器对“所有已知调用都带 true”的常量折叠当作可热补丁模块的公共保护。
- 本机观测：Steam PID 50424 在 2026-10-11 08:16:23 启动，YMCC PID 2308 在 18:20:22 启动。当前这一轮 Steam 进程本身不是这个 YMCC 启动后新创建的；窗口被拉起/最大化的具体触发源仍未唯一确认。
- Native 会按用户原要求启动缺失的 CSS/侧栏加载器；没有为了测试关闭这些功能。不能把外部加载器或其它窗口恢复行为冒充已被本页修复。

## 安装版现在也包含前轮的页面修复

- 实际 CSS 已开时，未接管/未准备不再强制显示 0/7。
- 点击立即反馈；最后点击 3 秒后合并提交；后台未返回仍可调整；失败保留选择不自动循环重试。
- 关闭编辑页不丢最后选择；YMCC 重启只恢复草稿显示，不回放提交、启动或模式切换。
- 唤醒时只读状态不依赖全局 powerReady；读取失败保留运行状态并标待复核。
- 全局恢复门槛晚到不丢掉排队/在途的可见页面只读查询；真正隐藏/离开仍使观察失效。

## 如何保护其他任务的更新

没有直接把新 chunk 的 main 文件名改成旧名。新 Steam chunk 的 25 个 main 导出、27 个 Vue/IPC 导出逐个按实际定义核对后适配；重用安装版的同一套 Vue/IPC runtime，不引入重复桥接器。

主模块除两个 Steam 函数与一条只读 export 外，其余已有导出、定义均不改。部署前校验原 SHA-256，若其它任务改变文件即停止，不覆写。备份落在本轮 Deliverables。部署前后非目标 assets 变化数为 0，保留了监控页面等已安装热补丁。

改动的安装资源：
- C:\SOFT\YeMan\YeManCC\assets\main-BK-n3Nrn.js
- C:\SOFT\YeMan\YeManCC\assets\SteamView-BopaEcuU.js
- C:\SOFT\YeMan\YeManCC\assets\SteamView-BEuw1XKP.css

没有伪造完整包身份；C:\SOFT\YeMan\YeManCC\steam-page-ui-hotfix.json 单独标明热补丁、校验和与备份。

## 验证

- resource-repair-tests.json：7 项通过。
- launch.json：19 项通过。
- steam-ui.json：38 项通过。
- draft.json：26 项通过。
- staged-browser.json：11 项通过。

合计 101 项，类型检查和隔离 Vite 构建通过。浏览器执行的是待部署的实际构建 Steam 模块与安装版 Vue/IPC，原生命令/文件 IO 为内存 fixture；这不是实际重启设备验证。

额外进行了本机 CSS 只读检测：由已补齐的组件读取 fetch_theme_path/get_themes，事务恢复/写配置/启动/刷新方法被禁用，预设元数据隔离至验证目录。

- backendReady=true，steamRunning=true；
- 七项均 installed/configured/backendEnabled=true；
- configuredMatchesBackend=true；主题目录为 C:\Users\DaVe\homebrew\themes。

该检测不启动任何进程，不提交/重载主题。源码、安装资源、实际用户主题三处 61 个文件均与原 manifest 一致。native SHA-256 仍为 E6CACC4B3437A54E3851D1D9E8888AEC602C3E966A679A868F44E4D9E551112A；YeManSteam2.bat 仍为 D788CE2906E67A4E4CCFE3538A1FF51A1BEDECF568C4D979F51CC2D2954E2479。

## 用户加载方式与剩余确认

当前运行中的 WebView 可能仍缓存旧模块。请真正退出 YMCC（不是只隐藏窗口）后重新打开来加载页面补丁；不要重启 Steam。可先直接点击外观开关重新检测，因为运行根组件已经补齐。

如果重开 YMCC 后 Steam 仍被拉到前台/最大化，应继续记录具体窗口触发链；本轮已验证页面初始化不发 Steam 启动/模式操作，未宣称外部加载器/全局窗口恢复原因已经完全解决。
