# 三类默认 Windows 体感夹具

入口 `node tools/hc_default_windows_motion_selftest.mjs`。固定 GPDWin2（GPD/WIN2）、GPDWin3（GPD/G1618-03）、SteamMachine（VALVE/FREMONT），保留原机型分类与空的 canonical matrix 字段。通过当前主线真实选择器／矩阵／ProcessMotion 参数／BUS 校准块、源归属事务和已锁定 GamepadMotion.dll，验证有实际完整来源时的数据处理与手动／自动校准。

期待值直接来自固定 HC 的实际加载和数值回调结果：三个型号没有资源时是单位矩阵。独立带符号六轴数值逐项核对，不使用待测映射函数构造期待值；没有宣称实机轴向或传感器存在。标准 SDK 读取线程和回调的 54 类验证见 hc_windows_sensor_fallback_selftest。

`--before-snapshot` 运行本阶段固定修复前 main.cpp，执行 352 项旧路径断言；新 helper 存在时另有直接边界断言，总数 508 项。相同夹具文件不代表两边断言数相同，结果分别记录。

故障对照：`--default-identity-broadened`、`--default-binding-ignored`、`--default-matrix-corrupted`、`--calibration-stamp-removed`、`--physical-gate-opened`。必须编译成功后出现预期执行断言失败；编译失败不能接受。69 个其他规范类不能进入新增默认入口。

实际 DLL 和文件 API 会执行，但置信度、传感器元数据和交错时点受控；不启动普通 App／Host，不操作传感器或驱动，不访问用户真实校准文件。证据根目录 WindowsDefaultMotion；详见 SOURCE-CONTRACT.json、VERIFICATION.json。
