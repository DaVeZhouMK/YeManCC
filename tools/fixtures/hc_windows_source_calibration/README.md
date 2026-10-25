# Windows 来源校准事务夹具

入口：`node tools/hc_reviewed_calibration_transaction_selftest.mjs --all-windows-models`。

覆盖 AYANEO、OneXPlayer、AOKZOE、Ayn、Zotac、Minisforum、SuiPlay 的 51 个规范类：既有 18 类键保持兼容，另 33 类使用来源身份键。

脚本提取当前 main.cpp 的真实校准函数和 BUS 调用块，使用已锁定的 GamepadMotion.dll、真实文件 API，以及受控元数据和时钟。手动／自动调度、切源、残缺来源、保存失败、回滚、读文件期间切源、终结前换主均在实际函数中执行。两个加载／终结插桩只安排确定性的交错时点；被包装的原函数体仍执行。置信度是受控值，不代表实际静置收敛。

故障对照：在上述入口追加 `--source-guard-removed` 或 `--save-receipt-spoofed`。必须产生真实执行断言失败；编译失败不能接受为故障对照。普通 18 类入口不带 `--all-windows-models`，使用既有无交错钩子的夹具。

所有文件写到 Build/Validation 的隔离目录；不读取用户真实校准文件，不启动普通 App/Host，不操作设备，不新增生产 HC DLL。最新证据入口为 WindowsSourceCalibration/VERIFICATION.json；来源轴向、单位、连续采样、实际输出与睡眠均需另行实机验收。
