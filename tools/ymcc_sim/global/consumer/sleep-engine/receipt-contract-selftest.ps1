[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'receipt-contract.ps1')
$checks = @()
foreach ($case in @(
  @{ name='exit0/pass'; exit=0; status='PASS'; natural=$true; completed=$true; expected=$true },
  @{ name='nonzero/pass contradiction'; exit=17; status='PASS'; natural=$true; completed=$true; expected=$false; code='CHILD_NONZERO_EXIT' },
  @{ name='nonzero/incomplete contradiction'; exit=10; status='INCOMPLETE'; natural=$true; completed=$true; expected=$false; code='CHILD_NONZERO_EXIT' },
  @{ name='exit0/fail'; exit=0; status='FAIL'; natural=$true; completed=$true; expected=$false; code='CHILD_RECEIPT_NOT_PASS' },
  @{ name='forced cleanup'; exit=0; status='PASS'; natural=$false; completed=$true; expected=$false; code='CHILD_NATURAL_EXIT_UNCONFIRMED' },
  @{ name='consumer incomplete'; exit=0; status='PASS'; natural=$true; completed=$false; expected=$false; code='CONSUMER_DID_NOT_COMPLETE' }
)) {
  $actual = Test-SleepEngineChildReceipt $case.exit $case.status $case.natural $case.completed
  $passed = $actual.passed -eq $case.expected -and (-not $case.ContainsKey('code') -or $actual.code -eq $case.code)
  $checks += [ordered]@{ name=$case.name; passed=$passed; actual=$actual }
}
$before = @([pscustomobject]@{path='fixture/source.cs';sha256=('a'*64)})
foreach ($case in @(
  @{name='unchanged source';after=@([pscustomobject]@{path='fixture/source.cs';sha256=('a'*64)});expected=$true},
  @{name='source changed during build/run';after=@([pscustomobject]@{path='fixture/source.cs';sha256=('b'*64)});expected=$false},
  @{name='source removed';after=@();expected=$false},
  @{name='source inserted';after=@([pscustomobject]@{path='fixture/source.cs';sha256=('a'*64)},[pscustomobject]@{path='fixture/extra.cs';sha256=('c'*64)});expected=$false}
)) {
  $actual = Test-SleepEngineSourceSnapshots -Before $before -After $case.after
  $checks += [ordered]@{name=$case.name;passed=$actual.unchanged -eq $case.expected;actual=$actual}
}
$fixtureRoot = Join-Path (Split-Path -Parent ([IO.Path]::GetFullPath($OutputPath))) 'source-change-fixture'
if (Test-Path -LiteralPath $fixtureRoot) { throw 'FRESH_SOURCE_CHANGE_FIXTURE_REQUIRED' }
New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
[IO.File]::WriteAllText((Join-Path $fixtureRoot 'Probe.cs'), '// source before')
[IO.File]::WriteAllText((Join-Path $fixtureRoot 'Probe.csproj'), '<Project />')
[IO.File]::WriteAllText((Join-Path $fixtureRoot 'Invoke-SleepEngine.ps1'), '# inert source fixture')
[IO.File]::WriteAllText((Join-Path $fixtureRoot 'receipt-contract.ps1'), '# inert source fixture')
$observedBefore = @(Get-SleepEngineSourceSnapshot -SourceRoot $fixtureRoot)
[IO.File]::WriteAllText((Join-Path $fixtureRoot 'Probe.cs'), '// source changed after before-build capture')
$observedAfter = @(Get-SleepEngineSourceSnapshot -SourceRoot $fixtureRoot)
$actualChange = Test-SleepEngineSourceSnapshots -Before $observedBefore -After $observedAfter
$checks += [ordered]@{ name='actual file content changes between source captures'; passed=(-not $actualChange.unchanged -and @($actualChange.differences|Where-Object code -eq 'SOURCE_HASH_CHANGED').Count -eq 1); actual=$actualChange }
$result = [ordered]@{schemaVersion=1;status=if(@($checks|Where-Object{-not $_.passed}).Count -eq 0){'PASS'}else{'FAIL'};checks=$checks;hostStarted=$false;networkUsed=$false;productSourceModified=$false}
$result|ConvertTo-Json -Depth 12|Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Output ($result|ConvertTo-Json -Depth 12)
if($result.status -ne 'PASS'){exit 1}

