# 本机／虚拟手柄的背键、专用键与陀螺仪反馈

2026-10-22 增强轮。总库仍由 `HcOemInputCapabilityBinding` 分配；`oem.keys.status` 只读取同一绑定、原采集器原子状态和现有官方软件隔离回执。旧 `oem.keys.get` 录制与校验协议、映射、固件写入、模式、屏蔽及退出归还均保持。

统一入口：`tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Tests controller.native-and-virtual-feature-status,controller.virtual-output-observed-ui -EvidenceDir <新的外部证据目录>`。

前一场景实际编译当前 `oem.keys.status` lambda，在内存机型／来源边界执行两配置；三项语义破坏均须运行后拒绝。TypeScript使用正式反馈函数覆盖本机、SteamDeck、PS5、Xbox；不得把目录、WMI订阅或隔离写入当成按键／动作／屏蔽成功。

后一既有场景运行正式页面setup与正式三状态组件，包括只读OEM事件、KeepAlive撤销、无新gyro检测、按键回执与模式切换。完整页面视觉检查另用既有 `tools/controller_shortcut_ui_merge_browser_selftest.mjs --feature-status-only`；默认原鼠标测试保留，专用分支不代替它。

本机陀螺仪仅提示需要现有虚拟手柄路线；不加接线或检测、不替用户开关。虚拟陀螺仪沿用原采集／处理／摇杆映射，Xbox无原生IMU不等于体感到摇杆不可用。微星本机背键自动固件写入仍被既有实测失败锁住；已有F11/F14输入可由原键盘采集消费，虚拟DInput准备路线不变。官方前端仅报告实际隔离操作状态，不推定输入冲突因果。

这些是软件消费者与模拟界面证据，不是掌机实物、官方前端或游戏验收；本机开发电脑身份不授予ROG掌机能力。
