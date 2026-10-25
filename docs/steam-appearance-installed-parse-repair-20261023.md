# Steam 外观模块加载与安装预检修复（2026-10-23）

现场idle.ps1为带13个解析错误的中间版本，主线正确文件已存在，但安装目录仍是坏字节。已恢复正确模块，并在控制入口的模块加载阶段将缺失/语法错误转换为一个明确JSON失败响应。

deploy-installed在安装目录任何移动/复制前执行Get-SteamAppearanceExportFiles；原复制后完整一致性校验保留。导出门分别解析入口、idle、preset、resident。新tools/steam_appearance_load_failure_selftest.ps1执行真实入口的隔离拷贝，覆盖每个模块缺失/语法破坏和健康Library导入；已经接入plugins.decky-update-recovery。此检查不启动Steam/CSS。

证据：G:/YeManCC-Work/Testing/YMCC/Evidence/SteamThemeReconcile-20261023/installed-parse-regression-133711/REPORT.md。专项通过不等于当前整主线、未完成监控接续、真实Steam页面/手柄验收。当前安装热修保持原有协议与个人7/8选择，没有部署VerifyOnly或ManualObservationProtocol。
