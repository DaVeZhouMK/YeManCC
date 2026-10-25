# YMCC 双承载正式输出：执行检查点（2026-10-10）

## 完整目标（没有缩减）

1. 先落地 Steam 插件承载，作为游戏全屏输出路线。
2. 再制作不依赖 Steam 的窗口／无边框原生输出，独占全屏不作首版保证。
3. 两路复用相同 H5 外观、数据和设置，用户观看感觉几乎一致。
4. 两路必须鼠标穿透；切换不能重叠输出，失效必须清理。
5. 不重新跑 CPU 测量／性能门槛；保持 YMCC 统一数据、单路采集和最多 1Hz 链路。

**目标仍 active，不是完成报告。**

## 本轮已写入的正式代码

- `monitorHudProtocol.ts`：有时限、会话、序号、操作号、当前 Steam 游戏绑定；拒绝 fixture、过期及错误身份。隐藏 ACK 增加 `hiddenConfirmed`，不能用“JS Map 删除了”冒充原生窗口关闭。
- `monitorHudRenderer.ts` + `monitor_hud_vue_build.mjs`：在既有 Decky 构建里复用真正的 `MonitorHudTemplate.vue`，不再保留探针的简化假外观。SFC CSS 仅放入拥有的子文档；Vue 使用独立 React Portal 容器，避免两套框架互相删除 DOM。
- `steamMonitorHud.tsx`：复用已审阅的商店插件思路、原 Steam composition/popup hook 与 Decky RouterHook。严格游戏绑定，拒绝桌面 fallback；窗口归属、数据 lease、关闭证据、父源重载会话退休及尺寸变化有界处理。
- 统一逻辑像素目标：Steam 要求本机已存在的 IgnoreSteamDisplayScale/DisableDPIScale/NoScalePosition/NoScaleSize flags，禁用 ApplyBrowserScaleToDimensions；**效果仍需实际游戏/不同 DPI 验证**，不能仅凭 flags 宣称像素一致。
- `steamHudRelay.ts` / `monitorHudHost.ts`：在主 YMCC 注册正式适配器；被动订阅统一快照和真实下发 TDP 状态，第一消费者取得同一采集需求。1000ms 数据/lease tick 没有硬件读操作。失联、错误游戏、超时不能假装已隐藏。
- 复用既有认证 WebSocket，未增加另一套后台/硬件采集；原生 broker 只新增 `hud.status` / `hud.ack` 显示命令白名单及 `hudProtocol:1` bootstrap。
- MirrorClient 后台 HUD 连接不主动读取风扇／游戏控制快照；QAM 显示才订阅业务快照，隐藏发送 `snapshot subscribe:false`。复用连接但不允许隐藏侧栏发业务操作。`hud-ready` 恢复主源重载后的状态握手。
- 原 YMCC 原生预览仍是桌面预览，不是第二阶段的游戏 HUD；不把它包装成已经支持无边框。
- 保留源码中其他任务的脏改动，未 reset 文件、重启/替换安装版、启动游戏或做硬件写入。

## 本轮证据及范围

目录：`G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\product-output-20261010`

- 正式 relay/protocol/生命周期：28 cases。
- HUD 单连接、隐藏业务订阅暂停、只读边界：11 cases。
- 相同生产 Vue renderer 在两份拥有的浏览器文档：9 cases，角落／胶囊／横板、全 29 项、100%/140%、0/35/65/86/90% 底板的文本、尺寸、样式及截图 SHA256 一致。**只是相同 DPI 浏览器的合成数据，不是 Steam/WebView2 真机像素验收。**
- 既有 MirrorClient 31、readonly host/relay 32、native carrier 48 cases 通过。
- 在当前真实 Steam/Decky 中仅挂载正式 Root、发现全部原始 hook/flags 能力并清理，通过。transport 明确 inert，未创建游戏/native popup，插件列表、popup 列表、SDK event map 与前后相同。`platform/STEAM-PLATFORM-SELFTEST.json`。
- 全项目 vue-tsc、任务目录 Vite、完整 native 编译链接、插件构建字节 check 通过。
- legacy mutation suite 未通过：历史 mainline15 FanView 来源保留断言与当前原先已有脏源码不同。未改历史预图，也未恢复 FanView。不能宣称该整套通过。
- **没有跑任何 CPU benchmark。**

## 还必须执行的工作

### Steam 本身未验收的项

- 正式 Popup / React / Vue 整条生命周期的真实承载验证，真实 YMCC 快照输出。
- 2077 游戏全屏设置模式实际显示、透明度与鼠标操作穿透。当前未运行 2077；先完成其余安全实现，不因无游戏而缩减目标。
- 浏览器硬件兼容、屏幕尺寸/DPI及正常窗口定位。不能把窗口 flags 或 H5 pointer-events:none 当作鼠标穿透证明。

### 第二阶段：独立原生 HUD（未制作）

- 新建专用 `monitor/hud-output` 轻量子路由；现有 App/main standalone 白名单增加该路由，绝不启动第二套监控/手柄/硬件控制。
- 复用 WebView2 env + 同一 renderer；原生 WS_POPUP、工具窗、NOACTIVATE、LAYERED/TRANSPARENT，透明根，鼠标/键盘不转发至 HUD。需要真正 WebView2 子窗的跨进程命中/点击穿透证据，不能用空 HWND 或 DOM 测试替代。
- **独立 HWND 不要把 g_hwnd 设为 Win32 owner**，否则主 YMCC 最小化会自动隐藏 owned popup；用内部生命周期所有权管理，主进程退出清理所有该类窗口。
- 目标来自已有 `getPolicyGame()/subscribePolicyGameStatus`（不是新增进程/传感器轮询）；进程出生身份必须严格验证。现有 `focusTargetIdentityMatches` 对 protected process 有“查询失败返回 true”的恢复容错，HUD 不能照搬，查询失败应拒绝归属。
- 目标仅前台／未最小化时显示，切桌面/YMCC时隐藏。WinEvent 前台/位置/销毁通知过滤目标 HWND，并在 UI 线程合并更新；无每帧 loop。
- WebView2 默认背景 alpha=0，HUD alpha 仍在 CSS；固定逻辑像素/RasterizationScale 策略与 Steam 保持一致，需要实际验证。
- nonce/id 约束布局、关闭与 child-ready RPC，关闭只触及自己窗口；主源消失有 child 自己的 deadman。

### 双承载与互斥控制（未完成）

- 顶层 Steam原生/YMCC自绘/RTSS 三选一不变，YMCC 内优先当前游戏就绪 Steam，否则窗口/无边框桌面层。
- 共用 ONE 统一样本源，给 Steam/桌面需求计数，最后消费者解除；不要两个 acquire 把彼此 epoch 冲掉。
- 原 native `g_steamSettingsObserver.steamPresence()` 已有三态缓存；`deckySidebar.get` 输出 `steamKnown/steamRunning` 是纯快照。可以用于确认 Steam 不存在，不新增 WMI/扫描。**unknown 不等于 stopped**。
- 现有 coordinator 要允许 confirmed-absent Steam 跳过该路 hide ACK，但 Steam 未确认仍不能视为 absent。
- 现有 coordinator 只在 `caps.ymcc` 为 true 时隐藏自绘，需要增加“存在/可能存在自绘 owner，能力未就绪也要 hide”的控制，否则连接断开后的旧窗口可能与 RTSS 重叠。
- 主源重载没有 peer 时也不能贸然认为旧 Steam child 不存在；结合 native Steam absence 或可信 peer closed proof 保持 unknown。此项在发布前必须补齐。

## 发布/完成条件

不能把以上构建或窄检查升级为完整支持。两路功能、统一数据、实际全屏 Steam、独立窗口/无边框、鼠标穿透、互斥、失效清理及安装/运行范围都要有对应当前证据；不足则保持 goal active。
