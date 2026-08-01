# YeManCC 硬件控制验收报告（2026-07-31）

机器：AMD Ryzen 9 9950X（16C/32T，AM5 桌面），电源计划「野蛮精睿优化电源V18」
执行体：`C:\SOFT\YeMan\PowerControl\pawnio\YeManTdpCtl.exe`
验收原则：**以实际读数/返回值为准，不以退出码为准**；每个结论给出数据。

---

## 1. TDP 15W 生效验收 —— PASS

方法：先 200W 起 32 个满载进程确认饱和，再降 15W 采样，最后恢复 200W。
性能计数器（`\Processor Information(_Total)`）：

| 阶段 | % Processor Utility | % of Maximum Frequency | Processor Frequency |
|---|---|---|---|
| 200W 满载饱和 | 108.1% | **100.0%** | 4300（恒值，不可信） |
| 降到 15W（负载仍在） | 14.1% | **14.0%** ≈ 0.6–0.8GHz | 4300（恒值） |
| 恢复 200W | 109.2% | **100.0%** | 4300（恒值） |

结论：
- `set 15` 三条 SMU 邮箱命令（STAPM 0x4F / FAST 0x3E / SLOW 0x5F，Desktop-AM5 家族）全部 OK。
- 15W 下满载频率从 100% 塌到 14%（≈0.6–0.8GHz），远低于用户判据 2.0GHz，恢复 200W 立即回满 → **15W 确实生效**。
- 重要陷阱：AMD 上 `Processor Frequency` 计数器恒报 4300，**不可用作判据**；必须看 `% of Maximum Frequency`。

## 2. CCD 开关逻辑验收 —— PASS（机制级）

- 拓扑探针（修正版，GetLogicalProcessorInformationEx）：32 逻辑 / 16 物理核，**2 个 L3 域（2 CCD）**。
- 复刻 native `detectCcdTopology()` 算法预测：本机 Windows 上报的缓存记录 GroupCount=0 → 走**兜底均分**路径 → `ccdMasks = ["0xFFFF", "0xFFFF0000"]`，与真实 CCD 线程分布一致（8 核×2 线程/CCD）。
- 亲和性切换实测（32 个满载进程，SetProcessAffinityMask）：

| 阶段 | 系统 % Processor Utility |
|---|---|
| 全核（0xFFFFFFFF） | 108.7% |
| 仅 CCD0（0xFFFF，16 线程） | 62.9%（容量减半） |
| 恢复全核 | 108.5% |

- 结论：拓扑检测与亲和性机制在真机上明确生效。**UI 层（CcdCard → cpu.setCcdMode → ccdWorker）需用户手动点一次开关复核占用对比**（我无法驱动运行中的 app 界面）。
- 附带发现：本机 Windows 的缓存记录是经典布局（`PROCESSOR_GROUP_INFO`），native 26100 SDK 的 `GroupMasks[]` 主路径读到 GroupCount=0，恰好走兜底逻辑；若未来 Windows 换用真 26100 布局则自动走主路径，两路都已被处理。

## 3. 降压（Undervolt）验收 —— 写入 PASS / 读回受限（如实说明）

| 命令 | 返回值 | 判定 |
|---|---|---|
| `uv probe --vendor amd` | `{"supported": true, "family": "AM5", "current": 0}` | 邮箱接受 set-coall |
| `uv set -8 --vendor amd` | `set-coall(0x36) -8 -> OK`（status=1） | SMU 接受写入 |
| `uv probe`（再读） | `current: 0` | **非真读回** |
| `uv set 0` | `set-coall(0x36) 0 -> OK` | 还原成功 |

- 说明：AMD MP1 邮箱的 `set-coall` 是**只写命令**，无标准读回；`probe` 返回的 `current: 0` 在源码中硬编码（YeManTdpCtl.py `amd_uv_probe`）。能验证的是「SMU 接受写入（status==1）+ 退出码」，无法验证「实际生效偏移量」。Intel 路径（rdmsr 0x150）才有真读回。此为协议限制，非程序 bug。

## 4. 门忙（0x8007054F）bug —— 已复现 → 已修复 → 已部署

### 复现
12 个并发 SMU 写（TDP/UV 混跑）：旧恢复版 11/12（首轮）→ 新中间版 10/12，失败日志：
`AMD-MP1 ... FAIL HRESULT=0x8007054F` + `[gate] 硬件门忙(0x8007054F), 25ms 后重试一次`（只重试一次仍失败，rc=6）。

### 根因（源码核实）
- `_smu_exec`：单次 25ms 重试（`GATE_RETRY_DELAY=0.025`，重试一次）。
- MP1 底层 `_smu_rd/_smu_wr`：**完全忽略 HRESULT**，忙时照样读写 → 邮箱序列损坏。
- 并发场景下 PawnIO 驱动加载（RyzenSMU.bin）存在竞争（rc=4）。

### 修复（YeManTdpCtl.py）
1. `_smu_exec` / `_smu_rd` / `_smu_wr`：门忙改为 **10/25/50/100/200ms 递增退避**，最多 5 次。
2. 新增**跨进程命名互斥体** `Local\YeManTdpCtl_SMU_Gate`（`_SmuGate` 上下文管理器），`amd_set` / `amd_get` / `amd_uv_set` / `amd_uv_probe` 全部包在互斥体内 → 并发调用排队串行，抢门场景直接消除；等待超时 10s 降级为直接执行。

### 终验
- 功能：set 15/200、uv probe/set、get 全部 OK，Desktop-AM5 家族识别与旧版完全一致（未重蹈误判移动端覆辙）。
- 压力：12 路并发 × 3 轮 = **36/36 全部成功**（修复前 11/12、10/12）。
- 已部署三处（exe + _internal），SHA-256 一致：
  `eea35b7f0efeaf43e14c622cdd29b3e32c76e6acd886a6a51f79a6e7682aa2d1`
  - `C:\SOFT\YeMan\PowerControl\pawnio\`（成品）
  - `C:\SOFT\YeMan\YeManCC4\YeManCC3\PowerControl\pawnio\`（工作区）
  - `C:\SOFT\YeMan\YeManCC4\PowerControl\pawnio\`（源目录，用户旧恢复版备份在 `YeManTdpCtl.exe.bak_*`）
- 产品目录就地冒烟通过（set 200 三条 OK、get 通道 OK、uv set -5/0 OK）。

## 5. 收尾状态
- 当前 SMU：TDP = 200W（恢复，匹配 tdp-ac.txt=200）；UV = 0（还原）。
- 测试脚本留在 `testrun/`：`topo_probe.py`、`cache_dump.py`、`tdp_verify.py`、`tdp_verify2.py`、`ccd_verify.py`、`uv_verify.py`、`gate_stress.py`、`final_verify.py`、`loadburn.py`、`sample_counters.ps1`。
