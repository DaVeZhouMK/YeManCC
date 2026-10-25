# Steam Deck Tools 触摸数据链对照 · R19 · 2026-10-16

用户确认直接 Mouse 模式正常，虚拟 Steam Deck 经 Steam 合成鼠标仍闪烁。本轮据此沿数据/输入所有权继续调查，不改显示策略。结论是：Steam Deck Tools（SDT）可以作为触点语义和鼠标输出实现的对照，但它没有提供一条可直接替换 YMCC 的“虚拟 Deck → Steam 合成鼠标”生产链。静态触点字段一致，不证明现场数据连续；本轮没有确认闪烁根因，也没有修改输入行为。

官方仓库通过 GitHub API 固定到 `deee7fa887c2864ee3b54eba2c32b2f58b547349`，提交时间 2025-06-10T15:56:10Z，查询于 2026-10-16。这是本次所见默认分支 HEAD，不能等同于最新发行版、所有 fork 或用户实际使用版本。完整源码字节、URL 和 SHA-256 见证据目录 `SteamDeckTools/verified-source-index.json`。仅下载源码和一个 11264B 的 HID 托管封装进行静态反编译；未启动 SDT 或其 HID 库，未打开设备、修改 Steam 配置或发送输入。

证据目录：`G:/YeManCC-Work/Build/Analysis/Touchpad-Data-R19-20261016`。主线 Steam audit：13 个区域、395 个候选、0 缺失锚点、0 未归类；这是索引检查。

## 实际路径

| 路径 | 输入与处理 | 输出 | 对本问题的证明范围 |
|---|---|---|---|
| SDT Desktop（正常安全上下文，默认 debug 配置） | 实体 `28DE:1205` 的 64B HID 输入，右 pad 的 touch 位决定是否处理 XY，按坐标差计算鼠标位移 | `H.InputSimulator` 的 `Mouse.MoveMouseBy`，整数余量累计，零位移不发送 | 接近用户已经确认正常的直接 Mouse 路径；没有复现虚拟 SD 和 Steam 的中间消费 |
| SDT Steam Input | 检测 Steam 使用 Steam Input 后选择 SteamProfile，关闭实体固件的 lizard 鼠标/按键，保留实体 Deck 供 Steam 消费 | 本路径没有把 screen pad 编成虚拟 SD raw report；具体快捷键另见 Profile 继承链审阅 | 可以借鉴所有权协调；实体固件的连续数据与 YMCC 虚拟生产不能混为一谈 |
| YMCC Deck | 屏幕原生触点 → BUS 快照 → Host 严格解析 → 64B raw SD → SDK/USB-IP → Steam | Steam 根据当时布局产生鼠标 | 当前故障路径；SDT Desktop 的正常不能证明这一整条链正确 |

准确入口：`SteamDeckTools/Sources/SteamController/Devices/SteamController.cs:43-48,69-92`、`Context.cs:116-143`、`Profiles/Predefined/DesktopProfile.cs:35-59`、`Profiles/Predefined/SteamProfile.cs:25-39`、`Profiles/Default/GuideShortcutsProfile.cs:164-183`、`Devices/MouseController.cs:159-168`。`SteamController.csproj:32` 固定 H.InputSimulator 1.3.0。

## 数据逐项比较

机器提取结果在 `static-pad-source-comparison.json`；这是读取声明和调用参数得到的静态对照，没有执行编码器或运行时测试。

| 字段 | SDT 实际声明/处理 | YMCC raw 编码 | 裁决 |
|---|---|---|---|
| 左/右接触 | byte `0x0A` masks `0x08/0x10`，即按钮域 bit19/20 | bit19/20 | 一致 |
| 左/右下压 | 同 byte masks `0x02/0x04`，即 bit17/18 | bit17/18 | 一致；不能用 pressure 代替 touch |
| 左 XY | `0x10/0x12`，signed Int16 | 16/18，Int16 LE | 偏移和有符号含义一致 |
| 右 XY | `0x14/0x16`，signed Int16 | 20/22，Int16 LE | 偏移和有符号含义一致 |
| 手指是否有效 | `SteamAxis.Active = ActiveButton.Value` | ScreenPad.Active 独立传入 touch 位 | SDT 并不要求 pressure>0 才认定接触 |
| 重新落指 | `ActiveButton.JustPressed()` 时令 `rawLastValue = newValue` | 直接 Mouse DOWN 重置 last/start；Deck 仍输出真实绝对 XY，交由消费方解释 | 借鉴的是“新接触重新建立基准”，不能把 SD 原始第一包强制归零或改为 delta |
| 点击与滑动 | 实体 click 位用于鼠标按钮，XY 由 touch 位门控 | screen pad 的点击为已有软件轻点语义；轻触压力0、点击32767 | SDT 消费实体压力/下压；不能从其读取器推导 YMCC 应发送某个固定轻触压力 |
| 左/右压力地址 | SteamButtons 的两个轴都写 `0x38`；其自身 SDCInput 结构右压力却是 `0x3A` | 56/58，即 `0x38/0x3A` | SDT 右轴声明存在内部不一致；保持 YMCC 的左右分离，不复制这个差异 |

SDT 来源：`ExternalHelpers/SDStructs.cs:70-77,116-137`、`Devices/SteamButtons.cs:43-67`、`Devices/SteamAction.cs:296-298,341-343,364-384`。YMCC 来源：`InputHost/ScreenTouchpadReport.cs:96-135`、`native/screen_touchpads.h:287-351,355-378`。各文件当前哈希见 `mainline-source-boundary-start.json`。原压力偏移的 Valve/SDL 交叉依据沿用 R17/R18 固定来源，不把 SDT 声明错误当协议规范。

SDT 的 `GetDeltaValue(..., Delta, 10)` 中 `10` 传给 `CheckDeadzone`，检查的是当前 Value 与关联轴的绝对坐标半径，不能直接解释为“每次位移小于10就过滤”。它不是一个可照搬到 SD raw 包的时域平滑器。最终的 `Accum.Consume` 只保留不足一像素的小数余量，零整数位移不发送；这属于本地鼠标转换，不解决 Steam 对虚拟 HID 的消费问题。

## 读取封装还暴露一个不能照抄的行为

固定 commit 随附 `ExternalHelpers/hidapi.net.dll` 的 SHA-256 为 `98663a94f660757a50de598eb526270548f0a67f4b9f2dcb8a2fea0b5353867e`，静态反编译为 `SteamDeckTools/HidapiMetadata/hidapi/HidDevice.cs`。

`Read(int timeout)`（77-84行）每次新建零初始化输入数组，调用 `hid_read_timeout` 后忽略返回长度并直接返回数组。SDT `SteamController.BeforeUpdate`（82-90行）仅判断是否 null，因此该封装在 native read 返回0且未写缓冲的超时情形下可把零数组继续交给按键/轴解析；它没有通过此重载证明“没有新包时保持上次真实触点”。另一个 `Read(byte[],int)` 重载（93-102行）保留返回长度，但该 Desktop 主路径没有调用它。

这是静态可达行为，不代表 SDT 在用户机器上确实超时，更不是 YMCC 本次故障的归因；YMCC 的本链路没有使用该 SDT HID 封装。该发现说明，不能把 SDT 整个类当作正确性参考实现。

## Lizard / 模式所有权

SDT `SteamControllerLizard.cs:9-69` 会在状态变化或约250ms后发送 feature 配置。这不是触摸采样/鼠标刷新率，也不是应该给 YMCC 新增的 timer。正常 Desktop 默认 debug 两项均 false（`SettingsDebug.cs:16-27`）；安全上下文失败时才把两项均置 true（`DesktopProfile.cs:43-52`）。不能仅按旧 issue 文字假定当前默认是 Mouse=true/Buttons=false。

上游 issue #215（2025-07-09）报告实体 Deck 固件中混用 lizard 模式导致约250ms卡顿，PR #216（2025-08-01，检索时仍未合并）提出一起配置左右 pad mode、点击压力与 watchdog。原帖/评论与 patch 保留在 `SteamDeckTools/Issues`，它们是原作者/报告者的一手陈述和候选补丁，并非已证明适用于所有固件的机制。

YMCC 固定 SDK 的 0x81/0x82 在 R18 已核查为无鼠标/键盘数据生产分支；SD raw 走 0x83，触摸期间已停止旧 SDK idle writer。不能为了对齐实体固件设置而反复给虚拟设备发送 lizard 禁用命令，也不能据 issue 直接禁用其它按键或吞掉真实 UP。所有权与配置的独立审阅见同证据根 `SteamActivityReview/`，该目录由旁路研究补充。

## 新日志与当前可证明范围

额外只读核对工作区 14:34:54 归档，SHA-256 `d8d1a64ab506ce8210c159037167a29079dd7ef069a3e41c4598ae59fccecc3e`，3216410B。文件名不作为触摸板成功或失败判断。`new-archive-review.json` 保存提取条目哈希与计算。

其中仍是 cursor policy16；132 个成本窗口覆盖14:30:22.918至14:34:44.308，139份可用 SDK snapshot 只说明缓存长度64，没有 `LastSteamDeckPadReport` 或 `steamDeckPadSubmissions` 字节/边沿字段。保留的7条 cursor trace 中6条有效数据都为 flags2、位置固定，按导出 wall/tick 估算约14:29:47至14:29:58；这段不含可供对齐的移动/闪烁轨迹。不能把该包判成数据正常，也不能把它叫作新诊断已经运行。

前一份用户明确报告失败的13:46包仍是故障依据：助手隐藏时也出现0/1可见性变化，单改显示不能闭合原因。R18已经加入低成本的逐成功提交触点边沿与预期/实际报告字段对照；它能补上1Hz缓存看不到短促off/on的盲区，但成功提交API仍不等于USB/Steam收到。

本轮采用的推进方向是：把“触点何时出现/释放”“谁实际生产输入”“Steam此时使用什么模式”分开取证，再判定是否需要修正生产路径。保持原频率、真实绝对坐标与释放语义；不再用平滑/延迟释放掩盖可能的模式冲突。新取证优先复用已有边沿统计和现成日志，不能为了借鉴 SDT 增加每帧分配、250ms配置重发或额外高频工作线程。

本轮没有生产代码修改、没有新增运行时负担，没有运行设备/输入/游戏测试、构建、打包或部署。完整 SD 闪烁目标仍未验收；未据静态对照宣布已修复。


## 所有权审阅与虚拟 feature 路径的进一步收敛

旁路研究逐个核验27份 SDT 文件、38处调用点和 PR 文本，报告为 `SteamActivityReview/SDT-STEAM-OWNERSHIP-REVIEW.md`，其 `verification.json` 仅验证离线来源与链接，不是运行时测试。查到两个与“为什么 SDT 表现不同”直接相关的条件：

- Setup 按 VID/PID 写 Steam `controller_blacklist`；选择 X360/DS4 与选择 Steam Input 时方向相反。这里的 `28de/1205` 粒度不区分具体设备实例。不能把它复制到 YMCC 然后误屏蔽正在依赖的虚拟 SD。
- SDT 的 SteamConfigsManager 会处理 desktop/chord 与 empty 模板，保留 basicui/bigpicture 默认绑定；触发条件是 Steam 运行状态变化，并不是每次 profile 切换。“Lock”实质是文件覆盖/恢复，不是 Steam 已加载指定 layout 的回执。模板的 cursor show/hide=0 也是配置事实，不能据此反推 Steam native 光标阈值。

主线程随后继续追了固定 SDK 的 feature handler，避免只凭“无 mouse endpoint writer”就排除固件候选。准确源码、来源哈希与当前36份 Host C# 文本交叉引用结果在 `VirtualFeatureReview/source-and-xref.json`。这里 `0x81/0x82` 作为 interrupt endpoint 地址，与同数值的 feature 命令完全是两套概念。

`UsbipEmulatedDevice.HandleHidClassRequest` 的 SET_REPORT（722-735行）只保存最近 feature 消息/参数/payload，把它通过 `PublishOutput` 发布到输出共享内存，再回复请求。该分支没有清空 input report、重置触点、设置物理 lizard mode，或打开物理 HID。`PublishOutput`（369-396行）写的是 output 共享环和事件，不是给 Steam 的 input 队列。

继续跟到 `HMController.OutputPollLoop`（503-609行），该输出用于通知 `OutputReceived/OutputDecoded` 和可选 extended arm；当前查到的 Host 源码没有这些事件引用。固定 Deck profile 是 `alwaysArmed=true`，没有 `armOn` 或 `extendedOutputReport`。因此在本次固定 SDK/profile/Host 路径内，没有找到把这些配置命令转发为实体 Deck 固件副作用的分支；不能把 SDT issue 中的“物理固件每250ms reset”机制直接用于本案。这项判断不排除其它进程或其它故障机制，也不是实际 USB 请求观察。

还保留一个重要语义差别：虚拟后端的 feature 成功回复不表示它实现了实体固件相应的 mode/watchdog 功能。其 GET_REPORT(Feature) 来自 profile stub/echo 表，固定表只声明0x83、0xAE与0x87，未命中返回错误；这不等同于“Steam实际请求了未支持的命令”。没有请求/回复现场，不据此增加假回复或改 SDK。

借鉴 SDT 后，问题已经收窄为两个独立核验点：最终输入报告是否真的在一次滑动中产生过接触/点击边沿，以及 Steam 实际加载/切换的 binding/source 是否让同一触摸被另一种输入解释打断。SDT 的普通鼠标算法不能回答后者，静态字段一致也不能回答前者；这轮没有把任何一个候选冒充根因。
