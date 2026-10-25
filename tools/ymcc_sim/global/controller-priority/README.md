# 外接控制器优先级与归还：正式消费者接入

当前2项：`controller.external-priority-policy`、`controller.external-priority-runtime`。`getControllerPriorityAdapters()`供父协调者串行加入原global registry；本目录不改共享CLI/registry。

两项直接运行原`external_controller_priority_*_selftest.ps1`和原C++断言；真实生产policy/runtime/ledger头被编译，设备、进程与设置终端使用原夹具假实现。Release与Checked均须真实运行并给出原完整摘要。原脚本不变，编译失败不能作为负例成功。

每项在自有复制源码中破坏一条实际生产门，保持原脚本与C++断言逐字相同，再执行原测试并要求真实FAIL摘要及非零退出。policy破坏物理身份门；runtime破坏电源代次门。故障副本明确为当前源码人工注入，不冒充旧历史版本。

`runtime`原夹具只替换旧CreateProcessW，而生产代码已改到processEfficiency::createProcess。本线仅增加指向原offline::process的namespace别名；移除新增两行后原夹具正文完全相同。未改产品header或业务断言。原dumpbin禁止CreateProcess/设备/驱动/注册表导入门仍执行。

入口：

```powershell
node tools/ymcc_sim/global/controller-priority/run.mjs --test controller.external-priority-policy --evidence-dir <不存在的自有目录>
node tools/ymcc_sim/global/controller-priority/run.mjs --test controller.external-priority-runtime --evidence-dir <不存在的自有目录>
node --test tools/ymcc_sim/global/controller-priority/runner.test.mjs
```

每个原脚本经既有host-process.ps1的受控Job启动。仅复用已安装MSVC/SDK，工具链设置只在子shell生效。实际产物、输入副本、原输出、故障证据及工具身份均记录；sourceHashes覆盖当前正式源码和本case中真实生成的文件。未知用例、已有证据目录、缺摘要、超时、缺真实失败或来源变化不能通过。

本模块不运行Native产品Main、真实Host、系统睡眠或真实设备控制；runtime只读取原夹具私有临时目录的ledger样本。`native/main.cpp`仅为既有header接入的未执行来源引用。HC总库绑定、整机输入/驱动/物理隐藏归还和实机效果仍需分别完成，不能由此宣称整个手柄接通。
