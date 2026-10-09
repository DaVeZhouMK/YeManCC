# T22-BUS-P57：静态回归与 Release 边界审计（2026-09-05）

## 范围

本页汇总 P54/P55/P56 后的只读静态回归。未启动 YeManCC/InputHost/HIDMaestro/HidHide、Steam/游戏、虚拟设备或真实硬件；不升级任何 runtime 结论。

## 通过的静态证据（Fact）

以下命令均以退出码 0 完成：

- `vue-tsc --noEmit`
- `rog_gyro_protocol_selftest.ts`
- `rog_input_adapter_selftest.ts`
- `rog_hidhide_contract_static_selftest.ts`（修正 `void|bool inputHostStart` 边界后）
- `gyro_motion_mapper_mock_selftest.ts`
- `gyro_config_activation_selftest.ts`
- `gyro_calibration_session_selftest.ts`
- `gyro_e37_hc_parity_selftest.ts`
- `gyro_virtual_feature_selftest.ts`
- `gyro_virtual_feature_sidecar_audit.ps1`
- `input_contracts_selftest.ts`
- `input_coordinator_mock_selftest.ts`
- `input_lifecycle_mock_selftest.ts`
- `input_owner_runtime_selftest.ts`
- `input_owner_runtime_integration_selftest.ts`
- `input_owner_resident_selftest.ps1`
- `physical_input_ownership_selftest.ts`
- `input_power_pnp_selftest.ts`
- `input_host_supervisor_selftest.ts`
- `input_route_evidence_selftest.ts`
- `input_t8_host_loop_selftest.ts`
- `input_host_ds4_imu_static_selftest.ts`
- `persona_descriptor_parity_selftest.ts`
- `rog_visibility_topology_selftest.ts`
- `rog_xbox_face_lifecycle_selftest.ps1`
- `hc_parity_drift_ledger_selftest.ts`
- `hc_parity_ledger_audit.ps1`
- `gyro_virtual_mainline_status_audit.ps1`

`test:t8-host-loop` 与 `test:input-host-ds4-imu-static` 的首次命令名不存在只是 package script 名称误调用；实际脚本 `test:input-t8-host-loop` 与直接 esbuild/node 运行均通过，不是产品失败。

## 明确失败/矛盾（Error / Conflict）

`gyro_virtual_release_boundary_audit.ps1` 仍为 `FAIL`，唯一正式包违规条目是：

```text
PowerControl/redist/HidHide_1.5.230_x64.exe
```

当前 `tools/package-release.ps1` 同时存在“主线升级包不得发布/安装 HidHide”的注释和“复制并强制校验该安装包”的实现/校验（约 358–370、572–573 行）。这不是 HC motion 参数问题，而是 Formal Release policy 与脚本实现的冲突；在用户裁决前不删除、不保留为已批准，也不重新发布正式 ZIP。

## HC 偏差与原创逻辑裁决

- 本轮没有新的轴交换、符号、单位、阈值、velocity decay、gyroWeight、SteeringAxis 或 lifecycle 顺序的无歧义偏移。
- P56 的 chord/debounce、ADS 参数消费、producer-plane ownership、`motionPairProofId` 绑定均为缺口/未知，不是允许猜测补齐的数值偏移。
- ROG 的 `pairProven=false`、safe-zero、DS4 direct IMU no-report 与 PID-only matrix fail-close 继续视为安全门，不得为通过静态回归而放开。

## 当前状态与介入点

```text
T10 = SOURCE-IMPLEMENTED / HOST-LOCAL-RECEIPT-ONLY
T11 = pair-unproven-safe-zero / matrix identity unbound
T12 = direct-IMU-no-report / descriptor-readback-unclosed
T13 = P-HID/P-XINPUT/P-OWNER-UNENCLOSED
T15/T16/T17/T18 = RUNTIME-BLOCKED
runtimeUpgrade = false
```

当前还需要真实 ROG 测试包回传来闭合：same-provider/generation/calibration/matrix proof、连续静止—运动—静止样本、InputHost ACK/first-frame/release、DS4 descriptor/readback、owner/consumer 计数、召回/关闭/失焦/睡眠/断开/Host crash 恢复。现有附件 `.prev` 仍是历史滚动文件，不能替代新包证据。

