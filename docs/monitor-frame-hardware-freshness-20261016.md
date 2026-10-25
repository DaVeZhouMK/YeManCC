# RTSS 补位与硬件数值独立有效期修复（2026-10-16）

实际复现：HWiNFO 的 FPS／硬件包已经过期，RTSS 提供了新的 FPS。原 `Output::packet()` 复制保留的 HWiNFO JSON 后仅改 FPS 相关字段，GPU 占用 97 和 CPU 功耗 31W 继续随新的包时间输出。自动浮动功耗也读取这一包中的 GPU 占用，因此这不只是 HUD 显示问题。修复前现场夹具见 `BEFORE-STALE-HARDWARE.json`。

## 产品变化

- `native/monitor_frame_output.h`：硬件指标带独立的 `hardwareObservedAt`、`hardwareValidUntil`、`hardwarePollPeriodMs`。期限使用既有硬件健康预算，来自生产者原始轮询时间，重复读取不会续期；RTSS 更新时间不改变该凭据。读取失败立即撤下 GPU 占用和 CPU 功耗，FPS／LOW／曲线仍各自保留原有期限。无效功耗不再变成可用的零值。
- `native/main.cpp`：消费既有唯一采集线程的同一份硬件样本。Presented FPS 可用时继续主源优先；Presented FPS 缺失时仍传递正常的 GPU 占用／功耗供 RTSS 包使用。不增加硬件读取、采样线程、定时器或独立 EXE。正常分支每份样本只处理一次。
- `src/bridge/monitorData.ts` 和 `monitorFpsHardware.ts`：统一快照与旧文件回退均验证独立凭据、当前健康和停止时间线。缓存包不能因 RTSS 时间更新获得新的硬件期限。旧 HWiNFO 包在原有观察期限内兼容；旧 RTSS 包没有可证明的独立硬件时间时只撤下这两个值。上方统一硬件数据继续正常消费。
- `src/bridge/autofloat.ts`：实际控制消费时再判断硬件凭据，防止持有的视图在使用时已经过期。GPU 数据缺失时沿用现有的“不加 GPU 占用限制”行为；不修改控制档位。
- `monitorDisplayFeed.ts`：旧文件显示去重包含新增凭据，避免只有证明时间变化时被误当成相同内容。

## 对比和验证

生产头文件、实际原生硬件接入分支共 24 项；统一接口、文件回退、实际自动浮动消费函数共 13 组。包括硬件正常但没有 FPS、停更、立即读失败、同秒重复、周期更正不续期、未来时间、进程复用、畸形凭据、无效功耗、真实零值及恢复。

既有 1,942 个原生输出包继续对比：FPS／LOW／曲线／游戏身份的序列化内容完全一致。新增硬件凭据单独核对；只对明确失效／无效的 GPU 和功耗允许差异。用例也保留精确数值检查，未用忽略整个硬件对象掩盖变化。正常 1000／3000／6000ms 三种节拍各 300 次，以及 300 次过期包路径，仍为零次堆分配；对象大小不变。这是确定性操作检查，不是 CPU 降幅测量。

回归通过：共享数组、无效载荷缓存、读源所有权、快照体积预算、RTSS 普通工作分支补位。旧通用 `monitor_data_selftest.ts` 因早期固定字段集合与当前电池／CPU 字段不一致失败；同一测试换回本轮修改前的 `monitorData.ts` 在相同位置失败，保留对比日志，不将其记为本轮通过，也未为了全绿修改其断言。

从原 ZIP 重新读取 G3E 454＋204、Win5 58、ROG Xbox 276 条，共 992 条硬件记录，送入实际生产 `hardwareMetrics()`。990 条健康记录无新增丢失，2 条不健康记录按预期撤值；异常差异为零。该回放使用合成 RTSS 载体来检查硬件字段，不声称复现历史 FPS 或历史恢复过程。

## 本机部署与实测

已备份后更新 `C:/SOFT/YeMan/YeManCC` 的原生程序、前端入口及需要的新哈希资产，共 27 个文件。旧哈希资产保留；设置未写入。原生和前端均从冻结输入构建，包含冻结时并行任务的修改。安装 27 项 SHA-256 与产物一致。未新增独立助手 EXE；本轮未改 HWiNFO INI 或 BAT／PS1。

新原生 SHA-256：`798b10dc4bdb05d8c33a180afd06e3546c887529acfae69f5b7b343e3c9ce0d8`。

实机用已运行的 Cyberpunk 2077 和原 HWiNFO PID 21512。在确认精确进程身份与路径后，通过进程句柄暂停约 9 秒，并在 finally 恢复同一进程。读取现场已开启的高频诊断中原生统一缓存，共 45 条：

| 阶段 | 条数 | 结论 |
| --- | ---: | --- |
| 硬件正常／Presented FPS | 38 | GPU 与 CPU 功耗在各自期限内有效 |
| 硬件仍有效／RTSS 补 FPS | 1 | 两项硬件值保留，避免过度修正 |
| 硬件不健康／RTSS 补 FPS | 6 | FPS 继续，GPU 与功耗撤下 |
| 恢复后的 Presented FPS | 2，包含在前述 38 条内 | 原 HWiNFO 恢复生产并重新成为 FPS 主源 |

独立凭据、选中 RTX 5080、有效 CPU 数值、有界核心／帧时间数组检查无异常。诊断仍为原有高频门、1Hz、每文件 2MiB、保留 2 文件。本轮没有更改现场日志开关；现场当前为开启。请求的 WebView 调试端口未开放，因此没有把 CDP 连接失败宣称为前端在线验收；消费者使用真实生产代码夹具验证，实机部分来自实际原生统一缓存。

最后正常重启 YMCC，已确认新进程 PID 32340、WebView 导航完成与可用回执。HWiNFO PID 21512 和游戏 PID 43508 保持不变。未做真实 12 小时到期测试，未对自动浮动执行实际功耗写入，未测 CPU 百分比。

## 证据与后续

证据目录：`G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/frame-hardware-freshness-20261016`。

- `FINAL-RESULT.json`、`DEPLOYMENT-PLAN.json`、`DEPLOYMENT-RESULT.json`、`NORMAL-LAUNCH-RESULT.json`、`rollback/`。
- `FRESHNESS-RESULT.json`、`NATIVE-FRESHNESS-RESULT.json`、`ARCHIVE-HARDWARE-RESULT.json`。
- `LIVE-STALL-AND-RESUME.json`、`LIVE-PAUSE-RESULT.json`、`LIVE-DATA-AUDIT.json`。
- `SOURCE-MANIFEST.json`、`FRONTEND-SOURCE-MANIFEST.json`、构建日志和 `regressions/`。

HWiNFO 配置／启动／恢复事务仍有 BAT／PS1 实现，本轮未将其冒称为已经回归本体。下一步继续迁回 YMCC 原生本体，保留此前已验证的备份、权限交接、共享关闭与中断恢复边界；不增加助手 EXE。整体数据和 CPU 优化任务保持进行中。
