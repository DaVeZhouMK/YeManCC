# 开机模式的 Windows 启动边界（2026-10-24）

开机模式保存下一次 Windows 启动的场景。点击“设为开机模式”即提交目标及其已经编辑的预设，不要求再进编辑器保存。当前运行配置不热切；同次启动内的前端重建、YeManCC 重开和睡眠恢复不得重放下次启动草稿。推荐默认只用于初始化，不覆盖用户已经保存的关闭、Xbox、PS5、SD 或跟随全局选择。

## G3E 现场根因

用户的 `YeManCC-logs-20261024-190108-开机偏慢+小黄鸭1失败.zip` 显示：18:45:26 的系统运行计时为 1,673,578ms，18:46:18 已降至 17,171ms，而旧 `windows-loader-v1` 标识仍完全相同。已保存的启动边界因此被误判为同一次开机：原生继续保留关闭的当前手柄，前端场景恢复也直接返回。日志导出的配置已经是 `xbox-steam`、SD 推荐预设及 `startupDesired.virtualGamepadPersona=steamdeck`；失败不能归因于用户少按一次编辑保存。

该包与 20261024-183717 主线导出的原生磁盘 SHA256 匹配。这是导出时磁盘身份匹配，不能冒充原子加载映像证明。逐行证据和完整包身份见核心证据层 `BootMode-G3E-190108-20261024/FIELD-ROOT-CAUSE.json`。

此前夹具给策略注入互不相同的 boot 字符串，只读生产者检查也仅验证同次开机的跨进程稳定性，漏掉该设备上真实重启后标识重复的情形。原失败和过宽的“非全局一律 SD”断言撤回记录均保留。

## 当前实现

- `native/boot_mode_identity.h` 通过 `HKLM\SOFTWARE\YeManCC\Runtime\BootSessionV2` 的 volatile key 保存 GUID；命名 mutex 最多等待 5 秒。生产原生已有 requireAdministrator 清单。返回 `windows-volatile-v2|{GUID}`，在进程之间复用；缺失、无效或读取失败保留 error，不猜测新开机。
- `native/main.cpp` 只给开机模式更换生产者并记录 provider／created／error。GPU 专用显存的旧身份接口保持原样。
- `native/input_startup_policy.h` 对旧 v1 选择回执做一次保守锚定，升级当天不突变当前配置；新版本明确选择模式即写 v2 边界，下一次确认的新 Windows 启动应用已选预设。
- 当前版本验收流程为：使用包含新原生的新包，选择“设为开机模式”，然后完整重启 Windows；不需要编辑保存。直接刷新 H5 无法更新原生生产者。

## 配套界面与默认值

通用开机启动重新显示“全局桌面模式”，紧随“启动 YeManCC野蛮系统控制中心”。它复用原桌面计划任务。用户补充要求默认开启：首次／升级后未记录过该项选择时，应用启动阶段一次性补建下次登录任务并持久化 `startupDesired.desktopModeOnBoot=true`。页面读取不创建任务；明确关闭保存 false，后续应用重开不补回。四模式切换保留这项独立的通用设置。

`src/bridge/desktopModeStartup.ts` 串行化默认初始化和用户开关，保留设置代次，写偏好失败回退任务。已有任务只记录选择、不重建；失败不写成功标记。原任务只还原非平板状态，和四种启动场景不冲突，因此移除旧模式选择时删除该任务的逻辑。只准备下次登录，不在当前会话执行 VBS；主渲染器在隔离检查后异步初始化，不等待编辑器、硬件采集或页面加载。

窗口、标题栏、托盘和程序文件描述使用 `YeManCC野蛮系统控制中心`；单实例键独立保留 `YeManCC`，避免改可见名称造成重复实例。

Windows＋Steam 与 Xbox＋Steam 的 FxSound 推荐默认已为开启，本次日志两项也都为 true，未另加强制覆盖。用户明确关闭或自定义仍优先。开机场景被跳过会使这些联动没有执行，不能把保存 true 等同于音频实际生效。

编辑器进入前记录原焦点，B 退出恢复到“编辑配置”按钮；在正式组件及正式手柄引擎的 720P／800P 回放中核验。

## 验证与限制

继续使用原 `settings.boot-modes-runtime`、`settings.boot-mode-startup-boundary`、`settings.boot-modes-browser`、`settings.boot-modes-file-ui`，不新增父门。原生消费者包含 G3E 重复旧标识负例及新标识恢复；生产新标识函数在唯一 HKCU 测试键中跨进程读取并隔离删除后再观察，结束删除测试键，不写生产 HKLM。四模式实际 UI→文件→原生启动策略覆盖有／无配置、旧配置、16 种模式转移、自定义和失败恢复。

隔离删除测试键模拟的是 hive 生命周期，并非真实重启。完整物理重启、Fast Startup／混合关机后的行为、安装版驱动枚举、Steam 真正消费和实物手柄仍待现场验证；不得将隔离策略、H5 或语法通过写成这些验收通过。未知／无效标识采用保留当前配置，宁可给出缺证回执也不把应用重开误当开机。

后一次现场启动先出现 `module-update-pending`，随后 `virtual-gamepad.log` 持续 ACK SD，Steam 在 18:49:58 打开控制器并于 18:50:14 读取 Neptune 配置。该等待后来恢复，与前一次的开机边界跳过分开记录；此补丁没有完成原生模块更新等待或整体开机速度验收。

完整本轮证据与最新源码身份见 `G:/YeManCC-Work/Testing/YMCC/Evidence/BootMode-G3E-190108-20261024/REPORT.md` 和 `RESULT.json`。构建、已安装版本与实机结论分别记录。
