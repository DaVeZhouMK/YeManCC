# 游戏修改器后台准备与 GCM 统一管理

2026-10-14：YMCC 负责游戏识别、额度内的后台准备和玩家点击启动；Game Cheats Manager (GCM) 负责修改器文件管理。

## 玩家操作

- 游戏持续识别约 2.5 秒后后台准备，主窗口隐藏时仍工作。默认开启，可从设置或修改器二级菜单关闭。
- 顶部游戏快捷菜单 → 游戏修改器打开四项菜单：启动当前游戏修改器、打开修改器管理器、关闭/开启自动下载修改器、按B取消。
- B 接到实际手柄引擎的 `ipc:gamepad-back` 事件，捕获并消费事件，只关闭这一层并恢复入口焦点；取消按钮也支持鼠标和手柄 A 点击。Escape、点遮罩同样返回。
- 自动准备只下载，不启动程序。玩家点击启动后才检查原游戏进程身份、当前管理器文件和哈希；返回菜单会取消等待启动，保留已准备文件。
- 模拟器不自动下载，手动入口保留 Cheat Engine 行为。

## 共享目录和身份记录

- 修改器写入 `%APPDATA%\GCM Settings\settings.json` 的 `downloadPath`；未配置时使用 `%APPDATA%\GCM Trainers`。不修改 GCM 的设置和资源数据库。
- 风灵下载采用 `[风灵]《游戏名》修改器` 文件夹；`gcm_info.json` 使用 GCM 的 `game_name`、`origin: fling_main`、点分隔日期 `version`、`extension`，嵌套程序也能被管理器发现。
- 当前 GCM 目录是文件状态的权威来源。每次准备或手动启动重新查找，管理器更新/删除后的状态不会被 YMCC 旧索引覆盖。已有文件优先，同游戏优先风灵来源；保留其他作者文件。
- 旧 `trainer-cache/installed` 的 YMCC 风灵文件校验后复制入 GCM；旧文件保留为备份并写迁移标记，不再参与查找，不占新下载额度。
- GCM 文件夹下的 `ymcc-library.json` 记录目录内修改器清单、官方元数据路径/资源标识、可用 Steam AppID、游戏启动 EXE 完整路径与 SHA-256、规范游戏名及关联资源。尚未识别过的游戏不猜测 EXE 路径。
- EXE 路径命中但显示标题不同，需要 EXE 哈希一致才沿用绑定；同路径替换成另一游戏不能沿用旧绑定。首次匹配仍要求规范名称或本地翻译明确相等，保留续作数字。
- `app.dataDir()/trainer-cache` 仅作为请求、临时下载、目录缓存和状态索引。修改器最终位置为 GCM 目录。

## 下载预算

- 默认滚动 24 小时最多 **3 次下载尝试、累计 200 MiB**，单次文件最多 80 MiB且不超过剩余额度。
- 预算与流水持久化在 `ymcc-library.json` 的 `budget` 和 `downloads`；`budget.maxDownloads`、`budget.maxBytes` 可配置，0 会禁止新的下载。时间窗口固定24小时。
- 网络请求前先预约次数和字节额度；失败/取消也消耗次数，已传输字节计入额度，正常结束按实际字节结算。异常退出保留预约，24小时内不自动退回，避免重启重置预算。
- 达到任一限额返回 `budget-limited` 并冷却；手动重试不能绕过限额。启动已有文件、迁移旧文件不扣额度。记录损坏时停止新下载，不重置限额。
- 缓存目录锁和 GCM 共享目录锁串行工作，多个缓存/实例共用同一份额度。
- 名称无匹配冷却24小时，失败/取消冷却1小时；关闭自动下载不会删除已有文件。

## 实现与范围

- `src/bridge/gameTrainer.ts`：准备/启动分离、合并任务、队列、取消、目标与文件校验、玩家打开GCM。
- `src/bridge/gameTrainerAuto.ts`：稳定识别订阅、自动准备和设置开关。
- `src/components/GameTrainerMenu.vue`：二级菜单、焦点恢复和手柄B消费。
- `PowerControl/YeManTrainerPrepare.ps1` 与 `PowerControl/Trainer/TrainerBackend.psm1`：隐藏后台、GCM目录安装、元数据、身份记录、预算和迁移。
- 新下载仅使用风灵月影官方公开独立修改器；其他作者的现有 EXE/CT/CETRAINER 可复用。中文别名/Steam ID 使用本地 GCM 翻译缓存；缺失时不猜测。
- 网络限制为风灵官方 HTTPS 地址；限制文件大小、压缩包路径和文件数量，不遍历目录联接。校验不能代替当前游戏补丁的实际兼容性验证。
- `YeManGcmSearch.ps1` 仅保留旧版兼容，新链路不模拟 GCM 输入；管理器只在玩家选择“打开修改器管理器”时启动。

## 验证

```powershell
node tools/game_trainer_background_selftest.mjs
powershell -NoProfile -ExecutionPolicy Bypass -File tools/game_trainer_backend_selftest.ps1
node tools/top_menu_power_group_browser_selftest.mjs
node tools/global_cache_config_selftest.mjs
node tools/optiscaler_warning_ui_browser_selftest.mjs
node node_modules/vue-tsc/bin/vue-tsc.js --noEmit
```

证据目录：`Mainline/Build/Validation/TrainerGcmMenu-20261014`。浏览器测试执行真实 Vue 菜单及生产手柄导航/B处理函数，原生操作与硬件输入使用桩；不能宣称操作者已真实按下物理B键。部署只更新相关Web和后台组件，保留原生宿主、用户设置及其他运行组件。
