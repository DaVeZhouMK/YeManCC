# R120 bounded owned-return settlement sidecar

范围仅限真实 Maintenance 的 physical-signal-confirmed 结算/发布、TryDischargeReturnDebt、AfterModeDischarge 与 StoreLocked。生产与 existing tests 零修改；不复制父级 ReadSignal 的证据判断。

截至 2026-10-16 的实际执行：Release/Checked 各 33 case / 389 assert，9 种 during-store 原子变化均观察到旧债完成。最终解释是 **未证明持久契约违反；本侧不建议生产修改**。所有事实与限制以证据根 `FINAL-REVIEW.md` 为准。

`harnessPassed` 或历史 `passed` 只表示 fixture 的预期观察/控制吻合。历史运行中的 `expectedGap` / `REPRODUCED_SETTLEMENT_CONTEXT_GAP` / `settlementSafetyPassed=false` 解释过强，已撤回。不能当作安全红账或绿账。

测试未在持有 Host+pump+owned+crash 锁期间执行真实新 mode2、新 episode 或新 journal。旧责任 store 前已证实物理归还，允许线性化完成；新 mode2 由生产写前持久认领另建责任。exit/intent 的撤销并不自动推翻已完成旧事实。

source/topology 标量变化保留为观察：没有证明真实 producer 造成必须追踪的新归还责任在旧结算后不可重建。缺少实际 source producer/设备模式转移/义务来源的因果链，故该性质 UNKNOWN。不要据此增加补写 pending、额外设备重试或回滚。

runner：`node tools/owned_return_settlement_r120_selftest.mjs`，输出固定到：

    G:/YeManCC-Work/Build/Validation/VirtualInput-ReturnClosure-R120-20261016/settlement-sidecar

最终交付后按用户要求停止测试与写入。最后一轮实际执行冻结 runner/fixtures 见时间目录 `2026-10-16T15-11-46-939Z/fixture`；当前 fixture 仅纠正结果标签/报告解释，未再执行。执行行为和用例集合未追加；验证数字只归属冻结版本，不冒充当前文件重跑结果。

边界：ReadSignal 直接输入合格 match/sequence/time/identity；sgWriteFileAtomic 为内存持久化 leaf；其余被审计生产函数逐字编译。设备分支 stub 一旦到达会抛错。无产品、Host、设备、服务、Steam、睡眠/电源动作。source-binding.json 列出全部冻结源与函数 SHA。
