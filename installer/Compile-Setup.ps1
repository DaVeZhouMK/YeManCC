[CmdletBinding()]
param([Parameter(Mandatory)][string]$SourceRoot,[Parameter(Mandatory)][string]$Payload,[Parameter(Mandatory)][string]$BuildInfo,[Parameter(Mandatory)][string]$SdkRoot,[Parameter(Mandatory)][string]$Output)
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
$names=@('AssemblyInfo.cs','Model.cs','SafeFiles.cs','Native.cs','Runtimes.cs','HidHide.cs','DesktopShortcuts.cs','Engine.cs','InstallPlatform.cs','DefenderExclusions.cs','WorkSecurity.cs','RuntimeProtectionProbe.cs','Program.cs','WebAssets.cs','WebSetupForm.cs','BootstrapForm.cs')
$sources=@($names|ForEach-Object{Join-Path $SourceRoot $_})
$compilerArgs=@('/nologo','/codepage:65001','/target:winexe','/platform:x64','/optimize+','/utf8output',('/lib:'+$framework),'/r:System.Windows.Forms.dll','/r:System.Drawing.dll','/r:System.Management.dll','/r:System.ServiceProcess.dll','/r:System.Web.Extensions.dll','/r:System.IO.Compression.dll','/r:System.IO.Compression.FileSystem.dll','/r:Microsoft.CSharp.dll',('/r:'+$core),('/r:'+$forms),('/resource:'+$Payload+',core.payload'),('/resource:'+$ui+',setup.html'),('/resource:'+$core+',wv.core'),('/resource:'+$forms+',wv.forms'),('/resource:'+$loader+',wv.loader'),('/out:'+$Output),$BuildInfo)+$sources
$compilerArgs+=@(('/win32icon:'+$icon),('/resource:'+$icon+',app.icon'),('/resource:'+$iconPng+',app.png'))
if(Test-Path -LiteralPath $license){$compilerArgs+=('/resource:'+$license+',wv.license')}
& $csc @compilerArgs
if($LASTEXITCODE){throw 'Shared H5 installer compilation failed'}
