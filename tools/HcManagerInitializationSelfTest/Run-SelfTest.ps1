[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$EvidenceRoot,
 [string]$RuntimeRoot='',
 [string]$SharedRuntimeRoot=''
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$evidence=[IO.Path]::GetFullPath($EvidenceRoot)
if($evidence.StartsWith($repo.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Keep fixture/build artifacts outside the repository.'}
if(-not $RuntimeRoot){$RuntimeRoot=Join-Path $repo 'PowerControl\handheldcompanion-runtime\YMCC-DEVICECORE-20261018-v1'}
if(-not $SharedRuntimeRoot){$SharedRuntimeRoot=Join-Path $repo 'PowerControl\ymcc-shared-runtime'}
New-Item -ItemType Directory -Force -Path $evidence|Out-Null
. (Join-Path $repo 'tools\hc-device-runtime\verify-runtime.ps1')
$closure=Assert-YmccDeviceCoreRuntime -ProjectRoot $repo -RuntimeRoot $RuntimeRoot
$artifacts=Join-Path $evidence 'build'
& dotnet build (Join-Path $PSScriptRoot 'HcManagerInitializationSelfTest.csproj') -c Release --artifacts-path $artifacts --nologo *> (Join-Path $evidence 'build.log')
if($LASTEXITCODE -ne 0){throw 'Manager constructor probe build failed.'}
$probe=Join-Path $artifacts 'bin\HcManagerInitializationSelfTest\release\HcManagerInitializationSelfTest.dll'
$positive=Join-Path $evidence 'approved-runtime'
& dotnet $probe $RuntimeRoot $SharedRuntimeRoot $positive *> (Join-Path $evidence 'approved-runtime.log')
$positiveExit=$LASTEXITCODE
$positiveResult=Get-Content -LiteralPath (Join-Path $positive 'result.json') -Raw|ConvertFrom-Json
if($positiveExit -ne 0 -or -not $positiveResult.ok -or $positiveResult.managerCount -ne 13){throw 'Approved runtime cannot construct all 13 HC managers.'}
# Recreate the actual bad ABI from retained, hash-locked pre-repair binary artifacts.
$negativeRoot=Join-Path $evidence 'negative-runtime'
if(Test-Path -LiteralPath $negativeRoot){throw 'Require a fresh evidence directory for negative fixture.'}
Copy-Item -LiteralPath $RuntimeRoot -Destination $negativeRoot -Recurse
$bad=@{
 'NAudio.Core.dll'='41ae47df2c6039a47aea97ad76624708937360e47a3d498848a7b229c4238450'
 'NAudio.Wasapi.dll'='e3443fb2ab478d9f3ecc70db045902a1acbb7a264b6a1ca887c696aaa5410777'
}
foreach($name in $bad.Keys){
 $source=Join-Path $repo ('deps\ymcc-device-core-runtime\regression-fixtures\pre-abi-repair\'+$name)
 if((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $bad[$name]){throw ('Known-bad ABI fixture changed: '+$name)}
 Copy-Item -LiteralPath $source -Destination (Join-Path $negativeRoot $name) -Force
}
$negative=Join-Path $evidence 'known-bad-runtime'
& dotnet $probe $negativeRoot $SharedRuntimeRoot $negative *> (Join-Path $evidence 'known-bad-runtime.log')
$negativeExit=$LASTEXITCODE
$negativeResult=Get-Content -LiteralPath (Join-Path $negative 'result.json') -Raw|ConvertFrom-Json
$leaf=@($negativeResult.exceptionChain)[-1]
$reproduced=$negativeExit -eq 1 -and -not $negativeResult.ok -and $leaf.type -eq 'System.ArgumentException' -and $leaf.hresult -eq '0x80070057' -and $leaf.target -eq 'NAudio.CoreAudioApi.Interfaces.IMMDeviceEnumerator.RegisterEndpointNotificationCallback'
$report=[ordered]@{status=$(if($reproduced){'PASS'}else{'FAIL'});approvedClosure=$closure;approvedManagerCount=$positiveResult.managerCount;knownBadReproduced=$reproduced;knownBadLeaf=$leaf;negativeFixtureIntentionallyChangesManifestBytes=$true;hardwareAcceptance=$false;noHostOrDeviceOpen=$true}
$report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $evidence 'verification.json') -Encoding UTF8
if(-not $reproduced){throw 'Known-bad audio ABI did not reproduce the expected constructor failure.'}
Write-Output 'HC_MANAGER_INITIALIZATION_REAL_BINARY_REGRESSION_PASS approved=13 known-bad=RegisterEndpointNotificationCallback/0x80070057'