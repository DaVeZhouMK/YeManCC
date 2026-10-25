<#
.SYNOPSIS
Exercises the REAL official-dependency Apply command in task-only directories.
.DESCRIPTION
Tests successful first migration, idempotence, prepared-receipt tampering,
corrupt downloads and rollback after a late generated-header failure.
No executable payload is run. No installed path, Steam or hardware is touched.
#>
param([Parameter(Mandatory=$true)][string]$EvidenceDir)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$EvidenceDir=[IO.Path]::GetFullPath($EvidenceDir)
New-Item -ItemType Directory -Path $EvidenceDir -Force|Out-Null
$lock=Get-Content -LiteralPath (Join-Path $root 'decky-plugin/vendor/decky-loader.lock.json') -Raw -Encoding UTF8|ConvertFrom-Json
$records=New-Object 'System.Collections.Generic.List[object]'
$fixtureRoot=Join-Path $EvidenceDir ('run-'+$PID+'-'+[DateTime]::UtcNow.Ticks)
$hostExe=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
function Check([bool]$Ok,[string]$Name){if(-not $Ok){throw $Name};$records.Add(@{name=$Name;passed=$true})|Out-Null}
function Write-Text([string]$Path,[string]$Value){New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force|Out-Null;[IO.File]::WriteAllText($Path,$Value)}
function Fixture([string]$Name,[bool]$NativeDirectory=$true){
 $scope=Join-Path $fixtureRoot $Name
 if(Test-Path -LiteralPath $scope){throw 'Selftest EvidenceDir must be fresh'}
 $project=Join-Path $scope 'project';$prepared=Join-Path $scope 'prepared'
 New-Item -ItemType Directory -Path $prepared,(Join-Path $project 'tools'),(Join-Path $project 'decky-plugin/vendor') -Force|Out-Null
 if($NativeDirectory){New-Item -ItemType Directory -Path (Join-Path $project 'native')|Out-Null}
 Copy-Item -LiteralPath (Join-Path $root 'tools/decky-loader-contract.mjs') -Destination (Join-Path $project 'tools/decky-loader-contract.mjs')
 Write-Text (Join-Path $project 'PowerControl/decky/PluginLoader_noconsole.exe') 'ORIGINAL-INERT-LOADER'
 Write-Text (Join-Path $project 'PowerControl/decky/LICENSE.decky-loader') 'ORIGINAL-LICENSE'
 Write-Text (Join-Path $project 'PowerControl/decky/settings/user.json') 'PRESERVE-USER-CONFIG'
 Copy-Item -LiteralPath (Join-Path $root 'PowerControl/decky/PluginLoader_noconsole.exe') -Destination (Join-Path $prepared 'PluginLoader-win-noconsole.exe')
 Copy-Item -LiteralPath (Join-Path $root 'PowerControl/decky/LICENSE.decky-loader') -Destination (Join-Path $prepared 'LICENSE')
 $release=[ordered]@{tag_name=$lock.version;draft=$false;prerelease=$false;published_at=$lock.publishedAt;html_url=('https://github.com/'+$lock.repository+'/releases/tag/'+$lock.version);assets=@([ordered]@{name=$lock.asset.name;id=$lock.asset.id;size=$lock.asset.size;digest=('sha256:'+$lock.asset.sha256);browser_download_url=$lock.asset.url;url=$lock.asset.apiUrl})}
 $release|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $prepared 'release.json') -Encoding UTF8
 @{sha=$lock.sourceCommit}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $prepared 'commit.json') -Encoding UTF8
 Copy-Item -LiteralPath (Join-Path $root 'decky-plugin/vendor/decky-loader.lock.json') -Destination (Join-Path $prepared 'candidate.lock.json')
 return @{project=$project;prepared=$prepared;scope=$scope}
}
function Apply($Fixture){
 $prior=$ErrorActionPreference
 try{
  $ErrorActionPreference='Continue'
  & $hostExe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools/decky-loader-upgrade.ps1') -Action Apply -EvidenceDir $Fixture.prepared -ProjectRoot $Fixture.project *> (Join-Path $Fixture.scope 'apply.log')
  $code=$LASTEXITCODE
 }finally{$ErrorActionPreference=$prior}
 return $code
}
$f=Fixture 'success'
Check ((Apply $f)-eq 0) 'actual Apply succeeds for first migration without existing identity files'
Check ((Get-FileHash -LiteralPath (Join-Path $f.project 'PowerControl/decky/PluginLoader_noconsole.exe')).Hash -ieq $lock.asset.sha256) 'applied bytes equal official hash'
Check ([IO.File]::ReadAllText((Join-Path $f.project 'PowerControl/decky/settings/user.json'))-eq'PRESERVE-USER-CONFIG') 'upgrade preserves user settings'
Check (Test-Path -LiteralPath (Join-Path $f.project 'native/decky_loader_identity.h')) 'upgrade generates native identity header'
Check ((Apply $f)-eq 0) 'reapplying identical official release is idempotent'
$f=Fixture 'tampered-receipt'
$changed=Get-Content -LiteralPath (Join-Path $f.prepared 'candidate.lock.json') -Raw|ConvertFrom-Json;$changed.byteIdentical=$false
$changed|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $f.prepared 'candidate.lock.json') -Encoding UTF8
Check ((Apply $f)-ne 0) 'tampered prepared receipt is rejected'
Check ([IO.File]::ReadAllText((Join-Path $f.project 'PowerControl/decky/PluginLoader_noconsole.exe'))-eq'ORIGINAL-INERT-LOADER') 'tampered receipt causes no source mutation'
$f=Fixture 'corrupt-download'
[IO.File]::WriteAllText((Join-Path $f.prepared 'PluginLoader-win-noconsole.exe'),'CORRUPT')
Check ((Apply $f)-ne 0) 'corrupt downloaded bytes are rejected before copying'
Check ([IO.File]::ReadAllText((Join-Path $f.project 'PowerControl/decky/PluginLoader_noconsole.exe'))-eq'ORIGINAL-INERT-LOADER') 'bad download preserves previous program'
$f=Fixture 'late-failure' $false
Check ((Apply $f)-ne 0) 'late missing native output directory fails the actual upgrade'
Check ([IO.File]::ReadAllText((Join-Path $f.project 'PowerControl/decky/PluginLoader_noconsole.exe'))-eq'ORIGINAL-INERT-LOADER') 'late failure restores previous program bytes'
Check ([IO.File]::ReadAllText((Join-Path $f.project 'PowerControl/decky/LICENSE.decky-loader'))-eq'ORIGINAL-LICENSE') 'late failure restores previous license'
Check (-not(Test-Path -LiteralPath (Join-Path $f.project 'decky-plugin/vendor/decky-loader.lock.json'))) 'late failure removes newly introduced lock'
Check (-not(Test-Path -LiteralPath (Join-Path $f.project 'native/decky_loader_identity.h'))) 'late failure leaves no false native identity'
Check ([IO.File]::ReadAllText((Join-Path $f.project 'PowerControl/decky/settings/user.json'))-eq'PRESERVE-USER-CONFIG') 'late failure preserves settings'
$result=@{status='passed';cases=$records.Count;results=@($records.ToArray());executedPayloads=0;steamActions=0;installedAppChanged=$false}
$result|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $EvidenceDir 'OFFICIAL-UPGRADE-TRANSACTION.json') -Encoding UTF8
$result|ConvertTo-Json -Depth 2 -Compress
