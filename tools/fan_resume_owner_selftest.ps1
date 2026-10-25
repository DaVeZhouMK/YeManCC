param([string]$ProjectRoot=(Split-Path -Parent $PSScriptRoot),[string]$EvidenceRoot)
$ErrorActionPreference='Stop'
if(-not $EvidenceRoot){$EvidenceRoot=Join-Path $env:TEMP ('fan-resume-owner-'+[guid]::NewGuid().ToString('N'))}
New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
$text=[IO.File]::ReadAllText((Join-Path $ProjectRoot 'native/main.cpp'))
if($text -notmatch 'g_fanResumeRequestOwner.begin\(generation\)'){throw 'production admission no longer uses tested owner'}
if($text -notmatch 'g_fanResumeRequestOwner.finish\(generation\)'){throw 'production completion no longer uses tested owner'}
if($text -notmatch 'fanHostScheduleFanResume\(next\);'){throw 'production owner does not consume queued wake'}
if($text -match 'g_fanResumeRecoveryInFlight'){throw 'obsolete drop-on-busy owner reappeared'}
$vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$lines=& $env:ComSpec /d /s /c "`"$vcvars`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'vcvars64 failed'}
foreach($line in $lines){$eq=$line.IndexOf('=');if($eq -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$eq),$line.Substring($eq+1),'Process')}}
$exe=Join-Path $EvidenceRoot 'fan-resume-owner.exe'
& cl.exe /nologo /std:c++17 /EHsc /utf-8 (Join-Path $ProjectRoot 'tools/fan_resume_owner_selftest.cpp') "/Fo$EvidenceRoot\owner.obj" "/Fe$exe" *> (Join-Path $EvidenceRoot 'build.out')
if($LASTEXITCODE -ne 0){Get-Content -LiteralPath (Join-Path $EvidenceRoot 'build.out');throw 'owner gate compile failed'}
& $exe *> (Join-Path $EvidenceRoot 'gate.out');$exit=$LASTEXITCODE
Get-Content -LiteralPath (Join-Path $EvidenceRoot 'gate.out')
if($exit -ne 0){throw 'owner gate failed'}
@{scope='actual-production-request-owner-and-caller-shape-concurrency-no-hardware';exit=$exit;headerSha256=(Get-FileHash -LiteralPath (Join-Path $ProjectRoot 'native/fan_resume_owner.h')).Hash}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $EvidenceRoot 'result.json') -Encoding utf8
