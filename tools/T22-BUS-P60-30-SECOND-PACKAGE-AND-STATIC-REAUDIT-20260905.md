# T22-BUS-P60：30 秒 ROG 测试包与 RF00/T20/T21/T14 回归（2026-09-05）

## 范围

本轮只处理两项：

1. 将 ROG 陀螺仪证据采集默认窗口从 60 秒缩短为 30 秒；
2. 对当前源码、锁定 HC 资产和测试包重新执行只读 RF00 → T20 → T21 → T14。

本轮没有修改 HC 参数、native motion 公式、InputHost 协议、HidHide/ROG 路由或正式 Release 包；没有启动真实 Host、虚拟设备、HidHide、Steam、游戏或硬件操作。

## Fact：新测试包已生成并通过自校验

| 项目 | 值 |
|---|---|
| 输出目录 | `G:/YeManCC-Work/Mainline/Build/TestPackages/GyroInput-ROG-20260905-235837` |
| ZIP | `YeManCC-GyroInput-ROG-Test.zip` |
| ZIP bytes / SHA-256 | `85,227,270` / `3105D3A0C837913D3DB62F17109C82CF3D1B2EC7E924B0AC6785BAB94F1657A4` |
| YeManCC.exe | `2,150,400` bytes / `B8D1971531B638D00CAA3128A38B6ECBE159816A657C4AB603793409FF6B8F6A` |
| YeManInputHost.exe | `162,304` bytes / `1B494435DA89085B7EA8CA6C8EE60CB4FD714826C92E54658EBFC61CBEC0CBD0` |
| HidHide installer | `8,078,016` bytes / `F4BBBCB82E6258641B887C74BC81C4C5F66E4AA811808DFC304347687B7605F6` |
| ZIP entries | `360` |
| HC | `0.32.4.0 / 06c0b9544db2b1f39abf9cd3796225ccfb096103 / 91 files / mismatch 0` |

包内确认存在完整 YMCC、InputHost、`gyro-motion`、`virtual-gamepad`、锁定 HC/HIDMaestro/WinRT 依赖、HidHide 安装包、采集器和 `test-package-manifest.json`。测试包的 manifest 固化 `defaultDurationSeconds=30` 与 `5 秒静止 + 20 秒缓慢转动 + 5 秒静止` 的操作窗口。

采集器仍是只读证据工具：不调用 HidHide，不发送 OEM Disable，不修改 sensor `ReportInterval`，不创建虚拟控制器，不修改 Steam/游戏；默认会启动包内 YeManCC 以收集已有 JSONL/lifecycle 日志。正式 Release/updater 包未被测试包脚本写入。

## Fact：清除日志包与闪退结论

用户提供的清除后 `C:/Users/DaVe/Desktop/陀螺仪/YeManCC.zip` 已在 BUS-P59 审计；本轮没有把它重新解释为闪退证据。该包成功完成 WebView2 navigation/render-ready，没有 `browser-process-failed`、`gpu-process-exited`、`recovery-exhausted`、`window-destroy`、`exit` 或 dump/WER 文件。因此“cmd 启动闪退”仍未被当前证据复现。

历史 ROG 日志的 `browser-process-failed / -1073741819 (0xC0000005)` 与源码第三次恢复后 `beginAsyncExit` 仍是已知风险路径，但不是本轮清除包的事实。若再次发生，必须回传同一时刻的 `native-lifecycle.log`、`webview-failures.log`、`recovery-service.log`，并尽量保留 Crashpad/WER。

## Fact：RF00 → T20 → T21 → T14

新的 RF00 工件：

- Manifest：`T22-RF00-T10-I-CURRENT-SOURCE-20260905-manifest.json`
- Manifest ID：`T22-RF00-20260905-T10-I-HC-ROUTE-STATIC-SNAPSHOT`
- Capture UTC：`2026-09-05T16:10:59.7071089Z`
- 71 key files；`sourceDigest=F43B59BD4DFBD9B323B56AF28E59714FA1A30135946A09BAB8EA219A6DBAFBF8`
- YMCC `HEAD=aa8f38b83cc960267d2f68bf78d0e8adaca2234f`；porcelain count `445`
- HC `HEAD=06c0b9544db2b1f39abf9cd3796225ccfb096103`；worktree clean
- RF00 操作类型为 static read/hash/search/documentation only，`runtimeOperation=false`、`buildOrTest=false`

T20/T21/T14 回归报告为 `T21-T14-RF00-T10-I-CURRENT-SOURCE-reaudit-20260905.md`：

- T20 仍为 21 条 claim，`4/13/4` disposition；TASK BUS 是唯一 authority；
- T21 仍为 9 条 REC，均绑定同一 manifest/digest，`runtimeUpgrade=false`；
- T14 仍只登记 DGF-01…DGF-15；C1 option 2 已解决，不创建 DGF-16；
- T10 仍是 source-implemented / Host-local receipt only，不提升为 runtime closure。

## Fact：静态回归

本轮通过：

- `gyro_motion_mapper_mock_selftest`
- `gyro_config_activation_selftest`
- `gyro_calibration_session_selftest`
- `gyro_e37_hc_parity_selftest`
- `gyro_virtual_feature_selftest`
- `rog_gyro_protocol_selftest`
- `rog_input_adapter_selftest`
- `rog_visibility_topology_selftest`
- `rog_hidhide_contract_static_selftest`
- `input_lifecycle_mock_selftest`
- `input_owner_runtime_integration_selftest`
- `hc_parity_drift_ledger_selftest`
- `gyro_virtual_feature_sidecar_audit`
- `vue-tsc --noEmit`
- `hc_parity_ledger_audit`（15 entries）

`gyro_virtual_mainline_status_audit` 仍为 `RUNTIME_BLOCKED`，与任务书一致。

## Error / unresolved：正式 Release 边界冲突仍存在

`gyro_virtual_release_boundary_audit.ps1` 仍返回 `FAIL`，唯一违规条目是：

```text
PowerControl/redist/HidHide_1.5.230_x64.exe
```

当前 `tools/package-release.ps1` 仍同时存在“正式升级包不得发布/安装 HidHide”的文档边界与“正式 ZIP 复制/包含 HidHide”的实现路径。该问题是发布策略冲突，不是 HC 参数或本轮测试包错误；在用户裁决前不删除、保留为已批准或重新生成正式 Release。

## Unknown / runtime gate

以下缺口没有因新测试包或静态回归而关闭：

- ROG 同 provider、同 calibration epoch/generation、matrix identity 的 `SamplePairProof`；
- 真实 GamepadMotion/DS4 descriptor、encoder/readback、InputHost ACK/first-frame/release；
- P-HID、P-XINPUT、P-OWNER 三平面和外部 Steam/游戏 consumer 的隔离/恢复 receipt；
- summon → suppress → neutral → YMCC owner，以及关闭、失焦、睡眠、断开、Host 崩溃后的 release/rearm；
- 采集器 WinRT callback 与 polling fallback 是否在同一 sensor owner/interval 上，不能把 `10 ms` 与独立 `100 ms` 读数当作已证实偏移；
- “cmd 闪退”发生的包类型、退出码和同刻 Crashpad/WER。

因此 T11–T13、T15–T18 仍保持 `UNENCLOSED / RUNTIME-BLOCKED`；本轮没有发现“明确不是 HC 且属于新增偏移代码”的可直接修复项。

## 介入点

1. 在 ROG 上运行新包并回传 30 秒证据 ZIP，才能继续 runtime 闭口；
2. 若再次闪退，回传同刻三类日志及 Crashpad/WER；
3. 裁决正式 Release 是否允许 `HidHide_1.5.230_x64.exe`，以便关闭 A1 发布边界冲突。
