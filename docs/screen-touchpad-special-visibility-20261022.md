# 触摸板专用按键：重选后未恢复显示（2026-10-22）

用户反馈：触控板切换时，偶尔选中了专用按键但屏幕不显示。本轮仅处理触摸板及专用按键，不接管监控、陀螺仪、风扇或虚拟手柄驱动任务。

## 日志事实与证据边界

样本：`G:/YeManCC-Work/日志/微星G3E/YeManCC-logs-20261022-174243-G3E-陀螺仪失效 -  游戏重连无效 - 风扇小图标停转了.zip`。

- 2026-10-22 17:34:28.605，Steam Deck 档专用键 specialMask=3、specialEnabled=true，autoHidden=false、error=0；17:34:37.803 起同档 autoHidden=true，直到 17:42:43 导出仍为 true。导出时原生穿透 active=true、runtimeError=0，不能据此推出所有按钮图案已显示。
- `visible` 旧字段检查输入 HWND，而自动隐藏仅撤下图案 HWND；因此 visible=true 与图案隐藏能同时成立。
- 相邻的 requestedPersona=disabled 记录是读取另一个保存档位。profileState 会将非当前档位 visible/autoHidden 清零，不能将它们当作实际模式切换证据。
- 提取到 120 条触摸板状态记录（4 个日志文件合计，部分重复）；样本没有 `screenTouchpads.set` 状态记录，也缺少逐按钮实际图案可见性。因此不能证明每次现场“选了不显示”都出自本次定位的路径。
- 日志已有 cursorPolicyVersion=17 与 exitInputWithdrawalVersion=1；本轮没有用“版本过旧”解释问题。

## 确认并修复的路径

正式 Dropdown.select 每次选项确认都会发送 update:modelValue，包括再次选择当前值；触摸板设置组件继续经保存队列调用 screenTouchpads.set。原生 configure 发现配置未变化时，reset=false，末尾 autoHideSync(false) 保留已经隐藏的状态。这条链可以稳定复现：专用按键自动隐藏后，用户重选同一项，设置成功，图案仍隐藏。

- configure 增加默认关闭的 revealFromSettings 参数，仅 screenTouchpads.set 在成功保存、且目标档位为当前档位时显式开启。
- 该参数复用 autoHideSync(true)：恢复这组触摸板/专用键图案并重新计时。相同配置不重建窗口、不释放正在使用的输入；原自动隐藏仍在完整超时后生效。
- 其它 owner refresh、显示恢复、重绑定仍不主动清除闲置隐藏。非当前档保存、总开关关闭、专用键关闭及终态退出门保持原约束。
- 新增 buttonVisibility：permittedMask / inputVisibleMask / artworkVisibleMask。位 0 为唤醒键，位 1、2 为两专用键，位 3～6 为背键；仅查询已有 HWND，表示窗口可见属性，不证明未被遮挡、扫描输出或信号已被游戏消费。
- 状态同时记录 activePersona、appliedPersona、controlTargetEnabled。查询非当前保存档位时 buttonVisibility=null，防止借用当前档窗口状态。

## CPU 与验证

本次不增加刷新率、线程、常驻轮询或 MOVE 路径工作。新增显示状态只在已有 state 查询时执行；7 个按钮最多查询 14 个已存在 HWND。恢复图案沿用缓存和单个截止时间计时器。

- `tools/screen_touchpad_settings_reveal_selftest.py <新证据目录>`：32 项通过，实际提取 configure、buttonPermitted、buttonVisibilityState 及完整自动隐藏头；Win32 窗口、绘图、时钟及布局协作者为内存替身。
- 同入口 `--negative` 只移除 revealFromSettings 条件，精确命中 Steam/PS5/Xbox 与无虚拟手柄 Steam/PS5 的 5 项原失败，其余检查通过。
- 覆盖旧计时消息、完整超时、全关闭、总开关、退出后迟到设置、专用键单选、背键/唤醒键维持原隐藏语义；10,000 次触摸 MOVE 不新增计时器重建或绘图。没有进行实机 CPU 百分比测量。
- 退出回归 53 项执行断言及 5 项源码断言通过；移除首轮 quiesce 的负例仍精确命中原 5 项失败。已有共享 Surface 新增 HasCapture 行为，本轮仅补齐退出夹具的对应内存适配与断言，未修改共享 Surface 产品代码。
- 569 个冻结原生输入完整编译通过，主线对应输入哈希一致。候选程序未启动、未部署，没有真实输入注入或设备操作。

## 证据与交付

证据根：`D:/Codex/2026-10-22/files-mentioned-by-the-user-yemancc-6/special-buttons-visibility-20261022`。

- verification-final/receipt.json：32 项正例。
- verification-negative-final/receipt.json：5 项确定性原策略失败。
- exit-regression-final/receipt.json、exit-negative-final/receipt.json：退出正反回归。
- native-build-r2-result.json、native-build-r2.log、build-inputs.json、source-drift.json：编译与输入身份。
- RESULT.json：最终总回执。

状态：已写入主线并完成上述软件验证；新候选尚未部署，现场偶发问题是否全部消除待普通日志/用户使用反馈验证。


后续 2026-10-23 日志确认另有 E_ABORT 后单键输入/图案窗口缺失，已在 [专用按键同步中止恢复](screen-touchpad-button-abort-recovery-20261023.md) 单独修正；上轮自动隐藏重选修复继续保留。
