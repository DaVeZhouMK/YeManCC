# YMCC 控制台 launch-failed — 降权启动修复

日期：2026-10-10（香港时间）。

## 已复现的失败

- 前轮管理员 native bootstrap 已通过 Steam 源校验并完成连接凭证注入，但 Loader 启动返回 `launch-failed / error=5`。
- 本轮源代码自测发现，`TokenLinkedToken` 在本机是 `TokenImpersonation / SecurityIdentification`，并非可直接复制的进程主令牌。用原 `DuplicateTokenEx(... TokenPrimary ...)` 返回真实错误 1346；原代码把所有复制失败统一覆写为 5，隐藏了具体阶段。
- 单纯补资源、重复开关、取消降权或管理员运行第三方插件都不是正确修复。

## 代码修复

新增 `native/decky_sidebar_launch.h`，由原 Runtime 使用，保留原事件驱动进程 owner 和 Job 清理。

- 普通权限父进程仍直接创建普通权限子进程。
- 管理员父进程仅把 UAC linked token 用作有限身份参照；不复制识别级 linked token。
- 获取真实 Windows 桌面 shell HWND/PID，固定该进程句柄，校验系统 `explorer.exe` 路径、TokenPrimary、同用户 SID、同 SessionId、同 AuthenticationId、非提权与 medium-or-lower integrity。
- 从已验证的桌面进程复制主令牌。CreateProcessWithTokenW 在本机还需要复制句柄的 TOKEN_ADJUST_DEFAULT / TOKEN_ADJUST_SESSIONID 权限；增加的是句柄访问权限，不向令牌增加任何特权。既有登录用户桌面已加载配置，不再请求 LOGON_WITH_PROFILE。
- 子进程一律 suspended/no-window；子令牌再次校验身份、登录会话与非管理员；通过原 Job ownership 验证后才 ResumeThread。只可终止本次创建的仍挂起子进程，绝不把 Steam/桌面 shell 当作终止对象。
- 任何 identity/permission 失败都拒绝启动；没有 elevated CreateProcess 或提权兜底，没有改 UAC、ACL、服务、Steam 设置或用户级数据目录。
- 错误保持实际 Windows code，新增 launchStage/launchMode，原控制台日志和 UI 提示会显示，不再只露裸 `launch-failed`。

## 验证

输出目录：`G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Launch25\validation`。

- TOKEN-ELEVATED-R3.json：只读 probe，复现 linked-token 1346，确认本用户正常 shell 主令牌可复制及登录身份一致。
- LAUNCH-NORMAL.json：普通权限父进程 → 自编无业务子进程，子进程非管理员、正常退出、恢复前 Job 归属。
- LAUNCH-ELEVATED-ADJUST.json：管理员父进程 → 自编无业务子进程成功，子进程独立回读证明普通权限。此前失败的 MINIMAL/WithProfile/AsUser 尝试保留在证据目录，不把它们混算成功。
- MAINLINE25-NATIVE-UI-ELEVATED.json：实际管理员测试监管器，使用精确生产 Runtime/Broker/native bootstrap，已校验的真实 pinned Loader PID 启动成功。`launchStage=child-token-verified`、`launchMode=verified-shell-primary`、`error=0`。监管器独立读回 Loader 令牌，`unelevated=true`。
- 该 native fixture 没有运行产品 YMCC，不改真实业务设置、不操作硬件；界面业务数据为内存 fixture。没有宣称页面完整接管或硬件回读已验收。有限90秒窗口结束后，自有 Loader/Job/Broker 均清理，Steam 没有关闭或重启。
- LAUNCH-ACCEPTANCE.json：本轮真实启动结果与身份/Job/无高权限兜底契约汇总。
- 原 native source-built 五组回归 27+26+33+32+28=146 项通过。
- DIAGNOSTICS-SELFTEST.json：7 项诊断、UI 可读提示与日志契约。
- 类型检查、Vite 与主线原生 compile/link 均通过；`Build\App\Native\YeManCC.exe` 为修复候选，没有执行产品候选。

## 部署边界

当前运行安装版仍是旧 EXE。此次启动实测成功来自隔离管理员监管器，不能自动声称安装版已修好。

下一步需要备份安装 EXE/前端，正常退出并更新 YMCC 的成套原生与前端，然后由用户原启动方式重新开启；Steam 不需要重启。YMCC 重新启动可能执行原 CPU 自启和风扇恢复，不能在未确认时悄悄替换并启动。

上轮 `YEMAN_POWER_CONTROL_DIR` 临时根覆盖仍保留；该目录的资源已补齐。清除变量和业务配置迁移是另一件事，不在本次降权启动补丁中擅自执行。
