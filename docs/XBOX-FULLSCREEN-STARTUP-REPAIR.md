# Windows 全屏体验自启动修复（2026-10-09）

## 目标与参数

用户在 ROG / Windows 26H2 / 26300.9457 上报告以下修复有效：

- HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\OEM：DeviceForm = DWORD 46。
- 当前用户 HKCU\Software\Microsoft\Windows\CurrentVersion\GamingConfiguration：StartupToGamingHome = DWORD 1。
- 不修改 GamingHomeApp；Xbox、AnyFSE 等现有主页选择保持不变。

这组值是“用户主动开启时”的默认配置，不是在安装、页面打开、刷新或启动应用时强制写入。用户的历史故障来源仍未确定，源码修复不等于证明历史故障由 YMCC 导致。

## 修复范围

1. 开机启动页面读取真实注册表状态，不用旧 Xbox 任务存在与否推断系统模式。
2. 页面挂载、激活、刷新和读取失败不关闭 StartupToGamingHome，也不改变任务栏常驻。
3. 全屏启动、Steam .earlystart、任务栏常驻分别控制；系统全屏不依赖 Steam 或 Xbox 应用名称。
4. Steam 启动脚本不再强制结束 XboxPcApp.exe。
5. 缺少掌机标识时只为 BIOS/DMI 确认的 ASUS ROG Ally 修复；其他已由 OEM 设置 DeviceForm=46 的掌机也可开启。普通电脑身份未确认时拒绝修改。
6. 关闭开机进入模式仅写 StartupToGamingHome=0，保留硬件标识和主页。
7. 修改前生成 JSON 备份，逐值回读；写入失败时按原始值或原始缺失状态回退。回退不删除整项，不覆盖已经被其他程序改成不同值的项目。
8. 只在用户主动设置时备份并清理旧 physpanel 任务；旧任务不再纳入默认创建／恢复列表。
9. 兼容 XML 改为调用 Repair-GamingHandheld.ps1，仅在识别的掌机上设置 HKLM DeviceForm=46；SYSTEM 不写 HKCU 自启动。无需该任务也能使用新方案。

## 验证

从仓库根目录运行：

```powershell
node tools/gaming_home_startup_selftest.mjs
node tools/input_startup_policy_selftest.mjs
powershell -NoProfile -ExecutionPolicy Bypass -File tools/fan_startup_defaults_selftest.ps1
node node_modules/vue-tsc/bin/vue-tsc.js --noEmit
```

新测试覆盖 22 项：只读刷新、正确默认值、主页保留、关闭保留硬件身份、桌面拒绝、幂等、备份失败、写入失败、按缺失状态回退、并发外部写入、真实 Vue 页生命周期／开关、实际桥层写入串行化与旧任务备份／退役等。测试使用模拟注册表和源码检查，不执行实际系统配置修复。

工作区前端可构建至独立验证目录；不自动部署安装目录，不改当前开发机的注册表或任务计划。真正的掌机登录／重启验证需在安装包含此修复的完整兼容版本后由用户完成。


## 后续界面文案与任务栏常驻关闭修复

- 全屏卡片标题改为「Xbox全屏游戏模式」，开关改为「开机进入Xbox全屏游戏模式」。
- 删除该开关下的 Xbox / AnyFSE、DeviceForm=46、StartupToGamingHome=1 技术描述；不改变实际参数与主页保留逻辑。
- Steam 卡片标题改为「Steam大屏开机启动」，说明改为「开启的是Steam大屏页面的联动模式」。
- 原生 tray.setResident 返回应用后的 resident 状态，而非操作成功标志；成功关闭返回 false。前端原先用 !applied 判断失败，导致关闭误报、偏好被回退。改为 applied !== requested 才判定失败。无需修改原生协议。
- UI 关闭成功不显示错误条；真实返回状态不一致、接口异常、持久化失败仍报错并按原逻辑回退；写入期间禁用开关。
- 模拟测试先复现原来的关闭误报，再验证修复：新任务栏测试 11 项，全屏／真实 Vue 页面及桥层测试扩展为 25 项；现有输入启动 10 项、严格内存配置 46 项均通过。类型检查与独立前端生产构建通过。
- 仅检查源码、使用模拟设置／IPC 测试，未读取本机运行状态、系统注册表、任务计划或安装版运行数据；未执行系统配置修改，未部署安装目录。远端实际验证需包含该修复的新版本。

新增测试命令：

```powershell
node tools/tray_resident_selftest.mjs
node tools/gaming_home_startup_selftest.mjs
```
