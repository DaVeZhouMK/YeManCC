# 陀螺仪公共睡眠消费者

陀螺仪读取唯一 Native `SharedPowerV1`，使用同一 `g_sharedPowerRevocations`。不生成另一套睡眠代次，不操作公共睡眠恢复事务。`inputCaptureSensorPowerAdmitted` 保留原有传感器 Ready／Resuming 策略，拒绝 UNKNOWN、pending、Suspending、Suspended；显示关闭、模块停止和退出仍有各自的停止条件。

现有 `inputHost::sharedPower::Scope` 以 `Use::Lifecycle` 复用，只提供不可变公共观察及一次性撤销登记。它本身不授予传感器启动权限，始终与传感器策略相与。不使用默认 `Use::Input`，避免把一般硬件写入许可或 InputHost Ready 变成陀螺仪的新前置条件。

冷启用先取得当前观察、派生传感器暂停状态，再执行原 STA Start。旧 OS suspend 的 pending 清理可以继续在原 STA 完成，但不能使之后的合法启用永久停在旧暂停状态。Start 返回后复核 capture、STA owner、sink 和原 scope；初始化发布 ready 后再调用原幂等 Resume，接续初始化期间已送达的公共 Ready。

Resume 的原 FIFO ticket 和失败重试持有同一个公共 scope。新 generation、suspendSerial、通知序号、phase／flags 变化及公共撤销，使旧 scope 永久失效；publicationRevision 只允许前进。同指针重试不自取消，替换观察时取消不同前任。实际 STA 执行前和完成后复核原 scope。公共 Ready／命令完成均不代表物理传感器成功供数。

WinRT 与 SensorAPI 回调各持有原 scope，撤销后不接收迟到样本。保留同对象重订阅、原有一次恢复预算、真实线程归还和未退订资源保护。未扩大 ROG 专用设备恢复，也未修改 Win5 方向能力或校准数据。

`g_sensorPowerPausePending` 只表示待完成的 STA 清理，`g_inputCaptureSensorsSuspended` 表示派生采样准入及资源暂停，不是公共睡眠权威。无需第二条恢复线程或新增轮询定时器。

原回归 ID 为 `sleep.gyro-power`。现有 `power-wake` 入口新增 11 个公共消费用例，使用真实私有 COM STA、FIFO、Windows event 和原公共撤销对象；传感器、Host、设备及共享 reader 的事实输入为测试端口。2026-11-17 已定向执行新增 11 条／56 项断言和受影响原 14 条／55 项断言，原源码晚启用用例在预期断言失败。未执行完整默认父组或物理睡眠，也未部署安装版。

现场证据、修改前后输入和实际执行记录：
`G:/YeManCC-Work/Testing/YMCC/Evidence/Win5-Gyro-Recovery-20261117-152301`。
