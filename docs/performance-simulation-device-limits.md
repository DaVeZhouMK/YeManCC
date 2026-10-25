# 模拟机型功耗上限

以下数值来自当前模拟能力库，属于模拟封顶与数据覆盖范围，不是所有同芯片设备的硬件保证。

| 机型 | 模拟有效TDP上限 | 原始参考曲线平台 |
|---|---:|---:|
| AMD Custom APU（Steam Deck） | 16W | 16W |
| AMD Ryzen 7 7840U | 45W | 35W |
| Intel Core Ultra 7 258V | 45W | 30W |
| Intel Arc G3 Extreme | 80W（短时睿频范围） | **未确认；35W以上待校准，已有官方45W持续设备** |
| AMD Ryzen AI Max+ 395 | 80W | 80W |
| AMD Ryzen Z2 Extreme | 45W | 35W |
| AMD Ryzen 7 6800U | 45W | 28W |
| AMD Ryzen 7 5800U | 45W | 28W |
| Intel Core i7-1195G7 | 45W | 28W |
| Intel Core Ultra 7 155H | 45W | 35W |
| AMD Ryzen Z2 Go | 45W | 28W |

设定值可保留到150W，但模拟控制器、CPU/GPU供给、核心切换和统计使用不高于该机型上限的有效瓦数。自动节能的相对降幅先换算到有效上限，避免150W影响搜索过程或产生额外提升。

原始参考平台仅表示只读输入曲线已无进一步增长；经过CPU共享功耗分配与优化余量收敛的运行供给仍可能继续追上可行能力。CPU或调度仍可能在模拟上限以内变化。提高瓦数可无额外收益，但同场景、同CPU参数下不能因优化幅度缩小而扣减GPU绝对性能。G3E的35–80W缺少同条件TS数据，旧数值是保守占位，不能将其解释为已经确认的性能平台。官方X2 Mini与APEX Air均标称45W持续释放；Intel的80W为最大睿频功耗。详细依据见`performance-simulation-g3e-evidence-20261017.md`。

快照powerLimits分别记录requestedWatts、effectiveWatts、maximumWatts和clamped，正式配置不被模拟封顶改写。
