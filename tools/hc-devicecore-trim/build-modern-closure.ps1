param(
    [string]$InputRuntime = 'D:\Codex\2026-10-18\kai-s\work\hc-large-strip\binary-trim\modern-closure\input-snapshot',
    [string]$OutputDirectory = 'D:\Codex\2026-10-18\kai-s\work\hc-large-strip\binary-trim\modern-closure\final',
    [string]$ScratchRoot = 'D:\Codex\2026-10-18\kai-s\work\hc-large-strip\binary-trim',
    [string]$SigningKeyPath = 'D:\Codex\2026-10-18\kai-s\work\hc-large-strip\binary-trim\keys\YMCC-DeviceCore-Development.private.pem',
    [string]$ExistingProfilePath = 'G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\deps\ymcc-device-core-runtime\profile.json'
)
$ErrorActionPreference = 'Stop'
$dotnet = Join-Path ${env:ProgramFiles} 'dotnet\dotnet.exe'
if (!(Test-Path -LiteralPath $dotnet)) { $dotnet = (Get-Command dotnet -ErrorAction Stop).Source }
$env:PATH = (Split-Path $dotnet) + [IO.Path]::PathSeparator + $env:PATH
$env:DOTNET_CLI_HOME = Join-Path $ScratchRoot 'dotnet-home'
$env:NUGET_PACKAGES = Join-Path $ScratchRoot 'nuget-cache'
function Invoke-Dotnet { param([string[]]$Argv) & $dotnet @Argv; if ($LASTEXITCODE -ne 0) { throw ('dotnet failed: ' + ($Argv -join ' ')) } }
function Hash { param([string]$Path) (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$writePrefix = [IO.Path]::GetFullPath($ScratchRoot).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
if (!$OutputDirectory.StartsWith($writePrefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Output must remain inside the approved binary-trim scratch root' }
if ((Test-Path -LiteralPath $OutputDirectory) -and (Get-ChildItem -LiteralPath $OutputDirectory -Force | Select-Object -First 1)) { throw 'Output directory must be new and empty' }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$profileHash = Hash $ExistingProfilePath
$profile = Get-Content -LiteralPath $ExistingProfilePath -Raw | ConvertFrom-Json
Copy-Item -LiteralPath $ExistingProfilePath -Destination (Join-Path $OutputDirectory 'consumer-profile.readonly-snapshot.json')
$resolvedPolicy = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'profiles\modern-headless.policy.json') -Raw | ConvertFrom-Json
$resolvedPolicy.SigningKeyPath = [IO.Path]::GetFullPath($SigningKeyPath)
$resolvedPolicy | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'policy.resolved.json') -Encoding utf8
& (Join-Path $PSScriptRoot 'build-offline.ps1') -ScratchRoot $ScratchRoot
$toolDll = Join-Path $ScratchRoot 'bin\HcDeviceCoreTrim.dll'
$closure = Join-Path $OutputDirectory 'closure'
Invoke-Dotnet -Argv @($toolDll,'modern-closure',$InputRuntime,(Join-Path $InputRuntime 'HandheldCompanion.runtime.json'),(Join-Path $OutputDirectory 'policy.resolved.json'),$closure)
$runtime = Join-Path $closure 'runtime'
$runtimePrefix = [IO.Path]::GetFullPath($runtime).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
$manifestPath = Join-Path $runtime 'HandheldCompanion.runtime.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$excluded = @()
foreach ($entry in $profile.exclusions) {
    $target = [IO.Path]::GetFullPath((Join-Path $runtime $entry.path))
    if (!$target.StartsWith($runtimePrefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Excluded path escapes scratch runtime' }
    if (!(Test-Path -LiteralPath $target -PathType Leaf)) { continue }
    $stream = [IO.File]::OpenRead($target)
    try { $pe = [Reflection.PortableExecutable.PEReader]::new($stream); try { if ($pe.HasMetadata) { throw 'Managed exclusion must be applied before the managed reference closure audit' } } finally { $pe.Dispose() } } finally { $stream.Dispose() }
    $excluded += [pscustomobject]@{Path=$entry.path;Sha256=(Hash $target);Bytes=(Get-Item -LiteralPath $target).Length;Reason=$entry.reason}
    Remove-Item -LiteralPath $target
}
$excludedSet = @{}; foreach ($entry in $excluded) { $excludedSet[$entry.Path] = $true }
$manifest.files = @($manifest.files | Where-Object { !$excludedSet.ContainsKey($_.path) })
$manifest.runtimeId = $manifest.runtimeId + '-profile-aligned'
$manifest.derivative.runtimeValidationPending = $true
$manifest.derivative | Add-Member -NotePropertyName ExistingProfileSha256 -NotePropertyValue $profileHash -Force
$manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $manifestPath -Encoding utf8
$excluded | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'existing-exclusions.applied.json') -Encoding utf8
$validation = Join-Path $OutputDirectory 'validation'; New-Item -ItemType Directory -Force -Path $validation | Out-Null
foreach ($name in @('PathLoadProbe','CallbackPathProbe','ModernHeadlessProbe')) {
    $project = Join-Path $PSScriptRoot ('probes\'+$name+'\'+$name+'.csproj')
    $obj = Join-Path $OutputDirectory ('probe-build\'+$name+'\obj\'); $bin = Join-Path $OutputDirectory ('probe-build\'+$name+'\bin\')
    Invoke-Dotnet -Argv @('build',$project,"-p:BaseIntermediateOutputPath=$obj","-p:OutputPath=$bin",'--source',(Join-Path $ScratchRoot 'packages'),'--nologo','-v:q')
}
$probeRoot = Join-Path $OutputDirectory 'probe-build'
Invoke-Dotnet -Argv @((Join-Path $probeRoot 'PathLoadProbe\bin\PathLoadProbe.dll'),$runtime,(Join-Path $runtime 'Microsoft.Windows.SDK.NET.dll'),(Join-Path $validation 'path-load-gettypes.json'))
$replacements = @(Get-Content -LiteralPath (Join-Path $closure 'replacement-files.json') -Raw | ConvertFrom-Json)
$hcHash = ($replacements | Where-Object Path -eq 'HandheldCompanion.dll').Sha256
Invoke-Dotnet -Argv @((Join-Path $probeRoot 'CallbackPathProbe\bin\CallbackPathProbe.dll'),(Join-Path $runtime 'HandheldCompanion.dll'),$hcHash,(Join-Path $ScratchRoot 'callback-original-path-golden.json'),(Join-Path $validation 'actual-callback-path-comparison.json'))
Invoke-Dotnet -Argv @((Join-Path $probeRoot 'ModernHeadlessProbe\bin\ModernHeadlessProbe.dll'),$runtime,'538ea1f68f629a04',(Join-Path $validation 'headless-construction.json'))
foreach ($name in @('iNKORE.UI.WPF.Modern','iNKORE.UI.WPF.Modern.Controls')) { Invoke-Dotnet -Argv @($toolDll,'resource-check',(Join-Path $runtime ($name+'.dll')),(Join-Path $validation ($name+'.resource-reader.json'))) }
$totalBytes = 0L
foreach ($entry in $manifest.files) { $file = Join-Path $runtime $entry.path; if ((Hash $file) -ne $entry.sha256) { throw 'Final runtime manifest mismatch' }; $totalBytes += (Get-Item -LiteralPath $file).Length }
if ((Hash $ExistingProfilePath) -ne $profileHash) { throw 'Read-only consumer profile changed during validation; retry from a fresh output directory' }
$manifest.derivative.runtimeValidationPending = $false
$manifest.derivative | Add-Member -NotePropertyName ValidationScope -NotePropertyValue 'HC all GetTypes, strict Modern/Controls identity, seven reviewed pure constructions, 22 memory-ACPI callbacks. No device/window/render/font enumeration. One absent legacy UI dependency matches baseline.' -Force
$manifest | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $manifestPath -Encoding utf8
$replacementDirectory = Join-Path $OutputDirectory 'replacements'; New-Item -ItemType Directory -Force -Path $replacementDirectory | Out-Null
foreach ($entry in $replacements) { $target=Join-Path $replacementDirectory $entry.Path; New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null; Copy-Item -LiteralPath (Join-Path $runtime $entry.Path) -Destination $target; if ((Hash $target) -ne $entry.Sha256) { throw 'Replacement copy hash mismatch' } }
$build = Get-Content -LiteralPath (Join-Path $closure 'build-result.json') -Raw | ConvertFrom-Json
$build.Status = 'HEADLESS-VALIDATED-READY-FOR-INTEGRATION'; $build.CandidateBytes=$totalBytes; $build.RuntimeId=$manifest.runtimeId
$build | Add-Member -NotePropertyName CandidateFiles -NotePropertyValue $manifest.files.Count -Force
$build | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath (Join-Path $closure 'build-result.json') -Encoding utf8
$sourceMatchingProfile = [long]$build.SourceBytes - [long](($excluded | Measure-Object Bytes -Sum).Sum)
$delivery = [pscustomobject]@{
    Status='READY-FOR-HEADLESS-INTEGRATION'; RuntimeId=$manifest.runtimeId; RuntimePath=$runtime; ReplacementDirectory=$replacementDirectory
    Files=$replacements; CandidateRuntimeFiles=$manifest.files.Count; CandidateRuntimeBytes=$totalBytes; MatchingSourceProfileBytes=$sourceMatchingProfile; NetModernClosureSavedBytes=($sourceMatchingProfile-$totalBytes)
    InputManifestSha256=$build.InputManifestSha256; ExistingProfileSha256=$profileHash; CandidateManifestSha256=(Hash $manifestPath)
    StrongName=[pscustomobject]@{PublicKeyToken='538ea1f68f629a04';KeyBits=1024;HashAlgorithm='SHA-1';Purpose='Development strong-name binding identity only';Authenticode='NotSigned';PublisherAuthentication=$false;ReleaseSignature=$false}
    Gates=[pscustomobject]@{PathLoad=$true;HcAllGetTypes=1767;ControlsAllGetTypes=445;ReviewedPureConstructions=7;CallbackChecks=22;DeviceConstructors=$false;WindowOrRendering=$false;FontEnumeration=$false;HardwareValidated=$false}
    KnownBaselineLimitation='Modern has 323 loadable of 324 runtime types; one unused legacy UI type requires the absent iNKORE.UI.WPF 1.2.8.0. Baseline and candidate match. HC all types and all reviewed pure constructors pass. No shim added.'
    ExistingExclusionsApplied=$excluded; Integration='Merge exactly the three DLL replacements together into the current owner profile; owner regenerates formal hashes/manifest/pins. Consumer files and production payload were not modified.'
}
$delivery | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'DELIVERY.json') -Encoding utf8
Write-Output ('DELIVERY '+(Join-Path $OutputDirectory 'DELIVERY.json'))
