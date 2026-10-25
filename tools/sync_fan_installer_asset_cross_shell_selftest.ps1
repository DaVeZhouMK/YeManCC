[CmdletBinding()]
param([string]$OutputRoot='')
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$validation=[IO.Path]::GetFullPath((Join-Path $project '..\..\Build\Validation')).TrimEnd('\')
if(!$OutputRoot){$OutputRoot=Join-Path $validation ('FanInstallerCrossShell-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
if(!$OutputRoot.StartsWith($validation+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Fixture output must remain inside workspace Build/Validation'}
$fixture=Join-Path $OutputRoot 'fixture with spaces'
$pc=Join-Path $fixture 'PowerControl\fan-host';$bridge=Join-Path $fixture 'src\bridge'
New-Item -ItemType Directory -Force -Path $pc,$bridge|Out-Null
$input=Join-Path $pc 'install-fan-host-payload.ps1'
Copy-Item -LiteralPath (Join-Path $project 'PowerControl\fan-host\install-fan-host-payload.ps1') -Destination $input
$target=Join-Path $bridge 'fanHostInstallerAsset.json'
$generator=Join-Path $project 'tools\sync-fan-host-installer-asset.ps1'
$winps=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
$pwsh=(Get-Command pwsh -ErrorAction Stop).Source
$checks=[Collections.Generic.List[object]]::new()
function Check([string]$name,[bool]$passed){$checks.Add(@{name=$name;passed=$passed});if(!$passed){throw "Check failed: $name"}}
function RunGenerator([string]$shell,[string]$script,[string]$label){& $shell -NoProfile -ExecutionPolicy Bypass -File $script -ProjectRoot $fixture *> (Join-Path $OutputRoot ($label+'.log'));if($LASTEXITCODE-ne0){throw "Generator failed: $label ($LASTEXITCODE)"}}
RunGenerator $winps $generator 'new-winps-first'
$first=(Get-FileHash -LiteralPath $target).Hash;$stamp=(Get-Item -LiteralPath $target).LastWriteTimeUtc
RunGenerator $pwsh $generator 'new-pwsh-second'
Check 'PS 5.1 and 7 produce identical JSON bytes' ($first-eq(Get-FileHash -LiteralPath $target).Hash)
Check 'PS 7 does not rewrite a byte-identical PS 5.1 asset' ($stamp-eq(Get-Item -LiteralPath $target).LastWriteTimeUtc)
RunGenerator $winps $generator 'new-winps-third'
Check 'PS 5.1 idempotence after PS 7' ($first-eq(Get-FileHash -LiteralPath $target).Hash-and$stamp-eq(Get-Item -LiteralPath $target).LastWriteTimeUtc)
$j=[IO.File]::ReadAllText($target)|ConvertFrom-Json
$bytes=[Text.Encoding]::UTF8.GetBytes($j.content);$original=[IO.File]::ReadAllBytes($input)
Check 'Payload content preserves every original UTF-8 byte including BOM' ([Convert]::ToBase64String($bytes)-eq[Convert]::ToBase64String($original))
Check 'Declared SHA256 binds the original installer bytes' ($j.sha256-eq(Get-FileHash -LiteralPath $input).Hash.ToLowerInvariant())
$out=[IO.File]::ReadAllBytes($target)
Check 'Generated JSON is BOM-free with deterministic LF-only line breaks' (!($out.Length-ge3-and$out[0]-eq239-and$out[1]-eq187-and$out[2]-eq191)-and![IO.File]::ReadAllText($target).Contains("`r"))
$legacy=Join-Path $OutputRoot 'legacy.ps1'
$legacyLines=@('param([string]$ProjectRoot)','$b=[IO.File]::ReadAllBytes((Join-Path $ProjectRoot ''PowerControl\fan-host\install-fan-host-payload.ps1''))','$a=[ordered]@{schemaVersion=1;sha256=''fixture'';content=[Text.Encoding]::UTF8.GetString($b)}','[IO.File]::WriteAllText((Join-Path $ProjectRoot ''src\bridge\fanHostInstallerAsset.json''),(($a|ConvertTo-Json -Depth 3)+[Environment]::NewLine),[Text.UTF8Encoding]::new($false))')
[IO.File]::WriteAllText($legacy,($legacyLines-join"`r`n"),[Text.UTF8Encoding]::new($true))
RunGenerator $winps $legacy 'legacy-winps';$legacy51=(Get-FileHash -LiteralPath $target).Hash
RunGenerator $pwsh $legacy 'legacy-pwsh';$legacy7=(Get-FileHash -LiteralPath $target).Hash
Check 'Old ConvertTo-Json implementation reproduces cross-shell source drift' ($legacy51-ne$legacy7)
RunGenerator $winps $generator 'new-repair';Check 'Canonical generator repairs either legacy serialization' ($first-eq(Get-FileHash -LiteralPath $target).Hash)
$report=@{status='passed';cases=$checks.Count;failed=0;checks=$checks;canonicalSha256=$first;legacyPS51Sha256=$legacy51;legacyPS7Sha256=$legacy7;fixtureRoot=$fixture;installedProductTouched=$false;payloadSourceChanged=$false}
$report|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $OutputRoot 'summary.json') -Encoding UTF8
Write-Output ('FAN_INSTALLER_CROSS_SHELL_SELFTEST_OK cases='+$checks.Count)
Write-Output ('REPORT='+$OutputRoot+'\summary.json')