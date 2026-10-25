# 触摸板与屏幕按键 CPU 链路优化（2026-10-14）

本轮直接修改主线的三个原生头文件。目标是减少每个触点事件中的重复状态计算、锁和窗口查询；没有改变触摸采样、输入报告或显示刷新节奏。未运行测试、编译、部署或 CPU 采样；下面的次数是源码路径比较，不是实测 CPU 百分比。

## 原链路与本轮改动

输入面 → handlePointer / handleButtonPointer → 手势/按键状态 → 震动边沿门控及输入输出 → 按下轮廓/触点光圈 → 原有 BUS 报告。

| 位置 | 原开销 | 主线调整 |
| --- | --- | --- |
| Core::event | 成功 DOWN/MOVE/UP 先汇总旧状态，再汇总新状态 | 成功事件只在修改完成后汇总一次；无效参数、重复落指、错误 ID、传输阻塞仍返回完整当前状态 |
| handlePointer | 配置单独取锁，随后路由判断再取同一锁 | 配置与路由在原有路由锁内一起读取；手势执行时的传输门控保留 |
| output | 震动处理后，即使无键鼠变化也清零 INPUT[12] 并扫描 8 个按键位 | 无键盘变化、鼠标位移、点击变化或 tap 时提前返回；震动发布在早退之前，因此不会省掉脉冲或抬指释放 |
| handleButtonPointer | 专用键路径同一事件多次 getConfig；唤醒、反馈、映射重复计算圆角命中 | 使用一次事件局部配置及命中结果；映射 helper 仍在执行前重读目标/Profile 准入原子值。buttonEvent 原签名作为兼容入口保留 |
| renderButton | 无虚拟目标时，为图标模式与缩放各读一次配置 | 使用同一份局部配置读取两个字段 |
| syncContactVisuals | 每次光圈 MOVE 重新执行 ensureDisplay 的有效窗口、启用状态、扩展样式查询 | 创建/显示、上传、强制刷新或抬升前校验；原 WM_STYLECHANGED/WM_ENABLE 处理继续修复，普通已显示 MOVE 仅更新所需位置 |
| 同面双触点光圈 | 两个触点分别查询同一输入面及轮廓可见性、窗口范围 | 在单次 syncContactVisuals 内共享查询结果；下一次事件重新查询，没有跨事件几何缓存 |

普通已显示、未换图的单个触点光圈 MOVE 路径，原 ensureDisplay 成功路径包含两次 IsWindow、两次 IsWindowEnabled 和两次 GetWindowLongPtrW；该重复检查从这个路径中移出。触摸输入 HWND 的穿透约束和窗口处理未改。光圈前景检查、坐标计算、父面可见性、即时目标准入、失败隐藏与恢复路径保留；所有者刷新仍可抬升光圈。

## 等价性与低 CPU 边界

- 不通过跳过触摸事件、延迟/合并输入报告或降低刷新实现节省。原始坐标、鼠标余数累积、键位 Schmitt 判定、tap 判定与 source time、UP/cancel、错误 ID 拒绝逻辑保留。
- 原 output 的 SendInput 组装顺序、部分失败后的潜在按下状态记录及补偿释放逻辑保留。新增早退条件与原最终 n==0 路径对应，且仍先发布震动状态。
- 按键新增帮助函数只共享配置与命中结果，点击派发仍在原 UP 分支；自有 UI 前景抑制、standalone Steam/PS 路由、输出目标实时检查保持。
- 三档震动、18ms 脉冲、45ms 最小间隔、80ms 队列期限、距离步进和取消逻辑没有修改。后台 EventGate、固定邮箱、设备 lease、退避与写前身份检查已经减少了重复工作，本轮未为少量收益改动这些故障边界。
- 光圈图像与窗口仍复用；没有加入线程、计时器、逐 MOVE 文件日志或额外堆分配。主线 cursor policy 7 和此前 InputHost raw-report 优化保留，本轮没有改动这两条链。

## 官方依据与审查范围

微软 WM_STYLECHANGED 文档说明样式改变后向窗口发送消息；WM_ENABLE 在 EnableWindow 返回前、启用位改变后发送。已有 contactVisualProc 对这两个消息执行 ensureDisplay 并在无法保持约束时隐藏窗口。本轮复用这一即时修复路径，并保留显示/抬升/换图前复核。资料为 MicrosoftDocs 官方仓库内容，URL、下载时间与 SHA-256 保存于留档 Sources/manifest.json；web 查询空返回没有当作证据。

静态复核覆盖正常输入、拒绝/取消路径、键鼠早退与震动顺序、按钮准入、样式修复、同面双触点及配置刷新。源码差异与空白检查不等于运行测试或手感验证，无法给出实测 CPU 降幅，也不证明先前 SteamDeck 光标闪烁已解决。

## 交付

生产文件：native/screen_touchpads.h、native/screen_button_overlay.h、native/screen_touchpad_contact_visual.h。研究与交付留档：G:/YeManCC-Work/Build/Analysis/ScreenTouchpad-Button-CPU-20261014。
