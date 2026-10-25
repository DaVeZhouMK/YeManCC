# 一体控制台全局默认外观与五版预览

2026-10-18 用户采纳一体控制台为默认，要求保持 YMCC 配色，其他内页提供五版真实比例 H5，保留气泡底板、至少当前字号和手柄可达性。

## 主线接入

- src/components/IntegratedPage.vue：九页共用标题与当前供电状态，只消费 powerSourceMode，不新增采集或设置写入。
- src/styles/integratedConsole.css：同一套字体、分组、半透明底板与边界。通过 main.ts 引入；性能页继续使用现有 ContinuousPerformancePanel。
- 各视图只增加共用外壳 import 与 slot 包裹。八页脚本逐字对照无行为变化；陀螺仪页在本轮期间收到并行任务新增的体感来源检测，已保留，未当作本轮 UI 修改回退。
- 自动/手动调度、CPU/GPU 模型、底层 IPC、Decky 与自定义游戏库独立实现不在本轮修改范围。

## H5

http://127.0.0.1:12063/?variant=a&height=720&route=%2Fbutton-mapping

默认 A；B–E 仅在预览中使用独立布局层，保持原控件与数据绑定。实际 Vue 组件 47 个。无原生运行边界，IPC 和配置为内存样例。

580×780 外壳、105 设计宽侧栏，沿用主线缩放函数。720p 的原窗口 535×720，800p 为 595×800。监控页的 900 设计上限仍受实际窗口宽度约束；快捷编辑器按 window.controllerEditorWidth 的 2 倍行为预览为 1070/1190 宽。

默认预览读取主线共用样式，未维护第二份默认控件外观。动态、明亮、暗色背景均检查过气泡可辨认性；菜单/编辑气泡也有半透明底板。

## 验证与交付

100 组布局、50 组预览导航、5 组参数保存/取消、10 组专属、10 组编辑器、3 类背景；类型检查和前端构建通过。

预览源与导出资源逐文件 SHA256 一致，组件源在构建后无变化。Steam 只读盘点 13 区域、431 候选文件，0 缺失锚点、0 未归类候选；不代表 Steam 实机兼容验收。

当前记录：Build/Tasks/ContinuousPerformance-20261017/GLOBAL-INTEGRATED-CONSOLE-CURRENT.json

H5 交付：G:/YeManCC-Work/Mainline/Deliverables/Global-Integrated-Console-H5-20261018-040037

未导出或部署新的原生运行包，保留原测试包及其身份记录。
