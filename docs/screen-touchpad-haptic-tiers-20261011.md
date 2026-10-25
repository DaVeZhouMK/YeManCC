# 屏幕触摸板三档触摸反馈（2026-10-11）

## 用户要求与当前行为

保留已确认正常的触摸／滑动分段逻辑，保留原最小档；新增相对最小档增加 100% 与 200% 的两档。
这里的增加按请求力度计算：增加 100% = 原值的 2 倍；增加 200% = 原值的 3 倍。

| 配置值 | 显示名称 | 可调后端请求力度 | 相对原最小档 |
| --- | --- | --- | --- |
| off | 关闭 | 无触觉请求 | — |
| minimal | 最小震动反馈 | 10% | 不变 |
| medium | 中震动反馈 | 20% | +100% |
| strong | 强震动反馈 | 30% | +200% |

新配置和缺少 touchFeedback 字段的旧配置默认 medium；明确保存的 off／minimal／medium／strong 均保留。
四个控制器配置档独立保存；布局开关与档位切换不强行把用户选择改回默认。没有批量改写已保存的配置。

## 实现

- 前端类型、选项名称、校验和 SFC 缺失字段 fallback 与 native 枚举／解析／JSON／默认值同步。
- Core 产生的 Action 携带选定请求力度，经原输出 callback 捕获到有界 Dispatch Ticket，
  现有串行 worker 使用该 Ticket 的力度写入原实体后端；没有在 worker 中重读新配置来重定向旧请求。
- 显示配置变化沿用原有 cancel／reset；Dispatch 还按力度边沿废弃已排队的旧档脉冲。
- EventGate 同时观察触点与力度边沿；重复 held／idle 不重复调度。日志只新增三个固定档位计数，
  不逐次 MOVE 生成或刷新成功日志。
- 时长 18ms、短边 5% 刻度、0.6% 噪声门、45ms 限频、80ms 请求过期均不变。
- 不增加生产线程、定时轮询、设备扫描或新 I/O 通道；不改鼠标穿透、显示恢复或 BUS/InputHost 单写者。

## 机型能力边界

只复用 YMCC 现有实体输出及机型适配，不启用虚拟手柄反馈。
XInput 可调后端对应双马达值为 6553／13107／19660；受管 MSI 的线性后端继续用既有转换。
MSI A1M 仍按固件能力写 0/193 开关量，因此三档标签不能证明该机型实际具有三档振幅。
没有为了伪造更强触感延长脉冲或缩短限频。设备实际手感和请求百分比不保证线性等价。

## 验证与构建

- 原生标准 build_native 配方和实际 Web 隔离构建均通过；没有借用上一轮旧主程序来代替新档位输出验证。
- 触觉 574 项：legacy／native 纯夹具各 274，实际主 EXE 模拟输出 26。
  包含三档请求值、起震／归零、既有实体身份和电源保护、档位持久化、四档配置隔离、
  相同触摸／滑动节奏、旧档排队取消和万次重复 held／idle 不重复调度。
- 前端 typecheck、严格选项校验、实际 SFC 渲染通过；实际 Vue 控件和生产 gamepad engine 的浏览器回归 138 项通过。
- 普通鼠标点击穿透 64、实际主 EXE owner 分支 40、游戏式窗口显示恢复 16 项通过。
- 实际主 EXE Core 108、WM_POINTER 439 项通过，native→Host 报文夹具通过。
  InputHost 的八个无设备模拟自测使用现有已构建二进制；没有声称覆盖其他任务正在改动的 Host 源码。
- 纯 Core 五类百万事件、五轮中位数微基准：本轮约 14–18.2ms／百万事件。
  前后鼠标输出 checksum 和脉冲次数相同；不是整个 YMCC 的 CPU 测量，也不包含设备 I/O。
- 本轮四个运行时文件及 main.cpp 触觉输出／调度／汇总／自测区域与固定验证来源一致，来源不匹配 0。
- 所有马达写入均使用内存模拟 writer，真实马达写入 0。真实震感、实际游戏／独占全屏尚未验收，安装版未覆盖。

构建目录使用本轮当前源码的固定快照，构建过程中其他任务的新改动不自动纳入；不回滚其他工作区修改。
第一次构建暴露新增自测名称的 string／const char* 类型不匹配，已修复并完成第二次原生构建；初次失败日志保留。

### 证据

- `G:\YeManCC-Work\Build\Validation\ScreenTouchpads-HapticTiers\final-verification.json`
- `G:\YeManCC-Work\Build\Validation\ScreenTouchpads-HapticTiers\LowCpu\lowcpu-verification.json`
- `G:\YeManCC-Work\Build\Validation\ScreenTouchpads-HapticTiers\Browser\browser-verification.json`
- `G:\YeManCC-Work\Build\Validation\ScreenTouchpads-HapticTiers\MouseClickthrough\mouse-clickthrough-verification.json`
- `G:\YeManCC-Work\Build\Validation\ScreenTouchpads-HapticTiers\GameVisibility\game-visibility-verification.json`
- `G:\YeManCC-Work\Build\Validation\ScreenTouchpads-HapticTiers\touchpad-live-to-frozen.json`
- `G:\YeManCC-Work\Build\Validation\ScreenTouchpads-HapticTiers\IsolatedBuild\Build\App\Native\YeManCC.exe`

主 EXE SHA256：`4ADF4B858B27CD078293876C4FAB823F1E0CC0EDF9B5BA89A5F6D92748954D19`。
