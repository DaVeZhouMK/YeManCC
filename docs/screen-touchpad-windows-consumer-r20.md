# Windows Steam SD 接收与鼠标生成链 · R20 · 2026-10-16

用户确认“直接模拟鼠标不经过 SD 正常，虚拟 SD 经 Steam 合成鼠标仍闪烁”。上轮 SDT 对照提示配置隔离是变量，本轮继续实际日志关联，并首次沿固定 Windows Steam 原生二进制追到输入缓冲、type9 字段归一化、触摸板 touch/click 状态及最终鼠标生成叶。没有修改显示、报告、压力、释放或 Steam 配置。没有完成实机闪烁修复。

新增证据改变下一步：已有失败的三段移动闪烁不支持“每次都是记录到的 layout/Lizard 重载”；固定 Windows 解码节点确实独立读取 touch、click、坐标、压力；通用鼠标生成器存在直接 SendInput 与 SecureDesktopInput 两条路，不能只把提交成功当成最终输入送达。仍不能据静态路径判断现场选中哪条分支。

## 来源及证据边界

证据根：`G:/YeManCC-Work/Build/Analysis/Touchpad-Data-R20-20261016`。

Steam audit：13 areas / 395 candidate files / 0 missing anchors / 0 unmapped，只代表源码维护索引。当前主线 `native/steam_desktop_binding_evidence.h` 的 contract 是 stored literal Ctrl/Alt 摘要；本轮仅读取它，没有扩展或改变其它按键任务的协议。

静态样本 `D:/game/steam/steamclient64.dll`：26544280B，SHA256 `e9045710ec7ee47d7b7488d1c5dda2d933dec7e251e965967a5c48f4df68eb4e`，PE image base `0x138000000`。逐字节等同既有官方缓存 `WEB-LINUX-PROCESS-LIFETIME-20261015/bins_win64-same-version.complete.zip` 内 steamclient64.dll。该 ZIP 为64307884B、SHA256 `2378aa4845fea7b08d3cc33a88db240db20e33cf8bb934f7f4037d89a1bac1e9`；所引用已保留 manifest 的哈希和成员核验在 `SteamNativeMouseReview/official-cache-provenance.json`。这是历史固定1791918634官方包，不是“当前最新 Steam”声明。静态 SteamUI 样本只用于 import 探索，不用于以下具体接收链归因。

未打开或调用 Steam 模块、设备、输入接口、callback 或进程内方法。R22后续将进程单独JSON核验，确认父机Steam20044从2026-10-15 14:16:38 +08持续存在；R20混合PowerShell表格省列造成“未运行”的误判，不能把原表格当作进程缺失证据。R20当轮没有读取运行中内存；R22另有明确只读回执。13:46 ZIP 没有对应模块原始字节；所选6项身份文件/日志中未找到上述模块 hash 或版本数字（`field-steam-binary-identity-text.json`），这不是对全包的身份缺失证明，亦不能把父机文件视作微星 loaded image 验真。

只引用 `AcceptedBlocks/` 中明确指令边界的10块共2098B、RTTI和IAT；每块都重新对照官方 ZIP 字节和哈希，见 `accepted-block-verification.json`。其它 asm 为探索性的 pdata 片段/候选，可能含跳表或相邻函数，不作为完整函数证明。直接控制流枚举不会自动解析间接调用和跳表；不以反汇编文本行数代替完整调用链。

## 失败时段与 layout / Lizard 的实际对应

以用户明确失败的 `YeManCC-logs-20261016-134659-屏蔽失败-虚拟收并稳定了一点点-触控板闪烁-Steam唤醒可操控到卡死.zip` 为依据，归档SHA256 `8280bfb15634cb7e5618188629bbf9bf437bf109f297eb2aac1a9eb739f8aa05`。移动 flags0/1 时段继承 R17 已分析轨迹，并非此轮重新解释绘制。原 Steam 日志按秒记录，wall 与 cursor tick 对齐沿用前轮估算；不声称毫秒原子因果。

| 移动闪烁时段（+08） | 前后2秒内记录到的配置/模式事件 | 最近前/后事件 | 对候选的影响 |
|---|---|---|---|
| 13:42:36.851–37.914 | 无 | 之前13:42:26加载本地 desktop；之后13:42:50进入 privileged Lizard | 不支持此段每次可见性变化都由日志中的重载解释 |
| 13:42:47.648–48.976 | 13:42:50有3条 Lizard 转换记录，发生在该段结束之后 | 前一加载仍为13:42:26 desktop | 接近转换但未覆盖闪烁段；1秒日志精度不能建立精确对应 |
| 13:46:19.242–19.461 | 无 | 之前13:46:09 desktop；之后13:46:25 privileged Lizard | 仍缺少重载作为该段直接触发的记录 |

13:42:04 相邻属性明确记 Controller15 / Type4 / PID4613(0x1205) / serial FX1A00000001；该编号不是随意假定的设备身份。不能据此证明现场没有另一设备，或把slot15身份推广到其它连接周期。证据：`SteamTimelineReview/timeline-correlation.json`、`slot-and-context-evidence.json`、原两份日志的条目哈希。没有把“没有相关日志”提升为“绝无模式切换”。

现有 stored binding 导出只保留 group ID 和 source/preset引用，不含 group mode、输出命令和实时选中 preset；`activePresetKnown=false`。所以看到 right_trackpad group14，不能声称现场确为 mouse、mouse region、joystick 或判断 cursor show/hide。只给此 stored 摘要加字段仍不能证明 Steam 已加载哪份配置，本轮没有为了补弱证据改动共享按键摘要。

## 固定 Windows Steam 的接收与字段归一化

RTTI 与表引用分别定位 `CSteamControllerAbstraction` 的 read slot0x20→`0x67b190`、normalize slot0x68→`0x679c60`；后者也被 Chell/SteamOSHandheld 表引用。此处使用 RVA，不是可直接用于其它版本的运行地址。

1. **输入缓冲**：`0x67b190..0x67b1ee` 先清 `this+0x18` 的73B，把这个地址传给底层适配对象虚表+0x18（capacity0x49、最后参数0）。返回长度>0、至少4B、缓冲开头word为1才置 `this+0x61` 有效。底层适配方法未展开，不能声称其具体 read API、实际长度或完成成功。
2. **消费门**：`0x679c60..0x679ce7` 检查有效标记及 type边界；后续type9分支 `0x67a204` 检查 `this+0x1a==9`，`this+0x1c` counter与旧归一化状态+8相同则不按新type9继续。清本次输入缓冲不等于发布了一份触点全零；有效/类型/计数器门和调用者仍需分别考虑。
3. **type9复制**：新counter分支先清归一化状态的0..0x8F，再复制 buttons、XY、pressure及其它字段。后续仍有机型/固件相关变换，不能把全部路径说成原样透传。

| 字段 | 类内输入缓冲地址 | 相对输入缓冲偏移 | 归一化状态地址 |
|---|---|---:|---:|
| 64bit buttons | this+0x20 | 8 | out+0 |
| 左 XY | this+0x28 / 0x2A | 16 / 18 | out+0x0C / 0x0E |
| 右 XY | this+0x2C / 0x2E | 20 / 22 | out+0x10 / 0x12 |
| 左/右压力 | this+0x50 / 0x52 | 56 / 58 | out+0x8C / 0x8E |

关系来自同一类的缓冲地址和消费偏移，不只靠SDL或SDT注释猜测。`decoder-field-map.json` 保留具体映射及限制。当前YMCC raw布局对应上述偏移。相同的静态字段不证明微星实际输入每帧都完整，或说明底层适配一定按原始HID字节填充而未转换。

## touch 与 pressure 的下一层关系

RTTI `CTrackpadSource` 表 `0x12e55e8` 的slot0x78指向 `0x672990`。已核最初状态建立部分：

- 左分支从归一化 buttons 的bit19取接触、bit17取点击；右分支使用bit20和bit18。接触先写 `this+0x27`，点击写 `this+0x26`。
- 左/右压力分别读取归一化 `+0x8C/+0x8E`，调用 `0x66eea0` 变换并存入另一个字段；这个小helper只有寄存器数值运算，没有写回touch状态。
- 随后的 selector 条件可走压力/force相关helper并覆盖click字段；本段直接指令没有根据pressure0覆盖touch字段。

这为“压力0与touch位可以独立存在”补上固定Windows实现节点的证据，而不只是公开SDL/SDT的推断。它不能排除其它模式、其它对象、间接helper或未追到的HID适配路径改变触点，也不能直接证明本次闪烁由哪种数据异常引发。因此不恢复8192轻触压力、不用加压试错，不延迟UP或人为保持接触。

## 鼠标生成的两个出口

RTTI `CInputGenerator` 表 `0x1308CE8`：slot0x28→`0x714BB0`，slot0x130→`0x715400`。

- `0x714BB0..0x714C55` 接收整数dx/dy，构造40B的INPUT_MOUSE，`dwFlags=1`（MOVE），time/extraInfo为0。若对象+0x60为空，直接调静态IAT中的USER32.SendInput(count1, size40)；非空则传同一INPUT到`0x944960`。此叶未检查零位移，因此“所有Steam路径都零位移不发”没有证据；上层是否门控仍待追。
- 前置虚表+0x130的具体实现 `0x715400..0x71541D` 是29B活动字段/计数更新，没有光标或输入API调用。不能把这个通知叶直接命名为每包HideCursor。
- `0x714480..0x71452F` 构造器只在其布尔参数为true时尝试创建+0x60对象；连接不成立则保留空指针走直接SendInput。不能由 Lizard日志或前台名称判断这个布尔值。
- `0x944850..0x9448EA` 使用具名 `Global\SecureDesktopInput_mem`；`0x944960..0x944B0A` 将INPUT写入共享记录，使用128槽索引（`&0x7F`）并发布序号。这段没有最终Windows输入消费确认；它返回提交数量并不代表服务端或目标窗口已经消费。这里不是SDK USB/IP报告队列，两个队列不能混算。

`CRelativeMouseMode` 的独有方法 `0x8D0890` 可静态看到从坐标/增益产生浮点鼠标量再交给传入对象虚表+8；该对象与上述具体 `CInputGenerator` 的完整桥接尚未建立。本轮有意不把两条分别找到的节点画成已经闭合的端到端执行链。绝对鼠标及UI鼠标另有入口，不能用通用relative叶解释所有现场行为。

这些结果使下一步需要核验的层更明确：HID适配实际读取的report/counter/touch边沿、选择的Mouse模式、实际输出生成器/出口和隐藏调用归属。不能再由“API成功”“FIFO采样0”“mouse位置变了”推断整条消费正常，也不为绕过这些盲区替换仍可用的SD输入。

## 本轮产出与未完成项

本轮是离线日志/二进制分析。仅写任务证据与本文、触摸板维护索引；没有生产代码改动、没有新增常驻线程/timer/日志频率或运行时CPU负担，没有运行设备/输入/游戏测试、打包或部署。已核源码/静态字节的完整性不等于实机行为测试。

仍未完成：现场真实连续输入数据是否含短促off/on、Steam实际选中的布局/模式、实际mouse出口及具体隐藏者，以及最终用户可见的闪烁改善。前一轮R18的已有低成本逐成功提交边沿继续保留，但不能把未运行的新诊断算作现场证据。整体目标保持进行中。


## 独立复核补充：真正存在的清 touch 条件

`NativeReview/REVIEW.md` 独立按三段 chained unwind 核全 `CTrackpadSource` 根 `[0x672990,0x673004)`：1652B、393条指令，SHA256 `36b3a1c2fb852ca1d4038dc32aa6c93e7f7e2e26cfbc447899dd50ee905cff1b`。它纠正探索稿少列的一条4B指令（0x672e50 `movss xmm6,[rdi]`）；探索稿不再用于完整根证明。此前接受的10块2098B选定范围不包含这处遗漏，字节核验仍成立。

完整根明确存在第四个 touch 写点：前三个为 raw bits 赋值；`0x672FB4` 则清 `this+0x27`，触发条件是后段处理得到的坐标超出 `[-32767,32767]` 或比较 unordered。此坐标段仅在 touch非零且对象 selector 属于 `{2,3,4,5,10}` 时进入；R21已确认该selector的枚举域尚未归属，不能套用EControllerSourceMode同数值名称。它提供了一个比笼统“显示问题/压力太小”更具体的可达数据候选：**原 touch=1，内部坐标处理失败后可变成 touch=0**。

但还不能直接归因或修改。当前 YMCC 最终 `normalizeContact` 已把 XY 限在 `[-32767,32767]`；Core内部允许-32768并非最终Deck输出证据。也尚未证明现场对象的selector允许进入此段、滤波有限输入能越界或参数异常。用户描述“大幅滑动更明显、此前平滑更糟”只作为待解释现象，不代替分支执行证据。继续审阅坐标helper而非增加我们自己的滤波、缩小触摸范围或强行保持touch。

## 真正虚拟 SD 生产者的时间对照

SDT 不生产虚拟SD，因此此处另外固定 HHD 官方实现作限于counter/时间的对照：`hhd-dev/hhd` commit `ed14d6b28f6497e35905f9c7e914ba67370208d0`，提交2026-10-15T22:33:29Z；仅下载 `src/hhd/controller/virtual/sd/{__init__.py,const.py}` 和空包初始化文件，不执行。URL、完整SHA、原字节见 `VirtualProducerTiming/source-receipt.json`，不是 Windows 或本机运行结果。

其SD report header为01/00/09/40（71-73行）；consume最终发送分支（366-369行）每次真正发送把独立counter写到bytes4..7，再按uint32递增/回绕。它没有为了125/250Hz或IMU时间把counter伪装为固定4ms时间戳。IMU timestamp事件用于允许发送的门控，另有失效退路；不能为消除屏幕触摸闪烁将其gyro调度方案直接移植到YMCC。

`const.py:51-53` 的右pad坐标尺度为65533、offset=-32767（Y反向），力由点击事件单独设置。源码表达式显示坐标端点留有约1单位差异，但底层axis encoder尚未展开；不能把这1单位差异当作已证明的Steam滤波安全余量或照抄缩放，更不能认为它能解释大幅滑动闪烁。

这项对照至少排除了“公开虚拟SD生产者用counter模拟绝对时间，因此YMCC必须把counter每次加2/按墙钟重写”的无根据修正方向。当前按实际发送递增的语义不因该假设改变。


## 坐标处理复核的最终边界

新增 `NativeReview/FILTER-REVIEW.md` 已沿坐标 reset、hold、update、getter、初始化和状态校正收敛到20个函数/leaf/thunk、6566B。该报告独立于原完整根review，原证据保持不变。它给出两个需要同时保留的结果：

- 新接触的首个普通样本将四状态初始化为 `(x,y,0,0)`，getter取前两项；有限合法输入下这条直接路径没有放大坐标。未初始化的hold分支保持零输出。因此不能把初始落指直接归因为该预测式越界。
- 后续样本不是已证明的坐标凸组合：状态预测含 `position + 0.005f * auxiliaryState`，保持分支也可把保存输入加辅助状态；最终还要做矩阵残差校正。局部矩阵参数虽然有凸插值，也不能保证最终坐标留在原域。已展开入口没有建立报告实测时间间隔的数据流；0.005不能直接解释为“设备必须200Hz”，更不是提高刷新/发送频率的依据。

这是具体、可继续检验的数据消费候选：若实际预测/校正结果越界，Steam会在raw touch=1的同一处理中清掉touch。**Host逐成功提交touch稳定，不能排除这个更下游的内部松触。** 反过来，无clamp/存在预测公式也不能证明可达的真实历史一定产生域外输出。仍缺实际滤波模式、辅助状态、增益/残差、最终XY与range guard结果的同实例证据，没有据此改压力、改包计数器、限小触摸区域、绕过滤波或更改显示。

本轮据此将下一步从“反复验证报头/显示”移到“Steam内部坐标结果与touch的状态变化”，同时保留原始接收数据的核验。整个闪烁目标未完成，生产修复保持hold，不把这轮字节审阅或数学条件当实机验收。


R21纠偏与新证据：T+0xb8不是已证明的SourceMode字段，不能把4/5解释为AbsoluteMouse/RelativeMouse来证明此分支准入。已恢复与两份现场摘要SHA完全一致的17477B布局，右pad的两个已存preset均为absolute_mouse，未显式设置mouse_smoothing；固定对应构造器key43当前/默认int20，仍不是runtime值。详见 `docs/screen-touchpad-field-layout-r21.md`。R20封存机器证据保持不变。
