# T22 BUS-P92：PS4 5% 电量显示的公开 HID 源码交叉核对（2026-09-06）

状态：`SOURCE-CROSSCHECK-COMPLETE / PRODUCER-UNPROVEN / RUNTIME-BLOCKED`

## 1. 交叉核对的公开源码事实

公开 Linux `hid-playstation.c` 的 DualShock 4 解析路径对 `status[0]` 的低四位执行。核对源文件为 Linux stable tree 的 `drivers/hid/hid-playstation.c`：

```text
https://kernel.googlesource.com/pub/scm/linux/kernel/git/stable/linux-stable/+/master/drivers/hid/hid-playstation.c
```

相关解析逻辑：

```c
battery_data = status[0] & 0x0f;
if (battery_data == 0x0f)
    capacity = 100;
else
    capacity = battery_data * 10 + 5;
```

因此这是源码直接证明的映射：

```text
DS4 status battery code 0  -> 5%
DS4 status battery code 1  -> 15%
...
DS4 status battery code 9  -> 95%
DS4 status battery code 15 -> 100% / full sentinel
```

这不是“把真实电池测得 5%”，而是 DS4 低电量桶 `0..9` 的中点表示。对应的充电状态还由同一个 status 字节的充电位解释。公开源码位置：

```text
kernel.googlesource.com/.../drivers/hid/hid-playstation.c
  dualshock4_parse_report(): status[0] & 0x0f
  battery_data * 10 + 5
```

该公开实现只证明标准 DS4 consumer/parser 的编码语义；不能证明 Steam、Windows GameInput 或本机 HIDMaestro 路径必然调用了同一函数。

## 2. 与锁定 YMCC/HIDMaestro source 的对账

当前锁定本地证据已证明：

```text
HMGamepadState.BatteryLevel default = 0
current InputHost SubmitState       = Buttons / Hat / Axes only
BatteryLevel/BatteryCharging/Full assignment = NOT FOUND
dualshock-4-v2 profile battery metadata = byte 14 / uint8-battery
HIDMaestro controlled encoder       = raw-value identity
  0 -> 0x00, 5 -> 0x05, 10 -> 0x0A, 100 -> 0x64, 255 -> 0xFF
```

对应本地 source/静态证据：

- `InputHost/Program.cs:461-479, 490-506` 只构造 buttons/hat/axes。
- `T22-BUS-P71-HIDMAESTRO-DS4-BATTERY-ROUTE-STATIC-PROBE-20260905.md` 记录默认值、`AlwaysArmed=false`、`ArmOn=null`、encoder 原值写入。
- `T22-BUS-P24-HC-HIDMAESTRO-YMCC-THREE-WAY-STATIC-AUDIT-20260905.md` 记录当前 USB `dualshock-4-v2` 的 extended route 静态不可达，`SubmitState` 回退到 legacy axes/hat/buttons 路径。

由此只能得到严格的 source-level 结论：

```text
当前产品没有显式 battery writer；
若某条最终 HID/consumer 路径看到一个未写入或零值的 DS4 电量字段，
标准 DS4 解析语义可以把它显示成 5%；
但当前快照没有证明该零值已进入最终 HID report，也没有证明 Steam 使用了该标准解析器。
```

## 3. HC 对照

锁定 HC `DualShock4Target.BuildReport()` 只写 VIIPER 31-byte report 的摇杆、按钮、触摸和 raw gyro/accel；没有发现 battery percentage、charging 或 full assignment。HC 的 `054C:05C4`/31-byte wire contract 不能直接替换当前 HIDMaestro `054C:09CC`/64-byte profile。

因此不能用“给 YMCC 写 `BatteryLevel=5`”作为 HC 对齐修复；那会是没有 HC source 依据的原创行为。

## 4. 事实、解释与未知分离

### FACT

- 公开 DS4 parser 的 battery code `0` 映射到 `5%`。
- 当前 YMCC `InputHost` 没有 battery writer。
- 锁定 HIDMaestro 默认 `BatteryLevel` 为 `0`，encoder 对输入值不做 `0→5` 转换。
- HC DS4 target 没有 battery assignment。

### 受证据支持的解释（不是 runtime closure）

“总是弹出 5%”与“虚拟 DS4 报告/描述符某个 battery 字段保持零或无效，随后由 DS4 consumer 采用 `0→5%` 桶语义”相容；这是目前最强的源码解释。

### UNKNOWN / RUNTIME-BLOCKED

- 本机最终 HID input/feature report 的原始 byte、report ID/length、descriptor battery usage。
- byte 14 是否实际被当作 battery，还是只属于 HIDMaestro 自定义 extended layout。
- Steam/Windows/GameInput 实际读取的 report、consumer/parser 和显示回执。
- 产生 popup 的确切进程/route；不能把 7 个 `YeManInputHost.exe` 直接认定为 producer。

## 5. 关闭该缺口所需的最小证据

在同一个 Host `runId/epoch/target identity` 下采集：

```text
1. HID descriptor / preparsed battery usage 与 report ID/length
2. input + feature raw report（至少同时记录 byte 14、标准 DS4 status 候选 byte 30/31）
3. 独立 decoder：按 descriptor 解释 byte 14，再按标准 DS4 规则解释 status[0]
4. Steam/Windows/GameInput 同设备 readback 与 5% popup 时间戳
5. Host SubmitState/target instance/owner/epoch 对齐
```

在这些证据之前固定保持：

```text
PS4 battery 5% producer = UNKNOWN / RUNTIME-BLOCKED
runtimeUpgrade         = false
不得写 BatteryLevel=5、0x05、0x0A、100 或 Full
不得切换 DS4 v1/v2 或把 HC 054C:05C4 直接替换为 054C:09CC contract
```
