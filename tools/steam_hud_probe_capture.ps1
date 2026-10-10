param([Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$allowed='G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\'
$full=[IO.Path]::GetFullPath($OutputPath)
if(-not $full.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetExtension($full) -ne '.png'){throw 'Owned PNG output path required'}
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public class YmccHudWindowCapture {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f,IntPtr l);
  [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
  public struct RECT { public int Left,Top,Right,Bottom; }
  public class Entry { public IntPtr Handle; public uint Pid; public string Title; public bool Visible; public RECT Rect; }
  public static Entry[] Find() {var a=new List<Entry>();EnumWindows((h,l)=>{uint p;GetWindowThreadProcessId(h,out p);var s=new StringBuilder(1024);GetWindowText(h,s,1024);var title=s.ToString();if(true){RECT r;GetWindowRect(h,out r);a.Add(new Entry{Handle=h,Pid=p,Title=title,Visible=IsWindowVisible(h),Rect=r});}return true;},IntPtr.Zero);return a.ToArray();}
}
"@
$steam=[IO.Path]::GetFullPath((Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam').SteamPath)
$ownerPids=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -in @('steamwebhelper.exe','steam.exe') -and $_.ExecutablePath -and $_.ExecutablePath.StartsWith($steam,[StringComparison]::OrdinalIgnoreCase)} | ForEach-Object {[uint32]$_.ProcessId})
$owned=@([YmccHudWindowCapture]::Find()|Where-Object {$_.Pid -in $ownerPids}); if($env:YMCC_HUD_CAPTURE_LIST_ONLY -eq '1'){$owned|ForEach-Object{[pscustomobject]@{Title=$_.Title;Pid=$_.Pid;Visible=$_.Visible;Width=$_.Rect.Right-$_.Rect.Left;Height=$_.Rect.Bottom-$_.Rect.Top}}|ConvertTo-Json -Compress;exit 0}; $windows=@($owned|Where-Object {$_.Visible -and $_.Title -match '^Steam (\u5927\u5c4f\u5e55\u6a21\u5f0f|Big Picture Mode)$' -and $_.Pid -in $ownerPids -and ($_.Rect.Right-$_.Rect.Left) -gt 200 -and ($_.Rect.Bottom-$_.Rect.Top) -gt 150})
if($windows.Count -ne 1){throw "Expected one actual visible Steam-owned Big Picture window; found $($windows.Count)"}
$w=$windows[0];$width=$w.Rect.Right-$w.Rect.Left;$height=$w.Rect.Bottom-$w.Rect.Top
$bitmap=[Drawing.Bitmap]::new($width,$height);$graphics=[Drawing.Graphics]::FromImage($bitmap)
try {
  $dc=$graphics.GetHdc()
  try {$ok=[YmccHudWindowCapture]::PrintWindow($w.Handle,$dc,2)} finally {$graphics.ReleaseHdc($dc)}
  if(-not $ok){throw 'PrintWindow refused actual Steam window'}
  $bitmap.Save($full,[Drawing.Imaging.ImageFormat]::Png)
  [pscustomobject]@{ActualWindowCapture=$true;Method='PrintWindow';Title=$w.Title;Pid=$w.Pid;Width=$width;Height=$height;Path=$full} | ConvertTo-Json -Compress
} finally {$graphics.Dispose();$bitmap.Dispose()}
