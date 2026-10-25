[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop';$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $OutputDirectory){throw 'Fresh directory required'}
[IO.Directory]::CreateDirectory($OutputDirectory)|Out-Null
$r=[ordered]@{ok=$false;startedAtUtc=[DateTime]::UtcNow.ToString('o');sources=@();actualSdkOrDeviceCalls=0;fieldFlickerVerified=$false}
try{
 foreach($rel in @('InputHost/SteamDeckPadSubmissionEvidence.cs','tools/steamdeck_pad_submission_selftest.cs')){$p=Join-Path $root $rel;$d=Join-Path $OutputDirectory ([IO.Path]::GetFileName($rel));$h=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash;Copy-Item -LiteralPath $p -Destination $d;$r.sources+=@{path=$p;frozen=$d;sha256=$h}}
 '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup></Project>'|Set-Content -LiteralPath ($OutputDirectory+'/Probe.csproj') -Encoding utf8
 & dotnet build ($OutputDirectory+'/Probe.csproj') -c Release -o ($OutputDirectory+'/Output') --nologo *> ($OutputDirectory+'/build.log');if($LASTEXITCODE -ne 0){throw 'Build failed'}
 $p=Start-Process -FilePath ($OutputDirectory+'/Output/Probe.exe') -ArgumentList ('"'+$OutputDirectory+'/result.json"') -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput ($OutputDirectory+'/stdout.txt') -RedirectStandardError ($OutputDirectory+'/stderr.txt')
 $v=Get-Content -LiteralPath ($OutputDirectory+'/result.json') -Raw|ConvertFrom-Json;$r.exitCode=$p.ExitCode;$r.checks=$v.checks;$r.allocationWindows=$v.allocationWindows;$r.timingUsFor100000Reports=$v.timingUsFor100000Reports;$r.failures=@($v.cases|Where-Object {!$_.ok}|ForEach-Object {$_.name});if($p.ExitCode -ne 0 -or !$v.ok){throw 'Submission evidence checks failed'}
 foreach($f in $r.sources){if((Get-FileHash -LiteralPath $f.path -Algorithm SHA256).Hash -ne $f.sha256 -or (Get-FileHash -LiteralPath $f.frozen -Algorithm SHA256).Hash -ne $f.sha256){throw 'Source changed'}}
 $r.sourcesUnchanged=$true;$r.ok=$true
}catch{$r.error=$_.Exception.Message;throw}finally{$r.finishedAtUtc=[DateTime]::UtcNow.ToString('o');$r|ConvertTo-Json -Depth 5|Set-Content -LiteralPath ($OutputDirectory+'/receipt.json') -Encoding utf8}
$r|ConvertTo-Json -Depth 4
