# YMCC Steam 原生子窗口探针：2077 实测准备 — 2026-10-10

## 状态

**更新：2026-10-10 17:18（+08:00），修订2在真实 Steam / 2077 完成首次显示与四档透明度测试；用户确认“目视到成功了”，随后人工逐张核验截图。**

已通过的范围只有当前会话设置页上的像素可见、四档底板、菜单收起后的短时持显和停止清理。真实YMCC数据、物理输入、长时间稳定性、无边框及DXGI独占全屏仍未验收。机器原始报告保留其待人工核验标记，不回改失败或原始布尔值；追加独立像素核验记录。下文准备阶段说明保留历史范围，后续实测见文末。

最初准备阶段只做源码修改、构建、离线/惰性mock测试和读取Steam源文件，没有现场CDP、切应用、发键、截图、创建真实子窗口或替换安装版。后续用户恢复桌面测试授权，才执行文末两轮实测；未替换安装版。用户选择的“角落信息块”是本版唯一子窗口布局，数据明确fixture，不能冒称真实YMCC监控已接通。

## 与上次的实质区别

- 上次：在Steam根文档挂HUD，并请求Composition.Notification；根窗口原生隐藏后没有像素。
- 本次：复用当前Steam**原generic popup hook**创建一个游戏绑定、唯一拥有者的子窗口；React Portal把同源HUD放进去。
- 原popup flag factory识别NotFocusable/TransparentParentWindow/ForceBrowserVisible；另外初始Hidden，等portal提交、透明底色和窗口alpha=1设置完成后，才对子窗口ShowWindow。
- creation pin与none pin分开成两种测试参数，不再固定根窗口，也不混合“创建时pin”和“创建后再次pin”两个变量。
- 不调用BringToFront抢占OS焦点、不改Steam原生性能监控设置，不注入桌面fallback，不创建外置网页浏览器。

## 所属源与复用

保留MagicBlack的composition hook定位/释放机制与原H5角落结构，原Steam popup hook/flag factory按唯一函数签名识别，使用其factory和portal，不复制Steam源实现进产品。

来源复核见同目录 `STEAM-HUD-CARRIER-SOURCE-REVIEW-20261010.md`；开发期原厂字节只在任务目录留存。生产代码不硬编码模块号、导出名或创建flags魔数。本机当前源字节hash与上一轮快照相同，原hook和flag factory签名在惰性VM中匹配通过；准备阶段没有实机执行；修订2随后复用此原native hook完成实测。

## 代码范围

- `decky-plugin/src/steamHudNativeCarrier.ts`：签名发现、原生窗口计划、游戏owner/不同ID校验、子窗口独立期限、透明根层、释放。
- `decky-plugin/src/steamHudProbeState.ts`：native-popup配置、深复制gameBinding，保持root-dom兼容。
- `decky-plugin/src/steamHudProbe.tsx`：将显示内容与lease分离；native carrier复用原hook＋Portal；nativeChildren诊断；停止先释放子资源再卸载原hook。
- `decky-plugin/src/index.tsx`、`tools/fixtures/steam_hud_probe_entry.ts`：仅probe构建传入现有SP_REACTDOM；默认构建不含探针。
- 新增native carrier单元、实际TSX惰性渲染、原厂源签名检查和2077实测脚本。
- `tools/steam_hud_probe_game_window.ps1`只新增ExpectedGamePid约束，以免游戏重启后把按键/截图交给另一进程；准备阶段仅编译/parse，后续授权实测才调用。

没有第二Decky插件，没有新EXE/DLL，没有硬件采集者，没有设置面板或业务设置协议变更。

## 游戏身份不是“所有ID相等”

配置分别绑定：运行appId、64位gameId字符串、overlay renderer PID/browserId/type/nativeAppId、context browserGameId、**owner CEF browserId**和原文档标题。

Windows shortcut appId与overlay nativeAppId已在早期记录中不同；owner CEF browserId也不假设等于overlay renderer browserId。每种ID从自己的第一方context读取，保持稳定，并用当前运行游戏关系交叉校验。禁止把SharedJSContext或主大屏误当游戏绑定/生命周期owner，禁止绑定标题相似的另一个游戏或走desktop fallback。原popup工厂的默认SharedJSContext opener与游戏生命周期owner是不同概念；修订2保留默认opener，通过target_browser定向当前游戏。参数/getter异常或能力缺失均拒绝，不随便补一个ID继续。

## 生命周期与安全边界

- 默认关闭；只能明确integration-test、overlay-only、有效gameBinding开始。
- 正式构建define=false删去探针；测试build只写任务输出，安装版JS本轮hash未变。
- 单轮170秒，硬上限180秒；父探针期限与子window-local期限独立，脚本被中断时仍有自动释放。
- 停止、child unload、owner unload、观察到owner身份更换、启动异常，释放自身pin、事件/计时器和portal；原popup hook负责常规Close。
- owner visibilitychange、context重渲染及每次fixture更新校验身份；不是常驻扫描器。不宣称已经验证了所有可能的游戏退出/OS焦点竞态。
- 不保存尺寸/固定状态，不占用用户笔记或浏览器的槽位，不修改根页面CSS和其他窗口。
- NotFocusable、SetKeyFocus(false)与HTML pointerEvents:none只是请求/代码约束；**不是实际键鼠/手柄穿透证明**。
- `nativeChildren[].visibleRequested`是请求记录；DOM存在、hidden=false、interface返回不能自动设置visual/passive pass。

## 外观与数据

同源角落信息块；FPS60、CPU18.0W / 67°C、GPU86%示例数据。底板0%、35%、65%、90%不透明度，文字独立alpha=1。窗口alpha=1，避免整个窗口一起淡化。

收到fixture后6秒过期置空。外观变更不改采集seq/时间，不新建子窗口、不重新请求合成、不延长期限。接入真实YMCC unified source是之后独立工作，当前source=ymcc-uniform仍拒绝。

## 本轮验证

输出：`G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\native-ready-20261010-164125`。

- carrier身份/flags/不同ID/拥有权/透明背景/期限/释放：**45项通过**。
- 精确production TSX，惰性React hooks/native APIs：**8项通过**，验证只建game child、portal先于show、四档不重建、stop释放原hook、root路径兼容。
- 旧probe状态/来源/失效/alpha测试：**35项通过**。
- 本机原JS hash、真实函数签名/纯flag factory检查通过。
- strict TypeScript通过；2077脚本node --check；Win32辅助脚本PowerShell parse+C# compile通过。
- 既有client31、presentation30、visibility15、placement12、global15、frame/touchpad28：**131项通过**。这些是当前工作区已有测试，不宣称全是本探针新增。
- 正常JS35,860字节，测试JS55,383字节（+19,523）；新增运行时EXE/DLL=0。正常bundle检查无probe global/child/HUD标记；安装版JS before/after hash一致。

上述准备阶段native/React测试均是惰性mock，当时没有运行Chromium/Steam窗口，不能证明像素、常驻、透明度和输入；后续实机证据单列在文末。这些bundle大小/资源记录来自初始准备版，不是修订2新测的压缩构建结果。

## 实测入口与顺序

脚本：`tools/steam_hud_native_child_2077.mjs`。仅带`--run`才允许读取现场Steam；没有该参数立即拒绝。它要求准备manifest和runtime SHA256匹配，使用全新`2077-native-*`目录保存失败和截图，不覆盖旧报告。

原定流程（用户已于后续明确桌面可用，实际执行结果见文末）：使用已打开2077，不自动启动/关闭游戏、不改分辨率或显示模式。流程：
1. 读取当前游戏/实际overlay身份、保存显示选项（不冒充DXGI独占证明）。
2. 必要时通过原MenuStore收起当前运行游戏侧菜单，确认menuVisible=false。
3. 真实game-client baseline截图，挂载native child。
4. 四档fixture HUD截图、短稳定性截图；所有帧同时存native菜单/合成状态、child DOM与窗口坐标。
5. stop后截图；移除probe，验证原插件列表/event-map引用与自身popup登记清空。

按顺序先跑creation pin；必要时另起输出目录跑none pin对照。发生异常保留原报告，清理自身，不重启Steam/游戏补证。

准备runtime：`G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\native-ready-20261010-164125\steam-hud-probe-runtime.js`。

准备阶段尚未执行以上步骤。后续显示与透明度已初步通过，但游戏输入不受阻仍需实测，因此不写完整载体pass；无边框与可确认的独占全屏仍需分别验证。

## 后续实测：修订1失败，修订2显示成功

### 修订1（17:11）：创建失败，保留原失败记录

准备版额外传入实际游戏根窗口为 `owner_window`。游戏身份/能力校验通过，但原popup工厂没有创建window或element，child-mount超时；没有window触发unload，原PopupManager残留自身空descriptor，原报告同时记录cleanup失败。检查之后只回收名字和对象身份均匹配、window/element均不存在的本探针descriptor，不清理其他popup。

失败及精确回收证据：`G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\2077-native-creation-20261010-171139` 中的 `2077-NATIVE-CHILD-INTEGRATION.json`、`NATIVE-POST-FAILURE-INSPECT.json`、`NATIVE-ORPHAN-RECOVERY.json`。这些记录未回改为成功。

### 修订2（17:18）：保持原factory默认opener

- 不再覆盖 `owner_window`，保持原OverlayPopup从SharedJSContext词法opener打开的方式；`target_browser`仍严格指向实际游戏overlay。
- 实际游戏owner单独用于身份检查与生命周期，不将它与factory opener混为一谈，也不要求renderer browser ID等于owner CEF browser ID。
- 使用原hook、原flag factory、creation pin和Portal；透明HTML/body/target、窗口alpha=1、SetKeyFocus(false)，内容提交后ShowWindow。没有外置桌面H5替代。
- 为上述“window未创建”的自身descriptor补足窄范围异常释放；常规Close仍由原hook处理。

修订2准备输出：`G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\native-ready-20261010-171826-opener`。48项carrier、8项精确TSX惰性渲染、35项状态测试、strict TypeScript与本机原厂签名检查通过；manifest中的初始正常构建、131项既有回归和资源测量保持历史来源，不当作修订2新测。

### 像素核验与用户目视确认

实测输出：`G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\2077-native-opener-20261010-171827`。

- 原机器报告：`2077-NATIVE-CHILD-INTEGRATION.json`，SHA256 `7EDAB6F56D8B38A2E936CA266C6F2DEDABB28C9BD49978071B2E61741A6DED0A`，保持原样。
- 后补人工记录：`2077-NATIVE-PIXEL-REVIEW.json`，逐帧路径/hash、人工观察、用户原话和未验收项分开保存；没有重新运行或操作桌面。
- 全部七张真实game-client CopyFromScreen截图已人工检查：baseline、四档alpha、短时stability、after-stop。HUD在2077设置页可见；底板0%、35%、65%、90%有清楚差异，文字没有随底板一起淡化。在该场景未见新增整屏黑底或超出卡片的大块CEF底色。亮暗游玩场景尚未覆盖。
- 四档和stability帧均记录MenuStore menuVisible=false / menuInteractable=false、overlay根窗口rootHidden=true、Composition=1，只有一个未关闭/未隐藏的本探针child；额外1.8秒后的截图仍可见。这只证明短时菜单收起持显，不是长时间验收。
- stop截图HUD消失；probe/component不存在，插件列表仍为ymcc-sidebar、eventMap引用未变，remainingOwnedPopups=[]。本轮没有关闭2077。
- 用户原话：“目视到成功了”。作为独立现场可见证据记录，不扩展成输入或所有全屏成功。

### 仍未验收与下一步

当前source仍是fixture：FPS60 / CPU18.0W、67°C / GPU86%，明确“示例数据”。没有接通YMCC真实统一快照，更没有直接消费HWiNFO。控制面板仍留在YMCC；不新增Decky控制台或硬件采集进程。

保存的WindowMode=Fullscreen / Resolution=1920x1080与截图期间3840×2160铺满显示器，都不能证明DXGI独占。无边框也未单独跑。实际键鼠/手柄、长时间持显、Steam菜单开关/Alt-Tab/退出路径仍需后续实测；NotFocusable、CSS pointer-events和前台HWND不替代物理输入。

实拍测试块占屏偏大，低alpha会与游戏文字重叠。下一步先校正分辨率/Steam缩放下的卡片尺寸与位置，并验真实游戏输入；再补稳定性及显示模式覆盖，最后接YMCC统一接口。显示已初步成功，不等于已可正式发布。
