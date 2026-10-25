# Steam 原生控制器矢量图（主线副本 UI 验收）

本次显示已统一为 SteamDeckTiltedOutline、PS5ControllerFrontOutline、XboxOneControllerFrontOutline。
来源：本机 Steam 的 steamui/sp.js。仅提取 SVG 几何与静态轮廓/填色属性；不载入 Steam 的运行代码，不增加 Steam 或设备接口。

`src/bridge/controllerDiagramData.json` 是产品使用的三套路径、显示几何、按键命中区和摇杆位移比例。
`ControllerVectorArtwork.ts` 共用渲染这些路径。ControllerDeviceDiagram.vue 只把现有按钮/OEM/轴状态映射到图形。
- 填色按 Steam 原有蓝色 #4897D2；轮廓、PS5 符号与 Xbox 字母分别保留。
- 双摇杆使用原始 SVG 可移动节点，以原有 XInput +Y 向上的规范转换显示坐标。
- 没有位图描边遮罩、发光、阴影、渐变或图像节点。
- 原生正面图没有独立可见的 LT/RT 轮廓时，共用相应肩键的原始填色形状；输入索引仍独立，鼠标命中分开到上下区域。
- Steam Deck 四背键沿用原图自身路径；PS5/Xbox 的四个背键槽复用这些原始虚线路径，等比放到各自握把内侧。它们是掌机背键在当前虚拟手柄图上的位置示意，不是声称所有标准 PS5/Xbox 手柄都有四背键。
- 背键只在原生 OEM 目录有相应 backIndex 且 captureImplemented 不为 false 时启用。缺失或采集未实现的槽只保留淡虚线，真实输入和本地模拟均不能点亮。
- 顶部气泡只显示专用按键，标题为“专用按键”；使用原 keyId、原 gamepad.state OEM 按下状态。额外无 1..4 槽的旧背键仍保留在原编辑器绑定清单，不增加第五个图形背键。
- 旧 WebP 参考保留供对比，当前编辑器不使用它们；Xbox 原 SVG 代码保留为无矢量数据时的回退。

资源来源不是开源声明。Valve 官方 FAQ 把 Steam 客户端列为专有软件；本地原型的来源和 SHA-256 见 steam-vector-provenance.json，不标注 MIT/CC0，也不从 SteamOS 开源许可推导 Steam 大屏资源许可。

Valve 另公开提供 Steam Deck 正面、背面、顶面 SVG 图包（本次主要采用更贴近用户参考的客户端交互图）。文档：
`https://partner.steamgames.com/doc/steamhardware/steamdeck/svg`

可运行 `python tools/controller_diagram_assets.py` 校验三套数据并导出供检查的无状态 SVG。它不读取、启动或修改 Steam。
