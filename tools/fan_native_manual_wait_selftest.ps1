param([string]$EvidenceRoot)
$ErrorActionPreference = 'Stop'
$fanRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $fanWorkspace = [IO.Path]::GetFullPath((Join-Path $fanRoot '..\..\..'))
    $EvidenceRoot = Join-Path $fanWorkspace ('_scratch\fan-native-manual-wait-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$fanEvidence = [IO.Path]::GetFullPath($EvidenceRoot)
New-Item -ItemType Directory -Path $fanEvidence -Force | Out-Null
$fanNode = (Get-Command node -ErrorAction Stop).Source
$fanBridge = Join-Path $fanRoot 'src\bridge\fanHost.ts'
$fanBefore = (Get-FileHash -LiteralPath $fanBridge -Algorithm SHA256).Hash
$fanBundle = Join-Path $fanEvidence 'fan-native-manual-wait.mjs'
& $fanNode (Join-Path $fanRoot 'node_modules\esbuild\bin\esbuild') (Join-Path $PSScriptRoot 'fan_native_manual_wait_selftest.ts') --bundle --platform=node --format=esm "--outfile=$fanBundle" "--alias:@=$(Join-Path $fanRoot 'src')" *> (Join-Path $fanEvidence 'bundle-build.out')
if ($LASTEXITCODE -ne 0) { throw 'Fixture build failed; see bundle-build.out' }
& $fanNode $fanBundle *> (Join-Path $fanEvidence 'native-manual-wait.out')
$fanExit = $LASTEXITCODE
$fanRecord = Get-Content -LiteralPath (Join-Path $fanEvidence 'native-manual-wait.out') -Encoding UTF8 | Where-Object { $_.StartsWith('{"ok"') } | Select-Object -Last 1
if (-not $fanRecord) { throw 'Fixture emitted no final result' }
$fanResult = $fanRecord | ConvertFrom-Json
if ($fanExit -ne 0 -or -not $fanResult.ok -or $fanResult.hardwareCalls -ne 0) { throw 'Native manual wait gate failed; see native-manual-wait.out' }
$fanAfter = (Get-FileHash -LiteralPath $fanBridge -Algorithm SHA256).Hash
if ($fanBefore -ne $fanAfter) { throw 'Bridge changed during verification; result cannot bind to changed source' }
[pscustomobject]@{ok=$true;checks=$fanResult.checks;bridgeSha256=$fanAfter;bundleSha256=(Get-FileHash -LiteralPath $fanBundle -Algorithm SHA256).Hash;hardwareCalls=0;scope=$fanResult.scope;evidenceRoot=$fanEvidence} | ConvertTo-Json -Compress
