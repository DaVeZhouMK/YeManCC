# 监控时间字段按显示需求计算（2026-10-16）

本轮移除未显示“当前时间”时的本地化时间格式化工作。监控继续消费 YMCC 唯一快照，不新增采集、读取循环、线程或定时器。

## 修改前实际证据

默认预设 clock=false，但实际 source 的 200 次不同时间取样仍会执行 200 次 Intl 时间格式化；顶部状态栏只使用 CPU/GPU/电池字段，同样 200 次投影产生 200 次无用时间格式化。原生预览 owner 每次数据发布也会构造该字段，和它是否显示无关。

首次负例见 BEFORE-CLOCK-PROBE.json。后续又运行了真实 startMonitorHudHost 的偏好接入路径，避免只用工厂默认行为推断产品。

## 修改与完整接入

- hudFromUniformSnapshot 新增可选 includeClock 参数。false 时 values.clock=null，跳过 Intl；既有三参数调用仍沿用原来的时间输出。
- 共用 HUD source 接收已有“当前时间”开关。初始化、实时偏好变化和成功输出应用均同步该开关；缓存键包含这个布尔值，同一毫秒切换也能立即重算。
- 顶部状态栏投影、隐藏的 relay 撤销帧不请求时间。
- 原生预览 owner 使用规范化后的字段开关，并在开关变化时只投影已有快照。开启先发送有时间的数据再发送显示配置；关闭先隐藏字段再清空数据，避免切换时短暂露出空值。
- Vue renderer 没有传入数据时的初始包，也按已规范化的显示配置决定是否计算时间。

覆盖七个生产文件：monitorHudModel.ts、monitorTopProjection.ts、monitorHudSampleSource.ts、monitorHudHost.ts、monitorHudPreview.ts、steamHudRelay.ts、monitorHudRenderer.ts。没有新增产品开关或修改预设选择；时钟开启时继续调用同一个 Intl formatter，没有分钟近似缓存或时区假设。

## 工作与正确性

新增 monitor_clock_demand_selftest.mjs：19 组通过。

| 隐藏时间的场景 | 修改前格式化次数 | 修改后 |
| --- | ---: | ---: |
| 200 次直接投影 | 200 | 0 |
| 200 次顶部状态栏投影 | 200 | 0 |
| 200 次共用 source 取样 | 200 | 0 |
| 200 次原生预览数据发布 | 200 | 0 |
| 真实 host 接入后的 200 次取样 | 200 | 0 |
| 20 次隐藏 relay 撤销帧 | 20 | 0 |

共用 source 和预览仍只有一次初始读取、一个既有需求/绑定，开关切换不增加硬件或文件读取。预览仍使用同一个 child，不为时间开关重开窗口。

逐份比较 2,000 份完整投影，除有意清空的隐藏 clock 外，所有读数、身份、数组、stale 标志与 freshUntil 相同。开启时间后的输出和旧版相同；旧三参数 API 保持输出；开关不会延长硬件/FPS 有效期或把过期读数变成新值。

新增布尔状态、缓存比较，以及时间开关变化时的一次缓存投影具有自身成本。计数描述具体格式化工作，不等于整机 CPU 百分比下降；没有运行 CPU 百分比测试。

## 真实 Vue 渲染对照

monitor_clock_demand_browser_selftest.mjs 13 组通过：

- corner、capsule、bar、circuit、neon 五种模板，各自时间开/关，10 组最终截图像素哈希逐组相同。
- 1280px 容器宽度的电路面板几何一致。
- 没有传入数据的 renderer 启动，时间开/关两种情形，文字与几何一致，关闭时不额外格式化初始空包。

使用真实共享 Vue SFC/renderer 与带原生字段形状的合成统一快照，并按原来的真实 FPS 入口时间推进；不是游戏、桌面承载或原生预览窗口实测。浏览器使用无头 Edge，未切换用户窗口。已目视检查电路面板时间开启截图。

开发中的浏览器失败保留：第一版 fixture 开启所有字段但漏写 dataPresetVersion=1，触发既有详细预设迁移，正确关闭 clock；修正测试元数据，未改生产迁移。第二版在横板 requestAnimationFrame 排版完成前测量，出现自然宽度 359.25 与最终 360 的差异；等待已有两帧排版后继续使用严格几何/像素断言，没有引入宽度容差。首次 host mock 打包遗漏 resolveDir，修正测试命名空间后运行；没有改生产逻辑来适配测试。

## 相关验证

- 既有热路径 10 项逻辑 + 11 项浏览器；CPU 消费 9 项逻辑 + 30 项浏览器通过，含全部 13 个模板和同次前后截图哈希一致。
- TDP 观察者 18 组、显示订阅 19 组、会话退休 26 组、停止交接 18 组、读取源复用 15 组、显示缓存 13 组通过。
- 失败解析缓存 23 组及 2,000 份回放、UTF-8 字节上限 21 组、数据链路 31 项、生命周期 13 项且最终 timer=0、CPU/健壮性纯逻辑 24 项、数值 101 项通过。
- vue-tsc --noEmit 与改动/新文件空白检查通过。
- Steam 接入只读盘点：13 类，缺失锚点 0、未归类 0。本轮没有改变 Steam 私有接口、命令或窗口创建；盘点不是新 Steam 版本兼容验收。

历史帧时间套件的 producer backwards resets 旧断言仍为历史未通过项，本轮未重跑或声称整套通过。未改 native，未重复原生整程序构建。

## 证据与范围

Build/Tasks/YMCC-Steam-HUD-Probe/monitor-clock-demand-20261016/：

- BASELINE.json、before/：本轮实际修改前的消费者和 Vue 文件。
- BEFORE-CLOCK-PROBE.json：首次生产路径工作计数。
- CLOCK-DEMAND-RESULT.json：19 组、真实 host 偏好接入（原生路由/IPC 端口隔离）、完整投影对照和源哈希。
- CLOCK-BROWSER-RESULT.json、browser/：13 组渲染对照、10 对截图与输入哈希。
- BROWSER-FIRST-ATTEMPT.*、BROWSER-SECOND-ATTEMPT.*：上述失败 fixture/排版观测记录。
- DATA-REGRESSIONS.json、RENDER-REGRESSIONS.json、TYPECHECK.log：本轮回归。
- STEAM-INVENTORY.json：只读盘点。
- PRODUCTION-CHANGE.diff、ROUND-RESULT.json：最终生产差异、文件身份与范围。

本轮未安装、部署、启动或重启 YMCC/Steam/游戏，没有桌面操作、真实传感器采集或 TDP 写入。改动仍仅在主线源码，持续优化目标保持进行中。
