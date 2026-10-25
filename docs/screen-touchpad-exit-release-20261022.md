# 触摸板终态退出与多轮信号确认（2026-10-22）

用户要求：排查触摸板/专用按键这类叠加层是否会在退出后保留触摸或鼠标控制；释放时多轮核对信号，退出必须严格确认。用户提供的监控对话仅作参考，本轮不接管监控问题，不修改监控窗口/WebView产品逻辑或现场开关。

## 已有日志与证据边界

只读核对G3E 20261022-080106和112346归档。前者两次完整记录退出（PID8620、8476）分别在07:52:44.039和07:57:29.708到达window-destroy，距退出开始2.702秒和2.272秒。window-destroy不是进程已退出或桌面输入已恢复的证明。旧日志缺触摸板原窗口、ControlledInput源及capture终态回执，不能据此确认永久输入卡死的归因或已放权；虚拟目标的local-dispose-complete/external-release-unproven不能当作桌面输入释放成功。

原日志路径、行号、提取时间线与限制见证据根log-review/report.md。用户继续追查虚拟手柄进出信号，本轮只收紧触摸板发送者、受控源和窗口这一层；不把本层结果包装成驱动收到中立帧或Steam/游戏已消费。

## 发现的退出顺序缺口与修补

此前Surface.close先执行cancel sink、撤事件、controller/target Close，再销毁child并恢复root样式。触摸板按顺序清理两板/七按键，没有在这些潜在等待或回调前把全部输入窗口一起撤下。主WM_APP_EXIT/WM_CLOSE有专用shutdown，但WM_QUIT进入的beginAsyncExit没有统一撤下，WM_DESTROY也在多个其它模块之后才停触摸板。这些是可明确修正的顺序缺口，不等于已经复现了用户的永久卡死。

新增终态quiesce：先关闭输入与配置准入，禁用触摸板投影，冻结新touch/按键/MOVE；再把两块触摸板、七个专用按键的root/child全部隐藏、禁用，才进入cancel/COM Close。刷新、迟到配置及queued rebind不能重建；旧输入事件在终态不再标记Handled。普通活动状态仍保持原先对过期generation事件的消费规则，避免把重绑定的旧UP泄漏给下层。

Surface只在显式终态withdraw时使用新quiesce。普通close/重绑定保留原显示语义，避免改变共享该类的其它叠加层。quiesce先锁住policy，防WM_ENABLE/样式维护再次启用；Win32 capture只释放精确属于当前root/child的句柄，随后终态close在撤事件后显式释放该source的pointer capture。关闭异常逐项处理，递归close受保护；终态Capture/controller/target失败独立保留，成员置空后也不会被误报为已释放。共享Compositor/DispatcherQueue不因触摸板退出而关闭。

主线统一入口：beginAsyncExit先withdraw，再走外部退出操作；正式close和回执在HID恢复/清理worker前。WM_DESTROY最先处理触摸板；确认的会话结束和消息循环最终兜底也关闭并记回执。托盘/任务栏常驻的关闭仍为隐藏，不进入终态。

## 严格多轮确认

退出保留原root、child、描边、触点反馈、备用光标句柄，而不是只查看已经置null的数组。一次检查读取：本层仍拥有的键盘键/鼠标按住状态、专用键mask、触点/点击脉冲、未确认关闭的两板和七按键输入源、原窗口是否仍存在。

最多6轮，必须最后连续3轮全部清空才令verified=true。每轮重新读取，失败会打断连续计数；仅对仍属于本层的UP和本线程本进程原窗口销毁做有限重试，不重放Down、MOVE或tap，不释放其它进程/线程窗口和其它overlay的capture。不在活跃会话执行该确认。DestroyWindow、SendInput UP或受控源Close永久失败会留下未确认记录；不会靠清空变量或重复打印成功使其通过，也不会无限循环拖住退出。

output终态门将迟到的旧请求投影为Action{}；正常BUS读取在原nativeMode/enabled/available准入关闭后返回中立触摸板投影。已有InputHost退出协议和驱动逻辑保持。本层SendInput账本仅能确认API接受的自己拥有的UP，无法证明目标应用消费；不通过全局GetAsyncKeyState干扰用户真实按键。

日志事件screen-touchpads-exit-release进入常驻日志通道（详细日志关闭时也保留），记录exitEntry、requiredConsecutiveCleanRounds、每轮heldKeys/heldMouseButton/screenButtons/contactsPending/openSources/liveOwnedWindows/error及verified。driverReceiptProven和downstreamConsumptionProven保持false。state同时导出inputTeardown、exitInputWithdrawalVersion和exitInputRelease。

## CPU与验证

重复读取/销毁尝试只在退出执行，固定空间与最多6轮；没有新增线程、timer、Sleep、忙等、常驻设备/窗口枚举或刷新率。正常输入仅增加终态准入判断；本轮未做实机CPU压测。

- 最终回归verification/all-reviewed：52项生产片段执行检查、5项源码准入/顺序检查通过；执行正式方法体，Windows/WinRT/输入边界为内存替代。
- 实际常驻事件分类器与日志通道策略3项检查通过，含详细日志关闭仍保留退出回执；见log-gate-check。
- 独立静态退出入口检查7项通过，见exit-entrypoint-checks.json；不将静态判断写成真实消息循环回放。
- 单一负例删去shutdown首个quiesce，精确出现预期5项失败，其中包含“两板七按键必须在任何cancel/Close前全部withdraw”；原/变体SHA及回执在verification/negative-omit-quiesce，证明回归能抓到该退场顺序缺口。
- 编译回执以native-build-final-verified-result.json为准。冻结完整原生源码构建只生成候选，未启动应用。首轮脚本替换带来的编译错误已修正，原失败日志保留；不将该构建错误当现场故障。

这些回放不运行真实WinRT输入实现，不创建设备/窗口/hook，不注入真实输入，不证明G3E的永久卡死已经消失，也不证明驱动/Steam消费。queued settings/rebind以实际消费者源码断言覆盖。

证据根：D:/Codex/2026-10-22/new-chat-4/touchpad-exit-audit-20261022。源码与构建/回放身份、漂移和最终状态见RESULT.json。未覆盖安装版、导出发布包、切换本机虚拟手柄或监控，也未执行真实睡眠。