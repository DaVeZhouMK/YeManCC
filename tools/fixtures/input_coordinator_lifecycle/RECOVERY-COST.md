# 恢复维护成本的实际消费者入口

在仓库根运行：

```powershell
node tools/input_coordinator_lifecycle_selftest.mjs --out <全新绝对目录> --suite recovery-cost
node tools/fixtures/input_coordinator_lifecycle/recovery_cost_analysis.mjs --fixture <上述目录>/recovery-cost/fixture-result.json --out <全新分析目录>
```

该 suite 用同一个实际生产 TU 编译候选执行既有12个原生模式、5个 late-hide 模式和新增 recovery-cost 模式。entry 的两种原定故障对照仍要求原有断言失败；编译失败、超时、源漂移不是业务故障对照。无需运行正式 YMCC、Steam、SDK 或设备。

新增模式是测量，不按 CPU 快慢设置 PASS 阈值。8种输入各15次、每次30000调用；状态顺序按trial轮换，先预热128次。计时包含完整 inputHostRecoveryMaintenanceTick 与显式间接调用循环；空函数循环单独保留。构造状态、预热、JSON、日志和断言不计入窗口。记录 QPC wall、QueryThreadCycleTime cycles 和 GetThreadTimes CPU时间，输出未经基线扣除的原始计数。分位数和基线差由分析器另算，不丢弃慢样本。

状态分别为：空循环、八项全清、仅terminal、仅attempts、terminal加attempts、仅fallback=2、仅fallback=3，以及实际admit→candidate→verifying后的已知外接优先暂停。前几项是受控状态输入，不伪称重演了真实故障来源；活动态逐次校对检查计数增加30000，预期暂停不能被当成故障触发Stop/Start。

生产恢复函数与被测helper正文不替换；TransactionMutex使用原模板和真实std::mutex，时钟沿用既有真实GetTickCount64端点，未启用交错回调。活动态的保护策略时钟固定在该窗口，方便测每次检查成本；因此它不是30秒生命周期时限验收。各次样本必须保持声明状态，且设备IO、日志、队列、Stop/Start端点不能新增；否则样本失败，分析器拒绝。

本入口未测锁竞争、逐锁等待、分配次数、完整worker、实际调用频率或整进程/整机CPU。计时器API、间接调用和现有夹具端点有测量开销；原始数据与空循环一并保留。不可由此删健康检查、延长预算或声称产品CPU下降。需要优化时先检查真实状态来源与正在执行的责任，再做原语义回归及同机前后量测。