# 控制器标准分页与触控板总开关

2026-10-22 采用 A「标准分页」。主入口仍为控制器页，横向分为“控制器 / 触控板 / 灯光震动”。沿用 YMCC 原顶部、导航、控件和缩放；专属状态横幅与退出确认跨分页保留。确认解除专属后，焦点返回当前可见分页标签。

触控板页顶部只读显示当前实际选择的虚拟手柄。切换虚拟手柄仍由控制器分页负责。触控板与灯光震动的设置区域独立挂载，离开触控板页时沿用现有待保存值收尾；页面打开不启动额外设备动作。

`screenTouchpads.profiles.<persona>.masterEnabled` 为新增的独立总开关，通过既有 `screenTouchpads.get/set` 保存。每个人格各自记忆，缺字段按 `true` 读取，保证旧配置行为不变。`enabled/layout` 继续表示已选触控板布局，关总开关不改布局、映射、显示参数、呼出或专用/背部按键选择。屏幕控件保持既有按人格共享的所有权；游戏专属虚拟输出与快捷键继续使用原专属数据链。

原生 `configure` 将总开关变化纳入释放/重设判定，关闭时释放旧触点、按住的屏幕按键和输入状态，并关闭触控板与屏幕按键准入；重新开启不重放旧输入。`Core`、原生/键鼠映射准入、屏幕按键和自动隐藏的可见集合均服从总开关，配置序列化仍保存原始字段。

验证入口：

- `tools/verify-screen-touchpads-master.ps1 -OutputDirectory <新证据目录>`：执行真实 native 配置库、Core、configure 和按键准入；legacy/candidate 两支各 23 项。隔离副本去掉输入准入判断后必须编译成功并在两个指定检查失败。无窗口初始化、实际系统输入或硬件操作。
- `tools/ymcc_sim/global/ui-navigation/templates/controllerPageCases.mjs`：维护在正式页面回归内的控制器分页、专属、总开关、保存失败、延迟回复、下拉、滑块、编辑器、返回与切页场景。复用当前主线组件及正式手柄引擎，接口为内存合同。
- `tools/screen_touchpads_selftest.ts`、`tools/screen_touchpads_ui_selftest.mjs`：字段约束、默认值与实际 SFC 渲染。

本轮证据：`D:/Codex/2026-10-22/3/Controller-Standard-Import/RESULT.json`。1280×720 / 1280×800 的实际比例 UI 回归为 24 状态、1224 方向、38 操作；原生 legacy/candidate、缺失准入负例、108 项既有 Core、1284 项触摸反馈均通过。未宣称物理手柄验收，未导出或安装程序。
