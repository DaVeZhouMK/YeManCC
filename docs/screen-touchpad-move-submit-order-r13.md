# R13：SD 普通滑动的显示通知顺序与 CPU 调用

日期：2026-10-16，Asia/Hong_Kong。证据根：`G:/YeManCC-Work/Build/Analysis/Touchpad-Move-Order-R13`。继续触摸板任务，监控与按键均未接管；按键按用户指示等待另一AI消息。

## 本轮发现

policy14已解决一条可回放的静止旧箭头补画路径，但仍未通过实机整体闪烁验收。继续读实际调用链发现：`cursorContactChanged(...deferDeckMove=true)`虽然不直接调用cursorSync，尾部仍调用cursorRequestSync。采样到期时，UI的普通MOVE可以先排队并处理显示消息，花掉24ms位置槽；随后本帧报告完成后的BUS请求无法再采样。R8只调整BUS通知位置，没有覆盖这条UI尾部旁路。

实际生产头文件替身对照，保留原19项并增加7项，policy14在26项中恰好失败4项：UI MOVE提前消费位置槽、提交后读不到当前坐标、BUS恢复后仍被UI抢先、普通MOVE重复取时。该模型证明允许这种交错，不等于已证明它解释了用户设备上的全部闪烁。

## 保留异常兜底的修正

主线policy15，`cursorDeckMoveSubmitOrderVersion=1`。只改两个触摸板头文件，没有编辑main.cpp或Host/驱动/按钮/监控。

BUS原有after-report请求使用本来就取得的时间，每最多24ms更新一次原子活动时间。该时间只说明这条观察路径有运行，不能证明Steam消费了报告或接受了输入。

稳定的纯SD普通MOVE：已有BUS活动在两个观察间隔（48ms门槛）内时，UI沿用已有tracking/observer状态，避免重复写入，并将本次显示观察留给原有BUS提交后路径。复用Core事件处理已有dispatchTick，不额外GetTickCount64。

没有BUS活动，或其活动已过门槛，则保留原UI请求作为兜底，仍受原24ms预算和pending合并限制。Host有多处提前返回、publication也可能被跳过，不能用删除UI请求或强制进入Host提交来修复顺序。新手势、模式转换、Stop清理活动状态；DOWN/UP、cancel、Mouse/mixed及visibility显隐事件继续保持各自原路径。

48ms为已有时间值上的门槛，不增加定时器或48ms轮询。系统时钟粒度和UI忙碌会影响实际处理时刻。它只约束普通MOVE的排队来源，不宣称所有visibility/接触边沿查询都必须等待本帧报告，也不宣称Host完成后Steam立即完成鼠标合成。

## CPU 调用量

相同的1000ms虚拟时序中，1000次原BUS机会、3000次普通SD MOVE，policy14的MOVE段额外取时3000次，policy15为0次，MOVE段额外光标查询均为0。正常状态下同时避开重复tracking/retained状态写入和无效的UI请求路径。BUS仅在活动时处理固定原子值，每最多24ms写一次活动时间；不创建线程、轮询、日志逐帧记录或动态分配。

这些是API替身调用次数和源代码范围证据，不是YMCC实机进程CPU百分比或功耗测量。不能把3000→0解释成CPU占用归零。

## 输入、显示和数据边界

`screen_touchpads.h`中的dispatchTick仍在同一Core mutex内、同一GetTickCount64位置取得，传入core.event、pointerEventTick的值和顺序不变；仅将局部变量作用域扩大，复用给显示调度。Core/坐标/点击/压力/抬起/震动、输入消费、鼠标穿透、窗口焦点与报告内容未有本轮修改。既有12项输入边界检查通过。

新增导出字段：`cursorDeckReportPulseTick`、`cursorDeckMoveFallbackMs`、`cursorDeckMovePostReportDeferrals`、`cursorDeckMoveFallbackRequests`。它们帮助下一份现场确认正常提交后的延迟路径与BUS停顿兜底，不能据此认定按键故障来源。

R12的motionGate1继续保留：静止新手势不补画旧坐标；实际受抑制坐标变化可以显示并连续移动，授权跨临时失败保留，SHOWING/flags0包括SHOWING+NULL清理旧授权。policy15重跑其59项专用回归全部通过，压缩端点回放仍为0/0补画显示/隐藏。对应回执位于上轮分析根`Motion-Gate-Policy15-Final`。

## 验证

- 同一生产头文件/替身调度：Before26项中4个明确差异失败，After26/26通过。原始失败报告不覆盖。
- 光标完整回归70/70；隐藏查询预算15/15。
- 私有桌面真实WinRT触点/消息167/167，包含点击穿透、层级、几何缓存和恢复。
- 鼠标穿透与切换窗口64轮、1038断言、1600次跨进程命中通过。
- Core/原生输入/穿透边界源检查12/12。
- 原生独立UI/BUS交错回归结果追加在下方，包含新普通MOVE和停顿兜底；非真实Steam或设备输入。

没有系统输入注入、设备改动、真实指针移动、桌面切换、Steam/游戏重启或部署。实机视觉验收仍待证据，目标未标为完成。

盘点开始为13区域、380候选、缺失锚点0、未归类1；并行维护任务继续更新索引。最终盘点为13区域、381候选、缺失锚点0、未归类0。本任务未编辑Steam维护索引，盘点结果不是Steam兼容或实机验收。

## 复现与留档

冻结policy14在`Before/`；调度对照为`Simulation-Before-Final`和`Simulation-After-Final`；两生产头精确差异为分析根的`.diff`；CPU数值见simulation.json的moveOrderMeasurements。更早的初次执行和测试错误均保留，最终结论以命名Final的回执为准。

入口：`tools/screen_touchpad_report_order_simulation.ps1`，Before指定`-CursorSourceDirectory <Before>`及四项已知`-ExpectedFailures`，After用默认主线。`tools/screen_touchpad_native_queue_selftest.ps1`进行真实私有消息队列验证；所有输出使用新目录，不覆盖已有receipt。


## 最终原生队列结果和交付身份

扩展原生夹具共38项，policy14 Before准确失败5项新增断言，policy15 Candidate38/38；原20项在前后均保留且通过。真实UI先处理普通MOVE时，Before查询并推进deadline，随后report未读取新XY；Candidate不提前查询，report后查询一次并使真实私有layered HWND跟随适配器新XY。48ms无BUS活动时兜底仍工作，之后+23ms无查询、+24ms一次查询，BUS恢复同槽不重复查询，下一槽重新按提交后次序观察。一次到期MOVE及其排队工作的头文件取时5→0，和3000MOVE测量的范围分别记录，不能混用。

两轮私有桌面清理、UI退出、输入/主线程桌面保持均通过，没有系统输入注入、真实指针移动或桌面切换。详情`NATIVE-MOVE-ORDER.md`，最终来源为`Native-Before/receipt.json`、`Native-Candidate/receipt.json`。

最终主线cursor头SHA256：`fab9c9dd0534375234b4b3745af6358035a765e96960b2b99731b70a6c55d24b`；screen_touchpads头：`f47d6247f4b84f6a1bf10195864261db7dc31cc9b0ef39e85a67f15bbf279967`。汇总回执：分析根`policy15-delivery.json`。源码改动和隔离回归已完成，现场整个滑动过程的闪烁验收仍未完成，未部署。
