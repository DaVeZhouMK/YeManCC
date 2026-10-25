[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceRoot, [string]$NodeExecutable='')
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$root=[IO.Path]::GetFullPath($EvidenceRoot)
if([string]::IsNullOrWhiteSpace($NodeExecutable)){
    $node=Get-Command node -ErrorAction SilentlyContinue
    if($node){$NodeExecutable=$node.Source}
    else {$workspace=[IO.Path]::GetFullPath((Join-Path $repo '..\..\..'));$NodeExecutable=Join-Path $workspace 'Environment\Toolchains\nodejs\node-v22.14.0-win-x64\node.exe'}
}
$nodePath=(Resolve-Path -LiteralPath $NodeExecutable).Path
New-Item -ItemType Directory -Path $root -Force | Out-Null
$bundle=Join-Path $root 'fan_late_curve_reply_selftest.mjs'
& $nodePath (Join-Path $repo 'node_modules\esbuild\bin\esbuild') (Join-Path $PSScriptRoot 'fan_late_curve_reply_selftest.ts') --bundle --platform=node --format=esm "--outfile=$bundle"
if($LASTEXITCODE -ne 0){throw 'Late curve reply test bundle failed'}
& $nodePath $bundle
if($LASTEXITCODE -ne 0){throw 'Late curve reply ownership test failed'}
Write-Output 'fan late curve reply self-test: PASS (120 checks; production bridge, adapter/native/hardware fixtures)'