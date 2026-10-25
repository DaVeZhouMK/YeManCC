# 外接手柄归还债务持久化接入

导出 `getControllerPriorityLedgerAdapters()`，id `controller.external-priority-ledger`。

只新增本目录。原 `external_controller_priority_ledger_selftest.ps1/.cpp` 与正式 `native/external_controller_priority_ledger.h` 不修改。

## 实际执行

原测试在两个原配置中执行13组，共26场景/17458断言；同时编译原standalone first-include接口。覆盖实际私有文件原子保存、部分归还、清空、损坏/截断、类型/owner错误、真实文件锁、刷盘/rename失败和同一已编译exe的子进程中断。原测试的可选符号链接分支按原平台条件执行，不外推为必经路径。

子进程仍由原测试创建：自身exe + `--interrupt-save` + 新建私有目录，原源码要求特定前缀和 `fixture-only.marker=externalpad-test-only`，只在原指定rename前中断点ExitProcess(73)。两配置原“flushed orphan and old file preserved”业务组准确通过。自有Job保护整棵后代树；没有按进程名结束程序。此项证明来自未改原代码、两个实际exe身份和原运行断言，不声称另有独立捕获的原C++子进程PID。

两个私有正式header故障先编译成功，再以原指定业务断言拒绝：owner保存准入错误；忽略刷盘失败。编译错误、启动失败或无终点不当作成功故障反例。

## 路径与真实账本

每次运行之前解析case、root、output最终完整路径并拒绝reparse目录，显式将TEMP/TMP/APPDATA/LOCALAPPDATA/PROGRAMDATA设在自有case。使用与原C++相同的GetTempPathW原生探针核对实际临时根，不凭环境变量推断；保存前后目录记录，原fixture前后为空。原header仅接受调用者明确路径，没有默认用户账本路径查找；不读取用户真实账本、设备或注册表。

独立边界helper用正式saveDebt/loadDebt对新建、凭证明确、无reparse的自有目录执行实际写读，保存before/partial/cleared三份原JSON和最终ledger。该helper20个检查，只验证真实持久化与剩余债务；路径中表示HID/USB/应用的字符串只是账本值，不产生设备/应用操作。

## 来源与产物

原脚本原文运行；仅向编译器增加/showIncludes诊断，记录英文或本机中文包含前缀。首次发现轮因中文前缀未被英文解析器识别而保留FAIL，原业务本身通过；该轮不是正式验收。真实观测296个VC/SDK包含文件，经安装根与文件类型核对后加入tool-inputs.json；下一次完整执行在读取前/结束后钉住这些文件及明确编译/运行工具，共306工具输入。14份产品/测试/包装来源和121个嵌套源/产物逐字绑定。工具身份范围是明确编译器/运行时文件和实际include闭包，不是完整操作系统或所有潜在SDK库的物理验收。

原输出、verification-summary、真实EXE/OBJ、编译/运行日志、根证明、账本前后原字节都进入sourceHashes。6种真实产物字节变更由原公共reader拒绝，恢复通过；终点和矛盾状态也有拒绝反例。

## 范围

只证明归还债务持久化。实际Windows文件系统操作和自有子进程已执行；真实HidHide/USBIP、驱动、物理设备归还、权限/跨进程所有权以及完整手柄生命周期保持独立。

本地独立执行：

```powershell
node tools/ymcc_sim/global/controller-priority-ledger/run.mjs --evidence-dir <新的自有绝对目录>
```

登记由协调者串行完成；随后用原公开PowerShell global执行此id和AuditRegistered。不要直接在用户数据/安装目录运行原测试。
