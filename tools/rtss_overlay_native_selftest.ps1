[CmdletBinding()]
param([string]$WorkspaceRoot = $env:YEMAN_WORKSPACE_ROOT)
$ErrorActionPreference='Stop'

# Windows PowerShell launched by pnpm/pwsh must resolve its own built-in modules first.
$ownModules = Join-Path $PSHOME 'Modules'
if (($env:PSModulePath -split ';')[0].TrimEnd('\') -ine $ownModules.TrimEnd('\')) {
  $env:PSModulePath = $ownModules + ';' + $env:PSModulePath
}
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $WorkspaceRoot){$WorkspaceRoot=[IO.Path]::GetFullPath((Join-Path $project '..\..'))}
$out=Join-Path $WorkspaceRoot 'Build\Validation\RTSS'
$hostDir=Join-Path $out 'host'
$products=Join-Path $WorkspaceRoot 'Build\App\Native'
$oldRoot=$env:YEMAN_WORKSPACE_ROOT
$oldCwd=Get-Location
$mock=$null
try {
 $env:YEMAN_WORKSPACE_ROOT=$WorkspaceRoot
 Set-Location -LiteralPath $project
 & cmd.exe /d /c (Join-Path $project 'native\rtss\build_bridge.bat')
 if($LASTEXITCODE -ne 0){throw 'RTSS plugin compile failed'}
 & cmd.exe /d /c ('"' + (Join-Path $project 'tools\rtss_probe\build.bat') + '" "' + $out + '"')
 if($LASTEXITCODE -ne 0){throw 'RTSS probe compile failed'}
 if(-not(Test-Path -LiteralPath (Join-Path $products 'YMCCRtssProfileHelper.exe'))){throw 'Build native first (profile helper missing)'}
 & (Join-Path $project 'tools\rtss_overlay_install_selftest.ps1') -WorkspaceRoot $WorkspaceRoot
 Copy-Item -LiteralPath (Join-Path $products 'YMCCOverlayBridge.dll') -Destination (Join-Path $hostDir 'Plugins\Client\YMCCOverlayBridge.dll') -Force
 Copy-Item -LiteralPath (Join-Path $products 'YMCCOverlayBridge.dll'),(Join-Path $products 'YMCCRtssProfileHelper.exe') -Destination $out -Force
 New-Item -ItemType Directory -Path (Join-Path $hostDir 'Plugins\Client\Overlays') -Force | Out-Null
 foreach($name in @('YeManOBS-W-1.ovl','YeManOBS-L-1.ovl','YeManOBS-JJ-1.ovl','Empty.ovl')) {
  Copy-Item -LiteralPath (Join-Path $project ('PowerControl\RTSS-Overlays\'+$name)) -Destination (Join-Path $hostDir ('Plugins\Client\Overlays\'+$name)) -Force
 }
 Set-Content -LiteralPath (Join-Path $hostDir 'Profiles\Global') -Value "[OSD]`r`nEnableBgnd=1`r`nZoomRatio=2`r`n[Framerate]`r`nLimit=120`r`n" -Encoding ascii
 Set-Content -LiteralPath (Join-Path $hostDir 'Plugins\Client\YMCCOverlayBridge.cfg') -Value '' -Encoding ascii
 Set-Content -LiteralPath (Join-Path $hostDir 'Plugins\Client\mock-state.ini') -Value '' -Encoding ascii
 $mock=Start-Process -FilePath (Join-Path $hostDir 'RTSS.exe') -WorkingDirectory $hostDir -WindowStyle Hidden -PassThru
 Start-Sleep -Milliseconds 500
 if($mock.HasExited){throw "Mock RTSS host exited: $($mock.ExitCode)"}
 Set-Location -LiteralPath $hostDir
 & (Join-Path $out 'client_test.exe') $hostDir (Join-Path $project 'PowerControl') *> (Join-Path $out 'cross-process-results.log')
 $code=$LASTEXITCODE
 Get-Content -LiteralPath (Join-Path $out 'cross-process-results.log') -Tail 20
 if($code -ne 0){throw "Native RTSS selftest failed ($code)"}
} finally {
 if($mock){
  try {
   $event=[Threading.EventWaitHandle]::OpenExisting("Local\YMCC.RtssProbe.$($mock.Id)")
   $event.Set() | Out-Null; $event.Dispose()
   $mock.WaitForExit(5000) | Out-Null
  } catch { Write-Warning $_.Exception.Message }
  # PID created by THIS test only; never terminate the real RTSS process family.
  if(-not $mock.HasExited){Stop-Process -Id $mock.Id -Force}
 }
 Set-Location -LiteralPath $oldCwd
 $env:YEMAN_WORKSPACE_ROOT=$oldRoot
}
