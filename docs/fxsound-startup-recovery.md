# FxSound 启动自愈

2026-10-22 用户决定：仅在每次启动 YMCC 时检查一次。入口为“设置 → 日志修复 → 音频修复 → FxSound 自动修复”，默认开启。保存开启状态影响下次 YMCC 启动；关闭可以取消当前尚未完成的启动确认，重新开启不会在本次运行中重新检测。

## 启动时的行为

| 启动时或确认期间的状态 | 处理 |
| --- | --- |
| 开关关闭 | 不创建检测线程 |
| 默认播放角色均不是 FxSound，或同一 Windows 会话内 FxSound.exe 正在运行 | 立即结束本次检测 |
| 任意默认播放角色为 FxSound，且 FxSound.exe 未运行 | 开始一次 3 秒确认，每 500 ms 复查 |
| 确认中 FxSound 启动、进程状态未知、设备读取失败、默认角色发生变化、关闭设置或退出 YMCC | 取消，结束本次检测 |
| 采样间隔超过 1.5 秒，例如进入睡眠 | 取消，避免在恢复时补做启动修复 |
| 满 3 秒，条件仍满足 | 仅修复仍指向 FxSound 的角色，并结束本次检测 |
| 没有可用替代设备或切换失败 | 留下失败回执，结束本次检测，不循环重试 |

检测线程退出后，不注册设备监听、不轮询进程、不处理后续 FxSound 启停。用户以后选择 FxSound、正常使用或退出 FxSound，都不会重新触发。本功能不启动、结束、卸载或禁用 FxSound，也不修改其开机启动配置。

## 恢复目标

Windows 当前默认输出既然已经是虚拟设备，就不能再把“读取 Windows 默认”作为恢复目标。正式消费者会从以下来源选择当前仍在线且符合播放设备条件的实际输出：

1. FxSound 保存的设备 ID，顺序为 `most_recent_default`、`original_default`、`most_recent_playback`、`prior_default`。读取 `HKCU\Software\DFX\<version>\23\devices` 的默认 REG_SZ，优先较新版本，并兼容 32/64 位注册表视图。
2. 当前仍为实际设备的 Windows 多媒体或控制台默认输出。
3. 当前可用的耳机、耳麦、扬声器，再考虑线路、SPDIF 和显示器数字音频。按设备 ID 稳定排序消除枚举顺序的影响。

所有候选必须仍在线；排除 FxSound 和名称明确标识的其他常见虚拟输出，不启用已禁用的端点。不满足条件时保留当前状态并记录失败。控制台、多媒体、通信三个播放角色分别在提交前复核，只修改仍属于故障的角色，不覆盖原本正常的通信输出或麦克风。

本机只读记录中，FxSound 保存的原默认设备 ID 是 `{0.0.0.00000000}.{aba2417f-f3b9-4493-b2da-b50bc2bd0650}`，对应“扬声器 (AIRPULSE A100)”。本轮未执行真实默认输出写入。

## 代码与证据

- `native/fxsound_recovery_policy.h`：正式单次启动消费者；成功、取消、失败均终结，后续调用不再读取或写入。
- `native/fxsound_recovery.h`：后台线程、Windows Core Audio 只读枚举、同会话 FxSound 进程判断、FxSound 注册表记录以及 COM 默认角色切换。切换前再次复核开关、进程、原端点与目标，切换后读回确认。
- `native/main.cpp`：原生启动入口、统一设置保存/重置和退出收尾。WebView 重建、重新进入设置、后续开启开关不会重启检查。
- `src/bridge/uiSettings.ts`、`src/views/SettingsView.vue`：设置持久化与控件，保存失败恢复真实设置并显示错误。
- `tools/fxsound_recovery_selftest.cpp`：通过内存音频/进程端口执行正式消费者；覆盖 3 秒边界、启动/退出 FxSound、关闭后再开启、睡眠间隔、角色变更、目标不可用、失败不重试和本次运行后续零读取/零写入。
- `tools/ymcc_sim/global/ui-navigation/templates/fxSoundRecoveryCases.mjs`：现有正式页面和导航 engine 内的 A/B、持久化及重入回归，覆盖 1280×720 和 1280×800；发布门要求新状态存在。

回归命令（证据目录必须不存在并位于源码目录外）：

```powershell
pwsh -NoProfile -File tools/fxsound_recovery_selftest.ps1 -EvidenceDir G:/YeManCC-Work/Build/Tasks/FxSound-Recovery-<new>/native -Inventory -NegativeControls
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Tests controller.ui-navigation -EvidenceDir G:/YeManCC-Work/Build/Tasks/FxSound-Recovery-<new>/ui
```

`-Inventory` 仅通过实际 Win32 端口读取本机设备与默认角色，默认测试和破坏反例只使用内存端口。4 个破坏反例分别移除“本次运行不得重新触发”、3 秒确认、进程启动取消和虚拟候选排除，必须在对应行为断言失败。软件回归和编译不等于真实切换或实物手柄验收。

核对的上游为 `fxsound2/fxsound-app`，固定提交 `d8e7a23d37ed5939c2a3090a1c1756c7f2500b17`。依据为 `audiopassthru/src/sndDevices/sndDevicesReg.cpp` 的注册表路径、`audiopassthru/include/sndDevices.h` 的键名及同目录 `PolicyConfig.h` 的接口 ABI。上游快照、SHA-256、历史失败及本轮回执保存在工作区 `Build/Tasks/FxSound-Recovery-20261022`。

## 2026-10-22 本轮验证状态

- 原生主程序已在工作区独立编译、链接并嵌入 manifest；未复制到安装目录或启动应用。
- 正式启动消费者 72 项断言通过，4 个破坏反例均在行为断言处被拒绝；本机只读设备枚举通过，无音频写入。
- 最终 FxSound 设置入口按当前源码重新构建，复用正式导航状态遍历及 A/B 用例，在 720/800 两种高度均通过，共 14 个焦点、56 次方向检查、2 个持久化/A/B 场景；截图在证据目录 `focused-current-ui`。
- 历史全页回归曾通过 228 状态、18624 方向、418 动作及精确源码门。随后并行任务移除了 Settings 中的 Steam 诊断入口，这份全页回执已不匹配当前页面，不能用于当前发布。最新全量重跑另遇到正在变动的 gameTrainer 源码的暂时字符串语法错误，后续当前 `vue-tsc --noEmit` 已通过；没有改写这两个并行任务。
- 当前 FxSound 定向验证与整包发布分别记录：`verification.json` 的 `featureChecksPassed=true`，`releaseGatePassed=false`。安装版、整包发布及真实默认设备切换尚未放行或执行。

## 2026-10-22 入口调整

按用户要求，将设置分区“诊断”改名为“日志修复”，FxSound 自动修复从设备分区移入该分区，放在日志操作下方。内部 `diagnostics` 分区标识保留。启动时一次检查、3 秒确认与设置保存逻辑不变。

当前源码专项回归在 1280×720 和 1280×800 均通过，18 个焦点、72 次方向检查、2 个开关保存与 A/B 返回场景，覆盖“设备分区没有此入口”“日志修复名称正确”和重入后保存状态。TypeScript 检查通过。证据目录为 `Build/Tasks/FxSound-Recovery-20261022/ui-log-repair/focused-ui-final`；这是专项验证，整包发布及安装版仍未放行或更新。
