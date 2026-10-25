# 全局桌面保持可见与可选 Steam 内部承载（2026-10-11）

## 当前实机证据
正式安装的 D8786B… 原生 HUD 在非游戏前台下仍可见，HWND=3153780 / PID=15932 / Win32 owner=0。2026-10-11 11:59:28 起的两次只读截图显示 CPU 占用 20%→25%，不是模拟帧。没有切换焦点、发送键鼠输入或单独读取传感器。此证据只验收现有全局桌面版本，不是新候选或 Steam 游戏层验收。

实际 Steam 正式 ymcc-sidebar 插件的 HUD owner 已加载，capable=true、roots=2，但 children=0、frame=null、visible=false。插件能力存在不等于 HUD 显示成功。

## 新候选能力
- 默认 ymccCarrier=desktop；桌面创建请求仍是 scope=global + nonce，不含游戏 PID。
- 新可选 ymccCarrier=steam-preferred；只有正式 relay 的游戏绑定与隐藏/显示状态已确认时才选 Steam，否则仍选全局桌面。
- 用户保存承载选项只是草稿，按应用输出才迁移。主输出仍只有 Steam 原生 / YMCC 自绘 / RTSS 三选一。
- Steam 首帧失败须先拿到旧窗口真实隐藏回执，再恢复桌面；旧窗口是否关闭未知时绝不伪造回执或叠开另一窗口。
- 已有 deckySidebar.updated 的确认 Steam 退出事件可撤销 Steam 状态并恢复桌面。没有新增进程、游戏或传感器轮询。
- 主输出事务期间禁止宿主恢复逻辑把旧偏好重新排入队列；否则关闭旧桌面的事件会覆盖用户刚选择的 Steam 路线。事务结束只记一次当前上下文，意外原生关闭可再恢复，不持续重开。
- 图块继续单排最多 16 项，无可见编号/百分比/标题，仍由原有 1Hz 统一源驱动。未增加数据获取；真实核心数组需唯一数据层提供。

## 验证边界
默认全局 9 项、可选路由 15 项、实际宿主工厂集成 9 项、完整控制页 41 项、正式 relay 28 项、控制时钟 33 项、后台时钟 10 项、导出校验 23 项通过。软件用例的 HWND、Steam 回执和样本均为明确的模拟测试，不能冒充真实窗口验收。

正式 build-workspace.ps1 构建与 deploy-installed.ps1 -IsolatedRoot 完整导出通过。Build ID：98f01c9e0dab31e61c08f48da5e86e7430cfa54b6de5feb42d675a3d38df6b95；候选 EXE：07caf1d6ae29732417579b7f748723d7b1b3cb04a8a2110a807699e94621f7e3。主机 InputHost 构建有 7 个既有可空警告、0 错误；fan-host 前门逐哈希验证通过。

以下为 12:17 时的历史状态（已由下方新验收更新）：真实安装版当时未改动，等待正常退出后部署；不能只混拷贝 Web/Native/插件，也不能绕过管理员或强杀。之后仍须验收新桌面默认路径以及实际 Steam 插件首帧/关闭/回退。当前整体目标不标完成，不做 CPU 基准。

## 13:28 正式安装与全局桌面验收更新
用户已明确授权正常 UAC 提示及 YMCC 开启/关闭。完整正式主线测试包已通过任务范围的哈希锁定离线事务部署；没有修改产品自动升级的严格新版本检查，也没有移除 deploy-installed 的既有 FanHost 保护或假造版本。版本仍为 0.0.35，测试事务只接受明确的旧 EXE 摘要、新包摘要与 Build ID。

- 正式包 SHA256：64764E98D5F1B119070F776D1154DE192A37175E111BA63372314246DE22C42E。
- Build ID：31c2b5d15700688d36aa3e6e56caccd8c77a875efdbf523c7a012cd44f17370f。
- 实际安装 EXE SHA256：03AE1DF7BEF0A67A110DFA5D8A22F439F25CF0D329EFA77E8B0F884CCB00E063。
- 隔离副本的注入失败回滚：原文件集与所有摘要完整恢复。成功轮：完整包校验、7 个不可变目录闭包、旧哈希资源清理、用户文件保留通过。
- 实际安装：逐文件摘要通过；正式 opt-inheritance install 门、Web/Decky matching-export 门、官方 fan payload 验证器均通过。安装与回滚过程不启动硬件控制、不安装驱动、不强杀。
- 真实 YMCC 正常托盘退出已确认旧进程及 HUD 消失，随后重启正常；当前 PID=10716 仅作为此时证据，下一轮需重新枚举。主窗口隐藏，HUD 可见，Win32 owner=0；非游戏前台仍显示。两次真实截图 CPU 16%→21%，背景透出 Codex；不发模拟数据。
- 当前真实设置根仍是原来的 R11 环境覆盖，未迁移数据目录。monitorOutput、TDP、风扇、CPU、游戏设置保留。6757 页面仍是明确的离线样式预览，不是实时验收窗口。
- 可选 Steam 承载代码及包已落地；实际只读插件探测 capable=true、children=0、frame=null。运行中 Steam 插件是否为本包代尚未确认，故不把它报告为新 Steam 路由首帧/关闭/回退验收成功。
- 16 纯图块消费端已安装，但统一发布端没有核心数组；真实窗口因此无图块，不能用模拟图补验收。未新增传感器采集器，未做 CPU 基准。

证据位于任务 global-followup-20261011/offline-transaction/，以 ACTUAL-ACCEPTANCE.json、ACTUAL-NORMAL-EXIT-RESTART.json、ACTUAL-INSTALL-OPT-GATE.json 及真实 HUD 截图为准。整体目标仍保留后续实际 Steam 承载验收，不标全部完成。
## 13:46 可选 Steam 承载接入与实际不可用回退
- 查明运行中的 Loader 使用原 R11 数据目录，YMCC 插件 bundle 原摘要为 46447948...，与正式配套包不同。仅备份并同步 ymcc-sidebar 的精确五文件闭包；不动其他插件、不迁移真实设置目录。
- 使用 DeckyPluginLoader.importPlugin 的官方插件重载入口，未重启 Steam、未关闭或启动游戏、未注入镜像凭据。实际插件网络资源新增取件请求，Loopback 1337 返回的 index.js 为 141779 bytes、SHA256=36566BF1EB3A7ED81FCF784D98AD2E5F7C144F65E2791866E88DA4F2A122C3DA，与正式包逐字节一致；重载后的协议、注册、平台能力均确认。
- 通过真实安装的 YMCC 页面及 UIAutomation 的 Expand/Invoke 操作选择“Steam 内部优先，可用时切换”，再点击“应用输出”。未用浏览器内存适配器，未直接构造 HUD 显示帧。实际反馈确认全局桌面开启、RTSS 后台保留、游戏 OSD 已关闭。
- 当前本机没有 Cyberpunk2077.exe，正式 Steam 插件只有 Full 根、无游戏绑定、children=0。在此状态下，显式 Steam 优先仍成功落到原生桌面；隐藏 YMCC 主窗口后 HWND=2168984 可见、owner=0，真实数据继续更新。此项仅验收“Steam 无可用游戏承载时桌面仍显示”，不是 Steam 游戏内首帧或失败创建后回退。
- 最后通过真实 UI 恢复默认 desktop，并正常退出/重启；13:46 时 PID=40356、HUD HWND=858812、owner=0、非游戏前台可见。测试的进程级调试环境已撤除，无 9366 监听。以后需重新枚举 PID/HWND。
- 原监控外观及数据开关、TDP、AC/DC 锁帧、风扇、CPU、游戏/调度等设置结构保持。新增的唯一监控偏好为 ymccCarrier=desktop。Input 业务设置未变；正常冷启动会依 native/input_startup_policy.h 更新 revision 与 startupAppliedSession，这两个运行期元数据如实记录，未伪称全文件字节不变。
- 原生/插件软件用例已覆盖首帧与失败关闭门，但真实“Steam 可用时”首帧、关闭及创建失败回退仍缺当前游戏环境。需要用户在本机提供已开启 Steam 叠加层的 2077 后才验收，不用 Desktop/H5 示例冒充。不做 CPU 基准。

新增证据：ACTUAL-FORMAL-PLUGIN-RELOAD.json、ACTUAL-ACTIVE-PLUGIN-SYNC.json、ACTUAL-UI-PLAN-PREFERSTEAM.json、ACTUAL-STEAM-PREFERRED-NO-GAME-FALLBACK.json、ACTUAL-PREFERENCES-PRESERVATION.json、desktop-restored-final/。CURRENT-GOAL-AUDIT.json 按两条原始目标分别列明已证实与缺失的真实证明；本轮有实际进展，目标仍 active。