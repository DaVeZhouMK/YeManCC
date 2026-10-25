# 统一监控重复载荷数组共享优化（2026-10-16）

本轮继续优化 RTSS 高频更新经过统一数据层时的重复数组复制。已有 decoder 会对相同 top/fps 原文跳过 JSON 解析，但每次仍复制 coreUsage、coreLabels 和 frameTimes。

## 修正

相同 wire 文本再次到达时，decoder 现在返回新的外层对象，同时冻结并共享已完成边界限制的数组。这样停止意图可以继续只修改当前外层对象，不会污染前一份快照；数组内容属于只读消费数据，不再重复复制。

上限保持不变：核心数组最多 128 项，帧时间最多 120 项。没有改变数值校验、有效期、HWiNFO 断开、FPS 零值或自动隐藏逻辑。

## 验证

- 生产 monitorData 重复载荷检查：6 项通过，外层对象独立、核心负载/标签/帧时间数组共享且冻结。
- 统一消费生命周期：13 项通过，51 次发布、一次 read、最终定时器数为 0。
- 数据链路健壮性：31 项通过。
- 全仓 vue-tsc --noEmit 通过。

没有执行 CPU 百分比压测，没有修改硬件读取、轮询线程或安装文件。持续优化目标保持进行中。

结果：Build/Tasks/YMCC-Steam-HUD-Probe/monitor-payload-array-share-20261016/FINAL-PAYLOAD-ARRAY-RESULT.json。

## 后续校验：真实非空历史（同日）

后续发现原测试 frametime 与历史末点不一致，曲线被正确拒绝为空；原引用相等检查不能证明完整历史共享。本轮修正 fixture 末点并加入长度 120、publisher → model 引用身份及冻结图块断言，共 12 项通过。原脚本保留，详见 [监控只读数组复用与回归核对](monitor-owned-arrays-20261016.md)。
