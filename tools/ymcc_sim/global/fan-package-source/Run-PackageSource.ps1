param([Parameter(Mandatory=$true)][string]$EvidenceDir)
$ErrorActionPreference='Stop'
$packageEngine=Join-Path $PSHOME 'pwsh.exe'
if(-not(Test-Path -LiteralPath $packageEngine -PathType Leaf)){throw 'PACKAGE_SOURCE_PS7_REQUIRED'}
$env:YMCC_PACKAGE_SOURCE_PS7=$packageEngine
& node (Join-Path $PSScriptRoot 'run.mjs') --evidence-dir $EvidenceDir
exit $LASTEXITCODE
