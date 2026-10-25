# SD触摸期间的光标可见性补偿（2026-10-21开始，2026-10-22收口）

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