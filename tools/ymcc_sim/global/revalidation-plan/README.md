# HC 接线的增量重验计划

本模块加入现有 `ymcc_sim/global`，复用正式 `auditReceipts` 和 `auditRegisteredScope`。不执行测试、不加载产品或 DLL，不产生新的通过结论，不改变现有 CLI、registry 或审计器。

从原始 global `result.json` 按既有先后顺序重审，不能把旧 `receipt-audit.json` 或 `needsRerun` 摘要当作输入。最新失败/阻塞仍覆盖旧通过；新登记、登记变更、源码/产物/证据变化均保留重验义务。

```powershell
node tools/ymcc_sim/global/revalidation-plan/cli.mjs --receipt C:/evidence/full/result.json --receipt C:/evidence/repair/result.json --out-dir C:/evidence/new-plan --profile full
```

`--test` / `--domain` 可限制明确范围，未知选择报错。`--max-batch-size` 为 1..32，默认 6。输出目录必须不存在；输出 `receipt-audit.json` 与 `revalidation-plan.json`。退出 12 表示仅计划，3 表示输入/审计阻塞，均不是产品验收通过。

按当前未通过状态、明确认领、精确共同变化依赖和原执行候选身份分组。同样修改过 main.cpp 不会自动把其他所有检查合并成巨型批次。候选 Host 根/二进制和显式 runtime/灯光载荷保持分开；运行批次前必须核对当前候选输入，计划不能自动选择、构建或安装候选。

可选 `--assignments FILE` 输入：

```json
[{"owner":"owning-thread-id","tests":["fan.unified-capability-dispatch"]}]
```

认领仅用于避免重复安排，不证明任务或进程还活跃，不会从 `needsRerun` 删除该项。重复认领与未知测试拒绝。负责人仍要提交原运行回执，再通过正式审计核验。

API：`createRevalidationPlan(receiptFiles, {repoRoot, adapters, profile, domains, tests, assignments, maxBatchSize})` 返回 `{plan,audit}`。`adapters` 使用主协调者当时的审查清单。`reusableAtAudit` 只表示该次审计有效；源码、候选、登记或证据改变后再次审计。最终候选范围完整性、实际 ABI、安装与物理设备验收沿用原门。

```powershell
node --test tools/ymcc_sim/global/revalidation-plan/plan.test.mjs
```
## 批次执行前检查

每批 `inputState` 保存本次计划观察到的明确源码、既有执行的动态依赖和已登记/已记录二进制身份；`inputStateFingerprint` 防止意外改写。协调者可在启动耗时场景前调用：

```js
import { checkBatchInputs } from './tools/ymcc_sim/global/revalidation-plan/index.mjs';
const check = checkBatchInputs(plan, 'batch-001', {repoRoot});
// 只有 INPUTS_MATCH_PLAN 表示已知输入仍匹配计划，随后仍须调用原runner。
// PLAN_INPUTS_CHANGED / BLOCKED_INPUTS_MISSING 时先更新计划或明确固定输入。
```

此检查不锁定源码、不证明worker活跃、不批准候选、不执行测试，也不能代替runner运行前后指纹与最终回执审计。原回执明确记录为缺失的可选Host伴随库可继续保持缺失；必需输入缺失不会被当作匹配。旧版计划没有每批inputState时明确拒绝，需重新生成计划。
## 历史回执与最新输入

计划器按每项测试最新有效容器回执观察动态依赖、候选和结构证据。已被后轮替代的旧依赖变化，不会使新轮计划无关阻断。原始旧失败及回执仍保留；畸形容器继续交正式审计阻断。

可选 `selectContributingReceipts(receiptFiles)` 只建议从审计输入列表省去“全部测试均已有后轮回执”的旧文件，输出替代关系、原顺序和文件哈希；不改写或删除任何原件，不生成合成result。新FAIL/BLOCKED不会退回旧PASS，仍贡献任一最新测试的旧文件保留，非法容器保留。输入读取期间变化则返回BLOCKED_INPUT_CHANGED并保留完整输入列表。

```js
import { selectContributingReceipts, createRevalidationPlan } from './tools/ymcc_sim/global/revalidation-plan/index.mjs';
const selection = selectContributingReceipts(orderedOriginalRuns);
if (selection.status !== 'SELECTION_ONLY' || !selection.withinFormalAuditReceiptLimit) {
  throw Error('需要重新核对原回执输入');
}
const {plan, audit} = createRevalidationPlan(selection.receiptFiles, options);
```

此建议器最多接收256份原始定位。正式审计与计划器共享 `receipt-audit.mjs` 导出的 `RECEIPT_AUDIT_LIMITS`：最多128份回执、单份32 MiB、每轮累计512 MiB；计划器的读取和哈希观察各自按该累计预算检查，读取前后均核对字节上限。筛选结果的 `readLimits` 保存这些限制，`withinFormalAuditReceiptLimit` 同时要求保留项数量及字节预算满足限制、原件未变；容器结构与业务结果仍由正式审计决定。无法读取或因预算未能解析的原件保留，不能通过筛选消失。

执行 `maxBatchSize` 仍只允许1到32，与回执文件数分开。最新FAIL/BLOCKED和过期项仍在重验列表；只读计划可以是 `PLAN_ONLY`，而原审计仍是FAIL或需要重验。超出正式数量或字节上限时不能伪造合并或丢掉未完成项。此建议不会自动修改父任务的原回执顺序；采纳时将selection记录一并存档。


用户明确排除的能力以 `capabilities/USER-SCOPE-DECISIONS.json` 和当前registry为准。原回执保留其原FAIL/STALE等历史状态，可能仍出现在audit中；计划只为当前选定registry产生执行队列。已撤下的USER_EXCLUDED项列入 `outsideSelection`，不进入 `needsRerun`、批次、已选数量或能力完成分母。不要为消除历史失败而重新导入，也不要改写旧回执。永久回归保留“历史失败可见、活动任务不排它、原件SHA未变、执行数0”的控制。
