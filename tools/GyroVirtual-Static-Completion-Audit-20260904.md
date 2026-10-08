# GyroVirtual 静态结案审计报告（2026-09-04）

## 结论

本报告结案的是 **BUS 当前源码静态审计**，不是 GyroVirtual 产品 runtime 结案。

当前可确认状态：

```text
CURRENT-SOURCE-STATIC-COMPLETE
/ T21-T14-STATIC-REAUDIT-COMPLETE
/ T10-T13-IMPLEMENTATION-PREPARATION-ONLY
/ T15-T18-R1-RUNTIME-BLOCKED
```

当前证据基线为 [P14 manifest](T22-RF00-P14-C06-C07-C08-C09-T21-current-source-manifest-20260904.json)：

- 20 个 key file；`sourceDigest=B1C0CEEC314F2A9F73378943FE1630DD42C9E1078493E62888A35D8AD514E8A7`
- YMCC HEAD：`aa8f38b83cc960267d2f68bf78d0e8adaca2234f`
- frozen HC：`0.32.4.0 / 06c0b9544db2b1f39abf9cd3796225ccfb096103`，worktree clean
- porcelain：337 entries；SHA-256 `B0BF3BC08D505C2B1A515BABF3B7128060CAC47E309048D30E6030F7188419C3`
- 本报告未运行 build、type-check、selftest、InputHost、HidHide、ROG report、Steam、游戏、虚拟设备或真实硬件。

## 已修正的错误

| ID | 错误 | 当前处置 | 结论边界 |
|---|---|---|---|
| C02 | native 只要一个 feature-assets 目录就允许采集/Host | 改为两个目录都存在才准入 | 静态 gate 正确；未运行 Host |
| C03 | `pairProven=false` 时 raw gyro 仍可写入 InputHost state | 直通改为 `pairProven && g_gmLocked` 才提交 GyroDps | safe-zero 仅是 state-file 路径，不是 wire no-report 证据 |
| C04 | 无 HC curve node 时敏感度对非零输入返回固定 `1.0` | 改为 HC fallback 的 normalized absolute magnitude，并拒绝非有限值 | 未实现完整 profile curve 消费 |
| C05 | Xbox ROG matrix 被无条件套给全部 Windows Sensor | 仅 `g_rog.identityMatched` 时使用 ROG matrix | MSI/Lenovo/未知设备仍未绑定 matrix |
| C06 | 前端诊断用 `virtualGamepad || gyroMotion`，native 用双目录 gate | 前端诊断改为 `virtualGamepad && gyroMotion`；侧边栏继续逐目录显示 | 未执行前端/Host runtime 验证 |
| C07 | updater policy selftest 仍要求测试目录不能进入 ZIP | 静态断言改为完整测试 ZIP 含目录、update manifest 声明 `updater-exclude` | 未运行 selftest/打包/升级 |
| C08 | RF00 digest 的实际排序不可复现 | 改为显式 lexical `(scope, relativePath)` 排序 | 仅审计工件问题 |
| C09 | 主任务书顶部仍显示 RF00 stale，与已验证 current manifest 矛盾 | 顶部状态改为 P14 current-source static | 不改变任何产品行为 |

## 与 HC 对齐的部分（已证静态事实）

| 范围 | 证据 | 审计结论 |
|---|---|---|
| Xbox ROG Ally X gyro matrix | HC `XboxROGAllyX.cs` gyro `(1,1,-1)` + Y/Z swap；YMCC `inputCaptureApplyRogGyro` 为 `x,z,-y` | 仅已识别 ROG identity 对齐 |
| Xbox ROG Ally X accel matrix | HC accel `(-1,-1,1)` + Y/Z swap；YMCC 为 `-x,-z,y` | 仅已识别 ROG identity 对齐 |
| 无 curve node 的 sensitivity fallback | HC `Utils/InputUtils.cs::ApplyCustomSensitivity`；YMCC C04 fallback | fallback 对齐，不代表完整 profile/ADS 曲线已接线 |
| ROG OEM Disable | HC 存在设备级切面，但本项目实机已有反例 | YMCC 已禁止把 Disable 用作 summon/game 隔离；这是正确的安全裁决，不称为 HC 完整 lifecycle parity |

## 偏离 HC 的部分

这些是已定位的不同点；除已标“安全适配”外，均未被宣称为正确或已闭口。

| 分类 | HC | YMCC 当前 | 状态 |
|---|---|---|---|
| 传感器选择 | `Gyrometer.GetDefault()` / `Accelerometer.GetDefault()` | `GetSensorsByType()` 后选第一个 | `UNENCLOSED / HC-SENSOR-SELECTION-DIVERGENCE` |
| 采样调度 | 设置 report interval + `ReadingChanged` 事件 | 启动后固定 2500ms，再约 50ms 轮询 | `UNENCLOSED / HC-SENSOR-SCHEDULING-DIVERGENCE` |
| ROG stable HID | 打开已绑定 HID，`ReadLoopAsync()` + `ReadReportAsync()` | 只发现/记录 path 与长度；reader 没有 live handle/preparsed/call site | `UNENCLOSED / T13-ROG-STABLE-HID-READ-MISSING` |
| 虚拟手柄层 | HC VirtualManager/目标实现 | YMCC InputHost + HIDMaestro state-file 适配器 | 产品架构不同，descriptor/wire parity 未证 |
| 释放序 | HC 生命周期管理不等于完整产品 release proof | YMCC candidate 缺 neutral receipt、ACK、join、epoch、恢复收据 | `DGF-02 / RUNTIME-BLOCKED` |

## 项目原创逻辑 / 安全适配

下列不是 HC 原样代码。它们被明确分类，避免误称“HC 完全一致”。

| 逻辑 | 定位/目的 | 审计评价 |
|---|---|---|
| `pairProven` gate | YMCC 为自有 `g_gmProcess` 与 direct GyroDps 增加同 provider/epoch 配对证明门 | **安全适配**：pair 不明即 safe-zero/no-report；不是 HC `ProcessMotion` 的原始签名 |
| P-HID / P-XINPUT / P-OWNER 三平面 | 用于拆开设备可见性、XInput、YMCC 语义 owner | **项目协调合同**：正确地拒绝由任一平面推导其他平面；外部消费者未证明 |
| 前端 owner semantic isolation | 呼出 YMCC 后转换为 semantic action | **项目扩展**，不等同 HC 虚拟 HID 回读；只可声称已观察的 YMCC 范围 |
| feature-assets 双目录 | 完整测试 surface 显式 gate，发布更新安装复制排除 | **项目发布策略**；静态一致，未完成真实升级实证 |
| InputHost state-file | HIDMaestro transport candidate | **项目 adapter**，不是 HC VirtualManager；无 descriptor/readback/consumer 证明 |
| `cgz/cgx * 1000`、fixed LocalSpace、`gyroWeight=1.2` | direct native candidate 路由 | **历史/候选原创固定路由**，属于 parameter-not-consumed，不是 HC parity |

## 未知与缺口

### 阻断 runtime 闭口的高优先级缺口

1. **T15 参数闭口**：缺 request → neutral → exact ACK(revision/hash) → same-instance first-active frame；当前 UI/CAS 保存不能证明 Host 参数已消费。
2. **T16 校准/复位闭口**：缺真实 provider identity、gyro+accel 同代 sequence、epoch、confidence/steady、stop/dispose/join/recovery trace。
3. **T17 wire 闭口**：缺 HIDMaestro descriptor 原件、reportId、length、offset、endianness、decoder/readback、submit 与指定 consumer 观察。X360 必须继续拒绝 IMU。
4. **T18 可见性/恢复闭口**：缺同一物理 identity 上的 P-HID/P-XINPUT/P-OWNER before/after、allowlist diff、restore receipt 与异常恢复。
5. **ROG 读取闭口**：stable HID 读循环尚未接入；接入前必须先设计 Coordinator ownership、入口、停止、睡眠、PnP、崩溃恢复，不能直接把 HC loop 粘贴进 native。
6. **外部游戏消费者**：`gameInputCount=0` 与 mock 计数都不是 Steam/游戏测量；不能以 HidHide 或 owner mirror 宣称游戏隔离。

### 仍未知，不能猜测

- MSI Claw、Lenovo 或未知设备的 provider/matrix；不得推广 ROG matrix。
- C06/C07 后的实际构建、测试包 ZIP、升级复制和旧目录清理结果。
- 物理静置漂移是否已消除；当前没有同代 motion capture/runtime trace，不能用静态代码宣称已修复。
- ROG 双 XInput/虚拟 DS4 观察是否在外部消费者层真正隔离；当前仍 `UNENCLOSED`。

## 审计过程发现的非产品错误

- 初次 HC 搜索使用了不存在的 `HandheldCompanion/Helpers/InputUtils.cs`；实际文件是 `HandheldCompanion/Utils/InputUtils.cs`。这是审计 locator 错误，已在本报告中纠正，未影响任何产品源文件。
- P10 的 manifest digest 生成不可重放，已由 C08/P11–P14 修正；P14 已独立重算验证。

## 下一阶段与所需实证

下一步不是继续静态改参数，而是按顺序进入：

```text
T15/T16（参数 + 校准/复位 runtime trace）
→ T17（persona/descriptor/wire/consumer）
→ T18（visibility/owner/recovery）
→ R1（Steam/游戏 + 睡眠/拔插/崩溃恢复）
```

在具备陀螺仪的目标设备和明确 runtime 授权前，审计结论必须保持 `RUNTIME-BLOCKED`。

