# 无虚拟手柄下的屏幕专用按键（2026-10-10）

## 本次范围
只接独立功能键，不放开或修改 SteamDeck / PS 原生触摸板映射，也不自动开启、切换或创建任何虚拟手柄。

| disabled 配置选项 | 左键 | 右键 |
|---|---|---|
| 开启Steam全部 | 切换 Steam 主菜单 | 切换 Steam 快捷菜单 |
| 开启PS5全部 | 切换 Steam 主菜单（PS / Guide 功能动作） | 切换默认麦克风静音 |

PS 独立模式不是原生 DualSense HID 报告，亦不是 PS Remote Play 输入。PS 静音使用 Windows 默认录音端点：优先通信麦克风，缺失时用普通默认麦克风；不把扬声器静音冒充麦克风静音。原有虚拟 persona 按键、触摸和背键报告保持原路径。

## 链路
透明按钮窗口 → buttonEvent → 有效 UP 点击 → standaloneDispatch → 已有 gamepadSerialSubmit 执行队列 → 独立功能动作。

- 独立按键不写 screenButtonMask，BUS / InputHost / HID 始终没有这次按键。
- Steam 专用键使用既有、经进程及端口身份校验的本地 Steam CDP 通路，直接调用 Steam 自身 OnHomeButtonPressed／OnQuickAccessButtonPressed 接收方法；不依赖 Decky 插件、控制器连接或虚拟设备。
- 触发是全局功能键语义，不读取当前前台 HWND／PID 作为许可，不强行聚焦 Steam，不自动切模式。Steam 自行路由并处理防抖、抑制及导航。
- 2026-10-11 已移除依赖 Steam 前台的 Ctrl+1／Ctrl+2 回退；原生入口不可用时报告失败，不向其他应用补发快捷键，也不修改 Steam 配置／开启调试。
- 按键送达与菜单响应分别记录；可能已送达的调用不自动重发、不改走键盘。完整边界及本轮验证见 `G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/docs/screen-standalone-steam-global-keys-20261011.md`。
- 麦克风切换使用 Core Audio API 并读回确认；无可用录音设备或拒绝写入时报告失败。

## 取消与错误
DOWN / MOVE 不触发功能动作；有效、命中且未取消的 UP 只执行一次。错误触点 ID、取消、移出区域松手、重复 UP 不执行。关闭开关、切换配置、缩放/布局变化、上下文变更会使排队动作及迟到回执失效。相同配置仅允许一个在途独立动作，防止重复菜单切换堆积。

沿用退出、快捷键录制及电源恢复门；该电源门不创建或要求虚拟输出目标。当前配置收到独立动作结果时，经 owner UI 消息更新 screenTouchpads.updated；错误文本显示在设置面板。动作失败不冒充设置保存失败。

## 验证入口与边界
在项目目录运行：

```powershell
.\tools\verify-screen-touchpads-standalone-special.ps1
node tools\screen_touchpads_ui_selftest.mjs
pnpm run type-check
```

验证脚本包含 184 个 native standalone/core case 和 22 个 Steam 生产脚本模拟 case，并检查脚本嵌入一致及源码哈希。覆盖四枚按钮的 dispatch、触点/取消、排队与配置代次、虚拟路径对照、键盘部分批次补发 KEYUP、麦克风失败与读回。另已通过 27 个原有 Steam live case、104 个光标 case、97 个产品 core case及产品 wire 自测。

本批完整原生候选编译链接通过。所有执行的输入/麦克风测试均使用模拟出口；只读核对了运行中 Steam 的 MenuStore API，没有切换真实麦克风、创建虚拟手柄或安装产品。尚未完成真实触屏点击 → 外部 Steam 菜单/真实麦克风的端到端验收，不能据模拟测试声称所有机器已验证可用。
