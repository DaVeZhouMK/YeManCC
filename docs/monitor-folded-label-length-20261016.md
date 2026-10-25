# 传感器字段长度提前拒绝（2026-10-16）

本轮先复核统一数据入口的载荷缓存、只读数组和显示发布预算；这些已有优化继续保留。确认可单独减少的重复工作位于唯一原生解析器所用的 `MonitorFoldedText::operator==`。产品只修改 `native/monitor_text_fold.h` 的这一精确比较函数，不改 parser、字段识别规则、显示接口、采集次数、源时间、HWiNFO 上游策略或 FPS 优先关系。

## 实现与成本边界

旧实现从字段开头逐字节折叠 ASCII 并比较，即使标签与已知别名的长度不同，也会扫描相同前缀。新实现先比较字段长度与目标 C 字符串长度；长度不同直接拒绝，相同再进行原 ASCII 比较。

原有 lvalue-string 引用行为保留：调用方编辑字符串后，视图继续观察新内容。借用的 string_view 不转换成拥有字符串，不新增长期缓存、内存分配或源生命周期。临时 std::string 仍不能构造视图。ASCII 之外的字节、嵌入 NUL 的字段、动态 NUL 结尾的目标字符串语义保持原样。

MSVC `/O2` 的代表性字面量调用反汇编中，25 字节目标成为 `cmp rdx,25`，没有运行时 strlen 调用；动态指针调用仍有一次 strlen。这个证据只说明被测两个调用点，不能据此声称所有完整 parser 调用点都有相同机器码，也不表示动态字符串路径没有额外长度扫描成本。

## 完整解析与原始共享区回放

`tools/monitor_folded_label_length_selftest.mjs` 按 `/O2` 编译前后同一完整生产 parser、publisher、GPU/RAM/network/voltage 分类器，只替换旧/新精确比较头。计数器对两侧采用相同注入，仅统计比较、前缀、find 调用与实际 ASCII 折叠字符访问。

- 6060 组标签、单位、数值/有效性组合输出逐项一致。
- 15 组完整发布、13 组原生 GPU 数据和 12 组需求/组边界结果一致。
- 原始共享区来自此前本机已保存的 222264 字节、472 行 HWiNFO SM2 区域，本轮不为该回放新增硬件采集。完整 GPU、FPS/LOW/帧时间、生产者证明和发布值均对比一致。
- 所有被测稳定解析窗口的堆分配仍为 0，数据与 GPU 包体积保持不变。

200 次解析的 ASCII 折叠字符访问：

| 输入 | 优化前 | 优化后 |
| --- | ---: | ---: |
| 472 行完整原始共享区 | 9371000 | 7189800 |
| 32 行归档标签/值子集 | 1004000 | 587000 |
| CPU/核心/硬盘组合 | 75400 | 65400 |
| GPU 显存/电压/温度组合 | 38400 | 34000 |
| RAM/网络组合 | 84200 | 63000 |

完整原始共享区约少 23.3% 折叠字符访问。相等比较、前缀和 find 的调用次数保持一致。该计数不包括所有 CPU 指令、动态字符串长度扫描或上游硬件工作，不是完整解析耗时或进程 CPU 降幅。

## 字节边界、可复现性与回归

`tools/monitor_folded_label_bytes_selftest.cpp` 对全部 256 种字节、2048 组确定性随机字符串、动态目标 C 字符串、嵌入零字节、空字符串、最长有界字段、视图后缀和 lvalue 修改进行对拍。与冻结的旧实际头和独立 ASCII 折叠 oracle 共 3467076 次断言通过。

冻结头与归档 32 行 fixture 位于 `tools/fixtures/monitor_text_fold_before_20261016.h`、`tools/fixtures/monitor_label_rows_20261016.json`。新输出目录不依赖旧 Build 目录，完整分类回归再次通过；原始 SM2 大文件为可选的本轮证据，不成为常规自测前置条件。

既有回归通过：447 项原生 CPU/发布检查、11 项统一接口/有效期检查、2498 项完整 publisher 检查，以及 10785 项 CPU 计数器一致性检查。没有执行 CPU 时间或进程 CPU 百分比压测。

## 完整编译与本机小更新

原生宿主基于 250 个冻结输入编译链接成功并嵌入原 manifest；最终审核时所有输入仍匹配当前工作区。产品源除精确比较函数外未变；并行任务修改保留。本轮仅更新 `C:/SOFT/YeMan/YeManCC/YeManCC.exe`，SHA-256：`89c206895166eb275a30b7a0712aae22e8a2e7d893a977fa0fabe42d9e3b0a66`。原程序和统一设置先备份。

在已运行的 Cyberpunk 2077 下检查 20 条新版本统一快照：67 个预期字段全部保留，20 条 HWiNFO 健康；内存/显存容量关系、无电池与风扇/锁帧证明均正常，GPU 始终选中 RTX 5080。32 个逻辑处理器槽位完整，首条仍按既有规则等待差分基线，后续 19 条 CPU 有效。FPS 返回包有 16 条 HWiNFO Presented、2 条既有 RTSS 补位，其余不产包。

HWiNFO PID 保持 28552、INI 哈希不变，上一轮只读助手保持原样；游戏没有关闭，Steam 未重启。详细日志门及备份配置已恢复原关闭状态，最终正常 YMCC PID 为 14824。

上游未新增开关。真实 12 小时共享到期和其他机器当前现场行为仍不以本轮本机短时数据代替；已有历史跨机器回放、共享关闭与持续停更恢复证据继续按各报告范围使用。

## 证据与回退

证据目录：`G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\folded-label-length-20261016`。

- `FINAL-RESULT.json`、`FOLD-LENGTH-RESULT.json`：源绑定、完整 parser 输出和字符访问统计。
- `BYTE-TESTS.log`、`CODEGEN-RESULT.json`、`literal-codegen.asm`：字节/生命周期检查与代表性编译结果。
- `RAW-SM2.bin`：此前保存的原始共享区，本轮只做离线回放。
- `clean-selftest/`、`consumer/`、`publisher/`、`cpu-counter/`：独立运行与现有回归。
- `LIVE-PUBLISHER-DATA-RESULT.json`、`LIVE-CPU-DATA-RESULT.json`：安装版真实统一快照。
- `SOURCE-MANIFEST.json`、`SOURCE-STABILITY.json`、`NATIVE-BUILD.log`：冻结源码与完整编译。
- `DEPLOYMENT-MANIFEST.json`、`ROLLBACK-MANIFEST.json`、`rollback/`：安装哈希及更新前备份。
