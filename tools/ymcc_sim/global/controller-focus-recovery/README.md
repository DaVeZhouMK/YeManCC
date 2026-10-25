# 聚焦屏蔽与故障自愈联合消费者

正式入口：`controller.focus-recovery`。直接调用现有 `tools/input_coordinator_lifecycle_selftest.mjs --out ABS_NEW_ROOT --managed-dll SDK_FREE_DLL --suite focus-recovery`。正常 `--suite all` 也必须执行该模式。没有第二套生命周期或复制的焦点决策模型。

用例位于 `tools/fixtures/input_coordinator_lifecycle/focus_recovery_cases.inc`，由真实 `inputCaptureOnGamepad` 计算 UI 中性，进入正式提交/序列化路径。专属私有管道在实际 SUBMIT_FRAME 写入后断开以制造传输故障；观察真实原生读失败和同一 R3 请求中的归还/重建。覆盖持续聚焦及恢复排队后进入聚焦两个时点，检查新 Host 首帧和连续帧、中性 ACK 健康计分、一次保底与原截止，以及释放 owner 后当前非零帧恢复。

环境 SHA 权威复用既有 `controller-cold-msi-archive/adapter.mjs` 的 SDK-free managed manifest/toolchain 绑定；这里只复用验证函数和文件清单，不执行该模块的 MSI 行为测试。输入与输出逐文件纳入 global 源/载荷漂移检查。当前软件结果以本次 global RESULT 为准，注册存在不表示测试通过。

物理源采用现有受控 XInput 叶，UI owner 是缓存径向菜单位。此场景不证明真实窗体焦点、MSI/其他厂商实体数据、物理 IMU/触摸、HIDMaestro 真实后端或 Steam/游戏消费。未知/失败及历史红结果保留，不放宽原断言。
