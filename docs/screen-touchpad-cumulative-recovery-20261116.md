# 触摸板跨恢复窗口的升级（2026-11-16）

Win5 `20261116-162354-10484-5825828-2` 的运行编译标记为 `5ac3e5a590e53029a7c1393056b38e144e9d5039b6733e0573f3242507d5f4df`，writer10484／出生时间 `134392901619393799`。它与历史074437Z导出包一致；实际source-symbols ZIP中的11份触摸板源码逐字匹配上一轮SourceReady。因此这份日志确实包含上一轮修补，不用后来的164319包倒填证明。

实际反馈有改善：16:17:21唤醒后两次检查healthy／repair0；16:19:22、16:19:58、16:20:01均有后发故障恢复。它们在同generation5013642／suspendSerial1内分别使用累计repair1、2、3，说明健康检查不再阻止后续自愈。但每个窗口都只用了自己的repair1，runtimeRebuilds始终0，暴露出恢复升级仍按单窗口计数的缺口。后续新电源范围还记录了两次恢复。时间沿原日志香港时间。

此次只修恢复阶梯：第二次**累计实际修复**进入触摸板自有资源重建；同generation＋suspendSerial最多重建一次，不因窗口完成而重新计数。总修复上限仍3次、普通故障窗口上限仍3次、每窗5秒；新电源范围有独立额度，手动OFF/ON和诊断发布不退款。原单窗口第二次修复的升级仍覆盖。已有回执的faultBudget增加runtimeRebuilds，方便区别“本窗口修1次”与“本电源范围已升级”。

新日志进一步定位到 `sync-input-process-events`，故障发生在独立输入通道ProcessEvents阶段，不能再统称Dispatcher getter失败。该Windows `0x8000FFFF` 的内部诱因尚未复现，本次不删除ProcessEvents、不忽略错误，也不宣称资源重建就是底层根因修复。

导出16:22:54触摸板error0、原生输入active，事件566；autoHidden=true、外观隐藏符合15秒自动隐藏，不能因此判“再次消失”。恢复回执里的lastError和按钮failed21是历史数据，不能当作导出时仍失败；这些当前标志也不是实际游戏像素或物理触摸证明。

复用原 `screen_touchpad_button_abort_native_selftest` 的互斥选择 `--cumulative-only`／`-CaseFilter cumulative`。旧源码在第二个独立窗口必要失败；候选32项通过，包含三个独立窗口、第二次累计重建、第三次不重建、第四次拒绝、额度不退款、真实关闭合成器后的升级、共享peer与DispatcherQueue保持，以及退出三次释放确认。两个实际编译图各644输入，根业务旧exit1／候选exit0；均以准确vctip工具收尾、关前Job0，raw businessSucceeded=false保留，不称自然退出。

原late-fault选择中受影响的第二窗口与剩余额度断言已同步，未重跑旧22项、64轮风暴或完整Native。没有新增常驻定时器、线程或轮询，也没有更改SD光标策略19、震动、鼠标穿透、输入映射、公共Steam／睡眠／监控。未部署或操作现场应用、设备和睡眠，掌机CPU收益未测。

权威证据：`Testing/YMCC/Evidence/Touchpad-Wake-Recovery-20261111/field-win5-162354-20261116`。下一自然Native／runtime／source-PDB消费 `Build/Exports/PendingSources/TouchpadCumulativeFaultEscalation.json`；上一轮Pending中仅恢复头和两份原夹具的pin被前向接续，其余产品和显示验证仍独立保留。实际Win5是否停止复发需要匹配版本现场日志确认。
