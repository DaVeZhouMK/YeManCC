param([ValidateSet('Inspect','Focus','Capture','ToggleOverlay')][string]$Action='Inspect',[string]$OutputPath)
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Threading;
public class Ymcc2077Window {
 public struct RECT {public int Left,Top,Right,Bottom;}
 public struct POINT {public int X,Y;}
 [StructLayout(LayoutKind.Sequential)] public struct MONITORINFO {public int cbSize; public RECT Monitor,Work; public uint flags;}
 [DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr h,uint flags);
 [DllImport("user32.dll",EntryPoint="GetMonitorInfoW")] public static extern bool GetMonitorInfo(IntPtr h,ref MONITORINFO info);
 [DllImport("user32.dll",EntryPoint="GetWindowLongW")] public static extern int GetWindowLong(IntPtr h,int index);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ShowWindowAsync(IntPtr h,int c);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h,ref POINT p);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a,uint b,bool v);
 [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")] static extern void keybd_event(byte k,byte scan,uint flags,UIntPtr extra);
 public static string Title(IntPtr h){var s=new StringBuilder(1024);GetWindowText(h,s,1024);return s.ToString();}
 public static bool Focus(IntPtr h){uint p;uint fg=GetWindowThreadProcessId(GetForegroundWindow(),out p),own=GetCurrentThreadId();bool attached=false;try{if(fg!=own)attached=AttachThreadInput(own,fg,true);if(IsIconic(h))ShowWindowAsync(h,9);SetForegroundWindow(h);Thread.Sleep(180);return GetForegroundWindow()==h;}finally{if(attached)AttachThreadInput(own,fg,false);}}
 public static void OverlayHotkey(){try{keybd_event(0x10,0,0,UIntPtr.Zero);keybd_event(0x09,0,0,UIntPtr.Zero);Thread.Sleep(60);}finally{keybd_event(0x09,0,2,UIntPtr.Zero);keybd_event(0x10,0,2,UIntPtr.Zero);}}
}
"@
[void][Ymcc2077Window]::SetThreadDpiAwarenessContext([IntPtr](-4))
$games=@(Get-Process -Name Cyberpunk2077 -ErrorAction Stop)
if($games.Count -ne 1 -or $games[0].MainWindowHandle -eq 0){throw 'Require one already running Cyberpunk2077 window; never launch/close the game'}
$game=$games[0];$h=[IntPtr]$game.MainWindowHandle;$previous=[Ymcc2077Window]::GetForegroundWindow();$owner=0;[void][Ymcc2077Window]::GetWindowThreadProcessId($h,[ref]$owner);if($owner -ne $game.Id){throw 'Game window ownership changed'}
if($Action -in @('Focus','Capture','ToggleOverlay')){if(-not [Ymcc2077Window]::Focus($h)){throw 'Could not activate the actual game; no key/capture sent'}}
if($Action -eq 'ToggleOverlay'){[Ymcc2077Window]::OverlayHotkey();Start-Sleep -Milliseconds 400}
$rect=New-Object Ymcc2077Window+RECT;$client=New-Object Ymcc2077Window+RECT;$pt=New-Object Ymcc2077Window+POINT
[void][Ymcc2077Window]::GetWindowRect($h,[ref]$rect);[void][Ymcc2077Window]::GetClientRect($h,[ref]$client);[void][Ymcc2077Window]::ClientToScreen($h,[ref]$pt)
$mi=New-Object Ymcc2077Window+MONITORINFO;$mi.cbSize=[Runtime.InteropServices.Marshal]::SizeOf($mi)
$monitorHandle=[Ymcc2077Window]::MonitorFromWindow($h,2);$monitorAvailable=[Ymcc2077Window]::GetMonitorInfo($monitorHandle,[ref]$mi)
$monitor=$null
if($monitorAvailable){$monitor=@{x=$mi.Monitor.Left;y=$mi.Monitor.Top;width=$mi.Monitor.Right-$mi.Monitor.Left;height=$mi.Monitor.Bottom-$mi.Monitor.Top;ClientExactlyCoversMonitor=($pt.X -eq $mi.Monitor.Left -and $pt.Y -eq $mi.Monitor.Top -and $client.Right -eq $mi.Monitor.Right-$mi.Monitor.Left -and $client.Bottom -eq $mi.Monitor.Bottom-$mi.Monitor.Top)}}
$style=[Ymcc2077Window]::GetWindowLong($h,-16);$exStyle=[Ymcc2077Window]::GetWindowLong($h,-20)
$result=[ordered]@{GeometryIsNotExclusiveFullscreenProof=$true;Monitor=$monitor;StyleHex=$style.ToString('X8');ExtendedStyleHex=$exStyle.ToString('X8');Action=$Action;GamePid=$game.Id;GameHandle=$h.ToInt64();Title=[Ymcc2077Window]::Title($h);Visible=[Ymcc2077Window]::IsWindowVisible($h);Minimized=[Ymcc2077Window]::IsIconic($h);ForegroundHandle=[Ymcc2077Window]::GetForegroundWindow().ToInt64();PreviousForegroundHandle=$previous.ToInt64();Window=@{x=$rect.Left;y=$rect.Top;width=$rect.Right-$rect.Left;height=$rect.Bottom-$rect.Top};Client=@{x=$pt.X;y=$pt.Y;width=$client.Right;height=$client.Bottom}}
if($Action -eq 'Capture'){
 $allowed='G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\';$full=[IO.Path]::GetFullPath($OutputPath)
 if(-not $full.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetExtension($full) -ne '.png'){throw 'Owned probe PNG path required'}
 if($client.Right -lt 320 -or $client.Bottom -lt 200){throw 'Actual game client is too small to validate'}
 $bitmap=[Drawing.Bitmap]::new($client.Right,$client.Bottom);$g=[Drawing.Graphics]::FromImage($bitmap)
 try{$g.CopyFromScreen($pt.X,$pt.Y,0,0,$bitmap.Size,[Drawing.CopyPixelOperation]::SourceCopy);$bitmap.Save($full,[Drawing.Imaging.ImageFormat]::Png);$result.Capture=@{Method='CopyFromScreen-game-client';Path=$full;NoHtmlSubstitute=$true}}finally{$g.Dispose();$bitmap.Dispose()}
}
$result|ConvertTo-Json -Depth 5 -Compress
