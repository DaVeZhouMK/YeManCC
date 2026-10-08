# feedback_capability_probe.ps1
# ROG 反馈能力诊断探针（只读，不改任何设置/设备）。
# 用途：把「震动/灯光不可用」时的系统侧事实一次性打出来，供反馈与定位。
# 运行：  powershell -NoProfile -ExecutionPolicy Bypass -File feedback_capability_probe.ps1
# 输出：  XInput 四个槽位 + 0B05 设备枚举 + 反馈相关日志文件位置。

$ErrorActionPreference = 'Continue'

Write-Output '==================== feedback capability probe ===================='

# ── Part 1: XInput 槽位（与 YMCC native 同一读取方式）──
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ProbeXInput {
  [StructLayout(LayoutKind.Sequential)] public struct Gamepad {
    public ushort wButtons; public byte bLeftTrigger; public byte bRightTrigger;
    public short sThumbLX; public short sThumbLY; public short sThumbRX; public short sThumbRY;
  }
  [StructLayout(LayoutKind.Sequential)] public struct State {
    public uint dwPacketNumber; public Gamepad Gamepad;
  }
  [DllImport("xinput1_4.dll", CallingConvention = CallingConvention.Winapi)]
  public static extern int XInputGetState(uint dwUserIndex, out State pState);
  [DllImport("xinput1_4.dll", CallingConvention = CallingConvention.Winapi)]
  public static extern int XInputGetCapabilities(uint dwUserIndex, uint dwFlags, out IntPtr pCapabilities);
}
'@
Write-Output ''
Write-Output '--- XInput slots (xinput1_4) ---'
$connectedMask = 0
for ($slot = 0; $slot -lt 4; $slot++) {
  $state = New-Object ProbeXInput+State
  $rc = [ProbeXInput]::XInputGetState([uint32]$slot, [ref]$state)
  if ($rc -eq 0) {
    $connectedMask = $connectedMask -bor (1 -shl $slot)
    Write-Output ("slot {0}: CONNECTED  buttons=0x{1:X4} LTRT={2}/{3} sticks=({4},{5})/({6},{7})" -f $slot,
      $state.Gamepad.wButtons, $state.Gamepad.bLeftTrigger, $state.Gamepad.bRightTrigger,
      $state.Gamepad.sThumbLX, $state.Gamepad.sThumbLY, $state.Gamepad.sThumbRX, $state.Gamepad.sThumbRY)
  } elseif ($rc -eq 0x48F) {
    Write-Output ("slot {0}: not-connected" -f $slot)   # ERROR_DEVICE_NOT_CONNECTED
  } else {
    Write-Output ("slot {0}: rc=0x{1:X8}" -f $slot, $rc)
  }
}
Write-Output ("connectedMask=0x{0:X}" -f $connectedMask)

# ── Part 2: 0B05（ASUS/ROG）相关 PnP 设备 ──
Write-Output ''
Write-Output '--- PnP devices containing VID_0B05 ---'
$ropDevices = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
  Where-Object { $_.InstanceId -match 'VID_0B05' }
if (-not $ropDevices) {
  Write-Output 'no present 0B05 device found (Aura handle would stay closed -> 灯光不可用)'
} else {
  $ropDevices | Sort-Object InstanceId | ForEach-Object {
    Write-Output ("{0} | class={1} | status={2} | problem={3} | {4}" -f
      $_.InstanceId, $_.Class, $_.FriendlyName, $_.Status, $_.ProblemCode)
  }
}

# ── Part 3: 其它可能被当成“物理手柄”的 XInput/HID 设备 ──
Write-Output ''
Write-Output '--- HID game controllers / XInput-class devices (non-0B05) ---'
$otherPad = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
  Where-Object { ($_.Class -eq 'XUSB' -or $_.Class -eq 'XboxComposite' -or ($_.Class -eq 'HIDClass' -and $_.FriendlyName -match '手柄|controller|gamepad')) -and $_.InstanceId -notmatch 'VID_0B05' }
if (-not $otherPad) { Write-Output '(none)' } else {
  $otherPad | Sort-Object InstanceId -Unique | ForEach-Object {
    Write-Output ("{0} | class={1} | status={2} | problem={3}" -f $_.InstanceId, $_.Class, $_.Status, $_.ProblemCode)
  }
}

# ── Part 4: 反馈相关日志文件位置（不发内容，只需路径）──
Write-Output ''
Write-Output '--- feedback/log file locations ---'
$local = $env:LOCALAPPDATA
if ($local) {
  Get-ChildItem -LiteralPath $local -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match 'YeMan' } |
    ForEach-Object {
      Write-Output ("appData dir: {0}" -f $_.FullName)
      foreach ($candidate in @('native-lifecycle.log', 'app.log', 'hardware-coordinator.log', 'fan-lifecycle.log')) {
        $p = Join-Path $_.FullName $candidate
        if (Test-Path -LiteralPath $p) {
          $fi = Get-Item -LiteralPath $p
          Write-Output ("    {0}  ({1} bytes, last write {2})" -f $candidate, $fi.Length, $fi.LastWriteTime)
        }
      }
    }
}

Write-Output ''
Write-Output '==================== probe done ===================='