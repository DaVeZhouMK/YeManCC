# ymcc_sim —— YMCC 全局模拟器（2026-09-22 提升到主线 tools 层）

原先挂在风扇任务目录下的"睡眠形态模拟器"，现在提升为 YMCC 主线层的**全局模拟器**：
一个入口、一个核心、一份证据格式，睡眠域与手柄域共用同一套 App 生命周期与反推取证。

```
tools\ymcc_sim\
  ymcc_sim.ps1                 # 全局入口（路由 + doctor 自检）
  registry.json                # 域/模式/场景清单与状态（机器可读）
  lib\sim_core.ps1             # 核心：路径、身份、Host HTTP、App 生命周期、反推读日志、证据写入
  domains\sleep\
    sleep_domain.ps1           # 睡眠域（selfcheck / forms / quick / switch / live）
    sleep_forms.json           # 20 形态 + 真实密度/时长/源码常量出处
  domains\gamepad\
    gamepad_domain.ps1         # 手柄域（包装既有虚拟手柄自测设施，只读不改）
  domains\full\
    full_domain.ps1            # 整机虚拟测试域（204-N：plan + run 组合执行；窗口段需 -WithApp）
  tests\                       # 断言库/套件（tool_gate / invariants / extremes / regressions / gyro_core
                               #   / signals / api_contract / cycle / provoke-missing / run_all 总门 / stub_host）
```

## 用法

```powershell
# 自检（不起宿主、不启动 App）
tools\ymcc_sim\ymcc_sim.ps1 -Domain doctor
tools\ymcc_sim\ymcc_sim.ps1 -Domain sleep -Mode selfcheck

# 睡眠：全形态回放（自启安全模式沙箱宿主 8765，跑完自动关）
tools\ymcc_sim\ymcc_sim.ps1 -Domain sleep -Mode forms

# 睡眠：模拟开关（沙箱宿主常驻 8766）
tools\ymcc_sim\ymcc_sim.ps1 -Domain sleep -Mode quick -Action sleep
tools\ymcc_sim\ymcc_sim.ps1 -Domain sleep -Mode quick -Action wake
tools\ymcc_sim\ymcc_sim.ps1 -Domain sleep -Mode quick -Action stop

# 睡眠：真实 App 实测（需管理员会话；会开/关 App）
tools\ymcc_sim\ymcc_sim.ps1 -Domain sleep -Mode live -StartApp -Elevate
tools\ymcc_sim\ymcc_sim.ps1 -Domain sleep -Mode live -SimulateRun     # 只演练编排，不启动任何 exe

# 手柄：设施清单 / 自环 / 只读探针
tools\ymcc_sim\ymcc_sim.ps1 -Domain gamepad -Mode registry
tools\ymcc_sim\ymcc_sim.ps1 -Domain gamepad -Mode selfloop -Persona xbox360 -DurationSec 25 -Elevate
tools\ymcc_sim\ymcc_sim.ps1 -Domain gamepad -Mode selfloop -DryRun
tools\ymcc_sim\ymcc_sim.ps1 -Domain gamepad -Mode probe -Probe toggle-state

# 整机虚拟测试（骨架：只解析组合清单）
tools\ymcc_sim\ymcc_sim.ps1 -Domain full -Mode plan
```

`-Domain` 之后的所有参数**原样透传**给该域脚本；每个域脚本也可以直接运行（例如
`powershell -File tools\ymcc_sim\domains\sleep\sleep_domain.ps1 -Mode forms`）。

## 退出码

| 码 | 含义 |
|----|------|
| 0 | 全部通过 |
| 2 | 有检查项 FAIL |
| 3 | 参数/前置错误、必需文件缺失 |
| 4 | 已有产品进程在运行（互斥前置） |
| 5 | NOT_IMPLEMENTED（域/模式/场景未实现） |
| 6 | 需要管理员会话（App 清单 requireAdministrator） |
| 7 | 沙箱宿主起不来（端口被占 / 宿主缺失） |

## 安全边界（硬约束）

- 睡眠域**从不**给 Fan Host 传 `--real-backend` ⇒ `realBackend=null` ⇒ 不存在 EC/HID/HC 写入路径；
- 睡眠域**从不**触发真实系统睡眠（不做 `SetSuspendState`）：`forms/quick/switch` 用的是安全模式沙箱宿主，
  只监听回环端口；真实睡眠只能由操作者手动执行；
- **不结束任何外来进程**（含网易 UU / GameViewer）；本工具只会用 `WM_APP_EXIT` 优雅关闭**自己启动的**产品 App；
- 手柄域只**包装**输入线的既有工具（`virtual_gamepad_selfloop.ps1` 等），不修改它们、不改设置、不装驱动、不动 HidHide；
- `-Attach` / `live` 会推进 Host 内存里的电源代次，之后 App 自身的电源边沿会被判 stale ⇒
  必须显式 `-AcknowledgeAttachRestart`，并且本轮结束后重启 App 才能恢复（这是 Host 的 fail-closed 语义，不是缺陷）。

## 收尾行为（实测，2026-09-22）

`live` 模式关 App 走 `WM_APP_EXIT`（向该进程的所有顶层窗口投递），判据是 **App 进程退出**；
同批的 `YeManFanHost` / `YeManRecoveryService` 会**随后自行退出**（实测：+2 s 时仍在、约 1 分钟后全部消失，
端口 8765 释放，产品进程 0）。因此关闭步骤如实把当时仍在的产品进程记在证据的 `remaining` 字段里，
而不是声称"全产品瞬间退出"。

## 退出码（v2：单一判定源 `Complete-SimRun`）

| 码 | 含义 |
|----|------|
| 0 | ALL_PASS（仅当全部必需项通过、无异常/超时/缺摘要） |
| 2 | CHECK_FAIL（必需检查失败，或记录了 exception） |
| 3 | ARG_OR_PREFLIGHT |
| 4 | PRODUCT_ALREADY_RUNNING |
| 5 | NOT_IMPLEMENTED |
| 6 | NEEDS_ELEVATION |
| 7 | HOST_START_FAILED |
| 8 | TIMEOUT（超时事件，或子进程未给出退出码） |
| 9 | EVIDENCE_INCOMPLETE_STALE（缺必需摘要，或盘上结论与计算值不一致） |
| 10 | BLOCKED_REQUIRED（必需项 BLOCKED/NOT_RUN：门未建立、MSI_DEVICE_PENDING 等） |
| 12 | REHEARSAL（`-SimulateRun`：不启动任何 exe、不发任何请求） |

**必需项失败/缺失、异常、超时、子进程无退出码一律不得 exit 0**；JSON 中的 `verdict`/`counts`/`exitCode` 与进程退出码同源（写盘后回读断言）。状态词固定为 `PASS/FAIL/BLOCKED/NOT_APPLICABLE/NOT_IMPLEMENTED/NOT_RUN`，未知不转成功、未测不转通过。

## 动作前置（SIM-2，2026-09-22 起）

- 硬件门只用**无副作用读**（`GET /api/state`）建立；门未知或开启 ⇒ **变更调用 0 次**（`tests/stub_host.ps1` 桩件可证明计数为 0）。
- `/api/handshake` **有副作用**（mock 模式下会改写 `HardwareCapable/AuthorizationGranted/FactoryType/FanRoute/DeviceIdentity`），因此**只在本次运行自启的沙箱宿主**上使用；对接外部 Host 时不再调用。
- 模式/身份/门是动作前置：`modeKind` 分 `offline-injection / live-app-injection / real-os-sleep / device-observation`。

## 收尾与反推（SIM-3/SIM-4）

- App / FanHost / RecoveryService **分别结算**，绑定 `runId`、`nativeSessionHash`、`hostInstanceId = pid + 创建时间 + 镜像哈希`。
- 清理只作用**本轮自有身份**（pid+创建时间匹配；外来同名进程一律拒绝并如实记录）。
- 反推日志只认**边沿前快照偏移之后新增的字节**（+会话/代次标记）；历史命中永远不通过。
- 重启复验：旧 Host 未排空 ⇒ 不给新实例结论；新 App 复用旧 Host ⇒ 记 `REUSE`，不算新实例测试。

## MSI 域（C 段交付）

```powershell
tools\ymcc_sim\ymcc_sim.ps1 -Domain msi -Mode table        # 三方能力表 + 证据等级
tools\ymcc_sim\ymcc_sim.ps1 -Domain msi -Mode fixtures     # golden fixtures（纯解码器）
tools\ymcc_sim\ymcc_sim.ps1 -Domain msi -Mode readback     # 默认关闭；开启需 -EnableReadback
```

只读适配：单飞、超时、会话归属、机型白名单；**曲线字节永不当作 RPM**，未知值一律 `null/Unknown`（绝不用 0 填空）；本批不新增 EC 写入/满速入口/驱动替换/曲线重排。无微星设备时如实交付 `MSI_DEVICE_PENDING`（exit 10）。

## 工具门

```powershell
tools\ymcc_sim\tests\tool_gate.ps1      # 204 §6 A.4 必需负例 + 正常对照 + 同种子复跑 -> TOOL_GATE=PASS/FAIL
```

2026-09-22 实测：**16/16 通过**（含修前负例：冻结版把失败检查当 exit 0，修复版 exit 2）。

## 故障预算/恢复重试（B 段可观测验证）

```powershell
tools\ymcc_sim\ymcc_sim.ps1 -Domain sleep -Mode retrybudget    # 只走公开 API；约 90 s
```

断言：曲线会话入睡→`FaultLocked`（不可证明的 OEM 恢复必须 fail-closed）→ 故障期唤醒 **409**（不伪造重建）→ 观察窗内状态**收敛为稳定结果**（最后 ≥10 s 不变，判据防"无限重试"）→ 期间任何 `On` 样本只能是 mock 恢复、**绝不伪装成物理读回证据** → 操作者 `release-control` 是显式恢复路径。源码锚点：`FanLab/real-host/Program.cs:6867-6869 / 10045-10051 / 10118-10123`。

## 单实例宿主（D-204-6，实测）

FanHost 是**单实例**：已有一个宿主时第二个实例立即退出（实测 `exit 2`）。因此瞬时模式（`forms`/`retrybudget`）自启的宿主**收尾即关闭**；只有 `quick`/`switch` 的常驻沙箱（8766）是显式例外，用 `-Mode quick -Action stop` 收工。


## S0/S3/S4 睡眠分型（204-F）

```powershell
tools\ymcc_sim\ymcc_sim.ps1 -Domain sleep -Mode sleeptypes
```

离线判型（**不触发任何睡眠**）：读 `powercfg /a` 的 available 段（中英双语）得到平台能力；对既有真机证据与 fixtures 作判型。判型词表：`S0ix` / `S0ix-network-disconnected` / `S3` / `S4` / `S4_MISLABELED_AS_S3`（微软侧已知：S4 可能被**错误表达**为 S3；以是否真的写盘为硬证据）/ `S4_DOWNGRADED_TO_S3`（请求了却没写盘）/ `S4_NOT_CONFIRMED` / `UNKNOWN`（证据不足不猜）。

**硬约束（operator 2026-09-22）**：不再触发任何睡眠（S3/S4/混合/休眠），以后也不允许 —— `_scratch/.../window-s3-20260922`、`window-s4-20260922` 下的触发脚本均已加永久硬闸（直接 throw），只保留为取证模板。

判型词与来源见 `domains\sleep\sleep_types.json`；判型器 `domains\sleep\lib_sleep_typing.ps1`；fixtures `domains\sleep\fixtures\sleeptype-*.json`。

## 证据

每次运行写一份 JSON（默认 `%LOCALAPPDATA%\YeManCC\sim-evidence\`，可用 `-EvidenceDir` 改）：

- `ymcc-sim-doctor-*.json`、`sleep-sim-*.json`、`gamepad-sim-*.json`、`full-sim-*.json`；
- 内含：工具自身身份（路径/字节/SHA256/版本，可解析回盘上文件）、Host 身份、逐步 HTTP 记录、
  检查项与判定、`reverse`/`reverse2`（反推：产品自己的日志里记了什么）、`extra`。

## 现状与后续

- 睡眠域 20 形态、live 实测、手柄域包装、doctor 自检：**已实现**；
- 手柄睡眠（`-Domain gamepad -Scenario sleep`）**已实现**（204-N：合成电源广播
  `WM_POWERBROADCAST/PBT_APMSUSPEND`→`PBT_APMRESUMEAUTOMATIC`，不触发真实睡眠；默认演练 exit 12，
  `-Confirm` 才投递；**入睡会冻结最大工作集进程**，须在授权窗口、操作者在场时执行；无 App ⇒ exit 10）；
- 整机虚拟测试（`-Domain full -Mode run`）**已实现**：必需段 sleep-typing + sleep-forms + sim-suites
  （`tests\run_all.ps1`，8 套件）；窗口段 sleep-live + pad-sleep 需 `-WithApp`（非提权 ⇒ exit 6），
  未请求时如实记 `NOT_RUN`；`gamepad -Scenario full-virtual` 委派本域并镜像退出码；
- 断言套件：`tests\run_all.ps1` 为总门（单一判定源 + 套件数假绿防护），逐套件亦可单独运行；
- native 层唤醒竞争/看门狗模拟（`tools\wake_race_sim_test.ps1`、`wake_watchdog_sim_test.ps1`）：
  其测试 exe（`native\testrun\wake_race_test\YeManCC-wake-race.exe`）不在盘上（`native\testrun` 目录不存在）⇒
  登记为 `NOT_AVAILABLE`，需原生构建域出 exe；本域的 API 级等价物（`sleep -Mode rapidwake` / `retrybudget`）
  覆盖同类睡眠/唤醒竞争语义（注：这两个原生工具用的合成电源广播原语 `0x0218 + 4/18/7` 与手柄域
  `-Scenario sleep` 一致）；
- 包与身份：`tools\` 不进发布载荷（`package-release.ps1` 不打包仓库 tools；安装树无 `tools\`），
  因此本工具对已冻结的包/payload 身份零影响。

## 来源

`domains\sleep\sleep_forms.json` 记录了形态库的全部出处：本机真实睡眠事实日志
（`PowerControl\Sleep\sleep-facts.log[.previous]`，含 SHA256）、操作者提供的历史日志归档、
以及源码常量（`native\main.cpp`、`FanLab\real-host\Program.cs`、`src\bridge\fanHost.ts`）。