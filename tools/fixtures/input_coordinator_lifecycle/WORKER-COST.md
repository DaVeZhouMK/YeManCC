# 空队列生命周期worker成本的实际消费者入口

在仓库根运行：

```powershell
node tools/input_coordinator_lifecycle_selftest.mjs --out <全新绝对目录> --suite worker-cost
node tools/fixtures/input_coordinator_lifecycle/worker_cost_analysis.mjs --fixture <上述目录>/worker-cost/fixture-result.json --out <全新分析目录>
```

该显式测量suite编译完整生产TU，同一个EXE执行既有12原生模式、5 late-hide模式和worker-cost。它不进入正式Main，不安装驱动、不启动SDK/观察器或真实设备。原有故障对照必须由原断言失败，编译错误/超时不能当故障检出。

与普通fault fixture的一个必要区别：正式构建已开启微星诊断，而fault宏通常将msiUsbNameContextRefresh编译为空。此suite仅在复制的main.inert.cpp改一个条件编译guard，允许YMCC_WORKER_COST_PRODUCTION_METADATA选择原生产元数据函数体；正文、真实ContextPublisher和真实TransactionMutex都不替换。实际编译命令保留6个明确define，source-manifest记录guard修改。私有子进程只为自己建立具PID+birth身份的共享内存映射，样本切换只关闭自己旧映射；无其他进程或观察器参与，来源字符串为人工受控元数据，不是采集到真实手柄。

8输入各15次，trial轮换顺序，预热128次。普通样本30000调用；到期维护样本只执行1次，受计时器成本影响，不与30000循环作直接因果比较。计时含完整inputHostRunQueuedLifecycleWork直到原空队列返回，记录QPC wall、线程cycles和GetThreadTimes；另外保留空循环与inputHostRecoveryMaintenanceTick参照。整个worker已经包含恢复函数M1，不可把两个结果相加。

输入为：空循环、恢复函数参照、普通机型空闲、微星未开详细日志、微星详细日志开启、开启后关闭、微星仅保留失败终态，以及微星500ms维护到期。每次通过真实worker建立私有设置/空ownership ledger状态；只有初次状态构造在计时外。常规循环不冻结其nextTick，真实时钟正好使维护到期也保留该样本，不按速度丢数据。到期专用样本在预热后推进既有受控时钟offset600ms，没有玩家请求或截止，必须由真实原代码推进维护nextTick才算完成。

每样本检查：真实未激活任何target/driver/setup/wake/归还义务；计时前后来源及责任状态不变；没有新增device/transaction/log/queue/Stop/Start端点调用；活动检查数仍0。详细日志开/关后映射状态必须与声明相等，周期维护到期事实单独记录。窗口中只有原代码的维护与诊断元数据发布允许；真实设置文件或设备环境成本未验收。

分析器使用recovery_cost_analysis.mjs同一分位数函数，校验120case/480断言、15个不重复trial、精确calls数、计时换算和隔离字段。所有原样本保留，原始baseline不扣除，median差另列；没有性能阈值。GetThreadTimes短窗口为0不代表零CPU。未测争用、逐锁等待、分配、真实频率或整进程/整机CPU。本入口不能证明物理掌机性能、启停/睡眠验收或硬件信号链完整。

2026-10-22首测见Build/Validation/VC-WORKER-COST-20261022。没有据此更改产品逻辑、限频或元数据发布，也没有覆盖R7运行包。