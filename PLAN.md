# YeManCC · 野蛮系统控制中心 重构总规划

> 版本：v1.0 · 2026-07-20
> 本文件是 YeManCC 的**唯一任务对齐依据**。每次开工前先看这里，避免偏离主线。
> 重构对象：`C:\SOFT\YeMan\YeManUI.hta`（159 KB，186 个 JS 函数，4 大页面，功能大部分已验证可运行）

---

## 一、重构目标

| 项 | 决策 |
|---|---|
| **壳框架** | [强强 QiangQiang](https://github.com/kobolingfeng/qiangqiang) — C++ Win32 + WebView2，单 exe ~884 KB，零运行时依赖 |
| **前端** | **Vue 3**（Composition API + `<script setup>`）+ Vite + Pinia + TypeScript |
| **产物位置** | `C:\SOFT\YeMan\YeManCC3\`（全新目录，不动旧文件） |
| **窗口** | 无边框、420×760 起步可缩放、单实例、深色常驻（不跟随系统主题） |
| **交互** | **手柄 + 鼠标 全量可操作**（对标 Newko：边沿检测、A确认/B返回、LB/RB切页、滑块微调） |
| **TDP 调度** | PawnIO 方案（GPL-2.0 官方签名内核驱动），执行体 `PowerControl\pawnio\YeManTdpCtl.exe`（bat 直调，现状保留） |
| **旧资产** | `C:\SOFT\YeMan\PowerControl\` 内所有 txt / bat / vbs / ps1 / xml 任务计划**全部保留继续调用**，只在新层做编排与状态读取 |

### 为什么这样选
- HTA（VBScript+IE 内核）已到头：无现代 CSS、无手柄、无热重载、调试靠 alert。
- 强强壳与 Newko 同款：`C:\Program Files\Newko` 已验证该壳能稳定承载这类深色控制台 UI（400×720 无边框）。
- Vue3 是用户指定；强强对前端框架完全自由，Vite 产物直接 `bun run build` 进 exe。
- TDP 不再新造轮子：YeManTdpCtl 已完成 AMD(RyzenSMU.bin MP1 邮箱) / Intel(IntelMSR.bin MSR 0x610) 双通路，并带 PawnIO 缺失自动静默安装；bat 已直接调用，实测稳定。

---

## 二、页面架构（按用户要求重新规划）

旧 HTA 是 4 页：`tab-tdp / tab-rtss / tab-startup / tab-steam`（+支持页入口）。
新架构**拆成 6 页**，核心变化：**TDP 与 CPU 调度拆成两个独立页，TDP 在最前**。

| 序 | 页面 (路由) | 图标 | 来源 | 状态 |
|---|---|---|---|---|
| 1 | **TDP 功耗** `/tdp` | ⚡ | 拆自旧 tab-tdp 左列 | ✓ 完成（M3）|
| 2 | **CPU 调度** `/cpu` | 🎛️ | 拆自旧 tab-tdp 右列 | ✓ 完成（M4）|
| 3 | **RTSS 监控锁帧** `/rtss` | 📊 | 旧 tab-rtss 全量 | ✓ 完成（M5）|
| 4 | **电源按键 / 开机启动** `/power` | 🔌 | 旧 tab-startup 全量 | ✓ 完成（M6）|
| 5 | **Steam 大屏** `/steam` | 🎮 | 旧 tab-steam 全量 | ✓ 完成（M7）|
| 6 | **支持** `/support` | 🌐 | YeMan-Support.html 内嵌 | ✓ 完成（M7）|

左侧竖排导航（宽 150px），底部固定「野蛮系统更新主页」+ 红色「退出」。
手柄 **LB/RB** 按上表顺序循环切页。

### 页面 1：TDP 功耗 `/tdp`
```
┌─────────────────────────────────────────┐
│ AMD Ryzen 9 9950X 16-Core   [当前 AC 模式]│  ← 状态条：CPU 名 + AC/DC 徽标
├─────────────────────────────────────────┤
│ 🔌 AC 插电 TDP 上限            300 W     │
│ [━━━━━━━━━━━━━━━●━━] 滑块 5W 步进        │
│ [⚡插电恢复TDP 已启用] TDP最大值 [300 W▾] │
├─────────────────────────────────────────┤
│ 🔋 DC 离电 TDP 上限             35 W     │
│ [━━━━●━━━━━━━━━━━] 滑块                │
│ [🔋离电恢复TDP 已启用] TDP最大值 [35 W▾] │
├─────────────────────────────────────────┤
│ [🚀开机启动TDP+电源预设        已开启]   │
│ [⏰唤醒后恢复TDP+电源预设      已开启]   │
└─────────────────────────────────────────┘
```
- 滑块松开 → 写 `tdp-ac.txt` / `tdp-dc.txt` → 立即调 `YeManTdpCtl.exe set <W>`（当前模式）或仅存档（另一模式）。
- 「插电/离电恢复 TDP」开关 = 创建/删除任务计划 `TDP-插电AC模式TDP调节` / `TDP-离电DC模式TDP调节`（XML 现成）。
- 「开机启动」「唤醒后恢复」= 任务计划 `TDP-开机启动野蛮快设TDP挡位` / `唤醒后-执行任务`。
- 厂商识别沿用约定：`PowerControl\AMD.txt` / `intel.txt` 优先，否则读注册表 VendorIdentifier；提供手动覆盖对话框。

### 页面 2：CPU 调度 `/cpu`
```
┌─────────────────────────────────────────┐
│ 当前电源方案  野蛮系统电源   [野蛮系统电源▾]│
├─────────────────────────────────────────┤
│ ⚡AC 最大主频        1.0G  [睿频已关闭]   │
│ [━━●━━━━━━━━━━━━━]                      │
│ ⚡AC CPU主频调度积极性              79   │
│ [━━━━━━━━━━●━━━]                        │
├─────────────────────────────────────────┤
│ 🔋DC 最大主频        3.0G  [睿频已开启]   │
│ [━━━━━━●━━━━━━━]                        │
│ 🔋DC CPU主频调度积极性            100   │
│ [━━━━━━━━━━━━━━●]                       │
├─────────────────────────────────────────┤
│ ⚙ 大小核心调度                          │
│ [大核为主(推荐)] [仅大核] [仅小核]       │
├─────────────────────────────────────────┤
│ 重制电源  [选择电源方案 ▾]              │
└─────────────────────────────────────────┘
```
- 电源方案固定优先 `1cb8b882-a900-4b9f-9bac-99d151e64441`（YMElite.pow 已备）；缺则 `powercfg /import` 后设为活动。
- 主频/积极性滑块 → `powercfg /setacvalueindex|setdcvalueindex`（GUID 映射沿用 `TPD\Elite.bat` 里验证过的那组：最大主频 `75b0ae3f-...e100/e101`，积极性 `36687f9e-...e863/e864`，睿频 `be337238-...d470c7`）。
- 大小核调度 → 复用 `3-Core.bat` / `8-Core.bat` 及其 PS1。
- 「重制电源」→ 调 `TPD\Plan-*.bat`（Elite/Extreme/Medium/Performance/Silent/Tubo 六档现成）。

### 页面 3：RTSS 监控锁帧 `/rtss`
```
[RTSS 已启动] [监控数据 已开启] [FPS锁帧 已开启]   ← 三态状态卡
RTSS锁定帧率上限: 90 FPS          FPS [200▾]
[━━━━━━━━━━━━●━━━━━━] 滑块
[⚡插电恢复锁帧                    已开启]
🔋 DC 电池模式锁帧任务
[不启用][30 FPS][45 FPS][60 FPS][90 FPS]
监控样式切换[需重启已开启游戏]
[横版监控][竖版监控]
[🚀 开机启动RTSS监控              已开启]
[        复位RTSS全部设置(红)        ]
```
- 锁帧写 `FPS-ac.txt` / `FPS-dc.txt` → 调 `RTSS-FPS-AC.bat` / `RTSS-FPS-DC.bat`（内部走 RTSS-FPS.ps1：改 Global profile `Limit=` + rundll32 调 RTSSHooks64.dll 的 LoadProfile/SaveProfile/UpdateProfiles）。
- 「插电恢复锁帧」= 任务计划 `锁帧-插电AC模式锁帧`；DC 五档按钮 → 写 FPS-dc.txt + 任务计划 `锁帧-离电DC模式锁帧`（不启用=删任务）。
- 横/竖版监控 → 写 OverlayEditor.cfg 的 `Layout=YeManOBS-W-1/2.ovl`（沿用 YeManRTSS.bat 逻辑）。
- 开机启动 = 任务计划 `监控-开机启动监控锁帧软件RTSS`；复位 = 删全部相关任务 + 重置 txt + 关 OSD。

### 页面 4：电源按键 / 开机启动 `/power`
```
电源按钮
⚡AC 插电电源按钮              [不操作]
🔋DC 离电电源按钮              [不操作]
[🌙系统休眠开关                已开启]
休眠文件大小百分比设置(已占用27.7G)   45%
[━━━━●━━━━━━━━━━━]
启动模式                    屏幕自动旋转: 不可用
[🖥桌面模式                    已启用]
[🎮Xbox游戏模式                已启用]
[⚠Windows 屏幕自动旋转失效]          ← 冲突警告条(条件显示)
开机启动项
[⭐开机能源之星自动优化         已启用]
[🧠开机清理一次内存            已启用]
[🎵开机启动音质优化            未启动]
[🎮开机启动手柄模拟鼠标        未启用]
[AMD395专门修复(精简版3DMark)  需3DMark]
[🔍Windows 任务栏搜索          已精简[点击恢复]]
```
- 电源按钮 = `powercfg /setacvalueindex ... 3fe58c63-...` 电源按钮动作（不操作/睡眠/休眠/关机）。
- 休眠开关 = `powercfg /hibernate on|off`；百分比 = `powercfg /hibernate /size N`；实时读 hiberfil.sys 占用。
- 桌面模式 / Xbox 模式 = 任务计划 `桌面模式-开机设置为桌面模式` / `Xbox大屏游戏模式`（互斥+旋转冲突检测：getRotationEnabled）。
- 能源之星 = `EnergyStar.vbs` + 任务计划 `节能-能源之星`；清理内存 = `MG-AUTO\清理内存.bat`(memreduct) + 任务计划 `内存-开机自动内存清理并关闭`；手柄模拟鼠标 = `JoyXoff.bat` 切换；AMD395 修复 = 任务计划 `Bug修复-AMD-395`（需检测 3DMark 存在）。
- **Windows 任务栏搜索**：检测 `C:\Windows\SystemApps\MicrosoftWindows.Client.CBS_cw5n1h2txyewy\SearchHost.exe` 是否存在判断「精简/完整」；点击打开 `C:\SOFT\精简掉的系统文件\SearchHost` 由用户手动操作（bat 无权限自动执行）。

### 页面 5：Steam 大屏 `/steam`
- 沿用旧 tab-steam 全量：Steam 路径检测、大屏模式启动（`YeManSteam.bat`）、Steam 高级开机启动（`.earlystart` 文件）+ 5 个联动启动项开关（读 `YeManSteam\*.txt` 状态）：
  - 🎨 Steam 美化（CSSLoader）
  - 🌐 网络加速（steamcommunity）
  - 🦆 小黄鸭缩放插帧（Lossless Scaling）
  - 🛠️ 游戏修改器合集
  - ⏩ 游戏变速器（OpenSpeedy）

### 页面 6：支持 `/support`
- 内嵌渲染 `YeMan-Support.html` 内容 + 「野蛮系统更新主页」外链（openYeManHome）+ 版本信息 + PawnIO 驱动状态自检（pawnio_present）。

---

## 三、技术架构

```
┌──────────────────── YeManCC/ ───────────────────────────────┐
│ native/            强强壳原版 (main.cpp / app.rc / app.ico)  │
│ src/               Vue3 前端 (Vite)                         │
│  ├─ main.ts / App.vue                                      │
│  ├─ router.ts      6 个页面路由                            │
│  ├─ stores/        Pinia: power / tdp / cpu / rtss / nav   │
│  ├─ gamepad/       手柄导航引擎(见 §五)                    │
│  ├─ components/    Slider / Toggle / SegButton / StateCard │
│  │                / WarnBar / NavRail / TitleBar           │
│  └─ bridge/        对强强 90 个原生 API 的 TS 封装         │
│ backend/           Node 侧桥 (经强强 shell.run 调用)       │
│  └─ bridge/yeman.ts  所有 bat/vbs/ps1/powercfg/任务计划封装  │
│ app.config.json    420x760 frameless 单实例 深色            │
└──────────────────────────────────────────────────────────────┘
            │ shell.run / fs 读写
            ▼
C:\SOFT\YeMan\PowerControl\   ← 全部旧资产原地保留
  ├─ pawnio\YeManTdpCtl.exe   ← TDP 唯一执行体 (PawnIO)
  ├─ *.txt (tdp-ac/tdp-dc/FPS-ac/FPS-dc/Power)  ← 配置真相源
  ├─ *.bat / *.vbs / *.ps1    ← 执行体
  └─ *.xml                    ← 12 个任务计划模板, schtasks /Create /XML
```

### 关键决策
1. **配置真相源仍是 txt**：UI 读取/展示/修改 `tdp-ac.txt` 等，bat 执行时自己再读——新旧两套 UI 可共存，随时回退 HTA。
2. **任务计划只识别状态、不解析内容**：UI 对每个任务计划只做三件事——`schtasks /Query /TN <名>` 判断是否存在（=开关状态）、用现成 XML `schtasks /Create /TN <名> /XML <file>` 创建、`schtasks /Delete /TN <名> /F` 删除。**不读取、不校验、不维护各 bat 内部的调用链**（任务计划指向哪个脚本是用户自己的配置，与 UI 无关）。
3. **强强 IPC**：前端 → `chrome.webview.postMessage` → 壳；需要跑 bat 时走 `shell.run`(静默、提权由 bat 自身 fltmc/net session 处理，沿用)。
4. **UI 只调桥，不拼命令**：所有 powercfg GUID、路径、提权逻辑收进 `backend/yeman.ts`，前端只调语义化方法如 `setTdp('ac', 300)`、`toggleTask('TDP-插电AC模式TDP调节', true)`。

---

## 四、设计规范（八大设计原则落地）

| 原则 | 落地方式 |
|---|---|
| **目的** | 首屏即 TDP——用户 80% 场景；每页只放该场景的事，不堆功能 |
| **能动性** | 手柄/鼠标/键盘三通道等价；所有操作可撤销（复位按钮 / 配置即文件可手改） |
| **责任** | 危险操作红色+二次确认（复位 RTSS、关休眠、重制电源）；写 MSR/SMU 前校验厂商 |
| **熟悉** | 布局沿用截图：左导航 + 卡片堆叠 + 蓝主色 + 圆角 12px，老用户零学习成本 |
| **灵活** | AC/DC 双档独立；滑块(粗调)+下拉档(精调)+txt(手改) 三层粒度 |
| **简洁** | 每屏 ≤6 张卡片；状态词统一「已启用/未启用/已开启/不操作」 |
| **匠心** | 焦点环 2px 呼吸动画；滑块拖动时实时瓦数气泡；AC 蓝 / DC 琥珀色语义 |
| **愉悦** | 操作成功轻音效（沿用 YeMan-on.wav）+ 卡片短暂辉光；页面切换 180ms 滑动 |

### 视觉令牌（沿用并规范化截图）
```
--bg:        #0b0e13   全局深色底
--bg-panel:  #121722   卡片底
--bg-input:  #1a2230   滑轨/输入底
--text:      #e9eef5   主文字
--text-dim:  #8b96a8   次文字
--accent:    #2ea6ff   主蓝 (AC/选中/焦点)
--accent-2:  #f5b93d   琥珀 (DC 语义, 仅点缀)
--danger:    #e5484d   红 (复位/退出)
--ok:        #3fb950   绿 (已开启状态点)
--radius:    12px 卡片 / 8px 控件
--focus-ring: 0 0 0 2px var(--accent), 0 0 12px rgba(46,166,255,.5)
```

---

## 五、手柄支持方案（硬需求）

**轮询引擎**：`navigator.getGamepads()` 60Hz rAF 轮询（WebView2 支持良好），不依赖 gamepadconnected 事件（手柄可能先于页面插入）。

| 输入 | 行为 |
|---|---|
| **LB / RB** | 6 页循环切换 |
| **十字键 / 左摇杆** | 焦点移动（空间导航：按 DOM 几何位置找最近可聚焦元素） |
| **A** | 确认 / 开关切换 / 按钮点击 |
| **B** | 返回 / 关闭下拉 / 取消确认框 |
| **←→（按住重复 200ms）** | 焦点在滑块上时 ±1 步进微调；±10 用 LT/RT |
| **Y** | 当前滑块「应用」；X 打开该卡片下拉档 |
| **Start** | 打开/关闭调试面板 |

工程要点：
- 焦点管理器：所有可交互元素注册进 `navStore`，带 `data-nav` 几何快照；高亮 = `--focus-ring`。
- 边沿检测：记录上一帧按键位掩码，只在 0→1 跳变触发。
- 鼠标/键盘事件与手柄走**同一个 action 分发层**，三者永不打架（手柄输入时隐藏光标，鼠标移动即恢复）。
- 无手柄时引擎静默，不影响纯鼠标用户。
- 调试面板显示手柄连接状态、按键日志、最近 20 条 action——自测不依赖用户。

---

## 六、TDP / 驱动层（PawnIO 方案，已验证）

TDP 执行体 = `C:\SOFT\YeMan\PowerControl\pawnio\YeManTdpCtl.exe`（bat 直接调用，沿用现状，不替换）：
- `YeManTdpCtl.exe set <W>` — AMD: W×1000mW 写 STAPM/PPT-fast/PPT-slow（RyzenSMU.bin, MP1 邮箱 0x3B10530）；Intel: 写 MSR 0x610 PL1=W/PL2=W+5（IntelMSR.bin）。
- 缺驱动时 exe 自身弹窗 + 静默跑 `PawnIO_setup.exe -silent -install`，装后自检。
- 前端只加一层：调用前 `pawnio_present` 预检，状态页展示驱动健康；不在 UI 层重复实现安装逻辑。
- PawnIO 官方资产参考 `C:\Program Files\Newko\assets\pawnio`（PawnIO_setup.exe 与 YeMan 副本字节一致；其 RyzenSMU.bin 为新版签名构建，接口相同——后续如需升级可直接拿官方 bin 替换）。

---

## 七、任务计划清单（12 个，全部保留）

| 任务名 | 触发 | 调用资产 | UI 开关位置 |
|---|---|---|---|
| TDP-开机启动野蛮快设TDP挡位 | 开机 | AUTOPlan.bat(vbs 静默) | TDP 页·开机启动 |
| TDP-插电AC模式TDP调节 | 电源事件 AC | Plan-AC.bat | TDP 页·插电恢复 |
| TDP-离电DC模式TDP调节 | 电源事件 DC | Plan-DC.bat | TDP 页·离电恢复 |
| 唤醒后-执行任务 | 唤醒 | YeManWake.bat | TDP 页·唤醒恢复 |
| 锁帧-插电AC模式锁帧 | 电源事件 AC | RTSS-FPS-AC.bat | RTSS 页·插电恢复锁帧 |
| 锁帧-离电DC模式锁帧 | 电源事件 DC | RTSS-FPS-DC.bat | RTSS 页·DC 锁帧任务 |
| 监控-开机启动监控锁帧软件RTSS | 开机 | YeManRTSS.bat | RTSS 页·开机启动 |
| Xbox大屏游戏模式 | 开机 | YeManSteam.bat | 电源页·Xbox 模式 |
| 桌面模式-开机设置为桌面模式 | 开机 | (内置) | 电源页·桌面模式 |
| 节能-能源之星 | 开机 | EnergyStar.vbs | 电源页·能源之星 |
| 内存-开机自动内存清理并关闭 | 开机 | MG-AUTO\清理内存.bat | 电源页·清理内存 |
| Bug修复-AMD-395 | 开机 | C:\SOFT\3DMark\YeMan-3DMark.bat | 电源页·AMD395 修复 |

> 注意（2026-07-20 用户已改）：所有 vbs 中原有的「kopanel.exe 运行中则退出」互斥检测**已全部移除**，新 UI 与任务计划直接执行 bat，不再检测、不再因 Newko(kopanel.exe) 在运行而退出。两个软件可同时工作，TDP 以最后写入者为准。

---

## 八、实施里程碑（每步端到端自测通过才进下一步）

| # | 里程碑 | 验收标准 | 状态 |
|---|---|---|---|
| M1 | 强强壳跑通 Vue3 骨架 | 6 页路由可切、深色主题、窗口拖动/缩放/单实例 | ✓（2026-07-20 本轮：从源码实编译 exe 跑通）|
| M2 | 桥层 yeman.ts + Debug 面板 | 每个 API 原始返回可见；txt 读写、schtasks 查询全通 | ✓（2026-07-20 本轮：15/15 自测通过）|
| M3 | **TDP 页** | 滑块→写 txt→YeManTdpCtl set 实测生效(verify)；4 个任务计划开关双向同步 | ✓（2026-07-20 本轮：真实 AC/DC 检测 + TDP 联动 tdp-ac/dc/{mode}.txt 写入 + 4 任务开关）|
| M4 | **CPU 调度页** | 主频/积极性 powercfg 写入+读回一致；大小核 3/8-Core 生效；六档重制电源可用 | ✓（2026-07-20 本轮：方案切换/全量 setac/dcvalueindex/大小核/六档复位全接 yeman.ts）|
| M5 | RTSS 页 | 锁帧写 Global profile+reload 生效；横竖版切换；复位可回滚 | ✓（2026-07-20 本轮：Limit= 改写+reload/横竖版/AC恢复+DC锁帧+开机监控+复位）|
| M6 | 电源/开机启动页 | 休眠开关+百分比、启动模式互斥+旋转冲突警告、6 个启动项开关（含 Windows 任务栏搜索精简/恢复） | ✓（2026-07-20 本轮：休眠±%/电源按钮队列/桌面↔Xbox 互斥/6 启动项+搜索精简）|
| M7 | Steam 页 + 支持页 | 沿用 HTA 逻辑移植；PawnIO 自检 | ✓（2026-07-20 本轮：.earlystart 主开关 + 5 联动项 + PawnIO 自检状态页）|
| M8 | **手柄全量导航** | 仅用手柄完成：切 6 页、改 AC TDP、改锁帧、开关节能、复位确认 | ✓（2026-07-20 本轮：engine.ts rAF 轮询+边沿+空间焦点+滑块微调+Start 调试面板）|
| M9 | 打包交付 | `bun run build:single` 单 exe ≤1.5MB；与 HTA 双开不冲突；配置互通 | ⬜ |

> **M1 完成备注（2026-07-20 本轮）** — 壳方案：从强强 QiangQiang **真·源码** `native/main.cpp`（C++ Win32 + WebView2 Composition）**独立重编译**为 `dist/YeManCC.exe`。**不使用 koPanel.exe 复制**（上轮失败根因：复制 Newko 预编译 exe 身份绑死、无法独立渲染）。工具链：VS Build Tools 2022 `cl.exe` 19.44 / Windows SDK 10.0.26100.0 / WebView2 SDK 1.0.3800.47（静态 `WebView2LoaderStatic.lib`）。`/MT` 静态 CRT → 单文件仅依赖系统 DLL，零 VC++ Redist 依赖。
> 产物：`dist/YeManCC.exe`（x64 PE32+, subsystem=Windows GUI, ~727KB，含 `native/app.rc` 图标 + 单例互斥体 `QQ_YeManCC`）。`app.config.json`（title=YeManCC, 420×760 frameless/resizable/singleInstance/dark）已复制到 `dist/`。
> 前端：`npm run build` 78 模块通过；`vue-tsc` 类型检查通过；6 路由 + 深色主题 + 组件齐全；dist 相对路径 `./assets` 兼容壳 `https://app.localhost` 虚拟主机（`SetVirtualHostNameToFolderMapping`）。
> 验证：本机已装 WebView2 运行时（Edge 150.x）；守护式启动测试进程存活 8s 不崩溃（壳可启动、WebView2 可初始化）。窗口拖动=TitleBar `app-region: drag`；缩放=无边框 `resizable`；单例=`singleInstance`（与 Newko 完全独立）。M2 接力 `src/bridge/`。

**硬规则（沿用用户此前多次强调）**：每个里程碑先脚本化端到端自测（Node 直调桥层验证数据往返），**禁止"编译→让用户测→再修"的循环**。

> **M2 完成备注（2026-07-20 本轮）** — 交付 `src/bridge/ipc.ts`（信封：请求 `{id,cmd,args}` 单对象；WebMessage 事件分发 `{event,data}`；`isNativeRuntime`；`setLogSink` 桥日志注入）+ `src/bridge/api.ts`（语义化封装 fs/shell/app/os/dialog/window/registry，**命令名逐行对齐 main.cpp 90+ 注册**）+ `src/bridge/yeman.ts`（txt 读写 / 12 任务计划 schtasks query·create·delete·toggle / 厂商识别 / pawnio 自检 / setTdp / FPS / powercfg 通用封装）+ `src/components/DebugView.vue`（4 页签：桥日志实时 / txt 读写测试 / schtasks 测试 / 手柄动作）+ `src/stores/debug.ts`（桥日志总线，main.ts init 订阅）。
> 关键契约：前端 `window.chrome.webview.postMessage({id,cmd,args})` 传**对象**；响应 `{id,result|error}`；事件 `{event,data}`。`shell.run` 收 `{program,args[]}` 返回 `{exitCode,stdout,stderr}`（可解析 schtasks）；`fs.readTextFile`/`fs.writeTextFile`/`fs.exists`、`app.exit`、`shell.open`/`execute` 均对齐 main.cpp。
> **M2 自测（硬规则）**：`tools/mock-shell.ts`（Node 复刻 chrome.webview 信封，fs 真实磁盘、shell.run=`spawnSync` 真跑 schtasks/powercfg）+ `tools/m2_selftest.ts` → `npm run test:m2` **15/15 通过**（txt 往返 4 / 厂商 2 / pawnio 1 / schtasks 真实 3 / powercfg 2 / createTask 缺模板·缺文件抛错 2 / TASKS=12 全 1）。`npm run build`(78 模块) 与 `vue-tsc` 类型检查均通过。
> **M2 未验证（沙箱限制）**：真机在 shell 内（chrome.webview 存在）的实时桥调用与 Debug 面板表现需真机双击 `dist/YeManCC.exe` 确认；M3 起各页接 yeman.ts 时自然验证。
> **M3 接力**：TDP 页（`src/views/TdpView.vue`）调用 `setTdp/readTdp/toggleTask('TDP-插电AC模式TDP调节'等)`；当前电源模式（AC/DC）由前端判定后决定调 `setTdp`（写硬件）还是 `saveTdp`（仅存档）——PLAN §二 TDP 页已明确。

> **M3–M8 完成备注（2026-07-20 本轮 · 次轮）** — 6 页全部从 `YeManUI.hta` 功能逐项对齐并接线 `yeman.ts`：
> - **TDP**：真实 `detectPowerMode()`（WMI BatteryStatus）+ `detectCpuName()` + `detectVendor()`；TDP 上限档 `[20,35,55,75,120,300]`；`setTdp(mode,v,{apply,vendor})` 始终写 `tdp-{mode}.txt`、仅当 mode==当前电源且厂商已知时调 `YeManTdpCtl.exe set <W> --vendor`，并联动 `tdp-ac.txt/tdp-dc.txt`；4 个任务计划开关（AC/DC 恢复、开机 TDP+电源预设、唤醒后恢复）双向同步（reactive `tasks` + `toggleTaskSafe`）。
> - **CPU**：`switchScheme`（yeman/bal/besteff/bestperf）、`applyPowerParams`（全量 setac/dcvalueindex + 节流1/2 + 积极性100-值）、`setCoreMode`（big/only-big/only-small）、`readPowerParams` 回读、六档重制电源 `runResetProfile`。
> - **RTSS**：`readRtssLimit/setRtssLimit`（`Limit=` 改写 + `rundll32 RTSSHooks64.dll` reload）、横/竖版 `OverlayEditor.cfg` 切换、AC 恢复锁帧 / DC 离电锁帧任务 / 开机启动 RTSS 监控、`resetAll` 可回滚。
> - **电源/开机**：休眠开关±百分比、电源按钮队列（S3/S4/不操作，休眠关时跳过 S4）、桌面↔Xbox 模式互斥 + 旋转冲突 `WarnBar`、`fxSet`(FxSound)/`joySet`(Joyxoff)/`amd395`(3DMark 预检) + 节能/清理内存 + Windows 任务栏搜索精简/恢复。
> - **Steam**：`.earlystart` 主开关 + 5 个联动启动项（读 `YeManSteam\*.txt` 状态）+ 运行态检测 + 大屏模式 `shell.execute`；**支持页** `checkPawnio()` 真实驱动自检。
> - **手柄**：`src/gamepad/engine.ts` rAF 60Hz 轮询 + 边沿检测；LB/RB 切 6 页、A 确认/B 模糊、Y 应用滑块、X 下拉、←→ 滑块 ±1（LT/RT ±10 长按重复）、dpad/左摇杆空间焦点导航（220ms 冷却）、Start 派发 `ipc:gamepad-start` 开调试面板；`App.vue` 挂载/卸载生命周期接管。
> **契约修正（次轮）**：`setAcValueIndex/setDcValueIndex/setHibernate` 改为返回 `RunResult`（`{exitCode,stdout,stderr}`），与 `npm run test:m2` 自测契约一致。`npm run type-check` 0 错；`npm run build` 79 模块；`npm run test:m2` **15/15 通过**（含 powercfg RunResult 2 项）。
> **重编译**：因 `vite build` 默认清空 `dist/` 会抹掉 `YeManCC.exe`，重编译后已将 `app.config.json` 重新拷回 `dist/`。`dist/YeManCC.exe` 由 `native/main.cpp` 经 `cl.exe 19.44 /MT /std:c++20` + `WebView2LoaderStatic.lib` 静态链接产出（~799KB，零 VC++ Redist）。部署单元 = `YeManCC.exe` + `app.config.json` + `index.html` + `assets/`。
> **未验证（沙箱限制）**：真机双击 `dist/YeManCC.exe` 的实时桥调用、手柄实连操作、TDP 硬件下发需用户在真机确认；逻辑层已由 15/15 自测 + 类型检查覆盖。

> **WebView2 崩溃修复（2026-07-21 凌晨）** — 用户报告双击 exe 弹 "WebView2进程已崩溃，应用将关闭"。
> - **根因**：`%LocalAppData%\YeManCC\EBWebView\` 用户数据目录损坏（含 2 个 Crashpad .dmp 转储共 ~23MB），WebView2 启动时加载脏数据导致浏览器进程立即崩溃，形成"崩→脏数据→再崩"恶性循环。Edge 150 运行时本身正常，前端 JS 无运行时错误。
> - **修复**：① 删除 EBWebView 目录后 exe 正常启动（22.5MB 内存，零新转储）；② 在 `native/main.cpp` 的 `init_webview()` 中增加**自动恢复机制**：启动时检测 `EBWebView/Crashpad/reports/*.dmp`，若存在则自动清除整个 EBWebView 再初始化 WebView2 环境。EBWebView 为纯缓存目录（所有持久状态在 PowerControl txt/xml），清除安全无副作用。重编译 784KB 验证通过。

> **第三轮 Bug 修复（2026-07-21 凌晨 01:30）** — 用户真机测试反馈大量问题，逐一修复：
>
> ### 🔴 关键修复
> 1. **TDP/RTSS/Power 页 Toggle 卡死根因**：`toggleTaskSafe()` catch 块中调用 `dialog.message()` 显示原生对话框——**WebView2 原生对话框阻塞渲染线程**，导致 `busy=true` 后 UI 永久冻结（所有按钮变灰）。**修复**：移除所有 `dialog.message()` 调用，改为 `errMsg` 非阻塞内联提示。涉及 TdpView/RtssView/PowerView 三个文件。
> 2. **CPU 页每次打开都关闭睿频**：默认值 `acTurbo = ref(false)` + `readPowerParams()` 读注册表失败时静默保留默认 → 视觉上每次打开都是"睿频已关闭"。且 `BASE_MHZ=3400` 对用户 9950X 应为 **4300MHz**。**修复**：① 默认全部改为 `acTurbo/dcTurbo = ref(true)`（安全默认）；② 动态检测 CPU 型号设置基准频率（9950X→4300, 9900X→4200, 9700X→3900 等）；③ 写入核心原则：**打开/切页只读取状态，绝不执行任何操作**。
> 3. **WebView2 多轮崩溃防护**：① `ProcessFailed` 回调中增加自动清除 EBWebView 脏数据；② 新增浏览器启动参数 `--disable-gpu-sandbox --disable-software-rasterizer --disable-extensions --no-default-browser-check` 等提升稳定性。
>
> ### 🟡 中等修复
> 4. **电源按钮 "Cannot open registry key"**：HKLM 电源方案注册表需管理员权限。错误信息增加"需要管理员权限"提示。
> 5. **休眠开关默认 OFF**：`isHibernateOff()` 异常时原来默认 `hibernateOn=false`（强制关闭）。**修复**：异常时设为 `null`（未知状态），UI 显示"检测中…"且禁用 Toggle。
> 6. **Steam 检测改进**：新增 `detectSteamPath()` 通过注册表+多路径探测找到实际 Steam 安装目录（不再硬编码单一路径）。
>
> ### 🟢 UI 优化
> 7. **TDP 页去掉多余 AC 标签**：`modeLabel` 已显示"当前电源为 AC 模式"，右侧 `pm-chip`（🔌 AC）冗余，已移除。
> 8. **CPU 页去掉"方案"标签**：第一排已有 SegButton 方案切换器，左侧"方案: 野蛮系统电源"文字冗余，已移除。
>
> **验证结果**：vue-tsc 0 错 / vite build 79 模块 / test:m2 **15/15** / cl.exe 编译 786KB / exe 5 秒稳定运行。

> **第四轮 Bug 修复（2026-07-21 凌晨 02:15）** — 用户真机测试反馈第二轮仍有问题，逐一深度修复：
>
> ### 🔴 关键修复
> 1. **Slider 滑块左侧无颜色填充**：`input[type='range']` 的 `background: var(--bg-input)` 覆盖了 `.slider-fill`（z-index 冲突）。**修复**：① input 背景改为 `transparent`；② 新增 `.slider-track-bg` 底层轨道（z-index:-1）显示未填充部分；③ fill 层 z-index:0 显示已填充彩色部分。三层叠加实现正确的滑块视觉效果。
> 2. **RTSS 页切页后 RTSS 消失**：`rtssRunning()` 要求 RTSS.exe **和** HWiNFO64.exe 同时运行才返回 true，用户只开 RTSS 时被误判为"未启动"。**修复**：改为仅检测 `RTSS.exe` 进程。
> 3. **RTSS FPS 滑块过长**：原 `max=200, step=5` 对掌机不友好（60帧足够用）。**修复**：改为 `max=240, step=1` + 快速设限档位 `[30,48,60,90,120,144,240]FPS`（含常见掌机/显示器刷新率）。
> 4. **电源按钮 "Cannot open registry key"**：直接写 HKLM 注册表需管理员权限且易出错。**修复**：改用 `powercfg /query` 和 `powercfg /setacvalueindex|setdcvalueindex` 命令（与 CPU 调度同路径，更可靠）。
> 5. **休眠状态无法真实检测**：原 `powercfg /a` 经 cmd 中转可能丢失输出。**修复**：三级降级检测 → powercfg /a → 注册表 HiberFileSizePercent(0=关) → C:\hiberfil.sys 存在性。
>
> ### 🟡 中等修复
> 6. **任务计划检测加强**：`taskExists()` 增加 CSV 输出降级匹配（exitCode 判断失败时搜索 stdout 是否包含任务名）。
> 7. **手柄导航逻辑清理**：重写 engine.ts，明确注释按键映射（Xbox LB/RB=L1/RB 切页）；改进 `focusables()` 过滤（检查可见性+尺寸）；对角线方向优先水平导航；滑块微调不再自动 commit change（需 Y 键确认）。
>
> ### 验证
> - vue-tsc: 0 错 ✓ | vite build: 79 模块 ✓ | test:m2: **15/15** ✓
> - cl.exe 编译: 786KB ✓ | **多轮稳定性测试**: Round 1/2/3 全部稳定 (22.1MB, 0 dumps) ✓

---

## 九、风险与边界

1. **强强壳的 Vue3 适配**：官方示例是原生 TS；Vite+Vue3 需自建 `dev.command/build.command`（M1 内解决，风险低——WebView2 对框架无感）。
2. **提权**：bat 内已有 fltmc/net session 自提权；强强 `shell.run` 需验证能否触发 UAC 弹窗链路，不能则前端改为调 wscript 静默 vbs 入口（现成）。
3. **WebView2 手柄焦点 vs 页面滚动**：左摇杆默认会滚动页面，需 `overflow` 管控 + 焦点模式切换。
4. **与 Newko 共存**：不做进程互斥（双方均可调 TDP，后写生效）；不注册相同 URL 协议/热键；不修改 Newko 任何文件。
5. **旧文件一律不动**：HTA 及其副本、PowerControl 全部内容只读调用；新文件**只写 YeManCC 目录**（唯一例外：UI 按用户操作更新 PowerControl 的 5 个 txt 配置——这是既有契约）。
