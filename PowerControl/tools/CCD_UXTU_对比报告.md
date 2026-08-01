# CCD 切换对比报告（YMCC vs UXTU）

## 测试环境
- CPU: AMD Ryzen 9 9950X 16-Core Processor（16C32T，2 CCD）
- 切换工具: Universal x86 Tuning Utility (UXTU)
- 快照脚本: `Get-CcdSnapshot.ps1`
- 拓扑定义: CCD0 = bits 0-15（mask 0x000000000000FFFF），CCD1 = bits 16-31（mask 0x00000000FFFF0000）

## 快照记录

### 切之前（全核默认）
- 时间: 2026-07-29 02:00:15
- 统计: 总计可读取=241，仅CCD0=0，仅CCD1=0，跨CCD=241，其他=0
- 所有进程亲和性: `0x00000000FFFFFFFF`（BOTH，全核）
- Top 进程示例:
  - victoria3 PID=2732 WS=... Affinity=0xFFFFFFFF -> BOTH
  - WorkBuddy/QQ/msedge/UXTU 等全部 BOTH

### 切之后（UXTU → CCD1）
- 时间: 2026-07-29 02:12:56
- 统计: 总计可读取=271，仅CCD0=0，仅CCD1=269，跨CCD=2，其他=0，读取失败=12
- 几乎所有用户进程亲和性被改为 `0x00000000FFFF0000`（仅 CCD1）
- Top 进程示例:
  - victoria3 PID=2732 WS=8004MB Affinity=0xFFFF0000 -> CCD1
  - UXTU PID=25548 Affinity=0xFFFF0000 -> CCD1
  - WorkBuddy/QQ/msedge/explorer 等全部 -> CCD1

## 结论

1. **UXTU 的 CCD1 模式行为**:
   - 立即遍历系统内所有可设置亲和性的用户进程
   - 强制把每个进程的 ProcessorAffinity 改为 `0xFFFF0000`（仅 CCD1）
   - 不是只影响新启动的进程，也不是只改游戏进程

2. **与 YMCC `cpu.setCcdMode` 的对照**:
   - YMCC 当前实现（`native/main.cpp`）:
     - `mode=0` 全核: mask = CCD0 | CCD1 = `0xFFFFFFFF`
     - `mode=1` 仅 CCD0: mask = `0x0000FFFF`
     - `mode=2` 仅 CCD1: mask = `0xFFFF0000`
     - 切换时调用 `ccdApplyMode()`，遍历 `CreateToolhelp32Snapshot` 得到的全部进程并 `SetProcessAffinityMask`
     - 后台 1.2s 轮询 `ccdApplyNew()`，把新启动进程也限制到目标 CCD
   - **行为与 UXTU 一致**: 都是「全量重设 + 新进程兜底」

3. **任务管理器验证**:
   - 用户截图显示 CPU 核心 0-7（CCD0）几乎空闲，核心 8-15（CCD1）有负载
   - 与快照 `0xFFFF0000` 完全吻合

## 后续建议

- 无需再修改 `cpu.setCcdMode` 的核心逻辑，当前实现已对齐 UXTU。
- 如需在 UI 上明确提示用户：「切换 CCD 会立即重设所有进程的 CPU 亲和性」。
- 建议在设置 CCD 时排除更多关键系统进程（当前已排除 system/registry/smss/csrss/wininit/services/lsass/winlogon/audiodg/dwm/svchost 及自身）。
