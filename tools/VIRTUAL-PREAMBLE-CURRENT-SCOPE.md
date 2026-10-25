# 当前BUS前段计时器的语义回归

```powershell
node tools/virtual_preamble_current_scope_selftest.mjs --out <全新绝对证据目录>
```

本入口复用virtual_preamble_cost_selftest.cpp原始断言，提取当前生产enum、24个stage名字/索引、原子计数、tickCostEnabled/Accumulate/ElapsedUs以及TickPreambleElapsedScope原函数体。仅把std::chrono::steady_clock类型改名为原夹具FixtureClock；GetLastError/SetLastError由原夹具模型接入。没有复制第二套计时算法，也不执行完整BUS、真实设备/SDK/Host或测CPU。

Release和Checked分别验证默认关闭零观察、finish幂等、当前LastError保留、异常展开、门控开关、嵌套范围、24个原stage身份、四线程40000次并发采样。第三个独立编译候选只删除finish里的active_=false，必须被原“repeated finish observed twice”断言拒绝；编译失败、超时或其他错误不得当指定负例。2026-10-22原夹具每配置80067检查，不以该次数宣称全BUS或实机通过。

旧virtual_preamble_cost_selftest.py还绑定2026-10-15整份历史patch reversal与保护函数身份。本入口没有放宽或修改旧历史门，而是明确新增“当前计时scope语义”的可复用入口；两种证明不可混为一谈。

产品实际范围：

- physical read在BUS读取当前真实源，避免用UI旧采样；没有证据允许为CPU减少它。
- power/OEM/host reconcile分别包住原入口。已知focus/释放/外接暂停与模式准入仍由原控制链负责。
- tickPreambleUs从真实callback context起点到frameStarted，是否有效由原context判断；n不必等于BUS调用数。
- 一秒window包含emitTickCostWindow自己；scope要在emit之后析构才累加，所以该单次elapsed会出现在后续排出的窗口。不能把一次窗口较大的值当作每tick同样开销。
- 旧field的tickTotal n126、tickPreamble n124、window n1、UI telemetry n11是不同总体。多数范围有嵌套，不能相加为排他CPU；原cycles字段是独立计数或校准估计，不是GetThreadTimes真实CPU。

当前源码边界行号/SHA、scope原始输入和故障对照见Build/Validation/VC-PREAMBLE-AND-GLOBAL-20261022。R7生命周期契约仍是VC-LIFECYCLE-20261020-R3，契约后缀不能拿来识别交付包。真正版本身份必须核对native/Host/SDK加载证据与导出哈希。

这轮不改采样率、线程、timer、超时、物理读、恢复预算或能力总库，也不覆盖R7；CPU收益及实际重启/睡眠/游戏消费仍需同版现场证据。