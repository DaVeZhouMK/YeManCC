# 屏幕触摸板：游戏切换后的原生鼠标穿透约束（2026-10-12）

## 范围与结论

本轮对照监控新增的原生约束，检查触摸板的显示、游戏切换置顶恢复、样式／启用状态变化、隐藏再显示和恢复重绑定。
修补已在源码完成；没有部署／替换安装产品，没有启动真实游戏，没有移动鼠标或切换用户可见桌面。
触摸板映射 Core 和受控指针消费代码与本轮前逐段相同，反馈档位／算法、坐标、短点按、按键语义、数据层和 BUS 单写者均未改变。

不能把纯显示监控的整窗禁用策略直接套给触摸输入窗口。

| 层 | 本轮约束 |
| --- | --- |
| 触摸输入 host | 保持 enabled；CoreIndependentInputSourceController 继续只控制 Touch/Pen，IsTransparentForUncontrolledInput 保持 true；NOACTIVATE 和 alpha-zero layered 输入位图在显示／抬升前重新验证；不能设置 WS_EX_TRANSPARENT 来同时放弃触摸 |
| 边框／功能键图标 | 原生 disabled，LAYERED／TRANSPARENT／NOACTIVATE／TOOLWINDOW；WM_STYLECHANGED／WM_ENABLE 自动修复；分层样式丢失后重新上传原来的 per-pixel 位图 |
| 触摸鼠标的可视光标 | 纯显示层 disabled／TRANSPARENT／NOACTIVATE；样式／启用状态变化自动修复，不更改下面应用的光标形状 |

这些修复只在已有窗口事件、刷新和游戏置顶恢复路径中工作，不新增轮询、定时器、线程、鼠标 hook、点击转发或抢焦点。
普通游戏置顶恢复复用位图缓存，不重建输入源，也不因置顶操作主动清掉现有触点。
显式 legacy 测试／兼容开关仍保持原语义；本轮保证的是正常 native 默认链路，而不是旧的非原生输入实验路径。

## 找到并修补的缺口

1. 旧 restoreScreenLayerPair 只恢复置顶，没有重新验证输入窗口／视觉窗口的原生约束。新路径先校验，安全后才抬升；校验失败隐藏这一对窗口并释放映射状态，不显示一个可截获鼠标的普通矩形。
2. 旧触摸板 refresh 忽略 nativeInputSync 返回值。原生绑定失败会关闭 source 并恢复旧非分层窗口样式，随后仍可能 ShowWindow。新路径使用 inputReady 控制输入与边框显示，失败不退回到不穿透的 host。
3. 旧 Surface 的 layered_／uploaded_ 是内部记忆，不等于窗口实际样式仍正确。现在重新验证 host 的实际样式、enabled、child NOACTIVATE、uncontrolled-input transparency；必要时重新上传 alpha-zero 位图。
4. 外部 SetLayeredWindowAttributes 会使之后 UpdateLayeredWindow 受影响，不能只信上传缓存。发现这类状态时清除再恢复 LAYERED，并重建原来的 alpha-zero 输入位图。视觉层不使用 SetLayeredWindowAttributes，不破坏 per-pixel 绘制机制。
5. 关闭 source 时禁止样式修复回调重新建立穿透，避免 teardown 与 WM_STYLECHANGED 互相递归；失败与重配置仍沿用原取消语义。

## Windows 隔离复现与验证

使用真正的 Windows HWND、两个独立下层进程、生产 initializeForOwner／refresh／restoreScreenLayerPair 等代码。
所有窗口运行在 CreateDesktop 创建且从未 SwitchDesktop 的隐藏桌面。
WindowFromPoint 判定鼠标目标；没有 SendInput、InjectTouchInput、可见桌面切换、实际鼠标移动或生产点击转发。
映射触摸使用生产 nativeInputSink 的模拟事件，不冒称为物理手指／触摸屏验收。

64 轮组合覆盖：

- 单面鼠标、双面键盘＋鼠标、双面 SteamDeck、单面 PS5。
- 80／100／120% 缩放，0／50／100% 透明度。
- 两个下层进程交替前台／置顶，恢复之后再切换。
- 输入 host 的 LAYERED／NOACTIVATE／TRANSPARENT 状态变化、enabled 变化，child 的 enabled／NOACTIVATE 变化。
- 视觉层的 TRANSPARENT／NOACTIVATE／LAYERED 丢失及重新启用。
- 常量 alpha API 干扰、隐藏再显示、恢复重绑定、子 HWND 丢失造成 native 初始化失败。
- 中心、边、角及功能键区域命中；普通切换不重建 child，不重复上传输入位图，映射触点保持／释放。

同一夹具对冻结前后源码验证：

| 版本 | 结果 |
| --- | --- |
| 本轮前 | 复现 142 项失败、1,348 次命中中有 112 次不是目标下层窗口；原生刷新／抬升失败后隐藏的断言均失败 |
| 本轮后 | 64 轮、1,038 项断言、1,600 次原生命中全部通过；下层目标进程均正常退出 |

两个版本的总检查数不同，是旧窗口丢失／隐藏后无法进入部分“可见功能键”检查分支；不能用不同分母计算提速或成功率提升。

补充回归：

- 映射 Core／受控 pointer 消费不变、修复不抢焦点／不注入等源码边界 12 项。
- 原生 standalone／Core 195 项、Steam 专用键边界 45 项。
- 触觉状态 legacy／native 构建各 274 项，均没有物理马达写入。
- 监控原来的原生约束夹具：64 轮跨进程命中、529 项断言通过；监控源码没有被本任务修改。
- 隔离 main.cpp 集成编译通过；只生成验证用 obj，没有链接或替换正常产品 EXE。

## 来源、复跑与边界

源码：

- `G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\screen_touchpad_display_policy.h`
- `G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\screen_touchpad_input_surface.h`
- `G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\screen_touchpads.h`
- `G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\screen_button_overlay.h`
- `G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\screen_touchpad_cursor.h`
- `G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\screen_touchpad_visibility.h`

复跑工具：

```powershell
& 'G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\verify-screen-touchpads-mouse-pass-game-switch.ps1' `
  -ValidationRoot 'G:\YeManCC-Work\Build\Validation\ScreenTouchpads-MousePass-GameSwitch-20261012\Final' `
  -BaselineNative 'G:\YeManCC-Work\Build\Validation\ScreenTouchpads-MousePass-GameSwitch-20261012\BeforeSource'
```

证据目录：`G:\YeManCC-Work\Build\Validation\ScreenTouchpads-MousePass-GameSwitch-20261012`。
Final 的冻结头文件、before／after 报告、来源 SHA256，以及 MainCompile 的源码快照、日志、obj 哈希可用于核对。

微软一手 API 依据：CoreIndependentInputSourceController 的 IsTransparentForUncontrolledInput、SetControlledInput；EnableWindow；Using Layered Windows 中的透明像素命中说明与 SetLayeredWindowAttributes／UpdateLayeredWindow 兼容性约束。

边界：没有真实商业游戏／独占全屏验收，没有物理手指、笔或掌机震动验收，也没有运行完整安装产品。隐藏桌面原生命中和模拟 sink 不能被描述为真实游戏鼠标点击或真实触摸硬件通过。

### 并行工作区说明

本轮完成后其他并行任务继续修改 Steam CSSLoader 头文件和 main.cpp。本轮没有覆盖或回滚这些变化。
Final 的实际触摸板夹具依赖闭包与本轮写入集哈希仍一致；main.cpp 中的 screenpads 调用行与集成编译快照一致。
MainCompile 只证明记录的冻结快照编译成功，不冒称随后整个活动工作区已重新编译或安装版已更新。
