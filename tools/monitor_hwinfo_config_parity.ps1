param([string]$Script,[string]$Cases,[string]$Output,[string]$Scratch)
$ErrorActionPreference='Stop'
$source=[IO.File]::ReadAllText($Script)
$end=$source.LastIndexOf('exit (Invoke-HWiNFORecoveryEntry)')
if($end -lt 0){throw 'Production entry boundary missing'}
. ([scriptblock]::Create($source.Substring(0,$end)))
function Get-HWiNFOAcquisitionOverrides { $choices=@{PresentMon=1;NvClockMeasuring=0;USBC_PD=0};if($null -ne $script:FixtureSnapshot){$choices.SnapshotPolling=[int]$script:FixtureSnapshot};return $choices }
function From-Hex([string]$hex){$b=New-Object byte[] ($hex.Length/2);for($i=0;$i -lt $b.Length;$i++){$b[$i]=[Convert]::ToByte($hex.Substring($i*2,2),16)};return ,$b}
$results=New-Object Collections.Generic.List[object]
$all=Get-Content -LiteralPath $Cases -Raw -Encoding UTF8|ConvertFrom-Json
[IO.Directory]::CreateDirectory($Scratch)|Out-Null
foreach($case in $all){
 $dst=Join-Path $Scratch 'HWiNFO64.INI';$src=Join-Path $Scratch 'Source.INI'
 if(Test-Path -LiteralPath $dst){Remove-Item -LiteralPath $dst -Force}
 if(Test-Path -LiteralPath $src){Remove-Item -LiteralPath $src -Force}
 $script:FixtureSnapshot=$null
 if($null -ne $case.snapshotPolling){$script:FixtureSnapshot=[bool]$case.snapshotPolling}
 if($null -ne $case.destinationHex){[IO.File]::WriteAllBytes($dst,(From-Hex $case.destinationHex))}
 $sourcePath='';if($null -ne $case.sourceHex){[IO.File]::WriteAllBytes($src,(From-Hex $case.sourceHex));$sourcePath=$src}
 try{$plan=Get-HWiNFOConfigPlan -Source $sourcePath -Destination $dst -RequestedIntervalMs ([int]$case.period);$hex=[BitConverter]::ToString($plan.Bytes).Replace('-','').ToLowerInvariant();$results.Add(@{name=$case.name;code=0;hex=$hex})}
 catch{$code=1;if($_.Exception.Data.Contains('HWiNFOExitCode')){$code=[int]$_.Exception.Data['HWiNFOExitCode']};$results.Add(@{name=$case.name;code=$code;error=$_.Exception.Message})}
}
[IO.File]::WriteAllText($Output,($results|ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
