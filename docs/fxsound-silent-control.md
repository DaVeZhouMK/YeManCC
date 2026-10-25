# FxSound 静默音质开关

日期：2026-10-24（Asia/Hong_Kong）。本节点交付主线实现、后台实测与 Decky 接口/真实 TSX 组件测试；真实 Decky/Xbox 全屏接入验收留待下一阶段。未将候选覆盖到当前安装目录。

## 用户合同

- Decky 底部“软件”区第一项“音质优化 · FxSound”，使用原生 ToggleField。
- 只指定“移动端通用音频优化”。独立 .fac 文件存在即不动，缺失才补；不解析或修改内部参数。删除后下次开启才重新补齐。
- 预设来源依次为 C:\SOFT\FxSound\FxSound\Presets、C:\SOFT\FxSound、随包 PowerControl\fxsound。最后一项备份与本机原文件 SHA256 一致。
- 禁止菜单、UIA、模拟鼠标/键盘、可见前台操作或抢焦点。程序通过系统 shell 启动；真实效果由原装 FxSound 处理。
- 关闭必须等待 FxSound 正常结束，再执行现有设置页的同一 Recovery 消费者，确认默认输出已离开 FxSound 且真实设备可用。A100 是本次实机检查设备，产品代码没有硬编码 A100。

## 开启与关闭

fxsound.getState 仅观察当前会话进程、FxSound.settings 和 Core Audio 路由，无写入及窗口操作。开启时若已有实例已启用并选中目标预设，则直接复用。其他状态下通过该进程的 JUCEWindow 消息线程投递 WM_QUIT；JUCE 6/7 调度器将其转换为 systemRequestedQuit，FxSound 调用 quit 与 shutdown 正常清理。没有 TerminateProcess/taskkill。

FxSound 1.2.6 的 anotherInstanceStarted 只显示主窗口，不接收新预设参数。因此既有实例需要调整时，先后台正常退出，再变更停止状态的配置并启动。静默配置补丁仅涉及 preset、power、run_minimized、hide_notifications，以及避免版本切换强制显示更新窗口的 version 记账字段。新建空白配置时增加 survey_displayed；不会植入本机扬声器 ID。其他字段逐字节保留，原 .fac 内容不触碰。仅在确认文件不存在时新建配置；现有空文件、损坏、被占用或读取失败均报错，不重置整份配置。提交前复核文件存在性和原字节，其他入口新建空文件或改写内容都会取消提交。

停机写入前后检查进程状态与原文件内容，再用同目录临时文件原子替换；有冲突时返回具体原因。启动前用系统 shell 的隐藏显示方式，FxSound 读取 run_minimized=1 和 hide_notifications=1。

开启失败但已退出旧实例时，同样检查是否残留 FxSound 虚拟默认设备；若存在则调用现有 Recovery 恢复系统输出，返回开启失败而不冒充成功。

关闭后的现有 Recovery 有 3 秒确认、每个角色写入前复读和实际默认设备验证；若玩家或开机项重新启动 FxSound，修复取消且不宣称关闭成功。

## Decky 与启动冲突

原生 API：fxsound.getState({})、fxsound.setEnabled({enabled:boolean})。后台 worker 执行，校验退出/电源代次；不增加独立产品 EXE。镜像命令 audioOptimization.setEnabled 经过原认证、快照代次与状态源校验后调用同一个 YMCC owner。YMCC 的音质优化启动场景也走该 owner。

侧栏显示期间共享一个 1500 ms 状态读取任务，手动启动或退出会反映到开关；只读状态，不周期重放 ON。隐藏/断开后释放计时器，迟到结果不会重新建立观察。已接受的开关操作可以正常完成；关闭侧栏不自动关闭 FxSound。客户端等待上限 40 秒，超时只重读、不重发写请求。

## 本轮证据

- 原生恢复与静默配置/临时文件：104 项断言；6 项故障负例均被断言拒绝。
- Decky 实际客户端、relay、host、owner 与 TSX：29 项，其中 6 项 FxSound 专项。硬件/IPC 口在该组测试中为内存实现。
- 真实 Windows 认证 WebSocket 桥 → 实际客户端/relay/host/owner → 原生控制消费者 → 本机 FxSound/A100：6 项通过；重复 ON 同 PID，4 个实际开关请求，关闭后有 A100 输出信号，最终 broker 端口/进程/观察计时器均释放。
- 原生桥漏列音质命令的旧策略反例可复现：请求在到达音频控制前断开，音频写入为 0。当前白名单已添加 audioOptimization.setEnabled，认证和其他准入不变。
- 被占用配置导致 ON 失败的真实测试：旧实例正常退出后恢复 A100，回执仍为失败，无可见窗口/焦点变化，后续能正常再开启/关闭。
- Decky 客户端已有回归：54 项；原生 broker 协议 26 项。
- YMCC 开机模式运行测试：106 项，包括实际 FxSoundOwner 到内存 IPC 的静默启动请求。
- MSVC 完整主程序构建、后续冻结主翻译单元语法检查、Vue 类型检查、Vite 构建与 Decky 插件构建/逐字节验证通过。共享主线其他任务的功能不因这些定向测试而获得全量验收。
- 本机 FxSound 1.2.6：冷启动、重复 ON 同 PID、关闭、重复 OFF、外部已有实例处于 General/音效关再切到目标预设均通过。
- WinEvent 观察上述最终路线：无非零尺寸可见 FxSound 窗口/前台事件，操作前后前台 HWND 相同。早期菜单方案已否决并退出产品源码，不作为通过证据。
- 开启时 A100 峰值 0.1688300520，关闭后 0.0549147725；两次播放 API 返回成功，A100 未静音，系统音量均为 0.12。此为系统端点输出信号证据，不冒充麦克风声学测量。
- 最终 FxSound 不运行，默认播放输出为 AIRPULSE A100，保留目标预设选择。

## 安装与尚待验收（2026-10-24 后续）

已完成配套候选构建、ZIP 配套校验及本机安装。候选 673fa23587f3f4a3b01026d624ebd7e99a5c68f26373e956a6d47553e1a98d91 的安装版 WebView IPC 真实开启/重复开启/关闭/重复关闭通过；无 FxSound 可见窗口或焦点变化，A100 恢复并有端点信号。17:57 的 Steam 保留页面认证通信接通，但该窗口未执行真实音频按钮操作。

本机随后换入另一份测试安装 9d391a78c352e38a1dda3c82afc3a69c3cd6416075871bee7c89b507b7bd8f5a，缺少音频 API 和插件命令。当前保留该外部测试安装；最终真实 Decky 按钮、Steam＋Xbox 全屏及实物手柄仍未验收，不能以编译或内存测试代替。最新报告位于本线程 outputs/FxSound-验证报告.md；共享主线音频代码保留。

安装器同步了普通测试导出的声明、配置基线合同和包内生成清单交付。模块清单必须随同主程序/网页/插件从同一已验证 ZIP 安装，否则原生资源自检可能恢复旧插件。全量发布夹具仍标记 NOT_RUN。

## 维护入口

产品：native/fxsound_control.h、src/bridge/fxSound.ts、native/main.cpp、src/bridge/bootModes.ts、src/bridge/deckyMirrorHost.ts、src/bridge/deckyMirrorRelay.ts、decky-plugin/src/index.tsx 与 mirrorClient.ts。原 Recovery 两个头文件未改。

当前旧 host-loopback 默认游戏字段场景在 corePolicyMode 已不存在时失败，证据保留在本线程 work/evidence/decky-native-loopback.log；音频实机模式不计作该默认套件通过，不删除或放宽其断言。

测试复用：tools/fxsound_recovery_selftest.ps1 -EvidenceDir <新目录> -Inventory -NegativeControls；tools/decky_sidebar_controls34_selftest.mjs；tools/decky_sidebar_client_selftest.mjs；tools/boot_modes_selftest.mjs --out-dir <证据目录>。不要自动扩跑整套模拟器。

音频完整传输入口复用 tools/decky_sidebar_host_loopback_selftest.mjs --fxsound-native <显式原生测试消费者 EXE>，只在显式参数存在时操作真实音频；普通模式仍为内存 IO。认证桥使用原生产 broker。真实 FxSound 与端点写入、纯内存业务配置、安装版 WebView/Steam 未执行范围在回执中分别标记。

## 场景默认值（2026-10-24 用户补充）

音质优化默认仅在 `steam`（Steam 大屏）和 `xbox-steam`（Xbox 全屏＋Steam 大屏）开启；`global` 与单独 `xbox` 默认关闭。`audioDefaultsVersion=1` 仅为旧场景缺少明确音频选择时补齐一次，保留已明确保存的选择和之后改为跟随全局的选择。观察到的软件运行状态独立保留在 activeSnapshot，不把默认值当作已执行结果。

场景启动通过同一个 FxSoundOwner 请求开启。关闭的场景表示不自动启动，不主动退出玩家手动启动的软件。Decky 开关始终反映实际软件状态；只读观察不会重复强制 ON。编辑或保存场景不启动 FxSound。

普通测试导出部署沿用包内 `mainline-local-test-export` 声明；先核验完整 ZIP、配套主程序/网页/插件，再识别 `formalReleaseAccepted=false`、`fixtureValidation=NOT_RUN`。不将未执行的完整 Decky 发布夹具写作通过；正式包仍要求原 Build/Package 回执。