# Decky 无虚拟手柄专用按键同步（2026-10-10）

## 本轮范围

只同步 Decky adapter 和入口。复用主线 SCREEN-STANDALONE-SPECIAL-KEYS.md 中的独立功能动作，未重新实现 Steam/麦克风 owner。

| 独立模式 | 左键 | 右键 |
| --- | --- | --- |
| 关闭（off） | 关闭 | 关闭 |
| 开启Steam全部（steamdeck） | Steam 键切换 Steam 主菜单 | 三点键切换 Steam 快捷菜单 |
| 开启PS5全部（ps5） | PS 主页键切换 Steam 主菜单 | PS 静音键切换默认麦克风静音 |

## 配置与界面边界

- disabled 全局槽读取/保存原 standaloneSpecialMode；非 disabled persona 保留 specialMask 和原虚拟输出路径，不发布独立模式字段。
- 仍为同一“专用按键”下拉；同一位置只显示独立模式或虚拟模式一个入口，不增加第五个主入口。
- 触摸板映射/布局/启用、左右映射、背键、虚拟手柄 outputTarget、AC/DC 和游戏专属均不由此保存改变。
- 无游戏也可配置，沿原 screenTouchpadsSet 全局 owner 和 lock/power/lifecycle/source CAS。save patch 仅包含 standaloneSpecialMode。
- source CAS 新增持久模式字段；排除 standaloneSpecialStatus/Requests/Completed 等运行计数。静默配置变化拒绝旧菜单。
- 后端不返回有效模式时入口不可用，不能通过 specialMask 或创建虚拟手柄绕过；客户端八字段边界没有放宽。
- 下拉继续使用统一宽度、向下展开的 AnchoredDropdown；原六模块、SVG Y 和电源档位 3 秒尾沿延迟保留。

## 验证

- exact production host/relay/store/adapter + 客户端集成：28 项。
- exact production TSX presentation：27 项（原 22 项加本轮 5 项）。
- MirrorClient：31；全局控制：10；电源延迟：16。
- 主线独立按键：184 Native standalone/core + 22 Steam 生产脚本模拟项；脚本嵌入一致。
- Vue SFC 触摸板渲染通过；vue-tsc --noEmit 通过。
- Native 编译链接、Web 构建、Decky pinned SDK 构建/校验通过；主线随包插件已同步。
- 新客户端集成使用真实生产快照的八个触摸板字段，经过生产 client→relay→global owner 保存模式，并确认第九字段仍拒绝。
- 所有输入/麦克风出口均模拟或未执行，没有真实用户配置写入，没有创建虚拟手柄，没有重启 Steam。

证据目录：G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Standalone31\validation
状态/产物/源码 SHA256：G:\YeManCC-Work\Build\Tasks\YMCC-Decky-Standalone31\validation\CURRENT-STATE31.json

## 安装状态与未验收项

本轮生成匹配 Native/Web/Plugin 候选，**未安装、未启动候选**。运行中产品仍是 Console30 Native/插件，已只读核对文件 SHA256 没有变化。不要将源码/模拟测试完成说成当前运行产品已生效。

长期安装验收目标保持 paused，未恢复、未标记完成。真实触屏点击→Steam 菜单/真实麦克风的端到端未验收。若后续部署，需备份并正常退出/重启 YMCC、匹配三份产物；不能只热换插件并声称独立功能已上线。
