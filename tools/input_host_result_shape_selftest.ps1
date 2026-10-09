$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$nativePath = Join-Path $root 'native\main.cpp'
$native = Get-Content -LiteralPath $nativePath -Raw

function Assert-Contains([string]$text, [string]$needle, [string]$message) {
    if (-not $text.Contains($needle)) { throw $message }
}

$match = [regex]::Match(
    $native,
    '(?s)static json inputHostSubmitPad\(.*?\n}\n\nstatic void inputCaptureStart\(\)')
if (-not $match.Success) { throw 'inputHostSubmitPad function boundary not found' }
$function = $match.Value

Assert-Contains $function 'json frameProof = json::object();' 'frameProof must be built as an object'
Assert-Contains $function 'json result = json::object();' 'host receipt must be built as an object'
Assert-Contains $function 'result["rightStick"] = json::object' 'rightStick must remain an object'
Assert-Contains $function 'result["gyroDps"] = json::object' 'gyroDps must remain an object'

$legacyInitializer = '{"frameProof", {"sampleSequence", g_inputHostSampleSequence}, {"motionPairProofId", "pair-unproven-safe-zero"}}'
if ($function.Contains($legacyInitializer)) {
    throw 'legacy frameProof initializer can produce a top-level JSON array'
}

[pscustomobject]@{
    ok = $true
    function = 'inputHostSubmitPad'
    resultType = 'explicit-json-object'
    frameProofType = 'explicit-json-object'
    hostFrameValueReadSafe = $true
} | ConvertTo-Json -Compress
