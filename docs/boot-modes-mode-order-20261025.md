# 开机模式顺序、手柄默认与主应用（2026-10-25）

> 用户同日再次澄清：本页“注册 Steam 主应用／AnyFSE”章节已撤回，Windows 按 Xbox 或无处理。最终启动矩阵见 `boot-modes-advanced-startup-matrix-20261025.md`；手柄默认、GIF 接口和三边光效部分继续有效。

用户明确数字 1–4 表示卡片顺序，以此修正此前理解。默认关系如下，仍支持各模式后续自定义；推荐预设不作为强制覆盖规则。

| 顺序 | 模式 ID | 名称 | 推荐手柄／触摸板 | 明确设为开机模式时的 Windows 主应用 |
|---|---|---|---|---|
| 1 | global | 全局 | 全局当前配置 | 保持用户原值 |
| 2 | xbox | Xbox 全屏 | 跟随全局 | Xbox |
| 3 | steam | Windows＋Steam 大屏 | SteamDeck 与完整 SD 触摸默认 | 已注册的 Steam 大屏入口 |
| 4 | xbox-steam | Xbox 全屏＋Steam 大屏 | SteamDeck 与完整 SD 触摸默认 | Xbox；Steam 继续走已有联动 |

主线顺序不变，数字不需要出现在产品文案中。开机模式仍在下次 Windows 启动应用手柄／鼠标场景，点击“设为开机模式”即保存，不额外要求编辑保存。主应用和系统启动选项属于该次明确操作的开机准备，不启动当前应用、不热切手柄。

## 默认与升级

`src/bridge/bootModeModel.ts` 初始化 2 号无独立 input／touch override，3、4 号为 SD 推荐配置。`controllerDefaultsVersion=1` 记住本轮升级，后续用户选择跟随全局不会被重新补回。

对之前自动生成的 2 号最小 SD recipe，逐字段与旧 factory 比较，完全相同才撤销该 module override。比较忽略原生 JSON 对象键排序。旧 3 号缺少模块时一次性补 SD。已经存在的其他手柄模块、关闭、PS5、自定义值保留。旧版本没有追踪“亲手选了与 factory 完全相同的最小对象”与“自动生成”的来源，无法区分的精确 factory 形状按本次用户纠正处理；不声称能还原缺失的编辑历史。现版本下的后续自定义和跟随全局优先。

## Windows 选择主应用

`GamingConfiguration\GamingHomeApp` 以前只是读取后保留，导致启动开关改了但主应用仍是原值。现在 `chooseBootMode` 明确传递目标：2／4 为 Xbox，3 为 Steam，1 不处理。关闭兼容请求不额外改主应用。

`gamingHomePolicy.ts` 在原备份、回读及有条件回退事务里处理可选 AUMID 字符串，失败不冒充成功；正常全屏开关的旧调用未传主应用时继续保留原值。Xbox 目标为本机实际清单确认的 `Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App`，不以连接或机型作为修复前提。

3 号通过 `PowerControl/Read-SteamGamingHome.ps1` 一次只读发现已注册 `windows.gamingApp` 入口，不安装／启动任何应用：Valve Steam 注册入口优先，然后已有 YeManCC.GamingHome，再是当前 AnyFSE。AnyFSE 必须已经配置为本登录用户 SteamExe 对应的可执行文件且参数为大屏，才能作为 Steam 入口；不能把其他 launcher 的 AnyFSE 认作 Steam。没有有效入口时，照常保存预设并关闭 Xbox StartupToGamingHome，保留未完成主应用的提示，避免写不存在的 Steam 字符串。仅 Steam 保持 DeviceForm、设置 StartupToGamingHome=0；2／4 继续修复 DeviceForm=46 和 StartupToGamingHome=1。

本机本轮只读观察找到已配置 Steam 的 AnyFSE，耗时约一秒；未修改实际注册表或系统设置。切回 3 的完整真实系统行为仍需新版本实机验证。

## 四张 GIF 接口

`src/bridge/bootModePreview.ts` 的 `BOOT_MODE_PREVIEWS` 有四个固定键，每个提供 src／fallback。后续将素材放到 `public/boot-modes`，把相应 src 改为 `/boot-modes/name.gif` 即可；保留 fallback 静图。全局默认 src 为空，使用原 Windows 展示，填入 GIF 后同样显示图片。

内页和编辑器共用 `BootModePreview.vue`；只显示当前模式，不额外打开四个动画。加载失败使用静图，再失败用模式图标／文字；不以动画替代可达按钮，当前未加入 GIF 成品。

## 静态三边光效

`src/styles/bootModeGlow.css` 共用于内页四卡和编辑器四卡。选中、正式手柄焦点、键盘焦点和鼠标悬停显示相同左／底／右三边光线，向按钮内缩 2px 并使用内阴影，避免父容器裁剪最外两边。没有呼吸或循环动画，不改变卡片尺寸。浏览器检查按真实 YMCC 缩放换算物理像素，覆盖第一个与最后一个卡片、两种分辨率及所有交互态。

## 证据边界

复用原 runtime／browser／file-ui 及 early_start_service 原消费者，不增加新父门。原失败回执保留，针对改变的输入重跑；文件 UI 仍检查无配置、旧配置、自定义、16 种转移和实际文件启动策略。修正“同一个模式已被选中”的测试等待：必须等本次保存结束，不能因旧 mode 值相等而提前读取文件。

最终计数与源码哈希见 `G:/YeManCC-Work/Testing/YMCC/Evidence/BootMode-ModeOrder-20261025/REPORT.md` 和 `RESULT.json`。浏览器手柄回放不等于实物手柄；隔离注册表消费者不等于已部署系统设置。本轮不安装／重启产品、不运行启动任务、不操作现场驱动。
