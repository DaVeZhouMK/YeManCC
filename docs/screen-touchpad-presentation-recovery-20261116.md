# 触摸板恢复器与 YMCC 临时隐藏的交错修复

已写入主线，未导出部署。发现并原生复现的是当前主线新增呼出展示 lease 的恢复判定缺口，不能倒推成旧 Win5 日志中 Windows 0x8000FFFF 的根因；固定旧包尚无 presentationSuspended 字段。

恢复器已完成第一次健康检查后，YMCC 呼出按设计通过 presentationSuspended/refresh 暂时隐藏触摸板。旧恢复器仍将 HWND 不可见当成输入失效，两次检查即消耗 repair2/runtimeRebuild1，甚至重建原正常资源。有效旧对照的前置健康检查通过，后续只在这一条必要检查失败，原证据保留。

产品仅在原 ownerResumeTimerProc 的当前上下文、OFF 和原期限检查之后，读取已有 presentationSuspended；临时隐藏时清除半段 clean、复用原250ms检查并沿用原5秒截止，不检查/修复、不退还或重置额度。解除后重新完成两次健康确认。正常未隐藏分支正文、显示刷新率、原3repair/3episode、退出和公共监控/Steam/睡眠均保持。

最终10项原生检查全过（7业务+3资源清理），覆盖有效前置健康、临时隐藏不探测/修复/续期、恢复后两次新确认、期限到期、迟到计时、OFF优先及退出释放。修前repair2/rebuild1，修后均0。原C3538夹具编译失败和首次漏observed的无效准备失败保留，不当成产品负例；4个owned Job关闭前0，原root/business/tool状态逐项保存。此组并非完整Native、实际游戏或实体睡眠验收。

1产品2原fixture在20个未改依赖同持share0下导入，150.0568ms/23HANDLE全还；产品仅1块，正逆bytes及导后23源同实际候选核同。没有为收口重跑原已过22/13/20/7/stop组。新增presentation-recovery可选组，默认测试正文不变。不增加闲置poller、线程或定时器，未测实际CPU降幅。

权威：VALIDATION-INTAKE.json、SOURCE-REVIEW.json、FIXED-PACKAGE-BOUNDARY.json、MAIN-SOURCE-BOUNDARY.json、MAINLINE-IMPORT.json、POST-IMPORT-READBACK.json、SOURCE_READY.json。原Windows内部原因、Win5新完整复位/全屏稳定性和长期CPU仍OPEN，持续目标ACTIVE。
