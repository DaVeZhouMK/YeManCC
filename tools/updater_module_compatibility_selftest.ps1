[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir,[Parameter(Mandatory=$true)][string]$ProcessFixture,[int]$Cycles=12,[string]$ModuleConsumer="",[string]$SetupExecutable="")
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$evidence=[IO.Path]::GetFullPath($EvidenceDir)
[IO.Directory]::CreateDirectory($evidence) | Out-Null
. (Join-Path $repo 'native/decky_package_maintenance.ps1')
. (Join-Path $repo 'native/module_package_maintenance.ps1')
. (Join-Path $repo 'tools/module_payload_packaging.ps1')
$results=New-Object 'System.Collections.Generic.List[object]'
function Need([bool]$Condition,[string]$Message) { if (!$Condition) { throw $Message } }
function Put([string]$moduleFixtureRoot,[string]$Relative,[string]$Text) {
  $file=Join-Path $moduleFixtureRoot $Relative
  [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($file)) | Out-Null
  [IO.File]::WriteAllText($file,$Text,[Text.UTF8Encoding]::new($false)); return $file
}
function Case([string]$Name,[scriptblock]$Body) {
  $timer=[Diagnostics.Stopwatch]::StartNew()
  try { & $Body; $results.Add([pscustomobject]@{name=$Name;status='PASS';milliseconds=$timer.ElapsedMilliseconds}) }
  catch { $results.Add([pscustomobject]@{name=$Name;status='FAIL';error=$_.Exception.Message;milliseconds=$timer.ElapsedMilliseconds}) }
}
function New-ModuleCase([string]$Name) {
  $moduleFixtureRoot=Join-Path $evidence ($Name+'-'+[guid]::NewGuid().ToString('N'))
  $source=Join-Path $moduleFixtureRoot 'source';$target=Join-Path $moduleFixtureRoot 'target'
  [IO.Directory]::CreateDirectory($target) | Out-Null
  foreach ($name in @('YeManInputHost.exe','YeManInputHost.dll','YeManInputHost.deps.json','YeManInputHost.runtimeconfig.json','HIDMaestro.Core.dll')) {
    Put $source ('feature-assets/virtual-gamepad/'+$name) ('036-'+$name) | Out-Null
    Put $target ('feature-assets/virtual-gamepad/'+$name) ('035-'+$name) | Out-Null
  }
  Put $source 'feature-assets/gyro-motion/runtime.json' '{"version":36}' | Out-Null
  Put $target 'feature-assets/gyro-motion/runtime.json' '{"version":35}' | Out-Null
  return [pscustomobject]@{home=$moduleFixtureRoot;source=$source;target=$target;manifest=(New-YmccModuleManifest $source '0.0.36')}
}
for ($cycle=0;$cycle -lt $Cycles;$cycle++) {
  Case ('module-upgrade-rollback-retry-'+$cycle) {
    $c=New-ModuleCase 'cycle';$data=Put $c.target 'feature-assets/virtual-gamepad/user-data/player.json' '{"preserve":true}'
    $plan=Get-YmccModulePlan $c.source $c.target $c.manifest '0.0.36'
    $t=Install-YmccModulePackage $plan (Join-Path $c.home 'backup')
    Need ((Get-Content -LiteralPath (Join-Path $c.target 'feature-assets/gyro-motion/runtime.json') -Raw) -eq '{"version":36}') 'new module was skipped'
    Restore-YmccModulePackage $t
    Need ((Get-Content -LiteralPath (Join-Path $c.target 'feature-assets/gyro-motion/runtime.json') -Raw) -eq '{"version":35}') 'rollback failed'
    $retry=Get-YmccModulePlan $c.source $c.target $c.manifest '0.0.36'
    $null=Install-YmccModulePackage $retry (Join-Path $c.home 'retry')
    Need (@((Get-YmccModulePlan $c.source $c.target $c.manifest '0.0.36').entries).Count -eq 0) 'same version rewrites modules'
    Need ([IO.File]::ReadAllText($data) -eq '{"preserve":true}') 'user module configuration changed'
  }
}
Case 'external manual installation wins before planned commit' {
  $c=New-ModuleCase 'external';$plan=Get-YmccModulePlan $c.source $c.target $c.manifest '0.0.36'
  $file=Put $c.target 'feature-assets/virtual-gamepad/YeManInputHost.dll' 'external-newer'
  $refused=$false; try { $null=Install-YmccModulePackage $plan (Join-Path $c.home 'backup') } catch { $refused=$true }
  Need $refused 'concurrent manual install was overwritten'; Need ([IO.File]::ReadAllText($file) -eq 'external-newer') 'external file changed'
}
Case 'rollback does not overwrite a later autonomous module update' {
  $c=New-ModuleCase 'rollback-external';$p=Get-YmccModulePlan $c.source $c.target $c.manifest '0.0.36';$t=Install-YmccModulePackage $p (Join-Path $c.home 'backup')
  $file=Put $c.target 'feature-assets/virtual-gamepad/YeManInputHost.dll' 'external-newer'
  $refused=$false; try { Restore-YmccModulePackage $t } catch { $refused=$true }
  Need $refused 'rollback overwrote concurrent owner'; Need ([IO.File]::ReadAllText($file) -eq 'external-newer') 'external file lost'
}
Case 'corrupt incoming module refuses before first target mutation' {
  $c=New-ModuleCase 'corrupt';Put $c.source 'feature-assets/virtual-gamepad/YeManInputHost.dll' 'damaged' | Out-Null
  $refused=$false;try { $null=Get-YmccModulePlan $c.source $c.target $c.manifest '0.0.36' } catch { $refused=$true }
  Need $refused 'corrupt source accepted';Need ([IO.File]::ReadAllText((Join-Path $c.target 'feature-assets/virtual-gamepad/YeManInputHost.dll')) -eq '035-YeManInputHost.dll') 'target mutated on validation failure'
}
Case 'locked module fails promptly and retry succeeds after release' {
  $c=New-ModuleCase 'locked';$file=Join-Path $c.target 'feature-assets/virtual-gamepad/YeManInputHost.dll'
  $stream=[IO.File]::Open($file,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
  $refused=$false;try { $null=Get-YmccModulePlan $c.source $c.target $c.manifest '0.0.36' } catch { $refused=$true } finally { $stream.Dispose() }
  Need $refused 'locked file accepted';$null=Install-YmccModulePackage (Get-YmccModulePlan $c.source $c.target $c.manifest '0.0.36') (Join-Path $c.home 'retry')
}
foreach ($mode in @('normal','hung')) { for ($round=0;$round -lt 3;$round++) {
  Case ('running-YMCC-'+$mode+'-'+$round) {
    $process=Start-Process -FilePath $ProcessFixture -ArgumentList $mode -PassThru -WindowStyle Hidden
    try {
      Start-Sleep -Milliseconds 150
      $created=$process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()
      $lease=Enter-YmccModuleMaintenance
      try { $result=Stop-YmccUpdaterParent $process.Id $ProcessFixture $created 300 2000 } finally { Exit-YmccModuleMaintenance $lease }
      Need ($result -eq $(if($mode -eq 'hung'){'forced-owned-parent'}else{'graceful'})) ('unexpected close result '+$result)
      Need $process.HasExited 'outgoing process remained running'
    } finally { if (!$process.HasExited) { $process.Kill();$null=$process.WaitForExit(2000) };$process.Dispose() }
  }
} }
Case 'wrong path and reused process identity are never killed' {
  $p=Start-Process -FilePath $ProcessFixture -ArgumentList hung -PassThru -WindowStyle Hidden
  try { Start-Sleep -Milliseconds 100;foreach ($identity in @('0',$p.StartTime.ToUniversalTime().ToFileTimeUtc().ToString())) {
    $refused=$false;try { $null=Stop-YmccUpdaterParent $p.Id $(if($identity -eq '0'){$ProcessFixture}else{$ProcessFixture+'.foreign'}) $identity 0 100 } catch { $refused=$true }
    Need $refused 'unverified PID accepted';Need (!$p.HasExited) 'foreign process was killed'
  } } finally { $p.Kill();$null=$p.WaitForExit(2000);$p.Dispose() }
}
Case 'cross-process C# module owner blocks PowerShell install and recovers after release' {
  $consumer=if($ModuleConsumer){$ModuleConsumer}else{Join-Path $evidence '..\ModuleConsumer\bin\ModuleConsumer.dll'}
  if (!(Test-Path -LiteralPath $consumer)) { throw 'MODULE_CONSUMER_REQUIRED_FOR_CROSS_PROCESS_TEST' }
  $signal=Join-Path $evidence ('lease-ready-'+[guid]::NewGuid().ToString('N'))
  $release=$signal+'.release'
  $info=[Diagnostics.ProcessStartInfo]::new('dotnet');$info.UseShellExecute=$false;$info.CreateNoWindow=$true
  foreach($arg in @($consumer,'lease',$signal,$release)){$info.ArgumentList.Add($arg)}
  $holder=[Diagnostics.Process]::Start($info)
  try {
    $timer=[Diagnostics.Stopwatch]::StartNew();while(!(Test-Path -LiteralPath $signal) -and $timer.ElapsedMilliseconds -lt 3000){Start-Sleep -Milliseconds 25}
    Need (Test-Path -LiteralPath $signal) 'C# holder did not acquire maintenance lease'
    $refused=$false;$timer.Restart()
    try { $unexpected=Enter-YmccModuleMaintenance;Exit-YmccModuleMaintenance $unexpected } catch { $refused=$true }
    Need $refused 'PowerShell crossed the C# mutation lease';Need ($timer.ElapsedMilliseconds -lt 1000) 'busy gate blocked instead of deferring'
    # The real prepare executable must exit BEFORE inventories/SDK constructors
    # when another chain owns modules. All output paths point into this fixture.
    $setup=if($SetupExecutable){$SetupExecutable}else{Join-Path $evidence '..\Build\GamepadPrerequisites\HIDMaestroSetup.exe'}
    $root=Join-Path $evidence 'busy-install';[IO.Directory]::CreateDirectory($root)|Out-Null
    $report=Join-Path $root 'report.json'
    & $setup --prepare --install-root $root --report $report | Out-Null
    Need ($LASTEXITCODE -eq 19) 'actual setup did not return maintenance-deferred'
    $record=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    Need ($record.status -eq 'DRIVER_MAINTENANCE_DEFERRED') 'setup deferred status missing'
    Need ($null -eq $record.usbipUpgrade) 'setup entered device inventory while another owner was active'
  } finally {
    [IO.File]::WriteAllText($release,'release')
    if(!$holder.WaitForExit(3000)){if(!$holder.HasExited){$holder.Kill()};throw 'LEASE_HOLDER_DID_NOT_EXIT'}
    $holder.Dispose()
  }
  $after=Enter-YmccModuleMaintenance;Exit-YmccModuleMaintenance $after
}
$failures=@($results | Where-Object status -eq FAIL)
[pscustomobject]@{status=$(if($failures.Count){'FAIL'}else{'PASS'});cycles=$Cycles;cases=$results;hardwareIo=$false;realInstalledYMCCStarted=$false;processConsumer='production Stop-YmccUpdaterParent with inert Win32 message-loop/hung processes'} | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $evidence 'result.json') -Encoding UTF8
$results | Format-Table -AutoSize
if ($failures.Count) { throw ('MODULE_COMPATIBILITY_FAILED '+$failures.Count) }
