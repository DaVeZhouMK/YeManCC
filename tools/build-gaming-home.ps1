[CmdletBinding()]
param(
 [string]$ProjectRoot=(Split-Path -Parent $PSScriptRoot),
 [string]$OutputDirectory='',
 [string]$PackageVersion='1.0.0.0',
 [switch]$RunSelfTests
)
$ErrorActionPreference='Stop'
$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot)
if (-not $OutputDirectory) { $OutputDirectory=Join-Path $ProjectRoot '..\..\Build\GamingHome' }
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if ($PSVersionTable.PSVersion.Major -lt 7) { throw 'Build requires PowerShell 7 (.NET CertificateRequest); shipped setup supports Windows PowerShell 5.1.' }
if ([version]$PackageVersion -lt [version]'1.0.0.0') { throw 'Invalid package version.' }
$source=Join-Path $ProjectRoot 'native\GamingHome'
$setup=Join-Path $ProjectRoot 'tools\gaming-home'
$sourceFiles=@(Get-ChildItem -LiteralPath $source,$setup -Recurse -File)
$sourceFiles+=Get-Item -LiteralPath $PSCommandPath,(Join-Path $ProjectRoot 'PowerControl\YeManSteam2.bat'),(Join-Path $ProjectRoot 'PowerControl\YeManSteam.bat')
$before=@($sourceFiles | ForEach-Object { [pscustomobject]@{path=$_.FullName;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()} })
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$run=Join-Path $OutputDirectory ('build-'+[guid]::NewGuid().ToString('N').Substring(0,12))
$compile=Join-Path $run 'compile';$stage=Join-Path $run 'package';$delivery=Join-Path $run 'delivery'
New-Item -ItemType Directory -Path $compile,$stage,$delivery -Force | Out-Null
$testArg=if($RunSelfTests){'selftest'}else{''}
& $env:ComSpec /d /c ('"'+(Join-Path $source 'build.bat')+'" "'+$compile+'" '+$testArg) 2>&1 | Tee-Object -FilePath (Join-Path $run 'compiler.log')
if ($LASTEXITCODE -ne 0) { throw 'Gaming home compile/selftest failed.' }
if ($RunSelfTests) {
 & (Join-Path $setup 'state-selftest.ps1') -SourceRoot $setup | Tee-Object -FilePath (Join-Path $run 'state-selftest.json')
 if ($LASTEXITCODE -ne 0) { throw 'Gaming home selection tests failed.' }
}
Copy-Item -LiteralPath (Join-Path $compile 'YeManGamingHome.exe') -Destination $stage
Copy-Item -LiteralPath (Join-Path $source 'Assets') -Destination $stage -Recurse
Copy-Item -LiteralPath (Join-Path $source 'CustomCapability.SCCD') -Destination $stage
Copy-Item -LiteralPath (Join-Path $source 'Public') -Destination $stage -Recurse
$xml=[xml](Get-Content -LiteralPath (Join-Path $source 'AppxManifest.xml') -Raw -Encoding UTF8)
$xml.Package.Identity.Version=$PackageVersion
$xml.Save((Join-Path $stage 'AppxManifest.xml'))
$sdk='C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64'
$msix=Join-Path $delivery 'YMCC-GamingHome.msix'
& (Join-Path $sdk 'makeappx.exe') pack /d $stage /p $msix /o 2>&1 | Tee-Object -FilePath (Join-Path $run 'makeappx.log')
if ($LASTEXITCODE -ne 0) { throw 'Gaming home package validation failed.' }
# Create a reusable private development signer in FILES ONLY. No certificate
# store, registry, package installation, task or application is touched here.
$keys=Join-Path $OutputDirectory 'private-signing';New-Item -ItemType Directory -Path $keys -Force | Out-Null
$pfx=Join-Path $keys 'YMCC-GamingHome.pfx';$passwordFile=Join-Path $keys 'password.txt'
if ((Test-Path -LiteralPath $pfx) -ne (Test-Path -LiteralPath $passwordFile)) { throw 'Incomplete private signing material.' }
if (-not (Test-Path -LiteralPath $pfx)) {
 $password=[guid]::NewGuid().ToString('N')+[guid]::NewGuid().ToString('N')
 $rsa=[Security.Cryptography.RSA]::Create(3072)
 try {
  $request=[Security.Cryptography.X509Certificates.CertificateRequest]::new('CN=YeManCC Gaming Home',$rsa,[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)
  $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509BasicConstraintsExtension]::new($false,$false,0,$true))
  $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509KeyUsageExtension]::new([Security.Cryptography.X509Certificates.X509KeyUsageFlags]::DigitalSignature,$true))
  $oids=[Security.Cryptography.OidCollection]::new();$oids.Add([Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.3')) | Out-Null
  $request.CertificateExtensions.Add([Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($oids,$true))
  $cert=$request.CreateSelfSigned([DateTimeOffset]::UtcNow.AddDays(-1),[DateTimeOffset]::UtcNow.AddYears(2))
  [IO.File]::WriteAllBytes($pfx,$cert.Export([Security.Cryptography.X509Certificates.X509ContentType]::Pfx,$password))
  [IO.File]::WriteAllText($passwordFile,$password)
  $cert.Dispose()
 } finally { $rsa.Dispose() }
 # Private key and password inherit a user/admin/system-only ACL.
 $sid=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value
 $acl=[Security.AccessControl.DirectorySecurity]::new();$acl.SetAccessRuleProtection($true,$false)
 foreach($id in @($sid,'S-1-5-18','S-1-5-32-544')) {
  $rule=[Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new($id),[Security.AccessControl.FileSystemRights]::FullControl,[Security.AccessControl.InheritanceFlags]'ContainerInherit,ObjectInherit',[Security.AccessControl.PropagationFlags]::None,[Security.AccessControl.AccessControlType]::Allow)
  $acl.AddAccessRule($rule)
 }
 Set-Acl -LiteralPath $keys -AclObject $acl
}
$password=[IO.File]::ReadAllText($passwordFile)
$cert=[Security.Cryptography.X509Certificates.X509Certificate2]::new($pfx,$password,[Security.Cryptography.X509Certificates.X509KeyStorageFlags]::EphemeralKeySet)
try {
 [IO.File]::WriteAllBytes((Join-Path $delivery 'YMCC-GamingHome.cer'),$cert.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))
 # Do not echo the signer password to build logs.
 $signOutput=& (Join-Path $sdk 'signtool.exe') sign /fd SHA256 /f $pfx /p $password $msix 2>&1
 $signOutput | Set-Content -LiteralPath (Join-Path $run 'signtool-sign.log') -Encoding UTF8
 if ($LASTEXITCODE -ne 0) { throw 'Gaming home package signing failed.' }
 $guarded=Join-Path $ProjectRoot 'PowerControl\YeManSteam2.bat'
 $guardedBytes=[IO.File]::ReadAllBytes($guarded)
 $text=[Text.Encoding]::UTF8.GetString($guardedBytes)
 $guard="        tasklist /FI `"IMAGENAME eq YeManCC.exe`" 2>nul | find /I `"YeManCC.exe`" >nul`r`n        if errorlevel 1 (`r`n            start `"`" `"%EXE_PATH%`"`r`n        )"
 if (-not $text.Contains($guard)) { throw 'Expected YMCC already-running BAT guard is missing.' }
 $originalBytes=[Text.Encoding]::UTF8.GetBytes($text.Replace($guard,'        start "" "%EXE_PATH%"'))
 $sha=[Security.Cryptography.SHA256]::Create()
 try { $compatibleSha=([BitConverter]::ToString($sha.ComputeHash($originalBytes))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
 Copy-Item -LiteralPath $guarded -Destination $delivery
 foreach($name in @('Manage-GamingHome.ps1','state.ps1','Enable-YMCC-Home.cmd','Restore-Previous-Home.cmd','Inspect-YMCC-Home.cmd','TESTING.md')) { Copy-Item -LiteralPath (Join-Path $setup $name) -Destination $delivery }
 $files=@(Get-ChildItem -LiteralPath $delivery -File | ForEach-Object {[ordered]@{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}})
 $manifest=[ordered]@{schema=1;component='YeManCC.GamingHome';packageVersion=$PackageVersion;certificateThumbprint=$cert.Thumbprint;compatibleBatSha256=$compatibleSha;guardedBatSha256=(Get-FileHash -LiteralPath $guarded -Algorithm SHA256).Hash.ToLowerInvariant();exeSha256=(Get-FileHash -LiteralPath (Join-Path $compile 'YeManGamingHome.exe') -Algorithm SHA256).Hash.ToLowerInvariant();hardwareValidated=$false;installedByBuilder=$false;files=$files}
 $manifest | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $delivery 'payload.json') -Encoding UTF8
} finally { $cert.Dispose();$password=$null }
if ($RunSelfTests) {
 & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $setup 'setup-selftest.ps1') -Delivery $delivery -SourceRoot $setup | Tee-Object -FilePath (Join-Path $run 'setup-selftest.json')
 if ($LASTEXITCODE -ne 0) { throw 'Actual shipping setup consumer regression failed.' }
}
& (Join-Path $setup 'verify-package.ps1') -Delivery $delivery -Source $source -Evidence (Join-Path $run 'verification')
foreach($f in $before) { if ((Get-FileHash -LiteralPath $f.path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $f.sha256) { throw ('Source changed during build: '+$f.path) } }
$receipt=[ordered]@{schema=1;status='BUILT_SIGNED_AWAIT_USER_TEST';delivery=$delivery;package=$msix;packageSha256=(Get-FileHash -LiteralPath $msix -Algorithm SHA256).Hash.ToLowerInvariant();exeSha256=$manifest.exeSha256;selftests=[bool]$RunSelfTests;source=$before;certificateStoreChanged=$false;packageInstalled=$false;applicationStarted=$false;windowsSelectionChanged=$false}
$receipt | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $run 'BUILD-RESULT.json') -Encoding UTF8
$receipt | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'LATEST.json') -Encoding UTF8
Write-Output ('GAMING_HOME_BUILD_OK '+$delivery)
