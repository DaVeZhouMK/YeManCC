# 独立输入邮箱溢出后恢复新触摸

已写入主线，未导出部署。范围仅上一末级故障恢复启用的独立InputChannel：邮箱丢边/配额通知转为取消后，旧代码将admitted永久保留false。实际原生旧对照已确认旧contact释放、旧epoch拒绝，但新Down仍被丢弃。此为新恢复链自身缺口，不冒旧Win5 0x8000FFFF根因。

候选先完整取消丢失手势，再仅在同一源/原代次、窗口仍显示且可用、未退出时重新同步原输入源。取消失败保持隔离；取消回调中的同源sync被cookie门阻止，本次失败用独立HRESULT累计，不被后续成功回调覆盖。切换/关闭/换源的新代无自动恢复权限；不重放丢失事件、不创造新通道或修复额度。

第一候选15项过后，独审发现取消失败与重入sync的组合；两顺序实际必要负例已失败，修订后17项全过（14业务+3资源）。含真实Input::publish→自有HWND→Surface drain/cancel→原nativeInputSink/Core、新Down/UP、旧epoch、失败隔离、切换及退出重入。数据通过私有friend显式输入端口，不能冒OS触摸生成/物理捕获/设备震动验收。该friend仅生成副本，四epoch整头逆除核同，主线产品不含该口。

4成功编译/4原生Job关前0，原root/business/tool分列，最终自然结束无工具终止。初候选15与最终17不拼父；未重跑原普通22/13/10/20/7/stop组。旧18个静态测试函数正文逐字保留，原PS1新增input-overflow可选组及私有生成函数，默认组不变；实际原PS1选择/最终身份检查语句及私有头生成已核同实编。首配套核收变量覆盖receipt失败保留，只续报告准备未重跑Native。

1产品2原fixture+20依赖全部share0同持，实际耗时和HANDLE见MAINLINE-IMPORT.json，正逆全bytes与导后23源核同。取消失败输出参数保零实参调用形式，普通UI源不走独立cookie门；monitor产品及main/公共睡眠/Steam未改。无新timer/thread/poller或刷新率，无每MOVE新分配；CPU收益未测。

Windows内部错误、Win5新版loaded/全屏实体稳定性、永久Close/queue停滞与长期CPU仍OPEN，下一自然配套Native/runtime/source-PDB带全部源；无追赶旧包/部署/新ID或默认门，持续目标ACTIVE。权威VALIDATION-INTAKE/SOURCE-REVIEW/REVIEW-OVERFLOW/MAINLINE-IMPORT/POST-IMPORT-READBACK/SOURCE_READY/CLOSING。
