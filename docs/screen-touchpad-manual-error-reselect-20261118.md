# 触摸板显式回选清错的原生回归

原生输入源短暂失败后，源和 HWND 可能已经恢复，但 `windowError` 仍保留。此时布局缓存都正常，同配置回选过去会跳过刷新，错误继续显示。本轮产品修补仅使显式回选携带旧错误时执行原有刷新验证；普通配置投影与自动恢复预算保持。

可复用工具：`tools/screen_touchpad_manual_error_reselect_selftest.cpp` 和同名 `.ps1`。

在项目目录执行，输出目录必须不存在：

```powershell
.\tools\screen_touchpad_manual_error_reselect_selftest.ps1 -OutputDirectory 'D:\Codex\TouchpadManualReselectCurrent'
```

工具启动自身的私有桌面子进程，使用真实正常 UI 输入源和独立输入通道。每种模式都通过零尺寸根窗口制造一次真实 `Surface.sync()` 失败，再恢复真实窗口与绑定；确认布局已正常、仅全局错误残留。随后检查被动配置不触发额外恢复，显式回选重新验证并清错，健康 source identity 保持。

退出检查包括三轮输入释放确认、Composition/DispatcherQueue 关闭、自然进程退出、Job 进程数归零和父进程关闭私有桌面。测试不注入指针或按键，不写设备，不启动 YMCC/Steam/游戏，不切换用户输入桌面，也不测 CPU 百分比。

`-TouchpadHeader` 可指定同一来源版本的修补前 `screen_touchpads.h` 作为反例，其余依赖由本次源码快照提供。准确的已知失败集合为：

```powershell
@('normal-explicit-reselection-clears-error', 'isolated-explicit-reselection-clears-error')
```

通过 `-ExpectedFailures` 指定这两项时，包装器只验证反例是否准确复现。原生报告仍保留 `ok=false`、退出码 1 和两项失败。当前实现要求原生报告 `ok=true`、退出码 0，且全部清理检查通过。

2026-11-18 本机验证：同一 19 项断言，当前实现全部通过，修补前头文件仅上述两项失败。正常与独立模式中，真实错误均为 `E_INVALIDARG`，源恢复后原错误保留；当前显式回选清为 0，反例继续保留原错误。原生头与编译依赖、可执行文件哈希、完整报告写入各自输出目录。

该回归验证源恢复后的错误状态处理，不能代替真实触摸投递、游戏全屏/睡眠后的现场验收或整体 CPU 测量。
