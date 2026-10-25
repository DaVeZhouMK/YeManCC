[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$SourceFile,[Parameter(Mandatory=$true)][string]$ContractFile,[Parameter(Mandatory=$true)][string]$ManifestFile)
$ErrorActionPreference='Stop'
$taskSource=$SourceFile
$taskTokens=$null;$taskParseErrors=$null
$taskAst=[Management.Automation.Language.Parser]::ParseFile($taskSource,[ref]$taskTokens,[ref]$taskParseErrors)
if($taskParseErrors.Count){throw 'PARSE_ERROR'}
$taskFns=@($taskAst.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -in @('Fail','Assert-ReviewedPayloadManifest')},$true))
if($taskFns.Count -ne 2){throw 'EXACT_MANIFEST_FUNCTIONS_REQUIRED'}
foreach($taskFn in $taskFns){Invoke-Expression $taskFn.Extent.Text}
$taskContract=Get-Content -LiteralPath $ContractFile -Raw -Encoding utf8|ConvertFrom-Json
$taskManifest=Get-Content -LiteralPath $ManifestFile -Raw -Encoding utf8|ConvertFrom-Json
$taskRows=@()
function Copy-Manifest { $taskManifest|ConvertTo-Json -Depth 12|ConvertFrom-Json }
function Check-Manifest([string]$Name,[bool]$Reject,[scriptblock]$Mutation){$sample=Copy-Manifest;& $Mutation $sample;$caught=$null;try{Assert-ReviewedPayloadManifest $sample $taskContract}catch{$caught=$_.Exception.Message};if([bool]$caught -ne $Reject){throw "WRONG_MANIFEST_DISPOSITION $Name $caught"};$script:taskRows+=,[ordered]@{name=$Name;expectedRejected=$Reject;actualRejected=[bool]$caught;error=$caught;passed=$true}}
Check-Manifest 'actual-reviewed-schema2' $false {param($m)}
Check-Manifest 'old-unreviewed-schema1' $true {param($m) $m.schemaVersion=1}
Check-Manifest 'unknown-schema3' $true {param($m) $m.schemaVersion=3}
Check-Manifest 'missing-member' $true {param($m) $m.files=@($m.files|Select-Object -Skip 1)}
Check-Manifest 'duplicate-member' $true {param($m) $m.files[1]=$m.files[0]}
Check-Manifest 'wrong-hash' $true {param($m) $m.files[0].sha256=('0'*64)}
Check-Manifest 'self-manifest-cannot-replace-runtime' $true {param($m) $m.files[0].path='YeManFanHost.payload.json';$m.files[0].sha256=$taskContract.expectedTuple.manifestSha256}
Check-Manifest 'path-traversal' $true {param($m) $m.files[0].path='../YeManFanHost.dll'}
[ordered]@{schemaVersion=1;status='PASS';source=[ordered]@{path=$taskSource;sha256=(Get-FileHash -LiteralPath $taskSource -Algorithm SHA256).Hash.ToLowerInvariant()};execution='Actual PowerShell AST-extracted manifest functions, pure input data only';cases=$taskRows;hostExecutions=0;productionPayloadWritten=$false;testCount=$taskRows.Count}|ConvertTo-Json -Depth 9

