# Steam 外观内容版本检查与自愈 · 2026-10-23

实际修复：原来的路径标记不能区分同名旧CSS。render协议4对44份静态CSS增加由正文SHA派生的内容标记，复用每助手的一份约14KB manifest索引；同一次有界CDP读取确认配置值、路径/内容版本及重复注入次数，不新增timer或后台全树扫描。发现异常按主题合并一次定向重提，原revision/target/Steam身份/编辑器围栏保留。

打包门独立重新计算正文摘要，拒绝只改普通SHA却沿用旧内容标记的错误资源。维护CSS后可运行 node tools/steam_appearance_content_update.mjs --write；默认不写，只验证。

兼容性：PS5.1和PS7 JSON整数允许有界Int32/Int64，布尔、小数、字符串、越界仍拒绝。Ensure/Automatic遵守已合入的CSS当前令牌合同，Decky仍使用自己的降权路线；CSS后台独立常驻，手动/自动助手受限退出。

本机前后证据：新规则只读核对旧安装，报告7个开启主题内容不匹配；部署配套Native/Web/PowerControl后，启动事件自动更新资源和重读，17:29:11进入ready。后续实际只读renderConfirmed=true，packRepairRequired/backendReloadRequired=false。8份个人配置逐字未变，保持7/8和Footer Default；Steam63648和CSS45500→11240的出生身份未变。独立观测5个大屏/主菜单/三点菜单页面均匹配内容标记；产品自动检查目前仍以大屏主文档为准，不能把一次性观察写成全页面自动覆盖。

验证：PS5.1实际HTTP/WebSocket/Edge完整82场景；PS7新增8场景；旧消费者在真实运行的过期焦点动画下仍报成功，新消费者拒绝并修复后计算动画为none。原生手动64、请求32、单助手68、导出22、bridge34，以及实际组件/正式手柄引擎252状态、19544方向、498动作通过。物理手柄输入没有验收。

20秒正常空闲观测没有新增外观助手，CSS两进程句柄变化0、CPU分别78.125ms和0ms；整个YMCC CPU296.875ms、句柄+16，包含其他模块，不归因为CSS也不当作全程序无泄漏证明。

用户要求的抽动完全消失仍须真机视觉反馈。本轮部署与测试不启动/重启/切换Steam，不改变当前主题选择。持续目标保持ACTIVE。

安装：C:/SOFT/YeMan/YeManCC/YeManCC.exe（PID 52352）；Native SHA256 242a96a83fda0b42253641336e495f0cf2508d868acdc7d59ca8ec2bc0543c42

备份：C:/SOFT/YeMan/Backup/SteamAppearance-Content-20261023-1712

证据：G:/YeManCC-Work/Build/Tasks/CssContent-20261023-1712/RESULT.json；G:/YeManCC-Work/Testing/YMCC/Evidence/SteamThemeReconcile-20261023/content-receipts

本轮 Git 提交限定外观模块、资源与直接测试；共享 native/main.cpp、部署入口和全局模拟器中其他任务的大量未提交变更没有整文件纳入。可复现的本机集成构建以证据目录中的冻结 source、source-snapshot.json 和构建日志为准；不把局部提交声明为整个主线的独立正式发布。
