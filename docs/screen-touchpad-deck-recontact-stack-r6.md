# SteamDeck 新光点显示后的光标顺序恢复（policy 11 / R6）

日期：2026-10-15，Asia/Hong_Kong。继续触摸板任务；监控由另一任务负责。本轮有新的原生复现与主线修正，实机闪烁仍未验收，未部署。

## 新证据与根因

policy10 修复的是外部可见性恢复时漏抬高反馈层。继续使用实际 WinRT 输入宿主、真实 User32/GDI 和两个自有下层进程，在私有桌面组合验证后，还发现另一条不会经过外部恢复事件的路径：

1. 上一手势 UP 后补画光标仍可见，保存的 XY 位置有效。
2. 新 DOWN 重新显示接触光点；或另一块 Deck 板的 DOWN 显示第二个光点。
3. `syncContactVisuals` 使用 HWND_TOPMOST 显示 owned marker，会连同相应输入/边框组改变顺序。
4. 紧接着的 `cursorContactChanged → cursorSyncInfo` 虽然查询了当前光标，但 XY 未变且缓存仍有效，原分支跳过全部定位/抬高。
5. 真实窗口排序出现 contact → outline → input → cursor，或第二块板的 contact/input 组高于 cursor。固定位置可以保持错误顺序。

四种尺寸（80/90/100/110%）各复现“同板重新落指”和“第二块板落指”，总计八项确定失败。此复现证明显示顺序缺陷，不证明用户先前归档里的全部闪烁均由它造成。

## 主线修正

`native/screen_touchpad_contact_visual.h` 只在接触光点的 show/raise SetWindowPos 确实成功后，如果补画光标已可见，将 `cursorPositionValid` 置为 false。

原来紧接着就会执行的光标同步，由此重新检查已知层级变化，必要时用原有分支恢复顺序。没有新增光标查询、立即同步入口、计时器、线程或刷新率。普通 MOVE 不经过这段 show/raise 分支，因此保留位置缓存。首次显示、正常 UP/cancel、系统显隐所有权、Mouse 的宽限规则与 Deck 坐标不变。

`native/screen_touchpads.h` 的 `cursorPolicyVersion` 由 10 改为 11；`cursorTransitionTrace.evidenceVersion` 仍为 1。没有修改 Steam 接口、虚拟驱动、报告编码、Core 映射或物理震动链。

## 低 CPU 调用量证据

实际 WinRT 组合夹具验证了：

- 两类新 DOWN 均只执行一次原有 GetCursorInfo 查询；Before/After 都通过该预算。
- 在外部恢复后的原缓存稳定后，每种尺寸连续 100 次 MOVE，固定系统光标位置；四组共 400 次没有额外光标查询、位置型 UpdateLayeredWindow、光标 SetWindowPos 或恢复事件抬高。
- 同一最终 Before/After 对比中，光标查询均为 29，光标位图上传均为 1，接触光点上传均为 8、MOVE 均为 400，边框上传均为 42。
- 光标位置提交（SetWindowPos + position-only UpdateLayeredWindow）合计由 12 变为 20：新增八次发生在八个需要修复顺序的显示边界，不发生在上述 400 次稳定 MOVE 中。

修复不是所有 API 调用都零增加：必要的新显示边界可能多一次原定位分支的 GetWindow/SetWindowPos。没有把这份调用量证据解释为真实 YMCC CPU 百分比下降；本轮未测量进程 CPU 占用。

## 组合验证及边界

| 验证 | Before / policy10 | After / policy11 |
| --- | --- | --- |
| 相同实际 WinRT/跨进程/私有桌面组合夹具 | 90 项中恰好八项顺序失败 | 90/90 |
| 组合夹具原生命中 | 112/112 | 112/112 |
| 上轮外部恢复顺序回归 | 本轮不重复其旧基线 | 25/25 |
| 实际头文件/API 替身光标回归 | 沿用 R5 对照 | 70/70 |
| 完整输入宿主鼠标穿透切换回归 | 本轮不重复其旧基线 | 64 轮、1,038 项断言、1,600 命中 |
| Core/输入所有权/穿透源码边界 | 对比本轮 Before | 12/12 |

新组合夹具执行真实 WinRT Surface 创建和 nativeInputSink 映射适配器，但没有注入物理 Touch/Pen。前台观察由自有下层进程 HWND 提供，PID/窗口对象和排序/几何/命中均为真实 Windows 数据；光标观察为 SUPPRESSED + 空形状的固定样本。实际 WinEvent 到达、Steam 映射消费、DWM 扫描输出和用户可见闪烁不在这份验证范围。

按住时切换下层进程，不取消/重放触点，不重建输入源或位图；正常 UP 保留补画、cancel 清空状态，目标窗口命中保持。没有改动运行中 Steam/游戏、设备、用户设置或已安装应用。

## 测试资源生命周期

初始组合夹具在 WinRT UI 线程仍存活时尝试 SetThreadDesktop/CloseDesktop，返回失败。这是夹具生命周期证据不足，不作为产品问题扩大处理。最终夹具在专用 STA worker 中创建私有桌面窗口、WinRT compositor/controller；关闭 Surface 后在所属线程关闭 compositor，再调用 DispatcherQueueController.ShutdownQueueAsync 并泵消息，只有观察 Completed/GetResults 后才完成 COM 清理。main join worker 后关闭私有桌面，且 main/input 桌面均保持。

Compositor close 异常仍继续尝试队列关闭。三秒内未确认队列完成时，夹具写入失败结果并只退出这个隔离的测试进程，不在活队列上伪称正常 COM/worker/desktop 清理；严格 runner 拒绝这种结果。最终 Before/After 均实际报告 runtimeShutdownCompleted、queueShutdownObservedCompleted、uiWorkerTerminated、privateDesktopClosed、inputDesktopUnchanged、ownedTargetsTerminal 为 true。没有将超时当作成功。

## 留档与下一步

证据目录：`G:/YeManCC-Work/Build/Analysis/Touchpad-Native-Deck-Stack-R6`。

- `Before/`、`After/`、`production.diff`、`delivery-receipt.json`：精确增量及哈希。
- `Initial/`、`Recontact-Before/`：初次真实排序失败与早期夹具清理失败；保留原始结果。
- `Combined-Before/After/`：完成 UI worker 清理后的 78 项验证。
- `Delivery-Before/After/`：最终同一 90 项夹具、冻结头文件、完整队列清理与调用量预算。
- `Cursor-Regression/`、`Previous-Stack-Regression/`、`Native-Mouse-Pass/`、`input-boundary-sourcecheck.json`：相关回归。
- `CLEANUP-REVIEW.md`：官方 Windows 文档与夹具清理审阅。

入口：`tools/screen_touchpad_deck_stack_native_selftest.ps1 -OutputDirectory <fresh-dir>`；基线比较另加 `-BaselineDirectory <R6/Before/native> -ExpectBaselineFailure`。基线 runner 要求恰好八项已知顺序失败，不能以任意非零退出替代复现。

用户提供的最新归档仍是 2026-10-15 17:05:38 的 policy9 版本，尚无 policy10/11 实机视觉证据。本轮确认并修复了另一个可靠的显示顺序失败，仍不能宣称 SteamDeck 连续滑动闪烁已彻底解决。后续现场先核对 policy11，再使用 R5 的运动/位置/抬高统计区分剩余原因。
