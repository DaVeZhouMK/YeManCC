# SteamDeck 触摸板闪烁实机日志复核与显示顺序修正（policy 10）

日期：2026-10-15，Asia/Hong_Kong。主线源码已修改，未部署或替换正在运行的程序。用户报告 policy 9 目测无改善，仍作为有效的未通过实机验收记录；本轮不能宣称全部闪烁已经消除。

## 实机证据及对旧判断的修正

输入归档为 `G:/YeManCC-Work/日志/微星G3E/YeManCC-logs-20261015-170538-大幅优化后初步测试.zip`，SHA256：`f91f6f5c997da8fd265fb2640d286eac9603c8ab10a137cd8fc49c454c71b4cc`。只读取 ZIP，将指定条目的数据存入调查目录；归档文本没有作为执行指令。

归档混有 PID 7072/9028 的 policy 5 和 PID 7564/9848 的 policy 9，不能把全部累计数据串成一个会话。相关活动来自 PID 9848；导出时刻为 2026-10-15 17:05:37.799。导出外壳不带 PID，其过渡记录与 PID 9848 的同序号/同 tick 内容重合。共读取 73 条触摸板状态记录，按 PID/时间/callSite 去重后 72 条。

导出累计 1,194 个触摸事件、1,037 次 Deck MOVE 查询合并、2,954 次光标查询、一次光标位图上传、七次显示和七次隐藏。有效过渡记录均为 `CURSOR_SUPPRESSED=2 / hCursor=0`，采样 flags 变化、查询失败、layered reset、可见性观察通知均为零。51 条过渡可分成七个生命周期段，包含 22 对接触/释放；不能把七次显隐直接称为滑动期间闪烁。

这些证据没有出现上一轮修复的 layered 丢失/error 87 路径；该修复处理的异常仍成立，但没有解释本次实机现象。

需要纠正的诊断盲点：

- policy 9 的过渡环不记录纯坐标变化。相同落指/抬指坐标不能排除中途移动后返回。
- `cursorDeckPositionOnlyMoves=0` 仅指位置型 UpdateLayeredWindow 分支。前驱 HWND 存在时，原分支通过 SetWindowPos 移动并抬高窗口；它此前没有成功移动计数。零计数不能证明 Windows 坐标或补画光标静止。
- 一赫兹前台日志中的同 PID 与触摸路由要求的精确 owner HWND 不是同一条件，不能据此前台快照把这些手势归为 feedback-only。

上述结论以归档中的字段和源码覆盖范围为限，没有屏幕录制、DWM scanout 或每次 Steam 映射消费证明。Steam 大屏/游戏/桌面配置切换只用作上下文，不修改用户映射或焦点。

## 原生复现确认的显示顺序缺陷

在私有、从未显示的桌面，使用实际 User32/GDI、实际 `restoreScreenLayerPair/consumeOwnerVisibility` 和实际 contact/cursor 渲染，复现以下顺序：

1. 初始从上到下为 cursor → contact → outline → input。
2. 外部置顶窗口抬高后，原恢复逻辑只抬高 input/outline。
3. Windows 的 owned-window 排序与最后的 outline 抬高结合，结果为 outline → contact → input → external → cursor。
4. 后续接触光点普通 MOVE 使用 SWP_NOZORDER；固定鼠标坐标也不会触发光标位置分支，因此错误顺序可以持续。

复现中 contact 在 outline 下面，cursor 在 external 下面。它是已确认的显示层顺序错误；没有直接证据证明它解释用户本次所有连续滑动闪烁，也没有据此宣称 blank frame 已复现。

## 主线改动

`native/screen_touchpad_visibility.h` 在已有外部可见性恢复事件中，只有 input/outline 的 SetWindowPos 实际成功后，才恢复已经可见的 contact，然后恢复已经可见的 cursor。保留原有输入层与边框恢复流程。反馈层重校验不可交互/不激活约束，失败时只隐藏相关显示层并保留既有接触/retained 所有权。显示层恢复使用 `SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE`，不使用 SWP_SHOWWINDOW，因而不会复活已隐藏的光点或光标。

`native/screen_touchpad_cursor.h` 保持 64 条固定环，每段保留初始位置、末位置/时刻、观察次数、位置变化次数及坐标范围。新增 SetWindowPos 成功/失败与 Deck 抬高计数，并从现有 GetWindow 调用保留最后的前驱句柄。句柄只是历史观察，不是耐久窗口身份；不能靠非空前驱断言遮挡。另记录事件触发的 cursor stack raises/failures。

原始查询尝试次数与环内观察次数不同：首次图像准备可能查询两次，但最终只记录一次观察。无效观察保持独立段，不以无效零坐标污染有效段范围。

`native/screen_touchpads.h` 的 `cursorPolicyVersion` 为 10；`cursorTransitionTrace.evidenceVersion` 为 1。没有新增或移动 Steam 接口，不需要为本轮图形修正新增 Steam 接入区域。

## CPU 与功能边界

- 原有 24ms 光标预算不变；没有额外 GetCursorInfo/GetCursorPos、轮询、计时器、线程、预测或平滑。
- 运动诊断随已有采样更新固定内存；静止样本跳过运动范围计算。热路径没有分配、序列化或文件写入；仅既有 state/export 时序列化。
- 反馈层恢复只在原外部事件的层级恢复确实发生后执行；100 次正常光点 MOVE 与重复的已恢复事件均不增加此类抬高。
- 保持 Core/报告映射、坐标、UP/cancel、震动档位、鼠标穿透、输入源启用状态与焦点规则。没有驱动/设备操作、Steam/游戏重启或部署。
- 本轮没有测量 YMCC 真实进程 CPU 百分比；不能给出占用降低百分比，也不能将没有增加调用频率等同于零开销。

## 验证及其实际覆盖

| 验证 | Before | After | 覆盖范围 |
| --- | --- | --- | --- |
| 同一原生显示顺序夹具 | 25 项中四项顺序失败 | 25/25 | active/retained、几何/激活/触点保持、隐藏不复活、无位图重建、重复事件无重复抬高 |
| 实际头文件/API 替身光标模拟 | 64 通过，六项新诊断断言按预期失败 | 70/70 | 保留原 58 项；增加往返运动、32 次强制 SetWindowPos 移动、96 次静止观察、无效样本、64 环覆盖、失败重试 |
| 完整 WinRT 输入宿主、私有桌面跨进程命中 | 本轮不重复旧 Before | 64 轮、1,038 断言、1,600 命中 | 真实 native hit targeting、输入宿主保持、三类布局/多 persona、受控映射适配器；不是物理 Touch/Pen 或真实游戏验收 |
| 输入边界源码检查 | 与本轮 Before 对照 | 12/12 | Core 与受控 pointer 消费保持、Touch/Pen 所有权、不可激活/不注入边界 |

原生显示顺序夹具将 foreground/PID 适配器替换为自有窗口，不调用 SetForegroundWindow；所有排序、样式、几何、位图调用为私有桌面真实 User32/GDI。其输入 HWND 是不含 WinRT 桥的 opaque surrogate，命中检查仅证明反馈层跳过至该输入代理，不能拿它证明下方游戏可点击。完整 WinRT 的下方跨进程命中由另一套 64 轮夹具验证。

调查中曾向该简化夹具添加“直接命中 external”的过宽断言，Before/After 都失败；记录保留在 `Stack-Delivery-*` 与 `Stack-Hit-Diagnostic`。诊断确认命中的是夹具自身的 opaque input。最终明确模型边界，未为使夹具通过而修改生产输入逻辑。最终同夹具 Before/After 均为 25 项，Before 仍准确失败四项已知排序检查。

原生测试确认没有切换输入桌面、没有注入真实输入或操纵系统光标，且私有窗口/桌面已清理。光标 API 替身模拟无 User32/GDI/input DLL 导入。所有这些结果均不能替代用户设备上的视觉验收。

## 留档和复现

调查根目录：`G:/YeManCC-Work/Build/Analysis/Touchpad-Flicker-Field-20261015`。

- `archive-provenance.json`、`export-state.json`、`state-records.json`、`field-summary.json`：归档来源及分会话数据。
- `trace-approximate-walltime.json`：用 recovery 事件名中的 GetTickCount64 与事件日志近似对齐，包含创建/记录延迟；不是精确每触点焦点证据。
- `PRIMARY-SOURCE-CURSOR-POSITION-INVESTIGATION.md`、`steam-cursor-source-excerpts.json`：官方 Windows 合同与已下载 Valve 源码研究。没有证据支持盲目改用 GetCursorPos，因此没有添加配对探针或替代坐标源。
- `Stack-Verified-Before/`、`Stack-Verified-After/`：最终原生显示顺序证据及冻结头文件。
- `Simulation-Final/` 与 `Simulation-Diagnostics-Baseline-20261015-174041-721/`：70 项最终模拟和六项基线诊断失败。
- `Native-Mouse-Pass/`、`input-boundary-sourcecheck.json`：WinRT 跨进程命中与源码边界检查。
- `Before/`、`After/`、`production.diff`、`delivery-receipt.json`：精确增量和来源哈希。

复现入口：`tools/screen_touchpad_stack_native_selftest.ps1 -OutputDirectory <fresh-dir>`；基线比较加 `-BaselineDirectory <Before/native> -ExpectBaselineFailure`。光标模拟仍用 `tools/screen_touchpad_cursor_r1_simulation.ps1`。不要以同目录旧的真实输入注入夹具替代这些入口。

下一份实机证据应先核对 policy 10 / evidenceVersion 1，再区分：有坐标变化且 SetWindowPos/Deck raise 密集、坐标变化但只走位置 API、已有有效查询下完全没有坐标变化、以及新的 query/upload/style 错误。当前只修复可证明的层级恢复缺陷，不改变 Steam 原本可用的输入链。
