param([Parameter(Mandatory=$true)][string]$Delivery,[Parameter(Mandatory=$true)][string]$Source,[Parameter(Mandatory=$true)][string]$Evidence)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Path $Evidence -Force | Out-Null
Add-Type -AssemblyName System.Security.Cryptography.Pkcs
$package=Join-Path $Delivery 'YMCC-GamingHome.msix'
$manifest=Get-Content -LiteralPath (Join-Path $Delivery 'payload.json') -Raw -Encoding UTF8 | ConvertFrom-Json
foreach($f in $manifest.files) {
 $p=Join-Path $Delivery $f.name
 if ((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant() -ne $f.sha256 -or (Get-Item -LiteralPath $p).Length -ne $f.bytes) { throw ('Delivery hash/size mismatch: '+$f.name) }
}
$archive=[IO.Compression.ZipFile]::OpenRead($package)
try {
 $s=$archive.GetEntry('AppxSignature.p7x').Open();$buffer=[IO.MemoryStream]::new();$s.CopyTo($buffer);$s.Dispose();$b=$buffer.ToArray();$buffer.Dispose()
 if ([Text.Encoding]::ASCII.GetString($b[0..3]) -ne 'PKCX') { throw 'Not an APPX signature.' }
 $cms=[Security.Cryptography.Pkcs.SignedCms]::new();$cms.Decode($b[4..($b.Length-1)]);$cms.CheckSignature($true)
 if ($cms.SignerInfos.Count -ne 1 -or $cms.SignerInfos[0].Certificate.Thumbprint -ne $manifest.certificateThumbprint) { throw 'Unexpected MSIX signer.' }
 $cer=[Security.Cryptography.X509Certificates.X509Certificate2]::new((Join-Path $Delivery 'YMCC-GamingHome.cer'))
 if ($cer.Thumbprint -ne $manifest.certificateThumbprint) { throw 'Exported certificate does not match signer.' }
 $binding=[ordered]@{cryptographicSignature='valid';subject=$cms.SignerInfos[0].Certificate.Subject;thumbprint=$cer.Thumbprint;signedContentBase64=[Convert]::ToBase64String($cms.ContentInfo.Content);trustStoreChanged=$false}
 $cer.Dispose()
 $binding | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Evidence 'cms-binding.json') -Encoding UTF8
 $stream=$archive.GetEntry('YeManGamingHome.exe').Open();$sha=[Security.Cryptography.SHA256]::Create()
 try {$exeHash=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant()} finally {$stream.Dispose();$sha.Dispose()}
 if ($exeHash -ne $manifest.exeSha256) { throw 'Packaged EXE is not the built EXE.' }
} finally { $archive.Dispose() }
& python -X utf8 (Join-Path $PSScriptRoot 'verify-package.py') $package (Join-Path $Evidence 'cms-binding.json') $Source (Join-Path $Evidence 'package-result.json')
if ($LASTEXITCODE -ne 0) { throw 'Signed package payload verification failed.' }
