# ymcc_sim · Operator 授权记录（AI 执行时先读这里）

日期：2026-09-23 · 记录人：Fan 执行线（203/FAN） · 依据：operator 明确指令（逐字见下）

## 1. 允许 AI 开关 YMCC（已授权，长期有效）

operator 逐字指令（2026-09-23）：

> "你来重启 自动提升权限 和 关闭 YMCC，并把代码写入模拟测试环境内，让 AI 以后知道我让你测试 = 你可以开关 YMCC，免得你每次都来问题"

⇒ **固化结论**：在**测试/诊断场景**下，AI 执行体**可以**自行：
- 以**管理员权限**启动 YMCC（`-Elevate`，UAC 自动提升）；
- **关闭** YMCC 及其本轮由测试拉起的伴生进程（宿主/恢复服务/输入宿主等），用于制造"干净前置"或让新构建生效；
- 之后按测试需要再启动一次并核对身份。

仍然**不得**（不在本授权内）：
- 关闭/重启**非测试目标**的用户程序（UU/GameViewer、浏览器、下载器等）；相关进程必须原样保留；
- 触发任何真实睡眠（永久禁令，三个真睡眠脚本已加硬闸）；
- 安装/替换安装位载荷、发布或改包（属发布/安装域，需单独窗口与身份流程）；
- 在 9950 上做 EC/微星真实写入。

判据：测试脚本启动前若需要"App 干净前置"，可先读本记录；满足上表即可继续，**不必逐次询问**。若动作会动到非测试程序或需要改安装位/发布物，仍然先问。

## 2. 测试模式（mock）架构纪律（同批 operator 裁决）

> "测试模式应该只允许 AI 代码调用，用户端口永远不可到达，修改它，导出器端如果也有问题一起修"

⇒ 固化结论：
- **mock 伪装握手只经显式 CLI 开关 `--mock-handshake`**（AI 代码/模拟器专用）；App 的用户路径（`src/bridge/fanHost.ts` 只传 `--real-backend/--allow-hardware-writes/--authorization/--confirm`）**永不触达**；
- exe 同目录遗留的 `mock-handshake.flag` **不再开启 mock**（只在宿主诊断里记 `mock-handshake-flag-ignored`）；
- 永久负例：`tests/api_contract.ps1` **AC13** —— 遗留旗标 + 不带开关 ⇒ `hostMode` 必须 ≠ `mock-handshake`（实测 `safe-no-hardware`）；
- 模拟器所有沙箱宿主启动点固定传 `--mock-handshake`（sleep 域/cycle/regressions/invariants/extremes/api_contract）。

## 3. 与安装位/正式导出的关系

- 正式包**按设计不含** mock 旗标；安装位被正式包刷新后，宿主进入**真装/无 mock**模式；
- 若某台机器因无风扇硬件而无法使用真实路径，属**产品侧**问题（应显式 Unsupported/只读，而不是终端 FaultLocked）——已登记并在报告里上报，不在本授权内自行改产品语义；
- 模拟器对宿主二进制做 mock 类测试时，用 **lane 载荷副本**（`YEMAN_SIM_PAYLOAD_ROOT`）而不是安装位，避免污染安装位。