# YMCC 控制台“尚未提供镜像连接”修复（Bootstrap26）

日期：2026-10-10（香港时间）。

## 定位

这不是要求用户提供下载镜像网址，而是 Steam 侧栏尚未拿到 YMCC 本地镜像连接凭据。

- 当前运行日志显示资源 ready，Loader 已以 verified-shell-primary / child-token-verified 普通权限启动；Steam PID 33856 未变化。
- 实际 Steam SharedJSContext 只读检查发现 __YMCC_DECKY_MIRROR__ 不存在，Decky 已加载。
- 生产代码在启动 Loader 前只执行一次注入，原 Runtime 仅观察 SharedJSContext 的进程退出；同一进程内的 JS 页面重载不会触发它。
- 已缓存的 Loader injector.py 的 reload_and_evaluate 明确执行 location.reload()；此前隔离 UI 夹具也需要在 Loader 初始重载后再注入。因此一次性启动注入不足以覆盖该生命周期。

## 修复范围

- 新增原生 BootstrapWatch，沿用已验证的 SharedJSContext CDP 连接，订阅 Runtime.executionContextCreated。先确认订阅成功，再恢复原 Loader 启动。
- 每个真实默认上下文事件都重新验证固定进程租约、Steam 父子进程路径、监听归属、Windows 用户与会话；仅 Steam loopback origin 和顶层窗口可接线。
- 上下文创建/重载时执行原冻结 bootstrap 表达式并发出原 ymcc-decky-bootstrap 事件；现有客户端的相同连接事件去重不变。
- 空闲时仅等待现有 socket / 取消事件，没有周期评估、端口扫描、重启 Steam 或额外 Loader。命令仍有有限超时；初始化遵守原 7 秒预算。
- 实际 Steam Runtime.enable 会补发历史调试事件。只跳过它们，不记录内容；有限超时和 4096 包上限防止大量历史事件误判接线失败，事件队列上限 32。
- 导航中已消失的上下文只等待下一个真实事件，不自动重发业务操作；身份或协议失败则关闭接线。共享页面进程退出/恢复继续使用原 Runtime 有限重绑机制。
- 关闭侧栏、切换原生会话或退出 YMCC 时，取消并释放 watcher/socket。Steam 的只读句柄不归属 Loader Job，绝不被终止。
- 不改变资源检查、前轮 Loader 降权、来源验证、原游戏/风扇/TDP/输入/锁帧业务 owner、私有 admission、锁及配置目录。

## 诊断

原日志：C:\Users\DaVe\AppData\Local\YeManCC\decky-sidebar.log

新增 bootstrap（注入结果）、bootstrap-watch（watching/context-attached/context-replaced/stopped/failed）、mirror-peer、mirror-host-attach、mirror-snapshot-request、mirror-snapshot-queued。字段仅允许阶段、PID、代次、ready/connected/revision 等元数据；token、endpoint、runId、脚本、请求参数和业务内容不入日志。沿用去重、1 MiB 轮换和现有日志导出。诊断不得改变已排队响应的成功结果。

## 验证及边界

- 原生六组源代码测试：27 + 26 + 33 + 24 + 32 + 28 = 170 项 PASS；产品编译、链接、管理员 manifest 嵌入 PASS，候选产品 EXE 未执行。
- 新 watcher 使用实际 WinHTTP + 自有 loopback WebSocket 夹具：已有上下文、同进程重载、192 条历史事件、空闲无轮询、外部 origin/隔离上下文拒绝、导航竞态、静默 IO 取消、身份变化拒绝、日志脱敏均覆盖。它不是实际 Steam。
- bootstrap 原表达式 9 项 PASS；原客户端、只读 relay、游戏/风扇动作、全局触摸板/独立帧率、3 秒电源去抖、下拉菜单与焦点等相关回归 PASS；类型检查和 Vite PASS。
- 当前 Steam 只读探针 PASS：收到一个符合条件的默认上下文事件；注入前后均无 bootstrap，证明没有测试凭据替换当前产品连接。没有页面重载、Steam 重启、Loader 启动或真实硬件/配置写入。
- 本轮没有在运行中的产品上完成“新补丁 → 实际侧栏快照”验收，不能把模拟重载和只读检查说成真实 YMCC 完整接管成功。更新后应在日志中看到 context-attached → mirror-peer connected → mirror-snapshot-queued ready。
- 历史主线验证入口停在 MAINLINE15-NATIVE-ADOPTION.json 缺失/旧字节保留证明；该证明不适用于本轮有意修复原生 Decky 单元，没有伪造“原生字节未变化”证明。其前面的相关测试及后续明确单独运行的目标测试均记录于 validation。
- 旧开关测试夹具补入已存在的只读环境查询/真实错误格式器；只读配置夹具补入已有独立帧率纯模型；旧 loopback 游戏期望同步现有整套专属独立帧率记忆规则。这些仅更新测试，不改业务实现。
- 源快照 PowerControl 插件目录缺少 package.json，默认 --check 因此失败；在任务 Build/Plugin 完整生成后 --check PASS。其 dist/index.js 与当前已部署侧栏同为 E454DA9A13DA414566D100B4F16898C9D7B0D32F64F9B1D010720F6C1D7E6663；本修复无需替换现有侧栏插件。

## 更新方式

本包仅包含 YeManCC.exe，不替换现有前端、PowerControl、侧栏插件或用户配置。包含前轮资源诊断与降权启动的原生修复。

1. 备份当前 YeManCC.exe，正常退出 YMCC（不要关闭 Steam）。
2. 用包内 YeManCC/YeManCC.exe 替换正在使用的那个安装目录中的程序。
3. 按原方式启动 YMCC，再打开 Steam 控制台查看连接与日志。

YMCC 重新启动可能按原配置执行 CPU 自启/风扇恢复，因此本轮没有擅自退出、更新或启动正在运行的产品。保留现有 YEMAN_POWER_CONTROL_DIR；不要清除该变量或迁移业务目录来处理这个接线问题。
