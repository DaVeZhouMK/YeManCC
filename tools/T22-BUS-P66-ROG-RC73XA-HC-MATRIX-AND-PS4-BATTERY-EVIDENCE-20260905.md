# T22-BUS-P66: ROG RC73XA / HC matrix reconciliation and PS4 5 percent evidence boundary

Status: `EVIDENCE-RECONCILED / HC-SOURCE-BOUND / PRODUCT-MATRIX-RECEIPT-UNENCLOSED / PS4-BATTERY-UNKNOWN / RUNTIME-BLOCKED`

This BUS page reconciles the user-provided ROG evidence package, the locked HC `0.32.4.0 / 06c0b9544db2b1f39abf9cd3796225ccfb096103`, and the current YMCC source. It is read-only evidence work: no YMCC/InputHost/HIDMaestro/HidHide, Steam, game, virtual device, physical device, formal Release/updater, or system mutation was started.

## 1. Evidence object and integrity

```text
path   = C:\Users\DaVe\Desktop\YeManCC-ROG-IMU-Evidence-20260906-011852.zip
bytes  = 37,266
sha256 = 5E59FF7C9416F9926BFF67ED83B4BA86E4F96FDF40321EAF02835E942C5E8AAD
```

The ZIP manifest matches all four members:

```text
rog-imu-evidence.json             28,320  B9808276A704C3E960713E03CD5929C2860C0B2F8D20EB94B47BDB349BBF5416
ymcc-input-capture.jsonl         461,427  876409E90F85C791CCB44E5BB38E08785629035455DE00F623E835436FAE6B78
ymcc-native-lifecycle.log          4,927  0E286BEB1C9265951F026D9F1BAD26BF62F5419DFCBF9838302E62591FA05EE9
ymcc-YeManCC-input-capture.jsonl 434,235  CE83C19416A4DABA7B6AE22978BEB33210C651C873AC87BFB015AF68570A4A97
```

## 2. ROG DMI identity and HC matrix

### 2.1 Proven facts

The evidence package reports:

```text
baseboard manufacturer = ASUSTeK COMPUTER INC.
baseboard product      = RC73XA
computer model         = ROG Xbox Ally X RC73XA_RC73XA
bios                   = RC73XA.317
```

Locked HC `HandheldCompanion/Devices/IDevice.cs` selects `new XboxROGAllyX()` when `ManufacturerName=ASUSTEK COMPUTER INC.` and `ProductName=RC73XA`. This is the HC SMBIOS/baseboard branch, not a PID inference.

Locked HC `Devices/ASUS/XboxROGAllyX.cs` defines:

```text
gyro   Axis=( 1,  1, -1), Swap X=X,Y=Z,Z=Y  => (raw.x, raw.z, -raw.y)
accel  Axis=(-1, -1,  1), Swap X=X,Y=Z,Z=Y  => (-raw.x, -raw.z, raw.y)
```

For the first raw sample in this package:

```text
raw gyro  ( 3.967285, -1.342773, -0.061035)
HC gyro   ( 3.967285, -0.061035,  1.342773)
raw accel ( 0.719788, -0.369629, -0.575439)
HC accel  (-0.719788,  0.575439, -0.369629)
```

These are reproducible HC formula results only; they are not evidence that YMCC wrote the transformed values or that an external consumer read them back.

### 2.2 Current YMCC reconciliation

The same package still contains:

```text
sensorFamily   = windows-default-winrt
gyro/accel     = true/true
matrixIdentity = unresolved-rog-pid-family/raw
confidence     = 0
steady         = false
player/world   = (0,0)/(0,0)
sample-pair    = unproven / safeZero=true
```

The layered conclusion is:

```text
HC RC73XA -> XboxROGAllyX -> exact matrix       = EVIDENCE-CLOSED
YMCC DMI identity -> native matrix receipt      = UNENCLOSED
YMCC output/Host/external consumer readback     = RUNTIME-BLOCKED
```

Native currently holds only the ROG HID PID family (`0x1ABE/0x1B4C`) and deliberately keeps raw axes. The evidence package cannot be silently treated as a native identity receipt, and the Xbox ROG matrix must not be applied globally without that receipt. `pairProven=false` must remain safe-zero.

## 3. Raw IMU and timing evidence

Parsed `ymcc-input-capture.jsonl`:

```text
sample rows                         = 552
unique gyro/accel event pairs       = 126
gyro sequence                       = 1 -> 732
accel sequence                      = 1 -> 648
receipt skew                        = -16 ms .. 93 ms (average 4.198 ms)
FILETIME timestamp skew             = -15.3683 ms .. 93.2665 ms (average 4.1623 ms)
logged event cadence                = 78 ms .. 5,781 ms (average 167 ms)
confidence/steady                   = 0 / false for all rows
player/world non-zero rows          = 0 / 0
```

This supports the following fact/gap split:

1. YMCC received real raw gyro/accel values from the ROG Windows sensors; the prior capture JSON-shape regression was not observed in this package.
2. Repeated rows and a maximum approximately 5.7 second event gap prove that one log row is not one independent hardware frame. Row count cannot prove frame-level pairing.
3. `pairProven=false` and safe-zero still prevent potentially stale or cross-clock values from entering GamepadMotion/Host. This is a safety gate, not an HC mathematical offset.
4. The user observation of non-zero motion while still is a runtime observation only. This package has no controlled pose script, independent stillness window, or external consumer readback; raw values alone cannot establish an HC axis, sign, drift, or calibration error.

## 4. Collector limitation

The independent WinRT collector in `rog-imu-evidence.json` reports:

```text
status                 = polling-fallback
eventSubscription      = InvalidOperationException (PowerShell could not subscribe to Windows RT events)
gyrometer samples      = 0
accelerometer samples  = 0
```

Therefore that collector is not proof of HC `ReadingChanged` callback cadence. The YMCC native capture is the raw IMU evidence in this package. It still has no DS4 HID descriptor, input/feature report, report ID/length, raw bytes, or Steam/game consumer receipt.

## 5. PS4 low battery - 5 percent issue

The user observation remains registered as:

```text
PS4 battery 5% origin = UNKNOWN / EXTERNAL-OR-ENCODER-DEFAULT-POSSIBILITY
```

This package contains no DS4 raw HID or battery feature report, so P64 is unchanged:

- HC `DualShock4Target.BuildReport()` uses a 31-byte VIIPER report and does not write a battery field.
- HC `DS4OutDevice.cs` defines `DS4_REPORT_EX.bBatteryLvl`, but no production call chain was found for it.
- DSU `DsBattery` is DSU network metadata, not the HIDMaestro DS4 report.
- YMCC `HMGamepadState` and native input capture have no proven DS4 battery assignment.
- Steam's `5%` display cannot be used as a source attribution.

Closing this issue still requires the same Host lifetime's virtual DS4 descriptor, input/feature raw bytes, report ID/length, device instance/container identity, and an independent decoder/readback. Do not guess-write `0x05`, `100%`, or `Full`.

## 6. Error / unknown / HC drift / original logic ledger

```text
Proven:
  RC73XA HC device branch and exact XboxROGAllyX matrix are reproducible from DMI + HC source
  YMCC raw IMU path exists; this package has no input-capture worker JSON exception

Unknown or gap:
  DMI identity entering the YMCC native matrix receipt
  same-provider/same-generation pair proof, age/skew admission, calibration lock
  GamepadMotion Default/Player/World production plane and Host ACK/first-frame receipt
  DS4 extended report/readback and PS4 battery origin
  P-HID/P-XINPUT/P-OWNER, Steam/game consumer, sleep/PnP/crash restore

No unambiguous locked-HC direct fix found:
  axis swap, sign, unit, gain, deadzone, velocity decay, battery assignment

Not classified as original drift logic:
  pairProven=false, safe-zero, raw-until-identity, direct IMU no-report
```

## 7. Changes and next gate

```text
product source changed      = none
formal Release changed      = none
system/device mutation      = none
runtimeUpgrade              = false
```

After this page and the main taskbook were written, RF00 was re-run and T20 -> T21 -> T14 were rebound to the new 74-key snapshot:

```text
manifestId   = T22-RF00-20260905-T10-I-HC-ROUTE-STATIC-SNAPSHOT
sourceDigest = 0C15A26E69DCAE9EC87C3C8B8BDD41F2CAD5F37DFCA2099BC3FD74B3AF537415
keyFiles     = 74
porcelain    = 453
HC clean     = true
runtimeUpgrade = false
```

Local static work is now at the evidence boundary. The next required intervention is a same-Host DS4 raw report/readback capture, or an explicitly authorized runtime matrix/consumer readback on ROG. Until then the main line remains `RUNTIME-BLOCKED`.
