[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceRoot,[string]$NativeSource)
$ErrorActionPreference='Stop'
$fanProject=Split-Path -Parent $PSScriptRoot
if(-not $NativeSource){$NativeSource=Join-Path $fanProject 'native\main.cpp'}
$fanSourceHash=(Get-FileHash -LiteralPath $NativeSource).Hash
$fanHeaderHashes=@{}
foreach($fanHeaderName in @('http_request_deadline.h','fan_response_contract.h','fan_http_body_contract.h')){$fanHeaderHashes[$fanHeaderName]=(Get-FileHash -LiteralPath (Join-Path $fanProject ('native\'+$fanHeaderName))).Hash}
$fanRaw=[IO.File]::ReadAllText($NativeSource).Replace("`r`n","`n")
$fanStart=$fanRaw.IndexOf('static bool crackHttpUrl(',[StringComparison]::Ordinal)
$fanRegStart=$fanRaw.IndexOf('static void reg_http() {',$fanStart,[StringComparison]::Ordinal)
$fanEnd=$fanRaw.IndexOf("`n}`n",$fanRegStart,[StringComparison]::Ordinal)+3
if($fanStart -lt 0 -or $fanEnd -le $fanStart){throw 'HTTP production boundary missing'}
$fanStartObserver=$fanRaw.LastIndexOf('static void fanLeaseKeepaliveObserveHttp(const std::string& method,',[StringComparison]::Ordinal)
$fanEndObserver=$fanRaw.IndexOf("`nstatic void fanLeaseKeepaliveShutdown()",$fanStartObserver,[StringComparison]::Ordinal)
if($fanStartObserver -lt 0 -or $fanEndObserver -le $fanStartObserver){throw 'Fan observer production boundary missing'}
$fanExtract=$fanRaw.Substring($fanStart,$fanEnd-$fanStart)+"`n"+$fanRaw.Substring($fanStartObserver,$fanEndObserver-$fanStartObserver)
$fanOriginalExtract=$fanExtract
$fanExtract=$fanExtract.Replace('uc.nPort == 8765','uc.nPort == fixturePort').Replace('port != 8765','port != fixturePort')
$fanEvidence=[IO.Path]::GetFullPath($EvidenceRoot)
New-Item -ItemType Directory -Path $fanEvidence -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $fanEvidence 'HttpExtract.inc'),$fanExtract,[Text.UTF8Encoding]::new($false))
$fanBuild=Join-Path $fanEvidence 'compile.cmd'
@"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /W4 /WX /std:c++20 /EHsc /utf-8 /MT /I"$fanProject\native" /I"$fanEvidence" "$PSScriptRoot\fan_renderer_http_transport_selftest.cpp" /Fo"$fanEvidence\fixture.obj" /Fe"$fanEvidence\fixture.exe"
"@ | Set-Content -LiteralPath $fanBuild -Encoding ascii
& $fanBuild *> (Join-Path $fanEvidence 'compile.txt')
if($LASTEXITCODE -ne 0){Get-Content -LiteralPath (Join-Path $fanEvidence 'compile.txt') -Tail 12;throw 'HTTP fixture compile failed'}
@{source=$fanSourceHash; extractSha256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($fanOriginalExtract))); headers=$fanHeaderHashes; replacements='Fan port only; actual reg_http/crackHttpUrl/receipt observer; fake activity persistence/display/auth sink/AI process hooks; real WinHTTP; no Host or hardware'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fanEvidence 'source-binding.json') -Encoding utf8
node (Join-Path $PSScriptRoot 'fan_renderer_http_transport_selftest.mjs') $fanEvidence
if($LASTEXITCODE -ne 0){throw 'Renderer HTTP receipt checks failed'}
if((Get-FileHash -LiteralPath $NativeSource).Hash -ne $fanSourceHash){throw 'Native source drifted while validating'}
foreach($fanHeaderName in $fanHeaderHashes.Keys){if((Get-FileHash -LiteralPath (Join-Path $fanProject ('native\'+$fanHeaderName))).Hash -ne $fanHeaderHashes[$fanHeaderName]){throw ('Header drifted: '+$fanHeaderName)}}
