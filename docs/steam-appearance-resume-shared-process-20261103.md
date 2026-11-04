> 2026-11-04 续审：[恢复链复查](steam-appearance-recovery-audit-20261104.md)补上了忙碌助手空耗预算、手动修改优先权和原电源代次回执等缺口。默认首次10秒，助手运行时暂停剩余时点，失败后从实际归还继续剩余间隔；不保证忙碌场景仍固定在绝对+30/+60秒执行。旧记录与失败证据保留。

# Steam 外观：睡眠补应用与统一进程检测（2026-11-03）

## 日志事实

本轮读取 G:/YeManCC-Work/日志/ROG-Xbox-2025 中 20261103-154729、165526、185243 三包。它们包含重叠轮转日志，不能把同一条历史记录重复计数。输入 SHA256、相关原日志与提取时间线保存在 Testing/YMCC/Evidence/SteamAppearanceResumeSharedProcess-20261103。

设备日志 2026-11-03T16:37:17.277 曾记录 appearance-applied-resident；16:40:43.613 电源 Ready 后 CSS 请求被接收，但在同毫秒记录 resume-mode-refresh-unavailable，未进入新确认。15:12、15:17 与 18:18 的几次 Ready 也出现此中断。旧实现一次消耗 Ready 后即停，后来的同一模式/文档被正常去重，没有再补应用。日志能证明恢复链路中断，不能单凭后台存在或这些字段证明真实画面样式已丢、CSS 崩溃，或确定 bool 回调的哪一子条件失败。

## 源码确认与反向复现

发现独立的确定性缺陷：guard.observeSteam 原来只按 PID/birth/root 去重，账号从未知变为已知时 desired_ 仍保留旧 account；共享 ModeObserver 的 snapshot/requestRefresh 却使用完整 Session 相等。因此同一正常 Steam 实例会永远无法通过后续刷新。修复为保留完整会话、同进程同步 account 元数据，但不重置启动/崩溃/恢复预算。原 resume 消费者恢复旧去重条件后，实际 worker 在原命名断言复现刷新失败；新条件通过。由于旧日志缺少具体子原因，不能断言档案每次 bool=false 全由该缺陷造成。

睡眠前旧 helper 在 settle 延时内返回的成功也不能取消新一轮补查；只有刷新后当前 helper 的 DOM 确认可完成该轮。另保留过期时点后的真实文档变化入口，避免一次耗尽使后续界面重建也失效。

## 原 worker 的有限恢复

- 仅实际提交 Ready 且当前 Steam 实例和外观开关仍匹配时入队，约 Ready +10/+30/+60 秒最多三个检查时点。重复 generation 合并，不为每次通知创建进程或线程。
- 延时使用原 CSS guard 的 WaitForMultipleObjects；重试仍走原共享模式读取、原 CSS use 和 DOM 确认，不增加长期计时扫描。已有 helper 运行时不叠加；确认成功取消本轮剩余时点；失败耗尽后等待后续真实事件。
- 刷新暂不可用也保留后续时点，禁止拿睡眠前旧模式确认直接放行。外观 OFF、Steam 身份变化、退出、新电源代次、再次非 Ready 撤销本轮待执行检查。正常/未知后台退出的原 parked 保护仍保留，不借睡眠解除用户关闭或崩溃预算限制。
- 原有 Steam 大屏进入、文档替换、开关/资源更改、手动重新应用及已验证异常后台退出节点保留。不会因此启动/重启 Steam、切大屏/桌面或抢前台，不调用用户 BAT/VBS。
- 新日志区分 resume-use-worker-unavailable / observation-changed / active-session-changed；真实过滤器现在保留 CSS resume 的 Steam PID/birth，guard 也记录对应实例。不能再只看 error=0 判断样式已应用。

## 唯一进程采集所有者

已询问“优化YMCC后台CPU占用2号”，依据 Docs/Tasks/CpuOptimization/PROCESS-INVENTORY-CONSUMER-CONTRACT.md 接线。共享 owner 当前没有纯缓存 peek 或通用跨进程服务；read() 可能刷新超过 250ms 的缓存，read(...,0) 必定采集。本轮不修改 process_inventory.h，不把集中采集声称为零系统枚举。

- CSS backend/editor/use 的三处 Toolhelp 与共享 Steam observer 的两处私有 Toolhelp，改为 processInventory 的同一份拓扑来源；拓扑仅产生候选，沿原定向句柄验证身份。原 Steam registry/窗口事件、已知进程 HANDLE 和有限 retry 继续拥有时机。
- PowerShell 不再使用 Get-Process -Name、Win32_Process 或 CIM 查询所属用户/父子树。原 CSS use worker 响应受同用户 ACL 保护、随机名的映射/事件请求，只返回过滤后的 CSS 后台/编辑器候选与已观察的 Steam 身份。不创建额外常驻线程、轮询器或 PS 进程扫描服务。
- 多个操作在 250ms 内共用 owner 缓存。只有原事件或实际操作边界需要候选且缓存过期时由唯一 owner 按需集中采集；静置无请求不采集。保留端口监听者定向身份验证；TCP 监听表不是 EXE 拓扑来源。
- helper 只 Get-Process -Id，按 pid/birth 查询固定镜像、用户 SID 和同会话，父子关系通过同一进程句柄直接读取；PyInstaller 父子仍视为一家。自己创建的 PID 与固定端口 owner 可直接观察，不靠反复名字遍历等待子进程。
- 共享观察失败/权限不明是 Unknown，不能解释为空集来启动第二个后台；一次新查询失败先撤销旧 helper receipt。CSSLoader Desktop 存在或身份未知仍保护编辑器及后台，YMCC 退出不关闭其常驻 CSS。
- Native/入口脚本/resident 模块通过新 process-query 标记拒绝混装。前端每次脚本请求传受限 channel 和精确 Steam 身份；主题资源及预装 CSS 的布局、比例和动态壁纸均未变更。

## 验证范围

复用原 resume、use、process-identity、resident、frontend-use-service、Steam session observer 消费者，增加对应边界而非新建测试父门。真实 native worker 到 PS5.1 的请求使用隔离复制的无功能 EXE；共享 owner 和进程 token/path 查询为实际实现。重复 Ready、短暂失败续接、持续 Unknown 耗尽、旧实例/电源状态、OFF、编辑器、父子唯一性和前端 channel 参数均单独验证。日志过滤器通过实际 record() 验证。

具体本轮原始结果、源码哈希和验证状态见 Evidence 的 RESULT.json/REPORT.md。main 的 /Zs 为语法编译，不是可执行成品或部署。初次 observer 测试因证据输出子目录未创建而失败，原日志保留；创建该目录后使用同一已编译 EXE 运行原测试通过，没有修改业务断言。没有操作真实 Steam/CSS、安装版 YMCC 或物理睡眠；未测得整机 CPU 收益，未宣称所有未来掉样式问题已消失。

测试过程还保留一次扩展场景失败：新文档回调已经切到 next-document，而夹具 readMode 边界仍固定返回旧 same-document，从而制造出假文档往返。已让该同一边界随场景更新，保留原 FAIL 日志；没有删除/放宽“新文档只补一次”的断言。
