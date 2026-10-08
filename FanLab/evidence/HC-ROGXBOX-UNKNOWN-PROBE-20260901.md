# ROG Xbox Ally / Ally X HC UNKNOWN 探针

日期：2026-09-01  
范围：只读探针；未修改源码、未启动 FanHost、未访问硬件。  
目标：以受控 HC 源码为基准，优先核对 ROG Xbox Ally / Ally X 的机型路由、风扇生命周期、睡眠唤醒和退出/门禁未知项。

> **编号说明（2026-09-17 审计 §28.46 追加）**：本文件为多轮**追加式**记录，章节编号存在
> **重号与乱序**——`## 13.` 出现两次（L131 BUS 两轮探针 / L321 2026-09-02 扩大关键词），
> 且 `## 12.`（L285）排在 `## 17.`（L249）之后。引用本文件时必须**带标题**（不能只写 §号）。

## 1. 固定来源

- HC 源码：`G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\deps\handheldcompanion-runtime\source`
- FanHost 源码：`G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\FanLab\real-host`
- 前端桥接：`G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\src\bridge`
- 真实测试日志：`YeMan-fan-logs-20260901-214949-943.zip`、`YeMan-sleep-logs-20260901-214950-601.log`
- HC 版本基线：`0.32.3.2`，批次 `HC-BINARY-BATCH12-20260831`

本轮涉及源码 SHA-256：

| 文件 | SHA-256 |
|---|---|
| `Devices/ASUS/XboxROGAlly.cs` | `78BF90520419B63C6C8FD2BC12543B2D16280895802D8F9952849C4E030FE3D4` |
| `Devices/ASUS/XboxROGAllyX.cs` | `404987B6116C5DC79FEC80AABDF97D6E28F146EC2FFC3AF69ED8FC71E40FF8EE` |
| `FanLab/real-host/Program.cs` | `A6E4C97886871F68C97DD378548394D405D3D91E8865750E1649EEF4C97685E8` |
| `src/bridge/fanHost.ts` | `E599F204BEAEC91FE9202DB44CEC9A41BC6862AA36C73312FB5E36822E75B446` |

## 2. ROG Xbox 机型路由

结论：PASS（静态证据）。

- HC `XboxROGAlly` 继承 `ROGAlly`；HC `XboxROGAllyX` 继承 `ROGAllyX`。两个 Xbox 子类只覆盖产品标识、功耗/陀螺仪参数和功耗档位，不另造风扇实现。
- `ROGAlly` 的能力包含 `FanControl`，实际风扇曲线通过 `AsusACPI.SetFanCurve` 写入 CPU/GPU/Mid 三路；关闭控制时写回 HC 默认曲线。
- FanHost 路由表明确包含 `HandheldCompanion.Devices.XboxROGAlly` 和 `HandheldCompanion.Devices.XboxROGAllyX`，测试日志实际识别为 `XboxROGAllyX`，型号为 `ROG Xbox Ally X RC73XA_RC73XA`。
- FanHost 使用 HC 的 Open/OpenEvents 和机型回调边界，没有把 Xbox 机型改成独立协议。

## 3. 生命周期三方交叉结果

| 边界 | HC 原生 | FanHost/YMCC | 裁决 |
|---|---|---|---|
| 机型识别 | `IDevice` 工厂选择 Xbox 子类 | 路由按 HC 完整类型名匹配 | PASS |
| Open → OpenEvents | `MainWindow.xaml.cs` 设备就绪后依次调用 | `Program.cs` 反射调用同一 HC 方法 | PASS（设备级） |
| 软件风扇曲线 | `PowerProfileManager_Applied` → `SetFanCurve` | 复用 HC 回调；HWiNFO 仅作温度输入例外 | PASS（行为边界） |
| 睡眠前 | `ManagerFactory.Suspend`、各 manager 暂停、`CurrentDevice.Close` | FanHost 先关闭写入/交还，再关闭设备会话 | 偏差：Fan-only 外层顺序，不得宣称完整 HC ManagerFactory 等价 |
| 唤醒后 | `ManagerFactory.Resume`，等待设备就绪，再 Open/OpenEvents | FanHost 自动重建当前 fan 会话并由守护维持 | 偏差：FanHost generation/lease/10 秒守护是 YMCC 外层 |
| 关闭/OEM 交还 | HC `CurrentDevice.Close()` 为设备释放边界；通用物理读回不是 HC 合同 | FanHost 另有 restore 证据和门禁判断 | UNKNOWN：物理 EC/OEM 最终所有权仍需目标机证据 |

## 4. 唤醒探针

HC `SystemManager.cs` 同时注册 `SystemEvents.PowerModeChanged` 和 Kernel-Power 506/507。状态层通过 `SystemPending/SystemReady` 边沿抑制重复状态切换，但 507 的唤醒原因通知可能在另一来源之后再次发布。因此“单次硬件事件”与“单次应用通知”不能直接等同。

本机日志已经证明：

- 进入睡眠后 FanHost 进入暂停/关闭写入边界。
- 唤醒后识别到 `XboxROGAllyX`，自动重新建立 Open/OpenEvents 和控制会话。
- 真实日志出现多来源电源事件与 generation 变化；不能据此断言 HC 原生不会重复通知。

FanHost 的 `powerGeneration`、lease/heartbeat、10 秒 Fan Guard、连续失败计数均属于 YMCC 外层安全机制。它们可以保留，但不能写入 HC 核心等价结论。当前“唤醒通知到重建完成之间是否存在不可接受的硬件写入窗口”仍是 UNKNOWN，需要目标机时序日志确认。

## 5. 退出、OEM 与门禁 UNKNOWN

已知真实运行曾出现以下组合：Host runtime 已记录 `restore.close-hc-success`、`api.close.success`、`Stopped`，但前端 `/api/close` 可能先收到取消；下一次启动还可能面对旧常驻 Host、旧会话令牌和 `401 API_SESSION_REQUIRED`。这说明应分别记录：

1. HC Close 返回；
2. Host listener/HTTP 停止；
3. parent watchdog 退出或交接；
4. 当前 session/lease 失效；
5. 前端收到最终停止确认。

目前不能把“回调成功”直接当作物理 OEM 读回成功，也不能把“状态 Stopped”单独当作文件句柄已释放。HC 原生并没有 FanHost 的 session token、manifest、ACL、lease 或 parent watchdog；这些是 YMCC 外层合同，必须单独验证。

## 6. 未发现的非 HC 设定与保留边界

本轮未发现 ROG Xbox 风扇路径额外调用手柄、陀螺仪、Virtual、HidHide 或完整非风扇 manager 图的证据。该结论仅限当前静态路径；若未来接入手柄/陀螺仪，必须重新审计共享设备句柄和统一 HC 版本。

唯一批准的行为例外仍为 HWiNFO 共享内存温度源。不得以本轮 ROG Xbox 通过为依据扩大其他外部温度源或修改 HC 机型类。

## 7. BUS 裁决

- ROG Xbox Ally / Ally X 的 HC 机型选择、设备级 Open/OpenEvents、风扇曲线回调和 HC Close 边界：**PASS**。
- HC 完整 ManagerFactory 应用图在独立 FanHost 中的等价性：**UNKNOWN/不适用声明**，FanHost 当前刻意不启动完整图。
- 多源唤醒通知与 YMCC generation/lease/10 秒守护的全流程等价性：**偏差已确认**，属于外层机制，不得标记为 HC 原生。
- 物理 OEM/EC 最终交还、HTTP listener 释放、旧 Host 接管和跨机门禁一致性：**UNKNOWN**，需要补充运行时证据后才能修改。

本轮不修改源码，不生成新包。任何后续修复必须以本文件与 `HC-LIFECYCLE-FULL-CROSS-AUDIT-20260901.md` 为依据，先确认偏差根因，再改动完整链路，禁止孤立修改单一事件。

## 8. BUS 三路探针回收结果

本轮三路探针均已回收，均为只读：

1. 生命周期探针：确认 Xbox 两个子类复用 HC ROG 风扇路线；确认 FanHost 的设备级 Open/OpenEvents/曲线/Close 边界基本一致；Fan-only 与完整 `ManagerFactory` 图不等价。
2. 唤醒探针：确认 HC 同时存在 Kernel-Power 506/507 与 `SystemEvents.PowerModeChanged` 两个信号源；507 可能再次发布 Resume。FanHost 的 generation、lease 和 10 秒守护是外层机制，不能当作 HC 原生等待机制。
3. 退出/门禁探针：确认 HC 的 Close 返回并不提供通用物理 OEM readback；`callback returned` 不等于物理交还完成。FaultLocked/HTTP 409 路径会保留 Host 等待恢复，不能强制假定已释放；真实 `API_SESSION_REQUIRED/401` 接管证据在本组附件中未复现，暂保留 UNKNOWN。安全自测的 no-hardware 结果不能证明真实 HC listener、EC/HID 句柄已释放。

因此，当前最重要的衍生风险不是 ROG Xbox 机型类缺失，而是：

- 多源唤醒通知与 FanHost 外层 generation/租约之间的时序窗口；
- 关闭时 HC 回调、物理 OEM 读回、HTTP listener、parent watchdog、前端确认不是同一个完成条件；
- FaultLocked 下保留常驻 Host 与后续接管门禁的组合行为；
- safe-no-hardware 自测不能替代 ROG Xbox 实机退出和文件释放证据。

本轮没有发现足以授权修改 HC 机型类或扩大非风扇 manager 的证据。后续若修复，必须针对上述完整事务链路进行，并在 ROG Xbox 目标机复测。

## 9. 关键论点：退出与恢复是否走机型库

结论：**风扇实际控制、软件曲线应用、OEM 恢复和设备 Close 均应且当前源码均通过当前 HC 机型对象执行**。对 ROG Xbox X，实际对象是 `HandheldCompanion.Devices.XboxROGAllyX`，继承 `ROGAllyX`；ROG Xbox Ally 同理继承 `ROGAlly`。FanHost 没有为 Xbox 另写一套 ACPI/EC 风扇协议。

具体证据：

- HC `ROGAlly.cs` 的 `PowerProfileManager_Applied` 将软件曲线写入 HC 的 ASUS ACPI 三路曲线，硬件模式调用 `SetFanControl(false)` 写回默认表。
- FanHost `Program.cs:2999-3015` 通过当前 HC 设备对象调用 `PowerProfileManager_Applied`；`Program.cs:2793-2830` 的 `RestoreOemCore` 走 Hardware profile，并使用当前路由的恢复合同。
- FanHost `Program.cs:3018-3118` 的 `CloseCore/CloseHcDevice` 最终反射调用当前 HC 机型对象的 `Close`；对于 ROG 路线，该调用进入 HC `ROGAlly.Close()`，包含 `AsusACPI.Close()`、控制器恢复、HID 释放和基类 Close。
- `Program.cs:1999-2024` 针对 ROG 的 `IsOpen` 前提补齐 HC 的 `Device_Inserted(true)` 边界，使机型库自身的 `SetFanControl(false)` 不因 Fan-only 没有完整 DeviceManager 而提前返回。

所以“退出/恢复走机型库”是关键正确方向，但要精确表述为：**硬件动作走机型库；退出完成不等于所有外部进程和物理读回证据已完成**。FanHost 的 session、lease、generation、HTTP listener 和 watchdog 仍是外层协调，不属于 HC 机型库。

## 10. 建议修改路线（本轮仅建议，未改代码）

1. **统一硬件动作入口**：恢复、手动关闭、睡眠前释放、进程退出全部只允许调用当前 `factoryType` 对应的 HC 设备对象；禁止任何通用 ASUS/Xbox fallback 写入。保留 `XboxROGAlly` / `XboxROGAllyX` 的 HC 继承路线不变。
2. **拆分完成条件**：日志和门禁分别记录 `hc.restore.callback.returned`、`hc.device.close.returned`、`http.listener.stopped`、`parent.watchdog.finished`、`process.exited`。不能用单一 `Stopped`、回调成功或 OEM readback 代替完整退出证据。
3. **恢复事务串行化**：以 power generation + HC session + route marker 组成唯一事务键；同一代的重复 Resume 只合并，不重复 Open/Close；新代必须等待 HC 设备 ready 后再 Open → OpenEvents → 恢复活动曲线。
4. **避免睡眠阻塞**：睡眠入口只执行 HC 所需的机型释放和事件解绑，不等待可选物理 readback；readback 只做诊断。任何超时都必须阻止新硬件写入，但不能让旧恢复任务与新 Resume 并行。
5. **退出接管顺序**：先关闭写入和租约，再完成机型库 Close，随后停止 HTTP listener，最后处理 parent watchdog/前端确认；前端取消只能标记“等待 Host 最终确认”，不能把已完成的 HC Close 改判成新一轮故障。
6. **增强证据日志**：每次硬件动作记录 `factoryType`、HC runtime/机型程序集哈希、route marker、generation、session generation 和 phase，便于确认另一台机器实际运行的确实是同一机型库。

## 11. 仍然未知的事项

- ROG Xbox 固件在每次 `AsusACPI.SetFanCurve` / `SetFanControl(false)` 返回后，物理 EC 是否立即完成切换：**UNKNOWN**。
- HC `ROGAlly.Close()` 返回后所有 HID/ACPI 句柄是否已释放，以及是否存在驱动异步释放延迟：**UNKNOWN**。
- Kernel-Power 与 `SystemEvents` 在目标机的实际到达顺序、重复 Resume 是否会造成真实写入窗口：**UNKNOWN**。
- FanHost 外层 10 秒守护与 HC 的一次性 ready 等待预算叠加后是否造成额外延迟：**UNKNOWN**。
- 完整 HC `ManagerFactory` 图是否必须在未来手柄/陀螺仪融合时共用同一进程：**UNKNOWN**；当前 FanHost 明确不启动完整非风扇 manager。
- 另一台机器上的安装包是否包含与主线完全相同的 HC 机型库、依赖哈希和门禁清单：**UNKNOWN**，需用包内清单和 SHA-256 核对。

## 13. BUS 两轮探针、已知修复与可重复测试包（2026-09-01）

本轮按 BUS 两阶段流程执行：第一轮分别检查 HC/ROG Xbox 生命周期、退出/门禁和唤醒/代际；第二轮扩大检索隐藏电源入口、恢复所有者、租约/会话、manifest/旧路径和打包重建来源。探针均为只读，没有启动真实 HC、没有访问硬件、没有修改 HC 机型类。

### 已修复

1. `Program.cs` 的 Close 后 ManagerFactory 证据改为重新只读采样，不再复用 `OpenEvents` 时的旧快照；不调用非风扇 manager 的 Start/Stop。
2. `src/App.vue` 的电源挂起通知不再额外调用 `fanHostLifecycle.suspend()`。App 只记录 native generation、冻结 UI 写入；实际 HC `CurrentDevice.Close()` 仍由常驻 FanHost/原生电源入口串行执行，避免 WebView、native、Host 三方重复拥有物理挂起权。
3. FanHost bridge 的 suspend/resume 请求携带 `{ generation, source }`，守护恢复使用同一代际身份；HC 风扇动作、ROG Xbox 机型库和 HWiNFO 温度例外均未改变。
4. native FanHost 紧急挂起 worker 保留更新的 generation。旧 worker 在执行期间收到新电源边界时，不再直接丢弃新代，而是在同一非阻塞调度边界继续处理。
5. FanHost payload 重建器新增显式冻结 runtime 输入参数。当前树缺失的默认路径不再被静默当成可重建；本轮使用已审计迁移备份中的 `batch11-runtime-09` 作为明确输入。

### 复测结果

- FanHost Release build：PASS，0 warning / 0 error。
- FanHost lifecycle、睡眠极端场景、hardware coordinator：PASS。
- HC lifecycle deep：PASS；T4 HC parity：20 checks PASS，既有 3 个架构边界仍明确标记 `needs-investigation`。
- FanHost payload rebuild：PASS，102 个 manifest 文件；HC device closure：PASS（runtimeFiles=93、deviceTypes=105、hardwareWrites=false）。
- 完整 standalone test ZIP：
  `Build/TestPackages/FanCoordinator-20260901-230041/YeManCC-FanCoordinator-Test.zip`
  - size：38,081,705 bytes
  - SHA-256：`4A531E866213746B02B9E045E905DC2218E589C0F42B1170BFD2588E37EB1021`
  - payload manifest SHA-256：`556045B6AD40A9918C39342D7A00090551F2DC0DF3A2F846791BDB65AD7DE3ED`

### 仍需实机确认

本轮不能把静态/安全自测当作 ROG Xbox 实际睡眠和退出的证明。以下仍是明确的 `UNKNOWN`：HC `Close()` 在目标机的实际返回时序、ACPI/HID 句柄最终释放、physical OEM readback、Kernel-Power 与 SystemEvents 的真实到达顺序，以及独立 FanHost 不启动完整 HC ManagerFactory 图时的手柄融合影响。本机额外机制暂时保留，后续再单独裁决。

## 14. 23:09 唤醒前恢复假象/真实顺序复核（2026-09-01）

证据来源：用户提供的 `YeMan-sleep-logs-20260901-231019-339.log` 与
`YeMan-fan-logs-20260901-231018-628.zip`。用户观察为“唤醒前一秒已恢复”，本报告先按假设记录，不将观察直接认定为硬件已在唤醒前写入。

### 当前日志能确认的顺序

- `23:09:03.632`：native 记录 `power-broadcast`，生命周期仍为 ready。
- `23:09:04.591`：Kernel-Power 506，随后接受 S0/S3 intent，generation=3。
- `23:09:29.820`：native 记录 power broadcast code 18；同一时刻 FanHost bridge 记录旧租约失效。
- `23:09:29`：10 秒守护完成一次 rebuild-success。
- `23:09:30.821`：Kernel-Power 507，才记录 `s0-wake-classified(userWake=true)`。

因此日志时间确实呈现“租约失效/守护重建早于 507 约 1 秒”。但当前 `yeman-fan-host-runtime.log` 为 0 字节，缺少 Host 侧 `SystemEvents`、HC `Open/OpenEvents` 和实际机型库动作证据，不能证明这次 rebuild 已经在物理唤醒前执行了 HC 风扇写入。它可能是：

1. 睡眠期间租约按时失效，守护先做状态重建；
2. Kernel-Power 507 到达晚于系统实际恢复；
3. fan-api 与 sleep/native 日志使用不同异步写入队列，显示顺序与硬件动作顺序不一致。

### 本次日志量与错误量

当前 ZIP 只有 7,070 bytes 是压缩后大小；其中 `fan-api.log` 原始大小 168,466 bytes、140 行，上一份 21:49 日志的 `fan-api.log` 为 94,736 bytes、114 行。真正减少的是 Host runtime 日志：本次 0 bytes，上一份约 693,013 bytes。因此“日志少”主要是 Host 侧采集缺失，不等于错误全部消失。

外层失败事件由上一份的 9 个降为本次 6 个；旧 Host/会话接管失败消失，但仍有 `LEASE_INVALID`、`/api/close` cancelled 和 IPC worker queue stopping。另有 `lifecycle.fan-guard-rebuild-success` 出现在 507 前，这是新的 P1 UNKNOWN。

### 当前最高概率根因（待探针裁决）

除机型库路线外，最高概率是**租约过期恢复与真实唤醒没有共用同一个 power generation / wake-ready 门**：守护看到远端租约失效后即可进入 rebuild，而不是必须等待 `userWake=true` 或 HC `SystemReady`。这属于 YMCC 外层时序，不等同于 `XboxROGAllyX` 机型类错误。

### 补充证据：23:09:29 已确实执行曲线写入

进一步逐项解码 fan-api 记录后，`23:09:29` 并非只有前端状态变更：守护在同一秒依次完成 `restore`、`release-control`、`acquire-control`，并成功返回 `/api/enable`（31 ms，100% 曲线，`hardwareWritesEnabled=true`）。因此这次存在实际重新写入，不是纯日志 flush 假象。

不过 sleep 日志在 `23:09:29.820` 已先记录 `power-broadcast code=18`，随后同一毫秒记录 code=7；Windows 语义中 code 18 是 `PBT_APMRESUMEAUTOMATIC`（自动恢复），code 7 是 `PBT_APMRESUMESUSPEND`（用户/交互恢复）。Kernel-Power 507 出现在 `23:09:30.821`，应被视为晚到约一秒的事件日志确认，而不能单独作为“机器尚未恢复”的时间锚点。

修正后的结论是：**不能证明曲线写入发生于物理唤醒前；已证明它发生在自动恢复通知之后、507 用户唤醒确认之前。** 这仍暴露一个外层设计缺口：Fan Guard 以 lease expiry 触发写入，未明确要求同 generation 的 Host `SystemReady/Open/OpenEvents` 完成证据。机型库本身是否在 code 18 前被调用，继续由 ROG Xbox 专项探针裁决。

### 待完成探针

- ROG Xbox 机型库探针：确认 `PowerProfileManager_Applied`、`SetFanControl(false)`、`Close/Open/OpenEvents` 是否可能在 507 前被调用。
- 时间源探针：对齐 sleep/native、fan-api、Host runtime 三个日志的写入时钟和 flush 行为。
- 隐藏入口探针：扩大搜索 lease expiry、fan guard、resume-ready、SystemEvents 和 native fallback 的代际门，确认是否存在未等待真实唤醒的恢复入口。

在 Host runtime 日志重新采集前，本条保持 `UNKNOWN`，不据此修改 HC 机型类；本机额外机制继续保留。

## 15. 第二轮完整 UNKNOWN 清点：机型库与外层恢复入口（2026-09-01）

本轮三路 BUS 探针均为只读，未修改源码、未启动 FanHost、未加载真实 HC、未访问硬件。探针范围分别覆盖：ROG Xbox 机型工厂/HC 调用链；附件日志与 native/Host 时间线；扩大关键词和隐藏入口审计。

### 已排除：ROG Xbox 机型库不是租约/守护发起者

`RC73YA → XboxROGAlly`、`RC73XA → XboxROGAllyX`，两个 Xbox 类只继承 ROG 风扇路线并覆盖机型参数。HC 类不包含 lease、heartbeat、Fan Guard、generation 或 rebuild 调度；这些均由 FanHost/bridge 外层发起。HC 的 Hardware profile 回调最终调用 `SetFanControl(false)`，软件 profile 回调执行曲线写入；`Close()` 再执行 ACPI/HID/base Close。故机型库是实际硬件动作执行层，但不是本次 `LEASE_INVALID` 或提前恢复的根因。

### P0：两个独立恢复所有者绕过统一 wake-ready

1. FanHost 常驻进程自己的 `SystemEvents.PowerModeChanged(Resume)` 仅凭 Resume 事件即可进入 `ResumeForSystemPower()`，随后执行 `Open → OpenEvents → lease → Enable`。它没有等待 native 的 Kernel-Power 507、`userWake=true` 或显式 `SystemReady(generation)` 证明。HC 原版也存在多源事件，但其恢复顺序由统一 HC 应用生命周期编排；FanHost 的独立事件订阅是外层偏移。
2. 前端 10 秒 Fan Guard 在远端仍为 `Suspended` 时可主动调用 `/api/resume`，随后重建并写入曲线；当前入口不检查 native 电源生命周期、同代 wake-ready 或用户唤醒证据。即使 WebView 电源通知尚未确认，常驻 Guard 仍可能动作。

这两条与本次日志吻合：`23:09:29` 已完成 restore/release/acquire/enable，`23:09:29.820` 已收到 code 18 自动恢复通知，`23:09:30.821` 才收到 507。当前不能把它称为“机型库提前写入”，应称为“外层恢复准入过早，实际 HC 写入发生在自动恢复通知后、507 确认前”。

### P1/P2 其他未知与衍生风险

- 无前置 query 的裸 `PBT_APMSUSPEND` 在策略为 Unknown 时仍被强制按 S3 处理，可能误暂停 FanHost/游戏链路。
- native `power.resumeComplete` 后先把生命周期置 Ready、打开硬件写门，TDP daemon 等后续恢复失败不会回滚 Ready；“可写”不等于全部恢复依赖就绪。
- 23:09:29 的 Guard 具体走“仅重取租约+Enable”还是“Resume→Open→OpenEvents→Enable”，因 Host runtime 日志为空仍未证实。
- `SetFanControl(false)` 返回到 EC 物理完成、HC Close 返回到 ACPI/HID 句柄释放，仍需目标机证据。
- 当前 `Program.cs` 与 `FANHOST-SOURCE-BASELINE.json` 的源哈希不一致；payload 自洽，但尚未建立二进制与当前源码的一致性证明。
- Host runtime 日志默认/本次均可能为空，缺少 power-ingress 入队、HC 调用开始/返回、generation、WakeReason 和拒绝原因；这是证据链 P2 缺口。

### 当前最高概率排序

1. P0：Fan Guard 与 FanHost `SystemEvents.Resume` 双恢复所有者没有统一 wake-ready/generation 门。
2. P1：租约 15 秒失效、5 秒 heartbeat、10 秒 Guard 与自动恢复通知并行，造成 restore 后立即重取租约和重写曲线。
3. P1：native/Host 的多源电源事件在代际和唤醒原因上没有贯通，导致同一次恢复被不同准入标准分别确认。
4. P1：目标包与当前源码基线可追溯性断裂，存在测试机运行旧成品的风险。
5. P2：Host runtime 为空使 HC 机型库实际逐调用顺序暂时无法从现场日志闭合。

本节只完成 UNKNOWN 清点和证据归档。下一步若授权修复，应优先收敛两个 P0 恢复所有者，保留 ROG Xbox HC 机型类不变；HWiNFO 仍是唯一批准行为例外，本机额外机制暂不裁决。

## 16. 扩大关键词审计第二轮归档（2026-09-01）

三路探针对 `suspend/resume/wake/lease/heartbeat/guard/Open/OpenEvents/Close/UNKNOWN/fault/pending` 及 native/Host/frontend/HC 工厂入口完成交叉检索。未发现新的 ROG Xbox 机型类内部调度入口；新增或确认的外层风险如下：

- P0：FanHost `SystemEvents.Resume` 和前端十秒 Guard 都可以单独推进恢复，未要求统一的同代 `wake-ready` 证明。
- P1：没有前置 query 的裸 suspend 在策略未知时默认按 S3，可能将不明确的广播误当成需要暂停风扇会话。
- P1：native resume complete 后先开放 Ready/硬件写门，后续 daemon 恢复失败不会同步回滚该门。
- P1：FanHost 当前源码哈希与 `FANHOST-SOURCE-BASELINE.json` 不一致；payload 自身清单自洽，但尚未建立当前源码到二进制的绑定证明。
- P2：Host runtime 日志为空，缺少 SystemEvents 入队、native 507 入队、HC Open/OpenEvents 和实际 profile callback 的共同单调时钟。

本轮明确分层：前四项属于可继续通过源码修改和安全自测收敛的 UNKNOWN；EC 物理完成时刻、ACPI/HID 最终释放、目标机真实事件顺序和完整 HC ManagerFactory 等价性，必须通过目标机实测或非空 Host runtime 证据消除，不能靠关键词搜索宣称清零。所有本机额外机制暂保留，HWiNFO 仍是唯一批准行为例外。

## 17. BUS 修复轮：唤醒准入、恢复所有者与构建根闭合（2026-09-02）

本轮三路独立探针先确认根因，再执行修复。结论仍是 YMCC 外层恢复准入偏移，未发现 ROG Xbox HC 机型工厂类需要修改。

### 已修复

1. `FanCoordinatorGate` 增加同代 `wakeReadyGeneration`。`resuming` 只代表恢复意图，不能建立 FanHost 会话或通过 HC-ready；native 提交同代 `resumed` 后才打开该门，睡眠/关闭会清除它。
2. 前端十秒 Fan Guard 保持常驻，但远端仍为 `Suspended` 时只记录 `FAN_GUARD_RESUME_PENDING` 并等待 Host 的 HC `SystemEvents.Resume` 恢复，不再由 Guard 调用 `/api/resume`，消除第二恢复所有者。
3. FanHost `SystemEvents.Resume` 保留为唯一 Host 恢复入口，继续执行 `Open -> OpenEvents -> lease -> Enable`；睡眠代际后的写入必须重新 handshake。
4. 租约过期在恢复重建期间延后，禁止恢复 worker 完成后出现 `Ready && lease=null`。
5. native `power.resumeComplete` 在同代 native/input readiness 达成后提交；可选 daemon 恢复状态只记录，不得阻塞提交。resume watchdog 只记录 degraded 并重试，不能在前端提交缺失时 fail-open 打开硬件写门。
6. 未分类的裸 `PBT_APMSUSPEND` 只推进生命周期观察，不伪造 S3 游戏冻结事务。
7. 完整关闭后协调器进入可重新启动的 stopped 边界，避免正常重启被旧 stopping 状态拒绝。
8. FanHost 重建器/测试打包器分别固定 checkout 源码根与 sibling Build 根，避免再次从错误 `Mainline` 容器或旧生成物组包。

### 复测结果

- TypeScript type-check：PASS。
- hardware coordinator、FanHost lifecycle、sleep extreme/conflict、exit
  cleanup、sleep priority：PASS。
- HC lifecycle deep、lifecycle order、enable alignment、T4 parity：PASS；
  仅保留已有 `needs-investigation`（物理 OEM 回读、完整 ManagerFactory
  图、不可用参考资产），未出现新增 P0/P1 偏移。
- FanHost Release 编译：0 warning / 0 error。
- payload 重建：102 文件，manifest/hash/ACL 自洽，PASS。

导出的完整测试包：
`G:/YeManCC-Work/Mainline/Build/TestPackages/FanCoordinator-20260902-000224/YeManCC-FanCoordinator-Test.zip`

SHA-256：
`B46E7074682C5543D88A675B5FF0B039797120CC96989A1BE75BA167A62B9AFD`

这轮仍未修改 HC 机型适配库或风扇 callback；唯一批准行为例外仍是
HWiNFO 共享内存温度源。实机 EC/OEM 物理完成、完整 HC ManagerFactory
等价和目标机实际事件顺序，必须由用户在目标机测试中确认，不能由安全自测冒充。

## 12. 2026-09-01 第一轮修复与复测

### 已修复：Close 后复用 OpenEvents 旧快照

根因：FanHost 的 `ConfirmManagerCleanupAfterClose` 原先只使用
`OpenEvents` 后的 ManagerFactory 状态快照。这不符合 HC `Window_Closed`
的“先 `CurrentDevice.Close()`，再处理关闭边界”语义：旧快照为 active 时
可能使有效 Close 误入 pending；旧快照为非 active 时又可能把未释放的资源
误标为完成。

修复：`Program.cs` 在 `CurrentDevice.Close()` 返回并解绑 FanHost profile
事件后，重新以**只读**方式采样 ManagerFactory 状态，再作关闭确认。修复不
调用任何 manager 的 Start/Stop，不改变 `XboxROGAlly` / `XboxROGAllyX` 的
机型库调用，也不扩大 HWiNFO 例外。

该修复的边界：FanHost 是 fan-only owner，不拥有完整 HC ManagerFactory
应用图。重新采样只用于防止旧证据误判；它不是授权 FanHost 接管、停止或重建
Controller/Sensors/Virtual 等非风扇 manager。

### 本地验证

- `dotnet build FanLab/real-host/YeManFanHost.csproj -c Release --no-restore`：PASS，0 warning / 0 error。
- FanHost lifecycle self-test：PASS。
- exit cleanup self-test：PASS。
- parent watchdog self-test：PASS。
- HC lifecycle deep self-test：PASS。
- T4 HC parity audit：20 checks PASS，3 个既有架构边界继续标记 needs-investigation。

### 仍未解决、不得伪报为 PASS

- 实机 `CurrentDevice.Close()` 可能卡在 HC/HID/ACPI 内部，15 秒后只能进入
  `HC_CLOSE_PENDING` 并保留 Host。HC 没有通用取消 API，不能强杀 STA 或重入
  第二个 Close。
- `Stopped` engine 到 listener/进程/文件句柄真实释放仍是独立阶段，必须记录
  listener 停止、watchdog 结束和进程退出，不能仅凭状态字符串确认。

## 13. 2026-09-02 扩大关键词与 HC 日志门审计

本轮检索范围：HC `SystemManager`、`ManagerFactory`、`DeviceManager`、
`MainWindow`、ROG/Xbox 机型类，以及 `suspend/resume/wake/507/Open/Close/
lease/guard/session` 关键词。全程只读，无硬件调用。

### 源码证实

- HC 的 `SystemEvents.Resume` 已直接推进 `SystemReady`；507 的职责是补发
  WakeReason，不能把 507 当作 HC Open 的前置门。
- `SystemReady` 之后，HC 最多等待十秒 `CurrentDevice.IsReady()`，超时只记
  critical，仍继续 `Open/OpenEvents`。FanHost 的 `WaitForHcDeviceReadyBeforeOpen`
  已采用相同规则。
- ROG Xbox 类仅继承 ROG 风扇实现。上游 `Close()` 没有通用物理 OEM 读回
  合同；FanHost 的 Hardware profile restore 是在同一串行 HC 会话内、在
  virtual Close 前执行的外层安全补偿，不能写成“HC 已确认物理交还”。

### 本轮 P0/P1

- P0：无源码或日志可证实 P0。
- P1（已修复，观测链）：native 写 `fan-logging-enabled.flag=enabled`，而
  Host 仅识别 `1`，导致 Host runtime 日志可被静默关闭。Host 现兼容 `enabled`
  和旧值 `1`。
- P1（已修复，导出链）：风扇日志导出/清空遗漏
  `%LOCALAPPDATA%\\YeManCC\\native-lifecycle.log`，导致同一 ZIP 没有 native
  power generation、写门与退出证据。现已纳入风扇日志集合。

### 仍为 UNKNOWN

- 目标机日志为空的 Host runtime 不能证明 Host 是否收到 SystemEvents、HC
  Close 是否返回，或最终 EC/OEM 所有权；heartbeat 成功只证明 lease 通道可达。
- 20260902-001240 的匿名 shutdown/401 属于旧包启动失败清理路径。当前源码已
  限制为只有本次启动取得 child handle 后才能 shutdown，仍需用新包实测。

## 复核补正（2026-09-17，审计系列 §28.46，未改码）

**逐位命中（本次第一手复核）**
- §1 两枚 HC 机型文件哈希**今日仍逐位一致**（deps 冻结面）：
  `XboxROGAlly.cs` = `78BF9052…FE3D4` ✓、`XboxROGAllyX.cs` = `404987B6…FF8EE` ✓。
- §13 测试包**仍存于 Backup 树且哈希逐位一致**：`FanCoordinator-20260901-230041\YeManCC-FanCoordinator-Test.zip`
  = **38,081,705 bytes** / SHA-256 **`4A531E86…1021`** ✓（全树仅存此一枚 09-01 包）。
- `runtimeFiles=93`（§13）为 09-01 23:00 口径，现与 90/91/94/96/101/122 同族（引用必须带时点）。

**遗留项升级记录（命中 Y 的源头）**
- §15 P1-4 / §16 P1-4 早已登记"当前 `Program.cs` 与 `FANHOST-SOURCE-BASELINE.json` 的源哈希不一致…
  存在测试机运行旧成品的风险"——**至 2026-09-17 仍未闭合**：门禁
  `verify-fanhost-source-baseline.ps1` 现仍失败（详见本系列 §28.44 命中 V）。
  ⇒ 该项不是新问题，而是**跨越 16 天的长期断链**，须与 §28.44 合并处置（修复待用户裁决）。

**已被后续取代（引用前必读）**
- §17"已修复"第 2、3 条（Guard 不再调 `/api/resume`；FanHost `SystemEvents.Resume` 为唯一 Host 入口）
  ——09-04 起再次被取代：Guard 整链已删、Host 订阅已移除、`/api/resume` 改由渲染器发送（见 §28.43）。
- §1 中 `Program.cs`（`A6E4C978…`）与 `fanHost.ts`（`E599F204…`）为 09-01 时点值（后续均漂移）。

**不可复核**：§14 引用的 `YeMan-sleep-logs-20260901-231019-339.log` / `…231018-628.zip`
（7,070 bytes 等）与桌面目录均已不在，23:09 时间线无法第一手复核（判定保留 UNKNOWN）。
