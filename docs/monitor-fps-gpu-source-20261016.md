# 监控 GPU 占用来源与冗余校验修复（2026-10-16）

本轮修复现代 RTSS/Presented 帧率数据被当成 GPU 占用证明的问题，并减少选中 GPU 已有数据时的无效校验。生产修改限 monitorHudModel.ts、monitorFrameTime.ts、monitorData.ts。继续只消费 YMCC 统一快照，没有新增采集、硬件/API 读取、轮询、线程、timer 或传输字段，没有部署或本机游戏操作。

## 已复现问题

修改前实际统一 hub 接纳一份 top=null、hwDown=true、FPS=120 且带 RTSS 观察/进程身份/有效期证明的包，FPS 的旧附带 gpu=99 会被映射为 99% GPU 占用，条形可见。输出只有 FPS 和帧时间截止时间，没有独立 GPU 硬件截止时间。持续交付新的 FPS 观察，就能持续保留这份没有 GPU 身份/采样证明的旧值。

另外通过现有统一 hub 的 stop floor 复现：先有选中 RTX 5080 数据，停止 top 而保留现代 RTSS FPS 后，top 被正确清空，却又从 FPS 的旧 gpu 字段恢复了占用。修改后该字段保持未知，真实 FPS/曲线保留。

初步探查同时把 packagePower 设置为 999 验证了边界：监控 CPU/GPU 功耗在 top 缺失时原本就保持未知，本轮没有改功耗或自动调节控制路径。这里是合成输入的确定性来源错误，未对某次实机闪烁建立唯一因果关系。

## 修改

- 把统一 parser 原有的“现代发布者元数据是否出现”判断提为 hasExplicitFpsMetadata，parser 和显示消费者共用。检查仍是原来五项 fpsSource/fpsObservedAt/fpsValidUntil/fpsPollPeriodMs/processCreated；出现元数据不是有效性、硬件新鲜度或 GPU 身份证明，原有 PID/进程创建时间、期限校验不变。
- 无选中 GPU 硬件信息时，现代 FPS 包不再通过 gpu 附带字段生成 GPU 占用。未知保持 null，开启无数据隐藏时移除相应数字/图形；关闭隐藏时仍是未知轨道，不伪装成测量 0%。
- 真正的旧格式仍保留原有 GPU 附带字段兼容与 6 秒边界，包括真实旧格式 0%。只有 frameTime 信息而无上述现代主帧率元数据的旧包继续按原兼容规则处理。
- 选中 GPU 的 GPU 使用率、0%、功耗、时钟、电压与显存仍由原 top 组供应，按硬件自己的截止时间失效，不依赖 FPS。缺卡/失效时不借用另一张卡或帧率包；没有游戏时仍能显示有效硬件 GPU 数据。
- 提前判断是否存在选中 GPU 数据。存在时直接走原硬件校验，不再先验证 FPS.gpu 然后覆盖结果。

不改变 native 的显卡选择/锁定算法、槽位对应或数据采集。当前代码只修正消费侧的来源与重复工作。

## 确定性工作计数

| 200 次投影的场景 | 修改前 | 修改后 |
| --- | ---: | ---: |
| 现代 FPS-only：错误 GPU 99% 输出次数 | 200 | 0 |
| 现代 FPS-only：FPS 附带 GPU 值的验证次数 | 200 | 0 |
| 已有选中 GPU：随后被覆盖的 FPS GPU 验证次数 | 200 | 0 |

选中 GPU 自己的数值、范围与新鲜度校验仍执行。计数只覆盖被省掉的附带字段验证，不是所有 GPU 校验或总 CPU 指令。新增的无分配元数据特征函数也有固定调用成本；没有 CPU 百分比压测或整机降幅结论。

## 验证

新增 monitor_fps_gpu_source_selftest.mjs：15 组通过。覆盖实际统一 hub、模型、过期及两个既有 HUD 数据协议；现代 RTSS/Presented 缺硬件、CPU/独立 Windows 数据仍有效、选中 GPU 与真实 0%、拔卡/缺字段、硬件独立过期、旧格式、坏元数据与范围、1500 份完整 wire parser A/B、200 次来源/工作计数、统一 stop floor 以及旧/现代零值区别。1500 份解析结果完整相同；投影只允许本轮明确修正的 GPU 回退差异。

新增共享 Vue 浏览器测试：12 组 before/after 对照（corner/bar × 隐藏开关 × 现代缺硬件/选中 GPU/旧格式）。使用真实 FPS 进入门与合成时钟观察跨度 3 秒，没有把包改成 fixture 来绕过准入。

- FPS 与帧时间曲线继续显示。
- 开启隐藏时，现代无硬件 GPU 的整个 GPU 分区消失；关闭隐藏时 aria-valuenow 缺失、状态为 unknown，数字沿用组件当前的未知值加单位样式。
- 有效选中 GPU 与旧格式内容一致；选中 GPU 的前后截图 SHA256 相同。
- 无浏览器错误。开发时只修正了测试对现有数字/单位 DOM 文本的预期，没有改变产品样式。

其他通过：

- GPU 消费原 14 项，输入复用既有 17 个原生 GPU fixture 场景。存档包含过去诊断字段，不能作为本机此刻双显卡、显卡坞插拔或游戏验收。
- 数据链路 31 项；消费者生命周期 13 项，最终 timer=0；数值 101 项；UTF-8 体积预算 21 组。
- 读取来源复用 15 组；停止交接竞态 18 组；CPU/健壮性 24 项。
- 热路径 10 组逻辑 + 11 浏览器场景；CPU 消费 9 组 + 30 浏览器场景，同次 A/B 截图哈希一致。
- vue-tsc --noEmit；相关 tracked 生产文件 git diff --check。

原帧时间测试的倒退时间旧断言仍列为历史未通过项，本轮未修改或声称其完整通过。没有原生源码变更，未重复原生整程序构建；未安装到本机或测试实际 5080/AMD/显卡坞状态。

## 证据

Build/Tasks/YMCC-Steam-HUD-Probe/monitor-fps-gpu-source-20261016/

- BASELINE.json、before/：本轮修改前实际源码与依赖。
- BEFORE-GPU-SOURCE-PROBE.json：最初 FPS=120、GPU=99 的错误来源负例。
- FPS-GPU-SOURCE-RESULT.json：15 组、1500 个 wire 对照、工作计数及最终生产输入哈希。
- browser/GPU-SOURCE-BROWSER-RESULT.json、before-selected.png、after-selected.png：实际共享组件对照和像素哈希。
- gpu-consumer/：存档原生输入的消费回归。
- data-link/、lifecycle/、numeric/、wire-budget/、read-source/、read-stop/：统一链路回归。
- cpu-robustness/、hotpath/、cpu-consumption/：工作与输出入口回归。
- ROUND-RESULT.json：本轮结果索引与最终源码哈希。

持续优化目标保持进行中，当前改动只在主线源码，尚未部署。
