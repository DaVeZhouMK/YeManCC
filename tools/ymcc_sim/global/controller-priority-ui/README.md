# 当前外置优先界面的真实消费回归

`getControllerPriorityUiAdapters()`提供`controller.external-priority-ui`。当前实际ButtonMappingView、Toggle、SwitchControl、ControllerChannelStatus、settingsRepository和gameInputEditor被编译并实际执行，21个场景覆盖窄CAS保存、并发冲突、迟到读取、键盘操作、暂停/归还/未确认状态、runtime快捷人格、手动人格串行和退出清理。检查1280×720/800及窄/宽布局；不以此冒充完整正式gamepad engine导航。

旧`external_controller_priority_ui_browser_selftest.mjs`依赖已移除的旧布局/角色/状态chip，首轮实际超时保留。原旧脚本未删或修改，也未因新21个当前场景通过而改签旧结果。当前fixtureSchemaVersion=2，原脚本的业务不变量对应到新组件；旧初始focus hook仍仅做源码观察，完整导航另由原controller.ui-navigation负责。

真实界面测试发现并复现旧异步读取覆盖较新CAS cache：持久priority=true而页面false。正式loadSettings只新增读取起点cache对象和两处发布保护；原generation/CAS/队列/回执未动。删除保护的自有副本被同一当前UI断言检出；另一个副本删除运行态suspended确认门，同样被拒绝。两种负例均须真实AssertionError，浏览器/编译启动失败不能算负例。

编译观察只加metafile和记录实际输出字节/实际读取与require工具身份，不改业务函数。已审阅source-inputs.json记录实际28个源码输入，tool-inputs.json记录观察到的69个工具文件，编译器、Node和浏览器身份另列。每轮正/负输入与产物前后校验，实际JS输出参与公共证据校验。设置/IPC/外部游戏提供者仅为明确内存终端，不访问安装应用、硬件或系统游戏。

```powershell
node tools/ymcc_sim/global/controller-priority-ui/run.mjs --evidence-dir <新的目录>
node --test tools/ymcc_sim/global/controller-priority-ui/runner.test.mjs
```

`--discover`仅用于观察当前编译输入，输出DISCOVERY_ONLY；发现输入不等于通过，正常执行必须与明确审阅列表一致。原全页导航另有gyro simulation控件归属失败和并行变源，已独立保留；不能用本场景抵消。
