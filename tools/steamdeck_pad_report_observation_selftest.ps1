[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop';$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a fresh evidence directory'}
[IO.Directory]::CreateDirectory($OutputDirectory)|Out-Null
$r=[ordered]@{ok=$false;startedAtUtc=[DateTime]::UtcNow.ToString('o');sources=@();actualSdkLoaded=$false;actualControllersCreated=0;actualDeviceReads=0;fieldFlickerVerified=$false}
try{
 foreach($rel in @('InputHost/UsbipTransportObservation.cs','InputHost/SteamDeckPadReportObservation.cs','tools/steamdeck_pad_report_observation_selftest.cs')){
  $src=Join-Path $project $rel;$dst=Join-Path $OutputDirectory ([IO.Path]::GetFileName($rel));$hash=(Get-FileHash -LiteralPath $src -Algorithm SHA256).Hash;Copy-Item -LiteralPath $src -Destination $dst;if((Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash -ne $hash){throw 'Freeze mismatch'};$r.sources+=@{source=$src;frozen=$dst;sha256=$hash}
 }
 '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><DefineConstants>USBIP_OBSERVER_FIXTURE</DefineConstants></PropertyGroup></Project>'|Set-Content -LiteralPath ($OutputDirectory+'/ObservationProbe.csproj') -Encoding utf8
 & dotnet build ($OutputDirectory+'/ObservationProbe.csproj') -c Release -o ($OutputDirectory+'/Output') --nologo *> ($OutputDirectory+'/build.log');if($LASTEXITCODE -ne 0){throw 'Build failed'}
 $run=Start-Process -FilePath ($OutputDirectory+'/Output/ObservationProbe.exe') -ArgumentList ('"'+$OutputDirectory+'/result.json"') -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput ($OutputDirectory+'/stdout.txt') -RedirectStandardError ($OutputDirectory+'/stderr.txt')
 $result=Get-Content -LiteralPath ($OutputDirectory+'/result.json') -Raw|ConvertFrom-Json;$r.exitCode=$run.ExitCode;$r.checks=$result.checks;$r.failures=@($result.cases|Where-Object {!$_.ok}|ForEach-Object {$_.name});$r.allocationWindows=$result.allocationWindows
 if($run.ExitCode -ne 0 -or !$result.ok){throw 'Observer fixture failed'}
 foreach($file in $r.sources){if((Get-FileHash -LiteralPath $file.source -Algorithm SHA256).Hash -ne $file.sha256 -or (Get-FileHash -LiteralPath $file.frozen -Algorithm SHA256).Hash -ne $file.sha256){throw 'Source changed during test'}}
 $r.sourcesUnchanged=$true;$r.ok=$true
}catch{$r.error=$_.Exception.Message;throw}finally{$r.finishedAtUtc=[DateTime]::UtcNow.ToString('o');$r|ConvertTo-Json -Depth 6|Set-Content -LiteralPath ($OutputDirectory+'/receipt.json') -Encoding utf8}
$r|ConvertTo-Json -Depth 4
