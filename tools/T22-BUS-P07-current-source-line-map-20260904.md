# T22 BUS-P07 当前源码线图（2026-09-04）

状态：`STATIC-SOURCE-LOCATED / PARTIAL-T22 / RUNTIME-BLOCKED`  
冻结快照：`T22-P00-20260904-C01-P05-P07-T21T14-DESIGN-CURRENT`，source digest `3FC345530AD8D974F159D42E94176C4EFE49DC3D77E435B4E3EF9087A1FDAA10`（v2：按 scope/relativePath 排序的 `scope<TAB>relativePath<TAB>fileSha256<TAB>bytes<LF>` canonical SHA-256）。  
HC：`0.32.4.0 / 06c0b9544db2b1f39abf9cd3796225ccfb096103`，clean worktree。

本工件只记录静态读取、hash、文本/二进制 metadata 对照结果。没有运行 build/test、YeManCC、InputHost、HIDMaestro、HidHide、Steam、游戏或硬件操作。v1 porcelain 附件因 `276` 条 manifest 记录只写入 `217` 条 raw data 已失效；v2/v3/R2、C01-CURRENT、P05-CURRENT 与 P05-P07-CURRENT 保留为历史快照。唯一 current P00 入口是此 `P05-P07-T21T14-DESIGN-CURRENT` manifest 与同名 porcelain raw，且它补纳了 P05 mock-producer 边界、P07 v2 索引、T21/T14 current-source 对账与 T15/T16 design re-audit。

## T22-C01：已直接修正的无歧义偏移

依据：`TASK.md` 的 `24`/T16 顺序 `neutral → stop/dispose/join → restore`；不涉及 descriptor、物理身份、游戏消费者或 HC 参数推测。

| 位置 | 原问题 | 最小修正 | 仍不能声称 |
|---|---|---|---|
| `InputHost/Program.cs:44-55` | 同一 file state 同时写 `alive=false,neutral=true` 时，旧代码先 return，`SubmitNeutral` 不执行。 | 先处理 `neutral`，再依 `stopRequested` 退出。 | Host ACK、neutral receipt、release proof、epoch 或 runtime closed。 |
| `native/main.cpp:5221-5235` | `hidHideRestorePhysical()` 发生在写 stop state 与等待 Host 退出之前。 | 写 stop/neutral → wait/terminate/close Host handle → restore physical visibility。 | wait timeout 后的安全释放；强杀仍是 `release-incomplete`。 |

此修正使先前 current-source snapshot 失效，已由 P05-P07-T21T14-DESIGN-CURRENT 重新冻结。未运行任何程序验证。

## T22-P01：参数 lineage

### HC 基线

- `MotionManager.cs:80-113`：HC 先取 calibrated gyro/gravity accel、raw DSU gyro/raw accel，再分别生成 `DefaultGyro *= GyrometerMultiplier` 与 `DefaultAccel *= AccelerometerMultiplier`。
- `MotionManager.cs:263-345`：按 motion space 取值，随后 invert → custom sensitivity curve（calibration gyro threshold）→ ADS trigger/multiplier → velocity accumulator → X/Y sensitivity → clamp；并对 accumulator 做消费与衰减。

### 当前 YMCC 线图

| 层 | 已定位内容 | 当前结论 |
|---|---|---|
| UI/schema | `src/bridge/inputContracts.ts:6-21` 定义 F01–F08 以及 curve、accelerometerMultiplier、gyroThreshold、axisOrder、outputStick/outputAxis 等字段。 | `source-located`，字段存在不等于产品消费。 |
| CAS/hash/activation | `inputConfigHash.ts:2-18,114-117` 与 `gyroConfigActivation.ts:16-102,127-148` 具备 pure revision/hash/exact-ACK 状态机。 | `mock/pure candidate`；注释明确 future native adapter 尚未接线。 |
| native transport | `native/main.cpp:5279-5307` 固定 `(Z,X)`、threshold `2000`、`*1000`、`1.2-stickNorm`，只写 pad 与 `gx/gy/gz`。 | 选定参数没有被从 UI/CAS transport 到产品 Host；`ACK-ABSENT / parameter-not-consumed`。 |
| InputHost sink | `InputHost/Program.cs:116-136` 只解析 base axes 与 `gx/gy/gz`，无 config revision/hash/parser/ACK/readback。 | 不得写“改参已应用”。 |

结论：没有额外可直接修正的单点偏移。把 HC 的整条参数/transaction 链嫁接到 native/Host 是 T10 合同实现，不可用局部常量替换冒充完成。

## T22-P02：motion、校准与 direct IMU

| 路径 | 当前静态事实 | 状态 |
|---|---|---|
| YMCC `g_gmProcess` / virtual-stick | `native/main.cpp:5369-5510` 把 `pairProven=false` 固定，因而同代 pair gate 下的 g_gmProcess、cal-lock 与 Player/World 候选不可达；未 lock 的 virtual-stick contribution 归零。 | 这是 YMCC fail-closed gate，不是 HC `ProcessMotion` 原始签名；`safe-zero` 只限该路径。 |
| direct IMU | 同一 native record 仍写 `gx/gy/gz`；`InputHost/Program.cs:132-136` 无条件写 `GyroDpsX/Y/Z` 并 `SubmitState`。 | `direct-IMU-path-unresolved`；不得由 virtual-stick safe-zero 推导 DS4 no-report。 |
| accel | native telemetry 有 accel 候选，但 state-file/Program.cs 没有 `ax/ay/az` transport。 | `accel-transport-absent`。 |
| calibration | 当前 source 有 confidence/steady/finite/weight 的候选条件与 timeout safe-zero 文字，但缺真实 provider/identity/unit/matrix/sequence/epoch pairing 与 lifecycle proof。 | `RUNTIME-BLOCKED`。 |

没有直接修改 direct IMU：要修正必须先有 descriptor、persona 和 no-report admission 的产品级证据；在此之前改写 gyro 值或强行清零都会是猜测。

## T22-P03：HIDMaestro persona/ABI

- `InputHost/YeManInputHost.csproj:1-16` 是 `net10.0-windows`、x64，静态引用锁定归档 `HIDMaestro.Core.dll`。
- 当前 file-mode 仅在启动时选 `dualshock4` 或 xbox-360 profile，`Program.cs:116-136` 以 `HMGamepadState` 提交 axes 与 gyro。
- 当前 tree 没有由产品代码归档的 descriptor 原字节、reportId、length、offset、signed/endian、encode/decode 或 readback；也没有 `ax/ay/az` 进入 Host。

结论：`descriptor-unverified / persona-enforcement-unresolved`。没有证据支持直接加入 DS4 IMU field layout，也没有证据支持把 X360+DS4-IMU reject 写成已实现。

## T22-P04：ROG 与 P-HID

- native 的 `rogWriteXboxFaceEnabled()`（`main.cpp:14301-14313`）只构造并写出 `5A D1 0B 01 01`；现有 product call-sites 是 boot/resume/suspend/exit/window teardown 的 restore/enabled safety direction。
- `src/bridge/rogInputAdapter.ts:43-45` 仍可构造 `...02`，但在 `src/native/InputHost` 产品路径未定位 import/caller；它目前只保留为 dormant audit/test material。
- `hidHideIsolatePhysicalXbox360()` 仍为 source-present legacy broad-scan helper；本轮没有把“无默认 caller”扩大写为 P-HID 已安全，也没有调用任何 HidHide CLI。

结论：ROG `0x02` product sink 当前未定位，不能声称“仓库已完全移除 Disable”。没有新的无歧义修正；若以后出现 product import/IPC/writer，立即 `SAFE_STOP`。

## T22-P05：P-XINPUT/P-OWNER

- `main.cpp:5535-5564` 只轮询 XInput slot `0…3` 并依据 summon combo 选择 semantic slot；没有 slot→device path→PnP/XUSB→physical identity 的证明。
- `src/bridge/inputCoordinatorMock.ts:41-44,123-128` 的确会分别增加 `gameInputCount` 与 `ymccActionCount`，但文件自己声明为 F1/F2 integration mock，且无 process/HID/PnP/power/filesystem side effect。它是 mock 内部 producer，不是 native、Host、Steam 或游戏消费者测量。
- `src/bridge/inputOwnerRuntime.ts:1-43` 明确只是 renderer-side observability mirror；`gameInputCount` 仅初始化/重置为 `0`，没有 increment；`ymccActionCount` 只在收到 UI semantic action 时增量。`src/gamepad/engine.ts:151-157,232-239` 仅将该 mirror 作为 browser trace，并从 `ipc:gamepad.ui-input` 分发 UI action。
- 受限的 `src`/`native`/`InputHost` identifier 搜索（排除 mock）只定位 mirror 字段与 pure `physicalInputOwnership.ts` 的 disposition label；没有定位 external Steam/game-consumer measurement producer。这是静态范围内的 absence finding，不是外部消费者不可消费的证明。原始命令输出见 `T22-BUS-P07-P05-supplement-static-evidence-20260904.raw.md`。

结论：`gameInputCount=0`（无论来自 mock 还是 renderer mirror）都是 `not-a-consumer-measurement`，不得用于宣称 Steam/游戏不可消费。此项要靠 R1 的指定消费者观察，不能由 UI counter 的代码改动闭合；本发现不满足 T22-Cxx 条件。

## T22-P06：剩余 lifecycle 缺口

C01 后本地顺序已改善，但 state-file 协议仍没有 request/ACK、neutral receipt、Host exit/release receipt、Coordinator epoch、target dispose/join 或 visibility restore receipt。`WaitForSingleObject(...,1500)` 的超时后强杀尤其不能证明 release。

因此 P06 保持 `DGF-02 / release-incomplete / RUNTIME-BLOCKED`。T22 P01–P07 的 raw command output、line-map 和正/负分支已经汇总；当前仅完成 T21/T14 对账和 T15/T16 design maintenance。只有再次发现满足 T22-Cxx 三条件的局部偏移时才直接改源码。
