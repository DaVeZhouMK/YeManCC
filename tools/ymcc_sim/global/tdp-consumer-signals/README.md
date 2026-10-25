# TDP frontend production consumer

该域复用原 `tools/tdp_frontend_static_selftest.ts`，直接执行正式 `src/bridge/yeman.ts` 的setTdp及原runtime/diagnostics/circuit-breaker。原20项断言、脚本正文和产品源码不改；原ESM搬迁环境不能处理mock-shell内的require，本域使用CJS编译，并在受限VM中提供原fixture需要的文件叶接口。

`getTdpConsumerAdapters()`导出单项 `performance.tdp-frontend-consumer`，由主协调者串行登记。模块不修改共享CLI/registry/exclusions或native/main.cpp。

```powershell
node tools/ymcc_sim/global/tdp-consumer-signals/run.mjs --evidence-dir <不存在的绝对输出目录>
node --test tools/ymcc_sim/global/tdp-consumer-signals/runner.test.mjs
```

源输入先按明确清单固定原字节；CJS构建从这些原字节读取，metafile必须匹配已审查导入图。编译图列出的监控、调度、Vue等输入不意味着每个分支执行过。本项实际断言覆盖7种调用来源、保存与应用分离、CLI原错误、daemon回退、MSI独占和部分失败、能力拒绝、睡眠代次、诊断失败不影响控制、操作者标记和无所有者恢复跳过。

真实负例只在自有冻结yeman.ts副本取消现有lifecycle拒绝条件，原脚本与断言保持相同；必须实际完成原20个检查并由指定原断言失败，不能拿编译失败/抛异常/超时充当检测成功。该副本明确标为当前源码人工故障，不冒充历史修前版本。

原mock-shell模块仍是原件。运行时只提供受限的fs/path/os/child_process接口：文件读写只在本轮scratch，子进程与网络拒绝，环境不含用户配置或凭据，定时器结束时收尾。VM是本组已审查软件消费者的IO隔离器；本项不启动产品Main/真实TDP程序、Host、驱动、注册表工具、设备或Windows睡眠。原测试的IPC响应只是内存终端，不证明OEM实际写入。

运行结果包含原报告、正负CJS程序、metafile、具体故障源、原输入副本与SHA。正式结果的sourceHashes绑定真实生成文件，父测试环境的checkStructuredEvidence会对生成程序、报告、metafile和原输入逐文件复核。源/工具在执行中变化、导入图变化、缺检查、错误布尔、缺报告和IO越界均不能通过。

这组软件证据不代表OEMCPU/OEMGPU已完成总库统一绑定，不证明实际功耗、OEM读回、真实UI/原生IPC端到端或已安装版本。那些能力仍需对应原执行线完成接入与验收。
