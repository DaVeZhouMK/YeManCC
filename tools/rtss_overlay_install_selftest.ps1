[CmdletBinding()]
param([string]$WorkspaceRoot = $env:YEMAN_WORKSPACE_ROOT)
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $WorkspaceRoot) { $WorkspaceRoot = [IO.Path]::GetFullPath((Join-Path $project '..\..')) }
$source = Join-Path $WorkspaceRoot 'Build\App\Native\YMCCOverlayBridge.dll'
if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw 'Build the RTSS bridge before running the installation test' }
$out = Join-Path $WorkspaceRoot ('Build\Validation\RTSS\startup-install-' + [Guid]::NewGuid().ToString('N'))
$rtss = Join-Path $out 'RTSS'
$client = Join-Path $rtss 'Plugins\Client'
$program = Join-Path $out 'YeManCC'
$powerControl = Join-Path $out 'PowerControl'
New-Item -ItemType Directory -Path $client,$program,$powerControl -Force | Out-Null
Copy-Item -LiteralPath $source -Destination (Join-Path $program 'YMCCOverlayBridge.dll')
Copy-Item -LiteralPath (Join-Path $project 'PowerControl\RTSS-start.ps1') -Destination $powerControl
Copy-Item -LiteralPath (Join-Path $project 'PowerControl\RTSS-Overlays') -Destination $powerControl -Recurse
# These are file markers, NOT executables/DLLs. InstallOnly must never launch
# them, use the installed RTSS, discover processes, or load third-party code.
foreach ($name in @('RTSS.exe','RTSSHooks64.dll','Plugins\Client\OverlayEditor.dll')) {
    Set-Content -LiteralPath (Join-Path $rtss $name) -Value 'private installation fixture, never executed' -Encoding ascii
}
function Require([bool]$Condition, [string]$Label) {
    if (-not $Condition) { throw "FAIL $Label" }
    Write-Output "PASS $Label"
}
function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function Prepare {
    & (Join-Path $powerControl 'RTSS-start.ps1') -RtssDirectory $rtss -InstallOnly
}
$bridge = Join-Path $client 'YMCCOverlayBridge.dll'
$config = Join-Path $rtss 'Profiles\Config'
Require (-not (Test-Path -LiteralPath $bridge)) 'boot-script fixture begins without deployed bridge'
Prepare
Require ((Test-Path -LiteralPath $bridge -PathType Leaf) -and (Hash $bridge) -eq (Hash $source)) 'boot script installs missing bridge from retained program file'
Require ((Hash (Join-Path $program 'YMCCOverlayBridge.dll')) -eq (Hash $source)) 'boot script retains original DLL beside YMCC'
$configText = Get-Content -LiteralPath $config -Raw
Require ($configText -match '(?m)^YMCCOverlayBridge\.dll=1\r?$' -and $configText -match '(?m)^OverlayEditor\.dll=1\r?$') 'boot script creates config and enables both plugins'
foreach ($name in @('YeManOBS-W-1.ovl','YeManOBS-L-1.ovl','YeManOBS-JJ-1.ovl','Empty.ovl')) {
    Require ((Hash (Join-Path $client ('Overlays\' + $name))) -eq (Hash (Join-Path $powerControl ('RTSS-Overlays\' + $name)))) "boot script supplies complete template $name"
}
$custom = Join-Path $client 'Overlays\YeManOBS-W-1.ovl'
$editor = Join-Path $client 'OverlayEditor.cfg'
$global = Join-Path $rtss 'Profiles\Global'
Set-Content -LiteralPath $custom -Value "[General]`r`nLayers=1`r`n[Layer0]`r`nText=user-custom" -Encoding ascii
Set-Content -LiteralPath $editor -Value "[Settings]`r`nLayout=user-custom.ovl`r`nSMART=0" -Encoding ascii
Set-Content -LiteralPath $global -Value "[Framerate]`r`nLimit=144`r`n[OSD]`r`nZoomRatio=3" -Encoding ascii
Set-Content -LiteralPath $config -Value "[Plugins]`r`nHotkeyHandler.dll=0`r`nOverlayEditor.dll=0`r`nYMCCOverlayBridge.dll=0`r`n[Other]`r`nKeep=7" -Encoding ascii
$before = @{}
foreach ($path in @($custom,$editor,$global)) { $before[$path] = Hash $path }
$bridgeTime = (Get-Item -LiteralPath $bridge).LastWriteTimeUtc
Prepare
Require ((Get-Item -LiteralPath $bridge).LastWriteTimeUtc -eq $bridgeTime) 'repeat boot script does not replace an installed bridge'
foreach ($path in @($custom,$editor,$global)) { Require ((Hash $path) -eq $before[$path]) 'repeat boot script preserves custom template Layout FPS zoom and editor config' }
$configText = Get-Content -LiteralPath $config -Raw
Require ($configText -match '(?m)^HotkeyHandler\.dll=0\r?$' -and $configText -match '(?m)^Keep=7\r?$') 'boot script preserves unrelated INI settings'
Require ($configText -match '(?m)^YMCCOverlayBridge\.dll=1\r?$' -and $configText -match '(?m)^OverlayEditor\.dll=1\r?$') 'boot script re-enables required plugins'
# Single-file deletion only, within a checked private fixture; real RTSS is
# never touched and the test fixtures are retained for diagnostics.
$resolvedBridge = [IO.Path]::GetFullPath($bridge)
$resolvedFixture = [IO.Path]::GetFullPath($out).TrimEnd('\') + '\'
if (-not $resolvedBridge.StartsWith($resolvedFixture, [StringComparison]::OrdinalIgnoreCase)) { throw 'Refuse deletion outside the test fixture' }
Remove-Item -LiteralPath $resolvedBridge
Prepare
Require ((Hash $bridge) -eq (Hash $source)) 'boot script repairs deleted DLL on next startup'
Set-Content -LiteralPath $bridge -Value 'old offline bridge version' -Encoding ascii
Prepare
Require ((Hash $bridge) -eq (Hash $source)) 'boot script updates old bridge before normal startup'
$sourceBackup = [IO.File]::ReadAllBytes((Join-Path $program 'YMCCOverlayBridge.dll'))
[IO.File]::WriteAllBytes((Join-Path $program 'YMCCOverlayBridge.dll'), ($sourceBackup + [byte[]](1,2,3)))
$installedBeforeLock = Hash $bridge
$locked = [IO.File]::Open($bridge, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
try { Prepare } finally { $locked.Dispose() }
Require ((Hash $bridge) -eq $installedBeforeLock) 'locked existing DLL update is deferred without deleting usable bridge'
Prepare
Require ((Hash $bridge) -eq (Hash (Join-Path $program 'YMCCOverlayBridge.dll'))) 'deferred update succeeds when lock is released'
[IO.File]::WriteAllBytes((Join-Path $program 'YMCCOverlayBridge.dll'), $sourceBackup)
Prepare
$scriptText = Get-Content -LiteralPath (Join-Path $powerControl 'RTSS-start.ps1') -Raw
Require ($scriptText.IndexOf('WritePrivateProfileString(') -lt $scriptText.IndexOf('Start-Process -FilePath $rtss.Exe')) 'installation and enabling precede first normal RTSS launch'
Require ($scriptText -notmatch 'Stop-Process|taskkill|LoadLibrary|CreateRemoteThread') 'boot script never terminates processes or injects DLLs'
Write-Output 'RTSS_INSTALL_SELFTEST_OK (clean boot, DLL retention, enabled config, templates, repeat boot, deletion repair, offline update, locked update deferral, settings preservation, no restart)'