# 内部温度消费统一校验（2026-10-17）

统一顶部投影可能在 HWiNFO 失联时保留独立 Windows 数据，同时用 hwDown=true/tempC=0 表示硬件数据不可用。内部消费不能只 Number(tempC) 后计算曲线。

monitorSnapshotSignals.monitorCpuTemperature 对已投影 top ref 检查 hwDown、cpuTempValid=false、真实有限数字和 0..150°C 范围。FanView、fanFeature 导航及 TopMonitorBar 共用这一判断。缺失时页面温度和开启状态下的期望转速显示无数据，不将失联零值当成温度。正常真实零值和旧版未带有效标志的数字保留。

helper 无 IO/时钟/定时器，依赖统一投影的已有有效期，不为 ref 续期。导航是否转动仍由认证控制回执决定；该修复不修改 Host 采集、安全控制、启停、实际风扇写入或睡眠恢复。

验证：21 组实际源码前后检查、三种合成设备身份的 21 项真实组件浏览器验证，以及既有 feature/数值/快照/内部控制需求回归通过。200 次失联更新的曲线插值访问从 200 到 0，不是 CPU 百分比测量。原扩展浏览器 OFF/迟到回执步骤曾超时，本轮未解决或验收，最终定向套件明确不覆盖该部分。

源码与前端候选已更新，未部署。完整报告：G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/shared-temperature-consumers-20261017/REPORT.md。
