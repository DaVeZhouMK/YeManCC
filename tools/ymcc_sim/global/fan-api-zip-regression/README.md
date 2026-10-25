# 普通 fan-api ZIP 的全局回归入口

`getFanApiZipRegressionAdapters({repoRoot, pythonPath})` 返回唯一场景 `fan.api-ordinary-zip-regression`。主协调会话串行接入 shared extra/registry；本目录不修改共享入口。独立调用：

```
node tools/ymcc_sim/global/fan-api-zip-regression/run.mjs --evidence-dir <新的绝对路径> --python <现有Python绝对路径>
```

`runFanApiZipRegression({evidenceDir, repoRoot, pythonPath})` 导出同一执行器。evidenceDir 必须不存在；默认仓库必须是本模块所在仓库。输出 `fan-api-zip-regression-result.json`，成功最终标记 `FAN_API_ZIP_REGRESSION PASS`，工具缺失为 BLOCKED/10，断言或来源失败为 FAIL/2。

运行现有 `generateFanApiFixture`，其 NoIO 边界执行未修改的 fanApi/fanDiagnostics。给这些软件产生的投影加固定合成 native envelope，再由 Python 标准 zipfile 生成自己的普通 ZIP。原 `cli.mjs --mode signal-check --domain fan` 执行原 Python ZIP 读取器、冻结 schema2 归一化器和原 FanHostLifecycle / HttpFanApiAdapter 消费者。没有复制这些生产判定逻辑，没有使用用户 ZIP，没有启动 Host。

同一回归保留三次原 CLI：正常投影 exit0/PASS；根 receipt.ok 与 response.ok 矛盾 exit2，plan FAIL / signal BLOCKED；缺 schema 的一成员批次仍 REJECTED/无事件，另一旋转成员的有效批次保留，archive/signal INCOMPLETE/exit10。后者验证成员批次隔离，不声称覆盖所有其他 producer。第四场景对实际 signal-result 的普通副本追加一个空格，原公开 checkStructuredEvidence 必须先接受、再拒绝、还原后接受；实际原件不改。

每次 CLI 保存 PID、exitCode、signal、timeout/truncation、stdout/stderr 和唯一最终标记，绑定实际 ZIP、请求、plan、signal、日志与工具观察回执的字节。日志中 12029 是预期的实际产品拒绝，回归检查通过表示该错误被保留，不能称日志没有故障。输出中的 cases.passed 是断言结果；原 CLI 的异常回执保留在 executions 与各自原始文件。

工具记录 Node/Python 的实际绝对路径、版本和前后 SHA。TypeScript/Vue 是现有夹具实际加载工具；父进程 fixture 和原 CLI 的 CJS 工具输入由只读 observer 记录，不能把保存的源码当作已保存编译产物。本链在 VM 中转译 TS，没有生成 DLL/PDB/磁盘 JS bundle。esbuild 的包、JS 与存在的原生工具身份单独登记为 identified-not-executed；本 adapter 是 node，不调用 esbuild，不声称这些工具均被加载。Node/Python 解释器和工具自身属于运行环境，不是应用载荷。公开来源数组使用仓库相对路径和本次 evidenceDir 内的绝对工件路径，外部工具身份另外记录并前后核对。

冻结归一化器的字段/身份/预算边界保持。原 archive SHA 与过滤后 JSON.stringify 数组 SHA 分开；没有原成员字节 SHA，不建立原会话、用户意图、具体机型、跨文件因果或精确现场时间线；所有重构事件是 source-derived-fixture。没有意图/功率事件，curveWrites/explicitRequests/physicalReceipts 均应为0。无 C 安装写入、网络、真实睡眠或物理硬件验收。注册回归 PASS 也不能代替完整产品覆盖。
