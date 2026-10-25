[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$sourceRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use fresh output; never overwrite evidence'}
[IO.Directory]::CreateDirectory($OutputDirectory)|Out-Null
$receipt=[ordered]@{ok=$false;contract='HIDMaestro-2.0.0-QueuedFrame-observer';startedAtUtc=[DateTime]::UtcNow.ToString('o');phase='freeze';inputs=@();failures=@();fieldFlickerVerified=$false;socketPayloadObserved=$false}
$probe=$null
try{
 foreach($rel in @('tools/steamdeck_sdk_queue_selftest.cs','tools/steamdeck_sdk_queue_selftest.ps1','deps/hidmaestro/HIDMaestro.Core.dll','deps/hidmaestro/dependency-lock.json','InputHost/UsbipTransportObservation.cs','InputHost/SteamDeckPadReportObservation.cs')){
  $src=Join-Path $sourceRoot $rel;$dst=Join-Path $OutputDirectory ([IO.Path]::GetFileName($rel));$hash=(Get-FileHash -LiteralPath $src -Algorithm SHA256).Hash
  Copy-Item -LiteralPath $src -Destination $dst
  if((Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash -ne $hash){throw 'Frozen copy mismatch'}
  $receipt.inputs+=@{source=$src;frozen=$dst;sha256=$hash}
 }
 $approved='8FE7BDB21EC497591F527B7B87C4BC7B41B45C35A06CC211A7A0DCE48838ED2C'
 $sdk=Join-Path $OutputDirectory 'HIDMaestro.Core.dll'
 $lock=Get-Content -LiteralPath (Join-Path $OutputDirectory 'dependency-lock.json') -Raw|ConvertFrom-Json
 $entry=@($lock.files|Where-Object {$_.path -eq 'deps/hidmaestro/HIDMaestro.Core.dll'})
 if((Get-FileHash -LiteralPath $sdk -Algorithm SHA256).Hash -ne $approved -or (Get-Item -LiteralPath $sdk).Length -ne 63076352 -or $entry.Count -ne 1 -or $entry[0].sha256 -ne $approved -or $entry[0].bytes -ne 63076352){throw 'Locked 2.0 SDK hash/size mismatch'}
 $receipt.phase='build'
 $project=Join-Path $OutputDirectory 'QueueProbe.csproj'
 '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><PlatformTarget>x64</PlatformTarget><StartupObject>Program</StartupObject><UseSharedCompilation>false</UseSharedCompilation><NuGetAudit>false</NuGetAudit><DefineConstants>USBIP_OBSERVER_FIXTURE</DefineConstants></PropertyGroup><ItemGroup><Reference Include="HIDMaestro.Core"><HintPath>HIDMaestro.Core.dll</HintPath></Reference></ItemGroup></Project>'|Set-Content -LiteralPath $project -Encoding utf8
 '<configuration><packageSources><clear /></packageSources></configuration>'|Set-Content -LiteralPath (Join-Path $OutputDirectory 'NuGet.Config') -Encoding utf8
 & dotnet build $project -c Release -o (Join-Path $OutputDirectory 'Output') --nologo *> (Join-Path $OutputDirectory 'build.log')
 $receipt.buildExitCode=$LASTEXITCODE;if($LASTEXITCODE -ne 0){throw 'QueueProbe build failed; build.log retained'}
 if((Get-FileHash -LiteralPath (Join-Path $OutputDirectory 'Output/HIDMaestro.Core.dll') -Algorithm SHA256).Hash -ne $approved){throw 'Built SDK copy mismatch'}
 $receipt.phase='private-queue-state'
 $resultPath=Join-Path $OutputDirectory 'result.json'
 $probe=Start-Process -FilePath (Join-Path $OutputDirectory 'Output/QueueProbe.exe') -ArgumentList ('"'+$resultPath+'"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $OutputDirectory 'stdout.txt') -RedirectStandardError (Join-Path $OutputDirectory 'stderr.txt')
 if(-not $probe.WaitForExit(30000)){$probe.Kill($true);$probe.WaitForExit();throw 'Own fixture helper exceeded30s and was stopped; evidence retained'}
 $receipt.exitCode=$probe.ExitCode
 if(-not(Test-Path -LiteralPath $resultPath)){throw 'No result; stdout/stderr retained'}
 $r=Get-Content -LiteralPath $resultPath -Raw|ConvertFrom-Json
 $receipt.checks=$r.checks;$receipt.failures=@($r.cases|Where-Object {!$_.ok}|ForEach-Object {$_.name})
 if($r.PSObject.Properties['error']){$receipt.fixtureError=$r.error}
 if($r.PSObject.Properties['cleanupError']){$receipt.cleanupError=$r.cleanupError}
 foreach($metric in @('sdkConstructorsCalled','inputPumpLoopCalls','unlinkCalls','privateWorkersRemaining','pollingWorkersCreated','busyWaitLoops','socketCalls','deviceDriverCalls','inputCalls','eventOrPhysicalHandlesCreated','environmentChanges')){
  $receipt[$metric]=$r.$metric;if($null -eq $r.$metric -or $r.$metric -ne 0){throw "Scope violation or absent metric: $metric"}
 }
 $receipt.actualHandleSubmitCalls=$r.actualHandleSubmitCalls;$receipt.skipped=$r.skipped;$receipt.privateLockWorkersCreated=$r.privateLockWorkersCreated
 if($probe.ExitCode -ne 0 -or !$r.ok){throw 'QueueProbe assertions failed; evidence retained'}
 foreach($i in $receipt.inputs){if((Get-FileHash -LiteralPath $i.source -Algorithm SHA256).Hash -ne $i.sha256 -or (Get-FileHash -LiteralPath $i.frozen -Algorithm SHA256).Hash -ne $i.sha256){throw 'Input source or frozen bytes changed'}}
 $receipt.sourcesUnchanged=$true;$receipt.resultSha256=(Get-FileHash -LiteralPath $resultPath -Algorithm SHA256).Hash;$receipt.phase='complete';$receipt.ok=$true
}catch{$receipt.error=$_.Exception.Message;throw}finally{
 if($null -ne $probe){$probe.Dispose()}
 $receipt.finishedAtUtc=[DateTime]::UtcNow.ToString('o')
 $receipt|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $OutputDirectory 'receipt.json') -Encoding utf8
}
$receipt|ConvertTo-Json -Depth 8
