[CmdletBinding(SupportsShouldProcess=$true)]
param([ValidateSet('Inspect','Enable','Restore')][string]$Action='Inspect')
$ErrorActionPreference='Stop'
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath
. (Join-Path $PSScriptRoot 'state.ps1')
$packageName='YeManCC.GamingHome'
$taskName='YeManCC-GamingHome'
$ymcc='C:\SOFT\YeMan\YeManCC\YeManCC.exe'
$bat='C:\SOFT\YeMan\PowerControl\YeManSteam2.bat'
$outer='C:\SOFT\YeMan\PowerControl\YeManSteam.bat'
$key='HKCU:\Software\Microsoft\Windows\CurrentVersion\GamingConfiguration'
$sid=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$developerMode=[int](Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock' -Name AllowDevelopmentWithoutDevLicense -ErrorAction SilentlyContinue).AllowDevelopmentWithoutDevLicense -eq 1
$stateRoot=Join-Path $env:LOCALAPPDATA 'YeManCC\gaming-home'
$statePath=Join-Path $stateRoot 'registration-state.json'
function Read-Value([string]$Name) {
  $k=Get-Item -LiteralPath $key -ErrorAction SilentlyContinue
  if (-not $k -or $k.GetValueNames() -notcontains $Name) { return [pscustomobject]@{exists=$false;kind=$null;value=$null} }
  [pscustomobject]@{exists=$true;kind=[string]$k.GetValueKind($Name);value=$k.GetValue($Name)}
}
function Write-Value([string]$Name,$Value) {
  if ($Value.exists) {
    if (-not (Test-Path -LiteralPath $key)) { New-Item -Path $key -Force | Out-Null }
    New-ItemProperty -LiteralPath $key -Name $Name -Value $Value.value -PropertyType $Value.kind -Force | Out-Null
  } else { Remove-ItemProperty -LiteralPath $key -Name $Name -ErrorAction SilentlyContinue }
}
function Save-State($State) {
  New-Item -ItemType Directory -Path $stateRoot -Force | Out-Null
  $temp=Join-Path $stateRoot ('registration-state-'+[guid]::NewGuid().ToString('N')+'.tmp')
  $State | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $temp -Encoding UTF8
  if (Test-Path -LiteralPath $statePath -PathType Leaf) { [IO.File]::Replace($temp,$statePath,$null) } else { [IO.File]::Move($temp,$statePath) }
}
function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Test-GamingHomeAdministrator {
  ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
function Grant-GamingHomeTaskAccess {
  $service=New-Object -ComObject 'Schedule.Service';$service.Connect()
  $registered=$service.GetFolder('\').GetTask($taskName)
  $registered.SetSecurityDescriptor(('D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;GRGX;;;'+$sid+')'),0)
}
function Invoke-GamingHomeSetup {
$package=Get-AppxPackage -Name $packageName
$task=Get-ScheduledTask -TaskPath '\' -TaskName $taskName -ErrorAction SilentlyContinue
$currentHome=Read-Value 'GamingHomeApp';$currentStartup=Read-Value 'StartupToGamingHome'
if ($Action -eq 'Inspect') {
  [ordered]@{action='inspect';developerMode=$developerMode;home=$currentHome;startup=$currentStartup;packageInstalled=([bool]$package);taskOwned=(Test-GamingHomeTask $task $ymcc $sid);ymccExists=(Test-Path -LiteralPath $ymcc -PathType Leaf);batExists=(Test-Path -LiteralPath $outer -PathType Leaf);stateExists=(Test-Path -LiteralPath $statePath);applicationsStarted=$false} | ConvertTo-Json -Depth 6
  return
}
$admin=Test-GamingHomeAdministrator
if (-not $admin) { throw '请右键以管理员身份运行本目录的启用或恢复 CMD。' }
$state=$null
if (Test-Path -LiteralPath $statePath) {
  $state=Get-Content -LiteralPath $statePath -Raw -Encoding UTF8 | ConvertFrom-Json
  if ($state.schema -ne 2 -or $state.userSid -ne $sid -or $state.packageName -ne $packageName) { throw 'Existing registration state belongs to another identity.' }
}
if ($Action -eq 'Restore') {
  if (-not $state) { throw 'No saved YMCC home registration state.' }
  if ((Test-Path -LiteralPath $bat) -and (Hash $bat) -eq $state.guardedBatSha256 -and (-not (Test-Path -LiteralPath $state.batBackup) -or (Hash $state.batBackup) -ne $state.originalBatSha256)) { throw 'Original BAT backup mismatch; preserved all settings.' }
  if (-not $PSCmdlet.ShouldProcess('YMCC Gaming Home','Restore previous home and original BAT; do not stop applications')) { return }
  $plan=Restore-GamingHomeSelection $state ${function:Read-Value} ${function:Write-Value}
  $batRestored=$false
  if ((Test-Path -LiteralPath $bat) -and (Hash $bat) -eq $state.guardedBatSha256) {
    if (-not (Test-Path -LiteralPath $state.batBackup) -or (Hash $state.batBackup) -ne $state.originalBatSha256) { throw 'Original BAT backup mismatch; refusing to overwrite.' }
    Copy-Item -LiteralPath $state.batBackup -Destination $bat -Force;$batRestored=$true
  }
  # Keep the registered package, certificate and demand task inert for reuse.
  # Removing an AppX package can close a running application; restore never does.
  $state.phase='restored';Save-State $state
  [ordered]@{action='restored';homeRestored=$plan.homeRestored;startupRestored=$plan.startupRestored;batRestored=$batRestored;applicationsStopped=$false;packageRetained=$true;taskRetained=$true} | ConvertTo-Json
  return
}
# Preflight every source, existing task and BAT before importing or selecting.
$payload=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'payload.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($payload.schema -ne 1 -or $payload.component -ne 'YeManCC.GamingHome') { throw 'Invalid home payload manifest.' }
$required=@('YMCC-GamingHome.msix','YMCC-GamingHome.cer','YeManSteam2.bat','Manage-GamingHome.ps1','state.ps1','Enable-YMCC-Home.cmd','Restore-Previous-Home.cmd','Inspect-YMCC-Home.cmd','TESTING.md')
if (@($payload.files).Count -ne $required.Count -or @($payload.files.name | Sort-Object -Unique).Count -ne $required.Count -or @(Compare-Object ($required | Sort-Object) ($payload.files.name | Sort-Object)).Count -ne 0) { throw 'Incomplete home payload manifest.' }
foreach ($f in $payload.files) {
  if ($f.name -notmatch '^[a-zA-Z0-9_.-]+$') { throw 'Invalid payload entry.' }
  $path=Join-Path $PSScriptRoot $f.name
  if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Hash $path) -ne $f.sha256) { throw ('Payload mismatch: '+$f.name) }
}
foreach ($p in @($ymcc,$outer,$bat,'C:\SOFT\NirCmd\nircmd.exe')) { if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { throw ('Missing startup dependency: '+$p) } }
if ($task -and -not (Test-GamingHomeTask $task $ymcc $sid)) { throw 'Existing YeManCC-GamingHome task has a different owner or action; preserved.' }
if ($currentHome.exists -and $currentHome.kind -ne 'String') { throw 'Unexpected GamingHomeApp type; preserved.' }
if ($currentStartup.exists -and $currentStartup.kind -ne 'DWord') { throw 'Unexpected StartupToGamingHome type; preserved.' }
$batSha=Hash $bat
if ($batSha -ne $payload.compatibleBatSha256 -and $batSha -ne $payload.guardedBatSha256) { throw 'YeManSteam2.bat contains custom changes. It was preserved; merge the supplied YMCC already-running guard first.' }
$certFile=Join-Path $PSScriptRoot 'YMCC-GamingHome.cer'
$cert=[Security.Cryptography.X509Certificates.X509Certificate2]::new($certFile)
if ($cert.Subject -ne 'CN=YeManCC Gaming Home' -or $cert.Thumbprint -ne $payload.certificateThumbprint) { throw 'Unexpected home package certificate.' }
if ($package -and $package.Publisher -ne $cert.Subject) { throw 'Existing package publisher mismatch.' }
$alreadyCurrent=$package -and [string]$package.Version -eq [string]$payload.packageVersion -and (Test-Path -LiteralPath (Join-Path $package.InstallLocation 'YeManGamingHome.exe')) -and (Hash (Join-Path $package.InstallLocation 'YeManGamingHome.exe')) -eq $payload.exeSha256
if ($package -and [version]$package.Version -ge [version]$payload.packageVersion -and -not $alreadyCurrent) { throw 'A different or newer home package is installed; preserved.' }
if (-not $developerMode) { throw '此测试包使用开发用 gamingHome 能力描述。请先在 Windows 设置中手动开启开发者模式，再运行启用入口；本工具不会代改该设置。' }
if ($state -and $state.phase -in @('failed','committing','prepared')) { throw 'Previous setup is incomplete. Run Restore-Previous-Home.cmd before enabling again.' }
if (-not $PSCmdlet.ShouldProcess('Current Windows user','Install YMCC home package, demand task and select it for fullscreen startup')) { return }
if (-not $state -or $state.phase -eq 'restored' -or ($state.phase -eq 'enabled' -and $currentHome.value -cne $state.aumid)) {
  New-Item -ItemType Directory -Path $stateRoot -Force | Out-Null
  $backup=Join-Path $stateRoot ('YeManSteam2-original-'+[guid]::NewGuid().ToString('N')+'.bat')
  Copy-Item -LiteralPath $bat -Destination $backup
  $state=[pscustomobject]@{schema=2;userSid=$sid;packageName=$packageName;phase='prepared';saved=[pscustomobject]@{home=$currentHome;startup=$currentStartup};before=[pscustomobject]@{home=$currentHome;startup=$currentStartup};writes=[pscustomobject]@{home=$false;startup=$false};attempt=[pscustomobject]@{home=$false;startup=$false};aumid='';batBackup=$backup;originalBatSha256=$batSha;guardedBatSha256=$payload.guardedBatSha256;certificateThumbprint=$cert.Thumbprint}
  Save-State $state
}
$state.attempt=[pscustomobject]@{home=$false;startup=$false}
try {
  $trustedPath='Cert:\LocalMachine\TrustedPeople\'+$cert.Thumbprint
  if (-not (Test-Path -LiteralPath $trustedPath)) { Import-Certificate -FilePath $certFile -CertStoreLocation 'Cert:\LocalMachine\TrustedPeople' | Out-Null }
  $sig=Get-AuthenticodeSignature -LiteralPath (Join-Path $PSScriptRoot 'YMCC-GamingHome.msix');if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Thumbprint -ne $cert.Thumbprint) { throw 'Home package signature verification failed.' }
  if (-not $alreadyCurrent) { Add-AppxPackage -Path (Join-Path $PSScriptRoot 'YMCC-GamingHome.msix') }
  $package=Get-AppxPackage -Name $packageName
  if (-not $package -or $package.Publisher -ne $cert.Subject) { throw 'Installed home package could not be verified.' }
  $manifest=Get-AppxPackageManifest -Package $package.PackageFullName
  $extension=@($manifest.Package.Applications.Application.Extensions.Extension.AppExtension | Where-Object {$_.Name -eq 'windows.gamingApp'})
  if ($extension.Count -ne 1) { throw 'Gaming home extension was not registered.' }
  $state.aumid=$package.PackageFamilyName+'!Home';$state.before=[pscustomobject]@{home=$currentHome;startup=$currentStartup};Save-State $state
  if (-not $task) {
    $actionDef=New-ScheduledTaskAction -Execute $ymcc -Argument '--minimized' -WorkingDirectory 'C:\SOFT\YeMan\YeManCC'
    $principal=New-ScheduledTaskPrincipal -UserId $sid -LogonType Interactive -RunLevel Highest
    $settings=New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -MultipleInstances IgnoreNew -ExecutionTimeLimit ([TimeSpan]::Zero)
    Register-ScheduledTask -TaskPath '\' -TaskName $taskName -Description 'YMCC Gaming Home demand start v1' -Action $actionDef -Principal $principal -Settings $settings | Out-Null
  }
  if (-not (Test-GamingHomeTask (Get-ScheduledTask -TaskPath '\' -TaskName $taskName) $ymcc $sid)) { throw 'Demand task verification failed.' }
  # Complete/repair the ACL also after an earlier interrupted registration.
  Grant-GamingHomeTaskAccess
  if ((Hash $bat) -ne $batSha) { throw 'BAT changed during setup; preserved.' }
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'YeManSteam2.bat') -Destination $bat -Force
  if ((Hash $bat) -ne $payload.guardedBatSha256) { throw 'BAT copy verification failed.' }
  # Selection is the final commit. .earlystart and the outer BAT are untouched.
  Invoke-GamingHomeSelection $state ${function:Read-Value} ${function:Write-Value} ${function:Save-State}
  [ordered]@{action='enabled';home=$state.aumid;applicationsStarted=$false;earlystartChanged=$false;taskTriggers=0;log='普通 YMCC 导出中的 steam-launch.log';next='下一次由你进入 Windows 全屏环境或重新登录后测试。'} | ConvertTo-Json
} catch {
  # Roll back only our just-written selection; keep verified inert components
  # and the original backup for the explicit Restore entry.
  Restore-GamingHomeSelection $state ${function:Read-Value} ${function:Write-Value} -RollbackOnly | Out-Null
  if ((Test-Path -LiteralPath $bat) -and (Hash $bat) -eq $state.guardedBatSha256 -and (Hash $state.batBackup) -eq $state.originalBatSha256) { Copy-Item -LiteralPath $state.batBackup -Destination $bat -Force }
  $state.phase='failed';Save-State $state
  throw
}

}
$setupMutex=New-Object Threading.Mutex($false,('Local\YeManCC.GamingHome.Setup.'+$sid))
$ownsSetup=$false
try {
  try { $ownsSetup=$setupMutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $ownsSetup=$true }
  if (-not $ownsSetup) { throw 'Another YMCC home setup is active.' }
  Invoke-GamingHomeSetup
} finally { if ($ownsSetup) { $setupMutex.ReleaseMutex() };$setupMutex.Dispose() }
