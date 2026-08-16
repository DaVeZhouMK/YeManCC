# 游戏识别阀门规则

## 1. 定义

“游戏识别阀门”是 YeManCC 中唯一负责从运行中的进程候选里选出当前游戏目标的核心机制。

技术职责名称：`GameTargetArbiter` / `nativeValveAcquire`

产品规则名称：游戏识别阀门

核心目标：

```text
进程候选扫描
    -> 黑白名单和基础条件过滤
    -> 唯一目标仲裁
    -> 输出一个游戏目标或无目标
    -> 所有游戏相关功能共同消费该目标
```

阀门的输出不是“可能的游戏列表”，而是：

- 有目标：只允许一个游戏 PID。
- 无目标：返回 `null`，并发布无效目标快照。

## 2. 权威归属

1. native 层持有阀门状态，前端不拥有第二份游戏真相。
2. `game.detect` 是前端获取阀门目标的 IPC 入口。
3. `nativeScanGame` 是游戏候选扫描入口。
4. `nativeValveAcquire` 是唯一目标仲裁和状态获取入口。
5. FPS、性能调度、睡眠暂停、游戏加速、关闭游戏和动态 Steam 壁纸都不能自行重新选择游戏进程。

共享快照路径：

```text
C:\SOFT\YeMan\PowerControl\game-target.json
```

该文件供 PowerShell 兼容脚本和外部监控读取。native 使用原子写入发布快照。

## 3. 唯一目标身份

阀门目标必须同时记录：

| 字段 | 规则 |
| --- | --- |
| `pid` | 实际控制用的 Windows 进程 PID，必须大于 0 |
| `processCreated` | 进程创建时间，防止 Windows PID 重用，必须存在且大于 0 |
| `name` | 进程名，仅用于显示和规则匹配 |
| `title` | 窗口标题，用于显示及动态 Steam 壁纸反向匹配 |
| `path` | 进程完整路径，用于身份复核 |
| `workingSet` | 当前工作集，用于候选仲裁和诊断 |
| `source` | `memory` 或 `whitelist` |
| `whitelistRule` | 命中的白名单规则 |
| `selectedAt` | 当前进程实例被选中的时间 |
| `lastSeen` | 最近一次确认仍有效的时间 |
| `rulesEpoch` | 规则版本，用于规则变更后的重新仲裁 |
| `generation` | 单调递增发布序号，防止并发快照乱序 |
| `valid` | 快照是否有有效目标 |

PID 是实际控制选择器；`processCreated` 是 PID 的强制身份护栏。名称、标题和路径不能单独替代 PID，也不能单独成为控制目标。

## 4. 候选扫描规则

### 4.1 基础范围

- 只扫描当前用户会话中的进程。
- 排除 PID `0`、PID `4` 和 native 自身进程。
- 必须能够查询进程信息、工作集和创建时间。
- 无法读取进程创建时间的候选直接丢弃。
- 窗口标题只在候选已经通过进程规则后读取。

### 4.2 内置黑名单

内置黑名单默认排除系统、桌面、浏览器、WebView、驱动工具、远程控制工具、Steam 自身、叠加层和虚拟机进程等候选；显式用户白名单可以覆盖该排除规则。

当前内置规则由 native 的 `nativeMonitorExcluded` 和共享黑名单定义维护，包含如下类别：

- Windows 系统和桌面进程。
- 浏览器、WebView2、PowerShell 和 YeManCC 自身。
- RTSS、HWiNFO、Lossless Scaling、Magpie、Game Bar、OpenSpeedy 等工具。
- Java、Python、Node、.NET 等通用运行时，以及 CEF、崩溃报告器和 Windows 错误报告辅助进程。
- Steam、Steam Web Helper、Discord、Teams 等常驻或通信程序。
- VMware、VirtualBox、QEMU、WSL/Hyper-V 等虚拟化进程。

黑名单命中后，不再进入游戏候选竞争。

### 4.3 用户黑名单

用户黑名单文件：

```text
C:\SOFT\YeMan\PowerControl\Sleep\player-blacklist.txt
```

规则特点：

- 按规范化后的 exe 基础名匹配。
- 支持安全的通配符模式。
- `#` 后内容视为注释，空行忽略。
- 用户黑名单是默认排除条件；显式用户白名单优先于用户黑名单。
- 黑名单命中后，进程不能成为阀门目标。

### 4.4 用户白名单

用户白名单文件：

```text
C:\SOFT\YeMan\PowerControl\Sleep\game-whitelist.txt
```

白名单语义保持原有逻辑：白名单是“允许进入游戏识别并降低捕获门槛”，不是仅凭名称证明它一定是游戏。

- 白名单同样按规范化 exe 基础名和通配符匹配。
- 白名单允许与黑名单重叠，运行时白名单优先。
- 白名单候选最低工作集为 `50 MB`。
- 普通候选最低工作集为 `500 MB`。
- 存在有效白名单候选时，白名单候选优先于普通工作集候选。
- 同一类别内仍按工作集较大者竞争。
- 白名单可以覆盖内置黑名单和用户黑名单，但仍需满足白名单候选的最低工作集门槛。

## 5. 仲裁和稳定性规则

### 5.1 普通获取

调用 `nativeValveAcquire()` 且没有指定 PID 时：

1. 如果当前阀门目标仍然有效，继续保留当前进程实例。
2. 不因工作集轻微变化而在多个候选之间来回切换。
3. 当前目标退出、身份变化、路径变化、命中黑名单或不再满足规则时，才重新扫描候选。
4. 重新扫描后只提交一个新目标。
5. 没有合格候选时清空目标并发布无效快照。

### 5.2 规则变更

黑白名单变化会增加 `rulesEpoch`，规则变化是重新仲裁边界：

- 先用完整规则重新验证旧 PID。
- 旧 PID 仍满足规则时可以继续作为唯一目标。
- 旧 PID 不满足规则时重新扫描。
- 新规则不能让旧的无效目标继续被下游使用。

### 5.3 指定 PID 获取

调用带有指定 PID 的 `nativeValveAcquire(preferredPid)` 时，语义是严格验证该 PID：

- 只验证指定 PID 是否满足现有规则。
- 指定 PID 不合格时返回无目标。
- 不允许悄悄换成同一轮扫描中另一个工作集更大的进程。
- 该规则用于前台召回、暂停、关闭和其他必须绑定原始 PID 的操作。

### 5.4 并发发布

- `generation` 单调递增。
- 只有更新的 generation 可以覆盖旧快照。
- 快照使用原子替换，避免读到半个 JSON。
- 无目标时发布 `valid: false` 的 tombstone，而不是仅删除文件。
- 下游看到无效快照后必须按“当前没有游戏目标”处理。

## 6. 共享快照有效性

外部脚本读取 `game-target.json` 时必须同时检查：

1. `valid` 为 `true`。
2. `pid` 大于 0。
3. `processCreated` 大于 0。
4. `generation` 大于 0。
5. `lastSeen` 存在且没有超过约 10 秒。
6. 当前 PID 仍然存在。
7. 当前进程创建时间等于快照中的 `processCreated`。

任一条件失败，都必须安全地按无目标处理，不能根据旧 PID 猜测或重新按名称选择进程。

## 7. 下游功能接入规则

### 7.1 游戏识别和界面

前端轮询和订阅只显示 native 阀门返回的唯一目标。前端缓存只能减少重复请求，不能绕过 native 重新选择目标。

### 7.2 FPS 和性能监控

- FPS 监控脚本只读取 `game-target.json`。
- HWiNFO FPS 是否大于 0 只决定是否输出 FPS 数据，不决定游戏 PID。
- 性能调度和 CPU 脚本只使用快照中的唯一 PID。
- 这些脚本不得重新枚举进程、按名称选择或按工作集选择第二个 PID。

### 7.3 游戏暂停和恢复

手动暂停、睡眠暂停和恢复共同遵守阀门身份：

- 暂停前必须验证传入 PID 是当前阀门目标。
- 实际暂停只针对唯一根 PID，不因子进程工作集较大而递归冻结整棵进程树。
- 暂停记录保存 `pid + processCreated`。
- 恢复前必须验证记录的创建时间仍匹配当前进程。
- PID 已退出或已被复用时标记为 stale 并清除，不得恢复新进程。
- 前端恢复状态不得把完整身份降级成只含 PID 的记录。
- 用户可见的睡眠唤醒会合并恢复 `manual-suspended` 中有效的手动暂停租约；这表示睡眠恢复优先保证游戏不会遗留在暂停状态，即使该手动暂停发生在本轮睡眠之前。
- 若将来需要“睡眠前手动暂停，唤醒后仍保持暂停”的产品语义，必须在 marker 中增加暂停来源/世代信息；不能仅靠 PID 或目录名推断。

### 7.4 关闭游戏

关闭游戏属于高风险控制操作，必须：

1. 重新获取当前阀门目标。
2. 验证页面传入 PID 与阀门 PID 一致。
3. 验证页面保存的 `processCreated` 与阀门一致。
4. 通过 `process.identity` 再次确认当前 PID 的创建时间。
5. 通过控制队列串行执行 `taskkill /F /T /PID`。
6. 等待退出时同时检查 PID 和创建时间；PID 被复用时视为原实例已退出。

### 7.5 游戏加速

游戏加速在每次应用前重新获取阀门目标：

- 调用方 PID 只是期望值。
- 实际控制 PID 和创建时间来自阀门。
- PID 或创建时间不一致时拒绝操作并保持安全的 `1x`。
- OpenSpeedy 的注入、启用、调速和清理都必须复核进程创建时间。
- 切换游戏时只清理保存过的旧实例，不能按旧 PID 重新选择新进程。

### 7.6 动态 Steam 壁纸

这是唯一明确采用“反向匹配”的功能，规则必须保持不变：

```text
游戏识别阀门
    -> 唯一 PID + processCreated + title/name
    -> 使用 title/name 匹配 Steam AppID
    -> 使用 Steam AppID 获取媒体
    -> native 只允许原阀门 PID + processCreated 提交和读取媒体
```

禁止改成“PID 直接查 Steam AppID”，因为 Steam AppID 与 Windows PID 没有稳定映射关系。

动态壁纸规则：

- `title` 优先用于识别显示名称，`name` 作为进程名和兜底。
- AppID 解析可以使用固定标题映射、已安装 Steam 库、缓存、Steam 搜索和 exe 兜底。
- Steam 匹配必须经过游戏标题与 Steam 正式名称的相似度校验。
- 媒体安装和读取必须携带同一个 PID 与 `processCreated`。
- 游戏切换、PID 重用或阀门目标变化时，旧媒体不得继续作为新游戏媒体显示。

## 8. 不属于游戏阀门的 PID 枚举

全仓存在一些合法的 PID 操作，但它们不拥有游戏目标选择权：

- 焦点恢复：按已记录窗口、路径和身份恢复焦点，不输出游戏目标。
- FPS/TopMonitor 单实例清理：只清理旧监控脚本实例。
- `proc.running`：查询指定工具是否运行，不返回游戏 PID。
- 自动关闭名单：按用户明确配置的程序名关闭工具。
- QuickApp 辅助控制：可建立工具自身的父子进程树，但不能用来选择游戏根 PID。
- Steam、RTSS、HWiNFO、JoyXoff 等工具清理：只针对各自固定工具名称。

这些路径不得被误改成游戏识别入口，也不得把它们的 PID 写入 `game-target.json`。

## 9. 禁止事项

后续修改游戏相关模块时，禁止：

- 在下游模块重新 `CreateToolhelp32Snapshot` 后自行选游戏。
- 在下游模块按最大工作集重新选 PID。
- 只按进程名、窗口标题或路径执行游戏控制。
- 只保存 PID 而丢弃 `processCreated`。
- 动态 Steam 壁纸改成 PID 到 Steam AppID 的直接映射。
- FPS 大于 0 时另选一个 PID。
- 规则变化后继续使用未经重新验证的旧目标。
- 用删除快照文件代替发布无效 tombstone。

## 10. 维护检查清单

任何新增或修改的游戏功能都必须回答：

- 是否只消费 `game.detect` 或 `game-target.json` 的唯一目标？
- 是否同时保存并校验 `pid + processCreated`？
- 是否会在目标变化时拒绝旧操作？
- 是否把黑名单作为最终排除条件？
- 是否错误地引入了第二套候选扫描？
- 是否会在无目标时安全停止，而不是使用历史 PID？
- 是否会影响动态 Steam 壁纸的标题/名称反向匹配？
- 是否需要加入专项自测和完整构建验证？

## 11. 当前实现位置

- native 阀门扫描和仲裁：`native/main.cpp`
- 前端游戏识别桥：`src/bridge/gamedetect.ts`
- 黑白名单桥：`src/bridge/gameRules.ts`
- 动态 Steam 壁纸：`src/bridge/dynamicBackground.ts`、`native/main.cpp`
- 游戏暂停、恢复和关闭：`src/bridge/gameproc.ts`
- 游戏加速：`src/bridge/speedhack.ts`
- FPS 兼容消费者：`PowerControl/FPS-Monitor.ps1`
- CPU 亲和性消费者：`PowerControl/3-Core.ps1`、`PowerControl/8-Core.PS1`
- 兼容识别消费者：`PowerControl/_gamedetect.ps1`
- 兼容关闭消费者：`PowerControl/KiLL-EXE.ps1`
- 共享目标快照：`C:\SOFT\YeMan\PowerControl\game-target.json`

## 12. 验证记录

本规则对应的当前版本已完成：

- TypeScript 类型检查。
- OpenSpeedy 安全自测：9/9。
- M2 自测：23/23。
- 电源恢复自测：5/5。
- 性能调度 TDP 隔离自测通过。
- Web + native 完整构建通过。

文档记录的是当前实现规则。若代码行为发生变化，必须同步更新本文件，并重新执行相关专项测试和完整构建。
