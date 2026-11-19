# 灯光总库接入与控件核查（2026-11-01）

2026-11-09 补证：当前主线所选程序集的返回值限制已核实，本文原控件修复和原测试结论保持其当时范围。主线 `ymccLightSetterApply` 对 Lenovo 选择现有 HC 路线和 `ymcc-device-core-v1` profile；`LightSetter` 编译 pin 与 runtime manifest 均指向 `HandheldCompanion.dll` SHA256 `4cd3dea283e26db52e10f36e3de74b2514972d6b5e4e99e1ef952a0a277aa13c`（2,774,016 bytes，MVID `80cb2df3-7340-9bd5-a675-3df4f6dc631d`）。独立 PE/RVA 字节核对绑定六个灯光方法体，没有加载或执行目标程序集。

| 总库精确机型 | 所选方法实现 | 写入结果与无设备分支 |
| --- | --- | --- |
| `LegionGoTablet` / `LegionGoTablet2` | Tablet 的 `SetLedColor`、`SetLedBrightness`、`SetLedStatus`；Tablet2 无这三项覆盖 | `HidDevice.Write(byte[])` 的 bool 被 `pop` 丢弃；正常路径和无 HID 分支返回 true。抛出异常仍是独立失败路径。 |
| `LegionGoSZ1` / `LegionGoSZ2` | SZ1 的同名三方法；SZ2 无这三项覆盖 | 直接返回 `Write` 的 bool，无 HID 返回 false。该软件回执仍不证明可见灯效。 |

固定 f2e58468 源码的 Tablet 实现丢弃的是 `WriteReport(bool)`，不能把它当作当前所选 DLL 的同一方法体；当前结论已独立落到上述 DLL。现有 LightSetter 会把这些 HC 方法返回的 true 转成 `ok`，因此 GO/GO2 的 ok 只能证明该层调用正常返回，不能证明 HID 写入、关灯、物理效果或 OEM 归还。现有字段日志尚未证明某次 false 写入被报成成功，也未建立安装版/内存加载身份；这两项保持 UNKNOWN。

总库不新增无消费者参数、不隐藏现有能力、不改固定 HC 或新增 DLL。消费端应沿既有 LightSetter 路线明确处理“写入结果不可观察”，保留实际异常与失败；若要声称写入成功，须提交现有消费者的源码与选中组件身份证据，以及无 HID、底层 false、正常返回和关灯的原最小正反验证。不能用调用前存在性检查或助手退出码代替当次写回执，也不新增第二个 writer 或借通用 HC Close 归还其它域。具体修正与六个 IL/RVA 证据见 `G:/YeManCC-Work/Docs/Tasks/HC/Capability-Library-TraeCode-CN/Codex/observations/HC-CODEX-107-LIGHTING-SELECTED-IL-REVIEW`。本次库侧未运行产品/夹具、构建、导出或硬件操作；以下为原历史。

本轮入口为微星 Claw A1M 一代“氛围下右摇杆环停留在一个颜色”的照片与反馈。静态照片不证明设备写入失败；本轮在源码与真实组件回放中确认了四项具体问题，修复沿用能力总库和现有消费者。

## 已确认与修复

1. **氛围右色被隐藏，覆盖 MSI／ROG 共 8 条路线。** 常规 `DynamicLightingSecondLEDColor` 位与分区颜色是两种条件。8 条路线都没有这个常规位，原生氛围 writer 却都消费主色与副色：微星区域 0–3 接副色，4–8 接主色；ROG 区域 1–2 接主色，3–4 接副色。旧 UI 因而只显示主色，右侧继续使用保存值或默认紫色 `#9d7cff`。改为在总库已授予 `ambilight` 时显示“左侧颜色／右侧颜色”，常规效果仍按原能力位判断。旧组件在相同用例中明确失败：应显示 2 个颜色控件，实际仅 1 个。
2. **OneXPlayer 3 暴露了无效预设 8。** 该审查扩展只有 10 个预设，原能力回执却复制旧 OneX 的 11 个。现在展示前调用现有精确机型 `validPreset` 过滤，保持稳定预设 ID。旧原生能力函数已在内存端点复现 11／10 不一致。
3. **OneXPlayer 3 固定速度事实没有传到 UI。** 既有总库 `fixedSpeed` 经生成的 Route 和原能力回执送到界面；速度禁用并解释“本机灯效速度由固件固定”。没有增加速度写入。
4. **OneXPlayer 3 预设的副色区域不可调。** 真实 writer 在预设下仍使用副色更新 G-Key／Logo。审查信息写回既有 `hc-lighting-policy.json` 的 `presetSecondColor`，由原生成器生成，再经能力回执开放“标志与功能键颜色”；其他机型的预设不借用此能力。

当前 YMCC 的“氛围”消费者是**左右固定分区配色**，没有持续屏幕取色或随时间变色。界面已明确说明这一点，并显示为“氛围（分区配色）”。本轮没有把静态配色冒称实时 Ambilight，也没有引入取屏循环。

## 全部已接入灯光路线

来源为 `hc-device-catalog.json` 的 lighting 域：47 条冻结路线（含 3 个基础别名）＋1 条已审查扩展，共 48 条；43 条亮度路线、8 条氛围路线、6 条可执行预设路线，合计 100 个有效“路线／效果”组合。未授予的效果、无传输的 Mini 预设、无实际亮度的 Loki 均保留原限制。

全部路线覆盖 ROG Aura、MSI 原生 HID、现有 LightSetter／HC 兼容路线，以及已认可的 Mini 独立核心；不新增 HC DLL。基础机型识别、所有权、输入、风扇、陀螺仪与实际 HID 协议不因本轮控件问题改变。

## 验证与边界

- 原注册 ID：`lighting.unified-capability-dispatch`、`lighting.signal-consumer`、`lighting.ui-feedback-ordering-consumer`。不新增测试父门或 ID，不扩大原预算。
- 原生校验执行生产能力回执、实际分发、ROG／MSI 报文函数；氛围覆盖全部 8 条路线、连续独立改左右色、MSI 两个固件地址分支及各色区／配置帧，保留关灯、短写与失败回执断言。
- 真实 Vue／Dropdown／Slider、正式 API 包装在 1280×720 和 1280×800 回放全部 100 个有效组合。氛围还执行正式导航函数及 focus／spatial／performanceNavigation，确认两色可达可返回、A 打开、B 取消返回且不额外写灯。
- 保留原 22 项生命周期正例、移除退出钩子的 18 项预期失败反例，以及原乱序回执正反例。前端类型检查及完整前端资源构建通过。
- 测试环境曾因已移除的 Edge 155 和 Node 工具迁移被阻塞；保留原记录，使用完整当前 Edge 工具清单和物理路径复核。严格观察器发现原闭包漏列 `mouse-classic.png` 与 `sliderRepeat.ts`，已补真实依赖，未放宽观察器。部分原失败保留供审计，不能把它们写成通过。

**以上是软件消费者与内存端点验证。** 没有启动或部署安装程序、没有操作真实灯光。真实固件回读、OEM 前端争用、休眠后效果及右灯实物表现仍为 `UNKNOWN`，不是写入成功即可验收。

证据根：`G:/YeManCC-Work/Testing/YMCC/Evidence/Lgt1101`。最终状态和源码身份见该目录 `completion-audit.json`；所有历史 FAIL／INCOMPLETE 原件保留。下次实测请分别调整左右颜色，再测常亮→分区→关灯；若仍有一侧不变，使用正常 YMCC 导出日志比对 `gamepad-setLed` 的两色请求与 `msi-claw-led` 的固件地址、帧数和写错误，而不是直接改协议。

当前共享主线的 `main.cpp` 在灯光测试后被并行任务修改了壁纸配置校验／读取。原完整文件身份因而不能直接复用。`current-consumer-identity.json` 固定了与已测回执完全一致的源码快照，并逐字节证明：排除明确的壁纸配置区域后，其余整份文件（包括全部灯光消费者）完全一致。本轮据此验证当前灯光消费者，不给旧完整文件回执重新签名，也不替壁纸任务或整份主线验收。

## 路线清单（总库快照，非新的运行分配表）

| 路线 | 消费者 | 亮度 | 常规副色 | 有效效果 | 可用预设数 |
|---|---|---|---|---|---|
| AOKZOEA1 | hc-light-setter | 有 | 无 | solid, rainbow | 0 |
| AOKZOEA1Pro | hc-light-setter | 有 | 无 | solid, rainbow | 0 |
| AOKZOEA1X | hc-light-setter | 有 | 无 | solid, rainbow | 0 |
| AOKZOEA2 | hc-light-setter | 有 | 无 | solid, rainbow | 0 |
| AYANEO2 | hc-light-setter | 有 | 有 | solid | 0 |
| AYANEO2S | hc-light-setter | 有 | 有 | solid | 0 |
| AYANEOAIR | hc-light-setter | 有 | 有 | solid | 0 |
| AYANEOAIR1S | hc-light-setter | 有 | 有 | solid | 0 |
| AYANEOAIRLite | hc-light-setter | 有 | 有 | solid | 0 |
| AYANEOAIRPlusAMD | hc-light-setter | 有 | 有 | solid | 0 |
| AYANEOAIRPlusAMDMendocino | hc-light-setter | 有 | 有 | solid | 0 |
| AYANEOAIRPlusIntel | hc-light-setter | 有 | 有 | solid | 0 |
| AYANEOAIRPro | hc-light-setter | 有 | 有 | solid | 0 |
| AYANEOFlip1SKB | hc-light-setter | 有 | 有 | solid | 0 |
| AYANEOFlipKB | hc-light-setter | 有 | 有 | solid | 0 |
| AYANEOKUN | hc-light-setter | 有 | 有 | solid | 0 |
| AYANEOSlide | hc-light-setter | 有 | 有 | solid | 0 |
| AynLoki | hc-light-setter | 无 | 无 | solid | 0 |
| ClawA1M | msi-claw-native-hid | 有 | 无 | solid, ambilight | 0 |
| ClawA2VM | msi-claw-native-hid | 有 | 无 | solid, ambilight | 0 |
| ClawBZ2EM | msi-claw-native-hid | 有 | 无 | solid, ambilight | 0 |
| ClawCG3EM | msi-claw-native-hid | 有 | 无 | solid, ambilight | 0 |
| GamingZone | hc-light-setter | 有 | 有 | solid, breathing, rainbow, gradient | 0 |
| LegionGoSZ1 | hc-light-setter | 有 | 无 | solid, breathing, rainbow, wheel | 0 |
| LegionGoSZ2 | hc-light-setter | 有 | 无 | solid, breathing, rainbow, wheel | 0 |
| LegionGoTablet | hc-light-setter | 有 | 无 | solid, breathing, rainbow, wheel | 0 |
| LegionGoTablet2 | hc-light-setter | 有 | 无 | solid, breathing, rainbow, wheel | 0 |
| LokiMax6600U | hc-light-setter | 无 | 无 | solid | 0 |
| LokiMax6800U | hc-light-setter | 无 | 无 | solid | 0 |
| LokiMiniPro | hc-light-setter | 无 | 无 | solid | 0 |
| LokiZero | hc-light-setter | 无 | 无 | solid | 0 |
| OneXPlayerApex | hc-light-setter | 有 | 有 | solid, preset | 11 |
| OneXPlayerG1 | hc-light-setter | 有 | 无 | solid, rainbow | 0 |
| OneXPlayerG1AMD | hc-light-setter | 有 | 无 | solid, rainbow | 0 |
| OneXPlayerG1Intel | hc-light-setter | 有 | 无 | solid, rainbow | 0 |
| OneXPlayerOneXFly | hc-light-setter | 有 | 无 | solid, rainbow | 0 |
| OneXPlayerOneXFlyF1Pro | hc-light-setter | 有 | 无 | solid, rainbow | 0 |
| OneXPlayerX1 | hc-light-setter | 有 | 有 | solid, preset | 11 |
| OneXPlayerX1AMD | hc-light-setter | 有 | 有 | solid, preset | 11 |
| OneXPlayerX1Intel | hc-light-setter | 有 | 有 | solid, preset | 11 |
| OneXPlayerX1Mini | hc-light-setter / approved independent Mini core | 有 | 有 | solid, rainbow | 0 |
| OneXPlayerX1Pro | hc-light-setter | 有 | 有 | solid, preset | 11 |
| ROGAlly | rog-aura | 有 | 无 | solid, breathing, rainbow, wheel, ambilight | 0 |
| ROGAllyX | rog-aura | 有 | 无 | solid, breathing, rainbow, wheel, ambilight | 0 |
| SuiPlay0X1 | hc-light-setter | 有 | 有 | solid | 0 |
| XboxROGAlly | rog-aura | 有 | 无 | solid, breathing, rainbow, wheel, ambilight | 0 |
| XboxROGAllyX | rog-aura | 有 | 无 | solid, breathing, rainbow, wheel, ambilight | 0 |
| OneXPlayer3 | hc-light-setter | 有 | 有 | solid, breathing, preset | 10 |
