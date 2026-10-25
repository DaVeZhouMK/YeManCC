[CmdletBinding(SupportsShouldProcess=$true)]
param([Parameter(Mandatory=$true)][string]$Delivery,[string]$SourceRoot=$PSScriptRoot)
$ErrorActionPreference='Stop'
. (Join-Path $SourceRoot 'state.ps1')
$script:fixtureDelivery=$Delivery
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $SourceRoot 'Manage-GamingHome.ps1'),[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw 'Setup source parse failed.' }
# Use the actual setup function. Only its package source folder is rebound;
# all OS/filesystem effects below are explicit memory terminals.
$fn=$ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Invoke-GamingHomeSetup'},$false)[0]
. ([scriptblock]::Create($fn.Extent.Text.Replace('$PSScriptRoot','$script:fixtureDelivery')))
$payload=Microsoft.PowerShell.Management\Get-Content -LiteralPath (Join-Path $Delivery 'payload.json') -Raw | ConvertFrom-Json
$script:checks=0
function Assert($Ok,$Text) { $script:checks++;if(-not $Ok){throw $Text} }
function Clone($Object){$Object|ConvertTo-Json -Depth 12|ConvertFrom-Json}
function V($Exists,$Kind=$null,$Value=$null){[pscustomobject]@{exists=$Exists;kind=$Kind;value=$Value}}
$packageName='YeManCC.GamingHome';$taskName='YeManCC-GamingHome';$ymcc='C:\SOFT\YeMan\YeManCC\YeManCC.exe';$bat='C:\SOFT\YeMan\PowerControl\YeManSteam2.bat';$outer='C:\SOFT\YeMan\PowerControl\YeManSteam.bat'
$sid='S-1-5-21-100-200-300-1001';$stateRoot=Join-Path $Delivery '..\memory-state';$statePath=Join-Path $stateRoot 'registration-state.json';$script:virtualInstall=Join-Path $Delivery '..\memory-installed'
function Reset {
 $script:reg=@{GamingHomeApp=(V $true 'String' 'Xbox!App');StartupToGamingHome=(V $true 'DWord' 0)}
 $script:hashes=@{};$script:hashes[$bat]=$payload.compatibleBatSha256
 $script:stored=$null;$script:package=$null;$script:task=$null;$script:fail='';$script:effects=@();$script:developerMode=$true;$script:admin=$true
}
function Test-GamingHomeAdministrator { $script:admin }
function Read-Value($Name) { Clone $script:reg[$Name] }
function Write-Value($Name,$Value) {
 $script:effects+=('write:'+ $Name)
 if($script:fail -eq 'home-write' -and $Name -eq 'GamingHomeApp' -and $Value.value -like '*!Home') {throw 'inert home-write failure'}
 $script:reg[$Name]=Clone $Value
}
function Save-State($State) { $script:stored=Clone $State }
function Hash($Path) {
 if($script:hashes.ContainsKey($Path)){return $script:hashes[$Path]}
 if($Path.StartsWith($Delivery,[StringComparison]::OrdinalIgnoreCase)){$digest=[Security.Cryptography.SHA256]::Create();try{return ([BitConverter]::ToString($digest.ComputeHash([IO.File]::ReadAllBytes($Path)))).Replace('-','').ToLowerInvariant()}finally{$digest.Dispose()}}
 throw ('unbound hash terminal '+$Path)
}
function Test-Path {param($LiteralPath,$PathType)
 if($LiteralPath -eq $statePath){return $null -ne $script:stored}
 if($LiteralPath -eq $bat -or $LiteralPath -eq $ymcc -or $LiteralPath -eq $outer -or $LiteralPath -eq 'C:\SOFT\NirCmd\nircmd.exe'){return $true}
 if($LiteralPath -like 'Cert:\LocalMachine\TrustedPeople\*'){return $false}
 if($script:hashes.ContainsKey($LiteralPath)){return $true}
 if($LiteralPath.StartsWith($Delivery,[StringComparison]::OrdinalIgnoreCase)){return Microsoft.PowerShell.Management\Test-Path -LiteralPath $LiteralPath -PathType Leaf}
 if($LiteralPath.StartsWith($script:virtualInstall,[StringComparison]::OrdinalIgnoreCase)){return $true}
 throw ('unbound exists terminal '+$LiteralPath)
}
function Get-Content {param($LiteralPath,[switch]$Raw,$Encoding)
 if($LiteralPath -eq $statePath){return $script:stored|ConvertTo-Json -Depth 12}
 if($LiteralPath.StartsWith($Delivery,[StringComparison]::OrdinalIgnoreCase)){return Microsoft.PowerShell.Management\Get-Content -LiteralPath $LiteralPath -Raw -Encoding UTF8}
 throw ('unbound read terminal '+$LiteralPath)
}
function New-Item {param($ItemType,$Path,[switch]$Force)
 if($Path -ne $stateRoot){throw 'unbound mkdir terminal'}
}
function Copy-Item {param($LiteralPath,$Destination,[switch]$Force)
 if($Destination -ne $bat -and -not $Destination.StartsWith($stateRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'unbound copy destination'}
 $script:effects+=('copy:'+ $Destination);$script:hashes[$Destination]=Hash $LiteralPath
}
function Get-AppxPackage {param($Name) if($Name -ne $packageName){throw 'unbound package query'};$script:package}
function Add-AppxPackage {param($Path)
 $script:effects+='package'
 if($script:fail -eq 'package'){throw 'inert package failure'}
 $script:package=[pscustomobject]@{Publisher='CN=YeManCC Gaming Home';PackageFamilyName='YeManCC.GamingHome_test';PackageFullName='YeManCC.GamingHome_1.0.0.0_x64_test';Version=$payload.packageVersion;InstallLocation=$script:virtualInstall}
 $script:hashes[(Join-Path $script:virtualInstall 'YeManGamingHome.exe')]=$payload.exeSha256
}
function Get-AppxPackageManifest {param($Package)
 if($script:fail -eq 'extension'){return [xml]'<Package><Applications><Application><Extensions/></Application></Applications></Package>'}
 [xml]'<Package><Applications><Application><Extensions><Extension><AppExtension Name="windows.gamingApp"/></Extension></Extensions></Application></Applications></Package>'
}
function Import-Certificate {param($FilePath,$CertStoreLocation) $script:effects+='certificate';if($script:fail -eq 'certificate'){throw 'inert certificate failure'}}
function Get-AuthenticodeSignature {param($LiteralPath)
 [pscustomobject]@{Status=$(if($script:fail -eq 'signature'){'HashMismatch'}else{'Valid'});SignerCertificate=[pscustomobject]@{Thumbprint=$payload.certificateThumbprint}}
}
function Get-ScheduledTask {param($TaskPath,$TaskName,$ErrorAction) $script:task}
function New-ScheduledTaskAction {param($Execute,$Argument,$WorkingDirectory) [pscustomobject]@{Execute=$Execute;Arguments=$Argument;WorkingDirectory=$WorkingDirectory}}
function New-ScheduledTaskPrincipal {param($UserId,$LogonType,$RunLevel) [pscustomobject]@{UserId=$UserId;LogonType=$LogonType;RunLevel=$RunLevel}}
function New-ScheduledTaskSettingsSet {param([switch]$AllowStartIfOnBatteries,[switch]$DontStopIfGoingOnBatteries,$MultipleInstances,$ExecutionTimeLimit)
 [pscustomobject]@{DisallowStartIfOnBatteries=(-not $AllowStartIfOnBatteries);StopIfGoingOnBatteries=(-not $DontStopIfGoingOnBatteries);MultipleInstances=$MultipleInstances}}
function Register-ScheduledTask {param($TaskPath,$TaskName,$Description,$Action,$Principal,$Settings)
 $script:effects+='task';if($script:fail -eq 'task'){throw 'inert task failure'}
 $script:task=[pscustomobject]@{Description=$Description;Actions=@($Action);Principal=$Principal;Settings=$Settings;Triggers=$null}
}
function Grant-GamingHomeTaskAccess { $script:effects+='task-dacl';if($script:fail -eq 'dacl'){throw 'inert dacl failure'} }
foreach($failure in @('certificate','signature','package','extension','task','dacl','home-write')) {
 Reset;$Action='Enable';$script:fail=$failure;$failed=$false
 try{Invoke-GamingHomeSetup|Out-Null}catch{$failed=$true;$script:lastFailure=([string]$_)+" STACK "+$_.ScriptStackTrace}
 Assert $failed ('setup propagates '+$failure)
 Assert ($script:reg.GamingHomeApp.value -eq 'Xbox!App') ('failed setup preserves home '+$failure)
 Assert ($script:reg.StartupToGamingHome.value -eq 0) ('failed setup restores startup '+$failure)
 Assert ($script:hashes[$bat] -eq $payload.compatibleBatSha256) ('failed setup restores BAT '+$failure)
 Assert ($script:stored.phase -eq 'failed') ('failed setup journal '+$failure+' cause='+$script:lastFailure)
}
Reset;$Action='Enable';Invoke-GamingHomeSetup|Out-Null
Assert ($script:reg.GamingHomeApp.value -eq 'YeManCC.GamingHome_test!Home') 'actual setup selects registered family'
Assert ($script:reg.StartupToGamingHome.value -eq 1) 'actual setup sets startup'
Assert ($script:hashes[$bat] -eq $payload.guardedBatSha256) 'actual setup copies guarded BAT'
Assert ($script:effects -contains 'task-dacl') 'medium user read/run ACL granted'
Assert ($script:stored.phase -eq 'enabled') 'actual setup final journal'
$before=@($script:effects);Invoke-GamingHomeSetup|Out-Null
Assert (@($script:effects|Where-Object {$_ -eq 'package'}).Count -eq 1) 'idempotent setup does not reinstall'
Assert (@($script:effects|Where-Object {$_ -eq 'task'}).Count -eq 1) 'idempotent setup does not re-register task'
$Action='Restore';Invoke-GamingHomeSetup|Out-Null
Assert ($script:reg.GamingHomeApp.value -eq 'Xbox!App' -and $script:reg.StartupToGamingHome.value -eq 0) 'actual Restore recovers original selection'
Assert ($script:hashes[$bat] -eq $payload.compatibleBatSha256) 'actual Restore recovers original BAT'
Assert ($script:package -and $script:task) 'Restore leaves inert package and task without stopping applications'
Reset;$Action='Inspect';Invoke-GamingHomeSetup|Out-Null
Assert ($script:effects.Count -eq 0 -and -not $script:stored) 'Inspect has no persistent effects'
foreach($fault in @('admin','developer-mode','custom-bat')) {
 Reset;$Action='Enable'
 if($fault -eq 'admin'){$script:admin=$false}
 if($fault -eq 'developer-mode'){$script:developerMode=$false}
 if($fault -eq 'custom-bat'){$script:hashes[$bat]='custom-bat'}
 $failed=$false;try{Invoke-GamingHomeSetup|Out-Null}catch{$failed=$true}
 Assert ($failed -and $script:effects.Count -eq 0) ('preflight preserved '+$fault)
}
[ordered]@{status='PASS';checks=$script:checks;consumer='actual Invoke-GamingHomeSetup';effects='memory-only';packageInstalled=$false;certificateStoreChanged=$false;applicationsStarted=$false}|ConvertTo-Json
