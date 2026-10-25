# 触摸板：低 CPU 刻度触觉与游戏窗口遮挡恢复（2026-10-11）

> 同日后续三档更新：原最小档保持 10%，新增中档 20% 与强档 30%，缺省值改为中档。本文记录的是此前低 CPU／显示恢复批次，其 400 项统计与 V3 证据保留；三档本轮 574 项验证见 `G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/docs/screen-touchpad-haptic-tiers-20261011.md`。

## 本轮范围

用户授权执行已研究的轻量算法，并补充：进入游戏后触摸板会消失，而新监控已能在游戏界面显示。
复用当前实体震动后端，维持默认开启与明确关闭持久化，保留鼠标穿透；不重做 HID 句柄生命周期、
不重构共享串行 worker 停震调度，不安装驱动／改变权限、UIAccess、电源策略、BUS/InputHost 单写者。

## 触觉实现

- 可调强度后端请求从 8% 改为 10%，时长仍 18ms；MSI A1M 的 0/193 开关量规则保持不变。
- 距离刻度由短边 6% 改为 5%，最短起震间隔仍 45ms，排队过期仍 80ms。
- DOWN 缓存短边倒数及噪声距离平方；MOVE 先比较平方，仅通过 0.6% 噪声门后计算欧氏距离。
- 跨越刻度按商／余量一次计算，跳过的刻度仍被消费；不循环生成脉冲，不积压停手后的补震。
- 每个触点的固定状态封装为纯 Motion；无分配、I/O、计时器、轮询或新线程。
- UI 输出仅在新脉冲／触点所有权边沿提交；一万次相同 held 或 idle 报文不重复进入调度。
- 正常短脉冲不逐条创建／即时刷新成功日志，复用 worker 上的固定计数，触点结束时最多一个排队汇总。
  失败日志和原有手动震动日志仍立即记录；MSI 的成功起／停帧同样使用该策略。
- 起震／零值停震／电源代际／实体身份／旧停震拒绝／手动脉冲保护均沿用，不开放虚拟或 Unknown 槽。

## 游戏内消失：证据与修复

新监控的显示栈维护具有前台／显示／窗口层级事件及确认遮挡后的重新置顶。
触摸板此前仅在创建／配置刷新时置顶；后来的 topmost 游戏窗口能把输入层与边框同时盖住。

新 `screen_touchpad_visibility.h` 在已有 owner/UI 线程上维护显示栈：

- 仅有可见触摸板／功能键时安装五种 WinEvent **显示事件观察器**，关闭全部控件后移除。
- 外部前台、显示、层级、位置及最小化恢复事件合并为一条 owner 消息，不加入固定轮询。
- 对前台 topmost 窗口的实际重叠和 Z 栈做有界检查；仅确认遮挡时用 SWP_NOACTIVATE 重新置顶。
- 输入层／视觉层成对恢复，包含功能键；不刷新设置、不更改位置、不重传边框、不重建输入源，
  不取消活跃触点，也不抢前台／转发鼠标点击。这里是 WinEvent 观察器，不是低级鼠标／键盘输入 hook。
- 生产主窗口路由了新的 owner 消息，停用／退出会撤销观察器并拒绝旧代际消息。

隔离重现还查到实际 `EVENT_OBJECT_REORDER` 可以归属于桌面窗口的 **OBJID_CLIENT (-4)**。
仅接受 OBJID_WINDOW 会漏掉游戏后置顶的事件；此分支已补齐。原始失败和原始事件记录保留，
不能通过单纯把窗口创建为 WS_EX_TOPMOST 就宣称问题解决。

## 延迟短点按发布窗修复

完整 WM_POINTER 回归暴露 `PS5-single-tap-produces-native-click` 失败：消息排队后，短点按仍应产生
原生点击，但以 source time 设置的 60ms 发布窗可能在实际处理前已经过期。

- DOWN／UP 按压长短继续依据原始 source time，保留已有延迟长按不误判点按的修复。
- 原生短点按的 60ms 发布窗从实际 delivery time 起算；`Core::event` 接收可选 `delivered=0`，
  `handlePointer` 传入当前 `dispatchTick`。
- 新增两项纯测试分别覆盖延迟短点按与延迟长按；没有转发／重放鼠标输入，没有改鼠标穿透策略。
- 原始失败记录保留在 `G:\YeManCC-Work\Build\Validation\ScreenTouchpads-Haptics-LowCpu\delayed-native-click-before-fix`。

## 验证与边界

- 触觉：legacy／native 夹具各 190 项，实际主 EXE 输出 20 项，合计 400 项通过。全程模拟实体 writer，实际马达写入 0。
- 纯生产 Core 微基准：五类负载，每类百万事件、五轮取中位数；前后鼠标输出 checksum 相同。
  新算法本轮约 13–17ms／百万事件，比旧算法约快 8–27%。这是纯事件状态机的耗时微基准，
  不包括实际设备 I/O、DWM、进程其他工作，不能换算成 YMCC 整体或整机 CPU 百分比。
- 其他回归：普通鼠标点击穿透 64、实际主 EXE owner 分支 40、WM_POINTER 437、Core 106、浏览器 136 项通过；
  InputHost 八个既有无设备自测及 native→Host 报文夹具通过，光标 104、视觉缓存 20 项通过，冗余上传为 0。
- 最终总审计 `ok=true`、来源不匹配 0、测试范围残留进程 0；它是已执行测试的汇总，不是硬件验收。
- 显示：自有子进程在自身线程执行游戏式置顶，不伪造 Winevent。负对照的边框像素确实变成白色，
  恢复后回到原边框像素。16 项检查包含后置顶／双板与按键／触点保持／相同输入 child／不重上传／
  下层鼠标点击／正确层级不反复置顶／闲置无轮询／关闭与退出撤销。
- 此测试并非实际游戏、并非真实触摸硬件、并非独占全屏验收。未自动启动用户游戏或正常产品，未部署安装版。
  真正独占全屏绕过桌面合成的场景不因这个 HWND Z-order 修复就被证明可用。

## 构建来源与验收范围

共享 `native/main.cpp` 在本轮持续收到其他任务修改，实时完整构建多次被来源新鲜度检查拦下，
也遇到其他任务的临时编译错误。最终验证采用最后一次可编译的固定项目基线，覆盖本轮触摸板文件后
隔离构建至 `FrozenBuildV3`；没有回滚其他人的改动，也没有把旧的完整主程序冒称最新发行版。

- 固定来源：`G:\YeManCC-Work\Build\Validation\ScreenTouchpads-Haptics-LowCpu\FrozenProjectV3`。
- 隔离主 EXE：`G:\YeManCC-Work\Build\Validation\ScreenTouchpads-Haptics-LowCpu\FrozenBuildV3\Build\App\Native\YeManCC.exe`。
- EXE SHA256：`D427B1A63BF356CD2C6EA0ECF5F11017CC9A29877043B56E8106E3D0469C9AB0`。
- 已核对工作区七个相关 native 文件及 main.cpp 的输出／调度、MSI writer、生产窗口消息接线三个区域与
  验证版一致。此比较仅证明本轮触摸板改动匹配，不覆盖共享 main.cpp 中其他任务的新修改。
- verifier 可显式接收 `SourceRoot` 验证固定来源；默认仍使用实时工作区，新鲜度检查没有放宽。
- `actualGameVerified=false`、`hardwareFeelVerified=false`、`installedProductChanged=false`。
  后置顶遮挡路径已在隔离窗口中修复并复现通过；多屏放置、控制器档位切换等其他现场消失原因尚未排除。

## 复现

```powershell
# 仓库根目录；先完成标准 native 构建
pnpm run test:screen-touchpads-haptics-lowcpu
pnpm run test:screen-touchpads-game-visibility
pnpm run test:screen-touchpads-mouse-clickthrough
```

证据：

- `G:\YeManCC-Work\Build\Validation\ScreenTouchpads-Haptics-LowCpu\final-touchpad-verification.json`（最终总审计）
- `G:\YeManCC-Work\Build\Validation\ScreenTouchpads-Haptics-LowCpu\FrozenVerificationV3\lowcpu-verification.json`（触觉与微基准）
- `G:\YeManCC-Work\Build\Validation\ScreenTouchpads-Haptics-LowCpu\FrozenBuildV3\Build\Validation\ScreenTouchpads\verification.json`（完整触摸板软件回归）
- `G:\YeManCC-Work\Build\Validation\ScreenTouchpads-GameVisibility\FrozenMouseClickthroughV3\mouse-clickthrough-verification.json`
- `G:\YeManCC-Work\Build\Validation\ScreenTouchpads-GameVisibility\FrozenVerificationV3\game-visibility-verification.json`
- `G:\YeManCC-Work\Build\Validation\ScreenTouchpads-Haptics-LowCpu\touchpad-live-to-frozen-v3.json`（本轮源码区域对照）
- `G:\YeManCC-Work\Build\Validation\ScreenTouchpads-Haptics-LowCpu\before-source`（保留修改前算法）
- `G:\YeManCC-Work\Build\Validation\ScreenTouchpads-GameVisibility\first-fixture-failure`、
  `G:\YeManCC-Work\Build\Validation\ScreenTouchpads-GameVisibility\desktop-client-reorder-before-fix`、
  `G:\YeManCC-Work\Build\Validation\ScreenTouchpads-GameVisibility\interrupted-foreground` 保留初始诊断。
