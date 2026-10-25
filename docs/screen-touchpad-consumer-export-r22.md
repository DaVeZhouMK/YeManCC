# R22：Steam消费端门值只读观测与日志导出接入

2026-10-16。目标仍是虚拟SD经Steam合成鼠标时闪烁，直接Mouse正常；尚未确认修复。R22新增了同版本父机Steam进程的真实只读观测，不再仅凭默认构造/外部console dump推断全局门；并将对应有界读取接入现有日志导出，供后续微星日志带回同类证据。没有改变触摸/点击/压力/坐标、显示、Steam配置或发送频率。

## 纠正之前的进程状态判断

R20将Get-ChildItem与Get-Process混合输出，PowerShell按先前表格列展示，Steam PID列被省略；据此写“Steam未运行”是错误的。R22单独JSON核验：父机Steam为PID20044，创建时间2026-10-15 14:16:38.0235785 +08，FILETIME `134365185980235785`，路径D:/Game/Steam/steam.exe。它不是R22新启动的进程。主线R20文档已更正，历史Build证据保留供审计；不以旧表格支撑进程缺失结论。

## 一次成功的有限只读观测

成功回执 `RuntimeObservation/observation.json`，时间2026-10-16T08:56:21.904134Z。OpenProcess仅PROCESS_QUERY_INFORMATION|PROCESS_VM_READ(0x410)，核验同用户SID、同Windows会话、PID/birth/路径，模块唯一base/size/path与前后进程身份一致，GetExitCodeProcess=259。

模块磁盘SHA固定为 `e9045710ec7ee47d7b7488d1c5dda2d933dec7e251e965967a5c48f4df68eb4e`，loaded PE头machine/timestamp/imageSize与该固定样本相符。另读并逐字节确认hold门59B及override读取13B与固定样本相同。没有重新哈希全部loaded text，不能把“头和选定代码块匹配”说成整个内存映像验真，也不是微星实际loaded image证明。

| 指针槽RVA | 对象+0x18的实际名称指针 | 对象+0x78 float / +0x7C int |
|---|---|---|
| 0x180FE50 | sc_kalman_repeated_sample | 0 / 0 |
| 0x1820D80 | sc_kalman_slowq | 0 / 0 |
| 0x1820E60 | sc_kalman_fastq | 0 / 0 |

两个之前未绑定的构造向量（0x180FEF8、0x1820AA8）该次均为(0,0)。总ReadProcessMemory **464B**，没有计时器、轮询、输入、Steam功能调用、设置/VM写入或设备访问。身份和数据读取为顺序操作，非原子快照；不代表历史或未来值。

这项证据关闭了父机该次的OR-hold外推分支：不能继续默认重复/同单轴样本会走这条路。它不证明整个坐标处理关闭，也不能排除微星故障时的其它门值、普通预测/校正或内部touch-clear。匹配的名称指针还把R21的三个独立CVar名称与上述实际对象连上；这种本机绑定不推广到未知版本/未知进程。

首次尝试的`observation-first-incomplete.json`保留为不完整：它对没有SYNCHRONIZE权限的句柄用了WaitForSingleObject，却未记录具体返回值，错误地产生processAliveAfter=false。该值不能证明Steam退出。随后改用已有查询权限支持的GetExitCodeProcess，得到上述成功回执；没有因错误结果重启Steam。`execution-provenance.json`冻结准确执行脚本，并另保存增加预期birth强校验的未来版本；后者没有再次执行。

## 主线只在日志导出时增加读取

新增 `native/steam_touchpad_consumer_evidence.h`，API为capture(detailedEnabled,context,hashFile,stillBound)。main.cpp仅新增include、导出wrapper、文件注册和清单说明，调用点为既有logs.exportAll。外层复用Steam已绑定session作为进程身份，不调用session准备、controller editor、恢复准入或游戏接口；不要求游戏PID。

- virtual详细日志门关闭时立即输出disabled，不查询进程/模块/文件摘要/内存。
- 开启且明确导出时核验同用户、Windows会话、Steam PID/birth/root、固定DLL摘要及文件stamp、模块边界/身份、loaded指令及固定名称契约后才输出已核全局值。
- 成功路径静态预算为16次ReadProcessMemory、386B，另有16次VirtualQueryEx；上限RPM32次、总量1024B、单次128B、组合API64次。频繁checkpoint仅检查elapsed，stillBound仅在OpenProcess前、hash后和最终身份/模块/文件校验后调用，最多3次，避免读取循环内反复查询注册表/进程。时间限1000ms是checkpoint预算，单次Windows/磁盘I/O不可硬中断，不能声称严格wall-time截止。
- 版本不符、身份变化、读取/预算失败保留不可用原因。结果标记export-time-only、非原子、未证明所选controller/消费/故障归因；错误不会改变输入或触发修复。
- 无新常驻线程、timer或订阅，不在触摸MOVE、BUS tick、Host提交和显示路径调用，因此正常滑动没有新增CPU工作。只有用户原本请求的日志导出增加一次有界诊断。

输出 `steam-touchpad-consumer.json`；Steam维护JSON把新header归到logs-export，总览同步该读取契约。它不能复原故障前门值，更不代替R18逐Host提交边沿或证明Steam最终坐标。下一份日志需按观测时间和PID/birth解释，不能把门值0自动当成输入正常。

## Selector侧增量与明确停止点

`SelectorReview/SELECTOR-REVIEW.md`仅跟随R21完整根和一个直接上层调用：0x620D94传Q=P+0x10，因此Q+0x20=P+0x30。新增2873B完整根加126B调用上下文，仍缺P+0x30的生产者或具名成员枚举元数据。没有把它命名为SourceMode/ControllerStyle；原R21的纠偏继续生效。此独立旁路未读取进程/设备、未改主线。

## 验证与当前状态

原生接线已完成隔离编译/链接、资源与manifest嵌入：NativeIntegration/build-exit.json记录exitCode0，build.log含TOUCHPAD_CONSUMER_EXPORT_BUILD_OK。冻结当前317份输入、22930872B，source-postcheck.json核到冻结0错配/主线0漂移；ProviderBuild独立头文件/W4/WX编译同样exitCode0。Steam索引盘点13区域、396候选、0缺失、0未归类。精确哈希、编译日志与边界由verification.json列明。main-r22-context.diff内的MSI availability变更来自并行任务，保留但不归本轮触摸板修改。没有运行应用/设备测试或数值仿真，没有覆盖Build/App、Release或安装目录，也没有提交其它任务修改。编译不等于实机闪烁改善。

本轮仍未获得微星故障中实际raw touch→内部最终XY/rangeguard的同实例回执；未把只读门值或诊断完成替代最终修复目标。生产输入/显示行为保持，整体任务继续。
