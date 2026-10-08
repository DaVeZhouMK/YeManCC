$ErrorActionPreference = 'Stop'

Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class XInputSlotProbe {
    [StructLayout(LayoutKind.Sequential)]
    public struct Gamepad {
        public ushort Buttons;
        public byte LeftTrigger;
        public byte RightTrigger;
        public short LeftX;
        public short LeftY;
        public short RightX;
        public short RightY;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct State {
        public uint PacketNumber;
        public Gamepad Gamepad;
    }
    [DllImport("xinput1_4.dll", EntryPoint="XInputGetState")]
    public static extern uint GetState(uint userIndex, out State state);
}
'@

$outputPath = 'C:\SOFT\YeMan\_test-evidence-20260907\xinput-slot-observation-after-failure.jsonl'
$start = Get-Date
$durationSeconds = 8
[ordered]@{ event = 'probe-start'; time = $start.ToString('o'); durationSeconds = $durationSeconds } | ConvertTo-Json -Compress | Set-Content -LiteralPath $outputPath -Encoding UTF8
while (((Get-Date) - $start).TotalSeconds -lt $durationSeconds) {
    for ($slot = 0; $slot -lt 4; $slot++) {
        $state = New-Object XInputSlotProbe+State
        $result = [XInputSlotProbe]::GetState([uint32]$slot, [ref]$state)
        if ($result -eq 0) {
            [ordered]@{
                event = 'sample'
                time = (Get-Date).ToString('o')
                slot = $slot
                packet = $state.PacketNumber
                buttons = $state.Gamepad.Buttons
                lt = $state.Gamepad.LeftTrigger
                rt = $state.Gamepad.RightTrigger
                lx = $state.Gamepad.LeftX
                ly = $state.Gamepad.LeftY
                rx = $state.Gamepad.RightX
                ry = $state.Gamepad.RightY
            } | ConvertTo-Json -Compress | Add-Content -LiteralPath $outputPath -Encoding UTF8
        }
    }
    Start-Sleep -Milliseconds 100
}
[ordered]@{ event = 'probe-complete'; time = (Get-Date).ToString('o') } | ConvertTo-Json -Compress | Add-Content -LiteralPath $outputPath -Encoding UTF8
