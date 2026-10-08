# HC 生命周期、唤醒与门禁全流程交叉审核

日期：2026-09-01  
范围：只读审核；未启动 FanHost，未加载 HC，未访问硬件，未执行硬件写入。  
唯一源码根：`G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC`  

## 1. 固定比较基线

- HC 源码：`deps/handheldcompanion-runtime/source`
- HC 基线：`HC-BINARY-BATCH12-20260831`
- HC 源文件：1048 个；版本 `0.32.3.2`
- FanHost 源基线：`FANHOST-REAL-HOST-20260831`
- FanHost 源文件：22 个
- 唯一允许的行为例外：HWiNFO 共享内存温度源

本报告不把 C 盘迁移副本、R4/V5 历史副本、旧构建目录或未列入当前主线的文件当作比较输入。

## 2. 三方交叉结论

| 流程 | HC 原版证据 | 当前 YMCC/FanHost | 判定 |
|---|---|---|---|
| 设备打开 | `Open` 后 `OpenEvents` | FanHost 调用 HC 设备 `Open`，再调用 `OpenEvents` | 设备级 PASS |
| 风扇写入 | HC 设备回调执行风扇策略 | FanHost 仍通过 HC `PowerProfileManager_Applied`/设备回调；HWiNFO 仅供温度 | PASS，温度为批准例外 |
| 睡眠 | HC `ManagerFactory.Suspend`，随后平台/传感器/设备按 HC 顺序收敛 | FanHost 仅执行风扇恢复/关闭边界，并由原生电源代次通知；不启动完整 ManagerFactory 图 | 已知边界，不得声称全图一致 |
| 唤醒 | HC 恢复 manager/platform，再等待设备 ready，之后 `Open -> OpenEvents` | FanHost 以现有原生代次进入恢复，执行 `Open -> OpenEvents -> lease -> Enable` | 设备级相容；完整图 UNKNOWN |
| 10 秒 | HC 的 10 秒是设备就绪等待预算 | YMCC 的 10 秒是常驻风扇守护周期；FanHost 另有 HC 就绪/插入等待 | 不是同一机制，禁止混称 |
| 租约 | HC 没有通用 Fan API lease | YMCC 有 lease、heartbeat、代次与冲突恢复 | YMCC 安全扩展，非 HC 原生机制 |
| AC/DC | HC 通过当前 Profile 的 `PowerProfiles[PowerLineStatus]` | FanHost 保留 HC profile 回调，但隔离完整 ProfileManager 图并使用 fan-only 观察/模板 | 完整自定义 Profile 映射 UNKNOWN |
| 退出 | HC `Window_Closed -> CurrentDevice.Close -> 解绑 -> manager Stop` | FanHost 负责真实设备 Close 与事件解绑，观察 manager 状态，不代替完整 manager Stop | 设备 Close PASS；完整退出图 UNKNOWN |
| OEM 交还 | HC 设备/厂商 Close 回调 | FanHost 使用 HC Close/restore，并额外记录物理确认 | HC 回调可证；物理所有权需实机证据 |

## 3. 唤醒专项审查

### 已确认

1. 根电源事件把同一个 native `powerGeneration` 转交给 FanHost 协调器观察层。
2. 协调器不会注册第二个 suspend/resume 监听，不启动第二个恢复循环，也不调用 HC。
3. `resumed` 事件不会由前端直接抢先调用 FanHost `resume()`；恢复仍由现有 FanHost 常驻守护/Host 生命周期负责。
4. FanHost 的新写入必须同时满足当前 Host session 与当前 power generation，旧唤醒代次会被拒绝。
5. 睡眠/关闭边界会关闭写入准入，晚到的 Applied、温度和 HID 回调不能进入新会话。

### 不存在于 HC、但存在于 YMCC 的设定

以下不是 HC 原版设定，不能用“HC 完全一样”描述：

- `powerGeneration`
- Fan API lease / heartbeat / lease generation
- 常驻 10 秒 Fan Guard
- 连续失败计数与 `fault-locked`
- Fan-only coordinator admission gate
- payload manifest、ACL、session token 和原生退出 watchdog

这些是 YMCC 外层安全与进程协调机制。它们可以保留，但必须单独验证，不能回写成 HC 核心机制。

### 当前未知项

- 未在真实目标机证明 Windows 唤醒通知、Host 自动恢复与硬件可写之间不存在时间窗口。
- 未证明所有设备的物理 EC/OEM 所有权读回都能在 HC void Close 返回后立即确认。
- 未证明完整 HC `ManagerFactory`、Platform、Sensors、Virtual/Controller 图在独立 FanHost 中可安全复制；当前策略是刻意不复制。
- 未证明外部 ProfileManager 的自定义 AC/DC 映射在 fan-only 隔离模式下与 HC 完全等价。

## 4. 退出与门禁专项审查

### 门禁结论

当前 fan payload 的运行时依赖权威是 `PowerControl/fan-host/YeManFanHost.payload.json`。前端不再维护重复 DLL 白名单；FanHost、payload 自测与独立测试打包器都读取同一份 payload 清单。升级器的正式策略仍为 preserve-existing，未被本次测试包改写。

已检查的边界：

- manifest 文件缺失、路径越界、文件缺失、哈希不符：失败关闭；
- payload ACL 与私有 session capability：单独检查；
- 启动前先识别并恢复当前精确 FanHost，再进行 payload 操作；
- native 退出先走 `parent-exit`，只在 404/405 时使用旧 close fallback；
- ZIP 根目录与 FanHost 102 项 manifest 文件逐条复核。

### 门禁未知项

- 真实另一台电脑的安装器/杀毒软件是否改变 ACL，静态证据无法证明。
- 测试人员实际运行的包是否与记录 ZIP 相同，必须以包内 `fan-coordinator-test-manifest.json` 和 ZIP SHA-256 核对。
- 完整正式发布器尚未纳入本次 fan-only 测试包；因此本报告不宣称升级器已完成 FanHost 发布接入。

## 5. 探针与证据

- HC 源基线：通过，1048 文件，`0.32.3.2`
- FanHost 源基线：通过，22 文件
- FanHost 生命周期自测：通过，含 gate、lease、restore、suspend/resume、close
- coordinator contract self-test：通过
- sleep extreme self-test：通过，18 个模型场景，硬件写入关闭
- sleep/apply conflict self-test：通过，14 个场景，硬件写入关闭
- exit cleanup self-test：通过
- Host parent watchdog self-test：通过，使用当前主线 payload 的安全无硬件模式
- payload self-test：通过
- HC 路由能力审核：70 个 HC 风扇工厂类与 FanHost 路由完整覆盖
- T4 生命周期审核：20 项通过，3 项明确 `needs-investigation` 架构边界
- T3 Profile/Events 审核：17 项通过，0 项失败，6 项明确 `needs-investigation` 边界

独立测试包：

`G:\YeManCC-Work\Mainline\Build\TestPackages\FanCoordinator-20260901-203802\YeManCC-FanCoordinator-Test.zip`

- 大小：38,081,044 bytes
- SHA-256：`337A8FC2103A7D0BB5FF753BCC4D8B1AC8E1568F6AB4C529E16519E3E32A9023`
- FanHost manifest：102 个文件
- ZIP 内 FanHost 文件哈希二次复核：通过

## 6. 审核裁决

当前可以确认：FanHost 的设备级 Open/Events/风扇回调/Close 边界遵循受控 HC 逻辑；协调器没有扩大 HWiNFO 以外的行为例外，也没有接管 HC 所有权。

当前不能确认：独立 FanHost 与完整 HC 应用级 ManagerFactory 生命周期、所有唤醒时序、所有设备物理 OEM 交还、以及外部自定义 ProfileManager 映射完全等价。

因此本轮结论为：**风扇接入结构可继续实机验证；“与 HC 全流程完全一致”暂不成立，所有未知项必须保留为 UNKNOWN/needs-investigation，不得删除或改写成 PASS。**

## 2026-09-02 Modern Standby 复核与修复记录

针对 `YeMan-sleep-logs-20260902-070738-222.log` 与对应 FanHost 日志的交叉审计：

- 07:05:52 已接受 S0 睡眠意图，根协调器进入 `suspending`；但此前根事件处理只调用 `observePowerTransition`，没有把已确认睡眠边界排队到 FanHost。
- FanHost 未在睡眠边界执行 HC `CurrentDevice.Close()`，直到 07:06:56 `LEASE_INVALID` 后才执行一次 `restore`/`release`。这解释了睡眠期间仍持有 HC 会话和入睡可能变慢的问题。
- 07:06:57 唤醒，07:07:06 完成 `handshake -> open -> open-events -> acquire -> enable`，恢复成功；退出阶段最终出现 `hc-close.success`、`api.close.success` 与 `host.stopped`。
- 睡眠日志中的连续 506/507 记录是 Modern Standby/EventLog 多源重复观测；协调器已按活动事务去重，不能把它们误判为多次真实睡眠。

本轮修复：`src/App.vue` 对 `queryBoundary !== true` 的已确认睡眠边界排队一次 `fanHostLifecycle.suspend()`；可取消的 `PBT_APMQUERYSUSPEND` 不提前关闭。FanHost 仍是唯一 HC Close 所有者，重复 native 边界由 Host/队列幂等抑制。

复核结果：`type-check`、FanHost lifecycle、sleep extreme、sleep/apply conflict、T0 sleep-hang、hardware coordinator 全部通过。该修复尚未替代另一台设备上的实机睡眠测试；实机仍需确认入睡延迟、唤醒恢复和退出文件释放。

## 复核补正（2026-09-16，审计系列 §28.44，未改码）

**仍成立（复核通过）**
- §1 的 `1048` 文件 / `0.32.3.2` 锚点仍准确；`deps` 与 Batch12 冻结面/参照库快照的**全树逐位同一性**已在 §28.42 完成
  （`1,242 = 1,047 + 195(obj)`，`deps = 1,047 + 1(manifest) = 1,048`）。
- §5 数字复核成立：HC 路由 **70/70**（§28.32 实跑）、T4 **20/0/3**、T3 **17/0/6**
  （存量 `t3-audit-result.json`：`pass=17 fail=0 needsInvestigation=6 p0=0 p1=6`，capturedAtUtc 2026-09-10）。
- "10 秒不是同一机制""租约属 YMCC 扩展""完整 ManagerFactory 图 UNKNOWN"等判定与现行记录一致。

**已被后续推翻/演化（引用前必读）**
1. **§5"FanHost 源基线：通过，22 文件"现行不成立**：`tools\verify-fanhost-source-baseline.ps1`
   本轮只读实跑 **exit=1**（`file count mismatch: 23`）——源目录多出 `Program.cs.bak-20260915-fanmock`
   （`ECD5F23C…`，门禁排除项不含 `.bak`），且 `Program.cs` 已漂移（磁盘 `60331ECD…` ≠ 记录 `BC479F2D…`）。
   基线 `baselineId` 亦已演化为 `FANHOST-REAL-HOST-20260902-HC-BOUNDARY`（`sourceFileCount` 仍写 22）。
   ⇒ 引用本行的"通过"必须带 09-01 时点；现行 R0 可追溯性为**红**（契约 §12.1 已同步更正）。
2. **§2026-09-02 节的 App.vue 修复已被取代**：该节称"`src/App.vue` … 排队一次
   `fanHostLifecycle.suspend()`"——现行 `App.vue` 中 `fanHostLifecycle.suspend` **0 命中**；
   F4 发送者已是 **native-only**（`source="native.power"`，Host 侧 `F4_NATIVE_AUTHORITY_REQUIRED`）。
3. **§3"已确认 3"已被取代**："`resumed` 不会由前端直接抢先调用 FanHost `resume()`；恢复仍由常驻守护/Host 生命周期负责"
   ——现行 `/api/resume` 的生产发送者**就是渲染器**（`fanApi.ts` L231/L329、`App.vue` L972），
   Host 已无 `SystemEvents` 订阅，十秒守护整链已删（见 §28.40/§28.43）。
4. **§4"FanHost 102 项 manifest"属 V6 期结构**：现行 payload = schema2 / **8 项指针清单**
   （`4BBAA1BB…AF9FA`，与 `verify-r5v9-fan-host-payload.ps1` pin 一致）。
5. **§5 测试包哈希不可复核**：`FanCoordinator-20260901-203802`（`337A8FC2…`）位于
   `Mainline\Build\TestPackages`——该 root **整体已不存在**（全树仅 Backup 残 09-01 一包）。
