# Steam 外观：健壮性检查与修复记录

日期：2026-10-10（Asia/Hong_Kong）。范围为本次 Steam 外观卡片、typed service、PowerShell 控制脚本及自带预设模块，不覆盖其它并行开发功能。

## 结论与部署边界

已发现、隔离复现并修复多类数据保护和状态误报问题。修复进入源码、交付副本和可交互预览；本机 C:\SOFT\YeMan\PowerControl 的脚本同步命令被执行工具策略拒绝，未执行，因此安装目录仍是先前版本。本次未替换 YMCC EXE、未改启动 BAT、未对正在运行的 Steam/CSSLoader 执行重置或重载。

不要将“源码检查通过”理解为“本机已经部署了加固版”。源码与交付目录的新 helper/module 应随后续正常 YMCC 发布一起更新。

## 主要问题及处理

| 级别 | 原问题 / 边界 | 修复 |
|---|---|---|
| P1 | 接管记录损坏被静默当作首次接管，可能覆盖首次恢复指针 | 损坏、过大或非法状态拒绝新的写入，不擅自建立新基线 |
| P1 | 后续新增的第三方/同名主题被暂停后，原“恢复接管前”备份没有覆盖它们 | 不可变合并恢复基线，为每个路径保留第一次被 YMCC 接触前的字节；历史备份不原地改写 |
| P1 | 受管主题的 manifest 损坏/缺失时，installed=false 会连带把有效参数配置重置 | 独立读取并验证配置：有效开关与参数原字节保留；读取/共享错误拒绝自动覆盖 |
| P1 | 直接 Apply 可绕过目录链接防护；空目录清理会递归枚举链接 | 在事务开始前校验全部目标；清理逐层验证、跳过链接，仅移除安全空目录，不递归删除资源 |
| P1 | 准备/重置/恢复/批量开关在进程中途退出时，可能留下半套配置和不一致的 owner | 写入前保存事务标记及 owner 原字节；下次持锁读取先回滚未提交事务。恢复前另存当前文件镜像 |
| P1 | 同名后端或 Steam 进程可能属于其它用户/会话 | 验证 Windows SessionId 与进程所有者 SID，绑定 PID/启动时间缓存；身份无法核实则不发出控制操作 |
| P2 | Ensure 重载不与配置操作共用互斥锁；读取也可能看到中间状态 | Status/Ensure/Apply/Refresh/Prepare/Reset/Restore 共用配置互斥；启动互斥保持统一锁顺序；前端仅容忍读取忙，不重发不确定的写操作 |
| P2 | 仅依据 RPC 成功回复判断已应用；刷新失败的依赖级联可能改掉用户的独立开关 | 回读每一项实际 enabled；失败保留请求的磁盘 active，保留参数，公开未确认状态 |
| P2 | 清理旧页面样式忽略 CDP 错误、异常与分片，并可能把事件当作响应 | 收齐分片并匹配 id；拒绝 error/exception/无响应；主页面清理未确认时保留待重载状态 |
| P2 | 预设已写入但重载失败时，错误还说“配置未改动”或误报恢复成功 | 分离 saved/applied；保留实际快照并提示“已保存，但未确认”；离线明确等待启动后生效 |
| P2 | 读取失败后继续显示缓存的绿色成功状态 | 清空未确认快照，显示未知数量，保留重新应用入口；恢复后重新回读 |
| P2 | 后端退出后丢失其它用户的自定义目录/EXE；重载换目录可能误报旧目录的重置成功 | 记录已验证目标；revision 绑定目录；增加不暴露路径的 targetId，并检查重载期间目录变化 |
| P2 | 备份的字符串 bool、重复路径或坏 blob 被接受；巨型坏配置造成无界读取 | 校验字段类型、路径/链接/摘要/重复项；限制元信息读取及单文件/总备份大小，超限拒绝覆盖 |
| P2 | 坏的第三方 manifest 被忽略，可能影响优先接管和后端启动 | 原字节备份后写可逆停用占位定义；恢复还原原字节，不假称修复第三方作者文件 |

## 验证

- 33 项新增故障/恢复/路径/其它用户/读取错误/参数保护检查。
- 4 项实际 .NET WebSocket 客户端的本机惰性服务器测试：事件、分片、异常、错误与无匹配响应；不连接 Steam。
- 35 项实际 Vue 组件、typed service 与生产导航代码的浏览器/软件手柄检查。
- 29 项原预设、其它用户、重置、恢复和事务回滚检查。
- 21 项原脚本、参数保留、依赖行为与跨进程单实例检查。
- Windows PowerShell 语法检查、vue-tsc --noEmit、最终 Vite 生产构建通过。

异常退出测试使用专门启动的隔离 PowerShell 子进程，在部分文件写入后退出，再由下一次读取恢复。不是物理断电测试。跨用户判断包含 SID/SessionId 模拟分支检查；未声称搭建了多个真实 Windows 账号的联机验收。手柄测试仍为软件输入，不声称物理手柄型号兼容验收。

本机 Steam 只做状态读取；61 个原主题资源及参数摘要与原备份保持一致。七项仍启用，先前安装版本的后端就绪，无待重载。

## 尚需注意的边界

1. 原启动 BAT 的 tasklist 默认表格截断长进程名风险仍存在；按用户要求保持原样，未保留任何修改。
2. CSSLoader Desktop / 其它编辑器不参加 YMCC 互斥锁。不要同时改同一组配置；外部写入与目录切换会导致拒绝、要求回读或重新应用，不能承诺全局无竞争。
3. 未来 Steam/CSSLoader API、注入类名、调试端口或主题 schema 变化仍需实际兼容性测试。配置/加载器一致不等同于所有实际像素均已验证；本轮没有运行真实页面重置测试。
4. 磁盘耗尽、权限/ACL 改变、WMI 不可用、文件持续锁定或备份自身损坏时，会停止新操作并保留证据。无法承诺损坏磁盘或真实断电后所有写入均可恢复；当前异常退出恢复已验证。
5. 备份不自动删除，以免损失恢复依据，长期使用会有磁盘增长。恢复“接管前”会有意覆盖当前受管路径；撤销前的当前字节也会备份。
6. 本机安装目录未同步本次加固版本，当前运行版本仍需后续正常部署。

## 文件与证据

源码控制脚本：G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\PowerControl\steam-appearance.ps1

源码预设模块：G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\PowerControl\steam-appearance\preset.ps1

前端服务：G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\src\bridge\steamAppearance.ts

界面状态：G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\src\components\SteamAppearanceCard.vue

新增测试：G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\steam_appearance_robustness_selftest.ps1

新增协议测试：G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\steam_appearance_cef_selftest.mjs

交付脚本：G:\YeManCC-Work\Deliverables\SteamAppearance-20261010\PowerControl

核验记录：G:\YeManCC-Work\Deliverables\SteamAppearance-20261010\validation\robustness-audit.json

变更清单：G:\YeManCC-Work\Deliverables\SteamAppearance-20261010\change-manifest-robustness.json