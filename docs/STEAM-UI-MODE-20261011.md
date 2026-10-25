# Steam 桌面／大屏识别、切换与按钮描边（2026-10-11）

## 行为

| 状态 | 状态按钮 | 操作按钮 |
| --- | --- | --- |
| Steam 停止 | 未启动 | 开启 Steam（原 VBS/BAT 联动入口） |
| 桌面模式 | 桌面Steam运行中 | 点击启动为Steam大屏 |
| 大屏模式 | 大屏Steam运行中 | 点击启动为Steam桌面 |
| 进程在运行、模式无法确认 | Steam运行中（模式未确认） | 重新识别Steam模式 |

运行中的操作按钮不再使用 taskkill/steamStop，也不重新跑联动 BAT；走被验证的 Steam 安装内 steam.exe 的固定 -url 入口，分别是 steam://open/bigpicture 与 steam://close/bigpicture。提交返回仅表示接受请求；实际 GetUIMode 读取到目标后才完成。只接受两个模式，不允许 UI 提供任意 URL 或可执行文件。

## 识别和低占用

读取 Valve 当前 UI.GetUIMode，当前客户端枚举为桌面 7、Gamepad 4（兼容 VGUI 0、Tenfoot 1），其它模式或 API 不可读保持 unknown。不通过启动参数、窗口大小、语言标题或前台焦点推断。因此最小化大屏不会误判桌面，未登录也可以读取进程级模式。

复用现有 Steam Observer 线程：注册表与进程退出继续用于会话；经过 Steam 安装路径验证的顶层主进程／steamwebhelper 窗口 create/show/hide/name 事件通知界面。仅界面可见且 Steam 页激活时，350ms 合并为一次读取。空闲不新增持续扫描或线程；显式操作后的确认仍是有期限的 2 秒检查。后台读取在原生工作线程，读请求采用单飞门禁，防止 UI 超时后堆积同类读任务。

PID 和创建时间绑定阻止把旧页面请求提交给一个重启／PID 重用后的 Steam。隐藏、切页或卸载取消确认并失效晚到的状态；未知状态允许重试，不会用退出 Steam 作为“切换失败”的兜底。

## 按钮样式

主状态／操作按钮均为明确的 2px 描边：状态按钮是较柔和的 YMCC 主题色，操作按钮为实色主题边框。悬停增强、手柄/键盘焦点环保留，宽度不因悬停改变。Steam 页面其它启动、添加、删除和自定义库入口的边框也增强。不新增外层框、不为外观开关套框；未改其它页面全局 CSS。默认、红黑和赛博主题随现有 --accent/--text 令牌变化，480px 下无横向溢出。

## 验证边界

- 28 项实际 Vue 内存渲染行为回归（模式、故障、超时、重复操作、外部切换、事件合并、隐藏/切页、晚到回读）。
- 26 项服务及实际内嵌 JS 模式读取测试。
- 10 项真实浏览器组件/类型服务测试及截图（原生 IPC 为隔离模拟）。
- 12 项原生模式安全门禁测试。
- 47 项带 UI 回调的隔离真实 Win32 Observer 测试；45 项原 Observer 回归。
- 63 项 CSS guard、55 项 Steam live JS、23 项 guard 源码边界回归。
- 类型检查、前端隔离生产构建、原生完整编译/链接通过。
- 原生新模式读取器在本机只读检测：Steam PID 50424，mode=bigpicture，known=true。没有实际切换、启停或重载本机 Steam/CSSLoader。

合计 309 项检查通过。浏览器的模式切换和故障为隔离 IPC 模拟，不冒充真实机器切换验收。

## 并行任务／部署

本次代码位于 G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC；隔离构建和记录位于 G:\YeManCC-Work\Deliverables\Steam-Mode-Switch-20261011。
本次没有主动覆盖 C:\SOFT\YeMan 的安装文件、没有重启 YMCC，也没有更改 YEMAN_POWER_CONTROL_DIR，以免影响其它仍在进行的任务。
此功能需要配套更新原生 EXE 与前端。旧 EXE 的 unknown: steam.uiState 会安全降级为运行状态；不会虚报模式，仍可通过原入口启动已停止的 Steam。不应只拷贝一个新的 Rollup chunk 到旧主模块上。
原 BAT/VBS 摘要和七主题/61 文件预设摘要保持一致。此前发现的用户级临时 PowerControl 目录覆盖仍是独立问题，本次没有清理它，不宣称外观组件未安装提示已消失。
