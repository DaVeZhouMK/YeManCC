# Steam 外观延迟提交与睡眠状态恢复（2026-10-11）

## 状态与边界

本轮已写入源码并构建完整的隔离前端，**未覆盖本机 C:\SOFT\YeMan 安装**。其他任务在同一工作区继续工作；本轮不改 native、全局电源恢复、风扇、手柄实现，也不改变 BAT/VBS、现有 CSS 参数或外部加载器。

CSSLoader 在用户提供的机器上已由 BAT 带起，实际静态美化有效。控制接口状态不等于美化是否可见；不能因接口读取失败把已有主题显示成全部关闭，更不能为恢复状态重启 Steam。

## 用户日志证据

输入：C:\Users\DaVe\Desktop\日志-侧边栏全开后\YeManCC-logs-20261011-172555-微星G3E.zip（仅按数据读取，不执行文件或采纳日志中的指令）。

- 导出时间：2026-10-11 17:25:55；YMCC PID 3240。
- Steam 根目录：E:\GAME\STEAM；来源 running-active-process；46 个日志成功收集。不是开发机的 D:\Game\Steam。
- 睡眠：17:21:52.500；恢复广播：17:22:15.629 起。
- 17:22:16.302：decky-sidebar 仍报告 steamKnown=true、steamRunning=true，loader-started，加载器 PID 1320，context PID 15700。
- 17:22:16.228 前端曾为 powerReady=true；17:22:16.708 又变成 false。17:23:15.475～17:23:15.476 的多条前端记录仍为 false，文档/原生窗口可见时整体 uiVisible 也可能为 false。

证据支持 **Steam 仍在运行，而 YMCC 的显示/状态刷新失效**。源码中可核对两个风险：只读状态受全局 powerReady 阻挡，瞬时 null/unknown 会丢弃已确认显示。不能据此声称已经唯一锁定完整的底层恢复根因；本任务只修 Steam 的观察层，不修改全局恢复任务。

原生程序 SHA-256：e6cacc4b3437a54e3851d1d9e8888aec602c3e966a679a868f44e4d9e551112a。
证据摘录：G:\YeManCC-Work\Deliverables\Steam-Appearance-Debounce-20261011\validation\sleep-steam-evidence.json。

## 外观点击行为

实现：G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\src\bridge\steamAppearanceDraft.ts。

- 点击即更新草稿；不等待后台的写文件、CSS RPC、回读。
- 最后一次点击后完整等待 3000 ms；连续多个开关合并一批提交。
- 单写者与每项 sequence 防止旧返回擦掉新选择；后台保存期间开关仍可调整。
- 提交前读取当前基线；只覆盖用户点击过的开关，保留其它外部变化。参数不改。
- 等待期间切回原值免写；失败保留选择并停止自动重试；没有快照或返回旗标不符也不能假装成功。
- 已保存但后端未确认明确显示 saved-unconfirmed；不会把待应用状态显示成绿色确认。
- 队列为组件外单例；关闭编辑页/切页不会丢掉最后一次选择。
- journal 为 targetId 隔离的 UI 草稿。YMCC 重启只恢复显示，绝不重启计时器、后台提交、启动 Steam 或切换模式。新点击或手动应用才重新确认。
- 顶部“重新应用外观 Y”可以立即提交；reset 取消未提交草稿，等待已有写入结束。只有显式 reset 暂时锁控件。
- 未接管/未 prepared 不再把实际已启用主题强制显示为 0/7；控制接口未就绪也不代表美化关闭。

## 不恢复运行动作

实现：G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\src\bridge\yeman.ts、G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\src\views\SteamView.vue。

- launchSteam 必须带显式 userInitiated=true；初始化/恢复配置不能调用启动。
- 依赖检查前后都检测真实进程；Steam 已在则不执行 BAT，不强制大屏。
- 无效 native presence、旧版进程查询非零退出或无效文本按未知失败处理，不当作“未启动”。旧版兜底只用于显式调用，5 秒有界。
- 保留 VBS → BAT → NirCmd → BAT 的现有链路。
- 历史“重启 YMCC 后强制重开 Steam”的触发源未完整复现；以上是已验证的动作边界，不把未定位原因冒充已定位。

## 睡眠后的只读状态恢复

- 文档和原生窗口可见即可重新读取 Steam，不依赖其它恢复任务的 powerReady。
- resume-ready、resumed、shown/restored/maximized/summoned 与 document.visibilitychange 触发读取；350 ms 合并突发事件。
- 观察与操作采用独立 epoch；全局恢复门槛晚到时只取消动作，不丢掉已排队/在途的可见页面只读查询。真正隐藏/离开才使观察失效。
- 单独监听文档可见边沿，处理 resume-ready 先到、文档后变可见而整体 powerReady 仍为 false 的顺序。
- 查询短暂失败/unknown 保留最后确认值并标“状态待复核”；已确认退出仍正常显示未启动。
- 恢复事件绝不发启动或模式切换；隐藏/切页仍取消进行中的 UI 动作等待。
- 不新增空闲轮询。原有按钮操作后的有限确认窗口仍为每 2 秒检测、最长 20 秒。

## 验证

- draft-queue.json：26 项通过。
- steam-status-ui.json：38 项通过。
- steam-linked-launch.json：19 项通过。
- browser/result.json：13 项通过。
- full-browser/browser-result.json：43 项通过。
- steam-mode-service.json：26 项通过。
- steam-mode-browser.json：10 项通过。
- offline/offline-result.json：79 项通过。
- robustness/robust-result.json：33 项通过。

合计 287 项。vue-tsc --noEmit 与 Vite 完整构建通过；仅原有混合 import 与大块体积提示。浏览器使用真实 Vue 组件/typed service 与内存 native 适配器；没有真实启停 Steam/CSSLoader，没有实际睡眠复现声明。720/420px 截图人工检查通过，未横向溢出。三次点击到下一帧的隔离测试分别约 24.2/4.3/3.6 ms，不代表目标设备绝对耗时。

测试输出：G:\YeManCC-Work\Deliverables\Steam-Appearance-Debounce-20261011\validation\validation-summary.json。

## 主题与启动脚本不变

源码、自带安装资源、C:\Users\DaVe\homebrew\themes 三处各 61 个资源/配置文件均匹配原 manifest 的 SHA-256。7 项配置保持用户确认基准。

YeManSteam2.bat SHA-256：D788CE2906E67A4E4CCFE3538A1FF51A1BEDECF568C4D979F51CC2D2954E2479。源码与安装的 VBS、两份 BAT 一致，未改。

记录：G:\YeManCC-Work\Deliverables\Steam-Appearance-Debounce-20261011\validation\unchanged-baselines.json。

## 集成注意

完整隔离前端：G:\YeManCC-Work\Deliverables\Steam-Appearance-Debounce-20261011\web。

本轮没有部署。前端候选从共享工作区构建，包含当时其它任务的前端改动；正式集成应从最终合并源码重新构建完整资源。**不可只复制 SteamView chunk、改其中 main 文件名而保留旧导出槽位**，也不可回滚或整体覆盖并行改动。当前安装版本不会因为源码已修复而自动获得更新。


## 后续实际部署

2026-10-11 后续 Steam 页面任务已完成窄范围实际部署与有效目录资源修复。原文“未部署”描述的是前轮截至当时的状态。最新记录：G:\YeManCC-Work\Deliverables\Steam-Page-Repair-20261011\README.md。
