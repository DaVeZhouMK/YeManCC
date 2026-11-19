# SD触摸期间的光标可见性补偿（2026-10-21开始，2026-10-22收口）

## 2026-11-15：SD 完全退出软件鼠标补画

现场后继：用户随 ROG `20261115-115123` 日志明确确认成功。实际双侧 SD、策略19、处理事件计数448，光标查询／同步／显示／上传／强显／桥接均为0，观察器未启动且无离手保留。该项现场成功已记录于 `Testing/YMCC/Evidence/Touchpad-Wake-Recovery-20261111/sd-cursor-owner-20261115/field-success-20261115-115123/FIELD-SUCCESS.json`；三点菜单交互及虚拟手柄初始问题分别保留。下文“未部署”是源码交付时的历史状态，原件不倒改。

ROG 091856 日志表明，策略18虽然关闭 flags=0 强显，仍通过 CURSOR_SUPPRESSED、触点形状桥接和离手后 retained 显示软件鼠标。当前策略19撤掉 SD 的所有这些显示许可：SD 按下、滑动和离手均不创建备用光标、不启动光标 WinEvent 观察、不请求 BUS 光标轮询。切回 SD 时撤下旧 Mouse 位图，迟到消息和图像准备期间的模式切换不能重新显示。

软件光标只由明确配置的直接 Mouse 触点拥有；原 Mouse 抑制补偿、移动和80ms交接保持。Steam／Windows 自己的光标显隐保持原生决定，没有调用 ShowCursor／SetCursor／SetSystemCursor 强制隐藏，也不改 SD 触摸报告、方向、点击、震动或穿透。旧 Deck 计数保留为日志兼容，新增 cursorDeckSoftwareCursorEnabled=false 和 cursorDeckContactShapeBridgeEnabled=false，force版本3。

既有生产头离线消费者53检查通过，单独旧头对照复现残留补画。当前只写源码，未部署；实机效果待下一自然原生构建。证据 Testing/YMCC/Evidence/Touchpad-Wake-Recovery-20261111/sd-cursor-owner-20261115。下文为历史行为，尤其“保留SD抑制补偿”已被本次要求取代。

## 2026-11-13：取消覆盖原生隐藏状态

用户反馈SD模式下不应出现的鼠标被强行显示。当前已撤下下文的active Deck隐藏状态覆盖：Windows返回flags=0时隐藏YMCC备用光标，不再因手指按住而补箭头；SHOWING但缺少形状时也不创建新光标。原CURSOR_SUPPRESSED触摸抑制补偿、移动/点击/虚拟报告、鼠标穿透及24/48ms预算保持。日志cursorPolicyVersion=18、cursorDeckForceVisibleVersion=2，并明确cursorDeckForceVisibleEnabled=false；旧强制计数保留兼容但不再增长。下面是历史策略与历史验证，不能作为当前行为说明。

用户在HIDMaestro/USBIP调查后明确要求继续，并询问能否强行可见。本轮仅调整屏幕SD触摸板的光标显示策略，不修改触摸报告、鼠标坐标、Steam消费设置、手势或虚拟目标生命周期。

## 触发依据与行为

20261021-224432的保留cursor样本sequence438/439在相同tick10780718、坐标1521/514，出现flags1/hCursor65539 → flags0/hCursor0。此前cursorSyncInfo遇flags0就隐藏软件光标，故既有CURSOR_SUPPRESSED补偿无法覆盖该状态。这解释了本程序为何未补画，尚未证明是谁将系统光标置为隐藏。

新增cursorDeckFallbackOverride仅在有效Deck-only触摸跟踪中生效：系统缺少SHOWING+有效句柄时允许现有备用光标显示，覆盖flags0、SUPPRESSED和SHOWING+空句柄。观察到可绘制的系统光标则立即撤下备用光标，保留原SD交还规则。

优先使用当前触摸手势内见到的有效形状；当前无形状时用Windows箭头。新手势、取消或模式变化清掉该手势的形状授权，不从前一手势的旧位图或UI线程的GetCursor恢复形状。首次图像准备失败不能显示旧缓存；已显示的有效帧保留原有失败容错。图像准备后的现有一次复查也允许active Deck的flags0补画，仍检查当前所有权/系统可绘制状态/形状变化。

UP后退出强制许可。flags0的正常离手不进入retained；原有SUPPRESSED/SHOWING空句柄的被动收尾规则保持，retained不能取得新的force许可。cancel、关闭、模式/transport释放仍走原cursorStop。直接Mouse和混合Mouse/Deck手势不取得此覆盖。

## CPU、穿透和边界

复用现有不可激活、禁用交互、鼠标穿透的layered cursor HWND。未调用ShowCursor计数循环、修改其他进程光标状态、设置焦点、发送鼠标输入或增加线程/timer/轮询。

原24ms位置预算及BUS请求合并保持。已可见的flags0边沿只记观察，不延后位置deadline，防止密集HIDE通知让位置更新饥饿。首次显示/到期移动仍共享原位置slot；位图按形状缓存，相同位置不再定位/绘制。新增固定形状指针与整数计数，snapshot policyVersion=17、cursorDeckForceVisibleVersion=1，记录forced hidden samples及forced shows。

该补偿保证已执行的有效显示决策覆盖flags0，不等同于保证每个Windows合成帧都无闪烁，也不证明HID/Steam数据侧没有问题。实际闪烁改善仍需运行新版本的现场反馈。

## 软件验证与交付

证据根：D:/Codex/2026-10-21/files-mentioned-by-the-user-yemancc-4/sd-force-visible-20261021。

- 完整冻结原生源码556份输入编译通过，Native/YeManCC.exe仅构建，未启动。
- 最终实际生产header+替代OS API的context回放26项通过，无User32/GDI输入图形导入、真实窗口或输入操作。
- 对照只替换为修改前cursor header，26项中的两个新补偿期望精确失败；失败点保留在context-baseline/RESULT.json。候选对应期望通过。
- 定向force-visible回归33项通过，见verification-final/receipt.json；包括现场同tick显隐、空形状、位图复用、密集隐藏边沿不饿死移动、24ms slot、10000次BUS请求合并、UP/取消/模式切换、资源/查询失败、准备期间取消。它回放实际生产header，不是Steam或G3E显示实测。初次回归在准备期间取消一项失败，原失败保留在verification；补充准备后所有权检查后33项全部通过。
- 旧context工具首次因本环境默认SDK/PATH缺口无法找到windows.h；原失败保留，随后明确使用Windows SDK10.0.26100.0及系统工具PATH完成构建，不改系统环境或安装组件。

本轮未运行产品、真实触摸/鼠标注入、虚拟目标、设备I/O或睡眠，未导出/部署安装版。
