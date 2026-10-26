[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference='Stop'
if($env:GITHUB_ACTIONS -cne 'true' -or $env:RUNNER_ENVIRONMENT -cne 'github-hosted'){throw 'Disposable hosted routing capture required'}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$baselineFile=Join-Path $repo 'tools/ymcc_sim/global/extra-adapter-index.json'
$baseline=Get-Content -LiteralPath $baselineFile -Raw|ConvertFrom-Json
& node (Join-Path $repo 'scripts/write-version.mjs')
if($LASTEXITCODE -ne 0){throw 'Generated version inputs failed'}
$missing=[Collections.Generic.List[string]]::new()
$rows=@(foreach($row in $baseline.sources){
 $file=[IO.Path]::GetFullPath((Join-Path $repo $row.path))
 if(-not $file.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Routing source escaped repository'}
 if(-not (Test-Path -LiteralPath $file -PathType Leaf)){$missing.Add([string]$row.path);continue}
 @{path=$row.path;sha256=(Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant()}
})
$record=[ordered]@{schemaVersion=1;meaning='Source-bound routing identities only; never execution/receipt evidence';selectionStatus='CAPTURED_FOR_REVIEW';originalIndexSha256=(Get-FileHash -LiteralPath $baselineFile).Hash.ToLowerInvariant();runnerImageVersion=$env:ImageVersion;sourceCommit=([string](& git -C $repo rev-parse HEAD)).Trim();groups=$baseline.groups;sources=$rows;missing=@($missing);consumerExecution='NOT_RUN';formalReleaseAccepted=$false}
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($OutputPath)))|Out-Null
[IO.File]::WriteAllText($OutputPath,($record|ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
Write-Output ('HOST_ROUTING_CAPTURED groups='+$baseline.groups.Count+' sources='+$rows.Count+' missing='+$missing.Count)
if($missing.Count){throw 'Routing source inputs missing'}