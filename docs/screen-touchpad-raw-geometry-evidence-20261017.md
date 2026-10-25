# 屏幕触摸板原始坐标诊断（2026-10-17）

`InputHost/SteamDeckPadSubmissionEvidence.cs` 在既有成功串行raw API返回观察中附加 `RawGeometry`。不改变64字节SD报告、packet counter、按钮/IMU字段、提交频率或触摸语义；数据仍由现有约1秒cost窗口捕获，记录开关保持原 `_costFlagEnabled`。没有新timer/thread/Record内日志、分配或反射。

## 数据能证明什么

`RawGeometry.Left/Right`统计**启用记录期间**每个成功Host raw API返回中touch bit为真的坐标，包含ActiveReports、X/Y范围、InnerSideReferenceReports、OutsidePadDomainReports、ReferenceClassChanges，以及首/末活动点、末次参考区点、末次区间变化点。这些点带CommandSequence、SampleSequence、SourceTick与PacketCounter，方便与外层Host实例、target generation和其他日志对齐。

参考单侧X条件为左X>30500、右X<−30500，边界相等不命中；域参考为[-32767,32767]。30500来自固定Steam样本的`deck_mouse_leftside_filter`注册默认，是**参考条件**，未读运行时cvar或policy。`SteamPolicyObserved`与`SteamConsumptionProven`均false。不能把命中计数叫Steam拒绝次数，不能据此认定光标闪烁根因。

首末等点是历史观察，保留到target generation reset。`RecordingEnabled`和`LatestReportValid`反映最近Record调用的本地记录门/报告结构状态，不是当前UI日志开关读回，也不是按时钟计算的当前新鲜度。判断同一故障窗口仍需SourceTick/sequence、外层时间和generation。未触摸时不会用零坐标伪造范围，empty范围为null。

关闭记录、无效报告、packet discontinuity、pad UP都会打断区间变化比较；合法uint packet wrap保持连续。disabled不会新增报告或几何计数。左右板独立记录，active=false时不读取为活动几何；Reset清掉前一目标的数据。

## CPU与存储边界

每个启用成功报告仅做固定次数整数比较、计数和固定结构赋值；不作滑动平滑/滤波、平方根、按MOVE分配、系统时钟查询或字节数组复制。每侧只保留固定4个小点，原有32项触摸/点击edge环及其omitted计数完全保留，连续坐标穿越不会挤掉UP记录。创建不可变快照及JSON序列化仍发生在既有诊断窗口。未启用记录先退出。

独立自检覆盖严格阈值、左右相反边缘、历史点/JSON、UP/recontact、disabled、无效帧、counter gap/wrap、reset、固定空间和输入字节不变；并保留已有100000次Record分配测量。该类级回放不是实机总CPU测量，也不证明Steam消费或显示结果。

## R40 反编译接线补充

固定 `steamclient64.dll` 的 `absolute_mouse` 处理最后进入 `CInputGenerator::MoveRelative`（RVA `0x714bb0`），构造相对 `INPUT_MOUSE` 并调用 `SendInput` 或 Steam 内部安全桌面队列；这与 YMCC 直接模拟鼠标的 `SendInput` 路径在生产端是两条链。

R40 的生产端→固定 SDK/socket 回放覆盖26帧，全部64字节与Host提交及独立字段参考一致。回放接入恢复的 `30500` 条件模型后，在明确 `policy=4/5`、左/右 source-kind 和状态门假设下复现了“报告 touch 仍为真、模式 contact 被清掉”。该模型不等于实际Steam执行。

进一步静态核对得到：`28de:1205` 在固定VID/PID表中得到内部 type4，但它与模式 setup 的 `r8d/newKind` 语义等价尚未闭合；不能因两个位置都出现数字4就直接套用30500策略。`sc_kalman_repeated_sample` 注册默认为0，启用时任一raw轴重复即可走合成观测后Predict→Correct；默认轨迹未出现越界，显式启用模型有条件越界样本。

这些结果支持“数据接触策略是候选”的判断，尚不支持修改坐标、Steam cvar或生产提交频率。

## R41 消费链补充

R41 固定二进制反编译进一步确认：`absolute_mouse` 中 raw 边缘策略清掉 `mode+0x834` contact 后，`8aca00` 仍可能在上一帧接触和事件状态满足时返回允许，`8acc40` 继续计算并累计 dx/dy，累计器满足条件后仍可进入 `0x714bb0`。因此 contact 状态与相对鼠标位移可能短暂不同步，不能把问题简化成 contact 清零后输入必然停止。固定链内没有直接 `ShowCursor`/`SetCursor`/`SetCursorPos`/`GetCursorInfo`/`ClipCursor` 调用。

R41 还闭合了 `mode+0x220 -> newKind` 的独立来源：配置/记录解析经过 `0xd27ba0 -> 0xd28a90 -> 0xd68ab0` 和表 `0x1418730`，表中包含 `controller_neptune=4`、`controller_triton=10`、`controller_steamos_handheld=100`、`controller_legion_go_s=102`、`controller_zotac_zone=130`。VID/PID 的 `28de:1205 -> type4 -> object+0x8` 是另一条链，不能因数值相同就合并。

这轮将数据侧候选进一步收敛为 Steam 内部 contact 与位移链的状态不同步，仍缺现场故障时间窗的运行时策略、raw坐标、累计输出和可见性状态，暂不修改生产输入。

## R42 设备类型交叉核对

固定 Steam 二进制的 `0x6758b0` 对 PID `0x1203–0x1206` 返回内部码4，对 `0x1302–0x1305` 返回内部码10；`0x674200` 将 PID `0x1205` 的结果经 `0x673e50` 写入设备对象 `+0x8`。独立控制器名称表又将4映射为 `controller_neptune`、10映射为 `controller_triton`。这使 `28de:1205` 进入 Neptune=4 策略的可能性明显提高。

同一路径的 RTTI 可能使用 `CSteamControllerTritonPacketAbstraction`，说明协议抽象类名称与控制器家族名称可以不同；不能用类名覆盖 policy 名称。固定样本仍未给出共享源代码 enum 的直接别名，也没有现场运行时对象和 cvar 证据。

## R43 旧归档边界复核

对工作区微星 G3E 的12份本地 ZIP 重新扫描后，只有 `2026-10-16 18:47:36` 包含旧版 `steamDeckPadSubmissions`，共190个 V1对象；所有归档都没有 R40 的 `RawGeometry`，也没有右板连续 active 坐标。旧版环形记录不能判断故障时是否越过 Neptune 策略的 `-30500` 右板门。

因此旧日志缺字段只能作为版本/部署证据边界，不能解释为现场没有触摸。要验证30500候选，必须取得包含 R40诊断字段的新归档。

## R44 Steam 与直接鼠标元数据差异

固定 Steam `0x714bb0` 发送零初始化的单个 `INPUT_MOUSE`，使用 `MOUSEEVENTF_MOVE`、`dwExtraInfo=0`，没有 `MOUSEEVENTF_MOVE_NOCOALESCE`。YMCC 直接模拟鼠标同样使用相对 MOVE，但写入 `dwExtraInfo=0x594d5450`，并在状态变化时可能合并多个 INPUT。该差异可能影响外部 hook/Overlay 分流，但当前固定 Overlay 证据尚未把 `0x714bb0` 接到 cursor tag8/ShowCursor 链，因此暂不修改直接鼠标路径。

## 与现场/导出版本关联

本轮R39已通过归档稳定正文SHA将桌面右板绑定到存储`absolute_mouse`，并从固定Steam代码恢复出原始坐标接触门。实际运行时preset/policy/cvar和故障时段仍未采到。R40导出链复查纠正了提交证据清单：2026-10-16 18:47:36 G3E包有190个非空V1 `steamDeckPadSubmissions`快照，环形条目去重后7项，其中3项左板touch有效、0项右板有效；它们保留触摸/点击边缘，不能还原连续滑动。R39中SDK缓存观察没有active-pad样本是另一组稀疏观察，不能与Host提交证据混同，更不能倒推出故障时没有触摸。前五包不含该提交字段，具体历史Host身份未确定。

本文件是诊断接口说明；仅源码包含此类型或独立自检通过，不等于用户设备已运行此构建。新日志应核对主应用/InputHost的部署身份，并实际出现`steamDeckPadSubmissions.RawGeometry`。整包导出时保留当前完整InputHost源码与正确产物，勿将独立编译DLL直接当成已验收部署包。

证据位于 `Build/Analysis/Touchpad-Consumer-R40-20261017`；固定Steam原始代码依据见R39的`ConfigReview/ContactGate`。本项不改变Steam设置、焦点、设备、虚拟手柄SDK或YMCC全局UI。
