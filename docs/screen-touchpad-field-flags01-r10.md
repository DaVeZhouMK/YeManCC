# policy12实机仍闪：系统光标0/1交替与低CPU取证（2026-10-15）

## 实机结论与版本身份

用户报告“闪烁没变化”成立，上一轮显示顺序/提交时点修正不能宣称改善。新包 YeManCC-logs-20261015-193544-Steam自愈了？.zip，SHA256=469f6abaa0f46bb001eaa7f3eab03a9f529057365e04e52e772f42dc58d99a44。57条screen-touchpads-state均为PID9988、cursorPolicyVersion=12、submitOrder1、geometry1。导出时间2026-10-15 19:35:44.652（Asia/Hong_Kong）；包内导出wrapper不自带PID，state与同进程日志轨迹对齐。报告标题中的Steam自愈不是本任务执行指令，不从标题推断Steam故障已恢复。

主线当前新增本轮低CPU/取证后为policy13、cursorHiddenQueryBudgetVersion=1。直接Mouse不闪、SD经Steam Input合成鼠标闪仍是用户提供的关键对照，未用新增SendInput代替SD链路。

## 新证据改变排查方向

| 指标 | 旧17:05包/policy9 | 新19:35包/policy12 |
| --- | --- | --- |
| 已采样系统flags转换 | 0 | 2070 |
| 系统显隐通知 | 0 | 2124 |
| 显隐事件GetCursorInfo查询 | 0 | 2105 |
| helper shows / hides累计 | 7 / 7 | 12 / 12 |
| 查询错误 / layered resets | 0 / 0 | 0 / 0 |

两包持续时长、手势与上下文不同，累计数不能当作CPU速率或视觉改善比较。新包仍只有一次cursor图像上传，未见查询失败、layered重建或定位失败。

从57个state的固定环取回111个唯一sequence片段；其中63个连续active Deck有效样本为flags0/shape0或flags1/shape65539，共62次0↔1切换，tick113765..114031（266ms），全部fallbackVisible=false。坐标从(1487,250)向(1438,386)改变。这证明该取回区间的系统光标状态/坐标在变，且YMCC补画没有反复show/hide；不能扩大到未留存的整次手势，也没有屏幕scanout证据把每个样本与肉眼闪烁逐帧对应。

CURSOR_SUPPRESSED=2与隐藏flags0不同。当前代码对flags0尊重隐藏；对SHOWING且形状有效交回系统；这段0/1序列都不会进入helper上传/定位/show。累计observer.unchanged=2099的含义是helper可见性未改变，不表示系统flags没有变化。新包能排除“这段只因helper频繁重建/显示而闪”的解释，不能证明隐藏API的调用者。

时钟用19:29:01.883的input-host-frame-evidence observedTick/receiptCompletedTick=184546近似关联，区间约19:27:51.102..51.368。controller_ui记录19:27:38进入Unprivileged并恢复app mapping，19:27:53进入Privileged并回默认M/KB。映射切换仅是附近上下文，不能用两秒相邻关系证明Steam隐藏调用、前台身份或确切触发原因。早期foreground日志轮转已不覆盖该时段。后来导出时nativeTransportReady=false是更晚的状态，不能反推它导致上述显隐序列。

Valve已下载官方sp.js中ChangeNavigationSource会对非MOUSE导航请求HideCursorUntilMouseEvent；代码存在不等于此现场实际执行。另一个候选是WinRT输入child类hCursor为空且没有WM_SETCURSOR处理，默认类/父窗口逻辑可能参与，但根窗口已注册箭头，现场消息可达性尚未证明。两者都保留未知，不直接改Steam行为或child光标策略，不通过忽略flags0强行画箭头。

## 本轮主线窄改动

1. native/screen_touchpad_cursor.h：仅在Deck active、helper已隐藏、now仍早于原cursorDeckNextPositionTick时，跳过本来不可能改变画面的GetCursorInfo。**不推进deadline、不减少已到期查询、不节流visible/retained、不改首次准备recheck。**到期0/1查询后紧接SUPPRESSED仍可按原机会恢复；这与曾考虑的“查询0/1也推进下一槽”方案不同，后者可能额外延迟恢复，未采用。原有24ms预算、映射、UP/cancel、震动与Mouse路径保持。

2. native/screen_touchpad_cursor_visibility.h：在既有回调过滤后、coalescing前保存固定64项SHOW/HIDE event、source TID、observer TID、eventTick32、generation。只是内存赋值，不新增hook、时间API、分配、文件写或进程查询。事件来源与稍后查询NOW得到的flags不强绑，sourceIsObserverThread只比较同一存活hook的线程。

3. 显式导出才对捕获trace中至多8个去重TID执行THREAD_QUERY_LIMITED_INFORMATION的OpenThread、GetProcessIdOfThread、GetThreadTimes，随即CloseHandle。结果在snapshot顶层cursorEventThreadOwnersAtExport中标明lateResolution=true、historicalEventProcessProven=false；线程退出/ID复用或无权限时保留未知，不把导出时PID归属冒充历史事件源或API调用者。普通state()/触摸MOVE/回调不调用此函数。main.cpp只改导出块，其他任务并行的任务视图和风扇哈希修改原样保留。

## 重放和回归

| 验证 | Before | After |
| --- | --- | --- |
| 最终同一实际头文件重放/来源/导出检查 | 15项中恰好7项预期失败 | 15/15 |
| 63条现场查询样本、266ms的一事件+既有BUS机会模型 | GetCursorInfo=72 | GetCursorInfo=18 |
| 同重放helper show/hide/upload/position提交 | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 |
| 既有光标回归 | 旧证据保留 | 70/70 |
| 实际WinRT触点/几何/消息CPU回归 | 旧证据保留 | 163/163 |
| 真实UI/BUS私有队列回归 | 旧证据保留 | 20/20 |
| 原提交时点模拟 | 旧证据保留 | 19/19 |
| 完整穿透切换模型 | 旧证据保留 | 64轮、1038断言、1600命中 |
| 源码改动与输入边界 | 无额外宽改动 | 8 + 12通过 |

重放输入是保留的GetCursorInfo样本，不是原WinEvent payload和完整现场时间线；每条样本构造一次通知并给原有BUS机会，72→18只证明这个确定模型内少了无作用查询，不能外推现场2105会变成多少、整机CPU下降比例或闪烁改善。固定环测试覆盖回调合并前仍记录两个不同来源、64容量覆盖；显式导出测试真正打开本测试线程一次并验证CloseHandle调用，校验最多8次探针；不导入User32/GDI/输入DLL。

早期实验保留：Hidden-Budget-Candidate是较宽、会推进hidden查询deadline的候选，未应用；Replay-Candidate的13次上限断言失败（实际18），说明窄优化并不承诺所有查询严格每槽一次。最终改为验证确实跳过无作用读取，保留可见/恢复的必要查询。首次导出测试因在NOMINMAX前包含windows.h编译失败，修正夹具包含顺序后最终15项通过。全文件main逆向Delta检查被并行任务修改触发失败，查明是任务视图证据与风扇payload哈希；最终严格比较共享导出块，并保留全文件差异作为并行证据，未撤销他人修改。

## 留档、复现与未完成项

根目录 G:/YeManCC-Work/Build/Analysis/Touchpad-Flicker-Field-20261015-193544。

archive-provenance.json保存ZIP身份及18项选取文件哈希；field-summary.json / state-records.json / trace-pid-9988.json / flags01-pid-9988.json / old-new-comparison.json记录解析；trace-approximate-walltime.json保留时间锚点；PRIMARY-HIDE-SHOW-REVIEW.md为一手合同与候选复核。Before/After、production.diff、delivery-receipt.json保留精确来源。Replay-Before-Delivery2 / Replay-After-Delivery2是最终15项；Cursor-Regression-Delivery / Native-Contact-Delivery是包含最终导出函数的头文件验证；Native-Queue/Report-Order/Mouse-Pass在仅添加未调用的导出函数之前运行，原执行路径未改变。

复现入口 tools/screen_touchpad_hidden_cursor_budget_selftest.ps1 -OutputDirectory <fresh-dir>；基线加-CursorSourceDirectory <Before/native>以及Replay-Before-Delivery2/receipt.json中的7项ExpectedFailures。来源fixture由63条原始样本生成，见tools/screen_touchpad_field_193544_fixture.h。tools/screen_touchpad_hidden_cursor_sourcecheck.mjs <Before/native> <current-native> <report.json>证明3个触摸板头文件与main导出块窄范围；不是完整主程序编译/运行。

未部署、重启Steam/游戏、改变设置/设备或注入实际输入。上一轮policy12现场视觉改善已被用户否定；本轮policy13主要完成低CPU与来源证据，**没有宣布闪烁修复**。下一份日志可利用sourceIsObserverThread和导出时有限线程归属进一步区分本地窗口/外部来源；如线程已退出则证据仍可能不足，需继续保留unknown。
