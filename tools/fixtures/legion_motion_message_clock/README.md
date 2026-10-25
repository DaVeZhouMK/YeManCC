# Legion OS queue message clock — parent integration contract

纯函数位于 `native/legion_motion_message_clock.h`，namespace `ymcc::legion_motion_clock`，C++17：

```cpp
std::optional<std::uint64_t> messageReceiptTick(
    std::uint64_t now64, std::uint32_t messageTime32, std::uint64_t maxAgeMs);
bool belongsToEpoch(std::uint64_t receivedAt, std::uint64_t epochStart);
```

`age=uint32(uint32(now64)-messageTime32)` 按 unsigned modulo 2^32 计算；仅当 `age<=now64 && age<=maxAgeMs` 才返回 `now64-age`。容量/全局状态/系统 API 均无，不调用 Win32、不读 clock、不新建线程、不影响其它 family。

zero/边界明确：age==maxAge 接受；age==now 接受并返回 optional{0}。`now=0,message=0,maxAge=0` 同样返回 optional{0}，不是 nullopt；但 `belongsToEpoch(0,任何cutover)` 均 false。messageTime32==0 在低32位回绕后是合法时刻，不能当缺失。epochStart==0 时，所有非零 receivedAt 均满足时间比较；receivedAt==cutover 即使非零也拒绝；receivedAt==cutover+1 的下一可证明新样本允许。GetMessageTime 的 signed 返回值由调用方保留原32bit转换，不在工具里猜负值含义。

语义是**OS 队列消息时间**，绝不是物理采样时刻或固件时间。调用方负责 now64 与消息时钟处于同一 uptime-ms 域，并传已经抓取的值。

## 回绕/半环证明边界

32位字段每2^32ms回绕。函数选择最近的同余过去候选，不宣称对任意远时间可唯一反推。若事件已经老了完整一圈或多圈，低32位可能看似很新；单靠这三个参数无法识别，maxAge 很小也不能证明不存在此类别名。

推荐父线程使用其已确定的短 maxAge（小于2^31ms）并确保队列事件是附近一圈内的实际消息。这样普通近未来值在 unsigned delta 下变成大 age 并被拒；半环处的先后顺序本来歧义，工具不擅自添加 half-range gate 或 clamp maxAge。对很大的 maxAge，某些真正未来的 low32值也可解释为上一圈过去，函数可能接受该过去候选——这是表示的信息不足，不能称通用“未来拒绝保证”。测试明确执行这些别名边界，不给虚假的唯一性结论。

`age>now` 单独拒绝启动前/64bit减法下溢候选；uptime不满一圈时它也可拒绝某些future值。不同 GetTickCount64 epochs、时钟重置或完全错误的调用方域没有自动识别。

## 父线程责任

父线程拟持有 atomic epochStart，并在源 revoke/sidechange/重新注册推进epoch之前更新；raw callback 捕获 now64 和消息time32，只为Legion归一时间，sample.capturedAt 使用返回值，publisher/snapshot/final issue 分别核 cutover。工具只提供纯算术和非零比较，不证明这些调用点/原子排序已接通。

millisecond相等使用严格 > 门拒绝：同刻度无法区分cutover前后事件，两边均拒绝，下一可证明新tick恢复；仍保留父线程的 generation/captureEpoch fence。没有真实PBT、睡眠、设备消息排队或固件采样时间保证。

测试只新增本夹具和 `tools/legion_motion_message_clock_selftest.mjs`，不改 main/旧fixtures。MSVC Release/Checked 真实编译执行；覆盖 low32 rollover、32bit sign位、future、stale、threshold equality、now<age、optional zero、epoch零/相等、完整圈别名与半环。整数 oracle 以独立 uint64 modulo 公式核对，未调用 Win32/真实设备/DLL。所有输出在 workspace `Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/LegionRuntime/MessageClock/<fresh>/`。
