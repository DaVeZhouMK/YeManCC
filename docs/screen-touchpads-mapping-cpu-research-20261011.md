# 屏幕触摸板映射 CPU 研究（2026-10-11）

## 结论

本轮只增加研究夹具与测量，不更改生产映射、触觉算法、窗口／穿透逻辑或安装产品。
当前 Core 计算成本很低。新增基线覆盖生产 snapshot、坐标归一化及无竞争 mutex，不能将纯 Core 微基准直接称为整个 YMCC 的 CPU 占用。
优先继续观察真实事件投递、BUS／JSON／InputHost 和设备写入，暂不以近似距离计算或改变手势阈值换取微小收益。

## 可复现测量

工具：

- `G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\screen_touchpads_mapping_cpu_benchmark.cpp`
- `G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\verify-screen-touchpads-mapping-cpu.ps1`

调用：

```powershell
& 'G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\verify-screen-touchpads-mapping-cpu.ps1' `
  -ValidationRoot 'G:\YeManCC-Work\Build\Validation\SteamOfficialKeys-And-TouchpadCpu\MappingCpu'
```

夹具将原生头文件及 JSON 依赖复制成隔离快照，核对复制前后、测量前后的 SHA256，再以 MSVC /O2 构建。
16 类负载，每类 100 万次、7 轮中位数。各轮 checksum 与脉冲计数一致。运行于独立夹具进程，不调用 output()、硬件 writer，不创建窗口／工作线程，不注入键鼠，不修改系统计时器分辨率。

本机本轮代表值：

| 负载 | 百万次墙钟中位数 | 单次墙钟成本 |
| --- | ---: | ---: |
| Core 鼠标，触觉关闭 | 16.538 ms | 0.01654 µs |
| Core 鼠标，中反馈 | 20.300 ms | 0.02030 µs |
| Core 鼠标，强反馈 | 20.314 ms | 0.02031 µs |
| Core 鼠标，中反馈、噪声内 MOVE | 16.985 ms | 0.01698 µs |
| Core WASD／方向键，中反馈 | 21.078／21.335 ms | 0.02108／0.02133 µs |
| Core Deck／PS5 双触点 | 18.671／18.661 ms | 0.01867／0.01866 µs |
| 生产适配器快照，disabled／mouse idle | 7.161／7.158 ms | 0.00716／0.00716 µs |
| 生产适配器快照，Deck idle | 16.776 ms | 0.01678 µs |
| 生产适配器快照，Deck 单触点 | 41.118 ms | 0.04112 µs |
| 生产适配器快照，PS5 双触点 | 50.724 ms | 0.05072 µs |
| 无竞争锁 event + 生产 snapshot，Deck／PS5 | 84.053／83.360 ms | 0.08405／0.08336 µs |
| 无竞争锁 event + 生产 snapshot，PS5 单面双触点 | 82.352 ms | 0.08235 µs |

GetProcessTimes 的本轮 CPU 累计读数呈 15.625 ms 粒度，部分短测量中位数为 0 不代表零 CPU；不据此推导应用 CPU 百分比。
墙钟值也不是硬件上固定的时延 SLA，只是这台参考主机的微基准。

不包含：Windows pointer 投递、BUS/UI 两线程真实锁竞争、JSON/BUS 序列化、InputHost／设备 I/O、可见光标/UI 重绘。也没有证明 CPU 优化后的功能等价，因为本轮没有修改生产算法。

## 优化方向与不采用的方案

### 已有且应该保留的低 CPU 设计

- 事件驱动固定大小 Core，没有独立常驻计算线程；snapshot 借用既有 BUS 节奏。
- 触觉几何在 DOWN 缓存，噪声门先比较平方距离，噪声内 MOVE 不做 sqrt。
- 触觉 EventGate 只在所有权／强度变化或真正的脉冲时进入 Dispatch；静止 MOVE 不反复取 dispatch 锁／时钟。
- 每次反馈为有界且会过期的一条请求，不积压补震；正常成功脉冲只计数并在结束时汇总，避免逐脉冲磁盘日志。
- 默认中反馈 20%，最小 10%、强 30%；强度仅改变下发幅度，当前算法路径相同。本轮相近成本与此实现吻合，但不宣称适配所有设备的实际震感。

### 后续有价值的研究顺序

1. 在真实事件率下测量现有 BUS 的 snapshot 锁等待、FrameContent 到 JSON 的序列化和 InputHost 写入，先识别总成本，不新增高频轮询／常驻监控。
2. 评估单次调用内的 mode／feedbackStrength 局部复用；不要首先增加跨事件可失效的配置缓存。/O2 已可能做部分公共子表达式优化，未做 A/B 前不能承诺提速。
3. 如快照归一化成为真实热点，研究只针对有效变化的缓存，必须把尺寸、布局、mode、persona／可用性、取消、短点按 publish／expiry 纳入失效条件；禁止因为 idle 优化而丢失释放帧。
4. 如锁竞争确实显著，再单独研究 POD 发布方案，不能冒然更换 HWND 所有权或 BUS/InputHost 单写者。

### 不采用

- 曼哈顿距离、近似 sqrt、改用平方距离累加：会改变路径长度与触觉刻度。
- 一旦超过点按阈值就停止记录 travel：尺寸变化和峰值语义可能改变；需完整差分证明，不能想当然。
- 只看单触点鼠标 Core 的一次运行，宣称整个 YMCC 降 CPU。
- 改变 18ms 脉冲、5% 距离刻度、0.6% 噪声门、45ms 限频、80ms 过期、源时间点击判断、delivery-time 短点击发布窗来追求性能。

研究结果与原始来源：

- `G:\YeManCC-Work\Build\Validation\SteamOfficialKeys-And-TouchpadCpu\MappingCpu\mapping-cpu-benchmark.json`
- `G:\YeManCC-Work\Build\Validation\SteamOfficialKeys-And-TouchpadCpu\MappingCpu\mapping-cpu-verification.json`

真实微星掌机点击／游戏覆盖层／设备震感尚未验收，本研究结果不能替代那些验收。
