# YMCC Steam HUD 接入探针 — 2026-10-10

日期：2026-10-10，Asia/Hong_Kong。用户授权修改 YMCC Decky 做接入性测试。

## 当前结论（源码复核更新，2026-10-10，Asia/Hong_Kong）

- 早期实际2077画面已有现代圆角HUD像素与用户截图证据，不是外置H5仿页；这只证明菜单打开阶段能绘制。
- 已测试游戏“全屏幕”选项下的窗口，但没有独立DXGI独占证明。**根页面DOM + Notification方式的菜单关闭后常驻仍失败**，四档底板透明度未取得可见HUD像素验收。
- 后续已确认游戏前台、原生MenuStore收起和Notification1，HUD仍不显示；不能再把问题归给用户环境。root pin测试不是子窗口pin测试。
- 用户已要求暂停本机直接验证；之后仅源码/惰性VM复核。21项离线规则通过不等于实机成功。
- 下一候选是当前游戏绑定的Steam原生子popup/render-only BrowserView；未部署/实测，数据统一接口尚未接通。详情见本目录的STEAM-HUD-CARRIER-SOURCE-REVIEW-20261010.md。

## 阶段一：主大屏 DOM/hook 测试的结论边界

**实际 Windows Steam 的 DOM/React 上下文与 Notification 合成请求接入通过；游戏画面像素、关闭菜单后常驻、无边框和独占全屏均未通过/未测试。**

本轮没有用独立 H5 或浏览器仿页代替 Steam 测试。使用现场已经运行的 Steam、YMCC 和 Loader；没有重启或替换安装版，没有新建游戏快捷方式、启动联网/反作弊游戏，没有更改硬件与原业务设置。

`REAL-STEAM-INTEGRATION.json` 中 `pass` 仅代表此文件的 DOM/hook 测试范围，不是整个监控产品可行性通过。`visualVerified=false`、`gameTested=false`、`fullscreenTested=false`、`uniformDataConnected=false` 必须一起读取。

## 来源与复用

MagicBlack 商店 2.0.0 固定提交：`6f61a516f64b65d88e5b30248821a5a3b80fcb28`（GPL-3.0）。
参考文件：`src/uiComposition.tsx`、`src/backgrounds/BlackBackground.tsx`。
采用其 `AddMinimumCompositionStateRequest` / `ChangeMinimumCompositionStateRequest` / `RemoveMinimumCompositionStateRequest` hook 签名和 Notification 合成思路，未复制黑屏、音量、输入捕获业务。
源码地址： https://github.com/steam3d/MagicBlackDecky/tree/6f61a516f64b65d88e5b30248821a5a3b80fcb28

探针通过模块导出签名查找唯一候选，模块号和导出名仅作为诊断记录，不写死。UI mode 枚举也由对应上下文模块识别。
本机实测找到 composition `55489/Bx`、context `33572/IB`；Full=1、Overlay=4、Notification=1。

## 改动范围

- `decky-plugin/src/steamHudProbeState.ts`：有限期试验配置、数据校验、递增序号和小型事件记录。
- `decky-plugin/src/steamHudProbe.tsx`：按需挂载只读 HUD；真实 context 内调用合成 hook；无按钮/焦点节点，pointerEvents=none；释放自身合成登记。
- `decky-plugin/src/index.tsx`：测试构建条件接线及卸载释放；保留原控制台。
- `tools/build-decky-sidebar-plugin.mjs`：`--hud-probe` 才包括探针；正常构建 define=false，探针被移除。
- 新增三项测试工具及临时入口；既有 presentation 测试只加默认构建禁用探针的 mock。

没有改 native/main.cpp、monitorData、mirrorHost/Relay、原产品部署 JS、Loader EXE。
未改变监控与控制配置协议/版本；没有添加第二硬件采集者、Python backend、运行时 EXE/DLL。

## 安全与数据

默认 build 不包含探针运行时代码；显式开始必须带 integration-test、唯一 runId、明确 scope 和最长180秒期限。无有效/唯一 hook 时拒绝启动。

- overlay-only：只在真实 Overlay mode 可挂载 Lease；主大屏 context 不显示且不申请 Notification。
- main-window-test：明确标“主大屏挂载测试 · 非游戏验收”，只用来验证现有渲染 context 的接入。
- 尚未连接统一监控导出。因此当前更新入口只接受明确 `fixture` 包，拒绝外部伪称 `ymcc-uniform`。
- 未连接显示 `—` 和“实时数据未连接”；fixture 显示“示例数据”；6秒失效后置空。
- 失效只是刷新显示，不自造采集时间戳/序号。
- 不读 HWiNFO、文件或硬件，不向原业务执行命令，不混用 `frames.fps` 限帧设置。
- 不从 SharedJSContext 直接调用 Window.SetComposition；由真实 context 的原 hook 负责合成。

## 验证

输出根：`G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\20261010-130654`。

- 专项状态/hook规则22项通过，严格 TypeScript 检查通过。
- 原 client31、presentation27、QAM visibility15、placement12、global10、frame/touchpad28：共123项通过。
- 默认和测试插件分别构建到任务输出，未部署；原产品 JS SHA256 不变。
- 实际 Steam DOM/hook六项通过：scope拒绝错误context、未接数据不伪造、fixture递增更新、过期显示、显式停止、到期释放。
- Notification 请求登记0→1→0，最终监听0、期限/新鲜度timer0、探针global和组件0。
- DOM activeElement tag/role前后相同，**不是物理键盘/手柄或游戏输入验证**。
- 最终原插件列表及Loader事件映射引用不变。

## 失败与未确认项

早期 CDP Page.captureScreenshot 超时，另一次调用期间大屏 target 重建、探针随pagehide释放。不能认定重建必然由截图引起；完整初次/第二次报告分别保存。
改用Win32 PrintWindow捕获，现场Steam标题窗口只有157×25可见，1440×756内容窗口标为隐藏，安全捕获拒绝。因此**没有可交付的真实显示截图**，不生成仿页补证。

早期临时入口曾再次连接同名 @decky/api，这会替换Loader事件映射；发现后改为直接复用现场Loader的RouterHook，不再建立第二SDK连接。最终测试校验映射引用不变；不能把最终不变解释为所有早期尝试都完全无副作用。

尚未进入游戏，因此合成最终状态在主页仍为Opaque=3。Notification计数增加仅证明最低请求被登记，不能证明游戏上方的像素已绘制。

## 资源与下一关

同源重建基线JS34,928字节；正常构建34,943字节（+15），测试构建42,293字节（+7,350相对正常）。新增交付EXE/DLL均0；试验尚非正式发布包。

下一关：选定安全的无反作弊测试程序/游戏，在真正OverlayGamepadUI context中挂载，分别验证无边框/独占全屏、关闭菜单后常驻、物理输入和全路径释放。通过后才接YMCC统一监控数据导出；不得用主大屏测试替代这项。


## 阶段二：用户已开启的 2077 实机测试

输出：`G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\2077-20261010-133934`。

- `2077-PROBE-OPEN-UNCONNECTED.png`：实际 game-client 的 CopyFromScreen 像素可见圆角 YMCC HUD；数据为“实时数据未连接 / —”。这是绘制证明，不是实时数据接通证明。
- `2077-PROBE-CLOSED-UNCONNECTED.png`、`2077-PROBE-CLOSED-FIXTURE.png`：关闭热键后只有原生 Steam 监控，自定义 HUD 不可见。后者 DOM 已更新示例数据但画面仍不显示。
- 未主动更改 Steam 原生监控设置。打开/关闭 Steam 叠加层后两者出现变化，不等于实施了原生/自定义监控切换功能。
- Root 的真实 Overlay mode=4，Notification request=1，GetCompositionState=1；关闭后 document.hidden=true。计数和状态不能推翻截图失败。
- 用户当时描述为窗口模式，采集 client 为1920×1080；没有独立核实运行期交换链模式，不应自动分类为普通窗口或无边框。
- 本次 harness 在过期数据截图前遭到前台激活失败并安全中止，没有向错误窗口发键或提供仿图补证。报告保留 failure。清理确认 probe/component=0，插件列表及 Loader event-map 引用不变，未关闭游戏。

## 阶段三：全屏透明度测试准备（不是全屏验收）

准备输出：`G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\alpha-prep-20261010-135225`。

### 示例数据与外观

- 使用原 H5 预览里的 FPS60、CPU18.0W / 67°C、GPU86%，明确 source=fixture 和“示例数据”。不伪称 YMCC 统一接口已接通。
- API 新增 `setAppearance(runId,{backgroundAlpha,textAlpha})`，限制为当前未到期 owner，两项均须为0..1有限数。
- 背景不透明度0%、35%、65%、90%，文字100%。背景 alpha 与文字 alpha 分开施加，不用整个 HUD 的 opacity 淡化文字；父层若有额外淡化会记录以便核查。
- 0%只表示 HUD 底板透明；不据此宣称整个 Steam 浏览器合成通道透明。是否黑底、菜单蒙层、字是否仍清晰，必须看真实游戏截图。
- 外观变更不刷新采集时间/序号，不重新申请 Notification，不延长170秒测试期限。停用恢复默认外观，期限/新鲜度计时器和订阅全部释放。

### 准备验证

- 专项状态规则由22项增至34项通过；strict TypeScript通过。
- 现场 Steam/Loader 的 preflight 通过：唯一原 hook/context/router可用，不挂载组件，不发键；最终全清理，原 event-map引用与插件列表不变。
- 现场主大屏 DOM/hook回归7步通过，实际 Chromium computed background 分档变化、container opacity=1、文字alpha=1、采集seq与合成登记不变。本轮明确 `--no-capture`，不得将其解释为像素或全屏通过。
- 原 client/presentation/visibility/placement/global/frame-touchpad六组回归通过。默认构建仍排除HUD探针，测试构建只写任务输出，安装版JS哈希不变；新增运行时EXE/DLL=0。

### 下一轮执行

`tools/steam_hud_probe_2077_alpha.mjs --run <新任务目录>`（需先在该目录运行专项selftest以生成同源runtime）。

前提：用户自行开启全屏2077，Steam叠加层先关闭。脚本匹配运行游戏ID与实际Overlay browser身份，拒绝含糊匹配；记录已保存WindowMode/Resolution和窗口/显示器几何，但均不冒充DXGI独占证据。

采集 baseline → 四档各热键打开/关闭截图 → stop后截图。每帧都保留原生状态、DOM、父级opacity、composition cache、来源/序号、真实game-client坐标与路径。菜单状态目前按热键序列记录，未独立读取原生活跃状态；截图仍需人工逐张核验。结束移除探针，保留所有失败，不关闭游戏。

重点验收：关闭Steam菜单后的可见性、亮暗背景透出、是否出现整屏黑底或蒙层、文字清晰度、原生监控共存和实际游戏输入。尚未通过这些项目；本轮“准备好”只代表测试工具就绪。

## 阶段四：全屏失效复核与直接验证暂停

后续全屏、菜单状态、root pin、hotkey激活轮次均保留真实截图。`2077-passive-20261010-141755`已确认游戏前台、MenuStore收起、Notification1，但HUD依然无像素；**不能归因用户没调好环境**。root SetWindowPinned/ShowWindow亦未成功，不能当作子popup路径已测。

用户随后要求暂停本机直接验证、允许继续源码分析。已遵守：之后没有CDP、切应用、发键、截图或注入。现成MagicBlack/MangoPeel/Crosshair/PiP/DimmerDeck/浏览器源码与本机Steam原popup/BrowserView源码复核，得到“最低Composition请求 ≠ 独立游戏显示载体”的区别。

21项静态/惰性VM检查通过，但不是实机pass。详情：`G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\docs\STEAM-HUD-CARRIER-SOURCE-REVIEW-20261010.md`。下一轮候选是**原生游戏绑定子popup**，或**当前game-owner的render-only BrowserView**；仍用YMCC统一数据源方向、示例先测、保持独立alpha，不部署外置置顶H5冒充Steam。需要用户恢复现场测试授权才会动桌面。
