# YMCC Steam HUD 显示载体源码复核 — 2026-10-10

## 本轮范围与结论

用户要求暂停本机直接验证、继续看别人源码。自该要求起，**没有连接现场 CDP、没有切换应用/发键/截图、没有向 Steam 注入或安装插件**。仅下载固定版本源码、读取本机 Steam JS 字节、在 Node 的隔离 VM 中执行两个原函数的惰性 mock 测试。

**结论：此前的“根页面 DOM + Notification 最低合成请求”未通过 Windows 全屏游戏常驻验收；不能把它当作可发布方案，但也不足以否定所有 Steam 叠加层方案。** 原生子窗口和原生 BrowserView 是两条尚未实测的、有源码依据的候选。

外观仍保持原 H5 方向；数据最终只由 YMCC 统一接口消费。后续测试先用 source=fixture，不能冒称已接真实监控。没有增加 HWiNFO 读取器。

## 保留的实机证据，不能改写为成功

1. 早期窗口轮次 `2077-20261010-133934`：菜单开时，真实游戏截图确实可见圆角 YMCC HUD，用户截图亦确认。证明自定义画面曾进入真实叠加层；**不证明关闭菜单常驻**。
2. 全屏轮次 `2077-fullscreen-alpha-20261010-140451`：游戏设置画面显示全屏，1920×1080设置/3840×2160屏幕几何分别记录；没有 DXGI 独占证据。菜单开时 HUD 被菜单遮挡/淡化，热键关闭后不可见，四档背景 alpha 没有像素验收。
3. `2077-passive-20261010-141755`：前台 HWND 是游戏；实际 MenuStore 从 Main=1 收起为 None=0、menuVisible=false，合成由Overlay2→Notification1，但 HUD 仍无像素。不能再把失败解释为“用户没有切回游戏”或“菜单没有关闭”。
4. `2077-pin-20261010-142427`：对叠加层**根窗口**调用 SetWindowPinned(true)、ShowWindow，仍不可见。这不是原生固定**子窗口**的测试，因此不能否定后者。
5. `2077-active-20261010-142722`：热键激活/收起和真实菜单状态再测，HUD仍不出现，document.hidden持续true。页面/窗口映射是否在原生侧仍为当前活跃目标，需要后续同步测量；不能只凭标题和静态函数可用认定。

每轮原始报告保留，新增 `MANUAL-PIXEL-REVIEW.json` 只记录人工复核，不覆盖原始日志。停止后的清理回执显示 probe/component=0、原 Loader event-map引用和插件列表不变；pin轮次记录取消固定/恢复隐藏。本轮只读取这些已有回执，不在用户使用电脑时补发任何恢复命令。

## 现成插件源码逐项核实

这些提交是本轮读取到的插件数据库引用/固定源码版本，**不宣称是“目前最新发行版”，也不保证当前商店已提供Windows版本**。

| 项目 | 固定提交 | 源码实际做的事 | YMCC可借鉴部分 / 不能当作什么证据 |
|---|---|---|---|
| MagicBlack | `6f61a516f64b65d88e5b30248821a5a3b80fcb28` | 原Steam composition hook；普通全屏div；允许触控时Notification并pointerEvents:none；不允许时Overlay | hook识别和释放可沿用；README场景是Steam Deck OLED，源码没给Windows固定子窗口保证 |
| MangoPeel | `9a7c74a567715f0d18e787745cf2ea6c5d0f3d07` | 查/proc中的mangoapp和MANGOHUD_CONFIGFILE，inotify/写配置替换Linux MangoApp监控布局 | 可参考字段布局和预设；不是Steam React/H5 HUD渲染器，不能直接移植为Windows数据入口 |
| Crosshair | `e603817eb0e97b75bc2d3fe5d74bc4b01ac35c35` | 把custom_text和background_alpha=0等写入MangoHUD配置 | alpha分离值得参考；不是Windows Steam的DOM绘制/常驻证据，不执行其覆盖配置行为 |
| decky-pip | `4cc181bccadda77c38b81e52eb43c61bd176bdc3` | Notification请求 + CreateBrowserView；浏览器有自己的SetVisible/SetBounds/Destroy | **关键：显示载体独立于最低合成请求**；直接版固定绑定GamepadUIMainWindowInstance、854×534和250ms轮询，不照搬到Windows游戏，不复制轮询/外站加载 |
| DeckWebBrowser | `74935db8a2cbb6794e1bc102f95b36c860dc609b` | 注册完整浏览器路由、菜单和页面 | CEF/生命周期可参考；不是游戏画面上菜单关闭后仍存在的HUD证据 |
| DimmerDeck | `080c6862c8d5152d58b18de5496f29ff999a7431` | Linux /proc、xprop、显示LUT | 不是可用的Windows HUD窗口；不引入其亮度或显示修改业务 |
| CSSLoader（已缓存主线参考） | `b1bc683e2d81c95c36265d9078e29ac64abebfb9` | 给Steam UI文档注入样式 | 只能改变已有像素载体的外观；没有凭CSS令隐藏的原生根窗口常驻的证明 |

## 本机 Steam 原生源码暴露出的区别

读取来源：
- `D:\Game\Steam\steamui\library.js`
- `D:\Game\Steam\steamui\chunk~2dcc5aaf7.js`

按模块边界抽取、格式化后逐行核实；仅研究留存，不把Steam内部源代码复制为产品发行代码。模块号仅定位本机这一版本，不写死到正式插件。

### A. 原生固定“笔记/浏览器”的载体是子窗口

模块51582的pin hook通过ref拿到**独立子popup**，对 `ref.current.SteamClient.Overlay.SetWindowPinned(...)` 操作，不是根BrowserWindow。

模块83718的OverlayPopup创建路径：
- `target_browser = 当前context.params.browserInfo`；
- `bPinned`传递给原生popup factory；
- `createPortal`把React children绘制到该popup的element；
- 能隐藏原生标题按钮，能不使用保存尺寸路径。

模块11131的原生CreatePopup把target PID/browser ID和`pinned=true`传给Steam自己的popup创建路径；使用about:blank和Steam内CEF目标，**不需要外置H5服务器/独立桌面浏览器来冒充叠加层**。

其Notification创建flags包含：NotFocusable、TransparentParentWindow、ForceBrowserVisible、NoTaskbarIcon等。注意这些是**窗口创建flags**，与CompositionState的Notification=1不是同一层概念。我们的旧探针只申请后者，没有创建带前者的独立载体。

单有flag也不是成功：Windows是否接受、透明合成与输入穿透是否符合期待，都尚未实测。NotFocusable不等于不捕获鼠标。AlwaysOnTop不等于独占全屏可见。

### B. 原生通知还有 render-only BrowserView

模块48197存在多种通知显示分支，不能把某一个分支推广成所有Steam平台的实现：
- 一条使用独立popup/Notification创建flags；未给游戏目标时明确可以退回DirectHWND_Borderless桌面窗口，**后续探针必须拒绝这个fallback，不能拍到桌面叠加后宣称游戏叠加层成功**。
- 另一条使用NotFocusable的render-only BrowserView，`GetRenderElement()` + portal，且调用自己的`SetVisible()`。

模块73375原生render-only BrowserView：
- 从ownerWindow的BrowserID建立`parentPopupBrowserID`；
- SteamClient.BrowserView.CreatePopup创建载体；
- React渲染目标由它返回，Destroy单独释放视图。

这与decky-pip“合成请求 + 独立BrowserView”的结构相呼应。但用**当前游戏context的ownerWindow**替换主大屏owner，是后续需验证的适配，不是已有Windows成功记录。

## 调整后的最小后续路线

先选真正Steam子载体，不继续无限调根页面的zIndex/opacity，也不试图一直打开交互菜单伪装常驻。

### 候选A（优先）：当前游戏绑定的原生popup

复用当前Steam原popup hook/factory和Portal，不新造窗口管理器：
1. 只在唯一真实游戏Overlay context创建；校验所有app/game/PID/browser/owner身份。旧GetOverlayInstances按运行shortcut appID返回空，浏览器API的appID与运行shortcut ID不同；必须按当前原生browser身份交叉核对，不猜ID相等。
2. 创建一个唯一拥有者的子popup；透明父层、不抢焦点、强制可见flags从当前源码导出识别，不硬编码魔数。
3. 分开测试“创建时bPinned”与“子窗口建立后native pin”，先不要混合两种变量。拒绝桌面DirectHWND fallback。
4. 无框/无标题控件，无外站页面；把同源HUD React组件portal过去。窗口alpha保持1，底板rgba四档，字alpha保持1，避免整个窗淡化造成字体变淡。
5. 不调用DesktopOverlay.SavePinnedWindows、SavePinnedDimensions，不借用笔记/浏览器的业务配置/槽位。
6. 最长180秒。停止/异常/pagehide/app退出/目标更换必须取消自身pin、卸载portal、Close自身子窗口、释放合成与订阅/计时器。不要去关闭根窗口或改变其他已固定笔记。

### 候选B：render-only BrowserView，当前游戏owner

借鉴decky-pip的显示与释放、Steam原生通知的render-target/parentBrowser机制；用当前游戏owner，不绑定主大屏。示例先绘同源React，不加载网页；事件触发更新位置与尺寸，不照搬250ms导航树轮询。透明与原生窗口生命周期仍需实测。

### 暂不采用

- 把H5页面显示在另一个普通置顶Windows窗口，然后谎称Steam叠加层；
- 为“常驻”不断重开Overlay2/不释放游戏输入；
- 拿MangoPeel硬件/Linux后台迁移替代YMCC统一源；
- 为外观改变原生Steam性能监控的实现或配置；
- 为测试自动更改全屏/无边框/分辨率或重启游戏/Steam。

## 本轮离线验证与证据边界

`tools/steam_hud_carrier_source_audit.mjs`：21项通过。
- 固定外部源码与本机Steam源码分别检查；
- 只抽出**原CreatePopup与RenderInternal函数**在VM中运行，window/document/SteamClient全部惰性mock；
- 验证目标PID/browser、creation pin、原尺寸、about:blank、非强制焦点路径；
- 验证原factory确有未指定目标的桌面fallback，后续要额外fail-closed；
- 没有Steam窗口、游戏像素或物理输入，因此不写fullscreen/passthrough pass。

任务输出：`G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\source-review-20261010-143431`。
- `UPSTREAM-SOURCE-LOCK.json`：固定仓库/提交、原始路径/URL、保存文件与SHA256。
- `LOCAL-STEAM-SOURCE-MODULES.json`：本机原JS hash和模块位置。
- `OFFLINE-CARRIER-AUDIT.json` / `.log`：21项测试与惰性创建参数。

网络工具web.run未返回可读取正文；随后通过PowerShell从GitHub第一方仓库下载到任务目录并留hash。Node直接fetch失败也保留为方法说明。没有因为网页工具空结果就宣称源码无法获取，也没有伪造网络引用。

## 恢复实机前的验收清单（当前全部待验收）

1. 当前游戏唯一原生子载体确实建立、绑定当前游戏，而不是主大屏/旧context/桌面窗口。
2. Steam交互菜单关闭后，示例HUD实机截图连续可见。
3. 背景四档/文字清晰、无整屏黑底蒙层、无一整块不透明CEF底色。
4. 用户实际键鼠/手柄能控制游戏；DOM pointerEvents和前台HWND不足以代替。
5. app切换、alt-tab、overlay快捷键、退出和失败均释放自身资源，既有YMCC/Loader不受影响。
6. 当前全屏与之后单独无边框验证；无法测得DXGI独占时明确记录，而不是自动升级成“所有全屏支持”。

**用户未恢复授权前不做上述现场步骤。** 本轮没有正式接数据、没有新增EXE/DLL或第二插件，也没有部署子载体实现。
