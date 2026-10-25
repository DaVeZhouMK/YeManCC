# controller_motion_session：父线程 Legion 消费者接线

这是独立同步 CPU Session 组合，只新增本头/自测/本夹具目录，复用并且不修改 `controller_motion_processor.h`。main 由父线程接入；本任务不声称完成物理支持、现场验证或主线整体接入。

## 已固定 API

```cpp
#include "controller_motion_session.h"
namespace cm = ymcc::controller_motion;

cm::Session session; // capture 的现有单一串行上下文长期持有
cm::Ops ops{create, destroy, setMode, setOffset, process};
cm::ReadOps reads{getCal, getGravity, getPlayer, getWorld};
// nowSeconds 是调用方 steady 时间，以 double 秒计；不得每 tick 重置 session。
if (session.select(key, ops, reads, calibration, nowSeconds)) {
    const auto result = session.process(key,
        cm::RawSixAxes{gx, gy, gz, ax, ay, az}, tickNowSeconds,
        cm::ReadRequest{wantPlayer, wantWorld});
    if (result) {
        // result->axes: 原始六轴；result->calibrated / gravity: {x,y,z}
        // result->orientation: {w,x,y,z}，此 Session 独有 Madgwick quaternion
        // result->orientationFinite=false 时必须拒绝 wire，或由主线显式选安全姿态
        // result->player / world: optional<{x,y}>，仅请求时有值
        // result->deltaSeconds: double 时间差；原生 process 接收该值的 float 转换
    }
}
// Stop/睡眠撤销/卸载回调 DLL 之前，在相同串行上下文：
session.revoke(); // 或 clear()，之后必须更新 captureEpoch 才能重选旧物理源
```

避免每 tick 读 json：先以 `session.matchesSelection(key,ops,reads)` 判断；true 时跳过 select/磁盘校准读取。失源时 `if(session.active())session.revoke();`，主线随后递增 captureEpoch；新的报告与新 token 可恢复。query 只检查当前 key/全部回调，不读取校准或调用 native。

公开方法：`bool select(const Key&,const Ops&,const ReadOps&,const Calibration&,double nowSeconds)`；`optional<MotionResult> process(const Key&,const RawSixAxes&,double nowSeconds,ReadRequest={})`；`active()`；`matchesSelection(Key,Ops,ReadOps)`；`handleFor(Key)`；`revoke()`；`clear()`。Session 禁止 copy/move。`getCal/getGravity` 签名 `void(void*,float*,float*,float*)`；`getPlayer/getWorld` 签名 `void(void*,float*,float*,float)`，可直接填现有 x64 actual GM pointers。

`getCal/getGravity` 是必需读取，缺失时 select 失败并撤销旧资源。player/world callback 允许为空；只有实际请求对应输出时才要求其存在，缺失则在 ProcessMotion 之前拒绝整个 tick，不改变上次处理时间。两个输出可同时请求；只有 player 请求才调用 `getPlayer(...,1.41f)`，只有 world 请求才调用 `getWorld(...,0.125f)`。未请求的 optional 为空，没有造零的输出或额外 DLL GetOrientation 调用。

## 时间、生命周期与姿态

- 新 handle 选择时间即 `nowSeconds`。第一帧 delta=`tickNow-selectionNow`；后续 delta=`tickNow-previousSuccessfulProcessNow`。同 Key、同五个 Ops、同四个 ReadOps 的重复 select 不重建、不重写校准、不重置时间/姿态。
- 在 double 精度先相减，`MotionResult.deltaSeconds` 保留 double；仅为原生 float ABI 转换一次。无 finite/负数/零/tiny/大值 dt 准入或 clamp；NaN/Inf 亦按输入差值语义前传。这里没有时钟采集。BUS 每 tick 可对当前 ControllerState 的相同样本重复 process，时间来自真实 tick，不做 packet 去重。
- raw 六轴分别 finite 才可处理，未知/无效 key、撤销或 create 失败时不能处理。拒绝 tick 不推进时间。Manual 模式/全零校准和全零静止原始样本合法。
- 姿态使用当前 main.cpp `ymccMadgwickUpdate` 的非零梯度公式、beta=0.1、初始 WXYZ `{0,0,-1,0}`；状态完全私有，不共享 `g_ymMadgQ` 或 `g_gm`。读取 calibrated gyro/gravity 后喂 `(-deg2rad(cx),+deg2rad(cy),-deg2rad(cz),-gravX,+gravY,-gravZ,dt)`，符合实际历史 `HandheldCompanion/Helpers/GamepadMotion.cs:122-133`，不是题述的 Utils 路径。档案 `Archives/HC/pre-candidate-20260902-1535/active-hc-source/Misc/MadgwickAHRS.cs` 的 SHA256/git blob 已与固定历史 primary tree 的 `6ec68719dcaa3603763b188940ff80af21b9d013` 核对；仅作公式来源佐证。当前 main 公式具有 dt<=0 return，HC 原 filter 无该 guard；数值 oracle 明确使用 main 范围，未宣称全部 HC 行为等价。
- 区分时间前传与公式行为：native process 总是收到直接差值；移植公式内部保留 main 原有 `dt<=0` 和零 gravity norm 早退。没有额外 dt finite/clamp 检查。新增 controller-only 精确零梯度修复：gradient squared norm==0 时将 s1..s4 保持为零，继续 gyro 积分及 quaternion 归一化；静止重力精确对齐时保持 Q，非零 gyro 仍积分。不引入 epsilon 或稳态阈值。非零梯度保持原式。Windows main 公式不改，不声称完整 HC bit 等价；某些非有限 callback 输出仍可能传播 NaN。`MotionResult.orientationFinite` 标记四分量是否 finite；它不是完整正确性证明，false 时父主线必须不 admit wire/Host，或显式用安全姿态。HC wrapper 的旧 deltaTime>=10us 缓存规则未移植，因为本轮明确要求实际 tick 差值前传。
- Key、任一资源 Ops 或任一 ReadOps callback 改变均重建，旧资源先退役，姿态/时间重新初始化；不只凭 handle 地址判断，allocator 重用地址也必须复位。
- Owner 原接口只比较五项资源 Ops。当仅 ReadOps 变化且 Key/资源 Ops 相同时，Session 在先 revoke 后重建其私有 Owner；Session 精确 token 集合跨这次内部重建保留历史，防止旧 key 复活。更换当前活跃 key 的回调允许重建；clear/revoke/create 失败后同 token 不可重选，必须新 epoch。

## 主线责任和局限

校准仍只取 controller 专用 `calibrationKeyFor(key)` 的 physicalIdentity+sensorIndex 持久键，不带代际，不接 Windows gyroDeviceId 缓存；四数值 Calibration 无法认证外部取值来源，主线负责正确查表。Session 不负责 HID、DLL 加载、线程、物理 identity/轴协议/单位/代际来源或设备准入。

所有调用必须由原有单 owner 串行上下文执行；C ABI callbacks 不应抛异常或重入，未注入测试 throw。必须 Session revoke 完成后才卸载 callback DLL。handleFor 只提供当前 token 的借用 handle，不能跨替换保存或自行 destroy。

**已知空间局限：** Owner 的 exact Key history 在其实例生命周期内无限增长；本 Session 还保存 attempted Key 元数据，以跨 ReadOps 导致的私有 Owner 重建保留失效历史，两者均随不同代际数增长。本轮只记录，没有修改旧 Owner 或另建全局框架。不得通过每次 Stop 重建 Session 来绕过失效历史。

验证脚本 `node tools/controller_motion_session_selftest.mjs` 使用现有 MSVC x64，真实 Release/Checked 编译执行和指定语义变异体。纯内存 effect sinks；非零梯度数值对照来自只读提取的当前 main Madgwick 原函数及历史 HC 包装映射，零梯度以初始 Q 固定值和独立闭式单步 gyro 积分核验，不加载 DLL/访问 HID。证据在 workspace `Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/LegionConsumer/Session/<fresh>/`，含 pin/源片段与 SHA256/编译命令/日志。父线程可并行改 main 的其它部分；验证只核对提取公式片段未变化，不把整个 main 当成独占输入。
