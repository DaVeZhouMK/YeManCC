# 触摸板 CPU 隔离测量与显示长序列检查

范围按用户澄清，仅触摸板和触摸按键。监控的源码与数据链不属于本轮。此轮先量化已有优化，不为了形成补丁而继续微改生产代码。

## CPU测量方法

新增tools/screen_touchpads_cpu_path_benchmark.cpp/.ps1，复用上一轮实际全头文件模拟的输入/前台API替身，调用生产handleButtonPointer及handlePointer。冻结所有依赖，两份源码仅保留R1按钮重复清理与R2反馈关闭步进这两处变化，差异另存controlled-*.diff。执行顺序Before→After→After→Before，每份每项7轮、每轮200,000事件，汇总14轮中位数。每个事件结果校验和、脉冲数、sink调用数在四次执行中一致；有意消除的取消次数单列。没有改进程优先级或系统电源设置。

| 实际处理路径（模拟外部API） | Before ns/事件 | After ns/事件 | 中位数变化 |
| --- | ---: | ---: | ---: |
| button-owned-medium | 33.72 | 20.84 | -38.2% |
| button-owned-off | 33.60 | 21.00 | -37.5% |
| pad-owned-off | 67.35 | 65.75 | -2.4% |
| pad-owned-medium | 67.61 | 68.75 | 1.7% |
| pad-blocked-off | 65.63 | 67.21 | 2.4% |

按钮两项的Before/After样本范围分离，支持这两条隔离路径加速约37–38%。其绝对开销仍很小；输入API替身、无窗口绘制和设备写入是关键条件。**这不是YMCC进程CPU下降37–38%，也不能外推到Steam或设备线程。**

三个pad路径的变化约±2.5%，样本范围重叠：本次完整handler测量不能确认R2带来显著耗时改善，也不能把小幅上升判定为稳定回归。R2此前已证明少执行距离状态更新；“少一次计算”和“可测的明显收益”必须区分。

## 当前手势/报告投影

另执行既有tools/screen_touchpads_mapping_cpu_benchmark.cpp：16项，每项1,000,000迭代×7轮，输出校验和/震动次数稳定。包含Mouse/Wasd/Arrows/Deck/PS5的Core事件，以及native adapter当前snapshot/归一化和无竞争mutex。

本轮各项中位数约7.6–119.0ns/迭代。该数值仅对应优化编译的合成路径，不含Windows触摸投递、实际总线争用、JSON/IPC、InputHost/驱动、GDI/DWM显示或真实电机。不能凭此排除未测路径的CPU瓶颈。

这些测量支持下一步优先研究实际高频窗口调用、消息积压和故障重试，而不是继续压缩已经很短且无明显收益的纯数学路径。没有因此降低触摸采样、报告频率或显示刷新。

## 当前现场证据

只读核对新增日志包的触摸板条目，已有SteamDeck手势仍标记policy5；当前运行实例最近状态标记policy6且events/queries为0。两者都不能验证当前policy8的画面。具体原始时间/PID见runtime-observation.json和latest-archive-touchpad-only.json；未分析该日志包的风扇或监控内容。

## 产物

证据根：G:/YeManCC-Work/Build/Analysis/Touchpad-CPU-Simulation-R3。Benchmarks内含冻结源码、A/B/B/A原始样本、comparison.json及受控差异；Mapping内含既有16项报告。新增benchmark需要显式OutputDirectory与包含三个声明基线头文件的BaselineDirectory；脚本拒绝覆盖已有receipt.json。

没有部署、创建真实测试窗口、输入注入或硬件写入。真实应用CPU和实机SD消闪结论仍未完成。

## policy8长序列结果

同一工具直接执行当前cursor/visibility实际头文件，原49项回归逐字保留，新增加6个长场景，合计55/55通过。未发现新的确定失败，因此没有追加生产补丁。

| 场景 | 结果 |
| --- | --- |
| 12秒虚拟时间，36,000MOVE＋48,000额外BUS请求 | 500次位置更新，最短24ms；上传和首次显示各1次，最多1条待处理消息 |
| 20,480显隐hint，320批 | 每批1次当前状态查询；不按旧hint重放显示，稳定图像只上传1次 |
| 冷上传/换图失败、连续查询失败恢复 | 2次成功上传；已知帧不会因失败查询被撤下，继续共享原位置期限 |
| 6,144次双触点交替及32轮模式切换 | 不因交替MOVE重装观察器；64条旧消息不能清除新代pending |
| 180轮periodic延迟，最长241ms | 恢复后每轮只提交一次最新位置，无历史位置追赶；两种消息顺序共用预算 |
| 96次手势/retained交接，224次查询失败 | 显示隐藏各96次，图像总上传1次；结束后无尾部轮询 |

所有场景记录环始终最多64条。一次冷图准备可能含上传和显示两个同tick窗口调用；DOWN/UP等原有即时边界也独立记录。不能把这些操作数当成屏幕呈现帧数。55个场景内47,178次断言是重复序列检查，并非47,178个独立测试。

实际Windows通知是否覆盖所有suppression变化、Steam本身异步输出和DWM呈现仍未被模拟替代。SD真实显示效果仍需policy8现场观察。详见Cursor/REPORT.md及Cursor/verification-summary.json。
