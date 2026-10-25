# SteamDeck 显示通知与报告提交顺序 R8（2026-10-15）

## 结论

以用户已经确认的对照为前提：YMCC 直接模拟 Mouse 不闪，虚拟 SteamDeck 触摸经 Steam Input 合成鼠标时闪。没有发现新的正常 MOVE 误抬起或报文坐标归零证据；不改当前可用的 HID 触点、压力/点击或 Steam 映射。

本轮确认并收窄一个调度风险：原 BUS 在 screenpads::snapshot 开头先通知 UI 查询光标，随后才提交本帧 SUBMIT_FRAME。UI 可在报告提交前读到上帧位置，并消耗本次24ms显示机会。本轮把稳定 Deck 模式中原有的这一次显示请求放在本次报告尝试完成并退出传输锁后，消除这一处确定存在的提前通知窗口。

**主线已改；尚未验证用户实机闪烁改善。** Host 回执不等于 Steam 已完成鼠标合成。既有已排队、DOWN/UP 或显隐事件的查询仍可早于该帧报告；这次没有强行串行化它们。Steam 在 after 通知之后才更新鼠标的情况仍要等原有下一个采样机会，离线测试明确保留这个限制。

## 精确代码范围

- native/screen_touchpad_cursor.h：新增 cursorRequestBeforeReport / cursorRequestAfterReport 两个薄包装，继续调用原有 cursorRequestSync。Mouse/混合模式在原 snapshot 位置调度；Deck active/retained 在报告尝试后调度。原24ms预算、合并pending、视觉/GDI、显隐所有权、DOWN/UP/cancel与失败逻辑不改。
- 原 UI-only cursorDeckOnlyTracking 改为 atomic，供 BUS 只读选择请求位置。UI 仅在模式改变时发布 release store，普通 MOVE 不执行重复原子写；不增加锁、线程或计时器。state显式load。
- native/screen_touchpads.h：snapshot使用before包装；cursorPolicyVersion=12、cursorSubmitOrderVersion=1、cursorTransitionTrace.evidenceVersion=1、contactGeometryCacheVersion=1。
- native/main.cpp：只在与该snapshot配对的g_inputHostMx作用域结束后增加after调用。覆盖未就绪、拒绝、失败、semantic-neutral、无触点与retained；不以accepted或触点存在门控，不为snapshot之前提前返回的BUS路径新增轮询。

实际Delta源码检查8/8：逆向移除上述三个文件中的精确改动后，与本轮Before归一化文本完全一致，证明未额外改动Core、坐标、驱动/Host、震动、焦点、R7几何缓存或报告内容。生产main.cpp原LF换行已保留；git apply曾按现有属性转换为CRLF，已只恢复原LF，记录见production-mutation-receipt.json。

模式在before/after之间变化时，Mouse→Deck可能两次尝试，由已有deadline/pending限制；Deck→Mouse可能本轮两处都跳过，但UI切换处理即时同步，下一次原BUS机会继续。保证针对稳定模式，不声称每个tick恰好一次。

## 验证与证据上限

| 检查 | 结果 | 实际范围 |
| --- | --- | --- |
| 同一时序模拟Before | 19项，恰好4项已知失败 | 原cursor真实头文件；旧主线通知位置由源码对照确认，夹具用前置request/空after适配 |
| 主线After时序模拟 | 19/19 | 强制UI先消费、模拟报告/Steam位置更新、后置request；非真实Host/Steam执行 |
| 既有光标回归 | 70/70 | 真实头文件/API替身，无User32/GDI/输入DLL导入 |
| 几何/显示栈实际原生回归 | 142/142 | 私有桌面、真实WinRT宿主、真实窗口/命中；无物理输入 |
| 完整鼠标穿透/游戏窗口切换模型 | 64轮、1038断言、1600命中 | 自有下层进程；不是真实游戏/触摸验收 |
| 输入边界源码 | 12/12 | Core/路由/ownership/穿透保持 |
| 原子调度/main精确Delta源码 | 8/8 | 证明调用位置及窄范围；不是完整BUS模块编译/运行 |

19项涵盖持续Deck、retained、Mouse和混合模式、双向模式切换、无Host报告、取消、通知投递失败重试及原24ms查询上限。还特意模拟Steam输出比after通知更迟：本轮不伪造坐标、不追加查询，只在原有下次机会跟随。160个8ms BUS机会中，Before/After均53次预算内查询。

隔离模拟不创建真实窗口、钩子、输入或设备输出；原生夹具在专用STA私有桌面完成，实际queue shutdown/worker退出/桌面与自有进程回收、输入桌面保持均为true。没有测量YMCC进程CPU百分比，也未测量真实Steam/DWM视觉结果。全程无部署、重启Steam/游戏、设置变更或设备变更。

## 留档与复现

证据根目录：G:/YeManCC-Work/Build/Analysis/Touchpad-Submit-Order-R8。

- Before/native、Candidate/native、After/native、production.diff：精确增量与冻结来源。
- Simulation-Before、Simulation-Candidate、Simulation-After：19项基线/早期候选/最终主线证据；以最终主线为准。
- Cursor-Regression、Native-Geometry-Regression、Native-Mouse-Pass：最终相关回归。
- candidate-sourcecheck.json、mainline-sourcecheck.json、input-boundary-sourcecheck.json、delivery-receipt.json：来源及检查。
- 调度研究与官方PostMessage合同：G:/YeManCC-Work/Build/Analysis/Touchpad-Flicker-Field-20261015/SD-PATH-DIFFERENTIAL-REVIEW.md。

复现入口：tools/screen_touchpad_report_order_simulation.ps1 -OutputDirectory <fresh-dir>；旧基线加 -CursorSourceDirectory <Before/native> -ExpectedFailures 四项已知名称（见Simulation-Before/receipt.json）。runner明确区分旧通知位置适配器，校验意外DLL导入和来源哈希。源码检查入口 tools/screen_touchpad_report_order_sourcecheck.mjs <Before/native> <current-native> <report.json>。

下一份现场归档须先核对policy12/submitOrder1/geometry1/evidence1。同一PID与活动Deck手势内比较运动段的first/last/min/max/positionChanges及SetWindowPos/raise计数，再结合显隐变化判断。旧policy9归档仍只能说明旧修正目测无效，不能拿本轮离线通过替代现场结果。
