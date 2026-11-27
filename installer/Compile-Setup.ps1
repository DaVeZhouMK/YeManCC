[CmdletBinding()]
param([Parameter(Mandatory)][string]$SourceRoot,[Parameter(Mandatory)][string]$Payload,[Parameter(Mandatory)][string]$BuildInfo,[Parameter(Mandatory)][string]$SdkRoot,[Parameter(Mandatory)][string]$Output,[string]$MaintenancePublicKey)
$ErrorActionPreference='Stop'
$csc=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$framework=Split-Path $csc -Parent
$core=Join-Path $SdkRoot 'lib\net462\Microsoft.Web.WebView2.Core.dll'
$forms=Join-Path $SdkRoot 'lib\net462\Microsoft.Web.WebView2.WinForms.dll'
$loader=Join-Path $SdkRoot 'runtimes\win-x64\native\WebView2Loader.dll'
$ui=Join-Path $SourceRoot 'ui\setup.html'
$icon=Join-Path $SourceRoot 'ui\app.ico'
$iconPng=Join-Path $SourceRoot 'ui\app-icon.png'
$license=Join-Path $SdkRoot 'LICENSE.txt'
foreach($path in @($core,$forms,$loader,$ui,$icon,$iconPng,$Payload,$BuildInfo)){if(!(Test-Path -LiteralPath $path)){throw "Missing local compile input: $path"}}
# Only the shared H5 host is compiled. The old WinForms UI remains historical source.
$names=@('AssemblyInfo.cs','Model.cs','SafeFiles.cs','Native.cs','Runtimes.cs','HidHide.cs','DesktopShortcuts.cs','Engine.cs','InstallPlatform.cs','DefenderExclusions.cs','WorkSecurity.cs','RuntimeProtectionProbe.cs','Program.cs','WebAssets.cs','WebSetupForm.cs','BootstrapForm.cs','MaintenanceRegistration.cs','MaintenanceProgram.cs','MaintenanceRollback.cs','MaintenanceUpdate.cs')
$sources=@($names|ForEach-Object{Join-Path $SourceRoot $_})
$common=@('/nologo','/codepage:65001','/target:winexe','/platform:x64','/optimize+','/utf8output',('/lib:'+$framework),'/r:System.Windows.Forms.dll','/r:System.Drawing.dll','/r:System.Management.dll','/r:System.ServiceProcess.dll','/r:System.Web.Extensions.dll','/r:System.IO.Compression.dll','/r:System.IO.Compression.FileSystem.dll','/r:Microsoft.CSharp.dll',('/r:'+$core),('/r:'+$forms))
if ($MaintenancePublicKey) {
 $keyPath=(Resolve-Path -LiteralPath $MaintenancePublicKey).Path
 if ((Get-Item -LiteralPath $keyPath).Length -gt 8192) { throw 'Invalid maintenance public key length' }
 $key=[Security.Cryptography.RSACryptoServiceProvider]::new()
 try {
  $key.PersistKeyInCsp=$false
  $key.FromXmlString([IO.File]::ReadAllText($keyPath))
  if (!$key.PublicOnly -or $key.KeySize -lt 2048) { throw 'Only an RSA public key of at least 2048 bits can be embedded' }
 } finally { $key.Dispose() }
 $common+=('/resource:'+$keyPath+',maintenance.release-key')
}
$maintenance=Join-Path (Split-Path -Parent ([IO.Path]::GetFullPath($Output))) 'YeManCC-Uninstall.exe'
$fallback = $maintenance + '.files.tmp'
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
$outer = [IO.Compression.ZipFile]::OpenRead($Payload)
try {
 $program = $outer.GetEntry('program.zip')
 if (!$program -or $program.Length -gt 512MB) { throw 'Missing or unreasonably large program.zip' }
 $memory = [IO.MemoryStream]::new()
 try {
  $stream = $program.Open()
  try { $stream.CopyTo($memory) } finally { $stream.Dispose() }
  $memory.Position = 0
  $inner = [IO.Compression.ZipArchive]::new($memory,[IO.Compression.ZipArchiveMode]::Read,$true)
  try {
   $mutable = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
   $settingsIndex = $inner.Entries | Where-Object { $_.FullName.Replace('\','/') -ceq 'PowerControl/settings-baseline.json' } | Select-Object -First 1
   if ($settingsIndex) {
    $reader = [IO.StreamReader]::new($settingsIndex.Open())
    try { $settings = ConvertFrom-Json ($reader.ReadToEnd()) } finally { $reader.Dispose() }
    foreach ($row in @($settings.document) + @($settings.legacyImports) + @($settings.independentUserFiles)) {
     foreach ($part in @($row.path,$row.backup)) { if ($part) { $null = $mutable.Add('PowerControl/' + $part.Replace('\','/')) } }
    }
   }
   $lines = @($inner.Entries | ForEach-Object { $_.FullName.Replace('\','/') } | Where-Object { $_ -match '^(YeManCC|PowerControl)/[^\r\n]+$' -and !$_.EndsWith('/') -and !$mutable.Contains($_) -and $_ -ine 'YeManCC/YeManCC-Maintenance-Update.zip' })
   if (!$lines.Count -or $lines.Count -gt 24000) { throw 'Invalid fallback program inventory size' }
   [IO.File]::WriteAllLines($fallback, $lines, [Text.UTF8Encoding]::new($false))
  } finally { $inner.Dispose() }
 } finally { $memory.Dispose() }
} finally { $outer.Dispose() }
try {
 & $csc @common '/main:YeMan.Setup.MaintenanceProgram' ("/out:$maintenance") ("/win32icon:$icon") ("/win32manifest:"+(Join-Path $SourceRoot 'maintenance.manifest')) ("/resource:"+$fallback+',maintenance.files') $BuildInfo @sources
 if($LASTEXITCODE){throw 'Standalone uninstaller compilation failed'}
} finally { if (Test-Path -LiteralPath $fallback) { Remove-Item -LiteralPath $fallback } }
$compilerArgs=$common+@('/main:YeMan.Setup.Program',('/resource:'+$maintenance+',maintenance.exe'),('/resource:'+$Payload+',core.payload'),('/resource:'+$ui+',setup.html'),('/resource:'+$core+',wv.core'),('/resource:'+$forms+',wv.forms'),('/resource:'+$loader+',wv.loader'),('/out:'+$Output))
$compilerArgs+=@(('/win32icon:'+$icon),('/resource:'+$icon+',app.icon'),('/resource:'+$iconPng+',app.png'))
if(Test-Path -LiteralPath $license){$compilerArgs+=('/resource:'+$license+',wv.license')}
& $csc @compilerArgs $BuildInfo @sources
if($LASTEXITCODE){throw 'Shared H5 installer compilation failed'}
