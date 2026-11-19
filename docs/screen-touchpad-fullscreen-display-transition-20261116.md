# Win5 全屏呼出 YMCC 后触摸板失效

本轮已写入主线，尚未导出或部署。参考监控的窗口生命周期，保留触摸板专属 Touch/Pen 输入和鼠标穿透，未替换为接收普通鼠标的监控交互窗。

日志准确 writer 9100/birth134393099142692595，执行 marker a080694d；21:46:09 曾恢复，21:46:15 在累计 repair3/compositor rebuild1 后仍出现 ProcessEvents 0x8000FFFF，导出 events 停268、failure sequence90。进程和监控采样继续，不据此称整个 YMCC 崩溃。suspendSerial0，本轮不能判为睡眠。固定194456包未含新的完整输入通道复位，不把该包失败当新通道实机失败。

WM_DISPLAYCHANGE、WM_DPICHANGED、工作区变化先撤本侧输入/外观并释放信号，保留一次100ms计时后恢复；新通知合并且不延长首计时。实际 refresh 入口统一服从等待，配置只更新最新请求并立即释放旧信号，避免旁路。原自动隐藏状态和截止保持，显式设置的预览/重置仍按原语义执行。恢复窗口仍5秒、累计最多3次修复，无新闲置轮询、常驻线程或输入刷新率。

首轮16项通过后独审发现普通刷新旁路与idle计时重置，未直接发布首候选。原同步语义必要控制失败；原首候选加新边界检查出现3项预期断言失败，随后终态0xc0000005亦保留，不签该负例完整生命周期成功。最终实际原生消费者22项全过，覆盖1024通知、8轮风暴、嵌套切换、配置交错、隐藏与临期计时、OFF/睡眠撤销/退出。4个Job均关闭前0，原business/工具状态分列；两最终Job自然结束，无工具终止。

6产品+2原fixture通过24个share0句柄导入，208.5427ms，句柄全归还；main仅三处唯一上下文正逆导入，保留并行任务其余字节。导后22源及新增头与最终实编消费者逐字相同，不重跑已过组。完整Native未编，Windows内部错误和Win5实体全屏稳定性仍OPEN，下一自然配套Native/runtime/source-PDB验证。

权威：FIELD-ANALYSIS.json、PACKAGE-MARKER-JOIN.json、VALIDATION-INTAKE.json、SOURCE-REVIEW.json、MAINLINE-IMPORT.json、POST-IMPORT-READBACK-r2.json、SOURCE_READY.json。
