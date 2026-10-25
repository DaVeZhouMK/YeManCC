[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$HostPayloadRoot,
 [Parameter(Mandatory=$true)][string]$EvidenceRoot
)
$ErrorActionPreference='Stop'
$EvidenceRoot=[IO.Path]::GetFullPath($EvidenceRoot)
if(Test-Path -LiteralPath $EvidenceRoot){throw 'Fresh evidence directory required'}
New-Item -ItemType Directory -Path $EvidenceRoot | Out-Null
$artifacts=Join-Path $EvidenceRoot 'build'
& dotnet build (Join-Path $PSScriptRoot 'FanFinalizeContractSelfTest.csproj') -c Release --artifacts-path $artifacts -v quiet *> (Join-Path $EvidenceRoot 'build.log')
if($LASTEXITCODE -ne 0){throw 'Finalize contract test build failed'}
$tool=Join-Path $artifacts 'bin\FanFinalizeContractSelfTest\release\FanFinalizeContractSelfTest.dll'
& dotnet $tool $HostPayloadRoot $EvidenceRoot (Join-Path $EvidenceRoot 'verification.json') *> (Join-Path $EvidenceRoot 'execution.log')
$code=$LASTEXITCODE
if($code -ne 0){Get-Content -LiteralPath (Join-Path $EvidenceRoot 'execution.log');exit $code}
$result=Get-Content -LiteralPath (Join-Path $EvidenceRoot 'verification.json') -Raw -Encoding UTF8|ConvertFrom-Json
if($result.status -ne 'PASS' -or $result.passed -ne $true){throw 'Finalize contract result invalid'}
# A real copied Host DLL with only its failure-terminal state changed to Ready
# must fail the exact same test. This DLL never has an EXE or payload manifest.
$negativeHost=Join-Path $EvidenceRoot 'negative-host'
New-Item -ItemType Directory -Path $negativeHost | Out-Null
foreach($name in @('YeManFanBackend.dll','Microsoft.Windows.SDK.NET.dll','WinRT.Runtime.dll')){
 $source=Join-Path $HostPayloadRoot $name
 if(Test-Path -LiteralPath $source){Copy-Item -LiteralPath $source -Destination $negativeHost}
}
& dotnet $tool '--write-negative-fixture' (Join-Path $HostPayloadRoot 'YeManFanHost.dll') (Join-Path $negativeHost 'YeManFanHost.dll') *> (Join-Path $EvidenceRoot 'negative-mutation.log')
if($LASTEXITCODE -ne 0){throw 'Exact honest-terminal mutation fixture failed'}
$negativeEvidence=Join-Path $EvidenceRoot 'negative-evidence'
& dotnet $tool $negativeHost $negativeEvidence (Join-Path $EvidenceRoot 'negative-verification.json') *> (Join-Path $EvidenceRoot 'negative-execution.log')
$negativeCode=$LASTEXITCODE
$negative=Get-Content -LiteralPath (Join-Path $EvidenceRoot 'negative-verification.json') -Raw -Encoding UTF8|ConvertFrom-Json
if($negativeCode -eq 0 -or $negative.status -ne 'FAIL' -or $negative.failure -notmatch 'exhausted finalize is honest failure'){
 throw 'A forged Ready terminal was not rejected by the actual state assertion'
}
$result|Add-Member -NotePropertyName knownBadReadyTerminalRejected -NotePropertyValue $true
$result|Add-Member -NotePropertyName negativeExitCode -NotePropertyValue $negativeCode
$result|Add-Member -NotePropertyName negativeReceipt -NotePropertyValue (Join-Path $EvidenceRoot 'negative-verification.json')
$result|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $EvidenceRoot 'verification.json') -Encoding UTF8
Write-Output ('FAN_FINALIZE_CONTRACT PASS checks='+@($result.checks).Count+' known-bad-terminal-rejected=True')
exit 0
