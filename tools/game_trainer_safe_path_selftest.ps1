[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ValidationRoot,[string]$BackendPath='')
$ErrorActionPreference='Stop'
if(-not $BackendPath){$BackendPath=Join-Path (Split-Path $PSScriptRoot) 'PowerControl/Trainer/TrainerBackend.psm1'}
Import-Module $BackendPath -Force -DisableNameChecking
$module=Get-Module TrainerBackend
$testRoot=Join-Path $ValidationRoot ('ps'+$PSVersionTable.PSVersion.Major+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
[IO.Directory]::CreateDirectory($testRoot)|Out-Null
$checks=[Collections.Generic.List[object]]::new();$aliases=[Collections.Generic.List[string]]::new();$launches=[Collections.Generic.List[object]]::new()
function Assert($value,[string]$message='assertion failed'){if(-not $value){throw $message}}
function Check([string]$name,[scriptblock]$test){try{& $test;$checks.Add(@{name=$name;ok=$true});Write-Output ('PASS '+$name)}catch{$checks.Add(@{name=$name;ok=$false;error=$_.Exception.Message;stack=$_.ScriptStackTrace});Write-Output ('FAIL '+$name+': '+$_.Exception.Message)}}
function RejectCollision([scriptblock]$test){$message='';try{& $test|Out-Null}catch{$message=$_.Exception.Message};Assert ($message -eq '修改器兼容路径已被其他目录占用') ('Unexpected collision result: '+$message)}
function Json([string]$path,$value){[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))|Out-Null;[IO.File]::WriteAllText($path,($value|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))}
function SafePath([string]$path){& $module {param($p)Get-TrainerSafeLaunchPath $p} $path}
function AliasFor([string]$path){$parent=[IO.Path]::GetDirectoryName($path);$alias=& $module {param($p)Join-Path ([IO.Path]::GetTempPath()) ('YMCC_TrainerLaunch_'+(Get-TrainerHash $p.ToLowerInvariant()).Substring(0,24))} $parent;if(-not $aliases.Contains($alias)){$aliases.Add($alias)};return $alias}
function FixtureFile([string]$relative,[string]$text='fixture'){$path=Join-Path $testRoot $relative;[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))|Out-Null;[IO.File]::WriteAllText($path,$text);AliasFor $path|Out-Null;return $path}
function NewFixtureJunction([string]$path,[string]$target){$literal=if($PSVersionTable.PSVersion.Major -le 5){[WildcardPattern]::Escape($target)}else{$target};New-Item -ItemType Junction -Path $path -Target $literal -ErrorAction Stop|Out-Null}
function LaunchFixture($plan){
 $receipt=& $module {param($p)Start-TrainerLaunchPlan $p} $plan
 $launches.Add($receipt);$marker=Join-Path $testRoot ('launch-'+$receipt.pid+'.txt');$until=[DateTime]::UtcNow.AddSeconds(8)
 while(-not [IO.File]::Exists($marker) -and [DateTime]::UtcNow -lt $until){Start-Sleep -Milliseconds 40}
 Assert ([IO.File]::Exists($marker)) 'Inert process did not write its launch receipt'
 $lines=[IO.File]::ReadAllLines($marker)
 Assert ($lines[0] -eq [string]$receipt.pid) 'PID does not match actual process marker'
 $process=Get-Process -Id $receipt.pid -ErrorAction SilentlyContinue
 if($process){Assert ($process.WaitForExit(6000)) 'Inert process did not finish'}
 return ,$lines
}
$library=Join-Path $testRoot 'library';$settings=Join-Path $testRoot 'settings';$ce=Join-Path $testRoot '[测试工具]引擎 目录'
$trainer=FixtureFile 'library/[风灵]《十字军之王3》修改器/Trainer.exe'
$ct=FixtureFile 'library/[GreenHouse]《忍者神龟：施莱德的复仇》修改器/Trainer.ct' '<CheatTable/>'
$ascii=FixtureFile 'ascii/Trainer.exe'
$settingsPath=Join-Path $settings 'settings.json'
Json $settingsPath @{downloadPath=$library;safePath=$true;launchAsAdmin=$false;launchCTAsAdmin=$false;cePath=$ce}
$settingsHash=(Get-FileHash -LiteralPath $settingsPath).Hash
$markerRoot=$testRoot.Replace('"','""')
$cs=@"
using System;using System.IO;using System.Diagnostics;using System.Threading;
class Fixture {
 [STAThread] static void Main(string[] args) {
  string[] data={Process.GetCurrentProcess().Id.ToString(),Environment.CurrentDirectory,args.Length.ToString(),args.Length>0?args[0]:"",args.Length>0?File.ReadAllText(args[0]):""};
  File.WriteAllLines(Path.Combine(@"$markerRoot","launch-"+data[0]+".txt"),data);
  Thread.Sleep(1200);
 }
}
"@
$csPath=Join-Path $testRoot 'inert.cs';[IO.File]::WriteAllText($csPath,$cs,[Text.UTF8Encoding]::new($true))
$inert=Join-Path $testRoot 'Inert.exe';$csc=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
& $csc /nologo /target:winexe ('/out:'+$inert) $csPath
if($LASTEXITCODE -ne 0){throw 'Inert fixture compilation failed'}
[IO.File]::Copy($inert,$trainer,$true);[IO.Directory]::CreateDirectory($ce)|Out-Null
$ceExe=Join-Path $ce 'Cheat Engine.exe';[IO.File]::Copy($inert,$ceExe);AliasFor $ceExe|Out-Null
$sha=(Get-FileHash -LiteralPath $trainer).Hash;$ctSha=(Get-FileHash -LiteralPath $ct).Hash
try{
 Check 'ASCII path passes through literally without a compatibility alias' {Assert ((SafePath $ascii) -ceq $ascii);Assert (-not [IO.Directory]::Exists((AliasFor $ascii)))}
 Check 'real CK3 author brackets and Chinese create a readable ASCII junction' {
  $safe=SafePath $trainer;Assert ($safe -ceq (Join-Path (AliasFor $trainer) 'Trainer.exe')) 'Fallback branch was not exercised';Assert ($safe -notmatch '[^\x00-\x7f]');Assert ((Get-FileHash -LiteralPath $safe).Hash -eq $sha)
  $item=Get-Item -LiteralPath (AliasFor $trainer) -Force;Assert ($item.LinkType -eq 'Junction');Assert ([string](@($item.Target)[0]) -ieq [IO.Path]::GetDirectoryName($trainer))
 }
 Check 'repeated preparation reuses the exact existing author junction' {Assert ((SafePath $trainer) -ceq (SafePath $trainer));Assert ((Get-FileHash -LiteralPath (SafePath $trainer)).Hash -eq $sha)}
 Check 'CT author brackets, punctuation, spaces and colon preserve literal file contents' {Assert ([IO.File]::ReadAllText((SafePath $ct)) -ceq '<CheatTable/>')}
 Check 'literal backticks in a Unicode author folder survive PowerShell target resolution' {$file=FixtureFile 'library/[风灵]《王国`资料》修改器/Trainer.exe' 'literal backtick';Assert ([IO.File]::ReadAllText((SafePath $file)) -ceq 'literal backtick')}
 Check 'separate author directories with the same filename cannot share an alias' {$other=FixtureFile 'library/[风灵]《维多利亚3》修改器/Trainer.exe' 'other trainer';Assert ((SafePath $other) -cne (SafePath $trainer));Assert ([IO.File]::ReadAllText((SafePath $other)) -ceq 'other trainer');Assert ((Get-FileHash -LiteralPath (SafePath $trainer)).Hash -eq $sha)}
 Check 'an existing ordinary compatibility directory is refused and preserved' {
  $file=FixtureFile 'library/[风灵]目录冲突/Trainer.exe';$alias=AliasFor $file;[IO.Directory]::CreateDirectory($alias)|Out-Null;$sentinel=Join-Path $alias 'sentinel';[IO.File]::WriteAllText($sentinel,'preserve')
  RejectCollision {SafePath $file};Assert ([IO.File]::ReadAllText($sentinel) -ceq 'preserve');[IO.File]::Delete($sentinel);[IO.Directory]::Delete($alias)
 }
 Check 'a junction pointing at another author is refused without replacement' {
  $file=FixtureFile 'wrong-collision/library/[风灵]《十字军之王3》修改器/Trainer.exe';$wrong=Join-Path $testRoot 'wrong-target';[IO.Directory]::CreateDirectory($wrong)|Out-Null;$alias=AliasFor $file;NewFixtureJunction $alias $wrong
  RejectCollision {SafePath $file};Assert ([string](@((Get-Item -LiteralPath $alias -Force).Target)[0]) -ieq $wrong)
 }
 Check 'production EXE plan and OS launch work with GCM safePath enabled' {
  $plan=Resolve-TrainerLaunchPlan -Mode Trainer -TrainerPath $trainer -TrainerFolder ([IO.Path]::GetDirectoryName($trainer)) -ExpectedSha256 $sha -GcmSettingsRoot $settings
  Assert ($plan.safePath -and $plan.verb -eq 'open');$lines=LaunchFixture $plan;Assert ($lines[2] -eq '0');Assert ($lines[1] -ieq (AliasFor $trainer)) 'Working directory does not follow safe path'
 }
 Check 'production CT plan passes one readable table argument through an inert engine' {
  $plan=Resolve-TrainerLaunchPlan -Mode Trainer -TrainerPath $ct -TrainerFolder ([IO.Path]::GetDirectoryName($ct)) -ExpectedSha256 $ctSha -GcmSettingsRoot $settings
  Assert ($plan.safePath -and $plan.verb -eq 'open');$lines=LaunchFixture $plan;Assert ($lines[2] -eq '1') 'Table path was split';Assert ($lines[3] -ceq (SafePath $ct));Assert ($lines[4] -ceq '<CheatTable/>');Assert ($lines[1] -ieq (AliasFor $ceExe))
 }
 Check 'startup leaves GCM preferences and trainer bytes unchanged' {Assert ((Get-FileHash -LiteralPath $settingsPath).Hash -eq $settingsHash);Assert ((Get-FileHash -LiteralPath $trainer).Hash -eq $sha);Assert ((Get-FileHash -LiteralPath $ct).Hash -eq $ctSha)}
}finally{
 foreach($receipt in $launches){$process=Get-Process -Id $receipt.pid -ErrorAction SilentlyContinue;if($process){try{if($process.MainModule.FileName.StartsWith($testRoot,[StringComparison]::OrdinalIgnoreCase)){$process.Kill();$process.WaitForExit(5000)|Out-Null}}catch{}}}
 foreach($alias in $aliases){$item=Get-Item -LiteralPath $alias -Force -ErrorAction SilentlyContinue;if($item -and $item.LinkType -eq 'Junction' -and [IO.Path]::GetFullPath([string](@($item.Target)[0])).StartsWith($testRoot+'\',[StringComparison]::OrdinalIgnoreCase)){[IO.Directory]::Delete($alias)}}
 $failed=@($checks|Where-Object{-not $_.ok});$report=@{powershell=$PSVersionTable.PSVersion.ToString();passed=$checks.Count-$failed.Count;failed=$failed.Count;checks=$checks.ToArray();root=$testRoot;inertLaunches=$launches.Count;realTrainerExecutions=0;networkRequests=0;mockBoundary='Isolated GCM paths and inert EXE/engine only; production path conversion, plan resolution and Windows ShellExecute are real.'}
 Json (Join-Path $testRoot 'safe-path-tests.json') $report
 [pscustomobject]$report|Select-Object powershell,passed,failed,inertLaunches,root|ConvertTo-Json
}
if($failed.Count){exit 1}