# YMCC 监控页纵向调整、RTSS 三模板恢复、原生预览入口 — 2026-10-10

## 用户修正已经落实的界面

- 单列上下顺序：输出选择 → **仅YMCC的预览** → 外观调节 → 自定义数据。取消右侧预览布局和sticky右栏。
- RTSS恢复原来的 **横版监控 / 竖版监控 / 极端简单** 三按钮及大小滑块，实际W/L/J分别映射原W/L/JJ ovl；移除退出RTSS选项，不删除原模板。
- RTSS和Steam原生都不显示预览或YMCC数据开关。Steam原生仍保留位置/详情/三滑块。
- 自定义仍为28独立项，整理为7个有独立底板、标题、分隔和选中计数的分区：帧率流畅度、CPU、GPU、内存显存、网络磁盘、电池续航、附加。
- 1280/720/420宽度均验证所有主段上下排布，数据分区在窄屏回落单列。

## “真实预览”与浏览器示例严格分开

当前用户打开的本地浏览器页仍是**离线示例**，没有 `chrome.webview` 原生YMCC IPC。没有给它伪造原生窗口对象，也没有用 `window.open` 弹个网页冒充YMCC/Steam真实窗口。因此按钮在浏览器内明确禁用并说明原因。

在正式YMCC界面的源码路径上，新增复用**已经存在的 `window.createChild` / `window.closeChild`** 的预览入口；不造新EXE/DLL，不修改native窗口实现。它创建YMCC所有的独立Win32/WebView2桌面子窗口，路由 `/monitor/hud-preview`，和主窗口同源、同数据通道。窗口明确标为**桌面预览，非Steam游戏叠加层**，不能以它替代之前/之后的游戏像素验收。

**本轮未把新UI部署到运行中的YMCC，也未实际创建/拍摄原生子窗口。** 真实窗口当前属于已写代码、离线验证的接入项，不是现场已通过。现有运行WebView也未发现开放的调试端口；没有重启YMCC/Steam/RTSS或切游戏去强行补验收。要实际弹出，需要新版界面加载进YMCC原生壳，而不是当前本地浏览器页。

## 真实数据路径及功耗边界

- 面板在生产模式不再使用fixture；被动订阅现有 `monitorData.ts` 的统一快照，复用TopMonitor/autofloat已经发生的读取。
- 原生窗口不自行读HWiNFO、HAL、历史JSON文件或硬件；只接收主YMCC发来的规范化统一数据。
- 父窗口用原hub登记 `{top,fps}` 展示需求，并在创建时做一次统一快照读取，之后被动收最新快照；没有另起硬件采集者或高频快照轮询。
- 相同会话/代际/序号/载荷时间及状态的重复读取不重复发布；订阅自身不发native请求，释放订阅不停止其它读者。
- 每份源载荷按6秒期限消隐，源缺失显示“—”；老FPS不能复活或提前过期新CPU。当前接口仍缺核心数组/GPU温度/内存等扩展字段，不用示例填进去。
- parent与child使用随机token、递增序号和同源BroadcastChannel，不接受fixture消息。子窗口没有monitor.start/stop/read循环，没有主窗口close命令。
- 创建失败、握手15秒超时、关闭事件、父页面切换/卸载、迟到创建均清理自身窗口/登记/订阅；5分钟独立期限收尾，不停止RTSS、不改锁帧。
- 子窗口沿用现有standalone壳早退：不复制主应用gamepad/FanHost/自动启动/电源生命周期；也不预载全部侧栏页。

## 验证结果与未通过项

- 49项模板/规范化/开关/切换模型检查通过。
- 23项真实Vue设置页+真实协调器的离线浏览器检查通过，包括纵向主段、RTSS W/L/J各自读回、两个不预览分支、七分区、禁用假弹窗、透明度、独立开关、窄屏、保存失败和未知回执拒绝。
- 25项原生预览拥有权/随机通道/握手/超时/释放/统一快照被动订阅检查通过。这些是**inert HWND mock +源码门禁**，actualNativeWindowCreated=false / actualPixelAcceptance=false。
- 隔离目录Vite完整构建通过，安装目录未改。
- 全局vue-tsc受到其它工作产生的 `src/components/SteamAppearanceCard.vue` 对 `packAvailable` 的类型错误影响；未修改不属于本任务的外观页代码，保留 `TYPECHECK.log`，不能宣称全局类型检查通过。
- 既有monitor_data回归前22项通过，之后由于旧R198冻结载荷证据文件不存在而停止；不手抄/伪造该夹具，不把完整回归写成通过。

## 产物

目录：`G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\templates-vertical-20261010`。

浏览器服务沿用本轮已打开的端口，内容更新为该目录 `browser\index.html`。可以刷新当前页继续看界面，不需要开启2077。

`TEMPLATE-MODEL-SELFTEST.json`、`browser\BROWSER-SELFTEST.json`、`NATIVE-PREVIEW-SELFTEST.json`、`TYPECHECK.log`、`NORMAL-WEB-BUILD.log`分别记录上述范围。前一轮templates-20261010证据仍保留，不回改历史结果。