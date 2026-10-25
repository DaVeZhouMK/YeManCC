# Controller GamepadMotion CPU owner：父线程接线说明

生产接口：`native/controller_motion_processor.h`，namespace `ymcc::controller_motion`，C++17，Windows x64 actual GamepadMotion C ABI 函数指针可直接赋给 `Ops` 的五个字段。此交付尚未接入 main；不能据此宣称 controller 功能已实现。

## 接口和调用约定

- `Key{std::string physicalIdentity; uint64_t power, rawGeneration, captureEpoch; unsigned sensorIndex;}`：identity 精确比较且非空，三项代际必须非零；sensorIndex=0 合法。Legion 左/右必须使用各自索引。这里只核调用方提供的 token；真实物理 identity 来源仍由父线程证明。
- `calibrationKeyFor(Key)` 返回 `optional<CalibrationKey{physicalIdentity,sensorIndex}>`。这是结构化持久键，不包含任何 generation，避免自行拼接字符串产生歧义。无有效 identity/generation 时返回空；没有 Windows sensor fallback。主线应只从这个 controller 专用 key 读取校准。
- `Calibration{x,y,z,weight}` 要求三个 float 全 finite，weight>=0；允许零偏移、weight=0、任意有限幅度及 INT_MAX。没有 confidence 准入或轴阈值。
- `Ops{create,destroy,setMode,setOffset,process}` 全部必需。主线自行准备 DLL 和函数指针；owner 内没有 DLL 加载/符号查询。callback 按 C ABI 不应抛异常，也不得重入 owner；fixture 不注入/不测试 callback throw。没有 catch/noexcept 包装吞掉资源异常，析构显式 `noexcept(false)`。
- `bool select(const Key&, const Ops&, const Calibration&)`：成功新建后依次调用 `setMode(handle,0)`（Manual）、`setOffset`。同 Key+同五项 Ops 且校准有效时返回 true，保持原 handle/原校准，不重复写新传入的有效校准。任何其它选择先 revoke 当前资源，再校验/创建；包括无效 key、缺 callback、无效校准在内的失败都会使旧授权失效。
- `bool process(const Key&, const RawSixAxes&, float dt)`：仅当前精确 key+handle 可处理；六轴分别 finite，直接按 gx/gy/gz/ax/ay/az 顺序传入，静止全零合法。dt 原样传递，单位、零、负数、非有限值都由调用方决定；不 clamp/推导稳态。调用方只在真实 sampleReady 时调用，select 不生成任何样本。
- `void* handleFor(const Key&) const`：只对当前 key 返回借用 handle。供父线程使用已核同 DLL 的读取回调；下一次 select/clear/revoke/析构使借用失效，不能跨边界保存或自行 destroy。
- `clear()`/`revoke()`：先分离当前状态，再通过创建该 handle 时保存的 destroy 回调销毁一次；重复调用及后续析构不会重复销毁。对象显式禁止 copy/move。

## 失效与校准边界

owner 保存本对象生命周期内已尝试 create 的精确 Key 集合。退役或 create 失败的 token 不能重新 select；Stop->Start 必须获得新的 captureEpoch，即使 power/raw 不变。切换 A->B->A 也拒绝旧 A。仅当前活跃 Key 更换 Ops 是允许的替换，仍先用旧 destroy 退役、再通过新 create 建资源；替换失败后该 token 同样失效。集合只保存 Key 元数据，不保存旧 handle，随单个 Owner 析构释放；空间随本对象尝试过的不同 token 数量增长。

此接口固定只接收四个校准数值，不能鉴别调用方从哪一个外部缓存取值。它保证传入值准确用于本次新建 handle、持久键不混代际、内部不访问/回退 Windows 校准。父线程必须保证 `Calibration` 来自 `calibrationKeyFor(key)` 对应的 controller 记录；缺记录时由父线程决定显式零校准或拒绝选择，不能转读 Windows `gyroDeviceId` 缓存。该责任不能靠声称 owner 能认证任意传入 float 来替代。

## 最小接线顺序

1. 将 Owner 放在现有 capture 单一串行所有者上下文，生命周期覆盖 Stop/Start 和睡眠代际；不要每 tick 或每 Stop 重新构造来丢弃失效历史。不新建线程。
2. 取得经主线核验的 controller identity、power/raw/capture 代际、sensorIndex；取 controller 专用校准。
3. 直接填充 `Ops ops{gmCreate, gmDestroy, gmSetMode, gmSetOffset, gmProcess};`，调用 `owner.select(key, ops, calibration)`；false 表示本 owner 当前无资源。
4. 真实 sampleReady 时 `owner.process(key, RawSixAxes{gx,gy,gz,ax,ay,az}, dt)`；成功后可在同一所有者上下文用 `handleFor(key)` 配合 DLL 读取回调。dt/轴的来源和单位沿用父线程实际协议。
5. Stop、撤销、睡眠边界按主线 token 语义 `owner.revoke()`；下次使用更新代际。卸载/替换 DLL 前，先在相同上下文 revoke，保证回调仍有效；owner 不管理 DLL 生命周期。

Windows 全局 `g_gm` 及其校准/processor 均未改动。CPU owner 不执行 HID 读写、设备枚举、Host/PBT、WinRT、睡眠操作或 DLL 加载。本夹具使用纯内存 effect sinks，模拟 create/destroy、校准顺序及 process 参数，并在 MSVC Release、Checked 分别真实编译/执行；测试断言独立于 `assert`/NDEBUG。编译失败或崩溃不能计入 semantic mutant 检出。

运行：`node tools/controller_motion_processor_selftest.mjs`。可传 `--out` 指定 workspace `Build/Validation/HC-ModelCapabilities-20261017/ControllerProcessor/` 下的新子目录，`--vcvars` 指定本机 MSVC 环境 batch；不会覆盖已有 source-bindings.json。证据包含源 SHA256/快照、编译参数/日志、两配置断言结果及每个行为变异体的两配置命中记录。
