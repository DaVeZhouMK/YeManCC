[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$RepoRoot,
 [Parameter(Mandatory=$true)][string]$EvidenceDir,
 [Parameter(Mandatory=$true)][string]$RuntimeRoot,
 [Parameter(Mandatory=$true)][string]$SharedRuntimeRoot,
 [Parameter(Mandatory=$true)][string]$HostPayloadRoot,
 [string]$ProbeAssembly='Newtonsoft.Json'
)
$ErrorActionPreference='Stop'
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath
$stage='prerequisites';$status='BLOCKED';$failure=$null;$checks=@();$closure=$null;$probe=$null
$EvidenceDir=[IO.Path]::GetFullPath($EvidenceDir)
if($EvidenceDir.StartsWith([IO.Path]::GetFullPath($RepoRoot).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Evidence must be outside product source.'}
if(Test-Path -LiteralPath (Join-Path $EvidenceDir 'verification.json')){throw 'Fresh evidence is required.'}
New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null
try {
 if($PSVersionTable.PSVersion.Major -lt 7){throw 'POWERSHELL_7_REQUIRED'}
 if(-not (Get-Command dotnet -ErrorAction SilentlyContinue)){throw 'REAL_CLR_SDK_REQUIRED'}
 foreach($file in @((Join-Path $RuntimeRoot 'HandheldCompanion.runtime.json'),(Join-Path $RuntimeRoot 'HandheldCompanion.dll'),(Join-Path $HostPayloadRoot 'YeManFanHost.dll'),(Join-Path $HostPayloadRoot 'YeManFanHost.payload.json'))){
  if(-not (Test-Path -LiteralPath $file -PathType Leaf)){throw ('REQUIRED_PAYLOAD_MISSING:'+ [IO.Path]::GetFileName($file))}
 }
 $stage='current-disk-contract';$status='FAIL'
 . (Join-Path $RepoRoot 'tools\hc-device-runtime\verify-runtime.ps1')
 $closure=Assert-YmccDeviceCoreRuntime -ProjectRoot $RepoRoot -RuntimeRoot $RuntimeRoot
 $manifest=Get-Content -LiteralPath (Join-Path $RuntimeRoot 'HandheldCompanion.runtime.json') -Raw -Encoding utf8 | ConvertFrom-Json
 foreach($entry in $manifest.sharedDependencies.files){
  $name=[string]$entry.path
  if($name -notmatch '^[^/\\:]+\.dll$'){throw 'UNSAFE_SHARED_DEPENDENCY_NAME'}
  $file=Join-Path $SharedRuntimeRoot $name
  if(-not (Test-Path -LiteralPath $file -PathType Leaf) -or (Get-Item -LiteralPath $file).Length -ne [long]$entry.bytes -or
   (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -cne [string]$entry.sha256){throw ('DELIVERED_SHARED_CONTRACT_MISMATCH:'+ $name)}
 }
 $payload=Get-Content -LiteralPath (Join-Path $HostPayloadRoot 'YeManFanHost.payload.json') -Raw -Encoding utf8 | ConvertFrom-Json
 $hostEntries=@($payload.files | Where-Object path -ceq 'YeManFanHost.dll')
 if($hostEntries.Count -ne 1 -or [string]$payload.runtimeId -cne [string]$manifest.runtimeId -or
  (Get-FileHash -LiteralPath (Join-Path $HostPayloadRoot 'YeManFanHost.dll') -Algorithm SHA256).Hash.ToLowerInvariant() -cne [string]$hostEntries[0].sha256){throw 'HOST_PAYLOAD_IDENTITY_MISMATCH'}
 $stage='build-real-clr-probe';$status='BLOCKED'
 $build=Join-Path $EvidenceDir 'build'
 & dotnet build (Join-Path $PSScriptRoot 'RuntimeConnectivityProbe.csproj') -c Release --artifacts-path $build --nologo --no-dependencies --disable-build-servers -m:1 -p:UseSharedCompilation=false *> (Join-Path $EvidenceDir 'build.log')
 if($LASTEXITCODE -ne 0){throw 'REAL_CLR_PROBE_BUILD_FAILED'}
 $stage='build-formal-product-host'
 & dotnet build (Join-Path $RepoRoot 'FanLab\real-host\YeManFanHost.csproj') -c Release --artifacts-path $build --nologo --disable-build-servers -m:1 -p:UseSharedCompilation=false *> (Join-Path $EvidenceDir 'product-build.log')
 if($LASTEXITCODE -ne 0){throw 'FORMAL_PRODUCT_HOST_BUILD_FAILED'}
 $candidateHosts=@(Get-ChildItem -LiteralPath (Join-Path $build 'bin\YeManFanHost') -Recurse -File -Filter 'YeManFanHost.dll')
 if($candidateHosts.Count -ne 1){throw 'FORMAL_PRODUCT_HOST_OUTPUT_AMBIGUOUS'}
 $candidateHostPath=$candidateHosts[0].FullName
 $compiledProduct=[ordered]@{path=$candidateHostPath;sha256=(Get-FileHash -LiteralPath $candidateHostPath -Algorithm SHA256).Hash.ToLowerInvariant();project='FanLab/real-host/YeManFanHost.csproj';sourceBuild=$true}
 $program=Join-Path $build 'bin\RuntimeConnectivityProbe\release\RuntimeConnectivityProbe.dll'
 $configuration=[ordered]@{evidenceDir=$EvidenceDir;runtimeRoot=$RuntimeRoot;sharedRuntimeRoot=$SharedRuntimeRoot;hostPath=(Join-Path $HostPayloadRoot 'YeManFanHost.dll');candidateHostPath=$candidateHostPath;probeAssembly=$ProbeAssembly}
 $configPath=Join-Path $EvidenceDir 'configuration.json'
 $configuration | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $configPath -Encoding utf8
 $stage='actual-load-and-fault-injection';$status='FAIL'
 & dotnet $program $configPath *> (Join-Path $EvidenceDir 'probe.log')
 $code=$LASTEXITCODE
 $resultPath=Join-Path $EvidenceDir 'probe-result.json'
 if(-not (Test-Path -LiteralPath $resultPath -PathType Leaf)){throw 'ACTUAL_CLR_RECEIPT_MISSING'}
 $probe=Get-Content -LiteralPath $resultPath -Raw -Encoding utf8 | ConvertFrom-Json
 $checks=@($probe.checks)
 if($code -ne 0 -or $probe.status -cne 'PASS' -or $checks.Count -lt 10 -or @($checks | Where-Object status -cne 'PASS').Count -gt 0){throw 'ACTUAL_CLR_CONTRACT_FAILED'}
 $stage='complete';$status='PASS'
} catch {
 $failure=[ordered]@{code=$_.Exception.Message;exceptionType=$_.Exception.GetType().FullName;hresult=('0x{0:X8}' -f $_.Exception.HResult);stage=$stage}
}
$report=[ordered]@{schemaVersion=1;status=$status;stage=$stage;checks=$checks;currentDiskClosure=$closure;compiledProduct=$compiledProduct;failure=$failure;
 hardwareAcceptance=$false;actualClrRequired=$true;hostStarted=$false;managerConstructorsExecuted=$false;managerStartsCalled=$false;deviceOpenCalled=$false;timestampUtc=[DateTime]::UtcNow.ToString('o')}
$report | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $EvidenceDir 'verification.json') -Encoding utf8
Write-Output ('RUNTIME_CONNECTIVITY '+$status+' stage='+$stage+' checks='+$checks.Count)
if($status -ceq 'PASS'){exit 0};if($status -ceq 'BLOCKED'){exit 2};exit 1


