# 监控标签子串搜索优化

日期：2026-10-17。主线已合入，尚未链接、制包或部署。本轮只修改生产头 `native/monitor_text_fold.h` 的 find 实现；没有修改主线 main.cpp、HWiNFO 配置、注册表或用户设置。

## 最终实现

原实现对每一个候选位置都先进行 ASCII 折叠，再逐字节匹配子串。现在先检查原始首字节是否可能等于 needle 首字节（小写字母可对应原始大写），不可能的位置直接跳过；后续字节继续使用原有有界 ASCII 折叠比较。

ASCII 大写 needle 首字节不可能出现在已做 ASCII 折叠的源文本中，因此直接返回未找到。空 needle、超长 needle、非 ASCII 字节、嵌入 NUL、动态 C 字符串、lvalue 字符串修改与 suffix 视图保持原语义。

该实现不缓存名称，不增加数组、不增加堆分配；视图对象仍为 24 字节，不保存硬件值或跨周期借用指针。它只减少现有唯一采集链处理新硬件样本时的字符串工作。

## 先验证收益再采用

本轮否决了两个候选，文件与结果原样保留：

- 第二次搜索后缓存折叠名称：操作计数减少，但无插桩完整捕获解析约慢 35%，没有合入。
- memchr 查找候选首字节：完整捕获解析约慢 29%，没有合入。

最终首字节直接筛选的短测出现 GPU 抖动，因此延长输入循环，再以交替前后顺序跑 5 轮，使用中位数：

| 实际解析函数输入 | 每轮次数 | 原实现中位数 | 新实现中位数 | 比例 |
| --- | ---: | ---: | ---: | ---: |
| 完整已捕获 SM2 | 10,000 | 355.27 ms | 316.31 ms | 0.890 |
| CPU / 硬盘标签 | 300,000 | 95.15 ms | 80.20 ms | 0.843 |
| GPU 内存 / 电压 / 温度 | 300,000 | 67.01 ms | 65.73 ms | 0.981 |
| RAM / 网络 | 300,000 | 86.19 ms | 84.18 ms | 0.977 |

完整捕获约少 11%，CPU/硬盘样本约少 16%；GPU/RAM 输入接近原水平。这里是去掉分类计数插桩、调用实际完整解析函数的局部 wall-clock 对比；进程调度不受控，不能当作整进程 CPU 百分比或游戏性能收益。测量 helper 与交付 helper 除注释外一致，当前 parser 与测量实际函数一致，见 TIMING-SOURCE-IDENTITY.json。

完整捕获 200 次解析的 ASCII 折叠字符处理为 7,189,800 → 1,603,200；堆分配保持 0。此工作量下降用于解释局部耗时结果，不能单独替代耗时证据。所有短测与被拒候选结果都保留，避免只留下有利的一轮。

## 等价性与稳健性

- 3,470,299 项字节、动态 needle、字符串生命周期、copy/assignment、suffix、0..128 字节字段边界检查通过。
- 6,060 组直接分类语料结果一致：当前 GPU、内存、网络原始字段及正反例、单位与无效值；12 组需求/组号边界一致。
- 15 组完整发布值、13 组原生 GPU 值、每侧 38 项原有实际解析场景一致。
- 12 份历史完整共享内存捕获回放共 384 次解析，完整发布/GPU/FPS 输出、堆分配与 checksum 一致；另完整捕获 200 次工作量验证通过。
- 当前实际显存证据记录器回归 36 项通过。
- 默认不依赖外部捕获的自测、加入历史捕获的自测均通过。
- 当前完整 native/main.cpp 翻译单元编译成功；合入后的全部 native 输入与已编译冻结输入一致（仅本头替换）。这不代表整包链接或部署成功。

## 上游只读核对

当前 HWiNFO EXE SHA-256 为 `e4ce0c85c4288fa7e8eb12ebeece99efff4d81e0b13de53f091daabc81a3c9d3`，与管理代码允许裁剪的固定映像相同。现有 INI 中共享内存开启、SensorInterval=3000、PresentMon=1，EC/SMART/SMBus/USBC_PD 与 GPU 时钟测量等既定值保持。65 个固定组 Enabled 偏好键均为 0；这不表示所有可能组都在本机物理存在，也不是每组采集行为的运行测量。

首轮 Store Python winreg 无法定位这些键，属于读取通路不可用，不是实际键缺失；后用现有 PowerShell Registry provider 验证，原失败读数独立保留于 UPSTREAM-PYTHON-REGISTRY-UNAVAILABLE.json。最终只读结果为 UPSTREAM-READONLY.json。

本轮没有扩大禁用策略，确保 HWiNFO Presented FPS、所需 CPU/GPU/显存和跨机器数据不会为节能被误关。

## 源码与重跑

- 修改前头 SHA-256：`953f5a6afec0ea87bef9937dfecefce24e9d916862642d8b34eb0a62565ea38a`
- 修改后头 SHA-256：`b9e250d266898980e9036e1106d6b23383a52187c31af27f543515aa8b467176`
- 安装版 EXE 保持 `9b674c974510c1a1452b4a36177ec57fccf5c2e1c38992b1de37f944147e6220`，本轮没有部署。
- 主线新增 `tools/monitor_folded_search_selftest.mjs`、`tools/monitor_folded_search_bytes_selftest.cpp` 与精确旧 helper `tools/fixtures/monitor_text_fold_search_before_20261017.h`。
- 证据：`Build/Tasks/YMCC-Steam-HUD-Probe/folded-search-cache-20261017`。目录沿用最初被拒候选名；实际交付没有名称缓存。

在主线目录中设置新输出目录后运行：

```powershell
$env:YMCC_FOLD_SEARCH_OUT='G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/your-new-run'
node tools/monitor_folded_search_selftest.mjs
```

可选 `YMCC_FOLD_SEARCH_RAW` 指向已有完整二进制捕获，`YMCC_FOLD_SEARCH_CAPTURES` 指向已有捕获路径列表 JSON。这些自测只回放文件，不读取真实硬件或增加运行采集器。长期数据与 CPU 优化目标仍保持进行中。
