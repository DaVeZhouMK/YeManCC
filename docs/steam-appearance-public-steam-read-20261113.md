## Steam 外观消费公共 Steam 层（2026-11-13）

Steam 外观已接回唯一公共 Steam 层。身份由现有 Observer 的不可变快照提供；PowerShell 不再枚举 Steam、不再查找 Steam 的 8080 监听者或自建 WebSocket。CSSLoader 的 35821 RPC、主题文件、编辑器保护与唯一常驻仍由外观模块负责。

公共 `native/steam_document_read.h` 复用原传输锁、监听者验证、进程租约和有界 socket，只接受三类文档的样式属性计划。来源、实际页面、文档代次与最终目标集合均复核；请求绑定 PID、出生时间、根目录、进程/账户代次及电源代次。未知事实不当作未运行；读取是顺序批次，缺少菜单文档保持 incomplete，不能当作三槽全部确认。

原 CSS worker 映射承载 typed 请求；新旧 native/Web/PowerControl 协议必须配套，缺配套明确拒绝，不退回私有读取。不新增 Steam producer、常驻线程或巡检定时器，不启动/重启 Steam、不切模式、不抢前台。七项主题及布局、参数与特效策略未改。

本次精确导入 8 产品文件、19 原夹具/导出支持文件；保留并行 main 和公共故障诊断。实际公共身份 13、文档 34、浏览器定向 20、前端到离线脚本 12、service 37、模块 10、离线配置 78及导出配套 4 项分别有原件，不能拼成整父 PASS。最终两份原生实际编译输入为 368/366、链接库 12/9；SDK 声明遗漏修复前的失败保留。9 个核收 Job 关前均为零，VCTIP 工具回收单列。历史默认组按原 epoch 引用。

仅源码配套就绪，未完整产品构建、打包、部署或操作真实 Steam/CSS；真实视觉、现场 CPU 和物理睡眠另验。下一自然构建收录整套来源，不追改旧包。原件：`Testing/YMCC/Evidence/SteamAppearanceSharedLayer-20261112/SOURCE_READY.json` 与同目录 `MAINLINE-IMPORT.json`、`SOURCE-VERIFICATION.json`。
