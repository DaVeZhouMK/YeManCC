[CmdletBinding()]
param(
    # Optional explicit directory for portable installs and isolated validation.
    [string]$RtssDirectory,
    # Provision files/config only; never launch or restart RTSS in this mode.
    [switch]$InstallOnly
)
$ErrorActionPreference = 'Stop'

function Resolve-RTSS {
    if ($RtssDirectory) {
        if (-not [IO.Path]::IsPathRooted($RtssDirectory)) { throw 'RTSS directory must be absolute' }
        $dir = (Resolve-Path -LiteralPath $RtssDirectory).Path
        foreach ($file in @('RTSS.exe', 'RTSSHooks64.dll', 'Plugins\Client\OverlayEditor.dll')) {
            if (-not (Test-Path -LiteralPath (Join-Path $dir $file) -PathType Leaf)) { throw "RTSS installation is incomplete: $file" }
        }
        return @{ Exe=(Join-Path $dir 'RTSS.exe'); Dir=$dir }
    }
    $candidates = @()
    try {
        $p = @(Get-Process -Name RTSS -ErrorAction SilentlyContinue | Select-Object -First 1)
        if ($p) { $candidates += $p.Path }
    } catch { }
    $candidates += @(
        (Join-Path ${env:ProgramFiles(x86)} 'RivaTuner Statistics Server\RTSS.exe'),
        (Join-Path ${env:ProgramFiles} 'RivaTuner Statistics Server\RTSS.exe'),
        (Join-Path ${env:LOCALAPPDATA} 'RivaTuner Statistics Server\RTSS.exe')
    )
    foreach ($k in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*','HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*','HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*')) {
        Get-ItemProperty $k -ErrorAction SilentlyContinue | Where-Object { $_.DisplayName -match 'RivaTuner Statistics Server|RTSS' } | ForEach-Object {
            if ($_.InstallLocation) { $candidates += (Join-Path $_.InstallLocation 'RTSS.exe') }
            if ($_.DisplayIcon) { $candidates += ($_.DisplayIcon -replace ',.*$','').Trim('"') }
        }
    }
    foreach ($candidate in ($candidates | Where-Object { $_ } | Select-Object -Unique)) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $dir = Split-Path -Parent (Resolve-Path -LiteralPath $candidate).Path
            if (Test-Path -LiteralPath (Join-Path $dir 'RTSSHooks64.dll') -PathType Leaf) { return @{ Exe=(Join-Path $dir 'RTSS.exe'); Dir=$dir } }
        }
    }
    throw 'RTSS.exe was not found; install RivaTuner Statistics Server'
}

$rtss = Resolve-RTSS
# YMCC's documented x86 RTSS client plugin invokes OverlayEditor's official
# PostOverlayMessage("Load") inside the RTSS host. Install before a normal
# launch only; NEVER stop/restart RTSS here while it is hooked into a game.
$client = Join-Path $rtss.Dir 'Plugins\Client'
if (-not (Test-Path -LiteralPath (Join-Path $client 'OverlayEditor.dll') -PathType Leaf)) {
    throw 'RTSS OverlayEditor.dll is missing; repair the RTSS installation'
}
$bridge = Join-Path $client 'YMCCOverlayBridge.dll'
$source = Join-Path $PSScriptRoot '..\YeManCC\YMCCOverlayBridge.dll'
if (-not ('YMCC.RtssIni' -as [type])) {
    Add-Type -TypeDefinition @"
using System.Runtime.InteropServices;
namespace YMCC { public static class RtssIni {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
 public static extern bool WritePrivateProfileString(string section, string key, string value, string file);
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
 public static extern bool MoveFileEx(string source, string destination, uint flags);
}}
"@
}
# Missing files are installed immediately. Update an existing bridge only
# before a normal launch; a running RTSS keeps its compatible loaded bridge.
# InstallOnly performs file preparation without probing or starting processes.
$installed = Test-Path -LiteralPath $bridge -PathType Leaf
$canUpdate = $InstallOnly -or -not (Get-Process -Name RTSS -ErrorAction SilentlyContinue)
if ((Test-Path -LiteralPath $source -PathType Leaf) -and (-not $installed -or $canUpdate)) {
    $needsCopy = -not $installed
    if ($installed) {
        try { $needsCopy = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $bridge -Algorithm SHA256).Hash }
        catch { Write-Warning "RTSS bridge comparison deferred: $($_.Exception.Message)" }
    }
    if ($needsCopy) {
        $temp = $bridge + ".ymcc-start-$PID.tmp"
        try {
            Copy-Item -LiteralPath $source -Destination $temp -Force -ErrorAction Stop
            if (-not [YMCC.RtssIni]::MoveFileEx($temp, $bridge, 9)) {
                $code = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
                throw "RTSS bridge atomic replacement failed (Win32 $code): $bridge"
            }
        } catch {
            if (-not $installed) { throw }
            Write-Warning "RTSS bridge update deferred; keeping installed DLL: $($_.Exception.Message)"
        } finally {
            if (Test-Path -LiteralPath $temp -PathType Leaf) { Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue }
        }
    }
}
if (-not (Test-Path -LiteralPath $bridge -PathType Leaf)) { throw 'YMCCOverlayBridge.dll is missing; use the complete YMCC build' }
$overlays = Join-Path $client 'Overlays'
New-Item -ItemType Directory -Path $overlays -Force | Out-Null
foreach ($name in @('YeManOBS-W-1.ovl','YeManOBS-L-1.ovl','YeManOBS-JJ-1.ovl','Empty.ovl')) {
    $target = Join-Path $overlays $name
    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
        $asset = Join-Path $PSScriptRoot ('RTSS-Overlays\' + $name)
        if (-not (Test-Path -LiteralPath $asset -PathType Leaf)) { throw "RTSS template is missing: $name" }
        $temp = $target + ".ymcc-start-$PID.tmp"
        Copy-Item -LiteralPath $asset -Destination $temp -Force
        Move-Item -LiteralPath $temp -Destination $target -Force
    }
}
# Use the same section-scoped INI API as RTSS. Preserve all other config,
# selected Layout and user hotkeys; no full-file truncation/rewrite.
New-Item -ItemType Directory -Path (Join-Path $rtss.Dir 'Profiles') -Force | Out-Null
$config = Join-Path $rtss.Dir 'Profiles\Config'
foreach ($plugin in @('OverlayEditor.dll','YMCCOverlayBridge.dll')) {
    if (-not [YMCC.RtssIni]::WritePrivateProfileString('Plugins', $plugin, '1', $config)) {
        throw "RTSS plugin enable failed: $plugin"
    }
}

if ($InstallOnly) { return }

if (-not (Get-Process -Name RTSS -ErrorAction SilentlyContinue)) {
    Start-Process -FilePath $rtss.Exe -WorkingDirectory $rtss.Dir -WindowStyle Hidden | Out-Null
}
for ($i = 0; $i -lt 80; $i++) {
    if (Get-Process -Name RTSS -ErrorAction SilentlyContinue) { exit 0 }
    Start-Sleep -Milliseconds 100
}
throw 'RTSS.exe did not stay running after launch'
