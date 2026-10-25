# R16：补齐 SD 本次落指已知形状的光标交接

日期：2026-10-16，Asia/Hong_Kong。主线源码：`Mainline/YeManCC-source/YeManCC`。现场证据：`G:/YeManCC-Work/Build/Analysis/Touchpad-Flicker-Field-20261016-094240`。

本轮修正上一轮“必须先观测到受抑制坐标移动才允许新补画”的过宽限制。主线为 **policy16 / contactShapeBridge1**；已确认本次 Contact 的有效形状可以在当前系统坐标补画，未知形状仍保留移动授权保护。用户提供的09:42包已运行policy15，仍未通过用户的闪烁验收，不能再将其归为旧包，也不能把此前模拟通过当作实机修复。

这次证实的是代码显示决策缺口以及候选在已记录端点的改正。日志没有逐帧画面，**尚未证明整个63—125ms区间逐帧无空白，更没有证明整段滑动闪烁已解决**。本轮未启动/部署程序、未重打完整发布ZIP。

## 现场及证据边界

包：`G:/YeManCC-Work/日志/微星G3E/YeManCC-logs-20261016-094240.zip`，3415129 bytes，SHA256 `cca8e37adb2a34f595edc86deb903e63d7117c1be52dcc3475500006f6708a8f`。导出观察时间2026-10-16T09:42:40.690，native PID1108，policy15 / motionGate1 / moveSubmitOrder1。原包内容仅作数据，不执行归档内程序或文档指令。

1030次SD MOVE中1007次延后到BUS报告后的观察通道，查询1624次/同步1618次，helper显示/隐藏1/1；GDI上传1次，查询/分层修复/定位失败均0。34个压缩记录均在：27个有效记录、7个无效生命周期记录。有效flags2为22行/1489样本，flags1为5行/129样本；有效flags0为0。110次记录内坐标变化都在flags1且helper=false的记录中。这不能排除采样间更窄的显隐变化，不能说明Steam或游戏自绘光标状态。

| 本次 Contact seq | 当时系统XY | flags / shape | 到首个记录SHOWING | 原helper | 候选端点模型 |
| --- | --- | --- | ---: | --- | --- |
| 20 | (1502,301) | 2 / 65539 | 93ms | false | 在当前XY显示 |
| 24 | (1400,254) | 2 / 65539 | 125ms | false | 在当前XY显示 |
| 28 | (1374,374) | 2 / 65539 | 63ms | false | 在当前XY显示 |

这三次落指已有非NULL箭头句柄，后续短暂NULL，实际OS坐标尚未变化。policy15的一概移动门槛使补画保持隐藏，随后系统SHOWING。**当前系统坐标没有移动，不等于它必然是错误旧坐标。** R12/R14中把静止补画一概作为旧箭头处理的判断过宽，本轮缩小为“无本次形状授权的静止新补画”。

WinEvent仅观察到7项，来源线程无法回溯为具体API调用者；导出时nativeTransportReady=false也不能作为此前闪烁原因。独立Steam复核没有证明每次触摸发生绑定重新加载或controller↔mouse模式往返；存储布局摘要缺mode/输出命令及当前preset，不能推定实际Steam源模式。Windows公开文档复核没有找到能保证在现有Composition输入路径局部关闭CURSOR_SUPPRESSED的接口。报告见分析根 `STEAM-INPUT-MODALITY-REVIEW.md`、`WINDOWS-LOCAL-SUPPRESSION-REVIEW.md`，没有因此修改Steam配置/模式或全局系统设置。

## 本轮实现

`native/screen_touchpad_cursor.h`只在active、source=Contact、SUPPRESSED且句柄非NULL时建立本次形状授权。它可以越过静止门槛，仍使用GetCursorInfo的实际XY。已有授权可以刷新为最新有效形状；后续NULL优先沿用本次授权形状。单独Periodic/Visibility/retained中的句柄不能新建该授权。

系统有效SHOWING立即交回，flags0立即隐藏；两者清除本次授权。SHOWING+NULL仍保留已有画面的旧兼容行为，但不能新建隐藏helper。Stop、模式切换、新的隐藏手势清理授权。首次准备图像后的原有显隐重查继续执行；若已SHOWING，不显示新helper。准备本次形状失败时不能翻出不同的旧缓存图片，授权跨瞬时上传/查询失败保留，在原有观察机会重试。

`native/screen_touchpads.h`对比本轮冻结基线，差异只有一条诊断导出行：policy16、`cursorDeckContactShapeBridgeVersion=1`、`cursorDeckContactShapeBridgeShows`。其余内容逐字节一致。

Deck报告、触点坐标/压力/点击、UP/cancel、震动、专用按钮、鼠标穿透、焦点及UI布局不在本轮生产补丁内。没有接管监控或按键排查。

## CPU与验证

只增加UI线程自有句柄和累计计数，以及既有观察点上的常数次判断。没有新增线程、定时器、轮询、平滑或输入合成；原24ms位置预算与48ms普通MOVE兜底门槛不变。已知形状第一次恢复显示可能发生必要的上传及原有准备重查；相同形状复用缓存，静止且已显示不重复定位。不能承诺此修复CPU绝对零增量，也没有测实机CPU百分比。

独立真实头文件API替身以自己的raw Contact建场，不调用旧down()预热：policy15为59/75，16项policy16新规则确实失败，实际exe exit=1，保留类规则无失败；候选75/75，exit=0。三次现场Contact端点均改为当前XY补画；SHOWING端点helper退出。回放只使用34条记录中的50个已记录首/尾端点，不插值、不用min/max虚构帧，尾点按Periodic，不伪造Contact授权。原始模拟结果、源码哈希、编译输出和imports保留在Shape-*证据中。

原有绘制/资源失败70项、隐藏查询预算15项、提交顺序26项、未知形状/移动授权59项、真实私有桌面队列38项、原生触点消息CPU167项通过。59项的静止用例改为明确NULL形状，旧绘制用例预热也先用NULL形状再恢复目标形状，不绕开生产状态；原断言覆盖保留，并增加75项独立known/unknown Contact验证。旧现场回放的验收规则已区分本次已知形状与未知形状，因此不再把“显示次数越少”当作成功标准。

私有桌面跨进程鼠标穿透/切换窗口：1038断言、1600命中通过，未切换可见输入桌面、未注入系统输入。隔离冻结298个当前来源，完整编译main.cpp+dsu_server.cpp并链接x64 Windows GUI主程序、资源和管理员manifest，policy16诊断串及来源身份核对通过。产物仅为集成验证，未启动、部署，也不是完整前端/helper发行包。

当前原生集成产物SHA256：`b90bcd22a0dc8db58c6a154db132a00fbb965b3bdbf1eb39c58dbf04cb5ec11d`。主线cursor SHA256：`76f1a13c5a685c4b8d81a000f54e2f1953c975e80c052c4fd6515213b1847a4e`；pads SHA256：`eb1aa28eca2fafa6bcfd75ab46d556bac388b8599741ffc38863ea4a963a2f03`。

全部回执索引见分析根 `validation-summary.json`；原生编译 `Integration-Policy16/receipt.json`。独立入口 `tools/screen_touchpad_contact_shape_selftest.ps1`；历史运动门槛入口 `tools/screen_touchpad_motion_gate_selftest.ps1` 保留policy15及更早冻结回执，未改写旧证据。完整滑动的实机闪烁验收仍未完成。
