# HWiNFO 只读共享健康检查与本机小补（2026-10-16）

本轮继续检查上游管理链路，确认了恢复助手的访问权限不匹配。数据采集仍归 YMCC 既有唯一接口；没有新增采集器、线程、定时器或周期读取。

## 已确认的问题

`PowerControl/YeManHWiNFO.ps1::Test-HWiNFOSharedMemory` 原来以默认 `OpenExisting(name)` 和 `CreateViewAccessor(0,0)` 打开映射。助手只读取共享数据，却向 Windows 申请读写访问。允许读取、禁止写入的有效共享区因此被判为不可用。

使用实际生产函数、隔离的 Windows 命名映射和当前用户的 ACL 复现：旧探测返回 false，新探测在同一数据、同一权限下返回 true。再通过实际健康启动事务与无副作用的进程边界替身确认：新探测连续 8 次健康启动不执行停止、配置准备、配置复制、注册表策略或程序启动；旧探测会进入不必要的恢复分支。旧事务实测被替身截断，没有为重现问题而重启真实 HWiNFO。

本机普通权限会话也复现了探测差异：旧脚本 `Test-HWiNFOSharedMemory` 与 `Test-HWiNFOHealth` 均为 false；新脚本均为 true，并读到有效的 `Global\HWiNFO_SENS_SM2`、3000ms 周期和新鲜生产者时间。这个对比只调用探测函数，不调用旧助手的恢复事务。

## 产品修复

只改两个权限申请点：

- 映射按 `MemoryMappedFileRights.Read` 打开。
- 视图按 `MemoryMappedFileAccess.Read` 创建。

共享名称候选顺序、布局与长度检查、明确周期确认、同一次读取前后的一致性检查、生产者年龄、连续推进确认、互斥、配置备份和恢复流程全部保持原样。没有提高陈旧数据容忍度，也没有写入上游共享区。

这能避免“原生只读消费者可以读取、助手却因多要写权限而判故障”的分歧。并不意味着可读取就一定健康；陈旧、错误签名、越界、空读数或不匹配的周期仍被拒绝。没有更改 HWiNFO 许可或 12 小时行为。

## 验证

新增 `tools/hwinfo_readonly_health_selftest.ps1`，46 项检查通过。涵盖：

- 真正只读 ACL 允许健康探测，取得的只读视图不能写入；写入后原周期仍为 3000ms。
- 1000/1500/2000/3000ms 合法周期、连续真实头部推进和明确周期不匹配。
- 陈旧、未来、零、负数及不可表示的生产者时间。
- 无映射、错误签名、零读数、越界区域、四个原有别名与不可读别名的顺序回退。
- 健康启动的实际生产事务不做恢复工作；明确周期变更仍返回未应用代码 12。

显式指定旧脚本的前后对比，以及没有历史证据目录依赖的独立运行均为 46 项通过。所有测试映射使用唯一名称；生产映射只在单独的本机只读探测中访问。

相关现有回归全部通过：

| 范围 | 通过用例 |
| --- | ---: |
| 进程恢复、UAC 交接、配置失败与共享边界 | 29 |
| 新鲜生产者及连续更新就绪确认 | 44 |
| 明确采样周期与同次读取的一致性 | 48 |
| 配置合并、编码与上限 | 26 |
| 可控采集策略与强制保留 Presented FPS | 32 |

`tools/hwinfo_upstream_period_selftest.mjs` 的映射替身定位也更新为同时识别旧默认权限调用及新只读调用；替身仍保留生产检查，只替换系统映射打开边界，校验项目未删除。

## 本机部署与验收

已仅替换安装目录 `C:/SOFT/YeMan/PowerControl/YeManHWiNFO.ps1`。安装后 SHA-256：`e9d6724796d7d92e21a3a4cdf3dc228cfd6d699dad8e7e80f6b703c19ef9f06f`，与主线及冻结候选一致。旧文件备份为 `G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\hwinfo-readonly-health-20261016\rollback\YeManHWiNFO.ps1`。

在普通权限、已安装版本上连续运行 3 次 `start`，返回值均为 0。全过程 HWiNFO PID 为 28552，YMCC PID 为 53204；两者均未重启。HWiNFO INI 哈希保持不变。原生 EXE、BAT、上游传感器策略、游戏和用户设置均未修改。

这次没有实际再次关闭共享、执行睡眠或等待 12 小时。与这些场景有关的恢复路径用上述现有回归检查；上一轮真实中断/共享关闭/持续停更实测仍见 `docs/monitor-local-hwinfo-lifecycle-20261016.md`。不把本轮正常启动验收扩大解释为真实到期验收，也没有将其推断为 CPU 百分比降幅。

证据目录：`G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\hwinfo-readonly-health-20261016`。

- `FINAL-RESULT.json`：最终源/安装哈希与回归汇总。
- `READONLY-HEALTH-RESULT.json`：真实生产函数、隔离 Windows ACL 与健康事务检查。
- `clean-selftest/READONLY-HEALTH-RESULT.json`：无历史文件依赖的独立运行。
- `LIVE-PROBE-BEFORE-AFTER.json`：同一本机普通权限会话的真实探测对比。
- `REAL-HEALTHY-START-RESULT.json`：安装版 3 次正常启动的返回码、PID 和 INI 哈希。
- `REGRESSIONS.json` 及子目录：现有回归与实际被测生产源绑定。
- `DEPLOYMENT-RESULT.json`、`source/`、`rollback/`：只更新助手的部署与回退文件。
