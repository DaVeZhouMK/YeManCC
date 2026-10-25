[CmdletBinding()]
param([string]$OutputDirectory='')
$ErrorActionPreference='Stop'
if(-not $OutputDirectory){$OutputDirectory=Join-Path (Split-Path $PSScriptRoot) 'PowerControl/MemoryScan'}
$source=Join-Path (Split-Path $PSScriptRoot) 'PowerControl/MemoryScan/MemoryScanHost.cs'
$compiler=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
[IO.Directory]::CreateDirectory($OutputDirectory)|Out-Null
$binary=Join-Path $OutputDirectory 'MemoryScanHost.exe';$temporary=Join-Path $OutputDirectory ('MemoryScanHost-'+[guid]::NewGuid().ToString('N')+'.exe')
try{
 & $compiler /nologo /optimize+ /target:winexe /platform:x64 /r:System.Web.Extensions.dll ('/out:'+$temporary) $source
 if($LASTEXITCODE -ne 0){throw 'MemoryScanHost compilation failed'}
 if([IO.File]::Exists($binary)){[IO.File]::Replace($temporary,$binary,[NullString]::Value)}else{[IO.File]::Move($temporary,$binary)}
 $luaSource=Join-Path (Split-Path $source) 'CeScanEngine.lua'
 if([IO.Path]::GetFullPath($OutputDirectory) -ine [IO.Path]::GetFullPath((Split-Path $source))){[IO.File]::Copy($luaSource,(Join-Path $OutputDirectory 'CeScanEngine.lua'),$true)}
 $manifest=@{schema=2;engine='cheat-engine';luaSha256=(Get-FileHash -LiteralPath $luaSource).Hash.ToLowerInvariant();sourceSha256=(Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant();binarySha256=(Get-FileHash -LiteralPath $binary).Hash.ToLowerInvariant();runtime='Windows .NET Framework 4.8 x64';source='MemoryScanHost.cs';binary='MemoryScanHost.exe'}
 [IO.File]::WriteAllText((Join-Path $OutputDirectory 'MemoryScanHost.payload.json'),($manifest|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
 $manifest|ConvertTo-Json
}finally{if([IO.File]::Exists($temporary)){[IO.File]::Delete($temporary)}}