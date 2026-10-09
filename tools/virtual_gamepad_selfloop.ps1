# virtual_gamepad_selfloop.ps1 - automated local virtual-gamepad (DS4/X360) self-test loop engine
# Usage: powershell -ExecutionPolicy Bypass -File tools\virtual_gamepad_selfloop.ps1 [-Persona dualshock4|xbox360|dualsense|disabled] [-DurationSec 25] [-OutDir <dir>] [-WaitStartSec 150] [-WireWatchSec 10] [-LeakWatchSec 12] [-StartProbeSec 8] [-UsageWatchSec 6] [-WireProfilePath <file>] [-WireDiffPath <file>]
# WireProfilePath saves the observed per-field activity of this run (reference for a real-pad A/B diff); WireDiffPath compares this run against such a reference.
# WireWatchSec>0 additionally runs tools\hid_readonly_probe --watch during the window (read-only wire capture; idle phantom check + field-level layout evidence).
# LeakWatchSec>0 samples XInput slots / gamepad HID nodes / fresh-process physical-HID opens while the target is up (second-source check).
# StartProbeSec>0 opens the Start menu (VK_LWIN tap, tap verified) and reports whether it stays open while the target is up (user-symptom check).
# UsageWatchSec>0 reads the same report through the declared HID usages (Windows HID parser) to measure the Sony-vs-Windows convention misread.
# Flow: write settings(outputTarget) -> start YeManCC -> wait input-host-started + SUBMIT_FRAME
#       -> wait DurationSec -> graceful close (WM_CLOSE) -> snapshot logs -> restore settings.
# NOTE: keept this file ASCII-only; Windows PowerShell 5.1 parses UTF-8 w/o BOM as ANSI.
param(
    [string]$Persona = 'dualshock4',
    [int]$DurationSec = 25,
    [string]$OutDir = 'C:\Users\DaVe\Desktop\selfloop',
    [int]$WaitStartSec = 150,
    [int]$WireWatchSec = 0,
    [int]$LeakWatchSec = 0,
    [int]$StartProbeSec = 0,
    [int]$UsageWatchSec = 0,
    [string]$WireProfilePath = '',
    [string]$WireDiffPath = '',
    [string]$UsageVidHex = '',
    [string]$UsagePidHex = ''
)
$ErrorActionPreference = 'Stop'
$exe = 'C:\SOFT\YeMan\YeManCC\YeManCC.exe'
$settings = 'C:\SOFT\YeMan\PowerControl\yeman-settings.json'
$logDir = Join-Path $env:LOCALAPPDATA 'YeManCC'
$log = Join-Path $logDir 'native-lifecycle.log'
$vgLog = Join-Path $logDir 'virtual-gamepad.log'

if (!(Test-Path $exe)) { Write-Error "exe not found: $exe"; exit 2 }
# Pre-flight: a resident instance makes the launched process exit immediately
# ("single-instance-existing-window") and the loop then silently observes the
# STALE instance (whose settings/teardown belong to an earlier run). Fail fast
# instead; also note the app may run elevated, where Stop-Process is refused.
$running = @(Get-Process -Name 'YeManCC' -ErrorAction SilentlyContinue)
if ($running.Count -gt 0) {
    Write-Error ("YeManCC already running (pid " + ($running.Id -join ',') + "). Close it first: the loop would observe that stale instance.")
    exit 3
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$iter = 1
while (Test-Path (Join-Path $OutDir ("loop-{0:D3}-*" -f $iter))) { $iter++ }
$tag = "{0:D3}" -f $iter
Write-Output ("[loop $tag] persona=$Persona duration=${DurationSec}s")

# WM_APP_EXIT helper: the app is tray-resident, so WM_CLOSE only hides it.
# WM_APP_EXIT (WM_USER+6) is the same message app.exit IPC posts to the main
# window and is the reliable graceful-exit channel for automation.
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class EnumWin {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    public const uint WM_APP_EXIT = 0x0800 + 6;
    public static bool SendExitForPid(uint targetPid) {
        bool got = false;
        EnumWindows(delegate(IntPtr hWnd, IntPtr lParam) {
            uint pid;
            GetWindowThreadProcessId(hWnd, out pid);
            if (pid == targetPid) { PostMessage(hWnd, WM_APP_EXIT, IntPtr.Zero, IntPtr.Zero); got = true; }
            return true;
        }, IntPtr.Zero);
        return got;
    }
}
'@

# XInput slot reader (used by the leak watch and by the restore assertions).
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class XiAssert {
    [StructLayout(LayoutKind.Sequential)] public struct S { public uint n; public G g; }
    [StructLayout(LayoutKind.Sequential)] public struct G { public ushort b; public byte lt; public byte rt; public short x1; public short y1; public short x2; public short y2; }
    [DllImport("xinput1_4.dll", EntryPoint = "XInputGetState")] public static extern int GetState(int i, ref S s);
}
'@

# 1) backup + write settings
$backup = $null
if (Test-Path $settings) {
    $backup = "$settings.bak-selfloop"
    Copy-Item -LiteralPath $settings -Destination $backup -Force
}
$j = if (Test-Path $settings) { Get-Content $settings -Raw -Encoding UTF8 | ConvertFrom-Json } else { New-Object psobject }
if ($null -eq $j.input) { $j | Add-Member -NotePropertyName input -NotePropertyValue (New-Object psobject) -Force }
$ot = [ordered]@{ persona = $Persona; gyroEnabled = $true; buttonMappingEnabled = $true }
$j.input | Add-Member -NotePropertyName outputTarget -NotePropertyValue ([pscustomobject]$ot) -Force
# Enable the gyro motion chain as well: outputTarget.gyroEnabled alone leaves
# motionEnabled=false (native requires gyroMotion.enabled + outputMode=virtual-stick).
$j.input | Add-Member -NotePropertyName gyroMotion -NotePropertyValue ([pscustomobject]@{ enabled = $true; outputMode = 'virtual-stick'; outputStick = 'right'; motionMode = 'on'; motionTrigger = 'none' }) -Force
# Also enable the virtual domain log so virtual-gamepad.log is produced this run.
$j | Add-Member -NotePropertyName domainLogs -NotePropertyValue ([pscustomobject]@{ gyro = $true; virtual = $true }) -Force
$j | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $settings -Encoding UTF8
Write-Output "settings outputTarget.persona -> $Persona (domainLogs.virtual=on, gyroMotion=on)"

# 2) mark old log boundaries
$startPos = [int64]0
if (Test-Path $log) { $startPos = (Get-Item $log).Length }
$vgStart = [int64]0
if (Test-Path $vgLog) { $vgStart = (Get-Item $vgLog).Length }

# 3) start app
$p = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru
Write-Output "started pid=$($p.Id)"

# 4) wait for key events. SUBMIT_FRAME success receipts are NOT logged to
# native-lifecycle.log (only failures/mismatches are), so wait for
# input-host-fresh-neutral-admission (first admitted frame after open) as the
# "frames are flowing" signal instead. Keep started+admitted as the gate.
$deadline = (Get-Date).AddSeconds($WaitStartSec)
$events = @()
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 800
    if ($p.HasExited) { Write-Output "process exited early code=$($p.ExitCode)"; break }
    if (Test-Path $log) {
        $size = (Get-Item $log).Length
        if ($size -gt $startPos) {
            $tail = Get-Content $log -Tail 400 | Where-Object { $_ -match 'input-host-started|fresh-neutral-admission|SUBMIT_FRAME|cycle-hub-port|p-hid-admitted|hidhide.p-hid-restore|virtual-gamepad|frame-receipt' }
            foreach ($line in $tail) {
                $ev = @{
                    t = if ($line -match '"time":"([^"]+)"') { $Matches[1] } else { '?' }
                    e = if ($line -match '"event":"([^"]+)"') { $Matches[1] } else { '?' }
                    ok = if ($line -match '"(ok|step)":"?([^",]+)"?') { $Matches[2] } else { '' }
                    persona = if ($line -match '"persona":"([^"]+)"') { $Matches[1] } else { '' }
                    reason = if ($line -match '"reason":"([^"]+)"') { $Matches[1] } else { '' }
                }
                $events += ($ev.e + '|' + $ev.ok + '|' + $ev.persona + '|' + $ev.reason + '|' + $ev.t)
            }
        }
    }
    $started = ($events | Where-Object { $_ -like 'input-host-started*' })
    $admitted = ($events | Where-Object { $_ -like 'input-host-fresh-neutral-admission*' })
    if ($started -and $admitted) {
        Write-Output "target up: started + fresh-neutral-admission (frames flowing)"
        break
    }
    if (($events | Where-Object { $_ -like '*frame-receipt*' -or $_ -like 'input-host-command-result*' -or $_ -like '*p-hid-restore*' })) {
        Write-Output "early sign of failure: $($events | Select-Object -Last 1)"
    }
}
Write-Output ("observation window ${DurationSec}s ...")

# 4b) wire-watch: consumer-side read-only capture of the live virtual target.
# Decodes every field of the raw input report and reports changes only, so any
# signal that is actually on the wire (even one the UI cannot show) is visible.
if ($WireWatchSec -gt 0) {
    $probe = Join-Path $PSScriptRoot 'hid_readonly_probe\bin\Release\net10.0-windows\hid_readonly_probe.exe'
    $watchOut = Join-Path $OutDir "loop-$tag-wire-watch.txt"
    if (Test-Path $probe) {
        Write-Output "wire-watch ${WireWatchSec}s -> $watchOut"
        $probeArgs = @('--watch', "$WireWatchSec", '--wait', '30')
        if ($WireProfilePath) { $probeArgs += @('--save-profile', $WireProfilePath) }
        if ($WireDiffPath) { $probeArgs += @('--diff-profile', $WireDiffPath) }
        & $probe @probeArgs 2>&1 | Tee-Object -FilePath $watchOut | Out-Null
        Write-Output ("wire-watch done: " + ((Get-Content -LiteralPath $watchOut -ErrorAction SilentlyContinue | Where-Object { $_ -match 'UNEXPECTED|NO unexpected|by design changed|frames=|never changed|profile saved|A/B diff|changed THERE|changed HERE|active on both' }) -join ' || '))
    } else {
        Write-Output "wire-watch skipped: probe not built ($probe)"
    }
}

# 4b2) usage-watch: read the SAME report through the declared HID usages (Windows
# HID parser). This shows what a Windows-convention consumer (DirectInput/WGI)
# reports for a Sony-convention pad, i.e. the measured form of the misread.
if ($UsageWatchSec -gt 0) {
    $probe = Join-Path $PSScriptRoot 'hid_readonly_probe\bin\Release\net10.0-windows\hid_readonly_probe.exe'
    $usageOut = Join-Path $OutDir "loop-$tag-usage-watch.txt"
    if (Test-Path $probe) {
        Write-Output "usage-watch ${UsageWatchSec}s -> $usageOut"
        $uArgs = @('--usage-watch', "$UsageWatchSec", '--wait', '20')
        if ($UsageVidHex) { $uArgs += @('--vid', $UsageVidHex) }
        if ($UsagePidHex) { $uArgs += @('--pid', $UsagePidHex) }
        & $probe @uArgs 2>&1 | Tee-Object -FilePath $usageOut | Out-Null
        Get-Content -LiteralPath $usageOut -ErrorAction SilentlyContinue | Where-Object { $_ -match '^\s+t=|final view|summary' } | Select-Object -First 6 | ForEach-Object { Write-Output ("   usage: " + $_.Trim()) }
    } else {
        Write-Output "usage-watch skipped: probe not built ($probe)"
    }
}

# 4c) leak watch: while the virtual target is up, is any SECOND input source
# visible? Two sources (physical pad plus virtual pad) would double-fire actions
# such as a Start-menu toggle, so this samples XInput slots, present gamepad HID
# nodes and - decisively - whether THIS fresh, non-allowlisted process can open
# the physical gamepad HID interfaces (HidHide blocks new opens; a handle holder
# from before the hide is out of scope here).
if ($LeakWatchSec -gt 0) {
    $probe = Join-Path $PSScriptRoot 'hid_readonly_probe\bin\Release\net10.0-windows\hid_readonly_probe.exe'
    $leakOut = Join-Path $OutDir "loop-$tag-leak-watch.jsonl"
    Remove-Item -LiteralPath $leakOut -Force -ErrorAction SilentlyContinue
    $physicalVendorIds = @(1118, 2645, 2821)   # 0x045E Microsoft, 0x0B05 ASUS, 0x1404 (legacy)
    $deadline = (Get-Date).AddSeconds($LeakWatchSec)
    $samples = @()
    while ((Get-Date) -lt $deadline) {
        $slots = @()
        for ($i = 0; $i -lt 4; $i++) {
            $s = New-Object XiAssert+S
            if ([XiAssert]::GetState($i, [ref]$s) -eq 0) { $slots += $i }
        }
        $padNodes = @(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
            Where-Object { $_.InstanceId -match 'VID_045E|VID_0B05|VID_054C|HIDMaestro' })
        $openPhysical = @()
        $hidInterfaces = -1
        if (Test-Path $probe) {
            try {
                $snap = (& $probe 2>&1 | Out-String) | ConvertFrom-Json
                $hidInterfaces = $snap.count
                $openPhysical = @($snap.devices | Where-Object { $physicalVendorIds -contains $_.vid -and $_.open -eq $true } |
                    ForEach-Object { ("{0:X4}:{1:X4}" -f $_.vid, $_.pid) })
            } catch { $hidInterfaces = -2 }
        }
        $sample = [ordered]@{
            t = (Get-Date).ToUniversalTime().ToString('o')
            xinputSlots = ($slots -join ',')
            padNodes = $padNodes.Count
            virtualNodePresent = [bool](@($padNodes | Where-Object { $_.InstanceId -match 'VID_054C|HIDMaestro' }).Count)
            hidInterfaces = $hidInterfaces
            physicalHidOpenedByFreshProcess = @($openPhysical | Select-Object -Unique)
        }
        $samples += $sample
        ($sample | ConvertTo-Json -Compress) | Add-Content -LiteralPath $leakOut
        Start-Sleep -Milliseconds 1500
    }
    $slotEver = @($samples | ForEach-Object { $_.xinputSlots } | Where-Object { $_ -ne '' } | Select-Object -Unique)
    $openedEver = @($samples | ForEach-Object { $_.physicalHidOpenedByFreshProcess } | Select-Object -Unique)
    Write-Output ("leak-watch ${LeakWatchSec}s samples=$($samples.Count): xinputSlotsSeen=[$($slotEver -join ';')] physicalHidOpened=[$($openedEver -join ';')]")
}

# 4d) start-menu watch: the user-reported symptom is "the Start menu goes back"
# while the virtual pad is active. Open the menu with a VK_LWIN tap (verifying
# the tap actually worked, else the result is inconclusive) and sample whether it
# stays open. Any input event (our wire included) would dismiss it immediately.
if ($StartProbeSec -gt 0) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class StartProbeHarness {
    [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr hWnd, StringBuilder name, int max);
    [StructLayout(LayoutKind.Sequential)] public struct LASTINPUTINFO { public uint cbSize; public uint dwTime; }
    [DllImport("user32.dll")] public static extern bool GetLastInputInfo(ref LASTINPUTINFO plii);
    public static void TapWinKey() { keybd_event(0x5B, 0, 0, UIntPtr.Zero); keybd_event(0x5B, 0, 2, UIntPtr.Zero); }
    // GetLastInputInfo advances on keyboard/mouse input only (not on gamepad
    // reports), so it separates "somebody injected key/mouse" from "the shell
    // reacted to the HID device itself".
    public static uint LastInputTick() { var info = new LASTINPUTINFO { cbSize = (uint)Marshal.SizeOf(typeof(LASTINPUTINFO)) }; GetLastInputInfo(ref info); return info.dwTime; }
    public static string FgClass() {
        var h = GetForegroundWindow();
        if (h == IntPtr.Zero) return "<none>";
        var sb = new StringBuilder(256);
        GetClassName(h, sb, sb.Capacity);
        return sb.ToString();
    }
}
'@
    $startClasses = @('DV2ControlHost', 'Windows.UI.Core.CoreWindow', 'XamlExplorerHostIslandWindow', 'SearchHost', 'Start')
    # Input sniffer: low-level keyboard/mouse hooks record every event with the
    # LLKHF/LLMHF INJECTED flag, so an injected event that dismisses the menu can
    # be named (and told apart from a real hardware keystroke).
    Add-Type -ReferencedAssemblies 'System.Windows.Forms' -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Windows.Forms;
public static class InputSniffer {
    public delegate IntPtr HookProc(int nCode, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", SetLastError=true)] public static extern IntPtr SetWindowsHookEx(int idHook, HookProc lpfn, IntPtr hMod, uint dwThreadId);
    [DllImport("user32.dll")] public static extern bool UnhookWindowsHookEx(IntPtr hhk);
    [DllImport("user32.dll")] public static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);
    public const int WH_KEYBOARD_LL = 13, WH_MOUSE_LL = 14;
    public const uint LLKHF_INJECTED = 0x10, LLMHF_INJECTED = 0x01;
    [StructLayout(LayoutKind.Sequential)] public struct KBDLLHOOKSTRUCT { public uint vkCode; public uint scanCode; public uint flags; public uint time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)] public struct MSLLHOOKSTRUCT { public int ptX; public int ptY; public uint mouseData; public uint flags; public uint time; public IntPtr dwExtraInfo; }
    static IntPtr kbHook = IntPtr.Zero, msHook = IntPtr.Zero;
    static HookProc kbProc, msProc;
    static readonly List<string> Events = new List<string>();
    static int Origin;
    public static void Start() {
        Origin = Environment.TickCount;
        if (kbProc == null) kbProc = KbCallback;
        if (msProc == null) msProc = MsCallback;
        kbHook = SetWindowsHookEx(WH_KEYBOARD_LL, kbProc, IntPtr.Zero, 0);
        msHook = SetWindowsHookEx(WH_MOUSE_LL, msProc, IntPtr.Zero, 0);
    }
    public static void Stop() {
        if (kbHook != IntPtr.Zero) UnhookWindowsHookEx(kbHook);
        if (msHook != IntPtr.Zero) UnhookWindowsHookEx(msHook);
        kbHook = msHook = IntPtr.Zero;
    }
    static IntPtr KbCallback(int nCode, IntPtr wParam, IntPtr lParam) {
        if (nCode >= 0) {
            var d = (KBDLLHOOKSTRUCT)Marshal.PtrToStructure(lParam, typeof(KBDLLHOOKSTRUCT));
            bool injected = (d.flags & LLKHF_INJECTED) != 0;
            lock (Events) Events.Add(string.Format("t={0}ms KEY vk=0x{1:X2} scan=0x{2:X} msg=0x{3:X} injected={4} extraInfo=0x{5:X}", Environment.TickCount - Origin, d.vkCode, d.scanCode, (int)wParam, injected, unchecked((ulong)d.dwExtraInfo.ToInt64())));
        }
        return CallNextHookEx(IntPtr.Zero, nCode, wParam, lParam);
    }
    static IntPtr MsCallback(int nCode, IntPtr wParam, IntPtr lParam) {
        if (nCode >= 0) {
            var d = (MSLLHOOKSTRUCT)Marshal.PtrToStructure(lParam, typeof(MSLLHOOKSTRUCT));
            bool injected = (d.flags & LLMHF_INJECTED) != 0;
            lock (Events) Events.Add(string.Format("t={0}ms MOUSE flags=0x{1:X} data=0x{2:X} msg=0x{3:X} injected={4}", Environment.TickCount - Origin, d.flags, d.mouseData, (int)wParam, injected));
        }
        return CallNextHookEx(IntPtr.Zero, nCode, wParam, lParam);
    }
    public static string[] Drain() { lock (Events) { var a = Events.ToArray(); Events.Clear(); return a; } }
    public static void Pump(int ms) { int end = Environment.TickCount + ms; while (Environment.TickCount < end) { Application.DoEvents(); System.Threading.Thread.Sleep(5); } }
}
'@
    [InputSniffer]::Start()
    [InputSniffer]::Pump(200) | Out-Null
    $null = [InputSniffer]::Drain()
    if ($startClasses -contains [StartProbeHarness]::FgClass()) { [StartProbeHarness]::TapWinKey(); [InputSniffer]::Pump(800) | Out-Null }
    [StartProbeHarness]::TapWinKey()
    [InputSniffer]::Pump(1200) | Out-Null
    $menuOpened = $startClasses -contains [StartProbeHarness]::FgClass()
    if (-not $menuOpened) {
        $events = [InputSniffer]::Drain()
        [InputSniffer]::Stop()
        Write-Output ("start-menu watch: INCONCLUSIVE (VK_LWIN tap did not open the menu; foreground=" + [StartProbeHarness]::FgClass() + ") | injected events seen=" + $events.Count)
        foreach ($ev in ($events | Select-Object -First 10)) { Write-Output ("   " + $ev) }
    } else {
        $tickAfterOpen = [StartProbeHarness]::LastInputTick()
        $probeStart = Get-Date
        $closedAfter = $null
        $tickAtClose = $null
        $remaining = $StartProbeSec * 1000
        while ($remaining -gt 0) {
            [InputSniffer]::Pump(100) | Out-Null
            $remaining -= 100
            if (-not ($startClasses -contains [StartProbeHarness]::FgClass())) {
                $closedAfter = [Math]::Round(((Get-Date) - $probeStart).TotalSeconds, 2)
                $tickAtClose = [StartProbeHarness]::LastInputTick()
                break
            }
        }
        $events = [InputSniffer]::Drain()
        [InputSniffer]::Stop()
        $verdict = if ($null -eq $closedAfter) { "STAYED-OPEN ${StartProbeSec}s+" } else { "CLOSED-AFTER ${closedAfter}s (reproduced)" }
        $kbMouse = if ($null -eq $tickAtClose) { 'n/a' } elseif ($tickAtClose -ne $tickAfterOpen) { "system last-input advanced by $([int]($tickAtClose - $tickAfterOpen))ms" } else { 'no keyboard/mouse input around the close' }
        Write-Output ("start-menu watch: tap=ok verdict=$verdict | $kbMouse | events=$($events.Count)")
        foreach ($ev in ($events | Select-Object -First 40)) { Write-Output ("   " + $ev) }
        if ($events.Count -gt 40) { Write-Output ("   ... +" + ($events.Count - 40) + " more") }
        if ($null -eq $closedAfter) { [StartProbeHarness]::TapWinKey(); Start-Sleep -Milliseconds 700 }
    }
}

Start-Sleep -Seconds $DurationSec

# 5) restore settings FIRST so native observes publicationWanted=false and runs
# the ordered stop chain (QUIESCE->release->restore->cycle) while the process is
# still alive. Only after the visibility restore completes do we exit the app.
if ($backup) { Copy-Item -LiteralPath $backup -Destination $settings -Force } else { Remove-Item -LiteralPath $settings -Force -ErrorAction SilentlyContinue }
Write-Output "settings restored (persona disabled) -> waiting for ordered stop chain"
Start-Sleep -Seconds 4

# 5b) graceful close via WM_APP_EXIT (tray-resident app: WM_CLOSE only hides)
[EnumWin]::SendExitForPid([uint32]$p.Id) | Out-Null
Start-Sleep -Seconds 6
if (!$p.HasExited) {
    Start-Sleep -Seconds 3
    if (!$p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
}
Write-Output "stop: wm_app_exit sent exited=$($p.HasExited)"

# 6) snapshot logs
Start-Sleep -Seconds 2
Copy-Item -LiteralPath $log -Destination (Join-Path $OutDir "loop-$tag-native.log") -Force -ErrorAction SilentlyContinue
if (Test-Path $vgLog) { Copy-Item -LiteralPath $vgLog -Destination (Join-Path $OutDir "loop-$tag-virtual-gamepad.log") -Force -ErrorAction SilentlyContinue }
$rc = Join-Path $logDir 'recovery-service.log'
if (Test-Path $rc) { Copy-Item -LiteralPath $rc -Destination (Join-Path $OutDir "loop-$tag-recovery.log") -Force -ErrorAction SilentlyContinue }

# 6.5) HC-parity restore assertions (virtual target disconnected -> physical
# controller must be visible again, HidHide empty, no HIDMaestro endpoints):
#   1. native stop chain produced p-hid-restore (unhideOk/readback)
#   2. at least one physical XInput slot is connected
#   3. HidHide --dev-list is empty and cloak is off
#   4. no HIDMaestro / DS4(054C) / X360(045E-IG_00) virtual endpoint in device tree
$restoreEvidence = Get-Content (Join-Path $OutDir "loop-$tag-native.log") -ErrorAction SilentlyContinue |
    Where-Object { $_ -match 'input-host-stop-result' } | Select-Object -Last 1
$restoreOk = $restoreEvidence -match 'visibilityRestored":true'

$slots = @()
for ($i = 0; $i -lt 4; $i++) {
    $s = New-Object XiAssert+S
    if ([XiAssert]::GetState($i, [ref]$s) -eq 0) { $slots += $i }
}
$xinputOk = $slots.Count -gt 0

$cli = 'C:\Program Files\Nefarius Software Solutions\HidHide\x64\HidHideCLI.exe'
$hidHideEmpty = $null
$cliError = ''
if (Test-Path $cli) {
    try {
        $devListText = (& $cli --dev-list 2>$null | Out-String).Trim()
        $cloakText = (& $cli --cloak-state 2>$null | Out-String).Trim()
        $hidHideEmpty = ($devListText.Length -eq 0) -and ($cloakText -match '--cloak-off')
    } catch {
        # Elevated contexts can hit HidHideCLI FilterDriverProxy 0x0005; the
        # native p-hid-restore readback in the log is the authoritative proof.
        $cliError = $_.Exception.Message
        $hidHideEmpty = $null
    }
}
$physicalHid = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object { $_.InstanceId -match 'HID\\VID_045E&PID_028E' -and $_.InstanceId -notmatch 'ROOT\\' }
$physicalHidPresent = ($physicalHid | Measure-Object).Count -gt 0
# Virtual x360 endpoint is a HIDMaestro ROOT node (ROOT\VID_045E...IG_00);
# the physical MS controller is a regular HID/USB node. Virtual Sony targets are
# ROOT\VID_054C nodes as well - a REAL Sony pad plugged over USB is a USB\ node
# and must NOT be counted here (2026-09-16: a plugged real DS4 made this assert
# report a false leak).
$virtualEndpoint = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
    Where-Object { $_.InstanceId -match 'HIDMaestro' -or $_.InstanceId -match '^ROOT\\VID_054C' -or $_.InstanceId -match '^ROOT\\VID_045E' }
$noVirtualEndpoint = ($null -eq $virtualEndpoint)

$asserts = [ordered]@{
    restoreReceiptOk = [bool]$restoreOk
    physicalXinputSlots = ($slots -join ',')
    xinputConnected = [bool]$xinputOk
    physicalHidPresent = [bool]$physicalHidPresent
    hidHideEmpty = $hidHideEmpty
    hidHideCliError = $cliError
    noVirtualEndpoint = [bool]$noVirtualEndpoint
}
$asserts | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $OutDir "loop-$tag-assert.json")
$allPass = $restoreOk -and $xinputOk -and $physicalHidPresent -and ($null -eq $virtualEndpoint)
Write-Output "restore-assert: pass=$allPass $($asserts | ConvertTo-Json -Compress -Depth 4)"

# 7) summary
$summary = (Get-Content (Join-Path $OutDir "loop-$tag-native.log") -ErrorAction SilentlyContinue | Where-Object {
    $_ -match 'input-host-lifecycle-work|cycle-hub-port|p-hid-admitted|p-hid-restore|p-hid-deferred|input-host-started|SUBMIT_FRAME|frame-receipt|input-host-stop-result|rog\.hid-frame|virtual-gamepad'
})
$summary | Set-Content (Join-Path $OutDir "loop-$tag-summary.txt")
Write-Output "snapshots -> $OutDir\loop-$tag-*"
Write-Output "[loop $tag] done"