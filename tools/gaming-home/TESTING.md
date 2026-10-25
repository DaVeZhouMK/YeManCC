# YMCC Steam 全屏主页测试版

本组件把 YMCC 注册成 Windows 全屏主页，由它按需启动现有管理员权限的 YMCC，再调用原 `C:\SOFT\YeMan\PowerControl\YeManSteam.bat`。无需安装 AnyFSE，也不依靠结束 Xbox 进程。`GamingHomeApp` 是 Windows 的主页选择，Steam 大屏由原 BAT 请求。

## 使用

1. 保留上一轮已测试的 YMCC / InputHost 安装版。本包只增加主页组件，不替换主程序、手柄、风扇或 TDP 程序。
2. 先运行 `Inspect-YMCC-Home.cmd` 查看状态；它只读取信息。
3. 在 Windows 设置手动开启“开发者模式”。测试包携带开发用 gamingHome 能力描述；启用脚本会检查该条件，不替你修改。
4. 右键 `Enable-YMCC-Home.cmd`，选择“以管理员身份运行”，使用你当前 Windows 登录账号。脚本导入本包公开签名证书到本机 TrustedPeople、安装主页包、建立当前用户最高权限的无触发器按需任务，备份并加上 BAT 的 YMCC 已运行检查，最后选择 YMCC 为全屏主页并开启登录进入主页。原 `.earlystart` 和外层 BAT 保持原样。安装不会启动 YMCC/Steam，不会注销或重启。
5. 由你手动进入 Windows 全屏环境或重新登录。系统主页应出现“YMCC Steam 全屏主页”，短暂显示启动画面，然后进入 Steam 大屏。再次通过系统 Home 进入时，应复用已有 YMCC/Steam。

## 验收

- 首次全屏进入：YMCC 启动一次，Steam 大屏打开；继续确认原虚拟手柄、TDP 和风扇功能。
- Steam 已运行：Home 返回现有 Steam 大屏，不重复开 YMCC。
- 启动中退出全屏：主页取消后不重试拉起。
- 虚拟手柄就绪由 YMCC 原控制器负责；主页日志中的“YMCC 进程已观察到”不能当作手柄可用证明。
- 主页用 Steam 所属窗口的会话、路径、类及样式做交接；窗口交接不是 Valve UI 模式确认。新版 Windows / Steam 的实际交接、系统是否再次调主页，需要本机测试。
- 发生问题直接用普通 YMCC“导出日志”，主页记录复用 `steam-launch.log`。不需要另外运行监督器。

## 恢复

右键以管理员身份运行 `Restore-Previous-Home.cmd`。它仅在当前主页/开机标志仍属于本次设置时恢复原值；BAT 只有仍等于本包保护版本时才恢复备份。后续自定义修改会保留。包、公开证书和无触发器任务保持已安装、但不自动运行的状态；不强关任何应用。

这次是独立主页组件交付。已编译和软件验证的结论与真实 Windows 全屏、实物手柄验收分开记录；尚未宣称 26H2 实机通过。
