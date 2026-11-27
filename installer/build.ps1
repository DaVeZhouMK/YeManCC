[CmdletBinding()]
param(
 [string]$WorkspaceRoot = 'G:\YeManCC-Work',
 [string]$ProgramZip = 'G:\YeManCC-Work\Deliverables\YMCC-v0.0.39-Permissions-20261113\YeManCC-0.39.zip',
 [string]$SystemRootSource = 'D:\Win11\TEST\野蛮整合优化系统',
 [string]$WebViewBootstrapper = 'G:\YeManCC-Work\Build\Installer\FirstInstall-Light-20261112\acquire\MicrosoftEdgeWebview2Setup.exe',
 [string]$HwExecutable = 'C:\Program Files\HWiNFO64\HWiNFO64.EXE',
 [string]$BuildRoot,
 [string]$CoreVersion = '2026.11.17.4',
 [string]$MaintenancePublicKey
)
$ErrorActionPreference='Stop'
$sourceRoot=$PSScriptRoot
$sdkRoot=Join-Path (Split-Path $sourceRoot -Parent) 'deps\webview2_raw'
if(!$BuildRoot){$BuildRoot=Join-Path $WorkspaceRoot ('Build\Installer\FirstInstall-Light-20261112\build-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
$BuildRoot=[IO.Path]::GetFullPath($BuildRoot)
if($BuildRoot.StartsWith((Split-Path $sourceRoot -Parent)+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Build output must be outside the repository'}
if(Test-Path -LiteralPath $BuildRoot){throw 'Build root must be new; preserve previous evidence'}
$null=New-Item -ItemType Directory -Path $BuildRoot
$snapshot=Join-Path $BuildRoot 'source';$null=New-Item -ItemType Directory -Path $snapshot;Get-ChildItem -LiteralPath $sourceRoot -File | Copy-Item -Destination $snapshot;Copy-Item -LiteralPath (Join-Path $sourceRoot 'ui') -Destination (Join-Path $snapshot 'ui') -Recurse;$sourceRoot=$snapshot
$stage=Join-Path $BuildRoot 'payload';$null=New-Item -ItemType Directory -Path $stage
$csc=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$framework=Split-Path $csc -Parent
$programHash='B00D1C8EC7B0A0D7C531F55A16D5EA2607FB0472FD193B10656F30D4589A4DE1'
if((Get-FileHash -LiteralPath $ProgramZip -Algorithm SHA256).Hash -ne $programHash){throw 'Formal program ZIP identity mismatch'}
Copy-Item -LiteralPath $ProgramZip -Destination (Join-Path $stage 'program.zip')
$runtimeSource=Join-Path $SystemRootSource '2系统环境包\2..NET+VC'
$desktopRuntimeVersion='10.0.12'
$mapping=[ordered]@{'desktop.exe'="windowsdesktop-runtime-$desktopRuntimeVersion-win-x64.exe";'vc-x64.exe'='VC_redist.x64.exe';'vc-x86.exe'='VC_redist.x86.exe'}
$signatures=@()
foreach($item in $mapping.GetEnumerator()){
 $src=Join-Path $runtimeSource $item.Value;$sig=Get-AuthenticodeSignature -LiteralPath $src
 if($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notlike '*O=Microsoft Corporation*'){throw "Invalid Microsoft signature: $src"}
 Copy-Item -LiteralPath $src -Destination (Join-Path $stage $item.Key)
 $signatures+=[ordered]@{name=$item.Key;source=$src;status=[string]$sig.Status;subject=$sig.SignerCertificate.Subject;thumbprint=$sig.SignerCertificate.Thumbprint;version=(Get-Item -LiteralPath $src).VersionInfo.FileVersion}
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$webSig=Get-AuthenticodeSignature -LiteralPath $WebViewBootstrapper
if($webSig.Status -ne 'Valid' -or $webSig.SignerCertificate.Subject -notlike '*O=Microsoft Corporation*'){throw 'Invalid Microsoft WebView2 bootstrapper signature'}
if((Get-Item -LiteralPath $WebViewBootstrapper).Length -gt 5MB){throw 'Bundle only the small bootstrapper, not the full WebView2 runtime'}
Copy-Item -LiteralPath $WebViewBootstrapper -Destination (Join-Path $stage 'webview2-bootstrapper.exe')
$signatures+=[ordered]@{name='webview2-bootstrapper.exe';source=$WebViewBootstrapper;status=[string]$webSig.Status;subject=$webSig.SignerCertificate.Subject;thumbprint=$webSig.SignerCertificate.Thumbprint}
$hw=Join-Path $SystemRootSource '1C盘主文件\Program Files\HWiNFO64'
$rtss=Join-Path $SystemRootSource '1C盘主文件\Program Files (x86)\RivaTuner Statistics Server'
foreach($tree in @($hw,$rtss)){
 if(Get-ChildItem -LiteralPath $tree -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } | Select-Object -First 1){throw 'Scheduler resource has reparse points'}
}
$hwStage=Join-Path $BuildRoot 'hw-source';$null=New-Item -ItemType Directory -Path (Join-Path $hwStage 'YeMan')
$hwSig=Get-AuthenticodeSignature -LiteralPath $HwExecutable
if($hwSig.Status -ne 'Valid'){throw 'Invalid local HWiNFO executable signature'}
$hwOriginalHash=(Get-FileHash -LiteralPath $HwExecutable).Hash
Copy-Item -LiteralPath $HwExecutable -Destination (Join-Path $hwStage 'HWiNFO64.EXE')
if((Get-FileHash -LiteralPath (Join-Path $hwStage 'HWiNFO64.EXE')).Hash -ne $hwOriginalHash){throw 'Local HWiNFO changed while being copied'}
# Keep the supplied scheduling presets. Do not export this computer's live INI/backups.
foreach($preset in @('HWiNFO64.INI','YeMan\HWiNFO64.INI')){Copy-Item -LiteralPath (Join-Path $hw $preset) -Destination (Join-Path $hwStage $preset)}
$hwInfo=(Get-Item -LiteralPath (Join-Path $hwStage 'HWiNFO64.EXE')).VersionInfo
$hwVersion=$hwInfo.FileVersion
if($hwVersion -notmatch '^[0-9]+\.[0-9]+-[0-9]+$'){throw 'Unexpected HWiNFO version format'}
$hwFileVersion=('{0}.{1}.{2}.{3}' -f $hwInfo.FileMajorPart,$hwInfo.FileMinorPart,$hwInfo.FileBuildPart,$hwInfo.FilePrivatePart)
[IO.Compression.ZipFile]::CreateFromDirectory($hwStage,(Join-Path $stage 'hw.zip'),[IO.Compression.CompressionLevel]::Optimal,$false)
[IO.Compression.ZipFile]::CreateFromDirectory($rtss,(Join-Path $stage 'rtss.zip'),[IO.Compression.CompressionLevel]::Optimal,$false)
& $csc /nologo /codepage:65001 /target:winexe /platform:x64 /optimize+ /r:System.Windows.Forms.dll ('/out:'+(Join-Path $stage 'launcher.exe')) (Join-Path $sourceRoot 'Launcher.cs')
if($LASTEXITCODE){throw 'Launcher compile failed'}
$files=@(Get-ChildItem -LiteralPath $stage -File | Sort-Object Name | ForEach-Object { [ordered]@{Path=$_.Name;Sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant();Length=$_.Length} })
$bundle=[ordered]@{Schema=1;Version='0.0.39';CoreVersion=$CoreVersion;Files=$files}
$manifest=Join-Path $stage 'bundle.json';$bundle|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $manifest -Encoding utf8
$hash=(Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToLowerInvariant()
$buildInfo=Join-Path $BuildRoot 'BuildInfo.cs'
('namespace YeMan.Setup { public static class BuildInfo { public const string CoreVersion="'+$CoreVersion+'"; public const string ProgramVersion="0.0.39"; public const string DesktopRuntimeVersion="'+$desktopRuntimeVersion+'"; public const string HwVersion="'+$hwVersion+'"; public const string HwFileVersion="'+$hwFileVersion+'"; public const string ManifestSha256="'+$hash+'"; } }') | Set-Content -LiteralPath $buildInfo -Encoding utf8
$payload=Join-Path $BuildRoot 'core-payload.zip'
[IO.Compression.ZipFile]::CreateFromDirectory($stage,$payload,[IO.Compression.CompressionLevel]::NoCompression,$false)
$out=Join-Path $BuildRoot '安装YeManCC.exe'
& (Join-Path $sourceRoot 'Compile-Setup.ps1') -SourceRoot $sourceRoot -Payload $payload -BuildInfo $buildInfo -SdkRoot $sdkRoot -Output $out -MaintenancePublicKey $MaintenancePublicKey
$sourceHashes=@(Get-ChildItem -LiteralPath $sourceRoot -File -Recurse | ForEach-Object {[ordered]@{Name=$_.FullName.Substring($sourceRoot.Length+1);Sha256=(Get-FileHash -LiteralPath $_.FullName).Hash;Length=$_.Length}})
$record=[ordered]@{artifact=$out;bytes=(Get-Item -LiteralPath $out).Length;sha256=(Get-FileHash -LiteralPath $out).Hash;manifestSha256=$hash;uiSha256=(Get-FileHash -LiteralPath (Join-Path $sourceRoot 'ui\setup.html')).Hash;source=$sourceHashes;runtimeSignatures=$signatures;hw=@{source=$HwExecutable;version=$hwVersion;fileVersion=$hwFileVersion;sha256=$hwOriginalHash;signature=[string]$hwSig.Status;subject=$hwSig.SignerCertificate.Subject;presetsSource=$hw};webViewRuntimeBundled=$false;webViewBootstrapperBundled=$true;networkPolicy='WEBVIEW2_IF_MISSING_ONLY';firstInstallOnly=$false;localUpdate=$true;fixedInstallRoot='C:\SOFT\YeMan';defenderDefault=$true;defenderScope='7_EXACT_CORE_FILES_6_UNPACK_AND_7_ATOMIC_TEMPORARY_PATHS';defenderSystemTest='NOT_RUN';systemActions='NOT_RUN';fullFixtures='NOT_RUN';programBaselineUnchanged=((Get-FileHash -LiteralPath $ProgramZip).Hash -eq $programHash);compilerSha256=(Get-FileHash -LiteralPath $csc).Hash}
$record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $BuildRoot 'build-record.json') -Encoding utf8
[pscustomobject]$record | Select-Object artifact,bytes,sha256,webViewRuntimeBundled,webViewBootstrapperBundled,systemActions | Format-List
