# T15/T16 实证闭口评估与测试设计（v1）

状态：`DESIGN-ONLY / P17-STATIC-SNAPSHOT-BOUNDARY / NOT-EXECUTABLE / RUNTIME-BLOCKED`  
日期：2026-09-04  
适用范围：仅 T15（T10 参数消费与 Host ACK 实证）和 T16（T11 校准、漂移安全与释放生命周期实证）。

## 1. 权威来源与边界

本工件的权威顺序如下：

1. [GyroVirtual/TASK.md](../../../../Docs/Tasks/GyroVirtual/TASK.md) 的 BUS 主线段；
2. [30-GYRO-PARAMETER-SHIELD-RESET-ACCEL-CONTRACT-20260904.md](../../../../Docs/Tasks/GyroVirtual/30-GYRO-PARAMETER-SHIELD-RESET-ACCEL-CONTRACT-20260904.md) §8–§10、§14–§15；
3. [24-UNIFIED-LIFECYCLE-AND-COORDINATION-CONTRACT-20260903.md](../../../../Docs/Tasks/GyroVirtual/24-UNIFIED-LIFECYCLE-AND-COORDINATION-CONTRACT-20260903.md)；
4. 已冻结的 HC `0.32.4.0 / 06c0b954` 对应的 motion、calibration、layout 生命周期语义。
5. [P17 manifest](T22-RF00-SIDEBAR-P17-C16-HOSTFRAME-current-source-20260905-manifest.json)、[P17 line-map](T22-RF00-SIDEBAR-P17-C16-HOSTFRAME-current-source-20260905-line-map.md) 和 [P17 T21→T14 report](T21-T14-RF00-SIDEBAR-P17-C16-HOSTFRAME-current-source-reaudit-20260905.md)。它们是 `2026-09-05T04:48:38.6795365Z` 的 53-key source 静态快照；其后 authority/source 改动必须重新 RF00，不能由本设计文件提升为 runtime 资格。

本文件不以已删除的独立 `31-*` 或 `32-*` 文档为 authority。它是未来实证的设计与准入门，不是实测结果，也不把 mock、static audit、API located、历史包或单项成功提升为 runtime 结论。

本设计当前只引用 P17 的静态 source/provenance 回链。C16 曾有独立的 source implementation type-check、contract selftest 与 native compile 记录，但它们不属于 P17 也不构成 T15/T16 runtime 实证。P17 阶段**未**运行 test/selftest、构建、启动 YeManCC/InputHost、创建虚拟设备、调用 HIDMaestro/HidHide、访问真实设备、启动 Steam/游戏或执行 R1。

## 2. 本轮评估结论

| 闭口项 | 当前结论 | 不能执行的直接原因 | 可记录的安全结果 |
|---|---|---|---|
| T15 / T10 参数与 ACK | `NOT-EXECUTABLE / RUNTIME-BLOCKED` | native 与 state-file Host 协议没有合同要求的配置事务、精确 ACK 和同代首帧证明；8 个字段没有全部进入真实 native map/layout/report 路径。 | 对当前实现只能记录 `parameter-not-consumed` 或 `no-report`，不得声称“参数已生效”。 |
| T16 / T11 校准与漂移安全 | `NOT-EXECUTABLE / RUNTIME-BLOCKED` | `pairProven=false` 是 YMCC 对自身 `g_gmProcess`/virtual-stick 路径的 fail-closed gate，而不是 HC `ProcessMotion` 的 raw 参数签名；当前仍缺同代配对证据、direct IMU product no-report gate、以及 Coordinator 级 epoch/release/join/recovery proof。 | virtual-stick 可保留 `sample-pair-unproven / safe-zero` 候选；direct `gx/gy/gz` state-file→InputHost 路径为 `direct-IMU-path-unresolved`，不得声称 Manual、漂移闭口、恢复或 direct no-report 已通过。 |

因此，当前不应要求操作者插入设备或执行任何“验证”。在合同实现尚未接线前，这类操作不能产生 T15/T16 的有效闭口证据。


### 2.1 T22 source-identity 再验证（2026-09-04，read-only）

T22 P01–P07、line-map、P07 v2 对冻结时刻的 artifact completeness 可通过，但“current-source”资格失败：manifest 记录 `native/main.cpp=034B028E…B54683D / 1,048,967 bytes`、`sourceDigest=3FC34553…FDAA10`、porcelain `304`；只读复核为 `A8210C34…0B1CAD / 1,049,153 bytes`、`sourceDigest=2E8BB197…FB2C20`、porcelain `305`（均为本次 MD 回写前的只读值）。本轮仅改 TASK/30/本设计三份 manifest DOC key file，故回写后的未冻结身份仍由 RF00 统一捕获；因此 §3 仅是**冻结快照事实**，不得投射为现行源码。继续 T15/T16 准备前，先完成 `T22-RF00 → T21 → T14`；该门仍是文档/哈希/静态对照，非 test/build/runtime 授权。

### 2.2 P17 / C16 对 T15/T16 的新增准入边界（2026-09-05，design only）

P17 已完成 `T22-RF00 → T21 → T14` 的 snapshot-only 回链，但它只证明 C16 的 source 字段路径：`inputHostWrite` 的 local atomic replacement → `hostFrame.stateWrite` → `gyro.telemetry` → UI。未来 T15/T16 必须把这条记录单列为 `native-state-write`，不得把 `stateWrite='written'` 填入 `host-ack`、`first-active-frame`、`HID-report` 或 `consumer-observation`。

| future observation | T15/T16 可写结论 | 禁止替代 |
|---|---|---|
| `hostFrame.stateWrite=written` + same native sequence | native 在该 sequence 完成 state-file replacement | Host applied ACK、same-epoch first frame、HID report 或游戏输入 |
| `hostFrame.stateWrite=write-failed/not-running` | native transport 未提交；保持 no-report/safe-stop | “Host 保持旧帧可用”或“失败后自动恢复” |
| independent Host reply carrying same `hostInstanceId/epoch/revision/configHash` | 仅可进入 ACK candidate；仍须同代 first-frame | consumer observation 或 release proof |
| independent report/readback + separately authorised consumer observer | 对该 persona/device/consumer 的限域观测 | 对其他 persona、SKU、游戏或恢复路径的泛化 |

P17 不消除 T15 的 F01–F08 参数消费缺口，也不消除 T16 的 provider/pair/calibration/epoch/release 缺口。没有实体数据时，以上表格只是未来证据包 schema，不是可执行通过项。

## 3. T22 冻结源码快照事实（评估依据，不是通过证据）

### 3.1 T15

- 在 T22 冻结快照中，`native/main.cpp::inputHostSubmitPad` 固定采用 LocalSpace `(Z, X)`、阈值 `2000`、倍率 `1000`、`1.2 - stickNorm` 权重；若干函数输入被忽略。它不是由完整 GyroMotionConfig 驱动的 map/layout 消费者；RF00 前不得称此为当前 source。
- 在 T22 冻结快照中，`inputHostWrite/inputHostStart/inputHostStop` state-file 仅承载 alive、neutral、手柄数值，不承载/回读 runId、requestId、epoch、powerGeneration、owner、targetId、persona、configRevision/configHash、provider/physical identity、calibration、visibility transaction；RF00 前不得外推到当前 source。
- 在 T22 冻结快照中，`InputHost/Program.cs::RunFile` 读取 state-file，alive=false 时退出、neutral=true 时送 neutral，否则提交状态；file protocol 不写 exact appliedRevision/appliedHash/status ACK。RunStdin 的一般 Reply 也不替代 file-protocol 的 T10 ACK/first-frame proof。
- `tools/ParameterConsumptionLedger.v1.json` 对该冻结 snapshot 把 T10-F01…F08 标为 native not-consumed、fixed candidate 或 unadmitted mode；它是负例台账，不是待运行的通过清单，RF00 后需刷新其 source pointer。

### 3.2 T16

- 在 T22 冻结快照中，`inputCaptureOnGamepad` 将 pairProven 固定 false；这是 YMCC 在调用 g_gmProcess 前的同代 pair-proof fail-closed gate，不是 HC ProcessMotion raw 参数签名，亦不能证明真实 provider/identity/unit/matrix/sequence/epoch。
- 在该冻结快照中，未准入候选把 Manual 限制为 confidence==1 && steady && finite offset && weight>0，timeout 为 safe-zero；但它只约束 virtual-stick motion，`gx/gy/gz → state-file → InputHost GyroDps → SubmitState` direct IMU dataflow 未证明 descriptor/no-report。
- 在 T22-C01 冻结快照中，`inputHostStop` 先写 `alive=false / neutral=true`；若 file-mode 在被强杀前读取该 record，它会尝试先 SubmitNeutral、再退出；native 在 Host wait/terminate/handle-close 后才恢复 P-HID。该语义只是 conditional local best-effort，不是 child ACK、neutral receipt、release proof、唯一 Coordinator 身份、fresh epoch、participant stop/dispose/join proof 或恢复目标裁决；1.5 秒后的强杀路径仍是 `release-incomplete`。该条必须经 RF00 再确认，不能称当前源码已闭口。

## 4. T15 实证设计：参数进入合帧与 Host ACK

### T15-EX-00：强制前置门

以下条件必须全部成立，才允许开始任一 T15 用例；任一缺失即记录失败原因并停止为 `parameter-not-consumed / no-report`，不得改用旧 direct InputHost 路径猜测结果。

1. 仅有一个符合 `24` 的 Coordinator，且它是 `runId`、`epoch`、`powerGeneration`、`targetId`、`configRevision` 与 `ownerIdentity` 的唯一裁决者；participant 不得绕过它写 target。
2. T10 request、revoke、旧 publication block、neutral、release/rebind、fresh epoch、Host ACK、same-generation first frame 和 `active` 按 `30` §8.2 的单一串行顺序记录。
3. 每个 request/ACK/reject/neutral/release 可关联同一组：`runId, requestId, epoch, powerGeneration, owner, targetId, persona, configRevision, configHash, providerId, physicalIdentity, calibrationId, visibilityTransactionId, sampleSequence, timestampUtc`。
4. Host ACK 必须精确回送 `appliedRevision`、`appliedHash`、`status`、`hostInstanceId` 和 neutral proof；错 revision/hash、旧 epoch、重复或迟到 ACK 必须被拒绝，不能改变 UI 的最新 saved-pending 快照。
5. ACK 后才可接收首个同 epoch、同 revision/hash 的 raw→calibrated→mapped→base-stick→assembled/report 记录；在此之前不准入 active。
6. T10-F01…F08 的实际设置值必须被 native map/layout/assembler 消费，而不是只由 UI、CAS、mock 或日志保存。每个字段要能关联到 HC 对齐的顺序与对应 output 层。
7. revision 变更要清除旧 velocity accumulator、toggle、held mapping state 和旧 mapped XY；neutral proof 先于新首帧。
8. 测试 target、persona、consumer observation 与恢复范围必须已单独批准；本设计不授权创建或写入虚拟 target。

### T15-EX-01：固定样本与观测面

每一个参数组只能独立变更一次，使用相同、已 hash 的固定 raw motion 样本与相同 base-stick 样本。对每帧必须保存：

```text
raw sample
  -> calibrated sample (calibrationId / provider identity / unit / matrix)
  -> mapped gyro XY
  -> base-stick XY
  -> assembler input and output report
  -> consumer observation (only if separately authorised)
```

每帧同时携带 T15-EX-00 的事务字段。任何非受影响层的变化、隐式重标定、跨 revision 状态泄漏、未证明 consumer 的“看起来可用”，均为失败或 `UNENCLOSED`，不是可接受误差。

### T15-EX-02：F01–F08 回放矩阵

| 用例 | 仅改变的配置组 | 必须变化 | 必须不变 | 关键拒绝/失败结果 |
|---|---|---|---|---|
| T15-F01 | `gyroMultiplier` | mapped gyro XY、assembled/report 的 gyro 贡献 | raw、calibrated、base-stick | `parameter-not-consumed` |
| T15-F02 | `motionSensitivityX` 或 `motionSensitivityY` | 对应单一输出轴与 clamp | 另一输出轴、raw、calibration | `axis-cross-coupled` |
| T15-F03 | `invertHorizontal` 或 `invertVertical` | 对应 mapped 轴符号 | provider matrix、另一轴、base-stick | `invert-before-matrix` |
| T15-F04 | `gyroWeight` | layout/assembler 中 gyro 与 base-stick 的 blend | raw、calibrated、独立 base-stick | `weight-not-applied` |
| T15-F05 | `innerDeadzone`、`outerDeadzone`、`antiDeadzone`、`outputShape` | AxisActions modifier 后的 gyro vector 与 assembler 贡献 | raw、calibrated、modifier 前的 map | `modifier-order-mismatch` |
| T15-F06 | `velocityMode`、`velocityScale` | 多帧 accumulator 路径输出 | raw、单帧 calibration、切换前的旧 accumulator | `accumulator-leak` |
| T15-F07 | `motionInput`（Local/Player/World/JoystickSteering） | 经 HC 对齐、被批准的空间/steering 层输出 | provider identity、unit、认证 SKU matrix、无关输出层 | `motion-space-unavailable` 或 `axis-matrix-conflict` |
| T15-F08 | `enabled`、`outputMode`、`motionMode`、`motionTrigger` | admission 与 gyro contribution；关闭时 gyro 归零 | base input 的正常表达、neutral/release 语义 | `gyro-off-base-input-corrupted` |

每一行都必须完整走一次：`request(N+1/H) → revoke → neutral proof → exact Host ACK(N+1/H) → same-generation first frame → active`。不允许从旧 N 直接换算出 N+1 的 report。

### T15-EX-03：强制负例

至少必须覆盖：

- stale ACK：旧 revision、旧 hash 或旧 epoch 的 ACK 迟到；保持最新设置 `saved-pending`，记录 `stale-ack-rejected`；
- hash mismatch：Host ACK 不是完整规范化 `input` 段的精确 hash；不得 active；
- Host restart：重启前帧、ACK、accumulator、toggle 和 held state 全部无效；需 fresh epoch、neutral 和同代首帧；
- no-first-active：ACK 后没有同代有效帧；状态只能 `acknowledged`，不得显示 applied；
- config race：N+2 在 N+1 ACK 前保存；N+1 结果不得覆盖 N+2；
- old-state leakage：F05/F06/F08 切换后，旧 modifier/velocity/toggle/held mapping 都不得影响新 revision；
- 无 target、身份不一致、asset 不存在或 capability 未批准：只能 `rejected` 或 `safe-zero`，不得降级直写 report。

## 5. T16 实证设计：校准、漂移安全与生命周期

### T16-EX-00：强制前置门

以下条件缺任一项，T16 只能是 `safe-zero / UNENCLOSED`，不可开始正例校准回放：

1. provider capability pairing 已证明：gyro 与 accel 来自同一逻辑设备、同一 physical identity、同一 power generation，并可逐帧关联 sample sequence；不允许通过“看起来同时有数据”推断配对。
2. 每个输入都带已验证的 providerId、physical identity、device/firmware/ABI、unit、axis order、认证 SKU matrix、timestamp、sample sequence；gyro 与 accel 的单位和 matrix 不可猜测或混用。
3. `GamepadMotion`/HC 对齐处理真实消费该同代样本，且 raw、calibrated、mapped、calibration provenance 与 report 之间可追溯。
4. calibration provenance 至少含：`calibrationId, providerId, physicalIdentity, unit, matrix, ABI, threshold, offset, weight, confidence, steady, powerGeneration, calibrationEpoch, timestampUtc`。
5. 唯一 Coordinator 能记录并执行 owner/admission 撤销、neutral、listener stop、target stop/dispose/join、release proof、fresh epoch、显式 rearm；睡眠、PnP、close 和 crash 不得自动 active。
6. 真实设备、virtual target、consumer observation 和故障注入范围必须取得独立授权。T16 不能以 HidHide 或 DS4 descriptor 存在替代校准/lifecycle 证据。

### T16-EX-01：唯一正例

用经批准的同一 provider/identity/matrix/unit 样本，在持续静止窗口中从 `unbound → collecting → candidate-steady → lock-requested → manual-bound` 回放。只有下列条件在同一 calibration epoch 内全部满足时，才可写 Manual：

```text
confidence == 1
AND steady == true
AND offset is finite
AND weight > 0
AND gyro/accel identity, provider, unit, matrix and powerGeneration match
```

正例还必须证明：

- Manual-bound 前无 gyro publication；
- lock 所用 calibration provenance 与首个 active motion/report 相同；
- 首个 active frame 仅发生在 fresh epoch、neutral proof、相同 calibrationId 和同代 sample 之后；
- 原始 base-stick 不因校准锁定而被污染；
- 停止/关闭后有 completed release proof，恢复必须 explicit rearm，不得沿用旧 calibration epoch 或旧 frame。

### T16-EX-02：强制负例与恢复矩阵

| 用例 | 注入/条件 | 预期安全结果 | 禁止结论 |
|---|---|---|---|
| T16-N01 | `confidence=0` | 不得 Manual；保持 collecting/candidate 或 safe-zero | “已自动校准” |
| T16-N02 | 5 秒到时但未满足门限 | `calibrate-timeout / safe-zero`；不写 Manual | “超时后锁定” |
| T16-N03 | 静止窗口内持续运动或 `steady=false` | 重置/拒绝候选；不发表 gyro output | “近似静止即可” |
| T16-N04 | offset/weight/sample 含 NaN、Inf 或非法单位 | reject + safe-zero，原始异常值不得进入 map/report | “clamp 后继续” |
| T16-N05 | gyro/accel sample sequence 不同代、provider/identity/unit/matrix 不匹配 | YMCC `g_gmProcess`/virtual-stick 为 `sample-pair-unproven / safe-zero`；direct IMU 必须由未来产品级 no-report gate 明确阻断，否则为 `direct-IMU-path-unresolved` | “按时间相近视为配对”或“所有 DS4 IMU 已 no-report” |
| T16-N06 | provider disconnect 或 PnP remove | revoke、neutral、stop、dispose/release；旧 target/epoch 均拒绝 | “自动续写” |
| T16-N07 | suspend/resume | suspend release/fault proof；resume fresh epoch、重验、neutral、显式 rearm | “睡眠后自动 active” |
| T16-N08 | Host/主进程 crash 或 stop | 外部 recovery owner journal、无并发写、release proof；无自动恢复 | “进程退出即释放完成” |
| T16-N09 | release incomplete、join timeout 或 target 未确认移除 | `faulted / safe-zero / UNENCLOSED`，阻止后续 active | “已尽力停止即可” |

T16 的 `safe-zero / no-report` 只证明本任务在该失败条件下没有把 motion 继续送入已批准的路径；它**不**证明 DS4 descriptor、虚拟手柄 persona、游戏消费者或 T17 已成功。

## 6. 未来执行时的证据包

未来每次执行应创建独立、不可覆盖的证据目录：

```text
tools/evidence/T15-T16/<utc-date>/<runId>/<test-id>/
  manifest.json
  command.json
  stdout.raw.txt
  stderr.raw.txt
  source-tree.sha256
  binary-and-fixture-hashes.json
  coordinator.ndjson
  native-state-write.ndjson             # C16 local atomic state-file write receipt only
  host-ack.ndjson
  raw-calibrated-mapped-report.ndjson
  neutral-and-release.ndjson
  recovery.ndjson
  consumer-observation.ndjson        # only where separately authorised
```

`manifest.json` 至少必须保存：执行者/生成者、UTC 开始结束时间、argv、cwd、经过脱敏的环境、exitCode、原始 stdout/stderr 文件 hash、source commit 与 working-tree digest、fixture hash、产物 hash、HC/runtime/driver 版本、`runId`、requestId、epoch、powerGeneration、configRevision、configHash、targetId、persona、provider/physical identity、calibrationId、consumer、恢复范围和最终结果。

所有日志必须是原始可关联记录；禁止只保留人工摘要、屏幕截图、覆盖写 JSON、没有命令 provenance 的 selftest 输出，或将一项 participant 成功写成完整 runtime-closed。

## 7. 当前审计记录

本轮有一次只读读取 `InputHost` 的 PowerShell 探针误用了保留变量 `$Host`，PowerShell 返回 `Cannot overwrite variable Host because it is read-only or constant.`。后续改用 `$hostLines` 完成读取。该错误未启动产品、未修改源码、未改设备或系统状态；它仅作为审计命令错误记录，不能产生或否定任何产品证据。

## 8. 闭口判定和下一授权点

T15/T16 只有在所有前置门、每个适用正负例、同代 first-frame、consumer observation（需要时）和 completed release/recovery proof 都在同一可归属证据包内时，才可逐项由 `RUNTIME-BLOCKED` 进入相应的已观察场景结论。任何未知、缺失、偏差或错误均必须登记为 `UNENCLOSED / SAFE_STOP / RUNTIME-BLOCKED`，不得自行补设需求、猜测 HC 行为或扩大测试范围。

继续推进需要用户审核本设计，并分别授权以下后续动作：

0. `T22-RF00 → T21 → T14` 的 P17 冻结快照回链已完成；本文件当前绑定 `T22-RF00-20260905-SIDEBAR-P17-C16-HOSTFRAME-STATIC-SNAPSHOT`，但 authority/source 在 capture 后变更即需再次 RF00，且不因此获得 runtime 执行资格；

1. 实现 T10/T11 合同符合的 runtime 接线（Coordinator、transaction、Host exact ACK、provider pairing、calibration/lifecycle release）；
2. 对该实现执行构建；
3. 在明确列出的安全范围中执行 T15/T16 实证；
4. 如测试涉及真实设备、virtual target、HIDMaestro、HidHide、Steam/游戏消费者或睡眠/PnP/crash 注入，再对相应范围单独授权。

在这些授权完成前，本工件是本轮的终点，不应发起操作者介入的无效测试。

## 9. BUS-P08 source correction boundary（2026-09-04）

在 RF00 之前对当前候选源码做了三项局部静态修正：完整 `gyro-motion + virtual-gamepad` 双目录 gate、`pairProven && g_gmLocked` 的 GyroDps state-file 准入，以及 HC 无曲线节点时 `ApplyCustomSensitivity` 的 `posAbs` fallback。它们只修正明确的本地偏移，不构成 T15/T16 runtime evidence。

ROG matrix 泛化、provider/identity pairing、descriptor/consumer、Host ACK、epoch、release/join/recovery 仍为 `UNENCLOSED / RUNTIME-BLOCKED`；T16 的 direct-IMU `no-report` 只能表述为当前 state-file gate 已收紧，不能宣称 HIDMaestro wire 层已经证明 no-report。源码和本文件已变化，必须先重新执行 `T22-RF00 → T21 → T14`，再刷新本设计的 source pointer。

`T22-C05`：源码现在只在 `g_rog.identityMatched` 时应用 HC ROG matrix；非 ROG/未知设备保持原始 Windows Sensor 轴。MSI Claw、Lenovo 及其他特殊形态仍不能用 ROG matrix 推断，需分别具备 HC device identity/matrix 证据。
