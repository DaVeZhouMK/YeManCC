# SD触摸板滑动闪烁：HIDMaestro 2.0 / USBIP复核（2026-10-21）

用户样本：G:/YeManCC-Work/日志/微星G3E/YeManCC-logs-20261021-224432-G3E.zip，SHA256 46428815d1559a2d44aae3eb7a011b521cbe75bf941a428e0f23c824d45a36a3。

本轮完成新日志及固定官方源码审查，未修改运行行为。已有可验证的传输方向，但未找到已经证明能够消除闪烁的补丁。继续保留“直接模拟鼠标正常，只有SD触摸板经Steam合成鼠标闪烁”的用户对照，不能将数据侧排除，也不能归因到某个显隐调用者。

## 本次现场证据

- 22:41:32.848 的Host 2396记录SDK磁盘SHA、长度及加载MVID，对应HIDMaestro.Core 2.0.0.0。SD persona为steam-deck-composite / 28DE:1205，双板左右均steamdeck。singleMode=mouse是未启用的备用项。
- 内嵌USBIP安装器为0.9.8.1；本包缺现场已安装/已使用BUS版本的充分取证。HIDM 1.12固定原件也已内嵌0.9.8.1，不能仅按SDK版本推导BUS升级前后分支。driver inventory的ERROR_UNPROVEN不是已证驱动失效。
- Host详细cost/submission止于22:42:54.696。其后约22:42:55.741起有cursor来源观测，末段滑动约22:43:12.960—22:43:20.444。故Host累计10024帧的零活动触点和零错配，只能描述滑动前覆盖，不能当成本次滑动丢触摸或数据正常的证明。同窗Native→Host frame-accepted ACK仍继续。
- 末64条cursor样本中有60次flags变化；sequence438/439在相同tick10780718、相同坐标1521/514记录flags1→0，hCursor65539→0。这证明系统cursor显隐切换发生；该组样本的YMCC fallbackVisible均false，不证明全时段均未绘制。
- 来源线程TID1220查询到PID1092；末段主要TID2760查询错误5，PID未知。WinEvent来源线程不等于ShowCursor/SetCursor调用者，不能把未知来源直接归属Steam。
- 导出22:44:32.378的Steam三项Kalman快照均0，晚于末段滑动约72秒；不能覆盖故障现场的运行时策略。30500仅为已有静态参考阈值，本次不据此裁剪坐标。
- 同窗存在注入鼠标移动，缺生产者PID；未获逐报告HID完成/Steam读取证据，不能据此断言双鼠标竞争、短暂中立帧、坐标跳跃或丢包。

详细原文件/行号和对应CSV见证据根log-analysis/REPORT.md。

## 升级能力与实际适用范围

固定HIDM2.0 commit 30b8faf0f7de746f249edc5f1520eaa0440965cf；1.12 commit b4d29d9f4b1b91ced6b4755f095184b66339b8be。固定USBIP0.9.8.1 commit 55e1fa7f0c2157017b02dc1f3236e98169a535e4；旧0.9.7.5 commit e1ffd3f974755a70e2290d585486bfbdb11304bf。原件按官方Git blob及SHA256双核，源清单在usbip-review/source-manifest.json。

1. SD reportId=0，HMController._rawEmitsVerbatim要求非零ID，故本案仍走dataLen64/extLen0。非零报告ID误头为鼠标的修复不能套用SD。profile的padY invert、trigger/pressure max声明不作用于当前自写raw帧。
2. 现有Host首次raw前已停止并确认SDK DeviceClock，后续一个写者递增packet counter。2.0新时钟不会自动改善接管后的连续滑动；重开4ms idle会改变节奏，raw重复亦不递增原字节中的packet counter，不能直接照搬。
3. 两版SDK的每事件delivery/ack门仍要求usbipHandle==null，SD不进入。不能给SD移入键鼠SpinWait等待当作交付优化。
4. 2.0的USBIP pump/queue复用缓冲、减少部分逐帧分配，是现有升级可获得的CPU/GC方向；尚无本轮实机CPU量测。共享输入仍是最新快照，FIFO容量仍8，未发现新的辅助鼠标中断补零或自动插入SD中立帧逻辑。
5. 新USBIP相对0.9.7.5强化请求所有权、取消/发送/响应生命周期，并把最终完成交给设备DPC队列。它改变完成调度，但不重写SD接触位/坐标，没有证据说明这已消除当前闪烁。

## 可验证方向：接收模式与交付证据

VhciClient.Attach用零初始化请求，wsk_events=false。USBIP因此走默认zero-copy/IRP接收线程；官方receive_mode::low_latency才把wsk_events设true，走WSK回调+ring buffer。该模式无需提高触摸报告频率，但会多一次缓冲拷贝并改变回调/DPC开销，不能先称其低CPU或必定改善。HIDM当前无公开mode参数，单设备对照需要重建该虚拟连接；本轮不改attach或默认行为。

优先用现成原生WPP定向补证：短窗口10—20秒，UDE provider、level5、GENERAL+USBIP flags0x3，二进制ETL，停止后离线解码；必要时第二窗口加URB/WSK。只按连接代次+devid+seqnum关联CMD IN[3]与RET_SUBMIT，核64字节/短包/零长/取消/无匹配响应。provider没有已确认的源头端点过滤，须解码后筛EP0x83并记录ETW丢事件和开销。

WPP已有点记录收包头/错误，但缺每个正常UdecxUrbComplete成功事件；必要时才核目标系统UCX/IRP事件schema能否补成功完成。SDK的_completionsPerEp在socket发送前递增且漏另一出队路径，不能冒充完整成功IN计数。即使Windows完成，也还不是Steam消费证明。

离线先准备匹配官方PDB/TMF；不为符号替换驱动。不直接执行官方全USB宽范围verbose脚本、不持续全局VerboseOn、不逐MOVE写文本、不新增轮询、线程、timer或刷新率。若后续证据明确指向接收调度，再评审单设备WSK模式A/B。

## 本轮操作边界与证据

只读用户日志、官方源码与当前消费者，保存分析及本说明；未运行产品、SDK或设备测试，未安装/重挂设备、启用trace、真实睡眠、构建、导出或部署。本轮产品行为修改为0，没有宣称闪烁改善。

证据根：D:/Codex/2026-10-21/files-mentioned-by-the-user-yemancc-4/sd-flicker-hidm200-20261021。
main-review/REPORT.md为Host/时钟/profile适用范围；log-analysis/REPORT.md为现场时间窗；usbip-review/REPORT.md为接收模式、completion与原生取证设计。