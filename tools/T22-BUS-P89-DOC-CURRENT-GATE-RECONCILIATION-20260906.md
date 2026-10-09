# T22-BUS-P89：TASK 当前 gate 与历史 RF00 叙述对账

日期：2026-09-06
状态：`DOC-CURRENT-GATE-CORRECTED / NO-SOURCE-CHANGE / RUNTIME-BLOCKED`

本轮只审计 TASK.md 的当前摘要段与历史 BUS 段之间的措辞边界。发现 E5/F 当前汇总仍保留旧的 17-key/66-key、REFREEZE-REQUIRED 和 CURRENT-SOURCE-REAUDIT-STALE 语言，而当前 RF00 manifest/re-audit 已是 74-key static snapshot；对应旧段落已明确标注为历史，但当前摘要必须改成最新静态状态。

## 1. 已修正的 current-summary drift

```text
E5 current status              = STATIC-SNAPSHOT-CURRENT / RF00-RECORDED / RUNTIME-BLOCKED
RF00 key-set wording           = manifest-declared set; latest observed count=74
T14 current status             = CURRENT-SOURCE-REAUDITED / STATIC-ONLY / RUNTIME-BLOCKED
T15/T16 current source basis   = static provenance current; empirical gates still runtime-blocked
T17/T18 current source basis   = static checks current; descriptor/consumer/recovery empirical gates open
T21 current binding            = latest 74-key RF00 manifest + T21/T14 re-audit report
```

历史 2026-09-04/2026-09-05 段落中的 17-key、66-key、旧 manifest 和 stale 状态仍保留为当时事实，不再作为 current-source authority。AY 历史回执明确说明旧 66-key 路径后来被最新 74-key RF00 覆盖。

## 2. 三方/runtime 结论没有改变

文档修正不改变锁定 HC、当前 YMCC 或 HIDMaestro 的电量结论：没有发现 HC DualShock4Target、YMCC InputHost 或 HIDMaestro encoder 的可归属 battery writer，也没有发现 0→5 隐式转换；PS4 低电量 5% 仍是 `UNKNOWN / RUNTIME-BLOCKED`。ROG matrix、DS4 descriptor/raw report、Steam/Windows readback、P-HID/P-XINPUT/P-OWNER、sleep/PnP/crash recovery 仍需真实运行时证据。

```text
program source changed = false
system/device changed  = false
runtimeUpgrade        = false
new HC source fix      = none
```
