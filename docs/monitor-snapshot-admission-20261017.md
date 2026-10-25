# 统一监控快照准入与内部证明有效期（2026-10-17）

统一入口 monitorData 在内层 JSON 解析前拒绝确定退役/乱序的身份，但只有完整数据验证后才允许 acceptLifecycle 提交新会话和序号。不可仅凭身份预检接纳新数据。完整验证后的最终校验必须保留，以处理重入状态变化。

可选 heartbeatAt/hwinfoOkAt 仅接受正安全整数毫秒，不得晚于观察时间；旧数值字符串兼容。非法证明置 null，不清空其他有效硬件/FPS。旧版四文件也执行同一规则。

内部消费时还须校验非负年龄：已准入快照也可能遇到系统时钟回退。autofloat 使用 monitorProofFresh 做 15 秒 idle/6 秒健康判断，且 hwDown 优先。CPU 压力使用现有 monitorCpuFresh，不能用普通 top 时间替代缺失的现代独立 CPU 证明。真实零值保留。

无新采集路径、定时器、产品 EXE 或硬件写入。13 组本轮前后案例及既有 hub/数值/会话/停止/浏览器集成回归通过。1000 个废包由 2000 次内部载荷解析降为 0；1000 个正常包的解析次数和完整输出保持一致。工作量计数不代表整进程 CPU 降幅。当前仅源码与前端候选，未部署。

完整报告：G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/snapshot-admission-20261017/REPORT.md。自测：tools/monitor_snapshot_admission_selftest.mjs。历史停止/会话脚本的深克隆计数需要按已合入的壳层复制契约运行，本轮适配副本和原失败日志均保存在上述证据目录。
