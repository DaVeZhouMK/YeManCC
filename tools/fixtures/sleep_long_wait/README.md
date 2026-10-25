# 睡眠相关长时间恢复异常：原始日志数据夹具

本目录复用现有睡眠工程，仅保存普通导出日志中的字段与明确预期，供后续故障复现和分类检查引用。它没有可执行场景，也没有证明产品已修复或 Windows 已达到有效低功耗。传感器软件修复另见既有 `sleep.gyro-power` 场景。

| 文件 | 实际证据 | 判断边界 |
| --- | --- | --- |
| `wake_ui_dual_channel_unresponsive_20261001.json` | 唤醒后19次窗口/WebView双通道无响应，观测跨度96.292秒；其中16次越过唤醒宽限期，跨度84.366秒 | 心跳新鲜不等于UI可响应；服务pid不等于YMCC targetPid；未定位具体阻塞函数 |
| `fan_recovery_failures_across_retries_20261015.json` | 风扇恢复失败过程跨6分36.192秒，3次约15秒deadline失败及1次关闭取消 | 不能把整个过程说成一次调用连续阻塞；Ready但电源Unknown/代际不匹配不能放行 |
| `watchdog_standby_overlap_20260930.json` | 105次确认跨7分3.183秒，但完整落在内核记录的待机会话中；5次system-sleeping延期、7次wake-grace延期和93次生命周期观察混在期间 | 保留为清醒状态未核实的重叠对照；不能据此宣布清醒卡死7分钟，也不能宣布有效低功耗成功 |

日志时间保留原格式，不推定时区。`sourceArchiveSha256`、`member` 和一基 `line` 可回查原件。显式 `[redacted]` 保留脱敏值。完整受限时段证据、来源成员哈希和计算校验见：

`G:/YeManCC-Work/Build/Tasks/GyroSleep-20261021/SleepEntryAnalysis/ArchiveSearch-20261021/REPORT.md`

已有快速入睡/唤醒、风扇重复睡眠意图、原生非用户唤醒等夹具的覆盖与局限，沿用上一级 `SleepEntryAnalysis/REPORT.md`；本目录不新建全局测试注册或产品状态机。
