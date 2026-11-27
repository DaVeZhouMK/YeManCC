# Steam 陀螺仪控制归属

Steam 预设将触发、抑制、切换、变速和游戏内体感映射交给 Steam Input。YMCC 保留机型轴向适配、传感器采集、校准及既有睡眠/自愈流程，并透传当前有效原始 IMU 与实体手柄输入。

## 原生边界

- 当前有效 activePreset 优先；无效或缺失时回退 preset。Steam 身份不由虚拟手柄人格推断，其他预设仍可在 SteamDeck 等人格下使用本地映射。
- RealStickTestConfig.steamControlled 独立于传感器启用状态。Steam 不执行 inputCaptureMotionTriggered，也不生成 MotionContribution。
- 切换控制归属时清理 toggle 的按下边沿和保持状态，以及速度模式的位移积累。
- Steam 的旧本地 outputMode=disabled 或无效触发键不能阻断采集生命周期；总陀螺仪开关和目标启用门仍生效。
- Steam 的应用回执要求已接纳的原始 IMU，并继续复核既有源与代次。不能用必为零的本地映射作为 Steam 成功条件。
- 原始报告仍使用已有源有效性、电源、前台隔离和最终上报门；Steam 预设不能绕过这些门。

## 配置入口

页面、游戏专属预设展开、快捷键、虚拟手柄联动和启动恢复对 Steam 均保存 motionMode=off、motionTrigger=null、aimingSightsTrigger=null。本地映射控件在 Steam 模式隐藏。旧配置即使未重存，原生和 H5 mock 的 Steam 身份门也阻止本地贡献。

内部 outputMode=virtual-stick 是兼容现有配置结构的载体，并不表示 Steam 模式仍执行本地映射。修改时不得把它改成传感器总关闭，或通过常开映射掩盖原始体感缺失。

## 定向回归

运行 node tools/gyro_steam_handoff_selftest.mjs --out <新的证据目录>，编译真实 Native 的归属选择、触发、贡献、状态清理、摇杆合成和 IMU 帧装配函数。用固定内存源事实验证，禁止启动正常 YMCC、Steam 或访问实体设备。两个故障对照分别移除本地触发门和贡献门，必须出现对应语义失败。

另有既有 sensor-power-consumer 的独立 gyro-cadence 回归。以上不构成完整 Native 构建、整包导出或实体 Steam 游戏验收。
