# 七家族仅传感器校准夹具

运行 `node tools/hc_windows_source_only_calibration_selftest.mjs`。从当前 main.cpp 提取实际准入／ProcessMotion 参数／BUS 校准调用／后续输出门，组合实际来源校准状态机和已锁定 GamepadMotion.dll。原生 Mock 模型目录只提供受控机型、传感器快照和时钟；没有真实设备、普通 App、Host 或 DSU 进程。

覆盖 51 个规范类在 WinRT／标准 SensorAPI、虚拟开启／关闭、手动／自动模式下的无活动物理手柄校准；另检查 18 个其他规范类不进入该入口；新增的 GPDWin2／GPDWin3／SteamMachine 由 hc_default_windows_motion 专项另行验证，不列入拒绝名单。原始采样归属、代际、提供者、缺传感器、非数值、未知选择器、处理后退休以及输出隔离都需通过。置信度由夹具控制，不代表真实静置收敛；ProcessMotion 和文件 API 实际执行。

故障对照参数：`--calibration-admission-removed`、`--calibration-stamp-removed`、`--physical-gate-opened`。必须产生执行断言失败，编译失败不能接受。`--before-snapshot` 用本阶段固定修复前 main.cpp，运行相同夹具证明原缺口；路径固定在本阶段证据目录。

编译工具环境只在测试子 shell 设置，使用本机完整 Windows SDK。消费者源码先校验归档原始字节哈希，再仅规范化 CRLF/LF 比较当前内容，不能忽略其他文本变更。所有校准文件写入独立 LOCALAPPDATA；不得读取用户真实校准文件。证据入口 WindowsSourceOnlyCalibration/VERIFICATION.json。
