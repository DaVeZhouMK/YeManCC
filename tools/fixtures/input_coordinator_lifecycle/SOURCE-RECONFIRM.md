# 来源重新确认与 Clear 归还边界

复用 `tools/input_coordinator_lifecycle_selftest.mjs` 的原 `switch-recovery`
消费者。场景定义在同目录 `ps_wake_cases.inc`，原生参数接线在
`fixture.cpp`；不另建测试器、全局 ID、minimum 或发布前置。

先查看明确范围，不编译或执行业务：

```powershell
node tools/input_coordinator_lifecycle_selftest.mjs --plan --out G:\YeManCC-Work\Testing\YMCC\Evidence\NEW-SOURCE-CHECK --suite switch-recovery --case source-reconfirm-collection:adoption-owner
```

确需实际验证时，沿原 owned Job 与任务时间预算执行同一入口，移除
`--plan` 并使用新的 Evidence 输出目录。原编译上限 300 秒、单次模式
运行上限 45 秒保持；外层还须包含准备、校验与归还时间。需要固定
候选时复用原 `--product-candidate` 配套源码清单及身份核对。

| 参数 | 实际选择 |
| --- | --- |
| `source-reconfirm-collection` | 原 14 项，默认范围保持 |
| `source-reconfirm-collection:adoption-owner` | 两项 Clear 竞争与原 exact/MSI 两项控制 |
| `source-reconfirm-collection:post-proof-clear` | helper 返回后竞争 Clear，提交后再实际 Clear |
| `source-reconfirm-collection:route-failed-clear` | route 失败期间 Clear，旧 pending 不得重写 |
| `source-reconfirm-collection:exact-identity` | 原精确身份正常控制 |
| `source-reconfirm-collection:msi-family-peer` | 原 MSI 同族正常控制 |

旧 14 项也可按其原参数名显式选择单项。空名、未知名、多余段以及不支持的
suite/参数组合必须拒绝。显式四项不追加到默认 14 项之后。

竞争观察只包装实际 helper 的返回点与原重新确认日志边界；helper 的 bool、
借用的锁、真实 source/pending 提交和 `inputHostClearLocked` 函数体保持生产
逻辑。另一条真实原生线程只尝试取得 Host，不等待采样锁；观察值不能替代
来源证明或清理结果。

Clear 后实体槽位仍可被正常采样。判断旧 owner 是否退役应核对 source 锁、
source mask、pre-target、pending 和原物理根，不能要求物理连接或普通采样消失。
四项不代替完整 OFF 链、真实设备、驱动或实际睡眠验收。

2026-11-13 已有固定私有来源的旧 4 项／30 断言及新 4 项／33 断言原件。
正式归并核对了相同执行体、原始输入身份及精确选择，不重跑原业务，也不把
引用记为本轮正式 native PASS。证据入口为核心测试层
`Evidence/VC-Win5-Intermittent-20261110-111926/field-111700-r1/fixture-source-wake-r3/formal-clear4/`。
旧原件、外层路径收尾 FAIL 与各自源码 epoch 保留。
