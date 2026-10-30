[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$CoreRoot)
$ErrorActionPreference='Stop'
$core=[IO.Path]::GetFullPath($CoreRoot).TrimEnd('\')
if($core -eq [IO.Path]::GetPathRoot($core).TrimEnd('\')){throw 'Historical inputs require an owned testing core'}
$marker=Join-Path $core '.ymcc-release-check-core.json'
if(!(Test-Path -LiteralPath $marker -PathType Leaf)){throw 'Historical input core owner marker missing'}
$identity=Get-Content -LiteralPath $marker -Raw|ConvertFrom-Json
if($identity.owner -cne 'YMCC-release-check'){throw 'Historical input core belongs to another owner'}
$relative='DeckyParentMigration-20261023/a1-2547/history'
$source=Join-Path (Join-Path $PSScriptRoot 'history') $relative
$target=[IO.Path]::GetFullPath((Join-Path (Join-Path $core 'Evidence') $relative))
if(!$target.StartsWith($core+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Historical input target escaped testing core'}
$files=[ordered]@{'result.json'='2a7a3f284df641659b691f84600f31335d77947937d827253ac061013dea59a7';'native-result.json'='2850ad18914927f98744ea0c0a31ac90d8a11577e09b093ebbbdeaf7abd22bd1';'consumer-result.json'='ea76797d7142bde1e05dd0422113af3bde1bfec0e939983b37c2e6ea01ca8859'}
New-Item -ItemType Directory -Path $target -Force|Out-Null
foreach($name in $files.Keys){
 $from=Join-Path $source $name;$to=Join-Path $target $name
 if((Get-FileHash -LiteralPath $from).Hash.ToLowerInvariant() -cne $files[$name]){throw ('Historical source changed: '+$name)}
 if(!(Test-Path -LiteralPath $to)){Copy-Item -LiteralPath $from -Destination $to}
 if((Get-FileHash -LiteralPath $to).Hash.ToLowerInvariant() -cne $files[$name]){throw ('Historical evidence changed: '+$name)}
}
Write-Output 'HISTORICAL_FAIL_INPUTS_PRESERVED files=3 currentExecutionAcceptance=NOT_RUN'