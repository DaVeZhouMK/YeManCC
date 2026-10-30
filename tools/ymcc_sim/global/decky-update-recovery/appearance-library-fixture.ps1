[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ComponentRoot,[Parameter(Mandatory=$true)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
$ComponentRoot=[IO.Path]::GetFullPath($ComponentRoot)
$EvidenceRoot=[IO.Path]::GetFullPath($EvidenceRoot)
if(![IO.Path]::IsPathRooted($ComponentRoot) -or ![IO.Path]::IsPathRooted($EvidenceRoot)){throw 'EXPLICIT_ABSOLUTE_APPEARANCE_FIXTURE_ROOTS_REQUIRED'}
$seen=@{};$definitions=New-Object Collections.Generic.List[object];$mainAst=$null
foreach($relative in @('steam-appearance.ps1','steam-appearance\preset.ps1','steam-appearance\idle.ps1','steam-appearance\resident.ps1')){
 $file=Join-Path $ComponentRoot $relative
 $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($file,[ref]$tokens,[ref]$errors)
 if($errors.Count){throw ('APPEARANCE_FIXTURE_PARSE_FAILED:'+ $relative)}
 if($relative -eq 'steam-appearance.ps1'){$mainAst=$ast}
 foreach($statement in $ast.EndBlock.Statements){
  if($statement -isnot [Management.Automation.Language.FunctionDefinitionAst]){continue}
  if($seen.ContainsKey($statement.Name)){throw ('APPEARANCE_FIXTURE_DUPLICATE_FUNCTION:'+ $statement.Name)}
  $seen[$statement.Name]=$true
  $body=$statement.Extent.Text
  $sha=[Security.Cryptography.SHA256]::Create()
  try{$digest=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($body)))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
  $definitions.Add([pscustomobject]@{name=$statement.Name;source=$file;sourceLine=$statement.Extent.StartLineNumber;sha256=$digest})
  Invoke-Expression $body
 }
}
$themeAssignment=@($mainAst.EndBlock.Statements|Where-Object{$_ -is [Management.Automation.Language.AssignmentStatementAst] -and $_.Left.Extent.Text -eq '$script:ThemeNames'})
if($themeAssignment.Count -ne 1){throw 'APPEARANCE_FIXTURE_THEME_LITERAL_REQUIRED'}
$themeNames=@($themeAssignment[0].Right.FindAll({param($n) $n -is [Management.Automation.Language.StringConstantExpressionAst]},$true)|ForEach-Object{$_.Value})
$reconstructed='@('+(($themeNames|ForEach-Object{"'"+$_+"'"}) -join ',')+')'
if($themeAssignment[0].Right.Extent.Text -cne $reconstructed){throw 'APPEARANCE_FIXTURE_NONLITERAL_THEME_NAMES'}
if(@($themeNames).Count -ne 7 -or @($themeNames|Where-Object{$_ -isnot [string]}).Count){throw 'APPEARANCE_FIXTURE_THEME_LITERAL_SCHEMA'}
$manifestThemes = Get-Content -LiteralPath (Join-Path $ComponentRoot 'steam-appearance\manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if (($themeNames -join "`n") -cne (@($manifestThemes.themes.name) -join "`n")) { throw 'APPEARANCE_FIXTURE_THEME_MANIFEST_MISMATCH' }
$script:ThemeNames=$themeNames
$script:PackRoot=Join-Path $ComponentRoot 'steam-appearance'
$script:AppearanceContentManifestFile=Join-Path $script:PackRoot 'manifest.json'
$script:AppearanceContentIndex=$null;$script:AppearanceContentIndexStamp=''

$script:ThemeRoot=Join-Path $EvidenceRoot 'profile\homebrew\themes'
$script:PresetStateBase=Join-Path $EvidenceRoot 'states'
$script:AppearanceIdleFile=Join-Path $EvidenceRoot 'idle.json'
$script:LinkFile=Join-Path $ComponentRoot 'steam-appearance\backend.path'
$script:BackendExe=Join-Path $EvidenceRoot 'CssLoader-Standalone-Headless.exe'
$script:PresetTransactionActive=$false
$script:CurrentSessionId=99
$script:CurrentUserSid='fixture-owned-user'
$script:ProcessOwnerCache=@{}
$script:AppearanceFixtureMutexPrefix='Local\YMCC.CssFixture.'+[guid]::NewGuid().ToString('N')+'.'
function New-AppearanceMutex([string]$Purpose) {
 if($Purpose -notin @('Launch','ThemeControl')){throw 'APPEARANCE_FIXTURE_MUTEX_PURPOSE_DENIED'}
 New-Object Threading.Mutex($false,($script:AppearanceFixtureMutexPrefix+$Purpose))
}

# Unexpected process/identity/network routes fail before a system call.
function Get-CimInstance {throw 'APPEARANCE_FIXTURE_CIM_DENIED'}
function Invoke-CimMethod {throw 'APPEARANCE_FIXTURE_CIM_METHOD_DENIED'}
function Invoke-WebRequest {throw 'APPEARANCE_FIXTURE_NETWORK_DENIED'}
function Invoke-RestMethod {throw 'APPEARANCE_FIXTURE_NETWORK_DENIED'}
function Start-Process {throw 'APPEARANCE_FIXTURE_PROCESS_START_DENIED'}
function Stop-Process {throw 'APPEARANCE_FIXTURE_PROCESS_STOP_DENIED'}
function Test-CurrentUserProcess {throw 'APPEARANCE_FIXTURE_IDENTITY_QUERY_DENIED'}
$script:AppearanceFixtureInitialization=[pscustomobject]@{mode='exact-production-function-asts-with-owned-state-and-memory-identity';sourceFunctions=$definitions.ToArray();kernelMutexIsolation='per-fixture-random-namespace';wholeProductionLibraryDotSourced=$false;productionStartupIdentityQueries=0;productionStartupStateReads=0;currentUserIdentity='memory-only';physicalOrInstalledAcceptance=$false}
