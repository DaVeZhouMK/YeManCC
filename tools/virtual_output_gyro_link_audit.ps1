<#
.SYNOPSIS
  Read-only source audit for the virtual->output gyro link HC parity
  (34-VIRTUAL-OUTPUT-GYRO-LINK-HC-PARITY).

  Asserts CURRENT state of YMCC vs locked HC source for G4/G5/G8/5B/5G/5I
  claims. Read-only: never loads HIDMaestro, never touches hardware, never
  writes. Forward (present) and reverse (absent / ordering) checks.

  Anchor sources:
    - YMCC InputHost/Program.cs
    - YMCC native/main.cpp
    - HC pinned Targets/Xbox360Target.cs, Targets/DualShock4Target.cs,
      Controllers/XInputController.cs, Managers/VirtualManager.cs
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$HcRoot,
  [Parameter(Mandatory = $true)][string]$InputHostSource,
  [Parameter(Mandatory = $true)][string]$NativeSource,
  # 前端 persona 触点（2026-09-16 补漏后加入）：默认从本脚本位置推断，
  # 保证"persona 枚举一处不漏"这类回归可被机械检出。
  [string]$FrontendView = '',
  [string]$FrontendBridgeDir = ''
)

$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($FrontendView)) {
  $FrontendView = Join-Path $projectRoot 'src\views\ButtonMappingView.vue'
}
if ([string]::IsNullOrWhiteSpace($FrontendBridgeDir)) {
  $FrontendBridgeDir = Join-Path $projectRoot 'src\bridge'
}
$feView = if (Test-Path -LiteralPath $FrontendView) { Get-Content -LiteralPath $FrontendView -Raw -Encoding UTF8 } else { '' }
$feContract = if (Test-Path -LiteralPath (Join-Path $FrontendBridgeDir 'inputContracts.ts')) { Get-Content -LiteralPath (Join-Path $FrontendBridgeDir 'inputContracts.ts') -Raw -Encoding UTF8 } else { '' }
$feGamepadContract = if (Test-Path -LiteralPath (Join-Path $FrontendBridgeDir 'gamepadControlContract.ts')) { Get-Content -LiteralPath (Join-Path $FrontendBridgeDir 'gamepadControlContract.ts') -Raw -Encoding UTF8 } else { '' }

$hcDualShock = Join-Path $HcRoot 'Targets\DualShock4Target.cs'
$hcXbox = Join-Path $HcRoot 'Targets\Xbox360Target.cs'
$hcXInput = Join-Path $HcRoot 'Controllers\XInputController.cs'
$hcVirtual = Join-Path $HcRoot 'Managers\VirtualManager.cs'
$native = Get-Content -LiteralPath $NativeSource -Raw -Encoding UTF8
$ih = Get-Content -LiteralPath $InputHostSource -Raw -Encoding UTF8
$hcDs = Get-Content -LiteralPath $hcDualShock -Raw -Encoding UTF8
$hcXb = Get-Content -LiteralPath $hcXbox -Raw -Encoding UTF8
$hcXi = Get-Content -LiteralPath $hcXInput -Raw -Encoding UTF8
$hcVm = Get-Content -LiteralPath $hcVirtual -Raw -Encoding UTF8

$fail = 0
function Assert([string]$n, [bool]$ok, [string]$detail) {
  if ($ok) { Write-Host "PASS | $n" }
  else { Write-Host "FAIL | $n | $detail"; $script:fail++ }
}

# ── G4: DS4/ DualSense 线缆布局（2026-09-16 重写）──────────────────────────
# 旧断言（'No direct IMU fields may enter the DS4' / 'DS4 encoder must not write
# gyro'）是 2026-09-13 GYRO-WIRE 之前的 fail-closed 状态，T12 已解除并接线 IMU，
# 该两条断言此后长期 FAIL（陈旧门）。现改为锁定真实 DS4 线缆布局：
#   基准 = SDL3 SDL_hidapi_ps4.c PS4StatePacket_t + ds4drv device.py parse_report
#   （gyro wire 13/15/17、accel wire 19/21/23、battery wire 30、touch id wire 35/39、
#    wire 7 bits2-7 = 6-bit 计数器；旧 data[14..25] 是 dualshock-4-v2.json 扩展
#    元数据的偏移，与同源元数据在 touch(38/42)/battery(14) 上的错误同因）。
$ds4Fn = [regex]::Match($ih, 'static byte\[\] BuildDs4V2DataOnlyReport[\s\S]*?\n    }').Value
Assert 'G4 DS4 IMU at real offsets' ($ds4Fn -match 'data, 12, envelope\.ImuGyroX' -and $ds4Fn -match 'data, 22, envelope\.ImuAccelZ') 'DS4 gyro data[12/14/16], accel data[18/20/22]'
Assert 'G4 DS4 accel 8192/g' ($ds4Fn -match 'envelope\.ImuAccelX, 8192\.0, 4\.0') 'DS4 accel = g x 8192 (SDL 1/8192, Linux DS4_ACC_RES_PER_G)'
Assert 'G4 DS4 gyro 16/dps' ($ds4Fn -match 'envelope\.ImuGyroX, 16\.0, 2048\.0') 'DS4 gyro = dps x 16 (SDL fallback 1/16)'
Assert 'G4 DS4 wire7 counter' ($ds4Fn -match 'counter & 0x3F\) << 2') 'DS4 wire 7 bits2-7 = 6-bit per-frame counter'
Assert 'G4 DS4 battery wire30' ($ds4Fn -match 'data\[Ds4WireBatteryLevelIndex\] = Ds4WireBatteryFull') 'DS4 battery at wire 30'
Assert 'G4 DS4 touch ids wire35/39' ($ds4Fn -match 'data\[34\] = TouchpadFinger0DisabledId' -and $ds4Fn -match 'data\[38\] = TouchpadFinger1DisabledId') 'DS4 touch contact ids at wire 35/39'
# wire 9-10 timestamp（2026-09-16 真机 A/B 实测补上）：真机每帧递增（单位速率
# 187,500/s，DS4 v1 直读 ~750 @250Hz），本产品 8ms 节拍折算 1500/帧。
Assert 'G4 DS4 wire9-10 timestamp' ($ds4Fn -match 'data\[9\] = \(byte\)\(wireTimestamp & 0xFF\)' -and $ds4Fn -match 'data\[10\] = \(byte\)\(\(wireTimestamp >> 8\) & 0xFF\)' -and $ds4Fn -match 'Ds4WireTimestampUnitsPerFrame = 1500|counter \* Ds4WireTimestampUnitsPerFrame') 'DS4 wire 9-10 must advance (real-pad measured unit rate)'
# DualSense（2026-09-16）：基准 = Linux hid-playstation struct dualsense_input_report
# （gyro wire 16/18/20、accel wire 22/24/26、seq wire 7、touch 33/37、status 53），
# 与 HIDMaestro dualsense profile 标注一致。
$dsFn = [regex]::Match($ih, 'static byte\[\] BuildDualSenseDataOnlyReport[\s\S]*?\n    }').Value
Assert 'G4 DualSense builder present' ([bool]$dsFn) 'InputHost must implement BuildDualSenseDataOnlyReport'
Assert 'G4 DualSense IMU offsets' ($dsFn -match 'data, 15, envelope\.ImuGyroX' -and $dsFn -match 'data, 25, envelope\.ImuAccelZ') 'DualSense gyro data[15/17/19], accel data[21/23/25]'
Assert 'G4 DualSense seq + PS byte' ($dsFn -match 'data\[6\] = \(byte\)\(frameIndex & 0xFF\)' -and $dsFn -match 'data\[9\] = \(byte\)\(\(\(envelope\.Buttons & 0x0400\)') 'DualSense seq wire 7 / PS at buttons[2] bit0'
# DualSense sensor_timestamp 必须按"单位速率×本产品节拍"推进（8ms → 24024/帧），
# 而不是按真机 4ms 的 12012（否则时间推进只有实际的一半）。
Assert 'G4 DualSense timestamp 8ms-scaled' ($dsFn -match '\* 24024L') 'DualSense sensor_timestamp must scale to the 8ms product tick'
Assert 'G4 DualSense touch + status' ($dsFn -match 'data\[32\] = TouchpadFinger0DisabledId' -and $dsFn -match 'data\[52\] = Ds4WireBatteryFull') 'DualSense touch wire 33/37, status wire 53'
Assert 'G4 DualSense persona wiring' ($ih -match '"dualsense" => "dualsense"' -and $native -match '"dualsense"') 'persona dualsense must be wired in InputHost + native'
# 前端 persona 枚举不得漏项（2026-09-16 实测缺陷回归锁）：ButtonMappingView 的
# 设置载入路径必须接受 dualsense，否则已保存的 DualSense 会在页面重载后被显示成
# 「本机手柄」，随后一次保存即把 persona 写回 disabled（静默关掉虚拟手柄）。
Assert 'G4 frontend load path accepts dualsense' ($feView -match "value === 'dualsense'") 'ButtonMappingView loadTarget must accept dualsense (settings reload path)'
Assert 'G4 frontend contract lists dualsense' ($feContract -match 'dualsense' -and $feGamepadContract -match 'dualsense') 'inputContracts / gamepadControlContract must list dualsense'
Assert 'G4 frontend persona sources present' (($feView.Length -gt 0) -and ($feContract.Length -gt 0) -and ($feGamepadContract.Length -gt 0)) 'frontend persona files must be readable (paths resolve)'

# ── G2/G1 (2026-09-11): SteamDeck persona + IMU HC parity ─────────────────
$hcDeck = Join-Path $HcRoot 'Targets\SteamDeckTarget.cs'
$hcDk = Get-Content -LiteralPath $hcDeck -Raw -Encoding UTF8
Assert 'G2 HC SteamDeckTarget present' ([bool]($hcDk -match 'class SteamDeckTarget')) 'HC SteamDeckTarget must exist'
Assert 'G2 YMCC steamdeck profile' ($ih -match '"steamdeck"[\s\S]{0,60}"steam-deck-composite"') 'InputHost must map steamdeck -> steam-deck-composite'
Assert 'G2 YMCC BuildSteamDeckState' ($ih -match 'BuildSteamDeckState\(HMProfile profile, CommandEnvelope envelope\)') 'InputHost must implement BuildSteamDeckState'
Assert 'G1 HC accel 16384/g' ($hcDk -match 'AccelCountsPerG = 16384\.0f') 'HC SteamDeckTarget accel scale 16384/g'
Assert 'G1 HC gyro 16/dps' ($hcDk -match 'GyroUnitsPerDps = 16\.0f') 'HC SteamDeckTarget gyro scale 16/dps'
Assert 'G1 YMCC accel 16384' ($ih -match 'envelope\.ImuAccelX \* 16384\.0') 'YMCC SteamDeck accel encodes 16384/g'
Assert 'G1 YMCC gyro 16' ($ih -match 'envelope\.ImuGyroX \* 16\.0') 'YMCC SteamDeck gyro encodes 16/dps'
Assert 'G1 YMCC accel axis X,-Z,Y' ($ih -match 'envelope\.ImuAccelY \* 16384\.0') 'YMCC SteamDeck accelZ = accel.Y*16384 (HC axis order)'
Assert 'G1 YMCC imu field whitelist' ($ih -match '"imu"') 'FrameFields must admit the imu field'
Assert 'G1 native frame imu' ($native -match '"imu", \{' -or $native -match 'imuAdmitted') 'native SUBMIT_FRAME must carry imu block'
# G1 (2026-09-11 frame trace): HC SteamDeckTarget feeds GetRawGyro/GetRawAcceleration
# (== ProcessMotion input: matrixed raw dps/g, SteamDeckTarget.cs:95-96), NOT the
# calibrated output. native must send mgx/mgy/mgz (matrixed input) and maxa/.. for
# the wire, while the motion->stick blend keeps the calibrated cgx plane.
Assert 'G1 native SD gyroDps = raw matrixed input' ($native -match 'mgx, mgy, mgz,\r?\n\s+maxa, may, maz,') 'SD wire must feed mgx (HC GetRawGyro parity), not calibrated cgx'
Assert 'G1 native SD accelG = input accel' ($native -match '"accelG", \{\{"x", imuAdmitted \? maxa') 'SD wire must feed maxa (HC GetRawAcceleration parity)'
Assert 'G2 usbip backend ensure' ($ih -match 'InstallUsbipBackend') 'usbip backend install path must exist (D3)'
Assert 'G2 usbip PnP root' ($ih -match 'IsSteamDeckUsbRoot') 'usbip VID_28DE PnP root recognition must exist'

# ── S54 (2026-09-11 wire probe): SteamDeck trigger axes are Z/Rz and DPad is
# HMHat. HIDMaestro steam-deck-composite StandardAxes = X/Y/Rx/Ry/Z/Rz; the
# codec ignores Vx/Vy (wire probe: Vx=Vy=1 -> triggers bytes 44-46 = 0) and
# encodes HMHat into Neptune DPAD bits (bit8-11, == HC data[9]).
Assert 'S54 YMCC Deck trigger Z' ($ih -match '\[HMAxis\.Z\] = envelope\.LtRaw') 'SteamDeck left trigger must map to HMAxis.Z consuming raw (HC SteamDeckTarget no deadzone)'
Assert 'S54 YMCC Deck trigger Rz' ($ih -match '\[HMAxis\.Rz\] = envelope\.RtRaw') 'SteamDeck right trigger must map to HMAxis.Rz consuming raw (HC SteamDeckTarget no deadzone)'
# S54 断言必须限定在 BuildSteamDeckState 函数体内（xbox360 合法消费 Vx/Vy=LtRaw/RtRaw）。
$deckFn = [regex]::Match($ih, 'private static HMGamepadState BuildSteamDeckState[\s\S]*?\n    }').Value
Assert 'S54 YMCC Deck no Vx/Vy triggers' ($deckFn -notmatch '\[HMAxis\.Vx\]|\[HMAxis\.Vy\]') 'SteamDeck must NOT consume Vx/Vy for triggers (codec ignores them)'
Assert 'S54 YMCC Deck Hat mapping' ($ih -match 'hat = HMHat\.None' -and $ih -match 'dpadUp = \(envelope\.Buttons & 0x0001\)' -and $ih -match 'HMHat\.NorthEast') 'BuildSteamDeckState must map frame dpad bits to HMHat'

# ── S57/S60 (2026-09-11): exempt HID physical read (HC OpenXInput parity). HC reads
# the physical controller through OpenXInput (in-process HID mapping, exempt
# from HidHide). S60 removed the YMCC-original "system XInput all-zero -> inject
# HID pad" takeover: HC has no such branch, and the injection produced abnormal
# frames on Flydigi 16-bit axes (S59.3 evidence). The canonical source remains
# the system XInput sample (HC UpdateXInputState semantics); the RawInput exempt
# capture stays as the zero-capable S59 diagnostic fallback that is never
# promoted to the read path.
Assert 'S57 native exempt capture fn' ($native -match 'static void physicalHidCaptureRawInput\(RAWINPUT\* raw\)') 'native must implement the exempt HID capture (HC OpenXInput parity)'
Assert 'S57 native WM_INPUT wiring' ($native -match 'physicalHidCaptureRawInput\(raw\)') 'WM_INPUT must dispatch the exempt HID capture'
Assert 'S60 canonical read uses system XInput' ($native -match 'pad = inputCanonicalizeGamepad\(g_xinputPads\[canonicalSlot\]\)' -and $native -notmatch 'pad = inputCanonicalizeGamepad\(g_physicalHidPad\)') 'read path must keep the system XInput canonical sample as the only authority (S60, HC parity); HID pad injection is removed'
Assert 'S59 native zero sample stored' ($native -match 'g_physicalHidValid = true;' -and $native -match 'rogParseReport\(prep, report, reportLen, 0, &pad\)') 'capture must store every report including release-to-zero'

# ── S58 (2026-09-11): Legion wireless hide-cycle parity. HC ControllerManager
# (2307-2312): Hide() for a Legion controller uses powerCycle = !IsWireless()
# (IsWireless = Legion HID report bytes 12/13 == 3). YMCC must skip the hide
# cycle for a wireless Legion Go controller (cycling the receiver drops the
# proprietary wireless link), fail-safe to the normal cycle otherwise.
Assert 'S58 native legion wireless fn' ($native -match 'static bool inputHostLegionWirelessFromHid\(\)') 'native must implement the Legion wireless HID check'
Assert 'S58 native legion cycle skip' ($native -match 'legionWirelessCycleSkip' -and $native -match 'hidhide.cycle-skip' -and $native -match 'legion-wireless-hc-parity') 'hide transaction must skip the cycle for a wireless Legion'
Assert 'S58 native family gate' ($native -match 'g_machineIdentity\.family == YmccFamily::LenovoLegionGo') 'Legion wireless check must be family-gated'

# ── G8: current low-band trigger dead zone is the canonical gate ──────────
# Option B (implemented, user-ruled strict parity): the DS4/Xbox360 wire
# consumes the RAW pre-canonical trigger (HC AxisState), the ≤30 canonical
# zeroing remains only for gate/merge/score semantics.
Assert 'G8 canonical trigger zeroing (gate side)' ($native -match 'bLeftTrigger <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD') 'inputCanonicalizeGamepad keeps the <30 gate'
Assert 'G8 HC raw AxisState store' ($hcXi -match 'AxisState\[AxisFlags.L2\] = .*LeftTrigger') 'HC stores raw trigger'
Assert 'G8 native frame carries ltRaw/rtRaw' ($native -match '"ltRaw", ltRaw' -and $native -match '"rtRaw", rtRaw') 'SUBMIT_FRAME must ship raw pre-canonical triggers'
Assert 'G8 raw values derive from raw pad' ($native -match 'float ltRaw, float rtRaw' -and $native -match 'static_cast<float>\(pad\.bLeftTrigger / 255\.0\)') 'ltRaw travels from the raw pre-canonical pad (inputHostSubmitPad caller)'
Assert 'G8 IH DS4 digital bit on raw>0' ($ih -match 'envelope\.LtRaw > 0f' -and $ih -match 'envelope\.RtRaw > 0f') 'DS4 L2/R2 press asserted on raw >0 (HC DualShock4Target.cs:40)'
Assert 'G8 IH DS4 analog passes raw' ($ih -match 'data\[7\] = TriggerToByte\(envelope\.LtRaw\)' -and $ih -match 'data\[8\] = TriggerToByte\(envelope\.RtRaw\)') 'DS4 data[7]/[8] write raw analog (no canonical zero)'
Assert 'G8 IH X360 consumes raw trigger' ($ih -match '\[HMAxis\.Vx\] = envelope\.LtRaw' -and $ih -match '\[HMAxis\.Vy\] = envelope\.RtRaw') 'Xbox360 AxisState consumes LtRaw/RtRaw'

# ── 5B: Xbox360 report per-bit parity markers ─────────────────────────────
# HC Xbox360Target.cs:39 maps Special -> 0x0400 (Guide); YMCC 0x0400 ->
# HMButton.Guide is the SAME guide bit, so both emit Guide. Parity check is
# presence on both sides, not absence.
Assert '5B HC x360 Guide bit present' ($hcXb -match 'ButtonFlags.Special.*0x0400') 'HC x360 must map Special to Guide'
Assert '5B YMCC Guide->HMButton.Guide' ($ih -match 'buttons \|= HMButton.Guide') 'YMCC maps 0x0400 to HMButton.Guide'
Assert '5B HC x360 direct int16 sticks' ($hcXb -match '\(short\)inputs\.AxisState') 'HC x360 writes int16 sticks'

# ── 5G: unconditional per-tick publish ─────────────────────────────────────
# 2026-09-16: packer signatures gained the per-frame wire counter arg
# (DS4 wire7 6-bit counter / DualSense seq). Both persona paths must keep
# publishing every frame, unconditionally.
Assert '5G unconditional SubmitRawReport' ($ih -match '_controller\.SubmitRawReport\(BuildDs4V2DataOnlyReport\(profile, envelope, _ds4FrameCounter\+\+\)\)' -and $ih -match '_controller\.SubmitRawReport\(BuildDualSenseDataOnlyReport\(profile, envelope, _dualSenseFrameCounter\+\+\)\)') 'TrySubmitFrame must publish every frame (DS4 + DualSense counter paths)'

# ── 5I: Xbox360 0.5-center contract ───────────────────────────────────────
Assert '5I Xbox360StickAxis 0.5 center' ($ih -match '\(value \+ 1f\) \* 0\.5f') 'Xbox360StickAxis maps signed to 0..1'

# ── 5F: motion->stick weighted blend present (HC LayoutManager parity) ────
Assert '5F axis modifiers applied' ($native -match 'inputHostApplyAxisModifiers\(contributionX, contributionY') 'pre-blend modifiers on motion contribution'
Assert '5F gyroWeight factor' ($native -match 'g_realStickTest\.gyroWeight - stickNorm') 'HC weightFactor = gyroWeight - stickNorm'
Assert '5F short clamp to int16' ($native -match 'outputShortX = inputHostClampShort') 'motion contribution clamps to int16'

# ── 5H: DS4 dpad independent-byte layout (VIIPER) parity marker ───────────
Assert '5H HC DS4 dpad own byte' ($hcDs -match 'byte dpad') 'HC DS4 uses dedicated dpad byte'
Assert '5H YMCC DS4 hat octant map' ($ih -match 'MapHatOctant\(envelope\.Buttons\)') 'YMCC DS4 encodes hat via octant map'

# ── 5J: single target owner (no multi-target fan-out) ─────────────────────
Assert '5J HC single vTarget' ($hcVm -match 'public static VIIPERTarget\? vTarget') 'HC VirtualManager owns one static target'

Write-Host ''
if ($fail -gt 0) { Write-Host "RESULT: $fail FAIL" -ForegroundColor Red; exit 1 }
Write-Host 'RESULT: ALL PASS' -ForegroundColor Green
exit 0