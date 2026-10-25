# 触摸板与专用按键自动隐藏（2026-10-14）

状态：源码实现完成，仅静态检查；按用户此前要求没有运行测试、编译、部署或发送实机输入，不能作为真实触屏、游戏切换或 CPU 的验收结果。

## 用户行为

设置页在“专用按键／背部按键”选择区之后新增“自动隐藏”下拉菜单：关闭、15秒、30秒、45秒、60秒、120秒，默认 60 秒。单板、双板和只开启专用按键的配置都可使用。四个输出档位各自保存设置。

同一组触摸板和专用按键共享最后触摸时间：闲置到期后全部视觉不可见，触摸原区域恢复原有显示透明度；手指持续按住不隐藏，最后抬指后重新计算闲置时间。恢复的第一笔触摸沿用原映射，不吞掉点击，也不另造点击或改变前台。正常鼠标穿透不触发唤醒；只观察这组控件自身的 Touch/Pen 事件。

用户所述“透明度 0%”按“完全不可见”实现。现有 transparency 字段是 100 才完全透明，0 是不透明；自动隐藏不覆盖该保存值，而是隐藏视觉 HWND，保留透明输入 HWND。边框、十字、接触光点/晕光及专用按键图标一起隐藏；YMCC 呼出键与背部按键维持原状。用户本来设置为 100% 透明时，唤醒也保持完全透明。

新配置以及旧配置缺失 autoHideSeconds 时默认 60。明确保存 0 表示关闭自动隐藏。修改显示透明度、自动隐藏时长、布局或档位时重新展示并计时，便于设置；用户明确保存/重选当前档位设置时，即使值未变也重新展示并计时（2026-10-22 修正）；单纯的同配置窗口/游戏显示恢复不清除已经隐藏的状态。完全关闭这组控件或退出时撤销计时。

## 代码与持久化

- src/bridge/screenTouchpads.ts：联合类型 0/15/30/45/60/120、默认 60、枚举选项及补丁白名单校验。
- src/components/ScreenTouchpadsSettings.vue：复用现有 Dropdown，支持既有手柄导航和保存流程；文案说明超时不可见、触摸恢复。
- native/screen_touchpads.h：Config 增加 autoHideSeconds，configJson/parseConfig 接入严格枚举校验。沿用 ProfileBank 与现有 screenTouchpads.set 的先保存再应用流程，无额外配置文件/独立状态源。
- native/screen_touchpad_auto_hide.h：新增 UI 所有者线程的到期计时与视觉切换。隐藏不调用 release、不取消输入源、不改变 BUS、原生报告或功能键发送路径。
- native/screen_button_overlay.h：只跟踪 index 1/2 专用键；额外固定两个物理触点 ID 只服务可见性，解决原前台策略暂停映射时仍按住的情况，不进入按键报告。取消、切档、目标不可用和退出都有清理。
- native/screen_touchpad_contact_visual.h：自动隐藏期间不展示接触光点。

导出状态新增 autoHidden、autoHideTimerArmed、autoHideTransitions、autoHideWakeups；原 visible 字段仍表示输入控件开启/可用，不能把它单独当成用户能看到图案。非当前档位的 autoHidden/autoHideTimerArmed 返回 false，避免借用另一档的实时状态。原震动三档、光点大小、十字范围和 40px 垂直间距保持原实现。

## 低 CPU 与恢复边界

最多一个原生 HWND 定时器，期限为剩余闲置时长，不加工作线程、BUS 轮询、WebView 轮询或渐变动画。回调先 KillTimer，再读一次当前状态；活动更新使旧期限提前到达时，仅为剩余时间重新预约。保持静止按住时，到期检查后不再周期检查，下一次触摸事件再预约；已完全隐藏或设置关闭时不留该定时器。

普通 MOVE 只更新最后触摸时间，已有定时器不重设；显示未变化时不因自动隐藏增加重绘、位图上传或 JSON。唤醒时复用既有图案缓存，隐藏直接隐藏视觉窗口；不修改输入窗口的鼠标穿透样式、不激活窗口。实际净 CPU 开销未测量。

每次预约使用新的定时 ID，撤销后队列里残留的 WM_TIMER 因 ID 不符而被忽略。SetTimer 指定非空 HWND 时以传入的 nIDEvent 作为取消标识，而非假定返回值就是 ID。回调经过原 UI 消息循环的 DispatchMessageW。超时执行依赖 UI 消息处理；UI 卡住期间不会由另一个线程强制操作窗口。

刷新路径检查隐藏状态：触摸板边框、专用键绘制窗口的 ShowWindow 都受该状态约束；接触光点检查同一状态。已有游戏置顶恢复只抬升已经可见的视觉窗口，不主动显示本次隐藏的窗口。单纯迟到的捕获丢失／无匹配 UP 不计为新的触摸活动。

Microsoft 官方原文已只读核对并存档：
- SetTimer：https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-settimer
- KillTimer：https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-killtimer

## 本轮记录

`G:\YeManCC-Work\Build\Analysis\ScreenTouchpad-AutoHide-20261014-002024`

包含修改前后快照、差异、文件 SHA256 及来源原文。保留工作区其他并行修改。没有运行任何自测、基准、构建、触摸注入或部署，旧轮次通过记录不适用于本轮。