[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceRoot,[string]$ApplicationEvidence='D:\Codex\2026-10-18\kai-s\work\hc-followup-trim\application',[string]$RepositoryRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
$ErrorActionPreference='Stop';$e=[IO.Path]::GetFullPath($EvidenceRoot);$current=Join-Path $e 'current-input';$base=Join-Path $e 'software-tests';if(Test-Path -LiteralPath $base){throw 'Software test directory must be new'};New-Item -ItemType Directory -Path $base|Out-Null
$callback=Join-Path $ApplicationEvidence 'callback-tool-build\bin\RuntimeProbe\release\RuntimeProbe.dll';$logger=Join-Path $ApplicationEvidence 'software-tool-build\bin\SoftwareProbe\release\SoftwareProbe.dll';$inspector=Join-Path $ApplicationEvidence 'tool-build\bin\ApplicationTrim\release\ApplicationTrim.dll';$golden=Join-Path $ApplicationEvidence 'callback-golden.json'
function Hash([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
function CopySnapshot([string]$dest){New-Item -ItemType Directory -Path $dest|Out-Null;foreach($f in Get-ChildItem -LiteralPath $current -File -Recurse){$rel=[IO.Path]::GetRelativePath($current,$f.FullName);$p=Join-Path $dest $rel;New-Item -ItemType Directory -Path (Split-Path -Parent $p) -Force|Out-Null;Copy-Item -LiteralPath $f.FullName -Destination $p}}
$targets=@('controller-hidapi.net','hidapi.net','Nefarius.Utilities.Bluetooth','Nefarius.ViGEm.Client','PInvoke.Windows.Core');$targetFile=Join-Path $base 'five-targets.json';[IO.File]::WriteAllText($targetFile,(ConvertTo-Json -InputObject $targets))
$combined=Join-Path $base 'five-leaf-runtime';CopySnapshot $combined
foreach($name in $targets){Copy-Item -LiteralPath (Join-Path $e "$name\linked\$name.dll") -Destination (Join-Path $combined "$name.dll") -Force}
$cases=@([ordered]@{name='current-baseline';runtime=$current;omitted=@();negativeControl=$false},[ordered]@{name='five-leaf-candidates';runtime=$combined;omitted=@();negativeControl=$false})
foreach($test in @([ordered]@{name='omit-bluetooth';omitted=@('Nefarius.Utilities.Bluetooth.dll')},[ordered]@{name='omit-vigem';omitted=@('Nefarius.ViGEm.Client.dll')},[ordered]@{name='omit-both';omitted=@('Nefarius.Utilities.Bluetooth.dll','Nefarius.ViGEm.Client.dll')},[ordered]@{name='omit-hidapi-negative-control';omitted=@('hidapi.net.dll')})){
 $root=Join-Path $base ($test.name+'-runtime');CopySnapshot $root
 foreach($f in $test.omitted){Remove-Item -LiteralPath (Join-Path $root $f)}
 $cases+=[ordered]@{name=$test.name;runtime=$root;omitted=$test.omitted;negativeControl=($test.name -like '*negative-control')}
}
$results=@()
foreach($test in $cases){$name=$test.name;$root=$test.runtime
 & dotnet $inspector contracts $root $targetFile (Join-Path $base "$name-contracts.json") *> (Join-Path $base "$name-contracts.log");$contractExit=$LASTEXITCODE
 & dotnet $callback (Join-Path $root 'HandheldCompanion.dll') (Hash (Join-Path $root 'HandheldCompanion.dll')) $golden (Join-Path $base "$name-callback.json") (Hash (Join-Path $root 'Sentry.dll')) *> (Join-Path $base "$name-callback.log");$callbackExit=$LASTEXITCODE
 & dotnet $logger $root $RepositoryRoot (Join-Path $base "$name-log.json") *> (Join-Path $base "$name-log.log");$logExit=$LASTEXITCODE
 $results+=[ordered]@{name=$name;runtime=$root;omitted=$test.omitted;negativeControl=$test.negativeControl;contractExit=$contractExit;callbackExit=$callbackExit;loggerExit=$logExit;noRealHost=$true;noHardware=$true;noNetwork=$true;manifestBypassedForSoftwareOnly=$true}
 Write-Output "CONTROLLED_SOFTWARE: $name contracts=$contractExit callbacks=$callbackExit logs=$logExit"
}
[IO.File]::WriteAllText((Join-Path $base 'RESULTS.json'),(ConvertTo-Json -InputObject ([ordered]@{schemaVersion=1;cases=$results;sharedCopiesAreExactOriginals=$true;noProductionChanges=$true;warning='A no-file callback pass alone is not deletion proof. Static missing-reference control and actual entry/manifest gates must also be considered.'}) -Depth 12))
