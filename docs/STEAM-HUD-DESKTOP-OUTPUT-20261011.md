# YMCC 双承载：独立原生 HUD 与后台快照时钟检查点

## 状态与完整目标

目标保持 **active / 未完成**。不缩减用户要求：Steam 插件是全屏游戏输出路线；不依赖 Steam 的路线使用几乎相同的 H5 外观，首版面向窗口／无边框，独占全屏不作保证；两路都必须鼠标穿透。后续不跑 CPU 测量测试。

已完成本轮源码、任务构建及以下范围的验证，**没有替换安装版，没有启动／切换 2077，没有做硬件写入**。

## 独立原生承载

- `native/monitor_desktop_hud.h`：真正 WebView2 CompositionController + DirectComposition visual，工具／置顶／不激活／层叠透明窗口。窗口没有主 YMCC 的 Win32 owner，主窗口最小化不会通过 owner 链自动隐藏它。
- 为什么不是直接 windowed WebView：第一版在 layered HWND 中握手和鼠标路由均通过，但截图是黑块，**被像素审计拒绝**。换用 composition 后需要显式 IsVisible/Commit，且仅 NOREDIRECTIONBITMAP+HTTRANSPARENT 时虽然 WindowFromPoint 指向目标，真实系统点击仍被吃掉，**也被拒绝**。最终 composition + LAYERED + TRANSPARENT 同时满足真实 H5 像素和系统级点击路由。
- 复用 `MonitorHudTemplate.vue` / `monitorHudRenderer.ts`，不绘制另一套伪装页面。`hud-output.html` 是独立轻量多页面入口，没有主 App/router/scheduler/监控采集启动。
- 专用 receiver 仅允许带对应 id/nonce 的 ready、layout、close；不调用主 `ipc_dispatch`，拒绝其它命令／导航／新窗口。数据只接受统一接口形状，fixture 在正式通道拒绝。
- 主 RPC 创建目标由已有 YMCC 游戏 valve 校验；native 再验证 PID、出生时间、同 Windows session 和 process handle 存活。没有第二路游戏进程轮询。
- WinEvent 监听前台、位置、最小化／恢复及销毁，合并至 UI 线程，只在目标位于前台且未最小化时显示；不抢焦点，不转发／合成游戏输入。
- native 独立 6s 数据 lease：主页面卡住也会隐藏；关闭只处理自己的 HWND/WebView/hooks。主 renderer 重载时不能以 JS Map 为空当作已关闭，需向 native 查询并关闭其拥有的旧 descriptor。
- 六个位置沿用现有 Steam/YMCC 数值：1 左上、5 上中、2 右上、3 右下、6 下中、4 左下。不是直接用 1..6 当 2×3 网格顺序。
- bar 切回 corner 时可能有旧 ResizeObserver layout 回执晚到；旧 seq 布局忽略，不把正常的竞态当作失败并误关 HWND。

## 两路统一与互斥

- 顶层 Steam原生 / YMCC自绘 / RTSS 三选一不变。自绘内部优先当前游戏就绪的 Steam 承载，否则选原生 desktop。
- 共用一个 `monitorHudSampleSource`：一个已有 native demand、一个统一 snapshot 订阅、一个真实下发 TDP 订阅，最后消费者释放。不是两个采集器。
- 迟到的初始 read 不能覆盖后来的被动 snapshot，源 epoch 与 revision 均有校验。
- `monitorHudCarrierRouter` 关闭两路旧承载并等待确认，再开启目的地；能力不可用不等于“旧窗口不存在”。uncertain 和 confirmed shown 在 UI/runtime 分开。
- Steam 缓存状态 unknown 不当成 stopped；可靠 absent 才跳过 Steam 的实时隐藏操作。原生输出不能在 Steam absent 时启用。
- TDP 仍为当前电源侧后端确认下发的值，非保存最大值，非 HWiNFO 包功耗，**不是持续硬件回读**。

## 最小化时的数据链接

原仅被动订阅顶部监控的已读快照不足以保证后台更新；主窗口最小化后，UI 轮询会暂停，浏览器定时器也可能节流。

- 增加 native 1000ms `monitor.hudPulse`，只执行已有 `monitorSnapshotServeFn()` 缓存拷贝，没有硬件/传感器/process enumeration 调用。
- native clock 有 owner nonce 和 7s ACK lease，显示所有者释放或主 renderer 消失后停止，不永久驻留空转。
- Publication 走现有统一 parser、session/generation/seq admission；旧/未知 publication 忽略，当前 unavailable 清空。不用 push 去确认控制硬件写入。
- display/TDP gates 可由 native heartbeat advance，保持 1000ms latest-only；两路 producer 在 native-clock 模式不设浏览器 steady refresh timer。
- **十个压制所有浏览器计时器回调的函数级测试通过；真正主 YMCC 最小化的长时间集成实测仍未完成。** 不把这个窄测试升级为整套后台验收。

## 本轮实际证据

目录：`G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\desktop-output-20261011`

- `native-test/NATIVE-DESKTOP-SELFTEST.json`：11 项实际 native/WebView2 隔离测试通过。真实 H5 像素、单独进程窗口下的 WindowFromPoint、SendInput 系统点击一次到达／焦点保留、三套模板及六位置、最小化／恢复、nonce 保护、native TTL、关闭清理。数据边界明确 synthetic，目标是测试程序，不是游戏；不是手动鼠标硬件测试。
- 截图 `native-test/native-bar-position-6.png`、`native-capsule-position-2.png` 等是实际 native 屏幕截图，不是浏览器页面截图。底部 TEST ONLY 标签来自测试目标程序，数值为合成测试值。
- 失败历史保存于 `native-test/attempt*`；`attempt2-layered-black/PIXEL-AUDIT.json` 显式否定旧黑块版狭窄 green report。
- Desktop owner/router/shared source：23 项；background clock/admission：10 项。
- Formal Steam relay：28；HUD client：11；共同 renderer 两文档同像素：9；生产控件 browser fixture：40；TDP：43；native-preview owner：33；legacy client：31；readonly host/relay：32。
- 当前真实 Steam 的正式 installer 原始 hook/flag 发现、Root 挂载及清理通过。其 transport 刻意 inert，未创建游戏 popup，也未做游戏点击或真实传感器输出。
- 当前 native 完整编译链接、Vite 多页面构建、plugin 构建/字节 check、vue-tsc 通过；没有执行安装/发布。build/release 文件存在守卫已增加 `hud-output.html`。
- 旧 mutation 整套历史 FanView 来源对拍仍有不属于本轮 HUD 的 preimage 差异；历史 monitor 冻结 payload 文件也缺失。没有伪造历史 fixture 或宣称全项目全部测试通过。
- **本轮没有执行 CPU benchmark。**

## 还必须验收

1. 新正式 Steam popup 使用真实 YMCC 数据在 2077 全屏模式下显示、透明度和鼠标穿透；当前没有运行中的 2077。
2. 新主程序原生后台缓存时钟在实际主窗口最小化场景下长期工作，真实统一数据、AC/DC 下发状态与两路切换不重叠。
3. 新主 RPC 的真实游戏 valve 绑定，以及非 Steam 游戏窗口／无边框的兼容实测。
4. 安装版／运行中二进制与前端、插件的发布交接，不能仅凭任务目录构建就宣称已在用户安装版落地。

满足这些之前，不能标记完整目标 complete；不保证独立层的真正独占全屏。

## 2026-10-11 续轮：控制时钟与后台操作截止

**仍为 active，未安装、未验收正式游戏；本轮未做 CPU 测量，不切换桌面。**

### 修复的缺口

此前 native clock 只随着数据源租约启用，Steam 单独隐藏和独立窗口 create/ready 阶段可能没有数据源消费者；12s 回执超时仍仅依赖浏览器计时器。因此既有“后台持续数据”10项通过，不足以证明后台创建／关闭能按时结束。

- 新增 `src/bridge/monitorHudClock.ts`：一个带 nonce 的 native 1Hz 时钟，由数据及控制引用共同持有。最后消费者释放即停止；控制租约不取得 HWiNFO/统一传感器 demand。启用／ACK 失败、过期及晚回执不复活已结束 generation。
- Steam pending 操作自带截止时间与控制租约，native advance 或浏览器 fallback 任一路都可超时；没有真实隐藏证明时保持 unknown，不能启动其它输出。
- 独立 create/ready/painted 持有控制时钟；status/create/frame/close 原生请求有5s截止，创建／绘制总阶段保留12s截止。请求本身超时与整个绘制阶段超时分开处理。
- 主 IPC 不再用短浏览器超时丢弃独立 HUD 的晚创建描述符。owner 截止解决调用方等待，原始晚回执仍按准确 id/nonce 关闭；原生创建请求仍在途时不宣称 hidden。
- 关闭意图后立即释放 sample，并禁止继续续期 frame 或接受晚 painted 为显示证明。旧关闭真实回执可以解除旧窗口的不确定性。
- 时钟不可用的纯 availability 探测不反复 invalidate 自己，避免不支持该 native API 时形成无意义重试循环。
- 主 host 释放时保留关闭阶段的 clock event binding，直到独立 owner 关闭／有界失败后才解绑。

### 本轮证据与构建边界

- `control-clock/CONTROL-CLOCK-SELFTEST.json`：**33项通过**。包括无数据消费者的 hide、missing ready/create/close、晚创建清理、控制／数据共用时钟、最后引用清理、ACK失败、禁止关闭后的 frame 续期／painted、availability失败不自重试。所有浏览器计时器回调刻意不执行。
- 既有 desktop owner/source/router 23、background admission/steady clock 10、正式 Steam relay 28、HUD client 11 复验通过。
- vue-tsc、多页面 Vite task build、正式插件 task build及字节对拍通过；新 frontend/plugin 输出在 `control-clock` 子目录。**没有替换已安装 YMCC、Steam 插件或启动游戏。**
- 本轮没有编辑或编译 native；检测到此前检查点之外已有 native/main.cpp 漂移（其它工作）。本轮 PROGRESS 保存漂移列表，不把较早 native 构建结果当作当前全源重新编译验收。
- 这些是函数级／任务构建证据，不代表真实主 WebView 最小化、正式 Steam 全屏 popup、真实游戏鼠标穿透、AC/DC 实际数据链或安装交接已完成。

### 发布顺序

仍按用户批准的顺序：先验收落地 Steam 插件作为全屏路线；非 Steam 独立原生承载首版只承诺窗口／无边框，不为真正独占全屏背书。两路复用同一生产 HUD 模板和统一快照，顶层三选一不增加第4个输出。Steam 进程启动不等于当前游戏插件承载就绪。

下一阶段实际桌面／2077 验证与运行版替换仍需用户确认桌面可用。此时完整目标不可标记 complete。

## 2026-10-11 续轮：冻结候选与正式交接门

上一轮控制时钟工作属于 progress；本轮继续完成候选原生重新构建和匹配发布链修复。没有改写完成标准。

- 候选目录：`G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\desktop-output-20261011\runtime-handoff-20261011`，详见其中 `HANDOFF.md` / `PROGRESS.json`。
- 冻结 431源码输入，native/frontend/formal plugin及byte check均通过；129候选输出按 manifest 复核，主 EXE哈希 `6415ea9faf7e25d19057a61a5e218b57655c778947360b2d08f4d5fd6f9ac150`。是冻结候选，不能当作当前持续变化的主线已重新构建。
- 修复 deploy 曾只检查主 index、使用源目录缓存插件的缺口：两份页面／所有前端 chunk 逐字对拍，匹配的 built plugin覆盖source-cache，先备份真正旧安装插件。
- build/package/deploy共用 `monitor-hud-export.ps1` 校验；正式 source identity覆盖独立 HTML／Vue helper／export guard／deploy；built payload覆盖Vue许可。最终 Decky archive用编译插件及锁定loader为权威输入，7件精确字节验收。
- 19 export helper tests（也在WinPS5.1通过）、17 final Decky archive identity、43 source/build/stage/ZIP/rollback identity通过。第一次 exporter测试的两处child execution-policy失败保存；仅对子进程测试命令加Bypass后复验43全部通过，未改全局策略、未放宽断言。
- 本轮没有 CPU benchmark、游戏／候选进程启动、切换桌面或安装替换。完整InputHost/PowerControl产品闭包、普通发布门、真实Steam及独立游戏验收、主窗口最小化真实数据链与安装交接仍未完成。

目标保持active，不能把候选core构建或隔离文件校验当成真实安装／全屏／鼠标穿透验收。

## 2026-10-11 续轮：完整资源闭包与正常隔离部署

上一轮为progress。本轮正常完整构建和完整隔离deploy闭包已完成，尚不构成实际游戏或当前主线发布。

- `full-export-20261011/PROGRESS.json`和`HANDOFF.md`记录完整证据；最终源码/成品身份buildId `b0aae9377458075444f5056051d2db4b3ecb157c33a2e7430fa9f20aeec6962b`。
- 3068文件来源/资源副本，独立离线冻结锁依赖51缓存包0下载；未清理主工作区依赖。完整普通build（InputHost/LightSetter/优化门）通过。
- 正常deploy-installed -IsolatedRoot在`DeploymentRehearsal-AllPolicies`走完完整复制和install门：DEPLOY_OK / GATE_OK。产品/硬件smoke均明确SKIPPED，未伪装通过。
- 实际演练补齐此前仅helper/AST测试未覆盖的普通源资产策略：Decky7文件必须在generic dist/JSON排除前精确放行；Steam appearance只按已批准manifest及hash放行。23策略/实际复制测试通过，已覆盖整套普通源材料，未知业务JSON仍拒绝。
- 正式ZIP主线Git门拒绝副本，未伪造身份。canonical-export原生代码并行改动导致第一次source-changed拒绝，第二次build通过后再被pre-package source identity拒绝；后来本轮策略修复也要求重建。未放宽门、未反复重编宣称侥幸通过。
- 运行版及桌面未动、未启动游戏/候选、未做CPU测量。下一步需要源码稳定的正式制包及桌面可用后的Steam/2077、真实后台/ACDC、独立真实游戏验证，目标保持active。

## 2026-10-11 受阻审计：需真实测试窗口与稳定发布源码

完整目标仍未完成，不以构建或隔离文件复制替代正式游戏/输入/真实数据/实际交接验收。

- 本轮核对主线10秒无改动、无相关构建进程后，将普通完整build与package连续执行；buildId `8c50b38b87ab26ef2fe93540af3967525fe6bf2e7df8e675ecc11868c2463f1b`通过，但制包前`native/screen_touchpad_input_surface.h`在2026-10-11 03:30:51被改写，正常source identity拒绝归档。没有用旧产物冒充当前源码，没有假Git/假CI/放宽门。
- 函数/Source/完整资源闭包/正常isolated deploy准备已完成；正式Steam 2077、主YMCC实际最小化实时数据与AC/DC、独立真实游戏、实际版本交接均仍欠验收。上一轮是progress；本轮亦得到新的真实build/拒绝证据，最终阶段现已没有无需外部变化的安全推进动作。
- 控制时钟、runtime-handoff、完整资源、当前连续轮均未收到新的人工桌面可用确认；当前没有2077，且不能自动抢桌面/替换运行版。这个交互阻塞已连续至少3轮复核，受阻审计满足阈值。
- 证据：`canonical-final-20261011/PROGRESS.json`、`SOURCE-DRIFT-BEFORE-PACKAGE.json`及`BLOCKED-AUDIT-20261011.json`。运行版、Steam/Decky临时目录、桌面未变，CPU测量未做。
- 下一步需要用户确认桌面空闲，并让主线暂时不再并行改写，随后正常正式制包与实际交接、Steam/2077验收、独立窗口/无边框验收。

## 2026-10-11 续轮：当前主线正式完整候选包已生成

受阻审计后源码出现稳定窗口，本轮取得新的实际 progress，目标恢复 active。

- 当前主线正常完整build通过，Build ID `e334482455be52632beb3955950725ef56b62457144ccb98353283b18de5d027`，主程序哈希 `507CA93E8D3B2F506C6FE3D137EF320BF813482518D7C4E5A92616A2568E59E2`。
- 正常完整package通过：`GATE_OK`、`RELEASE_PACKAGE_OK`、`PACKAGE_OK`；ZIP `YeManCC.zip` SHA-256 `4A800B1EBA3F0179235B0CC9969795C6892323D3FB82CB863047762FF9FE5449`，104,927,319 bytes。
- 正常`deploy-installed.ps1 -IsolatedRoot`通过`DEPLOY_OK/GATE_OK`，完整资源闭包已实际复制到任务隔离目录；真实安装版仍未改。
- ZIP内容复核含独立`hud-output.html`、编译版插件和Vue许可证；33/23/28 HUD关键回归通过，未做CPU测量。
- 仍缺真实运行版交接、Steam+2077全屏透明度/真实YMCC数据/鼠标穿透、主程序最小化AC/DC链路及独立窗口/无边框真实游戏验收。运行中只有Steam和现有YeManCC，没有2077；不把候选包通过等同于游戏验收。

