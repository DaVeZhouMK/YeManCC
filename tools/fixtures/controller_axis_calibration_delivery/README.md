# 校对事件 UI 线程投递回归

运行：

```powershell
node tools/controller_axis_calibration_delivery_selftest.mjs --out "D:/验证输出/校对投递"
```

测试从当前 `native/controller_axis_calibration_runtime.h` 提取实际 `observe`、`tick`、来源校验、轴心会话及 Apply 通知语句，并从 `native/main.cpp` 提取实际 `gamepadSerialPostOwnedEvent` 和 `WM_GAMEPAD_SERIAL_EVENT` 消费分支。

受控原始数据在工作线程产生，经过真正的 Windows `PostMessageW` 和消息窗口，在窗口所属线程接收。接收器模拟 WebView2 的线程限制；不正确线程的投递明确丢弃并计数。原来直接调用 `ipc_emit` 的反例必须复现收不到帧和校准结果；修复后检查连续轴值、前台门控、旧页面 token、停止订阅、轴心结果和保存通知。

上下文只提供内存来源和快照，不读取控制器、不提交虚拟手柄、不修改设置、不安装程序。输出包含原始失败、修复结果、实际源函数和 SHA256。该回归验证线程投递合同，不能代替物理手柄或 WebView2 实机验收。

编辑器数据回归：

```powershell
node tools/controller_axis_calibration_editor_selftest.mjs --out "D:/验证输出/校对编辑器"
```

直接执行正式 `CalibrationEditor` 和数据合同，核对失败回执换设备、同来源失败保留草稿、修订冲突、零偏移采样、左右轴心结果、无效结果拒绝以及全部配置字段的显式 Apply 读回。设备来源与提交端口为受控内存上下文；不执行原生设备读写。
