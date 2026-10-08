# T22-BUS-P59：清除后 YeManCC 日志包闪退审计（2026-09-05）

## 证据身份

| 项目 | 值 |
|---|---|
| 原始 ZIP | `C:/Users/DaVe/Desktop/陀螺仪/YeManCC.zip` |
| bytes / SHA-256 | `147,060` / `BB22751194710E73F8FE0D17D5D70BDDB0A4AB9508D1337B1BBA008C1B3B0DAE` |
| 内容 | `native-lifecycle.log`、`recovery-service.log`、`webview-failures.log`、`input-capture.jsonl`、清除后的 `EBWebView` profile |
| dump / WER | 未发现 `.dmp`、`.mdmp` 或 `.wer` |

## 已证实事实（Fact）

这次清除后启动于 `2026-09-05 23:30:24`，日志完整出现：

```text
boot-single-instance-acquired
window-created
recovery-service-started
environment-init (gpuMode=default, runtimeVersion=152.0.4191.62)
browser-process-exited-handler (registered=true, hresult=0)
navigation-start
navigation-complete
render-ready-signal
render-ready-complete
post-show-nudge
```

本包没有出现 `browser-process-failed`、`gpu-process-exited`、`recovery-exhausted`、`window-destroy` 或 `exit`。因此这份清除后日志**没有复现闪退**，且证明 WebView2 在该次启动成功完成导航和前端 ready。

`input-capture.jsonl` 只记录一组 `sample-pair-unproven` 与一条 sample；gyro/accel 均存在，`pairProven=false`、`playerSpace/worldSpace=0`，与当前安全门一致，与闪退无直接因果证据。

## 已知风险路径（Source fact；不是本包复现）

历史 ROG 日志曾记录 WebView2 browser process exit code `-1073741819`（十六进制 `0xC0000005`，访问冲突）。当前 `native/main.cpp` 在 `recoverWebViewProcess()` 中：

```text
browser-process-failed
→ persist software GPU mode
→ profile isolation / controller recovery
→ attempt > 2 时 beginAsyncExit(...)
```

因此如果用户看到的“闪退”同时伴随 `browser-process-failed` 且达到第三次 recovery attempt，源码会主动结束 YeManCC；但新清除包没有该事件，不能把它归因到本次启动。

## 不能从本包确认的内容（Unknown）

- 没有用户所称闪退时刻的进程退出码、Windows Application Error/WER 事件或 Crashpad dump；
- 没有证明闪退发生在正式包、测试包还是仅为 `.cmd` 控制台窗口正常退出；
- 没有证明旧 `EBWebView` profile、GPU mode marker、WebView2 runtime 或当前工作目录是触发条件。

## 裁决

- 不修改 HC 陀螺仪参数，不把采集问题归因于闪退；
- 不因未复现包擅自改动 WebView2 recovery 退出策略；
- 采集器默认时长已从 60 秒下调到 30 秒，下一次仍保留 5 秒静止 + 20 秒运动 + 5 秒静止窗口；
- 若再次闪退，必须回传同一时刻的 `native-lifecycle.log`、`webview-failures.log`、`recovery-service.log`，并尽量保留 `EBWebView/Crashpad/reports` 与 Windows WER 记录，才能继续定位。

