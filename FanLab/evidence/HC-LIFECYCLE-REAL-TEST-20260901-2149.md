# HC 生命周期实机测试证据：风扇 + 睡眠

日期：2026-09-01  
测试包：`YeManCC-FanCoordinator-Test.zip`  
日志：`YeMan-sleep-logs-20260901-214950-601.log`、`YeMan-fan-logs-20260901-214949-943.zip`  
测试设备：`HandheldCompanion.Devices.XboxROGAllyX` / `ROG Xbox Ally X RC73XA`

## 结论

本次证明了“开启风扇 → 睡眠 → 唤醒后 Host 自动重建并回到 Ready”在该目标机上发生；不能证明全流程通过，因为退出前端与旧 Host 接管仍有明确异常。

## 已观察到的正确链路

- `21:48:52` handshake 成功，识别 `XboxROGAllyX`。
- `21:48:54` `Open`、`OpenEvents` 和首次风扇曲线写入成功。
- `21:49:00` 手动关闭/恢复成功，`21:49:02` 再次开启成功。
- `21:49:12` FanHost 开始 HC restore/Close；`21:49:13` 目标为 `Suspended`。
- `21:49:28` `power.resume-auto-rebuild` 开始并成功，重新出现 HC device open 与 profile event subscription。
- `21:49:38` 前端 Fan Guard 观察到 `Ready`，恢复曲线仍存在。

因此唤醒后的恢复不是假成功：日志有新的 HC open、事件订阅、恢复完成和 Ready 状态证据。

## 仍存在的问题

### 1. 退出请求的前端观测与 Host 实际结果不一致

前端在 `21:49:41` 记录 `/api/close` transport failure，随后 IPC worker queue stopping；但 Host 同时记录：

- `restore.close-hc-success`；
- `api.close.success`，状态 `Stopped`；
- `hcVirtualCloseReturned=true`；
- `hcCloseCleanupPending=false`；
- `operationTimedOut=false`。

判定：本次真实 HC Close 实际完成，但 YMCC 前端在 native IPC 关闭窗口内先收到取消，把成功边界记成失败。这是退出事件观测问题，不是风扇物理交还失败。

### 2. 重启接管失败

`21:49:45` 新生命周期两次对旧 Host 发送 `/api/shutdown`，均返回 `401 API_SESSION_REQUIRED`；前端进入 `fault-locked`。旧 Host 此时已经是 `Stopped`，但 `parent-watchdog.handoff` 仍报告 `hostWillRemainResident=true`。

判定：旧 FanHost 的硬件会话已关闭，但进程/监听器仍处于 parent-exit 常驻交接边界。新 YMCC 没有可接管的旧 session capability，无法完成第二次启动。这是当前实机确认的 P1 链路问题，不应归类为 HC 风扇回调失败。

### 3. 睡眠信号存在多源时序交错

睡眠日志显示：`21:49:12` `power-broadcast code=4` generation=2；`21:49:13` Kernel-Power 506 后旧 generation 被标记 stale，并接受 generation=3；`21:49:28` 收到 code=18/code=7；`21:49:29` Kernel-Power 507，用户唤醒分类成功。

FanHost 最终只完成一次有效 suspend 和一次 resume，说明旧 generation 抑制生效。但 PBT resume 与 Kernel-Power 507 相差约 1 秒，且存在两次 sleep intent；这不是 HC 原版单一 `SystemEvents` 语义的直接等价证明，继续标记 UNKNOWN。

## HC 对比裁决

| 项目 | 结果 |
|---|---|
| HC 设备 Open/OpenEvents | 实机 PASS |
| 风扇曲线实际写入 | 实机 PASS |
| 睡眠前 HC restore/Close | 实机 PASS |
| 唤醒后 Open/OpenEvents/恢复曲线 | 实机 PASS |
| 物理 OEM 交还读回 | UNKNOWN；仅有 HC callback/readback-unconfirmed |
| YMCC 完整退出观测 | FAIL/待修复；前端误报 close transport failure |
| 旧 Host 到新 YMCC 接管 | FAIL/待修复；Stopped 常驻 Host 无令牌接管 |
| 多源睡眠/唤醒是否与 HC 完全等价 | UNKNOWN |
| 完整 HC ManagerFactory 应用图 | UNKNOWN/非目标；不应复制非风扇管理器 |

## 可保留的非 HC 外层机制

本次没有证据要求移除：HWiNFO 共享内存温度源、power generation、10 秒 Fan Guard、lease/heartbeat、coordinator gate、manifest/hash/ACL/session token。它们是 YMCC 外层安全机制，不得改名为 HC 原生设置，也不得绕过 HC 风扇回调。

## 下一轮顺序

1. 修复“Host 已成功 Close、前端却被 IPC 取消”的退出观测边界。
2. 修复或证明 `Stopped + hostWillRemainResident` 的旧 Host 接管合同，避免无令牌盲发 shutdown。
3. 继续核对 PBT、Kernel-Power 506/507、generation 与 FanHost suspend/resume 的实际顺序。
4. 修复后重新测试：开启 → 睡眠 → 唤醒 → 关闭风扇 → 退出 YMCC → 二次启动握手。

本证据不授权修改 HC 源码，也不授权启用非风扇 HC ManagerFactory 图。

## 复核补正（2026-09-16，审计系列 §28.45，未改码）

**不可复核（物证缺失）**：两份现场日志（`YeMan-sleep-logs-20260901-214950-601.log` 与对应 fan zip）
与本文档所引桌面目录均已不在，全部时间线（21:48-21:49）**无法第一手复核**。

**两个 FAIL 项的后续状态（引用须带时点）**
1. "退出请求的前端观测与 Host 实际结果不一致"——后续 09-01 第五/六轮（严格收尾：HC 终态
   `Stopped` / `not-started` 双接受 + 监听器/队列收尾）与 09-02 更正轮对同类边界做了修复；
   **本项未在本次复核中重放** ⇒ 保留实机再验。
2. "重启接管失败（401 API_SESSION_REQUIRED）"——后续已有**会话 sidecar 机制**在用：
   native 读取 `%LOCALAPPDATA%\YeManCC\fan-host\YeManFanHost.session`（`fanHostReadSessionToken`
   L25852，格式 64-hex 校验）、紧急恢复脚本同源读取、payload 构建器白名单与自测断言均覆盖；
   ⇒ 机制已具，**实机二次启动接管未复测**。

**机制项历史化**：表中"21:49:38 前端 Fan Guard 观察到 `Ready`"——Fan Guard **整链已删**
（`fanHost.ts` L2027），`FAN_GUARD_OBSERVATION_ONLY` 全树零命中 ⇒ 该观测项属 V5 期。

**仍成立**：HC 设备级 Open/OpenEvents/曲线写入/睡前 restore-Close/唤醒重建 五项实机 PASS 判定，
与"物理 OEM 读回 UNKNOWN / 完整 ManagerFactory 图非目标"的边界，与现行记录一致。
