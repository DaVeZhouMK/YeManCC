# 四模式 Windows／Steam 高级启动最终合同（2026-10-25）

本页取代同日“注册 Steam 主应用／选择 AnyFSE”解释。用户说的 Windows 主页设置按原 Xbox 启动开关处理：Xbox 或无，不注册或选择 Steam AUMID。之前增加的 Read-SteamGamingHome.ps1 已从正式源码删除，历史原件保留在本轮 before 证据。

| 顺序 | 模式 | Windows Xbox 启动 StartupToGamingHome | OEM DeviceForm | Steam 高级启动 |
|---|---|---:|---|---|
| 1 | 全局 | 不修改 | 不修改 | 不修改当前设置 |
| 2 | Xbox 全屏 | 1 | 自动修复为 46／0x2e | 关闭本程序 .earlystart 命令，不在选模式时执行 BAT |
| 3 | Windows＋Steam 大屏 | 0 | 自动修复为 46／0x2e | 写原 BAT 命令，确保 EarlyStartService 自动并运行 |
| 4 | Xbox 全屏＋Steam 大屏 | 1 | 自动修复为 46／0x2e | 同 3，叠加 Xbox 启动 |

2 的手柄默认跟随全局；3／4 推荐 SD 和完整 SD 触摸配置。既有独立自定义和一轮默认纠正保持。开机模式依旧在下次 Windows 启动应用手柄／鼠标场景，选择即保存，无额外编辑保存。Windows 启动开关、.earlystart 和服务的准备是本次明确模式操作；不热切当前手柄，不启动 Steam／Xbox／BAT。

## 对照旧 Git

已有固定 v0.0.35 证据五文件逐个重新核验 SHA256 与 Git blob SHA1，commit c2e5844aa57e28ee3dbd8c8c813161ccc93b1918。旧 yeman.ts 的 STEAM_EARLYSTART_CONTENT 精确为 `"C:\SOFT\YeMan\PowerControl\YeManSteam.bat"`，保存到当前用户 .earlystart。没有运行旧代码或退回整个旧版本，保留当前主线的服务检查、文件读回、输入边界及并行任务。

生产 Native 使用当前登录账号 profile，用户当前路径为 C:\Users\DaVe\.earlystart；不能把开发者账号硬编码给其他机器。文件为上述带双引号的 BAT 路径加 CRLF。用例通过真实 Win32 文件 IO 在两个隔离临时 profile 验证路径作用域和内容。

## 修复与静默行为

`setGamingHomeStartup` 新增 repairHandheld 参数：2／4 为 true/true（46/1），3 为 false/true（46/0）。普通 Xbox 总开关关闭默认仍只改启动值，保留其原语义。GamingHomeApp 只作为现有快照内容读取／备份，四模式不再写它，也不查找安装应用。注册表修改保留备份、回读与失败回退；已有未知类型／权限错误如实留下非模态状态，不伪造成功。

3／4 调用原 steam.startup.set，带明确 repairCommand=true。先提交 .earlystart，再确保既有 EarlyStartService 为自动启动且运行，等待最多 2.5 秒；不安装新服务、不改服务程序路径、不停止共享服务、不重启 Steam、不无限重试。2 也使用明确修复标记：先备份再清空陈旧命令，保留共享服务，不停止服务。1 不进入任何 Xbox／Steam／旧启动清理分支，原设置全部保持。

明确选 2／3／4 时，若 .earlystart 是陈旧或其他命令，先原字节复制为 `.earlystart.ymcc-before-<pid>-<tick>-<sequence>`，再原子清空（2）或写入用户明确要求的 BAT 命令（3／4）并精确读回；备份失败不覆盖。普通 Steam 高级启动开关不带 repairCommand，保留原自定义命令冲突保护。序号避免同一计时刻连续切换时备份重名；这个修复不弹确认窗口。

四模式准入不再依赖 Steam／Xbox 实时连接，也不弹连接确认框。无权限／服务未安装／读回失败仍保存在 bootNotice 和页面状态中，便于排错；“无弹窗”不等于把未完成标成成功。只读页面不写注册表、文件或服务。

## 界面与验证

四个 GIF 素材槽、统一预览组件和静态左底右三边光效保持，不动已通过布局。原 conditional performance／gyro 控件回放与手柄规则保留，旧连接弹窗测试改为四模式断连也不弹窗保存，未通过删除原性能／陀螺仪用例节省检查。

本轮复用原 settings.boot-modes-runtime、settings.boot-modes-browser、settings.boot-modes-file-ui 与 early_start_service_selftest。新增真实 .earlystart 文件检查在原消费者内调用生产 Startup 代码，不启动真实 BAT／服务。最终计数及源码身份见 Testing/YMCC/Evidence/BootMode-EarlyStart-Matrix-20261025 的 REPORT.md 和 RESULT.json。生产 main.cpp 需重编，因为 steam.startup.set 准入增加可选 repairCommand；前端和原生必须配套。

本轮未替换安装版、修改本机生产注册表／服务／.earlystart，未执行真实重启。安装版、真实控制器与实际开机消费仍需新包现场验证。
