# 执行完成合同

该模块只核对既有执行输出是否到达生产者自己声明的终点，不执行业务、不生成通过摘要，也不证明分支、硬件或完整产品覆盖。

## 已审查合同

- `settings.reactivity`：唯一的 `settings_reactivity_selftest: N cases passed`，N 至少为 26。
- `settings.cas`：必须同时存在唯一的 `settings CAS selftest: PASS` 和 `retired controller persona migration: PASS (12 target/overlay/startup/receipt cases)`。
- 合同为有界、严格类型的数据；仅接受 stdout/stderr 的精确单行，或固定前后缀包围的十进制计数。不接受正则、命令、空合同、重复规则或未知字段。

## 接线

`runner.mjs` 验证可选 completionContract，在实际进程自然退出、来源检查后记录 completionProof，并由原 evaluateResult 拒绝失败证明。

`receipt-audit.mjs` 重新读取同一 case 内的 stdout.log/stderr.log，核对合同指纹、原输出哈希和终点。当前登记增加或改变合同时，旧 PASS 仍需重跑，不允许补字段升级历史回执。

`adapters.mjs` 只给上述两个已审查生产者声明合同，同时登记 helper/runner 来源。其它生产者须先确认真实且完整的结束语义再接入。

## 运行验证

单元验证：

```powershell
node --test tools/ymcc_sim/global/execution-completion/completion.test.mjs
```

统一入口采用补丁后，从仓库运行下面的显式集成验证；evidence-root 必须是新的自有工作目录。它仅创建和运行两个惯性 Node fixture，保留证据，不启动产品、Host 或设备。

```powershell
node tools/ymcc_sim/global/execution-completion/verify-integration.mjs --global-root tools/ymcc_sim/global --evidence-root <新的绝对工作目录>
```

集成验证实际覆盖：异步未完成且自然退出 0 的 FAIL、正常终点 PASS、旧回执缺证明、合同变化、原 stdout 变化、恢复原字节后的 PASS、两个登记入口的坏合同拒绝。

共享三文件由模拟器协调者串行采用。候选 r2 在独立镜像上已经运行未修改的 runner/receipt-audit/registration-audit 与本模块单元验证，共 72 项通过。初次镜像遗漏外链校准工具模块造成的加载失败保留；最终补齐的是编译/模块来源，原断言没有改动。

`PASS` 只说明本合同的完成性；原结果、来源变化、产物校验、超时、结构化回执、业务断言和物理验收仍各自生效。共享 runner 变化可能使声明其来源的其它测试过期，应按实际输入定向重验。
