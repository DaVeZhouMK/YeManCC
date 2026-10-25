<# Offline SharpDX callback regression phase. No Host/native/device entry point is invoked. #>
[CmdletBinding()]
param(
    [string]$RuntimeRoot='',
    [string]$SharedRuntimeRoot='',
    [string]$UpstreamRuntimeRoot='',
    [string]$KnownTrimmedRoot='',
    [Parameter(Mandatory)][Alias('EvidenceRoot','EvidenceDir')][string]$OutputDirectory,
    [ValidateSet('full','compat')][string]$Scope='full',
    [ValidatePattern('^$|^[a-fA-F0-9]{64}$')][string]$ExpectedManifestSha256=''
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if(-not $RuntimeRoot){$RuntimeRoot=Join-Path $repo 'PowerControl\handheldcompanion-runtime\YMCC-DEVICECORE-20261018-v1'}
if(-not $SharedRuntimeRoot){$SharedRuntimeRoot=Join-Path $repo 'PowerControl\ymcc-shared-runtime'}
if(-not $UpstreamRuntimeRoot){$UpstreamRuntimeRoot=Join-Path $repo 'deps\hc-runtime-upstream\HC-CANDIDATE-0.32.4.0-06c0b954-20260902'}
if(-not $KnownTrimmedRoot){$KnownTrimmedRoot=Join-Path $repo 'deps\ymcc-device-core-runtime\regression-fixtures\pre-abi-repair'}
$KnownTrimmedRoot=[IO.Path]::GetFullPath($KnownTrimmedRoot)
$RuntimeRoot=[IO.Path]::GetFullPath($RuntimeRoot)
$SharedRuntimeRoot=[IO.Path]::GetFullPath($SharedRuntimeRoot)
$UpstreamRuntimeRoot=[IO.Path]::GetFullPath($UpstreamRuntimeRoot)
$out=[IO.Path]::GetFullPath($OutputDirectory)
foreach($inputRoot in @($RuntimeRoot,$SharedRuntimeRoot,$UpstreamRuntimeRoot,$KnownTrimmedRoot)){
    if(-not(Test-Path -LiteralPath $inputRoot -PathType Container)){throw "Input directory is missing: $inputRoot"}
    if($out.Equals($inputRoot,[StringComparison]::OrdinalIgnoreCase) -or $out.StartsWith($inputRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Evidence directory must be outside all runtime input trees'}
}
New-Item -ItemType Directory -Force -Path $out | Out-Null
$resultPath=Join-Path $out 'result.json'
# Replace stale success before build/admission. fan_domain also checks freshness and exit code.
[ordered]@{schemaVersion=2;status='running';passed=$false;scope=$Scope;targetNativePolicy='deny'} | ConvertTo-Json | Set-Content -LiteralPath $resultPath -Encoding UTF8
try{
    # Historical negatives must retain their original failures when production bins change.
    $historicalPins=@{
        'SharpDX.dll'='e862ba1f03827b812735008cfa03ca8a912d9c09a2d1845007ee0936116fa453'
        'SharpDX.DirectInput.dll'='9b68076fda1397229200021c05f062d0283f73e406889a23da47fe7b6f8f7d03'
        'SharpDX.Direct3D9.dll'='530a2e95fb84932086f8038d16cbaffd4b9b806b4389f5f29ce1f0ad11486ae7'
    }
    foreach($name in $historicalPins.Keys){
        if((Get-FileHash -LiteralPath (Join-Path $KnownTrimmedRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant() -cne $historicalPins[$name]){throw ('Historical SharpDX negative fixture changed: '+$name)}
    }
    $manifest=Join-Path $RuntimeRoot 'HandheldCompanion.runtime.json'
    $capturedManifest=(Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToLowerInvariant()
    $pinSource='caller'
    if(-not $ExpectedManifestSha256){$ExpectedManifestSha256=$capturedManifest;$pinSource='captured-at-admission'}
    $ExpectedManifestSha256=$ExpectedManifestSha256.ToLowerInvariant()
    if($capturedManifest -ne $ExpectedManifestSha256){throw "Manifest pin mismatch: expected $ExpectedManifestSha256, actual $capturedManifest"}
    $sourcePins=@(Get-ChildItem -LiteralPath $PSScriptRoot -File | Sort-Object Name | ForEach-Object {
        [ordered]@{path=$_.FullName;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
    [ordered]@{scope=$Scope;runtimeRoot=$RuntimeRoot;sharedRuntimeRoot=$SharedRuntimeRoot;upstreamRuntimeRoot=$UpstreamRuntimeRoot;knownTrimmedRoot=$KnownTrimmedRoot;expectedManifestSha256=$ExpectedManifestSha256;manifestPinSource=$pinSource;toolSources=$sourcePins} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $out 'invocation.json') -Encoding UTF8
    $artifacts=Join-Path $out 'tool-build'
    & dotnet build (Join-Path $PSScriptRoot 'SharpDxCallbackSelfTest.csproj') -c Release --artifacts-path $artifacts --configfile (Join-Path $PSScriptRoot 'NuGet.Config') -v quiet 2>&1 | Tee-Object -FilePath (Join-Path $out 'build.log')
    if($LASTEXITCODE -ne 0){throw 'SharpDX offline tool build failed'}
    $tool=Join-Path $artifacts 'bin\SharpDxCallbackSelfTest\release\SharpDxCallbackSelfTest.dll'
    & dotnet $tool run $RuntimeRoot $SharedRuntimeRoot $UpstreamRuntimeRoot $out $ExpectedManifestSha256 $Scope $KnownTrimmedRoot 2>&1 | Tee-Object -FilePath (Join-Path $out 'run.log')
    $suiteExit=$LASTEXITCODE
    $receipt=Get-Content -LiteralPath $resultPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if($suiteExit -ne 0 -or $receipt.status -ne 'passed' -or -not $receipt.passed){throw "SharpDX callback regression failed; exit=$suiteExit; inspect $resultPath"}
    if($receipt.scope -ne $Scope -or $receipt.runtimeManifestSha256 -ne $ExpectedManifestSha256 -or -not $receipt.sourceInputsUnchanged){throw 'SharpDX result input/scope mismatch'}
    if($receipt.targetNativePolicy -ne 'deny' -or $receipt.targetNativeAttempts -ne 0 -or $receipt.hardwareAccess){throw 'SharpDX native policy failed'}
    if($receipt.knownBadCount -ne 9 -or $receipt.knownBadRejected -ne $receipt.knownBadCount){throw 'SharpDX required negative controls did not all reject'}
    if(-not $receipt.knownTrimmedShadowRejected){throw 'Historical trimmed Include shadow was not rejected'}
    if($Scope -eq 'full' -and -not $receipt.currentShadowSurface.passed){throw 'Full Include shadow metadata and managed binding are required'}
    Write-Output "SHARPDX_CALLBACK_REGRESSION: passed scope=$Scope manifest=$ExpectedManifestSha256"
    # Expected failures run in C# child processes and must not leak a nonzero success exit.
    exit 0
}catch{
    $message=$_.Exception.Message
    $prior=$null
    try{$prior=Get-Content -LiteralPath $resultPath -Raw -Encoding UTF8 | ConvertFrom-Json}catch{}
    if($null -eq $prior -or $prior.status -ne 'failed'){
        [ordered]@{schemaVersion=2;status='failed';passed=$false;scope=$Scope;runtimeRoot=$RuntimeRoot;sharedRuntimeRoot=$SharedRuntimeRoot;targetNativePolicy='deny';error=$message} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $resultPath -Encoding UTF8
    }
    Write-Error $message -ErrorAction Continue
    exit 1
}

