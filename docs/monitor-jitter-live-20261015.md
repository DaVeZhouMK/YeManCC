# 监控跳动：本机实测与 G3E 日志对照（2026-10-15）

本轮直接读取正在运行的安装版，并临时开启其原有“显示监控”开关。测试后恢复原来的关闭状态、`#/schedule` 页面，关闭本轮打开的 DevTools；YMCC 保持运行。没有替换安装文件、重启 YMCC、写 TDP、变更 AC/DC 或进行游戏测试。

## 实机证据及结论边界

- 安装目录：`C:/SOFT/YeMan/YeManCC`；进程 PID 14576，版本 0.0.35；buildId `fc38efaa3f9e7d3328250690122a241444bce48ecdbaa9840fd0d5f09d807455`。
- 17:22:41–17:23:42（香港时间）取得 60 份 native `monitor.snapshot` 内存缓存及共用 TDP 显示订阅；17:26:59–17:27:47 临时开启真实自定义监控，取得 45 份快照、预览 DOM 和 `desktopHud.status`。总计 105 份统一快照，无读取错误，没有单独读 HWiNFO 或另建采集。
- 两轮 TDP 均为同一条 `AC / applied / write-ack / 120W` 回执，没有 pending/unknown 跳变。真实监控开启的 45 个样本内预览宽高始终为 369.462524 × 194.343750 CSS 像素，显示字段组成没有变化；原生 HUD 可见、已 armed，`inputTransparent=true`、`inputEnabled=false`。这一观测不能替代掌机触摸或游戏切换验收。
- CPU 功耗、温度、频率、占用、电压；独显功耗、温度、频率、占用、电压；RAM/VRAM 已用、可用、占比及网络、文件读写，在本轮均有连续有效数值。实际台式机无电池、风扇控制未启用，电池及风扇输出保持无数据。45 份数据再次经过正式 HUD 投影，22 项有源标量全部有限有效，独显持续为同一 RTX 5080 槽位。
- 因此，本机这次没有复现用户描述的持续跳动。不能据此声称掌机跳动已实机根治，也不能把台式机 120W 解释为 G3E 的生效功耗。

## 已确认并修正的显示问题

原来的 `createAppliedTdpFeed` 把任意 `pending` 当成即时无数据。每次 `tdpRuntimeStore.begin()` 都发布 pending/null，成功回执又经 1Hz 合并返回。连续浮动写入会导致 TDP 上限先隐藏、再出现，改变布局；底层写入越频繁越明显。

修正位于 `src/bridge/monitorAppliedTdp.ts`，只处理统一回执的显示消费：

- 同一物理 AC/DC 模式已有有效成功回执时，新的写入等待期间显示上一条真实确认值，最长 5 秒。
- 新写入成功后显示后端实际接受值，不显示请求值或保存的设置值。
- 持续 pending 没有新 ACK 时，5 秒到期清除；再次 begin 不能延长旧回执期限。
- 失败、unknown、AC/DC 切换、睡眠或电源状态失效立即清除；首次启动无成功回执也不会补数。
- 多个监控只共享一个原有 TDP 订阅和一个有界到期计时器；最后一个消费者离开时全部释放。原生显示节拍的 `advance` 同样检查到期，可覆盖浏览器后台计时器节流。
- 原始 TDP 控制状态仓、硬件控制过程、数据采集频率没有改变。本轮修正尚未部署到正在运行的安装版。

## G3E 日志的独立事实

来源：`日志/微星G3E/YeManCC-logs-20261015-170538-大幅优化后初步测试.zip`，当前运行 PID 9848。

- 2026-10-15 16:52:40，55W 的 MSI 路径写入取得 `acceptedVerified=true / acceptedPL1W=55` 回执。其验证范围是 OEM 接受值，不是持续测量的整机功耗。
- 16:56:06 起的一笔 49W 浮动请求，在 16:56:23 记录 OEM 读回未确认、代次/期限失效；16:56:24 前端记录 `tdpMsi.set` 超时，16:56:25 后续请求进入 backoff，期间 observedMode 曾为 null。这些失效必须真实传播，不能以隐藏延迟伪装成功。
- 后面 23W、30W 的多笔记录明确为 `apply=false`，只能证明保存配置，不能证明硬件已生效。因此不会用日志末尾保存的 30W 强行填补监控 TDP 上限。
- 日志还有降压 helper 失败；它们虽然同在 tdp.log 域内，不能混计为 TDP 上限写入成功/失败。
- 此包没有可逐帧对齐的 HUD 尺寸记录，所以不能把某一条硬件失败直接等同于用户看到的每一次跳动。

## 另外发现的上游问题

第一轮有 9/60 份 FPS 包关联到 `cl`（编译器），随后 FPS 包消失。共同游戏识别允许按大工作集进程兜底，当前 FPS 发布把统一游戏识别结果与 HWiNFO Presented 数值关联。这个组合可能让桌面后台构建时短暂出现帧率家族，与 TDP 失效是两条不同链路。

本轮未修改共享游戏识别策略，也未在监控消费者中新增一套独立游戏判断。该项保留为已捕获的上游数据归属问题；现有记录不能证明这组 Presented 数据属于编译器本身。

## 验证

- 新增 `tools/monitor_tdp_continuity_selftest.mjs`：10 个场景通过，包括重复写入、后端接受值、5 秒硬到期、失败/unknown/AC-DC/睡眠清除、后台显示节拍推进和订阅释放。使用虚拟时钟与惰性回执，没有真实硬件写入。
- 正式 Vue HUD 回放：13 种外观 × 3 种缩放（100%、140%、200%）× 修正前后，共 78 个组合、每组合 3 次写入。旧逻辑隐藏 117 次，其中立即测得尺寸变化 24 次；新逻辑隐藏 0 次、尺寸变化 0 次。
- 45 份本机真实快照通过正式 HUD 模型回放。
- 原有 `monitor_tdp_runtime_selftest`、`monitor_consumer_lifecycle_selftest`、`monitor_display_demand_selftest` 均通过。工程正式类型检查 `vue-tsc --noEmit` 通过。
- 原生硬件写入逻辑未改，无需以硬件写入验证显示补丁；本轮不包含 CPU 占用基准测试。

证据保存在 `Build/Tasks/YMCC-Steam-HUD-Probe/live-jitter-20261015`：`LIVE-SAMPLES.json`、`LIVE-HUD-SAMPLES.json`、对应 SUMMARY、`TDP-CONTINUITY-RESULT.json`、`BROWSER-REPLAY-RESULT.json` 和 `cleanup.json`。
