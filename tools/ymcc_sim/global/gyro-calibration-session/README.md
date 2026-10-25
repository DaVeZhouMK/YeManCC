# 原陀螺仪校准会话的统一验收适配器

导出：`getGyroCalibrationSessionAdapters()`，测试项：`controller.gyro-calibration-session`。

原测试 `tools/gyro_calibration_session_selftest.ts` 和正式 `src/bridge/gyroCalibrationSession.ts` 不修改。原脚本固定写 `../../Build/Validation/GyroVirtual/…json`；适配器设置真实工作目录为 `<自有case>/positive/cwd/deep`，因此该相对路径自然落入 `<自有case>/positive/Build`。没有文件系统替身、路径映射、真实共享Build写入或环境输出覆盖。

每一编译/执行在现有 `host-process.ps1` 的自建Windows Job内完成。Node、esbuild JavaScript模块和包、实际esbuild原生程序和包、PowerShell执行文件都明确记录身份；构建后保存完整metafile并与逐字onLoad读取的4个源文件核对。实际bundle、map、metafile、原stdout/stderr、JSON和工具/源码在最终sourceHashes中逐文件绑定。

原8组会话实际覆盖confidence、锁屏障、超时、运动、NaN偏移、旧电源代次、旧校准代次、缺加速度和阈值边界。原报告的 `MOCK-READY` 和字段逐字保留。它的 `physicalBaseInputOutsideGyroGate` 字段只属于原报告声明，不能据此宣称真实物理输入隔离已执行。

独立补充脚本只调用正式类，4组退出场景、13个实际检查：手动锁定后abort、invalidation、restart拒绝前校准代次、收集取消后的late timeout/sample/lock不复活。

两种私有正式源码故障只改副本，不改原断言：删去电源代次比较、让lock-requested绕过safe-zero。必须先bundle成功，再观察原测试精确断言错误和自然exit1；编译错或启动错不计检出。

新实现的开发失败保留：r1变量名遮蔽运行时process，r2结束合同误把13检查写成14；修复仅发生在本目录包装，原测试和产品源码从未修改。

边界仅校准会话策略。stillness/confidence是夹具观察值；未执行真实GamepadMotion算法、物理IMU、OEM/MSI provider、完整控制器motion、原生应用或系统睡眠。

单独运行：

```powershell
node tools/ymcc_sim/global/gyro-calibration-session/run.mjs --evidence-dir <新的绝对自有目录>
```

协调者串行登记后，从现有PowerShell global入口执行该id并运行AuditRegistered。原ADAPTER_EXCLUSIONS对直接原脚本的固定共享路径限制保持；不能绕开本适配器直接执行原脚本。
