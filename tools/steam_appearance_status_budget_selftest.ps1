param([Parameter(Mandatory=$true)][string]$Out)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Path $Out -Force|Out-Null
$source=Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl\steam-appearance.ps1'
$parseErrors=$null;$tokens=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($source,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw $parseErrors[0]}
# Execute only these production function definitions; all external IO is mocked.
$functions=$ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -in @('Get-AppearanceStatus','Invoke-LoaderRpc')},$true)
foreach($fn in $functions){Invoke-Expression $fn.Extent.Text}
$script:LinkFile=Join-Path $Out 'missing-link.txt';$script:RpcDeadline=$null;$script:ThemeNames=@('Round')
$checks=New-Object Collections.Generic.List[string]
function Assert($condition,$label){if(!$condition){throw $label}}
function Check($name,[scriptblock]$work){& $work;$checks.Add($name);Write-Output ('PASS '+$name)}
function Reset-Fixture {
 $script:ThemeRoot='X:\before\themes';$script:actualRoot='X:\backend\themes';$script:running=$true
 $script:rowReads=0;$script:ownerChecks=0;$script:rpcCalls=New-Object Collections.Generic.List[string]
 $script:rowPaths=New-Object Collections.Generic.List[string];$script:rejectOwnerAt=0;$script:failRpc=''
}
function Get-BackendProcesses {if($script:running){[pscustomobject]@{Id=42}}}
function Recover-PresetTransaction {}
function Save-PresetTargetHint {}
function Get-ThemeRows {$script:rowReads++;$script:rowPaths.Add($script:ThemeRoot);[pscustomobject]@{name='Round';installed=$true;configured=$true;backendEnabled=$null;parameterCount=0;error=''}}
function Get-ThemeTargetId {$script:ThemeRoot}
function Get-ThemeRevision($rows){'fixture-revision'}
function Get-Process {param($Name) @()}
function Test-BackendOwner {$script:ownerChecks++;$script:rejectOwnerAt -eq 0 -or $script:ownerChecks -lt $script:rejectOwnerAt}
function Invoke-RestMethod {
 param($Uri,$Method,$Body,$ContentType,$TimeoutSec)
 $request=[Text.Encoding]::UTF8.GetString($Body)|ConvertFrom-Json;$script:rpcCalls.Add($request.method)
 if($script:failRpc -eq $request.method){throw 'fixture unavailable'}
 if($request.method -eq 'fetch_theme_path'){return [pscustomobject]@{success=$true;res=$script:actualRoot}}
 if($request.method -eq 'get_themes'){return [pscustomobject]@{success=$true;res=@([pscustomobject]@{name='Round';enabled=$true})}}
 throw 'unexpected RPC'
}
Check 'Status resolves the actual root before exactly one theme read' {
 Reset-Fixture;$value=Get-AppearanceStatus
 Assert ($script:rowReads -eq 1 -and $script:rowPaths[0] -eq $script:actualRoot) 'duplicated or premature theme read'
 Assert ($value.backendReady -and $value.configuredMatchesBackend -and $value.targetId -eq $script:actualRoot) 'backend target not reflected'
 Assert ($script:ownerChecks -eq 2 -and $script:rpcCalls.Count -eq 2) 'each RPC must check its current owner once'
}
Check 'Offline Status reads configurations once and sends no RPC' {
 Reset-Fixture;$script:running=$false;$value=Get-AppearanceStatus
 Assert ($script:rowReads -eq 1 -and $script:ownerChecks -eq 0 -and $script:rpcCalls.Count -eq 0) 'offline work amplified'
 Assert (!$value.backendReady) 'offline must not be ready'
}
Check 'Ownership change between RPCs prevents the second request' {
 Reset-Fixture;$script:rejectOwnerAt=2;$value=Get-AppearanceStatus
 Assert (!$value.backendReady -and $script:ownerChecks -eq 2 -and $script:rpcCalls.Count -eq 1) 'stale ownership was reused'
 Assert ($script:rowReads -eq 1 -and $value.note -match 'owner changed') 'failed status must remain bounded and informative'
}
Check 'Unavailable API keeps one local theme read and does not retry' {
 Reset-Fixture;$script:failRpc='fetch_theme_path';$value=Get-AppearanceStatus
 Assert (!$value.backendReady -and $script:rowReads -eq 1 -and $script:rpcCalls.Count -eq 1) 'failure caused repeated audits'
 Assert ($script:rowPaths[0] -eq 'X:\before\themes') 'unconfirmed root was adopted'
}
Check 'Native leased path skips initial listener discovery' {
 $BackendPath='X:\fixture\CssLoader-Standalone-Headless.exe';$script:listenerScans=0
 function Get-NetTCPConnection {param($State,$LocalPort) $script:listenerScans++;throw 'discovery should not run'}
 $text=[IO.File]::ReadAllText($source);$first=$text.IndexOf('$portOwners=@()');$last=$text.IndexOf('if(!$BackendPath -and !$portOwners.Count',$first)
 Assert ($first -ge 0 -and $last -gt $first) 'discovery boundaries changed'
 Invoke-Expression $text.Substring($first,$last-$first)
 Assert ($script:listenerScans -eq 0) 'leased owner was redundantly rediscovered'
}
[pscustomobject]@{ok=$true;passed=$checks.Count;cases=@($checks);realSteamOperations=0;realCssOperations=0}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding utf8
