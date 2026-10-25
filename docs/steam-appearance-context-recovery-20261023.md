# Steam 独立菜单样式核对与测试交付 · 2026-10-23

本轮补上大屏主页面之外的 MainMenu / QuickAccess 文档核对。此前仅菜单缺失样式时，主页面正常会造成误报成功；保留的旧消费者反例可复现该问题。根 render 协议仍为4，额外 contextProtocol=1 为每个上下文提供文件、内容版本、重复注入与动态参数计划。动态补丁不适用于该菜单时，仍须保留 shared.css 的静态默认值，不能误判为参数残留。

沿用既有启动、手动应用、恢复等有界核对入口，一次读取目标列表；最多4个主页面、8个独立菜单文档，共用4秒CDP预算，每页最多1秒。各页面异常按主题合并，仍只走一次定向 Reconcile，保留 Steam 身份、revision、目录和编辑器保护。页面在读取中关闭或回执不明时返回未确认，不授权盲目写入。没有新增定时扫描、另起Steam模式监控、启动Steam、切换模式或前台操作。

范围限制：本轮扩展已有事件边界的核对；不声称单靠此补丁能立即发现上一次核对之后任意新建的菜单文档。真实Steam/GPU抽动是否完全消失仍需视觉验收，不能以CSS标记正确代替。

验证：Windows PowerShell5.1实际HTTP/WebSocket/Edge99场景；PowerShell7菜单17场景；生产配置计划364项，覆盖220个补丁选项；导出门23项。保留了菜单缺单文件、SP样式/变量误注入、同主题跨页面合并一次修复、未知/关闭页面不写入、布尔协议、外部WebSocket、重复id与数量上限反例。旧单页面测试的视觉比较已明确取SP页面，菜单不会冒充主页面。

本机18:26:48完成脚本最小导出：C:/SOFT/YeMan/PowerControl/steam-appearance/idle.ps1，SHA256 515d5537138f26091f40302342d608eb6eedd7da952114126b62c1320226a88f。同一Steam63648与CSS45500→11240保持不变；8份个人配置逐字保留，个人7/8与Footer Default保留。安装版读取5个页面（主页面1、菜单4）全部确认，耗时380ms。仅替换idle.ps1，现有Native/Web保持原配套版本，不必为此次兼容扩展重编译UI。备份：C:/SOFT/YeMan/Backup/SteamAppearance-Contexts-20261023-1823。

本地测试入口：C:/SOFT/YeMan/YeManCC/YeManCC.exe。现有YMCC可直接手动使用“重新应用”，下一次助手会读取新脚本；无需重启Steam。

源码/导出：G:/YeManCC-Work/Build/Tasks/CssContexts-20261023-1823/source-snapshot.json；本机证据：G:/YeManCC-Work/Build/Tasks/CssContexts-20261023-1823/deployment.json；完整回归：G:/YeManCC-Work/Testing/YMCC/Evidence/SteamThemeReconcile-20261023/context-recovery。本提交仅包含此外观补丁及直接测试和说明，其他共享主线任务未整文件纳入；不是完整正式发布验收。
