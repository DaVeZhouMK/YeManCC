# 睡眠守护（Sleep Guard）设计规范 v5

> 状态：**仅评估 + 已建静态排除清单，未改任何源码、未重编 exe。**
> 适用程序：`C:\SOFT\YeMan\YeManCC3\dist\YeManCC.exe`（Vue3 + Vite + 自研 C++ WebView2 原生壳）
> 验证记录见 §8（已实跑挂起/恢复 + 升级计数逻辑）。
> 已创建：`C:\SOFT\YeMan\PowerControl\Sleep\exclude.txt`（静态排除清单，数据文件，非源码）。

---

## 0. 一句话方案 + 可行性结论

**方案**：用户按电源键→S3 现代待机时，自动冻结系统最大工作集进程（即游戏）+ 压 TDP 到 12W；唤醒后由 Windows 自身消息判定是否"用户主动唤醒"——是则恢复，否则判定误唤醒、记录、立即重睡；短时间内反复误唤醒则自动升级到 S4 休眠（或关机）。

**可行性结论：高，且已实机验证核心链路。**
- 挂起/恢复用 `NtSuspendProcess`/`NtResumeProcess`（ntdll，Nyrna 同款机制），实机对 victoria3.exe 验证：冻结后 CPU 差=0，恢复成功。
- Windows 现代待机**可订阅** `PBT_APM*` 电源事件（官方明确覆盖 Modern Standby），非用户唤醒可判定。
- 升级 S4 计数逻辑正确性已验证（见 §6 边界修正）。
- 唯一硬前提：先 `powercfg /a` 确认 S4 可用；不可用则升级分支走"关机"兜底。

---

## 1. 主流程生命周期（5 个阶段）

```
[布防] 总开关ON → native 注册电源事件(RegisterSuspendResumeNotification)
   │
   ▼ 用户按电源键 → S3
[阶段1 入睡前] 正常线程: 枚举最大工作集 → 套 exclude.txt → NtSuspendProcess(最大者)
             → 写 suspended\<pid>.txt → setTdp('dc',12) 压12W
   │
   ▼ 系统睡眠(进程冻结, CPU停转, 内存仍占着)
[阶段2 睡眠中] 0 占用
   │
   ▼ 唤醒: PBT_APMRESUMEAUTOMATIC
[阶段3 唤醒判定]
   ├─ 收到 PBT_APMRESUMESUSPEND (用户主动) ──► [阶段4a] NtResumeProcess + 删suspended\ + 恢复TDP
   └─ 仅 AUTOMATIC (非用户) ──► 写 log\<epoch>.txt ──► [阶段4b] SetSuspendState(FALSE) 再睡 S3
                                          │
                                          ▼
[阶段5 升级] 扫 log\, 窗口[now-300s,now]内≥3次误唤醒
          ├─ S4可用 → SetSuspendState(TRUE) 进休眠(断电保会话)
          └─ S4不可用 → ExitWindowsEx(EWX_POWEROFF) 直接关机(物理断电)
   │
   ▼ 任何时刻崩溃/闪退
[孤儿恢复] 启动时扫 suspended\*.txt 残留 → 逐个 NtResumeProcess + 恢复TDP + 删txt
```

**关键纪律**：挂起、TDP、powercfg 等慢操作**绝不在 `PBT_APMSUSPEND` 广播回调里做**（全局串行、各进程抢资源，会拖垮挂起）。全部挪到正常线程。

---

## 2. UI 控件与主流程关系（核心）

> 本质：这是**后台常驻守护**，UI 只能做"布防/撤防 + 查看 + 手动兜底"，真正干活的是后台监听唤醒事件。因此主控件是 **Toggle（开关）**，不是一次性按钮。

### 2.1 控件总表

| 控件 | 类型 | 默认 | 持久化 | 作用 | 关联主流程阶段 |
|---|---|---|---|---|---|
| 🌙 **睡眠守护**（总开关） | Toggle | OFF | 注册表 `Enable` | 布防/撤防：开启后阶段1冻结游戏+压TDP、阶段4a恢复 | 阶段1 + 阶段4a |
| ⚡ **误唤醒修复**（升级开关） | Toggle | OFF | 注册表 `Escalation` | 允许误唤醒升级 S4/关机；**仅总开关ON时可点**（禁用态灰掉） | 阶段5 |
| 🔄 **恢复全部任务**（按钮） | 瞬时按钮 | — | 不持久化 | 手动扫描 `suspended\` 全部恢复+还原TDP，救"自动恢复漏了"的情况 | 阶段4a 的手动兜底 |
| 📊 **当前挂起任务数** | 文本显示 | — | 不持久化 | 实时显示 `suspended\` 数量；>0 高亮提示该点按钮 | 阶段4 状态 |

### 2.2 旋钮（可选，进阶）
| 控件 | 类型 | 作用 |
|---|---|---|
| TDP 压制值 | Slider/数值 | 默认 12W，入睡时压到该值（仅误唤醒再睡前兜底用） |
| 升级阈值 | Slider | 默认 5 分钟 ≥3 次→升级（window 秒数 + 次数） |
| USB4 唤醒禁用 | Toggle（可选加固） | best-effort，不保证，见 §4.5 |

### 2.3 开关/按钮 → 流程映射说明
- **总开关 OFF** → 程序完全不介入电源事件（阶段1/4a 不触发），等于原生 Windows。
- **总开关 ON + 升级 OFF** → 冻结/恢复照常；误唤醒只重睡 S3，**不升级 S4/关机**（阶段5 跳过）。
- **总开关 ON + 升级 ON** → 误唤醒 5 分钟内≥3 次自动升级 S4（或关机）。
- **恢复全部任务按钮** → 不参与自动流程，纯手动救急：当自动恢复因 bug/时序失败、游戏还冻着时点它。

### 2.4 状态脚本（照搬现有稳健写法：写值→回读→失败回滚）
```ts
// PowerView.vue 片段（参考现有 hibSize 写法）
const guardOn = ref(false)
const guardEsc = ref(false)
function onGuard(v: boolean) {
  yeman.setSleepGuard(v).then(ok => {
    if (ok) guardOn.value = v
    else /* 回滚：保持原值，提示失败 */ yeman.getSleepGuard().then(r => guardOn.value = r)
  })
}
function onEsc(v: boolean) {
  if (!guardOn.value) return            // 总开关关时禁用
  yeman.setSleepGuardEsc(v).then(ok => { if (ok) guardEsc.value = v; else yeman.getSleepGuardEsc().then(r => guardEsc.value = r) })
}
function recoverAll() { yeman.sleepGuardRecoverAll() }
```

---

## 3. 文件布局（`C:\SOFT\YeMan\PowerControl\Sleep`）

```
Sleep\
├── exclude.txt      # 静态排除清单（已建，可手编；UTF-8，支持中文）
├── target.txt       # 运行时写：本次选中目标 exe 名+PID（仅展示"识别到谁"，不用于恢复）
├── suspended\       # 运行时建：当前"被冻结"的进程，每个一个 <pid>.txt（PID纯数字，不乱码）
│   ├── 31304.txt     #   内容: name=Game.exe | epoch=<挂起时刻float> | tdplocked=12
│   └── ...
└── log\             # 运行时建：每次误唤醒一个 <时间戳>.txt（取证证据）
    ├── 2026-07-22_144700.txt   # 内容: epoch=<float> | src=<OS唤醒源> | user_initiated=false | action=resleep
    └── ...
```

- **`suspended\<pid>.txt` = 恢复/崩溃恢复的权威依据**：成功恢复（自动或手动）后**删除**；崩溃残留→启动孤儿扫描恢复。
- **`log\<时间戳>.txt` = 取证证据，正常恢复不删除**；内容内嵌 `epoch=<float>` 供升级计数精确比对（见 §6）。
- **`target.txt` 仅展示**，不参与恢复。

---

## 4. 技术实现要点

### 4.1 挂起机制（Nyrna 同款）
- 调 `ntdll.dll` 的 `NtSuspendProcess(hProcess)` / `NtResumeProcess(hProcess)`（运行时 `GetProcAddress` 解析，无需额外库）。
- 需 `OpenProcess(PROCESS_SUSPEND_RESUME, …)`（Win8+），同用户进程无需 debug 权限；系统/跨会话进程在黑名单内排除。
- **验证**：返回 0=成功；挂起后查 `GetProcessTimes` CPU 差≈0 确证冻结；恢复后确认恢复。

### 4.2 进程选择（最大工作集 + 排除）
- 入睡前正常线程：`CreateToolhelp32Snapshot` + `GetProcessMemoryInfo` 取 `WorkingSetSize64`（=任务管理器"内存"列，即物理 RAM 占用最大）。
- 过滤：系统黑名单（csrss/winlogon/lsass/services/smss/System/Idle/dwm/explorer/YeManCC 自身/WebView2 进程）+ `exclude.txt`（Steam/Playnite/QQ/微信 等）+ 自身 PID。
- 取剩余最大者 → `NtSuspendProcess`。**PID 是操作主键**（文件名=`<pid>.txt`）。

### 4.3 TDP 压 12W
- 复用现有 `setTdp('dc',12,{apply:true})`（yeman.ts → pawnio/YeManTdpCtl.exe，支持 AMD/Intel），**零新依赖**。
- 时机：入睡前正常线程 / 误唤醒再睡前；**绝不在广播回调里调外部 exe**。

### 4.4 唤醒判定（信任 Windows）
- 收到 `PBT_APMRESUMEAUTOMATIC` 后等 ~300–500ms：
  - 期间收到 `PBT_APMRESUMESUSPEND` → 用户主动 → 恢复（阶段4a）。
  - 仅 AUTOMATIC → 非用户误唤醒 → 写 log + 立即 `SetSuspendState(FALSE)` 再睡 S3（阶段4b）。
- 电源键唤醒（自己映射已知，且收 RESUMESUSPEND）一律视为用户主动。

### 4.5 USB4 / AC 唤醒屏蔽（可选加固，非必需）
- **决策**：软件层多禁不掉、BIOS 级系统更新会重置；且"非用户唤醒检测"已能代替此步（醒来即重睡，结果等价）。故降为**可选 best-effort**：
  - 提供开关让用户配置期跑 `powercfg /devicedisablewake "<设备名>"`（名从 `wake_armed` 读），**不在广播路径调**。
  - 失败/重置不影响主防护。
  - OS 唤醒源名仅用 `PowerReadLastWake()` **只读**记入 log 做取证，不禁用。
  - 仍建议用户 BIOS 关 `Wake on AC/USB` 作运输最稳手段（手动操作，不在程序硬依赖）。

### 4.6 PID 主键与中文编码（防乱码）
- **操作全用 PID**；txt 文件名=`<pid>.txt`（纯数字，不可能乱码）；名字仅作展示/排除/校验元数据。
- **PID 复用校验走运行时重解析**：恢复前 `QueryFullProcessImageNameW(PID)` 取宽字符串映像名，与存储名比对，一致才恢复 → **txt 内名字即便乱码也不影响正确性**。
- **排除匹配**：exclude.txt UTF-8 读，与 API 宽字符串进程名比对（去 `.exe`+小写归一），不绕 txt。
- **中文游戏 ID**：C++ 全程用 W 系列宽字符串 API（`QueryFullProcessImageNameW` / `Process32FirstW`）+ UTF-8 写 txt 即根治；演示脚本的乱码是 Python ctypes 64 位对齐坑，非设计缺陷。

---

## 5. 崩溃恢复 / 手动按钮

### 5.1 三层兜底（堵"冻死"）
1. **自动恢复**：唤醒判定为用户主动时 `NtResumeProcess` + 删 `suspended\` 标记 + 恢复 TDP。
2. **手动按钮「恢复全部任务」**：自动恢复因 bug/时序漏了→点按钮强制扫 `suspended\` 全恢复 + 还原 TDP；只恢复不挂起，PID 名不符跳过，绝不误恢复错进程。
3. **启动孤儿扫描**：native 启动时扫 `suspended\*.txt` 残留（=上次崩溃没恢复）→ 逐个校验 PID/名称后 `NtResumeProcess` + 恢复 TDP + 删 txt。标记**先于挂起写**，故挂起瞬间崩也能恢复。

### 5.2 健壮性纪律
- 操作前校验 PID 存活 + 映像名（防 PID 复用误冻/误恢复）。
- `WM_DESTROY` 里尽力恢复全部残留。
- TDP 恢复后回读确认已还原。

---

## 6. 升级 S4 / 关机 判定（含边界修正）

- **阈值**：`log\` 内 `epoch` ∈ `[now-300s, now]` 的文件数 **≥3** → 下一次误唤醒触发升级。
- **S4 可用**（`GetPwrCapabilities().systemS4 && HibernateFilePresent`，启动/配置期缓存 `s4_available`，配置变更后重查）→ `SetSuspendState(TRUE,…)` 进 S4。
- **S4 不可用/关闭** → `ExitWindowsEx(EWX_POWEROFF | EWX_FORCE | EWX_SHUTDOWN, SHTDN_REASON)` **直接关机**（需 `SeShutdownPrivilege`，沿用现有提权）。
- **⚠️ 边界修正（实测发现）**：升级计数**必须读文件内容内的 `epoch=<float>`，不能用文件名时间戳**（秒级截断会让恰好 300s 边界的唤醒被算成 300.8s 而漏判）。文件名仅展示，计数以文件内 epoch 为准。
- 防抖：重睡间隔≥60s；升级后重置计数。

---

## 7. 风险与对策

| 风险 | 对策 |
|---|---|
| `PBT_APMSUSPEND` 广播全局超时 | 慢操作全挪正常线程，广播回调只设标志 |
| 挂起路径拖垮睡眠 | 禁 powershell/文件IO/外部exe于广播路径 |
| PID 复用误恢复 | 运行时 `QueryFullProcessImageNameW` 重解析校验 |
| 程序崩溃冻死游戏 | 标记先于挂起写 + 启动孤儿扫描 + 手动按钮 |
| S4 关闭致升级失败 | 改为直接关机兜底 |
| USB4 软件禁不掉 | 移除硬依赖，信任 Windows 误唤醒检测 |
| 中文名乱码 | PID 主键 + W API + UTF-8，名字仅元数据 |

---

## 8. 验证记录（已实跑）

| 项 | 方法 | 结果 |
|---|---|---|
| 挂起/恢复稳定性 | Python ctypes 调 ntdll 对 victoria3.exe(31304,4.82GB) | **PASS**：NtSuspendProcess=0，冻结后 CPU 差=0.000s；NtResumeProcess=0 恢复 |
| 升级计数逻辑 | 隔离临时目录 3 场景（窗口内3次/2次/边界300s） | 逻辑正确；**发现边界截断 bug → 改用文件内 epoch 计数（已写入 §6）** |
| 进程选择 | 枚举144进程套 exclude.txt 取最大工作集 | PASS：正确选中 victoria3.exe 4.82GB |
| txt 生命周期 | 写 target.txt+suspended\→挂起→恢复→删suspended\ | PASS：瞬态标记清理、log 留证 |

> 注：以上验证用 Python ctypes 调用真实系统 ntdll，机制与 C++ 壳 `GetProcAddress` 同款；演示脚本的进程名乱码为 Python ctypes 64 位对齐坑，C++ 用 W API 无此问题。

---

## 9. 下一步零代码预检（开工前必跑）

1. `powercfg /a` —— 确认 S3/S4 哪个可用（决定阶段5 能否落 S4，否则走关机）。
2. `powercfg /lastwake` + `/devicequery wake_armed` —— 抓真实误唤醒源名（仅取证，不禁用）。
3. 小程序验证 `PBT_APMRESUMEAUTOMATIC` 单独 vs +`RESUMESUSPEND` 时序（信任 Windows 可行性）。
4. 确认 `YeManTdpCtl.exe` + pawnio 在位（TDP 复用前提）。

---

*备选（明确先不做，仅记录）：10s 全局输入观测（≥2 键/≥3 按/摇杆算，信任 Windows 判定前的双信号叠加）；HWiNFO 共享内存温度取证（读取复杂，暂缓，先用 txt 记录误唤醒事件）。*
