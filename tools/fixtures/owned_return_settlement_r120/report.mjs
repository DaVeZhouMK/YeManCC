export function reportText(result,binding) {
  const profiles=Object.entries(result.profiles).map(([name,p])=>`| ${name} | ${p.caseCount??'-'} | ${p.assertions??'-'} | ${p.duringStoreObservations??'-'} |`).join('\n');
  const hashes=Object.entries(result.sourceSHA).map(([name,value])=>`| ${name} | \`${value}\` |`).join('\n');
  return `# R120 bounded settlement sidecar — observations only

Verdict: ${result.verdict}

这套 fixture 只证明：持久写期间的 9 种原子 token 变化，不会阻止既有、已证物理归还的旧债完成结算及发布捕获 tuple。不能据此认定持久契约违反，不能把 harness passed/expected observations 当作安全通过或失败。settlementSafetyPassed=null 表示未据此裁决。

| Profile | Cases | Assertions | During-store observations |
| --- | ---: | ---: | ---: |
${profiles}

生产 Maintenance、TryDischargeReturnDebt、AfterModeDischarge、StoreLocked 函数逐字抽取；Recovery 与 journal 头原样包含；只有底层 sgWriteFileAtomic 用内存字节提交与 actor 代替。ReadSignal 是父级已合格输入边界，未复制其资格实现。

没有真实新 mode2、新 episode、新 journal 在 Host+pump+owned+crash 锁持有期间执行。actor 只改独立原子。原子变化并不推翻 store 前已经成立的历史物理归还事实。

候选语义：physical-signal-confirmed 是此前读窗口对该旧 episode 的物理证据；MatchedEpisode/Power/Topology 是捕获的证据 tuple，不能脱离其 consumer checks 声称当前上下文已经恢复。父级按旧责任可线性化完成、新 mode2 写前重新持久认领的语义审阅。exit/intent alone 不要求 rollback。

sourceEvent/mainSourceRevision/topology 观察仍保留；本 fixture 没有执行真实 source producer 链或实际设备回退，没有证明旧债清除后有新、应归还的程序责任丢失。该更强性质为 UNKNOWN。power/mode/Host token 为附加条件观察，未证明实际调用可在这些锁持有时推进。

对照：事件提前到既有 post-query gate 会拒绝；相同事件下底层写失败则三处债务保留；稳定提交成功；失败后新鲜窗口可重试纯结算。上述控制验证代码行为，不替代结算语义契约。

本侧不建议生产修改，不建议只因 token 变化补写 pending/回滚已完成旧债。详细最终裁决及静态 producer 绑定见输出根 FINAL-REVIEW.md 与 FINAL-SOURCE-BINDING.json。早期运行中的 expectedGap、REPRODUCED_SETTLEMENT_CONTEXT_GAP、settlementSafetyPassed=false 标签是已撤回的过强解释，原始观察留档。

| Frozen source | SHA-256 |
| --- | --- |
${hashes}

完整函数范围、字节偏移、SHA、fixture SHA 见 source-binding.json。运行路径：${result.run}。

限制：不覆盖真实 flush/replace/crash、ReadSignal 判断、实际物理归还/Steam 消费、新责任被覆盖的实机可达性。只进程内 fixture；未启动产品、Host、设备、服务、Steam 或电源动作。
`;
}
