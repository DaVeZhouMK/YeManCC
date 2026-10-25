# YMCC / HWiNFO 历史平均窗口预算（2026-10-17）

主线托管配置把 SensorAveragingInterval 由用户基础模板中的 1000 改为 HWiNFO 默认的 0，使用不遍历历史缓冲的平均值分支。SensorInterval=3000 和 PresentMon=1 保持原样。

YMCC 原生统一采集、现有 FanHost 温度控制及三套 RTSS 模板的 HWiNFO SDK 取数路径均读取当前 Value，不依赖历史 ValueAvg。Presented FPS / LOW / Frame Time 名称里的 avg 属于当前指标本身，应保留。

历史 Average 的语义明确改变，不能承诺它保持过去 1000 个样本窗口的结果。后续新增消费历史平均值的功能时必须重新评估。此优化没有新增采集器、产品 EXE、配置 UI 或特殊设备规则。

主生产路径在 native/monitor_hwinfo_config.h，兼容恢复入口在 PowerControl/YeManHWiNFO.ps1，二者必须同时维护。托管基线回归只允许这一项相对稳定模板的额外例外；不要放宽其他配置和恢复事务断言。

验证：64 个原始指令隔离案例，10 个真实原生解析/发布快照案例，三套 RTSS SDK 路径共 860 个当前值断言，306 个原生/PowerShell 配置一致性输入及恢复/采集回归通过。310 个冻结原生输入编译通过。没有测整进程 CPU 百分比，也未部署；本机运行配置仍为 1000。

完整证据位于 G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-average-window-20261017/REPORT.md 和 ROUND-RESULT.json。复核工具是 tools/hwinfo_average_window_emulation_selftest.py、tools/hwinfo_average_consumers_selftest.mjs。
