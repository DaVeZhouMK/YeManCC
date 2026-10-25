# 屏幕触摸板：卡死隔离、CPU 优化与接触光点（2026-10-13）

状态：源码修改完成；本轮按用户要求未测试、未编译、未部署、未发送实机输入。以下明确区分代码证据、故障推断和未验证的效果。没有本次漂移发生时的新日志或复现，不能把“持续向一个方向移动”归因成唯一原因。

## 1. 卡死后漂移：已确认的缺口与修正

当前链路是 Windows 触摸事件 → Core 固定两触点状态 → BUS 快照 → InputHost → 虚拟报告 → Steam Input/游戏。鼠标映射分支只在真实 MOVE 回调有非零相对增量时调用 SendInput，没有自行持续移动鼠标的循环；显示系统光标的 fallback 不注入鼠标移动。

发现的明确缺口：原生触摸输入主要看 Persona/目标的配置可用性，没有跟随 Host 的故障状态、输出许可和目标 epoch 撤销旧接触。故障期间保存在 Core 的坐标或尚未消费的短点击，存在被后续帧继续使用的路径。

本轮修正：

- BUS 在原有 g_inputHostMx 内，按该次提交的 Host 就绪状态与 epoch 观察触摸状态，再取快照。没有增加第二个报告写入者。
- Host 停止/清理、管道故障、提交拒绝、电源禁写、物理源暂停、快捷键录制、后端故障保持、等待回中、Host 未运行等路径，撤销原生触摸输出许可。
- 许可撤销、重新就绪或 epoch 变化时清除原生接触和待发送点击。旧的物理接触转入仅反馈路径，保留震动/视觉到抬指；即使 Host 恢复也不把这根仍按住的手指重新映射。需要抬起后重新落指。
- 恢复后以事件发生时间拒绝恢复前排队的 DOWN；UPDATE/UP 不创建新接触。状态边沿去重，不为重复故障通知反复清理。
- 这些操作使用现有触摸互斥量；BUS/生命周期线程只改状态、PostMessage 通知 UI，不绘图、不发送键鼠。UI 消费通知时重新读取当前状态，避免迟到通知取消更新的手势。锁顺序为 Host → 触摸；触摸锁内不回调 Host、震动 sink 或 SendInput。
- 导出状态加入 nativeTransportObserved/Ready/Epoch/Changes、nativeDiscardedGestures，便于后续日志区分目标状态和接触丢弃。

**边界：这修正的是旧手势的本地保存和恢复发布。** 如果 InputHost/驱动已经卡住并留下最后一份非零报告，下游仍可能持续消费那份报告（包括摇杆映射成鼠标）。当提交调用本身不可用时，此层无法证明中性帧已经送达，也不能承诺立即停住。故障感知仍依赖既有提交回执/超时与生命周期；未增加 watchdog、独立中性写入线程或强制卸载目标，未修改 InputHost 的故障保持策略。

## 2. 已实施的 CPU 改动

| 原开销 | 本轮源码改动 | 范围和代价 |
| --- | --- | --- |
| 每次 MOVE 为轻点分类计算距离开方 | 记录最大位移平方，和阈值平方比较；WASD/方向键 MOVE 跳过该分类 | 保留移动后返回原点也不能轻点的规则；触觉 Motion 参数不变 |
| 按下/松开亮度改变时创建 DC/DIB、重算整个边框图案 | 每侧保留一张全强度预乘 Alpha 位图，只在尺寸改变时重建；亮度使用 SourceConstantAlpha | 减少 CPU 光栅化及 GDI 分配；仍需分层窗口上传，不能说合成零成本；以固定的两张位图换取复用 |
| 边框生成遍历全图做形状计算 | 清空背景后只计算窄边条和十字线 | 清零仍是 O(宽×高)，昂贵形状计算约为 O(宽+高)，仅重建时运行 |
| 独立输入源每回调再调用 GetPointerInfo 查时间 | 直接用当前 PointerPoint.Timestamp 的开机相对微秒，转换为 DWORD 毫秒 | 少一次 Win32 查询，同时避免排队后读到其他消息的时间；旧 WM_POINTER 路径保留自身的 GetPointerInfo |
| 新增接触光点可能引入整板重绘 | 最多两张 72–288px 位图（后续按用户参考图放大），生成后缓存；MOVE 只在整像素位置变化时 SetWindowPos | 无动画/轮询线程或新 timer；窗口/API 合成仍有成本，未测量净 CPU 变化 |

未启用或不可用的原生触摸快照保留提前返回，不为每个正常空闲 BUS 帧新增触摸锁。活动原生快照复用既有触摸锁完成就绪检查；故障边沿的独立撤销也使用这个锁。没有为每个 MOVE 增加日志、文件写入、JSON 或堆队列。

只读观察了用户已经运行的旧进程：PID 27320，32 个逻辑处理器，1537.589 秒内累计 1578.25 CPU 秒，折合 **单核 102.64% / 全机 3.21%**；采样结束时未见 InputHost。样本不是本轮新代码，也未控制用户交互、UI、传感器等负载，所以既不能归因给触摸板，也不能当成本轮优化后的结果。线程级读数为空，未用于归因。原始数据在本轮 Analysis 目录的 cpu-observation.json。

后续若允许性能验证，值得优先比较按下/松开的 GDI 分配与光栅化成本，以及整进程 BUS、WebView、传感器/监控的 CPU 归属；本轮没有用历史纯 Core 微基准代替这些数据，也没有为追求数字降低触摸采样率或改变震动算法。

## 3. 移动=滑动、静止长按=点击：本轮不启用

重新联网核对了 Microsoft GestureRecognizer.Holding 和 Valve Steam Input Activators。Windows 提供 Started/Completed/Canceled 的长按识别；Valve 文档提供 Long Press 软件激活器。这说明软件长按可行，并不提供 SteamDeck 压感硬件的完整点击阈值算法，也不能把例子中的 0.15 秒当成 YMCC 的推荐时长。

要可靠替换现有轻点，需要统一触摸源事件时序、按指针/表面隔离的取消语义，以及“一旦滑动即锁定到抬指”的状态。还必须明确长按发一次点击还是保持按下，并防止 UI/BUS 卡顿后把迟到 DOWN 算成长按。现有 Core 没有完成这套原生长按接入；本轮不测试的条件下，不能声称已验证可靠。遵从用户“明确再改”的要求，**不新增长按、不删除短轻点、不改为边按边拖**。

此前已经实施的 Deck 普通接触 pressure=0、新 DOWN 清除上一轻点、投影缓存包含模式继续保留；本轮只把位移分类改成平方计算并修正输入事件时间来源。最小/中/强反馈 10/20/30%、新配置默认中档和现有震动节奏不变。

## 4. 细十字线与按下光点

按参考图增加 1px 中央十字线，蓝色约为边框 Alpha 的一半；背景继续透明。边框与十字同图缓存，不逐 MOVE 重绘。

接触期间显示青蓝色实心光点与柔光，随手指位置移动，UP/取消/关闭后隐藏。只对应已接收的物理触点，不等待原生 click 位，也不等于生成点击。Sony 单板可显示两个接触点；仅反馈路径也有光点。光点在边缘向内限制，避免越出面板。外观跟随现有按下可见度 ×1.5、最大 100% 的规则；保存为完全透明时仍完全不可见。

光点使用现有显示层约束：WS_DISABLED、WS_EX_TRANSPARENT、WS_EX_NOACTIVATE，拒绝鼠标/指针激活；只展示，不更改输入面板和鼠标穿透机制，不修改前台焦点。图案按尺寸/透明度需要上传，移动只调整坐标；尺寸重建、owner 销毁、配置切换和 shutdown 有对应清理。新增 outlineBitmapBuilds、contactVisualUploads、contactVisualMoves 诊断计数。

2026-10-13 后续按用户截图调整尺寸与晕光：参考图 Windows 白色圆环直径约 80px，在触摸板 100% 缩放时将蓝色主体直径设为 104px（约 +30%），外围晕光延伸到 144px。这是该截图的比例基准，不是读取 Windows 当前系统设置，也不保证不同 DPI/系统触摸视觉选项下恒为 +30%。主体和晕光均随现有 50%–200% 触摸板缩放；主体为 52–208px、含晕光位图为 72–288px。晕光从主体边缘向外按平滑曲线衰减，源 Alpha 峰值 210，主体 245，最终仍乘现有按下透明度。相比旧的 28px 位图和仅约 11px 主体，主体和可见晕光都放大。两张最大位图像素内存合计约 648 KiB；重建绘制只发生在创建/尺寸变化，MOVE 不增加逐像素计算。本次仍未测试、编译或部署。

图形资源代码在 native/screen_touchpad_contact_visual.h。现有测试中的“中心必须全透明”期待已改成“细十字+其余背景透明”，仅维护源码一致性，**没有执行这些测试，也没有视觉实机验收**。

## 一手资料和证据位置

2026-10-13 通过网络读取并保存原文与 SHA256：

- Microsoft PointerPoint.Timestamp：开机相对微秒。https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.pointerpoint.timestamp
- Microsoft GetPointerInfo：对应调用线程最近取出的指针消息。https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getpointerinfo
- Microsoft BLENDFUNCTION：SourceConstantAlpha 与每像素 Alpha 的组合。https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-blendfunction
- Microsoft GestureRecognizer.Holding：Started、Completed/Canceled。https://learn.microsoft.com/en-us/uwp/api/windows.ui.input.gesturerecognizer.holding
- Valve Steam Input Activators：Long Press 激活器。https://partner.steamgames.com/doc/features/steam_controller/activators

原文及来源收据、修改前后快照、差异与文件哈希：
`G:\YeManCC-Work\Build\Analysis\ScreenTouchpad-Stall-CPU-20261013-145320`

当前工作区还有其他并行修改；快照/整文件哈希不代表整文件全部由本任务修改。尤其 main.cpp 的 OEM 按键显示相关修改不属于本任务，已保留。本轮只做源码、调用点、资源生命周期、锁顺序及差异静态检查；不把其他轮次的 PASS 数字用作本轮验收。