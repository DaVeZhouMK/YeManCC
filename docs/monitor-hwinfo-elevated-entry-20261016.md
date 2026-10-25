# HWiNFO 提权恢复等待修复（2026-10-16）

本轮确认了普通权限恢复入口的一处真实阻塞：提权助手已结束、HWiNFO 已正常常驻，外层助手仍然等待。保存现场时等待已超过 237 秒。原入口的 Start-Process -Wait 会把长期运行的 HWiNFO 后代进程也纳入等待，这与 Microsoft Learn 的 Start-Process 文档一致。

产品只修改 PowerControl/YeManHWiNFO.ps1：提权启动不再使用树等待，改为绑定本次返回的进程句柄，仅等待该助手，启动后的等待上限为 60 秒，随后读取真实退出码。无句柄、超时、无法确认退出码返回明确的 15。超时不终止进程，也不循环启动。UAC 取消仍保留原权限失败码。用户响应 UAC 对话的时间不在这个等待区间内。

配置预检、停止确认、INI 备份与验证、传感器策略、只读共享探测及连续推进证明保持不变。没有新增常态采集、线程或计时器；Presented FPS 继续保留。

新增 tools/hwinfo_child_wait_selftest.ps1 的 34 项检查通过。无害真实 Windows 父子进程测试确认：父进程退出 7 后，等待函数返回 7，而预期常驻的子进程仍在。共享恢复测试替身同步增加句柄、精确等待与释放能力，历史树等待边界仍保留用于对比。

现有回归通过：进程/UAC 29 项、连续生产者 44 项、周期与一致性 48 项、配置合并 26 项、采集策略 32 项、只读共享/健康启动 46 项。

本机仅更新 C:/SOFT/YeMan/PowerControl/YeManHWiNFO.ps1。安装哈希为 A7F6FBBF69339E1ECD227368E65590F2973C51E62E668FD71989CBD2A23B8825。

修复后的普通权限入口真实经过提权恢复，约 9302 毫秒返回 0。HWiNFO 从 PID 51304 切换到 21512，返回后的生产者时间由 1792158880 推进至 1792158883；HWiNFO 继续后台常驻。INI 哈希和 YMCC 原生程序哈希均不变，YMCC PID 保持 14824。随后普通权限连续 3 次 start 均返回 0，PID 与配置不变。最终没有本轮遗留的恢复助手，详细日志门保持关闭。

旧入口在本轮受控重启替换其 HWiNFO 后才自然返回；这不能作为旧入口及时完成恢复的证据。此次只测试权限交接与完成回执，没有执行真实睡眠、等待满 12 小时共享期或测试 CPU 百分比。9302 毫秒是恢复入口完成延迟，不是 CPU 指标。

证据目录为 G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-elevated-entry-20261016。

- FINAL-RESULT.json：最终汇总与源/安装哈希。
- ORIGINAL-ENTRY-HANG.json：旧入口阻塞现场。
- fixed-run/REAL-ENTRY-RESULT.json：新入口的退出码与生产者推进。
- CHILD-WAIT-RESULT.json：边界与真实无害父子进程检查。
- REAL-HEALTHY-START-RESULT.json：三次正常启动。
- REGRESSIONS.json：现有回归。
- FINAL-PROCESS-OBSERVATION.json：最终进程状态。
- DEPLOYMENT-RESULT.json 与 rollback/YeManHWiNFO.ps1：仅助手更新及原文件备份。

官方文档：Microsoft Learn，Start-Process，Windows PowerShell 5.1，Wait 参数。
