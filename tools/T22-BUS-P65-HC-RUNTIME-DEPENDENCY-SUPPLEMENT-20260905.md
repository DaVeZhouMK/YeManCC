# T22-BUS-P65：HC runtime 依赖补齐与隔离探针工具链闭口（2026-09-05）

Status: `STATIC-EVIDENCE-CLOSED / TOOLCHAIN-CORRECTED / RUNTIME-BLOCKED`

本页记录 BUS 对 HC 锁定 runtime 五个依赖、隔离探针实现和 ROG 测试包的补充审计。动作只涉及隔离目录、审计工具、证据工件和独立测试包；没有启动 YeManCC/InputHost、HIDMaestro、HidHide、Steam、游戏或虚拟设备，也没有修改正式 Release/updater 包。

## 1. HC 依赖事实

来源均为锁定 HC `0.32.4.0 / 06c0b9544db2b1f39abf9cd3796225ccfb096103` 的候选构建输出，并已导入主线隔离 runtime：

| 文件 | bytes | SHA-256 |
|---|---:|---|
| `WpfScreenHelper.dll` | 17,920 | `727C1669A6094285F0ED771263A35847B6305B13A59562265AA955CC2E07B76F` |
| `GongSolutions.WPF.DragDrop.dll` | 214,392 | `1672736512DDBB8608795C25A54D3C040EBC570C46E3A332CE52727A19D2140E` |
| `GameLib.Core.dll` | 12,288 | `1946B5F67A272A9F0BA25962E091A331C19261E747CC138EF31051B66CE111D8` |
| `SDL3-CS.dll` | 1,300,480 | `23CC2258B6F5229C20FF631F518C23F8736E8F4D8BFA388601672D12299A20A9` |
| `iNKORE.UI.WPF.Modern.dll` | 7,405,056 | `7650B744E0ADD04549593F680EBF1C8FE40125F36EB8C86445113B974EE25D6C` |

锁定目录：

```text
Mainline/YeManCC-source/YeManCC/PowerControl/handheldcompanion-runtime/HC-CANDIDATE-0.32.4.0-06c0b954-20260902
C:/SOFT/YeMan/PowerControl/handheldcompanion-runtime/HC-CANDIDATE-0.32.4.0-06c0b954-20260902
```

`verify-hc-candidate-runtime.ps1` 结果为 `PASS matches=96/96`；`hc_input_dependency_audit.ps1` 结果为 `PASS missing=0`；`hc_dependency_location_audit.ps1` 结果为 `LOCKED_RUNTIME_PRESENT`。96 是 manifest 受审计文件数；目录中另有 manifest/runtime 元数据文件，不将“目录文件总数”误写为 manifest count。

## 2. 隔离探针修正

旧版 Windows PowerShell 反射探针同时存在两个工具链误报：

1. HC `HandheldCompanion.dll` 是 net10 托管程序集，PowerShell 5.1 反射时缺少 `System.Runtime, Version=10.0.0.0`；
2. `GamepadMotion.dll` 是 native DLL，不包含托管 assembly manifest，不能用 `Assembly.LoadFrom()` 判定。

本轮修正为：

- `tools/hc_net10_probe/Program.cs` 使用 net10 `AssemblyLoadContext` 枚举 HC 类型；仅读取 `ManagerFactory` 的静态元数据，不调用静态构造器、ManagerFactory、设备管理器或任何输入 API；
- 对 `GamepadMotion.dll` 跳过托管加载，改用 `NativeLibrary.Load` + 21 个锁定源声明导出名的存在性检查；不调用任何 native export；
- `tools/hc_isolated_dependency_probe.ps1` 不再生成/运行旧 PowerShell loader，改为创建临时隔离副本后转交 net10/native-aware probe；旧 evidence 的 `BLOCKED` 仅表示旧工具链，不能当作产品依赖缺失。

证据：

```text
A1-hc-net10-probe.json
status            = PARTIAL_LOAD_PASS
HC typeCount      = 1702
GamepadMotion     = native / managedAssemblyLoad=skipped
native exports    = 21/21 present; no export invoked
managerFactory    = metadata-only; not invoked

A1-hc-isolated-dependency-probe.json
status            = PARTIAL_LOAD_PASS
probeImplementation = net10-native-aware
systemMutation    = false
productRuntimeMutated = false
```

探针通过只读验证了“依赖可被正确装载/识别”，没有证明 HC ManagerFactory 输入闭包、HIDMaestro consumer receipt、Steam/game 隔离或 ROG runtime 闭环。

## 3. ROG 测试包事实

重新生成的 standalone 测试包：

```text
ZIP    = Mainline/Build/TestPackages/GyroInput-ROG-20260906-020604/YeManCC-GyroInput-ROG-Test.zip
bytes  = 88,546,692
SHA256 = 61C823E4D50A3BFD598CF28626E93455F4F7925BCC1A270F29A5CD354B441C61
HC     = 0.32.4.0 / 06c0b9544db2b1f39abf9cd3796225ccfb096103 / 96 files / mismatch 0
```

manifest 明确包含完整 YMCC、InputHost、HIDMaestro/WinRT、锁定 HC runtime、`gyro-motion`、`virtual-gamepad` 和 HidHide 安装包。采集器默认 30 秒，仍为 5 秒静止 + 20 秒慢转 + 5 秒静止；采集器不调用 HidHide、不发送 OEM Disable、不改 sensor ReportInterval、不创建虚拟控制器、不操作 Steam/游戏。

```text
formalReleasePackageUntouched = true
systemMutationDuringBuild     = false
runtimeOperationDuringBuild   = false
```

## 4. PS4 “电量低 - 5%”问题登记

该现象已在 BUS-P64 登记，本轮不改变裁决：

```text
PS4 5% origin = UNKNOWN / EXTERNAL-OR-ENCODER-DEFAULT-POSSIBILITY
```

静态 HC/YMCC/HIDMaestro 证据没有证明当前 VIIPER DS4 或 HIDMaestro encoder 主动写入 5%。不得把值猜改为 100%，不得把 HC DSU `DsBattery` 映射猜写入 HIDMaestro。关闭该未知仍需要同一 Host 生命周期中的虚拟 DS4 descriptor、原始 input/feature report、report ID/length、raw bytes 和设备 instance/container identity，并由独立 decoder 复核；Steam UI 数字本身不足以归因。

## 5. 三方裁决与剩余缺口

| 项目 | HC/source 事实 | 当前裁决 |
|---|---|---|
| 五个 HC 依赖 | 锁定候选输出中存在，96/96 integrity PASS | `STATIC-CLOSED` |
| 隔离 probe | net10 托管枚举 + native export presence PASS | `TOOLCHAIN-CLOSED`，非 runtime closure |
| ManagerFactory 输入图 | 静态宽图仍含 13 managers/4 direct-token managers | input-only composition `UNENCLOSED`；不裁剪、不改 HC |
| PS4 5% | 无当前 VIIPER/HIDMaestro battery assignment 证据 | `UNKNOWN`，需外部 raw report provenance |
| gyro/accel pair、ROG matrix、DS4 six-int16 readback | 尚无同 provider/epoch/matrix/consumer receipt | `UNENCLOSED / RUNTIME-BLOCKED` |
| P-HID/P-XINPUT/P-OWNER、sleep/PnP/crash restore | 只有静态/mock 证据 | `RUNTIME-BLOCKED` |

本轮没有发现可依据锁定 HC source 无歧义直接修正的轴交换、符号、单位、倍率、deadzone、velocity decay、电量赋值或生命周期顺序偏移。未修改产品源码、JSON、正式包或 HC 行为。

## 7. Evidence-quality selftest 修正

静态回归第一次运行 `evidence_quality_gate_selftest` 时失败，原因是测试脚本错误地要求 `native/main.cpp` 必须包含 `EXACT_HC` 字面量；当前 native source 已不含该旧注释，而 `src/bridge/hcInputUtils.ts` 仍保留 HC 子集标记，且 `EvidenceQualityGate.v1.json` 的 `legacy partial candidate` 自动降级规则存在。该失败属于自检前置条件过严，不是产品行为错误。

已将自检改为：

- 始终验证 `EvidenceQualityGate.v1.json` 保留 legacy partial-candidate 自动降级规则；
- 在 native 与 `hcInputUtils.ts` 两个相关 source 中统计旧标记（可为 0），不再把旧注释存在性当成 gate prerequisite。

修正后：

```text
pnpm run test:evidence-quality-gate = PASS
runtime/device/system mutation      = none
```

该修正只改变审计测试的判定范围，不放宽任何 runtime tier、不提升 HC parity、不改变 `runtimeUpgrade=false`。

## 6. 下一步门

由于本轮修改了 `hc_net10_probe/Program.cs`、`hc_isolated_dependency_probe.ps1` 并新增本页，旧 71-key RF00 失效。必须重新执行：

```text
T22-RF00 → T20 → T21 → T14
```

之后可继续静态回归。真正关闭 runtime 仍需 ROG 测试包回传和虚拟 DS4 原始 report/consumer provenance；这是明确的用户介入点。
