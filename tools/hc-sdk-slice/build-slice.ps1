[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$InputRuntimeRoot,
  [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
  [string]$ToolchainRoot = ''
)
$ErrorActionPreference = 'Stop'
$inputRoot = [IO.Path]::GetFullPath($InputRuntimeRoot)
$evidenceRoot = [IO.Path]::GetFullPath($EvidenceDirectory)
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null
function Hash([string]$p) { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant() }
$sdk = Join-Path $inputRoot 'Microsoft.Windows.SDK.NET.dll'
$hc = Join-Path $inputRoot 'HandheldCompanion.dll'
if ((Hash $sdk) -ne '0ec371d93798852e36461c8adddbeadce0f963a04752f0b64e54fe19c1c834a7') { throw 'Unreviewed original SDK input' }
if ((Hash $hc) -ne '0c5132a9d13aebfc5add2aa7c9ac54e8daaa8ebc816a0c68d099bb9685dc2e49') { throw 'Unreviewed original HC input' }
if ([string]::IsNullOrWhiteSpace($ToolchainRoot)) { $ToolchainRoot = Join-Path $env:USERPROFILE '.nuget\packages\microsoft.net.illink.tasks\10.0.11\tools\net' }
$linker = Join-Path $ToolchainRoot 'illink.dll'
if (!(Test-Path -LiteralPath $linker -PathType Leaf)) { throw "Pinned ILLink tool missing: $linker" }
$dotnetRoot = Split-Path -Parent (Get-Command dotnet).Source
$core = Join-Path $dotnetRoot 'shared\Microsoft.NETCore.App\10.0.11'
$desktop = Join-Path $dotnetRoot 'shared\Microsoft.WindowsDesktop.App\10.0.11'
$output = Join-Path $evidenceRoot 'linked'
$descriptor = Join-Path $PSScriptRoot 'sdk-preserve.xml'
$arguments = @('-a',$hc,'all','-a',(Join-Path $inputRoot 'Shared.dll'),'all',
  '-d',$inputRoot,'-d',$core,'-d',$desktop,'-out',$output,'-x',$descriptor,
  '--action','copyused','--action','link','Microsoft.Windows.SDK.NET',
  '--skip-unresolved','true','--deterministic','--keep-metadata','all','--strip-security','false',
  '--disable-opt','unreachablebodies','--disable-opt','overrideremoval','--disable-opt','unusedtypechecks',
  '--disable-opt','unusedinterfaces','--keep-dep-attributes','true')
foreach ($base in @($core,$desktop)) {
  foreach ($file in Get-ChildItem -LiteralPath $base -File -Filter '*.dll') { $arguments += @('--action','skip',$file.BaseName) }
}
foreach ($name in @('WindowsBase.dll','PresentationCore.dll','PresentationFramework.dll','System.Windows.Forms.dll')) {
  $arguments += @('-reference',(Join-Path $desktop $name))
}
# Only the SDK output is eligible for promotion. ILLink's other rewritten copies are scratch.
$log = Join-Path $evidenceRoot 'linker.log'
& dotnet $linker @arguments *> $log
if ($LASTEXITCODE -ne 0) { throw "ILLink failed; see $log" }
$candidate = Join-Path $output 'Microsoft.Windows.SDK.NET.dll'
$artifacts = Join-Path $evidenceRoot 'inspector-build'
& dotnet run --project (Join-Path $PSScriptRoot 'SdkSlice.csproj') --artifacts-path $artifacts -- verify $sdk $candidate $inputRoot (Join-Path $evidenceRoot 'static-reference-check.json')
if ($LASTEXITCODE -ne 0) { throw 'SDK static reference validation failed' }
$record = [ordered]@{
  schemaVersion=1; profile='YMCC-DEVICECORE-WINRT-SLICE-v1'; sourceSdkSha256=(Hash $sdk);
  sourceHcSha256=(Hash $hc); descriptorSha256=(Hash $descriptor); linkerSha256=(Hash $linker);
  candidateSha256=(Hash $candidate); originalBytes=(Get-Item -LiteralPath $sdk).Length;
  candidateBytes=(Get-Item -LiteralPath $candidate).Length; runtimeProbeRequired=$true;
  physicalDeviceVerified=$false; onlyPromotableFile='Microsoft.Windows.SDK.NET.dll'
}
[IO.File]::WriteAllText((Join-Path $evidenceRoot 'slice.provenance.json'),($record | ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
Write-Output "SDK_SLICE_STATIC_PASS: $($record.originalBytes) -> $($record.candidateBytes) bytes; runtime probes required"
